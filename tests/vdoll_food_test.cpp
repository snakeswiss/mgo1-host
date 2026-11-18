// (v150-host beta 25, food) A virtual doll and the food lying on the map (MGO1Loader/vdoll_body.cpp FoodFrame, vdoll_food.h,
// docs/VIRTUAL_DOLL.md 44), off line on a map's .geom: the map's own food points (the stage's chunk 7, by hash: the mushrooms'
// points FC8C5C + k, the box's point beside each (k << 20) | 0x008FBA, k 0 to 8, those the map has), a stand-in of the host's
// mushroom man (Host::foods: each a GOLOVA lying, its gain 13500), one doll whose stun gauge the test gives as vdoll.cpp does
// (body::Knocked each frame). Checked:
//   rule   vdoll_food.h: GOLOVA (class 3) 13500, the RUSSIAN GLOWCAP (class 1) 4500, a RATION (class 2) 9000, clamped at 30000;
//          the taken record {the number's low byte, the point in tenths cut toward zero, 3}; the zone 400 mm along x and z
//   eat    its gauge at 10000, started 6 to 14 m from a food box in its sight: it goes and takes it within 20 s: one record on the
//          mushroom man's channel (Self::send kind 6) {the entry's number's low byte, its point in tenths, 3}, its hips in the
//          game's pick-up zone then; FoodEaten gives the food's 13500 once (vdoll.cpp adds it to its gauge)
//   full   its gauge full (30000), the same starts: it never goes to the box (body::Food's "to" stays -1)
//   foe    its gauge at 10000, an enemy in its sight 8 to 14 m away: it never goes to the box
//   zone   its gauge full, standing in a box's zone: it takes it at once (the game takes a box whatever the gauge)
//   gone   the box taken by a player while it goes: it leaves it, no record
//   off    (MGO1_VDOLL_FOOD=0: run the test again with it) nothing taken, nothing walked to
//   wake   ((v150-host beta 25, review25) going to a food, a teammate reading a book 1.2 to 1.8 m away, friendly fire on: the food
//          left, he is thrown free (the CQC throw) within 480 frames, never "no way to him"; 65673d2's body fails it on every map)
// Beta 24's body (no food: built from f1ca99e) fails eat and zone on every map.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_food_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_food_test
//   ./vdoll_food_test <map.geom>        (VDOLL_TEST_VERBOSE=1 the body's log)
#include "doll_geom.h"
#include "vdoll_body.h"
#if __has_include("vdoll_food.h")
#include "vdoll_food.h"
#endif
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
static std::vector<std::string> gLines;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    gLines.push_back(b);
    if (getenv("VDOLL_TEST_VERBOSE")) printf("  | %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }
static int WeaponRule() { return 0; }
static int Phase() { return 2; }
static int gFF = -1;                                     // ((v150-host beta 25, review25) the game's friendly fire: part wake on, else not known)
static int FriendlyFire() { return gFF; }

struct Point { int k; float at[3], box[3]; };   // (a mushroom of the map: its point, its box's place beside it)
static const int kGolova = 13500;
const float kReach = 100.f;                      // (the food box's reach, +0xD0: 100 in a real client's game, probe 48 (food_p1))
#ifdef VDOLL_HOST_FOODS
static std::vector<vdoll::FoodNow> gFoods;
static int Foods(vdoll::FoodNow* out, int max) { int n = 0; for (const auto& f : gFoods) if (n < max) out[n++] = f; return n; }
static vdoll::FoodNow FoodAt(const Point& p) {
    vdoll::FoodNow f;
    f.number = 0x100 + p.k; f.state = 2; f.code = 0x5B; f.gain = kGolova; f.timer = 0;
    memcpy(f.at, p.at, sizeof f.at); memcpy(f.pos, p.box, sizeof f.pos); f.box = true; f.reach = kReach;
    return f;
}
#endif

struct Rec { long long frame; int slot, kind; std::vector<uint8_t> r; };
static std::vector<Rec> gRecs;
static long long gNow = 0;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gRecs.push_back({gNow, slot, kind, rec}); }

static int gFails = 0, gPasses = 0;
static void Check(bool ok, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("%s ", ok ? "PASS" : "FAIL");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (ok) ++gPasses; else ++gFails;
}
static int S16(const std::vector<uint8_t>& r, size_t at) { return (int16_t)(r[at] | r[at + 1] << 8); }

static bool WayTo(const float* from, const float* to, float* len, float* end) { return vdoll::body::WayTo(from, to, len, end); }
// one run of the doll from a place for n frames with that gauge; the records kept
struct Run { long long take = -1; std::vector<uint8_t> rec; float hips[3] = {0, 0, 0}; int gained = 0; bool walked = false; int to = -1; };
static long long gFrameAt = 600;
static int gLife = 0;
static Run RunDoll(float* pos, uint16_t* facing, int frames, int gauge, int takeAfter = -1) {
    Run r;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = facing; s.send = Send;
    s.life = ++gLife;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
    for (long long t = gFrameAt; t < gFrameAt + frames; ++t) {
        gNow = t;
        if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
        const size_t r0 = gRecs.size();
        vdoll::body::Frame(t);
        vdoll::body::Knocked(0, 1, false, gauge, 144);   // (vdoll.cpp: its gauge each frame)
        s.tick = t % 6 == 0;
        vdoll::body::Alive(s, t);
#ifdef VDOLL_BODY_FOOD
        if (const int g = vdoll::body::FoodEaten(0)) { r.gained += g; gauge = std::min(30000, gauge + g); }
        vdoll::body::FoodInfo fi;
        if (vdoll::body::Food(0, &fi) && fi.to >= 0) r.to = fi.to;
#endif
        for (size_t i = r0; i < gRecs.size(); ++i) {
            const Rec& x = gRecs[i];
            if (x.kind != 6) continue;
            if (r.take < 0) { r.take = t; r.rec = x.r; memcpy(r.hips, pos, sizeof r.hips); }
#ifdef VDOLL_HOST_FOODS
            for (auto& f : gFoods) if ((f.number & 0xFF) == x.r[0]) { f.state = 3; f.box = false; }   // (the host's game: taken, the box gone)
#endif
        }
#ifdef VDOLL_HOST_FOODS
        if (takeAfter >= 0 && t == gFrameAt + takeAfter) for (auto& f : gFoods) { f.state = 3; f.box = false; }   // (a player took it)
#endif
    }
    gFrameAt += frames + 600;
    for (const std::string& l : gLines) if (l.find("goes to eat the") != std::string::npos) r.walked = true;
    return r;
}
static void Settle(long long frames) {
    for (long long t = gFrameAt; t < gFrameAt + frames; t += 2) { gNow = t; vdoll::body::Frame(t); }
    vdoll::body::Down(0);
    gFrameAt += frames;
}
// a player put where the doll sees him: r from its place, within 130 degrees of its facing, his chest and head in its eye's line
static bool PlaceFoe(int id, const float* at, uint16_t facing, const std::vector<float>& radii) {
    const float yaw0 = facing / 65536.f * 6.2831853f;
    for (float r : radii)
        for (int j = 0; j < 27; ++j) {
            const float yaw = yaw0 + (j & 1 ? -1.f : 1.f) * (float)((j + 1) / 2) * 0.1745f;
            float e[3] = {at[0] + std::sin(yaw) * r, at[1], at[2] + std::cos(yaw) * r};
            const float eye[3] = {at[0], at[1] + 750.f, at[2]}, chest[3] = {e[0], e[1] + 350.f, e[2]}, head[3] = {e[0], e[1] + 850.f, e[2]};
            if (vdoll::body::SightStopped(eye, chest) || vdoll::body::SightStopped(eye, head)) continue;
            gHave[id] = true; gPlayers[id] = vdoll::PlayerNow(); gPlayers[id].team = 1; gPlayers[id].object = gPlayers[id].alive = true; gPlayers[id].life = 200;
            memcpy(gPlayers[id].pos, e, sizeof e);
            return true;
        }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_food_test <map.geom>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 1;
    fclose(f);
    const bool off = getenv("MGO1_VDOLL_FOOD") && !strcmp(getenv("MGO1_VDOLL_FOOD"), "0");

    // the rule's numbers (vdoll_food.h)
#ifdef VDOLL_BODY_FOOD
    {
        namespace vf = vdoll::food;
        const float at[3] = {14375.f, 4000.f, -16500.f}, neg[3] = {-16875.5f, -7.f, -17625.f};
        const std::vector<uint8_t> r = vf::TakenRecord(0x105, at), q = vf::TakenRecord(0x103, neg);
        Check(vf::Gain(3) == 13500 && vf::Gain(1) == 4500 && vf::Gain(2) == 9000 && vf::Gain(4) == 30000 && vf::After(20000, 13500) == 30000 && vf::After(10000, 13500) == 23500,
              "rule: GOLOVA 13500, the RUSSIAN GLOWCAP 4500, a RATION 9000, clamped at 30000");
        Check(r.size() == 8 && r[0] == 0x05 && S16(r, 1) == 1437 && S16(r, 3) == 400 && S16(r, 5) == -1650 && r[7] == 3 && S16(q, 1) == -1687 && S16(q, 3) == 0,
              "rule: the taken record {the number's low byte, the point in tenths cut toward zero, 3}");
        const float in[3] = {14700.f, 4800.f, -16200.f}, out[3] = {14800.f, 4800.f, -16500.f};
        Check(vf::InZone(in, at, 0.f) && !vf::InZone(out, at, 0.f) && vf::Wants(10000, 13500) && vf::Wants(23250, 13500) && !vf::Wants(23251, 13500) && !vf::Wants(30000, 4500),
              "rule: the zone 400 mm along x and z; a doll goes when it gets back half of the gain or more");
    }
#else
    Check(false, "rule: no food rule in this body (vdoll_food.h)");
#endif

    doll::Geom g;
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    // the map's mushrooms: each point and its box's beside it (32 bytes each: the hash, a word, x y z, 1)
    std::vector<Point> points;
    auto find = [&](uint32_t h, float* p) {
        for (size_t i = 0; i + 24 <= gGeom.size(); i += 4) {
            uint32_t w; memcpy(&w, &gGeom[i], 4);
            if (w != h) continue;
            float q[3]; memcpy(q, &gGeom[i + 8], 12);
            if (std::fabs(q[0]) < 1e6f && std::fabs(q[2]) < 1e6f) { memcpy(p, q, sizeof q); return true; }
        }
        return false;
    };
    for (int k = 0; k < 9; ++k) {
        Point p; p.k = k;
        if (!find(0xFC8C5C + k, p.at) || !find(((uint32_t)k << 20) | 0x008FBA, p.box)) continue;
        p.box[1] += 60.f;                                // (the box's place on the floor: 60 over the point, as the game's boxes lie)
        points.push_back(p);
    }
    printf("%s: %zu food points\n", argv[1], points.size());
    if (points.empty()) { printf("no food point on this map\n"); return 1; }
    std::vector<const doll::Spawn*> spawns;
    std::vector<doll::Spawn> ring;                   // (more starts: places on the bodies' floors 7 to 13 m around each food, below)
    for (const doll::Spawn& sp : g.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE || sp.kind == doll::SP_TDM_FIRST_RED || sp.kind == doll::SP_TDM_FIRST_BLUE ||
            sp.kind == doll::SP_TCM_RED || sp.kind == doll::SP_TCM_BLUE || sp.kind == doll::SP_OBJECT_RED || sp.kind == doll::SP_OBJECT_BLUE) spawns.push_back(&sp);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    if (!getenv("MGO1_VDOLL_WEAPONS")) setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    setenv("MGO1_VDOLL_PICKUP", "0", 1);                // (the special items left out: food only)
    setenv("MGO1_VDOLL_SUPPORT", "0", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.weaponRule = WeaponRule; h.phase = Phase;
#ifdef VDOLL_HOST_FOODS
    h.foods = Foods;
#endif
    h.friendlyFire = FriendlyFire;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }

    // (the starts: the map's spawn points, and places of the bodies' floors on rings 7 to 13 m around each food: a way from the box
    // ends there)
    ring.reserve(points.size() * 96);
    for (const Point& p : points)
        for (float r : {7000.f, 9000.f, 11000.f, 13000.f})
            for (int j = 0; j < 24; ++j) {
                const float a = j * 0.2618f, c[3] = {p.box[0] + std::sin(a) * r, p.box[1] - 60.f, p.box[2] + std::cos(a) * r};
                const float from[3] = {p.box[0], p.box[1] - 60.f, p.box[2]};
                float len = 0, end[3];
                if (!WayTo(from, c, &len, end) || std::hypot(end[0] - c[0], end[2] - c[2]) > 500.f || std::fabs(end[1] - c[1]) > 2500.f) continue;
                doll::Spawn sp; sp.hash = 0; sp.kind = doll::SP_UNKNOWN; memcpy(sp.p, end, sizeof sp.p);
                ring.push_back(sp);
            }
    for (const doll::Spawn& sp : ring) spawns.push_back(&sp);
    // the cases: for each food a start 6 to 14 m away, the box in its eye's line, a way the bodies run (2 at most a food)
    struct Case { const Point* p; const doll::Spawn* sp; };
    std::vector<Case> near;
    for (const Point& p : points) {
        int n = 0;
        for (const doll::Spawn* sp : spawns) {
            const float d = std::hypot(sp->p[0] - p.box[0], sp->p[2] - p.box[2]);
            const float eye[3] = {sp->p[0], sp->p[1] + 1550.f, sp->p[2]}, box[3] = {p.box[0], p.box[1] + 300.f, p.box[2]};
            if (d < 6000.f || d > 14000.f || std::fabs(sp->p[1] - p.box[1]) >= 2500.f || vdoll::body::SightStopped(eye, box) || n >= 2) continue;
            float len = 0, end[3];
            if (!WayTo(sp->p, p.box, &len, end) || len > d * 1.5f + 3000.f || std::hypot(end[0] - p.box[0], end[2] - p.box[2]) > 1000.f || std::fabs(end[1] - p.box[1]) > 600.f) continue;
            near.push_back({&p, sp}); ++n;
        }
    }
    if (getenv("VDOLL_TEST_VERBOSE"))
        for (const Point& p : points) {
            int a = 0, b = 0, c = 0, d2 = 0;
            for (const doll::Spawn* sp : spawns) {
                const float d = std::hypot(sp->p[0] - p.box[0], sp->p[2] - p.box[2]);
                if (d < 6000.f || d > 14000.f) continue; ++a;
                if (std::fabs(sp->p[1] - p.box[1]) >= 2500.f) continue; ++b;
                const float eye[3] = {sp->p[0], sp->p[1] + 1550.f, sp->p[2]}, box[3] = {p.box[0], p.box[1] + 300.f, p.box[2]};
                if (vdoll::body::SightStopped(eye, box)) continue; ++c;
                float len = 0, end[3];
                if (!WayTo(sp->p, p.box, &len, end)) continue; ++d2;
            }
            printf("  food %d: %d starts 6 to 14 m, %d at its height, %d in sight, %d with a way\n", p.k, a, b, c, d2);
        }
    printf("%zu cases\n", near.size());
    Check(!near.empty(), "a food box with a spawn point 6 to 14 m away in its sight on this map");

    // eat: its gauge at 10000
    int eaten = 0, recOk = 0, zoneOk = 0, gainOk = 0, walkedAny = 0;
    for (const Case& c : near) {
#ifdef VDOLL_HOST_FOODS
        gFoods.assign(1, FoodAt(*c.p));
#endif
        float pos[3] = {c.sp->p[0], c.sp->p[1] + 800.f, c.sp->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(c.p->box[0] - pos[0], c.p->box[2] - pos[2]) / 6.2831853f * 65536.f);
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 1200, 10000);
        if (r.walked) ++walkedAny;
        if (r.take >= 0) {
            ++eaten;
            const std::vector<uint8_t>& a = r.rec;
            if (a.size() == 8 && a[0] == ((0x100 + c.p->k) & 0xFF) && S16(a, 1) == (int)(c.p->at[0] * 0.1f) && S16(a, 3) == (int)(c.p->at[1] * 0.1f) && S16(a, 5) == (int)(c.p->at[2] * 0.1f) && a[7] == 3) ++recOk;
            else { printf("  its record:"); for (uint8_t x : a) printf(" %02x", x); printf("\n"); }
            if (std::fabs(r.hips[0] - c.p->box[0]) < 400.f && std::fabs(r.hips[2] - c.p->box[2]) < 400.f) ++zoneOk;
            if (r.gained == kGolova) ++gainOk;
        } else printf("  the food at %.0f %.0f %.0f not taken from %.0f %.0f %.0f (%.1f m)\n", c.p->box[0], c.p->box[1], c.p->box[2], c.sp->p[0], c.sp->p[1], c.sp->p[2],
                      std::hypot(c.sp->p[0] - c.p->box[0], c.sp->p[2] - c.p->box[2]) / 1000.f);
        Settle(120);
    }
    const int tried = (int)near.size();
    if (off) Check(eaten == 0 && walkedAny == 0, "off: MGO1_VDOLL_FOOD=0, no food taken nor walked to (%d taken, %d walks of %d runs)", eaten, walkedAny, tried);
    else {
        Check(tried > 0 && eaten * 10 >= tried * 8, "eat: %d of %d food boxes 6 to 14 m away in its sight eaten within 20 s, its gauge at 10000", eaten, tried);
        Check(eaten > 0 && recOk == eaten, "eat: each take the record a player's game sends on the mushroom man's channel {number, point in tenths, 3}: %d of %d", recOk, eaten);
        Check(eaten > 0 && zoneOk == eaten, "eat: at each take its hips in the game's pick-up zone (400 mm along x and z): %d of %d", zoneOk, eaten);
        Check(eaten > 0 && gainOk == eaten, "eat: each take gives back the food's 13500 once (FoodEaten): %d of %d", gainOk, eaten);
    }
    if (off) { printf("%s\n", gFails ? "SOME FAILED" : "ALL PASSED"); return gFails ? 1 : 0; }

    // full: never goes
    int fullWalked = 0;
    for (const Case& c : near) {
#ifdef VDOLL_HOST_FOODS
        gFoods.assign(1, FoodAt(*c.p));
#endif
        float pos[3] = {c.sp->p[0], c.sp->p[1] + 800.f, c.sp->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(c.p->box[0] - pos[0], c.p->box[2] - pos[2]) / 6.2831853f * 65536.f);
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 1200, 30000);
        if (r.walked || r.to >= 0) ++fullWalked;
        Settle(120);
    }
    Check(fullWalked == 0, "full: its gauge full, no food walked to (%d of %d runs)", fullWalked, tried);

    // foe: an enemy in its sight, never goes
    int foeRuns = 0, foeWalked = 0;
    for (const Case& c : near) {
#ifdef VDOLL_HOST_FOODS
        gFoods.assign(1, FoodAt(*c.p));
#endif
        float pos[3] = {c.sp->p[0], c.sp->p[1] + 800.f, c.sp->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(c.p->box[0] - pos[0], c.p->box[2] - pos[2]) / 6.2831853f * 65536.f);
        if (!PlaceFoe(5, pos, facing, {12000.f, 10000.f, 14000.f, 8000.f})) continue;
        ++foeRuns;
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 240, 10000);
        if (r.walked) ++foeWalked;
        gHave[5] = false;
        Settle(120);
        if (foeRuns >= 3) break;
    }
    Check(foeRuns > 0 && foeWalked == 0, "foe: an enemy in its sight, no food walked to (%d of %d runs)", foeWalked, foeRuns);

    // zone: its gauge full, standing on a box
    int zoneRuns = 0, zoneTaken = 0;
    for (const Point& p : points) {
#ifdef VDOLL_HOST_FOODS
        gFoods.assign(1, FoodAt(p));
#endif
        float pos[3] = {p.box[0] + 100.f, p.box[1] - 60.f + 800.f, p.box[2] - 100.f};
        uint16_t facing = 0;
        ++zoneRuns;
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 60, 30000);
        if (r.take >= 0 && r.take - (gFrameAt - 660) < 30) ++zoneTaken;
        Settle(120);
        if (zoneRuns >= 3) break;
    }
    Check(zoneRuns > 0 && zoneTaken == zoneRuns, "zone: its gauge full, standing in a box's zone: taken within 30 frames (%d of %d)", zoneTaken, zoneRuns);

    // gone: a player takes it 1 s after the doll set off: no record from the doll
    int goneRuns = 0, goneTaken = 0;
    for (const Case& c : near) {
#ifdef VDOLL_HOST_FOODS
        gFoods.assign(1, FoodAt(*c.p));
#endif
        float pos[3] = {c.sp->p[0], c.sp->p[1] + 800.f, c.sp->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(c.p->box[0] - pos[0], c.p->box[2] - pos[2]) / 6.2831853f * 65536.f);
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 900, 10000, 60);
        ++goneRuns;
        if (r.take >= 0) ++goneTaken;
        Settle(120);
        if (goneRuns >= 3) break;
    }
    Check(goneRuns > 0 && goneTaken == 0, "gone: the box taken by a player as it goes: no record from the doll (%d of %d runs)", goneTaken, goneRuns);

    // wake ((v150-host beta 25, review25) the food left for a teammate to free): going to a food, a teammate (a player) reading a book
    // 1.2 to 1.8 m from it in its sight, friendly fire on (the CQC throw, standing): it leaves the food and throws him once (1500,
    // 0xCF) within 480 frames (a reading's 8 s), never "no way to him". 65673d2: the food's leave cleared the way the wake had just
    // planned, the wake read it as a way's end short of him within 2 m and stopped ("no way to him"), 10 s before it looks at him
    // again. The doll is held where it stands until its wake's look has taken him (as the wake test's HoldForLook)
#if defined(VDOLL_BODY_FOOD) && defined(VDOLL_HOST_FOODS) && defined(VDOLL_PLAYERNOW_WAKE) && defined(VDOLL_BODY_WAKE)
    {
        int wakeRuns = 0, wakeThrown = 0, wakeStops = 0;
        gFF = 1;
        for (const Case& c : near) {
            if (wakeRuns >= 3) break;
            gFoods.assign(1, FoodAt(*c.p));
            float pos[3] = {c.sp->p[0], c.sp->p[1] + 800.f, c.sp->p[2]};
            uint16_t facing = (uint16_t)lrintf(std::atan2(c.p->box[0] - pos[0], c.p->box[2] - pos[2]) / 6.2831853f * 65536.f);
            gRecs.clear(); gLines.clear();
            vdoll::body::Self s;
            s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send; s.life = ++gLife;
            gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
            long long placedAt = -1, chosenAt = -1, thrownAt = -1;
            float hold[3] = {0, 0, 0};
            for (long long t = gFrameAt; t < gFrameAt + 1500; ++t) {
                gNow = t;
                if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
                vdoll::body::Frame(t);
                vdoll::body::Knocked(0, 1, false, 10000, 144);
                s.tick = t % 6 == 0;
                vdoll::body::Alive(s, t);
                vdoll::body::FoodInfo fi;
                vdoll::body::WakeInfo w;
                const bool going = vdoll::body::Food(0, &fi) && fi.to >= 0;
                const bool waking = vdoll::body::Wake(0, &w) && w.to == 3;
                if (placedAt < 0 && going) {             // (the reader set down beside it, in its sight, on its floor)
                    const float feet[3] = {pos[0], pos[1] - 800.f, pos[2]}, eye[3] = {pos[0], pos[1] + 750.f, pos[2]};
                    for (int j = 0; j < 24 && placedAt < 0; ++j)
                        for (float r : {1500.f, 1300.f, 1700.f}) {
                            const float a = (float)j * 0.2618f, q[3] = {feet[0] + std::sin(a) * r, feet[1], feet[2] + std::cos(a) * r};
                            float len = 0, end[3];
                            if (!WayTo(feet, q, &len, end) || std::hypot(end[0] - q[0], end[2] - q[2]) > 100.f || std::fabs(end[1] - feet[1]) > 200.f || len > 2500.f) continue;
                            const float chest[3] = {end[0], end[1] + 1150.f, end[2]};
                            if (vdoll::body::SightStopped(eye, chest)) continue;
                            const float d = std::hypot(end[0] - feet[0], end[2] - feet[2]);
                            if (d < 1200.f || d > 1800.f) continue;
                            gHave[3] = true; gPlayers[3] = vdoll::PlayerNow(); gPlayers[3].team = 0; gPlayers[3].object = gPlayers[3].alive = true; gPlayers[3].life = 200;
                            gPlayers[3].pos[0] = end[0]; gPlayers[3].pos[1] = end[1] + 800.f; gPlayers[3].pos[2] = end[2]; gPlayers[3].state = 3u;
                            gPlayers[3].reading = true; gPlayers[3].stunMax = 30000; gPlayers[3].wakePart = 50; gPlayers[3].stamina = 30000;
                            placedAt = t; memcpy(hold, pos, sizeof hold);
                            break;
                        }
                }
                if (placedAt >= 0 && chosenAt < 0) {
                    if (waking) chosenAt = t;
                    else if (t - placedAt <= 31) memcpy(pos, hold, sizeof hold);   // (held until its look has taken him)
                }
                if (thrownAt < 0)
                    for (const Rec& x : gRecs)
                        if (x.slot == 3 && x.kind == 0 && x.r.size() == 9 && x.r[0] == 0x04 && x.r[6] == 0xCF) { thrownAt = x.frame; break; }
                if (thrownAt >= 0 && t > thrownAt + 6) { gPlayers[3].reading = false; }   // (his fall: his reading over)
                if (placedAt >= 0 && t - placedAt > 600) break;
            }
            gFrameAt += 2100;
            if (placedAt < 0 || chosenAt < 0) { gHave[3] = false; Settle(120); continue; }   // (no reader placed, or its look did not take him)
            ++wakeRuns;
            bool stopped = false;
            for (const std::string& l : gLines) if (l.find("stops waking player 4") != std::string::npos && l.find("no way to him") != std::string::npos) stopped = true;
            if (stopped) ++wakeStops;
            if (thrownAt >= 0 && thrownAt - placedAt <= 480) ++wakeThrown;
            if (getenv("VDOLL_TEST_VERBOSE") || thrownAt < 0 || stopped)
                printf("  wake: the reader set down %lld frames after it set off to the food, taken %lld frames later; %s%s\n", placedAt - (gFrameAt - 2100), chosenAt - placedAt,
                       thrownAt >= 0 ? ("thrown " + std::to_string(thrownAt - placedAt) + " frames after").c_str() : "NOT thrown", stopped ? ", \"no way to him\"" : "");
            gHave[3] = false;
            Settle(120);
        }
        gFF = -1;
        Check(wakeRuns > 0 && wakeThrown == wakeRuns && wakeStops == 0,
              "wake: going to a food, a teammate reading 1.2 to 1.8 m away: the food left, he is thrown free within 480 frames (%d of %d runs; %d \"no way to him\")",
              wakeThrown, wakeRuns, wakeStops);
    }
#endif

    printf("%s\n", gFails ? "SOME FAILED" : "ALL PASSED");
    return gFails ? 1 : 0;
}
