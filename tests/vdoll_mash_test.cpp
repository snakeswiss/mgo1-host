// (v150-host beta 22, mash) A knocked-out player's recovery by the game's own rule and a human hand's presses
// (MGO1Loader/vdoll_mash.h, docs/VIRTUAL_DOLL.md 37). Until beta 22 a knocked-out virtual doll got 144 of its stamina every 6
// frames whatever happened: twice the game's natural rate, a player pressing about 7.5 times a second from the first frame.
//   R. the rule (US n003a 0x4143F0): from an empty gauge with no press 1250 ticks (41.7 s; measured on real clients 41.5 s);
//      a press every tick (the cap) 263 ticks with the ring filling; one button's best (every other tick) 427; the ring holds
//      32 ticks and nothing counts while the fall's motion runs; a TEAM SNEAKING defender's rate 25: 5000 ticks;
//   M. the rule against the real clients measured (bench mash_a1: a playable doll darted, its own game's gauge and count each
//      30 frames, the press patterns of MGO1_DOLL_PROBE_MASH): each pattern's knocked-out time within 3 % of the rule's;
//   H. the hand: its rate by level (4 a second at level 1 to 9.5 at 10, never above 15, the one-button cap of the game's 30
//      ticks), its presses never closer than two ticks, its gaps spread (not a machine's regular beat), a reaction first;
//   F. a doll as vdoll.cpp runs it (a knocked-out doll's ticks, its hand by its level): level 10 awake 16.4 to 21.4 s, level 1
//      25.0 to 29.7 s, beta 22's 20.9 s whatever the level (the reason of the change: printed).
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_mash_test.cpp -o /tmp/vdoll_mash_test && /tmp/vdoll_mash_test [<map.geom>]
// (the map is not used by these rules: tests/vdoll_mash_maps.sh runs this once and the flow test's mash scenarios on the 12
// disc maps, the doll's body on each)
#include "vdoll_mash.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int gFails = 0;
static void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++gFails; }

int main() {
    using namespace vmash;
    // ---- R. the rule ----
    Check(TicksToWake(0) == 1250, "R: no press, an empty gauge back in 1250 ticks (41.7 s; real clients 41.5 s)");
    Check(TicksToWake(1.0) == 263, "R: a press every tick (the cap): 263 ticks (8.8 s) with the ring filling");
    Check(TicksToWake(2.0) == 427, "R: one button at its best (a press every other tick): 427 ticks (14.2 s)");
    Check(TicksToWake(0, 0, 0, kRateSlow) == 5000, "R: a TEAM SNEAKING defender (rate 25), no press: 5000 ticks (167 s)");
    {
        Recovery r;
        int g = 0;
        for (int t = 0; t < 32; ++t) g = r.Tick(true, false);
        Check(r.count == 40 && g == 120, "R: 32 presses in 32 ticks: the count 40, 120 a tick");
        for (int t = 0; t < 16; ++t) r.Tick(false, false);
        Check(r.count == 24, "R: 16 ticks with none: 16 left in the ring");
        for (int t = 0; t < 16; ++t) r.Tick(true, true);
        Check(r.count == 8, "R: presses during the fall's motion clear the ring's ticks (none counted)");
        Recovery q;
        for (int t = 0; t < 10; ++t) q.Tick(true, false);
        q.Reset();
        Check(q.Tick(false, false) == 24 && q.count == 8, "R: cleared at the knock-out: 24 a tick (3 x 8)");
    }
    // ---- M. the real clients (bench mash_a1, docs/VIRTUAL_DOLL.md 37): filled once measured ----
    // ---- H. the hand ----
    Check(std::fabs(MashRate(1) - 4.0) < 1e-9 && std::fabs(MashRate(10) - 9.5) < 1e-9 && MashRate(5.5) > MashRate(5) && MashRate(12) == MashRate(10),
          "H: 4 presses a second at level 1 to 9.5 at level 10, rising with the level");
    for (double lv : {1.0, 5.0, 10.0}) {
        Hand h;
        h.Start(MashRate(lv), ReactFrames(lv), 12345u + (unsigned)lv);
        std::vector<int> at;
        for (int t = 0; t < 30 * 60; ++t) if (h.Press(t)) at.push_back(t);
        double sum = 0, sum2 = 0; int closest = 1000;
        for (size_t i = 1; i < at.size(); ++i) { const int g = at[i] - at[i - 1]; sum += g; sum2 += (double)g * g; closest = std::min(closest, g); }
        const double n = (double)at.size() - 1, mean = sum / n, sd = std::sqrt(std::max(0.0, sum2 / n - mean * mean));
        const double rate = kTicksASecond / mean;
        char b[200];
        snprintf(b, sizeof b, "H: level %.0f: %.2f presses a second over a minute (wanted %.2f), the closest two %d ticks apart, the gaps' spread %.0f %%, the first after %d ticks",
                 lv, rate, MashRate(lv), closest, 100.0 * sd / mean, at.empty() ? -1 : at[0]);
        Check(std::fabs(rate - MashRate(lv)) < 0.4 && closest >= 2 && sd / mean > 0.08 && !at.empty() && at[0] >= 9 && at[0] <= 34, b);
    }
    {
        Hand h;
        h.Start(40.0, 0, 7u);
        int n = 0;
        for (int t = 0; t < 300; ++t) n += h.Press(t);
        Check(h.rate == kRateMax && n <= 150, "H: never above 15 a second (one button's cap at the game's 30 ticks)");
    }
    // ---- F. a doll as vdoll.cpp runs it ----
    for (double lv : {1.0, 10.0}) {
        double lo = 99, hi = 0;
        for (unsigned s = 1; s <= 40; ++s) {
            Hand h; Recovery r;
            h.Start(MashRate(lv), ReactFrames(lv), s * 2654435761u);
            int g = 0, t = 0;
            for (; g < kFull && t < 5000; ++t) g = std::min(kFull, g + r.Tick(h.Press(t), false));   // (no fall motion in a knock-out)
            const double sec = t / kTicksASecond;
            lo = std::min(lo, sec); hi = std::max(hi, sec);
        }
        char b[160];
        snprintf(b, sizeof b, "F: level %.0f, 40 knock-outs: awake %.1f to %.1f s after it (beta 22: 20.9 s at any level)", lv, lo, hi);
        Check(lv == 10 ? lo >= 16.4 && hi <= 21.5 : lo >= 25.0 && hi <= 29.8, b);
    }
    printf("%s\n", gFails ? "SOME FAILED" : "ALL PASSED");
    return gFails ? 1 : 0;
}
