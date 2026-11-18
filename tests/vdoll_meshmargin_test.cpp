// (v150-host beta 25, meshmargin) The bodies' mesh nearer a wall than a player's body stands (docs/VIRTUAL_DOLL.md 50). A real
// client's body is held about 211 mm off a wall by its own game (its places in benches: the nearest of tens of thousands, on many
// kinds of wall, 211 mm); the virtual dolls walk the bodies' mesh, whose margin (375 mm, Recast's erosion by voxels) was missing in
// places: every eroded cell within 1.5 m of an objective given back (the floor kept against the walls near a KEROTAN, a goal, a
// base), faces that do not reach the floor (a rail, a leaning rock: Recast sees headroom, not a wall) and thin posts (the contour's
// simplification cut round them). A doll stood there closer to a wall than a player can (n003a 27788 -9297: 63 mm off a 125 mm
// wall). With MGO1_VDOLL_MESH_MARGIN (on) each tile's standing and crouching places nearer than 212 mm to a wall of the stage's faces
// are found and the cells round them that are nearer a wall go (doll_nav.h NavConfig::margin); the rest of the mesh is beta 25's.
// Off line on a map's .geom, one map a run:
//   1 the standing polygons of the mesh as the bodies build it (NavConfig::doorways; ::margin with the switch on): every place of
//     them (sampled every 100 mm along their triangles' edges, 15 mm inside) is kPlayerGap or more from every wall (the test's own
//     reading of the stage's faces: too steep to walk, between 0.4 m and 1.7 m over the place's floor, the walk-through layer and
//     the doorways' panels left out, as the mesh leaves them). The places nearer are listed, grouped (1.5 m), with why: near an
//     objective (the floor given back), a wall that starts over 0.9 m (a rail, an overhang), or other.
//   2 (with the switch) what the mesh keeps against beta 25's (MGO1_VDOLL_MESH_MARGIN=0's): the standing area within 3 %; every
//     objective point and every spawn point reached as before (its nearest standing place within 1.5 m, 2.5 m for a spawn, and a
//     way to it from the nearest spawn); every pair of spawns with a way between them before has one now (and its length); the
//     ladders linked as before (printed: the standing area of the parts that hold a spawn, joined by links either way, the stairs
//     of blocks and those with no way between their floors now, the drops, climbs and vaults, the parts); (2f) every link of beta
//     25's mesh (a drop, a climb, a vault, a ladder, stairs of blocks) whose start a body reaches from a spawn leads from its start to
//     its end now (by it or another way; the longer ways and the links whose start no spawn reaches are told).
//   3 the bodies' own (vdoll::body, the switch as set): for each place of part 1 that beta 25's mesh holds nearer than kPlayerGap,
//     the end of a body's way from the nearest spawn to it is kPlayerGap or more from the walls.
// Beta 25's code (the test built on b16 5ab9b78's sources, or MGO1_VDOLL_MESH_MARGIN=0) FAILS parts 1 and 3 on each of the 12 maps
// of the disc; the switch on passes the three.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_meshmargin_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_meshmargin_test
//   ./vdoll_meshmargin_test <map.geom>
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
#include <map>
#include <string>
#include <thread>
#include <vector>

static const float kPlayerGap = 210.f;           // (a real client's body held 211 mm off a wall: the nearest of its places)
static const float kBandLo = 400.f, kBandHi = 1700.f;

// ---- the stage's walls as the test reads them (not the mesh's own code) ----
struct Wall { float v[3][3]; float ymin, ymax, x0, x1, z0, z1; };
static std::vector<Wall> gWalls;
static std::map<long long, std::vector<int>> gBins;
static const float kBin = 1000.f;
static long long Key(int x, int z) { return ((long long)x << 32) ^ (unsigned)z; }
static void ReadWalls(const doll::Geom& g, const doll::NavConfig& c) {
    const float walkY = std::cos(c.slope * 3.14159265f / 180.f);
    for (const doll::Face& f : g.faces) {
        if ((f.attr & 0x2000000u) && f.n[1] < 0.7f) continue;                     // (the walk-through layer)
        if (c.doorways && (f.attr & doll::kDoorway) && f.n[1] < 0.7f) continue;   // (the doorways' panels)
        if (f.n[1] >= walkY) continue;                                             // (a floor)
        const bool quad = memcmp(f.v[3], f.v[0], sizeof f.v[0]) && memcmp(f.v[3], f.v[2], sizeof f.v[0]);
        static const int idx[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (int t = 0; t < (quad ? 2 : 1); ++t) {
            Wall w;
            for (int k = 0; k < 3; ++k) memcpy(w.v[k], f.v[idx[t][k]], sizeof w.v[k]);
            w.ymin = std::min({w.v[0][1], w.v[1][1], w.v[2][1]}); w.ymax = std::max({w.v[0][1], w.v[1][1], w.v[2][1]});
            w.x0 = std::min({w.v[0][0], w.v[1][0], w.v[2][0]}); w.x1 = std::max({w.v[0][0], w.v[1][0], w.v[2][0]});
            w.z0 = std::min({w.v[0][2], w.v[1][2], w.v[2][2]}); w.z1 = std::max({w.v[0][2], w.v[1][2], w.v[2][2]});
            if (w.ymax - w.ymin < 1.f) continue;
            const int id = (int)gWalls.size();
            gWalls.push_back(w);
            for (int bz = (int)std::floor(w.z0 / kBin); bz <= (int)std::floor(w.z1 / kBin); ++bz)
                for (int bx = (int)std::floor(w.x0 / kBin); bx <= (int)std::floor(w.x1 / kBin); ++bx) gBins[Key(bx, bz)].push_back(id);
        }
    }
}
// the face's cut at height Y (a segment on x z)
static bool Slice(const Wall& w, float Y, float* a, float* b) {
    float pts[3][2]; int n = 0;
    for (int i = 0; i < 3 && n < 3; ++i) {
        const float* p = w.v[i]; const float* q = w.v[(i + 1) % 3];
        const float dp = p[1] - Y, dq = q[1] - Y;
        if ((dp < 0 && dq > 0) || (dp > 0 && dq < 0)) { const float t = dp / (dp - dq); pts[n][0] = p[0] + (q[0] - p[0]) * t; pts[n][1] = p[2] + (q[2] - p[2]) * t; ++n; }
        else if (dp == 0) { pts[n][0] = p[0]; pts[n][1] = p[2]; ++n; }
    }
    if (n < 2) return false;
    a[0] = pts[0][0]; a[1] = pts[0][1]; b[0] = pts[1][0]; b[1] = pts[1][1];
    return true;
}
static float SegDist(float px, float pz, const float* a, const float* b) {
    const float ux = b[0] - a[0], uz = b[1] - a[1], L = ux * ux + uz * uz;
    float t = L > 1e-6f ? ((px - a[0]) * ux + (pz - a[1]) * uz) / L : 0.f;
    t = std::max(0.f, std::min(1.f, t));
    return std::hypot(px - (a[0] + ux * t), pz - (a[1] + uz * t));
}
// the nearest wall to the floor point p between p+lo and p+hi (50 mm cuts), within 1 m; *lowest: the lowest height (over p) of the
// nearest wall's cuts that are within kPlayerGap (the wall's foot near p)
static float WallDist(const float* p, float lo, float hi, float* lowest = nullptr) {
    float best = 1e9f, low = 1e9f;
    const float R = 1000.f;
    for (int bz = (int)std::floor((p[2] - R) / kBin); bz <= (int)std::floor((p[2] + R) / kBin); ++bz)
        for (int bx = (int)std::floor((p[0] - R) / kBin); bx <= (int)std::floor((p[0] + R) / kBin); ++bx) {
            auto it = gBins.find(Key(bx, bz));
            if (it == gBins.end()) continue;
            for (int i : it->second) {
                const Wall& w = gWalls[i];
                if (w.ymax < p[1] + lo || w.ymin > p[1] + hi) continue;
                if (p[0] < w.x0 - R || p[0] > w.x1 + R || p[2] < w.z0 - R || p[2] > w.z1 + R) continue;
                for (float h = lo; h <= hi + 1.f; h += 50.f) {
                    float a[2], b[2];
                    if (!Slice(w, p[1] + h, a, b)) continue;
                    const float d = SegDist(p[0], p[2], a, b);
                    best = std::min(best, d);
                    if (d < kPlayerGap) low = std::min(low, h);
                }
            }
        }
    if (lowest) *lowest = low;
    return best;
}

// ---- the mesh's standing places ----
struct Near { float p[3]; float d; int why; };    // (why: 0 near an objective, 1 a wall starting over 0.9 m, 2 other)
struct Stats {
    double standArea = 0, reach = 0; long samples = 0, offFloor = 0; int polys = 0, ladders = 0, stairs = 0, links[10] = {0}, parts = 0, largest = 0; long ms = 0;
    std::vector<Near> near;
};
static std::vector<std::array<float, 3>> gObjectives;
static std::vector<doll::Spawn> gSpawnsAll;          // (every point of chunk 7 but the cameras)
static bool NearObjective(const float* p) {
    for (const auto& o : gObjectives) if (std::hypot(o[0] - p[0], o[2] - p[2]) < 1700.f && p[1] > o[1] - 2600.f && p[1] < o[1] + 1100.f) return true;
    return false;
}
static void Measure(const doll::NavMesh& nav, Stats& s) {
    std::vector<float> tris; std::vector<uint8_t> areas; std::vector<int> comp;
    nav.Triangles(tris, areas, &comp);
    std::map<long long, int> seen;
    for (size_t t = 0; t < areas.size(); ++t) {
        if (areas[t] != doll::AREA_STAND) continue;
        const float* v = &tris[9 * t];
        s.standArea += 0.5 * std::fabs((v[3] - v[0]) * (v[8] - v[2]) - (v[5] - v[2]) * (v[6] - v[0]));
        const float cx = (v[0] + v[3] + v[6]) / 3.f, cz = (v[2] + v[5] + v[8]) / 3.f;
        for (int e = 0; e < 3; ++e) {
            const float* a = v + 3 * e; const float* b = v + 3 * ((e + 1) % 3);
            const int n = std::max(1, (int)std::ceil(std::hypot(b[0] - a[0], b[2] - a[2]) / 100.f));
            for (int i = 0; i <= n; ++i) {
                const float k = (float)i / n;
                float p[3] = {a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k};
                const float dd = std::hypot(cx - p[0], cz - p[2]);
                if (dd > 15.f) { p[0] += (cx - p[0]) * 15.f / dd; p[2] += (cz - p[2]) * 15.f / dd; }
                const long long key = ((long long)std::lround(p[0] / 25.f) << 40) ^ ((long long)std::lround(p[2] / 25.f) << 16) ^ std::lround(p[1] / 200.f);
                if (!seen.emplace(key, 1).second) continue;
                ++s.samples;
                // (the band from the stage's floor under the place, the first within 0.35 m of the mesh's height: the mesh's heights
                // are up to 0.2 m off a slope. A place with none is not counted but told: the mesh's detail there lies off every floor of
                // the stage, beside a ledge or a step)
                const float up[3] = {p[0], p[1] + 350.f, p[2]};
                const float fl = nav.GroundBelow(up, 700.f);
                if (fl < -1e29f) { ++s.offFloor; continue; }
                p[1] = fl;
                float low = 0;
                const float d = WallDist(p, kBandLo, kBandHi, &low);
                if (d < kPlayerGap) s.near.push_back({{p[0], p[1], p[2]}, d, NearObjective(p) ? 0 : low > 900.f ? 1 : 2});
            }
        }
    }
    {   // (the standing area of the parts that hold a spawn: where a doll can go from where it appears)
        std::vector<char> withSpawn(nav.ComponentCount() + 1, 0);
        for (const doll::Spawn& sp : gSpawnsAll) { float o[3]; const float q[3] = {sp.p[0], sp.p[1], sp.p[2]}; if (nav.Nearest(q, 2500.f, o)) { const int c = nav.ComponentAt(o); if (c >= 0) withSpawn[c] = 1; } }
        for (size_t t = 0; t < areas.size(); ++t) {
            if (areas[t] != doll::AREA_STAND || comp[t] < 0 || !withSpawn[comp[t]]) continue;
            const float* v = &tris[9 * t];
            s.reach += 0.5 * std::fabs((v[3] - v[0]) * (v[8] - v[2]) - (v[5] - v[2]) * (v[6] - v[0]));
        }
    }
    s.polys = nav.Polys(); s.ladders = (int)nav.Ladders().size(); s.stairs = (int)nav.AllSteps().size(); s.parts = nav.ComponentCount();
    for (int c = 0; c < nav.ComponentCount(); ++c) s.largest = std::max(s.largest, nav.ComponentSize(c));
    for (int i = 0; i < nav.LinkCount(); ++i) { float a[3], b[3]; uint8_t ar = 0; if (nav.LinkAt(i, a, b, &ar) && ar < 10) ++s.links[ar]; }
}
// the places grouped (1.5 m across, 1 m up)
static std::vector<int> Group(const std::vector<Near>& v, int* groups) {
    std::vector<int> g(v.size(), -1);
    int n = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (g[i] >= 0) continue;
        g[i] = n;
        std::vector<size_t> st{i};
        while (!st.empty()) {
            const size_t k = st.back(); st.pop_back();
            for (size_t j = 0; j < v.size(); ++j)
                if (g[j] < 0 && std::hypot(v[j].p[0] - v[k].p[0], v[j].p[2] - v[k].p[2]) < 1500.f && std::fabs(v[j].p[1] - v[k].p[1]) < 1000.f) { g[j] = n; st.push_back(j); }
        }
        ++n;
    }
    *groups = n;
    return g;
}
static bool MarginOn() { const char* e = getenv("MGO1_VDOLL_MESH_MARGIN"); return !e || strcmp(e, "0") != 0; }
static bool Build(const doll::Geom& g, bool margin, doll::NavMesh& nav, Stats& s) {
    doll::NavConfig c;
    c.doorways = true;                           // (the bodies' own, MGO1_VDOLL_STAIRS on)
#ifdef DOLL_NAV_MARGIN
    c.margin = margin;
#else
    (void)margin;
#endif
    std::string log;
    const auto t0 = std::chrono::steady_clock::now();
    if (!nav.Build(g, c, log)) return false;
    s.ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    printf("  %s: %s (%ld ms)\n", margin ? "the margin" : "beta 25", log.c_str(), s.ms);
    Measure(nav, s);
    return true;
}

// ---- the bodies' harness (part 3) ----
static std::vector<uint8_t> gGeom;
static bool gMeshSaid = false;
static std::string gMeshLine;
static void Log(const char* fmt, ...) {
    char b[2048]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) { gMeshSaid = true; gMeshLine = b; }
    if (strstr(b, "MGO1_VDOLL_MESH_MARGIN")) printf("  | %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int, vdoll::PlayerNow*) { return false; }
static int Rule() { return 1; }
static int WeaponRule() { return 0; }
static int Phase() { return 2; }

static int gFails = 0, gPasses = 0;
static void Check(bool ok, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf("%s ", ok ? "PASS" : "FAIL"); vprintf(fmt, ap); printf("\n");
    va_end(ap);
    if (ok) ++gPasses; else ++gFails;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "vdoll_meshmargin_test <map.geom>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("no file %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 2;
    fclose(f);
    doll::Geom g; std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("%s: %s\n", argv[1], err.c_str()); return 2; }
    for (const doll::Spawn& sp : g.spawns)
        if (sp.kind == doll::SP_TCM_GOAL || sp.kind == doll::SP_KEROTAN || sp.kind == doll::SP_CMPLUS_GOAL || sp.kind == doll::SP_SR_TARGET ||
            sp.kind == doll::SP_OBJECT_RED || sp.kind == doll::SP_OBJECT_BLUE) gObjectives.push_back({sp.p[0], sp.p[1], sp.p[2]});
    for (const doll::Spawn& sp : g.spawns) if (sp.kind != doll::SP_CAMERA) gSpawnsAll.push_back(sp);
    doll::NavConfig rc; rc.doorways = true;
    ReadWalls(g, rc);
#ifdef DOLL_NAV_MARGIN
    const bool on = MarginOn();
#else
    const bool on = false;                       // (beta 25's sources: no margin)
#endif
    printf("%s: %zu faces, %zu walls, %zu objectives; the switch %s\n", argv[1], g.faces.size(), gWalls.size(), gObjectives.size(), on ? "on" : "off");
    doll::NavMesh before, after;
    Stats sb, sa;
    if (!Build(g, false, before, sb)) { printf("beta 25's mesh not built\n"); return 1; }
    doll::NavMesh& mesh = on ? after : before;
    Stats& sm = on ? sa : sb;
    if (on && !Build(g, true, after, sa)) { printf("the margin's mesh not built\n"); return 1; }

    // part 1
    static const char* kWhy[3] = {"near an objective", "a wall from over 0.9 m", "other"};
    for (int which = 0; which < (on ? 2 : 1); ++which) {
        const Stats& s = which ? sa : sb;
        int groups = 0, why[3] = {0, 0, 0};
        const std::vector<int> grp = Group(s.near, &groups);
        float nearest = 1e9f;
        for (const Near& n : s.near) { ++why[n.why]; nearest = std::min(nearest, n.d); }
        printf("  %s: %lld standing places (%ld with no floor of the stage within 0.35 m of the mesh's height, not counted), %zu nearer than %.0f mm to a wall "
               "(%d near an objective, %d a wall from over 0.9 m, %d other) in %d places%s", which ? "the margin" : "beta 25", (long long)s.samples, s.offFloor, s.near.size(),
               kPlayerGap, why[0], why[1], why[2], groups, s.near.empty() ? "\n" : "");
        if (!s.near.empty()) printf(", the nearest %.0f mm\n", nearest);
        for (int k = 0; k < groups && k < 400; ++k) {
            int bi = -1, n = 0;
            for (size_t i = 0; i < s.near.size(); ++i) if (grp[i] == k) { ++n; if (bi < 0 || s.near[i].d < s.near[bi].d) bi = (int)i; }
            const Near& x = s.near[bi];
            if (getenv("VDOLL_TEST_VERBOSE") || k < 12)
                printf("    %s place %d: %.0f mm at %.0f %.0f %.0f (%d samples, %s)\n", which ? "margin" : "b25", k, x.d, x.p[0], x.p[1], x.p[2], n, kWhy[x.why]);
        }
    }
    Check(sm.near.empty(), "1 every standing place of the bodies' mesh %.0f mm or more from a wall (%zu nearer)", kPlayerGap, sm.near.size());

    // part 2 (the switch on): what the mesh keeps
    if (on) {
        printf("  the standing area reached from the spawns' parts %.0f m2 -> %.0f m2 (%.2f %%)\n", sb.reach / 1e6, sa.reach / 1e6, 100.0 * (sa.reach - sb.reach) / std::max(1.0, sb.reach));
        printf("  the standing area %.0f m2 -> %.0f m2 (%.2f %%), polygons %d -> %d, parts %d -> %d (the largest %d -> %d polygons), ladders %d -> %d, stairs %d -> %d, "
               "drops %d -> %d, climbs %d -> %d, vaults %d -> %d; built in %ld -> %ld ms\n", sb.standArea / 1e6, sa.standArea / 1e6, 100.0 * (sa.standArea - sb.standArea) / sb.standArea,
               sb.polys, sa.polys, sb.parts, sa.parts, sb.largest, sa.largest, sb.ladders, sa.ladders, sb.stairs, sa.stairs, sb.links[doll::AREA_DROP], sa.links[doll::AREA_DROP],
               sb.links[doll::AREA_CLIMB], sa.links[doll::AREA_CLIMB], sb.links[doll::AREA_VAULT], sa.links[doll::AREA_VAULT], sb.ms, sa.ms);
        // (the parts are joined by links either way, a drop's too: a floor a doll only drops from is in its part; told, not checked: the
        // ways themselves are, 2c to 2e)
        Check(sa.standArea >= sb.standArea * 0.97, "2a the standing area within 3 %% of beta 25's");
        int stairsLost = 0;
        for (const auto& st : before.AllSteps()) {
            bool found = false;
            for (const auto& s2 : after.AllSteps())
                if ((std::hypot(st.a[0] - s2.a[0], st.a[2] - s2.a[2]) < 2500.f && std::hypot(st.b[0] - s2.b[0], st.b[2] - s2.b[2]) < 2500.f && std::fabs(st.b[1] - s2.b[1]) < 600.f) ||
                    (std::hypot(st.a[0] - s2.b[0], st.a[2] - s2.b[2]) < 2500.f && std::hypot(st.b[0] - s2.a[0], st.b[2] - s2.a[2]) < 2500.f)) found = true;
            if (found) continue;
            // (no such stairs now: a way between its two floors all the same, the mesh's own or another link's?)
            float a0[3], b0[3], len = 0; std::vector<doll::Waypoint> w;
            const bool way = after.Nearest(st.a, 600.f, a0) && after.Nearest(st.b, 600.f, b0) && after.Path(a0, b0, w, &len) && std::hypot(w.back().p[0] - b0[0], w.back().p[2] - b0[2]) < 300.f &&
                             std::fabs(w.back().p[1] - b0[1]) < 300.f;
            if (!way) ++stairsLost;
            printf("    beta 25's stairs of blocks %.0f %.0f %.0f to %.0f %.0f %.0f: none like it now, %s\n", st.a[0], st.a[1], st.a[2], st.b[0], st.b[1], st.b[2],
                   way ? "a way between its floors all the same" : "and no way between its floors");
        }
        // (a stairs of blocks with no way between its floors now is told: the ways a doll needs are checked by 2c to 2e)
        Check(sa.ladders == sb.ladders && sa.links[doll::AREA_LADDER] == sb.links[doll::AREA_LADDER], "2b the ladders (%d, linked %d) as beta 25's (%d, %d); "
              "its stairs of blocks %d, %d of them with no way between their floors now (%d stairs now)", sa.ladders, sa.links[doll::AREA_LADDER], sb.ladders,
              sb.links[doll::AREA_LADDER], sb.stairs, stairsLost, sa.stairs);
        // the spawns and the objectives: their nearest place and a way to them
        std::vector<const doll::Spawn*> spawns, objs;
        for (const doll::Spawn& sp : g.spawns) {
            const bool obj = sp.kind == doll::SP_TCM_GOAL || sp.kind == doll::SP_KEROTAN || sp.kind == doll::SP_CMPLUS_GOAL || sp.kind == doll::SP_SR_TARGET ||
                             sp.kind == doll::SP_OBJECT_RED || sp.kind == doll::SP_OBJECT_BLUE;
            if (obj) objs.push_back(&sp);
            if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE || sp.kind == doll::SP_TDM_FIRST_RED || sp.kind == doll::SP_TDM_FIRST_BLUE ||
                sp.kind == doll::SP_TCM_RED || sp.kind == doll::SP_TCM_BLUE || sp.kind == doll::SP_UNKNOWN) spawns.push_back(&sp);
        }
        auto nearest = [](const doll::NavMesh& nav, const float* p, float ext, float* out) {
            const float q[3] = {p[0], p[1] + 100.f, p[2]};
            const float fl = nav.GroundBelow(q, 3000.f);
            const float at[3] = {p[0], fl > -1e29f ? fl : p[1], p[2]};
            return nav.Nearest(at, ext, out) ? std::hypot(out[0] - at[0], out[2] - at[2]) : 1e9f;
        };
        int spLost = 0, objLost = 0, pairs = 0, pairsLost = 0;
        float worst = 1.f;
        for (const doll::Spawn* sp : spawns) {
            float a[3], b[3];
            const float db = nearest(before, sp->p, 2500.f, b), da = nearest(after, sp->p, 2500.f, a);
            if (db <= 2500.f && da > 2500.f) { ++spLost; printf("    a spawn (%s) at %.0f %.0f %.0f: its nearest place %.0f mm off before, none within 2.5 m now\n", doll::SpawnKindName(sp->kind), sp->p[0], sp->p[1], sp->p[2], db); }
        }
        for (const doll::Spawn* o : objs) {
            float a[3], b[3];
            const float db = nearest(before, o->p, 1500.f, b), da = nearest(after, o->p, 1500.f, a);
            // a way to it from the nearest spawn that reaches it on beta 25's mesh
            bool wayB = false, wayA = false; float lb = 0, la = 0;
            float bestD = 1e30f; const doll::Spawn* from = nullptr;
            for (const doll::Spawn* sp : spawns) {
                std::vector<doll::Waypoint> w; float len = 0, s0[3];
                if (nearest(before, sp->p, 2500.f, s0) > 2500.f || db > 1500.f || !before.Path(s0, b, w, &len)) continue;
                if (len < bestD) { bestD = len; from = sp; }
            }
            if (from) {
                float s0[3], s1[3]; std::vector<doll::Waypoint> w;
                nearest(before, from->p, 2500.f, s0); wayB = before.Path(s0, b, w, &lb);
                wayA = nearest(after, from->p, 2500.f, s1) <= 2500.f && da <= 1500.f && after.Path(s1, a, w, &la) && std::hypot(w.back().p[0] - a[0], w.back().p[2] - a[2]) < 300.f;
            }
            const bool lost = (db <= 1500.f && da > 1500.f) || (wayB && !wayA);
            if (lost) ++objLost;
            printf("    objective %s at %.0f %.0f %.0f: its nearest place %.0f -> %.0f mm off; a way from the nearest spawn %s%.1f -> %s%.1f m%s\n", doll::SpawnKindName(o->kind), o->p[0], o->p[1], o->p[2],
                   db, da, wayB ? "" : "none ", lb / 1000.f, wayA ? "" : "none ", la / 1000.f, lost ? "  LOST" : "");
        }
        // every pair of spawns (the first 40) with a way before: a way now
        const size_t ns = std::min<size_t>(spawns.size(), 40);
        for (size_t i = 0; i < ns; ++i)
            for (size_t j = i + 1; j < ns; ++j) {
                float a0[3], a1[3], b0[3], b1[3]; std::vector<doll::Waypoint> w; float lb = 0, la = 0;
                if (nearest(before, spawns[i]->p, 2500.f, b0) > 2500.f || nearest(before, spawns[j]->p, 2500.f, b1) > 2500.f || !before.Path(b0, b1, w, &lb) ||
                    std::hypot(w.back().p[0] - b1[0], w.back().p[2] - b1[2]) > 300.f) continue;
                ++pairs;
                if (nearest(after, spawns[i]->p, 2500.f, a0) > 2500.f || nearest(after, spawns[j]->p, 2500.f, a1) > 2500.f || !after.Path(a0, a1, w, &la) ||
                    std::hypot(w.back().p[0] - a1[0], w.back().p[2] - a1[2]) > 300.f) { ++pairsLost; printf("    no way now between spawns %zu and %zu (%.1f m before)\n", i, j, lb / 1000.f); continue; }
                if (lb > 1000.f) worst = std::max(worst, la / lb);
            }
        printf("  %zu spawns, %zu objectives; %d pairs of spawns with a way before, %d without one now; the longest way now %.2f times its length before\n",
               spawns.size(), objs.size(), pairs, pairsLost, worst);
        Check(spLost == 0, "2c every spawn point reached as before (%d lost)", spLost);
        Check(objLost == 0, "2d every objective point reached as before, a way to it from the nearest spawn (%d lost)", objLost);
        Check(pairsLost == 0, "2e every pair of spawns with a way between them before has one now (%d of %d lost)", pairsLost, pairs);
        // every link of beta 25's mesh (a drop, a climb, a vault, a ladder, stairs of blocks): a way from its start to its end now (by it
        // or another way); one with none, or more than 5 m and twice longer than before, told, with whether a spawn reaches its start on
        // beta 25's mesh (a floor no body reaches from a spawn is not counted)
        static const char* kKind[10] = {"", "stand", "crouch", "crawl", "drop", "climb", "ladder", "vault", "roll", "steps"};
        int linksAll = 0, linksLonger = 0, linksLost = 0, linksAway = 0;
        auto way = [](const doll::NavMesh& nav, const float* a, const float* b, float* len) {
            float na[3], nb[3]; std::vector<doll::Waypoint> w;
            return nav.Nearest(a, 700.f, na) && nav.Nearest(b, 700.f, nb) && nav.Path(na, nb, w, len) && !w.empty() && std::hypot(w.back().p[0] - nb[0], w.back().p[2] - nb[2]) < 300.f &&
                   std::fabs(w.back().p[1] - nb[1]) < 600.f;
        };
        for (int i = 0; i < before.LinkCount(); ++i) {
            float a[3], b[3], lb = 0, la = 0; uint8_t ar = 0;
            if (!before.LinkAt(i, a, b, &ar) || !way(before, a, b, &lb)) continue;
            ++linksAll;
            const bool now = way(after, a, b, &la);
            if (now && la <= lb + 5000.f && la <= 2.f * lb + 2000.f) continue;
            bool reached = false;
            for (const doll::Spawn* sp : spawns) {
                float s0[3], len = 0;
                if (nearest(before, sp->p, 2500.f, s0) <= 2500.f && way(before, s0, a, &len)) { reached = true; break; }
            }
            if (!reached) ++linksAway; else if (now) ++linksLonger; else ++linksLost;
            printf("    beta 25's %s %.0f %.0f %.0f -> %.0f %.0f %.0f: a way %.1f m before, %s%.1f m now; its start %s\n", kKind[ar < 10 ? ar : 0], a[0], a[1], a[2], b[0], b[1], b[2],
                   lb / 1000.f, now ? "" : "NONE ", la / 1000.f, reached ? "reached from a spawn" : "reached from no spawn: not counted");
        }
        printf("  beta 25's links %d: a way from start to end now as before but %d (%d longer, %d none; %d more whose start no spawn reaches)\n", linksAll, linksLonger + linksLost + linksAway,
               linksLonger, linksLost, linksAway);
        Check(linksLost == 0, "2f every link of beta 25's mesh whose start a body reaches from a spawn leads from its start to its end now (%d lost; %d longer)", linksLost, linksLonger);
    }

    // part 3: the bodies' own mesh (the switch as set): a body's way to each place beta 25's mesh holds too near a wall
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.weaponRule = WeaponRule; h.phase = Phase;
    vdoll::body::Init(h);
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 9000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    printf("  | %s\n", gMeshLine.c_str());
    int groups = 0, asked = 0, nearEnds = 0;
    const std::vector<int> grp = Group(sb.near, &groups);
    float nearestEnd = 1e9f;
    for (int k = 0; k < groups; ++k) {
        int bi = -1;
        for (size_t i = 0; i < sb.near.size(); ++i) if (grp[i] == k && (bi < 0 || sb.near[i].d < sb.near[bi].d)) bi = (int)i;
        const float* p = sb.near[bi].p;
        // from the nearest spawn (by the straight line) that has a way
        std::vector<std::pair<float, const doll::Spawn*>> order;
        for (const doll::Spawn& sp : g.spawns) order.push_back({std::hypot(sp.p[0] - p[0], sp.p[2] - p[2]) + std::fabs(sp.p[1] - p[1]) * 3.f, &sp});
        std::sort(order.begin(), order.end(), [](const std::pair<float, const doll::Spawn*>& a, const std::pair<float, const doll::Spawn*>& b) { return a.first < b.first; });
        for (size_t o = 0; o < order.size() && o < 6; ++o) {
            const float from[3] = {order[o].second->p[0], order[o].second->p[1], order[o].second->p[2]};
            const float to[3] = {p[0], p[1] + 800.f, p[2]};
            float len = 0, end[3];
            if (!vdoll::body::WayTo(from, to, &len, end)) continue;
            ++asked;
            const float d = WallDist(end, kBandLo, kBandHi);
            nearestEnd = std::min(nearestEnd, d);
            if (d < kPlayerGap) { ++nearEnds; if (nearEnds <= 8) printf("    a body's way to %.0f %.0f %.0f ends at %.0f %.0f %.0f, %.0f mm from a wall\n", p[0], p[1], p[2], end[0], end[1], end[2], d); }
            break;
        }
    }
    printf("  %d places of beta 25's mesh nearer than %.0f mm: %d ways of a body to them, %d ending nearer (the nearest end %.0f mm)\n", groups, kPlayerGap, asked, nearEnds,
           asked ? nearestEnd : 0.f);
    Check(nearEnds == 0, "3 no body's way ends nearer than %.0f mm to a wall (%d of %d)", kPlayerGap, nearEnds, asked);
    printf("%s: %d PASS, %d FAIL\n", argv[1], gPasses, gFails);
    return gFails ? 1 : 0;
}
