// bcn.cpp - BC1 / BC3 blocks (bcn.h). Colour: the block's principal axis (power iteration on its
// covariance) gives the end points, each texel takes the nearest of the four colours, then one least
// squares pass on the end points for those indices (kept when it is closer). Alpha (BC3): the
// block's lowest and highest, eight steps between.
#include "bcn.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ps2 {
namespace bcn {
namespace {

inline int Clamp255(float v) { return v < 0 ? 0 : v > 255 ? 255 : (int)(v + 0.5f); }
inline uint16_t To565(int r, int g, int b) {
    return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
}
inline void From565(uint16_t c, int& r, int& g, int& b) {
    const int r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
    r = r5 << 3 | r5 >> 2; g = g6 << 2 | g6 >> 4; b = b5 << 3 | b5 >> 2;
}
void Palette(uint16_t c0, uint16_t c1, bool four, int pal[4][3]) {
    From565(c0, pal[0][0], pal[0][1], pal[0][2]);
    From565(c1, pal[1][0], pal[1][1], pal[1][2]);
    for (int k = 0; k < 3; ++k) {
        if (four) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
        else { pal[2][k] = (pal[0][k] + pal[1][k]) / 2; pal[3][k] = 0; }
    }
}
// the four colours' indices for the 16 texels and the squared error
int Assign(const float px[16][3], uint16_t c0, uint16_t c1, uint32_t& bits) {
    int pal[4][3]; Palette(c0, c1, true, pal);
    int err = 0; bits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bd = 1 << 30;
        for (int k = 0; k < 4; ++k) {
            const int dr = pal[k][0] - (int)px[i][0], dg = pal[k][1] - (int)px[i][1], db = pal[k][2] - (int)px[i][2];
            const int d = dr * dr + dg * dg + db * db;
            if (d < bd) { bd = d; best = k; }
        }
        bits |= (uint32_t)best << (2 * i); err += bd;
    }
    return err;
}
// the colour half of a block (8 bytes), always in the four colour mode (c0 > c1) unless flat
void ColourBlock(const float px[16][3], uint8_t* out) {
    float mean[3] = {0, 0, 0}, lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) for (int k = 0; k < 3; ++k) { mean[k] += px[i][k]; lo[k] = std::min(lo[k], px[i][k]); hi[k] = std::max(hi[k], px[i][k]); }
    for (float& m : mean) m /= 16.0f;
    uint16_t c0, c1; uint32_t bits = 0;
    if (hi[0] - lo[0] < 1 && hi[1] - lo[1] < 1 && hi[2] - lo[2] < 1) {           // flat
        c0 = c1 = To565(Clamp255(mean[0]), Clamp255(mean[1]), Clamp255(mean[2]));
    } else {
        float cov[6] = {0, 0, 0, 0, 0, 0};                                        // rr rg rb gg gb bb
        for (int i = 0; i < 16; ++i) {
            const float r = px[i][0] - mean[0], g = px[i][1] - mean[1], b = px[i][2] - mean[2];
            cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
        }
        float ax[3] = {hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
        for (int it = 0; it < 6; ++it) {
            const float x = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2];
            const float y = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2];
            const float z = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
            const float n = std::max(std::max(fabsf(x), fabsf(y)), fabsf(z));
            if (n < 1e-6f) break;
            ax[0] = x / n; ax[1] = y / n; ax[2] = z / n;
        }
        const float len2 = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2];
        float tmin = 1e9f, tmax = -1e9f;
        for (int i = 0; i < 16; ++i) {
            const float t = ((px[i][0] - mean[0]) * ax[0] + (px[i][1] - mean[1]) * ax[1] + (px[i][2] - mean[2]) * ax[2]) / (len2 > 0 ? len2 : 1);
            tmin = std::min(tmin, t); tmax = std::max(tmax, t);
        }
        c0 = To565(Clamp255(mean[0] + tmax * ax[0]), Clamp255(mean[1] + tmax * ax[1]), Clamp255(mean[2] + tmax * ax[2]));
        c1 = To565(Clamp255(mean[0] + tmin * ax[0]), Clamp255(mean[1] + tmin * ax[1]), Clamp255(mean[2] + tmin * ax[2]));
        if (c0 < c1) std::swap(c0, c1);
        if (c0 == c1) { if (c1 > 0) --c1; else ++c0; }
        int err = Assign(px, c0, c1, bits);
        // least squares end points for these indices: texel = a * w + b * (1 - w)
        static const float kW[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
        float aa = 0, ab = 0, bb = 0, ax_[3] = {0, 0, 0}, bx_[3] = {0, 0, 0};
        for (int i = 0; i < 16; ++i) {
            const float w = kW[(bits >> (2 * i)) & 3], v = 1.0f - w;
            aa += w * w; ab += w * v; bb += v * v;
            for (int k = 0; k < 3; ++k) { ax_[k] += w * px[i][k]; bx_[k] += v * px[i][k]; }
        }
        const float det = aa * bb - ab * ab;
        if (fabsf(det) > 1e-6f) {
            int ea[3], eb[3];
            for (int k = 0; k < 3; ++k) {
                ea[k] = Clamp255((ax_[k] * bb - bx_[k] * ab) / det);
                eb[k] = Clamp255((bx_[k] * aa - ax_[k] * ab) / det);
            }
            uint16_t d0 = To565(ea[0], ea[1], ea[2]), d1 = To565(eb[0], eb[1], eb[2]);
            if (d0 < d1) std::swap(d0, d1);
            if (d0 == d1) { if (d1 > 0) --d1; else ++d0; }
            uint32_t b2; const int e2 = Assign(px, d0, d1, b2);
            if (e2 < err) { c0 = d0; c1 = d1; bits = b2; err = e2; }
        }
    }
    out[0] = (uint8_t)c0; out[1] = (uint8_t)(c0 >> 8); out[2] = (uint8_t)c1; out[3] = (uint8_t)(c1 >> 8);
    memcpy(out + 4, &bits, 4);
}
// the alpha half of a BC3 block (8 bytes): eight steps from the highest (a0) to the lowest (a1)
void AlphaBlock(const uint8_t a[16], uint8_t* out) {
    int lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) { lo = std::min(lo, (int)a[i]); hi = std::max(hi, (int)a[i]); }
    out[0] = (uint8_t)hi; out[1] = (uint8_t)lo;
    uint64_t bits = 0;
    if (hi > lo) {
        int vals[8] = {hi, lo};
        for (int k = 1; k <= 6; ++k) vals[k + 1] = ((7 - k) * hi + k * lo + 3) / 7;
        for (int i = 0; i < 16; ++i) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 8; ++k) { const int d = abs(vals[k] - a[i]); if (d < bd) { bd = d; best = k; } }
            bits |= (uint64_t)best << (3 * i);
        }
    }
    for (int k = 0; k < 6; ++k) out[2 + k] = (uint8_t)(bits >> (8 * k));
}

}  // namespace

std::vector<uint8_t> Encode(const std::vector<uint32_t>& rgba, int w, int h, bool bc3) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4, bs = bc3 ? 16 : 8;
    std::vector<uint8_t> out((size_t)bw * bh * bs);
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            float px[16][3]; uint8_t a[16];
            for (int i = 0; i < 16; ++i) {
                const int x = std::min(bx * 4 + (i & 3), w - 1), y = std::min(by * 4 + (i >> 2), h - 1);
                const uint32_t c = rgba[(size_t)y * w + x];
                px[i][0] = (float)(c & 255); px[i][1] = (float)((c >> 8) & 255); px[i][2] = (float)((c >> 16) & 255); a[i] = (uint8_t)(c >> 24);
            }
            uint8_t* o = &out[((size_t)by * bw + bx) * bs];
            if (bc3) { AlphaBlock(a, o); ColourBlock(px, o + 8); }
            else ColourBlock(px, o);
        }
    return out;
}

std::vector<uint32_t> Decode(const uint8_t* blocks, int w, int h, bool bc3) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4, bs = bc3 ? 16 : 8;
    std::vector<uint32_t> out((size_t)w * h);
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* b = blocks + ((size_t)by * bw + bx) * bs;
            int alpha[16]; for (int& v : alpha) v = 255;
            if (bc3) {
                const int a0 = b[0], a1 = b[1];
                int vals[8] = {a0, a1};
                if (a0 > a1) for (int k = 1; k <= 6; ++k) vals[k + 1] = ((7 - k) * a0 + k * a1 + 3) / 7;
                else { for (int k = 1; k <= 4; ++k) vals[k + 1] = ((5 - k) * a0 + k * a1 + 2) / 5; vals[6] = 0; vals[7] = 255; }
                uint64_t bits = 0; for (int k = 0; k < 6; ++k) bits |= (uint64_t)b[2 + k] << (8 * k);
                for (int i = 0; i < 16; ++i) alpha[i] = vals[(bits >> (3 * i)) & 7];
                b += 8;
            }
            const uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
            int pal[4][3]; Palette(c0, c1, bc3 || c0 > c1, pal);
            uint32_t bits; memcpy(&bits, b + 4, 4);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= w || y >= h) continue;
                const int k = (bits >> (2 * i)) & 3;
                int a = alpha[i];
                if (!bc3 && c0 <= c1 && k == 3) a = 0;
                out[(size_t)y * w + x] = (uint32_t)pal[k][0] | (uint32_t)pal[k][1] << 8 | (uint32_t)pal[k][2] << 16 | (uint32_t)a << 24;
            }
        }
    return out;
}

std::vector<uint32_t> HalfSize(const std::vector<uint32_t>& rgba, int w, int h, int& nw, int& nh) {
    nw = std::max(1, w / 2); nh = std::max(1, h / 2);
    std::vector<uint32_t> out((size_t)nw * nh);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            uint32_t acc[4] = {0, 0, 0, 0}; int n = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(2 * x + dx, w - 1), sy = std::min(2 * y + dy, h - 1);
                    const uint32_t c = rgba[(size_t)sy * w + sx];
                    for (int k = 0; k < 4; ++k) acc[k] += (c >> (8 * k)) & 255;
                    ++n;
                }
            out[(size_t)y * nw + x] = (acc[0] + n / 2) / n | ((acc[1] + n / 2) / n) << 8 | ((acc[2] + n / 2) / n) << 16 | ((acc[3] + n / 2) / n) << 24;
        }
    return out;
}

}  // namespace bcn
}  // namespace ps2
