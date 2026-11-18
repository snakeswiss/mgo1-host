// ps2world.cpp - see ps2world.h. Every bridge call is logged (trace ps2).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "gs_thread.h"
#include "ps2world.h"
#include "ps2rt.h"
#include "inflate.h"
#include "disc.h"
#include "gs_gpu.h"
#include "hdtex.h"
#include "iop.h"
#include "stagedat.h"
#include "extramaps.h"
#include "../native/version.h"
#include <windows.h>
#include <psapi.h>
#include "cpuclass.h"
#include "collguard.h"                                  // the collision guards' plain parts
#include <cstdio>
#include <cstring>
#include <cctype>
#include <map>
#include <functional>
#include <set>
#include <string>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>
#include <deque>
#include <csetjmp>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include <cmath>
#ifndef MGO1_PLAYER_BUILD
#include "lobby_filter.h"                                   // the dedicated host's lobby filter (net::DollFilter*)
#include "vdoll.h"                                          // the virtual dolls (docs/VIRTUAL_DOLL.md)
#include "doll_geom.h"                                      // (the virtual dolls' respawn points)
#include "vdoll_trap.h"                                     // (v150-host beta 21) the claymores for the virtual dolls
#include "vdoll_book.h"                                     // (v150-host beta 22, book) the books for the virtual dolls
#include <cstdarg>
#endif
// ((v150-host beta 25, review25) both builds: FoodRecords and the stage's key are compiled in the player build too, which
// stopped compiling without them, -DMGO1_PLAYER_BUILD)
#include "vdoll_food.h"                                     // (v150-host beta 25, food) the food a player's game eats
#include "stagekey.h"                                       // (v150-host beta 22, sync) the page and extra map of one stage
// the switches of the tests, the simulation and the dedicated host (SIM_*, TRACE_*, MGO1_* but the
// memory card's folder and the host's log-in, both kept): unset in the player build (native/build.sh
// -DMGO1_PLAYER_BUILD), their code compiled out; getenv in the host's and the tests' (docs/NATIVE_BUILD.md)
#ifdef MGO1_PLAYER_BUILD
#define DevEnv(name) ((const char*)nullptr)
#define FPS60_KIND 0                                    // exact 60 only: the full rate is the simulation's (docs/FPS60.md)
#else
#define DevEnv(name) getenv(name)
#define FPS60_KIND P.fps60Kind
#endif
namespace ps2 {
void BudgetSet(Budget* b);                                  // ps2rt.cpp: gBudget and its TEB slot (the calls' fast read)
Budget* BudgetGet();
extern uint32_t gBackEdges;
extern volatile uint32_t gLastCall, gLastCaller;
extern uint32_t gRing[64][2]; extern unsigned gRingPos;
extern uint32_t gRingT[16][64][2]; extern unsigned gRingTPos[16]; extern unsigned gRingThread;
extern uint32_t gDmaLog;                                    // ps2rt.cpp: DMA starts left to log
extern int gDmaLogCh;                                       // and of which channel (-1: all)
extern uint64_t gVu1Ns, gVu1Runs;                           // vu0.cpp: time in VU1 programs
extern long long gVuCompiledRuns, gVuInterpRuns;            // vu0.cpp: runs of recompiled / interpreted programs
extern uint32_t gDmaStarts[16];                             // ps2rt.cpp: starts per channel
extern bool gHeadless;                                      // gs.cpp: no picture (MGO1_HEADLESS)
extern uint64_t gDmaNs[3];                                  // ps2rt.cpp: time in the VIF0, VIF1, GIF DMA starts (stats)
extern bool gVifStatsOn; std::string VifStats();             // vu0.cpp: the unpacks' forms (MGO1_VIF_STATS)
void DumpRing(const char* why);
void DumpRingThread(unsigned th, const char* why);
extern void (*gWatchLog)(uint32_t a);
extern void (*gLog)(const char* fmt, ...);
extern bool (*gBridge)(Ctx& c, uint32_t addr);
extern uint64_t (*gWinRead)(uint32_t a, int size);
extern void (*gWinWrite)(uint32_t a, uint64_t v, int size);
extern unsigned char gVariant[64];                          // ps2rt.cpp: the generated code's variants on (recomp/variants.txt)
}
namespace ps2 { void M37Reset(); extern bool gCallTrace, gCallTraceAll; extern uint32_t gLoopFnOf[16]; }
// (ps2rt.cpp) a function whose loop can run forever on garbage, run with a budget of its own back edges:
// past it, out at once (its caller's s0-s7, gp, sp, fp, ra back, v0 = 0). False when it was stopped
namespace ps2 { bool CallBounded(Ctx& c, uint32_t addr, long long maxEdges); }
namespace ps2 { extern volatile unsigned long long gFrames; }   // (v150-host beta 16) wildguard.h's frame count (declared here, not in ps2rt.h: the generated code's objects follow that header)
namespace pagemap {                                    // gen/g_page_map.cpp (tools/page_map.py)
struct Pair { uint32_t n3, len, page; };
struct Map { const char* page; const Pair* pairs; int count; };
extern const Map kMaps[];
extern const int kMapCount;
extern const uint32_t kN3Lo, kN3Hi;
}
namespace ps2world {
using namespace ps2;
bool IsRecompiled(uint32_t a);
void StageAsked(Ctx& c);                      // (v150-host beta 22, sync) a map page's load asked (below, with StageNow)
namespace {
std::map<uint32_t, uint32_t> gCallCounts;     // SIM_CALL_COUNTS
std::map<uint32_t, uint32_t> gFnDiff[2];      // SIM_FN_DIFF: the functions called in window A, in window B
long long gFnDiffW[4] = {-1, -1, -1, -1};     // SIM_FN_DIFF: the two windows' first and last frames
}
namespace {
#ifndef MGO1_PLAYER_BUILD
namespace report {                                   // the Game Report (below)
bool On(); void LobbyData(bool out, const uint8_t* p, int n); void ScreenText(const char* t); void HudMessage(uint32_t code, uint32_t arg);
void Stop(const char* why);
}
#endif
Platform P{};
bool gReady = false;
// the native build (NativeRun): MGO1 boots from its own entry and runs its own main loop;
// the platform is the PS2's system only (kernel, VBlank, IOP modules), no PC engine service
bool gNative = false;
std::map<uint32_t, std::string> gNativeActors;       // native build: the game's named actors by work (GV_SetNamedActor)
void (*gMessageHook)(uint32_t actor, uint32_t code, uint32_t arg) = nullptr;   // the PLAYABLE DOLL's view of the game's messages
std::atomic<uint32_t> gRadioChannel{0};                 // the doll's radio channel (SetRadioChannel)
std::mutex gRadioLock;
std::vector<uint32_t> gRadioCalls;                      // the calls read on it, not taken yet
uint32_t gLastItemBox = 0;                             // the last NewItemBox* made (BoxMade; tests: ItemBoxPos)
unsigned gItemBoxesMade = 0;                           // their count
std::atomic<long long> gDgFrames{0};
bool gPal = false;                                     // the program set a PAL display (SetGsCrt mode 3): FieldTicks
bool gMapAsks30 = false;                               // the map loaded asked DG for 30 frames a second (0x144190(1))
std::atomic<long long> gTicks{0};                      // the ticks of a map at half rate (GameTicks)
long long gExtraPics = 0, gExtraLate = 0;              // exact 60: the pictures drawn between the PS2's passes, the ones left out
// exact 60's pace guard (docs/FPS60.md, A machine that cannot draw 60 pictures a second): a tick is measured from its
// own picture's kick to the next one's (planned: 2 VBlanks, PAL at 60 its 2 or 3); a third of the last 30 ticks late,
// or 4 in a row, turn the pictures between off for 30 ticks, each tick's own picture then at the PS2's frame target,
// longer while the pictures, timed on the GS, could not be drawn at 60 (paceguard.h)
#include "paceguard.h"
PaceGuard gPace;
long long gTickKickVb = -1, gTickKickPrev = -1;         // gFrameNo at the last two tick pictures' kicks (DispatchBridge)
long long gTickKickAt = 0, gTickKickAtPrev = 0;         // their times (QpcNow)
int gTickPlan = 2;                                      // the VBlanks planned for the tick now measured
long long gVBlankAt = 0;                                // the last VBlank's time on the kernel's field clock (QpcNow)
long long gPicKickAt = 0, gPicVBlankAt = 0;             // the last picture's kick, the VBlank before it
int gPicsTimed = 0, gPicsOver = 0;                      // pictures timed on the GS since the guard's last tick, over a field
bool gTickAt30 = false;                                 // this tick's own picture at the PS2's target 2 (NativeVBlankStart)
// exact 60 (docs/FPS60.md): a map that asks for 30 shown at 60 with the PS2's own game at its pace
bool Exact60() { return P.matchFps == 60 && FPS60_KIND == 0 && gMapAsks30; }
// PAL at 60 (docs/FPS60.md): the European program's maps tick 25 times a second (two PAL fields).
// At the 60 setting its fields run at 60 a second while a map runs at half rate, the game's field
// count (0x23B8E8) kept at PAL's 50 a second (one VBlank in six not counted), and each tick has 2 or 3
// pictures (12 for 5 ticks): the extra ones after the tick, then the tick's own, each one drawn where
// what moves is at its time between the last two ticks (pictures a steady 1/60 s apart).
bool gPal60 = false;
bool gInExtra = false;                                 // a picture between the ticks is being drawn (the half flag 0 meanwhile)
long long gPalGroup = 0;                               // ticks since PAL at 60 began
int gPalGroupVb = 0;                                   // VBlanks since the last tick picture went out
int gPalLastKick = 0;                                  // gPalGroupVb when this tick's last picture went out
uint32_t gPal60Target = 1;                             // DG's frame target for the next picture
bool gPalTickPending = false;                          // the tick's own picture, drawn at gPalTickT
float gPalTickT = 1.0f;
long long gPalFieldAcc = 0;                            // sixths of a field: a VBlank counts 5
// the fractions of each tick's pictures between the last two ticks, in twelfths, 5 ticks in a row
const int kPalFrac[5][3] = {{0, 5, 10}, {3, 8, -1}, {1, 6, 11}, {4, 9, -1}, {2, 7, -1}};
std::atomic<int> gFpsWant{-1};                         // SetMatchFps: the rate to apply at the next VBlank                  // native build: DG's frames drawn (its frame actor's waits 0x143A40 / 0x143B00)
std::set<long long> gDumpAt;                           // MGO1_DUMP: frames whose memory is dumped (a KEROTAN in a goal)
uint32_t gHeapEnd = 0;                        // native: SetupHeap's end (EndOfHeap)
std::map<uint32_t, uint32_t> gSoftKeyboards;  // native: live NewSoftwareKeyboard_* actors (work -> act)
// The platform's own blocks in PS2 memory (strings and packets it hands to PS2 code, the
// EE TCP/IP stack's malloc): 0x1B00000..0x1D10000, RAM the PS2 gives to its display
// buffers (the DG, not run here). Actor works and the game's allocations live in the
// game's own pools (pool 0 at 0xF00000..0x1B00000, created again at each stage change).
uint32_t kHeapLo = 0x01B00000, kHeapHi = 0x01D10000;          // native: the kernel's RAM (NativeRun)
const uint32_t kObjLo = 0x01C00000, kObjHi = 0x01D00000;     // layout objects, kSlot each
const uint32_t kSlot = 0x100;
const uint32_t kHndLo = 0x01D00000, kHndHi = 0x01D80000;     // layout handles, 0x100 each
const uint32_t kGclLo = 0x01D80000;                          // script option tokens, 0x10 each
uint32_t kStackTop = 0x01FF0000;                                // native: the kernel's RAM (NativeRun)
uint32_t gHeap = kHeapLo;
uint32_t gGclNext = kGclLo;
std::vector<void*> gHandles;                  // token index -> PC handle
std::vector<void*> gObjs;                     // token index -> PC node
std::map<void*, uint32_t> gObjTok;
// the PS2 GCL cursor (0x2CF5B0) mirrors the host cursor; PS2 code sees tokens,
// each standing for one host script address
const uint32_t kGclCursor = 0x2CF5B0;
std::vector<unsigned char*> gTokPtr;
std::map<unsigned char*, uint32_t> gTokOf;
uint32_t TokOf(unsigned char* p) {
    if (!p) return 0;
    auto it = gTokOf.find(p);
    if (it != gTokOf.end()) return it->second;
    uint32_t t = 0x01D80000u + (uint32_t)gTokPtr.size() * 0x10;
    gTokPtr.push_back(p);
    gTokOf[p] = t;
    return t;
}
unsigned char* PtrOf(uint32_t t) {
    if (t < 0x01D80000u) return nullptr;
    size_t k = (t - 0x01D80000u) / 0x10;
    return k < gTokPtr.size() ? gTokPtr[k] : nullptr;
}
unsigned char* Cur() { return P.gclCursor ? *P.gclCursor : nullptr; }


void SetCur(unsigned char* p) { if (P.gclCursor) *P.gclCursor = p; }
bool AtParam(unsigned char* p) { return p && *p && (*p & 0xf0) != 0x50; }
struct Actor { uint32_t work, act, die; void* pc; bool live; std::string name; uint64_t level = 0; uint32_t poolGen = 0;
               bool dying = false, ownRetry = false; };   // dying: its die is not done (retried each frame)
std::vector<Actor> gActors;
std::map<void*, int> gByPc;
std::set<uint64_t> gOnce;
uint32_t gMainGp = 0;                         // $gp of the PS2 code, from the first CreateThread
void Schedule(int maxSlices = 64);

extern long long gFrameNo;
// no log-in data in a log or in a file sent to troubleshoot: the account (a whole word), its
// password (anywhere), the game's password (a whole word), masked as ***
struct Secret { std::string v; bool word; };
void AddSecret(const std::string& v, bool word);
std::vector<Secret>& Secrets() {
    static std::vector<Secret> s;
    static bool init = false;
    if (!init) {
        init = true;
        for (const char* e : {"HOST_ACCOUNT_PASSWORD", "HOST_ACCOUNT_ID"})
            if (const char* v = getenv(e)) AddSecret(v, e[13] == 'I');
    }
    return s;
}
void AddSecret(const std::string& v, bool word) {
    std::string u = v;
    for (auto& ch : u) ch = (char)toupper((unsigned char)ch);
    if (u.size() < (word ? 1u : 3u)) return;
    auto& s = Secrets();
    for (const auto& x : s) if (x.v == u) return;
    s.push_back({u, word});
    std::sort(s.begin(), s.end(), [](const Secret& a, const Secret& b) { return a.v.size() > b.v.size(); });
}
std::string Redact(const std::string& in) {
    std::string t = in;
    for (const auto& s : Secrets()) {
        std::string up = t;
        for (auto& ch : up) ch = (char)toupper((unsigned char)ch);
        std::string out;
        size_t i = 0, j;
        bool any = false;
        while ((j = up.find(s.v, i)) != std::string::npos) {
            auto wordc = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
            const bool inside = s.word && ((j && wordc(up[j - 1])) || (j + s.v.size() < up.size() && wordc(up[j + s.v.size()])));
            out += t.substr(i, j - i);
            out += inside ? t.substr(j, s.v.size()) : "***";
            any |= !inside;
            i = j + s.v.size();
        }
        if (any) t = out + t.substr(i);
    }
    return t;
}
void Scrub(uint8_t* p, size_t n) {                  // a memory dump: every secret's bytes (any case) become '*'
    for (const auto& s : Secrets()) {
        const size_t m = s.v.size();
        if (!m || m > n) continue;
        for (size_t i = 0; i + m <= n; ++i) {
            if (toupper(p[i]) != (unsigned char)s.v[0]) continue;
            size_t k = 1;
            while (k < m && toupper(p[i + k]) == (unsigned char)s.v[k]) ++k;
            if (k == m) { memset(p + i, '*', m); i += m - 1; }
        }
    }
}
void Log(const char* fmt, ...) {
    if (!P.log) return;
    char b[1024];
    va_list ap; va_start(ap, fmt); _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap); va_end(ap);
    P.log("ps2 f%lld: %s", gFrameNo, Redact(b).c_str());   // the frame number ties every line to the timeline
}
bool Once(uint64_t key) { return gOnce.insert(key).second; }
long long QpcNow(); long long QpcFreq(); unsigned long long PsMs();   // the PS2's time (SetTimeScale)
long long RealQpc();                                                // the machine's
long long NetQpc();                                                 // the network's (SetNetTimeScale)
void VersionAddresses();
// ---- the other versions of the program (docs/VERSIONS.md). The platform's addresses are the US
// program's: V(us) is the running version's address of one (code, the base's data, the page's), U(a)
// the US address of one of its (0: none). The US program: the same addresses.
bool gUsProgram = true;
uint32_t gPageGen = 0;                                 // the page selected changed (its addresses: its map)
std::map<uint64_t, uint32_t> gVCache, gUCache;
// in front of both maps a direct-mapped cache of the last answers (U runs on every call of another version's code)
struct AddrMemo { uint64_t k = 0; uint32_t v = 0; };        // k 0: empty (no address 0 is asked)
AddrMemo gVMemo[1 << 14], gUMemo[1 << 16];
uint32_t V(uint32_t us) {
    if (gUsProgram) return us;
    const uint64_t k = ((uint64_t)gPageGen << 32) | us;
    AddrMemo& m = gVMemo[(us >> 2) & ((1 << 14) - 1)];
    if (m.k == k && k) return m.v;
    auto it = gVCache.find(k);
    if (it != gVCache.end()) { m.k = k; m.v = it->second; return it->second; }
    uint32_t v = ps2::VerAddr(us);
    if (!v && Once(((uint64_t)40 << 40) | us)) Log("versions: the US address %08x has none in the %s program", us, ps2::SelectedVersion());
    gVCache[k] = v;
    m.k = k; m.v = v;
    return v;
}
int StubNumber(uint32_t addr);
// the kernel's stubs of the running program: each by its number and its rank among the stubs of
// that number (AddIntcHandler and AddIntcHandler2 are both syscall 0x10), the US one of the same
std::map<uint32_t, uint32_t> gStubToUs;
void MapStubs() {
    std::map<int, int> rank;
    for (uint32_t a = 0x100000; a + 16 < 0x201b00; a += 4) {        // the text, as tools/page_heads.py scans the US one
        const uint32_t w = R32(a);
        if ((w & 0xffff0000) != 0x24030000) continue;
        for (int k = 1; k <= 2; ++k)
            if (R32(a + 4 * k) == 0x0000000c && (R32(a + 4 * k + 4) == 0x03e00008 || R32(a + 4 * k + 8) == 0x03e00008)) {
                const int n = (int16_t)(w & 0xffff), r = rank[n]++;
                int seen = 0;
                for (int i = 0; i < kUsStubCount; ++i)
                    if (kUsStubs[i].number == n && seen++ == r) { gStubToUs[a] = kUsStubs[i].addr; break; }
                break;
            }
    }
}
uint32_t UsStubAddr(int n) {
    for (int i = 0; i < kUsStubCount; ++i) if (kUsStubs[i].number == n) return kUsStubs[i].addr;
    return 0;
}
uint32_t U(uint32_t a) {
    if (gUsProgram) return a;
    const uint64_t k = ((uint64_t)gPageGen << 32) | a;
    AddrMemo& m = gUMemo[(a >> 2) & ((1 << 16) - 1)];
    if (m.k == k && k) return m.v;
    auto it = gUCache.find(k);
    if (it != gUCache.end()) { m.k = k; m.v = it->second; return it->second; }
    uint32_t u = 0;
    if (auto st = gStubToUs.find(a); st != gStubToUs.end()) u = st->second;               // a kernel stub: the US one of its number and rank
    if (!u) u = ps2::UsAddr(a);
    gUCache[k] = u;
    m.k = k; m.v = u;
    return u;
}
// ---- Maps on other stages (docs/EXTRA_MAPS.md): the extra maps' page features were made on City Under Siege's page
// (US n003a addresses); every map page links the same code at its own addresses. gen/g_page_map.cpp
// (tools/page_map.py) pairs n003a's functions with each map page's (the same shape, in the same order). P3: the
// loaded page's US address of an n003a one (a function's pair plus the offset in it; the base's addresses and the
// page's data as they are, 0 for code not paired); A3: the n003a address of a function of the loaded page (0: none);
// V3: V of P3. N3Page: City Under Siege's page, or another map page while an extra map is built on it (a stock
// round of that page runs as before)
const pagemap::Map* gPageMap = nullptr;                // the loaded page's pairs (none on n003a and the other pages)
std::vector<std::pair<uint32_t, uint32_t>> gPageToN3;  // its paired functions: {its address, n003a's}, by address
std::string gOverlayName = "ntmenu2";
bool gPageN3 = false;                                  // the loaded page is n003a
struct A3Memo { uint32_t gen = 0, us = 0, n3 = 0; };   // gen: gPageGen + 1 (0: empty)
A3Memo gA3Memo[1 << 12];
void PageMapSelect(const std::string& page) {
    gPageMap = nullptr;
    gPageToN3.clear();
    gPageN3 = page == "n003a";
    for (int k = 0; k < pagemap::kMapCount; ++k)
        if (page == pagemap::kMaps[k].page) gPageMap = &pagemap::kMaps[k];
    if (!gPageMap) return;
    for (int i = 0; i < gPageMap->count; ++i) gPageToN3.push_back({gPageMap->pairs[i].page, gPageMap->pairs[i].n3});
    std::sort(gPageToN3.begin(), gPageToN3.end());
}
bool N3Page() { return gPageN3 || (gPageMap && ExtraMapsLoaded()); }
uint32_t P3(uint32_t us3) {
    if (!gPageMap || us3 < pagemap::kN3Lo || us3 >= pagemap::kN3Hi) return us3;
    const pagemap::Pair* p = gPageMap->pairs;
    int lo = 0, hi = gPageMap->count - 1, at = -1;
    while (lo <= hi) {                                 // the last pair from us3 down
        const int mid = (lo + hi) / 2;
        if (p[mid].n3 <= us3) { at = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (at < 0 || us3 - p[at].n3 >= p[at].len) return 0;
    return p[at].page + (us3 - p[at].n3);
}
uint32_t A3(uint32_t us) {
    if (!gPageMap) return us;
    if (gPageToN3.empty() || us < gPageToN3.front().first || us > gPageToN3.back().first) return 0;   // the base's own
    A3Memo& m = gA3Memo[(us >> 2) & ((1 << 12) - 1)];
    if (m.gen == gPageGen + 1 && m.us == us) return m.n3;
    const auto it = std::lower_bound(gPageToN3.begin(), gPageToN3.end(), std::make_pair(us, 0u));
    const uint32_t n3 = it != gPageToN3.end() && it->first == us ? it->second : 0;
    m.gen = gPageGen + 1; m.us = us; m.n3 = n3;
    return n3;
}
uint32_t V3(uint32_t us3) {
    const uint32_t p = P3(us3);
    return p ? V(p) : 0;
}
// the page area: from where the running version loads its pages, as far as the US program's largest
int gTestLanguage = 0;                                 // tests (=lang:N steps): the scripts' language from then on
uint32_t PageLo() { return ps2::gPageBase; }
uint32_t PageHi() { return ps2::gPageBase + (0x645680 - 0x2DA680); }
bool InPageArea(uint32_t a) { return a >= PageLo() && a < PageHi(); }
// ---- A page's data address in the running version, read from the code that builds it: the index-th address the
// function fnUs builds (lui, then addiu, or a load or a store off that register), in the version's copy of it. The
// versions keep a page's data elsewhere; VerAddr's data runs guess it from the references near it and miss some (US
// n003a 0x2E4730 in the Korean program, 0x555E40 in the Japanese and European ones: tools/page_data.py lists them).
// The US program: us (its own copy decoded once as a check).
static bool GprLoad(uint32_t op) {
    return op == 0x20 || op == 0x21 || op == 0x22 || op == 0x23 || op == 0x24 || op == 0x25 || op == 0x26 || op == 0x27 ||
           op == 0x37 || op == 0x1e || op == 0x1a || op == 0x1b;
}
static bool MemOp(uint32_t op) {
    return GprLoad(op) || op == 0x28 || op == 0x29 || op == 0x2b || op == 0x3f || op == 0x1f || op == 0x2a || op == 0x2e ||
           op == 0x2c || op == 0x2d || op == 0x31 || op == 0x35 || op == 0x39 || op == 0x3d;
}
uint32_t BuiltAddress(uint32_t f, int index) {
    uint32_t hi[32] = {};
    bool has[32] = {};
    int found = 0;
    for (int i = 0; f && i < 512; ++i) {
        const uint32_t w = R32(f + 4 * (uint32_t)i), op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
        const uint32_t simm = (uint32_t)(int32_t)(int16_t)(w & 0xFFFF);
        if (op == 0x0F) { hi[rt] = (w & 0xFFFF) << 16; has[rt] = true; continue; }                 // lui
        if ((op == 0x09 || op == 0x19) && has[rs]) {                                              // addiu, daddiu
            if (found++ == index) return hi[rs] + simm;
            has[rt] = false;
            continue;
        }
        if (MemOp(op) && has[rs]) {
            if (found++ == index) return hi[rs] + simm;
            if (GprLoad(op)) has[rt] = false;
            continue;
        }
        if (op == 0) has[(w >> 11) & 31] = false;                                                 // SPECIAL: rd
        else if (GprLoad(op) || !(MemOp(op) || op == 0x01 || op == 0x02 || op == 0x03 || op == 0x04 || op == 0x05 ||
                                  op == 0x06 || op == 0x07 || op == 0x14 || op == 0x15 || op == 0x16 || op == 0x17)) has[rt] = false;
    }
    return 0;
}
uint32_t PageData(uint32_t fnUs, int index, uint32_t us) {
    static std::map<uint64_t, uint32_t> cache;
    const uint64_t k = ((uint64_t)gPageGen << 40) ^ ((uint64_t)index << 32) ^ fnUs;
    if (auto it = cache.find(k); it != cache.end()) return it->second;
    const uint32_t f = V3(fnUs), a = f ? BuiltAddress(f, index) : 0;
    uint32_t r = a;
    if (gPageMap) {                                    // another map page: its own copy's (no US n003a address to check)
        static std::map<std::pair<std::string, uint32_t>, uint32_t> said;
        auto& was = said[{gOverlayName, us}];
        if (was != a) {
            was = a;
            Log("pages: n003a's %08x is %08x in the %s page (from %08x's copy at %08x)", us, a, gOverlayName.c_str(), fnUs, f);
        }
    } else if (gUsProgram) {
        if (a != us && Once(((uint64_t)0xDA << 40) | us)) Log("versions: %08x's address %d is %08x, not %08x", fnUs, index, a, us);
        r = us;
    } else if (!a) {
        r = V(us);
        if (Once(((uint64_t)0xDB << 40) | us)) Log("versions: the US address %08x not found in %08x's copy (%08x): %08x", us, fnUs, f, r);
    } else {
        static std::map<uint32_t, uint32_t> said;
        if (said[us] != a) {
            said[us] = a;
            Log("versions: the US address %08x is %08x in the %s program (from %08x's copy at %08x)", us, a, ps2::SelectedVersion(), fnUs, f);
        }
    }
    cache[k] = r;
    return r;
}
// US n003a's data the added weapons and the drops use, in the running version (only while n003a or a paired page is the page)
uint32_t N3Weapons() { return PageData(0x2FCB28, 1, 0x2DAB38); }       // the weapons' table (0x28 each)
uint32_t N3Items() { return PageData(0x2FE060, 0, 0x2DCB98); }         // the items' table (0x24 each)
uint32_t N3WeaponDescs() { return PageData(0x2FCB98, 0, 0x56B718); }   // the weapons' descriptors (0x38 each)
uint32_t N3ItemDescs() { return PageData(0x2FCBB8, 0, 0x56D3C0); }     // the items' descriptors (0x38 each)
uint32_t N3HeldDesc() { return PageData(0x2FCC38, 0, 0x56E914); }      // the word holding the held weapon's descriptor
uint32_t N3HudIcons() { return PageData(0x371B18, 0, 0x2E4730); }      // the HUD's 40 icon slots (8 each)
uint32_t N3DropsRef() { return PageData(0x3A1BA0, 0, 0x2E5268); }      // the word holding the drops' manager
uint32_t N3MenuPairs(int row) { return row ? PageData(0x3C8F20, 1, 0x555E70) : PageData(0x3C8F20, 0, 0x555E40); }
uint32_t N3MenuSlots(int row) { return row ? PageData(0x3CBDC8, 1, 0x5563D0) : PageData(0x3CBDC8, 0, 0x556378); }
uint32_t N3MenuTitles() { return PageData(0x3CBB08, 2, 0x556368); }
std::map<uint32_t, uint32_t> gHeapFree, gHeapUsed;     // start -> size (free blocks coalesced)
uint32_t Alloc(uint32_t n, uint32_t align) {
    n = (n + 15) & ~15u;
    if (align < 16) align = 16;
    for (auto it = gHeapFree.begin(); it != gHeapFree.end(); ++it) {
        uint32_t a = (it->first + align - 1) & ~(align - 1), end = it->first + it->second;
        if (a + n > end) continue;
        uint32_t lo = it->first;
        gHeapFree.erase(it);
        if (a > lo) gHeapFree[lo] = a - lo;
        if (a + n < end) gHeapFree[a + n] = end - (a + n);
        gHeapUsed[a] = n;
        memset(gMem + a, 0, n);
        return a;
    }
    uint32_t a = (gHeap + align - 1) & ~(align - 1);
    if (a + n > kHeapHi) { Log("platform heap full (%u bytes asked)", n); return 0; }
    if (a > gHeap) gHeapFree[gHeap] = a - gHeap;
    gHeap = a + n;
    static uint32_t mark = kHeapLo + 0x20000;
    if (gHeap > mark) {                                // the heap's growth, each 128 KB
        uint32_t used = 0; for (auto& u : gHeapUsed) used += u.second;
        Log("platform heap: top %08x (%#x in use in %zu blocks, %#x bytes asked)", gHeap, used + n, gHeapUsed.size() + 1, n);
        while (mark < gHeap) mark += 0x20000;
    }
    gHeapUsed[a] = n;
    memset(gMem + a, 0, n);
    return a;
}
uint32_t Alloc(uint32_t n) { return Alloc(n, 16); }
uint32_t HeapSize(uint32_t a) { auto it = gHeapUsed.find(a); return it == gHeapUsed.end() ? 0 : it->second; }
void Free(uint32_t a) {
    auto it = gHeapUsed.find(a);
    if (it == gHeapUsed.end()) return;
    uint32_t lo = a, n = it->second;
    gHeapUsed.erase(it);
    auto nx = gHeapFree.find(lo + n);                  // coalesce with the next free block
    if (nx != gHeapFree.end()) { n += nx->second; gHeapFree.erase(nx); }
    auto pv = gHeapFree.lower_bound(lo);               // and with the previous one
    if (pv != gHeapFree.begin()) { --pv; if (pv->first + pv->second == lo) { lo = pv->first; n += pv->second; gHeapFree.erase(pv); } }
    gHeapFree[lo] = n;
}
uint32_t PutStr(const char* s) {
    size_t n = strlen(s) + 1;
    uint32_t a = Alloc((uint32_t)n);
    if (a) memcpy(gMem + a, s, n);
    return a;
}

// The menu's texts are blocks of consecutive strings ([27b5ee] keeps a string
// resource as the base of a block and the screens walk the strings after it), so a
// string of the host script's resources is handed to the PS2 code as the same
// offset inside a PS2 copy of the whole string data.
struct StrMirror { const unsigned char* offs = nullptr; const char* data = nullptr; const char* end = nullptr; uint32_t ps2 = 0; };
StrMirror gStr;
bool StrSync() {
    const unsigned char* offs = nullptr; const char* data = nullptr; const char* end = nullptr;
    if (!P.gclStrings || !P.gclStrings(&offs, &data, &end) || !data || end <= data) return false;
    if (data != gStr.data || end != gStr.end) {
        size_t n = (size_t)(end - data);
        uint32_t a = Alloc((uint32_t)n + 1);
        if (!a) return false;
        memcpy(gMem + a, data, n);
        gMem[a + n] = 0;
        gStr.data = data; gStr.end = end; gStr.ps2 = a;
        Log("script strings: %u bytes mirrored at %08x", (unsigned)n, a);
    }
    gStr.offs = offs;
    return true;
}
// a host string: its PS2 twin when it lies in the script's string data, else a copy
uint32_t HostStr(const char* s) {
    if (s && StrSync() && s >= gStr.data && s < gStr.end) return gStr.ps2 + (uint32_t)(s - gStr.data);
    return PutStr(s);
}
const char* Str(uint32_t a) { return a && a < 0x02000000 ? (const char*)(gMem + a) : "(null)"; }
uint32_t ObjToken(void* node) {
    if (!node) return 0;
    auto it = gObjTok.find(node);
    if (it != gObjTok.end()) return it->second;
    uint32_t t = kObjLo + (uint32_t)gObjs.size() * kSlot;
    if (t >= kObjHi) { Log("object window full"); return 0; }
    gObjs.push_back(node);
    gObjTok[node] = t;
    return t;
}
void* ObjPtr(uint32_t tok) {
    if (tok < kObjLo || tok >= kObjHi) return nullptr;
    size_t k = (tok - kObjLo) / kSlot;
    return k < gObjs.size() ? gObjs[k] : nullptr;
}
void* Handle(uint32_t tok) {
    if (tok < kHndLo || tok >= kHndHi) return nullptr;
    size_t k = (tok - kHndLo) / 0x100;
    return k < gHandles.size() ? gHandles[k] : nullptr;
}
// PS2 node offsets -> PC node offsets (measured: code +0xc, flags +0x10,
// the two int16 fields +0x3c/+0x3e -> +0x44/+0x46)
int MapOff(int off) {
    if (off >= 0xc && off < 0x14) return off;
    if (off >= 0x3c && off < 0x40) return off + 8;
    return -1;
}
// the engine's stage flags, mirrored at their PS2 address (0x2C5238)
uint32_t kStageFlags = 0x2C5238;                // the running version's: VersionAddresses()
uint64_t StageFlagsRead(uint32_t a, int size) {
    uint64_t v = 0;
    memcpy(&v, (const uint8_t*)P.stageFlags + (a - kStageFlags), size > 4 - (int)(a - kStageFlags) ? 4 - (a - kStageFlags) : size);
    return v;
}
std::string RecentCalls(unsigned n);
void StageFlagsWrite(uint32_t a, uint64_t v, int size) {
    uint32_t before = *P.stageFlags;
    memcpy((uint8_t*)P.stageFlags + (a - kStageFlags), &v, size > 4 - (int)(a - kStageFlags) ? 4 - (a - kStageFlags) : size);
    if (*P.stageFlags != before) Log("stage flags %#x -> %#x (from %08x; calls before:%s)", before, *P.stageFlags, gLastCall, RecentCalls(12).c_str());
}
extern thread_local Ctx* gActive;                     // the running PS2 context (RunPs2Args)
std::string RecentCalls(unsigned n) {                  // the last calls, deduplicated: "callee<caller ..."
    std::set<std::pair<uint32_t, uint32_t>> seen; std::string s;
    for (unsigned k = 0; k < n && k < 64; ++k) {
        auto& e = gRing[(gRingPos - n + k) & 63];
        if (seen.insert({e[0], e[1]}).second) { char b[32]; snprintf(b, sizeof(b), " %08x<%08x", e[0], e[1] - 8); s += b; }
    }
    return s;
}
uint64_t WinRead(uint32_t a, int size) {
    if ((a & ~3u) == kStageFlags && P.stageFlags) return StageFlagsRead(a, size);
    if (a == kGclCursor) return TokOf(Cur());
    size_t k = (a - kObjLo) / kSlot;
    int off = (a - kObjLo) % kSlot;
    if (a < kObjLo || a >= kObjHi || k >= gObjs.size()) {
        static unsigned long long n = 0;               // the first ones with the calls that led there, then a count
        if (++n <= 3) Log("window read outside the objects at %08x (sp %08x); recent calls:%s", a,
                          gActive ? (uint32_t)gActive->r[29] : 0, RecentCalls(24).c_str());
        else if (n <= 20 || !(n & (n - 1))) Log("window read outside the objects at %08x (%llu so far)", a, n);
        return 0;
    }
    int pc = MapOff(off);
    if (pc < 0) {
        if (Once(((uint64_t)2 << 40) | off)) Log("object field +%#x read, not mapped yet (0 returned)", off);
        return 0;
    }
    uint64_t v = 0;
    memcpy(&v, (unsigned char*)gObjs[k] + pc, size);
    return v;
}
void WinWrite(uint32_t a, uint64_t v, int size) {
    if ((a & ~3u) == kStageFlags && P.stageFlags) { StageFlagsWrite(a, v, size); return; }
    if (a == kGclCursor) { SetCur(PtrOf((uint32_t)v)); return; }
    size_t k = (a - kObjLo) / kSlot;
    int off = (a - kObjLo) % kSlot;
    if (a < kObjLo || a >= kObjHi || k >= gObjs.size()) {
        static unsigned long long n = 0;
        if (++n <= 20 || !(n & (n - 1))) Log("window write outside the objects at %08x (%llu so far)", a, n);
        return;
    }
    int pc = MapOff(off);
    if (pc < 0) {
        if (Once(((uint64_t)4 << 40) | off)) Log("object field +%#x written, not mapped yet (dropped)", off);
        return;
    }
    memcpy((unsigned char*)gObjs[k] + pc, &v, size);
}

// actors: each PS2 actor gets a PC actor whose act/die run the recompiled code
// one budgeted run: a PS2 loop that waits on something we do not serve yet
// must never freeze the game thread
// runs nest (a bridge can call back into PS2 code): each run has its own context
// and starts its PS2 stack below the one it interrupts
thread_local Ctx* gActive = nullptr;
uint32_t gRunFn = 0;                                  // the function of the run in progress (logs)
bool RunPs2Args(uint32_t fn, std::initializer_list<uint32_t> args, uint64_t* ret = nullptr, long long budget = 2000000);
bool RunPs2(uint32_t fn, uint32_t a0, uint32_t a1 = 0, uint64_t* ret = nullptr) { return RunPs2Args(fn, {a0, a1}, ret); }
bool RunPs2Args(uint32_t fn, std::initializer_list<uint32_t> args, uint64_t* ret, long long budget) {
    Ctx c{};
    c.r[29] = gActive ? (((uint32_t)gActive->r[29] - 0x1000) & ~0xfu) : kStackTop;
    Ctx* outer = gActive;
    gActive = &c;
    c.r[28] = gMainGp;
    int k = 4;
    for (uint32_t v : args) if (k < 12) c.r[k++] = (uint64_t)(int64_t)(int32_t)v;
    const uint32_t a0 = args.size() ? *args.begin() : 0;
    jmp_buf jb;
    Budget b{budget, &jb, nullptr, 0};
    Budget* prev = ps2::BudgetGet();
    ps2::BudgetSet(&b);
    const uint32_t runOuter = gRunFn;
    gRunFn = fn;
    const unsigned roundingWas = _MM_GET_ROUNDING_MODE();   // (v150-host beta 18, review) the rounding this run began with
    if (setjmp(jb)) {
        // (v150-host beta 18) stopped inside a grid query (its PS2 rounding): the rounding the run began with, not the nearest (an
        // interrupt's handler run inside a grid query and stopped there gives that query its PS2 rounding back)
        _MM_SET_ROUNDING_MODE(roundingWas);
        ps2::BudgetSet(prev);
        gActive = outer;
        gRunFn = runOuter;
        Log("run of %08x (a0 %08x) exceeded its call budget: stopped (last call %08x from %08x)", fn, a0, gLastCall, gLastCaller - 8);
        {
            std::set<std::pair<uint32_t, uint32_t>> seen; std::string s;
            for (unsigned k = 0; k < 64; ++k) {
                auto& e = gRing[(gRingPos - 64 + k) & 63];
                if (seen.insert({e[0], e[1]}).second) { char b[32]; snprintf(b, sizeof(b), " %08x<%08x", e[0], e[1] - 8); s += b; }
            }
            Log("  recent calls:%s", s.c_str());
        }
        Log("  registers: a0 %08x a1 %08x a2 %08x a3 %08x s0 %08x s1 %08x s2 %08x s3 %08x s4 %08x s5 %08x s6 %08x s7 %08x sp %08x ra %08x f0 %g f12 %g f20 %g f21 %g",
            (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], (uint32_t)c.r[7], (uint32_t)c.r[16], (uint32_t)c.r[17], (uint32_t)c.r[18],
            (uint32_t)c.r[19], (uint32_t)c.r[20], (uint32_t)c.r[21], (uint32_t)c.r[22], (uint32_t)c.r[23], (uint32_t)c.r[29], (uint32_t)c.r[31],
            (double)c.f[0], (double)c.f[12], (double)c.f[20], (double)c.f[21]);
        {
            static int dumps = 0;
            if (DevEnv("SIM_STUCK_DUMP") && dumps < 3) {           // RAM and the scratchpad, for a look offline
                char name[64]; snprintf(name, sizeof name, "stuck_%d_%08x.bin", dumps++, fn);
                if (FILE* f = fopen(name, "wb")) { fwrite(gMem, 1, 0x02000000, f); fclose(f); Log("  memory written to %s", name); }
            }
        }
        if (U(gLastCall) >= 0x14c000 && U(gLastCall) < 0x162000) {
            // DG's node list (0x150A50: 64-byte nodes, +0xE the next index), walked by 0x15F888
            const uint32_t base = R32(V(0x23BC3C)), cur = R32(V(0x23BC34));
            std::string s;
            int idx = (int)((int32_t)(cur - base) >> 6);
            for (int n = 0; n < 48 && idx >= 0 && idx < 1024; ++n) {
                char b[16]; snprintf(b, sizeof b, " %d", idx); s += b;
                idx = (int8_t)R8(base + 64 * (uint32_t)idx + 0xE);
            }
            Log("  DG node list: base %08x current %08x, 0x289AA0 count %u, chain from the current node:%s", base, cur, R32(0x289AA0), s.c_str());
        }
        return false;
    }
    Call(c, fn);
    ps2::BudgetSet(prev);
    gActive = outer;
    gRunFn = runOuter;
    if (ret) *ret = c.r[2];
    return true;
}
// Pool 0 (0xF00000, 12 MB): every GV allocation of the base, actor works included
// (0x120E98), and the stage data the loader reads. 0x120F40 creates it (GV init
// 0x121088) and creates it again at each stage load (0x1034C0, from 0x104898), after
// the stage change killed the actors of the stage: a block of an older pool is never
// freed into the new one.
uint32_t gPoolGen = 0;
// pool 0's state: free bytes (+0x14 of 0x2C5DE0), and its free list from +8 ({next, prev,
// flags byte, pool byte, refs byte, _, size} before each block)
#ifndef MGO1_PLAYER_BUILD
// pool 0 walked block by block (header +4 physical previous, +8 flags (bit 0 used), +0xC
// size with the header): the first inconsistency, empty when the pool holds together
std::string PoolWalk() {
    const uint32_t pool = V(0x2C5DE0), start = R32(pool), end = R32(pool + 4);
    if (start < 0xF00000 || end > 0x1B00000 || start >= end) return "";
    uint32_t prev = 0, n = 0, freeBlocks = 0, freeBytes = 0;
    char e[160];
    for (uint32_t b = start; b < end; ++n) {
        const uint32_t size = R32(b + 0xC);
        if (size < 0x10 || (size & 0xF) || b + size > end) { snprintf(e, sizeof e, "block %u at %08x: size %#x (previous %08x)", n, b, size, prev); return e; }
        if (prev && R32(b + 4) != prev) { snprintf(e, sizeof e, "block %u at %08x: previous link %08x, the block before is %08x", n, b, R32(b + 4), prev); return e; }
        if (!(R8(b + 8) & 1)) { ++freeBlocks; freeBytes += size; }
        prev = b; b += size;
    }
    uint32_t listed = 0, listedBytes = 0;
    for (uint32_t b = R32(pool + 8); b && listed < 100000; b = R32(b), ++listed) {
        if (b < start || b >= end || (R8(b + 8) & 1)) { snprintf(e, sizeof e, "free list entry %u: %08x (%s)", listed, b, b < start || b >= end ? "outside the pool" : "a used block"); return e; }
        listedBytes += R32(b + 0xC);
    }
    if (listed != freeBlocks || listedBytes != freeBytes || listedBytes != R32(pool + 0x14)) {
        snprintf(e, sizeof e, "free list %u blocks %#x bytes, free in the pool %u blocks %#x bytes, counter %#x", listed, listedBytes, freeBlocks, freeBytes, R32(pool + 0x14));
        return e;
    }
    return "";
}
#endif
std::string PoolState() {
    const uint32_t pool = V(0x2C5DE0);
    uint32_t n = 0, largest = 0, total = 0;
    for (uint32_t b = R32(pool + 8); b && n < 100000; b = R32(b), ++n) {
        if (b < 0xF00000 || b >= 0x1B00000) { char e[64]; snprintf(e, sizeof e, "bad link %08x after %u blocks", b, n); return e; }
        const uint32_t sz = R32(b + 0xC);
        total += sz; if (sz > largest) largest = sz;
    }
    char t[128]; snprintf(t, sizeof t, "free bytes %#x, %u free blocks, %#x in them, largest %#x", R32(pool + 0x14), n, total, largest);
    return t;
}
// SIM_POOL_ROOM=1 (tests): the room a stage leaves (a rules page with files added, docs/EXTRA_MAPS.md): each
// pass of the actor lists, pool 0's free bytes and largest free block; their lows since the resident top last
// moved, logged when it moves and every 600 frames when they went lower (the least room a match left)
void PoolRoomTick() {
    static const bool on = getenv("SIM_POOL_ROOM") != nullptr;
    if (!on) return;
    static uint32_t top = 0, lowFree = ~0u, lowLargest = ~0u, lowN = 0;
    static bool lower = false;
    static long long lastLog = 0;
    const uint32_t t = R32(V(0x2C5E10)), pool = V(0x2C5DE0);
    uint32_t n = 0, largest = 0;
    for (uint32_t b = R32(pool + 8); b >= 0xF00000 && b < 0x1B00000 && n < 100000; b = R32(b), ++n) largest = std::max(largest, R32(b + 0xC));
    const uint32_t freeBytes = R32(pool + 0x14);
    auto say = [&](const char* why) {
        Log("pool room (%s): resident top %08x, pool 0 %08x..%08x, free %#x (%.2f MB) at least, largest free block %#x (%.2f MB) at least, %u free blocks",
            why, top, R32(pool), R32(pool + 4), lowFree, lowFree / 1048576.0, lowLargest, lowLargest / 1048576.0, lowN);
        lastLog = gFrameNo; lower = false;
    };
    if (t != top) {
        if (top && lower) say("before the resident top moved");
        top = t; lowFree = freeBytes; lowLargest = largest; lowN = n;
        say("the resident top moved");
        return;
    }
    if (freeBytes < lowFree || largest < lowLargest) { lowFree = std::min(lowFree, freeBytes); lowLargest = std::min(lowLargest, largest); lowN = n; lower = true; }
    if (lower && gFrameNo - lastLog >= 600) say("lower");
}
void ResetPool0(const char* why) {
    int stale = 0;
    for (auto& a : gActors)
        if (a.live && a.work >= 0xF00000 && a.work < 0x1B00000 && a.poolGen == gPoolGen) {
            Log("pool 0 reset (%s): actor %s %08x is still live, its work goes with the pool", why, a.name.c_str(), a.work);
            ++stale;
        }
    RunPs2(V(0x120F40), 0);
    ++gPoolGen;
    Log("pool 0 created (%s, generation %u, %d live actor works in the old one)", why, gPoolGen, stale);
}
// the game network's configuration blocks (table 0x28D528, 18 entries {count, fields, data}:
// 0x177288 registers them at boot), logged at the boot and after each page load
void ConfigBlocks(const char* why) {
    std::string t;
    for (int k = 0; k < 18; ++k) {
        uint32_t e = R32(V(0x28D528) + 4 * k);
        char b[64]; snprintf(b, sizeof b, " %d:%08x{%u,%08x,%08x}", k, e, e ? R32(e) : 0, e ? R32(e + 4) : 0, e ? R32(e + 8) : 0);
        t += b;
    }
    Log("configuration blocks (%s):%s", why, t.c_str());
}
// libeenetctl events (the platform stands in for eenetctl.irx and the EE stack):
// handlers registered by 0x18E068 sit in a list at 0x29E558, nodes {next, handler,
// gp}; the library's dispatcher (0x18CB08) calls handler(ifname, if index, event),
// the index -1 for events 10..12. Queued events go out at the start of a frame.
struct NetEvent { uint32_t ifname; int32_t index; int32_t event; };
static std::vector<NetEvent> gNetEvents;
static void QueueNetEvent(uint32_t ifname, int32_t index, int32_t event) { gNetEvents.push_back({ifname, index, event}); }
static void DeliverNetEvents() {
    if (gNetEvents.empty()) return;
    std::vector<NetEvent> q; q.swap(gNetEvents);
    for (auto& e : q) {
        const char* name = (e.ifname && e.ifname < 0x02000000) ? (const char*)(gMem + e.ifname) : "";
        int n = 0;
        for (uint32_t node = R32(V(0x29e558)); node && n < 16; node = R32(node), ++n) {
            Ctx c{};
            c.r[29] = kStackTop; c.r[28] = R32(node + 8);
            c.r[4] = e.ifname; c.r[5] = (uint64_t)(int64_t)e.index; c.r[6] = (uint64_t)(int64_t)e.event;
            Ctx* outer = gActive; gActive = &c;
            Call(c, R32(node + 4));
            gActive = outer;
        }
        Log("eenetctl: event %d (%s, index %d) to %d handler(s)", e.event, name, e.index, n);
    }
}
// libeenetctl's interface list, as its thread (0x18D670) keeps it from the stack's
// interfaces: head 0x240478, 0x5C-byte nodes from 0x18EBD0 {next; name[16] +4;
// configuration record +0x14; one address-family entry of 0x28 bytes from +0x30
// (AF_INET only): flags +0x30 / +0x34, state +0x38, ids +0x3C / +0x40, last error
// +0x50 / +0x54}. States: 0 detached, 1 starting, 2 retrying, 3 started, 4 stopping,
// 5 stopped. 0x18E168 reads a state: the link check 0x16E0C8 wants smap0 started.
static uint32_t IfNode(uint32_t name) {
    if (!name || name >= 0x02000000) return 0;
    for (uint32_t n = R32(V(0x240478)), k = 0; n && n < 0x02000000 && k < 16; n = R32(n), ++k)
        if (!strncmp((const char*)gMem + n + 4, (const char*)gMem + name, 16)) return n;
    return 0;
}
static void IfAttach(uint32_t name) {
    if (!name || name >= 0x02000000 || IfNode(name)) return;
    uint64_t n = 0;
    if (!RunPs2(V(0x18ebd0), 0, 0, &n) || !(uint32_t)n) { Log("eenetctl: no node for %s", (const char*)gMem + name); return; }
    uint32_t a = (uint32_t)n;
    strncpy((char*)gMem + a + 4, (const char*)gMem + name, 16);
    W32(a + 0x30, 0x40000000); W32(a + 0x34, 0x80000000);   // as 0x18D670 sets a new node
    W32(a + 0x3c, 0xffffffff); W32(a + 0x40, 0xffffffff);
    W32(a + 0x38, 5);                                         // attached: stopped (0x18CF08, event Attach)
    Log("eenetctl: %s attached (node %08x, stopped)", (const char*)gMem + name, a);
}
static void IfState(uint32_t name, uint32_t state) {
    if (uint32_t n = IfNode(name)) { W32(n + 0x38, state); Log("eenetctl: %s state %u", (const char*)gMem + name, state); }
}
// the PS2 pad system: raw records at 0x284a90 (+0x40 per port) as the pad
// driver leaves them, then the recompiled GV pad frame (0x123a78) derives
// press / release / repeat into GV_PAD[2] at 0x2c5e68 and the logical pads
uint32_t gPadPrev[2] = {0, 0};
// one port's record at 0x284A90 + 0x40 k, as the base's reader (0x123448 -> 0x123198) leaves it:
// the channels read (buttons 0-15, sticks 16-19, pressures 20-31: a DualShock 2), the held
// buttons, the sticks RX RY LX LY (0x80 centred), the pressures of the 12 pressure buttons
// (held bits 4..15); without an analog source a held pressure button reads 0xFF
void WritePadRecord(int k) {
    const uint32_t held = P.padHeld ? P.padHeld(k) : 0;
    uint8_t sticks[4] = {0x80, 0x80, 0x80, 0x80}, press[12];
    for (int i = 0; i < 12; ++i) press[i] = (held >> (i + 4)) & 1 ? 0xff : 0;
    if (P.padAnalog) P.padAnalog(k, sticks, press);
    const uint32_t r = V(0x284a90) + k * 0x40;
    W32(r + 0, 0xffffffffu);
    W32(r + 4, held);
    for (int i = 0; i < 4; ++i) W8(r + 8 + i, sticks[i]);
    for (int i = 0; i < 12; ++i) W8(r + 0xc + i, press[i]);
    W32(r + 0x18, 0);
    if (held != gPadPrev[k]) Log("pad %d: %04x", k, held);
    gPadPrev[k] = held;
}
void UpdatePads() {
    // GV_PAD (0x1237A8) makes pressed / released and the direction (0x1235A0: the left
    // stick, else the d-pad) from these records
    for (int k = 0; k < 2; ++k) WritePadRecord(k);
    // the base's pad frame (0x123A78, as 0x123F58 runs it after the reader): the physical pads
    // GV_PAD[2] (0x2C5E68) and the logical ones the game reads (0x2C5E20, with the pad locks
    // and replays of their flags +0x1E); the menus read the first, the players the second
    RunPs2(V(0x123a78), 0);
}
// IOP side memory the EE writes by SIF DMA (sceSifSetDma): only the buffers
// the served IOP modules hand out (netcnfif's data area)
const uint32_t kIopNetcnfData = 0x000B30F0;       // netcnfif data area (module offset 0x30F0)
std::map<uint32_t, std::vector<uint8_t>> gIop;
void IopWrite(uint32_t iop, const uint8_t* src, uint32_t n) {
    auto& v = gIop[iop];
    if (v.size() < n) v.resize(n);
    memcpy(v.data(), src, n);
}

// netcnfif (IOP network configuration interface, server 0x80001101), as measured
// in the module itself (MODNCD.IPK: netcnfif.irx, dispatcher 0x3F0): arguments
// { int data; int f_no_decode; int type; int addr; char fname[256]; char
// usr_name[256] }, answer in the first word. GET_COUNT (0) and GET_LIST (1):
// one configuration of each type on mc0:, none on mc1:; list entries are
// sceNetCnfList { int type; int stat; char sys_name[256]; char usr_name[256]; }
// copied into 0x240-byte slots. LOAD_ENTRY (2): sceNetcnfifData (0x1340 bytes)
// as ReadEnv (0x1028) fills it: an Ethernet adaptor on DHCP. MGO1 itself
// accepts ifc_type 3, or 2 with PPPoE (0x371DD8); a dial-up modem is refused.
const char* kCnfSys[3] = {"net000.cnf", "ifc000.cnf", "dev000.cnf"};
const char* kCnfUsr[3] = {"SaveMGO PC", "SaveMGO PC Internet", "Network Adaptor (Ethernet)"};
void CnfIfc(uint32_t d) {
    W32(d + 0x1300, 3);                         // ifc_type: 3 = broadband (NewNetConfLoad 0x371DD8 takes 3, or 2 with PPPoE)
    W8(d + 0x1320, 1);                          // dhcp
    W8(d + 0x1321, 1); W8(d + 0x1322, 1);       // dns1 / dns2 from DHCP
    W32(d + 0x1304, 1500);                      // mtu
}
void CnfDev(uint32_t d) {
    W32(d + 0x130c, 1);                         // dev_type: Ethernet
    strcpy((char*)gMem + d + 0xe00, "SCE");                            // vendor
    strcpy((char*)gMem + d + 0xf00, "Ethernet (Network Adaptor)");     // product
    W32(d + 0x1310, 1);                         // phy_config: auto
}
void NetcnfRpc(uint32_t fno, const std::vector<uint8_t>& args, uint32_t res) {
    auto A32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= args.size()) memcpy(&v, &args[o], 4); return v; };
    auto AStr = [&](size_t o, char* out, size_t n) { out[0] = 0; if (args.size() > o) { size_t k = args.size() - o < n - 1 ? args.size() - o : n - 1; memcpy(out, &args[o], k); out[k] = 0; } };
    char dev[64], name[64];
    AStr(0x10, dev, sizeof(dev));
    AStr(0x110, name, sizeof(name));
    int type = (int)A32(8);
    bool mc0 = strncmp(dev, "mc0:", 4) == 0;
    bool known = type >= 0 && type <= 2;
    int result = 0;
    switch (fno) {
    case 0:                                                  // GET_COUNT (fname, type)
        result = mc0 && known ? 1 : 0;
        break;
    case 1: {                                                // GET_LIST (fname, type, addr, max)
        uint32_t addr = A32(0xc);
        int max = (int)A32(0);
        int n = mc0 && known && max > 0 ? 1 : 0;
        for (int k = 0; k < n && addr && addr + 0x240 < 0x02000000; ++k) {
            uint32_t e = addr + k * 0x240;
            memset(gMem + e, 0, 0x240);
            W32(e + 0, (uint32_t)type);
            W32(e + 4, 0);
            strcpy((char*)gMem + e + 8, kCnfSys[type]);
            strcpy((char*)gMem + e + 0x108, kCnfUsr[type]);
        }
        result = n;
        Log("netcnf: list of %d (max %d, type %d) at %08x", n, max, type, addr);
        break; }
    case 2: {                                                // LOAD_ENTRY (fname, type, usr_name, addr)
        uint32_t d = A32(0xc);
        if (!mc0 || !known) { result = -1; break; }
        if (d && d + 0x1340 < 0x02000000) {
            memset(gMem + d, 0, 0x1340);
            if (type == 0) {
                // a combination names its ifc and dev entries by their file (sys_name):
                // the EE helper 0x606360 matches attach_ifc / attach_dev against sys_name
                strcpy((char*)gMem + d, kCnfSys[1]);             // attach_ifc
                strcpy((char*)gMem + d + 0x100, kCnfSys[2]);     // attach_dev
                CnfIfc(d); CnfDev(d);
            } else if (type == 1) CnfIfc(d);
            else CnfDev(d);
            if (DevEnv("MGO1_NETCNF_PROBE")) {
                for (int k = 2; k < 18; ++k) if (!gMem[d + k * 256]) snprintf((char*)gMem + d + k * 256, 16, "F%02d", k);
            }
        }
        Log("netcnf: load entry \"%s\" (type %d) into %08x", name, type, d);
        result = 0;
        break; }
    case 100:                                                // GET_ADDR: the IOP data area, answered in +0xC
        if (res && res + 0x10 < 0x02000000) W32(res + 0xc, kIopNetcnfData);
        result = 0;
        break;
    case 103: {                                              // SET_ENV: the data the EE wrote to the data area
        auto it = gIop.find(kIopNetcnfData);
        if (it != gIop.end() && it->second.size() >= 0x1340) {
            const uint8_t* dd = it->second.data();
            uint32_t ifcType = 0, devType = 0; memcpy(&ifcType, dd + 0x1300, 4); memcpy(&devType, dd + 0x130c, 4);
            Log("netcnf: set env: ifc \"%.40s\" dev \"%.40s\" ifc_type %u dev_type %u dhcp %u vendor \"%.30s\" product \"%.30s\"",
                (const char*)dd, (const char*)dd + 0x100, ifcType, devType, dd[0x1320], (const char*)dd + 0xe00, (const char*)dd + 0xf00);
        } else Log("netcnf: set env without data in the IOP area");
        result = 0;
        break; }
    default:                                                 // add / edit / delete / work area...
        result = 0;
        break;
    }
    if (res && res < 0x02000000) W32(res, (uint32_t)result);
    Log("netcnf: function %u (%s, type %d) -> %d", fno, dev, type, result);
}

// IOP RPC servers the platform answers, by server id (sceSifBindRpc records the
// client). Measured in the IOP modules of MODNCD.IPK: netcnfif 0x80001101,
// eenetctl 0x80000F81, ent_devm 0x80000F80 / 0x80000014, dbcman 0x80001300 /
// 0x8000131C / 0x8000131E.
std::map<uint32_t, uint32_t> gRpcServerOf;            // client -> server id
uint32_t RpcServerOf(uint32_t cd) {
    auto it = gRpcServerOf.find(cd);
    if (it != gRpcServerOf.end()) return it->second;
    return cd == V(0x6FB240) ? 0x80001101u : 0;      // netcnfif was bound before the world started
}
void EenetctlRpc(uint32_t fno, const std::vector<uint8_t>& args, uint32_t res) {
    uint32_t a0 = 0; if (args.size() >= 4) memcpy(&a0, args.data(), 4);
    if (res && res < 0x02000000) W32(res, 0);
    Log("eenetctl: function %u (first word %08x) -> 0", fno, a0);
}

// IOP file I/O server (0x80000001, libkernel's sceOpen / sceRead ...): the
// requests are logged with their path; nothing is opened on the host yet
void FileioRpc(uint32_t fno, const std::vector<uint8_t>& args, uint32_t res) {
    auto A32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= args.size()) memcpy(&v, &args[o], 4); return v; };
    auto Str = [&](size_t o) { std::string s; for (size_t k = o; k < args.size() && k < o + 96 && args[k]; ++k) s += (args[k] >= 0x20 && args[k] < 0x7f) ? (char)args[k] : '.'; return s; };
    int result = fno == 0 ? -2 : 0;                    // open: no such file (ENOENT)
    if (res && res + 4 <= 0x02000000) W32(res, (uint32_t)result);
    if (fno == 23 || fno == 28)                        // devctl: name at +12, command at +2060
        Log("fileio: devctl (function %u) \"%s\" command %#x, %u bytes in -> %d", fno, Str(12).c_str(), A32(2060), A32(2064), result);
    else
        Log("fileio: function %#x (word %08x, \"%s\", %zu bytes) -> %d", fno, A32(0), Str(4).c_str(), args.size(), result);
}

#include "usbkb.h"                                      // the USB keyboard's tables (KeyboardStrokes)
// USB keyboard (usbkb.irx, server 0x80000211), measured in the module and in
// the EE library (0x1E6838.., completion 0x1E70C0): 1 GET_INFO answers
// {max_connect, now_connect, status[max]}; 2 READ(kb) answers {led, count,
// report[8]} with report = HID boot report {modifiers, 0, key[6]}; count 0
// means no new report. 4 SET_LED(kb, led) answers {kb, led} (the EE handler
// compares the answered LED with the one it asked for: -1 otherwise), 5 the
// same; 3 (location) and 6 answer zeros.
struct KbReport { uint8_t mods = 0; uint8_t keys[6] = {0, 0, 0, 0, 0, 0}; };
KbReport gKbSent;
uint8_t gKbLed = 0;                                 // the keyboard's lights as the game last set them (1 Num Lock, 2 Caps Lock)
uint32_t gUsbkbWork = 0, gUsbkbAct = 0;             // the game's keyboard actor (GM_UsbkbdStartDaemon): work, act
bool (*gHostKeyboard)(KbReport&) = nullptr;         // the host's keyboard, set by the plugin
extern long long gFrameNo;
// simulation: SIM_KB="frame:text;frame:text" types each text from that frame,
// one key every 4 frames (down 2 frames, up 2); '\n' is Enter. The game turns Caps Lock on
// (a plain letter is a capital, Shift gives a small one): letters are typed with Shift inverted
// then, so the text keeps its exact case
uint8_t HidOf(char ch, bool& shift) {
    shift = false;
    // SIM_KB_LAYOUT=fr (tests): the keys of a French keyboard (AZERTY), as the French program reads them: a q z w m
    // elsewhere, the digits shifted
    static const bool azerty = getenv("SIM_KB_LAYOUT") && !strcmp(getenv("SIM_KB_LAYOUT"), "fr");
    if (azerty) {
        const char lo = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
        const bool up = ch >= 'A' && ch <= 'Z';
        if (lo == 'a') { shift = up; return 0x14; }
        if (lo == 'q') { shift = up; return 0x04; }
        if (lo == 'z') { shift = up; return 0x1A; }
        if (lo == 'w') { shift = up; return 0x1D; }
        if (lo == 'm') { shift = up; return 0x33; }
        if (ch >= '1' && ch <= '9') { shift = true; return (uint8_t)(0x1e + ch - '1'); }
        if (ch == '0') { shift = true; return 0x27; }
    }
    // SIM_KB_LAYOUT=de (tests): a German keyboard (QWERTZ) as the German program reads it in its log-in's entries: y and z
    // swapped, the number row shifted by the entry's lock (a bare '0' came out '=', run T8de3), so the digits with shift
    static const bool qwertz = getenv("SIM_KB_LAYOUT") && !strcmp(getenv("SIM_KB_LAYOUT"), "de");
    if (qwertz) {
        const char lo = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
        const bool up = ch >= 'A' && ch <= 'Z';
        if (lo == 'y') { shift = up; return 0x1D; }
        if (lo == 'z') { shift = up; return 0x1C; }
        if (ch >= '1' && ch <= '9') { shift = true; return (uint8_t)(0x1e + ch - '1'); }
        if (ch == '0') { shift = true; return 0x27; }
    }
    if (ch >= 'a' && ch <= 'z') return (uint8_t)(0x04 + ch - 'a');
    if (ch >= 'A' && ch <= 'Z') { shift = true; return (uint8_t)(0x04 + ch - 'A'); }
    if (ch >= '1' && ch <= '9') return (uint8_t)(0x1e + ch - '1');
    if (ch == '0') return 0x27;
    if (ch == '\n') return 0x28;
    if (ch == ' ') return 0x2c;
    if (ch == '-') return 0x2d;
    if (ch == '_') { shift = true; return 0x2d; }
    if (ch == '.') return 0x37;
    if (ch == '\b') return 0x2a;
    return 0;
}
std::vector<std::pair<long long, std::string>> gKbScript;
size_t gKbItem = 0, gKbStep = 0;                     // the key being typed: its text, and its place in it
bool SimKeyboard(KbReport& r) {
    auto& script = gKbScript;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (const char* s = DevEnv("SIM_KB")) {
            std::string all = s;
            size_t k = 0;
            while (k < all.size()) {
                size_t e = all.find(';', k); if (e == std::string::npos) e = all.size();
                std::string item = all.substr(k, e - k);
                size_t c = item.find(':');
                if (c != std::string::npos) {
                    std::string t = item.substr(c + 1), u;
                    for (size_t q = 0; q < t.size(); ++q) u += (t[q] == '\\' && q + 1 < t.size() && t[q + 1] == 'n') ? (++q, '\n') : t[q];
                    script.push_back({atoll(item.c_str()), u});
                }
                k = e + 1;
            }
        }
    }
    // paced by the keyboard's reads, not by frames: each key is down for two reads, then up for
    // two (a slow frame of the game never drops or repeats a key)
    r = KbReport();
    size_t& item = gKbItem; size_t& step = gKbStep;
    while (item < script.size() && step >= script[item].second.size() * 4) { ++item; step = 0; }
    if (item >= script.size() || gFrameNo < script[item].first) return true;
    static const bool game = DevEnv("SIM_KB_GAME") != nullptr;      // tests: the characters as the front end sends them
    if (step % 4 < 2 && game) {
        uint8_t u[2] = {0, 0}, m[2] = {0, 0}; int n = 0;
        const char ch = script[item].second[step / 4];
        if (ch == '\n' || ch == '\b') r.keys[0] = ch == '\n' ? 0x28 : 0x2a;
        else if (KeyboardStrokes((uint8_t)ch, u, m, &n)) { r.keys[0] = u[0]; r.mods = m[0]; }
    } else if (step % 4 < 2) {
        const char ch = script[item].second[step / 4];
        bool sh; r.keys[0] = HidOf(ch, sh);
        if ((gKbLed & 0x02) && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))) sh = !sh;
        if (sh) r.mods = 0x02;
    }
    ++step;
    return true;
}
void UsbkbRpc(uint32_t fno, const std::vector<uint8_t>& args, uint32_t res) {
    uint8_t out[16] = {0};
    uint8_t& led = gKbLed;
    if (fno == 1) { out[0] = 2; out[1] = 1; out[2] = 1; }                 // one keyboard of two, on port 0
    else if (fno == 4 || fno == 5) {                                       // LED: the driver answers the value it set
        out[0] = args.size() > 0 ? args[0] : 0;                            // (~value on failure); the EE compares it
        out[1] = args.size() > 1 ? args[1] : 0;
        if (fno == 4) {
            if (led != out[1]) Log("usbkb: LEDs %02x (Caps Lock %s)", out[1], (out[1] & 0x02) ? "on" : "off");
            led = out[1];
        }
    }
    else if (fno == 2) {
        static long reads = 0;
        static long long logFrom = DevEnv("SIM_KB_LOG") ? atoll(DevEnv("SIM_KB_LOG")) : -1;
        ++reads;
        if (reads == 1 || reads % 600 == 0 || (logFrom >= 0 && gFrameNo >= logFrom && gFrameNo < logFrom + 40))
            Log("usbkb: READ #%ld (frame %lld) args %02x %02x %02x %02x", reads, gFrameNo,
                args.size() > 0 ? args[0] : 0, args.size() > 1 ? args[1] : 0, args.size() > 2 ? args[2] : 0, args.size() > 3 ? args[3] : 0);
        uint8_t kb = args.empty() ? 0 : args[0];
        if (kb != 0) out[1] = 0xff;                                        // not connected
        else {
            KbReport now;
            bool have = P.keyboard ? P.keyboard(now.mods, now.keys) : gHostKeyboard ? gHostKeyboard(now) : SimKeyboard(now);
            out[0] = led;
            if (have && memcmp(&now, &gKbSent, sizeof(now)) != 0) {
                gKbSent = now;
                out[1] = 6; out[2] = now.mods; memcpy(out + 4, now.keys, 6);
                // the keys themselves are not logged: they may be an account's password
                static long long lastKeys = -1000;
                if (gFrameNo - lastKeys > 600) Log("usbkb: keys typed (not logged)");
                lastKeys = gFrameNo;
            }
        }
    }
    if (res && res + 16 <= 0x02000000) memcpy(gMem + res, out, 16);
    static std::set<uint32_t> said;
    if (fno != 2 && said.insert(fno).second)
        Log("usbkb: function %u answered (args %02x %02x %02x %02x)", fno, args.size() > 0 ? args[0] : 0,
            args.size() > 1 ? args[1] : 0, args.size() > 2 ? args[2] : 0, args.size() > 3 ? args[3] : 0);
}

// lgaud.irx (a Logitech USB headset: the maps' NewVoiceChat through liblgaud 1.10, docs/VOICE.md),
// played by the PC's microphone and sound. The EE library's calls (US n001a 0x5458A0..): 0x10 init
// (+8 the module's version, 1.10.006: the library halts on another; +0x20 its largest transfer: the
// voice actor reads up to 0x400 bytes at once, and 0x180 or less keeps the library's own buffer of
// 0x150), 0x17 the EE word the module writes a device's arrival to (1), 1 enumerate (+8 the index;
// +0x20 a description of 0x154 bytes), 2 open (+8 the index, +0x20 the format: mode 3, one channel
// of 16 bits at 8000 Hz to record and the same to play; the handle back at +0xC), 3 close, 4 / 5
// recording on / off, 6 / 7 playback on / off, 8 read (+0xC the handle, +0x12 the mode, +0x20 the
// bytes asked; back: +0x20 the bytes, the samples at +0x30), 9 write (the same, the samples sent at
// +0x30), 0x12 / 0x13 the bytes recorded / still to play (+0x20); the others answered 0. A headset
// is there while the front end gives a microphone (VoiceAttach); its samples come by VoiceIn, the
// game's voice goes by VoiceOut. Result +0: 0, or 0x80000003 (no such device).
// the voice between PCs (vnet below): per player id, when the game last played his voice (LPC-10) and when
// his Opus packets last came (the PS2's time, ms); the front end plays Opus (VoiceNetEnable)
std::atomic<unsigned long long> gVoiceHeard[32], gVoiceOpus[32];
std::atomic<bool> gVoiceOpusOn{false};
constexpr unsigned long long kVoiceHeardMs = 400, kVoiceOpusMs = 400;
struct Headset {
    std::mutex mu;
    std::deque<int16_t> rec, play;          // 8 kHz: the microphone's samples, the game's voice
    bool attached = false, told = false;    // a microphone given; the arrival word set for it
    uint32_t arrival = 0;                   // the EE word of call 0x17
    bool open = false, recording = false, playing = false;
    bool sidetone = false;                  // talking, the game plays his own voice back (and its beep): kept or not
    std::vector<int16_t> lastRead;          // the samples of the last read (talking: the next write is them and the beep)
    bool outOn = false;                     // VoiceOut playing (a new spurt waits for 45 ms of voice)
    int beepPos = -1;                       // the start beep's place at the last write of a talker's voice
    long long reads = 0, readBytes = 0, writes = 0, writeBytes = 0, dropped = 0, swapped = 0;
} gHs;
constexpr size_t kHsRecMax = 8000, kHsPlayMax = 8000;   // 1 s recorded, 1 s to play at most (the oldest dropped)
constexpr uint32_t kLgaudMaxTransfer = 0x1000;          // the RPC bridge copies 0x1000 bytes a call at most
void LgaudArrival() {                                   // (gHs.mu held) the EE's word: a headset arrived
    if (!gHs.arrival || gHs.told || !gHs.attached) return;
    W32(gHs.arrival, 1);
    gHs.told = true;
    Log("lgaud: a headset arrived (the PC's microphone)");
}
uint32_t VoiceActor() {                                 // the maps' NewVoiceChat work (0 when none)
    for (const auto& a : gNativeActors) if (a.second == "NewVoiceChat") return a.first;
    return 0;
}
namespace vnet { void Pump(); void Report(); }
bool BcLink();                                         // BASE CAPTURE (basecap.h): the PCs' link wanted
void VoiceTick() {                                      // a pass of the actor lists
    vnet::Pump();
    const uint32_t w = VoiceActor();
    static uint32_t lastActor = 0;
    if (lastActor && !w) vnet::Report();                // a match's end
    lastActor = w;
    std::lock_guard<std::mutex> g(gHs.mu);
    LgaudArrival();
    static const bool watch = DevEnv("SIM_VOICE_WATCH") != nullptr;   // tests: the voice actor's states
    if (!watch) return;
    static uint32_t last = ~0u, lastTalkers = ~0u, lastChans = ~0u;
    if (w && getenv("SIM_VOICE_WATCH")[0] == '2') {      // the talk conditions (US n033a: 0x2FF830 -> 0x3CEAF8)
        static uint32_t lastCond = ~0u;
        auto flag = [](uint32_t n) { return (R32(0x23C790 + 0x1c + (n >> 5) * 4) >> (n & 31)) & 1; };
        const uint32_t p9a0 = R32(0x2F09A0), hud = R32(0x2F07E8);
        const uint32_t a = p9a0 ? R32(p9a0 + 0xf8) : 0, b = hud ? R16(hud + 0xee) : 0, cc = (R32(0x2C5248) | R32(0x2C524C)) & 0x10;
        const uint32_t cond = (a ? 1 : 0) | ((b & 0x400) ? 2 : 0) | (cc ? 4 : 0) | flag(0x151) << 3 | flag(0x99) << 4 | flag(0x9b) << 5;
        if (cond != lastCond) {
            Log("voice: talk conditions: [0x2F09A0]+0xF8 %u, the HUD's flags %#x (0x400: may talk), [0x2C5248..] & 0x10 %u, flags 0x151 %u 0x99 %u 0x9B %u",
                a, b, cc, flag(0x151), flag(0x99), flag(0x9b));
            lastCond = cond;
        }
    }
    const uint32_t st = w ? R32(w + 0x1f8) : ~0u, talkers = w ? R32(w + 0x334c) : ~0u;
    const uint32_t chans = w ? (uint32_t)R8(w + 0x40) | (uint32_t)R8(w + 0xb0) << 8 | (uint32_t)R8(w + 0x120) << 16 : ~0u;
    if (st == last && talkers == lastTalkers && chans == lastChans) return;
    Log("voice: the voice chat's state %d (channel %d, talkers %#x, flags %#x, recorded %u bytes, bits %u / %u; channels' talkers %d %d %d, "
        "hearers %#x %#x %#x; start beep %d samples, at %d)",
        (int)st, w ? (int)R32(w + 0x3344) : -1, talkers, w ? R32(w + 0x3348) : 0, w ? R32(w + 0x3350) : 0,
        w ? R32(w + 0x3358) : 0, w ? R32(w + 0x3354) : 0, w ? (int)(int8_t)R8(w + 0x40) : -2, w ? (int)(int8_t)R8(w + 0xb0) : -2,
        w ? (int)(int8_t)R8(w + 0x120) : -2, w ? R32(w + 0x44) : 0, w ? R32(w + 0xb4) : 0, w ? R32(w + 0x124) : 0,
        w ? (int16_t)R16(w + 0x3f64) / 2 : 0, w ? (int)(int16_t)R16(w + 0x3f68) : 0);
    last = st; lastTalkers = talkers; lastChans = chans;
}
void LgaudRpc(uint32_t fno, const std::vector<uint8_t>& args, uint32_t res) {
    static std::set<uint32_t> said;
    std::lock_guard<std::mutex> g(gHs.mu);
    auto arg8 = [&](size_t o) -> uint32_t { return o < args.size() ? args[o] : 0; };
    auto arg16 = [&](size_t o) -> uint32_t { return arg8(o) | arg8(o + 1) << 8; };
    auto arg32 = [&](size_t o) -> uint32_t { return arg16(o) | arg16(o + 2) << 16; };
    const bool fit = res && res + 0x30 <= 0x02000000;
    auto put32 = [&](uint32_t o, uint32_t v) { if (fit) W32(res + o, v); };
    uint32_t code = 0;
    switch (fno) {
    case 0x10:                                          // init: the version, the largest transfer
        if (fit) { memset(gMem + res, 0, 0x30); W32(res + 8, 0x010A0600); W32(res + 0x20, kLgaudMaxTransfer); }
        if (said.insert(fno).second) Log("lgaud: init answered (module version 1.10.006, transfers up to %u bytes)", kLgaudMaxTransfer);
        return;
    case 0x17:                                          // the EE word for a device's arrival
        gHs.arrival = arg32(0x20) & 0x01fffffc; gHs.told = false;
        LgaudArrival();
        break;
    case 1:                                             // enumerate: one headset while a microphone is given
        if (gHs.attached && arg32(8) == 0) {
            if (res && res + 0x180 <= 0x02000000) {
                memset(gMem + res + 0x20, 0, 0x154);
                W16(res + 0x20, 0x046d); W16(res + 0x22, 0x0a01);   // Logitech's vendor id, a USB headset
                static const char name[] = "USB Headset (the PC's microphone)";
                memcpy(gMem + res + 0x28, name, sizeof name);
            }
        } else code = 0x80000003;
        break;
    case 2:                                             // open
        if (gHs.attached && arg32(8) == 0) {
            gHs.open = true; put32(0xc, 1);
            Log("lgaud: the headset opened: mode %u, recording %u channel(s) of %u bits at %u Hz, playback %u of %u bits at %u Hz",
                arg8(0x20), arg8(0x22), arg8(0x23), arg16(0x24), arg8(0x28), arg8(0x29), arg16(0x2a));
        } else code = 0x80000003;
        break;
    case 3: gHs.open = gHs.recording = gHs.playing = false; gHs.rec.clear(); gHs.play.clear(); break;
    case 4: gHs.recording = true; gHs.rec.clear(); break;
    case 5: gHs.recording = false; gHs.rec.clear(); break;
    case 6: gHs.playing = true; break;
    case 7: gHs.playing = false; gHs.play.clear(); break;
    case 8: {                                           // read: the microphone's samples since the last read
        size_t n = std::min<size_t>((arg32(0x20) & ~1u) / 2, gHs.rec.size());
        if (!res || res + 0x30 + n * 2 > 0x02000000) n = 0;
        gHs.lastRead.assign(gHs.rec.begin(), gHs.rec.begin() + (long)n);
        for (size_t i = 0; i < n; ++i) { W16(res + 0x30 + 2 * (uint32_t)i, (uint16_t)gHs.rec.front()); gHs.rec.pop_front(); }
        put32(0x20, (uint32_t)(n * 2));
        ++gHs.reads; gHs.readBytes += (long long)n * 2;
        break;
    }
    case 9: {                                           // write: the game's voice to play
        uint32_t bytes = arg32(0x20) & ~1u;
        if (0x30 + (size_t)bytes > args.size()) bytes = args.size() > 0x30 ? (uint32_t)((args.size() - 0x30) & ~1u) : 0;
        // talking, the actor plays back what it just read with its start beep added (US n001a 0x2FFF18,
        // 0x2FFBA8): without the sidetone the read samples come off and the beep stays
        const bool own = gHs.recording && !gHs.sidetone && gHs.lastRead.size() == bytes / 2;
        // listening (states 2 and 3), a frame of a talker's voice: 0x300788 decodes 180 samples of his LPC-10
        // and 0x2FFBA8 adds the start beep from its place on ([+0x3F5C], +0x3F64 bytes: 178 samples; its
        // place +0x3F68, set to 0 when a talker gets the channel, read nowhere else). The talker is the one of
        // this player's channel (+0x3344; + 0x40 + 0x70 k). His Opus packets coming (vnet), only the beep's
        // part of the frame stays. The beep's place is left at 0x7FFF once the beep is through (no beep
        // either way for the game): at the next frame a lower place says how far it went in that frame. The
        // end beep (0x2FFC20: [+0x3F60], +0x3F66 bytes) stays as it is.
        int swapFrom = -1, swapCount = 0; uint32_t beepAt = 0;
        if (const uint32_t w = gHs.recording || !gHs.playing ? 0 : VoiceActor()) {
            const uint32_t st = R32(w + 0x1f8), endAt = R32(w + 0x3f60) & 0x01ffffff, endLen = R16(w + 0x3f66);
            const bool endBeep = endLen == bytes && endAt + bytes <= 0x02000000 && !memcmp(gMem + endAt, args.data() + 0x30, bytes);
            if ((st == 2 || st == 3) && bytes && !endBeep) {
                const uint32_t ch = R32(w + 0x3344);
                const int talker = ch <= 2 ? R8(w + 0x40 + 0x70 * ch) : 0xff;
                const int beepLen = (int16_t)R16(w + 0x3f64) / 2, pos = (int16_t)R16(w + 0x3f68), n = (int)bytes / 2;
                const int prev = gHs.beepPos;
                int k = pos >= 0x7fff || pos < 0 ? 0 : prev < 0 || prev >= 0x7fff || prev > pos ? pos : pos - prev;   // beep samples in it
                k = std::clamp(k, 0, n);
                if (pos >= beepLen && pos < 0x7fff) W16(w + 0x3f68, 0x7fff);
                gHs.beepPos = pos >= beepLen ? 0x7fff : pos;
                if (talker < 32) {
                    const unsigned long long now = PsMs(), opusAt = gVoiceOpus[talker];
                    gVoiceHeard[talker] = now;
                    beepAt = R32(w + 0x3f5c) & 0x01ffffff;
                    if (gVoiceOpusOn && opusAt && now - opusAt < kVoiceOpusMs && beepAt + 2u * (uint32_t)std::min(pos, beepLen) <= 0x02000000) {
                        swapFrom = std::min(pos, beepLen) - k; swapCount = k; ++gHs.swapped;
                    }
                }
            }
        }
        if (gHs.playing)
            for (uint32_t i = 0; i < bytes; i += 2) {
                int v = (int16_t)arg16(0x30 + i);
                if (own) v = std::clamp(v - gHs.lastRead[i / 2], -32768, 32767);
                else if (swapFrom >= 0) v = (int)(i / 2) < swapCount ? (int16_t)R16(beepAt + 2 * (uint32_t)(swapFrom + (int)(i / 2))) : 0;
                gHs.play.push_back((int16_t)v);
            }
        gHs.lastRead.clear();
        while (gHs.play.size() > kHsPlayMax) { gHs.play.pop_front(); ++gHs.dropped; }
        put32(0x20, bytes);
        ++gHs.writes; gHs.writeBytes += bytes;
        break;
    }
    case 0x12: put32(0x20, (uint32_t)gHs.rec.size() * 2); break;
    case 0x13: put32(0x20, (uint32_t)gHs.play.size() * 2); break;
    default: break;
    }
    put32(0, code);
    if (said.insert(fno).second) Log("lgaud: function %#x answered %#x (args %u bytes)", fno, code, (unsigned)args.size());
}
struct PendingRpc { uint32_t cd, fno, send, recv, endFn, endParam; long long due; std::vector<uint8_t> args; int polls = 0; };
std::vector<PendingRpc> gRpcs;
long long gFrameNo = 0;
void ServeOne(size_t k) {
    PendingRpc r = gRpcs[k];
    gRpcs.erase(gRpcs.begin() + k);
    uint32_t sid = RpcServerOf(r.cd);
    if (sid == 0x80001101) NetcnfRpc(r.fno, r.args, r.recv);
    else if (sid == 0x80000F81) EenetctlRpc(r.fno, r.args, r.recv);
    else if (sid == 0x80000001) FileioRpc(r.fno, r.args, r.recv);
    else if (sid == 0x80000211) UsbkbRpc(r.fno, r.args, r.recv);
    else if (sid == 0x50494c42) LgaudRpc(r.fno, r.args, r.recv);
    else {
        static std::set<std::pair<uint32_t, uint32_t>> said;
        if (said.insert({sid ? sid : r.cd, r.fno}).second)
            Log("sif rpc: server %08x (client %08x) function %#x answered with 0", sid, r.cd, r.fno);
        if (r.recv && r.recv + 4 <= 0x02000000) W32(r.recv, 0);
    }
    if (r.endFn) RunPs2(r.endFn, r.endParam);
}
// a request whose completion semaphore is polled in a loop, or waited on, is
// answered at once (the IOP finishes while the EE spins); a single poll in the
// frame of the call still sees it busy
bool ServeForSema(uint32_t sema, bool wait) {
    for (size_t k = 0; k < gRpcs.size(); ++k)
        if (gRpcs[k].endParam == sema && (wait || ++gRpcs[k].polls >= 3)) { ServeOne(k); return true; }
    // a thread that blocks lets the IOP finish what it was asked (libusbkb: the
    // request's end function signals the library's lock, its end parameter is 0)
    if (wait && !gRpcs.empty()) {
        while (!gRpcs.empty()) ServeOne(0);
        return true;
    }
    return false;
}
void DumpRingThread(unsigned th, const char* why) {
    std::set<std::pair<uint32_t, uint32_t>> seen; std::string s;
    th &= 15;
    for (unsigned k = 0; k < 64; ++k) {
        auto& e = gRingT[th][(gRingTPos[th] - 64 + k) & 63];
        if (e[0] && seen.insert({e[0], e[1]}).second) { char b[32]; snprintf(b, sizeof(b), " %08x<%08x", e[0], e[1] - 8); s += b; }
    }
    Log("recent calls of thread %u (%s):%s", th, why, s.c_str());
}
void DumpRing(const char* why) {
    std::set<std::pair<uint32_t, uint32_t>> seen; std::string s;
    for (unsigned k = 0; k < 64; ++k) {
        auto& e = gRing[(gRingPos - 64 + k) & 63];
        if (seen.insert({e[0], e[1]}).second) { char b[32]; snprintf(b, sizeof(b), " %08x<%08x", e[0], e[1] - 8); s += b; }
    }
    Log("recent calls (%s):%s", why, s.c_str());
}
void ServeRpcs() {
    static long long ringAt = DevEnv("SIM_RING_FRAME") ? atoll(DevEnv("SIM_RING_FRAME")) : -1;
    if (gFrameNo == ringAt) {
        DumpRing("frame");
        for (unsigned th = 1; th < 16; ++th) if (gRingTPos[th]) DumpRingThread(th, "frame");
        Log("the last loop turned in the function at %08x", (uint32_t)ps2::gLoopFn);
        for (unsigned th = 1; th < 16; ++th) if (ps2::gLoopFnOf[th]) Log("thread %u: its loops last checked in the function at %08x", th, ps2::gLoopFnOf[th]);
    }
    for (size_t k = 0; k < gRpcs.size();) {
        if (gRpcs[k].due > gFrameNo) { ++k; continue; }
        ServeOne(k);
    }
}

// interrupt handlers added by the code: VBlank start (2) and end (3) run once a frame
struct IntcHandler { int cause; uint32_t fn; bool live; uint32_t arg = 0; };
std::vector<IntcHandler> gIntc;
// DMAC channel handlers (AddDmacHandler): run when a transfer of their channel ends, while
// the channel's interrupt is enabled (D_STAT CIM, _EnableDmac); the GS handlers (INTC cause 0)
// when a SIGNAL / FINISH IMR lets through reaches the GS. Both only while DG runs: the MOD
// leaves the GS to the PC engine
struct DmacHandler { int ch; uint32_t fn; bool live; uint32_t arg; };
std::vector<DmacHandler> gDmac;
uint32_t gIntcMask = 0;                            // INTC_MASK bits (_EnableIntc)
long long gIrqRuns = 0;                            // handlers run (log)
void IrqDeliver() {
    if (gInIrq) return;
    gInIrq = true;
    const uint32_t eie = gEie;
    gEie = 0;                                      // a handler runs with interrupts masked (EXL)
    for (int guard = 0; guard < 1024 && __atomic_load_n(&gIrqPending, __ATOMIC_ACQUIRE); ++guard) {
        const int bit = __builtin_ctz(__atomic_load_n(&gIrqPending, __ATOMIC_ACQUIRE));
        __atomic_fetch_and(&gIrqPending, ~(1u << bit), __ATOMIC_SEQ_CST);   // the GS thread may raise another meanwhile
        if (bit < 10) {
            if (!((uint32_t)HwR(0x1000E010, 4) & (0x10000u << bit))) {         // CIM: the channel's interrupt is masked
                if (Once(((uint64_t)13 << 40) | (uint32_t)bit)) Log("irq: DMA channel %d ended with its interrupt masked (D_STAT %08x)", bit, (uint32_t)HwR(0x1000E010, 4));
                continue;
            }
            for (size_t k = 0; k < gDmac.size(); ++k) {
                if (!gDmac[k].live || gDmac[k].ch != bit) continue;
                uint64_t r = 0;
                ++gIrqRuns;
                RunPs2Args(gDmac[k].fn, {(uint32_t)bit, gDmac[k].arg, 0}, &r);
                if ((int32_t)r < 0) break;         // a handler that answers < 0 ends the chain
            }
        } else if (bit == 16) {
            if (!(gIntcMask & 1)) continue;
            for (size_t k = 0; k < gIntc.size(); ++k) {
                if (!gIntc[k].live || gIntc[k].cause != 0) continue;
                uint64_t r = 0;
                ++gIrqRuns;
                RunPs2Args(gIntc[k].fn, {0u, gIntc[k].arg, 0}, &r);
                if ((int32_t)r < 0) break;
            }
        }
    }
    gEie = eie;
    gInIrq = false;
}
// PS2 actor loop globals (lists 0x122768, one list 0x1228C0): the time step timers
// accumulate (300 units a second: 5 a pass, 10 for the half-rate actors below), and
// the current actor's function and +0x38 word. Run rule of 0x1228C0 on the low word
// of an actor's level: no bit of (run mask | 0x30) -> it runs every pass; else it is
// skipped when it has a bit of the skip mask or of 0x30, and runs with a step of 10.
// Half-rate pages (every map: `[d8cb74] -m 1` -> 0x144190(1): frame target 2, run
// mask 0x40, half-rate flag) put 0x40 in the skip mask every other pass (the DG frame
// actor 0x143CB0), so the 0x40 actors (radar, remote players) and the session daemons
// (0x172510, 0x172638 test it) run at 30 Hz, the rest at 60 Hz. The loop clears the
// skip mask after each pass.
uint32_t kTimeStep = 0x23A5D4, kCurActFn = 0x23A5F4, kCurActWord = 0x23A5F0;   // the running version's: VersionAddresses()
uint32_t kActRunMask = 0x23A618, kActSkipMask = 0x23A61C, kHalfRate = 0x23B8FC, kHalfPhase = 0x23B900;
int gTextLogs = 0;
uint32_t kVsyncTotal = 0x23B8E8, kVsyncFrame = 0x23B904;
// libmc2 (ntmenu2's copy): its request block (+4 command, 0 none; +8 result) and the word holding
// the id of the semaphore that completes a request (0x59A6A8 polls or waits on it)
uint32_t kMc2Req = 0x64C700, kMc2Sema = 0x64CC28;
// the named addresses of the running version (BootElf once it knows which one)
void VersionAddresses() {
    for (uint32_t* a : {&kTimeStep, &kCurActFn, &kCurActWord, &kActRunMask, &kActSkipMask, &kHalfRate, &kHalfPhase, &kVsyncTotal, &kVsyncFrame,
                        &kStageFlags}) {
        const uint32_t us = *a;
        *a = V(us);
        if (*a != us) Log("versions: %08x -> %08x", us, *a);
    }
}
// DG runs (its init, VBlank handlers and frame actors: the PS2 world draws through the GS);
// otherwise the platform stands in for its counters and half-rate toggle
bool gDgLive = false;
// EE timers T2 and T3: the code reads their counts with plain loads (session nonces
// 0x16FB3C, the game network's seed 0x172AFC), which land on RAM words 0x1000 and 0x1800;
// kept moving at BUSCLK / 256 from the host clock, offset per process so two instances
// started together still draw different nonces
void EeTimers() {
    static long long t0 = 0; static uint64_t salt = 0;
    const uint64_t f = (uint64_t)QpcFreq();
    if (!salt) {
        t0 = NetQpc();
        salt = ((uint64_t)GetCurrentProcessId() * 0x9E3779B97F4A7C15ull) ^ (uint64_t)t0;
        if (!salt) salt = 1;
    }
    uint64_t d = (uint64_t)(NetQpc() - t0);
    uint64_t n = d / f * 576000ull + d % f * 576000ull / f + salt;
    W32(0x10001000, (uint32_t)(n & 0xffff));
    W32(0x10001800, (uint32_t)((n >> 16) & 0xffff));
}
// The frame work of the PS2 base runs on two resident PC actors of the platform, at
// the levels of the base's own session daemons (NT_SessionStartDaemon 0x172638 at
// 0x0100000081204000, 0x172510 at 0x0C000000FF204000): the first list opens the frame
// (clock, timers, IOP answers, VBlank, pads), the last list closes it (the PS2 threads,
// which run while the EE game thread waits for the vsync). Persistence class 2 (bits
// 20-21 of the low word): the stage change (PS2 0x122568(0), PC exe+0x10E9B0(0))
// removes class 0 only, so the network keeps running into the match.
const uint64_t kWorldBeginLevel = 0x0100000081204000ull, kWorldEndLevel = 0x0C000000FF204000ull;
void TrampAct(void* pc);
int TrampDie(void* pc);
void RetryDies();
void* gWorldBegin = nullptr;
void* gWorldEnd = nullptr;
bool gOverlayLive = true;                           // false once the page's overlay has left PS2 memory
std::string gOverlayPath;                           // the overlay image, read again when the page comes back
std::string gImageDir;                              // where the page images are (ps2_images.py writes them)
bool gPageNt = true;                                // the loaded page is ntmenu2: the overlay-range bridges are its addresses
void* NewWorldActor(uint64_t level) {
    void* pc = P.gvNewActor ? P.gvNewActor(0x40, level) : nullptr;
    if (pc && P.gvSetNamedActor) P.gvSetNamedActor(pc, level, (void*)&TrampAct, (void*)&TrampDie);
    return pc;
}
extern long long gPassNo;                              // (the passes of the actor lists, below: the player build's too)
#ifndef MGO1_PLAYER_BUILD
// SIM_CHANGE_WATCH="addr:len@from-to[,...]": from VBlank `from` to `to` each region is compared at
// every call; a change is logged with the call that follows it and the one before (the writer runs
// between them). addr may go through pointers: "150bab0>88>0" is R32(R32(0x150bab0) + 0x88) + 0,
// resolved at `from`
struct ChangeWatch { std::string spec; uint32_t lo = 0, len = 0; long long from = -1, to = -1; std::vector<uint8_t> last; };
std::vector<ChangeWatch> gChangeWatches;
void ChangeWatchHook(uint32_t addr) {
    for (auto& w : gChangeWatches) {
        if (!w.len || w.last.empty()) continue;
        const uint8_t* p = gMem + w.lo;
        if (!memcmp(p, w.last.data(), w.len)) continue;
        size_t off = 0; while (off < w.len && p[off] == w.last[off]) ++off;
        size_t end = w.len; while (end > off && p[end - 1] == w.last[end - 1]) --end;
        Log("change: %08x+%#zx..%#zx before the call of %08x; the call before: %08x from %08x (thread %u, dg %lld, pass %lld)", w.lo, off, end,
            addr, gLastCall, gLastCaller - 8, gRingThread, gDgFrames.load(), gPassNo);
        memcpy(w.last.data(), p, w.len);
    }
}
void ChangeWatchFrame() {
    static bool init = false;
    if (!init) {
        init = true;
        if (const char* e = DevEnv("SIM_CHANGE_WATCH"))
            for (const char* q = e; *q;) {
                const char* c = strchr(q, ',');
                std::string one = c ? std::string(q, c) : std::string(q);
                ChangeWatch w; w.spec = one;
                const char* colon = strchr(one.c_str(), ':'), * at = strchr(one.c_str(), '@');
                if (colon && at) {
                    w.len = (uint32_t)strtoul(colon + 1, nullptr, 0);
                    w.from = atoll(at + 1);
                    const char* dash = strchr(at, '-');
                    w.to = dash ? atoll(dash + 1) : w.from + 1;
                    gChangeWatches.push_back(w);
                }
                if (!c) break;
                q = c + 1;
            }
    }
    bool any = false;
    for (auto& w : gChangeWatches) {
        if (gFrameNo == w.from) {
            uint32_t a = (uint32_t)strtoul(w.spec.c_str(), nullptr, 16);
            for (const char* g = strchr(w.spec.c_str(), '>'); g && g < strchr(w.spec.c_str(), ':'); g = strchr(g + 1, '>'))
                a = R32(a) + (uint32_t)strtoul(g + 1, nullptr, 16);
            w.lo = a & 0x01ffffff;
            if (w.lo + w.len > 0x02000000) w.len = 0;
            w.last.assign(gMem + w.lo, gMem + w.lo + w.len);
            Log("change watch: %s -> %08x, %u bytes, frames %lld to %lld", w.spec.c_str(), w.lo, w.len, w.from, w.to);
        }
        if (gFrameNo == w.to) w.last.clear();
        any |= !w.last.empty();
    }
    ps2::gCallHook = any ? &ChangeWatchHook : nullptr;
}
#endif
// SIM_SPIKE=<px>[:<ram dumps>] (tests: the moving hair's spikes, a vertex thrown far for a frame): a triangle whose
// longest side is <px> at least and 2.5 times the median of its texture's (TEX0) largest side over the last 32
// frames that texture was drawn in (seen in 32 frames at least: the map's large triangles are their textures'
// usual) logged with its corners' places and texture coordinates, 40 a frame at most; the two pictures after one
// (gs_<frame>.png, 80 at most) and the EE's memory at the first ones (ram_spike_<frame>.bin, <ram dumps>, 0)
struct SpikeTex { float cur = 0; long long frame = -1; std::vector<float> maxes; int at = 0; };
std::map<uint64_t, SpikeTex> gSpikeTex;
double gSpikePx = 0; int gSpikeRam = 0, gSpikePng = 0;
long long gDumpActorAt = -1;                           // SIM_GS_DUMP_ACTOR: the frame its dump starts
void SpikeHook(const ps2::GsPrimInfo& p) {
    if (p.type < 3 || p.type > 5) return;
    double side = 0;
    for (int k = 0; k < 3; ++k) {
        const double dx = p.xs[(k + 1) % 3] - p.xs[k], dy = p.ys[(k + 1) % 3] - p.ys[k];
        side = std::max(side, sqrt(dx * dx + dy * dy));
    }
    SpikeTex& t = gSpikeTex[p.tex0];
    if (t.frame != gFrameNo) {                          // the texture's last frame: its largest side kept
        if (t.frame >= 0) {
            if (t.maxes.size() < 32) t.maxes.push_back(t.cur);
            else { t.maxes[t.at] = t.cur; t.at = (t.at + 1) % 32; }
        }
        t.frame = gFrameNo; t.cur = 0;
    }
    t.cur = std::max(t.cur, (float)side);
    if (side < gSpikePx || t.maxes.size() < 32) return;
    std::vector<float> m = t.maxes;
    std::nth_element(m.begin(), m.begin() + 16, m.end());
    const float med = m[16];
    if (side < 2.5 * med) return;
    static long long frame = -1, total = 0; static int inFrame = 0;
    if (frame != gFrameNo) { frame = gFrameNo; inFrame = 0; }
    if (++inFrame > 40 || ++total > 200000) return;
    const bool st = !(p.prim & 0x100);                  // STQ: the coordinates s/q, t/q
    char b[320]; int n = 0;
    for (int k = 0; k < 3; ++k)
        n += snprintf(b + n, sizeof b - n, " (%.0f,%.0f %.4f,%.4f)", p.xs[k], p.ys[k], st && p.q[k] ? p.u[k] / p.q[k] : p.u[k],
                      st && p.q[k] ? p.v[k] / p.q[k] : p.v[k]);
    Log("spike: tex0 %016llx side %.0f median %.0f:%s", (unsigned long long)p.tex0, side, med, b);
    static int pngs = 0;
    if (inFrame == 1) {
        if (pngs < 80) { gSpikePng = 2; pngs += 2; }
        if (gSpikeRam > 0) {
            --gSpikeRam;
            char name[64]; snprintf(name, sizeof name, "ram_spike_%05lld.bin", gFrameNo);
            if (FILE* f = fopen(name, "wb")) { fwrite(gMem, 1, 0x02000000, f); fclose(f); Log("spike: %s written", name); }
        }
    }
}
// the simulation's per-frame hooks (SIM_* settings): logs, pictures, dumps, peeks; both builds
long long gTestDrawUntil = 0;                          // tests (basecap.h SIM_BC_PNG): the GS drawing kept on until then
void SimFrameHooks() {
#ifndef MGO1_PLAYER_BUILD
    {   // SIM_CALL_TRACE="from:to": every call logged in those frames (the called address, its caller, a0-a2)
        static long long from = -1, to = -1;
        static bool init = false;
        if (!init) { init = true; if (const char* e = DevEnv("SIM_CALL_TRACE")) sscanf(e, "%lld:%lld", &from, &to); }
        if (from >= 0) { ps2::gCallTrace = gFrameNo >= from && gFrameNo <= to; ps2::gCallTraceAll = ps2::gCallTrace; }
    }
    if (DevEnv("SIM_CHANGE_WATCH")) ChangeWatchFrame();
    {   // SIM_POOL_CHECK: pool 0 walked once a frame; the first inconsistency is logged with the calls before it
        static const bool check = DevEnv("SIM_POOL_CHECK") != nullptr;
        static std::string last;
        if (check) {
            const std::string w = PoolWalk();
            if (!w.empty() && w != last) Log("pool 0 walk: %s; calls before:%s", w.c_str(), RecentCalls(24).c_str());
            last = w;
        }
    }
#endif
    {   // the game network's players (table 0x28B5F4: +0 id, +2 state 0x201..0x216, +8 flags
        // 1 host 2 local) and the slot blocks' names (0x28C608 + 0xC8 * slot, +2), on each change
        static std::string last;
        std::string t;
        const uint32_t n = R32(V(0x28B5F0));
        for (uint32_t k = 0; k < 16 && n && n < 17; ++k) {
            const uint32_t e = V(0x28B5F4) + 0x5C * k;
            if (R8(e) == 0xFF || !R16(e + 2)) continue;    // a free entry (state 0)
            char b[96];
            snprintf(b, sizeof b, " P%u:%03x%s%s", R8(e) + 1, R16(e + 2), (R32(e + 8) & 1) ? "H" : "", (R32(e + 8) & 2) ? "L" : "");
            t += b;
        }
        if (t != last) { Log("players (%u):%s", n, t.empty() ? " none" : t.c_str()); last = t; }
    }
#ifndef MGO1_PLAYER_BUILD
    // MGO1_DUMP=1 (diagnostics): a file "dump_now" in the run folder -> all PS2 memory in mem_<frame>.bin
    // and the game's named actors in actors_<frame>.txt, once (the file is deleted)
    if (gNative && DevEnv("MGO1_DUMP") && (gDumpAt.count(gFrameNo) || (gFrameNo % 30 == 0 && GetFileAttributesA("dump_now") != INVALID_FILE_ATTRIBUTES))) {
        DeleteFileA("dump_now");
        gDumpAt.erase(gFrameNo);
        char name[64];
        snprintf(name, sizeof name, "mem_%lld.bin", gFrameNo);
        std::vector<uint8_t> copy(gMem, gMem + (32u << 20));    // the log-in data scrubbed out of the copy
        Scrub(copy.data(), copy.size());
        if (FILE* f = fopen(name, "wb")) { fwrite(copy.data(), 1, copy.size(), f); fclose(f); }
        snprintf(name, sizeof name, "actors_%lld.txt", gFrameNo);
        if (FILE* f = fopen(name, "w")) { for (const auto& a : gNativeActors) fprintf(f, "%08x %s\n", a.first, a.second.c_str()); fclose(f); }
        Log("dump: memory and actors at frame %lld", gFrameNo);
    }
#endif
    {   // SIM_GS_PNG="frame,frame..." / SIM_GS_PNG_EVERY=N: the picture the GS displays, as gs_<frame>.png
#ifndef MGO1_PLAYER_BUILD
        static std::set<long long> at; static int every = 0; static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = getenv("SIM_GS_PNG")) for (const char* q = e; *q;) { char* end; long long v = strtoll(q, &end, 10); if (end == q) break; at.insert(v); q = *end ? end + 1 : end; }
            if (const char* e = getenv("SIM_GS_PNG_EVERY")) every = atoi(e);
            if (const char* e = getenv("SIM_GS_DRAW_FROM")) ps2::gGsDraw = false, (void)e;
            if (const char* e = getenv("SIM_SPIKE")) {
                gSpikePx = atof(e);
                if (const char* c = strchr(e, ':')) gSpikeRam = atoi(c + 1);
                ps2::gGsPrimHook = &SpikeHook;
                Log("spike: triangles of %.0f pixels and more, 2.5 times their texture's usual, logged", gSpikePx);
            }
        }
        if (const char* e = DevEnv("SIM_GS_DRAW_FROM")) if (gFrameNo >= atoll(e)) ps2::gGsDraw = true;
        // SIM_GS_DRAW_EVERY=N: rasterize only the last 3 frames of every N (the pictures at multiples of N),
        // registers and transfers always: a drawing run keeps close to real time for the other instances
        if (const char* e = DevEnv("SIM_GS_DRAW_EVERY")) {
            const long long n = atoll(e);
            const char* from = getenv("SIM_GS_DRAW_FROM");
            if (n > 3 && (!from || gFrameNo >= atoll(from))) ps2::gGsDraw = (gFrameNo % n) >= n - 3 || gFrameNo <= gTestDrawUntil;
        }
        // open list: SIM_GS_DRAW_WINDOW=K: the last K frames of every N drawn (3 above); an instance slower than a pass
        // in 3 VBlanks (two at 60 on two processors, the GS thread) drew none there: its picture was an older one
        if (const char* w = DevEnv("SIM_GS_DRAW_WINDOW")) if (const char* e = DevEnv("SIM_GS_DRAW_EVERY")) {
            const long long n = atoll(e), k = atoll(w);
            const char* from = getenv("SIM_GS_DRAW_FROM");
            if (k > 3 && n > k && (!from || gFrameNo >= atoll(from)) && (gFrameNo % n) >= n - k) ps2::gGsDraw = true;
        }
        // SIM_HD=<dir>: the HD textures' keys made from the start (docs/TEXTURES.md), each new texture
        // written in <dir> as the dump does, the keys counted every 600 frames (their stability)
        if (const char* e = DevEnv("SIM_HD")) {
            static bool on = false;
            if (!on) {
                on = true;
                ps2::hdtex::Settings s; s.dump = true; s.dumpDir = e; s.threads = 1;
                ps2::hdtex::Configure(s);
                ps2::GsSetHdTextures(true);
                Log("hd textures: keys from now on, new textures written in %s", e);
            }
            if (gFrameNo % 600 == 0) Log("hd textures: %s", ps2::GsHdStats().c_str());
        }
        // SIM_GS_DUMP="frame[:frames]": the GS state at that frame, then what reaches the GS for that
        // many frames (1), in gs_<frame>.dump (tests/gs_replay.cpp runs it again offline)
        if (const char* e = DevEnv("SIM_GS_DUMP")) {
            static long long endAt = -1;
            const long long at = atoll(e);
            const char* c = strchr(e, ':');
            if (gFrameNo == at) {
                char name[64]; snprintf(name, sizeof name, "gs_%05lld.dump", gFrameNo);
                endAt = at + (c ? atoll(c + 1) : 1);
                Log("gs dump: %s %s", name, ps2::GsDumpBegin(name) ? "started" : "not started");
            } else if (gFrameNo == endAt) { ps2::GsDumpEnd(); Log("gs dump: ended"); }
        }
#endif
        // SIM_GS_DUMP_ACTOR=<name>[:frames] (tests): such a dump from the frame after the first actor of that name is made
        // (the FAMAS's bullet: NewSniperBullet, alive four frames, a few frames earlier or later from run to run)
        if (const char* e = getenv("SIM_GS_DUMP_ACTOR")) {
            static long long endAt = -1;
            if (gDumpActorAt >= 0 && gFrameNo == gDumpActorAt) {
                const char* c = strchr(e, ':');
                char name[64]; snprintf(name, sizeof name, "gs_%05lld.dump", gFrameNo);
                endAt = gFrameNo + (c ? atoll(c + 1) : 2);
                Log("gs dump: %s %s (the actor %.*s made)", name, ps2::GsDumpBegin(name) ? "started" : "not started", (int)strcspn(e, ":"), e);
                gDumpActorAt = LLONG_MAX;                                          // once
            } else if (gFrameNo == endAt) { ps2::GsDumpEnd(); Log("gs dump: ended"); }
        }
        // SIM_GS_DUMP_FILE=<file> (tests): "k frames" written there (k higher than the last) starts such a dump at
        // the next frame checked (every 10): a moment a watcher sees in the log (the scope's view)
        if (const char* lf = getenv("SIM_GS_DUMP_FILE"); lf && gFrameNo % 10 == 0) {
            static int seenK = 0; static long long endAt = -1;
            if (gFrameNo == endAt) { ps2::GsDumpEnd(); Log("gs dump: ended"); endAt = -1; }
            if (endAt < 0)
                if (FILE* f = fopen(lf, "rb")) {
                    int k = 0, n = 0;
                    if (fscanf(f, "%d %d", &k, &n) == 2 && k > seenK && n > 0) {
                        seenK = k;
                        char name[64]; snprintf(name, sizeof name, "gs_%05lld.dump", gFrameNo);
                        endAt = gFrameNo + std::max(10, (n + 9) / 10 * 10);
                        Log("gs dump: %s %s (asked: %d)", name, ps2::GsDumpBegin(name) ? "started" : "not started", k);
                    }
                    fclose(f);
                }
        }
        // the language the scripts read (Platform::gameLanguage): link variable 4 ([[0x23A678] + 4], a byte),
        // set by the boot page (init_n) from the PS2's system language to English or the disc's other one
        const int wantLang = gTestLanguage > 0 ? gTestLanguage : P.gameLanguage;   // tests: a language set by a step
        if (wantLang > 0) {
            const uint32_t lv = R32(V(0x23A678));
            if (lv >= 0x100000 && lv + 8 < 0x02000000 && R8(lv + 4) != (uint8_t)wantLang) {
                static int told = 0;
                if (told++ < 3 || gTestLanguage > 0) Log("language: the scripts' language %u -> %d (link variable 4 at %08x)", R8(lv + 4), wantLang, lv + 4);
                W8(lv + 4, (uint8_t)wantLang);
            }
        }
#ifndef MGO1_PLAYER_BUILD
        // SIM_XFER_LOG="frame:count": from that frame, the sound driver's next count EE <-> IOP transfers
        if (const char* e = DevEnv("SIM_XFER_LOG")) if (gFrameNo == atoll(e)) { const char* c = strchr(e, ':'); iop::LogTransfers(c ? atoi(c + 1) : 200); }
        // SIM_RAM_DUMP="frame,frame...": the EE's 32 MB at that frame as ram_<frame>.bin (texts, tables, offline)
        if (const char* e = DevEnv("SIM_RAM_DUMP")) {
            for (const char* q = e; *q;) {
                char* end; const long long v = strtoll(q, &end, 10); if (end == q) break;
                if (v == gFrameNo) {
                    char name[64]; snprintf(name, sizeof name, "ram_%05lld.bin", gFrameNo);
                    if (FILE* f = fopen(name, "wb")) { fwrite(gMem, 1, 0x02000000, f); fclose(f); Log("ram dump: %s", name); }
                }
                q = *end ? end + 1 : end;
            }
        }
        // SIM_CALLTREE="fn@frame[:lines][,fn@frame[:lines]...]": the calls inside the first run of fn from that
        // frame, by depth (one tree at a time: each armed at its frame)
        if (const char* e = getenv("SIM_CALLTREE")) {
            for (const char* q = e; q && *q; q = strchr(q, ','), q = q ? q + 1 : nullptr) {
                const char* at = strchr(q, '@');
                const char* next = strchr(q, ',');
                if (!at || (next && at > next)) continue;
                if (gFrameNo == atoll(at + 1)) {
                    const char* c = strchr(at, ':');
                    if (c && next && c > next) c = nullptr;
                    ps2::gTreeRoot = (uint32_t)strtoul(q, nullptr, 16); ps2::gTreeLines = c ? atoi(c + 1) : 3000; ps2::gTreeArmed = true;
                    Log("call tree: %08x armed at frame %lld", ps2::gTreeRoot, gFrameNo);
                }
            }
        }
        // SIM_FN_DIFF's report, after its second window
        if (gFnDiffW[0] >= 0 && gFrameNo == gFnDiffW[3] + 1) {
            for (int k = 0; k < 2; ++k) {
                std::string t; int n = 0;
                for (auto& x : gFnDiff[k]) {
                    if (gFnDiff[1 - k].count(x.first)) continue;
                    char b[32]; snprintf(b, sizeof b, " %08x:%u", x.first, x.second); t += b;
                    if (++n % 12 == 0) { Log("fn diff: only in %c:%s", k ? 'B' : 'A', t.c_str()); t.clear(); }
                }
                if (!t.empty() || n == 0) Log("fn diff: only in %c:%s", k ? 'B' : 'A', t.empty() ? " none" : t.c_str());
            }
            std::string t; int n = 0;   // in both, the count in B at least 3 times A's or a third of it
            for (auto& x : gFnDiff[1]) {
                auto it = gFnDiff[0].find(x.first);
                if (it == gFnDiff[0].end()) continue;
                if (x.second >= 3 * it->second || 3 * x.second <= it->second) {
                    char b[40]; snprintf(b, sizeof b, " %08x:%u/%u", x.first, it->second, x.second); t += b;
                    if (++n % 10 == 0) { Log("fn diff: counts A/B:%s", t.c_str()); t.clear(); }
                }
            }
            if (!t.empty()) Log("fn diff: counts A/B:%s", t.c_str());
        }
        // SIM_GS_TRACE="frame[:lines]": that frame's DMA tags, VIF commands, GIF tags and GS registers
        if (const char* e = DevEnv("SIM_GS_TRACE")) if (gFrameNo == atoll(e)) {
            const char* c = strchr(e, ':');
            ps2::gGsTrace = c ? atoi(c + 1) : 4000;
            Log("gs trace: from frame %lld, %d lines", gFrameNo, ps2::gGsTrace);
        }
        static const long long drawFrom = getenv("SIM_GS_DRAW_FROM") ? atoll(getenv("SIM_GS_DRAW_FROM")) : 0;
        const bool spikePng = gSpikePng > 0 && gFrameNo >= drawFrom;
        if (spikePng) --gSpikePng;
        if (spikePng || at.count(gFrameNo) || (every > 0 && gFrameNo > 0 && gFrameNo % every == 0 && gFrameNo >= drawFrom)) {   // (none before
            char name[64]; snprintf(name, sizeof name, "gs_%05lld.png", gFrameNo);                                   // the drawing: black)
            Log("gs: %s %s", name, ps2::GsWritePng(name) ? "written" : "not written (no display set)");
            // SIM_YELLOW_TRACE=<lines> (tests: REX Hangar's scene turned yellow after the first person view in one run
            // of five, 30 September, hudA43 at 6040): a picture whose middle turns yellow logged, the frames after it
            // traced (SIM_GS_TRACE's lines), twice at most
            static const int yellowTrace = getenv("SIM_YELLOW_TRACE") ? atoi(getenv("SIM_YELLOW_TRACE")) : 0;
            std::vector<uint32_t> px; int w = 0, h = 0;
            if (yellowTrace > 0 && ps2::GsDisplayed(px, w, h) && w > 8 && h > 8 && px.size() >= (size_t)w * h) {
                double r = 0, g = 0, b = 0; int n = 0;
                for (int y = h / 4; y < 3 * h / 4; y += 2)
                    for (int x = w / 4; x < 3 * w / 4; x += 2) { const uint32_t p = px[(size_t)y * w + x]; r += p & 0xFF; g += (p >> 8) & 0xFF; b += (p >> 16) & 0xFF; ++n; }
                r /= n; g /= n; b /= n;
                static bool was = false; static int traces = 0;
                const bool yellow = r > 170 && g > 130 && r - b > 60;
                static bool whiteSeen = false;                  // the white picture the yellow one followed (hudA329x)
                if (!whiteSeen && r > 225 && g > 225 && b > 180) {
                    whiteSeen = true;
                    char name[64]; snprintf(name, sizeof name, "ram_white_%05lld.bin", gFrameNo);
                    if (FILE* f = fopen(name, "wb")) { fwrite(gMem, 1, 0x02000000, f); fclose(f); }
                    Log("yellow: the middle of frame %lld is %.0f %.0f %.0f (a white picture): %s written", gFrameNo, r, g, b, name);
                }
                static std::string pcrtc;                       // the display circuits' registers when they change
                const std::string now = ps2::GsPcrtc();         // or the picture turns yellow or back (lookVenus3: the
                if (now != pcrtc || yellow != was) {            // GPU drew those frames black, the software yellow)
                    Log("yellow: frame %lld middle %.0f %.0f %.0f%s: %s", gFrameNo, r, g, b, yellow ? " (yellow)" : "", now.c_str());
                    pcrtc = now;
                }
                if (yellow && !was && traces < 2) {
                    ++traces;
                    ps2::gGsTrace = yellowTrace;
                    Log("yellow: the middle of frame %lld is %.0f %.0f %.0f (the one before was not): the frames after it traced, %d lines", gFrameNo, r, g, b, yellowTrace);
                    // the EE's memory then too (ram_yellow_<frame>.bin: the colour filter's state, against a run
                    // that stayed right, SIM_RAM_DUMP at the same step)
                    char name[64]; snprintf(name, sizeof name, "ram_yellow_%05lld.bin", gFrameNo);
                    if (FILE* f = fopen(name, "wb")) { fwrite(gMem, 1, 0x02000000, f); fclose(f); Log("yellow: %s written", name); }
                }
                was = yellow;
            }
        }
#endif
    }
#ifndef MGO1_PLAYER_BUILD
    {   // SIM_DMA_COUNT=N: every N frames, the DMA starts per channel since the last report
        static const int every = DevEnv("SIM_DMA_COUNT") ? atoi(DevEnv("SIM_DMA_COUNT")) : 0;
        if (every > 0 && gFrameNo % every == 0) {
            std::string t;
            for (int k = 0; k < 16; ++k) if (ps2::gDmaStarts[k]) { char b[32]; snprintf(b, sizeof b, " ch%d:%u", k, ps2::gDmaStarts[k]); t += b; ps2::gDmaStarts[k] = 0; }
            Log("dma starts in %d frames:%s", every, t.empty() ? " none" : t.c_str());
            Log("gs in %d frames: %s; %lld interrupt handler runs; hud %s", every, ps2::GsStats().c_str(), gIrqRuns, ps2::GsHudStats().c_str());
            gIrqRuns = 0;
            if (!gCallCounts.empty()) {
                std::string c;
                for (auto& x : gCallCounts) { char b[32]; snprintf(b, sizeof b, " %08x:%u", x.first, x.second); c += b; x.second = 0; }
                Log("calls in %d frames:%s", every, c.c_str());
            }
        }
    }
    {   // SIM_PEEK="expr;expr...": values logged when they change. expr: [@Actor|hex]{[+off]|+off}...[:size]
        // "[23a678]+5d4:2" reads u16 at R32(0x23a678) + 0x5d4; "@NewWeaponChangeLinear+3a0" the u32 at
        // that actor's work + 0x3a0 (the first live one); brackets dereference what they hold
        struct Peek { std::string text; uint32_t last = 0; bool seen = false; };
        static std::vector<Peek> peeks;
        static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = DevEnv("SIM_PEEK")) {
                std::string all = e; size_t p = 0;
                while (p < all.size()) { size_t q = all.find(';', p); if (q == std::string::npos) q = all.size(); if (q > p) peeks.push_back({all.substr(p, q - p)}); p = q + 1; }
            }
        }
        for (Peek& k : peeks) {
            const std::string& t = k.text;
            size_t i = 0; uint32_t a = 0; bool ok = true; int size = 4;
            auto num = [&](void) -> uint32_t { size_t s = i; while (i < t.size() && isxdigit((unsigned char)t[i])) ++i; return (uint32_t)strtoul(t.substr(s, i - s).c_str(), nullptr, 16); };
            std::function<uint32_t(void)> term = [&](void) -> uint32_t {
                if (i < t.size() && t[i] == '[') { ++i; uint32_t v = term(); while (i < t.size() && t[i] == '+') { ++i; v += num(); } if (i < t.size() && t[i] == ']') ++i; return (v && v < 0x02000000) ? R32(v) : (ok = false, 0); }
                if (i < t.size() && t[i] == '@') {
                    ++i; size_t s = i; while (i < t.size() && (isalnum((unsigned char)t[i]) || t[i] == '_')) ++i;
                    const std::string n = t.substr(s, i - s);
                    for (const Actor& x : gActors) if (x.live && x.name == n) return x.work;
                    ok = false; return 0;
                }
                return num();
            };
            a = term();
            while (i < t.size() && t[i] == '+') { ++i; a += num(); }
            if (i < t.size() && t[i] == ':') { ++i; size = atoi(t.c_str() + i); }
            if (!ok || a >= 0x02000000) continue;
            const uint32_t v = size == 1 ? R8(a) : size == 2 ? R16(a) : R32(a);
            if (!k.seen || v != k.last) { Log("peek %s = %#x", t.c_str(), v); k.last = v; k.seen = true; }
        }
    }
    {   // SIM_FIND_POS=frame: where the local player's position (NetworkDummyPlayer + 0x110) is held in RAM
        // at that frame (float triples within 1 unit), to find the fields that move the player
        static const long long at = DevEnv("SIM_FIND_POS") ? atoll(DevEnv("SIM_FIND_POS")) : -1;
        float me[3];
        if (gFrameNo == at && PlayerPos(0, me)) {
            std::string t; int n = 0;
            for (uint32_t a = 0x100000; a + 12 <= 0x02000000 && n < 40; a += 4) {
                float v[3]; memcpy(v, gMem + a, 12);
                if (fabsf(v[0] - me[0]) < 1.f && fabsf(v[1] - me[1]) < 1.f && fabsf(v[2] - me[2]) < 1.f) {
                    char b[16]; snprintf(b, sizeof b, " %08x", a); t += b; ++n;
                }
            }
            Log("position %.1f %.1f %.1f held at:%s", me[0], me[1], me[2], t.c_str());
        }
    }
    {   // SIM_WORK_DUMP="Name:len@f,f,...": the first live actor of that name, its work's bytes at those
        // frames in work_<Name>_<frame>.bin (diff two of them to find a field)
        static std::string name; static uint32_t len = 0; static std::set<long long> at; static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = DevEnv("SIM_WORK_DUMP")) {
                std::string v = e; const size_t c = v.find(':'), a = v.find('@');
                if (c != std::string::npos && a != std::string::npos) {
                    name = v.substr(0, c); len = (uint32_t)strtoul(v.substr(c + 1, a - c - 1).c_str(), nullptr, 0);
                    for (const char* q = v.c_str() + a + 1; *q;) { char* end; long long f = strtoll(q, &end, 10); if (end == q) break; at.insert(f); q = *end ? end + 1 : end; }
                }
            }
        }
        if (!name.empty() && at.count(gFrameNo))
            for (const Actor& a : gActors) if (a.live && a.name == name && a.work && a.work + len <= 0x02000000) {
                char fn[96]; snprintf(fn, sizeof fn, "work_%s_%05lld.bin", name.c_str(), gFrameNo);
                if (FILE* f = fopen(fn, "wb")) { fwrite(gMem + a.work, 1, len, f); fclose(f); Log("work dump: %s (%08x, %#x bytes)", fn, a.work, len); }
                break;
            }
    }
    {   // SIM_PLAYER_POS=N: every N frames, the positions the player works hold (+0x110 / +0x240 / +0xe00)
        static const int every = DevEnv("SIM_PLAYER_POS") ? atoi(DevEnv("SIM_PLAYER_POS")) : 0;
        if (every > 0 && gFrameNo % every == 0) {
            std::string t;
            for (const Actor& a : gActors) {
                if (!a.live || a.name.find("Player") == std::string::npos || a.name.find("Network") == std::string::npos) continue;
                if (a.name.find("Dummy") == std::string::npos && a.name != "NewNetworkPlayer") continue;
                auto F = [&](uint32_t o) { float f; uint32_t u = R32(a.work + o); memcpy(&f, &u, 4); return f; };
                char b[200];
                snprintf(b, sizeof b, " [%s %08x: %.0f %.0f %.0f | %.0f %.0f %.0f | %.0f %.0f %.0f]", a.name.c_str() + 3, a.work,
                         F(0x110), F(0x114), F(0x118), F(0x240), F(0x244), F(0x248), F(0xe00), F(0xe04), F(0xe08));
                t += b;
            }
            if (!t.empty()) Log("player works:%s", t.c_str());
        }
    }
#endif
}
void FrameBegin() {
    {
        ++gFrameNo;
        EeTimers();
        DeliverNetEvents();
        static bool booted = false;
        if (!booted) {                              // resident actors of the base's boot (0x104460..)
            booted = true;
            RunPs2(V(0x1052E0), 0);                    // USB keyboard: sceUsbKbInit, actor with act 0x104DE8
            RunPs2(V(0x105F00), 0);                    // the stream heap (GM_Init 0x104518): 0x1F00000-0x1F80000 in 0x800 units
                                                    // (0x123F88); the stream actors (0x105B90) take their buffers there
            RunPs2(V(0x16d8f0), 0x01F80000, 0x7F000);  // the base's network init with the boot's buffer region: EENet, libeenetctl and its
                                                    // threads, the event handler 0x16CB78, network state 1
            RunPs2(V(0x173160), 0);                    // the base's online init, as the boot runs it: configuration blocks 0..17,
                                                    // two resident actors, the TX55 context (global 0x28C580)
            DiscSpan stageDat;
            if (DiscFind("MGS\\NET\\STAGE_N.DAT", stageDat)) {   // the stage archives: the base's cdbios client and archive table
                W32(V(0x2C5DD8), 2);                   // the disc: the network disc's archives (net/stage_n.dat, net/slot_n.dat)
                RunPs2Args(V(0x100AB8), {0}, nullptr, 200000000);   // the file system: cdbios, every archive of that disc
                                                    // (table 0x229B10, position at +0x10), the stage table of net/stage_n.dat
                for (uint32_t e = V(0x229B10); R32(e); e += 0x18)
                    Log("archive %s: position %08x", Str(R32(e + 8)), R32(e + 0x10));
            }
            ConfigBlocks("boot");
            // the loaders of the file types the stage pages hold, registered with the file system
            // cache (0x122FE8(type, handler, flags): table 0x284800) as the boot does, so the page
            // loader relocates what it reads: DG's (its init 0x143E98 also sets up the GS, not run
            // here: only its registrations), geom 0x157FD0, motions 0x162840, nav 0x1633D0, zon 0x16CB48
            const bool dg = gDgLive = DevEnv("SIM_DG") && atoi(DevEnv("SIM_DG")) != 0;
            if (dg) {
                // the PS2 world draws itself: DG's whole init (GS reset and display, VIF1 and VU1
                // reset, its VBlank handlers, its loaders, its two frame actors 0x143CB0 / 0x143DC0);
                // the GS reports its own events and the DMA / GS interrupts reach their handlers
                ps2::gGsReal = true;
                ps2::gIrqDeliver = &IrqDeliver;
                if (const char* e = DevEnv("SIM_GS_THREADS")) ps2::GsSetThreads(atoi(e));   // drawing threads (0: this one)
                RunPs2(V(0x143e98), 0);
                Log("DG: init 0x143E98 run (the PS2 world draws through its GS)");
            } else {
                RunPs2Args(V(0x122fe8), {0xd, V(0x147ee8), 0x80000000});
                RunPs2Args(V(0x122fe8), {0xd, V(0x147ee8), 0x80000000});
                RunPs2Args(V(0x122fe8), {0x10, V(0x147fd8), 0x80000000});
                RunPs2Args(V(0x122fe8), {3, V(0x1277e8), 0x80000000});
                RunPs2Args(V(0x122fe8), {6, V(0x148018), 0x80000000});
            }
            for (uint32_t init : {0x157fd0u, 0x162840u, 0x1633d0u, 0x16cb48u}) RunPs2(V(init), 0);
            if (!dg) {
                // DG init's memory-only parts (its tables, the texture manager); the rest of 0x143E98
                // sets up the GS, the DMA and their interrupts, which the PC side does
                RunPs2(V(0x146c20), 0);
                RunPs2(V(0x12ac70), 0);
            }
            {
                std::string t;
                for (uint32_t k = 0, n = R32(V(0x2847B0) + 0x48); k < n && k < 32; ++k) {
                    char b[32]; snprintf(b, sizeof b, " %u:%08x", R8(V(0x2847B0) + 0x50 + 8 * k), R32(V(0x2847B0) + 0x50 + 8 * k + 4)); t += b;
                }
                Log("file type loaders:%s", t.c_str());
            }
            RunPs2(V(0x121C30), 0);                    // the resident mark (boot 0x1005D4): what the boot put in the resident
                                                    // area (the stage table of the file system) stays through page changes
            if (DevEnv("SIM_SELFTEST")) {           // snprintf through newlib's formatter, in isolation
                const uint32_t buf = 0x01F00000, fmt = 0x01F00100, s1 = 0x01F00140;
                memset(gMem + buf, 0x2A, 64);
                memcpy(gMem + fmt, "name=%s&n=%d", 13); memcpy(gMem + s1, "SNAKE", 6);
                Ctx c{};
                c.r[29] = kStackTop; c.r[28] = gMainGp;
                c.r[4] = buf; c.r[5] = 64; c.r[6] = fmt; c.r[7] = s1; c.r[8] = 42;
                Ctx* outer = gActive; gActive = &c;
                Call(c, V(0x60d2e0));
                gActive = outer;
                Log("selftest snprintf -> %lld, \"%.40s\"", (long long)c.r[2], (const char*)(gMem + buf));
            }
        }
        ServeRpcs();
        if (!gDgLive) {                             // DG's VBlank handler 0x143C08 counts them itself when it runs
            W32(kVsyncTotal, R32(kVsyncTotal) + 1); // the VBlank handler's counters (60 Hz, one vsync a frame)
            W32(kVsyncFrame, R32(kVsyncFrame) + 1);
        }
        W32(kActSkipMask, 0);                       // a new pass of the actor lists (0x122768 clears it after each)
        if (!gDgLive && R32(kHalfRate)) {           // the DG frame actor's half-rate toggle (0x143CB0), when it does not run
            uint32_t phase = 1 - R32(kHalfPhase);
            W32(kHalfPhase, phase);
            if (phase) W32(kActSkipMask, 0x40);
        }
        SimFrameHooks();
        for (int cause : {2, 3})
            for (size_t k = 0; k < gIntc.size(); ++k)
                if (gIntc[k].live && gIntc[k].cause == cause) RunPs2(gIntc[k].fn, (uint32_t)cause);
        UpdatePads();
        if (!gWorldEnd) {                           // after the boot's daemons, so it closes the last list
            gWorldEnd = NewWorldActor(kWorldEndLevel);
            Log("world: frame end actor %p (level %016llx)", gWorldEnd, (unsigned long long)kWorldEndLevel);
        }
    }
}
void TrampAct(void* pc) {
    if (pc && pc == gWorldBegin) { FrameBegin(); return; }
    if (pc && pc == gWorldEnd) { RetryDies(); Schedule(); return; }
    auto it = gByPc.find(pc);
    if (it == gByPc.end()) return;
    const size_t k = it->second;                    // by index: the act may make actors (gActors grows)
    const Actor& a = gActors[k];
    if (a.live && a.act) {
        const uint32_t low = (uint32_t)a.level, act = a.act, work = a.work;
        bool slow = false;                          // the run rule of 0x1228C0
        if (low & (R32(kActRunMask) | 0x30)) {
            if ((low & R32(kActSkipMask)) || (low & 0x30)) return;
            slow = true;
        }
        uint32_t fn = R32(kCurActFn), word = R32(kCurActWord);
        if (slow) W32(kTimeStep, 10);
        W32(kCurActFn, act); W32(kCurActWord, R32(work + 0x38));
        bool ok = RunPs2(act, work);
        if (slow) W32(kTimeStep, 5);
        W32(kCurActFn, fn); W32(kCurActWord, word);
        if (!ok) { gActors[k].live = false; Log("actor %s %08x stopped (stuck)", gActors[k].name.c_str(), work); }
    }
}
// an actor's end, as the base's death list runs it (0x122218, at the end of the GV frame
// 0x122768): its die, then its work back where it came from (0x121528; a work in static
// memory stays). A die answering 1 while the level's low word has 0x8000 is not done: the
// actor stays dying, without act, and its die runs again the next frame (GEO_DaemonStart
// 0x14D4C0 waits so for its geometry releases). A stage restart (flags 0x40) keeps pool 0,
// so the next actors need these blocks. The die may make or end other actors: gActors can
// grow meanwhile, nothing is kept by reference across it.
int EndActor(size_t k) {
    const uint32_t work = gActors[k].work, die = gActors[k].die, gen = gActors[k].poolGen;
    const uint64_t level = gActors[k].level;
    const std::string name = gActors[k].name;
    uint64_t rv = 0;
    if (die) RunPs2(die, work, 0, &rv);
    if (((uint32_t)level & 0x8000) && (uint32_t)rv == 1) {
        if (!gActors[k].dying) Log("actor %s %08x: its die is not done (again next frame)", name.c_str(), work);
        gActors[k].dying = true;
        return 1;
    }
    gActors[k].dying = false;
    gActors[k].ownRetry = false;
    W64(work + 0x18, 0);                                // the handle no longer names a live actor
    if (work >= 0xF00000 && work < 0x1B00000) {
        if (gen != gPoolGen) Log("actor %s: its pool 0 block went with the pool's reset", name.c_str());
        else if ((int8_t)R8(work - 0x10 + 0xA) <= 0) Log("actor %s %08x: its pool 0 block is free already (refs %d): not freed again", name.c_str(), work, (int8_t)R8(work - 6));
        else RunPs2(V(0x121528), work);
    } else if (work >= 0x01B00000 && work < 0x01D10000) Free(work);
    return (int)rv;
}
// the PS2 ends one of its actors (GV_DestroyActor 0x1224A0): no act from now on and its
// handle cleared at once, its die at the end of the frame (RetryDies), as the base does
void KillActor(size_t k) {
    if (k >= gActors.size() || !gActors[k].live) return;
    gActors[k].live = false;
    gActors[k].dying = true;
    gActors[k].ownRetry = true;
    W64(gActors[k].work + 0x18, 0);
}
// the dies of the frame: the actors the PS2 ended, and those whose die was not done
void RetryDies() {
    for (size_t k = 0; k < gActors.size(); ++k)
        if (gActors[k].dying && gActors[k].ownRetry) EndActor(k);
}
// the engine ends the PC actor (stage change, restart): the die's answer goes back to the
// engine, which runs it again the next frame while it answers 1 (level flag 0x8000)
int TrampDie(void* pc) {
    if (pc && (pc == gWorldBegin || pc == gWorldEnd)) {
        Log("world: frame %s actor removed by the engine", pc == gWorldBegin ? "begin" : "end");
        if (pc == gWorldBegin) gWorldBegin = nullptr; else gWorldEnd = nullptr;
        return 0;
    }
    auto it = gByPc.find(pc);
    if (it == gByPc.end()) return 0;
    const size_t k = it->second;
    if (gActors[k].dying) { gActors[k].ownRetry = false; return EndActor(k); }   // asked again
    if (!gActors[k].live) return 0;
    gActors[k].live = false;
    Log("actor %s %08x dies with its PC actor", gActors[k].name.c_str(), gActors[k].work);
    return EndActor(k);
}


// ---- EE kernel (syscall wrappers of the base image): PS2 threads run one at a
// time, on OS threads handing over a single baton; semaphores and sleep block
// by giving the baton back. The game thread is PS2 thread 1.
struct EeThread { bool deleted = false;
    int id; uint32_t entry = 0, stack = 0, size = 0, gp = 0, arg = 0; int prio = 0;
    int state = 0;                 // 0x10 dormant, 2 ready, 1 running, 4 waiting, 5 done
    int waitSema = -1, wakeups = 0;
    HANDLE os = nullptr, go = nullptr;
    long long seq = 0;             // native: ready / wait order (the lowest of a priority runs or is woken first)
    long long wakeAt = 0;          // native: DelayThread's end (QueryPerformanceCounter ticks)
    uint32_t eie = 1;              // native: the thread's own interrupt enable (di / ei), kept across switches
    long long runTicks = 0, slices = 0;   // native: time with the baton, runs (SIM_THREAD_STATS)
};
struct EeSema { int count = 0, max = 0, init = 0; uint32_t attr = 0, option = 0; bool live = true; };
std::vector<EeThread> gThreads;
HANDLE gOsOf[256] = {};            // a PS2 thread's OS thread by its id (the profiler reads it from its own thread)
std::vector<EeSema> gSemas;
int gCur = 1;                      // running PS2 thread
HANDLE gBack = nullptr;            // set when a PS2 thread gives the baton back
long long gSlices = 0;

EeThread* Thr(int id) { return (id >= 1 && id <= (int)gThreads.size()) ? &gThreads[id - 1] : nullptr; }
// (v141 beta 12) the baton on futexes: under Wine an event is a wineserver round trip (four a handover, some
// 800 a second); RtlWaitOnAddress / RtlWakeAddressSingle wait in the process. The host build only;
// MGO1_BATON_EVENTS=1 the events, as before (the player build keeps them)
struct Baton { volatile LONG v = 0; };
Baton gGoOf[256], gBackB;
#if defined(_WIN32) && !defined(MGO1_PLAYER_BUILD)
using WaitAddrFn = LONG(WINAPI*)(const void*, const void*, SIZE_T, const LARGE_INTEGER*);
using WakeAddrFn = void(WINAPI*)(const void*);
WaitAddrFn gWaitAddr = nullptr;
WakeAddrFn gWakeAddr = nullptr;
bool FutexBaton() {
    static const bool on = [] {
        if (const char* e = DevEnv("MGO1_BATON_EVENTS")) if (atoi(e) != 0) return false;
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        gWaitAddr = nt ? (WaitAddrFn)(void*)GetProcAddress(nt, "RtlWaitOnAddress") : nullptr;
        gWakeAddr = nt ? (WakeAddrFn)(void*)GetProcAddress(nt, "RtlWakeAddressSingle") : nullptr;
        return gWaitAddr && gWakeAddr;
    }();
    return on;
}
void BatonSet(Baton& b) { InterlockedExchange(&b.v, 1); gWakeAddr((const void*)&b.v); }
bool BatonWait(Baton& b, DWORD ms) {
    LARGE_INTEGER to; to.QuadPart = -(LONGLONG)ms * 10000;
    for (;;) {
        if (InterlockedExchange(&b.v, 0)) return true;
        const LONG zero = 0;
        if (gWaitAddr((const void*)&b.v, &zero, sizeof(LONG), ms == INFINITE ? nullptr : &to) == 0x102) return InterlockedExchange(&b.v, 0) != 0;
    }
}
#else                                                  // (the player's build, the POSIX layer: the events)
bool FutexBaton() { return false; }
void BatonSet(Baton&) {}
bool BatonWait(Baton&, DWORD) { return true; }
#endif
void GoSet(EeThread* t) { if (FutexBaton() && t->id > 0 && t->id < 256) BatonSet(gGoOf[t->id]); else SetEvent(t->go); }
void GoWait(int id) {
    if (FutexBaton() && id > 0 && id < 256) BatonWait(gGoOf[id], INFINITE);
    else WaitForSingleObject(Thr(id)->go, INFINITE);
}
void BackSet() { if (FutexBaton()) BatonSet(gBackB); else SetEvent(gBack); }
bool BackWait(DWORD ms) { return FutexBaton() ? BatonWait(gBackB, ms) : WaitForSingleObject(gBack, ms) != WAIT_TIMEOUT; }
void PerfPs2Resumed();             // perf.h: where a PS2 thread runs, once a VBlank
void GiveBack() {                  // called on a PS2 thread: return the baton and wait for it
    const int id = gCur;
    BackSet();
    GoWait(id);
    PerfPs2Resumed();
}
long long gSeq = 0;
void NativeSafePoint();
// native: a thread made ready goes behind the others of its priority; one of higher priority
// than the running thread runs at once (the running one stays first of its own)
void NativeReady(EeThread* t) { if (t) { t->state = 2; t->seq = ++gSeq; } }
void NativePreempt() {
    if (!gNative || gInIrq) return;
    EeThread* cur = Thr(gCur);
    if (!cur || cur->state != 1) return;
    for (auto& t : gThreads)
        if (t.state == 2 && t.prio < cur->prio) { const int id = cur->id; cur->state = 2; GiveBack(); Thr(id)->state = 1; return; }
}
DWORD WINAPI ThreadBody(LPVOID p) {
    EeThread* t = Thr((int)(intptr_t)p);
    GoWait(t->id);
    t = Thr((int)(intptr_t)p);
    Log("thread %d starts at %08x (arg %08x, stack %08x+%#x, gp %08x)", t->id, t->entry, t->arg, t->stack, t->size, t->gp);
    static thread_local Ctx c;
    memset(&c, 0, sizeof(c));
    gActive = &c;
    static thread_local Budget slice{200000, nullptr, &GiveBack, 200000};   // a busy PS2 thread is preempted
    static thread_local Budget safe{20000, nullptr, &NativeSafePoint, 20000};   // native: timed events checked
    ps2::BudgetSet(gNative ? &safe : &slice);
    c.r[29] = t->stack + t->size - 0x10;
    c.r[28] = t->gp;
    c.r[4] = t->arg;
    const int id = t->id;
    Call(c, t->entry);
    t = Thr(id);
    Log("thread %d returned", t->id);
    t->state = 5;
    BackSet();
    return 0;
}
// run ready PS2 threads until every one waits (called on the game thread)
void Schedule(int maxSlices) {
    for (int n = 0; n < maxSlices; ++n) {
        EeThread* best = nullptr;
        for (auto& t : gThreads) if (t.id != 1 && t.state == 2 && (!best || t.prio < best->prio)) best = &t;
        if (!best) return;
        int prev = gCur;
        gCur = best->id; gRingThread = (unsigned)gCur;
        best->state = 1;
        ++gSlices;
        if (gSlices < 40 || gSlices % 500 == 0) Log("schedule: thread %d runs (slice %lld)", best->id, gSlices);
        if (gSlices == 2000 || gSlices == 4000) { DumpRing("slice"); for (auto& t : gThreads) if (t.id > 1) DumpRingThread(t.id, "slice"); }
        GoSet(best);
        if (!BackWait(5000)) {
            Log("schedule: thread %d did not give the baton back in 5 s (last call %08x from %08x)", best->id, gLastCall, gLastCaller);
            BackWait(INFINITE);
        }
        if (best->state == 1) best->state = 2;       // yielded without blocking: still ready
        gCur = prev; gRingThread = (unsigned)gCur;
    }
}
void Block(int state) {            // the running PS2 thread blocks
    EeThread* t = Thr(gCur);
    if (!t) return;
    if (t->id == 1 && !gNative) {  // the game thread cannot block: run the others until woken
        t->state = state;
        for (int k = 0; k < 256 && t->state == state; ++k) Schedule(1);
        if (t->state == state) {
            static int told = 0;
            if (told++ < 3) Log("game thread would block (state %d) with nothing left to run: released; calls before:%s", state, RecentCalls(16).c_str());
            else Log("game thread would block (state %d) with nothing left to run: released", state);
        }
        t->state = 1;
        return;
    }
    t->state = state;
    GiveBack();
    t->state = 1;
}
void Wake(EeThread* t) { if (t && (t->state == 4)) t->state = 2; }

// ---- cdbios: the game's own CD/DVD manager (cdbios.irx in MODNCD.IPK), driven from the EE
// by SIF commands (base 0x17DCE8 / 0x17DDC8, cid 2) as MGS2's libfs/cdbios.c describes it:
// 1 read, 2 read sync (the EE's answer to a read callback), 3 seek, 4 stop, 5 get time,
// 6 search file, 7 change parameters, 8 init, 11 (MGS3: before the archives are opened).
// The IOP writes the status block the EE polls (pointer at 0x28DB40, block 0x28DB80): +0
// status (0 idle), +0x10 result, read channel c (0..2) at +0x20 + 0x20 c {pos, ee_buffer,
// read_size, intr_size, total_size, status}, user callbacks at +0x80 + 4 c with their
// argument at +0x8C + 4 c. A position is (file << 24) | sector, as the HD mode of MGS2's
// cdbios numbers the files; the files are the archives in the image folder (STAGE_N.DAT,
// SLOT_N.DAT). Reads complete at once, callbacks included (the EE's IOP handler 0x17D638).
// The voices of another version (docs/LANGUAGES.md). VOX_2.DAT holds the same clips in each
// version, in the same order (a few under another name), at other places and of other lengths:
// a clip starts at a sector (16-byte records, then the 0x20-byte header 10 00 00 00 ... and the
// VAG header with the clip's name). With Platform::voicesFrom (or MGO1_VOICES_FROM: the other
// version's folder or disc image), a read of this version's archive is served from the other's:
// a read that goes on from where a read ended (a stream's next chunk) from where that one ended
// there, while the other's clip has data left; any other read from the same place of the other's
// clip. The clips are paired by their order when both archives hold as many, else by name.
struct VoxSide { std::vector<long long> at; std::vector<std::string> name; long long size = 0; };
bool VoxScan(FILE* f, long long base, long long size, VoxSide& out) {
    const size_t kBlock = 1 << 22, kKeep = 96;           // kept from the block before: the headers across blocks
    std::vector<uint8_t> b(kBlock + kKeep, 0);
    long long pos = 0;                                   // the file offset of b[kKeep]
    while (pos < size) {
        const size_t n = (size_t)std::min<long long>(kBlock, size - pos);
        if (_fseeki64(f, base + pos, SEEK_SET) != 0 || fread(b.data() + kKeep, 1, n, f) != n) return false;
        const size_t have = kKeep + n;
        for (size_t i = kKeep - 48; i + 48 <= have; ++i) {   // the last 48 bytes of a block: with the next one
            const uint8_t* p = (const uint8_t*)memchr(b.data() + i, 'V', have - 48 - i + 1);
            if (!p) break;
            i = (size_t)(p - b.data());
            if (p[1] != 'A' || p[2] != 'G' || (p[3] != '1' && p[3] != '2' && p[3] != 'p')) continue;
            if (i < 0x20 || p[-0x20] != 0x10 || p[-0x1f] || p[-0x1e] || p[-0x1d]) continue;
            const long long at = (pos + (long long)i - (long long)kKeep - 0x20) & ~2047LL;
            if (at < 0 || (!out.at.empty() && out.at.back() >= at)) continue;   // one clip a sector
            out.at.push_back(at);
            out.name.emplace_back((const char*)p + 32, strnlen((const char*)p + 32, 16));
        }
        memmove(b.data(), b.data() + n, kKeep);
        pos += (long long)n;
    }
    out.size = size;
    return !out.at.empty();
}
struct VoxSwap {
    std::atomic<int> state{0};                           // 1 building, 2 ready, -1 none
    FILE* f = nullptr; long long base = 0, size = 0;     // the other version's archive
    std::string from;
    VoxSide mine, theirs;
    std::vector<int> pair;                               // this version's clip -> the other's (-1: none)
    bool byOrder = false;
    struct Stream { long long mine = -1, theirs = 0, end = 0; unsigned long long used = 0; };
    Stream streams[4];
    unsigned long long reads = 0;
    std::mutex lock;
};
struct CdFile { std::string name; FILE* f = nullptr; long long base = 0, size = 0; std::shared_ptr<VoxSwap> vox; };
std::vector<CdFile> gCd(1);                         // ids from 1
std::mutex gCdLock;                                 // the files' reads (the EE's cdbios, the IOP's sound streams)
// the other archive's offset for a read of this version's VOX_2.DAT (-1: this one's data)
long long VoxOther(VoxSwap& v, long long at, size_t n) {
    std::lock_guard<std::mutex> g(v.lock);
    const unsigned long long now = ++v.reads;
    for (auto& s : v.streams)
        if (s.used && s.mine == at && s.theirs < s.end) {
            const long long t = s.theirs;
            s.mine += (long long)n; s.theirs += (long long)n; s.used = now;
            return t;
        }
    auto it = std::upper_bound(v.mine.at.begin(), v.mine.at.end(), at);
    if (it == v.mine.at.begin()) return -1;
    const size_t k = (size_t)(it - v.mine.at.begin()) - 1;
    const int j = v.pair[k];
    if (j < 0) return -1;
    const long long t = v.theirs.at[j] + (at - v.mine.at[k]);
    const long long end = (size_t)j + 1 < v.theirs.at.size() ? v.theirs.at[j + 1] : v.theirs.size;
    VoxSwap::Stream* s = &v.streams[0];
    for (auto& x : v.streams) if (x.used < s->used) s = &x;
    s->mine = at + (long long)n; s->theirs = t + (long long)n; s->end = end; s->used = now;
    static int said = 0;
    if (at == v.mine.at[k] && said < 40) {
        ++said;
        Log("voices: %s from %s (%s, sector %#llx -> %#llx)", v.mine.name[k].c_str(), v.from.c_str(),
            v.theirs.name[j] == v.mine.name[k] ? "same name" : ("there " + v.theirs.name[j]).c_str(), at / 2048, t / 2048);
    }
    return t;
}
// VOX_2.DAT opened: the other version's archive indexed on a thread of its own (a read before it is
// done plays this version's voice)
void VoxOpen(CdFile& cf) {
    const char* env = DevEnv("MGO1_VOICES_FROM");
    const std::string from = env ? env : P.voicesFrom ? P.voicesFrom : "";
    if (from.empty() || cf.name != "VOX_2.DAT" || !cf.f) return;
    DiscSpan span;
    if (!DiscFindIn(from, "MGS\\VOX_2.DAT", span)) { Log("voices: no VOX_2.DAT in %s: this version's voices", from.c_str()); return; }
    FILE* f = nullptr;
    if (fopen_s(&f, span.host.c_str(), "rb") != 0 || !f) { Log("voices: %s not opened: this version's voices", span.host.c_str()); return; }
    auto v = std::make_shared<VoxSwap>();
    v->f = f; v->base = span.base; v->size = span.size; v->from = from; v->state = 1;
    cf.vox = v;
    FILE* mineF = nullptr;
    DiscSpan mineSpan;
    if (!DiscFind("MGS\\VOX_2.DAT", mineSpan) || fopen_s(&mineF, mineSpan.host.c_str(), "rb") != 0 || !mineF) { v->state = -1; return; }
    std::thread([v, mineF, mineSpan]() {
        const ULONGLONG t0 = GetTickCount64();
        const bool ok = VoxScan(mineF, mineSpan.base, mineSpan.size, v->mine) && VoxScan(v->f, v->base, v->size, v->theirs);
        fclose(mineF);
        if (!ok) { Log("voices: the clips of the two VOX_2.DAT not found: this version's voices"); v->state = -1; return; }
        v->pair.assign(v->mine.at.size(), -1);
        int paired = 0, sameName = 0;
        if (v->mine.at.size() == v->theirs.at.size()) {
            v->byOrder = true;
            for (size_t k = 0; k < v->pair.size(); ++k) { v->pair[k] = (int)k; ++paired; sameName += v->mine.name[k] == v->theirs.name[k]; }
        } else {
            std::map<std::string, int> there;
            for (size_t j = 0; j < v->theirs.name.size(); ++j) there.emplace(v->theirs.name[j], (int)j);
            for (size_t k = 0; k < v->pair.size(); ++k) {
                auto it = there.find(v->mine.name[k]);
                if (it != there.end()) { v->pair[k] = it->second; ++paired; ++sameName; }
            }
        }
        Log("voices: VOX_2.DAT of %s: %d of this version's %zu clips paired with its %zu (%s, %d of the same name), in %llu ms",
            v->from.c_str(), paired, v->mine.at.size(), v->theirs.at.size(), v->byOrder ? "by their order" : "by name", sameName,
            (unsigned long long)(GetTickCount64() - t0));
        v->state = paired ? 2 : -1;
    }).detach();
}
// a read of an archive: n bytes at `at` into dst, the rest zeros; the bytes read
size_t CdFileRead(CdFile& cf, long long at, uint8_t* dst, size_t n) {
    if (cf.vox && cf.vox->state.load(std::memory_order_acquire) == 2) {
        VoxSwap& v = *cf.vox;
        const long long t = VoxOther(v, at, n);
        if (t >= 0) {
            size_t got = 0;
            if (t < v.size && _fseeki64(v.f, v.base + t, SEEK_SET) == 0)
                got = fread(dst, 1, (size_t)std::min<long long>((long long)n, v.size - t), v.f);
            if (got < n) memset(dst + got, 0, n - got);
            return n;
        }
    }
    size_t got = 0;
    if (at < cf.size && _fseeki64(cf.f, cf.base + at, SEEK_SET) == 0)
        got = fread(dst, 1, (size_t)std::min<long long>((long long)n, cf.size - at), cf.f);
    if (got < n) memset(dst + got, 0, n - got);
    if (cf.name == "STAGE_N.DAT" && ExtraMapsRead(at, dst, n)) got = n;   // an extra map's stage (docs/EXTRA_MAPS.md)
    return got;
}
std::atomic<bool> gSoundPulled{false};              // the front end's sound output takes the IOP's mix
uint32_t gCdS = 0;                                  // the status block (physical)
uint32_t gCdPacket = 0;                             // the IOP's callback command, built for 0x17D638
// a disc path from the game's name for it ("cdrom0:\\MGS\\NET\\STAGE_N.DAT;1")
std::string DiscRel(const std::string& path) {
    const size_t colon = path.find(':');
    std::string rel = colon == std::string::npos ? path : path.substr(colon + 1);
    for (auto& ch : rel) if (ch == '/') ch = '\\';
    while (!rel.empty() && rel[0] == '\\') rel.erase(0, 1);
    if (const size_t v = rel.find(';'); v != std::string::npos) rel.resize(v);
    return rel;
}
int CdOpen(std::string path) {
    size_t k = path.find_last_of("\\/:");
    std::string base = k == std::string::npos ? path : path.substr(k + 1);
    if (size_t v = base.find(';'); v != std::string::npos) base.resize(v);
    for (auto& ch : base) ch = (char)toupper((unsigned char)ch);
    for (size_t i = 1; i < gCd.size(); ++i) if (gCd[i].name == base) return (int)i;
    DiscSpan span;
    FILE* f = DiscOpen(DiscRel(path), span);
    if (!f) {
        CdFile cf; cf.name = base;                  // an archive this install does not have: a valid position,
        gCd.push_back(cf);                          // its reads are refused (the EE retries a failed search forever)
        return -((int)gCd.size() - 1);
    }
    CdFile cf; cf.name = base; cf.f = f; cf.base = span.base; cf.size = span.size;
    VoxOpen(cf);
    gCd.push_back(cf);
    return (int)gCd.size() - 1;
}
void CdCallback(uint32_t ch) {
    if (!gCdPacket) gCdPacket = Alloc(0x40);
    W32(gCdPacket + 0xC, (ch << 16) | 1);
    RunPs2(V(0x17D638), gCdPacket, gCdS | 0x20000000);
}
void CdRead(uint32_t ch) {
    const uint32_t rec = gCdS + 0x20 + 0x20 * ch;
    static int logs = 0;
    for (int guard = 0; R32(rec + 8) < R32(rec + 0x10) && guard < 1000000; ++guard) {
        const uint32_t pos = R32(rec), ptr = R32(rec + 4), done = R32(rec + 8), total = R32(rec + 0x10);
        const int32_t intr = (int32_t)R32(rec + 0xC);
        const uint32_t n = intr > 0 ? std::min((uint32_t)intr, total - done) : total - done;
        const uint32_t id = pos >> 24, sector = pos & 0xffffff, buf = ptr & 0x01ffffff;
        if (buf + n > 0x02000000) {
            Log("cdbios: read of file %u sector %#x (%#x bytes to %08x) outside the PS2 space: refused", id, sector, n, ptr);
            break;
        }
        memset(gMem + buf, 0, n);
        if (ps2::gHeadless && (id >= gCd.size() || !gCd[id].f)) {
            // a dedicated host: an archive it does not have (the music BGM_2, the voices VOX_2) reads
            // as done, without the stream's callback: that callback walks the zeros as stream blocks
            // until the run's call budget stops it (about 350 ms each time the music starts)
            static std::map<uint32_t, int> skipped;
            if (skipped[id]++ < 3)
                Log("cdbios: read of %s sector %#x: headless, no such archive, done without its callback",
                    id && id < gCd.size() ? gCd[id].name.c_str() : "no archive", sector);
            W32(rec + 8, total);
            W32(rec + 4, ptr + (total - done));
            if (intr > 0) W32(rec + 0xC, 0);
            break;
        }
        if (id >= gCd.size() || !gCd[id].f) {
            // an archive this install does not have (the PS2 streams: BGM_2, VOX_2 ...; the PC
            // engine plays the sound) or no archive at all: zeros, and the read completes
            static std::map<uint32_t, int> absent;
            if (absent[id]++ < 3)
                Log("cdbios: read of %s sector %#x (%#x bytes to %08x): no such archive here, zeros",
                    id && id < gCd.size() ? gCd[id].name.c_str() : "no archive", sector, n, ptr);
        } else {
            // in pieces of 64 KB, the lock taken for each: the IOP's own reads (the sound driver's) wait one piece at
            // most, not a whole stage's load (Snake 1 October: the music crackled while a map loaded)
            const long long at = (long long)sector * 2048;
            size_t got = 0;
            for (uint32_t k = 0; k < n; k += 0x10000) {
                const uint32_t piece = std::min<uint32_t>(0x10000, n - k);
                std::lock_guard<std::mutex> cl(gCdLock);
                got += CdFileRead(gCd[id], at + k, gMem + buf + k, piece);
            }
            if (logs < 400) { ++logs; Log("cdbios: read %s sector %#x, %#x bytes to %08x (%zu read)%s", gCd[id].name.c_str(), sector, n, ptr, got, intr > 0 ? ", chunk" : ""); }
        }
        W32(rec + 8, done + n);
        W32(rec + 4, ptr + n);
        if (intr > 0) W32(rec + 0xC, (uint32_t)(intr - (int32_t)n));
        W32(rec, pos + (n + 2047) / 2048);
        if (intr > 0 || done + n == total) CdCallback(ch);
    }
    W32(rec + 0x14, 0);
}
// ---- cross-region sessions. The EU base signs and encrypts its session datagrams with
// other constants than US and JP (bootstrap payload key 0xA7103C2F instead of 0xA7130C2F,
// MAC and peer-key constant 0x4B58DE69 instead of 0x48B5ED69: base 0x16F420, 0x16FD58,
// 0x170B48). A peer endpoint's region is learnt from the first bootstrap datagram whose
// MAC verifies with either set; while a peer is unknown, its connect datagrams alternate
// the two sets (NewHostJoin sends one every 3 frames), so a US or EU host answers one.
// A game of more than 8 players (docs/BIG_GAMES.md) speaks a third set only the PC programs know: a PS2 or
// PCSX2 program neither verifies its host's datagrams nor makes ones it takes, and never gets in. A game whose
// rotation holds maps 13 and up (docs/EXTRA_MAPS.md) speaks a set of its own: the PCs' constants with the
// signature of those maps' files mixed in, which a PC without them (or with other files for them) never speaks.
const uint32_t kKeyUs = 0xA7130C2F, kKeyEu = 0xA7103C2F, kMacUs = 0x48B5ED69, kMacEu = 0x4B58DE69;
const uint32_t kKeyPc = 0x50433D31, kMacPc = 0x3E6D6731;
// the sets: 0 US and JP, 1 EU, 2 the PCs', 3 and up the PCs' with extra maps (one per combination of maps)
struct DialectSet { uint32_t key, mac; std::string name; std::vector<int> maps; bool tsne = false; };   // tsne: TEAM SNEAKING's (tsne.h)
std::vector<DialectSet> gSets = {{kKeyUs, kMacUs, "US", {}}, {kKeyEu, kMacEu, "EU", {}}, {kKeyPc, kMacPc, "PCs'", {}}};
uint32_t SetKey(int r) { return gSets[(size_t)r].key; }
uint32_t SetMac(int r) { return gSets[(size_t)r].mac; }
const char* SetName(int r) { return gSets[(size_t)r].name.c_str(); }
// the set of a combination of extra maps (ids sorted), made on first use; 2 (the PCs') when a map is not here
int MapsSet(std::vector<int> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.empty()) return 2;
    for (size_t r = 3; r < gSets.size(); ++r) if (gSets[r].maps == ids && !gSets[r].tsne) return (int)r;
    const uint32_t sig = ExtraMapsSignature(ids);
    if (!sig) return 2;
    std::string name = "PCs' with the maps";
    for (int id : ids) name += " " + std::to_string(id);
    gSets.push_back({kKeyPc ^ sig, kMacPc ^ (sig * 0x9E3779B1u + 0x632BE5ABu), name, ids});
    return (int)gSets.size() - 1;
}
bool gBigGame = false;                               // a game of the PCs' dialect: more than 8 players, or maps 13 and up (BigGameOn)
int gGameSet = 2;                                    // the set of that game: 2 the PCs', 3 and up with its extra maps
int gJoinSet = -1;                                   // the game chosen to join (JoinChosen): 2 a big one, -1 not known
// the sets of the extra maps here: a set for each combination of them (every one when 3 maps or fewer, else each
// map alone and all of them)
std::vector<int> ExtraMapSets() {
    std::vector<int> ids, sets;
    for (auto& m : ExtraMaps()) if (m.ready) ids.push_back(m.id);
    const size_t n = ids.size();
    if (!n) return sets;
    if (n <= 3) {
        for (uint32_t bits = 1; bits < (1u << n); ++bits) {
            std::vector<int> one;
            for (size_t i = 0; i < n; ++i) if (bits & (1u << i)) one.push_back(ids[i]);
            sets.push_back(MapsSet(one));
        }
    } else {
        for (int id : ids) sets.push_back(MapsSet({id}));
        sets.push_back(MapsSet(ids));
    }
    return sets;
}
// the sets a joiner's connects go in, in turn, to a host not known yet: a big game chosen (more than 8 players in
// the host list) the PCs' ones; else both regions' too (a lobby server that shows 8 players at most for every game
// tells nothing), the PCs' with each combination of the extra maps here last; each PCs' set followed by its TEAM
// SNEAKING one
int TsneSet(int base);
bool TsneInRotation(uint32_t rules, uint32_t maps);
bool BcInRotation(uint32_t rules, uint32_t maps);       // BASE CAPTURE (basecap.h): the same PC-only dialect
uint8_t BcListMark(const uint8_t* rules, const uint8_t* maps);   // BASE CAPTURE (basecap.h): the host list's mark
uint8_t TsneListMark(const uint8_t* rules, const uint8_t* maps);   // TEAM SNEAKING's mark in the host list (tsne.h)
bool SetHasMaps(int r) { return r >= 0 && (size_t)r < gSets.size() && !gSets[(size_t)r].maps.empty(); }
int gJoinHintSet = -1;                               // open list: the set the host list tells for the game chosen (openfix.h)
uint8_t MapsListHint(const uint8_t* maps, bool big); void JoinHintFrom(uint8_t opts);
void LastGameTold(uint32_t st, const uint8_t* maps, const uint8_t* rules, uint8_t most);   // open list (openfix.h)
std::vector<int> JoinCandidates(int own) {
    std::vector<int> v;
    if (gJoinHintSet >= 0) v.push_back(gJoinHintSet);  // open list: the host list's hint first
    if (gJoinSet != 2) { v.push_back(own); v.push_back(1 - own); }
    v.push_back(2);
    v.push_back(TsneSet(2));                           // TEAM SNEAKING's games (tsne.h): each set's right after it
    for (int r : ExtraMapSets()) { v.push_back(r); v.push_back(TsneSet(r)); }
    return v;
}
void BigGameOn(const char* why);
bool IsSessionHost();
// web documents asked under another name (NativeScriptServers): an HTTP request's path rewritten
std::vector<std::pair<std::string, std::string>> gWebDocAlias;
std::map<uint64_t, int> gPeerRegion;                 // (address << 16 | port) -> 0 US or JP, 1 EU, 2 the PCs' (a big game)
uint64_t gLastRecvPeer = 0;                          // the source of the last datagram received (recvfrom)
uint64_t gLastRecvTick = 0;                          // when (GetTickCount64)
std::map<uint64_t, ULONGLONG> gPeerRxAt;              // (v150-host beta 18) each peer's last datagram (GetTickCount64): JoinerHold
void PeerHeard(uint64_t peer);                        // (JoinerHold, below)
std::string gErrText;                               // (v141 beta 12) the lobby's last message with an error code, its frame
long long gErrFrame = -1;
std::map<uint32_t, ULONGLONG> gUdpPeers;               // UDP destinations of the game (IPv4, network order) -> last send
std::mutex gUdpPeersLock;
std::map<uint64_t, uint32_t> gRegionTry;             // peer -> connect datagrams built while its set is unknown
std::map<uint64_t, ULONGLONG> gPeerBootAt;            // a host: a peer's last bootstrap datagram received (SessionKeys)
void PeerJoinsAgain(uint64_t peer, ULONGLONG gap);    // (the game channels' families, below)
int gDatagramRegion = -1;                            // the set of the datagram being built (-1: the program's own)
uint64_t PeerOf(uint32_t s) { return ((uint64_t)R32(s + 0x14) << 16) | R16(s + 0x12); }
int PeerRegion(uint64_t p) { auto it = gPeerRegion.find(p); return it == gPeerRegion.end() ? -1 : it->second; }
std::string PeerText(uint64_t p);
std::string PeerText(uint64_t p) {
    char b[32]; uint32_t a = (uint32_t)(p >> 16);
    snprintf(b, sizeof b, "%u.%u.%u.%u:%u", a & 255, (a >> 8) & 255, (a >> 16) & 255, a >> 24, (unsigned)(p & 0xffff));
    return b;
}
bool Ps2Sessions() {                                 // simulation: this instance's sessions a PS2's (its own set, no big game)
    static const bool ps2 = getenv("SIM_PS2_SESSIONS") != nullptr;
    return ps2;
}
// simulation (v141 beta 11): SIM_PS2_FAMILY1, this instance an unpatched EU or KR PS2: its game objects' channels at
// 0x70..0x80 as those programs open them (moved at their opening, as a big game's); with SIM_REGION_EU the PAL disc's
// dialect, with SIM_PS2_SESSIONS the Korean disc's (the US one)
bool SimFamily1() { static const bool on = DevEnv("SIM_PS2_FAMILY1") != nullptr; return on; }
// a PS2's program: no record moved nor put in another protocol on its side, no dialect learnt, no big game
bool SimPlainPs2() { static const bool on = Ps2Sessions() || DevEnv("SIM_REGION_EU") || SimFamily1(); return on; }
#include "netfix.h"                                    // netcode (v141): joins of PS2 and PCSX2 hosts, the link's tests
bool SessionKeys(Ctx& c, uint32_t addr) {
    if (Ps2Sessions()) return false;
    static const bool localEu = getenv("SIM_REGION_EU") != nullptr;   // simulation: this instance plays an unpatched EU PS2
    if (localEu && gUsProgram) {
        if ((addr == 0x16ef00 || addr == 0x16efa8) && (uint32_t)c.r[7] == kKeyUs) c.r[7] = kKeyEu;
        if (addr == 0x16f050 && (uint32_t)c.r[8] == kMacUs) c.r[8] = kMacEu;
        if (addr == 0x170b48) { W32((uint32_t)c.r[4] + 4, (uint32_t)c.r[5]); W32((uint32_t)c.r[4] + 8, (uint32_t)c.r[5] ^ kMacEu); return true; }
        return false;
    }
    // the program's own dialect (the EU program's is the EU one), the other region's, the PCs' of a big game;
    // a peer's region (0 US or JP, 1 EU, 2 the PCs') learnt from its datagrams
    const bool mineEu = !gUsProgram && !strcmp(ps2::SelectedVersion(), "EU");
    const int own = mineEu ? 1 : 0;
    if (addr == 0x16ef00 || addr == 0x16efa8) {        // payload encrypt / decrypt (session, buffer, length, key)
        if ((uint32_t)c.r[7] != SetKey(own)) return false;
        const uint32_t sess = (uint32_t)c.r[4];
        int r;
        if (addr == 0x16ef00) {
            r = PeerRegion(PeerOf(sess));
            if (r < 0 && IsSessionHost()) r = gBigGame ? gGameSet : own;   // a host: its game's set
            if (r < 0) {                                // a joiner's connects to a host not known yet: each set in turn
                uint32_t& k = gRegionTry[PeerOf(sess)];  // (a big game chosen: the PCs' ones alone)
                if (!k && gBigGame && gPageNt && gJoinSet < 0) {   // another join from the lobby: that game's set not known yet
                    gBigGame = false; gGameSet = 2;
                    Log("session: a join to a host not known yet: the game's own dialects and channels until it answers");
                }
                const std::vector<int> cands = JoinCandidates(own);
                r = cands[k % cands.size()];
                ++k;
                NetBootBuilt(PeerOf(sess), r, own);     // netcode: copies in both regions' dialects (netfix.h)
            }
            gDatagramRegion = r;
        } else { r = PeerRegion(gLastRecvPeer); if (r < 0) r = gBigGame ? gGameSet : own; }
        c.r[7] = SetKey(r);
        return false;
    }
    if (addr == 0x170b48) {                            // the peer's key: +4 key, +8 key ^ the MAC constant
        const uint32_t sess = (uint32_t)c.r[4], key = (uint32_t)c.r[5];
        const int r = PeerRegion(PeerOf(sess));
        static const bool keysLog = getenv("SIM_KEYS_LOG") != nullptr;
        if (keysLog) Log("keys: session key of %s set, its set %s", PeerText(PeerOf(sess)).c_str(), r < 0 ? "not known" : SetName(r));
        if (r < 0 || r == own) return false;
        W32(sess + 4, key); W32(sess + 8, key ^ SetMac(r));
        return true;
    }
    if (addr != 0x16f050 || (uint32_t)c.r[8] != SetMac(own)) return false;   // MAC (session, out, buffer, length, key t0)
    // the call's site: the send function's (0x16F420, its call at 0x16F7C8) or the receive function's
    // (0x16FD58, at 0x170790); the JP and JPS programs' send function differs inside (no address of that
    // call there): known by its function
    const uint32_t ra = (uint32_t)c.r[31];
    auto site = [&](uint32_t us, uint32_t fn) { const uint32_t v = V(us); return v ? ra == v : !gUsProgram && ps2::UsFnOf(ra) == fn; };
    if (site(0x16f7d0, 0x16f420)) {                    // a bootstrap datagram sent
        if (gDatagramRegion >= 0) c.r[8] = SetMac(gDatagramRegion);
        static const bool keysLog = getenv("SIM_KEYS_LOG") != nullptr;
        if (keysLog) Log("keys: %u bytes sent to %s in the %s set", (uint32_t)c.r[7], PeerText(PeerOf((uint32_t)c.r[4])).c_str(),
                         SetName(gDatagramRegion >= 0 ? gDatagramRegion : own));
        return false;
    }
    if (!site(0x170794, 0x16fd58)) return false;          // a bootstrap datagram received: which set
    ps2::Fn f = ps2::FindFn(V(addr));
    if (!f) return false;
    const Ctx saved = c;
    const uint32_t out = (uint32_t)c.r[5], buf = (uint32_t)c.r[6], len = (uint32_t)c.r[7];
    auto verifies = [&] { return !memcmp(gMem + (out & 0x01ffffff), gMem + ((buf + len) & 0x01ffffff), 10); };
    // a peer's region is the one of its first datagram that verifies, then kept: a PC joiner's connect
    // datagrams go in each set in turn, and one of another set must not verify once both ends run a
    // session in one of them. The sets taken from a peer not known yet: a big game's host the PCs' alone;
    // a host of a game of 8 its own and the other region's (not the PCs': a PC joiner of its game would
    // take it for a big one); a joiner all three (its host's tells whether the game is a big one)
    static const bool keysLog = getenv("SIM_KEYS_LOG") != nullptr;   // tests: each bootstrap datagram's check logged
    // (v150-host beta 14) a host: a peer's bootstrap datagram after 5 s without any (a join's connects come every 3
    // frames) is another join from that address and port, perhaps with another disc: its dialect and its game channels'
    // place learnt again (Snake, 24 October: a PAL PS2 ended on 1059 after Korean games from the same connection, its
    // connects checked in the US dialect alone)
    if (IsSessionHost()) {
        const ULONGLONG now = GetTickCount64();
        const auto at = gPeerBootAt.find(gLastRecvPeer);
        if (at != gPeerBootAt.end() && now - at->second > 5000) PeerJoinsAgain(gLastRecvPeer, now - at->second);
        gPeerBootAt[gLastRecvPeer] = now;
    }
    const int known = PeerRegion(gLastRecvPeer);
    if (known >= 0) {
        c.r[8] = SetMac(known); f(c);
        const bool ok = verifies();
        if (keysLog) Log("keys: %u bytes from %s (known %s): %s", len, PeerText(gLastRecvPeer).c_str(), SetName(known), ok ? "verifies" : "does not verify");
        NetJoinGot(gLastRecvPeer, ok);                     // netcode: the join's outcome (netfix.h)
        return true;
    }
    std::vector<int> order;
    if (gBigGame) order.push_back(gGameSet);
    else if (IsSessionHost()) order = {own, 1 - own};
    else order = JoinCandidates(own);
    if (keysLog) Log("keys: %u bytes from %s (not known): %zu set(s) tried", len, PeerText(gLastRecvPeer).c_str(), order.size());
    for (int r : order) {
        c = saved; c.r[8] = SetMac(r);
        f(c);
        if (!verifies()) continue;
        gPeerRegion.emplace(gLastRecvPeer, r);
        NetJoinGot(gLastRecvPeer, true); NetJoinLearnt(gLastRecvPeer, r);   // netcode: the join's outcome (netfix.h)
        if (r != own) Log("session: peer %s speaks the %s dialect", PeerText(gLastRecvPeer).c_str(), SetName(r));
        if (r >= 2 && !gBigGame) { gGameSet = r; BigGameOn(r == 2 ? "its host speaks the PCs' dialect" : gSets[(size_t)r].tsne ? "its host speaks TEAM SNEAKING's dialect" : "its host speaks the PCs' dialect with extra maps"); }
        return true;
    }
    if (keysLog) Log("keys: none of the sets verifies");
    NetJoinGot(gLastRecvPeer, false);                  // netcode: the join's outcome (netfix.h)
    c = saved; c.r[8] = SetMac(order[0]); f(c);   // none: the first set's result (the check fails; a big game's
    return true;                                       // host never takes a datagram in its program's own set)
}
// ---- game channels across versions (docs/VERSIONS.md). The US, JP and JPS programs open their
// game objects' channels at 0x60..0x70 (room state 0x62 ...; 0x70 the breakable windows of n021a and
// n024a, NewNetworkBrkWindowAll), the EU and KR programs at 0x70..0x80 (0x72 ..., the windows 0x80);
// the players' channels (0x40 + 3 x slot + 0..2), the base's (0, 1, 2) and the numbered ones
// (0x400..) are the same. A record: channel low byte, flags (low nibble: channel bits 8..11, 0x10 a
// sequence byte, 0x20 a 16-bit length), length, [sequence], data. A peer's wire family is learnt from
// the first game channel it sends that only one family has (0x70 is both's); before that it is the
// family of the players of the program's servers (the North American one for the Korean program).
// Records are moved to this program's channels on receipt, to the peer's before they are compressed
// and encrypted (0x178928, between the records and the cipher both ways).
std::map<uint64_t, int> gPeerChanFamily;             // peer -> 0: channels 0x60..0x70 (US, JP, JPS), 1: 0x70..0x80 (EU, KR)
// (v150-host beta 14) SessionKeys: a peer known from an earlier join joins again: its dialect and its game channels'
// place forgotten, learnt from its datagrams as a new peer's
void PeerJoinsAgain(uint64_t peer, ULONGLONG gap) {
    const int region = PeerRegion(peer);
    const auto fam = gPeerChanFamily.find(peer);
    if (region < 0 && fam == gPeerChanFamily.end()) return;
    Log("session: peer %s joins again, %llu s after its last connect: its dialect (%s) and its game channels' place (%s) learnt again",
        PeerText(peer).c_str(), (unsigned long long)(gap / 1000), region >= 0 ? SetName(region) : "not known",
        fam == gPeerChanFamily.end() ? "not known" : fam->second ? "the EU and KR programs'" : "the US, JP and JPS programs'");
    gPeerRegion.erase(peer);
    gPeerChanFamily.erase(peer);
    gNetProbe.erase(peer);                             // netcode: its place probed again (netfix.h)
}
// (v150-host beta 18) a dedicated host keeps a peer still loading past the game's two silence limits (joinhold.h,
// docs/DEDICATED_HOST.md "A joiner that loads slowly"). (1) 0x171008 ends a session (its state 1); called from 0x170E18 (the
// session's tick, v0 the record) at a reliable record's 11th time-out. (2) 0x1710D0(session, 30000) answers 0 when the
// session sent nothing for 30 s; the player's tick (0x175B60) then ends the player. A dedicated host's peer whose player
// entry (its +4 the session) is at 0x212 and that sent a datagram less than MGO1_JOINER_HOLD_S seconds ago (60; 0: the
// game's rules) is kept: (1) the record's count put back to 9 (the next time-out resends it, the one after asks again),
// (2) answered 1. Logged once a silence, with the time it was away when it answers (PeerHeard)
#include "joinhold.h"
struct HeldPeer { ULONGLONG since = 0; int times = 0; unsigned id = 0; };
std::map<uint64_t, HeldPeer> gHeldPeers;
void PeerHeard(uint64_t peer) {
    const ULONGLONG now = GetTickCount64();
    const auto h = gHeldPeers.find(peer);
    if (h != gHeldPeers.end()) {
        const auto at = gPeerRxAt.find(peer);
        Log("session: peer %s (P%u) answers again after %.1f s of silence, kept past the game's resend limit %d time%s",
            PeerText(peer).c_str(), h->second.id + 1, at == gPeerRxAt.end() ? 0.0 : (now - at->second) / 1000.0, h->second.times,
            h->second.times == 1 ? "" : "s");
        gHeldPeers.erase(h);
    }
    gPeerRxAt[peer] = now;
}
bool JoinerHold(Ctx& c, uint32_t addr) {
    const bool resend = addr == 0x171008, silence = addr == 0x1710d0;
    if ((!resend && !silence) || !gNative || !IsSessionHost()) return false;
    static const bool dedicated = DevEnv("MGO1_HOST_ONLY") || DevEnv("MGO1_HOST_SETTINGS");
    static const unsigned holdS = joinhold::HoldFromEnv(DevEnv("MGO1_JOINER_HOLD_S"));   // (v150-host beta 18, review: 0 to 600)
    if (!dedicated || !holdS) return false;
    if (resend) {                                                   // the resend limit's call alone (not a kick's, 0x172E24)
        const uint32_t ra = (uint32_t)c.r[31], v = V(0x170ebc);
        if (v ? ra != v : (gUsProgram || ps2::UsFnOf(ra) != 0x170e18)) return false;
    } else if ((uint32_t)c.r[5] != joinhold::kSilentMs) return false;   // the player's 30 s alone
    const uint32_t sess = (uint32_t)c.r[4], rec = (uint32_t)c.r[2], table = V(0x28B5F4);
    if (sess < 0x100000 || sess >= 0x02000000 || (silence && R8(sess) < 2)) return false;   // (2): a session ended stays so
    bool found = false; uint16_t state = 0; unsigned id = 0;
    for (uint32_t k = 0; table && k < 16 && !found; ++k) {
        const uint32_t e = table + 0x5C * k;
        if (R8(e) != 0xFF && R16(e + 2) && R32(e + 4) == sess) { found = true; state = R16(e + 2); id = R8(e); }
    }
    if (!found || state != joinhold::kLoading) return false;
    const uint64_t peer = PeerOf(sess);
    const ULONGLONG now = GetTickCount64();
    auto at = gPeerRxAt.find(peer);
    if (at == gPeerRxAt.end()) at = gPeerRxAt.emplace(peer, now).first;
    ULONGLONG silent = now - at->second;
    const auto held = gHeldPeers.find(peer);                        // (kept since: a peer heard of but never by the game)
    if (held != gHeldPeers.end() && now - held->second.since > silent) silent = now - held->second.since;
    const joinhold::Verdict verdict = joinhold::Decide(found, state, silent, holdS);
    if (verdict == joinhold::kKeep && silence) {                     // (2): alive; said once past the game's 30 s
        c.r[2] = 1;
        if (silent <= joinhold::kSilentMs) return true;
    }
    if (verdict == joinhold::kKeep && (silence || (rec >= 0x100000 && rec < 0x02000000))) {
        // (v150-host beta 18, review P2-3) three holds in all at most, whatever it sends meanwhile
        static std::map<uint64_t, joinhold::Episode> episodes;
        if (joinhold::EpisodeOver(episodes[peer], (unsigned long long)now, holdS)) {
            Log("session: peer %s (P%u, its entry %03x) kept %u s in all: ended at the game's %s (three holds, MGO1_JOINER_HOLD_S)",
                PeerText(peer).c_str(), id + 1, state, 3 * holdS, resend ? "resend limit" : "30 s without a datagram");
            gHeldPeers.erase(peer); episodes.erase(peer);
            return false;
        }
        HeldPeer& h = gHeldPeers[peer];
        if (!h.times++) {
            h.since = now; h.id = id;
            Log("session: peer %s (P%u, its entry %03x: its stage loading or waiting for the round) silent %.1f s: kept past the "
                "game's %s (until %u s of silence, MGO1_JOINER_HOLD_S)", PeerText(peer).c_str(), id + 1, state, silent / 1000.0,
                resend ? "resend limit" : "30 s without a datagram", holdS);
        }
        if (resend) W8(rec + 8, (uint8_t)joinhold::kCountKept);
        return true;
    }
    Log("session: peer %s (P%u, its entry %03x) silent %.1f s: ended at the game's %s (silent longer than MGO1_JOINER_HOLD_S)",
        PeerText(peer).c_str(), id + 1, state, silent / 1000.0, resend ? "resend limit" : "30 s without a datagram");
    gHeldPeers.erase(peer);
    return false;
}
int NativeChanFamily() {                             // the program's own place of its game objects' channels
    const char* v = ps2::SelectedVersion();
    return v && (!strcmp(v, "EU") || !strcmp(v, "KR")) ? 1 : 0;
}
int OwnChanFamily() { return gBigGame ? 1 : NativeChanFamily(); }   // a big game: all at 0x70..0x80 (BigGameChannel)
// the players' channels (0x40 + 3 x id + 0..2) end at 0x60 (10 players), in a big game at 0x70 (16)
uint32_t PlayersChanEnd() { return gBigGame ? 0x70 : 0x60; }
// ---- games of more than 8 players (docs/BIG_GAMES.md). Only PCs play them: their sessions speak the
// PCs' dialect (SessionKeys: a PS2 or PCSX2 program never gets in), and every program opens its game
// objects' channels at 0x70..0x80 (the EU and KR programs' own place; the US, JP and JPS ones moved there
// at the channel's opening, 0x174790), which leaves 0x40..0x6F to 16 players' channels. No record is
// moved between the two places then (every peer is a PC in that place). The host is in one from the
// game's creation (its maximum of players above 8: 0x35F1A8), a joiner from the game he chooses
// (JoinChosen) or else his host's first datagram that verifies in the PCs' dialect; the lobby's page
// ends it.
void BigGameOn(const char* why) {
    if (gBigGame) return;
    gBigGame = true;
    Log("session: a game of the PCs (%s): the %s dialect, the game objects' channels at 0x70..0x80%s", why, SetName(gGameSet),
        NativeChanFamily() ? "" : " (moved from 0x60..0x70)");
}
extern std::vector<std::pair<uint8_t, uint8_t>> gHostRotation;   // (a dedicated host's settings, below)
extern int gHostMaxPlayers;
void BigGameCreate(uint32_t menu) {                  // 0x35F1A8 (a0: ntmenu2's work): a game's creation
    if (menu < 0x100000 || menu + 0x300 >= 0x02000000) return;
    if (SimPlainPs2() && !getenv("MGO1_FORCE_ROTATION")) return;   // netcode: tests, a PS2 stand-in's rotation forced too
    if (const char* rot = getenv("MGO1_FORCE_ROTATION")) {   // tests: the rotation's pairs "rule,map,..." (maps 13 and up too)
        int v[32], n = 0;
        for (const char* q = rot; *q && n < 32;) { v[n++] = atoi(q); q = strchr(q, ','); if (!q) break; ++q; }
        for (int i = 0; i < 16; ++i) {
            W8(menu + 0x284 + i, (uint8_t)(2 * i + 1 < n ? v[2 * i] : 0));
            W8(menu + 0x294 + i, (uint8_t)(2 * i + 1 < n ? v[2 * i + 1] : 0));
        }
        Log("session: the rotation set to %s (MGO1_FORCE_ROTATION)", rot);
    }
    if (SimPlainPs2()) return;                         // netcode: tests, a PS2 stand-in has no dialect of the PCs
    if (!gHostRotation.empty() && !getenv("MGO1_FORCE_ROTATION")) {   // (v141) a dedicated host's settings file: its rotation
        for (int i = 0; i < 16; ++i) {                    // as written (the Create Game screens drop maps 13 and up, rules 5, 6)
            const bool on = i < (int)gHostRotation.size();
            W8(menu + 0x284 + i, on ? gHostRotation[(size_t)i].first : 0);
            W8(menu + 0x294 + i, on ? gHostRotation[(size_t)i].second : 0);
        }
        if (gHostMaxPlayers > 9) W8(menu + 0x2a7, (uint8_t)gHostMaxPlayers);   // (above 9: a game of the PCs, below)
        std::string t;
        for (const auto& pr : gHostRotation) t += (t.empty() ? "" : ", ") + std::to_string(pr.first) + ":" + std::to_string(pr.second);
        Log("host settings: the rotation %s and %u players at most at the game's creation", t.c_str(), R8(menu + 0x2a7));
    }
    if (R8(menu + 0x2a7) > 16) W8(menu + 0x2a7, 16);   // a dedicated host's one more (0x35F9A0): 16 entries in all
    // a Host Only game's maximum counts its host (the dedicated host, v141-host): a game of 9 is 8 players, a game of the
    // twelve maps and five rules every program joins (the PS2s and PCSX2 too), not a big game (NetPlayingCap, below)
    const uint32_t most = R8(menu + 0x2a7);
    std::vector<int> extra;                              // the rotation's maps 13 and up (docs/EXTRA_MAPS.md)
    for (int i = 0; i < 16; ++i) { const uint8_t m = R8(menu + 0x294 + i); if (!m) break; if (m > 12) extra.push_back(m); }
    const bool tsne = TsneInRotation(menu + 0x284, menu + 0x294) || BcInRotation(menu + 0x284, menu + 0x294);   // TEAM SNEAKING, BASE CAPTURE
    int set = extra.empty() ? 2 : MapsSet(extra);
    if (tsne) set = TsneSet(set);
    if (set != gGameSet && gBigGame) { gBigGame = false; }
    gGameSet = set;
    if (tsne) BigGameOn("its rotation holds TEAM SNEAKING");
    else if (!extra.empty()) BigGameOn(set > 2 ? "its rotation holds maps 13 and up" : "its rotation holds maps 13 and up not all here");
    else if (most > NetPlayingCap(R8(menu + 0xf7))) BigGameOn("its maximum of players");   // netcode: Host Only's one more (netfix.h)
    else if (gBigGame) { gBigGame = false; Log("session: a game of %u players at most: the game's own dialects and channels", most); }
}
// ---- maps 13 and up (docs/EXTRA_MAPS.md, extramaps.cpp). The game settings (configuration block 0, 0x28D528's
// first entry: its data at + 8) hold the rotation at key 2 (16 pairs {map, rule}) and the round at key 0x36; the
// game's logic reads them through 0x178490 and gets an extra map as the map of its stage (3 for City Under
// Siege's n003a), the stage then loads with the extra map's files
uint32_t ConfigData0() {
    const uint32_t e = R32(V(0x28D528)) & 0x01ffffff;
    return e >= 0x100000 && e < 0x02000000 ? R32(e + 8) & 0x01ffffff : 0;
}
int RoundMap() {                                       // the map of the round the settings name (its real number)
    const uint32_t d = ConfigData0();
    if (d < 0x100000 || d + 0x40 >= 0x02000000) return 0;
    const uint32_t i = R8(d + 0x36);
    return i < 16 ? R8(d + 2 + 2 * i) : 0;
}
int gLoadedMap = 0;                                    // the map (real number) of the stage loaded last
struct MinimapShown { uint32_t at = 0; int shown = 0, want = 0, held = 0; long long pass = 0, tried = -1000; };
MinimapShown gMinimap;                                 // the 2D map's floor shown (ExtraMinimapTick)
int RoundExtraMap(const char* page) {                   // the round's real map for an extra map's page (0: none)
    int real = RoundMap();
    if (real > 12 && !(gBigGame && SetHasMaps(gGameSet))) {   // not a game in the dialect of its maps: its pages as they are
        if (Once(((uint64_t)72 << 40) | (uint32_t)real)) Log("extra maps: map %d asked in a game not in the dialect of its maps: %s as it is", real, page);
        real = 0;
    }
    return real;
}
void ExtraStageLoad(Ctx& c) {                          // 0x101B28 (a0: the page's name): a page loads
    char nm[16] = {0};
    const uint32_t a = (uint32_t)c.r[4] & 0x01ffffff;
    for (uint32_t k = 0; k < 15 && a + k < 0x02000000; ++k) { nm[k] = (char)R8(a + k); if (!nm[k]) break; }
    const int number = StageMapNumber(nm);
    const bool rules = ExtraMapsRulesPage(nm);           // a rules page (the resident one): the round's map's own
    ExtraMapsStageLoad(nm, number || rules ? RoundExtraMap(nm) : 0, R32(V(0x2C5E14)));   // the resident area's kept top
    if (rules) return;
    gMinimap = MinimapShown();
    if (number) gLoadedMap = ExtraMapsLoaded() ? ExtraMapsLoaded() : number;
}
// 0x106CC0 (the stage manager's page to load next: the resident page's name when [0x2C5298] is set, else the
// stage's; link variables' + 0x14 and + 0x24): the rules page held is not the one the round's map needs (an extra
// map's own, or the stock one after it) while the rules stay the same, so the game would keep it: the resident
// set to load again first, as a change of rules does (0x106C40), the stage after it
void ExtraRulesCheck() {
    const uint32_t fl = V(0x2C5238), lv = R32(V(0x23A678)) & 0x01ffffff;
    if (lv < 0x100000 || lv + 0x40 >= 0x02000000 || R32(fl + 0x60)) return;
    char nm[16] = {0};
    for (uint32_t k = 0; k < 15; ++k) { nm[k] = (char)R8(lv + 0x14 + k); if (!nm[k]) break; }
    if (!ExtraMapsRulesPage(nm)) return;
    const int real = RoundExtraMap(nm), want = ExtraMapsRulesOf(real, nm), held = ExtraMapsRulesLoaded();
    if (want == held) return;
    W32(fl + 0x60, 1);
    Log("extra maps: the rules page %s held is %s, the round's (map %d) needs %s: loaded again", nm,
        held ? ExtraMapById(held)->name.c_str() : "the stock one", real, want ? ExtraMapById(want)->name.c_str() : "the stock one");
}
// 0x178490 (a0: a block's data, a1: the key, a2: the size, a3: where): a read of the rotation gets the maps of the
// extra maps' stages and RESCUE MISSION for TEAM SNEAKING (tsne.h: the game plays it with RESCUE's machinery); true
// when served
bool ExtraConfigRead(Ctx& c, uint32_t va) {
    const uint32_t key = (uint32_t)c.r[5], size = (uint32_t)c.r[6], dst = (uint32_t)c.r[7];   // dst: the stack or the scratchpad
    const uint32_t d0 = ConfigData0();
    if (!d0 || ((uint32_t)c.r[4] & 0x01ffffff) != d0 || key >= 0x22 || key + size <= 2 || size > 0x40) return false;
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    for (uint32_t o = std::max<uint32_t>(key, 2); o < std::min<uint32_t>(key + size, 0x22); ++o) {
        const uint32_t at = dst + (o - key);
        if ((o - 2) & 1) {                             // the pairs' rules (their second byte)
            if (R8(at) == 5) W8(at, 2);                // TEAM SNEAKING (tsne.h kTsneRule, kTsneAs)
            else if (R8(at) == 6) W8(at, 3);           // BASE CAPTURE as CAPTURE MISSION (basecap.h)
            continue;
        }
        const uint8_t m = R8(at);                      // the pairs' maps (their first byte)
        if (m > 12 && ExtraMapById(m)) W8(at, (uint8_t)ExtraMapStageOf(m));
    }
    return true;
}
const char* RuleName(uint32_t r) {                       // the rotation's rules (TEAM SNEAKING tsne.h, BASE CAPTURE basecap.h)
    static const char* const names[7] = {"DEATHMATCH", "TEAM DEATHMATCH", "RESCUE MISSION", "CAPTURE MISSION", "SNEAKING MISSION",
                                         "TEAM SNEAKING", "BASE CAPTURE"};
    return r < 7 ? names[r] : "?";
}
// 0x194C40 (a0: the lobby's context, a1: the game's settings, ntmenu2's work + 0x50): the create-game message 0x4310
// as a lobby server that checks the settings takes it (SaveMGO's: maps 1 to 12, 8 players at most, 9 with a
// host who does not play; docs/BIG_GAMES.md, EXTRA_MAPS.md): the maps 13 and up as their stages' maps, the
// players' maximum at that; the game keeps its own settings. True when served
bool LobbyCreateMessage(Ctx& c, uint32_t va) {
    const uint32_t st = (uint32_t)c.r[5] & 0x01ffffff;
    if (st < 0x100000 || st + 0x2b0 >= 0x02000000) return false;
    uint8_t maps[16], rules[16];
    bool change = false;
    for (int i = 0; i < 16; ++i) { maps[i] = R8(st + 0x244 + i); rules[i] = R8(st + 0x234 + i); change |= maps[i] > 12 || rules[i] == 5 || rules[i] == 6; }
    std::string rot;                                     // the rotation as this PC made it
    for (int i = 0; i < 16 && maps[i]; ++i) rot += std::string(i ? ", " : "") + RuleName(rules[i]) + " (" + std::to_string(rules[i]) + ") map " + std::to_string(maps[i]);
    Log("lobby: a game created, its rotation: %s", rot.empty() ? "none" : rot.c_str());
    // (v141 beta 5) MGO1_LOBBY_BIG_GAMES=1 (server.env LOBBY_BIG_GAMES): a lobby server that takes more than 8 (9 with Host
    // Only): the maximum told as it is; else that cap, the big game told by the hint (bit 5)
    static const bool bigLobby = DevEnv("MGO1_LOBBY_BIG_GAMES") != nullptr;
    // (v141 beta 5) a dedicated host's maximum above 9 is its settings' (the Create Game screens keep 9 at most; the
    // game's creation takes the settings', BigGameCreate): the hint's bit 5 from it (beta 4: none, the PCs tried every set)
    const uint8_t own = R8(st + 0x257), cap = R8(st + 0xa7) == 1 ? 9 : 8;
    const uint8_t most = cap == 9 && gHostMaxPlayers > own ? (uint8_t)gHostMaxPlayers : own;
    change |= most > cap;
    if (!change) return false;
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    for (int i = 0; i < 16; ++i) if (maps[i] > 12) W8(st + 0x244 + i, (uint8_t)ExtraMapStageOf(maps[i]));
    for (int i = 0; i < 16; ++i) if (rules[i] == 5) W8(st + 0x234 + i, 2);   // TEAM SNEAKING as RESCUE MISSION (tsne.h)
    for (int i = 0; i < 16; ++i) if (rules[i] == 6) W8(st + 0x234 + i, 3);   // BASE CAPTURE as CAPTURE MISSION (basecap.h)
    if (most > cap && !bigLobby) W8(st + 0x257, cap);
    const uint8_t options = R8(st + 0x2aa);              // open list: the extra maps told in the host list (openfix.h)
    const uint8_t mark = TsneListMark(rules, maps) | BcListMark(rules, maps);   // the host list's marks: bit 7 TSNE (tsne.h), bit 6 BASE CAPTURE (basecap.h)
    const uint8_t hint = MapsListHint(maps, most > cap);
    W8(st + 0x2aa, options | mark | hint);
    LastGameTold(st, maps, rules, most);                // open list: what the lobby server keeps (openfix.h)
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    for (int i = 0; i < 16; ++i) { W8(st + 0x244 + i, maps[i]); W8(st + 0x234 + i, rules[i]); }
    W8(st + 0x257, own);
    W8(st + 0x2aa, options);
    Log("session: the game told the lobby server with the maps of its stages, RESCUE MISSION for TEAM SNEAKING and %u players at most (%u here); its options %02x", most > cap && !bigLobby ? cap : most, most, options | mark | hint);
    return true;
}
// 0x11B288, [6bb005] (the same stage again, no load): the round's map is another extra map's (or its stage's own
// after one): the stage loaded again as [37c884] loads one (the next stage's name at the link variables' + 0x24,
// the stage flag 1), unless a load is under way (0x117AD8's test)
bool ExtraSameStage(Ctx& c) {
    const int real = RoundMap();
    if (!real || real == gLoadedMap || (real <= 12 && gLoadedMap <= 12)) return false;
    const char* stage = MapStage(real);
    const uint32_t fl = V(0x2C5238), lv = R32(V(0x23A678)) & 0x01ffffff;
    if (!stage || lv < 0x100000 || lv + 0x40 >= 0x02000000) return false;
    if ((int32_t)(R32(fl + 0x10) | R32(fl + 0x14)) < 0 || R32(fl + 0x3c)) return false;
    for (size_t k = 0; k <= strlen(stage); ++k) W8(lv + 0x24 + (uint32_t)k, (uint8_t)stage[k]);
    W32(fl, R32(fl) | 1);
    c.r[2] = 0;
    Log("extra maps: map %d after map %d on the same stage: %s loaded again", real, gLoadedMap, stage);
    return true;
}
// The 2D map of an extra map with floors (docs/EXTRA_MAPS.md): the game sends its map bank (online_map_0) to the GS
// each picture from the stage's copy in memory; the picture of the floor this player stands on is written over
// that copy (all the floors when he is not in the round), the copy found by the 64 bytes that tell it
int OwnSlot();
uint32_t PlayerOfSlot(int slot);
float F32(uint32_t a);
void ExtraMinimapTick() {
    if (!ExtraMapsFloors()) return;
    MinimapShown& m = gMinimap;
    ++m.pass;
    const uint32_t size = ExtraMapsMinimapSize();
    uint32_t off = 0;
    const uint8_t* probe = nullptr;
    if (m.at && (!ExtraMapsMinimapProbe(m.shown, off, probe) || memcmp(gMem + m.at + off, probe, 64))) {
        Log("extra maps: the 2D map's bank is no longer at %#x", m.at);
        m.at = 0; m.shown = 0;
    }
    if (!m.at) {                                         // looked for once a second (its image 16-byte aligned)
        if (m.pass - m.tried < 30 || !ExtraMapsMinimapProbe(m.shown, off, probe)) return;
        m.tried = m.pass;
        uint32_t first;
        memcpy(&first, probe, 4);
        int copies = 0;
        for (uint32_t a = 0x100000 + ((off + 15) & ~15u); a + 64 <= (32u << 20); a += 16) {
            uint32_t w;
            memcpy(&w, gMem + a, 4);
            if (w != first || memcmp(gMem + a, probe, 64) || a < off || a - off + size > (32u << 20)) continue;
            if (!copies++) m.at = a - off;               // the first (another one: logged, left as it is)
        }
        if (!m.at) return;
        Log("extra maps: the 2D map's bank at %#x (its image), %u floors%s", m.at, (unsigned)ExtraMapsFloors(), copies > 1 ? " (another copy in memory left as it is)" : "");
    }
    const int self = OwnSlot();                          // this player in the round: his floor; else all of them
    const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
    const bool alive = me && (int16_t)R16(me + 0xb64) > 0 && !(R32(me + 0xb48) & 0x1000000);
    int want = alive ? ExtraMapsFloorOf(F32(me + 0x114)) : 0;
    static const int cycle = getenv("SIM_MINIMAP_CYCLE") ? atoi(getenv("SIM_MINIMAP_CYCLE")) : 0;
    if (cycle > 0) want = (int)((m.pass / cycle) % (ExtraMapsFloors() + 1));   // tests: every picture in turn
    if (want != m.want) { m.want = want; m.held = 0; }
    if (m.want == m.shown || ++m.held < 8) return;       // a floor held for 8 passes
    ExtraMapsMinimapPut(gMem + m.at, m.want);
    m.shown = m.want;
    Log("extra maps: the 2D map shows %s", m.shown ? ExtraMapsFloorName(m.shown) : "all the floors");
}
// the text of the match's pages' map name (City Under Siege's page, its text function 0x3C7110 with the element
// 0xF4D880: the START menu's RULE page, 0x3CABC8 from the round's map in the settings, read as its stage's): the
// round's real map's name when it is an extra map
uint32_t NameString(int id, int kind);
void ExtraMapNameText(Ctx& c) {
    const int real = RoundMap();
    if (real <= 12 || !ExtraMapById(real)) return;
    if (const uint32_t s = NameString(real, 0)) {
        c.r[6] = s;
        if (Once(((uint64_t)71 << 40) | (uint32_t)real)) Log("extra maps: the match's pages name map %d %s", real, ExtraMapById(real)->name.c_str());
    }
}
// The texts of an extra map (map.ini texts=, texts<rule>=; docs/EXTRA_MAPS.md): while the stage loaded is that
// map's and its page runs, each copy of an old text in the page gets the new one in its place when it fits there
// (the text and the NULs after it to its 8-byte slot: the weapons' names, which the page's code also names by
// their address), else the page's pointers to it point to the new text in a block of the platform (the skins');
// set back when the page is another map's (a page loaded again holds its own). Applied when a script is set,
// before the page's actors copy its tables (the inventory's names), then at each pass; the page's text function
// (0x3C7110) is given the new text when handed an old one
int RoundRule() {                                      // the rule of the round the settings name (0 deathmatch ...), as the game plays it
    const uint32_t d = ConfigData0();
    if (d < 0x100000 || d + 0x40 >= 0x02000000) return -1;
    const uint32_t i = R8(d + 0x36);
    const int r = i < 16 ? R8(d + 3 + 2 * i) : -1;
    return r == 5 ? 2 : r == 6 ? 3 : r;                // TEAM SNEAKING as RESCUE MISSION (tsne.h), BASE CAPTURE as CAPTURE (basecap.h)
}
struct TextSwap {
    std::string from, to;
    uint32_t toAt = 0;                                 // the new text's copy (pointers)
    std::vector<uint32_t> inPlace;                     // the old text's copies it is written over
    std::vector<std::pair<uint32_t, uint32_t>> ptrs;   // {pointer, the old text it pointed to}
};
struct ExtraTexts { int map = 0, rule = -1; long long gen = -1; std::vector<TextSwap> swaps; std::map<std::string, uint32_t> strings; };
ExtraTexts gExtraTexts;
bool PageHeadIs(const char* page);
bool TextAt(uint32_t a, const std::string& t) { return a + t.size() + 1 <= 0x02000000 && !memcmp(gMem + a, t.c_str(), t.size() + 1); }
void PutText(uint32_t a, const std::string& t, size_t clear) {   // t and NULs over `clear` bytes
    memset(gMem + a, 0, clear);
    memcpy(gMem + a, t.c_str(), t.size());
}
void ExtraTextsTick() {
    ExtraTexts& x = gExtraTexts;
    const char* stage = gLoadedMap > 12 ? MapStage(gLoadedMap) : nullptr;
    const int map = stage && (gOverlayName == stage || PageHeadIs(stage)) ? gLoadedMap : 0, rule = map ? RoundRule() : -1;
    if (map != x.map || rule != x.rule || (map && gPageGen != x.gen)) {
        for (auto& t : x.swaps) {                        // the page's own texts again
            for (uint32_t a : t.inPlace) if (TextAt(a, t.to)) PutText(a, t.from, t.from.size() + 1);
            for (auto& p : t.ptrs) if (R32(p.first) == t.toAt) W32(p.first, p.second);
        }
        x.swaps.clear();
        x.map = map; x.rule = rule; x.gen = gPageGen;
        if (!map) return;
        const uint32_t lo = PageLo(), hi = std::min<uint32_t>(PageHi(), 0x02000000);
        const ExtraMap* em = ExtraMapById(map);
        for (auto& [from, to] : ExtraMapsTexts(map, rule)) {
            TextSwap t{from, to};
            std::vector<uint32_t> apart;                 // copies the new text does not fit
            for (uint32_t a = lo + 1; a + from.size() + 1 <= hi; ++a) {   // each copy of the old text (a whole string)
                if (gMem[a] != (uint8_t)from[0] || gMem[a - 1] || !TextAt(a, from)) continue;
                const uint32_t end = a + (uint32_t)from.size() + 1, slot = (a & ~7u) + ((end - (a & ~7u) + 7) & ~7u);
                uint32_t room = (uint32_t)from.size() + 1;
                while (a + room < slot && a + room < hi && !gMem[a + room]) ++room;
                (to.size() + 1 <= room ? t.inPlace : apart).push_back(a);
            }
            if (!apart.empty()) {
                uint32_t& at = x.strings[to];
                if (!at && (at = Alloc((uint32_t)to.size() + 1)) != 0) memcpy(gMem + at, to.c_str(), to.size() + 1);
                t.toAt = at;
                if (at)
                    for (uint32_t w = lo; w + 4 <= hi; w += 4)
                        if (std::find(apart.begin(), apart.end(), R32(w)) != apart.end()) t.ptrs.push_back({w, R32(w)});
            }
            Log("extra maps: %s's text \"%s\" shown \"%s\" (rule %d): %zu of its copies in the page written over, %zu pointers to %zu others",
                em ? em->name.c_str() : "?", from.c_str(), to.c_str(), rule, t.inPlace.size(), t.ptrs.size(), apart.size());
            x.swaps.push_back(std::move(t));
        }
    }
    for (auto& t : x.swaps) {
        for (uint32_t a : t.inPlace) if (TextAt(a, t.from)) PutText(a, t.to, t.from.size() + 1);
        for (auto& p : t.ptrs) if (R32(p.first) == p.second) W32(p.first, t.toAt);
    }
}
void ExtraTextSwap(Ctx& c) {                           // 0x3C7110 (a2: the text)
    const uint32_t s = (uint32_t)c.r[6] & 0x01ffffff;
    if (gExtraTexts.swaps.empty() || s < 0x100000 || s + 64 >= 0x02000000) return;
    for (auto& t : gExtraTexts.swaps) {
        if (t.toAt && s == t.toAt) return;
        if (!memcmp(gMem + s, t.from.c_str(), t.from.size() + 1)) {
            if (!t.toAt) {                               // a copy made before the page's own were written over
                uint32_t& at = gExtraTexts.strings[t.to];
                if (!at && (at = Alloc((uint32_t)t.to.size() + 1)) != 0) memcpy(gMem + at, t.to.c_str(), t.to.size() + 1);
                if (!(t.toAt = at)) return;
            }
            c.r[6] = t.toAt;
            if (Once(((uint64_t)73 << 40) | t.toAt)) Log("extra maps: the page's text \"%s\" drawn \"%s\"", t.from.c_str(), t.to.c_str());
            return;
        }
    }
}
// ---- The added weapons (docs/CHARACTERS.md, The added weapons; extramaps.h AddedWeapon): the weapons.ini of the
// extra map loaded, each one of the game's weapons (its base: its number, its logic) made, shown and named as
// another. Each player's choice, per slot and base weapon: the added weapon's rank + 1 in the map's list (0: the
// base itself). What changes for a player holding one:
// - its model, its points, its magazine: the base's init (the AK-47's 0x4D68A8 ...) asks GV_StrCode (0x123150) for
//   "akmr", "akmr_amo": the added weapon's names answered for its owner (AddedModelName);
// - its stance: the weapons' table's +0x24 (0x2FCB60) asked at a weapon change for a player (0x3FCE78, 0x3FD070,
//   0x49EE68 with the player in s0, 0x31B3C8 in s2; his slot at +0xB60): the added weapon's;
// - for the player of this program: the names and the icon of his inventory (the items, US n003a 0x56B718 + 0x38
//   a weapon: +0 the short name, +4 the long one, +0xC the window's icon), the first person targets (the base's
//   run of floats while he holds it);
// - the laser (LaserDrive, the origin's hooks)
uint8_t gAddedChoice[32][0x40];
// this player's own choice in the menu per base (the rank + 1 as above): the choice's cursor comes back to it (a weapon
// taken from a box changes gAddedChoice only: BoxTaken)
uint8_t gMenuPick[0x40];
// the drops of base weapons (a death's, by hand), each with its dropper's weapon (DropMade, vnet type 5, DropLabel)
struct Drop { int who = -1, seq = 0, code = 0, rank = 0; float pos[3]{}; unsigned long long ms = 0, sent = 0; int sends = 0; };
std::mutex gDropsLock;
std::vector<Drop> gDrops;
struct Box { int code = 0; float pos[3]{}; };             // a box made from a drop's record: its code, the record's place
std::map<uint32_t, Box> gBoxes;                          // by the box's work (the box moves as it falls: its record's place kept)
const AddedWeapon* SnakeWeaponOf(int slot, int base);   // Snake's class in SNEAKING (extrarules.h, #120)
const AddedWeapon* AddedOf(int slot, int base) {
    if (slot < 0 || slot >= 32 || base <= 0 || base >= 0x40) return nullptr;
    if (const AddedWeapon* s = SnakeWeaponOf(slot, base)) return s;   // #120: his weapons.ini snake= weapons
    const int k = gAddedChoice[slot][base];
    const auto& list = ExtraMapsWeapons();
    return k > 0 && k <= (int)list.size() && list[(size_t)k - 1].base == base ? &list[(size_t)k - 1] : nullptr;
}
int PlayerSlotOf(uint32_t obj) {                         // a player's actor (this program's or a copy): his slot, -1 none
    const auto it = gNativeActors.find(obj);
    if (it == gNativeActors.end() || (it->second != "NewNetworkPlayer" && it->second != "NewNetworkDummyPlayer")) return -1;
    return R8(obj + 0xB60);
}
// the language of the weapons' descriptions shown (the link variable 4: 1 English ... 5 Spanish; the Japanese and
// Korean programs their own): the texts of an added weapon in it (German and Italian: the English ones, as the
// game's own descriptions are then; docs/LANGUAGES.md)
std::string AddedTextLang() {
    const char* v = ps2::SelectedVersion();
    if (v && (!strcmp(v, "JP") || !strcmp(v, "JPS"))) return "ja";
    if (v && !strcmp(v, "KR")) return "ko";
    const uint32_t lv = R32(V(0x23A678)) & 0x01ffffff;
    const uint32_t lang = lv >= 0x100000 && lv + 8 < 0x02000000 ? R16(lv + 4) : 1;
    return lang == 2 ? "fr" : lang == 5 ? "es" : "en";
}
// a text of the platform's in PS2 memory (kept: the page's pointers may hold it)
uint32_t AddedString(const std::string& s) {
    static std::map<std::string, uint32_t> made;
    uint32_t& at = made[s];
    if (!at && (at = Alloc((uint32_t)s.size() + 1)) != 0) memcpy(gMem + at, s.c_str(), s.size() + 1);
    return at;
}
// SIM_ADDED_WEAPONS=FAMAS,SOCOM (tests): this program's player chooses those added weapons (before the menu's
// own choice exists)
void AddedTestChoice(int self) {
    static const char* want = getenv("SIM_ADDED_WEAPONS");
    if (!want || self < 0 || self >= 32) return;
    const auto& list = ExtraMapsWeapons();
    for (size_t k = 0; k < list.size(); ++k) {
        const std::string n = "," + list[k].name + ",", w = "," + std::string(want) + ",";
        if (w.find(n) != std::string::npos && gAddedChoice[self][list[k].base] != k + 1) {
            gAddedChoice[self][list[k].base] = (uint8_t)(k + 1);
            Log("added weapons: player %d holds the %s for weapon %#x (SIM_ADDED_WEAPONS)", self + 1, list[k].name.c_str(), list[k].base);
        }
    }
}
// ---- The added weapons' descriptions (Snake, 23:22, #114; docs/CHARACTERS.md, The added weapons: their texts): the R2
// window describes the weapon it shows with a string resource of the rules page's script (each rules page and r_menu_n
// hold them): sets of 159 strings, a weapon's number its index in a set, the string 116 "\x80|\n" four times in each;
// the North American and European pages two sets (the second one English, the first Spanish or French), the Japanese
// and Korean ones one. The script is set (0x125500) from the page as it is read into pool 0, and the pool is used
// again once the page has loaded: the strings the window shows are copies the page's code keeps (its script's
// callcode [ab5a2a] -s ...). So the descriptions are kept as the script holds them, and their copies found in memory
// (each one a whole string: a NUL before it); an added weapon's description is written over its base's in every copy
// while this player holds it (the base's own again when he holds the base), each set in its language, never longer
// than the base's (spaces after it: the strings keep their places). A description is weapons.ini's text= (text.fr=
// ...): the whole text, or parts of the base's written over (OLD=>NEW|OLD=>NEW, as map.ini's scripttexts=).
// The script's own glyphs (\x8c NN: the M1911A1's and the Mk22's suppressor button, the Japanese and Korean pages'
// kanji and hangul) have other codes in the copies (\x88 01 for the button on the North American page, dump R1, 30
// September): a copy is found with those codes as they come, each one's own code for them kept with it, and a text is
// written in each copy with that copy's codes
struct DescCopy { uint32_t at = 0; std::map<uint8_t, uint16_t> glyph; };    // glyph: the script's \x8c NN, this copy's code
struct DescText { std::string own, cur; std::vector<DescCopy> at; };          // cur: the text written (own at first)
struct DescPage { int sets = 0; std::map<int, DescText> t; long long scanned = -1000000, scanGen = -1; };   // key: set * 0x100 + weapon; scanGen: the page searched at its load
DescPage gDesc;
constexpr uint32_t kDescSet = 159, kDescMark = 116;
const std::string kDescMarkText = "\x80|\n\x80|\n\x80|\n\x80|\n";
std::string PS2Str(uint32_t a, uint32_t max = 2048) {   // the platform's own strings (Alloc) are below the program
    std::string s;
    while (a >= 0x1000 && a < 0x02000000 && s.size() < max && gMem[a]) s += (char)gMem[a++];
    return s;
}
// a text's codes: one byte below 0x80 (0x1F and every byte from 0x80 on lead a code of two); the places of the
// script's own glyphs (\x8c NN)
std::vector<size_t> DescGlyphs(const std::string& s) {
    std::vector<size_t> g;
    for (size_t i = 0; i < s.size(); ++i) {
        const uint8_t ch = (uint8_t)s[i];
        if (ch != 0x1F && ch < 0x80) continue;
        if (ch == 0x8C && i + 1 < s.size()) g.push_back(i);
        ++i;
    }
    return g;
}
// a text as a copy holds it: the script's glyphs with that copy's codes (one the copy does not show: \x8c's lead
// changed as the copy's others, the same number)
std::string DescAs(const std::string& s, const DescCopy& c) {
    if (c.glyph.empty()) return s;
    std::string out = s;
    int lead = -1;
    for (auto& [n, code] : c.glyph) {
        if ((code & 0xFF) != n) { lead = -2; break; }
        if (lead == -1) lead = code >> 8; else if (lead != (code >> 8)) { lead = -2; break; }
    }
    for (size_t i : DescGlyphs(s)) {
        const uint8_t n = (uint8_t)s[i + 1];
        const auto it = c.glyph.find(n);
        if (it != c.glyph.end()) { out[i] = (char)(it->second >> 8); out[i + 1] = (char)(it->second & 0xFF); }
        else if (lead >= 0) out[i] = (char)lead;
    }
    return out;
}
// 0x125500 (a0 the script): a script with the descriptions: the weapons' ones kept (the strings deciphered on a copy)
void AddedDescScript(uint32_t g) {
    g &= 0x01ffffff;
    if (g < 0x100000 || g + 64 >= 0x02000000) return;
    uint32_t n = 0;
    while (n < 4096 && g + 8 + 4 * n < 0x02000000 && R32(g + 4 + 4 * n) != 0xffffffffu) ++n;
    const uint32_t blk = g + 4 + 4 * (n + 1);
    if (n >= 4096 || blk + 20 >= 0x02000000) return;
    const uint32_t procOff = R32(blk), rtab = R32(blk + 4), sres = R32(blk + 8), font = R32(blk + 12);
    uint32_t seed = R32(blk + 16);
    if (!(rtab <= sres && sres <= font && font <= procOff && procOff < 0x100000 && blk + procOff < 0x02000000) || (sres - rtab) & 3) return;
    const uint32_t nres = (sres - rtab) / 4;
    if (!nres || nres % kDescSet || nres > 2 * kDescSet) return;
    std::vector<uint8_t> d(gMem + blk + sres, gMem + blk + font);
    for (size_t k = 0; seed && k < d.size(); ++k) { seed = seed * 0x7D2B89DDu + 0xCF9u; d[k] ^= (uint8_t)(seed >> 15); }
    auto str = [&](uint32_t index) -> std::string {
        const uint32_t o = R32(blk + rtab + 4 * index) & 0xffffff;
        if (o >= d.size()) return std::string();
        const auto e = std::find(d.begin() + o, d.end(), (uint8_t)0);
        return std::string(d.begin() + o, e);
    };
    const int sets = (int)(nres / kDescSet);
    for (int k = 0; k < sets; ++k) if (str(kDescSet * (uint32_t)k + kDescMark) != kDescMarkText) return;
    DescPage page;
    page.sets = sets;
    for (int k = 0; k < sets; ++k)
        for (int b = 1; b < 0x40; ++b) {
            std::string t = str(kDescSet * (uint32_t)k + (uint32_t)b);
            if (t.size() >= 8) page.t[k * 0x100 + b] = DescText{t, t, {}};
        }
    gDesc = std::move(page);
    Log("added weapons: the script at %08x holds the weapons' descriptions (%d sets, %zu texts)", g, sets, gDesc.t.size());
}
// an added weapon's description in a language made on the base's own (own): its text= or its parts over own; own when it
// has none or it does not fit
std::string DescFor(const AddedWeapon& w, const std::string& lang, const std::string& own) {
    const std::string& t = AddedWeaponText(w, true, lang);
    if (t.empty()) return own;
    std::string out;
    if (t.find("=>") == std::string::npos) out = t;
    else {
        out = own;
        size_t p = 0;
        while (p <= t.size()) {
            size_t e = t.find('|', p); if (e == std::string::npos) e = t.size();
            const std::string part = t.substr(p, e - p); p = e + 1;
            const size_t arrow = part.find("=>");
            if (arrow == std::string::npos || !arrow) continue;
            const std::string from = part.substr(0, arrow), to = part.substr(arrow + 2);
            const size_t at = out.find(from);
            if (at != std::string::npos) out.replace(at, from.size(), to);
        }
    }
    if (out.size() > own.size()) {
        if (Once(((uint64_t)99 << 56) | ((uint64_t)StageStrCode24(w.name) << 16) | (lang.size() == 2 ? (uint8_t)lang[0] << 8 | (uint8_t)lang[1] : 0)))
            Log("added weapons: the %s's description (%s) is %zu bytes, its base's %zu: the base's shown", w.name.c_str(), lang.c_str(), out.size(), own.size());
        return own;
    }
    return out + std::string(own.size() - out.size(), ' ');
}
// the time of the added weapons' work (logs: what each choice costs)
struct AddedClock { long long t0 = RealQpc(); double Ms() const { return (double)(RealQpc() - t0) * 1000.0 / (double)QpcFreq(); } };
// SIM_TICK_LOG=<n> (tests, off by default): the n ticks at 60 after an event (the weapon choice's menu, its OK) logged
int gSimTickLog = 0;
void SimTickLogFrom(const char* why) {
    static const int n = DevEnv("SIM_TICK_LOG") ? atoi(DevEnv("SIM_TICK_LOG")) : 0;
    if (n <= 0) return;
    gSimTickLog = n;
    Log("tick log: the next %d ticks (%s; %lld late so far)", n, why, gPace.lateTicks);
}
// the copies of the descriptions a pass needs, searched in memory (a whole string: a NUL before, a NUL after; the
// script's glyphs any code of two from \x88 to \x8f, the same glyph the same code in a copy): by the longest run of
// the text without the script's glyphs, each place it is at then read whole
void DescScan(const std::vector<int>& keys) {
    const AddedClock clock;
    for (int key : keys) gDesc.t[key].at.clear();
    int found = 0;
    for (int key : keys) {
        DescText& t = gDesc.t[key];
        const std::string& own = t.own;
        const std::vector<size_t> glyphs = DescGlyphs(own);
        size_t runAt = 0, runLen = 0;                    // the longest run between the glyphs (the whole text and its NUL: none)
        if (glyphs.empty()) runLen = own.size() + 1;
        else
            for (size_t k = 0, from = 0; k <= glyphs.size(); ++k) {
                const size_t to = k < glyphs.size() ? glyphs[k] : own.size() + 1;
                if (to - from > runLen) { runAt = from; runLen = to - from; }
                if (k < glyphs.size()) from = glyphs[k] + 2;
            }
        const std::string needle = std::string(own.c_str(), own.size() + 1).substr(runAt, runLen);
        const uint8_t* lo = gMem + 0x100000, * hi = gMem + 0x02000000;
        const std::boyer_moore_horspool_searcher<std::string::const_iterator> bm(needle.begin(), needle.end());
        for (const uint8_t* p = lo; runLen && p < hi;) {
            const auto r = std::search((const char*)p, (const char*)hi, bm);
            if (r == (const char*)hi) break;
            p = (const uint8_t*)r + 1;
            const uint32_t a = (uint32_t)((const uint8_t*)r - gMem) - (uint32_t)runAt;
            if (a <= 0x100000 || a + own.size() + 1 > 0x02000000 || gMem[a - 1] || gMem[a + own.size()]) continue;
            DescCopy c{a, {}};
            bool same = true;
            for (size_t i = 0, g = 0; same && i < own.size(); ++i) {
                if (g < glyphs.size() && glyphs[g] == i) {
                    const uint8_t lead = gMem[a + i], n = (uint8_t)own[i + 1];
                    const uint16_t code = (uint16_t)(lead << 8 | gMem[a + i + 1]);
                    const auto it = c.glyph.find(n);
                    same = lead >= 0x88 && lead <= 0x8F && (it == c.glyph.end() || it->second == code);
                    c.glyph[n] = code;
                    ++i; ++g;
                } else same = gMem[a + i] == (uint8_t)own[i];
            }
            if (!same) continue;
            bool asScript = true;                         // the codes as the script's (\x8c NN): no mapping kept
            for (auto& [n, code] : c.glyph) asScript = asScript && code == (0x8C00 | n);
            if (asScript) c.glyph.clear();
            t.at.push_back(c);
        }
        found += (int)t.at.size();
        for (const DescCopy& c : t.at)
            if (Once(((uint64_t)102 << 40) | c.at)) {
                std::string codes;
                for (auto& [n, code] : c.glyph) { char b[24]; snprintf(b, sizeof b, " 8c%02x=%04x", n, code); codes += b; if (codes.size() > 90) { codes += " ..."; break; } }
                Log("added weapons: weapon %#x's description (set %d) at %08x%s%s", key & 0xff, key >> 8, c.at, codes.empty() ? "" : ", the script's glyphs as", codes.c_str());
            }
        if (t.at.empty() && t.own.size() > 12 && Once(((uint64_t)103 << 40) | (uint32_t)key)) {   // what holds its start instead
            const std::string head = t.own.substr(0, 12);
            int shown = 0;
            for (const uint8_t* p = lo; p < hi && shown < 4;) {
                const auto r = std::search((const char*)p, (const char*)hi, head.begin(), head.end());
                if (r == (const char*)hi) break;
                const uint32_t a = (uint32_t)((const uint8_t*)r - gMem);
                std::string s;
                for (uint32_t i = 0; i < 48 && a + i < 0x02000000; ++i) {
                    const uint8_t ch = gMem[a + i];
                    char b[8]; snprintf(b, sizeof b, ch >= 0x20 && ch < 0x7f && ch != '\\' ? "%c" : "\\x%02x", ch); s += b;
                }
                Log("added weapons: weapon %#x's description (set %d) not whole in memory; its start at %08x (before it %02x): %s",
                    key & 0xff, key >> 8, a, gMem[a - 1], s.c_str());
                ++shown;
                p = (const uint8_t*)r + 1;
            }
            if (!shown) Log("added weapons: weapon %#x's description (set %d) nowhere in memory", key & 0xff, key >> 8);
        }
    }
    Log("added weapons: %d copies of %zu descriptions found in memory (%.2f ms)", found, keys.size(), clock.Ms());
}
// each pass: the descriptions of the weapons with added ones, as this player holds them (restore: every one its own)
void AddedDescTick(int self, bool restore) {
    if (gDesc.t.empty()) return;
    const std::string lang = AddedTextLang();
    const char* v = ps2::SelectedVersion();
    // each set's language, as the pages hold them: the North American one Spanish then English, the European one
    // French then English (whatever the language shown), the Japanese and Korean ones their own
    const std::string first = gDesc.sets == 1 ? lang : v && !strcmp(v, "EU") ? "fr" : "es";
    std::vector<int> want, lost;                         // the texts to hold (an added weapon's or their own again)
    for (auto& [key, t] : gDesc.t) {
        const int b = key & 0xff;
        if (!restore && AddedOf(self, b)) want.push_back(key);
        else if (t.cur != t.own) want.push_back(key);
    }
    if (want.empty()) return;
    for (int key : want) {                               // the copies still there
        DescText& t = gDesc.t[key];
        t.at.erase(std::remove_if(t.at.begin(), t.at.end(), [&](const DescCopy& c) { return !TextAt(c.at, DescAs(t.own, c)) && !TextAt(c.at, DescAs(t.cur, c)); }), t.at.end());
        if (t.at.empty()) lost.push_back(key);
    }
    if (!lost.empty() && gPassNo - gDesc.scanned >= 120) { gDesc.scanned = gPassNo; DescScan(lost); }
    for (int key : want) {
        DescText& t = gDesc.t[key];
        const int k = key >> 8, b = key & 0xff;
        const std::string setLang = k == 1 ? std::string("en") : first;
        const AddedWeapon* w = restore ? nullptr : AddedOf(self, b);
        const std::string text = w ? DescFor(*w, setLang, t.own) : t.own;
        if (text == t.cur && std::all_of(t.at.begin(), t.at.end(), [&](const DescCopy& c) { return TextAt(c.at, DescAs(text, c)); })) continue;
        for (const DescCopy& c : t.at) PutText(c.at, DescAs(text, c), t.own.size() + 1);
        t.cur = text;
        if (!t.at.empty()) Log("added weapons: the description of weapon %#x in set %d (%s) %s (%zu copies)", b, k, setLang.c_str(),
                               w ? ("is the " + w->name + "'s").c_str() : "is its own again", t.at.size());
    }
}
double gAddedPassMs = 0;                                 // the longest pass of AddedWeaponsTick since the last choice
int HudIconSlot(uint32_t code) {                         // the HUD's slot holding an icon (-1: none)
    const uint32_t slots = N3HudIcons();
    for (int k = 0; slots && code && k < 40; ++k) if (R16(slots + 8 * (uint32_t)k + 4) && R32(slots + 8 * (uint32_t)k) == code) return k;
    return -1;
}
void AddedIconsTick(const Ctx& c);
void AddedHeldReset();
// each pass: this program's inventory (the items' names and icons) and the first person targets of the weapon held
void AddedWeaponsTick(const Ctx& c) {
    const AddedClock clock;
    const auto& list = ExtraMapsWeapons();
    struct Kept { uint32_t shortName = 0, longName = 0, icon = 0; bool kept = false; };
    static Kept kept[0x40];                              // an item's own values while an added weapon's are in it
    static Kept keptItem[0x61];                          // the same for the items of a base weapon (its suppressor)
    static Kept keptDesc[0x61];                          // and for their descriptors
    static uint32_t fpvAt[0x40];                         // the base's run of first person floats (0: not found)
    static int keptCap[0x40];                            // a descriptor's own magazine rounds while rounds= holds it
    static std::vector<uint8_t> fpvOwn[0x40];            // its bytes as the page has them
    static long long fpvGen = -1;
    const bool here = !list.empty() && N3Page();      // (the page's data read only while it is the page)
    const uint32_t items = here ? N3WeaponDescs() : 0;
    if (!here || !items) {
        AddedDescTick(-1, true);
        memset(gAddedChoice, 0, sizeof gAddedChoice);
        memset(gMenuPick, 0, sizeof gMenuPick);
        for (auto& k : kept) k.kept = false;             // the page's own come again with its next load
        for (auto& k : keptItem) k.kept = false;
        for (auto& k : keptDesc) k.kept = false;
        memset(keptCap, 0, sizeof keptCap);
        fpvGen = -1;
        AddedHeldReset();
        { std::lock_guard<std::mutex> g(gDropsLock); gDrops.clear(); gBoxes.clear(); }   // the drops lie in that match only
        return;
    }
    const int self = OwnSlot();
    AddedTestChoice(self);
    const std::string lang = AddedTextLang();
    if (gDesc.scanGen != (long long)gPageGen && !gDesc.t.empty()) {   // the descriptions' copies, once a map load
        gDesc.scanGen = (long long)gPageGen;
        std::vector<int> keys;
        for (const auto& [key, t] : gDesc.t)
            if (std::any_of(list.begin(), list.end(), [&](const AddedWeapon& w) { return w.base == (key & 0xff); })) keys.push_back(key);
        const AddedClock t;
        if (!keys.empty()) { gDesc.scanned = gPassNo; DescScan(keys); }
        Log("added weapons: time: the descriptions' copies searched at the map's load in %.2f ms (%zu texts)", t.Ms(), keys.size());
    }
    AddedDescTick(self, false);
    AddedIconsTick(c);                                   // the HUD's slots for the weapons held now
    for (int b = 1; b < 0x40; ++b) {
        const uint32_t it = items + 0x38 * (uint32_t)b;
        const AddedWeapon* w = AddedOf(self, b);
        Kept& k = kept[b];
        if (w) {
            const uint32_t sn = AddedString(AddedWeaponText(*w, false, lang));
            if (!sn) continue;
            // its icon only while a slot of the HUD holds it (the windows look a code up at each draw: an empty box)
            const bool named = R32(it) == sn;
            if (!named && !k.kept) { k.shortName = R32(it); k.longName = R32(it + 4); k.icon = R32(it + 0xC); k.kept = true; }
            const uint32_t icon = w->icon && HudIconSlot(w->icon) >= 0 ? w->icon : k.icon;
            if (!named) {
                W32(it, sn); W32(it + 4, sn);
                if (icon) W32(it + 0xC, icon);
                Log("added weapons: this player's weapon %#x shown as the %s (its name, its icon %06x%s)", b, w->name.c_str(), icon,
                    icon == w->icon ? "" : ": its base's, no slot holds the added one");
            } else if (k.kept && icon && R32(it + 0xC) != icon) {
                W32(it + 0xC, icon);
                Log("added weapons: this player's weapon %#x shown with the icon %06x (%s)", b, icon,
                    icon == w->icon ? ("the " + w->name + "'s, in the HUD's slot " + std::to_string(HudIconSlot(icon))).c_str() : "its base's: no slot holds the added one");
            }
        } else if (k.kept) {
            if (R32(it) != k.shortName) { W32(it, k.shortName); W32(it + 4, k.longName); W32(it + 0xC, k.icon); }
            k.kept = false;
        }
    }
    // rounds= (an added weapon's magazine): the descriptor's +0x1E (+0x1C the rounds in the magazine, +0x18 all of
    // them; US n003a 0x2FCE50 the reload fills the magazine with +0x1E, 0x2FCEC8 a shot) while this player holds it,
    // a full magazine full again with it, one larger cut; the base's own again otherwise
    {
        const uint32_t link = R32(V(0x23A678)) & 0x01ffffff, heldRef = N3HeldDesc();
        const uint32_t held = heldRef ? R32(heldRef) & 0x01ffffff : 0;
        for (int b = 1; b < 0x40; ++b) {
            const uint32_t it = items + 0x38 * (uint32_t)b;
            const AddedWeapon* w = AddedOf(self, b);
            const bool mine = w && w->rounds > 0;
            const int want = mine ? w->rounds : keptCap[b];
            if (!want) continue;
            const int cap = (int16_t)R16(it + 0x1E);
            if (cap == want) { if (!mine) keptCap[b] = 0; continue; }
            if (mine && !keptCap[b]) keptCap[b] = cap;
            int mag = (int16_t)R16(it + 0x1C);
            const int all = (int16_t)R16(it + 0x18);
            if (mag == cap || mag > want) mag = std::min(want, std::max(all, 0));
            W16(it + 0x1E, (uint16_t)want);
            W16(it + 0x1C, (uint16_t)mag);
            if (held == it && link >= 0x100000 && link + 0x5E0 < 0x02000000) W16(link + 0x5DC, (uint16_t)mag);
            Log("added weapons: this player's weapon %#x holds %d rounds a magazine (%s), %d in it", b, want,
                mine ? ("the " + w->name + "'s").c_str() : "its own again", mag);
            if (!mine) keptCap[b] = 0;
        }
    }
    // the items of a base weapon (its suppressor): US n003a's item table 0x2DCB98 (0x24 an item: +0 the short name, +4
    // the long one, +0xC the icon, +0x14 the weapon it goes on; the game reads the names there, 0x2FCAF0) and the
    // items' descriptors made from it (0x56D3C0, 0x38 an item, 0x2FCF80: +0, +4, +0xC the same; the HUD draws the
    // suppressed M1911A1's and Mk22's icons from there), given the added weapon's name in place of the base's (SP/SOCOM,
    // SUPPRESSOR FOR SOCOM in the language shown) and its icon_suppressed; their own values again otherwise
    const uint32_t itemTable = N3Items(), itemDescs = N3ItemDescs(), weapons = N3Weapons();
    static long long itemGen = -1;
    if (itemGen != (long long)gPageGen) {                // a page loaded again: its own
        itemGen = (long long)gPageGen;
        for (auto& k : keptItem) k.kept = false;
        for (auto& k : keptDesc) k.kept = false;
    }
    for (int i = 1; itemTable && weapons && i < 0x61; ++i) {
        const uint32_t d = itemTable + 0x24 * (uint32_t)i, r = itemDescs ? itemDescs + 0x38 * (uint32_t)i : 0;
        const int weapon = (int)R32(d + 0x14);
        const AddedWeapon* w = weapon > 0 && weapon < 0x40 ? AddedOf(self, weapon) : nullptr;
        Kept& k = keptItem[i];
        Kept& kr = keptDesc[i];
        if (w) {
            if (!k.kept) { k.shortName = R32(d); k.longName = R32(d + 4); k.icon = R32(d + 0xC); k.kept = true; }
            if (r && !kr.kept) { kr.shortName = R32(r); kr.longName = R32(r + 4); kr.icon = R32(r + 0xC); kr.kept = true; }
            const std::string name = AddedWeaponText(*w, false, lang);
            const std::string baseShort = PS2Str(R32(weapons + 0x28 * (uint32_t)weapon), 64), baseLong = PS2Str(R32(weapons + 0x28 * (uint32_t)weapon + 4), 64);
            auto swap = [&](uint32_t s) -> uint32_t {
                std::string t = PS2Str(s, 128);
                size_t at = baseLong.empty() ? std::string::npos : t.find(baseLong);
                if (at != std::string::npos) t.replace(at, baseLong.size(), name);
                else if (!baseShort.empty() && (at = t.find(baseShort)) != std::string::npos) t.replace(at, baseShort.size(), name);
                else return s;
                return AddedString(t);
            };
            const uint32_t sn = swap(k.shortName), ln = swap(k.longName);
            const uint32_t icon = w->iconSuppressed && HudIconSlot(w->iconSuppressed) >= 0 ? w->iconSuppressed : k.icon;   // as the weapon's
            const bool page = R32(d) != sn || R32(d + 4) != ln || R32(d + 0xC) != icon;
            const bool desc = r && (R32(r + 0x14) & 0xFF) == (uint32_t)i && (R32(r) != sn || R32(r + 4) != ln || R32(r + 0xC) != icon);   // +0x14: its number
            if (page) { W32(d, sn); W32(d + 4, ln); W32(d + 0xC, icon); }
            if (desc) { W32(r, sn); W32(r + 4, ln); W32(r + 0xC, icon); }
            if (page || desc)
                Log("added weapons: this player's item %#x (of weapon %#x) shown as the %s's: \"%s\", its icon %06x (%s)", i, weapon, w->name.c_str(),
                    PS2Str(ln, 64).c_str(), icon, page && desc ? "the table and the descriptor" : page ? "the table" : "the descriptor");
        } else {
            if (k.kept) {
                if (R32(d) != k.shortName || R32(d + 4) != k.longName || R32(d + 0xC) != k.icon) { W32(d, k.shortName); W32(d + 4, k.longName); W32(d + 0xC, k.icon); }
                k.kept = false;
            }
            if (kr.kept) {
                if (R32(r) != kr.shortName || R32(r + 4) != kr.longName || R32(r + 0xC) != kr.icon) { W32(r, kr.shortName); W32(r + 4, kr.longName); W32(r + 0xC, kr.icon); }
                kr.kept = false;
            }
        }
    }
    if (fpvGen != (long long)gPageGen) {                 // the page's runs of first person floats, once a page
        const AddedClock t;
        fpvGen = (long long)gPageGen;
        memset(fpvAt, 0, sizeof fpvAt);
        const uint32_t lo = PageLo(), hi = std::min<uint32_t>(PageHi(), 0x02000000);
        for (const auto& w : list) {
            if (w.fpvOld.empty() || fpvAt[w.base]) continue;
            const size_t n = w.fpvOld.size() * 4;
            uint32_t found = 0; int copies = 0;
            auto runNear = [&](uint32_t a) {             // the run within 0.01 (the ini's floats as written)
                for (size_t i = 0; i < w.fpvOld.size(); ++i) if (std::fabs(F32(a + 4 * (uint32_t)i) - w.fpvOld[i]) > 0.01f) return false;
                return true;
            };
            for (uint32_t a = lo; a + n <= hi; a += 4)
                if (std::fabs(F32(a) - w.fpvOld[0]) <= 0.01f && runNear(a)) { if (!copies++) found = a; }
            fpvAt[w.base] = copies == 1 ? found : 0;
            if (copies == 1) fpvOwn[w.base].assign(gMem + found, gMem + found + n);
            Log("added weapons: the first person targets of weapon %#x %s (%zu floats from %.3f %.3f, the page %08x..%08x)", w.base,
                copies == 1 ? "found in the page" : copies ? "found more than once: left as they are" : "not found: left as they are",
                w.fpvOld.size(), w.fpvOld[0], w.fpvOld.size() > 1 ? w.fpvOld[1] : 0.f, lo, hi);
        }
        Log("added weapons: time: the first person targets searched at the map's load in %.2f ms", t.Ms());
    }
    const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
    const uint32_t eq = me ? R32(me + 0x608) : 0;
    const int held = eq >= 0x100000 && eq < 0x01FFFFC0 ? R8(eq + 0x14) : 0;
    for (const auto& w : list) {
        const uint32_t at = w.fpvOld.empty() ? 0 : fpvAt[w.base];
        if (!at) continue;
        const AddedWeapon* mine = AddedOf(self, w.base);
        if (mine && mine != &w) continue;                // another added weapon of that base writes it
        const uint8_t* v = mine && held == w.base ? (const uint8_t*)w.fpvNew.data() : fpvOwn[w.base].data();
        const size_t n = w.fpvNew.size() * 4;
        if (fpvOwn[w.base].size() == n && memcmp(gMem + at, v, n)) memcpy(gMem + at, v, n);
    }
    gAddedPassMs = std::max(gAddedPassMs, clock.Ms());
}
// ---- The choice before a spawn with the added weapons (the WEAPON INVENTORY, US n003a; docs/CHARACTERS.md, The added
// weapons: the choice). Its rows are built by 0x3CBDC8 (a0 the menu: [+0x40] its layout, [+0x334] the weapons the
// rules allow, one bit each: primary 0-5, secondary 6-8, support 9-15), each weapon shown in the row's next slot
// (7 at most: the slots' codes at 0x556378, 0x5563D0: its group, its icon's node, its name's node; the icon from
// the bank 984c8c by the code of the pair {id, icon} at 0x555E40, 0x555E70); the cursors at +0x33A (primary),
// +0x33E (secondary), the slots shown at +0x338, +0x33C; LEFT and RIGHT move a cursor (wrapping) then call 0x3CBB08
// (a0 the menu, a1 the row, a2 1: the move's sound), which highlights the cursor's slot and names its weapon (the
// row's title, 0x556368); OK takes each cursor's weapon's bit (0x3CC360, 0x3CC3A8: the n-th allowed bit, through
// 0x3CBA78 for the lists' index). Here the primary and secondary rows are the allowed weapons each followed by the
// weapons added to it (the FAMAS after the AK-47, the PSG1 after the SVD), shown 7 at a time: RIGHT past the last
// slot shows the next one, LEFT before the first the one before (the row goes round at its ends); the choice of an
// added weapon is its base's bit for the game, and this player's added choice (gAddedChoice) for MGO1.exe
struct MenuEntry { int id = 0, bit = 0, added = -1; uint32_t icon = 0; };   // added: its rank in ExtraMapsWeapons()
struct MenuRow { std::vector<MenuEntry> e; int window = 0, cursor = 0; };
MenuRow gMenuRows[2];
uint32_t gMenuWork = 0;
uint32_t CallPage(const Ctx& c, uint32_t us, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    ps2::Fn f = ps2::FindFn(V3(us));
    if (!f) { if (Once(((uint64_t)98 << 40) | us)) Log("added weapons: the page's function %08x not found", us); return 0; }
    Ctx x = c;
    x.r[4] = a0; x.r[5] = a1; x.r[6] = a2; x.r[7] = a3;
    f(x);
    return (uint32_t)x.r[2];
}
// a stock weapon's short name (the weapons' table's +0: "AK-47", "Mk22"; the items' names may hold an added one's)
uint32_t StockWeaponName(int id) { return R32(N3Weapons() + 0x28 * (uint32_t)id); }
// ---- The added weapons' icons in the HUD (docs/CHARACTERS.md, The added weapons: the icons). The HUD's icons are
// loaded by US n003a 0x371BC0 into its 40 slots (0x2E4730, 8 each: +0 the icon's code, +4 1 once loaded; a slot's
// cell of the icons' picture in the GS, 4 across of 128 x 48): the items' in 0-7, then the weapons', then the 28 of
// the weapons' icons of 0x545F10 over 8-35 (every weapon's and its suppressed one's). The windows and a box look an
// icon up by its code at each draw (0x371B38 from a descriptor's +0xC; the suppressed M1911A1, Mk22, XM16E1 their
// items' 0x26, 0x27, 0x28): a code no slot holds is an empty box. An added weapon's icon is none of the 28: the free
// slots 36-39 hold this player's added weapons' icons (icon=, icon_suppressed=), loaded as the game loads its own
// (0x371AD0: the slot, the image 0x122F18(code | 0x69000000), the code), first for the weapons he holds now (the
// last taken first, every weapon's own icon before the suppressors'); an added icon he no longer holds keeps its
// slot until a held one needs it
uint32_t gHudIconKept = 0;                             // an icon TEAM SNEAKING keeps in the slots (tsne.h): never taken here
struct AddedHeld {
    uint32_t seq = 0, since = 0;                       // the weapons taken, counted; the count as this choice began
    uint32_t taken[0x40] = {};                         // each base's last taking (a choice's OK, a pickup; 0: none)
    bool menu = false, choice = false;                 // a choice's menu opened; its bases held until the list changes
    std::vector<uint16_t> list;                        // this player's weapons' list as the choice's OK found it
    std::vector<uint32_t> want;                        // the held icons the slots were last given for
    std::string shown;                                 // the slots 36-39 as last logged
};
AddedHeld gAddedHeld;
void AddedHeldReset() { gAddedHeld = AddedHeld(); }
// this player's weapons' list (the link variables' +0x5AC entries at +0x5B4, the R2 window's; 0 an empty entry,
// 0x1F..0x31 their weapon as 0x371BC0 reads them, 0x2FDD08)
std::vector<uint16_t> AddedWeaponList(const Ctx& c) {
    std::vector<uint16_t> v;
    const uint32_t lv = R32(V(0x23A678)) & 0x01ffffff;
    if (lv < 0x100000 || lv + 0x600 >= 0x02000000) return v;
    const uint32_t n = std::min<uint32_t>(R32(lv + 0x5AC), 16);
    for (uint32_t i = 0; i < n; ++i) {
        int code = (int16_t)R16(lv + 0x5B4 + 2 * i);
        if (code >= 0x1F && code <= 0x31) code = (int)CallPage(c, 0x2FDD08, (uint32_t)code);
        if (code > 0 && code < 0x40 && std::find(v.begin(), v.end(), (uint16_t)code) == v.end()) v.push_back((uint16_t)code);
    }
    return v;
}
// a weapon taken by this player (the choice's OK, a pickup): its base's icons first
void AddedTaken(const Ctx& c, int base, bool choice) {
    AddedHeld& h = gAddedHeld;
    if (base <= 0 || base >= 0x40) return;
    if (choice && (h.menu || !h.choice)) { h.menu = false; h.choice = true; h.since = h.seq; h.list = AddedWeaponList(c); }
    h.taken[base] = ++h.seq;
}
// the icons for the slots, in order: the weapons held now (from a choice's OK until his weapons' list changes, the
// spawn's, the bases taken since it began; else that list), the last taken first, their own icons then their
// suppressors'; then this player's other added weapons' (held: how many of them are held ones)
std::vector<std::pair<uint32_t, const AddedWeapon*>> AddedIconOrder(const Ctx& c, size_t& held) {
    AddedHeld& h = gAddedHeld;
    const int self = OwnSlot();
    const std::vector<uint16_t> list = AddedWeaponList(c);
    if (h.choice && list != h.list) h.choice = false;
    std::vector<int> now, rest;
    for (int b = 1; b < 0x40; ++b) {
        if (!AddedOf(self, b)) continue;
        const bool in = h.choice ? h.taken[b] > h.since : std::find(list.begin(), list.end(), (uint16_t)b) != list.end();
        (in ? now : rest).push_back(b);
    }
    auto later = [&](int a, int b) { return h.taken[a] > h.taken[b]; };
    std::stable_sort(now.begin(), now.end(), later);
    std::stable_sort(rest.begin(), rest.end(), later);
    std::vector<std::pair<uint32_t, const AddedWeapon*>> out;
    auto add = [&](uint32_t code, const AddedWeapon* w) {
        if (code && std::none_of(out.begin(), out.end(), [&](const auto& o) { return o.first == code; })) out.push_back({code, w});
    };
    for (int b : now) add(AddedOf(self, b)->icon, AddedOf(self, b));
    for (int b : now) add(AddedOf(self, b)->iconSuppressed, AddedOf(self, b));
    held = out.size();
    for (int b : rest) { add(AddedOf(self, b)->icon, AddedOf(self, b)); add(AddedOf(self, b)->iconSuppressed, AddedOf(self, b)); }
    return out;
}
void AddedIcons(const Ctx& c) {
    const int self = OwnSlot();
    const uint32_t slots = N3HudIcons();
    if (self < 0 || !slots) return;
    AddedHeld& h = gAddedHeld;
    size_t held = 0;
    const auto order = AddedIconOrder(c, held);
    h.want.clear();
    for (size_t i = 0; i < held; ++i) h.want.push_back(order[i].first);
    auto rank = [&](uint32_t code) {                     // its place in the order (-1: not one of this player's)
        for (size_t i = 0; i < order.size(); ++i) if (order[i].first == code) return (int)i;
        return -1;
    };
    auto ours = [&](uint32_t code) {                     // an added weapon's icon of this map
        return code && std::any_of(ExtraMapsWeapons().begin(), ExtraMapsWeapons().end(), [&](const AddedWeapon& w) { return code == w.icon || code == w.iconSuppressed; });
    };
    for (size_t i = 0; i < held; ++i) {
        const uint32_t code = order[i].first;
        const std::string& name = order[i].second->name;
        if (HudIconSlot(code) >= 0) continue;
        int pick = -1;                                   // a free slot
        for (int k = 36; k < 40 && pick < 0; ++k) if (!R16(slots + 8 * (uint32_t)k + 4)) pick = k;
        if (pick < 0)                                    // all taken: the one of the added icon last in the order
            for (int k = 36, best = -1; k < 40; ++k) {
                const uint32_t s = R32(slots + 8 * (uint32_t)k);
                if (s == gHudIconKept || !ours(s)) continue;
                const int r = rank(s);
                if (r >= 0 && r <= (int)i) continue;     // a held icon before this one
                const int key = r < 0 ? 1 << 30 : r;     // (none of this player's: first)
                if (key > best) { pick = k; best = key; }
            }
        const uint32_t was = pick >= 0 && R16(slots + 8 * (uint32_t)pick + 4) ? R32(slots + 8 * (uint32_t)pick) : 0;
        const uint32_t img = pick >= 0 ? CallPage(c, 0x122F18, code | 0x69000000u) : 0;
        if (!img) {
            if (Once(((uint64_t)104 << 40) | ((uint64_t)(h.since & 0xFFFF) << 24) | code))
                Log("added weapons: the %s's icon %06x not loaded in the HUD (%s)", name.c_str(), code,
                    pick >= 0 ? "no such image in the page" : "the slots 36-39 hold the icons of the weapons held now: its base's shown");
            continue;
        }
        CallPage(c, 0x371AD0, (uint32_t)pick, img, code);
        char in[48] = "";
        if (was) snprintf(in, sizeof in, ", in place of %06x", was);
        Log("added weapons: the %s's icon %06x loaded in the HUD's slot %d (%s%s)", name.c_str(), code, pick, HudIconSlot(code) == pick ? "done" : "not taken", in);
    }
    std::string shown, now;
    for (int k = 36; k < 40; ++k) {
        char b[16]; snprintf(b, sizeof b, " %06x", R16(slots + 8 * (uint32_t)k + 4) ? R32(slots + 8 * (uint32_t)k) : 0u); shown += b;
    }
    for (size_t i = 0; i < held; ++i) { char b[16]; snprintf(b, sizeof b, " %06x", order[i].first); now += b; }
    if (shown != h.shown) {
        h.shown = shown;
        Log("added weapons: the HUD's slots 36-39:%s (the icons of the weapons held now:%s; %s)", shown.c_str(), now.empty() ? " none" : now.c_str(),
            h.choice ? "the choice's" : "his weapons' list");
    }
}
// each pass: the slots given again once the icons of the weapons held now change (a pickup, the spawn's list, a drop)
void AddedIconsTick(const Ctx& c) {
    if (OwnSlot() < 0 || !N3HudIcons()) return;
    size_t held = 0;
    const auto order = AddedIconOrder(c, held);
    if (held != gAddedHeld.want.size() || !std::equal(gAddedHeld.want.begin(), gAddedHeld.want.end(), order.begin(),
                                                      [](uint32_t a, const auto& o) { return a == o.first; })) AddedIcons(c);
}
void MenuRender(const Ctx& c, uint32_t menu, int row) {
    MenuRow& r = gMenuRows[row];
    const uint32_t layout = R32(menu + 0x40), slots = N3MenuSlots(row);
    const int shown = std::min<int>(7, (int)r.e.size());
    const auto& list = ExtraMapsWeapons();
    const std::string lang = AddedTextLang();
    for (int k = 0; k < 7; ++k) {
        const uint32_t group = R32(slots + 12 * (uint32_t)k), iconNode = R32(slots + 12 * (uint32_t)k + 4), nameNode = R32(slots + 12 * (uint32_t)k + 8);
        if (k >= shown) { CallPage(c, 0x3C70C8, layout, group); continue; }
        const MenuEntry& e = r.e[(size_t)(r.window + k)];
        const AddedWeapon* w = e.added >= 0 && e.added < (int)list.size() ? &list[(size_t)e.added] : nullptr;
        CallPage(c, 0x3C7090, layout, group);
        CallPage(c, 0x3C7110, layout, nameNode, w ? AddedString(AddedWeaponText(*w, false, lang)) : StockWeaponName(e.id), 0);
        if (w && w->choiceBank && w->choiceIcon) CallPage(c, 0x3C8008, layout, iconNode, w->choiceBank, w->choiceIcon);
        else CallPage(c, 0x3C8008, layout, iconNode, 0x984C8C, e.icon);
    }
    W16(menu + (row ? 0x33C : 0x338), (uint16_t)shown);
    W16(menu + (row ? 0x33E : 0x33A), (uint16_t)r.cursor);
}
// the rows as the game built them (0x3CBDC8 run), made again with the added weapons
void MenuBuild(const Ctx& c, uint32_t menu) {
    const auto& list = ExtraMapsWeapons();
    const uint32_t mask = R32(menu + 0x334), chosen = R32(CallPage(c, 0x3C6DE0, 0) + 0xF8);
    const int self = OwnSlot();
    gMenuWork = menu;
    gAddedHeld.menu = true;                              // a new choice: its OK takes its weapons
    gAddedPassMs = 0;
    SimTickLogFrom("the choice's menu");
    for (int row = 0; row < 2; ++row) {
        MenuRow& r = gMenuRows[row];
        const int first = row ? 6 : 0, count = row ? 3 : 6;
        const uint32_t pairs = N3MenuPairs(row);
        r.e.clear();
        int pick = -1;
        for (int i = 0; i < count; ++i) {
            if (!(mask >> (first + i) & 1)) continue;
            const int id = (int)R32(pairs + 8 * (uint32_t)i);
            const int mine = self >= 0 && self < 32 && id > 0 && id < 0x40 ? gMenuPick[id] : 0;   // the menu's own choice
            if ((chosen >> (first + i) & 1) && !mine) pick = (int)r.e.size();
            r.e.push_back({id, first + i, -1, R32(pairs + 8 * (uint32_t)i + 4)});
            for (size_t k = 0; k < list.size(); ++k)
                if (list[k].base == id && list[k].snake != 2) {   // snake=only: Snake's class alone (#120)
                    if ((chosen >> (first + i) & 1) && mine == (int)k + 1) pick = (int)r.e.size();
                    r.e.push_back({id, first + i, (int)k, list[k].choiceIcon});
                }
        }
        const int n = (int)r.e.size(), shown = std::min(7, n);
        if (pick < 0) pick = 0;
        r.window = std::max(0, std::min(pick - shown / 2, n - shown));
        r.cursor = n ? pick - r.window : 0;
        MenuRender(c, menu, row);
        CallPage(c, 0x3CBB08, menu, (uint32_t)row, 0);
    }
    Log("added weapons: the choice's rows made again: %zu primary weapons, %zu secondary (%zu added in all)",
        gMenuRows[0].e.size(), gMenuRows[1].e.size(), list.size());
}
// 0x3CBB08 (a0 the menu, a1 the row, a2 1 after a move): the row's window moved when the game's cursor went round its
// slots while more weapons are before or after them; the cursor's weapon named
void MenuRefresh(Ctx& c) {
    const uint32_t menu = (uint32_t)c.r[4];
    const int row = (int)(uint32_t)c.r[5];
    if (menu != gMenuWork || row < 0 || row > 1) return;
    MenuRow& r = gMenuRows[row];
    const int n = (int)r.e.size(), shown = std::min(7, n);
    if (!n) return;
    const int now = R16(menu + (row ? 0x33E : 0x33A));
    if ((uint32_t)c.r[6] == 1 && n > shown) {
        const int was = r.cursor;
        if (was == shown - 1 && now == 0) {               // RIGHT at the last slot: the next weapon, or the first
            if (r.window + shown < n) { ++r.window; r.cursor = shown - 1; } else { r.window = 0; r.cursor = 0; }
            MenuRender(c, menu, row);
        } else if (was == 0 && now == shown - 1) {        // LEFT at the first slot: the one before, or the last
            if (r.window > 0) { --r.window; r.cursor = 0; } else { r.window = n - shown; r.cursor = shown - 1; }
            MenuRender(c, menu, row);
        } else r.cursor = now;
    } else r.cursor = std::min(now, shown - 1);
}
// after 0x3CBB08: the row's title (the cursor's weapon) named as the entry is
void MenuTitle(const Ctx& c, uint32_t menu, int row) {
    if (menu != gMenuWork || row < 0 || row > 1) return;
    const MenuRow& r = gMenuRows[row];
    const size_t at = (size_t)(r.window + r.cursor);
    if (at >= r.e.size()) return;
    const MenuEntry& e = r.e[at];
    const auto& list = ExtraMapsWeapons();
    const AddedWeapon* w = e.added >= 0 && e.added < (int)list.size() ? &list[(size_t)e.added] : nullptr;
    const uint32_t title = R32(N3MenuTitles() + 4 * (uint32_t)row);
    CallPage(c, 0x3C7110, R32(menu + 0x40), title, w ? AddedString(AddedWeaponText(*w, false, AddedTextLang())) : StockWeaponName(e.id), 0);
}
// the entry at a row's cursor: its list index for the game (0x3CBA78) and its bit at OK (0x3CC360, 0x3CC3A8)
const MenuEntry* MenuAt(uint32_t menu, int row) {
    if (menu != gMenuWork || row < 0 || row > 1) return nullptr;
    const MenuRow& r = gMenuRows[row];
    const size_t at = (size_t)(r.window + (int)R16(menu + (row ? 0x33E : 0x33A)));
    return at < r.e.size() ? &r.e[at] : nullptr;
}
// the weapons' factory (0x45D770, a0 the player's weapon component, a1 the item's code): the player it makes the
// weapon for, for the base's init it runs (AddedModelName). A weapon's change asks its stance (0x2FCB60, the player
// known there) right before it makes the weapon (0x3FCA98 after 0x3FCE78 and 0x3FD070, 0x31AFB8 after 0x31B3C8):
// that player when the item is his weapon; else the actor holding the component's handle, if one does
int gFactoryOwner = -1, gEquipSlot = -1, gEquipId = -1;
int gFactoryCode = -1, gFactoryDepth = 0;                // the factory's item while it runs (its weapon's actor named then)
int gLocalWeaponsMade = 0;                               // weapons made for this program's player (tests: his spawns)
int gBoxesTaken = 0;                                    // weapons of added weapons' bases taken from boxes (tests)
// the weapons' actors (NewWeaponAkm ...) made by the factory: whose they are and which weapon (the added weapons'
// sounds: an effect played in such an actor's act is that player's weapon's)
struct WeaponActor { int owner = -1, id = 0; };
std::map<uint32_t, WeaponActor> gWeaponActors;
int ComponentOwner(uint32_t comp) {
    if (comp < 0x100000 || comp >= 0x01FFFF00) return -1;
    const uint32_t lo = R32(comp + 0x18), hi = R32(comp + 0x1C);
    static std::map<std::pair<uint32_t, uint64_t>, int> known;   // (component, handle) -> slot
    const auto key = std::make_pair(comp, (uint64_t)hi << 32 | lo);
    const auto k = known.find(key);
    if (k != known.end() && PlayerOfSlot(k->second)) return k->second;
    for (const auto& a : gNativeActors) {
        if (a.second != "NewNetworkPlayer" && a.second != "NewNetworkDummyPlayer") continue;
        for (uint32_t off = 0; off + 8 <= 0x1000; off += 4)
            if (R32(a.first + off) == lo && R32(a.first + off + 4) == hi) {
                const int slot = R8(a.first + 0xB60);
                known[key] = slot;
                if (Once(((uint64_t)95 << 40) | off)) Log("added weapons: a weapon component's handle held at +%#x of its player's actor", off);
                return slot;
            }
    }
    return -1;
}
// GV_StrCode (0x123150) called by a base weapon's init (the names of its model and magazine): the added weapon's
// names for the owner holding one; 0 when not
uint32_t AddedModelName(Ctx& c) {
    struct Init { uint32_t start; int base; const char* model; const char* magazine; };
    static const Init kInits[] = {                         // US n003a: each init from its start, 0x1C0 bytes at most
        {0x4E6EB8, 0x05, "mk22", "mk22_amo"}, {0x4CF250, 0x06, "gave", "gave_amo"}, {0x4D52F8, 0x0A, "vz61", "vz61_amo"},
        {0x4D08A8, 0x0B, "m16a", "m16a_amo"}, {0x4D68A8, 0x0C, "akmr", "akmr_amo"}, {0x4D33C0, 0x0F, "svdd", "svdd_amo"}};
    const uint32_t ra8 = (uint32_t)c.r[31] - 8, s = (uint32_t)c.r[4] & 0x01ffffff;
    if (s < 0x100000 || s + 16 > 0x02000000) return 0;
    for (const Init& in : kInits) {
        const uint32_t start = V3(in.start);
        if (!start || ra8 < start || ra8 >= start + 0x1C0) continue;
        const AddedWeapon* w = AddedOf(gFactoryOwner, in.base);
        if (!w) return 0;
        const bool model = !strcmp((const char*)gMem + s, in.model), magazine = !strcmp((const char*)gMem + s, in.magazine);
        const uint32_t name = model ? w->model : magazine ? w->magazine : 0;
        if (name && Once(((uint64_t)96 << 40) | ((uint64_t)gFactoryOwner << 32) | name))
            Log("added weapons: player %d's %s made with %06x (its %s)", gFactoryOwner + 1, w->name.c_str(), name, model ? "model" : "magazine");
        return name;
    }
    return 0;
}
// the stance archive of a weapon asked for a player at his weapon's change (0x2FCB60's four callers): an added
// weapon's own; 0 when not
uint32_t AddedStance(Ctx& c) {
    const uint32_t ra8 = (uint32_t)c.r[31] - 8;
    uint32_t obj = 0;
    if (ra8 == V3(0x3FCE78) || ra8 == V3(0x3FD070) || ra8 == V3(0x49EE68)) obj = (uint32_t)c.r[16];
    else if (ra8 == V3(0x31B3C8)) obj = (uint32_t)c.r[18];
    const int slot = PlayerSlotOf(obj), base = (int)(uint32_t)c.r[4] & 0xFF;
    gEquipSlot = slot; gEquipId = base;                   // the weapon made next is his (the factory)
    const AddedWeapon* w = AddedOf(slot, base);
    if (!w || !w->stance) return 0;
    if (Once(((uint64_t)97 << 40) | ((uint64_t)slot << 32) | w->stance)) Log("added weapons: player %d's %s with its stance %06x", slot + 1, w->name.c_str(), w->stance);
    return w->stance;
}
// the game chosen in the host list (ntmenu2 0x35B5B8, the join's check that it is not full; a0 its work: + 0x2496
// the line chosen, + 0x1000 the games' ids, + 0x40 the list; 0x197E58 an id's entry: + 0x1A its maximum of
// players, + 0x1D its players): a big game is joined in the PCs' dialect from the first connect, its
// channels in their place from then; a game of 8 in the regions' dialects
void JoinChosen(Ctx& c) {
    if (SimPlainPs2()) return;
    NetJoinReset();                                    // netcode: a new join's connects counted (netfix.h)
    const uint32_t w = (uint32_t)c.r[4] & 0x01ffffff;
    if (w < 0x100000 || w + 0x2498 > 0x02000000) return;
    const uint32_t line = R16(w + 0x2496);
    if (line >= 0x400) return;
    Ctx x = c;
    x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
    x.r[31] = V(0x1005f0);
    x.r[4] = R32(w + 0x40); x.r[5] = R32(w + 0x1000 + 4 * line);
    const uint32_t id = (uint32_t)x.r[5];
    Call(x, V(0x197e58));
    const uint32_t e = (uint32_t)x.r[2] & 0x01ffffff;
    if (e < 0x100000 || e + 0x20 > 0x02000000) return;
    const uint32_t most = R8(e + 0x1a);
    JoinHintFrom(R8(e + 0x1c));                       // open list: the extra maps the host list tells (openfix.h)
    // a game of the PCs from the list's hint (v141-host beta 5: more than 8 players, maps 13 and up, TEAM SNEAKING, BASE
    // CAPTURE: the host's own choice, openfix.h) or from its maximum: more than 8 players, a Host Only game's host not
    // counted (netcode: its list shows one more, netfix.h). Else not known: every set tried
    const int set = gJoinHintSet >= 2 || NetBigChosen(most, R8(e + 0x16)) ? 2 : -1;
    static uint32_t lastId = 0xffffffff;
    if (id != lastId || set != gJoinSet) Log("session: the game %u chosen holds %u players at most%s", id, most, set == 2 ? ": joined in the PCs' dialect" : "");
    // a join from the lobby: its host's set learnt again. The same game tried again goes on with the sets not tried
    // yet, its host's learnt one first (a join sends some 8 connects: fewer than the sets with three extra maps and
    // TEAM SNEAKING's)
    if (id != lastId || set != gJoinSet) gRegionTry.clear();
    else {
        const int own = !gUsProgram && !strcmp(ps2::SelectedVersion(), "EU") ? 1 : 0;
        const std::vector<int> cands = JoinCandidates(own);
        for (const auto& pr : gPeerRegion) {
            const auto it = std::find(cands.begin(), cands.end(), pr.second);
            if (it != cands.end()) gRegionTry[pr.first] = (uint32_t)(it - cands.begin());
        }
    }
    gPeerRegion.clear();
    lastId = id; gJoinSet = set;
    gGameSet = 2;
    // (v141-host) a big game whose host list tells its extra maps or rules (a dedicated host's game of 9, a game of more
    // than 8 with TEAM SNEAKING): its set is the hint's, not the PCs' plain one, else its host's answers never verify
    // (SessionKeys: a big game's joiner checks them in gGameSet alone) and the join fails (1059)
    if (set == 2 && gJoinHintSet >= 2) gGameSet = gJoinHintSet;
    if (set == 2) BigGameOn("the game chosen");
    else if (gBigGame) { gBigGame = false; Log("session: a game of %u players at most chosen: its dialect learnt from its host", most); }
}
void BigGameChannel(Ctx& c) {                        // 0x174790 (a0: the channel, a1: its id)
    const uint32_t id = (uint32_t)c.r[5] & 0xffff, ra = (uint32_t)c.r[31];
    // a player's own channels stay: players 10 to 15 have 0x5E..0x6F (the network player opens them at its
    // + 0x934 + 0x68 k, the call's delay slot `addiu a0, a0, 0x934` in every map and ntmenu2 of the five)
    if (ra >= 0x100004 && ra < 0x02000000 && R32(ra - 4) == 0x24840934) return;
    if (id >= 0x60 && id <= 0x70 && !NativeChanFamily()) {
        c.r[5] = id + 0x10;
        static std::set<uint32_t> said;
        if (said.insert(id).second) Log("session: the game channel %#x opened at %#x (%s)", id, id + 0x10, gBigGame ? "a big game" : "tests: an EU or KR PS2's");
    }
}
void BigGameEnd() {                                  // the lobby's page: no game
    gPeerRegion.clear(); gRegionTry.clear(); gPeerChanFamily.clear(); gPeerBootAt.clear();
    gNetProbe.clear();                                 // netcode: the stock joiners' places (netfix.h)
    gJoinSet = -1; gGameSet = 2;
    gJoinHintSet = -1;                                 // open list: no game chosen (openfix.h)
    if (!gBigGame) return;
    gBigGame = false;
    Log("session: back in the lobby: the game's own dialects and channels");
}
int ServersChanFamily() {
    const char* v = ps2::SelectedVersion();
    return v && !strcmp(v, "EU") ? 1 : 0;
}
int RecordChanFamily(const uint8_t* r) {             // -1: not a game channel, or 0x70 (either family's)
    if (r[1] & 7) return -1;
    return r[0] >= 0x60 && r[0] <= 0x6F ? 0 : r[0] >= 0x71 && r[0] <= 0x80 ? 1 : -1;
}
bool OnFamily(const uint8_t* r, int family) {        // a record on the game channels of that family
    const uint32_t lo = family ? 0x70 : 0x60;
    return !(r[1] & 7) && r[0] >= lo && r[0] <= lo + 0x10;
}
template <class F> bool ForRecords(uint8_t* p, size_t n, F fn) {
    size_t i = 0;
    while (i + 3 <= n) {
        const uint8_t fl = p[i + 1];
        const size_t hdr = 3 + (fl & 0x20 ? 1 : 0) + (fl & 0x10 ? 1 : 0);
        if (i + hdr > n) return false;
        const size_t len = fl & 0x20 ? (size_t)(p[i + 2] | p[i + 3] << 8) : p[i + 2];
        if (i + hdr + len > n) return false;
        fn(p + i);
        i += hdr + len;
    }
    return i == n;
}
void ChannelFamilies(bool out, uint32_t session, uint32_t buf, uint32_t len) {
    buf &= 0x01ffffff; session &= 0x01ffffff;
    if (len <= 2 || buf + len > 0x02000000 || session + 0x18 > 0x02000000) return;
    // a big game: every peer a PC with its game objects at 0x70..0x80, its players 10 to 15 at 0x5E..0x6F
    // (not a family to learn)
    if (gBigGame) return;
    uint8_t* p = gMem + buf + 2;
    const size_t n = len - 2;
    const uint64_t peer = PeerOf(session);
    const int own = OwnChanFamily();
    if (!out) {
        int seen = -1;
        ForRecords(p, n, [&](uint8_t* r) { if (seen < 0) seen = RecordChanFamily(r); });
        if (seen >= 0) {
            auto it = gPeerChanFamily.find(peer);
            if (it == gPeerChanFamily.end() || it->second != seen) {
                gPeerChanFamily[peer] = seen;
                Log("session: peer %s sends on the game channels of the %s programs%s", PeerText(peer).c_str(),
                    seen ? "EU and KR" : "US, JP and JPS", seen != own ? ": its records moved to this program's" : "");
                NetChanLearnt(peer, seen);                     // netcode: a stock joiner's place found (netfix.h)
            }
        }
    }
    auto it = gPeerChanFamily.find(peer);
    // netcode (netfix.h): a stock joiner of the US dialect probed in both places, an EU dialect peer in the EU one
    if (it == gPeerChanFamily.end() && NetChanProbe(out, peer, PeerRegion(peer), IsSessionHost(), p, n, own)) return;
    const int wire = it != gPeerChanFamily.end() ? it->second : NetChanSeed(PeerRegion(peer), ServersChanFamily());
    if (wire == own) return;
    const int from = out ? own : wire, shift = (out ? wire - own : own - wire) * 0x10;
    ForRecords(p, n, [&](uint8_t* r) { if (OnFamily(r, from)) r[0] = (uint8_t)(r[0] + shift); });
}
// ---- the players' records across the two protocols (docs/VERSIONS.md section 3). The JP and JPS
// programs speak the older protocol of the players' channels (0x40 + 3 x slot + kind), the US, EU and
// KR programs the newer one. Three records differ:
// - 03 (kind 0: the stun gauge and an event byte, its second data byte): a knocked out player sends 3
//   in the older protocol, 4 in the newer one, where 3 is an event of its own. A newer program took an
//   older player's 3 for that event and never played his lying down: the knocked out Japanese player
//   fell and got up again on North American screens (the stun loop). The stun component checks the
//   byte: US 0x441690 (== 4), JP 0x432F30 (== 3), JPS 0x433370 (3), EU 0x442700 (4), KR 0x441848 (4).
// - 07 (kind 1): the newer protocol's one-byte form (high nibble 1, on an attacker's channel) is unknown
//   to the older one (its down state taken as 0x1x): moved to an unopened channel for an older program.
// - 0F (kind 1: a player on a ladder, sent each frame he climbs; LadderUniversal).
// A player's protocol is learnt from its records: a 4 in its 03 (newer), a 3 in its 03 while its 0A
// record says it is knocked out (2, 4, 5: older) or while it is up (newer), a 0F record of 8 bytes
// (older, but the both-ways form a PC sends) or of 4 (newer). Received records are put in this program's
// protocol; sent ones in the receiver's once known (the knocked out event of an older program as the
// newer 4 until then: a newer receiver needs it, an older one plays the lying down after its wait
// instead of at once; a ladder's record in the form both read).
int gSlotProto[32], gSlotDown[32];                   // per player slot: -1 unknown, 0 older, 1 newer; its 0A state
bool gRelayFlag[32];                                 // per player slot: the flag of his last own 0F (a newer host relays it)
uint32_t gSlotSession[32];                           // the session of that player id when learnt: another one, learnt again
bool gSlotProtoInit = false;
int OwnProto() {
    const char* v = ps2::SelectedVersion();
    return v && (!strcmp(v, "JP") || !strcmp(v, "JPS")) ? 0 : 1;
}
const char* ProtoPrograms(int p) { return p ? "US, EU and KR" : "JP and JPS"; }
int PeerSlot(uint32_t session) {                      // the player id of a session (the player table, V(0x28B5F4))
    const uint32_t table = V(0x28B5F4);
    for (int k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * k;
        if (R16(e + 2) && R32(e + 4) == session) return R8(e);
    }
    return -1;
}
// proto 2: newer, from an event 3 while up only (a knocked out record lost on the way): an event 3 while
// knocked out later still makes it older
void SetSlotProto(int slot, int proto, const char* why) {
    if (slot < 0 || slot >= 32) return;
    const int was = gSlotProto[slot];
    if (was == proto || (proto == 2 && was >= 0) || (proto == 0 && was == 1)) return;
    gSlotProto[slot] = proto;
    if (was >= 1 && proto >= 1) return;
    Log("session: player %d's records come in the players' protocol of the %s programs (%s)%s", slot + 1, ProtoPrograms(proto != 0), why,
        (proto != 0) != (OwnProto() != 0) ? ": put in this program's" : "");
}
bool DownState(int d) { return d == 2 || d == 4 || d == 5; }         // 0A: knocked out, lying, getting up
int OwnSlot() {                                        // this program's player id (flag 2 in the player table)
    const uint32_t table = V(0x28B5F4);
    for (int k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * k;
        if (R16(e + 2) && (R32(e + 8) & 2)) return R8(e);
    }
    return -1;
}
// the players' channels end where the game objects' begin (0x60): a record of the room state (0x62) is
// not a player's
template <class F> void ForPlayerRecords(uint8_t* p, size_t n, F fn) {   // fn(record, data, length, slot, kind)
    ForRecords(p, n, [&](uint8_t* r) {
        const uint32_t ch = r[0] | (r[1] & 7) << 8;
        if (ch < 0x40 || ch >= PlayersChanEnd()) return;
        const size_t hdr = 3 + (r[1] & 0x20 ? 1 : 0) + (r[1] & 0x10 ? 1 : 0);
        const size_t len = r[1] & 0x20 ? (size_t)(r[2] | r[3] << 8) : r[2];
        if (len) fn(r, r + hdr, len, (int)(ch - 0x40) / 3, (int)(ch - 0x40) % 3);
    });
}
// 0F, a player on a ladder (the ladder component 0xEBB252 of each player; the ladders a map's script
// sets up, [d4d8a5]: up to 8, 64 bytes each), sent every frame he climbs (US n001a 0x4001A0, JP
// 0x3F16E0; off the ladder 0x3F1998). The older protocol: 8 bytes, the ladder's index (-1 off) and the
// climber's x, y, z in tenths (JP handler 0x43BF70; its receivers read y alone: JP 0x43CD34, 0x43CE6C).
// The newer: 4 bytes, the index (8 off) with a flag in bit 7 (his copy climbs and runs its exits at half
// speed with it, LadderSpeed: US n001a 0x44B508) and his y alone (US handler 0x44A6D0). A newer program read an older climber's x as his
// height and took his -1 (off) as the index 127: the Japanese player stayed at a ladder's first rung on
// the other screens, then jumped to its end (Snake, 27 September). An older program read 7 bytes of a
// newer one's 4: his height as x, two stale words of its stack as y and z, and the index 8 (off) out
// of its table of 8.
// The form both read (a PC sends it): 8 bytes, the index or 0xFF (off: 127 to a newer program, -1 to an
// older one), y at +2 (the newer height) and at +4 (the older y), 0x8000 as z (an older program keeps x
// and z unread), 0x8001 with a newer climber's flag (an older program takes bit 7 of the index for off:
// a newer PC puts the flag back on receipt), bit 1 of z when the older y is held under the top's exit
// for older PS2s (the climber's own exit at half speed: LadderRecordsGrow; the newer height at +2 stays
// his), bit 2 when a newer program wrote it (an older PC gives its copy the newer speeds: LadderSpeed). A
// record in that form without bit 2 is no evidence of the older protocol (a newer host relays what its
// handler writes, an older one the bytes as they came). Off, each handler looks its index up when the
// copy is busy (byte +0x62, the top's exit:
// newer index & 0x7F, older signed) and takes a nonzero word there for a ladder: a newer PC makes 0xFF
// its own 8 on receipt (127 reads 8 KB past the table of 8), an older PC sends 8 to a newer player it
// knows; a newer PS2 not known yet reads 127 (seen harmless in the top's exit: US n033a 0x569EF0).
bool LadderUniversal(const uint8_t* d) {                 // z 0x8000..0x8007: bit 0 the flag, 1 the older y held, 2 a newer writer
    return (d[6] & 0xf8) == 0x00 && d[7] == 0x80 && ((d[6] & 2) || (d[2] == d[4] && d[3] == d[5]));
}
// an older program's copies of the players on a ladder (LadderCopies, LadderHold below)
struct LadderCopy {                                     // newer: his records written by a newer program; up: his height not falling
    uint32_t player = 0, comp = 0; bool flag = false, newer = false, on = false, up = false; int state = 0; long long since = 0; uint8_t idx = 0; int16_t lastH = 0;
};
LadderCopy gLadderCopy[32];
void MakeLadderUniversal(uint8_t* d) {
    d[1] = (int8_t)d[1] < 0 || d[1] >= 8 ? 0xff : d[1];
    d[2] = d[4]; d[3] = d[5];
    d[6] = 0x00; d[7] = 0x80;
}
// ---- the Dead Body glitch (docs/GAME_FIXES.md). A player's respawn reaches the others in one record,
// sent once: 02 on his channel 0x40 + 3 x slot (the spawn command ee8, ee9, eea, the spawn point in
// tenths, the heading: n001a 0x413E18), which the host relays to everyone, him included. A datagram lost
// on a poor link to the host left him dead on every screen but his own while he played on, and the
// retail game never sends it again. The platform (MGO1_SPAWN_FIX=0: the retail behaviour, for tests):
// - sends this player's spawn again until the host sends it back (every 500 ms, 4 times at most);
//   every spawn record the game sends is a new spawn, also one with the same bytes as the last (the
//   same spawn point and heading: v137 watched only a record unlike the last, and db138 lost such a
//   second spawn for good); the platform's own sends are told apart by their count (own), each seen
//   going out within 3 s;
// - respawns a player it sees dead while his positions come from 3 m away from his body for
//   700 ms, where he is (the command the 02 handler 0x413C50 stores: ee8 = 1, the point at +0xED0),
//   and, as the host, sends that spawn to the others.
struct MySpawn { uint8_t rec[16] = {}; int len = 0; ULONGLONG t = 0, ownT = 0; int resends = 0, own = 0; bool open = false, lastOwn = false; } gMySpawn;
// tests: SIM_SPAWN_LOST=1 (a joiner): every spawn record the game sends takes the point and heading of
// his first one (the same bytes each time) and is dropped on its way (a channel no one opened): only the
// platform's own sends carry his spawns to the host. SIM_SPAWN_OLD=1: v137's watch (a spawn record the
// same as the last one not watched), for A/B runs
bool SimSpawnLost() { static const bool on = getenv("SIM_SPAWN_LOST") && atoi(getenv("SIM_SPAWN_LOST")) > 0; return on; }
bool SimSpawnOld() { static const bool on = getenv("SIM_SPAWN_OLD") && atoi(getenv("SIM_SPAWN_OLD")) > 0; return on; }
// the position and the input a player's copies have: the last 00 and 01 records this player sent
// (docs/GAME_FIXES.md, a player at rest). The game sends them from its moving states only (00: US
// n001a 0x439740, 0x448244, 0x451800): a player who got to where he stands without walking (his
// spawn's placement, a blow) stayed elsewhere on the other screens until he moved, and one who went
// from a walk straight into aiming walked on there, his last input still held by his copies
struct MyPos { float p[3] = {}; bool known = false, rest = false; ULONGLONG t = 0; } gMyPos;   // rest: sent at rest since
struct MyInput { uint8_t rec[7] = {}; bool moving = false; ULONGLONG t = 0, ladder = 0; } gMyInput;   // ladder: his last 0F
// per slot: where his body lies (his player seen dead), positions from far from it since, the last one's time,
// place and distance
struct SlotAlive { uint32_t obj = 0; bool wasAlive = false, body = false; float bodyPos[3] = {}; ULONGLONG movedSince = 0, lastFar = 0; float pos[3] = {}; float away = 0; } gSlotAlive[32];
const float kBodyFar = 3000.f;                            // 3 m: a spawn point, not a body pushed or a late position
std::vector<std::pair<int, std::array<float, 3>>> gHostSpawns;   // slots whose spawn the host sends at the next pass
// a player's copy at rest (docs/GAME_FIXES.md, a player at rest): the game's copy stands where its own
// motion left it once his input stops, and only its standing action (US n001a 0x43A708) goes to his
// position record (+0xE00) when within 600; aiming, or still in a moving state or a blow's, it stays
// away (up to 3000, where the game puts it there). A PC places it there when a position came after his
// last moving input: the one his PC sends at rest (below), or a later one. Walking while aiming (his style
// record's bits 0x70) the game moves a copy by his input only, turning it toward his position beyond 600
// (US n001a 0x455348, 0x43A708): it kept the distance it had, up to 1300 at a walk. A PC brings it an
// eighth nearer a pass while his positions come, as the game does for a copy walking otherwise. Per
// slot: the times of his last position, moving input and ladder records
struct SlotNet { ULONGLONG pos = 0, moving = 0, ladder = 0; uint32_t obj = 0; ULONGLONG real = 0; };   // obj: his copy they came to
// (v150-host beta 21, cover) real: his last position record's arrival on the machine's clock (GetTickCount64, not scaled), for
// the virtual dolls' bodies (VdollSee: how old the position he really sent is)
SlotNet gSlotNet[32];
bool CopyRestOn() { static const bool off = getenv("MGO1_COPY_REST") && !strcmp(getenv("MGO1_COPY_REST"), "0"); return !off; }
bool WalkAimOn() { static const bool off = getenv("MGO1_COPY_AIMWALK") && !strcmp(getenv("MGO1_COPY_AIMWALK"), "0"); return !off; }
bool SpawnFixOn() { static const bool off = getenv("MGO1_SPAWN_FIX") && !strcmp(getenv("MGO1_SPAWN_FIX"), "0"); return !off; }
bool IsSessionHost() { return (R32(V(0x28BBCC)) & 1) != 0; }       // 0x173840
uint32_t PlayerOfSlot(int slot) {                         // the player object of a slot (the actors named at creation)
    for (const auto& a : gNativeActors)
        if ((a.second == "NewNetworkPlayer" || a.second == "NewNetworkDummyPlayer") && R8(a.first + 0xb60) == (uint32_t)slot) return a.first;
    return 0;
}
float F32(uint32_t a) { const uint32_t u = R32(a); float f; memcpy(&f, &u, 4); return f; }
void SpawnRecords(bool out, uint8_t* d, size_t l, int slot, int kind, int self) {
    if (!SpawnFixOn() || self < 0) return;
    const ULONGLONG now = PsMs();
    if (out && kind == 1 && d[0] == 0x0f && slot == self) gMyInput.ladder = now;   // on a ladder: his input is its own
    if (!out && slot >= 0 && slot < 32 && slot != self) {  // another player's position, moving input, ladder
        SlotNet& n = gSlotNet[slot];
        if (kind == 1 && d[0] == 0x0f) n.ladder = now;
        if (kind == 0 && d[0] == 0x00 && l >= 13) { n.pos = now; n.real = GetTickCount64(); }
        if (kind == 0 && d[0] == 0x01 && l >= 7) for (int k = 1; k < 7; ++k) if (d[k]) { n.moving = now; break; }
    }
    if (kind != 0) return;
    if (out && d[0] == 0x00 && l >= 13 && slot == self) {  // this player's position as his copies have it
        for (int k = 0; k < 3; ++k) memcpy(&gMyPos.p[k], d + 1 + 4 * k, 4);
        gMyPos.known = true; gMyPos.rest = false; gMyPos.t = now;
    }
    if (out && d[0] == 0x01 && l >= 7 && slot == self) {   // and his input
        memcpy(gMyInput.rec, d, 7);
        gMyInput.moving = false; for (int k = 1; k < 7; ++k) if (d[k]) gMyInput.moving = true;
        gMyInput.t = now;
    }
    if (d[0] == 0x02 && slot == self && l <= sizeof gMySpawn.rec) {
        const bool same = gMySpawn.len == (int)l && !memcmp(gMySpawn.rec, d, l);
        if (out) {
            gMySpawn.lastOwn = false;
            if (!d[1] || IsSessionHost()) return;
            if (same && gMySpawn.own > 0 && now - gMySpawn.ownT < 3000) { --gMySpawn.own; gMySpawn.lastOwn = true; return; }   // the platform's own send
            if (same && SimSpawnOld()) return;
            memcpy(gMySpawn.rec, d, l); gMySpawn.len = (int)l;    // a spawn of the game: watched until the host sends it back
            gMySpawn.t = now; gMySpawn.resends = 0; gMySpawn.own = 0; gMySpawn.open = true;
        } else if (same && gMySpawn.open) {
            gMySpawn.open = false;
            if (gMySpawn.resends) Log("session: the host sent this player's spawn back after %d more", gMySpawn.resends);
        }
        return;
    }
    if (out || d[0] != 0x00 || l < 13 || slot == self || slot < 0 || slot >= 32) return;
    SlotAlive& a = gSlotAlive[slot];                      // a position of another player seen dead
    if (!a.body) return;
    float p[3]; for (int k = 0; k < 3; ++k) memcpy(&p[k], d + 1 + 4 * k, 4);
    const float dx = p[0] - a.bodyPos[0], dz = p[2] - a.bodyPos[2];
    if (!(dx * dx + dz * dz > kBodyFar * kBodyFar)) { a.movedSince = 0; return; }
    if (!a.movedSince) a.movedSince = now;              // SpawnFixups respawns him after 700 ms of it
    a.lastFar = now;
    memcpy(a.pos, p, sizeof p);
    a.away = sqrtf(dx * dx + dz * dz);
}
// ---- the upstream bandwidth (docs/GAME_FIXES.md, the host's relay). The network settings' UPSTREAM
// BANDWIDTH (US, EU and KR ntmenu2) offers 96, 128, 192, 256 and 512 kbps OR GREATER (US 0x377840: a
// choice's kbps, 0x377800: a kbps' choice), 256 by default (0x34C298); a game's start applies it
// (0x172A28 -> 0x16EBA0: the bits a second of all the sessions, network base 0x23BF68 + 0x1C, at most
// 512000; the scheduler 0x16D420 spends it). A host sends each joiner one datagram of every player's
// records every 6 VBlanks: with seven joiners they outgrew 256000, the host sent every 8 VBlanks and of
// the records that came twice meanwhile only the later went on. The PC adds 1024, 2048 and 4096 kbps,
// 4096 by default (variant 6: the first-time default, a saved 256, OR GREATER on the last choice), and
// takes up to 4096000. The Japanese programs have no such setting (their library keeps its 512000): a PC
// gives them 4096000. MGO1_UPSTREAM=0: the game's own (tests)
const uint16_t kUpstreamKbps[] = {96, 128, 192, 256, 512, 1024, 2048, 4096};
const uint32_t kUpstreamChoices = sizeof kUpstreamKbps / sizeof kUpstreamKbps[0];
#ifdef MGO1_PLAYER_BUILD
bool UpstreamOn() { return true; }
#else
bool UpstreamOn() { static const bool off = DevEnv("MGO1_UPSTREAM") && !strcmp(DevEnv("MGO1_UPSTREAM"), "0"); return !off; }
#endif
uint32_t UpstreamKbps(uint32_t choice) { return kUpstreamKbps[std::min(choice, kUpstreamChoices - 1)]; }
uint32_t UpstreamChoice(uint32_t kbps) {
    uint32_t i = 0;
    while (i + 1 < kUpstreamChoices && kbps > kUpstreamKbps[i]) ++i;
    return i;
}
void UpstreamSet(Ctx& c) {                                // 0x16EBA0 (a0: the mode's 2 bits, a1: kbps)
    const uint32_t nb = V(0x23BF68);
    W32(nb + 0x58, (R32(nb + 0x58) & ~3u) | ((uint32_t)c.r[4] & 3));
    static const int32_t test = DevEnv("MGO1_UPSTREAM_KBPS") ? atoi(DevEnv("MGO1_UPSTREAM_KBPS")) : 0;   // tests: this value
    const int32_t kbps = test > 0 ? test : (int32_t)c.r[5];
    if (kbps <= 0) return;
    const int64_t bits = std::clamp<int64_t>((int64_t)kbps * 1000, 56000, (int64_t)kUpstreamKbps[kUpstreamChoices - 1] * 1000);
    W32(nb + 0x1c, (uint32_t)bits);
    static int32_t logged = -1;
    if (kbps != logged) { logged = kbps; Log("session: upstream bandwidth %d kbps: %lld bits a second for the sessions", kbps, (long long)bits); }
}
// the dedicated host and the PLAYABLE DOLLs (docs/DEDICATED_HOST.md, docs/PLAYABLE_DOLL.md): the upstream
// bandwidth at its maximum whatever their account's card holds (the card's value raised too, as its
// network settings would show it), checked and logged at the game's start (before: the setter's kbps)
void UpstreamBots(Ctx& c, bool before) {
    static const bool bot = DevEnv("MGO1_HOST_ONLY") || DevEnv("MGO1_HOST_SETTINGS") || DevEnv("MGO1_DOLL");
    if (!bot) return;
    static int32_t asked = 0;
    const int32_t top = kUpstreamKbps[kUpstreamChoices - 1];
    if (before) {
        asked = (int32_t)c.r[5];
        if (asked != top) { c.r[5] = (uint32_t)top; W16(V(0x28D312), (uint16_t)top); }
        return;
    }
    const uint32_t budget = R32(V(0x23BF68) + 0x1c);
    Log("network: upstream bandwidth %d kbps%s, the send budget %u bits a second (%s)", top,
        asked != top ? (asked == 256 ? " (256 on the card, the game's default)" : " (another value on the card)") : "",
        budget, budget == (uint32_t)top * 1000u ? "the maximum" : "NOT the maximum");
}
void UpstreamElapsed() {                                  // before the scheduler 0x16D420: its budget, bits a second x the ms
    const uint32_t nb = V(0x23BF68);                      // since its last pass (+0x20), is a 32-bit product: past 524 ms at
    if (nb && R32(nb + 0x20) > 400) W32(nb + 0x20, 400);  // 4096000 it went negative and nothing left until a quicker pass
}
void UpstreamMenu() {                                     // ntmenu2 loaded: the item's range 0..4 made 0..7
    if (!UpstreamOn()) return;
    const uint32_t screens = V(0x2DC5D8);                 // the settings screens (0x3760A0: 0x1C each; +0x8 their items' count,
    if (!screens) return;                                 // +0x18 the items, 0x18 each; an item's +0x14 its range: min, max,
    const uint32_t net = screens + 0x1c, items = R32(net + 0x18), n = R32(net + 8);   // text group, texts or 0 for "%d kbps")
    for (uint32_t i = 0; i < n && i < 16 && items >= 0x100000 && items < 0x2000000; ++i) {
        const uint32_t r = R32(items + 0x18 * i + 0x14);
        if (r < 0x100000 || r >= 0x2000000 || R16(r) != 0 || R16(r + 2) != 4 || R32(r + 8) != 0) continue;
        W16(r + 2, (uint16_t)(kUpstreamChoices - 1));
        if (Once((uint64_t)51 << 40)) Log("session: UPSTREAM BANDWIDTH offers %u choices, up to %u kbps", kUpstreamChoices, UpstreamKbps(kUpstreamChoices - 1));
        return;
    }
    if (Once((uint64_t)52 << 40)) Log("session: no UPSTREAM BANDWIDTH item in this program's network settings");
}
// ntmenu2 loaded: the create-game settings' MAX PLAYERS (its range at 0x2DCCB8: 2..8, numbers; the item {1,
// text 0x4F, help 0x6D, range} in the list at 0x2DCF08) up to 16 (docs/BIG_GAMES.md: above 8 a game only PCs play)
void MaxPlayersMenu() {
    // the item {1, text 0x4F, help 0x6D, its range} in the create-game settings' list (the US program's at
    // 0x2DCF68, its range at 0x2DCCB8; each version's page has them elsewhere): found in the page
    auto range = [](uint32_t r) { return r >= 0x100000 && r + 12 <= 0x02000000 && R16(r) == 2 && (R16(r + 2) == 8 || R16(r + 2) == 16) &&
                                         R32(r + 4) == 1 && R32(r + 8) == 0; };
    uint32_t r = V(0x2DCCB8);
    if (!range(r)) {
        r = 0;
        for (uint32_t a = PageLo(); a + 16 <= PageHi() && !r; a += 4)
            if (R32(a) == 1 && R32(a + 4) == 0x4f && R32(a + 8) == 0x6d && range(R32(a + 12))) r = R32(a + 12);
    }
    if (!r) {
        if (Once((uint64_t)55 << 40)) Log("session: no MAX PLAYERS item in this program's create-game settings");
        return;
    }
    if (R16(r + 2) != 8) return;
    W16(r + 2, 16);
    if (Once((uint64_t)56 << 40)) Log("session: MAX PLAYERS offers 2 to 16 (above 8: a game only the PC versions play)");
}
// ---- the lobby's lists of maps with the maps 13 and up (docs/EXTRA_MAPS.md). ntmenu2's [27b5ee] (US 0x3498F8)
// takes from the script the bases of its texts among the script's strings and the maps (-m: their number, 12, the
// first of their names, a mask a rule of the maps it offers: DM, TDM, RESCUE and CAPTURE the twelve, SNEAKING
// seven), kept by getters (the number 0x349AC0, the bases 0x349AA0, the masks 0x349AD0). The menus take a map's
// names from the strings (base + i its name, base + n + i its short one, base + 2n + i its line of help) and, in
// the North American, European and Korean programs, from tables of {key, name} too (five languages, n names then
// n short ones: 0x35A358, 0x3751FC, 0x37B828, 0x37A900). After the command: n with the extra maps, their bits in
// the masks of the rules they offer, the names' base at kNamesBase (strings served at 0x125408 below), the tables
// copied longer and the code's addresses of them moved to the copies.
const uint32_t kNamesBase = 0x7000;
uint32_t gNamesOrig = 0, gNamesCount = 0;              // the script's own first map name; the maps' number with the extra ones
std::map<int, std::array<uint32_t, 3>> gNameStrings;   // an extra map's names in PS2 memory: its name, short name, help line
std::map<uint32_t, uint32_t> gNameTables;              // a page's table of names (its address) -> its longer copy
uint32_t NameString(int id, int kind) {                // kind 0 the name, 1 the short one, 2 the help line
    auto it = gNameStrings.find(id);
    if (it == gNameStrings.end()) {
        const ExtraMap* m = ExtraMapById(id);
        std::string name = m ? m->name : "?", shortName = m ? m->shortName : "?", help = name;
        for (size_t k = 1; k < help.size(); ++k)       // the help line in the menus' mixed case ("Silo Entrance")
            if (isalpha((unsigned char)help[k]) && isalpha((unsigned char)help[k - 1])) help[k] = (char)tolower((unsigned char)help[k]);
        std::array<uint32_t, 3> a{};
        const std::string* t[3] = {&name, &shortName, &help};
        for (int k = 0; k < 3; ++k) {
            a[k] = Alloc((uint32_t)t[k]->size() + 1);
            if (a[k]) memcpy(gMem + a[k], t[k]->c_str(), t[k]->size() + 1);
        }
        it = gNameStrings.emplace(id, a).first;
    }
    return it->second[kind];
}
// the address a getter reads: its lui t7 and the next addiu / lw on t7
uint32_t GetterAddr(uint32_t fn) {
    if (fn < 0x100000 || fn + 0x20 >= 0x02000000) return 0;
    uint32_t hi = 0; bool have = false;
    for (uint32_t a = fn; a < fn + 0x20; a += 4) {
        const uint32_t w = R32(a), op = w >> 26;
        if (op == 15 && ((w >> 16) & 31) == 15) { hi = (w & 0xffff) << 16; have = true; continue; }
        if (have && (op == 9 || op == 0x23) && ((w >> 21) & 31) == 15) return hi + (uint32_t)(int32_t)(int16_t)(w & 0xffff);
    }
    return 0;
}
// a table of the twelve maps' names: 24 {key, name} (the names, then the short ones), then {0, 0}
bool NameTable(uint32_t t) {
    if (t < 0x100000 || t + 200 >= 0x02000000) return false;
    const uint32_t key = R32(t);
    if (!key || key >= 0x01000000 || R32(t + 192) || R32(t + 196)) return false;
    for (uint32_t k = 0; k < 24; ++k) {
        const uint32_t s = R32(t + 8 * k + 4);
        if (R32(t + 8 * k) != key || s < 0x100000 || s >= 0x02000000 || !isprint(R8(s))) return false;
    }
    return true;
}
// the registers an instruction writes (the ones the scan below follows)
int WrittenReg(uint32_t w) {
    const uint32_t op = w >> 26;
    if (op == 0) return (int)((w >> 11) & 31);
    if (op == 3) return 31;
    if ((op >= 8 && op <= 15) || (op >= 0x20 && op <= 0x27) || op == 0x37 || op == 0x1a || op == 0x1b) return (int)((w >> 16) & 31);
    return -1;
}
void LobbyNameTables(uint32_t count) {
    struct Ref { uint32_t lui, addiu, table; };
    std::vector<Ref> refs;
    std::set<uint32_t> luiOther;                         // luis whose value also goes elsewhere: left as they are
    for (uint32_t us : {0x35a358u, 0x3751fcu, 0x37b828u, 0x37a900u}) {
        const uint32_t fn = gUsProgram ? us : ps2::VerAddr(us);   // (the Japanese programs have none of them)
        if (!fn) continue;
        bool returned = false;
        for (uint32_t a = fn; a < fn + 0x1800; a += 4) {
            const uint32_t w = R32(a);
            if (w == 0x03e00008) returned = true;
            if (returned && a > fn && (w & 0xffff8000) == 0x27bd8000) break;   // the next function's prologue
            if ((w >> 26) != 15) continue;                // lui rt, hi: its uses until rt is written again
            const uint32_t rt = (w >> 16) & 31, hi = (w & 0xffff) << 16;
            bool table = false, other = false;
            std::vector<Ref> mine;
            for (uint32_t b = a + 4; b < a + 0x80; b += 4) {
                const uint32_t x = R32(b), xop = x >> 26, rs = (x >> 21) & 31;
                if (rs == rt && xop != 0 && xop != 15) {
                    const uint32_t tgt = hi + (uint32_t)(int32_t)(int16_t)(x & 0xffff);
                    if (xop == 9 && NameTable(tgt)) { mine.push_back({a, b, tgt}); table = true; }
                    else other = true;
                } else if (xop == 0 && (rs == rt || ((x >> 16) & 31) == rt)) other = true;
                if (WrittenReg(x) == (int)rt) break;
            }
            if (!table) continue;
            if (other) { luiOther.insert(a); continue; }
            refs.insert(refs.end(), mine.begin(), mine.end());
        }
    }
    if (refs.empty()) { if (Once((uint64_t)62 << 40)) Log("extra maps: no table of the maps' names in this program's lobby (its strings only)"); return; }
    // the copies, in one block (one upper half for all: a lui shared by two tables takes one value)
    std::set<uint32_t> tables;
    for (auto& r : refs) tables.insert(r.table);
    const uint32_t each = (2 * count + 1) * 8;
    for (uint32_t t : tables)
        if (!gNameTables.count(t)) {
            uint32_t n = Alloc(each, 16);
            if (!n) return;
            for (uint32_t k = 0; k < 12; ++k) { W32(n + 8 * k, R32(t + 8 * k)); W32(n + 8 * k + 4, R32(t + 8 * k + 4)); }
            for (uint32_t k = 0; k < 12; ++k) { W32(n + 8 * (count + k), R32(t + 8 * (12 + k))); W32(n + 8 * (count + k) + 4, R32(t + 8 * (12 + k) + 4)); }
            for (uint32_t j = 12; j < count; ++j) {
                W32(n + 8 * j, R32(t)); W32(n + 8 * j + 4, NameString((int)j + 1, 0));
                W32(n + 8 * (count + j), R32(t)); W32(n + 8 * (count + j) + 4, NameString((int)j + 1, 1));
            }
            W32(n + 8 * 2 * count, 0); W32(n + 8 * 2 * count + 4, 0);
            gNameTables[t] = n;
        }
    int moved = 0;
    for (auto& r : refs) {
        const uint32_t n = gNameTables[r.table];
        W16(r.lui, (n + 0x8000) >> 16);
        W16(r.addiu, n & 0xffff);
        ++moved;
    }
    if (Once((uint64_t)63 << 40)) Log("extra maps: %zu tables of the maps' names copied with the extra maps, %d places of the lobby's code moved to them%s",
                                      tables.size(), moved, luiOther.empty() ? "" : " (some left: their address serves other code too)");
}
void LobbyMapLists() {                                  // after [27b5ee] ran (ntmenu2's script starts)
    const uint32_t countAt = GetterAddr(V(0x349ac0)), basesAt = GetterAddr(V(0x349aa0)), masksAt = GetterAddr(V(0x349ad0));
    if (!countAt || !basesAt || !masksAt) { if (Once((uint64_t)64 << 40)) Log("extra maps: the lobby's list of maps not found: not offered"); return; }
    const uint32_t n = R32(countAt);
    if (n != 12) { if (Once((uint64_t)65 << 40)) Log("extra maps: the lobby offers %u maps (12 expected): left as it is", n); return; }
    uint32_t count = 12;
    for (auto& m : ExtraMaps()) if (m.ready && m.id <= 32) count = std::max(count, (uint32_t)m.id);
    if (count == 12) return;
    std::string offered;
    for (auto& m : ExtraMaps()) {
        if (!m.ready || m.id > 32) continue;
        for (int r : m.rules) if (r >= 0 && r < 5) W32(masksAt + 4 * r, R32(masksAt + 4 * r) | (1u << (m.id - 1)));
        offered += (offered.empty() ? "" : ", ") + m.name;
    }
    gNamesOrig = R32(basesAt + 7 * 4);
    W32(basesAt + 7 * 4, kNamesBase);
    W32(countAt, count);
    gNamesCount = count;
    LobbyNameTables(count);
    if (Once((uint64_t)66 << 40)) Log("extra maps: the lobby offers %u maps: %s after the twelve", count, offered.c_str());
}
bool OlderProgram() {                                     // the JP and JPS programs (the older network library)
    static const bool older = ps2::SelectedVersion() && (!strcmp(ps2::SelectedVersion(), "JP") || !strcmp(ps2::SelectedVersion(), "JPS"));
    return older;
}
void UpstreamOlder() {                                    // the Japanese programs: no setting, their 512000 raised
    if (!UpstreamOn() || !OlderProgram()) return;
    const uint32_t nb = V(0x23BF68);
    if (!nb || R32(nb + 0x1c) != 512000) return;
    W32(nb + 0x1c, (uint32_t)kUpstreamKbps[kUpstreamChoices - 1] * 1000);
    if (Once((uint64_t)48 << 40)) Log("session: upstream bandwidth %u kbps (this program has no such setting)", kUpstreamKbps[kUpstreamChoices - 1]);
}
// games of more than 8 players (tests: MGO1_MAX_PLAYERS=n, 2..16): the create-game settings' maximum of
// players (ntmenu2's work [0x284410] + 0x2A7: 8 by default, 0x35EE70 clamps a loaded one to 2..8) set to n
// at each pass of ntmenu2's actor lists; a game's creation
// (0x35F1A8) copies it into sync object 0's field 0x22, which caps the host's joins (0x172218: the players
// counted at 0x28B560 + 0x90 against it; the player table and the sessions have 16 entries)
bool gNtLoaded = false;                                   // ntmenu2's page loaded once (its work set up)
// MGO1_ROUND_MINUTES=m (tests): the rules' time limits (+0x2C8, +0x2D0, +0x2D8, +0x2E0, +0x2EC: minutes, 30 for a
// deathmatch by default; 0x35F1A8 makes them seconds in sync object 0's field 2) set to m, for short rounds.
// MGO1_ROUNDS=r (tests): the team rules' rounds (+0x2CC, +0x2D4, +0x2DC, +0x2E4: 7, 5, 7, 5 by default, 0x35EDA0;
// config keys 0x4C..0x4F at the commit) set to r, for short stages
void MaxPlayers() {
    static const int n = getenv("MGO1_MAX_PLAYERS") ? atoi(getenv("MGO1_MAX_PLAYERS")) : 0;
    static const int minutes = getenv("MGO1_ROUND_MINUTES") ? atoi(getenv("MGO1_ROUND_MINUTES")) : 0;
    static const int rounds = getenv("MGO1_ROUNDS") ? atoi(getenv("MGO1_ROUNDS")) : 0;
    if (!gPageNt || !gNtLoaded || ((n < 2 || n > 16) && minutes <= 0 && rounds <= 0)) return;
    // the work once ntmenu2 runs (its page loaded): a maximum of 2..17 there and a deathmatch's time limit
    // of 1..99 minutes, else it is not the create-game settings yet (before, the pointer holds anything)
    const uint32_t w = R32(V(0x284410));
    if (w < 0x100000 || w + 0x300 >= 0x02000000) return;
    if (R8(w + 0x2a7) < 2 || R8(w + 0x2a7) > 17 || R32(w + 0x2e0) < 1 || R32(w + 0x2e0) > 99) return;
    if (minutes > 0 && minutes < 100)
        for (uint32_t off : {0x2c8u, 0x2d0u, 0x2d8u, 0x2e0u, 0x2ecu})
            if (R32(w + off) != (uint32_t)minutes) {
                W32(w + off, (uint32_t)minutes);
                if (Once((uint64_t)59 << 40)) Log("session: the rules' time limits set to %d minutes (MGO1_ROUND_MINUTES)", minutes);
            }
    if (rounds > 0 && rounds < 10)
        for (uint32_t off : {0x2ccu, 0x2d4u, 0x2dcu, 0x2e4u})
            if (R32(w + off) != (uint32_t)rounds) {
                W32(w + off, (uint32_t)rounds);
                if (Once((uint64_t)61 << 40)) Log("session: the team rules' rounds set to %d (MGO1_ROUNDS)", rounds);
            }
    if (n < 2 || n > 16 || R8(w + 0x2a7) == (uint32_t)n) return;
    W8(w + 0x2a7, (uint32_t)n);
    if (Once((uint64_t)58 << 40)) Log("session: the create-game settings' maximum of players set to %d (MGO1_MAX_PLAYERS)", n);
}
// SIM_CHANNELS=1 (tests): the ids of the session channels in use (the list at 0x28BCCC, + 4 the next, + 0x12 the
// id; 0x174790 opens one, 0x174530 gives a free id from 0x400 to 0x7FF), logged as new ones appear, with the page
void ChannelsWatch() {
    static const bool on = getenv("SIM_CHANNELS") != nullptr;
    if (!on) return;
    static std::set<std::pair<std::string, uint32_t>> seen;
    std::vector<uint32_t> fresh;
    uint32_t c = R32(V(0x28BCCC));
    for (int k = 0; c >= 0x100000 && c < 0x2000000 && k < 4096; ++k, c = R32(c + 4))
        if (seen.insert({gOverlayName, R16(c + 0x12)}).second) fresh.push_back(R16(c + 0x12));
    if (fresh.empty()) return;
    std::sort(fresh.begin(), fresh.end());
    std::string s;
    for (uint32_t id : fresh) { char b[12]; snprintf(b, sizeof b, " %x", id); s += b; }
    Log("channels: %s opens%s", gOverlayName.c_str(), s.c_str());
}
// the match's pause menu (START: "ONLINE PAUSE", the actor NewHudPause; its work: + 0x40 its screen, + 0x54 the
// players' records, 0x2C each, 16 at most (+ 0 the name, + 0x10 the id, + 0x12 bits: 2 muted, 8 a friend, 0x10
// blacklisted; + 0x1C the score, + 0x14 the kills, + 0x18 the deaths), + 0x314 their order (by score), + 0x324
// how many, + 0x328 the menu's state, + 0x32E the first of the 8 rows shown, + 0x330 the row chosen
uint32_t PauseMenuWork() {
    for (const auto& a : gNativeActors) if (a.second == "NewHudPause") return a.first;
    return 0;
}
// the table's functions in the map page loaded (the same code in the 12 maps of the five programs, found by
// it): its rows (a0 the work: the first function holding `lhu t6, 0x32e(s1)` then `lhu t7, 0x324(s1)`,
// n033a 0x3C12D0, the pause menu's and a round's results'; the second, 0x3C1708, the game's results') and the
// row chosen's mark (`lhu t4, 0x32e(a0)` then `lhu t6, 0x330(a0)`, 0x3C53D8; a1 1 with its sound). The game's
// results show a rank, the row's number (sprintf "%d" of the row + 1, its delay slot `move a2, s4` or `s5`):
// the text function called next (`move a3, zero` in its delay slot, n033a 0x3BE778: a0 the screen's
// elements, a1 an element, a2 the text) and the rows' elements (9 words a row, the rank the second: the
// table the function's first lui and addiu build, n033a 0x54F368)
struct TableFns { uint32_t rows = 0, rows9 = 0, mark = 0, text = 0, elems = 0; };
TableFns TableFunctions() {
    static uint32_t gen = 0xffffffff, base = 0; static TableFns t;
    if (gen == gPageGen && base == PageLo()) return t;
    gen = gPageGen; base = PageLo(); t = TableFns();
    auto start = [](uint32_t a) { for (uint32_t s = a; s > PageLo() && s + 0x4000 > a; s -= 4) if ((R32(s) & 0xffff8000) == 0x27bd8000) return s; return 0u; };
    for (uint32_t a = PageLo(); a + 8 <= PageHi(); a += 4) {
        const uint32_t w = R32(a);
        if (w == 0x962e032e && R32(a + 4) == 0x962f0324) { const uint32_t f = start(a); if (!t.rows) t.rows = f; else if (!t.rows9 && f != t.rows) t.rows9 = f; }
        else if (w == 0x948c032e && R32(a + 4) == 0x948e0330 && !t.mark) t.mark = start(a);
    }
    if (t.rows9) {
        int reg = -1; uint32_t hi = 0;
        for (uint32_t a = t.rows9; a < t.rows9 + 0x60 && !t.elems; a += 4) {
            const uint32_t w = R32(a);
            if (reg < 0 && (w >> 26) == 0xf && ((w >> 21) & 31) == 0) { reg = (int)((w >> 16) & 31); hi = w & 0xffff; }
            else if (reg >= 0 && (w >> 26) == 9 && (int)((w >> 21) & 31) == reg && (int)((w >> 16) & 31) == reg)
                t.elems = (hi << 16) + (uint32_t)(int32_t)(int16_t)(w & 0xffff);
        }
        for (uint32_t a = t.rows9; a < t.rows9 + 0x800 && a + 0x30 <= PageHi(); a += 4) {
            const uint32_t w = R32(a), ds = R32(a + 4);
            if ((w >> 26) != 3 || (ds & ~(31u << 21)) != 0x0000302D || (ds >> 21) < 16 || (ds >> 21) > 23) continue;
            for (uint32_t b = a + 8; b < a + 0x28; b += 4)
                if ((R32(b) >> 26) == 3 && R32(b + 4) == 0x0000382D) { t.text = (R32(b) & 0x3ffffff) << 2; break; }
            break;
        }
        if (t.elems < 0x100000 || t.elems + 0x120 > 0x02000000) t.elems = 0;
    }
    return t;
}
// the table's first row set to `first` and drawn again (the row chosen's mark with it)
void TableShow(Ctx& c, uint32_t w, uint32_t first, bool mark) {
    W16(w + 0x32e, first);
    const TableFns f = TableFunctions();
    Ctx x = c;
    x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
    x.r[31] = V(0x1005f0);
    if (f.rows) { x.r[4] = w; Call(x, f.rows); }
    if (mark && f.mark) { x = c; x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu; x.r[31] = V(0x1005f0); x.r[4] = w; x.r[5] = 0; Call(x, f.mark); }
}
// the game's results (screen 9: its rows drawn once): the first row set to `first`, the rows drawn again, their
// ranks written (the game writes the row's number: 1 to 8 on every page)
void TableShow9(Ctx& c, uint32_t w, uint32_t first, uint32_t n) {
    const TableFns f = TableFunctions();
    if (!f.rows9) return;
    W16(w + 0x32e, first);
    const uint32_t sp = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
    Ctx x = c;
    x.r[29] = sp; x.r[31] = V(0x1005f0); x.r[4] = w;
    Call(x, f.rows9);
    if (!f.text || !f.elems || !first) return;
    const uint32_t buf = sp + 0x200;                   // above the called functions' frames
    for (uint32_t k = 0; k < 8 && first + k < n; ++k) {
        char s[8]; const int len = snprintf(s, sizeof s, "%u", first + k + 1);
        for (int i = 0; i <= len; ++i) W8(buf + i, (uint8_t)s[i]);
        x = c; x.r[29] = sp; x.r[31] = V(0x1005f0);
        x.r[4] = R32(w + 0x40); x.r[5] = R32(f.elems + 0x24 * k + 4); x.r[6] = buf; x.r[7] = 0;
        Call(x, f.text);
    }
}
// the table's pages: more than 8 players, its 8 rows show the next 8 every 4 s (the first row at + 0x32E: 0, 8)
// on the team and skin select screens (the actor's screens 2 and 3, 0x3C2920 and 0x3C2B70) and a round's
// results (screens 7 and 8), which draw the rows at each pass, on the game's results (screen 9, while it is
// shown: its state 4) and in the pause menu while no row is being chosen (its state 1, the actions). Only the
// pause menu writes + 0x32E itself. Choosing a player (states 2 to 4, 7 to 11), the game's own scroll follows
// the row chosen, except when it goes round from the last row to the first or back (its window stays: the row
// chosen and its mark out of the 8, 0x3C6E28): then the rows that hold it
void TablePages(Ctx& c) {
    const uint32_t w = PauseMenuWork();
    static ULONGLONG since = 0; static uint32_t lastMode = 0xffff;
    if (!w || w + 0x340 > 0x02000000) { since = 0; return; }
    const uint32_t mode = R16(w + 0x326), n = R16(w + 0x324), state = R16(w + 0x328), first = R16(w + 0x32e);
    const bool eachPass = mode == 2 || mode == 3 || mode == 7 || mode == 8;   // the rows drawn at each pass
    const bool table = eachPass || mode == 6 || mode == 9;
    if (table && n <= 8 && first) {                    // 8 players or fewer again: the first row back
        if (mode == 6) TableShow(c, w, 0, state != 1);
        else if (mode == 9) { if (state == 4) TableShow9(c, w, 0, n); }
        else W16(w + 0x32e, 0);
    }
    if (n <= 8 || n > 16 || !table || (mode == 9 && state != 4)) { since = 0; lastMode = mode; return; }
    if (mode == 6 && state != 1) {
        since = 0; lastMode = mode;
        const uint32_t chosen = R16(w + 0x330);
        if (chosen >= n || (chosen >= first && chosen < first + 8)) return;
        TableShow(c, w, chosen < first ? chosen : chosen - 7, true);
        if (Once((uint64_t)60 << 40)) Log("session: the row chosen went round the table of %u players: the rows that hold it shown", n);
        return;
    }
    const ULONGLONG now = PsMs();
    if (!since || mode != lastMode) { since = now; lastMode = mode; }
    const uint32_t page = (uint32_t)((now - since) / 4000 % ((n + 7) / 8)) * 8;
    if (first == page) return;
    if (mode == 6) TableShow(c, w, page, false);
    else if (mode == 9) TableShow9(c, w, page, n);
    else W16(w + 0x32e, page);                        // (drawn at the screen's next pass)
    if (Once((uint64_t)57 << 40)) Log("session: the table of %u players shows them 8 at a time (a page every 4 s)", n);
}
// SIM_PAUSE_WATCH=1 (tests): the pause menu's players, rows shown and row chosen, logged as they change
void PauseWatch(Ctx& c) {
    TablePages(c);
    {                                                    // a page of NewHudPause shown (+0x326 its screen: 3 team select, 4 skin
        const uint32_t w = PauseMenuWork();              // select, 6 the START menu, 2, 7, 8, 9 the results): the GS draws it as a
        const bool page = w && w + 0x340 <= 0x02000000 && R16(w + 0x326) != 0;   // lobby page in a wide picture
        static bool was = false;
        ps2::GsSetMenuPage(page);
        if (page != was && Once((uint64_t)70 << 40)) Log("widescreen: a match's page drawn at its 4:3 width about the centre (screen %u)", w ? R16(w + 0x326) : 0);
        was = page;
    }
    static const bool on = getenv("SIM_PAUSE_WATCH") != nullptr;
    if (!on) return;
    const uint32_t w = PauseMenuWork();
    static std::string last;
    if (!w || w + 0x340 > 0x02000000) { last.clear(); return; }
    const uint32_t n = R16(w + 0x324);
    if (!n || n > 16) return;
    char b[160];
    snprintf(b, sizeof b, "pause menu: screen %u state %u, %u players, rows %u..%u shown, row %u chosen:", R16(w + 0x326), R16(w + 0x328), n, R16(w + 0x32e) + 1,
             std::min(n, (uint32_t)R16(w + 0x32e) + 8), R16(w + 0x330) + 1);
    std::string t = b;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t r = w + 0x54 + 0x2c * R8(w + 0x314 + k);
        char nm[16] = {}; for (int i = 0; i < 15; ++i) { const uint32_t ch = R8(r + i); if (!ch) break; nm[i] = ch >= 0x20 && ch < 0x7f ? (char)ch : '?'; }
        snprintf(b, sizeof b, " %s(%u)%s%s%s %d/%d/%d", nm, R8(r + 0x10), R8(r + 0x12) & 2 ? "M" : "", R8(r + 0x12) & 8 ? "F" : "",
                 R8(r + 0x12) & 0x10 ? "B" : "", (int)R32(r + 0x1c), (int)R32(r + 0x14), (int)R32(r + 0x18));
        t += b;
    }
    if (t != last) { Log("%s", t.c_str()); last = t; }
}
// at the start of a pass of the actor lists (0x122768): this player's spawn sent again, the host's spawns sent
void SpawnFixups(Ctx& c) {
    if (!SpawnFixOn()) return;
    // a record on the player's first channel (+0x934: 0x174BA8 writes, 0x174C60 sends); `skip`: a player id
    // it does not go to (the channel's flag 4 and its peer +0x54, as a relay leaves out its source)
    auto send = [&](uint32_t obj, const uint8_t* rec, int len, int skip) {
        const uint32_t chan = obj + 0x934;
        int peer = -1;
        if (skip >= 0) {                                  // that player's session: its index among the channel's peers
            const uint32_t table = V(0x28B5F4), sessions = R32(V(0x23BFA8));
            for (int k = 0; k < 16; ++k) {
                const uint32_t e = table + 0x5C * k;
                if (!R16(e + 2) || R8(e) != (uint32_t)skip || !R32(e + 4) || R32(e + 4) < sessions) continue;
                const uint32_t sid = (R32(e + 4) - sessions) / 0xa0;
                for (int i = 0; i < 16; ++i) if (((R32(chan + 0x14 + 4 * i) >> 24) & 0x7f) == sid && (int32_t)R32(chan + 0x14 + 4 * i) >= 0) peer = i;
            }
        }
        const uint8_t flags = (uint8_t)R8(chan + 0x11), from = (uint8_t)R8(chan + 0x54);
        Ctx x = c;
        x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
        x.r[31] = V(0x1005f0);
        const uint32_t buf = (uint32_t)x.r[29] + 0x200;              // above the callee's frame, below the caller's
        for (int k = 0; k < len; ++k) W8(buf + k, rec[k]);
        x.r[4] = chan; x.r[5] = buf; x.r[6] = (uint32_t)len;
        Call(x, V(0x174ba8));
        if (peer >= 0) { W8(chan + 0x11, flags | 4); W8(chan + 0x54, (uint32_t)peer); }
        x.r[4] = chan;
        Call(x, V(0x174c60));
        if (peer >= 0) { W8(chan + 0x11, flags); W8(chan + 0x54, from); }
    };
    const ULONGLONG now = PsMs();
    if (gMySpawn.open && now - gMySpawn.t > 500) {
        const uint32_t me = PlayerOfSlot(OwnSlot());
        if (!me || gMySpawn.resends >= 4) {
            if (me) Log("session: the host has not sent this player's spawn back after %d more: left", gMySpawn.resends);
            gMySpawn.open = false;
        } else {
            ++gMySpawn.own; gMySpawn.ownT = now;              // told from the game's own when it goes out
            send(me, gMySpawn.rec, gMySpawn.len, -1);
            ++gMySpawn.resends; gMySpawn.t = now;
            Log("session: the host has not sent this player's spawn back: sent again (%d)", gMySpawn.resends);
        }
    }
    const int self = OwnSlot();
    if (const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0) {   // at rest away from his last position record: one more
        const bool alive = (int16_t)R16(me + 0xb64) > 0 && !(R32(me + 0xb48) & 0x1000000);   // (not in the weapon choice)
        float p[3]; for (int k = 0; k < 3; ++k) p[k] = F32(me + 0x110 + 4 * k);
        const float dx = p[0] - gMyPos.p[0], dy = p[1] - gMyPos.p[1], dz = p[2] - gMyPos.p[2];
        if (alive && now - gMyPos.t >= 500 && (!gMyPos.known || !gMyPos.rest || dx * dx + dy * dy + dz * dz > 3.f * 3.f)) {
            uint8_t rec[13] = {0x00};
            memcpy(rec + 1, p, 12);
            send(me, rec, 13, -1);
            if (Once((uint64_t)49 << 40)) Log("session: this player at rest %.0f units from his last position record: his position sent", gMyPos.known ? sqrtf(dx * dx + dy * dy + dz * dz) : 0.f);
            memcpy(gMyPos.p, p, sizeof p); gMyPos.known = true; gMyPos.rest = true; gMyPos.t = now;
        }
        if (alive && gMyInput.moving && now - gMyInput.t >= 500 && now - gMyInput.ladder > 1000) {   // none for half a second after one
            const uint8_t rec[7] = {0x01};                            // that moved him: his copies told he stands
            send(me, rec, 7, -1);
            if (Once((uint64_t)50 << 40)) Log("session: this player's input records stopped while his last one moved him: an empty one sent");
            memcpy(gMyInput.rec, rec, 7); gMyInput.moving = false; gMyInput.t = now;
        }
    }
    for (int slot = 0; slot < 32 && CopyRestOn(); ++slot) {   // a copy at rest or walking while aiming: to his position record
        SlotNet& n = gSlotNet[slot];
        if (slot == self || !n.pos) continue;
        const uint32_t o = PlayerOfSlot(slot);
        if (!o || o != n.obj || (int16_t)R16(o + 0xb64) <= 0 || (R32(o + 0xb48) & 0x1000000)) {   // dead, (re)spawning, another
            n.pos = 0; n.obj = o; continue;                   // copy: a position of his after it only
        }
        if (now - n.ladder < 1000) continue;
        const uint32_t w0 = R32(o + 0xb2c), w1 = R32(o + 0xb30);
        const float px = F32(o + 0x110), pz = F32(o + 0x118), dx = F32(o + 0xe00) - px, dz = F32(o + 0xe08) - pz;
        const float d = sqrtf(dx * dx + dz * dz);
        if (!(d > 4.f && d <= 3000.f)) continue;             // (beyond 3000 the game puts it there)
        auto put = [&](uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); W32(a, u); };
        const bool still = !(w1 & 0x7ffffff) && !R32(o + 0xe10) && !R16(o + 0xe14);
        if (n.pos > n.moving && w0 < 0x10000 && ((w0 & 0xfff) == 2 || (w0 & 0xfff) == 3) && still) {   // standing or aiming,
            const float k = std::min(d, std::max(20.f, 0.25f * d)) / d;   // his input still: a quarter a pass, 20 units at least
            put(o + 0x110, px + dx * k); put(o + 0x118, pz + dz * k);
            if (Once((uint64_t)53 << 40)) Log("session: player %d's copy at rest %.0f units from his position: moved there", slot + 1, d);
        } else if (WalkAimOn() && (R8(o + 0xde0) & 0x70) && now - n.pos < 300 && w0 < 0x10000 &&
                   ((w0 & ~0x6000u) == 2 || (w0 & ~0x6000u) == 3)) {   // walking while aiming, his positions coming: an
            put(o + 0x110, px + dx * 0.125f); put(o + 0x118, pz + dz * 0.125f);   // eighth a pass (the game's own for a copy
            if (Once((uint64_t)54 << 40)) Log("session: player %d's copy walking while aiming %.0f units from his position: brought nearer", slot + 1, d);
        }                                                     // walking otherwise, twice a tick)
    }
    for (int slot = 0; slot < 32; ++slot) {             // a player seen dead whose positions came from elsewhere for 700 ms
        SlotAlive& a = gSlotAlive[slot];
        const uint32_t o = slot != self ? PlayerOfSlot(slot) : 0;
        if (o != a.obj) { a = SlotAlive(); a.obj = o; }   // another player (or none): nothing known of him yet
        if (!o || (int16_t)R16(o + 0xb64) > 0 || R8(o + 0xee8)) { a.body = false; a.movedSince = 0; if (o && (int16_t)R16(o + 0xb64) > 0) a.wasAlive = true; continue; }
        if (!a.wasAlive) continue;                        // not spawned yet (a player's copy starts at life 0)
        if (!a.body) {                                    // seen dead now: where his body lies
            a.body = true; a.movedSince = 0;
            for (int k = 0; k < 3; ++k) a.bodyPos[k] = F32(o + 0x110 + 4 * k);
            continue;
        }
        if (!a.movedSince || now - a.movedSince < 700 || now - a.lastFar > 1000) continue;
        a.movedSince = 0; a.body = false;
        W8(o + 0xee8, 1);                                 // the spawn command the 02 handler stores, where he is
        for (int k = 0; k < 3; ++k) { uint32_t u; memcpy(&u, &a.pos[k], 4); W32(o + 0xed0 + 4 * k, u); }
        Log("session: player %d is seen dead while his positions come from %.0f units away from his body: respawned there%s", slot + 1,
            a.away, IsSessionHost() ? ", his spawn sent to the others" : "");
        if (IsSessionHost()) gHostSpawns.push_back({slot, {a.pos[0], a.pos[1], a.pos[2]}});
    }
    for (const auto& h : gHostSpawns) {
        const uint32_t o = PlayerOfSlot(h.first);
        if (!o) continue;
        uint8_t rec[12] = {0x02, 1, (uint8_t)R8(o + 0xee9), (uint8_t)R8(o + 0xeea)};
        for (int k = 0; k < 3; ++k) { const int16_t v = (int16_t)lrintf(h.second[k] / 10.f); memcpy(rec + 4 + 2 * k, &v, 2); }
        const uint16_t head = (uint16_t)R16(o + 0xee2); memcpy(rec + 10, &head, 2);
        send(o, rec, 12, h.first);                         // to the others: he plays on where he is
    }
    gHostSpawns.clear();
}
// ---- (v150-host beta 16) The character a spawn asks for (docs/VIRTUAL_DOLL.md 13, docs/DEDICATED_HOST.md 19). A spawn
// record names its player's character (his class, +0xB62: KGB, GRU, the Ocelot Unit, the special characters, Snake). The
// 02 handler stores it (+0xEEA) and, on a host, writes the record again for the others from what it stored; a spawn back
// in play (US n003a 0x40D1A0) then makes the copy's body again when the stored character is not the copy's (the compare
// at 0x40D2F8; the body's model from the page's table 0x2F1310, 0x4096E0). The game takes whatever comes: a character
// whose body the round's rules page does not hold, or one past the table's nine, leaves the copy with no body, and the
// game reads its bones through nothing (a bot's game, 8 to 10 November; every game of the room the same, by the
// host's relay). No player's game sends such a spawn: it names its own character.
// The platform, in the US program (its 12 map pages hold this code and table, each at its own addresses, paired by
// gen/g_page_map.cpp and checked page by page). The US program is the only one a host runs: host/build_host.sh makes
// the host and the dolls from the North American images. The other discs' programs are left as they are (not checked
// there: a player's game with another disc; a host started with another disc's program would have the virtual dolls'
// own telling of their class as its only defence, and says so once, below):
// - on receipt, before the game reads the record (SpawnCharacterOnReceipt): a character the page cannot make gives way
//   to the copy's present one in the record itself, so what the host stores and what it relays are both right. Every
//   kind of spawn: a ghost's and a spectator's are stored too, and the platform's own respawn of a player seen dead
//   (SpawnFixups) tells the stored one. A record with no character in it, or for a player with no copy here and a
//   character the page cannot make, is dropped;
// - at the spawn itself (SpawnCharacterAtSpawn, the function's entry, before its compare): the same question on what
//   the copy holds, whatever stored it.
// "Can the page make it" is asked as the game asks for a body: its resource look-up (0x122F18) of the model's code
// (| 0x0D000000). A character the page holds goes through as before: a player's own change of character. A page whose
// table is not found (none known) keeps every copy's present character. One line a player and a round.
// MGO1_SPAWN_CHARACTER_GUARD=0 (tests, the bot's builds): the game's own behaviour.
bool SpawnCharacterGuardOn() {
    static const bool off = DevEnv("MGO1_SPAWN_CHARACTER_GUARD") && !strcmp(DevEnv("MGO1_SPAWN_CHARACTER_GUARD"), "0");
    return !off && gNative && gUsProgram && (gPageN3 || gPageMap);
}
// 1: the loaded pages can make that character's body, 0: they cannot, -1: not known (no table, no look-up)
int CharacterHeld(const Ctx& c, int character) {
    if (character < 0 || character > 8) return 0;
    const uint32_t table = PageData(0x4096E0, 0, 0x2F1310);
    const ps2::Fn look = ps2::FindFn(V(0x122F18));
    if (!table || !InPageArea(table) || !look) return -1;
    Ctx x = c;
    x.r[4] = R32(table + 4 * (uint32_t)character) | 0x0D000000u;
    look(x);
    return (uint32_t)x.r[2] ? 1 : 0;
}
// a spawn asking `asked` for a copy that is `present`: nullptr when it goes through, else why it does not
const char* SpawnCharacterRefused(const Ctx& c, int asked, int present) {
    if (asked == present) return nullptr;
    if (asked > 8) return "a character the game does not have";
    const int held = CharacterHeld(c, asked);
    return held > 0 ? nullptr : held == 0 ? "a character this round does not hold" : "a character that cannot be checked on this page";
}
void SpawnCharacterSaid(int slot, const char* what, const char* done) {
    static int said[32];                                 // the round (its byte + 1) each player's line was written in
    const int round = (int)R8(V(0x28BBCB)) + 1;
    if (slot < 0 || slot >= 32 || said[slot] == round) return;
    said[slot] = round;
    Log("session: player %d's spawn asks for %s: %s", slot + 1, what, done);
}
void SpawnCharacterOnReceipt(const Ctx& c, uint32_t buf, uint32_t len) {
    if (gNative && (gPageN3 || gPageMap)) {               // said once, at the first datagram of a map: checked or not
        static bool said = false;
        if (!said) {
            said = true;
            if (SpawnCharacterGuardOn()) Log("session: the character a spawn asks for is checked before the game takes it (the US program)");
            else if (gUsProgram) Log("session: the character a spawn asks for is NOT checked: turned off (MGO1_SPAWN_CHARACTER_GUARD=0, tests)");
            else Log("session: the character a spawn asks for is NOT checked: the %s program runs (the check is made for the US program, the one a host is built for)", ps2::SelectedVersion());
        }
    }
    if (!SpawnCharacterGuardOn()) return;
    buf &= 0x01ffffff;
    if (len <= 2 || buf + len > 0x02000000) return;
    ForPlayerRecords(gMem + buf + 2, len - 2, [&](uint8_t* r, uint8_t* d, size_t l, int slot, int kind) {
        if (kind != 0 || d[0] != 0x02) return;
        auto drop = [&](const char* what, const char* done) { r[0] = 0x3f; r[1] &= (uint8_t)~7; SpawnCharacterSaid(slot, what, done); };
        if (l < 4) { drop("no character (a record cut short)", "dropped"); return; }
        const uint32_t o = PlayerOfSlot(slot);
        if (!o) {                                         // no copy of him here: nothing to keep
            if (d[3] > 8 || CharacterHeld(c, d[3]) <= 0) drop("a character this round does not hold", "dropped (no copy of him here)");
            return;
        }
        const int present = (int)R8(o + 0xB62);
        if (const char* why = SpawnCharacterRefused(c, d[3], present)) {
            d[3] = (uint8_t)present;
            SpawnCharacterSaid(slot, why, "his present one kept");
        }
    });
}
void SpawnCharacterAtSpawn(const Ctx& c) {                // a0: the player whose spawn back in play begins
    const uint32_t o = (uint32_t)c.r[4];
    if (o < 0x100000 || o >= 0x01FFF000) return;
    const int present = (int)R8(o + 0xB62);
    if (const char* why = SpawnCharacterRefused(c, (int)R8(o + 0xEEA), present)) {
        W8(o + 0xEEA, (uint8_t)present);
        SpawnCharacterSaid((int)R8(o + 0xb60), why, "his present one kept (at his spawn)");
    }
}
#ifdef MGO1_PLAYER_BUILD
inline void HostStillWatch() {}
inline void VdollTick(Ctx&) {}
inline bool VdollTrapSensor(Ctx&, uint32_t) { return false; }   // (v150-host beta 21: the host's only)
inline void VdollClayAct(Ctx&) {}                       // ((v150-host beta 25, review25) beta 22's support: the host's only)
#else
void PlayerStillWatch();
inline void HostStillWatch() { PlayerStillWatch(); }
// ---- (virtual dolls) players the dedicated host runs itself, with no game of their own (vdoll.h, docs/VIRTUAL_DOLL.md):
// their datagrams given to the session engine's recvfrom (0x16E540), the host's to them read in the clear at the packet
// recorder and kept off the network (sendto) ----
void VdollLog(const char* fmt, ...) {
    char b[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    // (v150-host beta 21, the review) a line longer than Log's buffer (a body's status line reached 1067 characters) goes
    // on in a second line that says so, instead of its end being cut off
    const size_t len = strlen(b), kPart = 960;
    if (len <= kPart) { Log("%s", b); return; }
    for (size_t at = 0; at < len; at += kPart) Log(at ? "  (continued) %.*s" : "%.*s", (int)std::min(kPart, len - at), b + at);
}
int VdollGen() { return R8(V(0x28BBCB)); }               // the game network's round byte (0x173970)
int VdollSeenPlayers(vdoll::Seen* out, int max);          // (with the dedicated host's players' kinds, below)
int VdollSeats();
// the room's variables (set 0): +0 the phase, the rotation's {map, rule} from +2, +0x22 the players at most, +0x36 the entry played
int VdollRule() {
    uint8_t v[0x40];
    if (!RoomVariables(v, sizeof v)) return -1;
    const int e = v[0x36] < 16 ? v[0x36] : 0;
    return v[3 + 2 * e] <= 6 ? v[3 + 2 * e] : -1;
}
int VdollPhase() { uint8_t v[1]; return RoomVariables(v, 1) ? v[0] : -1; }
int VdollRoomChannel() { return 0x62 + 0x10 * OwnChanFamily(); }   // the game objects at 0x60 or 0x70 (BigGameChannel)
bool VdollPlayer(int id, int* team, int* life, float pos[3]) {
    int t = 0xFF;
    if (!PlayerInfo(id, &t, nullptr)) return false;
    *team = t; *life = 0;
    pos[0] = pos[1] = pos[2] = 0;
    if (const uint32_t o = PlayerOfSlot(id)) {                // its object in the host's game: its life and place
        *life = (int16_t)R16(o + 0xb64);
        for (int k = 0; k < 3; ++k) pos[k] = F32(o + 0x110 + 4 * k);
    }
    return true;
}
// the respawn points of the map loaded: its stage's .geom read on a thread of its own when its page comes (v150-host beta
// 14: read at the first respawn on the game's thread, it held the host's game some 70 ms once a map)
struct VdollSpawnsRead { std::mutex mu; std::string page, log; int extra = -1, gen = 0; bool ready = false; std::vector<doll::Spawn> list; };
VdollSpawnsRead gVdollSpawns;
void VdollSpawnsFor(const std::string& now, int ex) {   // (the game's thread; the reader's line logged here)
    int gen;
    {
        std::lock_guard<std::mutex> g(gVdollSpawns.mu);
        if (!gVdollSpawns.log.empty()) { Log("%s", gVdollSpawns.log.c_str()); gVdollSpawns.log.clear(); }
        if (now == gVdollSpawns.page && ex == gVdollSpawns.extra) return;
        gVdollSpawns.page = now; gVdollSpawns.extra = ex; gVdollSpawns.ready = false; gVdollSpawns.list.clear();
        gen = ++gVdollSpawns.gen;
    }
    std::thread([now, ex, gen] {
        std::vector<uint8_t> bytes; std::string err;
        doll::Geom g;
        std::vector<doll::Spawn> list;
        if (StageResource(now, 0x0C, bytes, err, ex) && doll::DecodeGeom(bytes.data(), bytes.size(), g, err)) list = g.spawns;
        std::lock_guard<std::mutex> lk(gVdollSpawns.mu);
        if (gen != gVdollSpawns.gen) return;               // another map came meanwhile
        char b[256];
        // ((v150-host beta 22, sync) an extra map's key, n003a_x13, as the mesh's line; a disc map's page alone, as before)
        snprintf(b, sizeof b, "vdoll: the respawn points of %s: %zu%s%s", stagekey::Key(now, ex).c_str(), list.size(), err.empty() ? "" : ": ", err.c_str());
        gVdollSpawns.list = std::move(list); gVdollSpawns.ready = true; gVdollSpawns.log = b;
    }).detach();
}
// a respawn point of the map loaded for a team, by the round's rule (the room's variables: +0 the phase, the rotation's
// {map, rule} from +2, the entry played +0x36): the deathmatch's own points, capture's (tcm), the team rules' (tdm); none
// while they are read (the doll's spawn as the host gave it). (v150-host beta 16) BASE CAPTURE as a player's game respawns
// its player there (basecap.h): by a base his team holds, the nearest to out (where he fell, given on the call), else a
// team deathmatch point clear of every base's zone
}  // (the two below are defined after this unnamed namespace, with basecap.h's tests' functions: declared outside it too)
bool BcVdollRespawn(int team, unsigned pick, const float fell[3], float out[3]);
bool BcVdollClear(const float p[3]);
namespace {
bool VdollSpawnPoint(int team, unsigned pick, float out[3]) {
    std::string now;
    int ex = 0;
    if (!StageNow(&now, &ex)) return false;                  // (v150-host beta 22, sync: one stage's page and extra map; none while one of another page loads)
    VdollSpawnsFor(now, ex);
    std::vector<doll::Spawn> spawns;
    {
        std::lock_guard<std::mutex> g(gVdollSpawns.mu);
        if (!gVdollSpawns.ready) return false;
        spawns = gVdollSpawns.list;
    }
    uint8_t v[0x40];
    int rule = 1;
    if (RoomVariables(v, sizeof v)) { const int e = v[0x36] < 16 ? v[0x36] : 0; rule = v[3 + 2 * e]; }
    const bool bc = rule == 6 && (team == 0 || team == 1);
    if (bc) { const float fell[3] = {out[0], out[1], out[2]}; if (BcVdollRespawn(team, pick, fell, out)) return true; }
    // (v150-host beta 19) the kinds a player of the team takes by the rule (doll_geom.cpp RespawnKinds), the first the map has
    doll::SpawnKind kinds[4];
    const int nk = doll::RespawnKinds(rule, team, kinds);
    std::vector<const doll::Spawn*> c;
    for (int k = 0; k < nk && c.empty(); ++k)
        for (const auto& s : spawns) if (s.kind == kinds[k]) c.push_back(&s);
    if (c.empty()) return false;
    if (bc) {                                                // (none clear: the asked ones kept, as the game does)
        std::vector<const doll::Spawn*> clear;
        for (const doll::Spawn* s : c) if (BcVdollClear(s->p)) clear.push_back(s);
        if (!clear.empty()) c.swap(clear);
    }
    const doll::Spawn* s = c[pick % c.size()];
    out[0] = s->p[0]; out[1] = s->p[1]; out[2] = s->p[2];
    return true;
}
// the virtual dolls' own processor (their tick, their reading of the host's datagrams): a line a minute
struct VdollCostRun { long long since = 0, ticks = 0, most = 0, passes = 0, mostAt = 0; };
VdollCostRun gVdollCost;
void VdollCostAdd(long long t, bool pass) {
    VdollCostRun& c = gVdollCost;
    c.ticks += t;
    if (t > c.most) { c.most = t; c.mostAt = gFrameNo; }
    if (pass) ++c.passes;
    const long long now = RealQpc(), f = QpcFreq();
    if (!c.since) { c = VdollCostRun(); c.since = now; return; }
    if (now - c.since < 60 * f) return;
    const double ms = 1000.0 / (double)f;
    Log("vdoll: %.0f s: the virtual dolls' own code %.3f ms a game pass (the longest %.2f ms, frame %lld), %.2f %% of a core",
        (double)(now - c.since) / (double)f, c.passes ? (double)c.ticks * ms / (double)c.passes : 0.0, (double)c.most * ms,
        c.mostAt, (double)c.ticks * 100.0 / (double)(now - c.since));
    c = VdollCostRun(); c.since = now;
}
// (the virtual dolls' bodies, vdoll_body.cpp) the map loaded, its collision, the players as the host's game holds them
// (+0x110 the hips, +0xB64 the life, the state words from +0xB2C: 10's 0x400000 a ghost, 7's 0x1000000 held at a spawn;
// +0xDE0 a remote player's style record), the round's rule
// (v150-host beta 22, sync) the page and the extra map of one stage (StageNow): none while a stage of another page loads (one of
// the same page: at its request); beta 21 paired the old page with the new stage's extra map for 20 to 30 frames (n033a_x13:
// Brown Town's mesh built for SILO ENTRANCE)
bool VdollMapNow(std::string* page, int* extra) { return StageNow(page, extra); }
bool VdollStageGeom(const std::string& page, int extra, std::vector<uint8_t>* out, std::string* err) { return StageResource(page, 0x0C, *out, *err, extra); }
bool VdollStealthed(uint32_t pl);                         // (v150-host beta 18, after tsne.h)
// (v150-host beta 21, cover) the session's own round trip to player id in ms (its entry's session +0x90, what the status
// file's "ping" tells); -1: none (the host's own player, no session yet)
int VdollRtt(int id) {
    const uint32_t table = V(0x28B5F4), countAt = V(0x28B5F0);
    if (!table || !countAt) return -1;
    const uint32_t count = R32(countAt);
    if (!count || count > 16) return -1;
    for (uint32_t k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * k;
        if (R8(e) != (uint32_t)id || !R16(e + 2)) continue;
        if (R32(e + 8) & 2) return -1;
        const uint32_t ss = R32(e + 4);
        if (!ss || ss >= 0x02000000) return -1;
        return (int16_t)R16(ss + 144);
    }
    return -1;
}
bool VdollSee(int id, vdoll::PlayerNow* s) {
    int t = 0xFF;
    if (!PlayerInfo(id, &t, nullptr)) return false;
    *s = vdoll::PlayerNow();
    s->team = t;
    const uint32_t o = id > 0 ? PlayerOfSlot(id) : 0;
    if (!o) return true;
    for (int k = 0; k < 3; ++k) s->pos[k] = F32(o + 0x110 + 4 * k);
    // (v150-host beta 16) a place that is no number, or 1 km away: an object being made or freed, not seen
    for (int k = 0; k < 3; ++k) if (!(std::fabs(s->pos[k]) < 1e6f)) { s->pos[0] = s->pos[1] = s->pos[2] = 0; return true; }
    s->object = true;
    s->life = (int16_t)R16(o + 0xb64);
    s->alive = s->life > 0 && !(R32(o + 0xb2c + 40) & 0x400000) && !(R32(o + 0xb48) & 0x1000000);
    s->style = R8(o + 0xde0);
    s->state = R32(o + 0xb2c);                                // (beta 21, geom: his stance, the first state word's bits 2 and 3)
    s->cls = R8(o + 0xb62);
    // (v150-host beta 18) knocked out (asleep or stunned): the state flag 0x99 (word 4 bit 25 of +0xB2C), on the host's
    // copies too (docs/TEAM_SNEAKING.md; tsne.h kTsneKnockedOut)
    s->asleep = s->life > 0 && ((R32(o + 0xb2c + 16) >> 25) & 1);
    s->camo = VdollStealthed(o);
    {                                                         // (v150-host beta 22, book) a state his book check refuses
        uint32_t w[11];
        for (int k = 0; k < 11; ++k) w[k] = R32(o + 0xb2c + 4 * (uint32_t)k);
        s->bookRefused = vbook::Refused(w);
    }
    s->reading = R8(o + 0xde9) != 0;                          // (v150-host beta 22, wake: his reading, his gauge, the ally wake's part)
    s->stunMax = (int16_t)R16(o + 0xb6a); s->wakePart = (int)R8(o + 0xb74); s->stamina = (int16_t)R16(o + 0xb68);
    s->yaw = (int)R16(o + 0x142); s->yawTo = (int)R16(o + 0x14a);   // ((v150-host beta 25, reactfix) his yaw, the one he turns to: a shooter's reaction)
    // (v150-host beta 21, cover) the position he really sent (his last position record, +0xE00: the game moves his copy at
    // +0x110 by his input records between them) and its age on the machine's clock; -1: none came from him
    for (int k = 0; k < 3; ++k) s->sent[k] = F32(o + 0xe00 + 4 * k);
    const ULONGLONG rec = id < 32 ? gSlotNet[id].real : 0;
    if (rec && std::fabs(s->sent[0]) < 1e6f && std::fabs(s->sent[1]) < 1e6f && std::fabs(s->sent[2]) < 1e6f)
        s->sentAge = (int)std::min<ULONGLONG>(600000, GetTickCount64() - rec);
    s->rtt = VdollRtt(id);
    return true;
}
unsigned long long VdollRealMs() { return GetTickCount64(); }   // (v150-host beta 21, cover: the machine's clock, not scaled)
// (v150-host beta 15) the mission objects (NewOnlineItemChara2: +0x410 the count, items 0x120 from +0xB0: +0x90 x y z, +0xD0
// the state, +0xD4 the holder's work, +0xF8 the team whose goal holds it; the goals' places +0x4C0 red, +0x700 blue) and
// BASE CAPTURE's bases, for the virtual dolls' bodies
uint32_t MissionObjects();
int VdollObjectives(vdoll::ObjNow* out, int max, float goals[2][3], bool set[2]) {
    const uint32_t man = MissionObjects();
    if (!man) return 0;
    int n = (int)R32(man + 0x410);
    if (n < 1 || n > 8) n = 1;
    n = std::min(n, max);
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const uint32_t it = man + 0xB0 + 0x120 * (uint32_t)i;
        vdoll::ObjNow o;
        for (int j = 0; j < 3; ++j) o.pos[j] = F32(it + 0x90 + 4 * (uint32_t)j);
        if (std::fabs(o.pos[0]) > 1e6f || std::fabs(o.pos[2]) > 1e6f) continue;
        o.state = (int)R32(it + 0xD0);
        const uint32_t h = R32(it + 0xD4);
        if (h) for (int id = 0; id < 16; ++id) if (PlayerOfSlot(id) == h) { o.holder = id; break; }
        const int g = R8(it + 0xF8);
        o.goalTeam = g == 0 || g == 1 ? g : -1;
        out[k++] = o;
    }
    // (v150-host beta 18) a side's goal only when that side has goals (+0x8B0 red, +0x8B4 blue: their counts). In a TEAM
    // SNEAKING round where red sneaks, tsne.h moves the goals to side A and sets side B's count to 0, its places left: both
    // sides looked like attackers (the defending dolls ran to take the item) and the body could not tell who sneaks
    for (int g = 0; g < 2; ++g) {
        for (int j = 0; j < 3; ++j) goals[g][j] = F32(man + (g ? 0x700 : 0x4C0) + 4 * (uint32_t)j);
        const uint32_t count = R32(man + (g ? 0x8B4 : 0x8B0));
        set[g] = count > 0 && count <= 3 && std::fabs(goals[g][0]) + std::fabs(goals[g][2]) > 1.f && std::fabs(goals[g][0]) < 1e6f && std::fabs(goals[g][2]) < 1e6f;
    }
    return k;
}
// (v150-host beta 19, kero) an item lying where no player can take it (the virtual dolls' mesh says so, vdoll_body.cpp
// ItemRule): put back at its home now, as the game's own return does after 180 s (US n003a 0x39C548: the manager's +0x8D0
// copied into the item's +0x90; the host's place goes to every player's game with the manager's sync each pass). Its last
// held place (+0x100, where the game puts a lost item back after a fall under -500 m) is set to the home too
bool VdollItemHome(int i) {
    const uint32_t man = MissionObjects();
    if (!man || i < 0 || i >= (int)std::min<uint32_t>(3, R32(man + 0x410))) return false;   // (the manager's work holds 3 items)
    const uint32_t it = man + 0xB0 + 0x120 * (uint32_t)i;
    if (R32(it + 0xD4) || R32(it + 0xD0) != 0) return false;            // (held, or not lying)
    bool home = true;
    for (int j = 0; j < 3; ++j) home = home && R32(man + 0x8D0 + 4 * (uint32_t)j) == R32(it + 0x90 + 4 * (uint32_t)j);
    if (home) return false;                                              // (at its home already)
    for (int j = 0; j < 4; ++j) { const uint32_t v = R32(man + 0x8D0 + 4 * (uint32_t)j); W32(it + 0x90 + 4 * (uint32_t)j, v); W32(it + 0x100 + 4 * (uint32_t)j, v); }
    return true;
}
int VdollBases(float pos[5][3], int owner[5], int contested[5]) { return BcBaseStates(pos, owner, contested); }
// (v150-host beta 16) a player's class as the host's game holds him (+0xB62: 0 KGB, 1 GRU, 2 the Ocelot Unit, 3 to 7
// the special characters, 8 Snake; docs/TEAM_SNEAKING.md, The heroes), -1 without an object; the class of a team's
// soldiers by the round's rule and the host's settings (red_team, blue_team: 0 GRU, 1 KGB, 2 the Ocelot Unit; deathmatch:
// none, each player's unit is his own; sneaking's blue side is Snake), -1 not known
extern int gHostUnits[2];
extern int gHostWeapons;
int VdollWeaponRule() { return gHostWeapons >= 0 && gHostWeapons <= 7 ? gHostWeapons : -1; }
extern int gHostFF;
int VdollFriendlyFire() { return gHostFF; }                // (v150-host beta 22, wake)
bool VdollHeadshotsOnly() { uint8_t v[0x34]; return RoomVariables(v, sizeof v) && (v[0x31] & 0x08); }   // (the room's flags +0x30: 0x800)
int VdollClass(int id) {
    const uint32_t o = id > 0 && id < 16 ? PlayerOfSlot(id) : 0;
    if (!o) return -1;
    // (the review's P1-4) only a whole copy is trusted. The game's constructor writes the player's id first, his class
    // later and his body last (+0x200): in between the class reads 0, which is KGB. So: a body, a place that is a
    // number (as VdollSee asks), a class of the game's nine
    const uint32_t body = R32(o + 0x200);
    if (body < 0x100000 || body >= 0x02000000) return -1;
    for (int k = 0; k < 3; ++k) if (!(std::fabs(F32(o + 0x110 + 4 * k)) < 1e6f)) return -1;
    const int cls = (int)R8(o + 0xb62);
    return cls <= 8 ? cls : -1;
}
// (v150-host beta 16, the configurations' audit) the soldiers' classes (0 to 2) the host's game holds a body for now
// (+0x200, the body the skeleton's update reads: 0 none) as bits; the class of that team's soldiers among them (-1 none,
// -2 two different ones)
int VdollSoldiers(int team, unsigned* bodies) {
    int seen = -1;
    for (int id = 1; id < 16; ++id) {
        const uint32_t o = PlayerOfSlot(id);
        if (!o || !R32(o + 0x200)) continue;
        const int c = R8(o + 0xb62);
        if (c > 2) continue;
        *bodies |= 1u << c;
        int t = 0xFF;
        if (PlayerInfo(id, &t, nullptr) && t == team) seen = seen == -1 || seen == c ? c : -2;
    }
    return seen;
}
int VdollTeamClass(int team) {
    static const int kUnitClass[3] = {1, 0, 2};
    const int rule = VdollRule();
    if (rule < 0 || team < 0) return -1;
    if (rule == 4 && team == 1) return 8;
    // (the configurations' audit) DEATHMATCH has no team's soldiers: a player's unit is his own (the bench A2_gru_ocu_u1:
    // three virtual dolls picked the unit 0 and the host made each one class 0, KGB, not the GRU the settings' numbering
    // gives); none told, the doll keeps its copy's class (the page holds the three soldiers)
    if (rule == 0) return -1;
    const int unit = team <= 1 ? gHostUnits[team] : -1;
    const int m = unit >= 0 && unit <= 2 ? kUnitClass[unit] : -1;
    // (the configurations' audit) the settings' unit is taken when the game holds its body now (as before); else the class
    // the team's soldiers play with now: the rules page holds that body (SNEAKING's page holds one unit, the one its script
    // takes from the settings; no settings file: no unit known)
    unsigned bodies = 0;
    const int seen = VdollSoldiers(team, &bodies);
    if (m >= 0 && ((bodies >> m) & 1)) return m;
    if (seen >= 0 && seen != m) {
        if (Once(((uint64_t)186 << 40) | ((uint64_t)rule << 16) | ((uint64_t)(team & 0xFF) << 8) | (uint64_t)(seen * 4 + (m + 1))))
            Log("vdoll: the %s soldiers play as class %d, the settings' unit gives %d (rule %d): class %d told", team == 1 ? "blue" : team == 0 ? "red" : "unit's",
                seen, m, rule, seen);
        return seen;
    }
    // SNEAKING: which setting its page's one unit follows is not known here; with no body of the settings' unit in the
    // game and no soldier in that team, none is told (the doll keeps its copy's class, a body the game holds)
    if (rule == 4 && seen < 0) return -1;
    return m;
}
bool VdollSpawnGuard() { return SpawnCharacterGuardOn(); }   // (ps2world.cpp, the character a spawn asks for)
// (v150-host beta 19, the virtual dolls' live level: vdoll_adapt.h) a player's counters in this match (his variable set: +0x14
// kills, +0x16 deaths, +0xC4 his VS RATING, as the playable doll reads them: doll_brain.cpp AdaptFrame, VsRating) and the
// room's round wins and tickets (the room's variable set: +0x42 + 4 t, +0x44 + 4 t, doll.cpp SeeRoom)
bool VdollScore(int id, int* kills, int* deaths, int* rating) {
    uint8_t v[0xC6];
    if (id < 0 || id >= 16 || !VariableSet(id + 1, v, sizeof v)) return false;
    *kills = v[0x14] | (v[0x15] << 8); *deaths = v[0x16] | (v[0x17] << 8); *rating = v[0xC4] | (v[0xC5] << 8);
    return true;
}
bool VdollRoomScore(int wins[2], int tickets[2]) {
    uint8_t v[0x4C];
    if (!RoomVariables(v, sizeof v)) return false;
    for (int t = 0; t < 2; ++t) { wins[t] = v[0x42 + 4 * t]; tickets[t] = v[0x44 + 4 * t] | (v[0x45 + 4 * t] << 8); }
    return true;
}
// (v150-host beta 21) a claymore sets itself off for one player only: each program's own (its sensor's callback, US n003a
// 0x3A8740: a target in its cone, its line clear, then its owner [+0x10] & 0xFFFFFF against this program's player's id, the
// link 0x23C790 + 0x14 = the player's +0x410; not when he crawls). That program's copy goes off (flag 4 of +0x12C: the blast
// and the claymore manager's state 3 told to all, 0x3A6168) and the owner's own claymore then wounds whoever is in its blast
// (the hit is made where the attacker is: 0x437F20 names this program's slot, the records 04 go to the victims). A virtual
// doll has no program of its own: nothing ever set a claymore off for it (the players' "immune to claymores"). The host's
// own copy of every claymore does it for a virtual doll, through the game's own test: the doll's id as this program's
// player for that one call. true: served (the target a virtual doll's copy)
uint32_t UserIdOf(uint32_t id);
// MGO1_VDOLL_TRAPS=0: off (beta 20's: nothing sets a claymore off for a virtual doll). MGO1_VDOLL_TRAP_TRACE=1 (tests):
// the sensor's calls on the host logged (each claymore and target at its first call, every 120th, and each one that sets it off)
bool VdollTrapsOn() { static const bool on = !(getenv("MGO1_VDOLL_TRAPS") && !strcmp(getenv("MGO1_VDOLL_TRAPS"), "0")); return on; }
bool VdollTrapSensor(Ctx& c, uint32_t va) {
    static const bool trace = getenv("MGO1_VDOLL_TRAP_TRACE") != nullptr;
    if (!IsSessionHost() || (!trace && (!vdoll::On() || !VdollTrapsOn()))) return false;
    const uint32_t tgt = (uint32_t)c.r[4], work = (uint32_t)c.r[6];
    ps2::Fn self = ps2::FindFn(va), ownerOf = ps2::FindFn(V(0x14F508));
    if (!self || !ownerOf || tgt < 0x100000 || tgt >= 0x02000000 || work < 0x100000 || work + 0x160 > 0x02000000) return false;
    Ctx t = c;
    t.r[4] = tgt;
    ownerOf(t);
    const uint32_t o = (uint32_t)t.r[2];
    const bool own = o >= 0x100000 && o + 0x14 <= 0x02000000;
    const uint32_t id = own ? R32(o + 0x10) & 0xFFFFFF : 0xFFFFFFFFu;
    vtrap::Seat seats[16];
    int any = -1;
    for (int s = 0; own && s < 16; ++s) {
        const uint32_t p = PlayerOfSlot(s);
        if (!p) continue;
        vtrap::Seat& q = seats[s];
        q.present = true; q.doll = vdoll::NumberOfId(UserIdOf((uint32_t)s)) != 0;
        q.id = R32(p + 0x410) & 0xFFFFFF; q.life = (int16_t)R16(p + 0xb64);
        q.bits0 = R32(p + 0xB2C); q.bits2 = R32(p + 0xB2C + 8); q.bits10 = R32(p + 0xB2C + 40);
        if (any < 0 && q.id == id) any = s;
    }
    const int slot = own ? vtrap::StandIn(seats, 16, id, vdoll::On() && VdollTrapsOn()) : -1;
    if (slot < 0 && !trace) return false;
    // the game's test reads this program's player twice: his id (the link's +0x14) and his state (the link's bits from
    // +0x1C, 0x179560: 3, 0x57 and 0x156 keep the claymore quiet: crawling, ..., a ghost). For the doll: its id, and its own
    // copy's state (the player's bits at +0xB2C) in the place of this program's (vdoll_trap.h)
    const uint32_t link = V(0x23C7A4), bits = V(0x23C7AC), was = R32(link), flags = R32(work + 0x12C);
    const uint32_t b0 = R32(bits), b2 = R32(bits + 8), b10 = R32(bits + 40);
    if (slot >= 0) {
        vtrap::Own host; host.id = was; host.bits0 = b0; host.bits2 = b2; host.bits10 = b10;
        const vtrap::Own d = vtrap::With(host, seats[slot]);
        W32(link, d.id); W32(bits, d.bits0); W32(bits + 8, d.bits2); W32(bits + 40, d.bits10);
    }
    const uint64_t ra = c.r[31];
    self(c);
    c.r[31] = ra;
    if (slot >= 0) { W32(link, was); W32(bits, b0); W32(bits + 8, b2); W32(bits + 40, b10); }
    const bool went = (int32_t)c.r[2] == 0 && !(flags & 4) && (R32(work + 0x12C) & 4);
    if (trace) {
        static std::map<uint64_t, int> calls;
        const int n = calls[((uint64_t)work << 32) | tgt]++;
        if (n == 0 || n % 120 == 0 || went)
            Log("vdoll trap trace: claymore %08x target %08x (its +0x24 %x) owner %08x id %06x: player %d%s; this program's id %06x, its bits %x %x %x, its slot %d; returns %d, its flags %x -> %x%s (call %d)",
                work, tgt, R32(tgt + 0x24), own ? o : 0, id & 0xFFFFFF, any + 1, slot >= 0 ? " (a virtual doll alive)" : "", was & 0xFFFFFF, b0, b2, b10, OwnSlot() + 1,
                (int32_t)c.r[2], flags, R32(work + 0x12C), went ? ": SET OFF" : "", n + 1);
    }
    if (went && slot >= 0) {
        // (tests: MGO1_VDOLL_TEST_TRAP_LIFE=<n>) the host's life for that doll lowered to n first, so that one claymore
        // (100 of a player's 200 at most) kills it: its death, its kind and its credit seen in a bench
        static const int testLife = getenv("MGO1_VDOLL_TEST_TRAP_LIFE") ? atoi(getenv("MGO1_VDOLL_TEST_TRAP_LIFE")) : 0;
        const uint32_t dp = PlayerOfSlot(slot);
        if (testLife > 0 && dp && (int16_t)R16(dp + 0xb64) > testLife) {
            Log("MGO1_VDOLL_TEST_TRAP_LIFE: player %d's life %d set to %d", slot + 1, (int16_t)R16(dp + 0xb64), testLife);
            W16(dp + 0xb64, (uint16_t)testLife);
        }
        const uint32_t m = R32(work + 0x48);
        if (m >= 0x100000 && m + 0x4C < 0x02000000)
            Log("vdoll %d: sets off a claymore at %.0f %.0f %.0f (player %d in its sensor, as his own game does)", vdoll::NumberOfId(UserIdOf((uint32_t)slot)),
                F32(m + 0x40), F32(m + 0x44), F32(m + 0x48), slot + 1);
        else Log("vdoll %d: sets off a claymore (player %d in its sensor, as his own game does)", vdoll::NumberOfId(UserIdOf((uint32_t)slot)), slot + 1);
    }
    return true;
}
// (v150-host beta 21, tests: MGO1_VDOLL_TEST_TRAP) the traps lying in the host's game: the claymores (NewOnlineClaymore,
// NewRecvOnlineClaymore: +0x48 its model) and the books (NewYMBookOnline, NewYMBookFromGhost: +0x188 its model), the
// place at the model's +0x40
// (v150-host beta 22, book) the books lying in the host's game for the virtual dolls' book rule (vdoll_book.h, docs/VIRTUAL_DOLL.md
// 30): the book list (NewOnlineBookMan, US n003a 0x3A7038, every page's the same: 16 entries of 0x40 bytes from +0xC0: +0x10
// the place, +0x2C the id (the layer's slot << 10, a count), +0x30 the state (1 lying), +0x34 the read time left; +0x4D4 the
// read time a reading starts), its entries in state 1 in their order, as a player's own game reads them (0x3A7378, 0x3A7398)
uint32_t VdollBookList() { return ActorByName("NewOnlineBookMan", 0); }
int VdollBooks(vdoll::BookNow* out, int max) {
    const uint32_t man = VdollBookList();
    if (man < 0x100000 || man + 0x4E0 > 0x02000000) return 0;
    int n = 0;
    for (int i = 0; i < 16 && n < max; ++i) {
        const uint32_t e = man + 0xC0 + 0x40 * (uint32_t)i;
        if (R32(e + 0x30) != 1) continue;
        vdoll::BookNow& b = out[n];
        b.id = R32(e + 0x2C); b.slot = (int)(b.id >> 10);
        for (int k = 0; k < 3; ++k) b.pos[k] = F32(e + 0x10 + 4 * (uint32_t)k);
        b.readTime = (int)R32(man + 0x4D4);
        if (std::fabs(b.pos[0]) < 1e6f && std::fabs(b.pos[1]) < 1e6f && std::fabs(b.pos[2]) < 1e6f) ++n;
    }
    return n;
}
// a book a virtual doll reads: its read time started as a reader's own game starts it (0x3A7468: the entry's +0x34 set to the
// list's +0x4D4 when it is 0). The game's list then counts it down and at its end takes the book away and tells the players
// (0x3A6AC8 -> 0x3A7300, the state 2 sent: the book gone on every screen), as after a player's reading
bool VdollReadBook(uint32_t id) {
    const uint32_t man = VdollBookList();
    if (man < 0x100000 || man + 0x4E0 > 0x02000000) return false;
    for (int i = 0; i < 16; ++i) {
        const uint32_t e = man + 0xC0 + 0x40 * (uint32_t)i;
        if (R32(e + 0x30) != 1 || R32(e + 0x2C) != id) continue;
        if (R32(e + 0x34) == 0) W32(e + 0x34, R32(man + 0x4D4));
        return true;
    }
    return false;
}
// a doll's reading over by the machine's clock: the book's read time left set to 1 pass (the game's list then takes it away at
// its next pass and tells the players, as at the end of a reader's own read time)
bool VdollBookDone(uint32_t id) {
    const uint32_t man = VdollBookList();
    if (man < 0x100000 || man + 0x4E0 > 0x02000000) return false;
    for (int i = 0; i < 16; ++i) {
        const uint32_t e = man + 0xC0 + 0x40 * (uint32_t)i;
        if (R32(e + 0x30) != 1 || R32(e + 0x2C) != id) continue;
        if ((int32_t)R32(e + 0x34) > 1) W32(e + 0x34, 1);
        return true;
    }
    return false;
}
int VdollTraps(float out[][3], int max) {
    int n = 0;
    for (const auto& a : gNativeActors) {
        if (n >= max) break;
        const bool mine = a.second == "NewRecvOnlineClaymore" || a.second == "NewOnlineClaymore";
        const bool book = a.second == "NewYMBookOnline" || a.second == "NewYMBookFromGhost";
        if (!mine && !book) continue;
        const uint32_t m = R32(a.first + (mine ? 0x48 : 0x188));
        if (m < 0x100000 || m + 0x4C > 0x02000000) continue;
        for (int j = 0; j < 3; ++j) out[n][j] = F32(m + 0x40 + 4 * (uint32_t)j);
        if (std::fabs(out[n][0]) < 1e6f && std::fabs(out[n][2]) < 1e6f && (out[n][0] != 0.f || out[n][2] != 0.f)) ++n;
    }
    return n;
}
// (v150-host beta 22, support) the claymores lying in the host's game for the virtual dolls (vdoll_support.h, docs/VIRTUAL_DOLL.md
// 35): the claymores' list (NewOnlineClaymoreMan, US n003a 0x3A5D20, the same layout on every page: 16 entries of 0xA0 bytes from
// the list itself: +0xC0 the copy's actor, +0xD0 its matrix (rows: the axes, then the place at +0x100), +0x150 the record's last
// word, +0x154 the id (the layer's slot << 10 | 0x100 + n), +0x158 the state (1 lying, 3 gone off, 4 taken))
uint32_t VdollClayList() { return ActorByName("NewOnlineClaymoreMan", 0); }
int VdollClaymores(vdoll::ClayNow* out, int max) {
    const uint32_t man = VdollClayList();
    if (man < 0x100000 || man + 0xAC0 > 0x02000000) return 0;
    int n = 0;
    for (uint32_t i = 0; i < 16 && n < max; ++i) {
        const uint32_t e = man + 0xA0 * i;
        if (R32(e + 0x158) != 1) continue;
        vdoll::ClayNow& c = out[n];
        c.id = R32(e + 0x154); c.slot = (int)((c.id >> 10) & 0xFF); c.word = R32(e + 0x150);
        for (int k = 0; k < 3; ++k) c.pos[k] = F32(e + 0x100 + 4 * (uint32_t)k);
        c.yaw = std::atan2(F32(e + 0xD0 + 0x20), F32(e + 0xD0 + 0x28));   // (the matrix's third row: the claymore's own z axis)
        if (c.id >= 0x100 && std::fabs(c.pos[0]) < 1e6f && std::fabs(c.pos[1]) < 1e6f && std::fabs(c.pos[2]) < 1e6f && std::isfinite(c.yaw)) ++n;
    }
    return n;
}
// a claymore's act (US n003a 0x3A8ED0, every page's own; through the dispatcher): the act that finds the flag 4 set (its sensor,
// a hit, or the list's state 3 that came) makes the blast and tells the list. The host's copy of a virtual doll's claymore (the
// owner -1: a copy's blast wounds nobody) noted here for the doll: no game of its own makes the layer's wounding blast
struct VdollClayBoom { uint32_t id; float at[3]; };
std::vector<VdollClayBoom> gVdollClayBooms;
void VdollClayAct(Ctx& c) {
    if (!IsSessionHost() || !vdoll::On()) return;
    const uint32_t w = (uint32_t)c.r[4];
    if (w < 0x100000 || w + 0x160 > 0x02000000 || !(R32(w + 0x12C) & 4) || (int32_t)R32(w + 0x128) != -1) return;
    const uint32_t id = R32(w + 0x148), m = R32(w + 0x48);
    if (id < 0x100 || id == 0xFFFFFFFFu || m < 0x100000 || m + 0x4C > 0x02000000) return;
    if (!vdoll::NumberOfId(UserIdOf((id >> 10) & 0xFF))) return;   // (a real player's claymore: his own game wounds)
    if (gVdollClayBooms.size() >= 32) return;
    VdollClayBoom b; b.id = id;
    for (int k = 0; k < 3; ++k) b.at[k] = F32(m + 0x40 + 4 * (uint32_t)k);
    gVdollClayBooms.push_back(b);
}
int VdollClayBlasts(uint32_t id[], float at[][3], int max) {
    int n = 0;
    for (const auto& b : gVdollClayBooms) { if (n >= max) break; id[n] = b.id; memcpy(at[n], b.at, sizeof b.at); ++n; }
    gVdollClayBooms.clear();
    return n;
}
// (v150-host beta 22, pickup) the drops' records of the host's game for the virtual dolls (DropRecords below)
int VdollDrops(vdoll::DropNow* out, int max) {
    if (!IsSessionHost()) return 0;
    DropRec r[16];
    const int n = DropRecords(r, std::min(max, 16));
    for (int i = 0; i < n; ++i) {
        vdoll::DropNow& d = out[i];
        d.number = r[i].number; d.kind = r[i].kind; d.code = r[i].code; d.count = r[i].count; d.state = r[i].state; d.flags = r[i].flags;
        memcpy(d.pos, r[i].pos, sizeof d.pos); d.box = r[i].box; d.reach = r[i].reach;
    }
    return n;
}
// (v150-host beta 25, food) the food of the host's game for the virtual dolls (FoodRecords below)
int VdollFoods(vdoll::FoodNow* out, int max) {
    if (!IsSessionHost()) return 0;
    FoodRec r[16];
    const int n = FoodRecords(r, std::min(max, 16));
    for (int i = 0; i < n; ++i) {
        vdoll::FoodNow& f = out[i];
        f.number = r[i].number; f.state = r[i].state; f.code = r[i].code; f.gain = r[i].gain; f.timer = r[i].timer;
        memcpy(f.at, r[i].at, sizeof f.at); memcpy(f.pos, r[i].pos, sizeof f.pos); f.box = r[i].box; f.reach = r[i].reach;
    }
    return n;
}
// (v150-host beta 22, blast) the smoke grenades' clouds of the host's game: NewNetworkSmokeManager's 8 entries of 0x20 bytes
// from +0x40 (US n003a 0x455830 writes them, 0x4558A0 counts their time down by the pass's step, 0x455710 asks them for the
// auto aim): +0 the middle, +0x10 the radius, +0x18 the time left (over 0: out). The host makes its own copy of every
// smoke grenade thrown, as every game does
int VdollSmokes(float c[][3], float r[], int left[], int max) {
    const uint32_t m = ActorByName("NewNetworkSmokeManager");
    if (m < 0x100000 || m + 0x140 > 0x02000000) return 0;
    int n = 0;
    for (uint32_t k = 0; k < 8 && n < max; ++k) {
        const uint32_t e = m + 0x40 + 0x20 * k;
        const int t = (int32_t)R32(e + 0x18);
        const float rr = F32(e + 0x10);
        if (t <= 0 || !(rr > 100.f && rr < 20000.f)) continue;
        for (int j = 0; j < 3; ++j) c[n][j] = F32(e + 4 * (uint32_t)j);
        if (!(std::fabs(c[n][0]) < 1e6f && std::fabs(c[n][1]) < 1e6f && std::fabs(c[n][2]) < 1e6f)) continue;
        r[n] = rr; left[n] = t; ++n;
    }
    return n;
}
// (v150-host beta 22, blast) the stun grenades' blasts of the host's game (NewStunBlast, US n003a 0x4FFEA0; its place +0x50,
// written by 0x4FFC10): every game makes its own of each stun grenade's blast and whitens its own screen from it
int VdollStunBlasts(uint32_t id[], float at[][3], int max) {
    int n = 0;
    for (const auto& a : gNativeActors) {
        if (n >= max) break;
        if (a.second != "NewStunBlast" || a.first < 0x100000 || a.first + 0x60 > 0x02000000) continue;
        for (int j = 0; j < 3; ++j) at[n][j] = F32(a.first + 0x50 + 4 * (uint32_t)j);
        if (!(std::fabs(at[n][0]) < 1e6f && std::fabs(at[n][1]) < 1e6f && std::fabs(at[n][2]) < 1e6f)) continue;
        id[n] = a.first; ++n;
    }
    return n;
}
// ---- (v150-host beta 21, geom) a virtual doll's line through the map tested by the game's own collision, the map the host's
// game loaded being the authority (the disc's and the added maps alike, nothing extracted). The game's line test 0x15A590
// (a0 the kinds: 1 and 4 the stage's grid, 2 and 8 the things registered while the round runs, 0x10 no face kind left out;
// a1 the grid's layers, 0x23BC18's word; a2 the attribute bits of a face that counts; a3, t0 the two ends; f12 0 a line).
// Asked as the game's own bullet asks it for the things (the kinds 0x1A: tree trunks, props, cardboard boxes, a shield:
// they are in no collision file) with the bits 0x42 (a face that stops a bullet; one with neither bit is a mark the
// bullet goes past): the hit's thing has a hardness (+0x20; a face of the grid 220) and a bullet goes through a thing
// softer than its power. The bodies hold every face of the stage's own collision (a superset of what stops a bullet on
// the 15 maps, tools/dev150/geom_audit); 0x1F (the grid too) is the comparison of MGO1_VDOLL_GEOM_AUDIT. Only on the
// game's thread, inside the actor pass (VdollTick's context) ----
Ctx* gVdollCtx = nullptr;
uint32_t gVdollLineMem = 0;
void VdollWF32(uint32_t at, float v) { uint32_t u; memcpy(&u, &v, 4); W32(at, u); }
int VdollGameLine(const float* a, const float* b, bool stage, vdoll::LineHit* hit) {
    if (!gVdollCtx || !IsSessionHost()) return -1;
    static ps2::Fn line, count, rec, recAttr, recObj, recPos;
    static uint32_t gen = ~0u;
    if (gen != gPageGen) {                                   // (the page's functions and the versions' addresses)
        gen = gPageGen;
        line = ps2::FindFn(V(0x15A590)); count = ps2::FindFn(V(0x150AD0)); rec = ps2::FindFn(V(0x150B08));
        recAttr = ps2::FindFn(V(0x150A20)); recObj = ps2::FindFn(V(0x150BF0)); recPos = ps2::FindFn(V(0x150C40));
    }
    if (!line) return -1;
    if (!stage && (int32_t)R32(V(0x289FC0)) <= 0) return 0;  // (no thing registered now: the test's own first look, without the call)
    if (!gVdollLineMem) gVdollLineMem = Alloc(64);
    if (!gVdollLineMem) return -1;
    for (int k = 0; k < 3; ++k) { VdollWF32(gVdollLineMem + 4 * k, a[k]); VdollWF32(gVdollLineMem + 16 + 4 * k, b[k]); }
    VdollWF32(gVdollLineMem + 12, 1.f); VdollWF32(gVdollLineMem + 28, 1.f);
    Ctx t = *gVdollCtx;
    t.r[4] = stage ? 0x1F : 0x1A; t.r[5] = R32(V(0x23BC18)); t.r[6] = 0x42; t.r[7] = gVdollLineMem; t.r[8] = gVdollLineMem + 16; t.f[12] = 0.f;
    line(t);
    if (!(uint32_t)t.r[2]) return 0;
    if (hit && count && rec && recAttr && recObj && recPos) {
        *hit = vdoll::LineHit();
        Ctx u = *gVdollCtx;
        count(u);
        const uint32_t idx = (uint32_t)u.r[2];
        { Ctx w = *gVdollCtx; w.r[4] = idx; rec(w); w.r[4] = w.r[2]; recAttr(w); hit->attr = (uint32_t)w.r[2]; }
        { Ctx w = *gVdollCtx; w.r[4] = idx; recObj(w);
          const uint32_t o = (uint32_t)w.r[2];
          if (o >= 0x100000 && o < 0x02000000 - 0x24) { hit->object = true; hit->objectId = R32(o); hit->hardness = (int32_t)R32(o + 0x20); } }
        { Ctx w = *gVdollCtx; w.r[4] = idx; w.r[5] = gVdollLineMem + 32; recPos(w); for (int k = 0; k < 3; ++k) hit->at[k] = F32(gVdollLineMem + 32 + 4 * k); }
    }
    return 1;
}
void VdollTick(Ctx& c) {
    static bool init = false;
    if (!init) {
        init = true;
        vdoll::Host h;
        h.log = VdollLog; h.gen = VdollGen; h.player = VdollPlayer; h.spawnPoint = VdollSpawnPoint;
        h.players = VdollSeenPlayers; h.rule = VdollRule; h.phase = VdollPhase; h.seats = VdollSeats; h.roomChannel = VdollRoomChannel;
        h.mapNow = VdollMapNow; h.stageGeom = VdollStageGeom; h.see = VdollSee; h.realMs = VdollRealMs; h.objectives = VdollObjectives; h.bases = VdollBases;
        h.playerClass = VdollClass; h.teamClass = VdollTeamClass; h.weaponRule = VdollWeaponRule; h.headshotsOnly = VdollHeadshotsOnly;
        h.friendlyFire = VdollFriendlyFire;                // (v150-host beta 22, wake)
        h.spawnGuard = VdollSpawnGuard;
        h.score = VdollScore; h.room = VdollRoomScore;   // (v150-host beta 19: the live level)
        h.itemHome = VdollItemHome;                          // (beta 19, kero)
        h.gameLine = VdollGameLine;                          // (beta 21, geom)
        h.traps = VdollTraps;                                // (beta 21, tests)
        h.books = VdollBooks; h.readBook = VdollReadBook; h.bookDone = VdollBookDone;   // (beta 22, book)
        h.drops = VdollDrops;                                // (beta 22, pickup)
        h.smokes = VdollSmokes; h.stunBlasts = VdollStunBlasts;   // (beta 22, blast)
        h.claymores = VdollClaymores; h.clayBlasts = VdollClayBlasts;   // (beta 22, support)
        h.foods = VdollFoods;                                // (v150-host beta 25, food)
        vdoll::Init(h);
    }
    static bool wasHost = false;
    if (!vdoll::On()) return;
    if (!IsSessionHost()) {                                  // (the host's game listening: created)
        if (wasHost) { wasHost = false; vdoll::HostGameGone(gFrameNo); }
        return;
    }
    wasHost = true;
    {   // the map's respawn points read ahead, off the game's thread ((v150-host beta 22, sync) of one stage: StageNow)
        std::string page;
        int ex = 0;
        if (StageNow(&page, &ex)) VdollSpawnsFor(page, ex);
    }
    const bool mineEu = !gUsProgram && !strcmp(ps2::SelectedVersion(), "EU");
    const int r = gBigGame ? gGameSet : (mineEu ? 1 : 0);
    vdoll::SetDialect(SetKey(r), SetMac(r));
    const long long t0 = RealQpc();
    gVdollCtx = &c;                                          // (beta 21, geom: the game's line test for the bodies)
    vdoll::Tick(gFrameNo);
    gVdollCtx = nullptr;
    VdollCostAdd(RealQpc() - t0, true);
}
#endif
#ifndef MGO1_PLAYER_BUILD
// (v141-host beta 13) the host: a player alive in a round being played who has not moved 1 m in 30 s, logged once a spell
// with what his input records held (Snake's frozen arrival of 20 October: alive at his spawn point through two rounds)
namespace report { uint32_t SetData(uint32_t p); }
struct Still { uint32_t obj = 0; float p[3] = {}; ULONGLONG since = 0; bool told = false; };
Still gStill[16];
void PlayerStillWatch() {
    static ULONGLONG last = 0;
    const ULONGLONG now = PsMs();
    if (now - last < 1000 || !IsSessionHost()) return;
    last = now;
    const uint32_t d = report::SetData(0);
    const int phase = d ? (int)R8(d) : -1;
    for (int slot = 0; slot < 16; ++slot) {
        Still& w = gStill[slot];
        const uint32_t o = PlayerOfSlot(slot);
        const bool alive = o && (int16_t)R16(o + 0xb64) > 0 && !(R32(o + 0xb48) & 0x1000000);
        if (!alive || phase != 2 || o != w.obj) { w = Still(); w.obj = o; continue; }
        float p[3]; for (int k = 0; k < 3; ++k) p[k] = F32(o + 0x110 + 4 * k);
        const float dx = p[0] - w.p[0], dz = p[2] - w.p[2];
        if (!w.since || dx * dx + dz * dz > 1000.f * 1000.f) { memcpy(w.p, p, sizeof p); w.since = now; w.told = false; continue; }
        if (w.told || now - w.since < 30000) continue;
        w.told = true;
        const SlotNet& n = gSlotNet[slot];                // (no name: an account doll's is never logged)
        Log("session: player %d alive at %.0f %.0f %.0f for 30 s of play: %s", slot + 1, p[0], p[1], p[2],
            n.moving > w.since ? "his input records not empty (a button or the stick)" : n.pos > w.since ? "his input records empty" : "no record from him");
    }
}
#endif
bool BridgeOff() {                                       // MGO1_PROTOCOL_BRIDGE=0 (tests): a PS2's behaviour
#ifdef MGO1_PLAYER_BUILD
    return false;
#else
    static const bool off = DevEnv("MGO1_PROTOCOL_BRIDGE") && !strcmp(DevEnv("MGO1_PROTOCOL_BRIDGE"), "0");
    return off;
#endif
}
void ProtocolRecords(bool out, uint32_t session, uint32_t buf, uint32_t len) {
    const bool off = BridgeOff();
    buf &= 0x01ffffff; session &= 0x01ffffff;
    if (len <= 2 || buf + len > 0x02000000) return;
    if (SpawnFixOn()) {
        const int self = OwnSlot();
        ForPlayerRecords(gMem + buf + 2, len - 2, [&](uint8_t* r, uint8_t* d, size_t l, int slot, int kind) {
            const bool spawn = SimSpawnLost() && out && kind == 0 && l >= 12 && d[0] == 0x02 && d[1] && slot == self && self >= 0;
            if (spawn) {                                  // tests: every spawn with the first one's point and heading
                static uint8_t first[8]; static bool have = false;
                if (!have) { memcpy(first, d + 4, 8); have = true; } else memcpy(d + 4, first, 8);
            }
            SpawnRecords(out, d, l, slot, kind, self);
            if (spawn && !gMySpawn.lastOwn) {             // the game's own send lost (the platform's go through)
                r[0] = 0x3f; r[1] &= (uint8_t)~7;
                Log("session: tests: this player's spawn record dropped (SIM_SPAWN_LOST)");
            }
        });
    }
    if (off) return;
    if (!gSlotProtoInit) { gSlotProtoInit = true; for (int k = 0; k < 32; ++k) gSlotProto[k] = -1, gSlotDown[k] = 0, gSlotSession[k] = 0; }
    {   // a player id's session changed (the player left, another took the slot): its protocol unknown again
        const uint32_t table = V(0x28B5F4);
        uint32_t now[32] = {};
        for (int k = 0; k < 16; ++k) {
            const uint32_t e = table + 0x5C * k;
            if (R16(e + 2) && R8(e) < 32) now[R8(e)] = R32(e + 4) ? R32(e + 4) : 1;
        }
        for (int k = 0; k < 32; ++k)
            if (now[k] != gSlotSession[k]) { gSlotSession[k] = now[k]; gSlotProto[k] = -1; gSlotDown[k] = 0; }
    }
    uint8_t* p = gMem + buf + 2;
    const size_t n = len - 2;
    const int own = OwnProto();
    auto neutral = [](uint8_t* r) { r[0] = 0x3f; r[1] &= (uint8_t)~7; };   // an unopened channel: dropped on receipt
    if (!out) {
        const int self = OwnSlot();                     // this player's own records come back from the host: no evidence
        int before[32]; memcpy(before, gSlotDown, sizeof before);
        {   // a peer sending the EU and KR programs' game channels (ChannelFamilies) to a program of the others:
            // its own, the newer protocol (a PC sends its receiver's channels: to an EU or KR program, none
            // of this; seen: an EU host took a JP PC for a newer program)
            const auto f = gPeerChanFamily.find(PeerOf(session));
            const int ps = PeerSlot(session);
            if (f != gPeerChanFamily.end() && f->second == 1 && OwnChanFamily() == 0 && ps >= 0 && ps != self)
                SetSlotProto(ps, 1, "its game channels, the EU and KR programs'");
        }
        const int from = PeerSlot(session);
        ForPlayerRecords(p, n, [&](uint8_t* r, uint8_t* d, size_t l, int slot, int kind) {   // the down states and the evidence first
            if (kind == 1 && d[0] == 0x0a && l >= 3) gSlotDown[slot] = d[2];
            if (slot == self) return;
            // the forms of 07 and 0F say which program wrote them: evidence only from their player himself
            // (a host relays another player's records as its own handlers write them)
            if (kind == 1 && d[0] == 0x07 && l == 2 && (d[1] >> 4) == 1 && slot == from) SetSlotProto(slot, 1, "its one-byte 07 record");
            if (kind == 1 && d[0] == 0x0f && (l == 4 || (l == 8 && !LadderUniversal(d))) && slot == from)
                SetSlotProto(slot, l == 4, l == 8 ? "a ladder's record of 8 bytes" : "a ladder's record of 4 bytes");
            if (kind == 0 && d[0] == 0x03 && l >= 3 && d[2] == 4) SetSlotProto(slot, 1, "its knocked out event 4");
        });
        ForPlayerRecords(p, n, [&](uint8_t* r, uint8_t* d, size_t l, int slot, int kind) {
            if (kind == 0 && d[0] == 0x03 && l >= 3 && d[2] == 3) {
                const bool down = DownState(gSlotDown[slot]) || DownState(before[slot]);   // the datagram of the change: either
                if (slot != self) SetSlotProto(slot, down ? 0 : 2, down ? "its event 3 while knocked out" : "its event 3 while up");
                // this player's own record comes back as it was sent: in the protocol of the one it went to
                int fam = slot == self ? (from >= 0 && from < 32 ? gSlotProto[from] : -1) : gSlotProto[slot];
                if (fam == 2) fam = 1;
                if (own == 1 && fam == 0) d[2] = 4;                                  // an older player knocked out
                else if (own == 0 && fam == 1) d[2] = 0;                             // the newer protocol's own event
            } else if (kind == 0 && d[0] == 0x03 && l >= 3 && d[2] == 4 && own == 0) d[2] = 3;
            else if (kind == 1 && d[0] == 0x07 && l == 2 && (d[1] >> 4) == 1 && own == 0) neutral(r);
            else if (kind == 1 && d[0] == 0x0f && l == 8 && own == 1) {        // what a newer handler reads: 4 bytes
                const uint8_t flag = LadderUniversal(d) ? (uint8_t)(d[6] & 1) : 0; // a newer PC's flag, from z
                if (!LadderUniversal(d)) MakeLadderUniversal(d);                  // an older PS2's
                d[1] = (uint8_t)((d[1] < 8 ? d[1] : 0x08) | flag << 7);           // off: the newer 8, not 127
            } else if (kind == 1 && d[0] == 0x0f && l == 8 && own == 0) {        // an older handler: -1 off, no flag
                if (LadderUniversal(d) && (d[1] & 0x7f) < 8 && slot >= 0 && slot < 32) gLadderCopy[slot].flag = (d[6] & 1) != 0;
                if (slot >= 0 && slot < 32) gLadderCopy[slot].newer = LadderUniversal(d) && (d[6] & 4);
                if (LadderUniversal(d) && (d[6] & 2)) { d[4] = d[2]; d[5] = d[3]; }  // held for older PS2s: here his height (LadderSpeed)
                d[1] = (d[1] & 0x7f) < 8 ? (uint8_t)(d[1] & 0x7f) : 0xff;       // (an older PC taken for a newer one sends 8)
            }
            if (kind == 1 && d[0] == 0x0f && own == 1 && slot == from && slot != self && slot >= 0 && slot < 32 && (d[1] & 0x7f) < 8)
                gRelayFlag[slot] = (d[1] & 0x80) != 0;                         // his own record, in this program's form
        });
        return;
    }
    // a newer host relays a climber's 0F without his flag (its handler writes idx & 0x7F: US n001a
    // 0x44A6D0): the third screens' copies climbed at the speeds without it. Put back from his own records
    if (own == 1 && IsSessionHost()) {
        const int self = OwnSlot();
        ForPlayerRecords(p, n, [&](uint8_t* r, uint8_t* d, size_t l, int slot, int kind) {
            if (kind == 1 && d[0] == 0x0f && l == 4 && slot != self && slot >= 0 && slot < 32 && d[1] < 8 && gRelayFlag[slot]) {
                d[1] |= 0x80;
                if (Once(((uint64_t)47 << 40) | (uint32_t)slot)) Log("session: player %d's ladder record relayed with his flag", slot + 1);
            }
        });
    }
    const int to = PeerSlot(session);
    const int theirs = to >= 0 && to < 32 ? (gSlotProto[to] == 2 ? 1 : gSlotProto[to]) : -1;
    if (theirs == own) return;
    if (own == 0)                                        // an older program's ladder: in the form both read, off 8 to a newer one
        ForPlayerRecords(p, n, [&](uint8_t* r, uint8_t* d, size_t l, int slot, int kind) {
            if (kind != 1 || d[0] != 0x0f || l != 8) return;
            MakeLadderUniversal(d);
            if (theirs == 1 && d[1] == 0xff) d[1] = 0x08;
        });
    ForPlayerRecords(p, n, [&](uint8_t* r, uint8_t* d, size_t l, int slot, int kind) {
        if (kind == 0 && d[0] == 0x03 && l >= 3) {
            if (own == 1 && theirs == 0) d[2] = d[2] == 4 ? 3 : d[2] == 3 ? 0 : d[2];
            else if (own == 0 && d[2] == 3) d[2] = 4;                                 // unknown or newer: the newer knocked out event
        } else if (kind == 1 && d[0] == 0x07 && l == 2 && (d[1] >> 4) == 1 && own == 1 && theirs == 0) neutral(r);
    });
}
// the length of a datagram where the callers of the packet recorder (0x178928) keep it, the same code in the
// five programs: on send the send function's s2 (0x16F420: 'move s2, s6' ... 'move a2, s2', jal; its buffer
// 16 KB) and its end s3 (buffer + length: where the 10 bytes of the check go when the datagram is not
// compressed, 0x16F7E0; a compressed one's end is made again from its length), on receipt the receive
// function's frame (0x16FD58: 'sw s0, 0x6c(sp)' six words before the return, 0x68(sp) the buffer, 0x6C(sp)
// the length and the 10 bytes of the check; its buffer 0x578 bytes). A sent datagram's check written at
// its former end fails on receipt, and the cipher's state of the two sides parts from there on
bool DatagramLenKnown(const Ctx& c, bool out, uint32_t len) {
    const uint32_t sp = (uint32_t)c.r[29], ra = (uint32_t)c.r[31];
    if (out) return R32(ra - 0x18) == 0x02c0902d && R32(ra - 0xc) == 0x0240302d && (uint32_t)c.r[18] == len &&
                    (uint32_t)c.r[19] == (uint32_t)c.r[5] + len;
    return R32(ra - 0x18) == 0xafb0006c && R32(sp + 0x68) == (uint32_t)c.r[5] && R32(sp + 0x6c) == len + 0xa;
}
uint32_t DatagramCap(bool out) { return out ? 1200 : 0x578 - 0xa; }   // a sent one kept short of the receive buffer
void SetDatagramLen(Ctx& c, bool out, uint32_t len, uint32_t now) {
    c.r[6] = now;
    if (out) { c.r[18] = now; c.r[19] = (uint32_t)c.r[5] + now; }
    else { W32((uint32_t)c.r[29] + 0x6c, now + 0xa); if ((uint32_t)c.r[18] == len) c.r[18] = now; }
}
void RecordSizes(const uint8_t* r, size_t& hdr, size_t& l) {
    hdr = 3 + (r[1] & 0x20 ? 1 : 0) + (r[1] & 0x10 ? 1 : 0);
    l = r[1] & 0x20 ? (size_t)(r[2] | r[3] << 8) : r[2];
}
// SIM_LADDER_RECORD (tests, an older program with a newer one): "recv:<frame>" on the older program puts a
// ladder's record of the newer protocol (off: index 8 and the flag, height 123) at the end of the first
// datagram of the host's player records received from that frame, as a newer host sends it; "send:<frame>"
// on the newer host puts it in one it sends (the receiver taken as older); "check" (and recv) on the
// older program: 60 frames after it received a 0F record, the climber's ladder component (0xEBB252, what
// the 0F handler JP 0x43BF70 writes: +0x90 x, +0x94 y, +0x98 z, each the record's x 10) logged.
// Two newer programs: "flag:<frame>" on the host sends ladder 0 with the flag (height 123) to a joiner
// whose protocol it does not know (the form both read), "flagcheck" on the joiner logs what its 0F
// handler (US n001a 0x44A6D0, n033a 0x438EE8) wrote: +0x65 the flag, +0x66 the index, +0x94 the height
// x 10, read from the next frame on until they hold the record (60 frames at most: a copy put on a
// ladder by a lone record leaves it soon after, and its component is reset, 60 frames later all three
// were back to 0, 8, 0)
#ifndef MGO1_PLAYER_BUILD
struct LadderTest { bool injected = false; int seenSlot = -1; long long seenAt = -1; bool checked = false; } gLadderTest;
#endif
void SimLadderInject(Ctx& c, bool out, uint32_t session, uint32_t buf, uint32_t& len) {
#ifndef MGO1_PLAYER_BUILD
    static const char* e = DevEnv("SIM_LADDER_RECORD");
    const bool flag = e && out && !strncmp(e, "flag:", 5);
    if (!e || gLadderTest.injected || (strncmp(e, out ? "send:" : "recv:", 5) && !flag) || gFrameNo < atoll(e + 5)) return;
    const int slot = out ? OwnSlot() : PeerSlot(session & 0x01ffffff);
    if (slot < 0 || slot > 10 || len <= 2) return;
    bool match = false;                                  // a datagram of that player's positions: a match
    ForPlayerRecords(gMem + (buf & 0x01ffffff) + 2, len - 2, [&](uint8_t*, uint8_t* d, size_t, int s, int kind) { if (s == slot && kind == 0 && d[0] == 0) match = true; });
    if (!match || !DatagramLenKnown(c, out, len) || len + 7 + 4 > DatagramCap(out)) return;
    const uint8_t rec[7] = {(uint8_t)(0x41 + 3 * slot), 0x00, 0x04, 0x0f, (uint8_t)(flag ? 0x80 : 0x88), 123, 0};
    memcpy(gMem + (buf & 0x01ffffff) + len, rec, sizeof rec);
    SetDatagramLen(c, out, len, len + 7);
    len += 7;
    if (out && !flag) { const int to = PeerSlot(session & 0x01ffffff); if (to >= 0 && to < 32) gSlotProto[to] = 0; }
    gLadderTest.injected = true;
    Log("SIM_LADDER_RECORD: a ladder's record of the %s programs (%s, height 123) put on player %d's channel, %s",
        ProtoPrograms(1), flag ? "ladder 0 and the flag" : "off: index 8 and the flag", slot + 1,
        flag ? "sent to a player whose protocol is not known" : out ? "sent to an older program" : "as received");
#endif
}
void SimLadderSeen(bool out, uint32_t buf, uint32_t len) {   // an older program's received 0F records (after the translation)
#ifndef MGO1_PLAYER_BUILD
    static const char* e = DevEnv("SIM_LADDER_RECORD");
    if (!e || out || gLadderTest.seenSlot >= 0 || len <= 2 || (OwnProto() != 0 && strcmp(e, "flagcheck"))) return;
    ForPlayerRecords(gMem + (buf & 0x01ffffff) + 2, len - 2, [&](uint8_t*, uint8_t* d, size_t l, int s, int kind) {
        if (kind == 1 && d[0] == 0x0f && gLadderTest.seenSlot < 0) {
            gLadderTest.seenSlot = s; gLadderTest.seenAt = gFrameNo;
            Log("SIM_LADDER_RECORD: player %d's ladder record received, %d bytes (index byte %02x)", s + 1, (int)l, d[1]);
        }
    });
#endif
}
// the component lookup of the loaded map's code (US n001a 0x30E988, n002a 0x30EE68, n033a 0x30EAE8; JP
// n001a 0x302908, n033a 0x302A68: each map page has its own addresses): a function called with a1 =
// 0xEBB252 ('lui a1, 0xeb' up to 4 instructions before 'ori a1, a1, 0xb252', the jal up to 3 after)
// that calls itself (it walks the component tree), found once a page
uint32_t LadderComponentLookup() {
    static std::string page; static uint32_t found = 0; static long long tried = -100000;
    if (page == gOverlayName && (found || gFrameNo < tried + 300)) return found;
    page = gOverlayName; found = 0; tried = gFrameNo;
    for (uint32_t a = 0x2da010; a + 16 <= 0x00800000 && !found; a += 4) {
        if (R32(a) != 0x34a5b252) continue;
        bool lui = false;
        for (uint32_t k = 1; k <= 4; ++k) lui |= R32(a - 4 * k) == 0x3c0500eb;
        for (uint32_t k = 1; lui && k <= 3 && !found; ++k) {
            if ((R32(a + 4 * k) >> 26) != 3) continue;
            const uint32_t t = (R32(a + 4 * k) & 0x3ffffff) << 2;
            if (t < 0x100000 || t >= 0x00800000 || (R32(t) >> 16) != 0x27bd) break;
            for (uint32_t i = 1; i < 40 && !found; ++i) if (R32(t + 4 * i) == (0x0c000000 | t >> 2)) found = t;
            break;
        }
    }
    if (found) Log("ladder: the %s page's component lookup at %06x", page.c_str(), found);
    return found;
}
// Tests of the rules pages (docs/EXTRA_MAPS.md): SIM_FORCE_TEAMS=a,b (the two teams' sides, 0 KGB, 1 GRU, 2 Ocelot
// Unit: the game settings' key 0x42, bytes 1 and 5) and SIM_FORCE_FLAGS=hex (key 0x30's bits 0x80 and 0x400, the
// variants of the team and sneaking pages) written into the settings at each pass: the maps' scripts' procedure 38
// picks the rules page from them (r_team_kg1_n: KGB and GRU with Sokolov and Raikov, _kg2_n with Reiko and Rowdy ...)
void SimForceSettings() {
    static const char* teams = getenv("SIM_FORCE_TEAMS");
    static const char* flags = getenv("SIM_FORCE_FLAGS");
    static const bool show = getenv("SIM_LOG_SETTINGS") != nullptr;   // tests: the teams and flags at each change
    if (!teams && !flags && !show) return;
    const uint32_t d = ConfigData0();
    if (d < 0x100000 || d + 0x50 >= 0x02000000) return;
    if (show) {
        static uint64_t last = ~0ull;
        const uint64_t k = ((uint64_t)R8(d + 0x43) << 40) | ((uint64_t)R8(d + 0x47) << 32) | R32(d + 0x30);
        if (k != last) { last = k; Log("SIM_LOG_SETTINGS: the settings' teams %u, %u and flags %08x", R8(d + 0x43), R8(d + 0x47), R32(d + 0x30)); }
    }
    if (!teams && !flags) return;
    if (teams) { W8(d + 0x43, (uint8_t)atoi(teams)); if (const char* b = strchr(teams, ',')) W8(d + 0x47, (uint8_t)atoi(b + 1)); }
    if (flags) W32(d + 0x30, (R32(d + 0x30) & ~0x480u) | ((uint32_t)strtoul(flags, nullptr, 16) & 0x480u));
    if (Once((uint64_t)73 << 40)) Log("SIM_FORCE_TEAMS %s, SIM_FORCE_FLAGS %s: the settings' teams %u, %u and flags %08x", teams ? teams : "-",
                                      flags ? flags : "-", R8(d + 0x43), R8(d + 0x47), R32(d + 0x30));
}
// SIM_KILL_AT=f,f,... (tests): this player's life set to 0 at the first pass from each of those frames he is alive
// (the respawns and their points seen: SIM_TRACE_FNS=161278)
void SimKill() {
    static std::vector<long long> at = [] {
        std::vector<long long> v;
        if (const char* e = getenv("SIM_KILL_AT")) for (const char* q = e; *q;) { v.push_back(atoll(q)); q = strchr(q, ','); if (!q) break; ++q; }
        return v;
    }();
    if (at.empty() || gFrameNo < at.front()) return;
    const int self = OwnSlot();
    const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
    if (!me || (int16_t)R16(me + 0xb64) <= 0 || (R32(me + 0xb48) & 0x1000000)) return;
    Log("SIM_KILL_AT: this player's life %d set to 0", (int16_t)R16(me + 0xb64));
    W16(me + 0xb64, 0);
    at.erase(at.begin());
}
void SimLadderCheck(Ctx& c) {                            // at a pass of the actor lists
#ifndef MGO1_PLAYER_BUILD
    static const bool flag = DevEnv("SIM_LADDER_RECORD") && !strcmp(DevEnv("SIM_LADDER_RECORD"), "flagcheck");
    if (gLadderTest.seenSlot < 0 || gLadderTest.checked || gFrameNo < gLadderTest.seenAt + (flag ? 1 : 60)) return;
    const bool last = gFrameNo >= gLadderTest.seenAt + 60;
    const uint32_t pl = PlayerOfSlot(gLadderTest.seenSlot), find = LadderComponentLookup();
    if (!pl || !find || (R32(find) >> 16) != 0x27bd) {
        if (!last) return;
        gLadderTest.checked = true;
        Log("SIM_LADDER_RECORD: no player %d or no component lookup", gLadderTest.seenSlot + 1);
        return;
    }
    Ctx x = c;
    x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
    x.r[31] = V(0x1005f0);
    x.r[4] = R32(pl + 0x44); x.r[5] = 0xEBB252;
    Call(x, find);
    const uint32_t comp = (uint32_t)x.r[2];
    if (!comp) {
        if (!last) return;
        gLadderTest.checked = true;
        Log("SIM_LADDER_RECORD: player %d has no ladder component", gLadderTest.seenSlot + 1);
        return;
    }
    if (flag) {
        const bool good = R8(comp + 0x65) == 1 && R8(comp + 0x66) == 0 && F32(comp + 0x94) == 1230.f;
        if (!good && !last) return;
        gLadderTest.checked = true;
        Log("SIM_LADDER_RECORD: player %d's ladder component: flag %u, index %u, height %.0f (the record's: 1, 0, 123), %lld frames after its receipt: %s",
            gLadderTest.seenSlot + 1, R8(comp + 0x65), R8(comp + 0x66), F32(comp + 0x94), gFrameNo - gLadderTest.seenAt, good ? "as sent" : "NOT as sent");
        return;
    }
    gLadderTest.checked = true;
    const float cx = F32(comp + 0x90), cy = F32(comp + 0x94), cz = F32(comp + 0x98), px = F32(pl + 0x110), pz = F32(pl + 0x118);
    const bool good = cy == 1230.f && fabsf(cx - px) <= 10.f && fabsf(cz - pz) <= 10.f;
    Log("SIM_LADDER_RECORD: player %d's ladder component: x %.0f, y %.0f, z %.0f (the player at x %.0f, z %.0f; the record's height 123): %s",
        gLadderTest.seenSlot + 1, cx, cy, cz, px, pz, good ? "as sent" : "NOT as sent");
#endif
}
// The speeds of a newer climber's copy on an older program. A newer program's copy of a climber moves
// at his flag's speeds: its ladder component's +0x3C, which each state function writes before the call
// that runs its motion (JP n033a 0x30C9C0) and the motion takes (US n033a: the climb 0x439950 two time
// steps, one with the flag; the exits 0x439700 at the top and 0x439820 at the bottom one step, a half
// with the flag; the time step kTimeStep). An older program's copy climbs at two steps whatever the flag
// (JP n033a 0x42B1F8 writes them every frame) and keeps them in its exits (0x42B018 and 0x42B100 write
// none). On n033a a North American climber's flag is on from y 2040 (he climbs at one step): his copy
// on a Japanese screen got ahead of him, then ran the top's exit in 129 frames where his took 200: it
// ended first, then either climbed back down facing away (his height below its own) or, off the ladder,
// was put on the ground under it (the ladder's foot: his positions come back at the end of his exit)
// and on the ladder again by his next records. An older PC gives its copies of newer climbers the newer
// speeds (LadderCopies lists them at each pass, LadderSpeed sets +0x3C at each call their player makes
// on a ladder: the motion's call comes after the state function's write), so a copy climbs and exits as
// a newer copy does. And (LadderHold):
// - during the copy's exit at the top, it gives it his height if above its own and his off as a record
//   at its height (the older handler takes an off while the copy is busy, +0x62, for a lookup at index
//   -1, 64 bytes before the table);
// - after the copy's exit, while he still sends his ladder, it makes his records off;
// - on every program, while his height does not fall, the copy's target is at least its own height
//   (LadderSpeed: the start of a climb).
// 15 s at most, a respawn or his off (outside the copy's exit) ends it. A newer PC's records held under
// the top's exit for older PS2s (LadderRecordsGrow) carry his true height at +2: an older PC reads that.
struct LadderSpeedCopy { uint32_t player, comp; bool speeds, flag, up; int slot; float his; };   // his: his last height
LadderSpeedCopy gLadderSpeed[8];
int gLadderSpeedN = 0;
uint32_t gActActor = 0;                                   // the actor whose act runs (the actor loop's calls: DispatchBridge)
float gOwnLadderTop = 0.f;                                // a newer program: its own climber's ladder, its last height
bool MapPage() { return gOverlayName.size() == 5 && gOverlayName[0] == 'n' && gOverlayName[4] == 'a'; }   // n001a .. n033a
// at a call (DispatchBridge) of a copy on a ladder during its act (its state functions', the time step
// then its own: 10 for the players' copies, a 30 Hz actor):
// - a newer climber's copy on an older program: his speeds, before its motion reads them;
// - any copy, while his height does not fall and the copy is in no exit: its target (+0x94, the height
//   the decision compares its y with, 200 either way: US n033a 0x439E08, JP 0x42B660) at least its own
//   height. A copy's first climb after getting on runs a whole motion (+0x3C -1: 450 units on n033a)
//   whatever his height: when his records stay at the first rung a while (late datagrams; 35 frames in
//   a test) his copy gets ahead of him, climbs down to him, and within 1700 of the foot (the exit
//   check's first zone, US n001a 0x44AAC8) gets off there, then on the ladder again with his next
//   records, behind him (seen with a North American copy of a North American climber: the game's
//   own). The copy now waits for him.
void LadderSpeed(Ctx& c) {
    const uint32_t a0 = (uint32_t)c.r[4];
    for (int k = 0; k < gLadderSpeedN; ++k) {
        const LadderSpeedCopy& s = gLadderSpeed[k];
        if (a0 != s.player) continue;
        if (gActActor != s.player || !R32(s.comp + 0x48)) return;
        const bool exit = R8(s.comp + 0x62) != 0;
        if (s.speeds) {
            const float step = (float)(int32_t)R32(kTimeStep), now = F32(s.comp + 0x3c);
            float want = now;
            if (step > 0.f && exit) want = s.flag ? step * 0.5f : step;       // an exit (the top, the bottom)
            else if (step > 0.f && s.flag && now == 2.f * step) want = step;  // the climb with his flag
            if (want != now) {
                uint32_t u; memcpy(&u, &want, 4); W32(s.comp + 0x3c, u);
                if (Once(((uint64_t)45 << 40) | (uint64_t)exit << 8 | (s.flag ? 1 : 0)))
                    Log("ladder: a newer climber's copy, its %s at his speed %.1f (its own %.1f, the time step %.0f)", exit ? "exit" : "climb", want, now, step);
            }
        }
        const float y = F32(s.player + 0x114), target = F32(s.comp + 0x94);
        if (s.up && !exit && !R8(s.comp + 0x60) && target < y) {
            uint32_t u; memcpy(&u, &y, 4); W32(s.comp + 0x94, u);
            if (s.his < y - 200.f && Once(((uint64_t)46 << 40) | (uint32_t)s.slot))
                Log("ladder: player %d's copy waits for him on the ladder (at %.0f, his height %.0f, not falling)", s.slot + 1, y, s.his);
        }
        return;
    }
}
void LadderCopies(Ctx& c) {                               // at a pass of the actor lists: the players' ladder components
    gLadderSpeedN = 0;
    if (!MapPage() || BridgeOff()) return;
    const int own = OwnProto();
    if (own != 0 && own != 1) return;
    const uint32_t find = LadderComponentLookup();
    if (!find) return;
    auto lookup = [&](uint32_t player) {                  // looked up each pass: a round's restart makes the players again
        Ctx x = c;
        x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
        x.r[31] = V(0x1005f0);
        x.r[4] = R32(player + 0x44); x.r[5] = 0xEBB252;
        Call(x, find);
        return (uint32_t)x.r[2];
    };
    for (const auto& a : gNativeActors) {
        if (own == 1 && a.second == "NewNetworkDummyPlayer") {   // a newer program's own climber: his ladder's last height
            const uint32_t comp = lookup(a.first), ladder = comp ? R32(comp + 0x48) : 0;
            if (ladder >= 0x100000 && ladder < 0x02000000) {
                const int n = (int16_t)R16(ladder + 0x22);
                if (n >= 1 && n <= 16) gOwnLadderTop = F32(ladder + 0x24 + 4 * (n - 1));
            }
            continue;
        }
        if (a.second != "NewNetworkPlayer") continue;
        const int slot = R8(a.first + 0xb60);
        if (slot >= 32) continue;
        LadderCopy& lc = gLadderCopy[slot];
        const uint32_t comp = lookup(a.first);
        if (lc.player != a.first || lc.comp != comp) { lc.player = a.first; lc.comp = comp; lc.state = 0; lc.on = lc.up = false; }
        if (!lc.comp) continue;
        const uint32_t ladder = R32(lc.comp + 0x48);
        const bool speeds = own == 0 && (lc.newer || lc.flag || gSlotProto[slot] >= 1);   // an older program, a newer climber
        if (ladder && gLadderSpeedN < 8) {
            gLadderSpeed[gLadderSpeedN++] = {a.first, lc.comp, speeds, lc.flag, lc.on && lc.up, slot, lc.lastH * 10.f};
            if (speeds && Once(((uint64_t)44 << 40) | (uint64_t)slot << 8 | (lc.flag ? 1 : 0)))
                Log("ladder: player %d's copy on a ladder at the %s programs' speeds (%s)", slot + 1, ProtoPrograms(1),
                    lc.flag ? "his flag: the climb at one time step, the exits at a half" : "the climb at two time steps, the exits at one");
        }
        if (!speeds) continue;
        if (lc.state && gFrameNo > lc.since + 900) lc.state = 0;
        if (lc.state < 2 && ladder && R8(lc.comp + 0x60) == 2) {
            lc.state = 2; lc.since = gFrameNo;
            Log("ladder: player %d's copy starts its exit at the top (at %.0f, his height %.0f)", slot + 1, F32(a.first + 0x114), (float)lc.lastH * 10.f);
        } else if (lc.state == 2 && !ladder) {
            lc.state = 3;
            Log("ladder: player %d's copy ends its exit at the top (%lld frames)", slot + 1, gFrameNo - lc.since);
        }
    }
}
// on receipt (after LadderRecordsGrow): a climber's records for his copy. His height's direction (both
// kinds of program: LadderSpeed); an older program's copy of a newer climber in its exit at the top
void LadderHold(uint32_t buf, uint32_t len) {
    if (len <= 2 || BridgeOff()) return;
    const bool older = OwnProto() == 0;
    ForPlayerRecords(gMem + (buf & 0x01ffffff) + 2, len - 2, [&](uint8_t*, uint8_t* d, size_t l, int slot, int kind) {
        if (kind != 1 || d[0] != 0x0f || slot < 0 || slot >= 32 || (l != 8 && (older || l != 4))) return;
        LadderCopy& lc = gLadderCopy[slot];
        if (!lc.player || !lc.comp) return;
        auto setY = [&](int16_t v) { d[4] = (uint8_t)v; d[5] = (uint8_t)((uint16_t)v >> 8); };
        const int16_t own = (int16_t)lrintf(F32(lc.player + 0x114) / 10.f);
        const int16_t h = older ? (int16_t)(d[4] | d[5] << 8) : (int16_t)(d[2] | d[3] << 8);   // the height each handler reads
        if (older ? (int8_t)d[1] < 0 : (d[1] & 0x7f) >= 8) {                  // his off
            lc.on = false;
            if (older && lc.state == 2 && R32(lc.comp + 0x48)) { d[1] = lc.idx; setY(std::max(h, own)); }   // the copy busy in its exit
            else lc.state = 0;
            return;
        }
        lc.up = !lc.on || h >= lc.lastH;                                     // his height not falling (LadderSpeed)
        lc.on = true; lc.lastH = h; lc.idx = d[1];
        if (!older) return;
        if (lc.state == 3) { d[1] = 0xff; return; }                          // the copy's exit done: off
        if (lc.state == 2 && h < own) setY(own);
    });
}
// SIM_LADDER_WATCH=n: every n frames, each player's ladder component (0xEBB252): its state function
// (+0x30), state (+0x40, +0x44), ladder (+0x48), bytes +0x60..+0x66, speed +0x3C, +0x90..+0x98; the player's y
// (+0x114) and turns (+0x142 now, +0x14A wanted), logged when they change
void SimLadderWatch(Ctx& c) {
#ifndef MGO1_PLAYER_BUILD
    static const int every = DevEnv("SIM_LADDER_WATCH") ? atoi(DevEnv("SIM_LADDER_WATCH")) : 0;
    if (every <= 0 || gFrameNo % every || !MapPage()) return;
    const uint32_t find = LadderComponentLookup();
    if (!find) return;
    static std::map<uint32_t, std::string> last;
    for (const auto& a : gNativeActors) {
        if (a.second != "NewNetworkPlayer" && a.second != "NewNetworkDummyPlayer") continue;
        const uint32_t pl = a.first;
        Ctx x = c;
        x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
        x.r[31] = V(0x1005f0);
        x.r[4] = R32(pl + 0x44); x.r[5] = 0xEBB252;
        Call(x, find);
        const uint32_t comp = (uint32_t)x.r[2];
        if (!comp) continue;
        char b[320];
        snprintf(b, sizeof b, "fn %06x st %x/%x ladder %06x b60 %02x %02x %02x %02x %02x %02x %02x sp %.1f c %.0f %.0f %.0f y %.0f turn %04x want %04x",
                 R32(comp + 0x30) & 0xffffff, R32(comp + 0x40), R16(comp + 0x44), R32(comp + 0x48) & 0xffffff,
                 R8(comp + 0x60), R8(comp + 0x61), R8(comp + 0x62), R8(comp + 0x63), R8(comp + 0x64), R8(comp + 0x65), R8(comp + 0x66),
                 F32(comp + 0x3c), F32(comp + 0x90), F32(comp + 0x94), F32(comp + 0x98), F32(pl + 0x114), R16(pl + 0x142), R16(pl + 0x14a));
        std::string& l = last[pl];
        if (l != b) { Log("ladder watch: %s slot %u: %s", a.second == "NewNetworkPlayer" ? "remote" : "local", R8(pl + 0xb60), b); l = b; }
    }
#endif
}
// A ladder's record of 4 bytes (newer) made 8 in the form both read (LadderUniversal, the flag in z):
// received by an older program, or sent to an older one or to a player whose protocol is not known yet;
// to a newer one it stays. The datagram grows by 4 bytes a record (DatagramLenKnown). Returns the length.
uint32_t LadderRecordsGrow(Ctx& c, bool out, uint32_t session, uint32_t buf, uint32_t len) {
    const bool off = BridgeOff();
    const uint32_t b = buf & 0x01ffffff;
    if (off || len <= 2 || b + len > 0x02000000) return len;
    const int own = OwnProto();
    int theirs = 0;                                      // the receiver's protocol (0 older, -1 not known yet)
    if (out) {
        const int to = PeerSlot(session & 0x01ffffff);
        theirs = to >= 0 && to < 32 ? (gSlotProto[to] == 2 ? 1 : gSlotProto[to]) : -1;
        if (own != 1 || theirs == 1) return len;
    } else if (own != 0) return len;
    uint8_t* p = gMem + b + 2;
    const size_t n = len - 2;
    auto isNewer = [](const uint8_t* r, size_t hdr, size_t l) {
        const uint32_t ch = r[0] | (r[1] & 7) << 8;
        return !(r[1] & 0x20) && l == 4 && r[hdr] == 0x0f && ch >= 0x40 && ch < PlayersChanEnd() && (ch - 0x40) % 3 == 1;
    };
    int count = 0;
    const bool framed = ForRecords(p, n, [&](uint8_t* r) {
        size_t hdr, l;
        RecordSizes(r, hdr, l);
        if (isNewer(r, hdr, l)) ++count;
    });
    if (!framed || !count) return len;
    const uint32_t grown = len + 4 * (uint32_t)count;
    if (!DatagramLenKnown(c, out, len) || grown > DatagramCap(out)) {
        if (Once(((uint64_t)41 << 40) | (out ? 1 : 0)))
            Log("session: a ladder's record of the %s programs left as it is for this one (%s)", ProtoPrograms(1),
                grown > DatagramCap(out) ? "the datagram is full" : out ? "the send function's frame is not the one known" : "the receive function's frame is not the one known");
        return len;
    }
    std::vector<uint8_t> o;
    o.reserve(grown);
    for (size_t i = 0; i < n;) {
        const uint8_t* r = p + i;
        size_t hdr, l;
        RecordSizes(r, hdr, l);
        if (!isNewer(r, hdr, l)) { o.insert(o.end(), r, r + hdr + l); i += hdr + l; continue; }
        const uint8_t* d = r + hdr;
        const int slot = (int)((r[0] | (r[1] & 7) << 8) - 0x40) / 3;
        // the form both read, to an older program too (its ladder code reads y alone: JP n001a 0x43CD34,
        // 0x43CE6C; an older host relays the bytes as they came, which a newer program then reads right):
        // the flag in z
        const uint16_t x = (uint16_t)(d[2] | d[3] << 8);
        uint16_t y = x, z = (uint16_t)(0x8004 | d[1] >> 7);           // bit 2: a newer program wrote it
        if (out && (d[1] & 0x80) && (d[1] & 0x7f) < 8 && slot == OwnSlot() && gOwnLadderTop > 0.f) {
            // this climber's exit runs at half a time step (the flag): an older PS2's copy runs its own
            // faster (its climb's speed kept) and would end it first: for an older PS2 the older y (+4)
            // held 250 under the exit during the exit's first 1500, the newer height (+2) kept, bit 1 of
            // z marks it (an older PC reads +2 and gives its copy his speeds instead: LadderSpeed)
            const float exitAt = gOwnLadderTop - 100.f, h = (int16_t)x * 10.f;
            if (h >= exitAt - 250.f && h < exitAt + 1500.f) { y = (uint16_t)(int16_t)lrintf((exitAt - 250.f) / 10.f); z |= 2; }
        }
        const size_t at = o.size();
        o.insert(o.end(), r, r + hdr);
        o[at + 2] = 8;
        const uint8_t idx = (d[1] & 0x7f) >= 8 ? 0xff : (uint8_t)(d[1] & 0x7f);
        if (!out && slot >= 0 && slot < 32) {
            if (idx != 0xff) gLadderCopy[slot].flag = (d[1] & 0x80) != 0;
            gLadderCopy[slot].newer = true;
        }
        const uint8_t rec[8] = {0x0f, idx, (uint8_t)x, (uint8_t)(x >> 8), (uint8_t)y, (uint8_t)(y >> 8), (uint8_t)z, (uint8_t)(z >> 8)};
        o.insert(o.end(), rec, rec + 8);
        if (Once(((uint64_t)42 << 40) | (uint64_t)(out ? 1 : 0) << 8 | (uint64_t)(theirs & 1) << 9 | (uint32_t)slot))
            Log("session: player %d's ladder record (index %d, height %d%s) %s in the form both protocols read (8 bytes)%s", slot + 1,
                (int8_t)idx, (int16_t)(d[2] | d[3] << 8), d[1] & 0x80 ? ", the flag" : "", out ? "sent" : "received",
                out ? (theirs == 0 ? " to an older program" : " to a player whose protocol is not known yet") : "");
        i += hdr + l;
    }
    memcpy(p, o.data(), o.size());
    const uint32_t now = (uint32_t)o.size() + 2;
    SetDatagramLen(c, out, len, now);
    return now;
}
// the IOP's sound driver reads its streams at cdbios positions (file << 24 | sector)
struct IopReads { long long n = 0, bytes = 0, waitUs = 0, readUs = 0, worstUs = 0; };   // the sound line's (perf)
IopReads gIopReads;
std::mutex gIopReadsMu;
bool IopCdRead(uint32_t pos, uint8_t* dst, uint32_t size) {
    const long long t0 = QpcNow();
    std::lock_guard<std::mutex> cl(gCdLock);
    const long long t1 = QpcNow();
    const uint32_t id = pos >> 24, sector = pos & 0xffffff;
    if (id == 0 || id >= gCd.size() || !gCd[id].f) return false;
    const long long at = (long long)sector * 2048;
    if (at >= gCd[id].size) return false;
    CdFileRead(gCd[id], at, dst, size);
    static const int slow = getenv("SIM_IOP_READ_DELAY") ? atoi(getenv("SIM_IOP_READ_DELAY")) : 0;   // tests: a slow disc (ms a read)
    if (slow > 0) Sleep(slow);
    const long long t2 = QpcNow(), us = (t2 - t0) * 1000000 / QpcFreq();
    std::lock_guard<std::mutex> g(gIopReadsMu);
    ++gIopReads.n; gIopReads.bytes += size; gIopReads.waitUs += (t1 - t0) * 1000000 / QpcFreq(); gIopReads.readUs += (t2 - t1) * 1000000 / QpcFreq();
    gIopReads.worstUs = std::max(gIopReads.worstUs, us);
    return true;
}
std::string IopReadStats() {                        // since the last call
    std::lock_guard<std::mutex> g(gIopReadsMu);
    char b[200];
    snprintf(b, sizeof b, "the IOP's disc reads: %lld (%lld KB), %.1f ms waiting for the disc's lock, %.1f ms reading, the longest %.1f ms",
             gIopReads.n, gIopReads.bytes >> 10, gIopReads.waitUs / 1000.0, gIopReads.readUs / 1000.0, gIopReads.worstUs / 1000.0);
    gIopReads = IopReads();
    return b;
}
bool Cdbios(Ctx& c, uint32_t addr) {
    // Konami's SIF channel 1 is the sound driver's (the IOP's own when it runs here)
    if ((addr == 0x17dce8 || addr == 0x17ddc8) && (uint32_t)c.r[4] == 1 && iop::Running()) {
        const uint32_t args = (uint32_t)c.r[6] & 0x01ffffff, size = std::min<uint32_t>((uint32_t)c.r[7], 0x100);
        if (args + size <= 0x02000000) iop::EeCommand(1, (uint32_t)c.r[5], gMem + args, size);
        c.r[2] = 1;
        return true;
    }
    if (addr == 0x17d6e8) {                            // cdbios_init: the status block, then INIT
        gCdS = V(0x28DB80);
        memset(gMem + gCdS, 0, 0x100);
        W32(V(0x28DB40), gCdS | 0x20000000);
        Log("cdbios: init, status block %08x", gCdS);
        return true;
    }
    if (addr != 0x17dce8 && addr != 0x17ddc8) return false;
    const uint32_t cmd = (uint32_t)c.r[5], args = (uint32_t)c.r[6] & 0x01ffffff, size = (uint32_t)c.r[7];
    if (!gCdS) { gCdS = V(0x28DB80); W32(V(0x28DB40), gCdS | 0x20000000); }
    auto arg = [&](int k) { return R32(args + 4 * k); };
    switch (cmd) {
    case 1: {                                          // read {pos, buffer, 0, intr_size, total_size, channel, flag}
        const uint32_t ch = arg(5) & 3;
        const uint32_t rec = gCdS + 0x20 + 0x20 * ch;
        W32(rec, arg(0)); W32(rec + 4, arg(1)); W32(rec + 8, 0); W32(rec + 0xC, arg(3)); W32(rec + 0x10, arg(4));
        W32(gCdS, 0);
        if (!(arg(0) >> 24)) {                         // no archive: who asked (the last calls before)
            std::set<std::pair<uint32_t, uint32_t>> seen; std::string s;
            for (unsigned k = 0; k < 64; ++k) {
                auto& e = gRing[(gRingPos - 64 + k) & 63];
                if (seen.insert({e[0], e[1]}).second) { char b[32]; snprintf(b, sizeof(b), " %08x<%08x", e[0], e[1] - 8); s += b; }
            }
            Log("cdbios: read at position %08x (channel %u, from %08x); recent calls:%s", arg(0), ch, (uint32_t)c.r[31] - 8, s.c_str());
        }
        static const bool voxLog = DevEnv("SIM_VOX_LOG") != nullptr;   // the voice archive's reads and who asked
        if (voxLog && (arg(0) >> 24) < gCd.size() && gCd[arg(0) >> 24].name == "VOX_2.DAT")
            Log("cdbios: VOX_2 read at sector %#x, %#x bytes (chunks %#x) to %08x, channel %u, from %08x; recent calls:%s", arg(0) & 0xffffff, arg(4), arg(3), arg(1), ch,
                (uint32_t)c.r[31] - 8, RecentCalls(24).c_str());
        CdRead(ch);
        break; }
    case 6: {                                          // search file: its position
        std::string path;
        for (uint32_t k = 0; k < 256 && k < size; ++k) { char ch = (char)R8(args + k); if (!ch) break; path += ch; }
        int id = CdOpen(path);
        const int n = id < 0 ? -id : id;
        W32(gCdS + 0x10, (uint32_t)n << 24);
        Log("cdbios: search file \"%s\" -> file %d %s", path.c_str(), n,
            id > 0 ? (gCd[n].name + " (" + std::to_string(gCd[n].size) + " bytes)").c_str() : "(not in the image folder: reads refused)");
        break; }
    case 5: {                                          // get time: sceCdCLOCK, BCD
        SYSTEMTIME t; GetLocalTime(&t);
        auto bcd = [](int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); };
        uint8_t clk[8] = {0, bcd(t.wSecond), bcd(t.wMinute), bcd(t.wHour), 0, bcd(t.wDay), bcd(t.wMonth), bcd(t.wYear % 100)};
        memcpy(gMem + gCdS + 0x10, clk, 8);
        break; }
    case 2: case 3: case 4: case 7: case 8: case 11:
        break;
    default:
        Log("cdbios: command %u (%u bytes) not known, answered", cmd, size);
        break;
    }
    if (cmd != 1) W32(gCdS, 0);
    c.r[2] = 1;
    return true;
}

// The console's system settings (OSD), as a PS2 set up like this PC: English, 4:3, the PC's time
// zone (minutes east of UTC, standard time), summer time and the date / time format of the PC's
// locale. libscf reads them (0x1E62F8 time zone, 0x1E6348 date format, 0x1E63B0 summer time):
// unserved, they came from whatever the stack held.
uint32_t OsdConfig() {
    TIME_ZONE_INFORMATION tz{};
    GetTimeZoneInformation(&tz);
    int offset = -(int)tz.Bias;
    offset = offset < -720 ? -720 : offset > 780 ? 780 : offset;
    static const char* env = DevEnv("SIM_OSD_LANG");
    const int lang = env ? atoi(env) : P.systemLanguage >= 0 ? P.systemLanguage : 1;
    return 0u                                          // spdif on, 4:3, RGB
         | (lang == 0 ? 0u : 1u) << 4                  // japLanguage: 0 Japanese
         | 2u << 13                                    // version
         | (uint32_t)(lang & 0x1f) << 16               // language (1 English)
         | ((uint32_t)offset & 0x7ff) << 21;           // time zone offset (11 bits, signed)
}
uint8_t OsdConfig2(uint32_t offset) {
    if (offset != 1) return 0;
    TIME_ZONE_INFORMATION tz{};
    const bool dst = GetTimeZoneInformation(&tz) == TIME_ZONE_ID_DAYLIGHT;
    char b[8] = {};
    int idate = GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_IDATE, b, sizeof b) ? atoi(b) : 1;
    int itime = GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_ITIME, b, sizeof b) ? atoi(b) : 1;
    const uint32_t date = idate == 0 ? 1 : idate == 2 ? 0 : 2;          // PS2: 0 Y/M/D, 1 M/D/Y, 2 D/M/Y
    const uint32_t time12 = itime == 0 ? 1 : 0;                          // PS2: 0 = 24 hours
    return (uint8_t)(date << 6 | time12 << 5 | (dst ? 1u : 0u) << 4);
}
bool Kernel(Ctx& c, uint32_t addr) {
    uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5];
    auto ret = [&](int64_t v) { c.r[2] = (uint64_t)v; return true; };
    switch (addr) {
    case 0x1f54a0: {                                   // CreateThread(ee_thread_t*)
        EeThread t;
        t.id = (int)gThreads.size() + 1;
        t.entry = R32(a0 + 4); t.stack = R32(a0 + 8); t.size = R32(a0 + 0xc); t.gp = R32(a0 + 0x10); t.prio = (int)R32(a0 + 0x14);
        t.state = 0x10;
        t.go = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!gMainGp && t.gp) gMainGp = t.gp;
        gThreads.push_back(t);
        Log("CreateThread %d entry %08x stack %08x size %#x gp %08x prio %d", t.id, t.entry, t.stack, t.size, t.gp, t.prio);
        return ret(t.id); }
    case 0x1f54c0: {                                   // StartThread(id, arg)
        EeThread* t = Thr((int)a0);
        if (!t) return ret(-1);
        t->arg = a1; t->state = 2; t->seq = ++gSeq;
        const int id = t->id;
        t->os = CreateThread(nullptr, 1 << 20, ThreadBody, (LPVOID)(intptr_t)t->id, 0, nullptr);
        if (id > 0 && id < 256) gOsOf[id] = t->os;
        Log("StartThread %d arg %08x", id, a1);
        NativePreempt();
        return ret(id); }                              // the kernel answers the thread's id
    case 0x100268:                                     // ExitThread()
        { EeThread* t = Thr(gCur); if (t && (t->id != 1 || gNative)) { Log("ExitThread %d", t->id); t->state = 5; BackSet(); ExitThread(0); } }
        return ret(0);
    case 0x1f54b0: case 0x1f54f0: {                   // DeleteThread / TerminateThread
        EeThread* t = Thr((int)a0);
        if (!t || t->deleted) return ret(-1);
        if (t->id != gCur) {
            t->state = 5;
            if (addr == 0x1f54b0) t->deleted = true;   // the id no longer names a thread
            Log("%s %d", addr == 0x1f54b0 ? "DeleteThread" : "TerminateThread", t->id);
        }
        return ret(t->id); }
    case 0x1f54d0: case 0x1f54e0: {                   // ExitThread / ExitDeleteThread (the running PS2 thread ends)
        EeThread* t = Thr(gCur);
        if (t && (t->id != 1 || gNative)) {
            Log("%s %d", addr == 0x1f54e0 ? "ExitDeleteThread" : "ExitThread", t->id);
            t->state = 5; if (addr == 0x1f54e0) t->deleted = true;
            BackSet(); ExitThread(0);
        }
        return ret(0); }
    case 0x1f5530: {                                   // ChangeThreadPriority
        EeThread* t = Thr((int)a0 ? (int)a0 : gCur);
        const int prev = t ? t->prio : -1;
        if (t) t->prio = (int)a1;
        NativePreempt();
        return ret(gNative ? prev : (t ? t->id : -1)); }   // the kernel answers the old priority (native)
    case 0x1f5590: return ret(gCur);                   // GetThreadId
    case 0x1f55a0: {                                   // ReferThreadStatus(id, status*)
        EeThread* t = Thr((int)a0 ? (int)a0 : gCur);
        if (t && t->deleted) {                         // a deleted thread: its control block is free, status 0
            if (a1) for (int k = 0; k < 7; ++k) W32(a1 + 4 * k, 0);   // (mrdUPnP_StopThread waits for status 0)
            return ret(-1);
        }
        if (!t) return ret(-1);
        int st = t->state == 1 ? 1 : t->state == 2 ? 2 : t->state == 4 ? 4 : 0x10;
        if (a1) { W32(a1, st); W32(a1 + 4, t->entry); W32(a1 + 8, t->stack); W32(a1 + 0xc, t->size); W32(a1 + 0x10, t->gp);
                  W32(a1 + 0x14, t->prio); W32(a1 + 0x18, t->prio); }
        return ret(t->id); }
    case 0x1f55c0: {                                   // SleepThread()
        EeThread* t = Thr(gCur);
        if (t && t->wakeups > 0) { --t->wakeups; return ret(gCur); }
        Block(4);
        return ret(gCur); }
    case 0x1f55d0: case 0x1f55e0:                     // WakeupThread(id), iWakeupThread
    case 0x1f6568: {                                   // the base's wakeup of any thread, itself too (the cdbios read callback)
        EeThread* t = Thr((int)a0);
        if (t && t->state == 4 && t->waitSema < 0) { if (gNative) NativeReady(t); else t->state = 2; }
        else if (t) ++t->wakeups;
        NativePreempt();
        return ret(a0); }
    case 0x1f5680: return ret(gNative ? gHeapEnd : 0x01E00000);   // EndOfHeap: native, SetupHeap's; else newlib's heap is 0x01B00000-0x01E00000
    case 0x1f56a0: {                                   // CreateSema(ee_sema_t*)
        EeSema s; s.count = (int)R32(a0 + 8); s.max = (int)R32(a0 + 4); s.init = s.count;
        s.attr = R32(a0 + 16); s.option = R32(a0 + 20);
        gSemas.push_back(s);
        Log("CreateSema %d init %d max %d", (int)gSemas.size(), s.count, s.max);
        return ret((int)gSemas.size()); }
    case 0x1f56b0: if (a0 >= 1 && a0 <= gSemas.size()) gSemas[a0 - 1].live = false; return ret(a0);   // DeleteSema
    case 0x1f56c0: case 0x1f56d0: {                    // SignalSema(id), iSignalSema
        if (a0 < 1 || a0 > gSemas.size()) return ret(-1);
        if (gNative) {                                 // the first thread to wait is woken
            EeThread* w = nullptr;
            for (auto& t : gThreads) if (t.state == 4 && t.waitSema == (int)a0 && (!w || t.seq < w->seq)) w = &t;
            if (w) { w->waitSema = -1; NativeReady(w); NativePreempt(); return ret(a0); }
        } else
            for (auto& t : gThreads) if (t.state == 4 && t.waitSema == (int)a0) { t.waitSema = -1; t.state = 2; return ret(a0); }
        ++gSemas[a0 - 1].count;
        return ret(a0); }
    case 0x1f56e0: {                                   // WaitSema(id)
        if (a0 < 1 || a0 > gSemas.size()) return ret(-1);
        if (gSemas[a0 - 1].count <= 0) ServeForSema(a0, true);
        EeSema& s = gSemas[a0 - 1];
        if (s.count > 0) { --s.count; return ret(a0); }
        EeThread* t = Thr(gCur);
        if (t) { t->waitSema = (int)a0; t->seq = ++gSeq; }
        Block(4);
        if (t && t->id == 1 && t->waitSema == (int)a0) {        // the game thread: take it if it came
            t->waitSema = -1;
            if (s.count > 0) --s.count;
        }
        return ret(a0); }
    case 0x1f56f0: case 0x1f5700: {                    // PollSema(id), iPollSema
        if (a0 < 1 || a0 > gSemas.size()) return ret(-1);
        if (gSemas[a0 - 1].count <= 0) ServeForSema(a0, false);
        EeSema& s = gSemas[a0 - 1];
        if (s.count > 0) { --s.count; return ret(a0); }
        return ret(-1); }
    case 0x1f5710: case 0x1f5720: {                    // ReferSemaStatus(id, ee_sema_t*), iReferSemaStatus
        if (a0 < 1 || a0 > gSemas.size() || !gSemas[a0 - 1].live) return ret(-1);
        const EeSema& s = gSemas[a0 - 1];
        int waiting = 0;
        for (auto& t : gThreads) if (t.state == 4 && t.waitSema == (int)a0) ++waiting;
        if (a1 && a1 + 24 <= 0x02000000) {
            W32(a1 + 0, (uint32_t)s.count); W32(a1 + 4, (uint32_t)s.max); W32(a1 + 8, (uint32_t)s.init);
            W32(a1 + 12, (uint32_t)waiting); W32(a1 + 16, s.attr); W32(a1 + 20, s.option);
        }
        return ret(a0); }
    case 0x1f5ad0: return ret(0);                      // Deci2Call: no debugger attached
    case 0x1f52a0:                                     // SetGsCrt(interlace, mode, field/frame)
        GsSetCrt((int)a0, (int)a1, (int)c.r[6]);
        Log("SetGsCrt(interlace %d, mode %#x, ffmd %d)", (int)a0, a1, (int)c.r[6]);
        if ((a1 == 3) != gPal) {                       // PAL: the fields and the IOP's vblanks at 50 a second
            gPal = a1 == 3;
            iop::SetPalFields(gPal);
            Log("display: %s, %s fields a second", gPal ? "PAL" : "NTSC", gPal ? "50" : "59.94");
        }
        return ret(0);
    case 0x1f5750: W32(a0, OsdConfig()); return ret(0);   // GetOsdConfigParam(ConfigParam*)
    case 0x1f59b0: {                                   // GetOsdConfigParam2(buf, size, offset)
        for (uint32_t k = 0; k < a1 && k < 64; ++k) W8(a0 + k, OsdConfig2((uint32_t)c.r[6] + k));
        return ret(0); }
    case 0x1f59c0: return ret((uint32_t)GsPrivRead(0x12001010, 8));   // GsGetIMR
    case 0x1f59e0: {                                   // GsPutIMR(imr): the old mask back
        const uint32_t was = (uint32_t)GsPrivRead(0x12001010, 8);
        GsPrivWrite(0x12001010, a0, 8);
        return ret(was); }
    case 0x1f5380: case 0x1f5390: {                    // AddIntcHandler(cause, handler, next) (and its twin stub)
        gIntc.push_back({(int)a0, a1, true, addr == 0x1f5390 ? (uint32_t)c.r[7] : 0});
        Log("AddIntcHandler(cause %d, %08x) -> %d", (int)a0, a1, (int)gIntc.size());
        return ret((int)gIntc.size()); }
    case 0x1f53b0: case 0x1f53c0: {                    // AddDmacHandler(channel, handler, next), AddDmacHandler2 (+ arg)
        gDmac.push_back({(int)a0, a1, true, addr == 0x1f53c0 ? (uint32_t)c.r[7] : 0});
        Log("AddDmacHandler(channel %d, %08x) -> %d", (int)a0, a1, (int)gDmac.size());
        return ret((int)gDmac.size()); }
    case 0x1f53d0: {                                   // RemoveDmacHandler(channel, id)
        if (a1 >= 1 && a1 <= gDmac.size()) gDmac[a1 - 1].live = false;
        return ret(0); }
    case 0x1f53a0: {                                   // RemoveIntcHandler(cause, id)
        if (a1 >= 1 && a1 <= gIntc.size()) gIntc[a1 - 1].live = false;
        return ret(0); }
    case 0x1f5400: case 0x1f5410: {                    // _EnableDmac(channel), _DisableDmac: D_STAT CIM, the old state back
        const uint32_t bit = 0x10000u << (a0 & 15);
        const bool was = ((uint32_t)HwR(0x1000E010, 4) & bit) != 0;
        if (was != (addr == 0x1f5400)) HwW(0x1000E010, bit, 4);   // writing 1 toggles CIM
        return ret(was ? 1 : 0); }
    case 0x1f53f0: {                                   // _DisableIntc(cause)
        const bool was = (gIntcMask >> (a0 & 31)) & 1;
        gIntcMask &= ~(1u << (a0 & 31));
        return ret(was ? 1 : 0); }
    case 0x1f53e0:                                     // EnableIntc(cause): VBlank causes run each frame
        gIntcMask |= 1u << (a0 & 31);
        return ret(1);
    case 0x1ff900: {                                   // the EE timer clock (T2 count + overflows) in BUSCLK ticks
        static long long t0 = NetQpc();                 // the network's clock (NetScaleSet)
        const uint64_t d = (uint64_t)(NetQpc() - t0), f0 = (uint64_t)QpcFreq();
        c.r[2] = d / f0 * 147456000ull + d % f0 * 147456000ull / f0;
        return true; }
    case 0x17d590:                                     // the base's delay in HSYNC lines (SetAlarm, then sleep)
    case 0x1bf418:                                     // the base stack's nanosleep({sec, nsec} 64-bit, rem)
    case 0x1f6718: {                                   // DelayThread(usec): the time passes for real
        if (gNative && addr == 0x17d590) return false; // native: the game's own (alarms are served)
        EeThread* t = Thr(gCur);
        uint32_t us = addr == 0x17d590 ? (uint32_t)((uint64_t)a0 * 63556 / 1000)    // one NTSC line: 63.556 us
                    : addr == 0x1bf418 ? (uint32_t)(R64(a0) * 1000000 + R64(a0 + 8) / 1000) : a0;
        if (addr == 0x1bf418 && a1) { W64(a1, 0); W64(a1 + 8, 0); }
        uint32_t ms = us / 1000 ? us / 1000 : 1;
        if (gNative && t) {                            // native: the thread sleeps, the kernel wakes it when the time has passed
            const int id = t->id;
            t->wakeAt = QpcNow() + (long long)((double)us * (double)QpcFreq() / 1e6);
            t->state = 6;
            GiveBack();
            Thr(id)->state = 1;
            return ret(0);
        }
        if (!t || t->id == 1) {                        // the game thread waits it out, the PS2 threads run meanwhile
            DeliverNetEvents();                        // (eenetctl's thread raises its events in such a wait)
            ULONGLONG end = GetTickCount64() + ms;
            for (;;) { Schedule(8); if (GetTickCount64() >= end) break; Sleep(1); }
            return ret(0);
        }
        int id = t->id;
        if (DevEnv("SIM_DELAY_LOG")) Log("thread %d sleeps %u ms", id, ms);
        t->state = 6; BackSet();                       // a PS2 thread sleeps without the baton: the game goes on
        Sleep(ms);
        t = Thr(id);
        if (DevEnv("SIM_DELAY_LOG")) Log("thread %d wakes (state %d)", id, t ? t->state : -1);
        t->state = 2; GoWait(id);
        PerfPs2Resumed();
        t = Thr(id); t->state = 1;
        return ret(0); }
    case 0x1f5920: return ret(0);                      // FlushCache
    case 0x1f5a60: {                                   // sceSifSetDma(desc*, count): EE -> IOP
        static uint32_t id = 0;
        for (uint32_t k = 0; k < a1 && k < 32; ++k) {
            uint32_t d = a0 + 16 * k, src = R32(d), dst = R32(d + 4), n = R32(d + 8);
            if (src && n && n < 0x100000 && src + n < 0x02000000) IopWrite(dst, gMem + src, n);
            Log("sif dma: %#x bytes from %08x to IOP %08x", n, src, dst);
        }
        return ret(++id); }
    case 0x1f5a40: return ret(-1);                     // sceSifDmaStat: done
    case 0x1f6170: return ret(gInIrq ? 2 : 0);         // COP0 Status: EXL while an interrupt handler runs
    case 0x1fefc8: W32(a0 & 0x1fffffff, a1); return ret(0);   // the SDK's kernel-mode store (KSEG1: the timer registers)
    case 0x1fe758: { int was = gEie ? 1 : 0; gEie = 0; return ret(was); }   // DI: returns whether they were enabled
    case 0x1fe7a8: gEie = 1; return ret(0);           // EI
    case 0x387b08: if (gNative) return false; return ret(0);   // empty function (ntmenu2; native: its own)
    case 0x185050: {                                   // the retail debug printf (empty on PS2): its text is logged
        const char* f = (a0 >= 0x100000 && a0 < 0x01000000) ? (const char*)(gMem + a0) : nullptr;
        if (f) {
            char line[160]; int n = 0;
            for (; n < 150 && f[n] && f[n] != '\n'; ++n) line[n] = (f[n] >= 32 && f[n] < 127) ? f[n] : '.';
            line[n] = 0;
            Log("debug print: \"%s\"", line);
        }
        return ret(0); }

    }
    return false;
}

// ---- libmc2 (overlay copy): requests complete at once on the virtual card.
// Request block 0x64C700: +4 command (0 = none), +8 result; completion is
// the semaphore whose id sits at 0x64CC28 (0x59a6a8 polls or waits on it).
std::set<uint32_t> gWatchSeen;
// SIM_WATCH=addr[:len]: each change of a watched word is logged with the function
// that accessed it last before the change (the write itself goes through InWin first)
std::map<uint32_t, std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> gWatchLast;
void WatchLog(uint32_t a) {
    static bool generic = DevEnv("SIM_WATCH") != nullptr;
    static bool readers = DevEnv("SIM_WATCH_READERS") != nullptr;   // each function touching the range, once
    if (generic && readers) {
        static std::set<uint64_t> seen;
        if (seen.size() < 2000 && seen.insert((uint64_t)(a & ~3u) << 32 | gLastCall).second)
            Log("watch: %08x touched in %08x (from %08x; run of %08x)", a, gLastCall, gLastCaller - 8, gRunFn);
        return;
    }
    if (generic) {
        uint32_t w4 = a & ~3u, v;
        memcpy(&v, gMem + (w4 & 0x01ffffff), 4);
        auto it = gWatchLast.find(w4);
        if (it != gWatchLast.end() && it->second.first != v)
            Log("watch: %08x %08x -> %08x, written in %08x (from %08x)", w4, it->second.first, v, it->second.second.first, it->second.second.second);
        gWatchLast[w4] = { v, { gLastCall, gLastCaller - 8 } };
        return;
    }
    static bool all = DevEnv("SIM_CARD_READS") != nullptr;          // every read, with the function reading it
    if (all || gWatchSeen.insert(a).second)
        Log("card info field read at %08x (+%#x) in %08x (from %08x)", a, a - 0x645654, gLastCall, gLastCaller - 8);
}
bool Mc2Done(Ctx& c, int cmd, int32_t result) {
    const uint32_t req = V(kMc2Req);                       // the running version's (its ntmenu2 page)
    W32(req + 4, (uint32_t)cmd);
    W32(req + 8, (uint32_t)result);
    uint32_t sid = R32(V(kMc2Sema));
    if (sid >= 1 && sid <= gSemas.size()) {
        bool woke = false;
        for (auto& t : gThreads) if (t.state == 4 && t.waitSema == (int)sid) { t.waitSema = -1; t.state = 2; woke = true; break; }
        if (!woke) ++gSemas[sid - 1].count;
    }
    c.r[2] = 0;                                    // request accepted
    return true;
}
// libmc2 on PC files, the way Konami's own PC ports served the memory card
// (MGS2's sceemux.c: <savedata>\<card directory>\<file>). MGO1's card is the folder
// MGO1_CARD_DIR (the plugin: <game>\mgs3_savedata_win\<SteamID64>\mgo1\card, next to
// MGS3's own saves but apart from them; the simulation: .\memcard). Each call is
// answered at once through the request block, results as libmc2 gives them
// (0x81010002 no entry, 0x81010011 exists, 0x81010014 not a directory).
namespace mc2 {
const int32_t kNoEnt = (int32_t)0x81010002, kExist = (int32_t)0x81010011, kNotDir = (int32_t)0x81010014;
std::string gCwd[2] = {"/", "/"};
std::string Root() {
    const char* r = getenv("MGO1_CARD_DIR");
    std::string root = (r && *r) ? r : "memcard";
    CreateDirectoryA(root.c_str(), nullptr);
    return root;
}
std::string Name(uint32_t a) { return (a && a < 0x02000000) ? std::string((const char*)(gMem + a)) : std::string(); }
// a card path (absolute or relative to the socket's directory) as "dir/file", no leading slash
std::string Norm(uint32_t sock, const std::string& name) {
    std::string full = (!name.empty() && name[0] == '/') ? name : gCwd[sock & 1] + "/" + name;
    std::vector<std::string> parts; std::string cur;
    for (size_t k = 0; k <= full.size(); ++k) {
        char ch = k < full.size() ? full[k] : '/';
        if (ch == '/' || ch == '\\') {
            if (cur == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (!cur.empty() && cur != ".") parts.push_back(cur);
            cur.clear();
        } else cur += ch;
    }
    std::string out;
    for (auto& q : parts) { if (!out.empty()) out += '/'; out += q; }
    return out;
}
std::string Host(const std::string& rel) {
    std::string h = Root();
    if (!rel.empty()) { h += '\\'; for (char ch : rel) h += (ch == '/') ? '\\' : ch; }
    return h;
}
bool Match(const char* p, const char* s) {                     // '*' and '?' as libmc2 (0x59AD80)
    if (!*p) return !*s;
    if (*p == '*') return Match(p + 1, s) || (*s && Match(p, s + 1));
    if (*s && (*p == '?' || *p == *s)) return Match(p + 1, s + 1);
    return false;
}
void PutTime(uint32_t a, const FILETIME& ft) {                  // sceMcStDateTime (JST on a PS2)
    SYSTEMTIME st{}; FILETIME lt{}; FileTimeToLocalFileTime(&ft, &lt); FileTimeToSystemTime(&lt, &st);
    W8(a, 0); W8(a + 1, (uint8_t)st.wSecond); W8(a + 2, (uint8_t)st.wMinute); W8(a + 3, (uint8_t)st.wHour);
    W8(a + 4, (uint8_t)st.wDay); W8(a + 5, (uint8_t)st.wMonth); W16(a + 6, st.wYear);
}
// one 0x38-byte entry as libmc2's GetDir fills it (0x59FA64): created, modified, size, mode, name[32]
void PutEntry(uint32_t a, const std::string& name, uint32_t size, bool dir, const FILETIME& c, const FILETIME& m) {
    memset(gMem + a, 0, 0x38);
    PutTime(a, c); PutTime(a + 8, m);
    W32(a + 0x10, size);
    W16(a + 0x14, dir ? 0x8427 : 0x8497);
    strncpy((char*)(gMem + a + 0x18), name.c_str(), 31);
}
struct Ent { std::string name; uint32_t size; bool dir; FILETIME c, m; };
std::vector<Ent> List(const std::string& rel, const std::string& pattern) {
    std::vector<Ent> out;
    DWORD at = GetFileAttributesA(Host(rel).c_str());
    if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) return out;
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    if (!rel.empty()) for (const char* dn : {".", ".."}) if (Match(pattern.c_str(), dn)) out.push_back({dn, 0, true, now, now});
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((Host(rel) + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == ".." || !Match(pattern.c_str(), n.c_str())) continue;
        out.push_back({n, fd.nFileSizeLow, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, fd.ftCreationTime, fd.ftLastWriteTime});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}
uint32_t FreeClusters() {                                      // an 8 MB card: 8192 clusters of 1 KB
    uint64_t used = 0;
    std::vector<std::string> todo = {""};
    while (!todo.empty()) {
        std::string d = todo.back(); todo.pop_back();
        for (auto& e : List(d, "*")) {
            if (e.name == "." || e.name == "..") continue;
            if (e.dir) { todo.push_back(d.empty() ? e.name : d + "/" + e.name); used += 1; }
            else used += (e.size + 1023) / 1024 + 1;
        }
    }
    return used >= 0x1f00 ? 0 : (uint32_t)(0x1f00 - used);
}
}  // namespace mc2
bool Mc2(Ctx& c, uint32_t addr) {
    using namespace mc2;
    uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5], a2 = (uint32_t)c.r[6], a3 = (uint32_t)c.r[7];
    uint32_t t0 = (uint32_t)c.r[8], t1 = (uint32_t)c.r[9];
    switch (addr) {
    // sockets: MGO1's card object (0x385F6C) asks the socket of ports 2 and 3 (the two
    // slots) with 0x5992C8, opening one with 0x5991B8 when there is none yet; the card
    // sits in slot 1 (port 2, socket 1), slot 2 is empty (port 3, socket 2)
    case 0x5992c8: case 0x5991b8: {
        uint32_t port = addr == 0x5992c8 ? a0 : R32(a0 + 4);
        int32_t sock = port == 2 ? 1 : port == 3 ? 2 : (int32_t)0x81010016;
        Log("card: socket for port %u -> %d", port, sock);
        c.r[2] = (uint64_t)(int64_t)sock;
        return true; }
    case 0x599248: c.r[2] = 0; return true;        // close a socket
    case 0x599548: {                               // GetInfo(socket, info*): type +0, formatted +4, free +8
        if (a0 != 1 && a0 != 2) { c.r[2] = (uint64_t)(int64_t)(int32_t)0x81018002; return true; }  // no such socket: the API refuses (0x599590)
        if (a0 == 2) {                             // the empty slot: the request runs and finds no card (0x59BC48)
            if (a1) { W32(a1, 0); W32(a1 + 4, 0); W32(a1 + 8, 0); }
            return Mc2Done(c, 2, (int32_t)0x81010013);
        }
        // the PC card is formatted; pc_card = 0 in the plugin (MGO1_NO_PC_CARD) or
        // SIM_CARD_UNFORMATTED=1 in the simulation shows an unformatted card instead
        static bool formatted = !DevEnv("MGO1_NO_PC_CARD") && !DevEnv("SIM_CARD_UNFORMATTED");
        if (a1) { W32(a1 + 0, 2); W32(a1 + 4, formatted ? 1 : 0); W32(a1 + 8, FreeClusters()); if (!DevEnv("SIM_WATCH")) { gWatchLo = a1; gWatchHi = a1 + 0x40; } }
        static bool announced = false;             // "new card" the first time, then 0
        int32_t res = 0;
        if (!announced) { announced = true; res = (int32_t)0x81019003; }
        if (res != 0) Log("card: GetInfo socket %u -> %08x (folder %s)", a0, (uint32_t)res, Root().c_str());
        return Mc2Done(c, 2, res); }
    case 0x5993e8: Log("card: Format socket %u", a0); Root(); return Mc2Done(c, 3, 0);
    case 0x599498: Log("card: Unformat socket %u (ignored)", a0); return Mc2Done(c, 4, 0);
    case 0x5997b8: case 0x5998d8: {                // ReadFile(socket, name, buf, offset, size)
        std::string rel = Norm(a0, Name(a1));
        FILE* f = fopen(Host(rel).c_str(), "rb");
        int32_t res = kNoEnt;
        if (f) {
            fseek(f, (long)a3, SEEK_SET);
            std::vector<uint8_t> buf(t0);
            size_t n = fread(buf.data(), 1, t0, f);
            for (size_t k = 0; k < n; ++k) W8(a2 + (uint32_t)k, buf[k]);    // any EE address (uncached, scratchpad)
            fclose(f); res = (int32_t)n;
        }
        Log("card: ReadFile %s at %u, %u bytes -> %d", rel.c_str(), a3, t0, res);
        return Mc2Done(c, addr == 0x5997b8 ? 5 : 32, res); }
    case 0x599b18: case 0x5999f8: {                // WriteFile(socket, name, buf, offset, size)
        std::string rel = Norm(a0, Name(a1)), host = Host(rel);
        FILE* f = fopen(host.c_str(), "r+b");
        if (!f) f = fopen(host.c_str(), "w+b");
        int32_t res = kNoEnt;
        if (f) {
            fseek(f, (long)a3, SEEK_SET);
            std::vector<uint8_t> buf(t0);
            for (uint32_t k = 0; k < t0; ++k) buf[k] = (uint8_t)R8(a2 + k);
            size_t n = fwrite(buf.data(), 1, t0, f);
            fclose(f); res = (int32_t)n;
        }
        Log("card: WriteFile %s at %u, %u bytes -> %d", rel.c_str(), a3, t0, res);
        return Mc2Done(c, addr == 0x599b18 ? 6 : 33, res); }
    case 0x599c38: case 0x599f48: {                // CreateFile (7) / Mkdir (11)
        std::string rel = Norm(a0, Name(a1)), host = Host(rel);
        int32_t res = 0;
        if (GetFileAttributesA(host.c_str()) != INVALID_FILE_ATTRIBUTES) res = kExist;
        else if (addr == 0x599f48) res = CreateDirectoryA(host.c_str(), nullptr) ? 0 : kNoEnt;
        else { FILE* f = fopen(host.c_str(), "wb"); if (f) fclose(f); else res = kNoEnt; }
        Log("card: %s %s -> %08x", addr == 0x599f48 ? "Mkdir" : "CreateFile", rel.c_str(), (uint32_t)res);
        return Mc2Done(c, addr == 0x599f48 ? 11 : 7, res); }
    case 0x599d30: {                               // Delete(socket, name): a file or an empty directory
        std::string rel = Norm(a0, Name(a1)), host = Host(rel);
        DWORD at = GetFileAttributesA(host.c_str());
        int32_t res = at == INVALID_FILE_ATTRIBUTES ? kNoEnt
                    : (at & FILE_ATTRIBUTE_DIRECTORY) ? (RemoveDirectoryA(host.c_str()) ? 0 : (int32_t)0x81010010)
                    : (DeleteFileA(host.c_str()) ? 0 : (int32_t)0x8101000d);
        Log("card: Delete %s -> %08x", rel.c_str(), (uint32_t)res);
        return Mc2Done(c, 8, res); }
    case 0x59a240: {                               // Rename(socket, old, new)
        std::string from = Norm(a0, Name(a1)), to = Norm(a0, Name(a2));
        int32_t res = MoveFileA(Host(from).c_str(), Host(to).c_str()) ? 0 : kNoEnt;
        Log("card: Rename %s -> %s: %08x", from.c_str(), to.c_str(), (uint32_t)res);
        return Mc2Done(c, 9, res); }
    case 0x599e18: {                               // GetDir(socket, pattern, offset, maxent, entries*, count*)
        std::string path = Name(a1);
        size_t cut = path.find_last_of('/');
        std::string dir = cut == std::string::npos ? "" : path.substr(0, cut + 1), pat = cut == std::string::npos ? path : path.substr(cut + 1);
        std::string rel = Norm(a0, dir.empty() ? "." : dir);
        auto ents = List(rel, pat.empty() ? "*" : pat);
        uint32_t n = 0;
        for (size_t k = a2; k < ents.size() && n < a3; ++k, ++n)
            if (t0) PutEntry(t0 + n * 0x38, ents[k].name, ents[k].size, ents[k].dir, ents[k].c, ents[k].m);
        if (t1) W32(t1, n);
        // the count goes to *count and the result is 0 (libmc2 0x59FB80)
        int32_t res = (GetFileAttributesA(Host(rel).c_str()) == INVALID_FILE_ATTRIBUTES) ? kNoEnt : 0;
        Log("card: GetDir %s (%s) from %u, max %u -> %u entries, %08x", rel.c_str(), pat.c_str(), a2, a3, n, (uint32_t)res);
        return Mc2Done(c, 10, res); }
    case 0x59a040: {                               // Chdir(socket, name, current*)
        std::string rel = Norm(a0, Name(a1));
        DWORD at = GetFileAttributesA(Host(rel).c_str());
        int32_t res = at == INVALID_FILE_ATTRIBUTES ? kNoEnt : !(at & FILE_ATTRIBUTE_DIRECTORY) ? kNotDir : 0;
        if (res == 0) gCwd[a0 & 1] = "/" + rel;
        if (a2) strcpy((char*)(gMem + a2), gCwd[a0 & 1].c_str());
        Log("card: Chdir %s -> %08x", rel.c_str(), (uint32_t)res);
        return Mc2Done(c, 12, res); }
    case 0x599608: {                               // SearchFile(socket, name, entry*)
        std::string path = Name(a1);
        size_t cut = path.find_last_of('/');
        std::string rel = Norm(a0, cut == std::string::npos ? "." : path.substr(0, cut + 1));
        auto ents = List(rel, cut == std::string::npos ? path : path.substr(cut + 1));
        int32_t res = kNoEnt;
        if (!ents.empty()) { if (a2) PutEntry(a2, ents[0].name, ents[0].size, ents[0].dir, ents[0].c, ents[0].m); res = 0; }
        Log("card: SearchFile %s -> %08x", path.c_str(), (uint32_t)res);
        return Mc2Done(c, 14, res); }
    case 0x59a140: case 0x59a360: case 0x599708: { // entry space (13), (15), (socket-only call)
        Log("card: request %08x (%s) answered", addr, Name(a1).c_str());
        return Mc2Done(c, addr == 0x59a140 ? 13 : addr == 0x59a360 ? 15 : 0, addr == 0x59a140 ? 30 : 0); }
    }
    return false;
}

// ---- sockets: the base ELF's EE TCP/IP stack API (BSD, newlib errno numbers,
// 4.4BSD sockaddr with a length byte) served by Winsock. MGO1's own code keeps
// every timeout and retry above this layer.
namespace net {
const int EAGAIN_ = 11, EINVAL_ = 22, EBADF_ = 9, EIO_ = 5, ECONNRESET_ = 104, ENOTSOCK_ = 108,
          ECONNREFUSED_ = 111, EADDRINUSE_ = 112, ECONNABORTED_ = 113, ENETUNREACH_ = 114, ETIMEDOUT_ = 116,
          EHOSTUNREACH_ = 118, EINPROGRESS_ = 119, EALREADY_ = 120, EMSGSIZE_ = 122, EISCONN_ = 127, ENOTCONN_ = 128;
bool gUp = false;
std::map<int, SOCKET> gFd;                  // PS2 descriptor -> host socket
std::map<int, bool> gNb;                    // non-blocking descriptors
// each socket's FIONBIO as last set (v141 beta 4: under Wine an ioctlsocket is a wineserver call, as long as a recvfrom:
// set only when it changes; none in the game's own case, its session socket non-blocking and read with flag 0x80)
std::map<int, bool> gOsNb;
void SetNb(int fd, SOCKET s, bool on) {
    const auto it = gOsNb.find(fd);
    if (it != gOsNb.end() && it->second == on) return;
    u_long v = on ? 1 : 0;
    if (ioctlsocket(s, FIONBIO, &v) == 0) gOsNb[fd] = on; else gOsNb.erase(fd);
}
int gNextFd = 1;
uint32_t gErrnoArea = 0, gHostent = 0;
void Up() {
    if (gUp) return;
    WSADATA wd;
    gUp = WSAStartup(MAKEWORD(2, 2), &wd) == 0;
    gErrnoArea = Alloc(0x400);                  // errno per PS2 thread (+4 * id), h_errno at +0x3fc
    gHostent = Alloc(0x200);
    Log("net: winsock %s", gUp ? "ready" : "failed");
}
uint32_t ErrnoAddr() { Up(); int id = gCur > 0 && gCur < 250 ? gCur : 0; return gErrnoArea + 4 * id; }
int FromWsa(int e) {
    switch (e) {
    case WSAEWOULDBLOCK: return EAGAIN_;
    case WSAEINPROGRESS: return EINPROGRESS_;
    case WSAEALREADY: return EALREADY_;
    case WSAECONNREFUSED: return ECONNREFUSED_;
    case WSAETIMEDOUT: return ETIMEDOUT_;
    case WSAECONNRESET: return ECONNRESET_;
    case WSAENOTCONN: return ENOTCONN_;
    case WSAEISCONN: return EISCONN_;
    case WSAEHOSTUNREACH: return EHOSTUNREACH_;
    case WSAENETUNREACH: return ENETUNREACH_;
    case WSAECONNABORTED: return ECONNABORTED_;
    case WSAEADDRINUSE: return EADDRINUSE_;
    case WSAEINVAL: return EINVAL_;
    case WSAEMSGSIZE: return EMSGSIZE_;
    case WSAENOTSOCK: return ENOTSOCK_;
    default: return EIO_;
    }
}
int Fail(int e) { W32(ErrnoAddr(), (uint32_t)e); return -1; }
SOCKET Sock(uint32_t fd) { auto it = gFd.find((int)fd); return it == gFd.end() ? INVALID_SOCKET : it->second; }
// redirects: "name=a.b.c.d,a.b.c.d=e.f.g.h,a.b.c.d:port=e.f.g.h:port" resolve a
// name, or send an address (or one port of it), elsewhere. Set by the plugin's
// `hosts` setting (MGO1_HOSTS) or by the simulation (SIM_HOSTS); what MGO1 asks
// for is unchanged and each redirect is logged.
std::map<std::string, std::string>& SimHosts() {
    static std::map<std::string, std::string> m;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        std::string all;
#ifndef MGO1_PLAYER_BUILD
        for (const char* v : {"MGO1_HOSTS", "SIM_HOSTS"})         // the plugin's `hosts` setting, the simulation's
            if (const char* s = DevEnv(v)) { if (!all.empty()) all += ','; all += s; }
#endif
        if (!all.empty()) {
            size_t k = 0;
            while (k < all.size()) {
                size_t e = all.find(',', k); if (e == std::string::npos) e = all.size();
                std::string item = all.substr(k, e - k); size_t q = item.find('=');
                if (q != std::string::npos) m[item.substr(0, q)] = item.substr(q + 1);
                k = e + 1;
            }
        }
    }
    return m;
}
// a name asked of a DNS server (UDP port 53, two tries of a second): the first address of its answer,
// as a PS2 whose network settings name that server gets it
bool DnsAsk(const char* server, const std::string& name, in_addr& out) {
    sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(53);
    if (!server || inet_pton(AF_INET, server, &to.sin_addr) != 1 || name.empty() || name.size() > 250) return false;
    std::vector<uint8_t> q = {0x4d, 0x47, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};    // id, recursion desired, one question
    for (size_t k = 0; k <= name.size();) {
        size_t e = name.find('.', k); if (e == std::string::npos) e = name.size();
        if (e == k || e - k > 63) return false;
        q.push_back((uint8_t)(e - k)); q.insert(q.end(), name.begin() + k, name.begin() + e);
        k = e + 1;
    }
    q.push_back(0); q.insert(q.end(), {0, 1, 0, 1});                            // type A, class IN
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    DWORD tmo = 1000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
    bool got = false, answered = false;
    uint8_t b[1500];
    for (int t = 0; t < 2 && !answered; ++t) {
        if (sendto(s, (const char*)q.data(), (int)q.size(), 0, (sockaddr*)&to, sizeof to) != (int)q.size()) break;
        for (;;) {
            sockaddr_in from{}; int fl = sizeof from;
            const int n = recvfrom(s, (char*)b, sizeof b, 0, (sockaddr*)&from, &fl);
            if (n < 12) break;                                                   // no answer in a second
            if (b[0] != 0x4d || b[1] != 0x47 || !(b[2] & 0x80)) continue;         // not the answer to this question
            answered = true;
            if (b[3] & 0xf) break;                                               // an error (no such name ...)
            auto skip = [&](size_t at) { while (at < (size_t)n) { const uint8_t l = b[at]; if (!l) return at + 1; if ((l & 0xc0) == 0xc0) return at + 2; at += 1 + l; } return (size_t)n; };
            size_t o = 12;
            for (int i = 0, qd = b[4] << 8 | b[5]; i < qd; ++i) o = skip(o) + 4;
            for (int i = 0, an = b[6] << 8 | b[7]; i < an && !got; ++i) {
                o = skip(o);
                if (o + 10 > (size_t)n) break;
                const int type = b[o] << 8 | b[o + 1], len = b[o + 8] << 8 | b[o + 9];
                o += 10;
                if (type == 1 && len == 4 && o + 4 <= (size_t)n) { memcpy(&out, b + o, 4); got = true; }
                o += len;
            }
            break;
        }
    }
    closesocket(s);
    return got;
}
// Platform::knownHosts: "name=address,..." (the answers kept for when the DNS server does not answer)
bool KnownHost(const std::string& name, in_addr& out) {
    if (!P.knownHosts) return false;
    const std::string all = P.knownHosts;
    for (size_t k = 0; k < all.size();) {
        size_t e = all.find(',', k); if (e == std::string::npos) e = all.size();
        const std::string item = all.substr(k, e - k);
        const size_t q = item.find('=');
        if (q != std::string::npos && _stricmp(item.substr(0, q).c_str(), name.c_str()) == 0 && inet_pton(AF_INET, item.substr(q + 1).c_str(), &out) == 1) return true;
        k = e + 1;
    }
    return false;
}
// web documents of another version's base, asked again under SaveMGO's North American one when the
// server has none there (SaveMGO hosts the US documents; the terms of use as text/policy_us_en.txt):
// set by NativeScriptServers for the EU and JP bases
struct WebFallback { std::string from, to; };     // path prefixes ("/eu/mgs3/" -> "/us/mgs3/")
WebFallback gWebFallback;
std::map<int, std::string> gWebAsked;             // descriptor -> what it sent (a GET or POST request), until the answer's first bytes
// the request again under the fallback's prefix (the terms of use: the US English document); "" none
std::string WebFallbackRequest(const std::string& req, std::string& pathWas, std::string& pathNow) {
    const size_t at = req.find(' ');
    if (gWebFallback.from.empty() || at == std::string::npos || at > 4) return "";
    const size_t sp = req.find(' ', at + 1);
    if (sp == std::string::npos) return "";
    pathWas = req.substr(at + 1, sp - at - 1);
    if (pathWas.compare(0, gWebFallback.from.size(), gWebFallback.from) != 0) return "";
    std::string doc = pathWas.substr(gWebFallback.from.size());
    if (doc.compare(0, 11, "text/policy") == 0) doc = "text/policy_us_en.txt";
    pathNow = gWebFallback.to + doc;
    return req.substr(0, at + 1) + pathNow + req.substr(sp);
}
// the redirected places datagrams went to -> the places MGO1 sent them to: an answer from there is given to the
// game as from the place it asked (a STUN server's answer on the session socket from another address was
// taken for a new peer: its session datagrams went to the STUN server, and a join waited on them)
std::map<uint64_t, uint64_t> gRedirectBack;
uint64_t AddrKey(const sockaddr_in& a) { uint32_t ip; memcpy(&ip, &a.sin_addr, 4); return (uint64_t)ip << 16 | a.sin_port; }
bool ToHost(uint32_t a, sockaddr_in& out) {
    if (!a) return false;
    memset(&out, 0, sizeof(out));
    out.sin_family = AF_INET;
    memcpy(&out.sin_port, gMem + a + 2, 2);
    memcpy(&out.sin_addr, gMem + a + 4, 4);
    auto& hosts = SimHosts();
    const uint64_t asked = AddrKey(out);
    struct Back { uint64_t asked; sockaddr_in& out; ~Back() { const uint64_t now = AddrKey(out); if (now != asked) gRedirectBack[now] = asked; } } back{asked, out};
    if (!hosts.empty()) {
        char ip[32]; const uint8_t* b = (const uint8_t*)&out.sin_addr;
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        char ipp[40]; snprintf(ipp, sizeof(ipp), "%s:%u", ip, ntohs(out.sin_port));
        auto it = hosts.find(ipp);
        if (it != hosts.end()) {                       // address and port
            std::string to = it->second; size_t c = to.find(':');
            inet_pton(AF_INET, to.substr(0, c).c_str(), &out.sin_addr);
            if (c != std::string::npos) out.sin_port = htons((u_short)atoi(to.c_str() + c + 1));
            Log("net: redirect: %s sent to %s", ipp, to.c_str());
        } else if ((it = hosts.find(ip)) != hosts.end()) { inet_pton(AF_INET, it->second.c_str(), &out.sin_addr); Log("net: redirect: %s sent to %s", ip, it->second.c_str()); }
    }
    return R8(a + 1) == 2;
}
void ToPs2(const sockaddr_in& in, uint32_t a) {
    if (!a) return;
    memset(gMem + a, 0, 16);
    W8(a, 16); W8(a + 1, 2);
    memcpy(gMem + a + 2, &in.sin_port, 2);
    memcpy(gMem + a + 4, &in.sin_addr, 4);
}
const char* Ip(const sockaddr_in& s) {
    static char b[32];
    const uint8_t* p = (const uint8_t*)&s.sin_addr;
    snprintf(b, sizeof(b), "%u.%u.%u.%u:%u", p[0], p[1], p[2], p[3], ntohs(s.sin_port));
    return b;
}
// a call that may block: a PS2 thread gives the baton back while it waits (the
// game and the other threads keep running); the game thread just waits
template <class F> auto Blocking(F f) -> decltype(f()) {
    EeThread* t = Thr(gCur);
    if (!t || t->id == 1) return f();
    const int id = t->id; const long long wakeWas = t->wakeAt, t0 = NetUsNow();
    NetWaitBegin(t);                                   // netcode: not taken back while it waits (netfix.h)
    t->state = 6;
    BackSet();
    auto r = f();
    t = Thr(id);
    const bool held = t->state != 6;                   // netcode: taken back by the kernel while it waited
    t->state = 2;
    NetWaitEnd(t, wakeWas, t0, held);                  // netcode: measured, its sleep's end put back (netfix.h)
    GoWait(id);
    PerfPs2Resumed();
    Thr(id)->state = 1;
    return r;
}
int NbFlag(uint32_t fd, uint32_t flags) { return (flags & 0x80) || gNb[(int)fd]; }
// data transfers of the recompiled code, for troubleshooting: sizes, the peer,
// the first line of HTTP, TX55 frame headers (command, length, sequence; no
// payload, account frames stay private), UDP previews; would-block results are
// counted and reported when data moves again
std::map<int, sockaddr_in> gPeer;
std::map<int, long> gWouldBlock;
std::string Tx55Heads(const uint8_t* p, int n) {
    static const uint8_t key[4] = {0x5A, 0x70, 0x85, 0xAF};
    std::string s; int i = 0, k = 0;
    while (i + 24 <= n && k < 8) {
        uint8_t h[8]; for (int j = 0; j < 8; ++j) h[j] = p[i + j] ^ key[j & 3];
        unsigned cmd = (h[0] << 8) | h[1], len = (h[2] << 8) | h[3];
        unsigned seq = ((unsigned)h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
        char b[64]; snprintf(b, sizeof(b), "%s%04x len %u seq %u", s.empty() ? "" : ", ", cmd, len, seq); s += b;
        i += 24 + (int)len; ++k;
    }
    if (i != n) { char b[48]; snprintf(b, sizeof(b), "%s(%d bytes not a whole frame)", s.empty() ? "" : " ", n - i); s += b; }
    return s;
}
bool gHostingNow = false;                               // a dedicated host with its game (HostWatch)
int gLobbyFd = -1;                                      // the lobby's connection: the one the game's 0x4310 went on
bool gLobbyClosed = false;
long long gLobbyRx = -1;                                // the frame of the lobby's last data
// (v141 beta 6) the lobby's last frames each way (their command and number, the frame then): the host watch's line
// (skip: the body's bytes still to come; hdr/have: a header read in pieces: the game reads the lobby's frames in parts, the
// first bench's "last received 0000 seq 0" came from headers never whole in one read)
struct LobbyLast { unsigned cmd = 0, seq = 0; long long frame = -1; int skip = 0, have = 0; uint8_t hdr[24] = {}; };
LobbyLast gLobbyLastOut, gLobbyLastIn;
void LobbyLastOf(const uint8_t* p, int n, LobbyLast& l) {
    static const uint8_t key[4] = {0x5A, 0x70, 0x85, 0xAF};
    int i = 0;
    while (i < n) {
        if (l.skip > 0) { const int k = std::min(n - i, l.skip); l.skip -= k; i += k; continue; }
        const int k = std::min(n - i, 24 - l.have);       // each frame's header: its command, length and number
        memcpy(l.hdr + l.have, p + i, k); l.have += k; i += k;
        if (l.have < 24) break;
        uint8_t h[8]; for (int j = 0; j < 8; ++j) h[j] = l.hdr[j] ^ key[j & 3];
        l.cmd = (h[0] << 8) | h[1];
        l.seq = ((unsigned)h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
        l.frame = gFrameNo;
        l.skip = (h[2] << 8) | h[3];
        l.have = 0;
    }
}
void NetData(const char* op, int fd, const sockaddr_in* peer, const uint8_t* p, int n) {
    // MGO1_QUIET_P2P=1: a dedicated host's datagrams not logged once it hosts (some 40 lines a second)
    static const bool quietP2p = DevEnv("MGO1_QUIET_P2P") != nullptr;
    if (n >= 2 && !strcmp(op, "send") && ((p[0] ^ 0x5A) << 8 | (p[1] ^ 0x70)) == 0x4310 && gLobbyFd != fd) {
        if (gLobbyFd >= 0) Log("net: the lobby's connection is now descriptor %d (it was %d%s)", fd, gLobbyFd, gLobbyClosed ? ", closed" : "");
        gLobbyFd = fd; gLobbyClosed = false; gLobbyRx = gFrameNo; gLobbyLastOut.skip = gLobbyLastIn.skip = 0; gLobbyLastOut.have = gLobbyLastIn.have = 0;
    }
    if (fd == gLobbyFd && !strcmp(op, "recv")) { gLobbyRx = gFrameNo; LobbyLastOf(p, n, gLobbyLastIn); }
    if (fd == gLobbyFd && !strcmp(op, "send")) LobbyLastOf(p, n, gLobbyLastOut);
#ifndef MGO1_PLAYER_BUILD
    if (fd == gLobbyFd && report::On() && (!strcmp(op, "send") || !strcmp(op, "recv"))) report::LobbyData(!strcmp(op, "send"), p, n);   // the Game Report: lobby frames (not peeks)
#endif
    auto wb = gWouldBlock.find(fd);
    // (v141 beta 2: a joiner's too, a doll's: 40 lines a second filled its day's log in an hour and a half; the lobby's kept)
    if (quietP2p && peer && fd != gLobbyFd) { if (wb != gWouldBlock.end()) wb->second = 0; return; }
    if (wb != gWouldBlock.end() && wb->second) { Log("net: %s(%d): %ld would-block before this", op, fd, wb->second); wb->second = 0; }
    unsigned port = peer ? ntohs(peer->sin_port) : 0;
    std::string what;
    if (n >= 4 && (!memcmp(p, "GET ", 4) || !memcmp(p, "POST", 4) || !memcmp(p, "HTTP", 4))) {
        int k = 0; while (k < n && k < 120 && p[k] != '\r' && p[k] != '\n') ++k;
        what = "\"" + std::string((const char*)p, k) + "\"";
    } else if (port >= 5731 && port <= 5739) {
        what = "tx55 " + Tx55Heads(p, n);
    } else {
        char b[80]; int k = 0;
        for (int j = 0; j < n && j < 16; ++j) k += snprintf(b + k, sizeof(b) - k, "%02x", p[j]);
        what = b;
    }
    Log("net: %s(%d) %d bytes %s%s: %s", op, fd, n, peer ? "peer " : "", peer ? Ip(*peer) : "", what.c_str());
}
// the host's network as the PS2 stack would know it after DHCP: the adapter the
// default route leaves by (address, netmask, gateway, DNS, MAC, MTU)
struct HostNet { uint32_t ip = 0, mask = 0, gw = 0, mtu = 1500; std::string dns1, dns2; uint8_t mac[6] = {0}; bool ok = false; };
HostNet& Host() {
    static HostNet h; static bool done = false;
    if (done) return h;
    done = true;
    ULONG size = 64 * 1024;
    std::vector<uint8_t> buf(size);
    ULONG fl = GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG r = GetAdaptersAddresses(AF_INET, fl, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    if (r == ERROR_BUFFER_OVERFLOW) { buf.resize(size); r = GetAdaptersAddresses(AF_INET, fl, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size); }
    if (r != NO_ERROR) { Log("net: host adapters not readable (%lu)", r); return h; }
    sockaddr_in probe{}; probe.sin_family = AF_INET; inet_pton(AF_INET, "8.8.8.8", &probe.sin_addr);
    DWORD best = 0; GetBestInterfaceEx((sockaddr*)&probe, &best);
    for (int pass = 0; pass < 2 && !h.ok; ++pass)
        for (auto* a = (IP_ADAPTER_ADDRESSES*)buf.data(); a && !h.ok; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (pass == 0 && best && a->IfIndex != best) continue;             // the default route's adapter first
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
                if (u->Address.lpSockaddr->sa_family == AF_INET) {
                    h.ip = ((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr;
                    unsigned pl = u->OnLinkPrefixLength;
                    h.mask = pl ? htonl(0xffffffffu << (32 - (pl > 32 ? 32 : pl))) : 0;
                    break;
                }
            for (auto* g = a->FirstGatewayAddress; g; g = g->Next)
                if (g->Address.lpSockaddr->sa_family == AF_INET) { h.gw = ((sockaddr_in*)g->Address.lpSockaddr)->sin_addr.s_addr; break; }
            int k = 0;
            for (auto* d = a->FirstDnsServerAddress; d && k < 2; d = d->Next)
                if (d->Address.lpSockaddr->sa_family == AF_INET) {
                    char s[32]; inet_ntop(AF_INET, &((sockaddr_in*)d->Address.lpSockaddr)->sin_addr, s, sizeof(s));
                    (k++ ? h.dns2 : h.dns1) = s;
                }
            if (a->PhysicalAddressLength >= 6) memcpy(h.mac, a->PhysicalAddress, 6);
            if (a->Mtu && a->Mtu < 65536) h.mtu = a->Mtu;
            h.ok = h.ip != 0;
        }
    char ip[20], mk[20], gw[20];
    inet_ntop(AF_INET, &h.ip, ip, sizeof(ip)); inet_ntop(AF_INET, &h.mask, mk, sizeof(mk)); inet_ntop(AF_INET, &h.gw, gw, sizeof(gw));
    Log("net: host interface for the PS2 stack: %s netmask %s gateway %s dns %s %s mtu %u%s", ip, mk, gw,
        h.dns1.empty() ? "-" : h.dns1.c_str(), h.dns2.empty() ? "-" : h.dns2.c_str(), h.mtu, h.ok ? "" : " (none found)");
    return h;
}
void NetError(const char* op, int fd, int wsa) {
    if (wsa == WSAEWOULDBLOCK) { ++gWouldBlock[fd]; return; }
    Log("net: %s(%d) failed, WSA error %d", op, fd, wsa);
}
#ifndef MGO1_PLAYER_BUILD
// ---- the accountless dolls' lobby filter (docs/PLAYABLE_DOLL.md 1, lobby_filter.h). MGO1_DOLL_IDS=<first>-<last>
// (host/mgo1host.sh) names the user ids host/mgo1-dollgate gives the server's dolls: on each TX55 connection of
// the host (its first frame a TX55 one), the frames naming them are kept here or sent as MGO1_DOLL_LIST says (off,
// own, host: lobby_filter.h's List; v141 beta 3, default host), the other frames renumbered; the game reads whole
// frames from the filter. Off without MGO1_DOLL_IDS ----
std::mutex gDollFilterLock;
std::map<int, std::shared_ptr<lobbyfilter::Conn>> gDollFilter;     // descriptor -> its connection's filter
std::set<int> gDollFilterNot;                                        // descriptors whose first bytes were no TX55 frame
const lobbyfilter::Ids* DollIds() {
    static lobbyfilter::Ids ids;
    static const bool on = [] {
        const char* v = DevEnv("MGO1_DOLL_IDS");
        if (!v) return false;
        if (!lobbyfilter::ParseIds(v, ids)) { Log("lobby filter: MGO1_DOLL_IDS=%s is not <first>-<last>: off", v); return false; }
        return true;
    }();
    return on ? &ids : nullptr;
}
// the filter's ids: the accountless dolls' (MGO1_DOLL_IDS) and the host's virtual dolls' (MGO1_VDOLL_IDS, docs/VIRTUAL_DOLL.md)
lobbyfilter::Ids FilterIds() {
    lobbyfilter::Ids ids = DollIds() ? *DollIds() : lobbyfilter::Ids{};
    if (vdoll::Count() > 0) { ids.vfirst = vdoll::FirstId(); ids.vlast = vdoll::FirstId() + (uint32_t)vdoll::Count() - 1; }
    return ids;
}
lobbyfilter::List DollList() {
    static const lobbyfilter::List mode = [] {
        lobbyfilter::List m = lobbyfilter::List::Host;
        const char* v = DevEnv("MGO1_DOLL_LIST");
        if (v && *v && !lobbyfilter::ParseList(v, m)) Log("lobby filter: MGO1_DOLL_LIST=%s is not off, own or host: host", v);
        const char* how = m == lobbyfilter::List::Off ? "kept off the lobby" : m == lobbyfilter::List::Own ? "listed in the game with their own ids (their statistics kept off)"
                                                                           : "listed in the game as the host's own account (their statistics kept off)";
        if (DollIds()) Log("lobby filter: the user ids %#x to %#x (the accountless dolls): %s", DollIds()->first, DollIds()->last, how);
        if (vdoll::Count() > 0)
            Log("lobby filter: the user ids %#x to %#x (the virtual dolls): %s", vdoll::FirstId(), vdoll::FirstId() + (uint32_t)vdoll::Count() - 1, how);
        return m;
    }();
    return mode;
}
// (v141 beta 7) SaveMGO's rankings and the dolls (docs/DEDICATED_HOST.md 11): MGO1_HOST_UNRANKED (server.env HOST_UNRANKED) auto
// (the default), always, off, a dedicated host's only. Unranked, its players' 0x4390 leave with a VS rating of 0 (lobby_filter.h
// SetUnranked): TX-55 keeps them for the game's own table, not for the rankings nor the VS rating. Auto: from the first doll seen in
// the game's session to the game's end (a report carries the player's VS rating as it is: a later round without dolls would rank
// what the dolls gave him)
enum class Unranked { Off, Auto, Always };
Unranked UnrankedMode() {
    static const Unranked m = [] {
        if (!DevEnv("MGO1_HOST_ONLY") && !DevEnv("MGO1_HOST_SETTINGS")) return Unranked::Off;
        const char* v = DevEnv("MGO1_HOST_UNRANKED");
        Unranked u = Unranked::Auto;
        if (v && *v) {
            if (!_stricmp(v, "off") || !strcmp(v, "0")) u = Unranked::Off;
            else if (!_stricmp(v, "always")) u = Unranked::Always;
            else if (_stricmp(v, "auto")) Log("unranked: MGO1_HOST_UNRANKED=%s is not auto, always or off: auto", v);
        }
        Log("unranked: %s (MGO1_HOST_UNRANKED)", u == Unranked::Off ? "off, every game's statistics ranked"
                                               : u == Unranked::Always ? "always, every game's statistics sent with a VS rating of 0"
                                                                       : "auto, the statistics of a game with a doll sent with a VS rating of 0");
        return u;
    }();
    return m;
}
const char* UnrankedName(Unranked u) { return u == Unranked::Off ? "off" : u == Unranked::Always ? "always" : "auto"; }
std::atomic<bool> gRoundDolls{false};                    // (auto) a doll seen in the game's session: this game unranked to its end
std::atomic<uint32_t> gUnrankedSent{0};                  // the 0x4390 sent unranked
bool UnrankedNow() { const Unranked u = UnrankedMode(); return u == Unranked::Always || (u == Unranked::Auto && gRoundDolls); }
void UnrankedRound();                                    // (a new round: below, with the session's players)
// the filter on: MGO1_DOLL_IDS, or (v141 beta 7) a dedicated host's unranked mode always, or auto with dolls on this server (their
// names list MGO1_DOLL_NAMES: dolls with an account); a host with no doll at all keeps the game's own sends and receives
bool DollFilterOn() {
    const Unranked u = UnrankedMode();
    return DollIds() || vdoll::Count() > 0 || u == Unranked::Always || (u == Unranked::Auto && DevEnv("MGO1_DOLL_NAMES"));
}
// send: the game's bytes through the filter, each frame sent whole; r the game's count. False: not filtered
bool DollFilterSend(int fd, SOCKET s, const std::vector<char>& data, int want, bool nb, int& r, int& e) {
    if (!DollFilterOn() || data.empty()) return false;
    std::vector<std::string> frames, log;
    {
        std::lock_guard<std::mutex> g(gDollFilterLock);
        if (gDollFilterNot.count(fd)) return false;
        auto it = gDollFilter.find(fd);
        if (it == gDollFilter.end()) {
            lobbyfilter::Frame f;
            bool ok = false;
            if (!lobbyfilter::Decode((const uint8_t*)data.data(), data.size(), f, &ok) || !ok) { gDollFilterNot.insert(fd); return false; }
            it = gDollFilter.emplace(fd, std::make_shared<lobbyfilter::Conn>(FilterIds(), DollList())).first;
            // (v141 beta 6) MGO1_LOBBY_FILTER_SEQ_LOG=1: a line a frame each way (lobby seq OUT / IN)
            static const bool seqLog = DevEnv("MGO1_LOBBY_FILTER_SEQ_LOG") != nullptr;
            it->second->SetSeqLog(seqLog);
            Log("lobby filter: on for descriptor %d (its first frame %04x)%s", fd, f.cmd, seqLog ? ", a line a frame" : "");
        }
        it->second->SetNow((long long)GetTickCount64());
        it->second->SetUnranked(UnrankedNow(), UnrankedMode() == Unranked::Always ? "always" : "a doll in this game");
        const uint32_t was = it->second->unranked;
        it->second->Out((const uint8_t*)data.data(), data.size(), frames, log);
        gUnrankedSent += it->second->unranked - was;
        it->second->TakeSeqLog(log);
    }
    for (const auto& l : log) Log("%s", l.c_str());
    SetNb(fd, s, false);
    r = want;
    for (const auto& f : frames) {
        for (size_t o = 0; o < f.size();) {
            const int k = Blocking([&] { const int x = send(s, f.data() + o, (int)(f.size() - o), 0); e = x < 0 ? WSAGetLastError() : 0; return x; });
            if (k <= 0) { r = -1; if (!e) e = WSAECONNRESET; WSASetLastError(e); break; }
            o += (size_t)k;
        }
        if (r < 0) break;
    }
    if (nb) SetNb(fd, s, true);
    return true;
}
// recv: the filter's whole frames (the lobby's, the answers given here); r as recv's. False: not filtered
bool DollFilterRecv(int fd, SOCKET s, char* out, int len, bool peek, bool nb, int& r, int& e) {
    if (len <= 0) return false;
    std::shared_ptr<lobbyfilter::Conn> c;
    {
        std::lock_guard<std::mutex> g(gDollFilterLock);
        auto it = gDollFilter.find(fd);
        if (it == gDollFilter.end()) return false;
        c = it->second;
    }
    auto take = [&]() -> int { std::lock_guard<std::mutex> g(gDollFilterLock); return c->Ready() ? (int)c->Take((uint8_t*)out, (size_t)len, peek) : -1; };
    auto pull = [&](long us) -> int {                     // the socket's bytes into the filter: 1 some, 0 closed, -1 error, 2 none
        if (us >= 0) {
            fd_set rs; FD_ZERO(&rs); FD_SET(s, &rs);
            timeval tv{0, us};
            const int k = ::select(0, &rs, nullptr, nullptr, &tv);
            if (k == 0) return 2;
            if (k < 0) { e = WSAGetLastError(); return -1; }
        }
        char b[4096];
        const int k = recv(s, b, sizeof b, 0);
        if (k > 0) {
            std::vector<std::string> lines;
            {
                std::lock_guard<std::mutex> g(gDollFilterLock);
                c->SetNow((long long)GetTickCount64());
                c->In((const uint8_t*)b, (size_t)k);
                c->TakeSeqLog(lines);
            }
            for (const auto& l : lines) Log("%s", l.c_str());
            return 1;
        }
        if (k == 0) return 0;
        e = WSAGetLastError();
        return e == WSAEWOULDBLOCK ? 2 : -1;
    };
    if ((r = take()) >= 0) return true;
    if (nb) {
        const int k = pull(-1);
        if ((r = take()) >= 0) return true;
        r = k == 0 ? 0 : -1;
        if (k > 0) e = WSAEWOULDBLOCK;                      // nothing yet, or a part of a frame only
    } else {
        r = Blocking([&] {                                // 20 ms waits: an answer given here is read at once
            for (;;) {
                const int t = take();
                if (t >= 0) return t;
                const int k = pull(20000);
                if (k == 0) return 0;
                if (k < 0) return -1;
            }
        });
    }
    if (r < 0) WSASetLastError(e);
    return true;
}
// (v141 beta 3) select, on the game's thread as its sends: the frames due on a filtered connection (the host's rows put
// back 3 s after a doll's leave, lobby_filter.h Due), each sent whole, in the connection's order
void DollFilterDue(const std::vector<int>& fds) {
    if (!DollFilterOn()) return;
    for (int fd : fds) {
        std::vector<std::string> frames, log;
        {
            std::lock_guard<std::mutex> g(gDollFilterLock);
            auto it = gDollFilter.find(fd);
            if (it == gDollFilter.end() || !it->second->RowsDue()) continue;
            it->second->SetNow((long long)GetTickCount64());
            it->second->Due(frames, log);
            it->second->TakeSeqLog(log);
        }
        for (const auto& l : log) Log("%s", l.c_str());
        const SOCKET s = Sock((uint32_t)fd);
        if (frames.empty() || s == INVALID_SOCKET) continue;
        SetNb(fd, s, false);
        for (const auto& f : frames) {
            bool lost = false;
            for (size_t o = 0; o < f.size();) {
                const int k = Blocking([&] { return send(s, f.data() + o, (int)(f.size() - o), 0); });
                if (k <= 0) { lost = true; break; }
                o += (size_t)k;
            }
            if (lost) { Log("lobby filter: descriptor %d: the frames due not sent (error %d)", fd, WSAGetLastError()); break; }
        }
        if (gNb[fd]) SetNb(fd, s, true);
    }
}
// select: the descriptors of the read set whose filter has bytes for the game (ready whatever the socket says)
std::vector<int> DollFilterReady(uint32_t readSet, const std::vector<int>& fds) {
    std::vector<int> v;
    if (!readSet || !DollFilterOn()) return v;
    std::lock_guard<std::mutex> g(gDollFilterLock);
    for (int fd : fds) {
        auto it = gDollFilter.find(fd);
        if (it != gDollFilter.end() && it->second->Ready() && (R32(readSet + (fd / 32) * 4) >> (fd % 32) & 1)
            && std::find(v.begin(), v.end(), fd) == v.end()) v.push_back(fd);
    }
    return v;
}
int DollFilterMark(uint32_t readSet, const std::vector<int>& ready, int n) {
    if (n < 0) return n;
    for (int fd : ready) {
        const uint32_t w = readSet + (fd / 32) * 4;
        if (!(R32(w) >> (fd % 32) & 1)) { W32(w, R32(w) | (1u << (fd % 32))); ++n; }
    }
    return n;
}
void DollFilterClose(int fd) {
    std::lock_guard<std::mutex> g(gDollFilterLock);
    gDollFilterNot.erase(fd);
    auto it = gDollFilter.find(fd);
    if (it == gDollFilter.end()) return;
    // (v141 beta 6) the whole state at the close: where each sequence stood, what was still awaited
    it->second->SetNow((long long)GetTickCount64());
    Log("lobby filter close: fd=%d %s", fd, it->second->DebugLine().c_str());
    if (it->second->kept || it->second->stripped)
        Log("lobby filter: descriptor %d closed: %u frames kept here and answered, %u ping entries left out", fd, it->second->kept, it->second->stripped);
    gDollFilter.erase(it);
}
#endif
}  // namespace net

// ---- the voice between PCs (docs/VOICE.md): the front end's Opus packets on the game's own UDP socket,
// beside its session datagrams (the same port, the same NAT mapping). A PC says hello to each peer of the
// session, 6 times at most (a PS2, or a PC of an older build, drops a datagram it cannot verify: its
// session's error count goes up 2 and the next good datagram clears it); a PC answers. A talker's packets
// go to the PCs among his peers (a joiner's only peer is his host); a host passes a joiner's packets on,
// once, to the other PCs of the talker's channel (the game's own list: the channel's players with a
// headset, NewVoiceChat + 0x44 + 0x70 k and + 0x334C). The listener's front end plays them while its game
// plays that talker's voice (VoiceNetHeard), and the game's LPC-10 of that talker is then left out of the
// headset's sound, its beeps kept (LgaudRpc). PS2 players keep the game's own voice both ways.
// A datagram: "MGVC", the version (1), the type (1 hello, 2 answer, 3 voice), the sender's player id, a
// flags byte (a hello: 1 the sender hosts); a voice packet then: the talker's player id, his talk (a new
// number each time he starts), the packet's number (16 bits), the channel (0..2), the hops (0 from the
// talker, 1 passed on by the host), 2 bytes kept 0, the Opus packet (20 ms); last, a 16-bit check.
// The same channel carries each player's choice of the added weapons (docs/CHARACTERS.md, The added weapons; REX
// Hangar is played by PCs only): type 4, then the chooser's player id, the hops, the count n, n pairs {the base
// weapon's number, the added weapon's rank + 1 (0: the base itself)}; sent when it changes and every 2 seconds on
// a map with added weapons, passed on once by the host. The hellos run while the voice or the added weapons do
// Type 5, a drop of a base weapon (a death's, by hand: its label names the dropper's weapon, DropLabel): the
// dropper's player id, the hops, the code, the added weapon's rank + 1 (0 the base), the drop's number (16 bits), its
// place (3 floats); sent 3 times a second apart, passed on once by the host
namespace vnet {
constexpr uint8_t kVersion = 1, kHello = 1, kAnswer = 2, kVoice = 3, kWeapons = 4, kDrop = 5, kBases = 6, kGrenade = 7;   // kBases: basecap.h, kGrenade: netfix.h
constexpr size_t kHead = 8, kVoiceHead = 16, kMaxOpus = 400;
constexpr int kHellos = 6;
struct Peer {
    uint32_t ip = 0; uint16_t port = 0;                  // ip in network order
    unsigned long long seen = 0, hello = 0;              // the last game datagram either way, the last hello
    int hellos = 0, slot = -1;
    bool pc = false, saidPs2 = false;
    long long sent = 0, got = 0;
};
struct Out { std::vector<uint8_t> opus; bool newTalk; };
struct State {
    std::mutex mu;
    int fd = -1;                                         // the game's descriptor of its session socket
    std::vector<Peer> peers;
    std::deque<Out> out;                                 // the front end's packets, sent on the game's thread
    std::deque<std::vector<uint8_t>> in;                 // voice packets received (their datagram)
    uint16_t seq = 0; uint8_t talk = 0;
    long long sent = 0, received = 0, passed = 0, dropped = 0, foreign = 0;
} gVn;
uint16_t Check(const uint8_t* p, size_t n) {             // FNV-1a folded to 16 bits
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return (uint16_t)(h ^ (h >> 16));
}
bool Ours(const uint8_t* p, size_t n) {
    return n >= kHead + 2 && p[0] == 'M' && p[1] == 'G' && p[2] == 'V' && p[3] == 'C' &&
           Check(p, n - 2) == (uint16_t)(p[n - 2] | p[n - 1] << 8);
}
void Seal(std::vector<uint8_t>& d) { const uint16_t c = Check(d.data(), d.size()); d.push_back((uint8_t)c); d.push_back((uint8_t)(c >> 8)); }
std::vector<uint8_t> Head(uint8_t type, uint8_t flags) {
    const int self = OwnSlot();
    return {'M', 'G', 'V', 'C', kVersion, type, (uint8_t)(self >= 0 ? self : 0xff), flags};
}
std::string Text(const Peer& p) { return PeerText((uint64_t)p.ip << 16 | p.port); }
Peer* Find(uint32_t ip, uint16_t port) {
    for (auto& p : gVn.peers) if (p.ip == ip && p.port == port) return &p;
    return nullptr;
}
bool SessionPort(uint16_t port) { return port != 53 && port != 1900 && port != 3478 && port != 3479; }
void SendTo(const std::vector<uint8_t>& d, Peer& p) {    // (gVn.mu held, the game's thread)
    const SOCKET s = net::Sock((uint32_t)gVn.fd);
    if (s == INVALID_SOCKET) return;
    static const int loss = DevEnv("SIM_NET_LOSS") ? atoi(DevEnv("SIM_NET_LOSS")) : 0;   // simulation: a poor link here too
    if (loss > 0) { static uint32_t lcg = 777; lcg = lcg * 1103515245u + 12345u; if ((int)((lcg >> 16) % 100) < loss) return; }
    sockaddr_in to{}; to.sin_family = AF_INET; to.sin_addr.s_addr = p.ip; to.sin_port = htons(p.port);
    if (sendto(s, (const char*)d.data(), (int)d.size(), 0, (sockaddr*)&to, sizeof to) == (int)d.size()) ++p.sent;
}
// the game sent a session datagram (from its socket fd) to, or received one from, a peer
void GameDatagram(int fd, const sockaddr_in& a, bool out) {
    const uint16_t port = ntohs(a.sin_port);
    if (!SessionPort(port)) return;
    std::lock_guard<std::mutex> g(gVn.mu);
    if (out) gVn.fd = fd; else if (fd != gVn.fd) return;
    Peer* p = Find(a.sin_addr.s_addr, port);
    if (!p) { if (!out) return; gVn.peers.push_back(Peer{}); p = &gVn.peers.back(); p->ip = a.sin_addr.s_addr; p->port = port; }
    const unsigned long long now = PsMs();
    if (now - p->seen > 30000) { p->hellos = 0; p->pc = false; p->saidPs2 = false; }   // a new session with it
    p->seen = now;
}
// the channel's players who hear a talker there (the game's own list; all when no voice actor)
uint32_t Hearers(int ch, int talker) {
    for (const auto& a : gNativeActors)
        if (a.second == "NewVoiceChat") {
            if (ch < 0 || ch > 2) return ~0u;
            return R32(a.first + 0x44 + 0x70 * ch) & R32(a.first + 0x334c) & ~(talker >= 0 && talker < 32 ? 1u << talker : 0u);
        }
    return ~0u;
}
int OwnChannel() {
    for (const auto& a : gNativeActors) if (a.second == "NewVoiceChat") return (int)R32(a.first + 0x3344);
    return -1;
}
// a datagram of ours came (the game's thread): answered, delivered to the front end, passed on
bool LinkOn() { return gVoiceOpusOn || !ExtraMapsWeapons().empty() || BcLink() || NetBlastSyncOn(); }   // the hellos: for the voice, the weapons, BASE CAPTURE, the grenades (netfix.h)
void WeaponsIn(Peer* p, const uint8_t* d, size_t n);
void BasesIn(Peer* p, const uint8_t* d, size_t n);       // BASE CAPTURE (basecap.h)
void DropIn(Peer* p, const uint8_t* d, size_t n);
void GrenadeIn(Peer* p, const uint8_t* d, size_t n);    // netcode: the grenades (netfix.h)
void Receive(int fd, const sockaddr_in& from, const uint8_t* d, size_t n) {
    std::lock_guard<std::mutex> g(gVn.mu);
    Peer* p = fd == gVn.fd ? Find(from.sin_addr.s_addr, ntohs(from.sin_port)) : nullptr;
    const unsigned long long now = PsMs();
    if (!p || now - p->seen > 30000 || d[4] != kVersion) { ++gVn.foreign; return; }   // not a peer of the session
    ++p->got;
    const uint8_t type = d[5], slot = d[6];
    if (type == kWeapons) { p->pc = true; WeaponsIn(p, d, n); return; }
    if (type == kDrop) { p->pc = true; DropIn(p, d, n); return; }
    if (type == kBases) { p->pc = true; BasesIn(p, d, n); return; }
    if (type == kGrenade) { p->pc = true; GrenadeIn(p, d, n); return; }
    if (type == kHello || type == kAnswer) {
        if (!LinkOn()) return;
        if (!p->pc) {
            char who[24] = "";
            if (slot < 32) snprintf(who, sizeof who, "player %d, ", slot + 1);
            Log("voice net: %s is a PC (%s%s): voices between us in Opus, the added weapons' choices", Text(*p).c_str(), who, (d[7] & 1) ? "the host" : "a joiner");
        }
        p->pc = true;
        if (slot < 32) p->slot = slot;
        if (type == kHello) { auto a = Head(kAnswer, IsSessionHost() ? 1 : 0); Seal(a); SendTo(a, *p); }
        return;
    }
    if (type != kVoice || n < kVoiceHead + 3 || !gVoiceOpusOn) { ++gVn.dropped; return; }
    const int talker = d[8], ch = d[12], hops = d[13];
    if (talker >= 32) { ++gVn.dropped; return; }
    if (!hops && slot < 32) p->slot = slot;             // the sender's player id, known now
    gVoiceOpus[talker] = now;
    ++gVn.received;
    gVn.in.emplace_back(d, d + n);
    while (gVn.in.size() > 100) { gVn.in.pop_front(); ++gVn.dropped; }
    if (hops || !IsSessionHost()) return;
    std::vector<uint8_t> f(d, d + n - 2);                // the host passes it on: hops 1
    f[13] = 1; Seal(f);
    const uint32_t mask = Hearers(ch, talker);
    for (auto& q : gVn.peers)
        if (&q != p && q.pc && now - q.seen < 5000 && (q.slot < 0 || (mask >> q.slot & 1))) { SendTo(f, q); ++gVn.passed; }
}
// the game's thread (each pass of the actor lists): hellos, the front end's packets sent, old peers gone
void WeaponsOut(unsigned long long now);
void DropsOut(unsigned long long now);
void BasesOut(unsigned long long now);
void GrenadesOut(unsigned long long now);
void Pump() {
    std::lock_guard<std::mutex> g(gVn.mu);
    const unsigned long long now = PsMs();
    gVn.peers.erase(std::remove_if(gVn.peers.begin(), gVn.peers.end(), [&](const Peer& p) { return now - p.seen > 60000; }), gVn.peers.end());
    if (!LinkOn() || gVn.fd < 0 || Ps2Like()) { gVn.out.clear(); return; }   // netcode: tests, a stock PS2 sends none (netfix.h)
    WeaponsOut(now);
    DropsOut(now);
    BasesOut(now);
    GrenadesOut(now);
    for (auto& p : gVn.peers) {
        if (p.pc || now - p.seen > 5000) continue;
        if (p.hellos < kHellos && now - p.hello >= 1000) {
            auto h = Head(kHello, IsSessionHost() ? 1 : 0); Seal(h); SendTo(h, p);
            ++p.hellos; p.hello = now;
        } else if (p.hellos >= kHellos && !p.saidPs2 && now - p.hello >= 2000) {
            p.saidPs2 = true;
            Log("voice net: %s did not answer %d hellos: the game's own voice with it (a PS2, or a PC of an older build)", Text(p).c_str(), kHellos);
        }
    }
    const int self = OwnSlot(), ch = OwnChannel();
    if (!gVoiceOpusOn) gVn.out.clear();
    while (!gVn.out.empty()) {
        Out o = std::move(gVn.out.front()); gVn.out.pop_front();
        if (self < 0 || o.opus.empty()) continue;
        if (o.newTalk) ++gVn.talk;
        std::vector<uint8_t> d = Head(kVoice, 0);
        const uint16_t seq = gVn.seq++;
        const uint8_t more[8] = {(uint8_t)self, gVn.talk, (uint8_t)seq, (uint8_t)(seq >> 8), (uint8_t)(ch >= 0 && ch <= 2 ? ch : 0xff), 0, 0, 0};
        d.insert(d.end(), more, more + 8);
        d.insert(d.end(), o.opus.begin(), o.opus.end());
        Seal(d);
        const uint32_t mask = IsSessionHost() ? Hearers(ch, self) : ~0u;
        for (auto& p : gVn.peers)
            if (p.pc && now - p.seen < 5000 && (p.slot < 0 || (mask >> p.slot & 1))) { SendTo(d, p); ++gVn.sent; }
    }
}
// this player's choice of the added weapons: to the PCs among his peers when it changes and every 2 seconds (gVn.mu held)
void WeaponsOut(unsigned long long now) {
    const auto& list = ExtraMapsWeapons();
    const int self = OwnSlot();
    if (list.empty() || self < 0 || self >= 32) return;
    std::vector<uint8_t> pairs;
    std::vector<int> bases;
    for (const auto& w : list)
        if (std::find(bases.begin(), bases.end(), w.base) == bases.end()) {
            bases.push_back(w.base);
            pairs.push_back((uint8_t)w.base); pairs.push_back(gAddedChoice[self][w.base]);
        }
    static std::vector<uint8_t> last;
    static unsigned long long sentAt = 0;
    if (pairs == last && now - sentAt < 2000) return;
    if (pairs != last) Log("added weapons: this player's choice sent to the PCs of the game (%zu weapons)", bases.size());
    last = pairs; sentAt = now;
    std::vector<uint8_t> d = Head(kWeapons, IsSessionHost() ? 1 : 0);
    d.push_back((uint8_t)self); d.push_back(0); d.push_back((uint8_t)bases.size());
    d.insert(d.end(), pairs.begin(), pairs.end());
    Seal(d);
    for (auto& p : gVn.peers) if (p.pc && now - p.seen < 5000) SendTo(d, p);
}
// another player's choice (gVn.mu held): kept for his weapons' models, stances, lasers; a host passes it on once
void WeaponsIn(Peer* p, const uint8_t* d, size_t n) {
    if (n < kHead + 3 + 2) return;
    const int who = d[kHead], hops = d[kHead + 1], count = d[kHead + 2];
    if (who >= 32 || who == OwnSlot() || n < kHead + 3 + 2 * (size_t)count + 2) return;
    const size_t listed = ExtraMapsWeapons().size();
    bool changed = false;
    for (int k = 0; k < count; ++k) {
        const int base = d[kHead + 3 + 2 * k], rank = d[kHead + 4 + 2 * k];
        if (base <= 0 || base >= 0x40 || (size_t)rank > listed) continue;
        changed |= gAddedChoice[who][base] != rank;
        gAddedChoice[who][base] = (uint8_t)rank;
    }
    if (changed) Log("added weapons: player %d's choice received (%d weapons)", who + 1, count);
    if (hops || !IsSessionHost()) return;
    std::vector<uint8_t> f(d, d + n - 2);                  // the host passes it on: hops 1
    f[kHead + 1] = 1; Seal(f);
    const unsigned long long now = PsMs();
    for (auto& q : gVn.peers) if (&q != p && q.pc && now - q.seen < 5000) SendTo(f, q);
}
// this player's drops of base weapons: to the PCs among his peers, 3 times a second apart (gVn.mu held)
void DropsOut(unsigned long long now) {
    std::vector<std::vector<uint8_t>> out;
    {
        std::lock_guard<std::mutex> g(gDropsLock);
        const int self = OwnSlot();
        for (auto& x : gDrops) {
            if (x.who != self || x.sends >= 3 || (x.sends && now - x.sent < 1000)) continue;
            std::vector<uint8_t> d = Head(kDrop, IsSessionHost() ? 1 : 0);
            const uint8_t more[6] = {(uint8_t)x.who, 0, (uint8_t)x.code, (uint8_t)x.rank, (uint8_t)x.seq, (uint8_t)(x.seq >> 8)};
            d.insert(d.end(), more, more + 6);
            const uint8_t* f = (const uint8_t*)x.pos;
            d.insert(d.end(), f, f + 12);
            Seal(d);
            out.push_back(std::move(d));
            ++x.sends; x.sent = now;
        }
    }
    for (const auto& d : out) for (auto& p : gVn.peers) if (p.pc && now - p.seen < 5000) SendTo(d, p);
}
// another player's drop (gVn.mu held): kept for its label; a host passes it on once
void DropIn(Peer* p, const uint8_t* d, size_t n) {
    if (n < kHead + 6 + 12 + 2) return;
    const int who = d[kHead], hops = d[kHead + 1], code = d[kHead + 2], rank = d[kHead + 3], seq = d[kHead + 4] | d[kHead + 5] << 8;
    if (who >= 32 || who == OwnSlot() || code <= 0 || code >= 0x40 || (size_t)rank > ExtraMapsWeapons().size()) return;
    Drop x;
    x.who = who; x.seq = seq; x.code = code; x.rank = rank; x.ms = PsMs(); x.sends = 3;
    memcpy(x.pos, d + kHead + 6, 12);
    {
        std::lock_guard<std::mutex> g(gDropsLock);
        for (const auto& y : gDrops) if (y.who == who && y.seq == seq) return;   // one of its copies came already
        gDrops.push_back(x);
        while (gDrops.size() > 64) gDrops.erase(gDrops.begin());
    }
    const auto& list = ExtraMapsWeapons();
    Log("added weapons: player %d's drop %d received: weapon %#x as the %s at %.0f %.0f %.0f", who + 1, seq, code,
        rank ? list[(size_t)rank - 1].name.c_str() : "base itself", x.pos[0], x.pos[1], x.pos[2]);
    if (hops || !IsSessionHost()) return;
    std::vector<uint8_t> f(d, d + n - 2);                  // the host passes it on: hops 1
    f[kHead + 1] = 1; Seal(f);
    const unsigned long long now = PsMs();
    for (auto& q : gVn.peers) if (&q != p && q.pc && now - q.seen < 5000) SendTo(f, q);
}
// netcode (netfix.h): type 7, a grenade of this player's (0 made, 1 exploded): the thrower's player id, the hops, the
// kind, the grenade's number (16 bits), the place (3 floats); sent 3 times 50 ms apart, passed on by the host each time
void GrenadesOut(unsigned long long now) {
    std::vector<std::vector<uint8_t>> out;
    {
        std::lock_guard<std::mutex> g(gNetNadesLock);
        const int self = OwnSlot();
        for (auto& m : gNetNadesOut) {
            if (self < 0 || self >= 32 || m.sends >= 3 || (m.sends && now - m.last < 50)) continue;
            std::vector<uint8_t> d = Head(kGrenade, IsSessionHost() ? 1 : 0);
            d.push_back((uint8_t)self); d.push_back(0);
            d.insert(d.end(), m.body.begin(), m.body.end());
            Seal(d);
            out.push_back(std::move(d));
            ++m.sends; m.last = now;
        }
        while (!gNetNadesOut.empty() && gNetNadesOut.front().sends >= 3) gNetNadesOut.pop_front();
    }
    for (const auto& d : out) for (auto& p : gVn.peers) if (p.pc && now - p.seen < 5000) SendTo(d, p);
}
void GrenadeIn(Peer* p, const uint8_t* d, size_t n) {
    if (n < kHead + 2 + 15 + 2) return;
    const int who = d[kHead], hops = d[kHead + 1];
    if (!hops && p->slot >= 0 && p->slot != who) return;  // a thrower tells his own grenades only
    if (NetBlastSyncOn()) NetNadeHeard(who, d + kHead + 2, n - kHead - 2 - 2);
    if (hops || !IsSessionHost() || who >= 32) return;
    std::vector<uint8_t> f(d, d + n - 2);                  // the host passes each copy on: hops 1
    f[kHead + 1] = 1; Seal(f);
    const unsigned long long now = PsMs();
    for (auto& q : gVn.peers) if (&q != p && q.pc && now - q.seen < 5000) SendTo(f, q);
}
void Report() {                                          // a match's end: what went
    NetNadeReport();
    std::lock_guard<std::mutex> g(gVn.mu);
    if (!gVn.sent && !gVn.received) return;
    int pcs = 0; for (const auto& p : gVn.peers) pcs += p.pc ? 1 : 0;
    Log("voice net: %lld Opus packets sent, %lld received, %lld passed on, %lld dropped, %lld datagrams of others; %d PC(s) among %zu peer(s)",
        gVn.sent, gVn.received, gVn.passed, gVn.dropped, gVn.foreign, pcs, gVn.peers.size());
    gVn.sent = gVn.received = gVn.passed = gVn.dropped = gVn.foreign = 0;
}
}  // namespace vnet
bool NetPeerPc(uint64_t peer) {                          // netcode (netfix.h NetChanProbe): it answered the hellos
    std::lock_guard<std::mutex> g(vnet::gVn.mu);
    const vnet::Peer* q = vnet::Find((uint32_t)(peer >> 16), (uint16_t)(peer & 0xffff));
    return q && q->pc;
}
// ---- The name of a dropped weapon (Snake, 23:22: a weapon dropped at a death or by hand named as the weapon it
// is, FAMAS ..., in the language shown; docs/CHARACTERS.md, The added weapons: dropped). A player's drops are made by
// his own program (US n003a 0x3A10E0: a0 the drops' manager [0x2E5268], a1 the code, a2 the count, a3 3 a weapon, 1
// an item, the place his; v0 0, or -1 when none was made; its record the manager + 0xD0 + 0x60 k numbered last (the
// manager's +0xC0): +0 its number, +4 kind << 10 | code, +0x10 its place; at a death the primary weapon and the
// support one (0x3A1750), by hand 0x3A1E78, 0x3A1F3C), each record a box on every program (0x39FE20 -> 0x4C6660),
// whose label (0x4C54D0: a0 the item, +0x44 its code, +0x1F8 its body, its place at +0x40, +0x208 a name of its
// own, else the names of this player's descriptors: his own choice's) names it. A drop of a base weapon is kept with the dropper's weapon (the added weapon's rank + 1, 0 the base) and
// sent to the other PCs (vnet type 5); a label of a base weapon's box is given the name of the drop of that code
// at its record's place (the box falls and slides away from it: 5 to 11 m in runs D7, D10), the base's own when none
// is known
bool AddedBase(int code) {
    for (const auto& w : ExtraMapsWeapons()) if (w.base == code) return true;
    return false;
}
void DropMade(const Ctx& c, int result) {           // result: 0 made, -1 none (the record: the one numbered last, +0xC0)
    const int self = OwnSlot();
    const uint32_t mgr = (uint32_t)c.r[4];
    if (self < 0 || result < 0 || mgr < 0x100000 || mgr >= 0x01FFF000) return;
    int record = -1;
    for (int k = 0; k < 16 && record < 0; ++k) if (R32(mgr + 0xD0 + 0x60 * (uint32_t)k) == R32(mgr + 0xC0)) record = k;
    if (record < 0) { Log("added weapons: a drop of this player's not found among the records (the last number %u)", R32(mgr + 0xC0)); return; }
    const uint32_t rec = mgr + 0xD0 + 0x60 * (uint32_t)record, flags = R32(rec + 4);
    const int code = (int)(flags & 0x1FF), kind = (int)((flags & 0x1E00) >> 10);
    if (kind != 3 || !AddedBase(code)) return;
    Drop x;
    static int seq = 0;
    x.who = self; x.seq = ++seq & 0xFFFF; x.code = code; x.rank = gAddedChoice[self][code]; x.ms = PsMs();
    for (int k = 0; k < 3; ++k) x.pos[k] = F32(rec + 0x10 + 4 * (uint32_t)k);
    {
        std::lock_guard<std::mutex> g(gDropsLock);
        gDrops.push_back(x);
        while (gDrops.size() > 64) gDrops.erase(gDrops.begin());
    }
    const auto& list = ExtraMapsWeapons();
    Log("added weapons: this player dropped weapon %#x as the %s (drop %d, record %d, at %.0f %.0f %.0f)", code,
        x.rank ? list[(size_t)x.rank - 1].name.c_str() : "base itself", x.seq, record, x.pos[0], x.pos[1], x.pos[2]);
}
// a box made (US n003a 0x4C6660: t2 its record's place, its code on the stack; v0 0 when none; the box the work named
// NewItemBox* during it): its record's place kept
void BoxMade(const Ctx& entry, uint32_t box) {
    const int code = (int)(R32((uint32_t)entry.r[29]) & 0x1FF);
    const uint32_t p = (uint32_t)entry.r[10];
    if (box < 0x100000 || box >= 0x01FFF000 || p < 0x100000 || p >= 0x01FFF000 || !AddedBase(code)) return;
    std::lock_guard<std::mutex> g(gDropsLock);
    Box& b = gBoxes[box];
    b.code = code;
    for (int k = 0; k < 3; ++k) b.pos[k] = F32(p + 4 * (uint32_t)k);
    while (gBoxes.size() > 128) gBoxes.erase(gBoxes.begin());
}
// the drop a box of a base weapon came from: its record's place when the box was made from a record (the same on every
// program; a drop within 1 m of it), else its body's (within 5 m); the nearest drop of that code: its rank (0 the base
// itself), its dropper (-1: none known there), its number, how far. False: the box has no body
struct BoxFrom { int rank = 0, who = -1, seq = 0; float away = 0.f; bool record = false; float at[3]{}; };
bool BoxDrop(uint32_t item, int code, BoxFrom& b) {
    const uint32_t body = R32(item + 0x1F8);
    if (body < 0x100000 || body >= 0x01FFF000) return false;
    b = BoxFrom();
    b.at[0] = F32(body + 0x40); b.at[1] = F32(body + 0x44); b.at[2] = F32(body + 0x48);
    float best = 500.f * 500.f;
    std::lock_guard<std::mutex> g(gDropsLock);
    const auto it = gBoxes.find(item);
    if (it != gBoxes.end() && it->second.code == code) { memcpy(b.at, it->second.pos, sizeof b.at); b.record = true; best = 100.f * 100.f; }
    for (const auto& x : gDrops) {
        if (x.code != code) continue;
        const float d = (x.pos[0] - b.at[0]) * (x.pos[0] - b.at[0]) + (x.pos[1] - b.at[1]) * (x.pos[1] - b.at[1]) + (x.pos[2] - b.at[2]) * (x.pos[2] - b.at[2]);
        if (d < best) { best = d; b.rank = x.rank; b.who = x.who; b.seq = x.seq; }
    }
    b.away = std::sqrt(best);
    return true;
}
std::string BoxFromText(const BoxFrom& b) {
    return b.who < 0 ? std::string("no drop known there: the base's own") :
        "player " + std::to_string(b.who + 1) + "'s drop " + std::to_string(b.seq) + ", " + std::to_string((int)b.away) + " away";
}
std::string AddedRankName(int rank, int code) {          // an added weapon's name by its rank + 1, the base's (0)
    const auto& list = ExtraMapsWeapons();
    return rank > 0 && rank <= (int)list.size() ? list[(size_t)rank - 1].name : PS2Str(StockWeaponName(code), 32) + " (the base itself)";
}
void DropLabel(uint32_t item) {
    if (item < 0x100000 || item >= 0x01FFF000) return;
    const int code = (int)R32(item + 0x44);
    if (code <= 0 || code >= 0x40 || !AddedBase(code)) return;
    BoxFrom b;
    if (!BoxDrop(item, code, b)) return;
    const auto& list = ExtraMapsWeapons();
    const bool added = b.rank > 0 && b.rank <= (int)list.size();
    const uint32_t name = added ? AddedString(AddedWeaponText(list[(size_t)b.rank - 1], false, AddedTextLang())) : StockWeaponName(code);
    static std::set<uint32_t> ours;                      // the names given here (a name of the box's own is left as it is)
    const uint32_t cur = R32(item + 0x208);
    if (!name || cur == name || (cur && !ours.count(cur))) return;
    ours.insert(name);
    W32(item + 0x208, name);
    Log("added weapons: the label of the dropped weapon %#x (%s %.0f %.0f %.0f) named \"%s\" (%s)", code, b.record ? "its record at" : "at",
        b.at[0], b.at[1], b.at[2], PS2Str(name, 32).c_str(), BoxFromText(b).c_str());
}
// #119 (a FAMAS box gives the FAMAS): a weapon of an added weapon's base taken from a box by this player (the pickup,
// US n003a 0x4C9FC8, a0 the box: +0x40 its kind, 2 a weapon, +0x44 its code, +0x48 its count; 0x2FD5B8 gives the
// weapon, 1 when it was not held, -1 when it was: then its ammunition only). A weapon gained is the one the box's drop
// was (its dropper's weapon), the base's own when no drop is known there (the map's own boxes); held already, it stays
// the weapon held. The choice goes to the other PCs (vnet type 4) and the HUD's icons follow
void BoxTaken(const Ctx& c, uint32_t box, int code, bool gained) {
    const int self = OwnSlot();
    if (self < 0 || self >= 32 || code <= 0 || code >= 0x40 || !AddedBase(code) || box < 0x100000 || box >= 0x01FFF000) return;
    BoxFrom b;
    const bool known = BoxDrop(box, code, b);
    const int was = gAddedChoice[self][code];
    if (!gained) {
        if (Once(((uint64_t)107 << 40) | (box & 0xFFFFFF)))
            Log("added weapons: this player took the ammunition of weapon %#x from a box (the %s, %s): its %s kept", code,
                AddedRankName(b.rank, code).c_str(), known ? BoxFromText(b).c_str() : "no body", AddedRankName(was, code).c_str());
        return;
    }
    gAddedChoice[self][code] = (uint8_t)b.rank;
    ++gBoxesTaken;
    Log("added weapons: this player took weapon %#x from a box: the %s (%s; its weapon of that base was the %s)", code,
        AddedRankName(b.rank, code).c_str(), known ? BoxFromText(b).c_str() : "the box has no body: the base's own",
        AddedRankName(was, code).c_str());
    AddedTaken(c, code, false);                          // its icons first now
    AddedIcons(c);
}

// (v150-host beta 21, cover) simulation: the session's datagrams held by SIM_NET_DELAY (Net's sendto), sent once due
struct NetDelayed { SOCKET s; sockaddr_in to; std::vector<uint8_t> d; ULONGLONG due; };
std::deque<NetDelayed> gNetDelayed;
void NetDelayFlush() {
    if (gNetDelayed.empty()) return;
    const ULONGLONG now = GetTickCount64();
    while (!gNetDelayed.empty() && gNetDelayed.front().due <= now) {
        const NetDelayed& n = gNetDelayed.front();
        sendto(n.s, (const char*)n.d.data(), (int)n.d.size(), 0, (const sockaddr*)&n.to, sizeof(n.to));
        gNetDelayed.pop_front();
    }
}
bool Net(Ctx& c, uint32_t addr) {
    using namespace net;
    uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5], a2 = (uint32_t)c.r[6], a3 = (uint32_t)c.r[7];
    uint32_t t0 = (uint32_t)c.r[8], t1 = (uint32_t)c.r[9];
    auto ret = [&](int64_t v) { c.r[2] = (uint64_t)v; return true; };
    switch (addr) {
    case 0x1bcc50: return ret(ErrnoAddr());                       // errno location
    case 0x1bcc88: Up(); return ret(gErrnoArea + 0x3fc);           // h_errno location
    case 0x1bcf50: {                                               // socket(af, type, proto)
        Up();
        int type = a1 == 1 ? SOCK_STREAM : a1 == 2 ? SOCK_DGRAM : (int)a1;
        SOCKET s = socket(AF_INET, type, a2 == 0 ? 0 : (int)a2);
        if (s == INVALID_SOCKET) { Log("net: socket(%u, %u, %u) failed %d", a0, a1, a2, WSAGetLastError()); return ret(Fail(FromWsa(WSAGetLastError()))); }
        int fd = gNextFd++;
        gFd[fd] = s; gNb[fd] = false;
        Log("net: socket(%u, %u, %u) -> %d", a0, a1, a2, fd);
        return ret(fd); }
    case 0x1bd020: {                                               // bind(s, addr, len)
        SOCKET s = Sock(a0); sockaddr_in sa;
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        ToHost(a1, sa);
        // SIM_PORT_SHIFT: a second simulation on the same address moves its UDP ports
        static int shift = DevEnv("SIM_PORT_SHIFT") ? atoi(DevEnv("SIM_PORT_SHIFT")) : 0;
        int st = 0, stl = sizeof(st);
        u_short bp = ntohs(sa.sin_port);                 // MGO1's game ports (5730-5745: 16 players) stay as MGO1 chose them
        if (shift && bp && (bp < 5730 || bp > 5745) && getsockopt(s, SOL_SOCKET, SO_TYPE, (char*)&st, &stl) == 0 && st == SOCK_DGRAM)
            sa.sin_port = htons((u_short)(ntohs(sa.sin_port) + shift));
        int r = bind(s, (sockaddr*)&sa, sizeof(sa));
        Log("net: bind(%u, %s) -> %d", a0, Ip(sa), r == 0 ? 0 : WSAGetLastError());
        return ret(r == 0 ? 0 : Fail(FromWsa(WSAGetLastError()))); }
    case 0x1bd420: {                                               // connect(s, addr, len)
        SOCKET s = Sock(a0); sockaddr_in sa;
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        ToHost(a1, sa);
        int e = 0;
        int r = Blocking([&] { int k = connect(s, (sockaddr*)&sa, sizeof(sa)); e = k == 0 ? 0 : WSAGetLastError(); return k; });
        Log("net: connect(%u, %s) -> %s (%d, %s)", a0, Ip(sa), r == 0 ? "connected" : "error", e, gNb[(int)a0] ? "non-blocking" : "blocking");
        gPeer[(int)a0] = sa;
        if (r == 0) return ret(0);
        if (e == WSAEWOULDBLOCK) return ret(Fail(EINPROGRESS_));
        return ret(Fail(FromWsa(e))); }
    case 0x1bd590: {                                               // send(s, buf, len, flags)
        SOCKET s = Sock(a0);
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        a1 &= 0x01ffffff;                                           // scratchpad buffers too (0x70000000)
        if (a2 > 0x100000 || a1 + a2 > 0x02000000) return ret(Fail(EINVAL_));
        std::vector<char> tmp(gMem + a1, gMem + a1 + a2);
        // a web document asked under another name (gWebDocAlias): the request sent with that path, the
        // game told its own length went out
        for (auto& [from, to] : gWebDocAlias) {
            if (a2 < 16 || memcmp(tmp.data(), "GET ", 4)) break;
            const std::string req(tmp.begin(), tmp.end());
            const size_t at = req.find(from), eol = req.find("\r\n");
            if (at == std::string::npos || eol == std::string::npos || at > eol) continue;
            const std::string now = req.substr(0, at) + to + req.substr(at + from.size());
            Log("net: web request %s asked as %s", from.c_str(), to.c_str());
            tmp.assign(now.begin(), now.end());
            break;
        }
        if (!gWebFallback.from.empty()) {                          // a web request's bytes, kept until its answer (WebFallbackRequest)
            auto wa = gWebAsked.find((int)a0);
            if (wa != gWebAsked.end()) wa->second.append(tmp.begin(), tmp.end());
            else if (tmp.size() >= 16 && (!memcmp(tmp.data(), "GET ", 4) || !memcmp(tmp.data(), "POST ", 5))) gWebAsked[(int)a0].assign(tmp.begin(), tmp.end());
        }
        const int want = (int)tmp.size();
        bool nb = NbFlag(a0, a3);
        SetNb((int)a0, s, nb);
        int e = 0;
        int r = 0;
#ifndef MGO1_PLAYER_BUILD
        if (!DollFilterSend((int)a0, s, tmp, want, nb, r, e))       // (the accountless dolls' lobby filter)
#endif
        r = nb ? send(s, tmp.data(), want, 0) : Blocking([&] { int k = send(s, tmp.data(), want, 0); e = k < 0 ? WSAGetLastError() : 0; return k; });
        if (r == want && want != (int)a2) r = (int)a2;
        if (nb && r < 0) e = WSAGetLastError();
        if (!gNb[(int)a0]) SetNb((int)a0, s, false);
        if (r < 0) { NetError("send", (int)a0, e); return ret(Fail(FromWsa(e))); }
        { auto pe = gPeer.find((int)a0); NetData("send", (int)a0, pe != gPeer.end() ? &pe->second : nullptr, (const uint8_t*)tmp.data(), r); }
        return ret(r); }
    case 0x1bd9d8: {                                               // recv(s, buf, len, flags)
        SOCKET s = Sock(a0);
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        a1 &= 0x01ffffff;                                           // scratchpad buffers too (0x70000000)
        if (a2 > 0x100000 || a1 + a2 > 0x02000000) return ret(Fail(EINVAL_));
        std::vector<char> tmp(a2 ? a2 : 1);
        bool nb = NbFlag(a0, a3);
        SetNb((int)a0, s, nb);
        int fl = (a3 & 2) ? MSG_PEEK : 0;
        int e = 0;
        int r = 0;
#ifndef MGO1_PLAYER_BUILD
        if (!DollFilterRecv((int)a0, s, tmp.data(), (int)a2, fl != 0, nb, r, e))   // (the lobby filter: whole frames)
#endif
        r = nb ? recv(s, tmp.data(), (int)a2, fl) : Blocking([&] { int k = recv(s, tmp.data(), (int)a2, fl); e = k < 0 ? WSAGetLastError() : 0; return k; });
        if (nb && r < 0) e = WSAGetLastError();
        if (!gNb[(int)a0]) SetNb((int)a0, s, false);
        if (r < 0) { NetError("recv", (int)a0, e); return ret(Fail(FromWsa(e))); }
        if (auto wa = gWebAsked.find((int)a0); wa != gWebAsked.end() && !fl) {      // a web answer's first bytes
            const std::string req = wa->second;
            gWebAsked.erase(wa);
            const int status = r >= 12 && !memcmp(tmp.data(), "HTTP/1.", 7) ? atoi(tmp.data() + 9) : 0;
            std::string was, now;
            const std::string again = status && status != 200 && !nb ? WebFallbackRequest(req, was, now) : "";
            auto pe = gPeer.find((int)a0);
            if (!again.empty() && pe != gPeer.end()) {
                const sockaddr_in peer = pe->second;
                SOCKET s2 = INVALID_SOCKET;
                const int r2 = Blocking([&] {
                    s2 = socket(AF_INET, SOCK_STREAM, 0);
                    if (s2 == INVALID_SOCKET) return -1;
                    if (connect(s2, (const sockaddr*)&peer, sizeof peer) != 0 || send(s2, again.data(), (int)again.size(), 0) != (int)again.size()) return -1;
                    return (int)recv(s2, tmp.data(), (int)a2, 0);
                });
                const int st2 = r2 >= 12 && !memcmp(tmp.data(), "HTTP/1.", 7) ? atoi(tmp.data() + 9) : 0;
                Log("net: web document %s answered %d: asked as %s, answered %d", was.c_str(), status, now.c_str(), st2);
                if (r2 > 0) { closesocket(s); gFd[(int)a0] = s2; r = r2; }
                else if (s2 != INVALID_SOCKET) closesocket(s2);
            }
        }
        memcpy(gMem + a1, tmp.data(), r);
        if (r == 0) { Log("net: recv(%u): the peer closed the connection", a0); if ((int)a0 == gLobbyFd) gLobbyClosed = true; }
        else { auto pe = gPeer.find((int)a0); NetData(fl ? "peek" : "recv", (int)a0, pe != gPeer.end() ? &pe->second : nullptr, (const uint8_t*)tmp.data(), r); }
        return ret(r); }
    case 0x1bd640: {                                               // sendto(s, buf, len, flags, to, tolen)
        SOCKET s = Sock(a0); sockaddr_in sa;
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        a1 &= 0x01ffffff;                                           // scratchpad buffers too (0x70000000)
        if (a2 > 0x10000 || a1 + a2 > 0x02000000) return ret(Fail(EINVAL_));
        ToHost(t0, sa);
#ifndef MGO1_PLAYER_BUILD
        // a virtual doll's: read in the clear at the packet recorder, never sent
        if (vdoll::On() && vdoll::Ours(sa.sin_addr.s_addr, ntohs(sa.sin_port))) return ret((int)a2);
#endif
        {   // simulation: SIM_NET_LOSS=<percent> of the session's datagrams (UDP 5730..5799) lost on the way out,
            // SIM_NET_OUTAGE=<every s>:<ms>: all of them lost for that long, that often (a poor link);
            // SIM_NET_LOSS_TO=<peer>:<percent>[,...]: those to that peer alone (the host's link to one player; the peer its port,
            // the bench's instances: 5730 + its number, or under 256 the last byte of its address)
            static const int loss = DevEnv("SIM_NET_LOSS") ? atoi(DevEnv("SIM_NET_LOSS")) : 0;
            static const char* outage = DevEnv("SIM_NET_OUTAGE");
            static const char* lossTo = DevEnv("SIM_NET_LOSS_TO");
            const uint16_t port = ntohs(sa.sin_port);
            int lossHere = loss;
            if (lossTo) {
                const unsigned last = ((const uint8_t*)&sa.sin_addr)[3];
                for (const char* c = lossTo; c && *c;) {
                    unsigned octet = 0; int pct = 0;
                    if (sscanf(c, "%u:%d", &octet, &pct) == 2 && (octet < 256 ? octet == last : octet == port)) lossHere = std::max(lossHere, pct);
                    c = strchr(c, ','); if (c) ++c;
                }
            }
            // (v141 beta 12) SIM_FIRST_JOIN_LOST=1: every session datagram lost until the lobby's first error message (a failed join)
            static const bool firstJoinLost = DevEnv("SIM_FIRST_JOIN_LOST") != nullptr;
            if (firstJoinLost && gErrFrame < 0 && port >= 5730 && port < 5800) lossHere = 100;
            if ((lossHere > 0 || outage) && port >= 5730 && port < 5800) {
                static uint32_t lcg = 12345; static long long dropped = 0;
                lcg = lcg * 1103515245u + 12345u;
                bool drop = lossHere > 0 && (int)((lcg >> 16) % 100) < lossHere;
                if (outage) {
                    const double every = atof(outage); const char* c = strchr(outage, ':'); const double ms = c ? atof(c + 1) : 500;
                    const double t = PsMs() / 1000.0;
                    if (every > 0 && fmod(t, every) < ms / 1000.0) drop = true;
                }
                if (drop) {
                    if (++dropped <= 5 || !(dropped & (dropped - 1))) Log("net: simulation: a datagram to port %u lost (%lld so far)", port, dropped);
                    return ret((int)a2);
                }
            }
        }
        // (v150-host beta 21, cover) simulation: SIM_NET_DELAY=<ms>: the session's datagrams (UDP 5730..5799) leave that much
        // later (a far player's link; set on both ends of a bench for a way each): held here, sent by NetDelayFlush at
        // the next sendto or recvfrom once due
        static const int delayMs = DevEnv("SIM_NET_DELAY") ? atoi(DevEnv("SIM_NET_DELAY")) : 0;
        NetDelayFlush();
        int r;
        if (delayMs > 0 && ntohs(sa.sin_port) >= 5730 && ntohs(sa.sin_port) < 5800) {
            gNetDelayed.push_back({s, sa, std::vector<uint8_t>(gMem + a1, gMem + a1 + a2), GetTickCount64() + (ULONGLONG)delayMs});
            r = (int)a2;
        } else r = sendto(s, (const char*)gMem + a1, (int)a2, 0, (sockaddr*)&sa, sizeof(sa));
        if (r < 0) { int e = WSAGetLastError(); NetError("sendto", (int)a0, e); return ret(Fail(FromWsa(e))); }
        NetJoinCopies(s, sa, gMem + a1, r);                        // netcode: a connect in both regions' dialects (netfix.h)
        NetData("sendto", (int)a0, &sa, gMem + a1, r);
        { const uint16_t port = ntohs(sa.sin_port);                 // the session's peers (SessionPeers)
          if (port != 1900 && port != 3478 && port != 3479) { std::lock_guard<std::mutex> g(gUdpPeersLock); gUdpPeers[sa.sin_addr.s_addr] = GetTickCount64(); } }
        vnet::GameDatagram((int)a0, sa, true);
        return ret(r); }
    case 0x1bda88: {                                               // recvfrom(s, buf, len, flags, from, fromlen*)
        SOCKET s = Sock(a0);
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        NetDelayFlush();                                            // (beta 21, simulation: SIM_NET_DELAY's datagrams due)
        a1 &= 0x01ffffff;                                           // scratchpad buffers too (0x70000000)
        if (a2 > 0x10000 || a1 + a2 > 0x02000000) return ret(Fail(EINVAL_));
        // the voice between PCs' datagrams (vnet) are taken out: the game reads the next one, or would block
        // (SIM_VNET_PASS, tests: left to the game, as a PS2 or an older build gets them)
        static const bool vnetPass = getenv("SIM_VNET_PASS") != nullptr;
#ifndef MGO1_PLAYER_BUILD
        if (vdoll::On() && (uint32_t)c.r[31] == V(0x16e548)) {      // the session engine's read: a virtual doll's datagram first
            std::vector<uint8_t> d; uint32_t ip = 0; uint16_t port = 0;
            if (vdoll::Pop(d, &ip, &port) && d.size() <= a2) {
                memcpy(gMem + a1, d.data(), d.size());
                sockaddr_in from{};
                from.sin_family = AF_INET; from.sin_addr.s_addr = ip; from.sin_port = htons(port);
                ToPs2(from, t0);
                if (t1) W32(t1, 16);
                if (t0) gLastRecvPeer = ((uint64_t)R32(t0 + 4) << 16) | (uint16_t)((R8(t0 + 2) << 8) | R8(t0 + 3));
                gLastRecvTick = GetTickCount64();
                if (t0) PeerHeard(gLastRecvPeer);
                return ret((int)d.size());
            }
        }
#endif
        std::vector<char> tmp(std::max<size_t>(a2 ? a2 : 1, 2048));
        bool nb = NbFlag(a0, a3);
        SetNb((int)a0, s, nb);
        sockaddr_in from{}; int fl = sizeof(from);
        int r = 0, e = 0;
        for (;;) {
            fl = sizeof(from);
            if (nb && NetLinkCfg().on) r = NetRecvDelayed(s, tmp.data(), (int)tmp.size(), &from, &fl, e);   // netcode: tests, a poor link (netfix.h)
            else {
            r = nb ? recvfrom(s, tmp.data(), (int)tmp.size(), 0, (sockaddr*)&from, &fl)
                   : Blocking([&] { const int k = recvfrom(s, tmp.data(), (int)tmp.size(), 0, (sockaddr*)&from, &fl); e = k < 0 ? WSAGetLastError() : 0; return k; });
            if (nb) e = r < 0 ? WSAGetLastError() : 0;     // netcode: a blocking call's error taken in it (B_ERR)
            }
            if (r > 0 && vnet::Ours((const uint8_t*)tmp.data(), (size_t)r)) {
                if (vnetPass) { static int told = 0; if (++told <= 10) Log("net: a voice datagram of a PC (%d bytes) left to the game (SIM_VNET_PASS)", r); break; }
                vnet::Receive((int)a0, from, (const uint8_t*)tmp.data(), (size_t)r);
                continue;
            }
            break;
        }
        if (r > (int)a2) { r = -1; e = WSAEMSGSIZE; }               // longer than the game's buffer, as Winsock says it
        if (!gNb[(int)a0]) SetNb((int)a0, s, false);
        if (r < 0) { NetError("recvfrom", (int)a0, e); return ret(Fail(FromWsa(e))); }
        memcpy(gMem + a1, tmp.data(), r);
        NetData("recvfrom", (int)a0, &from, (const uint8_t*)tmp.data(), r);
        vnet::GameDatagram((int)a0, from, false);
        {   // an answer from a redirected place: from the place MGO1 asked
            const auto rb = gRedirectBack.find(AddrKey(from));
            if (rb != gRedirectBack.end()) { const uint32_t ip = (uint32_t)(rb->second >> 16); memcpy(&from.sin_addr, &ip, 4); from.sin_port = (u_short)(rb->second & 0xffff); }
        }
        ToPs2(from, t0);
        if (t1) W32(t1, 16);
        if (t0) gLastRecvPeer = ((uint64_t)R32(t0 + 4) << 16) | (uint16_t)((R8(t0 + 2) << 8) | R8(t0 + 3));
        gLastRecvTick = GetTickCount64();
        if (t0) PeerHeard(gLastRecvPeer);                           // (v150-host beta 18) JoinerHold: its last datagram
        return ret(r); }
    case 0x1bdfa0: {                                               // setsockopt(s, level, name, val, len)
        SOCKET s = Sock(a0);
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        uint32_t v = a3 ? R32(a3) : 0;
        int r = 0;
        if (a1 == 0xffff && a2 == 0x1009) {                         // non-blocking I/O
            gNb[(int)a0] = v != 0;
            u_long on = v ? 1 : 0; r = ioctlsocket(s, FIONBIO, &on);
            if (r == 0) gOsNb[(int)a0] = v != 0; else gOsNb.erase((int)a0);
        } else if (a1 == 0xffff && (a2 == 0x1005 || a2 == 0x1006)) {  // send / receive timeout (timeval)
            DWORD ms = R32(a3) * 1000 + R32(a3 + 4) / 1000;
            r = setsockopt(s, SOL_SOCKET, a2 == 0x1005 ? SO_SNDTIMEO : SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
        } else if (a1 == 0 && a3) {                                  // IPPROTO_IP: BSD numbers are Winsock's
            if (a2 == 12 || a2 == 13) {                             // IP_ADD / DROP_MEMBERSHIP {group, interface}
                ip_mreq m; memcpy(&m, gMem + a3, 8);
                r = setsockopt(s, IPPROTO_IP, a2 == 12 ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, (const char*)&m, sizeof(m));
            } else if (a2 == 9) {                                   // IP_MULTICAST_IF (in_addr)
                in_addr ia; memcpy(&ia, gMem + a3, 4);
                r = setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ia, sizeof(ia));
            } else if (a2 == 10 || a2 == 11) {                      // IP_MULTICAST_TTL / LOOP: a byte on BSD
                DWORD x = t0 == 1 ? R8(a3) : R32(a3);
                r = setsockopt(s, IPPROTO_IP, a2 == 10 ? IP_MULTICAST_TTL : IP_MULTICAST_LOOP, (const char*)&x, sizeof(x));
            } else r = setsockopt(s, IPPROTO_IP, (int)a2, (const char*)&v, sizeof(v));
        } else {
            int lvl = a1 == 0xffff ? SOL_SOCKET : (int)a1, name = (int)a2;
            if (a1 == 0xffff) name = a2 == 0x1001 ? SO_SNDBUF : a2 == 0x1002 ? SO_RCVBUF : (int)a2;
            r = setsockopt(s, lvl, name, (const char*)&v, sizeof(v));
        }
        Log("net: setsockopt(%u, %#x, %#x, %u) -> %d", a0, a1, a2, v, r);
        return ret(r == 0 ? 0 : Fail(FromWsa(WSAGetLastError()))); }
    case 0x1be648: {                                               // select(nfds, read, write, except, timeout)
        fd_set sets[3]; uint32_t ptr[3] = {a1, a2, a3};
        std::vector<int> fds;
        for (int k = 0; k < 3; ++k) FD_ZERO(&sets[k]);
        for (uint32_t fd = 0; fd < a0 && fd < 256; ++fd)
            for (int k = 0; k < 3; ++k)
                if (ptr[k] && (R32(ptr[k] + (fd / 32) * 4) >> (fd % 32) & 1)) {
                    SOCKET s = Sock(fd);
                    if (s != INVALID_SOCKET) { FD_SET(s, &sets[k]); fds.push_back((int)fd); }
                }
        timeval tv{0, 0}; timeval* tp = nullptr;
        if (t0) { tv.tv_sec = (long)R32(t0); tv.tv_usec = (long)R32(t0 + 4); tp = &tv; }
        if (!tp || tv.tv_sec > 0 || tv.tv_usec > 20000) { tv.tv_sec = 0; tv.tv_usec = 20000; tp = &tv; }   // the game thread never blocks long
#ifndef MGO1_PLAYER_BUILD
        DollFilterDue(fds);                                               // (the lobby filter's frames due: sent first)
        const std::vector<int> filtered = DollFilterReady(ptr[0], fds);   // (the lobby filter's frames waiting: ready, no wait)
        if (!filtered.empty()) { tv.tv_sec = 0; tv.tv_usec = 0; }
#endif
        const long long selT0 = NetUsNow();
        int n = fds.empty() ? 0 : ::select(0, ptr[0] ? &sets[0] : nullptr, ptr[1] ? &sets[1] : nullptr, ptr[2] ? &sets[2] : nullptr, tp);
        const int selErr = n < 0 ? WSAGetLastError() : 0;    // netcode: the error before any other call (B_ERR)
        if (!fds.empty() && (tv.tv_sec || tv.tv_usec)) NetSelectWaited(NetUsNow() - selT0);   // netcode: measured (netfix.h)
        static bool selLog = DevEnv("SIM_SELECT_LOG") != nullptr;
        if (selLog) {
            std::string l;
            for (int fd : fds) {
                u_long avail = 0; ioctlsocket(Sock((uint32_t)fd), FIONREAD, &avail);
                char b[40]; snprintf(b, sizeof(b), " %d(%lu pending)", fd, (unsigned long)avail); l += b;
            }
            Log("net: select(%u) on%s -> %d", a0, l.c_str(), n);
        }
        for (int k = 0; k < 3; ++k) if (ptr[k]) for (uint32_t q = 0; q < (a0 + 31) / 32 && q < 8; ++q) W32(ptr[k] + q * 4, 0);
        if (n > 0)
            for (int fd : fds) for (int k = 0; k < 3; ++k)
                if (ptr[k] && FD_ISSET(Sock((uint32_t)fd), &sets[k])) W32(ptr[k] + (fd / 32) * 4, R32(ptr[k] + (fd / 32) * 4) | (1u << (fd % 32)));
#ifndef MGO1_PLAYER_BUILD
        n = DollFilterMark(ptr[0], filtered, n);
#endif
        return ret(n < 0 ? Fail(FromWsa(selErr)) : n); }
    case 0x1bf5c0:                                                 // abort a thread's blocking socket call
        Log("net: abort of a blocking call (thread info %08x) answered", a0);
        return ret(0);
    case 0x1bdf10: {                                               // shutdown(s, how)
        SOCKET s = Sock(a0);
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        shutdown(s, (int)a1);
        return ret(0); }
    case 0x1bf2e0: {                                               // close(s)
        SOCKET s = Sock(a0);
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        closesocket(s);
        gFd.erase((int)a0); gNb.erase((int)a0); gOsNb.erase((int)a0); gWebAsked.erase((int)a0);
#ifndef MGO1_PLAYER_BUILD
        DollFilterClose((int)a0);
#endif
        if ((int)a0 == gLobbyFd) gLobbyClosed = true;
        Log("net: close(%u)", a0);
        return ret(0); }
    case 0x1c42c0: {                                               // inet_addr(str)
        const char* sname = Str(a0);
        IN_ADDR ia{};
        uint32_t v = inet_pton(AF_INET, sname, &ia) == 1 ? ia.S_un.S_addr : 0xffffffffu;
        return ret((int64_t)(int32_t)v); }
    case 0x1c3f50: {                                               // inet_pton(af, str, dst)
        IN_ADDR ia{};
        if (a0 != 2 || inet_pton(AF_INET, Str(a1), &ia) != 1) return ret(0);
        memcpy(gMem + a2, &ia, 4);
        return ret(1); }
    case 0x1c3db8: {                                               // inet_ntoa(addr)
        static uint32_t buf = 0;
        if (!buf) buf = Alloc(32);
        const uint8_t* p = (const uint8_t*)&a0;
        char b[32]; snprintf(b, sizeof(b), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
        memcpy(gMem + buf, b, strlen(b) + 1);
        return ret(buf); }
    case 0x1c3e18: {                                               // inet_ntop(af, src, dst, size)
        if (a0 != 2 || !a1 || !a2) return ret(0);
        const uint8_t* p = gMem + a1;
        char b[32]; snprintf(b, sizeof(b), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
        if (strlen(b) + 1 > a3) return ret(0);
        memcpy(gMem + a2, b, strlen(b) + 1);
        return ret(a2); }
    // ---- EENet information (what mrdUPnP and the online code ask the stack)
    case 0x1c0500: {                                               // sceEENetGetIfnames(buf, int* n): 16-byte names
        static const char* names[2] = {"lo0", "smap0"};
        for (int k = 0; k < 2; ++k) { memset(gMem + a0 + 16 * k, 0, 16); strcpy((char*)gMem + a0 + 16 * k, names[k]); }
        if (a1) W32(a1, 2);
        return ret(0); }
    case 0x1c05d8: {                                               // sceEENetGetIfinfo(name, code, buf, int* len)
        std::string name = Str(a0);
        bool lo = name == "lo0";
        if (!lo && name != "smap0") return ret(-1);
        HostNet& h = Host();
        uint32_t v = 0; int n = 4; std::string s;
        switch (a1) {
        case 1: s = lo ? "" : "SCE"; break;                                        // VENDOR_NAME
        case 2: s = lo ? "" : "Ethernet (Network Adaptor)"; break;                 // PRODUCT_NAME
        case 3: v = lo ? 1 : 2; break;                                             // IFTYPE: LOOP / ETHER
        case 4: v = lo ? 0x8049 : 0x8043; break;                                   // IFFLAGS: UP RUNNING MULTICAST, LOOPBACK or BROADCAST
        case 5: n = 6; break;                                                      // MACADDR
        case 6: v = 0; break;                                                      // MEDIA
        case 7: v = lo ? 33224 : h.mtu; break;                                     // MTU
        case 8: v = lo ? htonl(0x7f000001) : h.ip; break;                          // ADDR
        case 9: v = lo ? htonl(0xff000000) : h.mask; break;                        // NETMASK
        case 10: v = lo ? htonl(0x7f000001) : (h.ip | ~h.mask); break;             // BROADADDR / DSTADDR
        default: return ret(-1);
        }
        if (a1 == 1 || a1 == 2) { n = (int)s.size() + 1; memcpy(gMem + a2, s.c_str(), n); }
        else if (a1 == 5) memcpy(gMem + a2, lo ? (const uint8_t*)"\0\0\0\0\0\0" : h.mac, 6);
        else W32(a2, v);
        if (a3) W32(a3, (uint32_t)n);
        return ret(0); }
    case 0x1c0e98: {                                               // routing table: 32-byte records {flags, dst, gateway, netmask, ifname[16]}
        HostNet& h = Host();
        uint32_t max = a1 ? R32(a1) : 0, n = 0;
        auto put = [&](uint32_t flags, uint32_t dst, uint32_t gw, uint32_t mask) {
            if (n >= max) return;
            uint32_t e = a0 + 32 * n++;
            memset(gMem + e, 0, 32); W32(e, flags);
            memcpy(gMem + e + 4, &dst, 4); memcpy(gMem + e + 8, &gw, 4); memcpy(gMem + e + 12, &mask, 4);
            strcpy((char*)gMem + e + 16, "smap0");
        };
        if (h.ok && h.gw) put(0x3, 0, h.gw, 0);                    // default: RTF_UP | RTF_GATEWAY
        if (h.ok) put(0x1, h.ip & h.mask, 0, h.mask);              // the local network
        if (a1) W32(a1, n);
        return ret(0); }
    case 0x1c0330: {                                               // sceEENetGetDNSAddr(dns1, dns2): strings of 256
        HostNet& h = Host();
        if (a0) { memset(gMem + a0, 0, 256); strncpy((char*)gMem + a0, h.dns1.c_str(), 255); }
        if (a1) { memset(gMem + a1, 0, 256); strncpy((char*)gMem + a1, h.dns2.c_str(), 255); }
        return ret(0); }
    case 0x1be2e0: {                                               // getsockname(s, sockaddr*, int* len)
        SOCKET s = Sock(a0);
        if (s == INVALID_SOCKET) return ret(Fail(EBADF_));
        sockaddr_in me{}; int ml = sizeof(me);
        if (getsockname(s, (sockaddr*)&me, &ml) != 0) return ret(Fail(FromWsa(WSAGetLastError())));
        if (me.sin_addr.s_addr == 0 && Host().ok) me.sin_addr.s_addr = Host().ip;   // bound to any: the interface's address
        ToPs2(me, a1);
        if (a2) W32(a2, 16);
        return ret(0); }
    case 0x1bcde8:                                                 // free a thread's EENet information
        return ret(0);
    case 0x1c3500: case 0x1c3530:                                  // gethostbyname2(name, af[, timeout, retries]): AF_INET only
        if (a1 != 2) { Up(); W32(gErrnoArea + 0x3fc, (uint32_t)-1); return ret(0); }   // NETDB_INTERNAL
        [[fallthrough]];
    case 0x1c3408: case 0x1c3438: {                                // gethostbyname(name[, timeout, retries]): Windows' resolver
        Up();
        std::string name = Str(a0);
        std::string ask = name;
        auto sh = SimHosts().find(name);
        if (sh != SimHosts().end()) { ask = sh->second; Log("net: redirect: %s resolved as %s", name.c_str(), ask.c_str()); }
        // the DNS server a PS2 set up for these servers asks (Platform::dnsServer), then the answer kept for
        // it (knownHosts), then Windows' resolver
        sockaddr_in found{}; found.sin_family = AF_INET;
        std::string how;
        if (sh == SimHosts().end() && P.dnsServer && *P.dnsServer) {
            if (Blocking([&] { return DnsAsk(P.dnsServer, name, found.sin_addr); })) how = std::string(" (DNS ") + P.dnsServer + ")";
            else if (KnownHost(name, found.sin_addr)) how = std::string(" (DNS ") + P.dnsServer + " did not answer: its known answer)";
        }
        if (how.empty()) {
            addrinfo hints{}; hints.ai_family = AF_INET; addrinfo* res = nullptr;
            int r = Blocking([&] { return getaddrinfo(ask.c_str(), nullptr, &hints, &res); });
            if (r != 0 || !res) { W32(gErrnoArea + 0x3fc, 1); Log("net: gethostbyname(%s) failed", name.c_str()); return ret(0); }
            found.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
            freeaddrinfo(res);
        }
        sockaddr_in* sa = &found;
        uint32_t h = gHostent;                                     // hostent {name, aliases, type, length, list}
        uint32_t nm = h + 0x40, list = h + 0x20, adr = h + 0x30, al = h + 0x28;
        memset(gMem + h, 0, 0x200);
        strncpy((char*)gMem + nm, name.c_str(), 0x1bf);
        W32(h, nm); W32(h + 4, al); W32(h + 8, 2); W32(h + 0xc, 4); W32(h + 0x10, list);
        W32(list, adr); W32(list + 4, 0); W32(al, 0);
        memcpy(gMem + adr, &sa->sin_addr, 4);
        Log("net: gethostbyname(%s) -> %s%s", name.c_str(), Ip(*sa), how.c_str());
        return ret(h); }
    }
    return false;
}

void NativePadRecords();
// the native build's host files (libkernel's file I/O): cdrom0:\X;1 -> <images>\X
uint32_t gSreg[32] = {};                          // the SIF software registers (sceSifGetSreg / SetSreg)
struct HostFile { FILE* f = nullptr; long long base = 0, size = 0, pos = 0; };
std::map<int, HostFile> gHostFiles;
int gHostFdNext = 3;
int HostOpen(const char* path, int mode) {
    std::string p = path;
    const size_t colon = p.find(':');
    const std::string dev = colon == std::string::npos ? "" : p.substr(0, colon);
    HostFile hf;
    DiscSpan span;
    if ((dev == "cdrom0" || dev == "cdrom") && !(mode & 2)) hf.f = DiscOpen(DiscRel(p), span);
    // the European program opens the DNAS module image before its first page (its content unused
    // here: the platform serves the IOP and DNAS never runs); the launcher's copy of the disc has
    // only MODNCD.IPK of MODULES: an empty file stands in for it
    const bool standIn = !hf.f && (dev == "cdrom0" || dev == "cdrom") && !(mode & 2) && _stricmp(DiscRel(p).c_str(), "MODULES\\DNAS300.IMG") == 0;
    if (!hf.f && !standIn) { Log("file %s: not found (mode %#x)", path, mode); return -2; }
    hf.base = span.base; hf.size = hf.f ? span.size : 0;
    const int fd = gHostFdNext++;
    gHostFiles[fd] = hf;
    Log("file %s: open, fd %d%s", path, fd, standIn ? " (not on the disc's copy: an empty file)" : "");
    return fd;
}
int HostClose(int fd) { auto it = gHostFiles.find(fd); if (it == gHostFiles.end()) return -9; if (it->second.f) fclose(it->second.f); gHostFiles.erase(it); return 0; }
int HostRead(int fd, uint32_t buf, int n) {
    auto it = gHostFiles.find(fd);
    if (it == gHostFiles.end()) return -9;
    HostFile& h = it->second;
    if (n <= 0 || buf >= 0x02000000 || h.pos >= h.size || !h.f) return 0;
    if (buf + (uint32_t)n > 0x02000000) n = (int)(0x02000000 - buf);
    n = (int)std::min<long long>(n, h.size - h.pos);
    if (_fseeki64(h.f, h.base + h.pos, SEEK_SET) != 0) return 0;
    const int got = (int)fread(gMem + buf, 1, (size_t)n, h.f);
    h.pos += got;
    return got;
}
int HostLseek(int fd, int off, int whence) {
    auto it = gHostFiles.find(fd);
    if (it == gHostFiles.end()) return -9;
    HostFile& h = it->second;
    const long long to = (whence == 2 ? h.size : whence == 1 ? h.pos : 0) + off;
    if (to < 0) return -22;
    h.pos = to;
    return (int)to;
}
bool Bridge(Ctx& c, uint32_t addr) {
    uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5], a2 = (uint32_t)c.r[6], a3 = (uint32_t)c.r[7];
    // a dedicated host (MGO1_HEADLESS) plays no sound: no PS2 audio stream starts (music BGM_2,
    // voices VOX_2), as the PC engine's build answers (-1: the base's answer when it cannot start
    // one), so none of their reads or decoding runs and the archives are not needed
    if (gNative && ps2::gHeadless && addr == 0x105fb8) {
        if (Once(((uint64_t)10 << 40) | a0)) Log("stream %08x (from %08x): headless, not started", a0, (uint32_t)c.r[31] - 8);
        c.r[2] = (uint64_t)(int64_t)-1;
        return true;
    }
    if (gNative) switch (addr) {
    // the MOD's PC engine services: actors, script, sound, streams, layouts (ntmenu2's
    // addresses; other pages have other code there): the game's own code runs
    case 0x122540: case 0x1222e8: case 0x1224a0:
    case 0x124c10: case 0x124ff8: case 0x124fb8: case 0x124f38: case 0x124d20: case 0x124e10: case 0x124fd8:
    case 0x125018: case 0x125408: case 0x1252d0: case 0x1257f8: case 0x125fb8: case 0x124f00: case 0x124ca0:
    case 0x115310: case 0x115038: case 0x114f88: case 0x114da0: case 0x105fb8:
        return false;
    case 0x43ba28: case 0x442850: case 0x4428b8: case 0x442878: case 0x4423f0: case 0x4425c8: case 0x4425f0:
    case 0x442618: case 0x443658: case 0x443160: case 0x442698:
        return false;
    case 0x123448: NativePadRecords(); c.r[2] = 0; return true;   // the pad reader (libpad): the PC's pads
    // the SIF library's IOP reboot: the platform is the IOP, always ready
    case 0x1fe648: Log("native: IOP reset (%s): the platform serves the IOP", a0 >= 0x100000 && a0 < 0x02000000 ? Str(a0) : "?");
        c.r[2] = 1; return true;                                     // sceSifIopReset
    case 0x1fe5f8: c.r[2] = 1; return true;                          // sceSifIopSync
    // libkernel's file I/O (sceOpen / sceClose / sceRead / sceWrite / sceLseek): the host's files,
    // cdrom0:\X;1 is <images>\X (the disc's files: MODULES\..., the DATs)
    case 0x1fa7f0: c.r[2] = (uint64_t)(int64_t)HostOpen(a0 < 0x02000000 ? Str(a0) : "", (int)a1); return true;
    case 0x1faa80: c.r[2] = (uint64_t)(int64_t)HostClose((int)a0); return true;
    case 0x1fae38: c.r[2] = (uint64_t)(int64_t)HostRead((int)a0, a1, (int)a2); return true;
    case 0x1fb0a8: c.r[2] = (uint64_t)(int64_t)-5; return true;     // sceWrite: the disc is read-only (EIO)
    case 0x1fabf8: c.r[2] = (uint64_t)(int64_t)HostLseek((int)a0, (int)a1, (int)a2); return true;
    // the SIF software registers the IOP modules set: sreg 8 bit 1, the sound driver booted
    case 0x1f8280: c.r[2] = a0 < 32 ? gSreg[a0] | (a0 == 8 ? 2u : 0u) : 0; return true;   // sceSifGetSreg
    case 0x1f8298: if (a0 < 32) gSreg[a0] = a1; c.r[2] = a1; return true;                 // sceSifSetSreg
    case 0x1fe138: {                                                 // sceSifLoadModule(path, arglen, args, result*): the platform's module
        static int id = 0;
        Log("native: IOP module %s loaded (served by the platform)", a0 >= 0x100000 && a0 < 0x02000000 ? Str(a0) : "?");
        if (a3 && a3 < 0x02000000) W32(a3, 0);                       // the module's start result: resident
        c.r[2] = ++id; return true; }
    }
    switch (addr) {
    // ---- actors (base ELF)
    case 0x122540: {                                         // GV_NewActor(size, type)
        uint64_t got = 0;                                   // the base's actor allocator (0x120E98, pool 0), as GV_NewActor calls it
        uint32_t w = RunPs2(V(0x120E98), a0 < 0x40 ? 0x40 : a0, (uint32_t)c.r[5], &got) ? (uint32_t)got : 0;
        if (!w) {
            w = Alloc(a0 < 0x40 ? 0x40 : a0);
            static int told = 0;
            if (told++ < 4) Log("GV_NewActor: pool 0 gave nothing (%#x bytes; %s), platform block %08x", a0, PoolState().c_str(), w);
            else Log("GV_NewActor: pool 0 gave nothing (%#x bytes), platform block %08x", a0, w);
        }
        static uint32_t serial = 0;
        W64(w + 0x18, ((uint64_t)++serial << 32) | (w >> 4));
        uint32_t nm = R32(V(0x23A5F0));
        std::string name = InPageArea(nm) ? Str(nm) : "?";
        gActors.push_back({w, 0, 0, nullptr, true, name, c.r[5], gPoolGen});
        Log("GV_NewActor %s size %#x level %016llx -> %08x", name.c_str(), a0, (unsigned long long)c.r[5], w);
        c.r[2] = w;
        return true; }
    case 0x1222e8: {                                         // GV_SetNamedActor(work, type, act, die)
        {   // a resident actor of the base lives in static memory (the USB keyboard's at 0x27CE70):
            // the base links it without GV_NewActor
            bool known = false;
            for (auto& x : gActors) if (x.work == a0 && x.live) known = true;
            if (!known && a0 >= 0x100000 && a0 < 0x02000000) {
                static uint32_t serialStatic = 0x10000;
                if (!R64(a0 + 0x18)) W64(a0 + 0x18, ((uint64_t)++serialStatic << 32) | (a0 >> 4));
                uint32_t nm = R32(V(0x23A5F0));
                std::string name = (nm >= 0x100000 && nm < PageHi()) ? Str(nm) : "resident";
                gActors.push_back({a0, 0, 0, nullptr, true, name, c.r[5]});
                Log("resident actor %s at %08x (static work)", name.c_str(), a0);
            }
        }
        // the live actor of that work: pool 0 gives a dead actor's block to the next one
        for (size_t k = 0; k < gActors.size(); ++k) if (gActors[k].work == a0 && gActors[k].live) {
            Actor& a = gActors[k];
            a.act = a2; a.die = a3;
            // the PC actor carries the PS2 level: same encoding on both (list = top byte - 1,
            // persistence class = bits 20-21 of the low word), so the PC stage change removes
            // exactly what the PS2 one would. A static actor linked again at the same level (the
            // stage load's 0x103600 does it for the stage's resident actors) keeps its PC actor
            void* pc = nullptr;
            if (a.pc && a.level == c.r[5]) pc = a.pc;
            else {
                if (a.pc) gByPc.erase(a.pc);            // the older PC actor runs nothing any more
                a.level = c.r[5];
                uint64_t level = a.level ? a.level : P.actorLevel;
                pc = P.gvNewActor ? P.gvNewActor(0x40, level) : nullptr;
                if (pc && P.gvSetNamedActor) {
                    P.gvSetNamedActor(pc, level, (void*)&TrampAct, (void*)&TrampDie);
                    a.pc = pc;
                    gByPc[pc] = (int)k;
                }
            }
            // header fields as the base sets them; the result is the actor's handle (+0x18)
            W32(a0 + 4, a2); W32(a0 + 0x14, a3); W32(a0 + 0xc, (uint32_t)a1); W32(a0 + 0x38, R32(V(0x23A5F0)));
            if (!DevEnv("MGO1_QUIET_P2P")) Log("GV_SetNamedActor %s %08x level %016llx act %08x die %08x -> PC actor %p", a.name.c_str(), a0, (unsigned long long)c.r[5], a2, a3, pc);
            c.r[2] = pc ? R64(a0 + 0x18) : 0;
            return true;
        }
        Log("GV_SetNamedActor on an unknown work %08x", a0);
        c.r[2] = 0;
        return true; }
    case 0x1224a0:                                           // GV_DestroyActor(work)
        for (size_t k = 0; k < gActors.size(); ++k) if (gActors[k].work == a0 && gActors[k].live) {
            if (!DevEnv("MGO1_QUIET_P2P")) Log("GV_DestroyActor %s %08x (from %08x)", gActors[k].name.c_str(), a0, (uint32_t)c.r[31] - 8);
            KillActor(k);
            break;
        }
        c.r[2] = 0;
        return true;
    // ---- a PS2 audio stream (0x105FB8(id, ...) -> the stream actor 0x105B90, reading
    // BGM_2 / VOX_2 through cdbios): the PC engine plays the sound, so no stream starts
    // (-1: what the base answers when it cannot start one)
    case 0x105fb8:
        if (Once(((uint64_t)10 << 40) | a0)) Log("stream %08x (from %08x): left to the PC engine's sound, not started", a0, (uint32_t)c.r[31] - 8);
        c.r[2] = (uint64_t)(int64_t)-1;
        return true;
    // ---- newlib's memmove / memcpy (0x1A09A0 / 0x1A0A90): hand-written, memmove branches
    // into memcpy's loops, so neither is a function of its own for the recompiler
    case 0x1a09a0: case 0x1a0a90: {
        const uint32_t d = a0 & 0x01ffffff, s = a1 & 0x01ffffff, n = a2;
        auto special = [](uint32_t p, uint32_t n) {
            return (p < gWinHi && p + n > gWinLo) || (gCursorWin - p < n) || (gStageWin + 4 > p && gStageWin < p + n) ||
                   (p < gWatchHi && p + n > gWatchLo);
        };
        if (!n || d + n > 0x02000000 || s + n > 0x02000000) { if (n) Log("memmove(%08x, %08x, %#x) outside the PS2 space", a0, a1, a2); }
        else if (!special(d, n) && !special(s, n)) memmove(gMem + d, gMem + s, n);
        else if (d <= s) for (uint32_t k = 0; k < n; ++k) W8(a0 + k, R8(a1 + k));
        else for (uint32_t k = n; k-- > 0;) W8(a0 + k, R8(a1 + k));
        c.r[2] = c.r[4];
        return true; }
    // ---- script options (base ELF GCL, onto the PC GCL while the class init runs)
    case 0x124c10: {                                         // GCL_GetOption(ch): the host moves its cursor
        char* p = P.gclGetOption ? P.gclGetOption((char)a0) : nullptr;
        Log("GCL_GetOption('%c') -> %s", (char)a0, p ? "present" : "absent");
        c.r[2] = TokOf((unsigned char*)p);
        return true; }
    case 0x124ff8: {                                         // next string resource INDEX (0x124e68: raw 16-bit, cursor += 3)
        unsigned char* cur = Cur();
        if (!AtParam(cur)) { c.r[2] = 0; return true; }
        int idx = (int16_t)(cur[1] | (cur[2] << 8));
        SetCur(cur + 3);
        Log("GCL string resource index %d", idx);
        c.r[2] = (uint64_t)(int64_t)idx;
        return true; }
    case 0x124fb8: case 0x124f38: case 0x124d20: case 0x124e10: case 0x124fd8: {   // value at the cursor
        long long v = 0; int type = 0;
        unsigned char* cur = Cur();
        if (addr == 0x124e10 && PtrOf(a0)) cur = PtrOf(a0);  // 0x124e10 reads from the pointer it is given
        if (AtParam(cur) && P.gclNextValue) SetCur(P.gclNextValue(cur, &type, &v));
        if ((type & 0x0f) == 7 && v) { c.r[2] = HostStr((const char*)v); Log("GCL value \"%.60s\"", (const char*)v); }
        else if (type == 0x80 && v) { c.r[2] = TokOf((unsigned char*)(intptr_t)v); Log("GCL value: block (token %08x)", (uint32_t)c.r[2]); }
        else { c.r[2] = (uint64_t)(int64_t)(int32_t)v; Log("GCL value %d", (int)v); }
        return true; }
    case 0x125018: {                                         // GCL_GetOptionValue(ch, def)
        char* p = P.gclGetOption ? P.gclGetOption((char)a0) : nullptr;
        unsigned char* cur = (unsigned char*)p;
        if (!AtParam(cur) || !P.gclNextValue) {             // the default, sign-extended as every 32-bit result on the EE
            c.r[2] = (uint64_t)(int64_t)(int32_t)a1; Log("GCL_GetOptionValue('%c') default %d", (char)a0, (int)a1); return true;
        }
        long long v = 0; int type = 0;
        SetCur(P.gclNextValue(cur, &type, &v));
        if ((type & 0x0f) == 7 && v) { c.r[2] = HostStr((const char*)v); Log("GCL_GetOptionValue('%c') -> \"%.60s\"", (char)a0, (const char*)v); }
        else { c.r[2] = (uint64_t)(int64_t)(int32_t)v; Log("GCL_GetOptionValue('%c') -> %d", (char)a0, (int)v); }
        return true; }
    // ---- script services: the rest of the GCL entry points the screens use (4.33)
    case 0x125408: {                                         // string resource (index)
        if (StrSync() && gStr.offs) {
            uint32_t count = (uint32_t)((gStr.data - (const char*)gStr.offs) / 4);
            if (a0 >= count) {
                static int warned = 0;
                if (warned++ < 8) Log("GCL string resource %u out of range (%u entries), called from %08x", a0, count, (uint32_t)c.r[31] - 8);
                c.r[2] = PutStr("");
                return true;
            }
            const unsigned char* e = gStr.offs + 4 * (uint32_t)a0;
            {
                uint32_t off = (e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t)e[3] << 24)) & 0x7fffffff;
                if (off < (uint32_t)(gStr.end - gStr.data)) { c.r[2] = gStr.ps2 + off; return true; }
            }
        }
        static std::map<int, uint32_t> cache;
        auto it = cache.find((int)a0);
        if (it == cache.end()) {
            const char* s = P.gclStrRes ? P.gclStrRes((int)a0) : nullptr;
            uint32_t addr = PutStr(s ? s : "");
            it = cache.emplace((int)a0, addr).first;
            Log("GCL string resource %d -> \"%.60s\"", (int)a0, s ? s : "(none)");
        }
        c.r[2] = it->second;
        return true; }
    case 0x1252d0: {                                         // ExecProc(proc, args*)
        // args: { u16 count; u32 argv } (0x34a8c0 builds it on its stack)
        int args[8] = {0}, n = 0;
        if (a1 && a1 < 0x02000000) {
            n = (int)R16(a1);
            uint32_t argv = R32(a1 + 4);
            if (n > 8) n = 8;
            for (int k = 0; k < n; ++k) args[k] = (int)R32(argv + 4 * k);
        }
        int r = P.gclExecProc ? P.gclExecProc((int)a0, args, n) : 0;
        Log("GCL ExecProc %d (%d args: %d %d %d %d) -> %d", (int)a0, n, args[0], args[1], args[2], args[3], r);
        c.r[2] = (uint64_t)(int64_t)r;
        return true; }
    case 0x1257f8: {                                         // ExecBlock(block, args): a block value is a token
        const unsigned char* blk = PtrOf(a0);
        if (blk && P.gclExecBlock) {
            int args[8] = {0}, n = 0;
            if (a1 && a1 < 0x02000000) {
                n = (int)R16(a1);
                uint32_t argv = R32(a1 + 4);
                if (n > 8) n = 8;
                for (int k = 0; k < n; ++k) args[k] = (int)R32(argv + 4 * k);
            }
            int r = P.gclExecBlock(blk, args, n);
            Log("GCL ExecBlock %08x (%d args) -> %d", a0, n, r);
            c.r[2] = (uint64_t)(int64_t)r;
            return true;
        }
        Log("GCL ExecBlock %08x (not served: no block for this value)", a0);
        c.r[2] = 0;
        return true; }
    case 0x125fb8: {                                         // SetVar(code*, value)
        uint32_t code = a0 ? ((uint32_t)R8(a0) << 24) | ((uint32_t)R8(a0 + 1) << 16) | ((uint32_t)R8(a0 + 2) << 8) | R8(a0 + 3) : 0;
        if (P.gclSetVar) P.gclSetVar(code, (int)a1);
        Log("GCL SetVar %08x = %d", code, (int)a1);
        c.r[2] = a0 + 4;
        return true; }
    case 0x124f00:                                           // next parameter at the cursor (token)
        c.r[2] = AtParam(Cur()) ? TokOf(Cur()) : 0;
        return true;
    case 0x124ca0:                                           // next option: the cursor moves to it
        c.r[2] = 0;
        return true;
    // libc formatting (0x1EFF18 is _vfprintf_r(reent, FILE*, fmt, ap)): the recompiled newlib code runs,
    // so snprintf / sprintf write into their string FILE as on the PS2
    case 0x103d48: {                                         // retail debug print to screen: logged
        const char* t = (a1 && a1 < 0x02000000) ? (const char*)(gMem + a1) : "";
        char line[160]; int n = 0;
        for (; n < 150 && t[n] && t[n] != '\n'; ++n) line[n] = (t[n] >= 32 && t[n] < 127) ? t[n] : '.';
        line[n] = 0;
        if (line[0]) Log("screen print: \"%s\"", line);
        c.r[2] = 0;
        return true; }
    // ---- SIF RPC (sceSifCallRpc): the only server answered is netcnfif, the
    // network configuration interface (client 0x6FB240, argument block 0x6FB280)
    case 0x1f95b0: {                                   // sceSifCheckStatRpc(client): 1 while its request is pending
        bool busy = false;
        for (auto& r : gRpcs) if (r.cd == a0) busy = true;
        c.r[2] = busy ? 1 : 0;
        return true; }
    case 0x1f8978:                                     // sceSifWriteBackDCache: no data cache here
        return true;
    case 0x1f8a28:                                     // sceSifInitRpc: the platform is the IOP
    case 0x1f82c8:                                     // sceSifInitCmd
        c.r[2] = 0;
        return true;
    case 0x1f91d0: {                                   // sceSifBindRpc(client, server id, mode)
        gRpcServerOf[a0] = a1;
        W32(a0 + 0x24, 0x000B0000u | (a1 & 0xffff));  // client->server: bound
        Log("sif rpc: client %08x bound to server %08x", a0, a1);
        c.r[2] = 0;
        return true; }
    case 0x1f93b0: {
        // answered one frame later, as the IOP round trip takes at least a frame: two
        // requesters sharing one argument block rely on it (NewNetConfCheck on mc0 / mc1)
        PendingRpc r;
        r.cd = a0; r.fno = a1; r.send = a3; r.recv = (uint32_t)c.r[9];
        r.endFn = (uint32_t)c.r[11]; r.endParam = R32((uint32_t)c.r[29]);
        r.due = gFrameNo + 1;
        uint32_t ssize = (uint32_t)c.r[8] > 0x1000 ? 0x1000 : (uint32_t)c.r[8];
        if (r.send && r.send + ssize <= 0x02000000) r.args.assign(gMem + r.send, gMem + r.send + ssize);
        gRpcs.push_back(r);
        if (!(a2 & 1)) ServeOne(gRpcs.size() - 1);    // mode 0: synchronous, the answer is there on return
        c.r[2] = 0;
        return true; }
    // ---- DNAS: the SaveMGO production patch of the US overlay (BIN offset 0x90B68,
    // 0x36B1E8 "li v0, 0; jr ra"): the DNAS check returns 0 at once
    case 0x36b1e8:
        Log("dnas: check skipped (SaveMGO production patch)");
        c.r[2] = 0;
        return true;
    // ---- packet cipher: the SaveMGO crypto patch of MGS3_N.ELF (image 0x99F38, "jr ra; nop"): the
    // Blowfish pass over 0x3003 / 0x4310 / 0x4320 / 0x4305 payloads returns at once, registers
    // untouched; the SaveMGO gate (TX55) reads and writes these payloads in clear
    case 0x199f38:
        if (Once((uint64_t)14 << 40)) Log("tx55: payload cipher skipped (SaveMGO crypto patch), payloads in clear");
        return true;
    case 0x1f5b00: c.r[2] = 0x02000000; return true;         // GetMemorySize: a retail PS2's 32 MB
    // ---- the base's network libraries: the EE stack is Winsock, eenetctl.irx is the
    // platform (it raises the interface events itself)
    case 0x1bf938:                                           // sceEENetInit (base stack)
        Log("eenet: base stack init answered (sockets go to the host)");
        c.r[2] = 0; return true;
    case 0x18b8e0: {                                         // eenetctl init: the Ethernet interface attaches
        Log("eenetctl: init answered");
        for (int i = 0; i < 3; ++i) {
            uint32_t nm = R32(V(0x23c010) + 4 * i);
            if (nm && nm < 0x02000000 && !strncmp((const char*)(gMem + nm), "smap", 4)) { QueueNetEvent(nm, i, 1); IfAttach(nm); }
        }
        c.r[2] = 0; return true; }
    // the base stack's memory (its pool is not set up): the platform's blocks
    case 0x1c1318:                                                     // malloc(size)
        c.r[2] = Alloc(a0 ? a0 : 16);
        if (a0 >= 0x8000 && Once(((uint64_t)9 << 40) | (uint32_t)c.r[31]))
            Log("malloc(%#x) -> %08x from %08x", a0, (uint32_t)c.r[2], (uint32_t)c.r[31] - 8);
        return true;
    case 0x1c1350: Free(a0); c.r[2] = 0; return true;                 // free(ptr)
    case 0x1c1388: {                                                   // realloc(ptr, size) or calloc(n, size)
        uint32_t m;
        if (a0 >= 0x100000 && a0 < 0x02000000) {
            uint32_t old = HeapSize(a0);
            m = Alloc(a1 ? a1 : 16);
            if (m) memmove(gMem + m, gMem + a0, old && old < a1 ? old : a1);
            Free(a0);
        } else m = Alloc(a0 * a1 ? a0 * a1 : 16);
        c.r[2] = m; return true; }
    case 0x1c1410: {                                                   // memalign(align, size)
        uint32_t al = a0 >= 16 && a0 <= 0x1000 ? a0 : 16;
        c.r[2] = Alloc(a1 ? a1 : 16, al); return true; }
    case 0x176620: case 0x188928: case 0x189ad8:            // network device set-up (IOP drivers)
        Log("net: device set-up %08x answered", addr);
        c.r[2] = 0; return true;
    case 0x18ddf0:                                           // eenetctl interface up / down (ifname, 0, up)
        Log("eenetctl: interface %s %s", (a0 && a0 < 0x02000000) ? (const char*)(gMem + a0) : "?", a2 ? "up" : "down");
        QueueNetEvent(a0, -1, a2 ? 10 : 11);
        IfState(a0, a2 ? 3 : 5);                             // started (its address is the host's) / stopped
        c.r[2] = 0; return true;
    // ---- EENet (the overlay's EE TCP/IP stack): its own thread is not run,
    // sockets will go to Winsock; start / stop answered as done
    case 0x605e18:                                           // start the stack thread (priority)
        Log("eenet: start (priority %d): answered, sockets go to the host", (int)a0);
        c.r[2] = 0;
        return true;
    case 0x605e80: case 0x606050:                            // stop the stack thread
        Log("eenet: stop");
        c.r[2] = 0;
        return true;
    // ---- sound (base ELF)
    case 0x115310:                                           // SE request (id, handle, ...)
        Log("sound: SE %#x", a0);
        if (P.seRequest) P.seRequest(a0);
        c.r[2] = 1;
        return true;
    case 0x115038:                                           // music / stream request (id)
        Log("sound: music %#x", a0);
        if (P.musicRequest) P.musicRequest(a0);
        c.r[2] = 0;
        return true;
    case 0x114f88:                                           // sound mode (n)
        Log("sound: mode %u", a0);
        c.r[2] = 0;
        return true;
    case 0x114da0:                                           // sound reset
        c.r[2] = 0;
        return true;
    // ---- layouts (the overlay's L2D API, onto the PC L2D)
    case 0x43ba28: {                                         // L2D_LoadLayout(code, chanl, mode, prio)
        unsigned long long h = 0;
        int r = P.l2dLoad ? P.l2dLoad(&h, a0, (int)a1, (int)a2, (int)a3, 1, 0) : -1;
        uint32_t t = 0;
        if (h) { t = kHndLo + (uint32_t)gHandles.size() * 0x100; gHandles.push_back((void*)h); }
        Log("L2D_LoadLayout [%06x] chanl %d mode %d prio %#x -> %d, handle %p token %08x", a0, (int)a1, (int)a2, a3, r, (void*)h, t);
        c.r[2] = t;
        return true; }
    case 0x442850: {                                         // L2D_GetObject(h, code)
        void* h = Handle(a0);
        void* o = (h && P.l2dGetObj) ? P.l2dGetObj(h, a1 & 0xffffff) : nullptr;
        c.r[2] = ObjToken(o);
        if (!o && Once(((uint64_t)5 << 40) | a1)) Log("L2D_GetObject [%06x] on %08x: not found", a1 & 0xffffff, a0);
        return true; }
    case 0x4428b8: {                                         // busy
        void* h = Handle(a0);
        c.r[2] = h ? (uint64_t)(int64_t)(P.l2dBusy ? P.l2dBusy(h) : 0) : (uint64_t)-1;
        return true; }
    case 0x442878: {                                         // is this action running
        void* h = Handle(a0);
        c.r[2] = h ? (uint64_t)(int64_t)(P.l2dRunning ? P.l2dRunning(h, a1) : 0) : (uint64_t)-1;
        return true; }
    case 0x4423f0: case 0x4425c8: case 0x4425f0: case 0x442618: {   // evoke core and its modes
        void* h = Handle(a0);
        int mode = addr == 0x4423f0 ? (int)a3 : addr == 0x4425c8 ? 0 : addr == 0x4425f0 ? 1 : 2;
        long long p = addr == 0x4423f0 ? (long long)a2 : addr == 0x442618 ? (long long)a2 : 0;
        // mode 2 compares the running entries' owner with p: an object token becomes its PC node
        if (p >= kObjLo && p < kObjHi && (size_t)((p - kObjLo) / kSlot) < gObjs.size()) p = (long long)gObjs[(p - kObjLo) / kSlot];
        int r = (h && P.l2dEvoke) ? P.l2dEvoke(h, a1 & 0xffffff, p, mode) : -1;
        {                                                      // an action evoked every frame is logged 8 times
            static std::map<uint64_t, int> seen;
            int& n = seen[((uint64_t)(a1 & 0xffffff) << 32) | ((uint64_t)a0 << 2) | (uint64_t)mode];
            if (++n <= 8) Log("L2D_Evoke [%06x] on %08x mode %d -> %d%s", a1 & 0xffffff, a0, mode, r, n == 8 ? " (again: not logged)" : "");
        }
        c.r[2] = (uint64_t)(int64_t)r;
        return true; }
    case 0x443658: case 0x443160: {                          // layout object text: set / prepare
        void* h = Handle(a0);
        void* obj = ObjPtr(a1);
        const char* t = (a2 && a2 < 0x02000000) ? (const char*)(gMem + a2) : "";
        if (gTextLogs < 400) { ++gTextLogs; Log("text %s on %08x: \"%.70s\" (from %08x, text at %08x)", addr == 0x443658 ? "set" : "prep", a1, t, (uint32_t)c.r[31] - 8, a2); }
        auto fnp = addr == 0x443658 ? P.l2dSetText : P.l2dTextPrep;
        if (h && obj && fnp) fnp(h, obj, t);
        c.r[2] = 0;
        return true; }
    case 0x442698: {                                         // stop that action
        void* h = Handle(a0);
        int r = (h && P.l2dStop) ? P.l2dStop(h, a1 & 0xffffff) : -1;
        Log("L2D_Stop [%06x] on %08x -> %d", a1 & 0xffffff, a0, r);
        c.r[2] = (uint64_t)(int64_t)r;
        return true; }
    }
    return false;
}

// calls logged with their arguments before running (recompiled or not)
std::set<uint32_t> gTrace = {
    0x43df40, 0x43e7e8,
    0x5990e0, 0x5991bc, 0x599248, 0x5992c8, 0x599378, 0x5993b0, 0x5993e8, 0x599498, 0x599548, 0x599608,
    0x599708, 0x5997b8, 0x5998d8, 0x5999f8, 0x599b18, 0x599c38, 0x599d30, 0x599e18, 0x599f48, 0x59a040,
    0x59a140, 0x59a240, 0x59a360, 0x59a6a8, 0x59a750, 0x59a754, 0x59a77c, 0x59a7a0, 0x59a7e0,
    0x384480, 0x3844ac, 0x3844e8, 0x3845dc, 0x3846ac};
std::map<uint64_t, int> gTraceSeen;
// P2P datagrams in the clear, as the reference captures decode them (MGO1_PCAP_REFERENCE
// decoded/*.tsv: t_rel, dir, mode, seq, len, clear_hex, lz, lzss_hex). The EENet send path
// (0x16F420) and receive path (0x16FD58) both hand the plain datagram [seq 2][records] to the
// development kit's packet recorder 0x178928 (session, buffer, length, 0 sent / 1 received;
// it records only with more than 128 MB of memory): before the LZSS compression and the
// encryption on send, after the decryption and the decompression on receive. So clear_hex
// holds the records uncompressed and lz stays empty. MGO1_P2P_TSV=<file>.
// dir: <sender>><receiver> as players P1..P16 (MGO1 player id + 1: the host is P1, joiners
// P2.. in the order the host numbered them; with a Host Only host the 8 players are P2..P9),
// from the game network's player table (0x28B5F4, 16 entries of 0x5C: +0 id, +2 state,
// +4 session, +8 flags: 1 host, 2 local); P? before the table knows the peer, me before it
// knows this instance (a joiner is numbered by the host). Mode B while the session is below
// state 8 (bootstrap keys, the connect exchange), S after.
FILE* gP2p = nullptr;
std::string PlayerName(uint32_t session, bool self) {
    const uint32_t table = V(0x28B5F4);
    for (int k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * k;
        if (!R16(e + 2)) continue;
        if (self ? (R32(e + 8) & 2) != 0 : R32(e + 4) == session) return "P" + std::to_string(R8(e) + 1);
    }
    return self ? "me" : "P?";
}
void P2pLog(bool out, uint32_t session, uint32_t buf, uint32_t len) {
    static bool tried = false;
    static std::set<uint32_t> sessions;
    if (!tried) {
        tried = true;
        if (const char* p = DevEnv("MGO1_P2P_TSV")) {
            if (fopen_s(&gP2p, p, "w") == 0 && gP2p)
                fprintf(gP2p, "# MGO1 P2P datagrams in the clear (ps2world). dir: sender>receiver, P<MGO1 player id + 1>,\n"
                              "# P? a peer not numbered yet, me this instance before the host numbered it\n"
                              "# lz: always empty, clear_hex holds the records uncompressed\n"
                              "t_rel\tdir\tmode\tseq\tlen\tclear_hex\tlz\tlzss_hex\n");
        }
    }
    buf &= 0x01ffffff;                              // the scratchpad (0x70000000) lives at 0 here
    if (!gP2p || len < 2 || buf + len > 0x02000000 || session >= 0x02000000) return;
    static ULONGLONG t0 = 0;
    ULONGLONG now = GetTickCount64(); if (!t0) t0 = now;
    if (sessions.insert(session).second)
        fprintf(gP2p, "# session %08x: peer %u.%u.%u.%u:%u\n", session, R8(session + 0x14), R8(session + 0x15),
                R8(session + 0x16), R8(session + 0x17), (unsigned)R16(session + 0x12));
    static long long lastFrame = -1;                // "# f<frame>" ties the rows to the log's frames
    if (gFrameNo != lastFrame) { fprintf(gP2p, "# f%lld\n", gFrameNo); lastFrame = gFrameNo; }
    const std::string me = PlayerName(0, true), peer = PlayerName(session, false);
    std::vector<uint8_t> clear(gMem + buf, gMem + buf + len);  // a copy: the log-in data in it masked ('*')
    Scrub(clear.data(), clear.size());
    const uint8_t* p = clear.data();
    uint16_t seq = (uint16_t)(p[0] | p[1] << 8);
    size_t bl = len - 2;
    static const char kHex[] = "0123456789abcdef";
    std::string hex(2 * bl, '0');
    for (size_t k = 0; k < bl; ++k) { hex[2 * k] = kHex[p[2 + k] >> 4]; hex[2 * k + 1] = kHex[p[2 + k] & 15]; }
    fprintf(gP2p, "%.3f\t%s>%s\t%c\t%04x\t%u\t%s\t\t\n", (now - t0) / 1000.0, (out ? me : peer).c_str(), (out ? peer : me).c_str(),
            R8(session) >= 8 ? 'S' : 'B', seq, (unsigned)bl, hex.c_str());
    fflush(gP2p);
}
#ifndef MGO1_PLAYER_BUILD
// SIM_WATCH_CALLS="addr:size": the watched words compared at every call, a change logged
// with the call about to run and the one before (the writer is the function running)
void WatchCalls(Ctx& c, uint32_t addr) {
    static uint32_t lo = 0, n = 0;
    static std::vector<uint32_t> last;
    static bool init = false;
    if (!init) {
        init = true;
        if (const char* w = DevEnv("SIM_WATCH_CALLS")) {
            lo = (uint32_t)strtoul(w, nullptr, 16) & 0x01fffffc;
            const char* k = strchr(w, ':');
            n = k ? ((uint32_t)strtoul(k + 1, nullptr, 0) + 3) / 4 : 1;
            if (n > 64) n = 64;
            for (uint32_t i = 0; i < n; ++i) { uint32_t v; memcpy(&v, gMem + lo + 4 * i, 4); last.push_back(v); }
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t v; memcpy(&v, gMem + lo + 4 * i, 4);
        if (v != last[i]) {
            Log("watch: %08x %08x -> %08x before the call of %08x (from %08x); calls before:%s", lo + 4 * i, last[i], v, addr,
                (uint32_t)c.r[31] - 8, RecentCalls(8).c_str());
            last[i] = v;
        }
    }
}
#endif
// ---- native build: MGO1's own boot and main loop ------------------------------------------
// crt0 (0x100024) -> main (0x100608: starts the game thread 0x100498 at priority 0x12, then
// sleeps) -> the game's init and its GV loop (0x1005E0 -> 0x122768). Every function is the
// game's, recompiled; the platform answers the kernel (syscalls, by number when no handler
// knows the stub's address), the IOP modules (SIF RPC) and paces the VBlank. Interrupt
// handlers (VBlank, alarms) run on the platform's thread while no PS2 thread holds the baton;
// a running thread gives it back at a call when one is due (NativeSafePoint).
// the PS2's time in QueryPerformanceCounter ticks: the machine's, or k times slower from SetTimeScale(k)
// on (tests: every instance of a simulation keeps one pace whatever its share of the processors)
double gTimeScale = 1.0;
long long gTimeQ0 = 0, gTimeV0 = 0;
long long QpcNow() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return gTimeV0 + (long long)((double)(t.QuadPart - gTimeQ0) / gTimeScale); }
long long RealQpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
// the network's clock (0x1FF900, the EE timers): the machine's, or k times slower from SetNetTimeScale(k)
// on (tests: slowed with the game once every player has joined; a joiner's handshake meets a host whose
// network runs at the machine's pace)
double gNetScale = 1.0;
long long gNetQ0 = 0, gNetV0 = 0;
long long NetQpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return gNetV0 + (long long)((double)(t.QuadPart - gNetQ0) / gNetScale); }
void NetScaleSet(double k) {
    const long long v = NetQpc();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    gNetQ0 = t.QuadPart; gNetV0 = v; gNetScale = k > 0 ? k : 1.0;
    Log("time: the network's clock %g times slower than the machine's from now on", gNetScale);
}
long long QpcFreq() { static long long f = 0; if (!f) { LARGE_INTEGER x; QueryPerformanceFrequency(&x); f = x.QuadPart; } return f; }
unsigned long long PsMs() { return (unsigned long long)(QpcNow() / (QpcFreq() / 1000)); }   // the PS2's time in ms
void TimeScaleSet(double k) {
    const long long v = QpcNow();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    gTimeQ0 = t.QuadPart; gTimeV0 = v; gTimeScale = k > 0 ? k : 1.0;
    Log("time: the PS2's time %g times slower than the machine's from now on", gTimeScale);
}
// the display's timing as the program set it (SetGsCrt, gPal): NTSC 59.94 fields a second and 15734
// lines, PAL (mode 3, the European program) 50 fields and 15625 lines; the EU program's speeds are PAL's
long long FieldTicks() { return gPal60 ? QpcFreq() / 60 : gPal ? QpcFreq() / 50 : QpcFreq() * 1001 / 60000; }   // one field (PAL at 60: 60)
long long LineTicks(uint32_t lines) { return (long long)((double)lines * (double)QpcFreq() / (gPal ? 15625.0 : 15734.2657)); }   // HSYNC lines
struct Alarm { int id; long long due; uint32_t fn, arg; uint16_t time; bool live; };
std::vector<Alarm> gAlarms;
int gAlarmNext = 0;
long long gNextVBlank = 0, gVBlankEndAt = 0;
HANDLE gKernelWake = nullptr;                                               // set when an interrupt is raised off the PS2 threads
void NetKernelWake() { if (gKernelWake) SetEvent(gKernelWake); }            // netcode: a thread ready again off the kernel (netfix.h)
uint32_t gVSyncFlag = 0, gVSyncCsr = 0;                                     // SetVSyncFlag's words
long long NextEventAt() {
    long long t = gNextVBlank;
    if (gVBlankEndAt && gVBlankEndAt < t) t = gVBlankEndAt;
    for (auto& a : gAlarms) if (a.live && a.due < t) t = a.due;
    for (auto& th : gThreads) if (th.state == 6 && th.wakeAt < t) t = th.wakeAt;
    return t;
}
void NativeSafePoint() {                // on a running PS2 thread, at a call: a due event takes the baton first
    // a loop without calls (a flag polled) still takes an interrupt the GS thread raised meanwhile
    // (the GS thread only: without it an interrupt is raised by the running thread's own DMA, as in v138)
    static const bool atSafePoint = !getenv("MGO1_IRQ_SAFEPOINT") || atoi(getenv("MGO1_IRQ_SAFEPOINT")) != 0;   // tests: 0 off
    if (atSafePoint && ps2::GsThreadOn() && __atomic_load_n(&gIrqPending, __ATOMIC_RELAXED) && gIrqDeliver && !gInIrq && gEie) gIrqDeliver();
    if (!gNative || gInIrq || QpcNow() < NextEventAt()) return;
    EeThread* t = Thr(gCur);
    if (!t || t->state != 1) return;
    const int id = t->id;
    t->state = 2;                       // still first of its priority (its order is kept)
    GiveBack();
    Thr(id)->state = 1;
}
// an interrupt handler (INTC, alarm): interrupts masked, the kernel's stack
void RunHandler(uint32_t fn, std::initializer_list<uint32_t> args) {
    const bool was = gInIrq; const uint32_t eie = gEie;
    gInIrq = true; gEie = 0;
    RunPs2Args(fn, args, nullptr, 50000000);
    gEie = eie; gInIrq = was;
}
void RunIntc(int cause) {
    if (!(gIntcMask >> cause & 1)) return;
    for (size_t k = 0; k < gIntc.size(); ++k)
        if (gIntc[k].live && gIntc[k].cause == cause) { ++gIrqRuns; RunHandler(gIntc[k].fn, {(uint32_t)cause, gIntc[k].arg, 0}); }
}
// the pad reader of the base (0x123448 -> 0x123198: libpad), served: the records it leaves
// at 0x284A90 (+0x40 a port) from the PC's pads; the base's pad frame 0x123A78 runs after it
void NativePadRecords() {
    for (int k = 0; k < 2; ++k) WritePadRecord(k);
}
// the IOP's sound driver as the EE sees it (0x180C98's commands): it reads the EE's command
// queue ([0x28ED90]: a count, then {cmd, a1, a2, a3} from +0x10, 256 entries) and answers in
// its status block ([0x28DD60], 0x40C bytes): +0x80 is 2 while a transfer (0x4C) runs, 1 once
// it is done (0x4D polls it). The commands are logged; the PC sound engine will play them.
void NativeSoundIop() {
    const uint32_t q = R32(V(0x28ED90)) & 0x01ffffff, st = R32(V(0x28DD60)) & 0x01ffffff;
    if (iop::Running()) {                              // the IOP's own driver answers; SIM_SOUND_LOG=1: the requests queued
#ifndef MGO1_PLAYER_BUILD
        static const char* logEnv = DevEnv("SIM_SOUND_LOG");
        static const bool logQ = logEnv != nullptr;
        static const long long logFrom = logEnv ? atoll(logEnv) : 0;   // from that frame
        static const bool startsOnly = logEnv && strchr(logEnv, 's');   // "<frame>s": the effects started only (0x43)
        static uint32_t seen = 0, lines = 0;
        if (logQ && gFrameNo < logFrom && q) seen = R32(q) & 0xff;                  // not logged before
        else if (logQ && q && q + 0x1010 <= 0x02000000) {
            const uint32_t wr = R32(q) & 0xff;
            for (int guard = 0; seen != wr && guard < 256 && lines < (startsOnly ? 20000u : 600u); ++guard) {
                const uint32_t e = q + 0x10 + 16 * seen;
                const uint32_t top = R32(e) >> 24;
                if (startsOnly ? top == 0x43 : (top != 0x08 && top != 0x53)) { Log("sound queue [%u]: %08x %08x %08x %08x", seen, R32(e), R32(e + 4), R32(e + 8), R32(e + 12)); ++lines; }
                seen = (seen + 1) & 0xff;
            }
        }
#endif
        return;
    }
    if (!q || !st || q + 0x1010 > 0x02000000 || st + 0x40c > 0x02000000) return;
    static uint32_t rd = 0;
    const uint32_t wr = R32(q) & 0xff;
    for (int guard = 0; rd != wr && guard < 256; ++guard) {
        const uint32_t e = q + 0x10 + 16 * rd;
        const uint32_t cmd = R32(e), a1 = R32(e + 4), a2 = R32(e + 8), a3 = R32(e + 12);
        static std::map<uint32_t, int> seen;
        if (seen[cmd >> 24]++ < 6) Log("sound iop: command %08x (%08x %08x %08x)", cmd, a1, a2, a3);
        if ((cmd >> 24) == 0x4c) { W32(st + 0x80, 2); W32(st + 0x80, 1); }    // the transfer runs, then is done
        rd = (rd + 1) & 0xff;
    }
}
#ifndef MGO1_PLAYER_BUILD   // the dedicated host's (docs/DEDICATED_HOST.md): commands, kicks, lists, status, Game Report
// MGO1_CMD_FILE=<file>: a dedicated host's commands, one per line, taken every half second (the
// file is renamed to <file>.run, read, then deleted, so a command written meanwhile waits for
// the next turn). The answers go to the log.
//   players          the game network's players: P<id + 1>, state, flags, name
//   kick <P2|name>   the game's own kick (below)
//   quit             the process ends (a service restarts it: a new game)
std::string NetPlayerName(uint32_t id) {                // the slot block's name (0x28C608 + 0xC8 * id, +2)
    std::string n;
    const uint32_t blocks = V(0x28C608);                // (v150-host beta 16: the running program's, as NetPlayers)
    if (id >= 16 || !blocks) return n;
    for (uint32_t k = 0; k < 16; ++k) { const char ch = (char)R8(blocks + 0xC8 * id + 2 + k); if (!ch) break; n += ch; }
    return n;
}
// kick: what a stage's own kick does (0x3F4C30 in n001a, one in each stage): the room message
// 04 <id> on the room channel (the P2P channel 0x62, the records of type 62), then the player's
// kick request (flag 0x400 on its entry, 0x1764A0), after which the player machine (0x175B60)
// ends the player and reports 0x4346 to the lobby. The message is sent from the game's own
// thread, at its next append to a channel (0x174BA8: the game sends from there too) while no
// room message is being built, so it is a record of its own; with no room channel for 3 s (no
// game running), the request alone. Kicks wait in a queue, one sent at a time.
struct KickItem { uint32_t id; long long at; std::string why; };
std::deque<KickItem> gKicks;
struct Kicked { std::string name, why; long long frame; };
std::deque<Kicked> gKicked;                             // the last kicks, for the status file
uint32_t NetEntry(uint32_t id) {                        // the player's entry (0x1738E8)
    const uint32_t table = V(0x28B5F4);                 // (v150-host beta 16: the running program's table, not the US one's)
    if (!table) return 0;
    for (uint32_t k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * k;
        if (R8(e) == id && R16(e + 2)) return e;
    }
    return 0;
}
void KickRequest(uint32_t id, const std::string& why, const char* how) {
    const uint32_t e = NetEntry(id);
    if (!e) { Log("kick P%u: gone before the kick", id + 1); return; }
    W32(e + 8, R32(e + 8) | 0x400);
    const std::string name = NetPlayerName(id);
    Log("kick P%u \"%s\": %s", id + 1, name.c_str(), how);
    gKicked.push_back({name, why, gFrameNo});
    while (gKicked.size() > 10) gKicked.pop_front();
}
bool KickQueued(uint32_t id) {
    for (const auto& k : gKicks) if (k.id == id) return true;
    return false;
}
uint32_t RoomChannel() {                                // the P2P channels: a list from [0x28BCCC], +4 the next, +18 the number
    uint32_t n = 0;
    for (uint32_t k = R32(0x28BCCC); k && k < 0x02000000 && n < 64; k = R32(k + 4), ++n)
        if (R16(k + 18) == 0x62) return k;
    return 0;
}
void KickSend(Ctx& c) {                                 // at the game's call of 0x174BA8 (append)
    const uint32_t ch = RoomChannel();
    if (!ch || R32(ch + 96)) return;                    // no room channel, or a room message being built
    const KickItem it = gKicks.front();
    gKicks.pop_front();
    Ctx k = c;                                          // the game's registers stay as they are
    const uint32_t sp = ((uint32_t)c.r[29] - 0x400) & ~15u, msg = sp + 0x200;   // below the caller's frame
    W8(msg, 4); W8(msg + 1, (uint8_t)it.id);
    k.r[29] = sp; k.r[4] = ch; k.r[5] = msg; k.r[6] = 2;
    Call(k, 0x174ba8);
    k.r[29] = sp; k.r[4] = ch;
    Call(k, 0x174c60);                                  // sent: the record 62 .. 04 <id>
    KickRequest(it.id, it.why, "room message 04 sent, kick requested");
}
// the game's players, for the status file and the commands
struct NetPlayer { uint32_t id, entry, state, flags; std::string name; };
std::vector<NetPlayer> NetPlayers() {
    std::vector<NetPlayer> v;
    // (v150-host beta 16) the running program's table (SessionPlayers' V()): the US addresses read another program's memory
    const uint32_t table = V(0x28B5F4), countAt = V(0x28B5F0);
    if (!table || !countAt) return v;
    const uint32_t count = R32(countAt);
    if (!count || count > 16) return v;
    for (uint32_t k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * k;
        if (R8(e) == 0xFF || !R16(e + 2)) continue;
        v.push_back({R8(e), e, R16(e + 2), R32(e + 8), NetPlayerName(R8(e))});
    }
    return v;
}
void HostCommand(const std::string& line) {
    std::string cmd = line, arg;
    if (const size_t sp = line.find(' '); sp != std::string::npos) { cmd = line.substr(0, sp); arg = line.substr(sp + 1); }
    while (!arg.empty() && (arg.back() == ' ' || arg.back() == '\t')) arg.pop_back();
    if (cmd == "players") {
        std::string t;
        for (const auto& p : NetPlayers()) {
            char b[128];
            snprintf(b, sizeof b, " P%u state %03x flags %x%s name \"%s\";", p.id + 1, p.state, p.flags,
                     (p.flags & 1) ? " (host)" : "", p.name.c_str());
            t += b;
        }
        Log("command players:%s", t.empty() ? " none" : t.c_str());
    } else if (cmd == "kick") {
        for (const auto& p : NetPlayers()) {
            const std::string pn = "P" + std::to_string(p.id + 1);
            if (_stricmp(arg.c_str(), pn.c_str()) && (p.name.empty() || _stricmp(arg.c_str(), p.name.c_str()))) continue;
            if (p.flags & 3) { Log("command kick %s: the host itself, refused", arg.c_str()); return; }
            if (!KickQueued(p.id)) gKicks.push_back({p.id, gFrameNo, "kick"});
            Log("command kick %s: %s \"%s\" (state %03x): with the next room message", arg.c_str(), pn.c_str(), p.name.c_str(), p.state);
            return;
        }
        Log("command kick %s: no such player", arg.c_str());
    } else if (cmd == "quit") {
        Log("command quit: the process ends");
        report::Stop("restart");
        fflush(stdout);
        if (gP2p) fflush(gP2p);
        ExitProcess(0);
    } else if (!cmd.empty()) {
        Log("command \"%s\": not known (players, kick <P2|name>, quit)", line.c_str());
    }
}
// MGO1_HOST_WATCH=<minutes>: a dedicated host that is not hosting ends (a service starts it
// again): no game of its own after that many minutes from the start, or its game gone (no
// host entry in the game network's table) for 30 s
bool gWasHosting = false;
std::string gLoginError;                                   // the log-in screen's last error, until the game is made
long long gLobbyLostAt = -1;                               // (v141 beta 6) the frame the host watch found the lobby closed (-1: open)
// (v141 beta 6) its game made, the lobby's connection closed (the game itself gave it up, or the lobby closed it): the game
// stays alive but out of the lobby's list for good (the 8 October incident: 0x4398 and 0x4392 unanswered, four 0x0005,
// 0x0003, close; the host's entry still there). MGO1_HOST_LOBBY_GRACE seconds (10; 5 to 60) for the game to open a new one
// (its 0x4310 on a new descriptor clears gLobbyClosed), then the process ends with 6: the service starts a new one
int LobbyGraceSeconds() {
    static const int g = [] { const char* v = DevEnv("MGO1_HOST_LOBBY_GRACE"); const int n = v ? atoi(v) : 10; return n < 5 ? 5 : n > 60 ? 60 : n; }();
    return g;
}
void HostWatch() {
    static const char* w = DevEnv("MGO1_HOST_WATCH");
    if (!w || gFrameNo % 60) return;
    static long long since = -1, lost = -1;
    if (since < 0) since = gFrameNo;
    bool host = false;
    for (const auto& p : NetPlayers()) if ((p.flags & 3) == 3) host = true;
    if (host && !gWasHosting) Log("host watch: hosting (frame %lld)", gFrameNo);
    net::gHostingNow = host;
    const bool lobbyDown = net::gLobbyFd >= 0 && net::gLobbyClosed;
    if (host && lobbyDown) {
        if (gLobbyLostAt < 0) {
            gLobbyLostAt = gFrameNo;
            const auto ago = [](const net::LobbyLast& l) { return l.frame < 0 ? -1LL : (gFrameNo - l.frame) / 60; };
            Log("host watch: the lobby's connection is closed (descriptor %d, frame %lld; its last data %lld s ago; last sent %04x seq %u %lld s ago, "
                "last received %04x seq %u %lld s ago; host entry %s): a new process in %d s unless the game opens a new one",
                net::gLobbyFd, gFrameNo, net::gLobbyRx < 0 ? -1LL : (gFrameNo - net::gLobbyRx) / 60, net::gLobbyLastOut.cmd, net::gLobbyLastOut.seq,
                ago(net::gLobbyLastOut), net::gLobbyLastIn.cmd, net::gLobbyLastIn.seq, ago(net::gLobbyLastIn), host ? "still there" : "gone",
                LobbyGraceSeconds());
        }
        if (gFrameNo - gLobbyLostAt >= (long long)LobbyGraceSeconds() * 60) {
            Log("host watch: the lobby's connection still closed after %d s: the process ends (6), the service starts a new one", LobbyGraceSeconds());
            report::Stop("lobby-lost");
            fflush(stdout); if (gP2p) fflush(gP2p); ExitProcess(6);
        }
    } else if (gLobbyLostAt >= 0) {
        if (!lobbyDown) Log("host watch: the lobby's connection is open again (descriptor %d) after %lld s: the process goes on", net::gLobbyFd, (gFrameNo - gLobbyLostAt) / 60);
        else Log("host watch: the host entry is gone too (the lobby closed %lld s ago): the game's end watched", (gFrameNo - gLobbyLostAt) / 60);
        gLobbyLostAt = -1;
    }
    if (host) { gWasHosting = true; lost = -1; return; }
    if (!gWasHosting) {
        if (gFrameNo - since > (long long)atoi(w) * 3600) {
            Log("host watch: no game of its own after %s minutes: the process ends", w);
            report::Stop("no-game");
            fflush(stdout); ExitProcess(3);
        }
        return;
    }
    if (lost < 0) { lost = gFrameNo; Log("host watch: the host entry is gone (frame %lld)", gFrameNo); }
    if (gFrameNo - lost > 30 * 60) {
        Log("host watch: no game for 30 s: the process ends");
        report::Stop("game-lost");
        fflush(stdout); if (gP2p) fflush(gP2p); ExitProcess(2);
    }
}
// MGO1_STATUS_FILE=<file>: a dedicated host's state for its control program (host/mgo1host-agent),
// JSON, rewritten every second: hosting, the lobby's link, the game (name, rotation, round, the
// most players), the players (P, name, round trip in ms, team, state, this game's kills /
// deaths / points), the bans and the last kicks. The round trip is the P2P session's own
// measure (the session at the entry's +4, +0x90: 0x1701A0, the host's link quality 0x175998
// reads it); team, round and stats are what the host reports to the lobby (0x4344, 0x4392,
// 0x4390), the rotation what it sent in its 0x4310.
struct HostGame { bool made = false; std::string name; uint8_t rule[15]{}, map[15]{}; int count = 0, maxPlayers = 0, round = -1; long long madeAt = -1; };
HostGame gGame;
std::map<uint32_t, int> gTeamOf;                        // TX55 user id -> team (0 red, 1 blue, 0xFE none)
struct Score { int kills = 0, deaths = 0, points = 0; };
std::map<uint32_t, Score> gScoreOf;                     // TX55 user id -> this game's totals
uint32_t VarBlock(uint32_t p) {                         // the variables of P (0x178658: [0x28D528 + 4P] + 8)
    const uint32_t d = R32(0x28D528 + 4 * p);
    return d && d < 0x02000000 ? R32(d + 8) : 0;
}
uint32_t UserIdOf(uint32_t id) {                        // the TX55 user id (variable 160 of P)
    const uint32_t b = VarBlock(id + 1);
    return b && b < 0x02000000 ? R32(b + 160) : 0;
}
int RttOf(const NetPlayer& p) {                         // ms, -1 without a session (the host)
    if (p.flags & 2) return -1;
    const uint32_t s = R32(p.entry + 4);
    if (!s || s >= 0x02000000) return -1;
    return (int16_t)R16(s + 144);
}
void JsonStr(std::string& o, const std::string& v) {
    o += '"';
    for (unsigned char ch : v) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch < 0x20 || ch > 0x7e) { char b[8]; snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += (char)ch;
    }
    o += '"';
}
// The player lists, read again when their file changes:
//   MGO1_BANS_FILE=<file>: the blacklist, names (one a line, # comments) kicked as soon as they join;
//   MGO1_WHITELIST_FILE=<file>: the whitelist, names and a line "only = on|off" (on: only these names may play).
struct NameList { std::vector<std::string> names; bool only = false; FILETIME last{}; };
NameList gBlack, gWhite;
void ListRead(const char* env, NameList& l, const char* what) {
    const char* path = DevEnv(env);
    if (!path) return;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) {
        if (!l.names.empty() || l.only) Log("%s: none (no file)", what);
        l.names.clear(); l.only = false; l.last = {};
        return;
    }
    if (!CompareFileTime(&a.ftLastWriteTime, &l.last)) return;
    l.last = a.ftLastWriteTime;
    l.names.clear(); l.only = false;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        std::string n = line;
        while (!n.empty() && isspace((unsigned char)n.back())) n.pop_back();
        size_t a0 = 0; while (a0 < n.size() && isspace((unsigned char)n[a0])) ++a0;
        n = n.substr(a0);
        if (n.empty() || n[0] == '#') continue;
        for (auto& ch : n) ch = (char)toupper((unsigned char)ch);
        if (n.compare(0, 4, "ONLY") == 0 && n.find('=') != std::string::npos) {
            std::string v = n.substr(n.find('=') + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            l.only = v == "ON" || v == "YES" || v == "1" || v == "TRUE";
            continue;
        }
        l.names.push_back(n);
    }
    fclose(f);
    Log("%s: %zu name(s)%s", what, l.names.size(), l.only ? ", only these may play" : "");
}
void ListsApply(const std::vector<NetPlayer>& ps) {
    static std::map<uint32_t, long long> recent;        // a player kicked for a list, not again for 10 s
    for (const auto& p : ps) {
        if (p.flags & 3) continue;
        std::string u = p.name;
        for (auto& ch : u) ch = (char)toupper((unsigned char)ch);
        if (u.empty()) continue;
        const bool black = std::find(gBlack.names.begin(), gBlack.names.end(), u) != gBlack.names.end();
        const bool notWhite = gWhite.only && std::find(gWhite.names.begin(), gWhite.names.end(), u) == gWhite.names.end();
        if (!black && !notWhite) continue;
        auto r = recent.find(p.id);
        if (r != recent.end() && gFrameNo - r->second < 600) continue;
        recent[p.id] = gFrameNo;
        if (!KickQueued(p.id)) gKicks.push_back({p.id, gFrameNo, black ? "black" : "white"});
        Log("lists: P%u \"%s\" is %s: kicked", p.id + 1, p.name.c_str(), black ? "on the blacklist" : "not on the whitelist");
    }
}
// (v141 beta 6) the server's dolls' names (MGO1_DOLL_NAMES: the doll manager's list, upper case), read again every 10 s
const std::vector<std::string>& HostDollNames() {
    static std::vector<std::string> names;
    static long long readAt = -100000;
    if (gFrameNo - readAt < 600) return names;
    readAt = gFrameNo;
    const char* path = DevEnv("MGO1_DOLL_NAMES");
    if (!path) return names;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return names;
    names.clear();
    char line[64];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        for (auto& ch : l) ch = (char)toupper((unsigned char)ch);
        if (!l.empty()) names.push_back(l);
    }
    fclose(f);
    return names;
}
// (v141 beta 6) a player's kind, as the dolls count them (doll.cpp PresenceNow): 0 real, 1 the dedicated host (its entry's
// host flag), 2 a doll (an accountless doll's user id in MGO1_DOLL_IDS: *dollNo its number; a name of MGO1_DOLL_NAMES), 3
// connecting (its entry's states below 0x212: its connection made, 0x202 to 0x211, or ended, 0x204 0x205; or no name yet: the
// presence bench's host counted each doll as a real player for 2 s while it connected)
int PlayerKind(const NetPlayer& p, int* dollNo) {
    *dollNo = 0;
    if (p.flags & 1) return 1;
    const uint32_t uid = UserIdOf(p.id);
    if (vdoll::IsId(uid)) return 2;                          // a virtual doll (MGO1_VDOLL_IDS)
    if (const auto* ids = net::DollIds(); ids && ids->In(uid)) { *dollNo = (int)(uid - ids->first) + 1; return 2; }
    std::string u = p.name;
    for (auto& ch : u) ch = (char)toupper((unsigned char)ch);
    if (u.empty() || (p.state >= 0x200 && p.state < 0x212)) return 3;
    for (const auto& n : HostDollNames()) if (n == u) return 2;
    return 0;
}
// (v141 beta 7) a doll in the game's session now (who: its slot, an accountless doll's number and user id; never a name)
bool UnrankedDollsNow(std::string* who) {
    for (const auto& p : NetPlayers()) {
        int dollNo = 0;
        if (PlayerKind(p, &dollNo) != 2) continue;
        if (who) {
            char b[96];
            const int vd = vdoll::NumberOfId(UserIdOf(p.id));
            if (vd) snprintf(b, sizeof b, "P%u, virtual doll %d, user id %#x", p.id + 1, vd, UserIdOf(p.id));
            else if (dollNo) snprintf(b, sizeof b, "P%u, accountless doll %d, user id %#x", p.id + 1, dollNo, UserIdOf(p.id));
            else snprintf(b, sizeof b, "P%u, a doll with an account", p.id + 1);
            *who = b;
        }
        return true;
    }
    return false;
}
// (virtual dolls) the session's players as they see them: each one's kind (PlayerKind), team (its variable set) and user id
int VdollSeenPlayers(vdoll::Seen* out, int max) {
    int n = 0;
    for (const auto& p : NetPlayers()) {
        if (n >= max) break;
        vdoll::Seen s;
        int dollNo = 0;
        s.id = (int)p.id; s.kind = PlayerKind(p, &dollNo); s.uid = UserIdOf(p.id);
        int t = 0xFF;
        if (PlayerInfo((int)p.id, &t, nullptr)) s.team = t;
        out[n++] = s;
    }
    return n;
}
// the players the host's game takes besides the host: the room's maximum (+0x22, the config key the game's accept checks),
// else the one its 0x4310 told
int VdollSeats() {
    uint8_t v[0x24] = {};
    const int most = RoomVariables(v, sizeof v) && v[0x22] >= 2 && v[0x22] <= 16 ? v[0x22] : gGame.maxPlayers;
    static int said = -1;
    if (most != said && most > 0) { said = most; Log("vdoll: the host's game takes %d players, the host one of them (the room's %d, its 0x4310's %d)", most, v[0x22], gGame.maxPlayers); }
    return most > 1 ? most - 1 : 0;
}
// (v141 beta 7) auto: the first doll seen in the game's session makes the game unranked to its end (every second, and at each
// 0x4390's making)
void UnrankedWatch() {
    if (net::UnrankedMode() != net::Unranked::Auto || net::gRoundDolls) return;
    std::string who;
    if (!UnrankedDollsNow(&who)) return;
    net::gRoundDolls = true;
    Log("unranked: a doll in the game (%s): its statistics go unranked to the game's end", who.c_str());
}
namespace net {
// (auto) a new round (its 0x4392 made, the last round's 0x4390 sent before it): the game unranked already stays so; else the dolls
// in the session now decide
void UnrankedRound() {
    if (UnrankedMode() != Unranked::Auto) return;
    static bool said = false;
    if (gRoundDolls) {
        if (!said) { said = true; Log("unranked: a new round (0x4392 sent): this game had dolls, its later rounds stay unranked (the VS rating carries over)"); }
        return;
    }
    std::string who;
    const bool dolls = UnrankedDollsNow(&who);
    gRoundDolls = dolls;
    if (dolls) Log("unranked: a new round (0x4392 sent), a doll in the game (%s): its statistics go unranked", who.c_str());
    else Log("unranked: a new round (0x4392 sent), no doll in the game: ranked unless one comes");
}
}  // namespace net
void StatusWrite(const std::vector<NetPlayer>& ps) {
    static const char* path = DevEnv("MGO1_STATUS_FILE");
    if (!path) return;
    bool host = false;
    for (const auto& p : ps) if ((p.flags & 3) == 3) host = true;
    std::string o;
    char b[256];
    // (v141 beta 6) lobby_closed, lobby_fd, lobby_rx_age (lobby_rx's seconds, kept), lobby_lost_for (the host watch's grace: s, -1)
    const long long rxAge = net::gLobbyRx < 0 ? -1LL : (gFrameNo - net::gLobbyRx) / 60;
    snprintf(b, sizeof b, "{\"v\":1,\"frame\":%lld,\"state\":\"%s\",\"lobby\":%d,\"lobby_rx\":%lld,\"lobby_rx_age\":%lld,\"lobby_closed\":%d,"
             "\"lobby_fd\":%d,\"lobby_lost_for\":%lld,", gFrameNo,
             host ? "hosting" : gWasHosting ? "lost" : "starting", net::gLobbyFd >= 0 && !net::gLobbyClosed ? 1 : 0, rxAge, rxAge,
             net::gLobbyFd >= 0 && net::gLobbyClosed ? 1 : 0, net::gLobbyFd, gLobbyLostAt < 0 ? -1LL : (gFrameNo - gLobbyLostAt) / 60);
    o += b;
    if (!host && !gWasHosting && !gLoginError.empty()) { o += "\"error\":"; JsonStr(o, gLoginError); o += ','; }
    o += "\"game\":{\"name\":"; JsonStr(o, gGame.name);
    o += ",\"rules\":[";
    for (int i = 0; i < gGame.count; ++i) { snprintf(b, sizeof b, "%s[%u,%u]", i ? "," : "", gGame.rule[i], gGame.map[i]); o += b; }
    snprintf(b, sizeof b, "],\"round\":%d,\"max\":%d,\"age\":%lld},\"players\":[", gGame.round, gGame.maxPlayers,
             gGame.madeAt < 0 ? -1LL : (gFrameNo - gGame.madeAt) / 60);
    o += b;
    bool first = true;
    int kinds[4] = {0, 0, 0, 0}, vdolls = 0;
    for (const auto& p : ps) {
        const uint32_t uid = UserIdOf(p.id);
        const auto t = gTeamOf.find(uid);
        const auto sc = gScoreOf.find(uid);
        int dollNo = 0;
        const int kind = PlayerKind(p, &dollNo);
        ++kinds[kind];
        snprintf(b, sizeof b, "%s{\"p\":%u,\"name\":", first ? "" : ",", p.id + 1);
        o += b; JsonStr(o, p.name);
        if (vdoll::IsId(uid)) ++vdolls;
        snprintf(b, sizeof b, ",\"host\":%d,\"kind\":\"%s\",\"doll\":%d,\"vdoll\":%d,\"state\":%u,\"team\":%d,\"ping\":%d,\"k\":%d,\"d\":%d,\"pts\":%d,\"kick\":%d}",
                 (p.flags & 1) ? 1 : 0, kind == 1 ? "host" : kind == 2 ? "doll" : kind == 3 ? "connecting" : "real", dollNo, vdoll::NumberOfId(uid), p.state,
                 t == gTeamOf.end() ? -1 : t->second, RttOf(p),
                 sc == gScoreOf.end() ? 0 : sc->second.kills, sc == gScoreOf.end() ? 0 : sc->second.deaths,
                 sc == gScoreOf.end() ? 0 : sc->second.points, (p.flags & 0x400) ? 1 : 0);
        o += b; first = false;
    }
    snprintf(b, sizeof b, "],\"players_real\":%d,\"players_dolls\":%d,\"players_host\":%d,\"players_connecting\":%d,\"players_total_network\":%zu",
             kinds[0], kinds[2], kinds[1], kinds[3], ps.size());
    o += b;
    // (virtual dolls) how many are set, how many are in the game now, each one's state (docs/VIRTUAL_DOLL.md)
    if (vdoll::On()) { snprintf(b, sizeof b, ",\"vdolls_set\":%d,\"players_vdolls\":%d,\"vdolls\":", vdoll::Wanted(), vdolls); o += b; o += vdoll::StatusJson(); }
    // (v141 beta 7) SaveMGO's rankings: the mode, the game unranked now (1), the reports sent so
    snprintf(b, sizeof b, ",\"unranked_mode\":\"%s\",\"unranked\":%d,\"unranked_sent\":%u", net::UnrankedName(net::UnrankedMode()),
             net::UnrankedNow() ? 1 : 0, (unsigned)net::gUnrankedSent);
    o += b;
    // ((v141 beta 9) the last "pace:" line (the doll manager's pace guard): its number, VBlanks a second, due, the share of a core)
    { int pl = 0; double pr = 0, pd = 0, pc = 0;
      if (LastPace(&pl, &pr, &pd, &pc)) { snprintf(b, sizeof b, ",\"pace\":{\"n\":%d,\"rate\":%.2f,\"due\":%.2f,\"cpu\":%.0f}", pl, pr, pd, pc); o += b; } }
    for (const char* key : {",\"bans\":[", "],\"black\":["}) {
        o += key;
        for (size_t i = 0; i < gBlack.names.size(); ++i) { if (i) o += ','; JsonStr(o, gBlack.names[i]); }
    }
    o += "],\"white\":[";
    for (size_t i = 0; i < gWhite.names.size(); ++i) { if (i) o += ','; JsonStr(o, gWhite.names[i]); }
    o += gWhite.only ? "],\"white_only\":1,\"kicked\":[" : "],\"white_only\":0,\"kicked\":[";
    for (size_t i = 0; i < gKicked.size(); ++i) {
        if (i) o += ',';
        o += "{\"name\":"; JsonStr(o, gKicked[i].name); o += ",\"why\":"; JsonStr(o, gKicked[i].why);
        snprintf(b, sizeof b, ",\"ago\":%lld}", (gFrameNo - gKicked[i].frame) / 60); o += b;
    }
    o += "]}\n";
    const std::string tmp = std::string(path) + ".tmp";
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return;
    fwrite(o.data(), 1, o.size(), f);
    fclose(f);
    MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING);
}
// the host's reports to the lobby, seen at their builders (0x194C40 0x4310, 0x195D90 0x4344,
// 0x1985D0 0x4392, 0x196220 0x4390)
void HostReports(Ctx& c, uint32_t addr) {
    if (addr == 0x194c40) {
        const uint32_t st = (uint32_t)c.r[5];
        if (!st || st + 700 > 0x02000000) return;
        gGame = HostGame{};
        gGame.made = true; gGame.madeAt = gFrameNo;
        for (int i = 0; i < 15 && R8(st + 4 + i); ++i) gGame.name += (char)R8(st + 4 + i);
        for (int i = 0; i < 15; ++i) {
            if (!R8(st + 580 + i)) break;
            gGame.rule[i] = R8(st + 564 + i); gGame.map[i] = R8(st + 580 + i); gGame.count = i + 1;
        }
        gGame.maxPlayers = R8(st + 599);
        gGame.round = 0;                                // the first of the rotation (0x4392 comes with the next ones)
        // (v150-host beta 16) the teams' units and the weapons setting of the game as it is made, for the virtual dolls:
        // the game's own settings at its creation, whatever set them (a settings file, the account's last ones)
        gHostUnits[0] = R8(st + 596); gHostUnits[1] = R8(st + 597); gHostWeapons = R8(st + 598);
        gTeamOf.clear(); gScoreOf.clear();
    } else if (addr == 0x195d90) {
        gTeamOf[(uint32_t)c.r[5]] = (int)(uint8_t)c.r[6];
    } else if (addr == 0x1985d0) {
        gGame.round = (int)(uint8_t)c.r[5];
        net::UnrankedRound();                            // (v141 beta 7)
    } else if (addr == 0x196220) {
        UnrankedWatch();                                 // (v141 beta 7) its report about to leave: the session's dolls first
        const uint32_t st = (uint32_t)c.r[6];
        if (!st || st + 32 > 0x02000000) return;
        Score& sc = gScoreOf[(uint32_t)c.r[5]];
        sc.kills += (int32_t)R32(st); sc.deaths += (int32_t)R32(st + 4); sc.points += (int32_t)R32(st + 24);
    }
}
// ---- Game Report ------------------------------------------------------------------------------------
// MGO1_EVENTS_DIR=<dir>: what happens in the game, one JSON object a line, appended to
// <dir>\<UTC date>.jsonl and flushed at once (the host's control program reads them):
//   {"t": ms since 1970 UTC, "k": kind, fields..., "pk": [packet or message details]}
// Sources: the lobby's frames (the host's reports 0x43xx and their answers, decoded on the
// wire), the messages the game sends its HUD's actors (0x1220E0 with codes 0x75xxxx: a kill
// 0x751005 {killer, victim}, a headshot kill 0x751010, a stun 0x751006, a join 0x75100F, a
// round's end 0x750004, a restart 0x760121, the first grab of the KEROTAN 0x750014...), the
// players' statistics (variable set P + 0x14: 32 counters the host keeps for its 0x4390
// reports, kills 0x14, deaths 0x16, stuns 0x18 / 0x1A, Snake kills 0x1C, points 0x1E,
// suicides 0x20, self stuns 0x22, team kills 0x24, team stuns 0x26, rounds 0x28 / 0x2A,
// KEROTANs 0x2C / 0x2E / 0x30, radio 0x32, chat 0x34, CQC 0x36 / 0x38, headshots 0x3A / 0x3C,
// team wins 0x3E, Scorpion kills 0x40, knife kills 0x42, food 0x44, rolls 0x46, goggles
// 0x48), the room's variables (set 0: +0 the phase, 1 before a round, 2 playing, 4 over,
// 5 the next one coming; +0x36 the round; +0x3C its seconds left) and the screens' texts.
namespace report {
bool On() { static const bool on = DevEnv("MGO1_EVENTS_DIR") != nullptr; return on; }
long long NowMs() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)(t / 10000ULL) - 11644473600000LL;
}
void Json(std::string& o, const std::string& v) {       // a JSON string (the game's names are ASCII)
    o += '"';
    for (unsigned char ch : v) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch < 0x20 || ch > 0x7e) { char b[8]; snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += (char)ch;
    }
    o += '"';
}
struct Ev {
    std::string k; std::vector<std::pair<std::string, std::string>> f;   // fields: key, JSON value
    std::vector<std::string> pk;                                          // details: JSON objects
    long long t = 0;                                                      // when it happened (0: when written)
    Ev(const char* kind) : k(kind) {}
    Ev& S(const char* key, const std::string& v) { std::string j; Json(j, v); f.push_back({key, j}); return *this; }
    Ev& N(const char* key, long long v) { f.push_back({key, std::to_string(v)}); return *this; }
    Ev& P(const std::string& d) { pk.push_back(d); return *this; }
};
// the lines go to the day's file in time order: an event finished some frames after it happened keeps
// its moment, the others wait a second in gOut
std::vector<std::pair<long long, std::string>> gOut;
void Flush(bool all) {
    if (gOut.empty()) return;
    const long long now = NowMs();
    std::stable_sort(gOut.begin(), gOut.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    // a day's lines in one open and one write (v141 beta 4: a file opened a line, 0.5 to 0.8 ms each under Wine, 20 at a
    // round's end in the same frame)
    size_t k = 0;
    std::string path, buf;
    const auto put = [&] {
        FILE* f = nullptr;
        if (!buf.empty() && fopen_s(&f, path.c_str(), "ab") == 0 && f) { fwrite(buf.data(), 1, buf.size(), f); fclose(f); }
        buf.clear();
    };
    for (; k < gOut.size() && (all || gOut[k].first <= now - 1000); ++k) {
        const time_t secs = (time_t)(gOut[k].first / 1000);
        struct tm u; gmtime_s(&u, &secs);
        char name[32]; strftime(name, sizeof name, "%Y-%m-%d.jsonl", &u);
        const std::string p = std::string(DevEnv("MGO1_EVENTS_DIR")) + "\\" + name;
        if (p != path) { put(); path = p; }
        buf += gOut[k].second;
    }
    put();
    gOut.erase(gOut.begin(), gOut.begin() + k);
}
void Write(const Ev& e) {
    if (!On()) return;
    const long long t = e.t ? e.t : NowMs();
    std::string o = "{\"t\":" + std::to_string(t) + ",\"k\":";
    Json(o, e.k);
    for (const auto& kv : e.f) { o += ",\""; o += kv.first; o += "\":"; o += kv.second; }
    if (!e.pk.empty()) {
        o += ",\"pk\":[";
        for (size_t i = 0; i < e.pk.size(); ++i) { if (i) o += ','; o += e.pk[i]; }
        o += ']';
    }
    o += "}\n";
    o = Redact(o);                                          // never the log-in data (the game's screens can show it)
    Log("report: %s", o.substr(0, o.size() - 1).c_str());
    gOut.push_back({t, o});
}
std::string Hex(const uint8_t* p, size_t n) { std::string s; char b[4]; for (size_t i = 0; i < n; ++i) { snprintf(b, sizeof b, "%02x", p[i]); s += b; } return s; }
// the players: name, team and account by game id (the slot table, the variable sets)
uint32_t SetData(uint32_t p) {                              // the data of variable set p
    const uint32_t e = R32(0x28D528 + 4 * p);
    if (!e || e >= 0x02000000) return 0;
    const uint32_t d = R32(e + 8);
    return d && d < 0x02000000 ? d : 0;
}
std::string NameOf(int id) {
    if (id < 0 || id >= 16) return "";
    std::string n = NetPlayerName((uint32_t)id);
    if (n.empty()) if (const uint32_t d = SetData((uint32_t)id + 1)) for (uint32_t k = 0; k < 16 && R8(d + 2 + k); ++k) n += (char)R8(d + 2 + k);
    return n;
}
std::map<uint32_t, std::string> gNameOfUser;                // TX55 user id -> name (kept for the leaves)
// the teams: each player's team in its variable set (+0x001: 0 red, 1 blue, 0xFF none yet, 0xFE the
// host); an event when a player takes a team in a team rule (a moment later: TEAM SELECT may change it),
// when one changes team in the round, and at each round's start (the teams carried over)
int gTeamVs[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
int gTeamSaid[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // reported this round
long long gTeamSaidAt[16];                                  // its frame: a team said less than 30 s ago is not said again
std::map<int, std::pair<std::string, long long>> gTeamPk;  // player id -> its last 0x4344 frame (detail), when
std::string NameOfUser(uint32_t uid) {
    for (int id = 0; id < 16; ++id) {
        const uint32_t d = SetData((uint32_t)id + 1);
        if (d && R32(d + 0xa0) == uid) { const std::string n = NameOf(id); if (!n.empty()) { gNameOfUser[uid] = n; return n; } }
    }
    const auto it = gNameOfUser.find(uid);
    return it == gNameOfUser.end() ? "" : it->second;
}
const char* TeamKey(int t) { return t == 0 ? "red" : t == 1 ? "blue" : "none"; }
int CurRule() {                                              // the rule of the rotation's entry being played (-1 unknown)
    const uint32_t d = SetData(0);
    const int entry = d ? R8(d + 0x36) : -1;
    return entry >= 0 && entry < gGame.count ? gGame.rule[entry] : -1;
}
// the players' statistics, a frame after another: what changed, by player id
struct Stats { uint16_t v[32]; bool have = false; };
Stats gStats[16];
struct Delta { int d[32] = {0}; bool any = false; };
Delta gDelta[16];                                           // changes since they were last claimed
long long gDeltaFrame[16];
void StatsTick() {
    for (int id = 0; id < 16; ++id) {
        const uint32_t d = SetData((uint32_t)id + 1);
        if (!d) { gStats[id].have = false; continue; }
        Stats now; now.have = true;
        for (int i = 0; i < 32; ++i) now.v[i] = R16(d + 0x14 + 2 * i);
        if (gStats[id].have) {
            bool reset = true;                              // a new round zeroes every counter: not events
            for (int i = 0; i < 32; ++i) if (now.v[i]) reset = false;
            if (!reset)
                for (int i = 0; i < 32; ++i) if (now.v[i] != gStats[id].v[i]) {
                    const int dv = (int)now.v[i] - (int)gStats[id].v[i];
                    if (dv > 0 && dv < 100) { gDelta[id].d[i] += dv; gDelta[id].any = true; gDeltaFrame[id] = gFrameNo; }
                }
        }
        gStats[id] = now;
    }
}
int Take(int id, int stat) {                               // a counter's change, claimed by an event
    if (id < 0 || id >= 16) return 0;
    const int v = gDelta[id].d[(stat - 0x14) / 2];
    gDelta[id].d[(stat - 0x14) / 2] = 0;
    return v;
}
// events of the HUD's messages, finished a few frames later with the statistics they changed
struct Pending { Ev e; int a = -1, b = -1; long long frame = 0; int hold = 4; uint32_t uid = 0; int v = -1; };   // v: a team
bool TeamRule();
std::vector<Pending> gPending;
long long gLastEndT = 0;                                    // the last round's end: the players' scores come after it
std::string StatName(int stat) {
    static const char* const n[32] = {"kills", "deaths", "stuns", "stunned", "snake kills", "points", "suicides", "self stuns",
        "team kills", "team stuns", "rounds", "rounds alive", "KEROTAN wins", "KEROTAN first grabs", "KEROTANs placed", "radio",
        "chat", "CQC", "CQC taken", "headshots", "headshots taken", "team wins", "Scorpion kills", "knife kills", "food",
        "rolls", "goggles", "stat 27", "stat 28", "stat 29", "best kill streak", "worst death streak"};
    const int i = (stat - 0x14) / 2;
    return i >= 0 && i < 32 ? n[i] : "?";
}
// a counter's change as a detail: {"s": player, "o": its offset in the player's variable set, "v": the
// set, "a": before, "b": after}
std::string StatDetail(int id, int stat, int delta) {
    const int i = (stat - 0x14) / 2;
    const int now = gStats[id].have ? gStats[id].v[i] : 0;
    std::string o = "{\"s\":";
    Json(o, NameOf(id));
    char b[96];
    snprintf(b, sizeof b, ",\"o\":%d,\"v\":%d,\"a\":%d,\"b\":%d}", stat, id + 1, now - delta, now);
    return o + b;
}
// a message of the game to its HUD as a detail: {"g": code (hex), "p": [players]}
std::string MsgDetail(uint32_t code, int a = -1000, int b = -1000) {
    char s[96];
    if (a == -1000) snprintf(s, sizeof s, "{\"g\":\"%06x\"}", code);
    else if (b == -1000) snprintf(s, sizeof s, "{\"g\":\"%06x\",\"p\":[%d]}", code, a);
    else snprintf(s, sizeof s, "{\"g\":\"%06x\",\"p\":[%d,%d]}", code, a, b);
    return s;
}
int Claim(Pending& p, int id, int stat) {                 // take a counter's change into the event, with its detail
    const int v = Take(id, stat);
    if (v) p.e.P(StatDetail(id, stat, v));
    return v;
}
// the HUD's messages on the host (0x1220E0: actor, code, argument)
void HudMessage(uint32_t code, uint32_t arg) {
    auto bytes = [&](int n) { std::vector<uint8_t> v(n, 0); const uint32_t p = arg & 0x01ffffff; if (arg >= 0x100000 && p + n <= 0x02000000) memcpy(v.data(), gMem + p, n); return v; };
    if (code == 0x751005 || code == 0x751010 || code == 0x751006) {
        const auto v = bytes(2);
        Pending p{Ev(code == 0x751006 ? "stun" : "kill")};
        p.a = (int8_t)v[0]; p.b = (int8_t)v[1]; p.frame = gFrameNo; p.hold = 12;   // the points come some frames later
        p.e.t = NowMs();
        if (code == 0x751010) p.e.N("hs", 1);
        p.e.P(MsgDetail(code, p.a, p.b));
        gPending.push_back(p);
    } else if (code == 0x75100f) {                           // a player in the game ({id}): the HUD's "JOINED"
        const auto v = bytes(1);
        const int id = v[0];
        for (const auto& np : NetPlayers()) if (np.id == (uint32_t)id && (np.flags & 3)) return;   // the host itself
        const std::string d = MsgDetail(code, id);
        const uint32_t sd = SetData((uint32_t)id + 1), uid = sd ? R32(sd + 0xa0) : 0;
        for (auto& p : gPending) if (p.e.k == "join" && ((uid && p.uid == uid) || p.a == id)) { p.e.P(d); p.a = id; return; }
        Pending p{Ev("join")};
        p.a = id; p.uid = uid; p.frame = gFrameNo; p.hold = 300;
        p.e.P(d);
        gPending.push_back(p);
    } else if (code == 0x760121) {
        Ev e("restart");
        e.P(MsgDetail(code));
        Write(e);
    } else if (code == 0x750014) {                           // the KEROTAN's first grab ({id} as the argument)
        Pending p{Ev("grab")};
        p.a = (int)(arg & 0xff); p.frame = gFrameNo; p.hold = 4; p.e.t = NowMs();
        p.e.N("first", 1);
        p.e.P(MsgDetail(code, p.a));
        gPending.push_back(p);
    } else if (code != 0x751008 && code != 0x751009 && code != 0x751001) {
        static std::set<uint32_t> said;
        if (said.insert(code).second) Log("report: HUD message %06x (arg %08x [%s]) not reported", code, arg, Hex(bytes(8).data(), 8).c_str());
    }
}
// the pending events, once their frames passed: the statistics they changed, then the line
void Finish() {
    for (size_t i = 0; i < gPending.size();) {
        Pending& p = gPending[i];
        if (gFrameNo - p.frame < p.hold) { ++i; continue; }
        Ev& e = p.e;
        if (e.k == "kill") {
            const bool valid = p.a >= 0 && p.a < 16 && p.a != p.b;
            if (!valid) e.k = "suicide";
            Claim(p, p.b, 0x16);
            if (valid) {
                Claim(p, p.a, 0x14);
                if (Claim(p, p.a, 0x24)) e.k = "teamkill";
                if (Claim(p, p.a, 0x1c)) e.N("snake", 1);
                if (Claim(p, p.a, 0x42)) e.S("w", "knife");
                else if (Claim(p, p.a, 0x40)) e.S("w", "scorpion");
                if (Claim(p, p.a, 0x3a) + Claim(p, p.b, 0x3c)) e.N("hs", 1);
                const int pts = Claim(p, p.a, 0x1e);
                if (pts) e.N("pts", pts);
                e.S("a", NameOf(p.a)).S("b", NameOf(p.b));
            } else {
                if (Claim(p, p.b, 0x20)) {}
                e.S("a", NameOf(p.b));
            }
        } else if (e.k == "stun") {
            if (p.a == p.b || p.a < 0) {
                e.k = "selfstun"; Claim(p, p.b, 0x22); Claim(p, p.b, 0x1a);
                e.S("a", NameOf(p.b));
            } else {
                Claim(p, p.a, 0x18); Claim(p, p.b, 0x1a);
                if (Claim(p, p.a, 0x26)) e.k = "teamstun";
                if (Claim(p, p.a, 0x36) + Claim(p, p.b, 0x38)) e.N("cqc", 1);
                e.S("a", NameOf(p.a)).S("b", NameOf(p.b));
            }
        } else if (e.k == "join") {
            e.S("a", p.a >= 0 ? NameOf(p.a) : NameOfUser(p.uid));
        } else if (e.k == "grab") {
            Claim(p, p.a, 0x2e);
            e.S("a", NameOf(p.a));
        } else if (e.k == "drop") {
            if (Claim(p, p.a, 0x30)) e.k = "goal";             // placed in its team's goal
            e.S("a", NameOf(p.a));
        } else if (e.k == "team") {
            if (p.v == gTeamSaid[p.a] || !TeamRule()) { gPending.erase(gPending.begin() + i); continue; }   // nothing new
            e.S("a", NameOf(p.a)).S("tm", TeamKey(p.v));
            if (gTeamSaid[p.a] == 0 || gTeamSaid[p.a] == 1) e.N("change", 1);
            gTeamSaid[p.a] = p.v; gTeamSaidAt[p.a] = gFrameNo;
            char b[64]; snprintf(b, sizeof b, "{\"tv\":[%d,%d]}", p.a + 1, p.v);
            e.P(b);
        } else if (e.k == "score") {
            if (e.t <= gLastEndT && e.t > gLastEndT - 3000) e.t = gLastEndT + 1;   // after the round's end line
        }
        Write(e);
        gPending.erase(gPending.begin() + i);
    }
    // what no event claimed, 20 frames on: events of their own
    for (int id = 0; id < 16; ++id) {
        if (!gDelta[id].any || gFrameNo - gDeltaFrame[id] < 20) continue;
        bool pending = false;
        for (const auto& p : gPending) if (p.a == id || p.b == id) pending = true;
        if (pending) continue;
        gDelta[id].any = false;
        auto one = [&](int stat, const char* kind) {
            const int v = Take(id, stat);
            if (!v) return;
            Ev e(kind); e.S("a", NameOf(id)); if (v > 1) e.N("n", v);
            e.P(StatDetail(id, stat, v));
            Write(e);
        };
        // CQC: the attacker's 0x36 with a victim's 0x38 of the same moment
        if (int v = Take(id, 0x36)) {
            int victim = -1;
            for (int j = 0; j < 16 && victim < 0; ++j) if (j != id && gDelta[j].d[(0x38 - 0x14) / 2]) victim = j;
            Ev e("cqc"); e.S("a", NameOf(id));
            e.P(StatDetail(id, 0x36, v));
            if (victim >= 0) { const int w = Take(victim, 0x38); e.S("b", NameOf(victim)); e.P(StatDetail(victim, 0x38, w)); }
            Write(e);
        }
        if (DevEnv("MGO1_DUMP") && gDelta[id].d[(0x30 - 0x14) / 2]) { gDumpAt.insert(gFrameNo + 60); gDumpAt.insert(gFrameNo + 360); }
        one(0x30, "goal");
        one(0x2c, "capture");
        one(0x32, "radio");
        one(0x34, "chat");
        one(0x44, "food");
        one(0x14, "kill");                                  // kills and deaths the HUD did not show
        one(0x16, "death");
        one(0x20, "suicide");
        for (int k = 0; k < 32; ++k) gDelta[id].d[k] = 0;  // the rest (rounds, points, rolls...): with the round's end
    }
}
// the capture mission's KEROTAN: NewOnlineItemChara2 (items at +0xB0, 0x120 bytes each, their count at
// +0x410; an item's position at +0x90, its holder at +0xD4 (the holder's player id at +0xB60), the team
// whose goal holds it at +0xF8 (0xFF none); the capture's countdown at +0x420: 900 frames, 15 s)
int gHolder[3] = {-1, -1, -1};
int gCapLeft = -1;
void ItemTick() {
    uint32_t man = 0;
    for (const auto& a : gNativeActors) if (a.second == "NewOnlineItemChara2") { man = a.first; break; }
    const int n = man ? std::min(3, (int)R32(man + 0x410)) : 0;
    char b[128];
    for (int i = 0; i < 3; ++i) {
        int id = -1;
        const uint32_t it = man + 0xb0 + 0x120 * i;
        if (i < n) { const uint32_t h = R32(it + 0xd4); if (h >= 0x100000 && h < 0x02000000) id = R8(h + 0xb60); }
        if (id >= 16) id = -1;
        if (id == gHolder[i]) continue;
        const int was = gHolder[i];
        gHolder[i] = id;
        if (i >= n) continue;                                // the round is over: no event
        float v[3]; for (int k = 0; k < 3; ++k) { uint32_t u = R32(it + 0x90 + 4 * k); memcpy(&v[k], &u, 4); }
        snprintf(b, sizeof b, "{\"kr\":%d,\"h\":%d,\"x\":%d,\"y\":%d,\"z\":%d}", i, id, (int)v[0], (int)v[1], (int)v[2]);
        if (id >= 0) {                                       // taken (its first grab: the HUD's message made that event)
            bool first = false;
            for (auto& p : gPending) if (p.e.k == "grab" && p.a == id) { first = true; p.e.P(b); }
            if (!first) {
                Pending p{Ev("grab")}; p.a = id; p.frame = gFrameNo; p.hold = 4; p.e.t = NowMs(); p.e.P(b);
                if (gCapLeft >= 0 && gCapLeft < 900) p.e.N("stop", (gCapLeft + 59) / 60);   // taken out of a goal: no capture
                gPending.push_back(p);
            }
        } else if (was >= 0) {                               // let go: in a goal (its "placed" counter), or dropped
            const int team = R8(it + 0xf8);
            Pending p{Ev("drop")}; p.a = was; p.frame = gFrameNo; p.hold = 20; p.e.t = NowMs(); p.e.P(b);
            if (team == 0 || team == 1) p.e.S("tm", TeamKey(team));
            gPending.push_back(p);
        }
    }
    // the capture's countdown: its start, 10 and 5 s left
    const int cnt = n ? (int)R32(man + 0x420) : -1;
    if (cnt >= 0 && cnt <= 900 && gCapLeft >= 0 && cnt != gCapLeft) {
        const int team = R8(man + 0xb0 + 0xf8);
        const bool start = gCapLeft >= 900 && cnt < 900;
        const bool mark = (gCapLeft > 600 && cnt <= 600) || (gCapLeft > 300 && cnt <= 300);
        if (start || mark) {
            Ev e("capleft"); e.N("s", (cnt + 59) / 60);
            if (start) e.t = NowMs() + 1;                      // after the line of the KEROTAN placed
            if (team == 0 || team == 1) e.S("tm", TeamKey(team));
            snprintf(b, sizeof b, "{\"cd\":%d}", cnt);
            e.P(b);
            Write(e);
        }
    }
    gCapLeft = cnt;
}
bool TeamRule() { const int r = CurRule(); return r >= 1 && r <= 4; }
void TeamEvent(int id, int team, int hold) {
    for (auto& p : gPending) if (p.e.k == "team" && p.a == id) { p.v = team; return; }   // a newer pick: that one
    Pending p{Ev("team")}; p.a = id; p.v = team; p.frame = gFrameNo; p.hold = hold; p.e.t = NowMs();
    const auto it = gTeamPk.find(id);
    if (it != gTeamPk.end() && gFrameNo - it->second.second < 120) { p.e.P(it->second.first); gTeamPk.erase(it); }
    gPending.push_back(p);
}
void TeamTick(bool roundStart) {
    const bool teams = TeamRule();
    bool in[16] = {false};                                  // the players in the session (a set outlives its player)
    for (const auto& np : NetPlayers()) if (np.id < 16 && !(np.flags & 3)) in[np.id] = true;
    for (int id = 1; id < 16; ++id) {
        const uint32_t d = in[id] ? SetData((uint32_t)id + 1) : 0;
        const int team = d ? R8(d + 1) : -1;
        const int was = gTeamVs[id];
        gTeamVs[id] = team;
        if (!d) { gTeamSaid[id] = -1; continue; }
        if (!teams || (team != 0 && team != 1) || NameOf(id).empty()) continue;
        if (roundStart) { TeamEvent(id, team, 300); continue; }
        if (team != was) TeamEvent(id, team, gTeamSaid[id] >= 0 ? 4 : 60);
    }
}
// the players in the game other than the host
int Joiners() {
    int n = 0;
    for (const auto& p : NetPlayers()) if (!(p.flags & 3)) ++n;
    return n;
}
// the room: its phase (set 0 +0: 1 before a round, 2 playing, 4 over, 5 the next one coming), the
// rotation's entry (+0x36) and the round's seconds left (+0x3C)
int gPhase = -1;
int gRounds[5] = {1, 1, 1, 1, 1};                          // rounds of each rule (0x4310: TDM +243, rescue +235, capture +227, sneaking +219)
struct RoundEnd { long long frame = -1, t = 0; int left = 0, entry = 0, sub = 0; } gRoundEnd;
void RoomTick() {
    const uint32_t d = SetData(0);
    if (!d) { gPhase = -1; return; }
    const int phase = R8(d), entry = R8(d + 0x36);
    const int left = (int)R32(d + 0x3c);
    if (phase == gPhase) return;
    const int was = gPhase;
    gPhase = phase;
    if (was < 0 || !Joiners()) return;                     // the host alone: its rounds are not reported
    char b[160];
    if (phase == 2) {
        for (int id = 0; id < 16; ++id) if (gFrameNo - gTeamSaidAt[id] > 1800) gTeamSaid[id] = -1;
        TeamTick(true);
        Ev e("round");
        e.N("i", entry).N("of", gGame.count).N("s", left);
        if (entry >= 0 && entry < gGame.count) {
            const int r = gGame.rule[entry];
            e.N("r", r).N("m", gGame.map[entry]);
            if (r >= 0 && r <= 4 && gRounds[r] > 1) e.N("rn", R8(d + 0x40) + 1).N("rof", gRounds[r]);   // its round of the rule (+0x40)
        }
        e.N("n", Joiners());
        snprintf(b, sizeof b, "{\"ph\":[%d,2],\"e\":%d,\"s\":%d}", was, entry, left);
        e.P(b);
        Write(e);
    } else if (phase == 4 && was == 2) {
        gRoundEnd.frame = gFrameNo; gRoundEnd.t = NowMs(); gRoundEnd.left = left; gRoundEnd.entry = entry; gRoundEnd.sub = R8(d + 0x40);
    }
}
// the round's end, 2 frames after the phase went to 4: the winners' team wins (0x3E), else the
// most points (then kills) in a deathmatch; the players' 0x4390 reports follow it
void RoundEndTick() {
    if (gRoundEnd.frame < 0 || gFrameNo - gRoundEnd.frame < 2) return;
    gRoundEnd.frame = -1;
    Ev e("end");
    e.t = gLastEndT = gRoundEnd.t;
    const int entry = gRoundEnd.entry;
    e.N("i", entry).N("left", gRoundEnd.left);
    const int rule = entry >= 0 && entry < gGame.count ? gGame.rule[entry] : -1;
    if (rule >= 0) {
        e.N("r", rule).N("m", gGame.map[entry]);
        if (rule <= 4 && gRounds[rule] > 1) e.N("rn", gRoundEnd.sub + 1).N("rof", gRounds[rule]);
    }
    // the teams (room variables +0x42 red, +0x46 blue: {round wins, team, tickets u16}): their round
    // wins, and a team deathmatch's tickets left
    if (const uint32_t d0 = SetData(0)) if (rule >= 1) {
        char w[64];
        snprintf(w, sizeof w, "[%d,%d]", R8(d0 + 0x42), R8(d0 + 0x46));
        e.f.push_back({"wins", w});
        if (rule == 1) { snprintf(w, sizeof w, "[%d,%d]", R16(d0 + 0x44), R16(d0 + 0x48)); e.f.push_back({"tickets", w}); }
    }
    int team = -1, best = -1, bestPts = -1, bestKills = -1;
    for (int id = 0; id < 16; ++id) {
        if (!gStats[id].have) continue;
        const uint32_t sd = SetData((uint32_t)id + 1);
        const int t = sd ? R8(sd + 1) : 0xff;
        if (Take(id, 0x3e) && (t == 0 || t == 1)) team = t;
        Take(id, 0x28); Take(id, 0x2a);
        if (t == 0xfe || NameOf(id).empty()) continue;      // the host (Host Only) plays no part
        const int pts = (int16_t)gStats[id].v[5], kills = gStats[id].v[0];
        if (pts > bestPts || (pts == bestPts && kills > bestKills)) { best = id; bestPts = pts; bestKills = kills; }
    }
    char b[160];
    if (team >= 0) { e.S("win", TeamKey(team)); snprintf(b, sizeof b, "{\"tw\":\"%s\"}", TeamKey(team)); e.P(b); }
    else if (best >= 0 && gGame.count && entry < gGame.count && gGame.rule[entry] == 0) {
        e.S("a", NameOf(best)).N("pts", bestPts).N("kills", bestKills);
    }
    snprintf(b, sizeof b, "{\"ph\":[2,4],\"e\":%d,\"s\":%d}", entry, gRoundEnd.left);
    e.P(b);
    Write(e);
}
// ---- the lobby's frames (TX55: 8-byte header {command, length, sequence}, MD5, body; XOR 5A 70 85 AF) ----
std::string gLobbyBuf[2];                                   // [0] received, [1] sent: bytes not yet a whole frame
std::map<uint32_t, long long> gKickedAt;                    // user id -> the frame of our kick (the reason's lookup)
std::string Who(uint32_t uid) {
    const std::string n = NameOfUser(uid);
    char b[64]; snprintf(b, sizeof b, "user 0x%x", uid);
    return n.empty() ? std::string(b) : n + " (" + b + ")";
}
void LobbyFrame(bool out, unsigned cmd, const uint8_t* b, size_t n) {
    if ((cmd >> 12) == 3 || cmd == 0x0005 || cmd == 0x4398 || cmd == 0x4399) return;   // log-in frames stay private; keep-alives, pings
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= n ? ((uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 | (uint32_t)b[o + 2] << 8 | b[o + 3]) : 0; };
    char d[200];
    if (!out) {                                            // an answer: {result, ...}; only a refusal is an event
        const int32_t r = (int32_t)u32(0);
        if (n >= 4 && r != 0) {
            Ev e("refused");
            e.N("c", cmd).N("r", r);
            snprintf(d, sizeof d, "{\"l\":\"%04x\",\"d\":\"i\",\"r\":%d}", cmd, r);
            e.P(d);
            Write(e);
        }
        return;
    }
    // a frame sent: {"l": its command, "d": "o", "n": its size, "x": its first bytes}
    snprintf(d, sizeof d, "{\"l\":\"%04x\",\"d\":\"o\",\"n\":%zu,\"x\":\"%s\"}", cmd, n, Hex(b, n < 24 ? n : 24).c_str());
    const std::string pk = d;
    if (cmd == 0x4340) {                                   // a player joined: the join's detail
        const uint32_t uid = u32(0);
        const std::string name = NameOfUser(uid);
        for (auto& p : gPending) if (p.e.k == "join" && (p.uid == uid || (!name.empty() && p.a >= 0 && NameOf(p.a) == name))) { p.e.P(pk); p.uid = uid; return; }
        Pending p{Ev("join")};
        p.a = -1; p.uid = uid; p.frame = gFrameNo; p.hold = 300;
        p.e.P(pk);
        gPending.push_back(p);
    } else if (cmd == 0x4342 || cmd == 0x4346) {            // left, or kicked
        const uint32_t uid = u32(0);
        static std::map<uint32_t, long long> kickedAt;
        if (cmd == 0x4346) kickedAt[uid] = gFrameNo;
        else if (kickedAt.count(uid) && gFrameNo - kickedAt[uid] < 600) return;   // the kick's own leave
        Ev e(cmd == 0x4346 ? "kicked" : "leave");
        e.S("a", NameOfUser(uid));
        if (cmd == 0x4346) {
            std::string why = "game";
            const std::string nm = NameOfUser(uid);
            for (auto it = gKicked.rbegin(); it != gKicked.rend(); ++it)
                if (!_stricmp(it->name.c_str(), nm.c_str()) && gFrameNo - it->frame < 1800) { why = it->why; break; }
            e.S("why", why);
        }
        e.P(pk);
        Write(e);
    } else if (cmd == 0x4344) {                             // a team {user, team}: the detail of the player's team event
        const std::string name = NameOfUser(u32(0));
        for (int id = 1; id < 16; ++id) {
            if (name.empty() || NameOf(id) != name) continue;
            bool attached = false;
            for (auto& p : gPending) if (p.e.k == "team" && p.a == id) { p.e.P(pk); attached = true; }
            if (!attached) gTeamPk[id] = {pk, gFrameNo};
        }
    } else if (cmd == 0x4392) {                             // the next round of the rotation
        Ev e("next");
        const int entry = n ? b[0] : 0;
        e.N("i", entry).N("of", gGame.count);
        if (entry < gGame.count) e.N("r", gGame.rule[entry]).N("m", gGame.map[entry]);
        e.P(pk);
        Write(e);
    } else if (cmd == 0x4390 && n >= 4 + 32) {              // a player's statistics for the round
        const uint32_t uid = u32(0);
        const uint8_t* s = b + 4;
        auto i32 = [&](size_t o) { return (int32_t)((uint32_t)s[o] << 24 | (uint32_t)s[o + 1] << 16 | (uint32_t)s[o + 2] << 8 | s[o + 3]); };
        Pending p{Ev("score")};                             // written after the round's end
        p.e.S("a", NameOfUser(uid)).N("kills", i32(0)).N("deaths", i32(4)).N("pts", i32(24)).N("stuns", i32(12));
        if (n >= 4 + 80) p.e.N("hs", i32(76));
        p.e.P(pk);
        p.frame = gFrameNo; p.hold = 10; p.e.t = NowMs();
        gPending.push_back(p);
    } else if (cmd == 0x4380) {
        Ev e("quit"); e.P(pk); Write(e);
    } else if (cmd == 0x4310) {
        for (int& t : gTeamSaid) t = -1;
        if (n >= 262) {                                     // TX55's CreateGameOptions: the rounds of each rule
            gRounds[0] = 1; gRounds[1] = (int)u32(243); gRounds[2] = (int)u32(235); gRounds[3] = (int)u32(227); gRounds[4] = (int)u32(219);
            for (int& r : gRounds) if (r < 1 || r > 99) r = 1;
        }
        Ev e("game");
        e.S("name", gGame.name).N("max", gGame.maxPlayers);
        std::string rot;
        for (int i = 0; i < gGame.count; ++i) { char x[16]; snprintf(x, sizeof x, "%s%d:%d", i ? "," : "", gGame.rule[i], gGame.map[i]); rot += x; }
        e.S("rot", rot);
        e.P(pk);
        Write(e);
    } else if (cmd == 0x4316) {
        Ev e("open"); e.P(pk); Write(e);                     // the game is in the lobby's list
    } else {
        Ev e("packet"); e.N("c", cmd); e.P(pk); Write(e);
    }
}
void LobbyData(bool out, const uint8_t* p, int n) {
    if (!On() || n <= 0) return;
    static const uint8_t key[4] = {0x5A, 0x70, 0x85, 0xAF};
    std::string& buf = gLobbyBuf[out ? 1 : 0];
    buf.append((const char*)p, n);
    while (buf.size() >= 24) {
        uint8_t h[8];
        for (int j = 0; j < 8; ++j) h[j] = (uint8_t)buf[j] ^ key[j & 3];
        const unsigned cmd = (h[0] << 8) | h[1], len = (h[2] << 8) | h[3];
        if (buf.size() < 24 + len) break;
        std::vector<uint8_t> body(len);
        for (unsigned i = 0; i < len; ++i) body[i] = (uint8_t)buf[24 + i] ^ key[(24 + i) & 3];
        buf.erase(0, 24 + len);
        LobbyFrame(out, cmd, body.data(), body.size());
    }
    if (buf.size() > 65536) buf.clear();
}
// the log-in screens' errors ("Server login failed.|( 816:-4 )"): before the game is made
void ScreenText(const char* t) {
    if (gWasHosting) return;
    const char* q = strstr(t, "( ");
    if (!q || !isdigit((unsigned char)q[2])) return;
    std::string s = t;
    for (auto& ch : s) if (ch == '|') ch = ' ';
    while (!s.empty() && s.back() == ' ') s.pop_back();
    gLoginError = Redact(s);                                // the status file's reason (the bot's state)
    if (!On()) return;
    static std::string last;
    if (last == t) return;
    last = t;
    Ev e("error"); e.S("x", s);
    std::string sc = "{\"sc\":"; Json(sc, s); e.P(sc + "}");
    Write(e);
}
void Stop(const char* why) {                                // the process ends (the service starts a new game)
    if (!On()) return;
    Ev e("stop"); e.S("why", why);
    Write(e);
    Flush(true);
}
void Tick() {
    if (!On()) return;
    StatsTick();
    RoomTick();
    TeamTick(false);
    ItemTick();
    RoundEndTick();
    Finish();
    Flush(false);
}
}  // namespace report
// (v150-host beta 19) the round's clocks of a dedicated host made up on the wall clock (clocks.h, docs/DEDICATED_HOST.md
// 23): at each pass of the actor lists, the passes the host's processor share could not make (the clock's fields not
// covered) are taken off the clocks the host counts for every player: the round's time (the supervisor's record [+0x3BC]
// +0, 5 a pass, 6 on the European program), the capture's countdown (NewOnlineItemChara2 +0x420, from its start value
// +0x41C, one a pass: 0x51DC80 in ntmenu2's copy), the lost items' return home (5 a pass) and each item's pick-up lock after a drop (+0xD8 of an item lying or falling,
// one a pass). Only the session's host, only while the round's time runs or the item set in a goal counts down past it
// (its overtime). MGO1_CLOCK_MAKEUP=0: as before.
#include "clocks.h"
ClockMakeUp gClocks;
void ClocksTick() {
    if (DevEnv("SIM_CLOCK_LOG")) {                      // (tests) the capture's countdown and the round's time as this game reads
        static int lastCd = -9, lastT = -9;            // them, at each change of their whole seconds, with the wall clock's ms
        uint32_t man = 0;
        for (const auto& a : gNativeActors) if (a.second == "NewOnlineItemChara2") { man = a.first; break; }
        const int cd = man ? (int32_t)R32(man + 0x420) : -9;
        const uint32_t at = PageData(0x3ECBA8, 0, 0x2F0C90), sup = at ? R32(at) & 0x01FFFFFF : 0;
        const uint32_t r = sup >= 0x100000 && sup + 0x400 < 0x02000000 ? R32(sup + 0x3BC) & 0x01FFFFFF : 0;
        const int t = r >= 0x100000 && r + 0x20 < 0x02000000 ? (int32_t)R32(r) : -9;
        const int cs = cd > 0 ? (cd + 59) / 60 : cd, ts = t > 0 ? (t + 299) / 300 : t;
        if (cs != lastCd || ts != lastT)
            Log("clock log: %llu ms: the countdown %d (%d s), the round's time %d (%d s)", (unsigned long long)(RealQpc() * 1000 / QpcFreq()), cd, cs, t, ts);
        lastCd = cs; lastT = ts;
    }
    static const bool on = (DevEnv("MGO1_HOST_ONLY") || DevEnv("MGO1_HOST_SETTINGS")) &&
                           !(DevEnv("MGO1_CLOCK_MAKEUP") && atoi(DevEnv("MGO1_CLOCK_MAKEUP")) == 0);
    if (!on) return;
    uint32_t rec = 0;
    if (IsSessionHost()) {
        const uint32_t at = PageData(0x3ECBA8, 0, 0x2F0C90), sup = at ? R32(at) & 0x01FFFFFF : 0;
        if (sup >= 0x100000 && sup + 0x400 < 0x02000000) rec = R32(sup + 0x3BC) & 0x01FFFFFF;
        if (rec < 0x100000 || rec + 0x20 >= 0x02000000) rec = 0;
    }
    uint32_t man = 0;
    for (const auto& a : gNativeActors) if (a.second == "NewOnlineItemChara2") { man = a.first; break; }
    if (man < 0x100000 || man + 0x8E0 >= 0x02000000) man = 0;
    const int32_t cd = man ? (int32_t)R32(man + 0x420) : 0;
    const bool counting = cd > 1 && cd < (int32_t)R32(man + 0x41C);       // the item set: it runs on past the round's time
    const bool running = rec && !R8(rec + 4) && ((int32_t)R32(rec) > 0 || counting);
    const long long field = gPal ? QpcFreq() / 50 : QpcFreq() * 1001 / 60000;   // a pass at the game's pace
    const long long now = QpcNow();
    const int n = gClocks.Pass(now, field, running, gFrameNo);   // gFrameNo: the VBlanks served (the pace line's)
    static long long saidAt = 0, saidMade = 0, saidPasses = 0, saidShort = 0;
    // (v150-host beta 20) the short clocks (the countdown's 20 s, each item's lock of 1.5 or 3 s) each on its own account
    // of the wall clock from its start (clocks.h ShortClock): beta 19's 3 passes a pass at most dropped what a pass owed
    // above 3 and left the countdown 8 to 11 % long on a slowed host. What the host lost before a countdown or a lock
    // began (paid at once to the round's time and the item's return when it turns slowed) cannot come off it.
    static ShortClock cdClock, lockClock[3];
    if (running && man) {
        const int32_t w = cdClock.Pass(now, field, counting, gClocks.slowed, cd);
        if (w != cd) W32(man + 0x420, (uint32_t)w);
        const uint32_t k = std::min(3u, R32(man + 0x410));
        for (uint32_t i = 0; i < 3; ++i) {
            if (i >= k) { lockClock[i].on = false; continue; }
            const uint32_t it = man + 0xB0 + 0x120 * i;
            const uint32_t st = R32(it + 0xD0);           // lying (0) or falling (2): 0x51DE08 counts its lock
            const int32_t l = (int32_t)R32(it + 0xD8);
            const int32_t m = lockClock[i].Pass(now, field, (st == 0 || st == 2) && l > 1, gClocks.slowed, l);
            if (m != l) W32(it + 0xD8, (uint32_t)m);
        }
    } else { cdClock.on = false; for (auto& c : lockClock) c.on = false; }
    if (n > 0) {
        W32(rec, (uint32_t)MakeUpTimeLeft((int32_t)R32(rec), gPal ? 6 : 5, n));
        if (man) {
            // the items' return home (capture only; kero's find, reports/kerotan_stuck.md): one timer for all, n003a
            // 0x2E5254 (0x39C548's second address): step x 10800 while an item is held, then the step off a pass
            // (180 s), the items back home at 0; -1 idle
            if (const uint32_t home = PageData(0x39C548, 1, 0x2E5254)) {
                const int32_t v = (int32_t)R32(home);
                if (v > 0 && v <= 12 * 10800) W32(home, (uint32_t)MakeUpTimeLeft(v, gPal ? 6 : 5, n));
            }
        }
        if (!saidAt) { saidAt = gFrameNo; saidMade = gClocks.made - n; saidPasses = gClocks.passes - 1; }
    }
    if (saidAt && gFrameNo - saidAt >= 3600) {          // a line a minute while it makes up
        const long long sh = cdClock.made + lockClock[0].made + lockClock[1].made + lockClock[2].made;
        Log("clocks: %lld passes of the round's clocks made up in %lld passes (the host short of processor: its passes "
            "fewer than the VBlanks of the clock; docs/DEDICATED_HOST.md 23); %lld off the capture's countdown and the items' locks",
            gClocks.made - saidMade, gClocks.passes - saidPasses, sh - saidShort);
        saidAt = 0; saidShort = sh;
    }
}
// MGO1_EVENTS_PROBE=1 (diagnostics): every change of the game network's variable sets (0x28D528:
// 18 sets, {count, descriptors (offset, element size, elements, flags; 10 bytes each), data}; the
// set of player P is P = id + 1) logged once a frame, a variable that changes more than 20 times in
// 2 s then left out; with the writes through 0x178300 (the game's own set) and their callers
int gProbeSet(uint32_t data) {                          // the set holding this data block, -1 none
    for (int s = 0; s < 18; ++s) { const uint32_t e = R32(0x28D528 + 4 * s); if (e && e < 0x02000000 && R32(e + 8) == data) return s; }
    return -1;
}
std::set<uint32_t> gProbeNoisy;                          // set << 16 | offset
void EventsProbe() {
    static const bool on = DevEnv("MGO1_EVENTS_PROBE") != nullptr;
    if (!on) return;
    static std::vector<uint8_t> prev[18];
    static std::map<uint32_t, std::deque<long long>> churn;
    for (int s = 0; s < 18; ++s) {
        const uint32_t e = R32(0x28D528 + 4 * s);
        if (!e || e >= 0x02000000) { prev[s].clear(); continue; }
        const uint32_t n = R32(e), desc = R32(e + 4), data = R32(e + 8);
        if (!data || data >= 0x02000000 || !desc || desc >= 0x02000000 || n > 256) { prev[s].clear(); continue; }
        std::vector<uint8_t> now;
        std::vector<std::pair<uint32_t, uint32_t>> vars;  // offset, size
        uint32_t size = 0;
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t d = desc + 10 * k, off = R16(d), el = R16(d + 2), cnt = R16(d + 4);
            vars.push_back({off, el * cnt});
            size = std::max(size, off + el * cnt);
        }
        if (!size || size > 0x4000 || data + size > 0x02000000) continue;
        now.assign(gMem + data, gMem + data + size);
        if (prev[s].size() == now.size()) {
            for (const auto& v : vars) {
                if (!memcmp(&now[v.first], &prev[s][v.first], v.second)) continue;
                const uint32_t key = (uint32_t)s << 16 | v.first;
                if (gProbeNoisy.count(key)) continue;
                auto& q = churn[key];
                q.push_back(gFrameNo);
                while (!q.empty() && gFrameNo - q.front() > 120) q.pop_front();
                if (q.size() > 20) { gProbeNoisy.insert(key); Log("probe: S%d +%03x (%u bytes) changes too often: left out", s, v.first, v.second); continue; }
                std::string a, b;
                char h[4];
                for (uint32_t i = 0; i < v.second && i < 64; ++i) { snprintf(h, sizeof h, "%02x", prev[s][v.first + i]); a += h; snprintf(h, sizeof h, "%02x", now[v.first + i]); b += h; }
                Log("probe: S%d +%03x: %s -> %s", s, v.first, a.c_str(), b.c_str());
            }
        } else Log("probe: S%d data %08x, %zu variables, %u bytes", s, data, vars.size(), size);
        prev[s].swap(now);
    }
}
void HostCommands() {
    HostWatch();
    EventsProbe();
    report::Tick();
    if (!gKicks.empty() && gFrameNo - gKicks.front().at > 180) {   // no room message meanwhile: the request alone
        const KickItem it = gKicks.front();
        gKicks.pop_front();
        KickRequest(it.id, it.why, "no room message for 3 s, kick requested");
    }
    if (gFrameNo % 60 == 0) {
        if (gFrameNo % 300 == 0) { ListRead("MGO1_BANS_FILE", gBlack, "blacklist"); ListRead("MGO1_WHITELIST_FILE", gWhite, "whitelist"); }
        const auto ps = NetPlayers();
        if (!gBlack.names.empty() || gWhite.only) ListsApply(ps);
        UnrankedWatch();                                 // (v141 beta 7) a doll in the game: unranked to its end
        StatusWrite(ps);
    }
    static const char* path = DevEnv("MGO1_CMD_FILE");
    if (!path || gFrameNo % 30) return;
    const std::string run = std::string(path) + ".run";
    if (!MoveFileExA(path, run.c_str(), MOVEFILE_REPLACE_EXISTING)) return;   // no command waiting
    FILE* f = nullptr;
    std::string all;
    if (fopen_s(&f, run.c_str(), "rb") == 0 && f) {
        char b[512]; size_t n;
        while ((n = fread(b, 1, sizeof b, f)) > 0) all.append(b, n);
        fclose(f);
    }
    DeleteFileA(run.c_str());
    size_t at = 0;
    while (at < all.size()) {
        size_t e = all.find('\n', at); if (e == std::string::npos) e = all.size();
        std::string line = all.substr(at, e - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        HostCommand(line);
        at = e + 1;
    }
}
#else
inline void ClocksTick() {}                          // (v150-host beta 21: the dedicated host's only; the player build compiles again)
#endif
// SIM_PROFILE="from:to[:us]": between those frames a sampler thread stops the running PS2 thread
// every <us> microseconds (1000 by default) and keeps its instruction pointer; after <to> the
// samples go to profile.tsv (frame, rip, PS2 thread; nm on the executable names the functions).
// Nothing is allocated or written while the thread is stopped (it may hold the heap's lock)
struct SimProfSample { long long frame, dg; unsigned long long rip; int thread; };
std::atomic<long long> gProfFrame{-1};                  // the frame inside the window, -1 outside, -2 after it
DWORD WINAPI ProfileBody(LPVOID arg) {
    const DWORD us = (DWORD)(uintptr_t)arg;
    std::vector<SimProfSample> smp;
    smp.reserve(1 << 20);
    for (;;) {
        Sleep(us >= 1000 ? us / 1000 : 0);
        const long long fr = gProfFrame.load();
        if (fr == -2) break;
        if (fr < 0 || smp.size() >= smp.capacity()) continue;
        const int id = gCur;
        EeThread* t = Thr(id);
        HANDLE h = t ? t->os : nullptr;
        if (!h || SuspendThread(h) == (DWORD)-1) continue;
        CONTEXT c; memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_CONTROL;
        const bool ok = GetThreadContext(h, &c) != 0;
        ResumeThread(h);
        if (ok) smp.push_back({fr, gDgFrames.load(), (unsigned long long)c.Rip, id});
    }
    FILE* f = nullptr;
    if (fopen_s(&f, "profile.tsv", "w") == 0 && f) {
        fprintf(f, "frame\trip\tthread\tdg\n");
        for (auto& x : smp) fprintf(f, "%lld\t%llx\t%d\t%lld\n", x.frame, x.rip, x.thread, x.dg);
        fclose(f);
    }
    Log("profile: %zu samples written to profile.tsv", smp.size());
    return 0;
}
#include "perf.h"                                       // the threads' processor time (SIM_THREAD_STATS)
// pace (v141 beta 4, a lag report): the VBlanks against the clock (a host or a doll whose VBlanks come late or fewer
// than 59.94 a second runs its game slower, and every player of the game sees it), the frame hook's time (a doll's
// brain), the process's processor and memory; a line a minute (docs/LOGS.md "pace:")
struct PaceRun { long long since = 0, vblanks = 0, late4 = 0, lateField = 0, maxLate = 0, hooks = 0, hookTicks = 0, hookMax = 0; double cpuMs = 0; };
PaceRun gPaceRun;
// ((v141 beta 9) the last "pace:" line's measure, for the status files: the doll manager's pace guard, host/mgo1doll-manager)
struct PaceLast { int lines = 0; double rate = 0, due = 0, cpu = 0; };
PaceLast gPaceLast;
void PaceHook(long long ticks) { gPaceRun.hookTicks += ticks; if (ticks > gPaceRun.hookMax) gPaceRun.hookMax = ticks; ++gPaceRun.hooks; }
void NativeVBlankStart() {
    ++gFrameNo;
    {   // MGO1_CRASH_TEST=<frame>: (tests) a crash at that frame, to check what the logs say of one
        static const long long crashAt = DevEnv("MGO1_CRASH_TEST") ? atoll(DevEnv("MGO1_CRASH_TEST")) : -1;
        if (crashAt >= 0 && gFrameNo == crashAt) { Log("crash test: a write to address 0"); *(volatile int*)nullptr = 1; }
    }
    gVBlankAt = gNextVBlank - FieldTicks();                  // this field's start (the pace guard's clock)
    PerfVBlankCpu();                                         // perf.h: where the kernel loop runs
    if (gFpsWant.load() >= 0) {                              // the in-game menu's rate: DG's rate variables as 0x144190 sets them
        P.matchFps = gFpsWant.exchange(-1);
        if (gMapAsks30) {
            const bool run60 = P.matchFps == 60, full = run60 && FPS60_KIND != 0;
            if (full || !R32(V(0x23B8FC))) { W32(V(0x23B8F8), full ? 1 : 2); W32(V(0x23B8FC), full ? 0 : 1); W32(V(0x23B900), 0); W32(V(0x23A618), full ? 0 : 0x40); }
            else W32(V(0x23B8F8), run60 ? 1 : 2);                  // 30 <-> exact 60: the same passes, only DG's frame target
            ps2::gVariant[3] = run60 && FPS60_KIND == 1;
            if (!ps2::gVariant[3]) ps2::gVariant[4] = 0;
            Log("frame rate: now %s (in-game menu)", !run60 ? "30 frames a second, as on the PS2" : !full ? "60 pictures a second, the game at the PS2's pace"
                : FPS60_KIND == 1 ? "60 frames a second with the fair rules" : "60 frames a second without rules");
        }
    }
#ifndef MGO1_PLAYER_BUILD
    HostCommands();
#endif
    {
        static long long from = -1, to = -1;
        static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = DevEnv("SIM_PROFILE")) {
                unsigned us = 1000;
                if (sscanf(e, "%lld:%lld:%u", &from, &to, &us) >= 2 && to >= from) {
                    CreateThread(nullptr, 0, ProfileBody, (LPVOID)(uintptr_t)us, 0, nullptr);
                    Log("profile: frames %lld to %lld, a sample every %u us", from, to, us);
                } else from = -1;
            }
        }
        if (from >= 0) gProfFrame = gFrameNo < from ? -1 : gFrameNo <= to ? gFrameNo : -2;
    }
    if (P.udpPort && P.udpPort != 5730 && R16(V(0x28D30C)) == 5730) W16(V(0x28D30C), (uint16_t)P.udpPort);   // online data + 0x84
    {
        static bool text = false;
        if (KeyboardTextEntry() != text) { text = !text; Log("keyboard: a software keyboard %s", text ? "is open" : "is closed"); }
        // each software keyboard: state (work + 0x4A), its owner's block (work + 0x40): +0x3C flags, +0x40 status
        static std::string last;
        std::string now;
        for (auto& k : gSoftKeyboards) {
            const uint32_t blk = R32(k.first + 0x40);
            char b[96];
            snprintf(b, sizeof b, " %08x:s%u", k.first, R16(k.first + 0x4A));
            now += b;
            if (blk >= 0x100000 && blk < 0x02000000) { snprintf(b, sizeof b, "/f%04x/p%d", R16(blk + 0x3C), (int16_t)R16(blk + 0x40)); now += b; }
        }
        if (now != last) { last = now; Log("keyboard: software keyboards%s", now.empty() ? " none" : now.c_str()); }
    }
    ps2::GsVSync();                                                          // CSR: VSINT, the field
    if (gVSyncFlag) { W32(gVSyncFlag, 1); if (gVSyncCsr) W64(gVSyncCsr, ps2::GsPrivRead(0x12001000, 8)); }
    {
        const bool pal60 = gPal && P.palAt60 && Exact60() && (R32(kHalfRate) || gInExtra);
        if (pal60 != gPal60) {
            gPal60 = pal60;
            gPalGroup = 0; gPalGroupVb = 0; gPalLastKick = 0; gPal60Target = 1; gPalTickPending = false; gPalFieldAcc = 0;
            Log("frame rate: PAL at 60 %s", pal60 ? "on (fields at 60 a second in the map, the game's time at 50)" : "off (fields at 50 a second)");
        }
        if (gPal60) ++gPalGroupVb;
    }
    if (Exact60() && (R32(kHalfRate) || gInExtra)) W32(V(0x23B8F8), gPal60 ? gPal60Target : gTickAt30 ? 2 : 1);   // exact 60: DG's frame target, a picture every VBlank (the pace guard's pause: 2)
    RunIntc(2);                                                              // VBlank start
    if (gPal60) {                                                            // the game's fields: 5 of 6 VBlanks (50 a second)
        gPalFieldAcc += 5;
        if (gPalFieldAcc >= 6) gPalFieldAcc -= 6;
        else W32(kVsyncTotal, R32(kVsyncTotal) - 1);
    }
    NativeSoundIop();
    if (iop::Running()) {
        if (gFrameNo % 600 == 0) {
            Log("sound: %s; %s", iop::Stats().c_str(), IopReadStats().c_str());
            if (const char* p = getenv("SIM_SPU_DUMP")) iop::DumpSoundMemory(p);     // checks: the 2 MB of sound memory
        }
        // nobody takes the sound (no sound device, the simulation): it is mixed here at the game's
        // pace, 48 kHz over its fields (59.94 or 50 a second), so the IOP's time moves with the game's;
        // SIM_WAV=path (tests) writes it to a WAV file
        if (!gSoundPulled) {
            static long long mixed = 0;
            static double fieldSec = 0;                       // the fields' time (PAL at 60: 60 a second)
            fieldSec += gPal60 ? 1.0 / 60 : gPal ? 1.0 / 50 : 1001.0 / 60000;
            const long long due = (long long)(fieldSec * 48000);
            const int n = (int)std::min<long long>(due - mixed, 1600);
            mixed = due;
            static int16_t buf[1600 * 2];
            if (n > 0) {
                memset(buf, 0, sizeof(int16_t) * 2 * n);
                iop::Mix(buf, n);
            }
            static FILE* wav = nullptr; static uint32_t wavBytes = 0; static bool tried = false;
            if (!tried) {
                tried = true;
                if (const char* p = DevEnv("SIM_WAV")) {
                    if ((wav = fopen(p, "wb"))) {
                        const uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                                                 0x80, 0xBB, 0, 0, 0, 0xEE, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
                        fwrite(hdr, 1, 44, wav);
                    }
                }
            }
            if (wav && n > 0) {
                fwrite(buf, 4, n, wav);
                wavBytes += n * 4;
                if (gFrameNo % 60 == 0) {
                    const uint32_t riff = wavBytes + 36;
                    fseek(wav, 4, SEEK_SET); fwrite(&riff, 4, 1, wav); fseek(wav, 40, SEEK_SET); fwrite(&wavBytes, 4, 1, wav); fseek(wav, 0, SEEK_END);
                    fflush(wav);
                }
            }
        }
    }
    ServeRpcs();
    DeliverNetEvents();
    SimFrameHooks();
    if (P.frame) { const long long h0 = RealQpc(); P.frame(gFrameNo); PaceHook(RealQpc() - h0); }
    static const int statsEvery = DevEnv("SIM_THREAD_STATS") ? atoi(DevEnv("SIM_THREAD_STATS")) : 0;
    if (statsEvery > 0 && gFrameNo % statsEvery == 0) {      // each thread's runs and time with the baton, and its state
        std::string t;
        for (auto& th : gThreads) {
            char b[128];
            char w[24] = "";
            if (th.waitSema >= 0) snprintf(w, sizeof w, " sema %d", th.waitSema);
            snprintf(b, sizeof b, " %d(p%d s%d%s):%lld/%.0fms", th.id, th.prio, th.state, w, th.slices, th.runTicks * 1000.0 / QpcFreq());
            t += b; th.slices = 0; th.runTicks = 0;
            if (const double cpu = PerfThreadCpuMs(th.id, th.os)) { snprintf(b, sizeof b, " cpu %.0fms", cpu); t += b; }   // perf.h
        }
        PerfCpuLine(statsEvery);                                                                                            // perf.h
        static long long lastAt = 0;
        const long long nowAt = QpcNow();
        Log("threads in %d frames (%.0f ms):%s; vu1 %llu runs %.0f ms; vu programs %lld recompiled, %lld interpreted; "
            "DMA starts VIF0 %.0f ms, VIF1 %.0f ms, GIF %.0f ms", statsEvery,
            lastAt ? (nowAt - lastAt) * 1000.0 / QpcFreq() : 0.0, t.c_str(), (unsigned long long)ps2::gVu1Runs, ps2::gVu1Ns / 1e6,
            ps2::gVuCompiledRuns, ps2::gVuInterpRuns, ps2::gDmaNs[0] / 1e6, ps2::gDmaNs[1] / 1e6, ps2::gDmaNs[2] / 1e6);
        lastAt = nowAt; ps2::gVu1Runs = 0; ps2::gVu1Ns = 0; ps2::gVuCompiledRuns = 0; ps2::gVuInterpRuns = 0;
        ps2::gDmaNs[0] = ps2::gDmaNs[1] = ps2::gDmaNs[2] = 0;
        if (ps2::GsThreadOn()) Log("gs thread: %s", ps2::GsThreadStats().c_str());
        if (ps2::gVifStatsOn) Log("vif unpacks (count/quadwords): %s", ps2::VifStats().c_str());
    }
}
// the page code at 0x2DA680: the page images' first 64 bytes tell which one the game loaded
uint64_t Fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}
struct PageSig { std::string page; uint64_t head; };
std::vector<PageSig> gPageSigs;
uint8_t gOvlHead[64]; bool gOvlSeen = false;
void NativeLoadPageSigs() {
    const char* v = ps2::SelectedVersion() ? ps2::SelectedVersion() : "US";
    for (int i = 0; i < kPageHeadCount; ++i)
        if (!strcmp(kPageHeads[i].version, v)) gPageSigs.push_back({kPageHeads[i].page, kPageHeads[i].head});
    Log("native: %zu pages of the %s program recompiled", gPageSigs.size(), v);
}
bool PageHeadIs(const char* page) {                    // the page area holds this page's image (its first 64 bytes)
    const uint64_t head = Fnv1a64(gMem + PageLo(), 64);
    for (auto& sg : gPageSigs) if (sg.head == head && sg.page == page) return true;
    return false;
}
void NativeOverlayCheck() {
    const uint32_t at = PageLo();
    if (gOvlSeen && !memcmp(gMem + at, gOvlHead, 64)) return;
    memcpy(gOvlHead, gMem + at, 64); gOvlSeen = true;
    const uint64_t head = Fnv1a64(gOvlHead, 64);
    for (auto& sg : gPageSigs)
        if (sg.head == head) {
            const bool ok = ps2::SelectOverlay(sg.page.c_str());
            gOverlayName = sg.page; gPageNt = sg.page == "ntmenu2";
            PageMapSelect(sg.page);
            ++gPageGen;
            if (gPageNt) { UpstreamMenu(); MaxPlayersMenu(); BigGameEnd(); gNtLoaded = true; }
            Log("native: page %s loaded at %#x, its code %s", sg.page.c_str(), at, ok ? "selected" : "NOT recompiled");
            return;
        }
    Log("native: the code at %#x is none of the page images (%08x %08x %08x %08x)", at, R32(at), R32(at + 4), R32(at + 8), R32(at + 12));
}
// The servers MGO1 connects to come from its menu script (ntmenu2): per region case of its
// switch on varbuf[0x10F5], the online root's -u (web base), -A (gate, STUN) and -g (their
// ports), each value in a fixed slot (07 LL text\0 padding; 01 / 08 a 16-bit value). When the
// game sets a script (0x125500, from the stage loader's gcx handler 0x1262F0), the retail
// Konami values are rewritten with SaveMGO's, in their slots, as SaveMGO's PS2 patch does on
// the disc; then <images>\servers.ini, if present, as tools\mgo1_servers.py writes it
// (docs/SERVERS.md). The disc's files are not changed.
struct ScriptCase { std::vector<std::pair<uint32_t, uint32_t>> web, addr; std::vector<uint32_t> port; };   // text slots {at, room}; port values
std::map<int, ScriptCase> ScriptCases(uint32_t g, uint32_t n) {
    std::map<int, ScriptCase> out;
    auto B = [&](uint32_t o) -> uint32_t { return o < n ? gMem[g + o] : 0; };
    auto find = [&](const uint8_t* pat, uint32_t k, uint32_t from, uint32_t to) -> int64_t {
        for (uint32_t o = from; o + k <= n && o + k <= to; ++o) if (!memcmp(gMem + g + o, pat, k)) return o;
        return -1;
    };
    // each case of the region switch (its variable is the version's: varbuf 0x10F5 in the US and EU
    // scripts, 0x1199 in the Japanese ones): the case tag 63 C1+n, then the online root [ccbefe]
    // and its class, past the script's header
    static const uint8_t root[4] = {0x06, 0xFE, 0xBE, 0xCC};
    uint32_t p0 = 4;
    while (p0 + 4 <= n && R32(g + p0) != 0xffffffffu) p0 += 4;       // the proc table's end
    for (int64_t p = find(root, 4, p0, n); p >= 0; p = find(root, 4, (uint32_t)p + 4, n)) {
        if (B((uint32_t)p + 4) != 0x06) continue;
        int cs = -1;
        for (uint32_t back = 2; back < 0x40 && (uint32_t)p >= back; ++back)
            if (B((uint32_t)p - back) == 0x63 && B((uint32_t)p - back + 1) >= 0xC1 && B((uint32_t)p - back + 1) < 0xC9) { cs = (int)B((uint32_t)p - back + 1) - 0xC1; break; }
        if (cs < 0 || out.count(cs)) continue;
        uint32_t q = (uint32_t)p + 8;
        ScriptCase& sc = out[cs];
        while (q < n && (B(q) & 0xF0) == 0x50) {                  // options: 0x5N size N (0xD: next byte), letter, values
            uint32_t size = B(q) & 0x0F; ++q;
            if (size == 0x0D) size = B(q++);
            const char letter = (char)B(q);
            uint32_t v = q + 1;
            const uint32_t end = q + size;
            while (v < end) {
                const uint32_t t = B(v);
                if (t == 0x07) { if (letter == 'u') sc.web.push_back({v + 2, B(v + 1)}); else if (letter == 'A') sc.addr.push_back({v + 2, B(v + 1)}); v += 2 + B(v + 1); }
                else if (t == 0x01 || t == 0x08) { if (letter == 'g') sc.port.push_back(v); v += 3; }
                else if (t == 0x09 || t == 0x0A) v += 5;
                else if (t >= 0xC0) v += 1;
                else break;
            }
            q = end;
        }
    }
    return out;
}
std::string SlotText(uint32_t g, std::pair<uint32_t, uint32_t> s) { return std::string((const char*)gMem + g + s.first, strnlen((const char*)gMem + g + s.first, s.second)); }
bool PutSlot(uint32_t g, std::pair<uint32_t, uint32_t> s, const std::string& v, const char* what, int cs) {
    const std::string was = SlotText(g, s);
    if (v.size() + 1 > s.second) { Log("servers: case %d %s \"%s\" is %zu characters, its slot holds %u: left \"%s\"", cs, what, v.c_str(), v.size(), s.second - 1, was.c_str()); return false; }
    if (was == v) return false;
    memset(gMem + g + s.first, 0, s.second);
    memcpy(gMem + g + s.first, v.data(), v.size());
    Log("servers: case %d %s %s -> %s", cs, what, was.c_str(), v.c_str());
    return true;
}
void PutPort(uint32_t g, uint32_t at, int v, const char* what, int cs) {
    if (v <= 0 || v >= 65536) { Log("servers: case %d %s %d is not a port", cs, what, v); return; }
    W8(g + at, v < 0x8000 ? 0x01 : 0x08);                       // int16, or uint16 above 32767
    W8(g + at + 1, (uint8_t)(v & 0xff)); W8(g + at + 2, (uint8_t)(v >> 8));
    Log("servers: case %d %s -> %d", cs, what, v);
}
// A page's script from another version (its texts and their glyphs, docs/LANGUAGES.md): the loaded
// script is told by its first 256 bytes among this version's own (MGS\NET\STAGE_N.DAT), and the
// same page's script of the other version (Platform::scriptsFrom, or MGO1_SCRIPTS_FROM: its folder or
// disc image) goes in its place when it fits there, else in a block of the platform's own
struct OtherScripts {
    bool tried = false;
    struct Own { std::string page; uint32_t size; uint64_t hash; };
    std::map<uint64_t, std::vector<Own>> own;                   // this version's, by their first 256 bytes
    std::map<std::string, std::vector<uint8_t>> other;          // the other version's, by page
    std::map<std::string, std::pair<uint32_t, uint32_t>> placed;  // blocks of the platform (scripts too large for their
                                                                  // place): one for the resident pages (one loaded at a time)
} gOtherScripts;
void ScriptFromVersion(Ctx& c) {
    OtherScripts& o = gOtherScripts;
    if (!o.tried) {
        o.tried = true;
        const char* env = DevEnv("MGO1_SCRIPTS_FROM");
        const std::string from = env ? env : P.scriptsFrom ? P.scriptsFrom : "";
        if (from.empty()) return;
        DiscSpan mine, theirs;
        std::map<std::string, std::vector<uint8_t>> own;
        std::string err;
        if (!DiscFind("MGS\\NET\\STAGE_N.DAT", mine) || !StageDatScripts(mine.host, mine.base, mine.size, own, err)) {
            Log("scripts: this version's STAGE_N.DAT not read (%s): the texts stay this version's", err.c_str());
            return;
        }
        if (!DiscFindIn(from, "MGS\\NET\\STAGE_N.DAT", theirs) || !StageDatScripts(theirs.host, theirs.base, theirs.size, o.other, err)) {
            Log("scripts: no STAGE_N.DAT read in %s (%s): the texts stay this version's", from.c_str(), err.c_str());
            o.other.clear();
            return;
        }
        for (auto& kv : own)
            if (kv.second.size() >= 256)
                o.own[Fnv1a64(kv.second.data(), 256)].push_back({kv.first, (uint32_t)kv.second.size(), Fnv1a64(kv.second.data(), kv.second.size())});
        Log("scripts: %zu pages of this version, %zu of the other one's (%s)", own.size(), o.other.size(), from.c_str());
    }
    if (o.other.empty()) return;
    const uint32_t g = (uint32_t)c.r[4] & 0x01ffffff;
    if (g < 0x100000 || g + 256 > 0x02000000) return;
    auto it = o.own.find(Fnv1a64(gMem + g, 256));
    const OtherScripts::Own* mine = nullptr;
    if (it != o.own.end())
        for (auto& e : it->second)                            // pages alike at their start: the whole script tells
            if (g + e.size <= 0x02000000 && Fnv1a64(gMem + g, e.size) == e.hash) { mine = &e; break; }
    if (!mine) { Log("scripts: the script at %08x is none of this version's pages", g); return; }
    const std::string& page = mine->page;
    auto th = o.other.find(page);
    if (th == o.other.end()) { Log("scripts: %s: the other version has no script for it", page.c_str()); return; }
    const std::vector<uint8_t>& d = th->second;
    uint32_t at = g;
    if (d.size() > mine->size) {                             // larger: a block of the platform's
        auto& blk = o.placed[page.rfind("r_", 0) == 0 ? std::string("resident") : page];
        if (blk.first && blk.second < d.size()) { Free(blk.first); blk = {0, 0}; }
        if (!blk.first) blk = {Alloc((uint32_t)d.size(), 64), (uint32_t)d.size()};
        if (!blk.first) { Log("scripts: %s: no room for the other version's script (%zu bytes)", page.c_str(), d.size()); return; }
        at = blk.first;
        c.r[4] = (uint64_t)(int64_t)(int32_t)at;
    }
    memcpy(gMem + at, d.data(), d.size());
    static std::set<std::string> told;
    if (told.insert(page).second)
        Log("scripts: %s: the other version's script (%zu bytes, %s) runs in place of this one's (%u bytes)", page.c_str(), d.size(),
            at == g ? "in its place" : "in a block of the platform", mine->size);
}
// The texts with a language the disc has none in (Platform::scriptEnglish): the pages choose between
// their two text sets with (linkvarbuf[4] == 1), English first (bytes 37 11 80 00 04 C2 AB A0: the
// expression, link variable 4, the number 1, ==); made on the language chosen, the test takes the
// English set while the program's pictures (titles, buttons) follow link variable 4. The resident pages
// test the disc's other language (== 5 North American, == 2 European) and take English otherwise.
// Only the procedures' code is searched (jayveer's Gcx: the block header's first word places the main
// procedure's offset, the procedures follow it; each one 0x8N size, 0x8D size8, 0x8E size16; the main
// one a 32-bit size).
void NativeScriptLanguage(uint32_t g) {
    if (!P.scriptEnglish || P.gameLanguage < 2 || P.gameLanguage > 5) return;
    g &= 0x01ffffff;
    if (g < 0x100000 || g + 0x40 >= 0x02000000) return;
    uint32_t n = 0;
    while (n < 0x400 && R32(g + 4 + 4 * n) != 0xffffffffu) ++n;
    if (n >= 0x400) return;
    const uint32_t blk = g + 4 + 4 * (n + 1), procOff = R32(blk);
    if (procOff > 0x100000 || blk + procOff + 8 >= 0x02000000) return;
    const uint32_t code = blk + procOff + 4, mainAt = code + R32(blk + procOff);
    if (mainAt + 4 >= 0x02000000) return;
    uint32_t end = mainAt + 4 + R32(mainAt);
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t at = code + (R32(g + 4 + 4 * k) & 0xffffff);
        if (at + 4 >= 0x02000000) return;
        const uint8_t m = R8(at) & 0x0f;
        end = std::max(end, m == 0x0d ? at + 2 + R8(at + 1) : m == 0x0e ? at + 3 + (R8(at + 1) | R8(at + 2) << 8) : at + 1 + m);
    }
    if (end <= code || end - code > 0x40000 || end > 0x02000000) return;
    static const uint8_t test[8] = {0x37, 0x11, 0x80, 0x00, 0x04, 0xC2, 0xAB, 0xA0};
    int changed = 0;
    for (uint32_t o = code; o + 8 <= end; ++o)
        if (gMem[o] == 0x37 && !memcmp(gMem + o, test, sizeof test)) { gMem[o + 5] = (uint8_t)(0xC1 + P.gameLanguage); ++changed; }
    static int told = 0;
    if (changed && told++ < 40)
        Log("language: the page script at %08x (%u procedures): %d text choices made on language %d, its English texts", g, n, changed, P.gameLanguage);
}
// ---- Another version's texts in this version's scripts (Platform::textsFrom, docs/LANGUAGES.md): the North
// American and European programs' pages have the same code; each holds English and a second set of texts
// (US Spanish, EU French) and their string tables differ by a few entries. For each page: the strings
// equal in both versions pair the English sets (a longest common subsequence, gaps of equal length paired
// in order); each version's second set is found from its English one by the pages' own choices: the
// language tests' two branches (`37 11 80 00 04 CN AB A0`, then a proc, then -e and a proc) and the
// procedures alike but for their strings (the radio's two lists), each a shift between the two sets.
// The page's second set takes the other version's (English where the other has an empty one) and its
// English entries point at the same texts: both branches show them. The strings go in again unciphered
// (seed 0); the code, the tables' kinds and the font data stay this version's.
namespace texts {
struct Gcx {
    std::vector<uint8_t> d;                                  // the script, its strings deciphered, seed 0
    uint32_t n = 0, blk = 0, procOff = 0, rtab = 0, sres = 0, font = 0;
    std::vector<uint32_t> tab;
    std::vector<std::string> strs;
};
uint32_t Get32(const std::vector<uint8_t>& d, size_t o) { uint32_t v = 0; if (o + 4 <= d.size()) memcpy(&v, &d[o], 4); return v; }
void Put32(std::vector<uint8_t>& d, size_t o, uint32_t v) { if (o + 4 <= d.size()) memcpy(&d[o], &v, 4); }
bool Parse(const uint8_t* p, size_t size, Gcx& g) {
    g = Gcx();
    g.d.assign(p, p + size);
    while (4 + 4 * (size_t)g.n + 4 <= size && Get32(g.d, 4 + 4 * g.n) != 0xffffffffu) ++g.n;
    g.blk = 4 + 4 * (g.n + 1);
    if (g.blk + 20 > size) return false;
    g.procOff = Get32(g.d, g.blk); g.rtab = Get32(g.d, g.blk + 4); g.sres = Get32(g.d, g.blk + 8); g.font = Get32(g.d, g.blk + 12);
    uint32_t seed = Get32(g.d, g.blk + 16);
    if (!(g.rtab <= g.sres && g.sres <= g.font && g.font <= g.procOff && g.blk + g.procOff + 8 <= size && !((g.sres - g.rtab) & 3))) return false;
    for (uint32_t k = g.sres; seed && k < g.font; ++k) { seed = seed * 0x7D2B89DDu + 0xCF9u; g.d[g.blk + k] ^= (uint8_t)(seed >> 15); }
    Put32(g.d, g.blk + 16, 0);
    const uint32_t nres = (g.sres - g.rtab) / 4, span = g.font - g.sres;
    for (uint32_t k = 0; k < nres; ++k) g.tab.push_back(Get32(g.d, g.blk + g.rtab + 4 * k));
    for (uint32_t k = 0; k < nres; ++k) {
        const uint32_t a = g.tab[k] & 0xffffff, b = k + 1 < nres ? (g.tab[k + 1] & 0xffffff) : span;
        if (a > span || b > span || b < a) return false;
        g.strs.emplace_back((const char*)&g.d[g.blk + g.sres + a], b - a);
    }
    return true;
}
// a procedure's body [from, to): 0x8N size N, 0x8D size8, 0x8E size16
std::pair<uint32_t, uint32_t> Body(const Gcx& g, uint32_t at) {
    if (at + 3 > g.d.size()) return {0, 0};
    const uint8_t m = g.d[at] & 0x0f;
    uint32_t s = at + 1, n = m;
    if (m == 0x0d) { s = at + 2; n = g.d[at + 1]; }
    else if (m == 0x0e) { s = at + 3; n = g.d[at + 1] | g.d[at + 2] << 8; }
    if (s + n > g.d.size()) return {0, 0};
    return {s, s + n};
}
// the string references (0x0E, 16-bit index) of a span, and the span with them zeroed
std::vector<uint32_t> Refs(const Gcx& g, uint32_t a, uint32_t b, std::string* masked = nullptr) {
    std::vector<uint32_t> out;
    if (masked) masked->assign((const char*)&g.d[a], b - a);
    for (uint32_t o = a; o + 3 <= b; ++o)
        if (g.d[o] == 0x0e) {
            out.push_back(g.d[o + 1] | g.d[o + 2] << 8);
            if (masked) (*masked)[o + 1 - a] = (*masked)[o + 2 - a] = 0;
            o += 2;
        }
    return out;
}
// pairs of string lists that hold the same texts in two languages
std::vector<std::pair<std::vector<uint32_t>, std::vector<uint32_t>>> Choices(const Gcx& g) {
    std::vector<std::pair<std::vector<uint32_t>, std::vector<uint32_t>>> out;
    const uint32_t code = g.blk + g.procOff + 4;
    static const uint8_t test[5] = {0x37, 0x11, 0x80, 0x00, 0x04};
    for (uint32_t o = code; o + 8 < g.d.size(); ++o) {
        if (memcmp(&g.d[o], test, 5) || g.d[o + 5] < 0xC1 || g.d[o + 6] != 0xAB || g.d[o + 7] != 0xA0) continue;
        const uint32_t p = o + 8;
        if ((g.d[p] & 0xf0) != 0x80) continue;
        const auto th = Body(g, p);
        if (!th.second) continue;
        uint32_t q = th.second;
        if (q + 3 >= g.d.size() || (g.d[q] & 0xf0) != 0x50) continue;
        const uint32_t q2 = (g.d[q] & 0x0f) == 0x0d ? q + 2 : q + 1;
        if (q2 + 2 >= g.d.size() || g.d[q2] != 'e') continue;
        const auto el = Body(g, q2 + 1);
        if (!el.second) continue;
        auto a = Refs(g, th.first, th.second), b = Refs(g, el.first, el.second);
        if (!a.empty() && a.size() == b.size()) out.push_back({a, b});
    }
    // procedures alike but for their strings: the table's and the main one
    std::vector<std::pair<uint32_t, uint32_t>> bodies;
    for (uint32_t k = 0; k < g.n; ++k) bodies.push_back(Body(g, code + (Get32(g.d, 4 + 4 * k) & 0xffffff)));
    std::vector<std::string> masks(bodies.size());
    std::vector<std::vector<uint32_t>> refs(bodies.size());
    for (size_t k = 0; k < bodies.size(); ++k) if (bodies[k].second) refs[k] = Refs(g, bodies[k].first, bodies[k].second, &masks[k]);
    for (size_t x = 0; x < bodies.size(); ++x)
        for (size_t y = x + 1; y < bodies.size(); ++y) {
            if (refs[x].empty() || refs[x].size() != refs[y].size() || masks[x] != masks[y]) continue;
            const int64_t d0 = (int64_t)refs[y][0] - refs[x][0];
            bool same = d0 != 0;
            for (size_t k = 0; same && k < refs[x].size(); ++k) same = (int64_t)refs[y][k] - refs[x][k] == d0;
            if (same) out.push_back({refs[x], refs[y]});
        }
    return out;
}
// each second-set index -> its English one; `shared`: the indices of strings the other version has too
std::map<uint32_t, uint32_t> SecondSet(const Gcx& g, const std::vector<bool>& shared) {
    std::map<uint32_t, uint32_t> out;
    const uint32_t nres = (uint32_t)g.strs.size();
    for (auto& [a, b] : Choices(g)) {
        size_t sa = 0, sb = 0;
        for (uint32_t i : a) sa += i < nres && shared[i];
        for (uint32_t i : b) sb += i < nres && shared[i];
        const auto& eng = sa >= sb ? a : b;
        const auto& sec = sa >= sb ? b : a;
        const int64_t D = (int64_t)sec[0] - eng[0];
        bool ok = D != 0;
        for (size_t k = 0; ok && k < eng.size(); ++k) ok = (int64_t)sec[k] - eng[k] == D;
        if (!ok) continue;
        const uint32_t e0 = *std::min_element(eng.begin(), eng.end()), s0 = *std::min_element(sec.begin(), sec.end());
        const uint32_t lo = e0, hi = D > 0 ? s0 : e0 + (e0 - s0);
        for (uint32_t e = lo; e < hi; ++e) {
            const int64_t s = e + D;
            if (s >= 0 && s < nres && e < nres) out.emplace((uint32_t)s, e);
        }
    }
    return out;
}
// the strings equal in both: a longest common subsequence, gaps of the same length paired in order
std::map<uint32_t, uint32_t> Pair(const Gcx& a, const Gcx& b, std::vector<bool>& sharedA, std::vector<bool>& sharedB) {
    const size_t n = a.strs.size(), m = b.strs.size();
    std::map<uint32_t, uint32_t> out;
    sharedA.assign(n, false); sharedB.assign(m, false);
    if (!n || !m || n > 8192 || m > 8192) return out;
    std::vector<uint16_t> L((n + 1) * (m + 1), 0);
    for (size_t i = n; i-- > 0;)
        for (size_t j = m; j-- > 0;)
            L[i * (m + 1) + j] = a.strs[i] == b.strs[j] ? L[(i + 1) * (m + 1) + j + 1] + 1
                                                        : std::max(L[(i + 1) * (m + 1) + j], L[i * (m + 1) + j + 1]);
    std::vector<std::pair<uint32_t, uint32_t>> eq;
    for (size_t i = 0, j = 0; i < n && j < m;) {
        if (a.strs[i] == b.strs[j]) { eq.push_back({(uint32_t)i, (uint32_t)j}); ++i; ++j; }
        else if (L[(i + 1) * (m + 1) + j] >= L[i * (m + 1) + j + 1]) ++i;
        else ++j;
    }
    uint32_t pi = 0, pj = 0;
    for (size_t k = 0; k <= eq.size(); ++k) {
        const uint32_t ei = k < eq.size() ? eq[k].first : (uint32_t)n, ej = k < eq.size() ? eq[k].second : (uint32_t)m;
        if (ei - pi == ej - pj)
            for (uint32_t t = 0; t < ei - pi; ++t) out[pi + t] = pj + t;
        if (k < eq.size()) { out[ei] = ej; sharedA[ei] = true; sharedB[ej] = true; pi = ei + 1; pj = ej + 1; }
    }
    return out;
}
bool Empty(const std::string& s) { return s.find_first_not_of('\0') == std::string::npos; }
// the page's script with the other version's second set; counts for the log
bool Build(const Gcx& own, const Gcx& other, std::vector<uint8_t>& out, int& fromOther, int& english) {
    std::vector<bool> sharedO, sharedT;
    const auto o2t = Pair(own, other, sharedO, sharedT);
    const auto secO = SecondSet(own, sharedO), secT = SecondSet(other, sharedT);
    if (secO.empty() || secT.empty()) return false;
    std::map<uint32_t, uint32_t> engToSecT;
    for (auto& [s, e] : secT) engToSecT.emplace(e, s);
    const uint32_t nres = (uint32_t)own.strs.size();
    std::vector<std::string> now(own.strs);
    std::vector<int64_t> alias(nres, -1);
    fromOther = english = 0;
    for (auto& [s, e] : secO) {
        std::string t;
        auto p = o2t.find(e);
        if (p != o2t.end()) { auto q = engToSecT.find(p->second); if (q != engToSecT.end() && !Empty(other.strs[q->second])) t = other.strs[q->second]; }
        if (!t.empty()) ++fromOther;
        else if (!Empty(own.strs[e])) { t = own.strs[e]; ++english; }
        else continue;
        now[s] = t;
        if (alias[e] < 0 && !secO.count(e)) alias[e] = s;              // the English entry shows the same text
    }
    std::string blob;
    std::vector<uint32_t> off(nres, 0);
    for (uint32_t k = 0; k < nres; ++k) if (alias[k] < 0) { off[k] = (uint32_t)blob.size(); blob += now[k]; }
    for (uint32_t k = 0; k < nres; ++k) if (alias[k] >= 0) off[k] = off[(size_t)alias[k]];
    if (blob.size() >= 0x1000000) return false;
    const uint32_t fontSize = own.procOff - own.font;
    const uint32_t pad = (uint32_t)((((int64_t)own.procOff - fontSize - (own.sres + (int64_t)blob.size())) % 16 + 16) % 16);
    blob.append(pad, '\0');
    const uint32_t font = own.sres + (uint32_t)blob.size(), procOff = font + fontSize;
    out.assign(own.d.begin(), own.d.begin() + own.blk + own.sres);
    for (uint32_t k = 0; k < nres; ++k) Put32(out, own.blk + own.rtab + 4 * k, (own.tab[k] & 0xff000000u) | off[k]);
    Put32(out, own.blk, procOff); Put32(out, own.blk + 12, font); Put32(out, own.blk + 16, 0);
    out.insert(out.end(), blob.begin(), blob.end());
    out.insert(out.end(), own.d.begin() + own.blk + own.font, own.d.end());
    return true;
}
}  // namespace texts
struct OtherTexts {
    bool tried = false;
    std::map<uint64_t, std::vector<std::pair<std::string, uint32_t>>> own;   // this version's pages by their first 256 bytes: name, size
    std::map<std::string, uint64_t> ownHash;                                 // each page's whole script
    std::map<std::string, std::vector<uint8_t>> ownBytes, other;
    std::map<std::string, std::vector<uint8_t>> built;                       // the page's script with the other's texts
} gOtherTexts;
void TextsFromVersion(Ctx& c) {
    OtherTexts& o = gOtherTexts;
    if (!o.tried) {
        o.tried = true;
        const char* env = DevEnv("MGO1_TEXTS_FROM");
        const std::string from = env ? env : P.textsFrom ? P.textsFrom : "";
        if (from.empty()) return;
        DiscSpan mine, theirs;
        std::string err;
        if (!DiscFind("MGS\\NET\\STAGE_N.DAT", mine) || !StageDatScripts(mine.host, mine.base, mine.size, o.ownBytes, err)) {
            Log("texts: this version's STAGE_N.DAT not read (%s): its own texts", err.c_str());
            return;
        }
        if (!DiscFindIn(from, "MGS\\NET\\STAGE_N.DAT", theirs) || !StageDatScripts(theirs.host, theirs.base, theirs.size, o.other, err)) {
            Log("texts: no STAGE_N.DAT read in %s (%s): this version's own texts", from.c_str(), err.c_str());
            o.other.clear();
            return;
        }
        for (auto& kv : o.ownBytes)
            if (kv.second.size() >= 256) {
                o.own[Fnv1a64(kv.second.data(), 256)].push_back({kv.first, (uint32_t)kv.second.size()});
                o.ownHash[kv.first] = Fnv1a64(kv.second.data(), kv.second.size());
            }
        Log("texts: the second texts of %s's pages in this version's (%zu pages here, %zu there)", from.c_str(), o.ownBytes.size(), o.other.size());
    }
    if (o.other.empty()) return;
    const uint32_t g = (uint32_t)c.r[4] & 0x01ffffff;
    if (g < 0x100000 || g + 256 > 0x02000000) return;
    auto it = o.own.find(Fnv1a64(gMem + g, 256));
    if (it == o.own.end()) return;
    const std::string* page = nullptr; uint32_t size = 0;
    for (auto& [name, n] : it->second)
        if (g + n <= 0x02000000 && Fnv1a64(gMem + g, n) == o.ownHash[name]) { page = &name; size = n; break; }
    if (!page) return;
    auto b = o.built.find(*page);
    if (b == o.built.end()) {
        std::vector<uint8_t> out;
        texts::Gcx own, other;
        int fromOther = 0, english = 0;
        auto th = o.other.find(*page);
        const bool ok = th != o.other.end() && texts::Parse(o.ownBytes[*page].data(), o.ownBytes[*page].size(), own) &&
                        texts::Parse(th->second.data(), th->second.size(), other) && texts::Build(own, other, out, fromOther, english) &&
                        out.size() <= size;
        if (ok) Log("texts: %s: %d texts of the other version's second set, %d English where it has none (%u bytes, %u before)", page->c_str(),
                    fromOther, english, (uint32_t)out.size(), size);
        else Log("texts: %s: this version's own texts (no pairing: %s)", page->c_str(), th == o.other.end() ? "the other version has no such page" : "its strings did not pair");
        if (!ok) out.clear();
        b = o.built.emplace(*page, std::move(out)).first;
    }
    if (b->second.empty()) return;
    memcpy(gMem + g, b->second.data(), b->second.size());
    memset(gMem + g + b->second.size(), 0, size - b->second.size());
}
// An extra map's texts in the scripts set for its rounds (map.ini texts=, scripttexts=; docs/EXTRA_MAPS.md): the
// weapons' and items' names the inventory shows are the stage script's string resources (callcode [7128d0] -w .. -i
// ..: entries `07 LL name 00`), each old name written over with the new one where it fits in its LL bytes (the rest
// NULs, LL kept); scripttexts: a part of any of its strings (the rules page's weapons' descriptions) written over
// with the new one, spaces after it to the old one's length. The script's strings then stay deciphered (its seed 0),
// as TextsFromVersion's
void ExtraScriptTexts(uint32_t g) {
    g &= 0x01ffffff;
    const int map = RoundMap() > 12 ? RoundMap() : gLoadedMap;
    if (map <= 12 || g < 0x100000 || g + 64 >= 0x02000000) return;
    const int rule = RoundRule();
    const auto names = ExtraMapsTexts(map, rule), parts = ExtraMapsScriptTexts(map, rule);
    if (names.empty() && parts.empty()) return;
    uint32_t n = 0;                                    // the procedures' table, then the block's header
    while (n < 4096 && g + 8 + 4 * n < 0x02000000 && R32(g + 4 + 4 * n) != 0xffffffffu) ++n;
    const uint32_t blk = g + 4 + 4 * (n + 1);
    if (n >= 4096 || blk + 20 >= 0x02000000) return;
    const uint32_t procOff = R32(blk), rtab = R32(blk + 4), sres = R32(blk + 8), font = R32(blk + 12);
    uint32_t seed = R32(blk + 16);
    if (!(rtab <= sres && sres <= font && font <= procOff && procOff < 0x100000 && blk + procOff < 0x02000000)) return;
    std::vector<uint8_t> d(gMem + blk + sres, gMem + blk + font);   // the strings, deciphered
    for (size_t k = 0; seed && k < d.size(); ++k) { seed = seed * 0x7D2B89DDu + 0xCF9u; d[k] ^= (uint8_t)(seed >> 15); }
    int put = 0, partsPut = 0;
    for (size_t k = 0; k + 3 < d.size(); ++k) {
        if (d[k] != 0x07) continue;
        const uint32_t len = d[k + 1];
        if (len < 2 || k + 2 + len > d.size() || d[k + 1 + len]) continue;
        for (auto& [from, to] : names)
            if (from.size() + 1 == len && !memcmp(&d[k + 2], from.c_str(), len) && to.size() + 1 <= len) {
                memset(&d[k + 2], 0, len);
                memcpy(&d[k + 2], to.c_str(), to.size());
                ++put;
                break;
            }
    }
    for (auto& [from, to] : parts) {
        if (from.empty() || to.size() > from.size()) continue;
        for (auto it = std::search(d.begin(), d.end(), from.begin(), from.end()); it != d.end();
             it = std::search(it + (ptrdiff_t)from.size(), d.end(), from.begin(), from.end())) {
            std::copy(to.begin(), to.end(), it);
            std::fill(it + (ptrdiff_t)to.size(), it + (ptrdiff_t)from.size(), (uint8_t)' ');
            ++partsPut;
        }
    }
    if (!put && !partsPut) return;
    memcpy(gMem + blk + sres, d.data(), d.size());
    W32(blk + 16, 0);
    const ExtraMap* em = ExtraMapById(map);
    Log("extra maps: %s: the script at %08x given its map's texts: %d names, %d parts", em ? em->name.c_str() : "?", g, put, partsPut);
}
void NativeScriptServers(uint32_t g) {
    g &= 0x01ffffff;
    if (g < 0x100000 || g >= 0x02000000) return;
    const uint32_t n = std::min<uint32_t>(0x40000, 0x02000000 - g);
    auto cases = ScriptCases(g, n);
    if (cases.empty()) return;
    // SaveMGO's patch: the retail US, EU and JP values (web base, gate, STUN). The Korean program's
    // (Konami's gate03 and /kr/ web base) go to SaveMGO's North American ones: Korean players meet
    // the NTSC ones (the same session dialect, docs/VERSIONS.md), their terms of use the US English
    // document (the send path below asks for it in place of text/policy_kr.txt)
    static const char* kSaveMgo[][6] = {
        {"https://mgs3sweb.konamionline.com/us/mgs3/", "http://savemgo.com/us/mgs3/", "mgs3sgate02.konamionline.com", "107.161.80.126", "mgs3sstun.konamionline.com", "15.204.66.207"},
        {"https://mgs3sweb.konamionline.com/eu/mgs3/", "http://savemgo.com/eu/mgs3/", "mgs3sgate04.konamionline.com", "107.161.80.126", "mgs3sstun.konamionline.com", "15.204.66.207"},
        {"https://mgs3sweb.konamionline.com/mgs3/", "http://savemgo.com/mgs3/", "mgs3sgate01.konamionline.com", "107.161.80.126", "mgs3sstun.konamionline.com", "15.204.66.207"},
        {"https://mgs3sweb.konamionline.com/kr/mgs3/", "http://savemgo.com/us/mgs3/", "mgs3sgate03.konamionline.com", "107.161.80.126", "mgs3sstun.konamionline.com", "15.204.66.207"}};
    for (auto& [cs, sc] : cases) {
        if (sc.web.empty()) continue;
        for (auto& r : kSaveMgo) {
            if (SlotText(g, sc.web[0]) != r[0]) continue;
            if (strstr(r[0], "/kr/")) gWebDocAlias = {{"text/policy_kr.txt", "text/policy_us_en.txt"}};
            if (!strstr(r[1], "/us/")) net::gWebFallback = {std::string(r[1]).substr(std::string(r[1]).find('/', 7)), "/us/mgs3/"};
            PutSlot(g, sc.web[0], r[1], "web", cs);
            if (sc.addr.size() > 0 && SlotText(g, sc.addr[0]) == r[2]) PutSlot(g, sc.addr[0], r[3], "gate", cs);
            if (sc.addr.size() > 1 && SlotText(g, sc.addr[1]) == r[4]) PutSlot(g, sc.addr[1], r[5], "stun", cs);
        }
    }
    // <images>\servers.ini (or the platform's file): [servers] case (3), web, gate, stun, gate_port, stun_port
    FILE* f = nullptr;
    const std::string ini = P.serversIni ? std::string(P.serversIni) : gImageDir + "\\servers.ini";
    if (fopen_s(&f, ini.c_str(), "r") != 0 || !f) return;
    std::map<std::string, std::string> kv;
    char line[512]; bool in = false;
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        const size_t sc = s.find_first_of(";#"); if (sc != std::string::npos) s.resize(sc);
        auto trim = [](std::string x) { const size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); };
        s = trim(s);
        if (s.empty()) continue;
        if (s[0] == '[') { in = _stricmp(s.c_str(), "[servers]") == 0; continue; }
        const size_t eq = s.find('=');
        if (in && eq != std::string::npos) kv[trim(s.substr(0, eq))] = trim(s.substr(eq + 1));
    }
    fclose(f);
    const int cs = kv.count("case") ? atoi(kv["case"].c_str()) : 3;
    auto it = cases.find(cs);
    if (it == cases.end()) { Log("servers: servers.ini names case %d, the script has none", cs); return; }
    ScriptCase& sc = it->second;
    if (kv.count("web") && !sc.web.empty()) PutSlot(g, sc.web[0], kv["web"], "web", cs);
    if (kv.count("gate") && sc.addr.size() > 0) PutSlot(g, sc.addr[0], kv["gate"], "gate", cs);
    if (kv.count("stun") && sc.addr.size() > 1) PutSlot(g, sc.addr[1], kv["stun"], "stun", cs);
    if (kv.count("gate_port") && sc.port.size() > 0) PutPort(g, sc.port[0], atoi(kv["gate_port"].c_str()), "gate port", cs);
    if (kv.count("stun_port") && sc.port.size() > 1) PutPort(g, sc.port[1], atoi(kv["stun_port"].c_str()), "stun port", cs);
}
// a kernel stub no handler knows by address: its number (addiu $v1, $zero, n; syscall)
int StubNumber(uint32_t addr) {
    if (addr < 0x100000 || addr + 16 > 0x02000000) return 0;
    int n = 0; bool have = false;
    for (int k = 0; k < 6; ++k) {
        const uint32_t w = R32(addr + 4 * k);
        if ((w & 0xffff0000) == 0x24030000) { n = (int16_t)(w & 0xffff); have = true; }
        if (w == 0x0000000c) return have ? n : 0;
    }
    return 0;
}
// the syscall table the SDK's _InitSys looks for (it installs its own find / copy handlers,
// then searches the kernel's RAM for their addresses): in the kernel's RAM above the
// scratchpad's alias (0 - 0x3FFF)
const uint32_t kSysTable = 0x00010000;
uint32_t gSysHandler[256] = {};
bool NativeSyscallN(Ctx& c, int n, uint32_t addr);
bool NativeSyscall(Ctx& c, uint32_t addr) {
    const int n = StubNumber(addr);
    return n && NativeSyscallN(c, n, addr);
}
bool NativeSyscallN(Ctx& c, int n, uint32_t addr) {
    const uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5], a2 = (uint32_t)c.r[6], a3 = (uint32_t)c.r[7];
    auto ret = [&](int64_t v) { c.r[2] = (uint64_t)v; return true; };
    switch (n) {
    case 0x3c: {                                       // SetupThread(gp, stack, size, args, root): the main thread's stack
        uint32_t stack = a1, size = a2;
        if (stack == 0xffffffffu) stack = 0x02000000 - size;
        EeThread* t = Thr(gCur);
        if (t) { t->stack = stack; t->size = size; t->gp = a0; }
        if (!gMainGp) gMainGp = a0;
        if (a3 && a3 + 0x44 + 32 < 0x02000000) {        // the arguments: argc 1, argv[0] the boot path
            static const char path[] = "cdrom0:\\MGS3_N.ELF;1";
            W32(a3, 1); W32(a3 + 4, a3 + 0x44); W32(a3 + 8, 0);
            memcpy(gMem + a3 + 0x44, path, sizeof path);
        }
        Log("native: SetupThread gp %08x stack %08x+%#x args %08x root %08x", a0, stack, size, a3, (uint32_t)c.r[8]);
        return ret(stack + size - 0x10); }
    case 0x3d:                                         // SetupHeap(start, size)
        gHeapEnd = a1 == 0xffffffffu ? (Thr(gCur) ? Thr(gCur)->stack : 0x01E00000) : a0 + a1;
        Log("native: SetupHeap %08x, end %08x", a0, gHeapEnd);
        return ret(gHeapEnd);
    case 0x3e: return ret(gHeapEnd);                   // EndOfHeap
    case 0xfc: case -0xfc: {                           // SetAlarm(hsync lines, handler(id, time, arg), arg)
        Alarm al; al.id = gAlarmNext++ & 0xff; al.time = (uint16_t)a0; al.fn = a1; al.arg = a2; al.live = true;
        al.due = QpcNow() + LineTicks(a0 & 0xffff);
        gAlarms.push_back(al);
        return ret(al.id); }
    case 0xfd: case -0xfd: {                           // ReleaseAlarm(id)
        for (auto& al : gAlarms) if (al.live && al.id == (int)a0) { al.live = false; return ret(0); }
        return ret(-1); }
    case 0x2b: case -0x2c: {                           // RotateThreadReadyQueue(priority): its first ready thread goes last
        EeThread* first = nullptr;
        for (auto& t : gThreads) if ((t.state == 2 || t.state == 1) && t.prio == (int)a0 && (!first || t.seq < first->seq)) first = &t;
        if (first) first->seq = ++gSeq;
        if (n > 0 && first && first->id == gCur) { const int id = gCur; first->state = 2; GiveBack(); Thr(id)->state = 1; }
        return ret((int)a0); }
    case -0x2a: {                                      // iChangeThreadPriority
        EeThread* t = Thr((int)a0);
        const int prev = t ? t->prio : -1;
        if (t) t->prio = (int)a1;
        return ret(prev); }
    case 0x35: case -0x36: {                           // CancelWakeupThread: the wakeups counted, then none
        EeThread* t = Thr((int)a0 ? (int)a0 : gCur);
        const int w = t ? t->wakeups : -1;
        if (t) t->wakeups = 0;
        return ret(w); }
    case 0x2d: case -0x2e: {                           // ReleaseWaitThread: a waiting thread made ready
        EeThread* t = Thr((int)a0);
        if (t && t->state == 4) { t->waitSema = -1; NativeReady(t); if (n > 0) NativePreempt(); return ret((int)a0); }
        return ret(-1); }
    case 0x4a: return ret(0);                          // SetOsdConfigParam
    case 0x73:                                         // SetVSyncFlag(flag*, csr*): written at each VSync
        gVSyncFlag = a0; gVSyncCsr = a1;
        return ret(0);
    case 0x74:                                         // SetSyscall(n, handler): the kernel's table in its RAM, where the SDK looks
        gSysHandler[a0 & 0xff] = a1;
        W32(kSysTable + 4 * (a0 & 0xff), a1);
        Log("native: SetSyscall(%#x, %08x)", a0, a1);
        return ret(0);
    case 0x7a: case -0x7a:                             // SifGetReg: the IOP booted (SMFLAG: SIF, CMD init, boot end)
        return ret(a0 == 4 ? 0x70000 : 0);
    case 0x79: case -0x79: return ret(0);              // SifSetReg
    case 0x7b: case 0x7d: return ret(0);               // ExecOSD, PSMode
    case 0x7e: return ret(0);                          // MachineType: a retail PS2
    }
    if (n > 0 && n < 256 && gSysHandler[n]) {          // a handler the game installed (SetSyscall): its own code, in kernel mode
        uint64_t r = 0;
        RunPs2Args(gSysHandler[n], {a0, a1, a2, a3}, &r, 50000000);
        return ret((int64_t)r);
    }
    if (Once(((uint64_t)10 << 40) | (uint32_t)(n & 0xffff)))
        Log("native: syscall %#x (stub %08x, a0 %08x a1 %08x a2 %08x a3 %08x, from %08x) not served, 0 returned", n, addr, a0, a1, a2, a3, (uint32_t)c.r[31] - 8);
    return ret(0);
}
// a syscall instruction inside recompiled code (crt0, the SDK's kernel patches): the stub of
// that number serves it (its handlers are by address), else the native kernel by number
std::map<int, uint32_t> gStubOf;
void FindStubs() {
    for (uint32_t a = 0x100000; a + 16 < 0x201b00; a += 4) {
        const uint32_t w = R32(a);
        if ((w & 0xffff0000) != 0x24030000) continue;
        for (int k = 1; k <= 2; ++k)
            if (R32(a + 4 * k) == 0x0000000c && (R32(a + 4 * k + 4) == 0x03e00008 || R32(a + 4 * k + 8) == 0x03e00008)) {
                const int n = (int16_t)(w & 0xffff);
                if (!gStubOf.count(n)) gStubOf[n] = a;
                break;
            }
    }
}
bool Kernel(Ctx& c, uint32_t addr);
void PlatformSyscall(Ctx& c, uint32_t pc) {
    const int n = (int)(int32_t)c.r[3];
    auto it = gStubOf.find(n);
    uint32_t us = it != gStubOf.end() ? it->second : 0;                       // the platform serves the US stubs
    if (!gUsProgram && us) { auto st = gStubToUs.find(us); us = st != gStubToUs.end() ? st->second : UsStubAddr(n); }
    if (us && (Kernel(c, us) || Bridge(c, us))) return;
    if (gNative && NativeSyscallN(c, n, it != gStubOf.end() ? it->second : pc)) return;
    if (Once(((uint64_t)12 << 40) | (uint32_t)(n & 0xffff)))
        Log("syscall %#x at %08x not served (a0 %08x a1 %08x), 0 returned", n, pc, (uint32_t)c.r[4], (uint32_t)c.r[5]);
    c.r[2] = 0;
}
EeThread* PickReady() {
    EeThread* best = nullptr;
    for (auto& t : gThreads)
        if (t.state == 2 && (!best || t.prio < best->prio || (t.prio == best->prio && t.seq < best->seq))) best = &t;
    return best;
}
// ---- the profiler (MGO1_PROFILE=<seconds>, docs/LOGS.md): 1000 times a second the instruction pointer
// of the OS thread of the running PS2 thread (SuspendThread, GetThreadContext), counted by the function
// holding it (the x64 unwind table, stripped binaries too); every <seconds> the functions with the most
// samples as module+offset, named by the build's unstripped copy (tools/profile_names.py)
struct ProfSample { uint64_t rip, own; int thread; };   // own: the first frame in this module (the caller of a system function)
uint64_t gProfModLo = 0, gProfModHi = 0;                   // this module's image (the exe or MGO1_core.dll)
std::string ProfModule(uint64_t a, uint64_t& off) {
    HMODULE m = nullptr; char n[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, n, sizeof n);
        off = a - (uint64_t)m;
    } else off = a;
    const char* b = strrchr(n, '\\');
    return std::string(b ? b + 1 : n);
}
void ProfileTop(const char* tag, const char* what, const std::unordered_map<uint64_t, uint32_t>& fns, size_t total, size_t n) {
    std::vector<std::pair<uint64_t, uint32_t>> top(fns.begin(), fns.end());
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t k = 0; k < top.size() && k < n; ++k) {
        uint64_t off; const std::string m = ProfModule(top[k].first, off);
        Log("%s %s %5.2f %% %s+0x%llx", tag, what, 100.0 * top[k].second / std::max<size_t>(1, total), m.c_str(), (unsigned long long)off);
    }
}
uint64_t ProfFn(uint64_t rip) {                              // the start of the function holding rip
    DWORD64 base = 0;
    const PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(rip, &base, nullptr);
    return f ? base + f->BeginAddress : rip & ~0xfull;
}
constexpr int kProfGs = 1000;                               // a sample of the GS thread (gs_thread.h)
void ProfileReport(std::vector<ProfSample>& buf, int secs) {
    for (int gs = 0; gs < 2; ++gs) {                        // the PS2 threads, then the GS thread
        std::unordered_map<uint64_t, uint32_t> self, own;
        std::map<int, uint32_t> threads;
        size_t outside = 0, n = 0;
        for (const auto& s : buf) {
            if ((s.thread == kProfGs) != (gs == 1)) continue;
            ++n;
            ++self[ProfFn(s.rip)];
            if (s.rip < gProfModLo || s.rip >= gProfModHi) { ++outside; if (s.own) ++own[ProfFn(s.own)]; }
            ++threads[s.thread];
        }
        if (gs && !n) continue;
        const char* tag = gs ? "profile gs:" : "profile:";
        std::string th;
        if (!gs) for (const auto& t : threads) th += " " + std::to_string(t.first) + ":" + std::to_string(t.second * 100 / std::max<size_t>(1, n)) + "%";
        Log("%s %zu samples in %d s (%s%s), %zu %% outside this module (the system's, the C library's)", tag, n, secs, gs ? "the GS thread" : "PS2 threads",
            th.c_str(), outside * 100 / std::max<size_t>(1, n));
        ProfileTop(tag, "self", self, n, 40);
        ProfileTop(tag, "from", own, n, 15);                 // the samples outside, by their caller here
    }
}
DWORD WINAPI ProfileThread(LPVOID p) {
    const int secs = std::max(5, (int)(intptr_t)p);
    std::vector<ProfSample> buf;
    buf.reserve(1u << 20);                                   // no allocation while a thread is held
    ULONGLONG last = GetTickCount64();
    CONTEXT ctx;
    for (;;) {
        Sleep(1);
        for (int which = 0; which < 2; ++which) {           // the running PS2 thread, then the GS thread (when on)
        const int id = which ? kProfGs : gCur;
        const HANDLE h = which ? (HANDLE)ps2::gGsThreadHandle : id > 0 && id < 256 ? gOsOf[id] : nullptr;
        if (h && buf.size() < buf.capacity() && SuspendThread(h) != (DWORD)-1) {
            ctx.ContextFlags = CONTEXT_FULL;
            const BOOL ok = GetThreadContext(h, &ctx);
            uint64_t own = 0;
            if (ok && (ctx.Rip < gProfModLo || ctx.Rip >= gProfModHi)) {   // outside: walk up to this module (8 frames)
                CONTEXT c = ctx;
                for (int k = 0; k < 8 && !own; ++k) {
                    DWORD64 base = 0;
                    const PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
                    if (f) {
                        void* data = nullptr; DWORD64 frame = 0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &data, &frame, nullptr);
                    } else {                                 // a leaf: its return address on top of the stack
                        c.Rip = *(const uint64_t*)c.Rsp; c.Rsp += 8;
                    }
                    if (!c.Rip) break;
                    if (c.Rip >= gProfModLo && c.Rip < gProfModHi) own = c.Rip;
                }
            }
            ResumeThread(h);
            if (ok) buf.push_back({ctx.Rip, own, id});
        }
        }
        if (GetTickCount64() - last >= (ULONGLONG)secs * 1000) {
            ProfileReport(buf, secs);
            buf.clear();
            last = GetTickCount64();
        }
    }
    return 0;
}
void ProfileStart() {
    const char* e = getenv("MGO1_PROFILE");
    if (!e || atoi(e) <= 0) return;
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&ProfileStart, &self) && self) {
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)self + ((const IMAGE_DOS_HEADER*)self)->e_lfanew);
        gProfModLo = (uint64_t)self; gProfModHi = gProfModLo + nt->OptionalHeader.SizeOfImage;
    }
    if (HANDLE t = CreateThread(nullptr, 0, ProfileThread, (LPVOID)(intptr_t)atoi(e), 0, nullptr)) {
        SetThreadPriority(t, THREAD_PRIORITY_TIME_CRITICAL);
        CloseHandle(t);
        Log("profile: the running PS2 thread (and the GS thread when on) sampled 1000 times a second, a report every %d s", std::max(5, atoi(e)));
    }
}
// a VBlank started `late` (the PS2's ticks) after its time; a minute since the last line: the line
void PaceVBlank(long long late) {
    const long long real = RealQpc(), f = QpcFreq();
    FILETIME c, e, k, u;
    if (!gPaceRun.since) {
        gPaceRun = PaceRun(); gPaceRun.since = real;
        gPaceRun.cpuMs = GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? FileTimeMs(k, u) : 0.0;
        return;
    }
    ++gPaceRun.vblanks;
    if (late > f / 250) ++gPaceRun.late4;
    if (late > FieldTicks()) ++gPaceRun.lateField;
    if (late > gPaceRun.maxLate) gPaceRun.maxLate = late;
    if (real - gPaceRun.since < 60 * f) return;
    const double secs = (double)(real - gPaceRun.since) / (double)f, ms = 1000.0 / (double)f;
    const double cpu = GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? FileTimeMs(k, u) : gPaceRun.cpuMs;
    PROCESS_MEMORY_COUNTERS m{}; m.cb = sizeof m;
    const bool mem = GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof m);
    const double due = (double)f / (double)FieldTicks() / gTimeScale, rate = (double)gPaceRun.vblanks / secs;
    const double slow = rate < due ? (1.0 - rate / due) * 100.0 : 0.0;
    char memText[96] = "";
    if (mem) snprintf(memText, sizeof memText, ", %.0f MB (the most %.0f MB)", m.WorkingSetSize / 1048576.0, m.PeakWorkingSetSize / 1048576.0);
    Log("pace: %.0f s: %lld VBlanks (%.2f a second, %.2f due), %lld late more than 4 ms, %lld more than a field (the latest %.1f ms); "
        "the frame hook %.2f ms a VBlank (the longest %.1f ms); the process %.0f %% of a core%s%s", secs, gPaceRun.vblanks, rate, due,
        gPaceRun.late4, gPaceRun.lateField, gPaceRun.maxLate * ms * gTimeScale, gPaceRun.hooks ? gPaceRun.hookTicks * ms / gPaceRun.hooks : 0.0,
        gPaceRun.hookMax * ms, (cpu - gPaceRun.cpuMs) / (secs * 10.0), memText,
        slow >= 2.0 ? " - SLOWED: the game's time lost, the processors busy (docs/DEDICATED_HOST.md, \"Lag\")" : "");
    ++gPaceLast.lines; gPaceLast.rate = rate; gPaceLast.due = due; gPaceLast.cpu = (cpu - gPaceRun.cpuMs) / (secs * 10.0);
    gPaceRun = PaceRun(); gPaceRun.since = real; gPaceRun.cpuMs = cpu;
}
using DelayExecFn = LONG(WINAPI*)(unsigned char, const LARGE_INTEGER*);
DelayExecFn gDelayExec = nullptr;                       // NtDelayExecution (MGO1_IDLE_SLEEP=0: the event's wait, as before)
// the kernel's loop: the timed events (VBlank start / end, alarms, delays), then the first
// ready thread until it gives the baton back; idle until the next event when none is ready
void NativeKernelLoop(long long frames) {
    ProfileStart();
#if defined(_WIN32) && !defined(MGO1_PLAYER_BUILD)
    if (!DevEnv("MGO1_IDLE_SLEEP") || atoi(DevEnv("MGO1_IDLE_SLEEP")) != 0)
        if (HMODULE nt = GetModuleHandleA("ntdll.dll")) gDelayExec = (DelayExecFn)(void*)GetProcAddress(nt, "NtDelayExecution");
#endif
    Log("native: the baton %s; idle %s", FutexBaton() ? "on futexes (RtlWaitOnAddress)" : "on events",
        gDelayExec && !ps2::GsThreadOn() ? "sleeps to the next event (NtDelayExecution)" : "waits on the wake event");
    gNextVBlank = QpcNow() + FieldTicks();
    long long slices = 0;
    while (gFrameNo < frames) {
        const long long now = QpcNow();
        if (now >= gNextVBlank) {
            PaceVBlank(now - gNextVBlank);
            gNextVBlank += FieldTicks();
            if (now - gNextVBlank > FieldTicks()) gNextVBlank = now + FieldTicks();   // late: no burst of catch-up fields
            gVBlankEndAt = now + LineTicks(22);
            NativeVBlankStart();
        }
        if (gVBlankEndAt && now >= gVBlankEndAt) { gVBlankEndAt = 0; RunIntc(3); }      // VBlank end
        for (size_t k = 0; k < gAlarms.size(); ++k)
            if (gAlarms[k].live && gAlarms[k].due <= now) {
                gAlarms[k].live = false;
                const Alarm al = gAlarms[k];
                RunHandler(al.fn, {(uint32_t)al.id, al.time, al.arg});
            }
        gAlarms.erase(std::remove_if(gAlarms.begin(), gAlarms.end(), [](const Alarm& a) { return !a.live; }), gAlarms.end());
        for (auto& t : gThreads) if (t.state == 6 && t.wakeAt <= now) NativeReady(&t);
        EeThread* t = PickReady();
        if (!t && ps2::GsThreadOn() && __atomic_load_n(&gIrqPending, __ATOMIC_ACQUIRE) && !gInIrq) {
            // every thread waits and an interrupt is raised (the GS thread's SIGNAL / FINISH, after the DMA
            // that asked for it): the idle processor takes it, its handler may wake the thread waiting for it
            // (DG's FINISH chain: without it the boot waited 35 s and a match never started)
            static long long idleIrqs = 0;
            if (++idleIrqs == 1 || !(idleIrqs & (idleIrqs - 1))) Log("irq: taken while every thread waits (%lld so far)", idleIrqs);
            const uint32_t eie = gEie;
            gEie = 1;
            IrqDeliver();
            gEie = eie;
            t = PickReady();
        }
        if (!t) {                                       // everyone waits: sleep to the next event or an interrupt
            const long long left = NextEventAt() - QpcNow();
            if (left > 0) {
                const long long ms = (long long)((double)left * gTimeScale * 1000.0 / (double)QpcFreq());
                // (v141 beta 12) no GS thread (headless): no interrupt comes from off the PS2 threads, a sleep to the
                // event itself (Wine: in the process, to the microsecond) instead of the event's wait and the yields
                if (gDelayExec && !ps2::GsThreadOn()) {
                    LARGE_INTEGER d; d.QuadPart = -(LONGLONG)((double)left * gTimeScale * 1e7 / (double)QpcFreq());
                    if (d.QuadPart < 0) gDelayExec(0, &d);
                } else if (ms > 1) WaitForSingleObject(gKernelWake, (DWORD)(ms - 1)); else SwitchToThread();
            }
            continue;
        }
        const int id = t->id;
        gCur = id; gRingThread = (unsigned)id;
        t->state = 1;
        gEie = t->eie;                                  // the thread's own interrupt enable
        ++slices;
        if (slices <= 40) Log("native: thread %d runs (priority %d)", id, t->prio);
        const long long t0 = QpcNow();
        GoSet(t);
        BackWait(INFINITE);
        EeThread* b = Thr(id);
        if (b) { b->eie = gEie; b->runTicks += QpcNow() - t0; ++b->slices; }
        if (b && b->state == 1) b->state = 2;           // gave the baton back without waiting: still ready
    }
}
// Exact 60: the PS2's half rate runs as it is (two passes of the actor lists a drawn frame, the
// 0x40 actors, the players, on the second one with the step 10), and DG draws one more picture
// after that second pass, the tick: its render actor 0x143DC0 and frame actor 0x143CB0 as they
// run at the full rate (the half flag 0: the full build 0x12AE98, then the wait for the VBlank and
// the kick 0x12AD90). With DG's frame target at 1 each picture waits for one VBlank: the tick's
// own picture (the next pass) goes out at the VBlank it has at 30, the new one a VBlank before it.
// The game's clocks do not see it: the VBlanks of a drawn frame (0x23B8EC, the game's time
// 0x1043D0) and their mark 0x23B8F4 are put back.
long long gPassNo = 0;                                  // passes of the actor lists (0x122768)
// The in-between picture shows DG's objects halfway between the last two ticks: the matrices the
// build reads for them are set, for that build only, to the middle of their value at the previous
// in-between picture (what the tick's own picture showed since) and their value now, then put back.
// A matrix: 4 rows of 4 floats (x, y, z axes, then the translation; w 0 0 0 1).
struct Mat16 { float m[16]; };
#include "midchain.h"                                    // sixty (v149): the chains' objects no list holds
extern bool gMidLooseOn;
std::map<uint32_t, Mat16> gMidPrev, gMidNow;           // a matrix's address -> its value at the last picture
std::vector<std::pair<uint32_t, Mat16>> gMidPut;       // the values to put back after the build
long long gMidBlended = 0, gMidSkipped = 0;
// tests: SIM_MID_DIAG=n (read at each picture between; a new value arms it): the next n pictures between log each
// body of 50 bones or more and the objects hung on it (+0x9C), each pose drawn h halfway, s still, n new, j a jump,
// p not a pose, a with the blend of the object before it on the chain (MidAttach)
int gMidDiag = 0;
std::map<uint32_t, char> gMidDiagWay;
bool MatSane(const float* m) {
    for (int i = 0; i < 16; ++i) if (!(m[i] == m[i]) || m[i] > 1e7f || m[i] < -1e7f) return false;
    return m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] == 1.0f;
}
// 3x3 rotation (rows) -> quaternion (x y z w) and back
void MatToQuat(const float* m, float* q) {
    const float t = m[0] + m[5] + m[10];
    if (t > 0) { float s = sqrtf(t + 1.0f) * 2; q[3] = 0.25f * s; q[0] = (m[6] - m[9]) / s; q[1] = (m[8] - m[2]) / s; q[2] = (m[1] - m[4]) / s; }
    else if (m[0] > m[5] && m[0] > m[10]) { float s = sqrtf(1.0f + m[0] - m[5] - m[10]) * 2; q[3] = (m[6] - m[9]) / s; q[0] = 0.25f * s; q[1] = (m[4] + m[1]) / s; q[2] = (m[8] + m[2]) / s; }
    else if (m[5] > m[10]) { float s = sqrtf(1.0f + m[5] - m[0] - m[10]) * 2; q[3] = (m[8] - m[2]) / s; q[0] = (m[4] + m[1]) / s; q[1] = 0.25f * s; q[2] = (m[9] + m[6]) / s; }
    else { float s = sqrtf(1.0f + m[10] - m[0] - m[5]) * 2; q[3] = (m[1] - m[4]) / s; q[0] = (m[8] + m[2]) / s; q[1] = (m[9] + m[6]) / s; q[2] = 0.25f * s; }
}
void QuatToMat(const float* q, float* m) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y + z * w);     m[2] = 2 * (x * z - y * w);
    m[4] = 2 * (x * y - z * w);     m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z + x * w);
    m[8] = 2 * (x * z + y * w);     m[9] = 2 * (y * z - x * w);     m[10] = 1 - 2 * (x * x + y * y);
}
// a pose between two: the axes turned by t (quaternions), their lengths (a scale) and the translation
// interpolated; none for a jump (a respawn, a teleport: 3 m or a quarter turn in a tick)
float gMidT = 0.5f;                                    // the fraction of the picture being built (0.5: exact 60)
// sixty (v143): a pose whose w column is not 0 0 0 1 (left over by the code that made it; the drawing reads its
// 3 x 4): its axes orthogonal (a turn and a scale), so it is blended as a pose and keeps its w column
bool MatPose(const float* m) {
    for (int i = 0; i < 16; ++i) if (!(m[i] == m[i]) || m[i] > 1e7f || m[i] < -1e7f) return false;
    if (MatSane(m)) return true;
    if (!gMidLooseOn) return false;
    for (int r = 0; r < 3; ++r)
        for (int q = r + 1; q < 3; ++q) {
            const float* u = m + 4 * r; const float* v = m + 4 * q;
            const float d = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
            const float lu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2], lv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (lu < 1e-12f || lv < 1e-12f || d * d > 0.0025f * lu * lv) return false;
        }
    return true;
}
bool MatBlend(const float* a, const float* b, float t, float* out) {
    if (!MatPose(a) || !MatPose(b)) return false;
    const float dx = b[12] - a[12], dy = b[13] - a[13], dz = b[14] - a[14];
    if (dx * dx + dy * dy + dz * dz > 3000.0f * 3000.0f) return false;
    float la[3], lb[3], ua[9], ub[9];
    for (int r = 0; r < 3; ++r) {
        la[r] = sqrtf(a[4 * r] * a[4 * r] + a[4 * r + 1] * a[4 * r + 1] + a[4 * r + 2] * a[4 * r + 2]);
        lb[r] = sqrtf(b[4 * r] * b[4 * r] + b[4 * r + 1] * b[4 * r + 1] + b[4 * r + 2] * b[4 * r + 2]);
        if (la[r] < 1e-6f || lb[r] < 1e-6f) return false;
        for (int k = 0; k < 3; ++k) { ua[3 * r + k] = a[4 * r + k] / la[r]; ub[3 * r + k] = b[4 * r + k] / lb[r]; }
    }
    float ma[16] = {ua[0], ua[1], ua[2], 0, ua[3], ua[4], ua[5], 0, ua[6], ua[7], ua[8], 0, 0, 0, 0, 1};
    float mb[16] = {ub[0], ub[1], ub[2], 0, ub[3], ub[4], ub[5], 0, ub[6], ub[7], ub[8], 0, 0, 0, 0, 1};
    float qa[4], qb[4];
    MatToQuat(ma, qa); MatToQuat(mb, qb);
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (d < 0) { for (float& v : qb) v = -v; d = -d; }
    if (d < 0.7071f) return false;                        // more than a quarter turn in a tick
    float q[4], n = 0;
    for (int k = 0; k < 4; ++k) { q[k] = (1 - t) * qa[k] + t * qb[k]; n += q[k] * q[k]; }
    n = sqrtf(n);
    for (float& v : q) v /= n;
    float m[16];
    QuatToMat(q, m);
    for (int r = 0; r < 3; ++r) {
        const float l = (1 - t) * la[r] + t * lb[r];
        for (int k = 0; k < 3; ++k) out[4 * r + k] = m[4 * r + k] * l;
        out[4 * r + 3] = 0;
    }
    for (int k = 12; k < 15; ++k) out[k] = (1 - t) * a[k] + t * b[k];
    out[15] = 1;
    if (!MatSane(b)) { out[3] = b[3]; out[7] = b[7]; out[11] = b[11]; out[15] = b[15]; }   // sixty: its own w column
    return true;
}
uint32_t gMidLogAt = 0;                                // SIM_EXTRA_LOG: this matrix's three poses logged
void MidMatrix(uint32_t a) {
    if (a < 0x100000 || a + 64 > 0x02000000 || (a & 3)) return;
    Mat16 now; memcpy(now.m, gMem + a, 64);
    if (!gMidNow.emplace(a, now).second) return;          // already done (shared by two objects)
    auto it = gMidPrev.find(a);
    if (it == gMidPrev.end()) { if (gMidDiag) gMidDiagWay[a] = 'n'; return; }
    Mat16 mid;
    if (!memcmp(it->second.m, now.m, 64)) { if (gMidDiag) gMidDiagWay[a] = 's'; return; }   // did not move
    if (!MatBlend(it->second.m, now.m, gMidT, mid.m)) {
        ++gMidSkipped;
        if (gMidDiag) gMidDiagWay[a] = MatPose(it->second.m) && MatPose(now.m) ? 'j' : 'p';
        return;
    }
    if (gMidDiag) gMidDiagWay[a] = 'h';
    if (a == gMidLogAt)
        Log("exact 60: %08x from (%.1f %.1f %.1f) to (%.1f %.1f %.1f), drawn at (%.1f %.1f %.1f)", a, it->second.m[12], it->second.m[13], it->second.m[14],
            now.m[12], now.m[13], now.m[14], mid.m[12], mid.m[13], mid.m[14]);
    gMidPut.push_back({a, now});
    memcpy(gMem + a, mid.m, 64);
    ++gMidBlended;
}
bool gMidLooseOn = true;                               // tests: MGO1_MID_LOOSE=0 (read each picture)
bool gMidBonesOn = false;                              // tests: MGO1_MID_BONES=1, an object's bones even when its own matrix is not a pose
void MidBones(uint32_t o) {
    const uint32_t nb = R32(o + 0x80), bones = R32(o + 0x88);
    if (nb > 0 && nb <= 256 && bones >= 0x100000 && bones + 64 * nb <= 0x02000000)
        for (uint32_t b = 0; b < nb; ++b) MidMatrix(bones + 64 * b);
}
// SIM_MID_DIAG's lines: each body (50 bones or more) of the queues, then the objects on its +0x9C chain
void MidDiagLog(const std::set<uint32_t>& queues) {
    auto way = [](uint32_t a) { auto it = gMidDiagWay.find(a); return it == gMidDiagWay.end() ? '-' : it->second; };
    auto bonesWay = [&](uint32_t o, char* s, size_t n) {
        const uint32_t nb = R32(o + 0x80), bones = R32(o + 0x88);
        int c[7] = {}; const char* k = "hsnjpa-";
        size_t w = 0;
        for (uint32_t b = 0; nb <= 256 && b < nb; ++b) {
            const char x = way(bones + 64 * b);
            if (nb <= 12 && w + 1 < n) s[w++] = x;
            for (int i = 0; i < 7; ++i) if (k[i] == x) ++c[i];
        }
        if (nb > 12) w = (size_t)snprintf(s, n, "h%d s%d n%d j%d p%d a%d -%d", c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
        else s[w < n ? w : n - 1] = 0;
    };
    for (uint32_t q : queues)
        for (uint32_t list = 0; list < 8; ++list) {
            uint32_t o = R32(q + 0x50 * list + 0x2C);
            for (uint32_t k = 0, count = R32(q + 0x50 * list + 0x24); k < count && k < 1024 && o >= 0x100000 && o < 0x02000000; ++k, o = R32(o + 4)) {
                if (R32(o + 0x80) < 50 || R32(o + 0x80) > 256) continue;
                char b[64]; bonesWay(o, b, sizeof b);
                const float* m = (const float*)(gMem + o + 0x10);
                Log("exact 60 diag: picture %lld (pass %lld): body %08x list %u at (%.1f %.1f %.1f) %c, bones %s", gExtraPics + 1, gPassNo, o, list, m[12], m[13], m[14], way(o + 0x10), b);
                uint32_t c = R32(o + 0x9C) & 0x01FFFFFF;
                for (int n = 0; n < 16 && c >= 0x100000 && c < 0x02000000; ++n, c = R32(c + 0x9C) & 0x01FFFFFF) {
                    const float* h = (const float*)(gMem + c + 0x10);
                    bonesWay(c, b, sizeof b);
                    Log("exact 60 diag:   hung %08x model %08x %u bones at (%.1f %.1f %.1f) w %g %g %g %g %c, bones %s", c, R32(c + 0x78), R32(c + 0x80),
                        h[12], h[13], h[14], h[3], h[7], h[11], h[15], way(c + 0x10), b);
                }
            }
        }
    --gMidDiag;
}
// tests: SIM_MID_WRITES=n (a new value arms it): for n pictures between, the first body with hung objects: their
// matrices' writes (compared at each act, pass and build), each build's places of the hung objects, their two ticks'
// poses (docs/LOGS.md)
int gMidWrites = 0;
struct MidWatch { uint32_t lo, len; std::string name; std::vector<uint8_t> last; };
std::vector<MidWatch> gMidWatch;
std::vector<uint32_t> gMidWatchChain;                  // the body, then its hung objects
std::string gMidWatchSince = "?";
void MidWatchCompare(const char* now, bool log = true) {
    if (!gMidWrites && gMidWatch.empty()) return;
    for (auto& w : gMidWatch) {
        if (w.lo < 0x100000 || w.lo + w.len > 0x02000000) continue;
        if (!memcmp(gMem + w.lo, w.last.data(), w.len)) continue;
        size_t off = 0; while (off < w.len && gMem[w.lo + off] == w.last[off]) ++off;
        const float* m = (const float*)(gMem + w.lo + (off & ~63u));
        if (log) Log("mid writes: %s +%#zx changed (at %.1f %.1f %.1f) by %s, seen at %s (pass %lld, phase %u%s)", w.name.c_str(), off, m[12], m[13], m[14],
                     gMidWatchSince.c_str(), now, gPassNo, R32(kHalfPhase), gInExtra ? ", picture between" : "");
        memcpy(w.last.data(), gMem + w.lo, w.len);
    }
    gMidWatchSince = now;
}
void MidWatchSetup(const std::set<uint32_t>& queues) {
    std::vector<uint32_t> chain;
    for (uint32_t q : queues)
        for (uint32_t list = 0; list < 8 && chain.empty(); ++list) {
            uint32_t o = R32(q + 0x50 * list + 0x2C);
            for (uint32_t k = 0, count = R32(q + 0x50 * list + 0x24); k < count && k < 1024 && o >= 0x100000 && o < 0x02000000; ++k, o = R32(o + 4)) {
                if (R32(o + 0x80) < 50 || R32(o + 0x80) > 256) continue;
                uint32_t c = R32(o + 0x9C) & 0x01FFFFFF;
                if (c < 0x100000 || c >= 0x02000000) continue;
                chain.push_back(o);
                for (int n = 0; n < 6 && c >= 0x100000 && c < 0x02000000; ++n, c = R32(c + 0x9C) & 0x01FFFFFF) chain.push_back(c);
                break;
            }
        }
    if (chain == gMidWatchChain) return;
    gMidWatchChain = chain; gMidWatch.clear();
    char b[96];
    for (size_t i = 0; i < chain.size(); ++i) {
        const uint32_t o = chain[i];
        if (i) snprintf(b, sizeof b, "hung %zu %08x (%u bones)", i, o, R32(o + 0x80));
        else snprintf(b, sizeof b, "body %08x", o);
        gMidWatch.push_back({o + 0x10, 64, std::string(b) + " matrix", {}});
        const uint32_t nb = R32(o + 0x80), bones = R32(o + 0x88);
        if (i && nb > 0 && nb <= 256 && bones >= 0x100000 && bones + 64 * nb <= 0x02000000) gMidWatch.push_back({bones, 64 * std::min<uint32_t>(nb, 4), std::string(b) + " bones", {}});
        Log("mid writes: chain %zu: %s, model %08x, bones at %08x", i, b, R32(o + 0x78), bones);
    }
    for (auto& w : gMidWatch) w.last.assign(gMem + w.lo, gMem + w.lo + w.len);
}
void MidWatchRecord(const std::set<uint32_t>& queues) {   // each chain object's two ticks' poses (the replay's input)
    std::set<uint32_t> listed;
    for (uint32_t q : queues)
        for (uint32_t list = 0; list < 8; ++list) {
            uint32_t o = R32(q + 0x50 * list + 0x2C);
            for (uint32_t k = 0, count = R32(q + 0x50 * list + 0x24); k < count && k < 1024 && o >= 0x100000 && o < 0x02000000; ++k, o = R32(o + 4)) listed.insert(o);
        }
    for (size_t i = 0; i < gMidWatchChain.size(); ++i) {
        const uint32_t a = gMidWatchChain[i] + 0x10;
        const auto pv = gMidPrev.find(a);
        std::string t;
        char b[24];
        if (pv == gMidPrev.end()) t = " -";
        else for (int k = 0; k < 16; ++k) { snprintf(b, sizeof b, " %.9g", pv->second.m[k]); t += b; }
        t += " now";
        for (int k = 0; k < 16; ++k) { snprintf(b, sizeof b, " %.9g", BitsF(R32(a + 4 * k))); t += b; }
        Log("mid writes: record picture %lld t %g: chain %zu %08x listed %d prev%s", gExtraPics + 1, gMidT, i, gMidWatchChain[i], (int)listed.count(gMidWatchChain[i]), t.c_str());
    }
}
void MidWatchPlaces(const char* what) {                 // each hung object in the frame of the one before it
    for (size_t i = 2; i < gMidWatchChain.size(); ++i) {
        const float* a = (const float*)(gMem + gMidWatchChain[i - 1] + 0x10);
        const float* b = (const float*)(gMem + gMidWatchChain[i] + 0x10);
        float q[3], x[3], z[3];
        for (int r = 0; r < 3; ++r) {
            const float* u = a + 4 * r; const float l = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
            const float d0 = b[12] - a[12], d1 = b[13] - a[13], d2 = b[14] - a[14];
            q[r] = l > 1e-12f ? (d0 * u[0] + d1 * u[1] + d2 * u[2]) / l : 0;
            const float lb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]) * sqrtf(l), lz = sqrtf(b[8] * b[8] + b[9] * b[9] + b[10] * b[10]) * sqrtf(l);
            x[r] = lb > 1e-12f ? (b[0] * u[0] + b[1] * u[1] + b[2] * u[2]) / lb : 0;
            z[r] = lz > 1e-12f ? (b[8] * u[0] + b[9] * u[1] + b[10] * u[2]) / lz : 0;
        }
        Log("mid writes: %s pass %lld: hung %zu in hung %zu's frame at %.2f %.2f %.2f, x axis %.4f %.4f %.4f, z axis %.4f %.4f %.4f (at %.1f %.1f %.1f)",
            what, gPassNo, i, i - 1, q[0], q[1], q[2], x[0], x[1], x[2], z[0], z[1], z[2], b[12], b[13], b[14]);
    }
}
// sixty (v149): an object on a +0x9C chain that no list holds (a weapon's magazine) takes its parent's blend: the
// object before it, or one of the chain it moved a unit less against; as it is when its parent is not blended
void MidAttach(uint32_t c, uint32_t before, const std::vector<uint32_t>& chain) {
    const uint32_t a = c + 0x10;
    if (gMidNow.count(a)) return;                        // a list's own, or done
    Mat16 now; memcpy(now.m, gMem + a, 64);
    if (!MatPose(now.m)) return;
    gMidNow.emplace(a, now);
    const auto cp = gMidPrev.find(a);
    uint32_t parent = before;
    if (cp != gMidPrev.end()) {
        auto moved = [&](uint32_t p) {
            const auto pn = gMidNow.find(p + 0x10), pp = gMidPrev.find(p + 0x10);
            return pn == gMidNow.end() || pp == gMidPrev.end() ? 1e30f : midchain::Moved(cp->second.m, now.m, pp->second.m, pn->second.m);
        };
        float best = moved(before);
        for (uint32_t p : chain)
            if (p != c && p != before) { const float d = moved(p); if (d < best - 1.0f) { best = d; parent = p; } }
    }
    const uint32_t pa = parent + 0x10;
    const auto pn = gMidNow.find(pa);
    if (pn == gMidNow.end()) return;
    const float* pMid = (const float*)(gMem + pa);
    const auto pp = gMidPrev.find(pa);
    const bool drawn = memcmp(pMid, pn->second.m, 64) != 0, still = pp != gMidPrev.end() && !memcmp(pp->second.m, pn->second.m, 64);
    if (!drawn && !still) return;                        // its parent as it is: so is it
    Mat16 mid;
    if (!midchain::Attached(cp != gMidPrev.end() ? cp->second.m : nullptr, now.m, pp != gMidPrev.end() ? pp->second.m : nullptr, pn->second.m, pMid, gMidT, MatBlend, mid.m))
        return;
    if (!MatPose(mid.m)) return;
    const uint32_t nb = R32(c + 0x80), bones = R32(c + 0x88);
    if (memcmp(mid.m, now.m, 64)) {
        if (gMidDiag) gMidDiagWay[a] = 'a';
        gMidPut.push_back({a, now});
        memcpy(gMem + a, mid.m, 64);
        ++gMidBlended;
    } else if (gMidDiag) gMidDiagWay[a] = 's';
    if (nb < 1 || nb > 256 || bones < 0x100000 || bones + 64 * nb > 0x02000000) return;
    for (uint32_t b = 0; b < nb; ++b) {                   // its bones: their place in its frame, on its pose as drawn
        const uint32_t ba = bones + 64 * b;
        if (gMidNow.count(ba)) continue;
        Mat16 bn; memcpy(bn.m, gMem + ba, 64);
        if (!MatPose(bn.m)) continue;
        gMidNow.emplace(ba, bn);
        const auto bp = gMidPrev.find(ba);
        Mat16 bm;
        if (!midchain::Attached(bp != gMidPrev.end() && cp != gMidPrev.end() ? bp->second.m : nullptr, bn.m, cp != gMidPrev.end() ? cp->second.m : nullptr, now.m, mid.m, gMidT, MatBlend, bm.m)
            || !memcmp(bm.m, bn.m, 64)) continue;
        if (gMidDiag) gMidDiagWay[ba] = 'a';
        gMidPut.push_back({ba, bn});
        memcpy(gMem + ba, bm.m, 64);
        ++gMidBlended;
    }
}
void MidChains(const std::set<uint32_t>& queues) {
    std::vector<std::vector<uint32_t>> chains;            // each animated object's chain, the object first
    std::set<uint32_t> hung;
    for (uint32_t q : queues)
        for (uint32_t list = 0; list < 8; ++list) {
            uint32_t o = R32(q + 0x50 * list + 0x2C);
            for (uint32_t k = 0, count = R32(q + 0x50 * list + 0x24); k < count && k < 1024 && o >= 0x100000 && o < 0x02000000; ++k, o = R32(o + 4)) {
                const uint32_t nb = R32(o + 0x80), bones = R32(o + 0x88);
                if (nb < 1 || nb > 256 || bones < 0x100000 || bones + 64 * nb > 0x02000000) continue;   // an animated object
                uint32_t c = R32(o + 0x9C) & 0x01FFFFFF;
                if (c < 0x100000) continue;
                chains.emplace_back(1, o);
                for (uint32_t n = 0; n < 16 && c >= 0x100000 && c + 0xA0 <= 0x02000000 && !(c & 15); ++n, c = R32(c + 0x9C) & 0x01FFFFFF) {
                    chains.back().push_back(c);
                    hung.insert(c);
                }
            }
        }
    for (const auto& chain : chains)                      // from their heads only (a weapon in a list is on the body's)
        if (!hung.count(chain[0]))
            for (size_t i = 1; i < chain.size(); ++i) MidAttach(chain[i], chain[i - 1], chain);
}
// the animated objects of DG's render queues (each chanl's queue [chanl + 0x3A0]: lists of 0x50
// bytes, their count at +0x24 and first object at +0x2C, objects linked by +4): an object's world
// matrix at +0x10, its bones' matrices at [+0x88] ([+0x80] of them)
void MidObjects() {
    std::set<uint32_t> queues;
    for (uint32_t ch = R32(V(0x23AF80)), n = 0; ch && n < 16; ch = R32(ch + 0x360), ++n) {
        if (ch < 0x100000 || ch >= 0x02000000) break;
        const uint32_t q = R32(ch + 0x3A0);
        if (q >= 0x100000 && q < 0x02000000) queues.insert(q);
    }
    if (gMidWrites) { MidWatchSetup(queues); MidWatchCompare("the picture between's start"); MidWatchRecord(queues); }
    for (uint32_t q : queues)
        for (uint32_t list = 0; list < 8; ++list) {
            const uint32_t base = q + 0x50 * list;
            const uint32_t count = R32(base + 0x24);
            uint32_t o = R32(base + 0x2C);
            for (uint32_t k = 0; k < count && k < 1024 && o >= 0x100000 && o < 0x02000000; ++k, o = R32(o + 4)) {
                if (!MatPose((const float*)(gMem + o + 0x10))) {      // sixty: MatSane before
                    if (gMidDiag) gMidDiagWay[o + 0x10] = 'p';
                    if (gMidBonesOn) MidBones(o);
                    continue;
                }
                if (!MatSane((const float*)(gMem + o + 0x10))) {
                    static int told = 0;
                    const float* m = (const float*)(gMem + o + 0x10);
                    if (told < 8 && Once(((uint64_t)198 << 40) | o)) {
                        ++told;
                        Log("exact 60: object %08x (list %u) has a pose with the w column %g %g %g %g: drawn halfway too", o, list, m[3], m[7], m[11], m[15]);
                    }
                }
                static const bool logOne = DevEnv("SIM_EXTRA_LOG") != nullptr;
                if (logOne && R32(o + 0x80) >= 50 && !gMidLogAt) gMidLogAt = o + 0x10;   // the first object with a body's bones
                MidMatrix(o + 0x10);
                MidBones(o);
            }
        }
    MidChains(queues);
    if (gMidDiag) MidDiagLog(queues);
}
// the views (DG's chanls, [0x23AF80] linked by +0x360): each one's camera matrix at +0 (the eye's
// axes and position) is set by 0x128FE8(chanl, matrix, f12, f13, f14), which derives the rest from
// it (+0x40 its inverse, +0x80 and +0x180 the projections, +0xC0 the view's, +0x280 the frustum;
// f12 kept at +0x338, f13 and f14 an offset of the projection): the middle camera goes through it
// again with the view's last settings, and the view's first 0x340 bytes are put back after
struct ChanlSet { float f13 = 0, f14 = 0; };
std::map<uint32_t, ChanlSet> gChanlSets;               // each view's last 0x128FE8 settings (DispatchBridge)
std::vector<std::pair<uint32_t, std::vector<uint8_t>>> gMidChanlPut;
void MidViews(Ctx& c) {
    for (uint32_t ch = R32(V(0x23AF80)), n = 0; ch && n < 16; ch = R32(ch + 0x360), ++n) {
        if (ch < 0x100000 || ch + 0x340 > 0x02000000) break;
        auto set = gChanlSets.find(ch);
        if (set == gChanlSets.end()) continue;
        Mat16 now; memcpy(now.m, gMem + ch, 64);
        gMidNow[ch] = now;
        auto it = gMidPrev.find(ch);
        if (it == gMidPrev.end() || !memcmp(it->second.m, now.m, 64)) continue;
        Mat16 mid;
        if (!MatBlend(it->second.m, now.m, gMidT, mid.m)) { ++gMidSkipped; continue; }
        static const bool logViews = DevEnv("SIM_EXTRA_LOG") != nullptr;
        if (logViews && n == 0)
            Log("exact 60: view %08x from (%.1f %.1f %.1f) to (%.1f %.1f %.1f), drawn at (%.1f %.1f %.1f)", ch, it->second.m[12], it->second.m[13], it->second.m[14],
                now.m[12], now.m[13], now.m[14], mid.m[12], mid.m[13], mid.m[14]);
        gMidChanlPut.push_back({ch, std::vector<uint8_t>(gMem + ch, gMem + ch + 0x340)});
        memcpy(gMem + ch, mid.m, 64);
        Ctx x = c;
        x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
        x.r[31] = V(0x1005f0);
        x.r[4] = ch; x.r[5] = ch;                           // the matrix copied onto itself
        x.f[12] = BitsF(R32(ch + 0x338)); x.f[13] = set->second.f13; x.f[14] = set->second.f14;
        Call(x, V(0x128fe8));
        ++gMidBlended;
    }
}
// DG's kick 0x12AD90 at 60 (each picture): the last picture timed on the GS from its kick to its last FINISH (DG's
// frame wait kicks the next one after it), late for 60 when done after the VBlank following the one it went out at
void PaceKick(bool tickPicture) {
    const long long now = QpcNow(), fin = ps2::GsFinishAt();
    if (gPicKickAt && fin > gPicKickAt && fin - gPicKickAt <= 30 * FieldTicks()) {   // longer: a load
        ++gPicsTimed;
        if (fin > gPicVBlankAt + FieldTicks()) ++gPicsOver;
    }
    gPicKickAt = now; gPicVBlankAt = gVBlankAt;
    if (tickPicture) { gTickKickPrev = gTickKickVb; gTickKickVb = gFrameNo; gTickKickAtPrev = gTickKickAt; gTickKickAt = now; }
}
bool PaceGuardPaused(int plan) {
    const long long took = gTickKickPrev >= 0 ? gTickKickVb - gTickKickPrev : 0;
    const double ms = gTickKickAtPrev ? (double)(gTickKickAt - gTickKickAtPrev) * 1000.0 / (double)QpcFreq() : 0.0;
    const int planned = gTickPlan;
    gTickPlan = plan;
    const bool paused = gPace.Tick(took, planned, gPicsTimed, gPicsOver);
    gPicsTimed = gPicsOver = 0;
    if (gSimTickLog > 0) {                              // SIM_TICK_LOG (tests)
        --gSimTickLog;
        Log("tick log: %lld VBlanks, %.1f ms (%d planned)%s", took, ms, planned, took > planned && took <= 30 ? " LATE" : "");
    }
    const char* who = gPal60 ? "PAL at 60" : "exact 60";
    static long long starts = 0, mores = 0, backs = 0;
    auto often = [](long long n) { return n <= 8 || !(n & (n - 1)); };
    if (gPace.event == PaceGuard::kStart && often(++starts))
        Log("%s: %d of the last %d ticks late, %d in a row (this one %lld VBlanks, %.1f ms, %d planned): no picture between for %d ticks (%lld late, %lld pauses so far)",
            who, gPace.eventLate, gPace.eventWindow, gPace.eventRow, took, ms, planned, PaceGuard::kPause, gPace.lateTicks, gPace.pauses);
    else if (gPace.event == PaceGuard::kMore && often(++mores))
        Log("%s: %d of the pause's %d pictures took over a field on the GS: no picture between for %d more ticks (%lld times so far)",
            who, gPace.eventSlow, gPace.eventPics, PaceGuard::kPause, gPace.mores);
    else if (gPace.event == PaceGuard::kBack && often(++backs))
        Log("%s: pictures between again after %d ticks without (%d of the pause's last %d pictures over a field on the GS)",
            who, (gPace.more + 1) * PaceGuard::kPause, gPace.eventSlow, gPace.eventPics);
    return paused;
}
void MidBegin(Ctx& c, float t = 0.5f) {
    gMidT = t;
    { const char* l = DevEnv("MGO1_MID_LOOSE"); const char* b = DevEnv("MGO1_MID_BONES"); gMidLooseOn = !l || atoi(l) != 0; gMidBonesOn = b && atoi(b) != 0; }
    {
        static int armed = 0;
        const char* d = DevEnv("SIM_MID_DIAG");
        const int v = d ? atoi(d) : 0;
        if (v != armed) { armed = v; gMidDiag = v; }
    }
    {
        static int armed = 0;
        const char* d = DevEnv("SIM_MID_WRITES");
        const int v = d ? atoi(d) : 0;
        if (v != armed) { armed = v; gMidWrites = v; }
    }
    gMidNow.clear(); gMidPut.clear(); gMidChanlPut.clear(); gMidDiagWay.clear();
    MidViews(c);
    if (gMidDiag) {
        const uint32_t ch = R32(V(0x23AF80));
        Log("exact 60 diag: picture %lld (pass %lld): %zu views drawn halfway; view %08x at (%.1f %.1f %.1f)", gExtraPics + 1, gPassNo, gMidChanlPut.size(), ch,
            ch >= 0x100000 && ch < 0x01FFFFC0 ? BitsF(R32(ch + 0x30)) : 0.0f, ch >= 0x100000 && ch < 0x01FFFFC0 ? BitsF(R32(ch + 0x34)) : 0.0f,
            ch >= 0x100000 && ch < 0x01FFFFC0 ? BitsF(R32(ch + 0x38)) : 0.0f);
    }
    MidObjects();
    if (gMidWrites) MidWatchCompare("the blend (MidObjects)", false);
}
void MidEnd(bool keep = true) {                          // keep: the values now are the previous ones of the next tick's pictures
    if (gMidWrites) MidWatchCompare("the picture between's build");
    for (auto it = gMidPut.rbegin(); it != gMidPut.rend(); ++it) memcpy(gMem + it->first, it->second.m, 64);
    for (auto it = gMidChanlPut.rbegin(); it != gMidChanlPut.rend(); ++it) memcpy(gMem + it->first, it->second.data(), it->second.size());
    gMidPut.clear(); gMidChanlPut.clear();
    if (keep) gMidPrev.swap(gMidNow);
    if (gMidWrites) { MidWatchCompare("the put back (MidEnd)", false); if (--gMidWrites == 0) gMidWatch.clear(), gMidWatchChain.clear(); }
}
#include "openfix.h"                                     // open list (v141): the 2D packets at 60 (DgPacksSync)
void ExtraPicture(Ctx& c) {
    static uint32_t lastPhase = 0;
    const uint32_t phase = R32(kHalfPhase);
    const bool tickDone = lastPhase == 1 && phase == 0;     // the frame actor turned the phase back: the tick ran
    lastPhase = phase;
    ++gPassNo;
    if (tickDone && R32(kHalfRate)) ++gTicks;
    if (!tickDone || !R32(kHalfRate) || !Exact60()) return;
    const int writers = (int)(R32(V(0x23AFD0)) & 1);       // open list: the DG clock the tick's 2D writers wrote at
    static const bool always = getenv("SIM_EXTRA_ALWAYS") != nullptr;
    static const bool mid = !getenv("SIM_NO_MIDDLE");
    if (gPal60) {                                           // PAL at 60: this tick's extra pictures, each at its VBlank
        const int r = (int)(gPalGroup++ % 5), n = kPalFrac[r][2] < 0 ? 2 : 3;
        const bool paused = !always && PaceGuardPaused(n);  // paused: the tick's own picture only, at its n-th VBlank
        gPalLastKick = 0;
        for (int m = 1; m < n && !paused; ++m) {
            if (gPalGroupVb >= m && !always) {                // its VBlank has gone: left out
                if (++gExtraLate == 1 || !(gExtraLate & (gExtraLate - 1))) Log("PAL at 60: late, a picture left out (%lld so far)", gExtraLate);
                continue;
            }
            const uint32_t vbFrame = R32(V(0x23B8EC)), vbMark = R32(V(0x23B8F4));
            gPal60Target = (uint32_t)(m - gPalLastKick);
            W32(V(0x23B8F8), gPal60Target);
            W32(kHalfRate, 0); gInExtra = true;
            if (mid) MidBegin(c, kPalFrac[r][m - 1] / 12.0f);
            Ctx x = c;
            x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
            x.r[31] = V(0x1005f0);
            Call(x, V(0x143dc0));                                  // the full build
            if (mid) MidEnd(false);                             // the last two ticks stay the ones to draw between
            x = c;
            x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
            x.r[31] = V(0x1005f0);
            Call(x, V(0x143cb0));                                  // the wait for its VBlank, the kick
            W32(kHalfRate, 1); gInExtra = false;
            W32(V(0x23B8EC), vbFrame); W32(V(0x23B8F4), vbMark);
            DgPacksSync(writers);                                  // open list: the next build reads the tick's 2D
            gPalLastKick = gPalGroupVb;
            if (++gExtraPics == 1) Log("PAL at 60: the first picture between the ticks (pass %lld, dg %lld)", gPassNo, gDgFrames.load());
        }
        gPal60Target = (uint32_t)std::max(1, n - gPalLastKick);  // the tick's own picture: at the n-th VBlank of the tick
        W32(V(0x23B8F8), gPal60Target);
        gPalTickT = kPalFrac[r][n - 1] / 12.0f;
        gPalTickPending = mid;
        if (gExtraPics && gExtraPics % 3000 < 2)
            Log("PAL at 60: %lld pictures between the ticks (%lld left out, late), %lld matrices drawn between, %lld not (jumps); %lld ticks late, %lld pauses, %lld ticks paused",
                gExtraPics, gExtraLate, gMidBlended, gMidSkipped, gPace.lateTicks, gPace.pauses, gPace.pausedTicks);
        return;
    }
    gTickAt30 = !always && PaceGuardPaused(2);
    if (gTickAt30) { W32(V(0x23B8F8), 2); return; }         // paused: the tick's own picture 2 VBlanks after the last one
    // late (two VBlanks since the last picture: the tick's own picture is due now): no picture in
    // between, so the game keeps the PS2's pace on a machine that cannot draw 60 a second
    if (R32(kVsyncFrame) >= 2 && !always) {
        if (++gExtraLate == 1 || !(gExtraLate & (gExtraLate - 1))) Log("exact 60: late, no picture in between (%lld so far)", gExtraLate);
        return;
    }
    const uint32_t vbFrame = R32(V(0x23B8EC)), vbMark = R32(V(0x23B8F4));
    W32(kHalfRate, 0); gInExtra = true;
    if (mid) MidBegin(c);
    Ctx x = c;
    x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;         // below the main loop's frame
    x.r[31] = V(0x1005f0);
    Call(x, V(0x143dc0));                                      // the full build
    if (mid) MidEnd();                                      // the packets hold what the build drew
    x = c;
    x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
    x.r[31] = V(0x1005f0);
    Call(x, V(0x143cb0));                                      // the wait for the VBlank, the kick
    W32(kHalfRate, 1); gInExtra = false;
    W32(V(0x23B8EC), vbFrame); W32(V(0x23B8F4), vbMark);
    DgPacksSync(writers);                                      // open list: the tick's own picture reads the tick's 2D
    if (++gExtraPics == 1) Log("exact 60: the first picture between the passes (pass %lld, dg %lld)", gPassNo, gDgFrames.load());
    static const bool logEach = DevEnv("SIM_EXTRA_LOG") != nullptr;
    if (logEach) Log("exact 60: picture between, pass %lld, dg %lld, %lld matrices halfway so far", gPassNo, gDgFrames.load(), gMidBlended);
    if (gExtraPics % 1800 == 0)
        Log("exact 60: %lld pictures between the passes (%lld left out, late), %lld matrices drawn halfway, %lld not (jumps); %lld ticks late, %lld pauses, %lld ticks paused",
            gExtraPics, gExtraLate, gMidBlended, gMidSkipped, gPace.lateTicks, gPace.pauses, gPace.pausedTicks);
}
// MGO1_HOST_ONLY=1: Host Only set in the create-game settings (below) without a file.
// MGO1_HOST_SETTINGS=<file>: a dedicated host's game settings, "key = value" lines (# comments),
// written into the game's create-game settings when the lobby's 0x4305 (the account's last
// settings) has been read (0x194838; the settings at its context + 0x6F50, 0x194C30), so the
// Create Game screens, the stage the game loads and the 0x4310 sent all have them.
// Keys: name (3 to 15 characters), comment (75), password (15; empty: no lock), rotation
// (rule:map pairs "1:2, 0:8"; rules 0 DM, 1 TDM, 2 rescue, 3 capture, 4 sneaking; maps 1 Lost
// Forest n001a, 2 Ghost Factory n002a, 3 City Under Siege n003a, 4-6 Killhouse A-C n011a-n013a,
// 7 Svyatogornyj East n021a, 8 Mountaintop n022a, 9 Graniny Gorki Lab n024a, 10 Pillbox
// Purgatory n031a, 11 High Ice n032a, 12 Brown Town n033a: the lobby's numbers), red_team /
// blue_team (0 GRU, 1 KGB, 2 Ocelot unit), max_players (with Host Only: the players who join + 1,
// as the Create Game screens send it: they show it less one, 2 to 8), weapons (0 none restricted
// .. 7 knife only), dm_minutes, tdm_minutes, tdm_rounds, tdm_tickets, cap_minutes, cap_rounds,
// res_minutes, res_rounds, sne_minutes, sne_rounds (1 to 99), auto_balance (the players' difference
// that balances the teams, 2 to 6), idle_kick_minutes, team_kill_count (1 to 99), rating_restriction
// (0 none, 1 above, 2 below the rating), rating (100 to 9900), the
// switches (on / off) unique_characters, rumble_roses, ghosts, friendly_fire, team_auto_balance,
// idle_kick, team_kill_kick, voice_chat, or flags (the two bytes of switches in hex).
// Host Only is always set. Offsets: the reads of 0x194838 (the fields in the order of 0x4310).
std::vector<std::pair<uint8_t, uint8_t>> gHostRotation;   // (v141) the host's rotation {rule, map}, put at the game's creation too
int gHostMaxPlayers = 0;                                  // (v141) its maximum of players (Host Only: the joiners + 1), 0 none
bool gHostRumble = false;                                 // (v141 beta 5) its settings ask for REIKO HINOMOTO/ROWDY REIKO
int gHostUnits[2] = {-1, -1};                             // (v150-host beta 16) its settings' units of the red and the blue team
int gHostWeapons = -1;                                    // (v150-host beta 16) its weapons setting (0 all .. 7 knife only)
int gHostFF = -1;                                         // (v150-host beta 22, wake) its friendly fire (the switches' 0x10 of +681): 1 on, 0 off, -1 not known
struct HostSettingKey { const char* key; uint32_t off; int size; };
const HostSettingKey kHostKeys[] = {
    {"max_players", 599, 1}, {"red_team", 596, 1}, {"blue_team", 597, 1}, {"weapons", 598, 1},
    {"sne_minutes", 632, 4}, {"sne_rounds", 636, 4}, {"cap_minutes", 640, 4}, {"cap_rounds", 644, 4},
    {"res_minutes", 648, 4}, {"res_rounds", 652, 4}, {"tdm_minutes", 656, 4}, {"tdm_rounds", 660, 4},
    {"tdm_tickets", 664, 4}, {"dm_minutes", 668, 4}, {"auto_balance", 683, 1}, {"idle_kick_minutes", 684, 2},
    {"team_kill_count", 686, 2}, {"rating_restriction", 626, 1}, {"rating", 628, 4}};
struct HostSwitch { const char* key; uint32_t off; uint8_t bit; };   // the switches: +681, +682
const HostSwitch kHostSwitches[] = {
    {"unique_characters", 681, 0x01}, {"rumble_roses", 681, 0x02}, {"ghosts", 681, 0x08},
    {"friendly_fire", 681, 0x10}, {"team_auto_balance", 681, 0x40}, {"idle_kick", 681, 0x80},
    {"team_kill_kick", 682, 0x01}, {"voice_chat", 682, 0x02}};
void PatchHostSettings(uint32_t st) {
    static const char* path = DevEnv("MGO1_HOST_SETTINGS");
    if (!st || st + 700 > 0x02000000) return;
    // Host Only set here, not toggled on the screen: the lobby keeps the account's last
    // settings, so a toggle would turn it off in every second game
    W8(st + 167, 1);
    // (v150-host beta 16) the game's own settings for the virtual dolls (the account's last ones without a file; the file's
    // below)
    gHostUnits[0] = R8(st + 596); gHostUnits[1] = R8(st + 597); gHostWeapons = R8(st + 598);
    if (!path) { Log("host settings: host only"); return; }
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) { Log("host settings: %s not readable (host only set)", path); return; }
    char line[512];
    std::string done;
    // a text field: max bytes, then its terminator when it has one (the password has none: +167 is Host Only)
    auto text = [&](uint32_t at, size_t max, const std::string& v, bool nul = true) {
        for (size_t k = 0; k < max + (nul ? 1 : 0); ++k) W8(st + at + (uint32_t)k, k < v.size() && k < max ? (uint8_t)v[k] : 0);
    };
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string x) { while (!x.empty() && isspace((unsigned char)x.back())) x.pop_back(); size_t a = 0; while (a < x.size() && isspace((unsigned char)x[a])) ++a; return x.substr(a); };
        const std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
        bool known = true;
        if (k == "name") { if (v.size() >= 3) text(4, 16, v.substr(0, 15)); else known = false; }
        else if (k == "comment") text(21, 128, v.substr(0, 75));
        else if (k == "password") { text(151, 16, v.substr(0, 15), false); W8(st + 150, v.empty() ? 0 : 1); if (!v.empty()) AddSecret(v.substr(0, 15), true); }
        else if (k == "rotation") {                          // "rule:map, rule:map": the rest cleared
            int n = 0; const char* q = v.c_str();
            gHostRotation.clear();
            for (int i = 0; i < 16; ++i) { W8(st + 564 + i, 0); W8(st + 580 + i, 0); }
            while (*q && n < 16) {
                char* e = nullptr; const long r = strtol(q, &e, 10);
                if (e == q || *e != ':') break;
                const long m = strtol(e + 1, &e, 10);
                if (r < 0 || r > 6 || m < 1 || m > 15) break;
                // (v141) the Create Game screens take maps 1 to 12 and rules 0 to 4 only: a map 13 and up as its
                // stage's map, TEAM SNEAKING as RESCUE, BASE CAPTURE as CAPTURE there; the game's creation gets the
                // rotation as written (BigGameCreate). A map this host does not have (no maps folder) is left out
                if (m > 12) ExtraMapsWait();             // (v150-host beta 22, sync) a map still building is not "not here"
                const ExtraMap* em = m > 12 ? ExtraMapById((int)m) : nullptr;
                const bool here = m <= 12 || (em && em->ready);
                if (!here) { Log("host settings: map %ld is not here (no maps folder, or not built): left out of the rotation", m); }
                else {
                    W8(st + 564 + n, (uint8_t)(r <= 4 ? r : r == 5 ? 2 : 3));
                    W8(st + 580 + n, (uint8_t)(m <= 12 ? m : ExtraMapStageOf((int)m)));
                    ++n;
                    gHostRotation.push_back({(uint8_t)r, (uint8_t)m});
                }
                while (*e == ',' || *e == ' ') ++e;
                q = e;
            }
            if (!n) known = false;
        }
        else if (k == "flags") { const unsigned x = (unsigned)strtoul(v.c_str(), nullptr, 16); W8(st + 681, x & 0xff); W8(st + 682, (x >> 8) & 0xff); }
        else {
            known = false;
            for (const auto& h : kHostSwitches) if (k == h.key) {
                const bool on = v == "on" || v == "yes" || v == "1" || v == "true";
                if (!on && v != "off" && v != "no" && v != "0" && v != "false") break;
                W8(st + h.off, on ? (R8(st + h.off) | h.bit) : (R8(st + h.off) & ~h.bit));
                known = true;
            }
            for (const auto& h : kHostKeys) if (k == h.key) {
                unsigned long x = strtoul(v.c_str(), nullptr, 10);
                // (v141 beta 5) 16 at most: the game network's 16 entries hold the Host Only host too (P1)
                if (k == "max_players" && x > 16) { Log("host settings: max_players %lu is above 16 (the host takes one of the 16 entries): 16", x); x = 16; }
                if (k == "max_players") gHostMaxPlayers = (int)x;
                if (h.size == 1) W8(st + h.off, (uint8_t)x); else if (h.size == 2) W16(st + h.off, (uint16_t)x); else W32(st + h.off, (uint32_t)x);
                known = true;
            }
        }
        done += " " + k + (known ? "" : " (ignored)");
    }
    fclose(f);
    W8(st + 167, 1);
    Log("host settings from %s:%s; host only", path, done.c_str());
    // (v141 beta 5) REIKO HINOMOTO/ROWDY REIKO (the Create Game's "???", rumble_roses): the game clears it unless the
    // host's own record unlocks it (its check 0x360660, passed over in DispatchBridge); a dedicated host whose settings ask
    // for it has it. They take two unique characters' places (the game's settings then have the unique characters on
    // whatever their switch): the switch set with them
    gHostRumble = (R8(st + 681) & 0x02) != 0;
    gHostUnits[0] = R8(st + 596); gHostUnits[1] = R8(st + 597);   // (v150-host beta 16: the teams' units, for the virtual dolls)
    gHostWeapons = R8(st + 598);
    if (gHostRumble) {
        const bool unique = (R8(st + 681) & 0x01) != 0;
        W8(st + 681, R8(st + 681) | 0x01);
        Log("host settings: REIKO HINOMOTO/ROWDY REIKO on, in place of two unique characters%s", unique ? "" : " (the unique characters set on with them)");
    }
}
// SIM_GUARD_TRACE=<n> (tests): the collision guards' first n lines (not 3), each with the calling chain (the saved
// return addresses, found by each caller's prologue: addiu sp,sp,-N and sd/sw ra) and the actor whose act runs
static int GuardCap() {
    static const int n = DevEnv("SIM_GUARD_TRACE") ? std::max(3, atoi(DevEnv("SIM_GUARD_TRACE"))) : 3;
    return n;
}
static std::string GuardChain(const Ctx& c) {
    if (!DevEnv("SIM_GUARD_TRACE")) return "";
    std::string s = " chain";
    uint32_t ra = (uint32_t)c.r[31], sp = (uint32_t)c.r[29];
    for (int k = 0; k < 10 && ra >= 0x100000u && ra < 0x01FFFFF0u && sp >= 0x100000u && sp < 0x01FFFFF0u; ++k) {
        char b[16]; snprintf(b, sizeof b, " %06x", ra - 8); s += b;
        uint32_t p = 0, frame = 0;
        for (uint32_t a = ra - 8; a > ra - 0x8000u && a >= 0x100000u; a -= 4) {
            const uint32_t w = R32(a);
            if ((w >> 16) == 0x27BDu && (int16_t)(w & 0xFFFF) < 0) { p = a; frame = (uint32_t)(-(int16_t)(w & 0xFFFF)); break; }
        }
        if (!p) break;
        int off = -1;
        for (uint32_t a = p; a < p + 0x100 && a < ra; a += 4) {
            const uint32_t w = R32(a);
            if ((w >> 16) == 0xFFBFu || (w >> 16) == 0xAFBFu) { off = (int16_t)(w & 0xFFFF); break; }
        }
        if (off < 0) break;
        ra = R32(sp + (uint32_t)off); sp += frame;
    }
    const auto it = gNativeActors.find(gActActor);
    char b[96]; snprintf(b, sizeof b, "; act %08x %s", gActActor, it != gNativeActors.end() ? it->second.c_str() : "?");
    return s + b;
}
// the collision grid's ray walk (0x159D88; a2 and a3 point at the ray's vectors): a coordinate past
// 10^12 (a vector left undefined upstream: 1.5e35 seen, from a remote player's aim on City Under Siege)
// makes a ray whose walk never ends (the dedicated host hung, 27 September): no hit, at once.
// (v150-host beta 18) x, y and z only: the walk reads nothing else of them (its line walk, f12 = 0, loads
// +0, +4, +8; its box, f12 != 0, is clamped to the grid). The game's bullets leave the fourth word of their
// line's vectors as the stack had it (0x38F608 in the map's code, a 5 mm line at the bullet's impact, for
// its mark on the wall: -3.4e38 seen), which the check of the four words took for a bad ray: the line
// went unwalked ("no hit", no mark) in nearly every game with shots (docs/RECOMPILED_WORLD.md)
static int RayWalkBad(const Ctx& c) {                 // 0 sane, 1 x, y or z past 10^12, 2 only the fourth word
    float v[2][4]; const float* p[2] = {nullptr, nullptr};
    for (int k = 0; k < 2; ++k) {
        const uint32_t a = (uint32_t)c.r[6 + k];
        if (a < 0x100000u || (a & 0x01FFFFFFu) > 0x01FFFFF0u) continue;
        for (int j = 0; j < 4; ++j) v[k][j] = BitsF(R32(a + 4 * j));
        p[k] = v[k];
    }
    return RayWordsBad(p[0], p[1]);
}
// map.ini laser= (docs/CHARACTERS.md, The SOCOM's laser): the laser's act (0x454D48, NewNetWeaponLaserSight: one
// per player, a0 the laser, its player at +0x40). The pistols' states never call the laser's control (0x417810),
// so a weapon given a laser on this map switches it itself: shown while its player aims or holds it readied (his
// flag 0x2E, bit 14 of +0xB30, as the Scorpion's control asks), hidden when he stops or holds another weapon. The
// beam goes the weapon's way from its point EF_laser (0x3FC3F8 below), as the Scorpion's: the recoil and the reload
// move it (Snake, 29 September 22:14). Its messages (0x454FF8): 0x1B0001 runs it (the ray, bit 2 of +0x44),
// 0x1B0002 shows it (bit 1), 0x1B0003 stops it, 0x1B0004 hides it, 0x1B0005 the beam its own way (bit 8 off)
static void LaserDrive(Ctx& c) {
    const uint32_t laser = (uint32_t)c.r[4];
    if (laser < 0x100000 || laser >= 0x01FFFF00) return;
    const uint32_t player = R32(laser + 0x40);
    if (player < 0x100000 || player >= 0x01FFF000) return;
    const uint32_t eq = R32(player + 0x608);
    const int id = eq >= 0x100000 && eq < 0x01FFFFC0 ? R8(eq + 0x14) : 0;
    const AddedWeapon* added = AddedOf(PlayerSlotOf(player), id);   // an added weapon's laser (weapons.ini laser=1)
    const bool mine = id != 10 && ((added && added->laser) || (!added && ExtraMapsLaser(id)));
    const bool want = mine && (R32(player + 0xB30) & 0x4000);
    static std::map<uint32_t, bool> shown;                  // the lasers shown here
    bool& on = shown[laser];
    const uint32_t flags = R32(laser + 0x44);
    static const bool logEach = getenv("SIM_LASER_LOG") != nullptr;   // tests: each change
    if (logEach) {
        static std::map<uint32_t, uint64_t> last;
        const uint64_t k = ((uint64_t)id << 40) | ((uint64_t)want << 39) | ((uint64_t)on << 38) | (flags & 0xFFFF) |
                           ((uint64_t)(R32(player + 0xB2C) & 0xFFFF) << 16);
        if (last[laser] != k) {
            last[laser] = k;
            Log("laser %08x: player %08x weapon %d, flags %08x %08x %08x, the laser's %04x, %s", laser, player, id,
                R32(player + 0xB2C), R32(player + 0xB30), R32(player + 0xB34), flags & 0xFFFF,
                want ? "aimed" : mine ? "held" : "another weapon");
        }
    }
    const bool off = added && added->laserOff;             // laser=0: its base's own laser hidden too (the P90's Scorpion)
    if (off ? !(flags & 3) : (want == on && (!want || (flags & 3) == 3))) return;
    ps2::Fn h = ps2::FindFn(V3(0x454FF8));
    if (!h) {
        if (Once((uint64_t)94 << 40)) Log("extra maps: the laser's messages (%08x) not found: no laser", V3(0x454FF8));
        return;
    }
    const Ctx keep = c;
    auto send = [&](uint32_t msg) { c.r[4] = laser; c.r[5] = msg; c.r[6] = 0; h(c); };
    if (want) { send(0x1B0001); send(0x1B0002); send(0x1B0005); }
    else { send(0x1B0003); send(0x1B0004); }
    c = keep;
    on = want;
    if (off) {
        if (Once(((uint64_t)108 << 40) | (uint32_t)id)) Log("extra maps: the laser of weapon %d hidden (the %s's laser=0)", id, added->name.c_str());
    } else if (Once(((uint64_t)93 << 40) | ((uint64_t)want << 32) | (uint32_t)id))
        Log("extra maps: the laser of weapon %d %s (its player aims%s)", id, want ? "shown" : "hidden", want ? "" : " no more");
}
// a model's group hidden (a model: its groups at [+0x70], [+0x6C] of them, 0x10 each: the group's code, its flags,
// bit 0 hidden; 0x138768 hides one, 0x1386F8 shows it): -1 when the model has no such group
static int GroupHidden(uint32_t model, uint32_t code) {
    const int n = (int)R32(model + 0x6C);
    const uint32_t t = R32(model + 0x70);
    if (n <= 0 || n > 64 || t < 0x100000 || t >= 0x01FFF000) return -1;
    for (int i = 0; i < n; ++i)
        if ((R32(t + 0x10 * i) & 0xFFFFFF) == code) return (int)(R32(t + 0x10 * i + 4) & 1);
    return -1;
}
// The team's armband on a body made for a map (docs/CHARACTERS.md, On the body): its act (0x4569F8, NewNetworkArmBand,
// a0 the band: its model at +0x40, the body's model at +0x1A0). A parka's upper arm near the eye in the first person
// view goes into a group under the head's (tools/mgo1_fit.py), hidden with the head; the band round it is a model
// of its own the game never hides: shown and hidden here with the body's head group 73ce1b (its one group b38bd6)
static void BandWithHead(Ctx& c) {
    const uint32_t band = (uint32_t)c.r[4];
    if (band < 0x100000 || band >= 0x01FFFE00) return;
    const uint32_t body = R32(band + 0x1A0), model = R32(band + 0x40);
    if (body < 0x100000 || body >= 0x01FFFF00 || model < 0x100000 || model >= 0x01FFFF00) return;
    const int head = GroupHidden(body, 0x73CE1B), mine = GroupHidden(model, 0xB38BD6);
    if (head < 0 || mine < 0 || head == mine) return;
    ps2::Fn f = ps2::FindFn(V(head ? 0x138768 : 0x1386F8));
    if (!f) return;
    const Ctx keep = c;
    c.r[4] = model; c.r[5] = 0xB38BD6;
    f(c);
    c = keep;
    if (Once(((uint64_t)96 << 40) | (uint64_t)head)) Log("extra maps: the armband %s with the head (the first person view%s)", head ? "hidden" : "shown", head ? "" : " left");
}
#include "heliport.h"                                   // map 15: the mission objects lost under the deck
// SIM_LOBBY_WATCH (tests): each actor's last handler called with message 0x80 (its action's tick)
#include "tsne.h"                                       // TEAM SNEAKING (docs/TEAM_SNEAKING.md)
// (v150-host beta 18) a TEAM SNEAKING sneaker the defense does not see now (his camouflage on, no alert), for the virtual
// dolls' eyes (vdoll_body.cpp Camouflaged)
bool VdollStealthed(uint32_t pl) { return TsneRound() && gTsneSideDone && TsneStealthed(pl); }
#include "basecap.h"                                    // BASE CAPTURE (docs/BASE_CAPTURE.md)
#include "extrarules.h"                                 // the extra maps' rules' play (docs/EXTRA_MAPS.md)
#include "prompts.h"                                    // the button prompts follow the device (docs/BUTTON_PROMPTS.md)
#include "inputwatch.h"                                 // the game's own holds on the player's controls, logged
const bool gTickWatch = getenv("SIM_LOBBY_WATCH") != nullptr;
std::map<uint32_t, uint32_t> gTickOf;
// (v141 beta 12) a game with no screen (MGO1_HEADLESS: the bots, the DOLLS) does not build the picture: DG's build
// (0x12AE98, its render actor's) not run, its caller told nothing was built (a fifth to a quarter of their processor;
// the DOLLS' aim, kills and moves the same on the bench). MGO1_NO_BUILD=0 builds it as before; MGO1_PERF_DG=1 (tests)
// times it, a line a minute
struct DgPerf { long long ticks = 0, n = 0, skipped = 0, since = 0; } gDgPerf;
bool NoBuild() {
    static const bool on = ps2::gHeadless && !(DevEnv("MGO1_NO_BUILD") && atoi(DevEnv("MGO1_NO_BUILD")) == 0);
    return on;
}
bool DgPerfOn() { static const bool on = DevEnv("MGO1_PERF_DG") || NoBuild(); return on; }
bool DgBuild(Ctx& c, uint32_t va) {
    static const bool skip = NoBuild(), timed = DevEnv("MGO1_PERF_DG") != nullptr;
    if (skip && !timed) {
        if (!gDgPerf.skipped++) Log("picture: not built (no screen); MGO1_NO_BUILD=0 builds it");
        c.r[2] = 0;
        return true;
    }
    const long long now = RealQpc(), f = QpcFreq();
    if (!gDgPerf.since) gDgPerf.since = now;
    if (skip) { ++gDgPerf.skipped; c.r[2] = 0; }
    else {
        const ps2::Fn fn = ps2::FindFn(va);
        if (!fn) return false;
        fn(c);
        gDgPerf.ticks += RealQpc() - now; ++gDgPerf.n;
    }
    if (now - gDgPerf.since >= 60 * f) {
        const double secs = (double)(now - gDgPerf.since) / (double)f, ms = 1000.0 / (double)f;
        Log("perf: the picture's build %.1f ms a second (%lld builds, %.2f ms each)%s", gDgPerf.ticks * ms / secs, gDgPerf.n,
            gDgPerf.n ? gDgPerf.ticks * ms / (double)gDgPerf.n : 0.0, skip ? " - not run (MGO1_NO_BUILD)" : "");
        gDgPerf = DgPerf(); gDgPerf.since = now;
    }
    return true;
}
// (v141 beta 13, tests) MGO1_PERF_CALLS=1: the game's calls counted by their address, the 60 most called each minute
struct PerfCall { uint32_t a = 0; uint64_t n = 0; };
std::vector<PerfCall> gPerfCalls;                     // open addressing, 1 << 16 entries
long long gPerfCallsSince = 0;
uint64_t gPerfCallsAll = 0;
void CountPerfCall(uint32_t va) {
    if (gPerfCalls.empty()) { gPerfCalls.resize(1 << 16); gPerfCallsSince = RealQpc(); }
    ++gPerfCallsAll;
    uint32_t h = (va * 2654435761u) >> 16;
    for (int k = 0; k < 64; ++k, h = (h + 1) & 0xffff) {
        PerfCall& e = gPerfCalls[h];
        if (e.a == va) { ++e.n; break; }
        if (!e.a) { e.a = va; e.n = 1; break; }
    }
    const long long now = RealQpc(), f = QpcFreq();
    if (now - gPerfCallsSince < 60 * f) return;
    const double secs = (double)(now - gPerfCallsSince) / (double)f;
    std::vector<PerfCall> v;
    for (const PerfCall& e : gPerfCalls) if (e.a) v.push_back(e);
    std::sort(v.begin(), v.end(), [](const PerfCall& x, const PerfCall& y) { return x.n > y.n; });
    uint64_t top = 0;
    for (size_t k = 0; k < v.size() && k < 60; ++k) top += v[k].n;
    Log("perf: %.2f million calls a second, %zu functions; the 60 most called %.1f %% of them", gPerfCallsAll / secs / 1e6, v.size(),
        gPerfCallsAll ? 100.0 * (double)top / (double)gPerfCallsAll : 0.0);
    for (size_t k = 0; k < v.size() && k < 60; k += 10) {
        std::string l;
        for (size_t j = k; j < v.size() && j < k + 10; ++j) {
            char b[40]; snprintf(b, sizeof b, " %06x %.2f%%", v[j].a, 100.0 * (double)v[j].n / (double)gPerfCallsAll); l += b;
        }
        Log("perf: calls%s", l.c_str());
    }
    std::fill(gPerfCalls.begin(), gPerfCalls.end(), PerfCall());
    gPerfCallsAll = 0; gPerfCallsSince = now;
}
// (v141 beta 13) the calls DispatchBridge never serves go straight to their recompiled code: the base's functions the
// game calls most (MGO1_PERF_CALLS on a host and two dolls, Killhouse A and City Under Siege: 37 % of the calls), each
// named by no hook of the platform (a search of every .cpp and .h for its address). Not for another version's program
// (its addresses), a call of the actor loop (the actor whose act runs, the motion marks), with a ladder's copy followed
// (LadderSpeed), nor with a test switch that sees every call or the calls it names. MGO1_FAST_CALLS=0: every call
// through the bridge
const uint32_t kPlainCalls[] = {
    0x10b218, 0x10b228, 0x10b2b8, 0x10e420, 0x122880, 0x1304f8, 0x130690, 0x138698, 0x1481a0, 0x148770,
    0x14a640, 0x14e850, 0x14e8e8, 0x151960, 0x1542d8, 0x154978, 0x1578f0, 0x158fd0, 0x159228, 0x159640,
    0x15b088, 0x15b200, 0x15b558, 0x15b590, 0x15ba90, 0x15c6a8, 0x1716d0, 0x1716f8, 0x171740, 0x171758,
    0x171788, 0x1717f0, 0x171ce8, 0x179560, 0x181418, 0x1816e0, 0x181798, 0x1853c8, 0x1ee1a4,
};
uint32_t gPlainBits[((0x2DA680 - 0x100000) >> 7) + 1];  // a bit a word of the base's code
bool FastCallsOn() {
    if (const char* e = DevEnv("MGO1_FAST_CALLS")) if (atoi(e) == 0) return false;
    static const char* const kTests[] = {"SIM_LOBBY_WATCH", "SIM_SE_PROBE", "SIM_WATCH_CALLS", "SIM_STRARGS", "SIM_FN_DIFF",
        "SIM_POOL_CHECK", "SIM_POOL_WALK_FROM", "SIM_POOL_TRACE", "TRACE_ADDRS", "SIM_TRACE_FNS", "SIM_RETVALS", "SIM_CALLERS",
        "SIM_CALL_COUNTS", "SIM_TEXT_FNS", "SIM_BRIDGE_LOG"};
    for (const char* t : kTests) if (DevEnv(t)) { Log("calls: every call through the bridge (%s)", t); return false; }
    int n = 0;
    for (uint32_t a : kPlainCalls)
        if (ps2::IsFn(a)) { gPlainBits[(a - 0x100000) >> 7] |= 1u << ((a >> 2) & 31); ++n; }
        else Log("calls: %06x is not a recompiled function of the base: through the bridge", a);
    Log("calls: %d of the base's functions straight to their code (MGO1_FAST_CALLS=0: through the bridge)", n);
    return n > 0;
}
bool DispatchBridge(Ctx& c, uint32_t va) {
    static const bool countCalls = DevEnv("MGO1_PERF_CALLS") != nullptr;
    if (countCalls) CountPerfCall(va);
#ifndef MGO1_PLAYER_BUILD
    static int fastCalls = -1;                           // (decided at the first call of the US program, natively)
    if (fastCalls && gNative && gUsProgram && va - 0x100000u < 0x2DA680u - 0x100000u) {
        if (fastCalls < 0) fastCalls = FastCallsOn() ? 1 : 0;
        if (fastCalls && (gPlainBits[(va - 0x100000u) >> 7] >> ((va >> 2) & 31) & 1) && !gLadderSpeedN) {
            const uint32_t ra = (uint32_t)c.r[31];
            if (ra != 0x1229e4 && ra != 0x122a3c) return false;
        }
    }
#endif
    // the platform's addresses are the US program's: another version's call is served by its US address
    // (none: a sentinel no service answers), its own code found by its own
    if (gTickWatch && c.r[6] == 0x80 && va >= 0x2DA680 && va < 0x02000000) gTickOf[(uint32_t)c.r[4] & 0x01ffffff] = va;
    uint32_t addr = va;
    if (!gUsProgram) { addr = U(va); if (!addr) addr = 0xFFFFFFF0u; }
    if (addr == 0x12ae98 && gNative && DgPerfOn() && DgBuild(c, va)) return true;
    if (addr == 0x159d88) {
        const int bad = RayWalkBad(c);
        static int logged = 0, traced = 0;
        auto vec = [](uint32_t p, char* o, size_t n) {
            if (p < 0x100000u || (p & 0x01FFFFFFu) > 0x01FFFFF0u) { snprintf(o, n, "(%08x)", p); return; }
            snprintf(o, n, "%08x(%g %g %g %g)", p, BitsF(R32(p)), BitsF(R32(p + 4)), BitsF(R32(p + 8)), BitsF(R32(p + 12)));
        };
        if (bad == 1) {
            if (logged++ < GuardCap()) {
                char va[96], vb[96]; vec((uint32_t)c.r[6], va, sizeof va); vec((uint32_t)c.r[7], vb, sizeof vb);
                if (DevEnv("SIM_GUARD_TRACE")) Log("a ray walk with a vector past 10^12 in x, y or z (from %08x): no hit; a2 %s a3 %s f12 %g a0 %08x a1 %08x%s",
                    (uint32_t)c.r[31] - 8, va, vb, c.f[12], (uint32_t)c.r[4], (uint32_t)c.r[5], GuardChain(c).c_str());
                else Log("a ray walk with a vector past 10^12 in x, y or z (from %08x): no hit (a vector left undefined: its walk would never end)", (uint32_t)c.r[31] - 8);
            }
            c.r[2] = 0;
            return true;
        }
        if (bad == 2 && DevEnv("SIM_GUARD_TRACE")) {      // tests: the rays the four words' check refused, walked
            const ps2::Fn fn = ps2::FindFn(va);
            if (fn) {
                char sa[96], sb[96]; vec((uint32_t)c.r[6], sa, sizeof sa); vec((uint32_t)c.r[7], sb, sizeof sb);
                const uint32_t from = (uint32_t)c.r[31] - 8;
                const std::string chain = GuardChain(c);
                const float f12 = c.f[12];
                fn(c);
                static int hits = 0;
                if (c.r[2]) ++hits;
                if (traced++ < GuardCap()) Log("guard trace: a ray walk with a fourth word past 10^12 (from %08x): walked, %u hits (%d of %d so far); a2 %s a3 %s f12 %g%s",
                    from, (uint32_t)c.r[2], hits, traced, sa, sb, f12, chain.c_str());
                else if (traced % 500 == 0) Log("guard trace: %d rays with a fourth word past 10^12 walked, %d with a hit", traced, hits);
                return true;
            }
        }
    }
    // the collision grid's chain walk (0x1562F8, a0 the chain): a query above the grid (Graniny Gorki Lab's
    // top at 14 m, a point at 16.9 m) reads a cell past the radix table and walks garbage, an entry whose
    // next is itself (the dedicated host hung, 28 September; on a PS2 its address, past 32 MB, is a bus
    // error): a chain outside the RAM is no hit, and a walk past 5 million turns is stopped. The RAM: its
    // 32 MB in the segments 0, 2, 3, 8 and 0xA, and the scratchpad (0x70000000, 16 KB), where the game
    // keeps the chains of the moving things (v133's audit: taken for garbage, the bullets hit nobody)
    static const bool guardTrace = DevEnv("SIM_GUARD_TRACE") != nullptr;
    if (guardTrace && addr == 0x161fd8) {                 // tests: the grid's point queries of the capture's item
        const auto it = gNativeActors.find(gActActor);
        static long long lastF = -100; static int n = 0;
        if (it != gNativeActors.end() && it->second.find("ItemChara") != std::string::npos && gFrameNo - lastF >= 30 && n < 200) {
            lastF = gFrameNo; ++n;
            const uint32_t p = (uint32_t)c.r[4];
            Log("guard trace: point query %08x (%g %g %g) code %08x by %s", p, BitsF(R32(p)), BitsF(R32(p + 4)), BitsF(R32(p + 8)), (uint32_t)c.r[6], it->second.c_str());
        }
    }
    static uint32_t lastGrid = 0; static float lastPt[3] = {0, 0, 0};
    if (guardTrace && addr == 0x156920) {                 // tests: a grid's query outside the grid
        const uint32_t g = (uint32_t)c.r[4], p = (uint32_t)c.r[6], r = (uint32_t)c.r[7];
        lastGrid = g;
        if (p >= 0x100000u && p < 0x01FFFFF0u) for (int k = 0; k < 3; ++k) lastPt[k] = BitsF(R32(p + 4 * k));
        // (beta 18, follow-up) once per grid, actor and verdict: whether the PC's old rounding read a cell layer
        // past this grid for this query (its top end one past the grid's size on an axis the box reaches)
        static std::set<std::string> seen;
        if (g >= 0x100000u && g < 0x01FFFF00u && p >= 0x100000u && seen.size() < 300) {
            float o[3], s[3], pt[3]; int d[3];
            for (int k = 0; k < 3; ++k) { o[k] = BitsF(R32(g + 4 * k)); s[k] = (float)(int16_t)R16(g + 0x10 + 2 * k); d[k] = (int16_t)R16(g + 0x18 + 2 * k); pt[k] = BitsF(R32(p + 4 * k)); }
            const float rad = r >= 0x100000u ? BitsF(R32(r)) : 0.0f;
            bool out = false, risk = false;
            int lo[3] = {0, 0, 0}, endNear[3] = {0, 0, 0}, endPs2[3] = {0, 0, 0};
            for (int k = 0; k < 3; ++k) {
                if (pt[k] - rad < o[k] || pt[k] + rad > o[k] + s[k] * d[k]) out = true;
                if (s[k] <= 0) continue;
                lo[k] = (int)std::max((pt[k] - rad - o[k]) / s[k], 0.0f);
                const float hi = (pt[k] + rad - o[k]) / s[k];
                endNear[k] = GridTopEnd(hi, (float)d[k]);
                int e = 0; WithPs2Rounding([&] { e = GridTopEnd(hi, (float)d[k]); }); endPs2[k] = e;
            }
            if (out) {
                bool reach = true;
                for (int k = 0; k < 3; ++k) if (s[k] <= 0 || lo[k] >= endNear[k]) reach = false;
                for (int k = 0; k < 3; ++k) if (reach && s[k] > 0 && endNear[k] > d[k] && lo[k] <= d[k]) risk = true;
                const auto it = gNativeActors.find(gActActor);
                const std::string who = it != gNativeActors.end() ? it->second : "?";
                char key[96]; snprintf(key, sizeof key, "%08x %s %d", g, who.c_str(), risk ? 1 : 0);
                if (seen.insert(key).second)
                    Log("guard trace: grid %08x query outside it by %s: point (%g %g %g) radius %g, grid from (%g %g %g) cells (%g %g %g) x (%d %d %d): cells y %d..%d (PS2) %d (PC before): %s%s",
                        g, who.c_str(), pt[0], pt[1], pt[2], rad, o[0], o[1], o[2], s[0], s[1], s[2], d[0], d[1], d[2], lo[1], endPs2[1], endNear[1],
                        risk ? "AT RISK (read past the grid with the PC's rounding)" : "no cell past the grid", GuardChain(c).c_str());
            }
        }
    }
    if (guardTrace && addr == 0x1564a0) {                 // tests: a cell's chain list whose count overflows the scratchpad's buffer
        static int n = 0;
        uint32_t e = (uint32_t)c.r[4];
        for (int k = 0; k < 64 && e >= 0x100000u && e < 0x01FFFF00u && n < 30; ++k, e += 0x18) {
            const int cnt = (int16_t)R16(e + 2);
            if (cnt >= 0x1B00) {
                ++n;
                std::string h;
                if (lastGrid >= 0x100000u && lastGrid < 0x01FFFF00u)
                    for (int j = 0; j < 12; ++j) { char b[12]; snprintf(b, sizeof b, " %08x", R32(lastGrid + 4 * j)); h += b; }
                Log("guard trace: chain list %08x entry %d: count %d (flags %02x, data %08x); grid %08x point (%g %g %g), its header%s%s", (uint32_t)c.r[4], k, cnt, R8(e), R32(e + 8),
                    lastGrid, lastPt[0], lastPt[1], lastPt[2], h.c_str(), GuardChain(c).c_str());
            }
            if (R8(e) & 1) break;
        }
    }
    // (v150-host beta 18) the collision grid's three box queries (0x156920 a point or a sphere, 0x1599C8 a box,
    // 0x15BD58) round their box's top cell up with "+ 0.99999994, then to an integer" after clamping it to the
    // grid's size. The PS2 rounds every float operation toward zero: 2 + 0.99999994 is 2.9999998, cell 2 is the
    // top's end; rounded to the nearest (the PC's default) it is 3.0, one cell past the grid. A query above the
    // grid then reads a cell past the grid's table: the capture item's "Drop_Item" test at 9.5 m above the
    // KEROTAN on Graniny Gorki Lab's upper floor (y 16.9 m, the grid's top 14 m) read counts of 16 KB there, the
    // game's DMA overran its scratchpad buffer and smashed the scratchpad, and its chain walk followed a float
    // (the chain walk's guard below: "outside the memory"). These three run with the PS2's rounding.
    // SIM_GRID_NEAREST=1 (tests): the PC's rounding, as before
    if (addr == 0x156920 || addr == 0x1599c8 || addr == 0x15bd58) {
        static const bool nearest = DevEnv("SIM_GRID_NEAREST") != nullptr;
        const ps2::Fn fn = nearest ? ps2::Fn() : ps2::FindFn(va);
        if (fn) { WithPs2Rounding([&] { fn(c); }); return true; }
    }
    if (addr == 0x1562f8) {
        static int logged = 0;
        const uint32_t chain = (uint32_t)c.r[4], seg = chain >> 28, off = chain & 0x0FFFFFFFu;
        const bool ram = seg == 7 ? off < 0x4000u : (seg == 0 || seg == 2 || seg == 3 || seg == 8 || seg == 0xA) && off < 0x02000000u;
        if (chain && !ram) {
            if (logged++ < GuardCap()) Log("a collision chain walk from %08x (from %08x): outside the memory, no hit (a query read past its grid's cells)%s", chain, (uint32_t)c.r[31] - 8, GuardChain(c).c_str());
            c.r[2] = 0;
            return true;
        }
        if (!ps2::CallBounded(c, va, 5000000) && logged++ < 3) Log("a collision chain walk from %08x (from %08x) never ended: stopped, no hit", chain, (uint32_t)c.r[31] - 8);
        return true;
    }
    if (gNative) {                                            // the actor whose act runs (the actor loop's calls, a0: the actor)
        static const uint32_t actA = V(0x1229e4), actB = V(0x122a3c);
        const uint32_t ra = (uint32_t)c.r[31];
        if (ra == actA || ra == actB) {
            gActActor = (uint32_t)c.r[4];
            if (!gMidWatch.empty()) {                          // tests: SIM_MID_WRITES
                const auto it = gNativeActors.find(gActActor);
                MidWatchCompare("an act's start");
                char b[96]; snprintf(b, sizeof b, "the act of %08x %s (act %08x)", gActActor, it != gNativeActors.end() ? it->second.c_str() : "?", va);
                gMidWatchSince = b;
            }
        }
    }
    if (gNative && !gMidWatch.empty() && (addr == 0x122768 || addr == 0x12ae98)) {   // tests: SIM_MID_WRITES
        MidWatchCompare(addr == 0x122768 ? "a pass's start" : gInExtra ? "the picture between's build start" : "the tick's build start");
        if (addr == 0x12ae98) MidWatchPlaces(gInExtra ? "build start (picture between)" : "build start (tick)");
    }
    // a 3D label's text located (the label system's draw, US n003a 0x5357F0, calls 0x107918 with the font's x and y,
    // s0 its entry, the text at +0x10): a wide picture's HUD narrows its glyphs about that place (gs.cpp GsLabelAt)
    if (addr == 0x11F528 && gNative) PromptsGlyphDrawn(c);   // a 2-byte glyph drawn into a text: the button prompts' (prompts.h)
    if (addr == 0x107918 && gNative && (uint32_t)c.r[31] == V3(0x535C08)) {
        const uint32_t e = (uint32_t)c.r[16];
        const std::string text = e >= 0x100000 && e < 0x01FFF000 ? PS2Str(e + 0x10, 64) : std::string(8, 'x');
        const int chars = (int)std::count_if(text.begin(), text.end(), [](char ch) { return ch != ' '; });   // sixty: its glyphs
        ps2::GsLabelAt((int)(int32_t)(uint32_t)c.r[5], (int)(int32_t)(uint32_t)c.r[6], chars);
        static int told = 0;
        if (told < 3 && Once(((uint64_t)122 << 40) | (e & 0xFFFFFF))) {
            ++told;
            Log("hud: a 3D label located at %d %d: \"%s\"", (int)(int32_t)(uint32_t)c.r[5], (int)(int32_t)(uint32_t)c.r[6], PS2Str(e + 0x10, 64).c_str());
        }
    }
    if (gLadderSpeedN) LadderSpeed(c);                        // a newer climber's copy on this older program: his speeds
    // SIM_SE_PROBE=<frame> (tests, #112 the added weapons' sounds): from that frame, the effects played at a place
    // (0x115310, 0x1153B8, 0x115440: a0 the effect's code, a3 its place) and the started effects' commands (0x180C98,
    // 0x43 ...), each with the actor whose act runs (the actor loop's calls)
    if (gNative) {
        static const char* seEnv = getenv("SIM_SE_PROBE");
        static const long long seFrom = seEnv ? atoll(seEnv) : -1;
        if (seFrom >= 0) {
            const uint32_t ra = (uint32_t)c.r[31];
            static int lines = 0;
            const bool place = addr == 0x115310 || addr == 0x1153B8 || addr == 0x115440;
            if ((place || (addr == 0x180C98 && ((uint32_t)c.r[4] >> 24) == 0x43)) && gFrameNo >= seFrom && lines < 4000) {
                ++lines;
                const auto it = gNativeActors.find(gActActor);
                const uint32_t p = (uint32_t)c.r[7];
                char at[64] = "";
                if (place && p >= 0x100000 && p < 0x01FFF000) snprintf(at, sizeof at, " (%.0f %.0f %.0f)", F32(p), F32(p + 4), F32(p + 8));
                Log("se probe: %s a0 %08x a1 %08x a2 %08x a3 %08x%s from %08x, the act of %08x (%s)", place ? "at a place" : "started",
                    (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], p, at, ra - 8, gActActor,
                    it != gNativeActors.end() ? it->second.c_str() : "?");
            }
        }
    }
    if (gNative && UpstreamOn()) {                           // the upstream bandwidth's choices (ntmenu2) and its setter
        if (addr == 0x16EBA0 && !OlderProgram()) { UpstreamBots(c, true); UpstreamSet(c); UpstreamBots(c, false); return true; }
        if (addr == 0x16D420) UpstreamElapsed();
        if (gPageNt && addr == 0x377840) { c.r[2] = UpstreamKbps((uint32_t)c.r[4] & 0xffff); return true; }
        if (gPageNt && addr == 0x377800) { c.r[2] = UpstreamChoice((uint32_t)c.r[4] & 0xffff); return true; }
    }
    if (gNative && addr == 0x122768) InputHoldsWatch();      // the game's holds on the player's controls (inputwatch.h)
    if (gNative && addr == 0x122768) PromptsTick();          // the button prompts (prompts.h)
    if (gNative && addr == 0x122768) ClocksTick();           // (v150-host beta 19) the round's clocks on the wall clock (clocks.h)
    if (gNative && addr == 0x122768) { SpawnFixups(c); HostStillWatch(); VdollTick(c); UpstreamOlder(); MaxPlayers(); ChannelsWatch(); PauseWatch(c); VoiceTick(); ExtraPicture(c); LadderCopies(c); SimLadderCheck(c); SimLadderWatch(c); ExtraMinimapTick(); ExtraTextsTick(); SimForceSettings(); SimKill(); PoolRoomTick(); AddedWeaponsTick(c); TsneTick(c); BcTick(c); ExtraRulesTick(c); if (BcPassAfter(c, va)) return true; }   // a pass of the actor lists starts (BASE CAPTURE: BcPassAfter runs it, then its HUD texts)
    // SIM_FORCE_BODY=<0..8> (tests, the North American program on City Under Siege's page and the maps built on it):
    // this player's body taken as that character's (0x4096E0: a player's model name from his character [+0xB62], the
    // page's table 0x2F1310: KGB, GRU, Ocelot Unit, Sokolov, Raikov, Ocelot, Reiko, Rowdy, Snake); the rules page
    // must hold it
    const bool n3 = gNative && N3Page();                     // City Under Siege's page or a paired one is loaded
    const uint32_t a3 = n3 ? A3(addr) : 0;                   // the call's n003a address (0: a base function, or none paired)
    const uint32_t ga = a3 ? a3 : gNative && gPageMap ? NetBlastPaged(addr) : 0;   // netcode: every map page (netfix.h)
    if (ga && (NetBlastSyncOn() || NetBlastTraceOn()) && NetBlast(c, ga, va)) return true;   // netcode: the grenades between PCs (netfix.h)
    {   // (v150-host beta 21) a claymore's sensor (US n003a 0x3A8740, every map page's own): a virtual doll sets it off (VdollTrapSensor)
        static uint32_t gen = 0xffffffffu, fn = 0;
        if (gen != gPageGen) { gen = gPageGen; fn = gNative && gUsProgram && (gPageN3 || gPageMap) ? P3(0x3A8740) : 0; }
        if (fn && addr == fn && VdollTrapSensor(c, va)) return true;
    }
    {   // (v150-host beta 22, support) a claymore's act (US n003a 0x3A8ED0, every map page's own): a virtual doll's claymore goes off
        static uint32_t gen = 0xffffffffu, fn = 0;
        if (gen != gPageGen) { gen = gPageGen; fn = gNative && gUsProgram && (gPageN3 || gPageMap) ? P3(0x3A8ED0) : 0; }
        if (fn && addr == fn) VdollClayAct(c);
    }
    {   // (v150-host beta 16) a spawn back in play begins (US n003a 0x40D1A0, every map page's own): the character it asks for
        static uint32_t gen = 0xffffffffu, fn = 0;
        if (gen != gPageGen) { gen = gPageGen; fn = gNative && gUsProgram && (gPageN3 || gPageMap) ? P3(0x40D1A0) : 0; }
        if (fn && addr == fn && SpawnCharacterGuardOn()) SpawnCharacterAtSpawn(c);
    }
    if (n3 && gUsProgram && a3 == 0x4096E0) {
        // SIM_FORCE_SKIN=<0..8> (tests): this player's character itself (+0xB62: his model, armband, radio and the
        // equipment of his class then follow it), where SIM_FORCE_BODY changes the model only
        static const int skin = getenv("SIM_FORCE_SKIN") ? atoi(getenv("SIM_FORCE_SKIN")) : -1;
        if (skin >= 0 && skin <= 8) {
            const uint32_t pl = (uint32_t)c.r[4];
            const int self = OwnSlot();
            if (self >= 0 && pl == PlayerOfSlot(self) && R8(pl + 0xB62) != (uint8_t)skin) {
                Log("SIM_FORCE_SKIN: this player's character %d in place of %d", skin, R8(pl + 0xB62));
                W8(pl + 0xB62, (uint8_t)skin);
            }
        }
        static const int body = getenv("SIM_FORCE_BODY") ? atoi(getenv("SIM_FORCE_BODY")) : -1;
        if (body >= 0 && body <= 8)
            if (ps2::Fn f = ps2::FindFn(va)) {
                const uint32_t pl = (uint32_t)c.r[4];
                const uint64_t ra = c.r[31];
                f(c);
                c.r[31] = ra;
                const int self = OwnSlot();
                if (self >= 0 && pl == PlayerOfSlot(self)) {
                    const uint32_t was = (uint32_t)c.r[2];
                    c.r[2] = R32(PageData(0x4096E0, 0, 0x2F1310) + 4 * (uint32_t)body);
                    if (Once(((uint64_t)74 << 40) | pl)) Log("SIM_FORCE_BODY: this player's body %06x in place of %06x", (uint32_t)c.r[2], was);
                }
                return true;
            }
    }
    if (gNative && (gBigGame || SimFamily1()) && addr == 0x174790) BigGameChannel(c);   // a channel opened (a big game: 0x60..0x70 at 0x70..0x80)
    if (gNative && gPageNt && addr == 0x35F1A8) BigGameCreate((uint32_t)c.r[4]);   // a game's creation
    if (gNative && gPageNt && addr == 0x35B5B8) JoinChosen(c);                     // a game chosen to join
    if (gNative && gPageNt && addr == 0x36DB18) NetJoinFailed();                   // netcode: NewHostJoin failed, 1059 (netfix.h)
    if (gNative && gPageNt && addr == 0x35F9A0) NetSimHostOnly((uint32_t)c.r[4]);  // netcode: tests, Host Only (netfix.h)
    if (gNative && gPal60 && addr == 0x12ad90 && R32(kHalfRate)) { gPalGroupVb = 0; gPalLastKick = 0; }   // a tick's picture goes out
    if (gNative && addr == 0x12ad90 && Exact60()) PaceKick(R32(kHalfRate) != 0);   // the pace guard's measure (a tick's picture: half flag on)
    if (gNative && gPalTickPending && addr == 0x143dc0 && R32(kHalfRate) && !R32(kHalfPhase)) {
        gPalTickPending = false;                            // PAL at 60: the tick's own picture, drawn at its time too
        if (ps2::Fn f = ps2::FindFn(va)) {
            MidBegin(c, gPalTickT);
            f(c);
            MidEnd(true);
            return true;
        }
    }
    if (gNative && addr == 0x128fe8) gChanlSets[(uint32_t)c.r[4]] = {c.f[13], c.f[14]};   // a view's camera set
    if (gNative && addr == 0x101B28) {                        // SIM_FORCE_MAP=n0XXa (tests): a map's page load asks for that one
        static const char* force = DevEnv("SIM_FORCE_MAP");
        if (force && strlen(force) == 5) {
            const uint32_t nm = (uint32_t)c.r[4];
            char cur[8] = {0};
            for (int k = 0; k < 6; ++k) cur[k] = (char)R8(nm + k);
            if (cur[0] == 'n' && cur[1] == '0' && cur[4] == 'a' && !cur[5] && strcmp(cur, force)) {
                for (int k = 0; k < 5; ++k) W8(nm + k, (uint8_t)force[k]);
                Log("SIM_FORCE_MAP: the page %s asked, %s loaded instead", cur, force);
            }
        }
    }
    if (gNative && addr == 0x194C40) {                       // the 0x4310 builder: the host's record of the game, then the lobby server's view of a PC game
        static const bool hostOnly = DevEnv("MGO1_HOST_ONLY") || DevEnv("MGO1_HOST_SETTINGS");
        if (hostOnly && !gHostRotation.empty()) {            // (v141) the host's rotation as written: LobbyCreateMessage tells
            const uint32_t st = (uint32_t)c.r[5] & 0x01ffffff;  // the lobby server its stages' maps and the host list's marks
            if (st >= 0x100000 && st + 0x2b0 < 0x02000000)
                for (int i = 0; i < 16; ++i) {
                    const bool on = i < (int)gHostRotation.size();
                    W8(st + 564 + i, on ? gHostRotation[(size_t)i].first : 0); W8(st + 580 + i, on ? gHostRotation[(size_t)i].second : 0);
                }
        }
#ifndef MGO1_PLAYER_BUILD
        HostReports(c, addr);
#endif
        // (v141 beta 5) a Host Only game of 9 (8 players and the host) told as 9: the host list shows 8 seats (told as 8 it
        // showed 7, and the lobby took 7). A joiner takes a game for a big one from the list's hint only (JoinChosen);
        // MGO1_HOST_TELL_8=1 (server.env HOST_LIST_9_AS_8): told as 8 again, for the MGO1.exe of v141 (8 > 9 there)
        static const bool tell8 = DevEnv("MGO1_HOST_TELL_8") != nullptr;
        if (hostOnly && tell8) {
            const uint32_t st = (uint32_t)c.r[5] & 0x01ffffff;
            if (st >= 0x100000 && st + 0x2b0 < 0x02000000 && R8(st + 599) == 9) W8(st + 599, 8);
        }
        if (hostOnly) Log("host settings: 0x4310 from %08x: \"%.16s\", host only %u, rotation %02x:%02x, %u players at most, switches %02x %02x", (uint32_t)c.r[5],
            (const char*)gMem + ((uint32_t)c.r[5] & 0x01ffffff) + 4, R8((uint32_t)c.r[5] + 167), R8((uint32_t)c.r[5] + 564), R8((uint32_t)c.r[5] + 580), R8((uint32_t)c.r[5] + 599),
            R8((uint32_t)c.r[5] + 681), R8((uint32_t)c.r[5] + 682));
        if (hostOnly) gHostFF = (R8((uint32_t)c.r[5] + 681) & 0x10) ? 1 : 0;   // (v150-host beta 22, wake: the game's friendly fire, for the virtual dolls)
        if (LobbyCreateMessage(c, va)) return true;
    }
    // (v141 beta 5) ntmenu2's check of the host's record for REIKO HINOMOTO/ROWDY REIKO (0x360660, from the Create Game
    // page 3's values read and written back, 0x3606D0: 0, the switch cleared and its line locked): a dedicated host whose
    // settings ask for them passes it
    if (gNative && gPageNt && gHostRumble && addr == 0x360660) {
        if (Once((uint64_t)131 << 40)) Log("host settings: REIKO HINOMOTO/ROWDY REIKO kept on (the game's check of the host's record passed over)");
        c.r[2] = 1; c.r[3] = 0;
        return true;
    }
    if (gNative && addr == 0x194838) {                       // open list: Create Game's last settings (openfix.h), then a dedicated host's
        static const bool hostOnly = DevEnv("MGO1_HOST_ONLY") || DevEnv("MGO1_HOST_SETTINGS");
        const uint32_t ctx = (uint32_t)c.r[4];
        if (LastSettingsRead(c, va)) { if (hostOnly && ctx) PatchHostSettings(ctx + 28496); return true; }
    }
    if (gNative && gPageNt && addr == 0x3498F8) {        // ntmenu2's [27b5ee]: the lists of maps and rules
        if (ps2::Fn f = ps2::FindFn(va)) {
            const uint64_t ra = c.r[31]; f(c); c.r[31] = ra;
            if (ExtraMapsAny()) LobbyMapLists();
            RulesListRestore();                          // again on the same page: the stock list of rules back (basecap.h)
            TsneLobby(GetterAddr(V(0x349aa0)));          // TEAM SNEAKING (tsne.h)
            BcLobby(GetterAddr(V(0x349aa0)));            // BASE CAPTURE (basecap.h), above it
            RulesListKeep();
            return true;
        }
    }
    if (gNative && gPageNt && (addr == 0x360610 || addr == 0x3605C0) && LobbyRulesRow(c, addr, va)) return true;   // logged (basecap.h)
    if (gNative && gPageNt && addr == 0x35EE70 && LobbySettingsCheck(c, va)) return true;   // the last game's TSNE and BC entries (basecap.h)
    if (gNative && gPageNt && BcLobbyCall(c, addr, va)) return true;   // BASE CAPTURE (basecap.h): before TEAM SNEAKING's
    if (gNative && gPageNt && TsneLobbyCall(c, addr, va)) return true;
    if (gNative && gNamesCount && addr == 0x125408) {     // a string of the scripts: the extra maps' names (kNamesBase ...)
        const uint32_t i = (uint32_t)c.r[4];
        if (i >= kNamesBase && i < kNamesBase + 3 * gNamesCount) {
            const uint32_t k = i - kNamesBase, kind = k / gNamesCount, j = k % gNamesCount;
            if (j < 12) c.r[4] = gNamesOrig + kind * 12 + j;   // the script's own
            else { c.r[2] = NameString((int)j + 1, (int)kind); c.r[3] = 0; return true; }
        }
    }
    // SIM_DROP_PROBE=1 (tests, the name of a dropped weapon, Snake 23:22): the drops' records (US n003a 0x39FE20: a0
    // the record's number, its record at [0x2E5268] + 0x60 a0 + 0xD0), the boxes made (0x4C6660) and the labels of the
    // items (0x4C54D0: a0 the item, +0x44 its code, +0x48 its count, +0x208 a name of its own; a2 the label's kind)
    if (n3) {
        static const bool probe = getenv("SIM_DROP_PROBE") != nullptr;
        if (probe && a3 == 0x39FE20) {
            const uint32_t mgr = R32(N3DropsRef()), i = (uint32_t)c.r[4];
            std::string rec;
            if (mgr && i < 16)
                for (uint32_t o = 0; o < 0x60; o += 4) { char b[12]; snprintf(b, sizeof b, " %08x", R32(mgr + 0x60 * i + 0xD0 + o)); rec += b; }
            Log("drop probe: record %u (a1 %#x) from %08x:%s", i, (uint32_t)c.r[5], (uint32_t)c.r[31] - 8, rec.c_str());
        } else if (probe && a3 == 0x4C6660) {
            const uint32_t sp = (uint32_t)c.r[29];
            Log("drop probe: box made, a0 %#x a1 %#x a2 %#x a3 %#x t0 %#x t1 %#x t2 %08x (%.0f %.0f %.0f) t3 %#x, its code %#x, %#x %#x %#x",
                (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], (uint32_t)c.r[7], (uint32_t)c.r[8], (uint32_t)c.r[9], (uint32_t)c.r[10],
                F32((uint32_t)c.r[10]), F32((uint32_t)c.r[10] + 4), F32((uint32_t)c.r[10] + 8), (uint32_t)c.r[11],
                R32(sp), R32(sp + 8), R32(sp + 0x10), R32(sp + 0x18));
        } else if (probe && a3 == 0x4C54D0) {
            const uint32_t it = (uint32_t)c.r[4];
            if (Once(((uint64_t)105 << 40) | ((uint64_t)(it & 0xffffff) << 8) | ((uint32_t)c.r[6] & 0xff)))
                Log("drop probe: label of %08x (kind %u): code %#x, count %d, its own name %08x \"%s\", +0x40 %08x +0x1f8 %08x",
                    it, (uint32_t)c.r[6], R32(it + 0x44), (int)R32(it + 0x48), R32(it + 0x208), PS2Str(R32(it + 0x208), 32).c_str(), R32(it + 0x40), R32(it + 0x1f8));
        }
    }
    // SIM_WEAPON_PROBE=1 (tests, the added weapons): who a weapon's actor is made for. The factory (0x45D770: a0 the
    // player's weapon component, a1 the item's code, a2 a byte of the caller's), the weapons' inits (a1, a2), the
    // stance's lookup (0x2FCB60) at its four callers, and the player actors holding the component's handle
    if (n3) {
        static const bool probe = getenv("SIM_WEAPON_PROBE") != nullptr;
        if (probe) {
            const uint32_t ra8 = (uint32_t)c.r[31] - 8;
            auto holders = [&](uint64_t handle, uint32_t comp) {
                std::string s;
                for (const auto& a : gNativeActors) {
                    if (a.second != "NewNetworkPlayer" && a.second != "NewNetworkDummyPlayer") continue;
                    for (uint32_t off = 0; off < 0x1000; off += 4) {
                        const uint32_t w = R32(a.first + off);
                        if ((handle && w == (uint32_t)handle && R32(a.first + off + 4) == (uint32_t)(handle >> 32)) || w == comp) {
                            char t[96]; snprintf(t, sizeof t, " %s %08x (slot %u) +%#x%s", a.second == "NewNetworkPlayer" ? "player" : "copy",
                                                 a.first, R8(a.first + 0xB60), off, w == comp ? " (its address)" : "");
                            s += t;
                        }
                    }
                }
                return s.empty() ? std::string(" none") : s;
            };
            const uint32_t code = (uint32_t)c.r[5] & 0xFF;
            if (a3 == 0x45D770 && (code == 5 || code == 6 || code == 0x0A || code == 0x0B || code == 0x0C || code == 0x0F)) {
                const uint32_t comp = (uint32_t)c.r[4];
                const uint64_t handle = (uint64_t)R32(comp + 0x18) | (uint64_t)R32(comp + 0x1C) << 32;
                Log("weapon probe: factory, item %#x, a2 %#x, component %08x (handle %016llx, +0x5C %08x +0x60 %08x +0x64 %08x) from %08x; held by:%s",
                    code, (uint32_t)c.r[6], comp, (unsigned long long)handle, R32(comp + 0x5C), R32(comp + 0x60), R32(comp + 0x64), ra8,
                    holders(handle, comp).c_str());
            } else if (a3 == 0x4D68A8 || a3 == 0x4CF250 || a3 == 0x4D33C0 || a3 == 0x4E6EB8 || a3 == 0x4D52F8 || a3 == 0x4D08A8) {
                Log("weapon probe: init %08x, the actor %08x, a1 %#x, a2 %#x, from %08x", a3, (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], ra8);
            } else if (a3 == 0x2FCB60) {
                const uint32_t o = ra8 == V3(0x31B3C8) ? (uint32_t)c.r[18] : (uint32_t)c.r[16];
                const auto it = gNativeActors.find(o);
                Log("weapon probe: stance of weapon %#x asked from %08x for %08x (%s, slot %u)", (uint32_t)c.r[4], ra8, o,
                    it != gNativeActors.end() ? it->second.c_str() : "?", o >= 0x100000 && o < 0x01FFF000 ? R8(o + 0xB60) : 0xFF);
            }
        }
    }
    // map.ini laser= (docs/CHARACTERS.md, The SOCOM's laser): a weapon given a laser sight on that map, switched in
    // the laser's act (LaserDrive); its origin (0x3FC3F8) asks that weapon its laser's point (0x45F2D0, message
    // 0x4000E) where it asks the others' muzzle (0x45F180); the M1911A1's messages (0x4CE000) answer 0x4000E as the
    // Scorpion's do (0x4D4510: the .11 point EF_laser times the model's matrix). The made bodies' armband with the
    // head (BandWithHead)
    if (n3 && ExtraMapsLoaded()) {
        static uint32_t originPlayer = 0;                   // the laser's origin (0x3FC3F8) runs for this player
        auto laserOf = [](uint32_t player, int id) {       // an added weapon's laser, or map.ini laser=
            const AddedWeapon* w = AddedOf(PlayerSlotOf(player), id);
            return w ? w->laser : ExtraMapsLaser(id);
        };
        if (a3 == 0x3FC3F8) originPlayer = (uint32_t)c.r[4];
        if (addr == 0x123150 && !ExtraMapsWeapons().empty()) {   // GV_StrCode: an added weapon's model and magazine
            if (const uint32_t name = AddedModelName(c)) { c.r[2] = name; c.r[3] = 0; return true; }
        } else if (a3 == 0x45D770) {
            const int code = (int)((uint32_t)c.r[5] & 0xFF);
            gFactoryOwner = ExtraMapsWeapons().empty() ? -1 : code == gEquipId && gEquipSlot >= 0 ? gEquipSlot : ComponentOwner((uint32_t)c.r[4]);
            gEquipId = -1;
            if (ps2::Fn f = ps2::FindFn(va)) {                  // run here: the weapon's actor named during it is his
                const Ctx keep = c;
                gFactoryCode = code; ++gFactoryDepth;
                if (gFactoryOwner >= 0 && gFactoryOwner == OwnSlot()) ++gLocalWeaponsMade;
                f(c);
                --gFactoryDepth;
                c.r[31] = keep.r[31];
                return true;
            }
        } else if ((addr == 0x115310 || addr == 0x1153B8 || addr == 0x115440) && !gWeaponActors.empty()) {
            // an effect at a place (a0 its code, v0 its handle) played in a weapon's act: an added weapon's own sound (the
            // game plays its base's; the base weapon's own players keep the game's)
            if (const auto wa = gWeaponActors.find(gActActor); wa != gWeaponActors.end())
                if (const AddedWeapon* w = AddedOf(wa->second.owner, wa->second.id)) {
                    const int effect = (int)((uint32_t)c.r[4] & 0xFFF);
                    const AddedWeapon::Sound* snd = nullptr;
                    for (const auto& x : w->sounds) if (x.effect == effect) snd = &x;
                    ps2::Fn f = snd ? ps2::FindFn(va) : ps2::Fn();
                    if (f) {
                        const Ctx keep = c;
                        f(c);
                        c.r[31] = keep.r[31];
                        const uint32_t handle = (uint32_t)c.r[2];      // the effect's handle: its 0x43 command's fourth word
                        const uint64_t cut = snd->cut ? ((uint64_t)(wa->second.owner + 1) << 8 | (uint64_t)wa->second.id) : 0;
                        iop::AddedWeaponSound(handle, snd->pcm, snd->gain, cut, w->name + " " + snd->file);
                        if (Once(((uint64_t)106 << 40) | ((uint64_t)wa->second.owner << 32) | (uint64_t)effect << 8 | (uint64_t)wa->second.id))
                            Log("added weapons: player %d's %s: its effect %03x heard as %s (handle %#x)", wa->second.owner + 1, w->name.c_str(),
                                effect, snd->file.c_str(), handle);
                        return true;
                    }
                }
        } else if (a3 == 0x2FCB60 && !ExtraMapsWeapons().empty()) {   // an added weapon's stance
            if (const uint32_t st = AddedStance(c)) { c.r[2] = st; c.r[3] = 0; return true; }
        } else if (a3 == 0x45DCA0 && !ExtraMapsWeapons().empty()) {
            // a sight made (0x45DCA0, a1 its kind, the table 0x55A7EC: 1 NewSightDragnov the SVD's, 2 NewSightScope,
            // 3 NewSightRpg7, 4 NewSightMosinNagant): the added weapon's own (scope=mosin: 4) when this player holds one
            // with scope= (the PSG1)
            const int self = OwnSlot(), kind = (int)(uint32_t)c.r[5];
            const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0, eq = me ? R32(me + 0x608) : 0;
            const int held = eq >= 0x100000 && eq < 0x01FFFFC0 ? R8(eq + 0x14) : 0;
            if (const AddedWeapon* w = AddedOf(self, held); w && w->sight && kind != w->sight && kind >= 1 && kind <= 6) {
                c.r[5] = (uint64_t)w->sight;
                if (Once(((uint64_t)101 << 40) | ((uint64_t)kind << 8) | (uint32_t)w->sight))
                    Log("added weapons: the %s's scope is the sight %d (the base's %d)", w->name.c_str(), w->sight, kind);
            }
        } else if (a3 == 0x3A10E0 && !ExtraMapsWeapons().empty()) {   // a drop of this player's (a death's, by hand)
            if (ps2::Fn f = ps2::FindFn(va)) {
                const Ctx keep = c;
                f(c);
                c.r[31] = keep.r[31];
                DropMade(keep, (int)(int32_t)(uint32_t)c.r[2]);
                return true;
            }
        } else if (a3 == 0x4C6660 && !ExtraMapsWeapons().empty()) {   // a box made from a record: its record's place
            if (ps2::Fn f = ps2::FindFn(va)) {
                const Ctx keep = c;
                const unsigned made = gItemBoxesMade;
                f(c);
                c.r[31] = keep.r[31];
                if (c.r[2] && gItemBoxesMade != made) BoxMade(keep, gLastItemBox);   // v0 not the box: its work named last
                return true;
            }
        } else if (a3 == 0x4C54D0 && !ExtraMapsWeapons().empty()) {   // an item's label: a dropped weapon's name
            DropLabel((uint32_t)c.r[4]);
        } else if (a3 == 0x2FD5B8 && !ExtraMapsWeapons().empty() && (uint32_t)c.r[31] == V3(0x4CA2D8)) {
            // the pickup (0x4C9FC8, s0 the box) gives the box's weapon (a0 its code): v0 1 gained, -1 held already (#119)
            if (ps2::Fn f = ps2::FindFn(va)) {
                const Ctx keep = c;
                f(c);
                c.r[31] = keep.r[31];
                BoxTaken(keep, (uint32_t)keep.r[16], (int)(uint32_t)keep.r[4], (int32_t)(uint32_t)c.r[2] == 1);
                return true;
            }
        } else if (a3 == 0x371BC0 && !ExtraMapsWeapons().empty()) {   // the HUD's icons loaded: this player's added weapons' too
            if (ps2::Fn f = ps2::FindFn(va)) {
                const Ctx keep = c;
                const AddedClock t;
                f(c);
                const double game = t.Ms();
                c.r[31] = keep.r[31];
                AddedIcons(keep);
                Log("added weapons: time: the HUD's icons loaded in %.2f ms (the game's %.2f, the added weapons' %.2f)", t.Ms(), game, t.Ms() - game);
                return true;
            }
        } else if (a3 == 0x3CBDC8 && !ExtraMapsWeapons().empty()) {   // the choice's rows, with the added weapons
            if (ps2::Fn f = ps2::FindFn(va)) {
                gMenuWork = 0;
                const Ctx keep = c;
                const AddedClock t;
                f(c);
                const double game = t.Ms();
                c.r[31] = keep.r[31];
                MenuBuild(keep, (uint32_t)keep.r[4]);
                Log("added weapons: time: the choice's rows in %.2f ms (the game's %.2f, made again %.2f)", t.Ms(), game, t.Ms() - game);
                return true;
            }
        } else if (a3 == 0x3CBB08 && gMenuWork && (uint32_t)c.r[4] == gMenuWork && !ExtraMapsWeapons().empty()) {
            MenuRefresh(c);                                  // the window moved at a row's ends, then the game's refresh
            if (ps2::Fn f = ps2::FindFn(va)) {
                const Ctx keep = c;
                f(c);
                c.r[31] = keep.r[31];
                MenuTitle(keep, (uint32_t)keep.r[4], (int)(uint32_t)keep.r[5]);
                return true;
            }
        } else if (a3 == 0x3CBA78 && gMenuWork && (uint32_t)c.r[4] == gMenuWork && (uint32_t)c.r[5] <= 1) {
            const int row = (int)(uint32_t)c.r[5];            // the list's index of the entry at a2 (its base's)
            const MenuRow& r = gMenuRows[row];
            const size_t at = (size_t)(r.window + (int)(uint32_t)c.r[6]);
            if (at < r.e.size()) { c.r[2] = (uint64_t)(r.e[at].bit - (row ? 6 : 0)); c.r[3] = 0; return true; }
        } else if ((a3 == 0x3CC360 || a3 == 0x3CC3A8) && gMenuWork && (uint32_t)c.r[4] == gMenuWork) {
            const int row = a3 == 0x3CC360 ? 0 : 1;          // OK: the entry's base's bit, this player's added choice
            const MenuRow& r = gMenuRows[row];
            const size_t at = (size_t)(r.window + (int)((uint32_t)c.r[5] & 0xFFFF));
            if (at < r.e.size()) {
                const MenuEntry& e = r.e[at];
                const int self = OwnSlot();
                if (e.id > 0 && e.id < 0x40) gMenuPick[e.id] = (uint8_t)(e.added + 1);
                if (self >= 0 && self < 32 && e.id > 0 && e.id < 0x40 && gAddedChoice[self][e.id] != e.added + 1) {
                    gAddedChoice[self][e.id] = (uint8_t)(e.added + 1);
                    const auto& list = ExtraMapsWeapons();
                    Log("added weapons: player %d chose %s (weapon %#x)", self + 1,
                        e.added >= 0 && e.added < (int)list.size() ? list[(size_t)e.added].name.c_str() : Str(StockWeaponName(e.id)), e.id);
                }
                AddedTestChoice(self);                       // SIM_ADDED_WEAPONS (tests): theirs at once, before the spawn's inventory
                const AddedClock t;
                AddedTaken(c, e.id, true);
                AddedIcons(c);                               // the HUD's icons were loaded as the menu opened: the new choice's now
                const auto& all = ExtraMapsWeapons();
                Log("added weapons: time: the choice of the %s row's weapon (%s) in %.2f ms (each pass's own work since the menu opened: at most %.2f ms)",
                    row ? "secondary" : "primary", e.added >= 0 && e.added < (int)all.size() ? all[(size_t)e.added].name.c_str() : Str(StockWeaponName(e.id)),
                    t.Ms(), gAddedPassMs);
                SimTickLogFrom("the choice's OK");
                c.r[2] = (uint64_t)1 << e.bit; c.r[3] = 0;
                return true;
            }
        }
        if (a3 == 0x45F180 && (uint32_t)c.r[31] - 8 == V3(0x3FC468) && laserOf(originPlayer, (int)(uint32_t)c.r[7])) {
            // the laser's origin (0x3FC3F8) asks the muzzle (0x40008) of a weapon other than 7 and 10: its laser's
            // point (0x45F2D0, 0x4000E) instead, the weapon found by its own number (a3)
            if (ps2::Fn g = ps2::FindFn(V3(0x45F2D0))) {
                const uint64_t ra = c.r[31];
                const uint32_t id = (uint32_t)c.r[7];
                g(c);
                c.r[31] = ra;
                if (Once(((uint64_t)92 << 40) | id)) Log("extra maps: the laser of weapon %u from its point EF_laser (%s)", id, c.r[2] ? "answered" : "no answer");
                return true;
            }
        } else if ((a3 == 0x417810 || a3 == 0x4202E8) && !ExtraMapsWeapons().empty()) {
            // the laser's control (a0 the player, a1 his weapon's record, its number at +0x44; 10 the Scorpion's laser
            // shown while he aims): an added weapon with laser=0 on the Scorpion (the P90) run as a rifle's (its
            // laser never shown, the aim's ray kept)
            const uint32_t player = (uint32_t)c.r[4], rec = (uint32_t)c.r[5];
            const AddedWeapon* w = rec >= 0x100000 && rec < 0x01FFFF00 && R32(rec + 0x44) == 10 ? AddedOf(PlayerSlotOf(player), 10) : nullptr;
            if (w && w->laserOff)
                if (ps2::Fn f = ps2::FindFn(va)) {
                    const Ctx keep = c;
                    W32(rec + 0x44, 11);
                    f(c);
                    W32(rec + 0x44, 10);
                    c.r[31] = keep.r[31];
                    if (Once(((uint64_t)109 << 40) | (uint32_t)(PlayerSlotOf(player) + 1)))
                        Log("added weapons: player %d's %s: no laser (laser=0), its control run as a rifle's", PlayerSlotOf(player) + 1, w->name.c_str());
                    return true;
                }
        } else if (a3 == 0x454D48) {
            LaserDrive(c);                                  // then the laser's act as it is
        } else if (a3 == 0x4569F8) {
            BandWithHead(c);                                // then the band's act as it is
        } else if ((a3 == 0x4CE000 || a3 == 0x4E5BB8) && ((uint32_t)c.r[5] & 0x0FFC03FFu) == 0x4000Eu) {
            // the M1911A1's messages (gave, 0x4CE000) and the Mk22's (0x4E5BB8): the laser's point as the Scorpion's
            // (0x4D4510: the .11 point EF_laser times the model's matrix; the same fields in every weapon), for a laser
            // of map.ini laser= or of an added weapon (the origin's hook sends 0x4000E only for those)
            const int base = a3 == 0x4CE000 ? 6 : 5;
            bool laser = ExtraMapsLaser(base);
            for (const auto& w : ExtraMapsWeapons()) laser |= w.base == base && w.laser;
            ps2::Fn g{};
            if (laser) g = ps2::FindFn(V3(0x4D4510));
            if (g) {
                const uint64_t ra = c.r[31];
                c.r[5] = c.r[6];                                // a1: the matrix to fill (the message's argument)
                g(c);
                c.r[31] = ra;
                c.r[2] = 4;
                if (Once(((uint64_t)91 << 40) | (uint32_t)base)) Log("extra maps: the %s gives its laser's point (EF_laser)", base == 6 ? "M1911A1" : "Mk22");
                return true;
            }
        }
    }
    if (gNative && addr == 0x122768) HeliportTick();          // map 15: a mission object lost under the deck (heliport.h)
    if (gNative && a3 == 0x39E0B0 && HeliportObjects(c, va)) return true;   // map 15: the mission objects (heliport.h)
    if (gNative && addr == 0x178490 && ExtraConfigRead(c, va)) return true;   // the rotation's maps 13 and up, TEAM SNEAKING
    if (gNative && TsneBridge(c, addr, va)) return true;      // TEAM SNEAKING's round (tsne.h)
    if (gNative && BcBridge(c, addr, va)) return true;        // BASE CAPTURE's round (basecap.h)
    if (gNative && ExtraMapsAny()) {                        // maps 13 and up (docs/EXTRA_MAPS.md)
        if (addr == 0x101B28) ExtraStageLoad(c);
        else if (addr == 0x106CC0) ExtraRulesCheck();
        else if (addr == 0x11B288 && ExtraSameStage(c)) return true;
        else if (a3 == 0x3C7110 && (uint32_t)c.r[5] == 0xF4D880) ExtraMapNameText(c);
        else if (a3 == 0x3C7110) ExtraTextSwap(c);
    }
    if (gNative && addr == 0x101B28) StageAsked(c);         // (v150-host beta 22, sync) the stage asked, with its extra map
    // the actor loop (0x1228C0) calling an actor's act (a0: the actor, its act mostly in the map's
    // page): the fair rules' motion marks (variant 4) for the actors run once a frame at 30 (flag
    // 0x40: the players). Here, before a page's call leaves for its recompiled code
    if (gNative && (ps2::gVariant[3] | ps2::gVariant[4])) {
        const uint32_t ra = (uint32_t)c.r[31];
        if (ra == V(0x1229e4) || ra == V(0x122a3c)) {
            const uint32_t actor = (uint32_t)c.r[4];
            ps2::gVariant[4] = ps2::gVariant[3] && (R32(actor + 0xc) & 0x40);
            if (ps2::gVariant[4] && Once(((uint64_t)14 << 40) | actor)) {
                auto it = gNativeActors.find(actor);
                Log("fair 60: motion marks of actor %08x (%s, act %08x) as at 30", actor,
                    it != gNativeActors.end() ? it->second.c_str() : "unnamed", addr);
            }
        }
    }
    if (gNative && InPageArea(va)) NativeOverlayCheck();
    if (gNative && addr == 0x125500) {                        // a script set: another version's in its place, its server slots
        if (ExtraMapsAny()) ExtraTextsTick();
        ScriptFromVersion(c);
        TextsFromVersion(c);
        NativeScriptServers((uint32_t)c.r[4]);
        NativeScriptLanguage((uint32_t)c.r[4]);
        if (ExtraMapsAny()) ExtraScriptTexts((uint32_t)c.r[4]);
        if (ExtraMapsAny()) AddedDescScript((uint32_t)c.r[4]);
        PromptsScriptSet((uint32_t)c.r[4]);                    // its button words (prompts.h)
    }
    if (addr == 0x178928) {                                   // a datagram's records: sent (a3 0) or received (1)
        const bool out = c.r[7] == 0;
        const uint32_t s = (uint32_t)c.r[4], b = (uint32_t)c.r[5];
        uint32_t n = (uint32_t)c.r[6];
        const bool pc = !Ps2Like() && !SimPlainPs2();        // netcode: tests, a stock PS2's records as they are (netfix.h; SIM_PS2_FAMILY1)
        if (out && pc) {                                      // the trace shows them as they travel
            SimLadderInject(c, true, s, b, n);
            ChannelFamilies(true, s, b, n); ProtocolRecords(true, s, b, n); n = LadderRecordsGrow(c, true, s, b, n);
        }
        P2pLog(out, s, b, n);
#ifndef MGO1_PLAYER_BUILD
        if (out && vdoll::On() && s < 0x02000000 && (b & 0x01ffffff) + n <= 0x02000000) {   // the host's datagram to a virtual doll
            const long long t0 = RealQpc();
            vdoll::HostClear(R32(s + 0x14), R16(s + 0x12), gMem + (b & 0x01ffffff), n);
            VdollCostAdd(RealQpc() - t0, false);
        }
#endif
        if (!out && pc) {
            SimLadderInject(c, false, s, b, n);
            ChannelFamilies(false, s, b, n); ProtocolRecords(false, s, b, n);
            SpawnCharacterOnReceipt(c, b, n);                 // (v150-host beta 16) the character a spawn asks for
            n = LadderRecordsGrow(c, false, s, b, n);
            LadderHold(b, n);
            SimLadderSeen(false, b, n);
        }
    }
    // the dedicated host (docs/DEDICATED_HOST.md): its settings, kicks, reports, the Game Report
    static const bool hostOnly = DevEnv("MGO1_HOST_ONLY") || DevEnv("MGO1_HOST_SETTINGS");
    // the network start's send budget (0x16EBA0 from 0x172B80: a0 3, a1 the online data's upstream
    // bandwidth, + 0x8A in kbps: 96, 128, 192, 256 or 512 "or more", 256 on a new card; stored x 1000
    // at 0x23BF84, 56000 to 512000): the dedicated host and the dolls at the game's maximum, checked.
    // With the PC's choices (variant 6, UpstreamOn: up to 4096 kbps) UpstreamBots does it above
    static const bool bot = hostOnly || DevEnv("MGO1_DOLL");
    if (gNative && bot && addr == 0x16eba0) {
        if (const Fn fn = FindFn(va)) {
            const int asked = (int)(uint32_t)c.r[5];
            if (asked != 512) { c.r[5] = 512; W16(V(0x28D312), 512); }
            fn(c);
            const uint32_t budget = R32(V(0x23BF84));
            Log("network: upstream bandwidth %d kbps%s, the send budget %u bits a second (%s)", 512,
                asked != 512 ? (asked == 256 ? " (256 on the card, the game's default)" : " (another value on the card)") : "",
                budget, budget == 512000 ? "the game's maximum" : "NOT the maximum");
            return true;
        }
    }
#ifndef MGO1_PLAYER_BUILD
    if (!gKicks.empty() && gNative && !gInIrq && addr == 0x174ba8) KickSend(c);   // a room message about to start
    if (gNative && (addr == 0x195d90 || addr == 0x1985d0 || addr == 0x196220)) HostReports(c, addr);
#endif
    if (gNative && addr == 0x1220e0 && gMessageHook) gMessageHook((uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6]);
    // the doll's radio reading a call's word (4 bytes) from its channel: the word kept for the doll
    if (gNative && addr == 0x174b20 && gRadioChannel.load() && (uint32_t)c.r[4] == gRadioChannel.load() && (uint32_t)c.r[6] == 4) {
        if (const Fn fn = FindFn(va)) {
            const uint32_t dst = (uint32_t)c.r[5];
            fn(c);
            std::lock_guard<std::mutex> g(gRadioLock);
            if (gRadioCalls.size() < 64 && dst < 0x02000000 - 4) gRadioCalls.push_back(R32(dst));
            return true;
        }
    }
#ifndef MGO1_PLAYER_BUILD
    if (gNative && addr == 0x1220e0 && report::On()) {       // the game's messages to its actors: the HUD's ones for the Game Report
        const uint32_t code = (uint32_t)c.r[5];
        if ((code >> 16) == 0x75 || (code >> 16) == 0x76) report::HudMessage(code, (uint32_t)c.r[6]);
    }
    {   // MGO1_EVENTS_PROBE: the game's writes of its network variables (0x178300: data, offset, size, source)
        static const bool probe = DevEnv("MGO1_EVENTS_PROBE") != nullptr;
        if (probe && addr == 0x178300) {
            const uint32_t data = (uint32_t)c.r[4], off = (uint32_t)c.r[5], size = (uint32_t)c.r[6], src = (uint32_t)c.r[7] & 0x01ffffff;
            const int s = gProbeSet(data);
            if (s >= 0 && size <= 64 && src + size <= 0x02000000 && !gProbeNoisy.count((uint32_t)s << 16 | off) &&
                memcmp(gMem + (data & 0x01ffffff) + off, gMem + src, size)) {
                std::string b; char h[4];
                for (uint32_t i = 0; i < size; ++i) { snprintf(h, sizeof h, "%02x", gMem[src + i]); b += h; }
                Log("probe: set S%d +%03x = %s from %08x (run of %08x)", s, off, b.c_str(), (uint32_t)c.r[31] - 8, gRunFn);
            }
        }
        // the actors' messages (0x121F18: actor, code, argument): the match's HUD and supervisor ones (0x75xxxx, 0x76xxxx)
        if (probe && addr == 0x121f18) {
            const uint32_t code = (uint32_t)c.r[5], arg = (uint32_t)c.r[6];
            if ((code >> 16) == 0x75 || (code >> 16) == 0x76) {
                std::string b; char h[4];
                const uint32_t p = arg & 0x01ffffff;
                if (arg >= 0x100000 && arg < 0x02000000 && p + 16 <= 0x02000000)
                    for (uint32_t i = 0; i < 16; ++i) { snprintf(h, sizeof h, "%02x", gMem[p + i]); b += h; }
                Log("probe: message %06x arg %08x [%s] to %08x from %08x", code, arg, b.c_str(), (uint32_t)c.r[4], (uint32_t)c.r[31] - 8);
            }
        }
    }
#endif
#ifndef MGO1_PLAYER_BUILD
    static const bool namesTrace = getenv("SIM_NAMES_TRACE") != nullptr;   // tests: ntmenu2's map names (US addresses)
    if (namesTrace && gPageNt && (addr == 0x349aa0 || addr == 0x349ac0 || addr == 0x349af0 || addr == 0x349b40 || addr == 0x373da0 || addr == 0x373d10)) {
        static std::map<std::pair<uint32_t, uint32_t>, int> seen;
        const uint32_t ra = (uint32_t)c.r[31] - 8, a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5], a2 = (uint32_t)c.r[6];
        const bool table = (addr == 0x373da0 || addr == 0x373d10) && a2 >= 0x2e6630 && a2 < 0x2e66f0;
        if ((addr == 0x349aa0 && a0 == 7) || table || addr == 0x349ac0 || addr == 0x349af0 || addr == 0x349b40)
            if (seen[{addr, ra}]++ < 3)
                Log("names: %08x(%x, %x, %x) from %08x%s", addr, a0, a1, a2, ra, table ? " (the table of the names)" : "");
    }
    static const bool watchCalls = getenv("SIM_WATCH_CALLS") != nullptr;
    if (watchCalls) WatchCalls(c, addr);
    {   // SIM_TEXT_FNS="fn:arg,...": the text these functions get (the in-game text draws), logged when it was
        // not drawn in the 120 frames before: a timeline of what the screen shows
        // an entry may name its page ("n011a:3c2890:2"): each map overlay has its own addresses
        struct TextFn { std::string page; int arg; };
        static std::multimap<uint32_t, TextFn> fns;
        static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = DevEnv("SIM_TEXT_FNS")) {
                std::string all = e; size_t p0 = 0;
                while (p0 < all.size()) {
                    size_t p1 = all.find(',', p0); if (p1 == std::string::npos) p1 = all.size();
                    std::string one = all.substr(p0, p1 - p0), page;
                    const size_t k = one.find(':');
                    if (k != std::string::npos) {                   // a first field that is not hex is a page name
                        bool hex = k > 0;
                        for (size_t i = 0; i < k; ++i) if (!isxdigit((unsigned char)one[i])) hex = false;
                        if (!hex) { page = one.substr(0, k); one = one.substr(k + 1); }
                    }
                    char* end; uint32_t v = (uint32_t)strtoul(one.c_str(), &end, 16);
                    int arg = -1; if (*end == ':') arg = (int)strtol(end + 1, &end, 10) & 3;   // no ":n": any of a0..a3
                    if (v) fns.insert({v, {page, arg}});
                    p0 = p1 + 1;
                }
            }
        }
        if (!fns.empty()) {
            auto it = fns.end();
            for (auto r = fns.equal_range(addr); r.first != r.second; ++r.first)
                if (r.first->second.page.empty() || !_stricmp(r.first->second.page.c_str(), gOverlayName.c_str())) { it = r.first; break; }
            for (int r = 0; it != fns.end() && r < 4; ++r) {
                if (it->second.arg >= 0 && r != it->second.arg) continue;
                const uint32_t p = (uint32_t)c.r[4 + r] & 0x01ffffff;
                if ((uint32_t)c.r[4 + r] < 0x02000000 && p >= 0x100000 && p + 128 <= 0x02000000) {
                    const char* s = (const char*)gMem + p;
                    size_t n = strnlen(s, 120);
                    bool text = n >= 2 && n < 120;
                    for (size_t k = 0; text && k < n; ++k) if ((uint8_t)s[k] < 0x20 || (uint8_t)s[k] >= 0x7f) text = false;
                    if (!text) continue;
                    static std::map<std::string, long long> last;
                    std::string key = std::to_string(addr) + ":" + std::string(s, n);
                    auto l = last.find(key);
                    if (l == last.end() || gFrameNo - l->second > 120) Log("text %08x: \"%.*s\"", addr, (int)n, s);
                    last[key] = gFrameNo;
                }
            }
        }
    }
    {   // SIM_STRARGS=<from frame>: a text argument (a0..a3 on printable ASCII), logged once per function and text
        static const long long from = DevEnv("SIM_STRARGS") ? atoll(DevEnv("SIM_STRARGS")) : -1;
        if (from >= 0 && gFrameNo >= from) {
            static std::set<uint64_t> seen;
            for (int r = 4; r <= 7; ++r) {
                const uint32_t p = (uint32_t)c.r[r] & 0x01ffffff;
                if ((uint32_t)c.r[r] >= 0x02000000 || p < 0x100000 || p + 64 > 0x02000000) continue;
                const uint8_t* s = gMem + p;
                size_t n = 0;
                while (n < 64 && s[n] >= 0x20 && s[n] < 0x7f) ++n;
                if (n < 3 || n >= 64 || s[n] != 0) continue;
                uint64_t h = 1469598103934665603ull;
                for (size_t i = 0; i < n; ++i) h = (h ^ s[i]) * 1099511628211ull;
                if (seen.insert(h ^ ((uint64_t)addr << 1)).second)
                    Log("strarg: %08x from %08x a%d \"%.*s\"", addr, (uint32_t)c.r[31] - 8, r - 4, (int)n, (const char*)s);
            }
        }
    }
    {   // SIM_RETVALS="addr,addr...": these functions run here and their results are logged (400 lines)
        static std::set<uint32_t> fns;
        static bool init = false;
        static int left = 400;
        if (!init) {
            init = true;
            if (const char* e = DevEnv("SIM_RETVALS"))
                for (const char* q = e; *q;) { char* end; uint32_t v = (uint32_t)strtoul(q, &end, 16); if (end == q) break; fns.insert(v); q = *end ? end + 1 : end; }
        }
        static const long long fromFrame = DevEnv("SIM_RETVALS_FROM") ? atoll(DevEnv("SIM_RETVALS_FROM")) : 0;
        if (left > 0 && !fns.empty() && gFrameNo >= fromFrame && fns.count(addr)) {
            if (ps2::Fn f = ps2::FindFn(va)) {
                const uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5], a2 = (uint32_t)c.r[6], a3 = (uint32_t)c.r[7], from = (uint32_t)c.r[31] - 8;
                const float f12 = c.f[12];
                f(c);
                --left;
                auto word = [](uint32_t p) -> uint32_t { p &= 0x01ffffff; return (p >= 0x100000 && p + 4 <= 0x02000000) ? R32(p) : 0; };
                auto fl = [&](uint32_t p) { uint32_t u = word(p); float f; memcpy(&f, &u, 4); return (double)f; };
                Log("retval: %08x(a0 %08x a1 %08x a2 %08x a3 %08x f12 %g) from %08x -> v0 %08x f0 %g; after: [a1] %08x [a2] %08x [a0+0x30] %g %g %g", addr, a0, a1, a2, a3,
                    (double)f12, from, (uint32_t)c.r[2], (double)c.f[0], word(a1), word(a2), fl(a0 + 0x30), fl(a0 + 0x34), fl(a0 + 0x38));
                return true;
            }
        }
    }
    {   // SIM_CALL_COUNTS="addr,addr...": calls of these functions, reported with SIM_DMA_COUNT
        static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = DevEnv("SIM_CALL_COUNTS"))
                for (const char* q = e; *q;) { char* end; uint32_t v = (uint32_t)strtoul(q, &end, 16); if (end == q) break; gCallCounts[v] = 0; q = *end ? end + 1 : end; }
        }
        if (!gCallCounts.empty()) { auto it = gCallCounts.find(addr); if (it != gCallCounts.end()) ++it->second; }
    }
    {   // SIM_FN_DIFF="a0-a1,b0-b1": every function called in frames a0..a1 and in b0..b1, counted (the report
        // after b1: what one state of the game runs that the other does not)
        static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = getenv("SIM_FN_DIFF"))
                if (sscanf(e, "%lld-%lld,%lld-%lld", &gFnDiffW[0], &gFnDiffW[1], &gFnDiffW[2], &gFnDiffW[3]) != 4) gFnDiffW[0] = -1;
        }
        if (gFnDiffW[0] >= 0) {
            if (gFrameNo >= gFnDiffW[0] && gFrameNo <= gFnDiffW[1]) ++gFnDiff[0][addr];
            else if (gFrameNo >= gFnDiffW[2] && gFrameNo <= gFnDiffW[3]) ++gFnDiff[1][addr];
        }
    }
    {   // SIM_CALLERS="addr,addr...": each distinct caller of these functions, logged once
        static std::set<uint32_t> fns;
        static bool init = false;
        if (!init) {
            init = true;
            if (const char* e = DevEnv("SIM_CALLERS"))
                for (const char* q = e; *q;) { char* end; uint32_t v = (uint32_t)strtoul(q, &end, 16); if (end == q) break; fns.insert(v); q = *end ? end + 1 : end; }
        }
        if (!fns.empty() && fns.count(addr)) {
            static std::set<uint64_t> seen;
            const uint32_t from = (uint32_t)c.r[31] - 8;
            if (seen.insert((uint64_t)addr << 32 | from).second)
                Log("caller: %08x from %08x (a0 %08x a1 %08x; run of %08x)", addr, from, (uint32_t)c.r[4], (uint32_t)c.r[5], gRunFn);
        }
    }
    {   // SIM_POOL_CHECK: pool 0's first free block checked at every call: its size never shrinks
        // in place (an allocation moves the header, a free only merges), so a smaller size under
        // the same header is a write past the end of the block before it
        static const bool check = DevEnv("SIM_POOL_CHECK") != nullptr;
        static uint32_t head = 0, size = 0, told = 0;
        if (check) {
            const uint32_t h = R32(V(0x2C5DE8)), sz = (h >= 0xF00000 && h < 0x1B00000) ? R32(h + 0xC) : 0;
            if (h == head && sz < size && told < 5) {
                ++told;
                Log("pool 0: the free block at %08x shrank in place from %#x to %#x (next %08x) before the call of %08x (from %08x); calls before:%s",
                    h, size, sz, R32(h), addr, (uint32_t)c.r[31] - 8, RecentCalls(16).c_str());
            }
            head = h; size = sz;
            // a block going back to a pool's free list (0x121360 / 0x121448: pool, header) that
            // is on it already: a double free, whose free list then loops on itself
            if ((addr == 0x121360 || addr == 0x121448) && told < 10) {
                const uint32_t pool = (uint32_t)c.r[4], blk = (uint32_t)c.r[5];
                const uint32_t list = pool + (addr == 0x121360 ? 8 : 0xC);
                uint32_t n = 0, slow = R32(list);
                for (uint32_t b = R32(list); b && n < 200000; b = R32(b), ++n) {
                    if (b == blk) {
                        ++told;
                        Log("pool %08x: block %08x freed again (already the free list's block %u) by the call of %08x (from %08x); calls before:%s",
                            pool, blk, n, addr, (uint32_t)c.r[31] - 8, RecentCalls(16).c_str());
                        break;
                    }
                    if (n && !(n & 1)) slow = R32(slow);             // half the pace: they meet only in a loop
                    if (n && b == slow) {
                        ++told;
                        Log("pool %08x: its free list loops (at %08x, after %u blocks) before the call of %08x (from %08x); calls before:%s",
                            pool, b, n, addr, (uint32_t)c.r[31] - 8, RecentCalls(16).c_str());
                        break;
                    }
                }
            }
        }
    }
    {   // SIM_POOL_WALK_FROM=<frame>: from that frame, pool 0 walked before every call until it breaks
        static const long long from = DevEnv("SIM_POOL_WALK_FROM") ? atoll(DevEnv("SIM_POOL_WALK_FROM")) : -1;
        static bool broken = false;
        if (from >= 0 && !broken && gFrameNo >= from) {
            const std::string w = PoolWalk();
            if (!w.empty()) {
                broken = true;
                Log("pool 0 walk: %s, first seen before the call of %08x (from %08x; run of %08x); calls before:%s", w.c_str(), addr,
                    (uint32_t)c.r[31] - 8, gRunFn, RecentCalls(32).c_str());
            }
        }
    }
    {   // SIM_POOL_TRACE=<frame>: from that frame, pool 0's allocations and frees (300 of them)
        static const long long from = DevEnv("SIM_POOL_TRACE") ? atoll(DevEnv("SIM_POOL_TRACE")) : -1;
        static int n = 0;
        if (from >= 0 && gFrameNo >= from && n < 3000 && (addr == 0x1211f0 || addr == 0x121528)) {
            ++n;
            const uint32_t h = R32(V(0x2C5DE8));
            const uint32_t p = (uint32_t)c.r[4];
            Log("pool 0 %s %08x (a1 %#x) from %08x: head %08x size %#x next %08x, free bytes %#x%s", addr == 0x1211f0 ? "alloc" : "free",
                p, (uint32_t)c.r[5], (uint32_t)c.r[31] - 8, h, h ? R32(h + 0xC) : 0, h ? R32(h) : 0, R32(V(0x2C5DF4)),
                addr == 0x121528 && p >= 0xF00010 && p < 0x1B00000
                    ? (" block flags " + std::to_string(R8(p - 8)) + " refs " + std::to_string((int8_t)R8(p - 6)) + " size " + std::to_string(R32(p - 4))).c_str() : "");
        }
    }
#endif
    // DG prims (0x14B838 -> 0x14F740(size)): their sizes come from DG's type tables, which the
    // PS2 world only half fills (the GS side of DG init and of the stage's commands is the PC's),
    // and the prim code writes past a short block into pool 0's next header. Nothing draws them
    // here: each gets 0x100 bytes more, so the writes land in its own block
    if (addr == 0x14f740 && !gNative) c.r[4] = (uint64_t)(int64_t)(int32_t)((uint32_t)c.r[4] + 0x100);
    if (addr == 0x1211f0 && (uint32_t)c.r[5] >= 0x40000) {   // a big pool allocation (GV 0x1211F0(pool, size, align))
        Log("pool %d: %#x bytes asked (from %08x); calls before:%s", (int)(int32_t)c.r[4], (uint32_t)c.r[5],
            (uint32_t)c.r[31] - 8, RecentCalls(10).c_str());
        static const bool result = getenv("SIM_POOL_ROOM") != nullptr;   // tests: what it gave, pool 0's room after
        if (result)
            if (ps2::Fn f = ps2::FindFn(va)) {
                const uint64_t ra = c.r[31];
                const uint32_t size = (uint32_t)c.r[5];
                f(c);
                c.r[31] = ra;
                Log("pool: %#x bytes given at %08x%s, pool 0 %s", size, (uint32_t)c.r[2], (uint32_t)c.r[2] ? "" : " (NONE: the pool is full)", PoolState().c_str());
                return true;
            }
    }
    if (!gOverlayLive && InPageArea(va)) {     // the page's code is gone (stage change)
        if (Once(((uint64_t)9 << 40) | addr))
            Log("call %08x into the unloaded %s overlay, from %08x: not run", addr, gOverlayName.c_str(), (uint32_t)c.r[31] - 8);
        c.r[2] = 0; c.r[3] = 0;
        return true;
    }
    // another page's overlay (a map): the ntmenu2 bridges below are addresses in ntmenu2's
    // image, so that page's calls go to its own recompiled code
    static uint32_t traceLo = 0, traceHi = 0;              // the traced addresses' range: most calls leave at once
    static const bool traceMore = [] {                      // SIM_TRACE_FNS=a,b,... (tests): more calls logged with their arguments (the pages' too)
        if (DevEnv("MGO1_QUIET_P2P") && !getenv("SIM_TRACE_FNS")) gTrace.clear();   // (production: none of the default ones)
        if (const char* e = getenv("SIM_TRACE_FNS"))
            for (const char* q = e; *q;) { gTrace.insert((uint32_t)strtoul(q, nullptr, 16)); q = strchr(q, ','); if (!q) break; ++q; }
        if (!gTrace.empty()) { traceLo = *gTrace.begin(); traceHi = *gTrace.rbegin(); }
        return true;
    }();
    (void)traceMore;
    if (addr >= traceLo && addr <= traceHi && gTrace.count(addr)) {
        uint64_t key = ((uint64_t)addr << 32) ^ (uint32_t)(c.r[4] * 31 + c.r[5] * 7 + c.r[6]);
        if (gTraceSeen[key]++ < 3)
            Log("call %08x (a0 %08x a1 %08x a2 %08x a3 %08x) t0 %08x t1 %08x t2 %08x t3 %08x ra %08x%s", addr, (uint32_t)c.r[4], (uint32_t)c.r[5],
                (uint32_t)c.r[6], (uint32_t)c.r[7], (uint32_t)c.r[8], (uint32_t)c.r[9], (uint32_t)c.r[10], (uint32_t)c.r[11], (uint32_t)c.r[31],
                (c.r[5] >= 0x100000 && c.r[5] < 0x01000000 && isprint(R8((uint32_t)c.r[5]))) ? (std::string(" a1 \"") + (const char*)(gMem + (uint32_t)c.r[5]) + "\"").c_str() : "");
    }
    const bool otherPage = !gPageNt && InPageArea(va);
    if (!otherPage && Mc2(c, addr)) {
        static const bool mcLog = DevEnv("SIM_BRIDGE_LOG") != nullptr;
        static std::set<uint32_t> seen;
        if (mcLog && seen.insert(addr).second) Log("served: %08x by libmc2 (from %08x)", addr, (uint32_t)c.r[31] - 8);
        return true;
    }
#ifndef MGO1_PLAYER_BUILD
    // TRACE_ADDRS="0x5f6154,0x5f1894": calls of those addresses logged with their caller
    // and arguments (50 per address); works in the simulation and in the game
    {
        // "0x600fc8@32" also dumps 32 bytes at a0
        // "0x1bd590!" also logs the last calls before each hit (who got there)
        static std::map<uint32_t, int> watch;
        static std::map<uint32_t, int> dump;
        static std::set<uint32_t> ring;
        static bool parsed = false;
        if (!parsed) {
            parsed = true;
            const char* s = DevEnv("TRACE_ADDRS");
            while (s && *s) {
                char* e = nullptr; uint32_t v = strtoul(s, &e, 16); if (e == s) break;
                watch[v] = 0;
                if (*e == '@') { dump[v] = (int)strtoul(e + 1, &e, 10); }
                if (*e == '!') { ring.insert(v); ++e; }
                s = *e ? e + 1 : e;
            }
        }
        static long long traceFrom = DevEnv("TRACE_FROM") ? atoll(DevEnv("TRACE_FROM")) : 0;
        static int traceMax = DevEnv("TRACE_MAX") ? atoi(DevEnv("TRACE_MAX")) : 50;
        static bool texts = DevEnv("SIM_TEXTS") != nullptr;   // every screen text, whatever TRACE_FROM says
        if (texts && gPageNt && addr == 0x443658) {
            uint32_t pa = (uint32_t)c.r[6];
            if (pa >= 0x100000 && pa < 0x01FF0000 && isprint(R8(pa))) {
                char t[81]; int n = 0;
                for (; n < 80 && R8(pa + n); ++n) { uint8_t ch = R8(pa + n); t[n] = (ch >= 32 && ch < 127) ? (char)ch : '|'; }
                t[n] = 0;
                // a text set again every frame (the game's own L2D) is logged when it changes
                static std::map<std::pair<uint32_t, uint32_t>, std::string> last;
                std::string& was = last[{(uint32_t)c.r[4], (uint32_t)c.r[5]}];
                if (was != t) {
                    was = t; Log("text: \"%s\"", t); report::ScreenText(t);
                    if (const char* q = strstr(t, "( ")) if (isdigit((unsigned char)q[2])) { gErrText = t; gErrFrame = gFrameNo; }
                }
            }
        }
        auto it = watch.empty() ? watch.end() : watch.find(addr);    // (beta 13: no lookup when nothing is traced)
        if (it != watch.end() && gFrameNo >= traceFrom && it->second++ < traceMax) {
            Log("trace call %08x from %08x (a0 %08x a1 %08x a2 %08x a3 %08x f12 %g f13 %g) dg %lld", addr, (uint32_t)c.r[31] - 8,
                (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], (uint32_t)c.r[7], (double)c.f[12], (double)c.f[13], gDgFrames.load());
            for (int r = 4; r <= 7; ++r) {                        // arguments that point at text
                uint32_t pa = (uint32_t)c.r[r];
                if (pa >= 0x100000 && pa < 0x01FF0000 && isprint(R8(pa)) && isprint(R8(pa + 1))) {
                    char t[81]; int n = 0;
                    for (; n < 80 && R8(pa + n); ++n) { uint8_t ch = R8(pa + n); t[n] = (ch >= 32 && ch < 127) ? (char)ch : '|'; }
                    t[n] = 0;
                    Log("  a%d -> \"%s\"", r - 4, t);
                }
            }
            if (ring.count(addr)) DumpRing("trace");
            auto dm = dump.find(addr);
            uint32_t p0 = (uint32_t)c.r[4];
            if (dm != dump.end() && p0 >= 0x100000 && p0 + dm->second < 0x02000000) {
                std::string hex;
                for (int k = 0; k < dm->second && k < 256; ++k) { char b[4]; snprintf(b, sizeof(b), "%02x", R8(p0 + k)); hex += b; if ((k & 3) == 3) hex += ' '; }
                Log("  a0 -> %s", hex.c_str());
            }
        }
    }
#endif
    const uint32_t called = va;
    if (otherPage) {
        if (uint32_t nt = gNative ? 0 : ps2::PageAlias(addr)) {  // the page's copy of a library ntmenu2 links (the MOD serves it)
            addr = nt;
            if (Mc2(c, addr)) return true;
        } else {
            if (IsRecompiled(va)) return false;
            if (Once(((uint64_t)6 << 40) | addr))
                Log("%s call %08x not recompiled (a0 %08x a1 %08x a2 %08x a3 %08x, from %08x), 0 returned", gOverlayName.c_str(),
                    addr, (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], (uint32_t)c.r[7], (uint32_t)c.r[31] - 8);
            c.r[2] = 0; c.r[3] = 0;
            return true;
        }
    }
    // mrdUPnP's own trace: its print (0x5F64AC) formats the text and hands it to the
    // game's event handler as event 0 (NewOLMPortCheck: 0x36C860); logged, then run
    if (addr == 0x36c860 && c.r[4] == 0 && c.r[5] >= 0x100000 && c.r[5] < 0x02000000) {
        static long lines = 0;
        if (++lines <= 4000) {
            std::string t;
            for (uint32_t k = 0; k < 400; ++k) {
                uint8_t ch = R8((uint32_t)c.r[5] + k);
                if (!ch) break;
                if (ch == '\n') { if (!t.empty()) Log("mrdUPnP: %s", t.c_str()); t.clear(); continue; }
                t += (ch >= 32 && ch < 127) ? (char)ch : '.';
            }
            if (!t.empty()) Log("mrdUPnP: %s", t.c_str());
        }
    }
    // DG_SetCamera's perspective matrices (sceVu0ViewScreenMatrix, called only there, twice): the aspect X
    // (f13, the literal 1.0) scaled for a wider view; the frustum planes come from this matrix
    if (gNative && addr == 0x148530 && !InMatch() && P.lobbyWide && P.hudX > 0.0f && P.hudX != 1.0f) c.f[13] *= P.hudX;   // the lobby's 3D (a character) at its proportions
    if (gNative && addr == 0x148530 && InMatch()) {
        if (P.wideX > 0.0f && P.wideX != 1.0f) c.f[13] *= P.wideX;
        if (P.wideY > 0.0f && P.wideY != 1.0f) c.f[14] *= P.wideY;
        static float said = 0;
        if (said != P.wideX + 2 * P.wideY && (P.wideX > 0 || P.wideY > 0)) {
            said = P.wideX + 2 * P.wideY;
            Log("widescreen: the 3D view x %.4f, y %.4f (2D elements x %.4f)", P.wideX > 0 ? P.wideX : 1.0f, P.wideY > 0 ? P.wideY : 1.0f, P.hudX > 0 ? P.hudX : 1.0f);
        }
    }
    // DG's 2D elements (DMAPACKs) in widescreen: a VIF1 MARK before and after each, so the GS
    // side narrows it to its 4:3 width at its edge (gs.cpp GsPackMark). 0x13000C draws a phase's
    // queue (a2: the phase); 0x132348 runs one element's commands; 0x132410 at 0x1301DC sends a
    // precompiled element's two CALL tags
    if (gNative && ((P.hudX > 0.0f && P.hudX != 1.0f) || ps2::GsHudScale() != 1.0f || gBcGaugeShown) &&   // the gauge: its
        (addr == 0x13000c || addr == 0x132348 || (addr == 0x132410 && (uint32_t)c.r[31] == V(0x1301e4)))) {   // marks in 4:3 too
        static int phase = 0;
        const bool match = InMatch();
        ps2::GsSetHud((match || P.lobbyWide) && P.hudX > 0.0f ? P.hudX : 1.0f, match);   // the lobby draws its queue without 0x13000C
        if (addr == 0x13000c) { phase = (int)c.r[6] & 0xf; return false; }
        if (!match && !P.lobbyWide) return false;                // the lobby's pages in a 4:3 picture: as drawn
        auto mark = [&](uint32_t imm) {                          // a CNT tag of no data with MARK imm, sent like the element's own
            W32(0x70000030, 0x10000000); W32(0x70000034, 0); W32(0x70000038, 0x07000000u | imm); W32(0x7000003c, 0);
            c.r[4] = 0x70000230; c.r[5] = 0x70000030; c.r[6] = 1;
            if (ps2::Fn send = ps2::FindFn(V(0x132410))) send(c);
        };
        if (ps2::Fn f = ps2::FindFn(va)) {
            const uint64_t a0 = c.r[4], a1 = c.r[5], a2 = c.r[6], ra = c.r[31];
            // the precompiled element's two tags (0x70000030..4F) are kept while the MARK uses 0x70000030
            uint64_t keep[4] = {R64(0x70000030), R64(0x70000038), R64(0x70000040), R64(0x70000048)};
            // sixty: 0x20 the font system's element (its packets: 0x27F340 + 0x44), the only one a 3D label's glyphs are in
            const uint32_t fontPack = R32(V(0x27F340) + 0x44) & 0x01FFFFFF;
            bool font = false;
            for (int t = 0; t < 4 && addr == 0x132410 && fontPack >= 0x100000 && fontPack + 0x20 < 0x02000000; t += 2) {   // its two CALLs
                const uint32_t call = (uint32_t)(keep[t] >> 32) & 0x01FFFFFF;
                font |= call && (call == (R32(fontPack + 0x18) & 0x01FFFFFF) || call == (R32(fontPack + 0x1C) & 0x01FFFFFF));
            }
            mark(0x5a00u | (uint32_t)phase | (addr == 0x132410 ? 0x10u : 0u) | (font ? 0x20u : 0u));
            W64(0x70000030, keep[0]); W64(0x70000038, keep[1]); W64(0x70000040, keep[2]); W64(0x70000048, keep[3]);
            c.r[4] = a0; c.r[5] = a1; c.r[6] = a2; c.r[31] = ra;
            f(c);
            mark(0x5aff);
            c.r[31] = ra;
            return true;
        }
    }
    if (gNative && (addr == 0x143a40 || addr == 0x143b00)) {             // DG's frame: its wait before the swap
        ++gDgFrames;
        ps2::gFrames = (unsigned long long)gDgFrames.load();              // (v150-host beta 16) a frame of the game runs: wildguard.h's loop that never ends
    }
    if (gNative && addr == 0x144190) {                          // DG's rate: 0 every VBlank, else every other one
        const bool run60 = c.r[4] && P.matchFps == 60;          // a map asking for 30, shown at 60
        gMapAsks30 = c.r[4] != 0;
        Log("frame rate: the game asks for %s%s%s", c.r[4] ? "30 frames a second" : "60 frames a second",
            run60 ? ", shown at 60 (MGO1.exe setting)" : "",
            !run60 ? "" : FPS60_KIND == 0 ? ": the game at the PS2's pace, a picture after each of its passes"
                        : FPS60_KIND == 1 ? ": the engine's full rate with the fair rules" : ": the engine's full rate without rules");
        // exact 60 (the default): the PS2's half rate as it is, DG's frame target 1 at each VBlank and a
        // picture drawn after each tick pass (ExtraPicture); the full rate (1, 2): 0x144190(0)
        if (run60 && FPS60_KIND != 0) c.r[4] = 0;
        // the fair rules at the full rate (recomp/variants.txt, docs/FPS60.md): the player's code at the PS2's pace
        ps2::gVariant[3] = run60 && FPS60_KIND == 1;
        if (!ps2::gVariant[3]) ps2::gVariant[4] = 0;
    }
    if (gNative && (addr == 0x1222e8 || addr == 0x1224a0)) {   // the game's own GV: its actors logged
        std::map<uint32_t, std::string>& names = gNativeActors;
        if (addr == 0x1222e8) {
            const uint32_t nm = R32(V(0x23A5F0));
            std::string name = (nm >= 0x100000 && nm < 0x01000000 && isprint(R8(nm))) ? Str(nm) : "?";
            names[(uint32_t)c.r[4]] = name;
            if (name.rfind("NewSoftwareKeyboard", 0) == 0) gSoftKeyboards[(uint32_t)c.r[4]] = (uint32_t)c.r[6];
            if (name == "GM_UsbkbdStartDaemon") { gUsbkbWork = (uint32_t)c.r[4]; gUsbkbAct = (uint32_t)c.r[6]; }
            if (name == "NewNetworkDummyPlayer") ps2::M37Reset();          // the local player (re)spawned: no pump owed
            if (name.rfind("NewItemBox", 0) == 0) { gLastItemBox = (uint32_t)c.r[4]; ++gItemBoxesMade; }   // the last box made
            static const char* dumpActor = getenv("SIM_GS_DUMP_ACTOR");                  // tests: a GS dump when it is made
            if (dumpActor && gDumpActorAt < 0 && name == std::string(dumpActor, strcspn(dumpActor, ":"))) gDumpActorAt = gFrameNo + 1;
            if (gFactoryDepth > 0 && name.rfind("NewWeapon", 0) == 0 && gFactoryOwner >= 0) gWeaponActors[(uint32_t)c.r[4]] = {gFactoryOwner, gFactoryCode};
            else gWeaponActors.erase((uint32_t)c.r[4]);
            static const bool quietActors = DevEnv("MGO1_QUIET_P2P") != nullptr;   // (production: hundreds a minute, none)
            if (!quietActors)
                Log("GV_SetNamedActor %s work %08x level %016llx act %08x die %08x dg %lld", name.c_str(), (uint32_t)c.r[4], (unsigned long long)c.r[5],
                    (uint32_t)c.r[6], (uint32_t)c.r[7], gDgFrames.load());
            static bool again = DevEnv("SIM_PROC11_AGAIN") != nullptr;   // tests: the online menu's result 4 (proc13 runs proc11:
            if (again && name == "NewOLMStartMenu") {                       // [27b5ee] again), as NewOLMLoad gives it after
                again = false;                                              // Load game settings (US 0x34BF04)
                Ctx x = c;
                x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
                x.r[31] = V(0x1005f0);
                x.r[4] = 4;
                Log("SIM_PROC11_AGAIN: the online menu's result 4");
                Call(x, V(0x34A8C0));
            }
        } else {
            auto it = names.find((uint32_t)c.r[4]);
            gSoftKeyboards.erase((uint32_t)c.r[4]);
            if ((uint32_t)c.r[4] == gUsbkbWork) gUsbkbWork = 0;
            gWeaponActors.erase((uint32_t)c.r[4]);
            static const bool quietActors = DevEnv("MGO1_QUIET_P2P") != nullptr;
            if (!quietActors) Log("GV_DestroyActor %s %08x (from %08x)", it != names.end() ? it->second.c_str() : "?", (uint32_t)c.r[4], (uint32_t)c.r[31] - 8);
            if (it != names.end()) names.erase(it);
        }
    }
    // SIM_BRIDGE_LOG=1: each address the platform serves, logged at its first call (with the
    // service and whether the game has its own code there): the list docs/NATIVE_BUILD.md keeps
    static const bool bridgeLog = DevEnv("SIM_BRIDGE_LOG") != nullptr;
    auto served = [&](const char* by) {
        if (bridgeLog) {
            static std::set<uint32_t> seen;
            if (seen.insert(addr).second)
                Log("served: %08x by %s%s (from %08x)", addr, by, IsRecompiled(va) ? " (recompiled code there)" : "", (uint32_t)c.r[31] - 8);
        }
        return true;
    };
    if (SessionKeys(c, addr)) return served("session keys");
    if (JoinerHold(c, addr)) return served("a loading joiner kept");     // (v150-host beta 18) joinhold.h: 0x171008, 0x1710D0
    if (Cdbios(c, addr)) return served("cdbios");
    if (Kernel(c, addr)) return served("kernel");
    if (Net(c, addr)) return served("net");
    if (Bridge(c, addr)) return served("bridge");
    // everything else that is not recompiled: logged once per address, answered 0
    if (IsRecompiled(called)) return false;
    if (gNative && NativeSyscall(c, va)) return true;
    if (Once(((uint64_t)6 << 40) | addr)) {
        Log("system call %08x not served yet (a0 %08x a1 %08x a2 %08x a3 %08x, from %08x), 0 returned",
            addr, (uint32_t)c.r[4], (uint32_t)c.r[5], (uint32_t)c.r[6], (uint32_t)c.r[7], (uint32_t)c.r[31] - 8);
        if (addr < 0x100000 || addr >= 0x02000000)            // a call through a bad pointer: how it got there
            Log("  calls before:%s", RecentCalls(24).c_str());
    }
    c.r[2] = 0; c.r[3] = 0;
    return true;
}
bool LoadImage(const char* path, uint32_t at, uint32_t maxlen) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    size_t n = fread(gMem + at, 1, maxlen, f);
    fclose(f);
    Log("image %s: %zu bytes at %08x", path, n, at);
    return n > 0;
}
}  // namespace

bool IsRecompiled(uint32_t a) { return ps2::IsFn(a); }
void ScrubSecrets(uint8_t* p, size_t n) { Scrub(p, n); }
std::string RedactSecrets(const std::string& s) { return Redact(s); }

bool Init(const Platform& p, const char* dir) {
    P = p;
    // 32 MB and a guard page: a 64-bit or quadword access at the last bytes (a stray -1
    // pointer, masked to 0x1FFFFFF) stays inside
    if (!gMem) gMem = (uint8_t*)VirtualAlloc(nullptr, (32 << 20) + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!gMem) { Log("no memory for the PS2 space"); return false; }
    char a[MAX_PATH * 2], b[MAX_PATH * 2];
    _snprintf_s(a, sizeof(a), _TRUNCATE, "%s\\ntmenu2_046341.bin", dir);
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%s\\mgs3_n_base.bin", dir);
    gOverlayPath = a;
    gImageDir = dir;
    DiscMount(dir);
    if (!LoadImage(a, 0x2DA680, 0x36B000) || !LoadImage(b, 0x100000, 0x1DA680)) {
        Log("images missing in %s (run ps2_images.py)", dir);
        return false;
    }
    gLog = P.log ? P.log : nullptr;
    gBridge = &DispatchBridge;
    FindStubs(); ps2::gSyscall = &PlatformSyscall;
    gWinLo = kObjLo; gWinHi = kObjHi;
    gCursorWin = kGclCursor;
    if (P.stageFlags) gStageWin = kStageFlags;
    W32(0x2432E8, 0x01B00000);                     // newlib's sbrk pointer (base 0x1F5DA0): its heap above ours
    gWinRead = &WinRead; gWinWrite = &WinWrite;
    gWatchLog = &WatchLog;
    ps2::Vu0Reset();
    if (const char* dl = DevEnv("SIM_DMA_LOG")) ps2::gDmaLog = (uint32_t)atoi(dl);   // the first N DMA starts
    if (const char* sw = DevEnv("SIM_WATCH")) {
        uint32_t lo = (uint32_t)strtoul(sw, nullptr, 16) & 0x01ffffff, len = 4;
        if (const char* c = strchr(sw, ':')) len = (uint32_t)strtoul(c + 1, nullptr, 0);
        gWatchLo = lo; gWatchHi = lo + len;
    }
    gBack = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    { EeThread m; m.id = 1; m.state = 1; m.prio = 1; gThreads.push_back(m); }
    gReady = true;
    if (ps2::SelectOverlay("ntmenu2")) Log("page code: ntmenu2's own table selected");
    Log("ready: %d recompiled functions, window %08x-%08x", ps2::BaseFunctionCount(), kObjLo, kObjHi);
    // the GV init (0x121088): the resident heap top (0x2C5E10 / 0x2C5E14) as its 0x121BF8 sets it
    // (its memset of 0x600000..0xF00000 left out: the overlay is already loaded in that range),
    // then pool 0, before the page's first actor
    W32(0x2c5e10, 0x00F00000); W32(0x2c5e14, 0x00F00000);
    ResetPool0("GV init");
    gWorldBegin = NewWorldActor(kWorldBeginLevel);
    Log("world: frame begin actor %p (level %016llx)", gWorldBegin, (unsigned long long)kWorldBeginLevel);
    return true;
}
// MGS3_N.ELF as the PS2 boots it: its one segment at 0x800000, then what its loader stub (the
// entry, 0x886D58) does: inflate the image at 0x800004 to [0x885BF4] (0x100000), XOR the word
// at +4 with the sum of the stub's own words 0x886D50..0x889D78; the game then starts at
// [0x885BF8] (crt0, 0x100024)
bool BootElf() {
    DiscSpan span;
    FILE* f = DiscOpen("MGS\\NET\\MGS3_N.ELF", span);
    if (!f) return false;
    const std::string path = span.host + (span.base ? " (MGS\\NET\\MGS3_N.ELF)" : "");
    std::vector<uint8_t> e((size_t)std::min<long long>(span.size, 64 << 20));
    const bool read = fread(e.data(), 1, e.size(), f) == e.size();
    fclose(f);
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= e.size()) memcpy(&v, &e[o], 4); return v; };
    if (!read || u32(0) != 0x464c457f) { Log("native: %s is not a PS2 program", path.c_str()); return false; }
    const uint32_t ph = u32(0x1C), off = u32(ph + 4), va = u32(ph + 8), fsz = u32(ph + 16), entry = u32(0x18);
    if (va != 0x800000 || (size_t)off + fsz > e.size() || entry < va || entry + 0xE0 > va + fsz) { Log("native: %s: unexpected segment", path.c_str()); return false; }
    memcpy(gMem + va, &e[off], fsz);
    // the loader stub at the entry (each version's the same code at its own place): the image's
    // pointer (lui t6 at +0x1C, lw s1 at +0x38), the range it sums (+0x90 / +0x98, +0x94 / +0x9C) and
    // XORs into the image's word at +4, the image's entry (+0xD0 / +0xDC)
    auto w = [&](uint32_t k) { return R32(entry + k); };
    auto hi = [&](uint32_t k) { return (w(k) & 0xffff) << 16; };
    auto lo = [&](uint32_t k) { return (uint32_t)(int32_t)(int16_t)(w(k) & 0xffff); };
    if ((w(0x1C) >> 16) != 0x3C0E || (w(0x38) >> 16) != 0x8DD1 || (w(0xDC) >> 16) != 0x8DCF) {
        Log("native: %s: its loader is not the one of MGO1's programs", path.c_str()); return false; }
    const uint32_t dstPtr = hi(0x1C) + lo(0x38), sumLo = hi(0x90) + lo(0x98), sumHi = hi(0x94) + lo(0x9C), entryPtr = hi(0xD0) + lo(0xDC);
    std::vector<uint8_t> img;
    if (!ZlibInflate(&e[off + 4], fsz - 4, img) || img.size() > 0x1DA680) { Log("native: %s: its image does not inflate", path.c_str()); return false; }
    uint32_t sum = 0;
    for (uint32_t a = sumLo; a < sumHi && a < 0x02000000; a += 4) sum += R32(a);
    const uint32_t dst = R32(dstPtr);
    if (dst < 0x100000 || dst + img.size() > 0x02000000) { Log("native: %s: its image's place %08x", path.c_str(), dst); return false; }
    memcpy(gMem + dst, img.data(), img.size());
    if (!DevEnv("SIM_NO_LOADER_SUM")) W32(dst + 4, R32(dst + 4) ^ sum);     // (the simulation's measures: the image as inflated)
    // which version: the image's hash without its word at +4, or most of its 4 KB blocks (a patched copy)
    int pc = 0;
    const char* name = MatchProgram(gMem + dst, img.size(), &pc);
    const VersionBase* vb = nullptr;
    for (int i = 0; name && i < kVersionBaseCount; ++i) if (!strcmp(kVersionBases[i].version, name)) vb = &kVersionBases[i];
    if (!vb || !ps2::SelectVersion(vb->version)) {
        Log("native: %s is a version of MGO1 this build was not recompiled for (image %zu bytes, %d%% of its blocks as the closest one's)",
            path.c_str(), img.size(), pc);
        return false;
    }
    if (pc < 100) Log("native: %s: a patched copy of the %s program (%d%% of its image the same): its code runs as the %s program's",
                      path.c_str(), vb->version, pc, vb->version);
    ps2::gPageBase = vb->pageBase;
    gUsProgram = !strcmp(vb->version, "US");
    if (!gUsProgram) MapStubs();
    VersionAddresses();
    Log("native: %s: the %s program, image inflated to %08x (%zu bytes), entry %08x, pages at %#x", path.c_str(), vb->version, dst, img.size(),
        R32(entryPtr), vb->pageBase);
    return true;
}
// the native build: MGO1 boots from its own entry, the platform is the PS2's system (above)
int NativeRun(const Platform& p, const char* dir, long long frames) {
    P = p;
#ifdef MGO1_PLAYER_BUILD
    P.fps60Kind = 0; P.m37Off = false;                      // the measures' modes (docs/FPS60.md): the simulation's only
#endif
    gNative = true;
    ps2::gVariant[5] = !P.m37Off;                          // the M37's pump owed after a weapon change (variant 5)
    ps2::gVariant[6] = UpstreamOn();                        // the upstream bandwidth's new choices (variant 6)
    if (!gMem) gMem = (uint8_t*)VirtualAlloc(nullptr, (32 << 20) + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!gMem) return 1;
    gLog = P.log ? P.log : nullptr;
    // the PS2 kernel's RAM (0 - 0x7FFFF) is the platform's: its blocks and the interrupt stack
    kHeapLo = 0x00020000; kHeapHi = 0x00070000; gHeap = kHeapLo; kStackTop = 0x0007FF00;
    gImageDir = dir;
    if (!DiscMount(dir)) Log("native: %s: no disc there (a folder with its files, or an .iso image)", dir);
    else Log("native: the disc: %s (%s)", dir, DiscIsImage() ? "ISO 9660 image" : "folder");
    Log("native: MGO1_core.dll %s", MGO1_BUILD);
    char b[MAX_PATH * 2];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%s\\mgs3_n_base.bin", dir);
    if (!LoadImage(b, 0x100000, 0x1DA680) && !BootElf()) {
        Log("native: no MGS3_N.ELF in %s (the disc's MGS\\NET\\MGS3_N.ELF, or the folder itself)", dir); return 1; }
    gBridge = &DispatchBridge;
    FindStubs(); ps2::gSyscall = &PlatformSyscall;
    gWatchLog = &WatchLog;
    ps2::Vu0Reset();
    if (const char* dl = DevEnv("SIM_DMA_LOG")) ps2::gDmaLog = (uint32_t)atoi(dl);
    if (const char* dc = DevEnv("SIM_DMA_LOG_CH")) ps2::gDmaLogCh = atoi(dc);   // SIM_DMA_LOG counts that channel's only
    if (const char* sw = DevEnv("SIM_WATCH")) {
        uint32_t lo = (uint32_t)strtoul(sw, nullptr, 16) & 0x01ffffff, len = 4;
        if (const char* c = strchr(sw, ':')) len = (uint32_t)strtoul(c + 1, nullptr, 0);
        gWatchLo = lo; gWatchHi = lo + len;
    }
    // the display is the game's own (DG): the GS reports its events, the DMA / GS interrupts reach their handlers
    gDgLive = true;
    ps2::gGsReal = true;
    // MGO1_HEADLESS=1: a dedicated host with no picture: VU1 does not run, the GIF keeps only
    // SIGNAL / FINISH / LABEL (what the game reads back), nothing is rasterized
    if (DevEnv("MGO1_HEADLESS")) {
        ps2::gHeadless = true; ps2::gGsDraw = false;
        Log("headless: no picture (VU1 off, the GIF keeps SIGNAL / FINISH / LABEL only)");
    }
    ps2::gIrqDeliver = &IrqDeliver;
    if (const char* e = getenv("SIM_GS_THREADS")) ps2::GsSetThreads(atoi(e));
    ps2::gGsPresented = [] { PerfSampleCpu(2); };            // perf.h: where the GS thread runs
    // the GS thread (gs_thread.h): VIF1, VU1, the GIF and the GS beside the game thread, in two stages (the GIF and the GS
    // on a second thread) when P.gsStages is 2; MGO1_GS_THREAD=0 / 1 / 2 overrides
    {
        const char* e = getenv("MGO1_GS_THREAD");
        const int want = ps2::gHeadless ? 0 : e ? atoi(e) : P.gsThread ? std::max(1, P.gsStages) : 0;   // (headless: nothing drawn)
        if (want > 0) ps2::GsThreadStart(want >= 2 ? 2 : 1);
        else Log("gs thread: off (VIF1, VU1, the GIF and the GS on the game thread)");
    }
    gBack = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    gKernelWake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    ps2::gIrqWake = [] { SetEvent(gKernelWake); };
    ps2::gGsClock = [] { return QpcNow(); };                                 // the FINISH times (the pace guard)
    gThreads.reserve(256);                             // thread pointers stay valid (the host threads hold them)
    NativeLoadPageSigs();
    {   // the sound: Konami's IOP sound driver, recompiled, from the disc's MODULES\MODNCD.IPK
        DiscSpan span;
        std::vector<uint8_t> pkg;
        if (FILE* f = DiscOpen("MODULES\\MODNCD.IPK", span)) {
            pkg.resize((size_t)span.size);
            if (_fseeki64(f, span.base, SEEK_SET) != 0 || fread(pkg.data(), 1, pkg.size(), f) != pkg.size()) pkg.clear();
            fclose(f);
        }
        iop::Host h; h.eeMem = gMem; h.cdRead = &IopCdRead; h.log = gLog;
        std::string err;
        if (pkg.empty()) Log("sound: no MODULES\\MODNCD.IPK on the disc: no sound");
        else if (DevEnv("SIM_NO_IOP")) Log("sound: the IOP is off (SIM_NO_IOP)");
        else if (!iop::Start(h, pkg, err)) Log("sound: %s: no sound", err.c_str());
        else Log("sound: the IOP's sound driver runs (sdlib, sddrv_mn from MODNCD.IPK)");
    }
    {   // maps 13 and up (docs/EXTRA_MAPS.md): their stages rebuilt from this version's in the background
        const char* md = getenv("MGO1_MAPS_DIR");
        const std::string maps = md ? md : P.mapsDir ? P.mapsDir : "";
        DiscSpan sd;
        if (!maps.empty() && DiscFind("MGS\\NET\\STAGE_N.DAT", sd)) ExtraMapsInit(maps, sd.host, sd.base, sd.size, &Log);
    }
    gReady = true;
    {   // the main thread: crt0, on a small stack of the kernel's until SetupThread gives it its own
        EeThread m; m.id = 1; m.entry = 0x100024; m.stack = 0x00078000; m.size = 0x7F00; m.prio = 0; m.eie = 0;   // crt0 enables them
        m.state = 2; m.seq = ++gSeq; m.go = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        gThreads.push_back(m);
        gThreads[0].os = CreateThread(nullptr, 1 << 20, ThreadBody, (LPVOID)(intptr_t)1, 0, nullptr);
        gOsOf[1] = gThreads[0].os;
    }
    Log("native: %d base functions recompiled; boot at 0x100024 (crt0)", ps2::BaseFunctionCount());
    NativeKernelLoop(frames);
    Log("native: %lld frames run", gFrameNo);
    return 0;
}
void Report(const char* why);
// the page overlay comes back (ntmenu2 loaded again after it had left): read again,
// bss cleared, as the PS2 stage loader does at 0x2DA680
bool LoadPageOverlay(const char* page);
void OverlayLoaded(const char* page) {
    if (!gReady || gOverlayLive) return;
    LoadPageOverlay(page ? page : "ntmenu2");
}
long long DgFrames() { return gDgFrames.load(); }
long long GameTicks() { return gTicks.load(); }
long long DgPictures() { return gDgFrames.load(); }
int CrashState(char* out, int n) {
    if (!out || n <= 0) return 0;
    int k = snprintf(out, (size_t)n, "ps2 world: frame %lld, page %s%s, map %d, run of %08x, last call %08x from %08x\nrecent calls:",
                     gFrameNo, gOverlayName.c_str(), gOverlayLive ? "" : " (gone)", gLoadedMap, gRunFn, gLastCall, gLastCaller - 8);
    for (unsigned i = 0; i < 64 && k > 0 && k < n - 24; ++i) {
        const auto& e = gRing[(gRingPos - 64 + i) & 63];
        if (e[0]) k += snprintf(out + k, (size_t)(n - k), "%s%08x<%08x", i % 8 ? " " : "\n  ", e[0], e[1] - 8);
    }
    for (unsigned th = 1; th < 16 && k > 0 && k < n - 64; ++th) {
        if (!gRingTPos[th]) continue;
        k += snprintf(out + k, (size_t)(n - k), "\nthread %u:", th);
        for (unsigned i = 48; i < 64 && k < n - 24; ++i) {
            const auto& e = gRingT[th][(gRingTPos[th] - 64 + i) & 63];
            if (e[0]) k += snprintf(out + k, (size_t)(n - k), " %08x<%08x", e[0], e[1] - 8);
        }
    }
    if (k > 0 && k < n - 2) { out[k++] = '\n'; out[k] = 0; }
    return k < 0 ? 0 : std::min(k, n - 1);
}
void SetMatchFps(int fps) { gFpsWant.store(fps == 60 ? 60 : 0); }
void SetTimeScale(double k) { TimeScaleSet(k); }
void SetNetTimeScale(double k) { NetScaleSet(k); }
// (v150-host beta 22, pickup) the drops' records (ps2world.h DropRec; US n003a 0x3A0A48 the manager's act, 0x3A08B0 a record
// applied, 0x3A2280 a claim, 0x3A2438 a take): each record +0 its number, +4 kind << 10 | code (+0x8000 its box falling),
// +0x10 its place (the box's own while it is in the world, 0x39FAD0), +0x24 its count, +0x40 its box's handle (the actor's
// +0x18: its address >> 4 in the low word), +0x50 its state. The box: its +0xC8 bit 2 once taken, +0xD0 its reach (the
// pick-up zone 400 mm either way along x and z around the box, 1500 mm up and down from its place raised by the reach:
// 0x4C94D8 with 0x2F5A80's half sizes)
int DropRecords(DropRec* out, int max) {
    const uint32_t ref = N3DropsRef();
    const uint32_t mgr = ref >= 0x100000 && ref < 0x02000000 ? R32(ref) : 0;
    if (mgr < 0x100000 || mgr + 0x6F0 >= 0x02000000) return 0;
    int n = 0;
    for (uint32_t k = 0; k < 16 && n < max; ++k) {
        const uint32_t rec = mgr + 0xD0 + 0x60 * k;
        DropRec d;
        d.number = (int32_t)R32(rec);
        d.flags = R32(rec + 4);
        d.kind = (int)((d.flags & 0x1E00) >> 10); d.code = (int)(d.flags & 0x1FF);
        d.count = (int32_t)R32(rec + 0x24);
        d.state = R8(rec + 0x50);
        for (int j = 0; j < 3; ++j) d.pos[j] = F32(rec + 0x10 + 4 * (uint32_t)j);
        const uint32_t lo = R32(rec + 0x40), hi = R32(rec + 0x44), box = lo << 4;
        if ((lo || hi) && box >= 0x100000 && box + 0xE0 < 0x02000000 && R32(box + 0x18) == lo && R32(box + 0x1C) == hi && !(R32(box + 0xC8) & 4)) {
            d.box = true; d.reach = F32(box + 0xD0);
        }
        out[n++] = d;
    }
    return n;
}
// (v150-host beta 25, food) the mushroom man's entries (ps2world.h FoodRec; US n003a 0x3A7E60 the man's act, 0x3A7BC8 a record
// applied, 0x3A8688 a box taken, 0x3A7DD0 the host's timers): each entry +0 its number (0x100 + n), +0x10 its point, +0x20 the
// point beside it, +0x30 the state last sent, +0x34 its state, +0x3C the food's code, +0x40 the model, +0x44 the timer, +0x48
// its actor's handle (the mushroom, then its box: the actor's +0x18 and +0x1C). The box (NewItemBox2): +0xC8 bit 2 once taken,
// bit 3 no pick-up, +0xD0 its reach, +0x220 its place (the pick-up zone's, 0x4C9FC8). A food's share of the stun gauge's
// maximum by its class (the descriptor's flags, 0x56B718 + code x 0x38 + 0x14, bits 20 to 22: the engine's eat 0x305F90 and its
// constants 0x542DB0 to 0x542DC0)
int FoodRecords(FoodRec* out, int max) {
    const uint32_t man = ActorByName("NewOnlineMushRoomMan", 0);
    if (man < 0x100000 || man + 0x5D0 >= 0x02000000) return 0;
    const uint32_t descs = N3WeaponDescs();
    int n = 0;
    for (uint32_t k = 0; k < 16 && n < max; ++k) {
        const uint32_t e = man + 0xC0 + 0x50 * k;
        const int number = (int32_t)R32(e);
        if (number < 0x100 || number > 0x1FF) continue;
        FoodRec f;
        f.number = number; f.state = (int32_t)R32(e + 0x34); f.code = (int32_t)R32(e + 0x3C); f.timer = (int32_t)R32(e + 0x44);
        for (int j = 0; j < 3; ++j) { f.at[j] = F32(e + 0x10 + 4 * (uint32_t)j); f.pos[j] = f.at[j]; }
        if (!(std::fabs(f.at[0]) < 1e6f && std::fabs(f.at[1]) < 1e6f && std::fabs(f.at[2]) < 1e6f)) continue;
        const uint32_t lo = R32(e + 0x48), hi = R32(e + 0x4C), box = lo << 4;
        if ((lo || hi) && box >= 0x100000 && box + 0x230 < 0x02000000 && R32(box + 0x18) == lo && R32(box + 0x1C) == hi && !(R32(box + 0xC8) & 0xC) &&
            (int32_t)R32(box + 0x44) == f.code) {
            float p[3];
            for (int j = 0; j < 3; ++j) p[j] = F32(box + 0x220 + 4 * (uint32_t)j);
            if (std::fabs(p[0]) < 1e6f && std::fabs(p[1]) < 1e6f && std::fabs(p[2]) < 1e6f) { f.box = true; memcpy(f.pos, p, sizeof p); f.reach = F32(box + 0xD0); }
        }
        int cls = -1;
        if (f.code >= 0x32 && f.code < 0x83 && descs >= 0x100000 && descs + 0x38 * 0x83 < 0x02000000) cls = (int)((R32(descs + 0x38 * (uint32_t)f.code + 0x14) >> 20) & 7);
        if (cls < 0) cls = f.code == 0x5B ? 3 : f.code == 0x56 ? 1 : 0;   // (no descriptor read: the disc maps' two foods)
        f.gain = f.code >= 0x32 && f.code < 0x62 ? vdoll::food::Gain(cls) : 0;
        out[n++] = f;
    }
    return n;
}
// tests: the place of an item box (a drop: its code +0x44, its body +0x1F8 with the place at +0x40): the one of
// that code (the last made when code is 0), false when none
int LocalWeaponsMade() { return gLocalWeaponsMade; }
int BoxesTaken() { return gBoxesTaken; }
bool ItemBoxPos(float xyz[3], int code) {
    uint32_t w = code ? 0 : gLastItemBox;
    if (code)
        for (const auto& a : gNativeActors)
            if (a.second.rfind("NewItemBox", 0) == 0 && (int)R32(a.first + 0x44) == code) w = a.first;
    if (!w || !gNativeActors.count(w)) return false;
    const uint32_t body = R32(w + 0x1F8);
    if (body < 0x100000 || body >= 0x01FFF000) return false;
    for (int k = 0; k < 3; ++k) { uint32_t u = R32(body + 0x40 + 4 * (uint32_t)k); memcpy(&xyz[k], &u, 4); }
    return true;
}
int TsnePoints(float out[4][3]) { return TsneTestPoints(out); }   // tests: TEAM SNEAKING's targets and goals (tsne.h)
bool MissionObjectPos(int k, float xyz[3], uint32_t* holder) {   // tests: a mission object's place and holder (extrarules.h)
    const uint32_t mo = MissionObjects();
    if (!mo || k < 0 || (uint32_t)k >= R32(mo + 0x410) || k > 2) return false;
    const uint32_t o = mo + 0xB0 + 0x120 * (uint32_t)k;
    for (int j = 0; j < 3; ++j) xyz[j] = F32(o + 0x90 + 4 * (uint32_t)j);
    if (holder) *holder = R32(o + 0xD4);
    return true;
}
int VisitPoint(int k, float xyz[3], uint32_t* code) {    // tests: SIM_VISIT's point k as the game found it (extrarules.h); how many
    if (k < 0) return gVisitGen;                               // k -1: the list's number (one more at each map)
    if (k < (int)gVisit.size()) {
        xyz[0] = gVisit[(size_t)k].x; xyz[1] = gVisit[(size_t)k].y; xyz[2] = gVisit[(size_t)k].z;
        if (code) *code = gVisit[(size_t)k].code;
    }
    return (int)gVisit.size();
}
int TsneRole() { return TsneRound() && gTsneObjects ? (TsneSneaker() ? 1 : 0) : -1; }
void TsneTestPicture(const char* what, int frames) { TsnePngAsk(what, frames); }   // tests (SIM_TSNE_DUEL)
uint32_t TsneTestPad() { return gTsnePad; }   // tests (SIM_TSNE_PAD)
int MenuScreen() { const uint32_t w = PauseMenuWork(); return w && w + 0x340 <= 0x02000000 ? (int)R16(w + 0x326) : -1; }   // tests (tsne.h runs)
int BcBases(float out[5][3]) { return BcTestBases(out); }   // tests: BASE CAPTURE's bases (basecap.h)
uint32_t BcRoundId() { return BcTestRound(); }               // tests: BASE CAPTURE's round (basecap.h)
float BcTimeLeft(int* result) { return BcTestTimeLeft(result); }   // tests: BASE CAPTURE's round time left (basecap.h)
// (v150-host beta 16) a virtual doll's respawn in BASE CAPTURE as BcSpawnBase gives a player's: a base its team holds, not
// contested nor being taken, the nearest to where it fell, 0.5 m from its centre (the pick's eighth of a turn); false: none
bool BcVdollRespawn(int team, unsigned pick, const float fell[3], float out[3]) {
    if (!BcRound() || gBc.n <= 0) return false;
    const bool haveFell = std::fabs(fell[0]) < 1e6f && std::fabs(fell[2]) < 1e6f;
    int best = -1; float bd = 0;
    for (int i = 0; i < gBc.n && i < 5; ++i) {
        const BcBase& b = gBc.b[i];
        if (b.owner != team || b.contested || (b.toward >= 0 && b.toward != team && b.prog > 0)) continue;
        const float dx = b.x - fell[0], dz = b.z - fell[2];
        const float d = haveFell ? dx * dx + dz * dz : (float)i;
        if (best < 0 || d < bd) { best = i; bd = d; }
    }
    if (best < 0) return false;
    const BcBase& b = gBc.b[best];
    const float a = (float)(pick % 8) * 0.785398f, r = 500.0f;
    out[0] = b.x + r * sinf(a); out[1] = b.y; out[2] = b.z + r * cosf(a);
    return std::fabs(out[0]) < 1e6f && std::fabs(out[1]) < 1e6f && std::fabs(out[2]) < 1e6f;
}
bool BcVdollClear(const float p[3]) { return !BcRound() || BcByBase(p[0], p[1], p[2]) < 0; }   // (outside every base's zone and a step)
int BcBaseStates(float pos[5][3], int owner[5], int contested[5]) {   // the PLAYABLE DOLL: BASE CAPTURE's bases now (basecap.h)
    if (!BcRound() || !gBc.n) return 0;
    const int n = std::min(gBc.n, 5);
    for (int i = 0; i < n; ++i) {
        pos[i][0] = gBc.b[i].x; pos[i][1] = gBc.b[i].y; pos[i][2] = gBc.b[i].z;
        owner[i] = gBc.b[i].owner; contested[i] = gBc.b[i].contested ? 1 : 0;
    }
    return n;
}
void SetGameLanguage(int lang) { gTestLanguage = lang; }
bool PlayerPos(int which, float xyz[3]) {
    int n = 0;
    if (gNative) {                                             // the game's own actors (named when made)
        for (const auto& a : gNativeActors) {
            const bool local = a.second == "NewNetworkDummyPlayer", remote = a.second == "NewNetworkPlayer";
            if (!(which == 0 ? local : (remote && ++n == which))) continue;
            for (int k = 0; k < 3; ++k) { uint32_t u = R32(a.first + 0x110 + 4 * k); memcpy(&xyz[k], &u, 4); }
            return true;
        }
        return false;
    }
    for (const Actor& a : gActors) {
        if (!a.live) continue;
        const bool local = a.name == "NewNetworkDummyPlayer", remote = a.name == "NewNetworkPlayer";
        if (!(which == 0 ? local : (remote && ++n == which))) continue;
        for (int k = 0; k < 3; ++k) { uint32_t u = R32(a.work + 0x110 + 4 * k); memcpy(&xyz[k], &u, 4); }
        return true;
    }
    return false;
}
uint32_t ActionTick(uint32_t actor) {
    const auto it = gTickOf.find(actor & 0x01ffffff);
    return it == gTickOf.end() ? 0 : it->second;
}
void LocalWeapon(int& aimed, int& reloadable, int& gun) {
    aimed = reloadable = gun = -1;
    if (!InMatch()) return;
    const uint32_t pl = PlayerObject(0) & 0x01ffffff;
    if (pl < 0x100000 || pl + 0xB60 > 0x02000000) return;
    aimed = (R32(pl + 0xB30) >> 14) & 1;                  // flag 0x2E: the gun's aim, its lowering (US n003a 0x407630 callers)
    reloadable = gun = 0;
    const uint32_t d = R32(pl + 0x608) & 0x01ffffff;     // the held weapon's descriptor (0x2FCBE8)
    if (d < 0x100000 || d + 0x38 > 0x02000000) return;
    const uint32_t flags = R32(d + 0x14);
    const int id = (int)(flags & 0xff), all = (int16_t)R16(d + 0x18), in = (int16_t)R16(d + 0x1C), size = (int16_t)R16(d + 0x1E);
    gun = id >= 5 && id <= 17;                           // the gun's weapons (its reloads 5 to 17; 19 to 21 grenades)
    reloadable = gun && size && (flags & 0x80100) && in < size && in < all;
}
uint32_t PlayerObject(int which) {
    int n = 0;
    if (gNative) {
        for (const auto& a : gNativeActors) {
            const bool local = a.second == "NewNetworkDummyPlayer", remote = a.second == "NewNetworkPlayer";
            if (which == 0 ? local : (remote && ++n == which)) return a.first;
        }
        return 0;
    }
    for (const Actor& a : gActors) {
        if (!a.live) continue;
        const bool local = a.name == "NewNetworkDummyPlayer", remote = a.name == "NewNetworkPlayer";
        if (which == 0 ? local : (remote && ++n == which)) return a.work;
    }
    return 0;
}
void SetMessageHook(void (*fn)(uint32_t, uint32_t, uint32_t)) { gMessageHook = fn; }
void SetRadioChannel(uint32_t channel) {
    if (gRadioChannel.exchange(channel) == channel) return;
    std::lock_guard<std::mutex> g(gRadioLock);
    gRadioCalls.clear();
}
size_t TakeRadioCalls(uint32_t* out, size_t max) {
    std::lock_guard<std::mutex> g(gRadioLock);
    const size_t n = std::min(max, gRadioCalls.size());
    for (size_t i = 0; i < n; ++i) out[i] = gRadioCalls[i];
    gRadioCalls.erase(gRadioCalls.begin(), gRadioCalls.begin() + n);
    return n;
}
static uint32_t VarSetData(uint32_t p) {                  // the data of the game network's variable set p ([V(0x28D528) + 4p] + 8)
    const uint32_t e = R32(V(0x28D528) + 4 * p);
    if (!e || e >= 0x02000000) return 0;
    const uint32_t d = R32(e + 8);
    return d && d < 0x02000000 ? d : 0;
}
bool PlayerInfo(int id, int* team, std::string* name) {
    if (id < 0 || id >= 16) return false;
    const uint32_t d = VarSetData((uint32_t)id + 1);
    if (!d) return false;
    if (team) *team = R8(d + 1);
    if (name) { name->clear(); for (uint32_t k = 0; k < 16 && R8(d + 2 + k); ++k) *name += (char)R8(d + 2 + k); }
    return true;
}
bool SessionPlayers(std::vector<SessionEntry>& out) {
    out.clear();
    const uint32_t count = R32(V(0x28B5F0));
    if (!count || count > 16) return false;
    const uint32_t table = V(0x28B5F4);
    for (uint32_t k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * k;
        const uint8_t id = R8(e);
        if (id >= 16 || !R16(e + 2)) continue;
        std::string nm;
        for (uint32_t c = 0; c < 16; ++c) {
            const uint8_t ch = R8(e + 0x28 + c);
            if (!ch) break;
            if (ch < 0x20 || ch > 0x7E) { nm.clear(); break; }
            nm += (char)ch;
        }
        out.push_back({id, R32(e + 8), R16(e + 2), nm});
    }
    return true;
}
uint32_t PlayerUserId(int id) {
    if (id < 0 || id >= 16) return 0;
    const uint32_t d = VarSetData((uint32_t)id + 1);
    return d ? R32(d + 0xA0) : 0;
}
bool VariableSet(int set, uint8_t* out, size_t n) {       // the game network's variable set (0 the room, 1 + id a player)
    if (set < 0 || set > 16) return false;
    const uint32_t d = VarSetData((uint32_t)set);
    if (!d || d + n > 0x02000000) return false;
    memcpy(out, gMem + d, n);
    return true;
}
bool RoomVariables(uint8_t* out, size_t n) {
    const uint32_t d = VarSetData(0);
    if (!d || d + n > 0x02000000) return false;
    memcpy(out, gMem + d, n);
    return true;
}
std::string CurrentPage() { return gOverlayLive ? gOverlayName : std::string(); }
// (v150-host beta 22, sync) the map page asked last and the extra map served for it (StageAsked, at 0x101B28), so that the
// per-map data follow one stage's page and extra map, never the old page with the new stage's extra map (stagekey.h)
stagekey::Tracker gStageAsked;
void StageAsked(Ctx& c) {                                // 0x101B28 (a0: the page's name), after ExtraStageLoad
    char nm[16] = {0};
    const uint32_t a = (uint32_t)c.r[4] & 0x01ffffff;
    for (uint32_t k = 0; k < 15 && a + k < 0x02000000; ++k) { nm[k] = (char)R8(a + k); if (!nm[k]) break; }
    gStageAsked.Asked(nm, ExtraMapsAny() ? ExtraMapsLoaded() : 0);
}
bool StageNow(std::string* page, int* extra) { return gStageAsked.Now(CurrentPage(), ExtraMapsLoaded(), page, extra); }
std::string LocalPlayerName() {                            // the log-in's player name (0x3041): TX55 context 0x28C580 + 0x152
    const uint32_t a = V(0x28C580) + 0x152;
    std::string s;
    for (uint32_t k = 0; k < 16; ++k) { const uint8_t c = R8(a + k); if (!c) break; if (c < 32 || c > 126) return std::string(); s += (char)c; }
    return s;
}
int ExtraMapNow() { return ExtraMapsLoaded(); }
bool StageResource(const std::string& stage, uint8_t type, std::vector<uint8_t>& out, std::string& err, int extra) {
    if (extra > 12) {                                      // an extra map (docs/EXTRA_MAPS.md): its own file, served in its stage's place
        const ExtraMap* m = ExtraMapById(extra);
        if (m && m->stage == stage && ExtraMapsFile(extra, type, out)) return true;
    }
    DiscSpan d;
    if (!DiscFind("MGS\\NET\\STAGE_N.DAT", d)) { err = "no MGS\\NET\\STAGE_N.DAT"; return false; }
    std::vector<std::vector<uint8_t>> all;
    if (!StageDatFiles(d.host, d.base, d.size, stage, type, all, err)) return false;
    size_t best = 0;
    for (size_t k = 1; k < all.size(); ++k) if (all[k].size() > all[best].size()) best = k;
    out = std::move(all[best]);
    return true;
}
bool LastPace(int* lines, double* rate, double* due, double* cpu) {
    if (!gPaceLast.lines) return false;
    *lines = gPaceLast.lines; *rate = gPaceLast.rate; *due = gPaceLast.due; *cpu = gPaceLast.cpu;
    return true;
}
bool ViewMatrix(float m[16]) {
    const uint32_t ch = R32(V(0x23AF80));
    if (ch < 0x100000 || ch + 64 > 0x02000000) return false;
    memcpy(m, gMem + ch, 64);
    return true;
}
void ForEachActor(void (*fn)(uint32_t work, const std::string& name, void* ctx), void* ctx) {
    if (gNative) { for (const auto& a : gNativeActors) fn(a.first, a.second, ctx); return; }
    for (const Actor& a : gActors) if (a.live) fn(a.work, a.name, ctx);
}
uint32_t ActorByName(const char* name, int nth) {
    int n = 0;
    if (gNative) {
        for (const auto& a : gNativeActors) if (a.second == name && n++ == nth) return a.first;
        return 0;
    }
    for (const Actor& a : gActors) if (a.live && a.name == name && n++ == nth) return a.work;
    return 0;
}
int SessionPeers(uint32_t* ips, int max) {
    std::lock_guard<std::mutex> g(gUdpPeersLock);
    const ULONGLONG now = GetTickCount64();
    int n = 0;
    for (auto it = gUdpPeers.begin(); it != gUdpPeers.end();) {
        if (now - it->second > 3000) { it = gUdpPeers.erase(it); continue; }
        if (n < max) ips[n] = it->first;
        ++n; ++it;
    }
    return n;
}
// a software keyboard of MGO1 is open: a NewSoftwareKeyboard_* actor (its act still at
// work + 4) in state 3 or more (u16 at work + 0x4A: 0 hidden, 1 waiting for its owner, 2 a key
// typed while closed, 3 opening, 4 typing, 8 closing; the map's chat keyboards wait in 0 to 2 for
// the whole match and SELECT opens them)
void SoundMix(int16_t* out, int frames, int waitUs) { gSoundPulled = true; iop::Mix(out, frames, waitUs); }
unsigned long long PsTimeMs() { return PsMs(); }
// the voice chat's headset (docs/VOICE.md; LgaudRpc above)
void VoiceAttach(bool on) {
    std::lock_guard<std::mutex> g(gHs.mu);
    if (on == gHs.attached) return;
    gHs.attached = on; gHs.told = false;
    if (!on) { gHs.rec.clear(); gHs.play.clear(); }
    Log("voice: %s", on ? "a microphone is there: the game's headset attached" : "no microphone: the game's headset detached");
}
void VoiceIn(const int16_t* pcm, int n) {
    std::lock_guard<std::mutex> g(gHs.mu);
    if (!gHs.recording || n <= 0) return;
    gHs.rec.insert(gHs.rec.end(), pcm, pcm + n);
    while (gHs.rec.size() > kHsRecMax) gHs.rec.pop_front();
}
int VoiceOut(int16_t* pcm, int max) {
    std::lock_guard<std::mutex> g(gHs.mu);
    // a spurt starts once 45 ms have come (the game writes a frame of 22.5 ms at a time); more than
    // 300 ms waiting (a burst after a stall) go down to 150 ms: the delay stays short
    if (!gHs.outOn && gHs.play.size() < 360) return 0;
    gHs.outOn = true;
    if (gHs.play.size() > 2400) { gHs.dropped += (long long)gHs.play.size() - 1200; gHs.play.erase(gHs.play.begin(), gHs.play.end() - 1200); }
    const int n = (int)std::min<size_t>((size_t)std::max(0, max), gHs.play.size());
    for (int i = 0; i < n; ++i) { pcm[i] = gHs.play.front(); gHs.play.pop_front(); }
    if (gHs.play.empty()) gHs.outOn = false;
    return n;
}
void VoiceSidetone(bool on) { std::lock_guard<std::mutex> g(gHs.mu); gHs.sidetone = on; }
int VoiceState() {
    std::lock_guard<std::mutex> g(gHs.mu);
    return (gHs.attached ? 1 : 0) | (gHs.open ? 2 : 0) | (gHs.recording ? 4 : 0) | (gHs.playing ? 8 : 0);
}
// the voice between PCs (vnet): the front end plays Opus; its packets while this player talks; the ones
// received (the talker's player id, his talk, the packet's number); the talkers the game plays now
void VoiceNetEnable(bool on) {
    if (gVoiceOpusOn.exchange(on) != on) Log("voice net: %s", on ? "Opus between PCs on (the game's own voice with PS2 players)" : "off: the game's own voice only");
}
void VoiceNetSend(const uint8_t* opus, int n, bool newTalk) {
    if (!gVoiceOpusOn || n <= 0 || n > (int)vnet::kMaxOpus) return;
    std::lock_guard<std::mutex> g(vnet::gVn.mu);
    vnet::gVn.out.push_back({std::vector<uint8_t>(opus, opus + n), newTalk});
    while (vnet::gVn.out.size() > 25) vnet::gVn.out.pop_front();
}
int VoiceNetRecv(uint8_t* opus, int max, int* talker, int* talk, int* seq) {
    std::lock_guard<std::mutex> g(vnet::gVn.mu);
    while (!vnet::gVn.in.empty()) {
        const std::vector<uint8_t> d = std::move(vnet::gVn.in.front());
        vnet::gVn.in.pop_front();
        const int n = (int)d.size() - (int)vnet::kVoiceHead - 2;
        if (n <= 0 || n > max) continue;
        *talker = d[8]; *talk = d[9]; *seq = d[10] | d[11] << 8;
        memcpy(opus, d.data() + vnet::kVoiceHead, (size_t)n);
        return n;
    }
    return 0;
}
uint32_t VoiceNetHeard() {
    const unsigned long long now = PsMs();
    uint32_t m = 0;
    for (int i = 0; i < 32; ++i) { const unsigned long long t = gVoiceHeard[i]; if (t && now - t < kVoiceHeardMs) m |= 1u << i; }
    return m;
}
int TitleStartSound(const char* path, long long base, long long size, int16_t* out, int maxFrames) {
    return iop::RenderStartSound(path, base, size, out, maxFrames);
}
int TitleMenuSound(const char* path, long long base, long long size, int which, int16_t* out, int maxFrames) {
    return iop::RenderMenuSound(path, base, size, which, out, maxFrames);
}
std::string SoundStats() { return iop::Stats(); }
double SecondsSinceRecv() { return gLastRecvTick ? (GetTickCount64() - gLastRecvTick) / 1000.0 : -1.0; }
std::string LastErrorText(long long* frame) { if (frame) *frame = gErrFrame; return gErrText; }
std::string CpuStats(long long vblanks) { return PerfCpuText(vblanks); }   // perf.h
double FieldRate() { return (double)QpcFreq() / (double)FieldTicks(); }
bool InMatch() {                                        // a map's page (n001a .. n033a) is the loaded one
    return gOverlayLive && gOverlayName.size() >= 5 && gOverlayName[0] == 'n' && gOverlayName[1] == '0';
}
void SetButtonPrompts(int family, const char* const menu[16][2], const char* const match[16][2], const char* const pad[16]) { PromptsSet(family, menu, match, pad); }   // prompts.h
void SetWidescreen(float wideX, float wideY, float hudX, bool lobbyWide) { P.wideX = wideX; P.wideY = wideY; P.hudX = hudX; P.lobbyWide = lobbyWide; }
bool KeyboardTextEntry() {
    for (auto it = gSoftKeyboards.begin(); it != gSoftKeyboards.end();) {
        if (R32(it->first + 4) != it->second) { it = gSoftKeyboards.erase(it); continue; }
        if (R16(it->first + 0x4A) >= 3) return true;
        ++it;
    }
    return false;
}
// the program's own table (usbkb.h): the one its keyboard actor and, in the European program, USB KEYBOARD TYPE
// give now; the lights as the game set them (its software keyboards turn Caps Lock on)
bool KeyboardStrokes(uint32_t ch, uint8_t usage[2], uint8_t mods[2], int* count) {
    if (count) *count = 0;
    const char* v = ps2::SelectedVersion();
    const usbkb::Program* p = usbkb::ProgramOf(v ? v : "US");
    if (!p || !gMem) return false;
    const usbkb::Mem M{gMem, 0x02000000};
    const uint32_t kb = gUsbkbWork && R32(gUsbkbWork + 4) == gUsbkbAct ? gUsbkbWork : 0;
    int setting = -1;
    const int table = usbkb::TableNow(M, *p, kb, &setting);
    static int shownTable = -1, shownLed = -1;
    if (table != shownTable || gKbLed != shownLed) {
        shownTable = table; shownLed = gKbLed;
        if (setting >= 0) Log("keyboard: MGO1 types with its %s table (USB KEYBOARD TYPE %d), Caps Lock %s", usbkb::TableName(table), setting, (gKbLed & 2) ? "on" : "off");
        else Log("keyboard: MGO1 types with its %s table, Caps Lock %s%s", usbkb::TableName(table), (gKbLed & 2) ? "on" : "off", p->caps ? "" : " (not read)");
    }
    usbkb::Key k[2];
    const int n = usbkb::StrokesFor(M, *p, table, gKbLed, ch, k);
    for (int i = 0; i < n; ++i) { usage[i] = k[i].usage; mods[i] = k[i].mods; }
    if (count) *count = n;
    return n > 0;
}
// another page's overlay at 0x2DA680 (a map at the match load): its image (<dir>\<page>_*.bin,
// ps2_images.py) read over the old one, its bss cleared, its own function table selected
bool LoadPageOverlay(const char* page) {
    if (!gReady || !page) return false;
    char pat[MAX_PATH * 2];
    _snprintf_s(pat, sizeof(pat), _TRUNCATE, "%s\\%s_*.bin", gImageDir.c_str(), page);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { Log("stage change: no overlay image for %s in %s", page, gImageDir.c_str()); return false; }
    FindClose(h);
    std::string path = gImageDir + "\\" + fd.cFileName;
    if (!LoadImage(path.c_str(), 0x2DA680, 0x400000)) return false;
    uint32_t end = R32(0x2DA684), bss = R32(0x2DA688);
    if (bss > end && bss < 0x01000000) memset(gMem + end, 0, bss - end);
    // a build with ntmenu2 only (one table) has no page tables: its code is ntmenu2's
    bool code = ps2::SelectOverlay(page) || (!_stricmp(page, "ntmenu2") && !ps2::SelectedOverlay());
    gOverlayPath = path;
    gOverlayName = page;
    PageMapSelect(page);
    gOverlayLive = code;
    gPageNt = !_stricmp(page, "ntmenu2");
    if (gPageNt) BigGameEnd();
    Log("stage change: the %s overlay is loaded (%s, image to %08x, bss to %08x, %s)", page, fd.cFileName, end, bss,
        code ? "its recompiled code selected" : "no recompiled code for it in this build");
    return code;
}
// One page through the base's loader (GM_StartLoader's work, 0x106A60 / 0x106AB8):
// FS_LoadStageRequest(name) 0x101B28, FS_LoadStageSync(info) 0x101C98 until it answers
// 0, FS_LoadStageComplete(info) 0x101CE0. The reads complete at once (cdbios above).
std::string gResidentPage;
bool LoadOnePage(const char* name) {
    const uint32_t nm = PutStr(name);
    uint64_t info = 0, r = 1;
    ULONGLONG t0 = GetTickCount64();
    bool ok = RunPs2Args(V(0x101B28), {nm}, &info, 200000000);
    int passes = 0;
    while (ok && r && passes < 100000) { ok = RunPs2Args(V(0x101C98), {(uint32_t)info}, &r, 200000000); ++passes; if (r) Schedule(8); }
    if (ok) ok = RunPs2Args(V(0x101CE0), {(uint32_t)info}, nullptr, 200000000);
    Free(nm);
    uint64_t largest = 0;
    RunPs2(V(0x121728), 0, 0, &largest);
    Log("page data: %s %s (%d passes, %llu ms, resident top %08x, pool 0 largest free %#x)", name, ok ? "loaded" : "NOT loaded",
        passes, (unsigned long long)(GetTickCount64() - t0), R32(V(0x2C5E10)), (uint32_t)largest);
    return ok;
}
// The PS2 stage manager's work around a page load (0x104898 before it, 0x104630 once it is
// in), less what the PC side does: the PS2 GCL (0x126390, 0x1256E8, 0x125888: the PC runs the
// script), the sound (0x115BC8) and the GS / DMA part of DG's reset (0x127288)
void StageTeardown() {
    RunPs2(V(0x120f30), 0);                               // 0x103440: the time step back to 5
    for (uint32_t f : {0x12b770u, 0x12ac70u, 0x146ca8u, 0x129640u}) RunPs2(V(f), 0);   // DG's reset (0x143E28)
    W32(V(0x23B90C), 0);
    RunPs2(V(0x12ad88), 0);
    RunPs2(V(0x144190), 0);                               // full frame rate until the map asks for half (`-m 1`)
    RunPs2(V(0x162890), 0);                               // motions
    RunPs2(V(0x1089b8), 0);
    RunPs2(V(0x103530), 0);                               // the subsystems again (node pools, nav, zon, network state)
}
void StagePostLoad(bool restart) {
    if (!restart) RunPs2(V(0x121d68), 0);                 // 0x104630 after a load
    RunPs2(V(0x103600), 0);                               // the stage's system actors, DG prim types, node pool
    RunPs2(V(0x11f318), 0);
    RunPs2(V(0x11ab90), 0);
    RunPs2(V(0x11b040), 0);
}
bool StageRestart() {                                  // the same stage again (flags 0x40): no load
    if (!gReady) return false;
    StageTeardown();
    StagePostLoad(true);
    Log("stage restart: the PS2 stage manager's reset and post-load steps done");
    return true;
}
bool LoadPageData(const char* stage, const char* resident) {
    if (!gReady || !stage || gCd.size() < 2) return false;
    StageTeardown();
    const bool resChanged = resident && *resident && gResidentPage != resident;
    // 0x1034C0, the stage load's reset: DG's texture blocks back to pool 0 (0x143E80 ->
    // 0x127680), pool 0 created again, the stage state (0x104A90) and the geom tables (0x158000)
    auto stageReset = [](const char* why) {
        RunPs2(V(0x127680), 0);
        ResetPool0(why);
        RunPs2(V(0x104a90), 0);
        RunPs2(V(0x158000), 0);
    };
    if (resChanged) {                                  // 0x104898: the resident area back to its mark, then the resident page
        RunPs2(V(0x121C48), 0);
        stageReset("resident page");
        if (LoadOnePage(resident)) gResidentPage = resident;
    }
    stageReset(stage);
    const bool ok = LoadOnePage(stage);
    if (ok) StagePostLoad(false);
    ConfigBlocks(stage);
    return ok;
}
// the page's overlay left PS2 memory (stage change): its code must not run any more
void OverlayUnloaded(const char* next) {
    if (!gReady) return;
    gOverlayLive = false;
    Log("stage change: the %s overlay is unloaded (next page %s)", gOverlayName.c_str(), next ? next : "?");
    Report("after the stage change");
}
// live PS2 actors and threads, with the ones whose code lies in the page overlay flagged
void Report(const char* why) {
    if (!gReady) return;
    auto inOvl = [](uint32_t a) { return InPageArea(a); };
    int n = 0, bad = 0;
    for (auto& a : gActors) {
        if (!a.live) continue;
        ++n;
        bool b = inOvl(a.act) || inOvl(a.die);
        bad += b;
        Log("report (%s): actor %s %08x level %016llx act %08x die %08x%s", why, a.name.c_str(), a.work,
            (unsigned long long)a.level, a.act, a.die, b ? "  <- code in the page overlay" : "");
    }
    for (auto& t : gThreads) {
        if (t.id == 1 || t.deleted || t.state == 5 || t.state == 0x10) continue;
        bool b = inOvl(t.entry);
        bad += b;
        Log("report (%s): thread %d entry %08x state %d prio %d%s", why, t.id, t.entry, t.state, t.prio, b ? "  <- code in the page overlay" : "");
    }
    Log("report (%s): %d live actors, %d of the actors and threads run page-overlay code", why, n, bad);
}
bool Ready() { return gReady; }
void SimType(const char* text) {
    std::string u;
    for (const char* q = text; *q; ++q) u += (q[0] == '\\' && q[1] == 'n') ? (++q, '\n') : *q;
    gKbScript.push_back({ gFrameNo + 1, u });
}
bool SimTyping() {
    while (gKbItem < gKbScript.size() && gKbStep >= gKbScript[gKbItem].second.size() * 4) { ++gKbItem; gKbStep = 0; }
    return gKbItem < gKbScript.size();
}
int OverlayTable(uint32_t* codes, uint32_t* fns, int max) {
    int n = 0;
    for (uint32_t a = 0x2DA698; n < max; a += 8) {
        uint32_t code = R32(a), fn = R32(a + 4);
        if (!code || !fn) break;
        codes[n] = code; fns[n] = fn; ++n;
    }
    return n;
}
void* CallCode(uint32_t fn, int name, int map) {
    if (!gReady) return nullptr;
    uint64_t r = 0;
    size_t before = gActors.size();
    Log("overlay call %08x (name %06x map %d)", fn, name & 0xffffff, map);
    uint32_t step = R32(kTimeStep);
    W32(kTimeStep, 5);
    bool ok = RunPs2(fn, (uint32_t)name, (uint32_t)map, &r);
    W32(kTimeStep, step);
    if (!ok) return nullptr;
    // a class returns its actor's handle ((serial << 32) | work >> 4, GV_SetNamedActor's result), a command an int
    for (size_t k = before; k < gActors.size(); ++k)
        if (gActors[k].live && (gActors[k].work == (uint32_t)r || R64(gActors[k].work + 0x18) == r)) return gActors[k].pc;
    return (void*)(intptr_t)(int32_t)r;
}
void* NewOnlineMenu(int name, int map) {
    if (!gReady) return nullptr;
    Log("NewOnlineMenu (recompiled 0x34a7b0) name %06x map %d", name & 0xffffff, map);
    uint64_t r = 0;
    uint32_t step = R32(kTimeStep);
    W32(kTimeStep, 5);
    bool ok = RunPs2(0x34a7b0, (uint32_t)name, (uint32_t)map, &r);
    W32(kTimeStep, step);
    if (!ok) return nullptr;
    // the constructor returns the actor's handle (GV_SetNamedActor's result), not its work
    for (auto& a : gActors) if (a.live && (a.work == (uint32_t)r || R64(a.work + 0x18) == r)) {
        Log("NewOnlineMenu -> work %08x, PC actor %p", a.work, a.pc);
        return a.pc;
    }
    Log("NewOnlineMenu failed (result %016llx)", (unsigned long long)r);
    return nullptr;
}
}  // namespace ps2world

// the M37's pump (docs/GAME_FIXES.md, recomp/variants.txt variant 5): the shotgun pumps after each shot
// (its fired state ends with the pump motion). Changing weapons during the pump and back skipped
// it: a shot of the local player's M37 while its last shot's pump has not ended pumps instead
// (the fire motion without the shot request), at 30 and at 60
namespace ps2 {
static std::map<uint32_t, bool> gM37Pumping;              // the local player's work: its M37 pump owed
uint64_t M37Fire(uint32_t player, uint32_t weapon, uint64_t request) {
    if (R32(weapon + 0x44) != 14) return request;                     // the M37
    auto it = ps2world::gNativeActors.find(player);
    if (it == ps2world::gNativeActors.end() || it->second != "NewNetworkDummyPlayer") return request;   // the local player only
    bool& owed = gM37Pumping[player];
    if (owed) { ps2world::Log("m37: a shot before the last shot's pump ended (weapon changed during it): pumped, no shot"); return 0; }
    owed = true;
    return request;
}
void M37Pumped(uint32_t player) { gM37Pumping.erase(player); }
// variant 6 (recomp/variants.txt): the saved network settings (ntmenu2 0x34AC08) with the game's default
// upstream bandwidth, 256 kbps: the PC's default, 4096
void UpstreamSaved(uint32_t settings) {
    const char* v = SelectedVersion();
    if (v && (!strcmp(v, "JP") || !strcmp(v, "JPS"))) return;   // no such setting there
    const uint32_t a = settings + 0xa;
    if (R16(a) != 256) return;
    W16(a, 4096);
    ps2world::Log("session: the saved upstream bandwidth was the game's default, 256 kbps: 4096 kbps");
}
void M37Reset() { gM37Pumping.clear(); }
}  // namespace ps2
