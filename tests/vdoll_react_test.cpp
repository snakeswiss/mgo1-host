// (v150-host beta 25, reactfix) The reaction a virtual doll's hit tells its victim ({09 shooter, kind, angle} on his channel 1), as a
// shooter's game picks it on its copy of him (US n003a 0x437F20 from 0x438960; MGO1Loader/vdoll_react.h, vdoll_body.cpp FireFlame,
// Shoot, WakeBlow, Roll, Blast, RocketBlast, ClayBlast; docs/VIRTUAL_DOLL.md 58). Off line, on a map's .geom, each part in a process of
// its own, run a frame at a time; the expected kind and angle worked out here again from the game's code, not from the header:
//   flame       the flamethrower (MGO1_VDOLL_WEAPONS=ak47+flamethrower) at an enemy standing 2 to 3.5 m away, facing the doll, his
//               back to it, and side on: every contact's reaction 1 when the vector of its effect record {14 .. 04 02 ..} runs against
//               his facing (his yaw's unit vector), else 2; its angle his yaw (1) or his yaw + 0x8000 (2); the fire that touches him
//               lies before him and past him along the stream (a quarter of the contacts each way at least, as measured on real
//               clients), not at the stream's nearest point
//   flamelying  the same at an enemy on the ground: lying on his front (the state bit 3) 3, down after a roll (0x1C) 4, down after
//               a blast (0x1B) 5, the angle the yaw he turns to (+0x14A)
//   m37         the M37 (MGO1_VDOLL_WEAPONS=m37) at an enemy standing 2 to 14 m away, facing the doll or his back to it: each hit's
//               power 250 a pellet that reached him (its damage record's 25 a pellet): 4 pellets 6 from his front, 7 from behind; 3
//               pellets 1 or 2; 2 or fewer no reaction; the angle the pellets' way less 0x8000 (+ 0x8000 more for 7 and 2)
//   m37lying    the M37 at an enemy lying on his front: 4 pellets 8 (room over him) or 3, 3 pellets 3 (the angle his +0x14A), fewer none
// b16's body fails flame (its angles a quarter turn from the way to the doll), flamelying (1 or 2 to a man on the ground), m37 and
// m37lying (no reaction at all); so does MGO1_VDOLL_REACT_KIND=0.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_react_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_react_test
//   rules       (no map) the rule's table: the burning hit, the M37 by power, a blast and a roll on a man standing and on the ground
//               with and without room over him, the punches and the kick
//   ./vdoll_react_test <map.geom> [<map.geom> ...] [rules|flame|flamelying|m37|m37lying ...]
#include "doll_geom.h"
#include "doll_nav.h"
#include "vdoll_body.h"
#if __has_include("vdoll_react.h")
#include "vdoll_react.h"
#endif
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
    if (getenv("VDOLL_TEST_VERBOSE")) printf("    %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }                          // (team deathmatch)
struct Rec { long long frame; int slot, kind; std::vector<uint8_t> r; };
static std::vector<Rec> gRecs;
static long long gFrame = 600;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gRecs.push_back({gFrame, slot, kind, rec}); }
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
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return gMeshSaid;
}
static bool PickPair(const float* spawn, float lo, float hi, float* me, float* him) {
    float feet[3];
    if (!gNav->Nearest(spawn, 1500.f, feet)) return false;
    for (int tries = 0; tries < 600; ++tries) {
        float p[3];
        if (!gNav->RandomPoint(feet, hi + 5000.f, p, Rnd)) continue;
        const float flat = std::hypot(p[0] - feet[0], p[2] - feet[2]);
        if (flat < lo || flat > hi || std::fabs(p[1] - feet[1]) > 1000.f) continue;
        const float eye[3] = {feet[0], feet[1] + 1550.f, feet[2]}, chest[3] = {p[0], p[1] + 1150.f, p[2]}, head[3] = {p[0], p[1] + 1650.f, p[2]}, foot[3] = {p[0], p[1] + 150.f, p[2]};
        if (!gNav->Clear(eye, chest) || !gNav->Clear(eye, head) || !gNav->Clear(eye, foot)) continue;
        me[0] = feet[0]; me[1] = feet[1] + 800.f; me[2] = feet[2];
        him[0] = p[0]; him[1] = p[1] + 800.f; him[2] = p[2];
        return true;
    }
    return false;
}
static uint16_t Yaw16(float a) { return (uint16_t)((long)lrintf(a / 6.2831853f * 65536.f) & 0xFFFF); }
static float Bear(const float* from, const float* to) { return std::atan2(to[0] - from[0], to[2] - from[2]); }
static std::vector<std::array<float, 3>> Spawns() {
    std::vector<std::array<float, 3>> pts;
    for (const doll::Spawn& sp : gG.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) pts.push_back({sp.p[0], sp.p[1], sp.p[2]});
    return pts;
}
static int16_t S16(const std::vector<uint8_t>& r, int o) { return (int16_t)(r[o] | r[o + 1] << 8); }
static int D16(int a, int b) { return std::abs((int16_t)(uint16_t)(a - b)); }   // (two 16-bit angles apart)
// the game's rule again (0x438D88: his yaw's unit vector dotted with the attack's vector, below 0: from his front)
static bool Front(int yaw, float vx, float vz) { const float a = (float)(yaw & 0xFFFF) * 6.2831853f / 65536.f; return std::sin(a) * vx + std::cos(a) * vz < 0.f; }
// one fight of the doll (slot 1, red) with the enemy (slot 2, blue) still at him with the yaw `yaw` and the state `state`
struct Hit { long long frame; std::vector<uint8_t> fx, dmg, react; };
static std::vector<Hit> Play(float* pos, const float* him, int yaw, unsigned state, int frames) {
    for (int i = 1; i < 16; ++i) gHave[i] = false;
    gHave[2] = true; gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1; gPlayers[2].object = gPlayers[2].alive = true; gPlayers[2].life = 200; gPlayers[2].cls = 1;
    memcpy(gPlayers[2].pos, him, 3 * sizeof(float)); gPlayers[2].state = state;
#ifdef VDOLL_PLAYERNOW_YAW
    gPlayers[2].yaw = yaw; gPlayers[2].yawTo = yaw;
#endif
    uint16_t facing = Yaw16(Bear(pos, him));
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].cls = 1;
    memcpy(gPlayers[1].pos, pos, 3 * sizeof(float));
    float copy[3] = {pos[0], pos[1], pos[2]};
    gRecs.clear();
    for (int t = 0; t < frames; ++t, ++gFrame) {
        const float dx = pos[0] - copy[0], dy = pos[1] - copy[1], dz = pos[2] - copy[2], d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float k = d > 50.f ? 50.f / d : 1.f;
        copy[0] += dx * k; copy[1] += dy * k; copy[2] += dz * k;
        if (gFrame % 6 == 0) memcpy(gPlayers[1].pos, copy, sizeof copy);
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
    }
    vdoll::body::Down(0);
    gFrame += 600;
    for (int i = 0; i < 6; ++i) vdoll::body::Frame(gFrame - 6 + i);
    // its hits on him: each damage record with the effect and the reaction records of the same frame on his channels
    std::vector<Hit> out;
    for (size_t i = 0; i < gRecs.size(); ++i) {
        const Rec& r = gRecs[i];
        if (r.slot != 2 || r.kind != 0 || r.r.size() < 9 || r.r[0] != 0x04 || r.r[1] != 1) continue;
        Hit h; h.frame = r.frame; h.dmg = r.r;
        for (const Rec& q : gRecs) {
            if (q.slot != 2 || q.frame != r.frame) continue;
            if (q.kind == 2 && q.r.size() >= 17 && q.r[0] == 0x14) h.fx = q.r;
            if (q.kind == 1 && q.r.size() >= 5 && q.r[0] == 0x09) h.react = q.r;
        }
        out.push_back(h);
    }
    return out;
}
static void PartFlame(bool lying) {
    int fights = 0, contacts = 0, bad = 0, before = 0, past = 0, none = 0;
    static const unsigned kLie[3] = {0x8u, 0x10000008u, 0x8000008u};
    for (const auto& sp : Spawns()) {
        if (fights >= 9) break;
        float me[3], him[3];
        if (!PickPair(sp.data(), 2000.f, 3500.f, me, him)) continue;
        const int f = fights++;
        const float toMe = Bear(him, me);
        // (facing the doll, his back to it, side on; lying: on his front, down after a roll, down after a blast)
        const int yaw = Yaw16(toMe + (f % 3 == 0 ? 0.f : f % 3 == 1 ? 3.14159265f : 1.5708f));
        const unsigned st = lying ? kLie[f % 3] : 0u;
        if (lying) him[1] -= 500.f;                         // (his hips 300 mm over the floor)
        float pos[3] = {me[0], me[1], me[2]};
        const std::vector<Hit> hits = Play(pos, him, yaw, st, 600);
        int n = 0;
        for (const Hit& h : hits) {
            if (h.dmg[6] != 0x86) continue;                 // (the flame's damage records only)
            ++n; ++contacts;
            if (h.react.empty()) { ++none; continue; }
            if (h.fx.empty()) { ++bad; Fail("%s: a contact with no effect record (f%lld)", lying ? "flamelying" : "flame", h.frame); continue; }
            const float vx = S16(h.fx, 11) * 10.f, vz = S16(h.fx, 15) * 10.f;
            const int kind = h.react[2], ang = h.react[3] | h.react[4] << 8;
            int wantK, wantA;
            bool either = false;                            // (the vector across his facing within the record's tenths: either kind, its own angle)
            if (lying) { wantK = (st & 0x10000000u) ? 4 : (st & 0x8000000u) ? 5 : 3; wantA = yaw; }
            else {
                const bool fr = Front(yaw, vx, vz); wantK = fr ? 1 : 2; wantA = fr ? yaw : (yaw + 0x8000) & 0xFFFF;
                const float a = (float)yaw * 6.2831853f / 65536.f;
                if (std::fabs(std::sin(a) * vx + std::cos(a) * vz) < 25.f && (kind == 1 || kind == 2)) { either = true; wantK = kind; wantA = kind == 1 ? yaw : (yaw + 0x8000) & 0xFFFF; }
            }
            (void)either;
            if (kind != wantK || D16(ang, wantA) > 2) {
                ++bad;
                if (bad <= 6) Fail("%s: the reaction %d at %04x where a shooter's game tells %d at %04x (his yaw %04x, the record's vector %.0f %.0f, state %x)", lying ? "flamelying" : "flame",
                                   kind, ang, wantK, wantA, yaw, vx, vz, st);
            }
            // (the fire that touches him: before him (between the doll and him) or past him along the stream; the stream's way: from the doll to him)
            const float ux = him[0] - pos[0], uz = him[2] - pos[2], ul = std::max(1.f, std::hypot(ux, uz));
            const float along = (vx * ux + vz * uz) / ul;       // (his point less the fire: along the stream, the fire before him)
            if (along > 30.f) ++before; else if (along < -30.f) ++past;
        }
        printf("    %s fight %d: %.1f m, his yaw %04x (%s), state %x: %d contacts\n", lying ? "flamelying" : "flame", f, std::hypot(him[0] - me[0], him[2] - me[2]) / 1000.f, yaw,
               f % 3 == 0 ? "facing the doll" : f % 3 == 1 ? "his back to it" : "side on", st, n);
    }
    printf("    %s: %d fights, %d contacts, %d reactions not the game's, %d without one; the fire before him %d, past him %d\n", lying ? "flamelying" : "flame", fights, contacts, bad, none,
           before, past);
    if (fights < 3) { Fail("%s: only %d fights found", lying ? "flamelying" : "flame", fights); return; }
    if (contacts < 4) Fail("%s: only %d contacts", lying ? "flamelying" : "flame", contacts);
    if (none) Fail("%s: %d contacts without a reaction (every burning contact carries one: the gate's 0x2000)", lying ? "flamelying" : "flame", none);
    if (!lying && contacts >= 8 && (before * 4 < contacts || past * 4 < contacts))
        Fail("flame: the fire that touches him before him %d and past him %d of %d contacts (real clients: both ways, about half each)", before, past, contacts);
}
static void PartM37(bool lying) {
    int fights = 0, hitsN = 0, bad = 0, by[5] = {0, 0, 0, 0, 0};
    for (const auto& sp : Spawns()) {
        if (fights >= 10) break;
        float me[3], him[3];
        const float lo = 2000.f + 1200.f * (float)(fights % 10), hi = lo + 1500.f;
        if (!PickPair(sp.data(), lo, hi, me, him)) continue;
        const int f = fights++;
        const int yaw = Yaw16(Bear(him, me) + (f % 2 ? 3.14159265f : 0.f));
        const unsigned st = lying ? 0x8u : 0u;
        if (lying) him[1] -= 500.f;
        float pos[3] = {me[0], me[1], me[2]};
        const std::vector<Hit> hits = Play(pos, him, yaw, st, 900);
        int n = 0;
        for (const Hit& h : hits) {
            if (h.dmg[6] != 0x0E) continue;                 // (the M37's damage records)
            const int dmg = S16(h.dmg, 2), pellets = dmg / 25;
            if (dmg % 25 || pellets < 1 || pellets > 4) continue;   // (MGO1_VDOLL_TEST_HARMLESS and such: not counted)
            ++n; ++hitsN; ++by[pellets];
            const int power = 250 * pellets;
            int wantK = 0, wantA = 0;
            float vx = 0, vz = 0;
            if (!h.fx.empty()) { vx = S16(h.fx, 11); vz = S16(h.fx, 15); }
            const int back = (Yaw16(std::atan2(vx, vz)) + 0x8000) & 0xFFFF;
            if (power >= 750) {
                if (lying) { wantK = power >= 1000 ? -1 : 3; wantA = yaw; }   // (-1: 8 with room over him, else 3)
                else { const bool fr = Front(yaw, vx, vz); wantK = power >= 1000 ? (fr ? 6 : 7) : (fr ? 1 : 2); wantA = fr ? back : (back + 0x8000) & 0xFFFF; }
            }
            const int kind = h.react.empty() ? 0 : h.react[2], ang = h.react.empty() ? 0 : h.react[3] | h.react[4] << 8;
            bool ok;
            if (wantK == -1) ok = (kind == 8 && D16(ang, back) <= 64) || (kind == 3 && D16(ang, yaw) <= 2);
            else if (!wantK) ok = kind == 0;
            else ok = kind == wantK && D16(ang, wantA) <= 64;   // (the pellets' way from the record's vector, in tenths)
            if (!ok) {
                ++bad;
                if (bad <= 6) Fail("%s: %d pellets (%d of damage): the reaction %d at %04x where a shooter's game tells %s %d at %04x (his yaw %04x, the vector %.0f %.0f)", lying ? "m37lying" : "m37",
                                   pellets, dmg, kind, ang, wantK == -1 ? "8 or" : "", wantK == -1 ? 3 : wantK, wantA, yaw, vx, vz);
            }
        }
        printf("    %s fight %d: %.1f m, his yaw %04x (%s): %d hits\n", lying ? "m37lying" : "m37", f, std::hypot(him[0] - me[0], him[2] - me[2]) / 1000.f, yaw,
               f % 2 ? "his back to it" : "facing the doll", n);
    }
    printf("    %s: %d fights, %d hits (1 to 4 pellets: %d %d %d %d), %d reactions not the game's\n", lying ? "m37lying" : "m37", fights, hitsN, by[1], by[2], by[3], by[4], bad);
    if (fights < 4) { Fail("%s: only %d fights found", lying ? "m37lying" : "m37", fights); return; }
    if (hitsN < 4) Fail("%s: only %d hits", lying ? "m37lying" : "m37", hitsN);
    if (by[3] + by[4] == 0) Fail("%s: no hit of 3 or 4 pellets (a reaction's power)", lying ? "m37lying" : "m37");
}

// the rule's table (no map): vdoll_react.h against the game's branches read here again (0x438960 on); b16 has no such rule
static void PartRules() {
#if __has_include("vdoll_react.h")
    using namespace vreact;
    auto chk = [](bool ok, const char* what) { if (!ok) Fail("rules: %s", what); else printf("    rules: %s\n", what); };
    Victim up; up.yaw = 0x4000; up.yawTo = 0x4000;              // (standing, facing +x)
    Victim lie = up; lie.state = 0x8;                           // (lying on his front)
    Victim roll = up; roll.state = 0x10000008; Victim blast = up; blast.state = 0x8000008; Victim cramped = lie; cramped.room = false;
    // (the attack's vector toward -x runs against his facing: from his front; toward +x: from behind)
    Out o = Burn(up, -100.f, 0.f, 0);   chk(o.kind == 1 && o.angle == 0x4000, "a burning hit from his front: 1 at his yaw");
    o = Burn(up, 100.f, 0.f, 0);        chk(o.kind == 2 && o.angle == 0xC000, "a burning hit from behind: 2 at his yaw + 0x8000");
    o = Burn(lie, 100.f, 0.f, 0);       chk(o.kind == 3 && o.angle == 0x4000, "a burning hit lying on his front: 3 at the yaw he turns to");
    o = Burn(roll, 100.f, 0.f, 0);      chk(o.kind == 4, "a burning hit down after a roll (0x1C): 4");
    o = Burn(blast, 100.f, 0.f, 0);     chk(o.kind == 5, "a burning hit down after a blast (0x1B): 5");
    o = M37(up, 1000, -100.f, 0.f);     chk(o.kind == 6 && o.angle == (uint16_t)(Angle16(-100.f, 0.f) + 0x8000), "the M37 at 1000 from his front: 6, the pellets' way less 0x8000");
    o = M37(up, 1000, 100.f, 0.f);      chk(o.kind == 7 && o.angle == Angle16(100.f, 0.f), "the M37 at 1000 from behind: 7, the angle + 0x8000 more");
    o = M37(up, 750, -100.f, 0.f);      chk(o.kind == 1, "the M37 at 750 from his front: 1");
    o = M37(up, 999, 100.f, 0.f);       chk(o.kind == 2, "the M37 at 999 from behind: 2");
    o = M37(up, 500, -100.f, 0.f);      chk(o.kind == 0, "the M37 under 750: no reaction");
    o = M37(lie, 1000, -100.f, 0.f);    chk(o.kind == 8, "the M37 at 1000 on a lying man with room over him: 8");
    o = M37(cramped, 1000, -100.f, 0.f); chk(o.kind == 3 && o.angle == 0x4000, "the M37 at 1000 on a lying man without room: 3 at the yaw he turns to");
    o = M37(roll, 750, -100.f, 0.f);    chk(o.kind == 4, "the M37 at 750 on a man down after a roll: 4 (room or not)");
    o = Throw(up, true, 100.f, 0.f);    chk(o.kind == 8 && o.angle == Angle16(-100.f, 0.f), "a blast on a standing man: 8, toward the blast");
    o = Throw(up, false, 100.f, 0.f);   chk(o.kind == 9, "a roll into a standing man: 9");
    o = Throw(lie, false, 100.f, 0.f);  chk(o.kind == 8, "a roll into a lying man with room over him: 8");
    o = Throw(cramped, true, 100.f, 0.f); chk(o.kind == 3, "a blast on a lying man without room: 3");
    o = Blow(up, 0, 0x1000);            chk(o.kind == 0, "a punch on a standing man: no reaction (measured)");
    o = Blow(up, 1, 0x1000);            chk(o.kind == 0, "the second punch: no reaction");
    o = Blow(up, 2, 0x1000);            chk(o.kind == 9 && o.angle == 0x9000, "the kick on a standing man: 9 at the kicker's yaw + 0x8000");
    o = Blow(lie, 2, 0x1000);           chk(o.kind == 0, "the kick on a man on the ground: no reaction (the wake path)");
#else
    Fail("rules: no reaction rule (vdoll_react.h): b16's kinds");
#endif
}

static int RunPart(const char* path, const std::string& part) {
    if (part == "rules") { PartRules(); return gFails; }
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 1;
    fclose(f);
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), gG, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    static doll::NavMesh nav;
    doll::NavConfig cfg;
    std::string log;
    if (!nav.Build(gG, cfg, log)) { printf("no mesh: %s\n", log.c_str()); return 1; }
    gNav = &nav;
    // (the M37's parts at level 5 unless given: a level 10 doll aims at the head, its pellets reach him 1 or 2 at a time and a reaction's
    // 3 or 4 seldom come within a part's fights)
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", part == "m37" || part == "m37lying" ? "5" : "10", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    setenv("MGO1_VDOLL_WEAPONS", part == "flame" || part == "flamelying" ? "ak47+flamethrower" : "m37", 1);
    if (!StartBody()) { printf("the bodies' mesh was not built\n"); return 1; }
    if (part == "flame") PartFlame(false);
    else if (part == "flamelying") PartFlame(true);
    else if (part == "m37") PartM37(false);
    else if (part == "m37lying") PartM37(true);
    return gFails;
}

int main(int argc, char** argv) {
    std::vector<std::string> maps, parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "flame" || a == "flamelying" || a == "m37" || a == "m37lying" || a == "rules") parts.push_back(a); else maps.push_back(a);
    }
    if (maps.empty()) { fprintf(stderr, "usage: vdoll_react_test <map.geom> ... [rules|flame|flamelying|m37|m37lying ...]\n"); return 2; }
    if (parts.empty()) parts = {"rules", "flame", "flamelying", "m37", "m37lying"};
    printf("controls %s, level %s, MGO1_VDOLL_REACT_KIND %s\n", getenv("MGO1_VDOLL_CONTROLS") ? getenv("MGO1_VDOLL_CONTROLS") : "direct",
           getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10", getenv("MGO1_VDOLL_REACT_KIND") ? getenv("MGO1_VDOLL_REACT_KIND") : "(on)");
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
