// The collision guards' plain parts (MGO1Loader/collguard.h, docs/RECOMPILED_WORLD.md section 2):
//   g++ -O1 -std=c++17 -I MGO1Loader tests/collision_guard_test.cpp -o collision_guard_test && ./collision_guard_test
// - the ray walk's vectors: the fourth word past 10^12 (a bullet's mark line, -3.4e38 left from the stack) is walked,
//   x, y or z past 10^12 (the 27 September hang, 1.5e35) refused;
// - the grid's box queries' top cell: Graniny Gorki Lab's grid (y from -6000 m, 2 cells of 10 m) asked at 16.9 m (the
//   capture item's "Drop_Item" test on the upper floor): the PC's rounding gives cell 3 (past the grid), the PS2's 2
#include "collguard.h"
#include <cstdio>

static int gFail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++gFail; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main() {
    // seen on the host (capture, Ghost Factory, f6532): a2 and a3 of the bullet's mark line
    const float markA[4] = {-3305.07f, 666.297f, -27504.f, -3.40282e38f}, markB[4] = {-3305.07f, 666.297f, -27499.f, -3.40282e38f};
    CHECK(RayWordsBad(markA, markB) == 2, "a bullet's mark line (the fourth word -3.4e38) is walked");
    const float undefA[4] = {1.5e35f, 800.f, -2000.f, 1.f}, sane[4] = {0.f, 800.f, -2000.f, 1.f};
    CHECK(RayWordsBad(undefA, sane) == 1, "an aim left undefined (x 1.5e35) is refused");
    CHECK(RayWordsBad(sane, undefA) == 1, "the same as its end");
    const float yBad[4] = {0.f, -2e13f, 0.f, -3.40282e38f};
    CHECK(RayWordsBad(sane, yBad) == 1, "y past 10^12 refused, whatever the fourth word");
    const float maxF[4] = {3.4028235e38f, 0.f, 0.f, 0.f};
    CHECK(RayWordsBad(maxF, sane) == 1, "a saturated x (the largest float) refused, as before");
    CHECK(RayWordsBad(sane, sane) == 0, "a sane ray");
    CHECK(RayWordsBad(nullptr, sane) == 0, "a vector outside the memory is not read");

    // Graniny Gorki Lab's big grid 012F1280: origin y -6000, cells 10000 high, 2 of them; the query at y 16895.3
    volatile float vhi = (16895.3f + 6000.f) / 10000.f, vsize = 2.f, vx = 2.527f, vxs = 5.f;   // (not folded at compile time)
    const float hi = vhi, size = vsize;
    _MM_SET_ROUNDING_MODE(_MM_ROUND_NEAREST);
    const int pc = GridTopEnd(hi, size);
    int ps2 = -1;
    WithPs2Rounding([&] { ps2 = GridTopEnd(hi, size); });
    CHECK(pc == 3, "the PC's rounding: the top cell's end %d, one past the grid of 2 (before the fix)", pc);
    CHECK(ps2 == 2, "the PS2's rounding: the top cell's end %d, the grid's size (no cell read past it)", ps2);
    CHECK(_MM_GET_ROUNDING_MODE() == _MM_ROUND_NEAREST, "the caller's rounding back after the query");
    // inside the grid both agree (x: 2.53 cells of 5 -> cells 0..2; an exact top 3.0 is one cell less on the PS2)
    const float x = vx, xs = vxs;
    int inPc = GridTopEnd(x, xs), inPs2 = -1;
    WithPs2Rounding([&] { inPs2 = GridTopEnd(x, xs); });
    CHECK(inPc == 3 && inPs2 == 3, "a box inside the grid: the same end %d / %d", inPc, inPs2);
    printf("%s (%d failed)\n", gFail ? "FAILED" : "PASSED", gFail);
    return gFail ? 1 : 0;
}
