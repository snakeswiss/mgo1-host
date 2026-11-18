// Exact 60 (docs/FPS60.md): a hung object's pose in the picture between from its parent's (its place in the
// parent's frame blended, on the parent's pose as drawn). Matrices: rows x, y, z axes, translation (p drawn at p M).
#pragma once
#include <cmath>
#include <cstring>

namespace midchain {

// the inverse of m's 3 x 4 (its w column taken as 0 0 0 1); false when its axes are not independent
inline bool AffineInv(const float* m, float* o) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (!(std::fabs(det) > 1e-20f)) return false;
    const float r = 1.0f / det;
    o[0] = A * r; o[1] = (c * h - b * i) * r; o[2] = (b * f - c * e) * r; o[3] = 0;
    o[4] = B * r; o[5] = (a * i - c * g) * r; o[6] = (c * d - a * f) * r; o[7] = 0;
    o[8] = C * r; o[9] = (b * g - a * h) * r; o[10] = (a * e - b * d) * r; o[11] = 0;
    for (int k = 0; k < 3; ++k) o[12 + k] = -(m[12] * o[k] + m[13] * o[4 + k] + m[14] * o[8 + k]);
    o[15] = 1;
    return true;
}

// o = a b: a pose given in b's frame, in b's parent's (3 x 4 parts; w column 0 0 0 1)
inline void AffineMul(const float* a, const float* b, float* o) {
    float r[16];
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 3; ++k)
            r[4 * row + k] = a[4 * row] * b[k] + a[4 * row + 1] * b[4 + k] + a[4 * row + 2] * b[8 + k] + (row == 3 ? b[12 + k] : 0.0f);
    r[3] = r[7] = r[11] = 0; r[15] = 1;
    memcpy(o, r, sizeof r);
}

// how far c moved in p's frame between the two ticks (its place, plus its axes as at 1000 units)
inline float Moved(const float* cPrev, const float* cNow, const float* pPrev, const float* pNow) {
    float inv[16], a[16], b[16];
    if (!AffineInv(pPrev, inv)) return 1e30f;
    AffineMul(cPrev, inv, a);
    if (!AffineInv(pNow, inv)) return 1e30f;
    AffineMul(cNow, inv, b);
    float t = 0, x = 0;
    for (int k = 0; k < 3; ++k) {
        t += (a[12 + k] - b[12 + k]) * (a[12 + k] - b[12 + k]);
        for (int r = 0; r < 3; ++r) x = std::fmax(x, std::fabs(a[4 * r + k] - b[4 * r + k]));
    }
    return std::sqrt(t) + 1000.0f * x;
}

// c's pose in the picture: its place in p's frame blended by t (the one now without prevs or for a jump), on pMid
template <class Blend>
bool Attached(const float* cPrev, const float* cNow, const float* pPrev, const float* pNow, const float* pMid, float t, Blend blend, float* out) {
    float inv[16], relNow[16], rel[16];
    if (!AffineInv(pNow, inv)) return false;
    AffineMul(cNow, inv, relNow);
    memcpy(rel, relNow, sizeof rel);
    if (cPrev && pPrev && AffineInv(pPrev, inv)) {
        float relPrev[16], mid[16];
        AffineMul(cPrev, inv, relPrev);
        if (blend(relPrev, relNow, t, mid)) memcpy(rel, mid, sizeof rel);
    }
    AffineMul(rel, pMid, out);
    out[3] = cNow[3]; out[7] = cNow[7]; out[11] = cNow[11]; out[15] = cNow[15];   // its own w column
    return true;
}

}  // namespace midchain
