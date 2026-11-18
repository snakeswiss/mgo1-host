// gs_gpu.cpp - the GS on the graphics card (gs_gpu.h). The local memory is a 1024 x 1024 R32UI
// texture: the shaders read textures and CLUTs from it through the PS2's layouts. Frame buffers
// are RGBA16F textures at Scale() times the PS2's pixels, wider in a widescreen picture (colour / 255, alpha / 128, so blending
// by As, Ad or FIX uses the fixed-function factors), depth buffers D32F with depth = z / 2^bits.
// Each 8 KB page of the local memory is either current in the CPU copy, in the GPU copy, or
// newer in a frame buffer: pages move between them only when something reads them.
//
// Two sides: gs.cpp's thread queues commands in a ring (Draw, Transfer, Present...), the render
// thread, which owns the GL context, runs them in order. The queue side remembers which pages
// the GPU may have drawn (gDrawnPages), so a CPU access to other pages never waits.
#include "gs_gpu.h"
#include "gs_gpu_cmd.h"                 // the settings, PageSet, the ring's commands, the Vulkan backend (gs_vk.cpp)
#include "gl_api.h"
#include "hdtex.h"
#include "bcn.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace ps2 {
void GsSwizzleTables(uint16_t lut[68][32]);         // gs.cpp: the block and column tables
uint32_t GsPixelAddress(uint32_t psm, int x, int y, uint32_t bp, uint32_t bw);   // gs.cpp: a pixel's address (its mode's units)
extern void (*gLog)(const char* fmt, ...);          // the platform's log (null in tests)

namespace gsgpu {
namespace {

void Say(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (gLog) gLog("gpu: %s", b); else fprintf(stderr, "gpu: %s\n", b);
}

glapi::Api gl;
PFNGLDISPATCHCOMPUTEPROC gDispatch = nullptr;
PFNGLMEMORYBARRIERPROC gBarrier = nullptr;
PFNGLUNIFORM1UIPROC gUniform1ui = nullptr;
PFNGLBINDIMAGETEXTUREPROC gBindImage = nullptr;
PFNGLGENERATEMIPMAPPROC gGenMipmap = nullptr;
PFNGLGENSAMPLERSPROC gGenSamplers = nullptr;
PFNGLBINDSAMPLERPROC gBindSampler = nullptr;
PFNGLSAMPLERPARAMETERIPROC gSamplerI = nullptr;
PFNGLSAMPLERPARAMETERFPROC gSamplerF = nullptr;
PFNGLCOMPRESSEDTEXIMAGE2DPROC gCompressedTexImage = nullptr;
bool gS3tc = false;                    // the driver samples BC1 / BC3 (EXT_texture_compression_s3tc)
std::atomic<bool> gOn{false};
int gScale = 1;                        // GL pixels a PS2 pixel: down (and across in a 4:3 picture)
int gScaleX = 1;                       // across (a widescreen picture shows the 512 columns wider)
std::atomic<bool> gVk{false};          // the Vulkan backend runs the commands (gs_vk.cpp)
std::atomic<bool> gDead{false};        // Vulkan stopped and OpenGL could not take over: commands let go (FromVulkan)
uint32_t* gVram = nullptr;
Host gHost;
std::string gDescription;              // the driver's version and renderer (Start gives it back)

inline uint32_t Bits(uint64_t v, int lo, int n) { return (uint32_t)((v >> lo) & ((1ull << n) - 1)); }

// ---- the pages of the local memory (PageSet: gs_gpu_cmd.h)
// the geometry of a buffer layout: page width and height in pixels, and the pages a row of pages takes
struct Layout { int pw, ph; uint32_t bwp; };
Layout LayoutOf(uint32_t psm, uint32_t bw) {
    switch (psm & 0x3f) {
    case 0x02: case 0x0a: case 0x32: case 0x3a: return {64, 64, bw};
    case 0x13: return {128, 64, bw >> 1};
    case 0x14: return {128, 128, bw >> 1};
    default: return {64, 32, bw};
    }
}
// the pages under a rectangle (x0..x1, y0..y1 inclusive) of a buffer at block bp (stamp: each one's entry set to v)
void PagesOf(PageSet& s, uint32_t bp, uint32_t bw, uint32_t psm, int x0, int y0, int x1, int y1, uint32_t* stamp = nullptr, uint32_t v = 0) {
    if (x1 < x0 || y1 < y0) return;
    const Layout L = LayoutOf(psm, bw ? bw : 1);
    const uint32_t base = bp >> 5; const bool spill = (bp & 31) != 0;
    const uint32_t bwp = std::max(1u, L.bwp);
    x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(x1, 2047); y1 = std::min(y1, 2047);
    for (int py = y0 / L.ph; py <= y1 / L.ph; ++py)
        for (int px = x0 / L.pw; px <= x1 / L.pw; ++px) {
            const uint32_t pg = base + (uint32_t)py * bwp + (uint32_t)px;
            s.Set(pg); if (spill) s.Set(pg + 1);
            if (stamp) { stamp[pg & 511] = v; if (spill) stamp[(pg + 1) & 511] = v; }
        }
}
// the pages of a texture (all its texels) and of the batch's box so far, each kept from the last primitive (most
// primitives use their predecessor's texture and box): DrawNow's page tests without PagesOf each time
const PageSet& TexPages(uint32_t tbp, uint32_t tbw, uint32_t tpsm, int tw, int th) {
    static uint32_t key[5] = {~0u, 0, 0, 0, 0};
    static PageSet p;
    const uint32_t k[5] = {tbp, tbw, tpsm, (uint32_t)tw, (uint32_t)th};
    if (memcmp(k, key, sizeof k)) { p = PageSet{}; PagesOf(p, tbp, tbw, tpsm, 0, 0, tw - 1, th - 1); memcpy(key, k, sizeof k); }
    return p;
}
const PageSet& BoxPages(uint32_t bp, uint32_t bw, uint32_t psm, int x0, int y0, int x1, int y1) {
    static uint32_t key[7] = {~0u, 0, 0, 0, 0, 0, 0};
    static PageSet p;
    const uint32_t k[7] = {bp, bw, psm, (uint32_t)x0, (uint32_t)y0, (uint32_t)x1, (uint32_t)y1};
    if (memcmp(k, key, sizeof k)) { p = PageSet{}; PagesOf(p, bp, bw, psm, x0, y0, x1, y1); memcpy(key, k, sizeof k); }
    return p;
}
// the box a primitive may draw (PS2 pixels, in its scissor); false when it draws nothing
bool PrimBox(int kind, const Regs& r, const Vtx& a, const Vtx& b, const Vtx& c, int& bx0, int& by0, int& bx1, int& by1) {
    int scx0 = Bits(r.scissor, 0, 11), scx1 = Bits(r.scissor, 16, 11), scy0 = Bits(r.scissor, 32, 11), scy1 = Bits(r.scissor, 48, 11);
    const uint32_t fbw = Bits(r.frame, 16, 6);
    if (scx1 < scx0 || scy1 < scy0 || !fbw) return false;
    scx1 = std::min(scx1, (int)fbw * 64 - 1);
    const float ox = Bits(r.xyoffset, 0, 16) / 16.0f, oy = Bits(r.xyoffset, 32, 16) / 16.0f;
    const Vtx* v[3] = {&a, &b, &c};
    const int nv = kind == 1 ? 3 : 2;
    float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
    for (int k = 0; k < nv; ++k) { const float x = v[k]->x / 16.0f - ox, y = v[k]->y / 16.0f - oy; lx = std::min(lx, x); hx = std::max(hx, x); ly = std::min(ly, y); hy = std::max(hy, y); }
    bx0 = std::max((int)floorf(lx) - 1, scx0); bx1 = std::min((int)ceilf(hx) + 1, scx1);
    by0 = std::max((int)floorf(ly) - 1, scy0); by1 = std::min((int)ceilf(hy) + 1, scy1);
    return bx0 <= bx1 && by0 <= by1;
}

// ---- shaders
GLuint Compile(GLenum type, const char* src, std::string& err) {
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &src, nullptr);
    gl.CompileShader(s);
    GLint ok = 0; gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096] = {0}; gl.GetShaderInfoLog(s, sizeof log, nullptr, log); err = log; gl.DeleteShader(s); return 0; }
    return s;
}
GLuint Program(const char* vs, const char* fs, std::string& err) {
    GLuint v = Compile(GL_VERTEX_SHADER, vs, err); if (!v) { err = "vertex: " + err; return 0; }
    GLuint f = Compile(GL_FRAGMENT_SHADER, fs, err); if (!f) { err = "fragment: " + err; return 0; }
    GLuint p = gl.CreateProgram();
    gl.AttachShader(p, v); gl.AttachShader(p, f); gl.LinkProgram(p);
    GLint ok = 0; gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096] = {0}; gl.GetProgramInfoLog(p, sizeof log, nullptr, log); err = std::string("link: ") + log; return 0; }
    return p;
}

// the local memory and its layouts, as the fragment shaders read them
const char* kMemGlsl = R"(
layout(binding = 0) uniform usampler2D uMem;       // the local memory: 1024 x 1024 words
layout(binding = 2) uniform usampler2D uLut;       // the block and column tables (gs.cpp)
uint lut(int row, uint col) { return texelFetch(uLut, ivec2(int(col), row), 0).r; }
uint memWord(uint w) { return texelFetch(uMem, ivec2(int(w & 1023u), int((w >> 10) & 1023u)), 0).r; }
uint addr32(uint x, uint y, uint bp, uint bw, uint zx) {
    uint bn = bp + ((y >> 5) * bw + (x >> 6)) * 32u + (lut(int((y >> 3) & 3u), (x >> 3) & 7u) ^ zx);
    return ((bn & 0x3fffu) << 6) + lut(20 + int(y & 7u), x & 7u);
}
uint addr16(uint x, uint y, uint bp, uint bw, uint zx, bool s) {
    uint blk = s ? lut(12 + int((y >> 3) & 7u), (x >> 4) & 3u) : lut(4 + int((y >> 3) & 7u), (x >> 4) & 3u);
    uint bn = bp + ((y >> 6) * bw + (x >> 6)) * 32u + (blk ^ zx);
    return ((bn & 0x3fffu) << 7) + lut(28 + int(y & 7u), x & 15u);
}
uint addr8(uint x, uint y, uint bp, uint bw) {
    uint bn = bp + ((y >> 6) * (bw >> 1) + (x >> 7)) * 32u + lut(int((y >> 4) & 3u), (x >> 4) & 7u);
    return ((bn & 0x3fffu) << 8) + lut(36 + int(y & 15u), x & 15u);
}
uint addr4(uint x, uint y, uint bp, uint bw) {
    uint bn = bp + ((y >> 7) * (bw >> 1) + (x >> 7)) * 32u + lut(4 + int((y >> 4) & 7u), (x >> 5) & 3u);
    return ((bn & 0x3fffu) << 9) + lut(52 + int(y & 15u), x & 31u);
}
vec4 unpack32(uint c) { return vec4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), float(c >> 24)); }
)";

const char* kDrawVs = R"(#version 430 core
layout(location = 0) in vec2 aPos;           // GS pixels, offset applied, +0.5 (GL samples at pixel centres)
layout(location = 1) in uint aZ;
layout(location = 2) in vec4 aColor;         // 0..255
layout(location = 3) in vec3 aStq;
layout(location = 4) in float aFog;
uniform vec2 uPosScale;                      // 2 * scale / the buffer's size in GL pixels
uniform float uZScale;
uniform uint uZMax;
out vec4 vColor;
flat out vec4 vColorFlat;
noperspective out vec3 vStq;
out float vFog;
void main() {
    gl_Position = vec4(aPos * uPosScale - 1.0, float(min(aZ, uZMax)) * uZScale, 1.0);
    vColor = aColor; vColorFlat = aColor; vStq = aStq; vFog = aFog;
}
)";

const char* kDrawFs = R"(
layout(binding = 1) uniform usampler2D uClut;      // 512 x 1 words
layout(binding = 3) uniform sampler2D uRtTex;      // a frame buffer read as a texture
layout(binding = 5) uniform sampler2D uHdTex;      // the texture's HD image (RGBA8: the GS's values, alpha 0x80 = 1.0)
uniform ivec2 uHd;         // 1: sample uHdTex; its alpha (a BC1 image: the GS's value), -1: the image's
uniform ivec4 uTex0;       // tbp, tbw, psm, fromRt
uniform ivec4 uTexSize;    // tw, th, bilinear, fst
uniform ivec4 uWrap;       // wms, wmt, iip, fba
uniform ivec4 uRegion;     // minu, maxu, minv, maxv
uniform ivec4 uTexFx;      // tme, tfx, tcc, c16 (CLUT entries in 16 bits)
uniform ivec4 uTexA;       // ta0, ta1, aem, csa
uniform ivec4 uFog;        // fge, fog colour r g b
uniform ivec4 uAtest;      // ate, atst, aref, pass (0 every fragment, 1 the passing ones, 2 the failing ones)
uniform vec4 uRtSample;    // frame buffer as texture: 1 / its size in texels (x, y), its scale (x, y)
uniform vec2 uSrcF;        // a blend GL has no factor for: 1 the colour F (Cd (1 + F): DST_COLOR, ONE), 2 the colour
                           // times 1 + F (Cs (1 + F)); F the source alpha, or y when y >= 0 (FIX)
in vec4 vColor;
flat in vec4 vColorFlat;
noperspective in vec3 vStq;
in float vFog;
layout(location = 0) out vec4 oColor;

vec4 expand16(uint c) {
    uint a = (c & 0x8000u) != 0u ? uint(uTexA.y) : ((uTexA.z != 0 && (c & 0x7fffu) == 0u) ? 0u : uint(uTexA.x));
    return vec4(float((c & 0x1fu) << 3), float(((c >> 5) & 0x1fu) << 3), float(((c >> 10) & 0x1fu) << 3), float(a));
}
vec4 clutEntry(uint i) {
    uint e = texelFetch(uClut, ivec2(int(i & 511u), 0), 0).r;
    return uTexFx.w != 0 ? expand16(e & 0xffffu) : unpack32(e);
}
vec4 texel(int x, int y) {
    uint ux = uint(x) & 2047u, uy = uint(y) & 2047u, bp = uint(uTex0.x), bw = uint(uTex0.y);
    int psm = uTex0.z;
    if (psm == 0x00) return unpack32(memWord(addr32(ux, uy, bp, bw, 0u)));
    if (psm == 0x01) { uint v = memWord(addr32(ux, uy, bp, bw, 0u)) & 0xffffffu; uint a = (uTexA.z != 0 && v == 0u) ? 0u : uint(uTexA.x); return unpack32(v | (a << 24)); }
    if (psm == 0x02 || psm == 0x0a) { uint h = addr16(ux, uy, bp, bw, 0u, psm == 0x0a); return expand16((memWord(h >> 1) >> ((h & 1u) * 16u)) & 0xffffu); }
    if (psm == 0x13) { uint b = addr8(ux, uy, bp, bw); return clutEntry((memWord(b >> 2) >> ((b & 3u) * 8u)) & 255u); }
    if (psm == 0x1b) return clutEntry(memWord(addr32(ux, uy, bp, bw, 0u)) >> 24);
    uint i;
    if (psm == 0x14) { uint n = addr4(ux, uy, bp, bw); i = (memWord(n >> 3) >> ((n & 7u) * 4u)) & 15u; }
    else if (psm == 0x24) i = (memWord(addr32(ux, uy, bp, bw, 0u)) >> 24) & 15u;
    else if (psm == 0x2c) i = memWord(addr32(ux, uy, bp, bw, 0u)) >> 28;
    else return unpack32(memWord(addr32(ux, uy, bp, bw, psm >= 0x30 ? 0x18u : 0u)));
    uint csaMask = uTexFx.w != 0 ? 0x1fu : 0x0fu;
    return clutEntry(((uint(uTexA.w) & csaMask) * 16u + i) & 511u);
}
int wrapc(int v, int mode, int size, int mn, int mx) {
    if (mode == 0) return v & (size - 1);
    if (mode == 1) return v < 0 ? 0 : (v > size - 1 ? size - 1 : v);
    if (mode == 2) return v < mn ? mn : (v > mx ? mx : v);
    return (v & mn) | mx;
}
vec4 fetchWrapped(int x, int y) {
    return texel(wrapc(x, uWrap.x, uTexSize.x, uRegion.x, uRegion.y), wrapc(y, uWrap.y, uTexSize.y, uRegion.z, uRegion.w));
}
// a frame buffer read as a texture: its own (upscaled) pixels, through the wrap mode (region
// repeat never comes here), as the texture's storage mode reads them
vec4 sampleRt(vec2 uv) {
    vec2 size = vec2(uTexSize.xy), lo = vec2(0.0), hi = size;
    if (uWrap.x == 0) uv.x = mod(uv.x, size.x); else if (uWrap.x == 2) { lo.x = float(uRegion.x); hi.x = float(uRegion.y) + 1.0; }
    if (uWrap.y == 0) uv.y = mod(uv.y, size.y); else if (uWrap.y == 2) { lo.y = float(uRegion.z); hi.y = float(uRegion.w) + 1.0; }
    vec4 c;
    if (uTexSize.z == 0) {
        vec2 q = uv;
        if (uWrap.x != 0) q.x = clamp(q.x, lo.x, hi.x - 0.5 / uRtSample.z);
        if (uWrap.y != 0) q.y = clamp(q.y, lo.y, hi.y - 0.5 / uRtSample.w);
        c = texelFetch(uRtTex, ivec2(floor(q * uRtSample.zw)), 0);
    } else {
        vec2 q = uv;
        if (uWrap.x != 0) q.x = clamp(q.x, lo.x + 0.5, hi.x - 0.5);
        if (uWrap.y != 0) q.y = clamp(q.y, lo.y + 0.5, hi.y - 0.5);
        c = textureLod(uRtTex, q * uRtSample.xy, 0.0);
    }
    c = clamp(c, 0.0, 2.0) * vec4(255.0, 255.0, 255.0, 128.0);
    c.rgb = min(c.rgb, vec3(255.0));
    int tpsm = uTex0.z;
    bool black = c.r + c.g + c.b < 0.5;
    if (tpsm == 0x01) c.a = (uTexA.z != 0 && black) ? 0.0 : float(uTexA.x);
    else if (tpsm == 0x02 || tpsm == 0x0a) c.a = c.a >= 127.5 ? float(uTexA.y) : ((uTexA.z != 0 && black) ? 0.0 : float(uTexA.x));
    return c;
}
// the HD image over the texture's tw x th texels: filtered (mipmaps), the wrap mode in its coordinates
// (repeat by the sampler; clamp and region clamp kept half an HD texel inside)
vec4 sampleHd(vec2 uv) {
    vec2 size = vec2(uTexSize.xy), lo = vec2(0.0), hi = size;
    vec2 k = vec2(textureSize(uHdTex, 0)) / size;
    if (uWrap.x == 2) { lo.x = float(uRegion.x); hi.x = float(uRegion.y) + 1.0; }
    if (uWrap.y == 2) { lo.y = float(uRegion.z); hi.y = float(uRegion.w) + 1.0; }
    if (uWrap.x != 0) uv.x = clamp(uv.x, lo.x + 0.5 / k.x, hi.x - 0.5 / k.x);
    if (uWrap.y != 0) uv.y = clamp(uv.y, lo.y + 0.5 / k.y, hi.y - 0.5 / k.y);
    vec4 c = texture(uHdTex, uv / size) * 255.0;
    if (uHd.y >= 0) c.a = float(uHd.y);
    return c;
}
vec4 sampleTex(vec2 uv) {
    if (uTex0.w != 0) return sampleRt(uv);
    if (uHd.x != 0) return sampleHd(uv);
    if (uTexSize.z == 0) { ivec2 p = ivec2(floor(uv)); return fetchWrapped(p.x, p.y); }
    uv -= 0.5;
    ivec2 p = ivec2(floor(uv)); vec2 f = uv - vec2(p);
    vec4 c00 = fetchWrapped(p.x, p.y), c10 = fetchWrapped(p.x + 1, p.y);
    vec4 c01 = fetchWrapped(p.x, p.y + 1), c11 = fetchWrapped(p.x + 1, p.y + 1);
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}
void main() {
    vec4 c = floor((uWrap.z != 0 ? vColor : vColorFlat) + 0.001);
    if (uTexFx.x != 0) {
        vec2 uv = uTexSize.w != 0 ? vStq.xy : vec2(vStq.x / (vStq.z != 0.0 ? vStq.z : 1e-10) * float(uTexSize.x),
                                                     vStq.y / (vStq.z != 0.0 ? vStq.z : 1e-10) * float(uTexSize.y));
        vec4 t = sampleTex(uv);
        if (uTexFx.y == 0) { c.rgb = floor(t.rgb * c.rgb / 128.0); if (uTexFx.z != 0) c.a = floor(t.a * c.a / 128.0); }
        else if (uTexFx.y == 1) { c.rgb = t.rgb; if (uTexFx.z != 0) c.a = t.a; }
        else if (uTexFx.y == 2) { c.rgb = floor(t.rgb * c.rgb / 128.0) + c.a; if (uTexFx.z != 0) c.a = c.a + t.a; }
        else { c.rgb = floor(t.rgb * c.rgb / 128.0) + c.a; if (uTexFx.z != 0) c.a = t.a; }
        c = min(c, 255.0);
    }
    if (uFog.x != 0) { float f = floor(vFog + 0.001); c.rgb = floor((f * c.rgb + (255.0 - f) * vec3(uFog.yzw)) / 256.0); }
    int a = int(c.a);
    if (uAtest.w != 0) {
        bool pass;
        int r = uAtest.z;
        switch (uAtest.y) {
        case 0: pass = false; break; case 1: pass = true; break;
        case 2: pass = a < r; break; case 3: pass = a <= r; break;
        case 4: pass = a == r; break; case 5: pass = a >= r; break;
        case 6: pass = a > r; break; default: pass = a != r; break;
        }
        if (uAtest.w == 1 && !pass) discard;
        if (uAtest.w == 2 && pass) discard;
    }
    float f = uSrcF.y >= 0.0 ? uSrcF.y : float(a) / 128.0;
    if (uWrap.w != 0) a |= 0x80;
    oColor = vec4(c.rgb / 255.0, float(a) / 128.0);
    if (uSrcF.x == 1.0) oColor.rgb = vec3(f);
    else if (uSrcF.x == 2.0) oColor.rgb *= 1.0 + f;
}
)";

// memory -> frame buffer: the buffer's own pixels read from the local memory (its storage mode)
const char* kLoadVs = R"(#version 430 core
layout(location = 0) in vec2 aPos;           // GL pixels of the target
uniform vec2 uTarget;                        // the target's size in GL pixels
void main() { gl_Position = vec4(aPos / uTarget * 2.0 - 1.0, 0.0, 1.0); }
)";
const char* kLoadFs = R"(
uniform ivec4 uBuf;        // bp, bw, psm, scale across
uniform int uScaleY;       // scale down
layout(location = 0) out vec4 oColor;
void main() {
    uvec2 p = uvec2(gl_FragCoord.xy) / uvec2(uint(uBuf.w), uint(uScaleY));
    uint x = p.x & 2047u, y = p.y & 2047u, bp = uint(uBuf.x), bw = uint(uBuf.y);
    int psm = uBuf.z;
    vec4 c;
    if (psm == 0x02 || psm == 0x0a) {
        uint h = addr16(x, y, bp, bw, 0u, psm == 0x0a);
        uint v = (memWord(h >> 1) >> ((h & 1u) * 16u)) & 0xffffu;
        c = vec4(float((v & 0x1fu) << 3), float(((v >> 5) & 0x1fu) << 3), float(((v >> 10) & 0x1fu) << 3), (v & 0x8000u) != 0u ? 128.0 : 0.0);
    } else {
        // CT24 too: the word's high byte kept in alpha (a CT24 draw leaves it, a CT32 draw into the same buffer
        // writes it, and the store puts it back: the game's byte tricks read it as T8H)
        c = unpack32(memWord(addr32(x, y, bp, bw, 0u)));
    }
    oColor = vec4(c.rgb / 255.0, c.a / 128.0);
}
)";

// frame buffer -> memory: each PS2 pixel of a rectangle (the first of its block of GL pixels)
// written into the local memory in the buffer's storage mode
const char* kStoreCs = R"(#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0, r32ui) uniform uimage2D uMemImg;
layout(binding = 2) uniform usampler2D uLut;
layout(binding = 3) uniform sampler2D uRt;
uniform ivec4 uBuf;        // bp, bw, psm, scale across
uniform int uScaleY;       // scale down
uniform ivec4 uRect;       // x, y, w, h (PS2 pixels)
uint lut(int row, uint col) { return texelFetch(uLut, ivec2(int(col), row), 0).r; }
uint addr32(uint x, uint y, uint bp, uint bw, uint zx) {
    uint bn = bp + ((y >> 5) * bw + (x >> 6)) * 32u + (lut(int((y >> 3) & 3u), (x >> 3) & 7u) ^ zx);
    return ((bn & 0x3fffu) << 6) + lut(20 + int(y & 7u), x & 7u);
}
uint addr16(uint x, uint y, uint bp, uint bw, uint zx, bool s) {
    uint blk = s ? lut(12 + int((y >> 3) & 7u), (x >> 4) & 3u) : lut(4 + int((y >> 3) & 7u), (x >> 4) & 3u);
    uint bn = bp + ((y >> 6) * bw + (x >> 6)) * 32u + (blk ^ zx);
    return ((bn & 0x3fffu) << 7) + lut(28 + int(y & 7u), x & 15u);
}
ivec2 at(uint w) { return ivec2(int(w & 1023u), int((w >> 10) & 1023u)); }
void main() {
    ivec2 g = ivec2(gl_GlobalInvocationID.xy);
    if (g.x >= uRect.z || g.y >= uRect.w) return;
    uint x = uint(uRect.x + g.x), y = uint(uRect.y + g.y);
    vec4 c = texelFetch(uRt, ivec2(int(x) * uBuf.w, int(y) * uScaleY), 0);
    uvec4 v = uvec4(clamp(round(c * vec4(255.0, 255.0, 255.0, 128.0)), 0.0, 255.0));
    uint bp = uint(uBuf.x), bw = uint(uBuf.y);
    int psm = uBuf.z;
    if (psm == 0x02 || psm == 0x0a) {
        uint h = addr16(x & 2047u, y & 2047u, bp, bw, 0u, psm == 0x0a);
        uint p = (v.r >> 3) | ((v.g >> 3) << 5) | ((v.b >> 3) << 10) | ((v.a & 0x80u) != 0u ? 0x8000u : 0u);
        uint sh = (h & 1u) * 16u;
        imageAtomicAnd(uMemImg, at(h >> 1), ~(0xffffu << sh));
        imageAtomicOr(uMemImg, at(h >> 1), p << sh);
    } else {
        // CT24 as CT32: its alpha is the word's high byte as loaded (CT24 draws mask it) or as a CT32 draw into
        // this buffer wrote it (MGS3's colour filter: its effects buffer, made CT24, gets CT32 tiles; their high
        // bytes lost turned the next picture's filter into yellow bars at the left in first person)
        uint w = addr32(x & 2047u, y & 2047u, bp, bw, 0u);
        uint p = v.r | (v.g << 8) | (v.b << 16);
        imageStore(uMemImg, at(w), uvec4(p | (v.a << 24)));
    }
}
)";

// host -> local: the pixels of a transfer (as the game sent them, bits per pixel in raster
// order) written into the local memory in the destination's storage mode
const char* kXferCs = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0, r32ui) uniform uimage2D uMemImg;
layout(binding = 4) uniform usampler2D uData;      // the transfer's words, 1024 a row
uniform ivec4 uDst;        // bp, bw, psm, bits per pixel
uniform ivec4 uRect;       // x, y, w, h
uniform int uCount;        // pixels in the data
ivec2 at(uint w) { return ivec2(int(w & 1023u), int((w >> 10) & 1023u)); }
uint dataWord(uint k) { return texelFetch(uData, ivec2(int(k & 1023u), int(k >> 10)), 0).r; }
void put(uint w, uint keep, uint v) {
    ivec2 a = at(w);
    imageAtomicAnd(uMemImg, a, keep);
    imageAtomicOr(uMemImg, a, v & ~keep);
}
void main() {
    ivec2 g = ivec2(gl_GlobalInvocationID.xy);
    if (g.x >= uRect.z || g.y >= uRect.w) return;
    int i = g.y * uRect.z + g.x;
    if (i >= uCount) return;
    uint bpp = uint(uDst.w), bit = uint(i) * bpp, k = bit >> 5, sh = bit & 31u;
    uint v = dataWord(k) >> sh;
    if (sh + bpp > 32u) v |= dataWord(k + 1u) << (32u - sh);
    if (bpp < 32u) v &= (1u << bpp) - 1u;
    uint x = uint(uRect.x + g.x) & 2047u, y = uint(uRect.y + g.y) & 2047u, bp = uint(uDst.x), bw = uint(uDst.y);
    int psm = uDst.z;
    uint zx = (psm & 0x30) == 0x30 ? 0x18u : 0u;
    if (psm == 0x00 || psm == 0x30) imageStore(uMemImg, at(addr32(x, y, bp, bw, zx)), uvec4(v));
    else if (psm == 0x01 || psm == 0x31) put(addr32(x, y, bp, bw, zx), 0xff000000u, v);
    else if (psm == 0x02 || psm == 0x0a || psm == 0x32 || psm == 0x3a) {
        uint h = addr16(x, y, bp, bw, zx, (psm & 0x0f) == 0x0a);
        uint s = (h & 1u) * 16u;
        put(h >> 1, ~(0xffffu << s), v << s);
    } else if (psm == 0x13) { uint b = addr8(x, y, bp, bw); uint s = (b & 3u) * 8u; put(b >> 2, ~(0xffu << s), v << s); }
    else if (psm == 0x14) { uint n = addr4(x, y, bp, bw); uint s = (n & 7u) * 4u; put(n >> 3, ~(0xfu << s), v << s); }
    else if (psm == 0x1b) put(addr32(x, y, bp, bw, 0u), 0x00ffffffu, v << 24);
    else if (psm == 0x24) put(addr32(x, y, bp, bw, 0u), 0xf0ffffffu, v << 24);
    else if (psm == 0x2c) put(addr32(x, y, bp, bw, 0u), 0x0fffffffu, v << 28);
}
)";

// a CLUT load: entries read from the local memory into the CLUT buffer (gs.cpp's LoadClut)
const char* kClutCs = R"(
layout(local_size_x = 64) in;
layout(binding = 1, r32ui) uniform writeonly uimage2D uClutImg;
uniform ivec4 uClut0;      // cbp, cpsm, csm, entries
uniform ivec4 uClut1;      // bw, x0, y0, slot
void main() {
    int i = int(gl_GlobalInvocationID.x);
    if (i >= uClut0.w) return;
    int x, y;
    if (uClut0.z == 0) {
        if (uClut0.w == 256) { int j = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); x = j & 15; y = j >> 4; }
        else { x = i & 7; y = i >> 3; }
    } else { x = uClut1.y + i; y = uClut1.z; }
    uint bp = uint(uClut0.x), bw = uint(uClut1.x), ux = uint(x) & 2047u, uy = uint(y) & 2047u;
    uint v;
    if (uClut0.y == 0x02 || uClut0.y == 0x0a) { uint h = addr16(ux, uy, bp, bw, 0u, uClut0.y == 0x0a); v = (memWord(h >> 1) >> ((h & 1u) * 16u)) & 0xffffu; }
    else v = memWord(addr32(ux, uy, bp, bw, 0u));
    int slot = uClut1.w + i;
    if (slot < 512) imageStore(uClutImg, ivec2(slot, 0), uvec4(v));
}
)";

// the picture on the window: a rectangle of a frame buffer, filtered to the window's pixels
const char* kPresentVs = R"(#version 430 core
uniform vec4 uDst;         // x0, y0, x1, y1: where it goes (NDC, y0 its top)
uniform vec4 uSrc;         // u0, v0, u1, v1: the texels shown (v0 the first line)
out vec2 vUv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(mix(uDst.xy, uDst.zw, c), 0.0, 1.0);
    vUv = mix(uSrc.xy, uSrc.zw, c);
}
)";
const char* kPresentFs = R"(#version 430 core
layout(binding = 0) uniform sampler2D uPic;
uniform vec2 uStep;        // texels per window pixel
uniform int uMode;         // 0 nearest, 1 smooth (the texels under each window pixel averaged)
in vec2 vUv;
layout(location = 0) out vec4 oColor;
void main() {
    vec2 size = vec2(textureSize(uPic, 0));
    vec3 c;
    if (uMode == 0) c = texelFetch(uPic, ivec2(floor(vUv)), 0).rgb;
    else {
        ivec2 n = ivec2(clamp(ceil(uStep - 0.01), vec2(1.0), vec2(8.0)));
        c = vec3(0.0);
        for (int j = 0; j < n.y; ++j)
            for (int i = 0; i < n.x; ++i) {
                vec2 o = (vec2(float(i), float(j)) + 0.5) / vec2(n) - 0.5;
                c += textureLod(uPic, (vUv + o * uStep) / size, 0.0).rgb;
            }
        c /= float(n.x * n.y);
    }
    oColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";
const char* kMenuFs = R"(#version 430 core
layout(binding = 0) uniform sampler2D uMenu;
in vec2 vUv;
layout(location = 0) out vec4 oColor;
void main() { oColor = texelFetch(uMenu, ivec2(floor(vUv)), 0); }
)";
const char* kOverlayFs = R"(#version 430 core
layout(binding = 0) uniform sampler2D uMask;
uniform vec4 uColor;
in vec2 vUv;
layout(location = 0) out vec4 oColor;
void main() { oColor = vec4(uColor.rgb, uColor.a * texelFetch(uMask, ivec2(floor(vUv)), 0).r); }
)";

// ---- frame and depth buffers (render thread)
struct Buf {
    int id;
    uint32_t bp, bw, psm;              // block, width in 64 pixels, storage mode
    bool depth;
    GLuint tex = 0;
    int w = 0, h = 0;                  // PS2 pixels
    int sx = 1, sy = 1;                // GL pixels a PS2 pixel (across, down): the renderer's scale, or 1 for the game's own effects
    PageSet owned;                     // pages newer here than in the GPU copy of the memory
    PageSet pages;                     // the pages it covers
    PageSet stale;                     // pages another buffer drew since this one last had them
    // its shadow: what 1:1 copies of a picture at a higher scale drew into it, at that scale (the scopes' lens: the
    // picture copied into an effects buffer at the PS2's scale, drawn back through the lens: blurred at a high
    // resolution); sampled in its place by a draw at that scale while no other draw, load or transfer changed the pages
    GLuint shadow = 0;
    int shx = 1, shy = 1;
    PageSet shadowPages;
};
std::vector<std::unique_ptr<Buf>> gBufs;
int gNextId = 1;
int gOwner[512];                       // page -> the buffer newer than the memory (0: none)
PageSet gOwned;                        // the pages gOwner gives a buffer
PageSet gGpuNewer;                     // GPU copy newer than the CPU copy
std::map<std::pair<GLuint, GLuint>, GLuint> gFbos;

GLuint gMem = 0, gClut = 0, gLut = 0, gProg = 0, gLoadProg = 0, gStoreProg = 0, gPresentProg = 0, gOverlayProg = 0;
GLuint gVao = 0, gVbo = 0, gEmptyVao = 0, gScratch = 0, gOverlayTex = 0, gXferProg = 0, gClutProg = 0, gXferData = 0;
GLuint gMenuProg = 0, gMenuTex = 0;
int gScratchW = 0, gScratchH = 0;
struct Loc { GLint posScale, zScale, zMax, tex0, texSize, wrap, region, texFx, texA, fog, atest, rtSample, hd, srcF; } L;
// the HD images by slot (render thread), their samplers
struct HdImage { GLuint tex = 0; int w = 0, h = 0; uint64_t bytes = 0, lastUse = 0; int constAlpha = -1; bool evicted = false, asked = false; };
uint64_t gHdBytes = 0;                 // the HD images on the graphics card (their level 0 and mipmaps)
uint64_t gPictureNo = 0;               // pictures shown (the HD images' last use)
std::vector<HdImage> gHd;
GLuint gHdSamplers[4] = {};
std::atomic<uint64_t> sHdImages{0}, sHdBytes{0}, sHdDraws{0};
struct LoadLoc { GLint target, buf, scaleY; } LL;
struct StoreLoc { GLint buf, rect, scaleY; } SL;
struct PresentLoc { GLint dst, src, step, mode; } PL;
struct OverlayLoc { GLint dst, src, color; } OL;
struct MenuLoc { GLint dst, src; } ML;
struct XferLoc { GLint dst, rect, count; } XL;
struct ClutLoc { GLint c0, c1; } CL;
constexpr GLbitfield kMemBarriers = GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT;
// counters (render thread; read for the log)
std::atomic<uint64_t> sDraws{0}, sBatches{0}, sUploads{0}, sStores{0}, sLoads{0}, sDownloads{0}, sLoadPx{0}, sPresents{0}, sWaits{0};
std::atomic<uint64_t> sWaitUs{0};
std::atomic<int> sBufs{0};

uint64_t gBufsGen = 1;                 // gBufs changed: a buffer made or resized (FindBuf's and TexSource's answers kept till then)
Buf* FindBuf(uint32_t bp, uint32_t bw, uint32_t psm, bool depth, int sx = 0, int sy = 0) {
    struct Memo { uint64_t gen = 0; uint32_t bp, bw, psm; bool depth; int sx, sy; Buf* b; };
    static Memo memo[4]; static unsigned next = 0;
    for (const Memo& m : memo)
        if (m.gen == gBufsGen && m.bp == bp && m.bw == bw && m.psm == psm && m.depth == depth && m.sx == sx && m.sy == sy) return m.b;
    Buf* found = nullptr;
    for (auto& b : gBufs)
        if (b->bp == bp && b->bw == bw && b->depth == depth && (!sx || (b->sx == sx && b->sy == sy)) &&
            (depth || (b->psm & 0x0f) == (psm & 0x0f) || ((b->psm & 0x3f) <= 1 && (psm & 0x3f) <= 1))) { found = b.get(); break; }
    memo[next++ & 3] = Memo{gBufsGen, bp, bw, psm, depth, sx, sy, found};
    return found;
}
// the buffer a texture at tbp / tbw in mode tm reads as a frame buffer (its own storage mode, CT24 from a 32-bit one)
Buf* TexSource(uint32_t tbp, uint32_t tbw, uint32_t tm) {
    static uint64_t gen = 0; static uint32_t key[3] = {}; static Buf* last = nullptr;
    if (gen == gBufsGen && key[0] == tbp && key[1] == tbw && key[2] == tm) return last;
    Buf* src = nullptr;
    for (auto& bb : gBufs) {
        if (bb->depth || bb->bp != tbp || bb->bw != tbw) continue;
        const uint32_t bm = bb->psm & 0x3f;
        if ((bm == tm && (tm <= 2 || tm == 0x0a)) || (tm == 1 && bm == 0)) { src = bb.get(); break; }
    }
    gen = gBufsGen; key[0] = tbp; key[1] = tbw; key[2] = tm; last = src;
    return src;
}
GLuint Fbo(GLuint color, GLuint depth) {
    auto it = gFbos.find({color, depth});
    if (it != gFbos.end()) return it->second;
    GLuint f = 0; gl.GenFramebuffers(1, &f);
    gl.BindFramebuffer(GL_FRAMEBUFFER, f);
    if (color) gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth) gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    const GLenum db = color ? GL_COLOR_ATTACHMENT0 : GL_NONE;
    gl.DrawBuffers(1, &db);
    gFbos[{color, depth}] = f;
    return f;
}
void DropFbos(GLuint tex) {
    for (auto it = gFbos.begin(); it != gFbos.end();) {
        if (it->first.first == tex || it->first.second == tex) { gl.DeleteFramebuffers(1, &it->second); it = gFbos.erase(it); }
        else ++it;
    }
}

// ---- the batch: primitives with the same state, drawn in one call
struct GV { float x, y; uint32_t z; uint8_t r, g, b, a; float s, t, q, fog; };
static_assert(sizeof(GV) == 32, "vertex layout");
struct State {
    Buf* rt = nullptr; Buf* zb = nullptr;
    int scx0 = 0, scy0 = 0, scx1 = 0, scy1 = 0;
    bool zte = false; int ztst = 0; bool zmsk = true; uint32_t zmax = 0; float zscale = 0;
    bool blend = false; GLenum eq = GL_FUNC_ADD, sf = GL_ONE, df = GL_ZERO; float fix = 0; int srcF = 0; float srcFix = -1;
    bool maskR = false, maskG = false, maskB = false, maskA = false;
    int passes = 1; int failPass = 0;      // alpha test: 1 pass, or 2 (the failing fragments drawn as AFAIL says)
    // shader state
    int tex0[4] = {}, texSize[4] = {}, wrap[4] = {}, region[4] = {}, texFx[4] = {}, texA[4] = {}, fog[4] = {}, atest[4] = {};
    float rtSample[4] = {};
    Buf* texRt = nullptr;
    bool texShadow = false;                  // texRt's shadow sampled (its scale in rtSample)
    GLuint hd = 0; int hdSampler = 0;        // the HD image and its sampler (0 repeat, 1 clamp across, 2 down, 3 both)
    int hdAlpha = -1;                        // a BC1 image's alpha (the GS's), -1: the image's own
    bool operator==(const State& o) const {
        return rt == o.rt && zb == o.zb && scx0 == o.scx0 && scy0 == o.scy0 && scx1 == o.scx1 && scy1 == o.scy1 && zte == o.zte &&
               ztst == o.ztst && zmsk == o.zmsk && zmax == o.zmax && blend == o.blend && eq == o.eq && sf == o.sf && df == o.df && srcF == o.srcF && srcFix == o.srcFix &&
               fix == o.fix && maskR == o.maskR && maskG == o.maskG && maskB == o.maskB && maskA == o.maskA && passes == o.passes &&
               failPass == o.failPass && !memcmp(tex0, o.tex0, sizeof tex0) && !memcmp(texSize, o.texSize, sizeof texSize) &&
               !memcmp(wrap, o.wrap, sizeof wrap) && !memcmp(region, o.region, sizeof region) && !memcmp(texFx, o.texFx, sizeof texFx) &&
               !memcmp(texA, o.texA, sizeof texA) && !memcmp(fog, o.fog, sizeof fog) && !memcmp(atest, o.atest, sizeof atest) &&
               texRt == o.texRt && texShadow == o.texShadow && !memcmp(rtSample, o.rtSample, sizeof rtSample) && hd == o.hd &&
               hdSampler == o.hdSampler && hdAlpha == o.hdAlpha;
    }
};
State gState; bool gHaveState = false;
bool gBatchCopy = false;                     // every primitive of the batch copies its frame buffer texture 1:1
std::vector<GV> gVerts;
int gBx0 = 1 << 30, gBy0 = 1 << 30, gBx1 = -1, gBy1 = -1;   // the batch's box (PS2 pixels, in the scissor)

// the pages of a buffer's rectangle, as page rectangles of that buffer (x, y, w, h in PS2 pixels)
template <class F> void PageRects(const Buf& b, const PageSet& pages, F&& f) {
    const Layout Lb = LayoutOf(b.psm, b.bw);
    const uint32_t base = b.bp >> 5, bwp = std::max(1u, Lb.bwp);
    const int rows = (b.h + Lb.ph - 1) / Lb.ph, cols = (int)bwp;
    for (int py = 0; py < rows; ++py)
        for (int px = 0; px < cols; ++px) {
            const uint32_t pg = (base + (uint32_t)py * bwp + (uint32_t)px) & 511;
            if (pages.Has(pg)) f(px * Lb.pw, py * Lb.ph, Lb.pw, Lb.ph);
        }
}
// a buffer's newer pages written into the GPU copy of the memory
void Store(Buf& b) {
    if (!b.owned.Any() || b.depth) { b.owned = PageSet{}; return; }
    static const bool debug = getenv("MGO1_GPU_DEBUG") != nullptr;
    if (debug) {
        int n = 0; for (int k = 0; k < 8; ++k) n += __builtin_popcountll(b.owned.w[k]);
        fprintf(stderr, "store %x/%u psm %x: %d pages (batch %llu)\n", b.bp, b.bw, b.psm, n, (unsigned long long)sBatches.load());
    }
    gl.UseProgram(gStoreProg);
    gl.Uniform4i(SL.buf, (GLint)b.bp, (GLint)b.bw, (GLint)b.psm, b.sx);
    gl.Uniform1i(SL.scaleY, b.sy);
    gl.ActiveTexture(GL_TEXTURE3); gl.BindTexture(GL_TEXTURE_2D, b.tex);
    gl.ActiveTexture(GL_TEXTURE2); gl.BindTexture(GL_TEXTURE_2D, gLut);
    PageRects(b, b.owned, [&](int x, int y, int w, int h) {
        w = std::min(w, b.w - x); h = std::min(h, b.h - y);
        if (w <= 0 || h <= 0) return;
        gl.Uniform4i(SL.rect, x, y, w, h);
        gDispatch((GLuint)((w + 7) / 8), (GLuint)((h + 7) / 8), 1);
    });
    gBarrier(kMemBarriers);
    for (int p = 0; p < 512; ++p) if (b.owned.Has(p)) { if (gOwner[p] == b.id) { gOwner[p] = 0; gOwned.Clear(p); } gGpuNewer.Set(p); }
    b.owned = PageSet{};
    ++sStores;
}
// the GPU copy of these pages current: the buffers newer there stored
void StoreOwnersOf(const PageSet& pages, const Buf* except) {
    for (auto& b : gBufs) if (b.get() != except && b->owned.Meets(pages)) Store(*b);
}
void Load(Buf& b, int x, int y, int w, int h);
// the pages of b another buffer drew since b had them: their owners store them, b loads them
void Freshen(Buf& b, const PageSet& pages) {
    if (b.depth) return;
    PageSet need; for (int k = 0; k < 8; ++k) need.w[k] = pages.w[k] & b.stale.w[k];
    if (!need.Any()) return;
    StoreOwnersOf(need, &b);
    PageRects(b, need, [&](int x, int y, int w, int h) { Load(b, x, y, w, h); });
    b.stale.Remove(need);
    b.shadowPages.Remove(need);
}
// b draws these pages: it owns them, the other buffers over them are stale there (and no shadow holds them)
void TakePages(Buf& b, const PageSet& drawn) {
    for (auto& o : gBufs) if (o->shadowPages.Meets(drawn)) o->shadowPages.Remove(drawn);
    for (int k = 0; k < 8; ++k) {
        uint64_t m = drawn.w[k];
        while (m) {
            const int p = k * 64 + __builtin_ctzll(m); m &= m - 1;
            if (gOwner[p] == b.id) continue;
            if (gOwner[p]) for (auto& o : gBufs) if (o->id == gOwner[p]) o->owned.Clear(p);
            gOwner[p] = b.id; gOwned.Set(p); b.owned.Set(p); gGpuNewer.Clear(p);
            for (auto& o : gBufs) if (o.get() != &b && !o->depth && o->pages.Has(p)) o->stale.Set(p);
        }
    }
}
// a buffer's rectangle loaded from the GPU copy of the memory (upscaled)
void Load(Buf& b, int x, int y, int w, int h) {
    if (b.depth) return;
    x = std::max(0, x); y = std::max(0, y); w = std::min(w, b.w - x); h = std::min(h, b.h - y);
    if (w <= 0 || h <= 0) return;
    static const bool debug = getenv("MGO1_GPU_DEBUG") != nullptr;
    if (debug) fprintf(stderr, "load %x/%u psm %x: %d,%d %dx%d (batch %llu)\n", b.bp, b.bw, b.psm, x, y, w, h, (unsigned long long)sBatches.load());
    const int Sx = b.sx, Sy = b.sy;
    gl.BindFramebuffer(GL_FRAMEBUFFER, Fbo(b.tex, 0));
    gl.Viewport(0, 0, b.w * Sx, b.h * Sy);
    gl.Disable(GL_SCISSOR_TEST); gl.Disable(GL_BLEND); gl.Disable(GL_DEPTH_TEST);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.UseProgram(gLoadProg);
    gl.Uniform2f(LL.target, (float)(b.w * Sx), (float)(b.h * Sy));
    gl.Uniform4i(LL.buf, (GLint)b.bp, (GLint)b.bw, (GLint)b.psm, Sx);
    gl.Uniform1i(LL.scaleY, Sy);
    gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, gMem);
    gl.ActiveTexture(GL_TEXTURE2); gl.BindTexture(GL_TEXTURE_2D, gLut);
    const float X0 = (float)(x * Sx), Y0 = (float)(y * Sy), X1 = (float)((x + w) * Sx), Y1 = (float)((y + h) * Sy);
    const float q[12] = {X0, Y0, X1, Y0, X0, Y1, X1, Y0, X1, Y1, X0, Y1};
    GV v[6] = {};
    for (int k = 0; k < 6; ++k) { v[k].x = q[2 * k]; v[k].y = q[2 * k + 1]; }
    gl.BindVertexArray(gVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, gVbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, 6);
    ++sLoads; sLoadPx += (uint64_t)w * h;
}
// the CPU copy of these pages current (the caller waits for it)
void Download(const PageSet& pages) {
    PageSet need = gGpuNewer; for (int k = 0; k < 8; ++k) need.w[k] &= pages.w[k];
    if (!need.Any()) return;
    static GLuint rf = 0;
    if (!rf) { gl.GenFramebuffers(1, &rf); }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, rf);
    gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gMem, 0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    BlockTimer bt;                                                       // the read back waits for the graphics card
    for (int p = 0; p < 512;) {
        if (!need.Has(p)) { ++p; continue; }
        int e = p;
        while (e < 512 && need.Has(e)) ++e;
        gl.ReadPixels(0, p * 2, 1024, (e - p) * 2, GL_RED_INTEGER, GL_UNSIGNED_INT, gVram + (size_t)p * 2048);
        ++sDownloads;
        p = e;
    }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gGpuNewer.Remove(need);
}
// pages of the CPU's data written into the GPU copy (words: 2048 a page, in page order)
void Upload(const PageSet& pages, const uint32_t* words) {
    gl.BindTexture(GL_TEXTURE_2D, gMem);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    size_t at = 0;
    for (int p = 0; p < 512;) {
        if (!pages.Has(p)) { ++p; continue; }
        int e = p;
        while (e < 512 && pages.Has(e)) ++e;
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, p * 2, 1024, (e - p) * 2, GL_RED_INTEGER, GL_UNSIGNED_INT, words + at);
        at += (size_t)(e - p) * 2048;
        ++sUploads;
        p = e;
    }
    gGpuNewer.Remove(pages);
}

GLuint NewTexture(bool depth, int w, int h) {
    GLuint t = 0; gl.GenTextures(1, &t);
    gl.BindTexture(GL_TEXTURE_2D, t);
    if (depth) gl.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    else gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
// sx, sy: the renderer's scale for the pictures the game shows and its 3D, 1 for its own effects buffers
Buf* MakeBuf(uint32_t bp, uint32_t bw, uint32_t psm, bool depth, int h, int sx, int sy) {
    auto b = std::make_unique<Buf>();
    b->id = gNextId++; b->bp = bp; b->bw = bw ? bw : 1; b->psm = psm; b->depth = depth; b->sx = sx; b->sy = sy;
    b->w = (int)b->bw * 64; b->h = std::min(2048, (h + 31) & ~31);
    b->tex = NewTexture(depth, b->w * sx, b->h * sy);
    PagesOf(b->pages, b->bp, b->bw, b->psm, 0, 0, b->w - 1, b->h - 1);
    Buf* r = b.get();
    gBufs.push_back(std::move(b)); ++gBufsGen;
    sBufs = (int)gBufs.size();
    if (depth) {                                       // the depth buffer starts at 0 (the game clears it anyway)
        gl.BindFramebuffer(GL_FRAMEBUFFER, Fbo(0, r->tex));
        gl.Disable(GL_SCISSOR_TEST);
        gl.DepthMask(GL_TRUE); gl.ClearDepth(0.0); gl.Clear(GL_DEPTH_BUFFER_BIT);
    } else {
        StoreOwnersOf(r->pages, r);
        Load(*r, 0, 0, r->w, r->h);
    }
    return r;
}
// a buffer with h lines at this scale (its pixels kept, each one as sx x sy; new lines from the memory)
Buf* Resize(Buf* b, int h, int sx, int sy) {
    const int nh = std::max(b->h, std::min(2048, (h + 63) & ~63));
    if (nh == b->h && sx == b->sx && sy == b->sy) return b;
    const GLuint t = NewTexture(b->depth, b->w * sx, nh * sy);
    const GLuint src = Fbo(b->depth ? 0 : b->tex, b->depth ? b->tex : 0), dst = Fbo(b->depth ? 0 : t, b->depth ? t : 0);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, src); gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
    gl.Disable(GL_SCISSOR_TEST);
    if (b->depth) { gl.DepthMask(GL_TRUE); gl.ClearDepth(0.0); gl.Clear(GL_DEPTH_BUFFER_BIT); }
    gl.BlitFramebuffer(0, 0, b->w * b->sx, b->h * b->sy, 0, 0, b->w * sx, b->h * sy,
                       b->depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    DropFbos(b->tex);
    gl.DeleteTextures(1, &b->tex);
    const int oh = b->h;
    b->tex = t; b->h = nh; b->sx = sx; b->sy = sy; ++gBufsGen;
    b->shadowPages = PageSet{};                         // made again at the next copy
    PagesOf(b->pages, b->bp, b->bw, b->psm, 0, 0, b->w - 1, b->h - 1);
    if (!b->depth && nh > oh) {
        PageSet more; PagesOf(more, b->bp, b->bw, b->psm, 0, oh, b->w - 1, nh - 1);
        StoreOwnersOf(more, b);
        Load(*b, 0, oh, b->w, nh - oh);
    }
    return b;
}
Buf* Tall(Buf* b, int h) { return h <= b->h ? b : Resize(b, h, b->sx, b->sy); }
void FlushBatch();
// SetScale, at a picture: the new scale for what is drawn next; the buffers at the old one (the
// pictures, the 3D, their depth) resized now when it is lower (a higher one grows them as drawn)
void Rescale() {
    const int want = gWantScale.load(), wantX = gWantScaleX.load();
    if (!want || (want == gScale && wantX == gScaleX)) return;
    FlushBatch();
    const int oldS = gScale, oldX = gScaleX;
    gScale = want; gScaleX = wantX;
    int n = 0;
    if (oldS > 1 || oldX > 1)
        for (auto& b : gBufs)
            if (b->sx == oldX && b->sy == oldS && (wantX < oldX || want < oldS)) { Resize(b.get(), b->h, std::min(b->sx, wantX), std::min(b->sy, want)); ++n; }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    Say("scale: x%d across, x%d down (was x%d, x%d), %d buffers resized", wantX, want, oldX, oldS, n);
}

// the GS blend (A - B) * C + D on RGB as fixed-function blending: Cs and Cd coefficients a + b * F; srcF (the
// shader's uSrcF) for two GL has no factor of: Cd (1 + F) (A Cd, B 0, D Cd: a brightening, the sniper bullets'
// trails, black when drawn plain) as DST_COLOR x F + ONE, and Cs (1 + F)
bool BlendOf(uint32_t A, uint32_t B, uint32_t C, uint32_t D, GLenum& eq, GLenum& sf, GLenum& df, int& srcF) {
    srcF = 0;
    int as = 0, bs = 0, ad = 0, bd = 0;                   // Cs = as + bs*F, Cd = ad + bd*F
    if (A == 0) bs += 1; else if (A == 1) bd += 1;
    if (B == 0) bs -= 1; else if (B == 1) bd -= 1;
    if (D == 0) as += 1; else if (D == 1) ad += 1;
    const GLenum F = C == 0 ? GL_SRC_ALPHA : C == 1 ? GL_DST_ALPHA : GL_CONSTANT_ALPHA;
    const GLenum NF = C == 0 ? GL_ONE_MINUS_SRC_ALPHA : C == 1 ? GL_ONE_MINUS_DST_ALPHA : GL_ONE_MINUS_CONSTANT_ALPHA;
    auto factor = [&](int a, int b, GLenum& out) -> bool {  // a + b*F as a factor
        if (a == 0 && b == 0) { out = GL_ZERO; return true; }
        if (a == 1 && b == 0) { out = GL_ONE; return true; }
        if (a == 0 && b == 1) { out = F; return true; }
        if (a == 1 && b == -1) { out = NF; return true; }
        return false;
    };
    if (factor(as, bs, sf) && factor(ad, bd, df)) { eq = GL_FUNC_ADD; return true; }
    if (factor(as, bs, sf) && factor(-ad, -bd, df)) { eq = GL_FUNC_SUBTRACT; return true; }
    if (factor(-as, -bs, sf) && factor(ad, bd, df)) { eq = GL_FUNC_REVERSE_SUBTRACT; return true; }
    if (C != 1 && as == 0 && bs == 0 && ad == 1 && bd == 1) { eq = GL_FUNC_ADD; sf = GL_DST_COLOR; df = GL_ONE; srcF = 1; return true; }
    if (C != 1 && as == 1 && bs == 1 && ad == 0 && bd == 0) { eq = GL_FUNC_ADD; sf = GL_ONE; df = GL_ZERO; srcF = 2; return true; }
    return false;
}
// a texture that cannot reach the pixels: TCC 0 (the vertex's alpha in every function) and a blend weighing the
// source colour by 0 (TEAM SNEAKING's camouflage: its alpha pass reads the frame buffer it draws into)
bool TexUnused(const Regs& r) {
    if (Bits(r.tex0, 34, 1)) return false;
    const uint32_t A = Bits(r.alpha, 0, 2), B = Bits(r.alpha, 2, 2), C = Bits(r.alpha, 4, 2), D = Bits(r.alpha, 6, 2);
    GLenum eq, sf, df; int srcF;
    return BlendOf(A, B, C & 3, D, eq, sf, df, srcF) && sf == GL_ZERO && srcF == 0;
}
std::atomic<uint64_t> sCopies{0}, sCopyBytes{0};   // a batch reading its own target: the target copied first
// the draw program's uniforms as last set (the program keeps them; Setup makes a new one: valid false)
struct UniformShadow {
    bool valid = false;                                // every uniform set once (the first batch of a program)
    float posScale[2], zScale[1], srcF[2], rtSample[4]; uint32_t zMax[1];
    int tex0[4], texSize[4], wrap[4], region[4], texFx[4], texA[4], fog[4], hd[2], atest[4];
    template <class T, size_t N> bool Changed(T (&have)[N], const T (&want)[N]) {
        if (valid && !memcmp(have, want, sizeof have)) return false;
        memcpy(have, want, sizeof have);
        return true;
    }
} gUs;

void FlushBatch() {
    if (gVerts.empty() || !gHaveState) { gVerts.clear(); return; }
    const State& s = gState;
    Buf* rt = s.rt;
    const int Sx = rt->sx, Sy = rt->sy;
    // the target's pages from other buffers current in it first
    PageSet drawn;
    if (gBx1 >= gBx0 && gBy1 >= gBy0) {
        PagesOf(drawn, rt->bp, rt->bw, rt->psm, gBx0, gBy0, gBx1, gBy1);
        Freshen(*rt, drawn);
        TakePages(*rt, drawn);
    }
    GLuint texRtCopy = 0;
    if (s.texRt) {                                       // a frame buffer as texture: a copy when it is the target
        if (s.texRt == rt) {
            if (gScratchW < rt->w * Sx || gScratchH < rt->h * Sy) {
                if (gScratch) { DropFbos(gScratch); gl.DeleteTextures(1, &gScratch); }
                gScratchW = std::max(gScratchW, rt->w * Sx); gScratchH = std::max(gScratchH, rt->h * Sy);
                gl.GenTextures(1, &gScratch); gl.BindTexture(GL_TEXTURE_2D, gScratch);
                gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gScratchW, gScratchH, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            const GLuint from = Fbo(rt->tex, 0), to = Fbo(gScratch, 0);        // made before binding (Fbo binds a new one)
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, from); gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
            gl.Disable(GL_SCISSOR_TEST);
            gl.BlitFramebuffer(0, 0, rt->w * Sx, rt->h * Sy, 0, 0, rt->w * Sx, rt->h * Sy, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            texRtCopy = gScratch;
            ++sCopies; sCopyBytes += (uint64_t)rt->w * Sx * rt->h * Sy * 8;
        } else texRtCopy = s.texShadow && s.texRt->shadow ? s.texRt->shadow : s.texRt->tex;
    }
    static const bool debug = getenv("MGO1_GPU_DEBUG") != nullptr;
    if (debug) fprintf(stderr, "batch %llu: rt %x/%u psm %x %dx%d, %zu verts, tex %x/%d psm %x rt %d (%s), scissor %d,%d-%d,%d, blend %d, box %d,%d-%d,%d, hd %u, rt#%d x%d tex#%d x%d%s%s\n",
                       (unsigned long long)sBatches.load(), rt->bp, rt->bw, rt->psm, rt->w, rt->h, gVerts.size(), s.tex0[0], s.tex0[1], s.tex0[2], s.tex0[3],
                       s.texRt ? (s.texRt == rt ? "self" : "other") : "-", s.scx0, s.scy0, s.scx1, s.scy1, (int)s.blend, gBx0, gBy0, gBx1, gBy1, s.hd,
                       rt->id, rt->sy, s.texRt ? s.texRt->id : 0, s.texRt ? s.texRt->sy : 0, s.texShadow ? ", its shadow" : "", gBatchCopy ? ", a 1:1 copy" : "");
    gl.BindFramebuffer(GL_FRAMEBUFFER, Fbo(rt->tex, s.zb ? s.zb->tex : 0));
    gl.Viewport(0, 0, rt->w * Sx, rt->h * Sy);
    gl.Enable(GL_SCISSOR_TEST);
    gl.Scissor(s.scx0 * Sx, s.scy0 * Sy, (s.scx1 - s.scx0 + 1) * Sx, (s.scy1 - s.scy0 + 1) * Sy);
    if (s.blend) {
        gl.Enable(GL_BLEND);
        gl.BlendEquationSeparate(s.eq, GL_FUNC_ADD);
        gl.BlendFuncSeparate(s.sf, s.df, GL_ONE, GL_ZERO);
        gl.BlendColor(0, 0, 0, s.fix);
    } else gl.Disable(GL_BLEND);
    if (s.zte && s.zb) {
        gl.Enable(GL_DEPTH_TEST);
        static const GLenum f[4] = {GL_NEVER, GL_ALWAYS, GL_GEQUAL, GL_GREATER};
        gl.DepthFunc(f[s.ztst & 3]);
    } else gl.Disable(GL_DEPTH_TEST);
    gl.UseProgram(gProg);
    // the texture's scale in the uniforms: a frame buffer read as texture samples at its own pixels (a uniform set
    // only when its value changed: the program keeps the last ones)
    { const float v[2] = {2.0f / (float)rt->w, 2.0f / (float)rt->h}; if (gUs.Changed(gUs.posScale, v)) gl.Uniform2f(L.posScale, v[0], v[1]); }
    { const float v[1] = {s.zscale}; if (gUs.Changed(gUs.zScale, v)) gl.Uniform1f(L.zScale, v[0]); }
    { const uint32_t v[1] = {s.zmax}; if (gUs.Changed(gUs.zMax, v)) gUniform1ui(L.zMax, v[0]); }
    if (gUs.Changed(gUs.tex0, s.tex0)) gl.Uniform4i(L.tex0, s.tex0[0], s.tex0[1], s.tex0[2], s.tex0[3]);
    if (gUs.Changed(gUs.texSize, s.texSize)) gl.Uniform4i(L.texSize, s.texSize[0], s.texSize[1], s.texSize[2], s.texSize[3]);
    if (gUs.Changed(gUs.wrap, s.wrap)) gl.Uniform4i(L.wrap, s.wrap[0], s.wrap[1], s.wrap[2], s.wrap[3]);
    if (gUs.Changed(gUs.region, s.region)) gl.Uniform4i(L.region, s.region[0], s.region[1], s.region[2], s.region[3]);
    if (gUs.Changed(gUs.texFx, s.texFx)) gl.Uniform4i(L.texFx, s.texFx[0], s.texFx[1], s.texFx[2], s.texFx[3]);
    if (gUs.Changed(gUs.texA, s.texA)) gl.Uniform4i(L.texA, s.texA[0], s.texA[1], s.texA[2], s.texA[3]);
    if (gUs.Changed(gUs.fog, s.fog)) gl.Uniform4i(L.fog, s.fog[0], s.fog[1], s.fog[2], s.fog[3]);
    { const float v[2] = {s.blend ? (float)s.srcF : 0.0f, s.srcFix}; if (gUs.Changed(gUs.srcF, v)) gl.Uniform2f(L.srcF, v[0], v[1]); }
    {
        float v[4] = {s.rtSample[0], s.rtSample[1], s.rtSample[2], s.rtSample[3]};
        if (texRtCopy && texRtCopy == gScratch) { v[0] = (float)Sx / gScratchW; v[1] = (float)Sy / gScratchH; }   // the copy is as large as the largest target yet
        if (gUs.Changed(gUs.rtSample, v)) gl.Uniform4f(L.rtSample, v[0], v[1], v[2], v[3]);
    }
    gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, gMem);
    gl.ActiveTexture(GL_TEXTURE1); gl.BindTexture(GL_TEXTURE_2D, gClut);
    gl.ActiveTexture(GL_TEXTURE2); gl.BindTexture(GL_TEXTURE_2D, gLut);
    gl.ActiveTexture(GL_TEXTURE3); gl.BindTexture(GL_TEXTURE_2D, texRtCopy);
    gl.ActiveTexture(GL_TEXTURE5); gl.BindTexture(GL_TEXTURE_2D, s.hd);
    if (s.hd) gBindSampler(5, gHdSamplers[s.hdSampler & 3]);
    { const int v[2] = {s.hd ? 1 : 0, s.hdAlpha}; if (gUs.Changed(gUs.hd, v)) gl.Uniform2i(L.hd, v[0], v[1]); }
    gl.BindVertexArray(gVao);
    gl.BindBuffer(GL_ARRAY_BUFFER, gVbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(gVerts.size() * sizeof(GV)), gVerts.data(), GL_STREAM_DRAW);
    for (int pass = 0; pass < s.passes; ++pass) {
        int at[4] = {s.atest[0], s.atest[1], s.atest[2], s.atest[3]};
        bool r = !s.maskR, g = !s.maskG, b = !s.maskB, a = !s.maskA, z = s.zte && !s.zmsk;
        if (s.passes == 2) {
            at[3] = pass == 0 ? 1 : 2;
            if (pass == 1) switch (s.failPass) {
                case 1: z = false; break;                                   // FB_ONLY
                case 2: r = g = b = a = false; break;                       // ZB_ONLY
                default: a = false; z = false; break;                       // RGB_ONLY
            }
        }
        if (gUs.Changed(gUs.atest, at)) gl.Uniform4i(L.atest, at[0], at[1], at[2], at[3]);
        gl.ColorMask(r, g, b, a);
        gl.DepthMask(z ? GL_TRUE : GL_FALSE);
        gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)gVerts.size());
    }
    gUs.valid = true;
    // a 1:1 copy of a picture at a higher scale into this buffer (the scopes' lens source): the same draw into its
    // shadow at the picture's scale (the buffer's pixels under it first, when the shadow holds none yet)
    if (gBatchCopy && s.texRt && s.texRt != rt && !s.texShadow && (s.texRt->sx > Sx || s.texRt->sy > Sy) && !s.zte &&
        s.passes == 1 && drawn.Any()) {
        const int hx = s.texRt->sx, hy = s.texRt->sy;
        if (!rt->shadow || rt->shx != hx || rt->shy != hy) {
            if (rt->shadow) { DropFbos(rt->shadow); gl.DeleteTextures(1, &rt->shadow); }
            rt->shadow = NewTexture(false, rt->w * hx, rt->h * hy); rt->shx = hx; rt->shy = hy; rt->shadowPages = PageSet{};
        }
        if (!rt->shadowPages.Any()) {
            const GLuint from = Fbo(rt->tex, 0), to = Fbo(rt->shadow, 0);
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, from); gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
            gl.Disable(GL_SCISSOR_TEST);
            gl.BlitFramebuffer(0, 0, rt->w * Sx, rt->h * Sy, 0, 0, rt->w * hx, rt->h * hy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        gl.BindFramebuffer(GL_FRAMEBUFFER, Fbo(rt->shadow, 0));
        gl.Viewport(0, 0, rt->w * hx, rt->h * hy);
        gl.Enable(GL_SCISSOR_TEST);
        gl.Scissor(s.scx0 * hx, s.scy0 * hy, (s.scx1 - s.scx0 + 1) * hx, (s.scy1 - s.scy0 + 1) * hy);
        gl.Disable(GL_DEPTH_TEST);
        gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)gVerts.size());
        rt->shadowPages.Add(drawn);
        static bool told = false;
        if (!told) { told = true; Say("shadow: a 1:1 copy of a picture at x%d into %x/%u (x%d) kept at x%d (the scopes' lens)", hx, rt->bp, rt->bw, Sx, hx); }
    }
    ++sBatches;
    gVerts.clear();
    gBx0 = gBy0 = 1 << 30; gBx1 = gBy1 = -1;
}

// ---- the render thread's side of the commands
Regs gRegs{};                          // the registers the next draws use (kRegs)
std::set<uint32_t> gShown;             // bases of the buffers the display circuits read (drawn at the renderer's scale)

// A sprite that reads, through a byte view of the memory (T8, T4, T8H, T4HL, T4HH), only bytes of the pixels it
// draws: MGS3's colour filter turns each CT32 page of its downscaled picture through a CLUT with 128 sprites of
// 8x2 pixels, each reading its own pixels' blue (or red) bytes as T8. Drawn one by one, each needed the page
// stored into the memory copy first: 7000 stores (compute passes and barriers) a picture, the graphics card
// renderer at 15 FPS on REX HANGAR after a grenade (Snake, 30 September 20:53) and on the forest maps. None of
// them reads what another drew, so the run is one batch reading the memory as it was at the run's start.
struct OwnRun { const void* rt = nullptr; uint32_t tbp = 0, tbw = 0, tpsm = 0; std::vector<std::array<int, 4>> rects; };
OwnRun gOwn;
// whether each pixel of X0..X1 x Y0..Y1 reads (texels from tu0, tv0 on) only bytes of its own: kept for the
// sprites seen (a picture draws the same ones as the last; the result depends on these values alone)
bool OwnBytes(uint32_t tm, uint32_t fm, uint32_t tbp, uint32_t tbw, uint32_t fbp, uint32_t fbw, int X0, int Y0, int X1, int Y1,
              int tu0, int tv0, int tw, int th) {
    using Key = std::array<int32_t, 14>;
    struct Hash { size_t operator()(const Key& k) const { uint64_t h = 1469598103934665603ull; for (const int32_t v : k) h = (h ^ (uint32_t)v) * 1099511628211ull; return (size_t)(h ^ (h >> 32)); } };
    static std::unordered_map<Key, bool, Hash> seen;
    const Key k = {(int32_t)tm, (int32_t)fm, (int32_t)tbp, (int32_t)tbw, (int32_t)fbp, (int32_t)fbw, X0, Y0, X1, Y1, tu0, tv0, tw, th};
    if (const auto it = seen.find(k); it != seen.end()) return it->second;
    if (seen.size() > 200000) seen.clear();
    bool ok = true;
    for (int y = Y0; y < Y1 && ok; ++y)
        for (int x = X0; x < X1; ++x) {
            const int tu = tu0 + (x - X0), tv = tv0 + (y - Y0);
            if (tu < 0 || tv < 0 || tu >= tw || tv >= th) { ok = false; break; }
            uint32_t tb;                                                 // the texel's byte
            if (tm == 0x13) tb = GsPixelAddress(tm, tu, tv, tbp, tbw);
            else if (tm == 0x14) tb = GsPixelAddress(tm, tu, tv, tbp, tbw) >> 1;
            else tb = GsPixelAddress(tm, tu, tv, tbp, tbw) * 4 + 3;       // 8H, 4HL, 4HH: a CT32 word's top byte
            const bool wide = fm == 0x00 || fm == 0x01;
            const uint32_t pb = GsPixelAddress(fm, x, y, fbp, fbw) * (wide ? 4u : 2u);   // the pixel's first byte
            if (tb < pb || tb >= pb + (wide ? 4u : 2u)) { ok = false; break; }
        }
    seen.emplace(k, ok);
    return ok;
}
bool OwnRead(int kind, const Regs& r, const Vtx& a, const Vtx& b, uint32_t tbp, uint32_t tbw, uint32_t tpsm, int tw, int th,
             bool fst, uint32_t fbp, uint32_t fbw, uint32_t fpsm, std::array<int, 4>& rect) {
    static const bool off = getenv("MGO1_GPU_OWN") && !strcmp(getenv("MGO1_GPU_OWN"), "0");   // tests: A/B
    if (off || kind != 2) return false;                                  // sprites
    const uint32_t tm = tpsm & 0x3f, fm = fpsm & 0x3f;
    if (tm != 0x13 && tm != 0x14 && tm != 0x1b && tm != 0x24 && tm != 0x2c) return false;
    if (fm != 0x00 && fm != 0x01 && fm != 0x02 && fm != 0x0a) return false;
    const float ox = Bits(r.xyoffset, 0, 16) / 16.0f, oy = Bits(r.xyoffset, 32, 16) / 16.0f;
    auto U = [&](const Vtx& q) { return fst ? q.u / 16.0f : (q.q != 0.0f ? q.s / q.q * (float)tw : 0.0f); };
    auto V = [&](const Vtx& q) { return fst ? q.v / 16.0f : (q.q != 0.0f ? q.t / q.q * (float)th : 0.0f); };
    const Vtx& lo = a.x <= b.x ? a : b; const Vtx& hi = a.x <= b.x ? b : a;
    const float x0 = lo.x / 16.0f - ox, x1 = hi.x / 16.0f - ox;
    const float y0 = std::min(a.y, b.y) / 16.0f - oy, y1 = std::max(a.y, b.y) / 16.0f - oy;
    const Vtx& top = a.y <= b.y ? a : b; const Vtx& bot = a.y <= b.y ? b : a;
    const float u0 = U(lo), u1 = U(hi), v0 = V(top), v1 = V(bot);
    if (x0 != floorf(x0) || y0 != floorf(y0) || x1 - x0 < 1 || y1 - y0 < 1 || x1 - x0 > 16 || y1 - y0 > 16) return false;
    if (fabsf((u1 - u0) - (x1 - x0)) > 0.01f || fabsf((v1 - v0) - (y1 - y0)) > 0.01f) return false;   // 1:1
    const bool linear = Bits(r.tex1, 5, 1) != 0;                         // MMAG: the texel under a pixel's centre alone
    const float fu = u0 - (linear ? 0.5f : 0.0f), fv = v0 - (linear ? 0.5f : 0.0f);
    if (linear && (fu != floorf(fu) || fv != floorf(fv))) return false;
    const int X0 = (int)x0, Y0 = (int)y0, X1 = (int)ceilf(x1), Y1 = (int)ceilf(y1);
    const int tu0 = (int)floorf(fu), tv0 = (int)floorf(fv);
    if (!OwnBytes(tm, fm, tbp, tbw, fbp, fbw, X0, Y0, X1, Y1, tu0, tv0, tw, th)) return false;
    rect = {X0, Y0, X1, Y1};
    return true;
}

void PullHd();
void DrawNow(int kind, const Regs& r, const Vtx& a, const Vtx& b, const Vtx& c, int hd, const int16_t* box) {
    ++sDraws;
    const uint64_t p = r.prim;
    const bool iip = Bits(p, 3, 1), abe = Bits(p, 6, 1), fge = Bits(p, 5, 1), fst = Bits(p, 8, 1);
    const bool tme = Bits(p, 4, 1) && !(abe && TexUnused(r));            // the same pixels untextured: no copy, one batch
    if (!tme) gOwn.rt = nullptr;                                         // a run of OwnRead sprites ends
    State s;
    // the target and the depth buffer
    const uint32_t fbp = Bits(r.frame, 0, 9) * 32, fbw = Bits(r.frame, 16, 6), fpsm = Bits(r.frame, 24, 6);
    const uint32_t fbmsk = (uint32_t)(r.frame >> 32);
    s.scx0 = Bits(r.scissor, 0, 11); s.scx1 = Bits(r.scissor, 16, 11); s.scy0 = Bits(r.scissor, 32, 11); s.scy1 = Bits(r.scissor, 48, 11);
    if (s.scx1 < s.scx0 || s.scy1 < s.scy0 || !fbw) return;
    s.scx1 = std::min(s.scx1, (int)fbw * 64 - 1);
    const uint64_t test = r.test;
    const bool ate = Bits(test, 0, 1); const int atst = Bits(test, 1, 3), aref = Bits(test, 4, 8), afail = Bits(test, 12, 2);
    s.zte = Bits(test, 16, 1); s.ztst = Bits(test, 17, 2);
    if (Bits(test, 14, 1)) {                                             // the destination alpha test: not drawn yet
        static bool once = false;
        if (!once) { once = true; Say("a draw with the destination alpha test (DATM %u, frame %05x) drawn without it", Bits(test, 15, 1), fbp); }
    }
    if (s.zte && s.ztst == 0) return;                                    // depth test NEVER
    const uint32_t zbp = Bits(r.zbuf, 0, 9) * 32, zpsm = Bits(r.zbuf, 24, 4) | 0x30;
    s.zmsk = Bits(r.zbuf, 32, 1);
    const int zbits = (zpsm & 0xf) == 1 ? 24 : (zpsm & 0xf) >= 2 ? 16 : 32;
    s.zmax = zbits == 32 ? 0xffffffffu : (1u << zbits) - 1; s.zscale = 1.0f / (float)(zbits == 32 ? 4294967296.0 : (double)(1u << zbits));
    int bx0 = box[0], by0 = box[1], bx1 = box[2], by1 = box[3];       // PrimBox's (the queue side's, from the same registers)
    const float ox = Bits(r.xyoffset, 0, 16) / 16.0f, oy = Bits(r.xyoffset, 32, 16) / 16.0f;
    // blending
    if (abe) {
        const uint32_t A = Bits(r.alpha, 0, 2), B = Bits(r.alpha, 2, 2), C = Bits(r.alpha, 4, 2), D = Bits(r.alpha, 6, 2);
        s.blend = BlendOf(A, B, C & 3, D, s.eq, s.sf, s.df, s.srcF);
        s.srcFix = s.srcF && (C & 3) == 2 ? Bits(r.alpha, 32, 8) / 128.0f : -1.0f;
        if (!s.blend) { static bool once = false; if (!once) { once = true; Say("blend %u %u %u %u drawn as plain", A, B, C, D); } }
        s.fix = Bits(r.alpha, 32, 8) / 128.0f;
    }
    s.maskR = (fbmsk & 0xff) == 0xff; s.maskG = ((fbmsk >> 8) & 0xff) == 0xff; s.maskB = ((fbmsk >> 16) & 0xff) == 0xff;
    s.maskA = ((fbmsk >> 24) & 0xff) == 0xff || (fpsm & 0x3f) == 0x01;
    // the alpha test and what failing fragments still write
    bool zwritesOff = false, colorAlphaOff = false, colorOff = false;
    if (ate && atst != 1) {
        s.atest[0] = 1; s.atest[1] = atst; s.atest[2] = aref;
        const bool zwrites = s.zte && !s.zmsk;
        const bool never = atst == 0 || (atst == 2 && aref == 0);          // NEVER, or LESS than 0
        if (afail == 0) { if (never) return; s.passes = 1; s.atest[3] = 1; }
        else if (never) {                                                  // every fragment fails: what AFAIL writes, in one pass
            s.atest[0] = 0;
            if (afail == 1) zwritesOff = true;
            else if (afail == 2) colorOff = true;
            else { colorAlphaOff = true; zwritesOff = true; }
        }
        else if (afail == 1 && !zwrites) { s.passes = 1; s.atest[3] = 0; } // FB_ONLY without depth writes: every fragment
        else { s.passes = 2; s.failPass = afail; }
    }
    if (zwritesOff) s.zmsk = true;
    if (colorOff) s.maskR = s.maskG = s.maskB = s.maskA = true;
    if (colorAlphaOff) s.maskA = true;
    if (!(s.zte && (s.ztst >= 2 || !s.zmsk))) s.zte = false;           // depth only when the test reads it or the draw writes it
    // the target: at the renderer's scale when the game draws 3D there or shows it, else at the
    // PS2's (the game's own effects: downscaled copies, byte shuffles, blurs)
    const bool full = s.zte || gShown.count(fbp);
    const int wantX = full ? gScaleX : 1, wantY = full ? gScale : 1;
    Buf* rt = FindBuf(fbp, fbw, fpsm, false);
    const int needH = by1 + 1;                                           // the drawn rows (a buffer grows with them)
    if (!rt) { FlushBatch(); rt = MakeBuf(fbp, fbw, fpsm, false, needH, wantX, wantY); }
    else if (needH > rt->h || rt->sx < wantX || rt->sy < wantY) { FlushBatch(); rt = Resize(rt, needH, std::max(rt->sx, wantX), std::max(rt->sy, wantY)); }
    s.rt = rt;
    if (s.zte) {                                                         // its depth buffer (GL takes attachments of other heights)
        Buf* zb = FindBuf(zbp, fbw, zpsm, true, rt->sx, rt->sy);
        if (!zb) { FlushBatch(); zb = MakeBuf(zbp, fbw, zpsm, true, needH, rt->sx, rt->sy); }
        else if (zb->h < needH) { FlushBatch(); zb = Tall(zb, needH); }
        s.zb = zb;
    }
    // texture
    bool primCopy = false;                                               // a sprite copying its frame buffer texture 1:1
    if (tme) {
        const uint64_t t0 = r.tex0;
        const uint32_t tbp = Bits(t0, 0, 14), tbw = Bits(t0, 14, 6), tpsm = Bits(t0, 20, 6);
        const int tw = 1 << std::min(Bits(t0, 26, 4), 10u), th = 1 << std::min(Bits(t0, 30, 4), 10u);
        s.tex0[0] = (int)tbp; s.tex0[1] = (int)tbw; s.tex0[2] = (int)tpsm;
        s.texSize[0] = tw; s.texSize[1] = th; s.texSize[2] = Bits(r.tex1, 5, 1); s.texSize[3] = fst;
        s.wrap[0] = Bits(r.clamp, 0, 2); s.wrap[1] = Bits(r.clamp, 2, 2);
        s.region[0] = Bits(r.clamp, 4, 10); s.region[1] = Bits(r.clamp, 14, 10); s.region[2] = Bits(r.clamp, 24, 10); s.region[3] = Bits(r.clamp, 34, 10);
        const uint32_t cpsm = Bits(t0, 51, 4);
        s.texFx[0] = 1; s.texFx[1] = Bits(t0, 35, 2); s.texFx[2] = Bits(t0, 34, 1); s.texFx[3] = (cpsm == 2 || cpsm == 0xa) ? 1 : 0;
        s.texA[0] = Bits(r.texa, 0, 8); s.texA[1] = Bits(r.texa, 32, 8); s.texA[2] = Bits(r.texa, 15, 1); s.texA[3] = Bits(t0, 56, 5);
        // a frame buffer read as a texture in its own storage mode (CT24 from a 32-bit one too): its
        // own upscaled pixels, current first; the other modes and region repeat read the memory
        // the batch not drawn yet writes pages this texture reads: drawn first, its pages taken, so that the reads
        // below (the memory's copy, a frame buffer's) see it (MGS3's colour filter: its first CT16 sprite read the
        // downscaled picture's pages before the batch that drew them was stored: yellow bars at the left in first
        // person, from the second picture on)
        // a sprite of a run reading its own pixels (OwnRead): in the run's batch, no store before it
        std::array<int, 4> ownRect{};
        const bool own = OwnRead(kind, r, a, b, tbp, tbw, tpsm, tw, th, fst, fbp, fbw, fpsm, ownRect);
        bool ownSkip = own && gOwn.rt == rt && gOwn.tbp == tbp && gOwn.tbw == tbw && gOwn.tpsm == tpsm;
        if (ownSkip)
            for (const auto& q : gOwn.rects)
                if (q[0] < ownRect[2] && ownRect[0] < q[2] && q[1] < ownRect[3] && ownRect[1] < q[3]) { ownSkip = false; break; }
        if (own) {
            if (!ownSkip) { gOwn.rt = rt; gOwn.tbp = tbp; gOwn.tbw = tbw; gOwn.tpsm = tpsm; gOwn.rects.clear(); }
            gOwn.rects.push_back(ownRect);
        } else gOwn.rt = nullptr;
        if (!ownSkip && gHaveState && !gVerts.empty() && gState.rt && gBx1 >= gBx0 && gBy1 >= gBy0) {
            if (BoxPages(gState.rt->bp, gState.rt->bw, gState.rt->psm, gBx0, gBy0, gBx1, gBy1).Meets(TexPages(tbp, tbw, tpsm, tw, th)))
                FlushBatch();
        }
        const uint32_t tm = tpsm & 0x3f;
        Buf* src = s.wrap[0] != 3 && s.wrap[1] != 3 ? TexSource(tbp, tbw, tm) : nullptr;
        if (src) {
            const PageSet& tp = TexPages(tbp, tbw, tpsm, tw, th);
            if (src->stale.Meets(tp)) { FlushBatch(); Freshen(*src, tp); }
            s.texRt = src; s.tex0[3] = 1;
            s.rtSample[0] = 1.0f / (float)src->w; s.rtSample[1] = 1.0f / (float)src->h; s.rtSample[2] = (float)src->sx; s.rtSample[3] = (float)src->sy;
            // what this primitive reads of it (texels) and draws (pixels): a 1:1 copy, or a read its shadow holds
            const Vtx* pv[3] = {&a, &b, &c};
            const int nv = kind == 1 ? 3 : 2;
            float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (int k = 0; k < nv; ++k) {
                const Vtx& q = *pv[k];
                const float U = fst ? q.u / 16.0f : (q.q != 0.0f ? q.s / q.q * (float)tw : 0.0f);
                const float V = fst ? q.v / 16.0f : (q.q != 0.0f ? q.t / q.q * (float)th : 0.0f);
                u0 = std::min(u0, U); u1 = std::max(u1, U); v0 = std::min(v0, V); v1 = std::max(v1, V);
                x0 = std::min(x0, (float)q.x); x1 = std::max(x1, (float)q.x); y0 = std::min(y0, (float)q.y); y1 = std::max(y1, (float)q.y);
            }
            x0 /= 16.0f; x1 /= 16.0f; y0 /= 16.0f; y1 /= 16.0f;
            primCopy = kind == 2 && fabsf((u1 - u0) - (x1 - x0)) <= 1.5f && fabsf((v1 - v0) - (y1 - y0)) <= 1.5f && x1 > x0 && y1 > y0;
            if (src != rt && (rt->sx > src->sx || rt->sy > src->sy) && gHaveState && gState.rt == src && !gVerts.empty())
                FlushBatch();                                            // the copy into it drawn first (its shadow with it)
            if (src->shadow && src->shadowPages.Any() && src != rt && (rt->sx > src->sx || rt->sy > src->sy)) {
                PageSet sp;
                PagesOf(sp, tbp, tbw, tpsm, std::clamp((int)floorf(u0), 0, tw - 1), std::clamp((int)floorf(v0), 0, th - 1),
                        std::clamp((int)ceilf(u1), 0, tw - 1), std::clamp((int)ceilf(v1), 0, th - 1));
                bool held = true;
                for (int k = 0; k < 8; ++k) if (sp.w[k] & ~src->shadowPages.w[k]) held = false;
                if (held) { s.texShadow = true; s.rtSample[2] = (float)src->shx; s.rtSample[3] = (float)src->shy; }
            }
        } else {
            // the texels in the GPU copy of the memory: the buffers newer there stored
            const PageSet& tp = TexPages(tbp, tbw, tpsm, tw, th);
            const bool storeNeeded = tp.Meets(gOwned);
            if (storeNeeded && !ownSkip) { FlushBatch(); StoreOwnersOf(tp, nullptr); }
            // its HD image, when it came (region repeat keeps the texels)
            if (hd > 0 && (hd >= (int)gHd.size() || !gHd[hd].tex)) PullHd();
            if (hd > 0 && hd < (int)gHd.size() && gHd[hd].evicted && !gHd[hd].asked) { gHd[hd].asked = true; hdtex::Reload(hd); }
            if (hd > 0 && hd < (int)gHd.size() && gHd[hd].tex && s.wrap[0] != 3 && s.wrap[1] != 3 && gReplaceOn.load(std::memory_order_relaxed)) {
                s.hd = gHd[hd].tex; s.hdSampler = (s.wrap[0] != 0 ? 1 : 0) | (s.wrap[1] != 0 ? 2 : 0); s.hdAlpha = gHd[hd].constAlpha;
                gHd[hd].lastUse = gPictureNo;
                ++sHdDraws;
            }
        }
    }
    s.wrap[2] = iip; s.wrap[3] = Bits(r.fba, 0, 1);
    if (fge) { s.fog[0] = 1; s.fog[1] = Bits(r.fogcol, 0, 8); s.fog[2] = Bits(r.fogcol, 8, 8); s.fog[3] = Bits(r.fogcol, 16, 8); }
    if (!gHaveState || !(s == gState)) { FlushBatch(); gState = s; gHaveState = true; gBatchCopy = true; }
    gBatchCopy = gBatchCopy && primCopy;
    gBx0 = std::min(gBx0, bx0); gBy0 = std::min(gBy0, by0); gBx1 = std::max(gBx1, bx1); gBy1 = std::max(gBy1, by1);
    // the vertices: PS2 pixels + 0.5 (the GS samples at whole coordinates, GL at pixel centres)
    auto gv = [&](const Vtx& x, float X, float Y, const Vtx& col, float S_, float T_, float Q_) {
        GV o;
        o.x = X + 0.5f; o.y = Y + 0.5f; o.z = x.z;
        o.r = col.r; o.g = col.g; o.b = col.b; o.a = col.a;
        o.s = S_; o.t = T_; o.q = Q_; o.fog = col.f;
        return o;
    };
    auto X = [&](const Vtx& x) { return x.x / 16.0f - ox; };
    auto Y = [&](const Vtx& x) { return x.y / 16.0f - oy; };
    auto st = [&](const Vtx& x, float& S_, float& T_, float& Q_) {
        if (fst) { S_ = x.u / 16.0f; T_ = x.v / 16.0f; Q_ = 1.0f; } else { S_ = x.s; T_ = x.t; Q_ = x.q; }
    };
    const Vtx* v[3] = {&a, &b, &c};
    if (kind == 1) {
        for (const Vtx* x : v) { float S_, T_, Q_; st(*x, S_, T_, Q_); gVerts.push_back(gv(*x, X(*x), Y(*x), *x, S_, T_, Q_)); }
    } else if (kind == 2) {
        // a sprite: its box from the two corners, the second vertex's colour, depth and q
        float s0, t0, q0, s1, t1, q1; st(a, s0, t0, q0); st(b, s1, t1, q1);
        if (!fst) q0 = q1;
        const float x0 = X(a), y0 = Y(a), x1 = X(b), y1 = Y(b);
        Vtx zc = b;
        const GV p00 = gv(zc, x0, y0, b, s0, t0, q1), p10 = gv(zc, x1, y0, b, s1, t0, q1);
        const GV p01 = gv(zc, x0, y1, b, s0, t1, q1), p11 = gv(zc, x1, y1, b, s1, t1, q1);
        gVerts.push_back(p00); gVerts.push_back(p10); gVerts.push_back(p01);
        gVerts.push_back(p10); gVerts.push_back(p11); gVerts.push_back(p01);
    } else {
        // a line one PS2 pixel wide along its minor axis (a point: one pixel), on the pixels the
        // GS steps through (its positions truncated)
        float xa = floorf(X(a)), ya = floorf(Y(a)), xb = floorf(X(b)), yb = floorf(Y(b));
        float sa, ta, qa, sb, tb, qb; st(a, sa, ta, qa); st(b, sb, tb, qb);
        const bool xmajor = fabsf(xb - xa) >= fabsf(yb - ya);
        float dx = xmajor ? 0.0f : 0.5f, dy = xmajor ? 0.5f : 0.0f;
        float ex = xmajor ? (xb >= xa ? 0.5f : -0.5f) : 0.0f, ey = xmajor ? 0.0f : (yb >= ya ? 0.5f : -0.5f);
        if (xa == xb && ya == yb) { dx = dy = 0.5f; ex = 0.5f; ey = 0.0f; }
        const GV q0 = gv(a, xa - ex - dx, ya - ey - dy, a, sa, ta, qa), q1 = gv(b, xb + ex - dx, yb + ey - dy, b, sb, tb, qb);
        const GV q2 = gv(b, xb + ex + dx, yb + ey + dy, b, sb, tb, qb), q3 = gv(a, xa - ex + dx, ya - ey + dy, a, sa, ta, qa);
        gVerts.push_back(q0); gVerts.push_back(q1); gVerts.push_back(q2);
        gVerts.push_back(q0); gVerts.push_back(q2); gVerts.push_back(q3);
    }
    if (gVerts.size() >= 60000) FlushBatch();
}

// the storage layout of a mode (buffers of one layout see a transfer's pixels at the same places)
int LayoutKey(uint32_t psm) {
    switch (psm & 0x3f) {
    case 0x02: return 1; case 0x0a: return 2; case 0x13: return 3; case 0x14: return 4;
    case 0x30: case 0x31: return 5; case 0x32: return 6; case 0x3a: return 7;
    default: return 0;                                   // CT32, CT24 and the 8H / 4HL / 4HH views
    }
}
int BitsPerPixel(uint32_t psm) {
    switch (psm & 0x3f) {
    case 0x00: case 0x30: return 32;
    case 0x01: case 0x31: return 24;
    case 0x02: case 0x0a: case 0x32: case 0x3a: return 16;
    case 0x13: case 0x1b: return 8;
    case 0x14: case 0x24: case 0x2c: return 4;
    default: return 0;
    }
}
std::atomic<uint64_t> sXfers{0}, sXferBytes{0}, sClutLoads{0};

// a transfer's pixels into the GPU copy of the memory, then into the frame buffers over them
void XferNow(uint32_t bp, uint32_t bw, uint32_t psm, int x, int y, int w, int h, std::vector<uint8_t>& d) {
    FlushBatch();
    gOwn.rt = nullptr;                                                   // the memory changes: an OwnRead run ends
    const int bpp = BitsPerPixel(psm);
    if (!bpp || w <= 0 || h <= 0 || d.empty()) return;
    const int count = (int)std::min<uint64_t>((uint64_t)w * h, (uint64_t)d.size() * 8 / bpp);
    const size_t words = (d.size() + 3) / 4;
    const int rows = (int)((words + 1023) / 1024);
    if (count <= 0 || rows > 1024) return;
    // the pages it writes: what buffers drew there is in the memory first (the rest of them stays)
    const bool wraps = x + w > 2048 || y + h > 2048;
    PageSet p;
    if (wraps) for (auto& q : p.w) q = ~0ull;
    else PagesOf(p, bp, bw, psm, x, y, x + w - 1, y + h - 1);
    StoreOwnersOf(p, nullptr);
    d.resize((size_t)rows * 4096, 0);
    gl.BindTexture(GL_TEXTURE_2D, gXferData);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, rows, GL_RED_INTEGER, GL_UNSIGNED_INT, d.data());
    gl.UseProgram(gXferProg);
    gl.Uniform4i(XL.dst, (GLint)bp, (GLint)bw, (GLint)psm, bpp);
    gl.Uniform4i(XL.rect, x, y, w, h);
    gl.Uniform1i(XL.count, count);
    gl.ActiveTexture(GL_TEXTURE2); gl.BindTexture(GL_TEXTURE_2D, gLut);
    gl.ActiveTexture(GL_TEXTURE4); gl.BindTexture(GL_TEXTURE_2D, gXferData);
    gDispatch((GLuint)((w + 7) / 8), (GLuint)((h + 7) / 8), 1);
    gBarrier(kMemBarriers);
    ++sXfers; sXferBytes += (uint64_t)words * 4;
    // the frame buffers over those pages: the written rectangle again from the memory (the pages
    // again when the buffer's layout differs)
    const int key = LayoutKey(psm);
    for (auto& b : gBufs) {
        b->shadowPages.Remove(p);
        if (b->depth || !b->pages.Meets(p)) continue;
        if (!wraps && b->bp == bp && b->bw == bw && LayoutKey(b->psm) == key) Load(*b, x, y, w, h);
        else for (int k = 0; k < 8; ++k) b->stale.w[k] |= p.w[k] & b->pages.w[k];   // loaded when it is used
    }
}

// a CLUT load from the GPU copy of the memory (what a buffer drew there first)
void ClutLoadNow(const ClutLoad& c) {
    FlushBatch();
    const uint32_t cpsm = (c.cpsm == 0x02 || c.cpsm == 0x0a) ? c.cpsm : 0;
    const uint32_t bw = c.bw ? c.bw : 1;
    PageSet p;
    if (c.csm == 0) PagesOf(p, c.cbp, 1, cpsm, 0, 0, c.entries == 256 ? 15 : 7, c.entries == 256 ? 15 : 1);
    else PagesOf(p, c.cbp, bw, cpsm, c.x0, c.y0, std::min(2047, c.x0 + (int)c.entries - 1), std::min(2047, c.y0));
    StoreOwnersOf(p, nullptr);
    gl.UseProgram(gClutProg);
    gl.Uniform4i(CL.c0, (GLint)c.cbp, (GLint)cpsm, (GLint)c.csm, (GLint)c.entries);
    gl.Uniform4i(CL.c1, (GLint)bw, c.x0, c.y0, c.slot);
    gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, gMem);
    gl.ActiveTexture(GL_TEXTURE2); gl.BindTexture(GL_TEXTURE_2D, gLut);
    gDispatch((GLuint)((c.entries + 63) / 64), 1, 1);
    gBarrier(kMemBarriers);
    ++sClutLoads;
}

// the buffer the display circuits read (circuit 1 when it is on); false when nothing is shown
bool DisplayedBuf(uint64_t pmode, const uint64_t dispfb[2], const uint64_t display[2], Buf*& b, int& x, int& y, int& w, int& h) {
    const int k = (pmode & 1) ? 0 : (pmode & 2) ? 1 : 0;
    const uint64_t fb = dispfb[k], dp = display[k];
    const uint32_t fbp = Bits(fb, 0, 9) * 32, fbw = Bits(fb, 9, 6), psm = Bits(fb, 15, 5);
    x = Bits(fb, 32, 11); y = Bits(fb, 43, 11);
    const int magh = Bits(dp, 23, 4) + 1, magv = Bits(dp, 27, 2) + 1;
    w = (Bits(dp, 32, 12) + 1) / magh; h = (Bits(dp, 44, 11) + 1) / magv;
    if (!fbw || w <= 0 || h <= 0) return false;
    gShown.insert(fbp);
    b = FindBuf(fbp, fbw, psm, false);
    if (!b) b = MakeBuf(fbp, fbw, psm, false, std::max(h + y, 448), gScaleX, gScale);
    else if (b->h < h + y || b->sx < gScaleX || b->sy < gScale) b = Resize(b, h + y, std::max(b->sx, gScaleX), std::max(b->sy, gScale));
    // what other buffers drew over the shown pages (another base or storage mode on the same memory)
    PageSet shown; PagesOf(shown, b->bp, b->bw, b->psm, x, y, x + w - 1, y + h - 1);
    static const bool debug = getenv("MGO1_GPU_DEBUG") != nullptr;
    if (debug) fprintf(stderr, "shown: buffer #%d %x/%u psm %x x%d, %d,%d %dx%d, stale %d of its pages\n", b->id, b->bp, b->bw, b->psm, b->sy, x, y, w, h,
                       [&] { PageSet n; for (int k = 0; k < 8; ++k) n.w[k] = shown.w[k] & b->stale.w[k]; return n.Count(); }());
    Freshen(*b, shown);
    return true;
}

// the picture on the window, the overlay over it, then shown
void Rescale();
// the HD images finished since the last picture made textures (at most 8 M texels a picture)
void PullHd() {
    static uint64_t madeIn = ~0ull, made = 0;                            // bytes made textures in this picture (every caller's)
    if (madeIn != gPictureNo) { madeIn = gPictureNo; made = 0; }
    uint64_t budget = made < (8u << 20) ? (8u << 20) - made : 0;         // 8 MB a picture
    hdtex::Image im;
    while (budget && hdtex::TakeFinished(im)) {
        if (im.slot <= 0 || im.w <= 0 || im.h <= 0 || im.levels.empty()) continue;
        if ((int)gHd.size() <= im.slot) gHd.resize((size_t)im.slot + 64);
        HdImage& h = gHd[im.slot];
        if (!h.tex) gl.GenTextures(1, &h.tex);
        gl.BindTexture(GL_TEXTURE_2D, h.tex);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        static const bool mark = getenv("MGO1_HD_MARK") != nullptr;        // tests: every HD image magenta
        const bool bc = im.format != hdtex::kRgba8, bc3 = im.format == hdtex::kBc3;
        uint64_t bytes = 0;
        int lw = im.w, lh = im.h;
        for (size_t k = 0; k < im.levels.size(); ++k) {
            const std::vector<uint8_t>& l = im.levels[k];
            if (bc && gS3tc && !mark)
                gCompressedTexImage(GL_TEXTURE_2D, (GLint)k, bc3 ? 0x83F3 : 0x83F1, lw, lh, 0, (GLsizei)l.size(), l.data());   // DXT5 / DXT1
            else {                                                         // no S3TC: the blocks decoded here
                std::vector<uint32_t> px = bc ? bcn::Decode(l.data(), lw, lh, bc3) : std::vector<uint32_t>((const uint32_t*)l.data(), (const uint32_t*)l.data() + (size_t)lw * lh);
                if (mark) for (uint32_t& c : px) c = (c & 0xff000000u) | 0xff00ffu;
                gl.TexImage2D(GL_TEXTURE_2D, (GLint)k, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            }
            bytes += (bc && gS3tc && !mark) ? l.size() : (uint64_t)lw * lh * 4;
            lw = std::max(1, lw / 2); lh = std::max(1, lh / 2);
        }
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)im.levels.size() - 1);
        if (im.levels.size() == 1) gGenMipmap(GL_TEXTURE_2D);             // an RGBA image without its levels
        gHdBytes -= h.bytes;
        h.w = im.w; h.h = im.h; h.bytes = bytes; h.constAlpha = im.format == hdtex::kBc1 ? im.constAlpha : -1;
        h.lastUse = gPictureNo; h.evicted = h.asked = false;
        gHdBytes += bytes;
        budget = bytes >= budget ? 0 : budget - bytes;
        made += bytes;
        ++sHdImages; sHdBytes += bytes;
    }
    // past the budget: the images unused for two seconds go, the least recently used first
    static uint64_t checked = 0;
    if (gPictureNo == checked) return;
    checked = gPictureNo;
    const uint64_t limit = (uint64_t)std::max(64, hdtex::Current().vramMb) << 20;
    if (gHdBytes <= limit) return;
    std::vector<int> old;
    for (int k = 1; k < (int)gHd.size(); ++k)
        if (gHd[k].tex && gHd[k].lastUse + 120 < gPictureNo && hdtex::CanReload(k)) old.push_back(k);
    std::sort(old.begin(), old.end(), [](int a, int b) { return gHd[a].lastUse < gHd[b].lastUse; });
    int n = 0;
    for (int k : old) {
        if (gHdBytes <= limit * 4 / 5) break;
        HdImage& h = gHd[k];
        gl.DeleteTextures(1, &h.tex); h.tex = 0;
        gHdBytes -= h.bytes; h.bytes = 0; h.w = h.h = 0;
        h.evicted = true; h.asked = false; ++n;
    }
    if (n) Say("hd textures: %d images taken off the graphics card (%llu MB kept, the budget %d MB)", n,
               (unsigned long long)(gHdBytes >> 20), hdtex::Current().vramMb);
}
void PresentNow(uint64_t pmode, const uint64_t dispfb[2], const uint64_t display[2]) {
    FlushBatch();
    ++gPictureNo;
    PullHd();
    Rescale();
    if (!gHost.swap || !gHost.size) return;
    int ww = 0, wh = 0; gHost.size(gHost.user, ww, wh);
    if (ww <= 0 || wh <= 0) return;                                     // minimized
    Buf* b = nullptr; int px = 0, py = 0, pw = 0, ph = 0;
    const bool shown = DisplayedBuf(pmode, dispfb, display, b, px, py, pw, ph);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, ww, wh);
    gl.Disable(GL_SCISSOR_TEST); gl.Disable(GL_DEPTH_TEST); gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.ClearColor(0, 0, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.BindVertexArray(gEmptyVao);
    if (shown) {
        // the picture's rectangle in the window: its shape kept, bars around
        const float want = gAspect.load(std::memory_order_relaxed);
        const float aspect = want > 0 ? want : want < 0 ? (float)ww / wh : 4.0f / 3.0f;
        int dw = ww, dh = (int)lrintf(ww / aspect);
        if (dh > wh) { dh = wh; dw = (int)lrintf(wh * aspect); }
        const int dx = (ww - dw) / 2, dy = (wh - dh) / 2;
        const float Sx = (float)b->sx, Sy = (float)b->sy;
        gl.UseProgram(gPresentProg);
        gl.Uniform4f(PL.dst, 2.0f * dx / ww - 1.0f, 1.0f - 2.0f * dy / wh, 2.0f * (dx + dw) / ww - 1.0f, 1.0f - 2.0f * (dy + dh) / wh);
        gl.Uniform4f(PL.src, px * Sx, py * Sy, (px + pw) * Sx, (py + ph) * Sy);
        gl.Uniform2f(PL.step, pw * Sx / dw, ph * Sy / dh);
        const int smooth = gSmooth.load(std::memory_order_relaxed);
        gl.Uniform1i(PL.mode, (smooth < 0 ? gHost.smooth : smooth != 0) ? 1 : 0);
        gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, b->tex);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    // the overlay line (FPS, ping): its shadow, then the text
    if (gHost.overlay) {
        static std::vector<uint8_t> mask; static int mw = 0, mh = 0, corner = 0; static uint64_t ver = 0;
        if (gHost.overlay(gHost.user, mask, mw, mh, corner, ver) && mw > 0 && mh > 0 && (int)mask.size() >= mw * mh) {
            if (!gOverlayTex) {
                gl.GenTextures(1, &gOverlayTex); gl.BindTexture(GL_TEXTURE_2D, gOverlayTex);
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            gl.BindTexture(GL_TEXTURE_2D, gOverlayTex);
            gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, mw, mh, 0, GL_RED, GL_UNSIGNED_BYTE, mask.data());
            gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }
        if (gOverlayTex && mw > 0 && corner) {
            const int margin = std::max(6, mh / 2);
            const int ox = corner == 2 ? ww - margin - mw : margin, oy = margin / 2;
            gl.Enable(GL_BLEND);
            gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            gl.UseProgram(gOverlayProg);
            gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, gOverlayTex);
            gl.Uniform4f(OL.src, 0, 0, (float)mw, (float)mh);
            for (int pass = 0; pass < 2; ++pass) {
                const int x = ox + (pass == 0 ? 1 : 0), y = oy + (pass == 0 ? 1 : 0);
                gl.Uniform4f(OL.dst, 2.0f * x / ww - 1.0f, 1.0f - 2.0f * y / wh, 2.0f * (x + mw) / ww - 1.0f, 1.0f - 2.0f * (y + mh) / wh);
                if (pass == 0) gl.Uniform4f(OL.color, 0, 0, 0, 1); else gl.Uniform4f(OL.color, 210 / 255.0f, 240 / 255.0f, 190 / 255.0f, 1);
                gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
            gl.Disable(GL_BLEND);
        }
    }
    // the in-game menu over everything (premultiplied alpha)
    if (gHost.menu) {
        static std::vector<uint32_t> px; static int mw = 0, mh = 0; static uint64_t ver = 0;
        if (gHost.menu(gHost.user, px, mw, mh, ver) && mw > 0 && mh > 0 && (int)px.size() >= mw * mh) {
            if (!gMenuTex) {
                gl.GenTextures(1, &gMenuTex); gl.BindTexture(GL_TEXTURE_2D, gMenuTex);
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            gl.BindTexture(GL_TEXTURE_2D, gMenuTex);
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, mw, mh, 0, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
        }
        if (gMenuTex && mw > 0 && mh > 0) {
            gl.Enable(GL_BLEND);
            gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            gl.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            gl.UseProgram(gMenuProg);
            gl.ActiveTexture(GL_TEXTURE0); gl.BindTexture(GL_TEXTURE_2D, gMenuTex);
            gl.Uniform4f(ML.src, 0, 0, (float)mw, (float)mh);
            gl.Uniform4f(ML.dst, -1.0f, 1.0f, 2.0f * mw / ww - 1.0f, 1.0f - 2.0f * mh / wh);
            gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            gl.Disable(GL_BLEND);
        }
    }
    { BlockTimer bt; gHost.swap(gHost.user); }                     // vsync: waiting for the screen
    ++sPresents;
    static int errors = 0;
    if (errors < 8) { const GLenum e = gl.GetError(); if (e != GL_NO_ERROR) { ++errors; Say("GL error %#x after a picture", e); } }
}

bool PictureNow(uint64_t pmode, const uint64_t dispfb[2], const uint64_t display[2], std::vector<uint32_t>& rgba, int& w, int& h) {
    FlushBatch();
    PullHd();
    Buf* b = nullptr; int px = 0, py = 0, pw = 0, ph = 0;
    if (!DisplayedBuf(pmode, dispfb, display, b, px, py, pw, ph)) return false;
    const int Sx = b->sx, Sy = b->sy;
    w = pw * Sx; h = ph * Sy;
    std::vector<float> f((size_t)w * h * 4);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, Fbo(b->tex, 0));
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    { BlockTimer bt; gl.ReadPixels(px * Sx, py * Sy, w, h, GL_RGBA, GL_FLOAT, f.data()); }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    rgba.assign((size_t)w * h, 0);
    for (size_t i = 0; i < rgba.size(); ++i) {
        const float* c = &f[i * 4];
        auto u8 = [](float v) { int k = (int)lrintf(v * 255.0f); return (uint32_t)std::clamp(k, 0, 255); };
        rgba[i] = u8(c[0]) | u8(c[1]) << 8 | u8(c[2]) << 16 | 0xff000000u;
    }
    return true;
}

// ---- the commands (gs_gpu_cmd.h), and the ring that carries them from gs.cpp's thread to the render thread
constexpr size_t kRingBytes = 16u << 20;
constexpr uint64_t kPublishBytes = 16u << 10;                  // draws handed over in runs of 16 KB (the other commands at once)
struct Ring {                                                  // the two threads' fields on cache lines of their own
    uint8_t* buf = nullptr;
    alignas(64) uint64_t wr = 0;                               // bytes written (gs.cpp's thread)
    uint64_t pub = 0, tailSeen = 0;                            // that thread's last head stored, tail read
    alignas(64) std::atomic<uint64_t> head{0};                 // bytes published
    std::atomic<bool> sleeping{false};
    alignas(64) std::atomic<uint64_t> tail{0};                 // bytes done (render thread)
    std::atomic<bool> waiting{false};
    alignas(64) std::mutex m;
    std::condition_variable work, done;
} gRing;
std::thread gThread;
uint64_t gPresentsQueued = 0;
std::atomic<uint64_t> gPresentsDone{0};

bool Setup(std::string& error);
// Vulkan stopped on the way (a lost device, a fault it cannot go on after): the window to OpenGL when the host has it
// (Host::begin, getProc), else nothing more is drawn here and gs.cpp's processor draws (On() false)
void FromVulkan(const std::string why) {
    gVulkanBackend->release();
    gVk = false;
    std::string e = "no OpenGL context for the window";
    if (gHost.begin && gHost.getProc && gHost.begin(gHost.user, e) && Setup(e)) {
        Say("Vulkan stopped (%s); OpenGL from now on: %s", why.c_str(), gDescription.c_str());
        return;
    }
    Say("Vulkan stopped (%s) and OpenGL cannot start (%s): the processor draws the PS2 picture", why.c_str(), e.c_str());
    gDead = true;
    gOn = false;
}
void Execute(uint32_t type, void* p) {
    if (gVk) {                                                    // the Vulkan backend (gs_vk.cpp)
        if (type == kRegs) gRegs = *(const Regs*)p;               // kept for OpenGL (FromVulkan)
        gVulkanBackend->execute(type, p);
        if (type == kPresent) gPresentsDone.fetch_add(1);
        if (const std::string* why = gVulkanBackend->fault()) FromVulkan(*why);
        return;
    }
    if (gDead) {                                                  // nothing drawn: the commands' data let go
        if (type == kXfer) delete ((XferCmd*)p)->pixels;
        else if (type == kWroteAll) delete ((AllCmd*)p)->data;
        else if (type == kPicture) *((PictureCmd*)p)->ok = false;
        else if (type == kPresent) gPresentsDone.fetch_add(1);
        return;
    }
    switch (type) {
    case kRegs: gRegs = *(const Regs*)p; break;
    case kClutSet: {
        FlushBatch();
        gl.BindTexture(GL_TEXTURE_2D, gClut);
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, ((const ClutCmd*)p)->words);
        break; }
    case kClutLoad: ClutLoadNow(*(const ClutLoad*)p); break;
    case kClutData: {                                             // Vulkan's (queued before FromVulkan): the entries as they are
        const ClutDataCmd& c = *(const ClutDataCmd*)p;
        FlushBatch();
        gl.BindTexture(GL_TEXTURE_2D, gClut);
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, (GLint)c.slot, 0, (GLsizei)c.n, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &c + 1);
        break; }
    case kDraw: { const DrawCmd& d = *(const DrawCmd*)p; DrawNow(d.kind, gRegs, d.v[0], d.v[1], d.v[2], d.hd, d.box); break; }
    case kXfer: { XferCmd& x = *(XferCmd*)p; XferNow(x.bp, x.bw, x.psm, x.x, x.y, x.w, x.h, *x.pixels); delete x.pixels; break; }
    case kWroteAll: {
        AllCmd& a = *(AllCmd*)p;
        FlushBatch();
        gOwn.rt = nullptr;
        memset(gOwner, 0, sizeof gOwner); gOwned = PageSet{};
        for (auto& b : gBufs) b->owned = PageSet{};
        PageSet all; for (auto& q : all.w) q = ~0ull;
        Upload(all, a.data->data());
        delete a.data;
        for (auto& b : gBufs) if (!b->depth) { Load(*b, 0, 0, b->w, b->h); b->stale = PageSet{}; b->shadowPages = PageSet{}; }
        break; }
    case kTouch: { const TouchCmd& t = *(const TouchCmd*)p; FlushBatch(); StoreOwnersOf(t.pages, nullptr); Download(t.pages); break; }
    case kSync: FlushBatch(); gl.Flush(); break;
    case kPresent: { const ScreenCmd& s = *(const ScreenCmd*)p; PresentNow(s.pmode, s.dispfb, s.display); gPresentsDone.fetch_add(1); break; }
    case kPicture: { PictureCmd& c = *(PictureCmd*)p; *c.ok = PictureNow(c.screen.pmode, c.screen.dispfb, c.screen.display, *c.rgba, *c.w, *c.h); break; }
    default: break;
    }
}
// the commands published so far, run (render thread, or the caller's without one)
void RunTo(uint64_t head) {
    uint64_t t = gRing.tail.load(std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();              // the busy time (gs_gpu_cmd.h)
    for (int n = 1; t < head; ++n) {
        CmdHead* h = (CmdHead*)(gRing.buf + t % kRingBytes);
        Execute(h->type, h + 1);
        t += h->size;
        if (n % 32 && t < head) continue;                          // done told every 32 commands and at the end
        gRing.tail.store(t, std::memory_order_seq_cst);
        if (gRing.waiting.load(std::memory_order_seq_cst)) { std::lock_guard<std::mutex> l(gRing.m); gRing.done.notify_all(); }
    }
    gBusyNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}
void Publish() {
    gRing.pub = gRing.wr;
    if (!gHost.threaded) { gRing.head.store(gRing.wr); RunTo(gRing.wr); return; }
    gRing.head.store(gRing.wr, std::memory_order_seq_cst);
    if (gRing.sleeping.load(std::memory_order_seq_cst)) { std::lock_guard<std::mutex> l(gRing.m); gRing.work.notify_one(); }
}
// until the render thread did every command before byte pos
void WaitFor(uint64_t pos) {
    if (gRing.tail.load(std::memory_order_acquire) >= pos) return;
    const auto t0 = std::chrono::steady_clock::now();
    for (int spin = 0; spin < 2000 && gRing.tail.load(std::memory_order_acquire) < pos; ++spin) std::this_thread::yield();
    if (gRing.tail.load(std::memory_order_acquire) < pos) {
        std::unique_lock<std::mutex> l(gRing.m);
        gRing.waiting.store(true, std::memory_order_seq_cst);
        while (gRing.tail.load(std::memory_order_seq_cst) < pos) gRing.done.wait_for(l, std::chrono::milliseconds(2));
        gRing.waiting.store(false);
    }
    ++sWaits;
    sWaitUs += (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}
// room for a command of payload bytes: its payload (filled by the caller before Publish)
void* Put(uint32_t type, size_t payload) {
    const size_t n = (sizeof(CmdHead) + payload + 15) & ~(size_t)15;
    size_t pos = gRing.wr % kRingBytes;
    auto room = [&](size_t need) {
        if (gRing.wr + need - gRing.tailSeen <= kRingBytes) return;   // the tail read again only when short of room
        gRing.tailSeen = gRing.tail.load(std::memory_order_acquire);
        if (gRing.wr + need - gRing.tailSeen <= kRingBytes) return;
        Publish();
        WaitFor(gRing.wr + need - kRingBytes);
        gRing.tailSeen = gRing.tail.load(std::memory_order_acquire);
    };
    if (pos + n > kRingBytes) {                              // not enough before the end: a pad to it
        const size_t pad = kRingBytes - pos;
        room(pad);
        CmdHead* h = (CmdHead*)(gRing.buf + pos); h->type = kPad; h->size = (uint32_t)pad;
        gRing.wr += pad; pos = 0;
    }
    room(n);
    CmdHead* h = (CmdHead*)(gRing.buf + pos); h->type = type; h->size = (uint32_t)n;
    gRing.wr += n;
    return h + 1;
}
bool Setup(std::string& error);
bool VkSetup(std::string& error);                              // the Vulkan backend's (gs_vk.cpp)
bool gGlWork = false;                                          // GL calls since the last glFlush
void RenderThread(std::string* error, std::mutex* m, std::condition_variable* cv, int* state) {
    bool ok = true;
    if (!gVk && gHost.begin && !gHost.begin(gHost.user, *error)) ok = false;
    if (ok) {
        std::string e;
        if (!(gVk ? VkSetup(e) : Setup(e))) { *error = e; ok = false; }
    }
    { std::lock_guard<std::mutex> l(*m); *state = ok ? 1 : 2; }
    cv->notify_all();
    if (!ok) return;
    for (;;) {
        const uint64_t head = gRing.head.load(std::memory_order_acquire);
        if (gRing.tail.load(std::memory_order_relaxed) < head) { RunTo(head); gGlWork = true; continue; }
        for (int spin = 0; spin < 200 && gRing.head.load(std::memory_order_acquire) == head; ++spin) std::this_thread::yield();
        if (gRing.head.load(std::memory_order_acquire) != head) continue;
        if (gGlWork) {                                         // idle: the GPU works on what it has
            if (gVk) { gVulkanBackend->idle(); if (const std::string* why = gVulkanBackend->fault()) FromVulkan(*why); }
            else if (!gDead) gl.Flush();
            gGlWork = false;
        }
        std::unique_lock<std::mutex> l(gRing.m);
        gRing.sleeping.store(true, std::memory_order_seq_cst);
        if (gRing.head.load(std::memory_order_seq_cst) == head) gRing.work.wait_for(l, std::chrono::milliseconds(4));
        gRing.sleeping.store(false);
    }
}

// the gs.cpp side's view: the registers last sent, the CLUT last sent, the pages the GPU may have drawn
Regs gSentRegs{}; bool gHaveSentRegs = false;
bool VkSetup(std::string& error) { return gVulkanBackend->setup(gHost, gScale, gScaleX, gVram, error, gDescription); }
PageSet gDrawnPages;
// Vulkan's CLUT loads from the processor's copy (ClutFromCpu): a block a transfer wrote whole after the GPU last drew
// on its page is the processor's (epochs: one per transfer; a page's last drawing, a block's last whole write)
uint32_t gEpoch = 1, gDrawEpoch[512] = {}, gXferEpoch[16384] = {};
uint32_t gDrawnGen = 0;                                        // gDrawnPages changed other than by a draw (Draw's last pages)

}  // namespace

// the GL objects (on the render thread, the context current)
namespace {
bool Setup(std::string& error) {
    if (!gl.Load(gHost.getProc)) { error = std::string("missing ") + (gl.missing ? gl.missing : "?"); return false; }
    gDispatch = (PFNGLDISPATCHCOMPUTEPROC)gHost.getProc("glDispatchCompute");
    gBarrier = (PFNGLMEMORYBARRIERPROC)gHost.getProc("glMemoryBarrier");
    gUniform1ui = (PFNGLUNIFORM1UIPROC)gHost.getProc("glUniform1ui");
    gBindImage = (PFNGLBINDIMAGETEXTUREPROC)gHost.getProc("glBindImageTexture");
    gGenMipmap = (PFNGLGENERATEMIPMAPPROC)gHost.getProc("glGenerateMipmap");
    gGenSamplers = (PFNGLGENSAMPLERSPROC)gHost.getProc("glGenSamplers");
    gBindSampler = (PFNGLBINDSAMPLERPROC)gHost.getProc("glBindSampler");
    gSamplerI = (PFNGLSAMPLERPARAMETERIPROC)gHost.getProc("glSamplerParameteri");
    gSamplerF = (PFNGLSAMPLERPARAMETERFPROC)gHost.getProc("glSamplerParameterf");
    gCompressedTexImage = (PFNGLCOMPRESSEDTEXIMAGE2DPROC)gHost.getProc("glCompressedTexImage2D");
    {   // S3TC (BC1 / BC3): every desktop driver, an extension of the core profile
        auto getStringi = (PFNGLGETSTRINGIPROC)gHost.getProc("glGetStringi");
        GLint n = 0; gl.GetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint k = 0; getStringi && k < n && !gS3tc; ++k) {
            const char* e = (const char*)getStringi(GL_EXTENSIONS, (GLuint)k);
            if (e && (!strcmp(e, "GL_EXT_texture_compression_s3tc") || !strcmp(e, "GL_NV_texture_compression_s3tc"))) gS3tc = gCompressedTexImage != nullptr;
        }
    }
    if (!gDispatch || !gBarrier || !gUniform1ui || !gBindImage || !gl.ClipControl || !gGenMipmap || !gGenSamplers || !gBindSampler || !gSamplerI || !gSamplerF) {
        error = "OpenGL 4.3 with clip control needed"; return false;
    }
    const char* ver = (const char*)gl.GetString(GL_VERSION);
    const char* ren = (const char*)gl.GetString(GL_RENDERER);
    gDescription = std::string("OpenGL ") + (ver ? ver : "?") + " on " + (ren ? ren : "?") +
                   (gS3tc ? ", S3TC (the HD images stay compressed)" : ", no S3TC (the HD images decoded first)");
    const std::string head = "#version 430 core\n";
    std::string err;
    gProg = Program(kDrawVs, (head + kMemGlsl + kDrawFs).c_str(), err);
    if (!gProg) { error = "draw program: " + err; return false; }
    gLoadProg = Program(kLoadVs, (head + kMemGlsl + kLoadFs).c_str(), err);
    if (!gLoadProg) { error = "load program: " + err; return false; }
    gPresentProg = Program(kPresentVs, kPresentFs, err);
    if (!gPresentProg) { error = "present program: " + err; return false; }
    gOverlayProg = Program(kPresentVs, kOverlayFs, err);
    if (!gOverlayProg) { error = "overlay program: " + err; return false; }
    gMenuProg = Program(kPresentVs, kMenuFs, err);
    if (!gMenuProg) { error = "menu program: " + err; return false; }
    auto computeProgram = [&](const std::string& src, const char* what) -> GLuint {
        GLuint c = Compile(GL_COMPUTE_SHADER, src.c_str(), err);
        if (!c) { error = std::string(what) + " program: " + err; return 0; }
        GLuint p = gl.CreateProgram(); gl.AttachShader(p, c); gl.LinkProgram(p);
        GLint ok = 0; gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) { error = std::string(what) + " program link"; return 0; }
        return p;
    };
    if (!(gStoreProg = computeProgram(kStoreCs, "store"))) return false;
    if (!(gXferProg = computeProgram(head + kMemGlsl + kXferCs, "transfer"))) return false;
    if (!(gClutProg = computeProgram(head + kMemGlsl + kClutCs, "CLUT"))) return false;
    gUs = UniformShadow{};                             // a new program: its uniforms set again
    L.posScale = gl.GetUniformLocation(gProg, "uPosScale"); L.zScale = gl.GetUniformLocation(gProg, "uZScale");
    L.zMax = gl.GetUniformLocation(gProg, "uZMax"); L.tex0 = gl.GetUniformLocation(gProg, "uTex0");
    L.texSize = gl.GetUniformLocation(gProg, "uTexSize"); L.wrap = gl.GetUniformLocation(gProg, "uWrap");
    L.region = gl.GetUniformLocation(gProg, "uRegion"); L.texFx = gl.GetUniformLocation(gProg, "uTexFx");
    L.texA = gl.GetUniformLocation(gProg, "uTexA"); L.fog = gl.GetUniformLocation(gProg, "uFog");
    L.atest = gl.GetUniformLocation(gProg, "uAtest"); L.rtSample = gl.GetUniformLocation(gProg, "uRtSample");
    L.srcF = gl.GetUniformLocation(gProg, "uSrcF");
    L.hd = gl.GetUniformLocation(gProg, "uHd");
    if (getenv("MGO1_GPU_DEBUG")) fprintf(stderr, "uHd at %d, uTex0 at %d\n", (int)L.hd, (int)L.tex0);
    // the HD images' samplers: trilinear, anisotropic when the driver has it; repeat or clamp per axis
    gGenSamplers(4, gHdSamplers);
    for (int k = 0; k < 4; ++k) {
        gSamplerI(gHdSamplers[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gSamplerI(gHdSamplers[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gSamplerI(gHdSamplers[k], GL_TEXTURE_WRAP_S, (k & 1) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        gSamplerI(gHdSamplers[k], GL_TEXTURE_WRAP_T, (k & 2) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        gSamplerF(gHdSamplers[k], GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    }
    gl.GetError();                                           // anisotropy is optional (GL 4.6 or the extension)
    LL.target = gl.GetUniformLocation(gLoadProg, "uTarget"); LL.buf = gl.GetUniformLocation(gLoadProg, "uBuf");
    LL.scaleY = gl.GetUniformLocation(gLoadProg, "uScaleY");
    SL.buf = gl.GetUniformLocation(gStoreProg, "uBuf"); SL.rect = gl.GetUniformLocation(gStoreProg, "uRect");
    SL.scaleY = gl.GetUniformLocation(gStoreProg, "uScaleY");
    PL.dst = gl.GetUniformLocation(gPresentProg, "uDst"); PL.src = gl.GetUniformLocation(gPresentProg, "uSrc");
    PL.step = gl.GetUniformLocation(gPresentProg, "uStep"); PL.mode = gl.GetUniformLocation(gPresentProg, "uMode");
    OL.dst = gl.GetUniformLocation(gOverlayProg, "uDst"); OL.src = gl.GetUniformLocation(gOverlayProg, "uSrc");
    OL.color = gl.GetUniformLocation(gOverlayProg, "uColor");
    ML.dst = gl.GetUniformLocation(gMenuProg, "uDst"); ML.src = gl.GetUniformLocation(gMenuProg, "uSrc");
    XL.dst = gl.GetUniformLocation(gXferProg, "uDst"); XL.rect = gl.GetUniformLocation(gXferProg, "uRect");
    XL.count = gl.GetUniformLocation(gXferProg, "uCount");
    CL.c0 = gl.GetUniformLocation(gClutProg, "uClut0"); CL.c1 = gl.GetUniformLocation(gClutProg, "uClut1");
    // the memory, the CLUT, the tables
    gl.GenTextures(1, &gMem); gl.BindTexture(GL_TEXTURE_2D, gMem);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, gVram);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.GenTextures(1, &gClut); gl.BindTexture(GL_TEXTURE_2D, gClut);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 512, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    static uint16_t lut[68][32];
    GsSwizzleTables(lut);
    gl.GenTextures(1, &gLut); gl.BindTexture(GL_TEXTURE_2D, gLut);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 2);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 32, 68, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, lut);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.GenTextures(1, &gXferData); gl.BindTexture(GL_TEXTURE_2D, gXferData);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gBindImage(0, gMem, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    gBindImage(1, gClut, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
    // the vertex layout
    gl.GenVertexArrays(1, &gVao); gl.BindVertexArray(gVao);
    gl.GenBuffers(1, &gVbo); gl.BindBuffer(GL_ARRAY_BUFFER, gVbo);
    gl.EnableVertexAttribArray(0); gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GV), (void*)0);
    gl.EnableVertexAttribArray(1); gl.VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(GV), (void*)8);
    gl.EnableVertexAttribArray(2); gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GV), (void*)12);
    gl.EnableVertexAttribArray(3); gl.VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(GV), (void*)16);
    gl.EnableVertexAttribArray(4); gl.VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(GV), (void*)28);
    gl.GenVertexArrays(1, &gEmptyVao);
    gl.ClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    gl.ProvokingVertex(GL_LAST_VERTEX_CONVENTION);
    memset(gOwner, 0, sizeof gOwner); gOwned = PageSet{};
    gGpuNewer = PageSet{};
    if (gl.GetError() != GL_NO_ERROR) { error = "GL error during set-up"; return false; }
    return true;
}
}  // namespace

bool Start(const Host& host, int scale, uint32_t* vram, std::string& error) {
    if (gOn) return true;
    gHost = host;
    gVk = host.api == 1;                                     // Vulkan (gs_vk.cpp), else OpenGL
    if (gVk && !gVulkanBackend) { error = "this build has no Vulkan renderer"; return false; }
    gAspect = host.aspect;
    gScale = std::clamp(scale, 1, 8);
    gScaleX = host.scaleX > 0 ? std::clamp(host.scaleX, gScale, 12) : gScale;
    gVram = vram;
    if (!gRing.buf) gRing.buf = new uint8_t[kRingBytes];
    if (!host.threaded) {
        if (!gVk && host.begin && !host.begin(host.user, error)) return false;
        if (!(gVk ? VkSetup(error) : Setup(error))) return false;
        gOn = true;
        error = gDescription;
        return true;
    }
    std::mutex m; std::condition_variable cv; int state = 0;
    gThread = std::thread(RenderThread, &error, &m, &cv, &state);
    {
        std::unique_lock<std::mutex> l(m);
        cv.wait(l, [&] { return state != 0; });
    }
    if (state != 1) { gThread.join(); return false; }
    gThread.detach();                                        // runs to the end of the program
    gOn = true;
    error = gDescription;
    return true;
}
bool On() { return gOn; }
void SetAspect(float aspect) { gAspect.store(aspect, std::memory_order_relaxed); }
void SetScale(int scale, int scaleX) {
    scale = std::clamp(scale, 1, 8);
    gWantScaleX.store(std::clamp(scaleX, scale, 12)); gWantScale.store(scale);
}
void SetSmooth(bool smooth) { gSmooth.store(smooth ? 1 : 0, std::memory_order_relaxed); }
void SetReplacementFolder(const std::string& dir) {
    hdtex::Settings s = hdtex::Current();
    s.packDir = dir; s.pack = true;
    hdtex::Configure(s);
}
void SetReplacements(bool on) { gReplaceOn = on; }
int Replacements() { return hdtex::PackCount(); }
int Scale() { return gScale; }

void Draw(int kind, const Regs& r, const Vtx& a, const Vtx& b, const Vtx& c, int hd) {
    int bx0, by0, bx1, by1;
    if (!PrimBox(kind, r, a, b, c, bx0, by0, bx1, by1)) return;
    // the pages it may draw: a CPU read of them waits for the GPU (depth never reaches the memory); the last draw's
    // pages not marked again
    const uint32_t fbp = Bits(r.frame, 0, 9) * 32, fbw = Bits(r.frame, 16, 6), fpsm = Bits(r.frame, 24, 6);
    {
        const bool vk = gVk.load(std::memory_order_relaxed);
        const Layout L = LayoutOf(fpsm, fbw ? fbw : 1);
        const uint32_t k[9] = {fbp, fbw, fpsm, (uint32_t)(std::max(0, bx0) / L.pw), (uint32_t)(std::max(0, by0) / L.ph),
                               (uint32_t)(std::min(bx1, 2047) / L.pw), (uint32_t)(std::min(by1, 2047) / L.ph), vk ? gEpoch : 0, gDrawnGen};
        static uint32_t last[9] = {~0u};
        if (memcmp(k, last, sizeof k)) { PagesOf(gDrawnPages, fbp, fbw, fpsm, bx0, by0, bx1, by1, vk ? gDrawEpoch : nullptr, gEpoch); memcpy(last, k, sizeof k); }
    }
    if (!gHaveSentRegs || memcmp(&r, &gSentRegs, sizeof r)) {
        *(Regs*)Put(kRegs, sizeof(Regs)) = r;
        gSentRegs = r; gHaveSentRegs = true;
    }
    DrawCmd* d = (DrawCmd*)Put(kDraw, sizeof(DrawCmd));
    d->kind = kind; d->hd = hd; d->v[0] = a; d->v[1] = b; d->v[2] = c;
    d->box[0] = (int16_t)bx0; d->box[1] = (int16_t)by0; d->box[2] = (int16_t)bx1; d->box[3] = (int16_t)by1;
    if (gRing.wr - gRing.pub >= kPublishBytes) Publish();
}
void Kick() { if (gOn && gRing.wr != gRing.pub) Publish(); }

void Transfer(uint32_t bp, uint32_t bw, uint32_t psm, int x, int y, int w, int h, std::vector<uint8_t>&& pixels) {
    if (!gOn || w <= 0 || h <= 0 || pixels.empty()) return;
    if (gVk.load(std::memory_order_relaxed)) {                      // the blocks it writes whole (ClutFromCpu)
        int bwPx = 0, bhPx = 0, shift = 0;
        switch (psm & 0x3f) {
        case 0x00: case 0x30: bwPx = 8; bhPx = 8; shift = 6; break;                       // CT32, Z32
        case 0x02: case 0x0a: case 0x32: case 0x3a: bwPx = 16; bhPx = 8; shift = 7; break;  // CT16, CT16S, Z16, Z16S
        case 0x13: bwPx = 16; bhPx = 16; shift = 8; break;                                 // T8
        case 0x14: bwPx = 32; bhPx = 16; shift = 9; break;                                 // T4
        default: break;
        }
        if (bwPx) {
            ++gEpoch;
            for (int ty = (y + bhPx - 1) / bhPx * bhPx; ty + bhPx <= y + h; ty += bhPx)
                for (int tx = (x + bwPx - 1) / bwPx * bwPx; tx + bwPx <= x + w; tx += bwPx)
                    gXferEpoch[(GsPixelAddress(psm, tx, ty, bp, bw) >> shift) & 0x3fff] = gEpoch;
        }
    }
    XferCmd* c = (XferCmd*)Put(kXfer, sizeof(XferCmd));
    c->bp = bp; c->bw = bw; c->psm = psm; c->x = x; c->y = y; c->w = w; c->h = h;
    c->pixels = new std::vector<uint8_t>(std::move(pixels));
    Publish();
}

namespace {
// Vulkan: a CLUT load whose pages the GPU drew none of made here from the processor's copy of the memory (kClutData:
// no compute pass on the render side), as the GPU's CLUT load (clut shader) reads them; the entries the GPU loaded
// (gQClutGpu) are not known here, so a load leaving one of them outside its range goes to the GPU as well
uint32_t gQClut[512] = {};
uint64_t gQClutGpu[8] = {};
struct ClutPlaces { uint16_t blk[256], col[256]; };
const ClutPlaces& Csm1Places(uint32_t cpsm, bool eight) {       // CSM1's entries: block and word from the CLUT's block
    static const ClutPlaces* p = [] {
        static ClutPlaces t[3][2];
        for (int l = 0; l < 3; ++l)
            for (int e = 0; e < 2; ++e)
                for (int i = 0; i < (e ? 256 : 16); ++i) {
                    int x, y;
                    if (e) { const int j = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); x = j & 15; y = j >> 4; }
                    else { x = i & 7; y = i >> 3; }
                    const uint32_t a = GsPixelAddress(l == 0 ? 0x00 : l == 1 ? 0x02 : 0x0a, x, y, 0, 1);
                    t[l][e].blk[i] = (uint16_t)(a >> (l ? 7 : 6)); t[l][e].col[i] = (uint16_t)(a & (l ? 127 : 63));
                }
        return &t[0][0];
    }();
    return p[(cpsm == 0 ? 0 : cpsm == 0x02 ? 1 : 2) * 2 + (eight ? 1 : 0)];
}
bool ClutFromCpu(const ClutLoad& c) {
    const int slot = std::max(0, c.slot), n = std::min((int)c.entries, 512 - slot);
    if (n <= 0 || n > 256 || (c.csm != 0 && c.x0 + (int)c.entries > 2048)) return false;
    const uint32_t cpsm = (c.cpsm == 0x02 || c.cpsm == 0x0a) ? c.cpsm : 0, bw = c.bw ? c.bw : 1;
    uint64_t range[8] = {};
    for (int i = slot; i < slot + n; ++i) range[i >> 6] |= 1ull << (i & 63);
    bool unknown = false;
    for (int k = 0; k < 8; ++k) unknown |= (gQClutGpu[k] & ~range[k]) != 0;
    // each entry's block the processor's: its page not drawn by the GPU, or written whole since
    auto mine = [](uint32_t bn) { const uint32_t pg = (bn >> 5) & 511; return !gDrawnPages.Has(pg) || gXferEpoch[bn & 0x3fff] > gDrawEpoch[pg]; };
    const uint16_t* v16 = (const uint16_t*)gVram;
    uint32_t e[256], last = ~0u;
    bool ok = !unknown;
    for (int i = 0; ok && i < n; ++i) {
        uint32_t bn, a;
        if (c.csm == 0) {
            const ClutPlaces& pl = Csm1Places(cpsm, c.entries == 256);
            bn = (c.cbp + pl.blk[i]) & 0x3fff;
            a = (bn << (cpsm ? 7 : 6)) + pl.col[i];
        } else {
            a = GsPixelAddress(cpsm, c.x0 + i, c.y0, c.cbp, bw);
            bn = (a >> (cpsm ? 7 : 6)) & 0x3fff;
        }
        if (bn != last) { last = bn; ok = mine(bn); }
        e[i] = cpsm ? v16[a & 0x1fffff] : gVram[a & 0xfffff];
    }
    if (!ok) { for (int k = 0; k < 8; ++k) gQClutGpu[k] |= range[k]; return false; }
    memcpy(gQClut + slot, e, (size_t)n * 4);
    for (int k = 0; k < 8; ++k) gQClutGpu[k] &= ~range[k];
    ClutDataCmd* d = (ClutDataCmd*)Put(kClutData, sizeof(ClutDataCmd) + (size_t)n * 4);
    d->slot = (uint32_t)slot; d->n = (uint32_t)n;
    memcpy(d + 1, gQClut + slot, (size_t)n * 4);
    Publish();
    return true;
}
}  // namespace
void LoadClut(const ClutLoad& cl) {
    if (!gOn) return;
    static const bool cpu = !getenv("MGO1_VK_CLUT_GPU");              // tests: every CLUT load through the GPU
    if (cpu && gVk.load(std::memory_order_relaxed) && ClutFromCpu(cl)) return;
    *(ClutLoad*)Put(kClutLoad, sizeof(ClutLoad)) = cl;
    Publish();
}

void SetClut(const uint32_t words[512]) {
    if (!gOn) return;
    memcpy(gQClut, words, sizeof gQClut); memset(gQClutGpu, 0, sizeof gQClutGpu);
    memcpy(((ClutCmd*)Put(kClutSet, sizeof(ClutCmd)))->words, words, 512 * sizeof(uint32_t));
    Publish();
}

void Flush() {
    if (!gOn) return;
    Put(kSync, 0);
    Publish();
    WaitFor(gRing.wr);
}

void CpuWroteAll() {
    if (!gOn) return;
    ((AllCmd*)Put(kWroteAll, sizeof(AllCmd)))->data = new std::vector<uint32_t>(gVram, gVram + 1024 * 1024);
    gDrawnPages = PageSet{}; ++gDrawnGen;
    Publish();
}

void CpuWillRead(const uint64_t pages[8]) {
    if (!gOn) return;
    PageSet p; for (int k = 0; k < 8; ++k) p.w[k] = pages[k] & gDrawnPages.w[k];
    if (!p.Any()) return;                                    // the GPU drew none of them: nothing to wait for
    static const bool trace = getenv("MGO1_GPU_TRACE") != nullptr;
    if (trace) {
        char b[160]; int n = 0;
        for (int q = 0; q < 512 && n < 140; ++q) if (p.Has(q)) n += snprintf(b + n, sizeof b - n, " %03x", q);
        Say("CPU read waits for pages%s", b);
    }
    ((TouchCmd*)Put(kTouch, sizeof(TouchCmd)))->pages = p;
    Publish();
    WaitFor(gRing.wr);
    gDrawnPages.Remove(p); ++gDrawnGen;
}

std::atomic<uint64_t> sPresentsLeftOut{0};
void Present(uint64_t pmode, const uint64_t dispfb[2], const uint64_t display[2]) {
    if (!gOn) return;
    // at most two pictures ahead of the screen: a newer one is left out, never waited for (the GS thread calls this
    // with the game's next drawing and its FINISH queued behind; docs/FPS60.md, The screen's refresh)
    if (gHost.threaded && gPresentsQueued - gPresentsDone.load(std::memory_order_acquire) >= 2) { ++sPresentsLeftOut; return; }
    ScreenCmd* s = (ScreenCmd*)Put(kPresent, sizeof(ScreenCmd));
    s->pmode = pmode; s->dispfb[0] = dispfb[0]; s->dispfb[1] = dispfb[1]; s->display[0] = display[0]; s->display[1] = display[1];
    Publish();
    ++gPresentsQueued;
}
uint64_t PresentsLeftOut() { return sPresentsLeftOut.load(); }

bool Picture(uint64_t pmode, const uint64_t dispfb[2], const uint64_t display[2], std::vector<uint32_t>& rgba, int& w, int& h) {
    if (!gOn) return false;
    bool ok = false;
    PictureCmd* c = (PictureCmd*)Put(kPicture, sizeof(PictureCmd));
    c->screen.pmode = pmode; c->screen.dispfb[0] = dispfb[0]; c->screen.dispfb[1] = dispfb[1];
    c->screen.display[0] = display[0]; c->screen.display[1] = display[1];
    c->rgba = &rgba; c->w = &w; c->h = &h; c->ok = &ok;
    Publish();
    WaitFor(gRing.wr);
    return ok;
}

std::string Stats() {
    if (gVk) return gVulkanBackend->stats(sWaits.exchange(0), sWaitUs.exchange(0));   // the Vulkan backend's counters
    if (gDead) return "stopped (Vulkan stopped, no OpenGL)";
    char b[600];
    char hd[200] = "";
    if (sHdImages.load() || sHdDraws.load())
        snprintf(hd, sizeof hd, ", HD: %llu images (%llu MB), %llu draws", (unsigned long long)sHdImages.exchange(0),
                 (unsigned long long)(sHdBytes.exchange(0) >> 20), (unsigned long long)sHdDraws.exchange(0));
    if (const uint64_t n = sCopies.exchange(0)) {
        const size_t at = strlen(hd);
        snprintf(hd + at, sizeof hd - at, ", %llu target copies (%llu MB)", (unsigned long long)n, (unsigned long long)(sCopyBytes.exchange(0) >> 20));
    }
    snprintf(b, sizeof b, "%llu draws, %llu batches, %llu transfers (%llu KB), %llu CLUT loads, %llu uploads, %llu stores, %llu loads (%llu px), "
             "%llu downloads, %llu pictures, %d buffers, %llu waits (%llu us)%s",
             (unsigned long long)sDraws.exchange(0), (unsigned long long)sBatches.exchange(0), (unsigned long long)sXfers.exchange(0),
             (unsigned long long)(sXferBytes.exchange(0) >> 10), (unsigned long long)sClutLoads.exchange(0), (unsigned long long)sUploads.exchange(0),
             (unsigned long long)sStores.exchange(0), (unsigned long long)sLoads.exchange(0), (unsigned long long)sLoadPx.exchange(0),
             (unsigned long long)sDownloads.exchange(0), (unsigned long long)sPresents.exchange(0), sBufs.load(),
             (unsigned long long)sWaits.exchange(0), (unsigned long long)sWaitUs.exchange(0), hd);
    return b + BusyText();
}

}  // namespace gsgpu
}  // namespace ps2
