// (v150-host beta 25, rolltime) A virtual doll's roll against a player's (MGO1Loader/vdoll_body.cpp RollBegin, Roll, RollAsPlayer,
// Records' style, MoveCode; docs/VIRTUAL_DOLL.md 48), off line on a map's .geom, each host frame run as a real host runs it (the dolls'
// tick twice in a frame, every other frame). A player's roll measured on real clients (probe 51, benches rolltime_p1 and rolltime_p2):
// Cross while he runs, 4 ticks more of running, the roll 20 ticks (state 0x4240, his record {06 01}: 6 or 7 of them, one each 3
// ticks), its getting up 18 ticks (0x4642, {06 00}: 6 records), the bit 9 all along (the move's code 8), 5.1 m from the roll's start
// on a flat floor (5.5 m from the press while running), free 42 ticks (84 frames) after the press; no round before 5 ticks after the
// getting up. Each part in a process of its own:
//   test   no enemy, MGO1_VDOLL_TEST_ROLL=2: the doll roams 90 s and rolls along its way where the floor runs on; each roll: its first
//          {06 01} 6 to 14 frames after its press (the press's 3 ticks of running, the roll's state from the 4th; records each 6
//          frames), 6 or 7 of them, then 5 to 7 {06 00}, MoveCode 8 from the roll to its end and 3 (the run) before, free (its code no
//          longer 8) 82 to 86 frames after the press, 4.5 to 5.2 m from the first {06 01} to its end, 5.3 to 5.7 m from the press.
//          Beta 25 (MGO1_VDOLL_ROLL_TIME=0): {06 01} from its press, 6 records, no {06 00}, free after 36 frames, 2.6 m (FAILS)
//   into   an enemy standing 1.8 m away (level 10: its roll into him), where the floor runs on 6 m past him: the knock-down records
//          to him, and its roll on through him to its end (6 or 7 {06 01}, 5 to 7 {06 00}, 3.5 m at least from its first {06 01}).
//          Beta 25: it stopped 12 frames after it met him (FAILS)
//   shot   an enemy 8 m away in its fight, the doll hurt every 20 frames (level 10: its rolls aside): no shot record {08 05} from its
//          press to its end, its first one 10 frames at least after its end (a player's gun raised again), its next press 84 frames
//          at least after the last. Beta 25: free and firing 36 frames after its press (FAILS)
// MGO1_VDOLL_ROLL_TIME=0: every part expects beta 25's roll (and passes on its body).
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_rolltime_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_rolltime_test
//   ./vdoll_rolltime_test <map.geom> [<map.geom> ...] [test|into|shot ...]   (VDOLL_TEST_VERBOSE=1: the body's lines)
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
static long long gFrame = 600;
static std::vector<long long> gPresses;                   // (the test rolls' presses: the body's line "a test roll at")
static int gKnocks = 0;                                   // (its rolls into him: the body's line "rolls into player")
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, ": a test roll at ")) gPresses.push_back(gFrame);
    if (strstr(b, ": rolls into player")) ++gKnocks;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("      %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }                          // (team deathmatch)
static const int kDoll = 2, kFoe = 5;
static int gStyle = -1, gShot = 0, gToFoe = 0;           // (this frame: its style record, its shot records, its records to the enemy)
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) {
    if (slot == kDoll && kind == 1 && r.size() == 2 && r[0] == 0x06) gStyle = r[1];
    if (slot == kDoll && kind == 1 && r.size() >= 2 && r[0] == 0x08 && r[1] == 0x05) ++gShot;
    if (slot == kFoe) ++gToFoe;
}
static unsigned gRnd = 0x2545F491u;
static unsigned Rnd() { gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5; return gRnd; }
static doll::NavMesh* gNav = nullptr;
static doll::Geom gG;
static int gFails = 0;
static bool gOld = false;                                // (MGO1_VDOLL_ROLL_TIME=0: beta 25's roll expected)
static void Check(bool ok, const char* fmt, ...) {
    char b[700];
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
// his chest and head; past (for a roll into him): the standing floor runs on that far beyond him on the doll's line
static bool PickPlaces(const float* spawn, float d, float past, float* foe, float* doll) {
    float feet[3];
    if (!gNav->Nearest(spawn, 1500.f, feet)) return false;
    for (int tries = 0; tries < 400; ++tries) {
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
        if (past > 0.f) {                                 // (the doll's line on through him: the standing floor)
            const float ux = p[0] - m[0], uz = p[2] - m[2], l = std::hypot(ux, uz);
            if (gNav->ClearAlong(m, ux / l, uz / l, d + past, true) < d + past - 50.f) continue;
        }
        memcpy(foe, p, sizeof p); memcpy(doll, m, sizeof m);
        return true;
    }
    return false;
}
// one host frame as a real host runs it: the frame advances by two, the dolls' tick comes twice (the second pass with no frame since)
struct Frame { int style = -1, shots = 0, code = 0, toFoe = 0; float pos[3] = {0, 0, 0}; };
static Frame HostFrame(vdoll::body::Self& s, float* pos) {
    Frame f;
    gFrame += 2;
    memcpy(gPlayers[kDoll].pos, pos, 3 * sizeof(float));   // (the host's copy of the doll where it is)
    gStyle = -1; gShot = 0; gToFoe = 0;
    for (int pass = 0; pass < 2; ++pass) {
        vdoll::body::Frame(gFrame);
        s.tick = pass == 0 && gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
    }
    f.style = gStyle; f.shots = gShot; f.toFoe = gToFoe;
    f.code = vdoll::body::MoveCode(0);                    // (what vdoll.cpp's MoveCostTick reads at the next frame's start)
    memcpy(f.pos, pos, sizeof f.pos);
    return f;
}
// a roll read from the frames: from its press (or its first {06 01} when the press is not known) to the end of its code 8
struct RollSeen { long long press = -1, first01 = -1, free = -1; int n01 = 0, n00 = 0, leadNot3 = 0, shotsIn = 0; long long firstShotAfter = -1;
                  float fromFirst = 0.f, fromPress = 0.f; };
static RollSeen ReadRoll(const std::vector<std::pair<long long, Frame>>& fr, long long press, size_t from) {
    RollSeen r; r.press = press;
    float p0[3] = {0, 0, 0}, p1[3] = {0, 0, 0};
    bool in01 = false, in00 = false, done = false;
    for (size_t i = from; i < fr.size(); ++i) {
        const long long t = fr[i].first; const Frame& f = fr[i].second;
        if (press >= 0 && t == press) memcpy(p0, f.pos, sizeof p0);        // (where it was at its press)
        if (r.first01 < 0 && f.style == 0x01) { r.first01 = t; memcpy(p1, f.pos, sizeof p1); in01 = true; if (press < 0) { r.press = t; memcpy(p0, f.pos, sizeof p0); } }
        if (r.first01 < 0) { if (f.code != 3 && press >= 0 && t > press && f.code != 8) ++r.leadNot3; continue; }   // (its run before the roll: 3)
        if (!done) {
            if (f.style == 0x01 && in01) ++r.n01;
            else if (f.style == 0x00 && (in01 || in00)) { in01 = false; in00 = true; ++r.n00; }
            else if (f.style >= 0 && f.style != 0x01 && f.style != 0x00) { in01 = in00 = false; }
            if (!gOld || t >= r.first01 + 6) r.shotsIn += f.shots;   // (beta 25: a shot just before its roll has its record in the roll's first)
            if (f.code != 8) { r.free = t; done = true; r.fromFirst = std::hypot(f.pos[0] - p1[0], f.pos[2] - p1[2]); r.fromPress = std::hypot(f.pos[0] - p0[0], f.pos[2] - p0[2]); }
            continue;
        }
        if (f.shots && r.firstShotAfter < 0) { r.firstShotAfter = t; break; }
        if (t - r.free > 400) break;
    }
    return r;
}
static void PartTest() {
    const doll::Spawn* sp = nullptr;
    for (const doll::Spawn& x : gG.spawns) if (x.kind == doll::SP_DM || x.kind == doll::SP_TDM_RED || x.kind == doll::SP_TDM_BLUE) { sp = &x; break; }
    if (!sp) { Check(false, "test: no spawn point"); return; }
    float feet[3];
    if (!gNav->Nearest(sp->p, 1500.f, feet)) { Check(false, "test: no mesh at the spawn point"); return; }
    float pos[3] = {feet[0], feet[1] + 800.f, feet[2]};
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = kDoll; s.team = 1; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
    Player(kDoll, 1, feet);
    std::vector<std::pair<long long, Frame>> fr;
    for (int t = 0; t < 2700; ++t) fr.push_back({gFrame + 2, HostFrame(s, pos)});   // (90 s)
    int rolls = 0, okLead = 0, okRecs = 0, okFree = 0, okWay = 0;
    for (long long press : gPresses) {
        size_t i = 0; while (i < fr.size() && fr[i].first < press) ++i;
        const RollSeen r = ReadRoll(fr, press, i);
        if (r.free < 0) continue;                         // (the run's end)
        if (r.first01 - press > 30) continue;             // (no roll from this press: a drop began in its frame and outlasted it)
        ++rolls;
        const long long lead = r.first01 - press, len = r.free - press;
        bool a, b, d, e;
        if (gOld) { a = lead >= 0 && lead <= 6; b = r.n01 == 6 && r.n00 == 0; d = len >= 34 && len <= 38; e = r.fromPress > 2300.f && r.fromPress < 2800.f; }
        else { a = lead >= 6 && lead <= 14 && r.leadNot3 == 0; b = r.n01 >= 6 && r.n01 <= 7 && r.n00 >= 5 && r.n00 <= 7; d = len >= 82 && len <= 86;
               e = r.fromFirst > 4500.f && r.fromFirst < 5200.f && r.fromPress > 5300.f && r.fromPress < 5700.f; }
        okLead += a; okRecs += b; okFree += d; okWay += e;
        printf("    roll at %lld: its first {06 01} +%lld (%d frames of its run before not coded 3), %d of them then %d {06 00}, its code 8 to +%lld, %.2f m from its first {06 01}, %.2f m from its press%s\n",
               press, lead, r.leadNot3, r.n01, r.n00, len, r.fromFirst / 1000.f, r.fromPress / 1000.f, a && b && d && e ? "" : " <-");
    }
    if (gOld) Check(rolls >= 3 && okLead == rolls && okRecs == rolls && okFree == rolls && okWay == rolls,
                    "test (MGO1_VDOLL_ROLL_TIME=0, beta 25): %d rolls: {06 01} from its press %d, 6 of them and no {06 00} %d, free after 36 frames %d, 2.6 m %d", rolls, okLead, okRecs, okFree, okWay);
    else Check(rolls >= 3 && okLead == rolls && okRecs == rolls && okFree == rolls && okWay == rolls,
               "test: %d rolls as a player's: its press's run (code 3) then {06 01} %d, 6 or 7 {06 01} then 5 to 7 {06 00} %d, its code 8 from the roll to 84 frames after its press (free) %d, 5.1 m (5.5 from its press) %d",
               rolls, okLead, okRecs, okFree, okWay);
    vdoll::body::Down(0);
}
static void PartClose(bool into) {
    std::vector<std::array<float, 3>> pts;
    for (const doll::Spawn& sp : gG.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) pts.push_back({sp.p[0], sp.p[1], sp.p[2]});
    int cases = 0, rolls = 0, okRecs = 0, okWay = 0, okShot = 0, okGap = 0, knocks = 0, gaps = 0;
    for (size_t i = 0; i < pts.size() && cases < 12 && rolls < 4; ++i) {
        float foe[3], dollFeet[3];
        if (!PickPlaces(pts[i].data(), into ? 1800.f : 8000.f, into ? 6000.f : 0.f, foe, dollFeet)) continue;
        ++cases;
        for (int k = 1; k < 16; ++k) gHave[k] = false;
        Player(kFoe, 0, foe);
        float pos[3] = {dollFeet[0], dollFeet[1] + 800.f, dollFeet[2]};
        uint16_t facing = Facing(pos, foe);
        vdoll::body::Self s;
        s.k = 0; s.slot = kDoll; s.team = 1; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
        s.hp = 200; s.attacker = -1;
        Player(kDoll, 1, dollFeet);
        const int knocksWas = gKnocks;
        std::vector<std::pair<long long, Frame>> fr;
        for (int t = 0; t < (into ? 180 : 600); ++t) {     // (6 s close, 20 s at 8 m)
            if (!into && t % 10 == 5 && s.hp > 20) { s.hp -= 2; s.attacker = kFoe; }   // (hurt every 20 frames: its rolls aside)
            fr.push_back({gFrame + 2, HostFrame(s, pos)});
            if (into) { gPlayers[kFoe].pos[1] = foe[1] + 800.f; }
        }
        knocks += gKnocks - knocksWas;
        long long lastPress = -1;
        for (size_t j = 0; j < fr.size(); ++j) {
            if (fr[j].second.code != 8 || (j > 0 && fr[j - 1].second.code == 8)) continue;
            // (a roll's first frame coded 8, its first {06 01} at or after it: its press its run before, 8 frames as a player's, beta 25's
            // at once)
            const RollSeen r = ReadRoll(fr, -1, j);
            if (r.free < 0 || r.first01 < 0) continue;
            ++rolls;
            // (beta 25: its roll's {06 01} only; a {06 00} after it is its fight's aim, not a getting up: its code 8 is over by then)
            const bool recs = gOld ? r.n01 >= 2 && r.n01 <= 6 && r.free - r.first01 <= 36 : r.n01 >= 6 && r.n01 <= 7 && r.n00 >= 5 && r.n00 <= 7;
            const bool way = gOld ? r.fromFirst < 2800.f : r.fromFirst > 3500.f;
            const long long raise = r.firstShotAfter >= 0 ? r.firstShotAfter - r.free : 9999;
            const bool shot = r.shotsIn == 0 && (gOld ? true : raise >= 10);
            okRecs += recs; okWay += way; okShot += shot;
            const long long press = r.first01 - (gOld ? 0 : 8);
            if (lastPress >= 0) { ++gaps; okGap += press - lastPress >= (gOld ? 36 : 84); }
            lastPress = press;
            printf("    %s roll at %lld: %d {06 01} then %d {06 00}, free %lld frames after its first {06 01}, %.2f m from it, %d shots meanwhile, its first shot %lld frames after its end%s\n",
                   into ? "into" : "aside", r.first01, r.n01, r.n00, r.free - r.first01, r.fromFirst / 1000.f, r.shotsIn, raise, recs && way && shot ? "" : " <-");
            while (j + 1 < fr.size() && fr[j + 1].first <= r.free) ++j;
        }
        vdoll::body::Down(0);
        gFrame += 120;
    }
    if (into) {
        printf("    %d cases at 1.8 m with 6 m of floor past him: %d rolls, %d knock-downs\n", cases, rolls, knocks);
        if (gOld) Check(rolls >= 1 && knocks >= 1 && okRecs == rolls && okWay == rolls, "into (MGO1_VDOLL_ROLL_TIME=0, beta 25): its roll stopped on him (6 {06 01} at most, its code 8 36 frames at most, under 2.8 m)");
        else Check(rolls >= 1 && knocks >= 1 && okRecs == rolls && okWay == rolls, "into: he is knocked down and its roll goes on through him as a player's (6 or 7 {06 01}, 5 to 7 {06 00}, 3.5 m at least)");
    } else {
        printf("    %d cases at 8 m, hurt every 20 frames: %d rolls\n", cases, rolls);
        if (gOld) Check(rolls >= 1 && okShot == rolls && okGap == gaps, "shot (MGO1_VDOLL_ROLL_TIME=0, beta 25): no shot during its 36 frames, its rolls 36 frames apart at least");
        else Check(rolls >= 1 && okShot == rolls && okGap == gaps && okRecs == rolls,
                   "shot: no shot from its press to its end, its first one 10 frames at least after it (its gun raised again), its presses 84 frames apart at least (%d of %d, %d of %d gaps)", okShot, rolls, okGap, gaps);
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
    if (part == "test") setenv("MGO1_VDOLL_TEST_ROLL", "2", 1);
    gRnd ^= (unsigned)gGeom.size();
    if (!StartBody()) { printf("  FAIL the bodies' mesh was not built\n"); return 1; }
    if (part == "test") PartTest(); else PartClose(part == "into");
    return gFails;
}
int main(int argc, char** argv) {
    std::vector<std::string> maps, parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "test" || a == "into" || a == "shot") parts.push_back(a); else maps.push_back(a);
    }
    if (maps.empty()) { fprintf(stderr, "usage: vdoll_rolltime_test <map.geom> ... [test|into|shot ...]\n"); return 2; }
    if (parts.empty()) parts = {"test", "into", "shot"};
    // (VDOLL_TEST_EXPECT=player: a player's roll expected whatever the switch: beta 25's roll then FAILS)
    gOld = getenv("MGO1_VDOLL_ROLL_TIME") && !strcmp(getenv("MGO1_VDOLL_ROLL_TIME"), "0") && !(getenv("VDOLL_TEST_EXPECT") && !strcmp(getenv("VDOLL_TEST_EXPECT"), "player"));
    printf("level %s, MGO1_VDOLL_ROLL_TIME=%s, %s expected\n", getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10",
           getenv("MGO1_VDOLL_ROLL_TIME") ? getenv("MGO1_VDOLL_ROLL_TIME") : "(on)", gOld ? "beta 25's roll" : "a player's roll");
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
