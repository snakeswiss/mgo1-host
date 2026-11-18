// ps2rt.h - runtime for the recompiled MGO1 ntmenu2 code: EE context,
// 32 MB of PS2 address space, unaligned accesses, calls.
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
namespace ps2 {
struct Ctx {
    uint64_t r[32];     // low 64 bits of the EE registers
    uint64_t rh[32];    // high 64 bits (lq/sq, MMI)
    uint64_t hi, lo, sa;
    uint64_t hi1, lo1;  // pipeline 1 (MULT1, DIV1, MFHI1 ...)
    float f[32];
    float acc;
    bool fcc;
};
// a recompiled function is a shape (recomp/mips2shape.py) run for an address: the shape's body
// reads the relocated fields of its instructions from the instance's own code
typedef void (*ShapeFn)(Ctx&, uint32_t);
struct Fn {
    ShapeFn fn = nullptr; uint32_t a = 0;
    void operator()(Ctx& c) const { fn(c, a); }
    explicit operator bool() const { return fn != nullptr; }
};
struct FnEntry { uint32_t addr; ShapeFn fn; };
// a shape's fields (W: the instance's first instruction in PS2 memory, A: its address)
#define WPTR(a) (::ps2::gMem + ((a) & 0x01ffffffu))
#define IMM(o) ((int32_t)*(const int16_t*)(W + (o)))
#define UIMM(o) ((uint32_t)*(const uint16_t*)(W + (o)))
#define HI(o) ((uint32_t)*(const uint16_t*)(W + (o)) << 16)
#define TGT(o) (((A + (o)) & 0xf0000000u) | ((*(const uint32_t*)(W + (o)) & 0x3ffffffu) << 2))
// page overlays built on their own (mips2cpp --select overlay --prefix <page>): each
// registers its table; the code from 0x2DA680 up runs from the selected page's table,
// as the PS2 stage loader puts one page's overlay at that address
// a page's copies of the libraries ntmenu2 links (tools/page_alias.py): the platform
// serves them at ntmenu2's addresses
struct AliasEntry { uint32_t addr; uint32_t served; };
// the tables of each version of the program (docs/VERSIONS.md): its base ("base") and its pages
struct OverlayRegistration { OverlayRegistration(const char* version, const char* page, const FnEntry* fns, int count,
                                                 const AliasEntry* alias = nullptr, int nalias = 0); };
bool SelectVersion(const char* version);    // its base table, its pages from gPageBase up (false: none built)
const char* SelectedVersion();
int BaseFunctionCount();                    // the selected version's base functions
extern uint32_t gPageBase;                  // where the selected version's pages load (US 0x2DA680)
bool SelectOverlay(const char* page);       // false when no table of that page was built
const char* SelectedOverlay();              // null when none is selected
// another version against the US program the platform is written for (tools/versions.py): the
// functions of both (same: the same shape, so an address inside one is at the same offset), the
// runs of data moved by one delta
struct VersionPair { uint32_t us, v, usLen, vLen, same; };
struct VersionImageMap { const char* page; uint32_t usLo, vLo; const VersionPair* pairs; int count; };
struct VersionRun { uint32_t usLo, usHi; int32_t delta; uint32_t votes; };
struct VersionMapRegistration { VersionMapRegistration(const char* version, uint32_t gpUs, uint32_t gpV, const VersionImageMap* images,
                                                       int nImages, const VersionRun* runs, int nRuns); };
// the selected version's address of a US one (code of the base or of the page `page`, or data), and
// the US address of one of the selected version's; 0 when there is none (the US version: the same)
uint32_t VerAddr(uint32_t us, const char* page = nullptr);
uint32_t UsAddr(uint32_t a, const char* page = nullptr);
uint32_t UsFnOf(uint32_t a, const char* page = nullptr);   // the US start of the function holding a (0: none)
bool IsUsVersion();
bool IsFn(uint32_t addr);                   // a recompiled function at that address (current page)
Fn FindFn(uint32_t addr);                   // that function, or null
uint32_t PageAlias(uint32_t addr);          // ntmenu2's address of the selected page's copy, or 0

extern uint8_t* gMem;                 // 32 MB, PS2 physical addresses 0x00000000..0x01ffffff
// window onto host objects (layout nodes of the PC engine): accesses in
// [gWinLo, gWinHi) are served by the platform, not by gMem
extern uint32_t gWinLo, gWinHi;
uint64_t WinR(uint32_t a, int size);
void WinW(uint32_t a, uint64_t v, int size);
extern uint32_t gWatchLo, gWatchHi;
void WatchHit(uint32_t p, int size, bool write, uint64_t v);
extern uint32_t gCursorWin;
extern uint32_t gStageWin;            // the engine's stage flags word (4 bytes), served by the platform
// COP0 Status as the code sees it: di / ei toggle EIE (bit 16)
extern uint32_t gEie;
inline uint64_t Cop0Status() { return gEie ? 0x70030c13ull : 0x70020c13ull; }
inline void Cop0Di() { gEie = 0; }
inline void Cop0Ei() { gEie = 1; }
inline bool InWin(uint32_t p) {
    if (p >= gWatchLo && p < gWatchHi) WatchHit(p, 0, false, 0);
    return (p >= gWinLo && p < gWinHi) || p == gCursorWin || (p & ~3u) == gStageWin;
}
// PS2 hardware (0x1xxxxxxx: EE registers and FIFOs, VU memory, GS registers) is served
// by the runtime's stand-ins, never by gMem: masked, it would alias the scratchpad
// (0x70000000, gMem[0..0x4000)) and RAM (VU memory 0x11004000 -> 0x01004000)
uint64_t HwR(uint32_t a, int size);
void HwW(uint32_t a, uint64_t v, int size);
inline bool IsHw(uint32_t a) { return __builtin_expect((a >> 28) == 1, 0); }
inline uint8_t* P(uint32_t a) { return gMem + (a & 0x01ffffff); }
inline uint8_t R8(uint32_t a) { if (IsHw(a)) return (uint8_t)HwR(a, 1); uint32_t p = a & 0x01ffffff; if (InWin(p)) return (uint8_t)WinR(p, 1); return gMem[p]; }
inline uint16_t R16(uint32_t a) { if (IsHw(a)) return (uint16_t)HwR(a, 2); uint32_t p = a & 0x01ffffff; if (InWin(p)) return (uint16_t)WinR(p, 2); uint16_t v; memcpy(&v, gMem + p, 2); return v; }
inline uint32_t R32(uint32_t a) { if (IsHw(a)) return (uint32_t)HwR(a, 4); uint32_t p = a & 0x01ffffff; if (InWin(p)) return (uint32_t)WinR(p, 4); uint32_t v; memcpy(&v, gMem + p, 4); return v; }
inline uint64_t R64(uint32_t a) { if (IsHw(a)) return HwR(a, 8); uint32_t p = a & 0x01ffffff; if (InWin(p)) return WinR(p, 8); uint64_t v; memcpy(&v, gMem + p, 8); return v; }
inline void W8(uint32_t a, uint64_t v) { if (IsHw(a)) { HwW(a, v, 1); return; } uint32_t p = a & 0x01ffffff; if (InWin(p)) { WinW(p, v, 1); return; } gMem[p] = (uint8_t)v; }
inline void W16(uint32_t a, uint64_t v) { if (IsHw(a)) { HwW(a, v, 2); return; } uint32_t p = a & 0x01ffffff; if (InWin(p)) { WinW(p, v, 2); return; } uint16_t x = (uint16_t)v; memcpy(gMem + p, &x, 2); }
inline void W32(uint32_t a, uint64_t v) { if (IsHw(a)) { HwW(a, v, 4); return; } uint32_t p = a & 0x01ffffff; if (InWin(p)) { WinW(p, v, 4); return; } uint32_t x = (uint32_t)v; memcpy(gMem + p, &x, 4); }
inline void W64(uint32_t a, uint64_t v) { if (IsHw(a)) { HwW(a, v, 8); return; } uint32_t p = a & 0x01ffffff; if (InWin(p)) { WinW(p, v, 8); return; } memcpy(gMem + p, &v, 8); }

inline uint64_t LWL(uint32_t a, uint64_t rt) {
    uint32_t w = R32(a & ~3u); int sh = 8 * (3 - (a & 3));
    uint32_t m = sh ? (1u << sh) - 1 : 0;
    return (uint64_t)(int64_t)(int32_t)(((uint32_t)rt & m) | (w << sh));
}
inline uint64_t LWR(uint32_t a, uint64_t rt) {
    uint32_t w = R32(a & ~3u); int sh = 8 * (a & 3);
    uint32_t m = sh ? ~(0xffffffffu >> sh) : 0;
    return (uint64_t)(int64_t)(int32_t)(((uint32_t)rt & m) | (w >> sh));
}
inline uint64_t LDL(uint32_t a, uint64_t rt) {
    uint64_t w = R64(a & ~7u); int sh = 8 * (7 - (a & 7));
    uint64_t m = sh ? (1ull << sh) - 1 : 0;
    return (rt & m) | (w << sh);
}
inline uint64_t LDR(uint32_t a, uint64_t rt) {
    uint64_t w = R64(a & ~7u); int sh = 8 * (a & 7);
    uint64_t m = sh ? ~(~0ull >> sh) : 0;
    return (rt & m) | (w >> sh);
}
inline void SWL(uint32_t a, uint64_t rt) {
    uint32_t al = a & ~3u, w = R32(al); int sh = 8 * (3 - (a & 3));
    uint32_t keep = sh ? ~(0xffffffffu >> sh) : 0;
    W32(al, (w & keep) | ((uint32_t)rt >> sh));
}
inline void SWR(uint32_t a, uint64_t rt) {
    uint32_t al = a & ~3u, w = R32(al); int sh = 8 * (a & 3);
    uint32_t keep = sh ? (1u << sh) - 1 : 0;
    W32(al, (w & keep) | ((uint32_t)rt << sh));
}
inline void SDL(uint32_t a, uint64_t rt) {
    uint32_t al = a & ~7u; uint64_t w = R64(al); int sh = 8 * (7 - (a & 7));
    uint64_t keep = sh ? ~(~0ull >> sh) : 0;
    W64(al, (w & keep) | (rt >> sh));
}
inline void SDR(uint32_t a, uint64_t rt) {
    uint32_t al = a & ~7u; uint64_t w = R64(al); int sh = 8 * (a & 7);
    uint64_t keep = sh ? (1ull << sh) - 1 : 0;
    W64(al, (w & keep) | (rt << sh));
}
inline void LQ(Ctx& c, int rt, uint32_t a) { if (rt) { c.r[rt] = R64(a & ~15u); c.rh[rt] = R64((a & ~15u) + 8); } }
inline void SQ(Ctx& c, int rt, uint32_t a) { W64(a & ~15u, c.r[rt]); W64((a & ~15u) + 8, c.rh[rt]); }

inline uint32_t FBits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
inline float BitsF(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
// The EE FPU's divide and square roots (as PCSX2's interpreter): a divisor of 0 or a denormal gives the largest
// float with the sign of the two operands' signs XORed; SQRT.S and RSQRT.S take the root of the absolute value (the
// I flag for a negative operand, no NaN), SQRT.S of 0 or a denormal keeps the sign. With the C library's sqrtf a
// root of a value just under 0 (1 - cos of two parallel vectors) was NaN: the made women's bust bone (US n003a's
// spring 0x4C3450 through 0x348230) then held +-FLT_MAX, its vertices thrown across the screen (the spikes)
// The PS2's FPU has no infinity and no NaN either: a result past the largest float is that float, its sign kept
// (FC: an IEEE infinity would never leave a loop that counts down a length: the host's hang of 27 September,
// 0x159d88 walking the collision grid along a ray of infinite length)
inline float FC(float x) {
    uint32_t u; memcpy(&u, &x, 4);
    if (__builtin_expect((u & 0x7f800000u) == 0x7f800000u, 0)) return (u >> 31) ? -3.4028235e38f : 3.4028235e38f;
    return x;
}
inline float FDiv(float a, float b) {
    const uint32_t ua = FBits(a), ub = FBits(b);
    if ((ub & 0x7F800000u) == 0) return BitsF(((ua ^ ub) & 0x80000000u) | 0x7F7FFFFFu);
    return FC(a / b);
}
inline float FSqrt(float a) {
    const uint32_t u = FBits(a);
    if ((u & 0x7F800000u) == 0) return BitsF(u & 0x80000000u);
    return sqrtf(fabsf(a));
}
inline float FRsqrt(float a, float b) {
    const uint32_t ub = FBits(b);
    if ((ub & 0x7F800000u) == 0) return BitsF((ub & 0x80000000u) | 0x7F7FFFFFu);
    return FC(a / sqrtf(fabsf(b)));
}
inline int32_t FToW(float f) {
    if (f >= 2147483520.0f) return 0x7fffffff;
    if (f <= -2147483648.0f) return (int32_t)0x80000000;
    return (int32_t)f;
}
// VU0 in macro mode (COP2), vu0.cpp: registers as raw float bits, x y z w per vf
enum { kViStatus = 16, kViMac = 17, kViClip = 18, kViR = 20, kViI = 21, kViQ = 22, kViCmsar0 = 27, kViFbrst = 28, kViVpuStat = 29 };
struct Vu0 {
    uint32_t vf[32][4];
    uint32_t acc[4];
    uint32_t vi[32];            // 0-15 integer registers (16 bit), then the control registers (P at 23)
    uint8_t mem[16384];         // data memory (VU0: 4 KB at EE 0x11004000; VU1: 16 KB at 0x1100C000)
    uint8_t micro[16384];       // micro memory (VU0: 4 KB at EE 0x11000000; VU1: 16 KB at 0x11008000)
};
extern Vu0 gVu0;
extern Vu0 gVu1;                 // VU1, run in micro mode only (VIF1 MSCAL / MSCNT)
void Vu1Micro(uint32_t addr);    // a VU1 microprogram run to its end (its XGKICKs reach the GIF)
extern uint32_t gMicroGen[2];    // VU0 / VU1 micro memory generations (every write bumps them)
void Vif1Write(const uint32_t* words, size_t n);   // VIF1 packet data (its DMA channel)
void Vif1Reset();
// the GIF (gs.cpp): packets of PATH1 (VU1 XGKICK, from VU1 memory), PATH2 (VIF1 DIRECT) and
// PATH3 (the GIF DMA channel); the GS registers and its local memory
void GifPath(int path, const uint8_t* data, size_t qwords, size_t wrapBytes = 0);
void GsPrivWrite(uint32_t addr, uint64_t v, int size);
uint64_t GsPrivRead(uint32_t addr, int size);
void GsSetCrt(int interlace, int mode, int ffmd);                  // the SetGsCrt syscall
void GsVSync();                  // a VSync (native build): CSR VSINT, the field, the GS interrupt unless masked
bool GsDisplayed(std::vector<uint32_t>& rgba, int& w, int& h);   // the frame the display circuits show
uint64_t GsDisplayFlips();      // changes of DISPFB1 / DISPFB2 so far (the game's frame swaps)
std::string GsPcrtc();           // the display circuits' registers (PMODE, DISPFB1/2, DISPLAY1/2, BGCOLOR), for a log
bool GsWritePng(const char* path);
std::string GsStats();           // GS work since the last call (log)
bool GsDumpBegin(const char* path);   // the GS state, then its packets until GsDumpEnd (tests/gs_replay.cpp)
void GsDumpEnd();
bool GsReplay(const char* path, long long& packets, long long& writes);
extern bool gGsDraw;             // false: registers and transfers only, no rasterization
void GsSetThreads(int n);        // drawing threads before the first primitive: 0 = the caller's, -1 = the processor count (up to 8)
struct GsPrimInfo {              // one primitive about to be drawn (the replay tool's draw log)
    int type; uint64_t prim, frame, zbuf, tex0, tex1, clamp, alpha, test, fba, texa;
    float x0, y0, x1, y1, u[3], v[3], q[3]; uint32_t rgba[3];
    float xs[3], ys[3]; uint32_t zs[3];   // the corners' places and depths (SIM_SPIKE, gs_replay --prims)
};
extern void (*gGsPrimHook)(const GsPrimInfo&);
std::vector<uint32_t> GsFrameRgba(uint64_t frame, int w, int h);   // a frame buffer read back (replay tool)
bool GsPngOf(const char* path, const std::vector<uint32_t>& px, int w, int h);
// a pixel's address in the GS memory in units of its storage mode's pixel (4 bits for PSMT4, 32 for PSMCT32)
uint32_t GsPixelAddress(uint32_t psm, int x, int y, uint32_t bp, uint32_t bw);
extern uint32_t gTreeRoot; extern int gTreeLines; extern bool gTreeArmed;   // SIM_CALLTREE
extern void (*gCallHook)(uint32_t addr);   // every call while set (diagnostics)
// interrupts raised by the hardware stand-ins, delivered at the next call boundary while
// interrupts are enabled (the platform runs the handlers): bits 0-9 DMAC channels done,
// bit 16 a GS event (SIGNAL, FINISH) not masked by IMR
extern uint32_t gIrqPending;
extern void (*gIrqDeliver)();
extern bool gInIrq;
extern bool gGsReal;             // the GS reports its own events (DG runs); else CSR reads them raised
extern int gGsTrace;             // > 0: DMA tags, VIF commands, GIF tags and GS registers logged (lines left)
void GsTraceLog(const char* fmt, ...);
void Vu0Reset();
void Vu0Macro(Ctx& c, uint32_t w, uint32_t pc);
void Vu0CallMs(Ctx& c, uint32_t addr);
void Vu0Micro(uint32_t addr);                // a microprogram run to its end (VCALLMS, VIF0 MSCAL)
void Vif0Write(const uint32_t* words, size_t n);   // VIF0 packet data (its DMA channel)
void Vif0Reset();
uint32_t Vu0Cfc2(int reg);
void Vu0Ctc2(int reg, uint32_t v);
inline void Lqc2(int ft, uint32_t a) {
    uint32_t b = a & ~15u;
    uint32_t v[4] = {R32(b), R32(b + 4), R32(b + 8), R32(b + 12)};
    if (ft) memcpy(gVu0.vf[ft], v, 16);
}
inline void Sqc2(int ft, uint32_t a) {
    uint32_t b = a & ~15u;
    W32(b, gVu0.vf[ft][0]); W32(b + 4, gVu0.vf[ft][1]); W32(b + 8, gVu0.vf[ft][2]); W32(b + 12, gVu0.vf[ft][3]);
}
inline void Qmfc2(Ctx& c, int rt, int fs) {
    if (!rt) return;
    c.r[rt] = gVu0.vf[fs][0] | (uint64_t)gVu0.vf[fs][1] << 32;
    c.rh[rt] = gVu0.vf[fs][2] | (uint64_t)gVu0.vf[fs][3] << 32;
}
inline void Qmtc2(Ctx& c, int rt, int fs) {
    if (!fs) return;
    gVu0.vf[fs][0] = (uint32_t)c.r[rt]; gVu0.vf[fs][1] = (uint32_t)(c.r[rt] >> 32);
    gVu0.vf[fs][2] = (uint32_t)c.rh[rt]; gVu0.vf[fs][3] = (uint32_t)(c.rh[rt] >> 32);
}
void Once(const char* what, uint32_t pc, uint32_t w);   // logged once per (what, pc)
bool TrapCond(Ctx& c, int fu, uint64_t a, uint64_t b);
void Trap(Ctx& c, uint32_t pc);
void Syscall(Ctx& c, uint32_t pc);                 // a syscall instruction in recompiled code ($v1 the number)
extern void (*gSyscall)(Ctx& c, uint32_t pc);      // the platform's; none: a trap
void Unimpl(Ctx& c, uint32_t pc, uint32_t w);
void MMI(Ctx& c, uint32_t pc, uint32_t w);
void Call(Ctx& c, uint32_t addr);
// guard for one run of recompiled code: past `budget` calls, Call() longjmps
// to *jb (set by the caller with setjmp)
struct Budget { long long left; void* jb; void (*yield)(); long long refill; };   // yield: preempt instead of abort
extern thread_local Budget* gBudget;       // per OS thread: PS2 threads run on their own
// loops without calls count against the same budget (checked every 4096 back edges)
extern uint32_t gBackEdges;
void BackEdgeCheck();
// (in a shape's body: the function whose loop it is, for the watchdog)
extern volatile uint32_t gLoopFn;
extern Ctx* volatile gLoopCtx;             // (the registers of the loop, for the log of a loop that never ends)
#define BACKEDGE() do { ps2::gLoopFn = A; ps2::gLoopCtx = &c; if (((++ps2::gBackEdges) & 0xfff) == 0) ps2::BackEdgeCheck(); } while (0)
}  // namespace ps2
