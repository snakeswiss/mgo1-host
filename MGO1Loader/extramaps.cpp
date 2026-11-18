// extramaps.cpp - maps 13 and up (extramaps.h, docs/EXTRA_MAPS.md): each map's folder (map.ini and the
// stage files it replaces, named as in the stage: a7b7fb.tri, f8c6c7.geom ...), its stage rebuilt from
// this version's own: the blocks that hold a replaced file inflated, the files put in (16-byte aligned,
// in the stage's order), the script's listed procedures taken from the map's script into this
// version's, the block compressed again (deflate.cpp) and crypted with its own key; the table of
// contents follows (sizes, places), the other blocks copied as they are. A map's rules\ files go into the
// rules pages (the resident r_* pages) the same way, a page rebuilt when a round of that map first asks for it.
#include "extramaps.h"
#include "deflate.h"
#include "inflate.h"
#include "png.h"
#include "ps2rt.h"
#include "stagedat.h"
#include "spu2.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace ps2world {
namespace {
void (*gLogFn)(const char* fmt, ...) = nullptr;
#define Log(...) do { if (gLogFn) gLogFn(__VA_ARGS__); } while (0)

const uint32_t kSector = 0x800;

uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
void Put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
uint32_t Up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
    return h;
}

// the stage files' types (the top byte of an entry's tag) by their extension
int TypeOf(const std::string& ext) {
    static const std::map<std::string, int> kTypes = {
        {"sar", 0x00}, {"bin", 0x01}, {"gcx", 0x02}, {"tri", 0x03}, {"mdh", 0x04}, {"mds", 0x05}, {"lt2", 0x06}, {"cv2", 0x07},
        {"mtar", 0x08}, {"mtsq", 0x09}, {"mtfa", 0x0a}, {"mtcm", 0x0b}, {"geom", 0x0c}, {"mdl", 0x0d}, {"nav", 0x0f}, {"cvd", 0x10},
        {"eft", 0x11}, {"zon", 0x12}, {"rpd", 0x13}, {"ico", 0x5e}, {"la2", 0x5f}, {"var", 0x68}, {"img", 0x69}, {"vib", 0x6a},
        {"rat", 0x6b}, {"rcm", 0x6c}, {"ola", 0x6d}, {"raw", 0x6e}, {"mtra", 0x6f}};
    auto it = kTypes.find(ext);
    if (it != kTypes.end()) return it->second;
    if (ext.size() == 2 && isxdigit((unsigned char)ext[0]) && isxdigit((unsigned char)ext[1])) return (int)strtoul(ext.c_str(), nullptr, 16);
    return -1;
}
// the stages of the map numbers the rotation holds (proc6 of ntmenu2, proc37 of the maps' scripts)
const char* const kStages[12] = {"n001a", "n002a", "n003a", "n011a", "n012a", "n013a", "n021a", "n022a", "n024a", "n031a", "n032a", "n033a"};

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    _fseeki64(f, 0, SEEK_END);
    const long long n = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const bool ok = n >= 0 && (n == 0 || fread(out.data(), 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    return ok;
}
std::string Trim(std::string s) {
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t k = 0; while (k < s.size() && isspace((unsigned char)s[k])) ++k;
    return s.substr(k);
}

// ---- the 2D map (docs/EXTRA_MAPS.md). MGO1 draws a stage's map from the corner [0, 200) x [0, 200) of the texture
// map_stage_nNN_alp_ovl (PSMT4, its palette white at levels of alpha) of the bank online_map_0 (ef0f5a.tri: its
// GS image in PSMCT32 rows of 64 words from block 0, then a page of palettes); the world's point x, z is the
// texture's pixel x s + cx, z s + cy, the constants of the stage's page (98.5, 72.5, 0.00235 for City Under
// Siege's n003a, -205, 620, 0.00255 for Mountaintop's n022a). A map brings its pictures (minimap.png: all its
// floors; minimap_<n>.png: floor n brought forward) and its cx, cy, s; the stage's bank and page get them
const uint32_t kMapBankTag = 0x03000000u | 0xef0f5au;
const int kMapPx = 200;
// the stages whose page holds its own cx, cy, s as three floats read by that stage's case of the page's 2D map
// setter only (US 0x3ADC58 in n003a's page); their bits as in the page (0.00255 is 0x3B271DE6 there, not the
// 0x3B271DE7 of a 0.00255f literal)
struct StageTransform { const char* stage; uint32_t bits[3]; };
const StageTransform kStageTransforms[] = {
    {"n003a", {0x42C50000u, 0x42910000u, 0x3B1A0275u}},     // City Under Siege: 98.5, 72.5, 0.00235 (US 0x548008)
    {"n022a", {0xC34D0000u, 0x441B0000u, 0x3B271DE6u}},     // Mountaintop: -205, 620, 0.00255 (US 0x53E314)
};
struct Minimap {
    float t[3] = {0, 0, 0};                                    // cx, cy, s (map.ini minimap=)
    std::vector<std::pair<float, std::string>> floors;         // each floor's lowest height and its name (map.ini floors=)
    std::vector<std::vector<uint8_t>> alpha;                   // the pictures, GS alphas (0x80 opaque), 200 x 200
    std::vector<std::vector<uint8_t>> index;                   // the same as the texture's indices (the bank built)
    std::vector<uint32_t> nibble;                              // each pixel's nibble in the bank's image: byte * 2 + 1 when high
    std::vector<std::vector<uint8_t>> probe;                   // the image's bytes at probeAt with each picture in
    uint32_t imageSize = 0, probeAt = 0;
    bool want = false, ok = false;
};

using Files = std::map<uint32_t, std::vector<uint8_t>>;     // tag (type << 24 | name hash) -> its bytes
// a stage rebuilt: its table of contents, then its data, from its first sector in STAGE_N.DAT
struct Image { uint32_t sector = 0; std::vector<uint8_t> bytes; std::vector<std::pair<uint32_t, uint32_t>> toc; };

struct MapData {
    ExtraMap m;
    Minimap mm;
    Files files;                                              // the stage's files it replaces
    std::vector<std::pair<int, uint64_t>> scripts;            // the script's procedures it brings: index, the hash of the one replaced
    std::vector<std::pair<int, uint64_t>> drops;              // the stage script's procedures it empties (map.ini drop=): the same
    // points.txt: groups of points added to the geom's tree (their hash, and each point's hash and x, y, z)
    struct Point { uint32_t hash; float x, y, z; };
    std::vector<std::pair<uint32_t, std::vector<Point>>> points;
    std::vector<uint8_t> pointsText;
    // the rules pages' files (rules\: put in every rules page that holds them; rules\<page>\: that page's, over those)
    Files rules;
    std::map<std::string, Files> rulesOf;
    // map.ini texts= (every rule) and texts0= ... texts4= (one rule, over those): the game's texts shown otherwise
    // on this map ("OLD:NEW|OLD:NEW"); scripttexts= (and scripttexts0= ...): parts of the scripts' texts (the
    // weapons' descriptions: "OLD=>NEW|..."); both with \n and \xNN
    std::vector<std::pair<std::string, std::string>> texts[6], scriptTexts[6];
    // map.ini pagefloats= ("OLD,...:NEW,...|..."): runs of floats of the stage's page given other values (the first
    // person view's hand targets of a weapon); each run found once in the running version's page
    std::vector<std::pair<std::vector<float>, std::vector<float>>> pageFloats;
    std::vector<int> laser;                                   // map.ini laser=: the weapons given the Scorpion's laser sight
    // map.ini addto= ("NEW:ANCHOR|..."): a file the stage or rules page does not hold, added to the block that holds
    // the anchor (a new model beside the one it varies); without its anchor in a page it is not added there
    std::map<uint32_t, uint32_t> addTo;
    std::vector<AddedWeapon> weapons;                         // weapons.ini: the weapons added on this map
    Image img;                                                // the rebuilt stage
    uint64_t content = 0;                                     // the hash of its files and procedures (the dialect's signature)
    bool ok = false;
};
std::vector<MapData> gMaps;
std::vector<ExtraMap> gPublic;
std::mutex gLock;
// open list: the builder ends long before the program but is joined only if waited for; a joinable std::thread
// destroyed at exit (the DLL's static destructors under ExitProcess) calls std::terminate
struct BuilderThread : std::thread {
    BuilderThread& operator=(std::thread&& t) { std::thread::operator=(std::move(t)); return *this; }
    ~BuilderThread() { if (joinable()) detach(); }
};
BuilderThread gBuilder;
std::atomic<bool> gBuilt{false};
std::atomic<int> gServed{-1};                                // gMaps index whose stage is served, -1 none
std::atomic<const Image*> gServedImage{nullptr};             // the stage or rules page served (its sectors read from it)
int gLoaded = 0;
// a map's rules pages, each rebuilt when a round of that map first asks for it (kept: their places stay)
struct RulesPage { int id = 0; std::string page; Image img; bool ok = false; };
std::list<RulesPage> gPages;
std::mutex gPagesLock;
int gRulesLoaded = 0;                                         // the map whose rules page the page loaded last is (0: the stock one)
std::string gHost; long long gBase = 0, gSize = 0;

// a script (gcx): its definitions' words (type << 24 | offset in the script section), the section's
// physical pieces, what comes after it (the main procedure, the tail)
struct Gcx {
    size_t defsAt = 4, sectAt = 0;                           // the definitions; the script section's length word
    std::vector<uint32_t> defs;
    std::vector<uint32_t> starts;                            // the pieces' offsets, sorted
    std::vector<std::vector<uint8_t>> pieces;
    std::vector<uint8_t> head, after;
    bool Read(const std::vector<uint8_t>& d, std::string& err) {
        size_t p = 4;
        while (true) {
            if (p + 4 > d.size()) { err = "definitions cut"; return false; }
            const uint32_t v = U32(&d[p]); p += 4;
            if (v == 0xFFFFFFFFu) break;
            defs.push_back(v);
            if (defs.size() > 4096) { err = "too many definitions"; return false; }
        }
        const size_t hp = p;
        if (hp + 20 > d.size()) { err = "header cut"; return false; }
        const int32_t sect = (int32_t)U32(&d[hp]);
        if (sect < 20 || hp + (size_t)sect + 4 > d.size()) { err = "script section outside"; return false; }
        sectAt = hp + (size_t)sect;
        const uint32_t len = U32(&d[sectAt]);
        if (sectAt + 4 + (size_t)len > d.size()) { err = "script section cut"; return false; }
        for (uint32_t v : defs) starts.push_back(v & 0xFFFFFF);
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (size_t k = 0; k < starts.size(); ++k) {
            const uint32_t b = starts[k], e = k + 1 < starts.size() ? starts[k + 1] : len;
            if (b > e || e > len) { err = "procedure outside its section"; return false; }
            pieces.emplace_back(d.begin() + sectAt + 4 + b, d.begin() + sectAt + 4 + e);
        }
        head.assign(d.begin(), d.begin() + sectAt);
        after.assign(d.begin() + sectAt + 4 + len, d.end());
        return true;
    }
    int PieceOf(size_t def) const {
        const uint32_t off = defs[def] & 0xFFFFFF;
        return (int)(std::lower_bound(starts.begin(), starts.end(), off) - starts.begin());
    }
    int Sharing(size_t def) const {                          // definitions on this one's piece
        int n = 0;
        for (uint32_t v : defs) n += (v & 0xFFFFFF) == (defs[def] & 0xFFFFFF);
        return n;
    }
    std::vector<uint8_t> Write() const {
        std::vector<uint8_t> out = head;
        std::vector<uint32_t> at(pieces.size());
        uint32_t len = 0;
        for (size_t k = 0; k < pieces.size(); ++k) { at[k] = len; len += (uint32_t)pieces[k].size(); }
        for (size_t i = 0; i < defs.size(); ++i) {
            const int k = PieceOf(i);
            Put32(&out[defsAt + 4 * i], (defs[i] & 0xFF000000u) | (at[(size_t)k] & 0xFFFFFF));
        }
        const size_t l = out.size();
        out.resize(l + 4);
        Put32(&out[l], len);
        for (auto& pc : pieces) out.insert(out.end(), pc.begin(), pc.end());
        out.insert(out.end(), after.begin(), after.end());
        return out;
    }
};

// this version's script with the map's procedures in it; false (err) when one of the procedures it
// replaces is not the one the map was made against
bool Transplant(const std::vector<uint8_t>& own, const std::vector<uint8_t>& theirs, const MapData& md, std::vector<uint8_t>& out, std::string& err) {
    Gcx a, b;
    if (!a.Read(own, err)) { err = "this version's script: " + err; return false; }
    if (!b.Read(theirs, err)) { err = "the map's script: " + err; return false; }
    for (auto& s : md.scripts) {
        const size_t i = (size_t)s.first;
        if (i >= a.defs.size() || i >= b.defs.size()) { char e[96]; snprintf(e, sizeof e, "procedure %zu missing", i); err = e; return false; }
        if (a.Sharing(i) != 1 || b.Sharing(i) != 1) { char e[96]; snprintf(e, sizeof e, "procedure %zu shares its code", i); err = e; return false; }
        auto& mine = a.pieces[(size_t)a.PieceOf(i)];
        if (s.second && Fnv(mine.data(), mine.size()) != s.second) {
            char e[128]; snprintf(e, sizeof e, "procedure %zu of this version is not the one the map replaces", i); err = e; return false;
        }
        mine = b.pieces[(size_t)b.PieceOf(i)];
    }
    for (auto& s : md.drops) {                               // a procedure emptied: a block that holds its end alone
        const size_t i = (size_t)s.first;
        if (i >= a.defs.size() || a.Sharing(i) != 1) { char e[96]; snprintf(e, sizeof e, "procedure %zu to empty missing", i); err = e; return false; }
        auto& mine = a.pieces[(size_t)a.PieceOf(i)];
        if (s.second && Fnv(mine.data(), mine.size()) != s.second) {
            char e[128]; snprintf(e, sizeof e, "procedure %zu of this version is not the one the map empties", i); err = e; return false;
        }
        mine = {0x81, 0x00};
    }
    out = a.Write();
    return true;
}
// the geom (its chunk 7: a tree of nodes {next, child, hash, flags; data}, file offsets; a point's flags 2: its x, y,
// z, 1 after the node) with the map's points.txt: each group a node of flags 0x80000000 after the last top node,
// its points as its children; the game walks the tree from its root (0x160E88) and relocates it the same way
bool AddPoints(std::vector<uint8_t>& geom, const MapData& md, std::string& err) {
    if (md.points.empty()) return true;
    if (geom.size() < 0x10) { err = "the geom is cut"; return false; }
    const bool sized = U32(&geom[4]) == geom.size();
    const uint32_t chunks = U32(&geom[8]);
    uint32_t root = 0;
    for (uint32_t i = 0; i < chunks && 0x10 + 12 * (size_t)(i + 1) <= geom.size(); ++i)
        if (U32(&geom[0x10 + 12 * i + 8]) == 7) root = U32(&geom[0x10 + 12 * i + 4]);
    if (!root || (size_t)root + 16 > geom.size()) { err = "the geom has no tree of points"; return false; }
    uint32_t last = root;
    for (int guard = 0; U32(&geom[last]) && guard < 100000; ++guard) {
        last = U32(&geom[last]);
        if ((size_t)last + 16 > geom.size()) { err = "the geom's tree goes out of it"; return false; }
    }
    geom.resize(Up((uint32_t)geom.size(), 16), 0);
    uint32_t prevGroup = last;
    for (auto& g : md.points) {
        const uint32_t at = (uint32_t)geom.size();
        geom.resize(at + 16 + 32 * g.second.size(), 0);
        Put32(&geom[prevGroup], at);                             // linked after the last top node, or the group before
        Put32(&geom[at + 4], g.second.empty() ? 0 : at + 16);
        Put32(&geom[at + 8], g.first);
        Put32(&geom[at + 12], 0x80000000u);
        for (size_t k = 0; k < g.second.size(); ++k) {
            const uint32_t n = at + 16 + 32 * (uint32_t)k;
            const MapData::Point& pt = g.second[k];
            const float w = 1.0f;
            Put32(&geom[n], k + 1 < g.second.size() ? n + 32 : 0);
            Put32(&geom[n + 8], pt.hash);
            Put32(&geom[n + 12], 2);
            memcpy(&geom[n + 16], &pt.x, 4); memcpy(&geom[n + 20], &pt.y, 4); memcpy(&geom[n + 24], &pt.z, 4); memcpy(&geom[n + 28], &w, 4);
        }
        prevGroup = at;
    }
    if (sized) Put32(&geom[4], (uint32_t)geom.size());         // the header's size word
    return true;
}
// points.txt: "group <hash>" then "<hash> <x> <y> <z>" lines; ';' or '#' comments
bool ReadPoints(const std::string& path, MapData& md, std::string& err) {
    if (!ReadFile(path, md.pointsText)) return true;         // none: nothing added
    std::string text(md.pointsText.begin(), md.pointsText.end());
    size_t p = 0; int line = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p); if (e == std::string::npos) e = text.size();
        std::string l = Trim(text.substr(p, e - p)); p = e + 1; ++line;
        const size_t c = l.find_first_of(";#"); if (c != std::string::npos) l = Trim(l.substr(0, c));
        if (l.empty()) continue;
        unsigned h = 0; float x, y, z;
        if (sscanf(l.c_str(), "group %x", &h) == 1) { md.points.push_back({h & 0xFFFFFF, {}}); continue; }
        if (sscanf(l.c_str(), "%x %f %f %f", &h, &x, &y, &z) == 4 && !md.points.empty()) { md.points.back().second.push_back({h & 0xFFFFFF, x, y, z}); continue; }
        char b[96]; snprintf(b, sizeof b, "points.txt line %d not read", line); err = b; return false;
    }
    return true;
}

// the pictures of a map's folder: minimap.png, then minimap_1.png ... for the floors (all or none)
bool MinimapLoad(const std::string& dir, Minimap& mm, std::string& err) {
    auto one = [&](const std::string& name, std::vector<uint8_t>& out) {
        std::vector<uint32_t> rgba;
        int w = 0, h = 0;
        std::string e;
        if (!ps2::PngRead(dir + "\\" + name, rgba, w, h, e)) { err = name + ": " + e; return false; }
        if (w != kMapPx || h != kMapPx) { err = name + " is not 200 x 200"; return false; }
        out.resize(rgba.size());
        for (size_t i = 0; i < rgba.size(); ++i) out[i] = (uint8_t)(((rgba[i] >> 24) + 1) / 2);   // 255 -> 0x80
        return true;
    };
    mm.alpha.assign(1, {});
    if (!one("minimap.png", mm.alpha[0])) return false;
    for (size_t k = 0; k < mm.floors.size(); ++k) {
        std::vector<uint8_t> a;
        char n[32]; snprintf(n, sizeof n, "minimap_%zu.png", k + 1);
        if (!one(n, a)) { mm.alpha.resize(1); mm.floors.clear(); Log("extra maps: %s: its floors left out (%s)", dir.c_str(), err.c_str()); err.clear(); break; }
        mm.alpha.push_back(std::move(a));
    }
    return true;
}
// the bank online_map_0 of the stage with the map's picture of all its floors in the texture's corner (the
// palette's free colors given the alphas it lacks); each picture's indices and nibbles kept for the floors
bool MinimapBank(Minimap& mm, const std::string& stage, std::vector<uint8_t>& tri, std::string& err) {
    if (tri.size() < 0x20 || stage.size() < 4) { err = "the map bank is cut"; return false; }
    const uint32_t count = U32(&tri[0x10]), imageAt = U32(&tri[0x18]), clutAt = U32(&tri[0x1C]);
    if (0x20 + (size_t)count * 0xA0 > imageAt || imageAt >= clutAt || (clutAt - imageAt) % 256 || (size_t)clutAt + 0x2000 > tri.size()) {
        err = "the map bank's layout is not known"; return false;
    }
    char name[32]; snprintf(name, sizeof name, "map_stage_n%c%c_alp_ovl", stage[2], stage[3]);
    const uint32_t code = StageStrCode24(name);
    uint64_t tex0 = 0;
    for (uint32_t i = 0; i < count && !tex0; ++i)
        if ((U32(&tri[0x20 + i * 0xA0 + 0x10]) & 0xFFFFFF) == code) memcpy(&tex0, &tri[0x20 + i * 0xA0 + 0x50], 8);
    if (!tex0) { err = std::string("no texture ") + name + " in the map bank"; return false; }
    const uint32_t tbp = tex0 & 0x3fff, tbw = (tex0 >> 14) & 0x3f, psm = (tex0 >> 20) & 0x3f, cbp = (tex0 >> 37) & 0x3fff;
    const uint32_t cpsm = (tex0 >> 51) & 15, csm = (tex0 >> 55) & 1, csa = (tex0 >> 56) & 31;
    if (psm != 0x14 || cpsm || csm || (1u << ((tex0 >> 26) & 15)) < (uint32_t)kMapPx || (1u << ((tex0 >> 30) & 15)) < (uint32_t)kMapPx) {
        err = std::string(name) + " is not the 4-bit texture of 256 x 256 it should be"; return false;
    }
    // the words of the image and of the palettes' page: GS word address -> its place in the file
    const uint32_t rows = (clutAt - imageAt) / 256;
    auto places = [](uint32_t n) {
        std::vector<int32_t> at;
        for (uint32_t y = 0; y < n; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                const uint32_t w = ps2::GsPixelAddress(0x00, (int)x, (int)y, 0, 1);
                if (w >= at.size()) at.resize(w + 1, -1);
                at[w] = (int32_t)(y * 64 + x);
            }
        return at;
    };
    const std::vector<int32_t> img = places(rows), pal = places(32);
    mm.nibble.resize((size_t)kMapPx * kMapPx);
    for (int y = 0; y < kMapPx; ++y)
        for (int x = 0; x < kMapPx; ++x) {
            const uint32_t a = ps2::GsPixelAddress(0x14, x, y, tbp, tbw), w = a >> 3;
            if (w >= img.size() || img[w] < 0) { err = "the map texture is outside the bank's image"; return false; }
            mm.nibble[(size_t)y * kMapPx + x] = ((uint32_t)img[w] * 4 + ((a >> 1) & 3)) * 2 + (a & 1);
        }
    uint8_t* image = &tri[imageAt];
    auto get = [&](uint32_t nb) { return (uint8_t)((image[nb >> 1] >> ((nb & 1) * 4)) & 15); };
    auto put = [&](uint8_t* im, uint32_t nb, uint8_t v) { uint8_t& b = im[nb >> 1]; b = (nb & 1) ? (uint8_t)((b & 0x0F) | v << 4) : (uint8_t)((b & 0xF0) | v); };
    // the palette of the texture (CSA csa of the 256 colors at cbp, their order CSM1's)
    auto entry = [&](int i) -> uint8_t* {
        const int e = (int)csa * 16 + i, sw = (e & ~0x18) | ((e & 8) << 1) | ((e & 16) >> 1);
        const uint32_t w = ps2::GsPixelAddress(0x00, sw & 15, sw >> 4, cbp, 1);
        return w < pal.size() && pal[w] >= 0 ? &tri[clutAt + (size_t)pal[w] * 4] : nullptr;
    };
    uint32_t color[16];
    bool used[16] = {};
    for (int i = 0; i < 16; ++i) { if (!entry(i)) { err = "the map texture's palette is outside the bank"; return false; } color[i] = U32(entry(i)); }
    for (uint32_t nb : mm.nibble) used[get(nb)] = true;
    uint8_t of[256];
    memset(of, 0xFF, sizeof of);
    int added = 0;
    auto indexOf = [&](uint8_t a) -> uint8_t {
        if (of[a] != 0xFF) return of[a];
        for (int i = 0; i < 16; ++i)                           // a color of the palette: white at that alpha (any color at 0)
            if ((color[i] >> 24) == a && (a == 0 || (color[i] & 0xFFFFFF) == 0xFFFFFF)) return of[a] = (uint8_t)i;
        for (int i = 1; i < 16; ++i)                           // a free one: black, unused by the corner
            if (!color[i] && !used[i]) { color[i] = (uint32_t)a << 24 | 0xFFFFFF; Put32(entry(i), color[i]); used[i] = true; ++added; return of[a] = (uint8_t)i; }
        int best = 0, dist = 1 << 30;                          // none: the nearest white
        for (int i = 0; i < 16; ++i)
            if ((color[i] & 0xFFFFFF) == 0xFFFFFF || !(color[i] >> 24)) {
                const int d = std::abs((int)(color[i] >> 24) - (int)a);
                if (d < dist) { dist = d; best = i; }
            }
        return of[a] = (uint8_t)best;
    };
    mm.index.assign(mm.alpha.size(), std::vector<uint8_t>((size_t)kMapPx * kMapPx));
    for (size_t k = 0; k < mm.alpha.size(); ++k)
        for (size_t p = 0; p < mm.alpha[k].size(); ++p) mm.index[k][p] = indexOf(std::min<uint8_t>(mm.alpha[k][p], 0x80));
    for (size_t p = 0; p < mm.nibble.size(); ++p) put(image, mm.nibble[p], mm.index[0][p]);
    mm.imageSize = clutAt - imageAt;
    // the probe: the 64 bytes of the image where the most pixels of the corner differ from 0 in all the pictures
    std::vector<int> busy(mm.imageSize / 64 + 1, 0);
    for (size_t p = 0; p < mm.nibble.size(); ++p) {
        bool all = true;
        for (auto& ix : mm.index) all &= ix[p] != 0;
        if (all) ++busy[(mm.nibble[p] >> 1) / 64];
    }
    mm.probeAt = (uint32_t)(std::max_element(busy.begin(), busy.end()) - busy.begin()) * 64;
    if (mm.probeAt + 64 > mm.imageSize) mm.probeAt = 0;
    mm.probe.clear();
    for (size_t k = 0; k < mm.index.size(); ++k) {
        std::vector<uint8_t> im(image, image + mm.imageSize);
        for (size_t p = 0; p < mm.nibble.size(); ++p) put(im.data(), mm.nibble[p], mm.index[k][p]);
        mm.probe.emplace_back(im.begin() + mm.probeAt, im.begin() + mm.probeAt + 64);
    }
    Log("extra maps: the 2D map in %s's bank: %zu pictures, %d colors added to its palette", stage.c_str(), mm.index.size(), added);
    return true;
}
// the stage's page with the map's cx, cy, s in place of the stage's own (the constants its 2D map is set with)
bool MinimapPage(const Minimap& mm, const std::string& stage, std::vector<uint8_t>& bin, std::string& err) {
    const StageTransform* st = nullptr;
    for (const StageTransform& s : kStageTransforms) if (stage == s.stage) st = &s;
    if (!st) { err = "no 2D map constants known in " + stage + "'s page"; return false; }
    uint8_t pat[12];
    for (int k = 0; k < 3; ++k) Put32(pat + 4 * k, st->bits[k]);
    size_t at = SIZE_MAX, n = 0;
    for (size_t o = 0; o + sizeof pat <= bin.size(); o += 4)
        if (!memcmp(&bin[o], pat, sizeof pat)) { at = o; ++n; }
    if (n != 1) { err = "the stage's page does not hold its 2D map constants once"; return false; }
    memcpy(&bin[at], mm.t, sizeof mm.t);
    Log("extra maps: the 2D map's cx, cy, s in %s's page at %#zx", stage.c_str(), at);
    return true;
}
// the stage's page with map.ini's pagefloats: each run of floats found once (on a 4-byte boundary, each float within
// 0.01 of the one given: the five versions hold the same tables at other places), its values that change written
// (the others keep the page's own bits); the runs done
int PageFloats(const MapData& md, std::vector<uint8_t>& bin) {
    int done = 0;
    for (size_t k = 0; k < md.pageFloats.size(); ++k) {
        const std::vector<float>& from = md.pageFloats[k].first;
        const std::vector<float>& to = md.pageFloats[k].second;
        const size_t n = from.size();
        size_t at = SIZE_MAX, hits = 0;
        for (size_t o = 0; o + 4 * n <= bin.size(); o += 4) {
            bool same = true;
            for (size_t i = 0; i < n && same; ++i) {
                float x;
                memcpy(&x, &bin[o + 4 * i], 4);
                same = std::fabs(x - from[i]) <= 0.01f;             // a NaN is never the same
            }
            if (same) { at = o; ++hits; }
        }
        if (hits != 1) {
            Log("extra maps: %s: pagefloats %zu (%zu floats from %g) found %zu times in the stage's page: left as it is", md.m.name.c_str(),
                k + 1, n, from[0], hits);
            continue;
        }
        int changed = 0;
        for (size_t i = 0; i < n; ++i)
            if (std::fabs(to[i] - from[i]) > 0.01f) { memcpy(&bin[at + 4 * i], &to[i], 4); ++changed; }
        Log("extra maps: %s: pagefloats %zu: %d of its %zu floats changed at the stage's page + %#zx", md.m.name.c_str(), k + 1, changed, n, at);
        ++done;
    }
    return done;
}

// a stage of this version's archive rebuilt (out: its table of contents, then its data) with the files of `files`
// in place of those it holds (their tags in `put`); the files it does not hold whose anchor (addTo) it holds added
// to the anchor's block, after its files (their tags in `added`; the table of contents grows by their entries); a
// map's stage (md): the procedures of its script put in, its points added to its geom, its 2D map
bool Rebuild(const std::string& stage, const Files& files, MapData* md, Image& out, std::vector<uint32_t>& put, int& scriptsPut,
             long long& grown, std::string& err, const std::map<uint32_t, uint32_t>* addTo = nullptr,
             std::vector<uint32_t>* added = nullptr) {
    put.clear();
    if (added) added->clear();
    scriptsPut = 0;
    grown = 0;                                                 // the sections' sizes, inflated: bytes more (less) than the stage's
    StageToc toc;
    if (!StageDatToc(gHost, gBase, gSize, stage, toc, err)) return false;
    FILE* f = nullptr;
    if (fopen_s(&f, gHost.c_str(), "rb") != 0 || !f) { err = "cannot open the archive"; return false; }
    struct Closer { FILE* f; ~Closer() { fclose(f); } } closer{f};
    const long long stageAt = (long long)toc.sector * kSector;
    auto readAt = [&](long long at, uint8_t* dst, size_t n) {
        return at >= 0 && stageAt + at + (long long)n <= gSize && _fseeki64(f, gBase + stageAt + at, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
    };
    const uint32_t tocBytes = Up(4 + 8 * (uint32_t)toc.entries.size(), kSector);
    // the blocks: compressed ones (their entry, place, size) and plain sections (their place after the
    // blocks before, their size); the files of each
    struct Blk { size_t entry; bool plain; uint32_t off, size; std::vector<size_t> files; size_t section; };
    std::vector<Blk> blocks;
    uint32_t high = 0;
    bool inPlain = false;
    size_t section = SIZE_MAX;
    std::map<size_t, size_t> sectionEnd;                       // a section's entry -> the entry closing it (its size too)
    for (size_t i = 0; i < toc.entries.size(); ++i) {
        const uint32_t tag = toc.entries[i].first, val = toc.entries[i].second;
        if (!tag && !val) break;
        if (tag == 0x7F000000u) { if (section != SIZE_MAX) sectionEnd[section] = i; section = SIZE_MAX; inPlain = false; continue; }
        if ((tag & 0xFF000000u) == 0x7F000000u) {
            section = i;
            inPlain = tag == 0x7F000010u || tag == 0x7F000004u || tag == 0x7F000005u;
            if (inPlain) { blocks.push_back({i, true, Up(high, kSector), val, {}, i}); high = blocks.back().off + val; }
            continue;
        }
        if ((tag & 0xFF000000u) == 0x7E000000u) {
            if (section == SIZE_MAX || inPlain) { err = "a compressed block outside a section"; return false; }
            blocks.push_back({i, false, val, tag & 0xFFFFFF, {}, section});
            high = std::max(high, val + Up(tag & 0xFFFFFF, 4));
            continue;
        }
        if (blocks.empty() || section == SIZE_MAX) { err = "a file outside a block"; return false; }
        blocks.back().files.push_back(i);
    }
    std::vector<std::pair<uint32_t, uint32_t>> entries = toc.entries;
    std::map<size_t, std::vector<uint32_t>> addIn;             // block (index in blocks) -> the files added to it
    if (addTo && added) {
        std::map<uint32_t, size_t> blockOf;                    // a held file's tag -> its block
        for (size_t bi = 0; bi < blocks.size(); ++bi)
            if (!blocks[bi].plain) for (size_t e : blocks[bi].files) blockOf[entries[e].first] = bi;
        for (const auto& fl : files) {
            if (blockOf.count(fl.first)) continue;
            const auto a = addTo->find(fl.first);
            if (a == addTo->end()) continue;
            const auto b = blockOf.find(a->second);
            if (b != blockOf.end()) addIn[b->second].push_back(fl.first);
        }
    }
    std::map<size_t, std::vector<std::pair<uint32_t, uint32_t>>> addEntries;   // block -> the added files' entries
    std::vector<uint8_t> data;                                 // the new data area
    int minimapPut = 0;
    std::string minimapFailed;
    const bool minimap = md && md->mm.want;
    const bool pagePatch = md && !md->pageFloats.empty();     // map.ini pagefloats=: the stage's page too
    auto MinimapTarget = [minimap, pagePatch](uint32_t tag) {
        return (minimap && (tag == kMapBankTag || (tag >> 24) == 0x01)) || (pagePatch && (tag >> 24) == 0x01);
    };
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        Blk& b = blocks[bi];
        const uint32_t at = Up((uint32_t)data.size(), kSector);
        data.resize(at);
        std::vector<uint8_t> raw(b.plain ? b.size : Up(b.size, 4));
        if (!readAt((long long)tocBytes + b.off, raw.data(), raw.size())) { err = "a block cut in the archive"; return false; }
        bool touch = false;
        for (size_t e : b.files) touch |= files.count(entries[e].first) != 0 || MinimapTarget(entries[e].first);
        touch |= addIn.count(bi) != 0;
        if (b.plain || !touch) {                               // as it is, at its new place
            if (!b.plain) entries[b.entry].second = at;
            data.insert(data.end(), raw.begin(), raw.end());
            continue;
        }
        const uint16_t key5 = (uint16_t)(raw[0] | raw[1] << 8);
        StageBlockDecrypt(raw.data(), raw.size());
        std::vector<uint8_t> plain;
        if (!ZlibInflate(raw.data(), b.size, plain)) { err = "a block does not inflate"; return false; }
        std::vector<uint8_t> blockOut;
        for (size_t k = 0; k < b.files.size(); ++k) {
            const size_t e = b.files[k];
            const uint32_t from = entries[e].second, to = k + 1 < b.files.size() ? entries[b.files[k + 1]].second : (uint32_t)plain.size();
            if (from > to || to > plain.size()) { err = "a file outside its block"; return false; }
            std::vector<uint8_t> file(plain.begin() + from, plain.begin() + to);
            auto it = files.find(entries[e].first);
            if (it != files.end()) {
                if ((entries[e].first >> 24) == 0x02 && md && (!md->scripts.empty() || !md->drops.empty())) {
                    std::vector<uint8_t> merged;
                    if (!Transplant(file, it->second, *md, merged, err)) return false;
                    file.swap(merged);
                    scriptsPut = (int)md->scripts.size();
                } else file = it->second;
                if ((entries[e].first >> 24) == 0x0c && md && !md->points.empty() && !AddPoints(file, *md, err)) return false;
                put.push_back(entries[e].first);
            } else if (entries[e].first == kMapBankTag && minimap) {
                std::string why;
                if (!MinimapBank(md->mm, stage, file, why)) { minimapFailed = why; break; }
                ++minimapPut;
            } else if ((entries[e].first >> 24) == 0x01 && (minimap || pagePatch)) {
                if (minimap) {
                    std::string why;
                    if (!MinimapPage(md->mm, stage, file, why)) { minimapFailed = why; break; }
                    ++minimapPut;
                }
                if (pagePatch) PageFloats(*md, file);
            }
            blockOut.resize(Up((uint32_t)blockOut.size(), 16));
            entries[e].second = (uint32_t)blockOut.size();
            blockOut.insert(blockOut.end(), file.begin(), file.end());
        }
        if (!minimapFailed.empty()) break;
        const auto add = addIn.find(bi);                        // the added files, after the block's own
        if (add != addIn.end())
            for (uint32_t tag : add->second) {
                blockOut.resize(Up((uint32_t)blockOut.size(), 16));
                addEntries[bi].push_back({tag, (uint32_t)blockOut.size()});
                const auto& fd = files.at(tag);
                blockOut.insert(blockOut.end(), fd.begin(), fd.end());
                added->push_back(tag);
            }
        std::vector<uint8_t> z = ZlibDeflate(blockOut.data(), blockOut.size());
        if (z.size() > 0xFFFFFF) { err = "a block too large"; return false; }
        std::vector<uint8_t> back;                             // read back as the game will inflate it
        if (!ZlibInflate(z.data(), z.size(), back) || back != blockOut) { err = "a block compressed again does not inflate back"; return false; }
        const uint32_t zsize = (uint32_t)z.size();
        z.resize(Up(zsize, 4), 0);
        StageBlockEncrypt(z.data(), z.size(), key5);
        entries[b.entry].first = 0x7E000000u | zsize;
        entries[b.entry].second = at;
        const uint32_t was = entries[b.section].second;
        entries[b.section].second += (uint32_t)blockOut.size() - (uint32_t)plain.size();   // the section's size
        const auto closing = sectionEnd.find(b.section);       // and its closing entry's: the game's loader (US 0x101F58)
        if (closing != sectionEnd.end() && entries[closing->second].second == was)   // ends the section's last file
            entries[closing->second].second = entries[b.section].second;              // there (a page that grew copied it
                                                                                      // with a size below 0: the crash)
        grown += (long long)blockOut.size() - (long long)plain.size();
        data.insert(data.end(), z.begin(), z.end());
    }
    if (minimap && minimapFailed.empty() && minimapPut != 2) minimapFailed = "the stage has no map bank or no page";
    if (!minimapFailed.empty()) {                              // the stage again without the 2D map (its own stays)
        Log("extra maps: %s (map %d): its 2D map left out: %s", md->m.name.c_str(), md->m.id, minimapFailed.c_str());
        md->mm.want = false;
        md->mm.index.clear();
        return Rebuild(stage, files, md, out, put, scriptsPut, grown, err, addTo, added);
    }
    if (md) md->mm.ok = md->mm.want;
    data.resize(Up((uint32_t)data.size(), kSector));
    if (!addEntries.empty()) {                                 // the added files' entries after their block's last file
        std::map<size_t, size_t> afterEntry;                   // entry index -> block
        for (const auto& ae : addEntries) afterEntry[blocks[ae.first].files.back()] = ae.first;
        std::vector<std::pair<uint32_t, uint32_t>> grownToc;
        for (size_t i = 0; i < entries.size(); ++i) {
            grownToc.push_back(entries[i]);
            const auto ae = afterEntry.find(i);
            if (ae != afterEntry.end()) for (const auto& e : addEntries[ae->second]) grownToc.push_back(e);
        }
        entries.swap(grownToc);
    }
    const uint32_t tocOut = Up(4 + 8 * (uint32_t)entries.size(), kSector);   // the blocks' places count from its end
    out.bytes.assign(tocOut, 0);
    Put32(&out.bytes[0], (uint32_t)entries.size());
    for (size_t i = 0; i < entries.size(); ++i) { Put32(&out.bytes[4 + 8 * i], entries[i].first); Put32(&out.bytes[8 + 8 * i], entries[i].second); }
    StageTocCrypt(out.bytes.data(), (4 + 8 * entries.size()) / 4, stage, toc.key);
    out.bytes.insert(out.bytes.end(), data.begin(), data.end());
    out.sector = toc.sector;
    out.toc = entries;
    return true;
}

// the stage rebuilt for one map (md.img)
bool Build(MapData& md, std::string& err) {
    std::vector<uint32_t> put, added;
    int scriptsPut = 0;
    long long grown = 0;
    if (!Rebuild(md.m.stage, md.files, &md, md.img, put, scriptsPut, grown, err, &md.addTo, &added)) return false;
    if (put.size() + added.size() != md.files.size()) {
        char e[96]; snprintf(e, sizeof e, "%zu of its %zu files are in the stage", put.size() + added.size(), md.files.size()); err = e; return false;
    }
    if (!added.empty()) Log("extra maps: %s: %zu files added to %s beside their anchors (map.ini addto=), %lld bytes %s in memory",
                            md.m.name.c_str(), added.size(), md.m.stage.c_str(), grown < 0 ? -grown : grown, grown <= 0 ? "less" : "more");
    Log("extra maps: %s (map %d) built on %s: %zu files replaced, %d procedures of its script put in, its data %lld bytes %s in memory, %zu bytes at sector %#x%s",
        md.m.name.c_str(), md.m.id, md.m.stage.c_str(), put.size(), scriptsPut, grown < 0 ? -grown : grown, grown <= 0 ? "less" : "more",
        md.img.bytes.size(), md.img.sector, md.mm.ok ? ", its 2D map in" : "");
    if (!md.drops.empty() || !md.points.empty()) {
        size_t n = 0;
        for (auto& g : md.points) n += g.second.size();
        Log("extra maps: %s: %zu procedures of its script emptied, %zu points in %zu groups added to its geom", md.m.name.c_str(), md.drops.size(), n, md.points.size());
    }
    return true;
}

// the rules pages (docs/EXTRA_MAPS.md): the resident pages the rules load (r_death_n, r_team_*, r_sneak_* ...),
// the lobby's (r_menu_n) apart
bool IsRulesPage(const std::string& s) { return s.size() > 2 && s[0] == 'r' && s[1] == '_' && s != "r_menu_n"; }

// The resident area (US 0x121BF8: 0x600000 .. 0xF00000, cleared at boot): the boot's own files first, then the top
// kept (0x121C30, [0x2C5E14]); each rules page's resident section (0x7F000003) is copied below that mark by the
// page loader (0x101F58 -> 0x121C90: each file below the last, its 16-byte header, a .tri's data at 128 bytes, the
// others' at 16; a file ends at the next entry's value, the last one at the section's closing entry's).
constexpr uint32_t kResidentMark = 0x00E60E90;             // the North American program's mark (read from the game)
uint32_t ResidentBottom(const std::vector<std::pair<uint32_t, uint32_t>>& toc, uint32_t top) {
    bool in = false;
    for (size_t i = 0; i + 1 < toc.size(); ++i) {
        const uint32_t tag = toc[i].first;
        if (!in) { in = tag == 0x7F000003u; continue; }
        if ((tag & 0xFF000000u) == 0x7F000000u) break;
        if ((tag & 0xFF000000u) == 0x7E000000u) continue;
        const uint32_t size = toc[i + 1].second - toc[i].second, align = (tag >> 24) == 0x03 ? 0x80 : 0x10;
        uint32_t t = top - size;
        t -= t % align;
        top = t - 0x10;
    }
    return top;
}
// the lowest the stock rules pages of the running version reach (r_sneak_l1_n's on the North American one): the
// game's lobby and stages live with it; a rebuilt page may go as low (MGO1_RESIDENT_FLOOR=<hex address>: lower,
// for tests; the area ends at 0x600000)
uint32_t ResidentFloor(uint32_t top, std::string* lowest) {
    static std::mutex m;
    static std::map<uint32_t, std::pair<uint32_t, std::string>> known;   // top -> the floor, its page
    std::lock_guard<std::mutex> g(m);
    auto it = known.find(top);
    if (it == known.end()) {
        uint32_t floor = top;
        std::string page;
        static const char* kPages[] = {"r_death_n", "r_sna01_n", "r_sna02_n", "r_sna03_n", "r_team_kl1_n", "r_team_kg1_n",
            "r_team_gl1_n", "r_team_kl2_n", "r_team_kg2_n", "r_team_gl2_n", "r_sneak_k1_n", "r_sneak_l1_n", "r_sneak_g1_n",
            "r_sneak_k2_n", "r_sneak_l2_n", "r_sneak_g2_n", "r_sneak_k3_n", "r_sneak_l3_n", "r_sneak_g3_n"};
        for (const char* n : kPages) {
            StageToc toc;
            std::string err;
            if (!StageDatToc(gHost, gBase, gSize, n, toc, err)) continue;
            const uint32_t b = ResidentBottom(toc.entries, top);
            if (b < floor) { floor = b; page = n; }
        }
        it = known.emplace(top, std::make_pair(floor, page)).first;
    }
    uint32_t floor = it->second.first;
    if (lowest) *lowest = it->second.second;
    if (const char* e = getenv("MGO1_RESIDENT_FLOOR")) {
        const uint32_t f = (uint32_t)strtoul(e, nullptr, 16);
        if (f >= 0x600000 && f < floor) { floor = f; if (lowest) *lowest = "MGO1_RESIDENT_FLOOR"; }
    }
    return floor;
}

// a map's rules page rebuilt with its rules\ files the page holds and its rules\<page>\ ones (over those), when
// first asked; null when the page holds none of them or cannot be rebuilt (the stock page then)
const RulesPage* RulesPageOf(const MapData& md, const std::string& page, uint32_t residentTop) {
    std::lock_guard<std::mutex> g(gPagesLock);
    for (auto& p : gPages) if (p.id == md.m.id && p.page == page) return p.ok ? &p : nullptr;
    gPages.emplace_back();
    RulesPage& p = gPages.back();
    p.id = md.m.id; p.page = page;
    Files files = md.rules;
    const auto own = md.rulesOf.find(page);
    if (own != md.rulesOf.end()) for (auto& f : own->second) files[f.first] = f.second;
    if (files.empty()) return nullptr;
    const unsigned long long t0 = GetTickCount64();
    std::vector<uint32_t> put, added;
    int scripts = 0;
    long long grown = 0;
    std::string err;
    p.ok = Rebuild(page, files, nullptr, p.img, put, scripts, grown, err, &md.addTo, &added);
    size_t missing = 0;                                        // rules\<page>\ files the page does not hold
    if (own != md.rulesOf.end())
        for (auto& f : own->second)
            missing += std::find(put.begin(), put.end(), f.first) == put.end() && std::find(added.begin(), added.end(), f.first) == added.end();
    if (p.ok && put.empty() && added.empty()) { p.ok = false; err = "it holds none of the map's rules files"; }
    uint32_t bottom = 0, floor = 0;
    std::string lowest;
    if (p.ok) {                                                // its resident files no lower than the stock pages go
        const uint32_t top = residentTop > 0x600000 && residentTop <= 0xF00000 ? residentTop : kResidentMark;
        bottom = ResidentBottom(p.img.toc, top);
        floor = ResidentFloor(top, &lowest);
        if (bottom < floor) {
            char e[224];
            snprintf(e, sizeof e, "its files would take the resident area down to %08x, %u bytes below the lowest the stock pages "
                     "reach (%08x, %s): halve some sheets (tools/mgo1_tri_half.py, tools/extra_map_pages.py)", bottom, floor - bottom,
                     floor, lowest.c_str());
            err = e;
            p.ok = false;
        }
    }
    if (p.ok) {                                                // a model replaced whose page has its .cvd: that too
        StageToc toc;                                          // (the stock .cvd read with another model: a hang at
        std::string terr;                                      // the player's spawn, tools/mgo1_cvd.py)
        if (StageDatToc(gHost, gBase, gSize, page, toc, terr))
            for (uint32_t tag : put) {
                if ((tag >> 24) != 0x0D) continue;
                const uint32_t cvd = (0x10u << 24) | (tag & 0xFFFFFF);
                bool held = false;
                for (auto& e : toc.entries) held |= e.first == cvd;
                if (held && !files.count(cvd)) {
                    char e[96]; snprintf(e, sizeof e, "%06x.mdl without its %06x.cvd", tag & 0xFFFFFF, tag & 0xFFFFFF); err = e;
                    p.ok = false;
                    break;
                }
            }
    }
    if (!p.ok) {
        p.img = Image();
        Log("extra maps: %s's rules page %s left as it is: %s", md.m.name.c_str(), page.c_str(), err.c_str());
        return nullptr;
    }
    Log("extra maps: %s's rules page %s built: %zu files replaced, %zu added%s, its data %lld bytes %s in memory, its resident files down to %08x "
        "(%u bytes above the lowest the stock pages reach), %zu bytes at sector %#x (%llu ms)",
        md.m.name.c_str(), page.c_str(), put.size(), added.size(), missing ? " (some of its own folder's files are not in it)" : "", grown < 0 ? -grown : grown,
        grown <= 0 ? "less" : "more", bottom, bottom - floor, p.img.bytes.size(), p.img.sector, GetTickCount64() - t0);
    return &p;
}

// the stage files of a folder (named as in a stage: a7b7fb.tri, f8c6c7.geom ...)
bool ReadStageFiles(const std::string& dir, Files& files, std::string& err) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string n = fd.cFileName;
        for (auto& ch : n) ch = (char)tolower((unsigned char)ch);
        if (n.find('.') != 6) continue;
        bool hex = true;
        for (size_t i = 0; i < 6; ++i) hex &= isxdigit((unsigned char)n[i]) != 0;
        const int type = TypeOf(n.substr(7));
        if (!hex || type < 0) continue;
        std::vector<uint8_t> d;
        if (!ReadFile(dir + "\\" + fd.cFileName, d)) { err = "cannot read " + n; FindClose(h); return false; }
        files[((uint32_t)type << 24) | (uint32_t)strtoul(n.substr(0, 6).c_str(), nullptr, 16)] = std::move(d);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return true;
}

// a map.ini text: \n a new line, \xNN a byte, \\ a backslash
std::string Unescape(const std::string& t) {
    std::string out;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '\\' || i + 1 >= t.size()) { out += t[i]; continue; }
        const char e = t[++i];
        if (e == 'n') out += '\n';
        else if (e == 'x' && i + 2 < t.size() && isxdigit((unsigned char)t[i + 1]) && isxdigit((unsigned char)t[i + 2])) {
            out += (char)strtoul(t.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else out += e;
    }
    return out;
}

// the model of a weapon of the game by its number: its weapon's init asks GV_StrCode for this name (US n003a: the
// AK-47's 0x4D68A8 "akmr"; docs/CHARACTERS.md, The added weapons); the added weapons' files go beside it
const char* BaseModelName(int base) {
    switch (base) {
    case 0x05: return "mk22";
    case 0x06: return "gave";
    case 0x0A: return "vz61";
    case 0x0B: return "m16a";
    case 0x0C: return "akmr";
    case 0x0F: return "svdd";
    case 0x10: return "msnn";
    }
    return nullptr;
}

// a WAV file (16-bit PCM, mono or stereo: mixed down): its samples and rate (null: not such a file)
std::shared_ptr<const spu2::Pcm> ReadWav(const std::string& path, std::string& why) {
    std::vector<uint8_t> b;
    if (!ReadFile(path, b)) { why = "not found"; return nullptr; }
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= b.size()) memcpy(&v, &b[o], 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v = 0; if (o + 2 <= b.size()) memcpy(&v, &b[o], 2); return v; };
    if (b.size() < 12 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) { why = "not a WAV file"; return nullptr; }
    uint32_t rate = 0;
    int channels = 0, bits = 0, format = 0;
    for (size_t o = 12; o + 8 <= b.size();) {
        const uint32_t n = u32(o + 4);
        if (!memcmp(&b[o], "fmt ", 4)) { format = u16(o + 8); channels = u16(o + 10); rate = u32(o + 12); bits = u16(o + 22); }
        else if (!memcmp(&b[o], "data", 4)) {
            if (format != 1 || bits != 16 || channels < 1 || channels > 2 || !rate) { why = "not 16-bit PCM, mono or stereo"; return nullptr; }
            auto pcm = std::make_shared<spu2::Pcm>();
            pcm->rate = rate;
            const size_t frames = std::min<size_t>(n, b.size() - (o + 8)) / (2 * (size_t)channels);
            // its infrasound taken out (a first-order high-pass at 20 Hz): the TTS weapons' shots hold half their energy
            // under 10 Hz, which a console's output (coupled through capacitors) never carried and a PC's may (thumps,
            // the mix's headroom); then scaled down if a peak would pass the 16 bits
            std::vector<double> y(frames);
            const double a = 1.0 / (1.0 + 2.0 * 3.14159265358979 * 20.0 / rate);
            double px = 0, py = 0, peak = 1;
            for (size_t k = 0; k < frames; ++k) {
                double x = 0;
                for (int ch = 0; ch < channels; ++ch) x += (int16_t)u16(o + 8 + 2 * ((size_t)channels * k + (size_t)ch));
                x /= channels;
                py = a * (py + x - px); px = x;
                y[k] = py;
                peak = std::max(peak, std::fabs(py));
            }
            const double scale = peak > 32767.0 ? 32767.0 / peak : 1.0;
            pcm->samples.resize(frames);
            for (size_t k = 0; k < frames; ++k) pcm->samples[k] = (int16_t)std::lround(y[k] * scale);
            return pcm;
        }
        o += 8 + n + (n & 1);
    }
    why = "no data";
    return nullptr;
}
// weapons.ini (docs/CHARACTERS.md, The added weapons): [NAME] then base=, model=, magazine=, stance=, icon=,
// icon_suppressed=, choice=BANK:ICON, fpv=OLD,...:NEW,..., laser=1, scope=mosin, name= (name.fr= ...), text= (text.fr= ...),
// sound.<effect>=FILE[,cut][,gain=G]; a name is given as the file's own name (famas: 924099) or its code (0x924099).
// Each added weapon's files in rules\ are added to the rules pages beside the base weapon's model
bool ReadWeapons(const std::string& dir, MapData& md, std::string& err) {
    std::vector<uint8_t> ini;
    if (!ReadFile(dir + "\\weapons.ini", ini)) return true;     // none: nothing added
    std::string text(ini.begin(), ini.end());
    auto code = [](const std::string& v) -> uint32_t {
        const std::string t = Trim(v);
        if (t.empty()) return 0;
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) return (uint32_t)strtoul(t.c_str() + 2, nullptr, 16) & 0xFFFFFF;
        return StageStrCode24(t);
    };
    auto floats = [](const std::string& s, std::vector<float>& out) {
        size_t r = 0;
        while (r <= s.size()) {
            size_t e = s.find(',', r); if (e == std::string::npos) e = s.size();
            const std::string t = Trim(s.substr(r, e - r)); r = e + 1;
            char* end = nullptr;
            const float x = strtof(t.c_str(), &end);
            if (t.empty() || !end || *end) return false;
            out.push_back(x);
        }
        return !out.empty();
    };
    AddedWeapon* w = nullptr;
    size_t p = 0;
    int line = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p); if (e == std::string::npos) e = text.size();
        const std::string l = Trim(text.substr(p, e - p)); p = e + 1; ++line;
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') {
            const size_t close = l.find(']');
            md.weapons.emplace_back();
            w = &md.weapons.back();
            w->name = Trim(l.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            continue;
        }
        const size_t eq = l.find('=');
        if (eq == std::string::npos || !w) { Log("extra maps: %s\\weapons.ini line %d: not NAME=VALUE in a [WEAPON]", dir.c_str(), line); continue; }
        std::string k = Trim(l.substr(0, eq));
        const std::string v = Trim(l.substr(eq + 1));
        std::string lang;
        const size_t dot = k.find('.');
        if (dot != std::string::npos) { lang = k.substr(dot + 1); k = k.substr(0, dot); }
        if (k == "base") w->base = (int)strtoul(v.c_str(), nullptr, 16);
        else if (k == "model") w->model = code(v);
        else if (k == "magazine") w->magazine = code(v);
        else if (k == "stance") w->stance = code(v);
        else if (k == "icon") w->icon = code(v);
        else if (k == "icon_suppressed") w->iconSuppressed = code(v);
        else if (k == "choice") {
            const size_t colon = v.find(':');
            if (colon == std::string::npos) { Log("extra maps: %s\\weapons.ini line %d: choice= is BANK:ICON", dir.c_str(), line); continue; }
            w->choiceBank = code(v.substr(0, colon)); w->choiceIcon = code(v.substr(colon + 1));
        } else if (k == "fpv") {
            const size_t colon = v.find(':');
            std::vector<float> a, b;
            if (colon == std::string::npos || !floats(v.substr(0, colon), a) || !floats(v.substr(colon + 1), b) || a.size() != b.size()) {
                Log("extra maps: %s\\weapons.ini line %d: fpv= is OLD,...:NEW,... (as many floats)", dir.c_str(), line);
                continue;
            }
            w->fpvOld = a; w->fpvNew = b;
        } else if (k == "laser") { w->laser = atoi(v.c_str()) != 0; w->laserOff = !w->laser; }
        else if (k == "rounds") w->rounds = std::max(0, std::min(atoi(v.c_str()), 999));
        else if (k == "scope") w->sight = v == "mosin" ? 4 : v == "svd" ? 1 : atoi(v.c_str());   // 0x45DCA0's kinds
        else if (k == "snake") w->snake = v == "only" ? 2 : atoi(v.c_str()) ? 1 : 0;              // Snake's class (#120)
        else if (k == "name") w->names.push_back({lang, Unescape(v)});
        else if (k == "text") w->texts.push_back({lang, Unescape(v)});
        else if (k == "sound") {                                // sound.<effect>=FILE[,cut][,gain=G]
            AddedWeapon::Sound snd;
            snd.effect = (int)strtol(lang.c_str(), nullptr, 16) & 0xFFF;
            size_t r = 0;
            for (int part = 0; r <= v.size(); ++part) {
                size_t e2 = v.find(',', r); if (e2 == std::string::npos) e2 = v.size();
                const std::string t = Trim(v.substr(r, e2 - r)); r = e2 + 1;
                if (!part) snd.file = t;
                else if (t == "cut") snd.cut = true;
                else if (t.rfind("gain=", 0) == 0) snd.gain = strtof(t.c_str() + 5, nullptr);
            }
            std::string why;
            snd.pcm = lang.empty() || snd.file.empty() ? nullptr : ReadWav(dir + "\\" + snd.file, why);
            if (!snd.pcm) { Log("extra maps: %s\\weapons.ini line %d: sound%s%s=%s left out (%s)", dir.c_str(), line, lang.empty() ? "" : ".", lang.c_str(), v.c_str(), lang.empty() ? "sound.<effect>= names the base's effect" : why.c_str()); continue; }
            w->sounds.push_back(snd);
        }
        else Log("extra maps: %s\\weapons.ini line %d: %s is not known", dir.c_str(), line, k.c_str());
    }
    // the added weapons' files: in rules\ (added to every rules page beside the base weapon's model) or in the map's
    // own folder (added to the map's stage beside its first model: the rules pages hold little more than they do,
    // docs/CHARACTERS.md, The added weapons: where their files go)
    std::set<uint32_t> own;                                     // the added weapons' names: the anchor is none of theirs
    for (const auto& w : md.weapons) for (uint32_t n : {w.model, w.magazine, w.stance, w.icon, w.iconSuppressed, w.choiceBank}) if (n) own.insert(n);
    uint32_t stageAnchor = 0;
    for (auto& f : md.files) if ((f.first >> 24) == 0x0D && !own.count(f.first & 0xFFFFFF)) { stageAnchor = f.first; break; }
    if (!stageAnchor) for (auto& f : md.files) if (!own.count(f.first & 0xFFFFFF)) { stageAnchor = f.first; break; }
    for (auto it = md.weapons.begin(); it != md.weapons.end();) {  // the weapons whose base the game has, their files
        const char* bm = BaseModelName(it->base);
        if (!bm || !it->model) {
            Log("extra maps: %s\\weapons.ini: [%s] left out: %s", dir.c_str(), it->name.c_str(), !bm ? "its base= is no weapon an added one can be" : "no model=");
            it = md.weapons.erase(it);
            continue;
        }
        const uint32_t rulesAnchor = 0x0D000000u | StageStrCode24(bm);
        int inRules = 0, inStage = 0;
        auto add = [&](int type, uint32_t name) {
            if (!name) return;
            const uint32_t tag = ((uint32_t)type << 24) | name;
            if (md.rules.count(tag)) { md.addTo[tag] = rulesAnchor; ++inRules; }
            else if (md.files.count(tag) && stageAnchor) { md.addTo[tag] = stageAnchor; ++inStage; }
        };
        for (int type : {0x0D, 0x11, 0x03}) { add(type, it->model); add(type, it->magazine); }
        add(0x08, it->stance); add(0x69, it->icon); add(0x69, it->iconSuppressed); add(0x03, it->choiceBank);
        const uint32_t model = 0x0D000000u | it->model;
        if (!md.rules.count(model) && !md.files.count(model)) {
            Log("extra maps: %s\\weapons.ini: [%s] left out: its model %06x.mdl is in neither the map's folder nor rules\\", dir.c_str(), it->name.c_str(), it->model);
            it = md.weapons.erase(it);
            continue;
        }
        std::string sounds;
        for (const auto& snd : it->sounds) {
            char b[96];
            snprintf(b, sizeof b, "%s%03x %s", sounds.empty() ? ", its sounds: " : ", ", snd.effect, snd.file.c_str());
            sounds += b;
        }
        Log("extra maps: %s: the weapon %s added (weapon %#x's logic): %d files beside %06x.mdl in the rules pages, %d in the map's stage%s%s%s",
            dir.c_str(), it->name.c_str(), it->base, inRules, rulesAnchor & 0xFFFFFF, inStage, it->laser ? ", a laser sight" : "", sounds.c_str(),
            it->snake == 2 ? ", Snake's class alone (SNEAKING)" : it->snake ? ", Snake's class too (SNEAKING)" : "");
        ++it;
    }
    (void)err;
    return true;
}

// a map's folder: map.ini, then its files
bool LoadFolder(const std::string& dir, MapData& md, std::string& err) {
    std::vector<uint8_t> ini;
    if (!ReadFile(dir + "\\map.ini", ini)) { err = "no map.ini"; return false; }
    std::string text(ini.begin(), ini.end()), line;
    size_t p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p); if (e == std::string::npos) e = text.size();
        line = Trim(text.substr(p, e - p)); p = e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
        if (k == "id") md.m.id = atoi(v.c_str());
        else if (k == "name") md.m.name = v;
        else if (k == "short") md.m.shortName = v;
        else if (k == "stage") md.m.stage = v;
        else if (k == "minimap") {
            float t[3];
            if (sscanf(v.c_str(), "%f , %f , %f", &t[0], &t[1], &t[2]) == 3 && t[2] > 0) { memcpy(md.mm.t, t, sizeof t); md.mm.want = true; }
        } else if (k == "floors") {
            size_t q = 0;
            while (q < v.size()) {
                size_t c = v.find(',', q); if (c == std::string::npos) c = v.size();
                const std::string one = Trim(v.substr(q, c - q)); q = c + 1;
                const size_t colon = one.find(':');
                if (!one.empty()) md.mm.floors.push_back({(float)atof(one.c_str()), colon == std::string::npos ? one : Trim(one.substr(colon + 1))});
            }
        }
        else if ((k.rfind("texts", 0) == 0 && (k.size() == 5 || (k.size() == 6 && k[5] >= '0' && k[5] <= '4'))) ||
                 (k.rfind("scripttexts", 0) == 0 && (k.size() == 11 || (k.size() == 12 && k[11] >= '0' && k[11] <= '4')))) {
            const bool script = k[0] == 's';
            const size_t base = script ? 11 : 5;
            auto& list = (script ? md.scriptTexts : md.texts)[k.size() == base ? 5 : k[base] - '0'];
            size_t q = 0;
            while (q < v.size()) {
                size_t c = v.find('|', q); if (c == std::string::npos) c = v.size();
                const std::string one = v.substr(q, c - q); q = c + 1;
                const size_t arrow = one.find("=>"), colon = one.find(':');
                const size_t cut = arrow != std::string::npos ? arrow : script ? std::string::npos : colon;
                const size_t skip = arrow != std::string::npos ? 2 : 1;
                if (cut == std::string::npos || cut == 0 || cut + skip >= one.size()) {
                    if (!Trim(one).empty()) Log("extra maps: %s: \"%s\" of %s is not OLD%sNEW", dir.c_str(), one.c_str(), k.c_str(), script ? "=>" : ":");
                    continue;
                }
                list.push_back({Unescape(one.substr(0, cut)), Unescape(one.substr(cut + skip))});
            }
        }
        else if (k == "pagefloats") {                         // OLD,...:NEW,...|... (as many floats on each side)
            auto nums = [](const std::string& s, std::vector<float>& out) {
                size_t r = 0;
                while (r <= s.size()) {
                    size_t e2 = s.find(',', r); if (e2 == std::string::npos) e2 = s.size();
                    const std::string t = Trim(s.substr(r, e2 - r)); r = e2 + 1;
                    char* end = nullptr;
                    const float x = strtof(t.c_str(), &end);
                    if (t.empty() || !end || *end) return false;
                    out.push_back(x);
                }
                return !out.empty();
            };
            size_t q = 0;
            while (q < v.size()) {
                size_t c = v.find('|', q); if (c == std::string::npos) c = v.size();
                const std::string one = Trim(v.substr(q, c - q)); q = c + 1;
                if (one.empty()) continue;
                const size_t colon = one.find(':');
                std::vector<float> a, b;
                if (colon == std::string::npos || !nums(one.substr(0, colon), a) || !nums(one.substr(colon + 1), b) || a.size() != b.size()) {
                    Log("extra maps: %s: \"%s\" of pagefloats is not OLD,...:NEW,... (as many floats)", dir.c_str(), one.c_str());
                    continue;
                }
                md.pageFloats.push_back({std::move(a), std::move(b)});
            }
        }
        else if (k == "addto") {                               // NEW:ANCHOR|... (stage file names: 924099.mdl:323a12.mdl)
            auto tagOf = [](const std::string& n, uint32_t& tag) {
                std::string x = Trim(n);
                for (auto& ch : x) ch = (char)tolower((unsigned char)ch);
                if (x.size() < 8 || x[6] != '.') return false;
                for (size_t i = 0; i < 6; ++i) if (!isxdigit((unsigned char)x[i])) return false;
                const int type = TypeOf(x.substr(7));
                if (type < 0) return false;
                tag = ((uint32_t)type << 24) | (uint32_t)strtoul(x.substr(0, 6).c_str(), nullptr, 16);
                return true;
            };
            size_t q = 0;
            while (q < v.size()) {
                size_t c = v.find('|', q); if (c == std::string::npos) c = v.size();
                const std::string one = Trim(v.substr(q, c - q)); q = c + 1;
                if (one.empty()) continue;
                const size_t colon = one.find(':');
                uint32_t a = 0, b = 0;
                if (colon == std::string::npos || !tagOf(one.substr(0, colon), a) || !tagOf(one.substr(colon + 1), b)) {
                    Log("extra maps: %s: \"%s\" of addto is not NEW.ext:ANCHOR.ext", dir.c_str(), one.c_str());
                    continue;
                }
                md.addTo[a] = b;
            }
        }
        else if (k == "laser") {                               // the weapons' numbers: 6,...
            size_t q = 0;
            while (q < v.size()) {
                size_t c = v.find(',', q); if (c == std::string::npos) c = v.size();
                const std::string one = Trim(v.substr(q, c - q)); q = c + 1;
                if (!one.empty() && isdigit((unsigned char)one[0])) md.laser.push_back(atoi(one.c_str()));
            }
        }
        else if (k == "rules" || k == "scripts" || k == "drop") {
            size_t q = 0;
            while (q < v.size()) {
                size_t c = v.find(',', q); if (c == std::string::npos) c = v.size();
                const std::string one = Trim(v.substr(q, c - q)); q = c + 1;
                if (one.empty()) continue;
                if (k == "rules") md.m.rules.push_back(atoi(one.c_str()));
                else {
                    const size_t colon = one.find(':');
                    (k == "drop" ? md.drops : md.scripts).push_back({atoi(one.c_str()), colon == std::string::npos ? 0 : strtoull(one.c_str() + colon + 1, nullptr, 16)});
                }
            }
        }
    }
    md.m.dir = dir;
    md.m.stageMap = StageMapNumber(md.m.stage);
    if (md.m.id < 13 || md.m.id > 60) { err = "its id is not 13 to 60"; return false; }
    if (!md.m.stageMap) { err = "its stage is none of the twelve maps'"; return false; }
    if (md.m.name.empty()) { err = "no name"; return false; }
    if (md.m.shortName.empty()) md.m.shortName = md.m.name.substr(0, 6);
    if (md.m.rules.empty()) md.m.rules = {0, 1, 3};
    if (!ReadStageFiles(dir, md.files, err)) return false;
    if (md.files.empty()) { err = "no stage file in it"; return false; }
    const std::string rd = dir + "\\rules";                    // the rules pages' files: rules\, rules\<page>\ (docs/EXTRA_MAPS.md)
    if (!ReadStageFiles(rd, md.rules, err)) return false;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((rd + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName;
            for (auto& ch : n) ch = (char)tolower((unsigned char)ch);
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !IsRulesPage(n)) continue;
            Files own;
            if (!ReadStageFiles(rd + "\\" + fd.cFileName, own, err)) { FindClose(h); return false; }
            if (!own.empty()) md.rulesOf[n] = std::move(own);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (!ReadWeapons(dir, md, err)) return false;            // weapons.ini: the weapons added (their files in rules\)
    if (md.mm.want) {                                          // its 2D map (none: the stage's own)
        std::string why;
        if (!MinimapLoad(dir, md.mm, why)) { Log("extra maps: %s: its 2D map left out: %s", dir.c_str(), why.c_str()); md.mm.want = false; }
    }
    if (!ReadPoints(dir + "\\points.txt", md, err)) return false;
    uint64_t sum = 0xcbf29ce484222325ull;                      // its content: each file's tag and bytes, the procedures
    auto mix = [&sum](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; ++i) { sum ^= b[i]; sum *= 0x100000001b3ull; } };
    for (auto& f : md.files) { mix(&f.first, 4); mix(f.second.data(), f.second.size()); }
    for (auto& sc : md.scripts) { mix(&sc.first, sizeof sc.first); mix(&sc.second, sizeof sc.second); }
    for (auto& sc : md.drops) { mix(&sc.first, sizeof sc.first); mix(&sc.second, sizeof sc.second); }
    mix(md.pointsText.data(), md.pointsText.size());
    for (auto& f : md.rules) { mix(&f.first, 4); mix(f.second.data(), f.second.size()); }
    for (auto& pg : md.rulesOf) {
        mix(pg.first.data(), pg.first.size());
        for (auto& f : pg.second) { mix(&f.first, 4); mix(f.second.data(), f.second.size()); }
    }
    for (auto& a : md.addTo) { mix(&a.first, 4); mix(&a.second, 4); }   // where the added files go
    for (auto& w : md.weapons) {                               // the weapons added: the choices every player must have
        mix(w.name.data(), w.name.size()); mix(&w.base, sizeof w.base); mix(&w.model, 4); mix(&w.magazine, 4); mix(&w.stance, 4);
        const uint8_t laser = w.laser; mix(&laser, 1);
        if (w.laserOff) mix("laser=0", 7);                    // (mixed only when set: the maps without them keep their signature)
        if (w.rounds) mix(&w.rounds, sizeof w.rounds);
    }
    md.content = sum;
    return true;
}

}  // namespace

void ExtraMapsInit(const std::string& mapsDir, const std::string& stageHost, long long stageBase, long long stageSize,
                   void (*log)(const char* fmt, ...)) {
    std::lock_guard<std::mutex> g(gLock);
    gLogFn = log;
    if (!gMaps.empty() || mapsDir.empty()) return;
    gHost = stageHost; gBase = stageBase; gSize = stageSize;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((mapsDir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { Log("extra maps: no maps folder (%s)", mapsDir.c_str()); return; }
    std::vector<std::string> dirs;
    do {
        const std::string n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != "." && n != "..") dirs.push_back(n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(dirs.begin(), dirs.end());
    for (auto& d : dirs) {
        MapData md;
        std::string err;
        if (!LoadFolder(mapsDir + "\\" + d, md, err)) { Log("extra maps: %s left out: %s", d.c_str(), err.c_str()); continue; }
        bool dup = false;
        for (auto& o : gMaps) dup |= o.m.id == md.m.id;
        if (dup) { Log("extra maps: %s left out: map %d is there already", d.c_str(), md.m.id); continue; }
        Log("extra maps: %s: map %d, %s (%s), on %s, %zu files, %zu procedures of its script", d.c_str(), md.m.id, md.m.name.c_str(),
            md.m.shortName.c_str(), md.m.stage.c_str(), md.files.size(), md.scripts.size());
        if (!md.rules.empty() || !md.rulesOf.empty()) {
            size_t own = 0;
            for (auto& pg : md.rulesOf) own += pg.second.size();
            Log("extra maps: %s: %zu files for the rules pages, %zu more for %zu of them apart", d.c_str(), md.rules.size(), own, md.rulesOf.size());
        }
        gMaps.push_back(std::move(md));
    }
    if (gMaps.empty()) return;
    for (auto& md : gMaps) gPublic.push_back(md.m);
    gBuilder = std::thread([] {
        for (size_t i = 0; i < gMaps.size(); ++i) {
            std::string err;
            MapData& md = gMaps[i];
            md.ok = Build(md, err);
            if (!md.ok) Log("extra maps: %s (map %d) left out: %s", md.m.name.c_str(), md.m.id, err.c_str());
        }
        {
            std::lock_guard<std::mutex> g2(gLock);
            for (size_t i = 0; i < gMaps.size(); ++i) gPublic[i].ready = gMaps[i].ok;
        }
        gBuilt = true;
    });
    // (the dedicated host, the dolls) a process that ends with exit(): the build joined before the statics go
    // (a joinable thread's destructor ends the process with std::terminate)
    atexit([] { if (gBuilder.joinable()) gBuilder.join(); });
}

int StageMapNumber(const std::string& stage) {
    for (int i = 0; i < 12; ++i) if (stage == kStages[i]) return i + 1;
    return 0;
}
const char* MapStage(int number) {
    if (number >= 1 && number <= 12) return kStages[number - 1];
    for (auto& m : gPublic) if (m.id == number) return m.stage.c_str();
    return nullptr;
}

const std::vector<ExtraMap>& ExtraMaps() { return gPublic; }
uint32_t ExtraMapsSignature(const std::vector<int>& ids) {
    if (gBuilder.joinable() && !gBuilt) gBuilder.join();       // the build first (it runs from the start)
    uint32_t h = 0x811c9dc5u;
    for (int id : ids) {
        const MapData* md = nullptr;
        for (auto& m : gMaps) if (m.m.id == id) md = &m;
        if (!md || !md->ok) return 0;
        const uint64_t v[2] = {(uint64_t)id, md->content};
        const uint8_t* b = (const uint8_t*)v;
        for (size_t i = 0; i < sizeof v; ++i) { h ^= b[i]; h *= 0x01000193u; }
    }
    return h ? h : 1;
}
const ExtraMap* ExtraMapById(int id) {
    for (auto& m : gPublic) if (m.id == id) return &m;
    return nullptr;
}
int ExtraMapStageOf(int id) {
    for (auto& m : gPublic) if (m.id == id) return m.stageMap;
    return id;
}
bool ExtraMapsAny() { return !gPublic.empty(); }
void ExtraMapsWait() { if (gBuilder.joinable() && !gBuilt) gBuilder.join(); }

namespace {
const MapData* RulesMap(int realMap) {                        // the map of that number when it brings rules files
    if (realMap <= 12) return nullptr;
    for (auto& md : gMaps)
        if (md.m.id == realMap && md.ok && (!md.rules.empty() || !md.rulesOf.empty())) return &md;
    return nullptr;
}
}  // namespace

void ExtraMapsStageLoad(const std::string& stage, int realMap, uint32_t residentTop) {
    if (realMap > 12 && gBuilder.joinable() && !gBuilt) gBuilder.join();   // the build first (it runs from the start)
    if (IsRulesPage(stage)) {                                  // a rules page (the resident one): the map's own or the stock one
        const MapData* md = RulesMap(realMap);
        const RulesPage* p = md ? RulesPageOf(*md, stage, residentTop) : nullptr;
        const int was = gRulesLoaded;
        gRulesLoaded = p ? md->m.id : 0;
        gServedImage = p ? &p->img : nullptr;             // the map's stage served again when its stage loads next
        if (p || was) Log("extra maps: the rules page %s loads%s%s", stage.c_str(), p ? " as " : " (the stock one)", p ? md->m.name.c_str() : "");
        return;
    }
    int served = -1;
    if (realMap > 12)
        for (size_t i = 0; i < gMaps.size(); ++i)
            if (gMaps[i].m.id == realMap && gMaps[i].m.stage == stage) served = gMaps[i].ok ? (int)i : -2;
    const int was = gServed.exchange(served >= 0 ? served : -1);
    gServedImage = served >= 0 ? &gMaps[(size_t)served].img : nullptr;
    const int now = served >= 0 ? gMaps[(size_t)served].m.id : 0;
    if (served == -2) Log("extra maps: map %d asked on %s but it could not be built: %s as it is", realMap, stage.c_str(), stage.c_str());
    if (now || was >= 0) Log("extra maps: the stage %s loads%s%s", stage.c_str(), now ? " as " : "", now ? gMaps[(size_t)served].m.name.c_str() : "");
    gLoaded = now;
}
int ExtraMapsLoaded() { return gLoaded; }
bool ExtraMapsFile(int id, int type, std::vector<uint8_t>& out) {
    for (const MapData& md : gMaps) {
        if (md.m.id != id) continue;
        const std::vector<uint8_t>* best = nullptr;
        for (const auto& f : md.files)
            if ((int)(f.first >> 24) == type && (!best || f.second.size() > best->size())) best = &f.second;
        if (!best) return false;
        out = *best;
        std::string err;
        if (type == 0x0c && !md.points.empty() && !AddPoints(out, md, err)) out = *best;
        return true;
    }
    return false;
}
bool ExtraMapsRulesPage(const std::string& stage) { return IsRulesPage(stage); }
// the map whose rules page the round's map needs for that page: its own when it was built (or not yet tried), the
// stock one (0) when the map brings none of that page's files (SILO ENTRANCE with r_team_gl1_n: the stock page, else
// the check asked for it again at every load, a black screen)
int ExtraMapsRulesOf(int realMap, const std::string& page) {
    const MapData* md = RulesMap(realMap);
    if (!md) return 0;
    std::lock_guard<std::mutex> g(gPagesLock);
    for (auto& p : gPages) if (p.id == md->m.id && p.page == page) return p.ok ? md->m.id : 0;
    return md->m.id;
}
int ExtraMapsRulesLoaded() { return gRulesLoaded; }
namespace {
std::vector<std::pair<std::string, std::string>> TextsOf(int realMap, int rule, bool script) {
    std::vector<std::pair<std::string, std::string>> out;
    std::lock_guard<std::mutex> g(gLock);
    for (auto& md : gMaps) {
        if (md.m.id != realMap) continue;
        const auto* sets = script ? md.scriptTexts : md.texts;
        out = sets[5];
        if (rule >= 0 && rule <= 4)
            for (auto& t : sets[rule]) {                       // the rule's own over those of every rule
                bool over = false;
                for (auto& o : out) if (o.first == t.first) { o.second = t.second; over = true; }
                if (!over) out.push_back(t);
            }
    }
    return out;
}
}  // namespace
std::vector<std::pair<std::string, std::string>> ExtraMapsTexts(int realMap, int rule) { return TextsOf(realMap, rule, false); }
std::vector<std::pair<std::string, std::string>> ExtraMapsScriptTexts(int realMap, int rule) { return TextsOf(realMap, rule, true); }

bool ExtraMapsLaser(int weapon) {
    if (!gLoaded) return false;
    for (auto& md : gMaps)
        if (md.m.id == gLoaded && md.ok) return std::find(md.laser.begin(), md.laser.end(), weapon) != md.laser.end();
    return false;
}

const std::vector<AddedWeapon>& ExtraMapsWeapons() {
    static const std::vector<AddedWeapon> none;
    if (!gLoaded || gRulesLoaded != gLoaded) return none;    // its files are in its own rules page only
    for (auto& md : gMaps) if (md.m.id == gLoaded && md.ok) return md.weapons;
    return none;
}
const std::string& AddedWeaponText(const AddedWeapon& w, bool description, const std::string& lang) {
    static const std::string empty;
    const auto& list = description ? w.texts : w.names;
    const std::string* any = nullptr;
    for (auto& t : list) {
        if (t.first == lang) return t.second;
        if (t.first.empty() && !any) any = &t.second;
    }
    return any ? *any : description ? empty : w.name;
}

// the 2D map of the extra map loaded
namespace {
const Minimap* LoadedMinimap() {
    if (!gLoaded) return nullptr;
    for (auto& md : gMaps) if (md.m.id == gLoaded && md.ok && md.mm.ok) return &md.mm;
    return nullptr;
}
}  // namespace
int ExtraMapsFloors() {
    const Minimap* mm = LoadedMinimap();
    return mm && mm->floors.size() + 1 == mm->index.size() ? (int)mm->floors.size() : 0;
}
int ExtraMapsFloorOf(float y) {
    const Minimap* mm = LoadedMinimap();
    if (!mm || mm->floors.empty()) return 0;
    int k = 1;
    for (size_t i = 1; i < mm->floors.size(); ++i) if (y >= mm->floors[i].first) k = (int)i + 1;
    return k;
}
const char* ExtraMapsFloorName(int k) {
    const Minimap* mm = LoadedMinimap();
    return mm && k >= 1 && (size_t)k <= mm->floors.size() ? mm->floors[(size_t)k - 1].second.c_str() : "all";
}
uint32_t ExtraMapsMinimapSize() { const Minimap* mm = LoadedMinimap(); return mm ? mm->imageSize : 0; }
bool ExtraMapsMinimapProbe(int k, uint32_t& at, const uint8_t*& bytes) {
    const Minimap* mm = LoadedMinimap();
    if (!mm || k < 0 || (size_t)k >= mm->probe.size()) return false;
    at = mm->probeAt; bytes = mm->probe[(size_t)k].data();
    return true;
}
bool ExtraMapsMinimapPut(uint8_t* image, int k) {
    const Minimap* mm = LoadedMinimap();
    if (!mm || k < 0 || (size_t)k >= mm->index.size()) return false;
    const std::vector<uint8_t>& ix = mm->index[(size_t)k];
    for (size_t p = 0; p < mm->nibble.size(); ++p) {
        const uint32_t nb = mm->nibble[p];
        uint8_t& b = image[nb >> 1];
        b = (nb & 1) ? (uint8_t)((b & 0x0F) | ix[p] << 4) : (uint8_t)((b & 0xF0) | ix[p]);
    }
    return true;
}

bool ExtraMapsRead(long long at, uint8_t* dst, size_t n) {
    const Image* im = gServedImage.load();
    if (!im) return false;
    const long long from = (long long)im->sector * kSector, to = from + (long long)im->bytes.size();
    if (at + (long long)n <= from || at >= to) return false;
    // the part in the rebuilt stage from it, the rest (before or after it) left to the archive
    const long long b = std::max(at, from), e = std::min(at + (long long)n, to);
    memcpy(dst + (b - at), im->bytes.data() + (b - from), (size_t)(e - b));
    return true;
}

}  // namespace ps2world
