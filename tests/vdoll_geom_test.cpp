// (v150-host beta 21, geom) A virtual doll never sees, aims at nor hits a player through what stops a bullet in the host's
// game, nor where his body is not (docs/VIRTUAL_DOLL.md 27). A map's collision file (City Under Siege by default), a doll
// and an enemy 15 m before it on open ground. Host::gameLine stands for the host's game's own line test with a thing the
// collision file does not hold (a wall across the middle of their line, as a vehicle or a crate the game makes while a
// round runs), of a hardness set by each part:
//   1. no thing: the doll sees him and hits him (the dolls still fight);
//   2. a thing harder than any bullet: not seen, not one hit;
//   3. the bullet's power against the thing's hardness (the game's rule: a bullet goes through a thing softer than it):
//      hardness 130 stops the AK-47 (120) and the M1911A1 (50), the M37 (150) goes through; 100 stops the M1911A1 alone;
//      40 stops none; a soft thing then a hard one: stopped; a sight is stopped by every thing but a pane of glass (a
//      cardboard box hides a player, a window does not);
//   4. the stances: a standing player's chest and head 350 and 850 mm over his hips, a crouched one's 50 and 310, a lying
//      one's -100 and 40 (his body along the ground, under his hips' point);
//   5. a player lying on open ground: seen and hit, no bullet's end more than 150 mm over his hips;
//   6. a player lying behind low cover of the map (a place searched for: a standing head there shows over the cover, a
//      lying body does not): not seen, not one hit, as long as the doll stands where it was put;
//   7. MGO1_VDOLL_GAME_LINES=0 and MGO1_VDOLL_STANCE=0: beta 20's behaviour back (part 2 sees and hits again, part 6 sees
//      him again: on 13 of the 15 maps it hits him too).
// VDOLL_GEOM_TEST_BETA20=1: the whole test with the two switches off from the start (beta 20's behaviour): parts 2, 3, 4, 5
// and 6 FAIL. Built on beta 20's own body (no Host::gameLine, no PlayerNow::state) the same parts fail.
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_geom_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o /tmp/vdoll_geom_test
//   /tmp/vdoll_geom_test <map.geom>
// (the map's collision file comes from the owner's disc: python3 -I tools/stage_dat.py extract <STAGE_N.DAT> n003a <dir>;
// it is never put in the repository)
#include "vdoll_body.h"
#include "doll_geom.h"
#include "doll_nav.h"
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
    if (strstr(b, ": sees player 3")) ++gSees;
    if (getenv("VDOLL_TEST_LOG") || strstr(b, "mesh")) printf("%s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "n003a"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }
struct Sent { int slot, kind; std::vector<uint8_t> rec; };
static std::vector<Sent> gSent;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gSent.push_back({slot, kind, rec}); }

// the things the game holds and the collision file does not: vertical planes across the line of the doll and the enemy,
// at a part of the way along it, each with its hardness (0: no thing)
struct Thing { float along; int hardness; uint32_t attr = 0x42; };
static std::vector<Thing> gThings;
static float gFrom[3], gDir[3], gLen = 1.f;
static int gAsked = 0;
#ifdef VDOLL_BODY_LINES
static int GameLine(const float* a, const float* b, bool, vdoll::LineHit* hit) {
    ++gAsked;
    int best = -1;
    float bestT = 2.f;
    for (size_t i = 0; i < gThings.size(); ++i) {
        const float at = gThings[i].along * gLen;
        const float da = (a[0] - gFrom[0]) * gDir[0] + (a[2] - gFrom[2]) * gDir[2] - at, db = (b[0] - gFrom[0]) * gDir[0] + (b[2] - gFrom[2]) * gDir[2] - at;
        if ((da > 0) == (db > 0)) continue;
        const float t = da / (da - db);
        if (t < bestT) { bestT = t; best = (int)i; }
    }
    if (best < 0) return 0;
    if (hit) {
        *hit = vdoll::LineHit();
        for (int k = 0; k < 3; ++k) hit->at[k] = a[k] + (b[k] - a[k]) * bestT;
        hit->attr = gThings[best].attr; hit->object = true; hit->objectId = 0x01000000u + (uint32_t)best; hit->hardness = gThings[best].hardness;
    }
    return 1;
}
#endif

static bool Read(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) gGeom.insert(gGeom.end(), b, b + n);
    fclose(f);
    return !gGeom.empty();
}

// a part: the doll at `start` facing the enemy for `frames`, ended when it walks more than `leash` from there (it no longer
// stands where the part put it); its sights of him, its hits, the highest end of a bullet over his hips
static long long gFrame = 0;
struct Result { int sees = 0, hits = 0; float top = -1e9f; long long ran = 0; };
static Result Part(vdoll::body::Self& s, float* pos, const float* start, uint16_t* facing, uint16_t face0, int frames, float leash, bool hold = false) {
    memcpy(pos, start, 3 * sizeof(float));
    *facing = face0;
    gSent.clear();
    gSees = 0;
    gHave[2] = true;
    s.life = gFrame;                                          // (a new life each part: its fight afresh)
    Result r;
    const long long from = gFrame;
    for (long long end = gFrame + frames; gFrame < end; gFrame += 2) {
        if (hold) memcpy(pos, start, 3 * sizeof(float));      // (kept where the part put it: its eyes there all along)
        else if (std::hypot(pos[0] - start[0], pos[2] - start[2]) > leash) break;
        memcpy(gPlayers[1].pos, pos, 3 * sizeof(float));
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
    }
    r.ran = gFrame - from;
    for (const Sent& m : gSent) {
        if (m.slot != 2 || m.rec.empty()) continue;
        if (m.kind == 0 && m.rec[0] == 4) ++r.hits;
        if (m.kind == 2 && m.rec[0] == 0x14 && m.rec.size() >= 11) {   // (its bullet's end: tenths of the game's units)
            const float y = (float)(int16_t)(m.rec[7] | (m.rec[8] << 8)) * 10.f;
            r.top = std::max(r.top, y - gPlayers[2].pos[1]);
        }
    }
    r.sees = gSees;
    gHave[2] = false;
    vdoll::body::Down(0);
    for (int k = 0; k < 120; k += 2) { vdoll::body::Frame(gFrame); gFrame += 2; }   // (out of sight a while: the next part's fight is a new one)
    return r;
}

// 0 standing, 1 crouched, 2 lying: as the host's copy of a player holds him (measured, bench geom_a1: his style record 00,
// 02, 03; his first state word 3, 5, 9; lying, his hips 300 mm over the floor)
static void Stance(vdoll::PlayerNow& p, int stance) {
    p.style = stance == 2 ? 3 : stance == 1 ? 2 : 0;
#ifdef VDOLL_PLAYERNOW_STATE
    p.state = stance == 2 ? 9 : stance == 1 ? 5 : 3;
#endif
}

int main(int argc, char** argv) {
    if (argc < 2 || !Read(argv[1])) { fprintf(stderr, "usage: vdoll_geom_test <map.geom>\n"); return 2; }
    const bool beta20 = getenv("VDOLL_GEOM_TEST_BETA20") != nullptr;
    setenv("MGO1_VDOLL_LEVEL", "10", 1);
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
    unsetenv("MGO1_VDOLL_GAME_LINES"); unsetenv("MGO1_VDOLL_STANCE");
    if (beta20) { setenv("MGO1_VDOLL_GAME_LINES", "0", 1); setenv("MGO1_VDOLL_STANCE", "0", 1); printf("(the two switches off: beta 20's behaviour)\n"); }
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
#ifdef VDOLL_BODY_LINES
    h.gameLine = GameLine;
#else
    printf("(this body has no Host::gameLine: the game's own line test is never asked)\n");
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    gHave[1] = true; gPlayers[1].team = 0; gPlayers[1].object = true; gPlayers[1].alive = true; gPlayers[1].life = 200;
    gPlayers[2].team = 1; gPlayers[2].object = true; gPlayers[2].alive = true; gPlayers[2].life = 200;
    Stance(gPlayers[1], 0); Stance(gPlayers[2], 0);
    const float kHips = 800.f, kLieHips = 300.f;             // (the hips over the floor: standing, lying)
    // the doll's and the enemy's places: two points of the map's mesh 15 m apart on open ground (the lines from the doll's
    // eyes to every height of the enemy clear on the bodies' collision), the first found; then a place behind low cover:
    // from the doll's eyes (standing, leaning left and right) a standing player's chest and head and the points beta 20
    // tested for a lying one (350 and 850 mm over his lying hips) show, nothing of a lying body does (to 600 mm up)
    float start[3], far[3], cStart[3], cFar[3];
    bool cover = false;
    {
        doll::Geom g; std::string err, log;
        doll::NavMesh nav; doll::NavConfig cfg;
        if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err) || !nav.Build(g, cfg, log)) { printf("FAIL: the map not read\n"); return 1; }
        static uint32_t seed = 0x1234567u;
        unsigned (*rnd)() = []() -> unsigned { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
        bool found = false;
        for (int k = 0; k < 20000 && !found; ++k) {
            float p[3], q[3];
            if (!nav.RandomPoint(nullptr, 0.f, p, rnd) || !nav.RandomPoint(p, 16000.f, q, rnd)) continue;
            const float d = std::hypot(p[0] - q[0], p[2] - q[2]);
            if (d < 14000.f || d > 16000.f || std::fabs(p[1] - q[1]) > 300.f) continue;
            bool ok = true;
            for (float h1 : {1550.f, 1100.f}) for (float h2 : {1150.f, 1650.f, 300.f, 100.f, 500.f}) {
                const float a[3] = {p[0], p[1] + h1, p[2]}, b[3] = {q[0], q[1] + h2, q[2]};
                ok = ok && nav.Clear(a, b);
            }
            if (!ok) continue;
            memcpy(start, p, sizeof start); memcpy(far, q, sizeof far);
            found = true;
        }
        if (!found) { printf("FAIL: no open ground found\n"); return 1; }
        for (int k = 0; k < 400000 && !cover; ++k) {
            float p[3], q[3];
            if (!nav.RandomPoint(nullptr, 0.f, p, rnd) || !nav.RandomPoint(p, 14000.f, q, rnd)) continue;
            const float d = std::hypot(p[0] - q[0], p[2] - q[2]);
            if (d < 8000.f || d > 13000.f || std::fabs(p[1] - q[1]) > 200.f) continue;
            const float yaw = std::atan2(q[0] - p[0], q[2] - p[2]);
            bool ok = true;
            for (int lean = -1; lean <= 1 && ok; ++lean) {
                const float e[3] = {p[0] + std::cos(yaw) * 490.f * lean, p[1] + 1550.f, p[2] - std::sin(yaw) * 490.f * lean};
                for (float up : {50.f, 150.f, 250.f, 300.f, 360.f, 450.f, 520.f, 600.f}) {
                    const float b[3] = {q[0], q[1] + up, q[2]};
                    if (nav.Clear(e, b)) ok = false;
                }
            }
            if (!ok) continue;
            const float e[3] = {p[0], p[1] + 1550.f, p[2]};
            for (float up : {kLieHips + 350.f, kLieHips + 850.f, kHips + 350.f, kHips + 850.f}) {
                const float b[3] = {q[0], q[1] + up, q[2]};
                ok = ok && nav.Clear(e, b);
            }
            // (and the lying hips' column well apart from the cover's own faces: 250 mm of room around the points beta 20 tested)
            for (float dx : {-250.f, 250.f}) for (float dz : {-250.f, 250.f}) {
                const float b[3] = {q[0] + dx, q[1] + kLieHips + 850.f, q[2] + dz};
                ok = ok && nav.Clear(e, b);
            }
            if (!ok) continue;
            memcpy(cStart, p, sizeof p); memcpy(cFar, q, sizeof q);
            cover = true;
        }
    }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(gFrame); gFrame += 2; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("FAIL: the bodies' mesh was not built\n"); return 1; }
    float pos[3];
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send;
    float hips[3] = {start[0], start[1] + kHips, start[2]};
    const float yaw = std::atan2(far[0] - start[0], far[2] - start[2]);
    const uint16_t face0 = (uint16_t)lrintf(yaw / 6.2831853f * 65536.f);
    gPlayers[2].pos[0] = far[0]; gPlayers[2].pos[1] = far[1] + kHips; gPlayers[2].pos[2] = far[2];
    printf("the doll at %.0f %.0f %.0f, the enemy at %.0f %.0f %.0f\n", hips[0], hips[1], hips[2], gPlayers[2].pos[0], gPlayers[2].pos[1], gPlayers[2].pos[2]);
    memcpy(gFrom, hips, sizeof gFrom);
    gLen = std::hypot(far[0] - start[0], far[2] - start[2]);
    gDir[0] = (far[0] - start[0]) / gLen; gDir[1] = 0.f; gDir[2] = (far[2] - start[2]) / gLen;
    const float leash = gLen * 0.4f;                          // (short of the thing in the middle)
    int fails = 0;

    gThings.clear(); gAsked = 0;
    Result r = Part(s, pos, hips, &facing, face0, 480, leash);
    printf("1. no thing: seen %d time(s), %d hit(s), %d lines asked of the game\n", r.sees, r.hits, gAsked);
    if (!r.hits || !r.sees) { printf("FAIL: the doll does not fight an enemy in the open\n"); ++fails; }

    gThings = {{0.5f, 220}}; gAsked = 0;
    r = Part(s, pos, hips, &facing, face0, 480, leash);
    printf("2. a thing the game holds between them (as hard as a wall): seen %d time(s), %d hit(s), %d lines asked of the game\n", r.sees, r.hits, gAsked);
    if (r.hits || r.sees) { printf("FAIL: the doll saw or hit him through what stops a bullet in the game\n"); ++fails; }

#ifdef VDOLL_BODY_LINES
    {
        const float a[3] = {hips[0], hips[1] + 750.f, hips[2]}, b[3] = {gPlayers[2].pos[0], gPlayers[2].pos[1] + 350.f, gPlayers[2].pos[2]};
        struct Case { std::vector<Thing> things; bool sight, ak47, m1911, m37; const char* what; };
        const Case cases[] = {
            {{}, false, false, false, false, "no thing"},
            {{{0.5f, 130}}, true, true, true, false, "hardness 130"},
            {{{0.5f, 100}}, true, false, true, false, "hardness 100"},
            {{{0.5f, 40}}, true, false, false, false, "hardness 40"},
            {{{0.3f, 40}, {0.6f, 220}}, true, true, true, true, "a soft thing then a hard one"},
            {{{0.3f, 40}, {0.6f, 100}}, true, false, true, false, "two soft things"},
            {{{0.5f, 15, 0x04000082u}}, false, false, false, false, "a pane of glass (hardness 15)"},
            {{{0.3f, 15, 0x04000082u}, {0.6f, 40}}, true, false, false, false, "glass then a cardboard box (hardness 40)"},
        };
        int bad = 0;
        for (const Case& c : cases) {
            gThings = c.things;
            const bool sight = vdoll::body::SightStopped(a, b), ak = vdoll::body::ShotStopped(a, b, 0x0C), pistol = vdoll::body::ShotStopped(a, b, 0x06),
                       m37 = vdoll::body::ShotStopped(a, b, 0x0E);
            const bool ok = sight == c.sight && ak == c.ak47 && pistol == c.m1911 && m37 == c.m37;
            printf("3. %s: a sight %s, the AK-47 %s, the M1911A1 %s, the M37 %s%s\n", c.what, sight ? "stopped" : "clear", ak ? "stopped" : "through",
                   pistol ? "stopped" : "through", m37 ? "stopped" : "through", ok ? "" : "   <- WRONG");
            if (!ok) ++bad;
        }
        if (bad) { printf("FAIL: a bullet's power against a thing's hardness\n"); ++fails; }
    }
    {
        vdoll::PlayerNow p = gPlayers[2];
        const float want[3][2] = {{350.f, 850.f}, {50.f, 310.f}, {-100.f, 40.f}};
        static const char* kName[3] = {"standing", "crouched", "lying"};
        int bad = 0;
        for (int st = 0; st < 3; ++st) {
            Stance(p, st);
            float c[3], hd[3];
            vdoll::body::BodyPoints(p, c, hd);
            const bool ok = std::fabs(c[1] - p.pos[1] - want[st][0]) < 1.f && std::fabs(hd[1] - p.pos[1] - want[st][1]) < 1.f;
            printf("4. %s: his chest %.0f and his head %.0f mm over his hips%s\n", kName[st], c[1] - p.pos[1], hd[1] - p.pos[1], ok ? "" : "   <- WRONG");
            if (!ok) ++bad;
        }
        if (bad) { printf("FAIL: a player's points by his stance\n"); ++fails; }
    }
#else
    printf("3., 4. FAIL: this body has no line through the game's things and no stance of a lying player\n"); ++fails;
#endif

    gThings.clear();
    Stance(gPlayers[2], 2);
    gPlayers[2].pos[1] = far[1] + kLieHips;
    r = Part(s, pos, hips, &facing, face0, 480, leash);
    printf("5. he lies on open ground: seen %d time(s), %d hit(s), its bullets' highest end %.0f mm over his hips\n", r.sees, r.hits, r.top);
    if (!r.sees || !r.hits) { printf("FAIL: the doll does not fight a lying enemy in the open\n"); ++fails; }
    else if (r.top > 150.f) { printf("FAIL: a bullet's end where a lying player's body is not\n"); ++fails; }

    float cHips[3] = {0, 0, 0};
    uint16_t cFace = 0;
    if (!cover) printf("6. (no low cover found on this map: the part is left out)\n");
    else {
        cHips[0] = cStart[0]; cHips[1] = cStart[1] + kHips; cHips[2] = cStart[2];
        cFace = (uint16_t)lrintf(std::atan2(cFar[0] - cStart[0], cFar[2] - cStart[2]) / 6.2831853f * 65536.f);
        gPlayers[2].pos[0] = cFar[0]; gPlayers[2].pos[1] = cFar[1] + kLieHips; gPlayers[2].pos[2] = cFar[2];
        r = Part(s, pos, cHips, &facing, cFace, 600, 600.f, true);
        printf("6. he lies behind low cover at %.0f %.0f %.0f, the doll at %.0f %.0f %.0f (%.1f m): seen %d time(s), %d hit(s) in %lld frames there\n",
               cFar[0], cFar[1], cFar[2], cStart[0], cStart[1], cStart[2], std::hypot(cFar[0] - cStart[0], cFar[2] - cStart[2]) / 1000.f, r.sees, r.hits, r.ran);
        if (r.sees || r.hits) { printf("FAIL: the doll saw or hit a lying player over cover that hides him\n"); ++fails; }
#ifdef VDOLL_BODY_LINES
        {
            const float eye[3] = {cHips[0], cHips[1] + 750.f, cHips[2]};
            float c[3], hd[3];
            vdoll::body::BodyPoints(gPlayers[2], c, hd);
            const bool sc = vdoll::body::SightStopped(eye, c), sh = vdoll::body::SightStopped(eye, hd);
            printf("6. its eye's line to his chest %s, to his head %s (%.0f and %.0f mm over the floor)\n", sc ? "stopped" : "clear", sh ? "stopped" : "clear",
                   c[1] - cFar[1], hd[1] - cFar[1]);
            if (!sc || !sh) { printf("FAIL: the points tested are not where a lying player's body is\n"); ++fails; }
        }
#endif
    }

    if (!beta20) {
        setenv("MGO1_VDOLL_GAME_LINES", "0", 1); setenv("MGO1_VDOLL_STANCE", "0", 1);
        vdoll::body::Init(h);
        Stance(gPlayers[2], 0);
        gPlayers[2].pos[0] = far[0]; gPlayers[2].pos[1] = far[1] + kHips; gPlayers[2].pos[2] = far[2];
        gThings = {{0.5f, 220}}; gAsked = 0;
        r = Part(s, pos, hips, &facing, face0, 480, leash);
        printf("7. MGO1_VDOLL_GAME_LINES=0, the hard thing: seen %d time(s), %d hit(s), %d lines asked of the game\n", r.sees, r.hits, gAsked);
        if (!r.hits || gAsked) { printf("FAIL: the switch does not give beta 20's behaviour back\n"); ++fails; }
        if (cover) {
            gThings.clear();
            Stance(gPlayers[2], 2);
            gPlayers[2].pos[0] = cFar[0]; gPlayers[2].pos[1] = cFar[1] + kLieHips; gPlayers[2].pos[2] = cFar[2];
            r = Part(s, pos, cHips, &facing, cFace, 600, 600.f, true);
            if (r.hits) printf("7. MGO1_VDOLL_STANCE=0, he lies behind the low cover: seen %d time(s), %d hit(s), its bullets' highest end %.0f mm over his hips\n", r.sees, r.hits, r.top);
            else printf("7. MGO1_VDOLL_STANCE=0, he lies behind the low cover: seen %d time(s), no hit (its own stance kept it from a shot there)\n", r.sees);
            if (!r.sees) { printf("FAIL: the switch does not give beta 20's behaviour back\n"); ++fails; }
        }
    }
    printf("%s\n", fails ? "FAILED" : "ALL PASSED");
    return fails ? 1 : 0;
}
