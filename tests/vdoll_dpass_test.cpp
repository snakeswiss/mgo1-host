// (v150-host beta 25, dpass) A virtual doll's every-100-ms decisions and its turn when the host runs its tick twice in a frame
// (MGO1Loader/vdoll_body.cpp Alive and TurnBody, MGO1_VDOLL_ONE_PASS; docs/VIRTUAL_DOLL.md 47), off line on a map's .geom. Each host frame
// is run as a real host runs it: its game runs its actor lists twice for a tick (docs/FPS60.md), the dolls' tick at the start of each,
// so the tick comes twice in a frame and not at all in the next (the frame advances by two, the second pass with no frame since). The
// other off-line tests call the tick once a frame and never showed it. Each part in a process of its own (the bodies start afresh):
//   held   the doll on its way; its copy on the host stopped where it was (held by the game): HeldBack takes the doll back to its copy
//          after 3 reads of the players (300 ms) with the copy still 1.2 m or more behind it, never before. Beta 25 counted both passes of
//          a read's frame (the second after the doll's move of the first): back after 2 reads, or 1 (FAILS on 806009c's body)
//   turn   60 s on its ways, then 12 fights against an enemy 5 m behind it: in no host frame (two frames) does it turn more than its
//          16 degrees a frame twice (32). Beta 25 turned a frame's worth in the pass with no frame since too: 48 (FAILS)
//   roll   level 10, an enemy 6 m away in its sight, both sides of it clear for a roll; one hit (its life 1 less, by him) a case, 150
//          cases: its roll when hurt (35 % a look, the two looks of the 12 frames after the hit) comes in 45 to 70 % of the cases
//          (1 - 0.65^2 = 58 %). Beta 25 drew it in both passes of each read's frame: 1 - 0.65^4 = 82 % (FAILS)
//   reads  (the counts of vdoll_body.h Decisions; a body without them: skipped) the cases of roll and a close fight at 1.8 m (its roll
//          into him, its grab, its rush): its decisions ran once a read, none in a pass with no frame since the last, and no chance
//          (roll, grab, roll into, rush) drawn more often than the reads
// MGO1_VDOLL_ONE_PASS=0 (beta 25's): held then expects 1 or 2 reads, turn a frame over 32 degrees, roll more than 70 %, reads twice.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_dpass_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_dpass_test
//   ./vdoll_dpass_test <map.geom> [<map.geom> ...] [held|turn|roll|reads ...]   (VDOLL_TEST_VERBOSE=1: the body's lines)
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
static long long gFrame = 600;
static long long gReadAt = -1;                           // (the frame of the players' last read: the body asked for its own copy)
static const int kDoll = 2, kFoe = 5;
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (id == kDoll) gReadAt = gFrame; if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }                          // (team deathmatch)
static bool gRollRec = false;                            // (this host frame sent its roll's style record {06 01})
static void Send(void*, int, int kind, const std::vector<uint8_t>& r) { if (kind == 1 && r.size() == 2 && r[0] == 0x06 && r[1] == 0x01) gRollRec = true; }
static unsigned gRnd = 0x2545F491u;
static unsigned Rnd() { gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5; return gRnd; }
static doll::NavMesh* gNav = nullptr;
static doll::Geom gG;
static int gFails = 0;
static bool gOld = false;                                // (MGO1_VDOLL_ONE_PASS=0: beta 25's expected)
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
static float YawTo(const float* from, const float* to) { return std::atan2(to[0] - from[0], to[2] - from[2]); }
static uint16_t Facing(float yaw) { return (uint16_t)((long)lrintf(yaw / 6.2831853f * 65536.f) & 0xFFFF); }
static float Wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static bool OnMesh(const float* p, float* out) { float f[3]; if (!gNav->Nearest(p, 400.f, f) || std::hypot(f[0] - p[0], f[2] - p[2]) > 300.f || std::fabs(f[1] - p[1]) > 400.f) return false; memcpy(out, f, sizeof f); return true; }
static void Player(int id, int team, const float* feet) {
    gHave[id] = true;
    vdoll::PlayerNow& p = gPlayers[id];
    p = vdoll::PlayerNow(); p.team = team; p.object = p.alive = true; p.life = 200; p.cls = 1;
    p.pos[0] = feet[0]; p.pos[1] = feet[1] + 800.f; p.pos[2] = feet[2];
}
// the doll's place (feet) near a spawn point with open floor round it (and, rollRoom, 2.6 m clear on both sides square to its line to
// him), the enemy's d mm from it with a clear line from its eye to his chest and head and a way between them
static bool PickPlaces(const float* spawn, float d, bool rollRoom, float* doll, float* foe) {
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
        const float eye[3] = {p[0], p[1] + 800.f + 750.f, p[2]}, chest[3] = {m[0], m[1] + 800.f + 350.f, m[2]}, head[3] = {m[0], m[1] + 800.f + 850.f, m[2]};
        if (!gNav->Clear(eye, chest) || !gNav->Clear(eye, head)) continue;
        if (rollRoom) {
            const float to = YawTo(p, m);
            bool room = true;
            // ((v150-host beta 25, rolltime) a player's roll needs 5.5 m of floor (MGO1_VDOLL_ROLL_TIME), beta 25's 2.4 m)
            const bool oldRoll = getenv("MGO1_VDOLL_ROLL_TIME") && !strcmp(getenv("MGO1_VDOLL_ROLL_TIME"), "0");
            const float way = oldRoll ? 2450.f : 5600.f;
            for (float side : {1.f, -1.f}) { const float y = to + side * 1.5707963f; if (gNav->ClearAlong(p, std::sin(y), std::cos(y), way + 150.f, true) < way) room = false; }
            if (!room) continue;
        }
        float len = 0, end[3];
        if (!vdoll::body::WayTo(p, m, &len, end) || len > d + 1500.f) continue;
        memcpy(doll, p, sizeof p); memcpy(foe, m, sizeof m);
        return true;
    }
    return false;
}
static std::vector<std::array<float, 3>> Spawns() {
    std::vector<std::array<float, 3>> pts;
    for (const doll::Spawn& sp : gG.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) pts.push_back({sp.p[0], sp.p[1], sp.p[2]});
    return pts;
}
// one host frame as a real host runs it: the frame advances by two, the dolls' tick comes twice (the second pass with no frame since);
// the host's copy of the doll where the doll is, unless held (then where it was held); its yaw before and after
struct Host { bool read = false; float yaw0 = 0, yaw1 = 0; };
static Host HostFrame(vdoll::body::Self& s, float* pos, bool held = false) {
    Host f;
    vdoll::body::AimInfo a;
    vdoll::body::Aim(0, &a); f.yaw0 = a.yaw;
    gFrame += 2;
    if (!held) memcpy(gPlayers[kDoll].pos, pos, 3 * sizeof(float));
    const long long readWas = gReadAt;
    for (int pass = 0; pass < 2; ++pass) {
        vdoll::body::Frame(gFrame);
        s.tick = pass == 0 && gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
    }
    f.read = gReadAt != readWas;
    vdoll::body::Aim(0, &a); f.yaw1 = a.yaw;
    return f;
}
static vdoll::body::Self NewSelf(float* pos, uint16_t* facing) {
    vdoll::body::Self s;
    s.k = 0; s.slot = kDoll; s.team = 1; s.life = gFrame; s.pos = pos; s.facing = facing; s.send = Send; s.hp = 200; s.attacker = -1;
    return s;
}
static void PartHeld() {
    const auto pts = Spawns();
    if (pts.empty()) { Check(false, "held: no spawn point"); return; }
    std::vector<int> counts;
    int tried = 0;
    for (size_t i = 0; i < pts.size() && counts.size() < 6 && tried < 24; ++i) {
        float feet[3];
        if (!gNav->Nearest(pts[i].data(), 1500.f, feet)) continue;
        for (int k = 1; k < 16; ++k) gHave[k] = false;
        float pos[3] = {feet[0], feet[1] + 800.f, feet[2]};
        uint16_t facing = 0;
        vdoll::body::Self s = NewSelf(pos, &facing);
        Player(kDoll, 1, feet);
        for (int t = 0; t < 150; ++t) HostFrame(s, pos);    // (5 s on its ways)
        for (int c = 0; c < 4 && counts.size() < 6; ++c, ++tried) {
            // its copy held where it is now; the reads with the copy 1.2 m or more behind it until it is taken back to it
            const float at[3] = {pos[0], pos[1], pos[2]};
            memcpy(gPlayers[kDoll].pos, at, sizeof at);
            int reads = 0;
            bool back = false;
            for (int t = 0; t < 60 && !back; ++t) {
                const float gap = std::hypot(pos[0] - at[0], pos[2] - at[2]);
                const Host f = HostFrame(s, pos, true);
                if (f.read && gap >= 1200.f) ++reads;
                if (gap >= 1200.f && std::hypot(pos[0] - at[0], pos[2] - at[2]) < 10.f) back = true;
            }
            if (back) counts.push_back(reads);
            for (int t = 0; t < 90; ++t) HostFrame(s, pos);   // (on its ways again, its copy with it)
        }
        vdoll::body::Down(0);
        gFrame += 120;
    }
    std::string l;
    for (int n : counts) l += " " + std::to_string(n);
    printf("    %zu holds of its copy: taken back to it after these reads with its copy 1.2 m or more behind:%s\n", counts.size(), l.c_str());
    // (a hold that begins with its copy's step since HeldBack's last look 300 mm or more, its last look before a drop or a climb, is no
    // hold but a copy moving behind: 3 more reads, so 6 (beta 25: 4); n022a has one)
    int three = 0, fewer = 0;
    for (int n : counts) { if (n == 3) ++three; if (n < 3) ++fewer; }
    const bool ok = counts.size() >= 2 && (gOld ? fewer * 2 >= (int)counts.size() : fewer == 0 && three * 2 >= (int)counts.size());
    Check(ok, gOld ? "held (MGO1_VDOLL_ONE_PASS=0, beta 25): back after 1 or 2 reads in most holds (both passes of a read's frame counted)"
                   : "held: never back before 3 reads of the players (300 ms), 3 in most holds, as HeldBack's count says");
}
static void PartTurn() {
    const auto pts = Spawns();
    if (pts.empty()) { Check(false, "turn: no spawn point"); return; }
    float most = 0.f, mostFight = 0.f;
    long long frames = 0, fightFrames = 0;
    {                                                     // (60 s on its ways)
        float feet[3];
        if (!gNav->Nearest(pts[0].data(), 1500.f, feet)) { Check(false, "turn: no mesh at the spawn point"); return; }
        for (int k = 1; k < 16; ++k) gHave[k] = false;
        float pos[3] = {feet[0], feet[1] + 800.f, feet[2]};
        uint16_t facing = 0;
        vdoll::body::Self s = NewSelf(pos, &facing);
        Player(kDoll, 1, feet);
        for (int t = 0; t < 1800; ++t) {
            const Host f = HostFrame(s, pos);
            if (t < 2) continue;                          // (its first pass: its facing set)
            most = std::max(most, std::fabs(Wrap(f.yaw1 - f.yaw0)) * 57.29578f); ++frames;
        }
        vdoll::body::Down(0);
        gFrame += 120;
    }
    int cases = 0;
    for (size_t i = 0; i < pts.size() && cases < 12; ++i) {   // (an enemy 5 m behind it: its fight turns it round)
        float dollFeet[3], foe[3];
        if (!PickPlaces(pts[i].data(), 5000.f, false, dollFeet, foe)) continue;
        ++cases;
        for (int k = 1; k < 16; ++k) gHave[k] = false;
        Player(kFoe, 0, foe);
        float pos[3] = {dollFeet[0], dollFeet[1] + 800.f, dollFeet[2]};
        uint16_t facing = Facing(Wrap(YawTo(pos, foe) + 3.14159265f));
        vdoll::body::Self s = NewSelf(pos, &facing);
        Player(kDoll, 1, dollFeet);
        for (int t = 0; t < 90; ++t) {
            const Host f = HostFrame(s, pos);
            if (t < 2) continue;
            mostFight = std::max(mostFight, std::fabs(Wrap(f.yaw1 - f.yaw0)) * 57.29578f); ++fightFrames;
        }
        vdoll::body::Down(0);
        gFrame += 120;
    }
    printf("    the most it turned in a host frame of two frames: %.1f degrees on its ways (%lld frames), %.1f in %d fights turning round (%lld frames)\n",
           most, frames, mostFight, cases, fightFrames);
    if (gOld) Check(std::max(most, mostFight) > 33.f, "turn (MGO1_VDOLL_ONE_PASS=0, beta 25): more than 16 degrees a frame (a frame's turn in the pass with no frame since)");
    else Check(cases >= 1 && most <= 32.5f && mostFight <= 32.5f && std::max(most, mostFight) > 20.f, "turn: never more than 16 degrees a frame (32 in a host frame of two)");
}
// the cases of a fight at d mm (one hit each: its life 1 less, by him, 60 host frames in); rolled: those whose roll came after the hit
static int Fights(float d, bool rollRoom, int want, int hitAt, int* rolled) {
    const auto pts = Spawns();
    int cases = 0;
    *rolled = 0;
    for (int round = 0; cases < want && round < 40; ++round)
        for (size_t i = 0; i < pts.size() && cases < want; ++i) {
            float dollFeet[3], foe[3];
            if (!PickPlaces(pts[i].data(), d, rollRoom, dollFeet, foe)) continue;
            ++cases;
            for (int k = 1; k < 16; ++k) gHave[k] = false;
            Player(kFoe, 0, foe);
            float pos[3] = {dollFeet[0], dollFeet[1] + 800.f, dollFeet[2]};
            uint16_t facing = Facing(YawTo(pos, foe));
            vdoll::body::Self s = NewSelf(pos, &facing);
            Player(kDoll, 1, dollFeet);
            bool roll = false;
            for (int t = 0; t < 90; ++t) {
                if (hitAt >= 0 && t == hitAt) { s.hp = 199; s.attacker = kFoe; }
                gRollRec = false;
                HostFrame(s, pos);
                if (gRollRec && hitAt >= 0 && t >= hitAt) roll = true;
            }
            if (roll) ++*rolled;
            vdoll::body::Down(0);
            gFrame += 600;                                // (its next roll's wait over: 240 to 480 frames after one)
        }
    return cases;
}
static void PartRoll() {
    int rolled = 0;
    const int cases = Fights(6000.f, true, 150, 60, &rolled);
    const double share = cases ? (double)rolled / cases : 0.0;
    printf("    %d cases at 6 m, one hit each: a roll after the hit in %d (%.0f %%; one look a read: 1 - 0.65^2 = 58 %%, two: 1 - 0.65^4 = 82 %%)\n", cases, rolled, share * 100.0);
    if (gOld) Check(cases >= 75 && share > 0.70, "roll (MGO1_VDOLL_ONE_PASS=0, beta 25): its roll's chance drawn in both passes of a read's frame (more than 70 %%)");
    else Check(cases >= 75 && share >= 0.45 && share <= 0.70, "roll: its roll when hurt drawn once a read (45 to 70 %% of the cases)");
}
static void PartReads() {
#ifdef VDOLL_BODY_DPASS
    vdoll::body::DecInfo a, m, b;
    vdoll::body::Decisions(0, &a);
    int rolled = 0;
    const int c1 = Fights(6000.f, true, 40, 60, &rolled);
    vdoll::body::Decisions(0, &m);
    const int c2 = Fights(1800.f, false, 40, -1, &rolled);
    vdoll::body::Decisions(0, &b);
    // (each scenario's own: what its looks began a minute of its fights, 90 host frames of two a case)
    for (int sc = 0; sc < 2; ++sc) {
        const vdoll::body::DecInfo& x = sc ? m : a;
        const vdoll::body::DecInfo& y = sc ? b : m;
        const double mins = (sc ? c2 : c1) * 180.0 / 59.94 / 60.0;
        if (mins <= 0) continue;
        printf("    %s: %.1f minutes of fight: reads %lld, decisions %lld; looks a read: roll %.3f, grab %.3f, roll into %.3f, rush %.3f; a minute: rolls %.1f, rolls into %.1f, "
               "grabs begun %.1f, rushes %.1f\n", sc ? "at 1.8 m, no hit" : "at 6 m, one hit a case", mins, y.reads - x.reads, y.blocks - x.blocks,
               (double)(y.rollLooks - x.rollLooks) / std::max(1LL, y.reads - x.reads), (double)(y.grabLooks - x.grabLooks) / std::max(1LL, y.reads - x.reads),
               (double)(y.intoLooks - x.intoLooks) / std::max(1LL, y.reads - x.reads), (double)(y.rushLooks - x.rushLooks) / std::max(1LL, y.reads - x.reads),
               (y.rolls - x.rolls) / mins, (y.intos - x.intos) / mins, (y.grabsBegun - x.grabsBegun) / mins, (y.rushes - x.rushes) / mins);
    }
    const long long reads = b.reads - a.reads, again = b.readsAgain - a.readsAgain, blocks = b.blocks - a.blocks, blocks0 = b.blocks0 - a.blocks0;
    const long long looks[4] = {b.rollLooks - a.rollLooks, b.grabLooks - a.grabLooks, b.intoLooks - a.intoLooks, b.rushLooks - a.rushLooks};
    printf("    %d cases at 6 m, %d at 1.8 m: %lld passes (%lld with no frame since the last), %lld reads of the players (%lld more passes in a read's frame); its decisions "
           "%lld (%lld in a pass with no frame since the last), HeldBack %lld; looks: roll %lld, grab %lld, roll into %lld, rush %lld; grenade looks %lld (%lld past its pace); "
           "turned %.0f degrees (%.0f in passes with no frame since the last)\n",
           c1, c2, b.passes - a.passes, b.pass0 - a.pass0, reads, again, blocks, blocks0, b.held - a.held, looks[0], looks[1], looks[2], looks[3],
           b.throwLooks - a.throwLooks, b.throwTries - a.throwTries, b.turn - a.turn, b.turn0 - a.turn0);
    bool once = true;
    for (long long n : looks) if (n > reads) once = false;
    if (gOld) Check(reads > 1000 && again * 10 >= reads * 9 && blocks0 * 10 >= reads * 9 && blocks >= reads + blocks0 - 20,
                    "reads (MGO1_VDOLL_ONE_PASS=0, beta 25): its decisions in both passes of each read's frame");
    else Check(reads > 1000 && again * 10 >= reads * 9 && blocks0 == 0 && blocks <= reads && blocks * 10 >= reads * 9 && once && b.turn0 - a.turn0 < 0.01,
               "reads: its decisions once a read, none in a pass with no frame since the last, no chance drawn more often than the reads, no turn there");
#else
    printf("    this body has no Decisions (vdoll_body.h, dpass): skipped\n");
#endif
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
    if (part == "held") PartHeld(); else if (part == "turn") PartTurn(); else if (part == "roll") PartRoll(); else PartReads();
    return gFails;
}
int main(int argc, char** argv) {
    std::vector<std::string> maps, parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "held" || a == "turn" || a == "roll" || a == "reads") parts.push_back(a); else maps.push_back(a);
    }
    if (maps.empty()) { fprintf(stderr, "usage: vdoll_dpass_test <map.geom> ... [held|turn|roll|reads ...]\n"); return 2; }
    if (parts.empty()) parts = {"held", "turn", "roll", "reads"};
    gOld = getenv("MGO1_VDOLL_ONE_PASS") && !strcmp(getenv("MGO1_VDOLL_ONE_PASS"), "0");
    printf("level %s, MGO1_VDOLL_ONE_PASS=%s\n", getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10", gOld ? "0 (beta 25's expected)" : "(on)");
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
