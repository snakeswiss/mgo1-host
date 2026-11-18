// vdoll_stamina.h - (v150-host beta 24, stamina2) what moving costs a player's stun gauge, the game's own rule (docs/VIRTUAL_DOLL.md
// 41), apart from the host's memory so that the tests check it.
// US n003a: the local player's tick 0x4143F0 gives the engine's stamina system a posture code (0x414420 to 0x414838, from his state
// flags: 1 walking (the flag 0xB), 3 running (0xE with 0xC or 0xD), 4 crawling (0xE with 3, lying), 6 climbing a ladder (0xE with
// 0x52), 2, 5, 7, 8 other moves) through 0x306B50; the engine (0x3065E0) counts the player's ticks with a code (+0x424) and at the
// 31st takes that code's cost off the gauge (0x3033A8, through its pointer to +0xB68), the cost times (his load / 8000 + 0.5); 31
// ticks with no code (+0x428) start the count again. The costs are the stage script's (its command 0x3033D0 sets them in play):
// on a real client (bench stamina2_cal, n003a) 7 walking, 14 running, 14 crawling, 42 on a ladder, 98 code 8; the load the
// weights of what he carries (the page's weapons table 0x2DAB38 and items table 0x2DCB98, +0x20 each; 0x2FE080 and 0x2FE0F0).
// The gauge is never taken to 0 by a move (0x414518: 1 left).
#pragma once
#include <algorithm>

namespace vstam {

const int kTicksASecond = 30;                    // (a player's ticks: 29.97 a real second, as vdoll_mash.h)
const int kPeriod = 31;                          // (the 31st tick with a code: (10 + 299) / 10 = 30 < the count)
const int kRest = 31;                            // (31 ticks with none: the count starts again)
const int kHitHold = 60;                         // (a hit's 60 ticks (+0x44C, (10 + 599) / 10) with no new start of the count)
// the stage's costs by posture code (0 still; measured: 1, 3, 4, 6; 2, 5, 7, 8 as the script set them, not used by a doll)
const int kCost[9] = {0, 7, 28, 14, 14, 28, 42, 14, 98};
// a cost taken off: the code's cost times (load / 8000 + 0.5), cut to a whole number (0x306814 to 0x306820)
inline int Cost(int code, int load) {
    if (code <= 0 || code > 8) return 0;
    return (int)((float)kCost[code] * ((float)load / 8000.f + 0.5f));
}
// the game's weights (+0x20 of each row): a weapon by its code, an item by its code
inline int WeaponWeight(int code) {
    static const int w[0x1D] = {0, 500, 100, 300, 100, 800, 1000, 500, 1000, 1500, 1300, 2900, 3500, 4500, 2900, 4600, 4400, 6300, 500,
                                300, 800, 500, 800, 500, 100, 1600, 1600, 1600, 100};
    return code >= 0 && code < 0x1D ? w[code] : 0;
}
// the items every soldier's list held on a real client (codes 0x06, 0x0E, 0x15: 1000, 600, 100), the knife's weight (code 1)
const int kItemsLoad = 1700, kKnife = 1;
// the counts of the engine (+0x424, +0x428, +0x44C) for one player; Tick: one player tick with this code; the cost taken now (0: none)
struct Mover {
    int moving = 0, still = 0, hold = 0;
    int drains = 0, spent = 0;
    void Reset() { moving = still = hold = 0; }
    void Hit() { hold = kHitHold; }              // (a hit on him: +0x44C)
    int Tick(int code, int load) {
        if (hold > 0) --hold;
        if (code > 0) {
            if (++moving < kPeriod) return 0;
            moving = still = 0;
            const int c = Cost(code, load);
            if (c > 0) { ++drains; spent += c; }
            return c;
        }
        ++still;
        if (hold <= 0 && still >= kRest) moving = still = 0;
        return 0;
    }
};
// the gauge after a move's cost (never 0 from a move: 1 left)
inline int After(int gauge, int cost) { return gauge > 0 ? std::max(1, gauge - cost) : gauge; }

}  // namespace vstam
