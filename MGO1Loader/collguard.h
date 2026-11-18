// The collision guards' plain parts (ps2world.cpp DispatchBridge; tests/collision_guard_test.cpp)
#pragma once
#include <cfloat>
#include <cmath>
#include <xmmintrin.h>

// (v150-host beta 18) the ray walk 0x159D88's two vectors (four words each): 1 when x, y or z is past 10^12 (a walk
// that would never end: refused), 2 when only the fourth word is (the walk never reads it: walked), 0 when sane.
// Any finite value past 10^12 counts, the saturated largest float too (as the check of beta 15 did)
inline int RayWordsBad(const float* a, const float* b) {
    int bad = 0;
    const float* const v2[2] = {a, b};
    for (const float* v : v2) {
        if (!v) continue;
        for (int k = 0; k < 4; ++k)
            if (std::fabs(v[k]) > 1e12f && std::fabs(v[k]) <= FLT_MAX) { if (k < 3) return 1; bad = 2; }
    }
    return bad;
}

// The collision grid's box queries (0x156920, 0x1599C8, 0x15BD58): the top cell's end, the box's top in cells
// clamped to the grid's size then "+ 0.99999994, to an integer" as the VU0 does it (vmini, vaddx, vftoi0), in the
// float unit's present rounding. The PS2 rounds toward zero: with hi past the grid, the end is the grid's size
__attribute__((noipa)) inline int GridTopEnd(float hi, float size) {   // (not moved across the rounding change)
    const __m128 m = _mm_min_ss(_mm_set_ss(hi), _mm_set_ss(size));
    const __m128 r = _mm_add_ss(m, _mm_set_ss(0.99999994f));
    return (int)_mm_cvtss_f32(r);
}
// fn run with the PS2's rounding (toward zero), the caller's rounding back after it
template <class F> inline void WithPs2Rounding(F&& fn) {
    const unsigned int was = _MM_GET_ROUNDING_MODE();
    _MM_SET_ROUNDING_MODE(_MM_ROUND_TOWARD_ZERO);
    fn();
    _MM_SET_ROUNDING_MODE(was);
}
