// iop_rt.h - what the recompiled IOP modules (gen/iop_*.cpp: Konami's sound library and sound
// driver) run on: the IOP's own 2 MB of memory, calls by address (a recompiled function, or a
// service of the IOP kernel iop.cpp stands in for), and the R3000's few helpers. The register
// file has the EE code's layout (the generator is the same); the upper halves stay the sign
// extension of the lower.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <climits>

namespace iop {
struct Ctx {
    uint64_t r[32];
    uint64_t rh[32];
    uint64_t hi, lo, sa;
    uint64_t hi1, lo1;
    float f[32];
    float acc;
    bool fcc;
};
typedef void (*Fn)(Ctx&);
struct FnEntry { uint32_t addr; Fn fn; };
struct ModuleInfo { const char* name; uint32_t base, size, entry; uint64_t hash; };
struct ImportStub { uint32_t addr; const char* lib; int index; };
struct ExportEntry { const char* lib; int index; uint32_t addr; };
extern const FnEntry kFunctions[];
extern const int kFunctionCount;
extern const ModuleInfo kModules[];
extern const int kModuleCount;
extern const ImportStub kImports[];
extern const int kImportCount;
extern const ExportEntry kExports[];
extern const int kExportCount;

// the IOP's memory: 2 MB, seen at 0x00000000 and through KSEG0 / KSEG1 (0x80000000, 0xA0000000)
constexpr uint32_t kMemSize = 0x200000;
extern uint8_t gMem[kMemSize + 64];
// hardware (0x1F800000 and up: the SPU2's registers, DMA, timers): the stand-ins' (iop.cpp)
uint64_t HwR(uint32_t a, int size);
void HwW(uint32_t a, uint64_t v, int size);
inline bool IsHw(uint32_t a) { return (a & 0x1fffffff) >= 0x1f800000; }
inline uint32_t Ph(uint32_t a) { return a & (kMemSize - 1); }
inline uint8_t R8(uint32_t a) { if (IsHw(a)) return (uint8_t)HwR(a, 1); return gMem[Ph(a)]; }
inline uint16_t R16(uint32_t a) { if (IsHw(a)) return (uint16_t)HwR(a, 2); uint16_t v; memcpy(&v, gMem + Ph(a), 2); return v; }
inline uint32_t R32(uint32_t a) { if (IsHw(a)) return (uint32_t)HwR(a, 4); uint32_t v; memcpy(&v, gMem + Ph(a), 4); return v; }
inline uint64_t R64(uint32_t a) { uint64_t v; memcpy(&v, gMem + Ph(a), 8); return v; }
inline void W8(uint32_t a, uint64_t v) { if (IsHw(a)) { HwW(a, v, 1); return; } gMem[Ph(a)] = (uint8_t)v; }
inline void W16(uint32_t a, uint64_t v) { if (IsHw(a)) { HwW(a, v, 2); return; } uint16_t x = (uint16_t)v; memcpy(gMem + Ph(a), &x, 2); }
inline void W32(uint32_t a, uint64_t v) { if (IsHw(a)) { HwW(a, v, 4); return; } uint32_t x = (uint32_t)v; memcpy(gMem + Ph(a), &x, 4); }
inline void W64(uint32_t a, uint64_t v) { memcpy(gMem + Ph(a), &v, 8); }
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
inline void SWL(uint32_t a, uint64_t rt) {
    uint32_t w = R32(a & ~3u); int sh = 8 * (3 - (a & 3));
    uint32_t m = sh ? ~(0xffffffffu >> sh) : 0;
    W32(a & ~3u, (w & m) | ((uint32_t)rt >> sh));
}
inline void SWR(uint32_t a, uint64_t rt) {
    uint32_t w = R32(a & ~3u); int sh = 8 * (a & 3);
    uint32_t m = sh ? (1u << sh) - 1 : 0;
    W32(a & ~3u, (w & m) | ((uint32_t)rt << sh));
}

// a call by address: a recompiled function, an import stub (the IOP kernel's service or another
// module's export), or a function this build did not recompile (logged, returns 0)
void Call(Ctx& c, uint32_t addr);
void Syscall(Ctx& c, uint32_t pc);
void Trap(Ctx& c, uint32_t pc);
bool TrapCond(Ctx& c, int fu, uint64_t a, uint64_t b);
void Unimpl(Ctx& c, uint32_t pc, uint32_t w);
inline uint64_t Cop0Status() { return 0x401; }
inline void Cop0Ei() {}
inline void Cop0Di() {}
// interrupts and preemption reach a running thread at its loops' backward branches (and at its
// kernel calls): gPoll is set while an interrupt waits or a thread became ready
extern std::atomic<int> gPoll;
void Poll();
#define BACKEDGE() do { if (gPoll.load(std::memory_order_relaxed)) Poll(); } while (0)
}  // namespace iop
