// vu0.cpp - the EE's VU0 in macro mode (COP2) for the recompiled code: the vector
// unit's registers, its arithmetic with the MAC / status / clipping flags, and its
// data memory. The operations follow PCSX2's interpreter (VUops.cpp, VUflags.cpp):
// operands with exponent 0 read as signed zero, results that overflow saturate at
// +-0x7F7FFFFF, DIV / SQRT / RSQRT set the I and D flags and saturate as the PS2 does.
#include "gs_thread.h"
#include "vu_micro.h"
#include "gs_gpu.h"
#include <chrono>
#include <atomic>
#include <thread>
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <string>

namespace ps2 {

Vu0 gVu0;
Vu0 gVu1;
namespace vux {
Vu0* gCurVu = &gVu0;
uint32_t gZero[4], gZero1[4];
Micro gMs;
uint32_t gTpc[2] = {0, 0};
Efu gEfu;
Efu gEfu1;
}  // namespace vux
using namespace vux;
long gVuStepLimit = 4000000;
// the unit the operations below act on: VU0 (macro mode, VIF0's programs), VU1 while a VIF1
// program runs; its memories wrap at 4 KB (VU0) or 16 KB (VU1)

void Vu0Reset() {
    for (Vu0* u : {&gVu0, &gVu1}) {
        memset(u, 0, sizeof(*u));
        u->vf[0][3] = 0x3f800000;                  // vf0 = (0, 0, 0, 1)
        u->vi[kViR] = 0x3f800000;
    }
}

// the EE's own view of VU0 (CFC2, CTC2, the macro instructions): VU0 itself, whatever unit a microprogram runs
// on (the GS thread runs VU1's beside the game thread)
#undef V
#define V (::ps2::gVu0)

uint32_t Vu0Cfc2(int reg) {
    if (reg < 16) return reg ? V.vi[reg] & 0xffff : 0;
    if (reg == kViR) return V.vi[kViR] & 0x7fffff;
    if (reg == kViVpuStat) return 0;                 // VU0 idle: microprograms run to their end at once
    return V.vi[reg];
}
void Vu0Ctc2(int reg, uint32_t v) {
    if (reg == 0) return;
    if (reg < 16) { V.vi[reg] = v & 0xffff; return; }
    switch (reg) {
    case kViMac: case 26: case kViVpuStat: return;   // read-only
    case kViR: V.vi[kViR] = (v & 0x7fffff) | 0x3f800000; return;
    case kViStatus: V.vi[kViStatus] = (V.vi[kViStatus] & 0x3f) | (v & 0xfc0); return;
    case kViFbrst:
        if (v & 2) { GsSync(); uint8_t micro[sizeof V.micro]; memcpy(micro, V.micro, sizeof micro); Vu0Reset(); memcpy(V.micro, micro, sizeof micro); }
        V.vi[kViFbrst] = v & 0x0c0c; return;
    }
    V.vi[reg] = v;
}

// COP2 with the CO bit: VU0 macro instructions (special1, special2 when funct is 0x3C..0x3F), also the micro-mode
// upper and integer instructions. These helpers take the unit (vu_micro.h's act on gCurVu, VU1 on the GS thread).
namespace {
inline uint32_t MMac(Vu0& u, int lane, float f) {
    const int sh = 3 - lane;
    uint32_t v = U(f), s = v & 0x80000000;
    int e = (v >> 23) & 0xff;
    uint32_t& m = u.vi[kViMac];
    if (s) m |= 0x10u << sh; else m &= ~(0x10u << sh);
    if (f == 0) { m = (m & ~(0x1100u << sh)) | (0x1u << sh); return v; }
    if (e == 0) { m = (m & ~(0x1000u << sh)) | (0x0101u << sh); return s; }
    if (e == 255) { m = (m & ~(0x0101u << sh)) | (0x1000u << sh); return s | 0x7f7fffff; }
    m &= ~(0x1101u << sh);
    return v;
}
inline void MMacClear(Vu0& u, int lane) { u.vi[kViMac] &= ~(0x1111u << (3 - lane)); }
inline void MStatusUpdate(Vu0& u) {
    uint32_t m = u.vi[kViMac], n = 0;
    if (m & 0x000f) n |= 1;
    if (m & 0x00f0) n |= 2;
    if (m & 0x0f00) n |= 4;
    if (m & 0xf000) n |= 8;
    uint32_t& s = u.vi[kViStatus];
    s = (s & 0xff0) | n | (n << 6);
}
uint32_t gMZero[2][4];                                       // writes to vf0 land here (a sink per unit)
inline uint32_t* MDst(Vu0& u, int fd) { return fd ? u.vf[fd] : gMZero[&u == &gVu1]; }
// on the four lanes at once (vu_micro.h's ArithV): the same results, MAC (dest lanes, the others cleared) and status
inline void MArith(Vu0& u, Kind k, uint32_t* d, int dest, const uint32_t* fs, const uint32_t* ft, const uint32_t* bc) {
    const __m128 a = LoadOpV(fs), b = bc ? BcastOpV(*bc) : LoadOpV(ft);
    __m128 r;
    switch (k) {
    case Kind::Add: r = _mm_add_ps(a, b); break;
    case Kind::Sub: r = _mm_sub_ps(a, b); break;
    case Kind::Mul: r = _mm_mul_ps(a, b); break;
    case Kind::Madd: r = _mm_add_ps(LoadOpV(u.acc), _mm_mul_ps(a, b)); break;
    default: r = _mm_sub_ps(LoadOpV(u.acc), _mm_mul_ps(a, b)); break;
    }
    const __m128i ri = _mm_castps_si128(r);
    const uint32_t m = MacBitsV(ri, dest);
    u.vi[kViMac] = m;
    const __m128i c = ClampBitsV(ri);
    alignas(16) uint32_t out[4]; _mm_store_si128((__m128i*)out, c);
    for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) d[l] = out[l];
    MStatusUpdate(u);
}
inline uint8_t* MMem(Vu0& u, uint32_t byteAddr) { return u.mem + (byteAddr & ((&u == &gVu1 ? 0x3ffu : 0xffu) << 4)); }   // 4 KB, VU1 16 KB
inline void MLfsr(Vu0& u) {
    uint32_t& r = u.vi[kViR];
    int x = (r >> 4) & 1, y = (r >> 22) & 1;
    r <<= 1; r ^= x ^ y;
    r = (r & 0x7fffff) | 0x3f800000;
}
}  // namespace
#undef V
#define V (u)
namespace {
void MacroOn(Vu0& u, Ctx& c, uint32_t w, uint32_t pc) {
    const int ft = (w >> 16) & 31, fs = (w >> 11) & 31, fd = (w >> 6) & 31, dest = (w >> 21) & 15;
    const int fsf = (w >> 21) & 3, ftf = (w >> 23) & 3;
    uint32_t* vf = &V.vf[0][0];
    auto VF = [&](int n) { return &vf[n * 4]; };
    auto VI = [&](int n) -> uint32_t& { return V.vi[n & 15]; };
    const uint32_t* bcx = &V.vf[ft][w & 3];
    const uint32_t* bcq = &V.vi[kViQ];
    const uint32_t* bci = &V.vi[kViI];
    const int f1 = w & 0x3f;
    if (f1 < 0x3c) {
        switch (f1) {
        case 0x00: case 0x01: case 0x02: case 0x03: MArith(u, Kind::Add, MDst(u, fd), dest, VF(fs), nullptr, bcx); return;
        case 0x04: case 0x05: case 0x06: case 0x07: MArith(u, Kind::Sub, MDst(u, fd), dest, VF(fs), nullptr, bcx); return;
        case 0x08: case 0x09: case 0x0a: case 0x0b: MArith(u, Kind::Madd, MDst(u, fd), dest, VF(fs), nullptr, bcx); return;
        case 0x0c: case 0x0d: case 0x0e: case 0x0f: MArith(u, Kind::Msub, MDst(u, fd), dest, VF(fs), nullptr, bcx); return;
        case 0x10: case 0x11: case 0x12: case 0x13:            // MAXbc
            if (fd) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[fd][l] = FpMax(V.vf[fs][l], *bcx);
            return;
        case 0x14: case 0x15: case 0x16: case 0x17:            // MINIbc
            if (fd) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[fd][l] = FpMin(V.vf[fs][l], *bcx);
            return;
        case 0x18: case 0x19: case 0x1a: case 0x1b: MArith(u, Kind::Mul, MDst(u, fd), dest, VF(fs), nullptr, bcx); return;
        case 0x1c: MArith(u, Kind::Mul, MDst(u, fd), dest, VF(fs), nullptr, bcq); return;
        case 0x1d: if (fd) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[fd][l] = FpMax(V.vf[fs][l], *bci); return;
        case 0x1e: MArith(u, Kind::Mul, MDst(u, fd), dest, VF(fs), nullptr, bci); return;
        case 0x1f: if (fd) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[fd][l] = FpMin(V.vf[fs][l], *bci); return;
        case 0x20: MArith(u, Kind::Add, MDst(u, fd), dest, VF(fs), nullptr, bcq); return;
        case 0x21: MArith(u, Kind::Madd, MDst(u, fd), dest, VF(fs), nullptr, bcq); return;
        case 0x22: MArith(u, Kind::Add, MDst(u, fd), dest, VF(fs), nullptr, bci); return;
        case 0x23: MArith(u, Kind::Madd, MDst(u, fd), dest, VF(fs), nullptr, bci); return;
        case 0x24: MArith(u, Kind::Sub, MDst(u, fd), dest, VF(fs), nullptr, bcq); return;
        case 0x25: MArith(u, Kind::Msub, MDst(u, fd), dest, VF(fs), nullptr, bcq); return;
        case 0x26: MArith(u, Kind::Sub, MDst(u, fd), dest, VF(fs), nullptr, bci); return;
        case 0x27: MArith(u, Kind::Msub, MDst(u, fd), dest, VF(fs), nullptr, bci); return;
        case 0x28: MArith(u, Kind::Add, MDst(u, fd), dest, VF(fs), VF(ft), nullptr); return;
        case 0x29: MArith(u, Kind::Madd, MDst(u, fd), dest, VF(fs), VF(ft), nullptr); return;
        case 0x2a: MArith(u, Kind::Mul, MDst(u, fd), dest, VF(fs), VF(ft), nullptr); return;
        case 0x2b: if (fd) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[fd][l] = FpMax(V.vf[fs][l], V.vf[ft][l]); return;
        case 0x2c: MArith(u, Kind::Sub, MDst(u, fd), dest, VF(fs), VF(ft), nullptr); return;
        case 0x2d: MArith(u, Kind::Msub, MDst(u, fd), dest, VF(fs), VF(ft), nullptr); return;
        case 0x2e: {                                           // OPMSUB (xyz)
            uint32_t* d = MDst(u, fd);
            float tx = Op(V.vf[ft][0]), ty = Op(V.vf[ft][1]), tz = Op(V.vf[ft][2]);
            float sx = Op(V.vf[fs][0]), sy = Op(V.vf[fs][1]), sz = Op(V.vf[fs][2]);
            uint32_t acc[4]; memcpy(acc, V.acc, 16);
            d[0] = MMac(u, 0, Op(acc[0]) - sy * tz);
            d[1] = MMac(u, 1, Op(acc[1]) - sz * tx);
            d[2] = MMac(u, 2, Op(acc[2]) - sx * ty);
            MStatusUpdate(u);
            return; }
        case 0x2f: if (fd) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[fd][l] = FpMin(V.vf[fs][l], V.vf[ft][l]); return;
        case 0x30: if (fd & 15) VI(fd) = (uint16_t)((int16_t)VI(fs) + (int16_t)VI(ft)); return;   // IADD id = is + it
        case 0x31: if (fd & 15) VI(fd) = (uint16_t)((int16_t)VI(fs) - (int16_t)VI(ft)); return;   // ISUB
        case 0x32: {                                           // IADDI it = is + imm5
            int16_t imm = (int16_t)((fd & 0x10) ? (0xfff0 | (fd & 0xf)) : (fd & 0xf));
            if (ft & 15) VI(ft) = (uint16_t)((int16_t)VI(fs) + imm);
            return; }
        case 0x34: if (fd & 15) VI(fd) = VI(fs) & VI(ft) & 0xffff; return;                        // IAND
        case 0x35: if (fd & 15) VI(fd) = (VI(fs) | VI(ft)) & 0xffff; return;                      // IOR
        case 0x38: Vu0CallMs(c, ((w >> 6) & 0x7fff) * 8); return;                                  // VCALLMS
        case 0x39: Vu0CallMs(c, (V.vi[kViCmsar0] & 0xffff) * 8); return;                       // VCALLMSR
        }
        Once("VU0 macro instruction not implemented", pc, w);
        return;
    }
    const int f2 = ((w >> 4) & 0x7c) | (w & 3);
    const uint32_t* bc2 = &V.vf[ft][w & 3];
    switch (f2) {
    case 0x00: case 0x01: case 0x02: case 0x03: MArith(u, Kind::Add, V.acc, dest, VF(fs), nullptr, bc2); return;
    case 0x04: case 0x05: case 0x06: case 0x07: MArith(u, Kind::Sub, V.acc, dest, VF(fs), nullptr, bc2); return;
    case 0x08: case 0x09: case 0x0a: case 0x0b: MArith(u, Kind::Madd, V.acc, dest, VF(fs), nullptr, bc2); return;
    case 0x0c: case 0x0d: case 0x0e: case 0x0f: MArith(u, Kind::Msub, V.acc, dest, VF(fs), nullptr, bc2); return;
    case 0x10: case 0x11: case 0x12: case 0x13: {              // ITOF0 / 4 / 12 / 15
        static const int sh[4] = {0, 4, 12, 15};
        if (ft) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = ItoF(V.vf[fs][l], sh[f2 & 3]);
        return; }
    case 0x14: case 0x15: case 0x16: case 0x17: {              // FTOI0 / 4 / 12 / 15
        static const int sh[4] = {0, 4, 12, 15};
        if (ft) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = FtoI(V.vf[fs][l], sh[f2 & 3]);
        return; }
    case 0x18: case 0x19: case 0x1a: case 0x1b: MArith(u, Kind::Mul, V.acc, dest, VF(fs), nullptr, bc2); return;
    case 0x1c: MArith(u, Kind::Mul, V.acc, dest, VF(fs), nullptr, bcq); return;
    case 0x1d: if (ft) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = V.vf[fs][l] & 0x7fffffff; return;   // ABS
    case 0x1e: MArith(u, Kind::Mul, V.acc, dest, VF(fs), nullptr, bci); return;
    case 0x1f: {                                               // CLIPw.xyz
        int32_t v = (int32_t)V.vf[ft][3];
        v = (v & 0x7f800000) ? (v & 0x7fffffff) : 0x007fffff;
        uint32_t& cf = V.vi[kViClip];
        cf <<= 6;
        for (int l = 0; l < 3; ++l) {
            if ((int32_t)(V.vf[fs][l] ^ 0x00000000) > v) cf |= 1u << (2 * l);
            if ((int32_t)(V.vf[fs][l] ^ 0x80000000) > v) cf |= 2u << (2 * l);
        }
        cf &= 0xffffff;
        return; }
    case 0x20: MArith(u, Kind::Add, V.acc, dest, VF(fs), nullptr, bcq); return;
    case 0x21: MArith(u, Kind::Madd, V.acc, dest, VF(fs), nullptr, bcq); return;
    case 0x22: MArith(u, Kind::Add, V.acc, dest, VF(fs), nullptr, bci); return;
    case 0x23: MArith(u, Kind::Madd, V.acc, dest, VF(fs), nullptr, bci); return;
    case 0x24: MArith(u, Kind::Sub, V.acc, dest, VF(fs), nullptr, bcq); return;
    case 0x25: MArith(u, Kind::Msub, V.acc, dest, VF(fs), nullptr, bcq); return;
    case 0x26: MArith(u, Kind::Sub, V.acc, dest, VF(fs), nullptr, bci); return;
    case 0x27: MArith(u, Kind::Msub, V.acc, dest, VF(fs), nullptr, bci); return;
    case 0x28: MArith(u, Kind::Add, V.acc, dest, VF(fs), VF(ft), nullptr); return;
    case 0x29: MArith(u, Kind::Madd, V.acc, dest, VF(fs), VF(ft), nullptr); return;
    case 0x2a: MArith(u, Kind::Mul, V.acc, dest, VF(fs), VF(ft), nullptr); return;
    case 0x2c: MArith(u, Kind::Sub, V.acc, dest, VF(fs), VF(ft), nullptr); return;
    case 0x2d: MArith(u, Kind::Msub, V.acc, dest, VF(fs), VF(ft), nullptr); return;
    case 0x2e: {                                               // OPMULA (xyz)
        float sx = Op(V.vf[fs][0]), sy = Op(V.vf[fs][1]), sz = Op(V.vf[fs][2]);
        float tx = Op(V.vf[ft][0]), ty = Op(V.vf[ft][1]), tz = Op(V.vf[ft][2]);
        V.acc[0] = MMac(u, 0, sy * tz);
        V.acc[1] = MMac(u, 1, sz * tx);
        V.acc[2] = MMac(u, 2, sx * ty);
        MStatusUpdate(u);
        return; }
    case 0x2f: return;                                         // NOP
    case 0x30: if (ft) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = V.vf[fs][l]; return;   // MOVE
    case 0x31: {                                               // MR32
        if (!ft) return;
        uint32_t s[4]; memcpy(s, V.vf[fs], 16);
        for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = s[(l + 1) & 3];
        return; }
    case 0x34: {                                               // LQI ft, (is++)
        if (ft) { const uint8_t* m = MMem(u, VI(fs) * 16); for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) memcpy(&V.vf[ft][l], m + 4 * l, 4); }
        if (fs & 15) VI(fs) = (VI(fs) + 1) & 0xffff;
        return; }
    case 0x35: {                                               // SQI fs, (it++)
        uint8_t* m = MMem(u, VI(ft) * 16);
        for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) memcpy(m + 4 * l, &V.vf[fs][l], 4);
        if (ft & 15) VI(ft) = (VI(ft) + 1) & 0xffff;
        return; }
    case 0x36: {                                               // LQD ft, (--is)
        if (fs & 15) VI(fs) = (VI(fs) - 1) & 0xffff;
        if (ft) { const uint8_t* m = MMem(u, VI(fs) * 16); for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) memcpy(&V.vf[ft][l], m + 4 * l, 4); }
        return; }
    case 0x37: {                                               // SQD fs, (--it)
        if (ft & 15) VI(ft) = (VI(ft) - 1) & 0xffff;
        uint8_t* m = MMem(u, VI(ft) * 16);
        for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) memcpy(m + 4 * l, &V.vf[fs][l], 4);
        return; }
    case 0x38: {                                               // DIV Q, fs.fsf, ft.ftf
        uint32_t ub = V.vf[ft][ftf], ua = V.vf[fs][fsf];
        float b = Op(ub), a = Op(ua);
        uint32_t& st = V.vi[kViStatus];
        st &= ~0x30u;
        if (b == 0.0f) {
            st |= (a == 0.0f) ? 0x410 : 0x820;
            V.vi[kViQ] = ((ub ^ ua) & 0x80000000) ? 0xff7fffff : 0x7f7fffff;
        } else V.vi[kViQ] = U(Op(U(a / b)));
        return; }
    case 0x39: {                                               // SQRT Q, ft.ftf
        float b = Op(V.vf[ft][ftf]);
        uint32_t& st = V.vi[kViStatus];
        st &= ~0x30u;
        if (b < 0.0f) st |= 0x410;
        V.vi[kViQ] = U(Op(U(sqrtf(fabsf(b)))));
        return; }
    case 0x3a: {                                               // RSQRT Q, fs.fsf, ft.ftf
        uint32_t ub = V.vf[ft][ftf], ua = V.vf[fs][fsf];
        float b = Op(ub), a = Op(ua);
        uint32_t& st = V.vi[kViStatus];
        st &= ~0x30u;
        if (b == 0.0f) {
            st |= 0x820;
            if (a != 0.0f) V.vi[kViQ] = ((ub ^ ua) & 0x80000000) ? 0xff7fffff : 0x7f7fffff;
            else { V.vi[kViQ] = ((ub ^ ua) & 0x80000000) ? 0x80000000 : 0; st |= 0x410; }
        } else {
            if (b < 0.0f) st |= 0x410;
            V.vi[kViQ] = U(Op(U(a / sqrtf(fabsf(b)))));
        }
        return; }
    case 0x3b: return;                                         // WAITQ
    case 0x3c: if (ft & 15) VI(ft) = V.vf[fs][fsf] & 0xffff; return;              // MTIR it, fs.fsf
    case 0x3d: if (ft) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = (uint32_t)(int32_t)(int16_t)VI(fs); return;   // MFIR
    case 0x3e: {                                               // ILWR it, (is)
        if (!(ft & 15)) return;
        const uint8_t* m = MMem(u, VI(fs) * 16);
        for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) { uint16_t h; memcpy(&h, m + 4 * l, 2); VI(ft) = h; }
        return; }
    case 0x3f: {                                               // ISWR it, (is)
        uint8_t* m = MMem(u, VI(fs) * 16);
        uint32_t v = VI(ft) & 0xffff;
        for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) memcpy(m + 4 * l, &v, 4);
        return; }
    case 0x40: MLfsr(u); if (ft) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = V.vi[kViR]; return;   // RNEXT
    case 0x41: if (ft) for (int l = 0; l < 4; ++l) if (dest & (8 >> l)) V.vf[ft][l] = V.vi[kViR]; return;          // RGET
    case 0x42: V.vi[kViR] = 0x3f800000 | (V.vf[fs][fsf] & 0x007fffff); return;                                     // RINIT
    case 0x43: V.vi[kViR] = 0x3f800000 | ((V.vi[kViR] ^ V.vf[fs][fsf]) & 0x007fffff); return;                  // RXOR
    }
    Once("VU0 macro instruction not implemented", pc, w);
}
}  // namespace
#undef V
#define V (::ps2::gVu0)
// a microprogram's instruction (its tag 0xF0000000 | micro address) on the unit it runs on, the EE's on VU0
void Vu0Macro(Ctx& c, uint32_t w, uint32_t pc) {
    if ((pc >> 28) == 0xF) MacroOn(*vux::gCurVu, c, w, pc);
    else MacroOn(gVu0, c, w, pc);
}

#undef V
#define V (*::ps2::vux::gCurVu)

// ---- VU0 micro mode: the microprograms of the game's VIF0 uploads (MPG), run by VCALLMS /
// VCALLMSR (and VIF0's MSCAL) to their end before the EE goes on, as its interlocked
// CFC2.I / QMFC2.I waits make it. Upper and lower instructions reuse the macro-mode
// operations above. The pipeline follows PCSX2's VU0 interpreter (VU0microInterp.cpp,
// VUops.cpp), which the map collision microprograms depend on:
// - flags have an issue side (mac, status, clip: what the operations update) and a visible
//   side (V.vi: what FMAND / FSAND / FCAND read). Each instruction pair with an FMAC-pipe
//   instruction takes a pipeline slot holding the issue-side flags after the pair; 4 cycles
//   later the slot completes: MAC = its mac; status ZSUO = its ZSUO and the sticky bits get
//   them too, or the sticky bits become its own when the pair held FSSET; clip = its clip when
//   the pair held CLIP or FCSET;
// - a read of a VF register (by field) an FMAC slot still writes stalls until it completes;
//   the accumulator does not stall;
// - DIV / SQRT write Q 7 cycles later, RSQRT 13, with their D / I status bits; WAITQ and the
//   next DIV-family instruction wait for it;
// - the lower instruction reads a register the upper one of the pair writes before that
//   write, and is dropped when both write the same register;
// - a branch reads an integer register's old value when an integer op, MTIR or an
//   incrementing load / store wrote it in the instruction just before (PCSX2's VI backup);
// - a branch lands after its delay slot, the program ends one instruction after the E bit.

// a run's place: the next instruction, a taken branch's target (jump: this instruction's,
// armed: the one after the delay slot), the E-bit countdown and the instructions run
void MicroBegin() {
    gMs = Micro();
    gEfu = Efu();
    gMs.mac = V.vi[kViMac]; gMs.status = V.vi[kViStatus]; gMs.clip = V.vi[kViClip];
}
long gVuLastSteps = 0;                                           // instructions of the last run (tests)
void MicroEnd(const MicroPos& P, uint32_t addr) {
    gVuLastSteps = P.steps;
    if (P.steps >= 4000000) Once(IsVu1() ? "VU1 microprogram without an end (stopped)" : "VU0 microprogram without an end (stopped)", addr, 0);
    gTpc[IsVu1() ? 1 : 0] = P.pc;
    gMs.bkLeft = 0;
    FlushAll();
}
// the interpreter from P on (a program start, or where a recompiled program hands over)
void MicroLoop(MicroPos& P) {
    uint32_t pc = P.pc;
    int64_t jump = P.jump, armed = P.armed;                      // a taken branch: its target, after the delay slot
    int ebit = P.ebit;
    Ctx dummy{};
    long steps = P.steps;
    for (; steps < gVuStepLimit; ++steps) {
        ++gMs.cycle;
        uint32_t lo, hi;
        memcpy(&lo, V.micro + pc, 4); memcpy(&hi, V.micro + pc + 4, 4);
        const uint32_t tag = 0xF0000000u | pc;                   // logs: a micro address
        if (hi & 0x40000000u) ebit = 2;
        const bool imm = (hi & 0x80000000u) != 0;
        const int64_t before = gMs.cycle - 1;
        const Regs ur = UpperRegs(hi);
        Stall(ur.r0, ur.f0); Stall(ur.r1, ur.f1);
        // the lower instruction's registers and pipe
        const int lop = lo >> 25;
        const int lft = (lo >> 16) & 31, lfs = (lo >> 11) & 31, ldest = (lo >> 21) & 15;
        const int fsf = (lo >> 21) & 3, ftf = (lo >> 23) & 3;
        int lf1 = -1, lf2 = -1;
        Regs lr;
        bool fdivPipe = false, efuPipe = false;
        uint32_t branchRead = 0; bool branch = false, ialuLoad = false;
        if (!imm) {
            if (lop == 0x40) { lf1 = lo & 0x3f; if (lf1 >= 0x3c) lf2 = ((lo >> 4) & 0x7c) | (lo & 3); }
            switch (lop) {
            case 0x00: lr.fmac = true; lr.wr = lft; lr.wf = ldest; break;                       // LQ
            case 0x01: lr.fmac = true; lr.r0 = lfs; lr.f0 = ldest; break;                        // SQ
            case 0x10: case 0x12: case 0x13: case 0x14: case 0x16: case 0x17: case 0x18: case 0x1a: case 0x1b: case 0x1c:
                lr.fmac = true; break;                                                          // flag tests
            case 0x11: lr.fmac = true; lr.viwrite = kFlagClip; break;                            // FCSET
            case 0x15: lr.fmac = true; lr.viwrite = kFlagStatus; break;                          // FSSET
            case 0x40:
                switch (lf2) {
                case 0x30: if (lft) { lr.fmac = true; lr.r0 = lfs; lr.f0 = ldest; lr.wr = lft; lr.wf = ldest; } break;   // MOVE
                case 0x31: lr.fmac = true; lr.r0 = lfs; lr.f0 = ((ldest >> 1) | ((ldest << 3) & 8)); lr.wr = lft; lr.wf = ldest; break;   // MR32
                case 0x34: case 0x36: lr.fmac = true; lr.wr = lft; lr.wf = ldest; break;          // LQI LQD
                case 0x35: case 0x37: lr.fmac = true; lr.r0 = lfs; lr.f0 = ldest; break;          // SQI SQD
                case 0x38: case 0x3a: fdivPipe = true; lr.r0 = lfs; lr.f0 = Fld(fsf); lr.r1 = lft; lr.f1 = Fld(ftf); break;   // DIV RSQRT
                case 0x39: fdivPipe = true; lr.r1 = lft; lr.f1 = Fld(ftf); break;                 // SQRT
                case 0x3b: fdivPipe = true; break;                                                // WAITQ
                case 0x3c: lr.fmac = true; lr.r0 = lfs; lr.f0 = Fld(fsf); break;                  // MTIR
                case 0x3d: case 0x40: case 0x41: lr.fmac = true; lr.wr = lft; lr.wf = ldest; break;   // MFIR RNEXT RGET
                case 0x42: case 0x43: lr.fmac = true; lr.r0 = lfs; lr.f0 = Fld(fsf); break;       // RINIT RXOR
                case 0x64: lr.fmac = true; lr.wr = lft; lr.wf = ldest; break;                     // MFP (VU1)
                case 0x70: case 0x71: case 0x72: case 0x73: case 0x76:                            // ESADD ERSADD ELENG ERLENG ESUM
                    efuPipe = true; lr.r0 = lfs; lr.f0 = lf2 == 0x76 ? 0xf : 0xe; break;
                case 0x74: efuPipe = true; lr.r0 = lfs; lr.f0 = 0xc; break;                        // EATANxy
                case 0x75: efuPipe = true; lr.r0 = lfs; lr.f0 = 0xa; break;                        // EATANxz
                case 0x78: case 0x79: case 0x7a: case 0x7c: case 0x7d: case 0x7e:                 // ESQRT ERSQRT ERCPR ESIN EATAN EEXP
                    efuPipe = true; lr.r0 = lfs; lr.f0 = Fld(fsf); break;
                case 0x7b: efuPipe = true; break;                                                  // WAITP
                }
                break;
            }
            switch (lop) {
            case 0x20: case 0x21: branch = true; break;                                                  // B BAL
            case 0x24: case 0x25: branch = true; branchRead = 1u << (lfs & 15); break;                    // JR JALR
            case 0x28: case 0x29: branch = true; branchRead = (1u << (lfs & 15)) | (1u << (lft & 15)); break;   // IBEQ IBNE
            case 0x2c: case 0x2d: case 0x2e: case 0x2f: branch = true; branchRead = 1u << (lfs & 15); break;   // IBLTZ IBGTZ IBLEZ IBGEZ
            case 0x04: ialuLoad = true; break;                                                            // ILW
            case 0x40: if (lf2 == 0x3e) ialuLoad = true; break;                                           // ILWR
            }
            Stall(lr.r0, lr.f0); Stall(lr.r1, lr.f1);
            if (branch) StallIalu(branchRead);
            if (fdivPipe && gMs.qBusy && gMs.qStart + gMs.qCycles > gMs.cycle) gMs.cycle = gMs.qStart + gMs.qCycles;
            if (efuPipe && gEfu.busy && gEfu.start + gEfu.cycles > gMs.cycle) gMs.cycle = gEfu.start + gEfu.cycles;
        }
        TestPipes();
        if (gMs.bkLeft > 0) gMs.bkLeft -= (int)std::min<int64_t>(gMs.cycle - before, gMs.bkLeft);
        // upper, on the issue-side flags
        const uint32_t macVis = V.vi[kViMac], stVis = V.vi[kViStatus], clipVis = V.vi[kViClip];
        const bool lowerReadsUpperDst = !imm && ur.wr > 0 && (lr.r0 == ur.wr || lr.r1 == ur.wr);
        const bool lowerDropped = !imm && ((ur.wr > 0 && lr.wr == ur.wr) || ((ur.viwrite & kFlagClip) && lop == 0x11));   // same VF, or CLIP + FCSET
        uint32_t oldVf[4] = {0, 0, 0, 0};
        if (lowerReadsUpperDst) memcpy(oldVf, V.vf[ur.wr], 16);
        V.vi[kViMac] = gMs.mac; V.vi[kViStatus] = gMs.status; V.vi[kViClip] = gMs.clip;
        Vu0Macro(dummy, 0x4A000000u | (hi & 0x01ffffffu), tag);
        gMs.mac = V.vi[kViMac]; gMs.status = V.vi[kViStatus]; gMs.clip = V.vi[kViClip];
        V.vi[kViMac] = macVis; V.vi[kViStatus] = stVis; V.vi[kViClip] = clipVis;
        if (imm) {
            V.vi[kViI] = lo;                                  // the lower word is I's new value
        } else if (!lowerDropped) {
            uint32_t newVf[4];
            if (lowerReadsUpperDst) { memcpy(newVf, V.vf[ur.wr], 16); memcpy(V.vf[ur.wr], oldVf, 16); }
            bool viWrite = false; int viReg = 0; uint32_t viOld = 0;
            auto backup = [&](int r) { viWrite = true; viReg = r & 15; viOld = VIr(r) & 0xffff; };
            auto BR = [&](int r) -> int16_t { r &= 15; if (gMs.bkLeft > 0 && gMs.bkReg == r) return (int16_t)gMs.bkVal; return r ? (int16_t)VIr(r) : 0; };
            const uint32_t link = (((armed >= 0 ? (uint32_t)armed : pc + 8) + 8) >> 3) & 0xffff;   // in a delay slot: the target's next
            const uint32_t next = (pc + 8 + (uint32_t)((int32_t)Imm11(lo) * 8)) & PcMask();
            switch (lop) {
            case 0x00: {                                                         // LQ ft, imm(is)
                const uint8_t* m = MemQ((uint32_t)((int16_t)VIr(lfs) + Imm11(lo)));
                if (lft) for (int l = 0; l < 4; ++l) if (ldest & (8 >> l)) memcpy(&V.vf[lft][l], m + 4 * l, 4);
                break; }
            case 0x01: {                                                         // SQ fs, imm(it)
                uint8_t* m = MemQ((uint32_t)((int16_t)VIr(lft) + Imm11(lo)));
                for (int l = 0; l < 4; ++l) if (ldest & (8 >> l)) memcpy(m + 4 * l, &V.vf[lfs][l], 4);
                break; }
            case 0x04: {                                                         // ILW it, imm(is)
                const uint8_t* m = MemQ((uint32_t)((int16_t)VIr(lfs) + Imm11(lo)));
                if (lft & 15) for (int l = 0; l < 4; ++l) if (ldest & (8 >> l)) { uint16_t h; memcpy(&h, m + 4 * l, 2); VIr(lft) = h; }
                break; }
            case 0x05: {                                                         // ISW it, imm(is)
                uint8_t* m = MemQ((uint32_t)((int16_t)VIr(lfs) + Imm11(lo)));
                const uint32_t v = VIr(lft) & 0xffff;
                for (int l = 0; l < 4; ++l) if (ldest & (8 >> l)) memcpy(m + 4 * l, &v, 4);
                break; }
            case 0x08: case 0x09: {                                              // IADDIU / ISUBIU it = is +- imm15
                const uint32_t i15 = ((lo >> 10) & 0x7800) | (lo & 0x7ff);
                if (lft & 15) { backup(lft); VIr(lft) = (lop == 0x08 ? VIr(lfs) + i15 : VIr(lfs) - i15) & 0xffff; }   // vi0: no backup
                break; }
            case 0x10: VIr(1) = ((V.vi[kViClip] & 0xffffff) == (lo & 0xffffff)) ? 1 : 0; break;            // FCEQ
            case 0x11: gMs.clip = lo & 0xffffff; break;                                                        // FCSET
            case 0x12: VIr(1) = (V.vi[kViClip] & lo & 0xffffff) ? 1 : 0; break;                           // FCAND
            case 0x13: VIr(1) = (((V.vi[kViClip] | lo) & 0xffffff) == 0xffffff) ? 1 : 0; break;             // FCOR
            case 0x14: case 0x15: case 0x16: case 0x17: {                                                       // FSEQ FSSET FSAND FSOR
                const uint32_t i12 = ((lo >> 10) & 0x800) | (lo & 0x7ff);
                const uint32_t st = V.vi[kViStatus];
                if (lop == 0x15) { gMs.status = (i12 & 0xfc0) | (gMs.status & 0x3f); break; }
                if (!(lft & 15)) break;
                if (lop == 0x14) VIr(lft) = ((st & 0xfff) == i12) ? 1 : 0;
                else if (lop == 0x16) VIr(lft) = st & i12 & 0xfff;
                else VIr(lft) = (st | i12) & 0xfff;
                break; }
            case 0x18: if (lft & 15) VIr(lft) = ((V.vi[kViMac] & 0xffff) == (VIr(lfs) & 0xffff)) ? 1 : 0; break;   // FMEQ
            case 0x1a: if (lft & 15) VIr(lft) = V.vi[kViMac] & VIr(lfs) & 0xffff; break;                           // FMAND
            case 0x1b: if (lft & 15) VIr(lft) = (V.vi[kViMac] | VIr(lfs)) & 0xffff; break;                          // FMOR
            case 0x1c: if (lft & 15) VIr(lft) = V.vi[kViClip] & 0xfff; break;                                        // FCGET
            case 0x20: jump = next; break;                                                                              // B
            case 0x21: if (lft & 15) VIr(lft) = link; jump = next; break;                                              // BAL
            case 0x24: jump = ((VIr(lfs) & 0xffff) * 8) & PcMask(); break;                                              // JR
            case 0x25: { const uint32_t t = ((VIr(lfs) & 0xffff) * 8) & PcMask(); if (lft & 15) VIr(lft) = link; jump = t; break; }   // JALR
            case 0x28: if (BR(lft) == BR(lfs)) jump = next; break;                                                      // IBEQ
            case 0x29: if (BR(lft) != BR(lfs)) jump = next; break;                                                      // IBNE
            case 0x2c: if (BR(lfs) < 0) jump = next; break;                                                             // IBLTZ
            case 0x2d: if (BR(lfs) > 0) jump = next; break;                                                             // IBGTZ
            case 0x2e: if (BR(lfs) <= 0) jump = next; break;                                                            // IBLEZ
            case 0x2f: if (BR(lfs) >= 0) jump = next; break;                                                            // IBGEZ
            case 0x40:
                if (lf2 == 0x38 || lf2 == 0x39 || lf2 == 0x3a) {                 // DIV / SQRT / RSQRT: Q later
                    const uint32_t q0 = V.vi[kViQ], st0 = V.vi[kViStatus];
                    V.vi[kViStatus] = gMs.status;
                    Vu0Macro(dummy, 0x4A000000u | (lo & 0x01ffffffu), tag | 4);
                    gMs.qVal = V.vi[kViQ]; gMs.status = V.vi[kViStatus]; gMs.qStat = gMs.status;
                    V.vi[kViQ] = q0; V.vi[kViStatus] = st0;
                    gMs.qBusy = true; gMs.qStart = gMs.cycle; gMs.qCycles = lf2 == 0x3a ? 13 : 7;
                } else if (lf2 == 0x3b) {                                        // WAITQ: waited above
                } else if (lf2 >= 0x64) {                                        // MFP XTOP XITOP XGKICK and the EFU: VU1's
                    if (!IsVu1()) { Once("VU1 instruction in a VU0 microprogram (ignored)", tag, lo); break; }
                    Vu1Lower(lf2, lo, lft, lfs, ldest, fsf, tag);
                } else {
                    if (lf1 == 0x30 || lf1 == 0x31 || lf1 == 0x34 || lf1 == 0x35) { if ((lo >> 6) & 15) backup((int)((lo >> 6) & 15)); }   // IADD ISUB IAND IOR
                    else if (lf1 == 0x32 || lf2 == 0x3c) { if (lft & 15) backup(lft); }                             // IADDI MTIR
                    else if (lf2 == 0x34 || lf2 == 0x36) backup(lfs);                                               // LQI LQD
                    else if (lf2 == 0x35 || lf2 == 0x37) backup(lft);                                               // SQI SQD
                    Vu0Macro(dummy, 0x4A000000u | (lo & 0x01ffffffu), tag | 4);
                }
                break;
            default:
                Once("VU0 micro lower instruction not implemented", tag, lo);
            }
            if (lowerReadsUpperDst) memcpy(V.vf[ur.wr], newVf, 16);
            if (viWrite) {                                               // repeat writes keep the value from before the chain
                if (!(gMs.bkLeft > 0 && gMs.bkReg == viReg)) { gMs.bkReg = viReg; gMs.bkVal = viOld; }
                gMs.bkLeft = 2;
            }
        }
        if (ialuLoad) {
            if (gMs.nialu == 4) { memmove(gMs.ialu, gMs.ialu + 1, 3 * sizeof(gMs.ialu[0])); --gMs.nialu; }
            gMs.ialu[gMs.nialu++] = {gMs.cycle, 1u << (lft & 15)};
        }
        // a pipeline slot for the pair (upper and lower FMAC-pipe instructions share it)
        if (ur.fmac || lr.fmac) {
            if (gMs.nslots == 8) { CompleteSlot(gMs.slot[0]); memmove(gMs.slot, gMs.slot + 1, 7 * sizeof(Slot)); --gMs.nslots; }
            Slot& p = gMs.slot[gMs.nslots++];
            p = Slot{};
            p.start = gMs.cycle;
            if (ur.fmac) { p.regU = ur.wr; p.fieldsU = ur.wf; p.flagreg = ur.viwrite; }
            if (lr.fmac && !imm) { p.regL = lr.wr; p.fieldsL = lr.wf; p.flagreg |= lr.viwrite; }
            p.mac = gMs.mac; p.status = gMs.status; p.clip = gMs.clip;
        }
        // the E bit ends the program after the next instruction; a branch lands after its delay slot
        uint32_t nextPc = pc + 8;
        if (armed >= 0) { nextPc = (uint32_t)armed; armed = -1; }
        if (jump >= 0) { armed = jump; jump = -1; }
        pc = nextPc & PcMask();
        if (ebit > 0 && ebit-- == 1) break;
    }
    P.pc = pc; P.jump = jump; P.armed = armed; P.ebit = ebit; P.steps = steps;
}
bool VuRunCompiled(uint32_t addr);
void MicroRun(uint32_t addr) {
    if (VuRunCompiled(addr)) return;
    MicroBegin();
    MicroPos P{addr & PcMask(), -1, -1, 0, 0};
    MicroLoop(P);
    MicroEnd(P, addr);
}
void VuNoteStart(int unit, uint32_t pc);
void VuNoteUpload(int unit, uint32_t addr, const uint32_t* code, uint32_t n);
// a microprogram run at a time: VU0's (the game thread) and VU1's (the GS thread, when on) share the pipeline's
// state (gMs, gEfu) and the unit pointer (gCurVu)
namespace {
std::atomic_flag gMicroBusy = ATOMIC_FLAG_INIT;
struct MicroLock {
    MicroLock() { while (gMicroBusy.test_and_set(std::memory_order_acquire)) { for (int k = 0; k < 64; ++k) __builtin_ia32_pause(); std::this_thread::yield(); } }
    ~MicroLock() { gMicroBusy.clear(std::memory_order_release); }
};
}  // namespace
void Vu0Micro(uint32_t addr) { MicroLock l; Vu0* was = gCurVu; gCurVu = &gVu0; MicroRun(addr); gCurVu = was; }
uint64_t gVu1Ns = 0, gVu1Runs = 0;                             // time in VU1 programs (log)
extern bool gGsTiming;                                          // gs.cpp: the stats' times wanted
bool Vu1RunStatic(uint32_t addr);                              // a static VU1 program, without the micro lock (below)
void Vu1Micro(uint32_t addr) {
    const auto t0 = gGsTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    if (!Vu1RunStatic(addr)) { MicroLock l; Vu0* was = gCurVu; gCurVu = &gVu1; MicroRun(addr); gCurVu = was; }
    if (gGsTiming) gVu1Ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    ++gVu1Runs;
}
// a static VU1 run handed to the interpreter (a jump its code does not hold), the pipe as its hand-over code left it
void Vu1HandOver(MicroPos& P, uint32_t iclip, uint32_t imac) {
    MicroLock l;
    Vu0* was = gCurVu; gCurVu = &gVu1;
    gEfu = Efu();
    gMs.clip = iclip; gMs.mac = imac; gMs.nslots = 0; gMs.nialu = 0; gMs.qBusy = false; gMs.bkLeft = 0;
    MicroLoop(P);
    MicroEnd(P, P.pc);
    gCurVu = was;
    P.jump = -2;                                                // the run's end done here
}
// a static VU0 program started where it has no entry (VuRunCompiled found it by its entry: not expected): the
// interpreter from there, under the lock its caller holds; the caller ends the run
void Vu0HandOver(MicroPos& P) {
    MicroBegin();
    MicroLoop(P);
}
void Vu0CallMs(Ctx& c, uint32_t addr) {
    (void)c;
    VuNoteStart(0, addr);
    Vu0Micro(addr);
}

// ---- VIF0 / VIF1: the packets of their DMA channels (0x10008000, 0x10009000), as they reach
// VU0 / VU1: MPG loads the micro memory, UNPACK the data memory (write cycle, mode, mask; VIF1
// adds the double buffer: BASE, OFFSET, TOPS / TOP), MSCAL / MSCALF / MSCNT run a program,
// VIF1's DIRECT / DIRECTHL hand GIF packets to PATH2
// the microprograms the game uploads (MPG), for the static VU recompiler: each upload is a
// blob {unit, load address, instructions, FNV-1a hash}; each doubleword of micro memory
// remembers the blob that wrote it last, so a program start finds the blob it runs in.
// SIM_VU_LOG=1 logs each new blob and each new entry point (blob, offset); SIM_VU_DUMP=1
// writes each new blob as vu<unit>_<address>_<hash>.bin
extern void (*gLog)(const char* fmt, ...);
uint32_t gMicroGen[2] = {0, 0};                              // micro memory generations: bumped at every write of it
uint32_t VifTop();
uint32_t VifItop(int unit);
namespace {
struct VuBlob { int unit; uint32_t addr, n; uint64_t hash; long long starts = 0; std::vector<uint32_t> entries; };
std::vector<VuBlob> gBlobs;
std::vector<int> gBlobOf[2] = {std::vector<int>(0x4000 / 8, -1), std::vector<int>(0x4000 / 8, -1)};
int VuLogMode() { static int m = getenv("SIM_VU_LOG") ? atoi(getenv("SIM_VU_LOG")) : 0; return m; }
}  // namespace
void VuNoteUpload(int unit, uint32_t addr, const uint32_t* code, uint32_t n) {
    ++gMicroGen[unit];
    if (!VuLogMode() && !getenv("SIM_VU_DUMP")) return;
    uint64_t h = 1469598103934665603ull;
    const uint8_t* b = (const uint8_t*)code;
    for (size_t k = 0; k < (size_t)n * 8; ++k) h = (h ^ b[k]) * 1099511628211ull;
    h ^= (uint64_t)addr << 48 ^ (uint64_t)unit << 62;
    int id = -1;
    for (size_t k = 0; k < gBlobs.size(); ++k) if (gBlobs[k].hash == h) { id = (int)k; break; }
    if (id < 0) {
        id = (int)gBlobs.size();
        gBlobs.push_back({unit, addr, n, h});
        if (gLog) gLog("vu%d: program blob %d at %04x, %u instructions, hash %016llx", unit, id, addr, n, (unsigned long long)h);
        if (getenv("SIM_VU_DUMP")) {
            char name[96]; snprintf(name, sizeof name, "vu%d_%04x_%016llx.bin", unit, addr, (unsigned long long)h);
            if (FILE* f = fopen(name, "wb")) { fwrite(code, 8, n, f); fclose(f); }
        }
    }
    const uint32_t mask = unit ? 0x3ff8 : 0xff8;
    for (uint32_t k = 0; k < n; ++k) gBlobOf[unit][((addr + 8 * k) & mask) / 8] = id;
}
void VuNoteStart(int unit, uint32_t pc) {
    static const bool dumps = getenv("SIM_VU_STATE") != nullptr;
    if (!VuLogMode() && !dumps) return;
    const int id = gBlobOf[unit][(pc & 0x3ff8) / 8];
    if (id < 0 && VuLogMode()) { if (gLog) gLog("vu%d: program start at %04x outside any uploaded blob", unit, pc); }
    if (id >= 0) ++gBlobs[(size_t)id].starts;
    // the micro memory's layout (which blob holds each part) when it changed: each new
    // (layout, entry) is logged once, for the recompiler's inputs
    static uint32_t seenGen[2] = {~0u, ~0u};
    static std::string layout[2];
    if (seenGen[unit] != gMicroGen[unit]) {
        seenGen[unit] = gMicroGen[unit];
        std::string t; int last = -2;
        const uint32_t n = unit ? 0x4000 / 8 : 0x1000 / 8;
        for (uint32_t d = 0; d < n; ++d) {
            const int o = gBlobOf[unit][d];
            if (o != last && o >= 0) { char x[32]; snprintf(x, sizeof x, " %d@%04x", o, d * 8); t += x; }
            last = o;
        }
        layout[unit] = t;
    }
    static std::set<std::string> said;
    char k[16]; snprintf(k, sizeof k, "%d:%04x", unit, pc);
    if (VuLogMode() && said.insert(std::string(k) + layout[unit]).second && gLog)
        gLog("vu%d: start %04x in blob %d, layout%s", unit, pc, id, layout[unit].c_str());
    // SIM_VU_STATE=N: the unit's whole state at the start (registers, memories, VIF1 TOP / ITOP)
    // for the first N starts of each (entry, micro memory), as vus<unit>_<entry>_<image>_<k>.bin:
    // the recompiler's input and the check of its programs against this interpreter
    static const int stateMax = getenv("SIM_VU_STATE") ? atoi(getenv("SIM_VU_STATE")) : 0;
    if (stateMax > 0) {
        static uint64_t img[2]; static uint32_t imgGen[2] = {~0u, ~0u};
        const Vu0& u = unit ? gVu1 : gVu0;
        const size_t size = unit ? 0x4000 : 0x1000;
        if (imgGen[unit] != gMicroGen[unit]) {
            imgGen[unit] = gMicroGen[unit];
            uint64_t h = 1469598103934665603ull;
            for (size_t b = 0; b < size; ++b) h = (h ^ u.micro[b]) * 1099511628211ull;
            img[unit] = h;
        }
        static std::map<std::string, int> count;
        char name[96]; snprintf(name, sizeof name, "vus%d_%04x_%016llx", unit, pc, (unsigned long long)img[unit]);
        int& c = count[name];
        if (c < stateMax) {
            char file[128]; snprintf(file, sizeof file, "%s_%d.bin", name, c++);
            if (FILE* f = fopen(file, "wb")) {
                const uint32_t head[6] = {0x31535556u, (uint32_t)unit, pc, VifTop(), VifItop(unit), (uint32_t)sizeof(Vu0)};
                fwrite(head, 4, 6, f); fwrite(&u, sizeof(Vu0), 1, f); fclose(f);
            }
        }
    }
}
extern bool gHeadless;                                           // gs.cpp: no picture, VU1 does not run
bool gVifStatsOn = getenv("MGO1_VIF_STATS") != nullptr;
uint64_t gVifStats[256][2];                                     // unpacks and their quadwords by form (tests)
std::string VifStats() {                                        // the forms since the last call, the busiest first
    std::vector<std::pair<uint64_t, int>> v;
    for (int k = 0; k < 256; ++k) if (gVifStats[k][1]) v.push_back({gVifStats[k][1], k});
    std::sort(v.rbegin(), v.rend());
    std::string out;
    for (size_t i = 0; i < v.size() && i < 12; ++i) {
        const int k = v[i].second; char b[96];
        snprintf(b, sizeof b, "%svif%d V%d-%d%s%s%s %llu/%llu", out.empty() ? "" : ", ", k >> 7, ((k >> 5) & 3) + 1, (k >> 3 & 3) == 3 ? 5 : 32 >> ((k >> 3) & 3),
                 k & 4 ? " masked" : "", k & 2 ? " mode" : "", k & 1 ? " fill" : "", (unsigned long long)gVifStats[k][0], (unsigned long long)gVifStats[k][1]);
        out += b;
    }
    memset(gVifStats, 0, sizeof gVifStats);
    return out;
}
namespace {
struct VifState {
    int idx = 0;
    std::vector<uint32_t> buf;                                   // words not consumed yet
    uint32_t cl = 1, wl = 1, mode = 0, mask = 0, row[4] = {0, 0, 0, 0}, col[4] = {0, 0, 0, 0};
    uint32_t base = 0, ofst = 0, tops = 0, top = 0, itops = 0, itop = 0, mark = 0;
    bool dbf = false, maskPath3 = false;
} gVif[2];
}  // namespace
uint32_t VifTop() { return gVif[1].top; }
uint32_t VifItop(int unit) { return gVif[unit ? 1 : 0].itop; }
uint32_t VuLastPc(int unit) { return gTpc[unit ? 1 : 0]; }                                  // tests
void VifSetTop(int unit, uint32_t top, uint32_t itop) { if (unit) gVif[1].top = top; gVif[unit ? 1 : 0].itop = itop; }   // tests
namespace {
uint32_t UnpackField(VifState& v, uint32_t x, int f, int cycle, bool masked, bool& write) {
    write = true;
    int m = 0;
    if (masked) m = (int)((v.mask >> (((cycle > 3 ? 3 : cycle) * 4 + f) * 2)) & 3);
    if (m == 1) return v.row[f];
    if (m == 2) return v.col[cycle > 3 ? 3 : cycle];
    if (m == 3) { write = false; return 0; }
    if (v.mode == 1) return x + v.row[f];
    if (v.mode == 2) { v.row[f] += x; return v.row[f]; }
    return x;
}
// a program start: VIF1 moves TOPS to TOP and flips its double buffer (PCSX2 vuExecMicro)
void VifStart(VifState& v, uint32_t addr) {
    Vu0& u = v.idx ? gVu1 : gVu0;
    (void)u;
    if (v.idx) {
        v.top = v.tops & 0x3ff;
        if (v.dbf) { v.dbf = false; v.tops = v.base; } else { v.dbf = true; v.tops = (v.base + v.ofst) & 0x3ff; }
        v.itop = v.itops;
        VuNoteStart(1, addr);
        Vu1Micro(addr);
    } else {
        v.itop = v.itops;
        VuNoteStart(0, addr);
        Vu0Micro(addr);
    }
}
// an UNPACK without a mask nor the row registers, every write with its data: element k to quadword
// addr + (k / wl) * cl + k % wl (wrapped by qmask), its components from data (past its end: 0), extended
template <int VN, int SIZE, bool USN>
void UnpackRunT(uint8_t* mem, uint32_t qmask, uint32_t addr, uint32_t count, uint32_t cl, uint32_t wl, const uint8_t* data, size_t dataBytes) {
    constexpr size_t stride = (size_t)(VN + 1) * SIZE;
    auto ld = [&](size_t o) -> uint32_t {
        if (o + SIZE > dataBytes) return 0;
        if (SIZE == 4) { uint32_t x; memcpy(&x, data + o, 4); return x; }
        if (SIZE == 2) { uint16_t x; memcpy(&x, data + o, 2); return USN ? (uint32_t)x : (uint32_t)(int32_t)(int16_t)x; }
        return USN ? (uint32_t)data[o] : (uint32_t)(int32_t)(int8_t)data[o];
    };
    if (VN == 3 && SIZE == 4 && cl == wl && (size_t)count * 16 <= dataBytes) {   // V4-32 in a row: copies
        for (uint32_t k = 0; k < count;) {
            const uint32_t q = (addr + k) & qmask, run = std::min(count - k, qmask + 1 - q);
            memcpy(mem + ((size_t)q << 4), data + (size_t)k * 16, (size_t)run * 16);
            k += run;
        }
        return;
    }
    if (VN == 3 && SIZE < 4 && cl == wl && (size_t)count * stride <= dataBytes) {   // V4-16 / V4-8 in a row: widened 4 at once
        const __m128i zero = _mm_setzero_si128();
        for (uint32_t k = 0; k < count; ++k) {
            __m128i x;
            if (SIZE == 2) {
                x = _mm_loadl_epi64((const __m128i*)(data + (size_t)k * 8));
                x = USN ? _mm_unpacklo_epi16(x, zero) : _mm_srai_epi32(_mm_unpacklo_epi16(x, x), 16);
            } else {
                uint32_t w; memcpy(&w, data + (size_t)k * 4, 4);
                x = _mm_cvtsi32_si128((int)w);
                x = USN ? _mm_unpacklo_epi16(_mm_unpacklo_epi8(x, zero), zero) : _mm_srai_epi32(_mm_unpacklo_epi16(_mm_unpacklo_epi8(x, x), _mm_unpacklo_epi8(x, x)), 24);
            }
            _mm_storeu_si128((__m128i*)(mem + ((size_t)((addr + k) & qmask) << 4)), x);
        }
        return;
    }
    uint32_t base = 0, cyc = 0;
    for (uint32_t k = 0; k < count; ++k) {
        uint8_t* dst = mem + (((addr + base + cyc) & qmask) << 4);
        if (++cyc == wl) { cyc = 0; base += cl; }
        const size_t o = (size_t)k * stride;
        if (VN == 3 && SIZE == 4 && o + 16 <= dataBytes) { memcpy(dst, data + o, 16); continue; }
        uint32_t comp[4];
        if (VN == 0) comp[0] = comp[1] = comp[2] = comp[3] = ld(o);
        else if (VN == 1) { comp[0] = ld(o); comp[1] = ld(o + SIZE); comp[2] = comp[0]; comp[3] = comp[1]; }
        else if (VN == 2) { comp[0] = ld(o); comp[1] = ld(o + SIZE); comp[2] = ld(o + 2 * SIZE); comp[3] = ld(o + stride); }
        else for (int f = 0; f < 4; ++f) comp[f] = ld(o + (size_t)f * SIZE);
        memcpy(dst, comp, 16);
    }
}
void UnpackRun(int vn, int size, bool usn, uint8_t* mem, uint32_t qmask, uint32_t addr, uint32_t count, uint32_t cl, uint32_t wl, const uint8_t* data, size_t dataBytes) {
#define UR(VN, SZ, U) UnpackRunT<VN, SZ, U>(mem, qmask, addr, count, cl, wl, data, dataBytes)
    switch (vn * 16 + size * 2 + (usn ? 1 : 0)) {
    case 0 * 16 + 8: case 0 * 16 + 9: UR(0, 4, false); return;
    case 1 * 16 + 8: case 1 * 16 + 9: UR(1, 4, false); return;
    case 2 * 16 + 8: case 2 * 16 + 9: UR(2, 4, false); return;
    case 3 * 16 + 8: case 3 * 16 + 9: UR(3, 4, false); return;
    case 0 * 16 + 4: UR(0, 2, false); return; case 0 * 16 + 5: UR(0, 2, true); return;
    case 1 * 16 + 4: UR(1, 2, false); return; case 1 * 16 + 5: UR(1, 2, true); return;
    case 2 * 16 + 4: UR(2, 2, false); return; case 2 * 16 + 5: UR(2, 2, true); return;
    case 3 * 16 + 4: UR(3, 2, false); return; case 3 * 16 + 5: UR(3, 2, true); return;
    case 0 * 16 + 2: UR(0, 1, false); return; case 0 * 16 + 3: UR(0, 1, true); return;
    case 1 * 16 + 2: UR(1, 1, false); return; case 1 * 16 + 3: UR(1, 1, true); return;
    case 2 * 16 + 2: UR(2, 1, false); return; case 2 * 16 + 3: UR(2, 1, true); return;
    case 3 * 16 + 2: UR(3, 1, false); return; case 3 * 16 + 3: UR(3, 1, true); return;
    }
#undef UR
}
// one command at the front of the buffer, when all its data is there: its size in words, 0 if not yet
size_t VifCommand(VifState& v, const uint32_t* w, size_t n) {
    const uint32_t code = w[0], cmd = (code >> 24) & 0x7f, imm = code & 0xffff;
    uint32_t num = (code >> 16) & 0xff;
    Vu0& u = v.idx ? gVu1 : gVu0;
    const uint32_t qmask = v.idx ? 0x3ff : 0xff, pcmask = v.idx ? 0x3ff8 : 0xff8;
    if (gGsTrace > 0) GsTraceLog("vif%d %08x (cmd %02x num %u imm %04x) top %03x tops %03x", v.idx, code, cmd, num, imm, v.top, v.tops);
    switch (cmd) {
    case 0x00: case 0x10: case 0x11: case 0x13: return 1;               // NOP, FLUSHE, FLUSH, FLUSHA (all done at once here)
    case 0x01: v.cl = imm & 0xff; v.wl = (imm >> 8) & 0xff; return 1;  // STCYCL
    case 0x02: if (v.idx) { v.ofst = imm & 0x3ff; v.dbf = false; v.tops = v.base; } return 1;   // OFFSET
    case 0x03: if (v.idx) v.base = imm & 0x3ff; return 1;               // BASE
    case 0x04: v.itops = imm & 0x3ff; return 1;                         // ITOP
    case 0x05: v.mode = imm & 3; return 1;                              // STMOD
    case 0x06: v.maskPath3 = (imm & 0x8000) != 0; return 1;             // MSKPATH3
    case 0x07: v.mark = imm; if (v.idx && (imm >> 8) == 0x5a) GsPackMark(imm); return 1;   // MARK (0x5Axx: the platform's 2D elements)
    case 0x14: case 0x15: if (!(v.idx && gHeadless)) VifStart(v, (imm * 8) & pcmask); return 1;   // MSCAL / MSCALF
    case 0x17: if (!(v.idx && gHeadless)) VifStart(v, gTpc[v.idx]); return 1;                      // MSCNT: on from where the last one ended
    case 0x20: if (n < 2) return 0; v.mask = w[1]; return 2;
    case 0x30: if (n < 5) return 0; memcpy(v.row, w + 1, 16); return 5;
    case 0x31: if (n < 5) return 0; memcpy(v.col, w + 1, 16); return 5;
    case 0x4a: {                                                           // MPG
        const uint32_t ni = num ? num : 256;
        if (n < 1 + 2 * (size_t)ni) return 0;
        if (v.idx && gHeadless) return 1 + 2 * (size_t)ni;
        bool same = true;                                                  // the code there already: nothing changes
        for (uint32_t k = 0; k < ni; ++k) {
            uint8_t* d = u.micro + ((imm * 8 + 8 * k) & pcmask);
            if (memcmp(d, w + 1 + 2 * k, 8)) { memcpy(d, w + 1 + 2 * k, 8); same = false; }
        }
        static const bool noted = VuLogMode() || getenv("SIM_VU_DUMP");   // the logs note every upload
        if (!same || noted) VuNoteUpload(v.idx, (imm * 8) & pcmask, w + 1, ni);
        return 1 + 2 * (size_t)ni; }
    case 0x50: case 0x51: {                                                // DIRECT / DIRECTHL: PATH2
        const size_t qw = imm ? imm : 65536, words = 4 * qw;
        if (n < 1 + words) return 0;
        if (v.idx) {
            // the data follows the code at the next quadword boundary: the words after it in
            // the stream, aligned by the DMA packing (the code sits in the last word of a qword)
            GifPath(2, (const uint8_t*)(w + 1), qw);
        } else Once("VIF0 DIRECT (VIF1 only)", code, 0);
        return 1 + words; }
    }
    if (cmd >= 0x60) {                                                     // UNPACK
        const int vn = (cmd >> 2) & 3, vl = cmd & 3;
        const bool masked = (cmd & 0x10) != 0, usn = (imm & 0x4000) != 0, flg = (imm & 0x8000) != 0;
        const uint32_t count = num ? num : 256;
        const uint32_t cl = v.cl, wl = v.wl ? v.wl : 1;
        uint32_t fromData = count;                                         // qwords taken from the data
        if (cl < wl) fromData = (count / wl) * cl + std::min(count % wl, cl);
        const int bits = vl == 3 ? 16 : (32 >> vl) * (vn + 1);
        const size_t words = ((size_t)fromData * bits + 31) / 32;
        if (n < 1 + words) return 0;
        if (v.idx && gHeadless) return 1 + words;                         // VU1's memory is only read by its programs
        const uint8_t* data = (const uint8_t*)(w + 1);
        const size_t dataBytes = words * 4;
        auto elem = [&](size_t idx, int comp) -> uint32_t {               // component comp of element idx, extended
            if (vl == 3) {
                size_t o = idx * 2; uint16_t h = 0; if (o + 2 <= dataBytes) memcpy(&h, data + o, 2);
                switch (comp) { case 0: return (h & 0x1f) << 3; case 1: return ((h >> 5) & 0x1f) << 3; case 2: return ((h >> 10) & 0x1f) << 3; default: return (uint32_t)(h >> 15) << 7; }
            }
            const int size = 4 >> vl;                                    // bytes per component
            const size_t o = (idx * (size_t)(vn + 1) + (size_t)comp) * size;
            uint32_t x = 0;
            if (o + size <= dataBytes) memcpy(&x, data + o, (size_t)size);
            if (size == 2) x = usn ? (x & 0xffff) : (uint32_t)(int32_t)(int16_t)x;
            else if (size == 1) x = usn ? (x & 0xff) : (uint32_t)(int32_t)(int8_t)x;
            return x;
        };
        uint32_t addr = (imm & 0x3ff) + (flg && v.idx ? v.tops : 0);
        if (gVifStatsOn) {                                                 // MGO1_VIF_STATS (tests): the unpacks' forms counted
            const int key = (v.idx << 7) | (vn << 5) | (vl << 3) | ((masked && v.mask) ? 4 : 0) | (v.mode ? 2 : 0) | (cl < wl ? 1 : 0);
            __atomic_fetch_add(&gVifStats[key][0], 1, __ATOMIC_RELAXED); __atomic_fetch_add(&gVifStats[key][1], count, __ATOMIC_RELAXED);
        }
        // MGO1_FASTPATH_CHECK=1 (tests): the fast form's memory checked against the general one's
        static const bool check = getenv("MGO1_FASTPATH_CHECK") != nullptr;
        static thread_local uint8_t before[16384], fastMem[16384];   // VIF0 and VIF1 may check at once (the GS thread)
        bool checkAfter = false;
        if (v.mode == 0 && (!masked || v.mask == 0) && cl >= wl && vl != 3) {   // no mask, no row, every write with data
            if (check) memcpy(before, u.mem, sizeof before);
            const int size = 4 >> vl;
            const size_t stride = (size_t)(vn + 1) * size;
            auto ld = [&](size_t o) -> uint32_t {
                uint32_t x = 0;
                if (o + size > dataBytes) return 0;
                memcpy(&x, data + o, (size_t)size);
                if (size == 2) x = usn ? (x & 0xffff) : (uint32_t)(int32_t)(int16_t)x;
                else if (size == 1) x = usn ? (x & 0xff) : (uint32_t)(int32_t)(int8_t)x;
                return x;
            };
            (void)stride; (void)ld;
            UnpackRun(vn, size, usn, u.mem, qmask, addr, count, cl, wl, data, dataBytes);
            if (!check) return 1 + words;
            memcpy(fastMem, u.mem, sizeof fastMem);
            memcpy(u.mem, before, sizeof before);
            checkAfter = true;
        }
        size_t di = 0;                                                     // next data element
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t pos = k % wl;                                  // place in the write cycle
            uint32_t q = cl >= wl ? addr + (k / wl) * cl + pos : addr + k;
            uint8_t* dst = u.mem + ((q & qmask) << 4);
            if (cl < wl && pos >= cl) {                                   // filling write: no data for it
                for (int f = 0; f < 4; ++f) { bool wr; uint32_t x = UnpackField(v, 0, f, (int)pos, true, wr); if (wr && masked) memcpy(dst + 4 * f, &x, 4); }
                continue;
            }
            uint32_t comp[4];
            if (vn == 0 && vl != 3) comp[0] = comp[1] = comp[2] = comp[3] = elem(di, 0);          // S: x everywhere
            else if (vn == 1) { comp[0] = elem(di, 0); comp[1] = elem(di, 1); comp[2] = comp[0]; comp[3] = comp[1]; }   // V2: x y x y
            else if (vn == 2) { comp[0] = elem(di, 0); comp[1] = elem(di, 1); comp[2] = elem(di, 2); comp[3] = elem(di + 1, 0); }   // V3: w from the next element
            else for (int f = 0; f < 4; ++f) comp[f] = elem(di, f);
            ++di;
            for (int f = 0; f < 4; ++f) { bool wr; uint32_t x = UnpackField(v, comp[f], f, (int)pos, masked, wr); if (wr) memcpy(dst + 4 * f, &x, 4); }
        }
        if (checkAfter) {
            static std::atomic<long long> checked{0}, bad{0};
            const long long nc = ++checked;
            if (memcmp(fastMem, u.mem, sizeof fastMem)) {
                size_t d = 0; while (d < sizeof fastMem && fastMem[d] == u.mem[d]) ++d;
                const long long nb = ++bad;
                if (nb <= 20 && gLog) gLog("vif%d: unpack %08x (cl %u wl %u) fast form differs at %04zx (%lld of %lld)", v.idx, code, cl, wl, d, nb, nc);
            } else if ((nc & (nc - 1)) == 0 && gLog) gLog("vif: unpack fast form checked %lld times, %lld differ", nc, bad.load());
        }
        return 1 + words;
    }
    Once(v.idx ? "VIF1 command not served" : "VIF0 command not served", code, 0);
    return 1;
}
void VifWrite(VifState& v, const uint32_t* w, size_t n) {
    if (v.buf.empty()) {                                           // whole commands read in place; a command whose data
        size_t at = 0;                                             // comes later is kept for the next write
        while (at < n) {
            const size_t used = VifCommand(v, w + at, n - at);
            if (!used) break;
            at += used;
        }
        if (at < n) v.buf.assign(w + at, w + n);
        return;
    }
    v.buf.insert(v.buf.end(), w, w + n);
    size_t at = 0;
    while (at < v.buf.size()) {
        const size_t used = VifCommand(v, v.buf.data() + at, v.buf.size() - at);
        if (!used) break;
        at += used;
    }
    v.buf.erase(v.buf.begin(), v.buf.begin() + (long)at);
}
}  // namespace
namespace vux {
// VU1's own lower instructions (the current unit is VU1)
void Vu1Lower(int lf2, uint32_t lo, int lft, int lfs, int ldest, int fsf, uint32_t tag) {
    auto vf = [&](int r, int f) { return Op(V.vf[r][f]); };
    auto startP = [&](float p, int cycles) {
        gEfu.busy = true; gEfu.start = gMs.cycle; gEfu.cycles = cycles; gEfu.val = U(p);
    };
    switch (lf2) {
    case 0x64:                                                  // MFP ft, P
        if (lft) for (int l = 0; l < 4; ++l) if (ldest & (8 >> l)) V.vf[lft][l] = V.vi[23];
        return;
    case 0x68: if (lft & 15) V.vi[lft & 15] = gVif[1].top & 0x3ff; return;       // XTOP
    case 0x69: if (lft & 15) V.vi[lft & 15] = gVif[1].itop & 0x3ff; return;      // XITOP
    case 0x6c: {                                                // XGKICK: a GIF packet from VU1 memory (PATH1)
        const uint32_t q = V.vi[lfs & 15] & 0x3ff;
        if (gGsTrace > 0) GsTraceLog("xgkick vi%d = %03x", lfs & 15, q);
        GifPath(1, V.mem, q, 0x4000);
        return; }
    case 0x70: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2); startP(x * x + y * y + z * z, 11); return; }             // ESADD
    case 0x71: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2), p = x * x + y * y + z * z; startP(p != 0 ? 1.0f / p : 0, 18); return; }   // ERSADD
    case 0x72: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2); startP(sqrtf(x * x + y * y + z * z), 18); return; }     // ELENG
    case 0x73: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2), p = x * x + y * y + z * z;                             // ERLENG
        if (p != 0) { p = sqrtf(p); if (p != 0) p = 1.0f / p; } startP(p, 24); return; }
    case 0x74: { float x = vf(lfs, 0), y = vf(lfs, 1); startP(x != 0 ? atan2f(y, x) : 0, 54); return; }                        // EATANxy
    case 0x75: { float x = vf(lfs, 0), z = vf(lfs, 2); startP(x != 0 ? atan2f(z, x) : 0, 54); return; }                        // EATANxz
    case 0x76: startP(vf(lfs, 0) + vf(lfs, 1) + vf(lfs, 2) + vf(lfs, 3), 12); return;                                          // ESUM
    case 0x78: { float a = vf(lfs, fsf); startP(a >= 0 ? sqrtf(a) : 0, 12); return; }                                          // ESQRT
    case 0x79: { float a = vf(lfs, fsf); startP(a > 0 ? 1.0f / sqrtf(a) : 0, 18); return; }                                    // ERSQRT
    case 0x7a: { float a = vf(lfs, fsf); startP(a != 0 ? 1.0f / a : 0, 12); return; }                                          // ERCPR
    case 0x7b: return;                                                                                                          // WAITP: waited above
    case 0x7c: startP(sinf(vf(lfs, fsf)), 29); return;                                                                         // ESIN
    case 0x7d: startP(atanf(vf(lfs, fsf)), 54); return;                                                                        // EATAN
    case 0x7e: startP(expf(-vf(lfs, fsf)), 44); return;                                                                        // EEXP
    }
    Once("VU1 lower instruction not implemented", tag, lo);
}
// the same for the static programs, without the micro lock: gVu1 and its own EFU (gEfu1)
void Vu1Lower1(int lf2, uint32_t lo, int lft, int lfs, int ldest, int fsf, uint32_t tag) {
    auto vf = [&](int r, int f) { return Op(gVu1.vf[r][f]); };
    auto startP = [&](float p, int cycles) {
        gEfu1.busy = true; gEfu1.start = 0; gEfu1.cycles = cycles; gEfu1.val = U(p);
    };
    switch (lf2) {
    case 0x64:                                                  // MFP ft, P
        if (lft) for (int l = 0; l < 4; ++l) if (ldest & (8 >> l)) gVu1.vf[lft][l] = gVu1.vi[23];
        return;
    case 0x68: if (lft & 15) gVu1.vi[lft & 15] = gVif[1].top & 0x3ff; return;       // XTOP
    case 0x69: if (lft & 15) gVu1.vi[lft & 15] = gVif[1].itop & 0x3ff; return;      // XITOP
    case 0x6c: {                                                // XGKICK: a GIF packet from VU1 memory (PATH1)
        const uint32_t q = gVu1.vi[lfs & 15] & 0x3ff;
        if (gGsTrace > 0) GsTraceLog("xgkick vi%d = %03x", lfs & 15, q);
        GifPath(1, gVu1.mem, q, 0x4000);
        return; }
    case 0x70: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2); startP(x * x + y * y + z * z, 11); return; }             // ESADD
    case 0x71: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2), p = x * x + y * y + z * z; startP(p != 0 ? 1.0f / p : 0, 18); return; }   // ERSADD
    case 0x72: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2); startP(sqrtf(x * x + y * y + z * z), 18); return; }     // ELENG
    case 0x73: { float x = vf(lfs, 0), y = vf(lfs, 1), z = vf(lfs, 2), p = x * x + y * y + z * z;                             // ERLENG
        if (p != 0) { p = sqrtf(p); if (p != 0) p = 1.0f / p; } startP(p, 24); return; }
    case 0x74: { float x = vf(lfs, 0), y = vf(lfs, 1); startP(x != 0 ? atan2f(y, x) : 0, 54); return; }                        // EATANxy
    case 0x75: { float x = vf(lfs, 0), z = vf(lfs, 2); startP(x != 0 ? atan2f(z, x) : 0, 54); return; }                        // EATANxz
    case 0x76: startP(vf(lfs, 0) + vf(lfs, 1) + vf(lfs, 2) + vf(lfs, 3), 12); return;                                          // ESUM
    case 0x78: { float a = vf(lfs, fsf); startP(a >= 0 ? sqrtf(a) : 0, 12); return; }                                          // ESQRT
    case 0x79: { float a = vf(lfs, fsf); startP(a > 0 ? 1.0f / sqrtf(a) : 0, 18); return; }                                    // ERSQRT
    case 0x7a: { float a = vf(lfs, fsf); startP(a != 0 ? 1.0f / a : 0, 12); return; }                                          // ERCPR
    case 0x7b: return;                                                                                                          // WAITP: waited above
    case 0x7c: startP(sinf(vf(lfs, fsf)), 29); return;                                                                         // ESIN
    case 0x7d: startP(atanf(vf(lfs, fsf)), 54); return;                                                                        // EATAN
    case 0x7e: startP(expf(-vf(lfs, fsf)), 44); return;                                                                        // EEXP
    }
    Once("VU1 lower instruction not implemented", tag, lo);
}
}  // namespace vux
// ---- the recompiled microprograms (recomp/vu2cpp.py): each checked against the micro memory
// (a hash of its instruction ranges) once per micro memory generation, then run in place of
// the interpreter; SIM_VU_INTERP=1 keeps the interpreter for every program
namespace {
struct VuCacheSlot { uint32_t gen = 0; int idx = -1; };
VuCacheSlot gVuCache[2][0x4000 / 8];
}  // namespace
bool gVuInterpOnly = getenv("SIM_VU_INTERP") != nullptr;
long long gVuCompiledRuns = 0, gVuInterpRuns = 0;
int gVuLastFast = 0;                                            // the last compiled run was a fast one (tests)
// ---- the static VU1 programs without the micro lock (the GS thread's VU1 beside VU0's programs on the game thread): a
// static program keeps nothing in gMs, gEfu nor gCurVu (its EFU is gEfu1, its registers gVu1's); a jump it does not hold
// goes to the interpreter through Vu1HandOver, which takes the lock
int Vu1Find(uint32_t pc) {                                      // a recompiled program found (cached), -1 none or not yet
    VuCacheSlot& c = gVuCache[1][pc >> 3];
    if (c.gen == gMicroGen[1] + 1) return c.idx;
    for (int k = 0; k < kVuProgramCount; ++k) {
        const VuProgram& p = kVuPrograms[k];
        if (p.unit != 1 || p.entry != pc) continue;
        uint64_t h = 1469598103934665603ull;
        for (int r = 0; r < p.nranges; ++r)
            for (uint32_t b = p.ranges[r][0]; b < p.ranges[r][1]; ++b) h = (h ^ gVu1.micro[b]) * 1099511628211ull;
        if (h == p.hash) { c.gen = gMicroGen[1] + 1; c.idx = k; return k; }
    }
    return -1;                                                  // (VuRunCompiled caches the miss and logs it)
}
bool Vu1RunStatic(uint32_t addr) {
    if (gVuInterpOnly || kVuProgramCount == 0) return false;
    const uint32_t pc = addr & 0x3ff8;
    const int idx = Vu1Find(pc);
    if (idx < 0 || kVuPrograms[idx].fast != 2) return false;
    __atomic_add_fetch(&gVuCompiledRuns, 1, __ATOMIC_RELAXED);
    gVuLastFast = 2;
    gEfu1 = Efu();
    MicroPos P{pc, -1, -1, 0, 0};
    kVuPrograms[idx].fn(P);
    if (P.jump == -2) return true;                              // handed over: the end done there
    gVuLastSteps = P.steps;
    if (P.steps >= 4000000) Once("VU1 microprogram without an end (stopped)", addr, 0);
    gTpc[1] = P.pc;
    return true;
}
bool VuRunCompiled(uint32_t addr) {
    if (gVuInterpOnly || kVuProgramCount == 0) { ++gVuInterpRuns; return false; }
    const int unit = IsVu1() ? 1 : 0;
    const uint32_t pc = addr & PcMask();
    VuCacheSlot& c = gVuCache[unit][pc >> 3];
    if (c.gen != gMicroGen[unit] + 1) {
        c.gen = gMicroGen[unit] + 1;
        c.idx = -1;
        for (int k = 0; k < kVuProgramCount; ++k) {
            const VuProgram& p = kVuPrograms[k];
            if (p.unit != unit || p.entry != pc) continue;
            uint64_t h = 1469598103934665603ull;
            for (int r = 0; r < p.nranges; ++r)
                for (uint32_t b = p.ranges[r][0]; b < p.ranges[r][1]; ++b) h = (h ^ V.micro[b]) * 1099511628211ull;
            if (h == p.hash) { c.idx = k; break; }
        }
        if (c.idx < 0) {
            static std::set<uint64_t> said;
            uint64_t h = 1469598103934665603ull;
            for (size_t b = 0; b < (unit ? 0x4000u : 0x1000u); ++b) h = (h ^ V.micro[b]) * 1099511628211ull;
            if (said.insert(h ^ ((uint64_t)pc << 48) ^ ((uint64_t)unit << 62)).second && gLog)
                gLog("vu%d: program at %04x (micro memory %016llx) not recompiled: interpreted", unit, pc, (unsigned long long)h);
        }
    }
    if (c.idx < 0) { ++gVuInterpRuns; return false; }
    if (unit == 1 && kVuPrograms[c.idx].fast == 2) { ++gVuInterpRuns; return false; }   // (Vu1RunStatic's: not under the lock)
    __atomic_add_fetch(&gVuCompiledRuns, 1, __ATOMIC_RELAXED);
    gVuLastFast = kVuPrograms[c.idx].fast;
    if (kVuPrograms[c.idx].fast >= 2) {                           // the static forms keep no pipeline at run time
        gEfu = Efu(); gMs.nslots = 0; gMs.nialu = 0; gMs.qBusy = false; gMs.bkLeft = 0;
        MicroPos P{pc, -1, -1, 0, 0};
        kVuPrograms[c.idx].fn(P);
        MicroEnd(P, addr);
        return true;
    }
    MicroBegin();
    MicroPos P{pc, -1, -1, 0, 0};
    kVuPrograms[c.idx].fn(P);
    MicroEnd(P, addr);
    return true;
}
void Vif0Write(const uint32_t* w, size_t n) { gVif[0].idx = 0; VifWrite(gVif[0], w, n); }
void Vif0Reset() { gVif[0] = VifState(); gVif[0].idx = 0; }
void Vif1Write(const uint32_t* w, size_t n) {
    if (gGsqOn) GsqPut(kGsqVif1, w, n * 4);                             // the bench's recording (gs_thread.h)
    gVif[1].idx = 1; VifWrite(gVif[1], w, n);
}
void Vif1Reset() { gVif[1] = VifState(); gVif[1].idx = 1; }
std::string Vif1Save() {
    const VifState& v = gVif[1];
    const uint32_t s[] = {v.cl, v.wl, v.mode, v.mask, v.row[0], v.row[1], v.row[2], v.row[3], v.col[0], v.col[1], v.col[2], v.col[3],
                          v.base, v.ofst, v.tops, v.top, v.itops, v.itop, v.mark, v.dbf, v.maskPath3, gTpc[1], (uint32_t)v.buf.size()};
    std::string o((const char*)s, sizeof s);
    o.append((const char*)v.buf.data(), v.buf.size() * 4);
    return o;
}
bool Vif1Load(const std::string& d) {
    uint32_t s[23];
    if (d.size() < sizeof s) return false;
    memcpy(s, d.data(), sizeof s);
    if (d.size() != sizeof s + (size_t)s[22] * 4) return false;
    VifState& v = gVif[1];
    v = VifState(); v.idx = 1;
    v.cl = s[0]; v.wl = s[1]; v.mode = s[2]; v.mask = s[3]; memcpy(v.row, s + 4, 16); memcpy(v.col, s + 8, 16);
    v.base = s[12]; v.ofst = s[13]; v.tops = s[14]; v.top = s[15]; v.itops = s[16]; v.itop = s[17]; v.mark = s[18];
    v.dbf = s[19] != 0; v.maskPath3 = s[20] != 0; gTpc[1] = s[21];
    v.buf.resize(s[22]);
    memcpy(v.buf.data(), d.data() + sizeof s, v.buf.size() * 4);
    return true;
}

}  // namespace ps2
