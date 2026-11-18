// The disc's files: a folder or an ISO 9660 image (disc.h).
#include "disc.h"
#include "ps2world.h"
#include "inflate.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

namespace ps2world {

extern const uint64_t kBaseImageHash;
extern const uint32_t kBaseImageSize;

const char* const kDiscFiles[] = {
    "MGS\\NET\\MGS3_N.ELF", "MGS\\NET\\STAGE_N.DAT", "MGS\\NET\\SLOT_N.DAT", "MGS\\BGM_2.DAT", "MGS\\VOX_2.DAT",
    "MGS\\CODEC_2.DAT", "MGS\\DEMO_2.DAT", "MODULES\\MODNCD.IPK",
};
const int kDiscFileCount = sizeof(kDiscFiles) / sizeof(kDiscFiles[0]);
int DiscFileCount() { return kDiscFileCount; }
const char* DiscFileName(int i) { return i >= 0 && i < kDiscFileCount ? kDiscFiles[i] : ""; }

namespace {

struct Mount {
    std::string where;
    bool image = false;
    std::map<std::string, std::pair<long long, long long>> paths;   // image: disc path -> (offset, size)
    std::map<std::string, std::string> names;                        // name -> first disc path (image) or host path (folder)
};
std::mutex gLock;
Mount gMount;

std::string Upper(std::string s) {
    for (auto& c : s) { if (c == '/') c = '\\'; c = (char)toupper((unsigned char)c); }
    while (!s.empty() && s[0] == '\\') s.erase(0, 1);
    if (const size_t v = s.find(';'); v != std::string::npos) s.resize(v);
    return s;
}
std::string NameOf(const std::string& p) { const size_t k = p.find_last_of('\\'); return k == std::string::npos ? p : p.substr(k + 1); }

bool ReadAt(FILE* f, long long at, void* dst, size_t n) {
    return _fseeki64(f, at, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
}

// ISO 9660: the primary volume descriptor at sector 16, then the directory tree from its root
void WalkIso(FILE* f, uint32_t lba, uint32_t size, const std::string& dir, Mount& m, int depth) {
    if (depth > 8 || size > (16u << 20)) return;
    std::vector<uint8_t> d(size);
    if (!ReadAt(f, (long long)lba * 2048, d.data(), size)) return;
    for (size_t pos = 0; pos < d.size();) {
        const uint8_t n = d[pos];
        if (n == 0) { pos = (pos / 2048 + 1) * 2048; continue; }       // the rest of the sector is padding
        if (pos + n > d.size() || n < 34) break;
        const uint8_t* r = &d[pos];
        uint32_t ext, len; memcpy(&ext, r + 2, 4); memcpy(&len, r + 10, 4);
        const uint8_t flags = r[25], nl = r[32];
        pos += n;
        if (nl == 1 && (r[33] == 0 || r[33] == 1)) continue;            // . and ..
        const std::string name = Upper(std::string((const char*)r + 33, nl));
        const std::string path = dir.empty() ? name : dir + "\\" + name;
        if (flags & 2) WalkIso(f, ext, len, path, m, depth + 1);
        else {
            m.paths[path] = {(long long)ext * 2048, (long long)len};
            m.names.emplace(name, path);
        }
    }
}

bool MountImage(const std::string& iso, Mount& m) {
    FILE* f = nullptr;
    if (fopen_s(&f, iso.c_str(), "rb") != 0 || !f) return false;
    uint8_t pvd[2048];
    const bool ok = ReadAt(f, 16 * 2048, pvd, sizeof pvd) && pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0;
    if (ok) {
        uint32_t lba, size; memcpy(&lba, pvd + 156 + 2, 4); memcpy(&size, pvd + 156 + 10, 4);
        WalkIso(f, lba, size, "", m, 0);
    }
    fclose(f);
    return ok && !m.paths.empty();
}

// a folder: the files the game reads, looked for by name under it (three levels at most)
void IndexFolder(const std::string& dir, Mount& m, int depth, int& budget) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (--budget < 0) break;
        const std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 3) IndexFolder(dir + "\\" + n, m, depth + 1, budget); }
        else m.names.emplace(Upper(n), dir + "\\" + n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

bool IsFile(const std::string& p) {
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool Load(const std::string& where, Mount& m) {
    m = Mount{};
    m.where = where;
    while (m.where.size() > 3 && (m.where.back() == '\\' || m.where.back() == '/')) m.where.pop_back();
    if (IsFile(m.where)) { m.image = true; return MountImage(m.where, m); }
    const DWORD a = GetFileAttributesA(m.where.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return false;
    int budget = 5000;
    IndexFolder(m.where, m, 0, budget);
    return true;
}

bool Find(const Mount& m, const std::string& rel, DiscSpan& out) {
    const std::string p = Upper(rel);
    if (m.image) {
        auto it = m.paths.find(p);
        if (it == m.paths.end()) {
            auto nt = m.names.find(NameOf(p));
            if (nt == m.names.end()) return false;
            it = m.paths.find(nt->second);
        }
        out.host = m.where; out.base = it->second.first; out.size = it->second.second;
        return true;
    }
    std::string host = m.where + "\\" + p;                        // as on the disc, then by its name
    if (!IsFile(host)) {
        auto nt = m.names.find(NameOf(p));
        if (nt == m.names.end()) return false;
        host = nt->second;
    }
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(host.c_str(), GetFileExInfoStandard, &fa)) return false;
    out.host = host; out.base = 0; out.size = ((long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    return true;
}

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}

}  // namespace

bool DiscMount(const std::string& where) {
    std::lock_guard<std::mutex> g(gLock);
    if (!gMount.where.empty() && gMount.where == where) return true;
    return Load(where, gMount);
}
const std::string& DiscWhere() { return gMount.where; }
bool DiscIsImage() { return gMount.image; }
bool DiscFind(const std::string& rel, DiscSpan& out) { std::lock_guard<std::mutex> g(gLock); return Find(gMount, rel, out); }

bool DiscFindIn(const std::string& where, const std::string& rel, DiscSpan& out) {
    Mount m;
    return !where.empty() && Load(where, m) && Find(m, rel, out);
}

FILE* DiscOpen(const std::string& rel, DiscSpan& span) {
    if (!DiscFind(rel, span)) return nullptr;
    FILE* f = nullptr;
    if (fopen_s(&f, span.host.c_str(), "rb") != 0 || !f) return nullptr;
    _fseeki64(f, span.base, SEEK_SET);
    return f;
}

bool ElfBaseHash(const std::vector<uint8_t>& e, uint64_t& hash, size_t& size, uint64_t* versionHash, std::vector<uint8_t>* image) {
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= e.size()) memcpy(&v, &e[o], 4); return v; };
    if (e.size() < 0x100 || u32(0) != 0x464c457f) return false;
    const uint32_t ph = u32(0x1C), off = u32(ph + 4), va = u32(ph + 8), fsz = u32(ph + 16);
    if (va != 0x800000 || fsz < 8 || (size_t)off + fsz > e.size()) return false;
    std::vector<uint8_t> img;
    if (!ZlibInflate(&e[off + 4], fsz - 4, img) || img.size() < 8) return false;
    if (u32(0x18) == 0x886D58 && 0x889D78 - va + off <= e.size()) {   // the US stub: the XOR of the word at +4
        uint32_t sum = 0;
        for (uint32_t a = 0x886D50; a < 0x889D78; a += 4) sum += u32(off + a - va);
        uint32_t w; memcpy(&w, &img[4], 4); w ^= sum; memcpy(&img[4], &w, 4);
    }
    hash = Fnv(img.data(), img.size());
    size = img.size();
    if (versionHash) {                                     // the platform's (BootElf): the word at +4 left out
        uint64_t h = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < img.size(); ++i) { h ^= (i >= 4 && i < 8) ? 0 : img[i]; h *= 0x100000001b3ull; }
        *versionHash = h;
    }
    if (image) image->swap(img);
    return true;
}

const char* MatchProgram(const uint8_t* img, size_t size, int* percent) {
    if (percent) *percent = 0;
    if (!img || size < 8) return nullptr;
    auto fnv = [&](size_t from, size_t n) {              // the image's bytes, the word at +4 left out
        uint64_t h = 0xcbf29ce484222325ull;
        for (size_t i = from; i < from + n; ++i) { h ^= (i >= 4 && i < 8) ? 0 : img[i]; h *= 0x100000001b3ull; }
        return h;
    };
    const uint64_t whole = fnv(0, size);
    for (int i = 0; i < kVersionBaseCount; ++i)
        if (kVersionBases[i].hash == whole && kVersionBases[i].size == size) { if (percent) *percent = 100; return kVersionBases[i].version; }
    // a patched copy (SaveMGO's patch, a fix): the same size, most blocks the same
    const size_t kBlock = 4096, blocks = (size + kBlock - 1) / kBlock;
    std::vector<uint64_t> mine(blocks);
    for (size_t b = 0; b < blocks; ++b) mine[b] = fnv(b * kBlock, std::min(kBlock, size - b * kBlock));
    const char* best = nullptr; int bestPc = 0;
    for (int i = 0; i < kVersionBaseCount; ++i) {
        const VersionBase& v = kVersionBases[i];
        if (v.size != size || v.blockCount != blocks || !v.blocks) continue;
        size_t same = 0;
        for (size_t b = 0; b < blocks; ++b) same += v.blocks[b] == mine[b];
        const int pc = (int)(same * 100 / blocks);
        if (pc > bestPc) { bestPc = pc; best = v.version; }
    }
    if (percent) *percent = bestPc;
    return bestPc >= 90 ? best : nullptr;
}

DiscReport DiscCheck(const std::string& where) {
    DiscReport r;
    Mount m;
    if (where.empty() || !Load(where, m)) { r.missing.assign(kDiscFiles, kDiscFiles + kDiscFileCount); return r; }
    for (int i = 0; i < kDiscFileCount; ++i) {
        DiscSpan s;
        if (!Find(m, kDiscFiles[i], s)) r.missing.push_back(kDiscFiles[i]);
    }
    DiscSpan s;
    if (Find(m, "MGS\\NET\\MGS3_N.ELF", s) && s.size > 0 && s.size < (8 << 20)) {
        std::vector<uint8_t> e((size_t)s.size);
        FILE* f = nullptr;
        if (fopen_s(&f, s.host.c_str(), "rb") == 0 && f) {
            const bool read = ReadAt(f, s.base, e.data(), e.size());
            fclose(f);
            uint64_t h = 0, hv = 0; size_t n = 0;
            std::vector<uint8_t> img;
            if (read && ElfBaseHash(e, h, n, &hv, &img)) {
                // a program this build was recompiled for (kVersionBases: docs/VERSIONS.md) runs, a
                // patched copy of one too (MatchProgram)
                int pc = 0;
                if (const char* v = MatchProgram(img.data(), img.size(), &pc)) { r.region = v; r.runs = true; r.patched = pc < 100; }
                // else the programs known by their hash: the three retail discs, the Japanese standalone disc
                // (SLPM 685.24), the Korean disc (SLKA 253.54), the EU Online Beta Test
                if (!r.runs) {
                    if (h == kBaseImageHash && n == kBaseImageSize) r.region = "US";
                    else if (h == 0x2cb6db4e65b5fd53ull) r.region = "EU";
                    else if (h == 0xe8dd2cf40703cfe8ull) r.region = "JP";
                    else if (h == 0xddf4b04a1fa68944ull) r.region = "JPS";
                    else if (h == 0xc3bf2cafbe81da88ull) r.region = "KR";       // SLKA 253.54
                    else if (h == 0x3ddf9977466e927eull) r.region = "EU_TRIAL";
                    else r.region = "?";
                }
            } else r.region = "?";
        }
    }
    r.ok = r.runs && r.missing.empty();
    return r;
}

}  // namespace ps2world
