// (v150-host beta 25, copyleash) A virtual doll and the host's copy of it on a slowed host: HeldBack (vdoll_body.cpp HeldBack, CopyFree;
// MGO1_VDOLL_COPY_LEASH; docs/VIRTUAL_DOLL.md 53).
// The host's game moves its copy of a doll as it moves the copy of a real player (docs/GAME_FIXES.md "a player at rest"; measured on the
// host with MGO1_VDOLL_COPY_TRACE, benches cl_m1_p50 and cl_m2_q5, a virtual doll and a playable doll running laps over City Under
// Siege's long stairs, the host at pace 50 and at 12 to 32): by the doll's records, in the HOST's frames, so on a slowed host it runs a
// player's pace times the frames' slowing behind a body that runs a player's pace in real time. The model here: each record reaches the
// host's game 4 frames after the doll's tick; running (the style record 06 40, state word 0x5002, 0x205002 on stairs) along the told
// facing at the pace of the floor under it (389 mm a record of 6 frames, 250 on stairs, 225 up a ramp of 15 degrees or more: the .geom's
// faces, the rule of tests/vdoll_pace_test.cpp), turned toward the place told once more than 600 mm from it (and running there whatever
// the style, as measured: style 00 far from its record, state 0x5002 at 389), an eighth of the way to it each frame within 600; standing
// (state 3) when the style says still and it is within 600: a quarter of the way a frame (the PC's fix of a copy at rest); a
// record 3000 mm or more from it puts it at the record's x and z at its own height and 240 mm more (measured: 0.2 to 0.3 m), where on
// stairs or a slope it falls (state 0x200002 in the air, 2 under the floor) with no move across until it lands and 18 frames of its
// landing (or, under the floor, the next put); a blow's motion records {08 0A} root it (state 2) while they come; no style record (a drop, a climb: the game plays the
// motion to the place told) puts it there. The mesh's walls stop it (the bodies' mesh, standing); off an edge it falls.
// The host slowed by k (VDOLL_TEST_SLOW style: its clock k times a 59.94th of a second a frame, Host::realMs), k = 1, 2 and 3.
// Parts (a map's .geom; a map with no ramp nor stairs on its mesh has no slope part):
//   slopes  laps over the map's ramps and stairs (6 places 4 m apart where the floor rises 1 m or more over 6 m either way, a lap of 6 m
//           each side, 30 s each at k = 2 and 3): no doll put back by HeldBack (the model copy is never held by anything but the game's
//           own put and fall; a real player there is never put back). Beta 25 (MGO1_VDOLL_COPY_LEASH=0) puts it back where its copy falls
//           after the game's put: FAILS on the maps with such places;
//   flat    (printed, no check) the laps and the spawn points' own ways at k = 1 (the copy within a record or two of it: no put): the holds
//           and the reads left out (a fall off an edge, a link's motion);
//   held    flat laps of 10 m from 6 spawn points; at a lap's start the copy held where it is while it runs (state 0x5002, the game's
//           collision: a prop the mesh lacks), at full pace: put back to it (HeldBack's purpose kept: 3 reads of it still, 6 when its last
//           step before the hold counted), with either switch;
//   wake    a teammate lying knocked out at each slope place, the doll starting 6 m above or below him, k = 2 and 3: no hold between a
//           combo's press and 30 frames after its end (the host's copy plays the blows where it is, its state word still the run's, as
//           measured on cl_w_before), two kicks where it presses twice. Beta 25 (MGO1_VDOLL_COPY_LEASH=0) holds there: FAILS where its copy
//           lags 1.2 m at the press;
//   pace    on the slope laps, the body's step in a host frame never over a player's pace on the floor it ran (k of his frames: the most
//           generous reading of the floor at the step's start, its end and 0.3 m before and after: 389 mm a record, 250 on stairs, 225 up a
//           ramp of 15 degrees or more) by 5 mm times k or more, but in 1 frame in 1000 at most (a doll no faster than a player in real time;
//           the body's stairs are read on its mesh, the test's on the .geom's faces: at some edges they disagree, the pace test's 91 to 98 %
//           agreement with the games).
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_copyleash_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_copyleash_test
//   ./vdoll_copyleash_test <map.geom> [slopes|flat|held|pace|wake ...]   (VDOLL_TEST_VERBOSE=1: the body's lines; VDOLL_TEST_EVENTS=1: each
//   hold and fall)
#include "doll_geom.h"
#include "doll_nav.h"
#include "vdoll_body.h"
#include <algorithm>
#include <array>
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
static doll::NavMesh gNav;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0;
static float gSlow = 1.f;
static int gHolds = 0, gFreeSaid = 0;
static const int kDoll = 2;
static const float kPi = 3.14159265f;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, "vdoll 1: held back")) { ++gHolds; if (getenv("VDOLL_TEST_EVENTS")) printf("      f%lld %s\n", gNow, b); }
    if (strstr(b, "vdoll 1: its copy") && strstr(b, "MGO1_VDOLL_COPY_LEASH)")) ++gFreeSaid;   // (the reads CopyFree left out that beta 25 took for a hold)
    if (getenv("VDOLL_TEST_VERBOSE")) printf("      %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }
static unsigned long long RealMs() { return (unsigned long long)((double)gNow * 1000.0 / 59.94 * gSlow); }

// ---- the floor from the .geom's faces (the pace test's rule)
static bool InsideFace(const doll::Face& f, float x, float z) {
    const int n = (f.v[3][0] == f.v[0][0] && f.v[3][1] == f.v[0][1] && f.v[3][2] == f.v[0][2]) ? 3 : 4;
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float xi = f.v[i][0], zi = f.v[i][2], xj = f.v[j][0], zj = f.v[j][2];
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi)) in = !in;
    }
    return in;
}
static const float kCell = 2000.f;
static float gX0, gZ0;
static int gNx, gNz;
static std::vector<std::vector<int>> gCells;
static void BuildGrid() {
    gX0 = gG.bmin[0] - 1.f; gZ0 = gG.bmin[2] - 1.f;
    gNx = (int)((gG.bmax[0] - gX0) / kCell) + 2; gNz = (int)((gG.bmax[2] - gZ0) / kCell) + 2;
    gCells.assign((size_t)gNx * gNz, {});
    for (size_t i = 0; i < gG.faces.size(); ++i) {
        const doll::Face& f = gG.faces[i];
        if (f.n[1] < 0.3f) continue;
        float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
        for (int k = 0; k < 4; ++k) { lo[0] = std::min(lo[0], f.v[k][0]); hi[0] = std::max(hi[0], f.v[k][0]); lo[1] = std::min(lo[1], f.v[k][2]); hi[1] = std::max(hi[1], f.v[k][2]); }
        const int x0 = std::max(0, (int)((lo[0] - gX0) / kCell)), x1 = std::min(gNx - 1, (int)((hi[0] - gX0) / kCell));
        const int z0 = std::max(0, (int)((lo[1] - gZ0) / kCell)), z1 = std::min(gNz - 1, (int)((hi[1] - gZ0) / kCell));
        for (int x = x0; x <= x1; ++x) for (int z = z0; z <= z1; ++z) gCells[(size_t)x * gNz + z].push_back((int)i);
    }
}
// the highest floor face under (x, z) between lo and hi; -1e30: none (its normal in n)
static float FloorIn(float x, float z, float lo, float hi, float* n = nullptr) {
    const int cx = (int)((x - gX0) / kCell), cz = (int)((z - gZ0) / kCell);
    if (cx < 0 || cz < 0 || cx >= gNx || cz >= gNz) return -1e30f;
    float best = -1e30f;
    for (int i : gCells[(size_t)cx * gNz + cz]) {
        const doll::Face& f = gG.faces[i];
        if (!InsideFace(f, x, z)) continue;
        const float y = f.v[0][1] - (f.n[0] * (x - f.v[0][0]) + f.n[2] * (z - f.v[0][2])) / f.n[1];
        if (y > hi || y < lo || y <= best) continue;
        best = y;
        if (n) memcpy(n, f.n, sizeof f.n);
    }
    return best;
}
static float Floor(float x, float z, float near, float* n = nullptr) { return FloorIn(x, z, near - 450.f, near + 450.f, n); }
// a player's run (mm a record) where he stands (floor fy) running along (ux, uz) (vdoll_pace_test.cpp's rule, measured on the games)
static float Pace(float x, float z, float fy, float ux, float uz) {
    float n[3];
    const float y = Floor(x, z, fy, n);
    if (y < -1e29f) return 389.f;
    if (n[1] < std::cos(3.f * kPi / 180.f)) return -(n[0] * ux + n[2] * uz) / n[1] >= std::tan(15.f * kPi / 180.f) ? 225.f : 389.f;
    const float a = Floor(x + ux * 300.f, z + uz * 300.f, y), b = Floor(x - ux * 300.f, z - uz * 300.f, y);
    if (a > -1e29f && b > -1e29f && ((a - y >= 60.f && y - b >= 60.f) || (y - a >= 60.f && b - y >= 60.f))) return 250.f;
    return 389.f;
}
static bool Stairs(float x, float z, float fy) {         // (flat steps both ways along some direction)
    for (int k = 0; k < 8; ++k) if (Pace(x, z, fy, std::sin(k * kPi / 8.f), std::cos(k * kPi / 8.f)) == 250.f) return true;
    return false;
}

// ---- the host's copy of the doll (the model above)
struct Pending { long long at; int kind; std::vector<uint8_t> r; };
static std::vector<Pending> gPend;
struct Copy {
    float p[3] = {0, 0, 0}, rec[3] = {0, 0, 0}, yaw = 0.f, vy = 0.f;
    int style = 0, mode = 0;                              // mode 0 on its floor, 1 in the air, 2 under the floor
    long long blowUntil = -1, styleAt = -1000, landUntil = -1;
    bool have = false, held = false;
    unsigned state = 3;
    int puts = 0, falls = 0;
};
static Copy gCopy;
static int gKicks = 0, gPresses = 0;
static long long gPressAt = -1;
static const int kMate = 3;                              // (the wake part: a teammate lying knocked out)
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) {
    if (slot == kDoll) gPend.push_back({gNow + 4, kind, r});
    if (slot == kDoll && kind == 1 && r.size() >= 2 && r[0] == 0x08 && r[1] == 0x0A && gNow - gPressAt > 30) { gPressAt = gNow; ++gPresses; }   // (a combo's first motion record)
    if (slot == kMate && kind == 0 && r.size() == 9 && r[0] == 0x04 && r[6] >= 0x95 && r[6] <= 0x97) ++gKicks;   // (a blow that lands: the kick)
}
static int FriendlyFire() { return 1; }
static float GetF(const std::vector<uint8_t>& r, size_t at) { float v; memcpy(&v, r.data() + at, 4); return v; }
static void CopyAt(const float* hips) { Copy c; memcpy(c.p, hips, sizeof c.p); memcpy(c.rec, hips, sizeof c.rec); c.have = true; c.styleAt = gNow; gCopy = c; gPend.clear(); }
static void CopyFrame() {
    Copy& c = gCopy;
    for (size_t i = 0; i < gPend.size();) {
        const Pending& q = gPend[i];
        if (q.at > gNow) { ++i; continue; }
        if (q.kind == 0 && q.r.size() == 13 && q.r[0] == 0x00) { c.rec[0] = GetF(q.r, 1); c.rec[1] = GetF(q.r, 5); c.rec[2] = GetF(q.r, 9); }
        else if (q.kind == 0 && q.r.size() == 7 && q.r[0] == 0x01) c.yaw = (float)(uint16_t)(q.r[3] | (q.r[4] << 8)) / 65536.f * 2.f * kPi;
        else if (q.kind == 1 && q.r.size() == 2 && q.r[0] == 0x06) { c.style = q.r[1]; c.styleAt = gNow; }
        else if (q.kind == 1 && q.r.size() >= 2 && q.r[0] == 0x08 && q.r[1] == 0x0A) c.blowUntil = gNow + 8;
        gPend.erase(gPend.begin() + (long)i);
    }
    if (c.held) { c.state = 0x5002; return; }             // (held by the game's collision: it runs on the spot)
    if (gNow - c.styleAt > 8) {                           // (no style record: a drop, a climb, a fence, a grab; the game plays its motion to the place told)
        memcpy(c.p, c.rec, sizeof c.p); c.mode = 0; c.state = 0x10002; return;
    }
    const float dx = c.rec[0] - c.p[0], dz = c.rec[2] - c.p[2], d = std::hypot(dx, dz);
    if (c.mode != 0) {                                    // (falling: down at the body's measured fall, no move across)
        c.p[1] -= c.vy / 6.f; c.vy += 197.f / 6.f;
        const float g = FloorIn(c.p[0], c.p[2], c.p[1] - 800.f - 3000.f, c.p[1] - 800.f + 50.f);
        if (c.mode == 1 && g > -1e29f && c.p[1] - 800.f <= g) { c.p[1] = g + 800.f; c.mode = 0; c.state = 0x200002; c.landUntil = gNow + 18; return; }
        if (c.mode == 2 && (d >= 3000.f || c.p[1] < c.rec[1] - 5000.f)) { c.mode = 0; c.p[0] = c.rec[0]; c.p[2] = c.rec[2]; c.p[1] = c.rec[1]; ++c.puts; }
        else { c.state = c.mode == 1 ? 0x200002 : 2; return; }
    }
    if (gNow < c.landUntil) { c.state = 0x200002; return; }   // (its landing: 18 frames still, as the body's own landing; measured: 2 reads and more)
    if (d >= 3000.f) {                                    // (the game's put: the record's x and z, its own height and 0.24 m)
        ++c.puts;
        c.p[0] = c.rec[0]; c.p[2] = c.rec[2]; c.p[1] += 240.f;
        const float g = FloorIn(c.p[0], c.p[2], c.p[1] - 800.f - 400.f, c.p[1] - 800.f + 300.f);
        if (g > -1e29f && c.p[1] - 800.f > g + 150.f) { c.mode = 1; c.vy = 0.f; ++c.falls; c.state = 0x200002; return; }
        if (g > -1e29f) { c.p[1] = g + 800.f; c.state = 3; return; }
        const float up = FloorIn(c.p[0], c.p[2], c.p[1] - 800.f, c.p[1] - 800.f + 3000.f);
        if (up > -1e29f) { c.mode = 2; c.vy = 0.f; ++c.falls; c.state = 2; return; }   // (under the floor)
        c.mode = 1; c.vy = 0.f; ++c.falls; c.state = 0x200002; return;
    }
    if (gNow < c.blowUntil) { c.state = Stairs(c.p[0], c.p[2], c.p[1] - 800.f) ? 0x205002 : 0x5002; return; }   // (a blow's motion: rooted, its state word the run's, measured)
    float ux = 0.f, uz = 0.f, step = 0.f;
    if ((c.style & 0x60) || d > 600.f) {                  // (measured: more than 600 mm from its record it runs there whatever the style, 389 a record)
        if (d > 600.f) { ux = dx / d; uz = dz / d; }
        else { ux = std::sin(c.yaw); uz = std::cos(c.yaw); }
        step = ((c.style & 0x40) || d > 600.f ? Pace(c.p[0], c.p[2], c.p[1] - 800.f, ux, uz) : 150.f) / 6.f;
        c.state = Stairs(c.p[0], c.p[2], c.p[1] - 800.f) ? 0x205002 : 0x5002;
    } else {
        c.state = 3;
        if (d > 1.f) { ux = dx / d; uz = dz / d; step = std::min(d, std::max(20.f, 0.25f * d)); }
    }
    float feet[3] = {c.p[0], c.p[1] - 800.f, c.p[2]};
    if (step > 0.f && d > 1.f && d <= 600.f && gNav.ClearAlong(feet, ux, uz, step, true) < step - 1.f) { ux = dx / d; uz = dz / d; }   // (steered to the place told)
    if (step > 0.f) {
        const float clear = gNav.ClearAlong(feet, ux, uz, step, true);
        float x = c.p[0] + ux * std::min(step, clear), z = c.p[2] + uz * std::min(step, clear);
        if ((c.style & 0x60) && d <= 600.f && d > 1.f) { x += (c.rec[0] - x) * 0.125f; z += (c.rec[2] - z) * 0.125f; }
        const float g = FloorIn(x, z, c.p[1] - 800.f - 450.f, c.p[1] - 800.f + 600.f), below = FloorIn(x, z, c.p[1] - 800.f - 3000.f, c.p[1] - 800.f - 450.f);
        if (g > -1e29f) { c.p[0] = x; c.p[2] = z; c.p[1] = g + 800.f; }   // (a step up of 0.6 m at most, a step down)
        else if (below > -1e29f) { c.p[0] = x; c.p[2] = z; c.mode = 1; c.vy = 0.f; c.state = 0x200002; }   // (off an edge: it falls)
        else if (getenv("VDOLL_TEST_EVENTS") && gNow % 30 == 0) printf("      f%lld the copy at %.0f %.0f %.0f finds no floor at %.0f %.0f (clear %.0f of %.0f)\n", gNow, c.p[0], c.p[1], c.p[2], x, z, clear, step);
    }
}

static uint16_t gFacing = 0;
static float gPos[3];
static vdoll::body::Self gS;
static void Step() {
    ++gNow;
    CopyFrame();
    vdoll::PlayerNow& p = gPlayers[kDoll];
    memcpy(p.pos, gCopy.p, sizeof p.pos); memcpy(p.sent, gCopy.rec, sizeof p.sent);
#ifdef VDOLL_PLAYERNOW_STATE
    p.state = gCopy.state;
#endif
    p.style = gCopy.style;
    vdoll::body::Frame(gNow);
    gS.tick = gNow % 6 == 0;
    vdoll::body::Alive(gS, gNow);
}
static void NewLife(const float* feet) {
    vdoll::body::Down(0);
    for (int i = 0; i < 150; ++i) { ++gNow; vdoll::body::Frame(gNow); }   // (the host's frames' length settled at this slowing: vdoll_body.cpp gMsAFrame)
    gPos[0] = feet[0]; gPos[1] = feet[1] + 800.f; gPos[2] = feet[2];
    gS.life = gNow;
    CopyAt(gPos);
}
static int gFails = 0;
static void Check(bool ok, const char* fmt, ...) {
    char b[600];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("  %s %s\n", ok ? "PASS" : "FAIL", b);
    if (!ok) ++gFails;
}
static bool Want(int argc, char** argv, const char* part) {
    bool any = false;
    for (int i = 2; i < argc; ++i) { if (!strcmp(argv[i], part)) return true; if (strchr(argv[i], '.') == nullptr) any = true; }
    return !any;
}
static std::string LapsOf(const float* a, const float* b) {
    char s[160];
    snprintf(s, sizeof s, "test:%.0f,%.0f,%.0f/%.0f,%.0f,%.0f", a[0], a[1], a[2], b[0], b[1], b[2]);
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_copyleash_test <map.geom> [slopes|flat|held|pace|wake ...]\n"); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); gGeom.resize(ftell(fp)); fseek(fp, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), fp) != gGeom.size()) return 1;
    fclose(fp);
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), gG, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    BuildGrid();
    setenv("MGO1_VDOLL_DEBUG", "1", 1);                   // (every hold's line)
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 0);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.realMs = RealMs;
#ifdef VDOLL_HOST_FF
    h.friendlyFire = FriendlyFire;
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (int w = 0; w < 12000 && !gMeshSaid; ++w) { vdoll::body::Frame(0); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    {
        std::string nlog;
        doll::NavConfig cfg;
#ifdef DOLL_NAV_DOORWAYS
        cfg.doorways = true;                              // (the bodies' own mesh: vdoll_body.cpp Build)
#endif
        if (!gNav.Build(gG, cfg, nlog)) { printf("the test's mesh was not built: %s\n", nlog.c_str()); return 1; }
    }
    gNow = 600;
    const char* sw = getenv("MGO1_VDOLL_COPY_LEASH");
    const bool off = sw && !strcmp(sw, "0");
    printf("%s: MGO1_VDOLL_COPY_LEASH %s\n", argv[1], off ? "0 (beta 25's HeldBack)" : "on");
    gS.k = 0; gS.slot = kDoll; gS.team = 1; gS.pos = gPos; gS.facing = &gFacing; gS.send = Send; gS.hp = 200; gS.attacker = -1;
    gHave[kDoll] = true; gPlayers[kDoll] = vdoll::PlayerNow(); gPlayers[kDoll].team = 1; gPlayers[kDoll].object = gPlayers[kDoll].alive = true;
    gPlayers[kDoll].life = 200; gPlayers[kDoll].cls = 1;
    vdoll::body::Frame(gNow);

    // the slope places: mesh points (a 2 m grid on its triangles) where the floor 6 m away rises 1 m or more one way and falls 1 m or more
    // the other along a walkable straight line, with a way between those ends; 6 of them 4 m apart (the steepest first)
    struct Lap { float a[3], b[3], m[3]; float rise; bool stairs; };
    std::vector<Lap> laps;
    {
        std::vector<float> tris;
        std::vector<uint8_t> areas;
        gNav.Triangles(tris, areas);
        std::vector<Lap> all;
        for (size_t t = 0; t + 8 < tris.size(); t += 9) {
            if (areas[t / 9] != doll::AREA_STAND) continue;
            const float* v = &tris[t];
            const float x0 = std::min({v[0], v[3], v[6]}), x1 = std::max({v[0], v[3], v[6]}), z0 = std::min({v[2], v[5], v[8]}), z1 = std::max({v[2], v[5], v[8]});
            for (float x = std::ceil(x0 / 2000.f) * 2000.f; x <= x1; x += 2000.f)
                for (float z = std::ceil(z0 / 2000.f) * 2000.f; z <= z1; z += 2000.f) {
                    const float d0 = (v[3] - v[0]) * (z - v[2]) - (v[5] - v[2]) * (x - v[0]), d1 = (v[6] - v[3]) * (z - v[5]) - (v[8] - v[5]) * (x - v[3]), d2 = (v[0] - v[6]) * (z - v[8]) - (v[2] - v[8]) * (x - v[6]);
                    if (!((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))) continue;
                    const float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2], bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
                    const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
                    if (std::fabs(ny) < 1e-3f) continue;
                    const float q[3] = {x, v[1] - (nx * (x - v[0]) + nz * (z - v[2])) / ny, z};   // (on the triangle's plane)
                    float m[3];
                    if (!gNav.Nearest(q, 300.f, m) || std::hypot(m[0] - x, m[2] - z) > 100.f) continue;
                    Lap best{}; best.rise = 0.f;
                    for (int k = 0; k < 16; ++k) {
                        const float ux = std::sin(k * kPi / 8.f), uz = std::cos(k * kPi / 8.f);
                        if (gNav.ClearAlong(m, ux, uz, 6000.f, true) < 5900.f || gNav.ClearAlong(m, -ux, -uz, 6000.f, true) < 5900.f) continue;
                        const float pa[3] = {m[0] + ux * 6000.f, m[1], m[2] + uz * 6000.f}, pb[3] = {m[0] - ux * 6000.f, m[1], m[2] - uz * 6000.f};
                        float a[3], b[3];
                        bool okA = false, okB = false;     // (the mesh 1 to 3.5 m higher there, and lower the other way)
                        for (float up = 1000.f; up <= 3500.f && !okA; up += 500.f) { const float qa[3] = {pa[0], m[1] + up, pa[2]}; okA = gNav.Nearest(qa, 300.f, a) && std::hypot(a[0] - pa[0], a[2] - pa[2]) < 300.f && a[1] - m[1] >= 1000.f; }
                        for (float up = 1000.f; up <= 3500.f && !okB; up += 500.f) { const float qb[3] = {pb[0], m[1] - up, pb[2]}; okB = gNav.Nearest(qb, 300.f, b) && std::hypot(b[0] - pb[0], b[2] - pb[2]) < 300.f && m[1] - b[1] >= 1000.f; }
                        if (!okA || !okB) continue;
                        float len = 0, end[3];
                        if (!vdoll::body::WayTo(b, a, &len, end) || len > 16000.f || std::hypot(end[0] - a[0], end[2] - a[2]) > 300.f) continue;
                        if (a[1] - b[1] > best.rise) { memcpy(best.a, a, sizeof a); memcpy(best.b, b, sizeof b); memcpy(best.m, m, sizeof m); best.rise = a[1] - b[1]; }
                    }
                    if (best.rise > 0.f) { best.stairs = Stairs(best.m[0], best.m[2], best.m[1]); all.push_back(best); }
                }
        }
        std::stable_sort(all.begin(), all.end(), [](const Lap& x, const Lap& y) { return x.rise > y.rise; });
        for (const Lap& l : all) {
            if (laps.size() >= 6) break;
            bool apart = true;
            for (const Lap& o : laps) if (std::hypot(o.m[0] - l.m[0], o.m[2] - l.m[2]) < 4000.f && std::fabs(o.m[1] - l.m[1]) < 1500.f) apart = false;
            if (apart) laps.push_back(l);
        }
        // (City Under Siege: the long stairs of wakeramp's and this agent's benches too, the victim's place there, x -22500 z 34000)
        if (strstr(argv[1], "n003a")) {
            Lap l{};
            const float q[3] = {-22500.f, 1250.f, 34000.f}, qa[3] = {-27000.f, 3000.f, 34000.f}, qb[3] = {-17000.f, 0.f, 34000.f};
            if (gNav.Nearest(q, 600.f, l.m) && gNav.Nearest(qa, 600.f, l.a) && gNav.Nearest(qb, 600.f, l.b)) { l.rise = l.a[1] - l.b[1]; l.stairs = true; laps.push_back(l); }
        }
        int st = 0; for (const Lap& l : laps) st += l.stairs ? 1 : 0;
        printf("  %zu slope places (of %zu found: the floor 1 m or more higher 6 m one way and lower 6 m the other), %d of them on stairs\n", laps.size(), all.size(), st);
    }
    // a run of the laps at slowing k for `frames`: the holds, the copy's puts and falls, the reads with its copy 1.2 m or more behind, the
    // body's steps in a host frame against a player's pace (over by 5 mm times k or more, and the most over)
    struct Run { int holds = 0, free = 0, puts = 0, falls = 0, reads = 0, far = 0, fast = 0, steps = 0; float most = 0.f; };
    auto runLaps = [&](const Lap& l, float k, int frames) {
        Run r;
        gSlow = k;
        setenv("MGO1_VDOLL_TEST_LAPS", LapsOf(l.a, l.b).c_str(), 1);
        NewLife(l.b);
        const int h0 = gHolds, f0 = gFreeSaid;
        int hw = gHolds;
        float was[3] = {gPos[0], gPos[1], gPos[2]};
        for (int i = 0; i < frames; ++i) {
            Step();
            const bool jump = gHolds != hw;
            hw = gHolds;
            // (its step this frame against a player's pace on the floor it ran, k frames of his: the most generous of the floor's pace at its
            // start, its end and 0.3 m before and after (389, 250 on stairs, 225 up a steep ramp); not a hold's jump, a drop's or a climb's
            // (0.15 m of height or more in the frame))
            const float st = std::hypot(gPos[0] - was[0], gPos[2] - was[2]);
            if (i > 120 && st > 1.f && !jump && std::fabs(gPos[1] - was[1]) < 150.f) {
                const float fa = Floor(was[0], was[2], was[1] - 800.f), fb = Floor(gPos[0], gPos[2], gPos[1] - 800.f);
                const float ux = (gPos[0] - was[0]) / st, uz = (gPos[2] - was[2]) / st;
                if (fa > -1e29f && fb > -1e29f) {
                    const float pa = Pace(was[0], was[2], fa, ux, uz), pb = Pace(gPos[0], gPos[2], fb, ux, uz);
                    float na[3], nb[3];
                    Floor(was[0], was[2], fa, na); Floor(gPos[0], gPos[2], fb, nb);
                    const bool flat = na[1] >= std::cos(3.f * kPi / 180.f) && nb[1] >= std::cos(3.f * kPi / 180.f);
                    // (and the same 0.3 m before and after it: at a riser's edge the body's own reading and the test's may fall either side)
                    const float fc = Floor(was[0] - ux * 300.f, was[2] - uz * 300.f, fa), fd = Floor(gPos[0] + ux * 300.f, gPos[2] + uz * 300.f, fb);
                    const bool around = fc > -1e29f && fd > -1e29f && Pace(was[0] - ux * 300.f, was[2] - uz * 300.f, fc, ux, uz) == pa && Pace(gPos[0] + ux * 300.f, gPos[2] + uz * 300.f, fd, ux, uz) == pa;
                    (void)flat;
                    const float pc = fc > -1e29f ? Pace(was[0] - ux * 300.f, was[2] - uz * 300.f, fc, ux, uz) : 389.f, pd = fd > -1e29f ? Pace(gPos[0] + ux * 300.f, gPos[2] + uz * 300.f, fd, ux, uz) : 389.f;
                    const float allowed = std::max(std::max(pa, pb), std::max(pc, pd));   // (the most generous of the four readings)
                    if (around || allowed == 389.f) {
                        ++r.steps; const float over = st - allowed / 6.f * k; if (over > 5.f * k) ++r.fast; r.most = std::max(r.most, over);
                        if (over > 5.f * k && getenv("VDOLL_TEST_EVENTS")) printf("      f%lld a step of %.0f mm from %.0f %.0f %.0f to %.0f %.0f %.0f where a player runs %.0f (k %.1f)\n", gNow, st, was[0], was[1], was[2], gPos[0], gPos[1], gPos[2], allowed / 6.f * k, k);
                    }
                }
            }
            memcpy(was, gPos, sizeof was);
            if (gNow % 6 == 0) {
                ++r.reads;
                if (std::hypot(gCopy.p[0] - gPos[0], gCopy.p[2] - gPos[2]) >= 1200.f) ++r.far;
            }
        }
        r.holds = gHolds - h0; r.free = gFreeSaid - f0; r.puts = gCopy.puts; r.falls = gCopy.falls;
        unsetenv("MGO1_VDOLL_TEST_LAPS");
        return r;
    };
    if (Want(argc, argv, "slopes") || Want(argc, argv, "pace")) {
        if (laps.empty()) printf("  slopes: no slope place on this map's mesh (nothing to check)\n");
        int holds = 0, falls = 0, puts = 0, fast = 0, steps = 0, free = 0;
        float most = -1e9f;
        for (size_t i = 0; i < laps.size(); ++i) {
            for (float k : {2.f, 3.f}) {
                const Run r = runLaps(laps[i], k, 1800);
                holds += r.holds; falls += r.falls; puts += r.puts; fast += r.fast; steps += r.steps; free += r.free; most = std::max(most, r.most);
                printf("    slope %zu at %.0f %.0f %.0f (%s, %.1f m over 12 m), k %.0f: %d holds, its copy put %d times (%d falls), 1.2 m or more behind at %d of %d reads; "
                       "%d frames' steps measured, %d over a player's pace (the most over %.0f mm)%s\n", i, laps[i].m[0], laps[i].m[1], laps[i].m[2], laps[i].stairs ? "stairs" : "a ramp",
                       laps[i].rise / 1000.f, k, r.holds, r.puts, r.falls, r.far, r.reads, r.steps, r.fast, r.most, r.free ? (" [" + std::to_string(r.free) + " holds beta 25 would have made]").c_str() : "");
            }
        }
        if (Want(argc, argv, "slopes")) {
            Check(holds == 0, "slopes: no doll put back on the slope laps at k 2 and 3 (%d holds; its copy put %d times by the game, %d falls; %d holds beta 25 would have made)", holds, puts, falls, free);
        }
        if (Want(argc, argv, "pace"))
            Check(laps.empty() || (steps > 0 && fast * 1000 <= steps), "pace: the body's step in a host frame over a player's pace on the floor it ran (k of his frames) by 5 mm times k or more: %d of %d, at most 1 in 1000 (the most over %.1f mm)", fast, steps, most);
    }
    if (Want(argc, argv, "flat")) {
        int holds = 0, puts = 0, runs = 0, free = 0;
        for (size_t i = 0; i < laps.size() && i < 3; ++i) { const Run r = runLaps(laps[i], 1.f, 1200); holds += r.holds; puts += r.puts; free += r.free; ++runs; }
        // (and the map's spawn points' own ways, flat or not, at k 1)
        int sp = 0;
        for (const doll::Spawn& s : gG.spawns) {
            if (sp >= 3 || !(s.kind == doll::SP_DM || s.kind == doll::SP_TDM_RED || s.kind == doll::SP_TDM_BLUE)) continue;
            float f[3];
            if (!gNav.Nearest(s.p, 1500.f, f)) continue;
            ++sp; gSlow = 1.f; NewLife(f);
            const int h0 = gHolds, f0 = gFreeSaid;
            for (int i = 0; i < 1800; ++i) Step();
            holds += gHolds - h0; puts += gCopy.puts; free += gFreeSaid - f0; ++runs;
        }
        printf("  flat (no check): at k 1 (its copy within a record or two of it) %d holds in %d runs of 20 to 30 s, %d reads of a copy not run by the game left out "
               "(its copy put %d times; a fall off an edge, a link's motion)\n", holds, runs, free, puts);
    }
    if (Want(argc, argv, "wake")) {
        // a teammate lying knocked out at each slope place, the doll starting 6 m above or below him (the lap's ends), k 2 and 3: no hold
        // between a combo's press and 30 frames after its end, and two kicks (his gauge empty) wherever it pressed twice
        int tries = 0, kicked = 0, holds = 0, presses = 0, kicks = 0;
        std::string say;
        for (size_t i = 0; i < laps.size(); ++i)
            for (float k : {2.f, 3.f})
                for (int side = 0; side < 2; ++side) {
                    gSlow = k;
                    const float* st = side ? laps[i].b : laps[i].a;
                    const float him[3] = {laps[i].m[0], laps[i].m[1], laps[i].m[2]};
                    NewLife(st);
                    for (int t = 0; t < 30; ++t) Step();
                    gHave[kMate] = true;
                    vdoll::PlayerNow& v = gPlayers[kMate];
                    v = vdoll::PlayerNow(); v.team = 1; v.object = v.alive = true; v.life = 200; v.cls = 0;
                    v.pos[0] = him[0]; v.pos[1] = him[1] + 300.f; v.pos[2] = him[2]; v.state = 8u; v.asleep = true;
#ifdef VDOLL_PLAYERNOW_WAKE
                    v.stunMax = 30000; v.wakePart = 50; v.stamina = 0;
#endif
                    const int h0 = gHolds, k0 = gKicks, p0 = gPresses;
                    int hold = 0;
                    long long upAt = -1;
                    for (int t = 0; t < 1500; ++t) {
                        const int hb = gHolds;
                        Step();
                        if (gHolds != hb && gPressAt >= 0 && gNow - gPressAt < 117 + 30 + 10) ++hold;   // (a hold in a combo's motion)
                        if (upAt < 0 && gKicks - k0 >= 2) upAt = gNow + 6;
                        if (upAt >= 0 && gNow == upAt) { v.state = 3u; v.asleep = false; v.pos[1] = him[1] + 800.f; }
                        if (upAt >= 0 && gNow > upAt + 60) break;
                    }
                    gHave[kMate] = false;
                    ++tries; holds += hold; presses += gPresses - p0; kicks += gKicks - k0;
                    if (gKicks - k0 >= 2) ++kicked;
                    char b[120];
                    snprintf(b, sizeof b, " [%zu%s k%.0f: %d presses, %d kicks, %d holds in them, %d holds]", i, side ? " below" : " above", k, gPresses - p0, gKicks - k0, hold, gHolds - h0);
                    say += b;
                }
        if (getenv("VDOLL_TEST_EVENTS")) printf("   %s\n", say.c_str());
        Check(laps.empty() || (holds == 0 && kicked > 0), "wake: a teammate lying knocked out on the slopes, k 2 and 3, the doll 6 m above or below him: %d holds in its blows' motion (%d presses, %d kicks; "
              "two kicks at %d of %d starts)", holds, presses, kicks, kicked, tries);
    }
    if (Want(argc, argv, "held")) {
        // flat laps of 10 m (a straight walkable line from a spawn point, its end within 0.3 m of its height); at the start of a lap its copy
        // held where it is, running (state 0x5002: the game's collision, a prop the mesh lacks): put back to it, at full pace (6 spawn points;
        // on a slowed host the game's own put takes a held copy past the prop once it is 3 m behind, not modelled here)
        int tried = 0, back = 0;
        std::string say;
        {
            const float k = 1.f;
            int sp = 0;
            for (const doll::Spawn& s0 : gG.spawns) {
                if (sp >= 6 || !(s0.kind == doll::SP_DM || s0.kind == doll::SP_TDM_RED || s0.kind == doll::SP_TDM_BLUE)) continue;
                float f[3], e[3];
                if (!gNav.Nearest(s0.p, 1500.f, f)) continue;
                bool ok = false;
                for (int d = 0; d < 16 && !ok; ++d) {
                    const float ux = std::sin(d * kPi / 8.f), uz = std::cos(d * kPi / 8.f), q[3] = {f[0] + ux * 10000.f, f[1], f[2] + uz * 10000.f};
                    ok = gNav.ClearAlong(f, ux, uz, 10000.f, true) >= 9950.f && gNav.Nearest(q, 400.f, e) && std::hypot(e[0] - q[0], e[2] - q[2]) < 200.f && std::fabs(e[1] - f[1]) < 300.f;
                }
                if (!ok) continue;
                ++sp; ++tried; gSlow = k;
                setenv("MGO1_VDOLL_TEST_LAPS", LapsOf(e, f).c_str(), 1);
                NewLife(f);
                for (int i = 0; i < 200; ++i) Step();             // (its lap to e and back: 10 m each)
                float at[3] = {0, 0, 0};
                int reads = -1;
                bool held = false;
                for (int i = 0; i < 1500 && reads != -2; ++i) {
                    const float before[3] = {gPos[0], gPos[1], gPos[2]};
                    Step();
                    const float moved = std::hypot(gPos[0] - before[0], gPos[2] - before[2]);
                    // (its copy held at the lap's end it leaves: the body under 0.3 m from that end and running)
                    if (!held && moved > 1.f && (std::hypot(gPos[0] - f[0], gPos[2] - f[2]) < 300.f || std::hypot(gPos[0] - e[0], gPos[2] - e[2]) < 300.f)) {
                        CopyAt(gCopy.p); gCopy.held = true; gCopy.style = 0x40; memcpy(at, gCopy.p, sizeof at); held = true;
                    }
                    if (!held) continue;
                    const float gap = std::hypot(before[0] - at[0], before[2] - at[2]);
                    if (gap >= 1200.f && reads < 0) reads = 0;
                    if (reads >= 0 && gNow % 6 == 0) ++reads;
                    if (gap >= 1200.f && std::hypot(gPos[0] - at[0], gPos[2] - at[2]) < 10.f) { if (reads <= 12) ++back; say += " " + std::to_string(reads); reads = -2; }
                }
                gCopy.held = false;
                unsetenv("MGO1_VDOLL_TEST_LAPS");
                if (reads != -2) say += held ? " (none)" : " (not held)";
            }
        }
        Check(tried > 0 && back == tried, "held: its copy held while it runs (the game's collision), at full pace: put back to it %d of %d times (the reads after its copy fell 1.2 m behind:%s)", back, tried, say.c_str());
    }
    printf("%s\n", gFails ? "SOME FAILED" : "ALL PASSED");
    return gFails ? 1 : 0;
}
