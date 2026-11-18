// HD textures (docs/TEXTURES.md). Every texture the game draws gets a key from what it was uploaded
// with (gs.cpp: the transfers under its pages and its place in them, its layout and size, the CLUT
// entries it reads). A key leads to a replacement image: a pack's PNG named after it, or the
// upscaler's (Real-ESRGAN's compact model, four times larger), made on worker threads and kept in a
// cache folder. The GPU renderer draws the replacement in place of the PS2's texels once it is ready.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps2 {
namespace hdtex {

uint64_t Hash64(const void* data, size_t n, uint64_t seed = 0);   // xxHash64

struct Settings {
    bool pack = false;              // replacements from packDir: <key>.png (16 hex digits, any sub-folder but dump and cache)
    std::string packDir;
    bool upscale = false;           // the other textures upscaled (the model at modelPath), kept in cacheDir
    std::string modelPath, cacheDir;
    bool dump = false;              // each new texture written as dumpDir/<key>.png (pack making)
    std::string dumpDir;
    int maxSize = 2048;             // the largest side of an upscaled texture
    int threads = 0;                // upscaler threads (0: half the logical processors less one, at least one)
    bool sync = false;              // tests: each image made when its key is first seen, on that thread
    int vramMb = 1024;              // the HD images kept on the graphics card: past it, the least used go (read again when drawn)
};
void Configure(const Settings& s);  // before the first texture; again later for new keys
Settings Current();
// the upscales running at once at most (-1: one a worker, the default; 0: none till it rises): MGO1.exe gives
// a match one, none while the game is behind its pace (the loads from the pack and the cache go on)
void SetUpscaleLimit(int n);
int UpscaleLimit();

// GS thread. The slot of a texture (0: none: the PS2's texels; -1: none yet, ask again at the next
// VBlank: an upscale waits until the texture is still drawn half a second later). When a replacement
// may come, decode fills the texture's pixels (RGBA, alpha as the GS reads it, 0x80 = 1.0) at its
// size; repeat: it tiles (the upscaler wraps its edges, else it extends them).
typedef bool (*Decode)(void* user, std::vector<uint32_t>& rgba, int w, int h);
int SlotOf(uint64_t key, int w, int h, bool repeat, Decode decode, void* user);

// render thread: an image finished since the last call; false when none. Its levels (the mipmaps,
// level 0 first, each half the last), BC1 or BC3 blocks (bcn.h) or RGBA texels; a BC1 image's
// alpha is constAlpha (the GS's, the same for every texel)
enum { kRgba8 = 0, kBc1 = 1, kBc3 = 3 };
struct Image {
    int slot = 0, w = 0, h = 0, format = kRgba8, constAlpha = -1;
    std::vector<std::vector<uint8_t>> levels;
    size_t Bytes() const { size_t n = 0; for (const auto& l : levels) n += l.size(); return n; }
};
bool TakeFinished(Image& out);
// render thread: a slot's image is no longer on the graphics card; true when it can be read again
// (Reload: from the pack or the cache, once it is drawn again)
bool CanReload(int slot);
void Reload(int slot);

std::string Stats();                // keys, replacements, upscales done and pending (the log)
int PackCount();                    // the pack's images found

// an image's levels made from its texels (mipmaps; BC1 when its alpha is the same everywhere, else BC3)
Image Prepare(const std::vector<uint32_t>& rgba, int w, int h);
bool SaveHdImage(const std::string& path, const Image& im);   // the cache's file (.hdt)
bool LoadHdImage(const std::string& path, Image& im);

// the upscaler alone (tools, tests): rgba w x h -> out (w * scale) x (h * scale); false without a model
bool Upscale(const std::string& modelPath, const std::vector<uint32_t>& rgba, int w, int h, bool repeat,
             std::vector<uint32_t>& out, int& ow, int& oh, int threads = 0);

}  // namespace hdtex
}  // namespace ps2
