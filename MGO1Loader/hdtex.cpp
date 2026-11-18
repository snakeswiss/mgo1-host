// hdtex.cpp - HD textures (hdtex.h, docs/TEXTURES.md): the keys' slots, the pack and the cache
// folders, the dump, and the upscaler: Real-ESRGAN's compact model (SRVGGNetCompact, BSD 3-Clause,
// docs/licenses/Real-ESRGAN.txt) run on the processor, 3x3 convolutions of 64 channels (AVX2 when
// the processor has it), PReLU, a pixel shuffle by 4 and the input added back as nearest pixels.
#include "hdtex.h"
#include "bcn.h"
#include "png.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define HD_X86 1
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ps2 {
extern void (*gLog)(const char* fmt, ...);
namespace hdtex {
namespace {

void Say(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (gLog) gLog("hd textures: %s", b); else fprintf(stderr, "hd textures: %s\n", b);
}

// ---- xxHash64
constexpr uint64_t P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full, P3 = 0x165667B19E3779F9ull,
                   P4 = 0x85EBCA77C2B2AE63ull, P5 = 0x27D4EB2F165667C5ull;
inline uint64_t Rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline uint64_t Rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }
inline uint32_t Rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline uint64_t Round(uint64_t acc, uint64_t in) { acc += in * P2; acc = Rotl(acc, 31); return acc * P1; }
inline uint64_t Merge(uint64_t acc, uint64_t v) { acc ^= Round(0, v); return acc * P1 + P4; }

}  // namespace

uint64_t Hash64(const void* data, size_t n, uint64_t seed) {
    const uint8_t* p = (const uint8_t*)data; const uint8_t* end = p + n;
    uint64_t h;
    if (n >= 32) {
        uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        const uint8_t* limit = end - 32;
        do {
            v1 = Round(v1, Rd64(p)); v2 = Round(v2, Rd64(p + 8)); v3 = Round(v3, Rd64(p + 16)); v4 = Round(v4, Rd64(p + 24));
            p += 32;
        } while (p <= limit);
        h = Rotl(v1, 1) + Rotl(v2, 7) + Rotl(v3, 12) + Rotl(v4, 18);
        h = Merge(h, v1); h = Merge(h, v2); h = Merge(h, v3); h = Merge(h, v4);
    } else h = seed + P5;
    h += (uint64_t)n;
    while (p + 8 <= end) { h ^= Round(0, Rd64(p)); h = Rotl(h, 27) * P1 + P4; p += 8; }
    if (p + 4 <= end) { h ^= (uint64_t)Rd32(p) * P1; h = Rotl(h, 23) * P2 + P3; p += 4; }
    while (p < end) { h ^= (*p++) * P5; h = Rotl(h, 11) * P1; }
    h ^= h >> 33; h *= P2; h ^= h >> 29; h *= P3; h ^= h >> 32;
    return h;
}

namespace {

// ---- the model
struct Layer {
    int cin = 0, cout = 0; bool prelu = false;
    std::vector<float> w, b, a;             // w: [tap][in][out]
    std::vector<float> wino;                // 64 -> 64: the weights in Winograd's F(2x2, 3x3) form, [16][in][out]
};
// G g G^T of each 3x3 kernel (Winograd F(2x2, 3x3): G = [1 0 0; .5 .5 .5; .5 -.5 .5; 0 0 1])
void WinoWeights(Layer& L) {
    L.wino.assign((size_t)16 * L.cin * L.cout, 0.0f);
    for (int c = 0; c < L.cin; ++c)
        for (int o = 0; o < L.cout; ++o) {
            float g[3][3], gg[4][3];
            for (int t = 0; t < 9; ++t) g[t / 3][t % 3] = L.w[((size_t)t * L.cin + c) * L.cout + o];
            for (int j = 0; j < 3; ++j) {
                gg[0][j] = g[0][j]; gg[1][j] = 0.5f * (g[0][j] + g[1][j] + g[2][j]);
                gg[2][j] = 0.5f * (g[0][j] - g[1][j] + g[2][j]); gg[3][j] = g[2][j];
            }
            for (int i = 0; i < 4; ++i) {
                const float u[4] = {gg[i][0], 0.5f * (gg[i][0] + gg[i][1] + gg[i][2]), 0.5f * (gg[i][0] - gg[i][1] + gg[i][2]), gg[i][2]};
                for (int j = 0; j < 4; ++j) L.wino[((size_t)(i * 4 + j) * L.cin + c) * L.cout + o] = u[j];
            }
        }
}
struct Model { int scale = 0; std::vector<Layer> layers; std::string error; };
float Half(uint16_t h) {
    uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, f;
    if (e == 0) {
        if (!m) f = s;
        else { int ee = 1; while (!(m & 1024)) { m <<= 1; --ee; } m &= 1023; f = s | (uint32_t)(ee + 112) << 23 | m << 13; }
    } else if (e == 31) f = s | 0x7f800000u | m << 13;
    else f = s | (e + 112) << 23 | m << 13;
    float r; memcpy(&r, &f, 4); return r;
}
bool LoadModel(const std::string& path, Model& m) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { m.error = "no model file " + path; return false; }
    std::vector<uint8_t> d;
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 16) { d.resize((size_t)n); if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear(); }
    fclose(f);
    if (d.size() < 16 || memcmp(d.data(), "MGO1SR1", 8)) { m.error = "not an upscaler file: " + path; return false; }
    uint32_t scale, count; memcpy(&scale, &d[8], 4); memcpy(&count, &d[12], 4);
    size_t at = 16;
    auto half = [&](size_t k) { uint16_t v; memcpy(&v, &d[at + 2 * k], 2); return Half(v); };
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 12 > d.size()) { m.error = "cut short"; return false; }
        Layer L; uint32_t hdr[3]; memcpy(hdr, &d[at], 12); at += 12;
        L.cin = (int)hdr[0]; L.cout = (int)hdr[1]; L.prelu = hdr[2] != 0;
        const size_t nw = (size_t)L.cin * L.cout * 9, need = 2 * (nw + L.cout + (L.prelu ? L.cout : 0));
        if (L.cin <= 0 || L.cout <= 0 || L.cin > 256 || L.cout > 256 || at + need > d.size()) { m.error = "bad layer"; return false; }
        L.w.resize(nw);
        for (int o = 0; o < L.cout; ++o)                      // the file's [out][in][3][3] as [tap][in][out]
            for (int c = 0; c < L.cin; ++c)
                for (int t = 0; t < 9; ++t) L.w[((size_t)t * L.cin + c) * L.cout + o] = half(((size_t)o * L.cin + c) * 9 + t);
        at += 2 * nw;
        L.b.resize(L.cout); for (int o = 0; o < L.cout; ++o) L.b[o] = half(o);
        at += 2 * (size_t)L.cout;
        if (L.prelu) { L.a.resize(L.cout); for (int o = 0; o < L.cout; ++o) L.a[o] = half(o); at += 2 * (size_t)L.cout; }
        m.layers.push_back(std::move(L));
    }
    m.scale = (int)scale;
    for (Layer& L : m.layers) if (L.cin == 64 && L.cout == 64) WinoWeights(L);
    if (m.layers.empty() || m.layers[0].cin != 3 || m.layers.back().cout != 3 * m.scale * m.scale) { m.error = "unexpected layers"; return false; }
    for (size_t i = 1; i < m.layers.size(); ++i) if (m.layers[i].cin != m.layers[i - 1].cout) { m.error = "layers do not chain"; return false; }
    return true;
}

// activations: (h + 2) x (w + 2) pixels of c floats, a border of zeros (the convolutions' padding),
// and 4 pixels more at the end (the 3-pixel steps read past the last one)
struct Act {
    int w = 0, h = 0, c = 0; std::vector<float> d;
    void Make(int W, int H, int C) { w = W; h = H; c = C; d.assign(((size_t)(W + 3) * (H + 3) + 4) * C, 0.0f); }
    // x, y from -1 to w + 1, h + 1: a border of zeros (and one more line and column for an odd size's last tile)
    float* At(int x, int y) { return d.data() + ((size_t)(y + 1) * (w + 3) + (x + 1)) * c; }
};

// one layer, any shape (the first and the last, and processors without AVX2)
void ConvGeneric(const Layer& L, Act& in, Act& out) {
    const int W = in.w, H = in.h, cin = L.cin, cout = L.cout;
    std::vector<float> acc(cout);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            std::copy(L.b.begin(), L.b.end(), acc.begin());
            for (int t = 0; t < 9; ++t) {
                const float* ip = in.At(x + t % 3 - 1, y + t / 3 - 1);
                const float* wp = L.w.data() + (size_t)t * cin * cout;
                for (int c = 0; c < cin; ++c) {
                    const float v = ip[c]; const float* wr = wp + (size_t)c * cout;
                    for (int o = 0; o < cout; ++o) acc[o] += v * wr[o];
                }
            }
            float* op = out.At(x, y);
            for (int o = 0; o < cout; ++o) op[o] = L.prelu && acc[o] < 0 ? acc[o] * L.a[o] : acc[o];
        }
}

#ifdef HD_X86
// a 64 -> 64 layer: three pixels at a time, 32 outputs at a time (12 accumulators; the weights as
// the multiplies' memory operands)
__attribute__((target("avx2,fma"))) void Conv64Avx2(const Layer& L, Act& in, Act& out) {
    const int W = in.w, H = in.h;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; x += 3) {
            const int n = std::min(3, W - x);
            for (int half = 0; half < 2; ++half) {
                __m256 a[3][4];
                for (int k = 0; k < 4; ++k) { const __m256 b = _mm256_loadu_ps(L.b.data() + half * 32 + 8 * k); a[0][k] = a[1][k] = a[2][k] = b; }
                for (int t = 0; t < 9; ++t) {
                    const float* ip = in.At(x + t % 3 - 1, y + t / 3 - 1);
                    const float* wp = L.w.data() + (size_t)t * 4096 + half * 32;
                    for (int c = 0; c < 64; ++c) {
                        const float* wr = wp + c * 64;
                        const __m256 v0 = _mm256_broadcast_ss(ip + c), v1 = _mm256_broadcast_ss(ip + 64 + c), v2 = _mm256_broadcast_ss(ip + 128 + c);
                        for (int k = 0; k < 4; ++k) {
                            const __m256 w = _mm256_loadu_ps(wr + 8 * k);
                            a[0][k] = _mm256_fmadd_ps(v0, w, a[0][k]);
                            a[1][k] = _mm256_fmadd_ps(v1, w, a[1][k]);
                            a[2][k] = _mm256_fmadd_ps(v2, w, a[2][k]);
                        }
                    }
                }
                for (int p = 0; p < n; ++p) {
                    float* op = out.At(x + p, y) + half * 32;
                    for (int k = 0; k < 4; ++k) {
                        __m256 v = a[p][k];
                        if (L.prelu) {
                            const __m256 s = _mm256_loadu_ps(L.a.data() + half * 32 + 8 * k);
                            const __m256 neg = _mm256_min_ps(v, _mm256_setzero_ps());
                            v = _mm256_add_ps(_mm256_max_ps(v, _mm256_setzero_ps()), _mm256_mul_ps(neg, s));
                        }
                        _mm256_storeu_ps(op + 8 * k, v);
                    }
                }
            }
        }
}
// a 64 -> 64 layer by Winograd's F(2x2, 3x3): per 2 x 2 output tile, its 4 x 4 input tile made
// B^T d B, sixteen 64 x 64 products with the transformed weights (three tiles and 32 outputs at a
// time), then A^T m A: 2.25 times fewer multiplies than the direct form
__attribute__((target("avx2,fma"))) void Conv64Wino(const Layer& L, Act& in, Act& out) {
    const int W = in.w, H = in.h, TW = (W + 1) / 2, TH = (H + 1) / 2;
    std::vector<float> V((size_t)16 * TW * 64), M((size_t)16 * TW * 64);
    for (int ty = 0; ty < TH; ++ty) {
        for (int tx = 0; tx < TW; ++tx)                                // the input tiles: V = B^T d B
            for (int c8 = 0; c8 < 64; c8 += 8) {
                __m256 d[4][4];
                for (int r = 0; r < 4; ++r) for (int q = 0; q < 4; ++q) d[r][q] = _mm256_loadu_ps(in.At(2 * tx - 1 + q, 2 * ty - 1 + r) + c8);
                __m256 t[4][4];
                for (int q = 0; q < 4; ++q) {
                    t[0][q] = _mm256_sub_ps(d[0][q], d[2][q]); t[1][q] = _mm256_add_ps(d[1][q], d[2][q]);
                    t[2][q] = _mm256_sub_ps(d[2][q], d[1][q]); t[3][q] = _mm256_sub_ps(d[1][q], d[3][q]);
                }
                for (int i = 0; i < 4; ++i) {
                    const __m256 v[4] = {_mm256_sub_ps(t[i][0], t[i][2]), _mm256_add_ps(t[i][1], t[i][2]), _mm256_sub_ps(t[i][2], t[i][1]), _mm256_sub_ps(t[i][1], t[i][3])};
                    for (int j = 0; j < 4; ++j) _mm256_storeu_ps(&V[(((size_t)(i * 4 + j) * TW) + tx) * 64 + c8], v[j]);
                }
            }
        for (int ij = 0; ij < 16; ++ij) {                              // M = U V for each of the 16
            const float* U = L.wino.data() + (size_t)ij * 4096;
            const float* Vr = &V[(size_t)ij * TW * 64];
            float* Mr = &M[(size_t)ij * TW * 64];
            for (int half = 0; half < 2; ++half)
                for (int tx = 0; tx < TW; tx += 3) {
                    const int n = std::min(3, TW - tx);
                    const float* v0 = Vr + (size_t)tx * 64; const float* v1 = v0 + (n > 1 ? 64 : 0); const float* v2 = v0 + (n > 2 ? 128 : 0);
                    __m256 a[3][4];
                    for (int p = 0; p < 3; ++p) for (int k = 0; k < 4; ++k) a[p][k] = _mm256_setzero_ps();
                    for (int c = 0; c < 64; ++c) {
                        const float* wr = U + c * 64 + half * 32;
                        const __m256 b0 = _mm256_broadcast_ss(v0 + c), b1 = _mm256_broadcast_ss(v1 + c), b2 = _mm256_broadcast_ss(v2 + c);
                        for (int k = 0; k < 4; ++k) {
                            const __m256 w = _mm256_loadu_ps(wr + 8 * k);
                            a[0][k] = _mm256_fmadd_ps(b0, w, a[0][k]); a[1][k] = _mm256_fmadd_ps(b1, w, a[1][k]); a[2][k] = _mm256_fmadd_ps(b2, w, a[2][k]);
                        }
                    }
                    for (int p = 0; p < n; ++p) for (int k = 0; k < 4; ++k) _mm256_storeu_ps(Mr + (size_t)(tx + p) * 64 + half * 32 + 8 * k, a[p][k]);
                }
        }
        for (int tx = 0; tx < TW; ++tx)                                // the outputs: A^T m A, the bias, PReLU
            for (int c8 = 0; c8 < 64; c8 += 8) {
                __m256 m[4][4];
                for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) m[i][j] = _mm256_loadu_ps(&M[(((size_t)(i * 4 + j) * TW) + tx) * 64 + c8]);
                __m256 sr[2][4];
                for (int j = 0; j < 4; ++j) {
                    sr[0][j] = _mm256_add_ps(_mm256_add_ps(m[0][j], m[1][j]), m[2][j]);
                    sr[1][j] = _mm256_sub_ps(_mm256_sub_ps(m[1][j], m[2][j]), m[3][j]);
                }
                const __m256 bias = _mm256_loadu_ps(L.b.data() + c8);
                const __m256 slope = L.prelu ? _mm256_loadu_ps(L.a.data() + c8) : _mm256_set1_ps(1.0f);
                for (int i = 0; i < 2; ++i) {
                    const __m256 y[2] = {_mm256_add_ps(_mm256_add_ps(_mm256_add_ps(sr[i][0], sr[i][1]), sr[i][2]), bias),
                                         _mm256_add_ps(_mm256_sub_ps(_mm256_sub_ps(sr[i][1], sr[i][2]), sr[i][3]), bias)};
                    for (int j = 0; j < 2; ++j) {
                        const int x = 2 * tx + j, yy = 2 * ty + i;
                        if (x >= W || yy >= H) continue;
                        __m256 v = y[j];
                        if (L.prelu) v = _mm256_add_ps(_mm256_max_ps(v, _mm256_setzero_ps()), _mm256_mul_ps(_mm256_min_ps(v, _mm256_setzero_ps()), slope));
                        _mm256_storeu_ps(out.At(x, yy) + c8, v);
                    }
                }
            }
    }
}
// a 64 -> n layer (n a multiple of 8, 64 at most: the last one, 48), two pixels at a time
__attribute__((target("avx2,fma"))) void Conv64ToN(const Layer& L, Act& in, Act& out) {
    const int W = in.w, H = in.h, groups = L.cout / 8;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; x += 2) {
            const int n = std::min(2, W - x);
            __m256 a[2][8];
            for (int k = 0; k < groups; ++k) a[0][k] = a[1][k] = _mm256_loadu_ps(L.b.data() + 8 * k);
            for (int t = 0; t < 9; ++t) {
                const float* ip = in.At(x + t % 3 - 1, y + t / 3 - 1);
                const float* wp = L.w.data() + (size_t)t * 64 * L.cout;
                for (int c = 0; c < 64; ++c) {
                    const float* wr = wp + (size_t)c * L.cout;
                    const __m256 v0 = _mm256_broadcast_ss(ip + c), v1 = _mm256_broadcast_ss(ip + 64 + c);
                    for (int k = 0; k < groups; ++k) {
                        const __m256 w = _mm256_loadu_ps(wr + 8 * k);
                        a[0][k] = _mm256_fmadd_ps(v0, w, a[0][k]); a[1][k] = _mm256_fmadd_ps(v1, w, a[1][k]);
                    }
                }
            }
            for (int p = 0; p < n; ++p) {
                float* op = out.At(x + p, y);
                for (int k = 0; k < groups; ++k) {
                    __m256 v = a[p][k];
                    if (L.prelu) {
                        const __m256 sl = _mm256_loadu_ps(L.a.data() + 8 * k);
                        v = _mm256_add_ps(_mm256_max_ps(v, _mm256_setzero_ps()), _mm256_mul_ps(_mm256_min_ps(v, _mm256_setzero_ps()), sl));
                    }
                    _mm256_storeu_ps(op + 8 * k, v);
                }
            }
        }
}
bool HaveAvx2() {
    static const int k = [] { __builtin_cpu_init(); return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma") ? 1 : 0; }();
    return k != 0;
}
#endif

void Conv(const Layer& L, Act& in, Act& out) {
#ifdef HD_X86
    static const bool direct = getenv("MGO1_HD_DIRECT") != nullptr;   // tests: the direct form
    if (L.cin == 64 && L.cout == 64 && HaveAvx2()) { if (!L.wino.empty() && !direct) Conv64Wino(L, in, out); else Conv64Avx2(L, in, out); return; }
    if (L.cin == 64 && L.cout % 8 == 0 && L.cout <= 64 && HaveAvx2() && !direct) { Conv64ToN(L, in, out); return; }
#endif
    ConvGeneric(L, in, out);
}

// the model on an RGB tile (0..1, w x h): (w * scale) x (h * scale) RGB
void RunTile(const Model& m, const float* rgb, int w, int h, std::vector<float>& out) {
    Act a, b;
    a.Make(w, h, 3);
    for (int y = 0; y < h; ++y) memcpy(a.At(0, y), rgb + (size_t)y * w * 3, sizeof(float) * 3 * w);
    Act* in = &a; Act* nx = &b;
    for (const Layer& L : m.layers) {
        if (nx->w != w || nx->h != h || nx->c != L.cout) nx->Make(w, h, L.cout);
        Conv(L, *in, *nx);
        std::swap(in, nx);
    }
    const int s = m.scale, ow = w * s, oh = h * s;
    out.assign((size_t)ow * oh * 3, 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* v = in->At(x, y);
            const float* src = rgb + ((size_t)y * w + x) * 3;
            for (int c = 0; c < 3; ++c)
                for (int i = 0; i < s; ++i)
                    for (int j = 0; j < s; ++j)
                        out[((size_t)(y * s + i) * ow + (x * s + j)) * 3 + c] = v[c * s * s + i * s + j] + src[c];
        }
}

// Catmull-Rom weights (alpha's upscale)
inline float Cubic(float t) {
    t = fabsf(t);
    return t < 1 ? 1.5f * t * t * t - 2.5f * t * t + 1 : t < 2 ? -0.5f * t * t * t + 2.5f * t * t - 4 * t + 2 : 0;
}

// the whole texture: padded (wrapped or extended), cut in tiles, each tile with its margin
bool UpscaleWith(const Model& m, const std::vector<uint32_t>& rgba, int w, int h, bool repeat,
                 std::vector<uint32_t>& out, int& ow, int& oh, int threads, int maxSize) {
    const int s = m.scale, pad = 16, tile = 192;          // a tile's margin: the convolutions' edges stay out of it
    ow = w * s; oh = h * s;
    auto src = [&](int x, int y) -> uint32_t {
        if (repeat) { x = ((x % w) + w) % w; y = ((y % h) + h) % h; }
        else { x = std::clamp(x, 0, w - 1); y = std::clamp(y, 0, h - 1); }
        return rgba[(size_t)y * w + x];
    };
    // transparent texels take the colour of their visible neighbours (no dark fringes from them)
    std::vector<uint32_t> px(rgba);
    bool anyClear = false, anyVisible = false;
    for (uint32_t c : px) { if (c >> 24) anyVisible = true; else anyClear = true; }
    if (anyClear && anyVisible) {
        std::vector<uint8_t> known(px.size());
        for (size_t i = 0; i < px.size(); ++i) known[i] = (px[i] >> 24) != 0;
        for (int pass = 0; pass < 8; ++pass) {
            std::vector<uint8_t> k2(known);
            bool changed = false;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const size_t i = (size_t)y * w + x;
                    if (known[i]) continue;
                    int r = 0, g = 0, b = 0, n = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            int xx = x + dx, yy = y + dy;
                            if (repeat) { xx = (xx + w) % w; yy = (yy + h) % h; } else if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                            const size_t j = (size_t)yy * w + xx;
                            if (!known[j]) continue;
                            r += px[j] & 255; g += (px[j] >> 8) & 255; b += (px[j] >> 16) & 255; ++n;
                        }
                    if (n) { px[i] = (px[i] & 0xff000000u) | (uint32_t)(r / n) | (uint32_t)(g / n) << 8 | (uint32_t)(b / n) << 16; k2[i] = 1; changed = true; }
                }
            known.swap(k2);
            if (!changed) break;
        }
    }
    auto srcpx = [&](int x, int y) -> uint32_t {
        if (repeat) { x = ((x % w) + w) % w; y = ((y % h) + h) % h; }
        else { x = std::clamp(x, 0, w - 1); y = std::clamp(y, 0, h - 1); }
        return px[(size_t)y * w + x];
    };
    std::vector<uint32_t> big((size_t)ow * oh, 0);
    struct Tile { int x0, y0, w, h; };
    std::vector<Tile> tiles;
    for (int y = 0; y < h; y += tile) for (int x = 0; x < w; x += tile) tiles.push_back({x, y, std::min(tile, w - x), std::min(tile, h - y)});
    std::atomic<size_t> next{0};
    auto work = [&] {
        std::vector<float> in, res;
        for (size_t k; (k = next.fetch_add(1)) < tiles.size();) {
            const Tile& T = tiles[k];
            const int tw = T.w + 2 * pad, th = T.h + 2 * pad;
            in.resize((size_t)tw * th * 3);
            for (int y = 0; y < th; ++y)
                for (int x = 0; x < tw; ++x) {
                    const uint32_t c = srcpx(T.x0 + x - pad, T.y0 + y - pad);
                    float* p = &in[((size_t)y * tw + x) * 3];
                    p[0] = (c & 255) / 255.0f; p[1] = ((c >> 8) & 255) / 255.0f; p[2] = ((c >> 16) & 255) / 255.0f;
                }
            RunTile(m, in.data(), tw, th, res);
            const int rw = tw * s;
            for (int y = 0; y < T.h * s; ++y)
                for (int x = 0; x < T.w * s; ++x) {
                    const float* p = &res[((size_t)(y + pad * s) * rw + (x + pad * s)) * 3];
                    auto u8 = [](float v) { return (uint32_t)std::clamp((int)lrintf(v * 255.0f), 0, 255); };
                    big[(size_t)(T.y0 * s + y) * ow + (T.x0 * s + x)] = u8(p[0]) | u8(p[1]) << 8 | u8(p[2]) << 16;
                }
        }
    };
    const int n = std::max(1, std::min<int>(threads, (int)tiles.size()));
    if (n == 1) work();
    else { std::vector<std::thread> t; for (int i = 0; i < n; ++i) t.emplace_back(work); for (auto& x : t) x.join(); }
    // alpha: Catmull-Rom from the texture's own (the model sees colours only)
    bool flat = true; const uint32_t a0 = rgba[0] >> 24;
    for (uint32_t c : rgba) if ((c >> 24) != a0) { flat = false; break; }
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            uint32_t a = a0;
            if (!flat) {
                const float fx = (x + 0.5f) / s - 0.5f, fy = (y + 0.5f) / s - 0.5f;
                const int ix = (int)floorf(fx), iy = (int)floorf(fy);
                float sum = 0, wsum = 0;
                for (int j = -1; j <= 2; ++j)
                    for (int i = -1; i <= 2; ++i) {
                        const float wgt = Cubic(fx - (ix + i)) * Cubic(fy - (iy + j));
                        sum += wgt * (float)(src(ix + i, iy + j) >> 24); wsum += wgt;
                    }
                a = (uint32_t)std::clamp((int)lrintf(sum / wsum), 0, 255);
            }
            big[(size_t)y * ow + x] |= a << 24;
        }
    // larger than allowed: halved (box) until it fits
    while (maxSize > 0 && (ow > maxSize || oh > maxSize) && ow > 1 && oh > 1) {
        const int nw = ow / 2, nh = oh / 2;
        std::vector<uint32_t> sm((size_t)nw * nh);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                uint32_t acc[4] = {};
                for (int k = 0; k < 4; ++k) {
                    const uint32_t c = big[(size_t)(2 * y + k / 2) * ow + 2 * x + k % 2];
                    for (int ch = 0; ch < 4; ++ch) acc[ch] += (c >> (8 * ch)) & 255;
                }
                sm[(size_t)y * nw + x] = (acc[0] + 2) / 4 | ((acc[1] + 2) / 4) << 8 | ((acc[2] + 2) / 4) << 16 | ((acc[3] + 2) / 4) << 24;
            }
        big.swap(sm); ow = nw; oh = nh;
    }
    out.swap(big);
    return true;
}

// ---- the manager
struct Slot { uint64_t key = 0; int w = 0, h = 0; int state = 0; uint64_t print = 0; };   // state: 0 coming, 1 done, 2 failed;
                                                                                           // print: the image accepted (Print)
struct Job { int kind = 0; int slot = 0; uint64_t key = 0; int w = 0, h = 0; bool repeat = false; std::string path; std::vector<uint32_t> rgba; };
enum { kLoad = 0, kUpscale = 1, kDump = 2 };
std::mutex gM;
std::condition_variable gWake;
Settings gSet;
bool gIndexed = false;
std::unordered_map<uint64_t, std::string> gPack, gCache;
std::unordered_map<uint64_t, int> gKeys;
std::unordered_set<uint64_t> gDumped;                          // keys dumped (the dump once each)
std::unordered_map<uint64_t, uint64_t> gFirstSeen;       // keys waiting before an upscale: first seen (ms)
uint64_t NowMs() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::vector<Slot> gSlots(1);
std::deque<Job> gJobs;
std::deque<Image> gDone;
std::vector<std::thread> gWorkers;
int gUpscaleLimit = -1;                                        // upscales at once at most (-1: one a worker): SetUpscaleLimit
int gUpscaling = 0;                                            // upscales running now
Model gModel; bool gModelTried = false; std::mutex gModelM;
std::atomic<uint64_t> sLoaded{0}, sUpscaled{0}, sFailed{0}, sDumped{0}, sUpscaleMs{0}, sRefused{0};

bool KeyOfName(const std::string& stem, uint64_t& key) {
    if (stem.size() < 16) return false;
    uint64_t k = 0;
    for (int i = 0; i < 16; ++i) {
        const char c = stem[i]; int v;
        if (c >= '0' && c <= '9') v = c - '0'; else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; else if (c >= 'A' && c <= 'F') v = c - 'A' + 10; else return false;
        k = k << 4 | (uint64_t)v;
    }
    if (stem.size() > 16 && stem[16] != '-' && stem[16] != '_' && stem[16] != '.') return false;
    key = k; return true;
}
void Index(const std::string& dir, std::unordered_map<uint64_t, std::string>& out, bool skipMade) {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (skipMade && it->is_directory(ec)) {                          // the dump and a cache: not replacements
            const std::string n = it->path().filename().string();
            if (n == "dump" || n == "cache") { it.disable_recursion_pending(); continue; }
        }
        std::string e = it->path().extension().string();
        std::transform(e.begin(), e.end(), e.begin(), ::tolower);
        uint64_t k;
        if ((e == ".png" || e == ".hdt") && KeyOfName(it->path().stem().string(), k)) {
            auto x = out.find(k);                                      // a key's .hdt before its .png
            if (x == out.end()) out.emplace(k, it->path().string());
            else if (e == ".hdt") x->second = it->path().string();
        }
    }
}
// the PNG files hold alpha as images do (255: opaque), the GS 0x80: doubled on writing, halved on reading
std::vector<uint32_t> ToFile(const std::vector<uint32_t>& px) {
    std::vector<uint32_t> o(px);
    for (uint32_t& c : o) { const uint32_t a = c >> 24; c = (c & 0xffffffu) | std::min(255u, a * 2) << 24; }
    return o;
}
void FromFile(std::vector<uint32_t>& px) { for (uint32_t& c : px) c = (c & 0xffffffu) | ((c >> 24) + 1) / 2 << 24; }
std::string Hex(uint64_t k) { char b[20]; snprintf(b, sizeof b, "%016llx", (unsigned long long)k); return b; }

bool GetModel(const Model*& m) {
    std::lock_guard<std::mutex> l(gModelM);
    if (!gModelTried) {
        gModelTried = true;
        std::string path; { std::lock_guard<std::mutex> g(gM); path = gSet.modelPath; }
        if (LoadModel(path, gModel)) Say("upscaler %s: %d layers, x%d%s", path.c_str(), (int)gModel.layers.size(), gModel.scale,
#ifdef HD_X86
                                          HaveAvx2() ? ", AVX2" : ", no AVX2 (slower)"
#else
                                          ""
#endif
                                          );
        else Say("upscaler: %s", gModel.error.c_str());
    }
    m = &gModel;
    return gModel.scale > 0;
}

// ---- a pack's or the cache's image no more see-through than the disc's texture (docs/TEXTURES.md): a
// wall, foliage or smoke made transparent would show the players behind it. Opacity: the GS's alpha
// over 0x80 (more counts as opaque), the mean of each texel of the texture; refused past these losses
constexpr double kLossAll = 1.0 / 32;        // over the whole texture
constexpr double kLoss4 = 1.0 / 4;           // over any 4 x 4 of its texels (every 2)
constexpr double kLoss2 = 1.0 / 2;           // over any 2 x 2 (every one)
std::vector<uint8_t> AlphaOf(const std::vector<uint32_t>& px) {
    std::vector<uint8_t> a(px.size());
    for (size_t i = 0; i < px.size(); ++i) a[i] = (uint8_t)(px[i] >> 24);
    return a;
}
// a level's alpha per texel as the graphics card draws it (gs_gpu.cpp PullHd: a BC1 image's constAlpha
// in place of its texels'; without one, DXT1's punch-through texels are clear)
std::vector<uint8_t> LevelAlpha(const Image& im, size_t k, int lw, int lh) {
    std::vector<uint8_t> a((size_t)lw * lh, 0);
    const std::vector<uint8_t>& l = im.levels[k];
    if (im.format == kBc1 && im.constAlpha >= 0) { std::fill(a.begin(), a.end(), (uint8_t)std::min(im.constAlpha, 255)); return a; }
    if (im.format == kRgba8) {
        for (size_t i = 0; i < a.size() && 4 * i + 3 < l.size(); ++i) a[i] = l[4 * i + 3];
        return a;
    }
    const bool bc3 = im.format == kBc3;
    const int bw = (lw + 3) / 4, bh = (lh + 3) / 4, bs = bc3 ? 16 : 8;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const size_t at = ((size_t)by * bw + bx) * bs;
            if (at + bs > l.size()) return a;
            const uint8_t* b = l.data() + at;
            int v[16];
            if (bc3) {                                                     // (bcn.cpp Decode)
                const int a0 = b[0], a1 = b[1];
                int vals[8] = {a0, a1};
                if (a0 > a1) for (int q = 1; q <= 6; ++q) vals[q + 1] = ((7 - q) * a0 + q * a1 + 3) / 7;
                else { for (int q = 1; q <= 4; ++q) vals[q + 1] = ((5 - q) * a0 + q * a1 + 2) / 5; vals[6] = 0; vals[7] = 255; }
                uint64_t bits = 0; for (int q = 0; q < 6; ++q) bits |= (uint64_t)b[2 + q] << (8 * q);
                for (int i = 0; i < 16; ++i) v[i] = vals[(bits >> (3 * i)) & 7];
            } else {
                const uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
                uint32_t bits; memcpy(&bits, b + 4, 4);
                for (int i = 0; i < 16; ++i) v[i] = c0 <= c1 && ((bits >> (2 * i)) & 3) == 3 ? 0 : 255;
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < lw && y < lh) a[(size_t)y * lw + x] = (uint8_t)v[i];
            }
        }
    return a;
}
// the mean opacity (0 to 1) of a w x h alpha in each cell of a gw x gh grid (gw <= w, gh <= h)
std::vector<float> Cells(const std::vector<uint8_t>& a, int w, int h, int gw, int gh) {
    std::vector<uint64_t> sum((size_t)gw * gh, 0);
    std::vector<uint32_t> n((size_t)gw * gh, 0);
    for (int y = 0; y < h; ++y) {
        const size_t row = (size_t)((int64_t)y * gh / h) * gw;
        for (int x = 0; x < w; ++x) {
            const size_t c = row + (size_t)((int64_t)x * gw / w);
            sum[c] += std::min<uint32_t>(a[(size_t)y * w + x], 128); ++n[c];
        }
    }
    std::vector<float> out(sum.size(), 0.0f);
    for (size_t i = 0; i < out.size(); ++i) if (n[i]) out[i] = (float)((double)sum[i] / (128.0 * n[i]));
    return out;
}
// false (why) when b, on a's grid, lost more opacity than the limits; wrap: the texture repeats
bool Kept(const std::vector<float>& a, const std::vector<float>& b, int gw, int gh, bool wrap, std::string& why) {
    std::vector<float> d(a.size());
    double all = 0;
    for (size_t i = 0; i < d.size(); ++i) { d[i] = a[i] - b[i]; all += d[i]; }
    all /= (double)d.size();
    char t[128];
    if (all > kLossAll) { snprintf(t, sizeof t, "%.2f less opaque over the whole texture", all); why = t; return false; }
    struct Win { int k, step; double limit; };
    for (const Win& wn : {Win{4, 2, kLoss4}, Win{2, 1, kLoss2}}) {
        const int kx = std::min(wn.k, gw), ky = std::min(wn.k, gh);
        auto starts = [&](int g, int k) {
            std::vector<int> s;
            if (k >= g) { s.push_back(0); return s; }
            const int last = wrap ? g - 1 : g - k;
            for (int v = 0; v <= last; v += wn.step) s.push_back(v);
            if (!wrap && s.back() != last) s.push_back(last);
            return s;
        };
        const std::vector<int> xs = starts(gw, kx), ys = starts(gh, ky);
        for (int y0 : ys)
            for (int x0 : xs) {
                double s = 0;
                for (int y = 0; y < ky; ++y)
                    for (int x = 0; x < kx; ++x) s += d[(size_t)((y0 + y) % gh) * gw + (size_t)((x0 + x) % gw)];
                s /= kx * ky;
                if (s > wn.limit) {
                    snprintf(t, sizeof t, "%.2f less opaque over its %d x %d texels at %d, %d", s, kx, ky, x0, y0);
                    why = t; return false;
                }
            }
    }
    return true;
}
// the replacement's alpha (W x H) against the texture's (w x h), on the coarser grid of the two
bool OpacityKept(const std::vector<uint8_t>& tex, int w, int h, const std::vector<uint8_t>& rep, int W, int H, bool wrap, std::string& why) {
    if (w <= 0 || h <= 0 || W <= 0 || H <= 0 || tex.size() < (size_t)w * h || rep.size() < (size_t)W * H) { why = "no texels"; return false; }
    const int gw = std::min(w, W), gh = std::min(h, H);
    return Kept(Cells(tex, w, h, gw, gh), Cells(rep, W, H, gw, gh), gw, gh, wrap, why);
}
// an HD image against the texture: its first level, then each other one against the first's own
// mipmaps (2 x 2 averages, bcn::HalfSize), as Prepare makes them
bool ImageKept(const std::vector<uint8_t>& tex, int w, int h, bool wrap, const Image& im, std::string& why) {
    if (im.levels.empty() || im.w <= 0 || im.h <= 0) { why = "no image"; return false; }
    std::vector<uint8_t> ref = LevelAlpha(im, 0, im.w, im.h);
    if (!OpacityKept(tex, w, h, ref, im.w, im.h, wrap, why)) return false;
    int rw = im.w, rh = im.h;
    for (size_t k = 1; k < im.levels.size(); ++k) {
        const int nw = std::max(1, rw / 2), nh = std::max(1, rh / 2);
        std::vector<uint8_t> half((size_t)nw * nh);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                int s = 0;
                for (int q = 0; q < 4; ++q) s += ref[(size_t)std::min(2 * y + q / 2, rh - 1) * rw + (size_t)std::min(2 * x + q % 2, rw - 1)];
                half[(size_t)y * nw + x] = (uint8_t)((s + 2) / 4);
            }
        ref.swap(half); rw = nw; rh = nh;
        if (!OpacityKept(ref, rw, rh, LevelAlpha(im, k, rw, rh), rw, rh, wrap, why)) { why = "level " + std::to_string(k) + ": " + why; return false; }
    }
    return true;
}
// what an image is (a reload must bring the one accepted)
uint64_t Print(const Image& im) {
    const int32_t head[4] = {im.w, im.h, im.format, im.constAlpha};
    uint64_t h = Hash64(head, sizeof head);
    for (const auto& l : im.levels) h = Hash64(l.data(), l.size(), h);
    return h ? h : 1;
}

std::atomic<int> gDoneCount{0};
void Finish(int slot, Image&& im, bool ok) {
    const uint64_t print = ok ? Print(im) : 0;
    std::lock_guard<std::mutex> l(gM);
    if (slot > 0 && slot < (int)gSlots.size()) { gSlots[slot].state = ok ? 1 : 2; if (ok) gSlots[slot].print = print; }
    if (ok) { im.slot = slot; gDone.push_back(std::move(im)); ++gDoneCount; }
}
void Refuse(const std::string& path, int slot, const std::string& why) {
    ++sRefused;
    Say("%s: refused, more see-through than the disc's texture (%s): the disc's texture kept",
        std::filesystem::path(path).filename().string().c_str(), why.c_str());
    Finish(slot, Image(), false);
}

void RunJob(Job& j) {
    if (j.kind == kDump) {
        std::error_code ec; std::filesystem::create_directories(std::filesystem::path(j.path).parent_path(), ec);
        if (!std::filesystem::exists(j.path, ec) && PngWrite(j.path, ToFile(j.rgba), j.w, j.h, true)) ++sDumped;
        return;
    }
    if (j.kind == kLoad) {
        Image im;
        std::string why;
        const bool first = !j.rgba.empty();                  // a first load has the texture's texels; a reload, the print
        const bool hdtFile = j.path.size() > 4 && j.path.compare(j.path.size() - 4, 4, ".hdt") == 0;
        if (hdtFile) {
            if (!LoadHdImage(j.path, im)) { ++sFailed; Say("%s: not read", j.path.c_str()); Finish(j.slot, Image(), false); return; }
            if (first && !ImageKept(AlphaOf(j.rgba), j.w, j.h, j.repeat, im, why)) { Refuse(j.path, j.slot, why); return; }
        } else {
            std::vector<uint32_t> px; int w = 0, h = 0; std::string err;
            if (!PngRead(j.path, px, w, h, err)) { ++sFailed; Say("%s: %s", j.path.c_str(), err.c_str()); Finish(j.slot, Image(), false); return; }
            FromFile(px);
            if (first && !OpacityKept(AlphaOf(j.rgba), j.w, j.h, AlphaOf(px), w, h, j.repeat, why)) { Refuse(j.path, j.slot, why); return; }
            im = Prepare(px, w, h);
        }
        if (!first) {
            uint64_t want = 0;
            { std::lock_guard<std::mutex> l(gM); if (j.slot > 0 && j.slot < (int)gSlots.size()) want = gSlots[j.slot].print; }
            if (!want || want != Print(im)) { Refuse(j.path, j.slot, "not the image checked at its first load"); return; }
        }
        ++sLoaded;
        if (hdtFile) { Finish(j.slot, std::move(im), true); return; }
        // a PNG of the cache (v128) made the compressed file it is now
        std::string cache; { std::lock_guard<std::mutex> l(gM); cache = gSet.cacheDir; }
        const std::filesystem::path pp(j.path);
        std::error_code ec;
        if (!cache.empty() && std::filesystem::equivalent(pp.parent_path(), std::filesystem::path(cache), ec)) {
            const std::string hdt = cache + "/" + Hex(j.key) + ".hdt";
            if (SaveHdImage(hdt + ".tmp", im)) {
                std::filesystem::rename(hdt + ".tmp", hdt, ec);
                std::filesystem::remove(pp, ec);
                std::lock_guard<std::mutex> l(gM); gCache[j.key] = hdt;
            }
        }
        Finish(j.slot, std::move(im), true);
        return;
    }
    const Model* m = nullptr;
    if (!GetModel(m)) { ++sFailed; Finish(j.slot, Image(), false); return; }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint32_t> out; int ow = 0, oh = 0; int maxSize, threads; std::string cache;
    { std::lock_guard<std::mutex> l(gM); maxSize = gSet.maxSize; cache = gSet.cacheDir; threads = gSet.sync ? std::max(1, (int)std::thread::hardware_concurrency()) : 1; }
    UpscaleWith(*m, j.rgba, j.w, j.h, j.repeat, out, ow, oh, threads, maxSize);
    sUpscaleMs += (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    ++sUpscaled;
    Image im = Prepare(out, ow, oh);
    if (!cache.empty()) {                                  // kept for the next runs
        std::error_code ec; std::filesystem::create_directories(cache, ec);
        const std::string path = cache + "/" + Hex(j.key) + ".hdt";
        if (SaveHdImage(path + ".tmp", im)) { std::filesystem::rename(path + ".tmp", path, ec); std::lock_guard<std::mutex> l(gM); gCache[j.key] = path; }
    }
    Finish(j.slot, std::move(im), true);
}
// the next job a worker may take (gM held): the first, an upscale only under the limit (loads and dumps are cheap)
std::deque<Job>::iterator NextJob() {
    const bool upscale = gUpscaleLimit < 0 || gUpscaling < gUpscaleLimit;
    for (auto it = gJobs.begin(); it != gJobs.end(); ++it)
        if (it->kind != kUpscale || upscale) return it;
    return gJobs.end();
}
void Worker() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    // EcoQoS (Windows 10 1709 and later): the efficient cores of a hybrid processor, a lower clock elsewhere
    struct { ULONG version, controlMask, stateMask; } eco = {1, 1, 1};    // THREAD_POWER_THROTTLING_EXECUTION_SPEED
    typedef BOOL(WINAPI * SetInfo)(HANDLE, int, LPVOID, DWORD);
    if (auto f = (SetInfo)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadInformation"))
        f(GetCurrentThread(), 3 /* ThreadPowerThrottling */, &eco, sizeof eco);
#endif
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> l(gM);
            gWake.wait(l, [] { return NextJob() != gJobs.end(); });
            auto it = NextJob();
            j = std::move(*it); gJobs.erase(it);
            if (j.kind == kUpscale) ++gUpscaling;
        }
        const bool up = j.kind == kUpscale;
        RunJob(j);
        if (up) { std::lock_guard<std::mutex> l(gM); --gUpscaling; gWake.notify_all(); }
    }
}
void StartWorkers() {
    if (!gWorkers.empty()) return;
    // half the logical processors less one: the game's own threads keep theirs (and their cores' other half)
    int n = gSet.threads > 0 ? gSet.threads : (int)std::thread::hardware_concurrency() / 2 - 1;
    n = std::clamp(n, 1, 32);
    for (int i = 0; i < n; ++i) { gWorkers.emplace_back(Worker); gWorkers.back().detach(); }
    gWorkers.resize(n);                                     // detached: the count stays
}
// (gM held) three textures decoded on the GS thread in a 16 ms window at most: an upscale's, a load's check
bool DecodeTurn(uint64_t now) {
    static uint64_t window = 0; static int used = 0;
    if (now - window >= 16) { window = now; used = 0; }
    if (used >= 3) return false;
    ++used;
    return true;
}

}  // namespace

void Configure(const Settings& s) {
    std::lock_guard<std::mutex> l(gM);
    const bool reindex = !gIndexed || s.packDir != gSet.packDir || s.cacheDir != gSet.cacheDir;
    const bool newModel = s.modelPath != gSet.modelPath;
    gSet = s;
    if (reindex) {
        gPack.clear(); gCache.clear();
        if (s.pack) Index(s.packDir, gPack, true);
        Index(s.cacheDir, gCache, false);
        gIndexed = true;
        Say("pack %s: %d images; cache %s: %d images", s.packDir.c_str(), (int)gPack.size(), s.cacheDir.c_str(), (int)gCache.size());
    }
    if (newModel) { std::lock_guard<std::mutex> g(gModelM); gModelTried = false; gModel = Model(); }
    // keys seen while an option was off get their chance again
    for (auto it = gKeys.begin(); it != gKeys.end();) { if (it->second == 0) it = gKeys.erase(it); else ++it; }
    if ((s.pack || s.upscale || s.dump) && !s.sync) StartWorkers();
}
Settings Current() { std::lock_guard<std::mutex> l(gM); return gSet; }
void SetUpscaleLimit(int n) {
    std::lock_guard<std::mutex> l(gM);
    if (n == gUpscaleLimit) return;
    gUpscaleLimit = n;
    gWake.notify_all();
}
int UpscaleLimit() { std::lock_guard<std::mutex> l(gM); return gUpscaleLimit; }
int PackCount() { std::lock_guard<std::mutex> l(gM); return (int)gPack.size(); }

int SlotOf(uint64_t key, int w, int h, bool repeat, Decode decode, void* user) {
    std::unique_lock<std::mutex> l(gM);
    auto it = gKeys.find(key);
    if (it != gKeys.end()) return it->second;
    const bool dump = gSet.dump && !gSet.dumpDir.empty() && gDumped.insert(key).second;   // once a key
    Job j; j.key = key; j.w = w; j.h = h; j.repeat = repeat; j.kind = -1;
    bool settled = true;                                     // the answer is the key's for good
    auto p = gSet.pack ? gPack.find(key) : gPack.end();
    if (p != gPack.end()) { j.kind = kLoad; j.path = p->second; }
    else if (gSet.upscale) {
        auto c = gCache.find(key);
        if (c != gCache.end()) { j.kind = kLoad; j.path = c->second; }
        else if (gSet.sync) j.kind = kUpscale;
        else {
            // an upscale only for a texture still drawn half a second after it was first seen: what
            // the game draws once (a changing texture, a transition) is not worth one
            const uint64_t now = NowMs();
            auto f = gFirstSeen.find(key);
            if (f == gFirstSeen.end()) { if (gFirstSeen.size() > 200000) gFirstSeen.clear(); gFirstSeen.emplace(key, now); settled = false; }
            else if (now - f->second < 500) settled = false;
            else if (!DecodeTurn(now)) settled = false;
            else { gFirstSeen.erase(f); j.kind = kUpscale; }
        }
    }
    // a pack's or the cache's image is checked against the texture's texels, decoded here too
    if (j.kind == kLoad && !gSet.sync && !DecodeTurn(NowMs())) { j.kind = -1; settled = false; }
    int slot = 0;
    if (j.kind >= 0) { slot = (int)gSlots.size(); Slot s; s.key = key; s.w = w; s.h = h; gSlots.push_back(s); j.slot = slot; }
    if (settled) gKeys[key] = slot;
    const std::string dumpPath = dump ? gSet.dumpDir + "/" + Hex(key) + ".png" : "";
    const bool sync = gSet.sync;
    l.unlock();
    std::vector<uint32_t> px;
    if ((dump || j.kind == kUpscale || j.kind == kLoad) && !(decode && decode(user, px, w, h) && px.size() >= (size_t)w * h)) {
        if (slot) { std::lock_guard<std::mutex> g(gM); gSlots[slot].state = 2; }
        return 0;
    }
    Job d;
    if (dump) { d.kind = kDump; d.w = w; d.h = h; d.path = dumpPath; d.rgba = px; }
    if (j.kind == kUpscale || j.kind == kLoad) j.rgba = std::move(px);
    if (sync) {                                              // tests: done now, on this thread
        if (dump) RunJob(d);
        if (j.kind >= 0) RunJob(j);
        return slot;
    }
    l.lock();
    if (dump) gJobs.push_back(std::move(d));
    if (j.kind >= 0) gJobs.push_back(std::move(j));
    l.unlock();
    gWake.notify_all();
    return settled ? slot : -1;
}

bool CanReload(int slot) {
    std::lock_guard<std::mutex> l(gM);
    if (slot <= 0 || slot >= (int)gSlots.size()) return false;
    const uint64_t k = gSlots[slot].key;
    return (gSet.pack && gPack.count(k)) || gCache.count(k);
}
void Reload(int slot) {
    std::unique_lock<std::mutex> l(gM);
    if (slot <= 0 || slot >= (int)gSlots.size()) return;
    const uint64_t k = gSlots[slot].key;
    auto p = gSet.pack ? gPack.find(k) : gPack.end();
    std::string path = p != gPack.end() ? p->second : "";
    if (path.empty()) { auto c = gCache.find(k); if (c == gCache.end()) return; path = c->second; }
    Job j; j.kind = kLoad; j.slot = slot; j.key = k; j.path = path;
    gSlots[slot].state = 0;
    if (gSet.sync) { l.unlock(); RunJob(j); return; }
    gJobs.push_back(std::move(j));
    l.unlock();
    gWake.notify_all();
}
bool TakeFinished(Image& out) {
    if (!gDoneCount.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> l(gM);
    if (gDone.empty()) return false;
    out = std::move(gDone.front()); gDone.pop_front(); --gDoneCount;
    return true;
}

std::string Stats() {
    std::lock_guard<std::mutex> l(gM);
    size_t upscales = 0;
    for (const Job& j : gJobs) if (j.kind == kUpscale) ++upscales;
    char b[340];
    snprintf(b, sizeof b, "%d textures seen, %d with an image coming or shown; %llu loaded, %llu upscaled (%.1f s), %llu failed, %llu refused (see-through), %llu dumped; %d upscales waiting",
             (int)gKeys.size(), (int)gSlots.size() - 1, (unsigned long long)sLoaded.load(), (unsigned long long)sUpscaled.load(), sUpscaleMs.load() / 1000.0,
             (unsigned long long)sFailed.load(), (unsigned long long)sRefused.load(), (unsigned long long)sDumped.load(), (int)upscales);
    return b;
}

Image Prepare(const std::vector<uint32_t>& rgba, int w, int h) {
    Image im; im.w = w; im.h = h;
    const uint32_t a0 = rgba.empty() ? 0 : rgba[0] >> 24;
    bool flat = true;
    for (uint32_t c : rgba) if ((c >> 24) != a0) { flat = false; break; }
    im.format = flat ? kBc1 : kBc3;
    im.constAlpha = flat ? (int)a0 : -1;
    std::vector<uint32_t> lvl = rgba; int lw = w, lh = h;
    for (;;) {
        if (flat) for (uint32_t& c : lvl) c |= 0xff000000u;         // BC1: opaque blocks (the alpha is constAlpha)
        im.levels.push_back(bcn::Encode(lvl, lw, lh, !flat));
        if (lw == 1 && lh == 1) break;
        int nw, nh; lvl = bcn::HalfSize(lvl, lw, lh, nw, nh); lw = nw; lh = nh;
    }
    return im;
}
// .hdt: "MGO1HDT1", u32 w, h, format, levels, i32 constAlpha, then each level's u32 size and bytes
bool SaveHdImage(const std::string& path, const Image& im) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t hdr[5] = {(uint32_t)im.w, (uint32_t)im.h, (uint32_t)im.format, (uint32_t)im.levels.size(), (uint32_t)im.constAlpha};
    bool ok = fwrite("MGO1HDT1", 1, 8, f) == 8 && fwrite(hdr, 4, 5, f) == 5;
    for (const auto& l : im.levels) { const uint32_t n = (uint32_t)l.size(); ok = ok && fwrite(&n, 4, 1, f) == 1 && fwrite(l.data(), 1, n, f) == n; }
    return fclose(f) == 0 && ok;
}
bool LoadHdImage(const std::string& path, Image& im) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[8]; uint32_t hdr[5];
    bool ok = fread(magic, 1, 8, f) == 8 && !memcmp(magic, "MGO1HDT1", 8) && fread(hdr, 4, 5, f) == 5 && hdr[3] > 0 && hdr[3] <= 16 &&
              hdr[0] > 0 && hdr[1] > 0 && hdr[0] <= 16384 && hdr[1] <= 16384;
    if (ok) {
        im = Image(); im.w = (int)hdr[0]; im.h = (int)hdr[1]; im.format = (int)hdr[2]; im.constAlpha = (int)hdr[4];
        int lw = im.w, lh = im.h;
        for (uint32_t k = 0; k < hdr[3] && ok; ++k) {
            uint32_t n = 0;
            const size_t want = im.format == kRgba8 ? (size_t)lw * lh * 4 : (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * (im.format == kBc3 ? 16 : 8);
            ok = fread(&n, 4, 1, f) == 1 && n == want;
            if (ok) { std::vector<uint8_t> l(n); ok = fread(l.data(), 1, n, f) == n; im.levels.push_back(std::move(l)); }
            lw = std::max(1, lw / 2); lh = std::max(1, lh / 2);
        }
    }
    fclose(f);
    return ok;
}

bool Upscale(const std::string& modelPath, const std::vector<uint32_t>& rgba, int w, int h, bool repeat,
             std::vector<uint32_t>& out, int& ow, int& oh, int threads) {
    Model m;
    if (!LoadModel(modelPath, m)) { Say("upscaler: %s", m.error.c_str()); return false; }
    if (threads <= 0) threads = std::max(1, (int)std::thread::hardware_concurrency());
    return UpscaleWith(m, rgba, w, h, repeat, out, ow, oh, threads, 0);
}

}  // namespace hdtex
}  // namespace ps2
