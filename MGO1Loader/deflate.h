// zlib / deflate encoder (RFC 1950, 1951): LZ77 with hash chains and one-step lazy matching, blocks
// with dynamic Huffman codes (lengths limited to 15 bits) or stored when smaller. The stages' blocks
// the extra maps rebuild (extramaps.cpp) are compressed with it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2world {

// a zlib stream of in: header 78 9C, the deflate blocks, the Adler-32 of in
std::vector<uint8_t> ZlibDeflate(const uint8_t* in, size_t size);

}  // namespace ps2world
