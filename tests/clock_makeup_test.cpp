// (v150-host beta 19) the round's clocks of a dedicated host short of processor (MGO1Loader/clocks.h,
// docs/DEDICATED_HOST.md 23). A model of the host's kernel loop (a VBlank served when due, one more than two fields late
// not caught up, one pass of the actor lists a VBlank served) on a processor cut by a quota in 100 ms periods (the
// owner's server, reports/pace.md), the capture's countdown (900 passes) and the round's time (5 a pass) counted as the
// game counts them, with the make-up of clocks.h. `--before`: without the make-up (beta 18): the slowed host's checks
// fail. `--beta19`: the countdown as beta 19 made it up (the host's lost passes, 3 a pass at most, the rest dropped):
// the slowed host's countdown checks fail (8 to 11 % long, what the final build's bench measured).
//   g++ -O1 -std=c++17 -I MGO1Loader tests/clock_makeup_test.cpp -o /tmp/clock_makeup_test && /tmp/clock_makeup_test
#include "clocks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <random>

static int gFails = 0;
static bool gBefore = false, gBeta19 = false;
static void Check(bool ok, const char* what) { printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFails; }

static const long long kUs = 1000;                           // ticks: ns
static const long long kField = 1001LL * 1000000 / 60;       // 16683 us (59.94 a second), in ns

// the host: each pass costs `cost` of processor; the quota gives `quota` of it in each `period` (0: no quota); `jitter`:
// a random delay of the wake up to that much. Runs until the clock ends; returns the seconds of the wall clock.
struct Host {
    long long cost, quota, period, jitter;
    long long t = 0, due = kField, periodAt = 0, used = 0;
    std::mt19937 rng{7};
    long long passes = 0;
    void Work(long long c) {                                 // c of processor, frozen at the quota's end until the next period
        while (c > 0) {
            if (quota && t >= periodAt + period) { periodAt += ((t - periodAt) / period) * period; used = 0; }
            if (quota && used >= quota) { t = periodAt + period; continue; }
            long long run = c;
            if (quota) run = std::min(run, quota - used);
            t += run; c -= run; used += run;
        }
    }
    // the next pass: its start time
    long long NextPass() {
        if (t < due) t = due;                                // idle to the VBlank
        if (jitter) t += (long long)(rng() % (unsigned)jitter);
        if (quota && t >= periodAt + period) { periodAt += ((t - periodAt) / period) * period; used = 0; }
        if (quota && used >= quota) t = periodAt + period, periodAt = t, used = 0;
        const long long now = t;
        due += kField;
        if (now - due > kField) due = now + kField;          // late: no burst of catch-up fields
        ++passes;
        return now;
    }
};

// the capture's countdown: 900 passes from its start; returns its seconds on the wall clock, made: passes made up
// (v150-host beta 20: the countdown on its own account, clocks.h ShortClock, as ps2world.cpp ClocksTick calls it)
// step: the most a pass took off the countdown, the game's own one included; first: what its first pass took
static double Countdown(Host h, int start, long long* made, long long* passes, int* step = nullptr, int* first = nullptr,
                        long long before = 30000000000LL) {
    ClockMakeUp mk;
    ShortClock sc;
    int left = start;
    long long t0 = -1;
    if (step) *step = 0;
    // the round's time runs 30 s before the item is set (the host's slowness judged meanwhile, as in a game)
    const long long setAt = h.t + before;
    for (int i = 0; i < 1000000; ++i) {
        const long long now = h.NextPass();
        const int n = mk.Pass(now, kField, true, h.passes);
        if (now < setAt) { h.Work(h.cost); continue; }
        const int was = left;
        if (gBeta19) left = MakeUpCountdown(left, start + 1, n < 3 ? n : 3);   // (start + 1: the value set before the first pass)
        else if (!gBefore) left = sc.Pass(now, kField, left > 1 && left < start + 1, mk.slowed, left);
        if (t0 < 0) { t0 = now; if (first) *first = was - left; }
        if (left > 0) --left;                                // the game's own pass (0x51DC80: one a call)
        if (step && was - left > *step) *step = was - left;
        if (left == 0) { if (made) *made = mk.made; if (passes) *passes = mk.passes; return (double)(now - t0) / 1e9; }
        h.Work(h.cost);
    }
    return -1;
}
// the round's time: `secs` of it (300 a second), 5 a pass (the game ends it under 5)
static double RoundTime(Host h, int secs) {
    ClockMakeUp mk;
    int left = secs * 300;
    long long t0 = -1;
    for (int i = 0; i < 10000000; ++i) {
        const long long now = h.NextPass();
        const int n = mk.Pass(now, kField, left > 0);
        if (!gBefore) left = MakeUpTimeLeft(left, 5, n);
        if (t0 < 0) t0 = now;
        left = left < 5 ? 0 : left - 5;
        if (left == 0) return (double)(now - t0) / 1e9;
        h.Work(h.cost);
    }
    return -1;
}

int main(int argc, char** argv) {
    gBefore = argc > 1 && !strcmp(argv[1], "--before");
    gBeta19 = argc > 1 && !strcmp(argv[1], "--beta19");
    printf("the clocks' make-up %s\n", gBefore ? "OFF (beta 18)" : gBeta19 ? "on, the countdown as beta 19 (3 a pass, the rest dropped)" : "on");
    // 1. the values: the clocks' floors and what is left alone
    Check(MakeUpCountdown(900, 900, 10) == 900, "the countdown at its start value (not begun) left alone");
    Check(MakeUpCountdown(500, 900, 10) == 490, "10 passes off a running countdown");
    Check(MakeUpCountdown(5, 900, 10) == 1, "never under 1: the game's own pass ends it");
    Check(MakeUpCountdown(0, 900, 3) == 0 && MakeUpCountdown(-1, 900, 3) == -1 && MakeUpCountdown(1, 900, 3) == 1, "an ended or idle countdown left alone");
    Check(MakeUpTimeLeft(36000, 5, 4) == 35980 && MakeUpTimeLeft(12, 5, 3) == 10 && MakeUpTimeLeft(10, 5, 3) == 10 && MakeUpTimeLeft(0, 5, 3) == 0,
          "the round's time: 5 a pass off, never under two passes");
    {   // a stall over a second (a load) is not made up; the clocks stopped: nothing carried
        ClockMakeUp mk;
        mk.Pass(1000, kField, true);
        int n = mk.Pass(1000 + 5000000000LL, kField, true);
        Check(n == 0, "a pass 5 s after the last (a stall): nothing made up");
        mk.Pass(1000 + 5000000000LL + 10 * kField, kField, false);
        n = mk.Pass(1000 + 5000000000LL + 11 * kField, kField, true);
        Check(n == 0 && mk.debt == 0, "the clocks stopped then running again: no debt carried");
    }
    // 2. a host at the game's pace (no quota, a pass 3 ms, the wake up to 3 ms late): nothing to make up
    {
        Host h{3000 * kUs, 0, 0, 3000 * kUs};
        long long made = 0, passes = 0;
        const double s = Countdown(h, 900, &made, &passes);
        printf("   at the pace: the countdown %.3f s, %lld passes made up of %lld\n", s, made, passes);
        Check(fabs(s - 900 * 1.001 / 60) < 0.1, "at the game's pace: the capture's countdown 15.0 s");
        Check(made == 0, "at the game's pace: nothing made up");
    }
    // 2b. at the game's pace with a late VBlank now and then (a 40 ms hiccup every 2.5 s, beta 18's benches: 10 to 40 VBlanks
    // a minute more than a field late): nothing made up, the clocks read exactly as without the make-up
    {
        ClockMakeUp mk;
        long long t = 0, made = 0; int left = 900, frames = 0;
        for (int i = 0; left > 0 && i < 100000; ++i) {
            t += kField; if (i % 150 == 149) t += 40000 * kUs;
            const int n = mk.Pass(t, kField, true);
            left = MakeUpCountdown(left, 901, n); made += n; --left; ++frames;
        }
        printf("   at the pace with hiccups: %lld passes made up, the countdown %d passes\n", made, frames);
        Check(made == 0 && frames == 900, "at the game's pace with late VBlanks: nothing made up, 900 passes as beta 18");
    }
    // 2c. at the game's pace with a hiccup of 0.4 s every 20 s and one at the clocks' start: nothing made up
    {
        ClockMakeUp mk;
        long long t = 0, made = 0;
        for (int i = 0; i < 60 * 120; ++i) {
            t += kField; if (i % 1200 == 30) t += 400000 * kUs;
            made += mk.Pass(t, kField, true);
        }
        Check(made == 0, "at the game's pace with a 0.4 s hiccup every 20 s: nothing made up");
    }
    // 2d. the host at its VBlanks' pace whose passes are fewer (a heavy map: one pass in 20 takes two fields, beta 19's
    // first seven-rules bench): its pace line reads 59.9, nothing may change
    {
        ClockMakeUp mk;
        long long t = 0, v = 0, made = 0;
        for (int i = 0; i < 60 * 120; ++i) {
            t += kField; ++v; if (i % 20 == 0) { t += kField; ++v; }
            made += mk.Pass(t, kField, true, v);
        }
        Check(made == 0, "VBlanks at the pace, a pass in 20 lost: nothing made up (the pace decides)");
    }
    // 2e. at the pace with a hitch of 2 s once in a round (a 3 s longer round in beta 19's second seven-rules bench): nothing
    {
        ClockMakeUp mk;
        long long t = 0, v = 0, made = 0;
        for (int i = 0; i < 60 * 120; ++i) {
            t += kField; ++v; if (i == 3000) { t += 2000000 * kUs / 2; }    // 1 s ...
            if (i == 3060) { t += 1000000 * kUs; }                         // ... and 1 s more a second later
            made += mk.Pass(t, kField, true, v);
        }
        Check(made == 0, "at the pace, a hitch of 2 s in a round: nothing made up");
    }
    // 3. the owner's server: a pass 8.2 ms of processor, 28 ms of it in each 100 ms (reports/pace.md: 32 to 46 passes a second)
    {
        Host h{8200 * kUs, 28000 * kUs, 100000 * kUs, 0};
        long long made = 0, passes = 0;
        int step = 0, first = 0;
        const double s = Countdown(h, 900, &made, &passes, &step, &first);
        printf("   the owner's server: the countdown %.3f s, %lld passes made up (%lld passes seen with the 30 s before); "
               "%d a pass at most, %d at its first\n", s, made, passes, step, first);
        Check(fabs(s - 900 * 1.001 / 60) < 0.12, "a host at a third of a core: the capture's countdown 15.0 s of the wall clock");
        Check(gBefore || (step <= 1 + ShortClock::kMaxAPass && first == 0),
              "a host at a third of a core: no jump (5 a pass at most), nothing from before its start off the countdown");
        const double r = RoundTime(h, 120);
        printf("   the owner's server: a round of 120 s lasts %.2f s\n", r);
        Check(fabs(r - 120.0) < 0.5, "a host at a third of a core: a round of 120 s lasts 120 s");
        const double back = RoundTime(h, 180);              // the lost item's return home: 54000 (5 a pass, 180 s), the same count
        printf("   the owner's server: a lost item back home after %.2f s\n", back);
        Check(fabs(back - 180.0) < 0.6, "a host at a third of a core: a lost item back home after 180 s");
    }
    // 4. a host at half its passes (a pass 16 ms in 20 of each 100 ms: about 15 passes a second)
    {
        Host h{16000 * kUs, 20000 * kUs, 100000 * kUs, 0};
        const double s = Countdown(h, 900, nullptr, nullptr);
        printf("   a host at a fifth of a core: the countdown %.3f s\n", s);
        Check(fabs(s - 900 * 1.001 / 60) < 0.15, "a host at a fifth of a core: the capture's countdown 15.0 s");
        const double s20 = Countdown(h, 1200, nullptr, nullptr);
        printf("   a host at a fifth of a core: map 2's countdown (1200 passes) %.3f s\n", s20);
        Check(fabs(s20 - 1200 * 1.001 / 60) < 0.15, "a host at a fifth of a core: a countdown of 1200 passes 20.0 s");
    }
    // 5. (v150-host beta 20) the bench's quota (18 % of a core in 100 ms periods, a pass 7 ms: 23 to 29 passes a second,
    // runs/V19_slow_cap: beta 19's countdown 21.7 and 22.3 s for 20)
    {
        Host h{7000 * kUs, 18000 * kUs, 100000 * kUs, 2000 * kUs};
        int step = 0, first = 0; long long passes = 0;
        const double s = Countdown(h, 1200, nullptr, &passes, &step, &first);
        printf("   the bench's quota: the countdown of 1200 passes %.3f s (%lld passes with the 30 s before), %d a pass at most\n", s, passes, step);
        Check(fabs(s - 1200 * 1.001 / 60) < 0.12, "a host under a quota of 18 %: the countdown of 1200 passes 20.0 s");
        Check(gBefore || step <= 1 + ShortClock::kMaxAPass, "a host under a quota of 18 %: 5 passes a pass at most");
    }
    // 6. the host turns slowed in the middle of a countdown (the item set 4 s after the clocks' start: the host is judged
    // slowed 12 s later at the earliest): the countdown catches up 5 a pass at most (no jump), nothing dropped
    if (!gBefore) {
        Host h{8200 * kUs, 28000 * kUs, 100000 * kUs, 0};
        int step = 0;
        const double s = Countdown(h, 1800, nullptr, nullptr, &step, nullptr, 4000000000LL);
        printf("   slowed in the middle of a countdown of 1800 passes: %.3f s, %d a pass at most\n", s, step);
        Check(step <= 1 + ShortClock::kMaxAPass, "the host turns slowed in the middle of a countdown: 5 a pass at most");
        Check(gBeta19 || fabs(s - 1800 * 1.001 / 60) < 0.15, "the host turns slowed in the middle of a countdown of 30 s: 30.0 s all the same");
    }
    // 7. the short clock's account alone: a pause, a restart, a stall are not made up; never under 1, never raised
    if (!gBefore && !gBeta19) {
        ShortClock sc; long long t = 0; int v = 600;
        auto pass = [&](long long dt, bool dec) { t += dt; if (dec) --v; const int was = v; v = sc.Pass(t, kField, v > 1 && v < 601, true, v); return was - v; };
        int took = 0;
        for (int i = 0; i < 100; ++i) took += pass(2 * kField, true);          // a host at half the pace: one more off a pass
        Check(took >= 97 && took <= 99 && v >= 401 && v <= 403, "a host at half the pace: one pass more off a pass, from the second pass");
        const int at = v; took = 0;
        for (int i = 0; i < 50; ++i) took += pass(2 * kField, false);          // the game does not count (a pause): nothing
        Check(took == 0 && v == at, "the countdown paused by the game: nothing taken off, the pause not made up after it");
        took = pass(2 * kField, true);
        Check(took <= 1 && v >= at - 2, "the countdown running again: its account from the last paused pass (a pass's own loss at most)");
        took = pass(5000000000LL, true);
        Check(took == 0, "a pass 5 s after the last (a stall): nothing taken off");
        v = 601; pass(2 * kField, false); took = 0;                             // back to its start value (the item taken out), set again
        for (int i = 0; i < 10; ++i) took += pass(2 * kField, true);
        Check(took >= 8 && took <= 9 && v >= 581, "the countdown set again: its account from its new start");
        v = 4; took = 0; for (int i = 0; i < 6 && v > 0; ++i) took += pass(6 * kField, true);
        Check(v >= 0 && sc.last >= 0, "the countdown's end: never under 1 by the make-up (the game's own pass ends it)");
        ShortClock at1; long long tt = 0; int w = 300, raised = 0, off = 0;
        for (int i = 0; i < 280; ++i) { tt += kField + (i % 7 == 0 ? 3000 * kUs : 0) - (i % 7 == 1 ? 3000 * kUs : 0); --w; const int o = at1.Pass(tt, kField, true, false, w); if (o > w) ++raised; if (o != w) ++off; w = o; }
        Check(raised == 0 && off == 0, "a host not slowed: the short clock reads as the game counts it");
    }
    printf(gFails ? "FAILED: %d\n" : "ALL PASSED\n", gFails);
    return gFails ? 1 : 0;
}
