// png.cpp - PNG read and write (png.h): the reader inflates with inflate.cpp; the writer compresses
// with LZ77 matches and deflate's fixed codes, each line with the filter that leaves the smallest sum.
#include "png.h"
#include "inflate.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ps2 {
namespace {

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t c = 0xffffffffu) {
    static uint32_t t[256]; static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t x = i; for (int k = 0; k < 8; ++k) x = x & 1 ? 0xedb88320u ^ (x >> 1) : x >> 1; t[i] = x; }
        init = true;
    }
    for (size_t i = 0; i < n; ++i) c = t[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void PutBe32(std::vector<uint8_t>& o, uint32_t v) { o.push_back((uint8_t)(v >> 24)); o.push_back((uint8_t)(v >> 16)); o.push_back((uint8_t)(v >> 8)); o.push_back((uint8_t)v); }
int Paeth(int a, int b, int c) {
    const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// ---- deflate with the fixed codes
struct Bits {
    std::vector<uint8_t>& out; uint32_t acc = 0; int n = 0;
    explicit Bits(std::vector<uint8_t>& o) : out(o) {}
    void Put(uint32_t v, int count) {                 // count bits of v, least significant first
        acc |= v << n; n += count;
        while (n >= 8) { out.push_back((uint8_t)acc); acc >>= 8; n -= 8; }
    }
    void Code(uint32_t code, int len) {               // a Huffman code, its first bit the most significant
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1) << (len - 1 - i);
        Put(r, len);
    }
    void Flush() { if (n) { out.push_back((uint8_t)acc); acc = 0; n = 0; } }
};
void Literal(Bits& b, int v) {
    if (v < 144) b.Code(0x30 + v, 8);
    else if (v < 256) b.Code(0x190 + (v - 144), 9);
    else if (v < 280) b.Code(v - 256, 7);
    else b.Code(0xc0 + (v - 280), 8);
}
const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
void Match(Bits& b, int len, int dist) {
    int li = 28; while (kLenBase[li] > len) --li;
    Literal(b, 257 + li);
    if (kLenExtra[li]) b.Put((uint32_t)(len - kLenBase[li]), kLenExtra[li]);
    int di = 29; while (kDistBase[di] > dist) --di;
    b.Code((uint32_t)di, 5);
    if (kDistExtra[di]) b.Put((uint32_t)(dist - kDistBase[di]), kDistExtra[di]);
}
std::vector<uint8_t> Zlib(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out = {0x78, 0x01};
    Bits b(out);
    b.Put(1, 1); b.Put(1, 2);                          // the last block, fixed codes
    const size_t n = in.size();
    const int kHash = 1 << 15, kWindow = 32768, kChain = 48;
    std::vector<int32_t> head(kHash, -1), prev(n ? n : 1, -1);
    auto hash3 = [&](size_t i) { return (int)(((uint32_t)in[i] << 10 ^ (uint32_t)in[i + 1] << 5 ^ in[i + 2]) & (kHash - 1)); };
    size_t i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            const int h = hash3(i);
            int cand = head[h], chain = kChain;
            while (cand >= 0 && (int)i - cand <= kWindow && chain-- > 0) {
                int l = 0; const int max = (int)std::min<size_t>(258, n - i);
                while (l < max && in[cand + l] == in[i + l]) ++l;
                if (l > bestLen) { bestLen = l; bestDist = (int)i - cand; if (l == max) break; }
                cand = prev[cand];
            }
            prev[i] = head[h]; head[h] = (int)i;
        }
        if (bestLen >= 3) {
            Match(b, bestLen, bestDist);
            for (size_t k = i + 1; k < i + bestLen && k + 2 < n; ++k) { const int h = hash3(k); prev[k] = head[h]; head[h] = (int)k; }
            i += bestLen;
        } else { Literal(b, in[i]); ++i; }
    }
    Literal(b, 256);
    b.Flush();
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t v : in) { s1 = (s1 + v) % 65521; s2 = (s2 + s1) % 65521; }
    PutBe32(out, s2 << 16 | s1);
    return out;
}
void Chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& d) {
    PutBe32(o, (uint32_t)d.size());
    const size_t at = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), d.begin(), d.end());
    PutBe32(o, Crc32(o.data() + at, o.size() - at) ^ 0xffffffffu);
}

}  // namespace

std::vector<uint8_t> PngEncode(const std::vector<uint32_t>& rgba, int w, int h, bool alpha) {
    const int ch = alpha ? 4 : 3;
    const size_t row = (size_t)w * ch;
    std::vector<uint8_t> raw, cur(row), prv(row, 0), f(row), best(row);
    raw.reserve((row + 1) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t c = rgba[(size_t)y * w + x];
            for (int k = 0; k < ch; ++k) cur[(size_t)x * ch + k] = (uint8_t)(c >> (8 * k));
        }
        long bestSum = -1; int bestType = 0;
        for (int t = 0; t < 5; ++t) {
            long sum = 0;
            for (size_t i = 0; i < row; ++i) {
                const int a = i >= (size_t)ch ? cur[i - ch] : 0, b = prv[i], c = i >= (size_t)ch ? prv[i - ch] : 0;
                int p = cur[i];
                switch (t) { case 1: p -= a; break; case 2: p -= b; break; case 3: p -= (a + b) / 2; break; case 4: p -= Paeth(a, b, c); break; }
                f[i] = (uint8_t)p; sum += abs((int8_t)f[i]);
            }
            if (bestSum < 0 || sum < bestSum) { bestSum = sum; bestType = t; best = f; }
        }
        raw.push_back((uint8_t)bestType);
        raw.insert(raw.end(), best.begin(), best.end());
        prv = cur;
    }
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr;
    PutBe32(ihdr, (uint32_t)w); PutBe32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(alpha ? 6 : 2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(out, "IHDR", ihdr); Chunk(out, "IDAT", Zlib(raw)); Chunk(out, "IEND", {});
    return out;
}

bool PngWrite(const std::string& path, const std::vector<uint32_t>& rgba, int w, int h, bool alpha) {
    if (w <= 0 || h <= 0 || rgba.size() < (size_t)w * h) return false;
    const std::vector<uint8_t> d = PngEncode(rgba, w, h, alpha);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    return fclose(f) == 0 && ok;
}

bool PngDecode(const uint8_t* d, size_t size, std::vector<uint32_t>& rgba, int& w, int& h, std::string& error) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (size < 8 || memcmp(d, sig, 8)) { error = "not a PNG file"; return false; }
    int depth = 0, type = -1, interlace = 0;
    std::vector<uint8_t> idat, plte, trns;
    w = h = 0;
    for (size_t at = 8; at + 12 <= size;) {
        const uint32_t n = Be32(d + at);
        if (at + 12 + (size_t)n > size) { error = "cut short"; return false; }
        const uint8_t* c = d + at + 4; const uint8_t* body = c + 4;
        if (!memcmp(c, "IHDR", 4) && n >= 13) {
            w = (int)Be32(body); h = (int)Be32(body + 4); depth = body[8]; type = body[9]; interlace = body[12];
        } else if (!memcmp(c, "PLTE", 4)) plte.assign(body, body + n);
        else if (!memcmp(c, "tRNS", 4)) trns.assign(body, body + n);
        else if (!memcmp(c, "IDAT", 4)) idat.insert(idat.end(), body, body + n);
        else if (!memcmp(c, "IEND", 4)) break;
        at += 12 + n;
    }
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) { error = "bad size"; return false; }
    if (interlace) { error = "interlaced PNG not read"; return false; }
    const int chans = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (!chans || !(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) || (type != 0 && type != 3 && depth < 8)) {
        error = "colour type " + std::to_string(type) + " depth " + std::to_string(depth) + " not read"; return false;
    }
    std::vector<uint8_t> raw;
    if (!ps2world::ZlibInflate(idat.data(), idat.size(), raw)) { error = "bad compressed data"; return false; }
    const size_t row = ((size_t)w * chans * depth + 7) / 8;
    const size_t bpp = std::max<size_t>(1, (size_t)chans * depth / 8);
    if (raw.size() < (row + 1) * h) { error = "image data cut short"; return false; }
    std::vector<uint8_t> prv(row, 0), cur(row);
    rgba.assign((size_t)w * h, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = raw.data() + (row + 1) * y;
        const int t = src[0]; ++src;
        for (size_t i = 0; i < row; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0, b = prv[i], c = i >= bpp ? prv[i - bpp] : 0;
            int v = src[i];
            switch (t) { case 1: v += a; break; case 2: v += b; break; case 3: v += (a + b) / 2; break; case 4: v += Paeth(a, b, c); break; }
            cur[i] = (uint8_t)v;
        }
        for (int x = 0; x < w; ++x) {
            auto sample = [&](int k) -> uint32_t {                       // sample k of pixel x at 8 bits
                if (depth == 16) return cur[((size_t)x * chans + k) * 2];
                if (depth == 8) return cur[(size_t)x * chans + k];
                const size_t bit = (size_t)x * depth;
                const uint32_t v = (cur[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1);
                return type == 3 ? v : v * 255 / ((1u << depth) - 1);
            };
            auto raw16 = [&](int k) -> uint32_t { return depth == 16 ? (uint32_t)cur[((size_t)x * chans + k) * 2] << 8 | cur[((size_t)x * chans + k) * 2 + 1] : 0; };
            uint32_t r, g, b, a = 255;
            if (type == 3) {
                uint32_t i = depth == 8 ? cur[x] : sample(0);
                if (i * 3 + 2 >= plte.size()) { r = g = b = 0; } else { r = plte[i * 3]; g = plte[i * 3 + 1]; b = plte[i * 3 + 2]; }
                if (i < trns.size()) a = trns[i];
            } else if (type == 0 || type == 4) {
                r = g = b = sample(0);
                if (type == 4) a = sample(1);
                else if (trns.size() >= 2) {
                    const uint32_t key = (uint32_t)trns[0] << 8 | trns[1];
                    const uint32_t v = depth == 16 ? raw16(0) : (depth == 8 ? cur[x] : ((cur[((size_t)x * depth) >> 3] >> (8 - depth - (((size_t)x * depth) & 7))) & ((1u << depth) - 1)));
                    if (v == key) a = 0;
                }
            } else {
                r = sample(0); g = sample(1); b = sample(2);
                if (type == 6) a = sample(3);
                else if (trns.size() >= 6) {
                    const bool t16 = depth == 16;
                    const uint32_t kr = (uint32_t)trns[0] << 8 | trns[1], kg = (uint32_t)trns[2] << 8 | trns[3], kb = (uint32_t)trns[4] << 8 | trns[5];
                    if ((t16 ? raw16(0) : r) == kr && (t16 ? raw16(1) : g) == kg && (t16 ? raw16(2) : b) == kb) a = 0;
                }
            }
            rgba[(size_t)y * w + x] = r | g << 8 | b << 16 | a << 24;
        }
        std::swap(prv, cur);
    }
    return true;
}

bool PngRead(const std::string& path, std::vector<uint32_t>& rgba, int& w, int& h, std::string& error) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open"; return false; }
    std::vector<uint8_t> d;
    fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0) { d.resize((size_t)n); if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear(); }
    fclose(f);
    if (d.empty()) { error = "empty or unreadable"; return false; }
    return PngDecode(d.data(), d.size(), rgba, w, h, error);
}

}  // namespace ps2
