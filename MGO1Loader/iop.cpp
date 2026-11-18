// iop.cpp - the IOP side of the sound (iop.h): memory, the recompiled modules' calls, a small IOP
// kernel (threads, semaphores, the hardware timer, vblank waits, memory, the C library), and the
// services the sound modules import from modules this build does not run: libsd (spu2.cpp),
// Konami's SIF manager (jsifman: the EE's memory by the platform) and disc manager (cdbios: the
// platform's reads of cdbios positions).
//
// One IOP thread runs at a time (the baton). A thread gives the baton back when it waits. The
// timer, the EE's commands and the block input's halves are interrupts: the host thread that
// raises one runs it itself when no IOP thread holds the baton, else the running thread runs it
// at its next backward branch or kernel call (as an interrupt preempts it on the IOP), then
// gives the baton to a higher priority thread the interrupt made ready.
//
// The IOP's time is the sound's: it moves only as the sound is mixed (Mix: 768 IOP cycles a 48 kHz
// sample), and the timer, the delays and the vblank waits fall at their sample inside the mix, the
// threads they wake running before the mix goes on. The sequencer's ticks and the voices it
// starts stay in step whatever the pace of the one mixing (the sound device, or the game's frames
// when there is none).
#include "iop.h"
#include "iop_rt.h"
#include "spu2.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace iop {

uint8_t gMem[kMemSize + 64];
std::atomic<int> gPoll{0};

namespace {

Host gHost;
std::atomic<bool> gRunning{false};
void Say(const char* fmt, ...) {
    char b[768];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (gHost.log) gHost.log("iop: %s", b);
}
std::atomic<long long> sTicks{0}, sCommands{0}, sPrints{0}, sQueueReads{0}, sStatusSends{0}, sCdReads{0};
std::atomic<int> gLogXfer{0};                  // LogTransfers: transfers still to log

// ---- time: the IOP's clock (36.864 MHz), moved by the mix (768 cycles a 48 kHz sample)
constexpr uint64_t kCyclesPerSample = 768;
std::atomic<uint64_t> gNow{0};
uint64_t Cycles() { return gNow.load(); }

// ---- functions by address
std::unordered_map<uint32_t, Fn> gFns;
struct Stub { const char* lib; int index; };
std::unordered_map<uint32_t, Stub> gStubs;
std::map<std::pair<std::string, int>, uint32_t> gExports;

// ---- threads
struct Thread {
    int id = 0;
    uint32_t entry = 0, stackBase = 0, stackSize = 0, attr = 0, arg = 0, gp = 0;
    int prio = 64;
    int state = 0;                       // 0 dormant, 1 ready, 2 running, 3 waiting, 5 ended
    int wait = 0;                        // 1 sleep, 2 delay, 3 semaphore, 4 vblank, 5 a disc read on the host
    int wakeups = 0;
    uint64_t due = 0;                    // delay and vblank waits: the cycle they end
    int sema = 0;
    bool released = false;               // the wait was ended by ReleaseWaitThread
    uint64_t seq = 0;
    bool go = false;
    std::condition_variable cv;
    std::thread os;
};
std::vector<std::unique_ptr<Thread>> gThreads;
std::mutex gM;
std::condition_variable gIdle;
int gOwner = 0;                          // the baton: 0 nobody, a thread's id, -1 an interrupt
uint64_t gSeq = 0;
thread_local Thread* tSelf = nullptr;
thread_local int tIntr = 0;
int gSuspend = 0;                        // CpuSuspendIntr depth
struct ThreadEnd {};
// interrupts waiting for the baton: the requester's function and its completion
struct Request { const std::function<void()>* fn; bool done; };
std::deque<Request*> gRequests;
std::condition_variable gDone;

#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void* thread, int priority);
#endif
// an IOP thread's host thread above the game's (THREAD_PRIORITY_HIGHEST): the sound device's mix waits for the one
// holding the baton, which the game's threads must not keep off the processor (docs/SOUND.md)
void HostPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), 2);
#endif
}
Thread* ThreadOf(int id) { return id >= 1 && id <= (int)gThreads.size() ? gThreads[id - 1].get() : nullptr; }
void DispatchLocked() {
    if (gOwner != 0) return;
    if (!gRequests.empty()) { gDone.notify_all(); return; }          // the interrupts first: their requesters run them
    Thread* best = nullptr;
    for (auto& t : gThreads)
        if (t->state == 1 && (!best || t->prio < best->prio || (t->prio == best->prio && t->seq < best->seq))) best = t.get();
    if (best) { best->state = 2; gOwner = best->id; best->go = true; best->cv.notify_one(); }
    else gIdle.notify_all();
}
void ReadyLocked(Thread* t) { t->state = 1; t->wait = 0; t->seq = ++gSeq; gPoll = 1; }
// the running thread gives the baton away and waits for it again
void BlockLocked(std::unique_lock<std::mutex>& l, Thread* t) {
    gOwner = 0;
    DispatchLocked();
    t->cv.wait(l, [&] { return t->go; });
    t->go = false;
}
// the running thread after it made others ready: a higher one takes the baton
void PreemptLocked(std::unique_lock<std::mutex>& l) {
    Thread* t = tSelf;
    if (!t || tIntr) return;
    for (auto& o : gThreads)
        if (o->state == 1 && o->prio < t->prio) { t->state = 1; t->seq = 0; BlockLocked(l, t); return; }
}

// the waiting interrupts, run by the baton's holder (gM held on entry and on return)
void RunRequestsLocked(std::unique_lock<std::mutex>& l) {
    while (!gRequests.empty()) {
        Request* q = gRequests.front();
        gRequests.pop_front();
        l.unlock();
        ++tIntr;
        (*q->fn)();
        --tIntr;
        l.lock();
        q->done = true;
        gDone.notify_all();
    }
}
// IOP code run as an interrupt from a host thread (the scheduler, the EE, the mixer); from IOP
// code (a module's start, a handler) it runs at once
thread_local bool tMixer = false;           // the mix's thread (Mix): its waits for the baton counted
std::atomic<long long> sMixBatonWaits{0}, sMixBatonUs{0}, sMixBatonWorstUs{0};
void Interrupt(const std::function<void()>& fn) {
    if (tIntr > 0) { fn(); return; }
    std::unique_lock<std::mutex> l(gM);
    if (tSelf && gOwner == tSelf->id) { l.unlock(); ++tIntr; fn(); --tIntr; return; }
    Request r{&fn, false};
    gRequests.push_back(&r);
    gPoll = 1;
    std::chrono::steady_clock::time_point t0{};
    bool waited = false;
    while (!r.done) {
        if (gOwner == 0) {
            gOwner = -1;
            RunRequestsLocked(l);
            gOwner = 0;
            DispatchLocked();
        } else {
            if (tMixer && !waited) { waited = true; t0 = std::chrono::steady_clock::now(); }
            gDone.wait(l);
        }
    }
    if (waited) {                                // an IOP thread held the baton: the mix waited for its next poll
        const long long us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        ++sMixBatonWaits; sMixBatonUs += us;
        long long w = sMixBatonWorstUs.load();
        while (us > w && !sMixBatonWorstUs.compare_exchange_weak(w, us)) {}
    }
}
constexpr uint32_t kIntrStack = 0x1ffff0;   // interrupts run on their own stack at the top of memory (above the heap)

uint32_t Run(uint32_t fn, std::initializer_list<uint32_t> args, uint32_t sp = kIntrStack) {
    Ctx c{};
    int k = 4;
    for (uint32_t a : args) c.r[k++] = (uint64_t)(int64_t)(int32_t)a;
    c.r[29] = sp - 64; c.r[31] = 0;
    Call(c, fn);
    return (uint32_t)c.r[2];
}

// ---- the hardware timer (timrman) and the other waits the mix ends
struct Timer { bool used = false, running = false; uint32_t compare = 0, handler = 0, arg = 0; uint64_t start = 0, next = 0; };
Timer gTimers[4];
uint64_t gVblankCycles = 36864000ull * 1001 / 60000;               // NTSC fields (59.94 Hz); PAL 50 Hz (SetPalFields)
uint64_t NextVblank() { return (Cycles() / gVblankCycles + 1) * gVblankCycles; }
// the next cycle something waits for (a timer, a delay, a vblank)
uint64_t NextEvent() {
    std::lock_guard<std::mutex> l(gM);
    uint64_t next = UINT64_MAX;
    for (auto& t : gTimers) if (t.used && t.running && t.handler) next = std::min(next, t.next);
    for (auto& t : gThreads) if (t->state == 3 && (t->wait == 2 || t->wait == 4)) next = std::min(next, t->due);
    return next;
}
// what is due at the current cycle: the waits that ended, the timers' handlers (as interrupts:
// their answer is the next interval, 0 stops the timer); true when something ran
bool RunDue() {
    bool any = false;
    {
        std::lock_guard<std::mutex> l(gM);
        const uint64_t now = Cycles();
        for (auto& t : gThreads)
            if (t->state == 3 && (t->wait == 2 || t->wait == 4) && t->due <= now) { ReadyLocked(t.get()); any = true; }
        if (any && gOwner == 0) DispatchLocked();
    }
    for (int k = 0; k < 4; ++k) {
        Timer& t = gTimers[k];
        if (!t.used || !t.running || !t.handler || Cycles() < t.next) continue;
        any = true;
        Interrupt([&] {
            ++sTicks;
            const uint32_t again = Run(t.handler, {t.arg});
            t.next += again ? again : t.compare;
            if (Cycles() > t.next + 36864000ull) t.next = Cycles();          // a long gap: no burst of ticks
            if (!again) t.running = false;
        });
    }
    return any;
}

// ---- memory for the modules (sysmem): first fit over the free space above the modules
std::map<uint32_t, uint32_t> gBlocks;          // allocated: address -> size
uint32_t gHeapLo = 0x140000, gHeapHi = 0x1f0000;
uint32_t Alloc(uint32_t size, int type, uint32_t at) {
    size = (size + 255) & ~255u;
    if (type == 2) { gBlocks[at] = size; return at; }
    std::vector<std::pair<uint32_t, uint32_t>> used(gBlocks.begin(), gBlocks.end());
    if (type == 1) {                                                   // from the top
        uint32_t top = gHeapHi;
        for (auto it = used.rbegin(); it != used.rend(); ++it) {
            if (it->first + it->second <= top - size && top - size >= gHeapLo) break;
            top = std::min(top, it->first);
        }
        if (top < gHeapLo + size) return 0;
        gBlocks[top - size] = size;
        return top - size;
    }
    uint32_t a = gHeapLo;
    for (auto& b : used) { if (a + size <= b.first) break; a = std::max(a, b.first + b.second); }
    if (a + size > gHeapHi) return 0;
    gBlocks[a] = size;
    return a;
}

// ---- the C library's formatted output over IOP arguments (the o32 ABI: a0..a3, then the stack)
std::string Format(Ctx& c, uint32_t fmt, int first) {
    std::string out;
    int k = first;
    auto arg = [&]() -> uint32_t { const int i = k++; return i < 4 ? (uint32_t)c.r[4 + i] : R32((uint32_t)c.r[29] + 4 * i); };
    for (uint32_t p = fmt; ; ++p) {
        char ch = (char)R8(p);
        if (!ch || out.size() > 1000) break;
        if (ch != '%') { out += ch; continue; }
        std::string spec = "%";
        for (;;) {
            ch = (char)R8(++p);
            if (!ch) return out;
            if (ch == 'l' || ch == 'h') continue;
            spec += ch;
            if (strchr("diouxXcsp%", ch)) break;
        }
        char b[256];
        const char conv = spec.back();
        if (conv == '%') out += '%';
        else if (conv == 's') {
            const uint32_t s = arg();
            std::string str;
            for (uint32_t q = s; q && str.size() < 200; ++q) { const char x = (char)R8(q); if (!x) break; str += x; }
            snprintf(b, sizeof b, spec.c_str(), str.c_str()); out += b;
        } else if (conv == 'c') { snprintf(b, sizeof b, spec.c_str(), (int)arg()); out += b; }
        else if (conv == 'p') { snprintf(b, sizeof b, "%08x", arg()); out += b; }
        else { snprintf(b, sizeof b, spec.c_str(), arg()); out += b; }
    }
    return out;
}
uint32_t StrLen(uint32_t s) { uint32_t n = 0; while (R8(s + n) && n < 0x100000) ++n; return n; }

// ---- Konami's SIF manager (jsifman) and disc manager (cdbios), as the sound driver uses them
struct CmdHandler { uint32_t fn = 0, arg = 0; };
CmdHandler gCmd[32];
constexpr uint32_t kSlot = 0x1f0400;           // the completion word of a read of EE memory (bit 31: done)
constexpr uint32_t kPacket = 0x1f0000;         // an EE command as the IOP's handler reads it
uint32_t R32e(const uint8_t* p, int o) { uint32_t v; memcpy(&v, p + o, 4); return v; }
uint8_t* Ee(uint32_t a, uint32_t n) {
    a &= 0x01ffffff;
    return gHost.eeMem && a + n <= 0x02000000 ? gHost.eeMem + a : nullptr;
}

// ---- libsd's transfer interrupt handlers (the block input's halves)
struct TransHandler { uint32_t fn = 0, arg = 0; };
TransHandler gTrans[2];
std::mutex gHalfLock;
std::deque<int> gHalves;                       // block halves played, their interrupts not run yet

// ---- libsd's effects: its presets (the ten modes' 32 register values) and work area sizes (in
// 8-byte units), read from the disc's libsd.irx (the build this code knows)
struct Effects { bool ok = false; uint16_t regs[10][32]; uint32_t size[10]; };
Effects gFx;
uint32_t gFxMode[2] = {0, 0};
constexpr uint32_t kLibsdSize = 0x5ce0, kLibsdPresets = 0x4de0, kLibsdSizes = 0x4db8;
constexpr uint64_t kLibsdHash = 0xb87f77cb9a7de949ull;

// ---- The driver's effects (sddrv_mn). An effect's start (0x11A248, a0 its 0x43 command: +0 its code, +0xC the EE's
// handle) takes a channel: 44 of 0x2A8 bytes from 0x126D30 (+0x14 the handle, +0x2A4 the driver's serial of the
// start). Its notes hand sdlib a voice's attributes (sdlib #12 from 0x11BD7C, a0 the voice's: +0 what is set (0x40
// its sample's address), +4 and +8 the voices of core 0 and 1, +0x18 the sample, +0x20 its channel), then key it on.
constexpr uint32_t kChannels = 0x126d30, kChannelSize = 0x2a8;
constexpr int kChannelCount = 44;
Fn gSeStartFn = nullptr;
int ChannelOf(uint32_t handle, uint32_t& serial) {        // the channel an effect took (the latest start of that handle)
    int ch = -1;
    serial = 0;
    for (int k = 0; k < kChannelCount; ++k) {
        const uint32_t a = kChannels + kChannelSize * (uint32_t)k;
        if (R32(a + 0x14) == handle && (ch < 0 || R32(a + 0x2a4) >= serial)) { ch = k; serial = R32(a + 0x2a4); }
    }
    return ch;
}
// IOP_SE_PROBE=N@seconds (checks): from then, N lines: the effects' starts (the channel each took) and the voices'
// attributes and switches the driver hands sdlib (#12, #13: their words, the caller's saved registers)
uint64_t gSeProbeFrom = UINT64_MAX;
std::atomic<int> gSeProbeLeft{0};
bool SeProbeOn() { return gSeProbeLeft.load() > 0 && Cycles() >= gSeProbeFrom; }
// The added weapons' own sounds (AddedWeaponSound): the handles the EE marked, then the channels their starts took;
// a channel's first voice keyed on plays the added weapon's sound (spu2::Substitute), its other voices are silent
struct Own { std::shared_ptr<const spu2::Pcm> pcm; float gain = 1; uint64_t cut = 0; std::string what; uint64_t at = 0; };
std::mutex gOwnLock;
std::map<uint32_t, Own> gOwnHandles;
struct ChanOwn { Own own; uint32_t serial = 0; bool first = true; };
std::map<int, ChanOwn> gOwnChannels;
std::atomic<int> gOwnCount{0};                             // handles and channels marked (none: nothing to look at)
int gOwnLogged = 0;
std::atomic<long long> sOwnMarked{0}, sOwnStarted{0}, sOwnHeard{0}, sOwnLost{0};   // for the log (Stats)
void SeStart(Ctx& c) {
    const uint32_t cmd = (uint32_t)c.r[4], w0 = R32(cmd), w1 = R32(cmd + 4), w2 = R32(cmd + 8), w3 = R32(cmd + 12);
    Own own;
    bool have = false;
    if (gOwnCount.load()) {
        std::lock_guard<std::mutex> g(gOwnLock);
        auto it = gOwnHandles.find(w3);
        if (it != gOwnHandles.end()) { own = it->second; have = true; gOwnHandles.erase(it); }
    }
    gSeStartFn(c);
    const bool probe = SeProbeOn();
    if (!have && !probe) return;
    uint32_t serial = 0;
    const int ch = ChannelOf(w3, serial);
    if (probe) {
        --gSeProbeLeft;
        Say("se probe: %.3f s: start %08x %08x %08x %08x: channel %d (serial %u)", Cycles() / (double)kCyclesPerSample / 48000.0, w0, w1, w2, w3, ch, serial);
    }
    if (!have) return;
    std::lock_guard<std::mutex> g(gOwnLock);
    if (ch >= 0) { gOwnChannels[ch] = {own, serial, true}; ++sOwnStarted; }
    else ++sOwnLost;
    gOwnCount = (int)(gOwnHandles.size() + gOwnChannels.size());
    if (gOwnLogged < 40) {
        ++gOwnLogged;
        if (ch >= 0) Say("sound: effect %03x (handle %#x) heard as %s: its channel %d", w0 & 0xfff, w3, own.what.c_str(), ch);
        else Say("sound: effect %03x (handle %#x): no channel taken, %s not heard", w0 & 0xfff, w3, own.what.c_str());
    }
}
// sdlib #12: a note's voice attributes; the voices of a marked channel given the sound (the first) or silence
void NoteVoices(uint32_t attr) {
    if (!gOwnCount.load() || !(R32(attr) & 0x40)) return;
    const uint32_t ch = R32(attr + 0x20);
    if (ch < kChannels || ch >= kChannels + kChannelSize * (uint32_t)kChannelCount || (ch - kChannels) % kChannelSize) return;
    const int k = (int)((ch - kChannels) / kChannelSize);
    std::lock_guard<std::mutex> g(gOwnLock);
    auto it = gOwnChannels.find(k);
    if (it == gOwnChannels.end()) return;
    if (R32(ch + 0x2a4) != it->second.serial) {                  // the channel is another start's now
        gOwnChannels.erase(it);
        gOwnCount = (int)(gOwnHandles.size() + gOwnChannels.size());
        return;
    }
    for (int core = 0; core < 2; ++core) {
        const uint32_t mask = R32(attr + 4 + 4 * (uint32_t)core) & 0xffffff;
        for (int v = 0; v < 24; ++v) {
            if (!(mask >> v & 1)) continue;
            ChanOwn& r = it->second;
            spu2::Substitute(core, v, r.first ? r.own.pcm : nullptr, r.own.gain, r.own.cut);
            if (r.first) ++sOwnHeard;
            if (r.first && gOwnLogged < 40) { ++gOwnLogged; Say("sound: channel %d's voice %d of core %d plays %s", k, v, core, r.own.what.c_str()); }
            r.first = false;
        }
    }
}

// ---- the services the modules import
const bool gTraceServices = getenv("IOP_TRACE") != nullptr;
std::mutex gTraceLock;
int32_t Service(Ctx& c, const Stub& s) {
    const std::string lib = s.lib;
    const int n = s.index;
    const uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5], a2 = (uint32_t)c.r[6], a3 = (uint32_t)c.r[7];
    auto stackArg = [&](int i) { return R32((uint32_t)c.r[29] + 4 * i); };
    if (gTraceServices) {                                          // IOP_TRACE=1: the first calls of each service
        static std::map<std::pair<std::string, int>, int> calls;
        std::lock_guard<std::mutex> g(gTraceLock);
        if (calls[{lib, n}]++ < 6) Say("trace %s #%d (%08x %08x %08x %08x) from %06x", lib.c_str(), n, a0, a1, a2, a3, (uint32_t)c.r[31]);
    }
    if (lib == "sdlib" && n == 12) NoteVoices(a0);
    if (lib == "sdlib" && (n == 12 || n == 13) && SeProbeOn()) {
        --gSeProbeLeft;
        const uint32_t p = n == 12 ? a0 : a1;
        char w[128] = "";
        for (int k = 0; k < 10; ++k) snprintf(w + strlen(w), sizeof w - strlen(w), " %08x", R32(p + 4 * (uint32_t)k));
        Say("se probe: %.3f s: sdlib #%d (%08x %08x) [%s] s0-s7 %06x %06x %06x %06x %06x %06x %06x %06x fp %06x from %06x",
            Cycles() / (double)kCyclesPerSample / 48000.0, n, a0, a1, w, (uint32_t)c.r[16], (uint32_t)c.r[17], (uint32_t)c.r[18],
            (uint32_t)c.r[19], (uint32_t)c.r[20], (uint32_t)c.r[21], (uint32_t)c.r[22], (uint32_t)c.r[23], (uint32_t)c.r[30], (uint32_t)c.r[31]);
    }
    if (lib == "sdlib") {                                          // another recompiled module's export
        auto it = gExports.find({lib, n});
        if (it != gExports.end()) { Call(c, it->second); return (int32_t)c.r[2]; }
    } else if (lib == "libsd") {
        switch (n) {
        case 4: spu2::Reset(); return 0;                                        // sceSdInit
        case 5:
            if (gTraceServices && ((a0 >> 8) & 0xff) >= 8) {                  // the cores' volumes and mix
                static int logged = 0;
                if (logged++ < 200) Say("libsd: core %u parameter %02x = %04x", a0 & 1, (a0 >> 8) & 0xff, a1 & 0xffff);
            }
            spu2::SetParam(a0, a1); return 0;
        case 6: return (int32_t)spu2::GetParam(a0);
        case 7:
            if (gTraceServices && ((a0 >> 8) & 0xff) >= 0x17) {                // the voices' mix (not the key ons)
                static int logged = 0;
                if (logged++ < 200) Say("libsd: core %u switch %02x = %06x", a0 & 1, (a0 >> 8) & 0xff, a1 & 0xffffff);
            }
            spu2::SetSwitch(a0, a1); return 0;
        case 8: return (int32_t)spu2::GetSwitch(a0);
        case 9: spu2::SetAddr(a0, a1); return 0;
        case 10: return (int32_t)spu2::GetAddr(a0);
        case 11:
            if (gTraceServices) { static int logged = 0; if (logged++ < 100) Say("libsd: core %u attribute %u = %x", a0 & 1, (a0 >> 1) & 7, a1); }
            spu2::SetCoreAttr(a0, a1); return 0;
        case 12: return (int32_t)spu2::GetCoreAttr(a0);
        case 13: {                                                              // sceSdNote2Pitch(center note, fine, note, fine)
            const double semis = ((int)(a2 & 0xffff) - (int)(a0 & 0xffff)) + (((int)(int16_t)a3) - (int)(int16_t)a1) / 128.0;
            return (int32_t)std::min(0x3fff, (int)lrint(4096.0 * std::pow(2.0, semis / 12.0))); }
        case 17: {                                                              // sceSdVoiceTrans(channel, mode, iop, spu, size)
            const uint32_t size = stackArg(4);
            static int logged = 0;
            if (gTraceServices && logged++ < 200) Say("libsd: voice transfer %s IOP %06x, sound memory %06x, %#x bytes", (a1 & 1) ? "to" : "from", Ph(a2), a3, size);
            if (size > 0x200000) return -1;
            // the sound memory address of a transfer goes by blocks of 16 bytes (the ADPCM blocks' grid):
            // Konami's driver loads a sound bank at an address 8 bytes past it (0x42B58 ...) and points
            // its voices at the block below each sample (0x53988 -> 0x53980)
            const uint32_t spuAt = a3 & ~15u;
            if (a1 & 1) spu2::Read(spuAt, gMem + Ph(a2), std::min(size, kMemSize - Ph(a2)));
            else spu2::Write(spuAt, gMem + Ph(a2), std::min(size, kMemSize - Ph(a2)));
            return (int32_t)size; }
        case 18: {                                                              // sceSdBlockTrans(channel, mode, iop, size)
            if ((a1 & 3) == 2) { spu2::BlockStop(a0 & 1); return 0; }         // STOP
            spu2::BlockStart(a0 & 1, gMem, Ph(a2), a3, (a1 & 0x10) != 0);
            Say("libsd: block input of core %u from %06x, %#x bytes%s", a0 & 1, Ph(a2), a3, (a1 & 0x10) ? ", looping" : "");
            return 0; }
        case 19: return 1;                                                      // sceSdVoiceTransStatus: done
        case 20: return (int32_t)spu2::BlockStatus(a0 & 1);                     // sceSdBlockTransStatus
        case 21: case 22: return 0;
        case 23: {                                                              // sceSdSetEffectAttr(core, attr)
            // as libsd does it: attr {core, mode (bit 8: clear the work areas), depth L, depth R
            // (16-bit), delay, feedback}; the depths are the effect volume, the preset's values
            // (echo and delay: moved by the delay, the feedback in vWALL) go to the registers and the
            // work area ends at the core's EEA
            const int core = a0 & 1;
            const uint32_t modeW = R32(a1 + 4), mode = modeW & 0xff;
            if (mode >= 10) return -100;
            spu2::SetParam(0x0b00 | core, R16(a1 + 8));
            spu2::SetParam(0x0c00 | core, R16(a1 + 10));
            if (!gFx.ok) return 0;
            uint16_t r[32];
            memcpy(r, gFx.regs[mode], sizeof r);
            if (mode == 7 || mode == 8) {
                const uint32_t d = R32(a1 + 12) + 1, fb = R32(a1 + 16);
                r[10] = (uint16_t)((d << 6) - r[0]); r[11] = (uint16_t)((d << 5) - r[1]);
                r[16] = (uint16_t)(r[17] + (d << 5)); r[12] = (uint16_t)(r[13] + (d << 5));
                r[27] = (uint16_t)(r[29] + (d << 5)); r[26] = (uint16_t)(r[28] + (d << 5));
                r[7] = (uint16_t)(fb * 258);
            }
            const uint32_t eea = spu2::GetAddr(0x1d00 | core) | 0x1ffff;
            if (modeW & 0x100) spu2::Clear(eea - (gFx.size[gFxMode[core]] * 8 - 1), eea);
            if (modeW & 0x100) spu2::Clear(eea - (gFx.size[mode] * 8 - 1), eea);
            spu2::SetReverb(core, r, eea - (gFx.size[mode] * 8 - 1));
            gFxMode[core] = mode;
            Say("libsd: core %d reverb mode %u, depth %04x %04x, delay %u, feedback %u, work area %06x-%06x", core, mode, R16(a1 + 8), R16(a1 + 10),
                R32(a1 + 12), R32(a1 + 16), eea - (gFx.size[mode] * 8 - 1), eea);
            return 0; }
        case 24: return 0;
        case 25: {                                                              // sceSdClearEffectWorkArea(core, channel, mode)
            const uint32_t mode = a2 & 0xff;
            if (mode >= 10 || !gFx.ok) return 0;
            const uint32_t eea = spu2::GetAddr(0x1d00 | (a0 & 1)) | 0x1ffff;
            spu2::Clear(eea - (gFx.size[mode] * 8 - 1), eea);
            return 0; }
        case 26: gTrans[a0 & 1] = {a1, a2}; return 0;                           // sceSdSetTransIntrHandler(channel, fn, arg)
        }
    } else if (lib == "sysmem") {
        if (n == 4) { std::lock_guard<std::mutex> g(gM); const uint32_t p = Alloc(a1, (int)a0, a2); Say("sysmem: %#x bytes (type %u) -> %06x", a1, a0, p); return (int32_t)p; }
        if (n == 5) { std::lock_guard<std::mutex> g(gM); gBlocks.erase(a0); return 0; }
        if (n == 14) { if (++sPrints < 200) Say("Kprintf: %s", Format(c, a0, 1).c_str()); return 0; }
    } else if (lib == "loadcore") {
        return 0;                                                               // FlushDcache, RegisterLibraryEntries
    } else if (lib == "intrman") {
        std::lock_guard<std::mutex> g(gM);
        if (n == 17) { if (a0) W32(a0, (uint32_t)gSuspend); ++gSuspend; return 0; }            // CpuSuspendIntr
        if (n == 18) { if (gSuspend > 0) --gSuspend; return 0; }                                 // CpuResumeIntr
        return 0;
    } else if (lib == "stdio") {
        if (n == 4) { if (++sPrints < 400) Say("printf: %s", Format(c, a0, 1).c_str()); return 0; }
    } else if (lib == "sysclib") {
        switch (n) {
        case 11: for (uint32_t k = 0; k < a2; ++k) { int d = (int)R8(a0 + k) - (int)R8(a1 + k); if (d) return d; } return 0;   // memcmp
        case 12: for (uint32_t k = 0; k < a2; ++k) W8(a0 + k, R8(a1 + k)); return (int32_t)a0;                                // memcpy
        case 13: if (a0 < a1) { for (uint32_t k = 0; k < a2; ++k) W8(a0 + k, R8(a1 + k)); }
                 else { for (uint32_t k = a2; k-- > 0;) W8(a0 + k, R8(a1 + k)); } return (int32_t)a0;                         // memmove
        case 14: for (uint32_t k = 0; k < a2; ++k) W8(a0 + k, a1); return (int32_t)a0;                                        // memset
        case 19: { const std::string s = Format(c, a1, 2); for (size_t k = 0; k <= s.size(); ++k) W8(a0 + (uint32_t)k, k < s.size() ? s[k] : 0); return (int32_t)s.size(); }
        case 20: { uint32_t d = a0 + StrLen(a0); for (uint32_t k = 0;; ++k) { uint8_t x = R8(a1 + k); W8(d + k, x); if (!x) break; } return (int32_t)a0; }   // strcat
        case 22: for (uint32_t k = 0;; ++k) { int x = R8(a0 + k), y = R8(a1 + k); if (x != y || !x) return x - y; }          // strcmp
        case 23: for (uint32_t k = 0;; ++k) { uint8_t x = R8(a1 + k); W8(a0 + k, x); if (!x) break; } return (int32_t)a0;     // strcpy
        case 27: return (int32_t)StrLen(a0);
        case 40: for (uint32_t k = 0; k < a2 / 4; ++k) W32(a0 + 4 * k, R32(a1 + 4 * k)); return (int32_t)a0;                  // wmemcopy
        case 41: for (uint32_t k = 0; k < a2 / 4; ++k) W32(a0 + 4 * k, a1); return (int32_t)a0;                               // wmemset
        }
    } else if (lib == "thbase") {
        std::unique_lock<std::mutex> l(gM);
        Thread* self = tIntr ? nullptr : tSelf;
        switch (n) {
        case 4: {                                                               // CreateThread({attr, option, entry, stack, prio})
            auto t = std::make_unique<Thread>();
            t->id = (int)gThreads.size() + 1;
            t->attr = R32(a0); t->entry = R32(a0 + 8); t->stackSize = R32(a0 + 12); t->prio = (int)R32(a0 + 16);
            t->stackBase = Alloc(t->stackSize ? t->stackSize : 0x800, 1, 0);
            t->gp = (uint32_t)c.r[28];
            Say("CreateThread %d: entry %06x, stack %#x at %06x, priority %d", t->id, t->entry, t->stackSize, t->stackBase, t->prio);
            gThreads.push_back(std::move(t));
            return (int32_t)gThreads.size(); }
        case 6: case 7: {                                                       // StartThread(id, arg)
            Thread* t = ThreadOf((int)a0);
            if (!t || t->state != 0) return -1;
            t->arg = a1;
            Thread* raw = t;
            t->os = std::thread([raw] {
                tSelf = raw;
                HostPriority();
                { std::unique_lock<std::mutex> w(gM); raw->cv.wait(w, [&] { return raw->go; }); raw->go = false; }
                try {
                    Ctx tc{};
                    tc.r[4] = raw->arg; tc.r[28] = raw->gp; tc.r[29] = raw->stackBase + raw->stackSize - 16; tc.r[31] = 0;
                    Call(tc, raw->entry);
                } catch (ThreadEnd&) {}
                std::lock_guard<std::mutex> w(gM);
                raw->state = 5; gOwner = 0; DispatchLocked();
            });
            t->os.detach();
            ReadyLocked(t);
            if (self) PreemptLocked(l);
            else if (gOwner == 0) DispatchLocked();
            return 0; }
        case 8: case 9: {                                                       // ExitThread
            l.unlock();
            throw ThreadEnd();
        }
        case 14: {                                                              // ChangeThreadPriority(id, prio)
            Thread* t = a0 ? ThreadOf((int)a0) : self;
            if (t) t->prio = (int)a1;
            if (self) PreemptLocked(l);
            return 0; }
        case 19: case 18: {                                                     // (i)ReleaseWaitThread(id)
            Thread* t = ThreadOf((int)a0);
            if (t && t->state == 3 && t->wait != 5) { t->released = true; ReadyLocked(t); if (self) PreemptLocked(l); }   // (not a disc read)
            return 0; }
        case 20: return self ? self->id : (tSelf ? tSelf->id : -1);             // GetThreadId
        case 24: {                                                              // SleepThread
            if (!self) return -1;
            if (self->wakeups > 0) { --self->wakeups; return 0; }
            self->state = 3; self->wait = 1; self->released = false;
            BlockLocked(l, self);
            return self->released ? -418 : 0; }
        case 25: case 26: {                                                     // (i)WakeupThread(id)
            Thread* t = ThreadOf((int)a0);
            if (!t) return -1;
            if (t->state == 3 && t->wait == 1) { ReadyLocked(t); if (self) PreemptLocked(l); }
            else ++t->wakeups;
            return 0; }
        case 27: case 28: {                                                     // (i)CancelWakeupThread(id)
            Thread* t = a0 ? ThreadOf((int)a0) : self;
            if (!t) return -1;
            const int w = t->wakeups; t->wakeups = 0; return w; }
        case 33: {                                                              // DelayThread(usec)
            if (!self) return -1;
            self->state = 3; self->wait = 2; self->released = false;
            self->due = Cycles() + (uint64_t)a0 * 36864 / 1000;
            BlockLocked(l, self);
            return 0; }
        case 34: { const uint64_t cy = Cycles(); W32(a0, (uint32_t)cy); W32(a0 + 4, (uint32_t)(cy >> 32)); return 0; }   // GetSystemTime
        case 39: { const uint64_t cy = (uint64_t)a0 * 36864 / 1000; W32(a1, (uint32_t)cy); W32(a1 + 4, (uint32_t)(cy >> 32)); return 0; }  // USec2SysClock
        case 40: {                                                              // SysClock2USec(clock, sec, usec)
            const uint64_t cy = (uint64_t)R32(a0) | (uint64_t)R32(a0 + 4) << 32;
            const uint64_t us = cy * 1000 / 36864;
            W32(a1, (uint32_t)(us / 1000000)); W32(a2, (uint32_t)(us % 1000000)); return 0; }
        }
    } else if (lib == "thsemap") {
        struct Sema { int count, max; bool live; };
        static std::vector<Sema> semas;
        std::unique_lock<std::mutex> l(gM);
        Thread* self = tIntr ? nullptr : tSelf;
        switch (n) {
        case 4: semas.push_back({(int)R32(a0 + 8), std::max(1, (int)R32(a0 + 12)), true}); return (int32_t)semas.size();
        case 5: if (a0 >= 1 && a0 <= semas.size()) semas[a0 - 1].live = false; return 0;
        case 6: case 7: {                                                        // (i)SignalSema
            if (a0 < 1 || a0 > semas.size()) return -1;
            for (auto& t : gThreads)
                if (t->state == 3 && t->wait == 3 && t->sema == (int)a0) { ReadyLocked(t.get()); if (self) PreemptLocked(l); return 0; }
            Sema& s = semas[a0 - 1];
            if (s.count < s.max) ++s.count;
            return 0; }
        case 8: {                                                               // WaitSema
            if (a0 < 1 || a0 > semas.size()) return -1;
            Sema& s = semas[a0 - 1];
            if (s.count > 0) { --s.count; return 0; }
            if (!self) return -419;
            self->state = 3; self->wait = 3; self->sema = (int)a0; self->released = false;
            BlockLocked(l, self);
            return self->released ? -418 : 0; }
        case 9: {                                                               // PollSema
            if (a0 < 1 || a0 > semas.size()) return -1;
            Sema& s = semas[a0 - 1];
            if (s.count > 0) { --s.count; return 0; }
            return -419; }
        }
    } else if (lib == "timrman") {
        std::lock_guard<std::mutex> g(gM);
        switch (n) {
        case 4: for (int k = 0; k < 4; ++k) if (!gTimers[k].used) { gTimers[k] = Timer(); gTimers[k].used = true; return k + 1; } return -1;   // AllocHardTimer
        case 6: if (a0 >= 1 && a0 <= 4) gTimers[a0 - 1].used = false; return 0;
        case 10: if (a0 >= 1 && a0 <= 4) return (int32_t)(uint32_t)(Cycles() - gTimers[a0 - 1].start); return 0;     // GetTimerCounter
        case 20: if (a0 >= 1 && a0 <= 4) { gTimers[a0 - 1].compare = a1; gTimers[a0 - 1].handler = a2; gTimers[a0 - 1].arg = a3; } return 0;   // SetTimerHandler
        case 21: case 22: return 0;                                             // SetOverflowHandler, SetupHardTimer
        case 23: if (a0 >= 1 && a0 <= 4) { Timer& t = gTimers[a0 - 1]; t.running = true; t.start = Cycles(); t.next = t.start + (t.compare ? t.compare : 36864); Say("timer %u: every %u cycles (%.2f ms)", a0, t.compare, t.compare / 36864.0); } return 0;
        case 24: if (a0 >= 1 && a0 <= 4) gTimers[a0 - 1].running = false; return 0;
        }
    } else if (lib == "vblank") {
        if (n == 5 || n == 7 || n == 4 || n == 6) {                             // WaitVblankEnd, WaitNonVblank ...
            std::unique_lock<std::mutex> l(gM);
            Thread* self = tIntr ? nullptr : tSelf;
            if (!self) return 0;
            self->state = 3; self->wait = 4; self->due = NextVblank();
            BlockLocked(l, self);
            return 0;
        }
    } else if (lib == "jsifman") {
        switch (n) {
        case 4:                                                                 // the handler of an EE command channel
            if (a0 < 32) gCmd[a0] = {a1, a2};
            Say("jsifman: channel %u handled by %06x (argument %06x)", a0, a1, a2);
            return 0;
        case 9: {                                                               // IOP memory to the EE (ee, iop, size)
            if (sStatusSends < 4 || (gLogXfer > 0 && gLogXfer-- > 0)) Say("jsifman: IOP %06x to EE %08x, %#x bytes (cycle %llu)", Ph(a1), a0, a2, (unsigned long long)Cycles());
            if (uint8_t* d = Ee(a0, a2)) { memcpy(d, gMem + Ph(a1), std::min(a2, kMemSize - Ph(a1))); ++sStatusSends; }
            return 0; }
        case 17: {                                                              // EE memory to the IOP (iop, ee, size): done at once
            if (sQueueReads < 4 || (gLogXfer > 0 && gLogXfer-- > 0)) Say("jsifman: EE %08x to IOP %06x, %#x bytes (cycle %llu)", a1, Ph(a0), a2, (unsigned long long)Cycles());
            if (uint8_t* s = Ee(a1, a2)) { memcpy(gMem + Ph(a0), s, std::min(a2, kMemSize - Ph(a0))); ++sQueueReads; }
            W32(kSlot, 0x80000000u);
            return (int32_t)kSlot; }
        case 12: return 0;                                                      // the transfer's slot back
        }
    } else if (lib == "cdbios") {
        switch (n) {
        case 4: {                                                               // read(position, buffer, size)
            ++sCdReads;
            const uint32_t size = std::min(a2, kMemSize - Ph(a1));
            // the baton given back while the host reads (the drive reads by DMA while the other threads and the
            // interrupts go on): the mix's interrupts do not wait for the disc (docs/SOUND.md)
            static const bool release = !getenv("MGO1_IOP_READ_RELEASE") || atoi(getenv("MGO1_IOP_READ_RELEASE")) != 0;   // 0: v140's (tests)
            Thread* self = tIntr ? nullptr : tSelf;
            bool released = false;
            if (self && gHost.cdRead && release) {
                std::lock_guard<std::mutex> g(gM);
                if (gOwner == self->id) { self->state = 3; self->wait = 5; self->released = false; gOwner = 0; DispatchLocked(); released = true; }
            }
            const bool got = gHost.cdRead && gHost.cdRead(a0, gMem + Ph(a1), size);
            if (released) {
                std::unique_lock<std::mutex> l(gM);
                ReadyLocked(self);
                if (gOwner == 0) DispatchLocked();
                self->cv.wait(l, [&] { return self->go; });
                self->go = false;
            }
            if (!got) {
                memset(gMem + Ph(a1), 0, size);
                static int said = 0;
                if (said++ < 8) Say("cdbios: read of position %08x (%#x bytes) failed: zeros", a0, a2);
            }
            return 0; }
        case 5: case 6: return 0;                                               // the disc's lock
        }
    }
    static std::map<std::pair<std::string, int>, int> said;
    if (said[{lib, n}]++ < 3) Say("%s #%d (a0 %08x a1 %08x a2 %08x a3 %08x) not served: 0", lib.c_str(), n, a0, a1, a2, a3);
    return 0;
}

// the block input's halves played: their interrupt handlers (sdlib refills the half)
void RunHalves() {
    for (;;) {
        int core;
        { std::lock_guard<std::mutex> g(gHalfLock); if (gHalves.empty()) return; core = gHalves.front(); gHalves.pop_front(); }
        if (gTrans[core].fn) Run(gTrans[core].fn, {(uint32_t)core, gTrans[core].arg});
    }
}
void OnHalf(int core) { std::lock_guard<std::mutex> g(gHalfLock); gHalves.push_back(core); }
std::mutex gMixLock;                           // one mix at a time (it moves the IOP's time)
std::atomic<long long> gMixOverBudget{0};      // steps mixed without waiting for the IOP (the mix's wait budget spent)
// the threads an interrupt made ready run before the mix goes on (a block half is refilled by a
// thread): until no thread is ready or running, at most ms milliseconds
void WaitIdle(int us) {
    std::unique_lock<std::mutex> l(gM);
    gIdle.wait_for(l, std::chrono::microseconds(us), [] {
        if (gOwner != 0 || !gRequests.empty()) return false;
        for (auto& t : gThreads) if (t->state == 1 || (t->state == 3 && t->wait == 5)) return false;   // (or in a disc read)
        return true;
    });
}

// ---- the modules from the disc's package (MODNCD.IPK): entries XORed with a running key
bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const bool ok = n > 0 && fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}
std::map<std::string, std::vector<uint8_t>> Unpack(const std::vector<uint8_t>& d) {
    std::map<std::string, std::vector<uint8_t>> mods;
    size_t pos = 0;
    while (pos + 16 <= d.size()) {
        uint32_t key = 0x7E8A6B4C;
        auto dec = [&](size_t at, size_t n, std::vector<uint8_t>& o) {
            o.resize(n);
            for (size_t k = 0; k + 4 <= n; k += 4) {
                uint32_t w; memcpy(&w, &d[at + k], 4); w ^= key; key += 0xA84E6B2E; memcpy(&o[k], &w, 4);
            }
        };
        std::vector<uint8_t> h; dec(pos, 16, h);
        uint32_t size, nameOff; memcpy(&size, &h[0], 4); memcpy(&nameOff, &h[4], 4);
        pos += 16;
        if (!size || pos + size > d.size()) break;
        std::vector<uint8_t> body; dec(pos, size, body);
        pos += size;
        std::string name;
        for (size_t k = nameOff; k < body.size() && body[k]; ++k) name += (char)body[k];
        if (const size_t s = name.find_last_of("\\/"); s != std::string::npos) name = name.substr(s + 1);
        for (auto& ch : name) ch = (char)tolower((unsigned char)ch);
        body.resize(std::min<size_t>(nameOff, body.size()));
        mods[name] = std::move(body);
    }
    return mods;
}
// an IRX (relocatable ELF) loaded at base: sections, relocations, bss (tools/irx.py)
bool Relocate(const std::vector<uint8_t>& d, uint32_t base, std::vector<uint8_t>& img, std::string& err) {
    auto u16 = [&](size_t o) { uint16_t v; memcpy(&v, &d[o], 2); return v; };
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, &d[o], 4); return v; };
    if (d.size() < 0x34 || memcmp(d.data(), "\x7f" "ELF", 4)) { err = "not an ELF"; return false; }
    const uint32_t shoff = u32(0x20); const uint16_t shentsize = u16(0x2e), shnum = u16(0x30);
    struct Sec { uint32_t name, type, flags, addr, off, size; };
    std::vector<Sec> secs;
    for (int i = 0; i < shnum; ++i) {
        const size_t o = shoff + (size_t)i * shentsize;
        if (o + 24 > d.size()) { err = "bad sections"; return false; }
        secs.push_back({u32(o), u32(o + 4), u32(o + 8), u32(o + 12), u32(o + 16), u32(o + 20)});
    }
    uint32_t size = 0;
    for (auto& s : secs) if ((s.flags & 2) && (s.type == 1 || s.type == 8)) size = std::max(size, s.addr + s.size);
    img.assign(size, 0);
    for (auto& s : secs) if ((s.flags & 2) && s.type == 1 && s.off + s.size <= d.size() && s.addr + s.size <= size) memcpy(&img[s.addr], &d[s.off], s.size);
    auto w = [&](uint32_t o) { uint32_t v = 0; if (o + 4 <= img.size()) memcpy(&v, &img[o], 4); return v; };
    auto put = [&](uint32_t o, uint32_t v) { if (o + 4 <= img.size()) memcpy(&img[o], &v, 4); };
    for (auto& s : secs) {
        if (s.type != 9) continue;
        std::vector<uint32_t> hi;
        for (uint32_t k = 0; k + 8 <= s.size; k += 8) {
            const uint32_t off = u32(s.off + k), info = u32(s.off + k + 4), t = info & 0xff;
            uint32_t x = w(off);
            if (t == 2) x += base;
            else if (t == 4) x = (x & 0xfc000000u) | ((((x & 0x3ffffff) << 2) + base) >> 2 & 0x3ffffff);
            else if (t == 5) { hi.push_back(off); continue; }
            else if (t == 6) {
                const int32_t lo = (int16_t)(x & 0xffff);
                for (uint32_t h : hi) {
                    const uint32_t hw = w(h), v = ((hw & 0xffff) << 16) + lo + base;
                    put(h, (hw & 0xffff0000u) | (((v + 0x8000) >> 16) & 0xffff));
                }
                hi.clear();
                x = (x & 0xffff0000u) | ((lo + base) & 0xffff);
            } else { err = "relocation type " + std::to_string(t); return false; }
            put(off, x);
        }
    }
    return true;
}
uint64_t Fnv(const std::vector<uint8_t>& b) { uint64_t h = 0xcbf29ce484222325ull; for (uint8_t x : b) h = (h ^ x) * 0x100000001b3ull; return h; }

}  // namespace

// ---- the recompiled code's side
uint64_t HwR(uint32_t a, int size) {
    static int said = 0;
    if (said++ < 20) Say("hardware read %08x (%d bytes): 0", a, size);
    return 0;
}
void HwW(uint32_t a, uint64_t v, int size) {
    static int said = 0;
    if (said++ < 20) Say("hardware write %08x = %llx (%d bytes): ignored", a, (unsigned long long)v, size);
}
// a running thread at a backward branch or a kernel call: the waiting interrupts run, then a
// higher priority thread they made ready takes the baton
void Poll() {
    if (tIntr || !tSelf) return;
    std::unique_lock<std::mutex> l(gM);
    if (gOwner != tSelf->id) return;
    gPoll = 0;
    if (!gSuspend) RunRequestsLocked(l);
    if (!gRequests.empty()) gPoll = 1;
    PreemptLocked(l);
}
void Call(Ctx& c, uint32_t addr) {
    auto f = gFns.find(addr);
    if (f != gFns.end()) { f->second(c); return; }
    auto s = gStubs.find(addr);
    if (s != gStubs.end()) {
        const int32_t r = Service(c, s->second);
        c.r[2] = (uint64_t)(int64_t)r;
        if (gPoll.load(std::memory_order_relaxed)) Poll();
        return;
    }
    static int said = 0;
    if (said++ < 20) Say("call of %08x: no such function (from %08x)", addr, (uint32_t)c.r[31]);
    c.r[2] = 0;
}
void Syscall(Ctx& c, uint32_t pc) { Say("syscall at %08x", pc); c.r[2] = 0; }
void Trap(Ctx&, uint32_t pc) { static int said = 0; if (said++ < 10) Say("break / trap at %08x", pc); }
bool TrapCond(Ctx&, int, uint64_t, uint64_t) { return false; }
void Unimpl(Ctx&, uint32_t pc, uint32_t w) { static int said = 0; if (said++ < 20) Say("instruction %08x at %08x not recompiled", w, pc); }

bool Start(const Host& host, const std::vector<uint8_t>& pkg, std::string& error) {
    if (gRunning) return true;
    gHost = host;
    auto mods = Unpack(pkg);
    memset(gMem, 0, sizeof gMem);
    for (int k = 0; k < kModuleCount; ++k) {
        const ModuleInfo& m = kModules[k];
        auto it = mods.find(std::string(m.name) + ".irx");
        if (it == mods.end()) { error = std::string(m.name) + ".irx is not in MODNCD.IPK"; return false; }
        std::vector<uint8_t> img;
        if (!Relocate(it->second, m.base, img, error)) { error = std::string(m.name) + ": " + error; return false; }
        if (img.size() != m.size || Fnv(img) != m.hash) {
            char b[160]; snprintf(b, sizeof b, "%s.irx is not the module this build recompiled (%#zx bytes, hash %016llx)", m.name, img.size(), (unsigned long long)Fnv(img));
            error = b; return false;
        }
        memcpy(gMem + m.base, img.data(), img.size());
        gHeapLo = std::max(gHeapLo, (m.base + m.size + 0xfff) & ~0xfffu);
    }
    // libsd's effect presets, from the package's libsd.irx (loaded at 0 to read its data)
    if (auto it = mods.find("libsd.irx"); it != mods.end()) {
        std::vector<uint8_t> img;
        std::string e;
        if (Relocate(it->second, 0, img, e) && img.size() == kLibsdSize && Fnv(img) == kLibsdHash) {
            for (int m = 0; m < 10; ++m) {
                memcpy(gFx.regs[m], &img[kLibsdPresets + m * 0x44 + 4], 64);
                memcpy(&gFx.size[m], &img[kLibsdSizes + 4 * m], 4);
            }
            gFx.ok = true;
        } else Say("libsd.irx is not the build this code knows: no reverb");
    }
    for (int k = 0; k < kFunctionCount; ++k) gFns[kFunctions[k].addr] = kFunctions[k].fn;
    for (int k = 0; k < kImportCount; ++k) gStubs[kImports[k].addr] = {kImports[k].lib, kImports[k].index};
    for (int k = 0; k < kExportCount; ++k) gExports[{kExports[k].lib, kExports[k].index}] = kExports[k].addr;
    spu2::Reset();
    if (const char* e = getenv("IOP_KEY_LOG")) spu2::SetLog(gHost.log, atoi(e), strchr(e, '@') ? atof(strchr(e, '@') + 1) : 0);   // "N@seconds": from then
    if (auto f = gFns.find(0x11a248); f != gFns.end()) {           // the effects' starts (the added weapons' sounds)
        gSeStartFn = f->second;
        f->second = SeStart;
    }
    if (const char* e = getenv("IOP_SE_PROBE")) {
        gSeProbeLeft = atoi(e);
        gSeProbeFrom = (uint64_t)((strchr(e, '@') ? atof(strchr(e, '@') + 1) : 0) * 48000 * kCyclesPerSample);
    }
    gRunning = true;
    // the modules' starts, in the order the game loads them: the library, then the driver; their
    // arguments as the package gives them (none: argv[0], the name)
    for (int k = 0; k < kModuleCount; ++k) {
        Interrupt([&] {
            const uint32_t argv = kPacket + 0x200, name = argv + 16;
            const std::string n = std::string(kModules[k].name) + ".irx";
            memset(gMem + argv, 0, 64);
            memcpy(gMem + name, n.c_str(), std::min<size_t>(n.size(), 40));
            W32(argv, name);
            const uint32_t r = Run(kModules[k].entry, {1, argv});
            Say("module %s started at %06x (%u)", kModules[k].name, kModules[k].base, r);
        });
    }
    return true;
}
bool Running() { return gRunning; }
void SetPalFields(bool pal) { gVblankCycles = pal ? 36864000ull / 50 : 36864000ull * 1001 / 60000; }
void LogTransfers(int count) { gLogXfer = count; }

void EeCommand(uint32_t cid, uint32_t cmd, const uint8_t* args, uint32_t size) {
    if (!gRunning || cid >= 32 || !gCmd[cid].fn) return;
    if (sCommands < 40) Say("EE command %u on channel %u (%u bytes: %08x %08x %08x %08x)", cmd, cid, size,
                           size >= 4 ? R32e(args, 0) : 0, size >= 8 ? R32e(args, 4) : 0, size >= 12 ? R32e(args, 8) : 0, size >= 16 ? R32e(args, 12) : 0);
    ++sCommands;
    size = std::min<uint32_t>(size, 0x100);
    Interrupt([&] {
        memset(gMem + kPacket, 0, 0x120);
        W32(kPacket + 0, 0x10 + size); W32(kPacket + 8, cid); W32(kPacket + 12, cmd);
        if (args && size) memcpy(gMem + kPacket + 0x10, args, size);
        Run(gCmd[cid].fn, {kPacket, gCmd[cid].arg});
    });
}

void Mix(int16_t* out, int frames, int waitUs) {
    if (!gRunning) return;
    std::lock_guard<std::mutex> one(gMixLock);
    tMixer = true;
    const auto t0 = std::chrono::steady_clock::now();
    // in steps that end where something is due (a timer, a wait) or a block half may end (128
    // samples at most); what is due runs, and the threads it woke, before the mix goes on
    while (frames > 0) {
        int n = std::min(frames, 128);
        const uint64_t next = NextEvent(), now = Cycles();
        if (next > now && next != UINT64_MAX) n = (int)std::min<uint64_t>(n, (next - now + kCyclesPerSample - 1) / kCyclesPerSample);
        else if (next <= now) n = 0;
        if (n > 0) {
            spu2::Mix(out, n, &OnHalf);
            gNow += (uint64_t)n * kCyclesPerSample;
            out += 2 * n; frames -= n;
        }
        bool any = RunDue();
        bool halves;
        { std::lock_guard<std::mutex> g(gHalfLock); halves = !gHalves.empty(); }
        if (halves) { Interrupt(RunHalves); any = true; }
        if (any) {
            if (waitUs < 0) WaitIdle(20000);                                // each step its 20 ms (the simulation)
            else {                                                          // the whole mix's budget
                const long long spent = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
                if (spent < waitUs) WaitIdle((int)std::min<long long>(20000, waitUs - spent));
                else ++gMixOverBudget;
            }
        }
    }
}

// the menus' effects as the driver plays them from the system bank (measured with the key-on log in the lobby,
// docs/SOUND.md "The menus' effects"): each voice's start in sound memory, pitch, ADSR, volume and the time the
// driver releases it; the voices as the driver takes them first (core 0 voice 0, then core 1 voice 21)
struct MenuVoice { int core, voice; uint32_t start, pitch, adsr1, adsr2, volL, volR; int releaseMs; };
struct MenuEffect { uint32_t code; int voices; MenuVoice v[4]; };
const MenuEffect kMenuEffects[3] = {
    {0x1A00B, 1, {{0, 0, 0x20080, 0x3275, 0x1b06, 0x4f46, 0x0f53, 0x0f53, 70}}},                                   // the cursor move
    {0x1A00C, 2, {{0, 0, 0x238f0, 0x2052, 0x2d14, 0x4ecf, 0x08e3, 0x0819, 390}, {1, 21, 0x25450, 0x2acc, 0x2f03, 0x4eca, 0x0b21, 0x0b21, 110}}},   // OK
    {0x1A00D, 2, {{0, 0, 0x238f0, 0x3a44, 0x22ff, 0x4bce, 0x0165, 0x0165, 170}, {1, 21, 0x26f50, 0x1a92, 0x25ff, 0x4c0e, 0x01e0, 0x01b5, 215}}},   // cancel
};
int RenderMenuSound(const char* path, long long base, long long size, int which, int16_t* out, int maxFrames) {
    if (gRunning || !path || maxFrames <= 0 || which < 0 || which >= 3 || !kMenuEffects[which].voices) return 0;
    static std::mutex renderLock;                                   // one render at a time (the SPU2 model is shared)
    std::lock_guard<std::mutex> one(renderLock);
    const MenuEffect& fx = kMenuEffects[which];
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    // the system bank is loaded at sound memory 0x20000; the anchors: FNV-1a of 64 bytes at
    // 0x23900 (the Start sound's first voice's second block) and at 0x25460 (its second voice's)
    constexpr uint64_t kAnchorA = 0x7f5e18537954e84eull, kAnchorB = 0x98b071f23e243827ull;
    auto fnv = [](const uint8_t* p) { uint64_t h = 0xcbf29ce484222325ull; for (int k = 0; k < 64; ++k) h = (h ^ p[k]) * 0x100000001b3ull; return h; };
    static std::string cachedPath; static long long cachedBase = -1, cachedSize = -1, cachedBank = -1;   // the bank found last
    long long bank = -1;
    if (cachedPath == path && cachedBase == base && cachedSize == size) bank = cachedBank;
    else {
        std::vector<uint8_t> buf((1 << 20) + 64);
        const long long span = std::min<long long>(size, 1LL << 26);
        for (long long at = 0; at < span && bank < 0; at += 1 << 20) {
            const size_t want = (size_t)std::min<long long>((1 << 20) + 64, span - at);
            if (_fseeki64(f, base + at, SEEK_SET) != 0 || fread(buf.data(), 1, want, f) != want) break;
            for (size_t k = 0; k + 64 <= want && k < (1u << 20); k += 16)
                if (fnv(&buf[k]) == kAnchorA) { bank = at + (long long)k - 0x3900; break; }
        }
        cachedPath = path; cachedBase = base; cachedSize = size; cachedBank = bank;
    }
    // the bank's waves, sound memory 0x20000 to 0x3D000 (the menus' effects: 0x20080 the cursor's, 0x238F0 and
    // 0x25450 the OK's)
    const uint32_t loadAt = 0x20000, loadEnd = 0x3D000;
    std::vector<uint8_t> data(loadEnd - loadAt);
    size_t got = 0;
    bool ok = bank >= 0 && bank + 0x5460 + 64 <= size && _fseeki64(f, base + bank, SEEK_SET) == 0;
    if (ok) { got = fread(data.data(), 1, std::min<size_t>(data.size(), (size_t)std::max<long long>(0, size - bank)), f); ok = got >= 0x5460 + 64 && fnv(&data[0x5460]) == kAnchorB; }
    fclose(f);
    if (!ok) return 0;
    for (int k = 0; k < fx.voices; ++k) if (fx.v[k].start < loadAt || fx.v[k].start >= loadAt + got) return 0;
    spu2::Reset();
    spu2::Write(loadAt, data.data(), (uint32_t)got);
    // the driver's settings: core volumes, the voices' mix, then the voices
    auto e = [](int param, int core, int voice) { return (uint32_t)(param << 8 | voice << 1 | core); };
    for (int core = 0; core < 2; ++core) {
        spu2::SetParam(e(0x09, core, 0), 0x3fff); spu2::SetParam(e(0x0a, core, 0), 0x3fff);
        spu2::SetParam(e(0x08, core, 0), core ? 0xfcc : 0xfcf);                        // MMIX
        spu2::SetParam(e(0x0d, core, 0), 0x7fff); spu2::SetParam(e(0x0e, core, 0), 0x7fff);   // AVOL: core 0 into core 1
        spu2::SetSwitch(e(0x18, core, 0), 0xffffff); spu2::SetSwitch(e(0x1a, core, 0), 0xffffff);
    }
    uint32_t keyOn[2] = {0, 0};
    for (int k = 0; k < fx.voices; ++k) {
        const MenuVoice& v = fx.v[k];
        spu2::SetParam(e(0x00, v.core, v.voice), v.volL); spu2::SetParam(e(0x01, v.core, v.voice), v.volR);
        spu2::SetParam(e(0x02, v.core, v.voice), v.pitch);
        spu2::SetParam(e(0x03, v.core, v.voice), v.adsr1); spu2::SetParam(e(0x04, v.core, v.voice), v.adsr2);
        spu2::SetAddr(e(0x20, v.core, v.voice), v.start);
        keyOn[v.core] |= 1u << v.voice;
    }
    for (int core = 0; core < 2; ++core) if (keyOn[core]) spu2::SetSwitch(e(0x15, core, 0), keyOn[core]);
    // released as the driver releases them, in the order of their release times
    memset(out, 0, sizeof(int16_t) * 2 * (size_t)maxFrames);
    int done = 0;
    auto mix = [&](int until) { const int n = std::min(until, maxFrames) - done; if (n > 0) { spu2::Mix(out + 2 * done, n, nullptr); done += n; } };
    int order[4] = {0, 1, 2, 3};
    std::sort(order, order + fx.voices, [&](int a, int b) { return fx.v[a].releaseMs < fx.v[b].releaseMs; });
    for (int k = 0; k < fx.voices; ++k) {
        const MenuVoice& v = fx.v[order[k]];
        mix(48000 * v.releaseMs / 1000);
        spu2::SetSwitch(e(0x16, v.core, 0), 1u << v.voice);
    }
    mix(maxFrames);
    int last = 0;                                                   // the tail of silence cut off
    for (int k = 0; k < done; ++k) if (out[2 * k] || out[2 * k + 1]) last = k + 1;
    spu2::Reset();
    return last;
}
int RenderStartSound(const char* path, long long base, long long size, int16_t* out, int maxFrames) {
    return RenderMenuSound(path, base, size, 1, out, maxFrames);
}
void DumpSoundMemory(const char* path) {
    if (FILE* f = fopen(path, "wb")) { fwrite(spu2::Ram(), 1, 2u << 20, f); fclose(f); }
}
void AddedWeaponSound(uint32_t handle, std::shared_ptr<const spu2::Pcm> pcm, float gain, uint64_t cut, const std::string& what) {
    std::lock_guard<std::mutex> g(gOwnLock);
    const uint64_t now = Cycles(), old = 10ull * 48000 * kCyclesPerSample;
    for (auto it = gOwnHandles.begin(); it != gOwnHandles.end();) it = now - it->second.at > old ? gOwnHandles.erase(it) : std::next(it);
    gOwnHandles[handle] = {std::move(pcm), gain, cut, what, now};
    gOwnCount = (int)(gOwnHandles.size() + gOwnChannels.size());
    ++sOwnMarked;
}
std::string Stats() {
    char b[400];
    int ready = 0, waiting = 0;
    { std::lock_guard<std::mutex> g(gM); for (auto& t : gThreads) { if (t->state == 1) ++ready; if (t->state == 3) ++waiting; } }
    snprintf(b, sizeof b, "%lld ticks, %lld EE commands, %lld queue reads, %lld status sends, %lld disc reads, threads %zu (%d ready, %d waiting); spu2: %s",
             sTicks.load(), sCommands.load(), sQueueReads.load(), sStatusSends.load(), sCdReads.load(), gThreads.size(), ready, waiting, spu2::Stats().c_str());
    std::string r = b;
    if (const long long over = gMixOverBudget.exchange(0)) {   // MGO1.exe's mixes that went on without the IOP
        snprintf(b, sizeof b, "; %lld mix steps without waiting for the IOP (the audio thread's budget spent)", over);
        r += b;
    }
    if (const long long w = sMixBatonWaits.exchange(0)) {      // the mix held up by an IOP thread holding the baton
        snprintf(b, sizeof b, "; the mix waited %lld times for an IOP thread (%.1f ms, the longest %.1f ms)", w,
                 sMixBatonUs.exchange(0) / 1000.0, sMixBatonWorstUs.exchange(0) / 1000.0);
        r += b;
    }
    if (sOwnMarked.load()) {                               // the added weapons' own sounds (docs/SOUND.md)
        snprintf(b, sizeof b, "; the added weapons' sounds: %lld asked, %lld started, %lld heard, %lld without a channel, %d playing",
                 sOwnMarked.load(), sOwnStarted.load(), sOwnHeard.load(), sOwnLost.load(), spu2::Substitutes());
        r += b;
    }
    return r;
}

}  // namespace iop
