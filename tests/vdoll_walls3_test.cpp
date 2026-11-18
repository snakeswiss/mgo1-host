// (v150-host beta 24, walls3) "They still can shoot through walls" (a player's third report, the owner's server on beta 23,
// MGO1_VDOLL_CONTROLS=player): what beta 21's cover test (tests/vdoll_cover_test.cpp: bullets in the direct controls, the
// stage's collision alone) does not reach (docs/VIRTUAL_DOLL.md 39, reports/vdoll_walls3.md). A map's collision file; the host's
// game's own line test for its things (Host::gameLine) stood in for by boxes the test places. One doll at level 10 as a player.
//   M. its gun under a cover's top (MGO1_VDOLL_MUZZLE): places on the map where the doll's standing eye sees the chest of a
//      player standing still 4 to 9.5 m away (the auto aim's reach, where a player fires from the third person) over a cover
//      of the map and its gun's line from its muzzle (0.45 m ahead of its hips, 0.38 m over them) to his chest is stopped
//      (from 8 m its crouched eye seeing nothing of him: it stands). Checked: no damage record whose bullet left from the third
//      person with the cover between its muzzle and the point the bullet met (it may still hit him from its first person view,
//      as a player raises his view over a cover); and, the control, places in the open with every line clear: a hit in 6 runs
//      of 10 at least.
//   R. its RPG-7 (a second doll, MGO1_VDOLL_WEAPONS "ak47,ak47+rpg7") at a player 16 to 24 m away on open ground, a crate of
//      the game (a box 1.3 m high, hardness 220) 2.5 m before him: it sees his head over it, its rocket aimed at his chest
//      meets the crate (the game's rocket goes off on the first face or thing it meets) and its blast's line to him crosses
//      it: no rocket damage reaches him. Control, the same with no crate: the rocket's damage reaches him.
//   F. its frag grenade at two enemies 1 m apart 9 to 16 m before it (blast test part 7's places): the second inside a crate
//      of the game: not wounded (the game's blast asks its line test of the stage and the things); and in another run the
//      first's copy in the open with the place he last sent 2 m behind it inside a crate: not wounded (judged where he last
//      said he is, as the rocket's and the claymore's blasts); the other one, in the open, wounded each time (the control).
// VDOLL_WALLS3_TEST_BETA23=1 (MGO1_VDOLL_MUZZLE=0 MGO1_VDOLL_BLAST_COVER=0, beta 23's behaviour): M, R and F FAIL. Built on beta
// 23's body (b16 3363fd4) the same parts fail.
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_walls3_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o /tmp/vdoll_walls3_test
//   /tmp/vdoll_walls3_test <map.geom> ...
// (the maps' collision files come from the owner's disc: python3 -I tools/stage_dat.py extract <STAGE_N.DAT> n003a <dir>; they
// are never put in the repository)
#include "vdoll_body.h"
#include "doll_geom.h"
#include "doll_nav.h"
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
static int gRockets = 0;
static long long gFrame = 0;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, ": fires its RPG-7")) ++gRockets;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("f%lld %s\n", gFrame, b);
}
static std::string gKey = "w3_0";
static bool MapNow(std::string* page, int* extra) { *page = gKey; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 0; }                         // (deathmatch: everyone an enemy)
struct Sent { long long frame; int slot, kind; std::vector<uint8_t> rec; };
static std::vector<Sent> gSent;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gSent.push_back({gFrame, slot, kind, rec}); }

// the things of the host's game the collision file does not hold: boxes (a crate, a vehicle), each with its hardness
struct Box { float lo[3], hi[3]; int hardness = 220; };
static std::vector<Box> gBoxes;
static int GameLine(const float* a, const float* b, bool, vdoll::LineHit* hit) {
    int best = -1;
    float bestT = 2.f;
    for (size_t i = 0; i < gBoxes.size(); ++i) {
        float t0 = 0.f, t1 = 1.f;
        bool in = true;
        for (int k = 0; k < 3 && in; ++k) {
            const float d = b[k] - a[k];
            if (std::fabs(d) < 1e-6f) { in = a[k] >= gBoxes[i].lo[k] && a[k] <= gBoxes[i].hi[k]; continue; }
            float u = (gBoxes[i].lo[k] - a[k]) / d, v = (gBoxes[i].hi[k] - a[k]) / d;
            if (u > v) std::swap(u, v);
            t0 = std::max(t0, u); t1 = std::min(t1, v);
            in = t0 <= t1;
        }
        if (in && t0 < bestT) { bestT = t0; best = (int)i; }
    }
    if (best < 0) return 0;
    if (hit) {
        *hit = vdoll::LineHit();
        for (int k = 0; k < 3; ++k) hit->at[k] = a[k] + (b[k] - a[k]) * bestT;
        hit->attr = 0x42; hit->object = true; hit->objectId = 0x01000000u + (uint32_t)best; hit->hardness = gBoxes[best].hardness;
    }
    return 1;
}

static int gFails = 0;
static void Fail(const char* fmt, ...) {
    ++gFails;
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}
static unsigned gRnd = 0x2545F491u;
static unsigned Rnd() { gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5; return gRnd; }
static doll::NavMesh* gNav = nullptr;
const float kHips = 800.f, kEye = 750.f, kLowEye = 300.f, kMuzzleUp = 380.f, kMuzzleAhead = 450.f;
static void Up(const float* feet, float up, float* out) { out[0] = feet[0]; out[1] = feet[1] + up; out[2] = feet[2]; }
// the standing muzzle of a doll whose hips are at `hips`, facing yaw, and the point of its hips' column at that height
static void Muzzle(const float* hips, float yaw, float* m, float* col) {
    col[0] = hips[0]; col[1] = hips[1] + kMuzzleUp; col[2] = hips[2];
    m[0] = hips[0] + std::sin(yaw) * kMuzzleAhead; m[1] = col[1]; m[2] = hips[2] + std::cos(yaw) * kMuzzleAhead;
}
static bool MuzzleStopped(const float* hips, float yaw, const float* to) {
    float m[3], col[3];
    Muzzle(hips, yaw, m, col);
    return !gNav->Clear(col, m) || !gNav->Clear(m, to);
}

// a run: doll k (seat slot) kept at `feet` facing `toward`, for `frames`; each bullet's state (from the view or not) and facing
struct Shot { long long frame; bool view; float yaw; };
struct RunOut { std::vector<Shot> shots; };
static RunOut Run(int k, int slot, const float* feet, const float* toward, int frames) {
    float pos[3] = {feet[0], feet[1] + kHips, feet[2]};
    const float yaw = std::atan2(toward[0] - pos[0], toward[2] - pos[2]);
    uint16_t facing = (uint16_t)lrintf(yaw / 6.2831853f * 65536.f);
    vdoll::body::Self s;
    s.k = k; s.slot = slot; s.team = 0; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
    gHave[slot] = true; gPlayers[slot] = vdoll::PlayerNow(); gPlayers[slot].team = 0; gPlayers[slot].object = gPlayers[slot].alive = true; gPlayers[slot].life = 200;
    gSent.clear();
    RunOut out;
    long long lastBullet = -1000;
    for (long long end = gFrame + frames; gFrame < end; ++gFrame) {
        pos[0] = feet[0]; pos[2] = feet[2]; pos[1] = feet[1] + kHips;    // (kept where the run put it)
        memcpy(gPlayers[slot].pos, pos, sizeof pos);
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
        vdoll::body::AimInfo ai;
        if (vdoll::body::Aim(k, &ai) && ai.lastBullet != lastBullet && ai.lastBullet >= 0) {
            lastBullet = ai.lastBullet;
            out.shots.push_back({gFrame, ai.state == 4, ai.yaw});
        }
    }
    if (getenv("VDOLL_TEST_RUNS")) printf("  run of doll %d at %.0f %.0f %.0f: frames to %lld, %zu bullets, %zu records\n", k + 1, feet[0], feet[1], feet[2], gFrame, out.shots.size(), gSent.size());
    vdoll::body::Down(k);
    gHave[slot] = false;
    for (int i = 0; i < 90; ++i) { vdoll::body::Frame(gFrame); ++gFrame; }   // (a while apart: the next run's fight a new one)
    return out;
}
static void Player(int id, const float* feet) {
    vdoll::PlayerNow& p = gPlayers[id];
    p = vdoll::PlayerNow();
    p.team = 1; p.object = p.alive = true; p.life = 200; p.rtt = 100;
    Up(feet, kHips, p.pos);
    memcpy(p.sent, p.pos, sizeof p.sent); p.sentAge = 50;   // (standing still: his record where he is)
    gHave[id] = true;
}
// each damage record to `id` of a run: the bullet's end (its effect record) and the weapon byte of its damage record
struct Dmg { long long frame; float end[3]; int weapon; };
static std::vector<Dmg> Damage(int id) {
    std::vector<Dmg> d;
    const float* last = nullptr;
    float e[3] = {0, 0, 0};
    for (const Sent& m : gSent) {
        if (m.slot != id || m.rec.empty()) continue;
        if (m.kind == 2 && m.rec[0] == 0x14 && m.rec.size() >= 11) {
            for (int k = 0; k < 3; ++k) e[k] = (float)(int16_t)(m.rec[5 + 2 * k] | (m.rec[6 + 2 * k] << 8)) * 10.f;
            last = e;
        }
        if (m.kind == 0 && m.rec[0] == 0x04 && m.rec.size() >= 9) {
            Dmg x; x.frame = m.frame; x.weapon = m.rec[6];
            if (last) memcpy(x.end, last, sizeof x.end); else memset(x.end, 0, sizeof x.end);
            d.push_back(x);
            last = nullptr;
        }
    }
    return d;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_walls3_test <map.geom> ...\n"); return 2; }
    const bool beta23 = getenv("VDOLL_WALLS3_TEST_BETA23") != nullptr;
    setenv("MGO1_VDOLL_LEVEL", "10", 1);
    setenv("MGO1_VDOLL_CONTROLS", "player", 1);            // (the owner's server)
    setenv("MGO1_VDOLL_WEAPONS", "ak47,ak47+rpg7", 1);     // (doll 1 its gun; doll 2 its gun and the RPG-7)
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "24", 1);
    if (beta23) { setenv("MGO1_VDOLL_MUZZLE", "0", 1); setenv("MGO1_VDOLL_BLAST_COVER", "0", 1); printf("(MGO1_VDOLL_MUZZLE=0 MGO1_VDOLL_BLAST_COVER=0: beta 23's behaviour)\n"); }
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    h.gameLine = GameLine;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0] = vdoll::PlayerNow(); gPlayers[0].team = 0xFE;   // (the host's own player: a spectator)
    int mPlaces = 0, mDmg = 0, mBad = 0, mView = 0, mHitRuns = 0, oRuns = 0, oHitRuns = 0, oViewRuns = 0;
    int rRuns = 0, rFired = 0, rDmg = 0, rcRuns = 0, rcDmg = 0;
    int fRuns = 0, fThing = 0, fRecRuns = 0, fRec = 0, fCtl = 0;
    for (int mi = 1; mi < argc; ++mi) {
        gGeom.clear();
        FILE* f = fopen(argv[mi], "rb");
        if (!f) { perror(argv[mi]); return 1; }
        uint8_t buf[65536]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) gGeom.insert(gGeom.end(), buf, buf + n);
        fclose(f);
        doll::Geom g; std::string err, log;
        doll::NavMesh nav;
        if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err) || !nav.Build(g, doll::NavConfig(), log)) { printf("%s: not read\n", argv[mi]); return 1; }
        gNav = &nav;
        char key[32]; snprintf(key, sizeof key, "w3_%d", mi); gKey = key;
        gMeshSaid = false;
        for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(gFrame); ++gFrame; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        if (!gMeshSaid) { printf("%s: the bodies' mesh was not built\n", argv[mi]); return 1; }
        const char* name = strrchr(argv[mi], '/') ? strrchr(argv[mi], '/') + 1 : argv[mi];
        gRnd = 0x2545F491u + (unsigned)mi * 7919u;
        gBoxes.clear();

        // ---- M: its gun under a cover's top; and the open control ----
        int places = 0, opens = 0, dmg = 0, bad = 0, view = 0, hitRuns = 0, oHit = 0, oView = 0;
        for (int tries = 0; tries < 300000 && (places < 6 || opens < 3); ++tries) {
            float d[3], p[3];
            if (!nav.RandomPoint(nullptr, 0.f, d, Rnd) || nav.AreaAt(d) != doll::AREA_STAND) continue;
            if (!nav.RandomPoint(d, 10000.f, p, Rnd) || nav.AreaAt(p) != doll::AREA_STAND) continue;
            const float dist = std::hypot(p[0] - d[0], p[2] - d[2]);
            if (dist < 4000.f || dist > 9500.f || std::fabs(p[1] - d[1]) > 300.f) continue;
            float eye[3], low[3], hips[3], pc[3], ph[3];
            Up(d, kHips + kEye, eye); Up(d, kHips + kLowEye, low); Up(d, kHips, hips); Up(p, kHips + 350.f, pc); Up(p, kHips + 850.f, ph);
            const float yaw = std::atan2(p[0] - d[0], p[2] - d[2]);
            const bool sees = nav.Clear(eye, pc) || nav.Clear(eye, ph);
            // (its standing eye sees him; from 8 m its crouched eye nothing, so that it stands: within 8 m a doll never crouches)
            if (!sees || (dist >= 8000.f && (nav.Clear(low, pc) || nav.Clear(low, ph)))) continue;
            // its gun's line to every height of him under his head stopped (where its third person bullet goes: his chest within
            // 10 m, level from its muzzle beyond): the cover before its gun; or all of them clear (the open control)
            int stopped = 0, heights = 0;
            for (float up : {-400.f, -100.f, 200.f, 350.f, 500.f}) { float q[3]; Up(p, kHips + up, q); ++heights; if (MuzzleStopped(hips, yaw, q)) ++stopped; }
            { float q[3] = {p[0], d[1] + kHips + kMuzzleUp, p[2]}; ++heights; if (MuzzleStopped(hips, yaw, q)) ++stopped; }
            // (the cover before its gun: its eye sees his chest, where its third person bullet goes within 10 m, its gun's line to
            // his chest stopped; the open control: every line clear)
            const bool low_ = nav.Clear(eye, pc) && MuzzleStopped(hips, yaw, pc), open = stopped == 0 && nav.Clear(eye, pc) && nav.Clear(eye, ph);
            (void)heights;
            if (!(low_ && places < 6) && !(open && opens < 3)) continue;
            Player(2, p);
            const RunOut r = Run(0, 1, d, p, 420);
            const std::vector<Dmg> got = Damage(2);
            int b = 0, v = 0;
            for (const Dmg& x : got) {
                const Shot* sh = nullptr;                   // (the last bullet before its record)
                for (const Shot& s : r.shots) if (s.frame <= x.frame) sh = &s;
                if (!sh) continue;
                if (sh->view) { ++v; continue; }
                if (MuzzleStopped(hips, sh->yaw, x.end)) {
                    ++b;
                    if (getenv("VDOLL_TEST_SHOW")) printf("  %s: a third person bullet through the cover before its gun, its end %.0f %.0f %.0f (%.1f m)\n", name, x.end[0], x.end[1], x.end[2], dist / 1000.f);
                }
            }
            vdoll::body::AimInfo ai; (void)ai;
            if (low_) { ++places; dmg += (int)got.size(); bad += b; view += v; if (!got.empty()) ++hitRuns; }
            else {
                ++opens; if (!got.empty()) ++oHit;
                const std::string st = vdoll::body::Status(0);
                (void)st;
            }
            if (open && v > 0 && got.size() == (size_t)v) ++oView;   // (in the open every hit from the view: noted, not a failure)
            gHave[2] = false;
        }
        printf("%s M: %d places with its gun under a cover's top: %d damage records, %d from the third person through the cover before its gun, %d from its view (%d runs with a hit); %d open places, a hit in %d\n",
               name, places, dmg, bad, view, hitRuns, opens, oHit);
        mPlaces += places; mDmg += dmg; mBad += bad; mView += view; mHitRuns += hitRuns; oRuns += opens; oHitRuns += oHit; oViewRuns += oView;

        // ---- R: its RPG-7 at a player behind a crate of the game ----
        {
            bool found = false;
            float d[3] = {0, 0, 0}, p[3] = {0, 0, 0};
            for (int tries = 0; tries < 200000 && !found; ++tries) {
                if (!nav.RandomPoint(nullptr, 0.f, d, Rnd) || nav.AreaAt(d) != doll::AREA_STAND) continue;
                if (!nav.RandomPoint(d, 24000.f, p, Rnd) || nav.AreaAt(p) != doll::AREA_STAND) continue;
                const float dist = std::hypot(p[0] - d[0], p[2] - d[2]);
                if (dist < 16000.f || dist > 24000.f || std::fabs(p[1] - d[1]) > 300.f) continue;
                bool ok = true;                             // (open ground: every height of him from both its eyes and its muzzle)
                for (float h1 : {kHips + kEye, kHips + kLowEye, kHips + kMuzzleUp}) for (float h2 : {kHips - 600.f, kHips, kHips + 350.f, kHips + 850.f}) {
                    float a[3], b[3]; Up(d, h1, a); Up(p, h2, b); ok = ok && nav.Clear(a, b);
                }
                // (and 8 m behind him: the rocket that misses goes on)
                const float ux = (p[0] - d[0]) / dist, uz = (p[2] - d[2]) / dist;
                { float a[3], b[3]; Up(p, kHips, a); const float q[3] = {p[0] + ux * 8000.f, p[1] + kHips, p[2] + uz * 8000.f}; memcpy(b, q, sizeof b); ok = ok && nav.Clear(a, b); }
                found = ok;
            }
            if (!found) printf("%s R: no open ground 16 to 24 m found\n", name);
            else {
                const float dist = std::hypot(p[0] - d[0], p[2] - d[2]), ux = (p[0] - d[0]) / dist, uz = (p[2] - d[2]) / dist;
                for (int withBox = 1; withBox >= 0; --withBox) {
                    gBoxes.clear();
                    if (withBox) {                            // (a crate 2.5 m before him: 2.4 m across, 0.4 m deep, 1.3 m high)
                        const float c[3] = {p[0] - ux * 2500.f, p[1], p[2] - uz * 2500.f};
                        Box bx;
                        bx.lo[0] = c[0] - std::fabs(uz) * 1200.f - std::fabs(ux) * 200.f; bx.hi[0] = c[0] + std::fabs(uz) * 1200.f + std::fabs(ux) * 200.f;
                        bx.lo[2] = c[2] - std::fabs(ux) * 1200.f - std::fabs(uz) * 200.f; bx.hi[2] = c[2] + std::fabs(ux) * 1200.f + std::fabs(uz) * 200.f;
                        bx.lo[1] = p[1] - 200.f; bx.hi[1] = p[1] + 1300.f;
                        gBoxes.push_back(bx);
                    }
                    Player(3, p);
                    const int before = gRockets;
                    Run(1, 4, d, p, 720);
                    int got = 0;
                    for (const Dmg& x : Damage(3)) if (x.weapon == 0x11) ++got;
                    printf("%s R: %s, %.1f m: %d rockets fired, %d rocket damage records reached him\n", name, withBox ? "a crate 2.5 m before him" : "no crate (the control)", dist / 1000.f, gRockets - before, got);
                    if (withBox) { ++rRuns; if (gRockets > before) ++rFired; rDmg += got; }
                    else { ++rcRuns; rcDmg += got; }
                    gHave[3] = false;
                }
                gBoxes.clear();
            }
        }

        // ---- F: its frag grenade: the second of a pair inside a crate; the first's record inside a crate ----
        {
            // places: a doll's place, and the pair 9 to 16 m before it on its floor, the first in its eyes' sight
            for (int tries = 0, runs = 0; tries < 200000 && runs < 2; ++tries) {
                float d[3], p[3];
                if (!nav.RandomPoint(nullptr, 0.f, d, Rnd) || nav.AreaAt(d) != doll::AREA_STAND) continue;
                if (!nav.RandomPoint(d, 16000.f, p, Rnd) || nav.AreaAt(p) != doll::AREA_STAND) continue;
                const float dist = std::hypot(p[0] - d[0], p[2] - d[2]);
                if (dist < 9000.f || dist > 16000.f || std::fabs(p[1] - d[1]) > 300.f) continue;
                const float ux = (p[0] - d[0]) / dist, uz = (p[2] - d[2]) / dist;
                float q[3] = {p[0] + uz * 1000.f, p[1] + 500.f, p[2] - ux * 1000.f};   // (the second 1 m aside)
                const float gq = nav.GroundBelow(q, 1500.f);
                if (gq < -1e29f || std::fabs(gq - p[1]) > 300.f) continue;
                q[1] = gq;
                float r[3] = {p[0] + ux * 2000.f, p[1] + 500.f, p[2] + uz * 2000.f};   // (the first's record 2 m behind him)
                const float gr = nav.GroundBelow(r, 1500.f);
                if (gr < -1e29f || std::fabs(gr - p[1]) > 300.f) continue;
                r[1] = gr;
                float eye[3], pc[3], qc[3], rc[3];
                Up(d, kHips + kEye, eye); Up(p, kHips + 350.f, pc); Up(q, kHips + 350.f, qc); Up(r, kHips + 350.f, rc);
                if (!nav.Clear(eye, pc) || !nav.Clear(pc, qc) || !nav.Clear(pc, rc)) continue;
                const bool recRun = runs == 1;
                // the crate round the one not to be wounded (0.5 m about his column, his feet to 2 m up)
                const float* in = recRun ? r : q;
                Box bx;
                for (int k : {0, 2}) { bx.lo[k] = in[k] - 500.f; bx.hi[k] = in[k] + 500.f; }
                bx.lo[1] = in[1] - 100.f; bx.hi[1] = in[1] + 2000.f;
                gBoxes.assign(1, bx);
                Player(2, p); Player(3, q);
                if (recRun) { Up(r, kHips, gPlayers[2].sent); }   // (his copy in the open, his record inside the crate)
                Run(0, 1, d, p, 720);
                int w2 = 0, w3 = 0;
                for (const Dmg& x : Damage(2)) if (x.weapon == 0x13) ++w2;
                for (const Dmg& x : Damage(3)) if (x.weapon == 0x13) ++w3;
                gHave[2] = gHave[3] = false;
                gBoxes.clear();
                const int ctl = recRun ? w3 : w2, kept = recRun ? w2 : w3;
                if (getenv("VDOLL_TEST_RUNS")) printf("  F try (%s): wounds %d and %d\n", recRun ? "record" : "thing", w2, w3);
                if (ctl == 0) continue;                      // (no grenade, or its blast out of reach of the control: another place)
                ++runs;
                printf("%s F: %s, %.1f m: the one in the open wounded %d times, the other %d\n", name,
                       recRun ? "the first's copy in the open, the place he last sent 2 m behind it inside a crate" : "the second of the pair inside a crate", dist / 1000.f, ctl, kept);
                ++fCtl;
                if (recRun) { ++fRecRuns; fRec += kept; } else { ++fRuns; fThing += kept; }
            }
        }
    }
    printf("all M: %d places, %d damage records, %d from the third person through the cover before its gun, %d from its view, %d runs with a hit; open: %d runs, a hit in %d\n",
           mPlaces, mDmg, mBad, mView, mHitRuns, oRuns, oHitRuns);
    printf("all R: %d runs with a crate (%d with a rocket fired), %d rocket damage records reached him; %d runs with no crate, %d\n", rRuns, rFired, rDmg, rcRuns, rcDmg);
    printf("all F: %d runs with the second inside a crate, %d wounds of it; %d runs with the first's record inside a crate, %d wounds of him; the control wounded in %d runs\n",
           fRuns, fThing, fRecRuns, fRec, fCtl);
    printf("%s\n", vdoll::body::Status(0).c_str());
    if (mPlaces < 10) Fail("M: only %d places with its gun under a cover's top", mPlaces);
    if (mBad) Fail("M: %d damage records from the third person through the cover before its gun", mBad);
    if (oRuns < 10) Fail("M: only %d open places", oRuns);
    else if (oHitRuns * 10 < oRuns * 6) Fail("M: a hit in only %d open runs of %d (the doll can still hit a player in the open)", oHitRuns, oRuns);
    if (rFired < 3) Fail("R: a rocket fired in only %d runs with a crate", rFired);
    if (rDmg) Fail("R: %d rocket damage records reached a player behind a crate", rDmg);
    if (rcRuns < 3 || rcDmg == 0) Fail("R: the control's rocket never reached him (%d runs)", rcRuns);
    if (fRuns < 3 || fRecRuns < 3) Fail("F: only %d and %d grenade runs", fRuns, fRecRuns);
    if (fThing) Fail("F: %d grenade wounds through a crate of the game", fThing);
    if (fRec) Fail("F: %d grenade wounds of a player whose place he last sent was inside a crate", fRec);
    printf("%s: walls3 (M, R, F)\n", gFails ? "FAIL" : "PASS");
    return gFails ? 1 : 0;
}
