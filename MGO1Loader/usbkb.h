// MGO1's USB keyboard as each program turns its keys into characters (docs/MC_PC_INPUT.md, section 3): the game's
// own tables, read in its memory, and the keys that make it type a character
#pragma once
#include <cstdint>
#include <cstring>

namespace usbkb {

// the PS2's memory as the tables are read from it
struct Mem {
    const uint8_t* m = nullptr;
    uint32_t size = 0;
    uint8_t R8(uint32_t a) const { return m && a < size ? m[a] : 0; }
    uint16_t R16(uint32_t a) const { uint16_t v = 0; if (m && a + 2 <= size) memcpy(&v, m + a, 2); return v; }
    uint32_t R32(uint32_t a) const { uint32_t v = 0; if (m && a + 4 <= size) memcpy(&v, m + a, 4); return v; }
};

// a program's tables: libusbkb's (sceUsbKbCnvRawCode: the 101-key, the 106-key at +0x400, kana at +0x800, each
// 0x100 usages without and with Shift), the European program's own layouts (pointers by USB KEYBOARD TYPE) and its
// settings' registry; caps: Caps Lock read (the Japanese programs pass Num Lock only)
struct Program { const char* version; uint32_t t101, euLayouts, euRegistry; bool caps; };
inline const Program* ProgramOf(const char* v) {
    static const Program k[] = {
        {"US", 0x276A58, 0, 0, true},
        {"KR", 0x276A58, 0, 0, true},
        {"EU", 0x276388, 0x229C40, 0x28EE68, true},
        {"JP", 0x2763D8, 0, 0, false},
        {"JPS", 0x276458, 0, 0, false},
    };
    for (const Program& p : k) if (v && !strcmp(v, p.version)) return &p;
    return nullptr;
}

// tables: libusbkb's arrangements 0 (101-key), 1 (106-key), 2 (kana); the European program's own from 0x10 + its
// USB KEYBOARD TYPE (0x12 French, 0x13 German, 0x14 Italian, 0x15 Spanish)
enum { kUs101 = 0, kJp106 = 1, kJpKana = 2, kEuOwn = 0x10 };
inline const char* TableName(int t) {
    switch (t) {
    case kUs101: return "US 101-key"; case kJp106: return "Japanese 106-key"; case kJpKana: return "kana";
    case kEuOwn + 2: return "French"; case kEuOwn + 3: return "German"; case kEuOwn + 4: return "Italian";
    case kEuOwn + 5: return "Spanish";
    }
    return "?";
}

// the table the program types with now: the keyboard actor's arrangement (work + 0x160: the Japanese programs set
// it from USB KEYBOARD TYPE, 0 the 101-key, 1 the 106-key; the others keep 0) and, in the European program, that
// setting itself (block 0x11 + 0x198, from the game's language: 2 to 5 its own tables, else the 101-key)
inline int TableNow(const Mem& M, const Program& p, uint32_t kbWork, int* setting = nullptr) {
    int arr = p.caps ? kUs101 : kJp106;                       // as the actor starts
    if (kbWork) { const uint32_t a = M.R32(kbWork + 0x160); if (a <= 2) arr = (int)a; }
    if (setting) *setting = -1;
    if (!p.euLayouts) return arr;
    const uint32_t ent = M.R32(p.euRegistry + 0x11 * 4), data = ent ? M.R32(ent + 8) : 0;
    const int s = data ? M.R8(data + 0x198) : 0;
    if (setting) *setting = s;
    return s < 6 && M.R32(p.euLayouts + 4 * s) ? kEuOwn + s : arr;
}

struct Key { uint8_t usage = 0, mods = 0; };              // a HID usage and the report's modifiers

// the character the program makes of a key with no dead key waiting (0: none, or a key that is no character);
// dead: the character waits for the next key (the European tables)
inline uint16_t CharOf(const Mem& M, const Program& p, int table, uint8_t led, Key k, bool* dead = nullptr) {
    if (dead) *dead = false;
    if (!p.caps) led &= 1;
    const bool shift = (k.mods & 0x22) != 0;
    if (table < kEuOwn) {                                     // sceUsbKbCnvRawCode (US 0x1E7A78)
        if (!k.usage) return 0;
        uint16_t c = M.R16(p.t101 + 0x400 * table + (shift ? 0x200 : 0) + 2 * k.usage);
        if (c == 0x4000 || c == 0x8000) return 0;             // the number pad, a key
        if (c >= 'a' && c <= 'z' && (led & 2)) c -= 0x20;
        if (c >= 'A' && c <= 'Z' && (led & 2) && shift) c += 0x20;
        if (!p.caps && table == kUs101 && c == '`') return 0;  // the Japanese programs: a key there (0x8035)
        return c >= 0x20 && c < 0x7f ? c : 0;
    }
    // the European program's own (EU 0x1055E0): [plain, shift, flags] for usages 4 .. 0x64; flags 8 Caps Lock
    // swaps the two, 4 the number pad, 0x10 a key, 0x20 a dead key, 0x40 AltGr (Right Alt) has its own list
    const uint32_t lay = M.R32(p.euLayouts + 4 * (table - kEuOwn));
    if (!lay || k.usage < 4 || k.usage > 0x64) return 0;
    uint32_t e = lay + 3 * (k.usage - 4);
    uint8_t flags = M.R8(e + 2);
    if ((flags & 0x40) && (k.mods & 0x40)) {
        const int n = M.R8(lay + 0x123);
        const uint32_t list = M.R32(lay + 0x128);
        for (int i = 0; i < n; ++i)
            if (M.R8(list + 4 * i) == k.usage) { e = list + 4 * i + 1; flags = M.R8(list + 4 * i + 3); break; }
    }
    if (flags & 0x14) return 0;
    int col = 0;
    if ((flags & 8) && (led & 2)) col ^= 1;
    if (shift) col ^= 1;
    const uint16_t c = M.R8(e + col);
    if (c && (flags & 0x20)) {
        const int n = M.R8(lay + 0x124);
        const uint32_t list = M.R32(lay + 0x12C);
        for (int i = 0; i < n; ++i) if (M.R8(list + 8 * i) == c && dead) *dead = true;
    }
    return c;
}

// a dead key's character followed by a key's: the character the European table composes (0: none)
inline uint16_t Composed(const Mem& M, const Program& p, int table, uint16_t d, uint16_t base) {
    if (table < kEuOwn || !d || !base) return 0;
    const uint32_t lay = M.R32(p.euLayouts + 4 * (table - kEuOwn));
    const int n = M.R8(lay + 0x124);
    const uint32_t list = M.R32(lay + 0x12C);
    for (int i = 0; i < n; ++i) {
        if (M.R8(list + 8 * i) != d) continue;
        const int cnt = M.R8(list + 8 * i + 1);
        const uint32_t q = M.R32(list + 8 * i + 4);
        for (int j = 0; j < cnt; ++j) if (M.R8(q + 2 * j) == base) return M.R8(q + 2 * j + 1);
        return 0;
    }
    return 0;
}

// the keys that make the program type ch now: one key (the main block first, without AltGr first, then the
// extra keys), or a dead key and a key; 0: it cannot
inline int KeysFor(const Mem& M, const Program& p, int table, uint8_t led, uint16_t ch, Key out[2]) {
    if (!ch) return 0;
    static const uint8_t kMods[] = {0x00, 0x02, 0x40, 0x42};
    static const uint8_t kExtra[] = {0x32, 0x64, 0x87, 0x89};  // Non-US # and \, the 106-key's \ _ and yen
    auto direct = [&](uint8_t u, uint8_t m) {
        bool dead = false;
        const Key k{u, m};
        return CharOf(M, p, table, led, k, &dead) == ch && !dead;
    };
    for (int pass = 0; pass < 2; ++pass)
        for (int u = 4; u <= 0x38; ++u)
            for (int i = 0; i < 2; ++i)
                if (u != 0x32 && direct((uint8_t)u, kMods[2 * pass + i])) { out[0] = {(uint8_t)u, kMods[2 * pass + i]}; return 1; }
    for (uint8_t u : kExtra)
        for (uint8_t m : kMods) if (direct(u, m)) { out[0] = {u, m}; return 1; }
    if (table < kEuOwn) return 0;
    for (uint8_t m : kMods)
        for (int u = 4; u <= 0x64; ++u) {
            bool dead = false;
            const Key d{(uint8_t)u, m};
            const uint16_t dc = CharOf(M, p, table, led, d, &dead);
            if (!dead) continue;
            for (uint8_t m2 : kMods)
                for (int u2 = 4; u2 <= 0x64; ++u2) {
                    bool dead2 = false;
                    const Key b{(uint8_t)u2, m2};
                    const uint16_t bc = CharOf(M, p, table, led, b, &dead2);
                    if (bc && !dead2 && Composed(M, p, table, dc, bc) == ch) { out[0] = d; out[1] = b; return 2; }
                }
        }
    return 0;
}

// what MGO1's field is to get for a character the user typed, best first: a letter's case turned by the game's Caps
// Lock (its software keyboards turn it on: capitals unless Shift, as on a PS2), the character as typed, then the
// letter without its accent (Latin-1; nothing else is typed)
inline int Targets(uint32_t ch, bool caps, uint16_t out[4]) {
    if (ch < 0x20 || ch > 0xff || ch == 0x7f) return 0;
    auto swapCase = [](uint16_t c) -> uint16_t {
        if ((c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7)) return (uint16_t)(c - 0x20);
        if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) return (uint16_t)(c + 0x20);
        return c;
    };
    auto base = [](uint16_t c) -> uint16_t {
        static const char kFrom[] = "\xe0\xe1\xe2\xe3\xe4\xe5\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef\xf1\xf2\xf3\xf4\xf5\xf6\xf9\xfa\xfb\xfc\xfd\xff"
                                    "\xc0\xc1\xc2\xc3\xc4\xc5\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd1\xd2\xd3\xd4\xd5\xd6\xd9\xda\xdb\xdc\xdd";
        static const char kTo[] = "aaaaaaceeeeiiiinooooouuuuyyAAAAAACEEEEIIIINOOOOOUUUUY";
        for (int i = 0; kFrom[i]; ++i) if ((uint8_t)kFrom[i] == c) return (uint16_t)kTo[i];
        return 0;
    };
    int n = 0;
    auto add = [&](uint16_t c) { if (!c) return; for (int i = 0; i < n; ++i) if (out[i] == c) return; out[n++] = c; };
    const uint16_t c = (uint16_t)ch, t = caps ? swapCase(c) : c;
    add(t); add(c); add(base(t)); add(base(c));
    return n;
}

// the keys for a character the user typed (Targets, then KeysFor): their count, 0 when the program cannot type it
inline int StrokesFor(const Mem& M, const Program& p, int table, uint8_t led, uint32_t ch, Key out[2], uint16_t* got = nullptr) {
    uint16_t t[4];
    const int n = Targets(ch, p.caps && (led & 2), t);
    for (int i = 0; i < n; ++i) {
        const int k = KeysFor(M, p, table, led, t[i], out);
        if (k) { if (got) *got = t[i]; return k; }
    }
    return 0;
}

}  // namespace usbkb
