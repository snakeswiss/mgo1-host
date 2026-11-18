// (v150-host beta 19) The virtual dolls' scoped rifles off line (MGO1Loader/vdoll_body.cpp, docs/VIRTUAL_DOLL.md 20): a map's
// .geom, one doll with the SVD at the map's spawn points, running on its way when an enemy comes into its sight at 5 to 40 m
// (and 0 to 150 degrees off), the enemy out of sight a second in the middle of the duel and back 6 m aside, 420 frames a
// duel, run a frame at a time, every record checked against what a player can do with a scoped rifle (measured on
// playable dolls' own games, docs/VIRTUAL_DOLL.md 20):
//   - no shot but still: the doll's place unchanged the 28 frames before each shot (the rifle to the hands 6 frames, its
//     scope up 22 frames after), so never a shot while it runs, and its first shot of a raise 28 frames after it stopped;
//   - the rifle named in its weapon record only while it stands (moving, another weapon in its hands);
//   - while the scope comes up its {12} record short (no view), its {08} the draw (1); a shot's {08 05} only with the scope
//     up (the long {12} sent before it);
//   - each hit's records one record tick after the record of the shot that made it (the host relays a datagram's events
//     before the players' states: the shot seen and heard before the blood);
//   - two shots 24 frames apart at least (the table's SVD);
//   - its copy (in the test: one that follows it at 50 mm a frame at most) caught up before each shot.
// MGO1_VDOLL_SCOPE=0 (as beta 18) fails them: the "before". MGO1_VDOLL_LEVEL, MGO1_VDOLL_CONTROLS as on the host.
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_scope_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_scope_test
//   MGO1_VDOLL_LEVEL=10 ./vdoll_scope_test <map.geom>
//   VDOLL_TEST_PAUSE=1 MGO1_VDOLL_LEVEL=10 ./vdoll_scope_test <map.geom>   (v150-host beta 19, review19) each duel's first shot
//     followed at once by 4 s knocked down (body::Pause each tick, then Resume), the enemy gone when it is up: no hit and no
//     shot record of that shot sent after it is up (a hit held for the record after its shot's went out seconds later)
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
static int gSees = 0;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, ": sees player")) ++gSees;
    if (getenv("VDOLL_TEST_VERBOSE") || strstr(b, "the bodies on")) printf("%s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 0; }                         // (deathmatch: everyone an enemy)

struct Rec { long long frame; int slot, kind; std::vector<uint8_t> r; };
static std::vector<Rec> gRecs;
static long long gNow = 0;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gRecs.push_back({gNow, slot, kind, rec}); }

static int gFails = 0;
static void Fail(const char* fmt, ...) {
    gFails++;
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_scope_test <map.geom>\n"); return 2; }
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
    setenv("MGO1_VDOLL_WEAPONS", "svd", 1);
    // (the owner, 13:42) every first person only weapon: VDOLL_TEST_FPV=mosin runs the same duels with the MOSIN NAGANT (the
    // body's test switch MGO1_VDOLL_FPV_TEST); the list of first person only codes checked first (SVD, MOSIN NAGANT, RPG-7)
    const bool mosin = getenv("VDOLL_TEST_FPV") && !strcmp(getenv("VDOLL_TEST_FPV"), "mosin");
    if (mosin) setenv("MGO1_VDOLL_FPV_TEST", "mosin", 1);
    const int code = mosin ? 0x10 : 0x0F, pace = mosin ? 180 : 24;
    for (int c : {0x0F, 0x10, 0x11}) if (!vdoll::body::FpvOnlyWeapon(c)) Fail("weapon code %02x not held as a first person only weapon", c);
    for (int c : {0x05, 0x06, 0x0A, 0x0B, 0x0C, 0x0E}) if (vdoll::body::FpvOnlyWeapon(c)) Fail("weapon code %02x held as a first person only weapon", c);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    const char* lv = getenv("MGO1_VDOLL_LEVEL");
    printf("%s, level %s, controls %s, MGO1_VDOLL_SCOPE=%s\n", mosin ? "MOSIN NAGANT" : "SVD", lv ? lv : "5", vdoll::body::Controls(), getenv("MGO1_VDOLL_SCOPE") ? getenv("MGO1_VDOLL_SCOPE") : "(on)");
    const float dists[] = {5000.f, 12000.f, 25000.f, 40000.f};
    const float offs[] = {0.f, 70.f, 150.f};
    long long fr = 600;
    int duels = 0, fights = 0, shots = 0, movingShots = 0, earlyShots = 0, rifleMoving = 0, shortAfter = 0, shotNoScope = 0, hitsFirst = 0, fastPace = 0, hitsN = 0;
    int raiseRecs = 0, copyFar = 0;
    const bool pauseTest = getenv("VDOLL_TEST_PAUSE") != nullptr;   // (review19)
    int pausedDuels = 0, staleHits = 0, staleShots = 0;
    std::vector<long long> toShot, toHit, stillBefore;
    for (size_t si = 0; si < spawns.size() && si < 8; ++si)
        for (float dist : dists)
            for (float off : offs) {
                ++duels;
                float pos[3] = {spawns[si]->p[0], spawns[si]->p[1] + 800.f, spawns[si]->p[2]};
                const float yaw = (float)(si * 37 % 360) * 3.14159265f / 180.f;
                uint16_t facing = (uint16_t)lrintf(yaw / 6.2831853f * 65536.f);
                gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
                gHave[2] = false;
                vdoll::body::Self s;
                s.k = 0; s.slot = 1; s.team = 0; s.life = fr; s.pos = pos; s.facing = &facing; s.send = Send;
                gRecs.clear(); gSees = 0;
                long long movedAt = fr, sight = -1, lastShot = -100000, longView = -1;
                float copy[3] = {pos[0], pos[1], pos[2]};
                int inHands = -1;
                long long shotRecAt = -1;   // (its copy in the host's game: it follows at 50 mm a frame at most, as on stairs)
                bool scopeShortSince = false;
                int dmgSeen = 0;
                vdoll::body::AimInfo was, now;
                vdoll::body::Aim(0, &was);
                long long downFrom = -1, upAt = -1;            // (VDOLL_TEST_PAUSE: knocked down after its first shot)
                for (long long t = fr; t < fr + 540; ++t) {
                    gNow = t;
                    const long long rel = t - fr;
                    // the enemy: none for 120 frames (the doll on its way), then ahead of where it runs; out of sight a second
                    // at 300, back 6 m aside
                    if (rel == 120 || rel == 360) {
                        const float fy = facing / 65536.f * 6.2831853f + off * 3.14159265f / 180.f + (rel == 360 ? 0.4f : 0.f);
                        gHave[2] = true; gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1; gPlayers[2].object = gPlayers[2].alive = true; gPlayers[2].life = 200;
                        gPlayers[2].pos[0] = pos[0] + std::sin(fy) * dist; gPlayers[2].pos[1] = pos[1]; gPlayers[2].pos[2] = pos[2] + std::cos(fy) * dist;
                    }
                    if (rel == 300) gHave[2] = false;
                    if (upAt >= 0 && t >= upAt) gHave[2] = false;   // (up again: no enemy in sight)
                    {
                        const float dx = pos[0] - copy[0], dy = pos[1] - copy[1], dz = pos[2] - copy[2], d = std::sqrt(dx * dx + dy * dy + dz * dz);
                        const float k = d > 50.f ? 50.f / d : 1.f;
                        copy[0] += dx * k; copy[1] += dy * k; copy[2] += dz * k;
                        if (t % 6 == 0) memcpy(gPlayers[1].pos, copy, sizeof copy);   // (read every 100 ms)
                    }
                    const float p0[3] = {pos[0], pos[1], pos[2]};
                    const size_t r0 = gRecs.size();
                    const int seesBefore = gSees;
                    vdoll::body::Frame(t);
                    s.tick = t % 6 == 0;
                    if (downFrom >= 0 && upAt < 0 && t < downFrom + 240) { if (s.tick) vdoll::body::Pause(0); }
                    else {
                        if (downFrom >= 0 && upAt < 0) { upAt = t; vdoll::body::Resume(0); gHave[2] = false; }
                        vdoll::body::Alive(s, t);
                    }
                    vdoll::body::Aim(0, &now);
                    if (pauseTest && downFrom < 0 && now.shots > was.shots) { downFrom = t + 1; ++pausedDuels; }
                    if (std::fabs(pos[0] - p0[0]) + std::fabs(pos[1] - p0[1]) + std::fabs(pos[2] - p0[2]) > 0.5f) movedAt = t;
                    if (gSees > seesBefore && sight < 0) { sight = t; ++fights; }
                    for (size_t i = r0; i < gRecs.size(); ++i)   // (the weapon in its hands: its last weapon record; the handgun's shots not counted)
                        if (gRecs[i].slot == 1 && gRecs[i].r.size() >= 3 && gRecs[i].r[0] == 0x13) inHands = gRecs[i].r[2];
                    if (now.shots > was.shots && inHands != code) { lastShot = -100000; }
                    else if (now.shots > was.shots) {
                        ++shots;
                        stillBefore.push_back(t - movedAt);
                        if (movedAt >= t - 1) ++movingShots;
                        else if (t - movedAt < (mosin ? 24 : 28)) ++earlyShots;
                        if (t - lastShot < pace) ++fastPace;
                        if (std::hypot(copy[0] - pos[0], copy[2] - pos[2]) > 600.f) ++copyFar;
                        if (sight >= 0 && toShot.size() < (size_t)fights) toShot.push_back(t - sight);
                        lastShot = t;
                    }
                    // its records of this frame
                    bool shotRec = false;
                    for (size_t i = r0; i < gRecs.size(); ++i) {
                        const Rec& r = gRecs[i];
                        if (r.slot == 1 && r.r.size() >= 3 && r.r[0] == 0x13 && r.r[2] == code && t - movedAt < 6) ++rifleMoving;
                        if (r.slot == 1 && !r.r.empty() && r.r[0] == 0x12) { if (r.r.size() > 2) longView = t; else if (r.r[1] != 0xFF) scopeShortSince = true; }
                        if (r.slot == 1 && r.r.size() >= 2 && r.r[0] == 0x08 && r.r[1] == 0x01) ++raiseRecs;
                        if (r.slot == 1 && r.r.size() >= 2 && r.r[0] == 0x08 && r.r[1] == 0x05) {
                            shotRec = true; shotRecAt = t;
                            if (longView != t) {                  // (the long {12} sent in the same tick, before it)
                                ++shotNoScope;
                                if (getenv("VDOLL_TEST_VERBOSE")) for (size_t j = r0; j < gRecs.size(); ++j) { printf("  f%lld slot %d:", t, gRecs[j].slot); for (uint8_t c : gRecs[j].r) printf(" %02x", c); printf("\n"); }
                            }
                        }
                        if (upAt >= 0 && t >= upAt && r.slot == 2 && r.r.size() >= 9 && r.r[0] == 0x04) ++staleHits;
                        if (upAt >= 0 && t >= upAt && r.slot == 1 && r.r.size() >= 2 && r.r[0] == 0x08 && r.r[1] == 0x05) ++staleShots;
                        if (r.slot == 2 && r.r.size() >= 9 && r.r[0] == 0x04) {
                            ++hitsN;
                            if (shotRec || shotRecAt < 0 || t - shotRecAt > 13) ++hitsFirst;   // (its shot's record a record tick before)
                            if (!dmgSeen && sight >= 0) toHit.push_back(t - sight);
                            dmgSeen += r.r[2] | r.r[3] << 8;
                        }
                    }
                    (void)scopeShortSince;
                    was = now;
                }
                fr += 540;
                vdoll::body::Down(0);
                fr += 10;
            }
    (void)shortAfter;
    auto Median = [](std::vector<long long> v) { if (v.empty()) return -1LL; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    auto Min = [](std::vector<long long> v) { return v.empty() ? -1LL : *std::min_element(v.begin(), v.end()); };
    printf("%d duels, %d fights, %d shots, %d hits; from its sight to its first shot: median %lld, quickest %lld; to its first hit: median %lld, quickest %lld; still before a shot: quickest %lld frames\n",
           duels, fights, shots, hitsN, Median(toShot), Min(toShot), Median(toHit), Min(toHit), Min(stillBefore));
    printf("%d records of the rifle coming up (08 1)\n", raiseRecs);
    printf("%s\n", vdoll::body::Status(0).c_str());
    if (movingShots) Fail("%d shots the frame after a move (a scoped rifle fired while it runs)", movingShots);
    if (earlyShots) Fail("%d shots within 28 frames of a stop (the rifle's swap 6 and its scope's 18 to 22 frames)", earlyShots);
    if (rifleMoving) Fail("%d weapon records naming the rifle while it moves", rifleMoving);
    if (shotNoScope) Fail("%d shot records without the scope's view record before them", shotNoScope);
    if (hitsFirst) Fail("%d hits sent before the record of the shot that made them", hitsFirst);
    if (fastPace) Fail("%d shots under %d frames apart", fastPace, pace);
    if (copyFar) Fail("%d shots with its copy 0.6 m or more behind it (still running on the screens)", copyFar);
    if (pauseTest) {
        printf("%d duels knocked down 4 s right after their first shot\n", pausedDuels);
        if (staleHits) Fail("%d hit records sent after it was up again (the shot before its knock down)", staleHits);
        if (staleShots) Fail("%d shot records sent after it was up again with no enemy in sight", staleShots);
        if (pausedDuels < 10) Fail("only %d duels knocked down", pausedDuels);
    }
    if (fights < 10) Fail("only %d fights in %d duels", fights, duels);
    if (hitsN == 0 && !pauseTest) Fail("no hit at all");   // (VDOLL_TEST_PAUSE: each duel's only shot is the one before its knock down)
    printf("%s: the scoped rifle's checks (7)\n", gFails ? "FAIL" : "PASS");
    return gFails ? 1 : 0;
}
