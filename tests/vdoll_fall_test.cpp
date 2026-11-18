// (v150-host beta 25, fallflinch) A virtual doll's drop off a ledge as a player's own game makes it (docs/VIRTUAL_DOLL.md 55). Measured
// on real clients (probe 52's fall lines; bench ff_falls1, reports/vdoll_fallflinch.md 3): from the edge a player's hips go down 44,
// 109, 196, 305, 436, 588, 762, 958, 1176, 1416, 1677, 1960, 2265, 2592, 2940, 3310 mm by his game's 1st to 16th tick (kPlayerDrop
// below: his own drops and every virtual doll's copy on his screen alike); at the first tick his feet are within 0.77 m of the floor
// the landing's motion begins and at the next one he stands on that floor (1.2 m: the 6th tick, 2.4 m: the 11th or 12th, 3 m: the
// 14th or 15th, 3.45 m: the 17th); then his ledge action holds him 9 ticks more (the book's gate open again 9 ticks after the
// landing in every drop). Beta 15's body (b16, MGO1_VDOLL_HIT_FALL=0) fell on to the floor itself: a metre in 16 frames where a player
// lands at the 5th tick (10 frames), 3 m in 30 frames.
// Off line on a map's .geom (MGO1Loader/vdoll_body.cpp FallAsPlayer): the body sent over its own drop links (MGO1_VDOLL_TEST_TRAP: its
// way's end 2 m past the link's foot), 3 a height band at most (0.45 to 0.8, 0.8 to 1.35, 1.35 to 2.35, 2.35 to 3, 3 to 3.5 m), 6 m
// apart; the doll starts 1.2 m before the link on its floor. Checks:
//   1 it lands (its hips on the floor below) at a player's landing tick for that height, 2 of the host's frames a tick, one frame's
//     slack; VDOLL_TEST_SLOW=k (the host's frames k times longer on the machine's clock, Host::realMs): the same real time;
//   2 at each of a player's ticks before it its hips are within 40 mm of a player's (the frame of that tick);
//   3 still 9 of a player's ticks after its landing (its place within 30 mm, no record of a move: no style {06}), then on its way;
//   4 its records from the edge to its hold's end as a player's game sends his through his ledge action (ff_falls1's P2P log: his
//     state, place {00} and weapon records each tick, one facing record of zeros {01 0 0 0 0 0 0}, no style {06}, no other facing
//     {01}, no look {17}).
// MGO1_VDOLL_HIT_FALL=0 and b16's body (beta 15's fall) FAIL 1, 2 and 4 on every map with a drop the body runs.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_fall_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_fall_test
//   ./vdoll_fall_test <map.geom>        (VDOLL_TEST_VERBOSE=1: the body's lines; VDOLL_TEST_EVENTS=1: each drop's frames;
//                                        VDOLL_TEST_RECS=1: its records from the edge on)
#include "doll_geom.h"
#include "doll_nav.h"
#include "vdoll_body.h"
#include <algorithm>
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
static doll::Geom gG;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0;
static float gSlow = 1.f;
static unsigned long long RealMsNow() { return (unsigned long long)((double)gNow * 1000.0 / 59.94 * gSlow); }
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("  f%lld %s\n", gNow, b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }

// ---- a player's fall as measured (ticks from the edge: the hips' drop in mm), and his landing tick for a drop of h mm
static const float kPlayerDrop[] = {0, 44, 109, 196, 305, 436, 588, 762, 958, 1176, 1416, 1677, 1960, 2265, 2592, 2940, 3310, 3702, 4115};
static const int kPlayerDropN = (int)(sizeof kPlayerDrop / sizeof kPlayerDrop[0]);
static const float kLandWithin = 770.f;            // (his feet within this of the floor at a tick: on it at the next; 661 to 763 seen, 785 to 1034 not)
static const int kPlayerLandHold = 9;               // (his ledge action's ticks after the landing)
static int PlayerLandTick(float h) {
    int n = 1;
    while (n + 1 < kPlayerDropN && kPlayerDrop[n] < h - kLandWithin) ++n;
    return n + 1;
}

// the floor under (x, z) near y from the .geom's faces (-1e30: none)
static bool InsideFace(const doll::Face& f, float x, float z) {
    const int n = (f.v[3][0] == f.v[0][0] && f.v[3][1] == f.v[0][1] && f.v[3][2] == f.v[0][2]) ? 3 : 4;
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float xi = f.v[i][0], zi = f.v[i][2], xj = f.v[j][0], zj = f.v[j][2];
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi)) in = !in;
    }
    return in;
}
static float Floor(float x, float z, float near) {
    float best = -1e30f;
    for (const doll::Face& f : gG.faces) {
        if (f.n[1] < 0.3f || !InsideFace(f, x, z)) continue;
        const float y = f.v[0][1] - (f.n[0] * (x - f.v[0][0]) + f.n[2] * (z - f.v[0][2])) / f.n[1];
        if (y > near + 450.f || y < near - 450.f || y <= best) continue;
        best = y;
    }
    return best;
}

static float gPos[3];
static uint16_t gFacing = 0;
static vdoll::body::Self gS;
struct TRec { long long f; int kind; std::vector<uint8_t> r; };
static std::vector<TRec> gRecs;
static void Send(void*, int, int kind, const std::vector<uint8_t>& r) { gRecs.push_back({gNow, kind, r}); }
// its drops, climbs and fences crossed so far (the body's Status counts: each counted as it begins)
static int Drops() {
    const std::string st = vdoll::body::Status(0);
    const size_t at = st.find(" drops,");
    if (at == std::string::npos) return 0;
    size_t b = at;
    while (b > 0 && st[b - 1] >= '0' && st[b - 1] <= '9') --b;
    return atoi(st.c_str() + b);
}
static void Step() {
    ++gNow;
    memcpy(gPlayers[1].pos, gPos, sizeof gPos);
    vdoll::body::Frame(gNow);
    gS.tick = gNow % 6 == 0;
    vdoll::body::Alive(gS, gNow);
}
static bool gLureOn = false;
static float gLure[3];
static int Traps(float out[][3], int max) { if (!gLureOn || max < 1) return 0; memcpy(out[0], gLure, sizeof gLure); return 1; }
static const float kPi = 3.14159265f;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_fall_test <map.geom>\n"); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); gGeom.resize(ftell(fp)); fseek(fp, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), fp) != gGeom.size()) return 1;
    fclose(fp);
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), gG, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 0);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    setenv("MGO1_VDOLL_SUPPORT", "0", 0);
    setenv("MGO1_VDOLL_PICKUP", "0", 0);
    setenv("MGO1_VDOLL_FOOD", "0", 0);
    if (const char* sl = getenv("VDOLL_TEST_SLOW")) gSlow = std::max(1.f, std::min(3.f, (float)atof(sl)));
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.realMs = RealMsNow; h.traps = Traps;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    gNow = 600;
    const bool on = !(getenv("MGO1_VDOLL_HIT_FALL") && !strcmp(getenv("MGO1_VDOLL_HIT_FALL"), "0"));
    printf("%s: MGO1_VDOLL_HIT_FALL %s, the host's frames %.2f times a 59.94th of a second\n", argv[1], on ? "on" : "0", gSlow);
    gS.k = 0; gS.slot = 1; gS.team = 0; gS.pos = gPos; gS.facing = &gFacing; gS.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].cls = 0;

    doll::NavMesh nav; doll::NavConfig cfg; std::string nl;
    cfg.doorways = !(getenv("MGO1_VDOLL_STAIRS") && !strcmp(getenv("MGO1_VDOLL_STAIRS"), "0"));
    nav.Build(gG, cfg, nl);
    struct Site { float a[3], b[3]; };
    static const float kBand[6] = {450.f, 800.f, 1350.f, 2350.f, 3000.f, 3550.f};
    std::vector<Site> sites[5];
    for (int i = 0; i < nav.LinkCount(); ++i) {
        Site st; uint8_t area = 0;
        if (!nav.LinkAt(i, st.a, st.b, &area) || area != doll::AREA_DROP) continue;
        const float hgt = st.a[1] - st.b[1];
        int band = -1;
        for (int k = 0; k < 5; ++k) if (hgt >= kBand[k] && hgt < kBand[k + 1]) band = k;
        if (band < 0 || sites[band].size() >= 12) continue;
        std::vector<doll::Waypoint> w(2);
        memcpy(w[0].p, st.a, sizeof st.a); w[0].area = area; w[0].link = true;
        memcpy(w[1].p, st.b, sizeof st.b); w[1].area = doll::AREA_STAND; w[1].link = false;
        if (!vdoll::body::WayRunnable(w)) continue;
        bool near = false;
        for (const Site& o : sites[band]) near |= std::hypot(o.a[0] - st.a[0], o.a[2] - st.a[2]) < 6000.f;
        if (!near) sites[band].push_back(st);
    }
    const bool ev = getenv("VDOLL_TEST_EVENTS") != nullptr;
    int tried = 0, ok1 = 0, ok2 = 0, ok3 = 0, ok4 = 0, metre = 0;
    std::string bad;
    for (int band = 0; band < 5; ++band)
        for (size_t si = 0, done = 0; si < sites[band].size() && done < 3; ++si) {
            const Site& st = sites[band][si];
            const float dx = st.b[0] - st.a[0], dz = st.b[2] - st.a[2], dl = std::max(1.f, std::hypot(dx, dz)), ux = dx / dl, uz = dz / dl;
            float s0[3] = {st.a[0] - ux * 1200.f, 0.f, st.a[2] - uz * 1200.f}, lure[3] = {st.b[0] + ux * 2000.f, 0.f, st.b[2] + uz * 2000.f};
            s0[1] = Floor(s0[0], s0[2], st.a[1]); lure[1] = Floor(lure[0], lure[2], st.b[1]);
            if (s0[1] < -1e29f || lure[1] < -1e29f || std::fabs(s0[1] - st.a[1]) > 200.f) { if (ev) printf("  a drop at %.0f %.0f %.0f: no floor before it or past it\n", st.a[0], st.a[1], st.a[2]); continue; }
            gPos[0] = s0[0]; gPos[1] = s0[1] + 800.f; gPos[2] = s0[2];
            gFacing = (uint16_t)(int)(std::atan2(dx, dz) / (2.f * kPi) * 65536.f);
            memcpy(gLure, lure, sizeof gLure); gLureOn = true; setenv("MGO1_VDOLL_TEST_TRAP", "1", 1);
            gS.life = gNow + 1;
            Step();
            gNow += 470;                                     // (its spawn's cooldown over by the clock)
            const int drops0 = Drops();
            long long edge = -1, land = -1, moved = -1;
            float y0 = 0.f, landAt[3] = {0, 0, 0};
            std::vector<float> ys;                            // (its hips each frame from the edge)
            size_t rec0 = 0;
            for (int i = 0; i < 1200; ++i) {
                const float was[3] = {gPos[0], gPos[1], gPos[2]};
                Step();
                if (edge < 0 && Drops() != drops0) {
                    if (std::hypot(gPos[0] - st.a[0], gPos[2] - st.a[2]) > 1500.f) break;   // (another drop)
                    edge = gNow; y0 = was[1]; rec0 = gRecs.size();
                    for (size_t r = 0; r < gRecs.size(); ++r) if (gRecs[r].f >= gNow) { rec0 = r; break; }
                }
                if (edge < 0 && i >= 900) break;
                if (edge >= 0 && land < 0) {
                    ys.push_back(gPos[1]);
                    // (its landing: the first frame its hips stay where they were, 0.2 m or more down; a neighbouring link's floor is
                    // another drop, not counted)
                    const size_t n = ys.size();
                    if (n >= 2 && ys[n - 1] == ys[n - 2] && ys[n - 1] < y0 - 200.f) { land = gNow - 1; memcpy(landAt, gPos, sizeof landAt); }
                }
                if (land >= 0 && gNow > land && moved < 0 && std::hypot(gPos[0] - landAt[0], gPos[2] - landAt[2]) > 30.f) moved = gNow;
                if (moved >= 0 && gNow - moved > 30) break;
            }
            gLureOn = false; unsetenv("MGO1_VDOLL_TEST_TRAP");
            if (edge < 0 || land < 0) { if (ev) printf("  a drop at %.0f %.0f %.0f: not taken (its edge %lld, landing %lld)\n", st.a[0], st.a[1], st.a[2], edge, land); continue; }
            if (std::fabs(landAt[1] - 800.f - st.b[1]) > 200.f) {
                if (ev) printf("  a drop at %.0f %.0f %.0f: another link taken (landed at %.0f, this one's floor %.0f)\n", st.a[0], st.a[1], st.a[2], landAt[1] - 800.f, st.b[1]);
                for (int i = 0; i < 60; ++i) Step();
                continue;
            }
            ++done; ++tried;
            const float hgt = y0 - 800.f - st.b[1];
            if (hgt >= 950.f) ++metre;
            const int pTick = PlayerLandTick(hgt);
            const double wantF = 2.0 * pTick / gSlow, gotF = (double)(land - edge);
            const bool c1 = gotF >= wantF - 1.0 && gotF <= wantF + 1.5;
            // 2: its hips at each of a player's ticks before the landing (the frame of that tick by the clock)
            float worst = 0.f;
            // (a frame whose time is a whole tick of the player's clock: every frame at full pace, every 4th slowed 1.5)
            for (size_t f = 1; f < ys.size(); ++f) {
                const double t = (double)f * gSlow / 2.0;
                const int n = (int)std::lrint(t);
                if (std::fabs(t - n) > 0.01 || n < 1 || n >= pTick || n >= kPlayerDropN) continue;
                const float drop = y0 - ys[f];            // (ys[0]: the edge's frame)
                worst = std::max(worst, std::fabs(drop - std::min(kPlayerDrop[n], hgt)));
            }
            const bool c2 = worst <= 40.f;
            // 3: still 9 of a player's ticks after its landing, then on
            const double holdF = 2.0 * kPlayerLandHold / gSlow, heldF = moved < 0 ? -1.0 : (double)(moved - land);
            const bool c3 = moved >= 0 && heldF >= holdF - 1.0 && heldF <= holdF + 8.0;
            // 4: its records from the edge to its hold's end (a player's ledge action): no style {06}, no look {17}, no facing but one of
            // zeros {01 0 0 0 0 0 0}; its place {00} at each record tick
            const long long holdEnd = land + (long long)std::lrint(holdF);
            bool style = false, facing = false, look = false; int places = 0, ticks = 0, landSaid = 0;
            for (size_t r = rec0; r < gRecs.size(); ++r) {
                const TRec& t = gRecs[r];
                if (t.f < edge || t.f >= holdEnd || t.r.empty()) continue;
                if (t.kind == 1 && t.r[0] == 0x06) style = true;
                if (t.kind == 2 && t.r[0] == 0x17) look = true;
                if (t.kind == 0 && t.r[0] == 0x01) { if (t.r.size() == 7 && !t.r[1] && !t.r[2] && !t.r[3] && !t.r[4] && !t.r[5] && !t.r[6]) ++landSaid; else facing = true; }
                if (t.kind == 0 && t.r.size() >= 13 && t.r[0] == 0x00) ++places;
            }
            for (long long f = edge; f < holdEnd; ++f) if (f % 6 == 0) ++ticks;
            if (getenv("VDOLL_TEST_RECS")) {                 // (its records from the edge to 30 frames after the landing)
                std::string l;
                for (size_t r = rec0; r < gRecs.size(); ++r) if (gRecs[r].f >= edge && gRecs[r].f < land + 30 && !gRecs[r].r.empty()) {
                    char t[40]; snprintf(t, sizeof t, " %+lld:%d:%02x%s", gRecs[r].f - edge, gRecs[r].kind, gRecs[r].r[0], gRecs[r].f >= land ? "L" : ""); l += t;
                }
                printf("  its records (frame from the edge : channel kind : type, L after its landing):%s\n", l.c_str());
            }
            const bool c4 = !style && !facing && !look && places >= ticks - 1 && landSaid == 1;
            if (c1) ++ok1; if (c2) ++ok2; if (c3) ++ok3; if (c4) ++ok4;
            char line[400];
            snprintf(line, sizeof line, "a drop of %.2f m at %.0f %.0f %.0f: landed %.0f frames after the edge (a player's tick %d: %.1f frames), its hips %.0f mm off a player's at most, "
                     "still %.0f frames after (his %.0f), records in the fall and hold: style %s, look %s, facing %s, places %d of %d ticks, {01 0} %d",
                     hgt / 1000.f, st.a[0], st.a[1], st.a[2], gotF, pTick, wantF, worst, heldF, holdF, style ? "SENT" : "none", look ? "SENT" : "none", facing ? "SENT" : "none", places, ticks, landSaid);
            if (ev || !(c1 && c2 && c3 && c4)) printf("  %s%s%s%s%s\n", line, c1 ? "" : " (1 FAIL)", c2 ? "" : " (2 FAIL)", c3 ? "" : " (3 FAIL)", c4 ? "" : " (4 FAIL)");
            if (!(c1 && c2 && c3 && c4)) { bad += "\n    "; bad += line; }
            for (int i = 0; i < 60; ++i) Step();
        }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    printf("drops taken %d (%d of a metre or more): landed at a player's tick %d, hips as a player's %d, held as a player %d, a player's records %d\n", tried, metre, ok1, ok2, ok3, ok4);
    check(ok1 == tried, "1 every drop lands at a player's tick for its height (by the clock)");
    check(ok2 == tried, "2 its hips through the fall as a player's, tick by tick (40 mm)");
    check(ok3 == tried, "3 still 9 of a player's ticks after its landing, then on");
    check(ok4 == tried, "4 its records in the fall and its hold as a player's: no style, look or facing but one of zeros, its place each tick");
    return fails ? 1 : 0;
}
