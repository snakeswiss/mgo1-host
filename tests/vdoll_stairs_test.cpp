// (v150-host beta 24, snakehouse) A virtual doll that is SNEAKING's Snake stayed in Mountaintop's house to his death (a player's
// report on beta 23; the owner's logs of 14 and 15 November: 3 rounds of 3 where a virtual doll was Snake at the start 107687
// 77675 -225625, within 2 m of it until a soldier killed him 1.5 to 4.5 minutes later). That start (EDB68B) lies in a room 2 m
// under the ground (n022a's two sunken houses): a floor of 3 by 3 m, its stairs of blocks (1.0, 0.5 and 0.5 m high, 1 m wide)
// up through the doorway, whose panel (a wall face with 0x100000 in its attribute) the bodies' mesh held as a wall. The mesh
// made of the room an island of 3 polygons and the body never runs a stairs link: no way out (docs/VIRTUAL_DOLL.md 43).
// Off line on a map's .geom (MGO1Loader/vdoll_body.cpp StairsAsClimbs, doll_nav.cpp NavConfig::doorways), one map a run:
//   points  every point of the map where the game puts a player or an item (its spawns of every rule, SNEAKING's Snake's starts
//           EDB68A..EDB692, its films FA828A..FA828B, its goals C74156..C74157, read from the stage's chunk 7): the body's ways
//           can leave it (vdoll_body.h CanLeave: a way of 10 m or more the body runs, to one of 24 places within 40 m);
//   snake   (maps with Snake's starts) a doll that is Snake (SNEAKING, the blue side, class 8) at each of his starts, the films
//           and his goal where the game holds them, two soldiers standing at their starts far off: in 60 s it is 8 m or more
//           from where it started (out of any house), in 120 s 15 m or more (on its way to its goals or the enemies).
//   fences  (with MGO1_VDOLL_VAULT) each fence Snake crossed: its records as a real client's over a fence (V22_owner_both, n021a's
//           fence at 13465 4404 -71452 crossed by a playable doll): walking (06 20), 150 to 160 mm a record, no landing record.
// A point with no way out is an ISLAND when the bodies' mesh itself has no way from it to the rest (what this test fails on);
// when the mesh's way out needs a move the body does not make (a ladder, a fence crossed with the action button with the switch
// off, a climb over 1 m, a crouched or crawled passage) it is KNOWN; a film's, a goal's or an objective's place no way of the
// body leads into cannot keep it there (n002a's film up a ladder, Silo Entrance's goal under a low ceiling): printed, not held.
// Checks: 1 no start of Snake's, film or goal of SNEAKING KNOWN; 2 no start of Snake's on an island; 3 no film or goal of
// SNEAKING on an island; 4 Snake leaves every start (8 m in 60 s, 15 m in 120 s); 5 every fence crossed a real client's
// records; 6 Snake takes a film within 60 s from every start where the body's ways reach one (the host's part stood in for: a
// film given within 1.5 m, scored within 1.5 m of his goal); 7 (MGO1_VDOLL_ITEM_WAY) Snake at the foot of a film no way of his
// leads to (n002a's FA828B up a ladder) takes the other one, ten runs. Beta 23's code (MGO1_VDOLL_STAIRS=0 MGO1_VDOLL_VAULT=0
// MGO1_VDOLL_ITEM_WAY=0, or the test built on 3363fd4's sources) FAILS on n021a (Snake's start EDB68C behind a fence, the goal
// C74157 behind the doorway's panel), n022a (Snake's start EDB68B, the goal C74157), n024a (Snake's start EDB68B) and n002a
// (Snake at the foot of FA828B: a way elsewhere in every run), and passes on the 11 other maps; the fix passes on the 15.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_stairs_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_stairs_test
//   ./vdoll_stairs_test <map.geom>        (tests/vdoll_stairs_maps.sh: the 12 maps of the disc and the 3 added ones)
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
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0;
static int gDrops = 0, gClimbs = 0, gVaults = 0, gVaultBad = 0;
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
static int gRule = 4;
static int Rule() { return gRule; }
static int Phase() { return 2; }
static int PlayerClass(int id) { return id >= 0 && id < 16 && gHave[id] ? gPlayers[id].cls : -1; }
// (the records of the doll in hand on a player's channels: its places, its facings, its styles, by frame)
struct Rec { long long f; int kind; std::vector<uint8_t> r; };
static std::vector<Rec> gRecs;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) { if (slot == 1 && (kind == 0 || kind == 1) && !r.empty()) gRecs.push_back({gNow, kind, r}); }

// the points of a code family as the stage's chunk 7 holds them ([instance][hi][lo][0], flags with bit 1, x y z)
struct Raw { uint32_t hash; float p[3]; };
static std::vector<Raw> RawPoints(uint32_t from, uint32_t to) {
    const std::vector<uint8_t>& b = gGeom;
    std::vector<Raw> out;
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= b.size()) memcpy(&v, b.data() + o, 4); return v; };
    size_t c7 = 0, c7n = 0;
    for (uint32_t i = 0; i < u32(8) && i < 16; ++i) if (u32(16 + 12 * i + 8) == 7) { c7 = u32(16 + 12 * i + 4); c7n = u32(16 + 12 * i); }
    for (size_t o = c7; c7 && o + 24 <= c7 + c7n && o + 24 <= b.size(); o += 4) {
        const uint32_t h = u32(o);
        if (h < from || h > to || !(b[o + 4] & 2)) continue;
        Raw r; r.hash = h; memcpy(r.p, b.data() + o + 8, 12);
        if (std::fabs(r.p[0]) < 5e5f && std::fabs(r.p[1]) < 5e5f && std::fabs(r.p[2]) < 5e5f) out.push_back(r);
    }
    return out;
}
// SNEAKING's mission objects as the game holds them at a round's start: the two films lying, the goal of the blue side (Snake's)
static std::vector<Raw> gFilms, gGoals;
// (the host's part: a film is given to Snake within 1.5 m of it, carried with him, scored within 1.5 m of his goal)
static int gFilmHolder[2] = {-1, -1}, gFilmState[2] = {0, 0};
static int Objectives(vdoll::ObjNow* out, int max, float goals[2][3], bool set[2]) {
    int n = 0;
    for (size_t i = 0; i < gFilms.size() && i < 2; ++i) {
        if (n >= max) break;
        out[n] = vdoll::ObjNow(); memcpy(out[n].pos, gFilms[i].p, sizeof gFilms[i].p);
        out[n].holder = gFilmHolder[i]; out[n].state = gFilmState[i]; out[n].goalTeam = gFilmState[i] == 3 ? 1 : -1;
        if (gFilmHolder[i] >= 0 && gHave[gFilmHolder[i]]) memcpy(out[n].pos, gPlayers[gFilmHolder[i]].pos, sizeof out[n].pos);
        ++n;
    }
    for (int g = 0; g < 2; ++g) { goals[g][0] = goals[g][1] = goals[g][2] = 0.f; set[g] = false; }
    if (!gGoals.empty()) { memcpy(goals[1], gGoals[0].p, sizeof gGoals[0].p); set[1] = true; }
    return n;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_stairs_test <map.geom>\n"); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); gGeom.resize(ftell(fp)); fseek(fp, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), fp) != gGeom.size()) return 1;
    fclose(fp);
    doll::Geom g;
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("FAIL %s: not a geom (%s)\n", argv[1], err.c_str()); return 1; }
    std::string name = argv[1];
    for (int cut = 0, at = (int)name.size(); at > 0;) { at = (int)name.rfind('/', at - 1); if (at < 0 || ++cut == 2) { name = name.substr(at + 1); break; } }
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.phase = Phase; h.objectives = Objectives; h.playerClass = PlayerClass;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("FAIL %s: the bodies' mesh was not built\n", name.c_str()); return 1; }
    const char* sw = getenv("MGO1_VDOLL_STAIRS");
    printf("%s: MGO1_VDOLL_STAIRS %s, MGO1_VDOLL_VAULT %s, MGO1_VDOLL_ITEM_WAY %s\n", name.c_str(), sw ? sw : "unset (on)", getenv("MGO1_VDOLL_VAULT") ? getenv("MGO1_VDOLL_VAULT") : "unset (on)",
           getenv("MGO1_VDOLL_ITEM_WAY") ? getenv("MGO1_VDOLL_ITEM_WAY") : "unset (on)");
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("%s %s: %s\n", ok ? "PASS" : "FAIL", name.c_str(), what.c_str()); if (!ok) ++fails; };

    // ---- points ----
    const std::vector<Raw> snake = RawPoints(0xEDB68A, 0xEDB692);
    gFilms = RawPoints(0xFA828A, 0xFA828B);
    gGoals = RawPoints(0xC74156, 0xC74157);
    struct Pt { std::string family; uint32_t hash; float p[3]; };
    std::vector<Pt> pts;
    for (const doll::Spawn& sp : g.spawns) {
        if (sp.kind == doll::SP_CAMERA) continue;
        Pt q; q.family = sp.kind == doll::SP_UNKNOWN ? "sneaking soldier" : doll::SpawnKindName(sp.kind); q.hash = sp.hash; memcpy(q.p, sp.p, sizeof q.p);
        if (sp.kind == doll::SP_UNKNOWN && (sp.hash < 0x0DC68A || sp.hash > 0x0DC690)) continue;   // (the soldiers' list alone of SP_UNKNOWN)
        pts.push_back(q);
    }
    for (const Raw& r : snake) pts.push_back({"Snake's start", r.hash, {r.p[0], r.p[1], r.p[2]}});
    for (const Raw& r : gFilms) pts.push_back({"sneaking film", r.hash, {r.p[0], r.p[1], r.p[2]}});
    for (const Raw& r : gGoals) pts.push_back({"sneaking goal", r.hash, {r.p[0], r.p[1], r.p[2]}});
    // a point with no way out for the body: the mesh's own way from it to a hub (a spawn the body leaves), as the bodies' mesh is
    // built (the doorways with the switch on); none: an island (what this test fails on); one: the first link the body does not
    // run (a ladder, a fence crossed, a climb over 1 m, a crouched or crawled passage): KNOWN, the same on beta 23, printed
    doll::NavMesh nav; doll::NavConfig cfg; std::string navLog;
#ifdef DOLL_NAV_DOORWAYS
    cfg.doorways = !(sw && !strcmp(sw, "0"));
#endif
#ifdef DOLL_NAV_MARGIN
    { const char* mg = getenv("MGO1_VDOLL_MESH_MARGIN"); cfg.margin = !(mg && !strcmp(mg, "0")); }   // ((v150-host beta 25, meshmargin) as the bodies')
#endif
    const char* vw = getenv("MGO1_VDOLL_VAULT");
#ifdef DOLL_NAV_DOORWAYS
    const bool stairsRun = !(sw && !strcmp(sw, "0")), vaultRun = !(vw && !strcmp(vw, "0"));
#else
    const bool stairsRun = false, vaultRun = false;    // (beta 23's body: no stairs link run, no fence crossed)
#endif
    nav.Build(g, cfg, navLog);
    float hub[3] = {0, 0, 0}; bool haveHub = false;
    { int big = 0; for (int c = 0; c < nav.ComponentCount(); ++c) if (nav.ComponentSize(c) > nav.ComponentSize(big)) big = c;
      for (const doll::Spawn& sp : g.spawns) { float n[3]; if (sp.kind != doll::SP_CAMERA && nav.Nearest(sp.p, 600.f, n) && nav.ComponentAt(n) == big && vdoll::body::CanLeave(sp.p)) { memcpy(hub, n, sizeof hub); haveHub = true; break; } } }
    auto why = [&](const float* p) -> std::string {
        float a[3];
        std::vector<doll::Waypoint> w; float len = 0;
        if (!haveHub || !nav.Nearest(p, 2500.f, a) || !nav.Path(a, hub, w, &len) || w.size() < 2 || std::hypot(w.back().p[0] - hub[0], w.back().p[2] - hub[2]) > 500.f) return "";
        static const char* kName[] = {"?", "stand", "a crouched passage", "a crawled passage", "a drop", "a climb over 1 m", "a ladder", "a fence crossed with the action button (a vault)", "a roll", "a stairs of blocks"};
        for (size_t i = 0; i < w.size(); ++i) {
            const doll::Waypoint& q = w[i];
            if (!q.link) { if (q.area == doll::AREA_STAND || q.area == 0) continue; return kName[q.area < 10 ? q.area : 0]; }
            if (q.area == doll::AREA_DROP) continue;
            if (q.area == doll::AREA_CLIMB && i + 1 < w.size() && w[i + 1].p[1] - q.p[1] <= 1000.f) continue;
            if (q.area == doll::AREA_STEPS && stairsRun) continue;
            if (q.area == doll::AREA_VAULT && vaultRun && i + 1 < w.size() && w[i + 1].p[1] - q.p[1] <= 300.f) continue;   // (onto a floor 0.3 m higher at most)
            return kName[q.area < 10 ? q.area : 0];
        }
        return "a place held";
    };
    int snakeOut = 0, itemOut = 0, allOut = 0, known = 0, never = 0;
    struct Foot { int hash; float film[3], at[3]; };   // (a film no way of the body's leads to: the nearest place its ways reach)
    std::vector<Foot> feet;
    for (const Pt& q : pts) {
        if (vdoll::body::CanLeave(q.p)) continue;
        ++allOut;
        // (an item's or a goal's place: the body comes there only by its ways; none leads in, none can keep it there)
        if (q.family == "sneaking film" || q.family == "sneaking goal" || q.family == "kerotan" || q.family == "tcm_goal" || q.family == "cmplus_goal" ||
            q.family == "sr_target") {
            float len = 0, end[3];
            if (!haveHub || !vdoll::body::WayTo(hub, q.p, &len, end) || std::hypot(end[0] - q.p[0], end[2] - q.p[2]) > 1500.f) {
                ++never;
                // (a film: the foot of its way in, where the mesh's way from the hub meets the first move the body does not make)
                float a[3]; std::vector<doll::Waypoint> w; float wl = 0;
                if (q.family == "sneaking film" && haveHub && nav.Nearest(q.p, 2500.f, a) && nav.Path(hub, a, w, &wl))
                    for (size_t i = 0; i < w.size(); ++i) {
                        const doll::Waypoint& k = w[i];
                        if (!k.link || k.area == doll::AREA_DROP || (k.area == doll::AREA_CLIMB && i + 1 < w.size() && w[i + 1].p[1] - k.p[1] <= 1000.f) ||
                            (k.area == doll::AREA_STEPS && stairsRun) || (k.area == doll::AREA_VAULT && vaultRun && i + 1 < w.size() && w[i + 1].p[1] - k.p[1] <= 300.f)) continue;
                        Foot f; f.hash = q.hash; memcpy(f.film, q.p, sizeof f.film); memcpy(f.at, k.p, sizeof f.at);
                        if (vdoll::body::CanLeave(f.at)) feet.push_back(f);
                        break;
                    }
                printf("  no way out of %s %06X at %.0f %.0f %.0f, and none of the body's ways leads there (its way in needs %s): it cannot be kept there\n", q.family.c_str(),
                       q.hash, q.p[0], q.p[1], q.p[2], why(q.p).empty() ? "what the mesh does not have" : why(q.p).c_str());
                continue;
            }
        }
        const std::string w = why(q.p);
        if (!w.empty()) {
            if (q.family == "Snake's start" || q.family == "sneaking film" || q.family == "sneaking goal") ++known;
            printf("  KNOWN: %s %06X at %.0f %.0f %.0f: its way out needs %s, which the body does not make\n", q.family.c_str(), q.hash, q.p[0], q.p[1], q.p[2], w.c_str());
            continue;
        }
        if (q.family == "Snake's start") ++snakeOut;
        if (q.family == "sneaking film" || q.family == "sneaking goal") ++itemOut;
        printf("  no way out of %s %06X at %.0f %.0f %.0f (an island of the bodies' mesh)\n", q.family.c_str(), q.hash, q.p[0], q.p[1], q.p[2]);
    }
    printf("  %zu points (%zu Snake's starts, %zu films, %zu goals): %d with no way out for the body, %d of them by a move the body does not make, %d where its ways never lead\n",
           pts.size(), snake.size(), gFilms.size(), gGoals.size(), allOut, known, never);
    check(known == 0, "no start of Snake's, film or goal of SNEAKING that the body cannot leave for want of a player's move (" + std::to_string(known) + " KNOWN)");
    check(snakeOut == 0, "every start of Snake's can be left by the body's ways, or needs a move it does not make (" + std::to_string(snakeOut) + " of " + std::to_string(snake.size()) + " on an island)");
    check(itemOut == 0, "every film and goal of SNEAKING can be left by the body's ways, or needs a move it does not make (" + std::to_string(itemOut) + " of " + std::to_string(gFilms.size() + gGoals.size()) + " on an island)");

    // ---- (v150-host beta 24, snakehouse3) the doorways' panels and the lines: lines through each panel (25 points across it, from
    // 0.6 m before it to 0.6 m past it; a panel may run on behind a wall of the stage: n024a's), one of them at least clear on the
    // bodies' mesh with MGO1_VDOLL_STAIRS (a player walks through the panel, and his
    // bullets and the game's auto aim pass it: snakehouse_door2, two real clients either side of n022a's house door). Snakehouse
    // kept the panels for the lines: none clear (FAIL); the switch off holds them as walls, as beta 23 (printed) ----
    {
        int panels = 0, clear = 0;
        for (const doll::Face& f : g.faces) {
            if (!(f.attr & doll::kDoorway) || f.n[1] >= 0.7f) continue;
            float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (int k = 0; k < 4; ++k) for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], f.v[k][i]); hi[i] = std::max(hi[i], f.v[k][i]); }
            const float c[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
            bool one = false;
            for (int u = 0; u < 5 && !one; ++u)
                for (int v = 0; v < 5 && !one; ++v) {
                    const float fu = 0.1f + 0.2f * u, fv = 0.1f + 0.2f * v;
                    const float q[3] = {lo[0] + (hi[0] - lo[0]) * fu, lo[1] + (hi[1] - lo[1]) * fv, lo[2] + (hi[2] - lo[2]) * fu};
                    const float a[3] = {q[0] + f.n[0] * 600.f, q[1], q[2] + f.n[2] * 600.f}, b[3] = {q[0] - f.n[0] * 600.f, q[1], q[2] - f.n[2] * 600.f};
                    one = nav.Clear(a, b);
                }
            ++panels;
            if (one) ++clear;
            else printf("  every line through the doorway's panel at %.0f %.0f %.0f (%.0f m wide) stopped\n", c[0], c[1], c[2], std::hypot(hi[0] - lo[0], hi[2] - lo[2]) / 1000.f);
        }
        if (panels) {
            printf("  the doorways' panels: %d, a line through %d of them clear (MGO1_VDOLL_STAIRS %s)\n", panels, clear, stairsRun ? "on" : "off");
            if (stairsRun) check(clear == panels, "a line through each doorway's panel is clear, as a player's bullet and auto aim pass it (" + std::to_string(panels - clear) + " of " + std::to_string(panels) + " stopped)");
        }
    }

    // ---- snake ----
    std::vector<const doll::Spawn*> soldiers;
    for (const doll::Spawn& sp : g.spawns) if (sp.kind == doll::SP_UNKNOWN && sp.hash >= 0x0DC68A && sp.hash <= 0x0DC690) soldiers.push_back(&sp);
    if (soldiers.empty()) for (const doll::Spawn& sp : g.spawns) if (sp.kind == doll::SP_TDM_RED) soldiers.push_back(&sp);
    long long fr = 600;
    float pos[3];
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 1; s.pos = pos; s.facing = &facing; s.send = Send;
    int stays = 0, noFilm = 0;
    for (size_t si = 0; si < snake.size(); ++si) {
        const float start[3] = {snake[si].p[0], snake[si].p[1] + 800.f, snake[si].p[2]};
        memcpy(pos, start, sizeof pos);
        facing = (uint16_t)(si * 21845);
        s.life = fr;
        gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 1; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 440; gPlayers[1].cls = 8;
        for (int e = 0; e < 2; ++e) {                  // (two soldiers standing at their starts, the nearest ones over 30 m off)
            const doll::Spawn* best = nullptr; float bd = 0.f;
            for (const doll::Spawn* sp : soldiers) {
                const float d = std::hypot(sp->p[0] - start[0], sp->p[2] - start[2]);
                bool taken = false;
                for (int o = 0; o < e; ++o) if (std::hypot(gPlayers[2 + o].pos[0] - sp->p[0], gPlayers[2 + o].pos[2] - sp->p[2]) < 100.f) taken = true;
                if (taken || d < 30000.f) continue;
                if (!best || d < bd) { best = sp; bd = d; }
            }
            gHave[2 + e] = best != nullptr;
            if (!best) continue;
            gPlayers[2 + e] = vdoll::PlayerNow(); gPlayers[2 + e].team = 0; gPlayers[2 + e].object = gPlayers[2 + e].alive = true; gPlayers[2 + e].life = 180; gPlayers[2 + e].cls = 1;
            gPlayers[2 + e].pos[0] = best->p[0]; gPlayers[2 + e].pos[1] = best->p[1] + 800.f; gPlayers[2 + e].pos[2] = best->p[2];
        }
        float far60 = 0.f, far120 = 0.f;
        long long out8 = -1, took = -1, scored = -1;
        gFilmHolder[0] = gFilmHolder[1] = -1; gFilmState[0] = gFilmState[1] = 0;
        bool filmReach = false;                        // (a film the body's ways reach from this start)
        gRecs.clear();
        std::vector<long long> vaultAt;                // (the ticks a fence crossing began: its body line's count went up)
        int fences = -1;
        for (const Raw& f : gFilms) { float len = 0, end[3]; const float a[3] = {start[0], start[1] - 800.f, start[2]}; if (vdoll::body::WayTo(a, f.p, &len, end) && std::hypot(end[0] - f.p[0], end[2] - f.p[2]) < 1500.f) filmReach = true; }
        for (long long t = fr; t < fr + 7200; ++t) {
            gNow = t;
            memcpy(gPlayers[1].pos, pos, sizeof pos);
            vdoll::body::Frame(t);
            s.tick = t % 6 == 0;
            vdoll::body::Alive(s, t);
            const float d = std::hypot(pos[0] - start[0], pos[2] - start[2]);
            if (t - fr < 3600) far60 = std::max(far60, d);
            far120 = std::max(far120, d);
            if (out8 < 0 && d >= 8000.f) out8 = t - fr;
            if (s.tick) {
                const std::string st = vdoll::body::Status(0);
                const char* fc = strstr(st.c_str(), " climbs, ");
                const int n = fc ? atoi(fc + 9) : 0;
                if (fences >= 0 && n > fences) vaultAt.push_back(t);
                fences = n;
            }
            for (size_t i = 0; i < gFilms.size() && i < 2; ++i) {
                if (gFilmState[i] == 0 && gFilmHolder[0] != 1 && gFilmHolder[1] != 1 && std::hypot(pos[0] - gFilms[i].p[0], pos[2] - gFilms[i].p[2]) < 1500.f &&
                    std::fabs(pos[1] - 800.f - gFilms[i].p[1]) < 1500.f) { gFilmHolder[i] = 1; gFilmState[i] = 1; if (took < 0) took = t - fr; }
                if (gFilmHolder[i] == 1 && !gGoals.empty() && std::hypot(pos[0] - gGoals[0].p[0], pos[2] - gGoals[0].p[2]) < 1500.f &&
                    std::fabs(pos[1] - 800.f - gGoals[0].p[1]) < 1500.f) { gFilmHolder[i] = -1; gFilmState[i] = 3; if (scored < 0) scored = t - fr; }
            }
        }
        const std::string st = vdoll::body::Status(0);      // (its counts since the test began: this run's, the last's taken off)
        const char* c = strstr(st.c_str(), " drops, ");
        const char* dr = c;
        while (dr && dr > st.c_str() && dr[-1] != ' ') --dr;
        const int drops = dr ? atoi(dr) - gDrops : -1, climbs = c ? atoi(c + 8) - gClimbs : -1;
        if (dr) gDrops += drops;
        if (c) gClimbs += climbs;
        printf("  Snake at his start %06X (%.0f %.0f %.0f): 8 m away after %s, the farthest %.1f m in 60 s and %.1f m in 120 s; %d drops, %d climbs; now at %.0f %.0f %.0f\n",
               snake[si].hash, snake[si].p[0], snake[si].p[1], snake[si].p[2], out8 < 0 ? "never" : (std::to_string(out8 / 60.0).substr(0, 5) + " s").c_str(), far60 / 1000.f,
               far120 / 1000.f, drops, climbs, pos[0], pos[1], pos[2]);
        // (each fence crossed: its records as a real client's over that fence, V22_owner_both: its places 150 to 160 mm a record,
        // walking (06 20), its facings on, no landing record {01 0 0 0 0 0 0})
        for (long long v : vaultAt) {
            // (the ticks of the crossing: those whose style record says walking; between two such ticks the place's step)
            struct Tk { long long f; float p[3]; bool pos = false; int style = -1; };
            std::vector<Tk> tk;
            int lands = 0;
            for (const Rec& r : gRecs) {
                if (r.f < v - 6 || r.f > v + 60) continue;
                if (tk.empty() || tk.back().f != r.f) { tk.push_back(Tk()); tk.back().f = r.f; }
                if (r.kind == 0 && r.r[0] == 0x00 && r.r.size() >= 13) { memcpy(tk.back().p, &r.r[1], 12); tk.back().pos = true; }
                if (r.kind == 1 && r.r[0] == 0x06 && r.r.size() >= 2) tk.back().style = r.r[1];
                if (r.kind == 0 && r.r.size() == 7 && r.r[0] == 0x01 && !r.r[1] && !r.r[2] && !r.r[3] && !r.r[4]) ++lands;
            }
            int walks = 0; float stepMax = 0;
            for (size_t i = 0; i < tk.size(); ++i) {
                if (tk[i].style != 0x20) continue;
                ++walks;
                if (i > 0 && tk[i - 1].style == 0x20 && tk[i].pos && tk[i - 1].pos) stepMax = std::max(stepMax, std::hypot(tk[i].p[0] - tk[i - 1].p[0], tk[i].p[2] - tk[i - 1].p[2]));
            }
            ++gVaults;
            if (stepMax > 200.f || walks < 3 || lands > 0) { ++gVaultBad; printf("    a fence crossed at frame %lld: %d walking records (the widest step between two %.0f mm), landing records %d: NOT a player's\n", v, walks, stepMax, lands); }
            else if (gVaults <= 3) printf("    a fence crossed at frame %lld: %d walking records, the widest step between two %.0f mm, no landing record (a real client's: 150 to 160 mm, walking)\n", v, walks, stepMax);
        }
        printf("    a film %s, taken %s, scored at his goal %s\n", filmReach ? "reached by the body's ways from there" : "out of the body's ways from there",
               took < 0 ? "never" : (std::to_string(took / 60.0).substr(0, 5) + " s in").c_str(), scored < 0 ? "never" : (std::to_string(scored / 60.0).substr(0, 5) + " s in").c_str());
        if (filmReach && (took < 0 || took > 3600)) ++noFilm;
        const std::string w = vdoll::body::CanLeave(snake[si].p) ? std::string() : why(snake[si].p);
        if (!w.empty()) printf("  KNOWN: that start's way out needs %s, which the body does not make: not counted\n", w.c_str());
        else if (far60 < 8000.f || far120 < 15000.f) ++stays;
        vdoll::body::Down(0);
        fr += 7200 + 60;
    }
    if (!snake.empty()) check(stays == 0, "Snake leaves every start of his (8 m in 60 s, 15 m in 120 s): " + std::to_string(stays) + " of " + std::to_string(snake.size()) + " starts kept him");
    if (gVaults) check(gVaultBad == 0, "every fence it crossed sent a real client's records (" + std::to_string(gVaultBad) + " of " + std::to_string(gVaults) + " not)");
    if (!snake.empty() && !gFilms.empty())
        check(noFilm == 0, "Snake takes a film within 60 s from every start where the body's ways reach one: " + std::to_string(noFilm) + " of " + std::to_string(snake.size()) + " starts did not");
    // ---- (v150-host beta 24, snakehouse, MGO1_VDOLL_ITEM_WAY) Snake at the foot of a film no way of his leads to (n002a's
    // FA828B on its tower, up a ladder), no enemy, ten times (ten facings): that film the nearest, his ways go to the other
    // one and he takes it within 60 s each time, with at most 5 ways elsewhere in all (level 10's draw of a way not to the
    // objective, 1 in 10: 0 to 4 in 10 runs on 4 seeds). Beta 23's doll: its way to the nearest refused, a way elsewhere in every
    // run (11 to 16 in 10 runs), the other film its objective only once a way of chance has taken it nearer ----
    int footFails = 0;
    for (const Foot& f : feet) {
        int took30 = 0, elsewhere = 0, other = 0;
        double tookSum = 0;
        for (int run = 0; run < 10; ++run) {
            const float start[3] = {f.at[0], f.at[1] + 800.f, f.at[2]};
            memcpy(pos, start, sizeof pos);
            facing = (uint16_t)(run * 6553);
            s.life = fr;
            gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 1; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 440; gPlayers[1].cls = 8;
            gHave[2] = gHave[3] = false;
            gFilmHolder[0] = gFilmHolder[1] = -1; gFilmState[0] = gFilmState[1] = 0;
            auto ways = []() {                         // (its random ways' count, from its body line: kept over its lives)
                const std::string st = vdoll::body::Status(0);
                const char* l = strstr(st.c_str(), ", running, "); if (!l) l = strstr(st.c_str(), ", still, ");
                if (!l) l = strstr(st.c_str(), ", aiming, "); if (!l) l = strstr(st.c_str(), ", reacting, ");
                return l ? atoi(strchr(l + 2, ',') + 2) : 0;
            };
            const int base = ways();
            long long took = -1;
            int at = 0;
            for (long long t = fr; t < fr + 3600 && took < 0; ++t) {
                gNow = t;
                memcpy(gPlayers[1].pos, pos, sizeof pos);
                vdoll::body::Frame(t);
                s.tick = t % 6 == 0;
                vdoll::body::Alive(s, t);
                for (size_t i = 0; i < gFilms.size() && i < 2; ++i)
                    if (gFilmState[i] == 0 && took < 0 && std::hypot(pos[0] - gFilms[i].p[0], pos[2] - gFilms[i].p[2]) < 1500.f && std::fabs(pos[1] - 800.f - gFilms[i].p[1]) < 1500.f) {
                        gFilmHolder[i] = 1; gFilmState[i] = 1; took = t - fr; at = gFilms[i].hash;
                    }
            }
            elsewhere += ways() - base;
            if (took >= 0) { ++took30; tookSum += took / 60.0; if (at != f.hash) ++other; }
            vdoll::body::Down(0);
            fr += 3600 + 60;
        }
        const bool ok = took30 == 10 && elsewhere <= 5;
        if (!ok) ++footFails;
        printf("  Snake at the foot of the film %06X (%.0f %.0f %.0f, the film %.1f m up), 10 times: a film taken within 60 s %d times (%d the other one, %.1f s on average), %d ways elsewhere before it\n",
               f.hash, f.at[0], f.at[1], f.at[2], (f.film[1] - f.at[1]) / 1000.f, took30, other, took30 ? tookSum / took30 : 0.0, elsewhere);
    }
    if (!feet.empty()) check(footFails == 0, "Snake at the foot of a film no way of his leads to takes the other one within 60 s each time, at most 5 ways elsewhere in 10 runs (" + std::to_string(footFails) + " of " + std::to_string(feet.size()) + " not)");
    printf("%s %s: %d checks failed\n", fails ? "FAIL" : "PASS", name.c_str(), fails);
    return fails ? 1 : 0;
}
