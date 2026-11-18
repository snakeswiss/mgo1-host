// vdoll_mash.h - (v150-host beta 22, mash) a knocked-out player's recovery by the game's own rule, and the presses a human hand
// makes meanwhile, for the virtual dolls (docs/VIRTUAL_DOLL.md 37). No platform code: vdoll.cpp runs it for a knocked-out doll;
// tests/vdoll_mash_test.cpp.
//
// The game (US n003a 0x4143F0, each tick of the local player while the state flag 0x99 is set): the tick's bit of a ring of 32
// is set when a press came this tick (a d-pad or face button newly down: the pad word +0x6E0 & 0xF0F0; or a stick turned more
// than a quarter turn past the angle of its last counted press, beyond a dead zone of 36 of 128), cleared otherwise, and
// cleared whatever the pad while the fall's motion runs (the state 0x14F); the gauge then rises by its maximum x (8 + the bits
// set) x the rate / 1 000 000 (rate 100: 3 x (8 + n) a tick; tsne.h gives a TEAM SNEAKING defender 25); at the maximum the
// flag 0x99 goes (awake: the getting up follows). The ring is cleared at the knock-out (0x4147BC). A player ticks 30 times a
// second (the actor loop's second pass, docs/FPS60.md): 1250 ticks (41.7 s) from nothing with no press, 250 (8.3 s) at the cap
// of a press every tick.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vmash {

const int kWindow = 32;                    // (the ring: the presses of the last 32 ticks)
const int kBase = 8;                       // (+0xB77 = 8 + the bits set)
const int kFull = 30000;                   // (+0xB6A, a player's stun gauge's maximum)
const int kRate = 100, kRateSlow = 25;     // (+0xB78: the player's init 0x4051C8; a TEAM SNEAKING defender, tsne.h)
const double kTicksASecond = 59.94 / 2.0;  // (a tick every two VBlanks)

// the game's ring and count (0x41465C to 0x41470C)
struct Recovery {
    uint32_t mask = 0;
    int index = 0;
    int count = 0;                         // (+0xB77 after the last tick)
    void Reset() { mask = 0; index = 0; count = 0; }
    // one tick while knocked out: a press this tick (an edge), the fall's motion now; the gauge's rise this tick
    int Tick(bool pressed, bool falling, int full = kFull, int rate = kRate) {
        if (pressed && !falling) mask |= 1u << index; else mask &= ~(1u << index);
        index = (index + 1) % kWindow;
        int bits = 0;
        for (int k = 0; k < kWindow; ++k) bits += (mask >> k) & 1;
        count = kBase + bits;
        return (int)((long long)full * count * rate / 1000000);
    }
};

// the ticks a player stays knocked out from an empty gauge pressing every `every` ticks from tick `from` (0: none), the
// fall's motion over at tick `fallTicks` (the rule's arithmetic; the tests and the report's table)
inline int TicksToWake(double every, int from = 0, int fallTicks = 0, int rate = kRate, int gauge = 0) {
    Recovery r;
    double next = from;
    for (int t = 0; t < 100000; ++t) {
        bool p = false;
        if (every > 0 && t >= next) { p = true; next += every; if (next < t + 1) next = t + 1; }
        gauge += r.Tick(p, t < fallTicks, kFull, rate);
        if (gauge >= kFull) return t + 1;
    }
    return -1;
}

// ---- the hand: what a person does with the pad while knocked out ----
// Mashing a button (Cross) at `rate` presses a second: each gap spread over 0.75 to 1.25 of the mean, never under two ticks
// (a press and a release, each seen by a tick: one button cannot give two edges in fewer), the first press a reaction after
// the knock-out. A person's sustained mash is 6 to 10 presses a second (a good player's near 10, the hand tires; 15 is the
// one-button cap of the game's 30 ticks); the rate set by the level (MashRate) or the host's MGO1_VDOLL_MASH_RATE.
const double kRateMin = 1.0, kRateMax = 15.0;
// a doll's rate by its level (1 to 10, the live level's too): 4 a second at level 1 to 9.5 at level 10, evenly
inline double MashRate(double level) {
    const double l = std::max(1.0, std::min(10.0, level));
    return 4.0 + (l - 1.0) * (9.5 - 4.0) / 9.0;
}
// its reaction (VBlanks from the knock-out to the first press): 36 at level 10 to 54 at level 1, give or take 12
inline int ReactFrames(double level) {
    const double l = std::max(1.0, std::min(10.0, level));
    return (int)std::lround(54.0 - (l - 1.0) * 2.0);
}
struct Hand {
    double rate = 0;                       // presses a second (0: none)
    double nextTick = 0;                   // the tick of its next press (from the knock-out)
    unsigned seed = 1;
    int presses = 0;
    double U() { seed = seed * 1103515245u + 12345u; return (double)((seed >> 8) & 0xFFFF) / 65535.0; }
    // a new spell: its rate, a reaction in VBlanks before the first press, a seed (a doll and a knock-out of its own)
    void Start(double r, int reactFrames, unsigned s) {
        seed = s ? s : 1; presses = 0;
        rate = r > 0 ? std::max(kRateMin, std::min(kRateMax, r)) : 0;
        const double react = std::max(0, reactFrames + (int)std::lround((U() - 0.5) * 24.0));
        nextTick = react / 2.0;
    }
    // tick t (from the knock-out): a press this tick
    bool Press(int t) {
        if (rate <= 0 || t + 1 <= nextTick) return false;
        ++presses;
        const double gap = std::max(2.0, kTicksASecond / rate * (0.75 + 0.5 * U()));
        nextTick = std::max(nextTick, (double)t) + gap;
        return true;
    }
};

}  // namespace vmash
