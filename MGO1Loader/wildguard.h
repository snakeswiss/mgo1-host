// wildguard.h - an access of the PS2 code outside its memory and its registers (ps2rt.cpp HwR, HwW): what the log says
// of it, a walk that never ends, and a loop that never ends (v150-host beta 16). Included once, by ps2rt.cpp (and by
// tools/hostpatch/wild_patch.cpp, which puts the same code into an executable built before it).
//
// The PS2 code follows a wild pointer: until beta 15 each new address had its line, with no limit (v150's OnceWild; the
// host line's limit of 64 lines, v133, was lost in the merge) and stayed in a set. On 8, 9 and 10 November 2026 a bot's game
// walked 700 000 addresses, a line each, 5000 a second: the day's log full in two minutes, the game held by its own
// log, its players gone, the watchdog 183 s later (docs/DEDICATED_HOST.md 19).
// Now:
// - the first 8 accesses with the calls before them, 56 more a line each, then one line every 10 s with their count and
//   their range while they go on; no set of addresses any more;
// - a walk: 65536 accesses in a row with no call of the PS2 code between them (a loop that lost its end and goes through
//   the address space), or kFlood accesses in one window of 10 s whatever the calls between them (a walk that calls a
//   function at each step). A headless game (a bot, a doll) says where in CRASH lines (the launchers' and mgo1log's
//   word for an error: the loop's function, the last calls, the registers) and ends with 70: its service starts it
//   again at once. A game with a picture only says it once (its player decides);
// - a loop that never ends (BackEdgeCheck's `endless loop in`): a headless game still in that loop 10 s later, with no
//   frame of the game run meanwhile, ends the same way (wild::LoopStill).
#pragma once
#include "ps2rt.h"
#include <cstdio>
#include <cstring>
#ifdef _WIN32
// (no <windows.h> in the runtime's file: its three functions named here)
extern "C" __declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);
extern "C" __declspec(dllimport) void* __stdcall GetCurrentProcess(void);
extern "C" __declspec(dllimport) int __stdcall TerminateProcess(void*, unsigned int);
#else
#include <chrono>
#endif

namespace ps2 {
extern void (*gLog)(const char* fmt, ...);
extern uint32_t gRing[64][2]; extern unsigned gRingPos;
extern volatile uint32_t gLastCall, gLastCaller;
extern bool gHeadless;                                  // gs.cpp: no picture (MGO1_HEADLESS)
void (*gWildEnd)(int code) = nullptr;                   // (tests) called in place of the process's end
unsigned long long (*gWildClock)() = nullptr;           // (tests) the clock, in ms
volatile unsigned long long gFrames = 0;                // the game's frames as the platform counts them (ps2world.cpp); 0: not counted
namespace wild {
const unsigned long long kWalk = 65536;                 // accesses in a row with no call between them: a walk
// Accesses in one window of 10 s, calls between them or not: a walk too (the review of beta 16: a walk that calls a
// function at each step never has 65536 in a row, and was left to the watchdog as before). The number, from what
// was seen:
// - the game of 9 November that went on some minutes after its first wild read wrote some hundreds of lines in all (a
//   line a new address then): a game that still plays makes few;
// - the bench's same fault (beta 15): 11 974 new addresses within one frame, and no frame after it (the loop below);
// - the server's walk: 5000 lines a second, 50 000 in 10 s, and that while each access waited for its own log line. A
//   walk free of the log makes that in a blink.
// 32 768 in one window: sixty times what the game that went on made in all its minutes, two thirds of what the walk
// made in 10 s when the log held it. A game that reads through wild pointers 3000 times a second for 10 s shows its
// players nothing right; ended, it is back in seconds.
const unsigned long long kFlood = 32768;
const unsigned long long kWindowMs = 10000;
struct State {
    unsigned long long total, reads, writes;
    unsigned long long winAt, winN;                     // the 10 s line: its window's start (ms), its accesses
    uint32_t winLo, winHi, last;
    unsigned long long floodAt, floodN;                 // the flood's window: its start (ms), every access in it
    unsigned ring;                                      // gRingPos at the last access
    unsigned long long row;                             // accesses in a row since a call
    uint32_t rowFirst;
    int told;                                           // lines written one by one
    bool walked;
};
State s;
inline unsigned long long NowMs() {
    if (gWildClock) return gWildClock();
#ifdef _WIN32
    return GetTickCount64();
#else
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
inline size_t Calls(char* b, size_t cap) {              // the last 12 calls: callee<caller
    size_t n = 0;
    b[0] = 0;
    for (unsigned k = 0; k < 12 && n + 20 < cap; ++k) {
        const uint32_t* e = gRing[(gRingPos - 12 + k) & 63];
        n += (size_t)snprintf(b + n, cap - n, " %08x<%08x", e[0], e[1] - 8);
    }
    return n;
}
inline void Registers() {                               // the loop's registers (as a loop that never ends says them)
    const Ctx* r = gLoopCtx;
    if (!r || !gLog) return;
    char b[640];
    int n = snprintf(b, sizeof b, "CRASH: the loop's registers: r");
    for (int i = 1; i < 32 && n < (int)sizeof b - 12; ++i) n += snprintf(b + n, sizeof b - n, " %x", (uint32_t)r->r[i]);
    gLog("%s", b);
}
inline void EndGame() {                                 // a headless game ends with 70; one with a picture goes on
    if (gLog && (gHeadless || gWildEnd)) gLog("CRASH: the game ends here (70): its service starts it again");
    if (gWildEnd) { gWildEnd(70); return; }
#ifdef _WIN32
    if (gHeadless) { fflush(stdout); TerminateProcess(GetCurrentProcess(), 70); }
#endif
}
// a walk: `flood` false, 65536 accesses in a row with no call between them (a slow one); true, kFlood in one window,
// calls between them or not
inline void Walk(const char* what, uint32_t a, bool flood, unsigned long long ms) {
    State& x = s;
    x.walked = true;
    if (gLog) {
        char c[16 * 20];
        Calls(c, sizeof c);
        const char* last = what && what[9] == 'w' ? "write" : "read";
        if (flood)
            gLog("CRASH: the PS2 code reads and writes outside its memory without end: %llu accesses in %llu.%llu s (the last %llu in a row with no call "
                 "between them, from %08x to %08x; the last a %s; %llu reads and %llu writes in all); last call %08x from %08x, last loop in %08x; calls "
                 "before:%s", x.floodN, ms / 1000, ms % 1000 / 100, x.row, x.rowFirst, a, last, x.reads, x.writes, (uint32_t)gLastCall,
                 (uint32_t)gLastCaller - 8, (uint32_t)gLoopFn, c);
        else
            gLog("CRASH: the PS2 code walks outside its memory and never ends: %llu accesses in a row with no call between them, from %08x to %08x "
                 "(the last a %s; %llu reads and %llu writes in all); last call %08x from %08x, last loop in %08x; calls before:%s",
                 x.row, x.rowFirst, a, last, x.reads, x.writes, (uint32_t)gLastCall, (uint32_t)gLastCaller - 8, (uint32_t)gLoopFn, c);
        Registers();
    }
    EndGame();
}

// ---- A loop that never ends. ps2rt.cpp BackEdgeCheck says it once (`endless loop in <function>`: 20000 of its checks
// in a row, 80 million turns, found the same function's loop running) and the game stayed in it: the bench's game with
// the fault of 8 to 10 November made no wild access any more, so no walk ended it, and on a server it waited 183 s for
// the watchdog. A headless game still in that loop kLoopMs later, as many turns again made, and no frame of the game
// run meanwhile, ends as a walk does. The delay: no loop of the game is that long. Its longest ones are a page's
// load and its decompression, well under a second each, and none of them ever reached the report's 80 million turns
// in one function on any bench (no `endless loop` line in a game that played); 10 s is ten to a hundred times the
// report's own time, and a loop that long with no frame of the game at all is a game no player is in any more.
// The frames are the game's own (ps2world.cpp counts one each time the game's main loop reaches its picture's end),
// not the VBlanks: on the bench (G2_class0_noguard, 10 November) the VBlanks, the interrupts and the other threads went
// on at full pace beside the main thread in its loop, 15 700 of them to the run's end, and the game was dead all the
// same. For the same reason another function's loop seen at a check between two reports does not start the count
// again (the other threads' loops still run): only a frame of the game does, or the report of another function's loop.
// Not counted (gFrames 0: no platform, or no frame of the game yet): never.
// A game with a picture stays as it was: the report alone, its player decides.
const unsigned long long kLoopMs = 10000;
const long long kLoopChecks = 20000;                    // BackEdgeCheck's own count for its report
struct Loop { uint32_t fn; unsigned long long at, frames; bool ended; };
Loop loop;
inline void LoopStill(uint32_t fn, long long checks);
inline void LoopSeen(uint32_t fn) {                     // the report's count reached (again: 20000 more of its checks in a row)
    if (loop.fn != fn) { loop.fn = fn; loop.at = NowMs(); loop.frames = gFrames; return; }
    LoopStill(fn, kLoopChecks);
}
inline void LoopLeft() {}                               // another function's loop at a check: see above, the game's frames decide
inline void LoopStill(uint32_t fn, long long checks) {  // the same function's loop, `checks` in a row now
    (void)checks;
    if (!loop.fn || loop.fn != fn || loop.ended || !(gHeadless || gWildEnd)) return;
    const unsigned long long frames = gFrames;
    if (!frames) return;
    const unsigned long long now = NowMs();
    if (frames != loop.frames) { loop.frames = frames; loop.at = now; return; }
    if (now - loop.at < kLoopMs) return;
    loop.ended = true;
    if (gLog) {
        char c[16 * 20];
        Calls(c, sizeof c);
        // (its accesses outside the memory, if any: a burst before the loop has no line of its own once the 64 are said)
        gLog("CRASH: the PS2 code is in a loop that never ends: the loop of %08x, %llu s more and no frame of the game run (frame %llu; %llu reads "
             "and %llu writes outside the memory before it); last call %08x from %08x; calls before:%s", fn, (now - loop.at) / 1000, frames, s.reads,
             s.writes, (uint32_t)gLastCall, (uint32_t)gLastCaller - 8, c);
        Registers();
    }
    EndGame();
}
}  // namespace wild

// an access through a wild pointer (outside memory and the registers): 0 read, nothing written
void OnceWild(const char* what, uint32_t a, uint32_t w) {
    wild::State& x = wild::s;
    ++x.total;
    if (what && what[9] == 'w') ++x.writes; else ++x.reads;
    if (gRingPos != x.ring || !x.row) { x.ring = gRingPos; x.row = 0; x.rowFirst = a; }
    ++x.row;
    // the flood's window: every access counted, its clock read at each of a window's first 256, then once in 256
    if (!x.floodN) x.floodAt = wild::NowMs();
    ++x.floodN;
    unsigned long long floodMs = 0;
    if (x.floodN <= 256 || (x.floodN & 0xff) == 1 || x.floodN >= wild::kFlood) {
        floodMs = wild::NowMs() - x.floodAt;
        if (floodMs >= wild::kWindowMs) { x.floodAt += floodMs; x.floodN = 1; floodMs = 0; }
    }
    if (gLog) {
        if (x.told < 8) {                               // the first ones with the calls before them
            ++x.told;
            char c[16 * 20];
            wild::Calls(c, sizeof c);
            gLog("ps2: %s at %08x (%08x); calls before:%s", what, a, w, c);
        } else if (x.told < 64) {
            if (++x.told == 64) {
                gLog("ps2: %s at %08x (%08x): 64 accesses outside the memory and the registers said one by one, the next ones a line every 10 s", what, a, w);
                x.winAt = wild::NowMs(); x.winN = 0; x.winLo = 0xffffffffu; x.winHi = 0;
            } else gLog("ps2: %s at %08x (%08x)", what, a, w);
        } else {
            ++x.winN;
            if (a < x.winLo) x.winLo = a;
            if (a > x.winHi) x.winHi = a;
            // (the clock: at each of a window's first 256 accesses, a few after a line were told hours later; then once in 256)
            if (x.winN <= 256 || (x.winN & 0xff) == 1) {
                const unsigned long long now = wild::NowMs();
                if (now - x.winAt >= wild::kWindowMs) {
                    gLog("ps2: %llu more accesses outside the memory and the registers in %llu s (from %08x to %08x, the last a %s at %08x; %llu reads and "
                         "%llu writes since the start): the PS2 code follows a wild pointer", x.winN, (now - x.winAt) / 1000, x.winLo, x.winHi,
                         what && what[9] == 'w' ? "write" : "read", a, x.reads, x.writes);
                    x.winAt = now; x.winN = 0; x.winLo = 0xffffffffu; x.winHi = 0;
                }
            }
        }
    }
    x.last = a;
    if (x.walked) return;
    if (x.row >= wild::kWalk) wild::Walk(what, a, false, 0);
    else if (x.floodN >= wild::kFlood) wild::Walk(what, a, true, floodMs);
}
}  // namespace ps2
