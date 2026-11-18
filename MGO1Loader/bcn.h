// Block compression for the HD textures (docs/TEXTURES.md): BC1 (4 bits a texel, RGB) and BC3 (8 bits,
// RGB and an interpolated alpha), the formats every desktop graphics card samples as they are
// (S3TC / DXT1 and DXT5). The images stay compressed on disk (hd\cache) and on the graphics card.
#pragma once
#include <cstdint>
#include <vector>

namespace ps2 {
namespace bcn {

// RGBA texels (R in the low byte), w x h (any size: the edge blocks repeat their last texels)
// to blocks, top-left block first; bc3: 16 bytes a block, else 8
std::vector<uint8_t> Encode(const std::vector<uint32_t>& rgba, int w, int h, bool bc3);
// the blocks back to RGBA (tests)
std::vector<uint32_t> Decode(const uint8_t* blocks, int w, int h, bool bc3);
// the next mipmap level: each texel the average of the 2 x 2 under it (an odd side keeps its last one)
std::vector<uint32_t> HalfSize(const std::vector<uint32_t>& rgba, int w, int h, int& nw, int& nh);

}  // namespace bcn
}  // namespace ps2
