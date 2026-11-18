// ps2rt.cpp - dispatch and the rare instructions of the recompiled code.
#include "ps2rt.h"
#include "gs_thread.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <set>
#include <csetjmp>
#include <chrono>
#include <vector>
#include <algorithm>
#include <map>
#include <mutex>
#include "wildguard.h"                                  // OnceWild: an access through a wild pointer (v150-host beta 16)
namespace ps2 {
// run-time instruction variants of the generated code (recomp/variants.txt): set by the platform
unsigned char gVariant[64];
uint8_t* gMem = nullptr;
uint32_t gWinLo = 0xffffffff, gWinHi = 0;           // no window until a platform sets one
uint64_t (*gWinRead)(uint32_t a, int size) = nullptr;
void (*gWinWrite)(uint32_t a, uint64_t v, int size) = nullptr;
uint64_t WinR(uint32_t a, int size) { return gWinRead ? gWinRead(a, size) : 0; }
uint32_t gWatchLo = 0xffffffff, gWatchHi = 0;
uint32_t gCursorWin = 0xffffffff;
uint32_t gStageWin = 0xffffffff;
uint32_t gEie = 1;
void (*gWatchLog)(uint32_t a) = nullptr;
void WatchHit(uint32_t p, int, bool, uint64_t) { if (gWatchLog) gWatchLog(p); }
void WinW(uint32_t a, uint64_t v, int size) { if (gWinWrite) gWinWrite(a, v, size); }
void (*gLog)(const char* fmt, ...) = nullptr;
bool (*gBridge)(Ctx& c, uint32_t addr) = nullptr;   // base-ELF functions and overlay functions served natively

static std::set<uint32_t> sReported;
void Once(const char* what, uint32_t pc, uint32_t w) {
    if (sReported.insert(pc).second && gLog) gLog("ps2: %s at %08x (%08x)", what, pc, w);
}
extern uint32_t gRing[64][2]; extern unsigned gRingPos;
extern volatile uint32_t gLastCall;
int gGsTrace = 0;
uint32_t gIrqPending = 0;
void (*gIrqDeliver)() = nullptr;
void (*gIrqWake)() = nullptr;
bool gInIrq = false;
bool gGsReal = false;
void GsTraceLog(const char* fmt, ...) {
    if (gGsTrace <= 0 || !gLog) return;
    --gGsTrace;
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    gLog("gs trace: %s", b);
}
uint32_t gDmaLog = 0;                                   // DMA starts left to log (the platform sets it)
int gDmaLogCh = -1;                                     // only this channel's (-1: every channel)
extern volatile uint32_t gLastCaller;
uint32_t gDmaStarts[16] = {};                           // starts per channel (the platform reads and clears them)
extern bool gHeadless;                                  // gs.cpp: no picture (MGO1_HEADLESS)
uint64_t gDmaNs[3] = {};                                // VIF0, VIF1, GIF: time in their DMA starts (the stats' timing)
extern bool gGsTiming;
// PS2 hardware stand-ins (IsHw: 0x1xxxxxxx). The recompiled DMA kicks and waits run
// against them: every DMA transfer completes when it starts (scratchpad transfers copy
// their data), the VIF / GIF report idle, the timers count from the host clock after
// their last write, the GS reports its events raised. The EE debug port (SIO) is logged.
namespace {
uint8_t sEe[0x10000];                      // 0x10000000..0x1000FFFF
uint8_t sGs[0x2000];                       // 0x12000000..0x12001FFF
#define sVu1Micro (gVu1.micro)             // VU1's memories live in its register file (vu0.cpp)
#define sVu1Mem (gVu1.mem)
uint32_t sDmaCis = 0;                      // D_STAT channel interrupt status (bits 0-9)
uint32_t sGsField = 0;
struct HwTimer { std::chrono::steady_clock::time_point t; uint32_t v; } sTimer[4];
bool sTimerInit = false;
char sSio[256]; size_t sSioN = 0;
std::set<uint64_t> sHwSeen;
const uint32_t kDmaChannel[10] = {0x8000, 0x9000, 0xa000, 0xb000, 0xb400, 0xc000, 0xc400, 0xc800, 0xd000, 0xd400};
int DmaChannel(uint32_t off) { for (int k = 0; k < 10; ++k) if (kDmaChannel[k] == off) return k; return -1; }
uint32_t Ee32(uint32_t off) { uint32_t v; memcpy(&v, sEe + (off & 0xfffc), 4); return v; }
void SetEe32(uint32_t off, uint32_t v) { memcpy(sEe + (off & 0xfffc), &v, 4); }
void HwSeen(uint32_t a, bool write, uint64_t v) {
    if (sHwSeen.size() >= 1024) return;                    // (the registers a game uses are a few hundred)
    if (sHwSeen.insert((uint64_t)a << 1 | (write ? 1 : 0)).second && gLog)
        gLog("ps2: hardware %s %08x (%08llx)", write ? "write" : "read", a, (unsigned long long)v);
}
uint32_t TimerCount(int t) {
    static const double kRate[4] = {147456000.0, 9216000.0, 576000.0, 15734.0};    // BUSCLK, /16, /256, H-blank
    if (!sTimerInit) { for (auto& x : sTimer) x.t = std::chrono::steady_clock::now(); sTimerInit = true; }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - sTimer[t].t).count();
    return (sTimer[t].v + (uint32_t)(uint64_t)(s * kRate[Ee32((uint32_t)t << 11 | 0x10) & 3])) & 0xffff;
}
// a DMA channel started: scratchpad transfers (normal mode) copy their data, the others
// have nowhere to go; the channel then reads as done and raises its interrupt status
void DmaStart(int ch, uint32_t chcr) {
    const uint32_t base = kDmaChannel[ch];
    uint32_t madr = Ee32(base + 0x10), qwc = Ee32(base + 0x20) & 0xffff, sadr = Ee32(base + 0x80);
    const uint32_t mode = (chcr >> 2) & 3;
    if (ch >= 8 && ((qwc << 4) + (sadr & 0x3ff0) > 0x4000) && gLog)            // past the scratchpad's end: wraps to its start
        Once("hardware: scratchpad DMA wraps past 16 KB", base, qwc);
    if (gDmaLog && gLog && (gDmaLogCh < 0 || gDmaLogCh == ch)) {
        --gDmaLog;
        gLog("ps2: DMA channel %d chcr %04x madr %08x qwc %#x tadr %08x sadr %08x (last call %08x from %08x); at madr %08x %08x %08x %08x", ch, chcr, madr, qwc,
             Ee32(base + 0x30), sadr, gLastCall, gLastCaller - 8, R32(madr & 0x01fffff0), R32((madr & 0x01fffff0) + 4), R32((madr & 0x01fffff0) + 8), R32((madr & 0x01fffff0) + 12));
    }
    ++gDmaStarts[ch & 15];
    if (ch >= 8 && mode == 0) {
        for (uint32_t k = 0; k < qwc; ++k) {
            const uint32_t m = (madr + 16 * k) & 0x01fffff0, sp = 0x70000000 | ((sadr + 16 * k) & 0x3ff0);
            const uint32_t from = ch == 8 ? sp : m, to = ch == 8 ? m : sp;
            W64(to, R64(from)); W64(to + 8, R64(from + 8));
        }
        SetEe32(base + 0x10, madr + 16 * qwc);
        SetEe32(base + 0x80, (sadr + 16 * qwc) & 0x3ff0);
    } else if (ch >= 8) {
        Once("hardware: scratchpad DMA mode not emulated", base, chcr);
    } else if (ch == 0 || ch == 1 || ch == 2) {             // VIF0 -> VU0, VIF1 -> VU1 / GIF, GIF (PATH3)
        struct Timed {                                      // the stats' timing: the start's time for its channel
            int ch; std::chrono::steady_clock::time_point t0;
            explicit Timed(int c) : ch(c), t0(gGsTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point()) {}
            ~Timed() { if (gGsTiming) gDmaNs[ch] += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count(); }
        } timed(ch);
        const bool queued = ch != 0 && GsThreadOn();       // VIF1 and GIF on the GS thread: their data copied now
        auto feed = [ch, queued](uint32_t a, uint32_t q) {
            if (!q) return;
            const uint32_t off = a & 0x01fffff0;           // in the main memory and inside it: read in place
            if (off + 16 * (size_t)q <= 0x02000000 && !(off < gWinHi && off + 16 * q > gWinLo)) {
                if (queued) { if (ch == 2) GsQueueGif(gMem + off, q); else GsQueueVif1((const uint32_t*)(gMem + off), 4 * (size_t)q); }
                else if (ch == 2) GifPath(3, gMem + off, q);
                else if (ch == 0) Vif0Write((const uint32_t*)(gMem + off), 4 * (size_t)q);
                else Vif1Write((const uint32_t*)(gMem + off), 4 * (size_t)q);
                return;
            }
            if (ch == 2) {                                  // the GIF reads the quadwords as they are
                std::vector<uint8_t> b(16 * (size_t)q);
                for (size_t k = 0; k < b.size(); k += 4) { uint32_t x = R32((a & 0x01fffff0) + (uint32_t)k); memcpy(b.data() + k, &x, 4); }
                if (queued) GsQueueGif(b.data(), q); else GifPath(3, b.data(), q);
                return;
            }
            std::vector<uint32_t> w(4 * (size_t)q);
            for (size_t k = 0; k < w.size(); ++k) w[k] = R32((a & 0x01fffff0) + 4 * (uint32_t)k);
            if (ch == 0) Vif0Write(w.data(), w.size()); else if (queued) GsQueueVif1(w.data(), w.size()); else Vif1Write(w.data(), w.size());
        };
        if (mode == 0) {
            if (gGsTrace > 0) GsTraceLog("dma%d normal: chcr %08x madr %08x qwc %u (last call %08x): %08x %08x %08x %08x", ch, chcr, madr, qwc, gLastCall,
                                         R32(madr), R32(madr + 4), R32(madr + 8), R32(madr + 12));
            feed(madr, qwc);
            SetEe32(base + 0x10, madr + 16 * qwc);
        } else if (mode == 1) {                             // source chain: tags from TADR
            uint32_t tadr = Ee32(base + 0x30), asr[2] = {0, 0};
            int depth = 0;
            for (int guard = 0; guard < 100000; ++guard) {
                const uint64_t tagw = R64(tadr & 0x01fffff0);
                const uint32_t q = (uint32_t)tagw & 0xffff, id = (uint32_t)(tagw >> 28) & 7, addr = (uint32_t)(tagw >> 32) & 0x7ffffff0;
                if ((chcr & 0x40) && ch != 2) {             // TTE: the tag's upper half reaches the VIF
                    uint32_t tw[2] = {R32((tadr & 0x01fffff0) + 8), R32((tadr & 0x01fffff0) + 12)};
                    if (ch == 0) Vif0Write(tw, 2); else if (queued) GsQueueVif1(tw, 2); else Vif1Write(tw, 2);
                }
                if (gGsTrace > 0) GsTraceLog("dma%d tag %08x: chcr %08x id %u qwc %u addr %08x (last call %08x)", ch, tadr, chcr, id, q, addr, gLastCall);
                bool end = false;
                switch (id) {
                case 0: feed(addr, q); end = true; break;                                       // refe
                case 1: feed(tadr + 16, q); tadr = tadr + 16 + 16 * q; break;                   // cnt
                case 2: feed(tadr + 16, q); tadr = addr; break;                                 // next
                case 3: case 4: feed(addr, q); tadr += 16; break;                              // ref, refs
                case 5: feed(tadr + 16, q); if (depth < 2) asr[depth++] = tadr + 16 + 16 * q; tadr = addr; break;   // call
                case 6: feed(tadr + 16, q); if (depth) tadr = asr[--depth]; else end = true; break;               // ret
                default: feed(tadr + 16, q); end = true; break;                                 // end
                }
                if (end || ((tagw >> 31) & 1 && (chcr & 0x80))) break;
            }
            SetEe32(base + 0x30, tadr);
        } else {
            Once("hardware: VIF / GIF DMA mode not emulated", base, chcr);
        }
        if (queued) GsQueueSubmit();                        // the GS thread starts on it
    }
    SetEe32(base + 0x20, 0);
    SetEe32(base, chcr & ~0x100u);
    sDmaCis |= 1u << ch;
    __atomic_fetch_or(&gIrqPending, 1u << ch, __ATOMIC_SEQ_CST);   // the channel's end interrupt (its handlers, if enabled); the GS thread raises bit 16
}
}  // namespace
uint64_t HwR(uint32_t a, int size) {
    uint64_t v = 0;
    if ((a & 0xffff0000) == 0x10000000) {
        const uint32_t off = a & 0xffff, reg = off & ~0xfu;
        if (off >= 0x4000 && off < 0x8000) v = 0;                                   // FIFOs
        else if (off < 0x2000 && (off & 0x7ff) == 0) v = TimerCount((int)(off >> 11));
        else if (reg == 0x3020 || reg == 0x3800 || reg == 0x3c00 || reg == 0xf130) v = 0;   // GIF / VIF0 / VIF1 idle, SIO
        else if (reg == 0xe010) v = sDmaCis | (Ee32(0xe010) & 0x03ff0000);
        else if (DmaChannel(reg) >= 0) v = Ee32(reg) & ~0x100u;
        else memcpy(&v, sEe + (off & (0x10000 - size)), (size_t)size);
    } else if ((a & 0xffff8000) == 0x11000000) {                                  // VU0 micro, data (4 KB each)
        memcpy(&v, (a & 0x4000 ? gVu0.mem : gVu0.micro) + (a & (0x1000 - size)), (size_t)size);
    } else if ((a & 0xffff8000) == 0x11008000) {                                  // VU1 micro, data (16 KB each)
        GsSync();                                                                   // as the GS thread left them
        memcpy(&v, (a & 0x4000 ? sVu1Mem : sVu1Micro) + (a & (0x4000 - size)), (size_t)size);
    } else if ((a & 0xffffe000) == 0x12000000) {
        memcpy(&v, sGs + (a & (0x2000 - size)), (size_t)size);
        if ((a & ~7u) == 0x12001000) {                                            // CSR: FINISH, HSINT, VSINT raised, FIELD alternates
            sGsField ^= 1;
            if (gGsReal) v = GsPrivRead(a, size);                                 // the GS's own events (DG runs)
            else v = (v & ~0x201full) | 0x0e | (sGsField ? 0x2000 : 0) | (GsPrivRead(a, size) & 0x1);
        } else if ((a & ~7u) == 0x12001080) v = GsPrivRead(a, size);              // SIGLBLID
    } else {
        OnceWild("hardware read outside the emulated registers", a, 0);
    }
    HwSeen(a, false, v);
    return v;
}
void HwW(uint32_t a, uint64_t v, int size) {
    HwSeen(a, true, v);
    if ((a & 0xffff0000) == 0x10000000) {
        const uint32_t off = a & 0xffff, reg = off & ~0xfu;
        if (off >= 0x4000 && off < 0x7000) {                                       // VIF0 / VIF1 / GIF FIFO: quadwords the EE stores
            const int f = (int)((off - 0x4000) >> 12);                             // (two 64-bit halves, or four words), in order
            static uint32_t q[3][4];                                               // with what the DMA brings
            if (size == 8) { q[f][(off & 8) >> 2] = (uint32_t)v; q[f][((off & 8) >> 2) + 1] = (uint32_t)(v >> 32); }
            else q[f][(off >> 2) & 3] = (uint32_t)v;
            if ((size == 8 && (off & 8)) || (size < 8 && ((off >> 2) & 3) == 3)) {
                if (f == 0) Vif0Write(q[f], 4);
                else if (GsThreadOn()) { if (f == 1) GsQueueVif1(q[f], 4); else GsQueueGif((const uint8_t*)q[f], 1); GsQueueSubmit(); }
                else if (f == 1) Vif1Write(q[f], 4);
                else GifPath(3, (const uint8_t*)q[f], 1);
            }
            return;
        }
        if (off >= 0x7000 && off < 0x8000) return;                                 // IPU FIFOs: no decoder here
        if (off < 0x2000 && (off & 0x7ff) == 0) {                                   // timer count: counts on from here
            const int t = (int)(off >> 11);
            if (!sTimerInit) TimerCount(0);
            sTimer[t].v = (uint32_t)v & 0xffff; sTimer[t].t = std::chrono::steady_clock::now();
            return;
        }
        if (reg == 0xe010) {                                                        // D_STAT: clear CIS, toggle CIM
            sDmaCis &= ~((uint32_t)v & 0x3ff);
            SetEe32(0xe010, Ee32(0xe010) ^ ((uint32_t)v & 0x03ff0000));
            return;
        }
        if (reg == 0xf590) { SetEe32(0xf520, (uint32_t)v); return; }                // D_ENABLEW, read back at D_ENABLER
        if (reg == 0xf180) {                                                        // SIO transmit: the EE debug output
            const char ch = (char)v;
            if (ch == '\n' || sSioN + 1 >= sizeof(sSio)) {
                sSio[sSioN] = 0;
                if (gLog && sSioN) gLog("ee sio: %s", sSio);
                sSioN = 0;
            }
            if (ch != '\n' && ch != '\r') sSio[sSioN++] = ch;
            return;
        }
        memcpy(sEe + (off & (0x10000 - size)), &v, (size_t)size);
        const int ch = DmaChannel(reg);
        if (ch >= 0 && size >= 4 && (v & 0x100)) DmaStart(ch, (uint32_t)v);
    } else if ((a & 0xffff8000) == 0x11000000) {
        memcpy((a & 0x4000 ? gVu0.mem : gVu0.micro) + (a & (0x1000 - size)), &v, (size_t)size);
        if (!(a & 0x4000)) ++gMicroGen[0];                                          // the micro memory changed
    } else if ((a & 0xffff8000) == 0x11008000) {
        if (GsThreadOn()) { GsQueueVu1Write(a, v, size); return; }                   // in order with the VIF1 data
        if (gGsqOn) { const uint64_t r[2] = {(uint64_t)a | (uint64_t)(uint32_t)size << 32, v}; GsqPut(kGsqVu1, r, 16); }   // the bench's recording
        memcpy((a & 0x4000 ? sVu1Mem : sVu1Micro) + (a & (0x4000 - size)), &v, (size_t)size);
        if (!(a & 0x4000)) ++gMicroGen[1];
    } else if ((a & 0xffffe000) == 0x12000000) {
        memcpy(sGs + (a & (0x2000 - size)), &v, (size_t)size);
        GsPrivWrite(a, v, size);
    } else {
        OnceWild("hardware write outside the emulated registers", a, (uint32_t)v);
    }
}
bool TrapCond(Ctx&, int fu, uint64_t a, uint64_t b) {
    switch (fu) {
        case 0x30: return (int64_t)a >= (int64_t)b;
        case 0x31: return a >= b;
        case 0x32: return (int64_t)a < (int64_t)b;
        case 0x33: return a < b;
        case 0x34: return a == b;
        case 0x36: return a != b;
    }
    return false;
}
extern uint32_t gRing[64][2]; extern unsigned gRingPos;
void (*gSyscall)(Ctx& c, uint32_t pc) = nullptr;
void Syscall(Ctx& c, uint32_t pc) { if (gSyscall) gSyscall(c, pc); else Trap(c, pc); }
void Trap(Ctx& c, uint32_t pc) {
    if (!sReported.insert(pc).second || !gLog) return;
    // a trap (teq after a division: divide by zero) with the calls that led there
    char s[64 * 20] = ""; size_t n = 0;
    for (unsigned k = 0; k < 16 && n + 20 < sizeof(s); ++k) {
        const uint32_t* e = gRing[(gRingPos - 16 + k) & 63];
        n += (size_t)snprintf(s + n, sizeof(s) - n, " %08x<%08x", e[0], e[1] - 8);
    }
    gLog("ps2: trap at %08x (ra %08x, a0 %08x); last calls:%s", pc, (uint32_t)c.r[31], (uint32_t)c.r[4], s);
}
void Unimpl(Ctx&, uint32_t pc, uint32_t w) { Once("unimplemented instruction", pc, w); }
// MMI (opcode 0x1C): mmi.cpp
struct OvlTable { const char* version; const char* page; const FnEntry* fns; int count; const AliasEntry* alias; int nalias; };
static OvlTable* Registry(int* n) {
    static OvlTable t[256];
    static int count = 0;
    if (n) *n = count;
    return t + (n ? 0 : count++);
}
OverlayRegistration::OverlayRegistration(const char* version, const char* page, const FnEntry* fns, int count, const AliasEntry* alias, int nalias) {
    int n = 0; Registry(&n);
    if (n < 256) { OvlTable* e = Registry(nullptr); e->version = version; e->page = page; e->fns = fns; e->count = count; e->alias = alias; e->nalias = nalias; }
}
static const OvlTable* sOvl = nullptr;
static const OvlTable* sBase = nullptr;
static const char* sVersion = nullptr;
uint32_t gPageBase = 0x2DA680;
bool SelectVersion(const char* version) {
    int n = 0; OvlTable* t = Registry(&n);
    for (int k = 0; k < n; ++k)
        if (!strcmp(t[k].version, version) && !strcmp(t[k].page, "base")) { sBase = &t[k]; sVersion = t[k].version; sOvl = nullptr; return true; }
    return false;
}
const char* SelectedVersion() { return sVersion; }
int BaseFunctionCount() { if (!sBase) SelectVersion("US"); return sBase ? sBase->count : 0; }
bool SelectOverlay(const char* page) {
    int n = 0; OvlTable* t = Registry(&n);
    if (!sVersion) SelectVersion("US");
    for (int k = 0; k < n; ++k)
        if (page && sVersion && !strcmp(t[k].page, page) && !strcmp(t[k].version, sVersion)) { sOvl = &t[k]; return true; }
    return false;
}
const char* SelectedOverlay() { return sOvl ? sOvl->page : nullptr; }
static const FnEntry* FindIn(const FnEntry* t, int count, uint32_t a) {
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (t[m].addr == a) return &t[m];
        if (t[m].addr < a) lo = m + 1; else hi = m - 1;
    }
    return nullptr;
}
// the last answers of Find, direct-mapped, for the tables they came from (every call of the recompiled code looks up
// its target)
struct FindMemo { uint32_t a = 0; const OvlTable* ovl = nullptr; const OvlTable* base = nullptr; uint32_t pageBase = 0; const FnEntry* e = nullptr; };
static FindMemo sFindMemo[1 << 14];
static const FnEntry* FindUncached(uint32_t a) {
    if (a >= gPageBase && sOvl) return FindIn(sOvl->fns, sOvl->count, a);
    if (!sBase && !SelectVersion("US")) return nullptr;
    return FindIn(sBase->fns, sBase->count, a);
}
static const FnEntry* Find(uint32_t a) {
    FindMemo& m = sFindMemo[(a >> 2) & ((1 << 14) - 1)];
    if (m.a == a && m.ovl == sOvl && m.base == sBase && m.pageBase == gPageBase && m.e) return m.e;
    const FnEntry* e = FindUncached(a);
    m.a = a; m.ovl = sOvl; m.base = sBase; m.pageBase = gPageBase; m.e = e;
    return e;
}
// ---- the other versions' maps
struct VerMap { const char* version; uint32_t gpUs, gpV; const VersionImageMap* images; int nImages; const VersionRun* runs; int nRuns; };
static VerMap sMaps[8]; static int sMapCount = 0;
VersionMapRegistration::VersionMapRegistration(const char* version, uint32_t gpUs, uint32_t gpV, const VersionImageMap* images, int nImages,
                                               const VersionRun* runs, int nRuns) {
    if (sMapCount < 8) sMaps[sMapCount++] = {version, gpUs, gpV, images, nImages, runs, nRuns};
}
static const VerMap* CurMap() {
    if (!sVersion || !strcmp(sVersion, "US")) return nullptr;
    for (int k = 0; k < sMapCount; ++k) if (!strcmp(sMaps[k].version, sVersion)) return &sMaps[k];
    return nullptr;
}
bool IsUsVersion() { return !sVersion || !strcmp(sVersion, "US"); }
static const VersionImageMap* ImageNamed(const VerMap* m, const char* page) {
    if (!page) return nullptr;
    for (int k = 0; k < m->nImages; ++k) if (!strcmp(m->images[k].page, page)) return &m->images[k];
    return nullptr;
}
// the pair of the function holding a. The pairs are in the US program's order; another version has
// moved some functions (the Japanese pages, the standalone disc's pad functions, a few of the European
// and Korean ones), so its side is searched in an index of its own order
static const VersionPair* PairOf(const VersionImageMap* im, bool us, uint32_t a) {
    if (us) {
        int lo = 0, hi = im->count - 1, best = -1;
        while (lo <= hi) {
            int k = (lo + hi) / 2;
            if (im->pairs[k].us <= a) { best = k; lo = k + 1; } else hi = k - 1;
        }
        if (best < 0) return nullptr;
        const VersionPair& p = im->pairs[best];
        return a < p.us + 4 * p.usLen ? &p : nullptr;
    }
    static std::mutex lock;
    static std::map<const VersionImageMap*, std::vector<int>> byV;
    std::lock_guard<std::mutex> g(lock);
    std::vector<int>& ix = byV[im];
    if (ix.empty() && im->count > 0) {
        for (int k = 0; k < im->count; ++k) ix.push_back(k);
        std::stable_sort(ix.begin(), ix.end(), [im](int x, int y) { return im->pairs[x].v < im->pairs[y].v; });
    }
    int lo = 0, hi = (int)ix.size() - 1, best = -1;
    while (lo <= hi) {
        int k = (lo + hi) / 2;
        if (im->pairs[ix[k]].v <= a) { best = k; lo = k + 1; } else hi = k - 1;
    }
    for (int k = best; k >= 0 && k > best - 4; --k) {            // the nearest start below a whose function holds it
        const VersionPair& p = im->pairs[ix[k]];
        if (a < p.v + 4 * p.vLen) return &p;
    }
    return nullptr;
}
static uint32_t PairMap(const VersionPair* p, bool toVer, uint32_t a) {
    const uint32_t from = toVer ? p->us : p->v, to = toVer ? p->v : p->us;
    if (a == from) return to;
    return p->same ? to + (a - from) : 0;
}
// data: the run of one delta holding the address, else the nearest run below it with more than one
// vote (the rest of the same object) in the same region (the base's data, a page's)
struct RevRun { uint32_t lo, hi; int32_t delta; uint32_t votes; };
static uint32_t DataMap(const VerMap* m, uint32_t a, bool toVer, uint32_t regionLo, uint32_t regionHi) {
    static std::vector<RevRun> rev; static const VerMap* revOf = nullptr;
    const VersionRun* runs = m->runs; int n = m->nRuns;
    if (!toVer && revOf != m) {
        rev.clear();
        for (int k = 0; k < n; ++k) rev.push_back({runs[k].usLo + runs[k].delta, runs[k].usHi + runs[k].delta, -runs[k].delta, runs[k].votes});
        std::sort(rev.begin(), rev.end(), [](const RevRun& x, const RevRun& y) { return x.lo < y.lo; });
        revOf = m;
    }
    auto lo = [&](int k) { return toVer ? runs[k].usLo : rev[k].lo; };
    auto hi = [&](int k) { return toVer ? runs[k].usHi : rev[k].hi; };
    auto delta = [&](int k) { return toVer ? runs[k].delta : rev[k].delta; };
    auto votes = [&](int k) { return toVer ? runs[k].votes : rev[k].votes; };
    int l = 0, h = n - 1, best = -1;
    while (l <= h) { int k = (l + h) / 2; if (lo(k) <= a) { best = k; l = k + 1; } else h = k - 1; }
    for (int k = best; k >= 0 && k > best - 16; --k) {
        if (lo(k) < regionLo) break;
        if (a <= hi(k)) return a + delta(k);
        if (votes(k) >= 2 && a - hi(k) < 0x1000) return a + delta(k);
    }
    return 0;
}
static const uint32_t kUsBaseTextEnd = 0x201b48;
uint32_t VerAddr(uint32_t us, const char* page) {
    const VerMap* m = CurMap();
    if (!m) return us;
    const VersionImageMap* base = ImageNamed(m, "base");
    const VersionImageMap* pim = ImageNamed(m, page ? page : (sOvl ? sOvl->page : nullptr));
    const bool inPage = pim && us >= pim->usLo;
    const VersionImageMap* im = inPage ? pim : base;
    if (im) if (const VersionPair* p = PairOf(im, true, us)) return PairMap(p, true, us);
    if (!inPage && us >= 0x100000 && us < kUsBaseTextEnd) return 0;      // base code no function of both holds
    if (inPage) {
        const uint32_t d = DataMap(m, us, true, pim->usLo, 0xFFFFFFFF);
        return d ? d : us - pim->usLo + pim->vLo;
    }
    return DataMap(m, us, true, 0, 0xFFFFFFFF);
}
uint32_t UsAddr(uint32_t a, const char* page) {
    const VerMap* m = CurMap();
    if (!m) return a;
    const VersionImageMap* base = ImageNamed(m, "base");
    const VersionImageMap* pim = ImageNamed(m, page ? page : (sOvl ? sOvl->page : nullptr));
    const bool inPage = pim && a >= pim->vLo;
    const VersionImageMap* im = inPage ? pim : base;
    if (im) if (const VersionPair* p = PairOf(im, false, a)) return PairMap(p, false, a);
    if (inPage) {
        const uint32_t d = DataMap(m, a, false, pim->vLo, 0xFFFFFFFF);
        return d ? d : a - pim->vLo + pim->usLo;
    }
    return DataMap(m, a, false, 0, 0xFFFFFFFF);
}
// the US start of the function of both holding one of the selected version's code addresses: a call
// site inside a function whose shape differs (VerAddr has no address for it) known by its function
uint32_t UsFnOf(uint32_t a, const char* page) {
    const VerMap* m = CurMap();
    if (!m) return 0;
    const VersionImageMap* base = ImageNamed(m, "base");
    const VersionImageMap* pim = ImageNamed(m, page ? page : (sOvl ? sOvl->page : nullptr));
    const VersionImageMap* im = pim && a >= pim->vLo ? pim : base;
    const VersionPair* p = im ? PairOf(im, false, a) : nullptr;
    return p ? p->us : 0;
}
bool IsFn(uint32_t a) { return Find(a) != nullptr; }
Fn FindFn(uint32_t a) { const FnEntry* e = Find(a); Fn f; if (e) { f.fn = e->fn; f.a = e->addr; } return f; }
uint32_t PageAlias(uint32_t a) {
    // the US program's table of the page (another version's platform addresses are the US ones)
    const OvlTable* t = sOvl;
    if (t && !t->alias && strcmp(t->version, "US")) {
        int n = 0; OvlTable* r = Registry(&n);
        for (int k = 0; k < n; ++k) if (!strcmp(r[k].version, "US") && !strcmp(r[k].page, t->page)) t = &r[k];
    }
    if (!t || !t->alias) return 0;
    int lo = 0, hi = t->nalias - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (t->alias[m].addr == a) return t->alias[m].served;
        if (t->alias[m].addr < a) lo = m + 1; else hi = m - 1;
    }
    return 0;
}
thread_local Budget* gBudget = nullptr;
// (v141 beta 12) the budget read at every call from a TLS slot of the thread's TEB (one instruction): gBudget, a
// thread_local, goes through emutls under MinGW (a call and TlsGetValue each time). BudgetSet keeps both.
#if defined(_WIN64) && defined(__GNUC__)
extern "C" __declspec(dllimport) unsigned long __stdcall TlsAlloc(void);
extern "C" __declspec(dllimport) int __stdcall TlsSetValue(unsigned long, void*);
static const unsigned long sBudgetSlot = TlsAlloc();     // under 64: the TEB's TlsSlots (0x1480)
static inline Budget* BudgetNow() {
    if (__builtin_expect(sBudgetSlot < 64, 1)) {
        Budget* b;
        __asm__("movq %%gs:(%1), %0" : "=r"(b) : "r"((unsigned long long)(0x1480 + 8 * sBudgetSlot)));
        return b;
    }
    return gBudget;
}
void BudgetSet(Budget* b) { gBudget = b; if (sBudgetSlot < 64) TlsSetValue(sBudgetSlot, b); }
#else
static inline Budget* BudgetNow() { return gBudget; }
void BudgetSet(Budget* b) { gBudget = b; }
#endif
Budget* BudgetGet() { return BudgetNow(); }
volatile uint32_t gLastCall = 0, gLastCaller = 0;       // for watchdogs
uint32_t gRing[64][2]; unsigned gRingPos = 0;            // the last calls, reported when a run is stopped
uint32_t gRingT[16][64][2]; unsigned gRingTPos[16];      // the same, per PS2 thread
unsigned gRingThread = 1;                                // set by the platform's scheduler
uint32_t gBackEdges = 0;
volatile uint32_t gLoopFn = 0;                           // the function of the last loop turn (BACKEDGE)
Ctx* volatile gLoopCtx = nullptr;
static thread_local jmp_buf* sBoundJb = nullptr;         // CallBounded: where its run is stopped
static thread_local uint32_t sBoundFn = 0;
static thread_local long long sBoundLeft = 0;
uint32_t gLoopFnOf[16];                                  // per PS2 thread: the function of its loop at its last check
void BackEdgeCheck() {
    gLoopFnOf[gRingThread & 15] = gLoopFn;
    if (sBoundJb && gLoopFn == sBoundFn && (sBoundLeft -= 4096) < 0) longjmp(*sBoundJb, 1);
    // a loop that never ends (the same function's back edges, 80 million in a row): its registers, once
    static uint32_t sFn = 0; static long long sSame = 0; static bool sLogged = false;
    if (gLoopFn == sFn) ++sSame; else { sFn = gLoopFn; sSame = 0; wild::LoopLeft(); }
    // (v150-host beta 16) still that loop 10 s later with no frame run: a headless game ends there (wildguard.h)
    if (sSame == wild::kLoopChecks) wild::LoopSeen(sFn);
    else if (sSame > wild::kLoopChecks && (sSame & 0x3ff) == 0) wild::LoopStill(sFn, sSame);
    if (sSame == wild::kLoopChecks && !sLogged && gLoopCtx && gLog) {
        sLogged = true;
        const Ctx& c = *gLoopCtx;
        char b[1600]; int n = snprintf(b, sizeof b, "endless loop in %08x: f", gLoopFn);
        for (int i = 0; i < 32 && n < (int)sizeof b - 20; ++i) n += snprintf(b + n, sizeof b - n, " %g", c.f[i]);
        n += snprintf(b + n, sizeof b - n, " r");
        for (int i = 1; i < 32 && n < (int)sizeof b - 20; ++i) n += snprintf(b + n, sizeof b - n, " %x", (uint32_t)c.r[i]);
        gLog("%s", b);
        // the PS2 stack: the return addresses saved in the frames above (words that look like code)
        {
            const uint32_t sp = (uint32_t)c.r[29] & 0x01FFFFF0u;
            n = snprintf(b, sizeof b, "endless loop: stack %08x, code addresses in it:", (uint32_t)c.r[29]);
            for (uint32_t o = 0; o < 0x800 && n < (int)sizeof b - 20; o += 4) {
                uint32_t v; memcpy(&v, gMem + ((sp + o) & 0x01FFFFFFu), 4);
                if (v >= 0x100000u && v < 0x700000u && (v & 3) == 0) n += snprintf(b + n, sizeof b - n, " +%x=%08x", o, v);
            }
            gLog("%s", b);
        }
        // what the argument registers point at (floats), and the call ring's callers
        for (int ri = 4; ri <= 9; ++ri) {
            const uint32_t a = (uint32_t)c.r[ri] & 0x01FFFFF0u;
            if (a < 0x100000u || a > 0x01FFFF00u) continue;
            n = snprintf(b, sizeof b, "endless loop: r%d %08x:", ri, (uint32_t)c.r[ri]);
            for (int k = 0; k < 32 && n < (int)sizeof b - 20; ++k) { float v; memcpy(&v, gMem + a + 4 * k, 4); n += snprintf(b + n, sizeof b - n, " %g", v); }
            gLog("%s", b);
        }
    }
    Budget* const bu = BudgetNow();
    if (!bu) return;
    bu->left -= 256;                                        // 4096 back edges weigh 256 calls
    if (bu->left < 0) {
        if (bu->yield) { bu->yield(); bu->left = bu->refill; }
        else if (bu->jb) longjmp(*(jmp_buf*)bu->jb, 1);
    }
}
bool CallBounded(Ctx& c, uint32_t addr, long long maxEdges) {
    const FnEntry* e = Find(addr);
    if (!e) { c.r[2] = 0; return true; }
    static const int kKeep[12] = {16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30, 31};
    uint64_t keep[12];
    for (int i = 0; i < 12; ++i) keep[i] = c.r[kKeep[i]];
    jmp_buf jb;
    jmp_buf* const outerJb = sBoundJb; const uint32_t outerFn = sBoundFn; const long long outerLeft = sBoundLeft;
    if (setjmp(jb) == 0) {
        sBoundJb = &jb; sBoundFn = addr; sBoundLeft = maxEdges;
        e->fn(c, addr);
        sBoundJb = outerJb; sBoundFn = outerFn; sBoundLeft = outerLeft;
        return true;
    }
    sBoundJb = outerJb; sBoundFn = outerFn; sBoundLeft = outerLeft;
    for (int i = 0; i < 12; ++i) c.r[kKeep[i]] = keep[i];
    c.r[2] = 0;
    return false;
}
bool gCallTrace = false;                                 // every call into the screen code, for the simulation
bool gCallTraceAll = false;                              // every call, any address
// call tree of one run of a function (the platform arms it): each call inside, indented by depth
uint32_t gTreeRoot = 0; int gTreeLines = 0; bool gTreeArmed = false;
static int sTreeDepth = -1;
static void CallInner(Ctx& c, uint32_t addr);
void Call(Ctx& c, uint32_t addr) {
    if (__builtin_expect(!gTreeArmed && sTreeDepth < 0, 1)) { CallInner(c, addr); return; }
    const bool root = sTreeDepth < 0 && gTreeArmed && addr == gTreeRoot;
    if (root) { sTreeDepth = 0; gTreeArmed = false; }
    if (sTreeDepth < 0) { CallInner(c, addr); return; }
    if (gTreeLines > 0 && gLog) {
        --gTreeLines;
        gLog("tree: %*s%08x from %08x (a0 %08x a1 %08x a2 %08x a3 %08x)", 2 * (sTreeDepth < 40 ? sTreeDepth : 40), "", addr, (uint32_t)c.r[31] - 8,
             (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], (uint32_t)c.r[7]);
    }
    ++sTreeDepth;
    CallInner(c, addr);
    --sTreeDepth;
    if (root) { sTreeDepth = -1; if (gLog) gLog("tree: end of %08x (v0 %08x)", addr, (uint32_t)c.r[2]); }
}
void (*gCallHook)(uint32_t addr) = nullptr;               // every call, while set (diagnostics)
static void CallInner(Ctx& c, uint32_t addr) {
    if (__builtin_expect(__atomic_load_n(&gIrqPending, __ATOMIC_RELAXED) != 0, 0) && gIrqDeliver && !gInIrq && gEie) gIrqDeliver();
    if (__builtin_expect(gCallHook != nullptr, 0)) gCallHook(addr);
    gLastCall = addr; gLastCaller = (uint32_t)c.r[31];
    gRing[gRingPos & 63][0] = addr; gRing[gRingPos & 63][1] = (uint32_t)c.r[31]; ++gRingPos;
    { unsigned th = gRingThread & 15, k = gRingTPos[th]++ & 63; gRingT[th][k][0] = addr; gRingT[th][k][1] = (uint32_t)c.r[31]; }
    if (gCallTrace && (gCallTraceAll || (addr >= 0x348000 && addr < 0x3a6000)) && gLog)
        gLog("trace: %08x from %08x (a0 %08x a1 %08x a2 %08x)", addr, (uint32_t)c.r[31] - 8, (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6]);
    if (Budget* const bu = BudgetNow(); bu && --bu->left < 0) {
        if (bu->yield) { bu->yield(); bu->left = bu->refill; }
        else if (bu->jb) longjmp(*(jmp_buf*)bu->jb, 1);
    }
    if (gBridge && gBridge(c, addr)) return;
    if (addr == 0) {                                     // a null function pointer
        Once("call through a null pointer, from", (uint32_t)c.r[31] - 8, 0);
        c.r[2] = 0;
        return;
    }
    if (const FnEntry* e = Find(addr)) { e->fn(c, addr); return; }
    Once("call to a function not recompiled nor bridged", addr, 0);
    c.r[2] = 0;
}
}  // namespace ps2
