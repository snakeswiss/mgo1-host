// gs.cpp - the PS2 world's own display (the simulation's SIM_DG mode, and the standalone
// renderer's reference): the GIF (PATH1 / PATH2 / PATH3 packets: PACKED, REGLIST, IMAGE), the
// GS registers, its 4 MB local memory with the PS2's pixel storage layouts, transfers,
// a software rasterizer (points, lines, triangles, strips, fans, sprites; Gouraud colour,
// textures with CLUTs, fog, alpha / destination alpha / depth tests, alpha blending, masks)
// and the display circuits (PMODE, DISPFB, DISPLAY) read back as an RGBA picture.
// Behaviour follows the GS User's Manual; the swizzle tables are the hardware's.
#include "ps2rt.h"
#include "gs_gpu.h"
#include "hdtex.h"
#include "gs_thread.h"
#include <set>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <array>
#include <map>
#include <unordered_map>
#include <string>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace ps2 {

bool gGsDraw = true;
// MGO1_HEADLESS=1 (a dedicated host): no picture at all. VU1 does not run (vu0.cpp) and the GIF
// takes only the writes the game can see back: SIGNAL, FINISH, LABEL (CSR, SIGLBLID, the GS
// interrupt); the tags are still followed so that a tag cut over several transfers stays whole
bool gHeadless = false;
long long (*gGsClock)() = nullptr;
void (*gGsPresented)() = nullptr;
namespace { bool gFieldByVSync = false; int gField = 0; int gFinishEvents = 0; }   // FINISH events not yet cleared by a handler
namespace { uint64_t gGsEvents[4][3] = {}; }                 // SIGNAL, FINISH, LABEL per path since the last GsStats (log)
namespace {
std::mutex gEvMu;                       // CSR, IMR, SIGLBLID and the FINISH events: the GS thread raises them, the game thread reads and clears them
std::atomic<long long> gFinishAt{0};    // the last FINISH's time (gGsClock)
inline void RaiseGsIrq() {                // the GS thread's events reach the kernel even while every PS2 thread waits
    __atomic_fetch_or(&gIrqPending, 1u << 16, __ATOMIC_SEQ_CST);
    if (gIrqWake && GsOnThread()) gIrqWake();
}
bool gGsDrawNow = true;                 // the GS thread: gGsDraw as the game thread had it for the data being drawn
}
void (*gGsPrimHook)(const GsPrimInfo&) = nullptr;
void (*gGsXferHook)(const GsXferInfo&) = nullptr;
int gGifPathNow = 0;                    // the packet being drawn: its path and number (the replay tool's draw log)
long long gGifPacketNow = 0;
void ImageData(const uint8_t* p, size_t n);                  // IMAGE data (HWREG, GIF IMAGE mode)
void NoteShown(uint64_t dispfb);                             // a frame buffer the display circuits show (widescreen)
extern void (*gLog)(const char* fmt, ...);

namespace {

// ---- local memory and its layouts
uint32_t gVram[1024 * 1024];                               // 4 MB
inline uint8_t* Vram8() { return (uint8_t*)gVram; }
inline uint16_t* Vram16() { return (uint16_t*)gVram; }

const uint8_t kBlock32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
                                {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
const uint8_t kBlock16[8][4] = {{0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
                                {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
const uint8_t kBlock16S[8][4] = {{0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26}, {9, 11, 25, 27},
                                 {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};
const uint8_t kColumn32[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15},
                                 {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
                                 {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47},
                                 {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
const uint8_t kColumn16[8][16] = {
    {0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
    {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31},
    {32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59},
    {36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63},
    {64, 66, 72, 74, 80, 82, 88, 90, 65, 67, 73, 75, 81, 83, 89, 91},
    {68, 70, 76, 78, 84, 86, 92, 94, 69, 71, 77, 79, 85, 87, 93, 95},
    {96, 98, 104, 106, 112, 114, 120, 122, 97, 99, 105, 107, 113, 115, 121, 123},
    {100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127}};
const uint8_t kColumn8[16][16] = {
    {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
    {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
    {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
    {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31},
    {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
    {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
    {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
    {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127},
    {128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182},
    {136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190},
    {161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151},
    {169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159},
    {224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214},
    {232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222},
    {193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247},
    {201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255}};
const uint16_t kColumn4[16][32] = {
    {0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63},
    {192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174},
    {208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190},
    {129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239},
    {145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255},
    {256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362, 260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366},
    {272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378, 276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382},
    {321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299, 325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303},
    {337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315, 341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319},
    {448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426, 452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430},
    {464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442, 468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446},
    {385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491, 389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495},
    {401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507, 405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511}};

enum : uint32_t {
    PSMCT32 = 0x00, PSMCT24 = 0x01, PSMCT16 = 0x02, PSMCT16S = 0x0a, PSMT8 = 0x13, PSMT4 = 0x14,
    PSMT8H = 0x1b, PSMT4HL = 0x24, PSMT4HH = 0x2c, PSMZ32 = 0x30, PSMZ24 = 0x31, PSMZ16 = 0x32, PSMZ16S = 0x3a
};
// the swizzle family of a storage mode: 0 = 32-bit, 1 = 16, 2 = 16S, 3 = 8, 4 = 4
int Family(uint32_t psm) {
    switch (psm & 0x3f) {
    case PSMCT16: case PSMZ16: return 1;
    case PSMCT16S: case PSMZ16S: return 2;
    case PSMT8: return 3;
    case PSMT4: return 4;
    default: return 0;
    }
}
inline bool IsZ(uint32_t psm) { return (psm & 0x30) == 0x30; }
// pixel addresses in units of the storage mode's pixel (32 / 16 / 8 / 4 bits): the page (bw
// 64-pixel units, halved for the 8 / 4-bit pages of 128 pixels), the block in the page (the
// Z modes' blocks swapped by 0x18, as PCSX2's blockTable*Z), the column swizzle in the block
inline uint32_t Addr32(uint32_t x, uint32_t y, uint32_t bp, uint32_t bw, uint32_t zx) {
    const uint32_t bn = bp + ((y >> 5) * bw + (x >> 6)) * 32 + (kBlock32[(y >> 3) & 3][(x >> 3) & 7] ^ zx);
    return ((bn & 0x3fff) << 6) + kColumn32[y & 7][x & 7];
}
inline uint32_t Addr16(uint32_t x, uint32_t y, uint32_t bp, uint32_t bw, uint32_t zx, bool s) {
    const uint32_t blk = s ? kBlock16S[(y >> 3) & 7][(x >> 4) & 3] : kBlock16[(y >> 3) & 7][(x >> 4) & 3];
    const uint32_t bn = bp + ((y >> 6) * bw + (x >> 6)) * 32 + (blk ^ zx);
    return ((bn & 0x3fff) << 7) + kColumn16[y & 7][x & 15];
}
inline uint32_t Addr8(uint32_t x, uint32_t y, uint32_t bp, uint32_t bw) {
    const uint32_t bn = bp + ((y >> 6) * (bw >> 1) + (x >> 7)) * 32 + kBlock32[(y >> 4) & 3][(x >> 4) & 7];
    return ((bn & 0x3fff) << 8) + kColumn8[y & 15][x & 15];
}
inline uint32_t Addr4(uint32_t x, uint32_t y, uint32_t bp, uint32_t bw) {
    const uint32_t bn = bp + ((y >> 7) * (bw >> 1) + (x >> 7)) * 32 + kBlock16[(y >> 4) & 7][(x >> 5) & 3];
    return ((bn & 0x3fff) << 9) + kColumn4[y & 15][x & 31];
}
inline uint32_t PixelAddress(uint32_t psm, int x, int y, uint32_t bp, uint32_t bw) {
    const uint32_t ux = (uint32_t)x & 2047, uy = (uint32_t)y & 2047;
    switch (psm & 0x3f) {
    case PSMCT16: return Addr16(ux, uy, bp, bw, 0, false);
    case PSMZ16: return Addr16(ux, uy, bp, bw, 0x18, false);
    case PSMCT16S: return Addr16(ux, uy, bp, bw, 0, true);
    case PSMZ16S: return Addr16(ux, uy, bp, bw, 0x18, true);
    case PSMT8: return Addr8(ux, uy, bp, bw);
    case PSMT4: return Addr4(ux, uy, bp, bw);
    case PSMZ32: case PSMZ24: return Addr32(ux, uy, bp, bw, 0x18);
    default: return Addr32(ux, uy, bp, bw, 0);          // CT32 / CT24 and the 8H / 4HL / 4HH views
    }
}
uint32_t ReadPixel(uint32_t psm, int x, int y, uint32_t bp, uint32_t bw) {
    const uint32_t a = PixelAddress(psm, x, y, bp, bw);
    switch (psm & 0x3f) {
    case PSMCT32: case PSMZ32: return gVram[a & 0xfffff];
    case PSMCT24: case PSMZ24: return gVram[a & 0xfffff] & 0xffffff;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return Vram16()[a & 0x1fffff];
    case PSMT8: return Vram8()[a & 0x3fffff];
    case PSMT4: return (Vram8()[(a >> 1) & 0x3fffff] >> ((a & 1) * 4)) & 15;
    case PSMT8H: return gVram[a & 0xfffff] >> 24;
    case PSMT4HL: return (gVram[a & 0xfffff] >> 24) & 15;
    case PSMT4HH: return gVram[a & 0xfffff] >> 28;
    }
    return 0;
}
void WritePixel(uint32_t psm, int x, int y, uint32_t bp, uint32_t bw, uint32_t v, uint32_t keep = 0) {
    const uint32_t a = PixelAddress(psm, x, y, bp, bw);
    switch (psm & 0x3f) {
    case PSMCT32: case PSMZ32: { uint32_t& d = gVram[a & 0xfffff]; d = (d & keep) | (v & ~keep); return; }
    case PSMCT24: case PSMZ24: { uint32_t& d = gVram[a & 0xfffff]; const uint32_t m = 0xff000000u | keep; d = (d & m) | (v & ~m); return; }
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: { uint16_t& d = Vram16()[a & 0x1fffff]; d = (uint16_t)((d & keep) | (v & ~keep)); return; }
    case PSMT8: Vram8()[a & 0x3fffff] = (uint8_t)v; return;
    case PSMT4: { uint8_t& d = Vram8()[(a >> 1) & 0x3fffff]; d = (a & 1) ? (uint8_t)((d & 0x0f) | (v << 4)) : (uint8_t)((d & 0xf0) | (v & 15)); return; }
    case PSMT8H: { uint32_t& d = gVram[a & 0xfffff]; d = (d & 0x00ffffff) | (v << 24); return; }
    case PSMT4HL: { uint32_t& d = gVram[a & 0xfffff]; d = (d & 0xf0ffffff) | ((v & 15) << 24); return; }
    case PSMT4HH: { uint32_t& d = gVram[a & 0xfffff]; d = (d & 0x0fffffff) | ((v & 15) << 28); return; }
    }
}
inline uint32_t Bits(uint64_t v, int lo, int n) { return (uint32_t)((v >> lo) & ((1ull << n) - 1)); }

// ---- the 512 pages (8 KB) of the local memory as a set: what queued primitives read and write
struct Pages {
    uint64_t w[8] = {};
    void Set(uint32_t p) { p &= 511; w[p >> 6] |= 1ull << (p & 63); }
    bool Meets(const Pages& o) const { for (int k = 0; k < 8; ++k) if (w[k] & o.w[k]) return true; return false; }
    void Add(const Pages& o) { for (int k = 0; k < 8; ++k) w[k] |= o.w[k]; }
    void Clear() { for (uint64_t& x : w) x = 0; }
};
// the pages under a rectangle of a buffer (inclusive corners, 0..2047) in a layout (Family: 0
// 32-bit, 1 / 2 16-bit, 3 8-bit, 4 4-bit); a buffer that does not start a page spills into the next
void MarkRect(Pages& p, uint32_t bp, uint32_t bw, int layout, int x0, int y0, int x1, int y1) {
    int pw = 64, ph = 32; uint32_t bwp = bw;
    switch (layout) {
    case 1: case 2: ph = 64; break;
    case 3: pw = 128; ph = 64; bwp = bw >> 1; break;
    case 4: pw = 128; ph = 128; bwp = bw >> 1; break;
    }
    const uint32_t base = bp >> 5; const bool spill = (bp & 31) != 0;
    for (int py = y0 / ph; py <= y1 / ph; ++py)
        for (int px = x0 / pw; px <= x1 / pw; ++px) {
            const uint32_t pg = base + (uint32_t)py * bwp + (uint32_t)px;
            p.Set(pg); if (spill) p.Set(pg + 1);
        }
}
bool PoolLive();                        // primitives may be queued for the drawing threads
void DrainAll();                        // every queued primitive drawn
void HazardRead(const Pages& p);        // before this thread reads p: drains if queued primitives write it
void HazardWrite(const Pages& p);       // before this thread writes p: drains if queued primitives read or write it
Pages gXferPages;                       // the pages of the host -> local transfer in progress
std::vector<uint8_t> gXferBytes;        // GPU renderer: that transfer's data as sent, its destination
uint32_t gXferDst[3];                   // bp, bw, psm
int gXferRect[4];                       // x, y, w, h
// bits of a pixel in a transfer's data
int BitsOf(uint32_t psm) {
    switch (psm & 0x3f) {
    case 0x00: case 0x30: return 32;
    case 0x01: case 0x31: return 24;
    case 0x02: case 0x0a: case 0x32: case 0x3a: return 16;
    case 0x13: case 0x1b: return 8;
    case 0x14: case 0x24: case 0x2c: return 4;
    default: return 0;
    }
}
uint64_t gClutVersion = 0;              // changes when the CLUT buffer loads

// ---- HD textures (hdtex.h, docs/TEXTURES.md): what each page of the local memory holds, as the
// transfers that wrote it. A transfer covering a whole page gives it the id (its pixels' hash, the
// page's place in the transfer); one covering part of it mixes into the id the page had. A texture's
// key: its pages' ids in order, its offset in its first page, its layout and size, the CLUT entries it
// reads. Where it sits in the memory does not count: the same bank uploaded elsewhere gets the same
// keys. Pages a primitive drew on lose their id (no key: the PS2's texels).
uint64_t gPageId[512];
Pages gIdPages;                         // the pages with an id
uint64_t gIdSerial = 0;                 // changes when an id does (the texture key cache)
bool gHdOn = false;                     // keys made (GsSetHdTextures)
uint32_t gIdLo = 512, gIdHi = 0;        // the range of the pages given an id (since GsSetHdTextures)
inline uint64_t Mix(uint64_t h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); h *= 0xff51afd7ed558ccdull; return h ^ (h >> 33); }
void NoteUpload(uint32_t bp, uint32_t bw, uint32_t psm, int x, int y, int w, int h, const uint8_t* data, size_t bytes) {
    if (w <= 0 || h <= 0) return;
    const size_t need = ((size_t)w * h * BitsOf(psm) + 7) / 8;
    const uint64_t hash = hdtex::Hash64(data, std::min(need, bytes), need);
    bw = bw ? bw : 1;
    int pw = 64, ph = 32; uint32_t bwp = bw;
    switch (Family(psm)) { case 1: case 2: ph = 64; break; case 3: pw = 128; ph = 64; bwp = bw >> 1; break; case 4: pw = 128; ph = 128; bwp = bw >> 1; break; }
    const int x1 = std::min(x + w - 1, 2047), y1 = std::min(y + h - 1, 2047);
    const uint32_t base = bp >> 5; const bool aligned = (bp & 31) == 0;
    // the data's layout: its mode, buffer width, block offset and where it starts in its pages
    const uint64_t layout = Mix(Mix(Mix(psm & 0x3f, bw), bp & 31), (uint64_t)(x % pw) << 32 | (uint32_t)(y % ph));
    for (int py = y / ph; py <= y1 / ph; ++py)
        for (int px = x / pw; px <= x1 / pw; ++px) {
            const uint64_t rel = (uint64_t)(py - y / ph) << 32 | (uint32_t)(px - x / pw);
            const int cx0 = std::max(x, px * pw), cy0 = std::max(y, py * ph), cx1 = std::min(x1, (px + 1) * pw - 1), cy1 = std::min(y1, (py + 1) * ph - 1);
            const bool full = aligned && cx0 == px * pw && cy0 == py * ph && cx1 == (px + 1) * pw - 1 && cy1 == (py + 1) * ph - 1;
            const uint64_t part = (uint64_t)(cx0 - px * pw) << 48 | (uint64_t)(cy0 - py * ph) << 32 | (uint64_t)(cx1 - px * pw) << 16 | (uint32_t)(cy1 - py * ph);
            for (uint32_t k = 0; k < (aligned ? 1u : 2u); ++k) {
                const uint32_t pg = (base + (uint32_t)py * bwp + (uint32_t)px + k) & 511;
                const uint64_t id = Mix(Mix(hash, rel << 1 | k), layout);
                const bool had = (gIdPages.w[pg >> 6] >> (pg & 63)) & 1;
                gPageId[pg] = full ? id : Mix(had ? gPageId[pg] : 0x5a5a5a5aull, Mix(id, part));
                gIdPages.Set(pg);
                gIdLo = std::min(gIdLo, pg); gIdHi = std::max(gIdHi, pg);
            }
        }
    ++gIdSerial;
}
// ---- registers
struct Context {
    uint64_t tex0 = 0, tex1 = 0, clamp = 0, xyoffset = 0, miptbp1 = 0, miptbp2 = 0, scissor = 0,
             alpha = 0, test = 0, fba = 0, frame = 0, zbuf = 0;
};
struct Vertex { int x, y; uint32_t z; uint8_t r, g, b, a, f; float s, t, q; int u, v; };
struct State {
    Context ctx[2];
    uint64_t prim = 0, prmode = 0, prmodecont = 1, texclut = 0, scanmsk = 0, texa = 0, fogcol = 0,
             dimx = 0, dthe = 0, colclamp = 1, pabe = 0, bitbltbuf = 0, trxpos = 0, trxreg = 0, trxdir = 3;
    uint8_t r = 0x80, g = 0x80, b = 0x80, a = 0x80, fog = 0xff;
    float q = 1.0f, s = 0, t = 0;
    int u = 0, v = 0;
    Vertex queue[3]; int nq = 0, tfanFirst = 0;             // vertex kicks
    uint32_t clut[512] = {};                                // the CLUT buffer (32-bit entries, or 512 16-bit ones)
    uint32_t cbp0 = ~0u, cbp1 = ~0u;
    // IMAGE transfer in progress
    int tx = 0, ty = 0; bool xfer = false; uint32_t pendBytes = 0; uint8_t pend[8] = {};
    // privileged
    uint64_t pmode = 0, smode2 = 0, dispfb[2] = {0, 0}, display[2] = {0, 0}, bgcolor = 0, csr = 0, imr = 0xff00;
    uint64_t siglblid = 0;
    int interlace = 1, mode = 2, ffmd = 0;                  // SetGsCrt (NTSC, interlaced, field by field)
} G;
std::atomic<uint64_t> gDisplayFlips{0};                     // fields in which DISPFB was set (GsDisplayFlips; the game thread's side)
bool gDispfbChanged = false;                                // DISPFB written since the last VSync (the lobby sets the same one each picture)

void LoadClut(uint64_t tex0);

void KickVertex(bool drawing);
}  // namespace
void EndTransfer();
uint32_t GsPixelAddress(uint32_t psm, int x, int y, uint32_t bp, uint32_t bw) { return PixelAddress(psm, x, y, bp, bw); }
namespace {

void WriteReg(uint32_t reg, uint64_t v) {
    if (gGsTrace > 0 && reg != 0x01 && reg != 0x02 && reg != 0x03 && reg != 0x04 && reg != 0x05 && reg != 0x0c && reg != 0x0d && reg != 0x54)
        GsTraceLog("gs reg %02x = %016llx", reg, (unsigned long long)v);
    switch (reg) {
    case 0x00: G.prim = v; G.nq = 0; G.tfanFirst = 0; return;                       // PRIM
    case 0x01: G.r = (uint8_t)v; G.g = (uint8_t)(v >> 8); G.b = (uint8_t)(v >> 16); G.a = (uint8_t)(v >> 24);
        { uint32_t q = (uint32_t)(v >> 32); memcpy(&G.q, &q, 4); } return;           // RGBAQ
    case 0x02: { uint32_t s = (uint32_t)v, t = (uint32_t)(v >> 32); memcpy(&G.s, &s, 4); memcpy(&G.t, &t, 4); return; }   // ST
    case 0x03: G.u = Bits(v, 0, 14); G.v = Bits(v, 16, 14); return;                  // UV
    case 0x04: case 0x0c: {                                                           // XYZF2 / XYZF3
        G.fog = (uint8_t)(v >> 56);
        Vertex& q = G.queue[G.nq < 3 ? G.nq : 2]; (void)q;
        Vertex x{}; x.x = Bits(v, 0, 16); x.y = Bits(v, 16, 16); x.z = Bits(v, 32, 24); x.f = G.fog;
        x.r = G.r; x.g = G.g; x.b = G.b; x.a = G.a; x.s = G.s; x.t = G.t; x.q = G.q; x.u = G.u; x.v = G.v;
        if (G.nq < 3) G.queue[G.nq++] = x; else { G.queue[0] = G.queue[1]; G.queue[1] = G.queue[2]; G.queue[2] = x; }
        KickVertex(reg == 0x04);
        return; }
    case 0x05: case 0x0d: {                                                           // XYZ2 / XYZ3
        Vertex x{}; x.x = Bits(v, 0, 16); x.y = Bits(v, 16, 16); x.z = (uint32_t)(v >> 32); x.f = G.fog;
        x.r = G.r; x.g = G.g; x.b = G.b; x.a = G.a; x.s = G.s; x.t = G.t; x.q = G.q; x.u = G.u; x.v = G.v;
        if (G.nq < 3) G.queue[G.nq++] = x; else { G.queue[0] = G.queue[1]; G.queue[1] = G.queue[2]; G.queue[2] = x; }
        KickVertex(reg == 0x05);
        return; }
    case 0x06: case 0x07: G.ctx[reg - 6].tex0 = v; LoadClut(v); return;              // TEX0_1 / 2
    case 0x08: case 0x09: G.ctx[reg - 8].clamp = v; return;
    case 0x0a: G.fog = (uint8_t)(v >> 56); return;
    case 0x14: case 0x15: G.ctx[reg - 0x14].tex1 = v; return;
    case 0x16: case 0x17: {                                                           // TEX2: TEX0's PSM and CLUT fields
        uint64_t& t = G.ctx[reg - 0x16].tex0;
        const uint64_t mask = (0x3full << 20) | (~0ull << 37);                        // PSM, CBP CPSM CSM CSA CLD
        t = (t & ~mask) | (v & mask);
        LoadClut(t);
        return; }
    case 0x18: case 0x19: G.ctx[reg - 0x18].xyoffset = v; return;
    case 0x1a: G.prmodecont = v; return;
    case 0x1b: G.prmode = v; return;
    case 0x1c: G.texclut = v; return;
    case 0x22: G.scanmsk = v; return;
    case 0x34: case 0x35: G.ctx[reg - 0x34].miptbp1 = v; return;
    case 0x36: case 0x37: G.ctx[reg - 0x36].miptbp2 = v; return;
    case 0x3b: G.texa = v; return;
    case 0x3d: G.fogcol = v; return;
    case 0x3f: return;                                                                // TEXFLUSH
    case 0x40: case 0x41: G.ctx[reg - 0x40].scissor = v; return;
    case 0x42: case 0x43: G.ctx[reg - 0x42].alpha = v; return;
    case 0x44: G.dimx = v; return;
    case 0x45: G.dthe = v; return;
    case 0x46: G.colclamp = v; return;
    case 0x47: case 0x48: G.ctx[reg - 0x47].test = v; return;
    case 0x49: G.pabe = v; return;
    case 0x4a: case 0x4b: G.ctx[reg - 0x4a].fba = v; return;
    case 0x4c: case 0x4d: G.ctx[reg - 0x4c].frame = v; return;
    case 0x4e: case 0x4f: G.ctx[reg - 0x4e].zbuf = v; return;
    case 0x50: G.bitbltbuf = v; return;
    case 0x51: G.trxpos = v; return;
    case 0x52: G.trxreg = v; return;
    case 0x53: {                                                                      // TRXDIR: a transfer starts
        G.trxdir = v & 3;
        const uint32_t sbp = Bits(G.bitbltbuf, 0, 14), sbw = Bits(G.bitbltbuf, 16, 6), spsm = Bits(G.bitbltbuf, 24, 6);
        const uint32_t dbp = Bits(G.bitbltbuf, 32, 14), dbw = Bits(G.bitbltbuf, 48, 6), dpsm = Bits(G.bitbltbuf, 56, 6);
        const int ssax = Bits(G.trxpos, 0, 11), ssay = Bits(G.trxpos, 16, 11), dsax = Bits(G.trxpos, 32, 11), dsay = Bits(G.trxpos, 48, 11);
        const int rrw = Bits(G.trxreg, 0, 12), rrh = Bits(G.trxreg, 32, 12);
        if (PoolLive() && G.trxdir != 3) {                                           // queued primitives on these pages
            if (G.trxdir == 1 || ssax + rrw > 2048 || ssay + rrh > 2048 || dsax + rrw > 2048 || dsay + rrh > 2048) DrainAll();
            else if (rrw > 0 && rrh > 0) {
                Pages dst; MarkRect(dst, dbp, dbw, Family(dpsm), dsax, dsay, dsax + rrw - 1, dsay + rrh - 1);
                if (G.trxdir == 2) { Pages src; MarkRect(src, sbp, sbw, Family(spsm), ssax, ssay, ssax + rrw - 1, ssay + rrh - 1); HazardRead(src); }
                HazardWrite(dst);
                if (G.trxdir == 0) gXferPages = dst;
            }
        }
        if ((gsgpu::On() || gGsXferHook || gHdOn) && G.xfer) EndTransfer();         // the one in progress ends here
        if (gsgpu::On() && G.trxdir == 2 && rrw > 0 && rrh > 0) {                   // a local copy reads what the GPU drew there
            Pages src; MarkRect(src, sbp, sbw, Family(spsm), ssax, ssay, std::min(ssax + rrw - 1, 2047), std::min(ssay + rrh - 1, 2047));
            gsgpu::CpuWillRead(src.w);
        }
        if (G.trxdir == 0) {
            G.xfer = true; G.tx = 0; G.ty = 0; G.pendBytes = 0;
            if (gsgpu::On() || gGsXferHook || gHdOn) { gXferBytes.clear(); gXferBytes.reserve(std::min<size_t>((size_t)std::max(0, rrw) * std::max(0, rrh) * BitsOf(dpsm) / 8 + 16, 4u << 20)); gXferDst[0] = dbp; gXferDst[1] = dbw; gXferDst[2] = dpsm; gXferRect[0] = dsax; gXferRect[1] = dsay; gXferRect[2] = rrw; gXferRect[3] = rrh; }
        }
        else if (G.trxdir == 2) {                                                     // local -> local
            std::vector<uint32_t> tmp((size_t)rrw * rrh);
            for (int y = 0; y < rrh; ++y) for (int x = 0; x < rrw; ++x) tmp[(size_t)y * rrw + x] = ReadPixel(spsm, ssax + x, ssay + y, sbp, sbw);
            for (int y = 0; y < rrh; ++y) for (int x = 0; x < rrw; ++x) WritePixel(dpsm, dsax + x, dsay + y, dbp, dbw, tmp[(size_t)y * rrw + x]);
            if ((gsgpu::On() || gGsXferHook || gHdOn) && rrw > 0 && rrh > 0) {       // the GPU writes the same pixels
                const int bpp = BitsOf(dpsm);
                std::vector<uint8_t> px(((size_t)rrw * rrh * bpp + 7) / 8 + 4, 0);
                for (size_t i = 0; bpp && i < tmp.size(); ++i) {
                    const size_t bit = i * bpp;
                    const uint64_t v = (uint64_t)(tmp[i] & (bpp == 32 ? 0xffffffffu : (1u << bpp) - 1)) << (bit & 7);
                    for (int k = 0; k < 5 && (bit >> 3) + k < px.size(); ++k) px[(bit >> 3) + k] |= (uint8_t)(v >> (8 * k));
                }
                if (gGsXferHook) gGsXferHook(GsXferInfo{dbp, dbw, dpsm, dsax, dsay, rrw, rrh, px.data(), ((size_t)rrw * rrh * bpp + 7) / 8, true});
                if (gHdOn) NoteUpload(dbp, dbw, dpsm, dsax, dsay, rrw, rrh, px.data(), ((size_t)rrw * rrh * bpp + 7) / 8);
                if (gsgpu::On()) gsgpu::Transfer(dbp, dbw, dpsm, dsax, dsay, rrw, rrh, std::move(px));
            }
            G.trxdir = 3;
        }
        return; }
    case 0x54: {                                                                      // HWREG: two pixels' worth of IMAGE data
        uint8_t b[8]; memcpy(b, &v, 8);
        ImageData(b, 8);
        return; }
    case 0x60: {                                                                      // SIGNAL
        ++gGsEvents[gGifPathNow & 3][0];
        const uint32_t id = (uint32_t)v, mask = (uint32_t)(v >> 32);
        std::lock_guard<std::mutex> l(gEvMu);
        G.siglblid = (G.siglblid & ~(uint64_t)mask) | (id & mask);
        G.csr |= 1; if (!(G.imr & 0x100)) RaiseGsIrq(); return; }                     // SIGMSK
    case 0x61: {                                                                      // FINISH (FINISHMSK): one event each
        ++gGsEvents[gGifPathNow & 3][1];
        std::lock_guard<std::mutex> l(gEvMu);
        ++gFinishEvents;                                                              // (the handler clears CSR FINISH per event)
        if (gGsClock) gFinishAt.store(gGsClock(), std::memory_order_relaxed);
        G.csr |= 2; if (!(G.imr & 0x200)) RaiseGsIrq(); return; }
    case 0x62: {                                                                      // LABEL
        ++gGsEvents[gGifPathNow & 3][2];
        const uint32_t id = (uint32_t)v, mask = (uint32_t)(v >> 32);
        std::lock_guard<std::mutex> l(gEvMu);
        G.siglblid = (G.siglblid & ~((uint64_t)mask << 32)) | ((uint64_t)(id & mask) << 32);
        return; }
    }
}

// ---- IMAGE data (host -> local): pixels in raster order over the TRXREG rectangle
}  // namespace
// a host -> local transfer done (or cut short by the next one): the GPU writes its pixels too
void EndTransfer() {
    G.xfer = false;
    if (gGsXferHook) gGsXferHook(GsXferInfo{gXferDst[0], gXferDst[1], gXferDst[2], gXferRect[0], gXferRect[1], gXferRect[2], gXferRect[3],
                                            gXferBytes.data(), gXferBytes.size(), false});
    if (gHdOn) NoteUpload(gXferDst[0], gXferDst[1], gXferDst[2], gXferRect[0], gXferRect[1], gXferRect[2], gXferRect[3], gXferBytes.data(), gXferBytes.size());
    if (!gsgpu::On()) { gXferBytes.clear(); return; }
    gsgpu::Transfer(gXferDst[0], gXferDst[1], gXferDst[2], gXferRect[0], gXferRect[1], gXferRect[2], gXferRect[3], std::move(gXferBytes));
    gXferBytes.clear();
}
void ImageData(const uint8_t* p, size_t n) {
    if (!G.xfer) return;
    const uint32_t dbp = Bits(G.bitbltbuf, 32, 14), dbw = Bits(G.bitbltbuf, 48, 6), dpsm = Bits(G.bitbltbuf, 56, 6);
    const int dsax = Bits(G.trxpos, 32, 11), dsay = Bits(G.trxpos, 48, 11);
    const int rrw = Bits(G.trxreg, 0, 12), rrh = Bits(G.trxreg, 32, 12);
    if (gsgpu::On() || gGsXferHook || gHdOn) {                      // the pixels as sent (the GPU's copy, the HD keys):
        size_t keep = n;                                            // a run of quadwords up to the one the transfer ends in
        if (n > 16) {
            if (rrw <= 0 || rrh <= 0 || G.ty >= rrh) keep = 16;
            else {
                const size_t px = (size_t)(rrh - G.ty) * (size_t)rrw - (size_t)G.tx, bits = (size_t)BitsOf(dpsm);
                const size_t left = bits ? (px * bits + 7) / 8 - (bits == 24 ? G.pendBytes : 0) : n;
                keep = std::min(n, (left + 15) & ~(size_t)15);
            }
        }
        gXferBytes.insert(gXferBytes.end(), p, p + keep);
    }
    if (rrw <= 0 || rrh <= 0) { G.xfer = false; return; }
    auto put = [&](uint32_t pix) {
        if (G.ty >= rrh) return;
        WritePixel(dpsm, dsax + G.tx, dsay + G.ty, dbp, dbw, pix);
        if (++G.tx >= rrw) { G.tx = 0; if (++G.ty >= rrh) EndTransfer(); }
    };
    const uint32_t psm = dpsm & 0x3f;
    size_t i = 0;
    // the usual modes written without the per-pixel switches: 32-bit, 16-bit, 8-bit and 4-bit stores
    auto next = [&]() -> bool { if (++G.tx >= rrw) { G.tx = 0; if (++G.ty >= rrh) { EndTransfer(); return false; } } return true; };
    if (psm == PSMCT32 || psm == PSMZ32) {                         // the textures' uploads (1.8 MB a picture): a row's run
        const uint32_t zx = psm == PSMZ32 ? 0x18 : 0;               // at a time, 8 pixels of a block row as 4 pairs
        int tx = G.tx, ty = G.ty;
        size_t left = n / 4;
        const uint8_t* s = p;
        bool ended = false;
        while (left && ty < rrh) {
            const uint32_t y = (uint32_t)(dsay + ty) & 2047, rowBase = dbp + (y >> 5) * dbw * 32;
            const uint8_t* blk = kBlock32[(y >> 3) & 3];
            const uint8_t* col = kColumn32[y & 7];
            const size_t run = std::min<size_t>(left, (size_t)(rrw - tx));
            for (size_t j = 0; j < run;) {
                const uint32_t x = (uint32_t)(dsax + tx + (int)j) & 2047;
                const uint32_t bn = rowBase + (x >> 6) * 32 + (blk[(x >> 3) & 7] ^ zx);
                uint32_t* d = gVram + ((bn & 0x3fff) << 6);
                if (!(x & 7) && j + 8 <= run) {
                    memcpy(d + col[0], s, 8); memcpy(d + col[2], s + 8, 8); memcpy(d + col[4], s + 16, 8); memcpy(d + col[6], s + 24, 8);
                    s += 32; j += 8;
                } else {
                    memcpy(d + col[x & 7], s, 4);
                    s += 4; ++j;
                }
            }
            left -= run; tx += (int)run;
            if (tx >= rrw) { tx = 0; if (++ty >= rrh) ended = true; }
        }
        G.tx = tx; G.ty = ty;
        if (ended) EndTransfer();
        return;
    }
    if (psm == PSMCT16 || psm == PSMCT16S) {
        const bool s = psm == PSMCT16S;
        while (i + 2 <= n && G.xfer && G.ty < rrh) {
            uint16_t v; memcpy(&v, p + i, 2); i += 2;
            Vram16()[Addr16((uint32_t)(dsax + G.tx) & 2047, (uint32_t)(dsay + G.ty) & 2047, dbp, dbw, 0, s) & 0x1fffff] = v;
            if (!next()) break;
        }
        return;
    }
    if (psm == PSMT8) {
        while (i < n && G.xfer && G.ty < rrh) {
            Vram8()[Addr8((uint32_t)(dsax + G.tx) & 2047, (uint32_t)(dsay + G.ty) & 2047, dbp, dbw) & 0x3fffff] = p[i++];
            if (!next()) break;
        }
        return;
    }
    if (psm == PSMT4) {
        while (i < n && G.xfer && G.ty < rrh) {
            const uint8_t b = p[i++];
            for (int h = 0; h < 2 && G.xfer && G.ty < rrh; ++h) {
                const uint32_t a = Addr4((uint32_t)(dsax + G.tx) & 2047, (uint32_t)(dsay + G.ty) & 2047, dbp, dbw);
                const uint32_t v = h ? b >> 4 : b & 15;
                uint8_t& d = Vram8()[(a >> 1) & 0x3fffff];
                d = (a & 1) ? (uint8_t)((d & 0x0f) | (v << 4)) : (uint8_t)((d & 0xf0) | v);
                if (!next()) break;
            }
        }
        return;
    }
    // the other modes, and leftover bytes of a pixel split across calls (24-bit)
    while (i < n && G.xfer) {
        switch (psm) {
        case PSMCT32: case PSMZ32:
            if (i + 4 > n) { i = n; break; }
            { uint32_t v; memcpy(&v, p + i, 4); put(v); i += 4; } break;
        case PSMCT24: case PSMZ24:
            G.pend[G.pendBytes++] = p[i++];
            if (G.pendBytes == 3) { put(G.pend[0] | G.pend[1] << 8 | G.pend[2] << 16); G.pendBytes = 0; }
            break;
        case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S:
            if (i + 2 > n) { i = n; break; }
            { uint16_t v; memcpy(&v, p + i, 2); put(v); i += 2; } break;
        case PSMT8: case PSMT8H: put(p[i++]); break;
        case PSMT4: case PSMT4HL: case PSMT4HH: put(p[i] & 15); if (G.xfer) put(p[i] >> 4); ++i; break;
        default: i = n; break;
        }
    }
}
namespace {

// ---- the CLUT buffer (TEX0 / TEX2 CLD)
void LoadClut(uint64_t tex0) {
    const uint32_t cld = Bits(tex0, 61, 3), cbp = Bits(tex0, 37, 14), cpsm = Bits(tex0, 51, 4), csm = Bits(tex0, 55, 1), csa = Bits(tex0, 56, 5);
    const uint32_t psm = Bits(tex0, 20, 6);
    bool load = false;
    switch (cld) {
    case 1: load = true; break;
    case 2: load = true; G.cbp0 = cbp; break;
    case 3: load = true; G.cbp1 = cbp; break;
    case 4: load = cbp != G.cbp0; G.cbp0 = cbp; break;
    case 5: load = cbp != G.cbp1; G.cbp1 = cbp; break;
    default: return;
    }
    if (!load) return;
    const bool eight = (psm & 0x3f) == PSMT8 || (psm & 0x3f) == PSMT8H;
    const int entries = eight ? 256 : 16;
    const bool c16 = cpsm == 2 || cpsm == 0xa;
    const uint32_t cp = c16 ? (cpsm == 0xa ? PSMCT16S : PSMCT16) : PSMCT32;
    ++gClutVersion;
    if (gsgpu::On()) {                                                          // the GPU loads its CLUT from its own copy
        gsgpu::ClutLoad c;
        c.cbp = cbp; c.cpsm = cp & 0x3f; c.csm = csm; c.entries = (uint32_t)entries;
        c.bw = csm == 0 ? 1 : (Bits(G.texclut, 0, 6) ? Bits(G.texclut, 0, 6) : 1);
        c.x0 = csm == 0 ? 0 : (int)Bits(G.texclut, 6, 6) * 16; c.y0 = csm == 0 ? 0 : (int)Bits(G.texclut, 12, 10);
        c.slot = eight ? 0 : (int)((csa & (c16 ? 0x1f : 0x0f)) * 16);
        gsgpu::LoadClut(c);
    }
    if (PoolLive()) {                                                           // the entries' pages, drawn first
        Pages pg;
        if (csm == 0) MarkRect(pg, cbp, 1, Family(cp), 0, 0, eight ? 15 : 7, eight ? 15 : 1);
        else {
            const int x0 = (int)Bits(G.texclut, 6, 6) * 16, y0 = (int)Bits(G.texclut, 12, 10), bw = (int)Bits(G.texclut, 0, 6);
            if (x0 + entries > 2048 || y0 > 2047) DrainAll();
            else MarkRect(pg, cbp, bw ? bw : 1, Family(cp), x0, y0, x0 + entries - 1, y0);
        }
        HazardRead(pg);
    }
    // CSM1: 16 x 16 (8-bit) or 8 x 2 (4-bit) in the buffer, with the 8-bit swizzle of its
    // columns; CSM2: a line at TEXCLUT's offset
    if (csm == 0) {                                     // CSM1 (every load of a match): the entries' places worked out once
        struct Places { uint8_t blk[256]; uint16_t col[256]; };
        static const Places* kPlaces = [] {             // [eight][layout: 0 CT32, 1 CT16, 2 CT16S]
            static Places p[2][3];
            for (int e = 0; e < 2; ++e)
                for (int l = 0; l < 3; ++l)
                    for (int i = 0; i < (e ? 256 : 16); ++i) {
                        int x, y;
                        if (e) { const int j = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); x = j & 15; y = j >> 4; }
                        else { x = i & 7; y = i >> 3; }
                        const uint32_t a = l == 0 ? Addr32((uint32_t)x, (uint32_t)y, 0, 1, 0) : Addr16((uint32_t)x, (uint32_t)y, 0, 1, 0, l == 2);
                        p[e][l].blk[i] = (uint8_t)(a >> (l == 0 ? 6 : 7)); p[e][l].col[i] = (uint16_t)(a & (l == 0 ? 63 : 127));
                    }
            return &p[0][0];
        }();
        const int layout = !c16 ? 0 : cp == PSMCT16S ? 2 : 1;
        const Places& pl = kPlaces[(eight ? 3 : 0) + layout];
        const int base = eight ? 0 : (int)((csa & (c16 ? 0x1f : 0x0f)) * 16);
        static const bool check = getenv("MGO1_FASTPATH_CHECK") != nullptr;   // tests: against ReadPixel
        for (int i = 0; i < entries; ++i) {
            const uint32_t bn = (cbp + pl.blk[i]) & 0x3fff;
            const uint32_t v = layout == 0 ? gVram[((bn << 6) + pl.col[i]) & 0xfffff] : Vram16()[((bn << 7) + pl.col[i]) & 0x1fffff];
            if (base + i < 512) G.clut[base + i] = v;
            if (check) {
                int x, y;
                if (eight) { const int j = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); x = j & 15; y = j >> 4; } else { x = i & 7; y = i >> 3; }
                static long long checked = 0, bad = 0;
                ++checked;
                if (ReadPixel(cp, x, y, cbp, 1) != v && ++bad <= 20 && gLog) gLog("gs: clut fast form differs (cbp %x cpsm %x entry %d)", cbp, cpsm, i);
                if (!(checked & (checked - 1)) && gLog) gLog("gs: clut fast form checked %lld entries, %lld differ", checked, bad);
            }
        }
        return;
    }
    for (int i = 0; i < entries; ++i) {
        int x, y;
        if (csm == 0) {
            if (eight) { const int j = (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); x = j & 15; y = j >> 4; }
            else { x = i & 7; y = i >> 3; }
        } else {
            x = Bits(G.texclut, 6, 6) * 16 + i; y = Bits(G.texclut, 12, 10);
        }
        const uint32_t cbw = csm == 0 ? (c16 ? 1 : 1) : Bits(G.texclut, 0, 6);
        uint32_t v = ReadPixel(cp, x, y, cbp, cbw ? cbw : 1);
        const int slot = eight ? i : (int)((csa & (c16 ? 0x1f : 0x0f)) * 16) + i;
        if (slot < 512) G.clut[slot] = v;
    }
}
// a 16-bit colour to 32 bits (TEXA alpha for texels)
inline uint32_t Expand16(uint32_t c, bool texa) {
    const uint32_t r = (c & 0x1f) << 3, g = ((c >> 5) & 0x1f) << 3, b = ((c >> 10) & 0x1f) << 3;
    uint32_t a;
    if (texa) {
        const bool aem = Bits(G.texa, 15, 1) != 0;
        a = (c & 0x8000) ? Bits(G.texa, 32, 8) : ((aem && !(c & 0x7fff)) ? 0 : Bits(G.texa, 0, 8));
    } else a = (c & 0x8000) ? 0x80 : 0;
    return r | g << 8 | b << 16 | a << 24;
}
// ---- rasterization
struct Attr { bool iip, tme, fge, abe, aa1, fst; int ctxt; bool fix; };
Attr Attrs() {                                                         // (a vertex kick each: the last one's decoded kept)
    const uint64_t p = Bits(G.prmodecont, 0, 1) ? G.prim : G.prmode;
    static uint64_t lastP = ~0ull;
    static Attr last{};
    if (p == lastP) return last;
    lastP = p;
    return last = Attr{Bits(p, 3, 1) != 0, Bits(p, 4, 1) != 0, Bits(p, 5, 1) != 0, Bits(p, 6, 1) != 0,
                       Bits(p, 7, 1) != 0, Bits(p, 8, 1) != 0, (int)Bits(p, 9, 1), Bits(p, 10, 1) != 0};
}
struct Pix { float r, g, b, a; double z; float s, t, q, u, v, f; };   // z: Z32 needs more than 24 bits
// one primitive's pixel state: the registers decoded once (Shade runs per pixel)
struct PS {
    Attr at;
    uint32_t fbp, fbw, fpsm, fbmsk; bool f16, f24;
    int sx0, sx1, sy0, sy1; uint32_t scan;
    bool tme, fst, tcc, bil; int tfx; uint32_t tbp, tbw, tpsm; int tw, th; float ftw, fth;
    uint32_t wms, wmt; int minu, maxu, minv, maxv; bool c16; uint32_t csa, csaMask;
    bool texaAem; uint32_t ta0, ta1;
    uint32_t (*texel)(const struct PS&, int x, int y);                   // the storage mode's texel (wrapped coordinates)
    const uint32_t* clut;                                                  // the CLUT as it was when the primitive came
    float ox, oy;                                                          // XYOFFSET
    int32_t ox16, oy16;                                                    // XYOFFSET in 1/16 pixel (12.4)
    bool fge; int fr, fg, fb;
    bool ate; int atst, aref, afail; bool date, datm;
    bool zte; int ztst; uint32_t zbp, zpsm, zmax; bool zmsk;
    bool blend; int ca, cb, cc, cd, fix; bool pabe, fba, clamp;
};
inline uint32_t Expand16T(const PS& s, uint32_t c);
// the texels of each storage mode (coordinates already wrapped), CLUT and TEXA applied
template <int M> uint32_t TexelOf(const PS& s, int x, int y) {
    const uint32_t ux = (uint32_t)x & 2047, uy = (uint32_t)y & 2047;
    if (M == 0) return gVram[Addr32(ux, uy, s.tbp, s.tbw, 0) & 0xfffff];                                          // CT32
    if (M == 1) { const uint32_t v = gVram[Addr32(ux, uy, s.tbp, s.tbw, 0) & 0xfffff] & 0xffffff;                  // CT24
                  return v | ((s.texaAem && !v) ? 0 : s.ta0) << 24; }
    if (M == 2) return Expand16T(s, Vram16()[Addr16(ux, uy, s.tbp, s.tbw, 0, false) & 0x1fffff]);                  // CT16
    if (M == 3) return Expand16T(s, Vram16()[Addr16(ux, uy, s.tbp, s.tbw, 0, true) & 0x1fffff]);                   // CT16S
    uint32_t e;
    if (M == 4) e = s.clut[Vram8()[Addr8(ux, uy, s.tbp, s.tbw) & 0x3fffff]];                                       // T8
    else if (M == 5) e = s.clut[gVram[Addr32(ux, uy, s.tbp, s.tbw, 0) & 0xfffff] >> 24];                           // T8H
    else {
        uint32_t i;
        if (M == 6) { const uint32_t a = Addr4(ux, uy, s.tbp, s.tbw); i = (Vram8()[(a >> 1) & 0x3fffff] >> ((a & 1) * 4)) & 15; }   // T4
        else if (M == 7) i = (gVram[Addr32(ux, uy, s.tbp, s.tbw, 0) & 0xfffff] >> 24) & 15;                           // T4HL
        else i = gVram[Addr32(ux, uy, s.tbp, s.tbw, 0) & 0xfffff] >> 28;                                              // T4HH
        e = s.clut[((s.csa & s.csaMask) * 16 + i) & 511];
    }
    return s.c16 ? Expand16T(s, e) : e;
}
const uint32_t* ClutSnapshot();
PS MakePS(const Attr& at, const Context& c) {
    PS s{};
    s.at = at;
    s.ox = Bits(c.xyoffset, 0, 16) / 16.0f; s.oy = Bits(c.xyoffset, 32, 16) / 16.0f;
    s.ox16 = (int32_t)Bits(c.xyoffset, 0, 16); s.oy16 = (int32_t)Bits(c.xyoffset, 32, 16);
    s.fbp = Bits(c.frame, 0, 9) * 32; s.fbw = Bits(c.frame, 16, 6); s.fpsm = Bits(c.frame, 24, 6); s.fbmsk = (uint32_t)(c.frame >> 32);
    s.f16 = Family(s.fpsm) == 1 || Family(s.fpsm) == 2; s.f24 = (s.fpsm & 0x3f) == PSMCT24;
    s.sx0 = Bits(c.scissor, 0, 11); s.sx1 = Bits(c.scissor, 16, 11); s.sy0 = Bits(c.scissor, 32, 11); s.sy1 = Bits(c.scissor, 48, 11);
    s.scan = Bits(G.scanmsk, 0, 2);
    s.tme = at.tme; s.fst = at.fst;
    const uint64_t t0 = c.tex0;
    s.tbp = Bits(t0, 0, 14); s.tbw = Bits(t0, 14, 6); s.tpsm = Bits(t0, 20, 6);
    s.tw = 1 << std::min(Bits(t0, 26, 4), 10u); s.th = 1 << std::min(Bits(t0, 30, 4), 10u);
    s.ftw = (float)(1 << std::min(Bits(t0, 26, 4), 10u)); s.fth = (float)(1 << std::min(Bits(t0, 30, 4), 10u));
    s.tcc = Bits(t0, 34, 1) != 0; s.tfx = (int)Bits(t0, 35, 2);
    const uint32_t cpsm = Bits(t0, 51, 4);
    s.c16 = cpsm == 2 || cpsm == 0xa; s.csa = Bits(t0, 56, 5); s.csaMask = s.c16 ? 0x1f : 0x0f;
    s.bil = Bits(c.tex1, 5, 1) != 0;                                                    // MMAG (magnification only here)
    s.wms = Bits(c.clamp, 0, 2); s.wmt = Bits(c.clamp, 2, 2);
    s.minu = Bits(c.clamp, 4, 10); s.maxu = Bits(c.clamp, 14, 10); s.minv = Bits(c.clamp, 24, 10); s.maxv = Bits(c.clamp, 34, 10);
    s.texaAem = Bits(G.texa, 15, 1) != 0; s.ta0 = Bits(G.texa, 0, 8); s.ta1 = Bits(G.texa, 32, 8);
    s.clut = at.tme && ((s.tpsm & 0x3f) == PSMT8 || (s.tpsm & 0x3f) == PSMT8H || (s.tpsm & 0x3f) == PSMT4 ||
                        (s.tpsm & 0x3f) == PSMT4HL || (s.tpsm & 0x3f) == PSMT4HH) ? ClutSnapshot() : G.clut;
    switch (s.tpsm & 0x3f) {
    case PSMCT24: s.texel = &TexelOf<1>; break;
    case PSMCT16: s.texel = &TexelOf<2>; break;
    case PSMCT16S: s.texel = &TexelOf<3>; break;
    case PSMT8: s.texel = &TexelOf<4>; break;
    case PSMT8H: s.texel = &TexelOf<5>; break;
    case PSMT4: s.texel = &TexelOf<6>; break;
    case PSMT4HL: s.texel = &TexelOf<7>; break;
    case PSMT4HH: s.texel = &TexelOf<8>; break;
    default: s.texel = &TexelOf<0>; break;                                        // CT32 (and the Z modes read as it)
    }
    s.fge = at.fge; s.fr = Bits(G.fogcol, 0, 8); s.fg = Bits(G.fogcol, 8, 8); s.fb = Bits(G.fogcol, 16, 8);
    const uint64_t test = c.test;
    s.ate = Bits(test, 0, 1) != 0; s.atst = Bits(test, 1, 3); s.aref = Bits(test, 4, 8); s.afail = Bits(test, 12, 2);
    s.date = Bits(test, 14, 1) != 0; s.datm = Bits(test, 15, 1) != 0;
    if (s.date) {                                                           // the graphics card renderer has no DATE yet: seen?
        static std::atomic<bool> seen{false};
        if (!seen.exchange(true) && gLog) gLog("gs: a draw with the destination alpha test (DATM %d, frame %05x)", (int)s.datm, s.fbp);
    }
    s.zte = Bits(test, 16, 1) != 0; s.ztst = Bits(test, 17, 2);
    s.zbp = Bits(c.zbuf, 0, 9) * 32; s.zpsm = Bits(c.zbuf, 24, 4) | 0x30; s.zmsk = Bits(c.zbuf, 32, 1) != 0;
    s.zmax = (s.zpsm & 0xf) == 1 ? 0xffffffu : (s.zpsm & 0xf) >= 2 ? 0xffffu : 0xffffffffu;
    s.blend = at.abe; s.pabe = Bits(G.pabe, 0, 1) != 0;
    s.ca = Bits(c.alpha, 0, 2); s.cb = Bits(c.alpha, 2, 2); s.cc = Bits(c.alpha, 4, 2); s.cd = Bits(c.alpha, 6, 2); s.fix = Bits(c.alpha, 32, 8);
    s.fba = Bits(c.fba, 0, 1) != 0; s.clamp = Bits(G.colclamp, 0, 1) != 0;
    return s;
}
inline uint32_t Expand16T(const PS& s, uint32_t c) {                        // a 16-bit texel through TEXA
    const uint32_t r = (c & 0x1f) << 3, g = ((c >> 5) & 0x1f) << 3, b = ((c >> 10) & 0x1f) << 3;
    const uint32_t a = (c & 0x8000) ? s.ta1 : ((s.texaAem && !(c & 0x7fff)) ? 0 : s.ta0);
    return r | g << 8 | b << 16 | a << 24;
}
inline int Wrap(int v, uint32_t m, int size, int mn, int mx) {
    switch (m) {
    case 0: return v & (size - 1);
    case 1: return v < 0 ? 0 : v > size - 1 ? size - 1 : v;
    case 2: return v < mn ? mn : v > mx ? mx : v;
    default: return (v & mn) | mx;
    }
}
// one texel at integer coordinates (wrapping applied), as RGBA
inline uint32_t Texel(const PS& s, int x, int y) {
    x = s.wms ? Wrap(x, s.wms, s.tw, s.minu, s.maxu) : (x & (s.tw - 1));
    y = s.wmt ? Wrap(y, s.wmt, s.th, s.minv, s.maxv) : (y & (s.th - 1));
    return s.texel(s, x, y);
}
inline uint32_t Sample(const PS& s, float u, float v) {                     // u, v in texels
    if (!s.bil) return Texel(s, (int)floorf(u), (int)floorf(v));
    u -= 0.5f; v -= 0.5f;
    const int x0 = (int)floorf(u), y0 = (int)floorf(v);
    const float fx = u - x0, fy = v - y0;
    const uint32_t c00 = Texel(s, x0, y0), c10 = Texel(s, x0 + 1, y0), c01 = Texel(s, x0, y0 + 1), c11 = Texel(s, x0 + 1, y0 + 1);
    uint32_t out = 0;
    for (int k = 0; k < 32; k += 8) {
        const float a = ((c00 >> k) & 255) * (1 - fx) + ((c10 >> k) & 255) * fx;
        const float b = ((c01 >> k) & 255) * (1 - fx) + ((c11 >> k) & 255) * fx;
        const int o = (int)(a * (1 - fy) + b * fy + 0.5f);
        out |= (uint32_t)(o < 0 ? 0 : o > 255 ? 255 : o) << k;
    }
    return out;
}
// one pixel through the pixel pipeline
void Shade(const PS& s, int x, int y, const Pix& p) {
    if (x < s.sx0 || x > s.sx1 || y < s.sy0 || y > s.sy1) return;         // scissor
    if ((s.scan == 2 && !(y & 1)) || (s.scan == 3 && (y & 1))) return;
    int r = (int)p.r, g = (int)p.g, b = (int)p.b, a = (int)p.a;
    if (s.tme) {
        float u, v;
        if (s.fst) { u = p.u; v = p.v; }
        else { const float q = p.q != 0 ? p.q : 1e-10f; u = p.s / q * s.ftw; v = p.t / q * s.fth; }
        const uint32_t t = Sample(s, u, v);
        const int tr = t & 255, tg = (t >> 8) & 255, tb = (t >> 16) & 255, ta = t >> 24;
        switch (s.tfx) {
        case 0: r = tr * r >> 7; g = tg * g >> 7; b = tb * b >> 7; a = s.tcc ? ta * a >> 7 : a; break;   // MODULATE
        case 1: r = tr; g = tg; b = tb; a = s.tcc ? ta : a; break;                                        // DECAL
        case 2: r = (tr * r >> 7) + a; g = (tg * g >> 7) + a; b = (tb * b >> 7) + a; a = s.tcc ? a + ta : a; break;   // HIGHLIGHT
        default: r = (tr * r >> 7) + a; g = (tg * g >> 7) + a; b = (tb * b >> 7) + a; a = s.tcc ? ta : a; break;       // HIGHLIGHT2
        }
        r = std::min(r, 255); g = std::min(g, 255); b = std::min(b, 255); a = std::min(a, 255);
    }
    if (s.fge) {                                                            // fog: f 255 = no fog
        const int f = (int)p.f;
        r = (f * r + (255 - f) * s.fr) >> 8; g = (f * g + (255 - f) * s.fg) >> 8; b = (f * b + (255 - f) * s.fb) >> 8;
    }
    bool writeRgb = true, writeA = true, writeZ = true;
    if (s.ate) {                                                            // alpha test
        bool pass;
        switch (s.atst) {
        case 0: pass = false; break; case 1: pass = true; break;
        case 2: pass = a < s.aref; break; case 3: pass = a <= s.aref; break;
        case 4: pass = a == s.aref; break; case 5: pass = a >= s.aref; break;
        case 6: pass = a > s.aref; break; default: pass = a != s.aref; break;
        }
        if (!pass) {
            switch (s.afail) {
            case 0: return;                                                // KEEP
            case 1: writeZ = false; break;                                 // FB_ONLY
            case 2: writeRgb = writeA = false; break;                      // ZB_ONLY
            default: writeA = false; writeZ = false; break;                // RGB_ONLY
            }
        }
    }
    const uint32_t d = ReadPixel(s.fpsm, x, y, s.fbp, s.fbw);
    const uint32_t dc = s.f16 ? Expand16(d, false) : (s.f24 ? (d | 0x80000000u) : d);
    if (s.date) {                                                           // destination alpha test
        const bool da = s.f16 ? (d & 0x8000) != 0 : (dc & 0x80000000u) != 0;
        if (s.datm ? !da : da) return;
    }
    uint32_t z = p.z <= 0.0 ? 0u : p.z >= 4294967295.0 ? 0xffffffffu : (uint32_t)p.z;   // depth
    if (z > s.zmax) z = s.zmax;
    if (s.zte && s.ztst != 1) {                                             // NEVER / GEQUAL / GREATER (ALWAYS reads nothing)
        if (s.ztst == 0) return;
        const uint32_t zd = ReadPixel(s.zpsm, x, y, s.zbp, s.fbw);
        if (s.ztst == 2 ? !(z >= zd) : !(z > zd)) return;
    }
    if (s.blend && (!s.pabe || (a & 0x80))) {                               // blend
        const int sel[3][3] = {{r, (int)(dc & 255), 0}, {g, (int)((dc >> 8) & 255), 0}, {b, (int)((dc >> 16) & 255), 0}};
        const int alpha = s.cc == 0 ? a : s.cc == 1 ? (int)(dc >> 24) : s.fix;
        int out[3];
        for (int k = 0; k < 3; ++k) {
            const int A = s.ca < 3 ? sel[k][s.ca] : 0, B = s.cb < 3 ? sel[k][s.cb] : 0, D = s.cd < 3 ? sel[k][s.cd] : 0;
            out[k] = (((A - B) * alpha) >> 7) + D;
        }
        r = out[0]; g = out[1]; b = out[2];
    }
    if (s.clamp) { r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255); }
    else { r &= 255; g &= 255; b &= 255; }
    if (s.fba) a |= 0x80;
    if (writeZ && !s.zmsk && s.zte) WritePixel(s.zpsm, x, y, s.zbp, s.fbw, z);
    uint32_t keep = s.fbmsk;
    if (!writeRgb) keep |= 0x00ffffff;
    if (!writeA) keep |= 0xff000000;
    if (s.f16) {
        const uint32_t c16 = (uint32_t)(r >> 3) | (uint32_t)(g >> 3) << 5 | (uint32_t)(b >> 3) << 10 | (a & 0x80 ? 0x8000u : 0);
        const uint32_t k16 = ((keep >> 3) & 0x1f) | (((keep >> 11) & 0x1f) << 5) | (((keep >> 19) & 0x1f) << 10) | ((keep >> 31) << 15);
        WritePixel(s.fpsm, x, y, s.fbp, s.fbw, c16, k16);
    } else {
        WritePixel(s.fpsm, x, y, s.fbp, s.fbw, (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | (uint32_t)a << 24, keep);
    }
}
Pix Lerp(const Vertex& v, bool fst) {
    Pix p{};
    p.r = v.r; p.g = v.g; p.b = v.b; p.a = v.a; p.z = (double)v.z; p.f = v.f;
    p.s = v.s; p.t = v.t; p.q = v.q; p.u = v.u / 16.0f; p.v = v.v / 16.0f; (void)fst;
    return p;
}
// the rows a worker draws: bands of 8 lines dealt round the workers (band, bands); (0, 1) all
struct Rows { int band, bands; bool Has(int y) const { return bands <= 1 || ((y >> 3) % bands) == band; } };
template <class F> inline void ForRows(const Rows& r, int y0, int y1, F&& f) {   // the rows of y0..y1 in r
    if (r.bands <= 1) { for (int y = y0; y <= y1; ++y) f(y); return; }
    if (y0 > y1) return;
    const int b0 = y0 >> 3;
    for (int b = b0 + ((r.band - b0) % r.bands + r.bands) % r.bands; (b << 3) <= y1; b += r.bands)
        for (int y = std::max(y0, b << 3), e = std::min(y1, (b << 3) + 7); y <= e; ++y) f(y);
}
// a triangle as the GS covers it: samples at whole window coordinates, edge functions exact in
// 12.4 fixed point, a sample on an edge drawn only by a top or left edge (two triangles sharing an
// edge draw it once); attributes from their planes, so a value equal at the three vertices stays
// exactly that value (a flat depth passes an equal-depth test everywhere, a flat colour stays flat)
void Triangle(const PS& ps, const Rows& rows, const Vertex& a, const Vertex& b, const Vertex& d) {
    const Attr& at = ps.at;
    const Vertex* v0 = &a; const Vertex* v1 = &b; const Vertex* v2 = &d;
    int64_t x0 = (int64_t)a.x - ps.ox16, y0 = (int64_t)a.y - ps.oy16;
    int64_t x1 = (int64_t)b.x - ps.ox16, y1 = (int64_t)b.y - ps.oy16;
    int64_t x2 = (int64_t)d.x - ps.ox16, y2 = (int64_t)d.y - ps.oy16;
    int64_t area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area == 0) return;
    if (area < 0) { std::swap(x1, x2); std::swap(y1, y2); std::swap(v1, v2); area = -area; }
    // the samples inside the bounding box and the scissor
    auto floor16 = [](int64_t v) { return (int)(v >= 0 ? v / 16 : -((-v + 15) / 16)); };   // ceil(v / 16) = -floor16(-v)
    int minx = std::max(-floor16(-std::min({x0, x1, x2})), ps.sx0), maxx = std::min(floor16(std::max({x0, x1, x2})), ps.sx1);
    int miny = std::max(-floor16(-std::min({y0, y1, y2})), ps.sy0), maxy = std::min(floor16(std::max({y0, y1, y2})), ps.sy1);
    if (minx > maxx || miny > maxy) return;
    // edge i (opposite vertex i) at sample S: E = A * Sx + B * Sy + C, twice the area of (S, next, next);
    // inside: E > 0, or E == 0 on a top edge (horizontal, left to right) or a left edge (going up)
    const int64_t A0 = y1 - y2, B0 = x2 - x1, C0 = x1 * y2 - x2 * y1;
    const int64_t A1 = y2 - y0, B1 = x0 - x2, C1 = x2 * y0 - x0 * y2;
    const int64_t A2 = y0 - y1, B2 = x1 - x0, C2 = x0 * y1 - x1 * y0;
    auto topLeft = [](int64_t dx, int64_t dy) { return dy < 0 || (dy == 0 && dx > 0); };
    const int64_t m0 = topLeft(x2 - x1, y2 - y1) ? 0 : 1, m1 = topLeft(x0 - x2, y0 - y2) ? 0 : 1, m2 = topLeft(x1 - x0, y1 - y0) ? 0 : 1;
    // attribute planes: f = f0 + (d1 * E1 + d2 * E2) / area
    const double inv = 1.0 / (double)area;
    const Pix p0 = Lerp(*v0, at.fst), p1 = Lerp(*v1, at.fst), p2 = Lerp(*v2, at.fst), flat = Lerp(d, at.fst);
    struct Plane { double f0, d1, d2; double At(double e1, double e2) const { return f0 + (d1 * e1 + d2 * e2); } };
    auto plane = [&](double f0, double f1, double f2) { return Plane{f0, (f1 - f0) * inv, (f2 - f0) * inv}; };
    const Plane pr = plane(p0.r, p1.r, p2.r), pg = plane(p0.g, p1.g, p2.g), pb = plane(p0.b, p1.b, p2.b), pa = plane(p0.a, p1.a, p2.a);
    const Plane pz = plane(p0.z, p1.z, p2.z), pf = plane(p0.f, p1.f, p2.f);
    const Plane ps_ = plane(p0.s, p1.s, p2.s), pt = plane(p0.t, p1.t, p2.t), pq = plane(p0.q, p1.q, p2.q);
    const Plane pu = plane(p0.u, p1.u, p2.u), pv = plane(p0.v, p1.v, p2.v);
    ForRows(rows, miny, maxy, [&](int y) {
        const int64_t sy = (int64_t)y * 16, sx = (int64_t)minx * 16;
        int64_t e0 = A0 * sx + B0 * sy + C0, e1 = A1 * sx + B1 * sy + C1, e2 = A2 * sx + B2 * sy + C2;
        for (int x = minx; x <= maxx; ++x, e0 += A0 * 16, e1 += A1 * 16, e2 += A2 * 16) {
            if (e0 < m0 || e1 < m1 || e2 < m2) continue;
            const double f1 = (double)e1, f2 = (double)e2;
            Pix p;
            if (at.iip) { p.r = (float)pr.At(f1, f2); p.g = (float)pg.At(f1, f2); p.b = (float)pb.At(f1, f2); p.a = (float)pa.At(f1, f2); }
            else { p.r = flat.r; p.g = flat.g; p.b = flat.b; p.a = flat.a; }
            p.z = pz.At(f1, f2); p.f = (float)pf.At(f1, f2);
            p.s = (float)ps_.At(f1, f2); p.t = (float)pt.At(f1, f2); p.q = (float)pq.At(f1, f2);
            p.u = (float)pu.At(f1, f2); p.v = (float)pv.At(f1, f2);
            Shade(ps, x, y, p);
        }
    });
}
void Sprite(const PS& ps, const Rows& rows, const Vertex& a, const Vertex& b) {
    const Attr& at = ps.at;
    const float ox = ps.ox, oy = ps.oy;
    float x0 = a.x / 16.0f - ox, y0 = a.y / 16.0f - oy, x1 = b.x / 16.0f - ox, y1 = b.y / 16.0f - oy;
    const Pix pa = Lerp(a, at.fst), pb = Lerp(b, at.fst);
    const bool swapx = x1 < x0, swapy = y1 < y0;
    const float lx = std::min(x0, x1), hx = std::max(x0, x1), ly = std::min(y0, y1), hy = std::max(y0, y1);
    int minx = (int)ceilf(lx), maxx = (int)ceilf(hx) - 1, miny = (int)ceilf(ly), maxy = (int)ceilf(hy) - 1;
    minx = std::max(minx, ps.sx0); maxx = std::min(maxx, ps.sx1);
    miny = std::max(miny, ps.sy0); maxy = std::min(maxy, ps.sy1);
    const float w = hx - lx, h = hy - ly;
    ForRows(rows, miny, maxy, [&](int y) {
        float fy = h > 0 ? (y - ly) / h : 0; if (swapy) fy = 1 - fy;
        for (int x = minx; x <= maxx; ++x) {
            float fx = w > 0 ? (x - lx) / w : 0; if (swapx) fx = 1 - fx;
            Pix p = pb;                                                     // flat: the second vertex's colour
            p.z = (double)b.z;
            p.u = pa.u + (pb.u - pa.u) * fx; p.v = pa.v + (pb.v - pa.v) * fy;
            p.s = pa.s + (pb.s - pa.s) * fx; p.t = pa.t + (pb.t - pa.t) * fy; p.q = pb.q;
            Shade(ps, x, y, p);
        }
    });
}
void Line(const PS& ps, const Rows& rows, const Vertex& a, const Vertex& b) {
    const Attr& at = ps.at;
    const float ox = ps.ox, oy = ps.oy;
    const float x0 = a.x / 16.0f - ox, y0 = a.y / 16.0f - oy, x1 = b.x / 16.0f - ox, y1 = b.y / 16.0f - oy;
    const int steps = (int)std::max(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    const Pix pa = Lerp(a, at.fst), pb = Lerp(b, at.fst);
    for (int i = 0; i < steps; ++i) {
        const float t = steps > 1 ? (float)i / (steps - 1) : 0;
        Pix p = pb;
        if (at.iip) { p.r = pa.r + (pb.r - pa.r) * t; p.g = pa.g + (pb.g - pa.g) * t; p.b = pa.b + (pb.b - pa.b) * t; p.a = pa.a + (pb.a - pa.a) * t; }
        p.z = pa.z + (pb.z - pa.z) * t;
        p.u = pa.u + (pb.u - pa.u) * t; p.v = pa.v + (pb.v - pa.v) * t;
        p.s = pa.s + (pb.s - pa.s) * t; p.t = pa.t + (pb.t - pa.t) * t; p.q = pa.q + (pb.q - pa.q) * t;
        const int py = (int)(y0 + (y1 - y0) * t);
        if (rows.Has(py)) Shade(ps, (int)(x0 + (x1 - x0) * t), py, p);
    }
}
void Draw(int kind, const PS& ps, const Rows& r, const Vertex& a, const Vertex& b, const Vertex& d) {
    switch (kind) {
    case 0: Line(ps, r, a, b); break;
    case 1: Triangle(ps, r, a, b, d); break;
    default: Sprite(ps, r, a, b); break;
    }
}

// ---- deferred drawing: primitives go to a ring that worker threads draw, each one the rows
// of its bands (Rows) in the ring's order. A pixel is always drawn by the same worker, in the
// primitives' order, so the picture is the serial one. The ring drains first when a primitive
// reads (texture) pages that queued ones write, writes pages they read, or writes pages they
// write through another buffer layout; and before this thread reads or writes such pages
// (transfers, CLUT loads, the display). A primitive whose rows could meet another worker's
// (its texture on its own pages, frame and Z buffers on the same pages, drawn past the buffer
// width) is drawn here after a drain.
constexpr uint32_t kRing = 8192, kClutSlots = 256;
struct Cmd { int kind; PS ps; Vertex v[3]; };
struct Pool {
    int n = 0;                                            // workers; 0 = drawn on the caller's thread
    Cmd* ring = nullptr;
    uint64_t queued = 0;                                  // commands written
    std::atomic<uint64_t> pub{0};                         // commands the workers may draw
    std::atomic<uint64_t>* done = nullptr;                // per worker: commands drawn
    std::mutex m; std::condition_variable work, idle;
    std::atomic<int> sleepers{0}; std::atomic<bool> waiting{false};
    Pages written, read; uint32_t key[512] = {};          // since the last drain; key: the layout that wrote a page
    uint32_t (*clut)[512] = nullptr; uint64_t clutUse[kClutSlots] = {}; uint32_t clutCur = 0; uint64_t clutVer = ~0ull;
    uint64_t drains = 0, waitNs = 0, serial = 0, hereNs = 0;   // log
};
Pool* gPool = nullptr;
int gThreadsWanted = -1;                                  // GsSetThreads; -1 = the processor count (up to 8)
inline void CpuRelax() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}
void Worker(int w) {
    Pool& P = *gPool;
    const Rows rows{w, P.n};
    uint64_t i = 0;
    for (;;) {
        uint64_t p = P.pub.load(std::memory_order_acquire);
        for (int spin = 0; i == p && spin < 4000; ++spin) { CpuRelax(); p = P.pub.load(std::memory_order_acquire); }
        if (i == p) {
            std::unique_lock<std::mutex> l(P.m);
            P.sleepers.fetch_add(1);
            P.work.wait(l, [&] { p = P.pub.load(); return p != i; });
            P.sleepers.fetch_sub(1);
        }
        while (i < p) {
            const uint64_t end = std::min(p, i + 64);
            for (; i < end; ++i) { const Cmd& c = P.ring[i % kRing]; Draw(c.kind, c.ps, rows, c.v[0], c.v[1], c.v[2]); }
            P.done[w].store(i);
            if (P.waiting.load()) { std::lock_guard<std::mutex> l(P.m); P.idle.notify_all(); }
        }
    }
}
Pool& GetPool() {
    if (gPool) return *gPool;
    Pool* p = new Pool;                                   // kept to the end (the workers never stop)
    int n = gThreadsWanted >= 0 ? gThreadsWanted : (int)std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
    p->n = std::clamp(n, 0, 32);
    if (p->n) {
        p->ring = new Cmd[kRing];
        p->clut = new uint32_t[kClutSlots][512];
        p->done = new std::atomic<uint64_t>[p->n];
        for (int w = 0; w < p->n; ++w) p->done[w].store(0);
    }
    gPool = p;
    for (int w = 0; w < p->n; ++w) std::thread(Worker, w).detach();
    return *p;
}
uint64_t MinDone(Pool& P) { uint64_t m = ~0ull; for (int w = 0; w < P.n; ++w) m = std::min(m, P.done[w].load()); return m; }
void Publish(Pool& P) {
    if (P.pub.load(std::memory_order_relaxed) == P.queued) return;
    P.pub.store(P.queued);
    if (P.sleepers.load()) { std::lock_guard<std::mutex> l(P.m); P.work.notify_all(); }
}
void WaitDone(Pool& P, uint64_t target) {                 // until every worker drew the commands before target
    if (MinDone(P) >= target) return;
    Publish(P);
    const auto t0 = std::chrono::steady_clock::now();
    for (int spin = 0; spin < 4000 && MinDone(P) < target; ++spin) CpuRelax();
    if (MinDone(P) < target) {
        std::unique_lock<std::mutex> l(P.m);
        P.waiting.store(true);
        P.idle.wait(l, [&] { return MinDone(P) >= target; });
        P.waiting.store(false);
    }
    P.waitNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}
bool PoolLive() { return gPool && gPool->n; }
void DrainAll() {
    if (!PoolLive()) return;
    Pool& P = *gPool;
    WaitDone(P, P.queued);
    P.written.Clear(); P.read.Clear();
    ++P.drains;
}
void HazardRead(const Pages& p) { if (PoolLive() && p.Meets(gPool->written)) DrainAll(); }
void HazardWrite(const Pages& p) { if (PoolLive() && (p.Meets(gPool->written) || p.Meets(gPool->read))) DrainAll(); }
const uint32_t* ClutSnapshot() {                          // the CLUT for a queued primitive
    if (!PoolLive()) return G.clut;
    Pool& P = *gPool;
    if (P.clutVer != gClutVersion) {
        const uint32_t next = (P.clutCur + 1) % kClutSlots;
        if (P.clutUse[next] > MinDone(P)) WaitDone(P, P.clutUse[next]);
        memcpy(P.clut[next], G.clut, sizeof G.clut);
        P.clutCur = next; P.clutVer = gClutVersion;
    }
    return P.clut[P.clutCur];
}
bool KeyClash(const Pool& P, const Pages& w, uint32_t key) {   // queued primitives wrote these pages through another layout
    for (int k = 0; k < 8; ++k)
        for (uint64_t m = w.w[k] & P.written.w[k]; m; m &= m - 1)
            if (P.key[k * 64 + __builtin_ctzll(m)] != key) return true;
    return false;
}
void AddWritten(Pool& P, const Pages& w, uint32_t key) {
    for (int k = 0; k < 8; ++k)
        for (uint64_t m = w.w[k]; m; m &= m - 1) P.key[k * 64 + __builtin_ctzll(m)] = key;
    P.written.Add(w);
}
// what the texture lookups of a primitive may read: the texel range its vertices span (a texel
// wider for filtering; perspective keeps s/q between the vertices' when every q is positive),
// through the wrap mode
void TexturePages(const PS& s, int kind, const Vertex* v, Pages& r) {
    float lu = 1e30f, hu = -1e30f, lv = 1e30f, hv = -1e30f; bool known = true;
    const int nv = kind == 1 ? 3 : 2;
    for (int k = 0; k < nv; ++k) {
        float u, t;
        if (s.fst) { u = v[k].u / 16.0f; t = v[k].v / 16.0f; }
        else {
            const float q = kind == 2 ? v[1].q : v[k].q;                             // a sprite uses its second q
            if (!(q > 0)) { known = false; break; }
            u = v[k].s / q * s.ftw; t = v[k].t / q * s.fth;
        }
        if (!(fabsf(u) < 1e6f) || !(fabsf(t) < 1e6f)) { known = false; break; }
        lu = std::min(lu, u); hu = std::max(hu, u); lv = std::min(lv, t); hv = std::max(hv, t);
    }
    auto range = [&](uint32_t mode, int size, int mn, int mx, float fl, float fh, int& lo, int& hi) {
        const int a = (int)floorf(fl) - 1, b = (int)floorf(fh) + 1;
        if (mode == 0) { if (known && a >= 0 && b <= size - 1) { lo = a; hi = b; } else { lo = 0; hi = size - 1; } }
        else if (mode == 1) { if (known) { lo = std::clamp(a, 0, size - 1); hi = std::clamp(b, 0, size - 1); } else { lo = 0; hi = size - 1; } }
        else if (mode == 2) {
            if (known && mn <= mx) { lo = std::clamp(a, mn, mx); hi = std::clamp(b, mn, mx); }
            else { lo = std::min(mn, mx); hi = std::max(mn, mx); }
        } else { lo = 0; hi = mn | mx; }
    };
    int u0, u1, v0, v1;
    range(s.wms, s.tw, s.minu, s.maxu, lu, hu, u0, u1); range(s.wmt, s.th, s.minv, s.maxv, lv, hv, v0, v1);
    int layout = 0;
    switch (s.tpsm & 0x3f) { case PSMCT16: case PSMCT16S: layout = 1; break; case PSMT8: layout = 3; break; case PSMT4: layout = 4; break; }
    MarkRect(r, s.tbp, s.tbw, layout, u0, v0, std::min(u1, 2047), std::min(v1, 2047));
}
// the pages a primitive draws on (its box in the scissor, the depth buffer's when it writes depth) lose their ids
void NoteDrawn(const Context& c, int kind, const Vertex* v, bool zwrite) {
    const uint32_t fbw = Bits(c.frame, 16, 6);
    if (gIdLo > gIdHi || !fbw) return;
    const float ox = Bits(c.xyoffset, 0, 16) / 16.0f, oy = Bits(c.xyoffset, 32, 16) / 16.0f;
    const int nv = kind == 1 ? 3 : 2;
    float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
    for (int k = 0; k < nv; ++k) {
        const float x = v[k].x / 16.0f - ox, y = v[k].y / 16.0f - oy;
        lx = std::min(lx, x); hx = std::max(hx, x); ly = std::min(ly, y); hy = std::max(hy, y);
    }
    const int x0 = std::max((int)floorf(lx), (int)Bits(c.scissor, 0, 11)), x1 = std::min((int)ceilf(hx), std::min((int)Bits(c.scissor, 16, 11), (int)fbw * 64 - 1));
    const int y0 = std::max((int)floorf(ly), (int)Bits(c.scissor, 32, 11)), y1 = std::min((int)ceilf(hy), (int)Bits(c.scissor, 48, 11));
    if (x0 > x1 || y0 > y1) return;
    auto touches = [&](uint32_t bp, uint32_t psm) {           // the box's first and last page against the id range
        int pw = 64, ph = 32; uint32_t bwp = fbw;
        switch (Family(psm)) { case 1: case 2: ph = 64; break; case 3: pw = 128; ph = 64; bwp = fbw >> 1; break; case 4: pw = 128; ph = 128; bwp = fbw >> 1; break; }
        const uint32_t lo = (bp >> 5) + (uint32_t)(y0 / ph) * bwp + (uint32_t)(x0 / pw);
        const uint32_t hi = (bp >> 5) + (uint32_t)(y1 / ph) * bwp + (uint32_t)(x1 / pw) + ((bp & 31) ? 1 : 0);
        return hi >= gIdLo && lo <= gIdHi;                    // (a range past 511 wraps: seen as touching)
    };
    const uint32_t fbp = Bits(c.frame, 0, 9) * 32, fpsm = Bits(c.frame, 24, 6), zbp = Bits(c.zbuf, 0, 9) * 32, zpsm = Bits(c.zbuf, 24, 4) | 0x30;
    const bool f = touches(fbp, fpsm), z = zwrite && touches(zbp, zpsm);
    if (!f && !z) return;
    Pages w;
    if (f) MarkRect(w, fbp, fbw, Family(fpsm), x0, y0, x1, y1);
    if (z) MarkRect(w, zbp, fbw, Family(zpsm), x0, y0, x1, y1);
    if (!w.Meets(gIdPages)) return;
    for (int k = 0; k < 8; ++k) gIdPages.w[k] &= ~w.w[k];
    ++gIdSerial;
}

// the CLUT entries a paletted texture reads (T8: all 256, T4: the 16 at CSA)
uint64_t ClutHash(uint64_t tex0) {
    static uint64_t ver = ~0ull, sel = ~0ull, h = 0;
    const uint32_t psm = Bits(tex0, 20, 6) & 0x3f, cpsm = Bits(tex0, 51, 4), csa = Bits(tex0, 56, 5);
    const bool eight = psm == PSMT8 || psm == PSMT8H, c16 = cpsm == 2 || cpsm == 0xa;
    const uint64_t now = (uint64_t)eight << 8 | (uint64_t)c16 << 7 | (eight ? 0 : csa);
    if (ver == gClutVersion && sel == now) return h;
    ver = gClutVersion; sel = now;
    h = eight ? hdtex::Hash64(G.clut, 256 * 4, 1) : hdtex::Hash64(G.clut + (csa & (c16 ? 0x1f : 0x0f)) * 16, 16 * 4, 2);
    return h = Mix(h, (uint64_t)c16 << 1 | cpsm >> 3);
}
// the texture's key (0: none: a page without an id, region repeat)
uint64_t TexKey(const Context& c, int& w, int& h, bool& repeat) {
    const uint64_t t0 = c.tex0;
    const uint32_t tbp = Bits(t0, 0, 14), tbw = Bits(t0, 14, 6), psm = Bits(t0, 20, 6) & 0x3f;
    w = 1 << std::min(Bits(t0, 26, 4), 10u); h = 1 << std::min(Bits(t0, 30, 4), 10u);
    const uint32_t wms = Bits(c.clamp, 0, 2), wmt = Bits(c.clamp, 2, 2);
    if (wms == 3 || wmt == 3 || IsZ(psm)) return 0;
    repeat = wms == 0 || wmt == 0;
    Pages p; MarkRect(p, tbp, tbw ? tbw : 1, Family(psm), 0, 0, w - 1, h - 1);
    uint64_t k = Mix(Mix(psm, tbw), (uint64_t)w << 32 | (uint64_t)h << 16 | (tbp & 31));
    int first = -1;
    for (int q = 0; q < 8; ++q)
        for (uint64_t m = p.w[q]; m; m &= m - 1) {
            const int pg = q * 64 + __builtin_ctzll(m);
            if (!((gIdPages.w[pg >> 6] >> (pg & 63)) & 1)) return 0;
            if (first < 0) first = pg;
            k = Mix(k, Mix(gPageId[pg], (uint64_t)(pg - first)));
        }
    if (psm == PSMT8 || psm == PSMT8H || psm == PSMT4 || psm == PSMT4HL || psm == PSMT4HH) k = Mix(k, ClutHash(t0));
    else if (psm == PSMCT24 || psm == PSMCT16 || psm == PSMCT16S) k = Mix(k, G.texa & 0xff000080ffull);
    return k ? k : 1;
}
// the texture's pixels as the GS reads them (CLUT, TEXA), for the upscaler and the dump
bool DecodeTex(void* user, std::vector<uint32_t>& rgba, int w, int h) {
    const Context& c = *(const Context*)user;
    Attr at{}; at.tme = true;
    const PS s = MakePS(at, c);
    rgba.resize((size_t)w * h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) rgba[(size_t)y * w + x] = s.texel(s, x, y);
    return true;
}
struct { uint64_t tex0 = ~0ull, clamp = 0, texa = 0, clut = ~0ull, ids = ~0ull, vsync = 0; int slot = 0; } gTk;
uint64_t gVSyncs = 0;                   // VBlanks (a texture whose HD answer is not known yet is asked again at the next)
std::atomic<uint64_t> sHdKeyed{0}, sHdNone{0}, sHdNs{0};
int HdSlot(const Context& c) {
    if (gTk.tex0 == c.tex0 && gTk.clamp == c.clamp && gTk.texa == G.texa && gTk.clut == gClutVersion && gTk.ids == gIdSerial &&
        (gTk.slot >= 0 || gTk.vsync == gVSyncs)) return std::max(gTk.slot, 0);
    gTk.tex0 = c.tex0; gTk.clamp = c.clamp; gTk.texa = G.texa; gTk.clut = gClutVersion; gTk.ids = gIdSerial; gTk.vsync = gVSyncs;
    int w = 0, h = 0; bool repeat = false;
    const uint64_t key = TexKey(c, w, h, repeat);
    if (!key) { ++sHdNone; return gTk.slot = 0; }
    ++sHdKeyed;
    Context copy = c;
    const auto t0 = std::chrono::steady_clock::now();
    gTk.slot = hdtex::SlotOf(key, w, h, repeat, &DecodeTex, &copy);
    sHdNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    return std::max(gTk.slot, 0);
}
int HdOfDraw(const Attr& at, const Context& c, int kind, const Vertex& a, const Vertex& b, const Vertex& d) {
    if (!gHdOn) return 0;
    const Vertex v[3] = {a, b, d};
    NoteDrawn(c, kind, v, Bits(c.test, 16, 1) && !Bits(c.zbuf, 32, 1));
    return at.tme ? HdSlot(c) : 0;
}
void SubmitGpu(int kind, const Vertex& a, const Vertex& b, const Vertex& d) {
    const Attr at = Attrs();
    const Context& c = G.ctx[at.ctxt];
    const int hd = HdOfDraw(at, c, kind, a, b, d);
    gsgpu::Regs r;
    r.prim = Bits(G.prmodecont, 0, 1) ? G.prim : G.prmode;
    r.frame = c.frame; r.zbuf = c.zbuf; r.test = c.test; r.alpha = c.alpha; r.tex0 = c.tex0; r.tex1 = c.tex1; r.clamp = c.clamp;
    r.scissor = c.scissor; r.xyoffset = c.xyoffset; r.fba = c.fba;
    r.texa = G.texa; r.fogcol = G.fogcol; r.colclamp = G.colclamp; r.pabe = G.pabe; r.dthe = G.dthe; r.scanmsk = G.scanmsk;
    r.miptbp1 = c.miptbp1; r.miptbp2 = c.miptbp2;
    auto cv = [](const Vertex& v) { gsgpu::Vtx o; o.x = v.x; o.y = v.y; o.z = v.z; o.r = v.r; o.g = v.g; o.b = v.b; o.a = v.a; o.f = v.f;
                                    o.s = v.s; o.t = v.t; o.q = v.q; o.u = v.u; o.v = v.v; return o; };
    gsgpu::Draw(kind == 0 ? 0 : kind == 1 ? 1 : 2, r, cv(a), cv(b), cv(d), hd);
}
void Submit(int kind, const Vertex& a, const Vertex& b, const Vertex& d) {
    if (gsgpu::On()) { SubmitGpu(kind, a, b, d); return; }
    const Attr at = Attrs();
    const Context& c = G.ctx[at.ctxt];
    HdOfDraw(at, c, kind, a, b, d);
    Pool& P = GetPool();
    if (!P.n) { const PS ps = MakePS(at, c); Draw(kind, ps, Rows{0, 1}, a, b, d); return; }
    if (P.queued - MinDone(P) >= kRing) WaitDone(P, P.queued - kRing + 1);
    Cmd& q = P.ring[P.queued % kRing];
    q.kind = kind; q.ps = MakePS(at, c); q.v[0] = a; q.v[1] = b; q.v[2] = d;
    const PS& s = q.ps;
    // the rectangle it may draw: its vertices' box in the scissor
    const int nv = kind == 1 ? 3 : 2;
    float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
    for (int k = 0; k < nv; ++k) {
        const float x = q.v[k].x / 16.0f - s.ox, y = q.v[k].y / 16.0f - s.oy;
        lx = std::min(lx, x); hx = std::max(hx, x); ly = std::min(ly, y); hy = std::max(hy, y);
    }
    const int bx0 = std::max((int)floorf(lx), s.sx0), bx1 = std::min((int)ceilf(hx), s.sx1);
    const int by0 = std::max((int)floorf(ly), s.sy0), by1 = std::min((int)ceilf(hy), s.sy1);
    if (bx0 > bx1 || by0 > by1) return;                                     // nothing in the scissor
    Pages w, z, r;
    MarkRect(w, s.fbp, s.fbw, Family(s.fpsm), bx0, by0, bx1, by1);
    const uint32_t keyF = s.fbp | s.fbw << 14 | (s.fpsm & 0x3f) << 20, keyZ = s.zbp | s.fbw << 14 | (s.zpsm & 0x3f) << 20;
    if (s.zte && (s.ztst >= 2 || !s.zmsk)) MarkRect(z, s.zbp, s.fbw, Family(s.zpsm), bx0, by0, bx1, by1);
    if (s.tme) TexturePages(s, kind, q.v, r);
    const bool here = !s.fbw || bx1 >= (int)s.fbw * 64 || w.Meets(z) || r.Meets(w) || r.Meets(z) ||
                      (G.xfer && (r.Meets(gXferPages) || w.Meets(gXferPages) || z.Meets(gXferPages)));
    if (here || r.Meets(P.written) || w.Meets(P.read) || z.Meets(P.read) || KeyClash(P, w, keyF) || KeyClash(P, z, keyZ)) DrainAll();
    if (here) {
        ++P.serial;
        const auto t0 = std::chrono::steady_clock::now();
        Draw(kind, s, Rows{0, 1}, q.v[0], q.v[1], q.v[2]);
        P.hereNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        return;
    }
    P.read.Add(r); AddWritten(P, w, keyF); AddWritten(P, z, keyZ);
    if (s.clut == P.clut[P.clutCur]) P.clutUse[P.clutCur] = P.queued + 1;
    ++P.queued;
    if (P.queued - P.pub.load(std::memory_order_relaxed) >= 32) Publish(P);
}

uint64_t gPrims = 0;                                                        // primitives drawn (log)
}  // namespace
long long gGsDrawLimit = -1;                                                // tests: the primitives after this many are not drawn
namespace {
void KickVertex(bool drawing) {
    const int type = (int)Bits(G.prim, 0, 3);
    const Attr at = Attrs();
    const Context& c = G.ctx[at.ctxt];
    auto draw = [&](auto fn) {
        if (!drawing || !(GsThreadOn() ? gGsDrawNow : gGsDraw) || (gGsDrawLimit >= 0 && gPrims >= (uint64_t)gGsDrawLimit)) return;
        if (gGsPrimHook) {                                                  // the draw log of the replay tool
            GsPrimInfo pi{};
            pi.type = type; pi.prim = Attrs().ctxt ? (G.prim | 0x200) : G.prim; pi.frame = c.frame; pi.zbuf = c.zbuf; pi.tex0 = c.tex0;
            pi.tex1 = c.tex1; pi.clamp = c.clamp; pi.alpha = c.alpha; pi.test = c.test; pi.fba = c.fba; pi.texa = G.texa;
            const float ox = Bits(c.xyoffset, 0, 16) / 16.0f, oy = Bits(c.xyoffset, 32, 16) / 16.0f;
            pi.x0 = pi.y0 = 1e9f; pi.x1 = pi.y1 = -1e9f;
            const int nv = type == 6 || type == 1 || type == 2 ? 2 : type == 0 ? 1 : 3;
            for (int k = 0; k < nv && k < G.nq; ++k) {
                const Vertex& v = G.queue[G.nq - 1 - k];
                const float x = v.x / 16.0f - ox, y = v.y / 16.0f - oy;
                pi.x0 = std::min(pi.x0, x); pi.y0 = std::min(pi.y0, y); pi.x1 = std::max(pi.x1, x); pi.y1 = std::max(pi.y1, y);
                pi.xs[k] = x; pi.ys[k] = y; pi.zs[k] = v.z;
                pi.u[k] = at.fst ? v.u / 16.0f : v.s; pi.v[k] = at.fst ? v.v / 16.0f : v.t; pi.q[k] = v.q;
                pi.rgba[k] = v.r | v.g << 8 | v.b << 16 | (uint32_t)v.a << 24;
            }
            gGsPrimHook(pi);
        }
        if (gGsTrace > 0) {                                                 // the vertices (trace)
            const int nv = type == 6 || type == 1 || type == 2 ? 2 : type == 0 ? 1 : 3;
            char b[256]; int n = snprintf(b, sizeof b, "prim %d:", type);
            for (int k = nv - 1; k >= 0 && k < G.nq && n < 200; --k) {
                const Vertex& v = G.queue[G.nq - 1 - k];
                n += snprintf(b + n, sizeof b - n, " (%.1f,%.1f z %06x a %02x)", v.x / 16.0f, v.y / 16.0f, v.z, v.a);
            }
            GsTraceLog("%s", b);
        }
        fn(); ++gPrims;
    };
    switch (type) {
    case 0: draw([&] { Submit(0, G.queue[G.nq - 1], G.queue[G.nq - 1], G.queue[G.nq - 1]); }); G.nq = 0; return;   // point
    case 1: if (G.nq == 2) { draw([&] { Submit(0, G.queue[0], G.queue[1], G.queue[1]); }); G.nq = 0; } return;       // line
    case 2: if (G.nq == 2) { draw([&] { Submit(0, G.queue[0], G.queue[1], G.queue[1]); }); G.queue[0] = G.queue[1]; G.nq = 1; } return;   // line strip
    case 3: if (G.nq == 3) { draw([&] { Submit(1, G.queue[0], G.queue[1], G.queue[2]); }); G.nq = 0; } return;   // triangle
    case 4: if (G.nq == 3) { draw([&] { Submit(1, G.queue[0], G.queue[1], G.queue[2]); }); G.queue[0] = G.queue[1]; G.queue[1] = G.queue[2]; G.nq = 2; } return;   // strip
    case 5:                                                                  // fan: the first vertex stays
        if (G.nq == 3) { draw([&] { Submit(1, G.queue[0], G.queue[1], G.queue[2]); }); G.queue[1] = G.queue[2]; G.nq = 2; }
        return;
    case 6: if (G.nq == 2) { draw([&] { Submit(2, G.queue[0], G.queue[1], G.queue[1]); }); G.nq = 0; } return;    // sprite
    default: G.nq = 0; return;
    }
}

// ---- GIF packets
uint64_t gGifTags = 0, gGifImageQw = 0;
uint64_t gPathKicks[4] = {}, gPathQw[4] = {};                                 // per GIF path (log)
FILE* gDump = nullptr;                                                        // GsDumpBegin .. GsDumpEnd
}  // namespace

// ---- dumps: the GS state when a dump starts, then every packet and privileged write that
// reaches it until it ends; GsReplay runs them again on that state (tests/gs_replay.cpp)
bool GsDumpBegin(const char* path) {
    GsSync();                                                               // the GS thread waits: its state is read here
    if (gDump) return false;
    DrainAll();
    if (gsgpu::On()) { uint64_t all[8]; memset(all, 0xff, sizeof all); gsgpu::CpuWillRead(all); }
    gDump = fopen(path, "wb");
    if (!gDump) return false;
    const char magic[8] = {'M', 'G', 'O', 'G', 'S', 'D', '1', 0};
    const uint32_t n = sizeof(State);
    fwrite(magic, 1, 8, gDump); fwrite(&n, 4, 1, gDump); fwrite(&G, sizeof G, 1, gDump);
    fwrite(gVram, sizeof gVram, 1, gDump);
    return true;
}
void GsDumpEnd() {
    GsSync();
    if (!gDump) return;
    const uint32_t end = 0;
    fwrite(&end, 4, 1, gDump); fclose(gDump); gDump = nullptr;
}
bool GsReplay(const char* path, long long& packets, long long& writes) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    DrainAll();
    char magic[8]; uint32_t n = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "MGOGSD1", 8) || fread(&n, 4, 1, f) != 1 || n != sizeof(State) ||
        fread(&G, sizeof G, 1, f) != 1 || fread(gVram, sizeof gVram, 1, f) != 1) { fclose(f); return false; }
    gsgpu::CpuWroteAll();
    gsgpu::SetClut(G.clut);
    std::vector<uint8_t> buf;
    packets = writes = 0;
    {   // the frame buffers the dump shows (widescreen: what its 2D elements draw on the screen)
        NoteShown(G.dispfb[0]); NoteShown(G.dispfb[1]);
        const long at = ftell(f);
        for (;;) {
            uint32_t type = 0;
            if (fread(&type, 4, 1, f) != 1 || type == 0) break;
            if (type == 1) { uint32_t h[2]; if (fread(h, 4, 2, f) != 2 || fseek(f, (long)h[1] * 16, SEEK_CUR)) break; }
            else if (type == 2) { uint32_t a; uint64_t v; if (fread(&a, 4, 1, f) != 1 || fread(&v, 8, 1, f) != 1) break; if ((a & 0x1ff0) == 0x70 || (a & 0x1ff0) == 0x90) NoteShown(v); }
            else if (type == 3) { uint32_t imm; if (fread(&imm, 4, 1, f) != 1) break; }
            else break;
        }
        fseek(f, at, SEEK_SET);
    }
    for (;;) {
        uint32_t type = 0;
        if (fread(&type, 4, 1, f) != 1 || type == 0) break;
        if (type == 1) {
            uint32_t h[2]; if (fread(h, 4, 2, f) != 2) break;
            buf.resize((size_t)h[1] * 16);
            if (h[1] && fread(buf.data(), 16, h[1], f) != h[1]) break;
            GifPath((int)h[0], buf.data(), h[1]);
            ++packets;
        } else if (type == 2) {
            uint32_t a; uint64_t v;
            if (fread(&a, 4, 1, f) != 1 || fread(&v, 8, 1, f) != 1) break;
            GsPrivWrite(a, v, 8);
            ++writes;
        } else if (type == 3) {                                             // a 2D element's MARK (widescreen)
            uint32_t imm; if (fread(&imm, 4, 1, f) != 1) break;
            GsPackMark(imm);
        } else break;
    }
    GsPackMark(0x5aff);
    fclose(f);
    DrainAll();
    gsgpu::Flush();
    return true;
}

namespace {
// the GIF keeps one tag in progress per path: a tag's data may come in several transfers
// (an IMAGE of 32752 quadwords over many VIF1 DIRECTs, PATH3 chains cut anywhere)
struct GifPathState { bool active = false; uint64_t tagLo = 0, tagHi = 0; uint32_t nloop = 0, nreg = 16, flg = 0, eop = 0, loop = 0, reg = 0; };
GifPathState gGif[4];
void PackedReg(uint32_t reg, uint64_t a, uint64_t b) {
    switch (reg) {
    case 0x0: WriteReg(0x00, a & 0x7ff); break;
    case 0x1: {                                                             // RGBA; Q from the last ST
        uint32_t qv; memcpy(&qv, &G.q, 4);
        WriteReg(0x01, (a & 0xff) | ((a >> 32) & 0xff) << 8 | (b & 0xff) << 16 | ((b >> 32) & 0xff) << 24 | (uint64_t)qv << 32);
        break; }
    case 0x2: WriteReg(0x02, a); { uint32_t qq = (uint32_t)b; memcpy(&G.q, &qq, 4); } break;   // ST, Q
    case 0x3: WriteReg(0x03, (a & 0x3fff) | ((a >> 32) & 0x3fff) << 16); break;             // UV
    case 0x4: case 0xc: {                                                   // XYZF2 / XYZF3 (ADC: no kick)
        const uint64_t v = (a & 0xffff) | ((a >> 32) & 0xffff) << 16 | ((b >> 4) & 0xffffff) << 32 | ((b >> 36) & 0xff) << 56;
        WriteReg(reg == 0xc || ((b >> 47) & 1) ? 0x0c : 0x04, v); break; }
    case 0x5: case 0xd: {                                                   // XYZ2 / XYZ3 (ADC: no kick)
        const uint64_t v = (a & 0xffff) | ((a >> 32) & 0xffff) << 16 | (b & 0xffffffff) << 32;
        WriteReg(reg == 0xd || ((b >> 47) & 1) ? 0x0d : 0x05, v); break; }
    case 0xa: WriteReg(0x0a, ((b >> 36) & 0xff) << 56); break;              // FOG
    case 0xe: WriteReg((uint32_t)(b & 0xff), a); break;                     // A+D
    case 0xf: break;                                                        // NOP
    default: WriteReg(reg, a); break;                                       // TEX0 / CLAMP (1 and 2)
    }
}
}  // namespace

uint64_t gGifNs = 0;                                                        // time in the GIF / GS (log)
void GifPathTimed(int path, const uint8_t* data, size_t qwords, size_t wrapBytes);
extern void (*gLog)(const char* fmt, ...);

// ---- widescreen: DG's 2D elements (DMAPACKs: the life bar, the timer, the radar, a text...)
// come between two VIF1 MARKs the platform puts around each (GsPackMark). An element's packets
// are held until it ends, then the X coordinates of what it draws on the screen are narrowed
// to the width it has in a 4:3 picture, about the edge it sits nearest: the left third of the
// screen keeps its distance to the left edge, the right third to the right edge, the rest to
// the centre. An element as wide as the screen (a fade, a filter) keeps its width; what it
// draws elsewhere (a texture it makes) is not changed.
namespace {
struct HudPacket { int path; std::vector<uint8_t> bytes; };
struct HudPack { bool open = false; uint32_t kind = 0; std::vector<HudPacket> packets; GifPathState p2; } gPack;
float gHudSqueeze = 1.0f;                   // an element's width in the picture: 0.75 in 16:9
std::atomic<float> gHudScale{1.0f};         // the HUD's size (the in-game menu): each element toward its corner
bool gInMatchHud = false;                   // the elements drawn now are a match's (the platform says)
bool gMenuPage = false;                     // a match's page is shown (the START menu, the team and skin select, the results)
int gHudLog = -1;
long long gHudPacks = 0, gHudMoved = 0;
// 3D labels (an item's name over it: the label system of US n003a 0x5357F0; BASE CAPTURE's texts): where the game
// located each text (the font's cursor: GS X (x + 0x700) * 16, Y (y + 0x720) * 16), given by the platform (GsLabelAt).
// The game's projection placed it for the wide view already: its sprites are narrowed about that place, not about
// the edge of the third they fall in (the dropped FAMAS's name drifted to the right edge at 21:9 and 32:9).
// The labels located after a picture are the next pictures' (one tick's font packet: at 60 the picture between and
// the tick's own), with the tick's before (the label system locates a label every other tick while its text stays), until
// the labels of a later tick; without newer ones, their first 3 pictures and kHudKeepVSyncs
struct LabelAt { int gx, gy, chars; uint64_t vsync; };
std::vector<LabelAt> gLabels;
std::vector<LabelAt> gLabelsBefore;         // the tick's before
uint64_t gLabelsBatch = 0;                  // the VSync count when the labels' tick located its first
bool gLabelsUsed = false;                   // a picture's elements read them: the next label starts a new tick's
int gLabelsUses = 0;                        // the pictures that read them (a long tick's own picture still has them)
uint64_t gHudPictures = 0;                  // the font system's elements drawn in a match (one a picture)
std::mutex gLabelsMu;
const uint64_t kHudKeepVSyncs = 8;          // a tick's labels and the HUD's boxes, without newer ones (and 2 pictures for a box)
bool gShownFbp[512] = {};                   // frame buffers the display circuits have shown

// a packet's length in quadwords: its tags and their data, up to the end of its EOP tag's
// (PATH1), or all of it (PATH2)
size_t GifPacketQw(const uint8_t* d, size_t qwords, bool toEop) {
    size_t q = 0;
    while (q < qwords) {
        uint64_t lo; memcpy(&lo, d + q * 16, 8);
        const uint32_t nloop = Bits(lo, 0, 15), eop = Bits(lo, 15, 1), flg = Bits(lo, 58, 2);
        uint32_t nreg = Bits(lo, 60, 4); if (!nreg) nreg = 16;
        ++q;
        const size_t n = flg == 0 ? (size_t)nloop * nreg : flg == 1 ? ((size_t)nloop * nreg + 1) / 2 : nloop;
        q = std::min(qwords, q + n);
        if (eop && toEop) break;
    }
    return q;
}
// the drawing state an element's writes are made in
struct HudState {
    int ctxt = 0;
    uint64_t ofs[2], frame[2], scissor[2];
    HudState() { for (int k = 0; k < 2; ++k) { ofs[k] = G.ctx[k].xyoffset; frame[k] = G.ctx[k].frame; scissor[k] = G.ctx[k].scissor; } }
    bool OnScreen(int c) const { return gShownFbp[Bits(frame[c], 0, 9)]; }
    int Ox(int c) const { return (int)(ofs[c] & 0xffff); }
    int Oy(int c) const { return (int)((ofs[c] >> 32) & 0xffff); }
};
// each register write of an element's packets, in order, as fn(reg, value, state): reg is the
// GS register (a PACKED XYZ register n as 0x100 | n: X in the value's first bytes as always);
// PATH1 packets start on a tag, PATH2 packets go on from the tag in progress (an image's data
// runs over several DIRECTs)
template <class F> void WalkPack(HudPack& pk, F fn) {
    HudState hs;
    GifPathState s2 = pk.p2;
    auto note = [&](uint32_t reg, uint8_t* v) {
        uint64_t x; memcpy(&x, v, 8);
        if (reg == 0x00 || reg == 0x100) hs.ctxt = (int)((x >> 9) & 1);
        else if (reg == 0x18 || reg == 0x19) hs.ofs[reg - 0x18] = x;
        else if (reg == 0x4c || reg == 0x4d) hs.frame[reg - 0x4c] = x;
        else if (reg == 0x40 || reg == 0x41) hs.scissor[reg - 0x40] = x;
        fn(reg, v, hs);
    };
    for (auto& p : pk.packets) {
        uint8_t* d = p.bytes.data();
        const size_t n = p.bytes.size() / 16;
        GifPathState s1;
        GifPathState& st = p.path == 2 ? s2 : s1;
        size_t q = 0;
        while (q < n) {
            if (!st.active) {
                uint64_t lo, hi; memcpy(&lo, d + q * 16, 8); memcpy(&hi, d + q * 16 + 8, 8); ++q;
                st.tagLo = lo; st.tagHi = hi; st.nloop = Bits(lo, 0, 15); st.eop = Bits(lo, 15, 1); st.flg = Bits(lo, 58, 2);
                st.nreg = Bits(lo, 60, 4); if (!st.nreg) st.nreg = 16;
                st.loop = 0; st.reg = 0;
                if (Bits(lo, 46, 1) && st.flg == 0) { uint64_t prim = Bits(lo, 47, 11); note(0x00, (uint8_t*)&prim); }
                st.active = st.nloop > 0;
                if (!st.active) { if (st.eop && p.path == 1) break; continue; }
            }
            if (st.flg == 0) {                                                  // PACKED
                while (st.loop < st.nloop && q < n) {
                    const uint32_t r = (uint32_t)(st.tagHi >> (4 * st.reg)) & 15;
                    uint8_t* v = d + q * 16; ++q;
                    if (r == 0xe) note((uint32_t)v[8], v); else note(0x100u | r, v);
                    if (++st.reg >= st.nreg) { st.reg = 0; ++st.loop; }
                }
            } else if (st.flg == 1) {                                           // REGLIST: loop counts registers
                const uint64_t total = (uint64_t)st.nloop * st.nreg;
                while (st.loop < total && q < n) {
                    uint8_t* v = d + q * 16; ++q;
                    for (int h = 0; h < 2 && st.loop < total; ++h, ++st.loop) {
                        const uint32_t r = (uint32_t)(st.tagHi >> (4 * (st.loop % st.nreg))) & 15;
                        if (r == 0x0 || r == 0x4 || r == 0x5 || r == 0xc || r == 0xd) note(r, v + 8 * h);
                    }
                }
                if (st.loop >= total) st.loop = st.nloop;
            } else {                                                            // IMAGE
                const size_t take = std::min<size_t>(st.nloop - st.loop, n - q);
                q += take; st.loop += (uint32_t)take;
            }
            if (st.loop >= st.nloop) { st.active = false; if (st.eop && p.path == 1) break; }
        }
    }
}
bool IsXyz(uint32_t reg) { return reg == 0x04 || reg == 0x05 || reg == 0x0c || reg == 0x0d || reg == 0x104 || reg == 0x105 || reg == 0x10c || reg == 0x10d; }
// a vertex's Y: bits 16..31 of a register's value, bits 32..47 of a PACKED quadword
size_t YAt(uint32_t reg) { return reg >= 0x100 ? 4 : 2; }

// the lobby's pages in a wide picture (GsSetHud(k, false), k = 0.75 in 16:9): the page is one element,
// its runs of primitives drawn in one state (the same PRIM, texture, blending ...) placed each by its
// extent: a run across the screen (a background of strips, a band) keeps its X (the wide picture
// stretches it), a run in the page narrowed to its 4:3 width about the centre, a band from one edge
// into the page stretched from that edge; SCISSOR narrowed about the centre unless it is the screen's.
// A band: untextured, or 4 times as long as it is high; a picture that only reaches an edge (the weapon
// inventory's icons, the lobby's arrows) is the page's: narrowed about the centre with the rest
bool StateReg(uint32_t reg, uint32_t& key) {
    if (reg >= 0x100) { const uint32_t n = reg & 0xff; if (n == 0 || (n >= 6 && n <= 9)) { key = n; return true; } return false; }
    if (reg == 0x00 || (reg >= 0x06 && reg <= 0x09) || (reg >= 0x14 && reg <= 0x1c) || reg == 0x22 || reg == 0x3b || reg == 0x3d ||
        (reg >= 0x40 && reg <= 0x4f)) { key = reg; return true; }
    return false;
}
struct RunX {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; uint32_t prim = 0;
    float widest = 0, lastX = 0; int sprites = 0, verts = 0;             // its sprites: the widest, how many
    bool Left() const { return x0 <= 8 && x1 < 504 && Band(); }       // a band from the screen's left edge
    bool Right() const { return x1 >= 504 && x0 > 8 && Band(); }      // ... from its right edge
    bool Across() const { return x0 <= 8 && x1 >= 504; }
    // untextured, or textured, 4 times as long as it is high and not a line of small sprites (a text, icons)
    bool Band() const {
        if (!(prim & 0x10)) return true;
        if (x1 - x0 < 4.0f * std::max(1.0f, y1 - y0)) return false;
        return (prim & 7) != 6 || widest >= 0.5f * (x1 - x0);
    }
};
// an element's runs of primitives drawn in one state and each on-screen vertex's run
void PackRuns(std::vector<RunX>& runs, std::vector<int>& runOf) {
    runs.assign(1, RunX()); runOf.clear();
    std::map<uint32_t, uint64_t> state;
    WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState& hs) {
        uint32_t key;
        if (StateReg(reg, key)) {
            uint64_t val; memcpy(&val, v, 8);
            auto it = state.find(key);
            if (it == state.end() || it->second != val) { state[key] = val; if (runs.back().x1 >= runs.back().x0) runs.emplace_back(); }
            if (key == 0) runs.back().prim = (uint32_t)(val & 0x7ff);
            else if (runs.back().x1 < runs.back().x0) { auto pr = state.find(0); if (pr != state.end()) runs.back().prim = (uint32_t)(pr->second & 0x7ff); }
            return;
        }
        if (!IsXyz(reg) || !hs.OnScreen(hs.ctxt)) return;
        uint16_t x, y; memcpy(&x, v, 2); memcpy(&y, v + YAt(reg), 2);
        const float px = ((int)x - hs.Ox(hs.ctxt)) / 16.0f, py = ((int)y - hs.Oy(hs.ctxt)) / 16.0f;
        runOf.push_back((int)runs.size() - 1);
        RunX& r = runs.back();
        if (px >= -512 && px <= 1024) {
            r.x0 = std::min(r.x0, px); r.x1 = std::max(r.x1, px);
            if ((r.prim & 7) == 6 && (++r.verts & 1) == 0) { r.widest = std::max(r.widest, std::fabs(px - r.lastX)); ++r.sprites; }
            r.lastX = px;
        }
        if (py >= -448 && py <= 896) { r.y0 = std::min(r.y0, py); r.y1 = std::max(r.y1, py); }
    });
}
void LobbyFlush(float k) {
    auto narrow = [k](float x) { return 256.0f + (x - 256.0f) * k; };
    std::vector<RunX> runs;
    std::vector<int> runOf;                                                 // each on-screen vertex's run, in order
    PackRuns(runs, runOf);
    size_t vi = 0;
    WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState& hs) {
        if (IsXyz(reg)) {
            if (!hs.OnScreen(hs.ctxt) || vi >= runOf.size()) return;
            const RunX& r = runs[runOf[vi++]];
            if (r.x1 < r.x0) return;
            const bool left = r.Left(), right = r.Right();
            if (r.Across()) return;                                         // across the screen: as drawn
            uint16_t x; memcpy(&x, v, 2);
            const int ox = hs.Ox(hs.ctxt);
            const float px = (x - ox) / 16.0f;
            if (px < -512 || px > 1024) return;
            float nx;
            if (left && r.x1 > r.x0) nx = r.x0 + (px - r.x0) * (narrow(r.x1) - r.x0) / (r.x1 - r.x0);
            else if (right && r.x1 > r.x0) nx = narrow(r.x0) + (px - r.x0) * (r.x1 - narrow(r.x0)) / (r.x1 - r.x0);
            else nx = narrow(px);
            x = (uint16_t)std::clamp(ox + (int)lrintf(nx * 16.0f), 0, 0xffff);
            memcpy(v, &x, 2);
        } else if ((reg == 0x40 || reg == 0x41) && hs.OnScreen(reg - 0x40)) {
            uint64_t sc; memcpy(&sc, v, 8);
            const float a = (float)Bits(sc, 0, 11), b = (float)Bits(sc, 16, 11) + 1;
            if (a <= 8 && b >= 504) return;
            const int na = std::clamp((int)floorf(narrow(a)), 0, 2047);
            const int nb = std::clamp((int)ceilf(narrow(b)) - 1, na, 2047);
            sc = (sc & ~0x7ff07ffull) | (uint64_t)na | (uint64_t)nb << 16;
            memcpy(v, &sc, 8);
        }
    });
    ++gHudPacks; ++gHudMoved;
    if (gHudLog < 0) gHudLog = getenv("SIM_HUD_LOG") ? atoi(getenv("SIM_HUD_LOG")) : 0;
    if (gHudLog > 0 && gLog) {
        --gHudLog;
        int across = 0, edge = 0, page = 0;
        for (const RunX& r : runs) { if (r.x1 < r.x0) continue; (r.Across() ? across : r.Left() || r.Right() ? edge : page)++; }
        std::string xs, es;
        for (const RunX& r : runs) if (r.x1 >= r.x0 && xs.size() < 200) { char b[24]; snprintf(b, sizeof b, " %.0f-%.0f", r.x0, r.x1); xs += b; }
        for (const RunX& r : runs)                                          // the runs that reach one edge: x, y, PRIM, band or not
            if (r.x1 >= r.x0 && (r.x0 <= 8) != (r.x1 >= 504) && es.size() < 300) {
                char b[80]; snprintf(b, sizeof b, " %.0f-%.0f/%.0f-%.0f p%03x s%d w%.0f%s", r.x0, r.x1, r.y0, r.y1, r.prim, r.sprites, r.widest, r.Band() ? " band" : ""); es += b;
            }
        gLog("lobby: element kind %04x, %zu packets, %d runs across the screen, %d from an edge, %d in the page:%s; at an edge:%s", gPack.kind, gPack.packets.size(), across, edge, page, xs.c_str(), es.c_str());
    }
    for (auto& p : gPack.packets) GifPathTimed(p.path, p.bytes.data(), p.bytes.size() / 16, 0);
    gPack.packets.clear();
}

// a lens's mask in a match (the scopes' and the binoculars' sights): an element across the screen with a run of
// untextured triangles across it (the dark ring, run 2 of hudlogB219: fans 0-512 with the reticle and 22 black
// sprites). Its vertices inside the screen narrowed about the centre, those at or beyond its edges kept: the circle
// stays round in a wide picture, as the lens inside it (a textured fan narrowed as the HUD), the black reaches the
// edges. A full-screen fade or filter has no vertex inside the screen: left as drawn
bool LensMask() {
    std::vector<RunX> runs; std::vector<int> runOf;
    PackRuns(runs, runOf);
    for (const RunX& r : runs)
        if (r.x1 >= r.x0 && r.Across() && !(r.prim & 0x10) && (r.prim & 7) >= 3 && (r.prim & 7) <= 5) return true;
    return false;
}
void LensFlush(float k) {
    long long moved = 0;
    WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState& hs) {
        if (!IsXyz(reg) || !hs.OnScreen(hs.ctxt)) return;
        uint16_t x; memcpy(&x, v, 2);
        const int ox = hs.Ox(hs.ctxt);
        const float px = (x - ox) / 16.0f;
        if (px <= 1.0f || px >= 511.0f) return;                              // at an edge or beyond: kept
        x = (uint16_t)std::clamp(ox + (int)lrintf((256.0f + (px - 256.0f) * k) * 16.0f), 0, 0xffff);
        memcpy(v, &x, 2);
        ++moved;
    });
    ++gHudPacks; ++gHudMoved;
    if (gHudLog > 0 && gLog) { --gHudLog; gLog("hud: element kind %04x, %zu packets: a lens's mask, %lld vertices narrowed about the centre", gPack.kind, gPack.packets.size(), moved); }
    static bool told = false;
    if (!told && gLog) { told = true; gLog("hud: a lens's mask (element kind %04x, %zu packets) narrowed about the centre: %lld vertices", gPack.kind, gPack.packets.size(), moved); }
    for (auto& p : gPack.packets) GifPathTimed(p.path, p.bytes.data(), p.bytes.size() / 16, 0);
    gPack.packets.clear();
}

// BASE CAPTURE's gauge (GsHudQuads): the last call's quads, drawn before the font system's element of each picture of a
// match, in its first glyph's context; every register they set is put back after
std::vector<HudQuad> gHudQuads;
uint64_t gHudQuadsAt = 0;                   // the VSync count at the last call
std::mutex gHudQuadsMu;
long long gHudQuadsDrawn = 0;
void HudQuadsDraw() {
    std::vector<HudQuad> q;
    {
        std::lock_guard<std::mutex> l(gHudQuadsMu);
        if (gHudQuads.empty() || gVSyncs > gHudQuadsAt + kHudKeepVSyncs) return;
        q = gHudQuads;
    }
    int c = -1; uint64_t frame = 0, scissor = 0, ofs = 0;                   // the element's first glyph's context and state
    WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState& hs) {
        if (c >= 0 || !IsXyz(reg) || !hs.OnScreen(hs.ctxt)) return;
        c = hs.ctxt; frame = hs.frame[c]; scissor = hs.scissor[c]; ofs = hs.ofs[c];
    });
    if (c < 0) {                                                            // no text in this picture: the state the
        c = (int)((G.prim >> 9) & 1);                                       // elements before left (their frame buffer)
        frame = G.ctx[c].frame; scissor = G.ctx[c].scissor; ofs = G.ctx[c].xyoffset;
        if (!gShownFbp[Bits(frame, 0, 9)]) return;
    }
    const Context& cx = G.ctx[c];
    const int ox = (int)(ofs & 0xffff), oy = (int)((ofs >> 32) & 0xffff);
    std::vector<uint64_t> p(2);                                             // the GIFtag, then (value, register) pairs
    auto ad = [&](uint64_t v, uint64_t reg) { p.push_back(v); p.push_back(reg); };
    ad(frame, 0x4C + c); ad(scissor, 0x40 + c); ad(ofs, 0x18 + c);
    ad(1, 0x1A); ad(0, 0x49);                                               // PRMODECONT, PABE
    ad(0x30000, 0x47 + c);                                                  // TEST: no alpha test, the depth test passes
    ad(0x44, 0x42 + c);                                                     // ALPHA: (Cs - Cd) * As + Cd, as the HUD's
    ad(cx.zbuf | 1ull << 32, 0x4E + c);                                     // ZBUF: the depth not written
    auto vertex = [&](float x, float y, uint32_t rgba) {
        ad((uint64_t)rgba | 0x3f800000ull << 32, 0x01);                     // RGBAQ (Q 1.0)
        const int gx = std::clamp(ox + (int)lrintf(x * 16.0f), 0, 0xffff), gy = std::clamp(oy + (int)lrintf(y * 16.0f), 0, 0xffff);
        ad((uint64_t)gx | (uint64_t)gy << 16 | 0xffffffull << 32, 0x05);   // XYZ2
    };
    for (const HudQuad& r : q) {
        if (!(r.x1 > r.x0) || !(r.y1 > r.y0)) continue;
        ad(4 | 8 | 0x40 | (uint64_t)c << 9, 0x00);                          // PRIM: a triangle strip, gouraud, blended
        vertex(r.x0, r.y0, r.top); vertex(r.x1, r.y0, r.top); vertex(r.x0, r.y1, r.bottom); vertex(r.x1, r.y1, r.bottom);
    }
    ad(cx.test, 0x47 + c); ad(cx.alpha, 0x42 + c); ad(cx.zbuf, 0x4E + c); ad(G.pabe, 0x49); ad(G.prmodecont, 0x1A);
    ad(cx.xyoffset, 0x18 + c); ad(cx.scissor, 0x40 + c); ad(cx.frame, 0x4C + c);
    uint32_t qbits; memcpy(&qbits, &G.q, 4);
    ad((uint64_t)G.r | (uint64_t)G.g << 8 | (uint64_t)G.b << 16 | (uint64_t)G.a << 24 | (uint64_t)qbits << 32, 0x01);
    ad(G.prim, 0x00);
    const uint64_t n = p.size() / 2 - 1;
    p[0] = n | 1ull << 15 | 1ull << 60; p[1] = 0xE;                         // PACKED, NREG 1: A+D, the end
    GifPathTimed(1, (const uint8_t*)p.data(), p.size() / 2, 0);
    ++gHudQuadsDrawn;
}
void HudFlushInner();
void HudFlush() {
    if (gPack.open && (gPack.kind & 0x20) && gInMatchHud) HudQuadsDraw();  // the font system's element of a match
    HudFlushInner();
}
void HudFlushInner() {
    if (!gPack.open) return;
    gPack.open = false;
    if (!gInMatchHud && gHudSqueeze > 0 && gHudSqueeze != 1.0f) { LobbyFlush(gHudSqueeze); return; }
    // the extent of what the element draws on the screen, in pixels (vertices far off it, X 0
    // placeholders among them, left out)
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    float cx0 = 1e9f, cx1 = -1e9f, cy0 = 1e9f, cy1 = -1e9f;                // the same inside the scissor (what shows)
    WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState& hs) {
        if (!IsXyz(reg) || !hs.OnScreen(hs.ctxt)) return;
        uint16_t x, y; memcpy(&x, v, 2); memcpy(&y, v + YAt(reg), 2);
        const float px = ((int)x - hs.Ox(hs.ctxt)) / 16.0f, py = ((int)y - hs.Oy(hs.ctxt)) / 16.0f;
        if (px >= -512 && px <= 1024) { x0 = std::min(x0, px); x1 = std::max(x1, px); }
        if (py >= -448 && py <= 896) { y0 = std::min(y0, py); y1 = std::max(y1, py); }
        const uint64_t sc = hs.scissor[hs.ctxt];
        const float qx = std::clamp(px, (float)Bits(sc, 0, 11), (float)Bits(sc, 16, 11) + 1), qy = std::clamp(py, (float)Bits(sc, 32, 11), (float)Bits(sc, 48, 11) + 1);
        if (px >= -512 && px <= 1024) { cx0 = std::min(cx0, qx); cx1 = std::max(cx1, qx); }
        if (py >= -448 && py <= 896) { cy0 = std::min(cy0, qy); cy1 = std::max(cy1, qy); }
    });
    // a match's page in a wide picture, as the lobby's: its layout (an element across the screen) placed by
    // its runs, what it draws inside it (the 2D map, the players' marks) narrowed about the centre; the
    // HUD's elements left at the edges they sit by
    const bool pageIn = gInMatchHud && gMenuPage && gHudSqueeze > 0 && gHudSqueeze != 1.0f && x1 >= x0;
    // an element across the screen: the page's layout is one the match does not draw without a page; the ones
    // it draws then (filters, fades: their sizes kept) are left as drawn
    int seenLog = -1;
    if (x1 >= x0 && x0 <= 8 && x1 >= 504) {
        static uint32_t playSizes[16] = {}; static int playNext = 0;
        const uint32_t n = (uint32_t)gPack.packets.size();
        bool seen = false;
        for (uint32_t v : playSizes) seen |= v == n;
        seenLog = seen;
        if (gInMatchHud && !gMenuPage && !seen) { playSizes[playNext] = n; playNext = (playNext + 1) & 15; }
        if (pageIn && !seen) { if (gHudLog > 0 && gLog) gLog("hud: page %d seen %d n %u -> lobby", (int)gMenuPage, (int)seen, n); LobbyFlush(gHudSqueeze); return; }
        if (gInMatchHud && gHudSqueeze > 0 && gHudSqueeze != 1.0f && LensMask()) { LensFlush(gHudSqueeze); return; }
    }
    // (a page's piece judged by what its scissor lets show: the 2D map's marks outside it do not count)
    const bool inPage = pageIn && cx0 > 8 && cx1 < 504 && cy0 >= 44 && cy1 <= 440;
    // the HUD's size: an element toward the corner or edge it sits by (the thirds of the picture);
    // the whole picture's (fades, filters) and the lobby's pages as drawn
    const float s = gInMatchHud && !inPage ? gHudScale.load(std::memory_order_relaxed) : 1.0f;
    const float k = gHudSqueeze * s;
    float pivot = -1, pivotY = -1;                                          // none: as the game drew it
    if (inPage) pivot = 256.0f;
    else if (x1 >= x0 && k > 0 && k != 1.0f && !(x0 <= 8 && x1 >= 504) && x1 - x0 < 480) {
        const float cx = (x0 + x1) / 2;
        pivot = cx < 512.0f / 3 ? 0.0f : cx > 1024.0f / 3 ? 512.0f : 256.0f;
        if (s != 1.0f && y1 >= y0 && !(y0 <= 8 && y1 >= 440) && y1 - y0 < 420) {
            const float cy = (y0 + y1) / 2;
            pivotY = cy < 448.0f / 3 ? 0.0f : cy > 896.0f / 3 ? 448.0f : 224.0f;
        }
    }
    // the font system's element holds every HUD text of the picture (0x20, the platform's mark): as wide as the screen or
    // not (the name bar's text to a count at the right edge), its glyphs are placed one by one (boxes, labels), the
    // others narrowed about the centre; it never keeps its 4:3 places
    if (pivot < 0 && (gPack.kind & 0x20) && gInMatchHud && !inPage && x1 >= x0 && k > 0 && k != 1.0f) pivot = 256.0f;
    // an element across the screen without a run across it (TEAM SNEAKING's item and weapon windows in one element when an
    // item is equipped: runs 0-125 and 387-512): each run toward the edge or the third it sits by, its boxes kept with their
    // runs' pivots (drawn as is before: the windows at their 4:3 places, their texts out of them)
    std::vector<RunX> sruns; std::vector<int> sof;
    bool split = false;
    if (pivot < 0 && gInMatchHud && !gMenuPage && !inPage && x1 >= x0 && x0 <= 8 && x1 >= 504 && k > 0 && k != 1.0f) {
        PackRuns(sruns, sof);
        split = std::none_of(sruns.begin(), sruns.end(), [](const RunX& r) { return r.x1 >= r.x0 && r.Across(); });
        if (split) pivot = 256.0f;
    }
    auto runPivot = [&](const RunX& r) {
        const float rx = (r.x0 + r.x1) / 2, ry = (r.y0 + r.y1) / 2;
        return std::make_pair(rx < 512.0f / 3 ? 0.0f : rx > 1024.0f / 3 ? 512.0f : 256.0f,
                              s != 1.0f ? (ry < 448.0f / 3 ? 0.0f : ry > 896.0f / 3 ? 448.0f : 224.0f) : -1.0f);
    };
    ++gHudPacks;
    if (gHudLog < 0) gHudLog = getenv("SIM_HUD_LOG") ? atoi(getenv("SIM_HUD_LOG")) : 0;
    if (static const char* lf = getenv("SIM_HUD_LOG_FILE"); lf && (gHudPacks & 255) == 0) {   // tests: "k n" logs the next n elements
        static int seenK = 0;
        if (FILE* f = fopen(lf, "rb")) { int k = 0, n = 0; if (fscanf(f, "%d %d", &k, &n) == 2 && k > seenK) { seenK = k; gHudLog = n; } fclose(f); }
    }
    if (gHudLog > 0 && gLog) {
        --gHudLog;
        int across = 0, edge = 0, page = 0;
        std::string xs;
        if (x1 >= x0) {                                                     // its runs
            std::vector<RunX> runs; std::vector<int> runOf;
            PackRuns(runs, runOf);
            for (const RunX& r : runs) {
                if (r.x1 < r.x0) continue;
                (r.Across() ? across : r.Left() || r.Right() ? edge : page)++;
                if (xs.size() < 400) { char b[64]; snprintf(b, sizeof b, " %.0f-%.0f/%.0f-%.0f p%03x s%d", r.x0, r.x1, r.y0, r.y1, r.prim, r.sprites); xs += b; }
            }
        }
        std::string fx;                                                     // the frame buffers drawn into, the textures read (TBP PSM CBP CLD)
        std::set<uint64_t> fbs, texs;
        fbs.insert(Bits(G.ctx[0].frame, 0, 9)); fbs.insert(Bits(G.ctx[1].frame, 0, 9) | 0x1000);
        WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState&) {
            uint64_t val; if (reg == 0x4C || reg == 0x4D || reg == 0x06 || reg == 0x07) memcpy(&val, v, 8); else return;
            if (reg >= 0x4C) fbs.insert(Bits(val, 0, 9) | (reg == 0x4D ? 0x1000 : 0));
            else if (texs.size() < 6) texs.insert(Bits(val, 0, 14) | (uint64_t)Bits(val, 20, 6) << 14 | (uint64_t)Bits(val, 37, 14) << 20 | (uint64_t)Bits(val, 61, 3) << 34);
        });
        for (uint64_t f : fbs) { char b[16]; snprintf(b, sizeof b, " fb%s%x", f & 0x1000 ? "2:" : "1:", (unsigned)(f & 0xfff)); fx += b; }
        for (uint64_t t : texs) { char b[40]; snprintf(b, sizeof b, " tex %x/%x/%x/%u", (unsigned)(t & 0x3fff), (unsigned)(t >> 14 & 0x3f), (unsigned)(t >> 20 & 0x3fff), (unsigned)(t >> 34)); fx += b; }
        gLog("hud: element kind %04x, %zu packets, x %.1f-%.1f y %.1f-%.1f -> pivot %.0f (runs %d across, %d from an edge, %d inside; page %d seen %d)%s;%s", gPack.kind, gPack.packets.size(),
             x0 < 1e8f ? x0 : 0.0f, x1 > -1e8f ? x1 : 0.0f, y0 < 1e8f ? y0 : 0.0f, y1 > -1e8f ? y1 : 0.0f, pivot, across, edge, page, (int)gMenuPage, seenLog, xs.c_str(), fx.c_str());
    }
    // the boxes the HUD's elements drew last (untextured runs 24 wide and 8 high or more, where they were drawn)
    // with the pivots they took: a sprite of a later element that sits in one (a label in its box: the R2 window's
    // names and counts, drawn with the help's panel; the suppressor's count x00 on its bar, 94 x 11 above the
    // weapon's box, which drifted to the middle at 16:9 and 21:9) takes that box's pivot, so that both stay together
    // (kept by VSyncs, not by elements: a picture between and the tick's own picture find the same boxes)
    struct Box { float x0, x1, y0, y1, pivot, pivotY; uint64_t at, atPic; };
    static std::vector<Box> boxes;
    boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [](const Box& b) { return b.at + kHudKeepVSyncs < gVSyncs && b.atPic + 2 < gHudPictures; }), boxes.end());
    std::vector<std::pair<float, float>> vp;                                // each on-screen vertex's own pivot (x, y)
    if (pivot >= 0 && !inPage && gInMatchHud) {
        std::vector<std::pair<float, float>> xy; std::vector<bool> closes;    // a vertex that ends a textured sprite
        std::vector<std::pair<int, int>> offs;                                // its context's offset (the labels' places)
        uint32_t cur = 0; int half = 0;
        WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState& hs) {
            if (reg == 0x00 || reg == 0x100) { uint64_t val; memcpy(&val, v, 8); cur = (uint32_t)(val & 0x7ff); half = 0; return; }
            if (!IsXyz(reg) || !hs.OnScreen(hs.ctxt)) return;
            uint16_t x, y; memcpy(&x, v, 2); memcpy(&y, v + YAt(reg), 2);
            xy.push_back({((int)x - hs.Ox(hs.ctxt)) / 16.0f, ((int)y - hs.Oy(hs.ctxt)) / 16.0f});
            offs.push_back({hs.Ox(hs.ctxt), hs.Oy(hs.ctxt)});
            const bool sprite = (cur & 7) == 6 && (cur & 0x10);
            closes.push_back(sprite && (++half & 1) == 0);
        });
        vp.assign(xy.size(), {pivot, pivotY});
        if (split) for (size_t i = 0; i < vp.size() && i < sof.size(); ++i) if (sruns[(size_t)sof[i]].x1 >= sruns[(size_t)sof[i]].x0) vp[i] = runPivot(sruns[(size_t)sof[i]]);
        std::vector<LabelAt> labels; size_t nowLabels = 0;                 // this tick's first, then the tick's before
        // the labels' texts are the font system's (its element marked 0x20 by the platform): another element's sprites near
        // a label (the radar's disc beside BASE CAPTURE's gauge text) keep their element's pivot
        static const char* fontKindEnv = getenv("SIM_HUD_FONT_KIND");      // tests (a recording of an older program): its kind
        const bool fontElement = (gPack.kind & 0x20) || (fontKindEnv && (uint32_t)strtoul(fontKindEnv, nullptr, 16) == gPack.kind);
        if (fontElement) { std::lock_guard<std::mutex> l(gLabelsMu); if (gLabelsUses < 3 || gLabelsBatch + kHudKeepVSyncs >= gVSyncs) { labels = gLabels; nowLabels = labels.size(); labels.insert(labels.end(), gLabelsBefore.begin(), gLabelsBefore.end()); } gLabelsUsed = true; ++gLabelsUses; ++gHudPictures; }
        struct Seen { int n = 0; float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; };
        std::vector<Seen> seen(labels.size());
        // each glyph (a textured sprite) takes the nearest label within its reach (its row, from its place to the reach of
        // its text; the font's element only) and the newest of the HUD's boxes it sits in. A run of glyphs one after the
        // other with the same label is that label's text when one of them is outside every box (BASE N over the name bar's
        // end); a run wholly inside boxes is the HUD's own (the timer's digits beside BASE CAPTURE's counts, the name bar's
        // letters): each keeps its box's pivot. Tests: SIM_HUD_GLYPHS=<file>: each glyph of the pictures drawn, its VSync,
        // its place before and after, what placed it (L a label, B a box, E its element)
        static FILE* glyphLog = getenv("SIM_HUD_GLYPHS") ? fopen(getenv("SIM_HUD_GLYPHS"), "a") : nullptr;
        const bool logGlyphs = glyphLog && (GsThreadOn() ? gGsDrawNow : gGsDraw);
        std::vector<size_t> gl;                                             // the glyphs in order: their closing vertex
        std::vector<int> lab; std::vector<const Box*> box;
        for (size_t i = 1; i < xy.size(); ++i) {
            if (!closes[i]) continue;
            const float cx = (xy[i - 1].first + xy[i].first) / 2, cy = (xy[i - 1].second + xy[i].second) / 2;
            int best = -1; float bd = 1e9f;
            for (size_t l = 0; l < labels.size(); ++l) {
                const float ax = (labels[l].gx - offs[i].first) / 16.0f, ay = (labels[l].gy - offs[i].second) / 16.0f;
                const float reach = 14.0f * (float)labels[l].chars + 24.0f;
                if (cy < ay - 4 || cy > ay + 28 || cx < ax - reach || cx > ax + reach) continue;
                const float d = std::fabs(cy - (ay + 8)) + 0.05f * std::fabs(cx - ax) + (l >= nowLabels ? 6.0f : 0.0f);   // the tick's own first
                if (d < bd) { bd = d; best = (int)l; }
            }
            const Box* hit = nullptr;
            for (const Box& b : boxes)
                if (cx >= b.x0 && cx <= b.x1 && cy >= b.y0 && cy <= b.y1 && (!hit || b.at >= hit->at)) hit = &b;
            gl.push_back(i); lab.push_back(best); box.push_back(hit);
        }
        for (size_t g = 0; g < gl.size();) {
            size_t e = g + 1;                                               // the run [g, e) of one label
            while (lab[g] >= 0 && e < gl.size() && lab[e] == lab[g]) ++e;
            bool text = lab[g] >= 0;
            if (text) { text = false; for (size_t k = g; k < e; ++k) text |= box[k] == nullptr; }
            for (size_t gi = g; gi < e; ++gi) {
                const size_t i = gl[gi];
                float px = vp[i].first; char how = 'E';
                if (text) {
                    const LabelAt& la = labels[(size_t)lab[gi]];
                    const float bx = (la.gx - offs[i].first) / 16.0f, by = (la.gy - offs[i].second) / 16.0f;
                    vp[i - 1] = vp[i] = {bx, s != 1.0f ? by : -1.0f};
                    px = bx; how = 'L';
                    Seen& q = seen[(size_t)lab[gi]];
                    ++q.n; q.x0 = std::min({q.x0, xy[i - 1].first, xy[i].first}); q.x1 = std::max({q.x1, xy[i - 1].first, xy[i].first});
                    q.y0 = std::min({q.y0, xy[i - 1].second, xy[i].second}); q.y1 = std::max({q.y1, xy[i - 1].second, xy[i].second});
                } else if (box[gi]) {
                    if (box[gi]->pivot != pivot) vp[i - 1] = vp[i] = {box[gi]->pivot, box[gi]->pivotY};
                    px = box[gi]->pivot; how = 'B';
                }
                if (logGlyphs) {
                    const float cx = (xy[i - 1].first + xy[i].first) / 2, cy = (xy[i - 1].second + xy[i].second) / 2;
                    fprintf(glyphLog, "%llu %lld %.1f %.1f %.1f %c\n", (unsigned long long)gVSyncs, gHudPacks, cx, cy, px + (cx - px) * k, how);
                }
            }
            g = e;
        }
        static int labelLines = 0;
        for (size_t l = 0; l < labels.size(); ++l)
            if (seen[l].n && gLog && (gHudLog > 0 || labelLines < 3)) {
                ++labelLines;
                gLog("hud: a 3D label located at %.0f %.0f (%d characters): %d glyphs, x %.0f-%.0f y %.0f-%.0f, narrowed about its place (the element's pivot %.0f)",
                     (labels[l].gx - offs[0].first) / 16.0f, (labels[l].gy - offs[0].second) / 16.0f, labels[l].chars, seen[l].n,
                     seen[l].x0, seen[l].x1, seen[l].y0, seen[l].y1, pivot);
            }
        std::vector<RunX> runs; std::vector<int> runOf;
        PackRuns(runs, runOf);
        for (const RunX& r : runs)
            if (r.x1 - r.x0 >= 24 && r.y1 - r.y0 >= 8 && !(r.prim & 0x10) && !r.Across()) {
                const std::pair<float, float> rp = split ? runPivot(r) : std::make_pair(pivot, pivotY);
                auto same = std::find_if(boxes.begin(), boxes.end(), [&](const Box& b) {
                    return b.x0 == r.x0 && b.x1 == r.x1 && b.y0 == r.y0 && b.y1 == r.y1 && b.pivot == rp.first && b.pivotY == rp.second; });
                if (same != boxes.end()) { same->at = gVSyncs; same->atPic = gHudPictures; }
                else if (boxes.size() < 256) boxes.push_back({r.x0, r.x1, r.y0, r.y1, rp.first, rp.second, gVSyncs, gHudPictures});
            }
    }
    if (pivot >= 0) {
        ++gHudMoved;
        size_t vi = 0;
        WalkPack(gPack, [&](uint32_t reg, uint8_t* v, const HudState& hs) {
            if (IsXyz(reg)) {
                if (!hs.OnScreen(hs.ctxt)) return;
                const float px0 = vi < vp.size() ? vp[vi].first : pivot, py0 = vi < vp.size() ? vp[vi].second : pivotY;
                ++vi;
                uint16_t x; memcpy(&x, v, 2);
                const int ox = hs.Ox(hs.ctxt);
                const float px = (x - ox) / 16.0f;
                if (px < -512 || px > 1024) return;
                const int nx = ox + (int)lrintf((px0 + (px - px0) * k) * 16.0f);
                x = (uint16_t)std::clamp(nx, 0, 0xffff);
                memcpy(v, &x, 2);
                if (py0 >= 0) {
                    uint16_t y; memcpy(&y, v + YAt(reg), 2);
                    const int oy = hs.Oy(hs.ctxt);
                    const float py = (y - oy) / 16.0f;
                    const int ny = oy + (int)lrintf((py0 + (py - py0) * s) * 16.0f);
                    y = (uint16_t)std::clamp(ny, 0, 0xffff);
                    memcpy(v + YAt(reg), &y, 2);
                }
            } else if ((reg == 0x40 || reg == 0x41) && hs.OnScreen(reg - 0x40)) {   // SCISSOR: its columns too
                uint64_t sc; memcpy(&sc, v, 8);
                const float a = (float)Bits(sc, 0, 11), b = (float)Bits(sc, 16, 11) + 1;
                if (b - a >= 480) return;                                   // the screen's kept: it stays set for the next elements
                float sp = pivot;                                           // a split element's: its third's
                if (split) { const float c = (a + b) / 2; sp = c < 512.0f / 3 ? 0.0f : c > 1024.0f / 3 ? 512.0f : 256.0f; }
                const int na = std::clamp((int)floorf(sp + (a - sp) * k), 0, 2047);
                const int nb = std::clamp((int)ceilf(sp + (b - sp) * k) - 1, na, 2047);
                sc = (sc & ~0x7ff07ffull) | (uint64_t)na | (uint64_t)nb << 16;
                if (pivotY >= 0) {
                    const float c0 = (float)Bits(sc, 32, 11), c1 = (float)Bits(sc, 48, 11) + 1;
                    const int nc = std::clamp((int)floorf(pivotY + (c0 - pivotY) * s), 0, 2047);
                    const int nd = std::clamp((int)ceilf(pivotY + (c1 - pivotY) * s) - 1, nc, 2047);
                    sc = (sc & ~(0x7ffull << 32 | 0x7ffull << 48)) | (uint64_t)nc << 32 | (uint64_t)nd << 48;
                }
                memcpy(v, &sc, 8);
            }
        });
    }
    for (auto& p : gPack.packets) GifPathTimed(p.path, p.bytes.data(), p.bytes.size() / 16, 0);
    gPack.packets.clear();
}
}  // namespace

// the platform's settings of the 2D elements: in order with the packets they are for (on the GS thread when it runs)
namespace {
void SetHudCall(const uint8_t* a) { if (gGsqOn) GsqPut(kGsqHud, a, 5); float q; bool m; memcpy(&q, a, 4); memcpy(&m, a + 4, 1); gHudSqueeze = q; gInMatchHud = m; }
void LabelAtCall(const uint8_t* a) {
    if (gGsqOn) GsqPut(kGsqLabel, a, 12);
    int v[3]; memcpy(v, a, sizeof v);
    std::lock_guard<std::mutex> l(gLabelsMu);
    if (gLabelsUsed) { gLabelsBefore.swap(gLabels); gLabels.clear(); gLabelsUsed = false; gLabelsUses = 0; gLabelsBatch = gVSyncs; }   // a newer tick's labels
    const LabelAt n{(v[0] + 0x700) * 16, (v[1] + 0x720) * 16, std::max(1, v[2]), gVSyncs};
    for (const LabelAt& e : gLabels) if (e.gx == n.gx && e.gy == n.gy && e.chars == n.chars) return;
    if (gLabels.size() < 64) gLabels.push_back(n);
}
void MenuPageCall(const uint8_t* a) { if (gGsqOn) GsqPut(kGsqMenuPage, a, 1); gMenuPage = a[0] != 0; }
void HudQuadsCall(const uint8_t* a) {        // [n][n HudQuads]
    uint32_t n; memcpy(&n, a, 4);
    if (gGsqOn) GsqPut(kGsqQuads, a, 4 + n * sizeof(HudQuad));
    std::lock_guard<std::mutex> l(gHudQuadsMu);
    gHudQuads.assign(n, HudQuad{});
    if (n) memcpy(gHudQuads.data(), a + 4, n * sizeof(HudQuad));
    gHudQuadsAt = gVSyncs;
}
bool Queued() { return GsThreadOn() && !GsOnThread(); }
}  // namespace
void GsSetHud(float squeeze, bool match) {
    uint8_t a[5]; memcpy(a, &squeeze, 4); memcpy(a + 4, &match, 1);
    if (Queued()) GsQueueCall(&SetHudCall, a, sizeof a); else SetHudCall(a);
}
void GsLabelAt(int x, int y, int chars) {
    const int v[3] = {x, y, chars};
    if (Queued()) GsQueueCall(&LabelAtCall, v, sizeof v); else LabelAtCall((const uint8_t*)v);
}
void GsHudQuads(const HudQuad* q, int n) {
    n = std::clamp(n, 0, 32);
    uint8_t a[4 + 32 * sizeof(HudQuad)];
    const uint32_t m = (uint32_t)n;
    memcpy(a, &m, 4);
    if (n) memcpy(a + 4, q, n * sizeof(HudQuad));
    if (Queued()) GsQueueCall(&HudQuadsCall, a, 4 + n * sizeof(HudQuad)); else HudQuadsCall(a);
}
void GsSetMenuPage(bool on) { const uint8_t a = on; if (Queued()) GsQueueCall(&MenuPageCall, &a, 1); else MenuPageCall(&a); }
void GsSetHdTextures(bool on) {
    GsSync();
    if (on == gHdOn) return;
    if (gGsqOn) { const uint8_t b = on; GsqPut(kGsqHd, &b, 1); }
    gHdOn = on;
    gIdPages.Clear(); ++gIdSerial; gIdLo = 512; gIdHi = 0;   // what was uploaded before is not known
}
std::string GsHdStats() {
    char b[160];
    snprintf(b, sizeof b, "%llu texture states keyed (%.1f ms), %llu without a key; ", (unsigned long long)sHdKeyed.exchange(0),
             sHdNs.exchange(0) / 1e6, (unsigned long long)sHdNone.exchange(0));
    return b + hdtex::Stats();
}
float GsHudScale() { return gHudScale.load(std::memory_order_relaxed); }
void GsSetHudScale(float scale) { gHudScale.store(std::clamp(scale, 0.4f, 1.0f), std::memory_order_relaxed); }
void NoteShown(uint64_t dispfb) { gShownFbp[Bits(dispfb, 0, 9)] = true; }
// the GS thread's two stages (GsThreadStart(2)): on the first one a GIF packet or a GS call goes to the second, in order
bool GsOnFirst();
void GsFwdGif(int path, const uint8_t* data, size_t qwords);
void GsFwdCall(void (*fn)(const uint8_t* arg), const void* arg, size_t n);
namespace { void PackMarkCall(const uint8_t* a) { uint32_t imm; memcpy(&imm, a, 4); GsPackMark(imm); } }
// a MARK of the platform's (0x5Axx): 0x5AFF ends an element, the others start one (the low
// byte: the drawing phase, 0x10 a precompiled element)
void GsPackMark(uint32_t imm) {
    if (gHeadless) return;
    if (GsOnFirst()) { GsFwdCall(&PackMarkCall, &imm, 4); return; }
    if (gDump) { const uint32_t h[2] = {3, imm}; fwrite(h, 4, 2, gDump); }
    HudFlush();
    if ((imm & 0xff) != 0xff) { gPack.open = true; gPack.kind = imm & 0xffff; gPack.p2 = gGif[2]; }
}
std::string GsHudStats() {
    GsSync();
    char b[128]; snprintf(b, sizeof b, "%lld 2D elements, %lld narrowed, %lld gauge draws", gHudPacks, gHudMoved, gHudQuadsDrawn);
    gHudPacks = gHudMoved = gHudQuadsDrawn = 0;
    return b;
}

bool gGsTiming = getenv("SIM_THREAD_STATS") || getenv("MGO1_GS_TIMING");   // the VU1 and GIF times of the stats (two clock reads a call)
void GifPath(int path, const uint8_t* data, size_t qwords, size_t wrapBytes) {
    if (gHeadless) {                                                        // no PATH1 (VU1 does not run), no 2D element held
        if (path != 1) GifPathTimed(path, data, qwords, 0);
        return;
    }
    if (gGsqOn && path == 3) GsqPut(kGsqGif, data, qwords * 16);           // the bench's recording (gs_thread.h)
    const auto t0 = gGsTiming ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
    // PATH1 (XGKICK): data is VU1 memory, qwords the start quadword, wrapBytes its size: the
    // packet is read from there, wrapping at the end of the memory, up to its EOP tag (its tags
    // read in place; only a packet that wraps is copied)
    std::vector<uint8_t> linear;
    if (path == 1 && wrapBytes) {
        const size_t total = wrapBytes / 16, start = qwords % total;
        size_t q = 0;
        while (q < total) {
            uint64_t lo; memcpy(&lo, data + (start + q) % total * 16, 8);
            const uint32_t nloop = Bits(lo, 0, 15), eop = Bits(lo, 15, 1), flg = Bits(lo, 58, 2);
            uint32_t nreg = Bits(lo, 60, 4); if (!nreg) nreg = 16;
            ++q;
            const size_t n = flg == 0 ? (size_t)nloop * nreg : flg == 1 ? ((size_t)nloop * nreg + 1) / 2 : nloop;
            q = std::min(total, q + n);
            if (eop) break;
        }
        if (start + q <= total) data += start * 16;
        else {
            linear.resize(q * 16);
            memcpy(linear.data(), data + start * 16, (total - start) * 16);
            memcpy(linear.data() + (total - start) * 16, data, (start + q - total) * 16);
            data = linear.data();
        }
        qwords = q;
    } else if (path == 1) qwords = GifPacketQw(data, qwords, true);
    if (GsOnFirst()) { GsFwdGif(path, data, qwords); return; }             // drawn on the second stage
    if (gDump) {                                                            // the packet as the game sent it
        const uint32_t h[3] = {1, (uint32_t)path, (uint32_t)qwords};
        fwrite(h, 4, 3, gDump); fwrite(data, 16, qwords, gDump);
    }
    if (gPack.open && (path == 1 || path == 2)) gPack.packets.push_back({path, std::vector<uint8_t>(data, data + qwords * 16)});
    else GifPathTimed(path, data, qwords, 0);
    if (PoolLive()) Publish(*gPool);                                        // the workers start on this packet
    if (gGsTiming) gGifNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}
void GifPathTimed(int path, const uint8_t* data, size_t qwords, size_t wrapBytes) {
    (void)wrapBytes;                                                        // GifPath made PATH1 linear
    ++gPathKicks[path & 3];
    gGifPathNow = path & 3; ++gGifPacketNow;
    GifPathState& st = gGif[path & 3];
    if (path == 1) st.active = false;                                       // a kick starts on a tag
    size_t q = 0;
    auto qw = [&](size_t i, uint64_t& lo, uint64_t& hi) { memcpy(&lo, data + i * 16, 8); memcpy(&hi, data + i * 16 + 8, 8); };
    bool done = false;
    for (int guard = 0; guard < 1 << 20 && q < qwords && !done; ++guard) {
        if (!st.active) {
            uint64_t lo, hi;
            qw(q++, lo, hi);
            ++gGifTags;
            st.tagLo = lo; st.tagHi = hi;
            st.nloop = Bits(lo, 0, 15); st.eop = Bits(lo, 15, 1); st.flg = Bits(lo, 58, 2);
            st.nreg = Bits(lo, 60, 4); if (!st.nreg) st.nreg = 16;
            st.loop = 0; st.reg = 0;
            const uint32_t pre = Bits(lo, 46, 1);
            if (gGsTrace > 0) GsTraceLog("gif path%d tag: nloop %u eop %u pre %u prim %03x flg %u nreg %u regs %016llx",
                                         path, st.nloop, st.eop, pre, (unsigned)Bits(lo, 47, 11), st.flg, st.nreg, (unsigned long long)hi);
            if (pre && st.flg == 0 && !gHeadless) WriteReg(0x00, Bits(lo, 47, 11));
            st.active = st.nloop > 0;
            if (!st.active) { if (st.eop && path == 1) done = true; continue; }
        }
        if (st.flg == 0) {                                                  // PACKED: nreg quadwords a loop
            while (st.loop < st.nloop && q < qwords) {
                uint64_t a, b; qw(q++, a, b);
                const uint32_t r = (uint32_t)(st.tagHi >> (4 * st.reg)) & 15;
                if (!gHeadless) PackedReg(r, a, b);
                else if (r == 0xe && (b & 0xff) >= 0x60 && (b & 0xff) <= 0x62) WriteReg((uint32_t)(b & 0xff), a);   // A+D SIGNAL / FINISH / LABEL
                if (++st.reg >= st.nreg) { st.reg = 0; ++st.loop; }
            }
        } else if (st.flg == 1) {                                           // REGLIST: two registers a quadword
            const uint64_t total = (uint64_t)st.nloop * st.nreg;
            while (st.loop < total && q < qwords) {                         // loop counts registers here
                uint64_t a, b; qw(q++, a, b);
                const uint32_t r0 = (uint32_t)(st.tagHi >> (4 * (st.loop % st.nreg))) & 15;
                if (r0 != 0xe && r0 != 0xf && !gHeadless) WriteReg(r0, a);
                ++st.loop;
                if (st.loop < total) {
                    const uint32_t r1 = (uint32_t)(st.tagHi >> (4 * (st.loop % st.nreg))) & 15;
                    if (r1 != 0xe && r1 != 0xf && !gHeadless) WriteReg(r1, b);
                    ++st.loop;
                }
            }
            if (st.loop >= total) st.loop = st.nloop;                       // the odd last half is padding
        } else {                                                            // IMAGE: the run of quadwords at once
            const size_t take = std::min<size_t>(st.nloop - st.loop, qwords - q);
            if (!gHeadless) ImageData(data + q * 16, take * 16);            // (headless: the image is not kept)
            q += take; st.loop += (uint32_t)take; gGifImageQw += take;
        }
        if (st.loop >= st.nloop) {
            st.active = false;
            if (st.eop && path == 1) done = true;                           // a kick ends with its EOP tag
        }
    }
    gPathQw[path & 3] += q;
}

// the privileged registers: the events (CSR, IMR, SIGLBLID) at once on the caller's side; the display circuits'
// registers (and a dump's record of every write) where the GS works: on the GS thread, in order with the packets
namespace {
void PrivDisplay(uint32_t addr, uint64_t v) {
    if (gGsqOn) { const uint64_t r[2] = {addr, v}; GsqPut(kGsqDisplay, r, 16); }
    if (gDump) { const uint32_t h[2] = {2, addr}; fwrite(h, 4, 2, gDump); fwrite(&v, 8, 1, gDump); }
    switch (addr & 0x1ff0) {
    case 0x0000: G.pmode = v; return;
    case 0x0020: G.smode2 = v; return;
    case 0x0070: G.dispfb[0] = v; gShownFbp[Bits(v, 0, 9)] = true; return;
    case 0x0080: G.display[0] = v; return;
    case 0x0090: G.dispfb[1] = v; gShownFbp[Bits(v, 0, 9)] = true; return;
    case 0x00a0: G.display[1] = v; return;
    case 0x00e0: G.bgcolor = v; return;
    }
}
void PrivDisplayCall(const uint8_t* a) { uint32_t addr; uint64_t v; memcpy(&addr, a, 4); memcpy(&v, a + 4, 8); PrivDisplay(addr, v); }
void GsqVSync();                                                              // the bench's recording: its start and end
void VSyncWork() { HudFlush(); ++gVSyncs; GsqVSync(); }                       // the GS's side of a VSync
}  // namespace
void GsQueueVSync();                                        // the GS thread's part of a VSync, queued
void GsPrivWrite(uint32_t addr, uint64_t v, int size) {
    (void)size;
    switch (addr & 0x1ff0) {
    case 0x0070: case 0x0090: gDispfbChanged = true; break;                  // a new picture this field (GsDisplayFlips)
    case 0x1000: {                                                          // CSR: writing 1 clears the events; RESET
        std::lock_guard<std::mutex> l(gEvMu);
        G.csr &= ~(v & 0x1f);
        if (v & 2) {                                                        // FINISH cleared: the next one queued raises it again
            if (gFinishEvents > 0) --gFinishEvents;
            if (gFinishEvents > 0) { G.csr |= 2; if (!(G.imr & 0x200)) RaiseGsIrq(); }
        }
        if (v & 0x200) { G.csr = 0; gFinishEvents = 0; }
        break; }
    case 0x1010: { std::lock_guard<std::mutex> l(gEvMu); G.imr = v; break; }
    case 0x1080: { std::lock_guard<std::mutex> l(gEvMu); G.siglblid = v; break; }
    }
    if (GsThreadOn() && !GsOnThread()) {
        uint8_t a[12]; memcpy(a, &addr, 4); memcpy(a + 4, &v, 8);
        GsQueueCall(&PrivDisplayCall, a, sizeof a);
    } else PrivDisplay(addr, v);
}
uint64_t GsPrivRead(uint32_t addr, int size) {
    (void)size;
    switch (addr & 0x1ff0) {
    case 0x1000: {                                                          // CSR: events, FIFO empty, the field
        std::lock_guard<std::mutex> l(gEvMu);
        static uint32_t field = 0;
        if (gFieldByVSync) field = gField ? 0x2000 : 0;                    // native: the field of the last VSync
        else field ^= 0x2000;                                               // else it toggles at each read
        return (G.csr & 0x1f) | 0x4000 /* FIFO empty */ | field | 0x1b000000 /* revision */; }
    case 0x1080: { std::lock_guard<std::mutex> l(gEvMu); return G.siglblid; }
    case 0x1010: { std::lock_guard<std::mutex> l(gEvMu); return G.imr; }
    }
    GsSync();                                                               // the display circuits' registers as the GS has them
    switch (addr & 0x1ff0) {
    case 0x0000: return G.pmode;
    case 0x0020: return G.smode2;
    case 0x0070: return G.dispfb[0];
    case 0x0080: return G.display[0];
    case 0x0090: return G.dispfb[1];
    case 0x00a0: return G.display[1];
    case 0x00e0: return G.bgcolor;
    }
    return 0;
}

namespace { void SetCrtCall(const uint8_t* a) { if (gGsqOn) GsqPut(kGsqCrt, a, 12); int v[3]; memcpy(v, a, sizeof v); G.interlace = v[0]; G.mode = v[1]; G.ffmd = v[2]; } }
void GsSetCrt(int interlace, int mode, int ffmd) {
    const int v[3] = {interlace, mode, ffmd};
    if (GsThreadOn() && !GsOnThread()) GsQueueCall(&SetCrtCall, v, sizeof v); else SetCrtCall((const uint8_t*)v);
}
void GsVSync() {                                                            // a VSync: VSINT raised, the next field
    if (GsThreadOn() && !GsOnThread()) { GsQueueVSync(); GsQueueSubmit(); }
    else VSyncWork();
    if (gDispfbChanged) { gDisplayFlips.fetch_add(1); gDispfbChanged = false; }   // one new picture a field at most (PAL sets both circuits)
    std::lock_guard<std::mutex> l(gEvMu);
    gFieldByVSync = true; gField ^= 1;
    G.csr |= 8;
    if (!(G.imr & 0x800)) RaiseGsIrq();                                     // VSMSK
}

uint64_t GsDisplayFlips() { return gDisplayFlips.load(); }

std::string GsPcrtc() {
    char b[200];
    snprintf(b, sizeof b, "PMODE %llx DISPFB1 %llx DISPLAY1 %llx DISPFB2 %llx DISPLAY2 %llx BGCOLOR %llx",
             (unsigned long long)G.pmode, (unsigned long long)G.dispfb[0], (unsigned long long)G.display[0],
             (unsigned long long)G.dispfb[1], (unsigned long long)G.display[1], (unsigned long long)G.bgcolor);
    return b;
}

bool GsDisplayed(std::vector<uint32_t>& rgba, int& w, int& h) {
    // the two read circuits (PMODE EN1 / EN2), each placed by its DISPLAY (DX / DY over MAGH /
    // MAGV), merged as the PCRTC does: circuit 1 over circuit 2 (or BGCOLOR when SLBG) with
    // alpha ALP (MMOD 1) or circuit 1's pixel alpha (MMOD 0, 0x80 = 1.0)
    GsSync();
    DrainAll();
    struct Circ { bool on; uint32_t fbp, fbw, psm; int dbx, dby, ox, oy, w, h; } c[2];
    for (int k = 0; k < 2; ++k) {
        const uint64_t fb = G.dispfb[k], dp = G.display[k];
        c[k].on = (G.pmode >> k) & 1;
        c[k].fbp = Bits(fb, 0, 9) * 32; c[k].fbw = Bits(fb, 9, 6); c[k].psm = Bits(fb, 15, 5);
        c[k].dbx = Bits(fb, 32, 11); c[k].dby = Bits(fb, 43, 11);
        const int magh = Bits(dp, 23, 4) + 1, magv = Bits(dp, 27, 2) + 1;
        c[k].ox = Bits(dp, 0, 12) / magh; c[k].oy = Bits(dp, 12, 11) / magv;
        c[k].w = (Bits(dp, 32, 12) + 1) / magh; c[k].h = (Bits(dp, 44, 11) + 1) / magv;
        if (c[k].on && (!c[k].fbw || c[k].w <= 0 || c[k].h <= 0 || c[k].w > 2048 || c[k].h > 2048)) c[k].on = false;
    }
    if (!c[0].on && !c[1].on) {                                              // nothing enabled: circuit 1 as set, if it is
        const uint64_t dp = G.display[0];
        if (!Bits(G.dispfb[0], 9, 6) || !Bits(dp, 32, 12)) return false;
        c[0].on = true;
    }
    int x0 = 1 << 20, y0 = 1 << 20, x1 = 0, y1 = 0;
    for (auto& k : c) if (k.on) { x0 = std::min(x0, k.ox); y0 = std::min(y0, k.oy); x1 = std::max(x1, k.ox + k.w); y1 = std::max(y1, k.oy + k.h); }
    // the picture of the first enabled circuit's size (the other one offset against it)
    const Circ& ref = c[0].on ? c[0] : c[1];
    w = ref.w; h = ref.h;
    if (w <= 0 || h <= 0) return false;
    rgba.assign((size_t)w * h, 0);
    const bool mmod = Bits(G.pmode, 5, 1) != 0, slbg = Bits(G.pmode, 7, 1) != 0;
    const int alp = Bits(G.pmode, 8, 8);
    const uint32_t bg = (uint32_t)(G.bgcolor & 0xffffff);
    auto sample = [&](const Circ& k, int sx, int sy, bool& inside) -> uint32_t {
        inside = sx >= 0 && sy >= 0 && sx < k.w && sy < k.h;
        if (!inside) return 0;
        const uint32_t v = ReadPixel(k.psm, k.dbx + sx, k.dby + sy, k.fbp, k.fbw);
        const bool p16 = Family(k.psm) == 1 || Family(k.psm) == 2;
        if (p16) return Expand16(v, false);
        return (k.psm & 0x3f) == PSMCT24 ? (v & 0xffffff) | 0x80000000u : v;
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int X = x + ref.ox, Y = y + ref.oy;                        // screen position
            bool in1 = false, in2 = false;
            const uint32_t p1 = c[0].on ? sample(c[0], X - c[0].ox, Y - c[0].oy, in1) : 0;
            const uint32_t p2 = c[1].on ? sample(c[1], X - c[1].ox, Y - c[1].oy, in2) : 0;
            const uint32_t back = slbg ? bg : (in2 ? p2 : 0);
            uint32_t out;
            if (in1) {
                const int a = mmod ? alp : std::min(255, (int)(p1 >> 24) * 2);
                out = 0;
                for (int s = 0; s < 24; s += 8) {
                    const int v = (((p1 >> s) & 255) * a + ((back >> s) & 255) * (255 - a) + 127) / 255;
                    out |= (uint32_t)std::min(v, 255) << s;
                }
            } else out = back & 0xffffff;
            rgba[(size_t)y * w + x] = out | 0xff000000u;
        }
    return true;
}

namespace {
uint32_t Crc(const uint8_t* p, size_t n, uint32_t c = 0xffffffffu) {
    static uint32_t t[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t x = i; for (int k = 0; k < 8; ++k) x = x & 1 ? 0xedb88320u ^ (x >> 1) : x >> 1; t[i] = x; } init = true; }
    for (size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
void Be32(std::vector<uint8_t>& o, uint32_t v) { o.push_back((uint8_t)(v >> 24)); o.push_back((uint8_t)(v >> 16)); o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); }
void Chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& d) {
    Be32(o, (uint32_t)d.size());
    std::vector<uint8_t> c(type, type + 4); c.insert(c.end(), d.begin(), d.end());
    o.insert(o.end(), c.begin(), c.end());
    Be32(o, Crc(c.data(), c.size()) ^ 0xffffffffu);
}
}  // namespace

std::vector<uint32_t> GsFrameRgba(uint64_t frame, int w, int h) {
    GsSync();
    DrainAll();
    const uint32_t fbp = Bits(frame, 0, 9) * 32, fbw = Bits(frame, 16, 6), psm = Bits(frame, 24, 6);
    std::vector<uint32_t> px((size_t)w * h);
    const bool p16 = Family(psm) == 1 || Family(psm) == 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t v = ReadPixel(psm, x, y, fbp, fbw ? fbw : 1);
            px[(size_t)y * w + x] = p16 ? Expand16(v, false) : v;
        }
    return px;
}
bool GsWritePng(const char* path) {
    std::vector<uint32_t> px; int w = 0, h = 0;
    if (!GsDisplayed(px, w, h)) return false;
    return GsPngOf(path, px, w, h);
}
bool GsPngOf(const char* path, const std::vector<uint32_t>& px, int w, int h) {
    // a PNG with stored (uncompressed) deflate blocks: RGB, one filter byte per line
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (1 + 3 * w));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) { const uint32_t c = px[(size_t)y * w + x]; raw.push_back((uint8_t)c); raw.push_back((uint8_t)(c >> 8)); raw.push_back((uint8_t)(c >> 16)); }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0; uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) { s1 = (s1 + b) % 65521; s2 = (s2 + s1) % 65521; }
    while (pos < raw.size() || raw.empty()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)n); z.push_back((uint8_t)(n >> 8)); z.push_back((uint8_t)~n); z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + (long)pos, raw.begin() + (long)(pos + n));
        pos += n;
        if (raw.empty()) break;
    }
    Be32(z, (s2 << 16) | s1);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr; Be32(ihdr, (uint32_t)w); Be32(ihdr, (uint32_t)h); ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(out, "IHDR", ihdr); Chunk(out, "IDAT", z); Chunk(out, "IEND", {});
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f); fclose(f);
    return true;
}
// counters for the logs
uint64_t GsPrims() { return gPrims; }
uint64_t GsTags() { return gGifTags; }
extern uint64_t gVu1Ns, gVu1Runs;
std::string GsStats() {
    // the GS work since the last call: kicks and quadwords per path, primitives, image quadwords, time
    GsSync();
    char b[300];
    snprintf(b, sizeof b, "path1 %llu kicks %llu qw, path2 %llu / %llu, path3 %llu / %llu, %llu prims, %llu image qw; VU1 %llu runs %.0f ms, GIF+GS %.0f ms",
             (unsigned long long)gPathKicks[1], (unsigned long long)gPathQw[1], (unsigned long long)gPathKicks[2], (unsigned long long)gPathQw[2],
             (unsigned long long)gPathKicks[3], (unsigned long long)gPathQw[3], (unsigned long long)gPrims, (unsigned long long)gGifImageQw,
             (unsigned long long)gVu1Runs, gVu1Ns / 1e6, gGifNs / 1e6);
    for (int k = 0; k < 4; ++k) gPathKicks[k] = gPathQw[k] = 0;
    gPrims = 0; gGifImageQw = 0; gVu1Ns = gVu1Runs = 0; gGifNs = 0;
    std::string out = b;
    {   // the events the game can see back, by the path that brought them
        char e[160];
        snprintf(e, sizeof e, "; SIGNAL/FINISH/LABEL path1 %llu/%llu/%llu path2 %llu/%llu/%llu path3 %llu/%llu/%llu%s",
                 (unsigned long long)gGsEvents[1][0], (unsigned long long)gGsEvents[1][1], (unsigned long long)gGsEvents[1][2],
                 (unsigned long long)gGsEvents[2][0], (unsigned long long)gGsEvents[2][1], (unsigned long long)gGsEvents[2][2],
                 (unsigned long long)gGsEvents[3][0], (unsigned long long)gGsEvents[3][1], (unsigned long long)gGsEvents[3][2],
                 gHeadless ? " (headless)" : "");
        out += e;
        memset(gGsEvents, 0, sizeof gGsEvents);
    }
    if (PoolLive()) {
        Pool& P = *gPool;
        char r[160];
        snprintf(r, sizeof r, "; raster %d threads, %llu drains, %llu drawn here (%.0f ms), %.0f ms waited",
                 P.n, (unsigned long long)P.drains, (unsigned long long)P.serial, P.hereNs / 1e6, P.waitNs / 1e6);
        out += r;
        P.drains = P.serial = P.waitNs = P.hereNs = 0;
    }
    return out;
}
void GsSetThreads(int n) { GsSync(); if (!gPool) gThreadsWanted = n; }

// the block and column tables as rows of 32 (gs_gpu.cpp): kBlock32 (rows 0-3), kBlock16 (4-11),
// kBlock16S (12-19), kColumn32 (20-27), kColumn16 (28-35), kColumn8 (36-51), kColumn4 (52-67)
void GsSwizzleTables(uint16_t lut[68][32]) {
    memset(lut, 0, 68 * 32 * sizeof(uint16_t));
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) lut[y][x] = kBlock32[y][x];
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 4; ++x) { lut[4 + y][x] = kBlock16[y][x]; lut[12 + y][x] = kBlock16S[y][x]; }
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) lut[20 + y][x] = kColumn32[y][x];
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) lut[28 + y][x] = kColumn16[y][x];
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) lut[36 + y][x] = kColumn8[y][x];
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) lut[52 + y][x] = kColumn4[y][x];
}
// the GPU renderer: started before the game draws (the CPU copy of the memory is its first state)
bool GsGpuStart(const gsgpu::Host& host, int scale, std::string& error) {
    GsSync();
    DrainAll();
    return gsgpu::Start(host, scale, gVram, error);
}
bool GsGpuOn() { return gsgpu::On(); }
// a picture shown: on the GS thread when it runs, after what it has to draw first
namespace { void PresentCall(const uint8_t*) { if (gGsPresented && GsOnThread()) gGsPresented(); gsgpu::Present(G.pmode, G.dispfb, G.display); } }
void GsGpuPresent() {
    if (GsThreadOn() && !GsOnThread()) { GsQueueCall(&PresentCall, nullptr, 0); GsQueueSubmit(); }
    else PresentCall(nullptr);
}
bool GsGpuPicture(std::vector<uint32_t>& rgba, int& w, int& h) { GsSync(); return gsgpu::Picture(G.pmode, G.dispfb, G.display, rgba, w, h); }
std::string GsGpuStats() { return gsgpu::Stats(); }
// the local memory as the CPU would read it now (tests: what the GPU drew there first)
bool GsMemoryFile(const char* path) {
    GsSync();
    if (gsgpu::On()) { uint64_t all[8]; memset(all, 0xff, sizeof all); gsgpu::CpuWillRead(all); }
    DrainAll();
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(gVram, sizeof gVram, 1, f) == 1;
    fclose(f);
    return ok;
}

// ---- the GS thread (gs_thread.h): the game thread fills a batch of items (a head {kind, size}, then the data,
// the next head 8-aligned) and hands it over at a DMA start's end, at a VSync, at a picture shown; the GS thread
// runs the batches in order. At most three VSyncs or 256 MB ahead of it: the game thread waits there.
namespace {
enum : uint32_t { kQVif1 = 1, kQGif = 2, kQVu1 = 3, kQCall = 4, kQDraw = 5, kQVSync = 6, kQGif1 = 7, kQGif2 = 8, kQCallAll = 9 };
struct QHead { uint32_t kind, size; };
struct QBuf {                                               // a batch: its bytes are not cleared (the data fills them)
    uint8_t* p = nullptr; size_t n = 0, cap = 0;
    QBuf() = default;
    QBuf(QBuf&& o) noexcept : p(o.p), n(o.n), cap(o.cap) { o.p = nullptr; o.n = o.cap = 0; }
    QBuf& operator=(QBuf&& o) noexcept { if (this != &o) { free(p); p = o.p; n = o.n; cap = o.cap; o.p = nullptr; o.n = o.cap = 0; } return *this; }
    QBuf(const QBuf&) = delete;
    ~QBuf() { free(p); }
    uint8_t* Grow(size_t more) {
        if (n + more > cap) {
            size_t c = std::max<size_t>(cap * 2, n + more + (1u << 20));
            uint8_t* q = (uint8_t*)realloc(p, c);
            if (!q) abort();
            p = q; cap = c;
        }
        uint8_t* at = p + n; n += more; return at;
    }
};
struct Queue {
    std::mutex mu;
    std::condition_variable work, idle;
    std::deque<QBuf> handed;                                // batches the GS thread has not taken yet
    std::vector<QBuf> spare;                                // batches done, their memory kept
    uint64_t submitted = 0, finished = 0;                   // batches (under mu)
    size_t pendingBytes = 0; int pendingVSyncs = 0;         // handed over, not done (under mu)
    QBuf open;                                              // the game thread's batch being filled
    int openVSyncs = 0;
    bool lastDraw = true;
    uint64_t batches = 0, bytes = 0, syncs = 0, syncWaits = 0, syncNs = 0, fullWaits = 0, fullNs = 0;   // statistics
} gQ, gQ2;                                                  // gQ2: the second stage's (filled by the first)
std::atomic<bool> gQOn{false};
int gStages = 1;                                            // 2: VIF1 and VU1 on the GS thread, the GIF and the GS on a second one
// the caller's part, set by each GS thread at its start: 1 the GS thread (its first stage), 2 the second stage, 0 another
// (not std::thread::id: MinGW gives two detached threads the same one, and the second stage took itself for the first)
thread_local int tGsStage = 0;
constexpr size_t kStage2Batch = 256u << 10;                 // the second stage gets the first's work in runs of 256 KB
}  // namespace
void* gGsThreadHandle = nullptr;                             // the GS thread's (Windows), for the profiler
void* gGsThread2Handle = nullptr;                            // the second stage's (perf.h)
namespace {
uint8_t* QPutTo(Queue& q, uint32_t kind, size_t size) {
    const size_t pad = (size + 7) & ~(size_t)7;
    uint8_t* at = q.open.Grow(sizeof(QHead) + pad);
    const QHead h{kind, (uint32_t)size};
    memcpy(at, &h, sizeof h);
    return at + sizeof h;
}
uint8_t* QPut(uint32_t kind, size_t size) { return QPutTo(gQ, kind, size); }
void QDrawFlag() {                                          // gGsDraw as it is for what follows
    if (gGsDraw == gQ.lastDraw) return;
    gQ.lastDraw = gGsDraw;
    *QPut(kQDraw, 1) = gGsDraw ? 1 : 0;
}
void Vu1Store(uint32_t a, uint64_t v, int size) {
    if (gGsqOn) { const uint64_t r[2] = {(uint64_t)a | (uint64_t)(uint32_t)size << 32, v}; GsqPut(kGsqVu1, r, 16); }
    memcpy((a & 0x4000 ? gVu1.mem : gVu1.micro) + (a & (0x4000 - size)), &v, (size_t)size);
    if (!(a & 0x4000)) ++gMicroGen[1];                      // the micro memory changed
}
void Submit2();
void Fwd2(uint32_t kind, const void* data, size_t n) {      // on the first stage: an input of the second one's, in order
    uint8_t* at = QPutTo(gQ2, kind, n);
    if (n) memcpy(at, data, n);
    if (kind == kQVSync) ++gQ2.openVSyncs;
    if (gQ2.open.n >= kStage2Batch) Submit2();
}
void Wait2();
void QRun(const QBuf& b, int& vsyncs) {
    const bool two = gStages == 2;
    for (size_t p = 0; p + sizeof(QHead) <= b.n;) {
        QHead h; memcpy(&h, b.p + p, sizeof h);
        const uint8_t* d = b.p + p + sizeof h;
        switch (h.kind) {
        case kQVif1: Vif1Write((const uint32_t*)d, h.size / 4); break;
        case kQGif: GifPath(3, d, h.size / 16); break;
        case kQVu1: { uint32_t a; int32_t size; uint64_t v; memcpy(&a, d, 4); memcpy(&size, d + 4, 4); memcpy(&v, d + 8, 8); Vu1Store(a, v, size); break; }
        case kQCall: if (two) Fwd2(kQCall, d, h.size); else { void (*fn)(const uint8_t*); memcpy(&fn, d, sizeof fn); fn(d + sizeof fn); } break;
        case kQDraw: if (two) Fwd2(kQDraw, d, 1); else gGsDrawNow = d[0] != 0; break;
        case kQVSync: if (two) Fwd2(kQVSync, nullptr, 0); else VSyncWork(); ++vsyncs; break;
        case kQCallAll: { if (two) { Submit2(); Wait2(); } void (*fn)(const uint8_t*); memcpy(&fn, d, sizeof fn); fn(d + sizeof fn); break; }
        }
        p += sizeof h + ((h.size + 7) & ~(size_t)7);
    }
    if (two) Submit2();                                     // the batch's GS work to the second stage
    else gsgpu::Kick();                                     // the batch's draws to the render thread
}
void QRun2(const QBuf& b, int& vsyncs) {                    // the second stage: the GIF's packets and the GS's calls
    for (size_t p = 0; p + sizeof(QHead) <= b.n;) {
        QHead h; memcpy(&h, b.p + p, sizeof h);
        const uint8_t* d = b.p + p + sizeof h;
        switch (h.kind) {
        case kQGif1: GifPath(1, d, h.size / 16); break;
        case kQGif2: GifPath(2, d, h.size / 16); break;
        case kQGif: GifPath(3, d, h.size / 16); break;
        case kQCall: { void (*fn)(const uint8_t*); memcpy(&fn, d, sizeof fn); fn(d + sizeof fn); break; }
        case kQDraw: gGsDrawNow = d[0] != 0; break;
        case kQVSync: VSyncWork(); ++vsyncs; break;
        }
        p += sizeof h + ((h.size + 7) & ~(size_t)7);
    }
    gsgpu::Kick();
}
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
extern "C" __declspec(dllimport) void* __stdcall OpenThread(unsigned long access, int inherit, unsigned long id);
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void* thread, int priority);
#endif
void QThreadOf(Queue& q, void (*run)(const QBuf&, int&), void** handle, int stage) {
    tGsStage = stage;
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), 1);               // THREAD_PRIORITY_ABOVE_NORMAL: the game thread waits on it
    *handle = OpenThread(0x1FFFFF, 0, GetCurrentThreadId());   // the profiler samples it (THREAD_ALL_ACCESS)
#else
    (void)handle;
#endif
    for (;;) {
        QBuf b;
        {
            std::unique_lock<std::mutex> l(q.mu);
            q.work.wait(l, [&q] { return !q.handed.empty(); });
            b = std::move(q.handed.front());
            q.handed.pop_front();
        }
        int vsyncs = 0;
        run(b, vsyncs);
        {
            std::lock_guard<std::mutex> l(q.mu);
            ++q.finished;
            q.pendingBytes -= b.n; q.pendingVSyncs -= vsyncs;
            b.n = 0;
            if (q.spare.size() < 32 && b.cap <= (64u << 20)) q.spare.push_back(std::move(b));   // kept: no new pages to fault in
        }
        q.idle.notify_all();
    }
}
void QThread() { QThreadOf(gQ, &QRun, &gGsThreadHandle, 1); }
void QThread2() { QThreadOf(gQ2, &QRun2, &gGsThread2Handle, 2); }
// a batch handed to a stage's thread (the open one made from the largest kept); far ahead, its thread catches up first
void HandOver(Queue& q, int aheadVSyncs, size_t aheadBytes) {
    std::unique_lock<std::mutex> l(q.mu);
    if (q.pendingVSyncs > aheadVSyncs || q.pendingBytes > aheadBytes) {
        const auto t0 = std::chrono::steady_clock::now();
        q.idle.wait(l, [&q] { return q.pendingVSyncs <= 1 && q.pendingBytes <= (128u << 20); });
        ++q.fullWaits; q.fullNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    }
    q.pendingBytes += q.open.n; q.pendingVSyncs += q.openVSyncs;
    ++q.batches; q.bytes += q.open.n;
    q.handed.push_back(std::move(q.open));
    ++q.submitted;
    if (!q.spare.empty()) {                                 // the largest one kept (a batch of uploads is not grown again)
        size_t best = 0;
        for (size_t k = 1; k < q.spare.size(); ++k) if (q.spare[k].cap > q.spare[best].cap) best = k;
        std::swap(q.spare[best], q.spare.back());
        q.open = std::move(q.spare.back()); q.spare.pop_back();
    }
    q.open.n = 0; q.openVSyncs = 0;
    q.work.notify_one();
}
void Submit2() { if (gQ2.open.n) HandOver(gQ2, 3, 256u << 20); }
void Wait2() {                                              // on the first stage: the second one has done all it was handed
    std::unique_lock<std::mutex> l(gQ2.mu);
    const uint64_t target = gQ2.submitted;
    gQ2.idle.wait(l, [target] { return gQ2.finished >= target; });
}
}  // namespace
bool GsThreadOn() { return gQOn.load(std::memory_order_relaxed); }
long long GsFinishAt() { return gFinishAt.load(std::memory_order_relaxed); }
bool GsOnThread() { return tGsStage != 0; }
bool GsOnFirst() { return tGsStage == 1 && gStages == 2; }
int GsThreadStages() { return GsThreadOn() ? gStages : 0; }
void GsFwdGif(int path, const uint8_t* data, size_t qwords) { Fwd2(path == 1 ? kQGif1 : path == 2 ? kQGif2 : kQGif, data, qwords * 16); }
void GsFwdCall(void (*fn)(const uint8_t* arg), const void* arg, size_t n) {
    uint8_t* d = QPutTo(gQ2, kQCall, sizeof fn + n);
    memcpy(d, &fn, sizeof fn);
    if (n) memcpy(d + sizeof fn, arg, n);
    if (gQ2.open.n >= kStage2Batch) Submit2();
}
bool GsThreadStart() { return GsThreadStart(1); }
bool GsThreadStart(int stages) {
    if (GsThreadOn()) return false;
    if (stages == 2 && (getenv("SIM_GSQ") || getenv("SIM_GSQ_AT"))) stages = 1;   // a recording takes the inputs on one thread
    DrainAll();
    gQ.lastDraw = gGsDraw; gGsDrawNow = gGsDraw;
    gStages = stages == 2 ? 2 : 1;
    if (gStages == 2) std::thread(QThread2).detach();
    std::thread(QThread).detach();
    gQOn.store(true);
    if (gLog) gLog(gStages == 2 ? "gs thread: on, in two stages (VIF1 and VU1 on one thread, the GIF and the GS on another, beside the game thread)"
                                : "gs thread: on (VIF1, VU1, the GIF and the GS beside the game thread)");
    return true;
}
void GsQueueVif1(const uint32_t* words, size_t n) {
    if (!n) return;
    QDrawFlag();
    memcpy(QPut(kQVif1, n * 4), words, n * 4);
}
void GsQueueGif(const uint8_t* data, size_t qwords) {
    if (!qwords) return;
    QDrawFlag();
    memcpy(QPut(kQGif, qwords * 16), data, qwords * 16);
}
void GsQueueVu1Write(uint32_t addr, uint64_t v, int size) {
    uint8_t* d = QPut(kQVu1, 16);
    const int32_t s = size;
    memcpy(d, &addr, 4); memcpy(d + 4, &s, 4); memcpy(d + 8, &v, 8);
}
void GsQueueCall(void (*fn)(const uint8_t* arg), const void* arg, size_t n) {
    QDrawFlag();
    uint8_t* d = QPut(kQCall, sizeof fn + n);
    memcpy(d, &fn, sizeof fn);
    if (n) memcpy(d + sizeof fn, arg, n);
}
void GsQueueCallAll(void (*fn)(const uint8_t* arg), const void* arg, size_t n) {
    QDrawFlag();
    uint8_t* d = QPut(kQCallAll, sizeof fn + n);
    memcpy(d, &fn, sizeof fn);
    if (n) memcpy(d + sizeof fn, arg, n);
}
void GsQueueVSync() { QPut(kQVSync, 0); ++gQ.openVSyncs; }
void GsQueueSubmit() {
    if (!GsThreadOn() || !gQ.open.n || GsOnThread()) return;         // the producers take turns with the PS2 threads' baton
    HandOver(gQ, 3, 256u << 20);
}
void GsSync() {
    if (!GsThreadOn() || GsOnThread()) return;
    GsQueueSubmit();                                        // what is queued first
    {
        std::unique_lock<std::mutex> l(gQ.mu);
        ++gQ.syncs;
        if (gQ.finished != gQ.submitted) {
            const uint64_t target = gQ.submitted;
            const auto t0 = std::chrono::steady_clock::now();
            gQ.idle.wait(l, [target] { return gQ.finished >= target; });
            ++gQ.syncWaits; gQ.syncNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        }
    }
    if (gStages != 2) return;                               // the first stage handed its work over before it finished
    std::unique_lock<std::mutex> l(gQ2.mu);
    if (gQ2.finished == gQ2.submitted) return;
    const uint64_t target = gQ2.submitted;
    const auto t0 = std::chrono::steady_clock::now();
    gQ2.idle.wait(l, [target] { return gQ2.finished >= target; });
    ++gQ2.syncWaits; gQ2.syncNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
}
// ---- the bench's recording (gs_thread.h, tests/gsq_bench.cpp): the GS side's state at a VSync (its own, VU1's,
// VIF1's), then every input it takes until the last VSync wanted. A payload seen before (the models' packets are
// the same each picture) is written as a reference to its first copy.
bool gGsqOn = false;
namespace {
struct Gsq {
    FILE* f = nullptr; std::string path; int after = -1, left = 0;
    std::unordered_map<uint64_t, uint64_t> seen;                 // a payload's hash and size -> its offset in the file
    uint64_t at = 0, items = 0, bytes = 0, refs = 0;
} gGsq;
void GsqWrite(const void* p, size_t n) { if (n) fwrite(p, 1, n, gGsq.f); gGsq.at += n; }
void GsqSection(const std::string& s) { const uint64_t n = s.size(); GsqWrite(&n, 8); GsqWrite(s.data(), s.size()); }
template <class T> void Put(std::string& o, const T& v) { o.append((const char*)&v, sizeof v); }
template <class T> bool Get(const std::string& s, size_t& at, T& v) { if (at + sizeof v > s.size()) return false; memcpy(&v, s.data() + at, sizeof v); at += sizeof v; return true; }
std::string GsqStateSave() {
    std::string o;
    Put(o, G); o.append((const char*)gVram, sizeof gVram);
    Put(o, gGif); Put(o, gShownFbp); Put(o, gHudSqueeze); Put(o, gInMatchHud); Put(o, gMenuPage); Put(o, gHdOn); Put(o, gVSyncs);
    Put(o, gPageId); Put(o, gIdPages); Put(o, gIdSerial); Put(o, gIdLo); Put(o, gIdHi); Put(o, gClutVersion);
    Put(o, gXferPages); Put(o, gXferDst); Put(o, gXferRect); Put(o, (uint64_t)gXferBytes.size()); o.append((const char*)gXferBytes.data(), gXferBytes.size());
    { std::lock_guard<std::mutex> l(gLabelsMu); Put(o, (uint64_t)gLabels.size()); for (auto& x : gLabels) Put(o, x); }
    Put(o, gPack.open); Put(o, gPack.kind); Put(o, gPack.p2); Put(o, (uint64_t)gPack.packets.size());
    for (auto& p : gPack.packets) { Put(o, p.path); Put(o, (uint64_t)p.bytes.size()); o.append((const char*)p.bytes.data(), p.bytes.size()); }
    return o;
}
uint64_t GsqHash(const uint8_t* p, size_t n) {
    uint64_t h = 0x9e3779b97f4a7c15ull ^ n;
    size_t k = 0;
    for (; k + 8 <= n; k += 8) { uint64_t w; memcpy(&w, p + k, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 29; }
    for (; k < n; ++k) h = (h ^ p[k]) * 0x100000001b3ull;
    return h ^ (h >> 31);
}
void GsqArmCall(const uint8_t* a) {
    if (gStages == 2) { if (gLog) gLog("gsq: not recorded (the GS thread in two stages)"); return; }
    int v[2]; memcpy(v, a, 8);
    gGsq.path.assign((const char*)a + 8);
    gGsq.after = v[0]; gGsq.left = v[1];
}
void GsqStop() {
    if (!gGsq.f) return;
    const uint32_t end[2] = {0, 0}; GsqWrite(end, 8);
    fclose(gGsq.f); gGsq.f = nullptr; gGsqOn = false;
    if (gLog) gLog("gsq: %s written: %llu inputs, %.1f MB (%llu repeats referenced)", gGsq.path.c_str(), (unsigned long long)gGsq.items,
                   gGsq.at / 1048576.0, (unsigned long long)gGsq.refs);
    gGsq.seen.clear();
}
void GsqVSync() {
    if (gGsqOn) {
        GsqPut(kGsqVSync, nullptr, 0);
        if (--gGsq.left <= 0) GsqStop();
        return;
    }
    if (gGsq.after < 0 || gGsq.after-- > 0) return;
    gGsq.f = fopen(gGsq.path.c_str(), "wb");
    if (!gGsq.f) { if (gLog) gLog("gsq: %s cannot be written", gGsq.path.c_str()); gGsq.after = -1; return; }
    setvbuf(gGsq.f, nullptr, _IOFBF, 1 << 20);
    DrainAll();
    gGsq.at = gGsq.items = gGsq.bytes = gGsq.refs = 0;
    const char magic[8] = {'M', 'G', 'O', 'G', 'S', 'Q', '1', 0};
    GsqWrite(magic, 8);
    const uint32_t sizes[2] = {(uint32_t)sizeof(State), (uint32_t)sizeof(Vu0)}; GsqWrite(sizes, 8);
    GsqSection(std::string((const char*)&gVu1, sizeof gVu1));
    GsqSection(Vif1Save());
    GsqSection(GsqStateSave());
    gGsqOn = true;
    if (gLog) gLog("gsq: recording %s from the GS side's VSync %llu, %d VSyncs", gGsq.path.c_str(), (unsigned long long)gVSyncs, gGsq.left);
}
}  // namespace
void GsqPut(uint32_t kind, const void* data, size_t n) {
    if (!gGsq.f) return;
    ++gGsq.items; gGsq.bytes += n;
    const uint8_t* p = (const uint8_t*)data;
    if (n >= 256 && (kind == kGsqVif1 || kind == kGsqGif)) {     // a payload seen before: a reference to it
        const uint64_t h = GsqHash(p, n) ^ ((uint64_t)n << 40) ^ kind;
        auto it = gGsq.seen.find(h);
        if (it != gGsq.seen.end()) {
            const uint32_t head[2] = {kind | kGsqRef, 16}; GsqWrite(head, 8);
            const uint64_t ref[2] = {it->second, n}; GsqWrite(ref, 16);
            ++gGsq.refs;
            return;
        }
        gGsq.seen[h] = gGsq.at + 8;
    }
    const uint32_t head[2] = {kind, (uint32_t)n}; GsqWrite(head, 8);
    GsqWrite(p, n);
    static const uint8_t pad[8] = {};
    GsqWrite(pad, ((n + 7) & ~(size_t)7) - n);
}
void GsqArm(const char* path, int afterVSyncs, int vsyncs) {
    std::string a(8, '\0'); const int v[2] = {afterVSyncs, vsyncs}; memcpy(&a[0], v, 8); a += path; a += '\0';
    if (GsThreadOn() && !GsOnThread()) GsQueueCall(&GsqArmCall, a.data(), a.size()); else GsqArmCall((const uint8_t*)a.data());
}
// the bench (tests/gsq_bench.cpp): the GS side's state as a recording holds it
bool GsqStateLoad(const std::string& s) {
    size_t at = 0;
    if (!Get(s, at, G) || at + sizeof gVram > s.size()) return false;
    memcpy(gVram, s.data() + at, sizeof gVram); at += sizeof gVram;
    uint64_t n = 0;
    if (!Get(s, at, gGif) || !Get(s, at, gShownFbp) || !Get(s, at, gHudSqueeze) || !Get(s, at, gInMatchHud) || !Get(s, at, gMenuPage) ||
        !Get(s, at, gHdOn) || !Get(s, at, gVSyncs) || !Get(s, at, gPageId) || !Get(s, at, gIdPages) || !Get(s, at, gIdSerial) ||
        !Get(s, at, gIdLo) || !Get(s, at, gIdHi) || !Get(s, at, gClutVersion) || !Get(s, at, gXferPages) || !Get(s, at, gXferDst) ||
        !Get(s, at, gXferRect) || !Get(s, at, n) || at + n > s.size()) return false;
    gXferBytes.assign(s.data() + at, s.data() + at + n); at += n;
    if (!Get(s, at, n)) return false;
    gLabels.clear();
    for (uint64_t k = 0; k < n; ++k) { LabelAt x; if (!Get(s, at, x)) return false; gLabels.push_back(x); }
    if (!Get(s, at, gPack.open) || !Get(s, at, gPack.kind) || !Get(s, at, gPack.p2) || !Get(s, at, n)) return false;
    gPack.packets.clear();
    for (uint64_t k = 0; k < n; ++k) {
        HudPacket p; uint64_t m = 0;
        if (!Get(s, at, p.path) || !Get(s, at, m) || at + m > s.size()) return false;
        p.bytes.assign(s.data() + at, s.data() + at + m); at += m;
        gPack.packets.push_back(std::move(p));
    }
    gTk = {};
    return at == s.size();
}
// the inputs a recording holds, run as the GS side ran them (the bench)
void GsqRun(uint32_t kind, const uint8_t* d, size_t n) {
    switch (kind) {
    case kGsqVif1: Vif1Write((const uint32_t*)d, n / 4); break;
    case kGsqGif: GifPath(3, d, n / 16); break;
    case kGsqVu1: { uint32_t a; int32_t size; uint64_t v; memcpy(&a, d, 4); memcpy(&size, d + 4, 4); memcpy(&v, d + 8, 8); Vu1Store(a, v, size); break; }
    case kGsqDisplay: { uint64_t r[2]; memcpy(r, d, 16); PrivDisplay((uint32_t)r[0], r[1]); break; }
    case kGsqVSync: VSyncWork(); break;
    case kGsqCrt: SetCrtCall(d); break;
    case kGsqHud: SetHudCall(d); break;
    case kGsqLabel: LabelAtCall(d); break;
    case kGsqMenuPage: MenuPageCall(d); break;
    case kGsqQuads: HudQuadsCall(d); break;
    case kGsqHd: if (d[0] != gHdOn) { gHdOn = d[0] != 0; gIdPages.Clear(); ++gIdSerial; gIdLo = 512; gIdHi = 0; } break;
    }
}
std::string GsThreadStats() {
    if (!GsThreadOn()) return "off";
    std::lock_guard<std::mutex> l(gQ.mu);
    char b[200];
    snprintf(b, sizeof b, "%llu batches (%.1f MB), %llu syncs (%llu waited %.1f ms), %llu waits ahead (%.1f ms), %d VSyncs behind",
             (unsigned long long)gQ.batches, gQ.bytes / 1048576.0, (unsigned long long)gQ.syncs, (unsigned long long)gQ.syncWaits, gQ.syncNs / 1e6,
             (unsigned long long)gQ.fullWaits, gQ.fullNs / 1e6, gQ.pendingVSyncs);
    gQ.batches = gQ.bytes = gQ.syncs = gQ.syncWaits = gQ.syncNs = gQ.fullWaits = gQ.fullNs = 0;
    if (gStages != 2) return b;
    std::lock_guard<std::mutex> l2(gQ2.mu);
    char c[200];
    snprintf(c, sizeof c, "; its second stage (the GIF and the GS): %llu batches (%.1f MB), %llu syncs waited (%.1f ms), the first stage waited %llu times (%.1f ms), %d VSyncs behind",
             (unsigned long long)gQ2.batches, gQ2.bytes / 1048576.0, (unsigned long long)gQ2.syncWaits, gQ2.syncNs / 1e6,
             (unsigned long long)gQ2.fullWaits, gQ2.fullNs / 1e6, gQ2.pendingVSyncs);
    gQ2.batches = gQ2.bytes = gQ2.syncs = gQ2.syncWaits = gQ2.syncNs = gQ2.fullWaits = gQ2.fullNs = 0;
    return std::string(b) + c;
}

}  // namespace ps2
