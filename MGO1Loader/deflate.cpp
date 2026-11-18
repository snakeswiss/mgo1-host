// deflate.cpp - zlib / deflate encoder (deflate.h): LZ77 over a 32 KB window (hash chains, one-step
// lazy matching, the search limits of zlib's level 6), 16384 symbols a block, each block written
// with dynamic Huffman codes or stored, whichever is smaller.
#include "deflate.h"
#include <algorithm>
#include <cstring>

namespace ps2world {
namespace {

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

const int kWindow = 32768, kWMask = kWindow - 1, kHashBits = 15, kHashSize = 1 << kHashBits;
const int kMinMatch = 3, kMaxMatch = 258;
const int kMaxChain = 128, kGoodLength = 8, kNiceLength = 128, kMaxLazy = 16;   // zlib's level 6
const size_t kBlockSymbols = 16384;

struct BitOut {
    std::vector<uint8_t>& out; uint64_t acc = 0; int n = 0;
    explicit BitOut(std::vector<uint8_t>& o) : out(o) {}
    void Put(uint32_t v, int count) {                 // count bits of v, least significant first
        acc |= (uint64_t)v << n; n += count;
        while (n >= 8) { out.push_back((uint8_t)acc); acc >>= 8; n -= 8; }
    }
    void Align() { if (n) { out.push_back((uint8_t)acc); acc = 0; n = 0; } }
};

// Huffman code lengths of n symbols, none above limit (0 for a symbol never used): the two-queue
// construction over the symbols sorted by frequency, then, when too deep, the counts per length
// brought within the limit as JPEG does (Annex K.3: the two deepest leaves, one up in their parent's
// place, the other down beside the deepest shallower leaf; the code stays complete), the longest codes
// going to the rarest symbols
void CodeLengths(const uint32_t* freq, int n, int limit, uint8_t* len) {
    memset(len, 0, (size_t)n);
    std::vector<int> sym;
    for (int i = 0; i < n; ++i) if (freq[i]) sym.push_back(i);
    const int m = (int)sym.size();
    if (m == 0) return;
    if (m == 1) { len[sym[0]] = 1; return; }
    std::stable_sort(sym.begin(), sym.end(), [&](int a, int b) { return freq[a] < freq[b]; });
    std::vector<uint64_t> w((size_t)2 * m);
    std::vector<int> parent((size_t)2 * m, -1), depth((size_t)2 * m, 0);
    for (int k = 0; k < m; ++k) w[k] = freq[sym[k]];
    int li = 0, ii = m, next = m;
    auto take = [&]() { return (li < m && (ii >= next || w[li] <= w[ii])) ? li++ : ii++; };
    while (next < 2 * m - 1) {
        const int a = take(), b = take();
        w[next] = w[a] + w[b]; parent[a] = parent[b] = next; ++next;
    }
    for (int k = 2 * m - 3; k >= 0; --k) depth[k] = depth[parent[k]] + 1;
    int deepest = 0;
    for (int k = 0; k < m; ++k) deepest = std::max(deepest, depth[k]);
    if (deepest <= limit) { for (int k = 0; k < m; ++k) len[sym[k]] = (uint8_t)depth[k]; return; }
    std::vector<int> count((size_t)deepest + 2, 0);
    for (int k = 0; k < m; ++k) ++count[(size_t)depth[k]];
    for (int i = deepest; i > limit; --i)
        while (count[(size_t)i] > 0) {
            int j = i - 2;
            while (j > 0 && count[(size_t)j] == 0) --j;
            count[(size_t)i] -= 2; count[(size_t)i - 1] += 1;
            count[(size_t)j + 1] += 2; count[(size_t)j] -= 1;
        }
    int k = 0;                                           // the rarest symbols first: the longest codes
    for (int bits = limit; bits >= 1; --bits)
        for (int c = 0; c < count[(size_t)bits]; ++c) len[sym[k++]] = (uint8_t)bits;
}

// canonical codes of the lengths, bit-reversed (deflate sends a code's first bit first)
void Codes(const uint8_t* len, int n, uint16_t* code) {
    int count[16] = {0}, next[16] = {0};
    for (int i = 0; i < n; ++i) ++count[len[i]];
    count[0] = 0;
    int c = 0;
    for (int bits = 1; bits < 16; ++bits) { c = (c + count[bits - 1]) << 1; next[bits] = c; }
    for (int i = 0; i < n; ++i) {
        if (!len[i]) { code[i] = 0; continue; }
        const int v = next[len[i]]++;
        int r = 0;
        for (int b = 0; b < len[i]; ++b) r |= ((v >> b) & 1) << (len[i] - 1 - b);
        code[i] = (uint16_t)r;
    }
}

int LenCode(int length) { int k = 28; while (kLenBase[k] > length) --k; return k; }
int DistCode(int dist) { int k = 29; while (kDistBase[k] > dist) --k; return k; }

struct Sym { uint16_t lit; uint16_t dist; };            // dist 0: a literal (or 256: the end); else a match of length lit

// one block: its symbols (the end included) as dynamic codes, or its bytes stored when that is smaller
void WriteBlock(BitOut& bo, const std::vector<Sym>& syms, const uint8_t* raw, size_t rawSize, bool last) {
    uint32_t lf[286] = {0}, df[30] = {0};
    for (const Sym& s : syms) {
        if (!s.dist) ++lf[s.lit];
        else { ++lf[257 + LenCode(s.lit)]; ++df[DistCode(s.dist)]; }
    }
    int used = 0;
    for (int i = 0; i < 30; ++i) used += df[i] != 0;
    if (used == 0) df[0] = df[1] = 1;                         // a complete distance code: two symbols at least
    else if (used == 1) { if (!df[0]) df[0] = 1; else df[1] = 1; }
    uint8_t ll[286], dl[30];
    CodeLengths(lf, 286, 15, ll);
    CodeLengths(df, 30, 15, dl);
    int hlit = 286; while (hlit > 257 && !ll[hlit - 1]) --hlit;
    int hdist = 30; while (hdist > 1 && !dl[hdist - 1]) --hdist;
    // the lengths of both codes run-length coded (16 repeat the last 3-6 times, 17 zeros 3-10, 18 zeros 11-138)
    uint8_t all[316];
    memcpy(all, ll, (size_t)hlit); memcpy(all + hlit, dl, (size_t)hdist);
    const int total = hlit + hdist;
    std::vector<std::pair<uint8_t, uint8_t>> rl;             // (symbol, extra bits value)
    for (int i = 0; i < total;) {
        const uint8_t v = all[i];
        int run = 1; while (i + run < total && all[i + run] == v) ++run;
        if (v == 0) {
            int r = run;
            while (r >= 11) { const int t = std::min(r, 138); rl.push_back({18, (uint8_t)(t - 11)}); r -= t; }
            if (r >= 3) { rl.push_back({17, (uint8_t)(r - 3)}); r = 0; }
            while (r-- > 0) rl.push_back({0, 0});
        } else {
            rl.push_back({v, 0});
            int r = run - 1;
            while (r >= 3) { const int t = std::min(r, 6); rl.push_back({16, (uint8_t)(t - 3)}); r -= t; }
            while (r-- > 0) rl.push_back({v, 0});
        }
        i += run;
    }
    uint32_t cf[19] = {0};
    for (auto& e : rl) ++cf[e.first];
    uint8_t cl[19];
    CodeLengths(cf, 19, 7, cl);
    int hclen = 19; while (hclen > 4 && !cl[kClOrder[hclen - 1]]) --hclen;
    uint16_t lc[286], dc[30], cc[19];
    Codes(ll, 286, lc); Codes(dl, 30, dc); Codes(cl, 19, cc);
    // its size in bits, dynamic against stored
    uint64_t bits = 3 + 5 + 5 + 4 + 3ull * hclen;
    for (auto& e : rl) bits += cl[e.first] + (e.first == 16 ? 2 : e.first == 17 ? 3 : e.first == 18 ? 7 : 0);
    for (int i = 0; i < 286; ++i) bits += (uint64_t)lf[i] * ll[i];
    for (int i = 0; i < 29; ++i) bits += (uint64_t)lf[257 + i] * kLenExtra[i];
    for (int i = 0; i < 30; ++i) if (df[i]) bits += (uint64_t)df[i] * (dl[i] + kDistExtra[i]);
    const uint64_t storedBits = 3 + 7 + (rawSize / 65535 + 1) * 32 + (uint64_t)rawSize * 8 + (rawSize / 65535) * 3 * 8;
    if (storedBits < bits) {
        size_t at = 0;
        do {
            const size_t n = std::min<size_t>(65535, rawSize - at);
            const bool end = last && at + n == rawSize;
            bo.Put(end ? 1 : 0, 1); bo.Put(0, 2); bo.Align();
            bo.Put((uint32_t)n, 16); bo.Put((uint32_t)(~n & 0xffff), 16);
            for (size_t k = 0; k < n; ++k) bo.Put(raw[at + k], 8);
            at += n;
        } while (at < rawSize);
        return;
    }
    bo.Put(last ? 1 : 0, 1); bo.Put(2, 2);
    bo.Put((uint32_t)(hlit - 257), 5); bo.Put((uint32_t)(hdist - 1), 5); bo.Put((uint32_t)(hclen - 4), 4);
    for (int i = 0; i < hclen; ++i) bo.Put(cl[kClOrder[i]], 3);
    for (auto& e : rl) {
        bo.Put(cc[e.first], cl[e.first]);
        if (e.first == 16) bo.Put(e.second, 2);
        else if (e.first == 17) bo.Put(e.second, 3);
        else if (e.first == 18) bo.Put(e.second, 7);
    }
    for (const Sym& s : syms) {
        if (!s.dist) { bo.Put(lc[s.lit], ll[s.lit]); continue; }
        const int l = LenCode(s.lit), d = DistCode(s.dist);
        bo.Put(lc[257 + l], ll[257 + l]);
        if (kLenExtra[l]) bo.Put((uint32_t)(s.lit - kLenBase[l]), kLenExtra[l]);
        bo.Put(dc[d], dl[d]);
        if (kDistExtra[d]) bo.Put((uint32_t)(s.dist - kDistBase[d]), kDistExtra[d]);
    }
}

}  // namespace

std::vector<uint8_t> ZlibDeflate(const uint8_t* in, size_t n) {
    std::vector<uint8_t> out = {0x78, 0x9C};
    out.reserve(n / 2 + 64);
    BitOut bo(out);
    std::vector<int32_t> head(kHashSize, -1), prev(kWindow, -1);
    auto hash = [&](size_t i) { return (int)((((uint32_t)in[i] << 10) ^ ((uint32_t)in[i + 1] << 5) ^ in[i + 2]) & (kHashSize - 1)); };
    auto insert = [&](size_t i) { if (i + 2 < n) { const int h = hash(i); prev[i & kWMask] = head[h]; head[h] = (int32_t)i; } };
    // the longest match at i among the chain (inserted positions before i), at least better than 'than'
    auto longest = [&](size_t i, int than, int& dist) {
        int best = than; dist = 0;
        if (i + 2 >= n) return 0;
        const int max = (int)std::min<size_t>(kMaxMatch, n - i);
        if (than >= max) return 0;
        int chain = than >= kGoodLength ? kMaxChain / 4 : kMaxChain;
        for (int32_t cand = head[hash(i)]; cand >= 0 && chain-- > 0; cand = prev[cand & kWMask]) {
            const size_t d = i - (size_t)cand;
            if (d == 0 || d > (size_t)kWindow - 262) break;   // zlib's MAX_DIST: the window less the look-ahead
            if (in[cand + best] != in[i + best] || in[cand] != in[i]) continue;
            int l = 0;
            while (l < max && in[cand + l] == in[i + l]) ++l;
            if (l > best) { best = l; dist = (int)d; if (l >= kNiceLength || l == max) break; }
            if (prev[cand & kWMask] >= cand) break;           // an entry the window reused: the chain ends
        }
        return dist ? best : 0;
    };
    std::vector<Sym> syms;
    syms.reserve(kBlockSymbols + 1);
    size_t blockStart = 0, i = 0;
    auto flush = [&](bool last) {
        syms.push_back({256, 0});
        WriteBlock(bo, syms, in + blockStart, i - blockStart, last);
        syms.clear();
        blockStart = i;
    };
    int prevLen = 0, prevDist = 0;                            // the lazy match waiting at i - 1
    bool pending = false;
    while (i < n) {
        int dist = 0;
        const int len = (pending && prevLen >= kMaxLazy) ? 0 : longest(i, pending ? prevLen : kMinMatch - 1, dist);
        insert(i);
        if (pending) {
            if (len > prevLen) {                              // better here: the byte before goes as a literal
                syms.push_back({in[i - 1], 0});
                prevLen = len; prevDist = dist;
                ++i;
            } else {                                          // the match from i - 1 wins
                syms.push_back({(uint16_t)prevLen, (uint16_t)prevDist});
                for (size_t k = i + 1; k < i - 1 + (size_t)prevLen; ++k) insert(k);
                i = i - 1 + (size_t)prevLen;
                pending = false;
            }
        } else if (len >= kMinMatch) {
            pending = true; prevLen = len; prevDist = dist;
            ++i;
        } else {
            syms.push_back({in[i], 0});
            ++i;
        }
        if (!pending && syms.size() >= kBlockSymbols) flush(false);
    }
    if (pending) {                                            // the input ends on a match started at i - 1
        syms.push_back({(uint16_t)prevLen, (uint16_t)prevDist});
        i = i - 1 + (size_t)prevLen;
    }
    flush(true);
    bo.Align();
    uint32_t s1 = 1, s2 = 0;
    for (size_t k = 0; k < n;) {                              // Adler-32 in runs short enough not to overflow
        const size_t end = std::min(n, k + 5552);
        for (; k < end; ++k) { s1 += in[k]; s2 += s1; }
        s1 %= 65521; s2 %= 65521;
    }
    const uint32_t adler = s2 << 16 | s1;
    out.push_back((uint8_t)(adler >> 24)); out.push_back((uint8_t)(adler >> 16)); out.push_back((uint8_t)(adler >> 8)); out.push_back((uint8_t)adler);
    return out;
}

}  // namespace ps2world
