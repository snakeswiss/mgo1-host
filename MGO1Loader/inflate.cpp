// zlib / deflate decoder (RFC 1950, 1951), canonical Huffman decoding one bit at a time.
#include "inflate.h"
#include <cstring>

namespace ps2world {
namespace {

struct Bits {
    const uint8_t* p; size_t n, pos = 0;
    uint32_t buf = 0; int cnt = 0; bool bad = false;
    int Get(int k) {
        while (cnt < k) {
            if (pos >= n) { bad = true; return 0; }
            buf |= (uint32_t)p[pos++] << cnt; cnt += 8;
        }
        const int v = (int)(buf & ((1u << k) - 1));
        buf >>= k; cnt -= k;
        return v;
    }
};

struct Huff { uint16_t count[16]; uint16_t sym[320]; };

bool Build(Huff& h, const uint8_t* len, int n) {
    memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i) h.count[len[i]]++;
    h.count[0] = 0;
    int left = 1;                                        // no over-subscribed code
    for (int i = 1; i < 16; ++i) { left <<= 1; left -= h.count[i]; if (left < 0) return false; }
    uint16_t offs[16]; offs[1] = 0;
    for (int i = 1; i < 15; ++i) offs[i + 1] = (uint16_t)(offs[i] + h.count[i]);
    for (int i = 0; i < n; ++i) if (len[i]) h.sym[offs[len[i]]++] = (uint16_t)i;
    return true;
}

int Decode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= b.Get(1);
        const int count = h.count[len];
        if (code - count < first) return h.sym[index + (code - first)];
        index += count; first += count;
        first <<= 1; code <<= 1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& b, std::vector<uint8_t>& out, const Huff& lit, const Huff& dist) {
    for (;;) {
        int s = Decode(b, lit);
        if (s < 0 || b.bad) return false;
        if (s < 256) { out.push_back((uint8_t)s); continue; }
        if (s == 256) return true;
        s -= 257;
        if (s >= 29) return false;
        const int len = kLenBase[s] + b.Get(kLenExtra[s]);
        const int d = Decode(b, dist);
        if (d < 0 || d >= 30) return false;
        const size_t back = kDistBase[d] + (size_t)b.Get(kDistExtra[d]);
        if (b.bad || back > out.size()) return false;
        const size_t from = out.size() - back;
        for (int k = 0; k < len; ++k) out.push_back(out[from + k]);
    }
}

}  // namespace

bool ZlibInflate(const uint8_t* in, size_t size, std::vector<uint8_t>& out) {
    if (size < 2 || (in[0] & 0x0f) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20)) return false;
    Bits b{in + 2, size - 2};
    out.clear();
    for (int last = 0; !last;) {
        last = b.Get(1);
        const int type = b.Get(2);
        if (b.bad) return false;
        if (type == 0) {                                  // stored
            b.buf = 0; b.cnt = 0;
            if (b.pos + 4 > b.n) return false;
            const unsigned len = b.p[b.pos] | b.p[b.pos + 1] << 8, nlen = b.p[b.pos + 2] | b.p[b.pos + 3] << 8;
            b.pos += 4;
            if ((len ^ 0xffff) != nlen || b.pos + len > b.n) return false;
            out.insert(out.end(), b.p + b.pos, b.p + b.pos + len);
            b.pos += len;
        } else if (type == 1) {                           // fixed codes
            static Huff lit, dist; static bool made = false;
            if (!made) {
                uint8_t l[288];
                for (int i = 0; i < 288; ++i) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
                Build(lit, l, 288);
                for (int i = 0; i < 30; ++i) l[i] = 5;
                Build(dist, l, 30);
                made = true;
            }
            if (!Codes(b, out, lit, dist)) return false;
        } else if (type == 2) {                           // dynamic codes
            const int nlen = b.Get(5) + 257, ndist = b.Get(5) + 1, ncode = b.Get(4) + 4;
            if (b.bad || nlen > 286 || ndist > 30) return false;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lens[320] = {0};
            for (int i = 0; i < ncode; ++i) lens[order[i]] = (uint8_t)b.Get(3);
            Huff lc;
            if (!Build(lc, lens, 19)) return false;
            memset(lens, 0, sizeof lens);
            for (int i = 0; i < nlen + ndist;) {
                int s = Decode(b, lc);
                if (s < 0 || b.bad) return false;
                if (s < 16) { lens[i++] = (uint8_t)s; continue; }
                int rep = 0; uint8_t v = 0;
                if (s == 16) { if (i == 0) return false; v = lens[i - 1]; rep = 3 + b.Get(2); }
                else if (s == 17) rep = 3 + b.Get(3);
                else rep = 11 + b.Get(7);
                if (i + rep > nlen + ndist) return false;
                while (rep--) lens[i++] = v;
            }
            Huff lit, dist;
            if (!Build(lit, lens, nlen) || !Build(dist, lens + nlen, ndist)) return false;
            if (!Codes(b, out, lit, dist)) return false;
        } else return false;
    }
    return true;
}

}  // namespace ps2world
