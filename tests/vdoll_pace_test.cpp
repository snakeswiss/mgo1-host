// (v150-host beta 21, spin2) A virtual doll's run against a player's pace, and its copy on a player's screen
// (MGO1Loader/vdoll_body.cpp GamePace, docs/VIRTUAL_DOLL.md 24.1).
// A player's game moves its player, and every other player's copy on its screen, at one pace set by the floor under him
// (measured on the playable dolls' own records, 115741 running records of 97 benches on the 12 maps of the disc, and on the
// virtual dolls' copies on a playable doll's screen: the same rule for both): 389 mm a record (6 frames) on the flat, DOWN
// any ramp, up a ramp under 15 degrees along the run and over a single step; 225 up a ramp of 15 degrees or more along the
// run; 250 on stairs (flat steps), up and down. A copy runs at that pace along the facing its player told, whatever pace
// the told places show, and turns back toward the told place once it has passed it by more than about 250 mm (benches
// spin_a1 and spin_r1, the playable doll's screen: 29 % of the time at 200 to 300 mm past, 69 % at 300 to 400): where the
// doll runs slower than its copy, the copy passes it and comes back round in a loop.
// The test: a map's .geom; one doll at each of the map's spawn points (8 at most), 90 s each with no enemy (its own ways),
// run a frame at a time; its records as a player's game gets them every 6 frames, and a copy run by them as a player's
// game runs it (each record 4 frames late, the host's relay; along the told facing at the pace of the floor under the copy,
// steered back toward the told line when 300 mm or more aside of it, turned back toward the told place once past it by
// 250 mm; put at the told place for a drop, a climb or a grab, the records with no 06), the pace taken here from the
// .geom's faces themselves (not from the body's mesh nor its code).
// Checks:
//   1 where it runs straight on (four running records in a row, its facing within 10 degrees; no way's end nor corner of
//     20 degrees or more inside the record) over one kind of floor (the pace the same each 50 mm along it), no record's step is under a player's run over the same frames (each frame at
//     the slower pace of the floor where it was and where it got to, along the way it ran in that frame or the one before,
//     a ramp steep from 14.5 degrees: near the edge, at a corner, a face's edge or a riser inside a frame, the body's own
//     reading at the frame's start and the test's may fall either side) by more than 30 mm (times the host's slowing);
//   2 its copy never passes the place it told by more than 250 mm while it runs straight on (the told facing within 10
//     degrees for 3 records): where a player's game turns the copy back round.
// Printed besides: its step by the kind of floor against a player's, the records over a player's pace, the loops of the
// model's copy (300 degrees one way within 90 frames while the told facing turned less than 45), the running records with
// a frame of no move in them (its way's end between two records: its copy runs on).
// MGO1_VDOLL_PACE=0 (beta 20's RunSpeed: slowed from 5 degrees of slope either way by its own measure) fails 1 on every map
// with ramps or stairs it runs over (9 of the 12), and 2 on 8 of them.
//   VDOLL_PACE_STEPS=<file>: no doll; each line "x0 y0 z0 x1 y1 z1 h" is a player's running record on this map (his hips
//   before and after, its length across): the pace taken here against the record's own length (the rule against a game)
//   VDOLL_PACE_AT=x,y,z,degrees: the floor and the pace each 100 mm along the 2 m before that place, that way
//   VDOLL_TEST_EVENTS=1 (or 2: each frame's way and facing): each record under the pace and each pass of the told place
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_pace_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_pace_test
//   MGO1_VDOLL_LEVEL=10 ./vdoll_pace_test <map.geom>
#include "doll_geom.h"
#include "vdoll_body.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static doll::Geom gG;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0;
// (VDOLL_TEST_SLOW=k) a slowed host: its frames k times a 59.94th of a second in real time (its clock, Host::realMs); the
// player's game runs at its own 60 frames a second: k of its frames a host frame (the copy's run and turn)
static float gSlow = 1.f;
static unsigned long long RealMsNow() { return (unsigned long long)((double)gNow * 1000.0 / 59.94 * gSlow); }
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("%s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 0; }
static float WrapD(float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; }
static const float kPi = 3.14159265f;

// ---- the floor from the .geom's faces (a grid of 2 m cells over the floor faces)
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
static bool Inside(const doll::Face& f, float x, float z) {
    const int n = (f.v[3][0] == f.v[0][0] && f.v[3][1] == f.v[0][1] && f.v[3][2] == f.v[0][2]) ? 3 : 4;
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float xi = f.v[i][0], zi = f.v[i][2], xj = f.v[j][0], zj = f.v[j][2];
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi)) in = !in;
    }
    return in;
}
// the highest floor face under (x, z) within 450 mm of near (a player's floor where he stands), its normal; -1e30: none
static float Floor(float x, float z, float near, float* n = nullptr) {
    const int cx = (int)((x - gX0) / kCell), cz = (int)((z - gZ0) / kCell);
    if (cx < 0 || cz < 0 || cx >= gNx || cz >= gNz) return -1e30f;
    float best = -1e30f;
    for (int i : gCells[(size_t)cx * gNz + cz]) {
        const doll::Face& f = gG.faces[i];
        if (!Inside(f, x, z)) continue;
        const float y = f.v[0][1] - (f.n[0] * (x - f.v[0][0]) + f.n[2] * (z - f.v[0][2])) / f.n[1];
        if (y > near + 450.f || y < near - 450.f || y <= best) continue;
        best = y;
        if (n) memcpy(n, f.n, sizeof f.n);
    }
    return best;
}
// a player's run (mm a record) where he stands (his floor fy) running along (ux, uz): the rule measured on the games
// (steep: from that slope up a ramp, 15 degrees; check 1 takes 14.5: within half a degree of the edge the way toward its
// next corner and the way it ran in the frame may fall either side)
static float Pace(float x, float z, float fy, float ux, float uz, float steep = 15.f) {
    float n[3];
    const float y = Floor(x, z, fy, n);
    if (y < -1e29f) return 389.f;
    if (n[1] < std::cos(3.f * kPi / 180.f))           // a ramp
        return -(n[0] * ux + n[2] * uz) / n[1] >= std::tan(steep * kPi / 180.f) ? 225.f : 389.f;
    const float a = Floor(x + ux * 300.f, z + uz * 300.f, y), b = Floor(x - ux * 300.f, z - uz * 300.f, y);
    if (a > -1e29f && b > -1e29f && ((a - y >= 60.f && y - b >= 60.f) || (y - a >= 60.f && b - y >= 60.f))) return 250.f;   // stairs
    return 389.f;
}
static const char* Kind(float x, float z, float fy, float ux, float uz) {
    float n[3];
    const float y = Floor(x, z, fy, n);
    if (y < -1e29f) return "flat";
    const float p = Pace(x, z, fy, ux, uz);
    if (p == 250.f) return "stairs";
    if (n[1] < std::cos(3.f * kPi / 180.f)) return p == 225.f ? "ramp up 15+" : -(n[0] * ux + n[2] * uz) > 0.f ? "ramp up" : "ramp down";
    return "flat";
}

// ---- its records as a player's game gets them
struct Tick { long long frame; float p[3] = {0, 0, 0}, yaw = 0; int style = -1; bool havePos = false, haveYaw = false, land = false; };
static std::vector<Tick> gTicks;
static std::vector<std::array<float, 5>> gFrames;       // (its place each frame: x y z, the frame, its facing)
static void Frames(long long f0, long long f1) {
    std::string s;
    char b[48];
    for (size_t i = 1; i < gFrames.size(); ++i)
        if ((long long)gFrames[i][3] > f0 && (long long)gFrames[i][3] <= f1) {
            const float dx = gFrames[i][0] - gFrames[i - 1][0], dz = gFrames[i][2] - gFrames[i - 1][2];
            if (getenv("VDOLL_TEST_EVENTS")[0] == '2') snprintf(b, sizeof b, " %.0f@%.0f(f%.0f)", std::hypot(dx, dz), std::atan2(dx, dz) * 57.29578f, gFrames[i][4]);
            else snprintf(b, sizeof b, " %.0f", std::hypot(dx, dz));
            s += b; }
    printf("    its steps frame by frame (mm) from %lld to %lld:%s\n", f0, f1, s.c_str());
}
static Tick& Cur() { if (gTicks.empty() || gTicks.back().frame != gNow) { gTicks.push_back(Tick()); gTicks.back().frame = gNow; } return gTicks.back(); }
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) {
    if (slot != 1 || r.empty()) return;
    if (kind == 0 && r[0] == 0x01 && r.size() >= 5) { Tick& t = Cur(); t.yaw = (r[3] | r[4] << 8) * 360.f / 65536.f; t.haveYaw = true; t.land = !r[3] && !r[4]; }
    else if (kind == 0 && r[0] == 0x00 && r.size() >= 13) { Tick& t = Cur(); memcpy(t.p, &r[1], 12); t.havePos = true; }
    else if (kind == 1 && r[0] == 0x06 && r.size() >= 2) Cur().style = r[1];
}

struct Figures {
    int straight = 0, slower = 0, faster = 0, overruns = 0, loops = 0, putBack = 0, running = 0, off20 = 0, off45 = 0, lost = 0, hidden = 0, hiddenFrames = 0, wayEnds = 0, overrunsTurn = 0;
    float seconds = 0, worst = 0;
    std::vector<std::pair<const char*, std::array<float, 2>>> byKind;   // (kind, {its step, the pace})
};
// the copy gets each record this many frames after the doll made it: the host passes a doll's records on with its next
// datagram to that player (4 frames at the median, cover_f2: vdoll_body.cpp gRelayFrames' measure); no network time here
const long long kRelay = 4;
static void Measure(const std::vector<Tick>& in, Figures& f) {
    // the told state carried on (a player's game keeps the last place, facing and style)
    std::vector<Tick> t;
    // (a facing of zero right after ticks with none, or after none at all: the landing's own record after a drop or a climb,
    // a grab's end, as a player's game sends it: no facing, the last one kept)
    Tick c; c.frame = -1;
    long long yawAt = -1000;
    for (const Tick& k : in) {
        if (k.havePos) { memcpy(c.p, k.p, sizeof c.p); c.havePos = true; }
        if (k.haveYaw && !(k.land && k.frame - yawAt > 12)) { c.yaw = k.yaw; c.haveYaw = true; }
        if (k.haveYaw) yawAt = k.frame;
        if (k.style >= 0) c.style = k.style;
        else if (k.havePos) c.style = 0x100;             // (no 06 with its place: a drop, a climb, a grab: the copy is moved by it)
        c.frame = k.frame;
        if (c.havePos && c.haveYaw && c.style >= 0) t.push_back(c);
    }
    if (t.size() < 4) return;
    f.seconds += (t.back().frame - t.front().frame) / 60.f;
    auto run = [&](size_t i) { return t[i].style == 0x40; };
    // its facing against the way it ran in the record (a player runs where he faces: its copy runs along the told facing)
    for (size_t i = 1; i < t.size(); ++i) {
        if (!run(i - 1) || !run(i) || t[i].frame - t[i - 1].frame != 6) continue;
        const float h = std::hypot(t[i].p[0] - t[i - 1].p[0], t[i].p[2] - t[i - 1].p[2]);
        if (h < 100.f || h > 600.f * gSlow) continue;
        const float way = std::atan2(t[i].p[0] - t[i - 1].p[0], t[i].p[2] - t[i - 1].p[2]) * 180.f / kPi, off = std::fabs(WrapD(t[i - 1].yaw - way));
        ++f.running; if (off > 20.f) ++f.off20; if (off > 45.f) ++f.off45;
    }
    // a stop the records do not show: a frame of no move inside a record that says it runs at both ends (its way's end and
    // the next way's start between two records: its copy runs on)
    {
        size_t j = 0;
        for (size_t i = 1; i < t.size(); ++i) {
            if (!run(i - 1) || !run(i) || t[i].frame - t[i - 1].frame != 6) continue;
            while (j < gFrames.size() && (long long)gFrames[j][3] <= t[i - 1].frame) ++j;
            int still = 0;
            for (size_t m = j; m < gFrames.size() && (long long)gFrames[m][3] <= t[i].frame; ++m)
                if (m > 0 && std::hypot(gFrames[m][0] - gFrames[m - 1][0], gFrames[m][2] - gFrames[m - 1][2]) < 5.f) ++still;
            if (still) { ++f.hidden; f.hiddenFrames += still; }
        }
    }
    // 1: its pace where it runs straight on, against a player's over the same frames
    for (size_t i = 2; i + 1 < t.size(); ++i) {
        if (!run(i - 2) || !run(i - 1) || !run(i) || !run(i + 1) || t[i].frame - t[i - 1].frame != 6) continue;
        if (std::fabs(WrapD(t[i].yaw - t[i - 1].yaw)) > 10.f || std::fabs(WrapD(t[i - 1].yaw - t[i - 2].yaw)) > 10.f) continue;
        const float* a = t[i - 1].p;
        const float* b = t[i].p;
        const float h = std::hypot(b[0] - a[0], b[2] - a[2]);
        if (h < 50.f || h > 600.f * gSlow) continue;
        const float ux = (b[0] - a[0]) / h, uz = (b[2] - a[2]) / h;
        const float fb = Floor(b[0], b[2], b[1] - 800.f);
        const float mx = (a[0] + b[0]) / 2.f, mz = (a[2] + b[2]) / 2.f, fm = Floor(mx, mz, (a[1] + b[1]) / 2.f - 800.f);
        if (fb < -1e29f || fm < -1e29f) continue;
        // over one kind of floor (the pace the same each 50 mm along the record: not across a face's edge, a stair's end)
        {
            const float p1 = Pace(mx, mz, fm, ux, uz);
            bool one = true;
            float fy = Floor(a[0], a[2], a[1] - 800.f);
            for (float u = 0.f; u <= h && one; u += 50.f) {
                const float x = a[0] + ux * u, z = a[2] + uz * u;
                fy = Floor(x, z, fy > -1e29f ? fy : a[1] + (b[1] - a[1]) * u / h - 800.f);
                one = fy > -1e29f && Pace(x, z, fy, ux, uz) == p1;
            }
            if (!one) continue;
        }
        // (its way's end or a corner inside the record: a frame of no move, or its run turned 20 degrees between two frames)
        // a player's run over the same frames: each frame at the pace of the floor where it was, along the way it ran
        bool steady = true;
        float was = -1e9f, want = 0.f, fyWas = -1e30f;   // (its floor followed frame to frame, from the record's hips)
        for (size_t m = 1; m < gFrames.size() && steady; ++m) {
            if ((long long)gFrames[m][3] <= t[i - 1].frame || (long long)gFrames[m][3] > t[i].frame) continue;
            const float dx = gFrames[m][0] - gFrames[m - 1][0], dz = gFrames[m][2] - gFrames[m - 1][2], d = std::hypot(dx, dz);
            if (d < 5.f) { steady = false; break; }
            const float w = std::atan2(dx, dz) * 180.f / kPi;
            if (was > -1e8f && std::fabs(WrapD(w - was)) >= 20.f) steady = false;
            was = w;
            const float fy = Floor(gFrames[m - 1][0], gFrames[m - 1][2], fyWas > -1e29f ? fyWas : gFrames[m - 1][1] - 800.f);
            if (fy < -1e29f) { steady = false; break; }
            fyWas = fy;
            // (the slower of: where it was and where it got to in the frame, along the way it ran in this frame or in the one
            // before: a corner, a face's edge, a stair's riser inside the frame)
            float pw = Pace(gFrames[m - 1][0], gFrames[m - 1][2], fy, dx / d, dz / d, 14.5f);
            const float fe = Floor(gFrames[m][0], gFrames[m][2], fy);
            if (fe > -1e29f) pw = std::min(pw, Pace(gFrames[m][0], gFrames[m][2], fe, dx / d, dz / d, 14.5f));
            if (m >= 2) {
                const float px = gFrames[m - 1][0] - gFrames[m - 2][0], pz = gFrames[m - 1][2] - gFrames[m - 2][2], pd = std::hypot(px, pz);
                if (pd > 5.f) pw = std::min(pw, Pace(gFrames[m - 1][0], gFrames[m - 1][2], fy, px / pd, pz / pd, 14.5f));
            }
            want += pw / 6.f * gSlow;
            // (a frame's step short of the pace where the one before ran at it, the floor's pace the same: the end of its way
            // or a corner reached inside the frame, the rest of its run that frame left)
            if (m >= 2) {
                const float before = std::hypot(gFrames[m - 1][0] - gFrames[m - 2][0], gFrames[m - 1][2] - gFrames[m - 2][2]);
                if (d < 0.8f * before && before > 0.9f * pw / 6.f * gSlow) { steady = false; break; }
            }
        }
        for (size_t m = 1; m < gFrames.size() && steady; ++m)   // (and its way's end at the record's end: no move the frame after)
            if ((long long)gFrames[m][3] == t[i].frame + 1 && std::hypot(gFrames[m][0] - gFrames[m - 1][0], gFrames[m][2] - gFrames[m - 1][2]) < 5.f) steady = false;
        if (!steady) { ++f.wayEnds; continue; }
        const float p1 = want;
        ++f.straight;
        f.byKind.push_back({Kind(mx, mz, fm, ux, uz), {h, Pace(mx, mz, fm, ux, uz)}});
        if (h < p1 - 30.f * gSlow) { ++f.slower; f.worst = std::max(f.worst, p1 - h);
            if (getenv("VDOLL_TEST_EVENTS")) { printf("  slower: frame %lld from %.0f %.0f %.0f to %.0f %.0f %.0f, %.0f mm where a player runs %.0f (%s)\n", t[i].frame, a[0], a[1], a[2], b[0], b[1], b[2], h, p1, Kind(mx, mz, fm, ux, uz)); Frames(t[i - 1].frame, t[i].frame); } }
        if (h > p1 + 30.f * gSlow) ++f.faster;
    }
    // 2 and 3: its copy run by its records as a player's game runs it, a frame at a time
    float cx = t[0].p[0], cz = t[0].p[2], cy = Floor(cx, cz, t[0].p[1] - 800.f), cyaw = t[0].yaw * kPi / 180.f, v = 0.f;
    if (cy < -1e29f) cy = t[0].p[1] - 800.f;
    std::vector<float> turn, toldTurn;                   // (a frame's turn of the copy, of the told facing)
    size_t k = 0;
    float toldWas = t[0].yaw;
    std::vector<std::array<float, 3>> seen;               // (VDOLL_TEST_EVENTS: its copy past the told place, its pace, aside)
    for (long long fr = t[0].frame; fr <= t.back().frame; ++fr) {
        bool tick = false;
        while (k + 1 < t.size() && t[k + 1].frame + kRelay <= fr) { ++k; tick = t[k].frame + kRelay == fr; }   // (each record kRelay frames late)
        const Tick& r = t[k];
        const float ty = r.yaw * kPi / 180.f, ux = std::sin(ty), uz = std::cos(ty);
        const float dx = r.p[0] - cx, dz = r.p[2] - cz, d = std::hypot(dx, dz);
        if (d > 3000.f || r.style == 0x100) { cx = r.p[0]; cz = r.p[2]; cy = r.p[1] - 800.f; cyaw = ty; v = 0.f; if (r.style != 0x100) ++f.putBack; turn.push_back(0.f); toldTurn.push_back(0.f); toldWas = r.yaw; continue; }
        const bool moving = r.style == 0x40 || r.style == 0x20;
        const float past = -(dx * ux + dz * uz);         // (the copy past the told place along the told facing)
        if (tick && moving && k >= 2 && t[k - 1].style == 0x40 && t[k - 2].style == 0x40 && std::fabs(WrapD(r.yaw - t[k - 1].yaw)) <= 10.f &&
            std::fabs(WrapD(t[k - 1].yaw - t[k - 2].yaw)) <= 10.f && past > 250.f) {
            // (its facing told the way it ran in the 6 records before: else the copy ran along a facing that was not its way,
            // a turn on the run at a corner, section 24's matter: counted apart)
            bool told = true;
            for (size_t j = k >= 6 ? k - 5 : 1; j <= k && told; ++j) {
                const float sx = t[j].p[0] - t[j - 1].p[0], sz = t[j].p[2] - t[j - 1].p[2];
                if (std::hypot(sx, sz) > 100.f && std::fabs(WrapD(t[j - 1].yaw - std::atan2(sx, sz) * 180.f / kPi)) > 20.f) told = false;
            }
            if (!told) { ++f.overrunsTurn; goto passed; }
            ++f.overruns;
            if (getenv("VDOLL_TEST_EVENTS")) { printf("  overrun: frame %lld, its copy %.0f mm past the told place %.0f %.0f %.0f\n", fr, past, r.p[0], r.p[1], r.p[2]); Frames(fr - 36, fr);
                std::string q; char b[64];
                for (size_t m = seen.size() > 36 ? seen.size() - 36 : 0; m < seen.size(); ++m) { snprintf(b, sizeof b, " %.0f/%.0f/%.0f", seen[m][0], seen[m][1], seen[m][2]); q += b; }
                printf("    its copy past the told place / its pace / aside, frame by frame:%s\n", q.c_str());
                q.clear();
                for (size_t m = k >= 7 ? k - 7 : 1; m <= k; ++m) { snprintf(b, sizeof b, " f%lld:%.0f/%.0f", t[m].frame, t[m].yaw, std::atan2(t[m].p[0] - t[m - 1].p[0], t[m].p[2] - t[m - 1].p[2]) * 180.f / kPi); q += b; }
                printf("    the told facing / the way it ran:%s\n", q.c_str()); }
        }
        passed:
        // running: along its own facing, turned toward the told facing, or toward the told place once past it; still: straight
        // to the told place (its facing toward it, then the told facing), no circle round it
        const float aside = dx * uz - dz * ux;           // (the told place to its side; over 300 mm: steered toward the told line)
        const float want = moving ? (past > 250.f ? std::atan2(dx, dz) : std::fabs(aside) > 300.f ? std::atan2(dx + ux * 600.f, dz + uz * 600.f) : ty)
                                  : d > 300.f ? std::atan2(dx, dz) : ty;
        const float e = std::remainder(want - cyaw, 2.f * kPi);
        const float stepYaw = std::max(-12.f * gSlow * kPi / 180.f, std::min(12.f * gSlow * kPi / 180.f, e));
        cyaw = std::remainder(cyaw + stepYaw, 2.f * kPi);
        turn.push_back(stepYaw * 180.f / kPi);
        toldTurn.push_back(WrapD(r.yaw - toldWas)); toldWas = r.yaw;
        const float sx = moving ? std::sin(cyaw) : d > 1.f ? dx / d : 0.f, sz = moving ? std::cos(cyaw) : d > 1.f ? dz / d : 0.f;
        const float pace = (r.style == 0x20 ? 158.f : Pace(cx, cz, cy, sx, sz)) / 6.f * gSlow;
        if (moving) v = std::min(pace, v + 65.f / 12.f * gSlow);   // (from still to the run in about 12 frames)
        else v = std::min(pace, d);
        cx += sx * v; cz += sz * v;
        if (getenv("VDOLL_TEST_EVENTS")) seen.push_back({past, v * 6.f, aside});
        float ny = Floor(cx, cz, cy);                    // (its floor followed; lost, the told place's floor)
        if (ny < -1e29f) ny = Floor(cx, cz, r.p[1] - 800.f);
        if (ny > -1e29f) cy = ny; else ++f.lost;
    }
    for (size_t i = 0; i < turn.size();) {
        float acc = 0.f, told = 0.f;
        size_t j = i;
        bool hit = false;
        for (; j < turn.size() && j - i <= 90; ++j) { acc += turn[j]; told += std::fabs(toldTurn[j]); if (std::fabs(acc) >= 300.f) { hit = true; break; } }
        if (hit && std::fabs(told) < 45.f) {
            ++f.loops;
            if (getenv("VDOLL_TEST_EVENTS")) printf("  a loop of its copy: frames %lld to %lld\n", t[0].frame + (long long)i, t[0].frame + (long long)j);
            i = j + 1;
        } else ++i;
    }
}

static int Steps(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) { perror(path); return 1; }
    float a[3], b[3], h;
    int n = 0, agree = 0;
    std::vector<std::array<int, 3>> by(4, {0, 0, 0});    // (389, 225, 250, other: the steps, those within 25 mm)
    while (fscanf(fp, "%f %f %f %f %f %f %f", &a[0], &a[1], &a[2], &b[0], &b[1], &b[2], &h) == 7) {
        if (h < 50.f) continue;
        const float ux = (b[0] - a[0]) / h, uz = (b[2] - a[2]) / h, mx = (a[0] + b[0]) / 2.f, mz = (a[2] + b[2]) / 2.f;
        const float fa = Floor(a[0], a[2], a[1] - 800.f), fb = Floor(b[0], b[2], b[1] - 800.f), fm = Floor(mx, mz, (a[1] + b[1]) / 2.f - 800.f);
        if (fa < -1e29f || fb < -1e29f || fm < -1e29f) continue;
        const float p0 = Pace(a[0], a[2], fa, ux, uz), p1 = Pace(mx, mz, fm, ux, uz), p2 = Pace(b[0], b[2], fb, ux, uz);
        if (p0 != p1 || p1 != p2) continue;
        ++n;
        const int c = p1 == 389.f ? 0 : p1 == 225.f ? 1 : p1 == 250.f ? 2 : 3;
        ++by[c][0];
        if (std::fabs(h - p1) <= 25.f) { ++agree; ++by[c][1]; }
        else if (c == 0 && h < 270.f && h > 200.f) ++by[3][2];   // (where the rule runs but the player went at a slope's or a stair's pace)
    }
    fclose(fp);
    printf("a player's running records on one kind of floor: %d, within 25 mm of the pace here %d (%.1f %%); 389: %d of %d, 225: %d of %d, 250: %d of %d\n", n, agree,
           100.0 * agree / std::max(1, n), by[0][1], by[0][0], by[1][1], by[1][0], by[2][1], by[2][0]);
    printf("of the 389's others, at 200 to 270 mm (a slope's or a stair's pace): %d\n", by[3][2]);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_pace_test <map.geom>\n"); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); gGeom.resize(ftell(fp)); fseek(fp, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), fp) != gGeom.size()) return 1;
    fclose(fp);
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), gG, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    BuildGrid();
    if (const char* sp = getenv("VDOLL_PACE_STEPS")) return Steps(sp);
    if (const char* at = getenv("VDOLL_PACE_AT")) {      // (x,y,z,degrees: the floor and the pace each 100 mm along 2 m to that place)
        float q[4];
        if (sscanf(at, "%f,%f,%f,%f", &q[0], &q[1], &q[2], &q[3]) != 4) return 2;
        const float ux = std::sin(q[3] * kPi / 180.f), uz = std::cos(q[3] * kPi / 180.f);
        for (int i = -20; i <= 0; ++i) {
            const float x = q[0] + ux * 100.f * i, z = q[2] + uz * 100.f * i;
            float n[3] = {0, 0, 0};
            const float y = Floor(x, z, q[1] - 800.f, n);
            printf("%.0f %.0f: floor %.0f normal %.3f %.3f %.3f, a player's pace %.0f (%s)\n", x, z, y, n[0], n[1], n[2], Pace(x, z, y, ux, uz), Kind(x, z, y, ux, uz));
        }
        return 0;
    }
    std::vector<const doll::Spawn*> spawns;
    for (const doll::Spawn& sp : gG.spawns) if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) spawns.push_back(&sp);
    if (spawns.size() < 2) { printf("fewer than 2 spawn points in the geom\n"); return 1; }
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 0);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    const bool on = !(getenv("MGO1_VDOLL_PACE") && !strcmp(getenv("MGO1_VDOLL_PACE"), "0"));
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    if (const char* sl = getenv("VDOLL_TEST_SLOW")) { gSlow = std::max(1.f, std::min(3.f, (float)atof(sl))); h.realMs = RealMsNow; }
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    printf("%s: level %s, MGO1_VDOLL_PACE %s, the host's frames %.2f times a 59.94th of a second\n", argv[1], getenv("MGO1_VDOLL_LEVEL"), on ? "on" : "0 (beta 20)", gSlow);
    long long fr = 600;
    float pos[3];
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
    gHave[2] = false;
    const int secs = getenv("VDOLL_TEST_SECONDS") ? std::max(10, atoi(getenv("VDOLL_TEST_SECONDS"))) : 90;
    Figures all;
    for (size_t si = 0; si < spawns.size() && si < 8; ++si) {
        pos[0] = spawns[si]->p[0]; pos[1] = spawns[si]->p[1] + 800.f; pos[2] = spawns[si]->p[2];
        facing = (uint16_t)(si * 9000);
        s.life = fr;
        gTicks.clear();
        for (long long t = fr; t < fr + 60LL * secs; ++t) {
            gNow = t; memcpy(gPlayers[1].pos, pos, sizeof pos);
            vdoll::body::Frame(t); s.tick = t % 6 == 0; vdoll::body::Alive(s, t);
            gFrames.push_back({pos[0], pos[1], pos[2], (float)t, facing * 360.f / 65536.f});
        }
        fr += 60LL * secs;
        Measure(gTicks, all);
        gFrames.clear();
    }
    printf("runs: %.0f s; records running straight on %d: under a player's pace by more than 30 mm %d (the worst %.0f mm), over it %d; "
           "its copy past the told place by more than 250 mm %d times, loops of its copy %d (put back at the told place %d)\n",
           all.seconds, all.straight, all.slower, all.worst, all.faster, all.overruns, all.loops, all.putBack);
    printf("its copy's floor not found in %d frames; running records with a frame of no move in them %d (%d frames); straight records left out for a way's end or a corner inside %d\n", all.lost, all.hidden, all.hiddenFrames, all.wayEnds);
    printf("its copy past the told place by more than 250 mm after a facing told off the way it ran (a turn on the run): %d\n", all.overrunsTurn);
    printf("running records %d: its facing at the record's start more than 20 degrees off the way it ran %d, more than 45 %d\n", all.running, all.off20, all.off45);
    for (const char* kind : {"flat", "ramp up", "ramp up 15+", "ramp down", "stairs"}) {
        std::vector<float> st;
        float pace = 0.f;
        for (auto& e : all.byKind) if (!strcmp(e.first, kind)) { st.push_back(e.second[0]); pace = e.second[1]; }
        if (st.empty()) continue;
        std::sort(st.begin(), st.end());
        printf("  %-12s %5zu records: its step %4.0f mm a record (median; 1 in 10 under %4.0f), a player's %3.0f\n", kind, st.size(), st[st.size() / 2], st[st.size() / 10], pace);
    }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    check(all.slower == 0, "where it runs straight on, no step under a player's pace by more than 30 mm");
    check(all.overruns == 0, "its copy never passes the told place by more than 250 mm while it runs straight on (where a player's game turns it back round)");
    return fails ? 1 : 0;
}
