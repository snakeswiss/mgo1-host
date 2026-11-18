// PNG files for the HD textures (docs/TEXTURES.md): the pack's images read, the dumped textures
// written. Pixels are RGBA, one uint32_t each (R in the low byte), top line first.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ps2 {

// a PNG file (8 or 16 bits a sample, any colour type, not interlaced) as RGBA; false with the
// reason in error
bool PngRead(const std::string& path, std::vector<uint32_t>& rgba, int& w, int& h, std::string& error);
bool PngDecode(const uint8_t* data, size_t size, std::vector<uint32_t>& rgba, int& w, int& h, std::string& error);
// RGBA (alpha: true) or RGB, compressed (deflate with fixed codes, each line's filter chosen)
bool PngWrite(const std::string& path, const std::vector<uint32_t>& rgba, int w, int h, bool alpha);
std::vector<uint8_t> PngEncode(const std::vector<uint32_t>& rgba, int w, int h, bool alpha);

}  // namespace ps2
