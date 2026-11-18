// net_grenades_test.cpp: the grenades' sync of netfix.h (its grenade section, cut out by net_grenades_test.sh) on a fake memory and a
// fake clock: a copy's act told its thrower's grenade (made, exploded), the hold past its own fuse, malformed messages.
//   bash tests/net_grenades_test.sh    (builds and runs it natively; exit 0 when every case passes)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <map>
#include <deque>
#include <vector>
#include <mutex>
#include <algorithm>
typedef unsigned long long ULONGLONG;
static ULONGLONG gNow = 1000;
static ULONGLONG GetTickCount64() { return gNow; }
struct Ctx { uint64_t r[32] = {}; float f[32] = {}; };
namespace ps2 { typedef void (*Fn)(Ctx&); inline Fn FindFn(uint32_t) { return nullptr; } }
namespace w {
static std::vector<uint8_t> gMem(32u << 20);
static uint8_t R8(uint32_t a) { return gMem[a & 0x01ffffff]; }
static uint16_t R16(uint32_t a) { uint16_t v; memcpy(&v, &gMem[a & 0x01ffffff], 2); return v; }
static uint32_t R32(uint32_t a) { uint32_t v; memcpy(&v, &gMem[a & 0x01ffffff], 4); return v; }
static void W32(uint32_t a, uint32_t v) { memcpy(&gMem[a & 0x01ffffff], &v, 4); }
static int gLogs = 0;
static void Log(const char* f, ...) { va_list ap; va_start(ap, f); printf("    log: "); vprintf(f, ap); printf("\n"); va_end(ap); ++gLogs; }
static bool Ps2Like() { return false; }
static int gOwn = 2;
int OwnSlot() { return gOwn; }
static uint32_t gPageGen = 0;
static uint32_t gPageShift = 0;                          // a map page's functions this far from n003a's (0: n003a)
static uint32_t P3(uint32_t us) { return us + gPageShift; }
#include "nade_part.h"
}
using namespace w;
static int fails = 0;
static void Check(bool ok, const char* what) { printf("  %s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
static float F(uint32_t a) { return NetF32(a); }
// a copy of player 1's grenade (slot 0) on this PC (slot 2): its actor at g, its model at m
static const uint32_t g0 = 0x00800000, m0 = 0x00900000;
static void MakeCopy(uint32_t g, uint32_t m, float fuse) {
    W32(g + 0x204, m); W32(g + 0x2e8, 0); NetF32Set(g + 0x2ec, fuse); W32(g + 0x2e0, 0x200000);
    NetF32Set(m + 0x40, 100); NetF32Set(m + 0x44, 0); NetF32Set(m + 0x48, 200);
    Ctx c; c.r[4] = 0x00700000; c.r[5] = 0x13;              // the throw of the thrower's copy (its player at 0x700000)
    gMem[0x00700000 + 0xb60] = 0; W32(0x00700000 + 0xb5c, 1);
    NetBlast(c, 0x3FF828, 0);
    Ctx i; i.r[4] = g; i.r[9] = (uint64_t)(int64_t)-1;       // the init: owner -1 (a copy)
    NetBlast(i, 0x4F0BB0, 0);
}
// one act of the copy: the hook, then the game's own act as 0x4F08B8 does it (bit 2: explode; else the fuse less a step,
// at 0 or below bit 2 for the next act); true when it exploded, at
static bool Act(uint32_t g, float step, float at[3]) {
    Ctx c; c.r[4] = g;
    NetBlast(c, 0x4F08B8, 0);
    if (R32(g + 0x2e8) & 4) {
        const uint32_t m = R32(g + 0x204);
        at[0] = F(m + 0x40); at[1] = F(m + 0x44); at[2] = F(m + 0x48);
        Ctx e; e.r[4] = g; NetBlast(e, 0x4F0448, 0);
        return true;
    }
    NetF32Set(g + 0x2ec, F(g + 0x2ec) - step);
    if (F(g + 0x2ec) <= 0) W32(g + 0x2e8, R32(g + 0x2e8) | 4);
    return false;
}
static std::vector<uint8_t> Msg(uint8_t kind, uint16_t seq, float x, float y, float z) {
    std::vector<uint8_t> b = {kind, (uint8_t)seq, (uint8_t)(seq >> 8)};
    const float p[3] = {x, y, z}; const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + 12); return b;
}
static void Reset() { gNetNades.clear(); gNetNadesTold.clear(); gNetNadesOut.clear(); gNetNadeStats = NetNadeStats{}; gNetThrow = NetThrow{}; }
int main() {
    const float step = 1.0f / 30, at0[3] = {0, 0, 0};
    float at[3];
    printf("case 1: told 'made', the copy's fuse ends first, held until the thrower's explosion is told 90 ms later\n");
    Reset(); gNow = 1000; MakeCopy(g0, m0, 3.0f);
    { auto b = Msg(0, 7, 0, 0, 0); gNow = 1060; Check(NetNadeHeard(0, b.data(), b.size()), "the making told"); }
    bool boom = false; int acts = 0; ULONGLONG when = 0;
    for (gNow = 1000; gNow < 6000 && !boom; gNow += 33, ++acts) {
        if (gNow >= 1000 + 3000 + 90 && gNetNadesTold.size() == 1) { auto b = Msg(1, 7, 500, 10, 900); Check(NetNadeHeard(0, b.data(), b.size()), "the explosion told"); }
        if ((boom = Act(g0, step, at))) when = gNow;
    }
    Check(boom && fabsf(at[0] - 500) < 1e-3f && fabsf(at[1] - 10) < 1e-3f && fabsf(at[2] - 900) < 1e-3f, "exploded at the thrower's place");
    Check(when >= 4090 && when <= 4090 + 40, "exploded at the act after the told explosion");
    Check(gNetNadeStats.held == 1 && gNetNadeStats.alone == 0, "held past its fuse, not alone");
    (void)at0;

    printf("case 2: the thrower's explosion told before the copy's fuse ends: exploded there at once\n");
    Reset(); gNow = 1000; MakeCopy(g0, m0, 3.0f);
    { auto b = Msg(0, 8, 0, 0, 0); NetNadeHeard(0, b.data(), b.size()); }
    boom = false;
    for (gNow = 1000; gNow < 6000 && !boom; gNow += 33) {
        if (gNow >= 2500 && gNetNadesTold.size() == 1) { auto b = Msg(1, 8, -300, 0, 40); NetNadeHeard(0, b.data(), b.size()); }
        if ((boom = Act(g0, step, at))) when = gNow;
    }
    Check(boom && fabsf(at[0] + 300) < 1e-3f && fabsf(at[2] - 40) < 1e-3f && when <= 2500 + 40, "exploded at the told place, at the next act");
    Check(gNetNadeStats.moved == 1 && gNetNadeStats.held == 0, "moved, not held");

    printf("case 3: the explosion never told: held %d acts, then exploded by itself at its own place\n", kNadeHoldActs);
    Reset(); gNow = 1000; MakeCopy(g0, m0, 3.0f);
    { auto b = Msg(0, 9, 0, 0, 0); NetNadeHeard(0, b.data(), b.size()); }
    boom = false; acts = 0;
    for (gNow = 1000; gNow < 9000 && !boom; gNow += 33, ++acts) if ((boom = Act(g0, step, at))) when = gNow;
    Check(boom && fabsf(at[0] - 100) < 1e-3f && fabsf(at[2] - 200) < 1e-3f, "its own place");
    Check(acts >= 90 + kNadeHoldActs - 1 && acts <= 90 + kNadeHoldActs + 2, "its own fuse's acts and the hold's");
    Check(gNetNadeStats.alone == 1, "counted alone");

    printf("case 4: a PS2 thrower (nothing told): the game's own fuse, no hold\n");
    Reset(); gNow = 1000; MakeCopy(g0, m0, 3.0f);
    boom = false;
    for (gNow = 1000; gNow < 8000 && !boom; gNow += 33) if ((boom = Act(g0, step, at))) when = gNow;
    Check(boom && when >= 3990 && when <= 4100 && gNetNadeStats.held == 0 && gNetNadeStats.alone == 0, "exploded at its fuse's end, as the game does");

    printf("case 5: two grenades 1.2 s apart: each copy explodes where its own grenade did\n");
    Reset(); gNow = 1000; MakeCopy(g0, m0, 3.0f);
    { auto b = Msg(0, 11, 0, 0, 0); gNow = 1050; NetNadeHeard(0, b.data(), b.size()); }
    gNow = 2200; MakeCopy(g0 + 0x1000, m0 + 0x1000, 3.0f);
    { auto b = Msg(0, 12, 0, 0, 0); gNow = 2250; NetNadeHeard(0, b.data(), b.size()); }
    bool b1 = false, b2 = false; float a1[3] = {}, a2[3] = {};
    for (gNow = 2200; gNow < 8000 && !(b1 && b2); gNow += 33) {
        if (gNow >= 4050 && gNow < 4083) { auto b = Msg(1, 11, 11, 0, 11); NetNadeHeard(0, b.data(), b.size()); }
        if (gNow >= 5250 && gNow < 5283) { auto b = Msg(1, 12, 22, 0, 22); NetNadeHeard(0, b.data(), b.size()); }
        if (!b1) b1 = Act(g0, step, a1);
        if (!b2) b2 = Act(g0 + 0x1000, step, a2);
    }
    Check(b1 && b2 && a1[0] == 11 && a2[0] == 22, "the first at 11, the second at 22");

    printf("case 6: malformed or foreign messages are refused\n");
    Reset(); gNow = 1000;
    { auto b = Msg(1, 1, NAN, 0, 0); Check(!NetNadeHeard(0, b.data(), b.size()), "a place not a number"); }
    { auto b = Msg(1, 1, 3e38f, 0, 0); Check(!NetNadeHeard(0, b.data(), b.size()), "a place out of any map"); }
    { auto b = Msg(2, 1, 0, 0, 0); Check(!NetNadeHeard(0, b.data(), b.size()), "an unknown kind"); }
    { auto b = Msg(1, 1, 0, 0, 0); Check(!NetNadeHeard(2, b.data(), b.size()), "this PC's own slot"); }
    { auto b = Msg(1, 1, 0, 0, 0); Check(!NetNadeHeard(40, b.data(), b.size()) && !NetNadeHeard(-1, b.data(), b.size()), "a slot out of range"); }
    { auto b = Msg(1, 1, 0, 0, 0); Check(!NetNadeHeard(0, b.data(), 14), "a short message"); }
    { auto b = Msg(1, 1, 0, 0, 0); Check(NetNadeHeard(0, b.data(), b.size()) && !NetNadeHeard(0, b.data(), b.size()), "a copy of a message heard once"); }
    printf("case 8: this screen's game ahead of the thrower's (a slowed thrower, a test instance's pace): its fuse ends 45 acts\n"
           "        (1.5 s) before the thrower's explosion is told: held, then exploded at the told place\n");
    Reset(); gNow = 1000; MakeCopy(g0, m0, 3.0f);
    { auto b = Msg(0, 31, 0, 0, 0); NetNadeHeard(0, b.data(), b.size()); }
    boom = false;
    for (gNow = 1000; gNow < 9000 && !boom; gNow += 33) {
        if (gNow >= 4000 + 1485 && gNetNadesTold.size() == 1) { auto b = Msg(1, 31, 70, 5, -70); NetNadeHeard(0, b.data(), b.size()); }
        if ((boom = Act(g0, step, at))) when = gNow;
    }
    Check(boom && fabsf(at[0] - 70) < 1e-3f && fabsf(at[2] + 70) < 1e-3f && when >= 5485 && when <= 5485 + 40, "at the told place, at the act after it was told");
    Check(gNetNadeStats.held == 1 && gNetNadeStats.alone == 0, "held, not alone");

    printf("case 9: the making told 5 s before the copy is made (the throw's motion slower here): taken; one 25 s away: not\n");
    Reset(); gNow = 1000;
    { auto b = Msg(0, 41, 0, 0, 0); NetNadeHeard(0, b.data(), b.size()); }
    gNow = 6000; MakeCopy(g0, m0, 3.0f);
    boom = false;
    for (gNow = 6000; gNow < 12000 && !boom; gNow += 33) {
        if (gNow >= 7000 && gNetNadesTold.size() == 1) { auto b = Msg(1, 41, -5, 0, 5); NetNadeHeard(0, b.data(), b.size()); }
        if ((boom = Act(g0, step, at))) when = gNow;
    }
    Check(boom && fabsf(at[0] + 5) < 1e-3f && when <= 7000 + 40, "matched: exploded at the told place");
    Reset(); gNow = 1000;
    { auto b = Msg(0, 42, 0, 0, 0); NetNadeHeard(0, b.data(), b.size()); }
    gNow = 26000; MakeCopy(g0, m0, 3.0f);
    boom = false;
    for (gNow = 26000; gNow < 33000 && !boom; gNow += 33) if ((boom = Act(g0, step, at))) when = gNow;
    Check(boom && fabsf(at[0] - 100) < 1e-3f && when <= 26000 + 3000 + 100 && gNetNadeStats.held == 0, "not matched: the game's own fuse");

    printf("case 7: another map page: its own addresses of the hooked functions found, the others not hooked\n");
    gPageShift = 0; ++gPageGen;
    Check(NetBlastPaged(0x4F08B8) == 0 && NetBlastPaged(0x3FF828) == 0, "n003a itself: none (its calls are n003a's)");
    gPageShift = 0x8758; ++gPageGen;                          // n001a: the act at 0x4F9010
    Check(NetBlastPaged(0x4F9010) == 0x4F08B8 && NetBlastPaged(0x3FF828 + 0x8758) == 0x3FF828 && NetBlastPaged(0x4367A0 + 0x8758) == 0x4367A0, "n001a's act, throw and record 04 at their n003a addresses");
    Check(NetBlastPaged(0x4F08B8) == 0 && NetBlastPaged(0x4F9014) == 0 && NetBlastPaged(0x100000) == 0, "other addresses: none");
    gPageShift = 0; ++gPageGen;
    Check(NetBlastPaged(0x4F9010) == 0, "the page changed: its table made again");
    printf(fails ? "net_grenades_test: %d FAILED\n" : "net_grenades_test: all passed\n", fails);
    return fails ? 1 : 0;
}
