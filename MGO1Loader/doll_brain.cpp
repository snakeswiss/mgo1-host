// The PLAYABLE DOLL's decisions (doll_int.h): what it looks at, whom it fights and how, where it goes
// (the orders, the objective of the rule, the items), the menus of a match (the spawn, the pause menu's
// TEAM SELECT, spectating while no real player is there). It plays with the pad only.
//
// Measured (doll_probe.cpp, docs/PLAYABLE_DOLL.md 3):
// - Square at 0x3F readies the weapon; at 0xFF for 5 frames or more it fires (a bullet at the 4th
//   frame; the Scorpion two in 8 frames); the first person view (R1) fires only with the weapon readied.
// - Weapon readied, the left stick at 0.55 or more turns the player toward its direction (slowly
//   walking); under 0.45 nothing happens. The bullet leaves the muzzle (0.45 m ahead, 0.2 m aside, 0.38 m
//   above the hips) along the facing; a wall in between stops it: the line of fire is checked.
// - First person view (R1 held): the left stick turns the view (right: the yaw goes down, up: the
//   pitch goes up); dead under 0.3, then faster the longer it is held (0.5: 1.75 deg in 12 frames,
//   1.0: 10.5 deg), and it runs on 4 to 8 frames after the stick is let go.
// - Circle close by (under 0.9 m) punches: 3000 of the stun gauge (30000) a hit.
#include "doll_int.h"
#include "doll_aim.h"
#include "ps2world.h"
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace doll {
namespace in {

// ---- the inventory. The weapon window's list (NewWeaponChangeLinear, its list made at 0x3720E0 of n011a):
// the local player's equipment [0x23A678], +0x5AC the weapons' count, +0x5B4 their codes (s16), each one
// listed when its state's +0x18 (its rounds) is not under 0; the item window's (0x370D00): +0x5B0 the
// items' count, +0x5C4 their codes, listed when their state's +0x18 is not 0. +0x5D4 the weapon in the
// hands, +0x5D6 the item. The states are the map's page's own (0x38 bytes a code, a table for the weapons
// and one for the items, each page at its own address): found in the page's code, in the function that
// gives a code's state (sltiu t7,a0,0x83 / lui t6,HI / mult / addiu t6,t6,LO / addiu v0,t7,0x158: the
// weapons; 0x61 and 0x1E00: the items). The page's header at 0x2DA680: its code from +0x10 to +0x14 ----
namespace {
uint32_t gStateBase[2] = {0, 0}, gStateKey = 0;
void FindStateBases() {
    const uint32_t key = R32(0x2DA684) ^ (R32(0x2DA694) << 1);
    if (key == gStateKey) return;
    gStateKey = key; gStateBase[0] = gStateBase[1] = 0;
    const uint32_t lo = R32(0x2DA690), hi = R32(0x2DA694);
    if (R32(0x2DA680) != 0x2DA680 || lo < 0x2DA680 || hi <= lo || hi - lo > 0x400000) return;
    for (uint32_t a = lo; a + 36 <= hi; a += 4) {
        const uint32_t w0 = R32(a);
        if (w0 != 0x2C8F0083 && w0 != 0x2C8F0061) continue;
        if (R32(a + 8) != 0x240F0038 || (R32(a + 12) >> 16) != 0x3C0E || R32(a + 16) != 0x008F7818 || (R32(a + 20) >> 16) != 0x25CE) continue;
        const uint32_t w8 = R32(a + 32);
        const uint32_t base = (R32(a + 12) << 16) + (uint32_t)(int32_t)(int16_t)(R32(a + 20) & 0xFFFF);
        if (w0 == 0x2C8F0083 && w8 == 0x25E20158) gStateBase[0] = base + 0x158;
        if (w0 == 0x2C8F0061 && w8 == 0x25E21E00) gStateBase[1] = base + 0x1E00;
        if (gStateBase[0] && gStateBase[1]) break;
    }
    if (gDebug) Log("inventory: the page's states at %08x (weapons) %08x (items)", gStateBase[0], gStateBase[1]);
}
}  // namespace
uint32_t EquipBase() { const uint32_t g = R32(0x23A678); return g >= 0x100000 && g < 0x2000000 ? g : 0; }
uint32_t WeaponState(int code) { FindStateBases(); return gStateBase[0] && code >= 0 && code < 0x83 ? gStateBase[0] + code * 0x38 : 0; }
uint32_t ItemState(int code) { FindStateBases(); return gStateBase[1] && code >= 0 && code < 0x61 ? gStateBase[1] + code * 0x38 : 0; }
const Inventory& Inv() {
    static Inventory inv;
    static long long at = -1;
    if (at == gFrame) return inv;
    at = gFrame;
    inv.ok = false; inv.weapons.clear(); inv.items.clear(); inv.weapon = inv.item = -1;
    for (int c = 0; c < 0x40; ++c) { inv.rounds[c] = -1; inv.count[c] = 0; }
    const uint32_t g = EquipBase();
    if (!g || !WeaponState(0) || !ItemState(0)) return inv;
    const int nw = R32(g + 0x5AC), ni = R32(g + 0x5B0);
    if (nw < 0 || nw > 8 || ni < 0 || ni > 8) return inv;
    for (int k = 0; k < nw; ++k) {
        const int c = R16s(g + 0x5B4 + 2 * k);
        if (c <= 0 || c >= 0x32) continue;
        const int r = R16s(WeaponState(c) + 0x18);
        inv.rounds[c] = r;
        if (r >= 0) inv.weapons.push_back(c);
    }
    for (int k = 0; k < ni; ++k) {
        const int c = R16s(g + 0x5C4 + 2 * k);
        if (c <= 0 || c >= 0x1A) continue;
        const int n = R16s(ItemState(c) + 0x18);
        inv.count[c] = n;
        if (n != 0) inv.items.push_back(c);
    }
    inv.weapon = R16s(g + 0x5D4); inv.item = R16s(g + 0x5D6);
    inv.ok = true;
    return inv;
}
bool OneLifeGuard(const float* item, float ring);    // (one-life defenders) the guard's holds and rounds (with the fight's moves)

namespace {

// ---- the skill of each tier (v141 beta 6: the ladder 1 TRAINING .. 10 PRO). The reaction: a player's, 0.67 s at 1 to 0.13 s at
// 5 (v137's first table, 0.4 s at 1, flattened the levels: level 1's K/D over level 5's), 0.08 s at 10; the tiers kept apart by
// the aim's error, the tolerance, the bursts, the eyes, and (6 up) the elite moves, the auto aim, the box pops, shorter holds.
// Rows 1 to 5 are beta 5's 1 to 5, row 6 v137's master tier (beta 5's NIGHTMARE), 7 to 10 new ----
struct Skill {
    int react;          // frames an enemy is in sight before the first shot (the band's middle: ReactFrames draws around it)
    float tolDeg;       // the facing's error allowed for a shot at 5 m (less farther away)
    int gap;            // frames between two bursts
    float view;         // how far it sees (mm)
    float fovDeg;       // half the angle it looks around its facing
    float fpv;          // the chance to aim in the first person view (the head) at range
    float cqc;          // the chance to punch an enemy that comes close
    float noiseDeg;     // its aim's own error
    int burst;          // frames of each burst (Square full)
    float hideLife;     // under this life it looks for food / cover
};
const Skill kSkill[10] = {
    {40, 5.0f, 30, 16000.f, 55.f, 0.00f, 0.25f, 7.0f, 8, 0.f},       // 1 TRAINING
    {28, 3.5f, 20, 24000.f, 65.f, 0.05f, 0.45f, 4.5f, 8, 50.f},      // 2 EASY
    {19, 2.6f, 13, 32000.f, 80.f, 0.20f, 0.60f, 2.5f, 12, 70.f},     // 3 NORMAL
    {12, 2.0f, 9, 40000.f, 95.f, 0.45f, 0.70f, 1.4f, 16, 90.f},      // 4 HARD
    {8, 1.6f, 6, 52000.f, 110.f, 0.70f, 0.80f, 0.6f, 20, 110.f},     // 5 EXPERT
    {8, 1.3f, 5, 52000.f, 110.f, 0.85f, 0.90f, 0.35f, 20, 120.f},    // 6 ELITE (v137's master tier)
    // (the release bench of 13 October, final1: PRO's tolerance 0.9 held its first pull back, sight to pull 76 frames within 10 m and
    // 108 at 10 to 25 m against ELITE's 48 and 38 and NORMAL's 38 and 52: the readied turn does not settle that fine, the old
    // audit's "experts waiting while the beginners fire". From 7 up the tolerance is ELITE's and a little more, their aim's own
    // error smaller: they pull sooner and still hit)
    {7, 1.30f, 4, 56000.f, 112.f, 0.85f, 0.92f, 0.30f, 22, 125.f},   // 7 VETERAN
    {6, 1.35f, 3, 60000.f, 114.f, 0.85f, 0.94f, 0.25f, 24, 130.f},   // 8 MASTER
    {6, 1.40f, 3, 62000.f, 116.f, 0.85f, 0.95f, 0.20f, 26, 135.f},   // 9 EXTREME
    {5, 1.45f, 2, 64000.f, 118.f, 0.85f, 0.96f, 0.15f, 28, 140.f},   // 10 PRO
};
const char* const kTierNames[10] = {"TRAINING", "EASY", "NORMAL", "HARD", "EXPERT", "ELITE", "VETERAN", "MASTER", "EXTREME", "PRO"};
// ---- (v141 beta 6) the live level: the difficulty set is its anchor (base); adaptive, its effective level moves over the
// whole ladder (within the server's bounds, adapt_min .. adapt_max) after the strength of its enemies AND of its own side:
// every 5 s of play with real players, each player's kills and deaths these 3 minutes and this minute (a real player's VS
// RATING, a rival's domination of the dolls), weighed by how much is known (a lucky streak of 3 kills moves little); the
// enemies' strength against its side's, and the match's stance (the tickets, the objectives, the rounds, the kills); a target
// level from them, approached a quarter level at a time (half a level when the gap is obvious; down a little slower), not at
// all within 0.3 of it. A spectator for want of a real player: nothing judged (the level kept) ----
float gLevel = -1.f;                    // the effective level (1 .. 10), -1 before the first frame
float gVsOffset = 0.f;                  // from the VS RATINGs (-0.75 .. 0.75), a part of the stance
long long gLevelAt = -100000;
int gLevelBase = -1;
std::string gLevelWhy = "even";
int gFoeNow = -1;                       // the foe being fought (the level against it)
struct AdaptState {
    float target = -1.f, enemy = 5.5f, ally = 5.5f, balance = 0.f, conf = 0.f, confE = 0.f, confA = 0.f;
    std::string reason = "even", trend = "flat";
    int tier = -1;                       // the tier last said (0 .. 9)
    bool elite = false;                  // the elite moves (beta 5's NIGHTMARE): from 6.0, off under 5.6
    long long changedAt = -100000;
};
AdaptState gAd;
// a player's VS RATING (its variable set's +0xC4: every machine has it; -1 not known). The game's own
// (n011a 0x3EDA00, at each kill): d = trunc(50 / (1 + 10^((killer's - victim's) / 1000))); the killer
// gains d (65535 at most), the victim loses d (0 at least); a new account has 1000
int VsRating(int id) {
    uint8_t v[0xC6];
    if (id < 0 || !ps2world::VariableSet(id + 1, v, sizeof v)) return -1;
    return v[0xC4] | (v[0xC5] << 8);
}
float FoeAdjust(int id) {               // a foe's play in this match and his rating: -0.5 (weak) .. +0.5 (strong)
    uint8_t v[0x20];
    if (id < 0 || !ps2world::VariableSet(id + 1, v, sizeof v)) return 0.f;
    const int k = v[0x14] | (v[0x15] << 8), d = v[0x16] | (v[0x17] << 8);
    float a = k + d < 3 ? 0.f : 0.35f * std::log2((k + 1.f) / (d + 1.f));   // (too few to judge: 0)
    const Player* p = PlayerById(id);
    const int r = p && !IsDollName(p->name) ? VsRating(id) : -1;
    if (r > 0) a += std::max(-0.25f, std::min(0.25f, (r - 1000) / 2000.f));
    return std::max(-0.5f, std::min(0.5f, a));
}
// ---- (v137) the rivals: each enemy real player's measures (its deaths by him, at one shot, how soon after seeing him, his
// hits on it), judged every 20 s: his domination (dom), a part of his strength in the live level (v141 beta 6). MGO1_DOLL_MASTER=1:
// level 6 at least (tests); =test: the other dolls measured as players (dolls-only benches) ----
struct Rival {
    std::string name;
    std::vector<std::array<int, 3>> ring;    // (second, his kills, his deaths) every 20 s, 4 minutes
    float m = 0.f, dom = 0.f;                // (v137's master amount, kept 0 since v141 beta 6); his domination judged
    long long presentAt = -1;
    int shots = 0, hits = 0;                 // his shots it heard, his hits on it
    int deaths = 0, oneShot = 0;             // its deaths by him, those at one shot (90 life and more at once: a headshot)
    std::vector<int> ttd;                    // frames from its sight of him to its death by him (the last 8)
};
std::map<int, Rival> gRivals;
int MasterMode() {                           // 0 production, 1 forced (tests), 2 test (the dolls measured too)
    static int m = -1;
    if (m < 0) { const char* e = getenv("MGO1_DOLL_MASTER"); m = !e ? 0 : !strcmp(e, "test") ? 2 : atoi(e) > 0 ? 1 : 0; }
    return m;
}
int BaseLevel() { return std::max(1, std::min(10, gSet.difficulty)); }
// ---- (v150-host beta 16) the parity mode (MGO1_DOLL_PARITY=vdoll, the ini's parity=vdoll; docs/PLAYABLE_DOLL.md 25): difficulty N
// plays by the VIRTUAL DOLL's level N, vdoll_body.cpp's kLevel copied here (keep the two alike): its sight and field (within 6 m
// all around), its reaction with its look's lateness (it looks every 100 ms: 0 to 5 frames), its aim's settling on him before a
// fight's first shot, its aim's error, its share of head aims (here the first person view's share), its bursts and pauses. What
// stays the game's: the turn (readied 3 to 16 degrees a frame, the view 1 to 2: under every level's own), the readied flag, the
// view's entry, the bullet at Square full's 4th frame (kParityBullet: taken off its reaction, so its first bullet never leaves
// before the virtual doll's would), the recoil. The live level off (doll.cpp). Off: nothing here is read ----
struct VdollLevel { int react; float err, sees, field, head, turn; int burst, pause; float tol; int settle; };
const VdollLevel kVdollLevel[10] = {
    {54, 7.0f, 16, 110, 0.00f, 3.f, 3, 40, 4.0f, 60},
    {38, 4.5f, 24, 130, 0.00f, 4.f, 3, 34, 3.5f, 52},
    {26, 2.5f, 32, 160, 0.00f, 5.f, 4, 28, 3.0f, 45},
    {16, 1.4f, 40, 190, 0.20f, 6.f, 4, 22, 2.5f, 38},
    { 9, 0.6f, 52, 220, 0.35f, 8.f, 5, 16, 2.0f, 32},
    { 8, 0.5f, 56, 230, 0.50f, 9.f, 5, 14, 1.7f, 26},
    { 7, 0.4f, 60, 240, 0.60f, 10.f, 6, 12, 1.4f, 20},
    { 6, 0.3f, 64, 250, 0.75f, 13.f, 6, 10, 1.2f, 12},
    { 4, 0.2f, 72, 260, 0.85f, 18.f, 8, 7, 1.0f, 6},
    { 3, 0.1f, 85, 280, 0.95f, 26.f, 10, 4, 0.7f, 2},
};
const int kParityBullet = 3;            // (probe 10: the bullet at Square full's 4th frame, the virtual doll's the frame it decides)
const VdollLevel& Vd() { return kVdollLevel[BaseLevel() - 1]; }
int AdaptLo() { return std::max(1, std::min(10, std::min(gSet.adaptMin, gSet.adaptMax))); }
int AdaptHi() { return std::max(1, std::min(10, std::max(gSet.adaptMin, gSet.adaptMax))); }
// its own small offset (the doll's number: -0.24 .. +0.24): the dolls of a side not all alike
float DollOffset() { return gSet.number > 0 ? ((gSet.number * 7) % 5 - 2) * 0.12f : 0.f; }
// the effective level without the foe's part (its eyes, its moves): the live level, else the difficulty set
float EffLevel() {
    const float base = (float)BaseLevel();
    float lv = !gSet.adapt ? base : gLevel < 0 ? std::max((float)AdaptLo(), std::min((float)AdaptHi(), base)) : gLevel;
    if (gSet.adapt) lv = std::max((float)AdaptLo(), std::min((float)AdaptHi(), lv + DollOffset()));
    if (MasterMode() == 1) lv = std::max(lv, 6.f);
    return std::max(1.f, std::min(10.f, lv));
}
// (v141 beta 6) the elite tier's moves (beta 5's NIGHTMARE: the box to get away, the quick aim, the lean before his shots, the
// taps, the box runner): the effective level 6 and up (off under 5.6: no flicker at the edge)
bool Nightmare() { return gAd.elite; }
// (v141 beta 2) NIGHTMARE's quick aim (FightFrame); MGO1_DOLL_QUICKAIM=0 turns it off (the benches' comparisons)
bool QuickAim() { static int q = -1; if (q < 0) { const char* e = getenv("MGO1_DOLL_QUICKAIM"); q = e && *e == '0' ? 0 : 1; } return q == 1; }
// (v141 beta 3) NIGHTMARE's third round: the gun and the box by a tap, no suppressor, no move in the open under his aim, the
// box runner. MGO1_NM_V3=0: beta 2's (the A/B benches)
bool NmV3() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_NM_V3"); v = e && *e == '0' ? 0 : 1; } return v == 1 && Nightmare(); }
Rival& RivalOf(const Player& p) {
    Rival& r = gRivals[p.id];
    if (r.name != p.name) { r = Rival(); r.name = p.name; }   // (another player in that slot)
    return r;
}
float MasterOf(int id) {
    if (MasterMode() == 1) return 1.f;
    const auto it = gRivals.find(id);
    return it == gRivals.end() ? 0.f : it->second.m;
}
// the level now: the effective level, and against the one fought his own play (a third of a level either way): the global
// level is the match's, the foe's part a tactical touch
float LevelNow() {
    float lv = EffLevel();
    if (gSet.adapt && gFoeNow >= 0) lv += 0.6f * FoeAdjust(gFoeNow);
    if (gSet.adapt) lv = std::max((float)AdaptLo(), std::min((float)AdaptHi(), lv));
    return std::max(1.f, std::min(10.f, lv));
}
float LevelAgainst(int id) { const int keep = gFoeNow; gFoeNow = id; const float lv = LevelNow(); gFoeNow = keep; return lv; }
bool MasterVs(int id) { return id >= 0 && LevelAgainst(id) >= 5.5f; }   // ((v137) the master tier against him: its moves)
bool MasterFight() { return LevelNow() >= 5.5f; }                      // ((v137) in a fight: against the one fought)
bool MasterStance() {                                   // ((v137) the elite moves against an enemy present)
    if (MasterMode() == 1 || EffLevel() >= 5.5f) return true;
    for (const auto& kv : gRivals) if (gFrame - kv.second.presentAt < 1500 && MasterVs(kv.first)) return true;
    return false;
}
Skill gSkNow;
float gSkLevel = -1.f;
Skill SkillAt(float lv) {               // the table's rows taken in proportion (6.7: 70 % of VETERAN, 30 % of ELITE)
    const int a = std::max(0, std::min(9, (int)std::floor(lv) - 1)), b = std::min(9, a + 1);
    const float t = std::max(0.f, std::min(1.f, lv - 1.f - a));
    const Skill &x = kSkill[a], &y = kSkill[b];
    auto li = [t](int p, int q) { return (int)std::lround(p + (q - p) * t); };
    auto lf = [t](float p, float q) { return p + (q - p) * t; };
    Skill r = {li(x.react, y.react), lf(x.tolDeg, y.tolDeg), li(x.gap, y.gap), lf(x.view, y.view), lf(x.fovDeg, y.fovDeg), lf(x.fpv, y.fpv),
               lf(x.cqc, y.cqc), lf(x.noiseDeg, y.noiseDeg), li(x.burst, y.burst), lf(x.hideLife, y.hideLife)};
    if (Parity()) {                     // ((v150-host beta 16) the virtual doll's level of that number: its eyes, error, head share, bursts)
        const VdollLevel& v = kVdollLevel[std::max(1, std::min(10, (int)std::lround(lv))) - 1];
        r.react = std::max(1, v.react + 3 - kParityBullet); r.view = v.sees * 1000.f; r.fovDeg = v.field * 0.5f; r.fpv = v.head;
        r.noiseDeg = v.err; r.burst = 6 * v.burst - 1; r.gap = v.pause;   // (a bullet every 6 frames from the 4th: n bullets in 6n - 1 frames)
    }
    return r;
}
const Skill& Sk() {                     // the skill of the level now, between the table's rows
    const float lv = LevelNow();
    if (lv != gSkLevel) { gSkLevel = lv; gSkNow = SkillAt(lv); }
    return gSkNow;
}
int Lv() { return (int)std::lround(LevelNow()); }      // the level as a whole number (the choices made by level)
// what it sees (the view's distance, the field) at its effective level without the foe's part: the foe fought changes its
// aim, not its eyes (the audit: an enemy at the field's edge seen, fought, the level down a little by the foe, not seen, the
// fight dropped, seen again: a fight begun and dropped every frame)
void SeeSkill(float* view, float* fovDeg) {
    const Skill s = SkillAt(EffLevel());
    *view = s.view; *fovDeg = s.fovDeg;
}
// ---- (v141 beta 7) tier 10 the cheated level (Snake's choice of 14 October: superhuman reflexes, maximum aggressiveness, it knows
// where its enemies are; tiers 1 to 9 unchanged and human-like). On while the live level without the foe's part is at tier 10: from
// 9.75, off under 9.5 (CheatFrame): against beginners the live level comes down and it plays human. One line at each change
// ("DOLL CHEAT: on|off (...)"). MGO1_DOLL_CHEAT=0: tier 10 plays as beta 6's PRO; MGO1_DOLL_CHEAT_AMBUSH=0: no ambush in the first
// person view (the readied pre-aim only) ----
struct CheatState {
    bool on = false;
    long long at = -1;                   // on or off since
    int ons = 0;                         // times on this run
    int preFor = -1;                     // the enemy its gun waits for (CheatPreAimFrame), -1 none
    const char* preWhy = "";             // his_corner, his_line, his_spawn_wait
    bool preView = false;                // that wait in the first person view (its ambush)
    long long viewAt = -1;               // its ambush's R1 since
    int huntId = -1;                     // the enemy it hunts (CheatHunt)
    long long huntAt = -100000;          // (its choice made again every half second)
    long long reloadAt = -1;             // (CheatReloadPress) its press cycle's start
    long long topUpAt = -1;              // (CheatTopUp) firing its magazine out since
    long long minOn = 0, minFrames = 0;  // this minute's frames on, frames
    int preAims = 0, hunts = 0, ambushes = 0;   // this minute's waits, enemies hunted, fights begun in its ambush's view
    int preAimsAll = 0;                  // (the status)
    std::vector<int> pulls;              // this minute's first pulls: frames from the sight
    bool cornerHave = false; float corner[3] = {0, 0, 0};   // the last corner found (CheatCorner)
    long long waitAt = -1, waitLast = -100, noWaitAll = -1;   // ((v141 beta 10) the wait since, its last frame; no wait at all until
    std::map<int, long long> noWait;     // ((v141 beta 10) no wait for him until that frame)
    float lastErr = 0.f; long long lastErrAt = -100; int lastErrId = -1;   // (the third person's crossing: the error the frame before)
    long long shoulderSince = -1;        // (its ambush: a shoulder of his in the view since: his head awaited 20 frames at most)
    std::map<int, long long> dollFoeUntil;   // ((v141 beta 8) its focus: another doll fought back until (he hurt it))
    bool focus = false;                  // ((v141 beta 8) its focus on the real players on now (the line at each change))
    int focusSkips = 0;                  // (this minute's frames with another doll in sight left alone for them)
};
CheatState gCheat;
bool CheatAllowed() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_CHEAT"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
bool CheatOn() { return gCheat.on; }
bool CheatAmbushOn() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_CHEAT_AMBUSH"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
float CheatViewFrom() { return 10000.f; }           // (the third person within it: the auto aim's reach)
// ((v141 beta 10) MGO1_DOLL_CHEAT_WAIT=b8: beta 8's waits (40 m, its own walk counted as him coming, no time budget), for A/B benches
bool CheatWaitsB8() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_CHEAT_WAIT"); v = e && !strcmp(e, "b8") ? 1 : 0; } return v == 1; }
// its waits for a coming one: 25 m ((v141 beta 10); beta 8: 40), 12 m in the objective rules; its peek to 40 m
const float kCheatWaitFor = 25000.f;                 // (an enemy coming within it out of sight: its gun waits where he will show)
const float kCheatPeekFor = 40000.f;
float CheatWaitRange() { return CheatWaitsB8() ? 40000.f : (gRule >= 2 && gRule <= 4) || gRoom.rule >= 5 ? 12000.f : kCheatWaitFor; }
// ((v141 beta 10) no cardboard box at all for tier 10 nor a doll set to 10 (its moves in it, the escapes, the sneaks, the fall backs)
bool NoBoxTier() { return !CheatWaitsB8() && (gCheat.on || BaseLevel() >= 10); }
const float kCheatFarShot = 12000.f;                 // ((v141 beta 8) from it, a bullet a pull, the view steady on him)
// ---- (v141 beta 8) tier 10's focus, MGO1_DOLL_CHEAT_FOCUS=1 only: in a deathmatch with a real player, the real players first.
// Another doll is fought only when he hurts it (HitBy: then 5 s), or is the one ordered; its hunt and its waits are for the real
// players alone. Elsewhere (teams) a real enemy first at equal threat. Off by default: everyone alike, as beta 7 (Snake's call of
// 14 October: a deathmatch is everyone against everyone, the dolls fight one another) ----
bool CheatFocusAllowed() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_CHEAT_FOCUS"); v = e && *e == '1' ? 1 : 0; } return v == 1; }
bool RealFoe(const Player& p) { return !p.local && p.id != gMyId && !IsDollName(p.name) && !IsDollUid(ps2world::PlayerUserId(p.id)); }
// ((v141 beta 8) the name of one in its new lines: a real player's own, a doll's slot only (an account doll's name may be its account's
// log-in: never written by new code); one word, as a name)
std::string LogWho(const Player& p) { return RealFoe(p) ? p.name : "doll@" + std::to_string(p.id); }
bool EnemySide(const Player& p);
// a real player among its enemies, looked at twice a second; kept 20 s after the last one seen (the players' list holds the bodies in the
// game: one waiting for his respawn is not in it; the bench b8_focus: the focus off and on again at each of his deaths)
bool CheatFocus() {
    static long long at = -100, realAt = -100000;
    if (!CheatOn() || !CheatFocusAllowed()) return false;
    if (gFrame - at >= 30 || gFrame < at) {
        at = gFrame;
        for (const Player& p : gPlayers) if (EnemySide(p) && RealFoe(p)) { realAt = gFrame; break; }
    }
    return gFrame - realAt < 1200;
}
bool CheatFocusDm() { return gRule == 0 && CheatFocus(); }   // (the deathmatch: the other dolls left alone)
// ((v141 beta 8) one at his spawn point fought once his gun is readied there; MGO1_DOLL_CHEAT_SPAWN=0: only once he hurts it, as beta 7)
bool CheatSpawnArmed() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_CHEAT_SPAWN"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
// ((v141 beta 8) its peek (CheatPeekFrame): the one peeked at, the lean's side, its stop and its lean since, a place given up until)
struct CheatPeek { int id = -1, side = 0; long long at = -1, leanAt = -1, offUntil = -100, viewAt = -1; float spot[3] = {0, 0, 0}; int peeks = 0, sprung = 0; };
CheatPeek gPk;
void CheatPeekEnd(const char* why) {
    if (gPk.id >= 0 && gDebug) Log("cheat: peek over (%s, %lld frames)", why, gFrame - gPk.at);
    if (gPk.id >= 0 && strcmp(why, "in sight")) { gPk.offUntil = gFrame + 180; }
    gPk.id = -1; gPk.side = 0; gPk.at = gPk.leanAt = gPk.viewAt = -1;
}
// another doll left alone for the real players: not him hurting it a moment ago (then fought 5 s), not the one ordered
bool HitBy(const Player& p);                     // ((v141 beta 7) hurt a moment ago by him, his gun up on it)
bool CheatFocusSkips(const Player& p) {
    if (!CheatFocusDm() || RealFoe(p)) return false;
    if (gSet.order == "target" && !gSet.target.empty() && PlayerByName(gSet.target) == &p) return false;
    if (HitBy(p)) gCheat.dollFoeUntil[p.id] = gFrame + 300;
    const auto it = gCheat.dollFoeUntil.find(p.id);
    return it == gCheat.dollFoeUntil.end() || gFrame >= it->second;
}
// ---- (v141 beta 6) the reactions, by family: a draw in the band of the level now (bounded, never zero), its middle the table's
// reaction for perception; the others a share of it (being hit: faster; an objective's change: slower). Frames. (v141 beta 7) Tier
// 10's cheat: 1 frame, every family (the other tiers keep their 2 frames at least) ----
enum ReactFamily { RF_PERCEPTION, RF_COMBAT, RF_MOVEMENT, RF_DEFENSE, RF_TARGET_SWITCH, RF_WEAPON_SWITCH, RF_FPV_ABORT, RF_OBJECTIVE, RF_AUTOAIM, kReactFamilies };
const char* const kReactNames[kReactFamilies] = {"perception", "combat", "movement", "defense", "target_switch", "weapon_switch", "fpv_abort", "objective", "autoaim"};
const float kReactShare[kReactFamilies] = {1.0f, 0.9f, 1.4f, 0.6f, 1.2f, 1.0f, 0.9f, 3.0f, 0.8f};
// ((v150-host beta 16) the parity mode, before the cheat's: the virtual doll's reaction (seen again: half of it, as its own) and its look's
// lateness, 0 to 5 frames; the families that lead to a shot less the doll's bullet's 3 frames; the auto aim's none (the settling's wait
// on him, ParitySettled, is the virtual doll's); 1 frame at least)
bool ParityShotFamily(int family) { return family == RF_PERCEPTION || family == RF_COMBAT || family == RF_DEFENSE || family == RF_TARGET_SWITCH; }
float ParityReact(int family, float late) {
    if (family == RF_AUTOAIM) return 1.f;
    const float r = (family == RF_COMBAT ? Vd().react * 0.5f : Vd().react * (ParityShotFamily(family) ? 1.f : kReactShare[family])) + late;
    return std::max(1.f, r - (ParityShotFamily(family) ? (float)kParityBullet : 0.f));
}
float ReactMid(int family) { return Parity() ? ParityReact(family, 2.5f) : CheatOn() ? 1.f : std::max(2.f, Sk().react * kReactShare[family]); }
int ReactFrames(int family) {           // a draw: 0.8 to 1.3 of the middle, 2 frames at least (the cheat: 1)
    if (Parity()) return (int)std::lround(ParityReact(family, (float)(Rnd() % 6)));
    if (CheatOn()) return 1;
    return std::max(2, (int)std::lround(ReactMid(family) * (0.8f + 0.5f * Rnd01())));
}
// ---- (v141 beta 6, aim) the fights' own moves by tier, a table beside kSkill (its rows taken in proportion the same way): the game's
// auto aim (L1, probe 31's step a: AutoAimFrame), the box pops (BoxPopFrame), the holds' length, the target's switch. The auto aim's
// reach is the tier's own use of it, never past the range measured (10 m: the game's lock decides within it) ----
struct FightSkill {
    float aaUse;        // the chance a fight uses the auto aim (drawn once a fight)
    float aaReach;      // how far it uses it (mm)
    float aaCone;       // degrees off its facing it fires with L1 held and the game's lock on (the lock measured to 24)
    float boxPop;       // the chance a box pop's moment is taken (0: never)
    int boxGap;         // frames between two box pops at least
    float holdK;        // the holds, feints and watches: their length times this
    float switchMargin; // a new target's score better than the one fought's (already 3 m ahead: PickTarget) by this more (mm) to take it
};
const FightSkill kFight[10] = {
    {0.00f,     0.f,  0.f, 0.00f,    0, 1.00f,    0.f},   // 1 TRAINING
    {0.10f,  4000.f, 10.f, 0.00f,    0, 1.00f,    0.f},   // 2 EASY
    {0.35f,  5000.f, 12.f, 0.10f, 2400, 1.00f,  500.f},   // 3 NORMAL
    {0.60f,  7000.f, 15.f, 0.20f, 1800, 0.90f, 1000.f},   // 4 HARD
    {0.80f,  9000.f, 17.f, 0.30f, 1500, 0.80f, 1500.f},   // 5 EXPERT
    {0.90f, 10000.f, 19.f, 0.45f, 1200, 0.70f, 2000.f},   // 6 ELITE
    {0.95f, 10000.f, 20.f, 0.55f, 1000, 0.62f, 2500.f},   // 7 VETERAN
    {1.00f, 10000.f, 21.f, 0.65f,  900, 0.55f, 3000.f},   // 8 MASTER
    {1.00f, 10000.f, 22.f, 0.75f,  800, 0.48f, 3500.f},   // 9 EXTREME
    {1.00f, 10000.f, 22.f, 0.85f,  700, 0.40f, 4000.f},   // 10 PRO
};
FightSkill FightSkillAt(float lv) {
    const int a = std::max(0, std::min(9, (int)std::floor(lv) - 1)), b = std::min(9, a + 1);
    const float t = std::max(0.f, std::min(1.f, lv - 1.f - a));
    const FightSkill &x = kFight[a], &y = kFight[b];
    auto lf = [t](float p, float q) { return p + (q - p) * t; };
    return {lf(x.aaUse, y.aaUse), lf(x.aaReach, y.aaReach), lf(x.aaCone, y.aaCone), lf(x.boxPop, y.boxPop), (int)std::lround(lf((float)x.boxGap, (float)y.boxGap)),
            lf(x.holdK, y.holdK), lf(x.switchMargin, y.switchMargin)};
}
FightSkill gFkNow;
float gFkLevel = -1.f;
const FightSkill& Fk() {
    const float lv = LevelNow();
    if (lv != gFkLevel) { gFkLevel = lv; gFkNow = FightSkillAt(lv); }
    return gFkNow;
}
int gPrimaryNow = -1;                   // the primary taken on the weapons page (4 SVD, 5 MOSIN NAGANT: a sniper)
int gSecondaryNow = -1;                 // the secondary (0 M1911A1, 1 Mk22)
int gSupportNow = -1;                   // the support row's item
bool IAmSnake();
// the one-life modes (rescue; the team sneaking to come): defined with the objectives
bool OneLifeMode();
bool OneLifeAttacker();
bool OneLifeDefender();
bool gQuiet = false;                    // (one-life modes) this frame's moves walked, not run (BrainFrame)
int gFpvFor = -1;                       // (one-life attackers) the enemy whose fight opens in the first person view (the head)
bool Sniper() { return (gPrimaryNow == 4 || gPrimaryNow == 5) && !IAmSnake(); }   // (Snake's Mosin Nagant: no sniper's role)

// ---- the weapon in the hands (probe 13). The game's weapon codes (MGS3's list): the weapons page's
// primaries SCORPION 0xA, XM16E1 0xB, AK-47 0xC, M37 0xE, SVD 0xF, MOSIN NAGANT 0x10; the secondaries
// M1911A1 6, Mk22 5; the grenade 0x13. The weapon window (NewWeaponChangeLinear): R2 held opens it
// (+0x44 12; a tap, 4, takes the previous weapon of the list instead: a press is never let go early),
// +0x48 2 still / 6 the cursor moving, +0x50 the weapon held, +0x54 the one under the cursor (the list
// by code, round); left / right move the cursor; R2 let go takes it. A spawn's weapon is not always the
// primary (a sniper's is its handgun): the window says which ----
const int kPrimaryCode[6] = {0xA, 0xB, 0xC, 0xE, 0xF, 0x10};
const int kSecondaryCode[2] = {6, 5};
int gHand = -1;                                 // the weapon held (-1 not known: seen when the window opens)
long long gHandSetAt = -1000;                   // (a window took it: the game's word follows a few frames later)
int Hand() { const Inventory& v = Inv(); return v.ok && gFrame - gHandSetAt >= 20 ? v.weapon : gHand; }
int PrimaryCode() { return gPrimaryNow >= 0 && gPrimaryNow < 6 ? kPrimaryCode[gPrimaryNow] : -1; }
// the support row's items (probe 17): their weapon codes
// (the grenade, WP grenade, stun grenade, chaff grenade, smoke grenade, claymore, the book)
int kSupportCode[8] = {0x13, 0x14, 0x15, 0x16, 0x17, 0x1B, 0x1C, -1};
int SupportCode() { return gSupportNow >= 0 && gSupportNow < 8 ? kSupportCode[gSupportNow] : -1; }
int SecondaryCode() { return gSecondaryNow >= 0 && gSecondaryNow < 2 ? kSecondaryCode[gSecondaryNow] : -1; }
// the weapons had (the game's lists; without them, the weapons page's choice), their rounds. The game's
// weapon codes (its table in the page, 0x28 bytes a weapon): none 0, the knife 1, Mk22 5, M1911A1 6, EZ
// GUN 7, SAA 8, the FLAME THROWER 9, SCORPION 0xA, XM16E1 0xB, AK-47 0xC, M63 0xD, M37 0xE, SVD 0xF, MOSIN
// NAGANT 0x10, RPG-7 0x11, the torch 0x12, the grenades 0x13 to 0x17 (grenade, WP, stun, chaff, smoke),
// the empty magazine 0x18, TNT 0x19, C3 0x1A, the claymore 0x1B, the book 0x1C, the mousetrap 0x1D
std::vector<int> gMissing;                      // codes a window did not have this life (not asked again)
bool Missing(int c) { return std::find(gMissing.begin(), gMissing.end(), c) != gMissing.end(); }
bool HasWeapon(int c) {
    if (c < 0) return false;
    if (c > 0 && Missing(c)) return false;          // (the window went round without it: its rounds alone, no gun)
    const Inventory& v = Inv();
    if (v.ok) return c == 0 || (c < 0x40 && v.rounds[c] >= 0);
    return c == 0 || c == 1 || c == PrimaryCode() || c == SecondaryCode() || c == SupportCode();
}
int Rounds(int c) { const Inventory& v = Inv(); return v.ok ? (c >= 0 && c < 0x40 ? v.rounds[c] : -1) : HasWeapon(c) ? 99 : -1; }
// a gun with nothing left (no rounds, its magazine empty: the weapon's state +0x18, +0x1C)
bool Empty(int c) { const uint32_t st = WeaponState(c); return Inv().ok && Rounds(c) == 0 && (!st || R16s(st + 0x1C) <= 0); }
bool HasItem(int c) {
    if (c > 0 && Missing(0x100 + c)) return false;   // (the window went round without it)
    const Inventory& v = Inv();
    return v.ok ? c == 0 || (c > 0 && c < 0x40 && v.count[c] != 0) : true;
}
// the view-locking weapons (the first person view, the player still: SVD, MOSIN NAGANT, RPG-7), the
// flamethrower (a few metres, the player still while it fires)
bool LocksView(int c) { return c == 0xF || c == 0x10 || c == 0x11; }
// the fight's own gun: the weapons page's primary when it is had (not a sniper's rifle), else the best one
int MainGun() {
    const int p = PrimaryCode();
    if (!IAmSnake() && p > 0 && !LocksView(p) && HasWeapon(p) && !Empty(p)) return p;   // (an empty one: the next)
    static const int kPref[] = {0xC, 0xB, 0xD, 0xA, 0xE, 6, 5, 8, 7};
    for (int c : kPref) if (HasWeapon(c) && !Empty(c)) return c;
    for (int c : kPref) if (HasWeapon(c)) return c;
    return -1;
}
int Handgun() {
    const int s = SecondaryCode();
    if (!IAmSnake() && s > 0 && HasWeapon(s)) return s;
    for (int c : {6, 5, 8, 7}) if (HasWeapon(c)) return c;
    return -1;
}
int SniperRifle() {
    const int p = PrimaryCode();
    if ((p == 0xF || p == 0x10) && HasWeapon(p)) return p;
    for (int c : {0xF, 0x10}) if (HasWeapon(c)) return c;
    return -1;
}
bool SniperInHand() { const int h = Hand(); return h > 0 ? h == 0xF || h == 0x10 : Sniper(); }
// a sniper's rifle taken from the window turns its scope's view on by itself (state 0x83, probe 15): the
// player does not walk in it, the stick turns the view; only another weapon takes it off. A sniper walks
// with its handgun (the one it spawns with) and takes its rifle at its spot. (v141 beta 6) The RPG-7 too
bool FpvWeaponInHand() { const int h = Hand(); return h > 0 ? LocksView(h) : Sniper(); }
bool Scoped() { return (gMe.bits[0] & 0x80) != 0 && FpvWeaponInHand(); }
bool FpvExitWanted();                            // ((v141 beta 6) the FPV state machine wants out: the window's pauses short)
bool FpvWindowHeld(int code);                    // ((v141 beta 6) MGO1_DOLL_FPV_TEST=stuck: the brain's swaps out held)
// a window (0 the weapons: R2, NewWeaponChangeLinear; 1 the items: L2, NewItemChangeLinear, the same
// layout): its button held until it is open and still, the cursor moved right to the code, the button
// let go. True while it has the pad's button (no shot meanwhile)
struct Swap { int want = -1; long long start = -1, moveAt = -1000, doneAt = -1000, pressAt = -1; int moves = 0, presses = 0, lastOpen = -1, lastStill = -1, lastCur = -1, failed = -1, closed = 0, firstCur = -1; bool left = false, round = false; };
Swap gSw[2];
int gItemHeld = 0;                              // the item held (0 none, 0xE the cardboard box, 6 the scope)
long long gBoxTakenAt = -1000;                   // (the box last taken from the item window: BoxOn)
long long gBoxSeenAt = -1000;                    // (BoxOn's last look at the box's actor)
int FightTarget();                               // (the enemy fought now, -1 none)
int FightTarget();
bool NmV3();
bool WindowTo(int win, int code) {
    Swap& sw = gSw[win];
    // (a moment between two; (v141 beta 3) NIGHTMARE in a fight, half a second: the box just put on at the contact, a grenade
    // just taken, the gun came 1.5 s late, ftrace bench f8167 and f9748)
    // ((v141 beta 6) out of a first person view weapon for its state machine: 0.5 s)
    const long long pause = (NmV3() && FightTarget() >= 0 && sw.doneAt <= gFrame) || (win == 0 && FpvExitWanted()) ? 30 : 90;
    if (code < 0 || gFrame - sw.doneAt < pause) return false;
    if (win == 0 && sw.start < 0 && FpvWindowHeld(code)) return false;
    if (sw.start < 0 && RadioPressing()) return false;                   // (a radio call being made: after it)
    // (merge review) the radio's menu up: the weapon window does not open (R2 held for nothing): after its close (RadioPad)
    if (win == 0 && sw.start < 0) { if (const uint32_t rw = RadioHud(); rw && (R32(rw + 0x180) & 0x10)) return false; }
    if (win ? !HasItem(code) : !HasWeapon(code)) return false;           // (not had: never asked)
    if (sw.want != code) { sw = Swap(); sw.want = code; sw.start = gFrame; if (gDebug) Log("%s: to %x", win ? "items" : "weapons", code); }
    const uint32_t wl = ps2world::ActorByName(win ? "NewItemChangeLinear" : "NewWeaponChangeLinear");
    const uint32_t button = win ? 0x0100 : 0x0200;
    if (!wl || gFrame - sw.start > 300 || sw.moves > 10 || sw.round) {
        Log("%s: %x not taken (%s; the window %x %x, its cursor %x, %d moves, %d presses)", win ? "items" : "weapons", code, wl ? "not in the window" : "no window",
            sw.lastOpen, sw.lastStill, sw.lastCur, sw.moves, sw.presses);
        if (wl && sw.round) gMissing.push_back(win ? 0x100 + code : code);   // (round the whole list: not had this life)
        // (not again for 10 s; from a sniper's scope out of a fight, where the doll can do nothing else, in 2 s; (v141 beta 6)
        // from a first person view weapon to another one, in or out of a fight, in under a second: the state machine's way out)
        const bool scoped = win == 0 && Scoped() && FightTarget() < 0, fpvOut = win == 0 && LocksView(Hand()) && !LocksView(code);
        sw = Swap(); sw.doneAt = gFrame + (fpvOut ? 20 : scoped ? 30 : 600); sw.failed = code;
        return false;
    }
    // not open a second after the press (a scope's view: seen 16 times in 99 on v133's audits), or closed
    // again after a move (v134's audit: a sniper's R2 held 5 s on a closed window): let go 8 frames and
    // pressed again
    if (sw.pressAt < 0) { sw.pressAt = gFrame; ++sw.presses; }
    const int open = R16(wl + 0x44), still = R16(wl + 0x48), held = R16(wl + 0x50), cur = R16(wl + 0x54);
    sw.lastOpen = open; sw.lastStill = still; sw.lastCur = cur;
    sw.closed = open == 0 && still == 0 ? sw.closed + 1 : 0;
    if (sw.moves > 0 && sw.closed >= 3) { sw.moves = 0; sw.pressAt = gFrame - 61; }   // (closed 3 frames after a move: let go now)
    if (open != 0xC && sw.moves == 0 && gFrame - sw.pressAt > 60) {
        if (gFrame - sw.pressAt < 68) return true;                        // (let go)
        sw.pressAt = gFrame; ++sw.presses;
    }
    gPad.buttons |= button;                                               // held: the window
    if (gFrame - sw.moveAt < 4) { gPad.buttons |= 0x0020; return true; } // right, 4 frames
    if (open != 0xC || still != 2 || gFrame - sw.moveAt < 10) return true;
    if (sw.moves == 0) { if (win) gItemHeld = held; else if (gFrame - gHandSetAt >= 20) gHand = held; sw.firstCur = cur; sw.left = false; }  // the window open: what is held
    else if (cur != sw.firstCur) sw.left = true;
    else if (cur != code && (sw.left || sw.moves >= 3)) { sw.round = true; return true; }   // (back where it began, or never off it: the whole list)
    if (cur == code) {                                                    // let go: taken
        gPad.buttons &= ~button;
        if (gDebug) Log("%s: %x taken (%x held before, %d moves)", win ? "items" : "weapons", code, held, sw.moves);
        sw = Swap(); sw.doneAt = gFrame;
        if (win) { gItemHeld = code; if (code == 0xE) gBoxTakenAt = gFrame; } else { gHand = code; gHandSetAt = gFrame; }
        return true;
    }
    sw.moveAt = gFrame; ++sw.moves; gPad.buttons |= 0x0020;
    return true;
}
bool SwapTo(int code) { return WindowTo(0, code); }
bool ItemTo(int code) { return WindowTo(1, code); }
// ---- (v141 beta 3) the taps (MGO1_TAP_TEST, tests/bench/run_tap141.sh, 6 rounds of taps 2, 4 and 8 frames long): L2 tapped
// takes the box off 4 frames after it is let go (the item window: 46 frames), running in it too, and with no item puts it
// on again 10 frames after; R2 tapped changed nothing in 120 frames (the M1911A1, a stun grenade held): the weapon window
// only. Square in the box does nothing (60 frames). kTapR2: 0 not used, 1 the weapon held before, 2 the one before it in
// the list; kTapL2: 0 not used, 1 the item off and on again. NIGHTMARE only; a tap with no effect 3 times: not used any
// more this game ----
int kTapR2 = 0, kTapL2 = 1;
int gHandNow = -1, gHandPrev = -1, gItemNow = -1, gItemPrev = -1;  // what is held, what was held before (the inventory's words)
void HeldWatch() {
    const Inventory& v = Inv();
    if (!v.ok) return;
    if (v.weapon != gHandNow) { gHandPrev = gHandNow; gHandNow = v.weapon; }
    if (v.item != gItemNow) { gItemPrev = gItemNow; gItemNow = v.item; }
}
struct TapRun { int win = -1, want = -1, from = -1, left = 0; long long at = -100000; };
TapRun gTp;
int gTapFails[2] = {0, 0};
long long gTapNoHoldUntil = -1;                      // a tap of R2 wanted: BrainFrame's 24 frames not kept until
bool NmV3();
// the taps of R2 from the weapon held to this one (kTapR2 2: back along the list), -1 none
int TapsTo(int code) {
    const Inventory& v = Inv();
    if (!v.ok || code < 0 || v.weapon == code) return -1;
    if (kTapR2 == 1) return gHandPrev == code ? 1 : -1;
    if (kTapR2 != 2) return -1;
    int i = -1, j = -1;
    for (size_t k = 0; k < v.weapons.size(); ++k) { if (v.weapons[k] == v.weapon) i = (int)k; if (v.weapons[k] == code) j = (int)k; }
    if (i < 0 || j < 0) return -1;
    const int n = (int)v.weapons.size();
    return ((i - j) % n + n) % n;
}
// a tap toward this weapon (win 0) or the item off / this item (win 1): true while it has the pad (4 frames pressed, 26 more
// for the game's answer, then the next tap or the end); false: no tap (the window instead)
// the tap going on, every frame of the world (BrainBody, before the windows): its button its 4 frames, whatever the brain
// asks meanwhile (the callers' own rules stop asking: a box runner's tap was pressed one frame, v141 beta 3's first A/B),
// then the game's answer watched 40 frames: taken (the box's state at once, not at its next look) or no effect
bool TapDone(int win, int code, int now) { return win ? (code == 0 ? now != 0xE : now == code) : now == code; }
bool TapFrame() {
    if (gTp.win < 0 || gTp.want < 0) return false;
    const long long t = gFrame - gTp.at;
    if (t >= 0 && t < 4) { gPad.buttons |= gTp.win ? 0x0100 : 0x0200; gTapNoHoldUntil = gFrame + 30; return true; }
    const Inventory& v = Inv();
    const int now = v.ok ? (gTp.win ? v.item : v.weapon) : -1;
    if (v.ok && TapDone(gTp.win, gTp.want, now)) {
        if (gTp.win) { gItemHeld = now; gBoxSeenAt = gFrame; if (now == 0xE) gBoxTakenAt = gFrame; }
        if (gDebug) Log("tap: %s %x taken in %lld frames", gTp.win ? "L2" : "R2", now, t);
        gTp = TapRun();
        return false;
    }
    if (v.ok && now != gTp.from && gTp.left > 0) { --gTp.left; gTp.from = now; gTp.at = gFrame; return TapFrame(); }   // (the next tap)
    if (t >= 40) {
        ++gTapFails[gTp.win];
        Log("tap: %s %x, no effect (holding %x)%s", gTp.win ? "L2 for the item" : "R2 for the weapon", gTp.want, now, gTapFails[gTp.win] >= 3 ? ": the windows only now" : "");
        gTp = TapRun();
    }
    return false;
}
// a tap toward this weapon (win 0) or the item off / this item (win 1): true while one for it goes on (pressed, its answer
// awaited); false: taken already, or no tap (the window instead)
bool TapTo(int win, int code) {
    if (!NmV3() || gTapFails[win] >= 3 || (win ? !kTapL2 : !kTapR2)) return false;
    if (win == 1 && (gMe.bits[0] & 0x80)) return false;            // (L2 in a first person view leans)
    if (RadioPressing()) return false;                              // ((merge review) a call's presses clear L2, R2: a tap then fails)
    const Inventory& v = Inv();
    if (!v.ok) return false;
    const int now = win ? v.item : v.weapon;
    if (gTp.win == win && gTp.want == code) return !TapDone(win, code, now);
    if (gTp.win >= 0 || TapDone(win, code, now)) return false;     // (another one going on; nothing to do)
    int n = 1;
    if (win == 0) { n = TapsTo(code); if (n < 1 || n > 2) return false; }
    else if (code == 0 ? now != 0xE : gItemPrev != code) return false;   // (L2: the box off; an item on: the one held before)
    gTp = TapRun(); gTp.win = win; gTp.want = code; gTp.from = now; gTp.left = n - 1; gTp.at = gFrame;
    if (gDebug) Log("tap: %s toward %x (holding %x, %d tap%s)", win ? "L2" : "R2", code, now, n, n > 1 ? "s" : "");
    TapFrame();
    return true;
}
// the moment between two windows (1.5 s after one was taken; a window given up, 10 s blocked, is not one)
bool WindowPause(int win) { const long long since = gFrame - gSw[win].doneAt; return since >= 0 && since < 90; }
bool WindowFree(int win) { return gFrame - gSw[win].doneAt >= 90; }   // (neither in that moment nor blocked)
// a window's work begun is finished before anything else (its button let go early would be a tap)
bool WindowBusy() {
    for (int w = 0; w < 2; ++w) if (gSw[w].want >= 0 && gSw[w].start >= 0) { WindowTo(w, gSw[w].want); return true; }
    return false;
}
// the cardboard box on (item 0xE: the actor NewNetworkCBox_NoBreak pointing at the player's work), checked
// every half second (the game takes it off too: a hit, a respawn); taken from the window a moment ago, its
// actor not there yet: on (v136's audit, a gun hold begun in a box just put on)
void FindMyBox(uint32_t w, const std::string& name, void* ctx) {
    if (name != "NewNetworkCBox_NoBreak") return;
    const uint32_t me = gMe.obj & 0x01FFFFFF;
    for (uint32_t o = 0x20; o < 0x100; o += 4) if (me && (R32(w + o) & 0x01FFFFFF) == me) { *(bool*)ctx = true; return; }
}
bool BoxOn() {
    if (gFrame - gBoxSeenAt >= 30) {
        bool on = false; ps2world::ForEachActor(&FindMyBox, &on);
        if (on) gItemHeld = 0xE; else if (gItemHeld == 0xE && gFrame - gBoxTakenAt > 45) gItemHeld = 0;
        gBoxSeenAt = gFrame;
    }
    return gItemHeld == 0xE;
}
constexpr float kDeg = 3.14159265f / 180.f;

// ---- the body of a player: its ground, its chest, its head ----
float GroundOf(const float* hips) {
    if (!MapReady()) return hips[1] - 800.f;
    const float g = gMap->nav->GroundBelow(hips, 2500.f);
    return g > -1e29f ? g : hips[1] - 800.f;
}
void BodyPoint(const float* hips, float above, float* out) {   // above: 0.45 the chest, 0.95 the head (of the hips' height)
    const float h = std::max(250.f, hips[1] - GroundOf(hips));   // 800 standing, ~500 crouched, ~300 lying
    if (h < 450.f) above *= 0.2f;                                // lying: the body along the ground, its middle
    out[0] = hips[0]; out[1] = hips[1] + h * above; out[2] = hips[2];
}
bool Lying(const float* hips) { return hips[1] - GroundOf(hips) < 450.f; }
bool InCrawl(const float* p) { return MapReady() && gMap->nav->AreaAt(p) == AREA_CRAWL; }   // (v137) a point in a crawl passage (a vent, under a truck)
// the head's middle (probe 27, Killhouse A, 6 m, the AK-47 in the first person view at a standing player:
// 0.65 and 0.725 m above his hips body hits, 30 a bullet; 0.8, 0.85 and 0.9 m a headshot, killed at once)
float gHeadAbove = 1.06f;                                    // of the hips' height above the ground (0.85 m standing)
// crouched (the state word 0: 5 still, 0x85 in the first person view): the hips' point stays where it was
// standing, the eye goes 0.54 m down (probe 26): the head and the chest aimed lower
bool CrouchedPlayer(const Player& p) { const uint32_t st = p.bits[0] & 0xFFFF; return (st == 5 || st == 0x85) && !Lying(p.pos); }
void HeadPoint(const Player& p, float* out) { BodyPoint(p.pos, gHeadAbove, out); if (CrouchedPlayer(p)) out[1] -= 540.f; }
void ChestPoint(const Player& p, float* out) { BodyPoint(p.pos, 0.45f, out); if (CrouchedPlayer(p)) out[1] -= 300.f; }
void MyEye(float* e) { BodyPoint(gMe.pos, 0.95f, e); }
void Muzzle(float* m) {                                      // 0.45 m ahead, 0.38 m above the hips
    const float f = Facing();
    m[0] = gMe.pos[0] + 450.f * std::sin(f); m[1] = gMe.pos[1] + 380.f; m[2] = gMe.pos[2] + 450.f * std::cos(f);
}
// the distance from p to the segment a-b (x, y, z)
float SegDist(const float* a, const float* b, const float* p) {
    float ab[3], ap[3];
    for (int k = 0; k < 3; ++k) { ab[k] = b[k] - a[k]; ap[k] = p[k] - a[k]; }
    const float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = l2 > 1.f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0.f;
    t = std::max(0.f, std::min(1.f, t));
    float d2 = 0;
    for (int k = 0; k < 3; ++k) { const float v = ap[k] - t * ab[k]; d2 += v * v; }
    return std::sqrt(d2);
}

// ---- who a player is: the character's type, a byte at +0xB62 of its work (the table of 0x401EB0 of n011a:
// its model; the voices' switch at 0x3FDB30): 0, 1, 2 the soldiers (their voice by their slot), 3 to 7
// the special characters (each its own voices: Raikov, Sokolov, Reiko, Rowdy...), 8 Snake. The sneaking
// mission's Snake is also the one whose life goes over 200 (320; the soldiers 180). The soldiers see him
// only close by (his camouflage) ----
std::map<int, int> gMaxLife;                    // player id -> the most life seen this round
int gMyMaxLife = 0;
void SeeLives() {
    for (const Player& p : gPlayers) if (p.id >= 0) { int& m = gMaxLife[p.id]; m = std::max(m, p.life); }
    gMyMaxLife = std::max(gMyMaxLife, gMe.life);
}
int TypeOf(uint32_t work) { return work ? R8(work + 0xB62) : -1; }
int MyType() { return TypeOf(gMe.obj); }
bool Special(int type) { return type >= 3 && type <= 7; }
bool IsSnake(const Player& p) {
    if (gRule != 4) return false;
    if (TypeOf(p.obj) == 8) return true;
    auto it = gMaxLife.find(p.id); return it != gMaxLife.end() && it->second > 200;
}
bool IAmSnake() { return gRule == 4 && (MyType() == 8 || gMyMaxLife > 200); }
// the CQC throw: Snake's, and the special characters' who have it (Raikov, Reiko, Rowdy): a special
// character tries it, and gives up when it never works. The type 3 has the stealth camouflage in its items
// (Sokolov: no CQC throw). Each type's own voices (0x3FDB30): 3 the set 8, 4 9, 5 0xA, 6 0xB, 7 0xC, Snake 0xE
int gCqcDone = 0, gCqcFailed = 0;               // (this map) throws that worked, tries that did not
bool HasItem(int c);
bool CqcThrower() {
    if (IAmSnake()) return true;
    const int t = MyType();
    if (!Special(t) || (t == 3 && HasItem(0x17) && gCqcDone == 0)) return false;
    return gCqcDone > 0 || gCqcFailed < 6;
}

// ---- what it knows of the enemies ----
struct Known { float pos[3] = {0, 0, 0}, seenPos[3] = {0, 0, 0}; long long first = -1, last = -100000, seen = -100000, heard = -100000; bool visible = false;   // (last: seen or heard; seen: seen; heard: its steps; pos: where it was last seen or heard, seenPos seen)
               float peek[3] = {0, 0, 0}; long long peekAt = -100000, shotAt = -100000; float shotFrom[3] = {0, 0, 0}; const char* by = "body"; };   // (v137) his last shot's muzzle seen (a lean: Sees), his last shot heard; how seen
std::map<int, Known> gKnown;                    // by player id
int gLastLife = 200;
long long gHurtAt = -100000;
int gLastStun = -1;                              // its stamina the frame before (-1 not yet: a new life)
long long gStunHitAt = -100000;                  // its stamina last hit (punches, a CQC, a dart: its life untouched)
std::vector<long long> gHits;                    // the frames it was hit (the last 16: the fight's moves)
struct PreAim { float at[3] = {0, 0, 0}; long long until = -1; bool fpv = false; };
PreAim gPre;                                     // a corner held, an enemy come into sight: the gun on him (FightFrame)
int FightTarget();                               // (the enemy fought now, -1 none: after the fight's state)
// an enemy's steps are heard: walking or running (20 mm a frame and more: not still, not crawling nor
// lying), on the doll's floor (the audit: an enemy heard through a floor, still in his box, was gone to)
std::map<int, std::array<float, 3>> gPrevPos;
std::map<int, float> gStepSpeed;                 // each enemy's move a frame (mm), smoothed
bool Audible(const Player& p) {
    const auto it = gStepSpeed.find(p.id);
    return it != gStepSpeed.end() && it->second >= 20.f && !Lying(p.pos) && std::fabs(p.pos[1] - gMe.pos[1]) < 2500.f;
}
bool Blind();
bool ThroughSmoke(const float* a, const float* b);
bool GogglesOn();
std::map<int, long long> gClearAt;               // an enemy last seen with no smoke in between
// an enemy in his cardboard box (the actor NewNetworkCBox_NoBreak pointing at his work: every machine has it,
// probe 31): standing still farther than 3 m and a little more with the level, overlooked (v136) unless seen
// a moment ago or hurting the doll; moving, a box is seen as he is
std::vector<uint32_t> gBoxActors;
long long gBoxActorsAt = -100;
void CollectBoxes(uint32_t w, const std::string& name, void* ctx) { if (name == "NewNetworkCBox_NoBreak") ((std::vector<uint32_t>*)ctx)->push_back(w); }
bool InBox(const Player& p) {
    if (gFrame - gBoxActorsAt >= 15) { gBoxActorsAt = gFrame; gBoxActors.clear(); ps2world::ForEachActor(&CollectBoxes, &gBoxActors); }
    const uint32_t o = p.obj & 0x01FFFFFF;
    if (!o) return false;
    for (uint32_t w : gBoxActors) for (uint32_t k = 0x20; k < 0x100; k += 4) if ((R32(w + k) & 0x01FFFFFF) == o) return true;
    return false;
}
// (v137) its eyes as they are: the view's in the first person view (a lean, crouched), else 0.76 m above the hips
// standing, 0.54 m lower crouched (probe 26; the hips' point stays): crouched behind a low wall it saw over it
void SeeEye(float* e) {
    float vm[16];
    if ((gMe.bits[0] & 0x80) && ps2world::ViewMatrix(vm)) { e[0] = vm[12]; e[1] = vm[13]; e[2] = vm[14]; return; }
    MyEye(e);
    const uint32_t st = gMe.bits[0] & 0xFFFF;
    if ((st == 5 || st == 4 || st == 0x4004 || st == 0x84) && !Lying(gMe.pos)) e[1] -= 540.f;
}
bool EdgePart(const Player& p, const float* from, const float* chest, bool body, float* out, int* kind);
const char* gSeesBy = "body";                    // (v137) how Sees saw him: body, edge (a shoulder), peek (his lean's head)
bool Sees(const Player& p) {
    if (Blind()) return false;                   // (a stun grenade's white)
    const float d = Dist2D(gMe.pos, p.pos);
    float view, fov;
    SeeSkill(&view, &fov);
    if (d > view) return false;
    if (!OldFight() && InBox(p)) {
        const auto sp = gStepSpeed.find(p.id);
        const auto kn = gKnown.find(p.id);
        const bool still = sp == gStepSpeed.end() || sp->second < 6.f, tracked = kn != gKnown.end() && gFrame - kn->second.seen < 120;
        if (still && !tracked && d > 3000.f + std::min(5, Lv()) * 800.f && gFrame - gHurtAt > 90) return false;   // ((v137) the master tier: level 5's eyes)
    }
    if (IsSnake(p) && d > 3000.f + std::min(5, Lv()) * 1500.f && gFrame - gHurtAt > 90) return false;   // camouflaged
    // (in the first person view the eyes are the view's: a corner held looks at it whatever the body faces)
    float look = Facing(), vm[16];
    if (!OldFight() && (gMe.bits[0] & 0x80) && ps2world::ViewMatrix(vm)) look = std::atan2(vm[8], vm[10]);
    const float ang = std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - look));
    const bool hurt = gFrame - gHurtAt < 90;       // shot at: it turns around
    // out of its field: not seen, but when it was shot at (it turns around), hears his steps within 6 m, or
    // is fighting him (its eyes on him: the first person view turns the view, not the body's facing)
    // ((v150-host beta 16) the parity mode: within 6 m all around, heard or not, as the virtual doll)
    if (ang > fov * kDeg && !hurt && p.id != FightTarget() && (d > 6000.f || (!Audible(p) && !Parity()))) return false;
    // (v137) its eyes as they are (SeeEye) and his posture's own points (a crouched one's head 0.54 m lower: behind a low
    // wall he was seen where his head would be standing); his chest and head hidden, what a player sees of him: his lean's
    // head by his last shot's muzzle (HearShots, these 45 frames), a shoulder past a corner's edge
    float eye[3], chest[3], head[3];
    SeeEye(eye); ChestPoint(p, chest); HeadPoint(p, head);
    gSeesBy = "body";
    if (!LineClear(eye, chest) && !LineClear(eye, head)) {
        float part[3]; int kind = 1;
        if (OldFight() || !EdgePart(p, eye, chest, true, part, &kind)) return false;
        gSeesBy = kind == 2 ? "edge" : "peek";
        memcpy(chest, part, sizeof chest); memcpy(head, part, sizeof head);   // (the smoke's test on that part)
    }
    // a smoke in between (not through the thermal goggles): a shape within 4 m, else the enemy fought a
    // moment ago, 1.5 s after the smoke came between (shot at where it went, badly: FightFrame)
    if (!GogglesOn() && ThroughSmoke(eye, chest) && ThroughSmoke(eye, head)) {
        const auto it = gClearAt.find(p.id);
        const bool seen = d < 4000.f || (p.id == FightTarget() && it != gClearAt.end() && gFrame - it->second < 90);
        static long long logged = -1000;
        if (gDebug && !seen && gFrame - logged > 120) { logged = gFrame; Log("smoke: %s hidden by a smoke (%.1f m)", p.name.c_str(), d / 1000.f); }
        return seen;
    }
    gClearAt[p.id] = gFrame;
    return true;
}
// (v141 beta 7) tier 10's cheat: in sight with a clear line from its eye to his chest or head, 80 m at most, whatever its field, a
// flash, a smoke, his camouflage or his box (a shoulder past an edge, his lean's head by his shot: seen too); it never fights a wall
float CheatEnemyDist() {                             // the nearest alive enemy (straight)
    float d = 1e30f;
    for (const Player& p : gPlayers) if (Enemy(p)) d = std::min(d, Dist2D(gMe.pos, p.pos));
    return d;
}
bool CheatSees(const Player& p) {
    if (!p.alive || Dist2D(gMe.pos, p.pos) > (Parity() ? std::max(80000.f, Vd().sees * 1000.f) : 80000.f)) return false;   // ((v150-host beta 16) parity: its sight)
    float eye[3], chest[3], head[3];
    SeeEye(eye); ChestPoint(p, chest); HeadPoint(p, head);
    gSeesBy = "body";
    if (LineClear(eye, chest) || LineClear(eye, head)) return true;
    float part[3]; int kind = 1;
    if (!EdgePart(p, eye, chest, true, part, &kind)) return false;
    gSeesBy = kind == 2 ? "edge" : "peek";
    return true;
}
void Perceive() {
    SeeLives();
    if (gMe.life < gLastLife) { gHurtAt = gFrame; gHits.push_back(gFrame); if (gHits.size() > 16) gHits.erase(gHits.begin()); }
    gLastLife = gMe.life;
    if (gLastStun >= 0 && gMe.stun < gLastStun - 1000) gStunHitAt = gFrame;
    gLastStun = gMe.stun;
    for (const Player& p : gPlayers) {                   // (each enemy's steps: his move a frame)
        if (!Enemy(p)) continue;
        const auto it = gPrevPos.find(p.id);
        const float m = it != gPrevPos.end() ? Dist2D(it->second.data(), p.pos) : 0.f;
        float& sp = gStepSpeed[p.id];
        sp = m > 400.f ? 0.f : 0.8f * sp + 0.2f * m;     // (a jump: a respawn)
        gPrevPos[p.id] = {p.pos[0], p.pos[1], p.pos[2]};
    }
    for (auto& k : gKnown) k.second.visible = false;
    const bool cheat = CheatOn();
    for (const Player& p : gPlayers) {
        if (!Enemy(p)) continue;
        Known& k = gKnown[p.id];
        // ((v141 beta 7) tier 10's cheat: every alive enemy's place known every frame, his steps heard at any distance)
        if (cheat && p.alive) {
            memcpy(k.pos, p.pos, sizeof k.pos);
            k.last = gFrame;
            const auto sp = gStepSpeed.find(p.id);
            if (sp != gStepSpeed.end() && sp->second >= 20.f) k.heard = gFrame;
        }
        if (cheat ? CheatSees(p) : Sees(p)) {
            if (!k.visible && gFrame - k.seen > 30) k.first = gFrame;   // in sight again after a while: the reaction again (heard: all the same)
            k.visible = true; k.last = k.seen = gFrame; k.by = gSeesBy;
            memcpy(k.pos, p.pos, sizeof k.pos); memcpy(k.seenPos, p.pos, sizeof k.seenPos);
        } else if (gDebug >= 2 && Dist2D(gMe.pos, p.pos) < 15000.f && gFrame % 60 == 0) {
            float eye[3], chest[3], head[3];
            MyEye(eye); BodyPoint(p.pos, 0.45f, chest); BodyPoint(p.pos, 0.95f, head);
            Log("unseen %s at %.0f m: angle %.0f, eye %.0f %.0f %.0f, chest %.0f %.0f %.0f clear %d, head clear %d", p.name.c_str(), Dist2D(gMe.pos, p.pos) / 1000.f,
                std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - Facing())) / kDeg, eye[0], eye[1], eye[2], chest[0], chest[1], chest[2], LineClear(eye, chest), LineClear(eye, head));
        }
        if (!cheat && !k.visible && Dist2D(gMe.pos, p.pos) < 5000.f && Audible(p) && (gFrame % 15 == 0 || gFrame - k.last > 300)) {
            const float a = Rnd01() * 6.2831853f, r = Rnd01() * 1500.f;   // heard close by: where it is, 1.5 m about, not in sight
            k.pos[0] = p.pos[0] + std::sin(a) * r; k.pos[1] = p.pos[1]; k.pos[2] = p.pos[2] + std::cos(a) * r;
            k.heard = gFrame;
            if (gFrame - k.last > 300) k.last = gFrame - 200;
        }
    }
    for (auto it = gKnown.begin(); it != gKnown.end();) {
        const Player* p = PlayerById(it->first);
        if (!p || !Enemy(*p)) it = gKnown.erase(it); else ++it;
    }
}
// ---- the shots heard (v133, the audit: nothing known of an enemy but what is seen or heard). A bullet
// (NewBullet: where it left at +0xE0, probe 10) new since the last look, fired by an enemy (his hips
// within 1.5 m of where it left) within 35 m: where he is, 2 m about (the chase goes there) ----
void FindBullets(uint32_t w, const std::string& name, void* ctx) { if (name == "NewBullet") ((std::vector<uint32_t>*)ctx)->push_back(w); }
std::vector<uint32_t> gShotsSeen;
void HearShots() {
    if (gFrame % 2) return;                          // ((v137) every other frame: a lean's muzzle seen sooner)
    std::vector<uint32_t> now;
    ps2world::ForEachActor(&FindBullets, &now);
    const bool cheat = CheatOn();                    // ((v141 beta 7) tier 10's cheat: his shots heard at any distance, his place known)
    for (uint32_t w : now) {
        if (std::find(gShotsSeen.begin(), gShotsSeen.end(), w) != gShotsSeen.end()) continue;
        float o[3];
        for (int i = 0; i < 3; ++i) o[i] = RF(w + 0xE0 + 4 * i);
        if (!(std::fabs(o[0]) < 1e6f && std::fabs(o[2]) < 1e6f) || Dist2D(gMe.pos, o) > (cheat ? 1e6f : 35000.f)) continue;
        if (Dist2D(gMe.pos, o) < 1200.f && std::fabs(gMe.pos[1] - o[1]) < 1500.f) continue;   // ((v137) its own)
        for (const Player& p : gPlayers) {
            if (!Enemy(p) || !p.alive || Dist2D(p.pos, o) > 1500.f || std::fabs(p.pos[1] - o[1]) > 2000.f) continue;
            Known& k = gKnown[p.id];
            // (v137) his shot's start, exact (ShotAtFrame: where he shoots from); its muzzle in the doll's sight (its eyes,
            // its view's distance): his gun seen and his head by it (0.12 m up), what Sees and the aim take him by when his
            // chest and head are behind a wall (a lean, a head over a low wall)
            k.shotAt = gFrame; memcpy(k.shotFrom, o, sizeof k.shotFrom);
            ++RivalOf(p).shots;                          // ((v137) the master tier's measure: his shots heard)
            {
                float eye[3], view = 0.f, fov = 0.f; SeeEye(eye); SeeSkill(&view, &fov);
                if (cheat) view = 80000.f;
                const float pk[3] = {o[0], o[1] + 120.f, o[2]};
                if ((cheat || !Blind()) && Dist2D(gMe.pos, o) <= view && LineClear(eye, pk)) { memcpy(k.peek, pk, sizeof pk); k.peekAt = gFrame; }
            }
            if (!k.visible && !cheat) {
                const float a = Rnd01() * 6.2831853f, r = Rnd01() * 2000.f;
                k.pos[0] = o[0] + std::sin(a) * r; k.pos[1] = o[1]; k.pos[2] = o[2] + std::cos(a) * r;
                if (gFrame - k.last > 60) { k.last = gFrame; if (gDebug >= 2) Log("heard %s's shot %.0f m away", p.name.c_str(), Dist2D(gMe.pos, o) / 1000.f); }
            }
            break;
        }
    }
    gShotsSeen.swap(now);
}

// ---- what hides the enemies (v134, probe 31). A smoke grenade's cloud: the game's own table of the smokes
// out (the actor NewNetworkSmokeManager, 8 slots of 0x20 bytes from +0x40: the cloud's middle, where it went
// off, 1.0, its radius 4.5 m, its time left: 3250, 5 less a frame (11 s), -1 an empty slot),
// spread in its first second, thinner in its last two. A stun grenade's flash: the camera's white
// (NewFadeInOut, its colour +0x60 white, its alpha +0x58: 128 at the flash, then going down: 2 s at 2.5 m
// from it or turned away, 24 s looking at it through the first person view 7.5 m away): nothing seen while
// over 40. The thermal goggles (the item 7: a weapon box may give them) see through a smoke ----
int ItemInHands();
int Battery();
bool GogglesOn() { return ItemInHands() == 7 && Battery() > 0; }   // (their battery out: nothing seen through them)
struct Smoke { float c[3], r; };
std::vector<Smoke> gSmokes;
long long gSmokesAt = -1000;
void SeeSmokes() {
    if (gFrame - gSmokesAt < 15) return;
    gSmokesAt = gFrame; gSmokes.clear();
    const uint32_t m = ps2world::ActorByName("NewNetworkSmokeManager");
    if (!m) return;
    for (int k = 0; k < 8; ++k) {
        const uint32_t e = m + 0x40 + 0x20 * k;
        const int left = (int)R32(e + 0x14);
        const float r = RF(e + 0x10);
        if (left <= 0 || left > 20000 || !(r > 100.f && r < 20000.f)) continue;
        Smoke s;
        for (int i = 0; i < 3; ++i) s.c[i] = RF(e + 4 * i);
        if (!(std::fabs(s.c[0]) < 1e6f && std::fabs(s.c[1]) < 1e6f && std::fabs(s.c[2]) < 1e6f)) continue;
        const float age = (3250 - left) / 5.f;                       // frames since it went off
        s.r = r * std::max(0.3f, std::min(1.f, age / 60.f)) * std::min(1.f, left / 600.f);
        gSmokes.push_back(s);
        if (gDebug && age < 15.f) Log("smoke: a cloud at %.0f %.0f %.0f, %.1f m from the doll (%.1f s left)", s.c[0], s.c[1], s.c[2], Dist2D(s.c, gMe.pos) / 1000.f, left / 300.f);
    }
}
bool ThroughSmoke(const float* a, const float* b) {                  // the line a-b through a cloud
    SeeSmokes();
    for (const Smoke& s : gSmokes) if (SegDist(a, b, s.c) < s.r) return true;
    return false;
}
bool InSmoke(const float* p) { SeeSmokes(); for (const Smoke& s : gSmokes) { float d2 = 0; for (int i = 0; i < 3; ++i) d2 += (p[i] - s.c[i]) * (p[i] - s.c[i]); if (d2 < s.r * s.r) return true; } return false; }
float gWhite = 0.f; long long gWhiteAt = -1000;
void FindWhite(uint32_t w, const std::string& name, void* ctx) {
    float& a = *(float*)ctx;
    if (name == "NewFlushOnCamera") { a = std::max(a, 128.f); return; }   // (the flash itself, 16 frames)
    if (name != "NewFadeInOut" || (R32(w + 0x60) & 0xFFFFFF) != 0xFFFFFF) return;
    const float v = RF(w + 0x58);
    if (v == v && v <= 256.f && v > a) a = v;
}
float WhiteOut() {                                                   // the flash's white (0 none .. 128)
    if (gFrame - gWhiteAt >= 4) { gWhiteAt = gFrame; gWhite = 0.f; ps2world::ForEachActor(&FindWhite, &gWhite); }
    return gWhite;
}
bool Blind() { return WhiteOut() > 40.f; }
long long gBlindLogAt = -100000;
void ChestPoint(const Player& p, float* out);
void MyEye(float* e);
bool SmokeBetween(const Player& p) {                                 // a smoke between the doll and him (not seen through the goggles)
    if (GogglesOn()) return false;
    float eye[3], c[3]; MyEye(eye); ChestPoint(p, c);
    return ThroughSmoke(eye, c);
}

// ---- the fight ----
// its back pressed to a wall (the state word 0: 2 still, 0x4002 sliding along it, 4 and 0x4004 crouched,
// 0x82 and 0x84 in the wall's first person view)
bool AgainstWall() { const uint32_t st = gMe.bits[0] & 0xFFFF; return st == 2 || st == 0x4002 || st == 4 || st == 0x4004 || st == 0x82 || st == 0x84; }
// the way off the wall the doll stands against (radians): away from the nearest hit of 16 rays 1 m around
// its chest (false: none)
bool WallAway(float* angle) {
    if (!MapReady()) return false;
    const float c[3] = {gMe.pos[0], gMe.pos[1] + 100.f, gMe.pos[2]};
    float best = 2.f;
    for (int i = 0; i < 16; ++i) {
        const float a = i * 6.2831853f / 16.f, e[3] = {c[0] + std::sin(a) * 1000.f, c[1], c[2] + std::cos(a) * 1000.f};
        float t = 2.f, n[3];
        if (gMap->nav->FirstHit(c, e, &t, n) && t < best) { best = t; *angle = Wrap(a + 3.14159265f); }
    }
    return best < 1.f;
}
// a fight's short step (its way, radians; its length, mm): the map's ground along it and 0.4 m past it within a metre
// under the doll's own (no ledge's edge: two dolls hung from Graniny Gorki Lab's upper floor after the wall's steps)
bool StepGround(float angle, float len) {
    if (!MapReady()) return true;
    const float g0 = GroundOf(gMe.pos), sx = std::sin(angle), sz = std::cos(angle);
    for (float r = 250.f; r <= len + 400.f; r += 250.f) {
        const float p[3] = {gMe.pos[0] + sx * r, g0 + 700.f, gMe.pos[2] + sz * r};
        const float g = gMap->nav->GroundBelow(p, 1800.f);
        if (g < -1e29f || g0 - g > 1000.f) return false;
    }
    return true;
}
// the first of the ways (radians) whose ground holds a short step; false: a drop every way, no step
bool SafeStep(const float* ways, int n, float len, float* out, const char* what) {
    for (int i = 0; i < n; ++i)
        if (StepGround(ways[i], len)) {
            if (i && gDebug) Log("fight: %s: a drop that way, %.0f degrees off it instead", what, Wrap(ways[i] - ways[0]) / kDeg);
            *out = ways[i];
            return true;
        }
    if (gDebug) Log("fight: %s: a drop every way, no step", what);
    return false;
}
// a fight's short moves and turning pushes: a walk (the stick's larger axis at 0.7; 0.75 runs). v134's audit:
// run, as the stick's fix made every full push, they carried the doll into walls, its back pressed to them
// (in fights 5 to 50 times as often as v133)
const float kStep = 0.7f;
struct Fight {
    int target = -1;
    long long readySince = -1, fireUntil = -1, nextFire = 0;
    bool fpv = false; long long fpvSince = -1, onTargetSince = -1;
    bool cqc = false;
    float noise = 0; long long noiseAt = -1000;
    int shots = 0, lifeAt0 = -1, stunAt0 = -1;           // (the target's life and stamina when the fight began: the debug's summary)
    long long turnSince = -1; float turnFacing = 0;      // the facing watched while the stick turns it
    long long squareSince = -1;                          // Square held (readied or firing) since, to the frame before
    long long backUntil = -1, downSince = -1;            // the gun would not come up (a wall at its muzzle): stepping back
    float backDir = 0;                                   // (its way back, set once: pushed, the player turns to the stick)
    long long lyingBackSince = -1;                       // stepping back from an enemy lying close by since
    int dryShots = 0, dryLife = -1, dryStun = -1;        // shots since the target was last hurt, its life and stamina then
    bool holdFire = false;                               // (the target out of sight a moment: aimed, no shot)
    long long punchSince = -1, punchLast = -1;           // punching without the target hurt since, the last frame punching
    int punchLife = -1, punchStun = -1;                  // (its life and stamina the frame before)
    long long wallSince = -1; int wallSteps = 0;         // getting off a wall since, the tries (3 at most while on it)
    long long offWallSince = -1; float wallAway = 0;     // off the wall since (the tries anew after a second), away from it
    float wallDir = 0; bool wallGo = false;              // this step's way (its ground checked), or no step
    bool smoke = false;                                  // a smoke between it and the target (the log when it changes)
    int rollTry = -1;                                    // a roll before a CQC or stun attack: decided once a fight (-1 not yet)
    bool punchVain = false;                              // its punches found to do nothing to him (PunchesVain)
    long long walkSince = -1, walkLast = -100; float walkBest = 0;   // its guns empty: walking up to him since, last asked, the closest
    int wallRounds = 0;                                  // the wall's steps started again instead of its locked view (2 at most)
    bool wallAside = false;                              // the wall's second step went aside (the third one keeps that side)
    long long wallViewSince = -1;                        // in the wall's first person view with him far off it since
    long long r1HoldUntil = -1, wallViewOffSince = -1;   // the wall's view on, not asked: R1 held until (then let go); since
    long long startAt = -1;                              // the fight with this target begun
    long long downAt = -100000;                          // (v136) the target last seen down asleep
    long long finishSince = -1, finishOff = -1, finishLast = -100;   // (v136) walking up to it asleep since; not again until; its last frame
    long long viewOnAt = -1, armedAt = -1;               // (v137) this spell's view state on (0x80) since; the third person's readied flag since
    bool turning = false, fpvLater = false;              // (v137) the readied turn going on (hysteresis); the first person view after the turn
    int asideSide = 0; long long asideAt = -100000;      // (v137) the step aside's side, kept, and when it was chosen
    float noiseTo = 0;                                   // (v137) the aim's error drifts toward this one
    int rollDodge = -1;                                  // (v137) the master tier's roll under close fire: drawn once a fight
    long long reactAt = -1, reactBase = -1;              // (v141 beta 6, aim) the reaction's end (drawn from its family), the sight it was drawn for
    const char* reactFamily = "";                        // (v141 beta 6, aim) that family (the AI's lines)
};
Fight gFi;
long long gFightEndAt = -100000;
int FightTarget() { return gFi.target; }
// ---- (v141 beta 6) the decision of the frame and its reason (MGO1_DOLL_AI_DEBUG=1: "AI DOLL_n ... decision=... reason=..." when
// they change, and a minute's summary: the share of each decision while an enemy was in sight) ----
const char* gDecision = "";
const char* gReason = "";
long long gReasonLeft = 0;                       // (a wait's frames left, when it has some)
void Decide(const char* decision, const char* reason, long long left = 0) { gDecision = decision; gReason = reason; gReasonLeft = left; }
bool AiDebug() { static int d = -1; if (d < 0) { const char* e = getenv("MGO1_DOLL_AI_DEBUG"); d = e && *e && *e != '0' ? 1 : 0; } return d == 1; }
// (v137) one line per engagement for tests/bench/tools/engage_report.py (MGO1_DOLL_DEBUG): the frames from the first
// sight to the reaction, the gun ready (view on / readied), the aim on him, the first pull, the first hit, the end
struct Engage {
    int id = -1, weapon = -1, life0 = -1, lifeLast = -1, stunLast = -1, pulls = 0;
    long long sight = -1, start = -1, react = -1, ready = -1, aim = -1, shot = -1, hit = -1;
    float ang = 0, dist = 0, lv = 0;
    bool fpv = false;
    const char* seen = "body";
    int aaPulls = 0, boxPops = 0;                        // (v141 beta 6, aim) its pulls with the auto aim (L1), its box pops
    long long aaAt = -1;                                 // (v141 beta 6, aim) L1 first held
    long long hurt = -100000;                            // (v141 beta 7) its first hurt from 1 s before the sight
    const char* pre = "none";                            // (v141 beta 7) the fight begun with its gun waiting: aim (readied), view (its ambush)
};
Engage gEn;
void EngageEnd() {
    if (gEn.id < 0) return;
    const Player* p = PlayerById(gEn.id);
    const bool died = !gMe.alive || gMe.life <= 0, down = !p || !p->alive || p->life <= 0;
    auto rel = [](long long f) { return f >= 0 && gEn.sight >= 0 ? f - gEn.sight : -1LL; };
    // (v141 beta 2) against a real player always (the production logs: why the dolls lose to one). (v141 beta 6, aim) its pulls with
    // the auto aim, when L1 first held, its box pops: after the old fields (the reports' patterns unchanged). ((v141 beta 7) its first
    // hurt from 1 s before the sight, "hurt none" without: an enemy's hit before its first pull, seen first; its gun waiting or not)
    char hurt[24] = "none";
    if (gEn.hurt != -100000 && gEn.sight >= 0) snprintf(hurt, sizeof hurt, "%lld", gEn.hurt - gEn.sight);
    if (gDebug || (p && !IsDollName(p->name)))
        Log("engage: %s lv %.2f ang %.0f dist %.1f %s w %x seen %s start %lld react %lld ready %lld aim %lld shot %lld hit %lld end %lld %s pulls %d life %d -> %d autoaim %d at %lld box %d hurt %s pre %s",
            p ? p->name.c_str() : "?", gEn.lv, gEn.ang, gEn.dist / 1000.f, gEn.fpv ? "fpv" : "tpv", gEn.weapon, gEn.seen, rel(gEn.start), rel(gEn.react), rel(gEn.ready),
            rel(gEn.aim), rel(gEn.shot), rel(gEn.hit), rel(gFrame), died ? "died" : down ? "down" : "lost", gEn.pulls, gEn.life0, p ? p->life : -1, gEn.aaPulls,
            rel(gEn.aaAt), gEn.boxPops, hurt, gEn.pre);
    gEn = Engage();
}
// (v141 beta 7) tier 10's cheat: a fight's first pull, its frames from the sight (the AI minute's median)
void CheatPull() { if (CheatOn() && gEn.sight >= 0 && gCheat.pulls.size() < 200) gCheat.pulls.push_back((int)(gFrame - gEn.sight)); }
// (v141 beta 6, aim) the box pops' clock and tally (BoxPopFight, BoxApproachFrame, BoxPopOut, TacticLull's ambush): one line each
long long gBoxPopAt = -100000;                   // the last one (kFight's boxGap from it)
int gBoxOns = 0, gBoxOuts = 0;                   // (this minute's: AiTraceFrame; on, out at an enemy)
int gBpPendingId = -1; long long gBpPendingAt = -100000;   // (a pop out just before its fight's start: counted in that engagement)
const char* gBoxWhy = "";                        // (why its box went on last: break_los, approach; its pop out says it)
long long gBoxWhyAt = -100000;
bool BoxPopOn() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_BOXPOP"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
void BoxSay(const char* what, const char* why, const std::string& foe, float dist) {
    if (!strcmp(what, "on")) ++gBoxOns; else ++gBoxOuts;
    Log("DOLL BOX: %s (%s) %s %.1f m, level %.2f %s", what, why, foe.c_str(), dist / 1000.f, LevelNow(), TierName(LevelNow()));
}
void FightStop() {
    EngageEnd();
    if (gFi.finishSince >= 0) StopPath();                // (a walk up to a sleeping one: its path not left behind)
    if (gDebug && gFi.target >= 0 && gFi.shots > 0) {
        const Player* p = PlayerById(gFi.target);
        Log("fight: %s over: %d trigger pulls, its life %d -> %d, stamina %d -> %d%s", p ? p->name.c_str() : "?", gFi.shots, gFi.lifeAt0, p ? p->life : -1,
            gFi.stunAt0, p ? p->stun : -1, p && !p->alive ? " (dead)" : "");
    }
    gFi.shots = 0;
    gFoeNow = -1;
    gFi.target = -1; gFi.readySince = -1; gFi.fireUntil = -1; gFi.fpv = false; gFi.fpvSince = -1; gFi.cqc = false; gFi.onTargetSince = -1; gFi.turnSince = -1;
    gFi.squareSince = -1; gFi.lyingBackSince = -1; gFi.dryShots = 0; gFi.dryLife = gFi.dryStun = -1;
    gFi.punchSince = gFi.punchLast = -1; gFi.punchLife = gFi.punchStun = -1;
    gFi.wallSince = -1; gFi.wallSteps = 0; gFi.offWallSince = -1;
    gFi.smoke = false; gFi.rollTry = -1; gFi.punchVain = false; gFi.walkSince = -1; gFi.wallRounds = 0; gFi.wallViewSince = -1;
    gFi.downAt = -100000; gFi.finishSince = gFi.finishOff = -1;
    gFi.viewOnAt = gFi.armedAt = -1; gFi.turning = gFi.fpvLater = false; gFi.asideSide = 0; gFi.asideAt = -100000; gFi.rollDodge = -1;
    gFi.reactAt = gFi.reactBase = -1; gFi.reactFamily = "";
}
// punches that do nothing (v133's audit: a soldier punched a Snake 78 s, its stamina never down): 5 s of
// them without the target's life or stamina going down, nor the target down, the gun instead
bool PunchesVain(const Player& p) {
    const bool hurt = p.life < gFi.punchLife || p.stun < gFi.punchStun - 10 || p.down;
    if (gFi.punchSince < 0 || gFrame - gFi.punchLast > 30 || hurt) gFi.punchSince = gFrame;
    gFi.punchLast = gFrame; gFi.punchLife = p.life; gFi.punchStun = p.stun;
    if (gFrame - gFi.punchSince < 300) return false;
    if (gDebug) Log("fight: punches at %s for nothing 5 s: the gun", p.name.c_str());
    gFi.cqc = false; gFi.punchSince = -1; gFi.punchVain = true;
    return true;
}
// allies in the line of fire (between the muzzle and the target, within 0.7 m of it)
bool AllyInTheWay(const float* from, const float* to) {
    const float d = Dist2D(from, to);
    for (const Player& p : gPlayers) {
        if (!Ally(p) || !p.alive) continue;
        if (Dist2D(from, p.pos) > d + 500.f) continue;
        float c[3]; BodyPoint(p.pos, 0.45f, c);
        if (SegDist(from, to, c) < 700.f) return true;
    }
    return false;
}
// asleep or knocked out (the state word 0: 0x2000000 going down, 0x8000000 or 0x10000000 down; its
// stamina comes back slowly, it gets up when full): no threat. Snake leaves it; a stun weapon (the Mk22's darts, the Mosin
// Nagant) does nothing more to it
bool Asleep(const Player& p) { return (p.bits[0] & 0x1A000000) != 0; }
bool AsleepDown(const Player& p) { return (p.bits[0] & 0x18000000) != 0; }   // (down, not going down: a stumble passes)
int gLeaderTarget = -1;                         // the enemy the leader it follows shoots at (the radio's follow)
std::map<int, long long> gCoveredUntil;          // an enemy shot at for nothing (a cover in the way): left alone until
std::map<int, long long> gLeftUntil;             // an enemy out of the shots' reach where it is (lying at the doll's feet): left alone until (unless hurt)
std::map<int, long long> gDryLeftUntil;          // its guns empty: an enemy out of its punches' reach (no flat way, above it, lying): left alone until
std::map<int, long long> gVainUntil;             // its guns empty, its punches doing nothing to him: left alone until
std::map<int, long long> gWallLeftUntil;         // not reached from the wall it stands against (behind it, its view locked): left alone until
int gTruceWith = -1; long long gTruceUntil = -1;  // an enemy whose emote it answers: not fought meanwhile (unless it is hurt)
std::map<int, long long> gFoughtAt;              // each enemy: the last frame it was fought (no truce with him for 30 s)
bool FoughtLately(int id) { const auto it = gFoughtAt.find(id); return it != gFoughtAt.end() && gFrame - it->second < 1800; }
// the players' lives seen (v134): each enemy's last death (a kill of its own: its victim), each one's spawn
std::map<int, long long> gDiedAt, gSpawnAt;
std::map<int, std::array<float, 3>> gSpawnPos;
std::map<int, bool> gWasAlive;
int gKillsSeen = -1;                              // its kills counter as last seen (-1 not yet)
// an enemy just (re)spawned, not moved from his spawn point yet: the game's spawn wait (v134's audit: a doll
// rolled through one at his spawn, as through nothing; he stood there 4.5 s): not fought, 10 s at most
bool SpawnGuarded(const Player& p) {
    const auto it = gSpawnAt.find(p.id);
    if (it == gSpawnAt.end() || gFrame - it->second > 600 || !p.alive) return false;
    const auto sp = gSpawnPos.find(p.id);
    return sp != gSpawnPos.end() && Dist2D(p.pos, sp->second.data()) < 300.f;
}
bool FreeObjectiveNear(float r);                  // (a free objective item within r: Triangle would take it)
bool NoAmmo();                                    // (no gun with a round left)
bool KnockedDown(const Player& p);                // (knocked down by a roll)
bool StunWeapon(int c) { return c == 5 || c == 7 || c == 0x10 || c == 0x15; }   // (Mk22, EZ GUN, MOSIN NAGANT, the stun grenade)
// a gun that kills (an enemy asleep is finished with it: the main gun, a sniper's rifle, a handgun)
int LethalGun() {
    const int g = MainGun();
    if (g > 0 && !StunWeapon(g) && g != 9) return g;
    const int r = SniperRifle();
    if (r > 0 && !StunWeapon(r)) return r;
    for (int c : {6, 8}) if (HasWeapon(c) && !Empty(c)) return c;
    return -1;
}
// an enemy asleep or knocked out lies still: no threat. Snake leaves it; a stun weapon does nothing more
// to it: the doll takes a gun that kills to finish it, first among the others (every level)
bool FinishAsleep() { return !IAmSnake() && LethalGun() > 0; }
long long gOffWallAt = -100000;                  // off a wall since (PickTarget)
bool CarrierOf(const Player& p);                 // (he holds the objective's item)
bool Noticed(int id, long long within);          // (one-life modes: he has had the doll in sight these frames)
long long gPeekKeepUntil = -1;                   // ((v137) the master tier's peek behind a corner: the one fought kept 45 frames out of sight)
// (v141 beta 6) how long the one fought stays its target out of sight (a cover's edge, a frame's doubt): 15 frames up to level 5,
// 24 at 7, 36 at 10; the gun on where it saw him last meanwhile (DoFight: no shot at him unseen)
long long TargetKeep() { return gFi.finishSince >= 0 ? 600 : gFrame < gPeekKeepUntil ? 45 : (long long)std::lround(15.f + std::max(0.f, LevelNow() - 5.f) * 4.2f); }
const char* gPickWhy = "";                     // (v141 beta 6) why the nearest enemy in sight was not taken (the AI's debug lines)
float gPickWhyDist = 1e30f;
// ---- (v141 beta 6, aim) the target's switch (section 69 of the requirements: no A/B/A/B): a new one only when clearly better, its score
// ahead of the one fought's (which has its 3 m already) by the tier's margin (kFight: none at TRAINING, 4 m at PRO), or hurting the
// doll (aiming at it while it is hit); never straight back to the one it just left within 1.5 s unless that one hurts it. Each switch
// in the AI's lines ("DOLL TARGET:"); MGO1_DOLL_TARGET_HYST=0: beta 5's choice ----
int gSwitchFrom = -1; long long gSwitchAt = -100000;
int gSwitches = 0;                                   // (this minute's: AiTraceFrame)
bool TargetHyst() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_TARGET_HYST"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
int PickSwitch(int best, float bestScore, bool bestHurts, float curScore, bool curIn) {
    if (best < 0 || gFi.target < 0 || best == gFi.target || !curIn) return best;
    if (TargetHyst() && !bestHurts && !CheatOn()) {      // ((v141 beta 7) tier 10's cheat: the most dangerous at once)
        const bool back = best == gSwitchFrom && gFrame - gSwitchAt < 90;
        if (back || bestScore > curScore - Fk().switchMargin) return gFi.target;
    }
    const Player* a = PlayerById(gFi.target);
    const Player* b = PlayerById(best);
    if (AiDebug()) Log("DOLL TARGET: switch from %s to %s (%s, scores %.0f -> %.0f)", a ? a->name.c_str() : "?", b ? b->name.c_str() : "?",
                       bestHurts ? "it hurts the doll" : "a clearly better threat", curScore, bestScore);
    gSwitchFrom = gFi.target; gSwitchAt = gFrame; ++gSwitches;
    return best;
}
bool HitBy(const Player& p);                     // ((v141 beta 7) hurt a moment ago by him, his gun up on it)
int PickTarget() {
    int best = -1; float bestScore = 1e30f;
    float curScore = 1e30f; bool curIn = false, bestHurts = false;   // (v141 beta 6, aim) the one fought's score, the best one hurting it
    static bool wasWall = false;
    if (AgainstWall()) wasWall = true; else if (wasWall) { wasWall = false; gOffWallAt = gFrame; }
    gPickWhy = ""; gPickWhyDist = 1e30f;
    auto skip = [](const Player* p, const char* why) {
        const float d = p ? Dist2D(gMe.pos, p->pos) : 1e30f;
        if (d < gPickWhyDist) { gPickWhyDist = d; gPickWhy = why; }
    };
    for (const auto& k : gKnown) {
        // (the enemy fought, out of sight a quarter of a second: still the one, a corner's edge or a frame's
        // doubt ends no fight; v136). (v141 beta 6) the higher tiers keep him longer behind a cover's edge (target memory: 15
        // frames up to 5, 24 at 7, 36 at 10), never as long as a real loss
        if (!k.second.visible && !(!OldFight() && k.first == gFi.target && gFrame - k.second.seen <= TargetKeep())) continue;
        const Player* p = PlayerById(k.first);
        if (!p || !Enemy(*p)) continue;
        if (CheatFocusSkips(*p)) { skip(p, "cheat_focus_real_players"); if (k.second.visible) ++gCheat.focusSkips; continue; }   // ((v141 beta 8))
        const bool asleep = Asleep(*p);
        if (asleep && (IAmSnake() || (StunWeapon(Hand()) && !FinishAsleep()))) { skip(p, "asleep_no_lethal_gun"); continue; }
        // (shot at for nothing a moment ago: a cover in the way; still fought close by or when it hurts the doll)
        const auto cv = gCoveredUntil.find(k.first);
        if (cv != gCoveredUntil.end() && gFrame < cv->second && Dist2D(gMe.pos, p->pos) > 8000.f && gFrame - gHurtAt > 120) { skip(p, "covered_shots_wasted"); continue; }
        // (left alone: fought again when the doll is hurt, or its stamina hit with him standing within a metre; its
        // guns empty, the dry ones only then, the vain punches' never)
        const bool close = Dist2D(gMe.pos, p->pos) < 1000.f && std::fabs(p->pos[1] - gMe.pos[1]) < 500.f && !Lying(p->pos) && !p->down &&
                           !Asleep(*p) && !KnockedDown(*p);
        const bool hit = gFrame - gHurtAt <= 120 || (close && gFrame - gStunHitAt <= 120);
        const auto lf = gLeftUntil.find(k.first);
        if (lf != gLeftUntil.end() && gFrame < lf->second && !hit) { skip(p, "left_alone"); continue; }
        const auto wl = gWallLeftUntil.find(k.first);           // (from its wall nothing reaches him: it moves on; half a second off it, fought)
        if (wl != gWallLeftUntil.end() && gFrame < wl->second && (AgainstWall() || gFrame - gOffWallAt < 30)) { skip(p, "wall_left_alone"); continue; }
        if (NoAmmo()) {
            const auto dl = gDryLeftUntil.find(k.first);
            if (dl != gDryLeftUntil.end() && gFrame < dl->second && !(close && hit)) { skip(p, "no_ammo_out_of_reach"); continue; }
            const auto vn = gVainUntil.find(k.first);
            if (vn != gVainUntil.end() && gFrame < vn->second) { skip(p, "no_ammo_punches_vain"); continue; }
        }
        if (k.first == gTruceWith && gFrame < gTruceUntil && gFrame - gHurtAt > 60) { skip(p, "emote_truce"); continue; }   // (its emote answered: a truce)
        // (waiting at his spawn: nothing reaches him. ((v141 beta 7) tier 10's cheat: fought only once he hurts it himself, his gun up on
        // it: another's hit is no reason to fire at one nothing reaches. (v141 beta 8) Or his gun readied there: in play, he can be hit
        // (Snake's game of 14 October: a real player fired from his spawn point, the doll's gun on his head 4 s, it fired once hurt);
        // MGO1_DOLL_CHEAT_SPAWN=0: beta 7's)
        if (SpawnGuarded(*p) && (CheatOn() ? !HitBy(*p) && !(CheatSpawnArmed() && (p->bits[1] & 0x4400)) : gFrame - gHurtAt > 60)) { skip(p, "spawn_protected"); continue; }
        float s = Dist2D(gMe.pos, p->pos);
        if (asleep) s -= 4000.f;                              // (finished before it wakes; one aiming at the doll first)
        if (!gSet.target.empty() && gSet.order == "target" && PlayerByName(gSet.target) == p) s -= 20000.f;   // the one ordered
        if (k.first == gFi.target) s -= 3000.f;              // keeps its target
        if (k.first == gLeaderTarget) s -= 25000.f;          // the one its leader shoots at
        if (OneLifeDefender() && CarrierOf(*p)) s -= 50000.f;   // (one-life defenders) the objective's carrier above all
        // (one-life attackers) one who has not noticed it, beyond 5 m, after the ones who have (it holds its fire at him)
        if (OneLifeAttacker() && !Noticed(k.first, 600) && Dist2D(gMe.pos, p->pos) > 5000.f && gFrame - gHurtAt > 300) s += 20000.f;
        if (!CheatOn()) s -= (200 - p->life) * 20.f;          // the wounded first
        else if (CheatFocus() && RealFoe(*p)) s -= 10000.f;   // ((v141 beta 8) its focus: a real player first, 10 m nearer than he is)
        // the one aiming at the doll first (its weapon up, facing it), more so when the doll is being hurt. ((v141 beta 7) tier 10's
        // cheat: the most dangerous first, aiming at it, then the nearest)
        const bool aiming = std::fabs(Wrap(AngleTo(p->pos, gMe.pos) - p->facing)) < 15.f * kDeg && (p->bits[1] & 0x4400) != 0;
        if (aiming) s -= gFrame - gHurtAt < 120 || CheatOn() ? 30000.f : 6000.f;
        if (k.first == gFi.target) { curScore = s; curIn = true; }
        if (s < bestScore) { bestScore = s; best = k.first; bestHurts = aiming && gFrame - gHurtAt < 120; }
    }
    return PickSwitch(best, bestScore, bestHurts, curScore, curIn);
}
// the first person view: the stick for the view's errors (yaw ye, pitch pe, radians); dead under 0.3,
// faster the longer it is held: small pushes near the aim
void StickFpv(float ye, float pe) {
    // big errors: a steady push, faster the bigger; under 1.5 degrees short pushes (2 frames in 8): the
    // view runs on 4 to 8 frames after each, a steady small push creeps or overshoots
    auto s = [](float deg) {
        const float a = std::fabs(deg);
        const float v = a < 0.2f ? 0.f : a < 1.5f ? (gFrame % 8 < 2 ? 0.46f : 0.f) : std::min(1.f, 0.36f + a * 0.045f);
        return deg < 0 ? -v : v;
    };
    const float sx = -s(ye / kDeg), sy = s(pe / kDeg);
    gPad.sticks[2] = (uint8_t)(0x80 + (int)(sx * 127));
    gPad.sticks[3] = (uint8_t)(0x80 - (int)(sy * 127));
}
// ---- the first person view's aim (v133, probes 29 and 30). The view turns in ticks of 2 frames; the
// stick pushed d from rest turns it r0(d) the first tick, then a(d) faster each tick held (d 0.40: 0.05
// then +0.033; 0.46: 0.09, +0.055; 0.55: 0.18, +0.088; 0.70: 0.35, +0.132; 0.85: 0.59, +0.187; 1.0: 0.89,
// +0.23 degree a tick; under 0.37 nothing); let go, the yaw's turn runs on, 0.75 of the last tick's each
// tick (3 times the last tick's in all; the pitch's about 0.6: 1.5 times); pushed the other way the turn
// stops at once and goes back at r0 (a brake). The aim: each tick, the error that would be left if the
// stick were let go now (the run-on and the target's own move counted): pushed toward it when it is
// more than the level's slack (harder the more is left), let go when the run-on covers it, the brake
// when the run-on would carry the view past it. The target's move (the head's angle a tick) is followed
// from level 4 (all of it at 5, half at 4) ----
struct AimAxis { float cam = 0, rate = 0, tgt = 0, tgtRate = 0; long long camAt = -100, tgtAt = -100; float push = 0; long long decidedAt = -100; };
AimAxis gAimY, gAimP;
float gAimAheadY = 1.f, gAimAheadP = 1.f;          // (v141 beta 7) the view's error in 2 ticks, when a bullet would leave (radians)
long long gAimSince = -100;                     // (the aim's first frame of this spell in the view: its rates afresh)
float AimPush(float need) {                     // the push for an error still to cover (degrees)
    const float a = std::fabs(need);
    return a < 0.35f ? 0.46f : a < 0.8f ? 0.52f : a < 1.6f ? 0.62f : a < 3.5f ? 0.75f : a < 7.f ? 0.88f : 1.f;
}
// one axis: its camera angle and the target's (degrees), the run-on factor (3 the yaw, 1.5 the pitch), the
// part of the target's move followed (0 .. 1); the stick's value for it (-1 .. 1, toward a larger angle)
float AimAxisFrame(AimAxis& A, float cam, float tgt, float runOn, float follow, float slack, bool yaw) {
    auto diff = [yaw](float a, float b) { float d = a - b; if (yaw) { while (d > 180.f) d -= 360.f; while (d < -180.f) d += 360.f; } return d; };
    if (gFrame - gAimSince <= 1) { A = AimAxis(); A.cam = cam; A.tgt = tgt; A.camAt = A.tgtAt = gFrame; }
    if (cam != A.cam) { const float d = diff(cam, A.cam); A.rate = gFrame - A.camAt <= 3 ? d : d * 2.f / (float)(gFrame - A.camAt); A.cam = cam; A.camAt = gFrame; }
    else if (gFrame - A.camAt > 3) A.rate = 0.f;
    if (gFrame - A.tgtAt >= 2) {                                    // (the target's angle a tick, smoothed)
        const float d = diff(tgt, A.tgt) * 2.f / (float)(gFrame - A.tgtAt);
        A.tgtRate = gFrame - A.tgtAt <= 6 ? 0.6f * A.tgtRate + 0.4f * d : 0.f;
        A.tgt = tgt; A.tgtAt = gFrame;
    }
    if (gFrame - A.decidedAt < 2) return A.push;                    // (a push lasts a tick: 2 frames)
    A.decidedAt = gFrame;
    const float move = A.tgtRate * follow;
    const float e = diff(tgt, cam);
    const float s = e >= 0.f ? 1.f : -1.f, E = std::fabs(e), rel = (A.rate - move) * s;   // (its turn toward it, the target's own taken off)
    const float lead = move * s > 0.f ? std::fabs(move) * 2.f : 0.f;   // (the target getting away: pushed harder)
    float p = 0.f;
    if (rel <= 0.05f) { if (E > slack) p = s * AimPush(E + lead); }  // (at rest, or turning away: a push toward it stops that at once)
    else {
        const float left = E - runOn * rel;                          // left if let go now
        if (left > slack) p = s * AimPush(left + lead);
        else if (left < -slack && E + 0.25f < -left) p = -s * 0.46f; // (the run-on would carry it further past than it is short: the brake)
    }
    A.push = p;
    return p;
}
// the view aimed: yaw and pitch (radians) of the view and of the point; the stick set; true when the
// predicted aim (in 2 ticks, when a bullet would leave) is within tolY / tolP (radians) and steady
bool AimView(float camYaw, float camPitch, float tYaw, float tPitch, float follow, float slackDeg, float tolY, float tolP) {
    const float sy = AimAxisFrame(gAimY, camYaw / kDeg, tYaw / kDeg, 3.f, follow, slackDeg, true);
    const float sp = AimAxisFrame(gAimP, camPitch / kDeg, tPitch / kDeg, 1.5f, follow, slackDeg, false);
    gPad.sticks[2] = (uint8_t)(0x80 - (int)(sy * 127));            // (the stick right turns the yaw down)
    gPad.sticks[3] = (uint8_t)(0x80 - (int)(sp * 127));            // (up: the pitch up)
    auto ahead = [](const AimAxis& A, float follow, bool yaw) {     // the error in 2 ticks, the relative turn a tick
        float d = A.tgt - A.cam; if (yaw) { while (d > 180.f) d -= 360.f; while (d < -180.f) d += 360.f; }
        return d + (A.tgtRate * follow - A.rate) * 2.f;
    };
    const float ey = ahead(gAimY, follow, true) * kDeg, ep = ahead(gAimP, follow, false) * kDeg;
    const float ry = std::fabs(gAimY.tgtRate * follow - gAimY.rate) * kDeg, rp = std::fabs(gAimP.tgtRate * follow - gAimP.rate) * kDeg;
    gAimAheadY = ey; gAimAheadP = ep;                               // ((v141 beta 7) tier 10's cheat: its shot on the aim's way across him)
    return std::fabs(ey) < tolY && std::fabs(ep) < tolP && ry < tolY && rp < tolP;
}
// ---- (v150-host beta 18) the direct aim (MGO1_DOLL_AIM=direct, doll_aim.h; docs/PLAYABLE_DOLL.md 26). Its own player's angles in its
// own game, nothing else (probe 44, City Under Siege): the third person's facing is the player's turn, s16 at work +0x142 (0x10000 a
// turn), its goal at +0x14A: both written, the facing is there the next frame and stays, readied or not, and the game sends it as
// it sends a turn by the stick (the host's copy follows, the records relayed). The first person view's angles are NewCameraProg's
// work +0x320 (the pitch, over 0 looking down) and +0x322 (the yaw, the player's turn follows it): written, the view is there at
// the game's next tick and stays. Written only on a view in its state (0x80) whose yaw word is the player's turn (else the pad, a
// line once). The turn toward the point at the virtual doll's degrees a frame of its level (kTurnDeg = vdoll_body.cpp kLevel's),
// no overshoot; the point keeps the difficulty's own error; the recoil (the game's, in the view) is turned back at that rate like
// any other error, nothing done before a bullet. Off: none of this runs ----
long long gDirectSaidAt = -100000;               // (the last "aim: ... the pad" line: once a minute at most)
int gDirectWrites = 0, gDirectFallbacks = 0, gViewFollowed = 0;     // (this minute's: AiTraceFrame's line)
long long gDirectMinAt = -1;
float DirectRate() {                             // (and a line a minute: its writes and the frames it gave back to the pad)
    if (gDirectMinAt < 0) gDirectMinAt = gFrame;
    if (gFrame - gDirectMinAt >= 3600) {
        Log("aim: direct: %d frames written, %d frames of the view by the pad this minute (%.0f degrees a frame; the turn followed the view %d frames)", gDirectWrites,
            gDirectFallbacks, aim::RateDeg(Lv()), gViewFollowed);
        gDirectWrites = gDirectFallbacks = 0; gViewFollowed = 0; gDirectMinAt = gFrame;
    }
    return aim::Rate(Lv());
}
// the view's angles' place (NewCameraProg's work), 0 when not to be written now (why said once a minute)
uint16_t gViewWritten = 0; long long gViewWrittenAt = -100;   // (the yaw it wrote last, when)
uint32_t gViewCam = 0, gViewOff = 0x320;          // (the camera actor whose angles it wrote last, their place in its work)
// (paim_a2, capture on Graniny Gorki Lab: in some lives the NewCameraProg 0x27F580 points at holds the view's angles at +0xB0/+0xB2
// instead of +0x320/+0x322 (its yaw the player's turn there, 0 at +0x322): both places are tried. Each place is kept only while the
// player's turn follows what it writes (the game derives the turn from the view's yaw at its tick): 12 frames of writes the turn does
// not follow, that camera's place is left 15 s and the pad has the view)
const uint32_t kViewOffs[2] = {0x320, 0xB0};
struct ViewWrite { long long at = -100; uint16_t v = 0; };
ViewWrite gViewHist[6];
int gViewNoFollow = 0; uint32_t gViewBanCam = 0, gViewBanOff = 0; long long gViewBanUntil = -1;
uint32_t gViewAbs = 0;                           // (a place its own search found, below: its pitch word)
void ViewFollowCheck() {
    if (!gViewCam || gFrame - gViewWrittenAt != 1) return;   // (only while writing)
    const uint16_t turn = R16(gMe.obj + 0x142);
    bool follows = false, old = true;
    for (const ViewWrite& w : gViewHist) if (gFrame - w.at <= 6 && std::abs((int)(int16_t)(turn - w.v)) <= 16) follows = true;
    for (const ViewWrite& w : gViewHist) if (gFrame - w.at > 6) old = false;   // (6 frames of writes in a row)
    if (follows) { gViewNoFollow = 0; ++gViewFollowed; return; }
    if (!old) return;
    if (++gViewNoFollow >= 12) {
        Log("aim: direct: the player's turn does not follow the view's angles written at camera %x +0x%x (%d frames): that place left 15 s, the pad", gViewCam, gViewOff, gViewNoFollow);
        gViewBanCam = gViewCam; gViewBanOff = gViewOff; gViewBanUntil = gFrame + 900; gViewNoFollow = 0;
        if (gViewCam == gViewAbs) gViewAbs = 0;          // (a searched place of an earlier stage: forgotten, searched again)
        gViewCam = 0;
    }
}
// ((v150-host beta 18) its own search, in a match where no known place holds the view (paim_du10c's second round, paim_a2: the
// NewCameraProg 0x27F580 points at with 0 at +0x322 and a copy of the turn at +0xB2 that the turn does not follow): the memory
// before and after a turn of the view by the pad (2 degrees or more), the 16-bit words that moved as the turn did and equal it;
// each one written 10 degrees on, the stick still 4 frames: the one the player's turn follows is the view's yaw (its pitch the word
// before it, as at both known places); the others put back to the turn. Once a minute at most; the place kept for the match while
// the turn follows it (ViewFollowCheck)
bool gVsHold = false;                            // (the search holds the stick this frame)
struct ViewSearch { std::vector<uint8_t> snap; long long snapAt = -1000, nextAt = 0, testAt = -1; int snapTurn = 0, testBefore = 0, fails = 0; std::vector<uint32_t> cand; size_t ci = 0; };
ViewSearch gVs;
// (a search that found nothing: the next one 1, 2, 4 then 8 minutes later; the stick it holds still is a fight's frames)
long long ViewSearchWait() { const long long w = 3600LL << std::min(gVs.fails, 3); ++gVs.fails; return w; }
bool ViewSearchFrame(uint16_t turn) {
    if (gFrame < gVs.nextAt || !gMe.obj) return false;
    const uint8_t* mem = GameMem();
    if (gVs.cand.empty()) {
        if (gVs.snap.empty() || gFrame - gVs.snapAt > 90) { gVs.snap.assign(mem, mem + 0x02000000); gVs.snapAt = gFrame; gVs.snapTurn = (int16_t)turn; return false; }
        const int d = (int16_t)(turn - gVs.snapTurn);
        if (std::abs(d) < 364 || gFrame - gVs.snapAt < 4) return false;
        const uint32_t me = gMe.obj & 0x01FFFFFF;
        for (uint32_t a = 0x100000; a + 2 <= 0x02000000 && gVs.cand.size() < 48; a += 2) {
            if (a >= me + 0x140 && a < me + 0x150) continue;   // (the turn and its goal themselves)
            int16_t cur, old; memcpy(&cur, mem + a, 2); memcpy(&old, gVs.snap.data() + a, 2);
            if (std::abs((int)(int16_t)(cur - old) - d) <= 4 && std::abs((int)(int16_t)(cur - (int16_t)turn)) <= 16) gVs.cand.push_back(a);
        }
        Log("aim: direct: the view's place searched: %d words moved with the turn (%.1f degrees in %lld frames)", (int)gVs.cand.size(), d * 360.f / 65536.f, gFrame - gVs.snapAt);
        gVs.snap.clear(); gVs.snap.shrink_to_fit(); gVs.ci = 0; gVs.testAt = -1;
        if (gVs.cand.empty()) { gVs.nextAt = gFrame + ViewSearchWait(); return false; }
    }
    if (gVs.testAt < 0) {
        const uint32_t a = gVs.cand[gVs.ci];
        gVs.testBefore = (int16_t)turn; gVs.testAt = gFrame;
        W16(a, (int16_t)R16(a) + 1820);
        return true;
    }
    if (gFrame - gVs.testAt < 4) return true;
    const uint32_t a = gVs.cand[gVs.ci];
    const int moved = (int16_t)(turn - gVs.testBefore);
    if (std::abs(moved - 1820) <= 300) {
        gViewAbs = a - 2; gVs.cand.clear(); gVs.nextAt = gFrame + 600; gVs.fails = 0;
        Log("aim: direct: the view's place found by its own search: %x (the turn followed it %.1f degrees of 10)", a, moved * 360.f / 65536.f);
        return false;
    }
    W16(a, (int16_t)turn);                              // (not the view: put back to the turn)
    gVs.testAt = -1;
    if (++gVs.ci >= gVs.cand.size()) { Log("aim: direct: the view's place not found by its own search (%d words tried)", (int)gVs.cand.size()); gVs.cand.clear(); gVs.nextAt = gFrame + ViewSearchWait(); }
    return true;
}
uint32_t DirectViewWork() {
    DirectRate();
    ViewFollowCheck();
    // (paim_d10c: more than one NewCameraProg lives, an old one's angles still: the one whose yaw is the player's turn, or the one it
    // wrote the last frames, its value unchanged since)
    uint32_t cam = 0, first = 0, off = 0x320;
    const uint32_t global = R32(0x27F580) & 0x01FFFFFF;  // (probe 44: the camera's work pointed at from 0x27F580)
    // (paim_t10: that pointer at another object some lives, its angles 0: every NewCameraProg then. Only that class: paim_t10b found
    // GM_CameraDaemonStart's word there following the turn, not the view)
    static std::vector<std::pair<uint32_t, std::string>> cams;
    cams.clear();
    if (gMe.bits[0] & 0x80) {
        ps2world::ForEachActor([](uint32_t w, const std::string& name, void*) { if (name == "NewCameraProg" && cams.size() < 12) cams.push_back({w & 0x01FFFFFF, name}); }, nullptr);
        for (auto& c : cams) if (c.first == global) { std::swap(c, cams.front()); cams.front().second = "the NewCameraProg pointed at from 0x27F580"; break; }   // (that one first)
    }
    const char* camName = "";
    const uint16_t turn = R16(gMe.obj + 0x142);
    gVsHold = false;
    // (a place its own search found in this match, first: its pitch word, its yaw word 2 bytes on)
    if (gViewAbs && (gMe.bits[0] & 0x80) && !(gViewAbs == gViewBanCam && gFrame < gViewBanUntil) &&
        (std::abs((int)(int16_t)(R16(gViewAbs + 2) - turn)) <= 8 || (gViewAbs == gViewCam && gFrame - gViewWrittenAt <= 3 && R16(gViewAbs + 2) == gViewWritten))) {
        cam = gViewAbs; off = 0; camName = "found by its own search"; first = gViewAbs;
    }
    // (MGO1_DOLL_AIM_SEARCH_TEST=1, tests: the known places not used, its own search finds the view's place in any match)
    static const bool searchTest = getenv("MGO1_DOLL_AIM_SEARCH_TEST") && *getenv("MGO1_DOLL_AIM_SEARCH_TEST") == '1';
    if (searchTest && !first && !cams.empty()) first = cams.front().first;
    for (size_t n = 0; n < cams.size() && !cam && !searchTest; ++n) {
        const uint32_t c = cams[n].first;
        if (c < 0x100000 || c + 0x330 > 0x02000000) continue;
        if (!first) first = c;
        for (uint32_t o : kViewOffs) {
            if (c == gViewBanCam && o == gViewBanOff && gFrame < gViewBanUntil) continue;
            if (std::abs((int)(int16_t)(R16(c + o + 2) - turn)) <= 8 ||
                (c == gViewCam && o == gViewOff && gFrame - gViewWrittenAt <= 3 && R16(c + o + 2) == gViewWritten)) { cam = c; off = o; camName = cams[n].second.c_str(); break; }   // ((the turn follows a written yaw at the game's tick)
        }
    }
    const char* no = !(gMe.bits[0] & 0x80) ? "" : !first ? "no camera actor" : !cam ? "no camera's yaw is the player's turn" : nullptr;
    if (no && *no && ViewSearchFrame(turn)) { gVsHold = true; return 0; }   // (its own search testing a place: the stick still)
    if (!no) {
        if ((cam != gViewCam || off != gViewOff) && gDebug) Log("aim: direct: the view's angles at camera %x +0x%x (%s)", cam, off, camName);
        if (cam != gViewCam || off != gViewOff) { gViewNoFollow = 0; for (ViewWrite& w : gViewHist) w = ViewWrite(); }
        gViewCam = cam; gViewOff = off; return cam;
    }
    if (*no && (gFrame - gDirectSaidAt > 3600 || (gDebug && gFrame - gDirectSaidAt > 30))) {
        gDirectSaidAt = gFrame;
        Log("aim: direct: the view's angles not written (%s): the pad (camera %x yaw %d/%d pitch %d/%d at +0x322/+0xB2, the turn %d, written %d %lld frames ago, state %x %x, weapon %x, 0x27F580 %x)", no,
            first, first ? R16s(first + 0x322) : 0, first ? R16s(first + 0xB2) : 0, first ? R16s(first + 0x320) : 0, first ? R16s(first + 0xB0) : 0, R16s(gMe.obj + 0x142),
            (int16_t)gViewWritten, gFrame - gViewWrittenAt, gMe.bits[0], gMe.bits[1], Hand(), global);
        if (gDebug) {
            std::string l; for (const auto& c : cams) { char b[96]; snprintf(b, sizeof b, " %s %x yaw %d/%d;", c.second.c_str(), c.first, R16s(c.first + 0x322), R16s(c.first + 0xB2)); l += b; }
            Log("aim: direct: the cameras:%s", l.c_str());
            // (the search for where the view's yaw is in such a life: the words equal to the turn in each camera's work and the pointed one)
            std::string h; const int tv = R16s(gMe.obj + 0x142);
            std::vector<uint32_t> ws; for (const auto& c : cams) ws.push_back(c.first); if (global >= 0x100000 && global < 0x01FF0000) ws.push_back(global);
            for (uint32_t w : ws) for (uint32_t o = 0; o < 0x1000 && h.size() < 700; o += 2)
                if (std::abs((int)(int16_t)(R16(w + o) - tv)) <= 8) { char b[48]; snprintf(b, sizeof b, " %x+%x=%d(p %d)", w, o, R16s(w + o), R16s(w + o - 2)); h += b; }
            Log("aim: direct: the turn's value in the cameras' works:%s", h.c_str());
        }
    }
    if (*no) ++gDirectFallbacks;
    return 0;
}
// the third person: the facing turned toward want (radians, the error in it) one frame; false when the state does not allow it
// (the turn's word itself, not the facing's matrix: the matrix follows at the game's tick, a step from it would be taken twice). The
// gun's way behind the facing followed (gGunLag, doll_aim.h kGunFollow): DirectAimed reads where the bullet will go
float gGunLag = 0.f, gDirectFace = 0.f; long long gGunAt = -100;
bool DirectFacing(float want) {
    if (!gMe.obj || !gMe.alive || AgainstWall() || OnLadder() || Prone() || (gMe.bits[0] & 0x80)) { gGunAt = -100; return false; }
    const float cur = aim::From16(R16s(gMe.obj + 0x142)), next = aim::StepYaw(cur, want, DirectRate());
    const long long gap = gFrame - gGunAt;
    gGunLag = (gap >= 1 && gap <= 20 ? aim::GunLagAfter(gGunLag, (int)gap) : 0.f) - aim::Wrap(next - cur);
    gGunAt = gFrame; gDirectFace = next;
    const int v = aim::To16(next);
    if (v != R16s(gMe.obj + 0x142)) { W16(gMe.obj + 0x142, v); W16(gMe.obj + 0x14A, v); }
    ++gDirectWrites;
    return true;
}
// the third person's aim with the direct aim: the gun's way when a bullet would leave (3 frames after the pull) within the tolerance
float DirectGunErr(float want, int ahead = 3) { return std::fabs(aim::Wrap(want - (gDirectFace + aim::GunLagAfter(gGunLag, ahead)))); }
// the first person view: its angles turned toward the point (radians) one frame; *on as AimView's (the view on it within the
// tolerances once this frame's step is taken); false: not written (the pad's AimView then)
bool DirectView(float tYaw, float tPitch, float tolY, float tolP, bool* on) {
    const uint32_t cam = DirectViewWork();
    if (!cam && gVsHold) { *on = false; return true; }    // (its own search testing a place: the stick still, no shot)
    if (!cam) return false;
    const uint32_t o = gViewOff;
    const float y0 = aim::From16(R16(cam + o + 2)), p0 = -aim::From16(R16(cam + o)), r = DirectRate();
    const float y1 = aim::StepYaw(y0, tYaw, r), p1 = aim::StepPitch(p0, std::max(-1.4f, std::min(1.4f, tPitch)), r);
    W16(cam + o + 2, aim::To16(y1)); W16(cam + o, aim::To16(-p1)); ++gDirectWrites;
    gViewWritten = (uint16_t)aim::To16(y1); gViewWrittenAt = gFrame;
    for (int i = 5; i > 0; --i) gViewHist[i] = gViewHist[i - 1];
    gViewHist[0].at = gFrame; gViewHist[0].v = gViewWritten;
    gAimAheadY = aim::Wrap(tYaw - y1); gAimAheadP = tPitch - p1;     // (left after this step: tier 10's tests read them)
    gAimY.rate = aim::Wrap(y1 - y0) / kDeg; gAimP.rate = (p1 - p0) / kDeg;
    *on = std::fabs(gAimAheadY) < tolY && std::fabs(gAimAheadP) < tolP;
    return true;
}
std::string DirectAimLine() {
    char b[300];
    snprintf(b, sizeof b, "direct: its facing and its first person view turned by writing their angles in its own game, %.0f degrees a frame (the virtual doll's "
             "level %d), its difficulty's error kept; the buttons, the weapon's rate, the bullet, the recoil and the damage the game's", aim::RateDeg(Lv()), Lv());
    return b;
}
// ---- the trigger (probe 10): a shot from the weapon readied (Square light) only; the automatic
// weapons (Scorpion, XM16E1, AK-47) fire while Square is full (a bullet every 6 frames); the SVD and the
// Mosin Nagant once a press and the M37 (pump, 4 shells, a shot every 70 frames at best) once a press
// too, Square let go between two shots (20 frames, the M37 35), then readied again ----
enum TriggerKind { TK_AUTO, TK_SEMI, TK_PUMP };
TriggerKind Trigger() {
    const int h = Hand();
    if (h <= 0) return gPrimaryNow == 3 ? TK_PUMP : (gPrimaryNow == 4 || gPrimaryNow == 5) ? TK_SEMI : TK_AUTO;
    return h == 0xE ? TK_PUMP : (h == 0xF || h == 0x10 || h == 0x11 || h == 5 || h == 6 || h == 8) ? TK_SEMI : TK_AUTO;   // (the handguns, (v141 beta 6) the RPG-7: a shot a press)
}
// an empty magazine: the game reloads by itself when the trigger is pulled (the player's state word 1
// has 0x80 meanwhile, a second or two): nothing to press, only to say it
bool Reloading() { return (gMe.bits[1] & 0x80) != 0; }
bool GoTo(const float* p, const char* why, float arrive = 800.f);
void FightFrame(const Player& p);
// the weapon for a fight at this distance: a sniper's handgun close by (its rifle's scope keeps it where it
// stands, the view locked), its rifle far away; the flamethrower close by only (4 m: it does not reach
// farther, the player still while it fires); Snake's Mk22 close by (the darts, the CQC), his XM16E1
// farther; never a grenade, a trap in a fight. (v141 beta 6) The rifle (and the RPG-7, tier 6 up) only for a valid shot
// (FpvEntryOk), kept while the first person view's state machine holds its stay; else the main gun or the handgun
bool FpvOn();                                    // ((v141 beta 6) MGO1_DOLL_FPV_SM=0: beta 5's)
bool FpvEntryOk(int code, const char* ask);      // ((v141 beta 6) section 87's checks: a valid shot for it now)
bool FpvKeepOk();                                // (its stay going on and not being left)
int FpvBackup();                                 // (the weapon a stay leaves for)
bool RpgUse();                                   // (the RPG-7 taken for a shot: tier 6 up, MGO1_DOLL_RPG)
bool RpgRules(int code);                         // (that weapon under the RPG-7's rules)
extern const char* gFvAsk;                       // (why the weapon window is asked for or away from one, this frame)
extern const char* gFvNo;                        // (the last check FpvEntryOk failed)
const char* FpvFireBlock(const Player& p, float dist);   // (the RPG-7's safety at its shot: why not; nullptr: fire)
int FightWeaponFor(float dist, int held);
int FightWeapon(float dist, int held, bool asleep) {
    const int w = FightWeaponFor(dist, held);
    if (asleep && StunWeapon(w) && FinishAsleep()) {                       // (asleep: a gun that kills)
        const int l = LethalGun();
        // ((v141 beta 6) a first person view weapon only when its state machine lets it in)
        if (FpvOn() && LocksView(l) && !(held == l ? FpvKeepOk() : FpvEntryOk(l, "finish_asleep"))) return w;
        if (LocksView(held) && !LocksView(l)) gFvAsk = "target_asleep";   // (the Mosin's darts did it: the stay's reason)
        return l;
    }
    return w;
}
int FightWeaponFor(float dist, int held) {
    const bool hs = HeadshotsOnly();                 // (headshots only: no flame, no shotgun's pellets)
    if (!hs && HasWeapon(9) && Rounds(9) != 0 && !IAmSnake() && (held == 9 ? dist < 4500.f : dist < 4000.f)) return 9;
    if (IAmSnake()) {
        const int close = HasWeapon(5) ? 5 : Handgun(), far = HasWeapon(0xB) ? 0xB : MainGun();
        if (held == close && close > 0 && dist < 12000.f) return held;          // (a margin: no switching to and fro)
        if (held == far && far > 0 && dist > 8000.f) return held;
        return dist < 10000.f ? (close > 0 ? close : far) : (far > 0 ? far : close);
    }
    if (Sniper()) {
        const int hg = Handgun(), rifle = SniperRifle();
        if (FpvOn()) {                                   // ((v141 beta 6) the rifle for a valid shot only; close by the main gun, else the handgun)
            if (rifle > 0 && (held == rifle ? FpvKeepOk() : FpvEntryOk(rifle, "valid_shot"))) return rifle;
            if (held == rifle && rifle > 0 && !gFvAsk) gFvAsk = "weapon_choice";
            const int close = FpvBackup();
            return close > 0 ? close : hg > 0 ? hg : rifle;
        }
        if (dist < 9000.f && hg > 0) return hg;
        if (dist > 14000.f && rifle > 0) return rifle;
        if ((held == hg || held == rifle) && held > 0) return held;
        return rifle > 0 ? rifle : hg;
    }
    const int g = MainGun();
    if (hs && (g == 0xE || g == 9 || g == 0x11 || g <= 0)) { const int h = Handgun(); if (h > 0) return h; }
    // ((v141 beta 6) the RPG-7 had (a weapon box): one shot at a still or grouped enemy at a safe range, its line clear)
    if (!hs && RpgUse() && HasWeapon(0x11) && !Empty(0x11) && (held == 0x11 ? FpvKeepOk() : FpvEntryOk(0x11, "rpg_shot"))) return 0x11;
    return g > 0 ? g : Handgun();
}
// ---- the bursts (v133): the AK-47 and the M63 (a bullet every 6 frames, the first at the 4th frame of
// Square full) fired a bullet at a time far away, 3 or 4 at a time at mid range, sprayed close by; the
// other automatic weapons their level's bursts. Headshots only: a bullet at a time (the recoil lifts
// the next ones) ----
int BurstFrames(int weapon, float dist, int levelBurst) {
    if (HeadshotsOnly()) return 5;                               // (a bullet: at the 4th frame, the next at the 10th)
    if (weapon != 0xC && weapon != 0xD) return levelBurst;
    if (dist < 7000.f) return 34 + (int)(Rnd() % 13);             // 6 to 8 bullets
    if (dist < 20000.f) return Rnd() % 2 ? 17 : 23;               // 3 or 4
    return 5;                                                    // 1
}
// ---- the first person view's lean and crouch while shooting (probe 26: R1 held, L2 leans left, R2
// right, the eye 0.5 m aside in 30 frames, the shots as usual; Cross crouches, the eye 0.54 m lower,
// state 0x85), levels 3 and up (crouched at 4 and up). The lean: toward the side whose eye sees the head
// when the straight one does not (around a corner); else, at 5, from side to side every second or two,
// at 3 and 4 one side now and then. Crouched when the crouched eye sees the head too ----
const float kLeanSide = 500.f;                       // the eye's offset leaning (mm; probe 26: 498)
const float kCrouchDrop = 540.f;                     // the eye lower crouched (mm; probe 26)
struct LeanRun { int side = 0, clearN = 0; long long at = -100000, sideAt = -100000, lowAt = -100000, pressAt = -100000, upAt = -100000, next = 0; bool low = false;
                 bool around = false; long long hideUntil = -1, heldSince = -1, lastPress = -100; };   // ((v137) the lean that sees him round a corner; the master tier's peek)
LeanRun gLean;
long long gStandUpSince = -1;                        // crouched by a fight: up again before walking (crouched, it would crawl)
long long gR2HoldUntil = -1;                         // R2 kept this long after a right lean began (BrainFrame): no tap
long long gR2Since = -1;                             // R2 out since (BrainFrame: 24 frames at least)
// low (crouched, lying) where it could stand, before it moves in a fight: up first, still (moving crouched
// is crawling), 2.5 s a time, then 1.5 s of the fight as it is, then again (v133's audit: given up for
// good after 2 s, a doll lying in a rescue fired 167 s at a ledge); never in a crawl passage nor hiding
long long gFightUpSince = -1;
bool FightStandUp(const std::string& arg) {
    const bool crawlArea = MapReady() && gMap->nav->AreaAt(gMe.pos) == AREA_CRAWL;
    if (!Low() || crawlArea || gSet.order == "hide") { gFightUpSince = -1; return false; }
    if (gFightUpSince < 0) gFightUpSince = gFrame;
    const long long t = (gFrame - gFightUpSince) % 240;
    if (t >= 150) return false;                                // (not up: on all the same a moment)
    if (t == 149 && gDebug) Log("fight: not up after 2.5 s (state %x, hips %.0f above the ground): again in 1.5 s", gMe.bits[0], HipHeight());
    SetLow(false); Steer(0, 0, 0); Act("standing_up", arg);
    return true;
}
void LeanFrame(const float* eye, float camYaw, const float* head, bool firing) {
    if (Lv() < 3 || Scoped() || gFrame - gFi.fpvSince < 12) { gLean.side = 0; return; }   // (R1 first: R2 alone opens the window)
    // (v137) nothing before the view's state is on: R2 or L2 then opened a window, Cross crouched (the motion lines, motion137/dm:
    // d3.txt f14142 R2 at +12 in the state 3, the view on at +42; d2.txt f7080 L2 at +24, on at +54)
    if (gFi.viewOnAt < 0) { gLean.side = 0; return; }
    // (against a wall: no lean; v134's audit, a lean there left the wall's view, 0x82, on after the fight 19 s)
    const uint32_t stw = gMe.bits[0] & 0x7F;
    if (stw == 2 || stw == 4) { if (gLean.side && gDebug) Log("fight: leaning no more (against a wall)"); gLean.side = 0; return; }
    const float rx = -std::cos(camYaw), rz = std::sin(camYaw);   // the view's right (the stick right turns the yaw down)
    if (gFrame - gLean.at > gLean.next) {
        gLean.at = gFrame; gLean.next = NmV3() ? 90 + (long long)(Rnd() % 60) : Lv() >= 5 ? 150 + (long long)(Rnd() % 120) : 240;   // (the next choice in 2.5 to 4.5 s at 5, 4 s below: the audit, a lean
                                                                                   // every second spoilt the aim more than it hid the doll)
        // the eye unleaned (leaning: 0.5 m aside after 30 frames), then its two leaned places
        const float k = gLean.side * kLeanSide * std::min(1.f, (gFrame - gLean.sideAt) / 30.f);
        const float e0[3] = {eye[0] - rx * k, eye[1], eye[2] - rz * k};
        const float l[3] = {e0[0] - rx * kLeanSide, e0[1], e0[2] - rz * kLeanSide}, r[3] = {e0[0] + rx * kLeanSide, e0[1], e0[2] + rz * kLeanSide};
        // (a side with a wall within the lean's half metre: not leaned to, it would press the doll to it)
        const bool straight = LineClear(e0, head), left = LineClear(e0, l) && LineClear(l, head), right = LineClear(e0, r) && LineClear(r, head);
        // (v137) a clear view: a lean only to dodge, under fire (hurt these 3 s) after its own first pull; leaned at random
        // the eye moved 0.5 m in 30 frames, the aim off 2.9 degrees at 10 m while it came back (the shots it delayed).
        // (NIGHTMARE) him aiming at it too (the peek's test): the lean before his shots
        const Player* lt = Nightmare() ? PlayerById(gFi.target) : nullptr;
        const bool aimed = lt && std::fabs(Wrap(AngleTo(lt->pos, gMe.pos) - lt->facing)) < 20.f * kDeg && (lt->bits[1] & 0x4400);
        // ((v141 beta 3) NIGHTMARE: from its first pull, leaned while it fires at the head, its side changed every 1.5 to 2.5 s)
        // ((v141 beta 10) tier 10: no dodge lean, its aim kept still (its crouched or leaned bullets on him 30 % against 43 % standing))
        const bool dodge = !(CheatOn() && !CheatWaitsB8()) && gFi.shots > 0 && (gFrame - gHurtAt < 180 || aimed || NmV3());
        int s = 0;
        if (!straight) s = right ? 1 : left ? -1 : 0;
        else if (!dodge) s = 0;
        else if (Lv() >= 5) s = gLean.side > 0 ? (left ? -1 : 0) : gLean.side < 0 ? (right ? 1 : 0) : Rnd() % 2 ? (right ? 1 : left ? -1 : 0) : (left ? -1 : right ? 1 : 0);
        else if (Rnd01() < 0.4f) s = Rnd() % 2 ? (right ? 1 : 0) : (left ? -1 : 0);
        if (s != gLean.side) { gLean.sideAt = gFrame; if (gDebug) Log("fight: leaning %s", s > 0 ? "right" : s < 0 ? "left" : "no more"); }
        gLean.side = s;
        gLean.around = !straight && s != 0;
    }
    // ((v137) the master tier's peek: leaning round a corner, its burst just over, him aiming at it: back 12 to 15 frames, no
    // shot, then out again (the fight keeps him 45 frames out of sight then). Only after 30 frames held: L2 or R2 let go
    // sooner is a tap)
    if (gLean.side != 0 && gLean.around && MasterFight() && !(CheatOn() && !CheatWaitsB8()) && !firing && gFrame >= gFi.fireUntil && gFrame - gFi.fireUntil < 3 && gLean.lastPress == gFrame - 1 &&
        gFrame - gLean.heldSince >= 30 && gFrame >= gLean.hideUntil) {
        const Player* t = PlayerById(gFi.target);
        if (t && std::fabs(Wrap(AngleTo(t->pos, gMe.pos) - t->facing)) < 20.f * kDeg && (t->bits[1] & 0x4400)) {
            gLean.hideUntil = gFrame + 12 + (long long)(Rnd() % 4); gPeekKeepUntil = gLean.hideUntil + 30;
            if (gDebug) Log("fight: back round the corner a moment (%s aiming at it)", t->name.c_str());
        }
    }
    const bool press = gFrame >= gLean.hideUntil && gLean.side != 0;
    if (press && gLean.lastPress != gFrame - 1) gLean.heldSince = gFrame;   // (a press begun now)
    if (press) gLean.lastPress = gFrame;
    if (press && gLean.side > 0) { gPad.buttons |= 0x0200; gPad.pressures[5] = 0xFF; gR2HoldUntil = std::max(gR2HoldUntil, gLean.sideAt + 24); }
    else if (press && gLean.side < 0) { gPad.buttons |= 0x0100; gPad.pressures[4] = 0xFF; }
    // crouched (4 and up) when the crouched eye sees the head; standing again when it does not. The
    // posture from the view's own eye (0.84 m above the hips standing, 0.3 m crouched: probe 26), Cross
    // pressed 3 frames and not again for 45 (pressed on a posture not seen yet, it would lie down: then
    // the stick crawls instead of turning the view)
    const float eyeUp = eye[1] - gMe.pos[1];
    const bool lowNow = eyeUp < 560.f, prone = Prone() || HipHeight() < 450.f;
    if (gFrame - gLean.pressAt < 3) { gPad.buttons |= 0x4000; return; }
    // ((v137) lying in a crawl passage, a vent: it stays down, no getting up there)
    if (prone && MapReady() && gMap->nav->AreaAt(gMe.pos) == AREA_CRAWL) { gLean.low = false; return; }
    if (prone) { if (gFrame - gLean.pressAt > 45) { gLean.pressAt = gFrame; gPad.buttons |= 0x4000; gLean.low = false; } return; }   // (up again)
    if (Lv() >= 4 && !firing) {
        if (gFrame - gLean.lowAt > 60) {                     // (decided once a second: no dithering)
            gLean.lowAt = gFrame;
            const float low[3] = {eye[0], eye[1] - (lowNow ? 0.f : kCrouchDrop), eye[2]};
            const bool clear = LineClear(low, head);
            gLean.clearN = clear ? gLean.clearN + 1 : 0;
            // up at once when the crouched eye does not see the head; down after two seconds of it seen, not
            // within 5 s of getting up (no bobbing up and down)
            // ((v137) not before its first pull of the fight: the crouch moves the eye 0.54 m down, the aim with it)
            const bool want = CheatOn() && !CheatWaitsB8() ? false : gLean.low ? clear : gLean.clearN >= 2 && gFrame - gLean.upAt > 300 && gFi.shots > 0;   // ((v141 beta 10) tier 10: standing)
            if (want != gLean.low) {
                if (gDebug) Log("fight: %s to shoot", want ? "crouched" : "standing");
                if (!want) gLean.upAt = gFrame;
            }
            gLean.low = want;
            if (want) gStandUpSince = 0;
        }
        if (gLean.low != lowNow && gFrame - gLean.pressAt > 45) { gLean.pressAt = gFrame; gPad.buttons |= 0x4000; }
    }
}
// ---- the CQC throw (Snake, Raikov, Reiko, Rowdy). A CQC weapon in the hands (the game's weapons with 0x80
// in their flags: none, the knife, Mk22, M1911A1, EZ GUN), within 0.7 m facing the enemy, Circle pressed
// anew and held: the grab, the enemy thrown down, its stamina gone at once (the doll's CQC score +1).
// Each try is logged with what came of it (the enemy's stamina, down, the score); two ways are tried in
// turn until one works (Circle alone, Circle with the stick toward the enemy), then that one is kept ----
bool CqcWeapon(int c) {                         // the weapon's flags (its state +0x15): 0x80 a CQC weapon
    if (c == 0) return true;
    const uint32_t st = WeaponState(c);
    return st ? (R16(st + 0x14) & 0x8000) != 0 : c >= 1 && c <= 7;
}
int CqcWeaponCode() { if (CqcWeapon(Hand())) return Hand(); for (int c : {5, 6, 1}) if (HasWeapon(c)) return c; return 0; }
struct CqcTry { int target = -1, tries = 0, phase = 0, stunBefore = 0, scoreBefore = -1; long long at = 0, releasedAt = -100, since = 0, offUntil = -1, lastCall = -100, stillWait = -1, grabAt = -1; bool stick = false; };
CqcTry gCq;
int gCqcWay = 0;                                // the way that worked: 0 not known yet, 1 Circle alone, 2 with the stick
bool CqcFrame(const Player& p, float dist, bool reacted) {
    CqcTry& C = gCq;
    if (C.target != p.id) { C = CqcTry(); C.target = p.id; C.since = gFrame; }
    if (gFrame - C.lastCall > 2) { C.since = gFrame; if (C.phase) C.phase = 0; }   // (not asked a moment: afresh)
    C.lastCall = gFrame;
    const long long t = gFrame - C.at;
    const float want = AngleTo(gMe.pos, p.pos), err = Wrap(want - Facing());
    if (C.phase == 0) {
        // (him or the doll on a ladder: no grab there; v136's audit, a try from a ladder's top at a carrier on it
        // came just before both games froze)
        if (gFrame < C.offUntil || p.down || !p.alive || OnLadder() || (p.bits[2] & 0x40000)) return false;
        const float look = std::fabs(Wrap(AngleTo(p.pos, gMe.pos) - p.facing));
        const bool unaware = look > 60.f * kDeg, aimingAtMe = look < 20.f * kDeg && (p.bits[1] & 0x4400) != 0;
        const float fa[3] = {gMe.pos[0], gMe.pos[1] - 700.f, gMe.pos[2]}, fb[3] = {p.pos[0], p.pos[1] - 700.f, p.pos[2]};
        const bool way = MapReady() && gMap->nav->Walkable(fa, fb);   // (a straight way to it on the ground)
        // close by; within 6 m not aiming at the doll (looking elsewhere, or its weapon down); on its floor (a rush at
        // one below a ledge's edge would carry it over)
        (void)unaware;
        const bool level = std::fabs(GroundOf(p.pos) - GroundOf(gMe.pos)) < 1000.f;
        if (!level || !(dist < 1500.f || (way && dist < 6000.f && !aimingAtMe))) { C.since = gFrame; return false; }
        if (gFrame - C.since > 300) { C.offUntil = gFrame + 600; C.since = gFrame; if (gDebug) Log("cqc: %s not reached", p.name.c_str()); return false; }
        // low: up first (rushed crouched it would crawl; lying, Circle grabs nothing: v133's audit, a Snake
        // lay 150 s at a soldier's feet trying)
        if (FightStandUp(p.name)) return true;
        if (Low()) return false;
        Act("cqc", p.name);
        if (!CqcWeapon(Hand())) {                                    // a CQC weapon in the hands first
            if (SwapTo(CqcWeaponCode())) { if (std::fabs(err) > 20.f * kDeg && gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep); return true; }
            if (!CqcWeapon(Hand())) { C.offUntil = gFrame + 600; return false; }
        }
        if (dist > 700.f) { Steer(p.pos[0] - gMe.pos[0], p.pos[2] - gMe.pos[2], 1.f); return true; }   // rushed
        if (std::fabs(err) > 30.f * kDeg) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep); return true; }
        Steer(0, 0, 0);
        if (!reacted || gFrame - C.releasedAt < 4) return true;     // (Circle let go a moment: a new press)
        // still first (Circle pressed walking is a punch: measured); 20 frames at most
        const int st = gMe.bits[0] & 0xFFFF;
        if (st != 3 && st != 5) { if (C.stillWait < 0) C.stillWait = gFrame; if (gFrame - C.stillWait < 20) return true; }
        C.stillWait = -1;
        C.phase = 1; C.at = gFrame; C.stunBefore = p.stun; C.scoreBefore = MyScore(0x36); C.grabAt = -1;
        C.stick = gCqcWay ? gCqcWay == 2 : (C.tries % 2) == 1;
        if (gDebug) Log("cqc: try %d at %s, %.2f m, %.0f deg off, weapon %x, %s", C.tries + 1, p.name.c_str(), dist / 1000.f, err / kDeg, Hand(),
                        C.stick ? "Circle with the stick toward it" : "Circle alone");
        return true;
    }
    Act("cqc", p.name);
    if (C.phase == 1) {                                              // Circle held (the stick toward it the first frames)
        gPad.buttons |= 0x2000;
        if (C.stick && t < 8) Steer(p.pos[0] - gMe.pos[0], p.pos[2] - gMe.pos[2], 1.f); else Steer(0, 0, 0);
        // the grab (the state word 0 is 2, the word 6 has 0x60000000): let go 8 frames later, the enemy thrown
        // down before it can struggle free (a doll held 40 frames got free once in three: measured)
        const bool grab = (gMe.bits[0] & 0xFFFF) == 2 && (gMe.bits[6] & 0x60000000) == 0x60000000;
        if (grab && C.grabAt < 0) { C.grabAt = gFrame; if (gDebug) Log("cqc: %s grabbed (%lld frames)", p.name.c_str(), t); }
        if ((C.grabAt >= 0 && gFrame - C.grabAt >= 8) || t >= 40) { C.phase = 2; C.at = gFrame; C.releasedAt = gFrame; }
        return true;
    }
    Steer(0, 0, 0);                                                  // let go, then what came of it (5 s at most: the
    const int score = MyScore(0x36);                                 // other player's state comes over the network)
    const bool thrown = p.down || Asleep(p) || (Lying(p.pos) && t > 10) || p.stun <= C.stunBefore - 9000 ||
                        (score >= 0 && C.scoreBefore >= 0 && score > C.scoreBefore);
    if (!thrown && t < 150) return true;
    ++C.tries; C.phase = 0; C.since = gFrame;
    Log("cqc: %s %s (%s; stamina %d -> %d, down %d, its state %x, CQC score %d -> %d, state %x)", p.name.c_str(), thrown ? "thrown" : "not thrown",
        C.stick ? "with the stick" : "Circle alone", C.stunBefore, p.stun, p.down ? 1 : 0, p.bits[0], C.scoreBefore, score, gMe.bits[0]);
    if (thrown) { if (!gCqcWay) gCqcWay = C.stick ? 2 : 1; C.offUntil = gFrame + 300; ++gCqcDone; }
    else { ++gCqcFailed; if (C.tries >= 4) { C.offUntil = gFrame + 900; C.tries = 0; } }
    return false;
}
// ---- the roll into an enemy (v134, probe 31): running (the state word 0 is 0x5002: the stick pushed full)
// straight at him, Cross pressed 1.4 to 2 m before him; the roll (the state 0x4240 from 8 frames after the
// press, 50 frames, then 0x4642 20 frames) knocks him down when it meets him in its first frames: his state
// 0x6000002 then lying (0x16000008, his hips 0.3 m up) about 2.5 s, 10000 of his stamina (30000) gone, three
// knocking him out; the objective he holds (the KEROTAN, the GA-KO, the microfilm) dropped where he falls.
// Pressed 2.2 m or more before him, the roll ends short or passes him by; walking, Cross crouches. Rolled:
// into an enemy holding the objective, into one close by when its guns are empty, and at level 3 and up
// sometimes before a CQC or a stun weapon's attack on an enemy not aiming at it; only on a straight, flat
// way to him 2.5 to 10 m ----
struct RollRun { int phase = 0, target = -1; long long at = 0, pressAt = -1, offUntil = -1; float lastDist = 0, closing = 0; const char* why = ""; bool hit = false; };
RollRun gRo;
int gRolls = 0, gRollHits = 0;                       // (this map: the rolls made, the ones that knocked down)
bool Rolling() { const uint32_t st = gMe.bits[0] & 0xFFFF; return st == 0x4240 || st == 0x4642; }
bool KnockedDown(const Player& p) { return (p.bits[0] & 0x16000000) != 0; }   // (by a roll: 0x6000002, then 0x16000008)
// a straight, flat way to him on foot: their grounds within 0.5 m (neither on a ladder), the feet's way walkable,
// nothing at the knees nor the hips (standing heights: lying or crouched, the same)
bool FlatWayTo(const Player& p) {
    if (!MapReady()) return false;
    const float gm = GroundOf(gMe.pos), gp = GroundOf(p.pos);
    if (std::fabs(gp - gm) > 500.f || p.pos[1] - gp > 1300.f || gMe.pos[1] - gm > 1300.f) return false;   // (not on a ladder)
    const float fa[3] = {gMe.pos[0], gm + 100.f, gMe.pos[2]}, fb[3] = {p.pos[0], gp + 100.f, p.pos[2]};
    const float ka[3] = {gMe.pos[0], gm + 350.f, gMe.pos[2]}, kb[3] = {p.pos[0], gp + 350.f, p.pos[2]};
    const float ha[3] = {gMe.pos[0], gm + 800.f, gMe.pos[2]}, hb[3] = {p.pos[0], gp + 800.f, p.pos[2]};
    return gMap->nav->Walkable(fa, fb) && LineClear(ha, hb) && LineClear(ka, kb);
}
bool RollGeom(const Player& p, float dist) { return dist >= 2500.f && dist <= 10000.f && FlatWayTo(p); }   // (a roll's way: 2.5 to 10 m)
bool ThrowableInHand() { const int h = Hand(); return h >= 0x13 && h <= 0x1D; }   // (a grenade, a trap, TNT: Square readies it)
bool SpawnGuarded(const Player& p);
bool RollWay(const Player& p, float dist) {
    // (on its feet in the third person view: still, walking, running or turning; no grenade in its hands; the
    // v134 audit: a doll in its first person view with a grenade readied stood crouched, the stick pushed for nothing)
    const uint32_t st0 = gMe.bits[0] & 0xFFFF;
    if ((st0 != 3 && st0 != 0x4802 && st0 != 0x5002 && st0 != 0x6002) || OnLadder() || ThrowableInHand() || RadioPressing()) return false;
    if (!p.alive || KnockedDown(p) || SpawnGuarded(p) || (p.bits[2] & 0x40000)) return false;   // (him on a ladder: no roll)
    return RollGeom(p, dist);
}
bool Carrying();
bool RollFrame(const Player& p, const char* why) {       // a frame of a roll into him: true while it has the pad
    RollRun& R = gRo;
    const float dist = Dist2D(gMe.pos, p.pos);
    auto end = [&](const char* how, long long offFor) {
        Log("roll: into %s (%s): %s", p.name.c_str(), R.why, how);
        R.phase = 0; R.offUntil = gFrame + offFor;
        return false;
    };
    if (R.phase == 0 || R.target != p.id) {
        if (gFrame < R.offUntil || !RollWay(p, dist) || Carrying()) return false;
        R = RollRun(); R.phase = 1; R.target = p.id; R.at = gFrame; R.lastDist = dist; R.why = why;
        if (gDebug) Log("roll: at %s, %.1f m (%s)", p.name.c_str(), dist / 1000.f, why);
    }
    const long long t = gFrame - R.at;
    if (!p.alive) return end("his death", 120);
    Act("rolling", p.name);
    StopPath();
    if (R.phase == 1) {                                  // the run at him, Cross when the roll (8 frames later) will meet him
        R.closing = 0.7f * R.closing + 0.3f * (R.lastDist - dist);   // (his place comes over the network in steps: smoothed)
        R.lastDist = dist;
        const bool running = (gMe.bits[0] & 0xFFFF) == 0x5002;
        if (t > 240 || dist > 12000.f || KnockedDown(p)) return end("not reached", 300);
        if (t >= 45 && !running) return end("not running", 600);   // (a wall the line did not show: not again for 10 s)
        if (gFrame - gHurtAt < 20 && dist > 6000.f) return end("hurt on the way", 300);
        for (const auto& k : gKnown) {                   // (another enemy aiming at it: the run given up)
            const Player* e = k.second.visible && k.first != p.id ? PlayerById(k.first) : nullptr;
            if (e && e->alive && (e->bits[1] & 0x4400) && std::fabs(Wrap(AngleTo(e->pos, gMe.pos) - e->facing)) < 20.f * kDeg) return end("another enemy aiming at it", 300);
        }
        Steer(p.pos[0] - gMe.pos[0], p.pos[2] - gMe.pos[2], 1.f);
        if (running && dist <= 1900.f && dist - 8.f * std::max(0.f, R.closing) <= 1250.f) {
            R.phase = 2; R.pressAt = gFrame; gPad.buttons |= 0x4000;
            if (gDebug) Log("roll: Cross at %.2f m from %s (closing %.0f mm a frame, his state %x)", dist / 1000.f, p.name.c_str(), R.closing, p.bits[0]);
            return true;
        }
        if (dist < 1100.f) return end(running ? "too close" : "not running", 300);
        return true;
    }
    if (R.phase == 2) {                                  // Cross 4 frames, the stick on him until the roll begins
        if (gFrame - R.pressAt < 4) gPad.buttons |= 0x4000;
        Steer(p.pos[0] - gMe.pos[0], p.pos[2] - gMe.pos[2], 1.f);
        if (Rolling()) { R.phase = 3; R.at = gFrame; return true; }
        if (gFrame - R.pressAt > 20) return end("no roll", 600);
        return true;
    }
    Steer(0, 0, 0);                                      // rolling: whether it knocked him down, its end
    if (KnockedDown(p) && !R.hit) { R.hit = true; ++gRollHits; }
    if (Rolling() && t < 90) return true;
    ++gRolls;
    return end(R.hit ? "knocked down" : "missed", R.hit ? 120 : 600);
}
// no gun with a round left (the fight's gun, the handgun, a sniper's rifle)
bool NoAmmo() {
    if (!Inv().ok) return false;
    for (int c : {MainGun(), Handgun(), SniperRifle()}) if (c > 0 && !Empty(c)) return false;
    return true;
}
// (v150-host beta 16, the weapons setting) no gun at all, the knife in its hands (a knife only or support only game: the
// game gives the knife and nothing else to fire): Square strikes with it at an enemy within reach, standing or down (its
// punches only stun: the knife only bench, two dolls at difficulty 10 for 3 minutes, never killed). Each strike logged
long long gKnifeAt = -1000;
bool KnifeOnly() { return Inv().ok && Hand() == 1 && MainGun() < 0 && Handgun() < 0 && SniperRifle() < 0; }
bool KnifeStrike(const Player& p, float dist) {
    if (!KnifeOnly() || dist > 1100.f || std::fabs(GroundOf(p.pos) - GroundOf(gMe.pos)) > 500.f) return false;
    Act("cqc", p.name);
    const float want = AngleTo(gMe.pos, p.pos), err = Wrap(want - Facing());
    if (std::fabs(err) > 20.f * kDeg) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep); return true; }
    const long long t = gFrame - gKnifeAt;
    if (t >= 30) { gKnifeAt = gFrame; Log("fight: a knife strike at %s (%.1f m%s)", p.name.c_str(), dist / 1000.f, Lying(p.pos) || p.down || KnockedDown(p) ? ", down" : ""); }
    if (gFrame - gKnifeAt < 5) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }
    return true;
}
// its guns empty: an enemy 1 to 3 m away walked up to for the punches, walking (Cross is never pressed running),
// on a straight, flat way only; no such way, or not 0.3 m closer in 2 s (4 s low: up first), left alone 5 s (a
// weapon box meanwhile). 1 walking, 0 not at this distance, -1 left alone
int DryWalkUp(const Player& p, float dist) {
    if (gFrame - gFi.walkLast > 2) gFi.walkSince = -1;   // (not asked a moment: the wait afresh)
    gFi.walkLast = gFrame;
    if (gRo.phase || dist < 1000.f || dist >= 3000.f) { gFi.walkSince = -1; return 0; }
    const bool way = FlatWayTo(p);
    if (way && (gFi.walkSince < 0 || dist < gFi.walkBest - 300.f)) { gFi.walkSince = gFrame; gFi.walkBest = dist; }
    if (!way || gFrame - gFi.walkSince > (Low() ? 240 : 120)) {
        if (gDebug) Log("fight: %s not reached for the punches (%s): left alone 5 s", p.name.c_str(), way ? "no closer in 2 s" : "no flat way");
        gDryLeftUntil[p.id] = gFrame + 300;
        return -1;
    }
    if (Scoped() && Handgun() > 0 && SwapTo(Handgun())) { gFi.walkSince = gFrame; Act("switching_weapon", p.name); return 1; }   // (no walking in the scope)
    if (FightStandUp(p.name)) return 1;
    Act("cqc", p.name);
    Steer(p.pos[0] - gMe.pos[0], p.pos[2] - gMe.pos[2], 0.6f);
    return 1;
}
bool CarrierOf(const Player& p);                     // (he holds the objective's item)

// (v137) what is seen of him from a point (the view's eye, the muzzle), the part wanted first: the head (kind 0), the
// chest (1; not with body: headshots only), his last shot's muzzle seen these 45 frames (a lean: his head by his gun,
// kind 0), a shoulder past a corner's edge (0.23 m aside, kind 2); false: nothing of him seen from there
bool EdgePart(const Player& p, const float* from, const float* chest, bool body, float* out, int* kind) {   // (his lean's head, a shoulder)
    const auto kn = gKnown.find(p.id);
    // ((v137) the muzzle's place counts while he is still by it, within 1.5 m: gone from there, nothing of him is seen)
    if (kn != gKnown.end() && gFrame - kn->second.peekAt < 45 && Dist2D(p.pos, kn->second.peek) < 1500.f && LineClear(from, kn->second.peek)) {
        memcpy(out, kn->second.peek, 3 * sizeof(float)); *kind = 0; return true;
    }
    const float dx = chest[0] - from[0], dz = chest[2] - from[2], l = std::max(1.f, std::sqrt(dx * dx + dz * dz));
    if (!body || l > 30000.f) return false;              // (a shoulder beyond 30 m: too small a part)
    for (const float s : {1.f, -1.f}) {
        const float sh[3] = {chest[0] - dz / l * 230.f * s, chest[1] + 150.f, chest[2] + dx / l * 230.f * s};
        if (LineClear(from, sh)) { memcpy(out, sh, sizeof sh); *kind = 2; return true; }
    }
    return false;
}
bool VisiblePart(const Player& p, const float* from, bool headFirst, bool body, float* out, int* kind) {
    float head[3], chest[3];
    HeadPoint(p, head); ChestPoint(p, chest);
    const float* pts[2] = {headFirst ? head : chest, headFirst ? chest : head};
    for (int i = 0; i < 2; ++i) {
        const bool isHead = pts[i] == head;
        if ((isHead || body) && LineClear(from, pts[i])) { memcpy(out, pts[i], 3 * sizeof(float)); *kind = isHead ? 0 : 1; return true; }
    }
    return EdgePart(p, from, chest, body, out, kind);
}
// (v137) the readied turn in place (docs section 3: "a push of 0.55 does not move a standing player", it turns him;
// the fights pushed 0.57 and walked: v136's state lines 0.48 m each 30 frames, motion137/dm's motion lines 72 to 81 %
// walking, 0.17 degree a frame): learned from the player's answer, 12 frames at a time, the hips walking (150 mm) a
// step down, the facing still (0.3 degree) a step up, 0.01 a step (the stick's bytes 69, 71, 72 about 0.55), 0.50 to 0.60
float gTurnPush = 0.55f;
long long gTpAt = -1;
float gTpFacing = 0.f, gTpPos[3] = {0, 0, 0};
float TurnPush() {
    if (gTpAt < 0 || gFrame - gTpAt > 14) { gTpAt = gFrame; gTpFacing = Facing(); memcpy(gTpPos, gMe.pos, sizeof gTpPos); return gTurnPush; }
    if (gFrame - gTpAt >= 12) {
        const float moved = Dist2D(gTpPos, gMe.pos), turned = std::fabs(Wrap(Facing() - gTpFacing)), was = gTurnPush;
        if (moved > 150.f) gTurnPush = std::max(0.50f, gTurnPush - 0.01f);
        else if (turned < 0.3f * kDeg) gTurnPush = std::min(0.60f, gTurnPush + 0.01f);
        if (gTurnPush != was && gDebug) Log("fight: the turn in place pushed %.2f (%.0f mm, %.1f degrees in 12 frames)", gTurnPush, moved, turned / kDeg);
        gTpAt = gFrame; gTpFacing = Facing(); memcpy(gTpPos, gMe.pos, sizeof gTpPos);
    }
    return gTurnPush;
}
// ((v141 beta 7) tier 10's cheat) the readied turn's stick: led past him 4 times the error within 6 degrees (2.5 to 30 degrees past the
// facing: the last degrees at 16 to 35 m took 47 to 143 frames with the elite tiers' 1.3), else 1.8 times it (170 at most); pushed
// a walk's 0.62 at least (the turn in place's 0.55 crept)
float CheatTurnWay(float err) {
    const float a = std::fabs(err), s = err < 0.f ? -1.f : 1.f;
    const float lead = a <= 6.f * kDeg ? std::max(2.5f * kDeg, std::min(30.f * kDeg, a * 4.f)) : std::min(170.f * kDeg, a * 1.8f);
    return Facing() + s * lead;
}
float CheatTurnPush(float err) { const float a = std::fabs(err); return a > 30.f * kDeg ? 0.72f : a > 8.f * kDeg ? 0.64f : 0.62f; }
// ((v141 beta 9) the game turns the body on to the stick's last way once the stick is let go (L1 held, a burst, the turn ended): the
// choke bench at 1.6 m, the stick led 1.8 times the error, then L1: the body went on 30 to 100 degrees past him, the auto aim let go
// at its cone, the turn back led past him the other way, again (1826 degrees in its first 10 s up, the duels: 4 auto aims in 5 within
// 2.5 m so). The last frame its stick's way was led past him by 4 degrees or more: L1 waits 3 frames after it (AutoAimFrame)
long long gLedAt = -100;

// ---- (v141 beta 6, aim) the game's auto aim (MGO1's "AA"; probe 31's step a, the aa3 bench, City Under Siege, the AK-47): L1 held
// with the gun readied, the body does not turn and the bullets go to the enemy the game locks. The state word 1's 0x1000 says an
// enemy is in the lock zone: within about 24 to 31 degrees of the facing at 3, 5 and 9.4 m, never at 35 degrees and more, with or
// without L1; L1's bullets hit 21 to 24 degrees off where the gun readied alone missed (its own hits stop at about 14). The third
// person's fights use it with the automatic guns and the handguns (never the first person view's weapons) within the tier's reach
// (10 m at most: the range measured), drawn once a fight by the tier (kFight: TRAINING never, PRO always). Him within 30 degrees of
// the facing: the reaction (the autoaim family) counted from there; within the tier's cone, L1 held and the stick still (no turn by
// hand under L1: not measured); the shot once the game's lock is on, L1 held 2 frames. Let go when he leaves the cone or the reach,
// when the lock does not come in 12 frames (a turn by hand, 1 s: a narrower zone, a box, a wall), when the fight ends (lost, dead),
// the weapon changes, the first person view, the radio or another move takes the pad (a frame without it). Another enemy in sight
// nearer the facing within the zone (the game may lock that one): let go, the turn toward its own target first.
// MGO1_DOLL_AUTOAIM=0: never (the A/B benches); MGO1_DOLL_AA_RANGE (m): the reach's ceiling (tests) ----
struct AutoAim {
    int target = -1, want = 0;                  // the fight it was drawn for; drawn (1: used in this fight)
    bool on = false;                            // L1 held
    long long zoneAt = -1, readyAt = -1, onAt = -1, lastAt = -100, offUntil = -1, noLockSince = -1;
    int spells = 0, shots = 0;                  // this fight's spells of L1, its pulls with it
    const char* why = "";                       // why it is not firing with it now (the AI's lines), "" when it is
};
AutoAim gAa;
int gAaSpells = 0, gAaShots = 0;                // (this minute's: AiTraceFrame)
bool AutoAimOn() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_AUTOAIM"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
float AaRangeMax() { static float r = -1.f; if (r < 0.f) { const char* e = getenv("MGO1_DOLL_AA_RANGE"); r = e && atof(e) > 0 ? (float)atof(e) * 1000.f : 10000.f; } return r; }
bool AaWeapon(int c) { return c == 0xA || c == 0xB || c == 0xC || c == 0xD || c == 5 || c == 6 || c == 7 || c == 8; }   // (the automatic guns, the handguns)
bool LockOn() { return (gMe.bits[1] & 0x1000) != 0; }
bool VsBox();                                   // (an enemy in his box: the lock awaited, below)
void AimSay(const char* mode, const Player& p, float dist, float err, const char* why) {
    if (AiDebug()) Log("DOLL AIM: mode=%s L1=%d target=%s dist=%.1f ang=%.0f lock=%d reason=%s", mode, (gPad.buttons & 0x0400) ? 1 : 0, p.name.c_str(), dist / 1000.f,
                       err / kDeg, LockOn() ? 1 : 0, why);
}
void AaRelease(const Player& p, float dist, float err, const char* why) {
    if (!gAa.on) return;
    gAa.on = false; gAa.onAt = -1; gAa.noLockSince = -1;
    gPad.buttons &= ~0x0400u;
    AimSay("MANUAL", p, dist, err, why);
}
void AaDraw(int id) {                           // (drawn once a fight; (v141 beta 7) tier 10's cheat: always)
    if (gAa.target == id) { if (CheatOn() && AutoAimOn()) gAa.want = 1; return; }
    gAa = AutoAim(); gAa.target = id;
    gAa.want = AutoAimOn() && (CheatOn() || Rnd01() < Fk().aaUse) ? 1 : 0;
}
// the fight's start: the auto aim would take this fight (its draw, the gun it will fight with, within its reach): no first person view
bool AaPrefers(const Player& p, float dist, int gun) {
    AaDraw(p.id);
    return gAa.want && AaWeapon(gun) && dist <= std::min(Fk().aaReach, AaRangeMax()) && !HeadshotsOnly();
}
// ((v141 beta 9) this fight drawn for the auto aim, its gun one, him within its reach (1 m more): L1 may be held at once
bool AaMayTake(const Player& p, float dist) {
    return gAa.target == p.id && gAa.want && AaWeapon(Hand()) && !HeadshotsOnly() &&
           dist <= std::min(CheatOn() ? 10000.f : Fk().aaReach, AaRangeMax()) + 1000.f;
}
// another enemy in sight within the lock zone and nearer the facing than him by 4 degrees: the game may lock that one
bool AaOtherCloser(const Player& p, float aerr) {
    for (const auto& kn : gKnown) {
        if (kn.first == p.id || !kn.second.visible) continue;
        const Player* o = PlayerById(kn.first);
        if (!o || !Enemy(*o) || !o->alive || Dist2D(gMe.pos, o->pos) > AaRangeMax() + 2000.f) continue;
        const float oe = std::fabs(Wrap(AngleTo(gMe.pos, o->pos) - Facing()));
        if (oe < 31.f * kDeg && oe + 4.f * kDeg < aerr) return true;
    }
    return false;
}
// a frame of the third person's fight: L1 held or not; true when a shot with it is allowed now (the facing's own tolerance not
// needed). clear: a line of fire to him (no lock through a wall). *still: L1 held, the stick left alone
bool AutoAimFrame(const Player& p, float dist, bool clear, bool* still) {
    *still = false;
    AaDraw(p.id);
    const float err = Wrap(AngleTo(gMe.pos, p.pos) - Facing()), aerr = std::fabs(err);
    const FightSkill& f = Fk();
    if (gAa.on && gFrame - gAa.lastAt > 1) AaRelease(p, dist, err, "another_move");   // (a frame without it: let go)
    gAa.lastAt = gFrame;
    const auto kn = gKnown.find(p.id);
    const bool visible = kn != gKnown.end() && kn->second.visible;
    // (the first reason that holds; L1 on, the reach 1 m longer and the cone 3 degrees wider: no flicker at the edge). ((v141 beta 7)
    // tier 10's cheat: within 10 m and the 24 degrees measured, at once, no settling)
    const bool cheat = CheatOn();
    const float reach = std::min(cheat ? 10000.f : f.aaReach, AaRangeMax()) + (gAa.on ? 1000.f : 0.f), cone = ((cheat ? 24.f : f.aaCone) + (gAa.on ? 3.f : 0.f)) * kDeg;
    const char* no = !gAa.want ? "not_drawn" : !AaWeapon(Hand()) ? "weapon" : HeadshotsOnly() ? "headshots_only" : dist > reach ? "outside_autoaim_range" :
                     !visible ? "target_hidden" : !clear ? "no_line_of_fire" : Reloading() ? "reloading" : RadioPressing() ? "radio" :
                     gFrame < gAa.offUntil ? "no_lock_lately" : aerr > 30.f * kDeg ? "outside_lock_zone" : nullptr;
    if (no) { gAa.why = no; if (strcmp(no, "no_line_of_fire") && strcmp(no, "reloading")) gAa.zoneAt = -1; AaRelease(p, dist, err, no); return false; }
    if (gAa.zoneAt < 0) { gAa.zoneAt = gFrame; gAa.readyAt = gFrame + (gAa.spells > 0 ? (cheat ? 1 : 2) : ReactFrames(RF_AUTOAIM)); }   // (again this fight: at once)
    if (aerr > cone) { gAa.why = "outside_autoaim_cone"; AaRelease(p, dist, err, "outside_autoaim_cone"); return false; }   // (the turn by hand first)
    if (AaOtherCloser(p, aerr)) { gAa.why = "other_enemy_in_lock"; AaRelease(p, dist, err, "other_enemy_in_lock"); return false; }
    if (gFrame < gAa.readyAt) { gAa.why = "autoaim_reaction"; return false; }
    if (!gAa.on && gFrame - gLedAt < 3) { gAa.why = "turn_settling"; return false; }   // ((v141 beta 9) the stick's way on him first)
    gPad.buttons |= 0x0400;                              // L1
    *still = true;
    if (!gAa.on) {
        gAa.on = true; gAa.onAt = gFrame; ++gAa.spells; ++gAaSpells;
        if (gEn.id == p.id && gEn.aaAt < 0) gEn.aaAt = gFrame;
        AimSay("AUTOAIM", p, dist, err, gAa.spells == 1 ? "engaged" : "engaged_again");
    }
    if (!LockOn()) {                                     // (the game's lock not on yet: 12 frames, then by hand 1 s)
        if (VsBox() && InBox(p)) { gAa.noLockSince = -1; gAa.why = "target_in_box"; return false; }   // (his box: the lock awaited, his pop out)
        if (!(gMe.bits[1] & 0x4000)) { gAa.noLockSince = -1; gAa.why = "autoaim_gun_not_ready"; return false; }   // (the lock with the gun readied: probe 31)
        if (gAa.noLockSince < 0) gAa.noLockSince = gFrame;
        if (gFrame - gAa.noLockSince >= 12) { gAa.offUntil = gFrame + 60; gAa.why = "no_lock"; AaRelease(p, dist, err, "no_lock"); *still = false; return false; }
        gAa.why = "autoaim_lock_wait";
        return false;
    }
    gAa.noLockSince = -1;
    if (gFrame - gAa.onAt < 2 && !cheat) { gAa.why = "autoaim_settling"; return false; }
    gAa.why = "";
    return true;
}
// (v141 beta 6, aim) hurt a moment ago by him (aiming at the doll, his weapon up): the defense family's reaction
bool HitBy(const Player& p) { return gFrame - gHurtAt < 90 && std::fabs(Wrap(AngleTo(p.pos, gMe.pos) - p.facing)) < 20.f * kDeg && (p.bits[1] & 0x4400) != 0; }
// (v141 beta 6, aim) an enemy in his cardboard box: the third person's bullets pass over it and the game's lock never takes it (the
// aim_radio bench, probe 31's step a against a boxed victim, City Under Siege: two AK-47 bullets straight at him 5.2 m away, with L1
// and without, his life untouched; 14 degrees off the same; the lock flag never on). A fight drawn for the auto aim waits for his pop
// out: the third person (no first person view for him), L1 held on the box with the gun readied, no shot (they would pass over), the
// lock awaited however long (AutoAimFrame): it comes as he leaves the box, the shot with it. (The aim_ab1 bench: the first person view
// at the box's low middle found no line of fire 400 frames.) The first person view already on keeps him, its aim at the box's middle,
// 0.5 m above his ground. MGO1_DOLL_VS_BOX=0: as before (the third person's shots)
bool VsBox() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_VS_BOX"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
bool BoxWaitFor(const Player& p) { return VsBox() && gAa.target == p.id && gAa.want && AaWeapon(Hand()) && InBox(p); }
int gBoxWaitId = -1; long long gBoxWaitSince = -1;    // (the wait on his box: 2.5 s, then the first person view at it)
// (v141 beta 6, aim) the first person view with no line of fire to him (its eye sees him, its gun 0.15 m lower does not: a low wall's
// top, a rail; the aim_ab1 bench's first minute: 30 % of a PRO doll's time with an enemy in sight): from EXPERT up, 45 frames so (90
// at EXPERT): crouched, up again (its gun 0.54 m higher); standing, the third person (its step aside searches the line 0.7 to 2.1 m to
// each side), no first person view for a part of him seen 2 s (else the two views in turn). MGO1_DOLL_FPV_UNBLOCK=0: as before
long long gFpvBlockedSince = -1, gNoFpvUntil = -1;
void FpvBlocked(const Player& p, float dist) {
    static const bool off = getenv("MGO1_DOLL_FPV_UNBLOCK") && *getenv("MGO1_DOLL_FPV_UNBLOCK") == '0';
    if (off || strcmp(gReason, "no_line_of_fire") || LevelNow() < 5.f || HeadshotsOnly() || Scoped() || SniperInHand()) { gFpvBlockedSince = -1; return; }
    if (gFpvBlockedSince < 0) gFpvBlockedSince = gFrame;
    if (gFrame - gFpvBlockedSince < (CheatOn() ? 15 : LevelNow() >= 6.f ? 45 : 90)) return;   // ((v141 beta 7) tier 10's cheat: 15)
    gFpvBlockedSince = -1;
    if (gLean.low) {
        gLean.low = false; gLean.upAt = gFrame; gLean.lowAt = gFrame;
        if (gDebug) Log("fight: no line of fire at %s from the view: standing to shoot", p.name.c_str());
        return;
    }
    if (gDebug) Log("fight: no line of fire at %s from the view %.1f m away: the third person, a step aside", p.name.c_str(), dist / 1000.f);
    gFi.fpv = false; gFi.fpvSince = -1; gFi.fpvLater = false; gFi.readySince = -1; gFi.asideSide = 0; gNoFpvUntil = gFrame + 120;
}

// ((v150-host beta 16) the parity mode: a fight's first shot not before the virtual doll's would leave in the same sight: its reaction
// (gFi.reactAt: the look's lateness in, the bullet's 3 frames out), its turn to him at its level's rate beyond that (the angle at the
// fight's start), then its aim on him for its level's settling (kVdollLevel's settle). The doll's own aim must be on him too (the third
// person within its tolerance or the auto aim's lock, the view on him): its slower turn is not charged twice. True when off
bool ParitySettled(bool aimed) {
    static long long fight = -2; static int turn = 0;
    if (!Parity()) return true;
    if (fight != gFi.startAt) { fight = gFi.startAt; turn = std::max(0, (int)std::ceil(gEn.ang / Vd().turn) - Vd().react); }
    if (gFi.shots > 0) return true;
    return aimed && gFrame >= gFi.reactAt + turn + Vd().settle;
}
// ((v150-host beta 18) the third person's bullet: 3.4 degrees down from the muzzle along the facing (probe 44); its height where he
// stands within his body (his ground + 0.15 m to his head's top). MGO1_DOLL_TPV_LEVEL=0: not checked (as before)
bool TpvLevelOn() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_TPV_LEVEL"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
int gTpvLevelViews = 0;                          // (the views opened for it: the minute's line)
bool TpvLevelHits(const Player& p, const float* mz, float dist) {
    // (only him on another floor, 1.2 m or more above or below: the bullet's drop on flat ground far off is not measured against a
    // player yet, those shots stay as before)
    if (std::fabs(p.pos[1] - gMe.pos[1]) < 1200.f) return true;
    float hd[3]; HeadPoint(p, hd);
    return aim::TpvReaches(mz[1], dist, GroundOf(p.pos), hd[1] + 120.f);
}
void DoFight(const Player& p) {
    // (the one fought out of sight a moment: the gun on where it was last seen, not through the wall, no shot
    // nor roll at nothing)
    const auto k = gKnown.find(p.id);
    gFi.holdFire = !OldFight() && k != gKnown.end() && !k->second.visible && (gFrame - k->second.seen <= TargetKeep() || gFi.finishSince >= 0);
    if (gFi.holdFire && !CheatOn()) {                   // ((v141 beta 7) tier 10's cheat: the gun on where he is, behind his cover)
        Player q = p; memcpy(q.pos, k->second.seenPos, sizeof q.pos);
        gRo.offUntil = std::max(gRo.offUntil, gFrame + 1);
        FightFrame(q);
    } else FightFrame(p);
    const bool square = (gPad.buttons & 0x8000) && gPad.pressures[11] >= 0x30;
    if (!square) gFi.squareSince = -1; else if (gFi.squareSince < 0) gFi.squareSince = gFrame;
}
void FightFrame(const Player& p) {
    const Skill& sk = Sk();
    Known& k = gKnown[p.id];
    const float dist = Dist2D(gMe.pos, p.pos);
    const bool hs = HeadshotsOnly();
    if (gFi.target != p.id) {
        // (a switch from a fight in the first person view, its view settled: the view kept, no new settle, its aim's
        // target followed afresh; from a third person fight with Square held: readied still)
        const bool viewOn = gFi.target >= 0 && gFi.fpv && gFi.fpvSince >= 0 && gFrame - gFi.fpvSince > 20 && !SniperInHand();
        const long long readied = gFi.target >= 0 && !gFi.fpv && gFi.squareSince >= 0 ? gFi.readySince : -1;
        const bool switched = gFi.target >= 0;               // ((v141 beta 6, aim) from another target: its own reaction)
        FightStop(); gFi.target = p.id; gFoeNow = p.id; gFi.lifeAt0 = p.life; gFi.stunAt0 = p.stun;
        // (v141 beta 6, aim) the reaction drawn once from its family (ReactFrames): hurt a moment ago by him (defense, from the hit or
        // his sight), a switch from another target (target_switch, from now), else his first sight (perception); seen again after a
        // while in the same fight, the combat family (below)
        {
            const int fam = HitBy(p) ? RF_DEFENSE : switched ? RF_TARGET_SWITCH : RF_PERCEPTION;
            const long long first = k.first >= 0 ? k.first : gFrame;
            const long long base = fam == RF_TARGET_SWITCH ? gFrame : fam == RF_DEFENSE ? std::max(first, gHurtAt) : first;
            gFi.reactBase = first; gFi.reactAt = base + ReactFrames(fam); gFi.reactFamily = kReactNames[fam];
        }
        gLean.side = 0; gLean.at = gLean.sideAt = -100000; gLean.clearN = 0;   // (the lean chosen again; the posture kept: the body is where it is)
        // ((v137) the posture as it is: a crouch kept from the last fight, the doll up since, was pressed again at once:
        // motion137/dm/d3.txt f13716, Cross 12 frames after R1)
        { const uint32_t st = gMe.bits[0] & 0xFFFF; gLean.low = st == 5 || st == 0x85; }
        // (a lying one: aimed at in the first person view; the thermal goggles on: the view more often)
        gFi.fpv = SniperInHand() ? dist > 5000.f : (dist > 6000.f && Rnd01() < sk.fpv + (GogglesOn() ? 0.25f : 0.f)) || (Lying(p.pos) && dist > 1500.f);
        if (!gFi.fpv && Prone() && InCrawl(gMe.pos) && dist > 900.f) gFi.fpv = true;   // ((v137) lying in a vent: its gun in the view)
        if (hs) gFi.fpv = true;                              // (headshots only: the first person view always)
        const bool quietFirst = p.id == gFpvFor;             // ((v137) kept for the third person's turn rule below)
        if (p.id == gFpvFor && dist > 1500.f && !SniperInHand()) gFi.fpv = true;   // (a first shot at the head: one who has not seen it)
        gFpvFor = -1;
        // ((v141 beta 7) tier 10's cheat: its ambush's view kept only for him showing within 25 degrees of the point it waits on and of
        // the view itself (one 48 degrees off a view still turning took 78 frames to aim); the view still coming on (R1 held): kept)
        float vmPre[16];
        const bool viewFar = (gMe.bits[0] & 0x80) && ps2world::ViewMatrix(vmPre) && std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - std::atan2(vmPre[8], vmPre[10]))) > 25.f * kDeg;
        const bool preNear = gFrame < gPre.until && std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - AngleTo(gMe.pos, gPre.at))) < 25.f * kDeg && !viewFar;
        const bool cheat = CheatOn();
        // ((v141 beta 10) him in sight from its peek's lean, its view on: that view kept whatever the angle (the bench base_b9rel: fights
        // begun from the peek dropped the view and the lean, him lost 38 frames later, again and again)
        const bool peekSprung = cheat && !CheatWaitsB8() && gPk.id == p.id && gPk.leanAt >= 0 && (gMe.bits[0] & 0x80) && !SniperInHand();
        const bool preView = (gFrame < gPre.until && gPre.fpv && !SniperInHand() && (!cheat || preNear)) || peekSprung;
        if (preView) {                                       // (a corner held: the view on him already, crouched as it was)
            gFi.fpv = true; gFi.fpvSince = cheat && gCheat.viewAt >= 0 ? std::min(gCheat.viewAt, gFrame - 1) : gFrame - 21; gAimSince = gFrame;
            if (peekSprung) gFi.fpvSince = std::min(gFi.fpvSince, gFrame - 21);   // ((v141 beta 10) its lean kept: LeanFrame's 12 frames past)
            if ((gMe.bits[0] & 0xFFFF) == 0x85) { gLean.low = true; gLean.clearN = 2; gLean.lowAt = gFrame; }
        } else if (viewOn && dist > 1500.f) {
            gFi.fpv = true; gFi.fpvSince = gFrame - 21; gAimY.tgtAt = gAimP.tgtAt = -100000;
            if (gDebug) Log("fight: the view kept on from the last target");
        }
        // ((v141 beta 8) tier 10's peek: him in sight from its lean, its lean kept (the side, its next choice in 1.5 s))
        if (preView && cheat && gPk.id == p.id && gPk.leanAt >= 0) {
            gLean.side = gPk.side; gLean.sideAt = gPk.leanAt; gLean.at = gFrame; gLean.next = 90; gLean.around = true;
            ++gPk.sprung;
            if (gDebug) Log("fight: %s in sight from its peek's lean (%lld frames leaned)", LogWho(p).c_str(), gFrame - gPk.leanAt);
        }
        if (gPk.id >= 0) CheatPeekEnd(gPk.id == p.id ? "in sight" : "another fought");
        // (v137) where it looks: the view's yaw in the first person view, else its facing
        float look = Facing(), vm0[16];
        const bool inView = (gMe.bits[0] & 0x80) != 0;
        if (inView && ps2world::ViewMatrix(vm0)) look = std::atan2(vm0[8], vm0[10]);
        const float ang = std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - look));
        if (inView && gFi.fpv && gFi.fpvSince >= 0 && gFrame - gFi.fpvSince > 20) gFi.viewOnAt = gFrame - 30;   // (the view on already: no settle)
        // (v137) levels 4 and 5, him more than 50 degrees off where it looks and not in the view yet: the third person's
        // readied turn first (3 to 5 degrees a frame against the view's 1 to 2, v136's state lines; the view's own entry
        // 16 to 35 frames), the first person view after the turn beyond 15 m. Not lying (up first: 45 frames and more; in a
        // vent it stays down), not the quiet first shot at one who has not seen it (HoldFireAt's)
        if (gFi.fpv && Lv() >= 4 && !hs && !inView && !SniperInHand() && !Lying(p.pos) && !Prone() && ang > 50.f * kDeg && !quietFirst) {
            gFi.fpv = false; gFi.fpvLater = dist > 15000.f;
            if (gDebug) Log("fight: %s %.0f degrees off: the third person's turn first%s", p.name.c_str(), ang / kDeg, gFi.fpvLater ? ", the view after it" : "");
        }
        // (v141 beta 2) NIGHTMARE: the quick aim (a real player took 37 kills for 4 deaths against five of them: their first
        // shot came 1.5 s after the sight, median, the first person view's 2.1 s, the third person's 1.1 s). Within 10 m the
        // third person only (his body wide enough there: the shot once turned); farther and more than 12 degrees off, the
        // readied turn first (3 to 5 degrees a frame, the view's 1 to 2), the view once faced within 4 degrees beyond 10 m.
        // MGO1_DOLL_QUICKAIM=0: as before (benches)
        if (QuickAim() && Nightmare() && !hs && !inView && !SniperInHand() && !Lying(p.pos) && !Prone() && !quietFirst) {
            if (gFi.fpv && dist < 10000.f) { gFi.fpv = false; gFi.fpvLater = false; }
            else if (gFi.fpv && ang > 12.f * kDeg) { gFi.fpv = false; gFi.fpvLater = true; }
        }
        // (v141 beta 6, aim) the auto aim's fight (drawn by the tier, the gun it fights with, within its reach): the third person with L1
        // rather than the first person view (its own entry 16 to 35 frames, then the aim). Not the view kept on (a corner held, the last
        // target's), not a lying one (the third person's shots pass over him), not the quiet first shot
        if (gFi.fpv && gFi.fpvSince < 0 && !hs && !inView && !SniperInHand() && !Lying(p.pos) && !Prone() && !quietFirst &&
            AaPrefers(p, dist, FightWeapon(dist, Hand(), AsleepDown(p)))) {
            gFi.fpv = false; gFi.fpvLater = false;
            if (gDebug) Log("fight: %s at %.1f m: the auto aim (L1) rather than the first person view", p.name.c_str(), dist / 1000.f);
        }
        // (v141 beta 6, aim) him in his cardboard box, the auto aim drawn: the third person, L1 on the box (BoxWaitFor)
        if (VsBox() && InBox(p) && gFi.fpv && gFi.fpvSince < 0 && !hs && !SniperInHand()) { AaDraw(p.id); if (gAa.want && AaWeapon(Hand())) { gFi.fpv = false; gFi.fpvLater = false; } }
        // ((v141 beta 7) tier 10's cheat: the third person within 10 m (the auto aim's reach, its readied turn led), the first person
        // view beyond once faced within 6 degrees (its body turned first: the view turns 1 to 2 degrees a frame, the readied turn 3 to
        // 5; beyond 10 m the third person's body shots took 160 to 200 frames to his fall, the view's head shots 40 to 70); a lying
        // one: the view (the third person's shots pass over him); its ambush's view: kept. His head alone seen, its gun's line blocked:
        // the view once faced (the third person's own rules, below))
        if (cheat && !hs && !SniperInHand() && !quietFirst && !preView) {
            if (Lying(p.pos) && dist > 1500.f) gFi.fpv = true;
            else if (dist <= CheatViewFrom()) { gFi.fpv = false; gFi.fpvLater = false; }
            else if (inView && ang <= 6.f * kDeg) gFi.fpv = true;
            else { gFi.fpv = false; gFi.fpvLater = true; }
            if (!gFi.fpv) gFi.fpvSince = -1;
        }
        if (!gFi.fpv && readied >= 0) gFi.readySince = readied;
        gFi.startAt = gFrame;
        gFi.cqc = !CheatOn() && Rnd01() < sk.cqc;             // ((v141 beta 7) tier 10's cheat: its gun, punches only with its guns empty)
        Act(gFi.fpv ? "aiming_head" : "fighting", p.name);
        if (gDebug) Log("fight: %s at %.0f m%s", p.name.c_str(), dist / 1000.f, gFi.fpv ? " (first person view)" : "");
        gEn = Engage(); gEn.id = p.id; gEn.sight = k.first >= 0 ? std::min(k.first, gFrame) : gFrame; gEn.start = gFrame; gEn.ang = ang / kDeg; gEn.dist = dist;
        gEn.lv = LevelNow(); gEn.fpv = gFi.fpv; gEn.weapon = Hand(); gEn.life0 = gEn.lifeLast = p.life; gEn.stunLast = p.stun; gEn.seen = k.by;
        if (gFi.fpv && gFi.viewOnAt >= 0) gEn.ready = gFrame;   // (the view kept on from the last target: ready now)
        if (gBpPendingId == p.id && gFrame - gBpPendingAt < 180) { ++gEn.boxPops; gBpPendingId = -1; }   // ((v141 beta 6, aim) its pop out at him)
        // ((v141 beta 7) its gun waiting on where he showed: its ambush's view, or readied; its first hurt from 1 s before the sight)
        gEn.pre = preView && gFi.fpv ? "view" : gFrame < gPre.until && preNear ? "aim" : "none";
        if (gHurtAt >= gEn.sight - 60) gEn.hurt = gHurtAt;
        if (cheat && preView && gFi.fpv) ++gCheat.ambushes;
        gCheat.shoulderSince = -1;
    }
    if (gEn.id == p.id && gEn.hurt == -100000 && gHurtAt >= gEn.sight - 60) gEn.hurt = gHurtAt;
    if ((p.life < gEn.lifeLast || p.stun < gEn.stunLast - 500) && gEn.hit < 0 && gEn.id == p.id) gEn.hit = gFrame;   // (v137) the first hit (life, a dart's stamina)
    if (gEn.id == p.id) { gEn.lifeLast = p.life; gEn.stunLast = p.stun; }
    if (gFi.finishSince < 0) StopPath();                 // (walking up to a sleeping one: its path kept)
    gFoughtAt[p.id] = gFrame;
    // shots for nothing: 8 pulls without the target hurt (its life nor its stamina down): a cover in the way
    // (the map's collision is not the bullets'), left alone 10 s unless close or hurting the doll: it moves on
    if (gFi.dryLife < 0 || p.life < gFi.dryLife || p.stun < gFi.dryStun) { gFi.dryShots = 0; gFi.dryLife = p.life; gFi.dryStun = p.stun; }
    // ((v141 beta 7) tier 10's cheat: 10. (v141 beta 8) 5 bursts within 12 m (a doll firing into a rock 10 times looks like no player:
    // Snake's video of 14 October); a bullet a pull from 12 m still 10 (5 ended open fights at 23 to 29 m in a bench, b8_duel3's first
    // run: its bullets there miss now and then, the head's own))
    if (gFi.dryShots >= (CheatOn() ? (dist >= kCheatFarShot ? 10 : 5) : 8) && dist > 8000.f && gFrame - gHurtAt > 120) {
        // ((v141 beta 10) tier 10: his head or chest in its eye's line, its misses (recoil, a crouch) are no cover: up, shot at again,
        // once a fight (the bullets' collision is not the map's); else left alone 2 s, not 10)
        static std::map<int, long long> againAt;
        if (CheatOn() && !CheatWaitsB8()) {
            float eyeNow[3], hdNow[3], chNow[3]; MyEye(eyeNow); HeadPoint(p, hdNow); ChestPoint(p, chNow);
            const bool inLine = LineClear(eyeNow, hdNow) || LineClear(eyeNow, chNow);
            const auto ag = againAt.find(p.id);
            const bool spent = ag != againAt.end() && gFrame - ag->second < 600;
            if (inLine && !spent) {
                againAt[p.id] = gFrame;
                if (gDebug) Log("fight: %s shot at %d times for nothing, in its line: up and again", LogWho(p).c_str(), gFi.dryShots);
                gFi.dryShots = 0; gFi.dryLife = p.life; gFi.dryStun = p.stun;
                if (Low()) SetLow(false);
            } else {
                if (gDebug) Log("fight: %s shot at %d times for nothing: a cover in the way, left alone %d s", LogWho(p).c_str(), gFi.dryShots, inLine ? 10 : 2);
                gCoveredUntil[p.id] = gFrame + (inLine ? 600 : 120);
                FightStop(); gFightEndAt = gFrame;
                return;
            }
        } else {
            if (gDebug) Log("fight: %s shot at %d times for nothing: a cover in the way, left alone 10 s", p.name.c_str(), gFi.dryShots);
            gCoveredUntil[p.id] = gFrame + 600;
            FightStop(); gFightEndAt = gFrame;
            return;
        }
    }
    // (v137) the aim's own error drifts to a new one drawn every 45 frames (a full swing in 20 frames): drawn at once, each
    // jump turned the readied player again, walking (the fights' "dance")
    if (gFrame - gFi.noiseAt > 45) { gFi.noiseTo = (Rnd01() * 2.f - 1.f) * sk.noiseDeg * kDeg; gFi.noiseAt = gFrame; }
    { const float st = sk.noiseDeg * kDeg / 10.f; gFi.noise += std::max(-st, std::min(st, gFi.noiseTo - gFi.noise)); }
    if (CheatOn() && !Parity()) gFi.noise = gFi.noiseTo = 0.f;   // ((v141 beta 7) tier 10's cheat: no error of its own, a smoke nothing to it;
                                                         // (v150-host beta 16) the parity mode: the virtual doll's error)
    // its aim's own error: through a smoke five times it (and never the head), the thermal goggles on 0.4 of it
    const bool smoke = !CheatOn() && SmokeBetween(p), goggles = GogglesOn();
    const float aimK = smoke ? 5.f : goggles ? 0.4f : 1.f;
    if (smoke != gFi.smoke) { gFi.smoke = smoke; if (gDebug) Log("fight: %s %s a smoke", p.name.c_str(), smoke ? "behind" : "out of"); }
    // (a corner held, him come round it: no surprise, the reaction short)
    const bool expected = gFrame < gPre.until && std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - AngleTo(gMe.pos, gPre.at))) < 12.f * kDeg;
    // ((v141 beta 6, aim) the reaction drawn at the fight's start from its family; in sight again after a while (Perceive: 30 frames
    // unseen), the combat family's from then; expected, 8 frames at most from his sight)
    if (k.first >= 0 && k.first > gFi.reactBase) { gFi.reactBase = k.first; gFi.reactAt = k.first + ReactFrames(RF_COMBAT); gFi.reactFamily = kReactNames[RF_COMBAT]; }
    const bool reacted = gFrame >= gFi.reactAt || (expected && gFrame - k.first >= std::min(sk.react, 8));
    if (reacted && gEn.react < 0 && gEn.id == p.id) gEn.react = gFrame;
    // (v137) a life's first 2 s, or a blow going on (the state word 1's 0x100: the deploy's punch, a hit taken): no CQC
    // try nor punch (v136's audit, dm/d3.txt f35904: Circle every 10 frames 1.5 s at its spawn, the word 1's 0x100 the
    // whole time, nothing came of it; then knocked out)
    const bool cqcOff = (LifeFrames() >= 0 && LifeFrames() < 120) || (gMe.bits[1] & 0x100) != 0;
    // the gun that does not come up (third person, standing, Square held, the state word 1 with 0x8000 and
    // without 0x400 the whole time: its muzzle against a wall, probe 27): the trigger let go, a step back
    // from where it faces, 25 frames. (The audit: the first person view, walking, turning have other words:
    // not this)
    if (gFrame < gFi.backUntil) { if (FightStandUp(p.name)) return; Act("fighting", p.name); Steer(std::sin(gFi.backDir), std::cos(gFi.backDir), kStep); return; }
    // against a wall (the state word 0: 2, 4 crouched, 0x82 and 0x84 in its first person view; walked into
    // by a turn or a step), its back to it: its facing held, the view's turn limited (v133's audit: 10 s
    // spells in that view, aiming at an enemy behind it, on Lost Forest in two runs). An enemy more than 45
    // degrees off its facing: off the wall first, the buttons let go, a step away from the wall (the
    // nearest hit of 16 rays around it; else along its facing), then toward the enemy, then aside (20 frames
    // each); the steps counted again only after a second off the wall (v134's audit: a view's frames between
    // them started them anew, a Snake 45 s against a wall on Pillbox Purgatory)
    {
        const uint32_t st0 = gMe.bits[0] & 0xFFFF;
        const bool wall = AgainstWall() && gCq.phase == 0;   // (a CQC grab is 2 too)
        if (wall) gFi.offWallSince = -1;
        // (the wall's first person view on though not asked for, left on after a lean: R1 held 40 frames, then let
        // go, once a second at most; v134's audit saw it end so)
        if ((st0 == 0x82 || st0 == 0x84) && !gFi.fpv && !Scoped() && gCq.phase == 0) {
            if (gFi.wallViewOffSince < 0) gFi.wallViewOffSince = gFrame;
            if (gFrame - gFi.wallViewOffSince >= 15) {           // (not the view's own end, a few frames)
                if (gFi.r1HoldUntil < 0 || gFrame - gFi.r1HoldUntil > 60) gFi.r1HoldUntil = gFrame + 40;
                if (gFrame < gFi.r1HoldUntil) { gPad.buttons |= 0x0800; Steer(0, 0, 0); Act("fighting", p.name); return; }
            }
        } else gFi.wallViewOffSince = -1;
        if (wall && std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - Facing())) > 45.f * kDeg && gFi.wallSteps < 3) {
            if (gFi.wallSince < 0) {
                gFi.wallSince = gFrame; ++gFi.wallSteps;
                if (gFi.wallSteps == 1 && !WallAway(&gFi.wallAway)) gFi.wallAway = Facing();
                if (gDebug) Log("fight: against a wall (state %x), %s %.0f degrees off its facing: off it first", st0, p.name.c_str(), Wrap(AngleTo(gMe.pos, p.pos) - Facing()) / kDeg);
                float a = gFi.wallSteps == 1 ? gFi.wallAway : gFi.wallSteps == 2 ? AngleTo(gMe.pos, p.pos) : gFi.wallAway + (gFi.wallAside ? -1.5707963f : 1.5707963f);
                if (gFi.wallSteps == 1) gFi.wallAside = false;
                if (gFi.wallSteps == 2) {                            // (toward him through the wall: aside instead)
                    const float h[3] = {gMe.pos[0] + std::sin(a) * 1200.f, gMe.pos[1], gMe.pos[2] + std::cos(a) * 1200.f};
                    const float k0[3] = {gMe.pos[0], gMe.pos[1] - 450.f, gMe.pos[2]}, k1[3] = {h[0], h[1] - 450.f, h[2]};
                    gFi.wallAside = !LineClear(gMe.pos, h) || !LineClear(k0, k1);
                    if (gFi.wallAside) a = gFi.wallAway - 1.5707963f;
                }
                // (its ground checked: along the wall instead, else no step)
                const float ways[3] = {a, gFi.wallAway + 1.5707963f, gFi.wallAway - 1.5707963f};
                gFi.wallGo = SafeStep(ways, 3, 700.f, &gFi.wallDir, "the step off a wall");
                if (!gFi.wallGo) gFi.wallSince = gFrame - 20;
            }
            if (gFrame - gFi.wallSince < 20) {
                gFi.fpv = false; gFi.fpvSince = -1;
                Act("fighting", p.name);
                Steer(std::sin(gFi.wallDir), std::cos(gFi.wallDir), kStep);
                return;
            }
        } else if (!wall) {
            gFi.wallSince = -1;
            if (gFi.offWallSince < 0) gFi.offWallSince = gFrame;
            if (gFrame - gFi.offWallSince > 60) gFi.wallSteps = 0;
        }
        if (wall && gFi.wallSince >= 0 && gFrame - gFi.wallSince >= 20) gFi.wallSince = -1;   // (not off it in 20 frames: once more, 3 at most)
    }
    // (nor the one that is not asked at all: the state word 1 empty with Square held, an enemy against the
    // doll; v133's audit: 3 minutes so in a rescue goal, 184 pulls for nothing)
    const bool down = gFi.squareSince >= 0 && !gFi.fpv && gFrame >= gFi.fireUntil + 10 && (gMe.bits[0] & 0xFFFF) == 3 &&
                      ((gMe.bits[1] & 0x8400) == 0x8000 || (gMe.bits[1] & 0xC400) == 0) && !Reloading() && !Scoped() && Hand() > 1 && Hand() != 9;
    if (!down) gFi.downSince = -1; else if (gFi.downSince < 0) gFi.downSince = gFrame;
    if (gFi.downSince >= 0 && gFrame - gFi.downSince > 45) {
        if (gDebug) Log("fight: the gun does not come up (state %x %x): a step back", gMe.bits[0], gMe.bits[1]);
        gFi.squareSince = -1; gFi.downSince = -1; gFi.fpvSince = -1;
        // (back, else back aside; a drop every way: the first person view instead, its line from the eye)
        const float back = Facing() + 3.14159265f, ways[3] = {back, back + 1.0471976f, back - 1.0471976f};
        if (SafeStep(ways, 3, 800.f, &gFi.backDir, "the step back from its gun")) gFi.backUntil = gFrame + 25;
        else gFi.fpv = true;
        return;
    }
    // its guns empty (v134): no first person view (a roll needs the third); an enemy lying or knocked down left
    // alone 3 s (a weapon box meanwhile), one above or below it 5 s; closer than a roll's run (3 m), walked up to
    // for the punches (DryWalkUp); its punches doing nothing to him, left alone 5 s
    const bool dry = NoAmmo();
    if (dry) {
        if (gFi.punchVain) { gVainUntil[p.id] = gFrame + 300; FightStop(); gFightEndAt = gFrame; return; }
        gFi.cqc = true;
        if (!Scoped()) { gFi.fpv = false; gFi.fpvSince = -1; }
        if (KnifeOnly() && dist < 3000.f && std::fabs(GroundOf(p.pos) - GroundOf(gMe.pos)) <= 500.f) {   // (the knife: down or up, struck)
            if (FightStandUp(p.name)) return;
            if (KnifeStrike(p, dist)) return;
            Act("cqc", p.name);
            Steer(p.pos[0] - gMe.pos[0], p.pos[2] - gMe.pos[2], 0.6f);
            return;
        }
        if (Lying(p.pos) || p.down || Asleep(p) || KnockedDown(p)) {
            if (!(gRo.phase && gRo.target == p.id)) { gDryLeftUntil[p.id] = gFrame + 180; FightStop(); gFightEndAt = gFrame; return; }
        } else if (dist < 1000.f && std::fabs(GroundOf(p.pos) - GroundOf(gMe.pos)) > 500.f) {   // (above or below it: no punch reaches him)
            gDryLeftUntil[p.id] = gFrame + 300; FightStop(); gFightEndAt = gFrame; return;
        } else if (!CqcThrower()) {
            const int w = DryWalkUp(p, dist);
            if (w < 0) { FightStop(); gFightEndAt = gFrame; return; }
            if (w > 0) return;
        }
        if (dist >= 3000.f && Low() && !gRo.phase) { if (FightStandUp(p.name)) return; }
    }
    // a roll into him (v134): he holds the objective; its guns are empty (then punches); at level 3 and up,
    // once in a few fights, before a CQC or a stun weapon's attack when he does not aim at it
    if (gRo.phase && gRo.target == p.id) { if (RollFrame(p, gRo.why)) { gFi.fpv = false; gFi.fpvSince = -1; return; } }
    else if (reacted) {
        const bool aimingAtMe = std::fabs(Wrap(AngleTo(p.pos, gMe.pos) - p.facing)) < 20.f * kDeg && (p.bits[1] & 0x4400) != 0;
        if (gFi.rollTry < 0) gFi.rollTry = Lv() >= 3 && Rnd01() < (Lv() >= 5 ? 0.45f : Lv() >= 4 ? 0.35f : 0.25f) ? 1 : 0;
        // ((v137) the master tier: under his close fire (hit these 0.5 s, him aiming at it, within 6 m, its life 40 % up), a roll
        // into him, drawn once a fight, half the time)
        if (gFi.rollDodge < 0) gFi.rollDodge = MasterFight() && Rnd01() < 0.5f ? 1 : 0;
        const bool dodge = gFi.rollDodge == 1 && aimingAtMe && gFrame - gHurtAt < 30 && dist < 6000.f && (gMyMaxLife <= 0 || gMe.life * 10 >= gMyMaxLife * 4);
        const char* why = CarrierOf(p) ? "the objective's carrier" : dry ? "no rounds left" : dodge ? "to dodge his close fire" :
                          gFi.rollTry == 1 && !aimingAtMe && dist < 8000.f && (gFi.cqc || CqcThrower() || StunWeapon(Hand())) &&
                          gFrame - gHurtAt > 120 && (gMyMaxLife <= 0 || gMe.life * 2 >= gMyMaxLife) ? "before a close attack" : nullptr;   // (not hurt: no rush at half its life)
        if (why && RollFrame(p, why)) { gFi.fpv = false; gFi.fpvSince = -1; if (!strcmp(why, "before a close attack")) gFi.rollTry = 0; if (dodge) gFi.rollDodge = 0; return; }
    }
    // ((v137) the master tier: one rushing at it within 3 m (closing 30 mm a frame and more): its own CQC first)
    if (MasterFight() && !CheatOn() && !gFi.cqc && dist < 3000.f && k.visible) {
        const float closing = (p.vel[0] * (gMe.pos[0] - p.pos[0]) + p.vel[1] * (gMe.pos[2] - p.pos[2])) / std::max(1.f, dist);
        if (closing > 30.f) { gFi.cqc = true; if (gDebug) Log("fight: %s rushing at it (%.0f mm a frame): its CQC first", p.name.c_str(), closing); }
    }
    // a CQC throw (Snake, Raikov, Reiko, Rowdy): an enemy close by, or looking elsewhere within 3.5 m, is
    // rushed, grabbed and thrown down (its stamina gone at once)
    if (CqcThrower() && (!gFi.fpv || dist < 6000.f) && !Asleep(p) && (!cqcOff || gCq.phase != 0) && CqcFrame(p, dist, reacted)) { gFi.fpv = false; gFi.fpvSince = -1; return; }
    if (dry && CqcThrower()) {                           // (its guns empty, the throw not tried now: walked up to, as the others)
        const int w = DryWalkUp(p, dist);
        if (w < 0) { FightStop(); gFightEndAt = gFrame; return; }
        if (w > 0) return;
    }
    if (hs && !gFi.fpv && !dry) { gFi.fpv = true; gFi.fpvSince = -1; }   // (headshots only: after a CQC try, the view again)
    // the weapon for the distance (FightWeapon); the window's switch has the pad's R2 meanwhile, the stick
    // still turns the doll toward the enemy
    const int held = Hand();
    const bool odd = held == 0 || held == 1 || held == 9 || held >= 0x11;   // (no gun in the hands)
    // (the gun that kills for one down only: a stumble would swap to and fro; down a second ago still: his state
    // flickers as he is hit, v136's audit, a sniper's Mk22 and M37 swapped 18 s at 6 m)
    if (AsleepDown(p)) gFi.downAt = gFrame;
    const bool asleep = AsleepDown(p) || (!OldFight() && gFrame - gFi.downAt < 60);
    int wantGun = FightWeapon(dist, held, asleep);
    const bool hsBad = hs && (held == 0xE || held == 9 || held == 0x11);   // (headshots only: never these)
    const bool stunBad = asleep && StunWeapon(held) && FinishAsleep();      // (asleep: a stun weapon does nothing more)
    if (!odd && !hsBad && !stunBad && wantGun != held && dist < (wantGun == 9 ? 1500.f : 3000.f) && !LocksView(held) && !Empty(held)) wantGun = held;   // (right there: no switching)
    if (wantGun >= 0 && wantGun != held && (Inv().ok || held >= 0 || Sniper() || odd) && (TapTo(0, wantGun) || SwapTo(wantGun))) {
        Act("switching_weapon", p.name);
        const float a = AngleTo(gMe.pos, p.pos);
        if (std::fabs(Wrap(a - Facing())) > 20.f * kDeg && gFrame % 6 < 2) Steer(std::sin(a), std::cos(a), kStep);
        return;
    }
    // a grenade or a trap still in the hands (a throw given up for this fight, the window not open again yet): no
    // trigger (Square readies a grenade: the player then stays where it is), the enemy faced (v134's audit); a
    // blocked window tried again in 3 s, not 10
    if (ThrowableInHand()) {
        if (gSw[0].doneAt > gFrame + 90) gSw[0].doneAt = gFrame + 90;
        gFi.fpv = false; gFi.fpvSince = -1;
        Act("switching_weapon", p.name);
        const float a = AngleTo(gMe.pos, p.pos);
        if (std::fabs(Wrap(a - Facing())) > 20.f * kDeg && gFrame % 6 < 2) Steer(std::sin(a), std::cos(a), kStep);
        return;
    }
    // the flamethrower: a few metres only (the player still while it fires): farther, closer first
    if (Hand() == 9 && dist > 4000.f) { Act("fighting", p.name); Steer(p.pos[0] - gMe.pos[0], p.pos[2] - gMe.pos[2], 1.f); return; }
    // a sleeping enemy far off (v136's audit: a doll fired its M37 22 s at one 24 m away): walked up to first, as a
    // player does, while no other enemy is in sight, on a way with no link, 10 s at most; then finished from 3.5 m
    // (the M37's pellets) or 8.5 m (not a scoped rifle: shot from where it is)
    // (asleep: down, its stamina low; knocked down awake by a blast or a roll, he is up in a moment: shot)
    const float finishAt = Hand() == 0xE ? 5000.f : 10000.f;
    const bool walking = gFi.finishSince >= 0;
    if (!OldFight() && (walking ? asleep && p.stun < 25000 : AsleepDown(p) && p.stun <= 12000) && !LocksView(Hand()) && dist > (walking ? finishAt - 1500.f : finishAt) &&
        gFrame >= gFi.finishOff) {
        bool other = false;
        for (const auto& k : gKnown) {
            const Player* o = k.second.visible && k.first != p.id ? PlayerById(k.first) : nullptr;
            if (o && Enemy(*o) && o->alive && !Asleep(*o)) other = true;
        }
        if (other) {                                      // (another enemy in sight: shot from here, looked again in 2 s)
            if (walking) StopPath();
            gFi.finishSince = -1; gFi.finishOff = gFrame + 120;
        } else if (!walking) {
            const float way = WayLen(p.pos);
            if (way > 0.f && way < 2.f * dist + 3000.f) {
                gFi.finishSince = gFrame;
                if (gDebug) Log("fight: %s asleep %.1f m away: walked up to (%.1f m)", p.name.c_str(), dist / 1000.f, way / 1000.f);
            } else gFi.finishOff = gFrame + 120;
        }
        if (gFi.finishSince >= 0 && gFrame - gFi.finishSince >= 600) { StopPath(); gFi.finishSince = -1; gFi.finishOff = gFrame + 600; }   // (not there in 10 s: from here)
        if (gFi.finishSince >= 0) {
            if (gFrame - gFi.finishLast > 2) StopPath();   // (resumed: its path afresh, the wait not taken for stuck)
            if (FightStandUp(p.name)) return;
            gFi.finishLast = gFrame;
            gFi.fpv = false; gFi.fpvSince = -1; gFi.readySince = -1;
            Act("fighting", p.name);
            GoTo(p.pos, "follow", finishAt - 1500.f);
            return;
        }
    } else if (walking) { gFi.finishSince = -1; StopPath(); }
    // an enemy lying (asleep, knocked out, crawling): the third person's shots pass over it: the first
    // person view, 0.7 m away at least (closer: a step back first, 1.5 s; still within 0.7 m, 0.75 s to
    // each side; still at its feet, left alone 5 s: v133's audit, a soldier standing on a Snake lying at
    // his feet fired over it 90 s)
    if (!Lying(p.pos)) gFi.lyingBackSince = -1;
    else if (Hand() != 9) {
        if (dist <= 1500.f && !hs) {
            if (gFi.lyingBackSince < 0) gFi.lyingBackSince = gFrame;
            const long long t = gFrame - gFi.lyingBackSince;
            if (t < 90 || (dist <= 700.f && t < 180)) {
                float ax = gMe.pos[0] - p.pos[0], az = gMe.pos[2] - p.pos[2];
                if (ax * ax + az * az < 1e4f) { ax = -std::sin(Facing()); az = -std::cos(Facing()); }
                if (t >= 90) { const float s = t < 135 ? 1.f : -1.f, bx = -az * s, bz = ax * s; ax = bx; az = bz; }
                if (!StepGround(std::atan2(ax, az), 700.f)) {     // (a drop that way: its next part, then none)
                    if (gDebug) Log("fight: no step from %s lying there (a drop that way)", p.name.c_str());
                    gFi.lyingBackSince = gFrame - (t < 90 ? 90 : t < 135 ? 135 : 180);
                    Act("fighting", p.name); Steer(0, 0, 0);
                    return;
                }
                if (FightStandUp(p.name)) return;
                Act("fighting", p.name);
                Steer(ax, az, kStep);
                return;
            }
            if (dist <= 700.f) {
                if (gDebug) Log("fight: %s lying at the doll's feet (%.1f m), not stepped away from: left alone 5 s", p.name.c_str(), dist / 1000.f);
                gLeftUntil[p.id] = gFrame + 300;
                FightStop(); gFightEndAt = gFrame;
                return;
            }
        } else gFi.lyingBackSince = -1;
        if (!gFi.fpv) { gFi.fpv = true; gFi.fpvSince = -1; }
    }
    // close by: punches (CQC), facing it, up first; the weapon down (headshots only: punches, no kill,
    // rather than a body shot; else a step back to aim at the head). Punches for nothing 5 s: the gun
    if (hs && dist <= 900.f) {
        const bool backOk = StepGround(AngleTo(p.pos, gMe.pos), 700.f);   // (a drop behind it: punches, else left alone)
        if ((gFi.cqc || dist < 600.f || !backOk) && !Asleep(p) && !p.down && !PunchesVain(p)) {
            if (FightStandUp(p.name)) return;
            Act("cqc", p.name);
            const float want = AngleTo(gMe.pos, p.pos), err = Wrap(want - Facing());
            if (std::fabs(err) > 20.f * kDeg && gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep);
            if (reacted && !cqcOff && (gFrame % 10) < 3) gPad.buttons |= 0x2000;
            return;
        }
        if (!backOk) {
            if (gDebug) Log("fight: %s too close for the head, a drop behind it: left alone 5 s", p.name.c_str());
            gLeftUntil[p.id] = gFrame + 300; FightStop(); gFightEndAt = gFrame;
            return;
        }
        if (FightStandUp(p.name)) return;
        Act("fighting", p.name);
        Steer(gMe.pos[0] - p.pos[0], gMe.pos[2] - p.pos[2], kStep);
        return;
    }
    // too close for the first person view (it aims from 1.5 m, 0.7 m at a lying one): its spell ends (the
    // audit: kept on, it barred the CQC and the gun-down guard while the enemy stood against the doll). (v137) Crouched
    // in the view, from 1 m: ended at 1.5 m, it stood up first for the third person's shots (45 frames and more)
    const float fpvMin = hs || Lying(p.pos) ? 700.f : (gMe.bits[0] & 0xFFFF) == 0x85 ? 1000.f : 1500.f;
    if (gFi.fpv && !Scoped() && dist <= fpvMin) { gFi.fpv = false; gFi.fpvSince = -1; }
    // ((v141 beta 8) tier 10's cheat: within 0.9 m its punches too (its bullets there pass him: a bench, two dolls 110 s at 0.5 m, 110
    // pulls each, a life 200 to 110); 5 s for nothing: its gun again)
    const bool cheatPunch = CheatOn() && dist < 900.f && !gFi.punchVain && !Lying(p.pos) && !p.down && !Asleep(p) && !KnockedDown(p);
    if (dist < 1000.f && (gFi.cqc || cheatPunch) && !gFi.fpv && !PunchesVain(p)) {
        if (FightStandUp(p.name)) return;
        Act("cqc", p.name);
        const float want = AngleTo(gMe.pos, p.pos), err = Wrap(want - Facing());
        if (std::fabs(err) > 20.f * kDeg && gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep);   // short pushes: it turns (under 0.55 nothing)
        if (reacted && !cqcOff && (gFrame % 10) < 3) gPad.buttons |= 0x2000;
        return;
    }
    float chest[3], head[3], mz[3];
    ChestPoint(p, chest); HeadPoint(p, head);
    // the first person view: at the head (the chest at 3 and under), the stick on the view
    if (Scoped()) gFi.fpv = true;                        // (in the scope's view: aimed there)
    if (gFi.fpv && dist > fpvMin) {
        Act("aiming_head", p.name);
        gPad.buttons |= (Scoped() ? 0 : 0x0800) | 0x8000; gPad.pressures[11] = 0x3F;
        if (gFi.fpvSince < 0) { gFi.fpvSince = gFrame; gAimSince = gFrame; }
        // ((v141 beta 8), debug) tier 10's view entry: its state words and its move every 2 frames until the view is on (60 at most)
        if (gDebug && CheatOn() && !(gMe.bits[0] & 0x80) && gFrame - gFi.fpvSince <= 60 && (gFrame - gFi.fpvSince) % 2 == 0) {
            static float lastPos[3] = {0, 0, 0}; static long long lastAt = -10;
            const float mv = gFrame - lastAt == 2 ? Dist2D(lastPos, gMe.pos) / 2.f : -1.f;
            memcpy(lastPos, gMe.pos, sizeof lastPos); lastAt = gFrame;
            Log("cheat entry: +%lld, state %x %x, its move %.0f mm a frame, buttons %04x", gFrame - gFi.fpvSince, gMe.bits[0] & 0xFFFF, gMe.bits[1] & 0xFFFF, mv,
                gPad.buttons);
        }
        // (v137) the view's own state (the state word 0's 0x80): it came 16 to 35 frames after R1 in v136's fights (the
        // state lines; probe 26, still: within 20), the shots allowed from the 21st frame came before it. The aim's rates
        // afresh from it (the camera's jump from behind the player to the eye)
        if (gMe.bits[0] & 0x80) {
            if (gFi.viewOnAt < 0) {
                gFi.viewOnAt = gFrame; gAimSince = gFrame; if (gEn.id == p.id && gEn.ready < 0) gEn.ready = gFrame;
                float mv[16];
                if (gDebug && CheatOn() && ps2world::ViewMatrix(mv)) {
                    float hd[3]; HeadPoint(p, hd);
                    const float vy = std::atan2(mv[8], mv[10]), vp = std::asin(std::max(-1.f, std::min(1.f, mv[9])));
                    const float hy = std::atan2(hd[0] - mv[12], hd[2] - mv[14]), hp = std::atan2(hd[1] - mv[13], std::max(1000.f, Dist2D(mv + 12, hd)));
                    Log("cheat view: on %lld frames after R1, %s %.1f m, its head off %.1f %.1f degrees, state %x", gFrame - gFi.fpvSince, LogWho(p).c_str(),
                        dist / 1000.f, Wrap(hy - vy) / kDeg, (hp - vp) / kDeg, gMe.bits[0] & 0xFFFF);
                }
            }
        }
        else gFi.viewOnAt = -1;
        float m[16]; if (!ps2world::ViewMatrix(m)) return;
        const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
        const float lv = LevelNow();
        // the head (the chest at 3 and under; the thermal goggles: the head from level 2.5). (v137) Not seen from the view's
        // eye: the part of him that is (the other one, his lean's head by his last shot, a shoulder past a corner)
        const bool wantHead = hs || (!smoke && lv >= (goggles ? 2.5f : 3.5f) && !RpgRules(Hand()));   // ((v141 beta 6) the RPG-7: the chest)
        const float* aimAt = wantHead ? head : chest;
        int partKind = wantHead ? 0 : 1;
        float part[3];
        const bool boxed = VsBox() && InBox(p) && !hs;      // ((v141 beta 6, aim) his box: its middle, 0.5 m above his ground)
        const float boxPt[3] = {p.pos[0], GroundOf(p.pos) + 500.f, p.pos[2]};
        if (boxed) { aimAt = boxPt; partKind = 1; }
        else { const float e0[3] = {m[12], m[13], m[14]}; if (!LineClear(e0, aimAt) && VisiblePart(p, e0, wantHead, !hs, part, &partKind)) aimAt = part; }
        const float tx = aimAt[0] - m[12], tz = aimAt[2] - m[14], ty = aimAt[1] - m[13];
        // the point aimed at: its own error added (not in headshots only), the aim (AimView) with the
        // level's slack and its following of the target's move; a shot when the view will be on it (within
        // the part aimed at as seen from here: the head 11 cm across a half, the chest 18 cm; headshots only
        // the head itself, 8 cm) when the bullet leaves, and steady
        const float tYaw = std::atan2(tx, tz) + gFi.noise * (hs ? (smoke ? 1.f : 0.f) : 0.3f * aimK);
        const float tPitch = std::atan2(ty, std::sqrt(tx * tx + tz * tz)) + (smoke ? gFi.noise * 0.6f : 0.f);
        const float range = std::max(1000.f, std::sqrt(tx * tx + tz * tz));
        // ((v137) a shoulder past a corner: 10 cm across a half, 12 up and down)
        // ((v141 beta 7) tier 10's cheat: the part's own size at any range, a tenth of a degree at least. (v141 beta 8) From 12 m 0.8 of
        // it: a bullet a pull there, the next only with the view back on him)
        const bool cheatFar = CheatOn() && dist >= kCheatFarShot && !hs;
        const float partK = cheatFar ? 0.8f : 1.f;
        const float tolY = std::max((CheatOn() ? 0.1f : hs ? 0.2f : 0.3f) * kDeg, std::atan2(partK * (partKind == 0 ? (hs ? 80.f : 110.f) : partKind == 2 ? 100.f : 180.f), range));
        const float tolP = std::max((CheatOn() ? 0.12f : hs ? 0.2f : 0.4f) * kDeg, std::atan2(partK * (partKind == 0 ? (hs ? 70.f : 100.f) : partKind == 2 ? 120.f : 250.f), range));
        const float follow = lv >= 4.5f ? 1.f : lv >= 3.5f ? 0.5f : 0.f;
        const float slack = (lv >= 5.5f ? 0.1f : lv >= 4.5f ? 0.12f : lv >= 3.5f ? 0.2f : lv >= 2.5f ? 0.3f : 0.4f) * (goggles ? 0.75f : 1.f);   // ((v137) 6: 0.1)
        // (the wall's own view, 0x82 or 0x84, is locked: him more than 60 degrees off it a second, the view let go
        // and the wall's steps again, twice a fight, then he is left alone 3 s)
        // ((v141 beta 7) tier 10's cheat: out of the view to the third person within 7 m (the auto aim's reach) and when he is more than
        // 25 degrees off the view (the readied turn first, the view again once faced); not its ambush's fight, not a lying one)
        // ((v141 beta 10) within 7 m only when the third person's gun has a line to his chest: else the view's own rule took it back at once,
        // 287 changes in 58 s and no pull in the bench base_b9rel)
        // (the third person's own test: its gun's line and its eye's, as the third person's rule back to the view reads them)
        bool tpvLine = true;
        if (CheatOn() && !CheatWaitsB8() && dist < CheatViewFrom() - 3000.f) {
            float mzTpv[3], eyeTpv[3]; Muzzle(mzTpv); SeeEye(eyeTpv);
            tpvLine = LineClear(mzTpv, chest) && LineClear(eyeTpv, chest);
        }
        if (CheatOn() && strcmp(gEn.pre, "view") && !hs && !Scoped() && !Lying(p.pos) && gFi.viewOnAt >= 0 &&
            ((dist < CheatViewFrom() - 3000.f && tpvLine) || std::fabs(Wrap(tYaw - camYaw)) > 25.f * kDeg)) {
            if (gDebug) Log("fight: %s %.1f m away, %.0f degrees off the view: the third person", p.name.c_str(), dist / 1000.f, Wrap(tYaw - camYaw) / kDeg);
            gFi.fpv = false; gFi.fpvSince = -1; gFi.fpvLater = dist > CheatViewFrom(); gFi.readySince = -1;
            return;
        }
        const uint32_t stv = gMe.bits[0] & 0xFFFF;
        if ((stv == 0x82 || stv == 0x84) && !Scoped() && std::fabs(Wrap(tYaw - camYaw)) > 60.f * kDeg) { if (gFi.wallViewSince < 0) gFi.wallViewSince = gFrame; }
        else gFi.wallViewSince = -1;
        if (gFi.wallViewSince >= 0 && gFrame - gFi.wallViewSince > 60) {
            gFi.wallViewSince = -1;
            if (gFi.wallRounds < 2) {
                if (gDebug) Log("fight: %s out of the wall's view (%.0f deg off): off it again", p.name.c_str(), Wrap(tYaw - camYaw) / kDeg);
                ++gFi.wallRounds; gFi.wallSteps = 0; gFi.wallSince = -1; gFi.fpv = false; gFi.fpvSince = -1;
                return;
            }
            if (gDebug) Log("fight: %s out of the wall's view (%.0f deg off): left alone 3 s", p.name.c_str(), Wrap(tYaw - camYaw) / kDeg);
            gWallLeftUntil[p.id] = gFrame + 180; FightStop(); gFightEndAt = gFrame;
            return;
        }
        // ((v150-host beta 18) the direct aim: the view's angles written toward the point, else the stick)
        bool on = false;
        if (!(DirectAim() && DirectView(tYaw, tPitch, tolY, tolP, &on))) on = AimView(camYaw, camPitch, tYaw, tPitch, follow, slack, tolY, tolP);
        // ((v141 beta 8) tier 10's cheat from 12 m: AimView's own test alone (on him in 2 ticks, when the bullet leaves, and steady), a
        // bullet a pull; nothing on the view's way across him. Snake's game of 14 October and the bench's bullets (svy_dbg1): 92 of 114
        // left with the view off him by the last one's recoil (about a degree a bullet) or turning, 0.7 to 3 degrees off at 12 to 30 m)
        // ((v141 beta 7) tier 10's cheat: the shot as soon as the view is on him or will be when the bullet leaves (in 2 ticks), steady or
        // not: a view still turning crosses him. (v141 beta 8) From 12 m only after 12 frames with the view on and no bullet (his move
        // keeps the view from steady: a shoulder peeking at 27 and 29 m was never shot at, the doll died, b8_duel3's second run))
        // ((v141 beta 10) its first bullet of the fight at once, no recoil to wait for: 60 of 83 fights pre-aimed from 12 m had no pull)
        const bool farPatient = cheatFar && gFi.viewOnAt >= 0 && (gFrame - std::max(gFi.viewOnAt, gFi.fireUntil) >= 12 || (gFi.shots == 0 && !CheatWaitsB8()));
        if (CheatOn() && !on && (!cheatFar || farPatient)) on = (std::fabs(Wrap(tYaw - camYaw)) < tolY && std::fabs(tPitch - camPitch) < tolP) ||
                                                                (std::fabs(gAimAheadY) < tolY && std::fabs(gAimAheadP) < tolP);
        // ((v141 beta 7) tier 10's cheat: its view still on its way to his head and on his body when the bullet leaves (his body's half
        // width, from his hips to his head's top): the shot now (a hit staggers him), its aim going on to his head. (v141 beta 8) Within
        // 12 m only)
        const float* fireAt = aimAt;
        float bodyAt[3];
        if (CheatOn() && !on && !hs && partKind == 0 && !boxed && gFi.viewOnAt >= 0 && !cheatFar) {
            const float pYaw = tYaw - gAimAheadY, pPitch = tPitch - gAimAheadP;       // (the view in 2 ticks)
            const float bYaw = std::atan2(chest[0] - m[12], chest[2] - m[14]), bd2 = std::max(1000.f, Dist2D(m + 12, chest));
            const float top = std::atan2(head[1] + 120.f - m[13], bd2), low = std::atan2(chest[1] - 350.f - m[13], bd2);
            if (std::fabs(Wrap(bYaw - pYaw)) < std::atan2(220.f, bd2) && pPitch < top && pPitch > low) {
                bodyAt[0] = chest[0]; bodyAt[2] = chest[2];
                bodyAt[1] = m[13] + std::tan(pPitch) * bd2;                               // (where the view will be on him)
                fireAt = bodyAt; on = true;
            }
        }
        // (v137) the stick still until the view's state is on (30 frames at most): pushed from R1 it aimed the camera behind
        // the player (full on both axes, the motion lines' "l=01,01"): a run went on 12 to 18 frames, the view came 24 to 54
        // frames after R1 (motion137/dm/d2.txt f6486, f7080); still, without the stick, within 20 (probe 26)
        if (gFi.viewOnAt < 0 && gFrame - gFi.fpvSince < 30 && !Scoped()) { gPad.sticks[2] = gPad.sticks[3] = 0x80; }
        if (on) { if (gFi.onTargetSince < 0) gFi.onTargetSince = gFrame; } else gFi.onTargetSince = -1;
        if (on && gFi.viewOnAt >= 0 && gEn.id == p.id && gEn.aim < 0) gEn.aim = gFrame;
        if (gDebug >= 2 && gFrame % 20 == 0)
            Log("fpv: view yaw %.1f pitch %.1f, to %s %.1f %.1f: errors %.2f %.2f, turns %.2f %.2f, its %.2f %.2f; facing %.1f, weapon %x, state %x, eye %.0f %.0f %.0f",
                camYaw / kDeg, camPitch / kDeg, p.name.c_str(), tYaw / kDeg, tPitch / kDeg, Wrap(tYaw - camYaw) / kDeg, (tPitch - camPitch) / kDeg, gAimY.rate, gAimP.rate,
                gAimY.tgtRate, gAimP.tgtRate, Facing() / kDeg, Hand(), gMe.bits[0], m[12], m[13], m[14]);
        const float eye[3] = {m[12], m[13], m[14]};
        const bool firing = gFrame < gFi.fireUntil;
        static long long sBurstAt = -1;                      // (debug: the bullets' frames of a burst, 4 then every 6)
        if (gDebug && CheatOn() && sBurstAt >= 0 && gFrame <= gFi.fireUntil + 4 && (gFrame - sBurstAt) % 6 == 4)
            Log("cheat bullet: %s %.1f m, %s, the view off %.2f %.2f degrees, turning %.2f %.2f, his move %.2f %.2f, tolerance %.2f %.2f, state %x",
                LogWho(p).c_str(), dist / 1000.f, partKind == 0 ? "head" : partKind == 2 ? "edge" : "body", Wrap(tYaw - camYaw) / kDeg, (tPitch - camPitch) / kDeg,
                gAimY.rate, gAimP.rate, gAimY.tgtRate, gAimP.tgtRate, tolY / kDeg, tolP / kDeg, gMe.bits[0] & 0xFFFF);
        LeanFrame(eye, camYaw, aimAt, firing);               // (levels 3 and up: the lean, crouched at 4 and up)
        if (firing) { gPad.pressures[11] = 0xFF; Decide("FIRE", "fpv_burst"); return; }   // firing: the aim kept on it (the recoil taken back)
        const TriggerKind tk = Trigger();
        if (tk != TK_AUTO && gFrame < gFi.nextFire && gFrame - gFi.fireUntil < (tk == TK_PUMP ? 35 : 20)) {
            gPad.buttons &= ~0x8000u; gPad.pressures[11] = 0;   // let go (R1 held): the next press a new shot
        }
        const bool cheat = CheatOn();
        const int settle = cheat ? 0 : hs ? std::max(1, 2 + 3 * (5 - Lv())) : 2;      // (headshots only: the lower levels line it up longer)
        // (v137) the view on and settled (2 frames at level 5, 4 at 4, 8 at 3, 14 below), else 45 frames after R1 (the
        // state not seen): it was 20 frames after R1, whatever the view. ((v141 beta 7) tier 10's cheat: the view on, no settling)
        const int viewSettle = cheat ? 0 : lv >= 4.5f ? 2 : lv >= 3.5f ? 4 : lv >= 2.5f ? 8 : 14;
        const bool viewReady = (gFi.viewOnAt >= 0 && gFrame - gFi.viewOnAt >= viewSettle && (cheat || gFrame - gFi.fpvSince > 4)) || gFrame - gFi.fpvSince > 45;
        // ((v141 beta 7) its ambush: a shoulder of his in the view first, the shot held 20 frames at most for his head)
        bool headWait = false;
        if (cheat && !strcmp(gEn.pre, "view") && partKind == 2 && !hs) {
            if (gCheat.shoulderSince < 0) gCheat.shoulderSince = gFrame;
            headWait = gFrame - gCheat.shoulderSince < 20;
        }
        // (the bullet leaves the gun, 0.15 m under the eye and 0.1 m to its right: a head seen over a wall
        // with the gun under its top is not shot at: the audit saw 47 shots into a cover)
        const float gun[3] = {eye[0] - std::cos(camYaw) * 100.f, eye[1] - 150.f, eye[2] + std::sin(camYaw) * 100.f};
        if (const char* rpgNo = FpvFireBlock(p, dist)) { Decide("WAIT", rpgNo); return; }   // ((v141 beta 6) the RPG-7's own safety: aimed, no shot)
        // ((v141 beta 10) tier 10 from 12 m: no bullet at a guessed edge of him (10 bullets at 30 m, none on him), the view kept on it
        if (cheat && cheatFar && partKind == 2 && !CheatWaitsB8() && gFrame - gHurtAt > 120) {
            // (half a second a fight for more of him to show, then its hunt on to a better line: no stand in front of him; hurt: its fire)
            static long long edgeFight = -1, edgeSince = -1;
            if (edgeFight != gFi.startAt) { edgeFight = gFi.startAt; edgeSince = gFrame; }
            if (gFrame - edgeSince > 30) {
                if (gDebug) Log("fight: %s's edge alone in sight %.1f m away: the hunt on", LogWho(p).c_str(), dist / 1000.f);
                gCoveredUntil[p.id] = gFrame + 90; FightStop(); gFightEndAt = gFrame;
                return;
            }
            Decide("WAIT", "edge_far");
            return;
        }
        const bool parOk = ParitySettled(on && gFi.viewOnAt >= 0);   // ((v150-host beta 16) the parity mode's first shot)
        // (v141 beta 6) the first condition that holds the shot back, for the AI's debug lines
        if (!on) Decide("AIM", "view_turning");
        else if (!reacted) Decide("WAIT", "reaction", gFi.reactAt - gFrame);
        else if (gFi.holdFire) Decide("WAIT", "target_hidden_a_moment");
        else if (!viewReady) Decide("WAIT", "view_settling");
        else if (gFrame - gFi.onTargetSince < settle || !parOk) Decide("WAIT", "aim_settling");
        else if (gFrame < gFi.nextFire) Decide("WAIT", "attack_cooldown", gFi.nextFire - gFrame);
        else if (gFrame < gLean.hideUntil) Decide("WAIT", "peek_back");
        else if (headWait) Decide("WAIT", "head_awaited");
        else if (!LineClear(eye, fireAt) || !LineClear(gun, fireAt)) Decide("WAIT", "no_line_of_fire");
        else if (AllyInTheWay(eye, fireAt)) Decide("WAIT", "ally_in_line");
        else Decide("FIRE", "fpv");
        if (on && reacted && !gFi.holdFire && viewReady && gFrame - gFi.onTargetSince >= settle && parOk && gFrame >= gFi.nextFire && gFrame >= gLean.hideUntil && !headWait &&
            LineClear(eye, fireAt) && LineClear(gun, fireAt) && !AllyInTheWay(eye, fireAt)) {
            // ((v141 beta 8) tier 10's cheat from 12 m: a bullet a pull, the next once the view is back on him and steady)
            const int burst = tk == TK_AUTO ? (cheatFar ? 5 : BurstFrames(Hand(), dist, partKind != 1 ? 5 : 8)) : 8;   // (the head: a bullet at a time, the recoil lifts the next)
            gFi.fireUntil = gFrame + burst;
            // ((v141 beta 7) tier 10's cheat: the next bullet as soon as the gun allows it, each one aimed again. (v141 beta 8) From 12 m 10
            // frames after the bullet: its recoil throws the view 3 to 7 frames after it (0.5 to 2.4 degrees, the bench b7b_duel1), the
            // next pull only once the view is back on him and steady)
            const int gap = cheat ? (tk == TK_PUMP ? 65 : tk == TK_SEMI ? 20 : cheatFar ? 10 : 2) : tk == TK_PUMP ? std::max(sk.gap, 65) : tk == TK_SEMI ? std::max(sk.gap, 45) : sk.gap + 8;
            gFi.nextFire = gFrame + burst + gap; ++gFi.shots; ++gFi.dryShots;
            gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF;
            if (gEn.id == p.id) { if (gEn.shot < 0) { gEn.shot = gFrame; CheatPull(); } ++gEn.pulls; }
            sBurstAt = gFrame;
            if (gDebug && cheat) Log("cheat pull: %s %.1f m, %s, burst %d, the view on since %lld, square since %lld", LogWho(p).c_str(), dist / 1000.f,
                                     fireAt == bodyAt ? "body crossing" : partKind == 0 ? "head" : partKind == 2 ? "edge" : "body", burst,
                                     gFi.viewOnAt >= 0 ? gFrame - gFi.viewOnAt : -1, gFi.squareSince >= 0 ? gFrame - gFi.squareSince : -1);
        }
        if (gFrame - gFi.fpvSince > 600) { if (hs) gFi.fpvSince = gFrame - 21; else gFi.fpv = false; }   // too long: the third person view again
        FpvBlocked(p, dist);                                 // ((v141 beta 6, aim) no line of fire from the view a while: up, else a step aside)
        return;
    }
    gFi.fpvSince = -1; gFi.viewOnAt = -1;
    // third person: readied, the stick toward it until the facing points at it, bursts (crouched by the
    // first person view: up first, the readied walk crouched would crawl)
    if (FightStandUp(p.name)) return;
    Act(Reloading() ? "reloading" : "fighting", p.name);
    gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
    if (gFi.readySince < 0) gFi.readySince = gFrame;
    // (v137) the game's readied flag (the state word 1's 0x4000: 4 to 7 frames after Square in v136's state lines)
    if (gMe.bits[1] & 0x4000) { if (gFi.armedAt < 0) { gFi.armedAt = gFrame; if (gEn.id == p.id && gEn.ready < 0) gEn.ready = gFrame; } }
    else gFi.armedAt = -1;
    Muzzle(mz);
    // (v137) the chest, else what is seen of him from the muzzle (his head, his lean's, a shoulder): the first person view
    // then (levels 2 and up, beyond 1.5 m), where the part is aimed at
    const float* tAt = chest;
    float tPart[3]; int tKind = 1;
    if (!LineClear(mz, chest) && VisiblePart(p, mz, false, true, tPart, &tKind)) {
        tAt = tPart;
        // ((v141 beta 6, aim) not just out of a view with no line (FpvBlocked); not with him more than 50 degrees off its facing from
        // level 4: the readied turn first, 3 to 5 degrees a frame against the view's 1 to 2 (the aim_ab1 bench: 150 frames turning the
        // view to one 2.3 m behind it, 116 degrees off))
        const bool farOff = LevelNow() >= 4.f && std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - Facing())) > 50.f * kDeg;
        // ((v141 beta 7) tier 10's cheat: a shoulder shot at from here, his head alone the view once faced within 8 degrees)
        const bool cheatFar = CheatOn() && std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - Facing())) > 8.f * kDeg;
        if (Lv() >= 2 && dist > 1500.f && !dry && !hs && gFrame >= gNoFpvUntil && !farOff && !cheatFar && !(CheatOn() && tKind == 2)) {
            if (gDebug) Log("fight: only a part of %s seen (%s): the first person view", p.name.c_str(), tKind == 2 ? "a shoulder" : "his head");
            gFi.fpv = true; gFi.fpvSince = -1; gFi.fpvLater = false;
            return;
        }
    }
    // ((v137) the master tier: led by his move 4 frames, the first bullet's (the 4th frame of Square full, probe 10); in sight only)
    const bool master = MasterFight(), lead = master && k.visible;
    const float led[3] = {tAt[0] + (lead ? p.vel[0] * 4.f : 0.f), tAt[1], tAt[2] + (lead ? p.vel[1] * 4.f : 0.f)};
    const float want = AngleTo(mz, led) + gFi.noise * aimK;
    const float err = Wrap(want - Facing());
    // the facing's tolerance: the body's half width (0.25 m) as seen from here, 2.5 times it at level 1,
    // 1.1 at 5 (the audit: the old one, a degree at 10 m at 5, was finer than the readied turn can go:
    // the experts stood waiting while the beginners fired)
    const float tol = std::max(0.7f * kDeg, std::atan2(250.f * (0.41f * sk.tolDeg + 0.44f), std::max(dist, 1000.f))) * (smoke ? 1.6f : goggles ? 0.85f : 1.f);
    float eyeNow[3]; SeeEye(eyeNow);
    const bool clear = LineClear(mz, tAt) && !AllyInTheWay(mz, tAt) && (!master || LineClear(eyeNow, tAt));   // ((v137) 6: its eye's line too)
    // ((v141 beta 7) tier 10's cheat: him in its eye's sight, its gun's line blocked (a low wall's top, a rail): the first person view
    // when its gun there (0.15 m under its eye) has a line to his head or chest, beyond 1.5 m, not just out of a view with no line
    // (FpvBlocked), not turned far off him)
    if (CheatOn() && !clear && k.visible && !gFi.holdFire && !dry && !hs && dist > 1500.f && gFrame >= gNoFpvUntil && !AllyInTheWay(mz, tAt) &&
        std::fabs(Wrap(AngleTo(gMe.pos, p.pos) - Facing())) < 8.f * kDeg) {
        const float gunF[3] = {eyeNow[0], eyeNow[1] - 150.f, eyeNow[2]};
        if (LineClear(gunF, head) || LineClear(gunF, chest)) {
            if (gDebug) Log("fight: its gun's line to %s blocked, its eye sees him: the first person view", p.name.c_str());
            gFi.fpv = true; gFi.fpvSince = -1; gFi.fpvLater = false;
            return;
        }
    }
    // ((v150-host beta 18) the third person's own bullet: along the facing, 3.4 degrees down from the muzzle whatever the height of the
    // one aimed at (probe 44 on City Under Siege and Graniny Gorki Lab: -3.34 to -3.37 degrees, a pitch written in the view changing
    // nothing). The line checked above is to his chest, a bullet the game never fires: him a floor above or below (DU10_both: 18 of 50
    // engagements 1.2 m or more above or below, pulls and no hit), no bullet of the third person reaches him. Then the first person view
    // when its eye's gun has a line to him (the view pitches), else no shot by hand (the step aside of a blocked line; the game's auto aim
    // keeps its own lock). MGO1_DOLL_TPV_LEVEL=0: as before)
    const bool levelOk = !TpvLevelOn() || TpvLevelHits(p, mz, dist);
    if (clear && !levelOk && k.visible && !gFi.holdFire && !dry && !hs && dist > 1500.f && gFrame >= gNoFpvUntil && Lv() >= 2 && !SniperInHand()) {
        const float gunF[3] = {eyeNow[0], eyeNow[1] - 150.f, eyeNow[2]};
        if (LineClear(gunF, head) || LineClear(gunF, chest)) {
            if (gDebug) Log("fight: %s %.1f m above or below its gun's level (%.1f m): the first person view", LogWho(p).c_str(), dist / 1000.f, (p.pos[1] - gMe.pos[1]) / 1000.f);
            ++gTpvLevelViews;
            gFi.fpv = true; gFi.fpvSince = -1; gFi.fpvLater = false;
            return;
        }
    }
    const bool tpvClear = clear && levelOk;
    // (v141 beta 6, aim) the game's auto aim (AutoAimFrame): L1 held, the shot within the tier's cone on the game's lock, the stick still
    bool aaStill = false;
    const bool aaShot = AutoAimFrame(p, dist, clear && !gFi.holdFire && !dry && !smoke, &aaStill);
    // ((v141 beta 9) the bursts' frames too, the camera's way; MGO1_FIGHT_TRACE=2: every frame)
    static const int ftrace = getenv("MGO1_FIGHT_TRACE") ? std::max(1, atoi(getenv("MGO1_FIGHT_TRACE"))) : 0;
    const bool traceNow = ftrace && (ftrace >= 2 || gFrame % 2 == 0);
    if (gFrame < gFi.fireUntil) {
        gPad.pressures[11] = 0xFF; if (gAa.on) Decide("AUTOAIM_FIRE", "autoaim_burst"); else Decide("FIRE", "tpv_burst");
        if (traceNow)
            Log("fight trace: %s dist %.1f err %.2f burst L1 %d facing %.1f cam %.1f bits %x %x", LogWho(p).c_str(), dist / 1000.f, Wrap(want - Facing()) / kDeg, gAa.on ? 1 : 0,
                Facing() / kDeg, std::atan2(gCam.fwd[0], gCam.fwd[1]) / kDeg, gMe.bits[0], gMe.bits[1]);
        return;
    }
    // ((v141 beta 6, aim) his box waited on 2.5 s and he stays in it (a box that runs): the first person view at it, beyond 1.5 m)
    if (BoxWaitFor(p)) {
        if (gBoxWaitId != p.id || gBoxWaitSince < 0) { gBoxWaitId = p.id; gBoxWaitSince = gFrame; }
        else if (gFrame - gBoxWaitSince > 150 && dist > 1500.f && gFrame >= gNoFpvUntil && !SniperInHand()) {
            if (gDebug) Log("fight: %s still in his box after 2.5 s (%.1f m): the first person view at it", p.name.c_str(), dist / 1000.f);
            gBoxWaitSince = -1; gFi.fpv = true; gFi.fpvSince = -1; gFi.fpvLater = false;
            return;
        }
    } else gBoxWaitSince = -1;
    // (v137) turned to him from far off (FightFrame's start, levels 4 and 5): the first person view once faced, beyond 15 m
    const bool quick = QuickAim() && Nightmare();
    // ((v141 beta 7) tier 10's cheat: beyond 12 m faced within 6 degrees, the view, whenever (him come farther in the fight too)
    if (CheatOn() && !gFi.fpvLater && dist > CheatViewFrom() + 2000.f && std::fabs(err) < 6.f * kDeg && !dry && !hs && !Lying(p.pos) && k.visible &&
        gFrame >= gNoFpvUntil && !gFi.holdFire)
        gFi.fpvLater = true;
    if (gFi.fpvLater && std::fabs(err) < (CheatOn() ? 6.f : quick ? 4.f : 15.f) * kDeg) {
        gFi.fpvLater = false;
        if (dist > (CheatOn() ? CheatViewFrom() : quick ? 10000.f : 15000.f) && !dry && !Lying(p.pos)) {   // ((v141 beta 7) tier 10's cheat: beyond 10 m)
            if (gDebug) Log("fight: %s faced: the first person view", p.name.c_str());
            gFi.fpv = true; gFi.fpvSince = -1; gFi.turning = false;
            return;
        }
    }
    // (v137) the readied turn begun past the shot's window (1.4 times the tolerance), ended within 0.6 of it: one threshold
    // turned it again at each small drift, walking (v136's state lines: 0.48 m each 30 frames in the third person's fights)
    const float aerr = std::fabs(err);
    if (!gFi.turning && aerr > tol * 1.4f) gFi.turning = true;
    else if (gFi.turning && aerr < tol * 0.6f) gFi.turning = false;
    // ((v141 beta 7) tier 10's cheat: the facing gone across him since the frame before (a turn of 3 to 5 degrees a frame steps over
    // a window of 1 to 2): aimed, the shot at once)
    bool crossed = false;
    if (CheatOn()) {
        crossed = gCheat.lastErrId == p.id && gFrame - gCheat.lastErrAt == 1 && (gCheat.lastErr > 0.f) != (err > 0.f) && std::fabs(gCheat.lastErr) < 10.f * kDeg &&
                  aerr < 10.f * kDeg;
        gCheat.lastErr = err; gCheat.lastErrAt = gFrame; gCheat.lastErrId = p.id;
    }

    if (aaStill) { Steer(0, 0, 0); gFi.turnSince = -1; }   // ((v141 beta 6, aim) L1 held: no turn by hand meanwhile)
    else if (DirectAim() && DirectFacing(want)) { Steer(0, 0, 0); gFi.turnSince = -1; }   // ((v150-host beta 18) the direct aim: the facing written, no walk)
    else if (gFi.turning && !StepGround(want, 500.f)) {
        // (a drop toward him: no readied walk that way; the first person view beyond 1.5 m, its guns not empty)
        Steer(0, 0, 0); gFi.turnSince = -1;
        if (dist > 1500.f && !dry) {
            if (gDebug) Log("fight: a drop toward %s: first person view", p.name.c_str());
            gFi.fpv = true; gFi.fpvSince = -1;
        } else {                                             // (no way to turn to him: left alone 3 s, the doll goes on)
            if (gDebug) Log("fight: a drop toward %s (%.1f m%s): left alone 3 s", p.name.c_str(), dist / 1000.f, dry ? ", its guns empty" : "");
            gWallLeftUntil[p.id] = gFrame + 180; FightStop(); gFightEndAt = gFrame;
            // ((v141 beta 10) left alone in earnest (the wall's mark holds only against a wall: the bench b10a_base, fought again every frame 78 s)
            if (!CheatWaitsB8()) gLeftUntil[p.id] = gFrame + 180;
            return;
        }
    } else if (gFi.turning) {
        // (v137) pushed by the error: 30 degrees off and more a strong push (the readied walk turns 3 to 5 degrees a frame
        // then), 8 the fight's 0.64, closer the turn in place (TurnPush; it was 0.57, a walk)
        // ((v141 beta 3) NIGHTMARE: the stick led past him. The facing goes to the stick's way by a part of what is left each
        // frame (the ftrace bench: 32, 24, 19, 14, 11 degrees a 2 frames from 138 off, the stick still), so the stick at 1.8
        // times the error (1.3 within 6 degrees, 170 at most) brings it there in about half the frames, the error read again
        // each frame)
        float way = want;
        // ((v141 beta 9) him within 2.5 m or the auto aim's reach: straight at him, no lead (gLedAt; the choke bench: the body went on
        // to a way led past him at 46 degrees off, the 3 frames straight at him after it not taken)
        const bool straight = dist < 2500.f || AaMayTake(p, dist);
        if (!straight && CheatOn()) way = CheatTurnWay(err);   // ((v141 beta 7) tier 10's cheat: led 4 times the error within 6 degrees)
        else if (!straight && NmV3()) {
            const float k = aerr > 6.f * kDeg ? 1.8f : 1.3f;
            way = Facing() + std::max(-170.f * kDeg, std::min(170.f * kDeg, err * k));
        }
        if (std::fabs(Wrap(way - want)) > 4.f * kDeg) gLedAt = gFrame;
        // ((v141 beta 8) tier 10's cheat: him within 2.5 m, the turn in place (the readied walk went round him))
        Steer(std::sin(way), std::cos(way), CheatOn() ? (dist < 2500.f ? TurnPush() : CheatTurnPush(err)) : aerr > 30.f * kDeg ? 0.72f : aerr > 8.f * kDeg ? 0.64f : TurnPush());
        // the facing does not follow (the readied walk against a wall turns nothing): the first person
        // view turns the aim where it stands
        if (gFi.turnSince < 0 || std::fabs(Wrap(Facing() - gFi.turnFacing)) > 2.f * kDeg) { gFi.turnSince = gFrame; gFi.turnFacing = Facing(); }
        else if (gFrame - gFi.turnSince > 40 && dist > 1500.f && aerr > std::max(3.f * kDeg, 2.f * tol)) {   // ((v137) not a fine turn's last degree)
            // (against a wall, him far off its facing: that view is locked, and pressed there it may stay on when R1
            // is let go, v134's audit: 10 and 20 s so; the wall's steps again, twice, then he is left alone 3 s)
            if (AgainstWall() && std::fabs(err) > 60.f * kDeg) {
                gFi.turnSince = -1;
                if (gFi.wallRounds < 2) {
                    ++gFi.wallRounds; gFi.wallSteps = 0; gFi.wallSince = -1;
                    if (gDebug) Log("fight: the facing does not turn against a wall (%.0f deg off): off it again", err / kDeg);
                } else {                                     // (still there: left alone 3 s, the doll goes on)
                    if (gDebug) Log("fight: the facing does not turn against a wall (%.0f deg off): %s left alone 3 s", err / kDeg, p.name.c_str());
                    gWallLeftUntil[p.id] = gFrame + 180; FightStop(); gFightEndAt = gFrame;
                    return;
                }
            } else {
                if (gDebug) Log("fight: the facing does not turn (%.0f deg off): first person view", err / kDeg);
                gFi.fpv = true; gFi.fpvSince = -1; gFi.turnSince = -1;
            }
        }
    } else { Steer(0, 0, 0); gFi.turnSince = -1; }
    // (v141 beta 3) MGO1_FIGHT_TRACE: the third person's aim every 2nd frame (the turn's physics), the stick of this frame
    if (traceNow)
        Log("fight trace: %s dist %.1f err %.2f tol %.2f turning %d facing %.1f cam %.1f bits %x %x pos %.0f %.0f %.0f stick %02x %02x pad %04x", LogWho(p).c_str(),
            dist / 1000.f, err / kDeg, tol / kDeg, gFi.turning ? 1 : 0, Facing() / kDeg, std::atan2(gCam.fwd[0], gCam.fwd[1]) / kDeg, gMe.bits[0], gMe.bits[1],
            gMe.pos[0], gMe.pos[1], gMe.pos[2], gPad.sticks[2], gPad.sticks[3], gPad.buttons);
    const TriggerKind tk = Trigger();
    if (tk != TK_AUTO && gFrame < gFi.nextFire && gFrame - gFi.fireUntil < (tk == TK_PUMP ? 35 : 20)) {
        gPad.buttons &= ~0x8000u; gPad.pressures[11] = 0;   // let go: the pump worked, the next press a new shot
    }
    // (v137) readied: the game's flag a moment (1 frame at level 5, 2 at 4, 4 at 3), else 10 frames after Square as
    // before (16 at level 1; not in a life's first 30 frames: the deploy's window, doll.cpp's DeployTail, holds Square back)
    const int lvI = Lv();
    const bool tail = LifeFrames() >= 0 && LifeFrames() < 30;
    const bool cheat = CheatOn();
    // ((v141 beta 7) tier 10's cheat: the game's flag on, no wait)
    const bool armed = (lvI >= 3 && gFi.armedAt >= 0 && gFrame - gFi.armedAt >= (cheat ? 0 : lvI >= 5 ? 1 : lvI >= 4 ? 2 : 4)) || (!tail && gFrame - gFi.readySince > (lvI <= 1 ? 16 : 10));
    // ((v141 beta 6, aim) aimed: the facing within its tolerance, or the auto aim's shot; the AI's lines say which and why not)
    // ((v150-host beta 18) the direct aim: where its gun will point when the bullet leaves, the facing written this frame and the gun's lag)
    const bool directNow = DirectAim() && gGunAt == gFrame;
    // ((v150-host beta 18) tier 10's sustained fire with the direct aim: the pull also when the second bullet (6 frames after the first)
    // will be on him and the first within 4 times the tolerance: the gun's lag closing meanwhile, the first bullet's 6 frames not waited)
    const bool directOn = directNow && (DirectGunErr(want) <= tol * 1.4f ||
                                        (CheatOn() && Trigger() == TK_AUTO && DirectGunErr(want, 9) <= tol * 1.4f && DirectGunErr(want) <= tol * 4.f));
    const bool aimed = (((directNow ? directOn : aerr <= tol * 1.4f) || (crossed && !directNow)) && !BoxWaitFor(p)) || aaShot;   // (his box: no shot by hand, they pass over it)
    // ((v141 beta 7) tier 10's cheat: its magazine empty, the trigger pulled at once (the game reloads at a pull), aimed or not)
    const bool reloadPull = cheat && MagNow() == 0 && !Reloading() && Rounds(Hand()) > 0 && !hs;
    const bool parOk = ParitySettled(aimed);             // ((v150-host beta 16) the parity mode's first shot)
    if (aimed && gEn.id == p.id && gEn.aim < 0) gEn.aim = gFrame;
    if (reloadPull && armed && gFrame >= gFi.nextFire) Decide("WAIT", "reload_press");
    else if (!aimed) { if (gAa.on) Decide("AUTOAIM", gAa.why); else if (gAa.want) Decide("MANUAL_AIM", *gAa.why ? gAa.why : "turning"); else Decide("AIM", "turning"); }
    else if (!reacted) Decide("WAIT", "reaction", gFi.reactAt - gFrame);
    else if (!parOk) Decide("WAIT", "aim_settling");
    else if (gFi.holdFire) Decide("WAIT", "target_hidden_a_moment");
    else if (!armed) Decide("WAIT", "weapon_not_ready");
    else if (gFrame < gFi.nextFire) Decide("WAIT", "attack_cooldown", gFi.nextFire - gFrame);
    else if (!clear) Decide("MOVE", "no_line_of_fire");
    else if (!aaShot && !tpvClear) Decide("MOVE", "tpv_bullet_level");
    else if (hs) Decide("WAIT", "headshots_only");
    else if (aaShot) Decide("AUTOAIM_FIRE", "autoaim");
    else Decide("FIRE", "tpv");
    if (reloadPull && armed && gFrame >= gFi.nextFire) {
        gFi.fireUntil = gFrame + 8; gFi.nextFire = gFrame + 12;
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF;
    } else if (aimed && reacted && parOk && !gFi.holdFire && armed && gFrame >= gFi.nextFire && (aaShot ? clear : tpvClear) && !hs) {
        // ((v137) the master tier's bursts shorter far off: 2 bullets beyond 10 m, 1 beyond 20 m). ((v141 beta 7) tier 10's cheat:
        // sustained fire, 4 bullets a pull and the next pull at once while it stays on him)
        const int burst = tk == TK_AUTO ? (cheat ? 24 : BurstFrames(Hand(), dist, master ? (dist > 20000.f ? 5 : dist > 10000.f ? 11 : sk.burst) : sk.burst)) : 8;
        const int gap = cheat ? (tk == TK_PUMP ? 65 : tk == TK_SEMI ? 20 : 0) : tk == TK_PUMP ? std::max(sk.gap, 65) : tk == TK_SEMI ? std::max(sk.gap, 40) : sk.gap;
        gFi.fireUntil = gFrame + burst; gFi.nextFire = gFrame + burst + gap; ++gFi.shots; ++gFi.dryShots;
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF;
        if (gEn.id == p.id) { if (gEn.shot < 0) { gEn.shot = gFrame; CheatPull(); } ++gEn.pulls; }
        if (aaShot) { ++gAa.shots; ++gAaShots; if (gEn.id == p.id) ++gEn.aaPulls; }
    }
    // no line of fire (a wall, an ally): a step aside, toward it (a drop that way: the other side, else none). (v137) The
    // side kept, chosen once: the one whose line opens nearer (0.7, 1.4, 2.1 m), else its habit's; again after 5 s still
    // blocked (v136 changed sides every 2 s: a strafe to and fro, the fights' "dance")
    if ((!clear || (!tpvClear && !aaShot)) && gFrame - gFi.readySince > (LevelNow() >= 6.f ? 15 : 40)) {   // ((v141 beta 6, aim) ELITE and up: sooner; (v150-host beta 18) his height out of its bullet's too)
        const float dx = p.pos[0] - gMe.pos[0], dz = p.pos[2] - gMe.pos[2], dl = std::max(1.f, std::sqrt(dx * dx + dz * dz));
        if (gFi.asideSide == 0 || gFrame - gFi.asideAt > 300) {
            int best = 0; float bestOff = 1e9f;
            for (const int s : {1, -1})
                for (const float off : {700.f, 1400.f, 2100.f}) {
                    if (!StepGround(std::atan2(-dz * s, dx * s), off)) break;
                    const float q[3] = {mz[0] - dz / dl * s * off, mz[1], mz[2] + dx / dl * s * off};
                    if (LineClear(q, tAt)) { if (off < bestOff) { bestOff = off; best = s; } break; }
                }
            if (!best) best = gFi.asideSide ? gFi.asideSide : HabitSide();
            if (gDebug && best != gFi.asideSide) Log("fight: no line of fire at %s: a step to its %s (%s)", p.name.c_str(), best > 0 ? "right" : "left",
                                                     bestOff < 1e8f ? "the line opens there" : "its side");
            gFi.asideSide = best; gFi.asideAt = gFrame;
        }
        for (const int s : {gFi.asideSide, -gFi.asideSide}) {
            const float vx = dx * 0.3f - dz * s, vz = dz * 0.3f + dx * s;
            if (StepGround(std::atan2(vx, vz), 700.f)) { Steer(vx, vz, kStep); if (s != gFi.asideSide) { gFi.asideSide = s; gFi.asideAt = gFrame; } break; }
        }
    }
}

// ---- (v141 beta 6) the first person view weapons: the SVD (0xF), the MOSIN NAGANT (0x10), the RPG-7 (0x11). Held, they lock the
// view in the first person and the player where he stands (section 3: only another weapon takes them off), so a stay in that
// view is a state machine of its own, bounded. Entered for a valid shot only (FpvEntryOk: the target in sight at the weapon's
// range, the line clear, no other enemy close, not under fire, not on the move; the RPG-7 its range and splash), left as soon
// as that reason is gone (FpvWatch: the target lost past its memory, too close, another enemy close, hit from elsewhere, no
// rounds, the objective; a move asked: FpvAfterBrain), each state timed by the tier (FpvTimesNow: a higher tier sets up and
// abandons faster, never stays longer); the way out by the weapon window, then other ways in turn when it does not open (v133:
// 16 swaps out of a scope in 99 failed; the v141 benches: a window that never opened kept a sniper 12 s in its scope until it
// was killed), a watchdog over it all. MGO1_DOLL_FPV_SM=0: beta 5's; MGO1_DOLL_FPV_DEBUG=1: every change logged ----
enum FpvState { FPV_NONE, FPV_ENTERING, FPV_AIMING, FPV_FIRING, FPV_RECOVERING, FPV_EXITING, FPV_ABORTING };
const char* const kFpvNames[] = {"NONE", "ENTERING", "AIMING", "FIRING", "RECOVERING", "EXITING", "ABORTING"};
struct FpvRun {
    int state = FPV_NONE, weapon = -1, target = -1, way = -1, ways = 0, shots = 0, fiShots = 0;   // (fiShots: the fight's pulls last seen)
    long long since = -1, stayAt = -1, progressAt = -1, shotAt = -100000, pendingAt = -1, wayAt = -1, aimAt = -1;
    float aimErr = 1e9f;                     // the view's least error to the one fought this aim (radians): its progress (aimAt)
    const char* why = "";                    // the reason of the state now
    const char* pending = nullptr;           // an abort seen: acted on at pendingAt (the tier's fpv_abort reaction)
    float at[3] = {0, 0, 0};                 // where the stay began, or last moved 0.5 m (the watchdog)
    bool test = false;                       // (MGO1_DOLL_FPV_TEST=stuck) this stay's exits held but the watchdog's
    std::string act;                         // (the move asked: its activity, for the line)
};
FpvRun gFv;
long long gFvNoEnterUntil = -1;              // no new stay until (an exit's reason: FpvCooldown)
long long gFvOppSince = -1; int gFvOppReact = 0;   // a valid shot seen since, the tier's reaction to it
long long gFvSawAt = -100000;                // an enemy last in sight (a sniper's watch)
long long gFvOrderAt = -100000;              // the order last changed (the move it asks: "order")
long long gFvTestAt = -100000;               // (MGO1_DOLL_FPV_TEST=stuck) the last test stay
const char* gFvAsk = nullptr;                // why the weapon window was asked for or away from one (FpvWatch takes it)
const char* gFvNo = "";                      // the last check FpvEntryOk failed
const char* gFvReasonLast = "";              // the fight's last reason (why no shot came: the aim's timeout)
int gFvStaysAll = 0, gFvDogAll = 0;          // (the status) the stays, the watchdog's recoveries, this run
struct FpvCount { long long since = -1, longest = 0; int stays = 0, shots = 0, watchdog = 0; std::map<std::string, int> out, no; };
FpvCount gFvc;                               // the minute's (the "AI DOLL_n minute: fpv" line)
bool FpvOn() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_FPV_SM"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
bool FpvDebug() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_FPV_DEBUG"); v = e && *e && *e != '0' ? 1 : 0; } return v == 1 || gDebug >= 2; }
// (tests) MGO1_DOLL_FPV_TEST, a list: "stuck" a stay a minute whose exits are held but the watchdog's (section 98 G); "rpg" the
// sniper's rifle ruled as the RPG-7 (its range, its splash checks, one shot: E and F with no RPG-7 at hand); "order" the order
// "follow" (its nearest ally) given in a stay, 1.5 s in, every 40 s, as the server gives it (D)
bool FpvTestOn(const char* what) { static const char* t = getenv("MGO1_DOLL_FPV_TEST"); return t && strstr(t, what) != nullptr; }
bool RpgRules(int code) { return code == 0x11 || ((code == 0xF || code == 0x10) && FpvTestOn("rpg")); }
long long gFvOrderTestAt = -100000;          // (MGO1_DOLL_FPV_TEST=order) the last order given
// an order that moves it (follow: its ally 6 m away or more; hide): no stay begun, a stay left (section 91)
bool FpvOrderMoves() {
    if (gSet.order == "hide") return true;
    if (gSet.order != "follow") return false;
    const Player* f = PlayerByName(gSet.target);
    return f && f->alive && !Enemy(*f) && Dist2D(gMe.pos, f->pos) > 6000.f;
}
// (its flight and blast not measured: taken for a shot from tier 6, ELITE; MGO1_DOLL_RPG=0 never)
bool RpgUse() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_RPG"); v = e && *e == '0' ? 0 : 1; } return v == 1 && LevelNow() >= 6.f && !CheatOn(); }   // ((v141 beta 7) tier 10's cheat: none)
const char* FpvName(int c) {
    if (c != 0x11 && RpgRules(c)) return "RPG7_TEST";
    switch (c) {
    case 0xF: return "SVD"; case 0x10: return "MOSIN"; case 0x11: return "RPG7"; case 0xC: return "AK47"; case 0xB: return "XM16E1";
    case 0xA: return "SCORPION"; case 0xE: return "M37"; case 0xD: return "M63"; case 6: return "M1911A1"; case 5: return "MK22"; case 1: return "KNIFE";
    default: return c < 0 ? "NONE" : "OTHER";
    }
}
// ---- (v141 beta 6) the maps' sightlines (tests/bench/tools/sightlines.cpp: 300 standing points over each map's mesh, the eye 1.5 m
// up, 16 level lines up to 80 m in its collision): the points with a line of 30 m or more (%), the lines' median (m). Close: under
// a quarter of the points; open: three quarters and more; mixed between ----
enum MapKind { MAP_CLOSE, MAP_MIXED, MAP_OPEN };
const char* const kMapKindNames[3] = {"close", "mixed", "open"};
struct MapProfile { const char* name; int kind; int views; float median; };
const MapProfile kMapProfiles[16] = {
    {"unknown", MAP_MIXED, 50, 6.0f},
    {"Lost_Forest", MAP_OPEN, 75, 9.0f}, {"Ghost_Factory", MAP_MIXED, 66, 9.3f}, {"City_Under_Siege", MAP_CLOSE, 20, 4.1f},
    {"Killhouse_A", MAP_CLOSE, 0, 4.8f}, {"Killhouse_B", MAP_MIXED, 35, 6.1f}, {"Killhouse_C", MAP_CLOSE, 5, 4.1f},
    {"Svyatogornyj_East", MAP_MIXED, 50, 6.6f}, {"Mountaintop", MAP_OPEN, 81, 7.8f}, {"Graniny_Gorki_Lab", MAP_CLOSE, 3, 2.9f},
    {"Pillbox_Purgatory", MAP_OPEN, 100, 38.5f}, {"High_Ice", MAP_OPEN, 92, 28.0f}, {"Brown_Town", MAP_MIXED, 59, 6.7f},
    {"Silo_Entrance", MAP_CLOSE, 16, 4.0f}, {"REX_Hangar", MAP_CLOSE, 1, 5.7f}, {"Big_Shell_Strut_E", MAP_CLOSE, 21, 5.5f}};
int MapNo() {                                            // 1 .. 15 (the lobby's numbers), 0 not known
    static const char* const kPages[12] = {"n001a", "n002a", "n003a", "n011a", "n012a", "n013a", "n021a", "n022a", "n024a", "n031a", "n032a", "n033a"};
    if (gMap) {
        if (gMap->extra >= 13 && gMap->extra <= 15) return gMap->extra;
        for (int i = 0; i < 12; ++i) if (gMap->page == kPages[i]) return i + 1;
    }
    return gRoom.map >= 1 && gRoom.map <= 15 ? gRoom.map : 0;
}
const MapProfile& MapProf() { return kMapProfiles[MapNo()]; }
int MapKindNow() { return MapProf().kind; }
// the tier's times (frames), the level now between TRAINING's and PRO's: the window and the view coming on (ENTERING), the aim
// without a shot (AIMING), the target's memory out of sight, the way out by the window before the other ways (EXITING), the
// watchdog (no shot, no move, no new sight). A tight map abandons sooner (the aim, the memory)
struct FpvTimes { int enter, aim, memory, exit, watchdog; };
FpvTimes FpvTimesNow() {
    const float t = (std::max(1.f, std::min(10.f, LevelNow())) - 1.f) / 9.f;
    auto mix = [t](float a, float b) { return (int)std::lround(a + (b - a) * t); };
    FpvTimes T{mix(240, 180), mix(360, 180), mix(240, 90), mix(180, 120), mix(540, 300)};
    if (MapKindNow() == MAP_CLOSE) { T.aim = T.aim * 4 / 5; T.memory = T.memory * 7 / 10; }
    return T;
}
// the ranges (mm): a stay begun from (enter), kept down to (keep); one asleep from 2 m (finished where he lies)
float FpvEnterRange(int code) { return RpgRules(code) ? 18000.f : MapKindNow() == MAP_CLOSE ? 16000.f : MapKindNow() == MAP_OPEN ? 12000.f : 14000.f; }
float FpvKeepRange(int code) { return RpgRules(code) ? 15000.f : MapKindNow() == MAP_CLOSE ? 10000.f : MapKindNow() == MAP_OPEN ? 8000.f : 9000.f; }
int FpvHeldCode() { const int h = Hand(); return h > 0 && LocksView(h) ? h : -1; }
int FpvBackup() {                                        // the weapon a stay leaves for: the main gun, else the handgun, else any
    const int g = MainGun();
    if (g > 0 && !LocksView(g)) return g;
    const int h = Handgun();
    if (h > 0) return h;
    for (int c : {1, 0x13, 0x15, 0x17, 0x14, 0x16}) if (HasWeapon(c)) return c;
    return -1;
}
bool FpvExitWanted() { return FpvOn() && (gFv.state == FPV_EXITING || gFv.state == FPV_ABORTING) && FpvHeldCode() > 0; }
bool FpvWindowHeld(int code) { return FpvOn() && gFv.test && gFv.state != FPV_ABORTING && code >= 0 && !LocksView(code); }
bool FpvKeepOk() { return !FpvOn() || (gFv.state != FPV_EXITING && gFv.state != FPV_ABORTING); }
// no new stay for a while after an exit, by its reason (frames; 1 s else)
void FpvCooldown(const char* why) {
    static const struct { const char* why; int frames; } k[] = {
        {"close_attacker", 180}, {"target_too_close", 120}, {"unsafe_range", 180}, {"rushed", 180}, {"hit_from_another_direction", 150},
        {"under_fire", 150}, {"aim_timeout", 300}, {"shot_blocked", 300}, {"ally_in_line", 120}, {"ally_near_line", 120},
        {"ally_near_target", 120}, {"watchdog", 600}, {"stay_timeout", 300}, {"enter_timeout", 180}, {"movement_required", 120},
        {"order", 120}, {"objective", 300}, {"no_ammo", 600}, {"blinded", 120}, {"shot_done", 240}};
    int f = 60;
    for (const auto& e : k) if (!strcmp(e.why, why)) f = e.frames;
    gFvNoEnterUntil = std::max(gFvNoEnterUntil, gFrame + f);
}
// a change of state: "DOLL FPV: weapon=SVD from=AIMING to=EXITING reason=target_lost after=1.4s" (after: the time in the state
// left). Always a stay's start, its view on, its way out, its end; the shots' own states with MGO1_DOLL_FPV_DEBUG
void FpvSet(int state, const char* why) {
    const int from = gFv.state;
    const long long after = gFv.since >= 0 ? gFrame - gFv.since : 0;
    gFv.state = state; gFv.since = gFrame; gFv.why = why; gFv.pending = nullptr;
    if (state == FPV_AIMING || state == FPV_FIRING || state == FPV_ENTERING) gFv.progressAt = gFrame;
    if (state == FPV_ABORTING) { gFv.way = -1; gFv.wayAt = -1; }
    const bool always = state == FPV_ENTERING || state == FPV_EXITING || state == FPV_ABORTING || state == FPV_NONE ||
                        (state == FPV_AIMING && (from == FPV_ENTERING || from == FPV_NONE));
    if (!always && !FpvDebug()) return;
    char tail[200] = "";
    int n = 0;
    const Player* t = gFv.target >= 0 ? PlayerById(gFv.target) : nullptr;
    if (t) n += snprintf(tail + n, sizeof tail - n, " target=%s distance=%.1f", t->name.c_str(), Dist2D(gMe.pos, t->pos) / 1000.f);
    if (state == FPV_NONE) n += snprintf(tail + n, sizeof tail - n, " stay=%.1fs shots=%d", (gFrame - gFv.stayAt) / 60.0, gFv.shots);
    if (state == FPV_EXITING && !gFv.act.empty()) snprintf(tail + n, sizeof tail - n, " act=%s", gFv.act.c_str());
    Log("DOLL FPV: weapon=%s from=%s to=%s reason=%s after=%.1fs%s", FpvName(gFv.weapon), kFpvNames[from], kFpvNames[state], why, after / 60.0, tail);
}
void FpvBegin(int code, const char* why, int state) {
    gFv = FpvRun();
    gFv.weapon = code; gFv.stayAt = gFrame; gFv.target = gFi.target; gFv.fiShots = gFi.shots; memcpy(gFv.at, gMe.pos, sizeof gFv.at);
    gFv.test = why && !strcmp(why, "test_stuck");
    ++gFvc.stays; ++gFvStaysAll;
    FpvSet(state, why ? why : "weapon_choice");
}
void FpvEnd(const char* why) {
    gFvc.longest = std::max(gFvc.longest, gFrame - gFv.stayAt);
    if (gFv.state != FPV_EXITING && gFv.state != FPV_ABORTING) ++gFvc.out[why];
    FpvSet(FPV_NONE, why);
    gFv.weapon = -1; gFv.target = -1; gFv.test = false; gFv.act.clear();
}
void FpvExit(const char* why) {
    if (gFv.state == FPV_EXITING || gFv.state == FPV_ABORTING) return;
    ++gFvc.out[why];
    FpvCooldown(why);
    FpvSet(FPV_EXITING, why);
}
// (section 95) the RPG-7's safety: its range, no ally by the target (7 m) or along its line (3 m), the line clear from the eye and
// the tube under it, no other enemy rushing it (12 m); nullptr: safe
const char* RpgUnsafe(const Player& t, float dist) {
    if (dist < FpvKeepRange(0x11)) return "unsafe_range";
    float eye[3], c[3]; SeeEye(eye); ChestPoint(t, c);
    for (const Player& a : gPlayers) {
        if (a.local || !a.alive || !Ally(a)) continue;
        if (Dist2D(a.pos, t.pos) < 7000.f) return "ally_near_target";
        float ac[3]; BodyPoint(a.pos, 0.45f, ac);
        if (Dist2D(eye, a.pos) < dist + 1000.f && SegDist(eye, c, ac) < 3000.f) return "ally_near_line";
    }
    const float tube[3] = {eye[0], eye[1] - 150.f, eye[2]};
    if (!LineClear(eye, c) || !LineClear(tube, c)) return "shot_blocked";
    for (const auto& k : gKnown) {
        if (k.first == t.id || gFrame - k.second.last > 120) continue;
        const Player* e = PlayerById(k.first);
        if (e && e->alive && Enemy(*e) && Dist2D(gMe.pos, k.second.visible ? e->pos : k.second.pos) < 12000.f) return "rushed";
    }
    return nullptr;
}
// worth a rocket: still (under 15 mm a frame), or another enemy within 6 m of him
bool RpgWorth(const Player& t) {
    const auto sp = gStepSpeed.find(t.id);
    if (sp == gStepSpeed.end() || sp->second < 15.f) return true;
    for (const Player& o : gPlayers) if (o.id != t.id && o.alive && Enemy(o) && Dist2D(o.pos, t.pos) < 6000.f) return true;
    return false;
}
const char* FpvFireBlock(const Player& p, float dist) {   // (the RPG-7: one shot, safe; nullptr: fire)
    if (!FpvOn() || !RpgRules(Hand())) return nullptr;
    if (gFv.shots > 0) return "shot_done";
    return RpgUnsafe(p, dist);
}
// a tight place: under 3 of 8 level lines from its eye reach 8 m (a room, a corridor): no stay begun there (section 90)
bool TightHere() {
    static long long at = -100; static bool tight = false; static float pos[3] = {0, 0, 0};
    if (gFrame - at < 30 && Dist2D(pos, gMe.pos) < 500.f) return tight;
    at = gFrame; memcpy(pos, gMe.pos, sizeof pos);
    float eye[3]; MyEye(eye);
    int open = 0;
    for (int j = 0; j < 8 && open < 3; ++j) {
        const float a = j * 0.7853982f, b[3] = {eye[0] + std::sin(a) * 8000.f, eye[1], eye[2] + std::cos(a) * 8000.f};
        if (LineClear(eye, b)) ++open;
    }
    tight = open < 3;
    return tight;
}
// (section 87) a valid shot for this weapon now: the fight's target (or, no fight: a sniper's watch at its spot, an enemy seen at
// its range within the tier's memory); false with the first check that fails (gFvNo). A valid one held the tier's reaction first
bool FpvEntryOk(int code, const char* ask) {
    if (!FpvOn()) return true;
    auto no = [](const char* why) {
        if (strcmp(gFvNo, why)) ++gFvc.no[why];
        gFvNo = why; gFvOppSince = -1;
        return false;
    };
    if (code <= 0 || !HasWeapon(code)) return no("not_had");
    if (Empty(code)) return no("no_ammo");
    if (gFrame < gFvNoEnterUntil) return no("cooldown");
    if (Carrying()) return no("objective");
    if (Following()) return no("moving");
    if (FpvOrderMoves()) return no("order");
    if (Blind()) return no("blinded");
    // (merge review) the radio's menu up: the weapon window does not open (its close first, RadioPad)
    if (const uint32_t rw = RadioHud(); rw && (R32(rw + 0x180) & 0x10)) return no("radio_on");
    const FpvTimes T = FpvTimesNow();
    const Player* p = gFi.target >= 0 ? PlayerById(gFi.target) : nullptr;
    // (merge review) the fights' rule from level 4: no first person view on a target more than 50 degrees off (turned to first;
    // the swap's window freezes the turn, the scope turns slowly)
    if (p && LevelNow() >= 4.f && std::fabs(Wrap(AngleTo(gMe.pos, p->pos) - Facing())) > 50.f * kDeg) return no("far_off");
    if (gFrame - gHurtAt < 180) {                        // (hurt these 3 s: by another than the one fought, or under half its life: no)
        bool byTarget = false, other = false;
        for (const auto& k : gKnown) {
            const Player* e = k.second.visible ? PlayerById(k.first) : nullptr;
            if (e && e->alive && Enemy(*e) && std::fabs(Wrap(AngleTo(e->pos, gMe.pos) - e->facing)) < 20.f * kDeg && (e->bits[1] & 0x4400) != 0)
                (p && k.first == p->id ? byTarget : other) = true;
        }
        if (other || !byTarget || (gMyMaxLife > 0 && gMe.life * 2 < gMyMaxLife)) return no("under_fire");
    }
    if (p) {
        if (!p->alive) return no("target_dead");
        const auto kn = gKnown.find(p->id);
        if (kn == gKnown.end() || !kn->second.visible) return no("target_not_visible");
        const float dist = Dist2D(gMe.pos, p->pos);
        if (dist < (AsleepDown(*p) && !RpgRules(code) ? 2000.f : FpvEnterRange(code))) return no(RpgRules(code) ? "unsafe_range" : "too_close");
        float eye[3], c[3], h[3]; SeeEye(eye); ChestPoint(*p, c); HeadPoint(*p, h);
        if (!LineClear(eye, c) && !LineClear(eye, h)) return no("no_line");
        if (RpgRules(code)) { if (const char* w = RpgUnsafe(*p, dist)) return no(w); if (!RpgWorth(*p)) return no("not_worth"); }
    } else {
        if (RpgRules(code)) return no("no_target");
        bool any = false;
        for (const auto& k : gKnown) if (gFrame - k.second.seen <= T.memory && Dist2D(gMe.pos, k.second.seenPos) >= FpvKeepRange(code)) any = true;
        if (!any) return no("no_target");
    }
    for (const auto& k : gKnown) {                       // (another enemy close: seen or heard these 3 s within 12 m)
        if (p && k.first == p->id) continue;
        if (gFrame - k.second.last > 180) continue;
        const Player* e = PlayerById(k.first);
        if (!e || !e->alive || !Enemy(*e) || Asleep(*e)) continue;
        if (Dist2D(gMe.pos, k.second.visible ? e->pos : k.second.pos) < 12000.f) return no("enemy_close");
    }
    if (TightHere()) return no("tight_area");
    if (gFvOppSince < 0) { gFvOppSince = gFrame; gFvOppReact = ReactFrames(RF_WEAPON_SWITCH); }
    if (gFrame - gFvOppSince < gFvOppReact) { gFvNo = "recognizing"; return false; }
    gFvNo = "";
    gFvAsk = ask;
    return true;
}
// (section 89) why a stay should end now (nullptr: it holds): the objective, no rounds, the flash; the one fought too close,
// another enemy close; hit from elsewhere, or losing the duel; the one fought dead or lost past the memory (the Mosin after a
// shot: a shorter re-acquire), no enemy seen for a watch; the RPG-7's own safety
const char* FpvAbortWhy(const FpvTimes& T) {
    const int w = gFv.weapon;
    if (Carrying()) return "objective";
    if (FpvOrderMoves()) return "order";
    if (Empty(w)) return "no_ammo";
    if (Blind()) return "blinded";
    for (const auto& k : gKnown) {
        if (gFrame - k.second.last > 120) continue;
        const Player* e = PlayerById(k.first);
        if (!e || !e->alive || !Enemy(*e)) continue;
        const float d = Dist2D(gMe.pos, k.second.visible ? e->pos : k.second.pos);
        if (k.first == gFv.target) {
            if (k.second.visible && d < (AsleepDown(*e) && !RpgRules(w) ? 2000.f : FpvKeepRange(w))) return RpgRules(w) ? "unsafe_range" : "target_too_close";
        } else if (!Asleep(*e) && d < 8000.f) return RpgRules(w) ? "rushed" : "close_attacker";
    }
    if (gFrame - gHurtAt <= 60 && gHurtAt > gFv.stayAt + 5) {
        bool byTarget = false, other = false;
        for (const auto& k : gKnown) {
            const Player* e = k.second.visible ? PlayerById(k.first) : nullptr;
            if (!e || !e->alive || !Enemy(*e)) continue;
            if (std::fabs(Wrap(AngleTo(e->pos, gMe.pos) - e->facing)) < 20.f * kDeg && (e->bits[1] & 0x4400) != 0) (k.first == gFv.target ? byTarget : other) = true;
        }
        if (other || !byTarget) return "hit_from_another_direction";
        int hits = 0;
        for (long long f : gHits) if (gFrame - f <= 90) ++hits;
        if (hits >= 2 && gMyMaxLife > 0 && gMe.life * 2 < gMyMaxLife) return "under_fire";
    }
    const long long memo = w == 0x10 && gFv.shots > 0 ? T.memory * 3 / 4 : T.memory;
    if (gFv.target >= 0) {
        const Player* t = PlayerById(gFv.target);
        if (!t || !t->alive || t->life <= 0) return "target_dead";
        const auto kn = gKnown.find(gFv.target);
        if (kn == gKnown.end() || gFrame - kn->second.seen > memo) return "target_lost";
        if (RpgRules(w) && kn->second.visible && gFv.shots == 0) { if (const char* r = RpgUnsafe(*t, Dist2D(gMe.pos, t->pos))) return r; }
    } else if (gFrame - gFvSawAt > memo) return "no_target_timeout";
    return nullptr;
}
const char* FpvAimWhy() {                                // (no shot within the aim's time: why, from the fight's last reason)
    for (const char* r : {"unsafe_range", "ally_near_line", "ally_near_target", "rushed", "shot_blocked"}) if (!strcmp(gFvReasonLast, r)) return r;
    if (!strcmp(gFvReasonLast, "no_line_of_fire")) return "shot_blocked";
    if (!strcmp(gFvReasonLast, "ally_in_line")) return "ally_in_line";
    if (!strcmp(gFvReasonLast, "target_hidden_a_moment")) return "target_lost";
    return "aim_timeout";
}
void FpvMinute() {                                       // ("AI DOLL_n minute: fpv ..." with MGO1_DOLL_AI_DEBUG or MGO1_DOLL_FPV_DEBUG)
    if (gFvc.since < 0) { gFvc = FpvCount(); gFvc.since = gFrame; return; }
    if (gFrame - gFvc.since < 3600) return;
    if (gFv.state != FPV_NONE) gFvc.longest = std::max(gFvc.longest, gFrame - gFv.stayAt);
    if ((AiDebug() || FpvDebug()) && (gFvc.stays || !gFvc.no.empty())) {
        std::string outs, nos;
        for (const auto& kv : gFvc.out) outs += (outs.empty() ? "" : ", ") + kv.first + " " + std::to_string(kv.second);
        for (const auto& kv : gFvc.no) nos += (nos.empty() ? "" : ", ") + kv.first + " " + std::to_string(kv.second);
        Log("AI DOLL_%d minute: fpv stays %d, the longest %.1f s, shots %d, watchdog %d; out by %s; not entered: %s", gSet.number, gFvc.stays,
            gFvc.longest / 60.0, gFvc.shots, gFvc.watchdog, outs.empty() ? "none" : outs.c_str(), nos.empty() ? "none" : nos.c_str());
    }
    gFvc = FpvCount(); gFvc.since = gFrame;
}
// the watchdog (section 92): in a stay, no shot, no new sight of the one fought, no move of 0.5 m for its time: forced out
// (ABORTING: the ways in turn), the line; again each time while held
void FpvWatchdog(const FpvTimes& T) {
    if (gFv.state == FPV_NONE) return;
    if (Dist2D(gMe.pos, gFv.at) > 500.f) { memcpy(gFv.at, gMe.pos, sizeof gFv.at); gFv.progressAt = gFrame; }
    if (gFrame - gFv.progressAt < T.watchdog) return;
    ++gFvc.watchdog; ++gFvDogAll;
    const Player* t = gFv.target >= 0 ? PlayerById(gFv.target) : nullptr;
    const auto kn = gFv.target >= 0 ? gKnown.find(gFv.target) : gKnown.end();
    Log("DOLL_%d FPV watchdog: weapon=%s state=%s duration=%lldms target=%s visible=%d movement=%.2fm shots=%d action=ABORT_AND_SWITCH%s", gSet.number,
        FpvName(gFv.weapon), kFpvNames[gFv.state], (gFrame - gFv.stayAt) * 1000 / 60, t ? t->name.c_str() : "NONE", kn != gKnown.end() && kn->second.visible ? 1 : 0,
        Dist2D(gMe.pos, gFv.at) / 1000.f, gFv.shots, gFv.test ? " test=1" : "");
    gFv.progressAt = gFrame; gFv.test = false;
    ++gFvc.out["watchdog"];
    FpvCooldown("watchdog");
    if (gFv.state != FPV_ABORTING) FpvSet(FPV_ABORTING, "watchdog");
    else gFv.wayAt = -100000;                            // (the next way at once)
}
void FpvShot() {                                         // (a pull of the fight's with such a weapon: FIRING)
    if (!FpvOn() || gFv.state == FPV_NONE || FpvHeldCode() < 0) return;
    ++gFv.shots; ++gFvc.shots; gFv.shotAt = gFrame;
    if (gFv.weapon == 0x10) gFi.nextFire = std::max(gFi.nextFire, gFrame + 78);   // (one shot, then the bolt: 1.3 s)
    FpvSet(FPV_FIRING, "shot");
}
// the ways out when the window does not open (ABORTING, in turn): the window afresh, Cross then the window (the posture changed),
// R2 tapped (the previous weapon of the list), the window to another weapon, Square light then the window
struct FpvWay { const char* name; const char* out; int frames; };
const FpvWay kFpvWays[5] = {{"window", "out_by_window", 150}, {"cross_then_window", "out_by_cross_then_window", 170}, {"r2_tap", "out_by_r2_tap", 60},
                            {"window_other", "out_by_other_weapon", 150}, {"square_then_window", "out_by_square_then_window", 180}};
// every frame of the brain (before the windows and the fight): the stay's states from what is held and what was asked, the
// timeouts, the aborts after the tier's fpv_abort reaction, the watchdog
void FpvWatch() {
    if (!FpvOn()) return;
    FpvMinute();
    for (const auto& k : gKnown) if (k.second.visible) { gFvSawAt = gFrame; break; }
    if (gFv.test && gFvSawAt == gFrame) { gFv.test = false; Log("DOLL FPV: test: an enemy in sight, the stay's own rules again"); }
    { static std::string order; const std::string now = gSet.order + " " + gSet.target; if (now != order) { if (!order.empty()) gFvOrderAt = gFrame; order = now; } }
    // (MGO1_DOLL_FPV_TEST=order) the order "follow" its nearest ally 1.5 s into a stay, every 40 s, "free" again 15 s later
    if (FpvTestOn("order")) {
        if (gFvOrderTestAt > 0 && gFrame - gFvOrderTestAt > 900 && gSet.order == "follow") { gSet.order = "free"; gSet.target.clear(); Log("DOLL FPV: test: the order free again"); }
        if (gFv.state >= FPV_AIMING && gFv.state <= FPV_RECOVERING && gFrame - gFv.stayAt > 90 && gFrame - gFvOrderTestAt > 2400 && gSet.order == "free") {
            const Player* best = nullptr;
            for (const Player& a : gPlayers) if (!a.local && a.alive && Ally(a) && (!best || Dist2D(a.pos, gMe.pos) < Dist2D(best->pos, gMe.pos))) best = &a;
            if (best && Dist2D(best->pos, gMe.pos) > 6000.f) {
                gFvOrderTestAt = gFrame; gSet.order = "follow"; gSet.target = best->name;
                Log("DOLL FPV: test: the order follow %s in the view (%.1f m away), as the server's", best->name.c_str(), Dist2D(best->pos, gMe.pos) / 1000.f);
            }
        }
    }
    const int held = FpvHeldCode();
    const int asked = gSw[0].want >= 0 && gSw[0].start >= 0 ? gSw[0].want : -1;
    const char* ask = gFvAsk;
    gFvAsk = nullptr;
    const FpvTimes T = FpvTimesNow();
    if (gFv.state != FPV_NONE && gFi.target >= 0 && gFi.target != gFv.target) { gFv.target = gFi.target; gFv.progressAt = gFrame; }
    switch (gFv.state) {
    case FPV_NONE:
        if (asked > 0 && LocksView(asked)) FpvBegin(asked, ask ? ask : "weapon_choice", FPV_ENTERING);
        else if (held > 0) FpvBegin(held, "found_in_hands", FPV_AIMING);
        return;
    case FPV_ENTERING:
        if (held > 0) { gFv.weapon = held; FpvSet(FPV_AIMING, "in_hands"); return; }
        if (asked != gFv.weapon) { gFvNoEnterUntil = std::max(gFvNoEnterUntil, gFrame + 60); FpvEnd(asked < 0 ? "window_failed" : "entry_dropped"); return; }
        if (gFrame - gFv.since > T.enter) {
            if (gSw[0].want == gFv.weapon) { gSw[0] = Swap(); gSw[0].doneAt = gFrame; }   // (the window let go, as its own give-up does)
            FpvCooldown("enter_timeout"); FpvEnd("enter_timeout");
        }
        return;
    case FPV_AIMING: case FPV_FIRING: case FPV_RECOVERING: {
        if (held < 0) { FpvEnd("out"); return; }
        if (asked >= 0 && !LocksView(asked)) {           // (the brain's own way out: the fight's choice, a move, the rest weapon)
            const char* why = ask ? ask : "weapon_choice";
            FpvExit(why);
            return;
        }
        if (gFi.shots > gFv.fiShots) FpvShot();           // (the fight's pull with it: FIRING)
        gFv.fiShots = gFi.shots;
        if (gFv.state == FPV_FIRING && gFrame >= gFi.fireUntil) FpvSet(FPV_RECOVERING, gFv.weapon == 0x10 ? "bolt" : RpgRules(gFv.weapon) ? "rocket" : "trigger_release");
        if (gFv.state == FPV_RECOVERING) {
            if (RpgRules(gFv.weapon) && gFrame - gFv.since >= 45) { FpvExit("shot_done"); return; }   // (one rocket, then out)
            if (gFrame >= gFi.nextFire || gFrame - gFv.since > 120) FpvSet(FPV_AIMING, "ready");
        }
        // the aim's progress: the view's error to the one fought (its chest) down by a degree. The scope's view turns slowly (the
        // fpv bench: 2.4 to 3.2 s at 20 m from 60 to 80 degrees off, tier 9): an aim still closing in is a shot to come, its time
        // extended, twice the aim's time at most
        if (gFv.state == FPV_AIMING && gFv.target >= 0) {
            float m[16];
            const Player* t = PlayerById(gFv.target);
            if (gFv.aimAt < gFv.since) { gFv.aimAt = gFv.since; gFv.aimErr = 1e9f; }
            if (t && ps2world::ViewMatrix(m)) {
                float c[3]; ChestPoint(*t, c);
                const float err = std::fabs(Wrap(std::atan2(c[0] - m[12], c[2] - m[14]) - std::atan2(m[8], m[10])));
                if (err < gFv.aimErr - 1.f * kDeg) { gFv.aimErr = err; gFv.aimAt = gFv.progressAt = gFrame; }   // (the watchdog's "aim progress" too)
            }
        }
        if (!gFv.test) {
            const char* why = FpvAbortWhy(T);
            if (!why && gFv.state == FPV_AIMING && gFv.target >= 0 && gFi.target >= 0 &&
                (gFrame - std::max({gFv.since, gFv.shotAt, gFv.aimAt}) > T.aim || gFrame - std::max(gFv.since, gFv.shotAt) > 2 * T.aim)) why = FpvAimWhy();
            if (!why && gFrame - gFv.stayAt > 3600) why = "stay_timeout";
            if (!why) gFv.pending = nullptr;
            else if (!gFv.pending) { gFv.pending = why; gFv.pendingAt = gFrame + ReactFrames(RF_FPV_ABORT); }
            else if (gFrame >= gFv.pendingAt) { FpvExit(gFv.pending); return; }
        }
        break;
    }
    case FPV_EXITING:
        if (held < 0) { FpvEnd("out"); return; }
        if (gFrame - gFv.since > T.exit) FpvSet(FPV_ABORTING, "exit_timeout");
        break;
    case FPV_ABORTING:
        if (held < 0) { FpvEnd(gFv.way >= 0 && gFv.way < 5 ? kFpvWays[gFv.way].out : "out"); return; }
        break;
    default: break;
    }
    FpvWatchdog(T);
}
// EXITING and ABORTING (true: it has the pad). EXITING: the weapon window to the backup weapon, its retries short; ABORTING: the
// ways in turn (kFpvWays), each said once
bool FpvExitFrame() {
    if (!FpvExitWanted()) return false;
    StopPath(); Steer(0, 0, 0); Act("switching_weapon", "fpv_exit");
    const int back = FpvBackup();
    if (gFv.state == FPV_EXITING) { if (back > 0) SwapTo(back); return true; }
    if (gFv.way < 0 || gFrame - gFv.wayAt >= kFpvWays[gFv.way].frames) {
        gFv.way = (gFv.way + 1) % 5; gFv.wayAt = gFrame; ++gFv.ways;
        if (gSw[0].start < 0) gSw[0].doneAt = -1000;     // (no pause after a window given up)
        Log("DOLL FPV: weapon=%s way=%s try=%d held=%.1fs", FpvName(gFv.weapon), kFpvWays[gFv.way].name, gFv.ways, (gFrame - gFv.stayAt) / 60.0);
    }
    const long long t = gFrame - gFv.wayAt;
    switch (gFv.way) {
    case 0: if (back > 0) SwapTo(back); break;
    case 1: if (t < 4) gPad.buttons |= 0x4000; else if (t >= 24 && back > 0) SwapTo(back); break;
    case 2: if (t < 4) { gPad.buttons |= 0x0200; gTapNoHoldUntil = gFrame + 30; } break;
    case 3: {
        int o = -1;
        for (int c : {Handgun(), MainGun(), 1, 0x13, 0x15, 0x17}) if (o < 0 && c > 0 && c != back && !LocksView(c) && HasWeapon(c)) o = c;
        if (o > 0) SwapTo(o); else if (back > 0) SwapTo(back);
        break;
    }
    default: if (t < 20) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; } else if (t >= 30 && back > 0) SwapTo(back); break;
    }
    return true;
}
// a move asked with such a weapon held (GoTo's Unscope, FpvAfterBrain): out at once
void FpvMoveExit() {
    if (!FpvOn() || gFv.test || gFv.state < FPV_AIMING || gFv.state > FPV_RECOVERING) return;
    FpvExit(gFrame - gFvOrderAt < 180 ? "order" : "movement_required");
}
// (MGO1_DOLL_FPV_TEST=stuck) a test stay a minute, no enemy seen these 5 s: the rifle taken for nothing, its exits held but the
// watchdog's (section 98 G: the watchdog must get it out)
bool FpvTestFrame() {
    if (!FpvTestOn("stuck") || !FpvOn() || gFv.state != FPV_NONE || LifeFrames() < 300 || gFrame - gFvTestAt < 3600 || gFi.target >= 0 || Carrying()) return false;
    for (const auto& k : gKnown) if (k.second.visible || gFrame - k.second.seen < 300) return false;
    const int r = SniperRifle() > 0 ? SniperRifle() : HasWeapon(0x11) ? 0x11 : -1;
    if (r < 0 || Hand() == r || !WindowFree(0)) return false;
    gFvTestAt = gFrame;
    gFvAsk = "test_stuck";
    if (!SwapTo(r)) return false;
    Log("DOLL FPV: test: the %s taken for nothing, its exits held but the watchdog's", FpvName(r));
    Act("testing", "fpv_stuck");
    return true;
}
void FpvNewLife() {                                      // (a spawn: the stay over)
    if (gFv.state != FPV_NONE) FpvEnd("died");
    gFv = FpvRun(); gFvOppSince = -1; gFvNoEnterUntil = -1; gFvAsk = nullptr;
}

// ---- grenades (probe 16, measured in the open; the model below matches the recorded flights within a
// few cm over a second): thrown from the first person view (R1 held), Square held 20 frames then let go:
// the grenade (NewBulletGrenadeWithBuller) appears 53 to 57 frames later, 0.6 m ahead of the eye (8
// degrees to the left of the view) and 0.29 m under it (less the higher the view: 4 mm a degree), flies
// straight 12 frames, then each tick of 2 frames: the horizontal speed 0.98 of itself, the vertical one
// 0.983 of itself less 12 mm a frame rising, 12 less falling; it bounces keeping a quarter of its speed
// (0.26 along the surface, 0.21 across it), rolls (12 mm a tick slower each tick) and goes off 108 frames
// after it appeared (a lob of 40 degrees and more goes off in the air). Its speed (mm a frame) by the
// view's pitch (Square held longer changes nothing): on flat ground it goes off 4.8 m ahead at -30
// degrees, 7.1 m at -20, 13 m at -10, 20 m at 0, 23 m at 10 to 20 ----
struct ThrowRow { float pitch, h, v; };
const ThrowRow kThrow[] = {{-30.f, 364.5f, -163.1f}, {-20.f, 409.8f, -89.5f}, {-10.f, 433.2f, -16.6f}, {0.f, 434.5f, 59.8f}, {10.f, 409.1f, 134.4f},
                           {20.f, 361.6f, 201.9f}, {30.f, 295.1f, 266.5f}, {40.f, 222.1f, 320.0f}, {50.f, 149.9f, 363.9f}};
void ThrowSpeed(float pitchDeg, float* h, float* v) {
    const int n = (int)(sizeof kThrow / sizeof kThrow[0]);
    int i = 0;
    while (i + 2 < n && pitchDeg > kThrow[i + 1].pitch) ++i;
    const float t = std::max(0.f, std::min(1.f, (pitchDeg - kThrow[i].pitch) / (kThrow[i + 1].pitch - kThrow[i].pitch)));
    *h = kThrow[i].h + (kThrow[i + 1].h - kThrow[i].h) * t;
    *v = kThrow[i].v + (kThrow[i + 1].v - kThrow[i].v) * t;
}
// where a grenade thrown from the eye at (yaw, pitch) goes off, through the map's collision (bounces, the
// roll); false when the throw hits something at once
bool SimulateGrenade(const float* eye, float yaw, float pitchDeg, float* end, int* bounces = nullptr) {
    if (!MapReady()) return false;
    const NavMesh& nav = *gMap->nav;
    float h, vy; ThrowSpeed(pitchDeg, &h, &vy);
    const float fx = std::sin(yaw), fz = std::cos(yaw);
    const float sy = std::sin(yaw - 8.f * kDeg), sz = std::cos(yaw - 8.f * kDeg);
    float p[3] = {eye[0] + sy * 605.f, eye[1] - 287.f + 4.2f * pitchDeg, eye[2] + sz * 605.f};
    if (!nav.Clear(eye, p)) return false;
    float v[3] = {fx * h * 2.f, vy * 2.f, fz * h * 2.f};              // a tick: 2 frames
    bool rolling = false;
    int nb = 0;
    for (int k = 0; k < 54; ++k) {
        if (!rolling && k >= 6) { v[0] *= 0.98f; v[2] *= 0.98f; v[1] = v[1] > 0.f ? v[1] * 0.983f - 24.f : v[1] - 24.f; }
        if (rolling) {
            const float sp = std::sqrt(v[0] * v[0] + v[2] * v[2]);
            const float f = sp > 12.2f ? (sp - 12.2f) / sp : 0.f;
            v[0] *= f; v[2] *= f;
            if (sp <= 12.2f) break;                                   // still: it goes off there
        }
        const float q[3] = {p[0] + v[0], p[1] + v[1], p[2] + v[2]};
        float t, nn[3];
        if (nav.FirstHit(p, q, &t, nn)) {
            for (int i = 0; i < 3; ++i) p[i] += (q[i] - p[i]) * t + nn[i] * 20.f;
            const float vn = v[0] * nn[0] + v[1] * nn[1] + v[2] * nn[2];
            for (int i = 0; i < 3; ++i) { const float n_ = nn[i] * vn; v[i] = (v[i] - n_) * 0.26f - n_ * 0.21f; }
            if (nn[1] < 0.7f) ++nb;                                   // (a wall: the ground's bounces are not counted)
            if (nn[1] > 0.7f && std::fabs(v[1]) < 50.f) { rolling = true; v[1] = 0.f; }
        } else memcpy(p, q, sizeof p);
        if (rolling) {                                                // on the ground (off a ledge: it falls again)
            const float up[3] = {p[0], p[1] + 150.f, p[2]};
            const float g = nav.GroundBelow(up, 500.f);
            if (g < -1e29f) rolling = false; else p[1] = g + 60.f;
        }
    }
    memcpy(end, p, sizeof p);
    if (bounces) *bounces = nb;
    return true;
}
struct ThrowPlan { float yaw = 0, pitch = 0, err = 1e30f, end[3] = {0, 0, 0}; int bounces = 0; };
// the throw that goes off nearest the point (the pitches 2.5 degrees apart, the directions around it)
ThrowPlan SolveThrow(const float* eye, const float* target) {
    // (v141 beta 4) the same throw asked again within 2 frames (VentFrame, then PlanThrowAt): the plan found (363
    // flights of 54 rays at most, 5 ms)
    static ThrowPlan last; static float lastEye[3], lastTarget[3]; static long long lastFrame = -100;
    if (gFrame - lastFrame <= 2 && Dist2D(lastEye, eye) < 100.f && std::fabs(lastEye[1] - eye[1]) < 100.f && Dist2D(lastTarget, target) < 100.f &&
        std::fabs(lastTarget[1] - target[1]) < 100.f) return last;
    ThrowPlan best;
    const float direct = std::atan2(target[0] - eye[0], target[2] - eye[2]);
    static const float kYaw[] = {0.f, 2.f, -2.f, 5.f, -5.f, 10.f, -10.f, 20.f, -20.f, 35.f, -35.f};
    for (float dy : kYaw) {
        if (std::fabs(dy) > 4.f && best.err < 1500.f && best.bounces == 0) break;   // (a direct one good enough: no bounce searched)
        for (float pd = -30.f; pd <= 50.f; pd += 2.5f) {
            float e[3]; int nb = 0;
            const float yaw = direct + dy * kDeg;
            if (!SimulateGrenade(eye, yaw, pd, e, &nb)) continue;
            const float dx = e[0] - target[0], dz = e[2] - target[2], dh = (e[1] - target[1]) * 2.f;
            const float err = std::sqrt(dx * dx + dz * dz + dh * dh);
            // off a wall the real flight drifts (2 to 3 m measured): a direct one is preferred
            const float score = err + 1500.f * nb, bestScore = best.err + 1500.f * best.bounces;
            if (score < bestScore) { best.err = err; best.yaw = yaw; best.pitch = pd; memcpy(best.end, e, sizeof e); best.bounces = nb; }
            if (best.err < 400.f && best.bounces == 0) break;           // ((v141 beta 4) direct and within 0.4 m: none better needed)
        }
        if (best.err < 400.f && best.bounces == 0) break;
    }
    last = best; memcpy(lastEye, eye, sizeof lastEye); memcpy(lastTarget, target, sizeof lastTarget); lastFrame = gFrame;
    return best;
}

// ---- throwing a grenade: in a moment of calm only (no enemy in sight that sees the doll, not hurt
// lately, over half its life), at an enemy known a moment ago (seen, heard) that it cannot shoot (behind
// a cover), 7 to 24 m away, no ally within 7 m of where it would go off; one every 20 s at most. The
// grenade from the weapon window, the view (R1) turned to the throw found, Square held 20 frames and let
// go, the view held 70 frames more (the throw), then the primary again ----
bool Carrying();
long long gSmokeAt = -100000, gRetreatUntil = -1;       // (the smoke: its last throw, away from its enemy until)
int gRetreatFrom = -1;
struct GrenadeRun { int phase = 0; long long at = 0, lastThrow = -100000, lastSolve = -100000, onSince = -1; ThrowPlan plan; int target = -1, code = -1; bool inFight = false; uint32_t blastBefore = 0;
                    bool unseen = false; };          // ((v137) unseen: at an enemy not in sight, ShotAtFrame's or VentFrame's)
GrenadeRun gGr;
bool gGrForce = false;                          // (probe 19: thrown whatever the moment)
bool InDanger() {
    if (gGrForce) return false;
    if (gFrame - gHurtAt < 300 || gMe.life < std::max(60, gMyMaxLife * 3 / 10)) return true;
    for (const auto& k : gKnown) {
        if (!k.second.visible) continue;
        const Player* e = PlayerById(k.first);
        if (!e) continue;
        float eye[3], me[3]; BodyPoint(e->pos, 0.95f, eye); BodyPoint(gMe.pos, 0.6f, me);
        if (LineClear(eye, me)) return true;                    // an enemy that sees it
    }
    return false;
}
int gGrCodeForce = -1;                          // (probe 31: the grenade thrown, whatever its kind)
bool OneLifeGrenadeOk(int id, const float* at);  // (one-life attackers: a grenade at him there)
int GrenadeCode() {                             // the grenade, WP, stun (one left)
    if (gGrCodeForce > 0) return HasWeapon(gGrCodeForce) && Rounds(gGrCodeForce) > 0 ? gGrCodeForce : -1;
    for (int c : {0x13, 0x14, 0x15}) if (HasWeapon(c) && Rounds(c) > 0) return c;
    return -1;
}
void MyThrowEye(float* e) { e[0] = gMe.pos[0]; e[1] = gMe.pos[1] + 822.f; e[2] = gMe.pos[2]; }   // (the first person view's eye, standing)
bool GrenadeFrame() {
    GrenadeRun& G = gGr;
    const long long t = gFrame - G.at;
    auto stop = [&](const char* why, long long retryIn) {
        if (gDebug && why) Log("grenade: %s", why);
        G.phase = 0; G.lastThrow = gFrame - 1200 + retryIn;
    };
    if (G.phase == 0) {
        const int code = GrenadeCode();
        if (code < 0 || HeadshotsOnly() || gFrame - G.lastThrow < 1200 || gFrame - G.lastSolve < 60 || Carrying() || OnLadder() || InDanger() ||
            !WindowFree(0) || (BoxOn() && !CheatWaitsB8())) return false;   // ((v141 beta 10) not from its box: R1 held 4 s there, nothing thrown)
        // an enemy known a moment ago, out of the line of fire, 7 to 24 m away, no ally near it
        // (at the place it was known, seen or heard: never where it went unseen since)
        const Player* best = nullptr; float bd = 1e30f; const float* bestAt = nullptr;
        for (const auto& k : gKnown) {
            if (gFrame - k.second.last > 240) continue;
            const Player* e = PlayerById(k.first);
            if (!e || !Enemy(*e) || !e->alive || (k.second.visible && Asleep(*e)) || SpawnGuarded(*e)) continue;   // (asleep: no threat)
            const float* at = k.second.pos;
            if (!OneLifeGrenadeOk(k.first, at)) continue;      // (one-life attackers: one who saw it, or one in its way)
            const float d = Dist2D(gMe.pos, at);
            if (d < 7000.f || d > 24000.f) continue;
            float mz[3], c[3]; BodyPoint(gMe.pos, 0.6f, mz); BodyPoint(at, 0.45f, c);
            if (k.second.visible && LineClear(mz, c)) continue;     // it can be shot: no grenade
            bool allyNear = false;
            for (const Player& a : gPlayers) if (Ally(a) && a.alive && Dist2D(a.pos, at) < 7000.f) allyNear = true;
            if (allyNear) continue;
            if (d < bd) { bd = d; best = e; bestAt = at; }
        }
        if (!best) return false;
        G.lastSolve = gFrame;
        float eye[3]; MyThrowEye(eye);
        const float feet[3] = {bestAt[0], GroundOf(bestAt) + 60.f, bestAt[2]};
        const ThrowPlan plan = SolveThrow(eye, feet);
        if (plan.err > 2000.f + 700.f * plan.bounces || plan.err > 3000.f || Dist2D(plan.end, gMe.pos) < 6000.f || (MasterVs(best->id) && plan.err > 1500.f)) {   // ((v137) 6: precise only)
            if (gDebug) Log("grenade: no throw reaches %s (%.1f m off at best)", best->name.c_str(), plan.err / 1000.f);
            return false;
        }
        for (const Player& a : gPlayers) if (Ally(a) && a.alive && Dist2D(a.pos, plan.end) < 6000.f) return false;
        G.plan = plan; G.target = best->id; G.phase = 1; G.at = gFrame; G.code = code; G.inFight = false;
        Log("grenade: %s at %s, %.1f m away: yaw %.0f pitch %.1f, goes off %.1f m from it (%d bounces)", code == 0x15 ? "a stun one" : code == 0x14 ? "a WP one" : "one",
            best->name.c_str(), bd / 1000.f, plan.yaw / kDeg, plan.pitch, plan.err / 1000.f, plan.bounces);
        return true;
    }
    const Player* tp = PlayerById(G.target);
    if (G.phase == 5) {                                          // a stun grenade out: turned away from it until it has gone off
        Act("turning_away");
        // (gone off: a NewStunBlast not there when it began, 100 to 170 frames from here; v134's audit, a doll
        // turned back at 130 was blinded 9 s by its own; 260 at most)
        const uint32_t blast = t >= 60 ? ps2world::ActorByName("NewStunBlast") : 0;
        if ((blast && blast != G.blastBefore) || t >= 260) { G.phase = 0; return false; }
        const float away = G.plan.yaw + 3.14159265f, err = Wrap(away - Facing());
        Steer(0, 0, 0);
        if (std::fabs(err) > 25.f * kDeg && gFrame % 6 < 2) Steer(std::sin(away), std::cos(away), kStep);
        return true;
    }
    Act(G.code == 0x17 ? "throwing_smoke" : G.code == 0x16 ? "throwing_chaff" : "throwing_grenade", tp ? tp->name : std::string());
    StopPath(); Steer(0, 0, 0);
    if (G.phase <= 2 && !G.inFight && InDanger()) { stop("in danger: not thrown", 300); return false; }
    switch (G.phase) {
    case 1: {                                                    // the grenade in the hands
        if (Hand() == G.code) { G.phase = 2; G.at = gFrame; G.onSince = -1; return true; }
        // (a radio call, the moment between two windows: waited for; the window blocked by another swap's failure:
        // later; its own failed, or not had: none)
        if (!SwapTo(G.code) && t > 30 && Hand() != G.code && !RadioPressing() && !WindowPause(0)) {
            if (gFrame < gSw[0].doneAt && gSw[0].failed != G.code) { stop("the weapon window given up a moment ago: later", 600); return false; }
            stop("no grenade in the window", 3600); return false;
        }
        if (t > 400) { stop("the window took too long", 600); return false; }
        return true;
    }
    case 2: {                                                    // the view turned to the throw (R1 held)
        gPad.buttons |= 0x0800;
        float m[16];
        if (!ps2world::ViewMatrix(m)) return true;
        const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
        // (v137) the throw from the view's own state (0x80; else the third person's throw, not the one planned: the view came
        // 30 to 42 frames after R1 with the stick pushed, the motion lines), the stick still until it is on, its rates afresh then
        const bool viewOn = (gMe.bits[0] & 0x80) != 0;
        static bool viewWas = false;
        if (t <= 1 || (viewOn && !viewWas)) gAimSince = gFrame;
        viewWas = viewOn;
        const bool on = AimView(camYaw, camPitch, G.plan.yaw, G.plan.pitch * kDeg, 0.f, 0.3f, 0.7f * kDeg, 0.8f * kDeg) && (viewOn || t > 90);   // (the view on the throw's line)
        if (!viewOn && t < 30) { gPad.sticks[2] = gPad.sticks[3] = 0x80; }
        if (on) { if (G.onSince < 0) G.onSince = gFrame; }
        else G.onSince = -1;
        if (G.onSince >= 0 && gFrame - G.onSince >= 4 && t > 20) { G.phase = 3; G.at = gFrame; return true; }
        if (t > 240) { stop("the view did not come round", 600); return false; }
        return true;
    }
    case 3: {                                                    // Square held 20 frames, then let go (the aim kept)
        gPad.buttons |= 0x0800;
        float m[16];
        if (ps2world::ViewMatrix(m))
            AimView(std::atan2(m[8], m[10]), std::asin(std::max(-1.f, std::min(1.f, m[9]))), G.plan.yaw, G.plan.pitch * kDeg, 0.f, 0.3f, 0.7f * kDeg, 0.8f * kDeg);
        if (t < 20) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; return true; }
        G.phase = 4; G.at = gFrame;
        return true;
    }
    default:                                                     // the throw (the grenade flies 55 frames later)
        gPad.buttons |= 0x0800;
        if (t < 75) return true;
        G.lastThrow = gFrame; G.inFight = false;
        if (G.code == 0x17 && gRetreatFrom >= 0) gRetreatUntil = gFrame + 360;   // (its smoke out: away from its enemy)
        // (a stun grenade goes off 175 frames after the release: its thrower's eyes turned away, else its white
        // lasts 24 s through the first person view, 2 s turned away: probe 31)
        if (G.code == 0x15) { G.phase = 5; G.at = gFrame; G.blastBefore = ps2world::ActorByName("NewStunBlast"); return true; }
        G.phase = 0;
        return false;
    }
}
// a throw of a given kind at a point from where the doll stands (the smoke in a fight: G.inFight); false: none reaches it
bool PlanThrowAt(int code, const float* point, int target, bool inFight, const char* what) {
    float eye[3]; MyThrowEye(eye);
    const ThrowPlan plan = SolveThrow(eye, point);
    if (plan.err > 2500.f || Dist2D(plan.end, gMe.pos) < 3000.f) {
        if (gDebug) Log("grenade: no throw for %s (%.1f m off at best)", what, plan.err / 1000.f);
        return false;
    }
    gGr.plan = plan; gGr.target = target; gGr.code = code; gGr.inFight = inFight; gGr.phase = 1; gGr.at = gFrame;
    gGr.unseen = false;                                          // ((v137) set by the callers that throw at one not in sight)
    Log("grenade: %s: yaw %.0f pitch %.1f, goes off %.1f m from the point (%d bounces)", what, plan.yaw / kDeg, plan.pitch, plan.err / 1000.f, plan.bounces);
    return true;
}
// ---- the smoke grenade (v134): hurt in a fight (a quarter to 60 % of its life, hit these 3 s) with its enemy 6 to
// 30 m away, a smoke thrown 5 m toward him (the cloud, 4.5 m across, between them: his aim then far worse,
// or nothing seen: Sees), then away from him 6 s (for food, cover); once every 30 s at most ----
// (its conditions, without the throw: the fight's moves ask them too)
bool SmokeWould(const Player& e, float* p = nullptr) {
    if (Asleep(e)) return false;                                    // (asleep: no smoke from him)
    if (!HasWeapon(0x17) || Rounds(0x17) <= 0 || gGr.phase || Carrying() || OnLadder() || RadioPressing() || gFrame - gSmokeAt < 1800 ||
        !WindowFree(0)) return false;                               // (the weapon window free: no wait under fire)
    const float d = Dist2D(gMe.pos, e.pos);
    // (a quarter to 60 % of its life: lower, the throw's 2 s are too long under fire, v134's audit)
    if (d < 6000.f || d > 30000.f || gMyMaxLife <= 0 || gMe.life >= gMyMaxLife * 0.6f || gMe.life * 4 < gMyMaxLife || gFrame - gHurtAt > 180 ||
        InSmoke(gMe.pos)) return false;
    float q[3];
    const float ux = (e.pos[0] - gMe.pos[0]) / d, uz = (e.pos[2] - gMe.pos[2]) / d;
    const float at[3] = {gMe.pos[0] + ux * 5000.f, gMe.pos[1], gMe.pos[2] + uz * 5000.f};
    if (!MapReady() || !gMap->nav->Nearest(at, 1500.f, q)) return false;
    if (p) memcpy(p, q, sizeof q);
    return true;
}
bool SmokeWanted(const Player& e) {
    float p[3];
    if (!SmokeWould(e, p)) return false;
    p[1] += 60.f;
    gSmokeAt = gFrame;
    if (!PlanThrowAt(0x17, p, e.id, true, ("a smoke between it and " + e.name).c_str())) return false;
    gRetreatFrom = e.id; gRetreatUntil = -1;
    return true;
}

// ---- the suppressors (v133; the weapon window's own code at 0x372658 of n011a): the window open (R2
// held), the cursor on the M1911A1 (6), the Mk22 (5) or the XM16E1 (0xB), Circle puts its suppressor on
// or takes it off (the weapon's state +0x28, 0x10) when its item is had (0x26, 0x27, 0x28: the item's
// state +0x18). Triangle there changes the AK-47's and the SCORPION's fire (+0x28, 1: a bullet a
// press) and the XM16E1's (+0x28 & 3, round 0, 2, 1); not used: the bursts are the trigger's (BurstFrames).
// At each spawn, in a calm moment: each gun of the doll's with its suppressor put on, the weapon to
// fight with left in the hands last (the window's cursor on it when R2 is let go) ----
struct SupRun { long long start = -1, moveAt = -1000, pressAt = -1000; int want = -1, moves = 0, presses = 0; bool done = false; std::vector<int> failed; uint32_t moveBtn = 0x0020; };
SupRun gSup;
int SuppressorItem(int weapon) { return weapon == 6 ? 0x26 : weapon == 5 ? 0x27 : weapon == 0xB ? 0x28 : -1; }
const char* SuppressedGun(int weapon) { return weapon == 6 ? "M1911A1" : weapon == 5 ? "Mk22" : weapon == 0xB ? "XM16E1" : "?"; }
bool SuppressorOn(int weapon) { const uint32_t st = WeaponState(weapon); return st && (R16(st + 0x28) & 0x10) != 0; }
bool SuppressorHad(int weapon) { const int i = SuppressorItem(weapon); const uint32_t st = i > 0 ? ItemState(i) : 0; return st && R16s(st + 0x18) > 0; }
int SuppressorNext() {
    const int rest = RestWeapon();
    int last = -1;
    for (int c : {6, 5, 0xB}) {
        if (!HasWeapon(c) || !SuppressorHad(c) || SuppressorOn(c)) continue;
        if (std::find(gSup.failed.begin(), gSup.failed.end(), c) != gSup.failed.end()) continue;
        if (c == rest) { last = c; continue; }                 // (the weapon to fight with: last)
        return c;
    }
    return last;
}
bool SuppressorFrame() {
    SupRun& S = gSup;
    if (S.done || !Inv().ok || RadioPressing()) return false;
    const uint32_t wl = ps2world::ActorByName("NewWeaponChangeLinear");
    if (S.start < 0) {                                           // (not begun: a calm moment, a gun to do)
        // ((v141 beta 3) NIGHTMARE: none, its window's 2 to 3 s at each spawn, and its Circle played as a CQC strike when an
        // enemy came meanwhile (ftrace bench: 40 frames without turning to him, then shot))
        if (NmV3()) { S.done = true; return false; }
        for (const auto& k : gKnown) if (k.second.visible) return false;
        if (gFrame - gHurtAt < 300 || OnLadder() || Carrying() || BoxOn()) return false;
        // (v137) not with a CQC weapon in the hands (a sniper's handgun): the window's Circle was also played as a CQC
        // strike (the word 1's 0x100, 8 of 9 such Circles of the v137 bench; with a rifle in the hands 0 of 44); nor in
        // a scope's view (the window slow to open there)
        if (CqcWeapon(Hand()) || Scoped() || LocksView(Hand())) return false;   // ((v141 beta 6) nor a view-locking weapon just taken: its stay's own)
        S.want = SuppressorNext();
        if (S.want < 0) { S.done = true; return false; }
        S.start = gFrame; S.moves = S.presses = 0;
        if (gDebug) Log("suppressor: to the %s", SuppressedGun(S.want));
    }
    Act("fitting_suppressor");
    StopPath(); Steer(0, 0, 0);
    if (!wl || gFrame - S.start > 600 || S.moves > 12 || S.presses > 4) {
        Log("suppressor: not put on the %s (%s)", SuppressedGun(S.want), !wl ? "no window" : S.presses > 4 ? "Circle changed nothing" : "not in the window");
        S.failed.push_back(S.want); S.start = -1;
        S.done = SuppressorNext() < 0;
        return !S.done;                                            // (R2 let go here: the window's weapon taken)
    }
    gPad.buttons |= 0x0200;                                       // R2 held: the weapon window
    if (gFrame - S.moveAt < 4) { gPad.buttons |= S.moveBtn; return true; }   // right (left), 4 frames
    if (gFrame - S.pressAt < 4) { gPad.buttons |= 0x2000; return true; }  // Circle, 4 frames
    const int open = R16(wl + 0x44), still = R16(wl + 0x48), cur = R16(wl + 0x54);
    if (open != 0xC || still != 2 || gFrame - S.moveAt < 10 || gFrame - S.pressAt < 24) return true;
    // (v137) a move: right only. Left in the window also turned the player 90 degrees, the stick let go (32 of the 45
    // lefts at the v137 bench's spawns, never a right): a twitch at each spawn; a gun left of the cursor takes more moves
    auto move = [&](int) { S.moveBtn = 0x0020; S.moveAt = gFrame; ++S.moves; gPad.buttons |= S.moveBtn; };
    if (SuppressorOn(S.want)) {                                   // on: the next one, else the weapon to fight with
        if (S.presses) { Log("suppressor: on the %s", SuppressedGun(S.want)); S.presses = 0; }
        const int next = SuppressorNext();
        if (next >= 0) { S.want = next; S.moves = S.presses = 0; S.start = gFrame; return true; }
        const int rest = RestWeapon();
        if (rest >= 0 && cur != rest && HasWeapon(rest) && S.moves <= 12) { move(rest); return true; }
        gPad.buttons &= ~0x0200u;                                 // let go: the cursor's weapon in the hands
        gHand = cur; gHandSetAt = gFrame;
        S.done = true; S.start = -1;
        return false;
    }
    if (cur != S.want) { move(S.want); return true; }
    S.pressAt = gFrame; ++S.presses; gPad.buttons |= 0x2000;
    return true;
}

// ---- the chaff grenade (0x16): Snake's in the first seconds of a life (the radar jammed: where he is
// hidden); a soldier's once a life when enemies were known within 40 m these 10 s and none is in sight (their
// radio and radar jammed 24 s, its own side's too: every machine, v134); a few metres ahead (the view 30
// degrees down) ----
struct ChaffRun { int phase = 0; long long at = 0, lifeAt = -1; bool done = false; };
ChaffRun gCh;
bool ChaffFrame() {
    ChaffRun& C = gCh;
    if (C.done) return false;
    const long long t = gFrame - C.at;
    if (C.phase == 0) {
        if (!HasWeapon(0x16) || Rounds(0x16) <= 0) { C.done = true; return false; }
        if (!WindowFree(0)) return false;                        // (the weapon window free first)
        for (const auto& k : gKnown) if (k.second.visible) return false;
        if (!IAmSnake()) {
            if (Jammed() || InDanger() || Carrying() || OnLadder() || gGr.phase) return false;
            bool near = false;
            for (const auto& k : gKnown) if (gFrame - k.second.last < 600 && Dist2D(k.second.pos, gMe.pos) < 40000.f) near = true;
            if (!near) return false;
        }
        C.phase = 1; C.at = gFrame;
        if (gDebug) Log("chaff: to the window (%s)", IAmSnake() ? "Snake" : "enemies close");
        return true;
    }
    Act("throwing_chaff");
    StopPath(); Steer(0, 0, 0);
    switch (C.phase) {
    case 1: {
        if (Hand() == 0x16) { C.phase = 2; C.at = gFrame; return true; }
        if (!SwapTo(0x16) && t > 30 && Hand() != 0x16 && !RadioPressing() && !WindowPause(0)) {   // (as the grenade's)
            if (gFrame < gSw[0].doneAt && gSw[0].failed != 0x16) { if (gDebug) Log("chaff: the weapon window given up a moment ago: later"); C.phase = 0; return false; }
            Log("chaff: none in the window"); C.done = true; C.phase = 0; return false;
        }
        if (t > 400) { C.done = true; C.phase = 0; return false; }
        return true;
    }
    case 2: {                                                    // the view 30 degrees down (R1), Square 20 frames
        gPad.buttons |= 0x0800;
        float m[16];
        if (ps2world::ViewMatrix(m) && t < 60) { const float pe = -30.f * kDeg - std::asin(std::max(-1.f, std::min(1.f, m[9]))); StickFpv(0.f, pe); return true; }
        if (t < 80) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; return true; }
        C.phase = 3; C.at = gFrame;
        Log(IAmSnake() ? "chaff: Snake's chaff grenade" : "chaff: a chaff grenade (enemies close)");   // (thrown: Square let go)
        return true;
    }
    default:
        gPad.buttons |= 0x0800;
        if (t < 70) return true;
        C.phase = 0; C.done = true;
        return false;
    }
}

// ---- Snake's uniforms (probe 22; the item window's own code at 0x370BB8 of n011a): the item window open
// (L2 held), its cursor on ITEM_CAMOUFLAGE (0x13), Circle changes the uniform (its number at +0xB63 of the
// player's work: 0, 1, 2 round; Triangle the second one). The camouflage index (+0xB6E, tenths of a
// percent: standing 0 to 15 %, crouched 30 %, the stealth camouflage 100 %) says how well he blends in
// there. Once a map the three are measured crouched still, the best one kept; at each life he puts it on
// (the game gives him the first one) ----
struct CamoWin { long long start = -1, moveAt = -1000, pressAt = -1000; int moves = 0, presses = 0; };
CamoWin gCw;
// the item window held open on ITEM_CAMOUFLAGE, Circle pressed until the uniform is the one wanted: true
// while it works (let go: the window takes ITEM_CAMOUFLAGE in the hands)
bool CamoWindow(int want) {
    const uint32_t wl = ps2world::ActorByName("NewItemChangeLinear");
    const int now = gMe.obj ? R8(gMe.obj + 0xB63) : -1;
    if (gCw.start < 0) { gCw = CamoWin(); gCw.start = gFrame; }
    if (!wl || now < 0 || gFrame - gCw.start > 450 || gCw.moves > 10 || gCw.presses > 6) {
        Log("camouflage: uniform %d not put on (%s)", want, wl ? "the window" : "no window");
        gCw = CamoWin(); return false;
    }
    gPad.buttons |= 0x0100;                                               // L2 held: the item window
    if (gFrame - gCw.moveAt < 4) { gPad.buttons |= 0x0020; return true; } // right, 4 frames
    if (gFrame - gCw.pressAt < 4) { gPad.buttons |= 0x2000; return true; }// Circle, 4 frames
    const int open = R16(wl + 0x44), still = R16(wl + 0x48), cur = R16(wl + 0x54);
    if (open != 0xC || still != 2 || gFrame - gCw.moveAt < 10 || gFrame - gCw.pressAt < 24) return true;
    if (cur != 0x13) { gCw.moveAt = gFrame; ++gCw.moves; gPad.buttons |= 0x0020; return true; }
    if (now == want) { gPad.buttons &= ~0x0100u; gCw = CamoWin(); gItemHeld = 0x13; return false; }   // let go: done
    gCw.pressAt = gFrame; ++gCw.presses; gPad.buttons |= 0x2000;
    return true;
}
struct CamoRun { int phase = 0, want = -1; long long at = 0; bool done = false; int measured[3] = {-99999, -99999, -99999}; };
CamoRun gCa;
// by map (its page): the best uniform once one is clearly better (30 or more: 3 %), the places measured
// (the three uniforms often blend in the same on a floor: tried again 15 m away, three times at most)
struct CamoMap { int best = -1; std::vector<std::array<float, 3>> spots; };
std::map<std::string, CamoMap> gCamoMaps;
bool CamoFrame() {
    CamoRun& C = gCa;
    if (!IAmSnake() || C.done || !gMe.obj || !MapReady()) return false;
    if (!HasItem(0x13)) { C.done = true; return false; }
    CamoMap& M = gCamoMaps[gMap->key];
    const int now = R8(gMe.obj + 0xB63);
    const long long t = gFrame - C.at;
    if (C.phase == 0) {
        for (const auto& k : gKnown) if (k.second.visible) return false;          // (nobody in sight)
        if (M.best < 0 && Inv().ok && Inv().item == 0x17) return false;           // (the stealth camouflage: 100 % whatever the uniform)
        if (M.best >= 0) {
            if (now == M.best) { C.done = true; return false; }
            C.want = M.best; C.phase = 3;
            Log("camouflage: uniform %d (the best one here)", C.want);
        } else {
            if (M.spots.size() >= 3) { C.done = true; return false; }
            for (const auto& q : M.spots) if (Dist2D(gMe.pos, q.data()) < 15000.f) return false;   // (elsewhere)
            for (int& v : C.measured) v = -99999;
            C.phase = 1;
            Log("camouflage: the three uniforms measured here (%zu)", M.spots.size() + 1);
        }
        C.at = gFrame;
        return true;
    }
    Act("changing_camouflage");
    StopPath();
    if (C.phase == 1) {                                  // crouched still 2 s: the index
        Steer(0, 0, 0);
        if (!SetLow(true) && t < 60) return true;
        if (t < 60) return true;
        if (now >= 0 && now < 3) C.measured[now] = R16s(gMe.obj + 0xB6E);
        if (gDebug) Log("camouflage: uniform %d, index %d", now, now >= 0 && now < 3 ? C.measured[now] : -1);
        for (int k = 0; k < 3; ++k) if (C.measured[k] == -99999) { C.want = k; C.phase = 2; C.at = gFrame; return true; }
        int best = 0, worst = 0;
        for (int k = 1; k < 3; ++k) { if (C.measured[k] > C.measured[best]) best = k; if (C.measured[k] < C.measured[worst]) worst = k; }
        M.spots.push_back({gMe.pos[0], gMe.pos[1], gMe.pos[2]});          // (a place measured: an interrupted one is not)
        if (C.measured[best] - C.measured[worst] >= 30) {
            M.best = best;
            Log("camouflage: uniform %d kept here (index %d, %d, %d)", best, C.measured[0], C.measured[1], C.measured[2]);
        } else Log("camouflage: the same here (index %d, %d, %d): measured again elsewhere", C.measured[0], C.measured[1], C.measured[2]);
        C.want = best; C.phase = 3; C.at = gFrame;
        return true;
    }
    Steer(0, 0, 0);
    if (CamoWindow(C.want)) return true;                 // phase 2 (the next one), 3 (the best one)
    const bool on = R8(gMe.obj + 0xB63) == C.want;
    if (C.phase == 3 || !on) {
        SetLow(false);
        if (on && gDebug) Log("camouflage: uniform %d on", C.want);
        C.phase = 0;
        if (M.best >= 0 || !on || M.spots.size() >= 3) C.done = true;   // (undecided: again 15 m away)
        return false;
    }
    C.phase = 1; C.at = gFrame;                          // the next one measured
    return true;
}

// ---- the stealth camouflage (the item 0x17: Snake's, a special character's): on, the camouflage index is
// 100 % and its battery (+0xB6C of the player's work, 300 full) goes down about 5 a second; off, it comes
// back slowly. On when an enemy is near and the doll is on its way (the objective, a hunt, a rush), off
// when its battery is low; never hiding (the box) ----
int Battery() { return gMe.obj ? R16s(gMe.obj + 0xB6C) : 0; }
int ItemInHands() { const Inventory& v = Inv(); return v.ok ? v.item : gItemHeld; }
bool EnemyNear(float r) {                        // (the enemies known: seen or heard in the last 10 s)
    for (const auto& k : gKnown) if (gFrame - k.second.last < 600 && Dist2D(gMe.pos, k.second.pos) < r) return true;
    return false;
}
long long gStealthAt = -100000, gStealthOnAt = -100000;
bool StealthFrame(bool hiding) {
    if (!HasItem(0x17) || gFrame - gStealthAt < 120) return false;
    const bool on = ItemInHands() == 0x17;
    if (on && gStealthOnAt < gStealthAt) gStealthOnAt = gFrame;
    bool want = !hiding && Battery() > (on ? 15 : 90) && EnemyNear(25000.f);
    if (on && !hiding && Battery() > 15 && gFrame - gStealthOnAt < 450) want = true;   // (on 15 s at least)
    if (want == on) return false;
    if (ItemTo(want ? 0x17 : 0)) { Act(want ? "stealth_on" : "stealth_off"); return true; }
    gStealthAt = gFrame;
    if (gDebug) Log("stealth: %s (battery %d)", want ? "on" : "off", Battery());
    return false;
}

// ---- the thermal goggles (the item 7, v134: a weapon box may give them; probe 31: the item window as the box,
// the view NewThermalMode; the battery of the stealth camouflage, +0xB6C of the player's work, 300 full, 5 a
// second on, 60 s): on when an enemy was known within 30 m these 10 s or a smoke is out, the battery over
// 90; off under 20, or 15 s with nothing to see; its shots finer then (FightFrame), a smoke no screen to
// it (Sees); never in place of the box when hiding nor of the stealth camouflage (one item at a time) ----
long long gGogglesAt = -100000, gGogglesOnAt = -100000, gGogglesUseAt = -100000;
bool gGogglesWere = false;                           // (on at the last look: the change said once)
bool GogglesFrame(bool hiding) {
    const bool on = GogglesOn(), worn = ItemInHands() == 7;   // (worn with the battery out: nothing seen, taken off)
    if (on != gGogglesWere) {
        gGogglesWere = on;
        if (on) gGogglesOnAt = gFrame;
        Log("goggles: the thermal goggles %s (battery %d)", on ? "on" : "off", Battery());
    }
    if (!HasItem(7) || gFrame - gGogglesAt < 120) return false;
    if (!worn && (hiding || ItemInHands() == 0x17 || ItemInHands() == 0xE)) return false;
    for (const auto& k : gKnown) if (k.second.visible) return false;          // (not in a fight's moment)
    SeeSmokes();
    if (EnemyNear(30000.f) || !gSmokes.empty()) gGogglesUseAt = gFrame;
    const bool want = !hiding && Battery() > (worn ? 20 : 90) && (gFrame - gGogglesUseAt < 900 || (worn && gFrame - gGogglesOnAt < 600));
    if (want == worn) return false;
    if (ItemTo(want ? 7 : 0)) { Act(want ? "goggles_on" : "goggles_off"); return true; }
    gGogglesAt = gFrame;                                 // (the window would not do it: again in 2 s)
    if (gDebug && gFrame - gSw[1].doneAt >= 90) Log("goggles: not %s (the item window)", want ? "put on" : "taken off");   // (just done: the game's word lags)
    return false;
}

// ---- Snake seen coming (an enemy 5 to 15 m away looking his way with a clear line, the stealth
// camouflage off): he crouches still where he is (his camouflage index up) until it looks elsewhere or
// goes, 6 s at most (then not again for 10 s), and goes on; closer, he is cornered (the fight, CQC first) ----
long long gFreezeSince = -1, gFreezeOffUntil = -1;
bool FreezeFrame() {
    if (!IAmSnake() || ItemInHands() == 0x17 || gFrame < gFreezeOffUntil) { gFreezeSince = -1; return false; }
    bool watched = false;
    for (const Player& e : gPlayers) {
        if (!Enemy(e) || !e.alive || Asleep(e)) continue;
        const float d = Dist2D(gMe.pos, e.pos);
        if (d > 15000.f || d < 5000.f) continue;
        if (std::fabs(Wrap(AngleTo(e.pos, gMe.pos) - e.facing)) > 40.f * kDeg) continue;
        if (std::fabs(Wrap(AngleTo(gMe.pos, e.pos) - Facing())) > Sk().fovDeg * kDeg && !Audible(e)) continue;   // (Snake sees him, or hears him)
        float eye[3], me[3]; BodyPoint(e.pos, 0.95f, eye); BodyPoint(gMe.pos, 0.5f, me);
        if (LineClear(eye, me)) { watched = true; break; }
    }
    if (!watched) { if (gFreezeSince >= 0) SetLow(false); gFreezeSince = -1; return false; }
    if (gFreezeSince < 0) { gFreezeSince = gFrame; if (gDebug) Log("freeze: seen coming, crouched still"); }
    if (gFrame - gFreezeSince > 180) { gFreezeOffUntil = gFrame + 300; gFreezeSince = -1; SetLow(false); return false; }
    Act("hiding"); StopPath(); Steer(0, 0, 0); SetLow(true);
    return true;
}

// ---- traps: the claymore (0x1B) and the book (0x1C) of the support row (probe 18): put down 0.7 m ahead
// of the player, facing where the player faces, with Square held half a second (a tap only crouched).
// Where: a narrow place (walls on both sides, under 2.2 m across) on the ways from the enemies' spawns
// toward the doll's side, 10 m from their spawns at least, facing their way in. When: in a moment of calm
// (as the grenades), two a life at most, 30 s apart ----
struct TrapSpot { float p[3]; float facing; float width; };
struct TrapRun { int phase = 0; long long at = 0, lastPut = -100000, lifeSince = -1, lowAt = -1; int put = 0, before = 0, roundsBefore = 0, code = -1; TrapSpot spot; std::vector<TrapSpot> spots; bool spotsMade = false; int spotsTeam = -1; };
TrapRun gTr;
int TrapCode() { for (int c : {0x1B, 0x1C}) if (HasWeapon(c) && Rounds(c) > 0) return c; return -1; }   // a claymore, a book left
void CountTraps(uint32_t, const std::string& name, void* ctx) { if (name == "NewOnlineClaymore" || name == "NewYMBookOnline") ++*(int*)ctx; }
int TrapsOnMap() { int n = 0; ps2world::ForEachActor(&CountTraps, &n); return n; }
// the free width across a way at p going along (dx, dz): the walls on each side (knee high), 2.5 m at most each
float Across(const float* p, float dx, float dz, float* left, float* right) {
    const float nx = -dz, nz = dx;
    const float o[3] = {p[0], p[1] + 500.f, p[2]};
    auto side = [&](float sgn) {
        for (float r = 250.f; r <= 2500.f; r += 250.f) { const float q[3] = {o[0] + nx * r * sgn, o[1], o[2] + nz * r * sgn}; if (!LineClear(o, q)) return r; }
        return 2500.f;
    };
    *left = side(1.f); *right = side(-1.f);
    return *left + *right;
}
void MakeTrapSpots() {
    gTr.spots.clear(); gTr.spotsMade = true; gTr.spotsTeam = gMyTeam;
    if (!MapReady() || !gMap->geom || gRule == 0 || (gMyTeam != 0 && gMyTeam != 1)) return;
    // the spawns of each side for the rule
    SpawnKind mine[2], theirs[2];
    const bool red = gMyTeam == 0;
    if (gRule == 3) { mine[0] = mine[1] = red ? SP_TCM_RED : SP_TCM_BLUE; theirs[0] = theirs[1] = red ? SP_TCM_BLUE : SP_TCM_RED; }
    else { mine[0] = red ? SP_TDM_RED : SP_TDM_BLUE; mine[1] = red ? SP_TDM_FIRST_RED : SP_TDM_FIRST_BLUE; theirs[0] = red ? SP_TDM_BLUE : SP_TDM_RED; theirs[1] = red ? SP_TDM_FIRST_BLUE : SP_TDM_FIRST_RED; }
    std::vector<const Spawn*> from, to;
    for (const Spawn& sp : gMap->geom->spawns) {
        if (sp.kind == theirs[0] || sp.kind == theirs[1]) from.push_back(&sp);
        if (sp.kind == mine[0] || sp.kind == mine[1]) to.push_back(&sp);
    }
    if (from.empty() || to.empty()) { if (gDebug) Log("trap: no spawns of the rule's sides"); return; }
    // the enemies' ways toward the doll's side, each resampled every 0.5 m (its point, how far along)
    struct Pt { float p[3]; float s; };
    std::vector<std::vector<Pt>> ways;
    for (size_t i = 0; i < from.size() && i < 4; ++i)
        for (size_t j = 0; j < to.size() && j < 3; ++j) {
            const Spawn* a = from[(i * 7) % from.size()]; const Spawn* b = to[(j * 5) % to.size()];
            float pa[3], pb[3];
            if (!gMap->nav->Nearest(a->p, 800.f, pa) || !gMap->nav->Nearest(b->p, 800.f, pb)) continue;
            std::vector<Waypoint> w; float len = 0;
            if (!gMap->nav->Path(pa, pb, w, &len) || w.size() < 2) continue;
            std::vector<Pt> r; float walked = 0;
            for (size_t k = 1; k < w.size(); ++k) {
                const float* s0 = w[k - 1].p; const float* s1 = w[k].p;
                const float sl = Dist2D(s0, s1);
                if (w[k - 1].link) { r.clear(); walked += sl; continue; }     // (a ladder, a ledge: the way after it)
                for (float u = 0; u < sl; u += 500.f) {
                    Pt q; q.s = walked + u;
                    for (int c = 0; c < 3; ++c) q.p[c] = s0[c] + (s1[c] - s0[c]) * (sl > 1.f ? u / sl : 0.f);
                    r.push_back(q);
                }
                walked += sl;
            }
            if (r.size() > 8) { for (Pt& q : r) q.s = len - q.s; ways.push_back(r); }   // (s: how far from the doll's side)
        }
    // the places: narrow ones (walls on both sides, 3 m across at most) and the corners (the way turning 40
    // degrees or more within 3 m, a wall within 2.5 m), 10 m from their spawns and 6 m from ours at least;
    // placed facing back toward the enemies' way in; the more of their ways pass there, the better
    std::vector<TrapSpot> cand;
    for (const auto& r : ways) {
        const float len = r.front().s;
        for (size_t i = 3; i + 3 < r.size(); ++i) {
            const Pt& q = r[i];
            if (len - q.s < 10000.f || q.s < 6000.f) continue;
            const float ix = q.p[0] - r[i - 3].p[0], iz = q.p[2] - r[i - 3].p[2], ox = r[i + 3].p[0] - q.p[0], oz = r[i + 3].p[2] - q.p[2];
            const float il = std::hypot(ix, iz), ol = std::hypot(ox, oz);
            if (il < 1.f || ol < 1.f) continue;
            const float dx = ox / ol, dz = oz / ol;
            float l, rr;
            const float width = Across(q.p, dx, dz, &l, &rr);
            const float turn = std::acos(std::max(-1.f, std::min(1.f, (ix * ox + iz * oz) / (il * ol))));
            TrapSpot t;
            if (width <= 3000.f && l <= 2000.f && rr <= 2000.f && i % 2 == 0) {
                memcpy(t.p, q.p, sizeof t.p); t.facing = std::atan2(-dx, -dz); t.width = width;
                cand.push_back(t);
            } else if (turn > 40.f * kDeg && std::min(l, rr) < 2500.f) {
                const Pt& after = r[i + 2];                                    // 1 m past the corner
                memcpy(t.p, after.p, sizeof t.p); t.facing = std::atan2(-dx, -dz); t.width = 2500.f + (180.f - turn / kDeg) * 10.f;
                cand.push_back(t);
            }
        }
    }
    for (TrapSpot& c : cand) {                                                // the ways that pass there
        int n = 0;
        for (const auto& r : ways) for (const Pt& q : r) if (Dist2D(q.p, c.p) < 1500.f) { ++n; break; }
        c.width -= 400.f * (n - 1);
    }
    // the best, 4 m apart at least
    std::sort(cand.begin(), cand.end(), [](const TrapSpot& x, const TrapSpot& y) { return x.width < y.width; });
    for (const TrapSpot& c : cand) {
        bool far = true;
        for (const TrapSpot& k : gTr.spots) if (Dist2D(k.p, c.p) < 4000.f) far = false;
        if (far) gTr.spots.push_back(c);
        if (gTr.spots.size() >= 6) break;
    }
    if (gDebug) {
        Log("trap: %zu ways from their spawns to ours, %zu places, %zu kept", ways.size(), cand.size(), gTr.spots.size());
        for (const TrapSpot& k : gTr.spots) Log("trap spot %.0f %.0f %.0f facing %.0f (score %.1f)", k.p[0], k.p[1], k.p[2], k.facing / kDeg, k.width / 1000.f);
    }
}
bool TrapFrame() {
    TrapRun& T = gTr;
    const long long t = gFrame - T.at;
    auto stop = [&](const char* why) { if (gDebug && why) Log("trap: %s", why); T.phase = 0; T.lastPut = gFrame; };
    if (T.phase == 0) {
        const int code = TrapCode();
        if (code < 0 || (HeadshotsOnly() && code == 0x1B) || T.put >= 2 || gFrame - T.lastPut < 1800 || Carrying() || OnLadder()) return false;   // (headshots only: no claymore)
        static long long saidAt = -100000;
        const char* why = InDanger() ? "in danger" : nullptr;
        for (const auto& k : gKnown) if (k.second.visible) why = "an enemy in sight";
        if (why) { if (gDebug && gFrame - saidAt > 900) { saidAt = gFrame; Log("trap: waiting (%s)", why); } return false; }
        if (!T.spotsMade || T.spotsTeam != gMyTeam) MakeTrapSpots();
        if (T.spots.empty()) return false;
        // the nearest spot, not within 5 m of a trap already there
        const TrapSpot* best = nullptr; float bd = 1e30f;
        for (const TrapSpot& k : T.spots) {
            const float d = Dist2D(gMe.pos, k.p);
            if (d < bd && d < 40000.f) { bd = d; best = &k; }
        }
        if (!best) return false;
        T.spot = *best; T.phase = 1; T.at = gFrame;
        T.spots.erase(T.spots.begin() + (best - &T.spots[0]));                 // (used: not again)
        Log("trap: a %s at %.0f %.0f %.0f facing %.0f (%.1f m across), %.0f m away", code == 0x1B ? "claymore" : "book", T.spot.p[0], T.spot.p[1], T.spot.p[2],
            T.spot.facing / kDeg, T.spot.width / 1000.f, bd / 1000.f);
        return true;
    }
    Act("setting_trap");
    if (T.phase <= 3 && InDanger()) { stop("in danger: not put"); return false; }
    switch (T.phase) {
    case 1:                                                      // there
        if (GoTo(T.spot.p, "trap", 350.f)) { T.phase = 2; T.at = gFrame; Steer(0, 0, 0); return true; }
        if (t > 3000) { stop("the place not reached"); return false; }
        return true;
    case 2: {                                                    // facing the enemies' way, the trap in the hands
        StopPath(); Steer(0, 0, 0);
        const float err = Wrap(T.spot.facing - Facing());
        if (std::fabs(err) > 10.f * kDeg && t < 120) { if (gFrame % 6 < 2) Steer(std::sin(T.spot.facing), std::cos(T.spot.facing), kStep); return true; }
        if (Hand() != TrapCode()) { if (!SwapTo(TrapCode()) && t > 200) { stop("no trap left in the window"); T.put = 2; return false; } return true; }
        T.phase = 3; T.at = gFrame; T.before = TrapsOnMap(); T.code = TrapCode(); T.roundsBefore = Rounds(T.code); T.lowAt = -1;
        return true;
    }
    case 3:                                                      // crouched, then Square held 30 frames (one trap)
        Steer(0, 0, 0);
        if (!SetLow(true) && t < 60) return true;
        if (T.lowAt < 0) T.lowAt = gFrame;
        if (gFrame - T.lowAt < 30) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; return true; }
        T.phase = 4; T.at = gFrame;
        return true;
    default: {                                                   // put down (it comes a moment after Square is let
        Steer(0, 0, 0);                                          // go: one more on the map, one less in the hands)?
        const bool ok = TrapsOnMap() > T.before || (T.roundsBefore > 0 && Rounds(T.code) < T.roundsBefore);
        if (!ok && t < 150) return true;
        Log("trap: %s (%d left)", ok ? "put down" : "NOT put down", std::max(0, Rounds(T.code)));
        if (ok) ++T.put;
        SetLow(false);
        T.phase = 0; T.lastPut = gFrame;
        return false;
    }
    }
}

// ---- the emote (v134, probe 31): Triangle held, standing still in the open: the player's state word 0 is 2
// with the word 6's 0xA0000000 (the word 10's 0x1100000) from 12 frames after the press until 48 frames
// after the release, 88 frames at least (the game's default emote: the salute); a tap does nothing. The
// other players' the same (their word 10 with 0x80000000 more). The doll makes one sometimes after a kill
// (its victim's kill camera shows it) when nothing threatens it, answers a player's emote made at it (an
// ally's, or an enemy's not aiming at it before any fight between them), and now and then greets a real
// ally close by; never where Triangle does something else (a ladder, an obstacle, a box close by) ----
bool EmoteOn(const uint32_t* b) { return (b[0] & 0xFFFF) == 2 && (b[6] & 0xF0000000) == 0xA0000000; }
long long gKillAt = -100000;
int gKillVictim = -1;
bool EnemySide(const Player& p) { Player q = p; q.alive = true; return Enemy(q); }   // (dead or alive: Enemy is for the living)
// each player's death and spawn (dead, then alive: where, SpawnGuarded), every frame (the doll dead, in a menu
// or knocked out too: a spawn missed would be taken for a later one)
void LivesWatch() {
    for (const Player& p : gPlayers) {
        if (p.local) continue;
        const auto it = gWasAlive.find(p.id);
        if (it != gWasAlive.end() && it->second != p.alive) {
            if (!p.alive && EnemySide(p)) gDiedAt[p.id] = gFrame;
            if (p.alive) { gSpawnAt[p.id] = gFrame; gSpawnPos[p.id] = {p.pos[0], p.pos[1], p.pos[2]}; }
        }
        gWasAlive[p.id] = p.alive;
    }
}
// a kill of its own seen (the kills counter, or the kill message on the host): once a victim (both may come)
long long gKillSeenAt = -100000; int gKillSeenVictim = -1;
void KillSeen(int victim) {
    if (victim == gKillSeenVictim && gFrame - gKillSeenAt < 300) return;
    gKillSeenAt = gFrame; gKillSeenVictim = victim;
    gKillAt = gFrame; gKillVictim = victim;
}
// its kills (v134's audit: the game's kill messages, 0x751005, reach the host's HUD only): its kills counter
// (its variable set +0x14) up, its victim the enemy it fought last who died these 3 s, else the last one seen
// dying
int gKwFoeLast = -1; long long gKwFoeAt = -100000, gKwPendingAt = -1;
void KillWatch() {
    if (gFi.target >= 0) { gKwFoeLast = gFi.target; gKwFoeAt = gFrame; }
    const int k = MyScore(0x14);
    if (k >= 0) {
        if (gKillsSeen >= 0 && k > gKillsSeen) gKwPendingAt = gFrame;       // (a kill: its victim looked for, 1.5 s at most)
        gKillsSeen = k;                                                      // (the first look, a new round's score: nothing)
    }
    if (gKwPendingAt < 0) return;
    if (gFrame - gKwPendingAt > 90) { if (gDebug) Log("kill: its kills %d, its victim not seen dying", gKillsSeen); gKwPendingAt = -1; return; }
    int victim = -1; long long last = -1;
    for (const auto& d : gDiedAt) {
        if (d.second < gKwPendingAt - 180) continue;
        if (d.first == gKwFoeLast && gFrame - gKwFoeAt < 300) { victim = d.first; break; }
        if (d.second > last) { last = d.second; victim = d.first; }
    }
    if (victim < 0) return;
    gKwPendingAt = -1;
    KillSeen(victim);
    if (gDebug) { const Player* v = PlayerById(victim); Log("kill: %s (%lld frames after his death)", v ? v->name.c_str() : "?", gFrame - gDiedAt[victim]); }
}
struct EmoteRun { int phase = 0, toward = -1, hold = 0, offFrames = 0; long long at = 0, pressAt = -1, offUntil = -1; bool seen = false; std::string why; };
EmoteRun gEm;
struct EmoteAsk { long long at = -1; int toward = -1; std::string why; };
EmoteAsk gEmAsk;                                  // an emote wanted: its moment, whom toward, why
std::map<int, bool> gEmoting;                     // each player: in an emote the frame before
std::map<int, long long> gAnsweredAt;             // each player: its last emote answered
long long gGreetAt = -100000;
int gEmotes = 0;
bool Stunned();
void StageBoxNear(uint32_t w, const std::string& name, void* ctx) {
    if (name != "NewOnlineStageCBox") return;
    const float p[3] = {RF(w + 0x2D0), RF(w + 0x2D4), RF(w + 0x2D8)};
    if (Dist2D(p, gMe.pos) < 2500.f && std::fabs(p[1] + 800.f - gMe.pos[1]) < 2000.f) *(bool*)ctx = true;
}
// what keeps it from an emote now (nullptr: nothing): toward, the player it is for (an enemy answered)
const char* EmoteBlock(int toward) {
    if (gFi.target >= 0 && gFi.target != toward) return "a fight";
    if (CheatOn() && CheatEnemyDist() < 25000.f) return "an enemy close";   // ((v141 beta 7) tier 10's cheat: none near its enemies)
    for (const auto& k : gKnown) {
        if (k.first == toward) continue;
        if (k.second.visible) return "an enemy in sight";
        if (gFrame - k.second.last < 180 && Dist2D(k.second.pos, gMe.pos) < 15000.f) return "an enemy close";
    }
    if (gFrame - gHurtAt < 300 || (gMyMaxLife > 0 && gMe.life < gMyMaxLife / 2)) return "hurt";
    if (Carrying()) return "carrying the objective";
    if (OnLadder()) return "on a ladder";
    if (Low()) return "low";
    if (Scoped() || (gMe.bits[0] & 0x80)) return "in a first person view";
    if (BoxOn()) return "in the box";
    if (RadioBusy()) return "a radio call";
    if (Blind() || Stunned()) return "blinded or knocked out";
    if (Rolling() || gRo.phase) return "a roll";
    if (AgainstWall()) return "against a wall";
    if (gGr.phase || gTr.phase || gCq.phase || gCh.phase || gCa.phase) return "a throw, a trap, a CQC try";
    for (int w = 0; w < 2; ++w) if (gSw[w].want >= 0 && gSw[w].start >= 0) return "a window";
    float feet[3];
    if (!MapReady() || !Ground(feet) || gMap->nav->LadderAt(feet, 2500.f, nullptr)) return "a ladder close";
    for (int i = 0; i < gMap->nav->LinkCount(); ++i) {   // (Triangle climbs an obstacle close by)
        float a[3], b[3]; uint8_t area = 0;
        if (gMap->nav->LinkAt(i, a, b, &area) && area != AREA_LADDER && (Dist2D(a, gMe.pos) < 2000.f || Dist2D(b, gMe.pos) < 2000.f) &&
            std::fabs(a[1] + 800.f - gMe.pos[1]) < 2000.f) return "an obstacle close";
    }
    const float f = Facing(), knee[3] = {gMe.pos[0], gMe.pos[1] - 300.f, gMe.pos[2]};
    const float ahead[3] = {knee[0] + std::sin(f) * 1200.f, knee[1], knee[2] + std::cos(f) * 1200.f};
    if (!LineClear(knee, ahead)) return "a wall ahead";
    bool box = false; ps2world::ForEachActor(&StageBoxNear, &box);
    if (box) return "a box close";
    if (FreeObjectiveNear(2500.f)) return "an objective item close";   // (Triangle takes a free KEROTAN, GA-KO or microfilm)
    return nullptr;
}
bool EmoteWant(int toward, long long in, const std::string& why) {     // false: busy (an emote asked, made, or just made)
    if (gEm.phase || gEmAsk.at >= 0 || gFrame < gEm.offUntil) return false;
    if (OneLifeMode() && why != "test") {                               // (one-life modes: none, it would give the doll away)
        if (gDebug) Log("emote: none in a one-life mode (%s)", why.c_str());
        return false;
    }
    gEmAsk.at = gFrame + in; gEmAsk.toward = toward; gEmAsk.why = why;
    if (gDebug) Log("emote: wanted in %lld frames (%s)", in, why.c_str());
    return true;
}
// the players' emotes seen (a new one made at the doll: answered), a kill of its own (the kill camera), a real
// ally close by now and then: an emote wanted
void EmoteWatch() {
    for (const Player& p : gPlayers) {
        if (p.local || p.team == 0xFE) continue;
        const bool on = p.alive && EmoteOn(p.bits);
        const bool was = gEmoting[p.id];
        gEmoting[p.id] = on;
        if (!on || was || gRule < 0) continue;
        const float d = Dist2D(gMe.pos, p.pos);
        const bool facingMe = std::fabs(Wrap(AngleTo(p.pos, gMe.pos) - p.facing)) < 40.f * kDeg;
        float eye[3], head[3]; MyEye(eye); BodyPoint(p.pos, 0.95f, head);
        if (d > 20000.f || !facingMe || !LineClear(eye, head)) continue;
        auto it = gAnsweredAt.find(p.id);
        if (it != gAnsweredAt.end() && gFrame - it->second < 1200) continue;
        if (Carrying() || gEm.phase || gEmAsk.at >= 0 || gFrame < gEm.offUntil) continue;   // (busy: the objective, an emote of its own)
        // (an enemy's: never in a fight, nor from one it fought these 30 s: an emote is no way out of a duel)
        const bool enemy = Enemy(p), aiming = (p.bits[1] & 0x4400) != 0;
        if (enemy && (aiming || gFi.target >= 0 || FoughtLately(p.id) || IAmSnake() || gFrame - gHurtAt < 300)) continue;
        const float chance = enemy ? 0.35f : 0.45f + 0.1f * Lv();
        gAnsweredAt[p.id] = gFrame;
        if (Rnd01() >= chance) { if (gDebug) Log("emote: %s's emote not answered", p.name.c_str()); continue; }
        if (!EmoteWant(p.id, enemy ? 8 + (long long)(Rnd() % 12) : 24 + (long long)(Rnd() % 36), "answering " + p.name + "'s emote")) continue;
        if (enemy) { gTruceWith = p.id; gTruceUntil = gFrame + 240; }   // (a truce while it answers: EmoteDone ends it)
    }
    if (gKillAt > 0) {                                  // (a kill of its own a moment ago: its victim's kill camera)
        const long long ago = gFrame - gKillAt;
        gKillAt = -100000;
        const Player* v = PlayerById(gKillVictim);
        if (ago < 60 && v && EnemySide(*v) && Rnd01() < (IAmSnake() ? 0.1f : 0.25f + 0.05f * Lv()))
            EmoteWant(gKillVictim, 20 + (long long)(Rnd() % 25), "after a kill (" + v->name + "'s kill camera)");
    }
    if (gFrame - gGreetAt > 10800 && gFrame % 60 == 0 && gRoom.phase == 2 && !IAmSnake())   // (a real ally close by: 3 minutes apart at most)
        for (const Player& p : gPlayers) {
            if (!Ally(p) || !p.alive || IsDollName(p.name) || Dist2D(p.pos, gMe.pos) > 8000.f) continue;
            float eye[3], head[3]; MyEye(eye); BodyPoint(p.pos, 0.95f, head);
            if (!LineClear(eye, head)) continue;
            gGreetAt = gFrame;
            if (Rnd01() < 0.2f) EmoteWant(p.id, 30, "greeting " + p.name);
            break;
        }
}
void EmoteDone() { if (gTruceWith >= 0 && gTruceUntil > gFrame + 30) gTruceUntil = gFrame + 30; }   // (a truce ends with its emote)
void EmoteDrop() {                                   // (blinded, the objective in its hands, a menu: the emote given up)
    if (gEm.phase || gEmAsk.at >= 0) EmoteDone();
    gEm.phase = 0; gEmAsk = EmoteAsk();
}
// the emote wanted, made: true while it has the pad
bool EmoteFrame() {
    EmoteRun& E = gEm;
    if (E.phase == 0) {
        if (gEmAsk.at < 0 || gFrame < gEmAsk.at) return false;
        if (LifeFrames() >= 0 && LifeFrames() < 120) return false;   // (v137: not in a life's first 2 s, the spawn quiet; the ask waits)
        const EmoteAsk ask = gEmAsk;
        gEmAsk = EmoteAsk();
        if (gFrame - ask.at > 120) { if (gDebug) Log("emote: its moment gone (%s)", ask.why.c_str()); EmoteDone(); return false; }
        if (const char* block = EmoteBlock(ask.toward)) {
            // (a moment's block, the view or the crouch after a fight's last shot, a window, a call, a roll, a throw:
            // waited for 1.5 s)
            const bool brief = !strcmp(block, "in a first person view") || !strcmp(block, "low") || !strcmp(block, "a window") ||
                               !strcmp(block, "a radio call") || !strcmp(block, "a roll") || !strcmp(block, "a throw, a trap, a CQC try");
            if (brief && gFrame - ask.at < 90) { gEmAsk = ask; return false; }
            if (gDebug) Log("emote: not now (%s): %s", block, ask.why.c_str());
            EmoteDone(); return false;
        }
        E = EmoteRun(); E.phase = 1; E.at = gFrame; E.toward = ask.toward; E.why = ask.why; E.hold = 36 + (int)(Rnd() % 30);
        Log("emote: %s", ask.why.c_str());
        StopPath();
    }
    const long long t = gFrame - E.at;
    Act("emoting");
    Steer(0, 0, 0);
    // hurt, or an enemy aiming at it in sight (not an emote's answer to him): Triangle let go
    bool threat = gFrame - gHurtAt < 30;
    for (const auto& k : gKnown) {
        const Player* e = k.second.visible && k.first != E.toward ? PlayerById(k.first) : nullptr;
        if (e && (e->bits[1] & 0x4400) && std::fabs(Wrap(AngleTo(e->pos, gMe.pos) - e->facing)) < 20.f * kDeg) threat = true;
    }
    if (threat && E.phase < 3) { Log("emote: stopped (%s)", gFrame - gHurtAt < 30 ? "hurt" : "an enemy aiming at it"); E.phase = 3; E.at = gFrame; }
    if (E.phase == 1) {                                  // facing whom it is for (short full pushes, 30 frames at most), still a moment
        const Player* p = E.toward >= 0 ? PlayerById(E.toward) : nullptr;
        if (p && t < 30) {
            const float want = AngleTo(gMe.pos, p->pos), err = Wrap(want - Facing());
            if (std::fabs(err) > 20.f * kDeg) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep); return true; }
        }
        if ((gMe.bits[0] & 0xFFFF) != 3) {                             // (standing still first: 40 frames at most)
            if (t < 40) return true;
            Log("emote: not made (not standing still: the state %x)", gMe.bits[0]);
            E.phase = 0; E.offUntil = gFrame + 300; EmoteDone();
            return false;
        }
        E.phase = 2; E.pressAt = gFrame;
        return true;
    }
    if (E.phase == 2) {                                  // Triangle held; the emote seen from 12 frames (not by 30: something else took it)
        const long long h = gFrame - E.pressAt;
        if (h < E.hold) gPad.buttons |= 0x1000;
        // (the word 6 drops its 0xA0000000 a frame now and then during the emote: 3 frames without it, over)
        const bool on = EmoteOn(gMe.bits);
        if (on) { E.seen = true; E.offFrames = 0; } else ++E.offFrames;
        if (h == 30 && !E.seen) {
            Log("emote: not made (the state %x %x): not again for 30 s", gMe.bits[0], gMe.bits[6]);
            E.phase = 0; E.offUntil = gFrame + 1800; EmoteDone();
            return false;
        }
        if (h >= E.hold && E.seen && E.offFrames >= 3) {
            ++gEmotes; E.phase = 0; E.offUntil = gFrame + 300; EmoteDone();
            if (gDebug) Log("emote: made (Triangle held %d frames, over %lld frames after the press)", E.hold, h);
            return false;
        }
        if (h > E.hold + 100) {                          // (still on: on a slope it lasts; the stick ends it)
            if (E.seen) ++gEmotes;
            if (gDebug) Log("emote: %s (still on 100 frames after Triangle let go: on its way)", E.seen ? "made" : "not made");
            E.phase = 0; E.offUntil = gFrame + 300; EmoteDone(); return false;
        }
        return true;
    }
    if (EmoteOn(gMe.bits) && t < 90) return true;        // (stopped: the emote's end, Triangle let go)
    E.phase = 0; E.offUntil = gFrame + 600; EmoteDone();
    return false;
}


// ---- going somewhere ----
struct Goal { float p[3] = {0, 0, 0}; bool set = false; long long at = -100000; const char* why = ""; };
Goal gGoal;
// goals with no way to them (another part of the mesh: a roof, a room without a door on the mesh): left
// alone 30 s (the audit: dolls standing half a minute before a weapon box or a chase they could not reach)
struct NoWay { float p[3]; long long at; };
std::vector<NoWay> gNoWay;
bool NoWayTo(const float* p) {
    bool found = false;
    for (auto it = gNoWay.begin(); it != gNoWay.end();) {
        if (gFrame - it->at > 1800) { it = gNoWay.erase(it); continue; }
        if (Dist2D(it->p, p) < 2500.f && std::fabs(it->p[1] - p[1]) < 3000.f) found = true;
        ++it;
    }
    return found;
}
// in a sniper rifle's scope the player does not walk (the stick turns the view): the handgun first (the
// audit: a sniper at its spot sent to a box stood 40 s "stuck" in its scope). (v141 beta 6) The state machine's exit
// (movement_required): its backup weapon; the window not open yet, no walk meanwhile (FpvExitFrame's ways next frames)
void FpvMoveExit();
bool Unscope() {
    if (!Scoped()) return false;
    FpvMoveExit();
    const int h = FpvOn() ? FpvBackup() : Handgun() > 0 ? Handgun() : MainGun();
    if (h > 0 && SwapTo(h)) { StopPath(); Steer(0, 0, 0); return true; }
    return FpvOn() && LocksView(Hand());
}
// a trip's way (PlanFlank): 2 the doll's taste among the map's ways, 1 another way only when allies go this one (an
// item, a carrier, an intruder), 0 the shortest (a place by someone, a trap's spot, cover, the goal with the item,
// a hiding place, a box)
static int TripWay(const char* why) {
    for (const char* w : {"objective", "carrier", "intruder"}) if (std::strcmp(why, w) == 0) return 1;
    for (const char* w : {"chase", "guard", "defend", "codec", "disperse", "sniper spot"}) if (std::strcmp(why, w) == 0) return 2;
    return 0;
}
bool GoTo(const float* p, const char* why, float arrive) {
    if (OnLadder() && Following()) { FollowPath(); return false; }    // (the climb goes on: no way from mid-ladder)
    const float d = Dist2D(gMe.pos, p);
    if (d < arrive && std::fabs(gMe.pos[1] - p[1]) < 2500.f) { StopPath(); return true; }
    // (its path ended within 0.5 m of a finer arrive: the last steps straight, walking; v136's audit, a doll
    // stood 45 s 0.4 m from a trap's place, its path planned and ended again each frame)
    if (d < 700.f && !Following() && std::fabs(gMe.pos[1] - p[1]) < 2500.f) { Steer(p[0] - gMe.pos[0], p[2] - gMe.pos[2], kStep); return false; }
    if (Unscope()) return false;
    if (!PathServes(p)) {
        // an objective out of the mesh (Lost Forest's KEROTAN on a log): the nearest reachable point
        // within 3 m of it, the rest straight (TakeItem: into it, the action button: a climb)
        const bool objective = std::strcmp(why, "objective") == 0;
        const int trip = TripWay(why);
        // (v141 beta 4) a point planned in vain half a second ago (within 1 m): not planned again each frame (a chase
        // after a player on a crate, on a ladder: 1.4 ms a frame, 36 with PlanNear)
        static float failAt[3] = {0, 0, 0};
        static long long failFrame = -100000;
        const bool failedLately = gFrame - failFrame < 30 && Dist2D(failAt, p) < 1000.f && std::fabs(failAt[1] - p[1]) < 1500.f;
        if (failedLately || (!(trip ? PlanFlank(p, trip == 2) : PlanTo(p)) && !(objective && PlanNear(p, 3000.f)))) {
            if (!failedLately) { memcpy(failAt, p, sizeof failAt); failFrame = gFrame; }
            // no path, close by on the same floor (an item on an obstacle: the mesh ends short of it): straight to it
            if (d < 2000.f && std::fabs(gMe.pos[1] - 800.f - p[1]) < 1200.f) { Steer(p[0] - gMe.pos[0], p[2] - gMe.pos[2], d < 1200.f ? kStep : 1.f); return false; }   // (v137: the last 1.2 m walked, no circling)
            if (!NoWayTo(p)) {
                gNoWay.push_back({{p[0], p[1], p[2]}, gFrame});
                if (gDebug) Log("no way to %.0f %.0f %.0f (%s): left alone 30 s", p[0], p[1], p[2], why);
            }
            return false;
        }
        // (v137) p off the mesh by its edge (a box against a wall, on a crate): the way ends at once at the mesh's point
        // next to it, planned and ended again each frame, the doll standing (dm/d2 f93366: 8 s 1.3 m from a weapon box):
        // the rest straight, walking (an objective: TakeItem's own way)
        const float* end = PathGoal();
        if (!objective && end && d < 2000.f && Dist2D(end, gMe.pos) < 600.f && std::fabs(gMe.pos[1] - 800.f - p[1]) < 1200.f) {
            StopPath(); Steer(p[0] - gMe.pos[0], p[2] - gMe.pos[2], kStep); return false;
        }
    }
    FollowPath();
    return false;
}

// ---- the orders and the objectives ----
long long gHuntAt = -100000;
float gHuntTo[3];
void Hunt() {
    // toward the fights: the last place an enemy was known (seen, heard: his steps within 5 m, his shots
    // within 35 m), else near where enemies were known lately, else anywhere
    if (Unscope()) { Act("switching_weapon"); return; }
    const Known* best = nullptr; int bestId = -1;
    const bool quietSide = OneLifeAttacker();             // (one-life attackers: no chase, no roaming toward the enemies)
    for (const auto& k : gKnown) {
        if (quietSide || gFrame - k.second.last >= 600) continue;
        // (asleep with nothing to finish it: no threat, left where it lies; v133's audit, a doll with only
        // its Mk22 ran circles 10 s and more around a sleeping enemy, chasing and roaming in turn)
        const Player* e = PlayerById(k.first);
        if (e && Asleep(*e) && (IAmSnake() || !FinishAsleep())) continue;
        if (!best || k.second.last > best->last) { best = &k.second; bestId = k.first; }
    }
    // a place reached with nobody seen there: not gone to again for 10 s unless the enemy is known 3 m
    // away from it (no dithering between the chase and the roaming)
    static float doneAt[3]; static long long doneFrame = -100000;
    if (best && Dist2D(gMe.pos, best->pos) <= 1500.f) { memcpy(doneAt, best->pos, sizeof doneAt); doneFrame = gFrame; }
    const bool visited = best && gFrame - doneFrame < 600 && Dist2D(best->pos, doneAt) < 3000.f;
    if (best && !visited && Dist2D(gMe.pos, best->pos) > 1500.f && !NoWayTo(best->pos)) {
        Act("chasing", PlayerById(bestId) ? PlayerById(bestId)->name : std::string());
        GoTo(best->pos, "chase");
        return;
    }
    // (v137) the roaming goal kept until reached, 25 s at most: a new one every 8 s turned the doll back on its way
    // (v136's audit, Mountaintop: 8 m one way, then back the same way); anywhere: ahead of it when it runs (6 drawn)
    if (gFrame - gHuntAt > (gSet.order == "target" ? 480 : 1500) || !Following()) {   // (a target ordered: his area every 8 s)
        gHuntAt = gFrame;
        // where the enemies were known these 2 minutes (seen, heard: 8 m about), 60 % of the time; the one
        // ordered (target): his area (the order says where to look); else anywhere
        std::vector<const float*> near;
        for (const auto& k : gKnown) if (gFrame - k.second.last < 7200) near.push_back(k.second.pos);
        const Player* named = gSet.order == "target" ? PlayerByName(gSet.target) : nullptr;
        bool ok = false;
        float g[3];
        if (named && Enemy(*named) && MapReady() && gMap->nav->RandomPoint(named->pos, 12000.f, g, Rnd)) { memcpy(gHuntTo, g, sizeof g); ok = true; }
        else if (!quietSide && !near.empty() && Rnd01() < 0.6f && MapReady() && gMap->nav->RandomPoint(near[Rnd() % near.size()], 8000.f, g, Rnd)) {
            memcpy(gHuntTo, g, sizeof g); ok = true;
        }
        const uint32_t st = gMe.bits[0] & 0xFFFF;
        const bool running = st == 0x4802 || st == 0x5002;
        bool behind = false;                                           // (none ahead: the first drawn)
        for (int i = 0; i < 6 && !ok; ++i) {
            if (!RandomReachable(40000.f, g)) continue;
            if (!running || std::fabs(Wrap(AngleTo(gMe.pos, g) - Facing())) < 100.f * kDeg) { memcpy(gHuntTo, g, sizeof g); ok = true; }
            else if (!behind) { memcpy(gHuntTo, g, sizeof g); behind = true; }
        }
        if (!ok && !behind) return;
        PlanTo(gHuntTo);
    }
    Act(gSet.order == "target" ? "hunting" : "roaming", gSet.order == "target" ? gSet.target : std::string());
    FollowPath();
}
// hiding: a low place (a vent, a passage to crawl in) or a corner far from the enemies, out of their sight
float gHideSpot[3]; bool gHideHave = false; long long gHideAt = -100000;
float HideScore(const float* p) {
    uint8_t area = MapReady() ? gMap->nav->AreaAt(p) : 0;
    float s = area == AREA_CRAWL ? 6000.f : area == AREA_CROUCH ? 3000.f : 0.f;
    float nearest = 60000.f;
    for (const auto& k : gKnown) {                   // (the enemies known this minute, where they were)
        if (gFrame - k.second.last > 3600) continue;
        const float d = Dist2D(k.second.pos, p);
        nearest = std::min(nearest, d);
        float eye[3]; BodyPoint(k.second.pos, 0.95f, eye);
        const float at[3] = {p[0], p[1] + 400.f, p[2]};
        if (LineClear(eye, at)) s -= 8000.f;       // in its sight
    }
    return s + std::min(nearest, 30000.f);
}
void Hide(const char* going = "going_to_hide") {
    if (!gHideHave || gFrame - gHideAt > 1800 || HideScore(gHideSpot) < -4000.f) {
        float best[3]; float bs = -1e30f; bool any = false;
        for (int i = 0; i < 24; ++i) {
            float c[3];
            if (!RandomReachable(35000.f, c)) continue;
            const float s = HideScore(c);
            if (s > bs) { bs = s; memcpy(best, c, sizeof c); any = true; }
        }
        if (any) { memcpy(gHideSpot, best, sizeof best); gHideHave = true; gHideAt = gFrame; StopPath(); }
    }
    if (!gHideHave) { Hunt(); return; }
    if (GoTo(gHideSpot, "hide", 600.f)) {
        Act("hiding");
        Steer(0, 0, 0);
        // a vent, a passage to crawl in: lying there; elsewhere in the cardboard box (the item window), still
        if (MapReady() && gMap->nav->AreaAt(gMe.pos) == AREA_CRAWL) { SetLow(true); return; }
        if (!BoxOn()) ItemTo(0xE);
    } else {
        if (BoxOn()) ItemTo(0);                     // (the box walks slowly: off on the way)
        Act(going);
    }
}
// its place by the one it follows: behind him on his left or his right (55 degrees off, 2 m aside at 2.5 m: not
// in his steps), keep away (the side fewer other allies are near, else its own); behind from his moves, turned
// 50 degrees at most a time (at first: toward the doll); not where the mesh or a wall parts it from him
struct FollowPlace { int id = -1; float ref[3] = {0, 0, 0}, back[2] = {0, 0}; int side = -1; long long at = -100000; bool settled = false, moving = false, alongside = false, runs = false, waiting = false, catching = false; };
FollowPlace gFp;
bool PlaceBy(const Player& p, float keep, float* out) {
    if (!MapReady()) return false;
    if (gFp.id != p.id || gFrame - gFp.at > 600) {
        gFp = FollowPlace(); gFp.id = p.id; memcpy(gFp.ref, p.pos, sizeof gFp.ref);
        const float bx = gMe.pos[0] - p.pos[0], bz = gMe.pos[2] - p.pos[2], bl = std::sqrt(bx * bx + bz * bz);
        if (bl > 1.f) { gFp.back[0] = bx / bl; gFp.back[1] = bz / bl; }
        else { gFp.back[0] = -std::sin(p.facing); gFp.back[1] = -std::cos(p.facing); }
    }
    gFp.at = gFrame;
    const float mx = p.pos[0] - gFp.ref[0], mz = p.pos[2] - gFp.ref[2], ml = std::sqrt(mx * mx + mz * mz);
    if (ml > 1500.f) {
        const float a0 = std::atan2(gFp.back[0], gFp.back[1]);
        const float da = std::max(-0.87f, std::min(0.87f, Wrap(std::atan2(-mx, -mz) - a0)));
        gFp.back[0] = std::sin(a0 + da); gFp.back[1] = std::cos(a0 + da);
        memcpy(gFp.ref, p.pos, sizeof gFp.ref);
    }
    float c[2][3]; bool ok[2]; float crowd[2] = {0, 0};
    for (int s = 0; s < 2; ++s) {
        const float a = s ? 0.96f : -0.96f;
        const float rx = gFp.back[0] * std::cos(a) - gFp.back[1] * std::sin(a), rz = gFp.back[0] * std::sin(a) + gFp.back[1] * std::cos(a);
        const float q[3] = {p.pos[0] + rx * keep, p.pos[1] - 700.f, p.pos[2] + rz * keep};
        ok[s] = gMap->nav->Nearest(q, 500.f, c[s]) && Dist2D(c[s], q) < 700.f && std::fabs(c[s][1] - q[1]) < 1000.f && gMap->nav->Walkable(p.pos, c[s], true);
        for (const Player& o : gPlayers)
            if (!o.local && o.alive && Ally(o) && o.id != p.id && Dist2D(o.pos, c[s]) < 2500.f) crowd[s] += 2500.f - Dist2D(o.pos, c[s]);
    }
    if (gFp.side < 0) gFp.side = crowd[0] < crowd[1] ? 0 : crowd[1] < crowd[0] ? 1 : (HabitSide() > 0 ? 1 : 0);
    else if (crowd[gFp.side] > crowd[1 - gFp.side] + 1000.f) gFp.side = 1 - gFp.side;
    const int s = ok[gFp.side] ? gFp.side : ok[1 - gFp.side] ? 1 - gFp.side : -1;
    if (s < 0) return false;
    memcpy(out, c[s], sizeof c[s]);
    return true;
}
// an escort where no place beside the carrier is walkable (a narrow passage; v136): ahead of him on his way to
// the goal when it is ahead already (it meets what comes first; there, it waits until that point is 2.5 m on),
// else 3 to 4.5 m behind him, a look back a second every 5 s while he walks (what comes after him). His way
// planned again every half second or 1.5 m of his; the mode kept 2 s once taken. False: not on his way
struct NarrowRun { int id = -1; long long pathAt = -1000, usedAt = -1000, since = -1000; std::vector<Waypoint> way; float len = 0, start[3] = {0, 0, 0}, at[3] = {0, 0, 0}; bool there = false; };
NarrowRun gNe;
bool NarrowEscort(const Player& p, const float* goal) {
    if (gNe.id != p.id || gFrame - gNe.pathAt > 30 || Dist2D(gNe.start, p.pos) > 1500.f) {
        float gc[3], gg[3];
        const float feet[3] = {p.pos[0], p.pos[1] - 700.f, p.pos[2]};
        gNe.way.clear(); gNe.len = 0; gNe.pathAt = gFrame; memcpy(gNe.start, p.pos, sizeof gNe.start);
        if (gNe.id != p.id) gNe.there = false;
        gNe.id = p.id;
        if (!gMap->nav->Nearest(feet, 800.f, gc) || !gMap->nav->Nearest(goal, 2500.f, gg) || !gMap->nav->Path(gc, gg, gNe.way, &gNe.len)) gNe.way.clear();
        else gNe.way.insert(gNe.way.begin(), Waypoint{{gc[0], gc[1], gc[2]}, AREA_STAND, false});
    }
    const std::vector<Waypoint>& w = gNe.way;
    if (w.size() < 2 || gNe.len < 3000.f) return false;
    float best = 1e30f, bestAlong = 0, walked = 0;
    for (size_t i = 1; i < w.size(); ++i) {
        const float* a = w[i - 1].p; const float* b = w[i].p;
        const float ex = b[0] - a[0], ez = b[2] - a[2], l2 = ex * ex + ez * ez, seg = std::sqrt(l2);
        const float t = l2 > 1.f ? std::max(0.f, std::min(1.f, ((gMe.pos[0] - a[0]) * ex + (gMe.pos[2] - a[2]) * ez) / l2)) : 0.f;
        const float q[3] = {a[0] + ex * t, a[1], a[2] + ez * t};
        const float d = Dist2D(q, gMe.pos);
        if (d < best && std::fabs(q[1] + 800.f - gMe.pos[1]) < 2000.f) { best = d; bestAlong = walked + t * seg; }
        walked += seg;
    }
    const float dp0 = Dist2D(gMe.pos, p.pos);
    if (best > 2500.f && !(best < 1e29f && bestAlong < 1.f && dp0 < 4500.f)) return false;   // (behind him on his floor, his way beginning at his feet: on it)
    gNe.usedAt = gFrame;
    if (bestAlong > 1500.f) {                                 // (ahead of him: 5 m ahead on his way)
        const float want = std::min(gNe.len - 500.f, 5000.f);
        float at[3] = {w.back().p[0], w.back().p[1], w.back().p[2]};
        walked = 0;
        for (size_t i = 1; i < w.size(); ++i) {
            const float seg = Dist2D(w[i - 1].p, w[i].p);
            if (walked + seg >= want && seg > 1.f) { const float k = (want - walked) / seg; for (int j = 0; j < 3; ++j) at[j] = w[i - 1].p[j] + (w[i].p[j] - w[i - 1].p[j]) * k; break; }
            walked += seg;
        }
        if (gNe.there && Dist2D(at, gMe.pos) < 2500.f) { StopPath(); Steer(0, 0, 0); return true; }   // (there: until the point moved on)
        memcpy(gNe.at, at, sizeof at);
        gNe.there = GoTo(at, "follow", 900.f);
        if (gNe.there) Steer(0, 0, 0);
        return true;
    }
    gNe.there = false;
    const float dp = Dist2D(gMe.pos, p.pos), sp = std::hypot(p.vel[0], p.vel[1]);
    if (dp > 4500.f || (dp > 3000.f && sp > 20.f)) { GoTo(p.pos, "follow", 3000.f); return true; }
    StopPath();
    if (sp > 20.f && (gFrame / 60) % 5 == 0) {                // (a look back)
        const float back = std::atan2(-p.vel[0], -p.vel[1]);
        if (std::fabs(Wrap(back - Facing())) > 25.f * kDeg && gFrame % 6 < 2) Steer(std::sin(back), std::cos(back), kStep); else Steer(0, 0, 0);
        return true;
    }
    Steer(0, 0, 0);
    return true;
}
void Follow(const Player& p, float keep = 2500.f, const float* goal = nullptr) {   // (its activity said by the caller; goal: an escort's)
    float at[3];
    const float dp = Dist2D(gMe.pos, p.pos);
    // (v137) him in a crawl passage (a vent, under a truck): after him in his steps, 1.8 m behind (its way crawls in, the path
    // follower lies down there); its place by him is on the standing floor only, and it stood outside within its keep
    if (!OldWays() && InCrawl(p.pos)) {
        if (dp < 1800.f && std::fabs(p.pos[1] - gMe.pos[1]) < 1500.f) { StopPath(); Steer(0, 0, 0); return; }
        GoTo(p.pos, "follow", 1500.f);
        return;
    }
    if (!OldWays() && goal && gNe.id == p.id && gFrame - gNe.usedAt <= 2 && gFrame - gNe.since < 120 && MapReady() && NarrowEscort(p, goal)) return;   // (the narrow mode kept 2 s)
    if (!OldWays() && dp < keep + 8000.f && PlaceBy(p, keep, at)) {
        const float da = Dist2D(gMe.pos, at), sp = std::hypot(p.vel[0], p.vel[1]);
        gFp.moving = sp > (gFp.moving ? 15.f : 25.f);                   // (on the move: 1.5 m/s, 0.9 to stop)
        if (!gFp.moving && (da < 900.f || (gFp.settled && da < 2000.f && dp < keep + 1500.f))) { gFp.settled = true; gFp.alongside = false; StopPath(); Steer(0, 0, 0); return; }
        gFp.settled = false;
        gFp.alongside = gFp.moving && da < (gFp.alongside ? 3500.f : 2500.f) && !Low();
        if (!gFp.alongside) gFp.waiting = gFp.catching = false;
        if (gFp.alongside) {
            // alongside him: toward its place and his move's next second (half a second walking), where the way
            // is clear and level (the wide mesh); running from 0.8 m behind its place (up to 0.3 m behind), at his
            // pace up to 0.3 m ahead of it, waiting beyond until it is back there
            gFp.runs = sp > (gFp.runs ? 45.f : 60.f);
            const float k = gFp.runs ? 60.f : 30.f;
            const float ahead[3] = {at[0] + p.vel[0] * k, at[1], at[2] + p.vel[1] * k};
            float gnd[3];
            if (Ground(gnd) && std::fabs(at[1] - gnd[1]) < 1000.f && (gMap->wide ? *gMap->wide : *gMap->nav).Walkable(gnd, ahead, true)) {
                StopPath();
                const float rel = ((gMe.pos[0] - at[0]) * p.vel[0] + (gMe.pos[2] - at[2]) * p.vel[1]) / sp;
                gFp.waiting = rel > (gFp.waiting ? 0.f : 300.f);
                gFp.catching = rel < (gFp.catching ? -300.f : -800.f);
                if (gFp.waiting) Steer(0, 0, 0);
                else Steer(ahead[0] - gMe.pos[0], ahead[2] - gMe.pos[2], gFp.runs || gFp.catching ? 1.f : kStep);
                return;
            }
        }
        GoTo(at, "follow", 600.f);
        return;
    }
    gFp.settled = false;
    if (!OldWays() && goal && dp < 12000.f && MapReady() && NarrowEscort(p, goal)) { gNe.since = gFrame; return; }   // (the mode taken: kept 2 s from here)
    if (dp < keep) { StopPath(); Steer(0, 0, 0); return; }
    GoTo(p.pos, "follow", keep - 500.f);
}
// a guard's points around c, the best first: in its own sector (its bearing drawn each life, the nearer the
// better), 4 m off the allies when it can
int GuardPoints(const float* c, float ring, float minR, float out[][3], int max) {
    if (!MapReady()) return 0;
    struct Cand { float p[3]; float s; };
    std::vector<Cand> v;
    int n0 = 0;
    for (int i = 0; i < 16; ++i) {
        Cand k;
        if (!gMap->nav->RandomPoint(c, ring, k.p, Rnd) || Dist2D(k.p, c) < minR) continue;
        if (OldWays()) { memcpy(out[n0++], k.p, sizeof k.p); if (n0 >= max) break; continue; }
        if (Dist2D(k.p, gMe.pos) < 2500.f || Dist2D(k.p, c) > ring) continue;   // (a step is no round; within the ring)
        k.s = -std::fabs(Wrap(std::atan2(k.p[0] - c[0], k.p[2] - c[2]) - HabitAngle())) * 1500.f;
        for (const Player& q : gPlayers)
            if (!q.local && q.alive && Ally(q)) { const float d = Dist2D(q.pos, k.p); if (d < 4000.f) k.s -= (4000.f - d) * 2.f; }
        v.push_back(k);
    }
    if (OldWays()) return n0;
    std::sort(v.begin(), v.end(), [](const Cand& a, const Cand& b) { return a.s > b.s; });
    int n = 0;
    for (const Cand& k : v) { if (n >= max) break; memcpy(out[n++], k.p, sizeof k.p); }
    return n;
}
// its place in its own sector around c, r away (on the mesh about c's height; else c itself)
void SectorPlace(const float* c, float r, float* out) {
    const float a = HabitAngle(), q[3] = {c[0] + std::sin(a) * r, c[1], c[2] + std::cos(a) * r};
    if (OldWays() || !MapReady() || !gMap->nav->Nearest(q, 2000.f, out) || Dist2D(out, q) > 2000.f || Dist2D(out, c) > r + 500.f ||
        std::fabs(out[1] - c[1]) > 2000.f || OtherPart(out) || NoWayTo(out)) memcpy(out, c, sizeof(float) * 3);
}
// a guard's pause at each point of its round, 2 to 5 s, looking out from what it guards (c), one way then
// another (short pushes turn a standing player); true while it lasts
long long gRestFrom = -1, gRestUntil = -1; float gRestLook = 0.f;
bool GuardRest(const float* c) {
    if (OldWays()) return false;
    if (gRestUntil < 0) { gRestFrom = gFrame; gRestUntil = gFrame + 120 + Rnd() % 180; gRestLook = AngleTo(c, gMe.pos) + (Rnd01() - 0.5f) * 1.6f; }
    if (gFrame >= gRestUntil) { gRestUntil = -1; return false; }
    Steer(0, 0, 0);
    const float want = gRestLook + (gFrame - gRestFrom > (gRestUntil - gRestFrom) / 2 ? 0.9f : 0.f);
    if (std::fabs(Wrap(want - Facing())) > 20.f * kDeg && gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep);
    return true;
}
float gDefendAt[3]; bool gDefendHave = false;
void Defend() {
    if (!gDefendHave) { memcpy(gDefendAt, gMe.pos, sizeof gDefendAt); gDefendHave = true; }
    Act("defending");
    if (Unscope()) return;
    if (Dist2D(gMe.pos, gDefendAt) > 5000.f) { GoTo(gDefendAt, "defend", 2000.f); return; }
    if (!Following() && gFrame % 240 == 0) {
        float g[1][3];
        if (GuardPoints(gDefendAt, 4000.f, 0.f, g, 1)) PlanTo(g[0]);
    }
    if (Following()) FollowPath(); else Steer(0, 0, 0);
}

// ---- a sniper's place: high, with long views (the map's reachable points scored by what they see) ----
struct Spot { float p[3]; float score; };
std::vector<Spot> gSpots;
int gSpotsTried = 0;
float gSpotAt[3]; bool gSpotHave = false; long long gSpotSince = -1;
void SpotsFrame() {                                         // a few points a frame, 72 in all per map
    if (gSpotsTried >= 72 || !MapReady()) return;
    for (int k = 0; k < 4 && gSpotsTried < 72; ++k, ++gSpotsTried) {
        float c[3], t[3];
        if (!RandomReachable(80000.f, c)) continue;
        const float eye[3] = {c[0], c[1] + 1500.f, c[2]};
        int seen = 0; float far = 0;
        for (int j = 0; j < 12; ++j) {
            if (!RandomReachable(80000.f, t)) continue;
            const float d = Dist2D(c, t);
            if (d < 10000.f) continue;
            const float at[3] = {t[0], t[1] + 1000.f, t[2]};
            if (LineClear(eye, at)) { ++seen; far += d; }
        }
        if (!seen) continue;
        gSpots.push_back({{c[0], c[1], c[2]}, seen * 1000.f + far / 40.f + c[1] * 0.6f});
    }
    if (gSpotsTried >= 72) {
        std::sort(gSpots.begin(), gSpots.end(), [](const Spot& a, const Spot& b) { return a.score > b.score; });
        if (gSpots.size() > 6) gSpots.resize(6);
        if (gDebug) for (const Spot& x : gSpots) Log("sniper spot %.0f %.0f %.0f (score %.0f)", x.p[0], x.p[1], x.p[2], x.score);
    }
}
// the enemies a spot would see (within the view, a clear line from a crouched eye to their chests): a
// sniper goes where the fight can be seen
int SpotSees(const float* p) {
    const float eye[3] = {p[0], p[1] + 1100.f, p[2]};
    int n = 0;
    for (const auto& k : gKnown) {                   // (the enemies known this minute, where they were)
        if (gFrame - k.second.last > 3600 || Dist2D(k.second.pos, p) > Sk().view) continue;
        float c[3]; BodyPoint(k.second.pos, 0.45f, c);
        if (LineClear(eye, c)) ++n;
    }
    return n;
}
long long gSpotSawAt = -1;
// a sniper at its spot: within 0.9 m of it, or, once there, within 2.5 m (a run's stop carries the player on a
// metre), on its floor (Snipe, and the weapon out of a fight: one test, no rifle-handgun swaps to and fro)
bool SniperAtSpot() {
    if (!Sniper() || !gSpotHave) return false;
    const float d = Dist2D(gMe.pos, gSpotAt);
    return std::fabs(gMe.pos[1] - 800.f - gSpotAt[1]) < 1500.f && (d < 900.f || (gSpotSince >= 0 && d < 2500.f));
}
bool Snipe() {
    if (!Sniper() || gSpots.empty() || gSpotsTried < 72) return false;
    if (!gSpotHave) {                                        // one of the best, seeing the enemies, not too close to them
        const Spot* best = nullptr; float bs = -1e30f;
        for (const Spot& x : gSpots) {
            if (NoWayTo(x.p) || OtherPart(x.p)) continue;          // (no way to it now, or ever)
            float near = 60000.f;
            for (const auto& k : gKnown) if (gFrame - k.second.last < 3600) near = std::min(near, Dist2D(k.second.pos, x.p));
            const float sc = x.score + SpotSees(x.p) * 6000.f + std::min(near, 15000.f) / 10.f - (near < 8000.f ? 5000.f : 0.f) + Rnd01() * 500.f;
            if (sc > bs) { bs = sc; best = &x; }
        }
        if (!best) return false;
        memcpy(gSpotAt, best->p, sizeof gSpotAt); gSpotHave = true; gSpotSince = -1; gSpotSawAt = gFrame;
    }
    const float dSpot = Dist2D(gMe.pos, gSpotAt);
    const bool floor = std::fabs(gMe.pos[1] - 800.f - gSpotAt[1]) < 1500.f;
    // (no way to it: another spot; v134's audit: a sniper stood 30 s where its way ran into a wall)
    if (gSpotSince < 0 && NoWayTo(gSpotAt)) { gSpotHave = false; return false; }
    // (v141) in its scope's view, an enemy seen these 5 s: where it stands is its spot now (a roof, a climb's top), not
    // the handgun to walk on and the rifle again at his next sight (v141's audit: 30 s to and fro on a City Under Siege roof)
    if (Scoped() && !(gSpotSince >= 0 && SniperAtSpot())) {
        bool seen = false;
        for (const auto& k : gKnown) if (k.second.visible || gFrame - k.second.seen < 300) { seen = true; break; }
        if (seen) {
            gSpotAt[0] = gMe.pos[0]; gSpotAt[1] = gMe.pos[1] - 800.f; gSpotAt[2] = gMe.pos[2];
            gSpotHave = true; gSpotSince = gFrame; gSpotSawAt = gFrame;
            if (gDebug) Log("sniping: from where it stands (%.0f %.0f %.0f), an enemy seen these 5 s", gSpotAt[0], gSpotAt[1], gSpotAt[2]);
        }
    }
    // (once there, there within 2.5 m: a run's stop carries the player on a metre, v134's audit: a sniper went
    // to and fro across its spot's 0.7 m, 53 times on Ghost Factory)
    const bool settled = gSpotSince >= 0 && SniperAtSpot();
    if (!settled) {
        if (!(dSpot < 900.f && floor) && Scoped() && Handgun() > 0 && SwapTo(Handgun())) { Act("switching_weapon"); return true; }   // (no walking in the scope)
        if (!GoTo(gSpotAt, "sniper spot", 700.f)) { gSpotSince = -1; Act("going_to_sniper_spot"); return true; }
    }
    StopPath();
    if (gSpotSince < 0) { gSpotSince = gFrame; gSpotSawAt = gFrame; }
    Act("sniping");
    // looking over the map, toward its middle, sweeping: crouched, the rifle's scope (the stick turns
    // the view), else the facing turned readied (0.55 at least)
    const float t = (gFrame - gSpotSince) / 240.f;
    const float look = std::atan2(-gSpotAt[0], -gSpotAt[2]) + std::sin(t) * 1.2f;
    if (!SetLow(true)) return true;                          // crouched first
    // ((v141 beta 6) the rifle's scope only for a watch: an enemy seen at its range within the tier's memory; else crouched, the
    // body's facing sweeping, the rifle taken at the next sight: FightWeaponFor)
    if (SniperRifle() > 0 && Hand() != SniperRifle() && FpvEntryOk(SniperRifle(), "sniper_watch") && SwapTo(SniperRifle())) return true;
    if (ThrowableInHand()) {                                 // (a grenade in the hands, the window free in a moment: no sweep)
        Steer(0, 0, 0);
        if (gFrame - gSpotSince > 3600 || gFrame - gSpotSawAt > 1200) gSpotHave = false;
        return true;
    }
    if (Scoped()) {
        float m[16];
        if (ps2world::ViewMatrix(m)) {
            const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
            StickFpv(Wrap(look - camYaw) * 0.3f, (-2.f * kDeg - camPitch) * 0.5f);   // slowly (the scope magnifies)
        }
    } else if (std::fabs(Wrap(look - Facing())) > 25.f * kDeg) {
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
        Steer(std::sin(look), std::cos(look), 0.6f);
    } else Steer(0, 0, 0);
    if (!gKnown.empty()) for (const auto& k : gKnown) if (k.second.visible) gSpotSawAt = gFrame;
    if (gFrame - gSpotSince > 3600 || gFrame - gSpotSawAt > 1200) gSpotHave = false;   // a minute there, or 20 s seeing nobody: another one
    return true;
}

// ---- items: the food boxes (NewItemBox_Food) when hurt, the weapon boxes (NewItemBox2) close by; their
// place at +0x1A0. A gun full takes nothing from a box and the box stays (the audit: dolls touching the
// same one again and again, a third of their time): a weapon box when a gun of the doll's is under half
// its rounds of this life, else each box once a life (a weapon it has not got). A box touched is left
// alone a minute, by its place (the game makes a taken box anew); the one chosen is kept until reached,
// 10 s and its distance at most (no dithering between two boxes, nor between a box and the roaming) ----
struct Box { uint32_t w; float p[3]; bool food; };
struct Touched { float p[3]; long long at; };
std::vector<Touched> gBoxTouched;                // this life's boxes touched (the weapon boxes once a life)
bool BoxTouched(const float* p, long long within) {
    for (const Touched& t : gBoxTouched) if (gFrame - t.at < within && Dist2D(t.p, p) < 600.f && std::fabs(t.p[1] - p[1]) < 1500.f) return true;
    return false;
}
void FindBox(uint32_t w, const std::string& name, void* ctx) {
    const bool food = name == "NewItemBox_Food";
    if (!food && name != "NewItemBox2") return;
    Box b; b.w = w; b.food = food;
    for (int i = 0; i < 3; ++i) b.p[i] = RF(w + 0x1A0 + 4 * i);
    if (std::fabs(b.p[0]) > 1e6f || std::fabs(b.p[2]) > 1e6f || !(b.p[1] > -50000.f)) return;   // (a box taken waits 100 m under the map)
    ((std::vector<Box>*)ctx)->push_back(b);
}
std::map<int, int> gRoundsMax;                   // each gun's most rounds this life
void TrackRounds() {
    const Inventory& v = Inv();
    if (!v.ok) return;
    for (int c : v.weapons) if (c > 0 && c < 0x40 && v.rounds[c] > 0) gRoundsMax[c] = std::max(gRoundsMax[c], v.rounds[c]);
}
bool GunLow() {
    const Inventory& v = Inv();
    if (!v.ok) return false;
    for (int c : {PrimaryCode(), SecondaryCode(), MainGun(), Handgun()}) {   // (MainGun skips an empty primary)
        if (c <= 0 || c >= 0x40 || v.rounds[c] < 0) continue;
        const auto it = gRoundsMax.find(c);
        if (it != gRoundsMax.end() && it->second >= 4 && v.rounds[c] * 2 < it->second) return true;
    }
    return false;
}
// (v141 beta 3) the food boxes judged by what they give: a food box touched, its life and its food eaten (the variable set's
// +0x44) watched 90 frames; two touched in a row for nothing (the A/B bench: 67 touched, 0 eaten, a doll at 100 of 200 ran
// from box to box 36 s, turned back at each), none more on this map unless one gave something (each map judged anew)
struct FoodRun { long long touchAt = -1; int life = 0, eaten = -1, fails = 0; bool works = false, off = false; };
FoodRun gFood;
void FoodWatch() {
    static std::string foodMap;
    if (MapReady() && gMap->key != foodMap) { foodMap = gMap->key; gFood = FoodRun(); }
    if (gFood.touchAt < 0) return;
    const int eaten = MyScore(0x44);
    if (gMe.life > gFood.life || (eaten >= 0 && gFood.eaten >= 0 && eaten > gFood.eaten)) {
        gFood.works = true; gFood.fails = 0; gFood.touchAt = -1;
        if (gDebug) Log("item: the food box gave something (life %d)", gMe.life);
    } else if (gFrame - gFood.touchAt > 90 || !gMe.alive) {
        gFood.touchAt = -1;
        if (gMe.alive && ++gFood.fails >= 2 && !gFood.works && !gFood.off) {
            gFood.off = true;
            Log("item: the food boxes give nothing here (%d touched, its life and its food eaten as they were): none more on this map", gFood.fails);
        }
    }
}
bool gBoxGoingHave = false;
Box gBoxGoing;
long long gBoxGoingSince = 0;
float gBoxGoingDist = 0;                         // (its distance when chosen: the time allowed)
bool Items() {
    std::vector<Box> boxes;
    ps2world::ForEachActor(&FindBox, &boxes);
    const bool hurt = gMe.life < Sk().hideLife, dry = NoAmmo(), low = GunLow() || dry;
    // ((v141 beta 7) tier 10's cheat: no food run; no weapon box with an enemy within 20 m, but its guns empty)
    const bool cheat = CheatOn(), cheatNear = cheat && !dry && CheatEnemyDist() < 20000.f;
    const Box* best = nullptr;
    if (gBoxGoingHave)                                                    // (the one chosen: still there?)
        for (const Box& b : boxes)
            if (b.food == gBoxGoing.food && Dist2D(b.p, gBoxGoing.p) < 300.f && std::fabs(b.p[1] - gBoxGoing.p[1]) < 1000.f &&
                (b.food ? hurt && !gFood.off && !cheat : !cheatNear) && !NoWayTo(b.p)) best = &b;
    if (!best) {
        float bd = 1e30f;
        for (const Box& b : boxes) {
            if (BoxTouched(b.p, b.food ? 3600 : low ? 3600 : 1LL << 40)) continue;   // (a weapon box once a life, unless a gun runs low)
            if (b.food && gFood.off) continue;                       // ((v141 beta 3) they give nothing here)
            if (b.food ? cheat : cheatNear) continue;
            const float d = Dist2D(gMe.pos, b.p);
            if (b.food ? !hurt || d > 30000.f : d > (dry ? 40000.f : 9000.f) || std::fabs(b.p[1] + 800.f - gMe.pos[1]) > (dry ? 8000.f : 2500.f)) continue;
            if (NoWayTo(b.p) || OtherPart(b.p)) continue;                // (no way to it now, or ever; (v141 beta 4) after the distance)
            const float score = d - (b.food ? 20000.f : dry ? 30000.f : 0.f);   // (its guns empty: a weapon box first)
            if (score < bd) { bd = score; best = &b; }
        }
        if (!best) { gBoxGoingHave = false; return false; }
        gBoxGoing = *best; gBoxGoingHave = true; gBoxGoingSince = gFrame; gBoxGoingDist = Dist2D(gMe.pos, best->p);
        if (gDebug) Log("item: %s box at %.0f %.0f %.0f%s", best->food ? "food" : "weapon", best->p[0], best->p[1], best->p[2], !best->food && low ? " (a gun low)" : "");
    }
    Act(best->food ? "going_for_food" : "going_for_weapon");
    // (the path ends 0.5 m from its point: the last steps straight onto the box; 10 s and a frame for each 30
    // mm of its first distance, the run's pace with its detours)
    const float bdist = Dist2D(gMe.pos, best->p);
    bool at = bdist < 250.f && std::fabs(gMe.pos[1] - best->p[1]) < 2500.f;
    // (v137: walked; run, the turn was wider than the 0.25 m to reach: v136's audit, a doll circled a food box 3.5 s)
    if (!at && bdist < 900.f && std::fabs(gMe.pos[1] - 800.f - best->p[1]) < 1200.f) { StopPath(); Steer(best->p[0] - gMe.pos[0], best->p[2] - gMe.pos[2], kStep); }
    else if (!at) at = GoTo(best->p, "box", 250.f);
    // (v137) within 2.5 m of it and no nearer by 0.1 m in 2 s (on a crate, behind a rail): not reached now rather than at
    // the time limit (dm/d2 f93366: 8 s still 1.3 m from a weapon box)
    static long long progFor = -1, progAt = 0;
    static float progBest = 1e30f;
    if (progFor != gBoxGoingSince) { progFor = gBoxGoingSince; progBest = 1e30f; progAt = gFrame; }
    if (bdist < progBest - 100.f) { progBest = bdist; progAt = gFrame; }
    const bool stalled = !at && bdist < 2500.f && gFrame - progAt > 120;
    if (at) {
        gBoxTouched.push_back({{best->p[0], best->p[1], best->p[2]}, gFrame}); gBoxGoingHave = false;
        if (best->food) { gFood.touchAt = gFrame; gFood.life = gMe.life; gFood.eaten = MyScore(0x44); }
        if (gDebug) Log("item: box touched (life %d)", gMe.life);
    } else if (stalled || gFrame - gBoxGoingSince > 600 + (int)(gBoxGoingDist / 30.f)) {   // not reached: left alone a minute
        gBoxTouched.push_back({{best->p[0], best->p[1], best->p[2]}, gFrame}); gBoxGoingHave = false; StopPath();
        if (gDebug) Log("item: box not reached (%.1f m away%s)", Dist2D(gMe.pos, best->p) / 1000.f, stalled ? ", no nearer in 2 s" : "");
    }
    return true;
}

// ---- the capture mission's KEROTANs (NewOnlineItemChara2, docs/DEDICATED_HOST.md: the items at +0xB0,
// 0x120 bytes each, their count +0x410; an item's position +0x90, its holder +0xD4 (a player's work),
// the team whose goal holds it +0xF8 (0 red, 1 blue, else none); the goals' points at +0x4C0 (red) and
// +0x700 (blue); the countdown of a goal holding one +0x420, 900 frames) ----
struct ObjItem { float pos[3]; uint32_t holder; int goalTeam; };
struct Objectives { bool present = false; std::vector<ObjItem> items; float goal[2][3] = {}; };
Objectives gObjKnown;                            // the items as last known (a chaff out: no radar)
Objectives SeeObjectivesNow();
// (v141) TEAM SNEAKING (rule 5, docs/TEAM_SNEAKING.md): RESCUE's play over two targets (the GA-KO, the KEROTAN) and the
// sneakers' two goals, the sides from the game (TsneRole: 1 its team sneaks). The doll's view made RESCUE's: first the
// target it plays (the one it or an ally holds, else one an enemy holds, else the nearest free one; a defender guards
// the one of its half of the team, by its id), then the sneakers' goal nearest to that target set on one side only (the
// sneakers' goal unset: they take it there; the defenders' set: they guard it)
void TsneView(Objectives& o) {
    float pt[4][3];
    const int n = ps2world::TsnePoints(pt), role = ps2world::TsneRole();
    if (n < 3 || role < 0 || o.items.empty() || (gMyTeam != 0 && gMyTeam != 1)) return;
    auto set = [](const float* g) { return std::fabs(g[0]) + std::fabs(g[2]) > 1.f; };
    int pick = -1;
    for (size_t k = 0; k < o.items.size() && pick < 0; ++k) if (o.items[k].holder && o.items[k].holder == gMe.obj) pick = (int)k;
    for (size_t k = 0; k < o.items.size() && pick < 0; ++k)
        if (o.items[k].holder) for (const Player& p : gPlayers) if (p.obj == o.items[k].holder && Ally(p)) { pick = (int)k; break; }
    for (size_t k = 0; k < o.items.size() && pick < 0; ++k) if (o.items[k].holder) pick = (int)k;
    if (pick < 0) {
        if (role == 1) {
            float bd = 1e30f;
            for (size_t k = 0; k < o.items.size(); ++k) { const float d = Dist2D(gMe.pos, o.items[k].pos); if (d < bd) { bd = d; pick = (int)k; } }
        } else pick = o.items.size() > 1 ? (gMyId & 1) : 0;
    }
    if (pick > 0) std::swap(o.items[0], o.items[(size_t)pick]);
    const float* goal = nullptr; float gd = 1e30f;
    for (int g = 2; g < 4; ++g)
        if (set(pt[g])) { const float d = Dist2D(o.items[0].pos, pt[g]); if (d < gd) { gd = d; goal = pt[g]; } }
    if (!goal) return;
    const int mine = gMyTeam == 1 ? 1 : 0;
    float* keep = o.goal[role == 1 ? 1 - mine : mine];
    for (int g = 0; g < 2; ++g) for (int i = 0; i < 3; ++i) o.goal[g][i] = 0.f;
    for (int i = 0; i < 3; ++i) keep[i] = goal[i];
}
Objectives SeeObjectives() {                             // (once a frame: the one-life modes ask it ten times a frame)
    static long long at = -1;
    static Objectives seen;
    if (at != gFrame) { seen = SeeObjectivesNow(); at = gFrame; }
    return seen;
}
Objectives SeeObjectivesNow() {
    Objectives o;
    const uint32_t man = ps2world::ActorByName("NewOnlineItemChara2", 0);
    if (!man) return o;
    o.present = true;
    int n = (int)R32(man + 0x410);
    if (n < 1 || n > 8) n = 1;
    for (int k = 0; k < n; ++k) {
        const uint32_t it = man + 0xB0 + 0x120 * k;
        ObjItem x;
        for (int i = 0; i < 3; ++i) x.pos[i] = RF(it + 0x90 + 4 * i);
        x.holder = R32(it + 0xD4);
        x.goalTeam = R8(it + 0xF8);
        if (std::fabs(x.pos[0]) > 1e6f || std::fabs(x.pos[2]) > 1e6f) continue;
        o.items.push_back(x);
    }
    for (int g = 0; g < 2; ++g) for (int i = 0; i < 3; ++i) o.goal[g][i] = RF(man + (g ? 0x700 : 0x4C0) + 4 * i);
    // a chaff out (v134: no radar): an item where it was last known, unless the doll holds it (or held it), sees
    // it (a clear line to it within its view), or has looked at its old place from close by and it is not there
    // (then where it is, as if found)
    if (Jammed() && gObjKnown.present && gObjKnown.items.size() == o.items.size()) {
        float eye[3]; MyEye(eye);
        float view, fov; SeeSkill(&view, &fov);
        for (size_t k = 0; k < o.items.size(); ++k) {
            ObjItem& it = o.items[k];
            const ObjItem& old = gObjKnown.items[k];
            const float at[3] = {it.pos[0], it.pos[1] + 300.f, it.pos[2]}, oldAt[3] = {old.pos[0], old.pos[1] + 300.f, old.pos[2]};
            const bool mine = gMe.obj && (it.holder == gMe.obj || old.holder == gMe.obj);
            const bool seen = Dist2D(gMe.pos, it.pos) < view && LineClear(eye, at);
            const bool gone = Dist2D(gMe.pos, old.pos) < 3000.f && Dist2D(old.pos, it.pos) > 1000.f && LineClear(eye, oldAt);
            if (mine || seen || gone) continue;
            it = old;
        }
    }
    if (gRoom.rule == 5) TsneView(o);
    gObjKnown = o;
    return o;
}
bool RollFrame(const Player& p, const char* why);
// an enemy holding the objective: chased, and rolled into when the way to him is straight (the item dropped);
// a chaff out (no radar): to where he was last seen or heard these 10 s until it gets there, then where the
// item was last known (not there: where it is, SeeObjectives)
int gChaseDoneId = -1; float gChaseDone[3];      // (the place he was last known, reached)
void ChaseCarrier(const Player& p) {
    if (RollFrame(p, "the objective's carrier")) return;
    Act("chasing_carrier", p.name);
    float at[3] = {p.pos[0], p.pos[1], p.pos[2]};
    if (Jammed()) {
        const auto k = gKnown.find(p.id);
        const bool fresh = k != gKnown.end() && gFrame - k->second.last < 600;
        const bool live = fresh && (k->second.visible || gFrame - k->second.heard < 30);   // (seen or heard now: followed)
        if (live) gChaseDoneId = -1;
        else if (fresh && Dist2D(gMe.pos, k->second.pos) <= 2000.f) { gChaseDoneId = p.id; memcpy(gChaseDone, k->second.pos, sizeof gChaseDone); }
        const bool reached = !live && fresh && gChaseDoneId == p.id && Dist2D(gChaseDone, k->second.pos) < 1500.f;
        if (fresh && !reached) memcpy(at, k->second.pos, sizeof at);
        else for (const ObjItem& it : SeeObjectives().items) if (it.holder && it.holder == p.obj) { memcpy(at, it.pos, sizeof at); break; }
    }
    GoTo(at, "carrier", 1200.f);
}
bool FreeObjectiveNear(float r) {
    if (gRule < 2 || gRule > 4) return false;
    for (const ObjItem& it : SeeObjectives().items)
        if (!it.holder && Dist2D(it.pos, gMe.pos) < r && std::fabs(it.pos[1] + 800.f - gMe.pos[1]) < 2000.f) return true;
    return false;
}
bool CarrierOf(const Player& p) {
    if (gRule < 2 || gRule > 4 || !p.obj) return false;
    const Objectives o = SeeObjectives();
    for (const ObjItem& it : o.items) if (it.holder && it.holder == p.obj) return true;
    return false;
}
long long gObjLogAt = -100000;
float gSneakFirst[3]; bool gSneakFirstHave = false;   // (sneaking) where the item was at the round's start
// the item held by a player who does not take it to a goal (rescue's defenders, sneaking's soldiers):
// no button lets it go (probe 8), and the hands full, no weapon: kept far from the enemies' goal, low,
// until the doll is stunned or killed
float gKeepAt[3]; bool gKeepHave = false;
bool KeepAway(const float* enemyGoal, bool enemyGoalSet) {
    if (!gKeepHave) {
        float best[3]; float bs = -1e30f; bool any = false;
        for (int i = 0; i < 40; ++i) {
            float c[3];
            if (!RandomReachable(30000.f, c)) continue;
            float sc = enemyGoalSet ? Dist2D(c, enemyGoal) : 0.f;
            for (const Player& e : gPlayers) if (Enemy(e)) sc += std::min(Dist2D(c, e.pos), 20000.f) * 0.5f;
            const uint8_t area = gMap->nav->AreaAt(c);
            if (area == AREA_CRAWL || area == AREA_CROUCH) sc += 3000.f;
            if (sc > bs) { bs = sc; memcpy(best, c, sizeof c); any = true; }
        }
        if (!any) return false;
        memcpy(gKeepAt, best, sizeof gKeepAt); gKeepHave = true;
        if (gDebug) Log("holding the objective: keeping it at %.0f %.0f %.0f", gKeepAt[0], gKeepAt[1], gKeepAt[2]);
    }
    Act("keeping_objective");
    if (GoTo(gKeepAt, "keep", 600.f)) Steer(0, 0, 0);       // (no crouching with the hands full)
    return true;
}
// a guard near the item who does not want it: never onto it (it would be held, the weapon lost). Close
// to it: around it (on the side of the way it goes), away from it closer still. The stick pushed fully:
// a standing player does not move for a push of 0.55
void AvoidItem(const float* item) {
    KeepOff(item);                                            // (its lane too)
    const float d = Dist2D(gMe.pos, item);
    if (d > 1800.f || std::fabs(gMe.pos[1] - item[1]) > 2000.f) return;
    if (gMe.bits[0] == 2) return;                             // against a wall: the path's own way off it
    if (Following() && !PathNear(item, 1200.f)) return;       // its way does not go onto it
    const float ax = gMe.pos[0] - item[0], az = gMe.pos[2] - item[2];
    const float* g = PathGoal();
    float side = 1.f;
    if (g) { const float gx = g[0] - item[0], gz = g[2] - item[2]; side = ax * gz - az * gx > 0 ? 1.f : -1.f; }
    const float k = d < 1500.f ? 1.f : 0.35f;                 // away from it, then around it
    Steer(ax * k - az * side, az * k + ax * side, kStep);      // (v137: walked; run, it circled the item)
}
// the item taken (probe 8: walked up to it, facing it, it came at half a metre; standing under it
// or running around it takes nothing). Close to it: a walk to 0.6 m facing it, then still; not taken,
// the action button; not taken, a short step into it; then again from a step back. Each try logged
struct Take { long long since = -1; int step = 0; float item[3] = {0, 0, 0}; float best = 1e30f; long long bestAt = -1; };
Take gTake;
// (v137) the floor an item lies on: the first surface under its point (3 m at most)
float ItemFloor(const float* item) {
    if (!MapReady()) return item[1] - 1000.f;
    const float above[3] = {item[0], item[1] + 300.f, item[2]};
    const float g = gMap->nav->GroundBelow(above, 3000.f);
    return g > -1e29f ? g : item[1] - 1000.f;
}
void TakeItem(const float* item) {
    const float d = Dist2D(gMe.pos, item);
    // (v137) near on its own floor (a climb up to it or a drop down to it): the old guess (1 m under its point) took a doll
    // 2.8 m under Killhouse B's catwalk KEROTAN for near
    const float dy = gMe.pos[1] - 800.f - ItemFloor(item);
    const bool near = d < 1600.f && dy > -1300.f && dy < 1500.f;
    if (!near) {
        gTake = Take();
        // (right under or over it: GoTo would take it as reached, its height allowance 2.5 m)
        // ((v141 beta 4) once: before, planned again and a no-way entry added each frame)
        if (d < 700.f) { if (!Following() && !NoWayTo(item) && !PlanFlank(item, false) && !PlanNear(item, 3000.f)) gNoWay.push_back({{item[0], item[1], item[2]}, gFrame}); FollowPath(); }
        else GoTo(item, "objective", 250.f);
        // (v141 beta 3) an item the mesh does not reach (Pillbox Purgatory's KEROTAN on its ledge): the way to the nearest
        // point within 3 m of it (PlanNear) ends where the doll stands, planned and ended again each frame, the doll still
        // (the links bench in play: three dolls 9 minutes so, the round never over). 2 s so: out of reach, left alone 30 s
        // (the capture's guard near it, ObjectiveWayAgain)
        static long long endedSince = -1; static float endedAt[3] = {0, 0, 0};
        if (!Following() && !OnLadder()) {
            if (endedSince < 0 || Dist2D(endedAt, gMe.pos) > 500.f) { endedSince = gFrame; memcpy(endedAt, gMe.pos, sizeof endedAt); }
            else if (gFrame - endedSince > 120) {
                gNoWay.push_back({{item[0], item[1], item[2]}, gFrame});
                if (gDebug) Log("objective: the way ends %.1f m from the item, %.1f m under it: out of reach, left alone 30 s", d / 1000.f, -dy / 1000.f);
                endedSince = -1;
            }
        } else endedSince = -1;
        return;
    }
    StopPath();
    if (Dist2D(gTake.item, item) > 300.f) { gTake = Take(); memcpy(gTake.item, item, sizeof gTake.item); }
    const float want = AngleTo(gMe.pos, item), err = std::fabs(Wrap(want - Facing()));
    const long long t = gTake.since < 0 ? 0 : gFrame - gTake.since;
    switch (gTake.step) {
    case 0:                                                    // up to 0.6 m, facing it
        if (d > 650.f) {
            // held back 1.5 s (the item on a log, a crate): the action button on the way (a climb); 7 s: out
            // of reach, left alone 30 s (NoWayTo)
            if (gTake.bestAt < 0 || d < gTake.best - 40.f) { gTake.best = d; gTake.bestAt = gFrame; }
            const long long held = gFrame - gTake.bestAt;
            if (held > 90 && gFrame % 15 < 3) gPad.buttons |= 0x1000;
            if (held > 420) {
                gNoWay.push_back({{item[0], item[1], item[2]}, gFrame});
                if (gDebug) Log("objective: out of reach at %.1f m (hips %.0f, the item %.0f): left alone 30 s", d / 1000.f, gMe.pos[1], item[1]);
                gTake = Take();
                return;
            }
            Steer(item[0] - gMe.pos[0], item[2] - gMe.pos[2], d > 1000.f || held > 90 ? 1.f : kStep);
            return;
        }
        if (err > 25.f * kDeg && d > 350.f) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep); return; }
        gTake.step = 1; gTake.since = gFrame; return;
    case 1:                                                    // still a moment
        Steer(0, 0, 0);
        if (t > 30) { gTake.step = 2; gTake.since = gFrame; }
        return;
    case 2:                                                    // the action button
        if (t < 4) gPad.buttons |= 0x1000;
        if (t > 40) { gTake.step = 3; gTake.since = gFrame; if (gDebug) Log("objective: not taken still nor with the action button at %.0f m", d / 1000.f); }
        return;
    case 3:                                                    // a short step into it
        if (t < 6) Steer(item[0] - gMe.pos[0], item[2] - gMe.pos[2], 1.f);
        if (t > 40) { gTake.step = 4; gTake.since = gFrame; if (gDebug) Log("objective: not taken by a step into it (%.0f m)", Dist2D(gMe.pos, item) / 1000.f); }
        return;
    default: {                                                 // a step back, then again
        Steer(gMe.pos[0] - item[0], gMe.pos[2] - item[2], kStep);
        if (t > 20 || d > 1300.f) {
            // (v137) these steps twice round for nothing: left alone 30 s (v136's audit, cap/d2 f14706 to f15397: six rounds
            // under a KEROTAN out of reach, a step back and in again each 2.7 s)
            static float roundsAt[3] = {0, 0, 0}; static int rounds = 0; static long long roundsFrame = -100000;
            if (Dist2D(roundsAt, item) > 300.f || gFrame - roundsFrame > 1200) rounds = 0;
            memcpy(roundsAt, item, sizeof roundsAt); roundsFrame = gFrame;
            if (++rounds >= 2) {
                rounds = 0;
                gNoWay.push_back({{item[0], item[1], item[2]}, gFrame});
                if (gDebug) Log("objective: not taken in two rounds of tries at %.1f m: left alone 30 s", d / 1000.f);
            }
            gTake = Take();
        }
        return;
    }
    }
}
// (v137) capture: an item left alone as out of reach (NoWay) gone for again when a way to its floor is there now, the doll on
// a part of 20 polygons and more (a NoWay set in the air after a drop, in a pocket, was the doll's moment); once a second
bool ObjectiveWayAgain(const float* item) {
    static long long at = -1000, cleared = -100000;
    static float clearedAt[3] = {0, 0, 0};
    if (gFrame - at < 60 || !MapReady() || (gFrame - cleared < 600 && Dist2D(clearedAt, item) < 2500.f)) return false;   // (10 s apart at one place)
    at = gFrame;
    float g[3], s[3];
    if (!Ground(g) || gMap->nav->ComponentSize(gMap->nav->ComponentAt(g)) < 20) return false;
    const float fy = ItemFloor(item), q[3] = {item[0], fy, item[2]};
    std::vector<Waypoint> w;
    if (!gMap->nav->Nearest(q, 700.f, s) || std::fabs(s[1] - fy) > 600.f || !gMap->nav->Path(g, s, w) || w.size() < 2) return false;
    gNoWay.erase(std::remove_if(gNoWay.begin(), gNoWay.end(), [&](const NoWay& n) { return Dist2D(n.p, item) < 2500.f && std::fabs(n.p[1] - item[1]) < 3000.f; }), gNoWay.end());
    cleared = gFrame; memcpy(clearedAt, item, sizeof clearedAt);
    Log("objective: a way to the item at %.0f %.0f %.0f again", item[0], item[1], item[2]);
    return true;
}
// (v137) capture: the decision in the log when it changes (debug)
void CaptureSay(const char* what, const float* item) {
    static std::string last;
    if (!gDebug || last == what) return;
    last = what;
    Log("objective: %s (the item at %.0f %.0f %.0f, %.1f m)", what, item[0], item[1], item[2], Dist2D(gMe.pos, item) / 1000.f);
}
// (v137) capture: a free item out of reach watched from a place 5 to 8 m from it its way can reach (its own sector first),
// as a guard (a carrier comes down there): not every doll at its own goal (the players' Killhouse B report). Kept 20 s
float gFarSpot[3]; bool gFarHave = false; long long gFarAt = -100000;
bool GuardOutOfReach(const float* item) {
    if (!MapReady()) return false;
    if (!gFarHave || gFrame - gFarAt > 1200 || Dist2D(gFarSpot, item) > 9000.f || NoWayTo(gFarSpot)) {
        if (!gFarHave && gFrame - gFarAt < 60) return false;     // (none found: looked for again a second later)
        gFarHave = false; gFarAt = gFrame;
        float g[3];
        if (!Ground(g)) return false;
        for (int k = 0; k < 8 && !gFarHave; ++k) {
            const float a = HabitAngle() + ((k & 1) ? 1.f : -1.f) * (float)((k + 1) / 2) * 0.7853982f;
            for (float r : {5000.f, 8000.f}) {
                const float q[3] = {item[0] + std::sin(a) * r, item[1], item[2] + std::cos(a) * r};
                float s[3]; std::vector<Waypoint> w;
                if (!gMap->nav->Nearest(q, 1500.f, s) || NoWayTo(s) || !gMap->nav->Path(g, s, w) || w.size() < 2 || Dist2D(w.back().p, s) > 500.f) continue;
                memcpy(gFarSpot, s, sizeof gFarSpot); gFarHave = true;
                break;
            }
        }
        if (!gFarHave) return false;
    }
    Act("guarding_objective");
    if (GoTo(gFarSpot, "guard", 1500.f) && !GuardRest(item)) Steer(0, 0, 0);
    return true;
}
bool gCarried = false;
bool Carrying() {
    if (gRule != 3 && gRule != 2 && gRule != 4) return false;
    const Objectives o = SeeObjectives();
    bool held = false;
    for (const ObjItem& it : o.items) if (it.holder && it.holder == gMe.obj) held = true;
    if (held && !gCarried && gDebug) Log("objective: taken (the try's step %d)", gTake.step);
    gCarried = held;
    return held;
}
// ((v141 beta 10) debug: an item's holder or goal team changed (taken, dropped, delivered), for the benches' reports
void ObjectiveWatch() {
    static uint32_t holder[8] = {};
    static int goal[8] = {-2, -2, -2, -2, -2, -2, -2, -2};
    if (!gDebug || (gRule != 2 && gRule != 3 && gRule != 4)) return;
    const Objectives o = SeeObjectives();
    for (size_t k = 0; k < o.items.size() && k < 8; ++k) {
        const ObjItem& it = o.items[k];
        if (it.holder == holder[k] && it.goalTeam == goal[k]) continue;
        Log("objective %zu: holder %08x -> %08x%s, goal team %d -> %d (its team %d), at %.0f %.0f %.0f", k, holder[k], it.holder,
            it.holder && it.holder == gMe.obj ? " (it)" : holder[k] && holder[k] == gMe.obj ? " (it before)" : "", goal[k], it.goalTeam, gMyTeam,
            it.pos[0], it.pos[1], it.pos[2]);
        holder[k] = it.holder; goal[k] = it.goalTeam;
    }
}
// "defend the target" from an ally (the radio): the objective played harder a while, as the doll's side
// plays it (the takers go for it through the enemies farther than 15 m, the guards keep closer to it and
// go farther for its intruders); ended by the round's end
long long gObjFocusUntil = -1;
void ObjectiveFocus(int seconds, const std::string& by) {
    gObjFocusUntil = gFrame + seconds * 60LL;
    Log("objective: played harder %d s (%s's \"defend the target\")", seconds, by.c_str());
}
bool MasterStance();
bool ObjFocus() {
    // ((v137) also the master tier against an enemy present: capture and sneaking only, rescue's one-life rules kept)
    return (gFrame < gObjFocusUntil && (gRule == 2 || gRule == 3 || gRule == 4)) || ((gRule == 3 || gRule == 4) && MasterStance());
}
// the side that takes the objective (capture: both; rescue: the team whose goal point is not set;
// sneaking: Snake): only the takers leave the far enemies for it, the guards fight them
bool ObjTaker() {
    if (gRoom.rule == 5) return ps2world::TsneRole() == 1;   // (v141) TEAM SNEAKING: its team sneaks
    if (gRule == 3) return true;
    if (gRule == 4) return IAmSnake();
    if (gRule != 2) return false;
    const Objectives o = SeeObjectives();
    const int mine = gMyTeam == 1 ? 1 : 0;
    return o.present && std::fabs(o.goal[mine][0]) + std::fabs(o.goal[mine][2]) <= 1.f;
}

// ---- the one-life modes: no respawn within a round (rescue; the team sneaking to come: its rule number in OneLifeMode,
// its sides in ObjTaker). Both sides play safe, with as little contact as possible: nothing that gives the doll or its
// allies away ----
bool OneLifeMode() { return gRule == 2; }
int OneLifeSide() {                                      // 1 an attacker (it takes the objective), -1 a defender, 0 none
    static long long at = -1;
    static int side = 0;
    if (at == gFrame) return side;
    at = gFrame; side = 0;
    if (!OneLifeMode() || (gMyTeam != 0 && gMyTeam != 1)) return side;
    const Objectives o = SeeObjectives();
    if (o.present && !o.items.empty()) side = ObjTaker() ? 1 : -1;
    return side;
}
bool OneLifeAttacker() { return OneLifeSide() > 0; }
bool OneLifeDefender() { return OneLifeSide() < 0; }
// who has noticed the doll: an enemy it sees who faces it (50 degrees) within 40 m, aims at it, or stands within 4 m,
// with a clear line from his eye to its body (crouched, lower; in its box, a still box beyond 3 m: not him); kept 10 s
std::map<int, long long> gNoticedAt;
bool Noticed(int id, long long within) { const auto it = gNoticedAt.find(id); return it != gNoticedAt.end() && gFrame - it->second < within; }
bool SpottedLately(long long within) {                   // noticed by an enemy, or hurt (a dart too), these frames
    if (gFrame - gHurtAt < within || gFrame - gStunHitAt < within) return true;
    for (const auto& n : gNoticedAt) if (gFrame - n.second < within) return true;
    return false;
}
void NoticeWatch() {
    if (!OneLifeMode()) return;
    const float body[3] = {gMe.pos[0], GroundOf(gMe.pos) + (Low() ? 650.f : 1150.f), gMe.pos[2]};
    for (const auto& k : gKnown) {
        if (!k.second.visible) continue;
        const Player* e = PlayerById(k.first);
        if (!e || !e->alive || Asleep(*e)) continue;
        const float d = Dist2D(e->pos, gMe.pos), look = std::fabs(Wrap(AngleTo(e->pos, gMe.pos) - e->facing));
        const bool aiming = look < 20.f * kDeg && (e->bits[1] & 0x4400) != 0;
        if (!aiming && d >= 4000.f && (d >= 40000.f || look >= 50.f * kDeg || BoxOn())) continue;
        float eye[3]; BodyPoint(e->pos, 0.95f, eye);
        if (!LineClear(eye, body)) continue;
        if (gDebug && !Noticed(k.first, 600)) Log("objective: %s has noticed it (%.1f m%s)", e->name.c_str(), d / 1000.f, aiming ? ", aiming at it" : "");
        gNoticedAt[k.first] = gFrame;
    }
}
const Player* EnemyCarrier() {                           // the enemy who holds the objective's item (nullptr: none)
    if (gRule < 2 || gRule > 4) return nullptr;
    for (const ObjItem& it : SeeObjectives().items)
        if (it.holder) for (const Player& p : gPlayers) if (p.obj == it.holder && Enemy(p)) return &p;
    return nullptr;
}
// (one-life attackers) a grenade only at an enemy who has noticed it or stands within 10 m of the item (in its way)
bool OneLifeGrenadeOk(int id, const float* at) {
    if (!OneLifeAttacker() || Noticed(id, 600)) return true;
    for (const ObjItem& it : SeeObjectives().items) if (!it.holder && Dist2D(it.pos, at) < 10000.f) return true;
    return false;
}

// ---- the quiet way (one-life attackers): to the objective (to the goal when it carries) by the way the known enemies
// see least. The shortest, the map's other ways (the middle of each way found left out, as doll.cpp's MapWays, on the
// wide mesh) and a flank on each side, each scored by the points of its first 45 m (every 2.5 m) where a standing body
// is seen from the eyes of the enemies known these 30 s (the nearest three, within 35 m); 1.6 times the shortest at
// most, no more links. Clearly less seen than the shortest: gone by its middle first (the doll's own path to it), then
// on; planned again every 3 s at most, or when more enemies are known ----
struct QuietRun { float goal[3] = {0, 0, 0}, via[3] = {0, 0, 0}; bool have = false; long long at = -100000, since = -1; int eyes = 0; };
QuietRun gQw;
int QuietEyes(float eyes[3][3]) {                        // the eyes of the enemies known these 30 s, the nearest first
    struct E { float d, p[3]; };
    std::vector<E> v;
    for (const auto& k : gKnown) {
        if (gFrame - k.second.last > 1800) continue;
        const Player* e = PlayerById(k.first);
        if (!e || !e->alive || Asleep(*e)) continue;
        E x; x.d = Dist2D(k.second.pos, gMe.pos); BodyPoint(k.second.pos, 0.95f, x.p);
        v.push_back(x);
    }
    std::sort(v.begin(), v.end(), [](const E& a, const E& b) { return a.d < b.d; });
    const int n = (int)std::min<size_t>(3, v.size());
    for (int i = 0; i < n; ++i) memcpy(eyes[i], v[i].p, sizeof v[i].p);
    return n;
}
int Exposure(const std::vector<Waypoint>& w, const float eyes[3][3], int ne) {
    int n = 0;
    float walked = 0.f, next = 2500.f;
    for (size_t i = 1; i < w.size() && next <= 45000.f; ++i) {
        const float* a = w[i - 1].p; const float* b = w[i].p;
        const float seg = Dist2D(a, b);
        while (next <= walked + seg && next <= 45000.f) {
            const float t = seg > 1.f ? (next - walked) / seg : 0.f;
            const float q[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t + 1150.f, a[2] + (b[2] - a[2]) * t};
            for (int j = 0; j < ne; ++j) if (Dist2D(eyes[j], q) < 35000.f && LineClear(eyes[j], q)) { ++n; break; }
            next += 2500.f;
        }
        walked += seg;
    }
    return n;
}
float WayLength(const std::vector<Waypoint>& w) { float l = 0.f; for (size_t i = 1; i < w.size(); ++i) l += Dist2D(w[i - 1].p, w[i].p); return l; }
bool WayMid(const std::vector<Waypoint>& w, float* out) {
    float half = WayLength(w) * 0.5f;
    for (size_t i = 1; i < w.size(); ++i) {
        const float* a = w[i - 1].p; const float* b = w[i].p;
        const float l = Dist2D(a, b);
        if (l >= half && l > 1.f) { for (int k = 0; k < 3; ++k) out[k] = a[k] + (b[k] - a[k]) * half / l; return true; }
        half -= l;
    }
    return false;
}
// a way less seen than the shortest: its middle (via); the points seen on it and on the shortest
bool QuietPlan(const float* goal, float* via, int* seen, int* seen0) {
    float g[3], gg[3], eyes[3][3];
    const NavMesh& nav = gMap->wide ? *gMap->wide : *gMap->nav;
    const int ne = QuietEyes(eyes);
    if (!ne || !Ground(g) || !nav.Nearest(goal, 2500.f, gg)) return false;
    std::vector<Waypoint> w0; std::vector<unsigned> corr, avoid; float l0 = 0.f;
    if (!nav.PathAvoiding(g, gg, avoid, w0, &l0, &corr) || w0.size() < 2) return false;
    const int e0 = Exposure(w0, eyes, ne);
    *seen0 = *seen = e0;
    if (e0 == 0) return false;
    auto links = [](const std::vector<Waypoint>& w) { int n = 0; for (const Waypoint& p : w) n += p.link; return n; };
    const int links0 = links(w0);
    const float len0 = WayLength(w0);
    float best = 1e30f; bool found = false;
    auto consider = [&](const std::vector<Waypoint>& w, const float* mid) {
        const float l = WayLength(w);
        if (w.size() < 2 || l > len0 * 1.6f || links(w) > links0) return;
        const int e = Exposure(w, eyes, ne);
        const float s = e + (l - len0) / 10000.f;           // (a point seen weighs 10 m more of way)
        if (e < e0 && s < best) { best = s; *seen = e; memcpy(via, mid, 3 * sizeof(float)); found = true; }
    };
    for (int k = 0; k < 3; ++k) {                         // the map's other ways
        const size_t had = avoid.size();
        for (size_t i = 2; i + 2 < corr.size(); ++i) {
            float pc[3];
            if (nav.PolyCenter(corr[i], pc) && Dist2D(pc, g) > 8000.f && Dist2D(pc, gg) > 8000.f) avoid.push_back(corr[i]);
        }
        if (avoid.size() == had) break;
        std::vector<Waypoint> w; float l = 0.f, mid[3];
        if (!nav.PathAvoiding(g, gg, avoid, w, &l, &corr) || w.size() < 2) break;
        if (WayMid(w, mid)) consider(w, mid);
    }
    const float d = Dist2D(g, gg);                        // a flank each side: 40 to 60 % along, 30 % of the way aside (4 to 20 m)
    if (d > 1.f)
        for (const float side : {1.f, -1.f}) {
            const float ux = (gg[0] - g[0]) / d, uz = (gg[2] - g[2]) / d, along = 0.4f + 0.2f * Rnd01(), aside = std::max(4000.f, std::min(20000.f, d * 0.3f));
            const float m[3] = {g[0] + ux * d * along + uz * side * aside, (g[1] + gg[1]) * 0.5f, g[2] + uz * d * along - ux * side * aside};
            float on[3];
            std::vector<Waypoint> a, b; float la = 0.f, lb = 0.f;
            if (!nav.Nearest(m, 4000.f, on) || std::fabs(on[1] - m[1]) > 2500.f || !nav.Path(g, on, a, &la) || !nav.Path(on, gg, b, &lb) || a.size() < 2 || b.size() < 2) continue;
            a.insert(a.end(), b.begin() + 1, b.end());
            consider(a, on);
        }
    return found && (*seen * 2 <= e0 || *seen + 2 <= e0);
}
// true: by the quiet way's middle this frame
bool QuietWayFrame(const float* goal) {
    if (!MapReady() || OldWays()) return false;
    const float d = Dist2D(gMe.pos, goal);
    if (Dist2D(gQw.goal, goal) > 8000.f) {                // (another goal: planned afresh)
        if (gQw.have && gDebug) Log("objective: the quiet way over (the goal moved)");
        gQw.have = false; gQw.eyes = 0; memcpy(gQw.goal, goal, sizeof gQw.goal);
    }
    if (gQw.have) {
        const char* over = Dist2D(gMe.pos, gQw.via) < 3000.f ? "its middle reached" : gFrame - gQw.since > 1500 ? "25 s" : NoWayTo(gQw.via) ? "no way" : nullptr;
        if (over) { gQw.have = false; gQw.at = gFrame; if (gDebug) Log("objective: the quiet way over (%s)", over); }
    }
    // (planned when more enemies are known than at the last plan, or 10 s after it without a quiet way: no to and fro
    // between two ways)
    int known = 0;
    for (const auto& k : gKnown) if (gFrame - k.second.last <= 1800) ++known;
    if (d > 15000.f && known && gFrame - gQw.at > 180 && (known > gQw.eyes || (!gQw.have && gFrame - gQw.at > 600))) {
        gQw.at = gFrame; gQw.eyes = known;
        float via[3]; int seen = 0, seen0 = 0;
        const auto t0 = std::chrono::steady_clock::now();
        const bool found = QuietPlan(goal, via, &seen, &seen0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (found) {
            memcpy(gQw.via, via, sizeof via); gQw.have = true; gQw.since = gFrame;
            if (gDebug) Log("objective: a quiet way by %.0f %.0f %.0f (seen at %d of its points, the shortest at %d; %.1f ms)", via[0], via[1], via[2], seen, seen0, ms);
        } else {
            gQw.have = false;                             // (the way planned with fewer enemies known left)
            if (gDebug && seen0 > 0) Log("objective: no quieter way (the shortest seen at %d points; %.1f ms)", seen0, ms);
        }
    }
    if (!gQw.have) return false;
    GoTo(gQw.via, "quiet", 1500.f);
    return true;
}
// (one-life attackers, walking quietly) an ally within 8 m nearer its goal: still, crouched, until he is 8 m on (5 s at
// most, then not again for 10 s); not the carrier's escort
long long gSpaceSince = -1, gSpaceOff = -1;
bool SpaceFromAllies(const float* goal) {
    if (!gQuiet || gFrame < gSpaceOff) { gSpaceSince = -1; return false; }
    const float mine = Dist2D(gMe.pos, goal);
    const Player* ahead = nullptr;
    for (const Player& a : gPlayers) {
        if (a.local || !a.alive || !Ally(a) || Dist2D(a.pos, gMe.pos) >= 8000.f || std::fabs(a.pos[1] - gMe.pos[1]) > 2500.f) continue;
        const float his = Dist2D(a.pos, goal);
        if (his < mine - 500.f || (his <= mine + 500.f && a.id < gMyId)) ahead = &a;
    }
    if (!ahead) { gSpaceSince = -1; return false; }
    if (gSpaceSince < 0) { gSpaceSince = gFrame; if (gDebug) Log("objective: %s %.1f m ahead: waiting for 8 m between them", ahead->name.c_str(), Dist2D(ahead->pos, gMe.pos) / 1000.f); }
    if (gFrame - gSpaceSince > 300) { gSpaceSince = -1; gSpaceOff = gFrame + 600; return false; }
    Act("going_to_objective"); StopPath(); Steer(0, 0, 0); SetLow(true);
    return true;
}
// ---- (one-life defenders) the objective taken: every defender for the carrier at once, the nearest (its distance to
// him, 10 m more from another floor) straight at him, the others to the first point of his way to the goal they reach
// before him (every 2 m from 4 m ahead of him; at a run against his pace, a second to spare), else to the goal itself;
// his way planned again every half second or 1.5 m of his; the roll into him (ChaseCarrier's) ----
struct InterRun { int id = -1, mode = -1; long long at = -1000, modeAt = -1000; float from[3] = {0, 0, 0}, point[3] = {0, 0, 0}; };
InterRun gIc;
float PathLenTo(const float* to) {                       // its path's length to a point (a link 2 m more), -1 none
    float g[3]; std::vector<Waypoint> w; float len = 0.f;
    if (!Ground(g) || !gMap->nav->Path(g, to, w, &len) || w.size() < 2) return -1.f;
    for (const Waypoint& p : w) if (p.link) len += 2000.f;
    return len;
}
void InterceptCarrier(const Player& p, const float* goal) {
    if (RollFrame(p, "the objective's carrier")) return;
    Act("chasing_carrier", p.name);
    if (!MapReady()) { GoTo(p.pos, "intercept", 1200.f); return; }
    auto cost = [&](const float* q) { return Dist2D(q, p.pos) + (std::fabs(q[1] - p.pos[1]) > 2500.f ? 10000.f : 0.f); };
    const float mine = cost(gMe.pos);
    // (the nearest straight at him: kept until another is 2 m nearer, within 2 m the lower id; a mode kept 1 s: its
    // network positions come by steps)
    const bool wasFirst = gIc.id == p.id && gIc.mode == 0;
    int rank = 0;
    for (const Player& a : gPlayers) {
        if (a.local || !a.alive || !Ally(a)) continue;
        const float c = cost(a.pos);
        if (c < mine - 2000.f || (!wasFirst && c <= mine + 2000.f && a.id < gMyId)) ++rank;
    }
    if (gIc.id == p.id && gIc.mode >= 0 && gFrame - gIc.modeAt < 60) rank = gIc.mode == 0 ? 0 : std::max(rank, 1);
    if (rank == 0) {
        if (gIc.mode != 0 || gIc.id != p.id) { gIc.mode = 0; gIc.id = p.id; gIc.modeAt = gFrame; if (gDebug) Log("objective: %s carries it: straight at him (%.1f m)", LogWho(p).c_str(), mine / 1000.f); }
        GoTo(p.pos, "intercept", 1200.f);
        return;
    }
    if (gIc.id != p.id || gIc.mode == 0 || gFrame - gIc.at > 30 || Dist2D(gIc.from, p.pos) > 1500.f) {
        const int was = gIc.mode;
        float wasAt[3]; memcpy(wasAt, gIc.point, sizeof wasAt);
        if (gIc.id != p.id || gIc.mode == 0) gIc.modeAt = gFrame;
        gIc.id = p.id; gIc.at = gFrame; gIc.mode = 2; memcpy(gIc.from, p.pos, sizeof gIc.from); memcpy(gIc.point, goal, sizeof gIc.point);
        const float feet[3] = {p.pos[0], p.pos[1] - 700.f, p.pos[2]};
        float pc[3], gg[3], ahead = 0.f; std::vector<Waypoint> w; float len = 0.f;
        if (gMap->nav->Nearest(feet, 800.f, pc) && gMap->nav->Nearest(goal, 2500.f, gg) && gMap->nav->Path(pc, gg, w, &len) && w.size() >= 2) {
            memcpy(gIc.point, gg, sizeof gIc.point);
            const float pace = std::max(20.f, std::hypot(p.vel[0], p.vel[1]));   // (his, mm a frame; its run 55)
            float walked = 0.f, next = 4000.f;
            int tries = 0;
            for (size_t i = 1; i < w.size() && gIc.mode == 2 && tries < 6; ++i) {
                const float* a = w[i - 1].p; const float* b = w[i].p;
                const float seg = Dist2D(a, b);
                while (gIc.mode == 2 && tries < 6 && next <= walked + seg) {
                    const float t = seg > 1.f ? (next - walked) / seg : 0.f;
                    const float q[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
                    if (Dist2D(gMe.pos, q) / 55.f + 60.f < next / pace) {   // (straight it could be first: its path then)
                        ++tries;
                        const float l = PathLenTo(q);
                        if (l >= 0.f && l / 55.f + 60.f < next / pace) { memcpy(gIc.point, q, sizeof q); gIc.mode = 1; ahead = next; }
                    }
                    next += 2000.f;
                }
                walked += seg;
            }
        }
        if (gDebug && (gIc.mode != was || Dist2D(gIc.point, wasAt) > 5000.f)) {
            if (gIc.mode == 1) Log("objective: %s carries it: to %.0f %.0f %.0f on his way to the goal, %.1f m ahead of him", LogWho(p).c_str(), gIc.point[0], gIc.point[1], gIc.point[2], ahead / 1000.f);
            else Log("objective: %s carries it: to the goal", LogWho(p).c_str());
        }
    }
    if (GoTo(gIc.point, "intercept", 1000.f)) {          // there: facing him, the gun ready
        const float a = AngleTo(gMe.pos, p.pos);
        Steer(0, 0, 0);
        if (std::fabs(Wrap(a - Facing())) > 25.f * kDeg && gFrame % 6 < 2) Steer(std::sin(a), std::cos(a), kStep);
    }
}
// ---- (one-life attackers) an enemy who has not noticed the doll is not fired at beyond 5 m, unless he blocks its way
// to the objective (its path within 4 m of him, or he within 10 m of the item) and the shot gives nothing away (a stun
// weapon, its suppressor on, or a level that aims at the head: the fight opens in the first person view) ----
bool GoodShot(const Player& e) {
    const int h = Hand();
    if (SmokeBetween(e)) return false;
    return StunWeapon(h) || (SuppressorItem(h) > 0 && SuppressorOn(h)) || LevelNow() >= 3.5f;
}
bool BlocksWay(const Player& e) {
    if (Following() && PathNear(e.pos, 4000.f)) return true;
    for (const ObjItem& it : SeeObjectives().items) if (!it.holder && Dist2D(it.pos, e.pos) < 10000.f && std::fabs(it.pos[1] - e.pos[1]) < 3000.f) return true;
    return false;
}
std::map<int, long long> gHoldLogAt, gShotLogAt;
bool HoldFireAt(const Player& e) {
    const float d = Dist2D(gMe.pos, e.pos);
    // (the fight with him begun: kept, a quiet shot needs its first person view's 20 frames; the item carried: the escort fires)
    if (d < 5000.f || Noticed(e.id, 600) || gFrame - gHurtAt < 300 || gFrame - gStunHitAt < 300 || CarrierOf(e) || gFi.target == e.id) return false;
    for (const ObjItem& it : SeeObjectives().items) if (it.holder) return false;
    if (e.id == gLeaderTarget || (gSet.order == "target" && PlayerByName(gSet.target) == &e)) return false;   // (its leader's, the one ordered)
    if (BlocksWay(e) && GoodShot(e)) {
        if (gFi.target != e.id && LevelNow() >= 3.5f && !StunWeapon(Hand())) gFpvFor = e.id;
        const auto it = gShotLogAt.find(e.id);
        if (gDebug && (it == gShotLogAt.end() || gFrame - it->second > 600)) { gShotLogAt[e.id] = gFrame; Log("objective: %s has not noticed it, in its way (%.1f m): a quiet shot", e.name.c_str(), d / 1000.f); }
        return false;
    }
    const auto it = gHoldLogAt.find(e.id);
    if (gDebug && (it == gHoldLogAt.end() || gFrame - it->second > 600)) { gHoldLogAt[e.id] = gFrame; Log("objective: %s has not noticed it (%.1f m): not fired at", e.name.c_str(), d / 1000.f); }
    return true;
}
// one not fired at: crouched still while he comes closer or is within 15 m, 3 to 6 s (then not again for him for 15 s);
// else it goes on, round him (the quiet way: he is known). Each enemy's wait its own (two in turn: no endless wait)
std::map<int, long long> gHwUntil, gHwOff;
bool WaitHidden(const Player& e) {
    const auto off = gHwOff.find(e.id);
    if (off != gHwOff.end() && gFrame < off->second) return false;
    const float d = Dist2D(gMe.pos, e.pos);
    auto w = gHwUntil.find(e.id);
    if (w == gHwUntil.end()) {
        const bool coming = e.vel[0] * (gMe.pos[0] - e.pos[0]) + e.vel[1] * (gMe.pos[2] - e.pos[2]) > 10.f * d;   // (10 mm a frame toward it)
        if (!coming && d >= 15000.f) return false;
        w = gHwUntil.emplace(e.id, gFrame + 180 + (long long)(Rnd() % 180)).first;
        if (gDebug) Log("objective: %s has not noticed it (%.1f m%s): crouched still", e.name.c_str(), d / 1000.f, coming ? ", coming" : "");
    }
    if (gFrame >= w->second) {
        gHwOff[e.id] = gFrame + 900; gHwUntil.erase(w);
        if (gDebug) Log("objective: round %s (%.1f m)", e.name.c_str(), d / 1000.f);
        return false;
    }
    Act("hiding"); StopPath(); Steer(0, 0, 0); SetLow(true);
    return true;
}
// ---- (v141) BASE CAPTURE (rule 6, docs/BASE_CAPTURE.md): TEAM DEATHMATCH's fights, and 4 or 5 bases taken by standing in
// them (2.3 m about their point; more players of a team: faster). The base to go to: the nearest one not its team's (or
// its team's contested), each ally already near one counted as 15 m more (the dolls spread out), its way known; every
// base its team's: the nearest held. Inside: still, crouched with no enemy known these 5 s ----
bool BcObjective() {
    float pos[5][3]; int owner[5], contested[5];
    const int n = ps2world::BcBaseStates(pos, owner, contested);
    if (n <= 0 || (gMyTeam != 0 && gMyTeam != 1)) return false;
    const int mine = gMyTeam;
    int allies[5] = {0, 0, 0, 0, 0};
    for (const Player& p : gPlayers)
        if (!p.local && p.alive && Ally(p)) for (int i = 0; i < n; ++i) if (Dist2D(p.pos, pos[i]) < 6000.f) ++allies[i];
    int best = -1; float bd = 1e30f;
    for (int i = 0; i < n; ++i) {
        if (owner[i] == mine && !contested[i]) continue;
        if (NoWayTo(pos[i])) continue;
        const float d = Dist2D(gMe.pos, pos[i]) + allies[i] * 15000.f - (contested[i] ? 5000.f : 0.f);
        if (d < bd) { bd = d; best = i; }
    }
    const bool held = best < 0;
    if (held) for (int i = 0; i < n; ++i) { const float d = Dist2D(gMe.pos, pos[i]); if (d < bd) { bd = d; best = i; } }
    if (best < 0) return false;
    static int told = -1;
    if (gDebug && told != best * 4 + (held ? 1 : 0) + (contested[best] ? 2 : 0)) {
        told = best * 4 + (held ? 1 : 0) + (contested[best] ? 2 : 0);
        Log("objective: base %d (%s%s), %.1f m", best + 1, held ? "every base ours: held" : owner[best] == mine ? "ours, contested" : owner[best] < 0 ? "neutral" : "theirs",
            contested[best] ? ", contested" : "", Dist2D(gMe.pos, pos[best]) / 1000.f);
    }
    const float d = Dist2D(gMe.pos, pos[best]);
    if (d < 1700.f && std::fabs(gMe.pos[1] - pos[best][1]) < 2400.f) {
        Act(held ? "holding_base" : "taking_base");
        StopPath(); Steer(0, 0, 0);
        bool known = false;
        for (const auto& k : gKnown) if (gFrame - k.second.last < 300) { known = true; break; }
        SetLow(!known);
        return true;
    }
    Act(held ? "going_to_base" : "taking_base");
    GoTo(pos[best], "base", 1200.f);
    return true;
}
bool PauseSuicide();                             // ((v141 beta 9) the pause menu's suicide begun: true when it begins
bool HoldApproach(const float* c);
bool Objective() {
    if (gRoom.rule == 6) return BcObjective();
    if (gRule != 3 && gRule != 2 && gRule != 4) return false;   // capture, rescue, sneaking: the KEROTAN / GA-KO
    const Objectives o = SeeObjectives();
    if (!o.present || o.items.empty()) return false;
    int mine = gMyTeam == 1 ? 1 : 0;
    const int theirs = 1 - mine;
    if (gDebug && gFrame - gObjLogAt > 600) {
        gObjLogAt = gFrame;
        if (gDebug >= 2) {                                       // the first item's bytes, and the manager's around the goals
            const uint32_t man = ps2world::ActorByName("NewOnlineItemChara2", 0);
            char b[1400]; int n = snprintf(b, sizeof b, "objective 0 bytes:");
            for (uint32_t o = 0; o < 0x120 && n < (int)sizeof b - 12; o += 4) n += snprintf(b + n, sizeof b - n, "%s%08x", o % 32 ? " " : " | ", R32(man + 0xB0 + o));
            Log("%s", b);
            n = snprintf(b, sizeof b, "objective manager:");
            for (uint32_t o = 0x400; o < 0x4C0 && n < (int)sizeof b - 12; o += 4) n += snprintf(b + n, sizeof b - n, "%s%08x", o % 32 ? " " : " | ", R32(man + o));
            Log("%s", b);
        }
        for (size_t k = 0; k < o.items.size(); ++k)
            Log("objective %zu at %.0f %.0f %.0f holder %08x goal team %d; goals %.0f %.0f %.0f / %.0f %.0f %.0f", k, o.items[k].pos[0], o.items[k].pos[1], o.items[k].pos[2],
                o.items[k].holder, o.items[k].goalTeam, o.goal[0][0], o.goal[0][1], o.goal[0][2], o.goal[1][0], o.goal[1][1], o.goal[1][2]);
    }
    auto isSet = [](const float* g) { return std::fabs(g[0]) + std::fabs(g[2]) > 1.f; };
    if (gRule == 4) {
        // sneaking: Snake steals the item and takes it to the goal away from it (the other goal's point
        // is not set); the soldiers guard it and hunt him
        if (IAmSnake()) {
            const float* item = o.items[0].pos;
            const bool g0 = isSet(o.goal[0]), g1 = isSet(o.goal[1]);
            if (!gSneakFirstHave && !o.items[0].holder) { memcpy(gSneakFirst, item, sizeof gSneakFirst); gSneakFirstHave = true; }
            const float* from = gSneakFirstHave ? gSneakFirst : item;       // (the goal away from where the item was)
            mine = g0 && g1 ? (Dist2D(from, o.goal[0]) > Dist2D(from, o.goal[1]) ? 0 : 1) : g1 ? 1 : 0;
            if (o.items[0].holder == gMe.obj) { Act("carrying_objective"); if (GoTo(o.goal[mine], "goal", 500.f)) Steer(0, 0, 0); return true; }
            // delivered (the item in the goal, nobody holding it: the round is won in a few seconds): hidden
            if (!o.items[0].holder && isSet(o.goal[mine]) && Dist2D(item, o.goal[mine]) < 1500.f) {
                Act("objective_delivered"); StopPath(); Steer(0, 0, 0);
                if (!BoxOn()) ItemTo(0xE);                                   // (in the box, still)
                return true;
            }
            if (NoWayTo(item)) return false;                         // (out of reach a while: the fights)
            for (const Player& p : gPlayers) if (o.items[0].holder && p.obj == o.items[0].holder && Enemy(p)) { ChaseCarrier(p); return true; }
            Act("going_to_objective"); TakeItem(item); return true;
        }
    }
    // rescue: one goal point is set; the players of the other team take the GA-KO (its guards cannot:
    // measured) and carry it there, and hold it there
    if (gRule == 2) {
        const bool g0 = isSet(o.goal[0]), g1 = isSet(o.goal[1]);
        if (g0 != g1 && !isSet(o.goal[mine])) {
            const float* dest = g0 ? o.goal[0] : o.goal[1];
            const ObjItem& it = o.items[0];
            if (it.holder && it.holder == gMe.obj) {
                Act("carrying_objective");
                if (OneLifeMode() && QuietWayFrame(dest)) return true;   // (by the way its enemies see least)
                static long long at = -1;
                if (GoTo(dest, "goal", 500.f)) { Steer(0, 0, 0); if (at < 0) { at = gFrame; if (gDebug) Log("objective: in the goal"); } }
                else at = -1;
                return true;
            }
            for (const Player& p : gPlayers) {
                if (it.holder && p.obj == it.holder && Ally(p)) { Act("escorting", p.name); Follow(p, 2500.f, dest); return true; }
                if (it.holder && p.obj == it.holder && Enemy(p)) { ChaseCarrier(p); return true; }
            }
            if (!it.holder && !NoWayTo(it.pos)) {
                Act("going_to_objective");
                // (one life: 8 m from the allies on its way, by the way its enemies see least)
                if (OneLifeMode() && (SpaceFromAllies(it.pos) || QuietWayFrame(it.pos))) return true;
                TakeItem(it.pos);
                return true;
            }
            return false;
        }
    }
    const bool goalSet = isSet(o.goal[mine]);
    if (gRule == 4 || (gRule == 2 && goalSet)) {
        const ObjItem& it = o.items[0];
        // held by the doll all the same (walked onto): kept away from the enemies' goal
        if (it.holder && it.holder == gMe.obj) {
            const float* eg = isSet(o.goal[theirs]) ? o.goal[theirs] : isSet(o.goal[mine]) ? o.goal[mine] : o.goal[theirs];
            if (KeepAway(eg, isSet(eg))) return true;
        }
        gKeepHave = false;
        const bool oneLife = OneLifeDefender();
        for (const Player& p : gPlayers)
            if (it.holder && p.obj == it.holder && Enemy(p)) {
                // (one life: the nearest straight at him, the others ahead of him on his way to the goal)
                if (oneLife && !Jammed()) InterceptCarrier(p, isSet(o.goal[theirs]) ? o.goal[theirs] : isSet(o.goal[mine]) ? o.goal[mine] : o.goal[theirs]);
                else ChaseCarrier(p);
                return true;
            }
        // an enemy heard or seen a moment ago near the objective: toward it (asleep with nothing to finish him:
        // no threat, left where he lies; v134's audit, a guard stood 50 s over one it had put to sleep). One life: within
        // 10 m of it only (farther, the corner toward him held: OneLifeGuard)
        const float reach = oneLife ? 10000.f : ObjFocus() ? 30000.f : 20000.f;
        for (const auto& k : gKnown)
            if (gFrame - k.second.last < 300 && Dist2D(k.second.pos, it.pos) < reach) {
                const Player* e = PlayerById(k.first);
                if (e && Asleep(*e) && (IAmSnake() || !FinishAsleep())) continue;
                Act("chasing", e ? e->name : std::string());
                GoTo(k.second.pos, "intruder", 1500.f);
                if (!it.holder) AvoidItem(it.pos);
                return true;
            }
        Act("guarding_objective");
        const float ring = ObjFocus() ? 4500.f : 6000.f;    // (closer to it when told to defend it)
        // the way that brought it near goes onto the item: given up there, a point around it instead (v133's
        // audit: followed to its end, the guard kept off the item by AvoidItem stood "stuck" against it)
        const bool nearNow = Dist2D(gMe.pos, it.pos) <= ring + 1000.f;
        if (nearNow && PathGoal() && Dist2D(PathGoal(), it.pos) < 2500.f) StopPath();
        if (oneLife && !it.holder && OneLifeGuard(it.pos, ring)) return true;   // (one life: holds and slow rounds)
        if (!nearNow) { float sp[3]; SectorPlace(it.pos, ring * 0.8f, sp); GoTo(sp, "guard", sp[0] == it.pos[0] && sp[2] == it.pos[2] ? 3000.f : 1000.f); }   // (to its own sector)
        else if (!Following()) {
            if (GuardRest(it.pos)) { if (!it.holder) AvoidItem(it.pos); return true; }
            float g[8][3];
            const int n = GuardPoints(it.pos, ring, 2500.f, g, 8);
            for (int i = 0; i < n; ++i)                     // a point around it whose way does not cross it
                if (PlanTo(g[i])) {
                    if (!it.holder && PathNear(it.pos, 1800.f)) { StopPath(); continue; }
                    break;
                }
        } else FollowPath();
        if (!it.holder) AvoidItem(it.pos);
        return true;
    }
    gKeepHave = false;
    // carried by the doll: to its goal. ((v141 beta 9) not 1.5 m on its way in 20 s, away from its goal: its life given up by the
    // pause menu, the KEROTAN dropped where it stands (beta 8's release check: a carrier on Graniny Gorki Lab's upper floor, its
    // ways out run out, the round never over; Snake's players: "impossible kerotan location"))
    for (const ObjItem& it : o.items)
        if (it.holder && it.holder == gMe.obj) {
            Act("carrying_objective");
            static float from[3] = {0, 0, 0};
            static long long since = -1, lastAt = -100;
            if (gFrame - lastAt > 30 || since < 0 || Dist2D(gMe.pos, from) > 1500.f || std::fabs(gMe.pos[1] - from[1]) > 1500.f) { since = gFrame; memcpy(from, gMe.pos, sizeof from); }
            lastAt = gFrame;
            if (gFrame - since > 1200 && Dist2D(gMe.pos, o.goal[mine]) > 3000.f && gRoom.phase == 2 && PauseSuicide()) {
                Log("objective: carried, not 1.5 m on its way in 20 s at %.0f %.0f %.0f (its goal %.1f m away): its life given up to drop it",
                    gMe.pos[0], gMe.pos[1], gMe.pos[2], Dist2D(gMe.pos, o.goal[mine]) / 1000.f);
                gNoWay.push_back({{gMe.pos[0], gMe.pos[1] - 800.f, gMe.pos[2]}, gFrame});   // (not taken again from there 30 s)
                since = gFrame;
                return true;
            }
            static const bool stillTest = getenv("MGO1_DOLL_TEST_CARRIER_STILL") != nullptr;   // (tests: a carrier that does not move)
            if (stillTest) { StopPath(); Steer(0, 0, 0); return true; }
            if (GoTo(o.goal[mine], "goal", 500.f)) Steer(0, 0, 0);
            return true;
        }
    // the one to go for: a free one (not in our goal), else one in their goal, else an enemy carrier; ours
    // all in our goal: defend it
    const ObjItem* best = nullptr; float bd = 1e30f; const Player* carrier = nullptr; const Player* allyCarrier = nullptr;
    const ObjItem* far = nullptr;                            // (v137) a free one out of reach a while
    // (v137) the round over (phase 4, 5): the item that won it stays in the goal, its goal team reset (255); not taken
    // there (98 of the 98 give-ups "not taken in two rounds" of motion137's capture bench came in phase 4)
    const bool over = gRoom.phase >= 0 && gRoom.phase != 2;
    for (const ObjItem& it : o.items) {
        if (it.holder) {
            for (const Player& p : gPlayers) if (p.obj == it.holder) { if (Enemy(p)) carrier = &p; else if (Ally(p)) allyCarrier = &p; }
            continue;
        }
        if (it.goalTeam == mine || over) continue;           // ours already
        if (NoWayTo(it.pos) && !ObjectiveWayAgain(it.pos)) { if (!far) far = &it; continue; }   // (out of reach a while)
        const float d = Dist2D(gMe.pos, it.pos) + (it.goalTeam == theirs ? 8000.f : 0.f);
        if (d < bd) { bd = d; best = &it; }
    }
    CaptureSay(carrier ? "chasing its carrier" : best ? "going for it" : allyCarrier ? "escorting its carrier" : far ? "out of reach: guarding near it" : "defending the goal",
               best ? best->pos : far ? far->pos : o.items[0].pos);
    // ((v141 beta 10) its carrier cut off on his way to his goal, the nearest straight at him (InterceptCarrier), not all behind him)
    if (carrier) { if (!CheatWaitsB8() && !Jammed() && isSet(o.goal[theirs])) InterceptCarrier(*carrier, o.goal[theirs]); else ChaseCarrier(*carrier); return true; }
    if (best) { Act("going_to_objective"); TakeItem(best->pos); return true; }
    if (allyCarrier) { Act("escorting", allyCarrier->name); Follow(*allyCarrier, 2500.f, o.goal[mine]); return true; }
    if (far && GuardOutOfReach(far->pos)) return true;      // (v137: not every doll at its own goal)
    // every one in our goal: guard it. ((v141 beta 10) an enemy known these 5 s within 25 m of it: at him (the defenders stood 38 to 93 % of
    // the time); tier 10 no pause at its points)
    if (!CheatWaitsB8()) {
        const Player* in = nullptr; float inAt[3] = {0, 0, 0}, ind = 25000.f;
        for (const auto& k : gKnown) {
            if (gFrame - k.second.last >= 300) continue;
            const Player* e = PlayerById(k.first);
            if (!e || !Enemy(*e) || !e->alive || (Asleep(*e) && (IAmSnake() || !FinishAsleep())) || NoWayTo(k.second.pos)) continue;
            const float dg = Dist2D(k.second.pos, o.goal[mine]);
            if (dg < ind) { ind = dg; in = e; memcpy(inAt, k.second.pos, sizeof inAt); }
        }
        if (in) { Act("chasing", LogWho(*in)); GoTo(inAt, "intruder", 1500.f); return true; }
    }
    Act("defending_goal");
    if (CheatOn() && !CheatWaitsB8() && HoldApproach(o.goal[mine])) return true;
    if (Dist2D(gMe.pos, o.goal[mine]) > 6000.f) { float sp[3]; SectorPlace(o.goal[mine], 3500.f, sp); GoTo(sp, "guard", 1500.f); }   // (to its own sector)
    else if (!Following()) { float g[1][3]; if (!GuardRest(o.goal[mine]) && GuardPoints(o.goal[mine], 5000.f, 0.f, g, 1)) PlanTo(g[0]); }
    else FollowPath();
    return true;
}
// ((v141 beta 10) tier 10 defending its goal c: 4 m out of it toward the nearest enemy, facing his way (not a round of points about it)
bool HoldApproach(const float* c) {
    // (its enemy kept unless another is 3 m nearer the goal, its place kept 1 s; one within 2 m of the goal is no way to face)
    static int forId = -1; static long long at = -100; static float s[3] = {0, 0, 0};
    const Player* n = nullptr; float nd = 1e30f, keepD = 1e30f;
    for (const Player& e : gPlayers) {
        if (!Enemy(e) || !e.alive) continue;
        const float d = Dist2D(e.pos, c);
        if (d < 2000.f) continue;
        if (e.id == forId) keepD = d;
        if (d < nd) { nd = d; n = &e; }
    }
    if (!n || !MapReady()) { forId = -1; return false; }
    if (keepD < 1e29f && n->id != forId && nd > keepD - 3000.f) n = PlayerById(forId);
    if (!n) { forId = -1; return false; }
    if (n->id != forId || gFrame - at >= 60) {
        const float a = AngleTo(c, n->pos), q[3] = {c[0] + std::sin(a) * 4000.f, c[1], c[2] + std::cos(a) * 4000.f};
        float sn[3];
        if (!gMap->nav->Nearest(q, 2000.f, sn) || Dist2D(sn, c) > 6000.f || NoWayTo(sn)) { forId = -1; return false; }
        if (n->id != forId || Dist2D(sn, s) > 1500.f) memcpy(s, sn, sizeof s);
        forId = n->id; at = gFrame;
    }
    if (!GoTo(s, "guard", 800.f)) return true;
    const float want = AngleTo(gMe.pos, n->pos);
    if (std::fabs(Wrap(want - Facing())) > 20.f * kDeg && gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), kStep);
    else Steer(0, 0, 0);
    return true;
}

// ---- the menus of a match: spawning, the pause menu (NewHudPause, act 0x3CD768 in n011a: its state
// +0x326, 6 open, 4 the weapons; its page +0x328: 1 the list, 8 TEAM SELECT, 3 a question, 0xB closing;
// its cursor +0x32A: on the list 0 MUTE, 1 SUICIDE, 2 HOST (the host's only: skipped), 3 TEAM SELECT,
// 4 RULE, 5 ADD LIST, 6 OPTIONS, 7 QUIT (a spectator skips SUICIDE too); on TEAM SELECT, in a team 0 TEAM
// CHANGE, 1 SPECTATOR, a spectator 0 RED TEAM, 1 BLUE TEAM; the question's answer +0x32C: 0 YES, 1 NO
// (the default; left / right). YES on TEAM SELECT sends the game's 0x751012 {the choice}; going
// spectator costs the player's life (a death). The menus read the trigger bits of 0x2C5E7C (up 0x10,
// down 0x40, left 0x80, right 0x20; the d-pad counts only with a pressure: the harness gives the doll's
// held buttons a full one) ----
long long gOutSince = -1, gCircleAt = -100000;
bool gSpectating = false;
bool WantSpectate() {
    if (gSet.team == "spectator") return true;
    static const bool alone = getenv("MGO1_DOLL_PLAY_ALONE") != nullptr;   // (tests: the dolls' audits, dolls only)
    return RealPlayers() == 0 && !gProbe && !alone;       // no real player: the dolls do not play among themselves
}
// (v150-host beta 14) a side the game does not let go: sneaking's Snake (spawned again as Snake until a round with players:
// TEAM SELECT's SPECTATOR only killed him; Snake's report of 27 October, a doll dead 2338 times in a game with nobody real),
// or three spectator jobs this round that left the doll in its side: it waits there, still
int gSpecTries = 0;
bool gSpecBarredSaid = false;
bool SpectatorBarred() { return IAmSnake() || gSpecTries >= 3; }
enum PauseJobKind { PJ_NONE, PJ_SPECTATOR, PJ_TEAM_CHANGE, PJ_SUICIDE, PJ_JOIN };
struct PauseJob { int kind = PJ_NONE; long long start = 0, pressAt = -100000; uint32_t press = 0; int presses = 0, rowPresses = 0, lastRow = -1; bool confirmed = false; };
PauseJob gPJ;
long long gPauseDoneAt = -100000;
const char* PauseJobName(int k) { return k == PJ_SPECTATOR ? "spectator" : k == PJ_TEAM_CHANGE ? "team_change" : k == PJ_SUICIDE ? "suicide" : k == PJ_JOIN ? "join" : "-"; }
bool PauseJobStart(int kind, int gap = 600) {
    if (gPJ.kind != PJ_NONE || gFrame - gPauseDoneAt < gap) return false;
    gPJ = PauseJob(); gPJ.kind = kind; gPJ.start = gFrame;
    Log("pause menu: %s", PauseJobName(kind));
    return true;
}
bool PauseSuicide() { return gPJ.kind == PJ_NONE && PauseJobStart(PJ_SUICIDE, 0); }
// the team a spectator joins: the one ordered, else the one with fewer players (0 red, 1 blue). ((v141 beta 13) the dolls
// still out of a team, this one too, take their turns in their slots' order, each to the side with fewer players then, red
// on a tie: two dolls that join at once take one side each. A draw on a tie had sent both dolls of Snake's capture game of
// 20 October to red: his arrival, put in blue by the team auto balance, restarted the round and froze him at his spawn)
int JoinTeam() {
    if (gSet.team == "red") return 0;
    if (gSet.team == "blue") return 1;
    int n[2] = {0, 0};                                  // ((v141 beta 6) the players in the session only: a slot keeps its last player's team)
    std::vector<int> out;                               // the dolls out of a team, in slot order
    for (int id = 0; id < 16; ++id) {
        if (id == gMyId) { out.push_back(id); continue; }
        int t = 0xFF; std::string nm;
        if (!ps2world::PlayerInfo(id, &t, &nm) || !InSession(id)) continue;
        if (t == 0 || t == 1) ++n[t];
        else if (IsDollUid(ps2world::PlayerUserId(id)) || IsDollName(nm)) out.push_back(id);
    }
    if (gMyId < 0) return n[1] < n[0] ? 1 : 0;
    for (int id : out) {
        const int side = n[1] < n[0] ? 1 : 0;
        if (id == gMyId) return side;
        ++n[side];
    }
    return 0;
}
// (v141 beta 13) the round's restart: the game plays its round again when a side gets its first player while the other has
// some (deathmatch: the second player). A real player whose own arrival did it (the team auto balance put Snake in the empty
// blue side 20 ms after his load, 20 October) stayed frozen at his spawn, out of reach, for the rest of the game. The dolls
// leave that restart to a moment the real players are not arriving: while they still load (two dolls or more, one side
// each: JoinTeam), or once they play (one doll: after their arrival), never within 10 s of a real player's team known.
// The reason, or none: it joins
const char* JoinHold() {
    static const bool alone = getenv("MGO1_DOLL_PLAY_ALONE") != nullptr;
    if (gProbe || alone) return nullptr;
    const long long at = RealTeamKnownAt();
    if (at >= 0 && gFrame - at < 600) return "a real player arriving";
    if (DollsInSession() < 2 && RealLoading() > 0) return "a real player loading (one doll)";
    return nullptr;
}
// deathmatch: everyone in team 0, the unit (the uniform) the row a spectator takes on TEAM SELECT (0x751012
// {the row}: 0 GRU, 1 KGB, 2 the Ocelot unit, the settings' numbering); a player who joins is put in GRU. (v150-host
// beta 16) The soldier's type +0xB62 numbers them otherwise: 0 KGB, 1 GRU, 2 the Ocelot unit (ClassUnit).
// Each doll draws one of the three once and takes it at its first spawn, away from the enemies
// (spectator, then TEAM SELECT on its row: as a player does; the pause menu does not open while dead)
int gDmUnit = -1, gDmTries = 0;
const char* UnitName(int u) { return u == 0 ? "GRU" : u == 1 ? "KGB" : u == 2 ? "Ocelot unit" : "?"; }
int ClassUnit(int type) { return type == 0 ? 1 : type == 1 ? 0 : type == 2 ? 2 : -1; }   // (a soldier's type to its unit)
int DmUnit() {
    if (gDmUnit < 0) {
        const char* e = getenv("MGO1_DOLL_UNIT");                 // (tests: 0 to 2)
        gDmUnit = e && *e >= '0' && *e <= '2' ? *e - '0' : (int)(Rnd() % 3);
        Log("deathmatch: the doll's unit %s", UnitName(gDmUnit));
    }
    return gDmUnit;
}
int gDmUnitSeen = -1;
void DmUnitLog() {                                      // (the unit worn, when it changes)
    const int t = MyType();
    if (gRule != 0 || t < 0 || t == gDmUnitSeen) return;
    gDmUnitSeen = t;
    Log("deathmatch: wearing %s%s", UnitName(ClassUnit(t)), gDmUnit >= 0 && ClassUnit(t) == gDmUnit ? " (its unit)" : "");
}
// (v141 beta 3) off unless MGO1_DOLL_DM_UNIT=1: the change goes through the spectators, a death at the game's start (the
// user's "suicide at the spawn")
bool DmUnitWanted() {                                   // its unit not taken yet (the soldiers' types only)
    static const bool on = getenv("MGO1_DOLL_DM_UNIT") && *getenv("MGO1_DOLL_DM_UNIT") == '1';
    if (!on) return false;
    const int t = MyType();
    return gRule == 0 && gMe.alive && gDmTries < 2 && t >= 0 && t <= 2 && ClassUnit(t) != DmUnit();
}
// the weapons page (NewHudPause state 4, shown when joining a team and at each spawn, with a countdown
// that takes what is there): its rows +0x328 (1 primary, 2 secondary, 3 support, 4 the fourth, 5 OK:
// the row it opens on), up / down between the rows, left / right change the row's item, Circle or START
// on OK (START on any row) takes them. The primary +0x33A of +0x338 (SCORPION, XM16E1, AK-47, M37, SVD,
// MOSIN NAGANT), the secondary +0x33E of +0x33C (M1911A1, Mk22), the support +0x342. The doll's loadout:
// MGO1_DOLL_INI's loadout (primary[,secondary]: scorpion xm16 ak47 m37 svd mosin sniper / m1911 mk22),
// else by its number and difficulty (docs/PLAYABLE_DOLL.md)
struct Loadout { int primary = -1, secondary = -1, support = -1, fourth = -1; };
// the support row when the loadout does not say (v134): the grenade at level 1, else one drawn at each spawn
// (the grenade 40 %, WP 8, the stun grenade 18, the chaff 8, the smoke 12, the claymore 8, the book 6; no
// claymore in a headshots-only game)
int AutoSupportDraw() {
    if (gSet.difficulty <= 1) return 0;
    static const int w[7] = {40, 8, 18, 8, 12, 8, 6};
    int r = (int)(Rnd() % 100);
    for (int i = 0; i < 7; ++i) {
        if (r < w[i]) return i == 5 && HeadshotsOnly() ? 0 : i;
        r -= w[i];
    }
    return 0;
}
// (v141 beta 6) drawn once a map (beta 5 drew it at each spawn: the weapons page gone through again each time, 2.5 s of presses)
int AutoSupport() {
    static std::string forMap;
    static int drawn = 0;
    const std::string key = gMap ? gMap->key : std::string();
    if (key.empty() || key != forMap) { forMap = key; drawn = AutoSupportDraw(); }
    return drawn;
}
// ---- (v141 beta 6) the auto loadout (the doll's loadout "auto" or empty): its weapons for the map, the rule and its side, chosen at
// the map's first spawn and kept for the map (the later weapons pages take it at once). The map's kind by its sightlines
// (MapProfile); the doll's role by its rank among its side's dolls (their names in order: each doll of a side finds the same
// ranks; the names not known, its number), at most one sniper in a side of 4 or fewer and two above on an open map, one on a
// mixed map from 3 players, none on a close one, none for the one-life attackers (they move); the tier: no sniper under HARD,
// the simple guns at TRAINING and EASY. The rule's support: claymores for the one-life defenders, smoke or stun grenades for the
// attackers and the capture's runners, stun grenades in close quarters from NORMAL. The server's loadout (the ini's loadout=,
// the order "loadout") always wins. MGO1_DOLL_AUTO_LOADOUT=0: beta 5's (a role by its number) ----
const char* const kPrimNames[6] = {"SCORPION", "XM16E1", "AK47", "M37", "SVD", "MOSIN"};
const char* const kSecNames[2] = {"M1911A1", "MK22"};
const char* const kSupNames[7] = {"GRENADE", "WP", "STUN", "CHAFF", "SMOKE", "CLAYMORE", "BOOK"};
const char* RuleWord(int r) { static const char* const n[] = {"DM", "TDM", "RESCUE", "CAPTURE", "SNEAKING", "TEAM_SNEAKING", "BASE_CAPTURE"}; return r >= 0 && r <= 6 ? n[r] : "UNKNOWN"; }
struct SidePlace { int rank = 0, dolls = 1, size = 1; bool byName = false; };
SidePlace MySidePlace() {                                // its rank among its side's dolls (DM: every player), the side's players
    SidePlace s;
    std::vector<std::string> dolls;
    const std::string me = MyName();
    for (const Player& p : gPlayers) {
        if (p.local || p.id == gMyId || p.team == 0xFE || p.name.empty() || !(gRule == 0 || Ally(p))) continue;
        ++s.size;
        if (IsDollName(p.name)) dolls.push_back(p.name);
    }
    if (IsDollName(me)) {
        dolls.push_back(me);
        std::sort(dolls.begin(), dolls.end());
        s.rank = (int)(std::find(dolls.begin(), dolls.end(), me) - dolls.begin());
        s.dolls = (int)dolls.size(); s.byName = true;
    } else {
        s.rank = gSet.number > 0 ? gSet.number - 1 : (int)(Rnd() % 4);
        s.dolls = std::max(s.size, s.rank + 1);
    }
    return s;
}
// what killed it on a map: its killer's distance at each death (MasterFrame); the next map's auto loadout answers the last map's
// (3 deaths and more, 60 % of them from 25 m and farther: a counter-sniper; within 10 m: the close guns)
struct DeathTally { std::string map; int far = 0, close = 0, all = 0; };
DeathTally gDeathsNow, gDeathsLast;
void LoadoutDeathNote(float dist) {
    const std::string key = gMap ? gMap->key : std::string();
    if (key != gDeathsNow.map) { if (gDeathsNow.all) gDeathsLast = gDeathsNow; gDeathsNow = DeathTally(); gDeathsNow.map = key; }
    ++gDeathsNow.all;
    if (dist >= 25000.f) ++gDeathsNow.far; else if (dist >= 0.f && dist < 10000.f) ++gDeathsNow.close;
}
struct AutoPick { Loadout l; const char* role = "rifleman"; std::string why; };
AutoPick AutoLoadoutPick() {
    AutoPick a;
    const MapProfile& mp = MapProf();
    const int kind = mp.kind, rule = gRoom.rule >= 0 && gRoom.rule <= 6 ? gRoom.rule : gRule, lv = (int)std::lround(EffLevel());
    const SidePlace sp = MySidePlace();
    const bool oneLife = rule == 2 || rule == 5, attacker = oneLife && ObjTaker(), defender = oneLife && !attacker;
    // the last map's deaths (another map's tally)
    const std::string key = gMap ? gMap->key : std::string();
    const DeathTally& dt = gDeathsNow.map != key ? gDeathsNow : gDeathsLast;
    const bool counterSniper = dt.all >= 3 && dt.far * 10 >= dt.all * 6 && lv >= 4, counterRush = dt.all >= 3 && dt.close * 10 >= dt.all * 6 && lv >= 3;
    // the side's snipers
    int snipers = 0;
    if (lv >= 4 && !attacker && rule != 4) snipers = kind == MAP_OPEN ? (sp.size > 4 ? 2 : 1) : kind == MAP_MIXED && sp.size >= 3 ? 1 : 0;
    bool sniper = (kind == MAP_OPEN && ((sp.rank == 0 && snipers >= 1) || (sp.rank == 2 && snipers >= 2))) || (kind == MAP_MIXED && sp.rank == 1 && snipers >= 1);
    if (counterSniper && kind != MAP_CLOSE && !attacker && rule != 4) sniper = true;   // (killed from afar: a counter-sniper)
    const int j = sniper ? 0 : sp.rank - (kind == MAP_OPEN && sp.rank > 0 && snipers >= 1 ? 1 : 0) - (kind == MAP_MIXED && sp.rank > 1 && snipers >= 1 ? 1 : 0);
    char why[240];
    if (sniper) { a.l.primary = 4; a.role = "sniper"; }
    else if (lv <= 2) { a.l.primary = j % 2 ? 1 : 2; a.role = "rifleman"; }      // (the simple guns: AK-47, XM16E1)
    else if (kind == MAP_CLOSE) {                         // (close quarters: the rifle, the submachine gun, the shotgun)
        static const int k[4] = {2, 0, 3, 1};
        a.l.primary = k[j % 4]; if (a.l.primary == 3 && lv < 4) a.l.primary = 2;
        a.role = a.l.primary == 0 ? "assault" : a.l.primary == 3 ? "breacher" : "rifleman";
    } else if (kind == MAP_MIXED) {
        static const int k[3] = {2, 1, 0};
        a.l.primary = k[j % 3]; a.role = a.l.primary == 0 ? "assault" : "rifleman";
    } else { a.l.primary = j % 2 ? 1 : 2; a.role = "rifleman"; }
    if (defender && !sniper && kind != MAP_OPEN && sp.rank == 0 && lv >= 4) { a.l.primary = 3; a.role = "breacher"; }   // (a room held: the shotgun)
    if (attacker && !sniper && lv >= 3 && a.l.primary == 2 && sp.rank % 2) a.l.primary = 1;   // (one-life attackers: the suppressed XM16E1 too)
    if (counterRush && !sniper && kind != MAP_OPEN) { a.l.primary = lv >= 4 ? 3 : 0; a.role = lv >= 4 ? "breacher" : "assault"; }   // (killed close by)
    // ((v141 beta 7) tier 10's cheat: the AK-47 and the M1911A1 on every map (on Mountaintop the AK-47 took 6 kills for 1 death against
    // two SVD snipers))
    const bool cheatTier = CheatAllowed() && EffLevel() >= 9.75f;
    if (cheatTier) { sniper = false; a.l.primary = 2; a.role = "rifleman"; }
    // the secondary: a sniper's lethal handgun close by; the Mk22's darts from NORMAL (quiet stuns)
    a.l.secondary = sniper || lv <= 2 || cheatTier ? 0 : 1;
    // the support row
    if (lv <= 1) a.l.support = 0;
    else if (defender) a.l.support = HeadshotsOnly() ? 0 : 5;
    else if (attacker || rule == 3) a.l.support = sp.rank % 2 ? 2 : 4;
    else if (sniper) a.l.support = HeadshotsOnly() || lv < 4 ? 0 : 5;
    else if (kind == MAP_CLOSE && lv >= 3) a.l.support = sp.rank % 2 ? 0 : 2;
    else if (lv >= 5) a.l.support = sp.rank % 2 ? 2 : 0;
    else a.l.support = 0;
    if (counterRush && !defender && lv >= 3) a.l.support = 2;                                          // (the stun grenade at the rushers)
    if (counterSniper && !sniper && !defender) a.l.support = 4;                                        // (a smoke across the sniper's lines)
    char dw[64] = "";
    if (counterSniper || counterRush) snprintf(dw, sizeof dw, "+%s_%d_of_%d_deaths", counterSniper ? "counter_sniper" : "counter_rush", counterSniper ? dt.far : dt.close, dt.all);
    snprintf(why, sizeof why, "%s_map_%dpct_views+%s+rank_%d_of_%d_dolls%s+side_%d+%s%s%s%s", kMapKindNames[kind], mp.views, oneLife ? (attacker ? "attacker" : "defender") : "free",
             sp.rank, sp.dolls, sp.byName ? "" : "_by_number", sp.size, TierName((float)lv), sniper ? "+side_sniper" : snipers ? "+sniper_slot_taken" : "+no_sniper_here", dw,
             cheatTier ? "+tier10_ak47_m1911" : "");
    a.why = why;
    return a;
}
Loadout WantLoadout() {
    Loadout l;
    static const char* prim[] = {"scorpion", "xm16", "ak47", "m37", "svd", "mosin"};
    static const char* sec[] = {"m1911", "mk22"};
    std::string v = gSet.loadout;
    for (char& c : v) c = (char)tolower((unsigned char)c);
    static const bool autoOld = getenv("MGO1_DOLL_AUTO_LOADOUT") && *getenv("MGO1_DOLL_AUTO_LOADOUT") == '0';
    if ((v.empty() || v == "auto") && !autoOld) {        // ((v141 beta 6) chosen once a map: its line at the map's first spawn)
        static std::string forKey;
        static Loadout kept;
        const std::string key = (gMap ? gMap->key : std::string("?")) + "/" + std::to_string(gRoom.rule);
        if (key != forKey) {
            const AutoPick a = AutoLoadoutPick();
            kept = a.l; forKey = key;
            Log("DOLL LOADOUT: map=%s rule=%s role=%s primary=%s secondary=%s support=%s reason=%s", MapProf().name, RuleWord(gRoom.rule >= 0 ? gRoom.rule : gRule),
                a.role, kPrimNames[kept.primary], kSecNames[kept.secondary], kept.support >= 0 && kept.support < 7 ? kSupNames[kept.support] : "NONE", a.why.c_str());
        }
        return kept;
    }
    if (!v.empty() && v != "auto") {
        for (int i = 0; i < 6; ++i) if (v.find(prim[i]) != std::string::npos) l.primary = i;
        for (int i = 0; i < 2; ++i) if (v.find(sec[i]) != std::string::npos) l.secondary = i;
        if (v.find("sniper") != std::string::npos) l.primary = 4;
        const size_t sp = v.find(",s");                          // ",s<n>": the support row's n-th item
        if (sp != std::string::npos && sp + 2 < v.size() && isdigit((unsigned char)v[sp + 2])) l.support = v[sp + 2] - '0';
        const size_t cp = v.find(",c");                          // ",c<n>": the fourth row's n-th item
        if (cp != std::string::npos && cp + 2 < v.size() && isdigit((unsigned char)v[cp + 2])) l.fourth = v[cp + 2] - '0';
        if (l.support < 0) l.support = AutoSupport();
        return l;
    }
    static const int roles[4] = {2, 4, 1, 3};                  // AK-47, SVD (a sniper), XM16E1, M37
    l.primary = roles[(gSet.number > 0 ? gSet.number - 1 : (int)(Rnd() % 4)) % 4];
    if (gSet.difficulty <= 1 && l.primary == 4) l.primary = 2;  // a beginner does not snipe
    l.secondary = gSet.difficulty >= 3 ? 1 : 0;                 // the Mk22 (stuns) from 3 on
    l.support = AutoSupport();
    return l;
}
// ---- (v141 beta 6) the respawn: its phases timed (the death, the game's wait, the weapons page, its OK, the deploy) and the doll's
// own part kept small. Measured (the deploy bench's logs): the game spawns the player by itself after its own wait (Circle taps
// changed nothing: a spawn 81 frames after the last one); then the weapons page, where beta 5 went through every row again at each
// spawn (its support drawn anew: 10 to 12 presses of 14 frames, 2.5 s), then the game's deploy. Now the page's OK as soon as the
// loadout is there (kept for the map: nothing to change after the first spawn), after the tier's reaction (TRAINING 0.7 to 1.5 s ..
// PRO 0.08 to 0.3 s); a page shown 3 s without its OK: START (the watchdog). Never the game's own wait shortened ----
enum RespawnState { RS_ALIVE, RS_DEAD_WAITING_GAME, RS_RESPAWN_AVAILABLE, RS_REQUESTING_RESPAWN, RS_WAITING_SPAWN_ACK, RS_RECOVERY };
const char* const kRespawnNames[] = {"ALIVE", "DEAD_WAITING_GAME", "RESPAWN_AVAILABLE", "REQUESTING_RESPAWN", "WAITING_SPAWN_ACK", "RECOVERY"};
struct RespawnRun {
    int state = RS_ALIVE, attempts = 0;
    long long deadAt = -1, spawnAt = -1, pageAt = -1, okAt = -1, readyAt = -1;
    long long legal = -1, ai = -1, total = -1;           // (the last respawn: the game's wait, the doll's own, all of it; frames)
    int respawns = 0;
};
RespawnRun gRs;
// the reaction after the page shows, by tier (ms): a draw in the band
int RespawnReactFrames() {
    if (CheatOn()) return 3;                         // ((v141 beta 7) tier 10's cheat: at once)
    static const float band[10][2] = {{700, 1500}, {700, 1500}, {400, 900}, {250, 600}, {250, 600}, {120, 400}, {120, 400}, {100, 350}, {90, 320}, {80, 300}};
    const float lv = std::max(1.f, std::min(10.f, LevelNow()));
    const int a = std::max(0, std::min(9, (int)std::floor(lv) - 1)), b = std::min(9, a + 1);
    const float t = lv - 1.f - a, lo = band[a][0] + (band[b][0] - band[a][0]) * t, hi = band[a][1] + (band[b][1] - band[a][1]) * t;
    return std::max(3, (int)std::lround((lo + (hi - lo) * Rnd01()) * 60.f / 1000.f));
}
void RespawnSay(const char* what) {
    Log("respawn: %s (dead %lld ms: the game's wait %lld ms, the doll's own %lld ms, its deploy %lld ms; %d attempt%s)", what,
        gRs.total * 1000 / 60, gRs.legal * 1000 / 60, gRs.ai * 1000 / 60, gRs.readyAt >= 0 && gRs.okAt >= 0 ? (gRs.readyAt - gRs.okAt) * 1000 / 60 : -1LL,
        gRs.attempts, gRs.attempts == 1 ? "" : "s");
}
// the menu's frames: dead, spawned (the weapons page), frozen (the deploy's wait)
void RespawnMenuWatch() {
    const bool dead = gMe.obj && !gMe.alive;
    if (dead && (gRs.state == RS_ALIVE || gRs.state == RS_WAITING_SPAWN_ACK)) {
        gRs = RespawnRun{RS_DEAD_WAITING_GAME, 0, gFrame, -1, -1, -1, -1, gRs.legal, gRs.ai, gRs.total, gRs.respawns};
        if (gDebug) Log("respawn: dead, the game's wait");
    }
    if (gMe.alive && gRs.state == RS_DEAD_WAITING_GAME) { gRs.spawnAt = gFrame; gRs.state = RS_RESPAWN_AVAILABLE; if (gDebug) Log("respawn: spawned by the game after %lld ms", (gFrame - gRs.deadAt) * 1000 / 60); }
}
// the brain's first free frame: the deploy done
void RespawnBrainWatch() {
    if (gRs.state == RS_ALIVE || gRs.deadAt < 0) { gRs.state = RS_ALIVE; return; }
    gRs.readyAt = gFrame;
    gRs.total = gFrame - gRs.deadAt;
    gRs.legal = (gRs.pageAt >= 0 ? gRs.pageAt : gRs.spawnAt >= 0 ? gRs.spawnAt : gFrame) - gRs.deadAt;
    gRs.ai = gRs.okAt >= 0 && gRs.pageAt >= 0 ? gRs.okAt - gRs.pageAt : 0;
    ++gRs.respawns;
    gRs.state = RS_ALIVE;
    if (gDebug || gRs.respawns <= 3 || gRs.respawns % 10 == 0) RespawnSay("alive");
}
std::string RespawnJsonNow() {
    char b[320];
    const long long deadFor = gRs.state != RS_ALIVE && gRs.deadAt >= 0 ? gFrame - gRs.deadAt : 0;
    snprintf(b, sizeof b, "{\"alive\":%s,\"respawn_state\":\"%s\",\"dead_ms\":%lld,\"legal_wait_ms\":%lld,\"ai_respawn_delay_ms\":%lld,\"total_dead_ms\":%lld,"
             "\"respawn_attempts\":%d,\"respawns\":%d}", gMe.alive ? "true" : "false", kRespawnNames[gRs.state], deadFor * 1000 / 60,
             gRs.legal < 0 ? -1LL : gRs.legal * 1000 / 60, gRs.ai < 0 ? -1LL : gRs.ai * 1000 / 60, gRs.total < 0 ? -1LL : gRs.total * 1000 / 60, gRs.attempts, gRs.respawns);
    return b;
}
long long gWeaponPressAt = -100000, gWeaponSince = -1;
int gWeaponPresses = 0;
uint32_t gWeaponPress = 0;
Loadout gLoad;
// (v141 beta 3) the weapons page's OK: START (MGO1_WPN_OK_BTN, hex, another). Circle there came back as a punch at the
// deploy (the run_deploy141 bench: Circle 6 deploys in 8 with the strike after, START 0 in 7)
uint32_t WeaponsOkButton() {
    static uint32_t b = 0;
    if (!b) { const char* e = getenv("MGO1_WPN_OK_BTN"); b = e ? (uint32_t)strtoul(e, nullptr, 16) : 0x08; if (!b) b = 0x08; }
    return b;
}
void WeaponSelectFrame() {
    const uint32_t hp = ps2world::ActorByName("NewHudPause");
    if (!hp || R16(hp + 0x326) != 4) return;
    gControl = true;
    static long long readyAt = -1;
    if (gWeaponSince < 0) {
        gWeaponSince = gFrame; gWeaponPresses = 0; gLoad = WantLoadout();
        readyAt = gFrame + RespawnReactFrames();                                 // ((v141 beta 6) the tier's reaction to the page)
        if (gRs.state == RS_RESPAWN_AVAILABLE || gRs.state == RS_DEAD_WAITING_GAME) { gRs.pageAt = gFrame; gRs.state = RS_REQUESTING_RESPAWN; }
    }
    if (gFrame < readyAt) return;
    if (gFrame - gWeaponPressAt < 4) { gPad.buttons = gWeaponPress; return; }   // a press held 4 frames
    // then 10 for the page ((v141 beta 6) 6 from the elite tiers)
    if (gFrame - gWeaponPressAt < (LevelNow() >= 6.f ? 10 : 14)) return;
    // ((v141 beta 6) the watchdog: the page shown 3 s after its reaction, START takes what is there; (b6-fpv) not while its rows
    // still change under the presses (a map's first page: up to 20 presses of 14 frames; cut at 3 s, the auto loadout's support
    // row was left half way, a chaff for a claymore on the fpv bench), 10 s at most)
    static int pageSig = -1;
    static long long pageSigAt = -1;
    { const int sig = R16(hp + 0x328) | R16(hp + 0x33A) << 4 | R16(hp + 0x33E) << 8 | R16(hp + 0x342) << 12 | R16(hp + 0x346) << 16;
      if (sig != pageSig || gWeaponPresses == 0) { pageSig = sig; pageSigAt = gFrame; } }
    if (gFrame - readyAt > 180 && gWeaponPresses > 0 && gWeaponPresses <= 40 && (gFrame - pageSigAt > 60 || gFrame - readyAt > 600)) {
        ++gRs.attempts;
        Log("respawn watchdog: state=%s on the weapons page for %lld ms (%d presses): START", kRespawnNames[gRs.state], (gFrame - gWeaponSince) * 1000 / 60, gWeaponPresses);
        gWeaponPresses = 41;
    }
    const int page = R16(hp + 0x328);
    if (page < 1 || page > 5) return;                                            // opening / closing
    const int pn = R16(hp + 0x338), pc = R16(hp + 0x33A), sn = R16(hp + 0x33C), sc = R16(hp + 0x33E), un = R16(hp + 0x340), uc = R16(hp + 0x342);
    const bool primaryOk = gLoad.primary < 0 || gLoad.primary >= pn || pc == gLoad.primary;
    const bool secondaryOk = gLoad.secondary < 0 || gLoad.secondary >= sn || sc == gLoad.secondary;
    const bool supportOk = gLoad.support < 0 || gLoad.support >= un || uc == gLoad.support;
    const int fn = R16(hp + 0x344), fc = R16(hp + 0x346);
    const bool fourthOk = gLoad.fourth < 0 || gLoad.fourth >= fn || fc == gLoad.fourth;
    if (gDebug >= 2 && gWeaponPresses == 0) {                   // the page's work (its lists)
        char d[700]; int k = snprintf(d, sizeof d, "weapons page:");
        for (uint32_t o = 0x330; o < 0x3B0 && k < (int)sizeof d - 8; o += 2) k += snprintf(d + k, sizeof d - k, "%s%x", o % 16 ? " " : " | ", R16(hp + o));
        Log("%s", d);
    }
    uint32_t b;
    if (++gWeaponPresses > 40) b = 0x08;                        // lost: START takes what is there
    else if (!primaryOk) b = page == 1 ? 0x20 : page > 1 ? 0x10 : 0x40;
    else if (!secondaryOk) b = page == 2 ? 0x20 : page > 2 ? 0x10 : 0x40;
    else if (!supportOk) b = page == 3 ? 0x20 : page > 3 ? 0x10 : 0x40;
    else if (!fourthOk) b = page == 4 ? 0x20 : page > 4 ? 0x10 : 0x40;
    else b = page == 5 ? WeaponsOkButton() : 0x40;
    if (gDebug) Log("weapons: row %d primary %d/%d secondary %d/%d support %d/%d fourth %d/%d: press %x", page, pc, pn, sc, sn, uc, un, fc, fn, b);
    if (b == WeaponsOkButton() || b == 0x08) {
        gPrimaryNow = pc; gSecondaryNow = sc; gSupportNow = uc; gHand = -1;
        if (gRs.state == RS_REQUESTING_RESPAWN) { gRs.okAt = gFrame; gRs.state = RS_WAITING_SPAWN_ACK; ++gRs.attempts; }
    }
    gWeaponPressAt = gFrame; gWeaponPress = b; gPad.buttons = b;
    Act("choosing_weapons");
}
bool WeaponSelectShown() { const uint32_t hp = ps2world::ActorByName("NewHudPause"); return hp && R16(hp + 0x326) == 4; }

// one frame of a pause menu job: true while it has the pad
bool PauseJobFrame() {
    if (gPJ.kind == PJ_NONE) return false;
    gControl = true;
    const uint32_t hp = ps2world::ActorByName("NewHudPause");
    const int state = hp ? R16(hp + 0x326) : -1, page = hp ? R16(hp + 0x328) : -1, cur = hp ? R16(hp + 0x32A) : -1, yes = hp ? R16(hp + 0x32C) : -1;
    if (gFrame - gPJ.pressAt < 4) { gPad.buttons = gPJ.press; return true; }     // a press held 4 frames
    if (gFrame - gPJ.pressAt < 14) return true;                                 // then 10 frames for the menu
    auto press = [&](uint32_t b) { gPJ.press = b; gPJ.pressAt = gFrame; gPad.buttons = b; ++gPJ.presses; };
    auto done = [&](const char* how) { Log("pause menu: %s %s", PauseJobName(gPJ.kind), how); gPJ.kind = PJ_NONE; gPauseDoneAt = gFrame; };
    if (gFrame - gPJ.start > 900 || gPJ.presses > 40) {                          // lost: closed, given up
        if (state == 6 && page != 0xB) { press(0x08); gPJ.start = gFrame - 890; gPJ.presses = 0; return true; }
        done("given up"); return false;
    }
    if (state == 4) { WeaponSelectFrame(); return true; }                        // the weapons (joining)
    if (gPJ.confirmed) { if (state != 6 || page == 0xB) { done("done"); return false; } if (page == 1) { press(0x08); return true; } }
    if (state != 6) { if (gFrame - gPJ.start > 30 || gPJ.presses == 0) press(0x08); return true; }   // START: open it
    if (page == 1) {
        const int want = gPJ.kind == PJ_SUICIDE ? 1 : 3;
        if (cur == want) press(0x2000); else press(cur < want ? 0x40 : 0x10);
        return true;
    }
    if (page == 8) {
        int want = gPJ.kind == PJ_SPECTATOR ? 1 : 0;
        if (gPJ.kind == PJ_JOIN) want = gRule == 0 ? DmUnit() : JoinTeam();
        if (gDebug && cur != gPJ.lastRow) Log("pause menu: team select row %d (wanted %d)", cur, want);
        gPJ.lastRow = cur;
        // a row the list does not have (it wraps or stops): the row there taken
        if (cur != want && ++gPJ.rowPresses > 6) {
            Log("pause menu: row %d not on the list, row %d taken", want, cur);
            if (gRule == 0 && gPJ.kind == PJ_JOIN) gDmUnit = cur;
            want = cur;
        }
        if (cur == want) press(0x2000); else press(0x40);
        return true;
    }
    if (page == 3) {                                                             // the question: YES
        if (yes != 0) press(0x80); else { press(0x2000); gPJ.confirmed = true; }
        return true;
    }
    if (page == 0xB) { done("done"); return false; }
    press(0x4000);                                                               // another page: back
    return true;
}

// ---- the lobby: the doll's game found in the Join Game list. NewOLMJoinGame: its state +0x22EA (3 the
// list, 0x19 a password to type); the list widget at +0x2490: the rows +0x2494, the cursor +0x2496,
// the first row shown +0x2498; each row's entry at +0x1000 (u32: an index in the lobby's host list at
// the TX55 context ([+0x40]) + 0xE914, 52 bytes an entry: +4 the name, +0x15 the password lock; the
// entries' count at + 0xE910, busy while + 0xE90C is not 0). The doll moves the cursor onto its game
// and presses Circle; the steps go on ([Join Game] Join this game); a locked game's password is typed
// when its keyboard opens (never logged). ----
struct Lobby { long long listSince = -1, pressAt = -100000, typedAt = -100000; int lastLogged = -2; uint32_t press = 0; };
Lobby gLobby;
bool GameName(uint32_t jg, int row, std::string& out) {
    const uint32_t ctx = R32(jg + 0x40);
    if (!ctx || ctx >= 0x02000000 || R32(ctx + 0xE90C) != 0) return false;
    const uint32_t idx = R32(jg + 0x1000 + 4 * row), n = R32(ctx + 0xE910);
    if (idx >= n || n > 1000) return false;
    const uint32_t e = ctx + 0xE914 + idx * 0x34;
    out.clear();
    for (int k = 0; k < 16; ++k) { const char c = (char)R8(e + 4 + k); if (!c) break; out += c; }
    return true;
}
bool SameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return false;
    return true;
}

}  // namespace

// ---- (v137) the ladders' side (doll.cpp's ladder code) ----
// known enemies covering a floor point: known within 6 m of it these 10 s, or a clear line from where each was last
// known (35 m at most) to its chest height. Their count; the nearest one's place, id, age (s)
int LadderThreat(const float* end, float* where, int* who, float* ago) {
    int n = 0; float best = 1e30f;
    const float at[3] = {end[0], end[1] + 1100.f, end[2]};
    for (const auto& k : gKnown) {
        const long long age = gFrame - k.second.last;
        if (age > 600) continue;
        const Player* e = PlayerById(k.first);
        if (!e || !e->alive || !Enemy(*e) || (k.second.visible && Asleep(*e))) continue;
        const float* kp = k.second.pos;
        const float d = Dist2D(kp, end);
        bool covers = d < 6000.f && std::fabs(kp[1] - 800.f - end[1]) < 2500.f;
        // ((v141 beta 10) tier 10 knows every enemy: only one within 10 m of the end makes it a detour (its paths' costs flipped 11 times a
        // capture round, its ladders left for long ways round)
        const float lineTo = CheatOn() && !CheatWaitsB8() ? 10000.f : 35000.f;
        if (!covers && d < lineTo) { float eye[3]; BodyPoint(kp, 0.95f, eye); covers = LineClear(eye, at); }
        if (!covers) continue;
        ++n;
        if (d < best) { best = d; if (where) memcpy(where, kp, sizeof(float) * 3); if (who) *who = k.first; if (ago) *ago = age / 60.f; }
    }
    return n;
}
// off a ladder at `at`: the nearest enemy known these 15 s with a line to it (40 m), else the nearest place one was known
// these 2 minutes (60 m). One coming in there gets the short reaction of a corner held (2 s). False: none
bool LadderExitLook(const float* at, float* look) {
    const float eyeAt[3] = {at[0], at[1] + 1500.f, at[2]};
    const float* best = nullptr; float bs = 1e30f;
    for (const auto& k : gKnown) {
        const Player* e = PlayerById(k.first);
        if (!e || !e->alive || !Enemy(*e)) continue;
        const long long age = gFrame - k.second.last;
        const float d = Dist2D(k.second.pos, at);
        float head[3]; BodyPoint(k.second.pos, 0.95f, head);
        const bool fresh = age < 900 && d < 40000.f && LineClear(eyeAt, head);
        if (!fresh && (age >= 7200 || d >= 60000.f)) continue;
        const float s = d + (fresh ? 0.f : 100000.f);
        if (s < bs) { bs = s; best = k.second.pos; }
    }
    if (!best) return false;
    memcpy(look, best, sizeof(float) * 3);
    memcpy(gPre.at, best, sizeof gPre.at); gPre.until = gFrame + 120; gPre.fpv = false;
    return true;
}
bool CarryingObjective() { return Carrying(); }

// (probe 19) a grenade at a player whatever the moment: the throw planned, then done; true while it runs
bool ProbeGrenadeAt(int id, float* predicted) {
    gGrForce = true;
    if (gGr.phase == 0) {
        const Player* e = PlayerById(id);
        if (!e) return false;
        float eye[3]; MyThrowEye(eye);
        const float feet[3] = {e->pos[0], GroundOf(e->pos) + 60.f, e->pos[2]};
        const auto t0 = std::chrono::steady_clock::now();
        const ThrowPlan plan = SolveThrow(eye, feet);
        Log("probe19: plan at %s %.1f m away: yaw %.1f pitch %.1f, goes off at %.0f %.0f %.0f, %.1f m from it, %d bounces (%llu ms)", e->name.c_str(),
            Dist2D(gMe.pos, e->pos) / 1000.f, plan.yaw / kDeg, plan.pitch, plan.end[0], plan.end[1], plan.end[2], plan.err / 1000.f, plan.bounces,
            (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
        if (plan.err > 3000.f) return false;
        gGr.plan = plan; gGr.target = id; gGr.phase = 1; gGr.at = gFrame; gGr.code = GrenadeCode(); gGr.inFight = false;
        memcpy(predicted, plan.end, sizeof plan.end);
    }
    return GrenadeFrame();
}

// (probe 31) a grenade of a given kind at a point: the throw planned, then done; true while it runs
bool ProbeThrowAt(const float* point, int code, float* predicted) {
    gGrForce = true; gGrCodeForce = code;
    if (gGr.phase == 0) {
        float eye[3]; MyThrowEye(eye);
        const ThrowPlan plan = SolveThrow(eye, point);
        Log("probe31: throw %x at %.0f %.0f %.0f: yaw %.1f pitch %.1f, goes off at %.0f %.0f %.0f, %.1f m from it, %d bounces", code, point[0], point[1], point[2],
            plan.yaw / kDeg, plan.pitch, plan.end[0], plan.end[1], plan.end[2], plan.err / 1000.f, plan.bounces);
        if (plan.err > 3000.f) return false;
        gGr.plan = plan; gGr.target = -1; gGr.phase = 1; gGr.at = gFrame; gGr.code = code; gGr.inFight = false;
        memcpy(predicted, plan.end, sizeof plan.end);
    }
    return GrenadeFrame();
}

// (probe 31) the nearest free objective item taken: true while it is not held
bool ProbeTakeObjective() {
    if (Carrying()) return false;
    const Objectives o = SeeObjectives();
    const ObjItem* best = nullptr; float bd = 1e30f;
    for (const ObjItem& it : o.items) if (!it.holder && Dist2D(gMe.pos, it.pos) < bd) { bd = Dist2D(gMe.pos, it.pos); best = &it; }
    if (!best) return false;
    TakeItem(best->pos);
    return true;
}
// (probe 31) the objective items: where, who holds them
std::string ProbeObjectives() {
    const Objectives o = SeeObjectives();
    std::string s;
    for (const ObjItem& it : o.items) {
        const Player* h = nullptr;
        for (const Player& p : gPlayers) if (it.holder && p.obj == it.holder) h = &p;
        char b[120]; snprintf(b, sizeof b, " [%.0f %.0f %.0f %s]", it.pos[0], it.pos[1], it.pos[2], !it.holder ? "free" : it.holder == gMe.obj ? "me" : h ? h->name.c_str() : "?");
        s += b;
    }
    return s;
}

// (probes) a window's code taken as the brain does it: true while it works
bool ProbeWindowTo(int win, int code) { return WindowTo(win, code); }
bool ProbeIAmSnake() { return IAmSnake(); }            // ((v141 beta 9) the choke step's actor)
int ProbeMyType() { return MyType(); }

// (probe 17) the pause menu's SUICIDE as a doll does it: true while it has the pad
bool ProbeSuicideFrame() { if (gPJ.kind == PJ_NONE) PauseJobStart(PJ_SUICIDE); return PauseJobFrame(); }
// (probe 31) TEAM SELECT's TEAM CHANGE: true while it has the pad
bool ProbeTeamChangeFrame() { if (gPJ.kind == PJ_NONE) PauseJobStart(PJ_TEAM_CHANGE); return PauseJobFrame(); }
int SupportNow() { return gSupportNow; }

bool LobbyFrame() {
    const uint32_t jg = ps2world::ActorByName("NewOLMJoinGame");
    if (!jg) { gLobby.listSince = -1; return false; }
    const int state = R16(jg + 0x22EA);
    if (state == 0x19 && !gSet.gamePassword.empty() && ps2world::KeyboardTextEntry() && gFrame - gLobby.typedAt > 600) {
        gLobby.typedAt = gFrame;
        Act("joining");
        Log("lobby: the game's password typed");
        ps2world::SimType((gSet.gamePassword + "\\n").c_str());
        return false;
    }
    if (state != 3 || gSet.game.empty()) { if (state != 3) gLobby.listSince = -1; return false; }
    const int rows = R16(jg + 0x2494), cursor = R16(jg + 0x2496);
    if (gLobby.listSince < 0) gLobby.listSince = gFrame;
    if (rows <= 0 || gFrame - gLobby.listSince < 45) return false;
    int want = -1;
    std::string nm;
    for (int i = 0; i < rows && i < 1000; ++i) if (GameName(jg, i, nm) && SameName(nm, gSet.game)) { want = i; break; }
    Act("finding_game", gSet.game);
    if (want != gLobby.lastLogged) { gLobby.lastLogged = want; Log("lobby: %d games listed, ours %s", rows, want >= 0 ? ("at row " + std::to_string(want + 1)).c_str() : "not there"); }
    if (want < 0) {
        if (gFrame - gLobby.listSince > 3600) { Log("lobby: the game is not listed: the doll starts again"); fflush(stdout); ExitNow(4); }
        return false;
    }
    gControl = true;
    if (gDebug && gFrame % 30 == 0) Log("lobby: rows %d cursor %d top %d, ours %d", rows, cursor, R16(jg + 0x2498), want);
    if (gFrame - gLobby.pressAt < 12) { if (gFrame - gLobby.pressAt < 4) gPad.buttons = gLobby.press; return true; }
    if (cursor == want) { gLobby.pressAt = gFrame; gLobby.press = gPad.buttons = 0x2000; Log("lobby: joining %s", gSet.game.c_str()); return true; }
    gLobby.pressAt = gFrame; gLobby.press = gPad.buttons = cursor < want ? 0x40 : 0x10;
    return true;
}

void CodecNewMap();
void BrainNewMap() {
    CodecNewMap();
    gMaxLife.clear(); gMyMaxLife = 0; gCqcDone = gCqcFailed = 0; gDmTries = 0; gDmUnitSeen = -1;
    gKnown.clear(); FightStop(); gHideHave = false; gDefendHave = false; gGoal = Goal(); gCoveredUntil.clear(); gLeftUntil.clear(); gDryLeftUntil.clear(); gVainUntil.clear(); gWallLeftUntil.clear();
    gSpots.clear(); gSpotsTried = 0; gSpotHave = false; gBoxTouched.clear(); gBoxGoingHave = false; gRoundsMax.clear();
    gOutSince = -1;
    gTr = TrapRun(); gGr = GrenadeRun();
    gRo = RollRun(); gRolls = gRollHits = 0; gEm = EmoteRun(); gEmAsk = EmoteAsk(); gEmotes = 0; gEmoting.clear(); gAnsweredAt.clear();
    gClearAt.clear(); gSmokes.clear(); gSmokesAt = -1000; gObjKnown = Objectives(); gKillAt = -100000;
    gTruceWith = -1; gTruceUntil = -1; gFoughtAt.clear(); gDiedAt.clear(); gWasAlive.clear(); gSpawnAt.clear(); gSpawnPos.clear(); gKillsSeen = -1;
    gKwFoeLast = -1; gKwFoeAt = -100000; gKwPendingAt = -1; gKillSeenAt = -100000; gKillSeenVictim = -1; gChaseDoneId = -1;
}

const char* BrainState() {
    static char b[200];
    // ((v141 beta 10) seen: the enemies seen or heard these 10 s; foe: the latest of them, where it thinks he is)
    int seen = 0, foe = -1; const Known* fk = nullptr;
    for (const auto& k : gKnown) {
        if (gFrame - k.second.last >= 600) continue;
        ++seen;
        if (!fk || k.second.last > fk->last) { fk = &k.second; foe = k.first; }
    }
    const int n = snprintf(b, sizeof b, "target %d%s%s known %zu seen %d", gFi.target, gFi.fpv ? " fpv" : "", gFi.cqc ? " cqc" : "", gKnown.size(), seen);
    if (CheatOn()) { const Player* h = gCheat.huntId >= 0 ? PlayerById(gCheat.huntId) : nullptr; if (h) { fk = nullptr; foe = h->id;
        if (n > 0 && n < (int)sizeof b) snprintf(b + n, sizeof b - n, " foe %d at %.0f %.0f %.0f hunted", foe, h->pos[0], h->pos[1], h->pos[2]); } }
    if (fk && n > 0 && n < (int)sizeof b) snprintf(b + n, sizeof b - n, " foe %d at %.0f %.0f %.0f age %lld", foe, fk->pos[0], fk->pos[1], fk->pos[2], gFrame - fk->last);
    return b;
}

// the game's kill messages (0x751005, a headshot's 0x751010: {killer, victim}; the scores come from the room):
// a kill of the doll's own, for its emote to its victim's kill camera
int gKilledBy = -1;                          // (v141 beta 2) the killer of its last death, from the game's kill message
long long gKilledByAt = -100000;
void OnKillMessage(uint32_t code, uint32_t arg) {
    if ((code != 0x751005 && code != 0x751010) || arg < 0x100000 || arg >= 0x02000000) return;
    const int killer = (int8_t)R8(arg), victim = (int8_t)R8(arg + 1);
    if (victim == gMyId && victim >= 0 && killer >= 0 && killer != victim) { gKilledBy = killer; gKilledByAt = gFrame; }
    if (killer == gMyId && killer >= 0 && victim >= 0 && victim != killer) KillSeen(victim);   // (the counter's too: once)
}

// (v137) the master tier's measures, every frame: its hits taken and its deaths, put to the enemy whose shot it heard
// last (else the one fought); every 20 s in play, each enemy real player judged (the dolls too with =test):
//   dom = 0.5 log2((K + 1) / (D + 1)) of his kills and deaths these 4 minutes (the scoreboard), only with K >= 6 and
//   K / D >= 2 and not a VS RATING under 950; + (VS RATING - 1200) / 1000 up to 0.4; + up to 0.3 when it dies within
//   1.5 s of seeing him (median of 3+); + up to 0.3 for his one-shot kills of it; + his hits on it over 25 % of his
//   shots it heard (30+), up to 0.3.
// The amount m: +0.15 a judgement while dom >= 1, -0.15 while dom < 0.4 (-0.3 gone), kept between; 0 with the
// difficulty set under 4 or the live level off ----
struct MasterSeen { int lastLife = -1; bool alive = false; long long judgedAt = -100000; };
MasterSeen gMs;
// (v141 beta 3) a death's line waits for its deaths counter (its variable set, read every 30 frames): not up within 3 s, the
// round started again (the game puts everyone back at the spawn when the second player comes in, or at a new round), said so
struct PendingDeath { std::string text; long long at = -1; int deaths = -1; };
PendingDeath gPendDeath;
void PendingDeathFrame() {
    if (gPendDeath.at < 0) return;
    if (gDeaths > gPendDeath.deaths) { Log("death: %s", gPendDeath.text.c_str()); gPendDeath.at = -1; }
    else if (gFrame - gPendDeath.at > 180) { Log("round restart: its life ended with no death counted (the game's restart of the round), %s", gPendDeath.text.c_str()); gPendDeath.at = -1; }
}
void MasterFrame() {
    PendingDeathFrame();
    const int mode = MasterMode();
    if (mode == 1) { static bool said = false; if (!said) { said = true; Log("level: MASTER forced (MGO1_DOLL_MASTER=1, tests): level 6 against everyone"); } return; }
    auto shooter = [](long long within) {                // (the enemy whose shot it heard last)
        int id = -1; long long best = -1;
        for (const auto& k : gKnown) if (gFrame - k.second.shotAt <= within && k.second.shotAt > best) { best = k.second.shotAt; id = k.first; }
        return id;
    };
    const bool alive = gMe.alive && gMe.life > 0;
    if (alive && gMs.alive && gMs.lastLife >= 0 && gMe.life < gMs.lastLife) { const Player* s = PlayerById(shooter(20)); if (s) ++RivalOf(*s).hits; }
    if (gMs.alive && !alive) {                           // (its death: who, how soon after it saw him, at one shot or not)
        int sid = gFrame - gKilledByAt < 120 ? gKilledBy : -1;   // ((v141 beta 2) the game's kill message first)
        if (sid < 0) sid = shooter(60);
        if (sid < 0) sid = gFi.target;
        const Player* s = PlayerById(sid);
        const auto kn = gKnown.find(sid);
        // (v141 beta 2) every death in the log: by whom (his shot heard last, else the one fought), how far, its life before the
        // last hit (90 and more: one shot), how long after it first saw him, the one it fought
        {
            const Player* f = PlayerById(gFi.target);
            const bool seen = kn != gKnown.end() && kn->second.first >= 0;
            char b[300];
            snprintf(b, sizeof b, "by %s%s at %.1f m, life %d before, %s, fighting %s, level %.2f", s ? s->name.c_str() : "?", s && !IsDollName(s->name) ? " (a player)" : "",
                     s ? Dist2D(gMe.pos, s->pos) / 1000.f : -1.f, gMs.lastLife, seen ? ("seen " + std::to_string((gFrame - kn->second.first) / 60.0).substr(0, 4) + " s before").c_str() : "never seen",
                     f ? f->name.c_str() : "nobody", LevelNow());
            if (gPendDeath.at >= 0) Log("death: %s", gPendDeath.text.c_str());   // (two in 3 s: the first one said as it was)
            gPendDeath.text = b; gPendDeath.at = gFrame; gPendDeath.deaths = gDeaths;
        }
        LoadoutDeathNote(s ? Dist2D(gMe.pos, s->pos) : -1.f);   // ((v141 beta 6) the next map's auto loadout answers what killed it)
        if (s) {
            Rival& r = RivalOf(*s);
            ++r.deaths;
            if (gMs.lastLife >= 90) ++r.oneShot;
            if (kn != gKnown.end() && kn->second.first >= 0 && gFrame - kn->second.first < 600) {
                r.ttd.push_back((int)(gFrame - kn->second.first));
                if (r.ttd.size() > 8) r.ttd.erase(r.ttd.begin());
            }
        }
    }
    gMs.alive = alive; gMs.lastLife = alive ? gMe.life : -1;
    if (gFrame - gMs.judgedAt < 1200 || gRoom.phase != 2) return;
    gMs.judgedAt = gFrame;
    for (const Player& p : gPlayers) if (!p.local && p.id != gMyId && EnemySide(p) && (mode == 2 || !IsDollName(p.name))) RivalOf(p);
    for (auto& kv : gRivals) {
        Rival& r = kv.second;
        const Player* p = PlayerById(kv.first);
        const bool present = p && p->name == r.name && EnemySide(*p) && (mode == 2 || !IsDollName(p->name));
        uint8_t v[0x20];
        const float was = r.m;
        char why[200] = "";
        if (!present || !ps2world::VariableSet(kv.first + 1, v, sizeof v)) {
            r.m = std::max(0.f, r.m - 0.3f); r.dom = 0.f;
            snprintf(why, sizeof why, "gone");
        } else {
            r.presentAt = gFrame;
            const int k = v[0x14] | (v[0x15] << 8), d = v[0x16] | (v[0x17] << 8);
            if (!r.ring.empty() && (k < r.ring.back()[1] || d < r.ring.back()[2])) r.ring.clear();   // (the scores reset)
            r.ring.push_back({(int)(gFrame / 60), k, d});
            while (r.ring.size() > 13) r.ring.erase(r.ring.begin());
            const int kw = r.ring.back()[1] - r.ring.front()[1], dw = r.ring.back()[2] - r.ring.front()[2];
            const int rating = IsDollName(p->name) ? -1 : VsRating(kv.first);
            const float kd = (kw + 1.f) / (dw + 1.f);
            float dom = 0.f;
            int ttdMed = -1;
            if (r.ttd.size() >= 3) { std::vector<int> s = r.ttd; std::sort(s.begin(), s.end()); ttdMed = s[s.size() / 2]; }
            if (kw >= 6 && kd >= 2.f && !(rating > 0 && rating < 950)) {
                dom = 0.5f * std::log2(kd);
                if (rating > 0) dom += std::max(0.f, std::min(0.4f, (rating - 1200) / 1000.f));
                if (ttdMed >= 0) dom += std::max(0.f, std::min(0.3f, (90 - ttdMed) / 150.f));
                if (r.deaths >= 3) dom += std::min(0.3f, 0.4f * r.oneShot / r.deaths);
                if (r.shots >= 30) dom += std::max(0.f, std::min(0.3f, (float)r.hits / r.shots - 0.25f));
            }
            const float domWas = r.dom;
            r.dom = dom;
            if (std::fabs(dom - domWas) >= 0.25f || (dom > 0.f) != (domWas > 0.f)) r.m = -1.f;   // (said below)
            snprintf(why, sizeof why, "his kills %d deaths %d these %.1f min, VS RATING %d, it died %d times by him (%d at one shot, %.1f s after seeing him), his hits %d of %d shots",
                     kw, dw, (r.ring.back()[0] - r.ring.front()[0]) / 60.f, rating, r.deaths, r.oneShot, ttdMed >= 0 ? ttdMed / 60.f : -1.f, r.hits, r.shots);
            if (r.shots > 400) { r.shots /= 2; r.hits /= 2; }
        }
        // (v141 beta 6) his domination is a part of his strength in the live level (AdaptFrame): said when it changes
        if (r.m < 0.f) {
            r.m = 0.f;
            Log("rival: %s dominates the dolls %.2f (%s): his strength in the live level", r.name.c_str(), r.dom, why);
        } else if (gDebug && r.dom > 0.f && (gMs.judgedAt / 1200) % 3 == 0) Log("rival: %s still %.2f (%s)", r.name.c_str(), r.dom, why);
        (void)was;
    }
    for (auto it = gRivals.begin(); it != gRivals.end();) {   // (gone 5 minutes and back to nothing: forgotten)
        if (it->second.m <= 0.f && gFrame - it->second.presentAt > 18000) it = gRivals.erase(it); else ++it;
    }
}

// the match's stance for its side (-1 ahead .. +1 behind; the VS RATINGs' part apart, -0.75 .. 0.75): team deathmatch the
// round's tickets half, the rounds won a quarter, the kills a quarter; capture the KEROTANs in the goals (1) and carried (0.5);
// rescue the GA-KO carried (0.5) or in the goal (1); sneaking the item on Snake (0.6); deathmatch its kills against the real
// players'. moved: how much the match has told (kills, tickets, objectives: its confidence); false: nothing to judge
bool Stance(float* b, float* vs, float* moved, char* why, size_t whyN) {
    auto clamp1 = [](float x) { return std::max(-1.f, std::min(1.f, x)); };
    int mine = 0, theirs = 0, best = -1, sum = 0, n = 0;
    int nSide[2] = {0, 0}, nReal[2] = {0, 0}, nAll = 0;
    float rSide[2] = {0.f, 0.f}, rAll = 0.f;
    const int me = MyScore(0x14);
    *b = 0.f; *vs = 0.f; *moved = 0.f;
    for (int id = 0; id < 16; ++id) {
        int t = 0xFF; std::string nm;
        uint8_t v[0x20];
        if (!ps2world::PlayerInfo(id, &t, &nm) || nm.empty() || t == 0xFE || t == 0xFF || !InSession(id)) continue;   // ((v141 beta 6) gone: not judged)
        const bool doll = id == gMyId || IsDollName(nm) || IsDollUid(ps2world::PlayerUserId(id));
        if (t == 0 || t == 1) ++nSide[t];
        if (!doll) {                                                  // a real player's VS RATING
            const int r = VsRating(id);
            if (r > 0) { rAll += (float)r; ++nAll; if (t == 0 || t == 1) { rSide[t] += r - 1000.f; ++nReal[t]; } }
        }
        if (id == gMyId || !ps2world::VariableSet(id + 1, v, sizeof v)) continue;
        const int k = v[0x14] | (v[0x15] << 8);
        if (gRule == 0) {
            if (doll) continue;
            best = std::max(best, k); sum += k; ++n;
        } else if (t == gMyTeam) mine += k; else theirs += k;
    }
    char vsWhy[96] = "";
    if (gRule == 0) {                                                 // deathmatch: against the real players
        if (n == 0 || me < 0) return false;
        const float avg = (float)sum / n;
        if (me >= best + 3) *b = -std::min(1.f, (me - best) / 6.f);
        else if (me + 2 <= avg) *b = std::min(1.f, (avg - me) / 6.f);
        if (nAll) { *vs = std::max(-0.75f, std::min(0.75f, (rAll / nAll - 1000.f) / 800.f)); snprintf(vsWhy, sizeof vsWhy, "; the real players' VS RATING %.0f", rAll / nAll); }
        *moved = std::min(1.f, (me + sum) / 6.f);
        snprintf(why, whyN, "its kills %d, the real players' best %d, their average %.1f%s", me, best, avg, vsWhy);
        return true;
    }
    if (gMyTeam != 0 && gMyTeam != 1) return false;
    const int my = gMyTeam, th = 1 - gMyTeam;
    mine += std::max(0, me);
    const float sMine = rSide[my] / std::max(1, nSide[my]), sTheirs = rSide[th] / std::max(1, nSide[th]);
    if (nReal[0] + nReal[1] > 0) {
        *vs = std::max(-0.75f, std::min(0.75f, (sTheirs - sMine) / 600.f));
        snprintf(vsWhy, sizeof vsWhy, "; VS RATING %+.0f against %+.0f a player", sMine, sTheirs);
    }
    const int winsMine = gRoom.wins[my], winsTheirs = gRoom.wins[th];
    const float sK = clamp1((0.5f - (mine + 2.f) / (mine + theirs + 4.f)) * 2.5f);   // kills
    const float sR = clamp1((winsTheirs - winsMine) / 2.f);                            // rounds
    float sO = 0.f, events = (float)(mine + theirs + 3 * (winsMine + winsTheirs));     // the mode's objective
    char obj[96] = "";
    if (gRule == 1) {                                                 // team deathmatch: the round's tickets
        const int tm = gRoom.tickets[my], tt = gRoom.tickets[th];
        if (tm + tt > 0) sO = clamp1((tt - tm) / std::max(4.f, 0.25f * (tm + tt)));
        events += std::abs(tm - tt) / 2.f;
        snprintf(obj, sizeof obj, "tickets %d against %d, ", tm, tt);
    } else if (gRule >= 2 && gRule <= 4) {
        const Objectives o = SeeObjectives();
        auto isSet = [](const float* g) { return std::fabs(g[0]) + std::fabs(g[2]) > 1.f; };
        float ours = 0.f, theirsO = 0.f;
        for (const ObjItem& it : o.items) {
            const Player* holder = nullptr;
            for (const Player& p : gPlayers) if (it.holder && p.obj == it.holder) holder = &p;
            const bool holderMine = holder && (holder->local || holder->team == my), holderTheirs = holder && !holderMine;
            if (gRule == 3) {                                     // capture: in a goal 1, carried 0.5
                if (it.goalTeam == my) ours += 1.f; else if (it.goalTeam == th) theirsO += 1.f;
                if (holderMine) ours += 0.5f; else if (holderTheirs) theirsO += 0.5f;
            } else if (gRule == 2 && o.present) {                 // rescue: the attackers' progress
                const bool attacker = !isSet(o.goal[my]);
                const float* goal = isSet(o.goal[0]) ? o.goal[0] : o.goal[1];
                float prog = holder ? 0.5f : 0.f;
                if (holder && Dist2D(it.pos, goal) < 3000.f) prog = 1.f;
                (attacker ? ours : theirsO) += prog;
            } else if (gRule == 4) {                              // sneaking: the item on Snake
                const bool snake = holder && IsSnake(*holder);
                if (snake) (IAmSnake() ? ours : theirsO) += 0.6f;
            }
        }
        sO = clamp1((theirsO - ours) / 1.5f);
        events += 4.f * (ours + theirsO);
        snprintf(obj, sizeof obj, "the objective %.1f against %.1f, ", ours, theirsO);
    }
    *b = clamp1(gRule == 1 ? 0.5f * sO + 0.25f * sR + 0.25f * sK : 0.4f * sO + 0.3f * sR + 0.3f * sK);
    *moved = std::min(1.f, events / 8.f);
    snprintf(why, whyN, "%sits team's kills %d against %d, rounds %d against %d%s", obj, mine, theirs, winsMine, winsTheirs, vsWhy);
    return true;
}
// each player's play (its variable set: +0x14 kills, +0x16 deaths) every 5 s, 3 minutes: (second, kills, deaths)
struct PerfRing { std::string name; std::vector<std::array<int, 3>> ring; };
std::map<int, PerfRing> gPerf;
// a player's strength on the ladder (1 .. 10) and how much is known of it (0 .. 1): his kills against his deaths these 3 minutes
// (0.6) and this minute (0.4), a real player's VS RATING, a rival's domination of the dolls
bool PlayerStrength(int id, const std::string& name, bool real, float* skill, float* conf) {
    const auto it = gPerf.find(id);
    if (it == gPerf.end() || it->second.name != name || it->second.ring.size() < 2) return false;
    const auto& r = it->second.ring;
    auto perf = [](int k, int d) { return std::log2((k + 1.f) / (d + 1.f)); };
    const auto& last = r.back();
    const auto& m0 = r.front();
    const auto& s0 = r[r.size() > 12 ? r.size() - 12 : 0];
    const int km = last[1] - m0[1], dm = last[2] - m0[2], ks = last[1] - s0[1], ds = last[2] - s0[2];
    const float span = (last[0] - m0[0]) / 60.f;                          // minutes
    float sk = 5.5f + 2.0f * (0.6f * perf(km, dm) + 0.4f * perf(ks, ds));
    float c = std::min(1.f, (km + dm) / 10.f) * std::min(1.f, span / 2.f);
    if (real) {
        const int rating = VsRating(id);
        if (rating > 0 && rating != 1000) { sk += std::max(-1.5f, std::min(1.5f, (rating - 1000) / 400.f)); c = std::min(1.f, c + 0.25f); }
        const auto rv = gRivals.find(id);
        if (rv != gRivals.end() && rv->second.name == name && rv->second.dom > 0.f) sk += std::min(1.2f, 0.8f * rv->second.dom);
    }
    *skill = std::max(1.f, std::min(10.f, sk));
    *conf = c;
    return true;
}
// (v141 beta 6) the live level judged every 5 s (see the table's head); its lines: "DOLL ADAPT: old=.. new=.. target=.. ..."
void AdaptFrame() {
    const int base = BaseLevel(), lo = AdaptLo(), hi = AdaptHi();
    // the elite moves follow the effective level (beta 5's NIGHTMARE at 6 and up)
    { const float e = EffLevel(); const bool was = gAd.elite; gAd.elite = gAd.elite ? e >= 5.6f : e >= 6.f;
      if (gAd.elite != was && gLevelBase > 0) Log("level: the elite moves %s (level %.2f)", gAd.elite ? "on" : "off", e); }
    if (base != gLevelBase || !gSet.adapt) {                          // a new anchor (an order): the level follows at once
        if (base != gLevelBase || gLevel < 0) {
            const float was = gLevel;
            gLevelBase = base; gLevel = (float)std::max(lo, std::min(hi, base)); gVsOffset = 0.f; gAd = AdaptState(); gAd.elite = EffLevel() >= 6.f;
            gLevelWhy = gSet.adapt ? "even" : "fixed";
            Log("level: %d %s (the difficulty set%s)%s", base, kTierNames[base - 1], gSet.adapt ? "" : ", the live level off",
                was >= 0.f && gSet.adapt ? "" : "");
            if (gSet.adapt) Log("level: live, between %d %s and %d %s", lo, kTierNames[lo - 1], hi, kTierNames[hi - 1]);
        }
        if (!gSet.adapt) { gLevel = (float)base; gLevelWhy = "fixed"; return; }
    }
    if (gLevel < 0) gLevel = (float)std::max(lo, std::min(hi, base));
    if (gLevel < lo || gLevel > hi) gLevel = std::max((float)lo, std::min((float)hi, gLevel));   // (the bounds changed)
    if (gFrame - gLevelAt < 300) return;
    gLevelAt = gFrame;
    // every player's play, sampled every 5 s (a new name in a slot, or its counters down: afresh)
    const int sec = (int)(gFrame / 60);
    for (int id = 0; id < 16; ++id) {
        int t = 0xFF; std::string nm; uint8_t v[0x20];
        if (!ps2world::PlayerInfo(id, &t, &nm) || nm.empty() || t == 0xFE || !ps2world::VariableSet(id + 1, v, sizeof v)) { gPerf.erase(id); continue; }
        const int k = v[0x14] | (v[0x15] << 8), d = v[0x16] | (v[0x17] << 8);
        PerfRing& pr = gPerf[id];
        if (pr.name != nm || (!pr.ring.empty() && (k < pr.ring.back()[1] || d < pr.ring.back()[2]))) { pr.name = nm; pr.ring.clear(); }
        pr.ring.push_back({sec, k, d});
        if (pr.ring.size() > 37) pr.ring.erase(pr.ring.begin());
    }
    if (gRoom.phase != 2 || RealPlayers() == 0) return;               // playing, with real players
    // the enemies' strength, its own side's (itself in it): real players weigh 1, dolls 0.4
    float eSum = 0.f, eW = 0.f, aSum = 0.f, aW = 0.f;
    int eReal = 0, aReal = 0;
    for (int id = 0; id < 16; ++id) {
        int t = 0xFF; std::string nm;
        if (!ps2world::PlayerInfo(id, &t, &nm) || nm.empty() || t == 0xFE || t == 0xFF || !InSession(id)) continue;   // ((v141 beta 6) gone: a slot keeps its last player's)
        const bool self = id == gMyId;
        const bool doll = self || IsDollName(nm) || IsDollUid(ps2world::PlayerUserId(id));
        float sk = 0.f, c = 0.f;
        if (!PlayerStrength(id, nm, !doll, &sk, &c)) continue;
        const bool enemy = !self && (gRule == 0 || t != gMyTeam);
        const float w = (self ? 0.5f : doll ? 0.4f : 1.f) * c;
        if (enemy) { eSum += w * sk; eW += w; if (!doll) ++eReal; }
        else { aSum += w * sk; aW += w; if (!doll) ++aReal; }
    }
    gAd.enemy = eW > 0.f ? eSum / eW : 5.5f; gAd.confE = std::min(1.f, eW / 1.5f);
    gAd.ally = aW > 0.f ? aSum / aW : 5.5f; gAd.confA = std::min(1.f, aW / 1.5f);
    float b = 0.f, vs = 0.f, moved = 0.f;
    char why[240] = "";
    const bool st = Stance(&b, &vs, &moved, why, sizeof why);
    if (!st) { b = 0.f; vs = 0.f; moved = 0.f; }
    gVsOffset = vs;
    gAd.balance = b + vs;
    // the target: the enemies against its side (weighed by how much is known), the stance; the objective modes judge more by the
    // stance (an objective player is not weak for few kills)
    const bool objectiveMode = gRule >= 2;
    const float kSkillW = objectiveMode ? 0.5f : 0.7f, kBal = objectiveMode ? 2.4f : 2.0f;
    gAd.conf = std::max(0.f, std::min(1.f, 0.6f * gAd.confE + 0.4f * gAd.confA));
    const float gap = gAd.confE > 0.05f ? gAd.enemy - gAd.ally : 0.f;
    float target = base + kSkillW * gap * gAd.conf + kBal * (b * moved + vs);
    target = std::max((float)lo, std::min((float)hi, target));
    gAd.target = target;
    const float was = gLevel, d = target - gLevel;
    // the ramp: a quarter level each 5 s (half a level up when the gap is obvious and known); down a little slower; nothing
    // within 0.3 (no rubber band)
    if (std::fabs(d) >= 0.3f) {
        const float up = d > 2.f && gAd.conf > 0.6f ? 0.5f : 0.25f, down = d < -2.f && gAd.conf > 0.6f ? 0.35f : 0.2f;
        gLevel = std::max((float)lo, std::min((float)hi, gLevel + std::max(-down, std::min(up, d))));
    }
    const char* reason = std::fabs(d) < 0.3f ? "on_target" : gAd.conf < 0.2f && std::fabs(b) < 0.15f ? "low_confidence" :
                         gap > 1.f && d > 0 ? "enemy_team_outperforming" : gap < -1.f && d < 0 ? "ally_team_dominating" :
                         b > 0.15f ? "its_side_behind" : b < -0.15f ? "its_side_ahead" : d > 0 ? "enemy_stronger" : "enemy_weaker";
    gAd.reason = reason;
    gAd.trend = gLevel > was + 0.01f ? "up" : gLevel < was - 0.01f ? "down" : "flat";
    const std::string whyCode = gAd.balance > 0.15f || gLevel > base + 0.25f ? "behind" : gAd.balance < -0.15f || gLevel < base - 0.25f ? "ahead" : "even";
    if (std::fabs(gLevel - was) >= 0.05f || whyCode != gLevelWhy) {
        gAd.changedAt = gFrame;
        Log("DOLL ADAPT: old=%.2f new=%.2f target=%.2f base=%d enemy_strength=%.1f ally_strength=%.1f balance=%+.2f confidence=%.2f reason=%s "
            "(enemies %d real, allies %d real; %s)", was, gLevel, target, base, gAd.enemy, gAd.ally, gAd.balance, gAd.conf, reason, eReal, aReal,
            st ? why : "the stance not known yet");
    }
    gLevelWhy = whyCode;
    const int tier = std::max(0, std::min(9, (int)std::lround(EffLevel()) - 1));
    if (tier != gAd.tier) {
        if (gAd.tier >= 0) Log("level: tier %s (%.2f), was %s", kTierNames[tier], EffLevel(), kTierNames[gAd.tier]);
        gAd.tier = tier;
    }
}
// (v141 beta 7) tier 10's own play on and off (CheatState): the live level without the foe's part, on from 9.75, off under 9.5; one
// line at each change; this minute's share on
void CheatFrame() {
    const float e = EffLevel();
    const bool was = gCheat.on;
    gCheat.on = CheatAllowed() && (was ? e >= 9.5f : e >= 9.75f);
    ++gCheat.minFrames;
    if (gCheat.on) ++gCheat.minOn;
    // ((v141 beta 8) its focus on the real players: a line at each change (no name in it))
    const bool focus = CheatFocus();
    if (focus != gCheat.focus) {
        gCheat.focus = focus;
        if (focus) Log("DOLL CHEAT: focus on the real players (%s)", gRule == 0 ? "a deathmatch: the other dolls left alone unless they hurt it" : "a real enemy first");
        else Log("DOLL CHEAT: focus off (%s)", !gCheat.on ? "the cheat off" : !CheatFocusAllowed() ? "MGO1_DOLL_CHEAT_FOCUS not 1" : "no real player among its enemies");
        if (!focus) gCheat.dollFoeUntil.clear();
    }
    if (gCheat.on == was) return;
    gCheat.at = gFrame;
    if (gCheat.on) ++gCheat.ons;
    gCheat.preFor = gCheat.huntId = -1; gCheat.reloadAt = gCheat.topUpAt = -1; gCheat.preView = false; gCheat.viewAt = -1;
    Log("DOLL CHEAT: %s at level %.2f (%s, the difficulty set %d, %s, enemy strength %.1f, ally strength %.1f, confidence %.2f)", gCheat.on ? "on" : "off", e,
        TierName(e), BaseLevel(), gSet.adapt ? "the live level" : "the live level off", gAd.enemy, gAd.ally, gAd.conf);
}
void LevelFrame() {
    MasterFrame();                                       // ((v137) the rivals: their measures every frame, judged every 20 s)
    AdaptFrame();
    CheatFrame();                                        // ((v141 beta 7) tier 10's own play)
}
const char* TierName(float level) { const int t = std::max(1, std::min(10, (int)std::lround(level))); return kTierNames[t - 1]; }
// ((v150-host beta 18) the direct aim's line (doll.cpp's "aim:" line, at the start and at the order)
std::string DirectAimSaid() { return DirectAimLine(); }
// ((v150-host beta 16) the parity mode's numbers at the difficulty set (doll.cpp's "parity:" line, at the start and at each order)
std::string ParitySaid() {
    if (!Parity()) return std::string();
    const VdollLevel& v = Vd();
    char b[400];
    snprintf(b, sizeof b, "difficulty %d plays by the virtual doll's level %d: reaction %d frames and its look's 0 to 5 (%d less for its bullet), "
             "its aim on him %d frames before a fight's first shot, aim error %.1f degrees, the head in %.0f %% of its fights (the first person view), "
             "sight %.0f m, field %.0f degrees, bursts %d bullets, pauses %d frames; the live level off; the turn, the view and the bullet the game's%s",
             BaseLevel(), BaseLevel(), v.react, kParityBullet, v.settle, v.err, v.head * 100.f, v.sees, v.field, v.burst, v.pause,
             CheatAllowed() && BaseLevel() >= 10 ? "; tier 10's cheat on (MGO1_DOLL_CHEAT=0: off)" : "");
    return b;
}
// (v141 beta 6) the status's "difficulty": base, effective, tier, adaptive, bounds, the estimates, the reaction now
std::string AdaptJson() {
    char b[768];
    // ((v141 beta 7) "cheat": tier 10's own play on, allowed (MGO1_DOLL_CHEAT), on or off since (s), times on, its guns readied on a coming one;
    // (v141 beta 8) its focus on the real players on)
    snprintf(b, sizeof b, "{\"base\":%d,\"base_tier\":\"%s\",\"effective\":%.2f,\"tier\":\"%s\",\"adaptive\":%s,\"min\":%d,\"max\":%d,\"target\":%.2f,"
             "\"enemy_strength\":%.1f,\"ally_strength\":%.1f,\"balance\":%.2f,\"confidence\":%.2f,\"trend\":\"%s\",\"reason\":\"%s\",\"elite\":%d,"
             "\"reaction_ms\":%d,\"offset\":%.2f,\"cheat\":{\"on\":%s,\"allowed\":%s,\"since_s\":%.0f,\"ons\":%d,\"pre_aims\":%d,\"focus\":%s},\"parity\":%s}",
             BaseLevel(), kTierNames[BaseLevel() - 1], EffLevel(), TierName(EffLevel()), gSet.adapt ? "true" : "false", AdaptLo(), AdaptHi(),
             gAd.target < 0 ? EffLevel() : gAd.target, gAd.enemy, gAd.ally, gAd.balance, gAd.conf, gAd.trend.c_str(), gSet.adapt ? gAd.reason.c_str() : "fixed",
             gAd.elite ? 1 : 0, (int)std::lround(ReactMid(RF_PERCEPTION) * 1000.f / 60.f), gSet.adapt ? DollOffset() : 0.f,
             gCheat.on ? "true" : "false", CheatAllowed() ? "true" : "false", gCheat.at < 0 ? 0.0 : (gFrame - gCheat.at) / 60.0, gCheat.ons, gCheat.preAimsAll,
             gCheat.focus ? "true" : "false", Parity() ? "true" : "false");
    return b;
}
float LevelShown() { return LevelNow(); }
const char* LevelWhy() { return gLevelWhy.c_str(); }

// ---- the radio (docs/PLAYABLE_DOLL.md 7). A teammate's calls, each caller's own: answered (roger,
// negative or nothing: its level, what it is doing, how far it is; the dolls closer to the caller answer
// first; too far or in a fight: negative; dead or stunned: nothing at all) and done: follow me, stay
// together, backup (two dolls at most with each caller), come here, pull out: along with the caller a
// while, taking cover by him and shooting what he aims at when he stops to shoot; hold: still where it
// is; go go go: on again, the ones following him spread out; defend the target: the objective played
// harder (section 5); enemy sighted, hit: toward the caller. Its own calls: an enemy sighted, backup (the
// objective on it, or hurt in a fight), follow me (the objective on it, an ally close), all clear after
// a fight, nice job, don't die, go go go at the round's start; never too often, never what its side has
// just said. What was asked ends when the caller dies (its own play again at once), leaves or changes
// sides, when its time is up, or on "go go go". Deathmatch: no allies, nothing answered, no call ----
namespace {
enum CodecKind { CK_NONE, CK_FOLLOW, CK_HOLD, CK_GOTO };
struct CodecOrder { int kind = CK_NONE, who = -1, phrase = -1; std::string name; long long until = 0; float at[3] = {0, 0, 0}; };
CodecOrder gCo;
struct Reply { int phrase = -1; long long at = 0; std::string why; };
Reply gReply;                                     // an answer, made after its reaction time
long long gOwnCallAt = -100000;                   // its last call of its own
std::map<int, long long> gSaidAt, gSideSaidAt;    // each phrase: its last time said by it, by its side (heard)
int gCodecTarget = -1;                            // the fight's target last seen (the calls about it)
std::map<int, int> gAllyKills;                    // an ally's kills (nice job)
std::map<int, long long> gDontDieAt;              // an ally's last "don't die"
long long gCodecKillsAt = -100000, gRoundGoAt = -1;
bool gWasFrozen = true;
int LevelIndex() { return std::max(1, std::min(5, Lv())) - 1; }
float Chatty() { static const float k[5] = {0.35f, 0.55f, 0.75f, 0.9f, 0.97f}; return k[LevelIndex()]; }   // answers a call
float Obeys() { static const float k[5] = {0.55f, 0.7f, 0.85f, 0.95f, 1.0f}; return k[LevelIndex()]; }    // does what it is asked
float Talks() { static const float k[5] = {0.1f, 0.25f, 0.45f, 0.65f, 0.8f}; return k[LevelIndex()]; }    // makes its own calls
int ReactFrames() { static const int k[5] = {80, 60, 45, 30, 20}; const int lo = k[LevelIndex()]; return lo + (int)(Rnd() % (unsigned)lo); }
const char* PhraseName(int p) { return p >= 0 && p < kPhrases ? kPhraseNames[p] : "?"; }
// what keeps it from doing what an ally asks
const char* CodecBusy(const Player& caller) {
    if (Carrying()) return "carrying the objective";
    if (gFi.target >= 0) return "in a fight";
    if (gSet.order == "defend") return "defending (ordered)";
    if (gSet.order == "hide") return "hiding (ordered)";
    if ((gSet.order == "follow" || gSet.order == "target") && !SameName(gSet.target, caller.name)) return "on another order";
    if (gCo.kind != CK_NONE && gCo.who != caller.id && gCo.phrase != PH_GO) return "on another order";   // (with another caller)
    if (IAmSnake()) return "Snake";
    return nullptr;
}
// the allied dolls alive closer to the caller than this one
int DollRank(const Player& caller) {
    const float mine = Dist2D(gMe.pos, caller.pos);
    int r = 0;
    for (const Player& p : gPlayers)
        if (!p.local && p.id != caller.id && p.id != gMyId && Ally(p) && p.alive && IsDollName(p.name) && Dist2D(p.pos, caller.pos) < mine) ++r;
    return r;
}
// the other dolls of this server (MGO1_DOLL_PEERS: their folders, a pattern with %d for 1 to 16): what each
// one does, from its status.json (every 2 s at most, when a call needs it)
struct Peer { std::string name, act, arg; int team = -1; bool alive = false; };
std::vector<Peer> gPeers;
long long gPeersAt = -100000;
std::string JsonField(const std::string& j, const char* key) {
    const std::string k = std::string("\"") + key + "\":";
    size_t a = j.find(k);
    if (a == std::string::npos) return "";
    a += k.size();
    if (a < j.size() && j[a] == '"') { const size_t b = j.find('"', a + 1); return b == std::string::npos ? "" : j.substr(a + 1, b - a - 1); }
    size_t b = a;
    while (b < j.size() && j[b] != ',' && j[b] != '}') ++b;
    return j.substr(a, b - a);
}
bool ReadPeers(bool fresh = false) {
    static const char* pat = getenv("MGO1_DOLL_PEERS");
    static const bool ok = pat && strstr(pat, "%d") && std::count(pat, pat + strlen(pat), '%') == 1;   // (one %d, nothing else)
    if (!ok) return false;
    if (!fresh && gFrame - gPeersAt < 120) return true;
    gPeersAt = gFrame;
    gPeers.clear();
    const std::string me = MyName();
    for (int i = 1; i <= 16; ++i) {
        char dir[400];
        snprintf(dir, sizeof dir, pat, i);
        FILE* f = fopen((std::string(dir) + "\\status.json").c_str(), "rb");
        if (!f) continue;
        char buf[2048];
        const size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        const std::string j(buf, n);
        Peer pe;
        pe.name = JsonField(j, "name"); pe.act = JsonField(j, "act"); pe.arg = JsonField(j, "arg");
        pe.team = atoi(JsonField(j, "team").c_str()); pe.alive = JsonField(j, "alive") == "1";
        if (!pe.name.empty() && !SameName(pe.name, me)) gPeers.push_back(pe);
    }
    return true;
}
// the dolls following a caller on the radio now (-1: not known, no peers' files)
int FollowersOf(const std::string& caller) {
    if (!ReadPeers(true)) return -1;                               // (a call: their status now, not 2 s old)
    int n = 0;
    for (const Peer& pe : gPeers)
        if (pe.alive && pe.team == gMyTeam && (pe.act == "following_codec" || pe.act == "covering" || pe.act == "taking_cover") && SameName(pe.arg, caller)) ++n;
    return n;
}
// a peer that would not go with the caller: fighting, carrying the objective, on an order, or with
// another caller (its activity in its status.json)
bool PeerBusy(const Peer& pe, const std::string& caller) {
    static const char* const kFight[] = {"fighting", "aiming_head", "cqc", "reloading", "switching_weapon", "throwing_grenade", "carrying_objective",
                                         "following", "defending", "hiding", "menu", "dead", "spawning", "leaving_box", "standing_up"};
    static const char* const kRadio[] = {"following_codec", "covering", "taking_cover", "holding", "going_to_ally", "dispersing"};
    for (const char* a : kFight) if (pe.act == a) return true;
    for (const char* a : kRadio) if (pe.act == a) return !SameName(pe.arg, caller);
    return false;
}
// the dolls closer to the caller that would go with him (DollRank without the busy ones, the ones too
// far and his followers already: the peers' status); without the peers' files, DollRank
int FreeRank(const Player& caller) {
    if (!ReadPeers()) return DollRank(caller);
    const float mine = Dist2D(gMe.pos, caller.pos);
    int r = 0;
    for (const Player& p : gPlayers) {
        if (p.local || p.id == caller.id || p.id == gMyId || !Ally(p) || !p.alive || !IsDollName(p.name)) continue;
        const float d = Dist2D(p.pos, caller.pos);
        if (d >= mine || d > 60000.f) continue;
        const Peer* pe = nullptr;
        for (const Peer& x : gPeers) if (SameName(x.name, p.name)) pe = &x;
        if (pe && (PeerBusy(*pe, caller.name) || ((pe->act == "following_codec" || pe->act == "covering" || pe->act == "taking_cover") && SameName(pe->arg, caller.name)))) continue;
        ++r;
    }
    return r;
}
// the leader followed: stopped and aiming at an enemy (his weapon up, an enemy within 12 degrees of his
// facing, 60 m at most): its followers take cover by him and shoot that enemy (PickTarget)
struct LeaderWatch { int id = -1, last = -1; float pos[3] = {0, 0, 0}; long long stillSince = -1, lastAt = -100000; };
LeaderWatch gLw;
int LeaderAim(const Player& l) {
    if (gLw.id != l.id) { gLw = LeaderWatch(); gLw.id = l.id; memcpy(gLw.pos, l.pos, sizeof gLw.pos); }
    if (Dist2D(gLw.pos, l.pos) > 400.f) { memcpy(gLw.pos, l.pos, sizeof gLw.pos); gLw.stillSince = gFrame; }
    else if (gLw.stillSince < 0) gLw.stillSince = gFrame;
    int best = -1; float bestErr = 12.f * kDeg;
    if (gFrame - gLw.stillSince >= 15 && (l.bits[1] & 0x4400))
        for (const Player& e : gPlayers) {
            if (!Enemy(e) || !e.alive || Dist2D(l.pos, e.pos) > 60000.f) continue;
            const float err = std::fabs(Wrap(AngleTo(l.pos, e.pos) - l.facing));
            if (err < bestErr) { bestErr = err; best = e.id; }
        }
    if (best >= 0) { gLw.last = best; gLw.lastAt = gFrame; return best; }
    const Player* e = gLw.last >= 0 ? PlayerById(gLw.last) : nullptr;       // (a second more: no flicker)
    return e && e->alive && gFrame - gLw.lastAt < 60 ? gLw.last : -1;
}
// a place to shoot from by the leader: 1.5 to 3.5 m from him, out of his line of fire, crouched out of the
// enemy's sight and standing in it (a low wall, a corner) the best, near the doll
struct CoverRun { int target = -1; float at[3] = {0, 0, 0}; bool have = false, reached = false; long long when = -100000; };
CoverRun gCv;
bool CoverSpot(const Player& leader, const Player& enemy, float* out) {
    if (!MapReady()) return false;
    float eye[3]; BodyPoint(enemy.pos, 0.9f, eye);
    const float toE = AngleTo(leader.pos, enemy.pos);
    float bestS = -1e30f; bool any = false;
    for (int i = 0; i < 16; ++i) {
        const float a = toE + (i % 8 - 3.5f) * 22.5f * kDeg + (i >= 8 ? kDeg * 180.f : 0.f);
        const float r = i >= 8 ? 1500.f : 2800.f;
        if (std::fabs(Wrap(a - toE)) < 35.f * kDeg) continue;                 // (not in front of him)
        const float c[3] = {leader.pos[0] + std::sin(a) * r, leader.pos[1] - 800.f, leader.pos[2] + std::cos(a) * r};
        float q[3]; uint8_t area = 0;
        if (!gMap->nav->Nearest(c, 600.f, q, &area) || !gMap->nav->Walkable(leader.pos, q)) continue;
        const float low[3] = {q[0], q[1] + 900.f, q[2]}, high[3] = {q[0], q[1] + 1450.f, q[2]};
        const bool hidden = !LineClear(eye, low), shoots = LineClear(eye, high);
        if (!shoots) continue;                                              // (it must see him standing)
        const float sc = (hidden ? 3000.f : 0.f) - Dist2D(q, gMe.pos) * 0.3f;
        if (sc > bestS) { bestS = sc; memcpy(out, q, sizeof q); any = true; }
    }
    return any;
}
// every frame of the brain: the enemy the leader followed aims at (PickTarget's first), a fight or not
void LeaderWatchFrame() {
    const Player* l = gCo.kind == CK_FOLLOW ? PlayerById(gCo.who) : nullptr;
    const int was = gLeaderTarget;
    gLeaderTarget = l && l->alive && SameName(l->name, gCo.name) ? LeaderAim(*l) : -1;
    if (gLeaderTarget != was && gLeaderTarget >= 0 && gDebug) {
        const Player* e = PlayerById(gLeaderTarget);
        Log("codec: %s shoots at %s", l->name.c_str(), e ? e->name.c_str() : "?");
    }
}
// the leader shooting: to the cover spot by him before the fight, when it is close (6 m), the enemy not close
// (8 m) and the doll not hurt a moment ago
bool CoverFirst(const Player& enemy) {
    if (gCo.kind != CK_FOLLOW || gLeaderTarget < 0) return false;
    const Player* l = PlayerById(gCo.who);
    const Player* e = PlayerById(gLeaderTarget);
    if (!l || !e) return false;
    if (!gCv.have || gCv.target != gLeaderTarget || (gFrame - gCv.when > 300 && !gCv.reached)) {
        if (gCv.target != gLeaderTarget) gCv.reached = false;
        gCv.have = CoverSpot(*l, *e, gCv.at); gCv.target = gLeaderTarget; gCv.when = gFrame;
        if (gCv.have && gDebug) Log("codec: cover by %s at %.0f %.0f %.0f against %s", l->name.c_str(), gCv.at[0], gCv.at[1], gCv.at[2], e->name.c_str());
    }
    const float d = gCv.have ? Dist2D(gMe.pos, gCv.at) : 0.f;
    if (gCv.have && d < 700.f) gCv.reached = true;                 // (there once for this enemy: the fight from where it stands now)
    if (!gCv.have || gCv.reached || d > 6000.f || Dist2D(gMe.pos, enemy.pos) < 8000.f || gFrame - gHurtAt < 60) return false;
    Act("taking_cover", l->name);
    GoTo(gCv.at, "cover", 600.f);
    return true;
}
bool AllyWithin(float r, bool realOnly = false) {
    for (const Player& p : gPlayers)
        if (Ally(p) && p.alive && (!realOnly || !IsDollName(p.name)) && Dist2D(p.pos, gMe.pos) < r) return true;
    return false;
}
void Answer(int phrase, const std::string& why, int rank) {
    gReply.phrase = phrase; gReply.why = why; gReply.at = gFrame + ReactFrames() + rank * 60;
    if (gDebug) Log("codec: \"%s\" in %lld frames (%s)", PhraseName(phrase), gReply.at - gFrame, why.c_str());
}
void CodecEnd(const char* why) {
    if (gCo.kind == CK_NONE) return;
    Log("codec: end of %s's \"%s\" (%s)", gCo.name.c_str(), PhraseName(gCo.phrase), why);
    gCo = CodecOrder(); gLeaderTarget = -1; gCv.have = false;
    StopPath();
}
void CodecStart(int kind, const Player& p, int phrase, int seconds, const float* at = nullptr) {
    gCo = CodecOrder(); gLeaderTarget = -1; gCv.have = false; gCo.kind = kind; gCo.who = p.id; gCo.name = p.name; gCo.phrase = phrase; gCo.until = gFrame + seconds * 60LL;
    memcpy(gCo.at, at ? at : gMe.pos, sizeof gCo.at);
    StopPath();
    if (kind == CK_FOLLOW) Log("codec: following %s for %d s (\"%s\")", p.name.c_str(), seconds, PhraseName(phrase));
    else if (kind == CK_HOLD) Log("codec: holding for %s for %d s (\"%s\")", p.name.c_str(), seconds, PhraseName(phrase));
    else Log("codec: going to %s (\"%s\")", p.name.c_str(), PhraseName(phrase));
}
bool Stunned() { return (gMe.bits[0] & 0x1A000000) != 0; }   // (knocked out, asleep: no radio, no answer)
void Disperse(const Player& caller);
bool LeaderFight(const Player& l);
// a call heard (from the network: every player's, the other side's too, which a player is not shown)
void OnCall(const Call& c) {
    const Player* p = PlayerById(c.from);
    if (!p || c.team != gMyTeam) return;                   // (shown only to the caller's side)
    if (Jammed()) { Log("codec: \"%s\" from %s not heard (jammed by a chaff grenade)", PhraseName(c.phrase), p->name.c_str()); return; }
    gSideSaidAt[c.phrase] = gFrame;
    Log("codec: heard \"%s\" from %s", PhraseName(c.phrase), p->name.c_str());
    if (!Ally(*p) || !gMe.alive || gMe.frozen || gRoom.phase != 2) return;   // (deathmatch: nobody's ally)
    if (Stunned()) { Log("codec: %s's \"%s\": nothing (stunned)", p->name.c_str(), PhraseName(c.phrase)); return; }
    const float d = Dist2D(gMe.pos, p->pos);
    const int rank = DollRank(*p);
    const char* busy = CodecBusy(*p);
    const std::string why = "answering " + p->name + "'s \"" + PhraseName(c.phrase) + "\"";
    auto decline = [&](const char* how) { Log("codec: %s's \"%s\": not done (%s)", p->name.c_str(), PhraseName(c.phrase), how); };
    const bool mine = gCo.kind != CK_NONE && gCo.who == c.from;
    switch (c.phrase) {
    case PH_FOLLOW_ME: case PH_TOGETHER: case PH_COVER: case PH_COME_HERE: case PH_PULL_OUT: {
        if (!p->alive) return;
        const bool follow = c.phrase == PH_FOLLOW_ME || c.phrase == PH_TOGETHER || c.phrase == PH_COVER;
        // too far (60 m), or in a fight: negative (the two dolls closest to him say it, the others nothing);
        // two dolls with him already: the others stay silent; the ones that go: the closest free ones
        if (d > 60000.f) { decline("too far"); if (rank < 2) Answer(PH_NEGATIVE, why, rank); return; }
        if (gFi.target >= 0 && !mine) { decline("in a fight"); if (rank < 2) Answer(PH_NEGATIVE, why, rank); return; }
        if (busy && !mine) { decline(busy); if (rank < 2 && Rnd01() < Chatty()) Answer(PH_NEGATIVE, why, rank); return; }
        const int free = FreeRank(*p);
        if (follow && !mine) {
            const int with = FollowersOf(p->name);
            if (with >= 2) { decline("two dolls follow him already"); return; }
            if (free >= 2 - std::max(0, with)) { decline("another doll is closer"); return; }
        } else if (!mine && free >= (c.phrase == PH_PULL_OUT ? 3 : 2)) { decline("another doll is closer"); return; }
        if (!mine && Rnd01() >= Obeys()) { decline("its level"); if (Rnd01() < 0.4f * Chatty()) Answer(PH_NEGATIVE, why, rank); return; }
        const int s = c.phrase == PH_TOGETHER ? 90 : c.phrase == PH_FOLLOW_ME ? 75 : c.phrase == PH_COVER ? 45 : 30;
        if (c.phrase == PH_COME_HERE || c.phrase == PH_PULL_OUT) CodecStart(CK_GOTO, *p, c.phrase, s, p->pos);
        else CodecStart(CK_FOLLOW, *p, c.phrase, s);
        if (Rnd01() < Chatty()) Answer(PH_ROGER, why, rank);
        else if (gDebug) Log("codec: no answer (level %.2f)", LevelNow());
        return;
    }
    case PH_HOLD: {
        if (!mine && (d > 25000.f || rank >= 2)) return;          // (a hold is for those around the caller)
        if (!mine && gFi.target >= 0) { decline("in a fight"); if (rank == 0) Answer(PH_NEGATIVE, why, rank); return; }
        if (busy && !mine) { decline(busy); if (rank == 0 && Rnd01() < Chatty()) Answer(PH_NEGATIVE, why, rank); return; }
        if (!mine && Rnd01() >= Obeys()) { decline("its level"); return; }
        CodecStart(CK_HOLD, *p, c.phrase, 25);
        if ((mine || rank == 0) && Rnd01() < Chatty()) Answer(PH_ROGER, why, rank);
        return;
    }
    case PH_GO:
        if (mine) {
            const bool was = gCo.kind == CK_FOLLOW;
            CodecEnd("go go go");
            if (was) Disperse(*p);                                 // (the ones following him spread out)
            if (Rnd01() < 0.4f * Chatty()) Answer(PH_ROGER, why, rank);
        }
        return;
    case PH_DEFEND: {
        // the objective played harder a while, as the side's role wants it (the takers take, the guards
        // guard: section 5); team deathmatch has none: by the caller a while
        if (gRule == 1) {
            if (rank >= 2 || d > 40000.f) return;
            if (busy || gFi.target >= 0) { decline(busy ? busy : "in a fight"); if (rank == 0) Answer(PH_NEGATIVE, why, rank); return; }
            if (Rnd01() >= Obeys()) { decline("its level"); return; }
            CodecStart(CK_HOLD, *p, c.phrase, 40, p->pos);
            if (Rnd01() < Chatty()) Answer(PH_ROGER, why, rank);
            return;
        }
        if (Rnd01() >= Obeys()) { decline("its level"); return; }
        ObjectiveFocus(90, p->name);
        if (rank <= 1 && Rnd01() < Chatty()) Answer(PH_ROGER, why, rank);
        return;
    }
    case PH_ENEMY: case PH_HIT: {                                 // toward the caller (a fight there)
        if (!p->alive || busy || rank >= 2 || d > 40000.f || d < 6000.f || gCo.kind != CK_NONE) return;
        if (Rnd01() >= Obeys()) return;
        CodecStart(CK_GOTO, *p, c.phrase, 25, p->pos);
        if (c.phrase == PH_HIT && Rnd01() < 0.5f * Chatty()) Answer(PH_ROGER, why, rank);
        return;
    }
    default:
        return;
    }
}
// what an ally asked, done (true: the frame's moves)
bool CodecOrderFrame() {
    if (gCo.kind == CK_NONE) return false;
    const Player* p = PlayerById(gCo.who);
    const char* end = nullptr;
    if (!p || !SameName(p->name, gCo.name)) end = "the caller left";
    else if (!Ally(*p)) end = "the caller changed sides";
    else if (!p->alive) end = "the caller died";
    else if (gFrame >= gCo.until) end = "its time is up";
    else if (Carrying()) end = "the objective on it";
    else if (gSet.order == "defend" || gSet.order == "hide") end = "another order";
    if (end) { CodecEnd(end); return false; }
    switch (gCo.kind) {
    case CK_FOLLOW: {
        if (Jammed()) {                                            // (a chaff out: no radar, the leader followed in sight only)
            float eye[3], head[3]; MyEye(eye); BodyPoint(p->pos, 0.95f, head);
            if (Dist2D(gMe.pos, p->pos) > 30000.f || !LineClear(eye, head)) { Act("jammed", p->name); StopPath(); Steer(0, 0, 0); return true; }
        }
        if (LeaderFight(*p)) return true;                         // (its activity: covering)
        Act("following_codec", p->name);
        Follow(*p, 3000.f + 1500.f * std::min(2, DollRank(*p)));
        return true;
    }
    case CK_HOLD:
        Act("holding", p->name);
        if (Dist2D(gMe.pos, gCo.at) > 2000.f) { GoTo(gCo.at, "hold", 1200.f); return true; }
        StopPath(); Steer(0, 0, 0);
        return true;
    default:
        if (gCo.phrase == PH_GO) {                                 // (spreading out after GO GO GO)
            Act("dispersing", p->name);
            if (GoTo(gCo.at, "disperse", 1500.f)) { CodecEnd("spread out"); return false; }
            return true;
        }
        Act("going_to_ally", p->name);
        if (GoTo(gCo.at, "codec", 3000.f)) { CodecEnd("arrived"); return false; }
        return true;
    }
}
// GO GO GO to its followers: each one away from him a moment, 8 to 14 m, its own way (by its rank)
void Disperse(const Player& caller) {
    if (!MapReady()) return;
    const int rank = DollRank(caller);
    const float base = caller.facing + (rank % 2 ? 1.f : -1.f) * (45.f + 25.f * (rank / 2)) * kDeg;
    // a reachable point 6 to 14 m from him, its own way first, any way then (a small map, walls)
    for (int i = 0; i < 24; ++i) {
        const float spread = i < 12 ? 40.f : 300.f;
        const float a = base + (Rnd01() - 0.5f) * spread * kDeg, r = 6000.f + Rnd01() * 8000.f;
        const float c[3] = {caller.pos[0] + std::sin(a) * r, caller.pos[1] - 800.f, caller.pos[2] + std::cos(a) * r};
        float q[3]; uint8_t area = 0;
        if (!gMap->nav->Nearest(c, 3000.f, q, &area) || Dist2D(q, caller.pos) < 5000.f) continue;
        if (!gMap->nav->Walkable(gMe.pos, q) && !PlanTo(q)) continue;          // (a way there)
        StopPath();
        CodecStart(CK_GOTO, caller, PH_GO, 12, q);
        return;
    }
    Log("codec: no place to spread out to from %s", caller.name.c_str());
}
// following a leader who stops to shoot: cover by him (crouched there at 4 and up), his enemy the target
// (PickTarget); true: the frame's moves made
bool LeaderFight(const Player& l) {
    const int t = gLeaderTarget;                               // (LeaderWatchFrame)
    if (t < 0) { gCv.have = false; return false; }
    const Player* e = PlayerById(t);
    if (!e) return false;
    Act("covering", l.name);
    if (!gCv.have || gCv.target != t || (gFrame - gCv.when > 300 && !gCv.reached)) {
        if (gCv.target != t) gCv.reached = false;
        gCv.have = CoverSpot(l, *e, gCv.at); gCv.target = t; gCv.when = gFrame;
        if (gCv.have && gDebug) Log("codec: cover by %s at %.0f %.0f %.0f against %s", l.name.c_str(), gCv.at[0], gCv.at[1], gCv.at[2], e->name.c_str());
    }
    if (gCv.have && !gCv.reached && !GoTo(gCv.at, "cover", 600.f)) return true;
    if (gCv.have) gCv.reached = true;
    else if (Dist2D(gMe.pos, l.pos) > 4000.f) { GoTo(l.pos, "cover", 3000.f); return true; }   // (no cover spot: by him)
    StopPath(); Steer(0, 0, 0);
    if (Lv() >= 4) SetLow(true);                              // (up again before it walks: FollowPath waits for it)
    return true;
}
// its own calls, as a teammate makes them (its level, never too often, never what its side has just said)
bool OwnCall(int phrase, const std::string& why, float chance, int ownGapS, int sideGapS, bool urgent = false) {
    if (gRule == 0 || IAmSnake() || RadioBusy() || gReply.phrase >= 0) return false;
    if (gFrame - gOwnCallAt < 8 * 60) return false;
    auto recent = [](const std::map<int, long long>& m, int ph, int s) { auto it = m.find(ph); return it != m.end() && gFrame - it->second < s * 60LL; };
    if (recent(gSaidAt, phrase, ownGapS) || recent(gSideSaidAt, phrase, sideGapS)) return false;
    if (Rnd01() >= chance) { gSaidAt[phrase] = gFrame - ownGapS * 30LL; return false; }   // (not now: asked again in half the gap)
    if (!RadioSay(phrase, why, urgent)) return false;
    gSaidAt[phrase] = gFrame; gOwnCallAt = gFrame;
    if (phrase == PH_FOLLOW_ME && gCo.kind != CK_NONE) CodecEnd("it leads now");
    return true;
}
void OwnCalls() {
    if (gRoom.phase != 2 || !AllyWithin(1e9f)) return;
    // go go go: the round's start, the first doll of its side (the lowest id), with a real player on it
    if (gRoundGoAt > 0 && gFrame >= gRoundGoAt) {
        gRoundGoAt = -1;
        bool first = true;
        for (const Player& p : gPlayers) if (Ally(p) && IsDollName(p.name) && p.id < gMyId) first = false;
        if (first && AllyWithin(1e9f, true)) OwnCall(PH_GO, "the round starts", 0.6f * Talks(), 120, 30);
    }
    // an enemy sighted: a new target, just seen
    if (gFi.target >= 0 && gFi.target != gCodecTarget) {
        const Player* e = PlayerById(gFi.target);
        auto k = gKnown.find(gFi.target);
        // (v137) urgent (said before the first shot, the gun lowered 10 to 40 frames: v136's audit, tdm/d3.txt f5480, a fight
        // at 28 m begun, the call took the pad from f5491 to f5512, no shot) at levels 1 and 2 only, or with him far (25 m) and
        // not aiming at it, the doll unhurt; else said in a calm moment of the fight
        const bool threat = e && (Dist2D(e->pos, gMe.pos) < 25000.f || gFrame - gHurtAt < 120 ||
                                  (std::fabs(Wrap(AngleTo(e->pos, gMe.pos) - e->facing)) < 20.f * kDeg && (e->bits[1] & 0x4400) != 0));
        if (e && k != gKnown.end() && gFrame - k->second.first < 90 && AllyWithin(50000.f))
            OwnCall(PH_ENEMY, "enemy " + e->name + " seen", Talks(), 20, 10, Lv() <= 2 || !threat);
    }
    // take cover: hit by an enemy it does not see (no enemy in sight, a moment after it was hurt; "I'm
    // hit" is on no list of the radio)
    if (gFi.target < 0 && gFrame - gHurtAt < 30 && gFrame - gHurtAt > 5 && AllyWithin(30000.f)) {
        bool seen = false;
        for (const auto& k : gKnown) if (k.second.visible) seen = true;
        if (!seen) OwnCall(PH_TAKE_COVER, "hit, the enemy not seen", 0.7f * Talks(), 30, 15);
    }
    // all clear: its fight over, its target down, no other enemy about
    if (gFi.target < 0 && gCodecTarget >= 0) {
        const Player* e = PlayerById(gCodecTarget);
        bool others = false;
        for (const auto& k : gKnown) if (gFrame - k.second.last < 300) others = true;
        if (e && !e->alive && !others && AllyWithin(50000.f)) OwnCall(PH_CLEAR, "the fight is over", 0.35f * Talks(), 60, 20);
    }
    gCodecTarget = gFi.target;
    // backup: the objective on it with an enemy close, or hurt in a fight; follow me: the objective on it
    // with an ally close (an escort)
    const bool carrying = Carrying();
    bool enemyClose = false;
    for (const auto& k : gKnown) if (gFrame - k.second.last < 180 && Dist2D(k.second.pos, gMe.pos) < 25000.f) enemyClose = true;
    if (carrying && enemyClose && AllyWithin(50000.f)) OwnCall(PH_COVER, "the objective on it", Talks(), 40, 15);
    else if (carrying && AllyWithin(20000.f)) OwnCall(PH_FOLLOW_ME, "the objective on it", Talks(), 60, 20);
    else if (gFi.target >= 0 && gMyMaxLife > 0 && gMe.life < gMyMaxLife * 0.4f && AllyWithin(40000.f))
        OwnCall(PH_COVER, "hurt in a fight", 0.6f * Talks(), 40, 15, Lv() <= 2);   // ((v137) urgent, its gun lowered under fire: levels 1 and 2 only)
    // nice job: an ally close gets a kill; don't die: an ally close is badly hurt
    if (gFrame - gCodecKillsAt >= 60) {
        gCodecKillsAt = gFrame;
        for (const Player& p : gPlayers) {
            if (!Ally(p)) continue;
            uint8_t v[0x20];
            if (!ps2world::VariableSet(p.id + 1, v, sizeof v)) continue;
            const int k = v[0x14] | (v[0x15] << 8);
            auto it = gAllyKills.find(p.id);
            const bool killed = it != gAllyKills.end() && k > it->second;
            gAllyKills[p.id] = k;
            if (killed && p.alive && Dist2D(p.pos, gMe.pos) < 30000.f) OwnCall(PH_NICE, p.name + "'s kill", 0.3f * Talks(), 90, 30);
            auto m = gMaxLife.find(p.id);
            if (p.alive && m != gMaxLife.end() && m->second > 0 && p.life < m->second / 4 && Dist2D(p.pos, gMe.pos) < 15000.f &&
                gFrame - gDontDieAt[p.id] > 120 * 60) {
                if (OwnCall(PH_DONT_DIE, p.name + " hurt", 0.25f * Talks(), 120, 60)) gDontDieAt[p.id] = gFrame;
            }
        }
    }
}
// the bench's calls: MGO1_DOLL_RADIO_TEST "frames:phrase,..." (frames from its first frame alive in the round;
// the phrase 99 the pause menu's SUICIDE, 98 an emote)
void RadioTest(long long sinceAlive) {
    static const char* e = getenv("MGO1_DOLL_RADIO_TEST");
    if (!e || sinceAlive < 0) return;
    for (const char* q = e; *q;) {
        long long f = 0; int ph = -1;
        if (sscanf(q, "%lld:%d", &f, &ph) == 2 && f == sinceAlive && ph == 99) PauseJobStart(PJ_SUICIDE, 0);
        else if (sscanf(q, "%lld:%d", &f, &ph) == 2 && f == sinceAlive && ph == 98) {   // (98: an emote, at the nearest ally)
            int to = -1; float bd = 1e30f;
            for (const Player& a : gPlayers) if (Ally(a) && a.alive && Dist2D(a.pos, gMe.pos) < bd) { bd = Dist2D(a.pos, gMe.pos); to = a.id; }
            EmoteWant(to, 0, "test");
        }
        else if (sscanf(q, "%lld:%d", &f, &ph) == 2 && f == sinceAlive && ph >= 0 && ph < kPhrases) {
            if (!RadioSay(ph, "test")) Log("codec: test \"%s\" not started (%s)", PhraseName(ph), RadioBusy() ? "busy" : Jammed() ? "jammed by a chaff grenade" : "not on its list");
            else if (ph == PH_FOLLOW_ME && gCo.kind != CK_NONE) CodecEnd("it leads now");
        }
        const char* c = strchr(q, ','); if (!c) break; q = c + 1;
    }
}
long long gAliveSince = -1;
// every frame alive in the world: the calls heard, the answer due, its own calls, the presses of a call
void CodecFrame(bool calm, bool quiet) {
    RadioFrame();
    Call c;
    while (RadioHeard(&c)) OnCall(c);
    if (gAliveSince < 0) gAliveSince = gFrame;
    if (gWasFrozen) { gWasFrozen = false; gRoundGoAt = gFrame + 60 + (long long)(Rnd() % 90); }
    RadioTest(gFrame - gAliveSince);
    if (gReply.phrase >= 0 && Stunned()) gReply = Reply();     // (stunned: nothing said)
    if (gReply.phrase >= 0 && gFrame >= gReply.at && !RadioBusy()) {
        if (gFrame - gReply.at < 300 && RadioSay(gReply.phrase, gReply.why)) gSaidAt[gReply.phrase] = gFrame;
        gReply = Reply();
    }
    if (!RadioBusy() && !Stunned()) OwnCalls();
    RadioPad(calm, quiet);
}
// out of the world (dead, a menu): the calls still heard; a call being made given up
void CodecMenuFrame() {
    RadioFrame();
    Call c;
    while (RadioHeard(&c)) OnCall(c);
    RadioStop(gMe.obj && !gMe.alive ? "dead" : "a menu");
    gReply = Reply();
    gAliveSince = -1;
    if (gMe.frozen || !gMe.obj) gWasFrozen = true;
    if (gCo.kind == CK_HOLD || gCo.kind == CK_GOTO) { if (gMe.obj && !gMe.alive) CodecEnd("it died"); }
}
}  // namespace
void CodecNewMap() {
    gCo = CodecOrder(); gReply = Reply(); gSaidAt.clear(); gSideSaidAt.clear(); gAllyKills.clear(); gDontDieAt.clear(); gLeaderTarget = -1;
    gCodecTarget = -1; gOwnCallAt = -100000; gRoundGoAt = -1; gWasFrozen = true; gAliveSince = -1;
    RadioStop("a new map");
}

int gLastPhase = -1;
void RoundWatch() {                                     // a new round: Snake chosen again, targets forgotten
    if (gRoom.phase == gLastPhase) return;
    if (gRoom.phase == 1 || gRoom.phase == 5) {
        gMaxLife.clear(); gMyMaxLife = 0; gKnown.clear(); gSpotHave = false; gHideHave = false; gSneakFirstHave = false; gDefendHave = false;
        gCoveredUntil.clear(); gLeftUntil.clear(); gDryLeftUntil.clear(); gVainUntil.clear(); gWallLeftUntil.clear(); gChaseDoneId = -1;
        CodecEnd("the round is over"); gReply = Reply(); gWasFrozen = true; gObjFocusUntil = -1;
        gSpecTries = 0; gSpecBarredSaid = false;
    }
    gLastPhase = gRoom.phase;
}

long long gBurningUntil = -1;
bool gFleeing = false;
// the inventory in the log (debug): at once when its codes or the hands change, else every 20 s at most
std::string gInvCodes, gInvAll;
long long gInvLogAt = -100000;
void InventoryLog() {
    if (!gDebug) return;
    const Inventory& v = Inv();
    if (!v.ok) return;
    char b[400]; int n = snprintf(b, sizeof b, "inventory: type %d; in the hands %x, item %x; weapons", MyType(), v.weapon, v.item);
    std::string codes(b);
    for (int c : v.weapons) { n += snprintf(b + n, sizeof b - n, " %x:%d", c, v.rounds[c]); codes += " " + std::to_string(c); }
    n += snprintf(b + n, sizeof b - n, " | items");
    for (int c : v.items) { n += snprintf(b + n, sizeof b - n, " %x:%d", c, v.count[c]); codes += " i" + std::to_string(c); }
    if (codes == gInvCodes && (b == gInvAll || gFrame - gInvLogAt < 600)) return;
    gInvCodes = codes; gInvAll = b; gInvLogAt = gFrame;
    Log("%s", b);
}
int RestWeapon() {
    if (IAmSnake()) { if (HasWeapon(5)) return 5; const int h = Handgun(); return h > 0 ? h : MainGun(); }   // (silent darts, the CQC)
    if (Sniper()) { const int h = Handgun(); return h > 0 ? h : MainGun(); }
    const int g = MainGun();
    if (HeadshotsOnly() && (g == 0xE || g == 9 || g == 0x11 || g <= 0) && Handgun() > 0) return Handgun();   // (headshots only)
    return g > 0 ? g : Handgun();
}
// ---- the fight's moves (v136): what a player does besides aiming, every level. Its magazine emptied in a
// fight (the game reloads it at the next press of the trigger, 1.8 s, the player free to run meanwhile:
// probe 31; no reload before it is empty): the trigger pressed, then to a place out of the enemies' sight
// within 7 m while it reloads (none: across his line and back), then the corner toward him held a moment.
// A fight at range going on (4 to 8 s), or hurt twice without hurting him, or shots for nothing: to another
// place that sees him (another angle, a low cover), 2.4 to 8 m away. Losing it (a third of its life and
// hurt, or two enemies aiming at it and under 60 % of its life): back out of their sight, toward its allies
// (its box on for the run when none sees it), then the corner he would come round held. The feint, now and then when it is
// not winning: out of his sight on purpose, the corner held, the gun on it. The corner held: the first person
// view on the point where he would appear (the way from its place to him: the last point it sees), crouched,
// 2 to 8 s; he comes round: the gun is on him (its reaction short: FightFrame); in its box instead, it waits
// for him to pass. An enemy gone out of sight in a fight: his corner watched 1.5 to 3.5 s before the chase ----
enum TacKind { TC_NONE = 0, TC_RELOAD, TC_MOVE, TC_BACK, TC_FEINT, TC_HOLD };
const char* const kTcName[] = {"none", "reload", "move", "fall back", "feint", "hold"};
struct Tactic {
    int kind = TC_NONE, foe = -1;
    float spot[3] = {0, 0, 0}, watch[3] = {0, 0, 0};
    bool have = false, crouch = false, reached = false, box = false, strafe = false, watchHave = false, crouched = false;
    long long at = -100000, until = 0, reachedAt = -1, crossAt = -100000, pressAt = -1, begunAt = -1, untilMax = 0;
    bool begun = false;
    float strafeDir = 0; long long strafeFrames = 0;
    const char* why = "";
    bool guard = false;                              // (one-life defenders) a guard's hold by the objective
};
Tactic gTc;
long long gGuardHoldAt = -1;                         // (one-life defenders) its next hold from (after a round, or at once)
bool gGuardRound = false;                            // (its round to another point of its sector going on)
long long gGuardRoundAt = -1;                        // (its next round's planning from: once in 1.5 s at most)
float gGuardFrom[3] = {0, 0, 0}, gGuardItem[3] = {0, 0, 0};   // (the hold's way in, the item where it was then)
long long gTcEndAt = -100000, gTcSearchAt = -100000, gTcFeintAt = -100000, gTcBackAt = -100000, gTcNextMove = -1, gTcFightAt = -1;
int gTcFight = -1, gTcMoves = 0;                     // the foe of the fight its timings are for, its moves in that fight
long long gTcPeekAt = -1; int gTcPeeks = 0;          // ((v137) the master tier's "shot and hidden": the next one from, how many this fight)
long long gTcReloadOff = -1;                         // (a reload that did not begin: not tried again until)
int gTcDryAt = 0;                                    // (the fight's shots for nothing when its last tactic ended or move began)
float gStyle = 0.5f;                                 // this life's taste: 0 careful .. 1 bold
std::map<int, int> gFoeLife;                         // each foe's life as last seen
std::map<int, long long> gDealtAt;                   // each foe's life last going down
int HitsWithin(long long frames) { int n = 0; for (long long f : gHits) if (gFrame - f <= frames) ++n; return n; }
// the magazine of the gun in the hands (-1 not known)
int MagNow() {                                       // (-1 too for the M37, its shells one by one, and the rifles whose view locks)
    const int h = Hand();
    const uint32_t st = h > 1 && h < 0x13 && h != 0xE && !LocksView(h) ? WeaponState(h) : 0;
    return st && Inv().ok ? R16s(st + 0x1C) : -1;
}
// ((v141 beta 8) probe 31's precision step) the first person view aimed at his head (part 0), his chest (1) or a share of his hips'
// height above them (above > 0) by the fights' aim; its errors and the view's turns (degrees); false: no view matrix
bool ProbeAimAt(const Player& p, int part, float* ey, float* ep, float* ry, float* rp, float above) {
    float m[16];
    if (!ps2world::ViewMatrix(m)) return false;
    float at[3];
    if (above > 0.f) BodyPoint(p.pos, above, at); else if (part == 0) HeadPoint(p, at); else ChestPoint(p, at);
    const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    const float tx = at[0] - m[12], tz = at[2] - m[14], ty = at[1] - m[13];
    const float tYaw = std::atan2(tx, tz), tPitch = std::atan2(ty, std::sqrt(tx * tx + tz * tz));
    AimView(camYaw, camPitch, tYaw, tPitch, 1.f, 0.05f, 0.05f * kDeg, 0.05f * kDeg);
    *ey = Wrap(tYaw - camYaw) / kDeg; *ep = (tPitch - camPitch) / kDeg;
    *ry = gAimY.rate; *rp = gAimP.rate;
    return true;
}
void ProbeAimReset() { gAimSince = gFrame; }
struct Threat { int id; float pos[3], eye[3], chest[3]; bool visible; };
std::vector<Threat> Threats(long long within) {      // the enemies known these frames (seen or heard), alive, awake
    std::vector<Threat> out;
    for (const auto& k : gKnown) {
        if (gFrame - k.second.last > within) continue;
        const Player* p = PlayerById(k.first);
        if (!p || !Enemy(*p) || !p->alive || Asleep(*p)) continue;
        Threat t; t.id = k.first; t.visible = k.second.visible;
        memcpy(t.pos, k.second.visible ? p->pos : k.second.pos, sizeof t.pos);
        BodyPoint(t.pos, 0.95f, t.eye); BodyPoint(t.pos, 0.45f, t.chest);
        out.push_back(t);
    }
    return out;
}
// a body standing at a point of the mesh against an eye: 2 seen (crouched too), 1 seen standing only (a low
// cover), 0 hidden (its head, chest and shoulders)
int SeenAt(const float* q, const float* eye) {
    const float dx = q[0] - eye[0], dz = q[2] - eye[2], l = std::max(1.f, std::sqrt(dx * dx + dz * dz));
    const float sx = -dz / l * 250.f, sz = dx / l * 250.f;
    const float head[3] = {q[0], q[1] + 1500.f, q[2]}, chest[3] = {q[0], q[1] + 1150.f, q[2]};
    const float sl[3] = {q[0] + sx, q[1] + 1150.f, q[2] + sz}, sr[3] = {q[0] - sx, q[1] + 1150.f, q[2] - sz};
    if (!LineClear(eye, head) && !LineClear(eye, chest) && !LineClear(eye, sl) && !LineClear(eye, sr)) return 0;
    const float ch[3] = {q[0], q[1] + 950.f, q[2]}, cc[3] = {q[0], q[1] + 750.f, q[2]};
    return LineClear(eye, ch) || LineClear(eye, cc) ? 2 : 1;
}
bool SeesFrom(const float* q, const float* chest) {  // from a point of the mesh, standing or crouched, a chest seen
    const float se[3] = {q[0], q[1] + 1550.f, q[2]}, ce[3] = {q[0], q[1] + 1000.f, q[2]};
    return LineClear(se, chest) || LineClear(ce, chest);
}
// in its cardboard box: its actor seen (BoxOn, every half second), or the box the item in its hands just taken
bool InMyBox() { return BoxOn() || (ItemInHands() == 0xE && gFrame - gBoxTakenAt < 90); }
bool SeenByAny(const std::vector<Threat>& th) {      // the doll in the sight of an enemy known these frames (where seen, or last known)
    float me[3]; BodyPoint(gMe.pos, 0.6f, me);
    for (const Threat& t : th) if (LineClear(t.eye, me)) return true;   // (not only the ones it sees: its back turned, he sees it)
    return false;
}
// the places around the doll on its part of the mesh (rings every 1.2 m, 16 a ring, turned at random), no crawl passage
std::vector<std::array<float, 3>> SpotCands(float rMin, float rMax) {
    std::vector<std::array<float, 3>> out;
    float g[3];
    if (!MapReady() || !Ground(g)) return out;
    const int part = gMap->nav->ComponentAt(g);
    const float rot = Rnd01() * 0.39f;
    for (float r = rMin; r <= rMax + 1.f; r += 1200.f)
        for (int k = 0; k < 16; ++k) {
            const float a = rot + k * 0.3926991f, c[3] = {g[0] + std::sin(a) * r, g[1], g[2] + std::cos(a) * r};
            float q[3];
            if (!gMap->nav->Nearest(c, 700.f, q) || std::fabs(q[1] - g[1]) > 1800.f || gMap->nav->ComponentAt(q) != part || gMap->nav->AreaAt(q) == AREA_CRAWL) continue;
            bool dup = false;
            for (const auto& o : out) if (Dist2D(o.data(), q) < 500.f) dup = true;
            if (!dup) out.push_back({q[0], q[1], q[2]});
        }
    return out;
}
float WayLen(const float* to) {                      // the path's length to a point as GoTo takes it, the wide mesh first (-1: none, or a link on it: a vault, a climb, a ladder)
    float g[3]; std::vector<Waypoint> w; float len = 0;
    if (!Ground(g)) return -1.f;
    if (!(gMap->wide && gMap->wide->Path(g, to, w, &len) && w.size() >= 2 && !PathTrapped(w)) && (!gMap->nav->Path(g, to, w, &len) || PathTrapped(w))) return -1.f;
    for (const Waypoint& p : w) if (p.link) return -1.f;
    return len;
}
bool AllyAt(const float* q, float r) {
    for (const Player& a : gPlayers) if (Ally(a) && a.alive && !a.local && Dist2D(a.pos, q) < r && std::fabs(a.pos[1] - 800.f - q[1]) < 1500.f) return true;
    return false;
}
// the corner he would come round: the way from the place q to him, the last point the crouched eye at q sees
// before the way turns out of its sight (2 to 14 m from q); false: none (all of it seen, or none of it)
bool WatchCorner(const float* q, const float* foe, float* watch) {
    const float fg[3] = {foe[0], foe[1] - 800.f, foe[2]};
    float f[3];
    std::vector<Waypoint> w; float len = 0;
    if (!gMap->nav->Nearest(fg, 1500.f, f) || !gMap->nav->Path(q, f, w, &len)) return false;
    const float eye[3] = {q[0], q[1] + 1050.f, q[2]};
    float prev[3] = {q[0], q[1], q[2]}, last[3] = {0, 0, 0};
    bool any = false; float walked = 0;
    for (size_t i = 0; i < w.size() && walked < 22000.f; ++i) {
        const float* b = w[i].p;
        const float seg = std::sqrt((b[0] - prev[0]) * (b[0] - prev[0]) + (b[2] - prev[2]) * (b[2] - prev[2]));
        for (float d = 400.f; d <= seg; d += 400.f) {
            const float k = d / seg, s[3] = {prev[0] + (b[0] - prev[0]) * k, prev[1] + (b[1] - prev[1]) * k, prev[2] + (b[2] - prev[2]) * k};
            const float head[3] = {s[0], s[1] + 1400.f, s[2]};
            walked += 400.f;
            if (LineClear(eye, head)) { memcpy(last, s, sizeof last); any = true; continue; }
            if (walked < 1200.f) continue;                // (its own cover at its side)
            if (!any) return false;
            const float r = Dist2D(q, last);
            if (r < 2000.f || r > 14000.f) return false;
            memcpy(watch, last, sizeof last); watch[1] += 1450.f;   // (a head there)
            return true;
        }
        memcpy(prev, b, sizeof prev);
    }
    return false;
}
enum SpotFor { SF_RELOAD, SF_MOVE, SF_BACK, SF_FEINT };
// the best place for a move against the threats (main: the foe fought); crouch: hidden only crouched there
bool PickSpot(int purpose, const std::vector<Threat>& th, const Threat& main, float* out, bool* crouch, float* watch, bool* watchHave) {
    const float dNow = Dist2D(gMe.pos, main.pos), bearingNow = AngleTo(main.pos, gMe.pos);
    // (falling back from a fight within 15 m: a cover close by, its way out of his sight soon; v136's audit, 16
    // of 20 dolls running 4 to 14 m from a closer fight died on the way)
    const bool near = purpose == SF_BACK && dNow < 15000.f;
    const float rMin = purpose == SF_RELOAD ? 1200.f : purpose == SF_MOVE ? 2400.f : purpose == SF_BACK ? (near ? 2400.f : 4000.f) : 3000.f;
    const float rMax = purpose == SF_RELOAD ? 7200.f : purpose == SF_MOVE ? 8000.f : purpose == SF_BACK ? (near ? 7200.f : 14000.f) : 8000.f;
    const auto cands = SpotCands(rMin, rMax);
    const bool quietSide = OneLifeAttacker();
    const bool master = MasterVs(main.id);              // ((v137) the master tier against him: high ground, crossfire)
    float g0[3];
    if (!Ground(g0)) memcpy(g0, gMe.pos, sizeof g0);
    std::vector<std::array<float, 3>> food;              // (hurt, falling back: the food boxes near its places)
    if (purpose == SF_BACK && gMyMaxLife > 0 && gMe.life < gMyMaxLife / 2) {
        std::vector<Box> boxes;
        ps2world::ForEachActor(&FindBox, &boxes);
        for (const Box& b : boxes) if (b.food) food.push_back({b.p[0], b.p[1], b.p[2]});
    }
    struct Scored { float s; std::array<float, 3> p; bool crouch; };
    std::vector<Scored> sc;
    for (const auto& c : cands) {
        const float* q = c.data();
        const float dFoe = Dist2D(q, main.pos);
        if ((purpose == SF_BACK || purpose == SF_FEINT) && dFoe < dNow + 500.f) continue;   // (back: away from him)
        if (purpose == SF_MOVE && dFoe < 4000.f) continue;
        int worst = 0;                                   // (the most any threat sees of it)
        for (const Threat& t : th) { worst = std::max(worst, SeenAt(q, t.eye)); if (worst == 2) break; }
        float s = 0.f;
        if (purpose == SF_MOVE) {
            if (!SeesFrom(q, main.chest)) continue;
            const float turn = std::fabs(Wrap(AngleTo(main.pos, q) - bearingNow));
            s += worst == 1 ? 3500.f : 0.f;              // (a low cover: seen standing only)
            s += std::min(1.f, turn / (25.f * kDeg)) * 3000.f;   // (another angle on him)
            s -= std::max(0.f, dNow - dFoe) * 1.5f;      // (not closer to him)
        } else {
            if (worst == 2 || (purpose == SF_FEINT && worst == 1)) continue;   // (hidden, crouched at least; the feint's wholly)
            s += worst == 0 ? 10000.f : 6500.f;
            s += (dFoe - dNow) * (purpose == SF_BACK ? 0.5f : 0.2f);
        }
        if (AllyAt(q, 1500.f)) s -= 4000.f;
        if (quietSide && AllyAt(q, 8000.f)) s -= 3000.f;   // (one-life attackers: not toward its allies, seen it would give them away)
        // ((v137) the master tier: high ground (up to 3 m over his floor); a move: a crossfire, 40 degrees off its allies near him)
        if (master && (purpose == SF_MOVE || purpose == SF_BACK)) {
            s += std::max(0.f, std::min(3000.f, q[1] - (main.pos[1] - 800.f)));
            if (purpose == SF_MOVE) {
                bool any = false, apart = true;
                for (const Player& a : gPlayers) {
                    if (!Ally(a) || !a.alive || a.local || Dist2D(a.pos, main.pos) > 30000.f) continue;
                    any = true;
                    if (std::fabs(Wrap(AngleTo(main.pos, a.pos) - AngleTo(main.pos, q))) < 40.f * kDeg) apart = false;
                }
                if (any && apart) s += 2000.f;
            }
        }
        if (purpose == SF_BACK) {
            if (!quietSide && AllyAt(q, 8000.f)) s += 2500.f;
            for (const auto& f : food) if (Dist2D(f.data(), q) < 6000.f && std::fabs(f[1] - q[1]) < 2500.f) { s += 3000.f; break; }
            if (near && gMap->nav->Walkable(g0, q, true)) {   // (a straight way, half of it out of his sight: the run's back to him shorter)
                const float mid[3] = {(g0[0] + q[0]) * 0.5f, (g0[1] + q[1]) * 0.5f, (g0[2] + q[2]) * 0.5f};
                if (SeenAt(mid, main.eye) == 0) s += 3000.f;
            }
        }
        s -= Dist2D(gMe.pos, q) * (purpose == SF_BACK ? (near ? 1.0f : 0.5f) : purpose == SF_MOVE ? 0.35f : 1.0f);   // (the straight distance first: paths for the best only)
        sc.push_back({s, c, worst == 1});
    }
    std::sort(sc.begin(), sc.end(), [](const Scored& a, const Scored& b) { return a.s > b.s; });
    const float lenMax = purpose == SF_RELOAD ? 9000.f : purpose == SF_BACK ? (near ? 10000.f : 20000.f) : 11000.f;
    for (size_t i = 0; i < sc.size() && i < 8; ++i) {
        const float* q = sc[i].p.data();
        const float len = WayLen(q);
        if (len < 0.f || len > lenMax || len > Dist2D(gMe.pos, q) * 2.2f + 1500.f) continue;
        if (purpose != SF_MOVE) {
            float w[3];
            const bool wh = WatchCorner(q, main.pos, w);
            if (purpose == SF_FEINT && !wh) continue;    // (a feint needs its corner)
            if (wh) memcpy(watch, w, sizeof w);
            *watchHave = wh;
        }
        memcpy(out, q, 3 * sizeof(float)); *crouch = sc[i].crouch;
        return true;
    }
    return false;
}
bool HoldAim(const float* at) {                      // the first person view (R1) on a point; true when on it
    gPad.buttons |= 0x0800;
    float m[16];
    if (!ps2world::ViewMatrix(m)) return false;
    // (v137) the stick still until the view's state is on (30 frames at most), the aim's rates afresh then (FightFrame's)
    static bool was = false;
    const bool viewOn = (gMe.bits[0] & 0x80) != 0;
    if (viewOn && !was) gAimSince = gFrame;
    was = viewOn;
    const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    const float tx = at[0] - m[12], tz = at[2] - m[14], ty = at[1] - m[13];
    const bool on = AimView(camYaw, camPitch, std::atan2(tx, tz), std::atan2(ty, std::sqrt(tx * tx + tz * tz)), 0.f, 0.3f, 1.2f * kDeg, 1.2f * kDeg);
    if (!viewOn && gFrame - gTc.at < 30) { gPad.sticks[2] = gPad.sticks[3] = 0x80; return false; }
    return on && viewOn;
}
void TacEnd(const char* how) {
    if (!gTc.guard || gDebug) Log("tactic: %s%s %s after %.1f s", kTcName[gTc.kind], gTc.guard ? " (guard)" : "", how, (gFrame - gTc.at) / 60.0);
    if (gTc.guard) gGuardHoldAt = Rnd01() < 0.6f ? gFrame : gFrame + 600;   // (another hold six times in ten, else a round first: 10 s at most)
    else if (gTc.crouched) gStandUpSince = 0;         // (up again before it walks)
    gTc = Tactic(); gTcEndAt = gFrame; gTcDryAt = gFi.dryShots;   // (its shots for nothing counted from here)
    gTcNextMove = gFrame + 240 + (long long)(Rnd01() * 240.f);   // (the next move from here, not at once)
    gFi.readySince = -1;                              // (readied afresh before a shot: a full press from nothing fires nothing)
    StopPath();
}
const char* FoeName(int id) { const Player* p = PlayerById(id); return p ? p->name.c_str() : "?"; }
void TacStart(int kind, int foe, const char* why) {
    gTc = Tactic();
    gTc.kind = kind; gTc.foe = foe; gTc.at = gFrame; gTc.why = why; gTc.reached = false; gTc.reachedAt = -1;
    StopPath();
}
// the corner held: its place reached, its watch point; in its box, or the gun on the point, crouched, until
void HoldStart(long long frames, bool box, const char* why) {
    // ((v141 beta 6, aim) shorter as the tier rises (kFight's holdK: 1 up to NORMAL, 0.4 at PRO), half a second at least; the one-life
    // modes' own waits (noticed, a passage's opening) as they were. MGO1_DOLL_HOLD_K=0: beta 5's lengths)
    static const bool holdOff = getenv("MGO1_DOLL_HOLD_K") && *getenv("MGO1_DOLL_HOLD_K") == '0';
    if (!holdOff && !OneLifeMode() && strcmp(why, "a passage's opening")) frames = std::max(30LL, (long long)std::lround(frames * Fk().holdK));
    const int foe = gTc.foe;
    const float w[3] = {gTc.watch[0], gTc.watch[1], gTc.watch[2]};
    const bool wh = gTc.watchHave, crouched = gTc.crouched;
    gTc = Tactic(); gTc.kind = TC_HOLD; gTc.foe = foe; gTc.at = gFrame; gTc.until = gFrame + frames; gTc.untilMax = gTc.until + 180; gTc.box = box && !NoBoxTier(); gTc.why = why;
    gTc.crouched = crouched;
    memcpy(gTc.watch, w, sizeof w); gTc.watchHave = wh;
    memcpy(gTc.spot, gMe.pos, sizeof gTc.spot);
    gAimSince = gFrame;
    Log("tactic: hold the corner toward %s (%s): %s, %.1f s, the corner %.1f m away", FoeName(foe), why, box ? "in its box" : "the gun on it", frames / 60.0,
        wh ? Dist2D(gMe.pos, w) / 1000.f : 0.f);
}
bool TacticRun(const Player* seen) {
    const long long t = gFrame - gTc.at;
    const char* foeName = gTc.foe >= 0 ? FoeName(gTc.foe) : "";
    switch (gTc.kind) {
    case TC_RELOAD: {
        const int mag = MagNow();
        Act("reloading", foeName);
        if (gTc.reached && gFrame - gHurtAt < 5 && gFrame - gTc.reachedAt > 10) { TacEnd("hurt at its cover"); return false; }
        if (!gTc.begun) {                                // (the reload first: the trigger pressed anew after the gun's own time,
            if (Reloading() || mag != 0) { gTc.begun = true; gTc.begunAt = gFrame; }   //  let go 4 frames, readied 12, pulled; 3 tries)
            else {
                StopPath(); Steer(0, 0, 0);
                if (gTc.pressAt < 0) { if (gFrame < gFi.nextFire) return true; gTc.pressAt = gFrame; }
                const long long u = (gFrame - gTc.pressAt) % 30;
                if (u < 4) { gPad.buttons &= ~0x8000u; gPad.pressures[11] = 0; }
                else { gPad.buttons |= 0x8000; gPad.pressures[11] = u < 16 ? 0x3F : 0xFF; }
                if (gFrame - gTc.pressAt > 90) { gTcReloadOff = gFrame + 600; TacEnd("no reload begun"); return false; }   // (not tried again for 10 s)
                return true;
            }
        }
        if (mag < 0 && !Reloading()) { TacEnd("another gun in its hands"); return false; }   // (its magazine not known any more)
        // reloading (the player free to run): to its cover, else across his line and back as far as it has room
        const long long r = gFrame - gTc.begunAt;
        if (gTc.have && !gTc.reached) {
            if (GoTo(gTc.spot, "cover", 450.f)) { gTc.reached = true; gTc.reachedAt = gFrame; }
        } else if (gTc.strafe && r < 2 * gTc.strafeFrames) {
            const float a = gTc.strafeDir + (r >= gTc.strafeFrames ? 3.14159265f : 0.f);
            Steer(std::sin(a), std::cos(a), 1.f);
        } else { StopPath(); Steer(0, 0, 0); }
        if (gTc.reached) { StopPath(); Steer(0, 0, 0); if (gTc.crouch && !Low()) { SetLow(true); gTc.crouched = true; } }
        if (!Reloading() && mag > 0 && r > 10 && (gTc.reached || !gTc.have)) {
            if (gTc.have && gTc.watchHave && !seen) { Log("tactic: reload done behind cover (%.1f s)", (gFrame - gTc.at) / 60.0); HoldStart(90 + (long long)(Rnd01() * 120.f), false, "reloaded"); return true; }
            TacEnd("reloaded");
            return false;
        }
        if (r > 300 || (gTc.have && !gTc.reached && r > 240)) { TacEnd("given up"); return false; }
        return true;
    }
    case TC_MOVE:
        Act("repositioning", foeName);
        if (GoTo(gTc.spot, "cover", 500.f) || t > 300) { TacEnd(t > 300 ? "given up" : "there"); return false; }
        return true;
    case TC_BACK: case TC_FEINT: {
        Act(gTc.kind == TC_FEINT ? "feinting" : gTc.box && InMyBox() ? "box_slipping" : "falling_back", foeName);
        // (its box on for the run: none of the enemies sees it now, 6 m and more to go)
        if (gTc.box && !InMyBox() && gTc.kind == TC_BACK && Dist2D(gMe.pos, gTc.spot) > 6000.f && gFrame - gHurtAt > 120 && !SeenByAny(Threats(120)) && HasItem(0xE)) {
            if (ItemTo(0xE)) { Steer(0, 0, 0); return true; }
        }
        if (!gTc.reached) {
            if (GoTo(gTc.spot, "hide", 500.f)) { gTc.reached = true; gTc.reachedAt = gFrame; StopPath(); }
            else if (t > 600) { TacEnd("given up"); return false; }
            else return true;
        }
        Steer(0, 0, 0);
        // there: the corner held (the feint always; falling back when he is coming, or now and then)
        const Player* f = PlayerById(gTc.foe);
        const auto kn = gKnown.find(gTc.foe);
        const bool coming = f && f->alive && kn != gKnown.end() && gFrame - kn->second.last < 240 && Dist2D(kn->second.pos, gMe.pos) < 25000.f;
        if (gTc.kind == TC_FEINT || (coming && gTc.watchHave) || (coming && gTc.box)) {
            // (its box planned, not on for the run: on now, unseen, he coming; the wait in it for him to pass)
            if (gTc.kind == TC_BACK && gTc.box && !InMyBox() && HasItem(0xE) && gFrame - gTc.reachedAt < 90 && gFrame - gHurtAt > 120 && !SeenByAny(Threats(120)) &&
                ItemTo(0xE)) {
                Steer(0, 0, 0); return true;
            }
            const bool box = gTc.box && InMyBox();
            if (!box && gTc.crouch && !Low() && gFrame - gTc.reachedAt < 40) { SetLow(true); gTc.crouched = true; return true; }   // (hidden only crouched: down first)
            const bool peek = !strcmp(gTc.why, "shot and hidden");   // ((v137) the master tier's: a short hold, then out again)
            HoldStart(box ? 360 + (long long)(Rnd() % 240) : peek ? 45 + (long long)(Rnd() % 46) : 150 + (long long)(Rnd01() * 330.f), box,
                      gTc.kind == TC_FEINT ? "the feint" : peek ? "shot and hidden" : "fallen back");
            return true;
        }
        TacEnd("there");
        return false;
    }
    case TC_HOLD: {
        const bool hurtIn = gFrame - gHurtAt < 20 && gHurtAt > gTc.at;   // ((v137) hurt since it began: a hold begun as it was hit goes on)
        if (gFrame >= gTc.until || hurtIn) { TacEnd(hurtIn ? "hurt" : "nobody came"); return false; }
        const Player* f = PlayerById(gTc.foe);
        if (f && Audible(*f) && Dist2D(f->pos, gMe.pos) < 6000.f && gTc.until - gFrame < 60) gTc.until = std::min(gTc.untilMax, gFrame + 60);   // (his steps near: a moment more, 3 s at most)
        StopPath(); Steer(0, 0, 0);
        if (gTc.box) {
            Act("box_waiting", foeName);
            if (!InMyBox() && HasItem(0xE)) ItemTo(0xE);
            return true;
        }
        Act(gTc.guard ? "guarding_objective" : strcmp(gTc.why, "out of sight") ? "holding_corner" : "watching", foeName);
        if (!gTc.watchHave) { TacEnd("no corner"); return false; }
        HoldAim(gTc.watch);
        const uint32_t st = gMe.bits[0] & 0xFFFF;
        // ((v137) not for the short holds, steps heard or shot at: up again would cost 45 frames when he shows elsewhere)
        const bool quick = !strcmp(gTc.why, "his steps heard") || !strcmp(gTc.why, "shot from there") || !strcmp(gTc.why, "shot and hidden");
        if (st == 0x83 && t > 15 && gFrame - gTc.crossAt > 45 && !quick) gTc.crossAt = gFrame;   // (crouched in the view: Cross)
        if (gFrame - gTc.crossAt < 3) gPad.buttons |= 0x4000;
        if (st == 0x85) gTc.crouched = true;
        return true;
    }
    }
    return false;
}
// an enemy in sight while the corner is held: the gun on him (the fight takes it on), or in the box, still
// until he passes by or finds the doll
bool HoldSprung(const Player& e) {
    if (gTc.box) {
        const float d = Dist2D(gMe.pos, e.pos);
        const bool facing = std::fabs(Wrap(AngleTo(e.pos, gMe.pos) - e.facing)) < 30.f * kDeg;
        const bool away = e.vel[0] * (e.pos[0] - gMe.pos[0]) + e.vel[1] * (e.pos[2] - gMe.pos[2]) > 0.f;
        const bool passed = d < 7000.f && !facing && away;
        const bool found = (facing && d < 9000.f) || d < 1800.f;
        if (gFrame - gHurtAt < 30 || found || passed || gFrame >= gTc.until) {
            const char* how = passed ? "passed by: out of" : found ? "found it: out of" : gFrame - gHurtAt < 30 ? "hurt: out of" : "time up: out of";
            Log("tactic: %s %s the box (%.1f m)", e.name.c_str(), how, d / 1000.f);
            TacEnd(passed ? "he passed by" : found ? "found" : gFrame - gHurtAt < 30 ? "hurt" : "time up");
            return false;
        }
        StopPath(); Steer(0, 0, 0); Act("box_waiting", e.name);
        return true;
    }
    if (gFrame - gTc.at < 10) { TacEnd("seen at once"); return false; }   // (its place not out of his sight: no ambush)
    const float off = gTc.watchHave ? std::fabs(Wrap(AngleTo(gMe.pos, e.pos) - AngleTo(gMe.pos, gTc.watch))) / kDeg : 180.f;
    if (off < 25.f) {                                // (round the corner held: the gun on him)
        memcpy(gPre.at, gTc.watch, sizeof gPre.at); gPre.until = gFrame + 120; gPre.fpv = (gMe.bits[0] & 0x80) != 0;
        Log("tactic: %s came round the corner held, %.1f m away (%.0f degrees off)", e.name.c_str(), Dist2D(gMe.pos, e.pos) / 1000.f, off);
        TacEnd("he came");
    } else {
        Log("tactic: %s came into sight elsewhere, %.1f m away (%.0f degrees off the corner held)", e.name.c_str(), Dist2D(gMe.pos, e.pos) / 1000.f, off);
        TacEnd("elsewhere");
    }
    return false;
}
// in a fight, before the shots: a move to make (true: the frame's moves made)
// (v141 beta 7) tier 10's cheat: no move of its own (a fall back, a feint, a hold, a watch, a standoff, another angle, a cover to reload:
// its reload in place); the one-life rules' own holds go on (a guard's, a noticed attacker's), any other move or hold going on ends
bool CheatKeepsTactic() { return gTc.kind == TC_HOLD && (gTc.guard || OneLifeMode()); }
bool TacticFight(const Player& e) {
    if (OldFight()) return false;
    { const auto it = gFoeLife.find(e.id); if (it != gFoeLife.end() && e.life < it->second) gDealtAt[e.id] = gFrame; gFoeLife[e.id] = e.life; }
    // (the one fought kept a quarter of a second out of sight: not in sight for a corner held nor a move)
    const auto kv = gKnown.find(e.id);
    const bool vis = kv != gKnown.end() && kv->second.visible;
    if (CheatOn()) {
        if (gTc.kind == TC_NONE) return false;
        if (CheatKeepsTactic()) return vis ? HoldSprung(e) : TacticRun(nullptr);
        TacEnd("tier 10");
        return false;
    }
    if (gTc.kind == TC_HOLD) return vis ? HoldSprung(e) : TacticRun(nullptr);
    // ((v141 beta 6, aim) ELITE and up: hit by him on its way to another place (a move, a feint not reached yet), him in sight, a third
    // of its life left at least: the move ended, its answer at once (the fight: the defense's reaction, his fire returned). A fall back
    // (losing) and a reload go on)
    if (vis && (gTc.kind == TC_MOVE || (gTc.kind == TC_FEINT && !gTc.reached)) && LevelNow() >= 6.f && gFrame - gHurtAt < 10 && HitBy(e) && gFrame - gTc.at > 20 &&
        (gMyMaxLife <= 0 || gMe.life * 3 >= gMyMaxLife)) {
        TacEnd("hit: it answers");
        return false;
    }
    if (gTc.kind != TC_NONE) return TacticRun(vis ? &e : nullptr);
    if (gTcFight != e.id || gFrame - gTcFightAt > 600) { gTcFight = e.id; gTcMoves = 0; gTcDryAt = 0; gTcNextMove = gFrame + 240 + (long long)(Rnd01() * 240.f); gTcPeeks = 0; }
    gTcFightAt = gFrame;
    if (!vis || Carrying() || Scoped() || gCq.phase || gRo.phase || gGr.phase || gFrame - gTcEndAt < 45 || gFrame - gTcSearchAt < 30) return false;
    const float d = Dist2D(gMe.pos, e.pos);
    const bool snake = IAmSnake();
    const std::vector<Threat> th = Threats(180);
    Threat main; bool mainHave = false;
    for (const Threat& t : th) if (t.id == e.id) { main = t; mainHave = true; }
    if (!mainHave) return false;
    const float lifeK = gMyMaxLife > 0 ? (float)gMe.life / gMyMaxLife : 1.f;
    const auto dl = gDealtAt.find(e.id);
    const long long sinceDealt = dl == gDealtAt.end() ? 100000 : gFrame - dl->second;
    int aiming = 0;
    for (const Threat& t : th) {
        const Player* p = PlayerById(t.id);
        if (p && t.visible && std::fabs(Wrap(AngleTo(p->pos, gMe.pos) - p->facing)) < 15.f * kDeg && (p->bits[1] & 0x4400)) ++aiming;
    }
    auto start = [&](int kind, int purpose, const char* why) {
        gTcSearchAt = gFrame;
        float spot[3], watch[3]; bool crouch = false, wh = false;
        const auto t0 = std::chrono::steady_clock::now();
        const bool found = PickSpot(purpose, th, main, spot, &crouch, watch, &wh);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!found) {
            if (gDebug) Log("tactic: no place for a %s against %s (%.1f ms)", kTcName[kind], e.name.c_str(), ms);
            if (kind != TC_RELOAD) { gTcSearchAt = gFrame + 60; return false; }   // (not again for a second and a half)
            // no cover within reach: across his line and back while it reloads (the side with room, as far as its room)
            float g[3];
            const float across = AngleTo(gMe.pos, e.pos) + 1.5707963f;
            const float l = Ground(g) ? gMap->nav->ClearAlong(g, std::sin(across), std::cos(across), 4000.f, true) : 0.f;
            const float r = Ground(g) ? gMap->nav->ClearAlong(g, -std::sin(across), -std::cos(across), 4000.f, true) : 0.f;
            TacStart(TC_RELOAD, e.id, why);
            gTc.strafe = std::max(l, r) > 1500.f; gTc.strafeDir = l >= r ? across : across + 3.14159265f;
            gTc.strafeFrames = std::max(15LL, std::min(50LL, (long long)((std::max(l, r) - 500.f) / 60.f)));
            Log("tactic: reload: no cover within 7 m of %s (%.1f m away): %s", e.name.c_str(), d / 1000.f, gTc.strafe ? "across his line" : "where it stands");
            return true;
        }
        TacStart(kind, e.id, why);
        memcpy(gTc.spot, spot, sizeof spot); gTc.have = true; gTc.crouch = crouch;
        memcpy(gTc.watch, watch, sizeof watch); gTc.watchHave = wh;
        gTc.box = kind == TC_BACK && HasItem(0xE) && (gStyle < 0.45f || Rnd01() < 0.3f) && !NoBoxTier();
        Log("tactic: %s (%s) against %s %.1f m away: to %.0f %.0f %.0f, %.1f m, %s%s%s (%.1f ms)", kTcName[kind], why, e.name.c_str(), d / 1000.f, spot[0], spot[1], spot[2],
            Dist2D(gMe.pos, spot) / 1000.f, kind == TC_MOVE ? (crouch ? "a low cover" : "in the open") : crouch ? "hidden crouched" : "hidden",
            wh ? ", a corner to hold" : "", gTc.box ? ", its box for the run" : "", ms);
        return true;
    };
    // its magazine empty: to cover while it reloads (not within a punch's reach)
    if (d > 3500.f && gFrame >= gTcReloadOff && MagNow() == 0 && Rounds(Hand()) > 0) return start(TC_RELOAD, SF_RELOAD, "the magazine empty") && TacticRun(&e);
    if (snake || gLeaderTarget >= 0) return false;   // (Snake: his own ways, hidden, the objective; its leader shooting: cover by him)
    // losing: back out of their sight (its guns empty: the weapon box, v134's; a smoke of its own about to go: SmokeWanted's).
    // One life: sooner, under half its life and hurt these 2.5 s; a defender while an enemy carries the objective: never
    // (nor a feint, nor another place: at all costs)
    const bool one = OneLifeMode(), allCosts = OneLifeDefender() && EnemyCarrier();
    const float lowK = MasterVs(e.id) ? 0.45f : 0.35f;   // ((v137) the master tier: back to heal sooner)
    const bool losing = (lifeK < lowK && HitsWithin(150) > 0 && e.life > gMe.life) || (aiming >= 2 && lifeK < 0.6f) || (one && lifeK < 0.5f && HitsWithin(150) > 0);
    const bool smokeNow = SmokeWould(e);
    if (smokeNow) return false;                          // (its smoke about to go: the smoke first, v134's)
    // (crouched, or its gun on a corner that paid: it fights on from there, getting up in his sight costs more; two on it: back)
    const bool placed = (Low() || gFrame < gPre.until) && aiming < 2;
    // (beyond 9 m only: closer, its back turned, it died within 1.4 s in v136's audit, every time; it fights on)
    if (losing && !allCosts && !placed && !smokeNow && !NoAmmo() && gFrame - gTcBackAt > 600 && d > 9000.f) {
        gTcBackAt = gFrame;
        if (start(TC_BACK, SF_BACK, aiming >= 2 ? "two enemies on it" : "hurt, losing")) return TacticRun(&e);
    }
    if (allCosts) return false;
    // ((v137) the master tier: shot and hidden. Its burst just over, him aiming at it or its hit these 1.5 s, beyond 9 m, not
    // placed: a place hidden from him within 7 m with a corner to hold (the reload's search), the corner held 0.75 to 1.5 s
    // there (TacticRun); 4 to 8 s apart, 3 a fight; not in a one-life mode)
    if (MasterVs(e.id) && !one && !placed && gFi.shots > 0 && gFrame >= gFi.fireUntil && gFrame - gFi.fireUntil < 20 && gFrame >= gTcPeekAt && gTcPeeks < 3 &&
        (aiming >= 1 || HitsWithin(90) > 0) && d > 9000.f && !NoAmmo()) {
        gTcPeekAt = gFrame + 240 + (long long)(Rnd() % 240); gTcSearchAt = gFrame;
        float spot[3], watch[3]; bool crouch = false, wh = false;
        if (PickSpot(SF_RELOAD, th, main, spot, &crouch, watch, &wh) && wh) {
            TacStart(TC_BACK, e.id, "shot and hidden");
            memcpy(gTc.spot, spot, sizeof spot); gTc.have = true; gTc.crouch = crouch;
            memcpy(gTc.watch, watch, sizeof watch); gTc.watchHave = true;
            ++gTcPeeks;
            Log("tactic: shot and hidden from %s (%.1f m away): to %.0f %.0f %.0f, %.1f m, %s", e.name.c_str(), d / 1000.f, spot[0], spot[1], spot[2],
                Dist2D(gMe.pos, spot) / 1000.f, crouch ? "hidden crouched" : "hidden");
            return TacticRun(&e);
        }
    }
    // the feint: not winning at mid range, one enemy, now and then (none in a one-life mode: it shows the doll)
    // ((v141 beta 3) NIGHTMARE: neither the feint nor the standoff's move while he aims at it: it shoots back (the ftrace bench:
    // a move in the open at 12 m under his fire, its life 80 -> 20)
    const bool nmHold = NmV3() && aiming > 0;
    if (!one && !nmHold && d > 6000.f && d < 18000.f && aiming <= 1 && gFrame - gTcFeintAt > 1200 && gFrame - gFi.startAt > 120 && (sinceDealt > 150 || HitsWithin(150) > 0) &&
        gFrame % 90 == 0) {
        gTcFeintAt = gFrame;
        if (Rnd01() < 0.25f + 0.2f * (1.f - gStyle) && start(TC_FEINT, SF_FEINT, "not winning")) return TacticRun(&e);
    }
    // the standoff: another place now and then, sooner when hurt twice without hurting him, or shots for nothing
    // (three a fight at most, none while a corner held pays: the gun on him; its hits and shots counted from where it stands)
    const long long here = std::max(gFrame - 180, gTcEndAt);
    int hitsHere = 0;
    for (long long f : gHits) if (f > here) ++hitsHere;
    const bool trading = hitsHere >= 2 && sinceDealt > 180;
    if (gFi.dryShots < gTcDryAt) gTcDryAt = 0;          // (the target hurt since: counted afresh)
    const int dryHere = gFi.dryShots - gTcDryAt;
    // ((v137) the time's own move only when neither hurts the other these 3 s: winning, it ran off its fire 4 to 8 s in)
    if (d > 7000.f && !nmHold && gTcMoves < 3 && gFrame >= gPre.until && ((gFrame >= gTcNextMove && sinceDealt > 180) || trading || dryHere >= 5) && !(e.life < 60 && sinceDealt < 90)) {
        gTcNextMove = gFrame + 240 + (long long)(Rnd01() * 240.f);
        if (start(TC_MOVE, SF_MOVE, trading ? "hurt without hurting him" : dryHere >= 5 ? "shots for nothing" : "a standoff")) { ++gTcMoves; gTcDryAt = gFi.dryShots; return TacticRun(&e); }
    }
    return false;
}
// out of a fight: a move going on, or the corner the enemy just fought left by watched a moment
bool TacticLull(int lost) {
    if (OldFight()) return false;
    if (CheatOn()) {                                     // ((v141 beta 7) tier 10's cheat: none of its own, the one-life rules' holds)
        if (gTc.kind == TC_NONE) return false;
        if (CheatKeepsTactic()) return TacticRun(nullptr);
        TacEnd("tier 10");
        return false;
    }
    if (gTc.kind != TC_NONE) return TacticRun(nullptr);
    if (lost < 0 || Carrying() || IAmSnake() || Scoped() || NoAmmo()) return false;
    const Player* f = PlayerById(lost);
    if (f && OneLifeDefender() && CarrierOf(*f)) return false;   // (the carrier gone out of sight: after him at once)
    const auto kn = gKnown.find(lost);
    // ((v137) the master tier, after its kill (seen, or him dead within a second of its last hit on him): another angle 7 times
    // in 10 (his kill camera showed it there): against another enemy known (SF_MOVE), else out of the line of where he fell
    // (SF_BACK); not in a one-life mode (its own rules))
    const auto dealt = gDealtAt.find(lost);
    const bool myKill = (gKillSeenVictim == lost && gFrame - gKillSeenAt < 180) || (dealt != gDealtAt.end() && gFrame - dealt->second < 60);
    if (f && !f->alive && myKill && MasterVs(lost) && !OneLifeMode() && Rnd01() < 0.7f) {
        std::vector<Threat> th = Threats(300);
        Threat main; bool other = false;
        for (const Threat& t : th) if (t.id != lost) { main = t; other = true; break; }
        if (!other) {
            main.id = lost; main.visible = false;
            memcpy(main.pos, kn != gKnown.end() ? kn->second.seenPos : f->pos, sizeof main.pos);
            BodyPoint(main.pos, 0.95f, main.eye); BodyPoint(main.pos, 0.45f, main.chest);
            th.push_back(main);
        }
        float spot[3], watch[3]; bool crouch = false, wh = false;
        if (PickSpot(other ? SF_MOVE : SF_BACK, th, main, spot, &crouch, watch, &wh)) {
            TacStart(other ? TC_MOVE : TC_BACK, main.id, "another angle after its kill");
            memcpy(gTc.spot, spot, sizeof spot); gTc.have = true; gTc.crouch = crouch;
            memcpy(gTc.watch, watch, sizeof watch); gTc.watchHave = wh;
            Log("tactic: another angle after its kill of %s: to %.0f %.0f %.0f, %.1f m (%s)", f->name.c_str(), spot[0], spot[1], spot[2], Dist2D(gMe.pos, spot) / 1000.f,
                other ? "against another enemy" : "out of the line of where he fell");
            return TacticRun(nullptr);
        }
    }
    // (gone out of its sight a moment ago: not a fight left for the objective or an order, him in sight still)
    if (!f || !f->alive || (Asleep(*f) && f->stun <= 12000) || kn == gKnown.end() || kn->second.visible || gFrame - kn->second.seen > 45 || Dist2D(kn->second.pos, gMe.pos) < 3000.f ||
        Rnd01() > 0.65f) return false;                   // (asleep: no corner to watch, he lies there)
    gTc = Tactic(); gTc.foe = lost;
    memcpy(gTc.watch, kn->second.pos, sizeof gTc.watch); gTc.watch[1] += 650.f; gTc.watchHave = true;
    // ((v137) the master tier: the corner he will come round, along the mesh's way from it to his last place (WatchCorner))
    if (MasterVs(lost)) { float g[3], w[3]; if (Ground(g) && WatchCorner(g, kn->second.pos, w)) memcpy(gTc.watch, w, sizeof w); }
    // (v137) levels 4 and 5, its box had, unhurt these 2 s and over 60 % of its life, him 6 to 25 m away, no other enemy in
    // sight, not a one-life attacker: an ambush in its box now and then (15 % at 4, 25 % at 5, more for a careful life's
    // taste), 6 to 10 s; out when he passes by, finds it or hurts it (HoldSprung)
    bool others = false;
    for (const auto& o : gKnown) if (o.first != lost && o.second.visible) others = true;
    const float dk = Dist2D(kn->second.pos, gMe.pos);
    // ((v141 beta 6, aim) a box pop's moment, break_los: the tier's chance (kFight's boxPop, half of it: none at TRAINING and EASY,
    // NORMAL and up) and its gap from the last box pop; MGO1_DOLL_BOXPOP=0: beta 5's (levels 4 and 5, 15 and 25 %))
    const FightSkill& fk = Fk();
    const bool ambushOk = NoBoxTier() ? false : BoxPopOn() ? Lv() >= 3 && fk.boxPop > 0.f && gFrame - gBoxPopAt >= fk.boxGap && Rnd01() < fk.boxPop * 0.5f + 0.1f * (1.f - gStyle)
                                     : Lv() >= 4 && Rnd01() < (Lv() >= 5 ? 0.25f : 0.15f) + 0.1f * (1.f - gStyle);
    if (HasItem(0xE) && !others && !OneLifeAttacker() && gFrame - gHurtAt > 120 && (gMyMaxLife <= 0 || gMe.life * 10 > gMyMaxLife * 6) &&
        dk > 6000.f && dk < 25000.f && ambushOk) {
        if (BoxPopOn()) { gBoxPopAt = gFrame; gBoxWhy = "break_los"; gBoxWhyAt = gFrame; BoxSay("on", "break_los", f->name, dk); }
        HoldStart(360 + (long long)(Rnd() % 240), true, "an ambush in its box");
        return TacticRun(nullptr);
    }
    HoldStart(90 + (long long)(Rnd01() * (1.f - 0.5f * gStyle) * 120.f), false, "out of sight");
    return TacticRun(nullptr);
}
// ---- (v137) hurt by an enemy it does not see (a lean behind a wall, a shot from far): his shot's start heard (HearShots;
// its muzzle seen: there, else a guess within 1.5 m) these 2 s. First the gun on that spot (a corner held: the first person view on his head there, 0.7 s at level 1
// to 1.5 s at 5): when he shows again the reaction is short (HoldSprung). Hurt again meanwhile (he wins that trade), or
// shot at again within 6 s: out of his line (the fall back's search against his spot: a place hidden from it, a corner
// held from there). He keeps one spot 3 s and more, the doll out of his line: a grenade there (a lethal one, 7 to 24 m,
// its throw reaching it, no ally there; level 2 and up). No still trade: the hold ends at the first hit after it began ----
struct ShotAtRun { int id = -1, holds = 0; long long holdAt = -100000, spotSince = -1; float spot[3] = {0, 0, 0};
                   float near[3] = {0, 0, 0}; };   // ((v137) near: where it takes him to be, his muzzle seen: there; heard only: within 1.5 m)
ShotAtRun gSa;
bool ShotAtFrame() {
    if (OldFight() || CheatOn() || gTc.kind != TC_NONE || gFi.target >= 0 || Carrying() || IAmSnake() || Scoped() || NoAmmo() || gFrame - gHurtAt > 120) return false;
    int id = -1; long long best = -1;
    for (const auto& kv : gKnown)
        if (!kv.second.visible && gFrame - kv.second.shotAt < 120 && kv.second.shotAt > best && Dist2D(kv.second.shotFrom, gMe.pos) < 40000.f) { best = kv.second.shotAt; id = kv.first; }
    if (id < 0) return false;
    const Player* e = PlayerById(id);
    if (!e || !Enemy(*e) || !e->alive || Asleep(*e)) return false;
    const Known& k = gKnown[id];
    if (gSa.id != id || Dist2D(gSa.spot, k.shotFrom) > 2000.f) {
        gSa.spotSince = gFrame; memcpy(gSa.spot, k.shotFrom, sizeof gSa.spot);
        const float a = Rnd01() * 6.2831853f, r = Rnd01() * 1500.f;   // (heard only: a guess about the spot, as a player's ears)
        gSa.near[0] = k.shotFrom[0] + std::sin(a) * r; gSa.near[1] = k.shotFrom[1]; gSa.near[2] = k.shotFrom[2] + std::cos(a) * r;
    }
    if (k.peekAt >= k.shotAt) memcpy(gSa.near, k.shotFrom, sizeof gSa.near);   // (his last shot's muzzle seen: there)
    if (gSa.id != id || gFrame - gSa.holdAt > 360) gSa.holds = 0;
    gSa.id = id;
    const float eyeAt[3] = {k.shotFrom[0], k.shotFrom[1] + 150.f, k.shotFrom[2]};
    const float watchAt[3] = {gSa.near[0], gSa.near[1] + 150.f, gSa.near[2]};
    float me[3]; BodyPoint(gMe.pos, 0.6f, me);
    const bool exposed = LineClear(eyeAt, me);
    const float d = Dist2D(gMe.pos, k.shotFrom);
    const int code = GrenadeCode();
    if (!exposed && (code == 0x13 || code == 0x14) && gFrame - gSa.spotSince >= 180 && d > 7000.f && d < 24000.f && gFrame - gGr.lastThrow >= 1200 && gGr.phase == 0 &&
        Lv() >= 2 && !HeadshotsOnly() && WindowFree(0) && !OnLadder()) {
        const float feet[3] = {gSa.near[0], GroundOf(k.shotFrom) + 60.f, gSa.near[2]};
        bool allyNear = false;
        for (const Player& a : gPlayers) if (Ally(a) && a.alive && Dist2D(a.pos, feet) < 7000.f) allyNear = true;
        if (!allyNear && PlanThrowAt(code, feet, id, true, ("a grenade at " + e->name + "'s spot").c_str())) { gGr.unseen = true; return true; }   // (GrenadeFrame from the next frame)
    }
    if (gSa.holds == 0) {
        gSa.holds = 1; gSa.holdAt = gFrame;
        Log("tactic: shot at by %s, not seen (%.1f m, %s)", e->name.c_str(), d / 1000.f, exposed ? "in his line" : "out of his line");
        gTc = Tactic(); gTc.foe = id; memcpy(gTc.watch, watchAt, sizeof watchAt); gTc.watchHave = true;
        HoldStart(30 + 12LL * std::min(5, Lv()), false, "shot from there");
        return TacticRun(nullptr);
    }
    if (!exposed || gSa.holds >= 2) return false;        // (out of his line already, or moved once: its own ways)
    gSa.holds = 2;
    std::vector<Threat> th = Threats(180);
    Threat main; main.id = id; main.visible = false;
    memcpy(main.pos, k.shotFrom, sizeof main.pos); memcpy(main.eye, eyeAt, sizeof main.eye);
    main.chest[0] = k.shotFrom[0]; main.chest[1] = k.shotFrom[1] - 250.f; main.chest[2] = k.shotFrom[2];
    bool had = false;
    for (Threat& t : th) if (t.id == id) { t = main; had = true; }
    if (!had) th.push_back(main);
    float spot[3], watch[3]; bool crouch = false, wh = false;
    if (!PickSpot(SF_BACK, th, main, spot, &crouch, watch, &wh)) { if (gDebug) Log("tactic: shot at by %s again: no place out of his line", e->name.c_str()); return false; }
    TacStart(TC_BACK, id, "shot at, not seen");
    memcpy(gTc.spot, spot, sizeof spot); gTc.have = true; gTc.crouch = crouch;
    memcpy(gTc.watch, watch, sizeof watch); gTc.watchHave = wh;
    Log("tactic: fall back (shot at by %s again, not seen) to %.0f %.0f %.0f, %.1f m, %s%s", e->name.c_str(), spot[0], spot[1], spot[2], Dist2D(gMe.pos, spot) / 1000.f,
        crouch ? "hidden crouched" : "hidden", wh ? ", a corner to hold" : "");
    return TacticRun(nullptr);
}
// ---- (v137) levels 4 and 5: an enemy's steps heard within 6 m (where they are, 1.5 m about: Perceive), him not seen these
// 2 s (a wall, a corner between): the gun on where he is heard 1 to 2 s (a corner held, the first person view), as a player
// turns to steps; he shows, the reaction is short (HoldSprung). Once in 4 s; not in a one-life mode (its own waits) ----
long long gHeardHoldAt = -100000;
bool HeardFrame() {
    if (OldFight() || CheatOn() || Lv() < 4 || OneLifeMode() || gTc.kind != TC_NONE || gFi.target >= 0 || Carrying() || IAmSnake() || Scoped() || NoAmmo() ||
        gFrame - gHeardHoldAt < 240 || gSet.order == "hide") return false;
    for (const auto& kv : gKnown) {
        const Known& k = kv.second;
        if (k.visible || gFrame - k.heard > 20 || gFrame - k.seen < 120 || Dist2D(k.pos, gMe.pos) > 6000.f) continue;
        const Player* e = PlayerById(kv.first);
        if (!e || !Enemy(*e) || !e->alive || Asleep(*e)) continue;
        gHeardHoldAt = gFrame;
        gTc = Tactic(); gTc.foe = kv.first;
        memcpy(gTc.watch, k.pos, sizeof gTc.watch); gTc.watch[1] += 650.f; gTc.watchHave = true;
        if (MasterVs(kv.first)) { float g[3], w[3]; if (Ground(g) && WatchCorner(g, k.pos, w)) memcpy(gTc.watch, w, sizeof w); }   // ((v137) 6: his corner)
        HoldStart(60 + (long long)(Rnd() % 60), false, "his steps heard");
        return TacticRun(nullptr);
    }
    return false;
}
// ---- (v137) an enemy gone into a crawl passage (a vent, under a truck: the mesh's crawl area), where a player lying in it
// sees out through its opening: his last place seen in it (or lying within a metre of it), or his shot's muzzle seen in it,
// these 8 s, not in sight now, his way in the passage from its opening 6 m at most. Its opening: the first point of the
// doll's way to him in the crawl area. Once in 20 s a foe, by its level, its life and its allies: a lethal grenade (the
// grenade, WP) when the opening is 7 to 24 m away and a throw ends in the passage within 2 m of him, no ally there (levels
// 2 and up; first at 4 and 5); after him (its way crawls in: the path follower lies down there, the fight goes on lying,
// LeanFrame keeps it down) with over 60 % of its life, or 40 % with an ally within 10 m; else the opening held (the gun on
// it, a lying head's height), 4 to 8 s ----
struct VentRun { int id = -1, mode = 0; long long at = -1; float to[3] = {0, 0, 0}; };   // (mode 1: after him)
VentRun gVt;
std::map<int, long long> gVentAt;
bool VentFrame() {
    if (OldFight() || CheatOn() || !MapReady() || gTc.kind != TC_NONE || gFi.target >= 0 || Carrying() || IAmSnake() || Scoped() || NoAmmo() || gGr.phase) { gVt.mode = 0; return false; }
    if (gVt.mode == 1) {                                     // after him: his last place, 15 s at most
        const Player* e = PlayerById(gVt.id);
        const bool hurtOther = gFrame - gHurtAt < 30;
        if (!e || !Enemy(*e) || !e->alive || gFrame - gVt.at > 900 || hurtOther || Dist2D(gMe.pos, gVt.to) < 1200.f) {
            Log("tactic: after %s in the passage: over (%s)", e ? e->name.c_str() : "?", !e || !e->alive ? "he is gone" : gFrame - gVt.at > 900 ? "15 s" : hurtOther ? "hurt" : "his place reached");
            gVt.mode = 0; StopPath();
            return false;
        }
        Act("chasing", e->name);
        GoTo(gVt.to, "follow", 1000.f);
        return true;
    }
    float g0[3];
    if (InCrawl(gMe.pos) || !Ground(g0)) return false;
    for (const auto& kv : gKnown) {
        const Known& k = kv.second;
        if (k.visible) continue;
        const bool shotIn = gFrame - k.shotAt < 480 && k.shotAt >= k.seen && k.peekAt >= k.shotAt && InCrawl(k.shotFrom);   // ((v137) its muzzle seen there)
        const float* at = shotIn ? k.shotFrom : k.seenPos;
        if (!shotIn && gFrame - k.seen > 480) continue;
        const auto vt = gVentAt.find(kv.first);
        if (vt != gVentAt.end() && gFrame - vt->second < 1200) continue;
        const Player* e = PlayerById(kv.first);
        if (!e || !Enemy(*e) || !e->alive || Asleep(*e)) continue;
        bool in = InCrawl(at);
        if (!in && Lying(at))
            for (int i = 0; i < 4 && !in; ++i) { const float q[3] = {at[0] + (i == 0 ? 1000.f : i == 1 ? -1000.f : 0.f), at[1], at[2] + (i == 2 ? 1000.f : i == 3 ? -1000.f : 0.f)}; in = InCrawl(q); }
        const float d = Dist2D(gMe.pos, at);
        if (!in || d < 3000.f || d > 30000.f) continue;
        gVentAt[kv.first] = gFrame - 900;                    // (no way, no opening near him: looked at again in 5 s)
        // its opening: the first point of its way to him in the crawl area; his way in from there 6 m at most
        float tg[3];
        std::vector<Waypoint> w; float len = 0.f;
        const float af[3] = {at[0], at[1] - 300.f, at[2]};
        if (!gMap->nav->Nearest(af, 1000.f, tg) || !gMap->nav->Path(g0, tg, w, &len) || w.size() < 2) continue;
        int oi = -1; float walked = 0.f, toOpen = 0.f;
        for (size_t i = 0; i < w.size(); ++i) {
            if (i) walked += Dist2D(w[i - 1].p, w[i].p);
            if (w[i].area == AREA_CRAWL && oi < 0) { oi = (int)i; toOpen = walked; }
        }
        if (oi < 0 || len - toOpen > 6000.f) continue;
        const float* open = w[oi].p;
        gVentAt[kv.first] = gFrame;
        const float lifeK = gMyMaxLife > 0 ? (float)gMe.life / gMyMaxLife : 1.f;
        bool allyNear = false;
        for (const Player& a : gPlayers) if (Ally(a) && a.alive && Dist2D(a.pos, gMe.pos) < 10000.f) allyNear = true;
        // a grenade into it
        const int code = GrenadeCode();
        const float dOpen = Dist2D(gMe.pos, open);
        const bool grenadeTurn = Lv() >= 4 || (Lv() == 3 && Rnd01() < 0.6f) || (Lv() == 2 && Rnd01() < 0.3f);
        if (grenadeTurn && (code == 0x13 || code == 0x14) && dOpen > 7000.f && dOpen < 24000.f && gFrame - gGr.lastThrow >= 1200 && !HeadshotsOnly() && WindowFree(0) &&
            gFrame - gHurtAt > 120) {
            float eye[3]; MyThrowEye(eye);
            const float feet[3] = {tg[0], tg[1] + 60.f, tg[2]};
            const ThrowPlan plan = SolveThrow(eye, feet);
            bool allyThere = false;
            for (const Player& a : gPlayers) if (Ally(a) && a.alive && Dist2D(a.pos, plan.end) < 7000.f) allyThere = true;
            if (plan.err < 2000.f && InCrawl(plan.end) && !allyThere && PlanThrowAt(code, feet, kv.first, true, ("a grenade into the passage at " + e->name).c_str())) {
                gGr.unseen = true;
                Log("tactic: %s gone into a passage %.1f m in: a grenade (its opening %.1f m away)", e->name.c_str(), (len - toOpen) / 1000.f, dOpen / 1000.f);
                return true;
            }
        }
        const bool go = (lifeK > 0.6f || (allyNear && lifeK > 0.4f)) && (Lv() >= 3 || Rnd01() < 0.33f);
        if (go) {
            gVt = VentRun(); gVt.id = kv.first; gVt.mode = 1; gVt.at = gFrame; memcpy(gVt.to, tg, sizeof gVt.to);
            Log("tactic: %s gone into a passage %.1f m in: after him (its opening %.1f m away)", e->name.c_str(), (len - toOpen) / 1000.f, dOpen / 1000.f);
            StopPath();
            Act("chasing", e->name);
            GoTo(gVt.to, "follow", 1000.f);
            return true;
        }
        gTc = Tactic(); gTc.foe = kv.first;
        gTc.watch[0] = open[0]; gTc.watch[1] = open[1] + 350.f; gTc.watch[2] = open[2]; gTc.watchHave = true;
        Log("tactic: %s gone into a passage %.1f m in: its opening held (%.1f m away)", e->name.c_str(), (len - toOpen) / 1000.f, dOpen / 1000.f);
        HoldStart(240 + (long long)(Rnd() % 240), false, "a passage's opening");
        return TacticRun(nullptr);
    }
    return false;
}
// ---- (one-life defenders, the item in place) the guard: at its place about the item, crouched, the gun on the corner of
// the attackers' likely way in (a corner held, TC_HOLD) 8 to 18 s; then another hold six times in ten, else a slow round
// to another point of its sector (walked: QuietWhy) and a hold there. The way in: from the enemies known these 20 s
// (the latest), else from their side's spawns (the nearer its sector's bearing, the likelier), else its sector's
// bearing 30 m out. A hold ends when the item is taken or moved, when it is hurt, when an enemy comes into sight
// (HoldSprung), or when steps are heard within 12 m away from the way held (a hold toward them then) ----
bool GuardOrigin(const float* item, float* out) {
    long long best = -1;
    for (const auto& k : gKnown) {
        const long long at = std::max(k.second.last, k.second.heard);
        if (gFrame - at < 1200 && at > best && Dist2D(k.second.pos, item) > 4000.f) { best = at; memcpy(out, k.second.pos, 3 * sizeof(float)); }
    }
    if (best >= 0) return true;
    std::vector<std::pair<float, const Spawn*>> sp;
    if (gMap->geom && (gMyTeam == 0 || gMyTeam == 1)) {
        const SpawnKind a = gMyTeam == 0 ? SP_TDM_BLUE : SP_TDM_RED, b = gMyTeam == 0 ? SP_TDM_FIRST_BLUE : SP_TDM_FIRST_RED;
        for (const Spawn& s : gMap->geom->spawns)
            if ((s.kind == a || s.kind == b) && Dist2D(s.p, item) > 8000.f) sp.push_back({std::fabs(Wrap(AngleTo(item, s.p) - HabitAngle())), &s});
    }
    if (!sp.empty()) {
        std::sort(sp.begin(), sp.end(), [](const std::pair<float, const Spawn*>& x, const std::pair<float, const Spawn*>& y) { return x.first < y.first; });
        const Spawn* s = sp[Rnd() % std::min<size_t>(3, sp.size())].second;
        out[0] = s->p[0]; out[1] = s->p[1] + 800.f; out[2] = s->p[2];   // (a body there: WatchCorner takes hips)
        return true;
    }
    const float h = HabitAngle(), q[3] = {item[0] + std::sin(h) * 30000.f, item[1], item[2] + std::cos(h) * 30000.f};
    float on[3];
    if (!gMap->nav->Nearest(q, 6000.f, on)) return false;
    out[0] = on[0]; out[1] = on[1] + 800.f; out[2] = on[2];
    return true;
}
bool GuardHoldStart(const float* item) {
    float g[3], from[3], watch[3];
    if (!MapReady() || !Ground(g) || Dist2D(g, item) < 2000.f || !GuardOrigin(item, from)) return false;   // (not on the item)
    bool have = WatchCorner(g, from, watch);
    if (!have) {                                         // (its way in seen 22 m, or not at all: a point of it 10 m on, in sight)
        const float fg[3] = {from[0], from[1] - 800.f, from[2]}, eye[3] = {g[0], g[1] + 1050.f, g[2]};
        float f[3]; std::vector<Waypoint> w; float len = 0.f;
        if (gMap->nav->Nearest(fg, 1500.f, f) && gMap->nav->Path(g, f, w, &len) && w.size() >= 2 && len > 3000.f) {
            float walked = 0.f;
            for (size_t i = 1; i < w.size() && !have; ++i) {
                const float seg = Dist2D(w[i - 1].p, w[i].p);
                if ((walked + seg >= 10000.f || i + 1 == w.size()) && seg > 1.f) {
                    const float t = std::min(1.f, (10000.f - walked) / seg);
                    for (int j = 0; j < 3; ++j) watch[j] = w[i - 1].p[j] + (w[i].p[j] - w[i - 1].p[j]) * t;
                    watch[1] += 1450.f; have = true;
                }
                walked += seg;
            }
            if (have && !LineClear(eye, watch)) have = false;
        }
    }
    if (!have) return false;
    gTc = Tactic(); gTc.kind = TC_HOLD; gTc.guard = true; gTc.foe = -1; gTc.at = gFrame; gTc.why = "guarding";
    gTc.until = gTc.untilMax = gFrame + 480 + (long long)(Rnd() % 600);
    memcpy(gTc.watch, watch, sizeof watch); gTc.watchHave = true; memcpy(gTc.spot, gMe.pos, sizeof gTc.spot);
    memcpy(gGuardFrom, from, sizeof gGuardFrom); memcpy(gGuardItem, item, sizeof gGuardItem);
    gAimSince = gFrame;
    StopPath();
    if (gDebug) Log("objective: guarding: the corner of the way from %.0f %.0f %.0f held, %.1f m away (%.0f s)", from[0], from[1], from[2],
                    Dist2D(gMe.pos, watch) / 1000.f, (gTc.until - gFrame) / 60.0);
    return true;
}
bool OneLifeGuard(const float* item, float ring) {
    if (OldFight()) return false;                        // (MGO1_DOLL_OLD_FIGHT: no tactics, the older guard)
    if (Dist2D(gMe.pos, item) > ring + 1000.f || std::fabs(gMe.pos[1] - item[1]) > 3500.f) { gGuardRound = false; return false; }   // (to its sector first)
    Act("guarding_objective");
    if (Following()) { FollowPath(); AvoidItem(item); return true; }   // (its round, walked)
    if (gGuardRound) { gGuardRound = false; gGuardHoldAt = gFrame; }   // (its round's point reached: a hold there)
    if (gFrame >= gGuardHoldAt && gFrame - gHurtAt >= 120) {   // (hurt a moment ago: its fight's moves first)
        if (GuardHoldStart(item)) return TacticRun(nullptr);
        gGuardHoldAt = gFrame + 300;                     // (no corner from here: a round, a hold again in 5 s)
    }
    if (gFrame >= gGuardRoundAt) {                       // (a round planned once in 1.5 s at most: none found, still there)
        gGuardRoundAt = gFrame + 90;
        float g[8][3];
        const int n = GuardPoints(item, ring, 2500.f, g, 8);
        bool found = false;
        for (int i = 0; i < n && !found; ++i)            // (a point of its sector whose way does not cross the item)
            if (PlanTo(g[i])) {
                if (PathNear(item, 1800.f)) { StopPath(); continue; }
                gGuardRound = true; found = true;
                if (gDebug) Log("objective: guarding: a round to %.0f %.0f %.0f", g[i][0], g[i][1], g[i][2]);
            }
        if (!found) gGuardHoldAt = std::min(gGuardHoldAt, gFrame + 60 + (long long)(Rnd() % 60));   // (no round: a hold again soon)
    }
    if (Following()) FollowPath(); else Steer(0, 0, 0);
    AvoidItem(item);
    return true;
}
void GuardHoldEnd() {                                    // (before the fight's choice and the tactic's frame)
    // (a one-life defender, the item carried by an enemy: any move but a reload over, after him at all costs)
    if (gTc.kind != TC_NONE && gTc.kind != TC_RELOAD && !gTc.guard && OneLifeDefender() && EnemyCarrier()) { TacEnd("the objective taken"); return; }
    if (gTc.kind != TC_HOLD || !gTc.guard) return;
    const char* why = nullptr;
    bool heard = false;
    if (!OneLifeDefender() || EnemyCarrier()) why = "the objective taken";
    else if (gCo.kind != CK_NONE || gSet.order == "follow" || gSet.order == "defend" || gSet.order == "target") why = "an order";
    else {
        bool there = false;
        for (const ObjItem& it : SeeObjectives().items) if (!it.holder && Dist2D(it.pos, gGuardItem) < 2000.f) there = true;
        if (!there) why = "the objective moved";
        if (!why && gFrame - gTc.at > 30)                // (an enemy known these 30 frames, out of sight, within 10 m of the item, not
            for (const auto& k : gKnown)                 //  on the way held: a hold toward him, GuardOrigin's latest)
                if (gFrame - k.second.last < 30 && !k.second.visible && Dist2D(k.second.pos, gGuardItem) < 10000.f && Dist2D(k.second.pos, gGuardFrom) > 4000.f) {
                    why = "an enemy close to the objective"; heard = true; break;
                }
        if (!why && gFrame - gTc.at > 120)
            for (const auto& k : gKnown)
                if (gFrame - k.second.heard < 30 && Dist2D(k.second.pos, gMe.pos) < 12000.f && Dist2D(k.second.pos, gGuardFrom) > 6000.f) { why = "steps heard elsewhere"; heard = true; break; }
    }
    if (!why) return;
    TacEnd(why);
    if (heard) gGuardHoldAt = gFrame;                    // (a hold toward the steps at once)
}
// ---- (one-life attackers) noticed these 10 s by an enemy known these 5 s within 30 m, allies within 15 m or the carrier it
// escorts: not toward them, they would be given away. The corner toward him held 3 to 6 s, once in 15 s ----
long long gChasedOff = -1;
bool ChasedHold() {
    if (OldFight() || !OneLifeAttacker() || Carrying() || gFrame < gChasedOff || gTc.kind != TC_NONE) return false;
    int chaser = -1; float cd = 30000.f;
    for (const auto& n : gNoticedAt) {
        const auto k = gKnown.find(n.first);
        const Player* e = PlayerById(n.first);
        if (gFrame - n.second > 600 || !e || !e->alive || k == gKnown.end() || gFrame - k->second.last > 300) continue;
        const float d = Dist2D(k->second.pos, gMe.pos);
        if (d < cd) { cd = d; chaser = n.first; }
    }
    if (chaser < 0) return false;
    bool allies = false;
    for (const Player& a : gPlayers) if (!a.local && a.alive && Ally(a) && (Dist2D(a.pos, gMe.pos) < 15000.f || CarrierOf(a))) allies = true;
    if (!allies) return false;
    gChasedOff = gFrame + 900;
    float g[3], w[3];
    if (!Ground(g) || !WatchCorner(g, gKnown[chaser].pos, w)) return false;
    gTc = Tactic(); gTc.foe = chaser; memcpy(gTc.watch, w, sizeof w); gTc.watchHave = true;
    HoldStart(180 + (long long)(Rnd() % 180), false, "noticed, its allies close");
    return TacticRun(nullptr);
}
// ---- (one-life modes) its moves walked, not run (the stick's larger axis at the fight's step, 0.7): an attacker out of a
// fight and of the fight's moves, not noticed nor hurt these 3 s, the item in place (a carrier and his escort run), near
// the enemies (one known these 20 s within 20 m, or the item within 31 m: 25 m of its guards' ring); a defender's rounds
// within 10 m of the item in place. Not following a player (he sets the pace), near a link (a roll, a vault, a climb, a
// ladder: their own pace), low, rolling, in a view. NIGHTMARE: never (its moves run), but in TEAM SNEAKING (the mode) ----
const char* QuietWhy() {
    const int side = OneLifeSide();
    if (!side || (Nightmare() && gRoom.rule != 5) || OldWays() || gFi.target >= 0 || gTc.kind != TC_NONE || Carrying() || SpottedLately(180)) return nullptr;
    if (gCo.kind == CK_FOLLOW || gSet.order == "follow") return nullptr;
    const Objectives o = SeeObjectives();
    for (const ObjItem& it : o.items) if (it.holder) return nullptr;
    if (side < 0) {
        for (const ObjItem& it : o.items) if (Dist2D(it.pos, gMe.pos) < 10000.f) return "its round about the objective";
        return nullptr;
    }
    for (const auto& k : gKnown) if (gFrame - k.second.last < 1200 && Dist2D(k.second.pos, gMe.pos) < 20000.f) return "an enemy known close";
    for (const ObjItem& it : o.items) if (Dist2D(it.pos, gMe.pos) < 31000.f) return "the guarded objective close";
    return nullptr;
}
bool NearLink(float r) {
    if (!MapReady()) return false;
    for (int i = 0; i < gMap->nav->LinkCount(); ++i) {
        float a[3], b[3]; uint8_t area = 0;
        if (!gMap->nav->LinkAt(i, a, b, &area)) continue;
        if ((Dist2D(a, gMe.pos) < r && std::fabs(a[1] + 800.f - gMe.pos[1]) < 2500.f) || (Dist2D(b, gMe.pos) < r && std::fabs(b[1] + 800.f - gMe.pos[1]) < 2500.f)) return true;
    }
    return false;
}
void QuietSay(const char* why) {                         // (the walk's start and end in the log, 2 s apart at least)
    static bool said = false;
    static long long at = -100000;
    if ((why != nullptr) == said || gFrame - at < 120) return;
    said = why != nullptr; at = gFrame;
    if (!gDebug) return;
    if (said) Log("objective: walking quietly (%s)", why); else Log("objective: running again");
}
void QuietStick() {                                      // (after the brain's frame: the left stick held to a walk)
    if (!gQuiet || (gPad.buttons & 0x0800) || Low() || OnLadder() || Rolling() || Scoped() || gRo.phase || gCq.phase || NearLink(3500.f)) return;
    auto val = [](uint8_t v) { return v >= 0x80 ? (v - 0x80) / 127.f : -(0x80 - v) / 128.f; };
    auto enc = [](float v) { v = std::max(-1.f, std::min(1.f, v)); return (uint8_t)(v >= 0.f ? 0x80 + (int)(127 * v) : 0x80 - (int)(128 * -v)); };
    const float sx = val(gPad.sticks[2]), sy = val(gPad.sticks[3]), big = std::max(std::fabs(sx), std::fabs(sy));
    if (big <= kStep + 0.02f) return;
    gPad.sticks[2] = enc(sx * kStep / big); gPad.sticks[3] = enc(sy * kStep / big);
}
// ---- (v137) one-life attackers, levels 3 and up, once a life (3 lives in 10 at level 3, 4.5 at 4, 6 at 5, a careful taste
// more): the cardboard box on for the quiet walk near the guards (QuietWhy: an enemy known within 20 m, the guarded item
// within 31 m), not noticed nor hurt these 3 s, no fight nor move; off 5 m before an item (its hands to take it), noticed,
// hurt, a fight, the walk over, or 25 s in it. In it, still beyond 3 m and more by level, a doll overlooks it (Sees) ----
struct SneakRun { bool drawn = false, want = false, on = false, done = false; long long at = -1; };
SneakRun gSnk;
bool BoxSneakFrame() {
    if (NoBoxTier()) { gSnk.on = false; return false; }   // ((v141 beta 10) tier 10: none)
    if (!gSnk.drawn) { gSnk.drawn = true; gSnk.want = Lv() >= 3 && Rnd01() < (Lv() >= 5 ? 0.6f : Lv() >= 4 ? 0.45f : 0.3f) + 0.2f * (1.f - gStyle); }
    if (!gSnk.want || gSnk.done) return false;
    bool nearItem = false, closeOne = false;
    if (gQuiet) { const Objectives o = SeeObjectives(); for (const ObjItem& it : o.items) if (Dist2D(it.pos, gMe.pos) < 5000.f) nearItem = true; }
    for (const auto& k : gKnown) if (k.second.visible && Dist2D(k.second.pos, gMe.pos) < 8000.f) closeOne = true;   // (one in sight close: its own wait, WaitHidden)
    const bool ok = gQuiet && OneLifeAttacker() && HasItem(0xE) && !SpottedLately(180) && gFi.target < 0 && gTc.kind == TC_NONE && !nearItem && !closeOne && !Carrying() &&
                    !OnLadder();
    if (gSnk.on) {
        if (!ok || gFrame - gSnk.at > 1500 || (gFrame - gSnk.at > 240 && !InMyBox())) {
            gSnk.on = false; gSnk.done = true;
            Log("objective: out of its box (%s, %.1f s in it)", nearItem ? "an item close" : gFrame - gSnk.at > 1500 ? "25 s" : !gQuiet ? "the quiet walk over" : "noticed, hurt or a fight",
                (gFrame - gSnk.at) / 60.0);
        }
        return false;                                    // (in it: the walk goes on, QuietStick keeps it a walk)
    }
    if (!ok || InMyBox() || !WindowFree(1)) return false;
    if (!ItemTo(0xE)) return false;
    gSnk.on = true; gSnk.at = gFrame;
    Log("objective: its box on for the quiet walk");
    StopPath(); Steer(0, 0, 0); Act("box_slipping");
    return true;
}
// ---- (NIGHTMARE) the cardboard box to get away: after its kill (seen, or him dead within a second of its last hit on him, as
// the master tier's), no other enemy in sight within 30 m; or out of a quick fight that hurt it hard (40 of its life in hits
// under 2 s apart), the one fought out of sight, none in sight, once in 20 s. Never with an enemy known within 8 m these 2 s,
// a move or a hold going on, the objective carried or held by anyone (the chase, the escort), as a one-life defender, in a
// base, nor within 5 s of the last. Up first, the box on, then in it (a run's speed) to a place hidden from the enemies known
// (the fall back's search), 3 to 5 s from the box on, then off; at once (the fight) when one comes within 6 m, aims at it or
// hurts it ----
struct NmBoxRun { int phase = 0; long long at = -1, until = -1, last = -100, endAt = -100000, quickAt = -100000, killAt = -100000; bool reached = false; float spot[3] = {0, 0, 0}; };
NmBoxRun gNm;                                            // (phase 1: the box being put on, 2: in it)
int gNmLifeRef = 0;                                      // its life before this streak of hits (NmWatch, every frame)
void NmWatch() { if (gFrame - gHurtAt > 120 || gMe.life > gNmLifeRef) gNmLifeRef = gMe.life; }
bool NmBoxLive() { return gNm.phase > 0 && gFrame - gNm.last <= (gNm.phase == 1 ? 120 : 2); }   // (not asked a moment: over; the window's frames not counted)
bool NmAims(const Player& e) { return std::fabs(Wrap(AngleTo(e.pos, gMe.pos) - e.facing)) < 15.f * kDeg && (e.bits[1] & 0x4400) != 0; }
// one in sight while it is in its box: not fought beyond 6 m while he does not aim at it and nothing has hit it since it began
bool NmBoxKeeps(const Player& e) { return NmBoxLive() && InMyBox() && gHurtAt <= gNm.at && gStunHitAt <= gNm.at && Dist2D(gMe.pos, e.pos) > 6000.f && !NmAims(e); }
bool NmBoxFrame(int lost) {
    if (CheatOn() || NoBoxTier()) { gNm.phase = 0; return false; }   // ((v141 beta 7) tier 10's cheat: no box to get away; (v141 beta 10) set to 10)
    if (!Nightmare() || OldFight()) return false;
    if (gNm.phase && (!NmBoxLive() || lost >= 0)) { gNm.phase = 0; gNm.endAt = gFrame; }   // (a fight took over meanwhile: over)
    if (!gNm.phase) {
        const Player* f = lost >= 0 ? PlayerById(lost) : nullptr;
        const auto dl = gDealtAt.find(lost);
        const bool kill = (f && !f->alive && ((gKillSeenVictim == lost && gFrame - gKillSeenAt < 180) || (dl != gDealtAt.end() && gFrame - dl->second < 60))) ||
                          (gKillSeenAt > gNm.killAt && gFrame - gKillSeenAt < 60);
        const Player* q = gFrame - gKwFoeAt < 120 ? PlayerById(gKwFoeLast) : nullptr;
        const bool quick = !kill && q && q->alive && Enemy(*q) && !Asleep(*q) && gFrame - gHurtAt < 120 && gNmLifeRef - gMe.life >= 40 && gFrame - gNm.quickAt >= 1200;
        if (!kill && !quick) return false;
        if (kill) gNm.killAt = gFrame + 120;             // (one chance a kill: its count seen a moment later too)
        if (kill && NmV3() && !getenv("MGO1_NM_BOX_ALWAYS") && Rnd01() < 0.5f) return false;   // ((v141 beta 3) back in the box one kill in two)
        if (gFrame - gNm.endAt < 300 || gTc.kind != TC_NONE || !HasItem(0xE) || InMyBox() || Carrying() || IAmSnake() || OneLifeDefender() || OnLadder() ||
            Scoped() || SniperAtSpot() || Prone() || InCrawl(gMe.pos) || !WindowFree(1) || gSet.order == "hide") return false;
        for (const ObjItem& it : SeeObjectives().items) if (it.holder) return false;
        for (const auto& k : gKnown) {
            const float d = Dist2D(k.second.pos, gMe.pos);
            if ((k.second.visible && (quick || d < 30000.f)) || (gFrame - k.second.last < 120 && d < 8000.f)) return false;
        }
        if (gRoom.rule == 6) {                           // (BASE CAPTURE: not out of a base it stands in)
            float bp[5][3]; int bo[5], bc[5];
            const int nb = ps2world::BcBaseStates(bp, bo, bc);
            for (int i = 0; i < nb; ++i) if (Dist2D(gMe.pos, bp[i]) < 3000.f) return false;
        }
        // away from the nearest enemy known, else from where he fell (or was last seen): a standing body's eye there
        std::vector<Threat> th = Threats(kill ? 600 : 180);
        Threat main; bool have = false; float best = 1e30f;
        for (const Threat& t : th) if (Dist2D(t.pos, gMe.pos) < best) { best = Dist2D(t.pos, gMe.pos); main = t; have = true; }
        if (!have) {
            const int id = kill ? (f ? lost : gKillSeenVictim) : q->id;
            const Player* p = PlayerById(id);
            if (!p) return false;
            const auto kn = gKnown.find(id);
            const float* at = kn != gKnown.end() ? kn->second.seenPos : p->pos;
            const float g = GroundOf(at);
            main.id = id; main.visible = false;
            main.pos[0] = main.eye[0] = main.chest[0] = at[0]; main.pos[2] = main.eye[2] = main.chest[2] = at[2];
            main.pos[1] = g + 800.f; main.eye[1] = g + 1560.f; main.chest[1] = g + 1160.f;
            th.push_back(main);
        }
        if (quick) gNm.quickAt = gFrame;
        float spot[3], watch[3]; bool crouch = false, wh = false;
        if (!PickSpot(SF_BACK, th, main, spot, &crouch, watch, &wh)) return false;
        gNm.phase = 1; gNm.at = gFrame; gNm.reached = false; memcpy(gNm.spot, spot, sizeof spot);
        if (gDebug) Log("tactic: box escape %s (cover %.1f m away)", kill ? "after a kill" : "out of a quick fight", Dist2D(gMe.pos, spot) / 1000.f);
        StopPath();
    }
    gNm.last = gFrame;
    bool end = gHurtAt > gNm.at || gStunHitAt > gNm.at;
    for (const auto& k : gKnown) {
        const Player* e = PlayerById(k.first);
        if (e && (((k.second.visible || gFrame - k.second.last < 30) && Dist2D(k.second.pos, gMe.pos) < 6000.f) || (k.second.visible && NmAims(*e)))) end = true;
    }
    for (const ObjItem& it : SeeObjectives().items) if (it.holder) end = true;
    if (gNm.phase == 1 && InMyBox()) { gNm.phase = 2; gNm.until = gFrame + 180 + (long long)(Rnd() % 121); }
    if ((gNm.phase == 1 && gFrame - gNm.at > 150) || (gNm.phase == 2 && (gFrame >= gNm.until || !InMyBox()))) end = true;
    if (gNm.phase == 2 && !gNm.reached && NextLinkDist() < 3000.f) end = true;   // ((v141 beta 3) a ladder or a ledge ahead: not in the box)
    if (end) {
        gNm.phase = 0; gNm.endAt = gFrame; StopPath();
        if (InMyBox() && (TapTo(1, 0) || ItemTo(0))) { Act("leaving_box"); return true; }
        return false;
    }
    if (gNm.phase == 1) {                                // (up first; no R2 out nor view on: the item window alone)
        StopPath(); Steer(0, 0, 0);
        if (Low()) { SetLow(false); Act("standing_up"); return true; }
        if (gFrame >= gR2HoldUntil && gR2Since < 0 && !(gMe.bits[0] & 0x80) && !TapTo(1, 0xE) && !ItemTo(0xE)) { gNm.phase = 0; gNm.endAt = gFrame; return false; }
        Act("box_slipping");
        return true;
    }
    if (!gNm.reached && GoTo(gNm.spot, "hide", 500.f)) gNm.reached = true;
    if (gNm.reached) { StopPath(); Steer(0, 0, 0); }
    Act(gNm.reached ? "hiding" : "box_slipping");        // (its cover reached: still in its box, as Hide's)
    return true;
}
bool gBoxRunner = false;                                 // (v141 beta 3, NIGHTMARE) this life in its box while it moves (NmBoxRoam)
long long gBoxRunSince = -1, gBoxRunOffUntil = -1;      // ((v141 beta 10) its spell in the box since; none until (a life's own))
long long gBoxRoamAt = -100000;                          // (the box last put on for it)
// (v141 beta 3) NIGHTMARE's box runner: not carrying, not Snake, not a sniper at its spot, not low nor in a vent nor on a
// ladder, not within 6 m of its path's next link (a ladder, a ledge, a vault: not taken in the box), not hurt these 5 s, no
// enemy seen these 8 s within 40 m nor heard these 4 s within 15 m
long long gBoxLinkAt = -100000;                          // (a link ahead last seen: the box not on again these 10 s, no on and off by it)
bool NmBoxRoam() {
    if (NmV3() && gBoxRunner && NextLinkDist() < 6000.f) {
        if (gDebug && InMyBox() && gFrame - gBoxLinkAt > 600) Log("tactic: the box off, a link ahead (%.1f m)", NextLinkDist() / 1000.f);
        gBoxLinkAt = gFrame;
    }
    if (CheatOn()) return false;                         // ((v141 beta 7) tier 10's cheat: no box runner)
    if (!NmV3() || !gBoxRunner || getenv("MGO1_NM_NO_BOXRUN") || !HasItem(0xE) || Carrying() || IAmSnake() || SniperAtSpot() || Prone() || InCrawl(gMe.pos) ||
        OnLadder() || gSet.order == "hide" || gFrame - gHurtAt < 300 || OneLifeDefender() || gFrame - gBoxLinkAt < 600) {
        gBoxRunSince = -1;
        return false;
    }
    // ((v141 beta 10) the players: "most of the time they run around in a circle with the cardboard box". Not for a doll set to 10, not in
    // the objective rules, only on its way to an enemy known these 10 s (no roaming nor item runs in it), 10 s a spell, 30 s between)
    long long& since = gBoxRunSince;
    const bool b8 = CheatWaitsB8();
    if (!b8) {
        if (NoBoxTier() || (gRule != 0 && gRule != 1) || gRoom.rule >= 5 || gFrame < gBoxRunOffUntil) { since = -1; return false; }
        bool chase = false;
        for (const auto& k : gKnown) if (gFrame - k.second.last < 600 && Dist2D(k.second.pos, gMe.pos) >= 40000.f) chase = true;
        if (!chase) { since = -1; return false; }
    }
    for (const auto& k : gKnown) {
        const float d = Dist2D(k.second.pos, gMe.pos);
        if ((k.second.visible || gFrame - k.second.seen < 480) && d < 40000.f) return false;
        if (gFrame - k.second.heard < 240 && d < 15000.f) return false;
    }
    if (!b8) {
        if (!InMyBox()) since = -1;
        else if (since < 0) since = gFrame;
        else if (gFrame - since > 600) { since = -1; gBoxRunOffUntil = gFrame + 1800; if (gDebug) Log("tactic: the box run over (10 s)"); return false; }
    }
    return true;
}
// ---- (v141 beta 6, aim) the box pops (the players' "box popping", sections 67 and 126 of the requirements): the cardboard box on and
// off by a tap of L2 (v141 beta 3's taps: off 4 frames after the release, on again 10 frames after; ELITE and up, the box the item held
// before), else by the item window (46 frames: the slower moments only). Its moments, each by the tier's chance (kFight's boxPop: none
// at TRAINING and EASY) and never within the tier's gap of the last one (boxGap: 40 s at NORMAL, 12 s at PRO), each said once
// ("DOLL BOX: on|off (<reason>) <enemy> <dist> m"):
// - under_fire (BoxPopFight; the taps): hit by the one it fights a moment ago, him 4 to 25 m away, the doll standing in the open
//   with a third of its life: in its box a moment (36 frames at ELITE, 25 at PRO), out at once if hit in it or him within 3 m, then
//   out by a tap and its fire back (its reaction short: expected there; the auto aim at once)
// - break_los (TacticLull): its fight lost out of his sight, the corner he left by held in its box (the ambush)
// - approach (BoxApproachFrame): out of a fight, an enemy known these 8 s, not in sight, 8 to 30 m away: in its box on its way to him
//   (the chase goes on in it), once a sighting; over when he shows (the pop out), is lost (8 s), it is hit, a link ahead, 12 s
// - pop_out (BoxPopOut): in its box (any of these, the box runner, the box escape), an enemy in sight: out of it at once by a tap
//   (the fight's own first move), its reaction short (it saw him come), the auto aim if drawn
// Never carrying, as Snake, a one-life defender, a sniper at its spot, low or in a vent, on a ladder. MGO1_DOLL_BOXPOP=0: none ----
struct BoxPop { int phase = 0, foe = -1; long long at = -1, until = -1, last = -100; };
BoxPop gBp;                                              // (phase 1: the box going on, 2: in it, 3: coming out)
long long gBpDrawAt = -100000;                           // (under fire: drawn once a second at most)
struct BoxWay { bool on = false; int foe = -1, drawnFoe = -1; long long at = -1, drawnSeen = -1; };
BoxWay gBw;
bool BoxTapReady() { const Inventory& v = Inv(); return NmV3() && kTapL2 && gTapFails[1] < 3 && v.ok && v.item == 0 && gItemPrev == 0xE && gTp.win < 0; }
bool BoxPopBarred() {
    return !BoxPopOn() || !HasItem(0xE) || Carrying() || IAmSnake() || OneLifeDefender() || SniperAtSpot() || Prone() || InCrawl(gMe.pos) || OnLadder() ||
           gSet.order == "hide" || Scoped();
}
void BoxPopNote(const Player& e) { if (gEn.id == e.id) ++gEn.boxPops; else { gBpPendingId = e.id; gBpPendingAt = gFrame; } }
// the expected reaction and the auto aim's at once: him where it saw him from its box (FightFrame's "expected", AutoAimFrame's spell)
void BoxPopAim(const Player& e) {
    BodyPoint(e.pos, 0.45f, gPre.at); gPre.until = gFrame + 90; gPre.fpv = false;
}
bool BoxPopFight(const Player& e) {
    if ((CheatOn() || NoBoxTier()) && !gBp.phase) return false;   // ((v141 beta 7) tier 10's cheat: no box pop (one going on ends by itself))
    const float d = Dist2D(gMe.pos, e.pos);
    if (gBp.phase && gFrame - gBp.last > 8) gBp.phase = 0;   // (not asked a moment, a tap's 4 frames apart: another move took over)
    if (gBp.phase) {
        gBp.last = gFrame;
        StopPath(); Steer(0, 0, 0); Act("box_popping", e.name);
        if (gBp.phase == 1) {                                // (the tap's frames, then the game's answer: in it, else given up)
            if (InMyBox()) { gBp.phase = 2; gBp.until = gFrame + std::max(18, (int)std::lround(50.f - 30.f * Fk().boxPop)) + (long long)(Rnd() % 8); }
            else if (gFrame - gBp.at > 40) { gBp.phase = 0; return false; }
            return true;
        }
        if (gBp.phase == 2) {                                // in it: still; out when its time is up, hit in it (no shelter), him within 3 m
            const bool hit = gHurtAt > gBp.at + 12;
            if (gFrame < gBp.until && !hit && d > 3000.f && InMyBox()) return true;
            gBp.phase = 3; gBp.at = gFrame;
            BoxSay("off", hit ? "under_fire_hit_in_box" : d <= 3000.f ? "under_fire_him_close" : "under_fire_pop_out", e.name, d);
            BoxPopAim(e);
        }
        if (!InMyBox() || gFrame - gBp.at > 60) { gBp.phase = 0; return false; }   // (out: the fight's own frames)
        if (TapTo(1, 0) || ItemTo(0)) return true;
        gBp.phase = 0;
        return false;
    }
    // the moment: hit by him these 10 frames, him 4 to 25 m away (closer: its fire at once), standing in the open (no tactic, not low
    // nor against a wall), a third of its life left at least, the tap ready, the tier's gap; the tier's chance drawn once a second
    const FightSkill& f = Fk();
    if (BoxPopBarred() || f.boxPop <= 0.f || gFrame - gBoxPopAt < f.boxGap || gFrame - gBpDrawAt < 60 || gFrame - gHurtAt >= 10 || !HitBy(e) || d < 4000.f ||
        d > 25000.f || Low() || AgainstWall() || gTc.kind != TC_NONE || (gMyMaxLife > 0 && gMe.life * 3 < gMyMaxLife) || !BoxTapReady())
        return false;
    gBpDrawAt = gFrame;
    if (Rnd01() >= f.boxPop || !TapTo(1, 0xE)) return false;
    gBp = BoxPop(); gBp.phase = 1; gBp.at = gBp.last = gFrame; gBp.foe = e.id; gBoxPopAt = gFrame;
    BoxSay("on", "under_fire", e.name, d);
    BoxPopNote(e);
    StopPath(); Steer(0, 0, 0); Act("box_popping", e.name);
    return true;
}
// in its box, an enemy in sight: the fight's own lines take the box off (a tap when it can); the pop out said once a box spell, its
// reaction short (BoxPopAim)
long long gBoxOutSaidAt = -100000;
void BoxPopOut(const Player& e) {
    if (!BoxPopOn() || gBp.phase || gFrame - gBoxOutSaidAt < 120) return;
    gBoxOutSaidAt = gFrame;
    const char* from = gBw.on ? "approach" : gTc.box || (gFrame - gBoxWhyAt < 1200 && *gBoxWhy) ? (gTc.box ? "break_los" : gBoxWhy) : NmBoxLive() ? "box_escape" :
                       gBoxRunner ? "box_runner" : "box";
    gBoxWhy = ""; gBoxWhyAt = -100000;
    gBw.on = false;
    std::string why = std::string("pop_out_") + from;
    BoxSay("off", why.c_str(), e.name, Dist2D(gMe.pos, e.pos));
    BoxPopNote(e);
    BoxPopAim(e);
}
bool BoxApproachLive() { return gBw.on; }
bool BoxApproachFrame() {
    if (CheatOn() || NoBoxTier()) { if (gBw.on) { gBw.on = false; if (gDebug) Log("DOLL BOX: approach over (tier 10)"); } return false; }   // ((v141 beta 7) none)
    const FightSkill& f = Fk();
    if (gBw.on) {                                        // (in it: the chase goes on, Hunt; over when...)
        const auto kn = gKnown.find(gBw.foe);
        const char* end = !InMyBox() && gFrame - gBw.at > 90 ? "box_gone" : kn == gKnown.end() || gFrame - kn->second.last > 480 ? "lost_him" :
                          gFrame - gHurtAt < 30 ? "hit" : NextLinkDist() < 4000.f ? "link_ahead" : gFrame - gBw.at > 720 ? "time_up" : BoxPopBarred() ? "barred" : nullptr;
        if (end) {
            gBw.on = false;
            if (gDebug) Log("DOLL BOX: approach over (%s)", end);   // (the box off by the brain's own line)
        }
        return false;
    }
    if (BoxPopBarred() || f.boxPop <= 0.f || gFrame - gBoxPopAt < f.boxGap || InMyBox() || gFrame - gHurtAt < 300 || NextLinkDist() < 8000.f || !WindowFree(1) ||
        OneLifeMode() || gTc.kind != TC_NONE)
        return false;
    int foe = -1; float fd = 0.f; long long seen = -1;
    for (const auto& kv : gKnown) {
        const Known& k = kv.second;
        if (k.visible || gFrame - k.last > 480 || gFrame - k.seen < 60) continue;
        const float d = Dist2D(gMe.pos, k.pos);
        const Player* e = PlayerById(kv.first);
        if (d < 8000.f || d > 30000.f || !e || !Enemy(*e) || !e->alive || Asleep(*e)) continue;
        if (foe < 0 || d < fd) { foe = kv.first; fd = d; seen = k.seen; }
    }
    if (foe < 0 || (gBw.drawnFoe == foe && gBw.drawnSeen == seen)) return false;   // (drawn once a sighting)
    gBw.drawnFoe = foe; gBw.drawnSeen = seen;
    if (Rnd01() >= f.boxPop * 0.5f) return false;
    if (!(BoxTapReady() ? TapTo(1, 0xE) : false) && !ItemTo(0xE)) return false;
    gBw.on = true; gBw.foe = foe; gBw.at = gFrame; gBoxPopAt = gFrame; gBoxWhy = "approach"; gBoxWhyAt = gFrame;
    const Player* e = PlayerById(foe);
    BoxSay("on", "approach", e ? e->name : std::string("?"), fd);
    Act("box_on");
    return true;
}
// ---- (v141 beta 7) tier 10's cheat out of a fight. An enemy within 40 m ((v141 beta 8); 25 m before) on its floor coming toward it out
// of its sight: its gun waits on where he will show, the last point of the way between them its eye sees, at a head's height
// (his_corner; no such corner: his head through the cover, his_line), readied in the third person, or held in the first person view
// (its ambush: the corner 3 to 40 m away, his head 4 to 40 m away; the view's entry, 16 to 50 frames, made before he shows), entered
// while he is hidden, its body turned on the point first; one waiting at his spawn: his head (his_spawn_wait; the view from 4.5 m). Out of
// it the moment it must move. In a calm moment (no enemy in sight, the nearest 12 m away and more) its magazine under a third fired out
// (the game reloads at the next pull). Its hunt: the nearest enemy along the mesh, his place every frame ----
std::map<int, int> gMagMax;                          // each gun's fullest magazine seen (its size)
void CheatMagWatch() { const int h = Hand(), m = MagNow(); if (h > 0 && m > 0) { int& x = gMagMax[h]; x = std::max(x, m); } }
int MagSize(int h) { const auto it = gMagMax.find(h); return it == gMagMax.end() ? -1 : it->second; }
// ((v141 beta 8) off unless MGO1_DOLL_CHEAT_TOPUP=1: a doll firing its magazine out at nothing looks like no player (Snake's video of
// 14 October: a doll emptying its gun into a rock and the snow))
bool CheatTopUpOn() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_CHEAT_TOPUP"); v = e && *e == '1' ? 1 : 0; } return v == 1; }
bool CheatTopUp() {
    if (!CheatTopUpOn()) { gCheat.topUpAt = gCheat.reloadAt = -1; return false; }
    const int h = Hand(), mag = MagNow(), size = MagSize(h);
    bool seen = false;
    for (const auto& k : gKnown) if (k.second.visible) seen = true;
    const float near = CheatEnemyDist();
    const bool may = CheatOn() && gFi.target < 0 && !Carrying() && !OnLadder() && !Scoped() && !BoxOn() && Trigger() == TK_AUTO && !seen &&
                     gFrame - gHurtAt > 120 && gGr.phase == 0 && !RadioPressing() && near >= 12000.f;
    if (gCheat.topUpAt < 0) {
        if (!may || Reloading() || mag <= 0 || size <= 0 || mag * 3 >= size || Rounds(h) < mag + size) return false;
        float mz[3]; Muzzle(mz);
        const float far[3] = {mz[0] + std::sin(Facing()) * 40000.f, mz[1], mz[2] + std::cos(Facing()) * 40000.f};
        if (AllyInTheWay(mz, far)) return false;
        gCheat.topUpAt = gFrame; gCheat.reloadAt = -1;
        if (gDebug) Log("cheat: its magazine fired out to reload (%d of %d rounds left, the nearest enemy %.0f m away)", mag, size, near / 1000.f);
    }
    if (!may || Reloading() || mag < 0 || gFrame - gCheat.topUpAt > 240) { gCheat.topUpAt = gCheat.reloadAt = -1; return false; }
    StopPath(); Steer(0, 0, 0);
    Act("reloading"); Decide("RELOAD", "magazine_top_up");
    gPad.buttons |= 0x8000;
    if (mag > 0) { gPad.pressures[11] = gFrame - gCheat.topUpAt < 8 ? 0x3F : 0xFF; return true; }
    // empty: the trigger let go 4 frames, readied, pulled (the game's reload at that pull)
    if (gCheat.reloadAt < 0) gCheat.reloadAt = gFrame;
    const long long u = (gFrame - gCheat.reloadAt) % 30;
    if (u < 4) { gPad.buttons &= ~0x8000u; gPad.pressures[11] = 0; }
    else gPad.pressures[11] = u < 16 ? 0x3F : 0xFF;
    return true;
}
// the corner he will come round from its ground g, its eye at eye: along the way to him, the last point the eye sees (a head's height
// there) before the way leaves its sight, 2 to 15 m from it; false: all of the way seen, or none of it
bool CheatCorner(const float* g, const float* eye, const float* foe, float* out) {
    const float fg[3] = {foe[0], foe[1] - 800.f, foe[2]};
    float f[3];
    std::vector<Waypoint> w; float len = 0;
    if (!MapReady() || !gMap->nav->Nearest(fg, 1500.f, f) || !gMap->nav->Path(g, f, w, &len)) return false;
    float prev[3] = {g[0], g[1], g[2]}, last[3] = {0, 0, 0};
    bool any = false; float walked = 0;
    const float range = CheatWaitRange();
    for (size_t i = 0; i < w.size() && walked < range + 7000.f; ++i) {
        const float* b = w[i].p;
        const float seg = std::sqrt((b[0] - prev[0]) * (b[0] - prev[0]) + (b[2] - prev[2]) * (b[2] - prev[2]));
        for (float d = 400.f; d <= seg; d += 400.f) {
            const float k = d / seg, s[3] = {prev[0] + (b[0] - prev[0]) * k, prev[1] + (b[1] - prev[1]) * k, prev[2] + (b[2] - prev[2]) * k};
            const float head[3] = {s[0], s[1] + 1450.f, s[2]};
            walked += 400.f;
            if (LineClear(eye, head)) { memcpy(last, s, sizeof last); any = true; continue; }
            if (walked < 1200.f) continue;                // (its own cover at its side)
            if (!any) return false;
            const float r = Dist2D(g, last);
            if (r < 2000.f || r > range) return false;
            memcpy(out, last, sizeof last); out[1] += 1450.f;
            return true;
        }
        memcpy(prev, b, sizeof prev);
    }
    return false;
}
// the first person view held on a point (R1; the stick still until the view is on); true when on it
bool CheatHoldView(const float* at) {
    gPad.buttons |= 0x0800;
    float m[16];
    if (!ps2world::ViewMatrix(m)) return false;
    static bool was = false;
    const bool viewOn = (gMe.bits[0] & 0x80) != 0;
    if (viewOn && !was) gAimSince = gFrame;
    was = viewOn;
    if (!viewOn) { gPad.sticks[2] = gPad.sticks[3] = 0x80; return false; }
    const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    const float tx = at[0] - m[12], tz = at[2] - m[14], ty = at[1] - m[13];
    bool on = false;                                     // ((v150-host beta 18) the direct aim: the view's angles written)
    if (DirectAim() && DirectView(std::atan2(tx, tz), std::atan2(ty, std::sqrt(tx * tx + tz * tz)), 0.6f * kDeg, 0.6f * kDeg, &on)) return on;
    return AimView(camYaw, camPitch, std::atan2(tx, tz), std::atan2(ty, std::sqrt(tx * tx + tz * tz)), 1.f, 0.1f, 0.6f * kDeg, 0.6f * kDeg);
}
void CheatPreEnd() { gCheat.preFor = -1; gCheat.preView = false; gCheat.viewAt = -1; gCheat.preWhy = ""; }
// coming: closing 8 mm a frame, or 0.2 m nearer than a quarter of a second before; kept half a second (no flicker at his turns)
// ((v141 beta 10) his own steps only: his distance to where the doll stood at the sample, not the doll's own walk toward him)
struct CheatDist { float d = 0.f; float me[3] = {0, 0, 0}; long long at = -100000, comingAt = -100000; };
std::map<int, CheatDist> gCheatDistAt;
bool CheatComing(const Player& e, float d, int keep = 30) {   // ((v141 beta 8) keep: the one waited for already, 90 frames)
    CheatDist& r = gCheatDistAt[e.id];
    if (gFrame - r.at > 30) { r.d = d; r.at = gFrame; memcpy(r.me, gMe.pos, sizeof r.me); }   // (not looked at a while: afresh)
    const bool closing = (e.vel[0] * (gMe.pos[0] - e.pos[0]) + e.vel[1] * (gMe.pos[2] - e.pos[2])) / std::max(1.f, d) > 8.f;
    const float now = CheatWaitsB8() ? d : Dist2D(r.me, e.pos);
    if (closing || now < r.d - 200.f) r.comingAt = gFrame;
    if (gFrame - r.at >= 15) { r.d = d; r.at = gFrame; memcpy(r.me, gMe.pos, sizeof r.me); }
    return gFrame - r.comingAt <= keep;
}
// its facing turned on a point by the readied turn (Square readied, the stick led past it); true when within tol
bool CheatFace(const float* at, float tol) {
    gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
    float mz[3]; Muzzle(mz);
    // ((v150-host beta 18) the direct aim: its facing written toward the point (the switch's own write), no readied walk)
    if (DirectAim() && DirectFacing(AngleTo(mz, at))) { Steer(0, 0, 0); return std::fabs(Wrap(AngleTo(mz, at) - gDirectFace)) <= tol; }
    const float err = Wrap(AngleTo(mz, at) - Facing()), a = std::fabs(err);
    if (a <= 1.f * kDeg) { Steer(0, 0, 0); return a <= tol; }
    // ((v141 beta 8) the point within 2.5 m: the turn in place, never the readied walk (it walked into him and round him: Snake's video
    // of 14 October at a player's spawn point); (v141 beta 9) and straight at it, no lead (gLedAt))
    const bool close = Dist2D(gMe.pos, at) < 2500.f;
    const float way = close ? Facing() + err : CheatTurnWay(err);
    if (std::fabs(Wrap(way - Facing() - err)) > 4.f * kDeg) gLedAt = gFrame;
    Steer(std::sin(way), std::cos(way), close ? TurnPush() : a > 30.f * kDeg ? 0.72f : a > 8.f * kDeg ? 0.64f : TurnPush());
    return a <= tol;
}
// ((v141 beta 8) one waiting at his spawn point within 6 m: it runs out to 6 m from him first (away from him, else to a side; a drop or
// nothing: no run), 3 s at most, before its view is held on his head; never at his spawn point itself (Snake's video of 14 October: a
// doll against a player at his spawn, turning round him, its auto aim never on; a path planned there left it standing); true while it
// goes
bool CheatSpawnBack(const Player& e) {
    static long long since = -1, lastAt = -100; static int forId = -1;
    if (forId != e.id || gFrame - lastAt > 30) { since = gFrame; forId = e.id; }
    lastAt = gFrame;
    const float d = Dist2D(gMe.pos, e.pos);
    if (gFrame - since > 180 || d >= 6000.f) return false;
    float ax = gMe.pos[0] - e.pos[0], az = gMe.pos[2] - e.pos[2];
    if (ax * ax + az * az < 1e4f) { ax = -std::sin(Facing()); az = -std::cos(Facing()); }
    float way = std::atan2(ax, az);
    if (!StepGround(way, 700.f)) {
        const float w1 = way + 1.5707963f, w2 = way - 1.5707963f;
        if (StepGround(w1, 700.f)) way = w1; else if (StepGround(w2, 700.f)) way = w2; else return false;
    }
    if (gFrame == since && gDebug) Log("cheat: %s at his spawn point %.1f m away: out to 6 m first", LogWho(e).c_str(), d / 1000.f);
    StopPath();
    Act("repositioning", e.name); Decide("MOVE", "cheat_spawn_back");
    Steer(std::sin(way), std::cos(way), 1.f);
    return true;
}
bool CheatPreAimFrame() {
    float g[3];
    if (!CheatOn() || Carrying() || OnLadder() || SniperInHand() || BoxOn() || gTc.kind != TC_NONE || gGr.phase || gRo.phase || gCq.phase || NoAmmo() ||
        IAmSnake() || gSet.order == "hide" || ThrowableInHand() || !Ground(g)) { CheatPreEnd(); return false; }
    float eye[3]; MyEye(eye);                            // (its standing eye: the view's own moves as it comes on)
    // the one it waits for: coming, out of sight, within 40 m on its floor ((v141 beta 8) 25 m before); or waiting at his spawn within
    // 25 m, his head in its eye's sight. Kept while he still is (41 m; at his spawn, whatever the line): a frame's doubt ends no wait;
    // another one 6 m nearer takes it ((v141 beta 8) 4 m before)
    const bool b8 = CheatWaitsB8();
    const float range = CheatWaitRange();
    if (!b8 && gFrame < gCheat.noWaitAll && gCheat.preFor < 0) return false;
    auto qualifies = [&](const Player& e, bool keep, bool* sw) -> bool {
        const float d = Dist2D(gMe.pos, e.pos);
        const auto kn = gKnown.find(e.id);
        const bool vis = kn != gKnown.end() && kn->second.visible;
        *sw = false;
        if (SpawnGuarded(e) && d < 25000.f) {
            float hd[3]; HeadPoint(e, hd);
            *sw = keep || LineClear(eye, hd);
            return *sw;
        }
        // ((v141 beta 10) waited for in vain a moment ago: not again a while)
        const auto nw = gCheat.noWait.find(e.id);
        if (!b8 && nw != gCheat.noWait.end() && gFrame < nw->second) return false;
        // ((v141 beta 8) the one waited for kept 90 frames after his last step its way (30 to begin); (v141 beta 10) within 12 m only)
        const int k = keep && (b8 || d < 12000.f) ? 90 : 30;
        return !vis && d < (keep ? range + 1000.f : range) && std::fabs(e.pos[1] - gMe.pos[1]) < 2500.f && CheatComing(e, d, k);
    };
    const Player* best = nullptr; float bd = 1e30f; bool spawnWait = false;
    const bool focusDm = CheatFocusDm();                 // ((v141 beta 8) its focus: in a deathmatch, its waits for the real players alone)
    if (gCheat.preFor >= 0) {
        const Player* c = PlayerById(gCheat.preFor);
        bool sw = false;
        if (c && Enemy(*c) && !Asleep(*c) && (!focusDm || RealFoe(*c)) && qualifies(*c, true, &sw)) { best = c; bd = Dist2D(gMe.pos, c->pos) - 6000.f; spawnWait = sw; }
    }
    for (const Player& e : gPlayers) {
        if (!Enemy(e) || Asleep(e) || (best && &e == best) || (focusDm && !RealFoe(e))) continue;
        bool sw = false;
        const float d = Dist2D(gMe.pos, e.pos);
        if (d < bd && qualifies(e, false, &sw)) { bd = d; best = &e; spawnWait = sw; }
    }
    if (!best) { CheatPreEnd(); return false; }
    const Player& e = *best;
    bd = Dist2D(gMe.pos, e.pos);
    // ((v141 beta 10) a wait's budget: 1.5 s beyond 12 m, 2 s nearer; then no wait for him 4 s (2 s within 10 m) nor any wait 2 s, the
    // hunt goes on)
    if (gCheat.preFor != e.id || gFrame - gCheat.waitLast > 1) gCheat.waitAt = gFrame;
    gCheat.waitLast = gFrame;
    if (!b8 && !spawnWait && gFrame - gCheat.waitAt > (bd > 12000.f ? 90 : 120)) {
        gCheat.noWait[e.id] = gFrame + (bd > 10000.f ? 240 : 120); gCheat.noWaitAll = gFrame + 120;
        if (gDebug) Log("cheat: %s not shown in %.1f s (%.1f m): the hunt again", LogWho(e).c_str(), (gFrame - gCheat.waitAt) / 60.0, bd / 1000.f);
        CheatPreEnd();
        return false;
    }
    float at[3]; const char* why; bool view;
    if (spawnWait) {
        HeadPoint(e, at); why = "his_spawn_wait";
        if (bd < 6000.f && CheatSpawnBack(e)) { gPre.fpv = false; return true; }   // ((v141 beta 8) never at his spawn point itself)
        view = bd >= 4500.f;
    }
    else {
        // (its corner looked for again every 6 frames, or for a new one)
        static long long cornerAt = -100; static int cornerFor = -1;
        if (cornerFor != e.id || gFrame - cornerAt >= 6) { cornerFor = e.id; cornerAt = gFrame; gCheat.cornerHave = CheatCorner(g, eye, e.pos, gCheat.corner); }
        if (gCheat.cornerHave) { memcpy(at, gCheat.corner, sizeof at); why = "his_corner"; const float r = Dist2D(gMe.pos, at); view = r >= 3000.f && r <= range; }
        else { HeadPoint(e, at); why = "his_line"; view = bd >= 4000.f && bd <= range; }
    }
    view = view && CheatAmbushOn();
    // the view kept once begun for him (its own entry 16 to 35 frames: not dropped at a frame's other corner)
    if (gCheat.preFor == e.id && gCheat.preView && gCheat.viewAt >= 0 && !view) view = true;
    if (gCheat.preFor != e.id || strcmp(gCheat.preWhy, why) || gCheat.preView != view) {
        if (gCheat.preFor != e.id) { ++gCheat.preAims; ++gCheat.preAimsAll; gCheat.viewAt = -1; }
        if (!view) gCheat.viewAt = -1;
        gCheat.preFor = e.id; gCheat.preWhy = why; gCheat.preView = view;
        if (gDebug) Log("cheat: the gun on where %s will show (%s%s, %.1f m)", e.name.c_str(), why, view ? ", the view" : "", bd / 1000.f);
    }
    StopPath();
    Act("holding_corner", e.name); Decide("PRE_AIM", why);
    memcpy(gPre.at, at, sizeof gPre.at); gPre.until = gFrame + 20;
    // its ambush: its body turned on the point first (the view comes on along its facing, and turns slowly), then R1 and the view held
    // there; else readied in the third person, its facing on the point (the readied turn, led)
    if (view && gCheat.viewAt < 0 && !(gMe.bits[0] & 0x80) && !CheatFace(at, 15.f * kDeg)) { gPre.fpv = false; return true; }
    if (view) {
        if (gCheat.viewAt < 0) gCheat.viewAt = gFrame;
        gPre.fpv = true;
        Steer(0, 0, 0); CheatHoldView(at);
        return true;
    }
    gPre.fpv = false;
    CheatFace(at, 1.f * kDeg);
    return true;
}
bool CheatCalm() {
    if (CheatTopUp()) return true;
    return CheatPreAimFrame();
}
// ---- (v141 beta 8) tier 10's peek: hunting one out of sight 8 to 40 m away (straight, his floor within 3 m), the doll stops where a
// lean of its eye (0.5 m aside, probe 26) would see his head and its own does not: its body turned on him, the first person view held
// on his head through the cover, then the lean to that side; the fight begins the moment he is in sight from the leaned eye, its view
// on him already, its lean kept (Snake's game of 14 October: the doll walked into his sight in the third person, his gun already on
// it; its view came 28 to 44 frames after R1). Given up 150 frames after its stop without its lean, 50 frames into its lean without
// him in sight: that place left alone 3 s. Not within 8 m (a first run of the bench b8_focus: its peek at 5 m began a fight of the
// view at close range, lost to the third person's auto aim). MGO1_DOLL_CHEAT_PEEK=0: none ----
bool CheatPeekAllowed() { static int v = -1; if (v < 0) { const char* e = getenv("MGO1_DOLL_CHEAT_PEEK"); v = e && *e == '0' ? 0 : 1; } return v == 1; }
// its lean's side for one hunted: 1 right, -1 left, 0 none (the eye unleaned at eye, the lean's eye 0.5 m aside from the line to him,
// no wall in the lean's way); his head at hd
int CheatPeekSide(const float* eye, const float* hd) {
    const float a = std::atan2(hd[0] - eye[0], hd[2] - eye[2]), rx = -std::cos(a), rz = std::sin(a);
    const float r[3] = {eye[0] + rx * kLeanSide, eye[1], eye[2] + rz * kLeanSide}, l[3] = {eye[0] - rx * kLeanSide, eye[1], eye[2] - rz * kLeanSide};
    if (LineClear(eye, r) && LineClear(r, hd)) return 1;
    if (LineClear(eye, l) && LineClear(l, hd)) return -1;
    return 0;
}
bool CheatPeekFrame(const Player& e) {
    const float d = Dist2D(gMe.pos, e.pos);
    const bool may = CheatPeekAllowed() && CheatAmbushOn() && e.alive && d > 8000.f && d < kCheatPeekFor && std::fabs(e.pos[1] - gMe.pos[1]) < 3000.f &&
                     !Carrying() && !OnLadder() && !SniperInHand() && !BoxOn() && !NoAmmo() && !Scoped() && !Asleep(e) && Trigger() == TK_AUTO;
    if (!may) { if (gPk.id >= 0) CheatPeekEnd("no more"); return false; }
    float hd[3]; HeadPoint(e, hd);
    if (gPk.id >= 0 && gPk.id != e.id) CheatPeekEnd("another hunted");
    if (gPk.id < 0) {
        if (gFrame < gPk.offUntil && Dist2D(gMe.pos, gPk.spot) < 2000.f) return false;   // (this place given up a moment)
        if (gMe.bits[0] & 0x80) return false;            // (a view on already: not a hunt's walk)
        float eye[3]; MyEye(eye);
        if (LineClear(eye, hd)) return false;            // (in sight from its eye: the fight's)
        const int side = CheatPeekSide(eye, hd);
        if (!side) return false;
        gPk.id = e.id; gPk.side = side; gPk.at = gFrame; gPk.leanAt = -1; gPk.viewAt = -1; memcpy(gPk.spot, gMe.pos, sizeof gPk.spot); ++gPk.peeks;
        gCheat.viewAt = -1;
        if (gDebug) Log("cheat: a peek at %s (%.1f m), its lean to the %s", LogWho(e).c_str(), d / 1000.f, side > 0 ? "right" : "left");
    }
    if (gPk.leanAt < 0 && gFrame - gPk.at > (CheatWaitsB8() ? 150 : 120)) { CheatPeekEnd("the view not on him"); return false; }
    if (gPk.leanAt >= 0 && gFrame - gPk.leanAt > 50) { CheatPeekEnd("not in sight leaned"); return false; }
    StopPath();
    Act("holding_corner", e.name); Decide("PRE_AIM", "his_edge");   // (MGO1 Host: "gun on the corner, waiting for ...")
    memcpy(gPre.at, hd, sizeof gPre.at); gPre.until = gFrame + 20;
    const bool viewOn = (gMe.bits[0] & 0x80) != 0;
    // ((v141 beta 10) its own stamp: gCheat.viewAt is reset by CheatPreAimFrame each frame without a wait)
    long long& vAt = CheatWaitsB8() ? gCheat.viewAt : gPk.viewAt;
    if (!viewOn && vAt < 0 && !CheatFace(hd, 15.f * kDeg)) { gPre.fpv = false; return true; }
    if (vAt < 0) vAt = gFrame;
    if (gCheat.viewAt < 0) gCheat.viewAt = vAt;
    gPre.fpv = true;
    Steer(0, 0, 0);
    const bool aimed = CheatHoldView(hd);
    if (viewOn && (aimed || gPk.leanAt >= 0 || gFrame - vAt > 60)) {
        if (gPk.leanAt < 0) gPk.leanAt = gFrame;
        if (gPk.side > 0) { gPad.buttons |= 0x0200; gPad.pressures[5] = 0xFF; gR2HoldUntil = std::max(gR2HoldUntil, gFrame + 24); }
        else { gPad.buttons |= 0x0100; gPad.pressures[4] = 0xFF; }
    }
    return true;
}
// ---- (v141 beta 8) its focus's spacing: hunting a real player in a deathmatch, another doll within 5 m of it and 0.5 m nearer to him:
// it holds 90 frames (its gun readied on his head through the cover), then hunts on 2 s whatever (the bench b8_focus: two dolls of
// the focus within 4 m of each other a fifth of their time, walking into his sight together, one blocking the other's line) ----
bool CheatSpacing(const Player& e) {
    static long long waitAt = -1, offUntil = -1;
    if (!CheatFocusDm() || !RealFoe(e) || gFrame < offUntil) { waitAt = -1; return false; }
    const float mine = Dist2D(gMe.pos, e.pos);
    bool ahead = false;
    for (const Player& p : gPlayers)
        if (Enemy(p) && !RealFoe(p) && !Asleep(p) && Dist2D(p.pos, gMe.pos) < 5000.f && std::fabs(p.pos[1] - gMe.pos[1]) < 2500.f &&
            Dist2D(p.pos, e.pos) < mine - 500.f) { ahead = true; break; }
    if (!ahead) { waitAt = -1; return false; }
    if (waitAt < 0) { waitAt = gFrame; if (gDebug) Log("cheat: another doll ahead on its hunt for %s: it holds", LogWho(e).c_str()); }
    if (gFrame - waitAt >= 90) { waitAt = -1; offUntil = gFrame + 120; return false; }
    StopPath();
    Act("holding_corner", e.name); Decide("WAIT", "cheat_spacing");   // (MGO1 Host: "gun on the corner, waiting for ...")
    float hd[3]; HeadPoint(e, hd);
    CheatFace(hd, 2.f * kDeg);
    return true;
}
// the hunt: the nearest enemy along the mesh (the way's length to the 3 nearest straight; a way with a link, or none: its straight
// distance 1.5 times and 5 m more), chosen again every half second; the one ordered (target) only
void CheatHunt() {
    if (Unscope()) { Act("switching_weapon"); return; }
    const Player* named = gSet.order == "target" ? PlayerByName(gSet.target) : nullptr;
    if (named && !Enemy(*named)) named = nullptr;
    const Player* e = gCheat.huntId >= 0 ? PlayerById(gCheat.huntId) : nullptr;
    if (!e || !Enemy(*e) || gFrame - gCheat.huntAt >= 30 || (named && e != named)) {
        gCheat.huntAt = gFrame;
        std::vector<std::pair<float, const Player*>> c;
        // ((v141 beta 8) its focus: in a deathmatch the real players alone (none alive: its own hunt meanwhile); with teams a real
        // enemy 10 m nearer than he is)
        const bool focusDm = CheatFocusDm(), focus = CheatFocus();
        for (const Player& p : gPlayers)
            if (Enemy(p) && (!named || &p == named) && !(Asleep(p) && (IAmSnake() || !FinishAsleep())) && (named || !focusDm || RealFoe(p)) &&
                (named || CheatWaitsB8() || !NoWayTo(p.pos)))   // ((v141 beta 10) one out of its reach a while: another hunted)
                c.push_back({Dist2D(gMe.pos, p.pos) - (focus && RealFoe(p) ? 10000.f : 0.f), &p});
        std::sort(c.begin(), c.end(), [](const std::pair<float, const Player*>& a, const std::pair<float, const Player*>& b) { return a.first < b.first; });
        const Player* best = nullptr; float bl = 1e30f, bw = 0.f;
        for (size_t i = 0; i < c.size() && i < 3; ++i) {
            const Player& q = *c[i].second;
            float w = WayLen(q.pos);
            if (w < 0.f) w = Dist2D(gMe.pos, q.pos) * 1.5f + 5000.f;
            const float way = w;
            if (focus && RealFoe(q)) w -= 10000.f;
            if (w < bl) { bl = w; best = &q; bw = way; }
        }
        if (best && best->id != gCheat.huntId) {
            ++gCheat.hunts; StopPath();
            if (gDebug) Log("cheat: hunting %s (%.1f m along the mesh)", LogWho(*best).c_str(), bw / 1000.f);
        }
        gCheat.huntId = best ? best->id : -1;
        e = best;
    }
    if (!e) {
        if (gPk.id >= 0) CheatPeekEnd("none hunted");
        // ((v141 beta 8) its focus, no real player alive: where it is (its own hunt chased the other dolls, and the dolls met him
        // again together: the bench b8_focus))
        // (the activity "defending": MGO1 Host's "holding" names who asked)
        if (CheatFocusDm()) { StopPath(); Steer(0, 0, 0); Act("defending"); Decide("WAIT", "cheat_focus_none_alive"); return; }
        Hunt(); return;
    }
    if (CheatSpacing(*e)) return;                        // ((v141 beta 8) its focus: not two dolls into his sight together)
    if (CheatPeekFrame(*e)) return;                      // ((v141 beta 8) its peek at the edge of his sight)
    Act("hunting", e->name);
    if (Dist2D(gMe.pos, e->pos) < 1500.f && std::fabs(e->pos[1] - gMe.pos[1]) < 2500.f) { StopPath(); Steer(0, 0, 0); return; }
    GoTo(e->pos, "hunt", 1500.f);
}
void TacticNewLife() {
    gBp = BoxPop(); gBw = BoxWay(); gAa = AutoAim();                      // ((v141 beta 6, aim) the box pops, the auto aim)
    CheatPreEnd(); gCheat.topUpAt = gCheat.reloadAt = -1; gCheat.huntId = -1;   // ((v141 beta 7) tier 10's cheat)
    gPk.id = -1; gPk.side = 0; gPk.at = gPk.leanAt = gPk.viewAt = -1; gPk.offUntil = -100;   // ((v141 beta 8) its peek)
    gTc = Tactic(); gHits.clear(); gPre = PreAim(); gStyle = Rnd01(); gTcNextMove = -1; gTcFight = -1; gFoeLife.clear(); gDealtAt.clear();
    gBoxRunner = NmV3() && Rnd01() < 0.5f; gBoxRunSince = gBoxRunOffUntil = -1;
    gHandNow = gHandPrev = gItemNow = gItemPrev = -1; gTp = TapRun();   // ((v141 beta 3) a life's first box by its window)
    gGuardHoldAt = -1; gGuardRound = false; gGuardRoundAt = -1; gChasedOff = -1;   // (the one-life modes' own)
    gNoticedAt.clear(); gQw = QuietRun(); gHwUntil.clear(); gHwOff.clear(); gIc = InterRun(); gSpaceSince = gSpaceOff = -1; gFpvFor = -1;
    gSnk = SneakRun(); gSa = ShotAtRun(); gVt = VentRun(); gTpAt = -1;   // (v137) the box's sneak, the shots from an unseen one, a vent's chase
    gNm.phase = 0;                                                        // (NIGHTMARE) the box run
    FpvNewLife();                                                         // ((v141 beta 6) a first person view weapon's stay over)
}

void FindMyFire(uint32_t w, const std::string& name, void* ctx) {
    if (name.rfind("NewFireForChara", 0) != 0) return;
    const uint32_t me = gMe.obj & 0x01FFFFFF;
    for (uint32_t o = 0x20; o < 0x200; o += 4) if (me && (R32(w + o) & 0x01FFFFFF) == me) { *(bool*)ctx = true; return; }
    for (uint32_t o = 0x20; o + 12 <= 0x300; o += 4) {          // (or its point within a metre of the doll's hips)
        const float x = RF(w + o), y = RF(w + o + 4), z = RF(w + o + 8);
        if (x == x && std::fabs(x - gMe.pos[0]) < 700.f && std::fabs(z - gMe.pos[2]) < 700.f && std::fabs(y - gMe.pos[1]) < 1200.f &&
            std::fabs(x) + std::fabs(z) > 1.f) { *(bool*)ctx = true; return; }
    }
}
bool OnFire() { bool f = false; ps2world::ForEachActor(&FindMyFire, &f); return f; }
void BrainBody();
// (hanging from a ledge: doll.cpp's HangFrame has the pad) the brain's watches kept up: the round, the level, the lives,
// its sight and hearing
void BrainWatchFrame() {
    RoundWatch();
    LevelFrame();
    LivesWatch();
    Perceive();
    HearShots();
}
// (v141 beta 3) the still watch: a doll whose activity moves it (to the objective, roaming, chasing, to an item, to a
// sniper's spot, escorting, carrying) not 0.3 m from where it stood 5 s: what it was at in the log (its path, its windows,
// its tap, its fight); 10 s: its path given up, that way's goal left alone 30 s (the in-play bench, Pillbox Purgatory's
// capture: three dolls still 9 minutes by its ladder's foot, nothing said). (v141 beta 6) At 5 s, the stick pushed 60 % of
// the spell's frames (not a wait: an escort by its carrier) and no link under way (a ladder, a climb's presses: their own
// clocks), the ways out first (doll.cpp's UnstickFrame: back, side, climb, low, avoid; the pad theirs meanwhile), the path
// given up when they ran out
void StillWatch() {
    static float at[3] = {0, 0, 0};
    static long long since = -1;
    static bool said = false;
    static int pushed = 0, frames = 0;
    static const char* const kMoving[] = {"going_to_objective", "roaming", "chasing", "going_for_weapon", "going_for_food", "going_to_sniper_spot",
                                          "escorting", "carrying_objective", "box_slipping", "chasing_carrier", "hunting"};   // ((v141 beta 10) hunting)
    bool moving = false;
    for (const char* a : kMoving) if (ActCode() == a && (strcmp(a, "hunting") || !CheatWaitsB8())) moving = true;
    auto giveUp = [&](bool ways) {
        const float* g = PathGoal();
        const float* q = PathAsked();
        if (g) gNoWay.push_back({{g[0], g[1], g[2]}, gFrame});
        if (q && (!g || Dist2D(g, q) > 300.f || std::fabs(g[1] - q[1]) > 1000.f)) gNoWay.push_back({{q[0], q[1], q[2]}, gFrame});
        if (ways) Log("still: the ways out ran out after %.0f s, its path given up%s", (gFrame - since) / 60.0, g ? ", that goal left alone 30 s" : "");
        else Log("still: 10 s, its path given up%s", g ? ", that goal left alone 30 s" : "");
        StopPath();
        memcpy(at, gMe.pos, sizeof at); since = gFrame; said = false; pushed = frames = 0;
    };
    const int u = UnstickFrame(moving && gMe.alive);
    if (u == 1 || u == 2) return;
    if (u == -1) { UnstickGaveUp(); giveUp(true); return; }
    // ((v141 beta 6) a link under way (a ladder, stairs, a climb's presses, a crossing) has its own clock and failure rules: its
    // failures counted, a link failed twice left out; beta 3's watch gave up the path of a climb pressed 10 s, the climb never
    // counted as failed and taken again: Brown Town's 0.97 m ledge, three times in 30 s in an A/B bench of beta 6)
    if (!gMe.alive || !moving || Dist2D(at, gMe.pos) > 300.f || OnLinkNow()) { memcpy(at, gMe.pos, sizeof at); since = gFrame; said = false; pushed = frames = 0; return; }
    ++frames;
    if (std::abs((int)gPad.sticks[2] - 0x80) > 0x30 || std::abs((int)gPad.sticks[3] - 0x80) > 0x30) ++pushed;
    const long long t = gFrame - since;
    if (t >= 300 && !said) {
        said = true;
        Log("still: %s 5 s at %.0f %.0f %.0f (state %x): %s; windows %x %x, tap %d, fight %d, box run %d, take %d, the stick pushed %d %%", ActCode().c_str(), gMe.pos[0],
            gMe.pos[1], gMe.pos[2], gMe.bits[0], PathState().c_str(), gSw[0].want, gSw[1].want, gTp.win, gFi.target, gNm.phase, gTake.step, pushed * 100 / std::max(1, frames));
        if (UnstickOn() && pushed * 10 >= frames * 6 && !OnLinkNow()) { UnstickStart(); UnstickFrame(true); return; }
    }
    if (t >= 600) giveUp(false);
}
std::string AimFields();
// (v141 beta 6) a weapon's name for the AI's lines
const char* WeaponName(int c) {
    switch (c) {
    case 0: return "NONE"; case 1: return "KNIFE"; case 5: return "MK22"; case 6: return "M1911A1"; case 7: return "EZGUN"; case 8: return "SAA";
    case 9: return "FLAMETHROWER"; case 0xA: return "SCORPION"; case 0xB: return "XM16E1"; case 0xC: return "AK47"; case 0xD: return "M63";
    case 0xE: return "M37"; case 0xF: return "SVD"; case 0x10: return "MOSIN"; case 0x11: return "RPG7";
    default: return c >= 0x13 && c <= 0x17 ? "GRENADE" : c >= 0x1B && c <= 0x1D ? "TRAP" : "?";
    }
}
// (v141 beta 6) the frame's decision: the one said (Decide), else from its activity (a move of the fight, a tactic, a switch)
const char* DecisionNow(const char** reason) {
    if (*gDecision) { *reason = gReason; return gDecision; }
    const std::string& a = ActCode();
    *reason = a.c_str();
    if (a == "switching_weapon") return "SWITCH_WEAPON";
    if (a == "standing_up") return "STAND_UP";
    if (a == "cqc") return "CQC";
    if (a == "rolling") return "ROLL";
    if (a == "fighting" || a == "aiming_head" || a == "reloading") return "FIGHT_MOVE";
    if (a == "leaving_box" || a == "box_on" || a == "box_waiting" || a == "box_slipping" || a == "box_popping") return "BOX";
    if (a == "feinting" || a == "holding_corner" || a == "falling_back" || a == "watching" || a == "repositioning" || a == "taking_cover" ||
        a == "reloading_in_cover" || a == "standing_off") return "TACTIC";
    return gFi.target >= 0 ? "FIGHT_MOVE" : "OTHER";
}
// (v141 beta 6) MGO1_DOLL_AI_DEBUG=1, in a first person view weapon's stay: "AI DOLL_n weapon=SVD fpv_state=AIMING fpv_ms=830
// target=X distance=42.7 visible=1 shot_clear=1 movement_required=0 decision=FIRE reason=fpv" when they change (a second apart)
void FpvAiLine() {
    if (!AiDebug() || gFv.state == FPV_NONE) return;
    static std::string last;
    static long long lastAt = -100000;
    const bool out = gFv.state == FPV_EXITING || gFv.state == FPV_ABORTING;
    const char* reason = "";
    const char* dec = out ? "ABORT_FPV" : DecisionNow(&reason);
    if (out) reason = gFv.why;
    const Player* t = gFv.target >= 0 ? PlayerById(gFv.target) : nullptr;
    const auto kn = t ? gKnown.find(t->id) : gKnown.end();
    const bool vis = kn != gKnown.end() && kn->second.visible;
    char key[200];
    snprintf(key, sizeof key, "%d|%s|%s|%d", gFv.state, dec, reason, vis ? 1 : 0);
    if ((key == last && gFrame - lastAt < 60) || gFrame - lastAt < 15) return;
    last = key; lastAt = gFrame;
    int clear = -1;
    if (t && vis) { float eye[3], c[3]; SeeEye(eye); ChestPoint(*t, c); clear = LineClear(eye, c) ? 1 : 0; }
    const bool move = !strcmp(gFv.why, "movement_required") || !strcmp(gFv.why, "order");
    Log("AI DOLL_%d weapon=%s fpv_state=%s fpv_ms=%lld target=%s distance=%.1f visible=%d shot_clear=%d movement_required=%d decision=%s reason=%s",
        gSet.number, FpvName(gFv.weapon), kFpvNames[gFv.state], (gFrame - gFv.stayAt) * 1000 / 60, t ? t->name.c_str() : "NONE",
        t ? Dist2D(gMe.pos, t->pos) / 1000.f : -1.f, vis ? 1 : 0, clear, move ? 1 : 0, dec, reason);
}
// (v141 beta 6) after the brain's frame: a move asked with a first person view weapon held (a path followed, a moving activity):
// out first (the movement's authority over a stale stay, section 100)
void FpvAfterBrain() {
    if (!FpvOn()) return;
    gFvReasonLast = gReason;
    if (gFv.state >= FPV_AIMING && gFv.state <= FPV_RECOVERING && !gFv.test && FpvHeldCode() > 0) {
        static const char* const kMoving[] = {"going_to_objective", "escaping", "climbing", "chasing", "hiding", "carrying_objective", "escorting",
                                              "chasing_carrier", "box_slipping", "taking_cover", "repositioning", "putting_out_fire", "going_to_sniper_spot",
                                              "going_to_ally", "following_codec", "following", "dispersing", "covering", "roaming", "going_for_weapon",
                                              "going_for_food", "falling_back", "guarding_objective", "defending"};
        bool moving = Following();
        const std::string& a = ActCode();
        for (const char* m : kMoving) if (a == m) moving = true;
        if (moving) { gFv.act = a; FpvMoveExit(); }
    }
    FpvAiLine();
}
// MGO1_DOLL_AI_DEBUG=1: a line when the decision, its reason or the enemy change (15 frames apart at least, a second while it
// fights), and a minute's summary of the frames with an enemy in sight (the visible-but-idle time and why)
struct AiStats { long long since = -1, frames = 0, visible = 0, fire = 0, aim = 0, idle = 0; int pulls0 = -1; std::map<std::string, long long> why; };
AiStats gAiS;
void AiTraceFrame() {
    int vis = -1; float vd = 1e30f;
    for (const auto& kn : gKnown) {
        if (!kn.second.visible) continue;
        const Player* p = PlayerById(kn.first);
        if (!p || !p->alive) continue;
        const float d = Dist2D(gMe.pos, p->pos);
        if (d < vd) { vd = d; vis = kn.first; }
    }
    const char* reason = "";
    const char* dec = DecisionNow(&reason);
    // the minute's tally (an enemy in sight: firing, aiming, or idle with its reason)
    if (gAiS.since < 0) { gAiS = AiStats(); gAiS.since = gFrame; }
    ++gAiS.frames;
    if (vis >= 0) {
        ++gAiS.visible;
        // ((v141 beta 6, aim) the auto aim's shots fire, its spells and the turns by hand aiming, a box pop under fire a move)
        if (!strcmp(dec, "FIRE") || !strcmp(dec, "AUTOAIM_FIRE")) ++gAiS.fire;
        else if (!strcmp(dec, "AIM") || !strcmp(dec, "MANUAL_AIM") || !strcmp(dec, "AUTOAIM") || !strcmp(dec, "FIGHT_MOVE") || !strcmp(dec, "SWITCH_WEAPON") || !strcmp(dec, "MOVE") ||
                 !strcmp(dec, "CQC") || !strcmp(dec, "ROLL") || (!strcmp(dec, "BOX") && !strcmp(reason, "box_popping"))) ++gAiS.aim;
        else { ++gAiS.idle; ++gAiS.why[std::string(dec) + "/" + reason]; }   // ((v141 beta 6, aim) a WAIT counted once: it was twice)
    }
    auto cheatReset = [] { gCheat.minOn = gCheat.minFrames = 0; gCheat.preAims = gCheat.hunts = gCheat.ambushes = gCheat.focusSkips = 0; gCheat.pulls.clear(); gPk.peeks = gPk.sprung = 0; };
    if (!AiDebug()) { if (gFrame - gAiS.since >= 3600) { gAiS.since = -1; gAaSpells = gAaShots = gBoxOns = gBoxOuts = gSwitches = 0; cheatReset(); } return; }
    if (gFrame - gAiS.since >= 3600) {
        std::vector<std::pair<long long, std::string>> w;
        for (const auto& kv : gAiS.why) w.push_back({kv.second, kv.first});
        std::sort(w.rbegin(), w.rend());
        std::string top;
        for (size_t i = 0; i < w.size() && i < 6; ++i) { char b[96]; snprintf(b, sizeof b, "%s %s %.0f%%", i ? "," : "", w[i].second.c_str(), 100.0 * w[i].first / std::max(1LL, gAiS.visible)); top += b; }
        // ((v141 beta 7) tier 10's own play: its share of the minute, its guns readied on a coming one, the enemies hunted, its first pulls
        // (frames from the sight, median), its fights begun in its ambush's view)
        // ((v141 beta 8) its peeks (and the ones he showed in), its focus on the real players: the frames another doll in sight was left
        // alone for them)
        char cheat[300] = "", focus[64] = "";
        if (gCheat.minOn > 0) {
            std::vector<int> pv = gCheat.pulls;
            std::sort(pv.begin(), pv.end());
            if (gCheat.focus || gCheat.focusSkips > 0) snprintf(focus, sizeof focus, ", other dolls left alone %d frames", gCheat.focusSkips);
            snprintf(cheat, sizeof cheat, ", tier 10 cheat on %.0f%% (pre-aims %d, hunted %d, first pulls %zu, %d frames after the sight median, in the view %d, "
                     "peeks %d (%d in sight)%s)", 100.0 * gCheat.minOn / std::max(1LL, gCheat.minFrames), gCheat.preAims, gCheat.hunts, pv.size(),
                     pv.empty() ? -1 : pv[pv.size() / 2], gCheat.ambushes, gPk.peeks, gPk.sprung, focus);
        }
        // ((v141 beta 6, aim) the minute's auto aim (spells of L1, pulls with it), box pops and target switches, after the old fields)
        Log("AI DOLL_%d minute: an enemy in sight %.0f%% of the time; then firing %.0f%%, aiming or moving to fire %.0f%%, other %.0f%% (%s); level %.2f %s; "
            "autoaim %d spells %d shots, box on %d out %d, target switches %d%s",
            gSet.number, 100.0 * gAiS.visible / std::max(1LL, gAiS.frames), 100.0 * gAiS.fire / std::max(1LL, gAiS.visible), 100.0 * gAiS.aim / std::max(1LL, gAiS.visible),
            100.0 * gAiS.idle / std::max(1LL, gAiS.visible), top.empty() ? "none" : top.c_str(), LevelNow(), TierName(LevelNow()), gAaSpells, gAaShots, gBoxOns, gBoxOuts, gSwitches,
            cheat);
        gAiS.since = -1; gAaSpells = gAaShots = gBoxOns = gBoxOuts = gSwitches = 0; cheatReset();
    }
    static std::string last;
    static long long lastAt = -100000;
    const Player* ep = vis >= 0 ? PlayerById(vis) : gFi.target >= 0 ? PlayerById(gFi.target) : nullptr;
    char key[160];
    snprintf(key, sizeof key, "%s|%s|%d", dec, reason, ep ? ep->id : -1);
    if ((key == last && gFrame - lastAt < (gFi.target >= 0 ? 60 : 600)) || gFrame - lastAt < 15) return;
    last = key; lastAt = gFrame;
    const float d = ep ? Dist2D(gMe.pos, ep->pos) / 1000.f : -1.f;
    char b[600];
    snprintf(b, sizeof b, "AI DOLL_%d state=%s enemy=%s distance=%.1f visible=%d weapon=%s %s L1=%d decision=%s reason=%s%s reaction_ms=%d level=%.2f",
             gSet.number, gFi.target >= 0 ? "COMBAT" : ActCode().c_str(), ep ? ep->name.c_str() : "NONE", d, vis >= 0 ? 1 : 0, WeaponName(Hand()),
             AimFields().c_str(), (gPad.buttons & 0x0400) ? 1 : 0, dec, reason,
             gReasonLeft > 0 && *gDecision ? (" remaining=" + std::to_string(gReasonLeft * 1000 / 60) + "ms").c_str() : "",
             (int)std::lround(ReactMid(RF_PERCEPTION) * 1000.f / 60.f), LevelNow());
    Log("%s", b);
}
void FpvAfterBrain();
void BrainFrame() {
    BrainBody();
    FpvAfterBrain();                                      // ((v141 beta 6) a move asked with a first person view weapon held: out first)
    AiTraceFrame();
    QuietStick();                                         // (one-life modes: walked near the enemies)
    StillWatch();                                         // ((merge review) after the quiet walk's clamp: the ways out's full-stick pulses kept)
    // (Square with a grenade or a trap in the hands readies it, let go it throws or drops it: only in a throw's or
    // a trap's own phases; v134's audit, a sniper's sweep readied its stun grenade, which blinded it 17 s)
    if (ThrowableInHand() && (gPad.buttons & 0x8000) && gGr.phase == 0 && gCh.phase == 0 && gTr.phase == 0) {
        gPad.buttons &= ~0x8000u; gPad.pressures[11] = 0;
    }
    if (gFrame < gR2HoldUntil) gPad.buttons |= 0x0200;    // (a right lean's R2 not let go early: a tap takes another weapon)
    if (gPJ.kind != PJ_NONE || gSet.leave || gMyTeam == 0xFE) { RadioStop("a menu"); return; }
    bool windows = gSup.start >= 0 || gFrame < gR2HoldUntil || (gR2Since >= 0 && gFrame - gR2Since < 24);   // (the suppressors' window, R2 held: no radio)
    for (int w = 0; w < 2; ++w) if (gSw[w].want >= 0 && gSw[w].start >= 0) windows = true;
    // (an urgent call, "enemy sighted" before the first shot: from the first person view too, R1 let go a
    // second as a player does: the audit saw the calls of the levels that aim there never made)
    // ((merge review) not within 6 m of the path's next link: a call's presses centre the sticks, a press run's pulses lost; not
    // with a view-locking weapon in the hands: its stay's own presses)
    const bool calm = !windows && gCa.phase == 0 && gGr.phase == 0 && gCq.phase == 0 && gTr.phase == 0 && gEm.phase == 0 && gRo.phase == 0 &&
                      (!gFi.fpv || RadioUrgent()) && !Scoped() && !OnLadder() && NextLinkDist() > 6000.f && !LocksView(Hand());
    CodecFrame(calm, !(gPad.buttons & 0x8800));           // (quiet: not aiming nor firing this frame)
    // ((v141 beta 6) the radio's menu left on with no call: closed (doll_radio.cpp RadioCloseFrame); not in a window, a throw, a CQC
    // move, an emote, a roll, on a ladder or by a link; not aiming nor firing, unless a first person view weapon is held (its stay
    // needs the weapon window, which the radio's menu keeps shut)
    RadioCloseFrame(!windows && gCa.phase == 0 && gGr.phase == 0 && gCq.phase == 0 && gTr.phase == 0 && gEm.phase == 0 && gRo.phase == 0 &&
                    !OnLadder() && NextLinkDist() > 3000.f && (!(gPad.buttons & 0x8800) || LocksView(Hand())));
    // R1 let go this frame (a window, a CQC try, a step back, the radio's presses): the first person view's
    // spell is over; the next one begins afresh (its aim, its 12 frames before a lean, 20 before a shot)
    if (!Scoped() && !(gPad.buttons & 0x0800) && gFi.fpvSince >= 0) { gFi.fpvSince = -1; gFi.viewOnAt = -1; gLean.side = 0; gLean.at = -100000; }
    // R2 let go within 24 frames of its press is a tap (the previous weapon): once out, held 24 frames
    // (a lean's R2 whose view ended at once; the radio's presses let none out meanwhile)
    if (gFrame < gTapNoHoldUntil) gR2Since = -1;          // (a tap wanted: let go as asked)
    else if (gPad.buttons & 0x0200) { if (gR2Since < 0) gR2Since = gFrame; }
    else if (gR2Since >= 0) { if (gFrame - gR2Since < 24 && !RadioPressing()) gPad.buttons |= 0x0200; else gR2Since = -1; }
}
// (v141 beta 3) MGO1_TAP_TEST=1: the taps of L2 (the item) and R2 (the weapon) measured, the brain's frame taken, alone in a
// game: the box on by the window, L2 tapped with it on, then off, the weapon window to the handgun, R2 tapped, Square after
// it, Square in the box, L2 tapped running in the box, R2 tapped holding a grenade; the tap 4, 2 then 8 frames by round
struct TapTest { int phase = 0, round = 0, w0 = -1, step = 0; long long at = -1, tapAt = -1; bool done = false; };
TapTest gTT;
bool BoxActor() { bool on = false; ps2world::ForEachActor(&FindMyBox, &on); return on; }
bool TapTestFrame() {
    static const bool on = getenv("MGO1_TAP_TEST") != nullptr;
    if (!on || gTT.done) return false;
    TapTest& T = gTT;
    const long long t = gFrame - T.at;
    const int len = T.round % 3 == 0 ? 4 : T.round % 3 == 1 ? 2 : 8;
    auto next = [&](int ph) { T.phase = ph; T.at = gFrame; T.tapAt = -1; T.step = 0; };
    auto tap = [&](uint16_t b) {                        // (pressed len frames from tapAt; true while pressed)
        if (T.tapAt < 0) T.tapAt = gFrame;
        if (gFrame - T.tapAt < len) { gPad.buttons |= b; gTapNoHoldUntil = gFrame + 30; return true; }
        return false;
    };
    const int weapon = Inv().ok ? Inv().weapon : -1;
    Steer(0, 0, 0); StopPath(); Act("testing");
    if (!gMe.alive || LifeFrames() < 0) { T.phase = 0; return true; }
    switch (T.phase) {
    case 0:
        if (LifeFrames() >= 240 && Inv().ok) next(1);
        return true;
    case 1: {                                           // the main gun in the hands
        const int g = MainGun();
        if (g > 0 && weapon != g) { SwapTo(g); return true; }
        if (t > 30) { Log("taptest: round %d (taps of %d frames), holding %x", T.round + 1, len, weapon); next(2); }
        return true;
    }
    case 2:                                             // the box on by the window
        if (!BoxActor()) { if (!ItemTo(0xE) && t > 400) { Log("taptest: the box not put on"); next(6); } return true; }
        Log("taptest: the box on by the window in %lld frames", t); next(3);
        return true;
    case 3:                                             // L2 tapped, the box on
        if (t < 60) return true;
        if (tap(0x0100)) return true;
        if (!BoxActor()) { Log("taptest: L2 tap (%d frames), the box on: off %lld frames after the press (state %x %x)", len, gFrame - T.tapAt, gMe.bits[0], gMe.bits[1]); next(4); }
        else if (gFrame - T.tapAt > 120) { Log("taptest: L2 tap (%d frames), the box on: still on after 120 frames", len); next(4); }
        return true;
    case 4:                                             // L2 tapped, no item: the box again?
        if (t < 60) return true;
        if (tap(0x0100)) return true;
        if (BoxActor()) { Log("taptest: L2 tap (%d frames), no item: the box on again %lld frames after the press", len, gFrame - T.tapAt); next(5); }
        else if (gFrame - T.tapAt > 120) { Log("taptest: L2 tap (%d frames), no item: no box after 120 frames (item %x)", len, Inv().item); next(5); }
        return true;
    case 5:                                             // off again (a tap) before the weapons
        if (!BoxActor()) { next(6); return true; }
        if (t < 60) return true;
        if (tap(0x0100)) return true;
        if (!BoxActor() || gFrame - T.tapAt > 120) next(6);
        return true;
    case 6: {                                           // the handgun by the window, then R2 tapped
        const int h = Handgun();
        if (T.step == 0) {
            if (h <= 0 || h == MainGun()) { Log("taptest: no handgun"); next(8); return true; }
            T.w0 = weapon;
            if (weapon != h) { SwapTo(h); if (t > 400) { Log("taptest: the handgun not taken"); next(8); } return true; }
            T.step = 1; T.at = gFrame; return true;
        }
        if (t < 60) return true;
        if (T.step == 1) { T.w0 = weapon; T.step = 2; }
        if (tap(0x0200)) return true;
        if (weapon != T.w0) { Log("taptest: R2 tap (%d frames) holding %x: %x %lld frames after the press", len, T.w0, weapon, gFrame - T.tapAt); next(7); }
        else if (gFrame - T.tapAt > 120) { Log("taptest: R2 tap (%d frames) holding %x: no change after 120 frames", len, T.w0); next(7); }
        return true;
    }
    case 7:                                             // Square: readied
        if (t < 30) return true;
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
        if (gMe.bits[1] & 0x4000) { Log("taptest: Square holding %x: readied in %lld frames", weapon, t - 30); next(8); }
        else if (t > 120) { Log("taptest: Square holding %x: not readied in 90 frames (state %x %x)", weapon, gMe.bits[0], gMe.bits[1]); next(8); }
        return true;
    case 8:                                             // the box on, Square held 60 frames in it
        if (T.step == 0) {
            if (!BoxActor()) { if (!ItemTo(0xE) && t > 400) { next(9); } return true; }
            T.step = 1; T.at = gFrame; return true;
        }
        if (t < 45) return true;
        if (t < 105) {
            gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
            if (!BoxActor()) { Log("taptest: Square in the box: off %lld frames after the press (state %x %x)", t - 45, gMe.bits[0], gMe.bits[1]); next(9); }
            return true;
        }
        Log("taptest: Square in the box: still on after 60 frames (state %x %x)", gMe.bits[0], gMe.bits[1]);
        next(9);
        return true;
    case 9:                                             // in the box, running 45 frames, then L2 tapped running
        if (T.step == 0) {
            if (!BoxActor()) { if (!ItemTo(0xE) && t > 400) { next(10); } return true; }
            T.step = 1; T.at = gFrame; return true;
        }
        if (t < 30) return true;
        Steer(std::sin(Facing()), std::cos(Facing()), 1.f);
        if (t < 75) return true;
        if (tap(0x0100)) return true;
        if (!BoxActor()) {
            Log("taptest: L2 tap (%d frames) running in the box: off %lld frames after the press (state %x %x)", len, gFrame - T.tapAt, gMe.bits[0], gMe.bits[1]);
            next(10);
        } else if (gFrame - T.tapAt > 120) { Log("taptest: L2 tap (%d frames) running in the box: still on after 120 frames", len); next(10); }
        return true;
    case 10: {                                          // a grenade by the window, then R2 tapped
        int gr = -1;
        for (int c = 0x13; c <= 0x17; ++c) if (HasWeapon(c) && Rounds(c) != 0) { gr = c; break; }
        if (T.step == 0) {
            if (gr < 0) { Log("taptest: no grenade"); next(11); return true; }
            if (weapon != gr) { SwapTo(gr); if (t > 400) { Log("taptest: the grenade not taken"); next(11); } return true; }
            T.step = 1; T.at = gFrame; return true;
        }
        if (t < 60) return true;
        if (T.step == 1) { T.w0 = weapon; T.step = 2; }
        if (tap(0x0200)) return true;
        if (weapon != T.w0) { Log("taptest: R2 tap (%d frames) holding the grenade %x: %x %lld frames after the press", len, T.w0, weapon, gFrame - T.tapAt); next(11); }
        else if (gFrame - T.tapAt > 120) { Log("taptest: R2 tap (%d frames) holding the grenade %x: no change after 120 frames", len, T.w0); next(11); }
        return true;
    }
    default:
        if (++T.round >= 6) { T.done = true; Log("taptest: over"); return false; }
        next(1);
        return true;
    }
}
// knocked down awake (an explosion, a fall: the state word 0's 0x8000000 with most of the stamina left; a
// roll into it: 0x10000000; v133's audit: dolls lay there up to 3 minutes until killed, while an
// unconscious one, its stamina spent, wakes in 30 to 50 s): from 0.75 s down, the inputs a player tries in
// turn, 2/3 s apart
static long long gDownSince = -1;
static int gDownInput = -1, gDownDeaths = 0;
static const char* const kGetUpInputs[4] = {"Cross", "the stick", "Triangle", "Circle"};
static void GetUpFrame() {
    // (0x8000000: an explosion, a fall; 0x10000000 with most of the stamina: a roll into it, v134)
    static int off = -1;
    if (off < 0) { const char* e = getenv("MGO1_DOLL_GETUP"); off = e && *e == '0' ? 1 : 0; }   // ((v141 beta 9) tests: no input)
    // ((v141 beta 9) its stamina when it fell: under 12000, knocked out (a choke, darts, punches): nothing pressed, the game stands it
    // up once its stamina is back (the choke bench: up 20 s after the knock-out with the inputs, 22 s without; players saw its standing
    // up begun again and again), Cross only if it still lies 2 s after; awake (a roll, an explosion): Cross alone (every get-up of the
    // benches came after Cross), the inputs in turn after 6 s)
    static long long lyingAt = -100, fullSince = -1, pressFrom = -1; static int lyingStun = 0;
    if (!(gMe.bits[0] & 0x18000000)) { lyingAt = -100; return; }
    if (gFrame - lyingAt > 1) { lyingStun = gMe.stun; fullSince = -1; pressFrom = -1; }
    lyingAt = gFrame;
    if (gMe.stun < 29900) fullSince = -1; else if (fullSince < 0) fullSince = gFrame;
    const bool out = lyingStun < 12000 && !(fullSince >= 0 && gFrame - fullSince > 120);
    if (gMe.stun <= 12000 || off) return;
    if (gDownSince < 0) {
        gDownSince = gFrame; gDownInput = -1; gDownDeaths = gDeaths;
        if (gDebug) Log("knocked down awake (stamina %d, %d when it fell, state %x): %s", gMe.stun, lyingStun, gMe.bits[0],
                        out ? "knocked out: it gets up by itself" : "getting up");
    }
    if (out) return;
    if (pressFrom < 0) {
        pressFrom = gFrame;
        if (lyingStun < 12000 && gDebug) Log("knocked out: still down 2 s after its stamina came back: getting up");
    }
    const long long t = gFrame - pressFrom;
    if (t < 45) return;
    if (t < 405) { gDownInput = 0; if ((t - 45) % 40 < 4) gPad.buttons |= 0x4000; return; }
    const int k = (int)(((t - 405) / 40) % 4), ph = (int)((t - 405) % 40);
    gDownInput = k;
    if (k == 0 && ph < 4) gPad.buttons |= 0x4000;
    if (k == 1 && ph < 24) Steer(std::sin(Facing()), std::cos(Facing()), 1.f);
    if (k == 2 && ph < 4) gPad.buttons |= 0x1000;
    if (k == 3 && ph < 4) gPad.buttons |= 0x2000;
}
void BrainBody() {
    gControl = true;
    gQuiet = false;
    Decide("", "");
    RespawnBrainWatch();
    RoundWatch();
    ObjectiveWatch();
    LevelFrame();
    LivesWatch();                                         // (the players' deaths and spawns: every frame)
    gOutSince = -1;
    gWeaponSince = -1;
    if (gSet.leave) { Act("leaving"); return; }
    if (gDownSince >= 0 && (!(gMe.bits[0] & 0x18000000) || gDeaths != gDownDeaths)) {
        if (gDebug) Log("knocked down: %s after %.1f s (the last input: %s)", gDeaths == gDownDeaths ? "up" : "killed", (gFrame - gDownSince) / 60.0,
                        gDownInput >= 0 ? kGetUpInputs[gDownInput] : "none");
        gDownSince = -1;
    }
    // asleep or knocked out (a dart, a CQC throw, punches): nothing to do until it gets up (the audit: its
    // paths "stuck" while it lay there), nothing seen nor heard (up again, its reaction time again)
    if (Stunned()) {
        SeeLives(); gLastLife = gMe.life; gLastStun = gMe.stun;
        for (auto& k : gKnown) k.second.visible = false;
        StopPath(); Steer(0, 0, 0); if (gFi.target >= 0) FightStop(); Act("knocked_out");
        GetUpFrame();
        return;
    }
    Perceive();
    HearShots();
    if (CheatOn()) CheatMagWatch();                       // ((v141 beta 7) tier 10's cheat: each gun's magazine size)
    InventoryLog();
    HeldWatch();                                          // ((v141 beta 3) what is held and what was: the taps)
    FoodWatch();                                          // ((v141 beta 3) what a food box gave)
    TrackRounds();
    DmUnitLog();
    KillWatch();                                          // (its kills: its victims' kill cameras)
    EmoteWatch();                                         // (the players' emotes, its kills: an emote wanted)
    NoticeWatch();                                        // (one-life modes: the enemies who have noticed it)
    if (Nightmare()) NmWatch();                           // (NIGHTMARE: its life before a streak of hits)
    FpvWatch();                                           // ((v141 beta 6) a first person view weapon's stay: its states, aborts, watchdog)
    if (LadderFrame()) return;                           // on a ladder: nothing else until off it
    if (TapFrame()) return;                               // ((v141 beta 3) a tap's 4 frames: nothing else meanwhile)
    if (WindowBusy()) { Steer(0, 0, 0); return; }        // a window open: finished first (a short while)
    // ((v141 beta 6) out of such a stay first; MGO1_DOLL_FPV_TEST=stuck. (merge review) not during a pause menu's job: its presses)
    if (gPJ.kind == PJ_NONE && (FpvExitFrame() || FpvTestFrame())) return;
    if (TapTestFrame()) return;                           // (MGO1_TAP_TEST: the taps measured)
    if (gGr.phase == 3 || gGr.phase == 4) { GrenadeFrame(); return; }   // a throw begun is finished (Square pressed)
    // blinded by a stun grenade's flash: nothing seen (Sees), no fight; still, crouched (up again after), where
    // it stands. ((v141 beta 7) tier 10's cheat: a flash nothing to it)
    if (Blind() && !CheatOn()) {
        if (gFrame - gBlindLogAt > 300) { gBlindLogAt = gFrame; Log("blinded by a flash (its white %.0f)", WhiteOut()); }
        if (gFi.target >= 0) { FightStop(); gFightEndAt = gFrame; }
        if (gRo.phase && !Rolling()) gRo.phase = 0;
        EmoteDrop();
        StopPath(); Steer(0, 0, 0); Act("blinded");
        if (!Carrying() && ((gMe.bits[0] & 0xFFFF) == 3 || Low())) { SetLow(true); gStandUpSince = 0; }   // (still first: Cross running rolls)
        return;
    }
    // on fire (a NewFireForChara* actor, the fire drawn on a character, pointing at the doll's work or
    // within a metre of it): the cardboard box puts it out (not seen on the bench: the doll's own WP fire
    // does not burn it)
    if (gFrame % 10 == 0 && OnFire()) gBurningUntil = gFrame + 60;
    if (gFrame < gBurningUntil && !BoxOn()) { if (ItemTo(0xE)) { Act("putting_out_fire"); return; } }
    // the pause menu's jobs: out of the game (spectator), to the team ordered
    if (PauseJobFrame()) { Act("menu", PauseJobName(gPJ.kind)); return; }
    if (gMyTeam == 0xFE) { gSpecTries = 0; MenuFrame(); return; }   // a spectator's frame: not in the game
    if (WantSpectate()) {
        if (SpectatorBarred()) {
            if (!gSpecBarredSaid) {
                gSpecBarredSaid = true;
                Log("spectator: %s: it waits in its side", IAmSnake() ? "sneaking's Snake cannot leave his side" : "the game kept it in its side 3 times this round");
            }
            StopPath(); Steer(0, 0, 0);
            Act(RealPlayers() == 0 ? "waiting_for_players" : "spectating", "in its side");
            return;
        }
        if (PauseJobStart(PJ_SPECTATOR)) ++gSpecTries;
        Act(RealPlayers() == 0 ? "waiting_for_players" : "spectating"); return;
    }
    // deathmatch: its unit at its first spawn, no enemy within 15 m, not hurt a moment ago
    if (DmUnitWanted() && gFi.target < 0 && !EnemyNear(15000.f) && gFrame - gHurtAt > 300 && PauseJobStart(PJ_SPECTATOR)) {
        ++gDmTries;
        Log("deathmatch: wearing %s, to %s", UnitName(ClassUnit(MyType())), UnitName(DmUnit()));
        Act("menu", "unit"); return;
    }
    if ((gSet.team == "red" || gSet.team == "blue") && (gMyTeam == 0 || gMyTeam == 1) && gRule != 0 && gRule != 4 &&
        gMyTeam != (gSet.team == "red" ? 0 : 1) && !JoinHold())
        PauseJobStart(PJ_TEAM_CHANGE);
    // carrying the objective (the hands full: no weapon): straight to the goal
    if (Carrying()) { if (gFi.target >= 0) FightStop(); EmoteDrop(); if (StealthFrame(false)) return; Objective(); return; }
    if (gRo.phase && gRo.target >= 0) {                   // a roll begun: finished (its target out of sight a moment too)
        const Player* rp = PlayerById(gRo.target);
        if (rp && RollFrame(*rp, gRo.why)) return;
        if (!rp) gRo.phase = 0;
    }
    // ((v137) a grenade at one not in sight, before Square: an enemy in sight drops it, the fight first, as the others')
    if (gGr.phase && gGr.phase < 3 && gGr.inFight && gGr.unseen)
        for (const auto& kv : gKnown) if (kv.second.visible) { gGr.phase = 0; Log("grenade: not thrown, %s in sight: the fight first", FoeName(kv.first)); break; }
    if (gGr.phase && gGr.phase < 5 && gGr.inFight) { GrenadeFrame(); return; }   // a smoke of a fight: thrown
    // its stun grenade about to go off (175 frames after Square let go, 100 to 170 after the throw's end: the
    // audit): turned away from 40, even in a fight (probe 31: looked at through the first person view, its
    // white lasts 24 s)
    if (gGr.phase == 5 && gFrame - gGr.at >= 40 && GrenadeFrame()) return;
    if (EmoteFrame()) return;                             // an emote (after a kill, an answer, a greeting)
    GuardHoldEnd();                                       // (one-life defenders: a guard's hold over when the item is taken)
    // a fight first (but hiding: only when found)
    LeaderWatchFrame();                                   // (following on the radio: the enemy its leader aims at)
    const int t = PickTarget();
    const Player* tp = t >= 0 ? PlayerById(t) : nullptr;
    const bool hiding = gSet.order == "hide";
    if (!tp && *gPickWhy) Decide("NO_FIGHT", gPickWhy);
    // Snake wins by the objective, not by fighting: he fights only when cornered (an enemy within 5 m, hurt
    // a moment ago, or seen aiming at him), else he goes on (hidden by his camouflage)
    bool cornered = true;
    if (tp && IAmSnake()) {
        const float d = Dist2D(gMe.pos, tp->pos);
        const bool aiming = std::fabs(Wrap(AngleTo(tp->pos, gMe.pos) - tp->facing)) < 15.f * kDeg && (tp->bits[1] & 0x4400) != 0;
        cornered = d < 5000.f || gFrame - gHurtAt < 180 || aiming;
        if (!cornered) Decide("NO_FIGHT", "snake_objective_first");
    }
    // Snake hurt and low (under 40 % of his life, hurt in the last 10 s): away from the fight to a hidden
    // place (the stealth camouflage on the way), the objective again when nothing has hurt him for 10 s;
    // an enemy within 3 m: fought (CQC first)
    if (IAmSnake() && gMe.life < gMyMaxLife * 0.4f && gFrame - gHurtAt < 300 && !(tp && Dist2D(gMe.pos, tp->pos) < 3000.f) && !Carrying()) {
        if (gFi.target >= 0) { FightStop(); gFightEndAt = gFrame; }
        if (!gFleeing) { gFleeing = true; gHideHave = false; Log("snake: hurt (life %d of %d): away to hide", gMe.life, gMyMaxLife); }
        if ((!gHideHave || Dist2D(gMe.pos, gHideSpot) > 1500.f) && StealthFrame(false)) return;
        Hide("fleeing"); return;
    }
    gFleeing = false;
    if (tp && ObjFocus() && ObjTaker() && !IAmSnake() && Dist2D(gMe.pos, tp->pos) > 15000.f && gFrame - gHurtAt > 180) { cornered = false; Decide("NO_FIGHT", "objective_first"); }   // (the objective first)
    // its guns empty (v134): an enemy neither a roll (a straight way 2.5 to 10 m, no roll a moment ago) nor its
    // punches (3 m) reach is not fought: a weapon box instead
    if (tp && NoAmmo() && !CarrierOf(*tp) && gRo.phase == 0) {
        const float d = Dist2D(gMe.pos, tp->pos);
        if (d > 3000.f && (gFrame < gRo.offUntil || !RollGeom(*tp, d))) { cornered = false; Decide("NO_FIGHT", "no_ammo_weapon_box_first"); }
    }
    // its smoke out (v134): away from the enemy it hides from, 6 s, unless he comes within 5 m (where it knows
    // him: seen, or last known behind the smoke); another enemy in sight is fought
    if (gFrame < gRetreatUntil && gRetreatFrom >= 0 && (!tp || tp->id == gRetreatFrom)) {
        const auto kn = gKnown.find(gRetreatFrom);
        const float d = tp ? Dist2D(gMe.pos, tp->pos) : kn != gKnown.end() ? Dist2D(gMe.pos, kn->second.pos) : 1e9f;
        if (d > 5000.f) {
            if (gFi.target >= 0) { FightStop(); gFightEndAt = gFrame; }
            Decide("NO_FIGHT", "smoke_retreat");
            if (!Items()) Hide("fleeing");
            return;
        }
    }
    // (one-life attackers) an enemy who has not noticed it, beyond 5 m and not in its way with a quiet shot: not fought;
    // crouched still a moment when he comes, else round him
    if (tp && cornered && OneLifeAttacker() && HoldFireAt(*tp)) {
        if (gFi.target >= 0) { FightStop(); gFightEndAt = gFrame; }
        Decide("NO_FIGHT", "one_life_unnoticed_hold_fire");
        if (WaitHidden(*tp)) return;
        cornered = false;
    }
    if (tp && cornered && NmBoxKeeps(*tp)) Decide("NO_FIGHT", "box_keeps_hidden");
    // ((NIGHTMARE) in its box to get away: one in sight beyond 6 m, not aiming at it, not fought: NmBoxKeeps)
    if (tp && cornered && !NmBoxKeeps(*tp) && (!hiding || Dist2D(gMe.pos, tp->pos) < 6000.f || gFrame - gHurtAt < 120)) {
        if (gGr.phase > 0 && gGr.phase < 3) gGr.phase = 0;               // (a grenade not thrown yet: the fight first)
        if (gTr.phase > 0) { gTr.phase = 0; gTr.lastPut = gFrame - 1200; }  // (a trap not put yet: later)
        if (gCa.phase) { gCa.phase = 0; gCw = CamoWin(); }                // (the uniform later)
        if (BoxPopFight(*tp)) return;                     // ((v141 beta 6, aim) a box pop under his fire: in its box a moment, out, its fire)
        // ((v141 beta 3) NIGHTMARE: its box off before anything, by a tap of L2 when it can. (v141 beta 6, aim) The ambush in its box
        // keeps its own moment (HoldSprung: him passed by, found it, hurt it, time up); each pop out said, its reaction short: BoxPopOut)
        const bool ambush = BoxPopOn() && gTc.kind == TC_HOLD && gTc.box;
        if (NmV3() && InMyBox() && !ambush && (TapTo(1, 0) || ItemTo(0))) { BoxPopOut(*tp); Act("leaving_box", tp->name); return; }
        if (TacticFight(*tp)) return;                     // (v136) the fight's moves: cover to reload, another place, back, the feint, a corner held
        if (InMyBox() && (NmV3() ? TapTo(1, 0) || ItemTo(0) : ItemTo(0))) { BoxPopOut(*tp); Act("leaving_box", tp->name); return; }   // the hands free first
        if (CoverFirst(*tp)) return;                      // (its leader shooting: to cover by him first)
        // (hurt: a smoke between them first; not a one-life defender while an enemy carries the objective)
        // ((v141 beta 7) tier 10's cheat: no smoke to get away)
        if (!CheatOn() && !(OneLifeDefender() && EnemyCarrier()) && SmokeWanted(*tp)) { GrenadeFrame(); return; }
        DoFight(*tp); return;
    }
    const int lostFoe = gFi.target;
    if (gFi.target >= 0) { FightStop(); gFightEndAt = gFrame; }
    if (NmBoxFrame(lostFoe)) return;                      // (NIGHTMARE) the box to get away: after its kill, out of a quick fight
    if (TacticLull(lostFoe)) return;                      // (v136) a move going on out of sight, or the corner he left by watched
    if (ChasedHold()) return;                             // (one-life attackers noticed: not toward their allies, the corner held)
    if (ShotAtFrame()) return;                            // (v137) hurt by an enemy not seen: the gun on his spot, then out of his line
    if (VentFrame()) return;                              // (v137) an enemy gone into a vent / under a truck: after him, a grenade, or its opening held
    if (HeardFrame()) return;                             // (v137) levels 4-5: steps heard close, him not seen: the gun on them a moment
    // crouched by the fight: standing again first (moving crouched is crawling), 2 s at most
    if (gStandUpSince >= 0 && gSet.order != "hide") {
        if (gStandUpSince == 0) gStandUpSince = gFrame;
        if (!Low() || gFrame - gStandUpSince > 120) gStandUpSince = -1;
        else { SetLow(false); StopPath(); Steer(0, 0, 0); Act("standing_up"); return; }
    }
    if (CheatOn() && CheatCalm()) return;                 // ((v141 beta 7) tier 10's cheat: its gun on where a coming one will show, its magazine)
    // a weapon other than the primary in the hands (the handgun of a fight close by, a grenade): the
    // primary again, a few seconds after the fight
    if (gGr.phase == 5 && GrenadeFrame()) return;         // its stun grenade out: turned away from it
    if (SuppressorFrame()) return;                        // the suppressors on at each spawn (M1911A1, Mk22, XM16E1)
    if (ChaffFrame()) return;                             // a chaff grenade: Snake's at the start, a soldier's with enemies close
    if (CamoFrame()) return;                              // Snake: the uniform that blends in best on this map
    if (StealthFrame(hiding)) return;                     // the stealth camouflage near the enemies (its battery)
    if (GogglesFrame(hiding)) return;                     // the thermal goggles on (a weapon box gave them)
    // ((v141 beta 7) tier 10's cheat: no freeze, no grenade in a calm moment, no trap)
    const bool cheat = CheatOn();
    if (!cheat && FreezeFrame()) return;                  // Snake seen coming: crouched still a moment
    if (!hiding && !IAmSnake() && !cheat && GrenadeFrame()) return;   // a grenade at an enemy behind a cover, in a moment of calm
    if (!hiding && !IAmSnake() && !cheat && gCo.kind == CK_NONE && TrapFrame()) return;   // a claymore / a book at a narrow place on the enemies' way (Snake: his objective)
    // the weapon out of a fight (RestWeapon): a sniper's handgun but at its spot (its rifle there), Snake's
    // Mk22, else the fight's gun; a few seconds after a fight
    const bool atSpot = SniperAtSpot();
    const int rest = RestWeapon();
    if (gFrame - gFightEndAt > 180 && rest >= 0 && Hand() != rest && !atSpot && (Inv().ok || Hand() >= 0 || rest > 0)) {
        if (SwapTo(rest)) { Act("switching_weapon"); return; }
    }
    if (EscapeFrame()) return;                            // a pocket of the mesh: out of it first
    const char* quietWhy = QuietWhy();                    // (one-life modes: its moves from here walked near the enemies)
    gQuiet = quietWhy != nullptr;
    QuietSay(quietWhy);
    if (hiding) { Hide(); return; }
    if (BoxSneakFrame()) return;                          // (v137) one-life attackers: the box on for the quiet walk near the guards
    // ((v141 beta 3) NIGHTMARE's box runner: in its box while it moves with no enemy near (the run's own speed in it: probe 31),
    // off at the contact (the fight's "the hands free first")
    if (BoxApproachFrame()) return;                       // ((v141 beta 6, aim) a box pop's approach: in its box toward an enemy known
    const bool boxRoam = NmBoxRoam();
    if (boxRoam && !InMyBox() && WindowFree(1) && gFrame - gBoxRoamAt > 300) {
        if (TapTo(1, 0xE) || ItemTo(0xE)) { if (gBoxRoamAt < gFrame - 300) { gBoxRoamAt = gFrame; if (gDebug) Log("tactic: the box on to move"); } Act("box_on"); return; }
    }
    if (BoxOn() && gFrame >= gBurningUntil && !gSnk.on && !NmBoxLive() && !boxRoam && !BoxApproachLive() && ((NmV3() && TapTo(1, 0)) || ItemTo(0))) { Act("leaving_box"); return; }   // (hidden no more, the fire out, a link ahead: the box off)
    if (CodecOrderFrame()) return;                        // what an ally asked on the radio
    if (gSet.order == "follow" || gSet.order == "target") {     // (a teammate ordered as a target: followed, never attacked)
        const Player* f = PlayerByName(gSet.target);
        if (f && f->alive && !Enemy(*f) && (gSet.order == "follow" || Ally(*f))) { Act("following", f->name); Follow(*f); return; }
    }
    if (gSet.order == "defend") { Defend(); return; }
    SpotsFrame();
    if (NoAmmo() && Items()) return;                      // (its guns empty: a weapon box before the objective, v134)
    if (gSet.order != "target" && Objective()) return;
    if (Items()) return;
    if ((gSet.order == "free" || gSet.order == "objective") && Snipe()) return;
    if (cheat) { CheatHunt(); return; }                   // ((v141 beta 7) tier 10's cheat: the nearest enemy along the mesh)
    Hunt();
}

void MenuFrame() {
    gControl = true;
    RespawnMenuWatch();
    CodecMenuFrame();
    RoundWatch();
    LevelFrame();
    LivesWatch();                                         // (the players' deaths and spawns: every frame)
    gItemHeld = 0; gHand = -1; gBoxTakenAt = -1000;       // (a spawn: the weapon held not known, no item)
    gTr.put = 0; gTr.phase = 0; gGr.phase = 0; gCh = ChaffRun();
    gSw[0] = Swap(); gSw[1] = Swap();
    gMissing.clear(); gCq = CqcTry(); gCa = CamoRun(); gCw = CamoWin(); gSup = SupRun();
    gRo.phase = 0; EmoteDrop(); gTruceWith = -1; gRetreatUntil = -1; gRetreatFrom = -1; gGogglesWere = false;
    gBoxTouched.clear(); gBoxGoingHave = false; gRoundsMax.clear();          // (a new life: its boxes, its rounds)
    gLastLife = 0; gLastStun = -1;                                            // (its first life's frame: no hurt)
    TacticNewLife();                                                          // (its moves' taste drawn again)
    if (gOutSince < 0) gOutSince = gFrame;
    FightStop();
    // (the join's weapons page: its own activity, not the menu's too each frame: the audit's log flipped)
    if (PauseJobFrame()) { if (!WeaponSelectShown()) Act("menu", PauseJobName(gPJ.kind)); return; }
    if (WeaponSelectShown()) { WeaponSelectFrame(); return; }
    gWeaponSince = -1;
    // no real player, or ordered: not in the game (spectating); else Circle every 1.5 s (the spawn, a result)
    gSpectating = WantSpectate();
    // (v141 beta 13) dead in a team: it spawns, then goes spectator by the pause menu (BrainFrame); a side kept with nobody
    // real would make a real player's arrival start the round again (JoinHold)
    const bool outOfTeam = !(gMyTeam == 0 || gMyTeam == 1) || !gMe.obj || gMe.alive;
    if (gSpectating && outOfTeam) {
        Act(RealPlayers() == 0 ? "waiting_for_players" : "spectating");
        return;
    }
    if (gMyTeam == 0xFE) {                                // a spectator: into a team again (not while a real player arrives)
        if (const char* why = JoinHold()) { Act("waiting_to_join", why); return; }
        PauseJobStart(PJ_JOIN, gRule == 0 ? 60 : 600); Act("joining_team"); return;
    }
    if (gMyTeam == 0xFF) {                                // no team yet: its first deploy takes one (the auto balance's)
        if (const char* why = JoinHold()) { Act("waiting_to_join", why); return; }
    }
    Act(gMe.obj && !gMe.alive ? "dead" : "spawning");
    // (v137) alive and frozen in a round being played, the weapons page gone (the respawn's wait): the game frees the
    // player 5 to 10 frames after a Circle and plays that Circle as a CQC strike too (the word 1's 0x100 as after a Circle
    // in a fight, the hips sliding 0.3 to 0.5 m, the stick dead 1.5 to 2 s): 97 of 98 such deploys of the v137 bench (DM,
    // TDM), Circle held across the free frame or let go before. Tried first: the stick (a walk, at 0.5 s), Triangle (at
    // 1 s); Circle from 1.4 s, a tap every 1.5 s. A round's start (phase 1 to 2) frees the players by itself within a few
    // frames of the change: nothing pressed there. Dead, or no player yet: a Circle tap every 1.5 s (the respawn)
    const bool waiting = gMe.obj && gMe.alive;
    if (waiting && gRoom.phase >= 0 && gRoom.phase != 2) return;
    static long long waitAt = -1, waitSeen = -100000;
    if (waiting) {
        if (gFrame - waitSeen > 2) waitAt = gFrame;           // (a new wait: the page gone, the round started, a spawn)
        waitSeen = gFrame;
        const long long t = gFrame - waitAt;
        // (v141 beta 3) nothing pressed: the game deploys the player by itself (the run_deploy141 bench: 7 deploys of 7 with no
        // press, none with a strike; Circle taps: 6 strikes in 8). 10 s still waiting: Cross once a second (0 strikes in 7).
        // MGO1_DEPLOY_BTN: "old" the v137 ways (the stick, Triangle, Circle), a hex mask that button once a second
        static int deployMode = -2; static uint32_t deployBtn = 0x4000;
        if (deployMode == -2) {
            const char* e = getenv("MGO1_DEPLOY_BTN");
            deployMode = e && !strcmp(e, "old") ? -1 : e && strcmp(e, "none") ? 1 : 0;
            if (deployMode == 1) deployBtn = (uint32_t)strtoul(e, nullptr, 16);
        }
        if (deployMode == 0 && t < 600) return;
        if (deployMode >= 0) {
            if (t >= 30 && (t - 30) % 60 == 0) { gPad.buttons |= deployBtn; if (gDebug) Log("respawn wait: %04x (%lld frames)", deployBtn, t); }
            return;
        }
        if (t >= 30 && t < 44) {
            if (t == 30 && gDebug) Log("respawn wait: the stick pushed");
            Steer(std::sin(Facing()), std::cos(Facing()), kStep);
            return;
        }
        if (t >= 60 && t < 64) {
            if (t == 60 && gDebug) Log("respawn wait: Triangle");
            gPad.buttons |= 0x1000;
            return;
        }
        if (t < 84) return;
        if (gFrame - gCircleAt >= 90) { gCircleAt = gFrame; if (gDebug) Log("respawn wait: Circle (%lld frames)", t); }
        if (gFrame - gCircleAt < 4) gPad.buttons |= 0x2000;
        return;
    }
    // (v141 beta 6, tests) MGO1_DOLL_RESPAWN_BTN (hex, 2000 Circle) and MGO1_DOLL_RESPAWN_EVERY (frames, 90): the press while dead
    static uint32_t deadBtn = 0; static int deadEvery = 0;
    if (!deadEvery) {
        const char* b = getenv("MGO1_DOLL_RESPAWN_BTN"); const char* e = getenv("MGO1_DOLL_RESPAWN_EVERY");
        deadBtn = b ? (uint32_t)strtoul(b, nullptr, 16) : 0x2000; deadEvery = e ? std::max(8, atoi(e)) : 90;
    }
    if (gFrame - gCircleAt >= deadEvery && gFrame - gOutSince > 60) { gCircleAt = gFrame; }
    // (the first spawn of a map needs Circle: the respawn bench's dolls pressing Cross or nothing at the join never came in, 9
    // minutes; after a death the game spawns the player by itself): the test's button only once spawned on this map
    static const void* knobMap = nullptr; static int knobSpawns = 0;
    if (gMap.get() != knobMap) { knobMap = gMap.get(); knobSpawns = gRs.respawns; }
    if (gFrame - gCircleAt < 4) gPad.buttons |= gRs.respawns > knobSpawns ? deadBtn : 0x2000;
}

// (v141 beta 6) the status's respawn and fight blocks (filled by the respawn's and the fight's state machines)
std::string RespawnJson() { return RespawnJsonNow(); }
// (v141 beta 6) the status's first person view weapon block: its state, the weapon, the time in the state and in the stay, the
// reason, its shots; the stays and the watchdog's recoveries of this run; the last check that kept it out
std::string FpvJson() {
    char b[360];
    const bool on = gFv.state != FPV_NONE;
    snprintf(b, sizeof b, "{\"state\":\"%s\",\"weapon\":\"%s\",\"state_ms\":%lld,\"stay_ms\":%lld,\"reason\":\"%s\",\"shots\":%d,\"stays\":%d,\"watchdog\":%d,"
             "\"not_entered\":\"%s\"}", kFpvNames[gFv.state], on ? FpvName(gFv.weapon) : "NONE", on ? (gFrame - gFv.since) * 1000 / 60 : 0LL,
             on ? (gFrame - gFv.stayAt) * 1000 / 60 : 0LL, gFv.why ? gFv.why : "", gFv.shots, gFvStaysAll, gFvDogAll, gFvNo);
    return b;
}
// (v141 beta 6, aim) the aim's mode: NONE (no fight), FPV, AUTOAIM (L1 held), MANUAL (the third person's turn by hand); the auto aim valid
// (L1 held, the game's lock on), and why it is not firing with it (AutoAimFrame's reason)
const char* AimMode() { return gFi.target < 0 ? "NONE" : gFi.fpv ? "FPV" : gAa.on && gAa.target == gFi.target ? "AUTOAIM" : "MANUAL"; }
bool AaValid() { return gFi.target >= 0 && !gFi.fpv && gAa.on && gAa.target == gFi.target && LockOn(); }
std::string FightJson() {
    const char* reason = "";
    const char* dec = DecisionNow(&reason);
    const bool aa = gFi.target >= 0 && gAa.target == gFi.target;
    char b[512];
    snprintf(b, sizeof b, "{\"aim_mode\":\"%s\",\"target_id\":%d,\"autoaim\":{\"drawn\":%s,\"valid\":%s,\"why\":\"%s\",\"spells\":%d,\"shots\":%d},"
             "\"box\":{\"pop_phase\":%d,\"approach\":%s,\"last_s\":%.1f},\"decision\":\"%s\",\"reason\":\"%s\"}",
             AimMode(), gFi.target, aa && gAa.want ? "true" : "false", AaValid() ? "true" : "false", aa ? gAa.why : "", aa ? gAa.spells : 0, aa ? gAa.shots : 0,
             gBp.phase, gBw.on ? "true" : "false", gBoxPopAt > 0 ? (gFrame - gBoxPopAt) / 60.0 : -1.0, dec, reason);
    return b;
}
std::string AimFields() {
    std::string s = std::string("aim_mode=") + AimMode() + " autoaim_valid=" + (AaValid() ? "1" : "0");
    if (gFi.target >= 0 && !gFi.fpv && gAa.target == gFi.target && *gAa.why) s += std::string(" autoaim_why=") + gAa.why;
    return s;
}

}  // namespace in
}  // namespace doll
