// (v150-host beta 21) the virtual dolls and the traps a player's own game decides (docs/VIRTUAL_DOLL.md 29).
// A claymore goes off for one player only in each program: that program's own. Its sensor asks, for a target in it:
// is this target's owner this program's player, and is he neither crawling nor a ghost? A virtual doll has no program:
// the host's copy of each claymore asks it for the dolls, with the doll in the place of the host's own player for
// that one question. This header holds the choice (which player stands in, with which state) apart from the game's
// memory, so that it is tested off line (tests/vdoll_trap_test.cpp); ps2world.cpp's VdollTrapSensor applies it.
#pragma once
#include <cstdint>

namespace vtrap {

// a player of the host's game as the sensor's stand-in sees him
struct Seat {
    bool present = false;                // his player object is there
    bool doll = false;                   // a virtual doll (the host's process speaks for him)
    uint32_t id = 0;                     // his object's id (24 bits): what a target's owner carries
    int life = 0;                        // the host's life for him
    uint32_t bits0 = 0, bits2 = 0, bits10 = 0;   // his state words 0, 2 and 10 as the host's copy holds them
};
// what the game's test reads of "this program's player": his id and three of his state bits
struct Own { uint32_t id = 0, bits0 = 0, bits2 = 0, bits10 = 0; };

// the three state bits that keep a claymore quiet for the player in its sensor (the game's bit numbers 3, 0x57 and
// 0x156): word 0's 0x8 (crawling), word 2's 0x800000, word 10's 0x400000 (a ghost)
const uint32_t kQuiet0 = 0x8u, kQuiet2 = 0x800000u, kQuiet10 = 0x400000u;

// the game's own question, as its code asks it: the target's owner is this program's player and none of the three
// bits is set
inline bool GameAsks(uint32_t ownerId, const Own& own) {
    if ((ownerId & 0xFFFFFFu) != (own.id & 0xFFFFFFu)) return false;
    return !(own.bits0 & kQuiet0) && !(own.bits2 & kQuiet2) && !(own.bits10 & kQuiet10);
}
// the seat that stands in for the target's owner: a living virtual doll with that id; -1: none (a real player's copy,
// the host's own player, a dead or absent doll, the switch off): the game's question is then asked unchanged
inline int StandIn(const Seat* seats, int n, uint32_t ownerId, bool on) {
    if (!on) return -1;
    for (int s = 0; s < n; ++s) {
        const Seat& p = seats[s];
        if (p.present && p.doll && p.life > 0 && (p.id & 0xFFFFFFu) == (ownerId & 0xFFFFFFu)) return s;
    }
    return -1;
}
// "this program's player" for that one question: the doll's id, and the doll's own three bits in the host's words
// (every other bit of the host's words kept)
inline Own With(const Own& host, const Seat& doll) {
    Own o;
    o.id = doll.id & 0xFFFFFFu;
    o.bits0 = (host.bits0 & ~kQuiet0) | (doll.bits0 & kQuiet0);
    o.bits2 = (host.bits2 & ~kQuiet2) | (doll.bits2 & kQuiet2);
    o.bits10 = (host.bits10 & ~kQuiet10) | (doll.bits10 & kQuiet10);
    return o;
}
// the whole choice: does the host's copy of a claymore go off for the owner of a target in its sensor?
inline bool GoesOff(const Seat* seats, int n, uint32_t ownerId, const Own& host, bool on, int* seat = nullptr) {
    const int s = StandIn(seats, n, ownerId, on);
    if (seat) *seat = s;
    return GameAsks(ownerId, s >= 0 ? With(host, seats[s]) : host);
}

}   // namespace vtrap
