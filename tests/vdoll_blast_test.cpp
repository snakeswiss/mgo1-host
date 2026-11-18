// (v150-host beta 22, blast) A stun grenade's flash and a smoke grenade's cloud on a virtual doll's eyes, by the game's own
// numbers (MGO1Loader/vdoll_blast.h, docs/VIRTUAL_DOLL.md 31). Until beta 21 a doll saw and shot through every cloud and was
// never whitened by a flash.
//   R. the rules alone (no map): the white's strength by distance (1 within 10 m, none beyond 30 m, the game's ramp with its
//      floor of 0.25 between), its frames (1440 facing within 45 degrees, 120 otherwise, half the strength hidden), the
//      frames nothing is seen through it (over 40 of the fade's 128), a cloud's line test (its radius as drawn: spread over
//      its first second, thinner in its last two; a shape seen within 4 m);
//   then a doll on a map's collision (City Under Siege by default), an enemy 15 m before it on open ground, the host's clouds
//   and stun blasts standing in for Host::smokes and Host::stunBlasts:
//   1. nothing: it sees him and hits him (the control);
//   2. a cloud on their line (4.5 m, two seconds old, out for the whole part): never seen, not one hit;
//   3. the same cloud 7 m aside of the line: seen and hit;
//   4. a cloud round both of them, he 3.5 m from it: seen (a shape so close);
//   5. in a fight with him, a stun blast 3 m before it: the game's long white (its camera faces the blast, in its view or in
//      the third person: 24 s, nothing seen 16.5 s), not one hit of its meanwhile, hits after; 3 m behind it the short white
//      (2 s, nothing seen 1.4 s); 35 m away none: hits all along;
//   7. its own grenade at two enemies 12 m away: each wound comes with the thrower's reaction record {09 doll, 8, angle} (a
//      blast's fall, as a player's game sends it); nearer, farther or aside till its grenade wounds one (10 places at most);
//   8. its grenade among five enemies: wounded within the game's 3.5 m of the blast, none beyond;
//   9. its own stun grenade (SNEAKING's Snake) at an enemy 6, 10, 15 and 21.5 m away: turned away from it until it goes off,
//      the short white at most (MGO1_VDOLL_TURN_AWAY=0: facing it, 4 to 10 s of nothing seen, measured and printed);
//   6. MGO1_VDOLL_BLASTS=0: beta 21's back (part 2 sees and hits him through the cloud, part 5 sees him at once, its grenade
//      sends no reaction).
// VDOLL_BLAST_TEST_BETA21=1: the whole test with the switch at 0 from the start (beta 21's behaviour): parts 2, 5, 7, 8 and 9
// FAIL. Built on beta 21's own body (b16 46942df: no vdoll_blast.h, no Host::smokes) R, 2, 5, 7, 8 and 9 FAIL (and 6's reach:
// that body logs no blast point).
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_blast_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o /tmp/vdoll_blast_test
//   /tmp/vdoll_blast_test <map.geom>
// (the map's collision file comes from the owner's disc: python3 -I tools/stage_dat.py extract <STAGE_N.DAT> n003a <dir>;
// it is never put in the repository)
#include "vdoll_body.h"
#ifdef VDOLL_HOST_BLASTS
#include "vdoll_blast.h"
#endif
#include "doll_geom.h"
#include "doll_nav.h"
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
static int gSees = 0, gFlashLines = 0;
static long long gFrame = 0, gFirstSee = -1;
static float gBlastAt[3]; static int gBlasts = 0;
static float gBlindS = 0.f;                            // (the last flash line's seconds of nothing seen)
// (v150-host beta 22, blast; review22a B2) its own stun grenade: the host's copy's blast where its log says it went off (the
// host's NewStunBlast for a doll's throw), and its flash lines (white, nothing seen)
static struct { bool on = false; uint32_t id = 0; float at[3] = {0, 0, 0}; long long from = 0, until = 0; } gOwnStun;
static int gOwnStuns = 0, gThrows = 0, gDollFlashes = 0;
static float gDollWhiteS = 0.f, gDollBlindS = 0.f, gDollFlashM = 0.f;
static int gRuleNow = 1;
static long long gFrameNow() { return gFrame; }       // (its grenade's last blast, from its log: MGO1_VDOLL_DEBUG)
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, ": sees player 3")) { ++gSees; if (gFirstSee < 0) gFirstSee = gFrame; }
    if (strstr(b, "stun grenade's flash")) { ++gFlashLines; const char* n = strstr(b, "nothing seen "); if (n) gBlindS = atof(n + 13); }
    if (const char* g = strstr(b, ": its grenade goes off at ")) { if (sscanf(g + 26, "%f %f %f", &gBlastAt[0], &gBlastAt[1], &gBlastAt[2]) == 3) ++gBlasts; }
    if (const char* g = strstr(b, ": its stun grenade goes off at ")) {
        if (sscanf(g + 31, "%f %f %f", &gOwnStun.at[0], &gOwnStun.at[1], &gOwnStun.at[2]) == 3) {
            ++gOwnStuns; gOwnStun.on = true; gOwnStun.id = 0x5E1F0000u + (uint32_t)gOwnStuns; gOwnStun.from = gFrame; gOwnStun.until = gFrame + 40;
        }
    }
    if (strstr(b, ": throws a grenade toward ")) ++gThrows;
    if (const char* f = strstr(b, "vdoll 1: a stun grenade's flash ")) {
        ++gDollFlashes; gDollFlashM = (float)atof(f + 32);
        const char* w = strstr(f, ": white "); const char* n = strstr(f, "nothing seen ");
        gDollWhiteS = w ? (float)atof(w + 8) : 0.f; gDollBlindS = n ? (float)atof(n + 13) : 0.f;
    }
    if (getenv("VDOLL_TEST_LOG") || strstr(b, "mesh") || strstr(b, "flash") || strstr(b, "smoke")) printf("   [%lld] %s\n", gFrame, b);
}
static bool MapNow(std::string* page, int* extra) { *page = "n003a"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return gRuleNow; }
struct Sent { int slot, kind; std::vector<uint8_t> rec; long long at = 0; };
static long long gFrameNow();
static std::vector<Sent> gSent;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gSent.push_back({slot, kind, rec, gFrameNow()}); }

// the host's clouds and stun blasts now
struct TCloud { float c[3]; float r; int left; };           // (the game's cloud: its middle, radius, time left)
static std::vector<TCloud> gCl;
static struct { bool on = false; uint32_t id = 0; float at[3] = {0, 0, 0}; long long from = 0, until = 0; } gBlast;
static int Smokes(float c[][3], float r[], int left[], int max) {
    int n = 0;
    for (const TCloud& k : gCl) { if (n >= max) break; memcpy(c[n], k.c, sizeof k.c); r[n] = k.r; left[n] = k.left; ++n; }
    return n;
}
static int StunBlasts(uint32_t id[], float at[][3], int max) {
    int n = 0;
    if (gBlast.on && n < max && gFrame >= gBlast.from && gFrame < gBlast.until) { id[n] = gBlast.id; memcpy(at[n], gBlast.at, sizeof gBlast.at); ++n; }
    if (gOwnStun.on && n < max && gFrame >= gOwnStun.from && gFrame < gOwnStun.until) { id[n] = gOwnStun.id; memcpy(at[n], gOwnStun.at, sizeof gOwnStun.at); ++n; }
    return n;
}

static bool Read(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) gGeom.insert(gGeom.end(), b, b + n);
    fclose(f);
    return !gGeom.empty();
}

// a part: the doll at `start` facing the enemy for `frames` (kept there); the enemy out of sight until `enemyFrom` frames in;
// its sights of him (the first one's frame from the part's start), its hits
// (v150-host beta 22, blast) kept at the height its body gives it at its new life (its copy's floor + 800, a few cm under the
// mesh's point): beta 22's first test put it back at the mesh's height from its second frame, so a grenade it solved on its
// first frame flew from 46 mm higher at the release (n003a: past a roof's edge, 9 m down, no wound: parts 6 and 7 failed)
struct Result { int sees = 0, hits = 0; long long first = -1; };
static Result Part(vdoll::body::Self& s, float* pos, const float* start, uint16_t* facing, uint16_t face0, int frames, int enemyFrom = 0) {
    float at[3] = {start[0], start[1], start[2]};
    memcpy(pos, at, sizeof at);
    *facing = face0;
    gSent.clear();
    gSees = 0; gFirstSee = -1;
    s.life = gFrame;
    Result r;
    const long long from = gFrame;
    for (long long end = gFrame + frames; gFrame < end; gFrame += 2) {
        gHave[2] = gFrame - from >= enemyFrom;
        memcpy(pos, at, sizeof at);
        memcpy(gPlayers[1].pos, pos, 3 * sizeof(float));
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
        if (gFrame == from) at[1] = pos[1];              // (its new life's height, from its first frame on)
    }
    for (const Sent& m : gSent) if (m.slot == 2 && !m.rec.empty() && m.kind == 0 && m.rec[0] == 4) ++r.hits;
    r.sees = gSees;
    r.first = gFirstSee >= 0 ? gFirstSee - from : -1;
    gHave[2] = false;
    vdoll::body::Down(0);
    for (int k = 0; k < 120; k += 2) { vdoll::body::Frame(gFrame); gFrame += 2; }   // (out of sight a while: the next part's fight is a new one)
    return r;
}

static int HitsBetween(long long a, long long b) { int n = 0; for (const Sent& m : gSent) if (m.slot == 2 && m.kind == 0 && !m.rec.empty() && m.rec[0] == 4 && m.at >= a && m.at <= b) ++n; return n; }
static int gFails = 0;
static void Check(bool ok, const char* what) { if (!ok) { printf("FAIL: %s\n", what); ++gFails; } }

int main(int argc, char** argv) {
    // ---- R. the rules ----
#ifdef VDOLL_HOST_BLASTS
    {
        using namespace vblast;
        Check(FlashStrength(5000.f) == 1.f && FlashStrength(9999.f) == 1.f, "R: full strength within 10 m");
        Check(std::fabs(FlashStrength(12000.f) - 0.25f) < 1e-4f && std::fabs(FlashStrength(20000.f) - 0.5f) < 1e-4f && std::fabs(FlashStrength(29000.f) - 0.95f) < 1e-4f,
              "R: the game's ramp from 10 to 30 m, its floor 0.25");
        Check(FlashStrength(30001.f) == 0.f, "R: none beyond 30 m");
        const float e[3] = {0, 1500, 0}, ahead[3] = {0, 0, 1}, side[3] = {1, 0, 0};
        const float p5[3] = {0, 1500, 5000}, p20[3] = {0, 1500, 20000}, pAngle[3] = {4000, 1500, 3000};   // (the last 53 degrees off)
        Check(FlashFrames(e, ahead, p5, false) == 1440, "R: facing it 5 m away, 1440 frames (24 s)");
        Check(FlashFrames(e, side, p5, false) == 120, "R: turned 90 degrees, 120 frames (2 s)");
        Check(FlashFrames(e, ahead, pAngle, false) == 120, "R: 53 degrees off, 120 frames");
        Check(FlashFrames(e, ahead, p5, true) == 60, "R: hidden, half the strength and never the long white");
        Check(FlashFrames(e, ahead, p20, false) == 720, "R: facing it 20 m away, half (the game's ramp)");
        const float none[3] = {0, 0, 0};
        Check(FlashFrames(e, none, p5, false) == 120, "R: no view (the third person), the short white");
        Check(BlindFrames(120) == 82 && BlindFrames(1440) == 990 && BlindFrames(0) == 0, "R: nothing seen while the white is over 40 of 128");
        Cloud k = {{0, 1500, 7500}, 4500.f, 3250 - 600};   // (two seconds old)
        const float a[3] = {0, 1500, 0}, b[3] = {0, 1500, 15000};
        Check(SmokeHides(a, b, &k, 1), "R: a cloud on the line hides him");
        Cloud aside = k; aside.c[0] = 7000.f; aside.c[2] = 7500.f;
        Check(!SmokeHides(a, b, &aside, 1), "R: a cloud 7 m aside does not");
        const float near[3] = {0, 1500, 3500};
        Cloud round = {{0, 1500, 1750}, 4500.f, 3000};
        Check(!SmokeHides(a, near, &round, 1), "R: a shape seen within 4 m");
        Cloud fresh = k; fresh.left = 3250; fresh.c[0] = 2000.f;    // (just out: 0.3 of its radius, 1.35 m)
        Check(!SmokeHides(a, b, &fresh, 1), "R: a cloud just out is small (spread over its first second)");
        fresh.left = 3250 - 300;                                     // (a second on: its whole radius)
        Check(SmokeHides(a, b, &fresh, 1), "R: a second on, it hides 2 m off its middle");
        Cloud old = k; old.left = 300; old.c[0] = 3000.f;           // (its last second: half its radius)
        Check(!SmokeHides(a, b, &old, 1), "R: thinner in its last two seconds");
        Cloud gone = k; gone.left = 0;
        Check(!SmokeHides(a, b, &gone, 1), "R: a cloud gone hides nothing");
        printf("R. the rules: %s\n", gFails ? "FAILED" : "pass");
    }
#else
    Check(false, "R: this body has no vdoll_blast.h (beta 21): no flash, no cloud");
#endif

    if (argc < 2 || !Read(argv[1])) { fprintf(stderr, "usage: vdoll_blast_test <map.geom>\n"); return gFails ? 1 : 2; }
    const bool beta21 = getenv("VDOLL_BLAST_TEST_BETA21") != nullptr;
    setenv("MGO1_VDOLL_LEVEL", "10", 1);
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
    setenv("MGO1_VDOLL_DEBUG", "1", 1);                       // (its grenade's blast point in its log: part 8)
    unsetenv("MGO1_VDOLL_BLASTS"); unsetenv("MGO1_VDOLL_FLASH"); unsetenv("MGO1_VDOLL_SMOKE");
    if (beta21) { setenv("MGO1_VDOLL_BLASTS", "0", 1); printf("(MGO1_VDOLL_BLASTS=0: beta 21's behaviour)\n"); }
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
#ifdef VDOLL_HOST_BLASTS
    h.smokes = Smokes; h.stunBlasts = StunBlasts;
#else
    (void)Smokes; (void)StunBlasts;
    printf("(this body has no Host::smokes nor Host::stunBlasts: beta 21's)\n");
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    gHave[1] = true; gPlayers[1].team = 0; gPlayers[1].object = true; gPlayers[1].alive = true; gPlayers[1].life = 200;
    gPlayers[2].team = 1; gPlayers[2].object = true; gPlayers[2].alive = true; gPlayers[2].life = 200;
    gPlayers[1].state = 3; gPlayers[2].state = 3;
    const float kHips = 800.f;
    float start[3], far[3];
    doll::Geom g; doll::NavMesh nav;                          // (kept: the grenade parts' places, below)
    {
        std::string err, log;
        doll::NavConfig cfg;
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
            // (the stun blasts' places 3 m before and behind the doll, on its line: their lines to the doll's eye clear too)
            const float ux = (q[0] - p[0]) / d, uz = (q[2] - p[2]) / d;
            const float bl[3] = {p[0] + ux * 3000.f, p[1] + 150.f, p[2] + uz * 3000.f}, e[3] = {p[0], p[1] + 1550.f, p[2]};
            const float bb[3] = {p[0] - ux * 3000.f, p[1] + 150.f, p[2] - uz * 3000.f};   // (and the one 3 m behind it)
            ok = ok && nav.Clear(bl, e) && nav.Clear(bb, e);
            if (!ok) continue;
            memcpy(start, p, sizeof start); memcpy(far, q, sizeof far);
            found = true;
        }
        if (!found) { printf("FAIL: no open ground found\n"); return 1; }
    }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(gFrame); gFrame += 2; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("FAIL: the bodies' mesh was not built\n"); return 1; }
    // (v150-host beta 22, blast) the parts from one frame whatever the mesh's build took (its thread: 300 to 1200 frames of
    // this wait, the parts' frames and the doll's 6-frame ticks with them)
    gFrame = 12000;
    float pos[3];
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send;
    float hips[3] = {start[0], start[1] + kHips, start[2]};
    const float len = std::hypot(far[0] - start[0], far[2] - start[2]);
    const float ux = (far[0] - start[0]) / len, uz = (far[2] - start[2]) / len;
    const uint16_t face0 = (uint16_t)lrintf(std::atan2(ux, uz) / 6.2831853f * 65536.f);
    gPlayers[2].pos[0] = far[0]; gPlayers[2].pos[1] = far[1] + kHips; gPlayers[2].pos[2] = far[2];
    printf("the doll at %.0f %.0f %.0f, the enemy at %.0f %.0f %.0f (%.1f m)\n", hips[0], hips[1], hips[2], gPlayers[2].pos[0], gPlayers[2].pos[1], gPlayers[2].pos[2], len / 1000.f);
    const float mid[3] = {start[0] + ux * len * 0.5f, start[1] + 1500.f, start[2] + uz * len * 0.5f};
    auto cloudAt = [&](const float* c) { TCloud k; memcpy(k.c, c, sizeof k.c); k.r = 4500.f; k.left = 2650; return k; };   // (2 s old, 8.8 s left)

    gCl.clear(); gBlast.on = false;
    Result r = Part(s, pos, hips, &facing, face0, 480);
    printf("1. nothing: seen %d time(s), first after %lld frames, %d hit(s)\n", r.sees, r.first, r.hits);
    Check(r.sees && r.hits, "1: the doll does not fight an enemy in the open");
    (void)r.first;

    gCl = {cloudAt(mid)};
    r = Part(s, pos, hips, &facing, face0, 480);
    printf("2. a cloud on their line: seen %d time(s), %d hit(s)\n", r.sees, r.hits);
    Check(!r.sees && !r.hits, "2: the doll saw or hit him through a smoke's cloud");

    {
        const float aside[3] = {mid[0] + uz * 7000.f, mid[1], mid[2] - ux * 7000.f};
        gCl = {cloudAt(aside)};
        r = Part(s, pos, hips, &facing, face0, 480);
        printf("3. the cloud 7 m aside: seen %d time(s), %d hit(s)\n", r.sees, r.hits);
        Check(r.sees && r.hits, "3: a cloud off the line hid him");
    }
    {
        float nearHips[3] = {start[0] + ux * 3500.f, start[1] + kHips, start[2] + uz * 3500.f};
        const float c[3] = {start[0] + ux * 1750.f, start[1] + 1500.f, start[2] + uz * 1750.f};
        gCl = {cloudAt(c)};
        const float keep[3] = {gPlayers[2].pos[0], gPlayers[2].pos[1], gPlayers[2].pos[2]};
        memcpy(gPlayers[2].pos, nearHips, sizeof nearHips);
        r = Part(s, pos, hips, &facing, face0, 240);
        printf("4. both in a cloud, he 3.5 m from it: seen %d time(s), %d hit(s)\n", r.sees, r.hits);
        Check(r.sees > 0, "4: a shape 3.5 m away not seen in a cloud");
        memcpy(gPlayers[2].pos, keep, sizeof keep);
    }

    // 5. in a fight with him (it sees him from the start), a stun blast 60 frames in, by the game's own white: 3 m before it
    // the long white (its camera faces it, in its view or in the third person: 1440 frames, nothing seen for 990), 3 m behind
    // it the short one (120: 82), 35 m away none. Not one hit of its from 40 frames after the blast to the end of its white
    // (its fight goes on 20 frames on what it saw last, its hits' records a few frames later), hits again after it
    auto flashPart = [&](uint32_t id, float along, int* during, int* after, float* blindS) {
        gCl.clear(); gBlindS = 0.f;
        gBlast.on = true; gBlast.id = id; gBlast.from = gFrame + 60; gBlast.until = gFrame + 100;
        gBlast.at[0] = start[0] + ux * along; gBlast.at[1] = start[1]; gBlast.at[2] = start[2] + uz * along;
        const long long b0 = gBlast.from;
        const int lines = gFlashLines;
        Part(s, pos, hips, &facing, face0, 1500);
        *blindS = gFlashLines > lines ? gBlindS : 0.f;
        const long long blind = (long long)(*blindS * 59.94f);
        *during = HitsBetween(b0 + 40, b0 + std::max(80LL, blind - 10)); *after = HitsBetween(b0 + std::max(82LL, blind) + 10, b0 + 1400);
        gBlast.on = false;
    };
    int during = 0, after = 0; float blindS = 0.f;
    flashPart(0x01234560u, 3000.f, &during, &after, &blindS);
    printf("5. in a fight, a stun blast 3 m before it: nothing seen %.1f s, %d hit(s) meanwhile, %d after\n", blindS, during, after);
    Check(blindS > 16.f && blindS < 17.f, "5: not the game's long white (24 s: nothing seen 16.5 s) facing a blast 3 m before it");
    Check(during == 0, "5: the doll hit him through a stun grenade's white");
    Check(after > 0, "5: the doll never fought him again after its white");
    flashPart(0x01234565u, -3000.f, &during, &after, &blindS);
    printf("5. a stun blast 3 m behind it: nothing seen %.1f s, %d hit(s) meanwhile, %d after\n", blindS, during, after);
    Check(blindS > 1.2f && blindS < 1.5f, "5: not the game's short white (2 s: nothing seen 1.4 s) for a blast behind it");
    Check(during == 0 && after > 0, "5: its fire through or after the short white");
    flashPart(0x01234570u, -35000.f, &during, &after, &blindS);
    printf("5. a stun blast 35 m behind it: nothing seen %.1f s, %d hit(s) from 40 frames after it\n", blindS, during);
    Check(blindS == 0.f && during > 0, "5: a blast out of the white's reach held its fire");

    // 7. its own grenade: two enemies 1 m apart 12 m before it (it throws a grenade at such a pair, as before); its blast's
    // wound on each and the thrower's reaction record with it, as a player's game sends them ({09 doll, 8, angle}: a blast's
    // fall); beta 21 sent no reaction record (the player hit by its grenade stood)
    // (v150-host beta 22, blast) the pair's places: 12 m before it on its line first, then nearer, farther and aside, each on
    // a floor within 0.5 m of the doll's (both of the pair), the first in its eye's sight; a part takes the next place until its
    // grenade wounds one of them (10 places at most), so that it tests what it should on every map
    std::vector<std::vector<float>> spots;                     // (the first of the pair's hips)
    {
        const float eye[3] = {hips[0], hips[1] + 750.f, hips[2]};
        for (float along : {12000.f, 10000.f, 14000.f, 11000.f, 13000.f, 9000.f, 15000.f, 16000.f})
            for (float side : {0.f, 2000.f, -2000.f, 4000.f, -4000.f}) {
                float y[2];
                bool ok = true;
                for (int m = 0; m < 2 && ok; ++m) {                // (the pair 1 m apart: the second aside of the line)
                    const float sd = side + (m ? 1000.f : 0.f);
                    const float p[3] = {start[0] + ux * along + uz * sd, start[1] + 500.f, start[2] + uz * along - ux * sd};
                    y[m] = nav.GroundBelow(p, 1500.f);
                    ok = y[m] > -1e29f && std::fabs(y[m] - start[1]) <= 500.f;
                    const float chest[3] = {p[0], y[m] + kHips + 400.f, p[2]};
                    ok = ok && (m == 1 || nav.Clear(eye, chest));   // (the first in its sight: its pair rule, StartThrow)
                }
                if (ok) spots.push_back({start[0] + ux * along + uz * side, y[0] + kHips, start[2] + uz * along - ux * side, along, side});
            }
    }
    auto spotSay = [&](size_t i) { static char t[96]; snprintf(t, sizeof t, "%.0f m before it, %.0f m aside, place %d", spots[i][3] / 1000.f, spots[i][4] / 1000.f, (int)i + 1); return t; };
    auto nadePart = [&](int* wounds, int* reacts, int* kind8, std::string* where) {
        const float keep[3] = {gPlayers[2].pos[0], gPlayers[2].pos[1], gPlayers[2].pos[2]};
        *wounds = *reacts = *kind8 = 0;
        *where = "no place";
        for (size_t i = 0; i < spots.size() && i < 10 && *wounds == 0; ++i) {
            const float e2[3] = {spots[i][0], spots[i][1], spots[i][2]};
            const float e3[3] = {e2[0] + uz * 1000.f, e2[1], e2[2] - ux * 1000.f};
            memcpy(gPlayers[2].pos, e2, sizeof e2);
            gPlayers[3] = gPlayers[2]; memcpy(gPlayers[3].pos, e3, sizeof e3); gHave[3] = true;
            Part(s, pos, hips, &facing, face0, 600);
            for (const Sent& m : gSent) {
                if (m.slot != 2 && m.slot != 3) continue;
                if (m.kind == 0 && m.rec.size() >= 9 && m.rec[0] == 4 && m.rec[6] == 0x13) ++*wounds;
                if (m.kind == 1 && m.rec.size() >= 5 && m.rec[0] == 9) { ++*reacts; if (m.rec[2] == 8) ++*kind8; }
            }
            *where = spotSay(i);
        }
        gHave[3] = false;
        memcpy(gPlayers[2].pos, keep, sizeof keep);
    };
    {
        int w = 0, rc = 0, k8 = 0;
        std::string where;
        nadePart(&w, &rc, &k8, &where);
        printf("7. its grenade at two enemies (%s): %d wound(s), %d reaction record(s), %d of them a blast's fall\n", where.c_str(), w, rc, k8);
        Check(w > 0, "7: no grenade wound (the part did not test what it should)");
        Check(w == 0 || (rc == w && k8 == w), "7: its grenade's wound without the reaction a thrower's game sends (a blast's fall)");
    }
    // 8. its grenade's reach: the pair (slots 2 and 3, 1 m apart) and three more enemies 3.15, 3.3 and 3.45 m from the first
    // in other directions; once its grenade has wounded one of them (the pair's places as part 7's), each enemy whose chest is
    // within the game's 3500 mm of the blast (its point 300 mm up, a clear line) is wounded and none beyond (0x4F0448: 3500
    // for the thrower's own grenade; beta 21's 3000 leaves the ring from 3 to 3.5 m untouched); the next place while none of
    // them stood 3 to 3.5 m from the blast (the wrong ones of every place counted)
    auto ringPart = [&](float reach, int* inRing, int* wrong) {
        *inRing = 0; *wrong = -1;
        const float keep[3] = {gPlayers[2].pos[0], gPlayers[2].pos[1], gPlayers[2].pos[2]};
        const float dirs[4][3] = {{uz, 0, -ux}, {-uz, 0, ux}, {ux, 0, uz}, {0.7071f * (uz + ux), 0, 0.7071f * (uz - ux)}};
        const float dist[4] = {1000.f, 3150.f, 3300.f, 3450.f};
        for (size_t i = 0; i < spots.size() && i < 10 && *inRing == 0; ++i) {
            const float c2[3] = {spots[i][0], spots[i][1], spots[i][2]};
            memcpy(gPlayers[2].pos, c2, sizeof c2);
            for (int k = 0; k < 4; ++k) { gPlayers[3 + k] = gPlayers[2]; for (int a = 0; a < 3; ++a) gPlayers[3 + k].pos[a] = c2[a] + dirs[k][a] * dist[k]; gHave[3 + k] = true; }
            const int before = gBlasts;
            Part(s, pos, hips, &facing, face0, 600);
            bool any = false;
            for (const Sent& m : gSent) if (m.slot >= 2 && m.slot < 7 && m.kind == 0 && m.rec.size() >= 9 && m.rec[0] == 4 && m.rec[6] == 0x13) any = true;
            if (gBlasts == before || !any) continue;
            if (*wrong < 0) *wrong = 0;
            for (int id = 2; id < 7; ++id) {
                float ch[3], hd[3]; vdoll::body::BodyPoints(gPlayers[id], ch, hd);
                const float up[3] = {gBlastAt[0], gBlastAt[1] + 300.f, gBlastAt[2]};
                const float d = std::sqrt((ch[0] - up[0]) * (ch[0] - up[0]) + (ch[1] - up[1]) * (ch[1] - up[1]) + (ch[2] - up[2]) * (ch[2] - up[2]));
                bool hit = false;
                for (const Sent& m : gSent) if (m.slot == id && m.kind == 0 && m.rec.size() >= 9 && m.rec[0] == 4 && m.rec[6] == 0x13) hit = true;
                if (vdoll::body::SightStopped(up, ch)) continue;    // (the blast's line to him blocked: no wound, the game's filter too)
                if (d > 3000.f && d <= 3500.f) ++*inRing;
                if (hit != (d <= reach) && std::fabs(d - reach) > 30.f) { ++*wrong; printf("   player %d at %.2f m from the blast: %s\n", id + 1, d / 1000.f, hit ? "wounded" : "not wounded"); }
            }
            printf("   (the five round %s: %d of them 3 to 3.5 m from the blast)\n", spotSay(i), *inRing);
        }
        for (int k = 3; k < 7; ++k) gHave[k] = false;
        memcpy(gPlayers[2].pos, keep, sizeof keep);
    };
    {
        int ring = 0, wrong = 0;
        ringPart(3500.f, &ring, &wrong);
        printf("8. its grenade among five enemies: %s, %d of them 3 to 3.5 m from the blast, %d wounded or spared against the game's 3.5 m\n",
               wrong < 0 ? "no wound at any place" : "thrown", ring, wrong);
        Check(wrong == 0, "8: its grenade's wound not the game's 3.5 m");
        if (ring == 0 && wrong == 0) printf("   (none in the ring this time: the reach beyond 3 m not shown)\n");
    }

    // 9. (v150-host beta 22, blast; review22a B2) its own stun grenade (SNEAKING's Snake, MGO1_VDOLL_NADE_TEST: at an enemy it
    // sees 4 to 22 m away) at 6, 10, 15 and 21.5 m: the white the host's copy of its grenade gives it, with
    // MGO1_VDOLL_TURN_AWAY=0 (beta 22's first version: it kept facing its throw) and on (it turns away until it goes off, as a
    // player does): on, the short white at most (nothing seen 1.4 s); off, the game's white facing it (4 s or more). Places
    // on open ground at that distance, the next one while it threw nothing there (8 at most)
    {
        const float dists[4] = {6000.f, 10000.f, 15000.f, 21500.f};
        const float keep2[3] = {gPlayers[2].pos[0], gPlayers[2].pos[1], gPlayers[2].pos[2]};
        gRuleNow = 4; s.team = 1; gPlayers[1].team = 1; gPlayers[2].team = 0;   // (the blue side: Snake)
        setenv("MGO1_VDOLL_NADE_TEST", "1", 1);
        static uint32_t seed9 = 0x2468ACEu;
        unsigned (*rnd9)() = []() -> unsigned { seed9 ^= seed9 << 13; seed9 ^= seed9 >> 17; seed9 ^= seed9 << 5; return seed9; };
        for (float dd : dists) {
            float res[2][3] = {{0, 0, 0}, {0, 0, 0}};      // (white s, nothing seen s, the blast's distance m: off, on)
            bool done = false;
            int places = 0;
            for (int k = 0; k < 40000 && !done && places < 8; ++k) {
                float p[3], q[3];
                if (!nav.RandomPoint(nullptr, 0.f, p, rnd9) || !nav.RandomPoint(p, dd + 1000.f, q, rnd9)) continue;
                const float d = std::hypot(p[0] - q[0], p[2] - q[2]);
                if (std::fabs(d - dd) > 300.f || std::fabs(p[1] - q[1]) > 300.f) continue;
                bool ok = true;
                for (float h1 : {1550.f, 1100.f}) for (float h2 : {1150.f, 1650.f, 150.f}) {
                    const float a[3] = {p[0], p[1] + h1, p[2]}, b[3] = {q[0], q[1] + h2, q[2]};
                    ok = ok && nav.Clear(a, b);
                }
                if (!ok) continue;
                ++places;
                const float hp[3] = {p[0], p[1] + kHips, p[2]};
                const uint16_t f9 = (uint16_t)lrintf(std::atan2((q[0] - p[0]) / d, (q[2] - p[2]) / d) / 6.2831853f * 65536.f);
                gPlayers[2].pos[0] = q[0]; gPlayers[2].pos[1] = q[1] + kHips; gPlayers[2].pos[2] = q[2];
                bool both = true;
                for (int on = 0; on < 2 && both; ++on) {
                    setenv("MGO1_VDOLL_TURN_AWAY", on ? "1" : "0", 1);
                    vdoll::body::Init(h);
                    const int t0 = gThrows, s0 = gOwnStuns, f0 = gDollFlashes;
                    Part(s, pos, hp, &facing, f9, 600);
                    gOwnStun.on = false;
                    if (gThrows == t0 || gOwnStuns == s0 || gDollFlashes == f0) both = false;
                    else { res[on][0] = gDollWhiteS; res[on][1] = gDollBlindS; res[on][2] = gDollFlashM; }
                }
                done = both;
            }
            if (!done) printf("9. its own stun grenade at an enemy %.1f m away: no throw at %d places\n", dd / 1000.f, places);
            else printf("9. its own stun grenade at an enemy %.1f m away (place %d; its blast %.1f m from its eye): facing it (MGO1_VDOLL_TURN_AWAY=0) white %.1f s, nothing seen %.1f s; turned away white %.1f s, nothing seen %.1f s\n",
                        dd / 1000.f, places, res[1][2], res[0][0], res[0][1], res[1][0], res[1][1]);
            Check(done, "9: no stun grenade of its own thrown and gone off (the part did not test what it should)");
            if (done) {
                Check(res[0][1] >= 3.f, "9: facing its own stun grenade (MGO1_VDOLL_TURN_AWAY=0) not the game's white (the part did not test what it should)");
                Check(res[1][1] <= 1.45f, "9: turned away from its own stun grenade, more than the short white (1.4 s nothing seen)");
            }
        }
        unsetenv("MGO1_VDOLL_NADE_TEST"); unsetenv("MGO1_VDOLL_TURN_AWAY");
        gRuleNow = 1; s.team = 0; gPlayers[1].team = 0; gPlayers[2].team = 1;
        memcpy(gPlayers[2].pos, keep2, sizeof keep2);
        vdoll::body::Init(h);
    }

    if (!beta21) {
        setenv("MGO1_VDOLL_BLASTS", "0", 1);
        vdoll::body::Init(h);
        gCl = {cloudAt(mid)};
        r = Part(s, pos, hips, &facing, face0, 480);
        printf("6. MGO1_VDOLL_BLASTS=0, the cloud on their line: seen %d time(s), %d hit(s) (beta 21: through it)\n", r.sees, r.hits);
        Check(r.sees && r.hits, "6: the switch at 0 does not give beta 21's eyes back (the cloud)");
        flashPart(0x01234580u, 3000.f, &during, &after, &blindS);
        printf("6. MGO1_VDOLL_BLASTS=0, in a fight, the stun blast 3 m before it: nothing seen %.1f s, %d hit(s) from 40 frames after it (beta 21: hits on)\n", blindS, during);
        Check(blindS == 0.f && during > 0, "6: the switch at 0 does not give beta 21's eyes back (the flash)");
        int w = 0, rc = 0, k8 = 0;
        std::string where;
        nadePart(&w, &rc, &k8, &where);
        printf("6. MGO1_VDOLL_BLASTS=0, its grenade (%s): %d wound(s), %d reaction record(s) (beta 21: none)\n", where.c_str(), w, rc);
        Check(w > 0 && rc == 0, "6: the switch at 0 does not give beta 21's grenade back");
        int ring = 0, wrong = 0;
        ringPart(3000.f, &ring, &wrong);
        printf("6. MGO1_VDOLL_BLASTS=0, its grenade among five enemies: %d of them 3 to 3.5 m from the blast, %d against beta 21's 3 m\n", ring, wrong);
        Check(wrong == 0, "6: the switch at 0 does not give beta 21's grenade reach back");
        unsetenv("MGO1_VDOLL_BLASTS");
    }
    printf("%s (%d failed)\n", gFails ? "FAILED" : "ALL PASSED", gFails);
    return gFails ? 1 : 0;
}
