// vu_micro.h - the VU micro-mode interpreter's state and operations (vu0.cpp), shared with the
// recompiled microprograms (recomp/vu2cpp.py, gen/vu_prog_*.cpp) so both follow the same code
#pragma once
#include "ps2rt.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <emmintrin.h>
#if defined(_MSC_VER)
#define VU_INLINE __forceinline
#else
#define VU_INLINE inline __attribute__((always_inline))
#endif
namespace ps2 {
extern void (*gLog)(const char* fmt, ...);
namespace vux {
// the unit the operations act on: VU0 (macro mode, VIF0's programs), VU1 while a VIF1
// program runs; its memories wrap at 4 KB (VU0) or 16 KB (VU1)
extern Vu0* gCurVu;
#define V (*::ps2::vux::gCurVu)
inline bool IsVu1() { return gCurVu == &gVu1; }
inline uint32_t QMask() { return IsVu1() ? 0x3ff : 0xff; }     // quadword addresses
inline uint32_t PcMask() { return IsVu1() ? 0x3ff8 : 0xff8; } // micro addresses
VU_INLINE float F(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
VU_INLINE uint32_t U(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
// an operand as the VU reads it: denormals are zero, the exponent 255 values are large numbers
VU_INLINE float Op(uint32_t u) {
    switch (u & 0x7f800000) {
    case 0: return F(u & 0x80000000);
    case 0x7f800000: return F((u & 0x80000000) | 0x7f7fffff);
    }
    return F(u);
}
// one result lane: its MAC bits (Z S U O at shift 0 / 4 / 8 / 12 for w, +1 z, +2 y, +3 x)
inline uint32_t Mac(int lane, float f) {             // lane 0 x .. 3 w
    const int sh = 3 - lane;
    uint32_t v = U(f), s = v & 0x80000000;
    int e = (v >> 23) & 0xff;
    uint32_t& m = V.vi[kViMac];
    if (s) m |= 0x10u << sh; else m &= ~(0x10u << sh);
    if (f == 0) { m = (m & ~(0x1100u << sh)) | (0x1u << sh); return v; }
    if (e == 0) { m = (m & ~(0x1000u << sh)) | (0x0101u << sh); return s; }
    if (e == 255) { m = (m & ~(0x0101u << sh)) | (0x1000u << sh); return s | 0x7f7fffff; }
    m &= ~(0x1101u << sh);
    return v;
}
inline void MacClear(int lane) { V.vi[kViMac] &= ~(0x1111u << (3 - lane)); }
inline void StatusUpdate() {
    uint32_t m = V.vi[kViMac], n = 0;
    if (m & 0x000f) n |= 1;
    if (m & 0x00f0) n |= 2;
    if (m & 0x0f00) n |= 4;
    if (m & 0xf000) n |= 8;
    uint32_t& s = V.vi[kViStatus];
    s = (s & 0xff0) | n | (n << 6);                  // Z S U O, and their sticky copies
}
extern uint32_t gZero[4];                            // writes to vf0 land here
inline uint32_t* Dst(int fd) { return fd ? V.vf[fd] : gZero; }

enum class Kind { Add, Sub, Mul, Madd, Msub };
VU_INLINE float Apply(Kind k, uint32_t acc, uint32_t a, uint32_t b) {
    switch (k) {
    case Kind::Add: return Op(a) + Op(b);
    case Kind::Sub: return Op(a) - Op(b);
    case Kind::Mul: return Op(a) * Op(b);
    case Kind::Madd: return Op(acc) + Op(a) * Op(b);
    case Kind::Msub: return Op(acc) - Op(a) * Op(b);
    }
    return 0;
}
// fd (or ACC) = fs op ft (per lane) or fs op bc (one value for every lane); defined below, on four lanes at once
inline void Arith(Kind k, uint32_t* d, int dest, const uint32_t* fs, const uint32_t* ft, const uint32_t* bc);
// a result lane as Mac() leaves it, without the flags: the recompiled programs whose MAC and
// status flags nothing reads
VU_INLINE uint32_t Clamp(float f) {
    const uint32_t v = U(f);
    if (f == 0) return v;
    const uint32_t e = (v >> 23) & 0xff;
    if (e == 0) return v & 0x80000000u;
    if (e == 255) return (v & 0x80000000u) | 0x7f7fffffu;
    return v;
}
// Mac() on a MAC word of the caller's (the recompiled programs keep the issue-side MAC in a local)
VU_INLINE uint32_t MacR(uint32_t& m, int lane, float f) {
    const int sh = 3 - lane;
    uint32_t v = U(f), s = v & 0x80000000;
    int e = (v >> 23) & 0xff;
    if (s) m |= 0x10u << sh; else m &= ~(0x10u << sh);
    if (f == 0) { m = (m & ~(0x1100u << sh)) | (0x1u << sh); return v; }
    if (e == 0) { m = (m & ~(0x1000u << sh)) | (0x0101u << sh); return s; }
    if (e == 255) { m = (m & ~(0x0101u << sh)) | (0x1000u << sh); return s | 0x7f7fffff; }
    m &= ~(0x1101u << sh);
    return v;
}
// Arith with the MAC flags in m and no status flags (the recompiled VU1 programs: no VU1
// program reads the status flags)
inline void ArithM(uint32_t& m, Kind k, uint32_t* d, int dest, const uint32_t* fs, const uint32_t* ft, const uint32_t* bc) {
    uint32_t acc[4]; memcpy(acc, V.acc, 16);
    uint32_t a[4], b[4]; memcpy(a, fs, 16);
    if (bc) { b[0] = b[1] = b[2] = b[3] = *bc; } else memcpy(b, ft, 16);
    for (int l = 0; l < 4; ++l) {
        if (dest & (8 >> l)) d[l] = MacR(m, l, Apply(k, acc[l], a[l], b[l]));
        else m &= ~(0x1111u << (3 - l));
    }
}
// the same on VU1 itself (the recompiled VU1 programs name the unit, so its registers are
// plain addresses)
extern uint32_t gZero1[4];                           // VU1's writes to vf0 (the static VU1 programs run beside VU0's)
VU_INLINE uint32_t& VIr1(int n) { return gVu1.vi[n & 15]; }
VU_INLINE uint32_t* Dst1(int fd) { return fd ? gVu1.vf[fd] : gZero1; }
// VU0's by name (its static programs, recomp/vu2cpp.py to_vu0s)
VU_INLINE uint32_t& VIr0(int n) { return gVu0.vi[n & 15]; }
VU_INLINE uint32_t* Dst0(int fd) { return fd ? gVu0.vf[fd] : gZero; }
VU_INLINE void ArithM0(uint32_t& m, Kind k, uint32_t* d, int dest, const uint32_t* fs, const uint32_t* ft, const uint32_t* bc) {
    uint32_t acc[4]; memcpy(acc, gVu0.acc, 16);
    uint32_t a[4], b[4]; memcpy(a, fs, 16);
    if (bc) { b[0] = b[1] = b[2] = b[3] = *bc; } else memcpy(b, ft, 16);
    for (int l = 0; l < 4; ++l) {
        if (dest & (8 >> l)) d[l] = MacR(m, l, Apply(k, acc[l], a[l], b[l]));
        else m &= ~(0x1111u << (3 - l));
    }
}
// the Z S U O bits StatusUpdate makes of a MAC word (of an OR of MAC words: the OR of theirs)
VU_INLINE uint32_t MacNib(uint32_t m) { return (m & 0x000f ? 1u : 0u) | (m & 0x00f0 ? 2u : 0u) | (m & 0x0f00 ? 4u : 0u) | (m & 0xf000 ? 8u : 0u); }
VU_INLINE void ArithM1(uint32_t& m, Kind k, uint32_t* d, int dest, const uint32_t* fs, const uint32_t* ft, const uint32_t* bc) {
    uint32_t acc[4]; memcpy(acc, gVu1.acc, 16);
    uint32_t a[4], b[4]; memcpy(a, fs, 16);
    if (bc) { b[0] = b[1] = b[2] = b[3] = *bc; } else memcpy(b, ft, 16);
    for (int l = 0; l < 4; ++l) {
        if (dest & (8 >> l)) d[l] = MacR(m, l, Apply(k, acc[l], a[l], b[l]));
        else m &= ~(0x1111u << (3 - l));
    }
}
VU_INLINE void ArithV1(Kind k, uint32_t* d, int dest, const uint32_t* fs, const uint32_t* ft, const uint32_t* bc) {   // no flags
    uint32_t acc[4]; memcpy(acc, gVu1.acc, 16);
    uint32_t a[4], b[4]; memcpy(a, fs, 16);
    if (bc) { b[0] = b[1] = b[2] = b[3] = *bc; } else memcpy(b, ft, 16);
    for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) d[l] = Clamp(Apply(k, acc[l], a[l], b[l]));
}
// ---- the same arithmetic on the four lanes at once (SSE2), for the recompiled VU1 programs:
// operands as Op() reads them, results as Mac() leaves them, the MAC bits of Mac() (dest lanes;
// the others cleared). IEEE single precision like the scalar code, no fused multiply-add.
VU_INLINE __m128i ClampBitsV(__m128i v) {                    // exponent 0: signed zero; 255: signed max
    const __m128i sign = _mm_and_si128(v, _mm_set1_epi32((int)0x80000000));
    const __m128i e = _mm_and_si128(v, _mm_set1_epi32(0x7f800000));
    const __m128i z = _mm_cmpeq_epi32(e, _mm_setzero_si128());
    const __m128i o = _mm_cmpeq_epi32(e, _mm_set1_epi32(0x7f800000));
    const __m128i big = _mm_or_si128(sign, _mm_set1_epi32(0x7f7fffff));
    __m128i r = _mm_or_si128(_mm_and_si128(z, sign), _mm_andnot_si128(z, v));
    return _mm_or_si128(_mm_and_si128(o, big), _mm_andnot_si128(o, r));
}
VU_INLINE __m128 OpV(__m128i v) { return _mm_castsi128_ps(ClampBitsV(v)); }
VU_INLINE __m128 LoadOpV(const uint32_t* p) { return OpV(_mm_loadu_si128((const __m128i*)p)); }
VU_INLINE __m128 BcastOpV(uint32_t u) { return OpV(_mm_set1_epi32((int)u)); }
// MAC bits of the four raw results (lane x at bit 3, as dest), only for the lanes of dest
VU_INLINE uint32_t MacBitsV(__m128i v, int dest) {
    const __m128i r = _mm_shuffle_epi32(v, 0x1B);             // w z y x: movemask then gives x at bit 3
    const __m128i e = _mm_and_si128(r, _mm_set1_epi32(0x7f800000));
    const __m128i zexp = _mm_cmpeq_epi32(e, _mm_setzero_si128());
    const __m128i mant0 = _mm_cmpeq_epi32(_mm_and_si128(r, _mm_set1_epi32(0x007fffff)), _mm_setzero_si128());
    const __m128i ovf = _mm_cmpeq_epi32(e, _mm_set1_epi32(0x7f800000));
    const uint32_t zb = (uint32_t)_mm_movemask_ps(_mm_castsi128_ps(zexp));
    const uint32_t sb = (uint32_t)_mm_movemask_ps(_mm_castsi128_ps(r));
    const uint32_t ub = (uint32_t)_mm_movemask_ps(_mm_castsi128_ps(_mm_andnot_si128(mant0, zexp)));
    const uint32_t ob = (uint32_t)_mm_movemask_ps(_mm_castsi128_ps(ovf));
    const uint32_t d = (uint32_t)dest & 15;
    return (zb & d) | ((sb & d) << 4) | ((ub & d) << 8) | ((ob & d) << 12);
}
// the dest lanes of r into d (lane x is dest bit 3)
template <int DEST> VU_INLINE void StoreDestV(uint32_t* d, __m128i r) {
    if (DEST == 15) { _mm_storeu_si128((__m128i*)d, r); return; }
    const __m128i m = _mm_set_epi32((DEST & 1) ? -1 : 0, (DEST & 2) ? -1 : 0, (DEST & 4) ? -1 : 0, (DEST & 8) ? -1 : 0);
    const __m128i old = _mm_loadu_si128((const __m128i*)d);
    _mm_storeu_si128((__m128i*)d, _mm_or_si128(_mm_and_si128(m, r), _mm_andnot_si128(m, old)));
}
// Arith on the four lanes at once (ArithV's operations): the same results, the MAC bits of the dest lanes (the others
// cleared) as Mac() and MacClear() leave them, then the status
inline void Arith(Kind k, uint32_t* d, int dest, const uint32_t* fs, const uint32_t* ft, const uint32_t* bc) {
    const __m128 a = LoadOpV(fs), b = bc ? BcastOpV(*bc) : LoadOpV(ft);
    __m128 r;
    switch (k) {
    case Kind::Add: r = _mm_add_ps(a, b); break;
    case Kind::Sub: r = _mm_sub_ps(a, b); break;
    case Kind::Mul: r = _mm_mul_ps(a, b); break;
    case Kind::Madd: r = _mm_add_ps(LoadOpV(V.acc), _mm_mul_ps(a, b)); break;
    default: r = _mm_sub_ps(LoadOpV(V.acc), _mm_mul_ps(a, b)); break;
    }
    const __m128i ri = _mm_castps_si128(r);
    V.vi[kViMac] = MacBitsV(ri, dest);
    alignas(16) uint32_t out[4];
    _mm_store_si128((__m128i*)out, ClampBitsV(ri));
    for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) d[l] = out[l];
    StatusUpdate();
}
// fd (or ACC) = fs op b (b: a register, or one value for every lane), as Arith: K 0 add,
// 1 sub, 2 mul, 3 madd, 4 msub; MAC: the MAC bits into m
template <int K, int DEST, bool MAC> VU_INLINE void ArithV(uint32_t* d, const uint32_t* fs, __m128 b, uint32_t& m) {
    const __m128 a = LoadOpV(fs);
    __m128 r;
    if (K == 0) r = _mm_add_ps(a, b);
    else if (K == 1) r = _mm_sub_ps(a, b);
    else if (K == 2) r = _mm_mul_ps(a, b);
    else if (K == 3) r = _mm_add_ps(LoadOpV(gVu1.acc), _mm_mul_ps(a, b));
    else r = _mm_sub_ps(LoadOpV(gVu1.acc), _mm_mul_ps(a, b));
    const __m128i ri = _mm_castps_si128(r);
    if (MAC) m = MacBitsV(ri, DEST);
    if (d) StoreDestV<DEST>(d, ClampBitsV(ri));
}
// ArithV with the operands vu2cpp knows clean (no exponent 0 nor 255, so OpV leaves them as they are) read as
// they are: CA the first operand, CC the accumulator (the static form's)
VU_INLINE __m128 LoadV(const uint32_t* p) { return _mm_castsi128_ps(_mm_loadu_si128((const __m128i*)p)); }
VU_INLINE __m128 BcastV(uint32_t u) { return _mm_castsi128_ps(_mm_set1_epi32((int)u)); }
template <int K, int DEST, bool MAC, bool CA, bool CC> VU_INLINE void ArithVC(uint32_t* d, const uint32_t* fs, __m128 b, uint32_t& m) {
    const __m128 a = CA ? LoadV(fs) : LoadOpV(fs);
    __m128 r;
    if (K == 0) r = _mm_add_ps(a, b);
    else if (K == 1) r = _mm_sub_ps(a, b);
    else if (K == 2) r = _mm_mul_ps(a, b);
    else if (K == 3) r = _mm_add_ps(CC ? LoadV(gVu1.acc) : LoadOpV(gVu1.acc), _mm_mul_ps(a, b));
    else r = _mm_sub_ps(CC ? LoadV(gVu1.acc) : LoadOpV(gVu1.acc), _mm_mul_ps(a, b));
    const __m128i ri = _mm_castps_si128(r);
    if (MAC) m = MacBitsV(ri, DEST);
    if (d) StoreDestV<DEST>(d, ClampBitsV(ri));
}
// ArithV / ArithVC on VU0's accumulator (VU0's static programs)
template <int K, int DEST, bool MAC> VU_INLINE void ArithV0(uint32_t* d, const uint32_t* fs, __m128 b, uint32_t& m) {
    const __m128 a = LoadOpV(fs);
    __m128 r;
    if (K == 0) r = _mm_add_ps(a, b);
    else if (K == 1) r = _mm_sub_ps(a, b);
    else if (K == 2) r = _mm_mul_ps(a, b);
    else if (K == 3) r = _mm_add_ps(LoadOpV(gVu0.acc), _mm_mul_ps(a, b));
    else r = _mm_sub_ps(LoadOpV(gVu0.acc), _mm_mul_ps(a, b));
    const __m128i ri = _mm_castps_si128(r);
    if (MAC) m = MacBitsV(ri, DEST);
    if (d) StoreDestV<DEST>(d, ClampBitsV(ri));
}
template <int K, int DEST, bool MAC, bool CA, bool CC> VU_INLINE void ArithVC0(uint32_t* d, const uint32_t* fs, __m128 b, uint32_t& m) {
    const __m128 a = CA ? LoadV(fs) : LoadOpV(fs);
    __m128 r;
    if (K == 0) r = _mm_add_ps(a, b);
    else if (K == 1) r = _mm_sub_ps(a, b);
    else if (K == 2) r = _mm_mul_ps(a, b);
    else if (K == 3) r = _mm_add_ps(CC ? LoadV(gVu0.acc) : LoadOpV(gVu0.acc), _mm_mul_ps(a, b));
    else r = _mm_sub_ps(CC ? LoadV(gVu0.acc) : LoadOpV(gVu0.acc), _mm_mul_ps(a, b));
    const __m128i ri = _mm_castps_si128(r);
    if (MAC) m = MacBitsV(ri, DEST);
    if (d) StoreDestV<DEST>(d, ClampBitsV(ri));
}
VU_INLINE uint32_t FpMax(uint32_t a, uint32_t b) { return ((int32_t)a < 0 && (int32_t)b < 0) ? (uint32_t)std::min((int32_t)a, (int32_t)b) : (uint32_t)std::max((int32_t)a, (int32_t)b); }
VU_INLINE uint32_t FpMin(uint32_t a, uint32_t b) { return ((int32_t)a < 0 && (int32_t)b < 0) ? (uint32_t)std::max((int32_t)a, (int32_t)b) : (uint32_t)std::min((int32_t)a, (int32_t)b); }
VU_INLINE uint32_t FtoI(uint32_t u, int shift) {
    float f = F(u);
    if (shift) f *= F(0x3f800000 + ((uint32_t)shift << 23));
    uint32_t v = U(f);
    if ((v & 0x7f800000) >= 0x4f000000) return (v & 0x80000000) ? 0x80000000 : 0x7fffffff;
    return (uint32_t)(int32_t)f;
}
VU_INLINE uint32_t ItoF(uint32_t u, int shift) {
    float f = (float)(int32_t)u;
    if (shift) f *= F(0x3f800000 - ((uint32_t)shift << 23));
    return U(f);
}
inline uint8_t* Mem(uint32_t byteAddr) { return V.mem + (byteAddr & (QMask() << 4)); }
inline uint8_t* Mem1(uint32_t byteAddr) { return gVu1.mem + (byteAddr & (0x3ffu << 4)); }
inline uint8_t* Mem0(uint32_t byteAddr) { return gVu0.mem + (byteAddr & (0xffu << 4)); }
inline void Lfsr() {
    uint32_t& r = V.vi[kViR];
    int x = (r >> 4) & 1, y = (r >> 22) & 1;
    r <<= 1; r ^= x ^ y;
    r = (r & 0x7fffff) | 0x3f800000;
}
enum : uint32_t { kFlagStatus = 1u << 16, kFlagClip = 1u << 18 };
struct Slot { int64_t start; int regU, fieldsU, regL, fieldsL; uint32_t flagreg, mac, status, clip; };
struct Micro {
    int64_t cycle = 0;
    Slot slot[8]; int nslots = 0;                               // oldest first
    uint32_t mac = 0, status = 0, clip = 0;                     // issue side
    bool qBusy = false; int64_t qStart = 0, qCycles = 0; uint32_t qVal = 0, qStat = 0;
    int bkReg = -1; uint32_t bkVal = 0; int bkLeft = 0;
    struct Ialu { int64_t start; uint32_t reg; } ialu[4]; int nialu = 0;   // ILW / ILWR: 4 cycles
};
extern Micro gMs;
// the end address of the last program of each unit (MSCNT goes on from there)
extern uint32_t gTpc[2];
// VU1's EFU (P): its result after its latency, as the FDIV pipe for Q
struct Efu { bool busy = false; int64_t start = 0, cycles = 0; uint32_t val = 0; };
extern Efu gEfu;
VU_INLINE void CompleteP() { if (gEfu.busy) { V.vi[23] = gEfu.val; gEfu.busy = false; } }
// the static VU1 programs' own EFU and helpers (gVu1 by name): they run without the micro lock, beside VU0's programs
extern Efu gEfu1;
VU_INLINE void CompleteP1() { if (gEfu1.busy) { gVu1.vi[23] = gEfu1.val; gEfu1.busy = false; } }
void Vu1Lower1(int lf2, uint32_t lo, int lft, int lfs, int ldest, int fsf, uint32_t tag);
void Vu1Lower(int lf2, uint32_t lo, int lft, int lfs, int ldest, int fsf, uint32_t tag);
inline void CompleteSlot(const Slot& p) {
    if (p.flagreg & kFlagClip) V.vi[kViClip] = p.clip;
    uint32_t& st = V.vi[kViStatus];
    if (p.flagreg & kFlagStatus) st = (st & 0x30) | (p.status & 0xfc0) | (p.status & 0xf);
    else st = (st & 0xff0) | (p.status & 0xf) | ((p.status & 0xf) << 6);
    V.vi[kViMac] = p.mac;
}
inline void CompleteQ() {
    V.vi[kViQ] = gMs.qVal;
    uint32_t& st = V.vi[kViStatus];
    st = (st & 0xfcf) | (gMs.qStat & 0xc30);
    gMs.qBusy = false;
}
inline void TestPipes() {                                               // _vuTestPipes
    int k = 0;
    while (k < gMs.nslots && gMs.cycle - gMs.slot[k].start >= 4) CompleteSlot(gMs.slot[k++]);
    if (k) { for (int j = k; j < gMs.nslots; ++j) gMs.slot[j - k] = gMs.slot[j]; gMs.nslots -= k; }   // (no memmove call: msvcrt's under Wine)
    if (gMs.qBusy && gMs.cycle - gMs.qStart >= gMs.qCycles) CompleteQ();
    if (gEfu.busy && gMs.cycle - gEfu.start >= gEfu.cycles) CompleteP();
    int a = 0;
    while (a < gMs.nialu && gMs.cycle - gMs.ialu[a].start >= 4) ++a;
    if (a) { for (int j = a; j < gMs.nialu; ++j) gMs.ialu[j - a] = gMs.ialu[j]; gMs.nialu -= a; }
}
inline void FlushAll() {                                                // _vuFlushAll, at the program's end
    if (gMs.qBusy) CompleteQ();
    CompleteP();
    for (int k = 0; k < gMs.nslots; ++k) CompleteSlot(gMs.slot[k]);
    gMs.nslots = 0; gMs.nialu = 0;
}
// a branch reading an integer register an ILW / ILWR loaded less than 4 cycles ago waits
inline void StallIalu(uint32_t viread) {
    for (int k = 0; k < gMs.nialu; ++k)
        if (gMs.cycle - gMs.ialu[k].start < 4 && (gMs.ialu[k].reg & viread) && gMs.ialu[k].start + 4 > gMs.cycle)
            gMs.cycle = gMs.ialu[k].start + 4;
}
// a read of VF register reg (fields: x 8, y 4, z 2, w 1) that a slot still writes waits for it
inline void Stall(int reg, int fields) {
    if (!reg || !fields) return;
    for (int k = 0; k < gMs.nslots; ++k) {
        const Slot& p = gMs.slot[k];
        if (gMs.cycle - p.start >= 4) continue;
        if ((p.regU == reg && (p.fieldsU & fields)) || (p.regL == reg && (p.fieldsL & fields)))
            if (p.start + 4 > gMs.cycle) gMs.cycle = p.start + 4;
    }
}
struct Regs { int r0 = 0, f0 = 0, r1 = 0, f1 = 0, wr = 0, wf = 0; bool fmac = false; uint32_t viwrite = 0; };
inline int Fld(int f) { return 8 >> (f & 3); }                         // x y z w -> 8 4 2 1
inline Regs UpperRegs(uint32_t w) {
    Regs r;
    const int ft = (w >> 16) & 31, fs = (w >> 11) & 31, fd = (w >> 6) & 31, dest = (w >> 21) & 15;
    const int f1 = w & 0x3f;
    r.fmac = true;
    if (f1 < 0x3c) {
        r.r0 = fs; r.f0 = dest; r.wr = fd; r.wf = dest;
        if (f1 < 0x1c) { r.r1 = ft; r.f1 = Fld(w); }                                  // bc
        else if (f1 >= 0x28) { r.r1 = ft; r.f1 = dest; }
        if (f1 == 0x2e) { r.f0 = r.f1 = r.wf = 0xe; }                                // OPMSUB
        return r;
    }
    const int f2 = ((w >> 4) & 0x7c) | (w & 3);
    if (f2 == 0x2f) { r.fmac = false; return r; }                                    // NOP
    if ((f2 >= 0x10 && f2 <= 0x17) || f2 == 0x1d) { r.r0 = fs; r.f0 = dest; r.wr = ft; r.wf = dest; return r; }   // ITOF FTOI ABS
    if (f2 == 0x1f) { r.r0 = fs; r.f0 = 0xe; r.r1 = ft; r.f1 = 1; r.viwrite = kFlagClip; return r; }             // CLIP
    r.r0 = fs; r.f0 = dest;                                                          // the ACC forms
    if (f2 < 0x1c) { r.r1 = ft; r.f1 = Fld(w); }
    else if (f2 >= 0x28) { r.r1 = ft; r.f1 = dest; }
    if (f2 == 0x2e) r.f0 = r.f1 = 0xe;                                               // OPMULA
    return r;
}
inline uint32_t& VIr(int n) { return V.vi[n & 15]; }
inline int16_t Imm11(uint32_t w) { return (int16_t)((w & 0x400) ? (w | 0xf800) : (w & 0x7ff)); }
inline uint8_t* MemQ(uint32_t qaddr) {
    if (!IsVu1() && (qaddr & 0x400)) Once("VU0 micro access to VU1's registers (not served)", qaddr, 0);
    return V.mem + ((qaddr & QMask()) << 4);
}
inline uint8_t* MemQ0(uint32_t qaddr) {                                 // MemQ on VU0 (its static programs)
    if (qaddr & 0x400) Once("VU0 micro access to VU1's registers (not served)", qaddr, 0);
    return gVu0.mem + ((qaddr & 0xffu) << 4);
}
}  // namespace vux
// a run's place: the next instruction, a taken branch's target (jump: this instruction's,
// armed: the one after the delay slot), the E-bit countdown and the instructions run
struct MicroPos { uint32_t pc; int64_t jump, armed; int ebit; long steps; };
extern long gVuStepLimit;                                        // instructions a run may take (tests stop runs early)
void MicroLoop(MicroPos& P);                                     // the interpreter from P on
void Vu1HandOver(MicroPos& P, uint32_t iclip, uint32_t imac);   // a static VU1 run to the interpreter (the micro lock taken)
void Vu0HandOver(MicroPos& P);                                   // a static VU0 run entered at no entry of its: the interpreter
// a recompiled microprogram: its unit, entry, the instruction ranges its code covers and
// their FNV-1a hash (checked against the micro memory), and its code
// fast: 0 the full form, 1 VU1's fast form, 2 VU1's static form (MAC and status not kept: no VU1 program reads them),
// 3 VU0's static form (its flags kept: the EE reads them; with vu2cpp.py --vu0-static, the dedicated host's build, also
// the programs that read or set their status inside, kept instruction by instruction)
struct VuProgram { int unit; uint32_t entry; int nranges; const uint16_t (*ranges)[2]; uint64_t hash; void (*fn)(MicroPos&); int fast; };
extern const VuProgram kVuPrograms[];
extern const int kVuProgramCount;
}  // namespace ps2
