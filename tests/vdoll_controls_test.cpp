// The virtual dolls "as a player" off line (MGO1Loader/vdoll_body.cpp FightPlayer, docs/VIRTUAL_DOLL.md 17): a map's .geom,
// one doll (AK-47) at the map's spawn points, an enemy standing at 5 to 40 m and 0 to 150 degrees off its facing, a duel
// of 360 frames each, run a frame at a time. With MGO1_VDOLL_CONTROLS=player every frame is checked against the pad's
// limits: no turn of its aim faster than the readied turn (16 degrees a frame) or the first person view's (2, and its
// recoil), nothing turned while the view comes up, no bullet but 3 frames or more after a pull and only readied or in the
// view, no head hit but from the view entered (16 frames at least after R1), the third person's hits on the body (60, a
// leg 30). Without it the same duels give today's numbers (and fail those checks: the "before"). MGO1_VDOLL_LEVEL as on
// the host (one level); MGO1_VDOLL_SEED for the same draws.
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_controls_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_controls_test
//   MGO1_VDOLL_CONTROLS=player MGO1_VDOLL_LEVEL=10 ./vdoll_controls_test <map.geom>
#include "doll_geom.h"
#include "vdoll_body.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static bool gQuiet = true;
static int gSees = 0;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, ": sees player")) ++gSees;
    if (!gQuiet || strstr(b, "the bodies") || strstr(b, "as a player")) printf("%s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 0; }                         // (deathmatch: everyone an enemy)

struct Hit { long long frame; int dmg; bool head; };
static std::vector<Hit> gHits;
static long long gNow = 0;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) {
    if (slot == 2 && kind == 0 && rec.size() >= 9 && rec[0] == 0x04) gHits.push_back({gNow, rec[2] | rec[3] << 8, rec[8] != 0});
}

static int gFails = 0;
static void Fail(const char* fmt, ...) {
    if (gFails++ >= 20) return;
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}
static float Deg(float r) { return r * 180.f / 3.14159265f; }
static float Wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_controls_test <map.geom>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 1;
    fclose(f);
    doll::Geom g;
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    std::vector<const doll::Spawn*> spawns;
    for (const doll::Spawn& sp : g.spawns) if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) spawns.push_back(&sp);
    if (spawns.empty()) { printf("no spawn point in the geom\n"); return 1; }
    gQuiet = !getenv("VDOLL_TEST_VERBOSE");
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 0);                 // (another weapon: MGO1_VDOLL_WEAPONS=svd ...)
    const bool ak = !strcmp(getenv("MGO1_VDOLL_WEAPONS"), "ak47");
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "12345", 1);
    // (VDOLL_TEST_CHECK=1: the checks without the switch too: today's code fails them, the "before")
    const bool player = (getenv("MGO1_VDOLL_CONTROLS") && !strcmp(getenv("MGO1_VDOLL_CONTROLS"), "player")) || getenv("VDOLL_TEST_CHECK");
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    printf("controls %s (%s)\n", vdoll::body::Controls(), player ? "the pad's limits checked" : "the limits only measured");
    const float dists[] = {5000.f, 8000.f, 15000.f, 25000.f, 40000.f};
    const float offs[] = {0.f, 30.f, 70.f, 150.f};
    long long fr = 600;
    int duels = 0, fights = 0, headsTps = 0, headsEarly = 0, fastTurns = 0, viewTurns = 0, entryTurns = 0, earlyBullets = 0, noPull = 0, badState = 0;
    int tpsHits = 0, viewHits = 0, viewHeads = 0, bodyDmgBad = 0;
    float maxTps = 0.f, maxView = 0.f;
    std::vector<long long> firstHit, firstKill;     // frames from its sight to the first hit, to 200 of damage
    for (size_t si = 0; si < spawns.size() && si < 8; ++si)
        for (float dist : dists)
            for (float off : offs) {
                ++duels;
                float pos[3] = {spawns[si]->p[0], spawns[si]->p[1] + 800.f, spawns[si]->p[2]};
                const float yaw = (float)(si * 37 % 360) * 3.14159265f / 180.f;
                uint16_t facing = (uint16_t)lrintf(yaw / 6.2831853f * 65536.f);
                const float b = yaw + off * 3.14159265f / 180.f;
                gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
                gHave[2] = false;
                vdoll::body::Self s;
                s.k = 0; s.slot = 1; s.team = 0; s.life = fr; s.pos = pos; s.facing = &facing; s.send = Send;
                gHits.clear(); gSees = 0;
                // a few frames alone (its new life, still), then the enemy
                for (long long t = fr; t < fr + 30; ++t) { gNow = t; memcpy(gPlayers[1].pos, pos, sizeof pos); vdoll::body::Frame(t); s.tick = t % 6 == 0; vdoll::body::Alive(s, t); }
                fr += 30;
                gHave[2] = true; gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1; gPlayers[2].object = gPlayers[2].alive = true; gPlayers[2].life = 200;
                gPlayers[2].pos[0] = pos[0] + std::sin(b) * dist; gPlayers[2].pos[1] = pos[1]; gPlayers[2].pos[2] = pos[2] + std::cos(b) * dist;
                vdoll::body::AimInfo was, now;
                vdoll::body::Aim(0, &was);
                long long sight = -1, enterAt = -1, lastPull = -1;
                int bulletState = 0; long long bulletEnter = -1, bulletAt = -1;   // (beta 19: a hit's records may follow its bullet to the next record tick: its bullet's state)
                int dmg = 0;
                bool killed = false;
                for (long long t = fr; t < fr + 360; ++t) {
                    gNow = t;
                    memcpy(gPlayers[1].pos, pos, sizeof pos);
                    const size_t hitsBefore = gHits.size();
                    const int seesBefore = gSees;
                    vdoll::body::Frame(t);
                    s.tick = t % 6 == 0;
                    vdoll::body::Alive(s, t);
                    vdoll::body::Aim(0, &now);
                    if (gSees > seesBefore && sight < 0) { sight = t; ++fights; }
                    // its aim's turn this frame
                    if (was.aiming && now.aiming && now.target == was.target) {
                        const float dy = std::fabs(Deg(Wrap(now.yaw - was.yaw)));
                        if (player && was.state == 4 && now.state == 4) { maxView = std::max(maxView, dy); if (dy > 4.f + 0.31f + 0.01f || (t & 1 && dy > 0.31f)) ++viewTurns; }
                        else if (player && now.state == 3 && was.state == 3) { if (dy > 0.001f) ++entryTurns; }
                        else { maxTps = std::max(maxTps, dy); if (dy > 16.f + 0.01f) ++fastTurns; }
                    }
                    if (now.state == 3 && was.state != 3) enterAt = t;
                    if (was.pullAt >= 0) lastPull = was.pullAt;
                    if (now.pullAt >= 0) lastPull = now.pullAt;
                    if (now.shots > was.shots && player) {
                        const long long pull = was.pullAt >= 0 ? was.pullAt : now.pullAt >= 0 ? now.pullAt : lastPull;
                        if (pull < 0) ++noPull;
                        else if (t - pull < 3) ++earlyBullets;
                        if (was.state != 2 && was.state != 4) ++badState;
                    }
                    if (now.shots > was.shots) { bulletState = was.state; bulletEnter = enterAt; bulletAt = t; }
                    for (size_t i = hitsBefore; i < gHits.size(); ++i) {
                        const Hit& hh = gHits[i];
                        if (!dmg && sight >= 0) firstHit.push_back(t - sight);
                        dmg += hh.dmg;
                        if (!killed && dmg >= 200 && sight >= 0) { killed = true; firstKill.push_back(t - sight); }
                        if (!hh.dmg) continue;                      // (beta 21: a grab's throw, a stun: no bullet's record)
                        const int hs = bulletAt >= 0 ? bulletState : now.state;
                        const long long he = bulletAt >= 0 ? bulletEnter : enterAt, ht = bulletAt >= 0 ? bulletAt : t;
                        if (hs == 4) { ++viewHits; if (hh.head) ++viewHeads; }
                        else {
                            ++tpsHits;
                            if (hh.head) ++headsTps;
                            if (player && ak && hh.dmg != 60 && hh.dmg != 30) ++bodyDmgBad;
                        }
                        if (player && hh.head && (hs != 4 || he < 0 || ht - he < 16)) ++headsEarly;
                    }
                    was = now;
                }
                fr += 360;
                vdoll::body::Down(0);
                fr += 10;
            }
    auto Median = [](std::vector<long long> v) { if (v.empty()) return -1LL; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    printf("%d duels, %d fights; frames from its sight to its first hit: median %lld (%zu hits), to 200 of damage: median %lld (%zu)\n",
           duels, fights, Median(firstHit), firstHit.size(), Median(firstKill), firstKill.size());
    printf("hits: %d not in the view (heads among them %d), %d in the view (%d heads); the fastest turn %.1f degrees a frame not in the view, %.2f in it\n",
           tpsHits, headsTps, viewHits, viewHeads, maxTps, maxView);
    printf("%s\n", vdoll::body::Status(0).c_str());
    if (player) {
        if (fastTurns) Fail("%d frames turned faster than the readied turn's 16 degrees", fastTurns);
        if (viewTurns) Fail("%d frames turned faster than the view's 4 degrees a 2-frame tick, or between its ticks (its recoil aside)", viewTurns);
        if (entryTurns) Fail("%d frames turned while the view came up", entryTurns);
        if (earlyBullets) Fail("%d bullets within 3 frames of their pull", earlyBullets);
        if (noPull) Fail("%d bullets with no pull", noPull);
        if (badState) Fail("%d bullets neither readied nor in the view", badState);
        if (headsTps || headsEarly) Fail("head hits from the third person %d, before the view's entry %d", headsTps, headsEarly);
        if (bodyDmgBad) Fail("%d third person hits not a body's or a leg's damage", bodyDmgBad);
        if (fights < 10) Fail("only %d fights in %d duels", fights, duels);
        if (tpsHits + viewHits == 0) Fail("no hit at all");
        printf("%s: %d checks of the pad's limits\n", gFails ? "FAIL" : "PASS", 10);
    }
    return gFails ? 1 : 0;
}
