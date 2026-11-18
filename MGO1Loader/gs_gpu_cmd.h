// Between gs_gpu.cpp and its Vulkan backend (gs_vk.cpp): the settings the render thread reads, the page sets
// and the ring's commands (gs_gpu.cpp's queue side writes them, a backend's render thread runs them).
#pragma once
#include "gs_gpu.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ps2 {
namespace gsgpu {

// the settings applied by the render thread (SetAspect, SetScale, SetSmooth, SetReplacements)
inline std::atomic<float> gAspect{0.0f};      // the picture's shape (Host::aspect, SetAspect)
inline std::atomic<int> gWantScale{0}, gWantScaleX{0};   // SetScale: applied by the render thread at a picture
inline std::atomic<int> gSmooth{-1};          // SetSmooth: -1 Host::smooth
// the HD textures (hdtex.h): drawn in place of the PS2's texels while on (SetReplacements)
inline std::atomic<bool> gReplaceOn{true};

// the render thread's time running commands (RunTo), and the part of it spent waiting for the graphics card (a
// swap, a read back, a fence): the log's line (Stats)
inline std::atomic<uint64_t> gBusyNs{0}, gBlockNs{0};
struct BlockTimer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~BlockTimer() { gBlockNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count(); }
};
inline std::string BusyText() {
    const uint64_t busy = gBusyNs.exchange(0), block = gBlockNs.exchange(0);
    char b[120];
    snprintf(b, sizeof b, ", the render thread busy %llu ms (and %llu ms waiting for the graphics card)",
             (unsigned long long)((busy > block ? busy - block : 0) / 1000000), (unsigned long long)(block / 1000000));
    return b;
}

// a backend of the render side other than OpenGL's: gs_vk.cpp registers the Vulkan one when it is linked in (the
// builds without it, the simulation's, have none); its functions run on the render thread
struct Backend {
    bool (*setup)(const Host& host, int scale, int scaleX, uint32_t* vram, std::string& error, std::string& description);
    void (*execute)(uint32_t type, void* payload);   // one command of the ring
    void (*idle)();                                    // nothing queued: the recorded commands sent to the device
    std::string (*stats)(uint64_t waits, uint64_t waitUs);
    const std::string* (*fault)();                     // what stopped it (a lost device, no memory for a frame buffer), else null
    void (*release)();                                 // after a fault: the window let go (OpenGL may take it)
};
inline const Backend* gVulkanBackend = nullptr;

// ---- the pages of the local memory
struct PageSet {
    uint64_t w[8] = {};
    void Set(uint32_t p) { p &= 511; w[p >> 6] |= 1ull << (p & 63); }
    bool Has(uint32_t p) const { p &= 511; return (w[p >> 6] >> (p & 63)) & 1; }
    void Clear(uint32_t p) { p &= 511; w[p >> 6] &= ~(1ull << (p & 63)); }
    bool Any() const { for (auto x : w) if (x) return true; return false; }
    bool Meets(const PageSet& o) const { for (int k = 0; k < 8; ++k) if (w[k] & o.w[k]) return true; return false; }
    void Add(const PageSet& o) { for (int k = 0; k < 8; ++k) w[k] |= o.w[k]; }
    void Remove(const PageSet& o) { for (int k = 0; k < 8; ++k) w[k] &= ~o.w[k]; }
    int Count() const { int n = 0; for (auto x : w) n += __builtin_popcountll(x); return n; }
};

// ---- the commands, and the ring that carries them from gs.cpp's thread to the render thread
enum : uint32_t { kPad, kRegs, kClutSet, kClutLoad, kDraw, kXfer, kWroteAll, kTouch, kSync, kPresent, kPicture, kClutData };
struct CmdHead { uint32_t type, size; };                       // size: the whole command, a multiple of 16
struct ClutCmd { uint32_t words[512]; };
struct ClutDataCmd { uint32_t slot, n, pad[2]; };              // then n words: CLUT entries slot.. made by gs.cpp's side
struct DrawCmd { int kind, hd; Vtx v[3]; int16_t box[4]; };       // box: the PS2 pixels it may draw (PrimBox, the queue side's)
struct XferCmd { uint32_t bp, bw, psm; int x, y, w, h; std::vector<uint8_t>* pixels; };   // pixels: freed by the render thread
struct AllCmd { std::vector<uint32_t>* data; };                // the whole memory, freed by the render thread
struct TouchCmd { PageSet pages; };
struct ScreenCmd { uint64_t pmode, dispfb[2], display[2]; };
struct PictureCmd { ScreenCmd screen; std::vector<uint32_t>* rgba; int* w; int* h; bool* ok; };

}  // namespace gsgpu
}  // namespace ps2
