// (v150-host beta 19) the round's clocks of a dedicated host short of processor, made up on the wall clock
// (docs/DEDICATED_HOST.md 23). The game counts the clocks of a round in passes of its actor lists: the round's time
// takes 5 (1/300 s) a pass, the capture's countdown one a pass (1200 passes on map 2: 20 s), the dropped item's pick-up lock
// one a pass, the lost item's return home 5 a pass (54000: 180 s). A PS2 makes one pass a VBlank, 59.94 a second. A host whose processor share is cut (a VPS's quota:
// its thread frozen 40 to 70 ms in every 100 ms) makes fewer passes than the VBlanks of the clock (a VBlank more than
// two fields late is not caught up, docs/DEDICATED_HOST.md 8), and every one of those clocks ran that much slower
// (11 November: 32 to 46 passes a second, the capture's countdown 38 s instead of 20). Each pass, the wall time not
// covered by passes is counted; whole passes of it are taken off the running clocks, so they keep the PS2's time
// whatever the passes cost and without one more pass (the processor share unchanged). Only a host slowed for good (the
// VBlanks it served in 10 s under 54.5 a second, 12 s in a row) makes up: at the game's pace every clock reads exactly as before.
#pragma once

struct ClockMakeUp {
    static constexpr int kGapMaxFields = 60;     // a pass more than a second after the last: a stall or a load, not made up
    static constexpr int kCreditFields = 4;      // passes ahead of the clock (back to back after a late one) counted, at most
    static constexpr int kRing = 600;            // the passes' times kept (10 s at the pace)
    static constexpr int kWindowFields = 600;    // slowed: the passes of the last 10 s (6 s at least since the clocks run)
    static constexpr int kFirstFields = 360;     //   cover less than 1 / 1.1 of it (under 54.5 passes a second)
    long long last = 0;                          // the last pass's time (0: none yet)
    long long since = 0;                         // the clocks running since (or since the last stall)
    long long debt = 0;                          // the wall time the passes did not cover since then (ticks)
    long long made = 0, passes = 0;              // passes made up, passes seen (the log's figures)
    long long ring[kRing] = {}, ringV[kRing] = {};   // the passes' times and the VBlanks served then
    int at = 0, have = 0;
    bool slowed = false;
    long long lowSince = -1;                     // the window under 54.5 a second since then (-1: not)
    static constexpr int kHoldFields = 720;      // slowed: the window under it 12 s in a row (one hitch keeps it under 10 s at most)

    // a pass starts at `now` (ticks; `field` ticks a pass at the game's pace); running: a clock of the round runs.
    // Returns the passes to take off the running clocks now. The lost wall time is counted from the clocks' start;
    // it is given back only while the host is slowed (the VBlanks it served in the last 10 s, its pace line's count, under
    // 54.5 a second, 12 s in a row: a hitch of a few seconds never; without that count its passes): a host at the
    // game's pace, a late VBlank or a hiccup of 0.4 s included, reads exactly as before.
    int Pass(long long now, long long field, bool running, long long vblanks = -1) {
        if (!running || !last || field <= 0) { last = since = now; debt = 0; have = 0; slowed = false; lowSince = -1; return 0; }
        long long d = now - last;
        last = now;
        ++passes;
        if (d > kGapMaxFields * field) { d = field; since = now; have = 0; }   // a stall: not made up, counted again from now
        debt += d - field;
        if (debt < -kCreditFields * field) debt = -kCreditFields * field;
        ring[at] = now; ringV[at] = vblanks; at = (at + 1) % kRing; if (have < kRing) ++have;
        const long long run = now - since;
        slowed = false;
        if (run >= kFirstFields * field) {
            const long long win = run < kWindowFields * field ? run : kWindowFields * field;
            long long count = 0, firstV = -1, firstT = now;
            for (int k = 0; k < have; ++k) if (ring[k] > now - win) { ++count; if (ring[k] < firstT) { firstT = ring[k]; firstV = ringV[k]; } }
            if (vblanks >= 0 && firstV >= 0) count = vblanks - firstV;   // (the host) the VBlanks served, as its pace line counts them
            const bool low = count * field * 11 < win * 10;
            if (!low) lowSince = -1;
            else if (lowSince < 0) lowSince = now;
            slowed = low && now - lowSince >= kHoldFields * field;
        } else lowSince = -1;
        if (!slowed || debt < field) return 0;
        const long long n = debt / field;
        debt -= n * field;
        made += n;
        return (int)n;
    }
};

// (v150-host beta 20) a short clock (the capture's countdown, an item's pick-up lock: one a pass) on its own account of
// the wall clock. Beta 19 took the host's lost passes off it, 3 a pass at most (so that what the host lost before the
// countdown began never came off it): what a pass owed above 3 after a freeze of the processor was dropped, and the
// countdown stayed 8 to 11 % long on a slowed host (21.7 and 22.3 s for 20, CHANGES.md's pass 175). Here the clock
// reads, from the pass it began to run (or ran on after a pause, a restart or a stall), its value then less the
// fields of the wall clock since: nothing from before its start can come off it, and what a pass owes is carried to
// the next ones, kMaxAPass a pass at most (a host that turns slowed in the middle of a countdown: no jump of
// seconds; a host at a fifth of the pace still keeps up). Only while the host is slowed; never under 1, never raised.
struct ShortClock {
    static constexpr int kMaxAPass = 4;
    bool on = false;
    long long t0 = 0, lastAt = 0;   // the pass its account began at; the last pass
    int v0 = 0, last = 0;           // its value at t0; its value after the last pass (the game takes one off by the next)
    long long made = 0;             // passes taken off (the log's figure)
    // a pass starts at `now`; counting: the clock runs; slowed: the host makes up (ClockMakeUp::slowed); v: its reading.
    // Returns the value it should read (v: nothing to write).
    int Pass(long long now, long long field, bool counting, bool slowed, int v) {
        if (!counting || field <= 0) { on = false; return v; }
        const bool stall = on && now - lastAt > ClockMakeUp::kGapMaxFields * field;
        if (!on || stall || v != last - 1) { on = true; t0 = lastAt = now; v0 = last = v; return v; }   // its account from here
        lastAt = now;
        const long long target = (long long)v0 - (now - t0) / field;
        int out = v;
        if (slowed && v > target) {
            const long long owed = (long long)v - target;
            const int take = owed > kMaxAPass ? kMaxAPass : (int)owed;
            out = v - take > 1 ? v - take : 1;
            made += v - out;
        }
        last = out;
        return out;
    }
};

// a countdown of passes running from `start` down to 0 (the game ends at 0 on its own pass): `n` passes off, never
// under 1; the start value itself (the item set, the countdown not begun) and an ended one stay as they are
inline int MakeUpCountdown(int left, int start, int n) {
    if (n <= 0 || left <= 1 || left >= start) return left;
    return left - n > 1 ? left - n : 1;
}
// a time left in 1/300 s that the game takes `unit` off a pass and ends under `unit`: `n` passes off, never under two
// units, so the game's own pass ends it
inline int MakeUpTimeLeft(int left, int unit, int n) {
    if (n <= 0 || unit <= 0 || left <= 2 * unit) return left;
    const long long v = (long long)left - (long long)n * unit;
    return v > 2 * unit ? (int)v : 2 * unit;
}
