// (v150-host beta 25, aimcost) A virtual doll's moves as its stamina's cost reads them (MGO1Loader/vdoll_body.cpp MoveCode, docs/VIRTUAL_DOLL.md
// 45), off line on a map's .geom, each host frame run as a real host runs it: its game runs its actor lists twice for a tick
// (docs/FPS60.md), so the dolls' tick comes twice in a frame and not at all in the next (bench aimcost_d2: 7577 frames twice, 2559
// once, 7857 none); here every other frame twice. Each part in a process of its own (the bodies start afresh):
//   run    no enemy, the doll roams 60 s: after each host frame in which its place moved 50 mm or more on its floor (a run is 129 mm
//          in two frames, a walk half; a drop's fall or a climb, 60 mm up or down, apart), MoveCode must give a move (3 or 1); 2 % of
//          them at most may read still (a way's end, a turn).
//          Beta 25 read its body's last pass (no frame since: its speed 0): every one still (FAILS on 65673d2's body)
//   aim    an enemy standing 8 m away in its sight, 6 s, at each disc map's spawn points: its frames aiming (its fight), none of them
//          with its place moving, MoveCode 0 in each (a player standing with his gun raised; the game reads no aim bit, only the
//          move: probe 50, a real client readied and running pays the run, readied and walking the walk, in the first person view
//          or a scope he does not move)
//   roll   an enemy standing 1.8 m away (level 10: its roll into him), 6 s, at the spawn points until three rolls: in each frame
//          its roll's style record {06 01} goes out, MoveCode 8, a player's roll (state 0x4240 then 0x4642, the bit 9 with 0xE: the
//          stage's 98, 134 a drain with a load of 7000 on a real client, bench aimcost_p1). Beta 25: 3, the run's (FAILS)
// MGO1_VDOLL_MOVE_FRAME=0 (beta 25's MoveCode): run then expects every moving frame read still and roll a 3; aim the same.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_aimcost_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_aimcost_test
//   ./vdoll_aimcost_test <map.geom> [<map.geom> ...] [run|aim|roll ...]   (VDOLL_TEST_VERBOSE=1: the body's lines)
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
    if (getenv("VDOLL_TEST_VERBOSE")) printf("      %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }                          // (team deathmatch)
static long long gFrame = 600;
static const int kDoll = 2, kFoe = 5;
static bool gRollRec = false;                            // (this pass sent its roll's style record {06 01})
static void Send(void*, int, int kind, const std::vector<uint8_t>& r) { if (kind == 1 && r.size() == 2 && r[0] == 0x06 && r[1] == 0x01) gRollRec = true; }
static unsigned gRnd = 0x2545F491u;
static unsigned Rnd() { gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5; return gRnd; }
static doll::NavMesh* gNav = nullptr;
static doll::Geom gG;
static int gFails = 0;
static bool gOld = false;                                // (MGO1_VDOLL_MOVE_FRAME=0: beta 25's MoveCode expected)
static void Check(bool ok, const char* fmt, ...) {
    char b[600];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("  %s %s\n", ok ? "PASS" : "FAIL", b);
    if (!ok) ++gFails;
}
static bool StartBody() {
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (int w = 0; w < 12000 && !gMeshSaid; ++w) { vdoll::body::Frame(0); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return gMeshSaid;
}
static uint16_t Facing(const float* from, const float* to) {
    const float yaw = std::atan2(to[0] - from[0], to[2] - from[2]);
    return (uint16_t)((long)lrintf(yaw / 6.2831853f * 65536.f) & 0xFFFF);
}
static bool OnMesh(const float* p, float* out) { float f[3]; if (!gNav->Nearest(p, 400.f, f) || std::hypot(f[0] - p[0], f[2] - p[2]) > 300.f || std::fabs(f[1] - p[1]) > 400.f) return false; memcpy(out, f, sizeof f); return true; }
static void Player(int id, int team, const float* feet) {
    gHave[id] = true;
    vdoll::PlayerNow& p = gPlayers[id];
    p = vdoll::PlayerNow(); p.team = team; p.object = p.alive = true; p.life = 200; p.cls = 1;
    p.pos[0] = feet[0]; p.pos[1] = feet[1] + 800.f; p.pos[2] = feet[2];
}
// the enemy's place (feet) near a spawn point with open floor round it, the doll's d mm from him with a clear line from its eye to
// his chest and head (and, for a roll, a flat floor and a clear line along the ground between them)
static bool PickPlaces(const float* spawn, float d, float* foe, float* doll) {
    float feet[3];
    if (!gNav->Nearest(spawn, 1500.f, feet)) return false;
    for (int tries = 0; tries < 300; ++tries) {
        float p[3];
        if (!gNav->RandomPoint(feet, 12000.f, p, Rnd)) continue;
        bool open = true;
        for (int a = 0; a < 8 && open; ++a) { const float q[3] = {p[0] + std::sin(a * 0.785f) * 1200.f, p[1], p[2] + std::cos(a * 0.785f) * 1200.f}; float f[3]; open = OnMesh(q, f); }
        if (!open) continue;
        const float yaw = (float)(Rnd() % 6283) / 1000.f;
        const float q[3] = {p[0] + std::sin(yaw) * d, p[1], p[2] + std::cos(yaw) * d};
        float m[3];
        if (!OnMesh(q, m) || std::fabs(m[1] - p[1]) > 200.f) continue;
        const float eye[3] = {m[0], m[1] + 800.f + 750.f, m[2]}, chest[3] = {p[0], p[1] + 800.f + 350.f, p[2]}, head[3] = {p[0], p[1] + 800.f + 850.f, p[2]};
        if (!gNav->Clear(eye, chest) || !gNav->Clear(eye, head)) continue;
        float len = 0, end[3];
        if (!vdoll::body::WayTo(m, p, &len, end) || len > d + 1500.f) continue;
        memcpy(foe, p, sizeof p); memcpy(doll, m, sizeof m);
        return true;
    }
    return false;
}
// one host frame as a real host runs it: the frame advances by two, the dolls' tick comes twice (the second pass with no frame since)
struct Frame { float moved = 0, dy = 0; int code = 0; bool aiming = false, roll = false; };
static Frame HostFrame(vdoll::body::Self& s, float* pos) {
    Frame f;
    const float p0[3] = {pos[0], pos[1], pos[2]};
    gFrame += 2;
    memcpy(gPlayers[kDoll].pos, pos, 3 * sizeof(float));   // (the host's copy of the doll where it is)
    gRollRec = false;
    for (int pass = 0; pass < 2; ++pass) {
        vdoll::body::Frame(gFrame);
        s.tick = pass == 0 && gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
    }
    f.moved = std::hypot(pos[0] - p0[0], pos[2] - p0[2]); f.dy = pos[1] - p0[1];
    f.code = vdoll::body::MoveCode(0);                    // (what vdoll.cpp's MoveCostTick reads at the next frame's start)
    vdoll::body::AimInfo a;
    vdoll::body::Aim(0, &a);
    f.aiming = a.aiming;
    f.roll = gRollRec;
    return f;
}
static void PartRun() {
    const doll::Spawn* sp = nullptr;
    for (const doll::Spawn& x : gG.spawns) if (x.kind == doll::SP_DM || x.kind == doll::SP_TDM_RED || x.kind == doll::SP_TDM_BLUE) { sp = &x; break; }
    if (!sp) { Check(false, "run: no spawn point"); return; }
    float feet[3];
    if (!gNav->Nearest(sp->p, 1500.f, feet)) { Check(false, "run: no mesh at the spawn point"); return; }
    float pos[3] = {feet[0], feet[1] + 800.f, feet[2]};
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = kDoll; s.team = 1; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
    Player(kDoll, 1, feet);
    int moving = 0, still = 0, run = 0, walk = 0, others = 0, drops = 0;
    std::vector<Frame> fr;
    for (int t = 0; t < 1800; ++t) fr.push_back(HostFrame(s, pos));   // (1800 host frames of two: 60 s)
    // ((v150-host beta 25, fallflinch) a drop's fall and its landing's hold: the frames from 6 before one with a fall or a climb (a step of
    // 60 mm up or down) to 30 after it, whatever their own step: their code is a player's ledge action's 8 with MGO1_VDOLL_HIT_FALL, its
    // first frames' drop under 60 mm; not this test's)
    std::vector<bool> near(fr.size(), false);
    for (size_t i = 0; i < fr.size(); ++i)
        if (std::fabs(fr[i].dy) > 60.f) for (size_t j = i >= 6 ? i - 6 : 0; j < fr.size() && j <= i + 30; ++j) near[j] = true;
    for (size_t i = 0; i < fr.size(); ++i) {
        const Frame& f = fr[i];
        if (f.moved < 50.f || f.roll) continue;
        if (near[i]) { ++drops; continue; }               // (a drop's fall or a climb: their codes are not this test's)
        ++moving;
        if (f.code == 3) ++run; else if (f.code == 1) ++walk; else if (f.code == 0) ++still; else ++others;
    }
    printf("    60 s roaming, each frame two passes: %d frames moving 50 mm or more on its floor (%d more falling or climbing): MoveCode 3 in %d, 1 in %d, 0 in %d, other %d\n",
           moving, drops, run, walk, still, others);
    if (gOld) Check(moving >= 300 && still * 10 >= moving * 9, "run (MGO1_VDOLL_MOVE_FRAME=0, beta 25): its moving frames read still (its last pass, no frame since)");
    else Check(moving >= 300 && still * 50 <= moving && others == 0, "run: its moving frames read as a move (2 %% still at most)");
    vdoll::body::Down(0);
}
static void PartClose(bool roll) {
    std::vector<std::array<float, 3>> pts;
    for (const doll::Spawn& sp : gG.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) pts.push_back({sp.p[0], sp.p[1], sp.p[2]});
    int cases = 0, aimF = 0, aimMoving = 0, aimCoded = 0, rolls = 0, rollF = 0, rollRight = 0, rollRun = 0;
    for (size_t i = 0; i < pts.size() && cases < 12 && (!roll || rolls < 3); ++i) {
        float foe[3], dollFeet[3];
        if (!PickPlaces(pts[i].data(), roll ? 1800.f : 8000.f, foe, dollFeet)) continue;
        ++cases;
        for (int k = 1; k < 16; ++k) gHave[k] = false;
        Player(kFoe, 0, foe);
        float pos[3] = {dollFeet[0], dollFeet[1] + 800.f, dollFeet[2]};
        uint16_t facing = Facing(pos, foe);
        vdoll::body::Self s;
        s.k = 0; s.slot = kDoll; s.team = 1; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
        Player(kDoll, 1, dollFeet);
        bool rolled = false;
        for (int t = 0; t < 180; ++t) {                   // (6 s)
            const Frame f = HostFrame(s, pos);
            if (f.aiming && !f.roll) { ++aimF; if (f.moved > 1.f) ++aimMoving; if (f.code != 0) ++aimCoded; }
            if (f.roll) { rolled = true; ++rollF; if (f.code == (gOld ? 3 : 8)) ++rollRight; if (f.code == 3) ++rollRun; }
        }
        if (rolled) ++rolls;
        vdoll::body::Down(0);
        gFrame += 120;
    }
    if (!roll) {
        printf("    %d cases at 8 m: %d frames aiming, %d of them with its place moving, %d with a move's code\n", cases, aimF, aimMoving, aimCoded);
        Check(cases >= 1 && aimF >= 60 * cases && aimMoving == 0 && aimCoded == 0, "aim: its aiming frames still, as a player standing with his gun raised (no move, no code)");
    } else {
        printf("    %d cases at 1.8 m: %d rolls, %d frames with its roll's record: MoveCode %s in %d (the run's 3 in %d)\n", cases, rolls, rollF, gOld ? "3" : "8", rollRight, rollRun);
        Check(rolls >= 1 && rollF >= 3 && rollRight == rollF, gOld ? "roll (MGO1_VDOLL_MOVE_FRAME=0, beta 25): the run's code 3" : "roll: a player's roll's code 8 in each of its frames");
    }
}
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
    if (!getenv("MGO1_VDOLL_WEAPONS")) setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    gRnd ^= (unsigned)gGeom.size();
    if (!StartBody()) { printf("  FAIL the bodies' mesh was not built\n"); return 1; }
    if (part == "run") PartRun(); else PartClose(part == "roll");
    return gFails;
}
int main(int argc, char** argv) {
    std::vector<std::string> maps, parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "run" || a == "aim" || a == "roll") parts.push_back(a); else maps.push_back(a);
    }
    if (maps.empty()) { fprintf(stderr, "usage: vdoll_aimcost_test <map.geom> ... [run|aim|roll ...]\n"); return 2; }
    if (parts.empty()) parts = {"run", "aim", "roll"};
    gOld = getenv("MGO1_VDOLL_MOVE_FRAME") && !strcmp(getenv("MGO1_VDOLL_MOVE_FRAME"), "0");
    printf("level %s, MGO1_VDOLL_MOVE_FRAME=%s\n", getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10", gOld ? "0 (beta 25's expected)" : "(on)");
    int fails = 0, runs = 0;
    for (const std::string& m : maps)
        for (const std::string& p : parts) {
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
