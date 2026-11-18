// (v150-host beta 21, cover) A real player killed behind cover on his own screen (the players' report of 12 November), off
// line on a map's .geom (MGO1Loader/vdoll_body.cpp CoverHit, ReflexHold; reports/cover_kills.md). One virtual doll on the
// map's mesh; corners found on the map's collision: a place A in the doll's clear line 8 to 25 m away, a place B 1.2 m
// on from A along the floor out of its line (chest and head), a place 1.6 m before A in its line too. The player's OWN
// game runs him from there through A to B at a run (3.9 m/s) and stops him behind the cover. What the host holds of him is
// what the network gives it: his position records every 100 ms (his place when sent), arriving VDOLL_TEST_LAT ms later
// (80), and his copy, which the game moves on by his inputs, as late as his records plus a frame (on a slowed host 100 ms
// more: the copy runs by the host's frames). Each damage record the doll sends reaches his game VDOLL_TEST_LAT ms after it
// goes out: there his own place is tested against the map's collision from the doll's eye (standing or crouched, straight
// or leaning: any clear line counts as seen).
// Checked: no damage reaches him while his own place is out of the doll's line; a fight's first damage no sooner than
// 250 ms of real time after the doll's sight. Run at the host's full pace (60 frames a second) and slowed (37).
// MGO1_VDOLL_COVER=0 MGO1_VDOLL_REFLEX=0 (as beta 20) fail them: the "before".
// Four kinds of run on each map: "corner" (above: straight through A into the cover), "turn" (B any way from A: he turns
// at A, back the way he came too; MGO1_VDOLL_COVER=run, the straight run alone, fails it), and two controls of what the
// gate must NOT take from the dolls: "open" (the same run where nothing within 1.5 m of his way is out of the doll's
// line: a hit in 6 fights of 10 at least) and "stand" (he stands at A, the cover 1.2 m from him: the same). The session's
// round trip the host reads is twice the network's way (VDOLL_TEST_RTT=<ms>, -1: none known, the gate's 100 ms a way).
// The host's game passes the doll's hit on with its own next datagram to him: each damage record waits 0 to
// VDOLL_TEST_RELAY host frames (7: what the gate counts, MGO1_VDOLL_COVER_RELAY; measured on the host: 7 or less 85
// times in 100, 9 or less 95) before its way to him. VDOLL_TEST_SEED=<n>: other corners; MGO1_VDOLL_SEED: other draws.
// VDOLL_TEST_KILL=1: another test on the same maps, the doll after its kill (the victim's own game turns his camera to
// his killer's copy 0.25 to 2.75 s after his death): a player standing in the open dies of its fourth hit; how far the
// doll is 2.75 s later from where it shot. As beta 20 it runs on at once (checked: over 1 m in half the kills at
// least); with MGO1_VDOLL_KILL_HOLD=3000 it is still there (checked: within 50 mm, every kill).
// VDOLL_TEST_PEEK=1: a third test, the killer the victim cannot see (MGO1_VDOLL_SHOWN): the player stands in the open, the
// doll comes round a corner on its own way and stops the moment it sees him; its copy in the host's game is what his
// game shows a relay later (here: where the doll was 14 frames before, 70 mm a frame at most: 0.9 m behind at a run and
// there 0.25 s after its stop, as measured on a playable doll's own game, cover_DU10_off). Checked: no damage goes out
// while that copy has no line to him (any of its stances), and the doll still hits him; as beta 20
// (MGO1_VDOLL_SHOWN=0 MGO1_VDOLL_REFLEX=0) it must fail.
// VDOLL_TEST_THING=1 (v150-host beta 24, walls3): the cover is a thing of the host's game, not the map: open corners (nothing
// within 1.5 m of his way out of the doll's line on the map) with a crate of the game (Host::gameLine: a box 1.4 m across,
// 2.4 m high) 1 m before B, hiding B alone; the same checks (no damage reaches him behind it), the open kind with no crate
// the control. Beta 23's gate asked the game 24 times a verdict at most (MGO1_VDOLL_REACH_THINGS=0): in the open every line
// is clear on the map, the places behind the crate were never asked, and it FAILS.
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_cover_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_cover_test
//   MGO1_VDOLL_LEVEL=10 ./vdoll_cover_test <map.geom> ...
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
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static int gSees = 0;
static double gFps = 60.0;
static long long gNow = 0;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, ": sees player")) ++gSees;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("f%lld %s\n", gNow, b);
}
static std::string gKey = "test0";
static bool MapNow(std::string* page, int* extra) { *page = gKey; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 0; }                         // (deathmatch: everyone an enemy)
static unsigned long long RealMs() { return (unsigned long long)((double)gNow * 1000.0 / gFps); }
static double Ms(long long frame) { return (double)frame * 1000.0 / gFps; }

struct Rec { long long frame; int slot, kind; std::vector<uint8_t> r; };
static std::vector<Rec> gRecs;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gRecs.push_back({gNow, slot, kind, rec}); }
static unsigned gRnd = 12345;
static unsigned Rnd() { gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5; return gRnd; }

enum { K_CORNER, K_TURN, K_OPEN, K_STAND, K_KINDS };
static const char* const kKind[K_KINDS] = {"corner", "turn", "open", "stand"};
struct Corner { float d[3], p0[3], a[3], b[3]; int kind = K_CORNER; };
static doll::NavMesh* gNav = nullptr;
// (v150-host beta 24, walls3: VDOLL_TEST_THING) the things of the host's game the map's collision does not hold: boxes
struct Box { float lo[3], hi[3]; };
static std::vector<Box> gBoxes;
static bool BoxHit(const float* a, const float* b, float* tout) {
    float best = 2.f;
    for (const Box& x : gBoxes) {
        float t0 = 0.f, t1 = 1.f;
        bool in = true;
        for (int k = 0; k < 3 && in; ++k) {
            const float d = b[k] - a[k];
            if (std::fabs(d) < 1e-6f) { in = a[k] >= x.lo[k] && a[k] <= x.hi[k]; continue; }
            float u = (x.lo[k] - a[k]) / d, v = (x.hi[k] - a[k]) / d;
            if (u > v) std::swap(u, v);
            t0 = std::max(t0, u); t1 = std::min(t1, v); in = t0 <= t1;
        }
        if (in && t0 < best) best = t0;
    }
    if (best > 1.f) return false;
    if (tout) *tout = best;
    return true;
}
static int GameLine(const float* a, const float* b, bool, vdoll::LineHit* hit) {
    float t;
    if (!BoxHit(a, b, &t)) return 0;
    if (hit) {
        *hit = vdoll::LineHit();
        for (int k = 0; k < 3; ++k) hit->at[k] = a[k] + (b[k] - a[k]) * t;
        hit->attr = 0x42; hit->object = true; hit->hardness = 220;
    }
    return 1;
}
static bool Clear(const float* a, const float* b) { return gNav->Clear(a, b) && !BoxHit(a, b, nullptr); }
static void Part(const float* hips, float up, float* out) { out[0] = hips[0]; out[1] = hips[1] + up; out[2] = hips[2]; }
static bool SeenFrom(const float* eye, const float* hips) {
    float c[3], h[3];
    Part(hips, 350.f, c); Part(hips, 850.f, h);
    return Clear(eye, c) || Clear(eye, h);
}
// any of the doll's eyes (standing 750 or crouched 300 over its hips; straight or leaning 490 mm aside) sees him
static bool SeenByDoll(const float* doll, const float* hips) {
    const float yaw = std::atan2(hips[0] - doll[0], hips[2] - doll[2]);
    for (float up : {750.f, 300.f})
        for (int lean : {0, 1, -1}) {
            const float e[3] = {doll[0] + std::cos(yaw) * 490.f * lean, doll[1] + up, doll[2] - std::sin(yaw) * 490.f * lean};
            if (lean && !Clear(doll + 0, e)) continue;
            if (SeenFrom(e, hips)) return true;
        }
    return false;
}
// nothing he can reach within 1.5 m of p is out of the eye's line (the mesh's places around it: chest and head seen)
static bool OpenAround(const float* eye, const float* hips) {
    for (int k = 0; k < 8; ++k) {
        const float q[3] = {hips[0] + std::sin(k * 0.785398f) * 1500.f, hips[1] - 800.f, hips[2] + std::cos(k * 0.785398f) * 1500.f};
        float m[3];
        if (!gNav->Nearest(q, 300.f, m)) continue;
        float c[3], h[3];
        const float mh[3] = {m[0], m[1] + 800.f, m[2]};
        Part(mh, 350.f, c); Part(mh, 850.f, h);
        if (!gNav->Clear(eye, c) || !gNav->Clear(eye, h)) return false;
    }
    return true;
}
static bool FindCorner(const doll::Geom& g, Corner* c, int kind) {
    std::vector<const doll::Spawn*> spawns;
    for (const doll::Spawn& sp : g.spawns) if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) spawns.push_back(&sp);
    if (spawns.empty()) return false;
    for (int tries = 0; tries < 4000; ++tries) {
        const doll::Spawn* sp = spawns[Rnd() % spawns.size()];
        float d[3], a[3];
        if (!gNav->Nearest(sp->p, 2000.f, d) || gNav->AreaAt(d) != doll::AREA_STAND) continue;
        if (!gNav->RandomPoint(d, 25000.f, a, Rnd) || gNav->AreaAt(a) != doll::AREA_STAND) continue;
        const float dist = std::hypot(a[0] - d[0], a[2] - d[2]);
        if (dist < 8000.f || dist > 25000.f || std::fabs(a[1] - d[1]) > 3000.f) continue;
        float dh[3] = {d[0], d[1] + 800.f, d[2]}, ah[3] = {a[0], a[1] + 800.f, a[2]};
        const float eye[3] = {dh[0], dh[1] + 750.f, dh[2]};
        if (!SeenFrom(eye, ah)) continue;
        for (int k = 0; k < 8; ++k) {
            const float yaw = (float)k * 0.785398f + (float)(Rnd() % 100) * 0.003f;
            const float dx = std::sin(yaw), dz = std::cos(yaw);
            // (a turn: he comes to A from any way, not the cover's)
            const float yawP = kind == K_TURN ? (float)(Rnd() % 628) * 0.01f : yaw;
            const float px = std::sin(yawP), pz = std::cos(yawP);
            float b[3] = {a[0] + dx * 1200.f, a[1], a[2] + dz * 1200.f}, p0[3] = {a[0] - px * 1600.f, a[1], a[2] - pz * 1600.f};
            float bn[3], pn[3];
            if (!gNav->Nearest(b, 300.f, bn) || !gNav->Nearest(p0, 300.f, pn)) continue;
            if (std::fabs(bn[1] - a[1]) > 200.f || std::fabs(pn[1] - a[1]) > 200.f) continue;
            if (!gNav->Walkable(pn, a, true) || !gNav->Walkable(a, bn, true)) continue;
            const float bh[3] = {bn[0], bn[1] + 800.f, bn[2]}, ph[3] = {pn[0], pn[1] + 800.f, pn[2]};
            if (!SeenFrom(eye, ph)) continue;
            if (kind == K_OPEN) { if (!OpenAround(eye, ah) || !OpenAround(eye, bh) || !OpenAround(eye, ph)) continue; }
            else if (SeenByDoll(dh, bh)) continue;
            memcpy(c->d, dh, sizeof c->d); memcpy(c->a, ah, sizeof c->a); memcpy(c->b, bh, sizeof c->b); memcpy(c->p0, ph, sizeof c->p0);
            c->kind = kind;
            return true;
        }
    }
    return false;
}
// (VDOLL_TEST_THING) the crate hiding B: 1 m before it toward the doll, 1.4 m across, 0.4 m deep, from B's floor to 2.4 m up
static void BoxFor(const Corner& c, Box* x) {
    const float dx = c.d[0] - c.b[0], dz = c.d[2] - c.b[2], l = std::max(1.f, std::hypot(dx, dz));
    const float ux = dx / l, uz = dz / l, cx = c.b[0] + ux * 1000.f, cz = c.b[2] + uz * 1000.f;
    x->lo[0] = cx - std::fabs(uz) * 700.f - std::fabs(ux) * 200.f; x->hi[0] = cx + std::fabs(uz) * 700.f + std::fabs(ux) * 200.f;
    x->lo[2] = cz - std::fabs(ux) * 700.f - std::fabs(uz) * 200.f; x->hi[2] = cz + std::fabs(ux) * 700.f + std::fabs(uz) * 200.f;
    x->lo[1] = c.b[1] - 800.f - 200.f; x->hi[1] = c.b[1] - 800.f + 2400.f;
}
// the player's own place at t ms after he starts: from p0 through a to b at 3.9 m/s, then still at b
static void Own(const Corner& c, double t, float* out) {
    if (c.kind == K_STAND) { memcpy(out, c.a, 3 * sizeof(float)); return; }   // (he stands at A, the cover 1.2 m from him)
    const float v = 3.9f;                                // mm a ms
    const float l1 = std::hypot(c.a[0] - c.p0[0], c.a[2] - c.p0[2]), l2 = std::hypot(c.b[0] - c.a[0], c.b[2] - c.a[2]);
    float s = (float)std::max(0.0, t) * v;
    const float* from = c.p0; const float* to = c.a; float len = l1;
    if (s > l1) { s -= l1; from = c.a; to = c.b; len = l2; }
    if (s > len) s = len;
    const float k = len > 1.f ? s / len : 0.f;
    for (int i = 0; i < 3; ++i) out[i] = from[i] + (to[i] - from[i]) * k;
}

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
    if (argc < 2) { fprintf(stderr, "usage: vdoll_cover_test <map.geom> ...\n"); return 2; }
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    if (!getenv("MGO1_VDOLL_WEAPONS")) setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    const double lat = getenv("VDOLL_TEST_LAT") ? atof(getenv("VDOLL_TEST_LAT")) : 80.0;
    const int corners = getenv("VDOLL_TEST_CORNERS") ? atoi(getenv("VDOLL_TEST_CORNERS")) : 12;
    if (getenv("VDOLL_TEST_SEED")) gRnd = (unsigned)atoi(getenv("VDOLL_TEST_SEED")) * 2654435761u + 12345u;   // (other corners)
    const int relayMax = getenv("VDOLL_TEST_RELAY") ? atoi(getenv("VDOLL_TEST_RELAY")) : 7;   // (the host's frames a hit waits at most)
    unsigned relayRnd = 2463534242u;
    const int rtt = getenv("VDOLL_TEST_RTT") ? atoi(getenv("VDOLL_TEST_RTT")) : (int)(2.0 * lat);   // (-1: the session tells none)
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.realMs = RealMs;
    const bool thingTest = getenv("VDOLL_TEST_THING") != nullptr;   // (v150-host beta 24, walls3)
#ifdef VDOLL_HOST_GAME_LINE
    if (thingTest) h.gameLine = GameLine;
#endif
    vdoll::body::Init(h);
    printf("level %s, weapons %s, MGO1_VDOLL_COVER=%s, MGO1_VDOLL_REFLEX=%s, the network %0.f ms a way, the host's game passing a hit on within %d frames\n", getenv("MGO1_VDOLL_LEVEL"), getenv("MGO1_VDOLL_WEAPONS"),
           getenv("MGO1_VDOLL_COVER") ? getenv("MGO1_VDOLL_COVER") : "(on)", getenv("MGO1_VDOLL_REFLEX") ? getenv("MGO1_VDOLL_REFLEX") : "(250)", lat, relayMax);
    long long fr = 0;
    const bool killTest = getenv("VDOLL_TEST_KILL") != nullptr, peekTest = getenv("VDOLL_TEST_PEEK") != nullptr;
    int peekRuns = 0, peekFights = 0, peekHits = 0, peekHidden = 0, peekHitFights = 0, peekFirstHidden = 0, peekFirstBody = 0, peekEarly = 0;
    std::vector<float> peekBehind;                       // (its copy behind it at each damage record, mm)
    const int hold = getenv("MGO1_VDOLL_KILL_HOLD") ? atoi(getenv("MGO1_VDOLL_KILL_HOLD")) : 0;
    std::vector<float> moved;                            // (VDOLL_TEST_KILL: the doll's way in the 2.75 s after each kill, mm)
    int allHits = 0, allHidden = 0, allFast = 0, allFights = 0, allHitFights = 0, allHiddenDecided = 0;
    int kHits[K_KINDS] = {}, kHidden[K_KINDS] = {}, kFights[K_KINDS] = {}, kHitFights[K_KINDS] = {}, kRuns[K_KINDS] = {};
    std::vector<double> kFirst[K_KINDS];                  // (each fight's first damage after the sight, ms)
    for (int m = 1; m < argc; ++m) {
        FILE* f = fopen(argv[m], "rb");
        if (!f) { perror(argv[m]); return 1; }
        fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
        if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 1;
        fclose(f);
        doll::Geom g;
        std::string err;
        if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("%s: not a geom: %s\n", argv[m], err.c_str()); return 1; }
        doll::NavMesh nav;
        std::string log;
        doll::NavConfig cfg;
        // ((v150-host beta 25, meshmargin) the walls' margin as the bodies' (MGO1_VDOLL_MESH_MARGIN): the corners' places where a player's
        // body can stand. With beta 25's mesh n011a's corner -4308 70 10877 hid him at -3301 72 11511, 37 mm off the cover's wall (a real
        // client is held 211 mm off): the doll's gate, which seeks his hiding places on its own mesh, had none there with the margin on,
        // and 1 damage record of 4927 reached him there (3 and 2 with MGO1_VDOLL_SEED 11 and 12; none in 11 runs with the switch at 0))
#ifdef DOLL_NAV_MARGIN
        { const char* mg = getenv("MGO1_VDOLL_MESH_MARGIN"); cfg.margin = !(mg && !strcmp(mg, "0")); }
#endif
        if (!nav.Build(g, cfg, log)) { printf("%s: no mesh\n", argv[m]); return 1; }
        gNav = &nav;
        // the body's own mesh of this map (a new page: built on its thread)
        static int page = 0;
        char key[32]; snprintf(key, sizeof key, "test%d", ++page);
        gKey = key;
        gMeshSaid = false;
        for (int w = 0; w < 6000 && !gMeshSaid; ++w) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        if (!gMeshSaid) { printf("%s: the bodies' mesh was not built\n", argv[m]); return 1; }
        std::vector<Corner> cs;
        if (peekTest) {
            // (the killer he cannot see) the player stands where the corner's doll stood, the doll starts in the cover
            gFps = 60.0;
            for (int i = 0, got = 0; i < corners * 3 && got < corners; ++i) {
                Corner c;
                if (!FindCorner(g, &c, K_CORNER)) continue;
                ++got; ++peekRuns;
                float pos[3] = {c.b[0], c.b[1], c.b[2]}, copy[3] = {c.b[0], c.b[1], c.b[2]};
                uint16_t facing = 0;
                vdoll::body::Self s;
                s.k = 0; s.slot = 1; s.team = 0; s.life = fr; s.pos = pos; s.facing = &facing; s.send = Send;
                gHave[0] = true; gPlayers[0] = vdoll::PlayerNow(); gPlayers[0].team = 0xFE;
                gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
                gHave[2] = true; gPlayers[2] = vdoll::PlayerNow();
                vdoll::PlayerNow& p = gPlayers[2];
                p.team = 1; p.object = p.alive = true; p.life = 200; p.rtt = rtt;
                memcpy(p.pos, c.d, sizeof p.pos); memcpy(p.sent, c.d, sizeof p.sent);
                gRecs.clear(); gSees = 0;
                std::vector<std::array<float, 3>> was;        // (the doll's place each frame: its copy follows 14 frames behind)
                bool fought = false, hit = false;
                long long sightAt = -1;
                for (long long t = fr; t < fr + 600; ++t) {
                    gNow = t;
                    p.sentAge = (int)((t - fr) % 6) * 17 + 80;
                    was.push_back({pos[0], pos[1], pos[2]});
                    const std::array<float, 3>& to = was[was.size() > 14 ? was.size() - 15 : 0];
                    const float dx = to[0] - copy[0], dy = to[1] - copy[1], dz = to[2] - copy[2], d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d > 0.f) { const float k = std::min(1.f, 70.f / d); copy[0] += dx * k; copy[1] += dy * k; copy[2] += dz * k; }
                    memcpy(gPlayers[1].pos, copy, sizeof copy);
                    const size_t r0 = gRecs.size();
                    const int seesBefore = gSees;
                    vdoll::body::Frame(t);
                    s.tick = t % 6 == 0;
                    vdoll::body::Alive(s, t);
                    if (gSees > seesBefore && !fought) { fought = true; ++peekFights; }
                    if (gSees > seesBefore) sightAt = t;
                    for (size_t k = r0; k < gRecs.size(); ++k) {
                        if (gRecs[k].slot != 2 || gRecs[k].r.size() < 9 || gRecs[k].r[0] != 0x04) continue;
                        ++peekHits;
                        if (!hit) peekBehind.push_back(std::hypot(copy[0] - pos[0], copy[2] - pos[2]));
                        // (what his game shows of it: from his eye, the copy's chest or head standing; or crouched, or its
                        // eye leaning either way: whatever its stance, something of it in his line)
                        const float he[3] = {c.d[0], c.d[1] + 750.f, c.d[2]};
                        const bool body = SeenFrom(he, copy);
                        bool any = body;
                        {
                            float lc[3], lh[3];
                            Part(copy, 50.f, lc); Part(copy, 310.f, lh);
                            any = any || gNav->Clear(he, lc) || gNav->Clear(he, lh);
                            const float yaw = std::atan2(c.d[0] - copy[0], c.d[2] - copy[2]);
                            for (int lean : {1, -1}) {
                                const float le[3] = {copy[0] + std::cos(yaw) * 490.f * lean, copy[1] + 750.f, copy[2] - std::sin(yaw) * 490.f * lean};
                                any = any || gNav->Clear(he, le);
                            }
                        }
                        if (!hit && !body) ++peekFirstBody;
                        if (!any) {
                            ++peekHidden;
                            if (!hit) ++peekFirstHidden;
                            if (sightAt >= 0 && t - sightAt < 60) ++peekEarly;   // (within a second of its sight: its hold's second)
                            if (getenv("VDOLL_TEST_SHOW")) printf("  %s: damage out with its copy %.0f mm behind it, nothing of the copy in his line\n", argv[m], std::hypot(copy[0] - pos[0], copy[2] - pos[2]));
                        }
                        hit = true;
                    }
                }
                if (hit) ++peekHitFights;
                vdoll::body::Down(0);
                fr += 610;
            }
            continue;
        }
        if (killTest) {
            // (the doll after its kill) a player standing in the open, dead of its fourth hit: where the doll is 2.75 s later
            gFps = 60.0;
            for (int i = 0, got = 0; i < corners * 2 && got < (corners + 1) / 2; ++i) {
                Corner c;
                if (!FindCorner(g, &c, K_OPEN)) continue;
                ++got;
                c.kind = K_STAND;
                float pos[3] = {c.d[0], c.d[1], c.d[2]};
                uint16_t facing = (uint16_t)lrintf(std::atan2(c.a[0] - pos[0], c.a[2] - pos[2]) / 6.2831853f * 65536.f);
                vdoll::body::Self s;
                s.k = 0; s.slot = 1; s.team = 0; s.life = fr; s.pos = pos; s.facing = &facing; s.send = Send;
                gHave[0] = true; gPlayers[0] = vdoll::PlayerNow(); gPlayers[0].team = 0xFE;
                gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
                gHave[2] = true; gPlayers[2] = vdoll::PlayerNow();
                vdoll::PlayerNow& p = gPlayers[2];
                p.team = 1; p.object = p.alive = true; p.life = 200; p.rtt = rtt;
                memcpy(p.pos, c.a, sizeof p.pos); memcpy(p.sent, c.a, sizeof p.sent);
                gRecs.clear();
                int got4 = 0; long long deadAt = -1; float at[3] = {0, 0, 0};
                for (long long t = fr; t < fr + 900; ++t) {
                    gNow = t;
                    p.sentAge = (int)((t - fr) % 6) * 17 + 80;
                    memcpy(gPlayers[1].pos, pos, sizeof pos);
                    const size_t r0 = gRecs.size();
                    vdoll::body::Frame(t);
                    s.tick = t % 6 == 0;
                    vdoll::body::Alive(s, t);
                    for (size_t k = r0; k < gRecs.size(); ++k) if (gRecs[k].slot == 2 && gRecs[k].r.size() >= 9 && gRecs[k].r[0] == 0x04) ++got4;
                    if (deadAt < 0 && got4 >= 4) { deadAt = t; p.alive = false; p.life = 0; memcpy(at, pos, sizeof at); }
                    if (deadAt >= 0 && t - deadAt >= 165) { moved.push_back(std::hypot(pos[0] - at[0], pos[2] - at[2])); break; }
                }
                vdoll::body::Down(0);
                fr += 910;
            }
            continue;
        }
        if (thingTest) {                                 // (the open corners, a crate hiding B: the "corner" kind; the open kind the control)
            for (int i = 0; i < corners * 6 && (int)cs.size() < corners; ++i) {
                Corner c;
                if (!FindCorner(g, &c, K_OPEN)) continue;
                Box x;
                BoxFor(c, &x);
                gBoxes.assign(1, x);
                const bool ok = SeenByDoll(c.d, c.a) && SeenByDoll(c.d, c.p0) && !SeenByDoll(c.d, c.b);   // (B alone behind it)
                gBoxes.clear();
                if (!ok) continue;
                c.kind = K_CORNER;
                cs.push_back(c);
            }
        } else
        for (int i = 0; i < corners * 3 && (int)cs.size() < corners; ++i) { Corner c; if (FindCorner(g, &c, K_CORNER)) cs.push_back(c); }
        const size_t straight = cs.size();
        for (int kind = K_TURN; kind < K_KINDS; ++kind) {
            if (thingTest && kind != K_OPEN) continue;
            int got = 0;
            for (int i = 0; i < corners * 2 && got < (corners + 1) / 2; ++i) { Corner c; if (FindCorner(g, &c, kind)) { cs.push_back(c); ++got; } }
        }
        int hits = 0, hidden = 0, fast = 0, fights = 0, hitFights = 0, hiddenDecided = 0;
        for (double fps : {60.0, 37.0}) {
            gFps = fps;
            const double copyLag = lat + 1000.0 / fps + (fps < 50.0 ? 100.0 : 0.0);
            for (const Corner& c : cs) {
                gBoxes.clear();
                if (thingTest && c.kind == K_CORNER) { Box x; BoxFor(c, &x); gBoxes.push_back(x); }
                float pos[3] = {c.d[0], c.d[1], c.d[2]};
                const float yaw = std::atan2(c.a[0] - pos[0], c.a[2] - pos[2]);
                uint16_t facing = (uint16_t)lrintf(yaw / 6.2831853f * 65536.f);
                vdoll::body::Self s;
                s.k = 0; s.slot = 1; s.team = 0; s.life = fr; s.pos = pos; s.facing = &facing; s.send = Send;
                gHave[0] = true; gPlayers[0] = vdoll::PlayerNow(); gPlayers[0].team = 0xFE;
                gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
                gHave[2] = false;
                gRecs.clear(); gSees = 0;
                const long long start = fr + 30;              // (the player comes at once into the doll's sight, at p0)
                double lastSent = -1e9, arrive = -1e9;
                float sent[3] = {c.p0[0], c.p0[1], c.p0[2]};
                long long sight = -1;
                bool hitThis = false, firstHit = true;
                const long long frames = (long long)(3.0 * fps);
                for (long long t = fr; t < fr + frames; ++t) {
                    gNow = t;
                    const double now = Ms(t), rel = now - Ms(start);
                    if (t >= start) {
                        // his records: sent every 100 ms by his game (his place then), here lat ms later
                        if (rel - lastSent >= 100.0) { lastSent = rel; }
                        if (rel - lat >= 0.0) {
                            const double sentAt = std::floor((rel - lat) / 100.0) * 100.0;
                            float sp[3]; Own(c, sentAt, sp);
                            if (sentAt + lat > arrive || arrive < -1e8) { if (std::fabs(sentAt + lat - arrive) > 0.5) { memcpy(sent, sp, sizeof sent); arrive = sentAt + lat; } }
                        }
                        gHave[2] = true;
                        vdoll::PlayerNow& p = gPlayers[2];
                        p.team = 1; p.object = p.alive = true; p.life = 200;
                        Own(c, rel - copyLag, p.pos);                  // (his copy: as late as his records and the host's frames)
                        memcpy(p.sent, sent, sizeof p.sent);
                        p.sentAge = arrive > -1e8 ? (int)(rel - arrive) : -1;
                        p.rtt = rtt;                                   // (the session's own round trip to him)
                    }
                    memcpy(gPlayers[1].pos, pos, sizeof pos);          // (the doll's own copy on the host: where it is)
                    const size_t r0 = gRecs.size();
                    const int seesBefore = gSees;
                    vdoll::body::Frame(t);
                    s.tick = t % 6 == 0;
                    vdoll::body::Alive(s, t);
                    if (gSees > seesBefore && sight < 0) { sight = t; ++fights; }
                    for (size_t i = r0; i < gRecs.size(); ++i) {
                        const Rec& r = gRecs[i];
                        if (r.slot != 2 || r.r.size() < 9 || r.r[0] != 0x04) continue;
                        ++hits; hitThis = true; ++kHits[c.kind];
                        // it reaches his game after the host's game passed it on and lat ms more: his own place then, and when
                        // the doll decided it (this tick at the latest)
                        float own[3], ownDecided[3];
                        relayRnd ^= relayRnd << 13; relayRnd ^= relayRnd >> 17; relayRnd ^= relayRnd << 5;
                        const double relay = (double)(relayRnd % (unsigned)(relayMax + 1)) * 1000.0 / fps;
                        Own(c, rel + relay + lat, own);
                        Own(c, rel - 1000.0 / fps * 6.0, ownDecided);
                        const bool seen = SeenByDoll(pos, own);
                        if (!seen) { ++hidden; ++kHidden[c.kind]; }
                        if (!SeenByDoll(pos, ownDecided)) ++hiddenDecided;
                        if (firstHit && sight >= 0 && Ms(t) - Ms(sight) < 250.0) ++fast;
                        if (firstHit && sight >= 0) kFirst[c.kind].push_back(Ms(t) - Ms(sight));
                        if (getenv("VDOLL_TEST_VERBOSE") || (!seen && getenv("VDOLL_TEST_SHOW")))
                            printf("  %s, %s, %.0f fps: damage out %.0f ms after he started, his own place %s when it reaches him (%.0f %.0f %.0f), %.0f ms after the sight\n",
                                   argv[m], kKind[c.kind], fps, rel, seen ? "seen" : "OUT OF THE DOLL'S LINE", own[0], own[1], own[2], sight >= 0 ? Ms(t) - Ms(sight) : -1.0);
                        firstHit = false;
                    }
                }
                if (hitThis) ++hitFights;
                ++kRuns[c.kind];
                if (sight >= 0) { ++kFights[c.kind]; if (hitThis) ++kHitFights[c.kind]; }
                vdoll::body::Down(0);
                fr += frames + 10;
            }
        }
        printf("%s: %zu corners, %d fights (both paces), %d with a hit; %d damage records, %d reached him while his own place was out of the doll's line (%d decided so), %d first hits within 250 ms of the sight\n",
               argv[m], cs.size(), fights, hitFights, hits, hidden, hiddenDecided, fast);
        if (straight < 3) Fail("%s: only %zu corners found", argv[m], straight);
        allHits += hits; allHidden += hidden; allFast += fast; allFights += fights; allHitFights += hitFights; allHiddenDecided += hiddenDecided;
    }
    if (peekTest) {
        std::sort(peekBehind.begin(), peekBehind.end());
        const bool on = !(getenv("MGO1_VDOLL_SHOWN") && !strcmp(getenv("MGO1_VDOLL_SHOWN"), "0"));
        printf("%d runs, %d fights, %d with a hit; the fight's first damage went out with nothing of the doll's copy in his eye's line in %d (its standing chest and head out of it in %d: it leans), the copy %.0f mm behind the doll then (median; the most %.0f); %d damage records in all, %d with nothing of it in his line, %d of them within a second of its sight (MGO1_VDOLL_SHOWN %s)\n",
               peekRuns, peekFights, peekHitFights, peekFirstHidden, peekFirstBody, peekBehind.empty() ? 0.f : peekBehind[peekBehind.size() / 2], peekBehind.empty() ? 0.f : peekBehind.back(), peekHits, peekHidden, peekEarly, on ? "on" : "0");
        printf("%s\n", vdoll::body::Status(0).c_str());
        if (peekFights < 20) Fail("only %d fights", peekFights);
        if (peekHitFights * 2 < peekFights) Fail("a hit in only %d fights of %d (the doll can still hit a player it comes upon)", peekHitFights, peekFights);
        // (a doll that sees a part of him from where nothing of it shows to him fires after a second of aim all the same:
        // counted apart, in the body's status line too)
        if (on && peekEarly) Fail("%d damage records went out within a second of its sight with nothing of the doll's own copy in the player's line", peekEarly);
        if (!on && getenv("MGO1_VDOLL_REFLEX") && !strcmp(getenv("MGO1_VDOLL_REFLEX"), "0") && !peekEarly) Fail("as beta 20 no damage went out with the copy hidden: the test does not show the defect");
        printf("%s: the killer he cannot see\n", gFails ? "FAIL" : "PASS");
        return gFails ? 1 : 0;
    }
    if (killTest) {
        std::sort(moved.begin(), moved.end());
        int far = 0, still = 0;
        for (float m : moved) { if (m > 1000.f) ++far; if (m < 50.f) ++still; }
        printf("%zu kills of a player standing in the open; the doll 2.75 s after: %.1f m from where it shot (median; the least %.2f m, the most %.1f m), %d over 1 m, %d within 50 mm (MGO1_VDOLL_KILL_HOLD=%d)\n",
               moved.size(), moved.empty() ? 0.f : moved[moved.size() / 2] / 1000.f, moved.empty() ? 0.f : moved.front() / 1000.f, moved.empty() ? 0.f : moved.back() / 1000.f, far, still, hold);
        if (moved.size() < 20) Fail("only %zu kills", moved.size());
        else if (hold >= 2750) { if (still != (int)moved.size()) Fail("%d of %zu kills: the doll moved in its hold", (int)moved.size() - still, moved.size()); }
        else if (hold == 0 && far * 2 < (int)moved.size()) Fail("the doll ran on over 1 m after only %d of %zu kills (the before)", far, moved.size());
        printf("%s: the doll after its kill\n", gFails ? "FAIL" : "PASS");
        return gFails ? 1 : 0;
    }
    printf("all: %d fights, %d with a hit, %d damage records, %d out of the doll's line where he really was (%d decided so), %d first hits within 250 ms\n",
           allFights, allHitFights, allHits, allHidden, allHiddenDecided, allFast);
    for (int k = 0; k < K_KINDS; ++k) {
        std::sort(kFirst[k].begin(), kFirst[k].end());
        printf("  %s: %d runs, %d fights, %d with a hit, %d damage records, %d out of the doll's line where he really was; the sight to the first damage %.0f ms (median)\n",
               kKind[k], kRuns[k], kFights[k], kHitFights[k], kHits[k], kHidden[k], kFirst[k].empty() ? 0.0 : kFirst[k][kFirst[k].size() / 2]);
    }
    printf("%s\n", vdoll::body::Status(0).c_str());
    if (allHidden) Fail("%d damage records reached the player while his own place was out of the doll's line", allHidden);
    // the controls: what the gate must leave to the dolls
    gBoxes.clear();
    for (int k : {K_OPEN, K_STAND})
        if (thingTest && k == K_STAND) continue;          // (VDOLL_TEST_THING: the open kind alone is its control)
        else if (kFights[k] < 20) Fail("only %d fights of the kind %s", kFights[k], kKind[k]);
        else if (kHitFights[k] * 10 < kFights[k] * 6) Fail("%s: a hit in only %d fights of %d (the doll can still hit a player in the open)", kKind[k], kHitFights[k], kFights[k]);
    if (allFast) Fail("%d fights' first damage within 250 ms of the doll's sight", allFast);
    if (allFights < 10) Fail("only %d fights", allFights);
    if (allHits == 0) Fail("no hit at all (the doll can still kill a player in its line)");
    printf("%s: the cover checks (4)%s\n", gFails ? "FAIL" : "PASS", thingTest ? ", the cover a thing of the game (VDOLL_TEST_THING)" : "");
    return gFails ? 1 : 0;
}
