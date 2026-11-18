// zlib / deflate decoder (RFC 1950, 1951): the base image of MGS3_N.ELF, as its loader stub
// inflates it at boot.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps2world {

// a zlib stream (2-byte header, deflate blocks); false on a malformed stream
bool ZlibInflate(const uint8_t* in, size_t size, std::vector<uint8_t>& out);

}  // namespace ps2world
