// mmi.cpp - the R5900 multimedia instructions (opcode 0x1C), for the recompiled code.
// Semantics follow PCSX2's interpreter (pcsx2/MMI.cpp), instruction by instruction,
// including its clamps, the division corner cases and the undocumented HI/LO results.
#include "ps2rt.h"
#include <cstdint>
namespace ps2 {
namespace {
union Q { uint64_t ud[2]; int64_t sd[2]; uint32_t ul[4]; int32_t sl[4]; uint16_t us[8]; int16_t ss[8]; uint8_t uc[16]; int8_t sc[16]; };
inline Q G(const Ctx& c, int r) { Q q; q.ud[0] = c.r[r]; q.ud[1] = c.rh[r]; return q; }
inline void S(Ctx& c, int r, const Q& q) { if (r) { c.r[r] = q.ud[0]; c.rh[r] = q.ud[1]; } }
inline Q GetHi(const Ctx& c) { Q q; q.ud[0] = c.hi; q.ud[1] = c.hi1; return q; }
inline Q GetLo(const Ctx& c) { Q q; q.ud[0] = c.lo; q.ud[1] = c.lo1; return q; }
inline void SetHi(Ctx& c, const Q& q) { c.hi = q.ud[0]; c.hi1 = q.ud[1]; }
inline void SetLo(Ctx& c, const Q& q) { c.lo = q.ud[0]; c.lo1 = q.ud[1]; }
inline uint32_t LeadingSignBits(int32_t x) {         // Common::CountLeadingSignBits
    uint32_t s = (uint32_t)x >> 31, n = 0;
    for (int b = 31; b >= 0 && (((uint32_t)x >> b) & 1) == s; --b) ++n;
    return n;
}
inline uint16_t ClampH(int32_t v) { return v > 0x7fff ? 0x7fff : v < -0x8000 ? 0x8000 : (uint16_t)v; }
}

void MMI(Ctx& c, uint32_t pc, uint32_t w) {
    const int rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31, sa = (w >> 6) & 31, fu = w & 63;
    const Q s = G(c, rs), t = G(c, rt);
    Q d = G(c, rd);
    switch (fu) {
    case 0x00: case 0x01: {                                          // MADD / MADDU
        uint64_t acc = (uint64_t)(uint32_t)c.lo | ((uint64_t)(uint32_t)c.hi << 32);
        if (fu == 0) acc = (uint64_t)((int64_t)acc + (int64_t)(int32_t)s.sl[0] * (int64_t)(int32_t)t.sl[0]);
        else acc += (uint64_t)s.ul[0] * t.ul[0];
        c.lo = (uint64_t)(int64_t)(int32_t)(uint32_t)acc; c.hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(acc >> 32);
        if (rd) { c.r[rd] = c.lo; }
        return;
    }
    case 0x04:                                                       // PLZCW
        if (!rd) return;
        d.ul[0] = LeadingSignBits(s.sl[0]) - 1; d.ul[1] = LeadingSignBits(s.sl[1]) - 1;
        S(c, rd, d); return;
    case 0x10: if (rd) c.r[rd] = c.hi1; return;                      // MFHI1
    case 0x11: c.hi1 = c.r[rs]; return;                              // MTHI1
    case 0x12: if (rd) c.r[rd] = c.lo1; return;                      // MFLO1
    case 0x13: c.lo1 = c.r[rs]; return;                              // MTLO1
    case 0x18: {                                                     // MULT1
        int64_t p = (int64_t)s.sl[0] * t.sl[0];
        c.lo1 = (uint64_t)(int64_t)(int32_t)(p & 0xffffffff); c.hi1 = (uint64_t)(int64_t)(int32_t)(p >> 32);
        if (rd) c.r[rd] = c.lo1;
        return;
    }
    case 0x19: {                                                     // MULTU1
        uint64_t p = (uint64_t)s.ul[0] * t.ul[0];
        c.lo1 = (uint64_t)(int64_t)(int32_t)(p & 0xffffffff); c.hi1 = (uint64_t)(int64_t)(int32_t)(p >> 32);
        if (rd) c.r[rd] = c.lo1;
        return;
    }
    case 0x1a:                                                       // DIV1
        if (s.ul[0] == 0x80000000 && t.ul[0] == 0xffffffff) { c.lo1 = (uint64_t)(int64_t)(int32_t)0x80000000; c.hi1 = 0; }
        else if (t.sl[0] != 0) { c.lo1 = (uint64_t)(int64_t)(s.sl[0] / t.sl[0]); c.hi1 = (uint64_t)(int64_t)(s.sl[0] % t.sl[0]); }
        else { c.lo1 = (uint64_t)(int64_t)(s.sl[0] < 0 ? 1 : -1); c.hi1 = (uint64_t)(int64_t)s.sl[0]; }
        return;
    case 0x1b:                                                       // DIVU1
        if (t.ul[0] != 0) { c.lo1 = (uint64_t)(int64_t)(int32_t)(s.ul[0] / t.ul[0]); c.hi1 = (uint64_t)(int64_t)(int32_t)(s.ul[0] % t.ul[0]); }
        else { c.lo1 = (uint64_t)(int64_t)-1; c.hi1 = (uint64_t)(int64_t)s.sl[0]; }
        return;
    case 0x20: case 0x21: {                                          // MADD1 / MADDU1
        uint64_t acc = (uint64_t)(uint32_t)c.lo1 | ((uint64_t)(uint32_t)c.hi1 << 32);
        if (fu == 0x20) acc = (uint64_t)((int64_t)acc + (int64_t)s.sl[0] * (int64_t)t.sl[0]);
        else acc += (uint64_t)s.ul[0] * t.ul[0];
        c.lo1 = (uint64_t)(int64_t)(int32_t)(uint32_t)acc; c.hi1 = (uint64_t)(int64_t)(int32_t)(uint32_t)(acc >> 32);
        if (rd) c.r[rd] = c.lo1;
        return;
    }
    case 0x30: {                                                     // PMFHL
        if (!rd) return;
        const Q hi = GetHi(c), lo = GetLo(c);
        switch (sa) {
        case 0: d.ul[0] = lo.ul[0]; d.ul[1] = hi.ul[0]; d.ul[2] = lo.ul[2]; d.ul[3] = hi.ul[2]; break;   // LW
        case 1: d.ul[0] = lo.ul[1]; d.ul[1] = hi.ul[1]; d.ul[2] = lo.ul[3]; d.ul[3] = hi.ul[3]; break;   // UW
        case 2:                                                                                          // SLW
            for (int k = 0; k < 2; ++k) {
                int64_t v = (int64_t)(((uint64_t)hi.ul[2 * k] << 32) | lo.ul[2 * k]);
                d.ud[k] = v >= 0x7fffffffLL ? 0x7fffffffULL : v <= -0x80000000LL ? 0xffffffff80000000ULL : (uint64_t)(int64_t)lo.sl[2 * k];
            }
            break;
        case 3:                                                                                          // LH
            d.us[0] = lo.us[0]; d.us[1] = lo.us[2]; d.us[2] = hi.us[0]; d.us[3] = hi.us[2];
            d.us[4] = lo.us[4]; d.us[5] = lo.us[6]; d.us[6] = hi.us[4]; d.us[7] = hi.us[6];
            break;
        case 4:                                                                                          // SH
            d.us[0] = ClampH((int32_t)lo.ul[0]); d.us[1] = ClampH((int32_t)lo.ul[1]);
            d.us[2] = ClampH((int32_t)hi.ul[0]); d.us[3] = ClampH((int32_t)hi.ul[1]);
            d.us[4] = ClampH((int32_t)lo.ul[2]); d.us[5] = ClampH((int32_t)lo.ul[3]);
            d.us[6] = ClampH((int32_t)hi.ul[2]); d.us[7] = ClampH((int32_t)hi.ul[3]);
            break;
        default: return;
        }
        S(c, rd, d); return;
    }
    case 0x31: {                                                     // PMTHL (LW only)
        if (sa != 0) return;
        Q hi = GetHi(c), lo = GetLo(c);
        lo.ul[0] = s.ul[0]; hi.ul[0] = s.ul[1]; lo.ul[2] = s.ul[2]; hi.ul[2] = s.ul[3];
        SetHi(c, hi); SetLo(c, lo); return;
    }
    case 0x34: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = (uint16_t)(t.us[k] << (sa & 15)); S(c, rd, d); return;   // PSLLH
    case 0x36: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = (uint16_t)(t.us[k] >> (sa & 15)); S(c, rd, d); return;   // PSRLH
    case 0x37: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = (uint16_t)(t.ss[k] >> (sa & 15)); S(c, rd, d); return;   // PSRAH
    case 0x3c: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = t.ul[k] << sa; S(c, rd, d); return;                      // PSLLW
    case 0x3e: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = t.ul[k] >> sa; S(c, rd, d); return;                      // PSRLW
    case 0x3f: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = (uint32_t)(t.sl[k] >> sa); S(c, rd, d); return;          // PSRAW

    case 0x08:                                                       // MMI0
        switch (sa) {
        case 0x00: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = s.ul[k] + t.ul[k]; break;                                 // PADDW
        case 0x01: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = s.ul[k] - t.ul[k]; break;                                 // PSUBW
        case 0x02: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = s.sl[k] > t.sl[k] ? 0xffffffffu : 0; break;               // PCGTW
        case 0x03: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = s.sl[k] > t.sl[k] ? s.ul[k] : t.ul[k]; break;             // PMAXW
        case 0x04: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = (uint16_t)(s.us[k] + t.us[k]); break;                    // PADDH
        case 0x05: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = (uint16_t)(s.us[k] - t.us[k]); break;                    // PSUBH
        case 0x06: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = s.ss[k] > t.ss[k] ? 0xffff : 0; break;                   // PCGTH
        case 0x07: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = s.ss[k] > t.ss[k] ? s.us[k] : t.us[k]; break;             // PMAXH
        case 0x08: if (!rd) return; for (int k = 0; k < 16; ++k) d.sc[k] = (int8_t)(s.sc[k] + t.sc[k]); break;                     // PADDB
        case 0x09: if (!rd) return; for (int k = 0; k < 16; ++k) d.sc[k] = (int8_t)(s.sc[k] - t.sc[k]); break;                     // PSUBB
        case 0x0a: if (!rd) return; for (int k = 0; k < 16; ++k) d.uc[k] = s.sc[k] > t.sc[k] ? 0xff : 0; break;                    // PCGTB
        case 0x10: if (!rd) return; for (int k = 0; k < 4; ++k) { int64_t v = (int64_t)s.sl[k] + t.sl[k];                         // PADDSW
                       d.ul[k] = v > 0x7fffffff ? 0x7fffffffu : v < (int32_t)0x80000000 ? 0x80000000u : (uint32_t)(int32_t)v; } break;
        case 0x11: if (!rd) return; for (int k = 0; k < 4; ++k) { int64_t v = (int64_t)s.sl[k] - t.sl[k];                         // PSUBSW
                       d.ul[k] = v >= 0x7fffffff ? 0x7fffffffu : v < (int32_t)0x80000000 ? 0x80000000u : (uint32_t)(int32_t)v; } break;
        case 0x12: if (!rd) return; d.ul[0] = t.ul[0]; d.ul[1] = s.ul[0]; d.ul[2] = t.ul[1]; d.ul[3] = s.ul[1]; break;             // PEXTLW
        case 0x13: if (!rd) return; d.ul[0] = t.ul[0]; d.ul[1] = t.ul[2]; d.ul[2] = s.ul[0]; d.ul[3] = s.ul[2]; break;             // PPACW
        case 0x14: if (!rd) return; for (int k = 0; k < 8; ++k) { int32_t v = (int32_t)s.ss[k] + t.ss[k];                          // PADDSH
                       d.us[k] = v > 0x7fff ? 0x7fff : v < (int32_t)0xffff8000 ? 0x8000 : (uint16_t)(int16_t)v; } break;
        case 0x15: if (!rd) return; for (int k = 0; k < 8; ++k) { int32_t v = (int32_t)s.ss[k] - t.ss[k];                          // PSUBSH
                       d.us[k] = v >= 0x7fff ? 0x7fff : v < (int32_t)0xffff8000 ? 0x8000 : (uint16_t)(int16_t)v; } break;
        case 0x16: if (!rd) return; for (int k = 0; k < 4; ++k) { d.us[2 * k] = t.us[k]; d.us[2 * k + 1] = s.us[k]; } break;       // PEXTLH
        case 0x17: if (!rd) return; for (int k = 0; k < 4; ++k) { d.us[k] = t.us[2 * k]; d.us[k + 4] = s.us[2 * k]; } break;       // PPACH
        case 0x18: if (!rd) return; for (int k = 0; k < 16; ++k) { int16_t v = (int16_t)(s.sc[k] + t.sc[k]);                       // PADDSB
                       d.uc[k] = v > 0x7f ? 0x7f : v < -128 ? 0x80 : (uint8_t)(int8_t)v; } break;
        case 0x19: if (!rd) return; for (int k = 0; k < 16; ++k) { int16_t v = (int16_t)(s.sc[k] - t.sc[k]);                       // PSUBSB
                       d.uc[k] = v >= 0x7f ? 0x7f : v < -128 ? 0x80 : (uint8_t)(int8_t)v; } break;
        case 0x1a: if (!rd) return; for (int k = 0; k < 8; ++k) { d.uc[2 * k] = t.uc[k]; d.uc[2 * k + 1] = s.uc[k]; } break;       // PEXTLB
        case 0x1b: if (!rd) return; for (int k = 0; k < 8; ++k) { d.uc[k] = t.uc[2 * k]; d.uc[k + 8] = s.uc[2 * k]; } break;       // PPACB
        case 0x1e: if (!rd) return; for (int k = 0; k < 4; ++k) { uint32_t v = t.ul[k];                                           // PEXT5
                       d.ul[k] = ((v & 0x1f) << 3) | ((v & 0x3e0) << 6) | ((v & 0x7c00) << 9) | ((v & 0x8000) << 16); } break;
        case 0x1f: if (!rd) return; for (int k = 0; k < 4; ++k) { uint32_t v = t.ul[k];                                           // PPAC5
                       d.ul[k] = ((v >> 3) & 0x1f) | ((v >> 6) & 0x3e0) | ((v >> 9) & 0x7c00) | ((v >> 16) & 0x8000); } break;
        default: Once("MMI0 instruction", pc, w); return;
        }
        S(c, rd, d); return;

    case 0x28:                                                       // MMI1
        switch (sa) {
        case 0x01: if (!rd) return; for (int k = 0; k < 4; ++k)                                                                    // PABSW
                       d.ul[k] = t.ul[k] == 0x80000000u ? 0x7fffffffu : t.sl[k] < 0 ? (uint32_t)-t.sl[k] : (uint32_t)t.sl[k]; break;
        case 0x02: if (!rd) return; for (int k = 0; k < 4; ++k) d.ul[k] = s.ul[k] == t.ul[k] ? 0xffffffffu : 0; break;           // PCEQW
        case 0x03: if (!rd) return; for (int k = 0; k < 4; ++k) d.sl[k] = s.sl[k] < t.sl[k] ? s.sl[k] : t.sl[k]; break;           // PMINW
        case 0x04: if (!rd) return; for (int k = 0; k < 4; ++k) d.us[k] = (uint16_t)(s.us[k] - t.us[k]);                           // PADSBH
                   for (int k = 4; k < 8; ++k) d.us[k] = (uint16_t)(s.us[k] + t.us[k]); break;
        case 0x05: if (!rd) return; for (int k = 0; k < 8; ++k)                                                                    // PABSH
                       d.us[k] = t.us[k] == 0x8000 ? 0x7fff : t.ss[k] < 0 ? (uint16_t)-t.ss[k] : (uint16_t)t.ss[k]; break;
        case 0x06: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = s.us[k] == t.us[k] ? 0xffff : 0; break;                // PCEQH
        case 0x07: if (!rd) return; for (int k = 0; k < 8; ++k) d.us[k] = s.ss[k] < t.ss[k] ? s.us[k] : t.us[k]; break;           // PMINH
        case 0x0a: if (!rd) return; for (int k = 0; k < 16; ++k) d.uc[k] = s.uc[k] == t.uc[k] ? 0xff : 0; break;                  // PCEQB
        case 0x10: if (!rd) return; for (int k = 0; k < 4; ++k) { int64_t v = (int64_t)s.ul[k] + t.ul[k];                         // PADDUW
                       d.ul[k] = v > 0xffffffffLL ? 0xffffffffu : (uint32_t)v; } break;
        case 0x11: if (!rd) return; for (int k = 0; k < 4; ++k) { int64_t v = (int64_t)s.ul[k] - (int64_t)t.ul[k];                // PSUBUW
                       d.ul[k] = v <= 0 ? 0 : (uint32_t)v; } break;
        case 0x12: if (!rd) return; d.ul[0] = t.ul[2]; d.ul[1] = s.ul[2]; d.ul[2] = t.ul[3]; d.ul[3] = s.ul[3]; break;             // PEXTUW
        case 0x14: if (!rd) return; for (int k = 0; k < 8; ++k) { int32_t v = (int32_t)s.us[k] + t.us[k];                          // PADDUH
                       d.us[k] = v > 0xffff ? 0xffff : (uint16_t)v; } break;
        case 0x15: if (!rd) return; for (int k = 0; k < 8; ++k) { int32_t v = (int32_t)s.us[k] - (int32_t)t.us[k];                // PSUBUH
                       d.us[k] = v <= 0 ? 0 : (uint16_t)v; } break;
        case 0x16: if (!rd) return; for (int k = 0; k < 4; ++k) { d.us[2 * k] = t.us[k + 4]; d.us[2 * k + 1] = s.us[k + 4]; } break;   // PEXTUH
        case 0x18: if (!rd) return; for (int k = 0; k < 16; ++k) { uint16_t v = (uint16_t)(s.uc[k] + t.uc[k]);                    // PADDUB
                       d.uc[k] = v > 0xff ? 0xff : (uint8_t)v; } break;
        case 0x19: if (!rd) return; for (int k = 0; k < 16; ++k) { int16_t v = (int16_t)((int16_t)s.uc[k] - (int16_t)t.uc[k]);    // PSUBUB
                       d.uc[k] = v <= 0 ? 0 : (uint8_t)v; } break;
        case 0x1a: if (!rd) return; for (int k = 0; k < 8; ++k) { d.uc[2 * k] = t.uc[k + 8]; d.uc[2 * k + 1] = s.uc[k + 8]; } break;   // PEXTUB
        case 0x1b: {                                                                                                             // QFSRV
            if (!rd) return;
            const uint32_t n = (uint32_t)c.sa << 3;
            if (n == 0) { d = t; }
            else if (n < 64) {
                d.ud[0] = (t.ud[0] >> n) | (t.ud[1] << (64 - n));
                d.ud[1] = (t.ud[1] >> n) | (s.ud[0] << (64 - n));
            } else {
                d.ud[0] = t.ud[1] >> (n - 64);
                d.ud[1] = s.ud[0] >> (n - 64);
                if (n != 64) { d.ud[0] |= s.ud[0] << (128 - n); d.ud[1] |= s.ud[1] << (128 - n); }
            }
            break;
        }
        default: Once("MMI1 instruction", pc, w); return;
        }
        S(c, rd, d); return;

    case 0x09: {                                                     // MMI2
        Q hi = GetHi(c), lo = GetLo(c);
        switch (sa) {
        case 0x00: case 0x04:                                                                                  // PMADDW / PMSUBW
            for (int dd = 0; dd < 2; ++dd) {
                const int ss = dd * 2;
                int64_t p = (int64_t)s.sl[ss] * (int64_t)t.sl[ss], h;
                if (sa == 0) {
                    h = p + ((int64_t)hi.sl[ss] << 32);
                    if (ss == 0 && ((t.sl[ss] & 0x7fffffff) == 0 || (t.sl[ss] & 0x7fffffff) == 0x7fffffff) && s.sl[ss] != t.sl[ss])
                        h += 0x70000000;
                    h = (int32_t)(h / 4294967295LL);
                    lo.sd[dd] = (int32_t)((uint32_t)p + lo.ul[ss]);
                } else {
                    h = ((int64_t)hi.sl[ss] << 32) - p;
                    h = (int32_t)(h / 4294967295LL);
                    lo.sd[dd] = (int32_t)(lo.ul[ss] - (uint32_t)p);
                }
                hi.sd[dd] = (int32_t)h;
                if (rd) { d.ul[dd * 2] = lo.ul[dd * 2]; d.ul[dd * 2 + 1] = hi.ul[dd * 2]; }
            }
            SetHi(c, hi); SetLo(c, lo); S(c, rd, d); return;
        case 0x02: if (!rd) return; d.sd[0] = (int32_t)(t.ul[0] << (s.ul[0] & 31)); d.sd[1] = (int32_t)(t.ul[2] << (s.ul[2] & 31)); break;   // PSLLVW
        case 0x03: if (!rd) return; d.sd[0] = (int32_t)(t.ul[0] >> (s.ul[0] & 31)); d.sd[1] = (int32_t)(t.ul[2] >> (s.ul[2] & 31)); break;   // PSRLVW
        case 0x08: if (!rd) return; d = hi; break;                                                             // PMFHI
        case 0x09: if (!rd) return; d = lo; break;                                                             // PMFLO
        case 0x0a: if (!rd) return; for (int k = 0; k < 4; ++k) { d.us[2 * k] = t.us[k]; d.us[2 * k + 1] = s.us[k + 4]; } break;   // PINTH
        case 0x0c:                                                                                             // PMULTW
            for (int dd = 0; dd < 2; ++dd) {
                int64_t p = (int64_t)s.sl[dd * 2] * (int64_t)t.sl[dd * 2];
                lo.ud[dd] = (uint64_t)(int64_t)(int32_t)(p & 0xffffffff); hi.ud[dd] = (uint64_t)(int64_t)(int32_t)(p >> 32);
                if (rd) d.sd[dd] = p;
            }
            SetHi(c, hi); SetLo(c, lo); S(c, rd, d); return;
        case 0x0d:                                                                                             // PDIVW
            for (int dd = 0; dd < 2; ++dd) {
                const int ss = dd * 2;
                if (s.ul[ss] == 0x80000000u && t.ul[ss] == 0xffffffffu) { lo.sd[dd] = (int32_t)0x80000000; hi.sd[dd] = 0; }
                else if (t.sl[ss] != 0) { lo.sd[dd] = s.sl[ss] / t.sl[ss]; hi.sd[dd] = s.sl[ss] % t.sl[ss]; }
                else { lo.sd[dd] = s.sl[ss] < 0 ? 1 : -1; hi.sd[dd] = s.sl[ss]; }
            }
            SetHi(c, hi); SetLo(c, lo); return;
        case 0x0e: if (!rd) return; d.ud[1] = s.ud[0]; d.ud[0] = t.ud[0]; break;                                // PCPYLD
        case 0x10: case 0x14: {                                                                                // PMADDH / PMSUBH
            const int sign = sa == 0x10 ? 1 : -1;
            lo.ul[0] = (uint32_t)((int32_t)lo.ul[0] + sign * (int32_t)s.ss[0] * (int32_t)t.ss[0]);
            lo.ul[1] = (uint32_t)((int32_t)lo.ul[1] + sign * (int32_t)s.ss[1] * (int32_t)t.ss[1]);
            hi.ul[0] = (uint32_t)((int32_t)hi.ul[0] + sign * (int32_t)s.ss[2] * (int32_t)t.ss[2]);
            hi.ul[1] = (uint32_t)((int32_t)hi.ul[1] + sign * (int32_t)s.ss[3] * (int32_t)t.ss[3]);
            lo.ul[2] = (uint32_t)((int32_t)lo.ul[2] + sign * (int32_t)s.ss[4] * (int32_t)t.ss[4]);
            lo.ul[3] = (uint32_t)((int32_t)lo.ul[3] + sign * (int32_t)s.ss[5] * (int32_t)t.ss[5]);
            hi.ul[2] = (uint32_t)((int32_t)hi.ul[2] + sign * (int32_t)s.ss[6] * (int32_t)t.ss[6]);
            hi.ul[3] = (uint32_t)((int32_t)hi.ul[3] + sign * (int32_t)s.ss[7] * (int32_t)t.ss[7]);
            if (rd) { d.ul[0] = lo.ul[0]; d.ul[1] = hi.ul[0]; d.ul[2] = lo.ul[2]; d.ul[3] = hi.ul[2]; }
            SetHi(c, hi); SetLo(c, lo); S(c, rd, d); return;
        }
        case 0x11: case 0x15: {                                                                                // PHMADH / PHMSBH
            const bool add = sa == 0x11;
            auto pair = [&](int n, uint32_t& a, uint32_t& b) {
                int32_t first = (int32_t)s.ss[n + 1] * (int32_t)t.ss[n + 1];
                int32_t second = (int32_t)s.ss[n] * (int32_t)t.ss[n];
                a = (uint32_t)(add ? first + second : first - second);
                b = add ? (uint32_t)first : ~(uint32_t)first;
            };
            pair(0, lo.ul[0], lo.ul[1]); pair(2, hi.ul[0], hi.ul[1]);
            pair(4, lo.ul[2], lo.ul[3]); pair(6, hi.ul[2], hi.ul[3]);
            if (rd) { d.ul[0] = lo.ul[0]; d.ul[1] = hi.ul[0]; d.ul[2] = lo.ul[2]; d.ul[3] = hi.ul[2]; }
            SetHi(c, hi); SetLo(c, lo); S(c, rd, d); return;
        }
        case 0x12: if (!rd) return; d.ud[0] = s.ud[0] & t.ud[0]; d.ud[1] = s.ud[1] & t.ud[1]; break;            // PAND
        case 0x13: if (!rd) return; d.ud[0] = s.ud[0] ^ t.ud[0]; d.ud[1] = s.ud[1] ^ t.ud[1]; break;            // PXOR
        case 0x1a: if (!rd) return;                                                                            // PEXEH
            d.us[0] = t.us[2]; d.us[1] = t.us[1]; d.us[2] = t.us[0]; d.us[3] = t.us[3];
            d.us[4] = t.us[6]; d.us[5] = t.us[5]; d.us[6] = t.us[4]; d.us[7] = t.us[7]; break;
        case 0x1b: if (!rd) return;                                                                            // PREVH
            d.us[0] = t.us[3]; d.us[1] = t.us[2]; d.us[2] = t.us[1]; d.us[3] = t.us[0];
            d.us[4] = t.us[7]; d.us[5] = t.us[6]; d.us[6] = t.us[5]; d.us[7] = t.us[4]; break;
        case 0x1c:                                                                                             // PMULTH
            lo.ul[0] = (uint32_t)((int32_t)s.ss[0] * t.ss[0]); lo.ul[1] = (uint32_t)((int32_t)s.ss[1] * t.ss[1]);
            hi.ul[0] = (uint32_t)((int32_t)s.ss[2] * t.ss[2]); hi.ul[1] = (uint32_t)((int32_t)s.ss[3] * t.ss[3]);
            lo.ul[2] = (uint32_t)((int32_t)s.ss[4] * t.ss[4]); lo.ul[3] = (uint32_t)((int32_t)s.ss[5] * t.ss[5]);
            hi.ul[2] = (uint32_t)((int32_t)s.ss[6] * t.ss[6]); hi.ul[3] = (uint32_t)((int32_t)s.ss[7] * t.ss[7]);
            if (rd) { d.ul[0] = lo.ul[0]; d.ul[1] = hi.ul[0]; d.ul[2] = lo.ul[2]; d.ul[3] = hi.ul[2]; }
            SetHi(c, hi); SetLo(c, lo); S(c, rd, d); return;
        case 0x1d:                                                                                             // PDIVBW
            for (int n = 0; n < 4; ++n) {
                if (s.ul[n] == 0x80000000u && t.us[0] == 0xffff) { lo.sl[n] = (int32_t)0x80000000; hi.sl[n] = 0; }
                else if (t.us[0] != 0) { lo.sl[n] = s.sl[n] / t.ss[0]; hi.sl[n] = s.sl[n] % t.ss[0]; }
                else { lo.sl[n] = s.sl[n] < 0 ? 1 : -1; hi.sl[n] = s.sl[n]; }
            }
            SetHi(c, hi); SetLo(c, lo); return;
        case 0x1e: if (!rd) return; d.ul[0] = t.ul[2]; d.ul[1] = t.ul[1]; d.ul[2] = t.ul[0]; d.ul[3] = t.ul[3]; break;   // PEXEW
        case 0x1f: if (!rd) return; d.ul[0] = t.ul[1]; d.ul[1] = t.ul[2]; d.ul[2] = t.ul[0]; d.ul[3] = t.ul[3]; break;   // PROT3W
        default: Once("MMI2 instruction", pc, w); return;
        }
        S(c, rd, d); return;
    }

    case 0x29: {                                                     // MMI3
        Q hi = GetHi(c), lo = GetLo(c);
        switch (sa) {
        case 0x00:                                                                                             // PMADDUW
            for (int dd = 0; dd < 2; ++dd) {
                const int ss = dd * 2;
                uint64_t v = ((uint64_t)lo.ul[ss] | ((uint64_t)hi.ul[ss] << 32)) + (uint64_t)s.ul[ss] * t.ul[ss];
                lo.sd[dd] = (int32_t)(v & 0xffffffff); hi.sd[dd] = (int32_t)(v >> 32);
                if (rd) d.ud[dd] = v;
            }
            SetHi(c, hi); SetLo(c, lo); S(c, rd, d); return;
        case 0x03: if (!rd) return; d.sd[0] = (int64_t)(t.sl[0] >> (s.ul[0] & 31)); d.sd[1] = (int64_t)(t.sl[2] >> (s.ul[2] & 31)); break;   // PSRAVW
        case 0x08: c.hi = c.r[rs]; c.hi1 = c.rh[rs]; return;                                                    // PMTHI
        case 0x09: c.lo = c.r[rs]; c.lo1 = c.rh[rs]; return;                                                    // PMTLO
        case 0x0a: if (!rd) return; for (int k = 0; k < 4; ++k) { d.us[2 * k] = t.us[2 * k]; d.us[2 * k + 1] = s.us[2 * k]; } break;   // PINTEH
        case 0x0c:                                                                                             // PMULTUW
            for (int dd = 0; dd < 2; ++dd) {
                uint64_t v = (uint64_t)s.ul[dd * 2] * t.ul[dd * 2];
                lo.ud[dd] = (uint64_t)(int64_t)(int32_t)(v & 0xffffffff); hi.ud[dd] = (uint64_t)(int64_t)(int32_t)(v >> 32);
                if (rd) d.ud[dd] = v;
            }
            SetHi(c, hi); SetLo(c, lo); S(c, rd, d); return;
        case 0x0d:                                                                                             // PDIVUW
            for (int dd = 0; dd < 2; ++dd) {
                const int ss = dd * 2;
                if (t.ul[ss] != 0) { lo.sd[dd] = (int32_t)(s.ul[ss] / t.ul[ss]); hi.sd[dd] = (int32_t)(s.ul[ss] % t.ul[ss]); }
                else { lo.sd[dd] = -1; hi.sd[dd] = s.sl[ss]; }
            }
            SetHi(c, hi); SetLo(c, lo); return;
        case 0x0e: if (!rd) return; d.ud[0] = s.ud[1]; d.ud[1] = t.ud[1]; break;                               // PCPYUD
        case 0x12: if (!rd) return; d.ud[0] = s.ud[0] | t.ud[0]; d.ud[1] = s.ud[1] | t.ud[1]; break;            // POR
        case 0x13: if (!rd) return; d.ud[0] = ~(s.ud[0] | t.ud[0]); d.ud[1] = ~(s.ud[1] | t.ud[1]); break;      // PNOR
        case 0x1a: if (!rd) return;                                                                            // PEXCH
            d.us[0] = t.us[0]; d.us[1] = t.us[2]; d.us[2] = t.us[1]; d.us[3] = t.us[3];
            d.us[4] = t.us[4]; d.us[5] = t.us[6]; d.us[6] = t.us[5]; d.us[7] = t.us[7]; break;
        case 0x1b: if (!rd) return;                                                                            // PCPYH
            for (int k = 0; k < 4; ++k) { d.us[k] = t.us[0]; d.us[k + 4] = t.us[4]; } break;
        case 0x1e: if (!rd) return; d.ul[0] = t.ul[0]; d.ul[1] = t.ul[2]; d.ul[2] = t.ul[1]; d.ul[3] = t.ul[3]; break;   // PEXCW
        default: Once("MMI3 instruction", pc, w); return;
        }
        S(c, rd, d); return;
    }
    }
    Once("MMI instruction", pc, w);
}
}
