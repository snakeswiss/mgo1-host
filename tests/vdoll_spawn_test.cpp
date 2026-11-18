// (v150-host beta 19) where a virtual doll appears again during a round (docs/VIRTUAL_DOLL.md 22): the points of its team
// by the rule (doll_geom.cpp RespawnKinds, the host's VdollSpawnPoint), on each map's .geom, against the points the game
// itself gives a player of that team (traced on a playable doll's own game: 0x161200's codes):
// - CAPTURE: its respawns 2B16C2 .. (red) and 2B1AC2 .. (blue), never the round's start points by the goal the enemy
//   scores at (BCD39F .., BCD79F ..: beta 18's virtual dolls appeared there at every respawn);
// - team deathmatch, rescue, sneaking's Snake, team sneaking, base capture (when no base is held): tdm_red / tdm_blue;
// - (v150-host beta 21) SNEAKING's soldiers (red): the seven points of the stage script's list 1881 (0DC68A to 0DC690),
//   where the game's respawn command puts a soldier (its rand(8) into that list); never the eighth place the game reads
//   past the list's end (258BA6), never a point within 5 m of a start of Snake's (EDB68A ..) or of a goal (C74156, C74157);
// - deathmatch: dm; a spectator's view: the camera.
// For each map and side it prints the distances of the points taken to the goal the enemy scores at and to its own.
//     g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_spawn_test.cpp MGO1Loader/doll_geom.cpp -o /tmp/vspawn_test
//     /tmp/vspawn_test [<map.geom> ...]   (none: heliport, n18 and n21 under $MGO1_MAPS, the maps' .geom files made from your disc)
#include "doll_geom.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace doll;

static int gFail = 0, gChecks = 0;
static void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) { ++gFail; printf("FAIL %s\n", what.c_str()); }
}
static float D2(const float* a, const float* b) { return std::hypot(a[0] - b[0], a[2] - b[2]) / 1000.f; }

// the points the doll takes: the first kind of RespawnKinds the map has (as VdollSpawnPoint)
static std::vector<const Spawn*> Taken(const Geom& g, int rule, int team, SpawnKind* kind) {
    SpawnKind k[4];
    const int n = RespawnKinds(rule, team, k);
    std::vector<const Spawn*> c;
    for (int i = 0; i < n && c.empty(); ++i) {
        for (const Spawn& s : g.spawns) if (s.kind == k[i]) c.push_back(&s);
        if (!c.empty()) *kind = k[i];
    }
    return c;
}
static std::vector<const Spawn*> OfKind(const Geom& g, SpawnKind k) {
    std::vector<const Spawn*> c;
    for (const Spawn& s : g.spawns) if (s.kind == k) c.push_back(&s);
    return c;
}

// (v150-host beta 21) the points of a code family as the file holds them (chunk 7's records [instance][hi][lo][0], flags
// with bit 1, x y z), read apart from DecodeGeom: Snake's starts and the goals are no kind of its list
struct Raw { uint32_t hash; float p[3]; };
static std::vector<Raw> RawPoints(const std::vector<uint8_t>& b, uint32_t from, uint32_t to) {
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

static int Map(const char* path) {
    std::vector<uint8_t> b;
    if (FILE* f = fopen(path, "rb")) { int ch; while ((ch = fgetc(f)) != EOF) b.push_back((uint8_t)ch); fclose(f); }
    Geom g; std::string err;
    if (b.empty() || !DecodeGeom(b.data(), b.size(), g, err)) { printf("FAIL %s: not read (%s)\n", path, err.c_str()); ++gFail; return 1; }
    std::string name = path;
    for (int cut = 0, at = (int)name.size(); at > 0; ) {         // the folder and the file (two maps' files share a name)
        at = (int)name.rfind('/', at - 1);
        if (at < 0 || ++cut == 2) { name = name.substr(at + 1); break; }
    }
    // the rules with a respawn or a join during the round, each team
    static const char* kRule[] = {"deathmatch", "team deathmatch", "rescue", "capture", "sneaking", "team sneaking", "base capture"};
    for (int rule = 0; rule <= 6; ++rule)
        for (int team = 0; team <= 1; ++team) {
            SpawnKind k = SP_UNKNOWN;
            const auto c = Taken(g, rule, team, &k);
            SpawnKind want;
            if (rule == 0) want = SP_DM;
            else if (rule == 3) want = team ? SP_OBJECT_BLUE : SP_OBJECT_RED;
            else want = team ? SP_TDM_BLUE : SP_TDM_RED;
            if (rule == 4 && team == 0) {                             // (beta 21) SNEAKING's soldiers: the list's seven points
                const auto list = RawPoints(b, 0x0DC68A, 0x0DC690);
                if (list.empty()) {                                   // (a map without them: team deathmatch's, as before)
                    Check(c.empty() || k == SP_TDM_RED || k == SP_DM, name + " sneaking red: no soldiers' list, takes " + SpawnKindName(k));
                    continue;
                }
                bool all = k == SP_SNE_SOLDIER && c.size() == list.size() && c.size() <= 7;
                for (const Spawn* sp : c) if (sp->hash < 0x0DC68A || sp->hash > 0x0DC690) all = false;
                Check(all, name + " sneaking red: takes the " + std::to_string(list.size()) + " points of the soldiers' list (0DC68A to 0DC690), nothing else");
                const auto snake = RawPoints(b, 0xEDB68A, 0xEDB692), goals = RawPoints(b, 0xC74156, 0xC74157), eighth = RawPoints(b, 0x258BA6, 0x258BA6);
                float ds = 1e9f, dsx = 0, dg = 1e9f, d8s = 1e9f, d8g = 1e9f;
                for (const Spawn* sp : c) {
                    float one = 1e9f;
                    for (const Raw& r : snake) one = std::min(one, D2(sp->p, r.p));
                    ds = std::min(ds, one); dsx = std::max(dsx, one);
                    for (const Raw& r : goals) dg = std::min(dg, D2(sp->p, r.p));
                    for (const Raw& r : eighth) Check(D2(sp->p, r.p) > 0.5f || sp->hash != r.hash, name + " sneaking red: the eighth place (258BA6) taken");
                }
                for (const Raw& e : eighth) { for (const Raw& r : snake) d8s = std::min(d8s, D2(e.p, r.p)); for (const Raw& r : goals) d8g = std::min(d8g, D2(e.p, r.p)); }
                Check(snake.empty() || ds >= 5.f, name + " sneaking red: a point within 5 m of a start of Snake's");
                Check(goals.empty() || dg >= 5.f, name + " sneaking red: a point within 5 m of a goal");
                printf("  %-12s sneaking red: %zu points, %5.1f to %5.1f m from Snake's nearest start (%zu), %5.1f m at least from a goal (%zu); the eighth place left out: %5.1f m and %5.1f m\n",
                       name.c_str(), c.size(), snake.empty() ? -1.f : ds, snake.empty() ? -1.f : dsx, snake.size(), goals.empty() ? -1.f : dg, goals.size(),
                       eighth.empty() || snake.empty() ? -1.f : d8s, eighth.empty() || goals.empty() ? -1.f : d8g);
                continue;
            }
            if (OfKind(g, want).empty()) continue;                   // (a map without them: the next kind, not checked)
            Check(k == want && c.size() == OfKind(g, want).size(),
                  name + " " + kRule[rule] + (team ? " blue" : " red") + ": takes " + SpawnKindName(k) + ", the game's player " + SpawnKindName(want));
            if (rule == 3)                                            // never the round's start points (by the enemy's goal)
                for (const Spawn* s : c) Check(s->kind != (team ? SP_TCM_BLUE : SP_TCM_RED), name + " capture: a start point taken for a respawn");
        }
    {                                                             // a spectator's view: the camera only
        SpawnKind k = SP_UNKNOWN;
        const auto c = Taken(g, 3, 0xFE, &k);
        Check(c.empty() || k == SP_CAMERA, name + " spectator: takes " + SpawnKindName(k));
    }
    // the measure: CAPTURE, each side, the points taken against its round's start, to the goals
    const auto goals = OfKind(g, SP_TCM_GOAL);
    if (goals.size() == 2)
        for (int team = 0; team <= 1; ++team) {
            const auto st = OfKind(g, team ? SP_TCM_BLUE : SP_TCM_RED), en = OfKind(g, team ? SP_TCM_RED : SP_TCM_BLUE);
            if (st.empty() || en.empty()) continue;
            auto nearest = [&](const std::vector<const Spawn*>& set) {   // the goal by a start set: the one its enemy scores at
                float c[3] = {0, 0, 0};
                for (const Spawn* s : set) for (int i = 0; i < 3; ++i) c[i] += s->p[i] / set.size();
                return D2(goals[0]->p, c) <= D2(goals[1]->p, c) ? goals[0] : goals[1];
            };
            const Spawn* theirs = nearest(st);                       // the goal the enemy scores at (by this side's start)
            const Spawn* mine = nearest(en);                         // the one this side scores at
            SpawnKind k = SP_UNKNOWN;
            const auto c = Taken(g, 3, team, &k);
            auto line = [&](const char* what, const std::vector<const Spawn*>& set) {
                float lo = 1e9f, hi = 0, lo2 = 1e9f;
                for (const Spawn* s : set) { lo = std::min(lo, D2(s->p, theirs->p)); hi = std::max(hi, D2(s->p, theirs->p)); lo2 = std::min(lo2, D2(s->p, mine->p)); }
                printf("  %-12s capture %-4s %-12s: %zu points, %5.1f to %5.1f m from the goal the enemy scores at, %5.1f m at least from its own\n",
                       name.c_str(), team ? "blue" : "red", what, set.size(), lo, hi, lo2);
            };
            line("start", st);
            line(SpawnKindName(k), c);
        }
    return 0;
}

int main(int argc, char** argv) {
    std::vector<std::string> maps;
    for (int i = 1; i < argc; ++i) maps.push_back(argv[i]);
    if (maps.empty())
    {   // (build 1.00) the maps' folder from MGO1_MAPS (the .geom files made from your disc), no fixed path
        const char* dir = getenv("MGO1_MAPS");
        if (!dir) { fprintf(stderr, "usage: vspawn_test <map.geom> ... (or MGO1_MAPS=<maps folder>)\n"); return 2; }
        for (const char* m : {"heliport/f8cea7.geom", "n18/f8c6c7.geom", "n21/f8c6c7.geom"}) maps.push_back(std::string(dir) + "/" + m);
    }
    for (const auto& m : maps) Map(m.c_str());
    printf("%s: %d checks, %d failed (%zu maps)\n", gFail ? "FAIL" : "PASS", gChecks, gFail, maps.size());
    return gFail ? 1 : 0;
}
