// exact 60's pace guard (docs/FPS60.md, A machine that cannot draw 60 pictures a second): told each tick how long
// the last one took and how the GS did with its pictures, it says whether the next tick leaves its picture between out
#pragma once

struct PaceGuard {
    static constexpr int kWindow = 30;          // the ticks at 60 looked at (a second)
    static constexpr int kWindowLate = 10;      // late ones among them that start a pause (a third)
    static constexpr int kRowLate = 4;          // late ones in a row that start it at once
    static constexpr int kBackLate = 4;         // late ones among the first 30 after a pause that start the next
    static constexpr int kPause = 30;           // a pause's ticks (a second)
    static constexpr int kMoreMax = 9;          // times in a row a pause goes on (10 s), then 60 again
    static constexpr int kPicsMin = 8;          // pictures timed in a pause to judge it

    enum Event { kNone, kStart, kMore, kBack };
    bool late[kWindow] = {};
    int at = 0, inWindow = 0, lateInWindow = 0, row = 0;
    int left = 0, more = 0;                     // ticks left in the pause, the times it went on
    int pics = 0, slow = 0;                     // the pause's pictures timed on the GS, those over a field
    bool measuredPaused = false;                // the tick measured next had no picture between
    bool justBack = false;                      // the window holds the first ticks after a pause
    Event event = kNone;                        // what the last Tick did (for the log)
    int eventLate = 0, eventRow = 0, eventWindow = 0;   // kStart: the window's late ticks, in a row, its ticks
    int eventPics = 0, eventSlow = 0;           // kMore, kBack: the pictures it was judged on
    long long ticks = 0, lateTicks = 0, pauses = 0, mores = 0, pausedTicks = 0;

    void Clear() { for (bool& b : late) b = false; at = inWindow = lateInWindow = row = 0; }
    // took: the VBlanks between the last two tick pictures, planned: those it had (2; PAL at 60 2 or 3);
    // timed / over: the pictures timed on the GS since the last call (kick to FINISH, the precise clock), and those
    // that took longer than a field (late at 60). True: the next tick without its picture between
    bool Tick(long long took, int planned, int timed, int over) {
        event = kNone;
        ++ticks;
        const bool isLate = took > planned && took <= 30;          // longer: a load, not the pace
        if (isLate) ++lateTicks;
        if (measuredPaused) { pics += timed; slow += over; }
        else if (took <= 30) {
            if (inWindow == kWindow) lateInWindow -= late[at] ? 1 : 0;
            else ++inWindow;
            late[at] = isLate; lateInWindow += isLate ? 1 : 0;
            at = (at + 1) % kWindow;
            row = isLate ? row + 1 : 0;
            if (inWindow == kWindow) justBack = false;
        }
        bool pause = false;
        if (left > 0) {
            pause = true;
            if (--left == 0) {                                      // the pause's last tick
                eventPics = pics; eventSlow = slow;
                if (pics >= kPicsMin && slow * 4 >= pics && more < kMoreMax) { left = kPause; ++more; ++mores; event = kMore; }
                else { event = kBack; justBack = true; }            // a quarter would not make 60: on; else 60
                pics = slow = 0;
            }
        } else if (lateInWindow >= (justBack ? kBackLate : kWindowLate) || row >= kRowLate) {
            event = kStart;
            eventLate = lateInWindow; eventRow = row; eventWindow = inWindow;
            pause = true;
            left = kPause - 1; more = 0; pics = slow = 0; justBack = false;
            ++pauses;
            Clear();
        }
        if (pause) ++pausedTicks;
        measuredPaused = pause;
        return pause;
    }
};
