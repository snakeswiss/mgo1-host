// (v150-host beta 22, book) a book (the support row's girlie magazine, the weapon 0x1C) and the players (docs/VIRTUAL_DOLL.md
// 30). The book's own code touches nobody: each player's OWN game makes him read another player's book. An action of the local
// player (US n003a: its check 0x443C18, act 0x443718, end 0x443A08) looks each frame through the game's list of the books lying
// (NewOnlineBookMan's entries) and takes the first one not laid by himself whose place is within 1.5 m of his hips with a
// clear line from his head to 0.2 m over it: he stops, turns to it and reads it (he cannot move), the game's read time starts
// on that book, and when it ends his game takes the book away and tells the others; then 450 VBlanks (7.5 s) before he can read
// another (and as long after each spawn). Raikov (the class 4) reads none; a player in one of a few states (a ladder, a ledge,
// knocked out ...) neither. A virtual doll has no game of its own: nobody ever asked for it. The host's game holds the same
// list; this header holds the choice apart from the game's memory, so that it is tested off line (tests/vdoll_book_test.cpp);
// vdoll_body.cpp applies it with the host's list (ps2world.cpp VdollBooks) and starts the read time in the host's game
// (VdollReadBook) as a reader's own game does.
#pragma once
#include <cstdint>
#include <cstring>

namespace vbook {

const float kReach2 = 2250000.f;                 // 1.5 m squared, his hips (+0x110) to the book's place (0x559C8C)
const float kLift = 200.f;                       // the line's end over the book's place (0x559C90)
const int kCooldownMs = 7508;                    // 450 VBlanks (+0xF54 at a spawn and after a reading, counted down by the step)
const int kRaikov = 4;                           // the class (+0xB62) the check refuses first
const int kMaxReadMs = 30000;                    // (a guard: a reading whose book never leaves the list ends there)

// the state bits the check refuses (0x407598's list over the words at +0xB2C): 9, 0x5C (hanging from a ledge), 0x52 (on a
// ladder), 0x99 (knocked out), 0xF0, 0x19, 0x1A, 0x1B, 0x1C
const int kRefusedBits[9] = {9, 0x5C, 0x52, 0x99, 0xF0, 0x19, 0x1A, 0x1B, 0x1C};
inline bool Refused(const uint32_t* words11) {
    for (int b : kRefusedBits) if (words11[b >> 5] & (1u << (b & 31))) return true;
    return false;
}

// a book of the game's list (its entries in state 1, in the list's order)
struct Book {
    uint32_t id = 0;                             // its id: its layer's slot << 10, a count
    int slot = -1;                               // its layer's slot (id >> 10)
    float pos[3] = {0, 0, 0};                    // its place (on the floor)
    int readTime = 0;                            // the read time a reading starts, in the game's passes (0 not known)
};
// a player as the check sees him
struct Reader {
    int slot = -1;                               // his slot (his own books are passed over)
    int cls = -1;                                // his class (+0xB62)
    bool alive = false;                          // alive, not a ghost
    bool refused = false;                        // one of the refused state bits (Refused)
    bool reading = false;                        // reading already
    float hips[3] = {0, 0, 0};
    unsigned long long readyMs = 0;              // the end of his cooldown (the machine's clock in ms)
};

// the game's check for one player: the first book of the list, not his own, within reach of his hips, whose place 0.2 m up
// is in a clear line from his eye (clear(i): that line's test for book i). -1: none
template <class Clear>
int Pick(const Reader& r, const Book* b, int n, unsigned long long nowMs, Clear clear) {
    if (!r.alive || r.reading || r.cls == kRaikov || r.refused || nowMs < r.readyMs) return -1;
    for (int i = 0; i < n; ++i) {
        if (b[i].slot == r.slot) continue;
        const float dx = r.hips[0] - b[i].pos[0], dy = r.hips[1] - b[i].pos[1], dz = r.hips[2] - b[i].pos[2];
        if (dx * dx + dy * dy + dz * dz > kReach2) continue;
        if (!clear(i)) continue;
        return i;
    }
    return -1;
}
// the line's end for book i: 0.2 m over its place
inline void Target(const Book& b, float out[3]) { out[0] = b.pos[0]; out[1] = b.pos[1] + kLift; out[2] = b.pos[2]; }
// a reading's length by the machine's clock: the read time's passes at a player's own 60 a second (59.94), kMaxReadMs when not
// known. The host's game counts its passes slower when it is slowed: the reading ends by this clock, as a player's own game's
// does, and the host's read time is ended then (Host::bookDone)
inline unsigned long long ReadMs(int readTime) {
    return readTime > 0 ? (unsigned long long)((double)readTime * 1000.0 / 59.94 + 0.5) : (unsigned long long)kMaxReadMs;
}
// a reading goes on while its book is in the list (another reader's game may take it away first) and its time is not over
inline bool StillReading(const Book* b, int n, uint32_t id, unsigned long long sinceMs, unsigned long long untilMs, unsigned long long nowMs) {
    if (nowMs >= untilMs || nowMs - sinceMs >= (unsigned long long)kMaxReadMs) return false;
    for (int i = 0; i < n; ++i) if (b[i].id == id) return true;
    return false;
}
// MGO1_VDOLL_BOOKS: on unless "0"
inline bool On(const char* env) { return !(env && !strcmp(env, "0")); }
// ---- (v150-host beta 25, bookhold) the moves that hold a player's book back (docs/VIRTUAL_DOLL.md 51; measured on real clients, the
// playable dolls' probe 52). The book is one of the actions of the player's root action (id 0xEADB2E, priority 0x13, exclusive); each
// tick the scheduler (US n003a 0x30F178) checks no exclusive action below one that runs. The ledge action (0x21F3700, priority 0x16)
// runs from a drop's edge to 9 ticks (18 frames) after its landing, and through 24 ticks of a climb from 2 ticks after the press: a
// player read a book in reach 54, 55 and 57 frames after his climb's press (27 ticks: the next check after the action's end). A
// fence crossed (a walk over it) runs no action and sets no refused state bit: read on the fence. The ladder (0x2EBB252, 0x1B) holds
// it to the ladder's end; the roll's state bit 9 (refused by the check) its whole roll; crouched or lying: read
const int kClimbHoldFrames = 54;
// MGO1_VDOLL_BOOK_HOLD: on unless "0" (0: beta 22's holds: a climb's 60 frames, a fence's crossing)
inline bool HoldOn(const char* env) { return !(env && !strcmp(env, "0")); }

// ---- (v150-host beta 22, bookplay) a reading on the other players' screens (docs/VIRTUAL_DOLL.md 34). Every game holds a
// copy of each player with the same book action; a copy reads only when the reader's own game tells it (US n003a: the
// check 0x443C18's copy path, the act 0x443718, the record's reader 0x442FF8, the state record's 0x405E48):
// - the reader's act sends each frame on his channel of kind 1 the record {0E, +0xDE9 (1 while he reads)}, the first one of
//   a reading long: {0E, 1, his hips / 10, the book's place / 10} (six 16-bit numbers, cut toward 0). On a copy it sets
//   +0xDE9; the long one also its +0xE00 (his place) and +0xE50 (the book's), and marks the action (+0x58);
// - the copy's check then starts the copy's reading (and sets the copy's +0xFB4 to 2): put once on his place, turned to the
//   book, looking at it standing (the motion 0xB2, about 3 s), then crouched over it (0xB3, the state word 0 bit 2);
// - the reader's act sets his +0xFB4 to 2 each frame, and his state record {03, state, +0xFB4, stamina, 0x12C, flags}
//   (every 100 ms; +0xFB4 cleared after each) carries it as its third byte. A copy reads that byte into its +0xFB4 and its
//   reading ends at the first state record whose byte is not 2: then its end (0x443A08) stands it up again and clears the
//   reading's state bits (0x126, 0x127, 0xDD, 0x153, 0x158, 0x159), as the reader's own end does;
// - while he reads, his game sends no style record {06}, no facing {01 ...} (one {01 0 0 0 0 0 0} with his first place
//   record) and no look {17 ...}: the look's record writes the same +0xE50 the book's place is in, so a copy turned and
//   looked at the look's point instead of the book. His place {00 x y z}, {12 FF}, {13 ...} go on.
const uint8_t kRecord = 0x0E;                    // the reading's record (kind 1's channel)
const uint8_t kStateReading = 2;                 // the state record's third byte while he reads (+0xFB4); 4 is a fall
// MGO1_VDOLL_BOOK_POSE: on unless "0" (0: the doll's copy stands while it reads, as beta 22's first build)
inline bool PoseOn(const char* env) { return !(env && !strcmp(env, "0")); }
// a place in the record: the game's float / 10, cut toward 0 (div.s, cvt.w.s), 16 bits
inline int16_t Tenth(float v) {
    const float t = v / 10.f;
    if (!(t > -32768.f && t < 32768.f)) return 0;
    return (int16_t)(int)t;
}
// the reading's record: the long one (14 bytes: his hips and the book's place) or the short one {0E, 1} (2 bytes). Its length
inline int Record(uint8_t* out, bool first, const float hips[3], const float book[3]) {
    out[0] = kRecord; out[1] = 1;
    if (!first) return 2;
    int n = 2;
    for (int k = 0; k < 3; ++k) { const uint16_t v = (uint16_t)Tenth(hips[k]); out[n++] = (uint8_t)v; out[n++] = (uint8_t)(v >> 8); }
    for (int k = 0; k < 3; ++k) { const uint16_t v = (uint16_t)Tenth(book[k]); out[n++] = (uint8_t)v; out[n++] = (uint8_t)(v >> 8); }
    return n;
}

}  // namespace vbook
