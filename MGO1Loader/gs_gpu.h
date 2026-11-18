// The GS drawn by the graphics card (OpenGL 4.3 core), at a scale of the PS2's resolution.
// gs.cpp keeps the registers, the transfers and its copy of the local memory; this side keeps
// a copy of the local memory on the GPU (textures and CLUTs are read from it in the shaders,
// with the PS2's own layouts) and the frame and depth buffers the game draws into, upscaled.
// The GL work runs on a thread of its own (the render thread): gs.cpp's calls queue commands
// and return, except when the CPU needs pixels only the GPU has.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ps2 {
namespace gsgpu {

struct Vtx { int x, y; uint32_t z; uint8_t r, g, b, a, f; float s, t, q; int u, v; };   // gs.cpp's Vertex
// the registers a primitive is drawn with (its context's, and the shared ones)
struct Regs {
    uint64_t prim;                  // PRIM (or PRMODE) attributes: IIP TME FGE ABE AA1 FST CTXT FIX
    uint64_t frame, zbuf, test, alpha, tex0, tex1, clamp, scissor, xyoffset, fba;
    uint64_t texa, fogcol, colclamp, pabe, dthe, scanmsk;
    uint64_t miptbp1, miptbp2;
};

// what the program hosting the renderer provides; the functions run on the render thread
struct Host {
    void* user = nullptr;
    // an OpenGL 4.3 core context made current on the calling thread (null: already current)
    bool (*begin)(void* user, std::string& error) = nullptr;
    void* (*getProc)(const char* name) = nullptr;
    void (*swap)(void* user) = nullptr;                    // the back buffer shown (null: no window)
    void (*size)(void* user, int& w, int& h) = nullptr;    // the window's drawable size
    // the overlay line: a coverage mask (one byte a pixel, top line first), drawn in a top corner
    // (1 left, 2 right); true when it changed since version
    bool (*overlay)(void* user, std::vector<uint8_t>& mask, int& w, int& h, int& corner, uint64_t& version) = nullptr;
    // the in-game menu over everything: premultiplied BGRA at the window's size, top line first
    // (w 0: none); true when it changed since version
    bool (*menu)(void* user, std::vector<uint32_t>& bgra, int& w, int& h, uint64_t& version) = nullptr;
    float aspect = 0;               // the picture's shape on screen: 0 the PS2's 4:3, > 0 this ratio, < 0 the window's
    int scaleX = 0;                 // GL pixels a PS2 pixel across when not the scale (a widescreen picture shows its 512 columns wider)
    bool smooth = true;             // filtered to the window's pixels (false: nearest)
    bool threaded = true;           // false: every command runs at once on the caller's thread (tests)
    // the Vulkan backend (api 1, gs_vk.cpp): the instance extensions the window's surface needs, the surface made for
    // an instance (getProc: its vkGetInstanceProcAddr; vkSurface null: no window), vsync, the pipeline cache file
    int api = 0;                    // 0 OpenGL (begin, getProc, swap), 1 Vulkan (OpenGL's begin, getProc, swap: should it stop on the way)
    std::vector<std::string> vkExtensions;
    bool (*vkSurface)(void* user, void* instance, void* getProc, uint64_t& surface, std::string& error) = nullptr;
    bool vsync = true;
    std::string vkCache;            // empty: none
    int vkDevice = -1;              // the device's index (-1: a discrete graphics card first)
};

// the renderer started (scale: 1 to 8 times the PS2's pixels, down; across: host.scaleX); vram: gs.cpp's copy of the 4 MB
// local memory. False with the reason when OpenGL 4.3 core is not there; true with the driver's
// version and renderer in text.
bool Start(const Host& host, int scale, uint32_t* vram, std::string& error);
bool On();
int Scale();

// kind 0: a line (a == b: a point), 1: a triangle, 2: a sprite; hd: the texture's HD slot (hdtex.h,
// 0: none), drawn in place of its texels once its image came
void Draw(int kind, const Regs& r, const Vtx& a, const Vtx& b, const Vtx& c, int hd = 0);
// a host -> local transfer (or the destination of a local copy): the pixels as the game sent them
// (32, 24, 16, 8 or 4 bits each, in raster order over w x h), written by the GPU into its copy
void Transfer(uint32_t bp, uint32_t bw, uint32_t psm, int x, int y, int w, int h, std::vector<uint8_t>&& pixels);
// a CLUT load (TEX0 / TEX2 CLD): entries read from the GPU's copy of the memory into the CLUT buffer.
// cpsm: 0x00 CT32, 0x02 CT16, 0x0a CT16S; csm 0: CSM1 (bw 1), 1: CSM2 (a line at x0, y0 of a bw
// buffer); entries 16 or 256; slot: where entry 0 goes in the 512-word buffer
struct ClutLoad { uint32_t cbp, cpsm, csm, entries, bw; int x0, y0, slot; };
void LoadClut(const ClutLoad& c);
void SetClut(const uint32_t words[512]);   // the CLUT buffer as it is (a dump's state)
// before the CPU reads these pages of local memory (512 bits, one per 8 KB page): what the GPU
// drew there is in the CPU copy first (waits for the render thread when it drew some)
void CpuWillRead(const uint64_t pages[8]);
void CpuWroteAll();                // the whole local memory changed on the CPU side (a dump's state loaded)
void Flush();                      // every queued command done (the caller waits)
void Kick();                       // the commands queued so far handed to the render thread (the end of a batch)

// the picture the display circuits show, on the window (queued; left out when the render thread is
// two pictures behind)
void Present(uint64_t pmode, const uint64_t dispfb[2], const uint64_t display[2]);
uint64_t PresentsLeftOut();         // pictures not shown so far: two were still waiting for the screen
// the picture's shape on screen from now on (as Host::aspect: 0 4:3, > 0 this ratio, < 0 the window's)
void SetAspect(float aspect);
// the renderer's scale from the next picture on (down, across: as Start's); the buffers drawn at
// the old one are redrawn at the new one (their pixels kept, each one stretched or picked)
void SetScale(int scale, int scaleX);
void SetSmooth(bool smooth);       // the picture filtered to the window's pixels, or nearest
// the HD textures (hdtex.h, docs/TEXTURES.md): drawn (on) or the PS2's texels (off); Replacements()
// says how many images the pack folder has
void SetReplacementFolder(const std::string& dir);
void SetReplacements(bool on);
int Replacements();

// the displayed picture read back at the renderer's scale, RGBA, top line first (tests, screenshots)
bool Picture(uint64_t pmode, const uint64_t dispfb[2], const uint64_t display[2], std::vector<uint32_t>& rgba, int& w, int& h);

std::string Stats();               // draws, batches, uploads since the last call (the log)

}  // namespace gsgpu

// gs.cpp: the GPU renderer on its local memory and registers
bool GsGpuStart(const gsgpu::Host& host, int scale, std::string& error);
bool GsGpuOn();
void GsGpuPresent();               // the displayed picture to the window
bool GsGpuPicture(std::vector<uint32_t>& rgba, int& w, int& h);
std::string GsGpuStats();
bool GsMemoryFile(const char* path);   // the local memory as the CPU reads it now, to a file (tests)
extern long long gGsDrawLimit;         // >= 0: the primitives after this many are not drawn (tests)
struct GsXferInfo {                 // a transfer into the local memory done: host -> local, or a local copy (its pixels as written)
    uint32_t bp, bw, psm; int x, y, w, h; const uint8_t* data; size_t bytes; bool copy;
};
extern void (*gGsXferHook)(const GsXferInfo&);   // the replay tool's log (tests/gs_replay.cpp --xfers)
// HD textures (hdtex.h): keys made for the textures drawn from now on, their slots sent with the draws
void GsSetHdTextures(bool on);
std::string GsHdStats();
// widescreen: the width of DG's 2D elements in the picture (1: as drawn; 0.75: a 16:9 picture),
// the platform's VIF1 MARKs around each element (0x5Axx), the counts for the log
void GsSetHud(float squeeze, bool match);
// a match's page is shown (the START menu, the team and skin select, the results): drawn as the lobby's in a wide picture
void GsSetMenuPage(bool on);
// the HUD's size in a match (1 as drawn, down to 0.4): each element toward its corner or edge
void GsSetHudScale(float scale);
float GsHudScale();
void GsPackMark(uint32_t imm);
// a 3D label's text located by the game (the font's x, y; its characters): narrowed about that place in a wide picture
void GsLabelAt(int x, int y, int chars);
// BASE CAPTURE's gauge (basecap.h): flat quads drawn under the texts of each picture of a match, in picture pixels; a
// vertical gradient from the top colour to the bottom one (RGBA bytes, A 0x80 opaque), blended as the HUD's quads
struct HudQuad { float x0, y0, x1, y1; uint32_t top, bottom; };
void GsHudQuads(const HudQuad* q, int n);     // kept for the pictures that follow; none after 8 VSyncs without a call
std::string GsHudStats();
}  // namespace ps2
