// (v150-host beta 21) A virtual doll aiming at an enemy it sees and never firing (MGO1Loader/vdoll_body.cpp Fight and
// FightPlayer, docs/VIRTUAL_DOLL.md 25): beta 20's regression runs/V20_cap9, capture on map 9 (n024a), three virtual dolls
// alone, stood "aiming" 446 s with 0 shots. Off line, on a map's .geom, each part in a process of its own (a body's state
// starts afresh), run a frame at a time, the dolls' copies on the host following them; a doll "stalls" while it aims at a
// target and neither fires nor moves; every part fails when a doll stalls more than 5 s (300 frames):
//   cap9   (n024a only) the three dolls of V20_cap9 where they stood, their weapons as their records named them (the red
//          AK-47 and M37 92 mm apart, the blue M37 26 m away and 3 m lower)
//   pair   two allies side by side (92 and 400 mm apart, AK-47s), an enemy standing 12 to 22 m ahead in sight: each fires
//   ahead  an ally standing still 4 m ahead on its line of fire, the enemy 12 to 22 m away: it steps aside and fires
//   m37    the M37 (16 m reach), an enemy standing 20 to 28 m away in sight, a way to him: it closes in and fires
//   sweep  pairs of dolls (weapons by chance) and an enemy each 3 to 40 m away in sight: none stalls
//   at     (only when named) VDOLL_TEST_AT="x,y,z ex,ey,ez": one doll (AK-47, or MGO1_VDOLL_WEAPONS) at a place (hips), an
//          enemy standing at another, 20 s: a place of a run replayed (runs/tsneW_wake5 on n003a: "-2788,4800,32414
//          -4423,4174,35297"; with MGO1_VDOLL_CQC_TEST=1, that bench's switch, no shot is what the switch says: not judged)
// Every map given, every part (or the parts named after the maps). MGO1_VDOLL_CONTROLS and MGO1_VDOLL_LEVEL as on the host
// (10 when unset); MGO1_VDOLL_STANDOFF=0 gives beta 20's rules back (the "before": cap9, pair, ahead and m37 fail).
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_standoff_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_standoff_test
//   ./vdoll_standoff_test <map.geom> [<map.geom> ...] [cap9|pair|ahead|m37|sweep|at ...]
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
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (getenv("VDOLL_TEST_VERBOSE") || strstr(b, "with no shot")) printf("    %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }                          // (team deathmatch)
// (VDOLL_TEST_DUMP=<file>: every record sent, a line each (frame slot channel hex), appended: two builds compared with the same
// seed, tests/vdoll_standoff_same.sh)
static FILE* gDump = nullptr;
static long long gFrame = 600;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) {
    if (!gDump) return;
    fprintf(gDump, "%lld %d %d ", gFrame, slot, kind);
    for (uint8_t b : rec) fprintf(gDump, "%02x", b);
    fputc('\n', gDump);
}
static unsigned gRnd = 0x2545F491u;
static unsigned Rnd() { gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5; return gRnd; }
static float Rnd01() { return (Rnd() & 0xFFFFFF) / 16777216.f; }

// a doll of a part: its place (hips), facing, team, its slot (player id)
struct Doll { float pos[3]; float off[3] = {0, 0, 0}; uint16_t facing; int team, slot; vdoll::body::Self s; long long stall = 0, maxStall = 0; int shots = 0; float ran = 0; };
struct Still { float pos[3]; int team, slot; };          // a player standing still (an enemy, an ally)

static const bool gTrace = getenv("VDOLL_TEST_TRACE") != nullptr;
static const int gTraceEvery = gTrace && atoi(getenv("VDOLL_TEST_TRACE")) > 1 ? atoi(getenv("VDOLL_TEST_TRACE")) : 30;   // (VDOLL_TEST_TRACE=<frames>: that often)
static bool StartBody() {
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return gMeshSaid;
}
// one fight: the dolls and the players standing still, `frames` frames; each doll's longest stall (frames)
static void Play(std::vector<Doll>& dolls, const std::vector<Still>& still, int frames) {
    for (int i = 0; i < 16; ++i) if (i) gHave[i] = false;
    for (const Still& p : still) {
        gHave[p.slot] = true; gPlayers[p.slot] = vdoll::PlayerNow(); gPlayers[p.slot].team = p.team; gPlayers[p.slot].object = gPlayers[p.slot].alive = true;
        gPlayers[p.slot].life = 200; gPlayers[p.slot].cls = 1; memcpy(gPlayers[p.slot].pos, p.pos, sizeof p.pos);
    }
    for (size_t k = 0; k < dolls.size(); ++k) {
        Doll& d = dolls[k];
        d.s = vdoll::body::Self();
        d.s.k = (int)k; d.s.slot = d.slot; d.s.team = d.team; d.s.life = gFrame; d.s.pos = d.pos; d.s.facing = &d.facing; d.s.send = Send;
        gHave[d.slot] = true; gPlayers[d.slot] = vdoll::PlayerNow(); gPlayers[d.slot].team = d.team; gPlayers[d.slot].object = gPlayers[d.slot].alive = true;
        gPlayers[d.slot].life = 200; gPlayers[d.slot].cls = 1;
        for (int i = 0; i < 3; ++i) gPlayers[d.slot].pos[i] = d.pos[i] + d.off[i];
        d.stall = d.maxStall = 0; d.shots = 0; d.ran = 0;
    }
    std::vector<vdoll::body::AimInfo> was(dolls.size());
    for (size_t k = 0; k < dolls.size(); ++k) vdoll::body::Aim((int)k, &was[k]);
    for (int t = 0; t < frames; ++t, ++gFrame) {
        if (gFrame % 6 == 0)                          // (the host's copies, read every 100 ms; off: where the host's game held them)
            for (Doll& d : dolls) for (int i = 0; i < 3; ++i) gPlayers[d.slot].pos[i] = d.pos[i] + d.off[i];
        vdoll::body::Frame(gFrame);
        for (size_t k = 0; k < dolls.size(); ++k) {
            Doll& d = dolls[k];
            const float p0[3] = {d.pos[0], d.pos[1], d.pos[2]};
            d.s.tick = gFrame % 6 == 0;
            vdoll::body::Alive(d.s, gFrame);
            vdoll::body::AimInfo now;
            vdoll::body::Aim((int)k, &now);
            const float moved = std::hypot(d.pos[0] - p0[0], d.pos[2] - p0[2]);
            d.ran += moved;
            const bool shot = now.shots > was[k].shots;
            d.shots += now.shots - was[k].shots;
            if (now.aiming && now.target >= 0 && !shot && moved < 0.5f) { if (++d.stall > d.maxStall) d.maxStall = d.stall; }
            else d.stall = 0;
            was[k] = now;
            // (VDOLL_TEST_TRACE=1: each doll's place, aim and shots twice a second)
            if (gTrace && t % gTraceEvery == 0)
                printf("      t%d doll %zu at %.0f %.0f %.0f, yaw %.1f pitch %.1f, %s target %d, state %d, %d shots, stall %lld\n", t, k + 1, d.pos[0], d.pos[1], d.pos[2],
                       now.yaw * 57.29578f, now.pitch * 57.29578f, now.aiming ? "aiming" : "not aiming", now.target + 1, now.state, d.shots, d.stall);
        }
    }
    for (size_t k = 0; k < dolls.size(); ++k) vdoll::body::Down((int)k);
    gFrame += 30;
}

static doll::NavMesh* gNav = nullptr;
static doll::Geom gG;
static int gFails = 0;
static void Fail(const char* fmt, ...) {
    char b[600];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("  FAIL %s\n", b);
    ++gFails;
}
// a doll's place (hips) on the mesh at a spawn point, and an enemy's place (hips) min to max m away with a clear line from the
// doll's eye to his chest and head, on the mesh; (reach) a way between them
static bool PickPair(const float* spawn, float lo, float hi, bool reach, float* me, float* him) {
    float feet[3];
    if (!gNav->Nearest(spawn, 1500.f, feet)) return false;
    for (int tries = 0; tries < 400; ++tries) {
        float p[3];
        if (!gNav->RandomPoint(feet, hi + 5000.f, p, Rnd)) continue;
        const float flat = std::hypot(p[0] - feet[0], p[2] - feet[2]);
        if (flat < lo || flat > hi || std::fabs(p[1] - feet[1]) > 3500.f) continue;
        const float eye[3] = {feet[0], feet[1] + 800.f + 750.f, feet[2]}, chest[3] = {p[0], p[1] + 800.f + 350.f, p[2]}, head[3] = {p[0], p[1] + 800.f + 850.f, p[2]};
        if (!gNav->Clear(eye, chest) || !gNav->Clear(eye, head)) continue;
        if (reach) { std::vector<doll::Waypoint> w; float len = 0; if (!gNav->Path(feet, p, w, &len) || len > 2.f * flat + 10000.f) continue; }
        me[0] = feet[0]; me[1] = feet[1] + 800.f; me[2] = feet[2];
        him[0] = p[0]; him[1] = p[1] + 800.f; him[2] = p[2];
        return true;
    }
    return false;
}
static uint16_t Facing(const float* from, const float* to) {
    const float yaw = std::atan2(to[0] - from[0], to[2] - from[2]);
    return (uint16_t)((long)lrintf(yaw / 6.2831853f * 65536.f) & 0xFFFF);
}
static std::vector<float*> Spawns() {
    static std::vector<std::array<float, 3>> pts;
    pts.clear();
    for (const doll::Spawn& sp : gG.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) pts.push_back({sp.p[0], sp.p[1], sp.p[2]});
    std::vector<float*> out;
    for (auto& p : pts) out.push_back(p.data());
    return out;
}
static const long long kMaxStall = 300;
static void Judge(const char* what, const std::vector<Doll>& dolls, bool mustFire) {
    for (size_t k = 0; k < dolls.size(); ++k) {
        const Doll& d = dolls[k];
        printf("    %s: doll %zu: %d shots, its longest stall %lld frames, %.1f m run\n", what, k + 1, d.shots, d.maxStall, d.ran / 1000.f);
        if (d.maxStall > kMaxStall) {
            Fail("%s: doll %zu aimed %lld frames (%.1f s) with no shot and no move", what, k + 1, d.maxStall, d.maxStall / 60.f);
            printf("      %s\n", vdoll::body::Status((int)k).c_str());
        }
        if (mustFire && d.shots == 0) { Fail("%s: doll %zu never fired", what, k + 1); printf("      %s\n", vdoll::body::Status((int)k).c_str()); }
    }
}

static void PartCap9() {
    // (runs/V20_cap9: the dolls' own place records from frame 3957 to the end, the weapon records 13 00 0c 02, 13 00 0e 02)
    std::vector<Doll> dolls(3);
    const float red1[3] = {26936.6f, 3800.f, -37030.5f}, red3[3] = {26845.0f, 3800.f, -37030.5f}, blue2[3] = {25035.4f, 800.f, -11030.5f};
    memcpy(dolls[0].pos, red1, sizeof red1); dolls[0].team = 0; dolls[0].slot = 1;
    memcpy(dolls[1].pos, blue2, sizeof blue2); dolls[1].team = 1; dolls[1].slot = 2;
    memcpy(dolls[2].pos, red3, sizeof red3); dolls[2].team = 0; dolls[2].slot = 3;
    // (the host's copies: its lines `player 2 alive at 26937 3800 -37322`, `player 4 ... 26845 3800 -37322`, `player 3 ... 25474 800 -11588`)
    dolls[0].off[2] = dolls[2].off[2] = -37322.f - red1[2];
    dolls[1].off[0] = 25474.f - blue2[0]; dolls[1].off[2] = -11588.f - blue2[2];
    dolls[0].facing = Facing(red1, blue2); dolls[2].facing = Facing(red3, blue2); dolls[1].facing = Facing(blue2, red1);
    if (getenv("VDOLL_TEST_VERBOSE"))
        for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) if (dolls[a].team != dolls[b].team) {
            const Doll& B = dolls[b];
            const float e[3] = {dolls[a].pos[0], dolls[a].pos[1] + 750.f, dolls[a].pos[2]}, c[3] = {B.pos[0] + B.off[0], B.pos[1] + 350.f, B.pos[2] + B.off[2]}, h[3] = {B.pos[0] + B.off[0], B.pos[1] + 850.f, B.pos[2] + B.off[2]};
            printf("    line %d -> %d: chest %d head %d\n", a + 1, b + 1, (int)gNav->Clear(e, c), (int)gNav->Clear(e, h));
        }
    Play(dolls, {}, 2400);
    Judge("cap9", dolls, false);
    int all = 0;
    for (const Doll& d : dolls) all += d.shots;
    if (!all) Fail("cap9: no shot at all in 40 s");
}
static void PartPair() {
    int n = 0;
    for (float* sp : Spawns()) {
        if (n >= 4) break;
        float me[3], him[3];
        if (!PickPair(sp, 12000.f, 22000.f, false, me, him)) continue;
        for (float gap : {92.f, 400.f}) {
            std::vector<Doll> dolls(2);
            const float yaw = std::atan2(him[0] - me[0], him[2] - me[2]);
            memcpy(dolls[0].pos, me, sizeof me);
            dolls[1].pos[0] = me[0] + std::cos(yaw) * gap; dolls[1].pos[1] = me[1]; dolls[1].pos[2] = me[2] - std::sin(yaw) * gap;
            for (int k = 0; k < 2; ++k) { dolls[k].team = 0; dolls[k].slot = 1 + k; dolls[k].facing = Facing(dolls[k].pos, him); }
            Still e{{him[0], him[1], him[2]}, 1, 5};
            char what[64]; snprintf(what, sizeof what, "pair %d (%.0f mm apart, %.1f m)", n + 1, gap, std::hypot(him[0] - me[0], him[2] - me[2]) / 1000.f);
            Play(dolls, {e}, 600);
            Judge(what, dolls, true);
        }
        ++n;
    }
    if (!n) Fail("pair: no place found");
}
static void PartAhead() {
    int n = 0;
    for (float* sp : Spawns()) {
        if (n >= 4) break;
        float me[3], him[3];
        if (!PickPair(sp, 12000.f, 22000.f, false, me, him)) continue;
        const float dl = std::hypot(him[0] - me[0], him[2] - me[2]);
        const float ally[3] = {me[0] + (him[0] - me[0]) / dl * 4000.f, me[1] + (him[1] - me[1]) * 4000.f / dl, me[2] + (him[2] - me[2]) / dl * 4000.f};
        std::vector<Doll> dolls(1);
        memcpy(dolls[0].pos, me, sizeof me); dolls[0].team = 0; dolls[0].slot = 1; dolls[0].facing = Facing(me, him);
        char what[64]; snprintf(what, sizeof what, "ahead %d (%.1f m)", n + 1, dl / 1000.f);
        Play(dolls, {{{ally[0], ally[1], ally[2]}, 0, 4}, {{him[0], him[1], him[2]}, 1, 5}}, 900);
        Judge(what, dolls, false);
        if (dolls[0].shots == 0 && dolls[0].ran < 500.f) Fail("%s: neither fired nor moved", what);
        ++n;
    }
    if (!n) Fail("ahead: no place found");
}
static void PartM37() {
    int n = 0;
    for (float* sp : Spawns()) {
        if (n >= 4) break;
        float me[3], him[3];
        if (!PickPair(sp, 20000.f, 28000.f, true, me, him)) continue;
        std::vector<Doll> dolls(1);
        memcpy(dolls[0].pos, me, sizeof me); dolls[0].team = 0; dolls[0].slot = 1; dolls[0].facing = Facing(me, him);
        char what[64]; snprintf(what, sizeof what, "m37 %d (%.1f m)", n + 1, std::hypot(him[0] - me[0], him[2] - me[2]) / 1000.f);
        Play(dolls, {{{him[0], him[1], him[2]}, 1, 5}}, 1200);
        Judge(what, dolls, true);
        ++n;
    }
    if (!n) Fail("m37: no place found");
}
static void PartSweep() {
    int n = 0;
    std::vector<float*> sps = Spawns();
    for (int i = 0; i < 12 && !sps.empty(); ++i) {
        float me[3], him[3];
        if (!PickPair(sps[Rnd() % sps.size()], 3000.f, 40000.f, false, me, him)) continue;
        std::vector<Doll> dolls(2);
        const float yaw = std::atan2(him[0] - me[0], him[2] - me[2]), side = 300.f + 1500.f * Rnd01();
        memcpy(dolls[0].pos, me, sizeof me);
        dolls[1].pos[0] = me[0] + std::cos(yaw) * side; dolls[1].pos[1] = me[1]; dolls[1].pos[2] = me[2] - std::sin(yaw) * side;
        float f[3];
        const float q[3] = {dolls[1].pos[0], dolls[1].pos[1] - 800.f, dolls[1].pos[2]};
        if (gNav->Nearest(q, 600.f, f)) { dolls[1].pos[0] = f[0]; dolls[1].pos[1] = f[1] + 800.f; dolls[1].pos[2] = f[2]; }
        else memcpy(dolls[1].pos, me, sizeof me);
        for (int k = 0; k < 2; ++k) { dolls[k].team = 0; dolls[k].slot = 1 + k; dolls[k].facing = Facing(dolls[k].pos, him); }
        char what[64]; snprintf(what, sizeof what, "sweep %d (%.1f m)", n + 1, std::hypot(him[0] - me[0], him[2] - me[2]) / 1000.f);
        Play(dolls, {{{him[0], him[1], him[2]}, 1, 5}}, 900);
        Judge(what, dolls, false);
        ++n;
    }
}

static void PartAt() {
    float me[3], him[3];
    const char* e = getenv("VDOLL_TEST_AT");
    if (!e || sscanf(e, "%f,%f,%f %f,%f,%f", &me[0], &me[1], &me[2], &him[0], &him[1], &him[2]) != 6) { Fail("at: VDOLL_TEST_AT=\"x,y,z ex,ey,ez\" not given"); return; }
    std::vector<Doll> dolls(1);
    memcpy(dolls[0].pos, me, sizeof me); dolls[0].team = 0; dolls[0].slot = 1; dolls[0].facing = Facing(me, him);
    const float eye[3] = {me[0], me[1] + 750.f, me[2]}, chest[3] = {him[0], him[1] + 350.f, him[2]}, head[3] = {him[0], him[1] + 850.f, him[2]};
    printf("    at: %.1f m, %.1f m higher; from its eye his chest %s, his head %s\n", std::hypot(him[0] - me[0], him[2] - me[2]) / 1000.f, (him[1] - me[1]) / 1000.f,
           gNav->Clear(eye, chest) ? "seen" : "hidden", gNav->Clear(eye, head) ? "seen" : "hidden");
    Play(dolls, {{{him[0], him[1], him[2]}, 1, 5}}, 1200);
    if (getenv("MGO1_VDOLL_CQC_TEST")) { printf("    at (MGO1_VDOLL_CQC_TEST): doll 1: %d shots, its longest stall %lld frames, %.1f m run (not judged)\n", dolls[0].shots, dolls[0].maxStall, dolls[0].ran / 1000.f); return; }
    Judge("at", dolls, true);
}

// one part on one map, in a child process: its exit code the fails
static int RunPart(const char* geomPath, const std::string& part) {
    FILE* f = fopen(geomPath, "rb");
    if (!f) { perror(geomPath); return 1; }
    fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 1;
    fclose(f);
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), gG, err)) { printf("  FAIL %s: not a geom: %s\n", geomPath, err.c_str()); return 1; }
    gNav = new doll::NavMesh();
    doll::NavConfig cfg;
    std::string log;
    if (!gNav->Build(gG, cfg, log)) { printf("  FAIL %s: no mesh\n", geomPath); return 1; }
    if (part == "m37") setenv("MGO1_VDOLL_WEAPONS", "m37", 1);
    else if (part == "cap9") setenv("MGO1_VDOLL_WEAPONS", "ak47,m37,m37", 1);
    else if (part == "sweep") setenv("MGO1_VDOLL_WEAPONS", "auto", 1);
    else if (part == "at" && getenv("MGO1_VDOLL_WEAPONS")) {}
    else setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    gRnd ^= (unsigned)gGeom.size();
    if (!StartBody()) { printf("  FAIL the bodies' mesh was not built\n"); return 1; }
    if (const char* d = getenv("VDOLL_TEST_DUMP")) { gDump = fopen(d, "a"); if (gDump) fprintf(gDump, "# %s %s\n", geomPath, part.c_str()); }
    if (part == "cap9") PartCap9();
    else if (part == "pair") PartPair();
    else if (part == "ahead") PartAhead();
    else if (part == "m37") PartM37();
    else if (part == "sweep") PartSweep();
    else if (part == "at") PartAt();
    if (gDump) fclose(gDump);
    return gFails;
}

int main(int argc, char** argv) {
    std::vector<std::string> maps, parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "cap9" || a == "pair" || a == "ahead" || a == "m37" || a == "sweep" || a == "at") parts.push_back(a); else maps.push_back(a);
    }
    if (maps.empty()) { fprintf(stderr, "usage: vdoll_standoff_test <map.geom> ... [cap9|pair|ahead|m37|sweep ...]\n"); return 2; }
    if (parts.empty()) parts = {"cap9", "pair", "ahead", "m37", "sweep"};
    printf("controls %s, level %s, MGO1_VDOLL_STANDOFF=%s\n", getenv("MGO1_VDOLL_CONTROLS") ? getenv("MGO1_VDOLL_CONTROLS") : "direct",
           getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10", getenv("MGO1_VDOLL_STANDOFF") ? getenv("MGO1_VDOLL_STANDOFF") : "(on)");
    int fails = 0, runs = 0;
    for (const std::string& m : maps)
        for (const std::string& p : parts) {
            if (p == "cap9" && m.find("n024a") == std::string::npos) continue;   // (V20_cap9's places are map 9's)
            printf("%s %s\n", m.substr(m.find_last_of('/') + 1).c_str(), p.c_str());
            fflush(stdout);
            const pid_t pid = fork();
            if (pid == 0) { const int r = RunPart(m.c_str(), p); fflush(stdout); _exit(std::min(r, 100)); }
            int st = 0;
            waitpid(pid, &st, 0);
            const int r = WIFEXITED(st) ? WEXITSTATUS(st) : 100;
            if (r) printf("  %s %s: %d FAIL\n", m.c_str(), p.c_str(), r);
            fails += r; ++runs;
        }
    printf("%s: %d parts, %d fails\n", fails ? "FAIL" : "PASS", runs, fails);
    return fails ? 1 : 0;
}
