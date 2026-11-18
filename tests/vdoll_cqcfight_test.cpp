// (v150-host beta 24, cqcfight) A virtual doll against an enemy who fights its teammate in close combat (docs/VIRTUAL_DOLL.md 42):
// a player's report on beta 23, "when I was going full CQC the bots stopped attacking until one KO'd another". Off line, on a map's
// .geom, each case in a process of its own (the bodies start afresh), a frame at a time: the doll (blue, AK-47) stands D m from an
// enemy player (red, standing still: the CQC attacker); its teammate (a blue player) is where a CQC fight leaves him:
//   hold    standing 0.45 m from the attacker, held by him (his hold: the teammate in front of him), at an angle round him from the
//           doll's side (0: between them, the attacker's shield) to behind him (180)
//   thrown  lying 0.7 m from the attacker's hips, thrown down or knocked out by him, at the same angles
//   punch   standing 0.6 m from the attacker (his punches), at the same angles
//   open    no teammate near him (the control)
// Each case 8 s. What is counted: the frames from the doll's sight of him to its first attack that reaches him (a damage or stun
// record on his channel 0: a bullet, a punch, a throw's stun; its hold's record or its roll's fall on his channel 1), its records on the teammate's channels (none may come: no blow and
// no bullet on a teammate), its going to wake the teammate while the attacker stands in its sight (never), its giving him up
// ("leaves him 5 s"). A case fails when the doll sends a record to its teammate, goes to wake him with the attacker there, or does
// not reach the attacker in time: 1.5 s with no teammate, with the teammate lying (a player fires over him) and with him beside or
// behind the attacker (90 to 180 degrees; at 3 m its CQC grab may come first: its hold's record 60 to 80 frames on); 4 s at 45 degrees (it may have to move); 6 s at 0 degrees (the teammate between them: it must go
// round him first); each limit and twice its level's own reaction and settling (none at level 10: 2 frames each way).
// Every map given, every part (or the parts named after the maps). MGO1_VDOLL_CONTROLS and MGO1_VDOLL_LEVEL as on the host (10
// when unset); MGO1_VDOLL_CQCFIGHT=0 gives beta 23's rules back (the "before").
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_cqcfight_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_cqcfight_test
//   ./vdoll_cqcfight_test <map.geom> [<map.geom> ...] [hold|thrown|punch|open|crouch|fence ...]   (VDOLL_TEST_VERBOSE=1: the body's lines)
// ((v150-host beta 24, cqcfight2) fence: the attacker and its held teammate beyond a fence the doll sees over; PartFence below)
// ((v150-host beta 24, review) crouch: as a player only, a teammate crouched before the doll under its eye's line and on its gun's:
// no third person bullet through him; PartCrouch below. 7b015f6 fails it on the 12 maps, 33 cases)
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
static int gLeaves = 0, gWakeLines = 0, gStallLines = 0;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, "leaves him")) ++gLeaves;
    if (strstr(b, "with no shot")) ++gStallLines;
    if (strstr(b, "goes to wake")) ++gWakeLines;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("      %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }                          // (team deathmatch)
static int gFF = 1;
static int FriendlyFire() { return gFF; }
static long long gFrame = 600;
static const int kDoll = 2, kMate = 3, kFoe = 5;         // (the seats: the doll, its teammate, the attacker)
// the records the doll sends: to whom, the channel's kind, the record, the frame
struct Rec { int slot, kind; std::vector<uint8_t> r; long long at; };
static std::vector<Rec> gRecs;
static FILE* gDump = nullptr;                            // (VDOLL_TEST_DUMP=<file>: every record, a line each, appended: tests/vdoll_cqcfight_same.sh)
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) {
    gRecs.push_back({slot, kind, r, gFrame});
    if (gDump) { fprintf(gDump, "%lld %d %d ", gFrame, slot, kind); for (uint8_t b : r) fprintf(gDump, "%02x", b); fputc('\n', gDump); }
    if (getenv("VDOLL_TEST_EVENTS")) { printf("      f%lld: to %d kind %d:", gFrame, slot, kind); for (uint8_t c : r) printf(" %02x", c); printf("\n"); }
}
static unsigned gRnd = 0x2545F491u;
static unsigned Rnd() { gRnd ^= gRnd << 13; gRnd ^= gRnd >> 17; gRnd ^= gRnd << 5; return gRnd; }

static doll::NavMesh* gNav = nullptr;
static doll::Geom gG;
static int gFails = 0;
static void Fail(const char* fmt, ...) {
    char b[600];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("  FAIL %s\n", b);
    ++gFails;
}
static bool StartBody() {
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
#ifdef VDOLL_HOST_FF
    h.friendlyFire = FriendlyFire;
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    // (the mesh waited for at one frame: its build's time on the machine no longer moves the bodies' reads of the players, every 6
    // frames, against the case's frames; two runs give the same records: tests/vdoll_cqcfight_same.sh)
    for (int w = 0; w < 12000 && !gMeshSaid; ++w) { vdoll::body::Frame(0); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return gMeshSaid;
}
static uint16_t Facing(const float* from, const float* to) {
    const float yaw = std::atan2(to[0] - from[0], to[2] - from[2]);
    return (uint16_t)((long)lrintf(yaw / 6.2831853f * 65536.f) & 0xFFFF);
}
static std::vector<std::array<float, 3>> Spawns() {
    std::vector<std::array<float, 3>> pts;
    for (const doll::Spawn& sp : gG.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) pts.push_back({sp.p[0], sp.p[1], sp.p[2]});
    return pts;
}
// the mesh's floor within 300 mm of a point (feet), its height: false none
static bool OnMesh(const float* p, float* out) { float f[3]; if (!gNav->Nearest(p, 400.f, f) || std::hypot(f[0] - p[0], f[2] - p[2]) > 300.f || std::fabs(f[1] - p[1]) > 400.f) return false; memcpy(out, f, sizeof f); return true; }
// a place for the attacker (feet) near a spawn point with the floor round him flat and open (the mesh 1.2 m round him), and the
// doll's place (feet) d mm from him with a clear line from its eye to his chest and head and a way to him (2 d + 4 m at most)
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
        if (!OnMesh(q, m)) continue;
        const float eye[3] = {m[0], m[1] + 800.f + 750.f, m[2]}, chest[3] = {p[0], p[1] + 800.f + 350.f, p[2]}, head[3] = {p[0], p[1] + 800.f + 850.f, p[2]};
        if (!gNav->Clear(eye, chest) || !gNav->Clear(eye, head)) continue;
        float len = 0, end[3];                                             // (a way the bodies run from the doll to him, as in a close fight)
        if (!vdoll::body::WayTo(m, p, &len, end) || len > 2.f * d + 4000.f || std::hypot(end[0] - p[0], end[2] - p[2]) > 500.f) continue;
        memcpy(foe, p, sizeof p); memcpy(doll, m, sizeof m);
        return true;
    }
    return false;
}
static bool gMateCrouch = false;                         // ((v150-host beta 24, review) the part "crouch": the teammate crouched)
struct Bullet { long long frame; bool view; float pos[3], yaw; };
static std::vector<Bullet> gBullets;                     // (each bullet of a case: its frame, from the view or not, the doll's hips and facing)
static void Player(int id, int team, const float* feet, bool lying, bool out) {
    gHave[id] = true;
    vdoll::PlayerNow& p = gPlayers[id];
    p = vdoll::PlayerNow(); p.team = team; p.object = p.alive = true; p.life = 200; p.cls = 1;
    p.pos[0] = feet[0]; p.pos[1] = feet[1] + (lying ? 300.f : 800.f); p.pos[2] = feet[2];
    p.state = lying ? 8u : gMateCrouch && id == kMate ? 4u : 0u; p.asleep = out;
#ifdef VDOLL_PLAYERNOW_WAKE
    p.stunMax = 30000; p.wakePart = 50; p.stamina = out ? 0 : 30000;
#endif
}
// one case: the doll's frames to its first record that reaches the attacker (-1 none), its records to the teammate, its wake
struct Result { long long first = -1, leftAt = -1; int toMate = 0, toFoe = 0, shots = 0, grabs = 0, rolls = 0, punches = 0; bool wakes = false; float ran = 0; };
static Result Play(const float* dollFeet, const float* foeFeet, const float* mateFeet, bool mateLying, bool mateOut, int frames) {
    for (int i = 1; i < 16; ++i) gHave[i] = false;
    gRecs.clear(); gBullets.clear(); gLeaves = gWakeLines = gStallLines = 0;
    Player(kFoe, 0, foeFeet, false, false);
    if (mateFeet) Player(kMate, 1, mateFeet, mateLying, mateOut);
    float pos[3] = {dollFeet[0], dollFeet[1] + 800.f, dollFeet[2]};
    uint16_t facing = Facing(pos, foeFeet);
    vdoll::body::Self s;
    s.k = 0; s.slot = kDoll; s.team = 1; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
    Player(kDoll, 1, dollFeet, false, false);
    Result r;
    vdoll::body::AimInfo was;
    vdoll::body::Aim(0, &was);
    const long long t0 = gFrame;
    for (int t = 0; t < frames; ++t, ++gFrame) {
        if (gFrame % 6 == 0) memcpy(gPlayers[kDoll].pos, pos, sizeof pos);   // (the host's copy of the doll, read every 100 ms)
        const float p0[3] = {pos[0], pos[1], pos[2]};
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
        r.ran += std::hypot(pos[0] - p0[0], pos[2] - p0[2]);
        vdoll::body::AimInfo now;
        vdoll::body::Aim(0, &now);
        r.shots += now.shots - was.shots;
        if (was.target == kFoe && now.target != kFoe && r.leftAt < 0) r.leftAt = gFrame - t0;   // ((cqcfight2) it gave him up)
        if (now.lastBullet >= 0 && now.lastBullet != was.lastBullet) gBullets.push_back({gFrame, now.state == 4, {pos[0], pos[1], pos[2]}, now.yaw});
        was = now;
#ifdef VDOLL_BODY_WAKE
        vdoll::body::WakeInfo w;
        if (mateFeet && vdoll::body::Wake(0, &w) && w.to == kMate) r.wakes = true;
#endif
    }
    for (const Rec& c : gRecs) {
        if (c.slot == kMate && c.kind <= 2) ++r.toMate;
        if (c.slot == kFoe && c.kind == 0 && !c.r.empty() && c.r[0] == 0x04) { ++r.toFoe; if (r.first < 0) r.first = c.at - t0; if (c.r.size() >= 7 && c.r[6] == 0x97) ++r.punches; }
        if (c.slot == kFoe && c.kind == 1 && !c.r.empty() && c.r[0] == 0x0D) { ++r.grabs; if (r.first < 0 || c.at - t0 < r.first) r.first = c.at - t0; }   // (its hold takes him)
        if (c.slot == kFoe && c.kind == 1 && c.r.size() >= 3 && c.r[0] == 0x09 && c.r[2] == 0x09) { ++r.rolls; if (r.first < 0 || c.at - t0 < r.first) r.first = c.at - t0; }   // (its roll's fall)
    }
    vdoll::body::Down(0);
    gFrame += 60;
    return r;
}
static void Say(const char* what, const Result& r) {
    printf("    %s: first reach %s, %d records on him (%d shots fired, %d grabs, %d rolls, %d punches), %d to the teammate, %s, %.1f m run, stall lines %d, left him %d\n", what,
           r.first < 0 ? "NEVER" : (std::to_string(r.first) + " frames").c_str(), r.toFoe, r.shots, r.grabs, r.rolls, r.punches, r.toMate, r.wakes ? "WENT TO WAKE the teammate" : "no wake",
           r.ran / 1000.f, gStallLines, gLeaves);
}
// its level's reaction and settling (vdoll_body.cpp kLevel), frames: the limits grow by twice their sum above level 10's
static long long LevelMore() {
    static const int kReactSettle[10] = {114, 90, 71, 54, 41, 34, 27, 18, 10, 5};
    const int lv = getenv("MGO1_VDOLL_LEVEL") ? std::max(1, std::min(10, atoi(getenv("MGO1_VDOLL_LEVEL")))) : 10;
    return 2 * (kReactSettle[lv - 1] - kReactSettle[9]);
}
static void Judge(const char* what, const Result& r, long long within) {
    within += LevelMore();
    Say(what, r);
    if (r.first < 0 || r.first > within) Fail("%s: the attacker not reached within %.1f s (%s)", what, within / 60.f, r.first < 0 ? "never" : (std::to_string(r.first) + " frames").c_str());
    if (r.toMate) Fail("%s: %d records on its teammate", what, r.toMate);
    if (r.wakes) Fail("%s: went to wake its teammate with the attacker in its sight", what);
}
static const float kAngles[] = {0.f, 45.f, 90.f, 135.f, 180.f};
// the teammate's place (feet) round the attacker, at gap mm, the angle from the doll's side; false: off the mesh
static bool MatePlace(const float* foe, const float* doll, float angle, float gap, float* out) {
    const float yaw = std::atan2(doll[0] - foe[0], doll[2] - foe[2]) + angle * 3.14159265f / 180.f;
    const float q[3] = {foe[0] + std::sin(yaw) * gap, foe[1], foe[2] + std::cos(yaw) * gap};
    if (!OnMesh(q, out)) { memcpy(out, q, sizeof q); out[1] = foe[1]; }
    return true;
}
static void PartClose(const char* part) {
    const bool lying = !strcmp(part, "thrown"), open = !strcmp(part, "open");
    const float gap = lying ? 700.f : !strcmp(part, "punch") ? 600.f : 450.f;
    int n = 0;
    for (const auto& sp : Spawns()) {
        if (n >= 3) break;
        for (float d : {3000.f, 7000.f}) {
            float foe[3], doll[3];
            if (!PickPlaces(sp.data(), d, foe, doll)) continue;
            if (open) {
                char what[96]; snprintf(what, sizeof what, "open %d (%.0f m)", n + 1, d / 1000.f);
                Judge(what, Play(doll, foe, nullptr, false, false, 480), 90);
                continue;
            }
            for (float a : kAngles) {
                float mate[3];
                MatePlace(foe, doll, a, gap, mate);
                for (int out = 0; out < (lying ? 2 : 1); ++out) {
                    char what[128]; snprintf(what, sizeof what, "%s %d (%.0f m, the teammate at %.0f degrees%s)", part, n + 1, d / 1000.f, a, lying ? (out ? ", knocked out" : ", awake") : "");
                    Judge(what, Play(doll, foe, mate, lying, lying && out, 480), lying || a >= 90.f ? 90 : a >= 45.f ? 240 : 360);
                }
            }
        }
        ++n;
    }
    if (!n) Fail("%s: no place found", part);
}

// ((v150-host beta 24, review) "crouch": the doll as a player (MGO1_VDOLL_CONTROLS=player) 7 m from the attacker, its teammate
// crouched 1.5 m before it on its line to him, the floor flat: its eye's line to his chest passes over the teammate's head, its
// gun's line (a third person bullet leaves the muzzle, 0.45 m ahead of its hips and 0.38 m over them: MGO1_VDOLL_MUZZLE) meets
// it. A doll's bullet that meets a teammate's body stops there (MGO1_VDOLL_CQCFIGHT, AllyStops); a player raises his view to
// shoot over him. Fails: a damage record on the attacker from a third person bullet whose gun's line to his chest met the
// teammate's head or body (beta 24's 7b015f6 tested the teammate on the eye's line only); the attacker not reached within 6 s;
// a record on the teammate. Direct controls: no muzzle, the part says so and checks nothing.
static float SegGap(const float* a, const float* b, const float* p0, const float* p1) {   // (the two segments' least distance, sampled)
    float best = 1e30f;
    for (int i = 0; i <= 200; ++i) {
        const float u = i / 200.f, q[3] = {a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u};
        const float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        float v = dd > 0.f ? ((q[0] - p0[0]) * d[0] + (q[1] - p0[1]) * d[1] + (q[2] - p0[2]) * d[2]) / dd : 0.f;
        v = std::max(0.f, std::min(1.f, v));
        best = std::min(best, std::hypot(q[0] - p0[0] - d[0] * v, std::hypot(q[1] - p0[1] - d[1] * v, q[2] - p0[2] - d[2] * v)));
    }
    return best;
}
// a crouched player's head and body as vdoll_body.cpp HitPart takes them (over his hips' point: the head 310, radius 120; the body
// -217 to -120, radius 220); the hips 800 over his feet
static bool MeetsCrouched(const float* a, const float* b, const float* feet, float grow) {
    const float h0[3] = {feet[0], feet[1] + 800.f + 310.f, feet[2]}, b0[3] = {feet[0], feet[1] + 800.f - 217.f, feet[2]}, b1[3] = {feet[0], feet[1] + 800.f - 120.f, feet[2]};
    return SegGap(a, b, h0, h0) < 120.f + grow || SegGap(a, b, b0, b1) < 220.f + grow;
}
static void PartCrouch() {
    const char* c = getenv("MGO1_VDOLL_CONTROLS");
    if (!c || strcmp(c, "player")) { printf("    crouch: the direct controls fire from the eye (no muzzle): nothing to check\n"); return; }
    int n = 0, cases = 0;
    for (const auto& sp : Spawns()) {
        if (n >= 3) break;
        ++n;
        float foe[3], doll[3], mate[3];
        bool placed = false;
        for (int tries = 0; tries < 8 && !placed; ++tries) {
            if (!PickPlaces(sp.data(), 7000.f, foe, doll)) break;
            const float dx = foe[0] - doll[0], dz = foe[2] - doll[2], len = std::hypot(dx, dz);
            const float q[3] = {doll[0] + dx / len * 1500.f, doll[1], doll[2] + dz / len * 1500.f};
            if (!OnMesh(q, mate) || std::fabs(mate[1] - doll[1]) > 60.f || std::fabs(foe[1] - doll[1]) > 60.f) continue;
            const float eye[3] = {doll[0], doll[1] + 800.f + 750.f, doll[2]}, chest[3] = {foe[0], foe[1] + 800.f + 350.f, foe[2]};
            const float m[3] = {doll[0] + dx / len * 450.f, doll[1] + 800.f + 380.f, doll[2] + dz / len * 450.f};
            placed = !MeetsCrouched(eye, chest, mate, 150.f) && MeetsCrouched(m, chest, mate, -30.f);   // (the eye's line well over him, the gun's well into him)
        }
        if (!placed) continue;
        ++cases;
        gMateCrouch = true;
        const Result r = Play(doll, foe, mate, false, false, 480);
        gMateCrouch = false;
        const float chest[3] = {foe[0], foe[1] + 800.f + 350.f, foe[2]};
        int through = 0, third = 0, view = 0;
        for (const Bullet& u : gBullets) (u.view ? view : third)++;
        for (const Rec& x : gRecs) {
            if (x.slot != kFoe || x.kind != 0 || x.r.empty() || x.r[0] != 0x04) continue;
            const Bullet* last = nullptr;
            for (const Bullet& u : gBullets) if (u.frame <= x.at) last = &u;
            if (!last || last->view) continue;
            const float m[3] = {last->pos[0] + std::sin(last->yaw) * 450.f, last->pos[1] + 380.f, last->pos[2] + std::cos(last->yaw) * 450.f};
            if (MeetsCrouched(m, chest, mate, 0.f)) ++through;
        }
        char what[96]; snprintf(what, sizeof what, "crouch %d (7 m, the teammate crouched 1.5 m before it)", n);
        Say(what, r);
        printf("      bullets: %d from the third person, %d from its view; damage records from a third person bullet whose gun's line met the teammate: %d\n", third, view, through);
        if (through) Fail("%s: %d damage records on the attacker from third person bullets through its crouched teammate", what, through);
        if (r.first < 0 || r.first > 360 + LevelMore()) Fail("%s: the attacker not reached within %.1f s (%s)", what, (360 + LevelMore()) / 60.f, r.first < 0 ? "never" : (std::to_string(r.first) + " frames").c_str());
        if (r.toMate) Fail("%s: %d records on its teammate", what, r.toMate);
    }
    printf("    crouch: %d cases\n", cases);
}

// ((v150-host beta 24, cqcfight2) "fence": the attacker 2.5 m beyond a fence a player climbs over (the mesh's vault links), its
// teammate held between them (0.45 m before him), the doll 4 or 7 m on its own side, its eye's line to his chest and head clear over the
// fence. A way in a fight crosses no fence (snakehouse2), its steps aside see him through its teammate, so b16 06932d0's guard "stays
// on him" for ever, its fire held (review24 F2: "the bots stopped attacking"). It fails when the doll neither reaches him within 8 s
// (over the fence and round him) nor gives him up within 6 s (each case 10 s, both ways over each fence, the doll 4 and 7 m from it, 6
// fences a map at most), or sends its teammate a record
static void PartFence() {
    int fences = 0, cases = 0;
    for (int i = 0; i < gNav->LinkCount() && fences < 6; ++i) {
        float a[3], b[3];
        uint8_t area = 0;
        if (!gNav->LinkAt(i, a, b, &area) || area != doll::AREA_VAULT || std::fabs(a[1] - b[1]) > 300.f) continue;
        bool any = false;
        for (int dir = 0; dir < 4; ++dir) {
            const float* n = dir & 1 ? b : a; const float* f = dir & 1 ? a : b;   // (its side, his side)
            const float back = dir < 2 ? 4000.f : 7000.f;                         // (the doll 4 or 7 m from the fence)
            const float dx = f[0] - n[0], dz = f[2] - n[2], len = std::hypot(dx, dz);
            if (len < 100.f) continue;
            const float ux = dx / len, uz = dz / len;
            const float qf[3] = {f[0] + ux * 2500.f, f[1], f[2] + uz * 2500.f}, qn[3] = {n[0] - ux * back, n[1], n[2] - uz * back};
            float foe[3], doll[3], mate[3];
            if (!OnMesh(qf, foe) || !OnMesh(qn, doll) || std::fabs(foe[1] - f[1]) > 400.f || std::fabs(doll[1] - n[1]) > 400.f) continue;
            const float qm[3] = {foe[0] - ux * 450.f, foe[1], foe[2] - uz * 450.f};
            if (!OnMesh(qm, mate)) { memcpy(mate, qm, sizeof qm); }
            const float eye[3] = {doll[0], doll[1] + 800.f + 750.f, doll[2]}, chest[3] = {foe[0], foe[1] + 800.f + 350.f, foe[2]}, head[3] = {foe[0], foe[1] + 800.f + 850.f, foe[2]};
            if (!gNav->Clear(eye, chest) || !gNav->Clear(eye, head)) continue;
            any = true; ++cases;
            const Result r = Play(doll, foe, mate, false, false, 600);
            char what[128]; snprintf(what, sizeof what, "fence %d way %d (%.1f m over a fence, the teammate held between them)", fences + 1, (dir & 1) + 1, std::hypot(foe[0] - doll[0], foe[2] - doll[2]) / 1000.f);
            Say(what, r);
            printf("      gave him up %s\n", r.leftAt < 0 ? "never" : (std::to_string(r.leftAt) + " frames after its sight").c_str());
            const long long reachBy = 480 + LevelMore(), leaveBy = 360 + LevelMore();   // (over the fence and round him: 8 s; given up: 6 s)
            if (!(r.first >= 0 && r.first <= reachBy) && !(r.leftAt >= 0 && r.leftAt <= leaveBy))
                Fail("%s: neither reached within %.1f s nor given up within %.1f s: frozen (first reach %s)", what, reachBy / 60.f, leaveBy / 60.f, r.first < 0 ? "never" : (std::to_string(r.first) + " frames").c_str());
            if (r.toMate) Fail("%s: %d records on its teammate", what, r.toMate);
        }
        if (any) ++fences;
    }
    printf("    fence: %d fences, %d cases\n", fences, cases);
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
    if (!getenv("MGO1_VDOLL_WEAPONS")) setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    gRnd ^= (unsigned)gGeom.size();
    if (!StartBody()) { printf("  FAIL the bodies' mesh was not built\n"); return 1; }
    if (const char* d = getenv("VDOLL_TEST_DUMP")) { gDump = fopen(d, "a"); if (gDump) fprintf(gDump, "# %s %s\n", geomPath, part.c_str()); }
    if (part == "crouch") PartCrouch(); else if (part == "fence") PartFence(); else PartClose(part.c_str());
    if (gDump) fclose(gDump);
    return gFails;
}

int main(int argc, char** argv) {
    std::vector<std::string> maps, parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "hold" || a == "thrown" || a == "punch" || a == "open" || a == "crouch" || a == "fence") parts.push_back(a); else maps.push_back(a);
    }
    if (maps.empty()) { fprintf(stderr, "usage: vdoll_cqcfight_test <map.geom> ... [hold|thrown|punch|open|crouch|fence ...]\n"); return 2; }
    if (parts.empty()) parts = {"open", "hold", "thrown", "punch", "crouch", "fence"};
    printf("controls %s, level %s, MGO1_VDOLL_CQCFIGHT=%s\n", getenv("MGO1_VDOLL_CONTROLS") ? getenv("MGO1_VDOLL_CONTROLS") : "direct",
           getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10", getenv("MGO1_VDOLL_CQCFIGHT") ? getenv("MGO1_VDOLL_CQCFIGHT") : "(on)");
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
