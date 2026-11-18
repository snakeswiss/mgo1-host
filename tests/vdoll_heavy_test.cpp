// (v150-host beta 22, heavy) A virtual doll firing the RPG-7 and the flamethrower as a player's game does (MGO1Loader/vdoll_body.cpp
// HeavyWant, FireRocket, RocketBlast, FireFlame; docs/VIRTUAL_DOLL.md 33). Off line, on a map's .geom, each part in a process of its
// own (a body's state starts afresh), run a frame at a time, the doll's copy on the host following it; the doll given the weapon
// by the bench's switch (MGO1_VDOLL_WEAPONS=ak47+rpg7, ak47+flamethrower), as a weapon box gives it:
//   range   an enemy standing 15 to 30 m away in its sight: it fires its RPG-7 at him (the rocket's weapon record, the long view,
//           the shot's record, in that order; never while it moves), the blast's records reach him after the rocket's flight,
//           with the game's bytes: {14 shooter fx..} on his channel 2 and {04 shooter, damage, 0, kind, 0, 0} on his channel 0
//   close   an enemy 3 to 6 m away: no rocket (its blast would take the doll): its AK-47
//   mate    an enemy 15 to 30 m away with a teammate of the doll 1.5 m from him: no rocket
//   group   two enemies 1.5 m apart, 9.5 to 13 m away: the rocket (a group), both in its blast
//   flame   an enemy 2 to 3.5 m away: the flamethrower's records (the weapon, shots) and burns reach him, the game's bytes
//   far     an enemy 8 to 14 m away: its AK-47, no flame while he is beyond its reach (closer by its own moves, its flame)
//   off     MGO1_VDOLL_HEAVY=0 with the RPG-7 given: never in its hands (beta 21's records)
//   dodge   (heavy2, review22b H2) an enemy 20, 40 and 60 m away (where the map has such a line, 7 m clear behind him): standing,
//           its rocket meets his body (the blast's records on him, "the rocket on him"); running aside at a player's 3.9 m/s
//           from the rocket's launch, to 3 m off its line, he is passed by: no damage of the rocket on him (it bursts on the map
//           behind him, more than 6 m away), as every player's own copy of the rocket flies on past him (level 10 always, each
//           rocket judged when its line met him at its launch)
//   drop    (with pickup's part, VDOLL_BODY_PICKUP) an RPG-7 taken from a box (body::Give, 15 rounds) is fired at an enemy 15 to
//           30 m away, then at the doll's death dropped where it fell with its rounds left, as a player's game drops it (on
//           the drops' channel, Self::send kind 3: {number slot << 10 | 0x100.., 3 << 10 | 0x11, 0, x y z in tenths, rounds});
//           a second doll's RPG-7 given by the bench's switch is not dropped (docs/VIRTUAL_DOLL.md 33)
// Beta 21's body fails range, group and flame (it has no RPG-7 nor flamethrower: the switch's "+rpg7" is unknown to it).
// MGO1_VDOLL_CONTROLS and MGO1_VDOLL_LEVEL as on the host (10 when unset).
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_heavy_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_heavy_test
//   ./vdoll_heavy_test <map.geom> [<map.geom> ...] [range|close|mate|group|flame|far|off|drop|dodge ...]
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

// the game's records of a real player's shooter game, measured on probe 46 (runs/heavy_p2_rpg, heavy_p3_flame): the rocket's
// blast {04 shooter, 150, 0, 0x11, 0, 0}, {09 shooter 08 ..}, {14 shooter 00 02 ..}; the flame's {04 shooter, 95, 0, 0x86, 0, 0},
// {09 shooter 1|2 ..}, {14 shooter 04 02 ..}
static const uint8_t kBlastKind = 0x11, kFlameKind = 0x86;
static const uint16_t kBlastFx = 0x0200, kFlameFx = 0x0204;
static const int kFlameDmg = 95, kBlastDmg = 150;

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static std::vector<std::string> gLines;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    gLines.push_back(b);
    if (getenv("VDOLL_TEST_VERBOSE")) printf("    %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 1; }                          // (team deathmatch: a teammate and enemies)
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
// the doll's place (hips) on the mesh at a spawn point, an enemy's (hips) lo to hi m away, a clear line from its eye to his
// chest, head and feet, on the mesh and within 1.5 m of the doll's height
static bool PickPair(const float* spawn, float lo, float hi, float* me, float* him) {
    float feet[3];
    if (!gNav->Nearest(spawn, 1500.f, feet)) return false;
    for (int tries = 0; tries < 600; ++tries) {
        float p[3];
        if (!gNav->RandomPoint(feet, hi + 5000.f, p, Rnd)) continue;
        const float flat = std::hypot(p[0] - feet[0], p[2] - feet[2]);
        if (flat < lo || flat > hi || std::fabs(p[1] - feet[1]) > 1500.f) continue;
        const float eye[3] = {feet[0], feet[1] + 1550.f, feet[2]}, chest[3] = {p[0], p[1] + 1150.f, p[2]}, head[3] = {p[0], p[1] + 1650.f, p[2]}, foot[3] = {p[0], p[1] + 150.f, p[2]};
        if (!gNav->Clear(eye, chest) || !gNav->Clear(eye, head) || !gNav->Clear(eye, foot)) continue;
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
static std::vector<std::array<float, 3>> Spawns() {
    std::vector<std::array<float, 3>> pts;
    for (const doll::Spawn& sp : gG.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) pts.push_back({sp.p[0], sp.p[1], sp.p[2]});
    return pts;
}
struct Still { float pos[3]; int team, slot; };
static int gGiveCode = 0, gGiveRounds = 0;           // (part drop: a box's weapon given at the fight's first frame, body::Give)
static bool gDieAtEnd = false;                       // (part drop: the doll's death at the fight's end, body::Died)
static int gLeftAtDeath = -1;                        // (its special's rounds then)
// what one fight showed: its weapon records naming each code, its shots' records, the damage records on each player
struct Seen {
    int rpgRecs = 0, flameRecs = 0, shotRecs = 0, rpgShots = 0, flameShots = 0, movingShots = 0, noViewShots = 0, hitsBeforeShot = 0;
    int pulls = 0, pullsNoRocket = 0, aimed3 = 0;        // (the RPG-7: its {08 03} pulls, those with no rocket after them, 03 records not in a pull)
    int blastFx = 0, badPoint = 0, knocks = 0, badKnock = 0, noKnock = 0;
    int flameFar = 0, rocketNear = 0;                    // (a moment of flame with the enemy beyond 5.5 m of the doll; a rocket fired within 8 m)   // (the blast's {14 .. 00 02 ..} records, their point wrong; its {09 .. 08 ..})
    int dmg[16] = {}, dmgRecs[16] = {}, badKind = 0, badFx = 0, badBytes = 0;   // (the rocket's and the flame's damage records: their kind's)
    long long firstShot = -1, firstDmg = -1;
    std::string bytes;                                   // (the first damage record's bytes, hex)
};
// one fight of the doll (slot 1, red) with the players standing still, `frames` frames
static Seen Play(float* pos, uint16_t facing, const std::vector<Still>& still, int frames) {
    Seen o;
    for (int i = 1; i < 16; ++i) gHave[i] = false;
    for (const Still& p : still) {
        gHave[p.slot] = true; gPlayers[p.slot] = vdoll::PlayerNow(); gPlayers[p.slot].team = p.team; gPlayers[p.slot].object = gPlayers[p.slot].alive = true;
        gPlayers[p.slot].life = 200; gPlayers[p.slot].cls = 1; memcpy(gPlayers[p.slot].pos, p.pos, sizeof p.pos);
    }
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.life = gFrame; s.pos = pos; s.facing = &facing; s.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].cls = 1;
    memcpy(gPlayers[1].pos, pos, 3 * sizeof(float));
    float copy[3] = {pos[0], pos[1], pos[2]};
    gRecs.clear();
    long long movedAt = gFrame, longView = -1000, lastShotRec = -1000, pullAt = -1;
    int last08 = -1;
    int inHands = -1;
    vdoll::body::AimInfo was, now;
    vdoll::body::Aim(0, &was);
    for (int t = 0; t < frames; ++t, ++gFrame) {
        {                                                // (its copy follows it, 50 mm a frame at most; read every 100 ms)
            const float dx = pos[0] - copy[0], dy = pos[1] - copy[1], dz = pos[2] - copy[2], d = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float k = d > 50.f ? 50.f / d : 1.f;
            copy[0] += dx * k; copy[1] += dy * k; copy[2] += dz * k;
            if (gFrame % 6 == 0) memcpy(gPlayers[1].pos, copy, sizeof copy);
        }
        const float p0[3] = {pos[0], pos[1], pos[2]};
        const size_t r0 = gRecs.size();
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
        if (t == 0 && gGiveCode && !vdoll::body::Give(0, gGiveCode, gGiveRounds)) Fail("drop: Give refused the code %#x", gGiveCode);
        vdoll::body::Aim(0, &now);
        if (std::fabs(pos[0] - p0[0]) + std::fabs(pos[2] - p0[2]) > 0.5f) movedAt = gFrame;
        for (size_t i = r0; i < gRecs.size(); ++i) {
            const Rec& r = gRecs[i];
            if (r.slot == 1 && r.r.size() >= 3 && r.r[0] == 0x13) { inHands = r.r[2]; if (r.r[2] == 0x11) ++o.rpgRecs; if (r.r[2] == 0x09) ++o.flameRecs; }
            if (r.slot == 1 && r.r.size() > 2 && r.r[0] == 0x12) longView = gFrame;
            // (the RPG-7 as a player's game tells it: held in its sight {08 01}, the pull {08 03} and the rocket 14 frames later, then
            // {08 05} some records: a pull is a rocket on every machine, so never an {08 03} that no rocket follows)
            if (r.slot == 1 && r.r.size() >= 2 && r.r[0] == 0x08) {
                const int k8 = r.r[1];
                if (inHands == 0x11 && k8 == 3 && last08 != 3) { ++o.pulls; pullAt = gFrame; }
                if (inHands == 0x11 && k8 != 3 && k8 != 5 && last08 == 3) ++o.pullsNoRocket;
                if (k8 == 5 && last08 != 5) {
                    ++o.shotRecs; lastShotRec = gFrame;
                    if (inHands == 0x11) { ++o.rpgShots; if (longView != gFrame) ++o.noViewShots; if (gFrame - movedAt < 20) ++o.movingShots; if (pullAt < 0 || gFrame - pullAt < 12 || gFrame - pullAt > 20) ++o.aimed3; pullAt = -1; }
                    if (inHands == 0x09) ++o.flameShots;
                    if (o.firstShot < 0) o.firstShot = t;
                } else if (k8 == 5 && inHands == 0x09) ++o.flameShots;
                if (k8 == 5 && gHave[2]) {                   // (how far the enemy is when it fires: the flame's reach, the rocket's own blast)
                    const float dd = std::hypot(gPlayers[2].pos[0] - pos[0], gPlayers[2].pos[2] - pos[2]);
                    if (inHands == 0x09 && dd > 5500.f) ++o.flameFar;
                    if (inHands == 0x11 && last08 != 5 && dd < 8000.f) ++o.rocketNear;
                }
                last08 = k8;
            }
            // (the rocket's and the flame's damage records: those of the kinds 0x11, 0x91 and 0x09; the gun's and the CQC's apart)
            const bool heavyRec = r.r.size() >= 9 && (r.r[6] == 0x11 || r.r[6] == 0x91 || r.r[6] == 0x09 || r.r[6] == 0x86);
            if (r.slot >= 2 && r.slot < 16 && r.kind == 0 && r.r.size() >= 9 && r.r[0] == 0x04 && heavyRec) {
                const int dmg = r.r[2] | r.r[3] << 8;
                o.dmg[r.slot] += dmg; ++o.dmgRecs[r.slot];
                if (o.firstDmg < 0) o.firstDmg = t;
                if (lastShotRec < 0 || gFrame < lastShotRec) ++o.hitsBeforeShot;
                if (r.r[1] != 1 || r.r[4] || r.r[5] || r.r[7] || r.r[8] || dmg != (r.r[6] == kBlastKind ? kBlastDmg : kFlameDmg)) ++o.badBytes;
                if (r.r[6] != (o.rpgShots ? kBlastKind : kFlameKind)) ++o.badKind;
                if (o.bytes.empty()) { char h[4]; for (uint8_t c : r.r) { snprintf(h, sizeof h, "%02x", c); o.bytes += h; } }
            }
            // (the blast's effect record: his body's point, his hips + 230, less 2500 mm in x, as the game's (int16) mm then tenths)
            if (r.slot >= 2 && r.slot < 16 && r.kind == 2 && r.r.size() >= 17 && r.r[0] == 0x14 && (((r.r[2] | r.r[3] << 8) == kBlastFx && o.rpgShots) || ((r.r[2] | r.r[3] << 8) == kFlameFx && o.flameShots))) {
                ++o.blastFx;
                const float* hp = gPlayers[r.slot].pos;
                const float want[3] = {hp[0] - (o.rpgShots ? 2500.f : 600.f), hp[1] + 230.f, hp[2]};
                for (int k = 0; k < 3; ++k) {
                    const int16_t got = (int16_t)(r.r[5 + 2 * k] | r.r[6 + 2 * k] << 8), w = (int16_t)(int)((float)(int16_t)(int)want[k] / 10.f);
                    if (std::abs(got - w) > 1) ++o.badPoint;
                }
                if (r.r[1] != 1 || r.r[4] != 0) ++o.badPoint;
            }
            if (r.slot >= 2 && r.slot < 16 && r.kind == 0 && r.r.size() >= 9 && r.r[0] == 0x04 && (r.r[6] == kBlastKind || r.r[6] == kFlameKind)) {
                // (its reaction record in the same tick: the blast's knock down {09 shooter 08 ..}, the flame's flinch {09 shooter 1|2 ..};
                // a roll's own {09 .. 06} beside it is not counted)
                bool k = false;
                for (size_t j = r0; j < gRecs.size(); ++j)
                    if (gRecs[j].slot == r.slot && gRecs[j].kind == 1 && gRecs[j].r.size() >= 5 && gRecs[j].r[0] == 0x09 && gRecs[j].r[1] == 1 &&
                        (r.r[6] == kBlastKind ? gRecs[j].r[2] == 8 : gRecs[j].r[2] == 1 || gRecs[j].r[2] == 2)) { k = true; ++o.knocks; }
                if (!k) ++o.noKnock;
            }
            if (r.slot >= 2 && r.slot < 16 && r.kind == 2 && r.r.size() >= 5 && r.r[0] == 0x14 && (o.rpgShots || o.flameShots)) {
                const uint16_t fx = (uint16_t)(r.r[2] | r.r[3] << 8);
                if (fx != kBlastFx && fx != kFlameFx && fx != 0x010A && fx != 0x0208 && fx != 0x0204) ++o.badFx;   // (its gun's and its frag grenade's own between)
            }
        }
        was = now;
    }
#ifdef VDOLL_BODY_PICKUP
    if (gDieAtEnd) { vdoll::body::Special(0, &gLeftAtDeath); vdoll::body::Died(s); }
#endif
    vdoll::body::Down(0);
    gFrame += 600;                                       // (a rocket in flight gone off before the next fight)
    for (int i = 0; i < 6; ++i) vdoll::body::Frame(gFrame - 6 + i);
    return o;
}
static void Say(const char* what, const Seen& o, const std::vector<Still>& still) {
    printf("    %s: weapon records RPG-7 %d, flamethrower %d; shots %d (rockets %d, flame %d), first shot +%lld, first damage +%lld (%s);", what, o.rpgRecs, o.flameRecs,
           o.shotRecs, o.rpgShots, o.flameShots, o.firstShot, o.firstDmg, o.bytes.c_str());
    for (const Still& p : still) printf(" player %d: %d in %d records;", p.slot + 1, o.dmg[p.slot], o.dmgRecs[p.slot]);
    printf("\n");
}
static void Bytes(const char* what, const Seen& o) {
    if (o.movingShots) Fail("%s: %d rockets fired with the doll moving within 20 frames (the RPG-7 holds a player where he stands)", what, o.movingShots);
    if (o.noViewShots) Fail("%s: %d rocket shots without the long view record in the same tick", what, o.noViewShots);
    if (o.badBytes) Fail("%s: %d damage records whose bytes are not the game's {04 shooter, damage, 0, kind, 0, 0}", what, o.badBytes);
    if (o.badKind) Fail("%s: %d damage records whose kind is not the weapon's (blast %02x, flame %02x)", what, o.badKind, kBlastKind, kFlameKind);
    if (o.badFx) Fail("%s: %d effect records whose kind is not the weapon's", what, o.badFx);
    if (o.hitsBeforeShot) Fail("%s: %d damage records before the record of their shot", what, o.hitsBeforeShot);
    if (o.pullsNoRocket) Fail("%s: %d pulls of the RPG-7 ({08 03}) with no rocket after them (every machine's copy fires one)", what, o.pullsNoRocket);
    if (o.aimed3) Fail("%s: %d rockets not 12 to 20 frames after their pull (the game's 14)", what, o.aimed3);
    if (o.flameFar) Fail("%s: %d moments of flame with the enemy beyond 5.5 m", what, o.flameFar);
    if (o.rocketNear) Fail("%s: %d rockets fired at an enemy within 8 m (its own blast)", what, o.rocketNear);
    if (o.badPoint) Fail("%s: %d values of the blast's effect records not the game's (his point less 2500 mm in x, tenths)", what, o.badPoint);
    if (o.badKnock || o.noKnock) Fail("%s: knock down records not the game's {09 shooter 08 angle}: %d wrong, %d blasts without one", what, o.badKnock, o.noKnock);
}

static void PartRange() {
    int fights = 0, fired = 0, hit = 0;
    for (const auto& sp : Spawns()) {
        if (fights >= 6) break;
        float me[3], him[3];
        if (!PickPair(sp.data(), 15000.f, 30000.f, me, him)) continue;
        ++fights;
        std::vector<Still> st = {{{him[0], him[1], him[2]}, 1, 2}};
        const Seen o = Play(me, Facing(me, him), st, 900);
        Say("range", o, st);
        Bytes("range", o);
        if (o.rpgShots) ++fired;
        if (o.dmgRecs[2]) ++hit;
    }
    if (fights < 3) { Fail("range: only %d fights found", fights); return; }
    if (fired < fights) Fail("range: the RPG-7 fired in %d fights of %d (an enemy 15 to 30 m away in its sight)", fired, fights);
    if (hit * 2 < fights) Fail("range: its blast reached him in %d fights of %d", hit, fights);
}
static void PartClose() {
    int fights = 0;
    for (const auto& sp : Spawns()) {
        if (fights >= 6) break;
        float me[3], him[3];
        if (!PickPair(sp.data(), 3000.f, 6000.f, me, him)) continue;
        ++fights;
        std::vector<Still> st = {{{him[0], him[1], him[2]}, 1, 2}};
        const Seen o = Play(me, Facing(me, him), st, 600);
        Say("close", o, st);
        if (o.rpgRecs || o.rpgShots) Fail("close: the RPG-7 in its hands (%d records, %d rockets) at an enemy 3 to 6 m away", o.rpgRecs, o.rpgShots);
        if (!o.shotRecs) Fail("close: no shot at all at an enemy 3 to 6 m away (its AK-47)");
    }
    if (fights < 3) Fail("close: only %d fights found", fights);
}
static void PartMate() {
    int fights = 0;
    for (const auto& sp : Spawns()) {
        if (fights >= 6) break;
        float me[3], him[3];
        if (!PickPair(sp.data(), 15000.f, 30000.f, me, him)) continue;
        ++fights;
        float mate[3] = {him[0] + 1500.f, him[1], him[2]};
        std::vector<Still> st = {{{him[0], him[1], him[2]}, 1, 2}, {{mate[0], mate[1], mate[2]}, 0, 3}};
        const Seen o = Play(me, Facing(me, him), st, 600);
        Say("mate", o, st);
        if (o.rpgShots) Fail("mate: %d rockets at an enemy with its teammate 1.5 m from him", o.rpgShots);
        if (o.dmgRecs[3]) Fail("mate: its teammate wounded");
    }
    if (fights < 3) Fail("mate: only %d fights found", fights);
}
static void PartGroup() {
    int fights = 0, fired = 0, both = 0;
    for (const auto& sp : Spawns()) {
        if (fights >= 6) break;
        float me[3], him[3];
        if (!PickPair(sp.data(), 9500.f, 13000.f, me, him)) continue;
        ++fights;
        const float a = std::atan2(him[0] - me[0], him[2] - me[2]) + 1.5708f;
        std::vector<Still> st = {{{him[0], him[1], him[2]}, 1, 2}, {{him[0] + std::sin(a) * 1500.f, him[1], him[2] + std::cos(a) * 1500.f}, 1, 3}};
        const Seen o = Play(me, Facing(me, him), st, 900);
        Say("group", o, st);
        Bytes("group", o);
        if (o.rpgShots) ++fired;
        if (o.dmgRecs[2] && o.dmgRecs[3]) ++both;
    }
    if (fights < 3) { Fail("group: only %d fights found", fights); return; }
    if (fired < fights) Fail("group: the RPG-7 fired in %d fights of %d (two enemies 1.5 m apart, 9.5 to 13 m away)", fired, fights);
    if (both * 2 < fights) Fail("group: both in its blast in %d fights of %d", both, fights);
}
static void PartFlame(bool far) {
    int fights = 0, fired = 0, hit = 0;
    for (const auto& sp : Spawns()) {
        if (fights >= 6) break;
        float me[3], him[3];
        if (!PickPair(sp.data(), far ? 8000.f : 2000.f, far ? 14000.f : 3500.f, me, him)) continue;
        ++fights;
        std::vector<Still> st = {{{him[0], him[1], him[2]}, 1, 2}};
        const Seen o = Play(me, Facing(me, him), st, 600);
        Say(far ? "far" : "flame", o, st);
        Bytes(far ? "far" : "flame", o);
        if (far) {
            if (o.flameFar) Fail("far: %d moments of flame with the enemy beyond 5.5 m (out of its reach)", o.flameFar);   // (closer by its own moves: its flame, rightly)
            if (!o.shotRecs) Fail("far: no shot at all (its AK-47)");
            continue;
        }
        if (o.flameShots) ++fired;
        if (o.dmgRecs[2]) ++hit;
        if (o.dmgRecs[2] && o.dmg[2] != kFlameDmg * o.dmgRecs[2]) Fail("flame: %d of damage in %d records (%d each, the game's)", o.dmg[2], o.dmgRecs[2], kFlameDmg);
    }
    if (fights < 3) { Fail("%s: only %d fights found", far ? "far" : "flame", fights); return; }
    // (two fights in three at least: at 2 to 3.5 m a doll of level 6 and up may grab him or roll into him first, its CQC, and
    // fight on with its gun where he lands beyond the flame's reach)
    if (!far && fired * 3 < fights * 2) Fail("flame: the flamethrower fired in %d fights of %d (an enemy 2 to 3.5 m away)", fired, fights);
    if (!far && hit * 3 < fights * 2) Fail("flame: its flame reached him in %d fights of %d", hit, fights);
}
static void PartOff() {
    int fights = 0;
    for (const auto& sp : Spawns()) {
        if (fights >= 4) break;
        float me[3], him[3];
        if (!PickPair(sp.data(), 15000.f, 30000.f, me, him)) continue;
        ++fights;
        std::vector<Still> st = {{{him[0], him[1], him[2]}, 1, 2}};
        const Seen o = Play(me, Facing(me, him), st, 600);
        Say("off", o, st);
        if (o.rpgRecs || o.flameRecs || o.rpgShots) Fail("off: MGO1_VDOLL_HEAVY=0 and a special weapon in its hands (%d records)", o.rpgRecs + o.flameRecs);
    }
    if (fights < 2) Fail("off: only %d fights found", fights);
}

// (heavy2) one rocket at an enemy standing at him (him: hips), or running aside from its launch (3.9 m/s, 65 mm a frame, to 3 m
// off the line): the rocket's damage records on him, whether its launch came, the frames from the launch to its blast
struct Dodge { bool launched = false, inLine = false; int dmg = 0, onHim = 0; long long flight = -1; };   // (inLine: his body in the rocket's line at its launch)
static Dodge OneRocket(float* me, const float* him, bool run) {
    Dodge o;
    for (int i = 1; i < 16; ++i) gHave[i] = false;
    gHave[2] = true; gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1; gPlayers[2].object = gPlayers[2].alive = true; gPlayers[2].life = 200; gPlayers[2].cls = 1;
    memcpy(gPlayers[2].pos, him, 3 * sizeof(float));
    uint16_t facing = Facing(me, him);
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.life = gFrame; s.pos = me; s.facing = &facing; s.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].cls = 1;
    memcpy(gPlayers[1].pos, me, 3 * sizeof(float));
    const float dx = him[0] - me[0], dz = him[2] - me[2], dl = std::max(1.f, std::hypot(dx, dz));
    const float side[2] = {-dz / dl, dx / dl};           // (square to the line, flat)
    gRecs.clear();
    const size_t lines0 = gLines.size();
    long long launchAt = -1, blastAt = -1;
    float moved = 0.f;
    for (int t = 0; t < 900; ++t, ++gFrame) {
        if (gFrame % 6 == 0) memcpy(gPlayers[1].pos, me, 3 * sizeof(float));
        vdoll::body::Frame(gFrame);
        s.tick = gFrame % 6 == 0;
        vdoll::body::Alive(s, gFrame);
        for (size_t i = lines0; i < gLines.size(); ++i) {
            if (launchAt < 0 && gLines[i].find("fires its RPG-7") != std::string::npos) { launchAt = gFrame; o.inLine = gLines[i].find("(player 3 in its line") != std::string::npos; }
            if (blastAt < 0 && launchAt >= 0 && (gLines[i].find("its rocket's blast") != std::string::npos || gLines[i].find("hits nobody") != std::string::npos)) blastAt = gFrame;
            if (gLines[i].find("(the rocket on him)") != std::string::npos) ++o.onHim;
        }
        gLines.resize(lines0);                           // (each frame's lines read once)
        if (run && launchAt >= 0 && moved < 3000.f) {    // (from the launch, aside at a player's run: 3.9 m/s)
            const float st = std::min(65.f, 3000.f - moved);
            gPlayers[2].pos[0] += side[0] * st; gPlayers[2].pos[2] += side[1] * st; moved += st;
        }
        if (launchAt >= 0 && (gFrame > launchAt + 120 || (blastAt >= 0 && gFrame > blastAt + 3))) break;   // (its flight at 60 m: 14 + 73 frames; its next rocket 145 frames on at least)
    }
    for (const Rec& r : gRecs) if (r.slot == 2 && r.kind == 0 && r.r.size() >= 9 && r.r[0] == 0x04 && r.r[6] == kBlastKind) o.dmg += r.r[2] | r.r[3] << 8;
    o.launched = launchAt >= 0;
    o.flight = launchAt >= 0 && blastAt >= 0 ? blastAt - launchAt : -1;
    vdoll::body::Down(0);
    gFrame += 600;
    for (int i = 0; i < 6; ++i) vdoll::body::Frame(gFrame - 6 + i);
    return o;
}
static void PartDodge() {
    int tested = 0, judged = 0;
    for (float want : {20000.f, 40000.f, 60000.f}) {
        float me[3], him[3];
        bool found = false;
        for (const auto& sp : Spawns()) {
            for (int tries = 0; tries < 40 && !found; ++tries) {
                if (!PickPair(sp.data(), want - 3000.f, want + 3000.f, me, him)) break;
                // (7 m clear behind him along the line: the rocket that passes him bursts more than 6 m from him once he is
                // 3 m aside)
                const float eye[3] = {me[0], me[1] + 750.f, me[2]}, chest[3] = {him[0], him[1] + 350.f, him[2]};
                float dir[3], l = 0;
                for (int k = 0; k < 3; ++k) { dir[k] = chest[k] - eye[k]; l += dir[k] * dir[k]; }
                l = std::sqrt(l);
                const float beyond[3] = {chest[0] + dir[0] / l * 7000.f, chest[1] + dir[1] / l * 7000.f, chest[2] + dir[2] / l * 7000.f};
                if (!gNav->Clear(chest, beyond)) continue;
                found = true;
            }
            if (found) break;
        }
        if (!found) { printf("    dodge: no line of %.0f m with 7 m clear behind on this map\n", want / 1000.f); continue; }
        ++tested;
        const float d = std::hypot(him[0] - me[0], him[2] - me[2]);
        const Dodge still = OneRocket(me, him, false);
        const Dodge run = OneRocket(me, him, true);
        printf("    dodge at %.1f m: standing %s%s, %d damage (the rocket on him %d), its flight %lld frames; running aside %s%s, %d damage (on him %d), %lld frames\n",
               d / 1000.f, still.launched ? "fired" : "not fired", still.inLine ? " at him" : " beside him", still.dmg, still.onHim, still.flight,
               run.launched ? "fired" : "not fired", run.inLine ? " at him" : " beside him", run.dmg, run.onHim, run.flight);
        if (!still.launched || !run.launched) { Fail("dodge at %.1f m: no rocket fired (%d, %d)", d / 1000.f, still.launched, run.launched); continue; }
        // (a rocket whose line missed him at the launch, its level's spread at that range: not judged; at level 10 none)
        if (still.inLine && (still.dmg < kBlastDmg || !still.onHim)) Fail("dodge at %.1f m: standing still in its line and not hit by the rocket (%d damage)", d / 1000.f, still.dmg);
        if (run.inLine && (run.dmg || run.onHim)) Fail("dodge at %.1f m: 3 m aside from the launch and still hit (%d damage, on him %d): the blast where his body was at the launch", d / 1000.f, run.dmg, run.onHim);
        if (still.inLine && run.inLine) ++judged;
    }
    printf("    dodge: %d of the 3 distances on this map, %d judged (both rockets in his line at their launch)\n", tested, judged);   // (a small map has none: Killhouse A)
    if (tested && !judged) Fail("dodge: no rocket in his line at its launch at any distance");
}
// (with pickup's part) a box's RPG-7 dropped at the doll's death with its rounds left; the bench switch's one not dropped
static void PartDrop() {
#ifdef VDOLL_BODY_PICKUP
    float me[3], him[3];
    bool found = false;
    for (const auto& sp : Spawns()) if (PickPair(sp.data(), 15000.f, 30000.f, me, him)) { found = true; break; }
    if (!found) { Fail("drop: no place found"); return; }
    std::vector<Still> st = {{{him[0], him[1], him[2]}, 1, 2}};
    gGiveCode = 0x11; gGiveRounds = 15; gDieAtEnd = true;   // (MGO1_VDOLL_WEAPONS=ak47: no special but the box's)
    const Seen o = Play(me, Facing(me, him), st, 900);
    gGiveCode = 0; gDieAtEnd = false;
    Say("drop", o, st);
    const int left = gLeftAtDeath;
    if (!o.rpgShots || left >= 15 || left < 0) Fail("drop: the RPG-7 taken from the box not fired (%d rockets, %d rounds left)", o.rpgShots, left);
    int drops = 0;
    for (const Rec& x : gRecs) {
        if (x.kind != 3 || x.r.size() < 15) continue;
        const int num = x.r[0] | x.r[1] << 8, kc = x.r[2] | x.r[3] << 8;
        const int cnt = (int)(x.r[11] | x.r[12] << 8 | x.r[13] << 16 | (unsigned)x.r[14] << 24);
        const int px = (int16_t)(x.r[5] | x.r[6] << 8), pz = (int16_t)(x.r[9] | x.r[10] << 8);
        printf("    its death's drop: record %#x, %#x, state %d, at %d %d (tenths), %d rounds\n", num, kc, x.r[4], px, pz, cnt);
        if (kc != (3 << 10 | 0x11)) continue;
        ++drops;
        if (num >> 10 != 1 || (num & 0x3FF) < 0x100 || x.r[4] != 0) Fail("drop: the record's number %#x or state %d not a player's own drop's", num, x.r[4]);
        if (cnt != left) Fail("drop: %d rounds in the drop, %d left in its hands", cnt, left);
        if (std::abs(px - (int)(me[0] * 0.1f)) > 1 || std::abs(pz - (int)(me[2] * 0.1f)) > 1) Fail("drop: dropped at %d %d, it fell at %.0f %.0f", px, pz, me[0], me[2]);
    }
    if (drops != 1) Fail("drop: %d RPG-7 drops at its death (one expected)", drops);
    // a second doll (k 1, slot 3) given the RPG-7 by the switch at its spawn: none dropped
    float p2[3] = {me[0] + 1000.f, me[1], me[2]};
    uint16_t facing = 0;
    int r = -1;
    vdoll::body::Self s2;
    s2.k = 1; s2.slot = 3; s2.team = 0; s2.life = gFrame; s2.pos = p2; s2.facing = &facing; s2.send = Send;
    gHave[3] = true; gPlayers[3] = vdoll::PlayerNow(); gPlayers[3].team = 0; gPlayers[3].object = gPlayers[3].alive = true; gPlayers[3].life = 200; gPlayers[3].cls = 1;
    memcpy(gPlayers[3].pos, p2, sizeof p2);
    for (int t = 0; t < 30; ++t, ++gFrame) { vdoll::body::Frame(gFrame); s2.tick = gFrame % 6 == 0; vdoll::body::Alive(s2, gFrame); }
    if (vdoll::body::Special(1, &r) != 0x11) Fail("drop: the switch's RPG-7 not given to the second doll");
    gRecs.clear();
    vdoll::body::Died(s2);
    for (const Rec& x : gRecs) if (x.kind == 3 && x.r.size() >= 5 && (x.r[2] | x.r[3] << 8) == (3 << 10 | 0x11)) Fail("drop: the bench switch's RPG-7 dropped at the death (no box was taken)");
#else
    printf("    drop: no pickup part in this body (VDOLL_BODY_PICKUP): skipped\n");
#endif
}

static int RunPart(const char* path, const std::string& part) {
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
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    setenv("MGO1_VDOLL_WEAPONS", part == "flame" || part == "far" ? "ak47+flamethrower" : part == "drop" ? "ak47,ak47+rpg7" : "ak47+rpg7", 1);
    if (part == "off") setenv("MGO1_VDOLL_HEAVY", "0", 1);
    if (part == "dodge") {                               // (its rocket's blast that hits nobody said; level 10 whatever the run's: the
        setenv("MGO1_VDOLL_DEBUG", "1", 1);              // rocket's flight is judged here, not the aim, and a lower level's spread
        setenv("MGO1_VDOLL_LEVEL", "10", 1);             // misses a standing man at 40 m and more)
    }
    if (!StartBody()) { printf("the bodies' mesh was not built\n"); return 1; }
    if (part == "range") PartRange();
    else if (part == "close") PartClose();
    else if (part == "mate") PartMate();
    else if (part == "group") PartGroup();
    else if (part == "flame") PartFlame(false);
    else if (part == "far") PartFlame(true);
    else if (part == "off") PartOff();
    else if (part == "drop") PartDrop();
    else if (part == "dodge") PartDodge();
    printf("    %s\n", vdoll::body::Status(0).c_str());
    return gFails;
}

int main(int argc, char** argv) {
    std::vector<std::string> maps, parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "range" || a == "close" || a == "mate" || a == "group" || a == "flame" || a == "far" || a == "off" || a == "drop" || a == "dodge") parts.push_back(a); else maps.push_back(a);
    }
    if (maps.empty()) { fprintf(stderr, "usage: vdoll_heavy_test <map.geom> ... [range|close|mate|group|flame|far|off|drop|dodge ...]\n"); return 2; }
    if (parts.empty()) parts = {"range", "close", "mate", "group", "flame", "far", "off", "drop", "dodge"};
    printf("controls %s, level %s\n", getenv("MGO1_VDOLL_CONTROLS") ? getenv("MGO1_VDOLL_CONTROLS") : "direct", getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10");
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
