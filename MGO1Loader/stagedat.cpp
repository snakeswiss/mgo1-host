// STAGE_N.DAT's page scripts (stagedat.h)
#include "stagedat.h"
#include "inflate.h"
#include <cstdio>
#include <cstring>
#include <functional>

namespace ps2world {
namespace {
const uint32_t kKey1 = 0x02E90EDD, kKey2 = 0x7A88FB59, kKey34 = 0xA78925D9, kKey6 = 0x9385, kKey7 = 0x0116, kKey8 = 0x6576;
const uint32_t kSector = 0x800;

uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
void Xor(uint8_t* p, size_t words, uint32_t key, uint32_t step) {
    for (size_t i = 0; i < words; ++i) {
        const uint32_t next = key * kKey1 + step;
        uint32_t v = U32(p + 4 * i) ^ key;
        memcpy(p + 4 * i, &v, 4);
        key = next;
    }
}
uint32_t StrCode24(const std::string& s) {
    uint32_t id = 0;
    for (unsigned char c : s) { id = ((id << 5) | (id >> 19)) & 0xFFFFFF; id = (id + c) & 0xFFFFFF; }
    return id ? id : 1;
}
}  // namespace

namespace {
// every resource of `type` (the top byte of its entry) in the stages `want` accepts, handed to `got`
// (stage name, resource hash, bytes); false when the file is not a STAGE_N.DAT (err says why)
bool ForEachResource(const std::string& host, long long base, long long size, uint8_t type,
                     const std::function<bool(const std::string&)>& want,
                     const std::function<void(const std::string&, uint32_t, std::vector<uint8_t>&&)>& got, std::string& err) {
    FILE* file = nullptr;
    if (fopen_s(&file, host.c_str(), "rb") != 0 || !file) { err = "cannot open " + host; return false; }
    struct Closer { FILE* f; ~Closer() { fclose(f); } } closer{file};
    struct Span { FILE* f; long long base, size; } f{file, base, size};
    auto ReadAt = [](const Span& s, long long at, void* dst, size_t n) {
        return at >= 0 && at + (long long)n <= s.size && _fseeki64(s.f, s.base + at, SEEK_SET) == 0 && fread(dst, 1, n, s.f) == n;
    };
    uint8_t head[16];
    if (!ReadAt(f, 0, head, 16)) { err = "too short"; return false; }
    const uint32_t key = U32(head);
    uint8_t peek[12];
    memcpy(peek, head + 4, 12);
    Xor(peek, 3, key, key ^ 0xF0F0);
    const uint16_t stages = (uint16_t)(peek[4] | peek[5] << 8);
    if (!stages || stages > 512) { err = "not a stage archive"; return false; }
    std::vector<uint8_t> table(12 + 20 * (size_t)stages);
    if (!ReadAt(f, 4, table.data(), table.size())) { err = "stage table cut"; return false; }
    Xor(table.data(), table.size() / 4, key, key ^ 0xF0F0);
    for (uint16_t s = 0; s < stages; ++s) {
        const uint8_t* e = &table[12 + 20 * (size_t)s];
        const std::string name((const char*)e, strnlen((const char*)e, 16));
        if (!want(name)) continue;
        const long long at = (long long)U32(e + 16) * kSector;
        const uint32_t hash = StrCode24(name);
        const uint32_t step = (hash << 7) + hash + kKey2, first = (hash << 7) + key + hash + kKey34;
        uint8_t cnt[4];
        if (!ReadAt(f, at, cnt, 4)) continue;
        Xor(cnt, 1, first, step);
        const uint32_t files = U32(cnt);
        if (!files || files > 100000) continue;
        std::vector<uint8_t> toc(4 + 8 * (size_t)files);
        if (!ReadAt(f, at, toc.data(), toc.size())) continue;
        Xor(toc.data(), toc.size() / 4, first, step);
        const long long data = at + ((toc.size() + kSector - 1) & ~(size_t)(kSector - 1));
        // a zlib block (its place and size), then its files (type << 24 | hash, offset): inflated only
        // when one of them is the script
        long long blockAt = -1; uint32_t blockSize = 0;
        std::vector<std::pair<uint32_t, uint32_t>> entries;
        auto flush = [&]() {
            bool script = false;
            for (auto& e : entries) script |= (e.first >> 24) == type;
            if (script && blockAt >= 0) {
                std::vector<uint8_t> z((blockSize + 3) & ~3u), block;
                if (ReadAt(f, blockAt, z.data(), z.size())) {
                    const uint32_t v = (uint32_t)(z[0] | z[1] << 8) ^ kKey6;
                    Xor(z.data(), z.size() / 4, v | ((v ^ kKey8) << 16), v * kKey7);
                    const uint16_t magic = (uint16_t)((z[0] | z[1] << 8) + (0x9C78 - kKey6));
                    z[0] = (uint8_t)magic; z[1] = (uint8_t)(magic >> 8);
                    if (ZlibInflate(z.data(), blockSize, block))
                        for (size_t k = 0; k < entries.size(); ++k) {
                            if ((entries[k].first >> 24) != type) continue;
                            const uint32_t b = entries[k].second, end = k + 1 < entries.size() ? entries[k + 1].second : (uint32_t)block.size();
                            if (b < end && end <= block.size()) got(name, entries[k].first & 0xFFFFFF, std::vector<uint8_t>(block.begin() + b, block.begin() + end));
                        }
                }
            }
            entries.clear();
            blockAt = -1;
        };
        for (uint32_t i = 0; i < files; ++i) {
            const uint32_t tag = U32(&toc[4 + 8 * (size_t)i]), off = U32(&toc[8 + 8 * (size_t)i]);
            if (!tag && !off) break;
            if ((tag & 0xFF000000) == 0x7F000000) flush();          // a section (or its end): the block before is done
            else if ((tag & 0xFF000000) == 0x7E000000) { flush(); blockAt = data + off; blockSize = tag & 0xFFFFFF; }
            else if (blockAt >= 0) entries.push_back({tag, off});
        }
        flush();
    }
    return true;
}
}  // namespace

bool StageDatScripts(const std::string& host, long long base, long long size, std::map<std::string, std::vector<uint8_t>>& out,
                     std::string& err) {
    if (!ForEachResource(host, base, size, 2, [](const std::string&) { return true; },
                         [&](const std::string& stage, uint32_t, std::vector<uint8_t>&& bytes) { out[stage] = std::move(bytes); }, err)) return false;
    if (out.empty()) { err = "no page script found"; return false; }
    return true;
}

bool StageDatFiles(const std::string& host, long long base, long long size, const std::string& stage, uint8_t type,
                   std::vector<std::vector<uint8_t>>& out, std::string& err) {
    out.clear();
    if (!ForEachResource(host, base, size, type, [&](const std::string& name) { return name == stage; },
                         [&](const std::string&, uint32_t, std::vector<uint8_t>&& bytes) { out.push_back(std::move(bytes)); }, err)) return false;
    if (out.empty()) { err = "no such resource in " + stage; return false; }
    return true;
}

uint32_t StageStrCode24(const std::string& s) { return StrCode24(s); }

void StageTocCrypt(uint8_t* p, size_t words, const std::string& stage, uint32_t key) {
    const uint32_t hash = StrCode24(stage);
    Xor(p, words, (hash << 7) + key + hash + kKey34, (hash << 7) + hash + kKey2);
}

void StageBlockDecrypt(uint8_t* z, size_t size) {
    if (size < 4) return;
    const uint32_t v = (uint32_t)(z[0] | z[1] << 8) ^ kKey6;
    Xor(z, size / 4, v | ((v ^ kKey8) << 16), v * kKey7);
    const uint16_t magic = (uint16_t)((z[0] | z[1] << 8) + (0x9C78 - kKey6));
    z[0] = (uint8_t)magic; z[1] = (uint8_t)(magic >> 8);
}

void StageBlockEncrypt(uint8_t* z, size_t size, uint16_t key5) {
    if (size < 4) return;
    const uint16_t first = (uint16_t)((z[0] | z[1] << 8) - (0x9C78 - kKey6));   // the zlib header 78 9C: kKey6
    z[0] = (uint8_t)first; z[1] = (uint8_t)(first >> 8);
    const uint32_t v = (uint32_t)key5 ^ kKey6;
    Xor(z, size / 4, v | ((v ^ kKey8) << 16), v * kKey7);
}

bool StageDatToc(const std::string& host, long long base, long long size, const std::string& stage, StageToc& out, std::string& err) {
    FILE* file = nullptr;
    if (fopen_s(&file, host.c_str(), "rb") != 0 || !file) { err = "cannot open " + host; return false; }
    struct Closer { FILE* f; ~Closer() { fclose(f); } } closer{file};
    auto ReadAt = [&](long long at, void* dst, size_t n) {
        return at >= 0 && at + (long long)n <= size && _fseeki64(file, base + at, SEEK_SET) == 0 && fread(dst, 1, n, file) == n;
    };
    uint8_t head[16];
    if (!ReadAt(0, head, 16)) { err = "too short"; return false; }
    const uint32_t key = U32(head);
    uint8_t peek[12];
    memcpy(peek, head + 4, 12);
    Xor(peek, 3, key, key ^ 0xF0F0);
    const uint16_t stages = (uint16_t)(peek[4] | peek[5] << 8);
    if (!stages || stages > 512) { err = "not a stage archive"; return false; }
    std::vector<uint8_t> table(12 + 20 * (size_t)stages);
    if (!ReadAt(4, table.data(), table.size())) { err = "stage table cut"; return false; }
    Xor(table.data(), table.size() / 4, key, key ^ 0xF0F0);
    for (uint16_t s = 0; s < stages; ++s) {
        const uint8_t* e = &table[12 + 20 * (size_t)s];
        if (std::string((const char*)e, strnlen((const char*)e, 16)) != stage) continue;
        const long long at = (long long)U32(e + 16) * kSector;
        uint8_t cnt[4];
        if (!ReadAt(at, cnt, 4)) { err = "stage cut"; return false; }
        StageTocCrypt(cnt, 1, stage, key);
        const uint32_t files = U32(cnt);
        if (!files || files > 100000) { err = "stage table of contents unreadable"; return false; }
        std::vector<uint8_t> toc(4 + 8 * (size_t)files);
        if (!ReadAt(at, toc.data(), toc.size())) { err = "stage table of contents cut"; return false; }
        StageTocCrypt(toc.data(), toc.size() / 4, stage, key);
        out.key = key; out.sector = U32(e + 16); out.entries.clear();
        for (uint32_t i = 0; i < files; ++i) out.entries.push_back({U32(&toc[4 + 8 * (size_t)i]), U32(&toc[8 + 8 * (size_t)i])});
        return true;
    }
    err = "no stage " + stage;
    return false;
}
}  // namespace ps2world
