// (v150-host beta 24, cqc) A virtual doll's CQC throw as its character's in the game (MGO1Loader/vdoll_cqc.h, docs/VIRTUAL_DOLL.md 40).
// Beta 23's doll sent a stun of 30000 (a knock out at once) 56 frames after its hold whatever its character; in the game the stun is
// the map script's for the holder's character and the rule (a soldier's 10000: measured, cqc_m1) and goes at its thrown motion's frame.
//   R. the rules alone (no map): the stun of the hold's throw by character and rule, its frame, the throws a full gauge takes;
//   then a doll on a map's collision (City Under Siege by default) at level 10 with MGO1_VDOLL_CQC_TEST=1 (no shot: its rush and
//   its grab only), an enemy standing still 0.9 m before it; each part a new life of the doll as one character in one rule, its
//   records to him read:
//   1. a GRU soldier (team deathmatch): the hold {0D}, then {04 doll, 0, 10000, kind 0} 33 frames after it (cqc_m1: a real client's, 30 to 36);
//   2. Raikov: the hold, 30000 at +50;
//   3. Ocelot: the hold, 10000 at +50;
//   4. Reiko (capture): the hold, 30000 at +174 (the thrown motion's frame);
//   5. Raikov in SNEAKING: the hold, 7500;
//   6. a KGB soldier (deathmatch): the hold, 10000 at +33;
//   7. a GRU soldier at an enemy crouched (his state word's bit 2): no hold record, no stun (cqc_m3: a real client's hold takes
//      nothing then).
// MGO1_VDOLL_CQC=0 (beta 23's): every part holds and sends 30000 at +56 (checked as such). On beta 23's own body (b16 3363fd4: no
// vdoll_cqc.h) R fails to build in and parts 1 to 6 FAIL (30000 at +56 each time).
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_cqc_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o /tmp/vdoll_cqc_test
//   /tmp/vdoll_cqc_test <map.geom>
// (the map's collision file comes from the owner's disc: python3 -I tools/stage_dat.py extract <STAGE_N.DAT> n003a <dir>; it is never
// put in the repository)
#include "vdoll_body.h"
#if __has_include("vdoll_cqc.h")
#include "vdoll_cqc.h"
#define CQC_RULES 1
#endif
#include "doll_geom.h"
#include "doll_nav.h"
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gFrame = 0;
static int gRuleNow = 1;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (getenv("VDOLL_TEST_LOG") || strstr(b, "mesh") || strstr(b, "grab") || strstr(b, "throws player") || strstr(b, "CQC")) printf("   [%lld] %s\n", gFrame, b);
}
static bool MapNow(std::string* page, int* extra) { *page = "n003a"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return gRuleNow; }
struct Sent { int slot, kind; std::vector<uint8_t> rec; long long at = 0; };
static std::vector<Sent> gSent;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gSent.push_back({slot, kind, rec, gFrame}); }
static bool Read(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) gGeom.insert(gGeom.end(), b, b + n);
    fclose(f);
    return !gGeom.empty();
}
static int gFails = 0;
static void Check(bool ok, const char* what) { printf("  %s: %s\n", ok ? "pass" : "FAIL", what); if (!ok) ++gFails; }

// what the doll sent to him in a part: its hold, its stun record (value, kind, frame), its fall record
struct Got { long long holdAt = -1, stunAt = -1, fallAt = -1; int stun = -1, kind = -1, fall = -1, holds = 0, stuns = 0; };
static Got Part(vdoll::body::Self& s, float* pos, const float* start, const float* enemy, uint16_t* facing, uint16_t face0, int cls, int rule, int team, uint32_t enemyState = 3) {
    gRuleNow = rule;
    gPlayers[1].cls = cls; gPlayers[1].team = team; gPlayers[2].team = team ^ 1;
    s.team = team;
    memcpy(pos, start, 3 * sizeof(float));
    *facing = face0;
    gSent.clear();
    gHave[2] = true;
    memcpy(gPlayers[2].pos, enemy, sizeof gPlayers[2].pos);
    gPlayers[2].alive = true; gPlayers[2].life = 200; gPlayers[2].state = enemyState; gPlayers[2].stunMax = 30000; gPlayers[2].stamina = 30000;
    s.life = gFrame;
    const long long from = gFrame;
    float at[3] = {start[0], start[1], start[2]};
    for (long long end = gFrame + 900; gFrame < end; gFrame += 2) {
        memcpy(gPlayers[1].pos, pos, 3 * sizeof(float));
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
        if (gFrame == from) at[1] = pos[1];
        (void)at;
    }
    Got g;
    for (const Sent& m : gSent) {
        if (m.slot != 2 || m.rec.empty()) continue;
        if (m.kind == 1 && m.rec[0] == 0x0D) { ++g.holds; if (g.holdAt < 0) g.holdAt = m.at - from; }
        if (m.kind == 0 && m.rec[0] == 0x04 && m.rec.size() >= 7 && g.stunAt < 0) { g.stunAt = m.at - from; g.stun = m.rec[4] | (m.rec[5] << 8); g.kind = m.rec[6]; }
        if (m.kind == 0 && m.rec[0] == 0x04) ++g.stuns;
        if (m.kind == 1 && m.rec[0] == 0x09 && m.rec.size() >= 3 && g.fallAt < 0) { g.fallAt = m.at - from; g.fall = m.rec[2]; }
    }
    printf("   class %d, rule %d: holds %d (first +%lld), stun records %d (first %d kind %#x at +%lld), fall %d at +%lld\n", cls, rule, g.holds, g.holdAt, g.stuns,
           g.stun, g.kind, g.stunAt, g.fall, g.fallAt);
    gHave[2] = false;
    vdoll::body::Down(0);
    for (int k = 0; k < 1700; k += 2) { vdoll::body::Frame(gFrame); gFrame += 2; }   // (out of sight; its grab's 25 s on a knocked player over)
    return g;
}

int main(int argc, char** argv) {
    // ---- R. the rules ----
#ifdef CQC_RULES
    {
        using namespace vcqc;
        Check(HoldStun(1, 1, 30000, false) == 10000 && HoldStun(0, 0, 30000, false) == 10000 && HoldStun(2, 3, 30000, false) == 10000 && HoldStun(3, 1, 30000, false) == 10000,
              "R: a soldier's and Sokolov's 10000 outside SNEAKING (measured: cqc_m1)");
        Check(HoldStun(5, 1, 30000, false) == 10000, "R: Ocelot's 10000 (tsneW_wake5: 30000 -> 20000)");
        Check(HoldStun(4, 1, 30000, false) == 30000 && HoldStun(6, 3, 30000, false) == 30000 && HoldStun(7, 6, 30000, false) == 30000, "R: Raikov's, Reiko's, Rowdy's 30000");
        Check(HoldStun(4, 4, 30000, false) == 7500 && HoldStun(1, 4, 30000, false) == 7500, "R: 7500 in SNEAKING");
        Check(HoldStun(8, 4, 30000, false) == 30000 && HoldStun(8, 4, 20000, false) == 20000, "R: Snake's: the victim's whole gauge");
        Check(HoldStun(5, 1, 30000, true) == 5000 && HoldStun(4, 1, 30000, true) == 15000, "R: halved for a teammate");
        Check(HoldStunFrame(1) == 33 && HoldStunFrame(4) == 50 && HoldStunFrame(5) == 50 && HoldStunFrame(8) == 50 && HoldStunFrame(6) == 174 && HoldStunFrame(7) == 174,
              "R: the thrown motions' frames (33, 50, 50, 50, 174, 174)");
        Check(HoldThrowRecord(0, 0) && !HoldThrowRecord(0, 5) && !HoldThrowRecord(0, 0xCF) && !HoldThrowRecord(75, 0), "R: the hold's throw is the stun record of kind 0, no life");
        int st = kGauge, n = 0; while (st > 0 && n < 20) { st = After(st, HoldStun(5, 1, kGauge, false)); ++n; }
        int st2 = kGauge, n2 = 0; while (st2 > 0 && n2 < 20) { st2 = After(st2, kThrowAmount); ++n2; }
        printf("   a full gauge: %d of a soldier's or Ocelot's throws, %d of Raikov's, %d throws without a hold (none recovered between)\n", n, KnocksOut(kGauge, HoldStun(4, 1, kGauge, false)) ? 1 : 0, n2);
        Check(n == 3 && n2 == 10 && KnocksOut(kGauge, 30000) && !KnocksOut(kGauge, 29999) && !KnocksOut(20000, 10000) && KnocksOut(10000, 10000), "R: a knock out only when the gauge is empty");
    }
#else
    Check(false, "R: this body has no vdoll_cqc.h (beta 23): the game's CQC rule is not there");
#endif

    if (argc < 2 || !Read(argv[1])) { fprintf(stderr, "usage: vdoll_cqc_test <map.geom>\n"); return gFails ? 1 : 2; }
    const bool beta23 = getenv("MGO1_VDOLL_CQC") && !strcmp(getenv("MGO1_VDOLL_CQC"), "0");
    if (beta23) printf("(MGO1_VDOLL_CQC=0: beta 23's behaviour expected)\n");
    setenv("MGO1_VDOLL_LEVEL", "10", 1);
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    setenv("MGO1_VDOLL_CQC_TEST", "1", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
    setenv("MGO1_VDOLL_DEBUG", "1", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    gHave[1] = true; gPlayers[1].team = 0; gPlayers[1].object = true; gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].state = 3;
    gPlayers[2].team = 1; gPlayers[2].object = true; gPlayers[2].alive = true; gPlayers[2].life = 200; gPlayers[2].state = 3;
    const float kHips = 800.f;
    float start[3], far[3];
    {
        doll::Geom g; doll::NavMesh nav;
        std::string err, log;
        doll::NavConfig cfg;
        if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err) || !nav.Build(g, cfg, log)) { printf("FAIL: the map not read\n"); return 1; }
        static uint32_t seed = 0x2345671u;
        unsigned (*rnd)() = []() -> unsigned { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
        bool found = false;
        for (int k = 0; k < 20000 && !found; ++k) {      // (open flat ground: 0.9 m between them, a clear line at every height)
            float p[3], q[3];
            if (!nav.RandomPoint(nullptr, 0.f, p, rnd) || !nav.RandomPoint(p, 2000.f, q, rnd)) continue;
            const float d = std::hypot(p[0] - q[0], p[2] - q[2]);
            if (d < 500.f || std::fabs(p[1] - q[1]) > 100.f) continue;
            const float ux = (q[0] - p[0]) / d, uz = (q[2] - p[2]) / d;
            const float e[3] = {p[0] + ux * 900.f, p[1], p[2] + uz * 900.f};
            bool ok = nav.Walkable(p, e);
            for (float h1 : {300.f, 1100.f, 1550.f}) { const float a[3] = {p[0], p[1] + h1, p[2]}, b[3] = {e[0], e[1] + h1, e[2]}; ok = ok && nav.Clear(a, b); }
            if (!ok) continue;
            memcpy(start, p, sizeof start); memcpy(far, e, sizeof far);
            found = true;
        }
        if (!found) { printf("FAIL: no open ground found\n"); return 1; }
    }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(gFrame); gFrame += 2; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("FAIL: the bodies' mesh was not built\n"); return 1; }
    gFrame = 12000;
    float pos[3];
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send;
    float hips[3] = {start[0], start[1] + kHips, start[2]};
    const float enemy[3] = {far[0], far[1] + kHips, far[2]};
    const uint16_t face0 = (uint16_t)lrintf(std::atan2(far[0] - start[0], far[2] - start[2]) / 6.2831853f * 65536.f);
    printf("the doll at %.0f %.0f %.0f, the enemy 0.9 m before it\n", hips[0], hips[1], hips[2]);
    struct Case { const char* what; int cls, rule, team; bool hold; int stun, kind; long long at; };   // (hold: every character holds)
    const Case cases[] = {
        {"1. a GRU soldier, team deathmatch: the hold, 10000 at +33", 1, 1, 0, true, 10000, 0, 33},
        {"2. Raikov, team deathmatch: the hold, 30000 at +50", 4, 1, 0, true, 30000, 0, 50},
        {"3. Ocelot, team deathmatch: the hold, 10000 at +50", 5, 1, 0, true, 10000, 0, 50},
        {"4. Reiko, capture: the hold, 30000 at +174", 6, 3, 0, true, 30000, 0, 174},
        {"5. Raikov in SNEAKING: the hold, 7500 at +50", 4, 4, 0, true, 7500, 0, 50},
        {"6. a KGB soldier, deathmatch: the hold, 10000 at +33", 0, 0, 0, true, 10000, 0, 33},
    };
    for (const Case& c : cases) {
        const Got g = Part(s, pos, hips, enemy, &facing, face0, c.cls, c.rule, c.team);
        if (g.stunAt < 0) { Check(false, (std::string(c.what) + ": no CQC from the doll").c_str()); continue; }
        if (beta23) {
            Check(g.holds == 1 && g.stun == 30000 && g.kind == 0 && g.stunAt - g.holdAt >= 54 && g.stunAt - g.holdAt <= 58,
                  (std::string(c.what) + " -> MGO1_VDOLL_CQC=0: the hold and 30000 at +56, as beta 23").c_str());
            continue;
        }
        const long long after = g.stunAt - g.holdAt;
        Check(g.holds == 1 && g.stun == c.stun && g.kind == c.kind && after >= c.at - 3 && after <= c.at + 3 && g.fallAt < 0, c.what);
    }
    {                                                   // (7: a crouched enemy, the state word's bit 2)
        const Got g = Part(s, pos, hips, enemy, &facing, face0, 1, 1, 0, 5);
        if (beta23) Check(g.holds == 1 && g.stun == 30000, "7. a crouched enemy -> MGO1_VDOLL_CQC=0: held and 30000, as beta 23");
        else Check(g.holds == 0 && g.stuns == 0, "7. a GRU soldier at a crouched enemy: no hold, no stun (its grab reaches nothing)");
    }
    printf("%s\n", gFails ? "FAILED" : "ALL PASSED");
    return gFails ? 1 : 0;
}
