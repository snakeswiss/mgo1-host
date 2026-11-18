// wildguard_test.cpp - MGO1Loader/wildguard.h off line (v150-host beta 16): what the log gets of accesses through a wild
// pointer, the end of a walk (with and without calls between its steps) and of a loop that never ends. No game and no
// disc; the clock is the test's own.
//   g++ -std=c++17 -O1 -w -IMGO1Loader tests/wildguard_test.cpp -o /tmp/wildguard_test && /tmp/wildguard_test
#include "wildguard.h"
#include <cstdarg>
#include <string>
#include <vector>

namespace ps2 {
void (*gLog)(const char* fmt, ...) = nullptr;
uint32_t gRing[64][2]; unsigned gRingPos = 0;
volatile uint32_t gLastCall = 0, gLastCaller = 0, gLoopFn = 0;
Ctx* volatile gLoopCtx = nullptr;
bool gHeadless = true;
uint8_t* gMem = nullptr;
}
static std::vector<std::string> gLines;
static int gEnded = 0;
static unsigned long long gNow = 1000000;               // the test's clock (ms)
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    gLines.push_back(b);
}
static void End(int code) { gEnded = code; }
static unsigned long long Clock() { return gNow; }
static int gFail = 0;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s (line %d)\n", what, __LINE__); ++gFail; } else printf("ok   %s\n", what); } while (0)
static int Count(const char* part) { int n = 0; for (const auto& l : gLines) if (l.find(part) != std::string::npos) ++n; return n; }
static void Call(uint32_t fn, uint32_t ra) { ps2::gRing[ps2::gRingPos & 63][0] = fn; ps2::gRing[ps2::gRingPos & 63][1] = ra; ++ps2::gRingPos; ps2::gLastCall = fn; ps2::gLastCaller = ra; }
static void Fresh() { ps2::wild::s = ps2::wild::State{}; ps2::wild::loop = ps2::wild::Loop{}; gLines.clear(); gEnded = 0; }

int main() {
    ps2::gLog = Log;
    ps2::gWildEnd = End;
    ps2::gWildClock = Clock;
    const char* R = "hardware read outside the emulated registers";
    const char* W = "hardware write outside the emulated registers";
    const int flood = (int)ps2::wild::kFlood;
    // 1. the skeleton code's garbage (9 and 10 November): matrices read at wild places, a call every 16 words; fewer than
    //    a flood in each window of 10 s, for 20 windows
    for (int win = 0; win < 20; ++win) {
        for (int k = 0; k < flood - 1000; ++k) {
            if (k % 16 == 0) Call(0x475a58, 0x40bbac);
            ps2::OnceWild(R, 0x1476ed50u + 4u * (uint32_t)(k % 4096), 0);
        }
        gNow += 10000;
    }
    CHECK(Count("calls before:") == 8, "the first 8 accesses with the calls before them");
    CHECK(Count("00475a58<0040bba4") >= 1, "the calls as callee<caller, the caller's own address");
    CHECK(Count("said one by one") == 1, "the 64th line says the next ones come every 10 s");
    CHECK(Count("more accesses outside the memory and the registers in 10 s") == 19, "then a line every 10 s (19 for 20 windows)");
    CHECK((int)gLines.size() == 64 + 19, "and no line an access (600 000 accesses, 83 lines)");
    CHECK(gEnded == 0, "accesses with calls between them, under a flood in each window, do not end the game");
    // 2. a few accesses after a line are told at the next one, 10 s on (they waited for a 257th)
    gLines.clear();
    for (int k = 0; k < 300 && gLines.empty(); ++k) ps2::OnceWild(R, 0x1476ed50u + 4u * (uint32_t)k, 0);   // (the 20th window's line)
    CHECK(gLines.size() == 1, "the last window's line at an access after its 10 s");
    gLines.clear();
    for (int k = 0; k < 100; ++k) ps2::OnceWild(R, 0x1476ed50u + 4u * (uint32_t)k, 0);
    CHECK(gLines.empty(), "100 accesses within the new window: no line yet");
    gNow += 11000;
    ps2::OnceWild(R, 0x1476ed50u, 0);
    CHECK(Count("ps2: 101 more accesses outside the memory and the registers in 11 s") == 1, "one access 11 s later: their line");
    // 3. a walk that calls a function at each step: never 65536 in a row, a flood in one window
    Fresh();
    ps2::Ctx regs{};
    for (int i = 0; i < 32; ++i) regs.r[i] = 0x100000u + (uint32_t)i;
    ps2::gLoopCtx = &regs; ps2::gLoopFn = 0x40bac0;
    uint32_t a = 0x10010000u;
    unsigned long long n = 0;
    while (!gEnded && n < 1000000) { Call(0x40ba50, 0x40bb7c); ps2::OnceWild(R, a, 0); a += 0x10; ++n; if (n % 4096 == 0) gNow += 1; }
    CHECK(gEnded == 70, "a walk with a call at each step ends the game with 70");
    CHECK(n == ps2::wild::kFlood, "at the flood's count, within one window");
    CHECK(Count("CRASH: the PS2 code reads and writes outside its memory without end") == 1, "its CRASH line");
    CHECK(Count("(the last 1 in a row with no call between them") == 1 && Count("last loop in 0040bac0") == 1, "which says the calls between them and the loop's function");
    CHECK(Count("CRASH: the loop's registers: r 100001 100002") == 1 && Count("the game ends here (70)") == 1, "the registers, and the line that says the game ends");
    // 4. the walk of the hung game: one byte every 16, no call between them. Fast, it is a flood first
    Fresh();
    Call(0x40ba50, 0x40bb7c);
    a = 0x10010000u; n = 0;
    while (!gEnded && n < 1000000) { ps2::OnceWild(W, a + 0xf, 0); a += 0x10; ++n; if (n % 16 == 0) gNow += 1; }
    CHECK(gEnded == 70 && n == ps2::wild::kFlood, "a fast walk with no call ends at the flood's count");
    CHECK(Count("in a row with no call between them, from 1001000f to 1008fff") == 1, "its CRASH line says they were in a row, from where to where");
    //    slow (a line an access held it, as on the server: 1000 a second), the 65536 in a row end it
    Fresh();
    Call(0x40ba50, 0x40bb7c);
    a = 0x10010000u; n = 0;
    while (!gEnded && n < 1000000) { ps2::OnceWild(W, a + 0xf, 0); a += 0x10; ++n; gNow += 1; }
    CHECK(gEnded == 70, "a slow walk ends the game with 70");
    CHECK(n == 65536, "after 65536 accesses in a row with no call");
    CHECK(Count("CRASH: the PS2 code walks outside its memory and never ends") == 1, "its CRASH line");
    CHECK(Count("from 1001000f to 1010ffff") == 1, "the walk's first and last address");
    CHECK(Count("last loop in 0040bac0") == 1 && Count("last call 0040ba50 from 0040bb74") == 1, "the loop's function and the last call");
    CHECK(Count("CRASH: the loop's registers: r 100001 100002") == 1, "the loop's registers");
    CHECK(Count("the game ends here (70)") == 1, "and the line that says the game ends");
    // 5. said once: the walk going on (a game with a picture does not end) adds no CRASH line
    const size_t before = gLines.size();
    for (int k = 0; k < 200000; ++k) { ps2::OnceWild(W, a + 0xf, 0); a += 0x10; }
    CHECK(Count("CRASH: the PS2 code") == 1 && gLines.size() <= before + 1, "a walk is said once");
    // 6. a loop that never ends (ps2rt.cpp BackEdgeCheck calls these at its report's count, then every 1024 checks)
    Fresh();
    const long long report = ps2::wild::kLoopChecks;
    ps2::gFrames = 7114;
    ps2::wild::LoopSeen(0x1228c0);
    gNow += 9000; ps2::wild::LoopStill(0x1228c0, 100 * report);
    CHECK(gEnded == 0, "the same loop 9 s after its report: not yet");
    gNow += 2000; ps2::wild::LoopStill(0x1228c0, report + 1024);
    CHECK(gEnded == 70, "11 s after, the same loop still, no frame of the game run: the game ends with 70");
    CHECK(Count("CRASH: the PS2 code is in a loop that never ends: the loop of 001228c0, 11 s more and no frame of the game run (frame 7114; 0 reads and 0 writes outside the memory before it)") == 1, "its CRASH line");
    CHECK(Count("CRASH: the loop's registers: r 100001 100002") == 1 && Count("the game ends here (70)") == 1, "the registers, and the line that says the game ends");
    ps2::wild::LoopStill(0x1228c0, 3 * report);
    CHECK(Count("CRASH: the PS2 code is in a loop") == 1, "said once");
    // 7. a long loop of a game whose frames run: the count starts again at each frame seen
    Fresh();
    ps2::wild::LoopSeen(0x1228c0);
    for (int k = 0; k < 10; ++k) { gNow += 6000; ps2::gFrames += 1; ps2::wild::LoopStill(0x1228c0, (10 + k) * report); }
    CHECK(gEnded == 0, "a frame run every 6 s for a minute: the game goes on");
    gNow += 6000; ps2::wild::LoopStill(0x1228c0, 30 * report);
    gNow += 6000; ps2::wild::LoopStill(0x1228c0, 31 * report);
    CHECK(gEnded == 70, "then none for 12 s: it ends");
    // 8. another function's loop seen at a check meanwhile (the other threads' and the interrupts' loops run beside a dead
    // main thread: the bench of 10 November) does not start the count again; the report of another function's loop does
    Fresh();
    ps2::wild::LoopSeen(0x1228c0); ps2::wild::LoopLeft();
    gNow += 6000; ps2::wild::LoopSeen(0x1228c0);
    CHECK(gEnded == 0, "the report reached again 6 s later: not yet");
    ps2::wild::LoopLeft(); gNow += 5000; ps2::wild::LoopSeen(0x1228c0);
    CHECK(gEnded == 70, "and again 11 s after the first, checks of other loops between, no frame: it ends");
    Fresh();
    ps2::wild::LoopSeen(0x1228c0);
    gNow += 8000; ps2::wild::LoopSeen(0x140000);
    gNow += 8000; ps2::wild::LoopStill(0x140000, 2 * report);
    CHECK(gEnded == 0, "another function's loop reported 8 s later: its own 10 s start there");
    gNow += 3000; ps2::wild::LoopStill(0x140000, 3 * report);
    CHECK(gEnded == 70, "11 s in that one: it ends");
    Fresh();
    ps2::gFrames = 0; ps2::wild::LoopSeen(0x1228c0);
    gNow += 20000; ps2::wild::LoopStill(0x1228c0, 5 * report);
    CHECK(gEnded == 0, "no frame counted by the platform: nothing");
    ps2::gFrames = 9; ps2::gHeadless = false; ps2::gWildEnd = nullptr;
    ps2::wild::LoopSeen(0x1228c0);
    gNow += 20000; ps2::wild::LoopStill(0x1228c0, 5 * report);
    CHECK(gLines.empty(), "a game with a picture: the report alone, no CRASH line");
    printf(gFail ? "wildguard_test: %d FAILED\n" : "wildguard_test: all passed\n", gFail);
    return gFail ? 1 : 0;
}
