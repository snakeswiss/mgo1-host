// Device-aware button prompts (docs/BUTTON_PROMPTS.md): the PS2 font's four button glyphs (base codes 0x8302
// circle, 0x8303 triangle, 0x8304 square, 0x8305 cross, the same in the five versions) and the button words of
// the pages' texts (L1 .. R3, START, SELECT) follow the device the player used last: PlayStation (the stock
// glyphs and words), Xbox, Nintendo Switch, or the keyboard and mouse (the keys bound to those PS2 buttons).
// Included by ps2world.cpp (one namespace with the platform); prompts_font.h holds the letters.
#pragma once
#include "prompts_font.h"
#include "prompts_names.h"
#include <mutex>

namespace prompts {

enum Family { kPlayStation = 0, kXbox = 1, kSwitch = 2, kKeys = 3, kFamilies };
const char* const kFamilyName[kFamilies] = {"playstation", "xbox", "switch", "keyboard"};
// PS2 buttons in libpad's bit order (select 0 .. square 15)
enum { kSelect, kL3, kR3, kStart, kUp, kRight, kDown, kLeft, kL2, kR2, kL1, kR1, kTriangle, kCircle, kCross, kSquare, kButtons };
const char* const kButtonName[kButtons] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                           "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
// the four glyphs: base cell (code - 0x825C) and the PS2 button each shows
const int kCells = 4;
const uint16_t kCellCode[kCells] = {0x8302, 0x8303, 0x8304, 0x8305};
const int kCellButton[kCells] = {kCircle, kTriangle, kSquare, kCross};
const uint32_t kCellBytes = 0x90;                          // 24 x 24, 2 bits a pixel, first pixel in bits 7-6

// ---- the cells
struct Cell { uint8_t b[kCellBytes]; };
inline void Put(Cell& c, int x, int y, int v) {
    if (x < 0 || x > 23 || y < 0 || y > 23) return;
    const int bit = (y * 24 + x) * 2, sh = 6 - bit % 8;
    c.b[bit / 8] = (uint8_t)((c.b[bit / 8] & ~(3 << sh)) | ((v & 3) << sh));
}
inline int Get(const Cell& c, int x, int y) { const int bit = (y * 24 + x) * 2; return (c.b[bit / 8] >> (6 - bit % 8)) & 3; }
const FontGlyph* Glyph(const PromptFont& f, char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    for (int i = 0; i < f.n; ++i) if (f.g[i].ch == ch) return &f.g[i];
    return nullptr;
}
int TextWidth(const PromptFont& f, const char* s) {
    int w = 0, n = 0;
    for (; *s; ++s) if (const FontGlyph* g = Glyph(f, *s)) { w += g->w; ++n; }
    return n ? w + (n - 1) : 0;
}
// a label centred on (cx, cy) in level v
void Text(Cell& c, const PromptFont& f, const char* s, float cx, float cy, int v) {
    int x = (int)(cx - TextWidth(f, s) / 2.0f + 0.5f);
    const int y0 = (int)(cy - f.h / 2.0f + 0.5f);
    for (; *s; ++s) {
        const FontGlyph* g = Glyph(f, *s);
        if (!g) continue;
        for (int r = 0; r < f.h; ++r)
            for (int k = 0; k < g->w; ++k)
                if (g->rows[r] >> (g->w - 1 - k) & 1) Put(c, x + k, y0 + r, v);
        x += g->w + 1;
    }
}
// shapes about the cell's centre (11.5, 11.5)
void Disc(Cell& c, float r, int v) {
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
        const float dx = x - 11.5f, dy = y - 11.5f;
        if (dx * dx + dy * dy <= r * r) Put(c, x, y, v);
    }
}
void Ring(Cell& c, float r0, float r1, int v) {
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
        const float dx = x - 11.5f, dy = y - 11.5f, d = dx * dx + dy * dy;
        if (d <= r1 * r1 && d >= r0 * r0) Put(c, x, y, v);
    }
}
void RoundBox(Cell& c, int x0, int y0, int x1, int y1, int rad, int v) {
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        const int cx = x < x0 + rad ? x0 + rad : x > x1 - rad ? x1 - rad : x;
        const int cy = y < y0 + rad ? y0 + rad : y > y1 - rad ? y1 - rad : y;
        const int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy <= rad * rad) Put(c, x, y, v);
    }
}
// an arrow key's label: a triangle pointing up, down, left or right
void Arrow(Cell& c, char dir, int v) {
    for (int i = 0; i < 6; ++i)
        for (int k = -i; k <= i; ++k) {
            const int a = 9 + i, b = 12 + k;
            if (dir == '^') Put(c, b, a, v); else if (dir == 'v') Put(c, b, 23 - a, v);
            else if (dir == '<') Put(c, a, b - 1, v); else Put(c, 23 - a, b - 1, v);
        }
}
// Xbox: a disc, its letter cut out; Switch: a ring, its letter inside
Cell PadCell(bool xbox, char letter) {
    Cell c{};
    char s[2] = {letter, 0};
    if (xbox) { Disc(c, 11.0f, 3); Text(c, kFontL, s, 11.5f, 12.0f, 0); }
    else { Ring(c, 9.2f, 11.2f, 3); Text(c, kFontM, s, 11.5f, 12.0f, 3); }
    return c;
}
// a pad button that is not a face button (LB, ZR, MENU ...): its name in the family's ring or disc
Cell LabelCell(bool disc, const char* name) {
    Cell c{};
    if (disc) Disc(c, 11.5f, 3); else Ring(c, 9.6f, 11.4f, 3);
    const PromptFont* fonts[] = {&kFontM, &kFontS, &kFontT};
    const PromptFont* f = &kFontT;
    for (const PromptFont* t : fonts) if (TextWidth(*t, name) <= (disc ? 18 : 16)) { f = t; break; }
    Text(c, *f, name, 11.5f, 11.5f, disc ? 0 : 3);
    return c;
}
// the keyboard: a key cap (a rounded box), its key's name cut out, as large as it fits
Cell KeyCell(const char* name) {
    Cell c{};
    RoundBox(c, 0, 2, 23, 21, 4, 3);
    if (name[0] == '@' && name[1] && !name[2]) { Arrow(c, name[1] == 'U' ? '^' : name[1] == 'D' ? 'v' : name[1] == 'L' ? '<' : '>', 0); return c; }
    const PromptFont* fonts[] = {&kFontL, &kFontM, &kFontS, &kFontT};
    const PromptFont* f = &kFontT;
    for (const PromptFont* t : fonts) if (TextWidth(*t, name) <= 21) { f = t; break; }
    if (TextWidth(*f, name) > 22) {                            // too long even small: its first four letters
        static char cut[5]; snprintf(cut, sizeof cut, "%s", name); name = cut;
    }
    Text(c, *f, name, 11.5f, 11.5f, 0);
    return c;
}

// ---- the words of the texts
struct WordSub { const char* word; int button; };
const WordSub kWords[] = {{"SELECT", kSelect}, {"START", kStart}, {"L1", kL1}, {"R1", kR1}, {"L2", kL2}, {"R2", kR2},
                          {"L3", kL3}, {"R3", kR3}};
// a pad's buttons as config.ini [pad] names them (XInput's, by place on every pad) and as each family calls them
enum { kPA, kPB, kPX, kPY, kPLB, kPRB, kPLT, kPRT, kPBack, kPStart, kPLS, kPRS, kPUp, kPDown, kPLeft, kPRight, kSources };
const char* const kSourceName[kSources] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "BACK", "START", "LS", "RS", "UP", "DOWN", "LEFT", "RIGHT"};
const char* const kXboxName[kSources] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "VIEW", "MENU", "LS", "RS", "UP", "DOWN", "LEFT", "RIGHT"};
const char* const kSwitchName[kSources] = {"B", "A", "Y", "X", "L", "R", "ZL", "ZR", "-", "+", "LS", "RS", "UP", "DOWN", "LEFT", "RIGHT"};
const char* const kPsName[kSources] = {"", "", "", "", "L1", "R1", "L2", "R2", "SELECT", "START", "L3", "R3", "UP", "DOWN", "LEFT", "RIGHT"};
const int kPsCell[kSources] = {3, 0, 2, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // A cross, B circle, X square, Y triangle
// the PS2 buttons' own sources (MGO1.exe's defaults: SetPadDefaults)
const int kDefaultSource[kButtons] = {kPBack, kPLS, kPRS, kPStart, kPUp, kPRight, kPDown, kPLeft, kPLT, kPRT, kPLB, kPRB, kPY, kPB, kPA, kPX};

// what the player sees now: the family and, for the keyboard, the keys of each PS2 button (two each, as config.ini
// binds them: their short names, ASCII capitals) in the lobby's menus and in a match
struct Prompts {
    int family = kPlayStation;
    char menu[kButtons][2][8] = {}, match[kButtons][2][8] = {};
    char pad[kButtons][8] = {};                                  // the pad's button of each PS2 button ("": its default)
    bool operator==(const Prompts& o) const { return !memcmp(this, &o, sizeof o); }
};

// the pad's button a PS2 button is bound to
inline int SourceOf(const Prompts& p, int button) {
    for (int k = 0; k < kSources; ++k) if (p.pad[button][0] && !strcmp(p.pad[button], kSourceName[k])) return k;
    return kDefaultSource[button];
}
// a key's name in room characters (a word of a text): the first key that fits, else its first letters
inline std::string FitKey(const char (&keys)[2][8], size_t room) {
    for (const char* k : keys) if (k[0] && k[0] != '@' && strlen(k) <= room) return k;
    const char* k = keys[0][0] ? keys[0] : keys[1];
    if (!k[0]) return std::string();
    if (k[0] == '@') {                                          // an arrow key: its name
        const char* n = k[1] == 'U' ? "UP" : k[1] == 'D' ? "DOWN" : k[1] == 'L' ? "LEFT" : "RIGHT";
        return std::string(n).substr(0, room);
    }
    return std::string(k).substr(0, room);
}

// a text with its button words for a family (never longer than the text: written in its place)
inline std::string WordsFor(const std::string& s, const Prompts& p, bool inMatch) {
    bool own = p.family == kPlayStation;                          // PlayStation with the PS2's own buttons: the text as it is
    for (int b = 0; b < kButtons && own; ++b) own = SourceOf(p, b) == kDefaultSource[b];
    if (own) return s;
    std::string out;
    auto alnum = [](unsigned char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'); };
    for (size_t i = 0; i < s.size();) {
        const unsigned char ch = (unsigned char)s[i];
        if (ch >= 0x80 || ch == 0x1F) { out += s.substr(i, 2); i += 2; continue; }    // a 2-byte code, an accented letter
        bool done = false;
        if (i == 0 || !alnum((unsigned char)s[i - 1]))
            for (const WordSub& w : kWords) {
                const size_t n = strlen(w.word);
                if (s.compare(i, n, w.word) != 0 || (i + n < s.size() && alnum((unsigned char)s[i + n]))) continue;
                if (n > 2) {                                              // START, SELECT: not a word of a title (TEAM SELECT)
                    size_t a = i, z = i + n;
                    while (a > 0 && s[a - 1] == ' ') --a;
                    while (z < s.size() && s[z] == ' ') ++z;
                    if ((a > 0 && s[a - 1] >= 'A' && s[a - 1] <= 'Z') || (z < s.size() && s[z] >= 'A' && s[z] <= 'Z')) continue;
                }
                const int src = SourceOf(p, w.button);
                std::string to = p.family == kXbox ? kXboxName[src] : p.family == kSwitch ? kSwitchName[src]
                                 : p.family == kPlayStation ? (src == kDefaultSource[w.button] ? w.word : kPsName[src])
                                 : FitKey(inMatch ? p.match[w.button] : p.menu[w.button], n);
                if (to.empty() || to.size() > n) to = s.substr(i, n);
                out += to; i += n; done = true;
                break;
            }
        if (!done) out += s[i++];
    }
    return out;
}

// ---- the state (the front end or the simulation sets it; the game's thread applies it)
std::mutex gLock;
Prompts gWant, gShown;
bool gWantSet = false;
unsigned gWantGen = 1, gShownGen = 0;
struct FontTable { Cell stock[kCells]; Cell last[kCells]; bool have[kCells] = {}, ours[kCells] = {}; };
std::map<uint32_t, FontTable> gTables;                       // the base glyph tables seen (lobby, match)
struct WordText { uint32_t at; std::string orig, last; };     // a text holding button words, in its script
std::vector<WordText> gWordTexts;
std::vector<uint32_t> gScriptsSet;                           // scripts set since the last tick (their texts not read yet)
// a script's own font may hold a button glyph too (the rules pages' circle, 0x8C01): its glyphs equal to the stock four
struct ScriptCell { uint32_t at; int k; Cell stock, last; bool ours; };
std::vector<ScriptCell> gScriptCells;
std::vector<std::pair<uint32_t, uint32_t>> gScriptFonts;      // a script font's glyphs (place, count) not compared yet
// the stock button glyphs known (FNV-1a of a cell): the four of the font files (US, EU, JP, JPS; KR), and the circle the
// US and European rules pages' scripts draw in their own font (0x8C01). Only a cell holding one of them, or the one
// written there, is ever written: the place is the font's (not data loaded there since)
const uint32_t kStockHash[2][kCells] = {{0x5bf7b83du, 0x8ab34d51u, 0x4b6a116du, 0xbe569802u}, {0xe6bfdd3du, 0xada41c51u, 0x23917d05u, 0x1f665cd2u}};
const uint32_t kScriptCircleHash = 0xe8c0e45du;
inline uint32_t CellHash(const uint8_t* p) { uint32_t h = 0x811C9DC5u; for (uint32_t i = 0; i < kCellBytes; ++i) h = (h ^ p[i]) * 0x01000193u; return h; }
inline int StockCell(const uint8_t* p) {                     // the button a stock glyph draws (0..3), -1 none
    const uint32_t h = CellHash(p);
    for (const auto& set : kStockHash) for (int k = 0; k < kCells; ++k) if (h == set[k]) return k;
    return h == kScriptCircleHash ? 0 : -1;
}
unsigned gCellsGen = 0, gWordsGen = 0;
bool gCellsChanged = false;                                  // a cell written this tick: the texts showing it drawn again
// a button glyph drawn into a text's 4-bit image (US 0x11F528: a0 the text's font object, a1 x, a2 y, a3 the code, t0
// its edge): the game draws a text once, so when the device changes each is drawn again in its place, as long as the
// image there still holds what was drawn (its hash taken at the next pass)
struct DrawnGlyph { uint32_t obj, x, y, code, t0, buf; int32_t stride; uint8_t f3, f4; uint32_t hash; bool pending; };
std::vector<DrawnGlyph> gDrawn;
bool gRedrawing = false;
bool gWordsInMatch = false;

void Set(const Prompts& p) {
    std::lock_guard<std::mutex> g(gLock);
    if (gWantSet && p == gWant) return;
    gWant = p; gWantSet = true; ++gWantGen;
}

}  // namespace prompts

// ---- in the platform
uint32_t PromptsFontTable() {                                 // the base glyphs of the font set up now (as 0x11F350)
    const uint32_t at = PageData(0x11F350, 0, 0x284030);
    if (at < 0x100000 || at >= 0x02000000) return 0;
    const uint32_t tab = R32(at) & 0x01FFFFFF;
    return tab >= 0x100000 && tab + 194 * prompts::kCellBytes < 0x02000000 ? tab : 0;
}
// a glyph as the family shows it; PlayStation: the stock cell of the bound face button (stock: the font's four)
prompts::Cell PromptsCell(const prompts::Prompts& p, int k, bool inMatch, const prompts::Cell* stock = nullptr) {
    using namespace prompts;
    const int b = kCellButton[k], src = SourceOf(p, b);
    const bool face = src <= kPY;
    if (p.family == kXbox) return face ? PadCell(true, kXboxName[src][0]) : LabelCell(true, kXboxName[src]);
    if (p.family == kSwitch) return face ? PadCell(false, kSwitchName[src][0]) : LabelCell(false, kSwitchName[src]);
    if (p.family == kPlayStation) {
        if (face && stock) return stock[kPsCell[src]];
        return LabelCell(false, face ? "?" : kPsName[src]);
    }
    const char (&keys)[2][8] = inMatch ? p.match[b] : p.menu[b];
    return KeyCell(keys[0][0] ? keys[0] : keys[1][0] ? keys[1] : "?");
}
// the four cells of the font set up now as the family shows them (the stock ones kept for PlayStation)
void PromptsCells(const prompts::Prompts& p, bool inMatch) {
    using namespace prompts;
    const uint32_t tab = PromptsFontTable();
    if (!tab) return;
    FontTable& t = gTables[tab];
    int wrote = 0;
    for (int k = 0; k < kCells; ++k) {                         // each cell the font's: ours, or a stock glyph in its place
        const uint8_t* m = gMem + tab + kCellBytes * (kCellCode[k] - 0x825C);
        if (!(t.ours[k] && !memcmp(m, t.last[k].b, kCellBytes)) && StockCell(m) != k) return;
    }
    for (int k = 0; k < kCells; ++k) {                         // the game's own (a font file loaded there) kept as stock
        const uint8_t* m = gMem + tab + kCellBytes * (kCellCode[k] - 0x825C);
        if (!(t.ours[k] && !memcmp(m, t.last[k].b, kCellBytes))) { memcpy(t.stock[k].b, m, kCellBytes); t.have[k] = true; t.ours[k] = false; }
    }
    for (int k = 0; k < kCells; ++k) {
        uint8_t* m = gMem + tab + kCellBytes * (kCellCode[k] - 0x825C);
        Cell now; memcpy(now.b, m, kCellBytes);
        const Cell want = PromptsCell(p, k, inMatch, t.stock);
        const bool stockNow = !memcmp(want.b, t.stock[k].b, kCellBytes);
        if (!memcmp(now.b, want.b, kCellBytes)) continue;
        memcpy(m, want.b, kCellBytes);
        t.last[k] = want; t.ours[k] = !stockNow;
        ++wrote; gCellsChanged = true;
    }
    if (wrote && Once(((uint64_t)0x9B << 40) | ((uint64_t)p.family << 32) | tab))
        Log("prompts: the font at %08x shows the %s buttons (%d glyphs written)", tab, kFamilyName[p.family], wrote);
    if (!(t.have[0] && t.have[1] && t.have[2] && t.have[3])) return;
    for (const auto& f : gScriptFonts) {                       // the scripts' glyphs drawn as the stock buttons
        int found = 0;
        for (uint32_t g = 0; g < f.second; ++g) {
            const uint32_t at = f.first + kCellBytes * g;
            const int k = StockCell(gMem + at);
            if (k < 0) continue;
            ScriptCell sc{at, k, {}, {}, false};
            memcpy(sc.stock.b, gMem + at, kCellBytes);
            gScriptCells.push_back(sc);
            ++found;
        }
        if (found) Log("prompts: the script font at %08x holds %d button glyphs", f.first, found);
    }
    gScriptFonts.clear();
    for (auto it = gScriptCells.begin(); it != gScriptCells.end();) {
        uint8_t* m = gMem + it->at;
        const bool mine = it->ours && !memcmp(m, it->last.b, kCellBytes);
        if (!mine && memcmp(m, it->stock.b, kCellBytes)) { it = gScriptCells.erase(it); continue; }   // another script there now
        Cell want = PromptsCell(p, it->k, inMatch, t.stock);
        if (p.family == kPlayStation && SourceOf(p, kCellButton[it->k]) == kDefaultSource[kCellButton[it->k]]) want = it->stock;
        if (memcmp(m, want.b, kCellBytes)) { memcpy(m, want.b, kCellBytes); it->last = want; it->ours = memcmp(want.b, it->stock.b, kCellBytes) != 0; gCellsChanged = true; }
        ++it;
    }
}
// a script was set (0x125500's a0): its texts read at the next tick, once deciphered in place
void PromptsScriptSet(uint32_t g) { prompts::gScriptsSet.push_back(g & 0x01FFFFFF); }
void PromptsReadScript(uint32_t g) {
    using namespace prompts;
    if (g < 0x100000 || g + 32 >= 0x02000000) return;
    uint32_t p = g + 4;
    while (p + 4 < 0x02000000 && R32(p) != 0xFFFFFFFFu && p - g < 0x10000) p += 4;
    const uint32_t blk = p + 4;
    if (blk + 20 >= 0x02000000 || R32(p) != 0xFFFFFFFFu) return;
    const uint32_t strRes = R32(blk + 8), fontOff = R32(blk + 12), seed = R32(blk + 16);
    if (seed || fontOff <= strRes || fontOff - strRes > 0x200000 || blk + fontOff >= 0x02000000) return;
    const uint32_t a0 = blk + strRes, a1 = blk + fontOff;
    gWordTexts.erase(std::remove_if(gWordTexts.begin(), gWordTexts.end(), [&](const WordText& w) { return w.at >= a0 && w.at < a1; }),
                     gWordTexts.end());
    int found = 0;
    for (uint32_t a = a0; a < a1;) {
        uint32_t e = a;
        while (e < a1 && gMem[e]) ++e;
        const std::string s((const char*)gMem + a, e - a);
        Prompts any; any.family = kXbox;
        if ((s.find(' ') != std::string::npos || s.find('\n') != std::string::npos) && WordsFor(s, any, false) != s) {
            gWordTexts.push_back({a, s, s});
            ++found;
        }
        a = e + 1;
    }
    if (found) Log("prompts: script %08x: %d texts with button words", g, found);
    const uint32_t fs = R32(a1);                                // its font: the glyphs' size, then the 24 x 24 glyphs
    if (fs && fs % kCellBytes == 0 && fs <= 2040 * kCellBytes && a1 + 4 + fs < 0x02000000) {
        gScriptCells.erase(std::remove_if(gScriptCells.begin(), gScriptCells.end(), [&](const ScriptCell& c) { return c.at >= a1 && c.at < a1 + 4 + fs; }),
                           gScriptCells.end());
        gScriptFonts.push_back({a1 + 4, fs / kCellBytes});
    }
}
// each texts' words as the family shows them, in place (a text the game changed since is forgotten)
void PromptsWords(const prompts::Prompts& p, bool inMatch) {
    using namespace prompts;
    int wrote = 0;
    for (auto it = gWordTexts.begin(); it != gWordTexts.end();) {
        const size_t n = it->orig.size();
        if (it->at + n >= 0x02000000 || memcmp(gMem + it->at, it->last.c_str(), it->last.size() + 1)) { it = gWordTexts.erase(it); continue; }
        const std::string t = WordsFor(it->orig, p, inMatch);
        if (t != it->last) {
            memcpy(gMem + it->at, t.c_str(), t.size());
            memset(gMem + it->at + t.size(), 0, n + 1 - t.size());
            it->last = t;
            ++wrote;
        }
        ++it;
    }
    if (wrote) Log("prompts: %d texts' button words as %s shows them", wrote, kFamilyName[p.family]);
}
// tests (SIM_PROMPTS, SIM_PROMPTS_AT, SIM_PROMPTS_KEYS: docs/BUTTON_PROMPTS.md): a family forced from a frame on
void PromptsSim() {
    using namespace prompts;
    static bool init = false;
    static std::vector<std::pair<long long, int>> at;
    static long long cycle = 0, cycleFrom = 0;
    static Prompts keys;
    if (!init) {
        init = true;
        auto fam = [](const std::string& s) {
            for (int f = 0; f < kFamilies; ++f) if (s == kFamilyName[f]) return f;
            return s == "ps" ? (int)kPlayStation : s == "keys" ? (int)kKeys : -1;
        };
        // the keyboard's keys: the MC's layout B as MGO1.exe binds it, the menus' Enter and Backspace
        const char* def[kButtons][2] = {{"1", "TAB"}, {"RMB", "N"}, {"O", ""}, {"2", ""}, {"W", "@U"}, {"D", "@R"}, {"S", "@D"}, {"A", "@L"},
                                        {"Q", "."}, {"R", "/"}, {"V", ""}, {"MMB", "C"}, {"E", ""}, {"F", ""}, {"SPACE", ""}, {"LMB", ""}};
        for (int b = 0; b < kButtons; ++b)
            for (int k = 0; k < 2; ++k) { snprintf(keys.match[b][k], 8, "%s", def[b][k]); snprintf(keys.menu[b][k], 8, "%s", def[b][k]); }
        snprintf(keys.menu[kCircle][0], 8, "ENTER"); keys.menu[kCircle][1][0] = 0;
        snprintf(keys.menu[kCross][0], 8, "BKSP"); keys.menu[kCross][1][0] = 0;
        if (const char* e = getenv("SIM_PROMPTS_KEYS")) {        // button=KEY[,KEY2];... (config.ini names)
            std::string s = e;
            for (size_t i = 0; i < s.size();) {
                size_t j = s.find(';', i); if (j == std::string::npos) j = s.size();
                const std::string item = s.substr(i, j - i);
                const size_t q = item.find('=');
                if (q != std::string::npos)
                    for (int b = 0; b < kButtons; ++b)
                        if (item.compare(0, q, kButtonName[b]) == 0) {
                            const std::string v = item.substr(q + 1);
                            const size_t c = v.find(',');
                            const std::string k1 = ShortKeyName(v.substr(0, c)), k2 = c == std::string::npos ? "" : ShortKeyName(v.substr(c + 1));
                            snprintf(keys.match[b][0], 8, "%s", k1.c_str()); snprintf(keys.match[b][1], 8, "%s", k2.c_str());
                            if (b != kCircle && b != kCross) { memcpy(keys.menu[b], keys.match[b], sizeof keys.menu[b]); }
                        }
                i = j + 1;
            }
        }
        if (const char* e = getenv("SIM_PROMPTS")) { const int f = fam(e); if (f >= 0) at.push_back({0, f}); }
        if (const char* e = getenv("SIM_PROMPTS_AT")) {          // frame:family,frame:family...
            std::string s = e;
            for (size_t i = 0; i < s.size();) {
                size_t j = s.find(',', i); if (j == std::string::npos) j = s.size();
                const std::string item = s.substr(i, j - i);
                const size_t q = item.find(':');
                if (q != std::string::npos) { const int f = fam(item.substr(q + 1)); if (f >= 0) at.push_back({atoll(item.c_str()), f}); }
                i = j + 1;
            }
        }
        if (const char* e = getenv("SIM_PROMPTS_CYCLE")) {       // frames[@from]: keyboard, xbox, switch, playstation in turn
            cycle = atoll(e);
            if (const char* q = strchr(e, '@')) cycleFrom = atoll(q + 1);
        }
        if (!at.empty() || cycle > 0) Log("prompts: the simulation forces the device (SIM_PROMPTS: %zu choices, a cycle of %lld frames)", at.size(), cycle);
    }
    int f = -1;
    for (const auto& e : at) if (gFrameNo >= e.first) f = e.second;
    if (cycle > 0 && gFrameNo >= cycleFrom) { const int order[4] = {kKeys, kXbox, kSwitch, kPlayStation}; f = order[((gFrameNo - cycleFrom) / cycle) % 4]; }
    if (f < 0) return;
    Prompts p = keys; p.family = f;
    if (!(gWantSet && p == gWant)) Log("prompts: the simulation's device: %s (frame %lld)", kFamilyName[f], (long long)gFrameNo);
    Set(p);
}
// the cell a code draws (as 0x11F350: base codes, then the pages from 0x8400), 0 none
uint32_t PromptsGlyphAt(uint32_t code) {
    const uint32_t c = code & ~0x6000u, hi = c >> 8;
    if (hi == 0x83) { const uint32_t tab = PromptsFontTable(); return tab && c >= 0x825D && c < 0x825D + 194 ? tab + prompts::kCellBytes * (c - 0x825C) : 0; }
    if (hi < 0x84 || hi >= 0xA0) return 0;
    const uint32_t pages = PageData(0x11F350, 1, 0x23A5A0);
    if (pages < 0x100000 || pages >= 0x02000000) return 0;
    const uint32_t page = (c - 0x8400) >> 10, a = c - (page << 10) - 0x8401, idx = a - (a >> 8);
    const uint32_t tab = R32(pages + 4 * page) & 0x01FFFFFF;
    return tab >= 0x100000 && idx < 1024 && tab + prompts::kCellBytes * (idx + 1) < 0x02000000 ? tab + prompts::kCellBytes * idx : 0;
}
bool PromptsButtonCode(uint32_t code) {                     // a code drawing one of the cells the prompts write
    using namespace prompts;
    const uint32_t c = code & ~0x6000u;
    if (c >= 0x8302 && c <= 0x8305) return true;
    if (gScriptCells.empty() || (c >> 8) < 0x84) return false;
    const uint32_t at = PromptsGlyphAt(c);
    for (const ScriptCell& sc : gScriptCells) if (sc.at == at) return true;
    return false;
}
// the bridge at 0x11F528 (any version's, by its US address): a button glyph about to be drawn into a text
void PromptsGlyphDrawn(Ctx& c) {
    using namespace prompts;
    if (gRedrawing || !gWantSet) return;
    const uint32_t code = (uint32_t)c.r[7] & 0xFFFF, obj = (uint32_t)c.r[4] & 0x01FFFFFF;
    if (!PromptsButtonCode(code) || obj < 0x100000 || obj + 0x20 >= 0x02000000) return;
    DrawnGlyph g{obj, (uint32_t)c.r[5], (uint32_t)c.r[6], code, (uint32_t)c.r[8], R32(obj + 0x10) & 0x01FFFFFF, (int16_t)R16(obj + 8),
                 R8(obj + 3), R8(obj + 4), 0, true};
    for (auto it = gDrawn.begin(); it != gDrawn.end(); ++it)
        if (it->obj == g.obj && it->x == g.x && it->y == g.y && it->buf == g.buf) { gDrawn.erase(it); break; }
    if (gDrawn.size() >= 256) gDrawn.erase(gDrawn.begin());
    gDrawn.push_back(g);
}
// the image bytes a glyph covers (24 rows of 13 bytes), hashed; 0 when outside the memory
uint32_t PromptsGlyphHash(const prompts::DrawnGlyph& g) {
    const int64_t at = (int64_t)g.buf + (int64_t)(int32_t)g.y * g.stride + (int32_t)g.x / 2;
    if (g.stride <= 0 || g.stride > 4096 || at < 0x100000 || at + 24LL * g.stride + 13 >= 0x02000000) return 0;
    uint32_t h = 0x811C9DC5u;
    for (int r = 0; r < 24; ++r) for (int k = 0; k < 13; ++k) h = (h ^ gMem[at + (int64_t)r * g.stride + k]) * 0x01000193u;
    return h ? h : 1;
}
// each button glyph shown drawn again with the cells written now (the text's image unchanged since: else forgotten)
void PromptsRedraw() {
    using namespace prompts;
    const uint32_t fn = V(0x11F528);
    if (!fn) return;
    int drawn = 0;
    for (auto it = gDrawn.begin(); it != gDrawn.end();) {
        DrawnGlyph& g = *it;
        if (g.pending || (R32(g.obj + 0x10) & 0x01FFFFFF) != g.buf || PromptsGlyphHash(g) != g.hash) { it = gDrawn.erase(it); continue; }
        const uint8_t f3 = R8(g.obj + 3), f4 = R8(g.obj + 4);
        W8(g.obj + 3, g.f3); W8(g.obj + 4, g.f4);              // its colour and edge as when the game drew it
        gRedrawing = true;
        RunPs2Args(fn, {g.obj, g.x, g.y, g.code, g.t0});
        gRedrawing = false;
        W8(g.obj + 3, f3); W8(g.obj + 4, f4);
        g.hash = PromptsGlyphHash(g);
        ++drawn; ++it;
    }
    if (drawn) Log("prompts: %d button glyphs of the texts shown drawn again", drawn);
}
// once a pass of the actor lists (the game's thread)
void PromptsTick() {
    using namespace prompts;
    PromptsSim();
    for (uint32_t g : gScriptsSet) PromptsReadScript(g);
    const bool newTexts = !gScriptsSet.empty();
    gScriptsSet.clear();
    Prompts p;
    unsigned gen;
    { std::lock_guard<std::mutex> g(gLock); if (!gWantSet) return; p = gWant; gen = gWantGen; }
    const bool inMatch = InMatch();
    if (gen != gShownGen) {
        gShownGen = gen;
        std::string k;
        if (p.family == kKeys)
            for (int c = 0; c < kCells; ++c) { const int b = kCellButton[c]; k += std::string(" ") + kButtonName[b] + " " + p.match[b][0] + "/" + p.menu[b][0]; }
        Log("prompts: the %s buttons shown%s", kFamilyName[p.family], k.c_str());
    }
    for (auto& g : gDrawn) if (g.pending) { g.hash = PromptsGlyphHash(g); g.pending = false; }   // drawn since the last pass
    gCellsChanged = false;
    PromptsCells(p, inMatch);
    if (gCellsChanged) PromptsRedraw();
    if (newTexts || gen != gWordsGen || inMatch != gWordsInMatch) { gWordsGen = gen; gWordsInMatch = inMatch; PromptsWords(p, inMatch); }
    gShown = p;
}
// the front end's choice (ps2world::SetButtonPrompts)
void PromptsSet(int family, const char* const menu[16][2], const char* const match[16][2], const char* const pad[16]) {
    prompts::Prompts p;
    p.family = family >= 0 && family < prompts::kFamilies ? family : prompts::kPlayStation;
    for (int b = 0; b < prompts::kButtons; ++b) {
        for (int k = 0; k < 2; ++k) {
            snprintf(p.menu[b][k], 8, "%s", menu && menu[b][k] ? menu[b][k] : "");
            snprintf(p.match[b][k], 8, "%s", match && match[b][k] ? match[b][k] : "");
        }
        snprintf(p.pad[b], 8, "%s", pad && pad[b] ? pad[b] : "");
    }
    prompts::Set(p);
}
