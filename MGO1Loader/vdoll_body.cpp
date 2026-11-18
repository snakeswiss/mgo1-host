// vdoll_body.cpp - a virtual doll's body (vdoll_body.h, docs/VIRTUAL_DOLL.md 7)
//
// Measured on the reference capture of two full dolls (team deathmatch, City Under Siege): every 100 ms a PC's game sends
// for its player {0x12 ...} (its weapon down 0xFF, or up: the eye in tenths, the view's pitch and yaw), {0 x y z} (the
// hips, floats: the floor + 800), {1, 0, 0, facing, 0, 0} (the facing: atan2(x, z), 0x10000 a turn), {6, style} (0x40 a
// run, 389 mm a record; 0x20 a walk, 158 mm; 0 still), {0x13, 0, weapon, 2}, and every 200 ms {0x17 x y z} (the point
// looked at, tenths). Aiming, {8, kind, 0, x y z, u32} every 100 ms: the point 50 m along the view (kind 3; 5 when a
// shot left since the last one).
#include "vdoll_body.h"
#include "vdoll_adapt.h"
#include "vdoll_book.h"
#include "vdoll_blast.h"
#include "vdoll_wake.h"
#include "vdoll_cqc.h"
#include "vdoll_support.h"
#include "vdoll_stamina.h"
#include "vdoll_food.h"
#include "vdoll_react.h"
#include "doll_nav.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <thread>

namespace vdoll {
namespace body {
namespace {

Host gHost;
bool gOn = false, gDebug = false, gCqcTest = false;   // (MGO1_VDOLL_CQC_TEST: benches, no shot from level 6, a rush from 25 m and a grab)
bool gDartTest = false;                          // (v150-host beta 18, MGO1_VDOLL_DART_TEST: benches, Snake and the sneakers with their Mk22 only, at any range)
bool gNadeTest = false;                          // (v150-host beta 18, MGO1_VDOLL_NADE_TEST: benches, a sneaker's or Snake's stun grenade at an enemy seen 4 to 22 m away, every 5 s, at any level, and no dart)
int gTestTarget = 0;                             // (v150-host beta 18, MGO1_VDOLL_TEST_TARGET=<player number>: benches, its fights with that player only)
bool gTrapTest = false;                          // (v150-host beta 21, MGO1_VDOLL_TEST_TRAP: benches, its ways to the nearest claymore or book lying there)
int gTestHarmless = -1;                          // ((v150-host beta 24, cqcfight) MGO1_VDOLL_TEST_HARMLESS=<player number> or real (every player who is not a
                                                 // virtual doll): benches, its bullets and grenades on him with a damage of 0, their records, counts and lines as ever:
                                                 // a bench's player kept alive to count the hits; -1 off, -2 real)
std::vector<int> gLevels;                        // MGO1_VDOLL_LEVEL: one a doll, the last for the rest (1 to 10)
std::vector<int> gWeapons;                       // (beta 15) MGO1_VDOLL_WEAPONS: ak47, xm16e1, scorpion, m37, svd or auto (-1), the same way
std::vector<int> gSpecials;                      // (v150-host beta 22, heavy) its "+rpg7" or "+flamethrower" (W_RPG7, W_FLAME; -1 none), the same way
bool gHeavy = true;                              // (v150-host beta 22, heavy) MGO1_VDOLL_HEAVY (0: as beta 21, a special weapon never in its hands)
std::string gLearnDir;                           // MGO1_DOLL_LEARN: the full dolls' meshes, read when there
long long gFrame = 0;

#define BLOG(...) do { if (gHost.log) gHost.log(__VA_ARGS__); } while (0)

const float kPi = 3.14159265f, kTurn = 6.2831853f;
const float kRun = 389.f / 6.f;                  // mm a frame (60 a second): a run, 389 mm a record
const float kHips = 800.f;                       // the hips over the floor
const float kEye = 750.f;                        // the eye over the hips
const float kAimReach = 50000.f;                 // the point aimed at: 50 m along the view
const float kLookReach = 48000.f, kLookDown = 14.6f * kPi / 180.f;   // the third person view's look
const float kRange = 120000.f;                   // a bullet's reach

unsigned gSeed = 0x6C078965u;
unsigned Rnd() { gSeed ^= gSeed << 13; gSeed ^= gSeed >> 17; gSeed ^= gSeed << 5; return gSeed; }
float Rnd01() { return (Rnd() & 0xFFFFFF) / 16777216.f; }
float Gauss() { const float u = std::max(1e-6f, Rnd01()), v = Rnd01(); return std::sqrt(-2.f * std::log(u)) * std::cos(kTurn * v); }
// (v150-host beta 16: no number, or a huge one that the loop would never bring back, is 0)
float Wrap(float a) { if (!(std::fabs(a) < 1e4f)) return 0.f; while (a > kPi) a -= kTurn; while (a < -kPi) a += kTurn; return a; }
float Dist2D(const float* a, const float* b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
float Dist3(const float* a, const float* b) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
float Bearing(const float* from, const float* to) { return std::atan2(to[0] - from[0], to[2] - from[2]); }
uint16_t Angle16(float a) { return std::isfinite(a) ? (uint16_t)((long)lrintf(a / kTurn * 65536.f) & 0xFFFF) : 0; }
int16_t Tenth(float v) { return std::isfinite(v) ? (int16_t)std::max(-32768L, std::min(32767L, (long)lrintf(v / 10.f))) : 0; }
void Put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
void PutF(std::vector<uint8_t>& o, float f) { uint32_t v; memcpy(&v, &f, 4); for (int k = 0; k < 4; ++k) o.push_back((uint8_t)(v >> (8 * k))); }
void Dir(float yaw, float pitch, float* d) { d[0] = std::sin(yaw) * std::cos(pitch); d[1] = -std::sin(pitch); d[2] = std::cos(yaw) * std::cos(pitch); }

// ---- the map: its mesh, one for all the dolls, built on a thread when a map's page comes in ----
struct Map {
    std::string page, key;
    int extra = 0;
    std::unique_ptr<doll::Geom> geom;
    std::unique_ptr<doll::NavMesh> nav;
    std::atomic<int> state{0};                   // 1 building, 2 ready, 3 failed
    std::string log;
};
std::shared_ptr<Map> gMap;
bool Ready() { return gMap && gMap->state == 2; }

// ---- (beta 16) the bodies' work counted, not timed: on a processor the server shares, a pass's time holds its waits for
// that processor too (10 November: the host's "vdoll: 60 s" line read a pass of 78 ms every minute and 0.3 ms on average
// with three dolls). A pass's lines of sight over the map's collision, its way searches and its grenade flights tell what
// it did; a line every 3600 frames with some work: the sums, the busiest pass and its frame (a way search weighs about 8
// lines of sight, a flight's 54 steps about 50) ----
struct Work { long long sight = 0, ways = 0, flights = 0; long long Cost() const { return sight + 8 * ways + 50 * flights; } };
Work gWorkPass, gWorkSum, gWorkMost;             // this pass's, the sums since the last line, the busiest pass since
long long gWorkAt = 0, gWorkMostAt = 0, gWorkFrom = -1, gWorkPasses = 0;
// (beta 16) the way searches of a pass shared by all the dolls: a doll that needs a new way of its own (Run: a place of the
// map, the enemies' side, its objective) when the pass has made kWaysAPass searches already waits for the next pass, a
// frame later, 6 passes at most (0.1 s, a failed search's own wait); 15 dolls with no way tried 10 each in one pass. The
// ways of a fight (after an enemy lost, a rush) are never put off. MGO1_VDOLL_BUDGET=0: as before
const int kWaysAPass = 12;
bool gBudget = true;
long long gPutOff = 0;                           // (new ways put off a pass since the last line)
bool LineClear(const float* a, const float* b) { ++gWorkPass.sight; return gMap->nav->Clear(a, b); }
// (v150-host beta 24, snakehouse) MGO1_VDOLL_STAIRS (1; 0: beta 23's bodies): the doorways' panels a player walks through out
// of the bodies' walking (doll_nav.h NavConfig::doorways) and the stairs of blocks taken as a player takes them (StairsAsClimbs
// below). Until beta 23 a body never left Mountaintop's sunken house where SNEAKING puts Snake (docs/VIRTUAL_DOLL.md 43)
bool gStairs = true;
// (v150-host beta 25, meshmargin) MGO1_VDOLL_MESH_MARGIN (1; 0: beta 25's mesh): no standing or crouching place of the bodies' mesh
// nearer a wall of the stage's faces than a player's body stands (211 mm): where beta 25's came nearer (by the objectives, under rails
// and leaning rocks, round thin posts) the cells round the place go (doll_nav.h NavConfig::margin, docs/VIRTUAL_DOLL.md 50)
bool gMeshMargin = true;
bool gSpotTest = false;                            // ((v150-host beta 25, meshmargin, benches) MGO1_VDOLL_TEST_SPOT "x,y,z": Plan below)
float gSpot[3] = {0, 0, 0};
void StairsAsClimbs(std::vector<doll::Waypoint>& w);
// (v150-host beta 24, snakehouse) MGO1_VDOLL_VAULT (1; 0: beta 23's bodies, a fence never crossed): a fence or a low wall that
// a player climbs over with the action button (the mesh's vault links: its top 1.55 m at most, 350 mm thick at most, a floor
// on the far side within 0.55 m of the near one's height) crossed as a real client's game crosses it (Vault below)
bool gVault = true;
const float kVaultPace = 26.f;                     // (mm a frame: a real client's climb over, Vault below)
const float kVaultRise = 300.f;                    // (the far floor this much higher at most: LinkOk)
// (v150-host beta 24, snakehouse2) a way planned in a fight (the doll has a target: its chase, its rush, its step aside, its way
// in or round a teammate) crosses no fence, as beta 23's: over a fence it fires no shot for 35 to 50 frames, and the way to an
// enemy made again every 45 frames went over a fence and back (tests/vdoll_cqcfight_test.cpp on n021a, the hold at 0 degrees
// and 7 m: three fences in 120 frames, no shot in 6 s). Its ways out of a fight (the objective, its own) cross them (PlanTo)
bool gFightWay = false;
// (v150-host beta 24, snakehouse) MGO1_VDOLL_ITEM_WAY (1; 0: as beta 23): a free item no way of the body's leads to (up a ladder:
// n002a's film FA828B on its tower) is passed over for 30 s for the next free one, as a player who cannot climb there takes
// the other; beta 23's doll kept trying the nearest one (its way refused each time) and roamed, the item it could take never
// its objective while the other lay nearer (ObjectiveGoal, Plan)
bool gItemWay = true;
const long long kItemNoWay = 1800;                 // (frames an item out of its ways is passed over)
bool FindWay(const float* from, const float* to, std::vector<doll::Waypoint>& w, float* len) {
    ++gWorkPass.ways;
    if (!gMap->nav->Path(from, to, w, len)) return false;
    StairsAsClimbs(w);
    return true;
}
// (v150-host beta 21, geom) a sight's or a shot's line: clear on the bodies' collision (every face of the stage's own
// collision file: a superset of what stops a bullet on the 15 maps, tools/dev150/geom_audit) AND through the things the host's
// game made while the round runs (tree trunks, props, cardboard boxes, a player's shield: they are in no file, the host's game
// holds them; Host::gameLine),
// asked only when the first is clear and the game holds such a thing. A bullet goes through a thing softer than its power
// (the game's own rule; the powers below are the game's): a shot's line follows the weapon held. A sight's line stops at
// every thing but a pane of glass (its faces' own attribute bit; the game's windows, softer than every bullet): a
// cardboard box hides a player though a bullet goes through it, a window does not. Beta 20 asked the bodies' collision
// alone. MGO1_VDOLL_GAME_LINES=0: as beta 20
bool gGameLines = true;
const int kSightPower = -(1 << 30);              // (a sight: no thing is seen through, the softest one too, but glass)
const uint32_t kGlassFace = 0x04000000u;         // (a face's attribute word: glass)
// (its cost: the sum, the longest, and how many took under 2, 5, 10, 20, 50, 100, 200, 500 us and more: on a busy machine a
// call's time on the clock holds the other threads' turns, the half-way time does not)
const int kLineCostSteps[8] = {2, 5, 10, 20, 50, 100, 200, 500};
struct GameLines { long long asked = 0, blocked = 0, unanswered = 0, ns = 0, most = 0, soft = 0, atEnd = 0, under[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; } gGameLineSum;
long long gStanceHits[3] = {0, 0, 0};            // (the dolls' hits this minute on a standing, a crouched, a lying player)
long long gMuzzleStops = 0, gMuzzleViews = 0;    // (v150-host beta 24, walls3, this minute: bullets met by the cover before the gun; views taken for it)
// (v150-host beta 24, walls3) this minute's hits on real players as the cover gate judged them (the owner's logs held no line of
// it: its counts were in the body's status line, MGO1_VDOLL_DEBUG only): 0 counted, 1 hidden at the place he last sent, 2 within
// his reach, 3 its own copy out of his line, 4 counted with no record of him to judge (alive), 5 its records gone after his
// death (the host's game refuses those: docs/VIRTUAL_DOLL.md 38); and those counted after the copy's hold of a second with its
// copy still hidden
long long gGate[6] = {0, 0, 0, 0, 0, 0}, gGateLate = 0;
int GameLine(const float* a, const float* b, bool stage, vdoll::LineHit* hit) {
    if (!gHost.gameLine) return -1;
    const auto t0 = std::chrono::steady_clock::now();
    const int r = gHost.gameLine(a, b, stage, hit);
    const long long ns = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    ++gGameLineSum.asked; gGameLineSum.ns += ns; gGameLineSum.most = std::max(gGameLineSum.most, ns);
    int step = 0;
    while (step < 8 && ns >= (long long)kLineCostSteps[step] * 1000) ++step;
    ++gGameLineSum.under[step];
    if (r < 0) ++gGameLineSum.unanswered;
    return r;
}
// a thing of the game stops the line a -> b for a bullet of that power (the game's bullet: on through each thing softer than
// it, 18 marks at most) or for a sight (kSightPower: on through glass alone)
float gThingAt = -1.f;                           // (the last line asked: where a thing stopped it, mm from its start; -1 none did)
bool ThingStops(const float* a, const float* b, int power) {
    gThingAt = -1.f;
    if (!gGameLines || !gHost.gameLine) return false;
    float from[3] = {a[0], a[1], a[2]};
    const float len = Dist3(a, b);
    if (len < 1.f) return false;
    const float d[3] = {(b[0] - a[0]) / len, (b[1] - a[1]) / len, (b[2] - a[2]) / len};
    for (int i = 0; i < 18; ++i) {
        vdoll::LineHit h;
        if (GameLine(from, b, false, &h) <= 0) return false;
        const float along = (h.at[0] - a[0]) * d[0] + (h.at[1] - a[1]) * d[1] + (h.at[2] - a[2]) * d[2];
        const bool through = h.object && (power == kSightPower ? (h.attr & kGlassFace) != 0 : h.hardness < power);
        if (!through) { ++gGameLineSum.blocked; if (len - along < 600.f) ++gGameLineSum.atEnd; gThingAt = along; return true; }
        ++gGameLineSum.soft;
        const float next = along + 30.f;                     // (on from just behind the face it went through)
        if (!(next > 0.f) || next >= len - 1.f) return false;
        for (int k = 0; k < 3; ++k) from[k] = a[k] + d[k] * next;
    }
    return true;
}
// (v150-host beta 22, blast) a smoke grenade's cloud hides a player from a virtual doll's eyes as from a player's, and a stun
// grenade's flash whitens them, by the game's own numbers (vdoll_blast.h, docs/VIRTUAL_DOLL.md 31): until beta 21 a doll saw
// and shot through every cloud and fought on within the second of a flash that leaves a player white 2 to 24 s.
// MGO1_VDOLL_BLASTS=0: both as beta 21 (MGO1_VDOLL_SMOKE=0, MGO1_VDOLL_FLASH=0: one of them)
bool gSmoke = true, gFlash = true;
bool gReactOut = true;                           // (its own grenade's reaction record: Blast)
// (v150-host beta 22, blast; review22a B2) a thrower turns away from its own stun grenade until it goes off, as a player does
// (Avert); the game's white still its rule. MGO1_VDOLL_TURN_AWAY=0: it keeps facing its throw (beta 22's first version: 4 to
// 16.5 s of nothing seen after most of its own stun grenades); off with the flash
bool gAvert = true;
vblast::Cloud gClouds[8];
int gCloudN = 0;
long long gSmokeHid = 0;                         // (the lines a cloud stopped this minute: the log)
bool SmokeBetween(const float* a, const float* b) {
    if (!gSmoke || gCloudN <= 0 || !vblast::SmokeHides(a, b, gClouds, gCloudN)) return false;
    ++gSmokeHid;
    return true;
}
bool SightClear(const float* a, const float* b) {
    gThingAt = -1.f;
    if (SmokeBetween(a, b)) return false;        // (beta 22, blast: a smoke's cloud, as a player sees it)
    if (!LineClear(a, b)) return false;
    return !ThingStops(a, b, kSightPower);
}
// ---- (v150-host beta 24, walls3) MGO1_VDOLL_BLAST_COVER (on; 0: as beta 23): a doll's blasts and flames stop where the game's
// own do. The game's blast wounds a player only with a clear line from it to him through the stage AND the things (its filter
// US n003a 0x356300 asks 0x15A590 with the kinds 0x1F); its rocket goes off on the first face or thing it meets (0x160950); its
// grenade bounces off both. Beta 23's dolls: the rocket's flight passed every thing (it asked them with a power no thing is
// harder than: a crate, a tree trunk, a vehicle let it through to the player behind); the frag's blast asked the stage alone,
// at the host's copy of the player (not the place he last sent, as the rocket's, the claymore's and the flame's are); the frag
// flew through the things; and the rocket's, the claymore's and the flame's lines took the things from the cover gate's 24
// game lines a verdict, none left after a verdict that spent them ----
bool gBlastCover = true;
const int kStopsAll = 0;                         // (a power no thing is softer than: every thing stops it, glass too)
// a blast's or a flame's line from its middle to a player's body: the stage's collision, then every thing of the game but glass
// (the sight's rule, as beta 23's gate; whether the game's blast passes glass is not known), each line its own test
bool BlastClear(const float* a, const float* b) { gThingAt = -1.f; return LineClear(a, b) && !ThingStops(a, b, kSightPower); }
// (Frame, once a pass before the dolls' own: the pass before closed)
void WorkPass(long long frame) {
    if (gWorkPass.Cost() > gWorkMost.Cost()) { gWorkMost = gWorkPass; gWorkMostAt = gWorkAt; }
    gWorkSum.sight += gWorkPass.sight; gWorkSum.ways += gWorkPass.ways; gWorkSum.flights += gWorkPass.flights;
    gWorkPass = Work(); gWorkAt = frame; ++gWorkPasses;
    if (gWorkFrom < 0 || frame < gWorkFrom) gWorkFrom = frame;
    if (frame - gWorkFrom < 3600) return;
    if (gWorkSum.Cost() > 0)
        BLOG("vdoll: %lld frames: the bodies' work %lld lines of sight, %lld way searches and %lld grenade flights in %lld passes; the most in one pass %lld, %lld and %lld (frame %lld)",
             frame - gWorkFrom, gWorkSum.sight, gWorkSum.ways, gWorkSum.flights, gWorkPasses, gWorkMost.sight, gWorkMost.ways, gWorkMost.flights, gWorkMostAt);
    if (gGameLineSum.asked) {
        long long seen = 0;
        int half = 8;
        for (int i = 0; i < 9; ++i) { seen += gGameLineSum.under[i]; if (seen * 2 >= gGameLineSum.asked) { half = i; break; } }
        char mid[32];
        if (half < 8) snprintf(mid, sizeof mid, "under %d us", kLineCostSteps[half]); else snprintf(mid, sizeof mid, "500 us or more");
        BLOG("vdoll: %lld frames: %lld lines asked of the game's own things (trees, props, boxes, shields), %lld stopped by one (%lld within 0.6 m of the line's end), %lld through a soft one (%lld not answered); half of them %s, %.1f us each on the clock, the longest %lld us",
             frame - gWorkFrom, gGameLineSum.asked, gGameLineSum.blocked, gGameLineSum.atEnd, gGameLineSum.soft, gGameLineSum.unanswered, mid,
             (double)gGameLineSum.ns / 1000.0 / (double)gGameLineSum.asked, gGameLineSum.most / 1000);
    }
    gGameLineSum = GameLines();
    if (gStanceHits[1] + gStanceHits[2])
        BLOG("vdoll: %lld frames: the dolls' hits on a standing player %lld, a crouched one %lld, a lying one %lld", frame - gWorkFrom, gStanceHits[0], gStanceHits[1], gStanceHits[2]);
    gStanceHits[0] = gStanceHits[1] = gStanceHits[2] = 0;
    if (gMuzzleStops || gMuzzleViews)   // (v150-host beta 24, walls3)
        BLOG("vdoll: %lld frames: %lld third person bullets met by the cover before the gun, the first person view taken in %lld fights for it (MGO1_VDOLL_MUZZLE)", frame - gWorkFrom, gMuzzleStops, gMuzzleViews);
    gMuzzleStops = gMuzzleViews = 0;
    if (gGate[0] + gGate[1] + gGate[2] + gGate[3] + gGate[4] + gGate[5])   // (v150-host beta 24, walls3)
        BLOG("vdoll: %lld frames: the dolls' hits on real players by the cover gate: %lld counted (%lld fired after its copy's hold of a second), %lld not counted (%lld hidden at the place he last sent, %lld within his reach, %lld its copy out of his line), %lld with no record of him (counted, not judged), %lld sent after his death",
             frame - gWorkFrom, gGate[0], gGateLate, gGate[1] + gGate[2] + gGate[3], gGate[1], gGate[2], gGate[3], gGate[4], gGate[5]);
    for (long long& g : gGate) g = 0;
    gGateLate = 0;
    if (gSmokeHid) BLOG("vdoll: %lld frames: %lld lines of the dolls' eyes stopped by a smoke's cloud", frame - gWorkFrom, gSmokeHid);   // (beta 22, blast)
    gSmokeHid = 0;
    if (gPutOff) BLOG("vdoll: %lld frames: %lld new ways put off to the next pass (%d way searches a pass for all the dolls; MGO1_VDOLL_BUDGET=0: none)", frame - gWorkFrom, gPutOff, kWaysAPass);
    gWorkSum = gWorkMost = Work(); gWorkPasses = 0; gWorkMostAt = 0; gWorkFrom = frame; gPutOff = 0;
}

uint64_t Fnv64(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
bool ReadAll(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char b[65536];
    size_t n;
    out.clear();
    while ((n = fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    fclose(f);
    return !out.empty();
}
bool GetPart(const std::string& s, size_t& at, std::string& part) {
    uint32_t n = 0;
    if (at + 4 > s.size()) return false;
    memcpy(&n, s.data() + at, 4); at += 4;
    if (at + n > s.size()) return false;
    part.assign(s.data() + at, n); at += n;
    return true;
}
// (v150-host beta 24, snakehouse) the vaults' second look, the playable dolls' rule (doll.cpp LookAtVault, VaultsSecondLook, v137:
// measured on real clients): over a sheet (a fence, a railing: under 60 mm thick) the mesh's 1.55 m stand; over a wall the
// action button climbs only a top within 1.2 m (climbFacesMax) of the floor at its foot on both sides. The others are left out
// of the bodies' paths when MGO1_VDOLL_VAULT is on (n022a's wall at 120431 73157 -202175: its top 1.48 and 1.59 m over its
// floors, never crossed by a player). Their count
int VaultsSecondLook(doll::NavMesh& nav) {
    int left = 0;
    for (int i = 0; i < nav.LinkCount(); ++i) {
        float a[3], b[3]; uint8_t area = 0;
        if (!nav.LinkAt(i, a, b, &area) || area != doll::AREA_VAULT) continue;
        const float l = std::hypot(b[0] - a[0], b[2] - a[2]);
        if (l < 1.f) continue;
        const float ux = (b[0] - a[0]) / l, uz = (b[2] - a[2]) / l, len = l + 200.f;
        auto at = [&](float r, float y, float* out) { out[0] = a[0] + ux * r; out[1] = y; out[2] = a[2] + uz * r; };
        float ta = 0.f, tb = 0.f, n[3];
        bool hit = false;
        for (float h : {350.f, 250.f, 650.f}) {
            const float p[3] = {a[0] - ux * 100.f, a[1] + h, a[2] - uz * 100.f}, q[3] = {b[0] + ux * 100.f, b[1] + h, b[2] + uz * 100.f};
            if (nav.FirstHit(p, q, &ta, n) && nav.FirstHit(q, p, &tb, n)) { hit = true; break; }
        }
        if (!hit) continue;
        const float nearAt = ta * len - 100.f, farAt = (1.f - tb) * len - 100.f, thick = farAt - nearAt;
        float p[3], q[3], foot[2];
        at(nearAt - 200.f, a[1] + 600.f, p); foot[0] = nav.GroundBelow(p, 2000.f);
        at(farAt + 200.f, b[1] + 600.f, p); foot[1] = nav.GroundBelow(p, 2000.f);
        if (foot[0] < -1e29f || foot[1] < -1e29f || thick < 60.f) continue;
        const float base = std::max(foot[0], foot[1]);
        float top = -1e30f;
        for (float h = 100.f; h <= 2600.f; h += 50.f) {
            float p2[3], q2[3];
            at(nearAt - 250.f, base + h, p); at(farAt + 250.f, base + h, q);
            at(nearAt - 250.f, base + h + 100.f, p2); at(farAt + 250.f, base + h + 100.f, q2);
            if (nav.Clear(p, q) && nav.Clear(p2, q2)) { top = base + h - 25.f; break; }
        }
        if (top < -1e29f || std::max(top - foot[0], top - foot[1]) <= nav.Config().climbFacesMax) continue;
        if (nav.DisableLink(a, b, 50.f)) ++left;
    }
    return left;
}
// (the full dolls keep each map's meshes in their folder, doll.cpp BuildMap: <key>_<the collision's hash>.nav, its first
// part the mesh; read here when there, never written: their file holds their wide mesh too)
void Build(std::shared_ptr<Map> m) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> bytes;
    std::string err;
    if (!gHost.stageGeom || !gHost.stageGeom(m->page, m->extra, &bytes, &err)) { m->log = "vdoll: map " + m->key + ": no collision (" + err + ")"; m->state = 3; return; }
    auto g = std::make_unique<doll::Geom>();
    if (!doll::DecodeGeom(bytes.data(), bytes.size(), *g, err)) { m->log = "vdoll: map " + m->key + ": " + err; m->state = 3; return; }
    auto nav = std::make_unique<doll::NavMesh>();
    std::string how, log;
    if (!gLearnDir.empty() && !gStairs) {             // (v150-host beta 24, snakehouse: the full dolls' meshes hold the doorways)
        char hex[24];
        snprintf(hex, sizeof hex, "%016llx", (unsigned long long)Fnv64(bytes.data(), bytes.size()));
        const std::string path = gLearnDir + "\\" + m->key + "_" + hex + (getenv("NAV_KEEP_TABLES") ? "_t" : "") +
                                 (getenv("NAV_KEEP_LOW_VAULTS") ? "_v" : "") + ".nav";
        std::string all, a;
        size_t at = 0;
        if (ReadAll(path, all) && GetPart(all, at, a) && nav->Load(*g, a, log)) how = "read from the dolls' folder";
    }
    if (how.empty()) {
        doll::NavConfig cfg;
        cfg.doorways = gStairs;                        // (v150-host beta 24, snakehouse)
        cfg.margin = gMeshMargin;                      // (v150-host beta 25, meshmargin)
        if (!nav->Build(*g, cfg, log)) { m->log = "vdoll: map " + m->key + ": " + log; m->state = 3; return; }
        how = "built";
    }
    int vaultsLeft = gVault ? VaultsSecondLook(*nav) : 0;   // (v150-host beta 24, snakehouse)
    const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    char b[512];
    snprintf(b, sizeof b, "vdoll: map %s: %zu faces, %d polygons, %s in %ld ms (the bodies' mesh)", m->key.c_str(), g->faces.size(), nav->Polys(),
             how.c_str(), ms);
    if (vaultsLeft) snprintf(b + strlen(b), sizeof b - strlen(b), "; %d fences left out (their top over 1.2 m: no player crosses them)", vaultsLeft);
    const size_t mg = log.find("; the walls' margin: ");   // (v150-host beta 25, meshmargin: doll_nav.cpp's count)
    if (mg != std::string::npos) snprintf(b + strlen(b), sizeof b - strlen(b), "%s", log.c_str() + mg);
    m->log = b;
    m->geom = std::move(g);
    m->nav = std::move(nav);
    m->state = 2;
}

// ---- the players as the host's game holds them, read every 100 ms; each one's run (mm a frame, a few reads' average) ----
PlayerNow gSeen[16];
bool gHave[16];
float gVel[16][3];
long long gSeenAt = -1000;
int gRule = -1;
// (v150-host beta 16, the configurations' audit) the game's weapons setting: what a player may carry (1 a primary, 2 the
// secondary, 4 the support row's grenade) for 0 all, 1 no primary, 2 no secondary, 3 no support, 4 primary only, 5
// secondary only, 6 support only, 7 knife only; not known: all, as before
int gWeaponRule = -1;
unsigned Allowed() { static const unsigned k[8] = {7, 6, 5, 3, 1, 2, 4, 0}; return gWeaponRule >= 0 && gWeaponRule <= 7 ? k[gWeaponRule] : 7u; }
// (v150-host beta 18) the last virtual doll's dart or stun grenade that reached each player (its frame, the doll's number):
// the host's game knocking him out within 3 s after it is said once (`player 4 knocked out (asleep) by vdoll 2's dart`)
long long gDartAt[16] = {-100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000,
                         -100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000};
int gDartBy[16] = {0}, gDartKind[16] = {0};
int gKnockOuts = 0;
int StanceOf(const PlayerNow& s);                // (beta 21, geom: below, with the points of a player)
// ---- (v150-host beta 21, cover) a real player killed behind cover on his own screen (the players' report of 12 November):
// the host's copy of a remote player (PlayerNow::pos) is moved by the game with his input records between his position
// records, and a doll's hit is a record that wounds him wherever he stands on his own screen. From the place he last sent
// to the moment its hit reaches his game he runs on: his record's age here, the way here and back (the session's own round
// trip to him), the host's game passing the hit on with its next datagram to him (reports/cover_kills.md).
// MGO1_VDOLL_COVER (on; 0: as beta 20): a hit counts only when the part it hit is in the doll's clear line (the map's
// collision, as its bullets) at the place he last sent and, while he moves, at every place he can have run to by then
// (the mesh's places within his run's reach, whatever way he turned; "run": straight on only, the first form). The hit is
// judged when its records go (the scope rule holds them a record or two), with what the host holds of him then.
// MGO1_VDOLL_COVER_MS=<ms>: the network one way, instead of half the session's round trip (100 when it has none).
// MGO1_VDOLL_REFLEX=<ms> (250; 0: as beta 20): a fight's first shot no sooner than that in real time after its sight (half
// after it saw him again), at every level. MGO1_VDOLL_COVER_LOG=1 (benches): a line at each hit judged (with the gate off:
// what it would say, the hit counted all the same); 2: and each real player's copy and record every 100 ms ----
enum { COVER_OFF = 0, COVER_RUN = 1, COVER_REACH = 2 };
int gCover = COVER_REACH;
int gCoverLog = 0;
bool gCoverSee = false;                          // (the gate off and its log on: judged for the log only)
// MGO1_VDOLL_KILL_HOLD=<ms> (0, off: as beta 20): after its kill of a real player it stays where it shot from that long,
// unless it sees an enemy, is hurt or holds the item: the victim's own game turns his camera to his killer's copy as it is
// THEN (the game's defeated camera: a quarter of a second after his death, a swing of 2.5 s, 5 s on him), and a doll that
// ran on at once is shown metres away, behind a wall (the players' report of 12 November, their third cause)
int gKillHoldMs = 0;
// MGO1_VDOLL_SHOWN (on; 0: as beta 20): no shot at a real player before the doll's OWN copy, as the host's game holds it (his
// game shows it there a relay later), has been in his line for the relay's frames and the network's way: a doll stops the
// moment it sees him, often at a corner's edge, its copy on the screens still 0.5 to 1.5 m behind it, behind the wall, and
// fired at once (cover_DU10_off: at his death the killer's copy on the victim's own game was out of his line in 19 kills
// of 36, the doll's real place in 2). A second at most (the game can hold a copy short): then it fires all the same
bool gShown = true;
const int kShownMaxMs = 1000;
int gReflexMs = 250;
float gNetMs = -1.f;                             // (MGO1_VDOLL_COVER_MS: the network one way; under 0: the session's round trip)
const float kNetNone = 100.f;                    // (one way, ms: a player whose session tells no round trip)
// (the host's game passes a doll's records on to a player with its own next datagram to him: measured 5 to 11 frames
// apart, cover_f2: a hit handed over at any moment waits 4 frames (median), 7 or less 85 times in 100, 9 or less 95;
// MGO1_VDOLL_COVER_RELAY=<frames>)
float gRelayFrames = 7.f;
const float kAheadMax = 2500.f;                  // (his run beyond his last record, mm at most)
const float kAlongStep = 150.f;                  // ("run": his run's places tested, mm apart)
const float kRunMs = 4.f;                        // (a run, mm a ms: kRun's 3.89 and a little)
const float kReachMore = 150.f;                  // (his reach's edge: half a step of the places asked, mm)
const float kLineStep = 150.f, kDepthStep = 400.f;   // (his reach: the lines from the eye across it, mm apart; the places along each)
const float kSnap = 250.f;                       // (a place he can stand at: the mesh within, mm)
double gMsAFrame = 1000.0 / 59.94;               // (the host's frames now, real ms each: slowed, longer)
// the machine's clock (real ms; the frames at 59.94 a second when the platform gives none: the tests)
unsigned long long RealMs() { return gHost.realMs ? gHost.realMs() : (unsigned long long)((double)gFrame * 1000.0 / 59.94); }
// each player's position records as they came (his last one, when it came, his run between his last two: mm a ms)
struct RecNow { bool have = false; float p[3] = {0, 0, 0}, v[3] = {0, 0, 0}; double at = 0; };
RecNow gRec[16];
void SeeRecord(int id, const PlayerNow& s, bool had) {
    RecNow& r = gRec[id];
    if (!had || !s.alive || s.sentAge < 0 || !(std::fabs(s.sent[0]) < 1e6f)) { r = RecNow(); return; }
    const double at = (double)RealMs() - s.sentAge;
    if (r.have && std::fabs(at - r.at) < 1.0) return;    // (the same record)
    if (r.have) {
        const double dt = at - r.at;
        const float jump = std::sqrt((s.sent[0] - r.p[0]) * (s.sent[0] - r.p[0]) + (s.sent[2] - r.p[2]) * (s.sent[2] - r.p[2]));
        if (dt >= 40.0 && dt <= 400.0 && jump < 3000.f) for (int k = 0; k < 3; ++k) r.v[k] = (float)((s.sent[k] - r.p[k]) / dt);
        else if (dt > 400.0 || jump >= 3000.f) r.v[0] = r.v[1] = r.v[2] = 0.f;
    }
    r.have = true; r.at = at; memcpy(r.p, s.sent, sizeof r.p);
}
void SeeAll(long long frame) {
    for (int id = 0; id < 16; ++id) {
        const PlayerNow was = gSeen[id];
        const bool had = gHave[id];
        gSeen[id] = PlayerNow();
        gHave[id] = gHost.see && gHost.see(id, &gSeen[id]);
        const PlayerNow& s = gSeen[id];
        SeeRecord(id, s, gHave[id] && s.object);         // (v150-host beta 21, cover)
        static long long asleepFrom[16] = {0};
        if (had && gHave[id] && s.asleep && !was.asleep) asleepFrom[id] = frame;
        if (had && gHave[id] && !s.asleep && was.asleep && s.alive)   // (v150-host beta 18) the host's flag off: he is up
            BLOG("vdoll: player %d no longer knocked out on the host's copy (%.1f s after it came)", id + 1, (frame - asleepFrom[id]) / 59.94);
        if (had && gHave[id] && s.asleep && !was.asleep && frame - gDartAt[id] < 180) {
            ++gKnockOuts;
            BLOG("vdoll: player %d knocked out (%s) by vdoll %d's %s %.1f s before (%d this game)", id + 1, gDartKind[id] == 0x15 ? "stunned" : "asleep",
                 gDartBy[id], gDartKind[id] == 0x15 ? "stun grenade" : gDartKind[id] == 0x10 ? "MOSIN NAGANT dart" : "Mk22 dart",
                 (frame - gDartAt[id]) / 59.94, gKnockOuts);
        }
        // (v150-host beta 21, geom; MGO1_VDOLL_DEBUG) a player's stance on the host's copy when it changes: his state word, his
        // style record, his hips over the floor under him
        if (gDebug && had && gHave[id] && s.object && s.alive && Ready() && gMap->nav &&
            StanceOf(s) != StanceOf(was)) {
            static const char* kStance[3] = {"standing", "crouched", "lying"};
            const float g = gMap->nav->GroundBelow(s.pos, 2500.f);
            BLOG("vdoll: player %d %s (state %x, style %02x), his hips %.0f mm over the floor", id + 1, kStance[StanceOf(s)], s.state, s.style & 0xFF,
                 g > -1e29f ? s.pos[1] - g : -1.f);
        }
        const float dt = (float)(frame - gSeenAt);
        if (!had || !gHave[id] || !s.alive || !was.alive || dt <= 0.f || dt > 30.f || Dist3(s.pos, was.pos) > 3000.f) {
            gVel[id][0] = gVel[id][1] = gVel[id][2] = 0.f;
            continue;
        }
        for (int k = 0; k < 3; ++k) gVel[id][k] = gVel[id][k] * 0.5f + (s.pos[k] - was.pos[k]) / dt * 0.5f;
    }
    gRule = gHost.rule ? gHost.rule() : -1;
    const int wr = gHost.weaponRule ? gHost.weaponRule() : -1;
    if (wr != gWeaponRule) {
        static const char* kName[8] = {"all", "no primary", "no secondary", "no support", "primary only", "secondary only", "support only", "knife only"};
        if (wr >= 0 && wr <= 7) BLOG("vdoll: the game's weapons setting %d (%s): the bodies carry %s", wr, kName[wr],
                                     wr == 0 ? "their primary, the M1911A1 and two grenades" : "what it allows (no gun: its fists and grenades; no secondary: the knife for a grab)");
        gWeaponRule = wr;
    }
}
// a player's hips now: his last place read and his run since
void Now(int id, float* out) {
    const float t = (float)(gFrame - gSeenAt);
    for (int k = 0; k < 3; ++k) out[k] = gSeen[id].pos[k] + gVel[id][k] * t;
}
// (v150-host beta 21, geom) a player's stance as the host's copy of him holds it: lying on his front (his first state word's
// bit 3: still, crawling, in his view, thrown down), crouched (its bit 2, or his style record as before), else standing.
// Beta 20 knew the crouch by the style record alone and took a lying player for a standing one: his chest and head were
// tested and hit 350 and 850 mm over his hips, where nothing of him is (a doll saw and shot him over low cover).
// MGO1_VDOLL_STANCE=0: as beta 20
bool gStance = true;
enum Stance { S_STAND, S_CROUCH, S_LIE };
int StanceOf(const PlayerNow& s) {
    if (gStance && (s.state & 8)) return S_LIE;
    if ((s.style & 0x0F) == 2 || (gStance && (s.state & 4))) return S_CROUCH;
    return S_STAND;
}
// the points a sight tests, over his hips: the chest, the head. Lying, his hips' point is 300 mm over the floor (the host's
// copy, measured) and his body lies along the ground under it: his chest 200 mm, his head 340 mm over the floor
const float kChestUp[3] = {350.f, 50.f, -100.f}, kHeadUp[3] = {850.f, 310.f, 40.f};
void Chest(const PlayerNow& s, float* out) { out[0] = s.pos[0]; out[1] = s.pos[1] + kChestUp[StanceOf(s)]; out[2] = s.pos[2]; }
void Head(const PlayerNow& s, float* out) { out[0] = s.pos[0]; out[1] = s.pos[1] + kHeadUp[StanceOf(s)]; out[2] = s.pos[2]; }
bool Enemy(const PlayerNow& s, int myTeam) {
    if (s.team == 0xFE || s.team == 0xFF) return false;
    if (gRule == 0) return true;                 // deathmatch: everyone else
    return (s.team == 0 || s.team == 1) && s.team != myTeam;
}

// ---- the weapons (v150-host beta 15: the game's weapons' and damage tables; the head x4 and a limb half for a readied
// shot, the M37's pellets 25 each with no part rule; the gun's rate or its own for a single shot, its magazine, its
// reload): its record 13 {13, 0, code, flags: 2 rounds in the magazine} ----
struct Weapon { const char* name; int code; int magazine, reload, every; bool automatic; int body; uint16_t fx; int pellets;
                float near, far;                     // (near, far: the fight's range it likes, mm)
                bool dart = false;                   // (v150-host beta 18) a tranquilizer: body is the stun, no life taken
                int heavy = 0; };                    // (v150-host beta 22, heavy) 1 the RPG-7's rocket, 2 the flamethrower's flame (FireHeavy)
// (v150-host beta 18) the non-lethal guns of TEAM SNEAKING's sneakers and of SNEAKING's Snake: the game's damage table (US
// n003a 0x2F4360, read by 0x4600F8) gives the Mk22 12000 and the MOSIN NAGANT 30000, both of the stamina (30000 at most:
// asleep at 0), the head x4 (x2 on Snake), a leg half; the magazines from the page's weapons table (0x2DAB4C): 8 and 5. A
// playable doll's Mk22 dart to a head: {14 id 03 00 4 ...} and {04 id, life 0, stun 30000, 05, 0, head 1} (tsne_b17_d2)
// (v150-host beta 18) each number measured on a playable doll's own game (probe 10, docs/VIRTUAL_DOLL.md 18): a shot's
// frames at best, the magazine, the frames from the magazine's last shot to the next one; none faster than the game lets
// a player (they were: the AK-47's and XM16E1's reload 64, the SCORPION's 6 a shot and 98, the M37's 40 and 110, the SVD's
// reload 96, the M1911A1's 12 and 55, the Mk22's reload 90, the MOSIN NAGANT's 60 a shot). The SVD's 24 a shot is slower
// than the 16 measured: left
const Weapon kWeapons[] = {
    {"AK-47", 0x0C, 30, 126, 6, true, 60, 0x010A, 0, 0.f, 45000.f},
    {"XM16E1", 0x0B, 20, 121, 6, true, 50, 0x010A, 0, 0.f, 45000.f},
    {"SCORPION", 0x0A, 30, 166, 7, true, 38, 0x010A, 0, 0.f, 30000.f},
    {"M37", 0x0E, 4, 310, 94, false, 25, 0x0208, 4, 0.f, 16000.f},
    {"SVD", 0x0F, 10, 192, 24, false, 180, 0x010A, 0, 10000.f, 90000.f},
    {"M1911A1", 0x06, 7, 106, 14, false, 30, 0x010A, 0, 0.f, 25000.f},
    {"Mk22", 0x05, 8, 166, 68, false, 12000, 0x0003, 0, 0.f, 25000.f, true},
    {"MOSIN NAGANT", 0x10, 5, 192, 180, false, 30000, 0x0003, 0, 10000.f, 90000.f, true},   // (its reload not measured: the SVD's)
    // (v150-host beta 22, heavy) a weapon box's: the RPG-7 (a rocket a magazine, its blast below) and the flamethrower (a stream:
    // no magazine, the box's 999 rounds; its contacts below)
    {"RPG-7", 0x11, 1, 145, 38, false, 150, 0x0200, 0, 12000.f, 120000.f, false, 1},   // (a pull and the rocket 14 frames, shown 24; the reload a pull, 145)
    {"FLAME THROWER", 0x09, 30000, 0, 6, true, 95, 0x0204, 0, 0.f, 5200.f, false, 2},   // (a stream: a moment every 6 frames, its contacts by FireFlame's clock)
    // (v150-host beta 22, pickup) the M63, a special item's weapon, never a doll's own: taken where it lies (docs/VIRTUAL_DOLL.md
    // 32). The game's damage table (n003a 0x2F4360: 70), the page's weapons table (0x2DAB4C: its magazine 100); one of the
    // rifles' family (its actor 0x403200, its fire routine 0x402478: the AK-47's effect record and bullet power 120); its pace
    // measured on a playable doll's own game (probe 47, bench pickup_t2_probe47: Square held, 6 frames a shot, the mode of 750
    // bullets; its belt's reload 332 frames in the reload state, 351 from the magazine's last shot to the next, where the
    // AK-47's is 110 and 126 in the same pattern). (After agent heavy's RPG-7 and FLAME THROWER rows: index
    // 10; the code names it W_M63 only)
    {"M63", 0x0D, 100, 351, 6, true, 70, 0x010A, 0, 0.f, 45000.f},
};
// (v150-host beta 22, heavy) the RPG-7, measured on a playable doll's own game (probe 46, runs/heavy_p2_rpg: its records, its
// rocket's actor, the blast's actor and the damage records it sent): in its hands 6 frames after R2 (the scoped rifles'), its
// sight up 20 frames after that (the state 0x183 at 19, 0x83 at 20 to 22); held in the sight its {08 01}; Square pulled, its
// {08 03} and the rocket 14 frames after the pull (the copies' rockets at the same moment: they start from the {08 03}), then
// its {08 05} four records; an empty tube reloaded by a pull, 145 frames (the rocket in it 90 to 133 frames in, heavy_p4_rpg).
// The rocket (its place every 4 frames, heavy_p4_rpg): about 100 mm a frame its first 14 frames (1.4 m), then about 800 (9.0 m
// in 21 to 29 frames, 10 m in 24, 16.4 m in 30); its blast where it meets a face, a thing or a player. Its records to each
// player in the blast (6.0 m from its centre to his body's point, his hips + 220 to 230 mm: 150 every time, at 0.1 to 5.97 m,
// on the virtual doll and on a playable doll alike, knocked down on its own game): {14
// shooter, 00 02, 00, his point less 2500 mm in x, his point less the blast} (each value (int16) mm / 10, 0x436520),
// {04 shooter, 150, 0, 0x11, 0, 0}, {09 shooter, 08, the angle from him to the blast} (knocked down: 08)
const int kRpgSightUp = 20, kRpgPull = 14, kRpgShow = 24, kRocketStart = 14, kRpgLoaded = 90;
const float kRocketRadius = 6000.f;              // (the blast's reach to a body's point: 5.97 m measured; the wound sphere 5000, 0x4F7F20)
const float kRocketSpeed = 800.f, kRocketSlow = 1400.f, kRocketRange = 120000.f;   // (mm a frame after its slow start; its flight at most)
const float kRpgSelf = 8000.f, kRpgFrom = 12000.f, kRpgKeep = 10000.f, kRpgGroupFrom = 9000.f;
const int kRocketDamage = 150, kBlastKind = 0x11, kKnockBlast = 0x08;
const float kBodyPointUp = 230.f, kBlastOffX = -2500.f;
const uint16_t kBlastFx = 0x0200;
// the flamethrower, measured the same way (runs/heavy_p3_flame: 12 contacts on the virtual doll and on the playable doll, 2.4 to
// 5.2 m): its records in its hands {13 00 09 02}, the pull {08 03}, {08 05} while Square is full, {08 03} light; the box's 999
// rounds not spent by its streams. A contact 33 to 94 frames after the stream began on him (56 the median), then every 51 to 57
// while it stays on him: {14 shooter, 04 02, 00, his point less 600 mm in x, his point less the flame's place}, {04 shooter, 95,
// 0, 0x86, 0, 0} (the damage table's 0x86, 95), {09 shooter, 1 or 2, an angle}; his own game then burns him (NewFireForChara,
// about a life every 10 frames, to 1 at least: a playable doll 200 -> 105 -> 5 -> 1). The doll's reach the farthest measured
const float kFlameReach = 5200.f, kFlameFrom = 4000.f, kFlameKeep = 5000.f, kFlameWidth = 400.f, kFlameSpread = 0.1f, kFlameOffX = -600.f;
const int kFlameDamage = 95, kFlameKind = 0x86, kFlameFirst = 56, kFlameEvery = 51;
// ((v150-host beta 25, reactfix) the fire that touches him, about his point: along the stream either way, aside (FireFlame, measured)
const float kFlameAlong = 950.f, kFlameAside = 400.f;
const uint16_t kFlameFx = 0x0204;

const int kWeaponCount = (int)(sizeof kWeapons / sizeof kWeapons[0]);
// (v150-host beta 21, geom) a bullet's power as the game's own fire routines give it: the handguns' and the MOSIN NAGANT's
// 50, the M37's 150, the others' 120 (the rifles, the SCORPION, the SVD). A bullet goes through a thing softer than its
// power; a face of the stage itself is 220: no weapon goes through one
int Power(const Weapon& w) { return w.code == 0x0E ? 150 : w.code == 0x05 || w.code == 0x06 || w.code == 0x10 ? 50 : 120; }
enum WeaponIx { W_AK47, W_XM16E1, W_SCORPION, W_M37, W_SVD, W_M1911, W_MK22, W_MOSIN, W_RPG7, W_FLAME, W_M63 };   // (v150-host beta 22: heavy's RPG-7 and flamethrower, pickup's M63)
int BoxRounds(int w) {                           // (a weapon box's count: the RPG-7 15, the flamethrower 999, the playable dolls' inventories)
    if (const char* e = getenv("MGO1_VDOLL_HEAVY_ROUNDS")) { const int n = atoi(e); if (n > 0) return std::min(30000, n); }
    return w == W_RPG7 ? 15 : 999;
}
// (v150-host beta 18) what the rule lets this doll carry: the soldiers' guns (as before), TEAM SNEAKING's sneakers' set
// (MOSIN NAGANT, Mk22, STUN G, the knife: tsne.h kTsneNonLethal, every class) or SNEAKING's Snake's own (his XM16E1, Mk22,
// MOSIN NAGANT, stun grenades: docs/PLAYABLE_DOLL.md, his inventory)
enum Role { R_SOLDIER, R_SNEAKER, R_SNAKE };
const char* const kRoleName[3] = {"soldier", "sneaker", "Snake"};
const int kStunNade = 0x15, kFragNade = 0x13;
enum Part { P_NONE = -1, P_HEAD = 4, P_BODY = 2, P_LEG = 0x0F };

// ---- the levels (1 to 10): reaction, aim error (a bullet's spread around the point aimed at, degrees), sight, field,
// the head's share of the aims, the view's turn, the bursts and their pauses, the aim's tolerance to fire, its settling
// on him before a fight's first shot (the full dolls' engage lines at level 6: their first shot 60 to 130 frames after
// they saw him) ----
struct Level { int react; float err, sees, field, head, turn; int burst, pause; float tol; int settle; };
const Level kLevel[10] = {
    {54, 7.0f, 16, 110, 0.00f, 3.f, 3, 40, 4.0f, 60},
    {38, 4.5f, 24, 130, 0.00f, 4.f, 3, 34, 3.5f, 52},
    {26, 2.5f, 32, 160, 0.00f, 5.f, 4, 28, 3.0f, 45},
    {16, 1.4f, 40, 190, 0.20f, 6.f, 4, 22, 2.5f, 38},
    { 9, 0.6f, 52, 220, 0.35f, 8.f, 5, 16, 2.0f, 32},
    { 8, 0.5f, 56, 230, 0.50f, 9.f, 5, 14, 1.7f, 26},
    { 7, 0.4f, 60, 240, 0.60f, 10.f, 6, 12, 1.4f, 20},
    { 6, 0.3f, 64, 250, 0.75f, 13.f, 6, 10, 1.2f, 12},
    { 4, 0.2f, 72, 260, 0.85f, 18.f, 8, 7, 1.0f, 6},
    { 3, 0.1f, 85, 280, 0.95f, 26.f, 10, 4, 0.7f, 2},     // (beta 15: a pro's flick, the head nearly always)
};

// ---- a doll's body ----
// a point of its way (beta 15: a drop or a climb starts at a point marked link, to the next one)
struct WayPt {
    float p[3];
    uint8_t area = 0;
    bool link = false;
    WayPt(float x, float y, float z, uint8_t a = 0, bool l = false) : p{x, y, z}, area(a), link(l) {}
    float& operator[](int i) { return p[i]; }
    const float& operator[](int i) const { return p[i]; }
};
struct Body {
    long long life = -2, last = -1, lifeAt = 0;
    int heldBack = 0, planFails = 0, noMesh = 0;
    int putOff = 0;                              // (beta 16) passes its new way has waited for the pass's way searches
    float copyPrev[3] = {0, 0, 0}, copyRan = 0;  // the host's copy at the last check, its run over the checks of a hold
    bool copyHave = false;
    int holds = 0, lates = 0;
    long long aliveF = 0, stillF = 0, fightF = 0;   // (its frames alive, still out of a fight, in a fight: the status)
    long long copyWaitF = 0;                     // (beta 15: still while its copy, moving, catches up: not still on the screens)
    bool waitCopy = false;
    std::string planWhy;                         // (MGO1_VDOLL_DEBUG) why its last way was not found
    int hp = -1, hurtBy = -1;                    // its life last seen, who hurt it last (seen whatever its field 2 s)
    long long hurtAt = -1000;
    int level = 5;
    bool live = false;                           // (v150-host beta 19) the live level on (vdoll_adapt.h): its level now lvf, its tier level
    float lvf = 5.f;
    int lvk = 0;
    // its way
    std::vector<WayPt> path;
    // (beta 15) a drop from a ledge (the position straight down, no style meanwhile, then 18 frames of landing) or a climb
    // onto a low wall (the position at once on top, no style 48 frames)
    long long fallAt = -1, landUntil = -1, climbUntil = -1;
    float fallTo[3] = {0, 0, 0}, fallVy = 0;
    // ((v150-host beta 25, fallflinch) a player's fall: its ticks so far by the clock, the edge's place and floor, the tick it lands at,
    // the frame it landed at)
    float fallT = 0, fallFrom[3] = {0, 0, 0}, fallY0 = 0; int fallN = 0; long long fallAt0 = -1, landAt = -1;
    bool landSaid = false;
    int drops = 0, climbs = 0;
    // (v150-host beta 24, snakehouse) a fence crossed with the action button (a vault link): from its near side to its far side
    // at a player's climb-over pace, its style walking, its facing as it goes (climbUntil holds it busy meanwhile)
    long long vaultAt = -1, vaultUntil = -1;
    float vaultFrom[3] = {0, 0, 0}, vaultTo[3] = {0, 0, 0};
    int vaults = 0;
    bool vaultSaid = true;                       // (MGO1_VDOLL_DEBUG: its copy's place told 30 frames after the crossing)
    size_t next = 0;
    long long pauseUntil = 0, planAt = 0;
    float speed = 0;                             // mm a frame now
    // ((v150-host beta 25, aimcost) its speed over the host's last frame, for its moves' cost (MoveCode): the speed of its last pass with
    // frames in it; its passes and those with no frame since the last (the debug count of its moves)
    float moveSpeed = 0;
    int passN = 0, pass0 = 0;
    // ((v150-host beta 25, dpass) the players' read it took last (Frame's gSeenAt), and whether this pass takes one (MGO1_VDOLL_ONE_PASS)
    long long readAt = -1000;
    bool readNow = false;
    float yaw = 0, pitch = 0;                    // its facing, its view's pitch (radians, over 0 down)
    float floorY = 0;
    // its fight
    int target = -1;
    long long seenAt = -1000, firstSeen = -1, nextShot = 0, burstEnd = -1000, reloadUntil = 0;
    int burstLeft = 0;
    // (beta 15) its weapons: the primary of this life, the one in its hands, each one's magazine; a draw, a reload
    int primary = W_AK47, held = W_AK47, mag[12] = {30, 20, 30, 4, 10, 7, 8, 5};   // (v150-host beta 22: room for the special weapons' rows, heavy's and pickup's; NewLife fills it)
    // (v150-host beta 18) its role in this round's rule, its handgun (the M1911A1, or the Mk22), its long gun for the far
    // fights (the sneaker's MOSIN NAGANT; -1 none), its grenades' code (frag 0x13, stun 0x15)
    int role = R_SOLDIER, secondary = W_M1911, longGun = -1, nade = kFragNade, soldierPrimary = W_AK47;
    int darts = 0, sleeps = 0;                   // (its darts that hit, the players it put to sleep)
    long long switchUntil = -1, reloadAt = -1;
    bool drawn = false, sent08 = false;
    bool resync = false;                         // (beta 15) up again: its place taken from the host's copy
    bool unarmed = false;                        // (beta 16) no gun the weapons setting allows (support only, knife only)
    // (beta 16, the weapons setting) its fists: the Circle records of the punch on its way, the target's hits so far; at him
    // (within 3 m: the knife in its hand rather than a grenade)
    int punchTicks = 0, punchHits = 0, punchTarget = -1;
    bool melee = false;
    // (beta 15) its stance in a fight: crouched (06 02, the eye 300 over the hips), leaning (12 v 4 right, 2 left: the eye
    // 490 mm aside); its roll (06 01, 36 frames along rollYaw)
    bool low = false;
    int lean = 0;
    long long rollUntil = -1, nextRollAt = 0;
    float rollYaw = 0;
    long long testRollAt = 0;                    // ((v150-host beta 25, rolltime) MGO1_VDOLL_TEST_ROLL: its next test roll, benches)
    float rollT = -1.f;                          // ((v150-host beta 25, rolltime) MGO1_VDOLL_ROLL_TIME: a player's ticks since its press, -1 beta 25's roll)
    long long rollAt = -1;                       // ((v150-host beta 25, rolltime) the frame of its last roll's press)
    float rollFrom[3] = {0, 0, 0}, rollMax = 0.f, rollRun = 3.f;   // ((v150-host beta 25, rolltime) where it pressed, the floor clear ahead then, its run's ticks)
    int rolls = 0, leans = 0, crouches = 0;
    long long standUntil = -1;                   // (up from a crouch: still 18 frames, its copy standing up before it runs)
    // (beta 21, geom) its crouched eye's two lines to its target (his chest, his head) asked of the game's things at most
    // every 6 frames while neither end moves 100 mm (a fight asks them every frame; a player's place is read every 6)
    struct ThingMemo { float a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; long long frame = -1000; bool stopped = false; };
    ThingMemo lowThing[2];
    int rollInto = -1, rollHits = 0;             // (a roll into an enemy: whom, the knock-downs it gave)
    // (beta 15) CQC: a rush at him, Circle from the grab's start (its M1911A1 drawn), the hold to grabAt + 32, his stun at
    // + 56, free at + 84 (the full dolls' grab and throw, measured)
    long long rushUntil = -1, rushPlanAt = 0, grabStart = -1, grabAt = -1, nextGrabAt = 0;
    int grabbed = -1, grabs = 0;
    bool grabSent = false, grabStun = false, grabOverSaid = false;
    long long grabStunAt = 56;                   // (v150-host beta 24, cqc: its stun's frame after the hold, by its character)
    int leanWant = 0;                            // (the side it sees its target from, 0 straight)
    // (beta 15) its grenades (2 a life): a throw's start, its view, its stage's 08 kinds (0x0D ready, 0x11 the throw, 0x0F
    // after); the blast 108 frames after the release, where its flight ends
    int grenades = 2, throws = 0, blastHits = 0;
    long long throwAt = -1, nextThrowAt = 0, blastAt = -1;
    float throwYaw = 0, throwPitch = 0, blast[3] = {0, 0, 0};
    int blastSlot = -1, blastTeam = -1;
    bool blastStun = false;                      // (v150-host beta 18) a stun grenade's blast: 12500 of the stamina, no life
    void (*blastSend)(void*, int, int, const std::vector<uint8_t>&) = nullptr;
    void* blastCtx = nullptr;
    // (beta 15) the objective: its way to it (planned again every second: the item, its carrier, a goal), why, its counts
    bool objWay = false, roundAbout = false, carrying = false;
    long long objAt = -1000;
    const char* objWhy = "";
    int objWays = 0, roundAbouts = 0, carries = 0;
    bool mustObj = false, holding = false;       // (in a base to take: always the objective, held there a moment)
    long long nearItemSince = -1;                // (by a free item the host does not give it: not its side's)
    int objItem = -1;                            // (v150-host beta 24, snakehouse) the free item ObjectiveGoal chose (its index)
    long long itemNoWay[4] = {-100000, -100000, -100000, -100000};   // (when its way to each item was refused: MGO1_VDOLL_ITEM_WAY)
    bool head = false;                           // this target's aim: the head (else the chest)
    float burstErr[2] = {0, 0};
    float tpos[3] = {0, 0, 0}, tvel[3] = {0, 0, 0};   // the target's hips when last seen, his run then (mm a frame)
    long long tposAt = 0, chaseAt = 0, onSince = -1;   // (onSince: its aim on him since, before a fight's first shot)
    long long hitAt = -1000;
    bool aiming = false, aimedLast = false, firstAim = false, shotSince = false;
    int16_t dev[2] = {0, 0};
    // (v150-host beta 16) its last place on the map (taken again if its place is ever no number or off the map), said once a
    // life; the round its grenade was thrown in
    float okPos[3] = {0, 0, 0};
    bool okHave = false, offSaid = false;
    bool boxOff = false;                 // this life began outside the collision's box (its spawn's place): the box not asked
    int blastGen = -1;
    // its records
    int recs = 0;
    // its counts
    int shots = 0, hits = 0, fightShots = 0, fightHits = 0, goals = 0, reached = 0, fights = 0;
    float ran = 0;
    // (v150-host beta 18) "as a player" (MGO1_VDOLL_CONTROLS=player, FightPlayer): its aim's state (Ctl), its end (the readied
    // flag, the view's entry), the view's turn on each axis (degrees a frame, its way), Square held since pullAt, the
    // recoil's kicks to come, its last bullet
    int ctl = 0;
    long long ctlUntil = -1, pullAt = -1, lastBullet = -1000;
    float viewV[2] = {0, 0};
    int viewSgn[2] = {0, 0};
    bool trigger = false;
    long long kickAt[4] = {-1, -1, -1, -1};
    float kick[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    // (v150-host beta 19) a scoped rifle (SVD, MOSIN NAGANT) as a player holds it (MGO1_VDOLL_SCOPE, on): the rifle in its
    // hands from raiseAt (the swap), its scope up from scopeAt; down again until lowerUntil (still meanwhile); its shots'
    // effect records held until the record that shows the shot has gone (Records)
    long long raiseAt = -1, scopeAt = -1, lowerUntil = -1;
    int scopeShots = 0, scopeRaises = 0, copyWaits = 0, scopeRecs = 0;   // (scopeRecs: its records sent with the scope up, this raise)
    struct Pending { int to, chan; std::vector<uint8_t> r; bool shown = false; int gate = 0; };   // (gate, beta 21: its hit judged when it goes)
    std::vector<Pending> pending;
    // (v150-host beta 21, the standoff: MGO1_VDOLL_STANDOFF, Engage) why it holds its fire this frame, its frames aimed with no
    // shot (the waits a player has too left out) and with an ally in its line, a move it makes instead (to moveUntil), its M37
    // closing in, a target it gave up (to ignoreUntil); its counts: frames held by reason, moves, M37 closings
    int hold = 0, ignore = -1;
    long long stallF = 0, allyF = 0, moveUntil = -1, ignoreUntil = -1, noWayAt = -1000;
    bool closing = false, stallSaid = false;
    long long holdF[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int unstalls = 0, closeIns = 0;
    // (its aim at a target with no shot and no move, as it is whatever the switch: the frames now, the longest of the game and
    // of the time since the last body line; tools/dev150/aimcheck.py reads them)
    long long aimStill = 0, aimStillMax = 0, aimStillLap = 0;
    // (v150-host beta 21, spin) its body's turn as a pad turns a player (MGO1_VDOLL_TURN, on): the facing of its last record,
    // the way round of the wide turn under way (0 none), its goal, the other way round taken; its way a short one
    float sentYaw = 0;
    int turnSgn = 0;
    float turnWant = 0;
    bool turnLong = false;
    bool shortWay = false;
    int shortRun = 0;                            // (its short ways in a row)
    // its turns of the last 96 frames (degrees a frame, signed: Spun), its facing at the last pass; its last shot's frame
    float spun[96] = {}, yawWas = 0;
    int spunAt = 0;
    long long shotAt = -1000;
    // (v150-host beta 21, cover) its sight of this target on the machine's clock (real ms), its hits not counted (the part out
    // of its line where he really is; coverAtRec of them at the place he last sent), its first shots held for the reflex;
    // its hits whose records wait (their shot's eye, part and target)
    unsigned long long sightMs = 0;
    int coverDrops = 0, coverAtRec = 0, reflexHolds = 0;
    long long tickAt = 0;                        // (its last record tick's frame)
    struct Gate { int id = 0, target = -1, part = 0, left = 2, stance = 0; bool ok = true, late = false; float eye[3] = {0, 0, 0}, at = 0; const char* weapon = ""; };   // (late: fired after its hold's second, its copy not shown)
    std::vector<Gate> gates;
    int gateIds = 0;
    bool coverNow = false;
    long long shownRead = -1, shownSince = -1;   // (MGO1_VDOLL_SHOWN: its copy in this target's line since, asked at each read)
    unsigned long long shownFrom = 0;            // (its hold for it began, real ms; 0: none in this fight)
    int shownHolds = 0, shownLate = 0;           // (frames held; fights fired after the second with its copy still hidden)
    // (v150-host beta 24, walls3) MGO1_VDOLL_MUZZLE: its third person bullets met by a cover before its gun, the fights it took its
    // view for that; its muzzle's line to its target last asked (frame, answer, the fight it was asked in)
    int muzzleStops = 0, muzzleViews = 0, muzzleFight = -1;
    long long muzzleAt = -1000;
    bool muzzleHid = false;
    unsigned long long killHoldUntil = 0;        // (MGO1_VDOLL_KILL_HOLD: where it shot from until then, real ms)
    long long killAt = -1000;
    int killHolds = 0;
    // (v150-host beta 22, heavy) its special weapon of this life (the RPG-7 or the flamethrower, as a weapon box gives it: Give, or
    // the bench's MGO1_VDOLL_WEAPONS), its rounds left (the one in the tube or the fuel included); its rocket in flight (the
    // blast at rocketAt, where it ends, the player it met; made in its round, by its slot and team); its counts
    int heavyW = -1, heavyLeft = 0;                // (W_RPG7, W_FLAME, -1 none: pickup's own special, the M63, is apart)
    bool heavyBench = false;                     // (given by the bench's MGO1_VDOLL_WEAPONS, no box taken: not dropped at its death)
    long long rocketAt = -1;
    float rocketEnd[3] = {0, 0, 0}, rocketFrom[3] = {0, 0, 0};
    float rocketDir[3] = {0, 0, 1}, rocketWall = 0.f, rocketDone = 0.f;   // (its line, where a face or a thing stops it, how far it has flown: RocketStep)
    long long rocketLaunch = -1;
    int rocketHit = -1, rocketSlot = -1, rocketTeam = -1, rocketGen = -1;
    void (*rocketSend)(void*, int, int, const std::vector<uint8_t>&) = nullptr;
    void* rocketCtx = nullptr;
    int rockets = 0, rocketHits = 0, flames = 0, flameHits = 0, heavyHolds = 0;
    bool heavyHeld = false;                      // (its last shot not fired: its rocket would end at its feet or by a teammate)
    long long rpgPullAt = -1, rpgShowUntil = -1;  // (the RPG-7's pull: its rocket leaves at rpgPullAt; its shot shown until rpgShowUntil)
    float rpgDev[2] = {0, 0};                    // (its spread drawn at the pull)
    long long rpgSafeAt = -1000;                 // (HeavyWant: its rocket's line to him last asked, its answer)
    long long flameOn[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // (each player in its flame since, -1 not)
    long long flameNext[16] = {}, flameSeen[16] = {};
    bool rpgSafe = false;
    int rpgSaid = -1;                            // (MGO1_VDOLL_DEBUG: the fight whose refused rocket it told)
    // (v150-host beta 22, book) the book it reads (its id, 0 none; its place), since when, the end of its cooldown (real ms), its
    // readings in all
    uint32_t bookId = 0;
    float bookAt[3] = {0, 0, 0};
    unsigned long long bookSince = 0, bookUntil = 0, bookReadyMs = 0;
    int bookReads = 0;
    int bookRecs = 0;                            // (v150-host beta 22, bookplay) its reading's record ticks sent (0: its long record next)
    long long bookEndF = -1;                     // (v150-host beta 22, bookplay) the frame its reading ended (no look record in it)
    uint32_t bookClassSaid = 0;                  // (v150-host beta 22, bookplay2) the book its class refusal was said for
    float bookVault = -1.f;                      // (v150-host beta 25, bookhold) a reading begun on a fence: the crossing's share done then (-1 none)
    // (v150-host beta 22, pickup: MGO1_VDOLL_PICKUP, docs/VIRTUAL_DOLL.md 32) a special item taken where it lay, for this life:
    // its weapon (-1 none) and its rounds left (the magazine in), the thermal goggles had, worn now, their battery (the stealth
    // camouflage's, 300 full); the record it goes to (its number, -1 none) since when; its take's second record and when it goes
    int special = -1, specialRounds = 0;
    int carryCode = 0, carryRounds = 0;          // (the RPG-7 or the flamethrower without agent heavy's body, MGO1_VDOLL_PICKUP=2: carried, dropped at its death)
    bool goggles = false, gogglesOn = false;
    float battery = 300.f;
    long long gogglesAt = -1, gogglesSeen = -100000;
    int pickNum = -1;
    long long pickSince = -1, takeAt = -1;
    float pickPos[3] = {0, 0, 0};
    std::vector<uint8_t> takeSecond;
    long long specialHitAt = -100000;            // (its last hit with the special weapon: a kill by it)
    int specialHitW = -1;                        // (pickup2: that weapon; its M63 spent, another special taken: the kill named right)
    int takes = 0, specialKills = 0;             // (its counts over its lives: the specials it took, the kills with them)
    // (v150-host beta 22, blast) a stun grenade's white: nothing seen until blindUntil (the game's white over 40 of 128); its
    // flashes, its frames blind
    long long blindUntil = -1;
    long long avertUntil = -1;                   // (beta 22, blast: turned away from its own stun grenade till then: Avert)
    int flashes = 0;
    long long blindF = 0;
    int slot = -1;                               // (its player's slot, told by Alive: the flash's eye from its own place)
    // (v150-host beta 22, wake: MGO1_VDOLL_WAKE, docs/VIRTUAL_DOLL.md 36) a teammate it wakes: his seat (-1 none), why (1 knocked
    // out, 2 reading), since; the combo under way (its press's frame, -1 none), its presses and the blows landed in it, the
    // combos given him; the last frame it perceived an enemy; its counts over its lives (blows given, teammates up or freed)
    int wakeTo = -1, wakeWhy = 0, wakePress = 0, wakeLanded = 0, wakeCombos = 0;
    long long wakeSince = -1, wakeCombo = -1, wakePlanAt = 0, wakeLookAt = 0, wakeDoneAt = -1, foeAt = -100000;
    bool wakeDone = false;                       // (its last blow did it by the game's rule: no more presses)
    int wakeGauge = -1;                          // (his gauge by its combo's blows: his copy's at the combo's start, each ally wake added)
    long long wakeRunSince = -1;                 // (running since: the combo on a teammate lying is pressed running into him)
    bool wakeRunUp = false;                      // (going back 3 m from him to run at him again)
    long long wakeCutAt = -100000;               // ((v150-host beta 23) its straight step at him cut short then: on by its way)
    long long wakeFastAt = -100000;              // ((v150-host beta 23) its last frame at a run's pace)
    int wakeSent = 0;                            // ((v150-host beta 23) the blows of this combo already sent: bit i, the throw's hit bit 3)
    float wakeSpot[3] = {0, 0, 0};               // (its run-up's place)
    long long wakeRoundFor = -1;                 // ((v150-host beta 25, wakeway) the waking (its wakeSince) whose way round to his side was counted)
    int wakeMode = 0;                            // (WM_COMBO the punches, WM_THROW the CQC throw)
    long long wakeSkip[16] = {0};                // (a teammate it found no way to: not looked at again until then)
    int wakeBlows = 0, woken = 0;
    // (v150-host beta 25, food: MGO1_VDOLL_FOOD, docs/VIRTUAL_DOLL.md 44) the food it goes to (its entry's number, -1 none) since
    // when; the stun gauge its last eating gave back, for vdoll.cpp to add to its own (FoodEaten); its counts over its lives
    int foodNum = -1;
    long long foodSince = -1;
    int foodGain = 0, eats = 0, foodGains = 0;
};
// (v150-host beta 22, pickup) the counts of the game, all dolls: the specials taken (by kind: the M63, the RPG-7, the
// flamethrower, the goggles), the kills made with them, the specials dropped at a death, the goggles' frames worn
struct PickCounts { int taken[4] = {0, 0, 0, 0}, kills = 0, dropped = 0; long long gogglesF = 0; };
PickCounts gPick;
static_assert(kWeaponCount <= (int)(sizeof(Body::mag) / sizeof(int)), "Body::mag holds every weapon's magazine");   // (review22b P11)
std::vector<Body> gBodies;
Body& BodyOf(int k) {
    if ((int)gBodies.size() <= k) gBodies.resize(k + 1);
    return gBodies[k];
}
int LevelOf(int k) { return gLevels.empty() ? 5 : gLevels[std::min((size_t)k, gLevels.size() - 1)]; }
// (v150-host beta 19) the live level (MGO1_VDOLL_ADAPT=1, vdoll.cpp, docs/VIRTUAL_DOLL.md 19): each doll's level now (its offset
// in, within the bounds) and its part against each player; off: its level set, the table's row as before
struct LiveNow { bool on = false; float level = 5.f; int lo = 1, hi = 10; float foe[16] = {}; };
LiveNow gLive[16];
void LevelNow(Body& b, int k) {
    const LiveNow& v = gLive[k & 15];
    b.live = v.on && k < 16;
    if (!b.live) { b.level = LevelOf(k); return; }
    b.lvf = v.level; b.lvk = k & 15;
    b.level = adapt::Tier(v.level);                       // (what switches at a tier: the whole level, 5.5 and up is 6)
}
// its level's row: the table's (off), or between two rows (on), against its target with its part against him (foe)
Level RowOf(const Body& b, bool foe) {
    if (!b.live) return kLevel[b.level - 1];
    float lv = b.lvf;
    const LiveNow& v = gLive[b.lvk];
    if (foe && b.target >= 0 && b.target < 16) lv = std::max((float)v.lo, std::min((float)v.hi, lv + v.foe[b.target]));
    int i, j; float t;
    adapt::Between(lv, &i, &j, &t);
    const Level &x = kLevel[i], &y = kLevel[j];
    return Level{adapt::MixI(x.react, y.react, t), adapt::MixF(x.err, y.err, t), adapt::MixF(x.sees, y.sees, t), adapt::MixF(x.field, y.field, t),
                 adapt::MixF(x.head, y.head, t), adapt::MixF(x.turn, y.turn, t), adapt::MixI(x.burst, y.burst, t), adapt::MixI(x.pause, y.pause, t),
                 adapt::MixF(x.tol, y.tol, t), adapt::MixI(x.settle, y.settle, t)};
}
// ---- (v150-host beta 19) the scoped rifles as a player's controls allow them (measured on playable dolls' own games, probe
// 10 with MGO1_DOLL_TRIG_WALK and _SWAPTEST, the other games' copies by MGO1_DOLL_WATCH: docs/VIRTUAL_DOLL.md 20). With the
// SVD or the MOSIN NAGANT in his hands a player does not move (the stick pushed full 120 frames: 0 mm); the rifle comes to
// his hands 6 frames after R2, its scope is up 20 to 23 frames after that (the state 0x83), down 12 frames after R2; he
// runs with another weapon in his hands. A shot from the scope is {08 05} with the long {12} record (the eye, the view): the
// other games make a muzzle flash and a sniper's bullet (NewSniperBullet) at once. MGO1_VDOLL_SCOPE=0: as beta 18 (the rifle
// carried while it runs, a shot as soon as its aim is on, the hit's records sent before the shot's) ----
bool gScope = true;
// (v150-host beta 21) the standoff's rules on (MGO1_VDOLL_STANDOFF; 0: beta 20's): an ally beside it no longer holds its
// fire, its M37 closes in, a doll aimed a while with no shot moves (Engage)
bool gStandoff = true;
// (v150-host beta 24, cqcfight) its teammate in a close fight with its enemy (MGO1_VDOLL_CQCFIGHT; 0: beta 23's, docs/VIRTUAL_DOLL.md 42):
// a teammate held fire only where his own body (grown by its aim's spread) is on its line of fire, a bullet stops at a teammate's body,
// no grab at a crouched or lying player, a target fighting its teammate never given up, no wake of a teammate an enemy stands at
bool gCqcFight = true;
struct CqcFightCount { long long freed = 0; int stops = 0, kept = 0, noWake = 0, noGrab = 0, over = 0, nades = 0, left = 0; };   // (by doll: asks the old rule alone
CqcFightCount gCqcFightCount[16];                  // held, bullets stopped, targets kept, wakes not begun, grabs not tried; (cqcfight2) ways over a fence, grenades, left)
// ((v150-host beta 24, cqcfight2) the guard's rounds kept on a target that fights its teammate, in this fight (its key: its life, its fight)
int gKeptRounds[16]; long long gKeptKey[16];
bool gTurn = true;                               // (v150-host beta 21, spin: MGO1_VDOLL_TURN, TurnBody below)
// (v150-host beta 22, book: MGO1_VDOLL_BOOKS, BookFrame below) the books lying in the host's game, read each frame
bool gBooks = true;
vbook::Book gBookList[16];
int gBookN = 0;
// (v150-host beta 22, bookplay: MGO1_VDOLL_BOOK_POSE, Records below) its reading told to the other games as a reader's own game
// tells it (vdoll_book.h): its copy then reads on every screen, crouched over the book after its look, up again at the end
bool gBookPose = true;
// (v150-host beta 25, bookhold: MGO1_VDOLL_BOOK_HOLD, BookHeld below) the moves that hold a book back as a player's own game holds it
// (probe 52 on real clients); 0: beta 22's holds (a drop, its landing, a climb's 60 frames, a fence crossed)
bool gBookHold = true;
std::vector<int> gBookPlaceEvery;                // (tests: MGO1_VDOLL_TEST_BOOK_PLACE "n,m,...": a doll's place record each n-th tick while it reads)
// (v150-host beta 22, bookplay) a reader's game sends his place about every 20 frames while he reads (book_r4: 12 to 33 frames
// apart, none in the datagram of his long {0E} record): the doll's every 3rd record tick from the one after its long record
const int kBookPlaceTicks = 3;
bool gPace = true;                               // (v150-host beta 21, spin2: MGO1_VDOLL_PACE, GamePace below)
bool gClockFix = true;                           // ((v150-host beta 25, clockfix) MGO1_VDOLL_CLOCK_FIX: the frames' real ms (gMsAFrame) weighed by frames)
// (v150-host beta 25, aimcost: MGO1_VDOLL_MOVE_FRAME, MoveCode below) its moves' cost read from its speed over the host's whole frame
// (0: from its last pass's, beta 25's: half its run's ticks taken for still ones)
bool gMoveFrame = true;
// (v150-host beta 25, dpass: MGO1_VDOLL_ONE_PASS, Alive and TurnBody below) its every-100-ms decisions once a read of the players, in its
// first pass of the read's frame, and no turn in a pass with no frame since the last. The host's game runs its actor lists twice for a
// tick (docs/FPS60.md) and the dolls' tick comes at the start of each: most host frames ran Alive twice, the second pass with no frame
// since, and the decisions ran in both (the roll's 35 % a look drawn twice: 58 %; HeldBack's 3 reads counted in 2), TurnBody a frame's
// turn in the second (0: beta 25's)
bool gOnePass = true;
// ((v150-host beta 25, dpass) by doll, whatever its lives: its passes, the reads it took, what its decisions did (MGO1_VDOLL_DEBUG: the
// line every 3600 host frames, DecLine; the tests: Decisions)
struct DecCount {
    long long passes = 0, pass0 = 0, reads = 0, readsAgain = 0, blocks = 0, blocks0 = 0, held = 0, heldFired = 0, perceive = 0;
    long long rollLooks = 0, rolls = 0, grabLooks = 0, grabsBegun = 0, intoLooks = 0, intos = 0, rushLooks = 0, rushes = 0;
    long long throwLooks = 0, throwTries = 0, supLooks = 0, supTries = 0, supBegun = 0, recs = 0, recs0 = 0;
    double turn = 0, turn0 = 0;                   // (degrees turned in its passes; in those with no frame since the last)
    int lastRollHits = 0, lastGrabs = 0, lastThrows = 0;   // (the body's own counts at the last line)
};
DecCount gDec[16], gDecLap[16];
inline bool Drew(long long& n) { ++n; return true; }   // ((dpass) a chance about to be drawn, counted)
const int kSwapHands = 6, kScopeUp = 22, kScopeDown = 12;
// (the owner, 13:42: "the SVD, the MOSIN NAGANT and the RPG-7 are FPV weapons") the first person only weapons by the game's
// code, so that a weapon added to the table later is held so too: the RPG-7 (0x11) is in no doll's table today (a player
// takes it from a weapon box; a virtual doll takes nothing from the map)
const int kFpvOnly[] = {0x0F, 0x10, 0x11};
bool FpvCode(int code) { for (int c : kFpvOnly) if (c == code) return true; return false; }
bool Scoped(int w) { return w >= 0 && w < kWeaponCount && FpvCode(kWeapons[w].code); }
// its rifle up (in its hands, in a fight), its scope's view on
bool RifleUp(const Body& b) { return gScope && Scoped(b.held) && b.raiseAt >= 0 && gFrame >= b.raiseAt; }
bool ScopeOn(const Body& b) { return RifleUp(b) && gFrame >= b.scopeAt; }
// its rifle keeps it still: up, coming down, or its reload (a player reloads it in his hands, where he stands)
bool RifleHolds(const Body& b) { return gScope && Scoped(b.held) && (b.raiseAt >= 0 || gFrame < b.lowerUntil || gFrame < b.reloadUntil || b.rpgPullAt >= 0 || gFrame < b.rpgShowUntil); }   // (v150-host beta 22: the RPG-7's pull and shot too)
// the rifle to its hands for a fight (it stands from now on): in its hands after the swap, the scope up after that
void RaiseRifle(Body& b) {
    if (b.raiseAt >= 0) return;
    // (its opening measured from the rifle in the hands to the state 0x83: the SVD 20 to 23 frames, the MOSIN NAGANT 18
    // (svd_mosin: 0x183 at +16, 0x83 at +18); the RPG-7, in no doll's hands, the SVD's)
    const int up = kWeapons[b.held].code == 0x10 ? 18 : kWeapons[b.held].code == 0x11 ? kRpgSightUp : kScopeUp;   // (v150-host beta 22, heavy: the RPG-7's sight)
    b.raiseAt = std::max(gFrame, b.lowerUntil) + kSwapHands; b.scopeAt = b.raiseAt + up; ++b.scopeRaises; b.scopeRecs = 0;
}
// out of the fight: the rifle down (another weapon in its hands kSwapHands frames later); its reload keeps it up
void LowerRifle(Body& b) {
    if (b.raiseAt < 0 || gFrame < b.reloadUntil || b.rpgPullAt >= 0) return;   // (v150-host beta 22: the RPG-7's pull goes to its end)
    if (gFrame >= b.raiseAt) b.lowerUntil = gFrame + (gFrame >= b.scopeAt ? kScopeDown : kSwapHands);   // (its scope's view off 12 frames after R2)
    b.raiseAt = b.scopeAt = -1;
}
// its copy in the host's game caught up (the other games' copies follow the same records): a scoped rifle's shot waits for
// it, 45 frames after the scope at most (svd_a_d10: a copy 1.3 m behind on stairs still ran 28 frames after the doll
// stopped, and showed no shot). Caught up: within 0.5 m, or still (a copy the game holds 1.1 m off, svd_a3_d10)
const int kCopyWaitMax = 45;
bool CopyAtRest(Body& b, const Self& s) {
    if (!gHave[s.slot] || !gSeen[s.slot].object || gFrame - b.scopeAt >= kCopyWaitMax) return true;
    const PlayerNow& me = gSeen[s.slot];
    const bool rest = (Dist2D(me.pos, s.pos) < 500.f && std::fabs(me.pos[1] - s.pos[1]) < 700.f) || std::hypot(gVel[s.slot][0], gVel[s.slot][2]) < 5.f;
    if (!rest && ++b.copyWaits % 60 == 1 && gDebug)
        BLOG("vdoll %d: its shot waits for its copy: %.2f m from it (height %.2f m), moving %.1f mm a frame", s.k + 1, Dist2D(me.pos, s.pos) / 1000.f,
             (me.pos[1] - s.pos[1]) / 1000.f, std::hypot(gVel[s.slot][0], gVel[s.slot][2]));
    return rest;
}
// a record of a shot's effect (the hit, the dart): with the scope rule on, sent after the record that shows its shot
void SendHit(Body& b, const Self& s, int to, int chan, const std::vector<uint8_t>& r) {
    if (gScope) b.pending.push_back({to, chan, r}); else s.send(s.ctx, to, chan, r);
}
// its primary for a life: its setting, or (auto) one by chance: the AK-47 35 %, the XM16E1 20 %, the SCORPION, the M37, the SVD 15 %
int WeaponOf(int k) {
    if (const char* t = getenv("MGO1_VDOLL_FPV_TEST")) if (!strcmp(t, "mosin")) return W_MOSIN;   // (v150-host beta 19, tests only: a soldier with the MOSIN NAGANT)
    const int w = gWeapons.empty() ? -1 : gWeapons[std::min((size_t)k, gWeapons.size() - 1)];
    if (w >= 0) return w;
    const float r = Rnd01();
    return r < 0.35f ? W_AK47 : r < 0.55f ? W_XM16E1 : r < 0.70f ? W_SCORPION : r < 0.85f ? W_M37 : W_SVD;
}
// (v150-host beta 22, heavy) the rounds a weapon box gives with the RPG-7 and the flamethrower (MGO1_VDOLL_HEAVY_ROUNDS: the bench's)
int BoxRounds(int w);
// the bench's weapon box (MGO1_VDOLL_WEAPONS "+rpg7", "+flamethrower") at each life; no special weapon else (the boxes' own
// weapons come with Give, pickup's take of a box). MGO1_VDOLL_HEAVY=0: none, as beta 21
void GiveSpecial(Body& b, int k) {
    b.heavyW = -1; b.heavyLeft = 0;
    const int w = gSpecials.empty() ? -1 : gSpecials[std::min((size_t)k, gSpecials.size() - 1)];
    if (!gHeavy || w < 0) return;
    b.heavyW = w; b.heavyLeft = BoxRounds(w); b.heavyBench = true;
}

void Feet(const float* hips, float* out) { out[0] = hips[0]; out[1] = hips[1] - kHips; out[2] = hips[2]; }
// (v150-host beta 16) a place the game can hold a player at: numbers, inside the map's collision (its faces' box, 2 m
// around and 3 m over it); with no mesh yet, numbers within 1 km
bool OnMap(const float* hips) {
    for (int k = 0; k < 3; ++k) if (!(std::fabs(hips[k]) < 1e6f)) return false;
    if (!gMap || gMap->state != 2 || !gMap->geom) return true;
    const doll::Geom& g = *gMap->geom;
    return hips[0] > g.bmin[0] - 2000.f && hips[0] < g.bmax[0] + 2000.f && hips[2] > g.bmin[2] - 2000.f && hips[2] < g.bmax[2] + 2000.f &&
           hips[1] > g.bmin[1] - 1000.f && hips[1] < g.bmax[1] + 3000.f;
}
// (the review's P1-5) the same for a body: a life that began outside the box (a spawn point the collision's faces do not
// reach) is asked for numbers only, or it would be kept at its first place, or told nowhere, to its death
bool Number(const float* p) { return std::fabs(p[0]) < 1e6f && std::fabs(p[1]) < 1e6f && std::fabs(p[2]) < 1e6f; }
// the round being played (its end's screens and the next one's coming: no fight, as a player's game then)
bool RoundOver() { const int ph = gHost.phase ? gHost.phase() : -1; return ph == 4 || ph == 5; }
// the floor under a point of the mesh (the map's collision: the stairs' steps, a floor's own height)
float FloorAt(float x, float meshY, float z) {
    const float p[3] = {x, meshY + 350.f, z};
    const float g = gMap->nav->GroundBelow(p, 1200.f);
    return g > -1e29f ? g : meshY;
}
// the places where the game held a doll back (a prop of the stage out of the map's collision, as City Under Siege's
// cardboard box): the ways keep 1.2 m off them on this map; from by one, a way does not head toward it. (v150-host beta 14)
// kept once held there twice: a copy stopped a moment elsewhere (15 virtual dolls: 121 of 160 places held once) is not one
std::vector<std::array<float, 3>> gHeld, gHeldOnce;
// (beta 15) a drop from a ledge and a climb onto a wall up to 1 m are its moves too
bool LinkOk(const std::vector<doll::Waypoint>& w, size_t i) {
    const auto& p = w[i];
    if (!p.link) return p.area == doll::AREA_STAND || p.area == 0;
    if (i + 1 >= w.size()) return false;
    if (p.area == doll::AREA_DROP) return true;
    // (v150-host beta 24, snakehouse) a fence crossed onto a floor 0.3 m higher at most: the host's game held the copy back at the
    // one of 0.55 m up on n021a (57954 1050 -54222, 5 tries of 5, snakehouse_t4v) and let it over every other (0.11 m up at most)
    if (p.area == doll::AREA_VAULT) return gVault && !gFightWay && w[i + 1].p[1] - p.p[1] <= kVaultRise;
    return p.area == doll::AREA_CLIMB && w[i + 1].p[1] - p.p[1] <= 1000.f;
}
// (v150-host beta 22, support) the doll in hand's places to keep off: its team's claymores and books, the enemies' claymores it saw
// (gAvoidWays, set by Alive; vdoll_support.h kKeepOff): no way within 1.6 m of one (its start may be: it walks out of it)
const std::vector<std::array<float, 3>>* gAvoidWays = nullptr;
bool AvoidOk(const std::vector<doll::Waypoint>& w) {
    if (!gAvoidWays || gAvoidWays->empty() || w.size() < 2) return true;
    for (const auto& h : *gAvoidWays) {
        const float dh = std::hypot(h[0] - w[0].p[0], h[2] - w[0].p[2]);
        if (dh < 1600.f) {                             // (by one already, its own just laid: a way out of it, not over it)
            const float ax = w[1].p[0] - w[0].p[0], az = w[1].p[2] - w[0].p[2], al = std::hypot(ax, az);
            if (al > 1.f && dh > 1.f && (ax * (h[0] - w[0].p[0]) + az * (h[2] - w[0].p[2])) / (al * dh) > 0.3f) return false;
            continue;
        }
        for (size_t i = 1; i < w.size(); ++i) {
            const float* a = w[i - 1].p;
            const float* c = w[i].p;
            const float ux = c[0] - a[0], uz = c[2] - a[2], l2 = ux * ux + uz * uz;
            float t = l2 > 1.f ? ((h[0] - a[0]) * ux + (h[2] - a[2]) * uz) / l2 : 0.f;
            t = std::max(0.f, std::min(1.f, t));
            if (std::hypot(a[0] + ux * t - h[0], a[2] + uz * t - h[2]) < 1600.f && std::fabs(a[1] + (c[1] - a[1]) * t - h[1]) < 1500.f) return false;
        }
    }
    return true;
}
bool WayOk(const std::vector<doll::Waypoint>& w, bool anyHeld = false) {
    for (size_t i = 0; i < w.size(); ++i) if (!LinkOk(w, i)) return false;
    if (!AvoidOk(w)) return false;                     // (v150-host beta 22, support)
    if (anyHeld) return true;
    const float* s0 = w[0].p;
    for (const auto& h : gHeld) {
        const float dh = std::hypot(h[0] - s0[0], h[2] - s0[2]);
        if (dh < 1600.f) {
            const float ax = w[1].p[0] - s0[0], az = w[1].p[2] - s0[2], al = std::hypot(ax, az);
            if (al > 1.f && dh > 1.f && (ax * (h[0] - s0[0]) + az * (h[2] - s0[2])) / (al * dh) > 0.5f) return false;
            continue;
        }
        for (size_t i = 1; i < w.size(); ++i) {
            const float* a = w[i - 1].p;
            const float* c = w[i].p;
            const float ux = c[0] - a[0], uz = c[2] - a[2], l2 = ux * ux + uz * uz;
            float t = l2 > 1.f ? ((h[0] - a[0]) * ux + (h[2] - a[2]) * uz) / l2 : 0.f;
            t = std::max(0.f, std::min(1.f, t));
            if (std::hypot(a[0] + ux * t - h[0], a[2] + uz * t - h[2]) < 1200.f && std::fabs(a[1] + (c[1] - a[1]) * t - h[1]) < 1500.f) return false;
        }
    }
    return true;
}
// (v150-host beta 24, snakehouse) a way's stairs of blocks (a link of AREA_STEPS: doll_nav.cpp FindStairs, blocks whose tops the
// mesh leaves out, each step 0.35 to 1.2 m) as a player takes them with the action button: a climb at each block's edge onto its
// top (up), a drop off it (down), its top walked across between two, as the body's other climbs (1 m at most: LinkOk) and drops.
// The edges between two points of the stairs: along the line from one to the next, each place where the stage's floor jumps
// (0.2 m or more between two places 50 mm apart: a block's side; a ramp's floor never jumps so), the climb or the drop from
// 0.3 m before it to 0.3 m past it (less between two edges closer than 0.6 m), at the floors' own heights. A line with no edge,
// or with no floor under a place of it, leaves the link as it was: LinkOk refuses it as before; so does a step over 1 m.
// MGO1_VDOLL_STAIRS=0: every link as it was (beta 23: a stairs link never run). The stairs' blocks come from the mesh
// (NavMesh::AllSteps)
const float kEdgeStep = 50.f, kEdgeOff = 300.f, kEdgeJump = 200.f;
bool StairEdges(const doll::NavMesh& nav, const float* p, const float* q, std::vector<doll::Waypoint>& out) {
    const float dx = q[0] - p[0], dz = q[2] - p[2], l = std::hypot(dx, dz);
    if (l < 2.f * kEdgeStep) return false;
    const float ux = dx / l, uz = dz / l, top = std::max(p[1], q[1]) + 300.f, down = std::fabs(q[1] - p[1]) + 900.f;
    auto floorAt = [&](float t) { const float a[3] = {p[0] + ux * t, top, p[2] + uz * t}; return nav.GroundBelow(a, down); };
    std::vector<float> edges;
    float last = floorAt(0.f);
    if (last < -1e29f) return false;
    for (float t = kEdgeStep; t <= l + 1.f; t += kEdgeStep) {
        const float g = floorAt(std::min(t, l));
        if (g < -1e29f) return false;
        if (std::fabs(g - last) >= kEdgeJump) {
            if (!edges.empty() && t - kEdgeStep * 0.5f - edges.back() < 150.f) edges.back() = (edges.back() + t - kEdgeStep * 0.5f) * 0.5f;   // (one side met twice)
            else edges.push_back(t - kEdgeStep * 0.5f);
        }
        last = g;
    }
    if (edges.empty()) return false;
    for (size_t k = 0; k < edges.size(); ++k) {
        const float before = k ? (edges[k - 1] + edges[k]) * 0.5f : 0.f, after = k + 1 < edges.size() ? (edges[k] + edges[k + 1]) * 0.5f : l;
        const float t0 = std::max(before, edges[k] - kEdgeOff), t1 = std::min(after, edges[k] + kEdgeOff);
        doll::Waypoint a{}, b{};
        a.p[0] = p[0] + ux * t0; a.p[2] = p[2] + uz * t0; a.p[1] = floorAt(t0);
        b.p[0] = p[0] + ux * t1; b.p[2] = p[2] + uz * t1; b.p[1] = floorAt(t1);
        if (a.p[1] < -1e29f || b.p[1] < -1e29f) return false;
        a.area = b.p[1] > a.p[1] ? doll::AREA_CLIMB : doll::AREA_DROP; a.link = true;
        b.area = doll::AREA_STAND; b.link = false;
        out.push_back(a); out.push_back(b);
    }
    return true;
}
void StairsAsClimbsOn(const doll::NavMesh& nav, std::vector<doll::Waypoint>& w) {
    for (size_t i = 0; i + 1 < w.size(); ++i) {
        if (!w[i].link || w[i].area != doll::AREA_STEPS) continue;
        // (the stairs whose two ends are nearest the link's: NavMesh::StepsAt takes the first within 0.4 m, and stairs of one
        // block and of two from the same floor end 0.3 m apart: n022a's second sunken house)
        bool rev = false;
        const doll::NavMesh::Steps* st = nullptr;
        float best = 800.f;
        for (const auto& c : nav.AllSteps()) {
            const float fw = Dist3(c.a, w[i].p) + Dist3(c.b, w[i + 1].p), bw = Dist3(c.b, w[i].p) + Dist3(c.a, w[i + 1].p);
            if (fw < best) { best = fw; st = &c; rev = false; }
            if (bw < best) { best = bw; st = &c; rev = true; }
        }
        if (!st || st->stones.empty()) continue;
        std::vector<std::array<float, 3>> chain;
        chain.push_back({w[i].p[0], w[i].p[1], w[i].p[2]});
        if (rev) for (size_t k = st->stones.size(); k-- > 0;) chain.push_back(st->stones[k]);
        else for (const auto& sn : st->stones) chain.push_back(sn);
        chain.push_back({w[i + 1].p[0], w[i + 1].p[1], w[i + 1].p[2]});
        std::vector<doll::Waypoint> mid;
        bool ok = true;
        for (size_t k = 0; k + 1 < chain.size() && ok; ++k) ok = StairEdges(nav, chain[k].data(), chain[k + 1].data(), mid);
        if (!ok) continue;
        w[i].link = false; w[i].area = doll::AREA_STAND;
        w.insert(w.begin() + (long)i + 1, mid.begin(), mid.end());
        i += mid.size();
    }
}
void StairsAsClimbs(std::vector<doll::Waypoint>& w) { if (gStairs) StairsAsClimbsOn(*gMap->nav, w); }
// the mesh's point a way starts from: under the doll, or (v150-host beta 14) within 2.5 m in plain line of it when its place
// is off the mesh (a respawn point in a spot the mesh's margin left out: on n033a 15 dolls stood at three of them); far:
// the way goes from its own place to that point first
int gWideStart[16] = {0};                          // (beta 15: off the mesh 2 s, its start sought 6 m around)
bool StartPoint(const Self& s, float* from, bool* far) {
    float feet[3];
    Feet(s.pos, feet);
    *far = false;
    if (gMap->nav->Nearest(feet, 600.f, from)) return true;
    if (s.k >= 0 && s.k < 16 && gWideStart[s.k] && gMap->nav->Nearest(feet, 6000.f, from)) { *far = true; return true; }
    if (!gMap->nav->Nearest(feet, 2500.f, from)) return false;
    const float at[3] = {from[0], from[1] + kHips, from[2]};
    if (!LineClear(s.pos, at)) return false;
    *far = true;
    return true;
}
// (v150-host beta 15) the polygons near the enemies (12 m around each one alive), for a way that keeps off them
void NearEnemies(const Self& s, const float* from, const float* goal, std::vector<unsigned>& avoid) {
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], s.team)) continue;
        float f[3];
        Feet(gSeen[id].pos, f);
        if (Dist2D(f, from) < 6000.f || Dist2D(f, goal) < 4000.f) continue;   // (its own place and the goal stay open)
        gMap->nav->PolysNear(f, 12000.f, avoid);
    }
}
// (v150-host beta 19, kero) anyHeld: the way may pass the places the game held a doll back (as the carrier's way to its goal
// always did): the way to take a free item. Those places are kept for the map's rest and lie on its main ways (11 November,
// Ghost Factory: 13 of them by the round's end, 20 to 55 % of the ways to each item refused; the item left 73.5 s 1.4 m from
// one): the dolls planned no way to a free item a player could take
bool PlanTo(Body& b, const Self& s, const float* hips, bool keepOff = false, bool anyHeld = false, bool fences = false) {   // ((cqcfight2) fences: as out of a fight)
    struct FightWay { explicit FightWay(bool on) { gFightWay = on; } ~FightWay() { gFightWay = false; } } fight(b.target >= 0 && !fences);   // (v150-host beta 24, snakehouse2)
    float from[3], to[3], goal[3];
    bool far = false;
    b.roundAbout = false;
    Feet(hips, to);
    if (!StartPoint(s, from, &far) || !gMap->nav->Nearest(to, 1500.f, goal)) return false;
    std::vector<doll::Waypoint> w;
    float len = 0;
    bool ok = false;
    if (keepOff) {                                     // a way round the enemies first (another way each time they move)
        std::vector<unsigned> avoid;
        NearEnemies(s, from, goal, avoid);
        if (!avoid.empty()) ++gWorkPass.ways;
        ok = !avoid.empty() && gMap->nav->PathAvoiding(from, goal, avoid, w, &len, nullptr) && w.size() >= 2;
        if (ok) StairsAsClimbs(w);                     // (v150-host beta 24, snakehouse)
        ok = ok && WayOk(w, true);
        if (ok) b.roundAbout = true;
    }
    if (!ok && (!FindWay(from, goal, w, &len) || w.size() < 2)) return false;
    if (!ok && !WayOk(w, keepOff || anyHeld)) return false;
    b.path.clear();
    if (far) b.path.push_back({s.pos[0], s.pos[1] - kHips, s.pos[2]});
    for (const auto& p : w) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link});
    b.next = 1;
    return true;
}
// ---- (v150-host beta 15) the rule's objectives: the mission objects and the goals (the host gives an item to the nearest
// allowed player within reach, carries it with him and scores it: the doll only goes there), BASE CAPTURE's bases ----
struct Objectives { int n = 0; ObjNow it[4]; float goal[2][3] = {}; bool set[2] = {false, false};
                    int bases = 0; float base[5][3] = {}; int owner[5] = {}, contested[5] = {}; long long at = -1000; };
Objectives gObj;
void SeeObjectives(long long frame) {
    if (frame - gObj.at < 30) return;
    gObj.at = frame;
    static ObjNow was[4];
    static int wasN = 0;
    gObj.n = gHost.objectives && gRule >= 2 && gRule <= 5 ? gHost.objectives(gObj.it, 4, gObj.goal, gObj.set) : 0;
    gObj.bases = gHost.bases && gRule == 6 ? gHost.bases(gObj.base, gObj.owner, gObj.contested) : 0;
    if (gDebug)                                        // (each item's change: who holds it, where it is)
        for (int i = 0; i < gObj.n; ++i) {
            const ObjNow& o = gObj.it[i];
            if (i < wasN && was[i].holder == o.holder && was[i].state == o.state && was[i].goalTeam == o.goalTeam) continue;
            if (o.holder >= 0) BLOG("vdoll: the item %d held by player %d", i + 1, o.holder + 1);
            else if (o.goalTeam >= 0) BLOG("vdoll: the item %d in the %s goal (state %d)", i + 1, o.goalTeam ? "blue" : "red", o.state);
            else BLOG("vdoll: the item %d free (state %d) at %.0f %.0f %.0f", i + 1, o.state, o.pos[0], o.pos[1], o.pos[2]);
        }
    for (int i = 0; i < gObj.n; ++i) was[i] = gObj.it[i];
    wasN = gObj.n;
}
// (v150-host beta 19, kero) a place a player can take an item at: a standing player's hips (the mesh's floor + 0.8 m)
// within the game's pick-up reach (the mission objects' -l, 1500 mm, +0x414) of it; *gap: the nearest such hips' distance
struct Trail { struct P { long long frame; float copy[3], mine[3]; bool own; } p[30]; int n = 0; };
Trail gTrail[16];
float gOwnPos[16][3];                            // (a virtual doll's own place by its player's slot, the frame told)
long long gOwnAt[16] = {-100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100};
bool Harmless(int id) { return gTestHarmless != -1 && id >= 0 && id < 16 && (gTestHarmless == -2 ? !(gOwnAt[id] >= 0 && gFrame - gOwnAt[id] < 600) : id == gTestHarmless); }   // ((v150-host beta 24, cqcfight) MGO1_VDOLL_TEST_HARMLESS)
bool ItemReachable(const float* p, float* gap) {
    *gap = 1e30f;
    if (!Ready() || !Number(p)) return false;
    float m[3];
    if (!gMap->nav->Nearest(p, 1500.f, m)) return false;   // (1.5 m around, 3 m up and down)
    const float d[3] = {m[0] - p[0], m[1] + kHips - p[1], m[2] - p[2]};
    *gap = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    return *gap < 1500.f;
}
}  // namespace
void KeepOff(const float* at) { gHeld.push_back({at[0], at[1], at[2]}); }
namespace { bool CanLeaveFrom(const float* feet); }
bool CanLeave(const float* feet) { return CanLeaveFrom(feet); }
namespace {
// (v150-host beta 19, kero) with virtual dolls in the game, a free item never lies where no player can take it: lying 3 s
// in the same place (its fall over) with no standing player's place within the pick-up reach and nothing under it within
// 3 m (under the floor, in the air), it goes back to its home now, as the game's own return does after 180 s (capture
// only; rescue has none). Only such a place: the item on a floor, a crate or a ledge is never moved. The owner's case
// (11 November): a carrier's copy 3.4 m under Ghost Factory's ramp when he died, the item left there, home 10800 passes
// later (5 min 45 s on his slowed server)
void ItemRule(long long frame) {
    static const bool off = getenv("MGO1_VDOLL_ITEM_HOME") && !strcmp(getenv("MGO1_VDOLL_ITEM_HOME"), "0");   // (0: the item left where it lies, as beta 18)
    if (off || !gHost.itemHome || !Ready() || (gRule != 2 && gRule != 3)) return;
    static long long since[4] = {-1, -1, -1, -1}, doneAt[4] = {-100000, -100000, -100000, -100000};
    static float at[4][3];
    for (int i = 0; i < gObj.n && i < 4; ++i) {
        const ObjNow& o = gObj.it[i];
        if (o.holder >= 0 || o.state != 0 || o.goalTeam >= 0 || frame - doneAt[i] < 120) { since[i] = -1; continue; }
        if (since[i] < 0 || Dist3(o.pos, at[i]) > 100.f) { since[i] = frame; memcpy(at[i], o.pos, sizeof at[i]); continue; }
        if (frame - since[i] < 180) continue;
        float gap = 0;
        static int tests = getenv("MGO1_VDOLL_ITEM_TEST") ? atoi(getenv("MGO1_VDOLL_ITEM_TEST")) : 0;   // (benches: the first n rests sent home)
        const bool test = tests > 0;
        if (!test && (ItemReachable(o.pos, &gap) || gMap->nav->GroundBelow(o.pos, 3000.f) > -1e29f)) { since[i] = frame + 100000; continue; }   // (fine: not asked again here)
        if (gHost.itemHome(i)) {
            if (test) { --tests; BLOG("vdoll: MGO1_VDOLL_ITEM_TEST: the item %d's rest taken as one no player can take", i + 1); }
            BLOG("vdoll: the item %d lay %.1f s at %.0f %.0f %.0f where no player can take it (nothing under it within 3 m): back at its home, as the game's own return does after 180 s",
                 i + 1, (frame - since[i]) / 59.94, o.pos[0], o.pos[1], o.pos[2]);
            doneAt[i] = frame; since[i] = -1;
            gObj.at = -1000;                             // (its new place read at once)
        } else since[i] = frame + 100000;                // (refused, at its home: not asked again here)
    }
}
// (v150-host beta 19, kero) MGO1_VDOLL_DEBUG: each free item's rest (where it came to lie, whether a player can take it
// there, how long it lay), and a player's copy where no player stands (off the walkable ground: no mesh within 0.6 m of
// its feet and nothing under them within 3 m), once a life
void WatchItems(long long frame) {
    if (!gDebug || !Ready()) return;
    static long long restAt[4] = {-1, -1, -1, -1};
    static float restPos[4][3];
    for (int i = 0; i < gObj.n && i < 4; ++i) {
        const ObjNow& o = gObj.it[i];
        const bool resting = o.holder < 0 && o.state == 0 && o.goalTeam < 0;
        if (resting && (restAt[i] < 0 || Dist3(o.pos, restPos[i]) > 300.f)) {
            float gap = 0;
            const bool ok = ItemReachable(o.pos, &gap);
            const float g = gMap->nav->GroundBelow(o.pos, 20000.f);
            BLOG("vdoll: the item %d at rest at %.0f %.0f %.0f: %s (the floor %s, a player's hips %s)", i + 1, o.pos[0], o.pos[1], o.pos[2],
                 ok ? "a player can take it" : "NO PLAYER CAN TAKE IT", g > -1e29f ? (std::to_string((int)(o.pos[1] - g)) + " mm under it").c_str() : "none under it",
                 gap < 1e29f ? (std::to_string((int)gap) + " mm away").c_str() : "not within 1.5 m");
            restAt[i] = frame; memcpy(restPos[i], o.pos, sizeof restPos[i]);
        } else if (!resting && restAt[i] >= 0) {
            BLOG("vdoll: the item %d %s after %.1f s at rest", i + 1, o.holder >= 0 ? "taken" : o.goalTeam >= 0 ? "in a goal" : "moved", (frame - restAt[i]) / 59.94);
            restAt[i] = -1;
        }
    }
    static long long offFrom[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    static float lowest[16];
    for (int id = 0; id < 16; ++id) {
        float f[3], m[3];
        const bool seen = gHave[id] && gSeen[id].object && gSeen[id].alive;
        if (seen) Feet(gSeen[id].pos, f);
        const bool off = seen && !gMap->nav->Nearest(f, 600.f, m) && gMap->nav->GroundBelow(f, 3000.f) < -1e29f;
        if (seen && gSeenAt == frame) {              // (the last 3 s of reads: the way the copy went there)
            Trail& t = gTrail[id];
            Trail::P& q = t.p[t.n++ % 30];
            q.frame = frame; memcpy(q.copy, gSeen[id].pos, sizeof q.copy);
            q.own = frame - gOwnAt[id] < 12; if (q.own) memcpy(q.mine, gOwnPos[id], sizeof q.mine);
        }
        if (off && offFrom[id] < 0) {
            offFrom[id] = frame; lowest[id] = gSeen[id].pos[1];
            const Trail& t = gTrail[id];
            for (int k = std::max(0, t.n - 30); k < t.n; ++k) {
                const Trail::P& q = t.p[k % 30];
                float qf[3] = {q.copy[0], q.copy[1] - kHips, q.copy[2]};
                const float g = gMap->nav->GroundBelow(qf, 3000.f);
                BLOG("vdoll:   player %d at frame %lld: its copy %.0f %.0f %.0f (the ground %s)%s", id + 1, q.frame, q.copy[0], q.copy[1], q.copy[2],
                     g > -1e29f ? (std::to_string((int)(q.copy[1] - g)) + " mm under its hips").c_str() : "none within 3 m under its feet",
                     q.own ? (", the doll " + std::to_string((int)q.mine[0]) + " " + std::to_string((int)q.mine[1]) + " " + std::to_string((int)q.mine[2])).c_str() : "");
            }
            char own[96] = "";
            if (frame - gOwnAt[id] < 12)
                snprintf(own, sizeof own, "; its virtual doll at %.0f %.0f %.0f", gOwnPos[id][0], gOwnPos[id][1], gOwnPos[id][2]);
            BLOG("vdoll: player %d's copy off the walkable ground at %.0f %.0f %.0f (frame %lld%s)", id + 1, gSeen[id].pos[0], gSeen[id].pos[1], gSeen[id].pos[2], frame, own);
        } else if (off) lowest[id] = std::min(lowest[id], gSeen[id].pos[1]);
        else if (offFrom[id] >= 0) {
            BLOG("vdoll: player %d's copy %s after %.1f s off the walkable ground (its lowest height %.0f)", id + 1, seen ? "back on it" : "gone or dead", (frame - offFrom[id]) / 59.94, lowest[id]);
            offFrom[id] = -1;
        }
    }
}
bool Ally(int id, int team) { return id >= 0 && id < 16 && gHave[id] && gSeen[id].team == team && (team == 0 || team == 1); }
// (an item a side's dolls stood on 2 s without the host giving it: not that side's to take, 30 s)
long long gNotFor[2][4] = {{-100000, -100000, -100000, -100000}, {-100000, -100000, -100000, -100000}};
bool NotOurs(int team, int i) { return team >= 0 && team < 2 && i >= 0 && i < 4 && gFrame - gNotFor[team][i] < 1800; }
// (v150-host beta 18) the side that sneaks in this TEAM SNEAKING round: the one whose goals the mission objects hold, alone
// (the platform's goal counts, the ones tsne.h TsneSneakSide reads); -1 not known (another rule, no mission objects yet)
int SneakSide() {
    if (gRule != 5 || gObj.n <= 0 || gObj.set[0] == gObj.set[1]) return -1;
    return gObj.set[0] ? 0 : 1;
}
// its role now: a TEAM SNEAKING sneaker, SNEAKING's Snake (his class 8 on the host's copy, or the blue side: his alone),
// else a soldier
int RoleOf(const Self& s) {
    if (gRule == 5 && (s.team == 0 || s.team == 1) && SneakSide() == s.team) return R_SNEAKER;
    if (gRule == 4 && (s.team == 1 || (s.slot >= 0 && s.slot < 16 && gHave[s.slot] && gSeen[s.slot].object && gSeen[s.slot].cls == 8))) return R_SNAKE;
    return R_SOLDIER;
}
// (v150-host beta 18) a player knocked out (asleep or stunned) as the host's game holds him (the flag 0x99), or put out by
// this side's darts less than 8 s ago (the game's quickest wake, pressing all the time) and still where he fell: the
// host's copy of a remote player does not always show the flag (tsne_t1_pc_red: the playable doll's own game knocked
// out, its copy on the host not, 3 s on)
long long gSleptAt[16] = {-100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000,
                          -100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000};
float gStamina[16] = {30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000, 30000};
long long gStaminaAt[16] = {0};
float gSleptPos[16][3];
bool Asleep(int id) {
    if (id < 0 || id >= 16 || !gHave[id]) return false;
    if (gSeen[id].asleep) return true;
    return gFrame - gSleptAt[id] < 480 && Dist2D(gSeen[id].pos, gSleptPos[id]) < 1000.f;
}
// its estimate of a player's stamina after a dart (it refills at the game's knocked out rate at most, 24 a tick): true when
// it reaches 0 (asleep)
bool StaminaHit(int id, int stun) {
    if (id < 0 || id >= 16) return false;
    float& st = gStamina[id];
    st = std::min(30000.f, st + 12.f * (float)(gFrame - gStaminaAt[id]));
    gStaminaAt[id] = gFrame;
    st -= (float)stun;
    if (st > 0.f) return false;
    st = 30000.f; gSleptAt[id] = gFrame;
    if (gHave[id]) memcpy(gSleptPos[id], gSeen[id].pos, sizeof gSleptPos[id]);
    return true;
}
const char* const kTakeItem = "to take the item";   // (v150-host beta 19, kero: its way passes the held places)
// where the objective sends it now (the hips' place), whether its way keeps off the enemies, why; false: no objective
// (the rule has none, or its share of the ways went to the fights)
bool ObjectiveGoal(Body& b, const Self& s, float* to, bool* keepOff, const char** why) {
    *keepOff = false;
    b.mustObj = false; b.holding = false;
    const int team = s.team;
    if (gObj.bases > 0) {                              // BASE CAPTURE: the nearest base not its team's, held there
        for (int i = 0; i < gObj.bases; ++i) {         // (in one to take: it stays, a step or two inside)
            if ((gObj.owner[i] == team && !gObj.contested[i]) || Dist2D(gObj.base[i], s.pos) > 1800.f ||
                s.pos[1] - kHips < gObj.base[i][1] - 800.f || s.pos[1] - kHips > gObj.base[i][1] + 2800.f) continue;
            const float a = Rnd01() * kTurn, r = 400.f + 1100.f * Rnd01();
            to[0] = gObj.base[i][0] + std::sin(a) * r; to[1] = gObj.base[i][1] + kHips; to[2] = gObj.base[i][2] + std::cos(a) * r;
            *why = "holding a base"; b.mustObj = true; b.holding = true;
            return true;
        }
        int best = -1; float bd = 1e30f;
        for (int i = 0; i < gObj.bases; ++i) {
            if (gObj.owner[i] == team && !gObj.contested[i]) continue;
            const float d = Dist2D(gObj.base[i], s.pos) * (0.7f + 0.6f * Rnd01());
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0) return false;
        const float a = Rnd01() * kTurn, r = 300.f + 1200.f * Rnd01();
        to[0] = gObj.base[best][0] + std::sin(a) * r; to[1] = gObj.base[best][1] + kHips; to[2] = gObj.base[best][2] + std::cos(a) * r;
        *why = gObj.contested[best] ? "to a base held by both" : gObj.owner[best] < 0 ? "to a free base" : "to an enemy base";
        return true;
    }
    if (gObj.n <= 0 || team < 0 || team > 1) return false;
    const bool attack = gObj.set[team];                // (its team's goal set: it takes the items there; else it defends them)
    const ObjNow* mine = nullptr; const ObjNow* theirs = nullptr; const ObjNow* ours = nullptr; const ObjNow* free = nullptr;
    float fd = 1e30f;
    const ObjNow* passed = nullptr; float pd = 1e30f; int fi = -1, pi = -1;   // (v150-host beta 24, snakehouse: one out of its ways)
    for (int i = 0; i < gObj.n; ++i) {
        const ObjNow& o = gObj.it[i];
        if (o.holder == s.slot) mine = &o;
        else if (o.holder >= 0 && Ally(o.holder, team)) ours = &o;
        else if (o.holder >= 0) theirs = &o;
        else if ((o.state != 3 || o.goalTeam != team) && o.state != 2 && !NotOurs(team, i)) {
            const float d = Dist2D(o.pos, s.pos);
            if (gItemWay && i < 4 && gFrame - b.itemNoWay[i] < kItemNoWay) { if (d < pd) { pd = d; passed = &o; pi = i; } continue; }
            if (d < fd) { fd = d; free = &o; fi = i; }
        }
    }
    if (!free && passed) { free = passed; fi = pi; }   // (every free one out of its ways: the nearest, as before)
    b.objItem = fi;
    b.carrying = mine != nullptr;
    if (mine) {
        if (!attack) return false;                     // (a defender holding it: it keeps it, on its ways)
        memcpy(to, gObj.goal[team], sizeof gObj.goal[team]); to[1] += kHips;
        *keepOff = true; *why = "the item to its goal";
        return true;
    }
    if (theirs && gSeen[theirs->holder].object) { memcpy(to, gSeen[theirs->holder].pos, sizeof gSeen[theirs->holder].pos); *why = "after the item's carrier"; return true; }
    if (free && attack) { memcpy(to, free->pos, sizeof free->pos); to[1] += kHips; *why = kTakeItem; return true; }
    if (ours && gSeen[ours->holder].object) {          // escorting its carrier: a little ahead of him toward the goal
        const float* c = gSeen[ours->holder].pos;
        const float* g = gObj.goal[team];
        const float dx = g[0] - c[0], dz = g[2] - c[2], l = std::max(1.f, std::hypot(dx, dz));
        to[0] = c[0] + dx / l * 5000.f + (Rnd01() - 0.5f) * 4000.f; to[1] = c[1]; to[2] = c[2] + dz / l * 5000.f + (Rnd01() - 0.5f) * 4000.f;
        *why = "escorting its carrier";
        return true;
    }
    const ObjNow* guard = free ? free : gObj.n ? &gObj.it[0] : nullptr;   // a defender: round the item, between it and the enemies
    if (!guard) return false;
    const float a = Rnd01() * kTurn, r = 3000.f + 7000.f * Rnd01();
    to[0] = guard->pos[0] + std::sin(a) * r; to[1] = guard->pos[1] + kHips; to[2] = guard->pos[2] + std::cos(a) * r;
    *why = "guarding the item";
    return true;
}
const float kObjective[10] = {0.5f, 0.55f, 0.6f, 0.65f, 0.7f, 0.75f, 0.8f, 0.85f, 0.9f, 0.9f};   // the objective's share of its ways

// (v150-host beta 15) a goal toward the enemies: the place of one of them (the nearest, else any) give or take 4 to 12 m
// (where the fights are, as a player who knows the map goes there), on the mesh
bool HuntGoal(const Self& s, const float* from, float* goal) {
    int best = -1;
    float bestD = 1e30f;
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], s.team)) continue;
        if (gRule == 5 && gSeen[id].camo) continue;   // (beta 18) a camouflaged sneaker's place is not known to the defense
        const float d = Dist2D(gSeen[id].pos, from) * (0.6f + 0.8f * Rnd01());
        if (d < bestD) { bestD = d; best = id; }
    }
    if (best < 0) return false;
    // (v150-host beta 21, spin) on its side of him (within 70 degrees of his bearing to it) and no farther from him than it
    // is: toward him, not round him nor back from him. A point at any angle made each hunt cross to his other side, the
    // doll running round his place (the circles). Within 5 m of him: no hunt (a way of its own, ahead)
    const float ra = Rnd01();                         // (the draws in beta 20's order: the angle's, the distance's)
    float r = 4000.f + 8000.f * Rnd01();
    if (gTurn) {
        const float d = Dist2D(gSeen[best].pos, from);
        if (d < 5000.f) return false;
        r = std::min(r, d - 1000.f);
    }
    const float a = gTurn ? Bearing(gSeen[best].pos, from) + (ra * 2.f - 1.f) * 70.f * kPi / 180.f : ra * kTurn;
    const float p[3] = {gSeen[best].pos[0] + std::sin(a) * r, gSeen[best].pos[1] - kHips, gSeen[best].pos[2] + std::cos(a) * r};
    return gMap->nav->Nearest(p, 3000.f, goal);
}
const float kHunt[10] = {0.2f, 0.25f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.85f, 0.9f};   // the hunts' share of the ways, by level
// (v150-host beta 19, kero) on its way to take a free item already: the way made again each second (the item, the enemies
// move) goes on to it without a new draw of the objective's share. Each refresh drew it anew: at level 10 a 10 % chance a
// second to leave for a random place 15 to 70 m away, so a 40 m way reached the item about one time in three (Lost Forest,
// kero_t1_caprot: the item left 105 s on the hill, 47 s, 57 s on open ground; the share is of its ways, as before)
bool TakingOn(const Body& b, const char* why) { return why == kTakeItem && b.objWay && b.objWhy == kTakeItem; }
// a new way: toward the enemies (by level) or a random point of the map, reachable on foot, standing all the way (no drop,
// climb, ladder, crawl); (v150-host beta 15) 10 tries, the last ones nearer and through the places held too: never still
bool LapPlan(Body& b, const Self& s);              // ((v150-host beta 25, copyleash) benches: MGO1_VDOLL_TEST_LAPS, below)
bool Plan(Body& b, const Self& s) {
    float from[3];
    bool far = false;
    if (!StartPoint(s, from, &far)) { b.planWhy = "no mesh within 2.5 m"; return false; }
    // (beta 15) the rule's objective first: its share of the ways by level, always when it carries the item
    // (v150-host beta 24, snakehouse, MGO1_VDOLL_ITEM_WAY) its way to the free item refused (a ladder): that item passed over
    // for 30 s and the next free one chosen at once (the share drawn once)
    for (int again = 0; again < 2; ++again) {
        float to[3];
        bool keepOff = false;
        const char* why = "";
        const bool chose = (gObj.n > 0 || gObj.bases > 0) && ObjectiveGoal(b, s, to, &keepOff, &why) &&
            (b.carrying || b.mustObj || TakingOn(b, why) || again > 0 || Rnd01() < kObjective[b.level - 1]);
        if (chose && PlanTo(b, s, to, keepOff, why == kTakeItem)) {
            b.objWay = true; b.objAt = gFrame; b.objWhy = why; ++b.objWays; b.shortWay = false; b.shortRun = 0;
            if (b.roundAbout) ++b.roundAbouts;
            if (gDebug) BLOG("vdoll %d: %s (%.1f m%s)", s.k + 1, why, Dist2D(s.pos, to) / 1000.f, b.roundAbout ? ", a way round the enemies" : "");
            return true;
        }
        // (within the game's pick-up reach of it, 1.5 m from its hips, no way is needed: the host gives it; snakehouse_t5f marked
        // an item 0.5 m off, the game giving it 13 frames later. Under the tower's film, 4 m down, it is not in reach)
        if (!gItemWay || !chose || why != kTakeItem || b.objItem < 0 || b.objItem >= 4 || gFrame - b.itemNoWay[b.objItem] < kItemNoWay ||
            Dist3(s.pos, to) < 1500.f) break;
        b.itemNoWay[b.objItem] = gFrame;
        if (gDebug) BLOG("vdoll %d: no way of its to the item %d at %.0f %.0f %.0f (a ladder, a move it does not make): passed over for %lld s", s.k + 1,
                         b.objItem + 1, to[0], to[1] - kHips, to[2], kItemNoWay / 60);
    }
    b.objWay = false;
    if (LapPlan(b, s)) return true;                    // ((v150-host beta 25, copyleash) benches: MGO1_VDOLL_TEST_LAPS)
    if (gTrapTest && gHost.traps) {                    // (v150-host beta 21, tests: MGO1_VDOLL_TEST_TRAP) to the nearest trap
        float t[8][3];
        const int n = gHost.traps(t, 8);
        int best = -1; float bd = 1e30f; bool keptBest = false;
        for (int i = 0; i < n; ++i) {
            bool kept = false;                         // (v150-host beta 22, support: one its ways keep off, its own among them: 2.5 m short of it)
            if (gAvoidWays) for (const auto& a : *gAvoidWays) if (std::hypot(a[0] - t[i][0], a[2] - t[i][2]) < 500.f) kept = true;
            const float d = Dist2D(t[i], s.pos);
            if (kept && d < 3000.f) continue;
            if (d < bd) { bd = d; best = i; keptBest = kept; }
        }
        if (best >= 0 && keptBest) {
            const float a = Bearing(t[best], s.pos), n0[3] = {t[best][0] + std::sin(a) * 2500.f, t[best][1], t[best][2] + std::cos(a) * 2500.f};
            float nn[3];
            if (gMap->nav->Nearest(n0, 400.f, nn) && Dist2D(nn, t[best]) > 2000.f) memcpy(t[best], nn, sizeof nn); else best = -1;
        }
        if (best >= 0) {
            float to[3] = {t[best][0], t[best][1] + kHips, t[best][2]};
            if (PlanTo(b, s, to)) { BLOG("vdoll %d: to the trap at %.0f %.0f %.0f (%.1f m, MGO1_VDOLL_TEST_TRAP)", s.k + 1, t[best][0], t[best][1], t[best][2], bd / 1000.f); return true; }
        }
    }
    if (gSpotTest) {                                   // ((v150-host beta 25, meshmargin, benches) MGO1_VDOLL_TEST_SPOT: to the mesh's place nearest it, then still)
        static float at[3]; static bool have = false; static long long said[16] = {};
        const float feet[3] = {gSpot[0], gSpot[1], gSpot[2]};
        if (!have) have = gMap->nav->Nearest(feet, 1500.f, at);
        if (!have) { b.planWhy = "the test spot off the mesh"; return false; }
        const float to[3] = {at[0], at[1] + kHips, at[2]};
        if (Dist2D(s.pos, to) > 250.f && PlanTo(b, s, to)) { BLOG("vdoll %d: to the test spot %.0f %.0f %.0f (MGO1_VDOLL_TEST_SPOT)", s.k + 1, at[0], at[1], at[2]); return true; }
        if (Dist2D(s.pos, to) <= 250.f && gFrame - said[s.k & 15] >= 600) {
            said[s.k & 15] = gFrame;
            const float f0[3] = {s.pos[0], b.floorY, s.pos[2]}, *c = gSeen[s.slot].pos;
            const float f1[3] = {c[0], c[1] - kHips, c[2]};
            BLOG("vdoll %d: at the test spot: its hips %.0f %.0f %.0f, %.0f mm from a wall; its copy on the host %.0f %.0f %.0f, %.0f mm from a wall", s.k + 1, s.pos[0], s.pos[1],
                 s.pos[2], gMap->nav->WallGap(f0, 400.f, 1700.f, 1000.f), c[0], c[1], c[2], gMap->nav->WallGap(f1, 400.f, 1700.f, 1000.f));
        }
        b.planWhy = "at the test spot";
        return false;
    }
    int noPoint = 0, noPath = 0, shortWay = 0, notStanding = 0, offGoal = 0, areas[10] = {};
    for (int tries = 0; tries < 10; ++tries) {
        float goal[3];
        const bool hunt = tries < 5 && Rnd01() < kHunt[b.level - 1] && HuntGoal(s, from, goal);
        if (!hunt && !gMap->nav->RandomPoint(from, tries < 7 ? 40000.f : 15000.f, goal, Rnd)) { ++noPoint; continue; }
        // (v150-host beta 21, spin) its first random places ahead of it (within 120 degrees of its facing): a place behind it
        // was a way back at once, the doll to and fro (the about turns of beta 20's benches: 279 of 611 on random ways)
        if (gTurn && !hunt && tries < 5 && std::fabs(Wrap(Bearing(from, goal) - b.yaw)) > 120.f * kPi / 180.f) { ++noPoint; continue; }
        std::vector<doll::Waypoint> w;
        float len = 0;
        if (!FindWay(from, goal, w, &len) || w.size() < 2) { ++noPath; continue; }
        if (len < (tries < 7 ? 4000.f : 1500.f)) { ++shortWay; continue; }
        if (!WayOk(w, tries >= 8)) {
            ++notStanding;
            int a = -1;
            for (size_t i = 0; i < w.size(); ++i) if (!LinkOk(w, i)) { const auto& p = w[i]; a = p.link && p.area == doll::AREA_STAND ? 0 : p.area < 10 ? p.area : 0; break; }
            ++areas[a < 0 ? doll::AREA_STAND : a];   // (AREA_STAND: a place held on the way)
            continue;
        }
        if (Dist3(w.back().p, goal) > 500.f) { ++offGoal; continue; }
        // (beta 21, spin) nor a way whose first leg goes back (its first 5 tries, a hunt's too): round a wall to a place ahead
        if (gTurn && tries < 5 && std::fabs(Wrap(std::atan2(w[1].p[0] - w[0].p[0], w[1].p[2] - w[0].p[2]) - b.yaw)) > 120.f * kPi / 180.f) { ++noPoint; continue; }
        b.shortWay = len < 4000.f;
        b.shortRun = b.shortWay ? b.shortRun + 1 : 0;
        b.path.clear();
        if (far) b.path.push_back({s.pos[0], s.pos[1] - kHips, s.pos[2]});   // (its own place first: off the mesh)
        for (const auto& p : w) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link});
        b.next = 1;
        ++b.goals;
        if (gDebug) BLOG("vdoll %d: runs to %.0f %.0f %.0f (%.1f m, %zu corners%s)", s.k + 1, goal[0], goal[1], goal[2], len / 1000.f, w.size(),
                         hunt ? ", toward the enemies" : "");
        return true;
    }
    if (gDebug) {
        char t[320];
        snprintf(t, sizeof t, "%s; points %d, paths %d, short %d, refused %d (a place held %d, drop %d, climb %d, ladder %d, crawl %d, crouch %d, "
                 "vault %d, roll %d, steps %d, a link %d), off the goal %d", far ? "off the mesh" : "on the mesh", noPoint, noPath, shortWay,
                 notStanding, areas[doll::AREA_STAND], areas[doll::AREA_DROP], areas[doll::AREA_CLIMB], areas[doll::AREA_LADDER],
                 areas[doll::AREA_CRAWL], areas[doll::AREA_CROUCH], areas[doll::AREA_VAULT], areas[doll::AREA_ROLL], areas[doll::AREA_STEPS],
                 areas[0], offGoal);
        b.planWhy = t;
    }
    return false;
}
// (v150-host beta 21, spin) a respawn point the body's ways can leave: from it a way of 10 m or more that it can run, to
// one of 24 places of the map within 40 m (kept by point until the map changes; an open point answers at the first or
// second place). False for a patch of the mesh with no way out (n021a's 31875 1000 -82250, 2.6 x 2.9 m: a doll
// respawned there paced to and fro until it fell) and for a place fenced in that a player leaves over the fence with the
// action button, which the body does not do (n021a's 76000 1375 -32000: 5 places of 300 by a way it runs, 3 m at
// most): vdoll.cpp takes the next point. Of the disc's 12 maps' points, 300 places each: 17 points have under 5 in
// 100 such ways (n021a 10, n022a 4, n011a 2, n024a 1), every other point 7 in 100 or more, most over 60. True when it
// cannot tell (the mesh not built, the point off it) and with MGO1_VDOLL_TURN=0 (beta 20: every point taken)
std::vector<std::pair<std::array<float, 3>, bool>> gLeave;
bool CanLeaveFrom(const float* feet) {
    if (!gTurn || !Ready()) return true;
    for (const auto& k : gLeave) if (k.first[0] == feet[0] && k.first[1] == feet[1] && k.first[2] == feet[2]) return k.second;
    float from[3];
    bool ok = !gMap->nav->Nearest(feet, 2500.f, from);
    for (int i = 0; i < 24 && !ok; ++i) {
        float goal[3], len = 0;
        std::vector<doll::Waypoint> w;
        if (!gMap->nav->RandomPoint(from, 40000.f, goal, Rnd)) continue;
        ok = FindWay(from, goal, w, &len) && w.size() >= 2 && len >= 10000.f && WayOk(w, true);
    }
    if (gLeave.size() < 256) gLeave.push_back({{feet[0], feet[1], feet[2]}, ok});
    return ok;
}
// the floor under a point near the doll, within 450 mm of its own floor's height (-1e30: none)
float FloorNear(float x, float z, float near) {
    const float p[3] = {x, near + 450.f, z};
    return gMap->nav->GroundBelow(p, 900.f);
}
// the run's speed (mm a frame) on the slope ahead (its rise over its run, up over 0), the game's own (the reference
// capture: 389 mm a record on the flat, up a slope of 20 to 30 degrees 228, down 252)
float RunSpeed(float rise) {
    const float deg = std::atan(std::fabs(rise)) * 180.f / kPi;
    const float k = std::max(0.f, std::min(1.f, (deg - 5.f) / 17.f));
    return (389.f - (389.f - (rise > 0.f ? 228.f : 252.f)) * k) / 6.f;
}
// (v150-host beta 21, spin2) the run's pace (mm a frame) as a player's game moves a player, and so the copy of the doll on
// every player's screen (MGO1_VDOLL_PACE=0: RunSpeed above, beta 20's). Measured on the playable dolls' own records (115741
// running records of 97 benches, the 12 maps of the disc) and on the virtual dolls' copies on a playable doll's screen, the
// same rule for both: 389 mm a record on the flat, DOWN any ramp (measured to 32 degrees), up a ramp under 15 degrees along
// the run, over a single step; 225 up a ramp of 15 degrees or more along the run; 250 on stairs (flat steps), up and down.
// RunSpeed slowed the body from 5 degrees either way by its own measure of the floor while its copy ran on at 389: the copy
// passed the place the doll told and came back round to it in a loop on the players' screens (docs/VIRTUAL_DOLL.md 24.1)
const float kTanSteep = 0.26795f;                // (tan 15 degrees: up a ramp this steep along the run, 225 mm a record)
float GamePace(const Body& b, const Self& s, float ux, float uz) {
    float n[3], t;
    const float a[3] = {s.pos[0], b.floorY + 300.f, s.pos[2]}, c[3] = {s.pos[0], b.floorY - 300.f, s.pos[2]};
    if (gMap->nav->FirstHit(a, c, &t, n) && n[1] > 0.3f && n[1] < 0.9986f)   // (a ramp's face under it, over 3 degrees)
        return -(n[0] * ux + n[2] * uz) / n[1] >= kTanSteep ? 225.f / 6.f : kRun;
    // stairs: a flat face under it and a riser of 60 mm or more within 300 mm on both sides the same way (a single step, a
    // curb: one side only, the full run)
    const float ahead = FloorNear(s.pos[0] + ux * 300.f, s.pos[2] + uz * 300.f, b.floorY), back = FloorNear(s.pos[0] - ux * 300.f, s.pos[2] - uz * 300.f, b.floorY);
    if (ahead > -1e29f && back > -1e29f && ((ahead - b.floorY >= 60.f && b.floorY - back >= 60.f) || (b.floorY - ahead >= 60.f && back - b.floorY >= 60.f))) return 250.f / 6.f;
    return kRun;
}
// (v150-host beta 21, spin2) the host slowed: its frames longer than a 59.94th of a second (the owner's server while people
// play: 33 to 46 VBlanks a second). Each player's game runs its copies of the dolls at its own 60 frames a second, a player's
// run in real time, so a body that ran 389 mm in 6 of the host's slowed frames told places that moved at 55 to 77 % of
// that: its copies ran ahead of them and came back round (cover_thr_on, the host at 28 to 31: the copy ahead of the place
// told by more than 250 mm in 30 % of its running records, 0 % on a host at full pace). The run is kept a player's in real
// time: its pace times this (1 to 3, the frames' real length, gMsAFrame)
float Slowed() { return gPace ? (float)std::max(1.0, std::min(3.0, gMsAFrame * 59.94 / 1000.0)) : 1.f; }
// (v150-host beta 14) the host's copy more than 1.3 m behind it (the game's collision slides it along a wall slower than
// the run): the doll waits for it; HeldBack when it stops
bool Leashed(const Body& b, const Self& s) {
    const PlayerNow& me = gSeen[s.slot];
    if (!gHave[s.slot] || !me.object || !me.alive || gFrame - b.lifeAt < 120 || gFrame - b.landUntil < 60 || gFrame - b.climbUntil < 60) return false;
    // (beta 21, spin2) on a slowed host the host's own copy runs at its slowed frames' pace behind a body that runs in real
    // time (as it runs behind a real player): no wait for it there
    if (Slowed() > 1.1f) return false;
    float c[3];
    Now(s.slot, c);
    return Dist2D(c, s.pos) > 1300.f && std::fabs(c[1] - s.pos[1]) < 1500.f;
}
// (v150-host beta 21, spin) the body turned as a pad turns a player (MGO1_VDOLL_TURN=0: beta 20's, the facing set at once).
// Beta 20 set the facing to its way's next corner in one frame: a new way behind it was a 180 degree flip in one record,
// and the players' games turn a copy over about 25 frames, each wide turn its own way round, so two about turns within a
// second showed as a full spin (spin_b0: 1 to 3 a doll in 2.5 minutes). Now at most kBodyTurn degrees a frame (the
// readied turn's ceiling, kTpsMax below), on the spot while the turn left is wider than kTurnOnSpot, and no spin: on its
// ways never more than kSpinWay degrees one way round within kSpinFrames (1.6 s). An about turn (over kWideTurn) that
// would pass it goes the other way round (kSpinLong that way at most: a turn back, not a spin); another turn waits where
// it stands for the frames to pass. In a fight its aim is never held back: the shortest way round, the other one only for
// a turn of 120 degrees or more that would make kSpin degrees one way. A wide turn's way round is kept to its end (its
// goal within 45 degrees a frame).
const float kBodyTurn = 16.f, kTurnOnSpot = 60.f, kWideTurn = 150.f, kSpinWay = 180.f, kSpinLong = 210.f, kSpin = 270.f;
const int kSpinFrames = 96;
// the degrees it may still turn that way round (1 or -1) now, of lim within kSpinFrames
float SpinRoom(const Body& b, int sg, float lim) {
    float sum = 0.f, most = 0.f;
    for (int k = 1; k <= kSpinFrames; ++k) {
        sum += b.spun[(b.spunAt - k + 2 * kSpinFrames) % kSpinFrames] * (float)sg;
        most = std::max(most, sum);
    }
    return std::max(0.f, lim - most);
}
// its turns kept, a frame at a time (Alive, whatever turned it: its way, its fight, a roll)
void SpunNow(Body& b, int dt) {
    const float d = Wrap(b.yaw - b.yawWas) * 180.f / kPi;
    for (int i = 0; i < dt; ++i) { b.spun[b.spunAt] = d / (float)dt; b.spunAt = (b.spunAt + 1) % kSpinFrames; }
    if (dt > 0) b.yawWas = b.yaw;
}
// its turn left toward a facing along its way round (radians, signed): the shortest, or the other one (the rule above)
float TurnLeft(Body& b, float want, bool fight) {
    const float toDeg = 180.f / kPi;
    float e = Wrap(want - b.yaw);
    if ((b.turnSgn || b.turnLong) && std::fabs(Wrap(want - b.turnWant)) * toDeg > 45.f) { b.turnSgn = 0; b.turnLong = false; }   // (another goal: its way round chosen again)
    b.turnWant = want;
    if (b.turnLong && std::fabs(e) * toDeg < 2.f) b.turnLong = false;   // (the other way round's turn made: kSpinLong was its bound to the end)
    if (b.turnSgn == 0 && !b.turnLong) {
        const int sg = e < 0.f ? -1 : 1;
        const float deg = std::fabs(e) * toDeg;
        if (deg > SpinRoom(b, sg, fight ? kSpin : kSpinWay) + 0.5f && deg >= (fight ? 120.f : kWideTurn) &&
            360.f - deg <= SpinRoom(b, -sg, fight ? kSpin : kSpinLong)) { b.turnSgn = -sg; b.turnLong = true; }
        else if (deg > kWideTurn) b.turnSgn = sg;
    }
    if (b.turnSgn) {                                  // (its way round kept to the turn's end)
        if ((e < 0.f ? -1 : 1) != b.turnSgn) e += (float)b.turnSgn * kTurn;
        if (std::fabs(e) * toDeg > 300.f) { e = Wrap(e); b.turnSgn = 0; }   // (its goal moved round: the short way)
        else if (std::fabs(e) * toDeg < 90.f) b.turnSgn = 0;
    }
    return e;
}
// toward a facing, maxDeg a frame at most; what is left of the turn (degrees); waits: held back this frame (no spin)
float TurnBody(Body& b, float want, int dt, float maxDeg = kBodyTurn, bool fight = false, bool* waits = nullptr) {
    const float toDeg = 180.f / kPi;
    const float e = TurnLeft(b, want, fight);
    // ((v150-host beta 25, dpass) a pass with no frame since the last turns nothing (MGO1_VDOLL_ONE_PASS; beta 25: a frame's turn there too,
    // half as much again in a host frame that ran it twice)
    float step = maxDeg * (float)(gOnePass ? dt : std::max(1, dt));
    if (!fight) {
        const float room = SpinRoom(b, e < 0.f ? -1 : 1, b.turnLong ? kSpinLong : kSpinWay);
        if (room < std::min(step, std::fabs(e) * toDeg) - 0.01f) { step = room; if (waits) *waits = true; }
    }
    step *= kPi / 180.f;
    const float d = std::max(-step, std::min(step, e));
    b.yaw = Wrap(b.yaw + d);
    return std::fabs(e - d) * toDeg;
}
// (v150-host beta 21, spin) a shot not yet in a record (Records, the next tick: 5 frames at most): the body stays as it
// fired. Beta 20 ended its fight at its last shot (he is down, lost, a reload), turned to its way, and the record that
// tells the shot (08 kind 5, the point aimed at) was made from the facing it had by then: the killing shot shown
// from a doll that faced and aimed somewhere else
bool ShotHold(const Body& b) { return gTurn && gScope && b.shotSince && gFrame - b.shotAt < 6; }

// (v150-host beta 24, snakehouse) on a block of a stairs (its top off the mesh, StairsAsClimbs): the objective's way is not made
// again there (from the mesh's floor a metre under it the new way went down and up the same stairs again each second)
bool OnBlock(const Self& s) {
    if (!gStairs) return false;
    float feet[3], n[3];
    Feet(s.pos, feet);
    return !gMap->nav->Nearest(feet, 300.f, n) || std::fabs(n[1] - feet[1]) > 300.f;
}
void FallBegin(Body& b, const Self& s);              // ((v150-host beta 25, fallflinch) below, with the fall)
// along the way: the run's distance this pass at the slope's speed, the floor under it, the hips over the floor around
// (the steps' heights averaged: the game's hips go up stairs smoothly)
void Run(Body& b, const Self& s, int dt) {
    b.speed = 0;
    if (b.next >= b.path.size()) {
        if (!b.path.empty()) {
            b.path.clear(); ++b.reached; b.pauseUntil = gFrame + (long long)(b.holding ? 6 + Rnd() % 18 : Rnd() % 12);   // (beta 15: on at once; in a base, its steps there)
            // (beta 21, spin2) a pause shorter than a record falls between two records that both say it runs: its copy runs on
            // (on a host slowed by half, 7 of the player's frames: 480 mm), so none: on at once
            if (gPace && !b.holding && b.pauseUntil - gFrame < 6) b.pauseUntil = gFrame;
            // (beta 21) after a way under 4 m a stand of 0.7 to 1.5 s, not a shuttle; twice, three, four times as long after the
            // second, third, fourth short way in a row (a patch of the mesh with no way out: it stands there, it does not pace)
            if (gTurn && b.shortWay) b.pauseUntil = gFrame + (40 + (long long)(Rnd() % 50)) * (long long)std::max(1, std::min(4, b.shortRun));
        }
        if (gFrame < b.pauseUntil || gFrame < b.planAt) return;
        if (gBudget && gWorkPass.ways >= kWaysAPass && b.putOff < 6) { ++b.putOff; ++gPutOff; b.planAt = gFrame + 1; return; }   // (beta 16)
        b.putOff = 0;
        if (Plan(b, s)) {
            b.planFails = 0; b.noMesh = 0; if (s.k >= 0 && s.k < 16) gWideStart[s.k] = 0;
            if (!gPace || b.next >= b.path.size()) return;
            // (beta 21, spin2) on along the new way in the same frame: no frame of no move between two ways
        } else {                                        // (no way: as before)
        if (b.planWhy == "no mesh within 2.5 m") {         // (beta 15) off the mesh (a fall, a knock-down): its copy's place
            ++b.noMesh;                                     // again after 1 s, its start sought 6 m around after 2 s
            if (b.noMesh == 10) b.resync = true;
            if (b.noMesh >= 20 && s.k >= 0 && s.k < 16) gWideStart[s.k] = 1;
        }
        b.planAt = gFrame + 6;
        if (++b.planFails >= 2) {                       // no way from here: the places held nearby forgotten
            b.planFails = 0;
            gHeld.erase(std::remove_if(gHeld.begin(), gHeld.end(), [&](const std::array<float, 3>& h) { return std::hypot(h[0] - s.pos[0], h[2] - s.pos[2]) < 4000.f; }), gHeld.end());
            gHeldOnce.erase(std::remove_if(gHeldOnce.begin(), gHeldOnce.end(), [&](const std::array<float, 3>& h) { return std::hypot(h[0] - s.pos[0], h[2] - s.pos[2]) < 4000.f; }), gHeldOnce.end());
            if (gDebug) BLOG("vdoll %d: no way from %.0f %.0f %.0f: the places held near it forgotten (%s)", s.k + 1, s.pos[0], s.pos[1], s.pos[2], b.planWhy.c_str());
        }
        return;
        }
    }
    if (b.objWay && gFrame - b.objAt >= 60 && !OnBlock(s)) { b.path.clear(); b.next = 0; return; }   // (the objective's way again: it moves)
    if (gFrame < b.standUntil) return;
    if (Leashed(b, s)) { b.waitCopy = true; return; }
    if (gTurn) {                                       // (v150-host beta 21) turned toward its way, on the spot when wide
        const auto& c = b.path[b.next];
        const auto& p = b.path[b.next - 1];
        const float dx = c[0] - s.pos[0], dz = c[2] - s.pos[2], from = std::hypot(s.pos[0] - p[0], s.pos[2] - p[2]);
        const bool link = p.link && from < 2.f;        // (a drop, a climb: faced first)
        const bool start = b.next == 1 && from < 100.f;   // (its way's start: a turn over kTurnOnSpot on the spot; a corner on the run: only a sharp one, no halt at each)
        bool waits = false;                            // (held back by its turns of the last 1.6 s: it stands, not a run sideways)
        if (std::hypot(dx, dz) > 1.f && TurnBody(b, std::atan2(dx, dz), dt, kBodyTurn, false, &waits) > (link ? 8.f : waits ? 25.f : start ? kTurnOnSpot : 120.f)) return;
    }
    float rise = 0.f, pace = kRun;
    {
        const auto& c = b.path[b.next];
        const float dx = c[0] - s.pos[0], dz = c[2] - s.pos[2], dl = std::hypot(dx, dz);
        if (dl > 1.f && gPace) pace = GamePace(b, s, dx / dl, dz / dl);   // (beta 21, spin2: a player's pace, its copy's)
        else if (dl > 1.f) {
            const float back = FloorNear(s.pos[0] - dx / dl * 300.f, s.pos[2] - dz / dl * 300.f, b.floorY);
            const float ahead = FloorNear(s.pos[0] + dx / dl * 500.f, s.pos[2] + dz / dl * 500.f, b.floorY);
            if (back > -1e29f && ahead > -1e29f) rise = (ahead - back) / 800.f;
        }
    }
    float left = (gPace ? pace * Slowed() : RunSpeed(rise)) * dt;
    float x = s.pos[0], z = s.pos[2], meshY = b.floorY;
    while (left > 0.f && b.next < b.path.size()) {
        const auto& c = b.path[b.next];
        const auto& from = b.path[b.next - 1];
        if (from.link && std::hypot(x - from[0], z - from[2]) < 2.f) {   // (beta 15) at a link's start: its drop or its climb
            s.pos[0] = x; s.pos[2] = z;
            if (from.area == doll::AREA_DROP) {
                b.fallAt = gFrame; b.fallVy = 130.f; b.fallTo[0] = c[0]; b.fallTo[1] = c[1]; b.fallTo[2] = c[2]; ++b.drops;
                FallBegin(b, s);                         // ((v150-host beta 25, fallflinch) a player's fall from here)
            } else if (from.area == doll::AREA_VAULT) {     // (v150-host beta 24, snakehouse) over the fence at a player's pace
                memcpy(b.vaultFrom, s.pos, sizeof b.vaultFrom); b.vaultFrom[1] = from[1] + kHips;
                b.vaultTo[0] = c[0]; b.vaultTo[1] = c[1] + kHips; b.vaultTo[2] = c[2];
                const float d = std::hypot(c[0] - x, c[2] - z);
                b.vaultAt = gFrame; b.vaultUntil = gFrame + std::max(12L, std::lrint(d / kVaultPace));
                b.climbUntil = b.vaultUntil; b.landSaid = true; ++b.vaults; b.vaultSaid = false;
                if (gDebug) BLOG("vdoll %d: crosses the fence from %.0f %.0f %.0f to %.0f %.0f %.0f (%lld frames)", s.k + 1, x, from[1], z, c[0], c[1], c[2], b.vaultUntil - b.vaultAt);
            } else {
                s.pos[0] = c[0]; s.pos[2] = c[2]; b.floorY = c[1]; s.pos[1] = c[1] + kHips;
                b.climbUntil = gFrame + 60; b.landSaid = false; ++b.climbs;
            }
            if (!gTurn) b.yaw = std::atan2(c[0] - from[0], c[2] - from[2]);   // (beta 21: faced before it, above)
            ++b.next;
            b.speed = 0;
            return;
        }
        const float dx = c[0] - x, dz = c[2] - z, d = std::hypot(dx, dz);
        if (d < 1.f) { ++b.next; continue; }
        if (!gTurn) b.yaw = std::atan2(dx, dz);        // (beta 21: turned above, kBodyTurn a frame)
        const float step = std::min(left, d);
        const auto& p = b.path[b.next - 1];
        const float seg = std::hypot(c[0] - p[0], c[2] - p[2]);
        x += dx / d * step; z += dz / d * step;
        left -= step;
        const float along = seg > 1.f ? std::min(1.f, std::hypot(x - p[0], z - p[2]) / seg) : 1.f;
        meshY = p[1] + (c[1] - p[1]) * along;
        if (step >= d - 0.5f) ++b.next;
    }
    if (gAvoidWays && !gAvoidWays->empty()) {          // (v150-host beta 22, support) never a step into the keep-off of a claymore or book
        for (const auto& h : *gAvoidWays) {
            const float now = std::hypot(h[0] - s.pos[0], h[2] - s.pos[2]), then = std::hypot(h[0] - x, h[2] - z);
            // (into its keep-off from out of it, or nearer than 1 m: its own just laid one leaves it by a way out)
            const bool in = (then < vsup::kKeepOff && now >= vsup::kKeepOff) || (then < 1000.f && then < now);
            if (in && std::fabs(h[1] - (s.pos[1] - kHips)) < 1500.f) { b.speed = 0; b.path.clear(); b.next = 0; b.planAt = gFrame + 6; return; }
        }
    }
    const float moved = std::hypot(x - s.pos[0], z - s.pos[2]);
    b.ran += moved;
    b.speed = dt > 0 ? moved / dt : 0.f;
    const float f = FloorNear(x, z, b.floorY);
    b.floorY = f > -1e29f ? f : FloorAt(x, meshY, z);
    const float ux = std::sin(b.yaw), uz = std::cos(b.yaw);
    float sum = b.floorY, n = 1.f;
    for (float off : {-300.f, 300.f}) {
        const float g = FloorNear(x + ux * off, z + uz * off, b.floorY);
        if (g > -1e29f && std::fabs(g - b.floorY) < 400.f) { sum += g; n += 1.f; }
    }
    s.pos[0] = x; s.pos[2] = z; s.pos[1] = sum / n + kHips;
}

// (v150-host beta 25, fallflinch) a drop's fall and landing as a player's own game makes them (docs/VIRTUAL_DOLL.md 55), measured on
// real clients (probe 52's fall lines, bench ff_falls1: a playable doll's own drops of 1.2, 2.4 and 3 m off ledges, and the virtual
// dolls' copies on its screen): from the edge his hips go down 21.8 x n (n + 3) / 2 mm by the n-th tick of his game (44, 109, 196,
// 305, 436, 588, 762, 958, 1176, 1416, 1677, 1960, 2265, 2592: the game's own fall, a copy's on another screen the same, beta 15's
// model to a few mm); at the first tick his feet are within 0.77 m of the floor below the landing's motion (0x82) begins and at the
// next one he stands on that floor (the hips dip there and come back up over 2 or 3 ticks). So a metre's drop lands at the 5th tick
// (10 frames: bookhold's measure), 1.2 m at the 6th, 2.4 m at the 12th, 3 m at the 14th or 15th, where beta 15's body fell on to
// the floor itself (1 m in 16 frames, 3 m in 30) and its copies, falling by the players' own games, stood landed on their screens
// while it was still in the air. Then the ledge action holds him 9 ticks more (18 frames: the doll's own already). On a player's
// clock (30 ticks a real second, Slowed()). MGO1_VDOLL_HIT_FALL=0: beta 15's fall, the 18 frames by the host's frames
bool gHitFall = true;
const float kFallTick = 21.8f;                      // (mm: the n-th tick's drop is kFallTick x (n + 1))
const float kLandReach = 770.f;                     // (the feet within this of the floor: the landing at the next tick)
const int kLandHoldTicks = 9;
float PlayerDrop(float t) { return t <= 0.f ? 0.f : kFallTick * t * (t + 3.f) * 0.5f; }
// the tick a player's fall of h mm lands at
int LandTick(float h) {
    int n = 1;
    while (n < 60 && PlayerDrop((float)n) < h - kLandReach) ++n;
    return n + 1;
}
void FallBegin(Body& b, const Self& s) {
    b.fallAt0 = gFrame; b.fallT = 0.f;
    memcpy(b.fallFrom, s.pos, sizeof b.fallFrom);
    b.fallY0 = s.pos[1] - kHips;
    b.fallN = LandTick(b.fallY0 - b.fallTo[1]);
}
void FallAsPlayer(Body& b, const Self& s, int dt) {
    b.fallT += (float)dt * Slowed() * 0.5f;
    const float h = b.fallY0 - b.fallTo[1];
    if (b.fallT >= (float)b.fallN || h <= 0.f) {
        memcpy(s.pos, b.fallTo, sizeof b.fallTo);
        s.pos[1] = b.fallTo[1] + kHips; b.floorY = b.fallTo[1];
        b.fallAt = -1; b.landAt = gFrame; b.landSaid = false;
        b.landUntil = gFrame + std::max(1L, std::lrint(2.f * kLandHoldTicks / Slowed()));   // (9 of a player's ticks by the clock)
        if (gDebug) BLOG("vdoll %d: lands %.2f m down, %lld frames after the edge (a player's %d ticks by his clock)", s.k + 1, h / 1000.f, gFrame - b.fallAt0, b.fallN);
    } else {
        const float k = b.fallT / (float)b.fallN;      // (across to the landing's place by the landing's tick)
        s.pos[0] = b.fallFrom[0] + (b.fallTo[0] - b.fallFrom[0]) * k;
        s.pos[2] = b.fallFrom[2] + (b.fallTo[2] - b.fallFrom[2]) * k;
        s.pos[1] = b.fallY0 + kHips - std::min(h, PlayerDrop(b.fallT));
    }
    b.speed = 0;
}
// (beta 15) its fall: the place to the landing's across, straight down by the game's measured fall (each 100 ms 197 mm a
// record faster), then 18 frames of landing
void Fall(Body& b, const Self& s, int dt) {
    if (gHitFall) { FallAsPlayer(b, s, dt); return; }   // ((v150-host beta 25, fallflinch) a player's fall and landing)
    for (int i = 0; i < dt && b.fallAt >= 0; ++i) {
        const float dx = b.fallTo[0] - s.pos[0], dz = b.fallTo[2] - s.pos[2], d = std::hypot(dx, dz), st = std::min(d, 60.f);
        if (d > 1.f) { s.pos[0] += dx / d * st; s.pos[2] += dz / d * st; }
        s.pos[1] -= b.fallVy / 6.f;
        b.fallVy += 197.f / 6.f;
        if (s.pos[1] - kHips <= b.fallTo[1]) {
            s.pos[1] = b.fallTo[1] + kHips; b.floorY = b.fallTo[1];
            b.fallAt = -1; b.landUntil = gFrame + 18; b.landSaid = false;
            b.landAt = gFrame;
            if (gDebug) BLOG("vdoll %d: lands %.2f m down, %lld frames after the edge (MGO1_VDOLL_HIT_FALL=0: 16 frames a metre)", s.k + 1, (b.fallY0 - b.fallTo[1]) / 1000.f, gFrame - b.fallAt0);
        }
    }
    b.speed = 0;
}

// (v150-host beta 24, snakehouse) a fence crossed: its place along the line from the near side to the far side, a real client's
// pace over it (V22_owner_both, n021a's fence at 13465 4404 -71452 crossed by a playable doll: its positions 150 to 160 mm a
// record of 6 frames, its style 20 walking, its hips at the floor's height plus theirs, its facing records as it goes; then
// 40 running), no landing record (none in a real client's crossing)
void Vault(Body& b, const Self& s, int dt) {
    (void)dt;
    const float t = std::min(1.f, (float)(gFrame - b.vaultAt) / std::max(1.f, (float)(b.vaultUntil - b.vaultAt)));
    const float was[3] = {s.pos[0], s.pos[1], s.pos[2]};
    for (int k = 0; k < 3; ++k) s.pos[k] = b.vaultFrom[k] + (b.vaultTo[k] - b.vaultFrom[k]) * t;
    b.floorY = s.pos[1] - kHips;
    b.speed = std::hypot(s.pos[0] - was[0], s.pos[2] - was[2]) > 0.5f ? kVaultPace : 0.f;
    b.ran += std::hypot(s.pos[0] - was[0], s.pos[2] - was[2]);
}
bool Vaulting(const Body& b) { return b.vaultUntil >= 0 && gFrame < b.vaultUntil; }
// ---- (v150-host beta 25, copyleash, benches) MGO1_VDOLL_COPY_TRACE=1: at each read of the players a line for each player, the host's
// copy, the last position record its game took from him (its age: a real player's only), its state word and style; and for each doll
// its body (Alive). MGO1_VDOLL_TEST_LAPS="<page>:x,y,z/x,y,z/...;<page>:...": on a listed map a doll's ways go to those floor places in
// turn (its other ways on the others). The lag of a copy behind its player by the floor and the host's pace (docs/VIRTUAL_DOLL.md 53)
int gCopyTrace = 0;
std::string gLapSpec;
void CopyTrace(long long frame) {
    for (int id = 0; id < 16; ++id) {
        if (!gHave[id] || !gSeen[id].object) continue;
        const PlayerNow& p = gSeen[id];
        BLOG("vdoll: copytrack f=%lld t=%llu player %d copy %.0f %.0f %.0f rec %.0f %.0f %.0f age %d state %x style %02x alive %d slowed %.2f", frame, RealMs(), id + 1,
             p.pos[0], p.pos[1], p.pos[2], p.sent[0], p.sent[1], p.sent[2], p.sentAge, p.state, p.style & 0xFF, p.alive ? 1 : 0, Slowed());
    }
}
bool LapPlan(Body& b, const Self& s) {
    const char* e = getenv("MGO1_VDOLL_TEST_LAPS");     // (read at each way: a test sets other places for its next part)
    if (!e || !*e || !gMap) return false;
    static std::string page;
    static std::vector<std::array<float, 3>> pts;
    static int at[16];
    if (page != gMap->page || gLapSpec != e) {
        page = gMap->page; gLapSpec = e; pts.clear();
        for (int& a : at) a = -1;
        for (size_t k = 0; k < gLapSpec.size();) {
            size_t semi = gLapSpec.find(';', k);
            if (semi == std::string::npos) semi = gLapSpec.size();
            const std::string part = gLapSpec.substr(k, semi - k);
            const size_t c = part.find(':');
            if (c != std::string::npos && part.substr(0, c) == page)
                for (size_t j = c + 1; j < part.size();) {
                    size_t sl = part.find('/', j);
                    if (sl == std::string::npos) sl = part.size();
                    std::array<float, 3> q;
                    if (sscanf(part.substr(j, sl - j).c_str(), "%f,%f,%f", &q[0], &q[1], &q[2]) == 3) pts.push_back(q);
                    j = sl + 1;
                }
            k = semi + 1;
        }
    }
    if (pts.empty()) return false;
    int& i = at[s.k & 15];
    for (size_t n = 0; n < pts.size(); ++n) {
        i = (i + 1) % (int)pts.size();
        const float to[3] = {pts[i][0], pts[i][1] + kHips, pts[i][2]};
        if (Dist2D(to, s.pos) < 1000.f && std::fabs(to[1] - s.pos[1]) < 1500.f) continue;
        if (PlanTo(b, s, to)) { BLOG("vdoll %d: a lap to %.0f %.0f %.0f, %.1f m (MGO1_VDOLL_TEST_LAPS)", s.k + 1, pts[i][0], pts[i][1], pts[i][2], Dist2D(to, s.pos) / 1000.f); return true; }
    }
    return false;
}
// the host's own copy of the doll held back 1.2 m behind it, still 300 ms (what the game's collision has and the mesh
// has not): the doll goes on from where the game has it, by another way
bool RollHoldsCopy(const Body& b);                    // ((v150-host beta 25, rolltime) below, with the roll)
// ((v150-host beta 25, copyleash) MGO1_VDOLL_COPY_LEASH (on; 0: as beta 25): no hold counted while the host's copy falls after the game's
// own put, nor while it lands. On a slowed host the game runs a copy in its own slowed frames, so it falls behind its records (the host's
// copy of a real player too: 0.9 to 1.0 m at pace 50, up to 3.7 m at pace 12 to 22), and once a record is 3 m or more from it the game
// puts it at the record's x and z at its own height and 0.2 to 0.3 m more: on stairs or a slope that is in the air going down (it falls
// 1 to 2 s with no move across, its state word 0x200002, landed 0x200003) or under the floor going up (2), and once down it stands 0 to
// 43 host frames more (cl_m2_q5: 63 falls), as the host's copy of a real player does (docs/VIRTUAL_DOLL.md 53). HeldBack took that for the
// game's collision holding the copy and set the doll back 2.6 to 8.0 m; nothing puts a real player back. Counted as before: a copy on its
// floor running or walking (the moving bit 0x4000: 0x5002, 0x205002 on stairs, 0x4802), standing or aiming (3, 2: 2 is also a back to
// a wall) once kCopyLanding frames have gone since its last fall, and a state word of 0 (none read). Nor while the copy plays the doll's
// own blows of a teammate's wake (BlowHoldsCopy below)
bool gCopyLeash = true;
const unsigned kCopyMoving = 0x4000u, kCopyAir = 0x200000u;
const long long kCopyLanding = 48;                    // (host frames after its last read off its floor: its landing, 43 at most measured)
long long gCopyFellAt[16] = {-100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000,
                             -100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000};
// its copy off its floor now: in the air after the put (0x200000 without the moving bit), or its hips not over the ground under them
// (in the air, under the floor)
bool CopyFalls(const PlayerNow& me) {
    if (me.state == 0 || !Ready()) return false;
    if ((me.state & kCopyAir) && !(me.state & kCopyMoving)) return true;
    const float g = gMap->nav->GroundBelow(me.pos, 3000.f);
    return !(g > -1e29f && std::fabs(me.pos[1] - kHips - g) < 300.f);
}
// its copy in its own blows' motion (a teammate's wake: the punch combo pressed running into him, its kick, or the CQC throw) from its
// press to 30 host frames after the motion's end (the records' way to the host's game and its motion, in the host's frames): the host's
// game plays them on the copy where it is, the doll's run on into him at the press, and on a slowed host it is 1.3 to 2.5 m behind then
// (cl_w_before f6484 to f6520: its state word still 0x5002 and 0x205002, 0.2 m in 300 ms, the kick missed 2.18 m away when HeldBack set
// the doll back; wakeramp_w_after: 20 such)
bool BlowHoldsCopy(const Body& b) { return b.wakeCombo >= 0 || (b.wakeDoneAt >= 0 && gFrame - b.wakeDoneAt < 30); }
int gFreeN[16], gFreeHolds[16];                       // (the debug count: the reads beta 25 would have taken for a hold)
float gFreeRan[16];
void CopyFreeSay(const Self& s, const PlayerNow& me, float step, const char* why) {
    const int k = s.k & 15;
    gFreeRan[k] = gFreeN[k] ? gFreeRan[k] + step : step;
    if (++gFreeN[k] < 3) return;
    gFreeN[k] = 0;
    if (gFreeRan[k] >= 300.f) return;
    ++gFreeHolds[k];
    if (gDebug) BLOG("vdoll %d: its copy %.1f m behind it at %.0f %.0f %.0f, still across, %s (state %x): not held (%d times, MGO1_VDOLL_COPY_LEASH)", s.k + 1,
                     Dist2D(me.pos, s.pos) / 1000.f, me.pos[0], me.pos[1], me.pos[2], why, me.state, gFreeHolds[k]);
}
void HeldBack(Body& b, const Self& s) {
    const PlayerNow& me = gSeen[s.slot];
    const float step = b.copyHave ? Dist2D(me.pos, b.copyPrev) : 0.f;
    b.copyHave = gHave[s.slot] && me.object;
    memcpy(b.copyPrev, me.pos, sizeof b.copyPrev);
    if (RollHoldsCopy(b)) { b.heldBack = 0; return; }   // ((v150-host beta 25, rolltime) its host copy still in its roll's motion)
    // ((v150-host beta 25, copyleash) its copy off its floor after the game's put: no hold while it falls and lands (kCopyLanding frames),
    // nor while it plays the doll's own blows (BlowHoldsCopy)
    const bool falls = gCopyLeash && gHave[s.slot] && me.object && CopyFalls(me);
    if (falls) gCopyFellAt[s.k & 15] = gFrame;
    if (!gHave[s.slot] || !me.object || !me.alive || b.aiming || b.next >= b.path.size() || gFrame - b.lifeAt < 120 || !(b.boxOff ? Number(me.pos) : OnMap(me.pos)) ||
        Dist2D(me.pos, s.pos) < 1200.f || std::fabs(me.pos[1] - s.pos[1]) > 1500.f) { b.heldBack = 0; gFreeN[s.k & 15] = 0; return; }
    if (gCopyLeash && gFrame - gCopyFellAt[s.k & 15] < kCopyLanding) { CopyFreeSay(s, me, step, "falling or landing after the game's put"); b.heldBack = 0; return; }   // ((copyleash))
    if (gCopyLeash && BlowHoldsCopy(b)) { CopyFreeSay(s, me, step, "in its blows' motion"); b.heldBack = 0; return; }   // ((copyleash) a wake's combo or throw)
    gFreeN[s.k & 15] = 0;
    b.copyRan = b.heldBack ? b.copyRan + step : step;
    if (++b.heldBack < 3) return;
    b.heldBack = 0;
    const float gap = Dist2D(me.pos, s.pos), ux = (s.pos[0] - me.pos[0]) / gap, uz = (s.pos[2] - me.pos[2]) / gap;
    // (v150-host beta 14) the copy still moving (along a wall: Leashed), not held
    if (b.copyRan >= 300.f) {
        ++b.lates;
        if (gDebug && b.lates % 20 == 1) BLOG("vdoll %d: its copy %.1f m behind it but moving (%.1f m in 300 ms): not held (%d times)", s.k + 1, gap / 1000.f, b.copyRan / 1000.f, b.lates);
        return;
    }
    ++b.holds;
    // (v150-host beta 14) another player's body against it: not a prop, the place not kept
    int by = -1;
    for (int id = 0; id < 16 && by < 0; ++id)
        if (id != s.slot && gHave[id] && gSeen[id].object && gSeen[id].alive && Dist2D(gSeen[id].pos, me.pos) < 1300.f &&
            std::fabs(gSeen[id].pos[1] - me.pos[1]) < 1500.f) by = id;
    if (by >= 0) {
        if (gDebug) BLOG("vdoll %d: held back by player %d at %.0f %.0f %.0f, %.1f m behind it: on from there", s.k + 1, by + 1, me.pos[0], me.pos[1], me.pos[2], gap / 1000.f);
    } else {
        const std::array<float, 3> at = {me.pos[0] + ux * 600.f, me.pos[1] - kHips, me.pos[2] + uz * 600.f};
        const auto once = std::find_if(gHeldOnce.begin(), gHeldOnce.end(), [&](const std::array<float, 3>& h) {
            return std::hypot(h[0] - at[0], h[2] - at[2]) < 1500.f && std::fabs(h[1] - at[1]) < 1500.f; });
        if (once != gHeldOnce.end()) {
            gHeldOnce.erase(once);
            if (gHeld.size() >= 64) gHeld.erase(gHeld.begin());
            gHeld.push_back(at);
            BLOG("vdoll %d: held back at %.0f %.0f %.0f, %.1f m behind it, the second time there: the ways keep off it", s.k + 1, me.pos[0], me.pos[1], me.pos[2], gap / 1000.f);
        } else {
            if (gHeldOnce.size() >= 64) gHeldOnce.erase(gHeldOnce.begin());
            gHeldOnce.push_back(at);
            if (gDebug) BLOG("vdoll %d: held back at %.0f %.0f %.0f, %.1f m behind it (its copy %.1f m in 300 ms): on from there", s.k + 1, me.pos[0], me.pos[1],
                             me.pos[2], gap / 1000.f, b.copyRan / 1000.f);
        }
    }
    s.pos[0] = me.pos[0]; s.pos[1] = me.pos[1]; s.pos[2] = me.pos[2];
    b.floorY = me.pos[1] - kHips;
    b.path.clear();
    b.pauseUntil = gFrame + 12;
}

// ---- seeing: the enemies in its sight and field with a clear line from its eye to their chest or head ----
void Eye(const Self& s, float* e) { e[0] = s.pos[0]; e[1] = s.pos[1] + kEye; e[2] = s.pos[2]; }
const float kLeanSide = 490.f, kLowEye = 300.f;
// its eye as its stance has it (crouched lower, leaning aside of the facing yaw)
void EyePose(const Self& s, bool low, int lean, float yaw, float* e) {
    e[0] = s.pos[0] + std::cos(yaw) * kLeanSide * (float)lean;
    e[1] = s.pos[1] + (low ? kLowEye : kEye);
    e[2] = s.pos[2] - std::sin(yaw) * kLeanSide * (float)lean;
}
void EyeNow(const Body& b, const Self& s, float* e) { EyePose(s, b.low, b.lean, b.yaw, e); }
void StandUp(Body& b) { if (b.low) { b.low = false; b.standUntil = gFrame + 18; } }
// (beta 21, geom) the game's things too. *beyond: his chest and his head are both hidden by a thing of the game more than
// a lean's reach from the eye (a lean moves the eye 0.49 m: it looks round what is near, a tree trunk 20 m away is not)
const float kLeanReach = 5000.f;
bool Visible(const float* eye, const PlayerNow& o, bool* beyond = nullptr) {
    float c[3], h[3];
    Chest(o, c); Head(o, h);
    if (SightClear(eye, c)) return true;
    const float first = gThingAt;
    if (SightClear(eye, h)) return true;
    if (beyond) *beyond = first > kLeanReach && gThingAt > kLeanReach;
    return false;
}
// (beta 21, geom) a fight's own look from its crouched eye, every frame: the bodies' collision every frame as before, the
// game's things from the body's memo (asked again after 6 frames, or when an end moved 100 mm)
bool VisibleLow(Body& b, const float* low, const PlayerNow& o) {
    float pts[2][3];
    Chest(o, pts[0]); Head(o, pts[1]);
    for (int k = 0; k < 2; ++k) {
        if (SmokeBetween(low, pts[k]) || !LineClear(low, pts[k])) continue;   // (beta 22, blast: a smoke's cloud)
        Body::ThingMemo& m = b.lowThing[k];
        bool same = gFrame - m.frame < 6 && gFrame >= m.frame;
        for (int a = 0; a < 3 && same; ++a) same = std::fabs(m.a[a] - low[a]) < 100.f && std::fabs(m.b[a] - pts[k][a]) < 100.f;
        if (!same) { m.stopped = ThingStops(low, pts[k], kSightPower); m.frame = gFrame; memcpy(m.a, low, sizeof m.a); memcpy(m.b, pts[k], sizeof m.b); }
        if (!m.stopped) return true;
    }
    return false;
}
// (v150-host beta 16, the configurations' audit) SNEAKING's Snake in his camouflage: seen close by only (3 m, 1.5 m more a
// level up to 5), unless he hurt it a moment ago, as the full dolls see him (doll_brain.cpp Sees); the virtual dolls saw
// him as any soldier, to their sight's end
// (v150-host beta 18) TEAM SNEAKING's sneaker whose stealth camouflage works (the host's copy: his flag 0x96, no alert) the
// same: the defenders' virtual dolls saw him to their sight's end (config_audit.md P4), the playable dolls' eyes as Snake's
// (v150-host beta 22, pickup2: review22b P9) MGO1_VDOLL_GOGGLES_CAMO (1; 0: the goggles change nothing in what a doll sees). The
// rule below is the owner's words, not the game's: the game gives the wearer of the thermal goggles nothing more on a camouflaged
// player than his screen's picture (NewThermalMode's flag [0x23AFD4] bit 0 is read by the model draw routines only; the auto
// aim's score 0x45F790 and the name over a player 0x3D3F70 ask nothing of the goggles; TEAM SNEAKING's own rule, no lock and
// no name on a stealthed sneaker, neither); whether that picture shows a camouflaged player was not seen
bool gGogglesCamo = true;
bool Camouflaged(const Body& b, const Self& s, int id) {
    const PlayerNow& o = gSeen[id];
    if (!(gRule == 4 && (o.cls == 8 || o.life > 200)) && !(gRule == 5 && o.camo)) return false;
    if (id == b.hurtBy && gFrame - b.hurtAt < 90) return false;
    // (v150-host beta 22, pickup) the thermal goggles on: every player drawn as heat on the wearer's screen (the owner: "seeing
    // camouflaged or hidden players"), the camouflage gives no cover (MGO1_VDOLL_GOGGLES_CAMO above)
    if (b.gogglesOn && gGogglesCamo) return false;
    return Dist2D(s.pos, o.pos) > 3000.f + (float)std::min(5, b.level) * 1500.f;
}
bool Blind(const Body& b) { return gFlash && gFrame < b.blindUntil; }   // (beta 22, blast: a stun grenade's white)
void Perceive(Body& b, const Self& s) {
    const Level L = RowOf(b, false);
    float eye[3];
    Eye(s, eye);
    int best = -1;
    float bestD = 1e30f, bestAt = 0;                         // (bestD 0: the player who hurt it first; bestAt his distance)
    int bestSide = 0;
    bool targetSeen = false;
    for (int id = 0; id < 16 && !Blind(b); ++id) {         // (beta 22, blast: white, nobody seen)
        if (id == s.slot || !gHave[id]) continue;
        const PlayerNow& o = gSeen[id];
        if (!o.object || !o.alive || !Enemy(o, s.team)) continue;
        if ((b.role != R_SOLDIER || gDartTest) && Asleep(id)) continue;   // (beta 18: a sleeping enemy is no threat; its darts do nothing more)
        if (gTestTarget > 0 && id + 1 != gTestTarget) continue;   // (the test switch: that player only)
        if (id == b.ignore && gFrame < b.ignoreUntil && !(id == b.hurtBy && gFrame - b.hurtAt < 120)) continue;   // (v150-host beta 21: a target given up, Engage)
        float c[3];
        Chest(o, c);
        const float d = Dist3(eye, c);
        if (d > L.sees * 1000.f || Camouflaged(b, s, id)) continue;
        const float off = std::fabs(Wrap(Bearing(s.pos, o.pos) - b.yaw)) * 180.f / kPi;
        if (id != b.target && d > 6000.f && off > L.field / 2.f && !(id == b.hurtBy && gFrame - b.hurtAt < 120)) continue;
        int side = 0;
        bool beyond = false;
        if (!Visible(eye, o, &beyond)) {             // (beta 15) from level 4 a lean round the corner: its eye 490 mm aside
            if (b.level < 4 || beyond) continue;     // (beta 21, geom: not round a thing out of a lean's reach)
            const float yaw = Bearing(s.pos, o.pos);
            for (int sd : {1, -1}) {
                float le[3];
                EyePose(s, false, sd, yaw, le);
                if (SightClear(eye, le) && Visible(le, o)) { side = sd; break; }
            }
            if (!side) continue;
        }
        if (id == b.target) { targetSeen = true; b.leanWant = side; memcpy(b.tpos, o.pos, sizeof b.tpos); memcpy(b.tvel, gVel[id], sizeof b.tvel); b.tposAt = gFrame; }
        if (d < bestD || (id == b.hurtBy && gFrame - b.hurtAt < 120)) { bestD = id == b.hurtBy ? 0.f : d; bestAt = d; best = id; bestSide = side; }
    }
    if (b.target >= 0) {
        const PlayerNow& t = gSeen[b.target];
        if (!gHave[b.target] || !t.object || !t.alive) {         // down (or gone)
            if (gFrame - b.hitAt < 180) {
                BLOG("vdoll %d: player %d down, %d shots and %d hits in this fight", s.k + 1, b.target + 1, b.fightShots, b.fightHits);
                // (and heavy's RPG-7 or flamethrower from a box; pickup2: the special weapon of that hit, one spent before another taken)
                const int sw = b.specialHitW >= 0 ? b.specialHitW : b.special >= 0 ? b.special : b.heavyW >= 0 && !b.heavyBench ? b.heavyW : -1;
                if (sw >= 0 && !(sw == b.heavyW && b.heavyBench) && gFrame - b.specialHitAt < 180) {   // (v150-host beta 22, pickup: a kill with the special item)
                    ++b.specialKills; ++gPick.kills;
                    BLOG("vdoll %d: player %d down by its %s (a special item it took)", s.k + 1, b.target + 1, kWeapons[sw].name);
                }
                if (gKillHoldMs > 0 && !(gOwnAt[b.target] >= 0 && gFrame - gOwnAt[b.target] < 600)) { b.killHoldUntil = RealMs() + (unsigned long long)gKillHoldMs; b.killAt = gFrame; }   // (beta 21, cover)
            } else if (gDebug) BLOG("vdoll %d: player %d no longer in play", s.k + 1, b.target + 1);
            b.target = -1;
            b.path.clear();
        } else if ((b.role != R_SOLDIER || gDartTest) && Asleep(b.target)) {     // (beta 18) asleep or stunned: left there, as Snake leaves him
            if (gFrame - b.hitAt < 180) {
                ++b.sleeps;
                BLOG("vdoll %d: player %d asleep, %d shots and %d hits in this fight (%s)", s.k + 1, b.target + 1, b.fightShots, b.fightHits,
                     gSeen[b.target].asleep ? "the host's game holds him knocked out" : "its darts emptied his stamina");
            } else if (gDebug) BLOG("vdoll %d: player %d knocked out: left there", s.k + 1, b.target + 1);
            b.target = -1;
            b.aiming = false;
            b.path.clear();
        } else if (targetSeen) {
            if (gFrame - b.seenAt > 20) { b.firstSeen = gFrame - L.react / 2; b.sightMs = RealMs() - (unsigned long long)(gReflexMs / 2); }   // back in sight: half a reaction (beta 21: half the reflex)
            b.seenAt = gFrame;
        } else if (gFrame - b.seenAt > 90) {                       // lost: the gun down, to where it was last seen (or on)
            if (gDebug || b.fightShots) BLOG("vdoll %d: lost sight of player %d (%d shots, %d hits)", s.k + 1, b.target + 1, b.fightShots, b.fightHits);
            const bool chase = Rnd01() < 0.5f + 0.05f * b.level;
            b.target = -1;
            b.path.clear();
            if (chase) PlanTo(b, s, b.tpos);
        }
    }
    if (best >= 0) b.foeAt = gFrame;                  // (v150-host beta 22, wake: an enemy in its sight)
    if (b.target < 0 && best >= 0 && !b.carrying) {    // (the item in its hands: no weapon, on to the goal)
        b.target = best; b.firstSeen = b.seenAt = gFrame; b.fightShots = b.fightHits = 0; b.onSince = -1; ++b.fights; b.leanWant = bestSide;
        b.sightMs = RealMs();                            // (v150-host beta 21, cover: the reflex counts from here)
        b.shownRead = b.shownSince = -1; b.shownFrom = 0;
        // (v150-host beta 16) the game's headshot rule: only a headshot counts, so the head always (as the full dolls,
        // doll.cpp HeadshotsOnly)
        b.head = Rnd01() < L.head || (gHost.headshotsOnly && gHost.headshotsOnly());
        memcpy(b.tpos, gSeen[best].pos, sizeof b.tpos); memcpy(b.tvel, gVel[best], sizeof b.tvel); b.tposAt = gFrame;
        BLOG("vdoll %d: sees player %d at %.1f m%s", s.k + 1, best + 1, bestAt / 1000.f, gSeen[best].camo ? " (camouflaged)" : "");
    }
}

// ---- a bullet: along the view with the level's spread; the target's parts (the head, the body, the legs) against the
// map's collision; an ally near the line holds the fire ----
// the nearest point of a ray to a segment: the ray's distance there (t), the gap
float RaySegment(const float* o, const float* d, const float* a, const float* b, float* t) {
    float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    const float A = d[0] * d[0] + d[1] * d[1] + d[2] * d[2], B = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    const float C = u[0] * u[0] + u[1] * u[1] + u[2] * u[2], D = d[0] * w[0] + d[1] * w[1] + d[2] * w[2];
    const float E = u[0] * w[0] + u[1] * w[1] + u[2] * w[2];
    const float den = A * C - B * B;
    float sc, tc;
    if (C <= 1e-6f) { tc = 0.f; sc = std::max(0.f, -D / A); }      // (a point: the head)
    else {
        sc = den > 1e-6f ? std::max(0.f, (B * E - C * D) / den) : 0.f;
        tc = (E + sc * B) / C;
        if (tc < 0.f) { tc = 0.f; sc = std::max(0.f, -D / A); }
        else if (tc > 1.f) { tc = 1.f; sc = std::max(0.f, (B - D) / A); }
    }
    float g[3];
    for (int k = 0; k < 3; ++k) g[k] = o[k] + d[k] * sc - (a[k] + u[k] * tc);
    *t = sc;
    return std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
}
int HitPart(const float* o, const float* d, const PlayerNow& s, float* t, float grow = 0.f) {   // ((v150-host beta 24, cqcfight) grow: each piece's radius more, mm)
    // (over the hips, standing: the head 730 to 970, the body -140 to 640, the legs -790 to -60; crouched 540 lower)
    // (v150-host beta 21, geom) lying: the head -50 to 130, the body -310 to 110 about his hips' point, no leg apart (his
    // body along the ground: nothing of him higher than 130 mm over his hips' point, 0.43 m over the floor)
    const int stance = StanceOf(s);
    const float low = stance == S_CROUCH ? -540.f : 0.f;
    struct Piece { Part part; float y0, y1, r; } pieces[3] = {
        {P_HEAD, 850.f + low, 850.f + low, 120.f}, {P_BODY, 80.f + low * 0.55f, 420.f + low, 220.f}, {P_LEG, -630.f, low < 0 ? -450.f : -220.f, 160.f}};
    if (stance == S_LIE) {
        pieces[0] = {P_HEAD, kHeadUp[S_LIE], kHeadUp[S_LIE], 90.f}; pieces[1] = {P_BODY, -150.f, -50.f, 160.f}; pieces[2] = {P_LEG, -100.f, -100.f, 0.f};
    }
    int best = P_NONE;
    float bestT = 1e30f;
    for (const Piece& p : pieces) {
        const float a[3] = {s.pos[0], s.pos[1] + p.y0, s.pos[2]}, b[3] = {s.pos[0], s.pos[1] + p.y1, s.pos[2]};
        float tt;
        const float gap = RaySegment(o, d, a, b, &tt), r = p.r + grow;
        if (gap > r) continue;
        tt -= std::sqrt(std::max(0.f, r * r - gap * gap));
        if (tt > 0.f && tt < bestT) { bestT = tt; best = p.part; }
    }
    *t = bestT;
    return best;
}
const float kAllyFrom = 600.f;                   // (beta 21) the line of fire from its muzzle on, ahead of its eye (mm)
// (v150-host beta 24, cqcfight) his own body on the line of fire before the point aimed at: his head, body and legs as a bullet meets
// them (HitPart's pieces by his stance), each grown by 50 mm and by the doll's own spread at his distance (1.7 times its level's error,
// err in radians: twice its bullets' sigma, Shoot's 0.7 and 0.5 of it; a bullet that meets him all the same stops there, AllyStops)
bool AllyBodyMeets(const float* o, const float* d, float until, float err, const PlayerNow& a) {
    const float along = std::max(0.f, (a.pos[0] - o[0]) * d[0] + (a.pos[1] - o[1]) * d[1] + (a.pos[2] - o[2]) * d[2]);
    float t;
    return HitPart(o, d, a, &t, 50.f + 1.7f * std::max(0.f, err) * along) != P_NONE && t < until;
}
// a teammate (not an enemy, not the host) alive in the world
bool Mate(const Self& s, int id) {
    return id != s.slot && id >= 0 && id < 16 && gHave[id] && gSeen[id].object && gSeen[id].alive && !Enemy(gSeen[id], s.team) && gSeen[id].team != 0xFE && gSeen[id].team != 0xFF;
}
// (v150-host beta 24, cqcfight) enemy id within 1.5 m of a teammate of the doll (holding him, throwing him, punching him, standing over
// him): in a close fight with him
bool AtMate(const Self& s, int id) {
    if (id < 0 || id >= 16 || !gHave[id] || !gSeen[id].object) return false;
    for (int m = 0; m < 16; ++m)
        if (m != id && Mate(s, m) && Dist2D(gSeen[m].pos, gSeen[id].pos) < 1500.f && std::fabs(gSeen[m].pos[1] - gSeen[id].pos[1]) < 1000.f) return true;
    return false;
}
bool AllyInLine(const Self& s, const float* o, const float* d, float until, float err = -1.f) {
    // (v150-host beta 21) the line from the muzzle (kAllyFrom ahead of the eye), an ally held only when the line's nearest point
    // to him lies ahead of it (within 700 mm), or the line starts within 300 mm of him (in front of the muzzle). The ray's
    // nearest point was clamped at the eye: an ally within 700 mm of its eye in any direction (beside it, behind it) held its
    // fire, and it held his, for ever (runs/V20_cap9: two allies 92 mm apart, 446 s aiming with no shot)
    float m[3] = {o[0], o[1], o[2]};
    if (gStandoff) for (int k = 0; k < 3; ++k) m[k] += d[k] * kAllyFrom;
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || Enemy(gSeen[id], s.team)) continue;
        if (gSeen[id].team == 0xFE || gSeen[id].team == 0xFF) continue;
        float c[3], t;
        Chest(gSeen[id], c);
        const float top[3] = {c[0], c[1] + 500.f, c[2]}, bottom[3] = {c[0], c[1] - 900.f, c[2]};
        if (!gStandoff) { if (RaySegment(o, d, bottom, top, &t) < 700.f && t < until) return true; continue; }
        const float gap = RaySegment(m, d, bottom, top, &t);
        if (t < until - kAllyFrom && ((t > 0.f && gap < 700.f) || gap < 300.f)) {
            // ((v150-host beta 24, cqcfight) beta 23 held its fire for a teammate within 0.7 m of its line, his height from 0.9 m under his
            // chest to 0.5 m over it: a teammate held by the enemy, thrown at his feet or lying there, or beside him, held it 1.5 to 5 s
            // (tests/vdoll_cqcfight_test.cpp), the line clear past him; now only his own body, grown by its aim's spread, on the line)
            if (!gCqcFight || err < 0.f || AllyBodyMeets(o, d, until, err, gSeen[id])) return true;
            ++gCqcFightCount[s.k & 15].freed;
        }
    }
    return false;
}
// ---- (v150-host beta 18) the virtual dolls "as a player" (MGO1_VDOLL_CONTROLS=player, docs/VIRTUAL_DOLL.md 17): its level's own
// decisions (sight, field, reaction, error, tolerance, settling, the head's share, bursts, grenades, CQC, rolls), the pad's
// control path (docs/PLAYABLE_DOLL.md 3, 17 to 19, 25): the readied flag 4 to 7 frames after Square, the readied turn (a
// part of what is left each frame, 3 to 16 degrees), the first person view entered 16 to 50 frames after R1 and turned 1
// to 2 degrees a frame, a bullet 3 frames after the pull, the third person's bullets on the body (the head only from the
// view), the view's recoil (about a degree, 3 to 7 frames after a bullet), a bullet a pull in the view at range. Off: none
// of this runs (Fight as before) ----
bool gPlayerCtl = false;
enum Ctl { C_LOWERED, C_ARMING, C_READIED, C_ENTERING, C_VIEW };
const int kArmMin = 4, kArmMax = 7, kEntryMin = 16, kEntryMax = 50, kPullFrames = 3, kOnePullGap = 10;
const float kTpsPart = 0.11f, kTpsMin = 3.f, kTpsMax = 16.f;                // the readied turn (degrees a frame; 60 degrees to 1.5 in 16 frames)
const float kViewV0 = 0.445f, kViewMore = 0.0575f, kViewMax = 2.f;            // the view's turn (degrees a frame: 0.89, 0.23 more, 4 a 2-frame tick)
const float kViewFaced = 6.f, kViewOff = 25.f;                               // into the view faced within, out beyond (degrees)
const float kViewFrom = 10000.f, kViewNear = 7000.f, kOnePullFrom = 12000.f; // the view beyond, out within; a bullet a pull from (mm)
const float kMuzzle = 380.f;                                                 // the muzzle over the hips (its bullet's height, mm)
struct CtlCount { int entries = 0, tpsHits = 0, viewHits = 0, viewHeads = 0, bodyNotHead = 0; };
CtlCount gCtlCount[16];
// a bullet's part as the controls allow it: the third person's (readied, not the view) on the body, never the head
void PlayerPart(const Body& b, const Self& s, int* part) {
    CtlCount& c = gCtlCount[s.k & 15];
    if (b.ctl == C_VIEW) { ++c.viewHits; if (*part == P_HEAD) ++c.viewHeads; return; }
    ++c.tpsHits;
    if (*part == P_HEAD) { *part = P_BODY; ++c.bodyNotHead; }
}
// ---- (v150-host beta 24, walls3) MGO1_VDOLL_MUZZLE (on; 0: as beta 23). A player's bullet from the third person leaves his
// gun's muzzle, 0.45 m ahead of his hips along his facing and 0.38 m over them, and a wall between stops it (docs/PLAYABLE_DOLL.md
// 3); a gun against a wall does not fire. Beta 18 to 23's dolls "as a player" tested that bullet from their eye, 0.75 m over the
// hips (geom's note, reports/geom_wallbang.md: not done while the owner's server ran the direct controls; it runs the player's
// since 13 November): standing behind a cover between those heights (a crate, a sill, sandbags), or crouched behind a lower one,
// a doll shot over it where a player's gun hits its top, and on the victim's screen its copy's own bullets struck the cover in
// front of it while his life went down: "they shoot through walls". Now the third person's bullet must also pass from the hips'
// column to the muzzle and from the muzzle to the point it meets (the map's collision, the things as hard as the bullet or
// harder); and a doll whose muzzle's line to him is stopped takes its first person view, whose bullet leaves by its eye, as a
// player raises his view to shoot over a cover (the playable dolls' rule, docs/PLAYABLE_DOLL.md 18). Crouched, its muzzle as
// much lower as its eye (not measured on a real client). The view's own gun (0.15 m under the eye) is not tested ----
bool gMuzzle = true;
const float kMuzzleAhead = 450.f;                                            // (the muzzle ahead of the hips along the facing, mm)
const float kMuzzleLow = kMuzzle - (kEye - kLowEye);                         // (crouched: as much lower as its eye, -70 mm)
bool MuzzleOn(const Body& b) { return gMuzzle && gPlayerCtl && b.ctl != C_VIEW; }
// its muzzle now (its facing yaw) and the point on its hips' column at the muzzle's height
void MuzzleAt(const Body& b, const Self& s, float yaw, float* m, float* col) {
    const float up = b.low ? kMuzzleLow : kMuzzle;
    col[0] = s.pos[0]; col[1] = s.pos[1] + up; col[2] = s.pos[2];
    m[0] = s.pos[0] + std::sin(yaw) * kMuzzleAhead; m[1] = col[1]; m[2] = s.pos[2] + std::cos(yaw) * kMuzzleAhead;
}
// the third person's bullet of that power from its muzzle to the point `to`: nothing of the map nor a thing as hard as it between
bool MuzzleClear(const Body& b, const Self& s, float yaw, const float* to, int power) {
    float m[3], col[3];
    MuzzleAt(b, s, yaw, m, col);
    if (!LineClear(col, m) || !LineClear(m, to)) return false;
    return !ThingStops(col, m, power) && !ThingStops(m, to, power);
}
// (v150-host beta 21, cover) MGO1_VDOLL_SHOWN: something of the doll's own copy (the host's, as the players were last read)
// is in player id's line: from his eye at the place he last sent (his copy's, with no record) to its chest or head by its
// stance, or to its leaning eye
bool CopyInLine(const Body& b, const Self& s, int id) {
    const PlayerNow& t = gSeen[id];
    const RecNow& r = gRec[id];
    const float* hp = r.have && Dist2D(r.p, t.pos) <= 4000.f ? r.p : t.pos;
    // (at the merge with geom: his eye by his stance, lying too; the things of the game hide the doll's copy from him as walls)
    const int st = StanceOf(t);
    const float he[3] = {hp[0], hp[1] + (st == S_LIE ? kHeadUp[S_LIE] : st == S_CROUCH ? kLowEye : kEye), hp[2]};
    const float* me = gSeen[s.slot].pos;
    const float ch[3] = {me[0], me[1] + (b.low ? 50.f : 350.f), me[2]}, hd[3] = {me[0], me[1] + (b.low ? 310.f : 850.f), me[2]};
    if (SightClear(he, ch) || SightClear(he, hd)) return true;
    if (!b.lean) return false;                           // (leaning: its eye aside of its copy, across its line to him)
    const float yaw = Bearing(me, hp);
    const float le[3] = {me[0] + std::cos(yaw) * kLeanSide * (float)b.lean, me[1] + kEye, me[2] - std::sin(yaw) * kLeanSide * (float)b.lean};
    return SightClear(he, le);
}
// (v150-host beta 21, cover) the gate. A player with no record of his own (a virtual doll: its copy follows its body) or a
// record of another life (4 m from his copy) is not judged: as before
float LineUntil(const float* a, const float* b) { float wall = 1.f, n[3]; ++gWorkPass.sight; return gMap->nav->FirstHit(a, b, &wall, n) && wall < 0.999f ? wall : 1.f; }
bool LineFree(const float* a, const float* b) { return LineUntil(a, b) >= 1.f; }
// (v150-host beta 21, at the merge of cover and geom) the gate's lines of sight also stop at a thing the host's game made while
// the round runs (a tree trunk, a prop, a cardboard box: geom's ThingStops, the sight's rule, glass passes): a place behind
// one is hidden as a place behind a wall. Only on the game's thread with a game map (else as the mesh alone). The walk's
// line (can he be there) stays the mesh's
// (the review: the game's line test asked 24 times at most for one verdict; the rest of its lines the mesh's alone. Measured
// at the merge: 2 calls a verdict at the median, 124 for one in a hundred, 211 at most, 10 to 144 us each)
int gCoverThings = 24;
const int kCoverThingsMax = 24;
float CoverUntil(const float* a, const float* b) {
    const float t = LineUntil(a, b);
    if (t < 1.f || gCoverThings <= 0) return t;
    --gCoverThings;
    if (!ThingStops(a, b, kSightPower)) return t;
    const float len = Dist3(a, b);
    return len > 1.f ? std::max(0.f, std::min(0.999f, gThingAt / len)) : 0.f;
}
bool CoverFree(const float* a, const float* b) { return CoverUntil(a, b) >= 1.f; }
// (v150-host beta 24, walls3) MGO1_VDOLL_REACH_THINGS (on; 0: as beta 23, the 24 game lines a verdict). His reach's places
// are tested on lines from the eye across it (ReachHidden). Beta 23 asked the game for each place whose own line the mesh
// found clear, 24 a verdict at most, the rest the mesh's alone: a player running behind a thing of the game (a crate, a
// trunk, a vehicle) in the open, where every line is clear on the mesh, spent them on the lines nearest his record, and
// the places behind the thing were never asked (tests/vdoll_cover_test.cpp VDOLL_TEST_THING=1: 16 of 24 damage records
// reached him behind it at 80 ms a way, 28 of 38 at 150 ms). Now each line from the eye asks the game once, at its first
// place the mesh finds clear (a line of 2.5 m reach: 33 lines at most), and its nearer places take that answer by their
// distance along the floor: hidden when the thing is nearer than they are. The mesh is still asked for every place
// that is not on the line's own height (a slope, a stair). lineThing: -2 not asked yet, -1 no thing, else the thing's
// distance from the eye along the floor (mm)
bool gReachThings = true;
float ReachUntil(const float* a, const float* b, float* lineThing) {
    const float t = LineUntil(a, b);
    if (t < 1.f) return t;
    const float flat = Dist2D(a, b), len = Dist3(a, b);
    if (*lineThing < -1.5f) {
        *lineThing = -1.f;
        if (ThingStops(a, b, kSightPower) && gThingAt >= 0.f) *lineThing = len > 1.f ? gThingAt / len * flat : 0.f;
    }
    if (*lineThing < 0.f || *lineThing >= flat - 50.f) return 1.f;
    return std::max(0.f, std::min(0.999f, *lineThing / std::max(1.f, flat)));
}
// a target the gate is for: a real player (a playable doll too), not a virtual doll's own copy
bool Gated(int id) { return (gCover != COVER_OFF || gCoverSee) && id >= 0 && id < 16 && !(gOwnAt[id] >= 0 && gFrame - gOwnAt[id] < 600); }
// his record is known now (a first record after his spawn comes a moment later: a hit waits for its records' tick either way)
bool Judged(int id) { return Gated(id) && gRec[id].have && Dist2D(gRec[id].p, gSeen[id].pos) <= 4000.f; }
// the part's height over the hips (the copy's stance): the head, the chest, the legs
// (at the merge with geom: his stance as the bodies hold it now, lying too: kChestUp, kHeadUp; a lying player's legs along the
// ground, HitPart's -100)
float PartUp(int part, int stance) { return part == P_HEAD ? kHeadUp[stance] : part == P_LEG ? (stance == S_LIE ? -100.f : -400.f) : kChestUp[stance]; }
struct CoverSay { int why = 0, rays = 0, places = 0, ways = 0; float ms = 0, reach = 0, age = 0, at[3] = {0, 0, 0}; int run = -1, all = -1; };   // (run, all: MGO1_VDOLL_COVER_LOG=2, what each form says)
// the time his own game runs on from the place he last sent to the hit's arrival there (ms): his record's age here, the
// network both ways, the frames this hit still waits here (its record tick, the host's game passing it on)
float CoverMs(int id, double age, float frames) {
    const int rtt = gSeen[id].rtt;
    const float both = gNetMs >= 0.f ? 2.f * gNetMs : rtt > 0 ? (float)std::min(1000, rtt) : 2.f * kNetNone;
    return (float)age + both + (float)((double)(frames + gRelayFrames) * gMsAFrame);
}
// he moves: his records say so (stopped 250 ms: he stands where the last one is), or the host's copy runs (his input came)
bool Moving(int id, double age) {
    const float copy = std::hypot(gVel[id][0], gVel[id][2]) / (float)gMsAFrame;
    const float rec = age < 250.0 ? std::hypot(gRec[id].v[0], gRec[id].v[2]) : 0.f;
    return std::max(copy, rec) > 0.4f;                   // (mm a ms: 0.4 m a second)
}
// "run": the part out of the eye's line somewhere on his way straight on (his records' speed, T ms)
bool RunHidden(const float* eye, const RecNow& r, float up, float T, CoverSay* say) {
    float run[3] = {r.v[0] * T, 0.f, r.v[2] * T};
    const float len = std::hypot(run[0], run[2]);
    if (len > kAheadMax) { run[0] *= kAheadMax / len; run[2] *= kAheadMax / len; }
    say->reach = std::min(len, kAheadMax);
    const int steps = (int)std::ceil(std::min(len, kAheadMax) / kAlongStep);
    for (int i = 1; i <= steps; ++i) {                   // (all along his run: a pillar between)
        const float k = (float)i / (float)steps;
        const float p[3] = {r.p[0] + run[0] * k, r.p[1] + up, r.p[2] + run[2] * k};
        const float q[3] = {p[0], p[1] - 150.f, p[2]};   // (a slope on his way, the part a little lower: the edge of the cover)
        say->rays += 2;
        if (!CoverFree(eye, p) || !CoverFree(eye, q)) { memcpy(say->at, p, sizeof say->at); return true; }
    }
    return false;
}
// his reach: a place he can have run to within R (the mesh's, by its ways: straight on it, or a way round a corner) where
// the part is out of the eye's line. The places: on lines from the eye across his reach, kLineStep apart (a cover's shadow
// runs along them), every kDepthStep along each from its far end; a line's far place tells for the nearer ones at its
// height (clear to it: clear before it; a wall met: hidden behind it), the others have their own line
bool ReachHidden(const float* eye, const float* rec, float up, float R, CoverSay* say) {
    const doll::NavMesh& nav = *gMap->nav;
    const float feet[3] = {rec[0], rec[1] - kHips, rec[2]};
    float start[3];
    const bool onMesh = nav.Nearest(feet, 400.f, start);
    const float w0[2] = {rec[0] - eye[0], rec[2] - eye[2]};
    const float dist = std::hypot(w0[0], w0[1]);
    say->reach = R;
    if (dist < R + 500.f) return false;                  // (at arm's length: no cover between them)
    const float yaw0 = std::atan2(w0[0], w0[1]);
    const int n = (int)std::floor(R / kLineStep);
    int ways = 0;
    for (int i = 0; i <= 2 * n; ++i) {                   // (the middle line first, then out on both sides)
        const int j = ((i + 1) / 2) * ((i & 1) ? 1 : -1);
        const float side = (float)j * kLineStep;         // (across, at his distance)
        const float yaw = yaw0 + std::asin(std::max(-1.f, std::min(1.f, side / dist)));
        const float d[2] = {std::sin(yaw), std::cos(yaw)};
        const float mid = std::sqrt(std::max(0.f, dist * dist - side * side)), half = std::sqrt(std::max(0.f, R * R - side * side));
        bool have = false;                               // (this line's far place: its part, how far its line went)
        float farQ[3] = {0, 0, 0}, farRho = 0.f, farT = 1.f;
        float lineThing = -2.f;                          // (v150-host beta 24, walls3: this line's thing of the game, ReachUntil)
        for (float rho = mid + half; rho >= mid - half - 1.f; rho -= kDepthStep) {
            const float p[3] = {eye[0] + d[0] * rho, feet[1], eye[2] + d[1] * rho};
            float m[3];
            if (!nav.Nearest(p, kSnap, m) || std::hypot(m[0] - p[0], m[2] - p[2]) > kSnap) continue;
            ++say->places;
            const float q[3] = {p[0], rec[1] + up + (onMesh ? m[1] - start[1] : 0.f), p[2]};
            bool hidden;
            const float yLine = have ? eye[1] + (farQ[1] - eye[1]) * (rho / farRho) : 0.f;
            if (have && std::fabs(yLine - q[1]) <= 80.f) hidden = rho > farT * farRho - 50.f;
            else {
                ++say->rays;
                const float t = gReachThings ? ReachUntil(eye, q, &lineThing) : CoverUntil(eye, q);
                hidden = t < 1.f;
                if (!have) { have = true; memcpy(farQ, q, sizeof farQ); farRho = rho; farT = t; }
            }
            if (!hidden) continue;
            // can he be there: straight on the mesh, or by a way no longer than his reach (a few asked a hit at most)
            bool can;
            if (!onMesh) { const float h[3] = {p[0], q[1] - up, p[2]}; can = LineFree(rec, h); }
            else if (nav.Walkable(start, m)) can = true;
            else if (ways < 6 && std::hypot(m[0] - start[0], m[2] - start[2]) <= R * 0.9f) {
                std::vector<doll::Waypoint> way;
                float len = 0.f;
                ++ways; ++say->ways; gWorkPass.sight += 8;
                can = nav.Path(start, m, way, &len) && len <= R + kSnap;
            } else can = false;
            if (can) { memcpy(say->at, q, sizeof say->at); return true; }
        }
    }
    return false;
}
// (MGO1_VDOLL_COVER_LOG=2, benches) each judged player's copy and record as the host holds them, every 100 ms
void CoverTrack() {
    for (int id = 0; id < 16; ++id) {
        if (!gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Judged(id)) continue;
        const PlayerNow& t = gSeen[id];
        BLOG("vdoll: copy: player %d t=%llu copy %.0f %.0f %.0f rec %.0f %.0f %.0f age %d rtt %d", id + 1, RealMs(), t.pos[0], t.pos[1], t.pos[2],
             t.sent[0], t.sent[1], t.sent[2], t.sentAge, t.rtt);
    }
}
// (the players are read every 6 frames) his record as it is this frame; true: his copy moved since that read (his input
// came: he moves)
bool Stirred(int id) {
    PlayerNow now;
    if (!gHost.see || !gHost.see(id, &now) || !now.object || !now.alive) return false;
    SeeRecord(id, now, true);
    return Dist2D(now.pos, gSeen[id].pos) > 25.f && gFrame > gSeenAt;
}
// the verdict for a hit of this part from this eye on player id, its records going `frames` host frames from now:
// 0 it counts, 1 the part out of the eye's line at the place he last sent, 2 somewhere on his run from there
int CoverVerdict(const float* eye, int id, int part, int stance, float frames, bool stirred, CoverSay* say) {
    gCoverThings = kCoverThingsMax;
    const RecNow& r = gRec[id];
    const float up = PartUp(part, stance);
    const double age = std::max(0.0, (double)RealMs() - r.at);
    say->age = (float)age; say->ms = CoverMs(id, age, frames);
    const float atRec[3] = {r.p[0], r.p[1] + up, r.p[2]};
    ++say->rays;
    if (!CoverFree(eye, atRec)) { memcpy(say->at, atRec, sizeof say->at); return say->why = 1; }
    if (!stirred && !Moving(id, age)) return 0;
    if (gCoverLog >= 2) {                                // (benches: both forms' answers in the line, whatever the form in use)
        CoverSay a, b;
        say->run = RunHidden(eye, r, up, age < 250.0 ? say->ms : 100.f, &a) ? 1 : 0;
        say->all = ReachHidden(eye, r.p, up, std::min(kAheadMax, kRunMs * say->ms + kReachMore), &b) ? 1 : 0;
    }
    if (gCover == COVER_RUN) return say->why = RunHidden(eye, r, up, age < 250.0 ? say->ms : 100.f, say) ? 2 : 0;
    return say->why = ReachHidden(eye, r.p, up, std::min(kAheadMax, kRunMs * say->ms + kReachMore), say) ? 2 : 0;
}
void CoverLine(const Body& b, const Self& s, const float* eye, int id, int part, const CoverSay& say, const char* when) {
    if (!gCoverLog) return;
    const RecNow& r = gRec[id];
    const PlayerNow& t = gSeen[id];
    float c[3];
    Now(id, c);
    BLOG("vdoll %d: cover: player %d t=%llu %s copy %.0f %.0f %.0f rec %.0f %.0f %.0f age %.0f rtt %d ms %.0f reach %.0f eye %.0f %.0f %.0f part %d rays %d places %d ways %d: %s%s (%.0f %.0f %.0f) v %.2f %.2f run %d all %d",
         s.k + 1, id + 1, RealMs(), when, c[0], c[1], c[2], r.p[0], r.p[1], r.p[2], say.age, t.rtt, say.ms, say.reach, eye[0], eye[1], eye[2], part, say.rays, say.places, say.ways,
         say.why ? (gCoverSee ? "counted (the gate off), " : "not counted, ") : "hit", say.why == 1 ? "hidden at his record" : say.why == 2 ? "hidden within his reach" : say.why == 3 ? "its own copy out of his line" : "", say.at[0], say.at[1], say.at[2],
         r.v[0], r.v[2], say.run, say.all);
}
// at its shot: the hit counts so far. later (its records wait for their tick: the scope rule): only the place he last sent
// is asked now, the rest when they go (CoverSend); else all of it now, the wait to its tick counted
bool CoverHit(Body& b, const Self& s, const float* eye, const PlayerNow& t, int part, bool later) {
    b.coverNow = false;
    if (!Gated(b.target)) return true;
    const bool wait = later && gScope;
    if (!Judged(b.target)) { b.coverNow = wait; return true; }   // (no record of him yet: asked again when its records go)
    CoverSay say;
    if (wait) {
        gCoverThings = kCoverThingsMax;
        const RecNow& r = gRec[b.target];
        const float up = PartUp(part, StanceOf(t)), atRec[3] = {r.p[0], r.p[1] + up, r.p[2]};
        say.rays = 1; say.age = (float)std::max(0.0, (double)RealMs() - r.at);
        if (!CoverFree(eye, atRec)) { say.why = 1; memcpy(say.at, atRec, sizeof say.at); }
    } else {
        const float frames = gScope ? (float)(std::max(0LL, b.tickAt + 6 - gFrame) + 6) : 0.f;   // (the scope rule: the tick after its shot's)
        CoverVerdict(eye, b.target, part, StanceOf(t), frames, Stirred(b.target), &say);
    }
    const bool drop = say.why != 0 && !gCoverSee;
    b.coverNow = wait && !drop;
    if (drop) { ++b.coverDrops; if (say.why == 1) ++b.coverAtRec; if (say.why >= 1 && say.why <= 3) ++gGate[say.why]; }
    else if (!wait) ++gGate[0];                          // (beta 24, walls3: judged at the shot, a dart or MGO1_VDOLL_SCOPE=0)
    if (say.why || !wait) CoverLine(b, s, eye, b.target, part, say, "shot");
    return !drop;
}
// its hit's two records are waiting (SendHit): judged when they go; true: counted then, not now
bool CoverLater(Body& b, const float* eye, const PlayerNow& t, int part, float at, const char* weapon) {
    if (!b.coverNow || b.pending.size() < 2) return false;
    b.coverNow = false;
    Body::Gate g;
    g.id = ++b.gateIds; g.target = b.target; g.part = part; g.stance = StanceOf(t); g.at = at; g.weapon = weapon; g.late = b.shownFrom == 1;
    memcpy(g.eye, eye, sizeof g.eye);
    b.pending[b.pending.size() - 2].gate = b.pending[b.pending.size() - 1].gate = g.id;
    b.gates.push_back(g);
    return true;
}
// a waiting record of a hit goes now: its hit judged once (its first record), counted and said if it holds
bool CoverSend(Body& b, const Self& s, int gate) {
    auto it = std::find_if(b.gates.begin(), b.gates.end(), [gate](const Body::Gate& g) { return g.id == gate; });
    if (it == b.gates.end()) return true;
    Body::Gate& g = *it;
    if (g.left == 2) {
        CoverSay say;
        const bool stirred = Gated(g.target) && Stirred(g.target);
        const bool known = Judged(g.target);
        if (known) CoverVerdict(g.eye, g.target, g.part, g.stance, 0.f, stirred, &say);
        // (MGO1_VDOLL_SHOWN) and something of its own copy still in his line (its hold's second over: fired all the same)
        if (!say.why && known && gShown && !g.late && gHave[s.slot] && gSeen[s.slot].object && !CopyInLine(b, s, g.target)) say.why = 3;
        g.ok = say.why == 0 || gCoverSee;
        if (!Gated(g.target)) {} else if (!known) ++gGate[gHave[g.target] && gSeen[g.target].alive ? 4 : 5]; else if (g.ok && !say.why) { ++gGate[0]; if (g.late) ++gGateLate; } else if (say.why >= 1 && say.why <= 3) ++gGate[say.why];   // (beta 24, walls3)
        if (known) CoverLine(b, s, g.eye, g.target, g.part, say, "sent");
        if (g.ok) {
            ++b.hits; ++b.fightHits;
            b.hitAt = gFrame;
            BLOG("vdoll %d: hits player %d at %.1f m (%s)", s.k + 1, g.target + 1, g.at / 1000.f, g.weapon);
        } else { ++b.coverDrops; if (say.why == 1) ++b.coverAtRec; }
    }
    const bool ok = g.ok;
    if (--g.left <= 0) b.gates.erase(it);
    return ok;
}
// (v150-host beta 21, cover) MGO1_VDOLL_KILL_HOLD: after its kill of a real player, where it shot from (no enemy seen, not
// hurt since, no item in its hands)
bool KillHold(Body& b) {
    if (gKillHoldMs <= 0 || b.target >= 0 || b.carrying || b.hurtAt > b.killAt || RealMs() >= b.killHoldUntil) return false;
    ++b.killHolds;
    return true;
}
// (v150-host beta 21, cover) MGO1_VDOLL_SHOWN: its shots at a real player held until its own copy has been in his line long
// enough for his game to show it there. Asked at each read of the players: from his eye at the place he last sent (the
// copy's, with no record) to its copy's chest or head by its stance, or its leaning eye
bool CopyHold(Body& b, const Self& s) {
    const int id = b.target;
    if (!gShown || id < 0 || id >= 16 || (gOwnAt[id] >= 0 && gFrame - gOwnAt[id] < 600)) return false;
    if (!gHave[s.slot] || !gSeen[s.slot].object) return false;
    if (b.shownRead != gSeenAt) {
        b.shownRead = gSeenAt;
        if (!CopyInLine(b, s, id)) b.shownSince = -1; else if (b.shownSince < 0) b.shownSince = gFrame;
    }
    const int rtt = gSeen[id].rtt;
    const float way = gNetMs >= 0.f ? gNetMs : rtt > 0 ? (float)std::min(1000, rtt) * 0.5f : kNetNone;
    const unsigned long long now = RealMs();
    if (b.shownSince >= 0 && (float)(gFrame - b.shownSince) >= gRelayFrames + way / (float)gMsAFrame) {
        if (gDebug && b.shownFrom > 1) BLOG("vdoll %d: its copy in player %d's line long enough: it may fire (%llu ms held)", s.k + 1, id + 1, now - b.shownFrom);
        b.shownFrom = 0;
        return false;
    }
    if (!b.shownFrom) {
        b.shownFrom = now;
        if (gDebug) BLOG("vdoll %d: its shot held: its copy %s player %d's line (its copy %.2f m from it; %s%s)", s.k + 1, b.shownSince >= 0 ? "only just in" : "not in", id + 1,
                         Dist2D(gSeen[s.slot].pos, s.pos) / 1000.f, b.low ? "crouched" : "standing", b.lean > 0 ? ", leaning right" : b.lean < 0 ? ", leaning left" : "");
    }
    if (b.shownFrom == 1 || now - b.shownFrom >= (unsigned long long)kShownMaxMs) {   // (a second of it: it fires; 1: said for this time)
        if (b.shownFrom != 1) { ++b.shownLate; b.shownFrom = 1; }
        return false;
    }
    ++b.shownHolds;
    return true;
}
// (v150-host beta 21, cover) a fight's first shot waits for a human reflex in real time from its sight (the host's frames are
// longer when it is slowed: its level's reaction stays in frames, this floor in ms)
bool ReflexHold(Body& b) {
    if (gReflexMs <= 0 || b.fightShots > 0) return false;
    if (RealMs() - b.sightMs >= (unsigned long long)gReflexMs) return false;
    ++b.reflexHolds;
    return true;
}
bool FireHeavy(Body& b, const Self& s, const Weapon& w);   // (v150-host beta 22, heavy: below, with the grenades)
// (v150-host beta 24, cqcfight) a bullet along d from o meets a teammate's body (HitPart's pieces, his stance) before until: it stops there.
// Beta 23's bullet went through him to the enemy (only the 0.7 m rule kept its aim off him). It never wounds him: a virtual doll sends a
// teammate no record (whether a real client's bullet would hurt him with friendly fire on is not measured: not done)
bool AllyStops(const Self& s, const float* o, const float* d, float until) {
    for (int id = 0; id < 16; ++id) {
        if (!Mate(s, id)) continue;
        float t;
        if (HitPart(o, d, gSeen[id], &t) != P_NONE && t < until) return true;
    }
    return false;
}
// (v150-host beta 24, review) a third person bullet leaves its muzzle (MGO1_VDOLL_MUZZLE): a teammate's body on its muzzle's line to
// the point `to` (AllyStops' pieces). AllyInLine and AllyStops test the eye's line, 0.37 m higher where they start: a teammate
// crouched 1.5 m before it, his head under the eye's line and on the gun's, let 7b015f6's doll as a player fire through him (every
// one of its bullets: tests/vdoll_cqcfight_test.cpp crouch, 33 cases on the 12 disc maps), where beta 23's 0.7 m rule held it
bool MuzzleMate(const Body& b, const Self& s, float yaw, const float* to) {
    float m[3], col[3];
    MuzzleAt(b, s, yaw, m, col);
    const float len = Dist3(m, to);
    if (len < 1.f) return false;
    const float d[3] = {(to[0] - m[0]) / len, (to[1] - m[1]) / len, (to[2] - m[2]) / len};
    return AllyStops(s, m, d, len);
}
// its view helps: a teammate's body on its muzzle's line to `to` and none on its eye's (no lean: the view's own eye); where both
// meet him the view gains nothing and its hold and guard go on as before (AllyInLine, Engage)
bool ViewOverMate(const Body& b, const Self& s, float yaw, const float* to) {
    if (!MuzzleMate(b, s, yaw, to)) return false;
    float e[3];
    EyePose(s, b.low, 0, yaw, e);
    const float len = Dist3(e, to);
    if (len < 1.f) return false;
    const float d[3] = {(to[0] - e[0]) / len, (to[1] - e[1]) / len, (to[2] - e[2]) / len};
    return !AllyStops(s, e, d, len);
}
// ---- (v150-host beta 25, reactfix) the reaction a shooter's game tells the victim of its hit ({09 shooter, kind, angle}), by the
// game's own rule (vdoll_react.h, docs/VIRTUAL_DOLL.md 58): picked on its copy of him from his state, his yaw and the yaw he turns to
// (here the host's copy, Host::see; a virtual doll's own body when the copy's are not known) and the room over him (0x407CB0: no face
// of the map within the sphere of his +0x10C at 1200 mm over his hips). b16 picked the flame's 1 and 2 by turns at a quarter turn from
// the way to the doll, sent none for the M37, a flinch for a punch, and 3 to 5 for a blast or a roll on a player on the ground
// whatever the room. MGO1_VDOLL_REACT_KIND=0: b16's ----
bool gReactKind = true;
const float kRoomUp = 1200.f;                      // (0x407CB0: the sphere's middle over his hips, 0x5589DC)
float gRoomR = 400.f;                              // (its radius: his +0x10C, 400 on a real client's copy (rf_m2, probe 54); MGO1_VDOLL_TEST_ROOM_R)
// room over a player whose hips are at p: no face of the map within the sphere (its middle and 14 ways from it: the 6 axes, the 8
// corners' diagonals)
bool RoomOver(const float* p) {
    if (!gMap || !gMap->nav) return true;
    const float c[3] = {p[0], p[1] + kRoomUp, p[2]};
    static const float kW[14][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0.577f, 0.577f, 0.577f}, {-0.577f, 0.577f, 0.577f},
                                     {0.577f, -0.577f, 0.577f}, {-0.577f, -0.577f, 0.577f}, {0.577f, 0.577f, -0.577f}, {-0.577f, 0.577f, -0.577f}, {0.577f, -0.577f, -0.577f},
                                     {-0.577f, -0.577f, -0.577f}};
    for (const auto& w : kW) {
        const float e[3] = {c[0] + w[0] * gRoomR, c[1] + w[1] * gRoomR, c[2] + w[2] * gRoomR};
        float t = 1.f, n[3];
        ++gWorkPass.sight;
        if (gMap->nav->FirstHit(c, e, &t, n) && t < 1.f) return false;
    }
    return true;
}
vreact::Victim ReactVictim(int id) {
    vreact::Victim v;
    const PlayerNow& p = gSeen[id];
    v.state = p.state;
#ifdef VDOLL_PLAYERNOW_YAW
    v.yaw = p.yaw; v.yawTo = p.yawTo;
#endif
    if (v.yaw < 0)                                 // (a virtual doll's own body: its facing)
        for (const Body& o : gBodies) if (o.life >= 0 && o.slot == id) { v.yaw = v.yawTo = Angle16(o.yaw); break; }
    v.room = vreact::Ground(v.state) ? RoomOver(p.pos) : true;
    return v;
}
void ReactLog(const Self& s, int id, const char* what, const vreact::Out& o, const vreact::Victim& v) {
    if (!gDebug) return;
    static const char* kHow[10] = {"none", "a flinch from his front", "a flinch from behind", "a flinch lying", "a flinch down after a roll", "a flinch down after a blast",
                                   "a fall from his front", "a fall from behind", "thrown", "a fall"};
    BLOG("vdoll %d: %s on player %d: the reaction %d (%s), its angle %04x (his yaw %04x, turning to %04x, state %x%s)", s.k + 1, what, id + 1, o.kind, kHow[o.kind % 10], o.angle,
         v.yaw & 0xFFFF, v.yawTo & 0xFFFF, v.state, vreact::Ground(v.state) ? (v.room ? ", room over him" : ", no room over him") : "");
}
void Shoot(Body& b, const Self& s, const Weapon& w) {
    if (w.heavy) { b.heavyHeld = !FireHeavy(b, s, w); return; }   // (v150-host beta 22, heavy: the rocket, the flame)
    const Level L = RowOf(b, true);
    const float e = L.err * kPi / 180.f * (w.code == 0x0F || w.code == 0x10 ? 0.6f : 1.f);   // (the SVD's scope; beta 18: the MOSIN NAGANT's)
    const float dy = b.burstErr[0] + Gauss() * e * 0.5f, dp = b.burstErr[1] + Gauss() * e * 0.5f;
    float eye[3], d[3];
    EyeNow(b, s, eye);
    Dir(b.yaw + dy, b.pitch + dp, d);
    ++b.shots; ++b.fightShots; --b.mag[b.held];
    if (b.held == b.special && b.specialRounds > 0) --b.specialRounds;   // (v150-host beta 22, pickup: the rounds it took with it)
    b.shotSince = true; b.shotAt = gFrame;
    if (ScopeOn(b)) ++b.scopeShots;
    b.dev[0] = (int16_t)lrintf(dy / kTurn * 65536.f); b.dev[1] = (int16_t)lrintf(dp / kTurn * 65536.f);
    PlayerNow t = gSeen[b.target];
    Now(b.target, t.pos);
    // its bullet (the M37: its pellets, about 2 degrees apart): the first part each one meets, before the map's walls
    int part = P_NONE, hits = 0;
    float at = 0.f, hd[3] = {0, 0, 0};
    for (int i = 0; i < std::max(1, w.pellets); ++i) {
        float pd[3];
        if (w.pellets) Dir(b.yaw + dy + Gauss() * 0.035f, b.pitch + dp + Gauss() * 0.035f, pd); else memcpy(pd, d, sizeof pd);
        float tt;
        const int p = HitPart(eye, pd, t, &tt);
        if (p == P_NONE || tt > (w.pellets ? w.far : kRange)) continue;
        if (gCqcFight && AllyStops(s, eye, pd, tt)) { ++gCqcFightCount[s.k & 15].stops; continue; }   // ((v150-host beta 24, cqcfight) a teammate's body first)
        const float end[3] = {eye[0] + pd[0] * tt, eye[1] + pd[1] * tt, eye[2] + pd[2] * tt};
        float wall = 1.f, n[3];
        ++gWorkPass.sight;
        if (gMap->nav->FirstHit(eye, end, &wall, n) && wall < 0.999f) continue;
        if (ThingStops(eye, end, Power(w))) continue;        // (beta 21, geom: a thing of the host's game that stops this bullet)
        // (v150-host beta 24, walls3) the third person's bullet leaves its muzzle: the cover before its gun meets it
        if (MuzzleOn(b) && !MuzzleClear(b, s, b.yaw + dy, end, Power(w))) { ++b.muzzleStops; ++gMuzzleStops; continue; }
        // ((v150-host beta 24, review) and a teammate's body on that line stops it, as on the eye's: AllyStops above)
        if (gCqcFight && MuzzleOn(b) && MuzzleMate(b, s, b.yaw + dy, end)) { ++gCqcFightCount[s.k & 15].stops; continue; }
        if (!hits++) { part = p; at = tt; memcpy(hd, pd, sizeof hd); }
    }
    if (!hits) return;
    if (!CoverHit(b, s, eye, t, part, !w.dart)) return;   // (v150-host beta 21, cover: out of its line where he really is)
    ++gStanceHits[StanceOf(t)];
    if (gDebug) {                                      // (beta 21, geom: where its bullet ends against his stance)
        static const char* kStance[3] = {"standing", "crouched", "lying"};
        BLOG("vdoll %d: its bullet ends %.0f mm over player %d's hips (he is %s, state %x)", s.k + 1, eye[1] + hd[1] * at - t.pos[1], b.target + 1,
             kStance[StanceOf(t)], t.state);
    }
    if (gPlayerCtl) PlayerPart(b, s, &part);           // (beta 18, as a player: the third person's bullets on the body)
    // (v150-host beta 18) a dart: the stun of the game's table (the head x4, x2 on Snake; a leg half), 30000 at most (the
    // stamina's maximum, 0x437F20), no life; its effect record as a playable doll's Mk22 dart (14 kind 03 00, a push of
    // about 1000 along it: tsne_b17_d2)
    if (w.dart) {
        const int v = b.target;
        const int stun = std::min(30000, part == P_HEAD ? w.body * (t.cls == 8 ? 2 : 4) : part == P_LEG ? w.body / 2 : w.body);
        const float end[3] = {eye[0] + hd[0] * at, eye[1] + hd[1] * at, eye[2] + hd[2] * at};
        std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, (uint8_t)w.fx, (uint8_t)(w.fx >> 8), (uint8_t)part};
        Put16(fx, (uint16_t)Tenth(end[0])); Put16(fx, (uint16_t)Tenth(end[1])); Put16(fx, (uint16_t)Tenth(end[2]));
        for (int k = 0; k < 3; ++k) Put16(fx, (uint16_t)(int16_t)lrintf(hd[k] * 1000.f));
        SendHit(b, s, v, 2, fx);
        SendHit(b, s, v, 0, {0x04, (uint8_t)s.slot, 0x00, 0x00, (uint8_t)stun, (uint8_t)(stun >> 8), (uint8_t)w.code, 0x00, (uint8_t)(part == P_HEAD ? 1 : 0)});
        ++b.hits; ++b.fightHits; ++b.darts;
        b.hitAt = gFrame;
        gDartAt[v] = gFrame; gDartBy[v] = s.k + 1; gDartKind[v] = w.code;
        const bool out = StaminaHit(v, stun);
        BLOG("vdoll %d: darts player %d at %.1f m (%s, %s, stun %d%s)", s.k + 1, v + 1, at / 1000.f, w.name, part == P_HEAD ? "the head" : part == P_LEG ? "a leg" : "the body",
             stun, out ? ": his stamina out, asleep" : "");
        return;
    }
    // the game's damage: the table's, the head x4 and a limb half for a readied shot (it sends 5), the M37 25 a pellet;
    // (v150-host beta 18) the head x2 on Snake (0x437F20: x4 unless the victim's class is 8)
    const int dmg = Harmless(b.target) ? 0 : w.pellets ? w.body * hits : part == P_HEAD ? w.body * (t.cls == 8 ? 2 : 4) : part == P_LEG ? w.body / 2 : w.body;   // (MGO1_VDOLL_TEST_HARMLESS: 0)
    const bool head = !w.pellets && part == P_HEAD;
    const float end[3] = {eye[0] + hd[0] * at, eye[1] + hd[1] * at, eye[2] + hd[2] * at};
    const float push = std::min(2666.f, 400.f + at * 0.24f);
    // ((v150-host beta 25, reactfix) the M37's effect as a player's game tells it: 0x0208 when 3 or 4 pellets reached him (a power of 750 and
    // more), the rifles' 0x010A with 1 or 2 (rf_m2: a real client's M37, 13 hits); b16: 0x0208 whatever the pellets)
    const uint16_t fxKind = gReactKind && w.code == 0x0E && w.pellets && hits < 3 ? (uint16_t)0x010A : w.fx;
    std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, (uint8_t)fxKind, (uint8_t)(fxKind >> 8), (uint8_t)part};
    Put16(fx, (uint16_t)Tenth(end[0])); Put16(fx, (uint16_t)Tenth(end[1])); Put16(fx, (uint16_t)Tenth(end[2]));
    for (int k = 0; k < 3; ++k) Put16(fx, (uint16_t)(int16_t)lrintf(hd[k] * push));
    SendHit(b, s, b.target, 2, fx);
    std::vector<uint8_t> hit = {0x04, (uint8_t)s.slot, (uint8_t)dmg, (uint8_t)(dmg >> 8), 0x00, 0x00, (uint8_t)w.code, 0x00, (uint8_t)(head ? 1 : 0)};
    SendHit(b, s, b.target, 0, hit);
    // ((v150-host beta 25, reactfix) the M37's reaction as a shooter's game picks it (0x438B6C): its power 250 a pellet that reached him
    // (the table's 100 at 1000, 25 a pellet), its vector the pellets' way; b16 sent none)
    if (gReactKind && w.code == 0x0E && w.pellets) {
        const vreact::Victim rv = ReactVictim(b.target);
        const vreact::Out ro = vreact::M37(rv, 250 * hits, hd[0], hd[2]);
        if (ro.kind) SendHit(b, s, b.target, 1, {0x09, (uint8_t)s.slot, (uint8_t)ro.kind, (uint8_t)ro.angle, (uint8_t)(ro.angle >> 8)});
        char what[48]; snprintf(what, sizeof what, "its M37 (%d pellets)", hits);
        ReactLog(s, b.target, what, ro, rv);
    }
    if (b.held == b.special) { b.specialHitAt = gFrame; b.specialHitW = b.special; }   // (v150-host beta 22, pickup)
    if (CoverLater(b, eye, t, part, at, w.name)) return;   // (v150-host beta 21, cover: counted when its records go, if it still holds)
    ++b.hits; ++b.fightHits;
    b.hitAt = gFrame;
    BLOG("vdoll %d: hits player %d at %.1f m (%s)", s.k + 1, b.target + 1, at / 1000.f, w.name);
}
// a weapon to its hands: its draw (08 kind 1 in its next record, then aimed after the draw's frames)
void Draw(Body& b, int w) {
    if (b.held == w) return;
    b.held = w; b.drawn = true; b.switchUntil = gFrame + 18;
    b.raiseAt = b.scopeAt = -1;                       // (beta 19: a scoped rifle taken again is raised again)
}
// its magazine spent: the M1911A1 when the enemy is near and it has rounds (quicker than a reload), else the reload
// (08 kind 8 then 6 for the weapon's reload time; on its way meanwhile, the gun down)
void Spent(Body& b, float dist) {
    if (b.held == b.heavyW && b.heavyLeft <= 0) { Draw(b, b.primary); return; }   // (v150-host beta 22, heavy: its last rocket, its fuel out)
    if (b.held == b.heavyW) { b.reloadAt = gFrame; b.reloadUntil = gFrame + kWeapons[b.held].reload; b.burstLeft = 0; b.burstEnd = gFrame; return; }   // (the RPG-7's tube reloaded in its hands: no swap to the handgun)
    if (b.held != b.secondary && b.mag[b.secondary] > 0 && dist < 15000.f && (Allowed() & 2)) { Draw(b, b.secondary); return; }   // (beta 18: its handgun, the Mk22 for a sneaker or Snake)
    b.reloadAt = gFrame; b.reloadUntil = gFrame + kWeapons[b.held].reload;
    b.burstLeft = 0; b.burstEnd = gFrame;
}
// ---- (beta 15) a frag grenade (the full dolls' measures, doll_brain.cpp: its flight from the first person view, its
// speed by the view's pitch, its bounces and roll, off 108 frames after it appears); it wounds 150 x (0.5 + 0.5
// (3000 - d) / 3000) within 3 m with nothing between, as the game's table says ----
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
// (v150-host beta 24, walls3) things: the game's things met too (MGO1_VDOLL_BLAST_COVER, at the release only: a crate, a trunk, a
// vehicle the game made while the round runs, which beta 23's grenades flew through; their face's way is not known: a bounce
// back along the throw, or off the top when it falls on one)
bool SimulateGrenade(const float* eye, float yaw, float pitchDeg, float* end, bool things = false) {
    const doll::NavMesh& nav = *gMap->nav;
    float h, vy;
    ++gWorkPass.flights;
    ThrowSpeed(pitchDeg, &h, &vy);
    const float fx = std::sin(yaw), fz = std::cos(yaw);
    const float sy = std::sin(yaw - 8.f * kPi / 180.f), sz = std::cos(yaw - 8.f * kPi / 180.f);
    float p[3] = {eye[0] + sy * 605.f, eye[1] - 287.f + 4.2f * pitchDeg, eye[2] + sz * 605.f};
    if (!nav.Clear(eye, p)) return false;
    float v[3] = {fx * h * 2.f, vy * 2.f, fz * h * 2.f};              // a tick: 2 frames
    bool rolling = false;
    for (int k = 0; k < 54; ++k) {
        if (!rolling && k >= 6) { v[0] *= 0.98f; v[2] *= 0.98f; v[1] = v[1] > 0.f ? v[1] * 0.983f - 24.f : v[1] - 24.f; }
        if (rolling) {
            const float sp = std::sqrt(v[0] * v[0] + v[2] * v[2]);
            const float f = sp > 12.2f ? (sp - 12.2f) / sp : 0.f;
            v[0] *= f; v[2] *= f;
            if (sp <= 12.2f) break;
        }
        const float q[3] = {p[0] + v[0], p[1] + v[1], p[2] + v[2]};
        float t, nn[3];
        if (nav.FirstHit(p, q, &t, nn)) {
            for (int i = 0; i < 3; ++i) p[i] += (q[i] - p[i]) * t + nn[i] * 20.f;
            const float vn = v[0] * nn[0] + v[1] * nn[1] + v[2] * nn[2];
            for (int i = 0; i < 3; ++i) { const float n_ = nn[i] * vn; v[i] = (v[i] - n_) * 0.26f - n_ * 0.21f; }
            if (nn[1] > 0.7f && std::fabs(v[1]) < 50.f) { rolling = true; v[1] = 0.f; }
        } else if (things && ThingStops(p, q, kStopsAll) && gThingAt >= 0.f) {
            const float len = Dist3(p, q), hv = std::hypot(v[0], v[2]);
            float nn2[3] = {0.f, 1.f, 0.f};                   // (fallen on its top, or against its side: back along the throw)
            if (!(v[1] < 0.f && -v[1] > hv) && hv > 1.f) { nn2[0] = -v[0] / hv; nn2[1] = 0.f; nn2[2] = -v[2] / hv; }
            const float k = len > 1.f ? std::max(0.f, gThingAt - 30.f) / len : 0.f;
            for (int i = 0; i < 3; ++i) p[i] += (q[i] - p[i]) * k + nn2[i] * 20.f;
            const float vn = v[0] * nn2[0] + v[1] * nn2[1] + v[2] * nn2[2];
            for (int i = 0; i < 3; ++i) { const float n_ = nn2[i] * vn; v[i] = (v[i] - n_) * 0.26f - n_ * 0.21f; }
            if (nn2[1] > 0.7f && std::fabs(v[1]) < 50.f) { rolling = true; v[1] = 0.f; }
        } else memcpy(p, q, sizeof p);
        if (rolling) {
            const float up[3] = {p[0], p[1] + 150.f, p[2]};
            const float g = nav.GroundBelow(up, 500.f);
            if (g < -1e29f) rolling = false; else p[1] = g + 60.f;
        }
    }
    memcpy(end, p, sizeof p);
    return true;
}
// a throw that goes off within 2.5 m of the point: the pitch for its distance and 2 more, 3 directions (9 flights)
bool SolveThrow(const float* eye, const float* target, float* yaw, float* pitch) {
    const float d = Dist2D(eye, target);
    static const float kFar[][2] = {{-30.f, 4800.f}, {-20.f, 7100.f}, {-10.f, 13000.f}, {0.f, 20000.f}, {12.f, 23000.f}};
    float p0 = 12.f;
    for (int i = 0; i + 1 < 5; ++i)
        if (d <= kFar[i + 1][1]) { const float t = (d - kFar[i][1]) / (kFar[i + 1][1] - kFar[i][1]); p0 = kFar[i][0] + (kFar[i + 1][0] - kFar[i][0]) * std::max(0.f, t); break; }
    const float direct = std::atan2(target[0] - eye[0], target[2] - eye[2]);
    float best = 2500.f;
    bool ok = false;
    for (float dp : {0.f, -4.f, 4.f}) for (float dy : {0.f, 0.05f, -0.05f}) {
        float end[3];
        if (!SimulateGrenade(eye, direct + dy, p0 + dp, end)) continue;
        const float e = Dist3(end, target);
        if (e < best) { best = e; *yaw = direct + dy; *pitch = p0 + dp; ok = true; }
    }
    return ok;
}
// from level 5, every 15 s at most: at the target it lost these 0.5 to 5 s behind cover 6 to 22 m away (where it saw him
// last), or at two enemies within 3 m of each other it sees 8 to 22 m away
bool StartThrow(Body& b, const Self& s) {
    const bool nadeTest = gNadeTest && b.nade == kStunNade;
    if (b.grenades <= 0 || (b.level < 5 && !b.unarmed && !nadeTest) || gFrame < b.nextThrowAt || b.throwAt >= 0 || b.carrying || gFrame < b.reloadUntil) return false;
    float eye[3], at[3];
    Eye(s, eye);
    bool want = false;
    // (beta 16, the weapons setting) its grenades its only weapon (support only): at the target it sees 5 to 22 m away too
    if (b.unarmed && b.target >= 0 && gFrame - b.seenAt <= 20) {
        const float d = Dist2D(s.pos, b.tpos);
        if (d > 5000.f && d < 22000.f) { Feet(b.tpos, at); want = true; }
    }
    if (nadeTest && b.target >= 0 && gFrame - b.seenAt <= 20) {   // (the test: at him as soon as it sees him within reach)
        const float d = Dist2D(s.pos, b.tpos);
        if (d > 4000.f && d < 22000.f) { Feet(b.tpos, at); want = true; }
    }
    if (b.target >= 0 && gFrame - b.seenAt > 30 && gFrame - b.seenAt < 300) {
        const float d = Dist2D(s.pos, b.tpos);
        if (d > 6000.f && d < 22000.f) { Feet(b.tpos, at); want = true; }
    }
    for (int i = 0; i < 16 && !want; ++i) {
        if (i == s.slot || !gHave[i] || !gSeen[i].object || !gSeen[i].alive || !Enemy(gSeen[i], s.team) || Camouflaged(b, s, i)) continue;
        const float d = Dist2D(s.pos, gSeen[i].pos);
        if (d < 8000.f || d > 22000.f) continue;
        for (int j = i + 1; j < 16; ++j)
            if (j != s.slot && gHave[j] && gSeen[j].object && gSeen[j].alive && Enemy(gSeen[j], s.team) && Dist2D(gSeen[i].pos, gSeen[j].pos) < 3000.f &&
                Visible(eye, gSeen[i])) { Feet(gSeen[i].pos, at); want = true; break; }
    }
    b.nextThrowAt = gFrame + 120;                     // (looked again in 2 s at the soonest)
    if (!want || !SolveThrow(eye, at, &b.throwYaw, &b.throwPitch)) return false;
    b.throwAt = gFrame; b.nextThrowAt = gFrame + (b.unarmed || nadeTest ? 300 : 900); if (!nadeTest) --b.grenades; ++b.throws;
    b.target = -1; b.aiming = true; b.firstAim = true; StandUp(b); b.lean = 0; b.path.clear();
    if (gDebug) BLOG("vdoll %d: throws a grenade toward %.0f %.0f %.0f (%.1f m)", s.k + 1, at[0], at[1], at[2], Dist2D(s.pos, at) / 1000.f);
    return true;
}
// ((v150-host beta 24, cqcfight2) a grenade at a target's feet as StartThrow throws one (the guard's KeepOn): its own rules (level 5,
// a grenade left, its pace, 6 to 22 m, a throw it can solve on the map)
bool ThrowAtFeet(Body& b, const Self& s, const float* hips) {
    if (b.grenades <= 0 || b.level < 5 || b.unarmed || gFrame < b.nextThrowAt || b.throwAt >= 0 || b.carrying || gFrame < b.reloadUntil) return false;
    const float d = Dist2D(s.pos, hips);
    if (d < 6000.f || d > 22000.f) return false;
    float eye[3], at[3];
    Eye(s, eye);
    Feet(hips, at);
    b.nextThrowAt = gFrame + 120;
    if (!SolveThrow(eye, at, &b.throwYaw, &b.throwPitch)) return false;
    b.throwAt = gFrame; b.nextThrowAt = gFrame + 900; --b.grenades; ++b.throws;
    b.target = -1; b.aiming = true; b.firstAim = true; StandUp(b); b.lean = 0; b.path.clear();
    return true;
}
// the throw (the game's own timing, measured: Square held 30 frames, the throw 60 with the grenade let go 34 frames in, 18
// after it): its view on the throw's line; at the release its own flight, the blast 108 frames later
const int kReady = 30, kThrowFor = 60, kRelease = 34, kAfter = 18;
const int kBlastAfter = 108;                     // (the blast after the release: a frag's or a stun grenade's copy on a victim's game)
uint8_t ThrowKind(const Body& b) {
    const long long t = gFrame - b.throwAt;
    return t < 6 ? 0 : t < 6 + kReady ? 0x0D : t < 6 + kReady + kThrowFor ? 0x11 : t < 6 + kReady + kThrowFor + kAfter ? 0x0F : 0;
}
void Throw(Body& b, const Self& s, int dt) {
    const long long t = gFrame - b.throwAt;
    b.speed = 0;
    const float step = (gTurn ? kBodyTurn : 20.f) * kPi / 180.f * dt, want = -b.throwPitch * kPi / 180.f;   // (the view's pitch: over 0 down; beta 21: 16 a frame)
    if (gTurn) TurnBody(b, b.throwYaw, dt, kBodyTurn, true);   // (beta 21: its turn at 16 a frame, as Rush, Grab and Melee: the review, its aim not held back by the way's spin rule)
    else b.yaw = Wrap(b.yaw + std::max(-step, std::min(step, Wrap(b.throwYaw - b.yaw))));
    b.pitch += std::max(-step, std::min(step, want - b.pitch));
    if (t == 6 + kReady + kRelease || (t > 6 + kReady + kRelease && b.blastAt < 0 && b.blastSlot < 0)) {
        float eye[3];
        Eye(s, eye);
        if (SimulateGrenade(eye, b.throwYaw, b.throwPitch, b.blast, gBlastCover)) {   // (beta 24, walls3: the things met too)
            // (beta 18) a stun grenade goes off 108 frames after its release, as the frag: its copy on the victim's game
            // (tsneB_nade4: NewBulletSturnGrenadeWithBuller at the release, NewStunBlast 109 and 110 frames later); the
            // 175 frames of docs/PLAYABLE_DOLL.md count from the grenade made in the thrower's hand (par_b5: 169)
            b.blastStun = b.nade == kStunNade;
            b.blastAt = gFrame + kBlastAfter; b.blastSlot = s.slot; b.blastTeam = s.team; b.blastSend = s.send; b.blastCtx = s.ctx;
            b.blastGen = gHost.gen ? gHost.gen() : -1;
        } else b.blastSlot = -2;
    }
    if (t >= 6 + kReady + kThrowFor + kAfter + 6) {
        b.throwAt = -1; b.aiming = false; b.pitch = 0; b.drawn = !b.unarmed; b.blastSlot = b.blastAt >= 0 ? b.blastSlot : -1;
        // (v150-host beta 22, blast; review22a B2) its own stun grenade still in the air (58 frames left): turned away from it
        // until it goes off and the host's copy with it (NewStunBlast 109 and 110 frames after the release: tsneB_nade4)
        if (gAvert && b.blastStun && b.blastAt > gFrame && b.blastSlot == s.slot) {
            b.avertUntil = b.blastAt + 12;
            if (gDebug) BLOG("vdoll %d: turns away from its stun grenade (%.1f m from it, %lld frames before it goes off)", s.k + 1, Dist2D(s.pos, b.blast) / 1000.f, b.blastAt - gFrame);
        }
    }
}
// (v150-host beta 22, blast; review22a B2) its own stun grenade about to go off: turned away from its blast point (at the
// body's turn, 16 degrees a frame: 180 degrees in 12 frames), standing, no fight begun; a player does so (probe 31's thrower
// turned away: the short white, 118 frames, blast_b21_d). The game's white is still its rule: its third person camera 3.9 m
// behind it then faces away from the blast (the short white, 2 s at most within 10 m of the camera)
void Avert(Body& b, const Self& s, int dt) {
    b.speed = 0; b.aiming = false;
    const float away = std::atan2(s.pos[0] - b.blast[0], s.pos[2] - b.blast[2]);
    const float step = (gTurn ? kBodyTurn : 20.f) * kPi / 180.f * dt;
    if (gTurn) TurnBody(b, away, dt, kBodyTurn, true);
    else b.yaw = Wrap(b.yaw + std::max(-step, std::min(step, Wrap(away - b.yaw))));
    b.pitch += std::max(-step, std::min(step, -b.pitch));
}
// the blast: each player within 3 m of it with nothing between wounded as the game's table says (the thrower's 14 + 04)
void Blast(Body& b, int k) {
    b.blastAt = -1;
    // (v150-host beta 16) thrown in another round, or the round over: it wounds nobody
    if (!b.blastSend || !Ready() || RoundOver() || (gHost.gen && b.blastGen != gHost.gen())) { b.blastSlot = -1; return; }
    const float up[3] = {b.blast[0], b.blast[1] + 300.f, b.blast[2]};
    if (gDebug) BLOG("vdoll %d: its %s goes off at %.0f %.0f %.0f", k + 1, b.blastStun ? "stun grenade" : "grenade", b.blast[0], b.blast[1], b.blast[2]);   // (beta 22, tests)
    for (int id = 0; id < 16; ++id) {
        if (id == b.blastSlot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], b.blastTeam)) continue;
        float c[3];
        // (v150-host beta 24, walls3) a real player where he last said he is, as the rocket's and the claymore's blasts judge him
        PlayerNow at = gSeen[id];
        if (gBlastCover && gCover != COVER_OFF && Judged(id)) memcpy(at.pos, gRec[id].p, sizeof at.pos);
        Chest(at, c);
        const float d = Dist3(up, c);
        // (v150-host beta 18) a stun grenade's reach 8 m (US n003a: the grenade's blast 0x4F3B40 asks 0x4FFEA0 for a wound of
        // 8000, 0x10A750's sphere; the frag's 3000), the stun 12500 whatever the distance (0x437F20: its kind 0x15, no fall off)
        // (v150-host beta 22, blast) the frag's wound 3500 mm for the thrower's own grenade (0x4F0448: 0xDAC, 3000 only for a
        // copy's picture, bit 31 of its word), its damage 1 - d / 6 m within 3 m and half beyond (0x43912C): 75 from 3 to 3.5 m,
        // where beta 21's dolls wounded nobody (MGO1_VDOLL_BLASTS=0: 3000 as beta 21)
        if (d > (b.blastStun ? 8000.f : gReactOut ? 3500.f : 3000.f) || !(gBlastCover ? BlastClear(up, c) : LineClear(up, c))) continue;   // (beta 24, walls3: the things too)
        // (v150-host beta 18) a stun grenade: 12500 of the stamina (the game's table, docs/PLAYABLE_DOLL.md's measure), no life,
        // no wound's effect (its flash is each player's own game's, from the copy of the grenade)
        if (b.blastStun) {
            b.blastSend(b.blastCtx, id, 0, {0x04, (uint8_t)b.blastSlot, 0x00, 0x00, 0xD4, 0x30, (uint8_t)kStunNade, 0x00, 0x00});
            ++b.blastHits;
            gDartAt[id] = gFrame; gDartBy[id] = k + 1; gDartKind[id] = kStunNade;
            StaminaHit(id, 12500);
            BLOG("vdoll %d: its stun grenade reaches player %d at %.1f m from the blast (stun 12500)", k + 1, id + 1, d / 1000.f);
            continue;
        }
        const int dmg = Harmless(id) ? 0 : (int)(150.f * (0.5f + 0.5f * std::max(0.f, 3000.f - d) / 3000.f));   // (MGO1_VDOLL_TEST_HARMLESS: 0)
        std::vector<uint8_t> fx = {0x14, (uint8_t)b.blastSlot, 0x04, 0x02, 0x00};
        Put16(fx, (uint16_t)Tenth(c[0])); Put16(fx, (uint16_t)Tenth(c[1])); Put16(fx, (uint16_t)Tenth(c[2]));
        for (int i = 0; i < 3; ++i) Put16(fx, (uint16_t)(int16_t)lrintf((c[i] - up[i]) / std::max(1.f, d) * 2000.f));
        b.blastSend(b.blastCtx, id, 2, fx);
        b.blastSend(b.blastCtx, id, 0, {0x04, (uint8_t)b.blastSlot, (uint8_t)dmg, (uint8_t)(dmg >> 8), 0x00, 0x00, 0x13, 0x00, 0x00});
        // (v150-host beta 22, blast) the reaction a thrower's game sends with its grenade's wound (US n003a 0x437F20 at 0x438E14
        // and 0x436480; a frag's wound carries the fall flag 0x200, NewBlastWound 0x356E78): {09 thrower, 8, the angle toward
        // the blast} (a blast's fall: the body thrown away from it), or for a player already on the ground a flinch (lying 3,
        // knocked down awake 5, by a roll 4). Measured: a playable doll's frag at a playable doll's feet, {09 01 08 33c4}, the
        // victim thrown 3.6 m (blast_b21_d). Beta 21's dolls sent none: a player or a doll in their grenade's blast took the
        // damage and stood (MGO1_VDOLL_BLASTS=0 or MGO1_VDOLL_REACT=0: as beta 21)
        if (gReactOut) {
            const unsigned st = gSeen[id].state;
            uint8_t kind = (st & 0x8000000u) ? 5 : (st & 0x10000000u) ? 4 : (st & 0x8u) ? 3 : 8;
            uint16_t a16 = Angle16(std::atan2(up[0] - at.pos[0], up[2] - at.pos[2]));
            if (gReactKind && vreact::Ground(st)) {     // ((v150-host beta 25, reactfix) on the ground: thrown again with room over him (vdoll_react.h Throw))
                const vreact::Victim rv = ReactVictim(id);
                const vreact::Out ro = vreact::Throw(rv, true, at.pos[0] - up[0], at.pos[2] - up[2]);
                kind = (uint8_t)ro.kind; a16 = ro.angle;
            }
            b.blastSend(b.blastCtx, id, 1, {0x09, (uint8_t)b.blastSlot, kind, (uint8_t)a16, (uint8_t)(a16 >> 8)});
        }
        ++b.blastHits;
        BLOG("vdoll %d: its grenade hits player %d at %.1f m from the blast", k + 1, id + 1, d / 1000.f);
    }
    b.blastSlot = -1;
}

// ---- (v150-host beta 22, heavy) the RPG-7 and the flamethrower as a player's game fires them (docs/VIRTUAL_DOLL.md 33).
// Every game shows a player's rocket and flame from his records (the weapon in his hands {13 .. code}, his view {12 ..}, the
// shot {08 05 ..}: its copy fires its own rocket or flame, which wounds nobody: US n003a 0x4017C0, 0x4F7F20, the flags -1 of
// a copy); only the shooter's own game wounds, and tells each victim his damage {14 ..} {04 ..}. A virtual doll has no game:
// it works out its rocket's flight and blast, and its flame's reach, and sends those records itself, as for its grenades ----
bool IsRocket(int w) { return w >= 0 && w < kWeaponCount && kWeapons[w].heavy == 1; }
bool IsFlame(int w) { return w >= 0 && w < kWeaponCount && kWeapons[w].heavy == 2; }
// an ally (not itself) within r of the point p (his chest)
bool AllyNear(const Self& s, const float* p, float r) {
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || Enemy(gSeen[id], s.team)) continue;
        if (gSeen[id].team == 0xFE || gSeen[id].team == 0xFF) continue;
        float c[3];
        Chest(gSeen[id], c);
        if (Dist3(c, p) < r) return true;
    }
    return false;
}
// the enemies within r of the point p (their chests), the target too
int EnemiesNear(const Self& s, const float* p, float r) {
    int n = 0;
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], s.team)) continue;
        float c[3];
        Chest(gSeen[id], c);
        if (Dist3(c, p) < r) ++n;
    }
    return n;
}
// the point its rocket is aimed at: his chest (the rocket goes off on the first thing it meets, his body too: probe 46's shots
// at a chest, 4 of 4 blasts on him)
void RocketAim(const PlayerNow& t, float* at) { Chest(t, at); }
// where a rocket fired from eye along d ends: the first of a face of the map, a thing of the game (every one: a rocket is no
// bullet), a player's body (the host's copies: hit, his id), its flight's end (kRocketRange). Its distance
// (v150-host beta 22, heavy2) where a rocket fired from eye along d is stopped by what does not move: a face of the map or a
// thing of the game (every one: a rocket is no bullet), else its flight's end (kRocketRange). Its distance
float RocketStatic(const float* eye, const float* d) {
    float far[3] = {eye[0] + d[0] * kRocketRange, eye[1] + d[1] * kRocketRange, eye[2] + d[2] * kRocketRange};
    float len = kRocketRange;
    float wall = 1.f, n[3];
    ++gWorkPass.sight;
    if (gMap->nav->FirstHit(eye, far, &wall, n) && wall < 1.f) len = kRocketRange * wall;
    for (int k = 0; k < 3; ++k) far[k] = eye[k] + d[k] * len;
    if (ThingStops(eye, far, gBlastCover ? kStopsAll : 1 << 30) && gThingAt >= 0.f && gThingAt < len) len = gThingAt;   // (beta 24, walls3: every thing stops it; beta 23's 1 << 30 let every thing through)
    return len;
}
float RocketFlight(const Self& s, const float* eye, const float* d, float* end, int* hit) {
    float far[3] = {eye[0] + d[0] * kRocketRange, eye[1] + d[1] * kRocketRange, eye[2] + d[2] * kRocketRange};
    float len = kRocketRange;
    *hit = -1;
    float wall = 1.f, n[3];
    ++gWorkPass.sight;
    if (gMap->nav->FirstHit(eye, far, &wall, n) && wall < 1.f) len = kRocketRange * wall;
    for (int k = 0; k < 3; ++k) far[k] = eye[k] + d[k] * len;
    if (ThingStops(eye, far, gBlastCover ? kStopsAll : 1 << 30) && gThingAt >= 0.f && gThingAt < len) len = gThingAt;   // (beta 24, walls3)
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || gSeen[id].team == 0xFE) continue;
        float tt;
        if (HitPart(eye, d, gSeen[id], &tt) != P_NONE && tt > 0.f && tt < len) { len = tt; *hit = id; }
    }
    for (int k = 0; k < 3; ++k) end[k] = eye[k] + d[k] * std::max(0.f, len - 100.f);
    return len;
}
// the special weapon it holds for this fight, if any: the RPG-7 at a target at range (kRpgFrom) or at a group (two enemies
// within its blast of him, from kRpgGroupFrom), never with its blast at its own feet (kRpgSelf) nor a teammate within its
// blast (and 2 m) of the point or near the line; the flamethrower within its reach. Else its gun (-1)
bool RocketSafe(const Self& s, const float* eye, const float* d);
int HeavyWant(Body& b, const Self& s, const float* tpos) {
    if (!gHeavy || b.heavyW < 0 || b.heavyLeft <= 0 || b.target < 0 || b.role != R_SOLDIER || b.carrying) return -1;
    if (gHost.headshotsOnly && gHost.headshotsOnly()) return -1;    // (a head shots only game: a blast or a flame is no head shot)
    const float d = Dist2D(s.pos, tpos);
    if (IsFlame(b.heavyW)) return d <= (b.held == b.heavyW ? kFlameKeep : kFlameFrom) ? b.heavyW : -1;
    if (!IsRocket(b.heavyW)) return -1;
    const float c[3] = {tpos[0], tpos[1], tpos[2]};
    if (d < kRpgSelf || d > kRocketRange) return -1;
    const bool group = d >= kRpgGroupFrom && EnemiesNear(s, c, kRocketRadius) >= 2;
    const bool range = d >= (b.held == b.heavyW ? kRpgKeep : kRpgFrom);
    if (!group && !range) return -1;
    if (AllyNear(s, c, kRocketRadius + 2000.f)) return -1;
    // (its rocket along the line to his chest ends within kRpgSelf, a wall or a crate in front: its gun, not a stand for ever)
    float eye[3], at[3], dir[3];
    Eye(s, eye);
    PlayerNow t = gSeen[b.target];
    memcpy(t.pos, tpos, sizeof t.pos);
    RocketAim(t, at);
    const float len = Dist3(eye, at);
    if (len < 1.f) return -1;
    for (int k = 0; k < 3; ++k) dir[k] = (at[k] - eye[k]) / len;
    if (gFrame - b.rpgSafeAt >= 6 || gFrame < b.rpgSafeAt) { b.rpgSafe = RocketSafe(s, eye, dir); b.rpgSafeAt = gFrame; }   // (asked every 6 frames: the game's line test)
    if (!b.rpgSafe && gDebug && b.rpgSaid != b.fights) {   // (once a fight: why its gun and not its RPG-7)
        b.rpgSaid = b.fights;
        float end[3];
        int hit;
        const float l = RocketFlight(s, eye, dir, end, &hit);
        BLOG("vdoll %d: its RPG-7 not raised at player %d %.1f m away: its rocket would end %.1f m away (%s)", s.k + 1, b.target + 1, d / 1000.f, l / 1000.f,
             l >= kRpgSelf ? "a teammate by its end" : hit >= 0 ? "on a player" : gThingAt >= 0.f && std::fabs(gThingAt - l) < 1.f ? "a thing of the game" : "a face of the map");
    }
    return b.rpgSafe ? b.heavyW : -1;
}
// the frames from the rocket's start to its blast at len mm: slow its first kRocketStart frames (kRocketSlow mm), then its speed
long long RocketFrames(float len) { return kRocketStart + (long long)lrintf(std::max(0.f, len - kRocketSlow) / kRocketSpeed); }
// its rocket's flight along its aim from eye (where it will end, the player it meets): a player would not fire it into a wall
// or a crate at his own feet (kRpgSelf), nor with a teammate within its blast of where it ends
bool RocketSafe(const Self& s, const float* eye, const float* d) {
    float end[3];
    int hit;
    const float len = RocketFlight(s, eye, d, end, &hit);
    return len >= kRpgSelf && !AllyNear(s, end, kRocketRadius + 1000.f);
}
// the pull's end: the rocket leaves along its aim then (the copies' own rockets leave at the same moment on every machine), its
// flight worked out, its blast scheduled (RocketBlast); its shot shown kRpgShow frames ({08 05})
void LaunchRocket(Body& b, const Self& s) {
    b.rpgPullAt = -1;
    float eye[3], d[3], end[3];
    EyeNow(b, s, eye);
    Dir(b.yaw + b.rpgDev[0], b.pitch + b.rpgDev[1], d);
    int hit;
    const float len = RocketFlight(s, eye, d, end, &hit);   // (the players in its line now: the log only)
    const float wall = RocketStatic(eye, d);
    const int rw = IsRocket(b.held) ? b.held : W_RPG7;   // (the RPG-7 still in its hands: its pull goes to its end)
    ++b.shots; ++b.fightShots; --b.mag[rw]; --b.heavyLeft; ++b.rockets;
    b.shotSince = true; b.shotAt = gFrame; b.rpgShowUntil = gFrame + kRpgShow;
    if (ScopeOn(b)) ++b.scopeShots;
    b.dev[0] = (int16_t)lrintf(b.rpgDev[0] / kTurn * 65536.f); b.dev[1] = (int16_t)lrintf(b.rpgDev[1] / kTurn * 65536.f);
    // (v150-host beta 22, heavy2: review22b H2) as the game's own rocket (US n003a 0x4F88E0, its act each frame: from where it
    // was to where it is now, a line test of the map and the things (0x160950) and its contact of 40 mm swept along that stretch
    // (0x14BFD8, the callback 0x4F7EC0: the blast at the next act) on the players' bodies as they are then): the faces and the
    // things do not move, so their end is found now; the bodies are met in flight, a frame at a time (RocketStep)
    b.rocketAt = gFrame + RocketFrames(wall);
    for (int k = 0; k < 3; ++k) { b.rocketEnd[k] = eye[k] + d[k] * std::max(0.f, wall - 100.f); b.rocketDir[k] = d[k]; }
    memcpy(b.rocketFrom, eye, sizeof b.rocketFrom);
    b.rocketWall = wall; b.rocketDone = 0.f; b.rocketLaunch = gFrame;
    b.rocketHit = -1; b.rocketSlot = s.slot; b.rocketTeam = s.team; b.rocketSend = s.send; b.rocketCtx = s.ctx;
    b.rocketGen = gHost.gen ? gHost.gen() : -1;
    BLOG("vdoll %d: fires its RPG-7%s%s: its flight ends %.1f m away on the map in %lld frames%s (%d rockets left)", s.k + 1, b.target >= 0 ? " at player " : "",
         b.target >= 0 ? std::to_string(b.target + 1).c_str() : "", wall / 1000.f, b.rocketAt - gFrame,
         hit >= 0 ? (" (player " + std::to_string(hit + 1) + " in its line " + std::to_string((int)lrintf(len / 100.f) / 10) + "." + std::to_string((int)lrintf(len / 100.f) % 10) + " m away now)").c_str() : "",
         b.heavyLeft);
}
// the blast: each enemy whose body's point (his hips + 230 mm) is within its reach with a clear line from the blast to it, as
// the shooter's game tells him (no line from the doll is asked: the blast's own). A real player is judged at the place he last
// sent (as the shooter's game tests its copy of him: beta 21's gate, its first part)
void RocketBlast(Body& b, int k) {
    b.rocketAt = -1; b.rocketLaunch = -1;
    if (!b.rocketSend || !Ready() || RoundOver() || (gHost.gen && b.rocketGen != gHost.gen())) { b.rocketSlot = -1; return; }
    const float c0[3] = {b.rocketEnd[0], b.rocketEnd[1], b.rocketEnd[2]};
    int hits = 0;
    for (int id = 0; id < 16; ++id) {
        if (id == b.rocketSlot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], b.rocketTeam)) continue;
        float hp[3] = {gSeen[id].pos[0], gSeen[id].pos[1], gSeen[id].pos[2]};
        if (gCover != COVER_OFF && Judged(id)) memcpy(hp, gRec[id].p, sizeof hp);   // (beta 21's gate: where he last said he is)
        const float pt[3] = {hp[0], hp[1] + kBodyPointUp, hp[2]};
        const float dist = Dist3(c0, pt);
        if (dist > kRocketRadius || !(gBlastCover ? BlastClear(c0, pt) : CoverFree(c0, pt))) continue;   // (beta 24, walls3: its own lines)
        // (the game's 0x436520: each value the float made a 16 bit integer of mm, then tenths; the PS2's conversions cut toward 0)
        auto put = [](std::vector<uint8_t>& o, float v) { const int16_t mm = (int16_t)(int)v; Put16(o, (uint16_t)(int16_t)(int)((float)mm / 10.f)); };
        std::vector<uint8_t> fx = {0x14, (uint8_t)b.rocketSlot, (uint8_t)kBlastFx, (uint8_t)(kBlastFx >> 8), 0x00};
        put(fx, pt[0] + kBlastOffX); put(fx, pt[1]); put(fx, pt[2]);
        for (int i = 0; i < 3; ++i) put(fx, pt[i] - c0[i]);
        b.rocketSend(b.rocketCtx, id, 2, fx);
        b.rocketSend(b.rocketCtx, id, 0, {0x04, (uint8_t)b.rocketSlot, (uint8_t)kRocketDamage, (uint8_t)(kRocketDamage >> 8), 0x00, 0x00, (uint8_t)kBlastKind, 0x00, 0x00});
        const uint16_t to = Angle16(Bearing(pt, c0));
        // (at the merge with blast: the reaction's kind as the shooter's game picks it from his state, the frag's rule, section
        // 31: thrown, 8, standing or crouched; on the ground already a flinch, 5 knocked down awake, 4 rolled over, 3 lying)
        const unsigned st = gSeen[id].state;
        uint8_t react = (st & 0x8000000u) ? 5 : (st & 0x10000000u) ? 4 : (st & 0x8u) ? 3 : (uint8_t)kKnockBlast;
        uint16_t toA = to;
        if (gReactKind && vreact::Ground(st)) {         // ((v150-host beta 25, reactfix) on the ground: thrown again with room over him)
            const vreact::Out ro = vreact::Throw(ReactVictim(id), true, pt[0] - c0[0], pt[2] - c0[2]);
            react = (uint8_t)ro.kind; toA = ro.angle;
        }
        b.rocketSend(b.rocketCtx, id, 1, {0x09, (uint8_t)b.rocketSlot, react, (uint8_t)toA, (uint8_t)(toA >> 8)});
        ++hits; ++b.rocketHits;
        if (id == b.target) { b.hitAt = gFrame; b.specialHitAt = gFrame; b.specialHitW = W_RPG7; }   // (at the merge with pickup: its fight's hit, a kill counted as the special's)
        BLOG("vdoll %d: its rocket's blast hits player %d at %.1f m from it (%d, %s)%s", k + 1, id + 1, dist / 1000.f, kRocketDamage,
             react == kKnockBlast ? "knocked down" : "on the ground: a flinch", id == b.rocketHit ? " (the rocket on him)" : "");
    }
    if (!hits && gDebug) BLOG("vdoll %d: its rocket's blast at %.0f %.0f %.0f hits nobody", k + 1, c0[0], c0[1], c0[2]);
    b.rocketSlot = -1;
}
// (v150-host beta 22, heavy2: review22b H2) its rocket in flight, each frame: the stretch it flew since the last (its model:
// kRocketSlow mm over its first kRocketStart frames, then kRocketSpeed a frame, to the face or thing that stops it), the players'
// bodies on that stretch as they are now (a real player at the place he last sent, beta 21's gate, as the blast judges him):
// the first one met stops it there and then, as the game's contact does; else the blast where its flight ends. A player who
// moved out of its line is passed by (it bursts behind him, as every player's own copy of the rocket does on his screen); one
// who stepped into it stops it. Teammates' bodies stop it too (the blast wounds none of them)
void RocketStep(Body& b, int k, long long frame) {
    if (b.rocketAt < 0) return;
    if (b.rocketLaunch >= 0 && Ready()) {
        const long long t = frame - b.rocketLaunch;
        float cur = t < kRocketStart ? kRocketSlow * (float)t / (float)kRocketStart : kRocketSlow + (float)(t - kRocketStart) * kRocketSpeed;
        cur = std::min(cur, b.rocketWall);
        if (cur > b.rocketDone) {
            float best = cur + 1.f;
            int who = -1;
            for (int id = 0; id < 16; ++id) {
                if (id == b.rocketSlot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || gSeen[id].team == 0xFE) continue;
                PlayerNow p = gSeen[id];
                if (gCover != COVER_OFF && Judged(id)) memcpy(p.pos, gRec[id].p, sizeof p.pos);
                float tt;
                // (its entry into his body on this stretch; 400 mm back for a body it is already in, a player who stepped onto it)
                if (HitPart(b.rocketFrom, b.rocketDir, p, &tt) != P_NONE && tt >= b.rocketDone - 400.f && tt <= cur && tt < best) { best = tt; who = id; }
            }
            b.rocketDone = cur;
            if (who >= 0) {
                for (int i = 0; i < 3; ++i) b.rocketEnd[i] = b.rocketFrom[i] + b.rocketDir[i] * std::max(0.f, best - 100.f);
                b.rocketHit = who;
                if (gDebug) BLOG("vdoll %d: its rocket meets player %d's body %.1f m from its start, %lld frames after the launch", k + 1, who + 1, best / 1000.f, t);
                RocketBlast(b, k);
                return;
            }
        }
    }
    if (frame >= b.rocketAt) RocketBlast(b, k);
}
// a moment of its flame (every w.every frames while it streams): each enemy in its reach, near its line and in a clear line
// from its eye, is in the flame since flameOn[id]; kFlameFirst frames on, then every kFlameEvery while he stays in it, a contact as
// the shooter's game tells it (the measures above), its own game then burning him (a real client's own NewFireForChara; the
// host's game burns a virtual doll's copy)
void FireFlame(Body& b, const Self& s, const float* eye, const float* d) {
    ++b.flames;
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], s.team)) { if (id < 16) b.flameOn[id] = -1; continue; }
        float hp[3] = {gSeen[id].pos[0], gSeen[id].pos[1], gSeen[id].pos[2]};
        if (gCover != COVER_OFF && Judged(id)) memcpy(hp, gRec[id].p, sizeof hp);   // (beta 21's gate: where he last said he is)
        const float pt[3] = {hp[0], hp[1] + kBodyPointUp, hp[2]};
        const float lo[3] = {hp[0], hp[1] - 700.f, hp[2]}, hi[3] = {hp[0], hp[1] + 900.f, hp[2]};
        float tt;
        const float gap = RaySegment(eye, d, lo, hi, &tt);
        if (tt <= 0.f || tt > kFlameReach || gap > kFlameWidth + tt * kFlameSpread || !(gBlastCover ? BlastClear(eye, pt) : CoverFree(eye, pt))) {   // (beta 24, walls3)
            if (b.flameOn[id] >= 0 && gFrame - b.flameSeen[id] > 12) b.flameOn[id] = -1;   // (out of it a moment: the flame's time on him begins again)
            continue;
        }
        if (b.flameOn[id] < 0 || gFrame - b.flameSeen[id] > 12) { b.flameOn[id] = gFrame; b.flameNext[id] = gFrame + kFlameFirst; }   // (its stream stopped a moment, a reading, a swap: its time on him begins again)
        b.flameSeen[id] = gFrame;
        if (gFrame < b.flameNext[id]) continue;
        b.flameNext[id] = gFrame + kFlameEvery;
        float ax[3];                                     // (the flame's line at his distance: the record's second values his point less it)
        for (int k = 0; k < 3; ++k) ax[k] = eye[k] + d[k] * tt;
        // ((v150-host beta 25, reactfix) the flame that touches him as a shooter's game finds it: one of its fires along the stream about him,
        // before him or past him (measured on real clients, heavy_p3_flame and rf_m2: his point less it 0.03 to 0.95 m along the stream either
        // way, 0 to 0.4 m aside), not the stream's nearest point; its reaction's kind and angle follow from it (vdoll_react.h Burn))
        if (gReactKind) {
            const float hl = std::max(1.f, std::hypot(d[0], d[2])), sx = d[2] / hl, sz = -d[0] / hl;
            const float along = (Rnd01() * 2.f - 1.f) * kFlameAlong, aside = (Rnd01() * 2.f - 1.f) * kFlameAside;
            ax[0] += d[0] / hl * along + sx * aside; ax[2] += d[2] / hl * along + sz * aside;
        }
        auto put = [](std::vector<uint8_t>& o, float v) { const int16_t mm = (int16_t)(int)v; Put16(o, (uint16_t)(int16_t)(int)((float)mm / 10.f)); };
        std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, (uint8_t)kFlameFx, (uint8_t)(kFlameFx >> 8), 0x00};
        put(fx, pt[0] + kFlameOffX); put(fx, pt[1]); put(fx, pt[2]);
        for (int i = 0; i < 3; ++i) put(fx, pt[i] - ax[i]);
        SendHit(b, s, id, 2, fx);
        SendHit(b, s, id, 0, {0x04, (uint8_t)s.slot, (uint8_t)kFlameDamage, (uint8_t)(kFlameDamage >> 8), 0x00, 0x00, (uint8_t)kFlameKind, 0x00, 0x00});
        // (its reaction {09 shooter, 1 or 2, an angle}: the game's two flinches, its angle his facing turned a quarter (VIRTUAL1, still:
        // 0x4001 with 2, 0xC001 with 1); his facing is not read here: the way to the doll stands for it)
        int kind = 1 + (b.flameHits & 1);
        uint16_t ang = (uint16_t)(Angle16(Bearing(hp, s.pos)) + (kind == 2 ? 0x4001 : 0xC001));
        // ((v150-host beta 25, reactfix) a shooter's game picks it on its copy of him (vdoll_react.h Burn): 1 when the record's vector runs
        // against his facing (his yaw the angle), else 2 (his yaw + 0x8000: his own game turns him about); on the ground 4, 5 or 3. b16: by
        // turns, a quarter turn from the way to the doll)
        if (gReactKind) {
            const vreact::Victim rv = ReactVictim(id);
            const vreact::Out ro = vreact::Burn(rv, pt[0] - ax[0], pt[2] - ax[2], Angle16(Bearing(hp, s.pos)));
            kind = ro.kind; ang = ro.angle;
            ReactLog(s, id, "its flame", ro, rv);
        }
        SendHit(b, s, id, 1, {0x09, (uint8_t)s.slot, (uint8_t)kind, (uint8_t)ang, (uint8_t)(ang >> 8)});
        ++b.flameHits; ++b.hits; ++b.fightHits;
        b.hitAt = gFrame; b.specialHitAt = gFrame; b.specialHitW = W_FLAME;   // (at the merge with pickup: its kills with a special counted)
        BLOG("vdoll %d: its flame on player %d at %.1f m (%d, burning)", s.k + 1, id + 1, tt / 1000.f, kFlameDamage);
    }
}
// its special weapon's shot (Shoot, when it holds one): the spread of its level; the RPG-7's pull (its rocket leaves kRpgPull
// frames later: LaunchRocket), a moment of flame. False: not fired (its rocket would end at its own feet or by a teammate)
bool FireHeavy(Body& b, const Self& s, const Weapon& w) {
    const Level L = RowOf(b, true);
    const float e = L.err * kPi / 180.f;
    const float dy = b.burstErr[0] + Gauss() * e * 0.5f, dp = b.burstErr[1] + Gauss() * e * 0.5f;
    float eye[3], d[3];
    EyeNow(b, s, eye);
    Dir(b.yaw + dy, b.pitch + dp, d);
    if (w.heavy == 1) {
        if (b.rpgPullAt >= 0 || gFrame < b.rpgShowUntil) return false;
        if (!RocketSafe(s, eye, d)) { ++b.heavyHolds; return false; }
        b.rpgPullAt = gFrame + kRpgPull; b.rpgDev[0] = dy; b.rpgDev[1] = dp;
        return true;
    }
    ++b.shots; ++b.fightShots;                        // (the flame: its 999 rounds not spent by a stream, measured)
    b.shotSince = true; b.shotAt = gFrame;
    b.dev[0] = (int16_t)lrintf(dy / kTurn * 65536.f); b.dev[1] = (int16_t)lrintf(dp / kTurn * 65536.f);
    FireFlame(b, s, eye, d);
    return true;
}

long long gKnockedAt[16] = {-100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000,
                            -100000, -100000, -100000, -100000, -100000, -100000, -100000, -100000};   // (a player its side knocked down last)
// ((v150-host beta 25, rolltime) MGO1_VDOLL_ROLL_TIME (on; 0: beta 25's roll, 36 frames of its style record 01 at 72 mm a frame, then
// free at once): a doll's roll as a player's (docs/VIRTUAL_DOLL.md 48; probe 51 on real clients, benches rolltime_p1 and p2). A player
// presses Cross while he runs and runs on 3 ticks; from the 4th he rolls 20 ticks (his state 0x4240, his record {06 01}) and gets up 18
// ticks (0x4642, {06 00}), the roll's bit 9 all along (the stage's cost: MoveCode 8), each tick moved by the motion's own step (kRollStep:
// 5.1 m on a flat floor, stopped by a wall, the motion going on); no shot, no other move, no new roll meanwhile (US n003a 0x446508 and
// 0x4462C0 read no button but Cross at the roll's end), his gun raised again after (his first round kRollRaise ticks at the soonest
// after the getting up). Every player's game plays that motion on a copy when its {06 01} comes and moves the copy by those steps: a
// doll that told other places (beta 25) was pulled back to them, 3 to 4 m at a time, while it ran or fired. Its ticks are 2 of the
// host's frames at full pace, fewer on a slowed host (Slowed(): a player's ticks in real time, as its run)
bool gRollTime = true;
// (the ticks from the press: the roll's state from the 4th (its first tick moves nothing), the getting up from the 24th, standing at the
// 42nd; probe 51's motion times: 0x16 200 of 1/300 s, 0x17 180; its first round 5 ticks after at the soonest, Square from the roll on)
const float kRollLead = 4.f, kRollTicks = 20.f, kRollUpTicks = 18.f, kRollRaise = 5.f;
const float kRollStep[38] = {0.f, 26.f, 444.f, 444.f, 485.f, 449.f, 401.f, 354.f, 310.f, 260.f, 222.f, 177.f, 133.f, 98.f, 72.f, 58.f, 60.f, 61.f, 56.f, 51.f,   // (the roll: 4.16 m)
                             52.f, 30.f, 82.f, 100.f, 102.f, 98.f, 83.f, 75.f, 67.f, 58.f, 48.f, 41.f, 33.f, 25.f, 25.f, 19.f, 19.f, 14.f};                 // (its getting up: 0.97 m)
float RollAlong(float t) {                             // (mm along the roll after t of its ticks, the press's delay not counted)
    float a = 0.f;
    for (int i = 0; i < 38 && t > 0.f; ++i, t -= 1.f) a += kRollStep[i] * std::min(1.f, t);
    return a;
}
// its roll now: 0 none, 1 the press's delay (it runs on), 2 the roll ({06 01}), 3 its getting up ({06 00}); beta 25's roll all 2
int RollPart(const Body& b) {
    if (gFrame >= b.rollUntil) return 0;
    if (!gRollTime || b.rollT < 0.f) return 2;
    return b.rollT < kRollLead ? 1 : b.rollT < kRollLead + kRollTicks ? 2 : 3;
}
// (mm from the press after t ticks: its run before the roll, 130 mm a tick, the 3 ticks after the press when it was running, the last
// one from standing (a player readied and still: the stick and Cross, he runs a tick and rolls, probe 51's a and A), then the roll's)
float RollDone(float t, float run = 3.f) { return std::max(0.f, std::min(t - (kRollLead - 1.f - run), run)) * kRun * 2.f + RollAlong(t - (kRollLead - 1.f)); }
// the floor it needs ahead for a roll begun now: the press's run and the roll's own way (5.5 m); beta 25's 2.4 m
float RollWay() { return gRollTime ? RollDone(kRollLead + kRollTicks + kRollUpTicks) : 2400.f; }
// (the host's own copy of it plays the roll's motions in the host's ticks, its {06 01} a record late: on a slowed host it ends its
// getting up after the doll, whose ticks are real time, and HeldBack took that for the game's collision holding the copy back and
// set the doll back to it, 2.5 to 2.9 m in one record on every player's screen (bench rolltime_after's first run, 99 times after 146
// rolls; beta 25's roll 131 after 151). A real player's roll is not pulled back by the host's copy: none until the copy's motions can
// have ended, 2 frames a tick of them in the host's own frames and 30 more)
bool RollHoldsCopy(const Body& b) { return gRollTime && b.rollAt >= 0 && gFrame - b.rollAt < (long long)(2.f * (kRollLead + kRollTicks + kRollUpTicks)) + 30; }
void RollBegin(Body& b, const Self& s, float yaw, int into) {
    b.rollYaw = yaw; b.rollInto = into; b.rollAt = gFrame;
    if (gRollTime) {
        b.rollT = 0.f; b.rollUntil = gFrame + 8 + (long long)(2.f * (kRollLead + kRollTicks + kRollUpTicks));   // (RollAsPlayer ends it)
        float feet[3];                                // (its way along the standing floor from here: a wall stops it there, as a player)
        Feet(s.pos, feet);
        memcpy(b.rollFrom, s.pos, sizeof b.rollFrom);
        b.rollRun = b.moveSpeed > kRun * 0.5f ? 3.f : 1.f;   // (running at its press, or still: aiming, a fight)
        b.rollMax = std::max(0.f, std::min(RollWay(), gMap->nav->ClearAlong(feet, std::sin(yaw), std::cos(yaw), RollWay() + 200.f, true) - 50.f));
        if (gDebug) BLOG("vdoll %d: its press for a roll at %.0f %.0f %.0f, its heading %.0f degrees, %s, its way %.1f m%s", s.k + 1, s.pos[0], s.pos[1], s.pos[2], yaw * 57.29578f,
                         b.rollRun > 2.f ? "running" : "still", b.rollMax / 1000.f, into >= 0 ? " (into an enemy)" : "");
    } else { b.rollT = -1.f; b.rollUntil = gFrame + 36; }
    b.aiming = false; b.lean = 0; b.path.clear(); ++b.rolls;
}
// ((v150-host beta 25, main session) the floor a roll will step on, walked as RollAsPlayer steps it (FloorNear from the floor it
// stands on, each step from the last): how far it runs on (mm, up to `upto`). The mesh's ClearAlong can run on over a drop's edge to
// the floor below (the mesh joins them), where RollAsPlayer finds no floor and held the roll where it was: its 7 {06 01} and 6 {06 00}
// sent with no move, so every screen rolled its copy 5.5 m on and pulled it back (tests/vdoll_rolltime_test.cpp n022a: a test roll
// pressed at a drop's edge, 0.00 m; meshmargin's n012a: a roll begun on a stone of a stairs of blocks). A player cannot roll in place:
// no roll is begun where its way's floor does not run on (MGO1_VDOLL_ROLL_TIME=0: beta 25's starts, unchanged)
float RollFloorAlong(const Body& b, const Self& s, float yaw, float upto) {
    float y = b.floorY;
    for (float a = 130.f; a <= upto; a += 130.f) {
        const float f = FloorNear(s.pos[0] + std::sin(yaw) * a, s.pos[2] + std::cos(yaw) * a, y);
        if (f < -1e29f) return a - 130.f;
        y = f;
    }
    return upto;
}
// (the way RollBegin will step: to a wall at the most, as rollMax; a roll into an enemy may meet a wall past him, a player's too)
bool RollFloorOk(const Body& b, const Self& s, float yaw) {
    if (!gRollTime) return true;
    float feet[3];
    Feet(s.pos, feet);
    const float steps = std::max(0.f, std::min(RollWay(), gMap->nav->ClearAlong(feet, std::sin(yaw), std::cos(yaw), RollWay() + 200.f, true) - 50.f));
    return RollFloorAlong(b, s, yaw, steps) >= steps - 130.f;
}
// (beta 15) a roll (from level 7, hurt in a fight at 15 m or less): 36 frames along a side of the line to him where the
// standing floor runs on 2.4 m, the gun down; its style 01 (the game's roll), then the fight again
bool StartRoll(Body& b, const Self& s) {
    if (b.low || b.target < 0 || !gHave[b.target] || Dist2D(s.pos, gSeen[b.target].pos) > 15000.f) return false;
    const float to = Bearing(s.pos, gSeen[b.target].pos);
    float feet[3];
    Feet(s.pos, feet);
    const float first = Rnd01() < 0.5f ? 1.f : -1.f;
    const float way = RollWay();                     // ((v150-host beta 25, rolltime) a player's roll's way)
    for (float side : {first, -first}) for (float a : {90.f, 60.f, 120.f}) {
        const float y = Wrap(to + side * a * kPi / 180.f);
        if (gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), way + 200.f, true) < way || !RollFloorOk(b, s, y)) continue;
        RollBegin(b, s, y, -1); b.nextRollAt = gFrame + 240 + (long long)(Rnd() % 240);
        StandUp(b);
        return true;
    }
    return false;
}
// (beta 15) a roll into an enemy 1.4 to 2.2 m before it (from level 6, a chance): he is knocked down 4.2 s, as by a
// player's roll
bool StartRollInto(Body& b, const Self& s) {
    if (b.low || b.target < 0 || b.level < 6 || !gHave[b.target] || !gSeen[b.target].alive || gFrame - gKnockedAt[b.target] < 600) return false;
    const float d = Dist2D(s.pos, gSeen[b.target].pos);
    if (d < 1400.f || d > 2200.f || std::fabs(gSeen[b.target].pos[1] - s.pos[1]) > 400.f) return false;
    float feet[3];
    Feet(s.pos, feet);
    const float y = Bearing(s.pos, gSeen[b.target].pos);
    if (gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), d, true) < d - 300.f || !RollFloorOk(b, s, y)) return false;
    RollBegin(b, s, y, b.target); b.nextRollAt = gFrame + 300;   // ((v150-host beta 25, rolltime) on through him as a player's roll, to a wall)
    return true;
}
// ((v150-host beta 25, rolltime) MGO1_VDOLL_TEST_ROLL=<seconds> (benches only): a doll running on its way out of a fight rolls on along
// its way once every <seconds> at most, where the standing floor runs on for a roll: a bench sees many rolls (the roll itself is StartRoll's)
int gTestRoll = 0;
void TestRoll(Body& b, const Self& s) {
    if (gTestRoll <= 0 || b.target >= 0 || b.low || b.carrying || gFrame < b.testRollAt || gFrame < b.rollUntil || b.moveSpeed < kRun * 0.8f || b.grabStart >= 0 ||
        b.throwAt >= 0 || gFrame < b.rushUntil || b.fallAt >= 0 || gFrame < b.climbUntil || gFrame < b.landUntil) return;
    float feet[3];
    Feet(s.pos, feet);
    const float y = b.yaw, way = std::max(2600.f, RollWay());
    if (gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), way + 200.f, true) < way || !RollFloorOk(b, s, y)) return;
    RollBegin(b, s, y, -1); StandUp(b);
    b.testRollAt = gFrame + gTestRoll * 60LL;
    BLOG("vdoll %d: a test roll at %.0f %.0f %.0f, its heading %.0f degrees", s.k + 1, s.pos[0], s.pos[1], s.pos[2], y * 57.29578f);
}
// ((v150-host beta 25, rolltime) a player's roll (MGO1_VDOLL_ROLL_TIME): its ticks, its run then its motion's steps along rollYaw from
// where it pressed, as far as the standing floor was clear then (a wall stops the steps, not the motion), its gun raised after; true
// while it lasts
bool RollAsPlayer(Body& b, const Self& s, int dt) {
    if (gFrame == b.rollAt) return true;              // (its press this frame: its ticks from the next one)
    const float was = b.rollT, total = kRollLead + kRollTicks + kRollUpTicks;
    b.rollT = std::min(total, b.rollT + (float)dt * Slowed() * 0.5f);
    const float along = std::min(b.rollMax, RollDone(b.rollT, b.rollRun));
    if (along > std::min(b.rollMax, RollDone(was, b.rollRun)) + 0.5f) {
        const float x = b.rollFrom[0] + std::sin(b.rollYaw) * along, z = b.rollFrom[2] + std::cos(b.rollYaw) * along, f = FloorNear(x, z, b.floorY);
        if (f > -1e29f) { s.pos[0] = x; s.pos[2] = z; b.floorY = f; s.pos[1] = f + kHips; }
        else {                                         // (no floor there: it stays where it is to its roll's end)
            b.rollMax = std::min(b.rollMax, RollDone(was, b.rollRun));
            if (gDebug) BLOG("vdoll %d: its roll held at %.0f %.0f %.0f (tick %.1f, %.0f mm from its press): no floor", s.k + 1, s.pos[0], s.pos[1], s.pos[2], b.rollT, along);
        }
    }
    // (running before the roll: its style 0x40, its move's cost the run's, to the tick the roll begins)
    b.speed = dt > 0 && b.rollT < kRollLead && b.rollT > kRollLead - 1.f - b.rollRun ? kRun : 0.f;
    if (b.rollT >= total) {                           // (up: free, its gun to raise again)
        if (gDebug) BLOG("vdoll %d: up from its roll at %.0f %.0f %.0f, %.2f m from its press (%lld frames)", s.k + 1, s.pos[0], s.pos[1], s.pos[2], std::hypot(s.pos[0] - b.rollFrom[0], s.pos[2] - b.rollFrom[2]) / 1000.f, gFrame - b.rollAt);
        b.rollUntil = gFrame; b.rollT = -1.f;
        b.nextShot = std::max(b.nextShot, gFrame + (long long)std::lrintf(2.f * kRollRaise / Slowed()));
        return false;
    }
    // (a bound only: its end is the frame its ticks are all done, above, whatever the host's pace does meanwhile; 2 frames a tick at
    // the least and a skip of frames past dt's 6)
    b.rollUntil = gFrame + 8 + (long long)std::ceil(2.f * (total - b.rollT));
    return true;
}
void Roll(Body& b, const Self& s, int dt) {
    if (!ShotHold(b)) b.yaw = b.rollYaw;              // (beta 21: its last shot's record first, as it fired)
    if (b.rollInto >= 0 && RollPart(b) == 2 && gHave[b.rollInto] && gSeen[b.rollInto].alive && Dist2D(s.pos, gSeen[b.rollInto].pos) < 900.f) {
        const int v = b.rollInto;
        float c[3];
        Chest(gSeen[v], c);
        const uint16_t f16 = Angle16(Wrap(b.rollYaw + kPi));
        std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, 0x00, 0x02, 0x02};
        Put16(fx, (uint16_t)Tenth(c[0])); Put16(fx, (uint16_t)Tenth(c[1])); Put16(fx, (uint16_t)Tenth(c[2]));
        Put16(fx, (uint16_t)(int16_t)lrintf(std::sin(b.rollYaw) * 96.f)); Put16(fx, (uint16_t)(int16_t)-12);
        Put16(fx, (uint16_t)(int16_t)lrintf(std::cos(b.rollYaw) * 96.f));   // (its push: about 100 along the roll)
        s.send(s.ctx, v, 2, fx);
        s.send(s.ctx, v, 0, {0x04, (uint8_t)s.slot, 0x00, 0x00, 0x10, 0x27, 0x9C, 0x00, 0x00});
        // ((v150-host beta 25, reactfix) a roll's word 0x400: 9 standing; on the ground 8 with room over him, else 4, 5 or 3 (vdoll_react.h
        // Throw); b16: 9 whatever his state)
        vreact::Out ro; ro.kind = 9; ro.angle = f16;
        if (gReactKind) { const vreact::Victim rv = ReactVictim(v); ro = vreact::Throw(rv, false, std::sin(b.rollYaw), std::cos(b.rollYaw)); ReactLog(s, v, "its roll", ro, rv); }
        s.send(s.ctx, v, 1, {0x09, (uint8_t)s.slot, (uint8_t)ro.kind, (uint8_t)ro.angle, (uint8_t)(ro.angle >> 8)});
        ++b.rollHits; gKnockedAt[v] = gFrame;
        b.rollInto = -1; b.rushUntil = -1; b.nextGrabAt = std::max(b.nextGrabAt, gFrame + 300);   // (he is down: no grab on him)
        if (!gRollTime) b.rollUntil = std::min(b.rollUntil, gFrame + 12);   // (it stops on him; (v150-host beta 25, rolltime) a player's rolls on through)
        BLOG("vdoll %d: rolls into player %d: down", s.k + 1, v + 1);
    }
    if (gRollTime && b.rollT >= 0.f) { RollAsPlayer(b, s, dt); return; }   // ((v150-host beta 25, rolltime) MGO1_VDOLL_ROLL_TIME)
    const float x = s.pos[0] + std::sin(b.rollYaw) * 72.f * dt, z = s.pos[2] + std::cos(b.rollYaw) * 72.f * dt;
    const float f = FloorNear(x, z, b.floorY);
    if (f < -1e29f) { b.rollUntil = gFrame; return; }
    s.pos[0] = x; s.pos[2] = z; b.floorY = f; s.pos[1] = f + kHips;
    b.speed = 0;
}
// (beta 15) CQC as a player's: within 1.1 m of an enemy at its height (from level 6), its M1911A1 drawn, Circle 3 records,
// the grab, the hold 32 frames, the throw (he lies knocked out about 20 s); its own pose free 84 frames after the
// grab. A rush at him first when
// he is within 4 m and it reloads (or a chance at the top levels): straight at him on the floor, 1.5 s at most
const float kGrabReach = 1100.f;
bool CanGrab(const Body& b, const Self& s, float reach) {
    if (b.target < 0 || b.level < 6 || b.carrying || gFrame < b.nextGrabAt || b.grabStart >= 0 || b.throwAt >= 0 || !gHave[b.target]) return false;
    // ((v150-host beta 24, cqcfight) a crouched or lying player is not taken by a hold (agent cqc's cqc_m3: a real client's Circle held on one
    // grabs nothing): no grab, its gun or its other moves instead (beta 23 held him, and with cqc's hold that reaches nothing, a grab begun
    // and over at once kept it from another 10 s)
    if (gCqcFight && StanceOf(gSeen[b.target]) != S_STAND) { if (Dist2D(s.pos, gSeen[b.target].pos) < std::min(reach, kGrabReach)) ++gCqcFightCount[s.k & 15].noGrab; return false; }
    if (gFrame - gKnockedAt[b.target] < 1500) return false;   // (on the floor: shot, not grabbed again)
    for (const Body& o : gBodies)                    // (an ally holds him, or he, a virtual doll, grabs this one already)
        if (&o != &b && o.grabStart >= 0 && (o.grabbed == b.target || o.grabbed == s.slot)) return false;
    const PlayerNow& t = gSeen[b.target];
    return t.object && t.alive && Dist2D(s.pos, t.pos) < reach && std::fabs(t.pos[1] - s.pos[1]) < 400.f;
}
// (v150-host beta 24, cqc) its CQC throw as its character's in the game (docs/VIRTUAL_DOLL.md 40): the hold, then the stun the game's
// table gives its character in this rule (vdoll_cqc.h HoldStun: 10000 a soldier's, Sokolov's or Ocelot's, 30000 Raikov's, Reiko's and
// Rowdy's, the whole gauge Snake's, 7500 in SNEAKING but Snake's), sent at its thrown motion's frame after the hold record (33 a
// soldier's, measured: cqc_m1, 30 to 36; 50 Raikov's, Ocelot's and Snake's; 174 Reiko's and Rowdy's). Beta 23 sent 30000 (a knock out at once)
// 56 frames after the hold whatever its character: three times a soldier's throw (the owner's logs of 14 November: 40 of the 45
// knock-outs of a virtual doll after a hold came from another virtual doll's). MGO1_VDOLL_CQC=0: beta 23's
bool gCqc = true;
int OwnClass(const Self& s) { return s.slot >= 0 && s.slot < 16 && gHave[s.slot] && gSeen[s.slot].object ? gSeen[s.slot].cls : -1; }
void StartGrab(Body& b, const Self& s) {
    b.grabbed = b.target; b.grabStart = gFrame; b.rushUntil = -1;
    const int cls = OwnClass(s);
    b.grabStunAt = !gCqc ? 56 : vcqc::kAfterHold + vcqc::HoldStunFrame(cls < 0 ? 0 : cls);   // (its character not known yet: a soldier's)
    if (b.reloadAt >= 0) { b.reloadAt = -1; b.reloadUntil = 0; }   // (its reload dropped: the M1911A1 in its hand)
    if (Allowed() & 2) { if (b.held != b.secondary) Draw(b, b.secondary); }   // (beta 16: no secondary, the knife it always has, a CQC weapon; beta 18: the Mk22 is one too)
    b.grabAt = std::max(gFrame + (b.drawn ? 30 : 14), b.switchUntil + 12);   // (the draw's 18 frames, then Circle 2 or 3 records)
    if (gDebug) BLOG("vdoll %d: grabbing player %d (%.1f m)", s.k + 1, b.grabbed + 1, Dist2D(s.pos, gSeen[b.grabbed].pos) / 1000.f);
    b.grabSent = b.grabStun = b.grabOverSaid = false;
    b.nextGrabAt = gFrame + 600;
    b.aiming = false; StandUp(b); b.lean = 0; b.path.clear(); b.speed = 0;
}
bool StartRush(Body& b, const Self& s) {
    if (!CanGrab(b, s, gCqcTest ? 25000.f : 4000.f) || b.rushUntil >= gFrame) return false;
    const float d = Dist2D(s.pos, gSeen[b.target].pos);
    if (!PlanTo(b, s, gSeen[b.target].pos)) return false;   // (a way to him on the map: the rush runs it)
    StandUp(b);                                       // (crouched: up first, then the rush)
    b.rushUntil = std::max(gFrame, b.standUntil) + (gCqcTest ? 400 : 90); b.rushPlanAt = gFrame;
    b.aiming = false; b.lean = 0; b.objWay = false;
    if (gDebug) BLOG("vdoll %d: rushes at player %d (%.1f m)", s.k + 1, b.target + 1, d / 1000.f);
    return true;
}
// the rush: along its way to him (made again every 0.5 s as he moves), the last 2.5 m straight at him when the line is
// clear; within 1.1 m the grab
void Rush(Body& b, const Self& s, int dt) {
    if (b.target < 0 || !gHave[b.target] || !gSeen[b.target].alive) { b.rushUntil = -1; return; }
    if (gFrame < b.standUntil || ShotHold(b)) return;   // (beta 21: its last shot's record first)
    const PlayerNow& t = gSeen[b.target];
    const float d = Dist2D(s.pos, t.pos);
    if (d < kGrabReach) { if (CanGrab(b, s, kGrabReach + 400.f)) StartGrab(b, s); else b.rushUntil = -1; return; }
    float feet[3];
    Feet(s.pos, feet);
    const float y = Bearing(s.pos, t.pos);
    if (d < 2500.f && std::fabs(t.pos[1] - s.pos[1]) < 400.f && gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), d, true) >= d - 300.f) {
        const float step = std::min(kRun * dt, d - 800.f);
        if (step <= 0.f) return;
        const float x = s.pos[0] + std::sin(y) * step, z = s.pos[2] + std::cos(y) * step;
        const float f = FloorNear(x, z, b.floorY);
        if (f > -1e29f && std::fabs(f - b.floorY) <= 300.f) {
            if (gTurn) TurnBody(b, y, dt, kBodyTurn, true); else b.yaw = y;   // (beta 21: turned, as in a fight)
            s.pos[0] = x; s.pos[2] = z; b.floorY = f; s.pos[1] = f + kHips;
            b.speed = dt ? step / dt : 0; b.ran += step;
            return;
        }
    }
    if (b.next >= b.path.size() || gFrame - b.rushPlanAt >= 30) {
        b.rushPlanAt = gFrame;
        if (!PlanTo(b, s, t.pos)) {
            b.rushUntil = -1;
            if (gDebug) BLOG("vdoll %d: its rush stopped %.1f m from player %d (no way to him)", s.k + 1, d / 1000.f, b.target + 1);
            return;
        }
    }
    Run(b, s, dt);
}
void Grab(Body& b, const Self& s) {
    b.speed = 0; b.aiming = false;
    const int v = b.grabbed;
    if (!b.grabSent) {
        if (v >= 0 && gHave[v]) { if (gTurn) TurnBody(b, Bearing(s.pos, gSeen[v].pos), 1, kBodyTurn, true); else b.yaw = Bearing(s.pos, gSeen[v].pos); }   // (beta 21: turned, as in a fight)
        if (gFrame < b.grabAt) return;
        if (v < 0 || !gHave[v] || !gSeen[v].object || !gSeen[v].alive || Dist2D(s.pos, gSeen[v].pos) > 1500.f || std::fabs(gSeen[v].pos[1] - s.pos[1]) > 400.f) {
            if (gDebug && v >= 0) BLOG("vdoll %d: its grab missed player %d", s.k + 1, v + 1);
            b.grabStart = b.grabAt = -1; b.grabbed = -1;
            return;
        }
        // (v150-host beta 24, cqc) a crouched or lying player is not taken by a hold (cqc_m3, a real client's Circle held on one: its grab
        // motion, no hold record, no stun): its grab reaches nothing (MGO1_VDOLL_CQC=0: held as beta 23)
        if (gCqc && StanceOf(gSeen[v]) != S_STAND) {
            BLOG("vdoll %d: its grab reaches nothing: player %d is %s", s.k + 1, v + 1, StanceOf(gSeen[v]) == S_LIE ? "lying" : "crouched");
            b.grabStart = b.grabAt = -1; b.grabbed = -1;
            return;
        }
        const uint16_t f16 = Angle16(b.yaw);
        s.send(s.ctx, s.slot, 1, {0x0C, (uint8_t)v, (uint8_t)f16, (uint8_t)(f16 >> 8)});
        s.send(s.ctx, s.slot, 1, {0x08, 0x00, 0x00});
        b.sent08 = false;
        std::vector<uint8_t> r = {0x0D, (uint8_t)s.slot, 0x01, 0x01};
        Put16(r, (uint16_t)Tenth(s.pos[0])); Put16(r, (uint16_t)Tenth(s.pos[1] + 165.f)); Put16(r, (uint16_t)Tenth(s.pos[2])); Put16(r, f16);
        s.send(s.ctx, v, 1, r);
        b.grabSent = true;
        return;
    }
    // (v150-host beta 16) he left or died meanwhile: no hold told, no throw
    if (v < 0 || v >= 16 || !gHave[v] || !gSeen[v].object || (!b.grabStun && !gSeen[v].alive)) { b.grabStart = b.grabAt = -1; b.grabbed = -1; return; }
    if (!b.grabStun && gFrame >= b.grabAt + b.grabStunAt) {
        const int cls = OwnClass(s);
        // (v150-host beta 24, cqc) the game's stun for its character in this rule (beta 23 and MGO1_VDOLL_CQC=0: 30000 whoever it is)
        const int stun = gCqc ? vcqc::HoldStun(cls < 0 ? 0 : cls, gRule, gSeen[v].stunMax > 0 ? (int)gSeen[v].stunMax : vcqc::kGauge, false) : 30000;
        s.send(s.ctx, v, 0, {0x04, (uint8_t)s.slot, 0x00, 0x00, (uint8_t)(stun & 0xFF), (uint8_t)((stun >> 8) & 0xFF), 0x00, 0x00, 0x00});
        b.grabStun = true; ++b.grabs; gKnockedAt[v] = gFrame;
        if (gCqc) BLOG("vdoll %d: grabs player %d and throws him down (its character %d: the stun %d)", s.k + 1, v + 1, cls, stun);
        else BLOG("vdoll %d: grabs player %d and throws him down", s.k + 1, v + 1);
    }
    if (gFrame >= b.grabAt + std::max(84LL, b.grabStunAt + 28)) { b.grabStart = b.grabAt = -1; b.grabbed = -1; }
}
// (v150-host beta 16, the weapons setting) unarmed (support only, knife only): its fists, as a player's punches (the bench
// K_knife_full2, two full dolls in a knife only game: Circle held, {08 0A} three records 7 to 9 frames apart, then the hit
// {14 id 12 01 part, the point hit in tenths, a push of about 30} on his channel 2, {04 id, life 0, stamina 4500, 0x97}
// on his channel 0, {08 00} on its own; a punch every 30 frames, within half a metre). On its way to him (a straight run
// within 2.5 m, else a way made again every 0.5 s); with MGO1_VDOLL_PUNCHES=1 only (its knife strikes otherwise, below):
// seven hits (31500 of the stamina's 30000) knock him out, left 20 s
// (v150-host beta 16) the knife's strike, as a player's (the bench K2_knife_full2, two full dolls striking with the knife:
// {08 00} and ten zero bytes, {08 09} twice 6 frames apart, then the hit {14 id 42 01 part, the point hit, a push of about
// 30} on his channel 2, {04 id, life 30, stamina 0, the knife 0x01} on his channel 0, {08 00} on its own; a strike every
// 25 to 32 frames, hits from 0.4 to 1.1 m, standing or down: 4 kills each in 3 minutes). Its knife is always in its
// hands here (knife only, support only), so it strikes, down or up; MGO1_VDOLL_PUNCHES=1 (tests): the punches instead
const float kKnifeReach = 1000.f;
bool KnifeStrikes() { static const bool punches = getenv("MGO1_VDOLL_PUNCHES") && *getenv("MGO1_VDOLL_PUNCHES") == '1'; return !punches; }
const float kPunchReach = 900.f;
bool Melee(Body& b, const Self& s, int dt) {
    b.melee = false;
    if (b.target < 0 || b.carrying || !gHave[b.target] || !gSeen[b.target].object || !gSeen[b.target].alive) { b.punchTicks = 0; return false; }
    if (b.target != b.punchTarget) { b.punchTarget = b.target; b.punchHits = 0; b.punchTicks = 0; }
    if (gFrame - b.seenAt > 20) {
        b.aiming = false; b.punchTicks = 0;
        if (gFrame >= b.chaseAt) { b.chaseAt = gFrame + 45; b.path.clear(); PlanTo(b, s, b.tpos); }
        return false;
    }
    const bool knife = KnifeStrikes();
    if (!knife && gFrame - gKnockedAt[b.target] < 1200) { b.target = -1; b.punchTicks = 0; return false; }
    const PlayerNow& t = gSeen[b.target];
    const float d = Dist2D(s.pos, t.pos);
    b.melee = d < 3000.f;
    b.aiming = false;
    if (d > (knife ? kKnifeReach : kPunchReach) || std::fabs(t.pos[1] - s.pos[1]) > 400.f) {
        b.punchTicks = 0;
        if (gFrame < b.standUntil) return true;
        StandUp(b);
        float feet[3];
        Feet(s.pos, feet);
        const float y = Bearing(s.pos, t.pos);
        if (d < 2500.f && std::fabs(t.pos[1] - s.pos[1]) < 400.f && gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), d, true) >= d - 300.f) {
            const float step = std::min(kRun * dt, d - 500.f);
            if (step > 0.f) {
                const float x = s.pos[0] + std::sin(y) * step, z = s.pos[2] + std::cos(y) * step;
                const float f = FloorNear(x, z, b.floorY);
                if (f > -1e29f && std::fabs(f - b.floorY) <= 300.f) {
                    if (gTurn) TurnBody(b, y, dt, kBodyTurn, true); else b.yaw = y;   // (beta 21: turned, as in a fight)
                    s.pos[0] = x; s.pos[2] = z; b.floorY = f; s.pos[1] = f + kHips;
                    b.speed = dt ? step / dt : 0; b.ran += step;
                    return true;
                }
            }
        }
        if (b.next >= b.path.size() || gFrame - b.rushPlanAt >= 30) {
            b.rushPlanAt = gFrame;
            if (!PlanTo(b, s, t.pos)) { b.target = -1; return false; }
        }
        Run(b, s, dt);
        return true;
    }
    b.speed = 0;
    if (gTurn) TurnBody(b, Bearing(s.pos, t.pos), dt, kBodyTurn, true); else b.yaw = Bearing(s.pos, t.pos);   // (beta 21: turned, as in a fight)
    if (!s.tick) return true;
    if (knife) {
        ++b.punchTicks;
        if (b.punchTicks == 1) { s.send(s.ctx, s.slot, 1, {0x08, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); return true; }
        if (b.punchTicks < 5) { if (b.punchTicks >= 3) s.send(s.ctx, s.slot, 1, {0x08, 0x09, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); return true; }
        b.punchTicks = 0;
        const int v = b.target;
        const bool head = Rnd01() < 0.5f;
        float c[3];
        if (head) Head(t, c); else Chest(t, c);
        const float dir[3] = {(t.pos[0] - s.pos[0]) / std::max(1.f, d), 0.f, (t.pos[2] - s.pos[2]) / std::max(1.f, d)};
        std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, 0x42, 0x01, (uint8_t)(head ? 4 : 2)};
        Put16(fx, (uint16_t)Tenth(c[0])); Put16(fx, (uint16_t)Tenth(c[1])); Put16(fx, (uint16_t)Tenth(c[2]));
        for (int k = 0; k < 3; ++k) Put16(fx, (uint16_t)(int16_t)lrintf(dir[k] * 30.f));
        s.send(s.ctx, v, 2, fx);
        s.send(s.ctx, v, 0, {0x04, (uint8_t)s.slot, 0x1E, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});
        s.send(s.ctx, s.slot, 1, {0x08, 0x00, 0x00});
        ++b.hits; b.hitAt = gFrame;
        BLOG("vdoll %d: strikes player %d with its knife at %.1f m", s.k + 1, v + 1, d / 1000.f);
        return true;
    }
    if (++b.punchTicks < 4) { s.send(s.ctx, s.slot, 1, {0x08, 0x0A, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}); return true; }
    b.punchTicks = 0;
    const int v = b.target;
    const bool head = Rnd01() < 0.5f;
    float c[3];
    if (head) Head(t, c); else Chest(t, c);
    const float dir[3] = {(t.pos[0] - s.pos[0]) / std::max(1.f, d), 0.f, (t.pos[2] - s.pos[2]) / std::max(1.f, d)};
    std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, 0x12, 0x01, (uint8_t)(head ? 4 : 2)};
    Put16(fx, (uint16_t)Tenth(c[0])); Put16(fx, (uint16_t)Tenth(c[1])); Put16(fx, (uint16_t)Tenth(c[2]));
    for (int k = 0; k < 3; ++k) Put16(fx, (uint16_t)(int16_t)lrintf(dir[k] * 30.f));
    s.send(s.ctx, v, 2, fx);
    s.send(s.ctx, v, 0, {0x04, (uint8_t)s.slot, 0x00, 0x00, 0x94, 0x11, 0x97, 0x00, 0x00});
    s.send(s.ctx, s.slot, 1, {0x08, 0x00, 0x00});
    ++b.punchHits;
    BLOG("vdoll %d: punches player %d (%d)", s.k + 1, v + 1, b.punchHits);
    if (b.punchHits >= 7) { gKnockedAt[v] = gFrame; b.punchHits = 0; BLOG("vdoll %d: player %d knocked out by its punches", s.k + 1, v + 1); }
    return true;
}
// (v150-host beta 21) instead of aiming this frame (MGO1_VDOLL_STANDOFF): its M37 out of reach closes in as a player with a
// shotgun runs at him (a way to him, planned again every 45 frames, until 2 m within its reach; no way: its handgun if that
// reaches), or the move Engage started (to moveUntil); true: it runs this frame, its gun down. Fight stood aiming with the M37
// beyond 16 m, nothing moving it (runs/V20_cap9: the blue doll 26 m from the red ones, 446 s). Asked before Fight's "out of
// sight 20 frames" chase: the guard's step runs to its end (a place that sees him) even when a corner hides him on the way
bool Unstall(Body& b, const Self& s) {
    if (!gStandoff || b.target < 0) return false;
    const Weapon& w = kWeapons[b.held];
    if (w.pellets) {
        float eye[3], c[3];
        Eye(s, eye);
        PlayerNow t = gSeen[b.target];
        memcpy(t.pos, b.tpos, sizeof t.pos);
        Chest(t, c);
        const float dist = Dist3(eye, c);
        if (dist > (b.closing ? w.far - 2000.f : w.far)) {
            if (gFrame >= b.chaseAt) {
                b.chaseAt = gFrame + 45; b.objWay = false;
                if (!PlanTo(b, s, b.tpos)) {              // (no way to him: its handgun when that reaches, else Engage leaves him)
                    b.path.clear(); b.closing = false;
                    const bool gun = (Allowed() & 2) && b.mag[b.secondary] > 0 && dist <= kWeapons[b.secondary].far;
                    if (gDebug && gFrame - b.noWayAt > 300) {
                        b.noWayAt = gFrame;
                        BLOG("vdoll %d: player %d at %.1f m, beyond its %s's reach, no way to him%s%s", s.k + 1, b.target + 1, dist / 1000.f, w.name, gun ? ": its " : "", gun ? kWeapons[b.secondary].name : "");
                    }
                    if (gun) Draw(b, b.secondary);
                    return false;
                }
                if (!b.closing) { b.closing = true; ++b.closeIns; if (gDebug) BLOG("vdoll %d: player %d at %.1f m, beyond its %s's reach: closes in", s.k + 1, b.target + 1, dist / 1000.f, w.name); }
            }
            return b.closing && b.next < b.path.size();
        }
    }
    b.closing = false;
    return gFrame < b.moveUntil && b.next < b.path.size();
}
// ---- (v150-host beta 18) the fight "as a player" (MGO1_VDOLL_CONTROLS=player): Fight's decisions through a player's aim ----
// its aim down (out of a fight, reloading, a roll, a throw, a grab): the next fight readies the gun again
void CtlLower(Body& b) {
    b.ctl = C_LOWERED; b.pullAt = -1; b.trigger = false;
    b.viewV[0] = b.viewV[1] = 0.f; b.viewSgn[0] = b.viewSgn[1] = 0;
    for (long long& k : b.kickAt) k = -1;
}
// the readied turn of the third person over dt frames: each frame a part of what is left (3 to 16 degrees), the level's
// own turn a ceiling, never past him
void ReadiedTurn(Body& b, float want, float levelTurn, int dt) {
    for (int i = 0; i < dt; ++i) {
        const float e = gTurn ? TurnLeft(b, want, true) : Wrap(want - b.yaw), a = std::fabs(e) * 180.f / kPi;   // (beta 21: no spin, TurnBody's way round)
        const float step = std::min(a, std::min(levelTurn, std::max(kTpsMin, std::min(kTpsMax, kTpsPart * a)))) * kPi / 180.f;
        b.yaw = Wrap(b.yaw + (e < 0.f ? -step : step));
    }
}
// the first person view's turn of one axis over dt frames, in the game's ticks of 2 frames (the stick read once a tick): from
// rest 0.89 degree a tick, 0.23 more each tick held, 4 at most (2 a frame; the level's own turn a ceiling); at rest again on
// him or turning back (the brake)
void ViewTurn(float& cur, float want, float& v, int& sgn, float levelTurn, int dt, bool yaw) {
    for (int i = dt - 1; i >= 0; --i) {
        if ((gFrame - i) & 1) continue;               // (the view moves on even frames only)
        const float e = yaw ? Wrap(want - cur) : want - cur, a = std::fabs(e) * 180.f / kPi;
        const int sg = e < 0.f ? -1 : 1;
        if (a < 0.01f) { v = 0.f; sgn = 0; cur = yaw ? Wrap(want) : want; continue; }
        if (sg != sgn) v = 0.f;
        sgn = sg;
        v = std::min(2.f * levelTurn, v <= 0.f ? 2.f * kViewV0 : std::min(2.f * kViewMax, v + 2.f * kViewMore * 2.f));
        if (a <= v) { cur = yaw ? Wrap(want) : want; v = 0.f; sgn = 0; }
        else cur += (float)sg * v * kPi / 180.f;
        if (yaw) cur = Wrap(cur);
    }
}
void ReleaseTrigger(Body& b) {
    if (b.trigger && kWeapons[b.held].automatic) { b.burstLeft = 0; b.burstEnd = gFrame; }
    b.trigger = false; b.pullAt = -1;
}
int GunFor(const Body& b, float d);
bool FightPlayer(Body& b, const Self& s, int dt) {
    const Level L = RowOf(b, true);
    if (b.target >= 0 && gFrame < b.reloadUntil && RifleUp(b)) { b.speed = 0; return true; }   // (beta 19: a scoped rifle's reload in its scope, where it stands)
    if (b.target < 0 || b.carrying || b.unarmed || gFrame < b.reloadUntil) { b.aiming = false; StandUp(b); b.lean = 0; CtlLower(b); return false; }
    if (Unstall(b, s)) { b.aiming = false; StandUp(b); b.lean = 0; CtlLower(b); return false; }   // (v150-host beta 21)
    if (gFrame - b.seenAt > 20) {
        b.aiming = false; StandUp(b); b.lean = 0; CtlLower(b);
        if (gFrame >= b.chaseAt) { b.chaseAt = gFrame + 45; b.path.clear(); PlanTo(b, s, b.tpos); }
        return false;
    }
    b.speed = 0;
    if ((b.role != R_SOLDIER || gDartTest) && gFrame >= b.switchUntil && b.burstLeft <= 0 && !b.trigger) {   // (as Fight: a sneaker's or Snake's gun for that distance)
        const int want = GunFor(b, Dist2D(s.pos, b.tpos));
        if (want != b.held) { Draw(b, want); CtlLower(b); }
    }
    if (IsFlame(b.held) && (b.burstLeft > 0 || b.trigger) && Dist2D(s.pos, b.tpos) > kFlameKeep) { ReleaseTrigger(b); b.burstLeft = 0; b.burstEnd = gFrame; }   // (v150-host beta 22, heavy: he left its flame's reach: its stream over)
    if (b.heavyW >= 0 && gFrame >= b.switchUntil && b.burstLeft <= 0 && !b.trigger && b.rpgPullAt < 0 && gFrame >= b.rpgShowUntil) {   // (v150-host beta 22, heavy) its RPG-7 or flamethrower for this fight
        const int want = HeavyWant(b, s, b.tpos);
        if (want >= 0 && b.held != want) { Draw(b, want); CtlLower(b); }
        else if (want < 0 && b.held == b.heavyW) { Draw(b, b.primary); CtlLower(b); }
    }
    const Weapon& w = kWeapons[b.held];
    if (gScope && Scoped(b.held)) RaiseRifle(b);      // (beta 19: a scoped rifle raised where it stands)
    if (!b.aiming) CtlLower(b);                       // (its aim ended elsewhere since)
    // (the SVD and the MOSIN NAGANT: their scope's view on by itself, the stick turning the view, no readied third person
    // (docs/PLAYABLE_DOLL.md 3, probe 15): the view coming up at once, then its turn only)
    const bool sniper = w.code == 0x0F || w.code == 0x10 || w.code == 0x11;   // (v150-host beta 22, heavy: the RPG-7's sight the same)
    if (sniper && (b.ctl == C_LOWERED || b.ctl == C_ARMING || b.ctl == C_READIED)) {
        ReleaseTrigger(b);
        b.ctl = C_ENTERING; b.ctlUntil = gFrame + kEntryMin + (long long)(Rnd() % (kEntryMax - kEntryMin + 1)); b.pitch = 0.f;
        b.viewV[0] = b.viewV[1] = 0.f; b.viewSgn[0] = b.viewSgn[1] = 0;
    }
    if (b.ctl == C_LOWERED) { b.ctl = C_ARMING; b.ctlUntil = gFrame + kArmMin + (long long)(Rnd() % (kArmMax - kArmMin + 1)); }
    if (b.ctl == C_ARMING && gFrame >= b.ctlUntil) b.ctl = C_READIED;
    if (b.ctl == C_ENTERING && gFrame >= b.ctlUntil) { b.ctl = C_VIEW; ++gCtlCount[s.k & 15].entries; }
    PlayerNow ts = gSeen[b.target];
    const float ahead = (float)(gFrame - b.tposAt + 2);
    for (int k = 0; k < 3; ++k) ts.pos[k] = b.tpos[k] + b.tvel[k] * ahead;
    // the view: its fight's head beyond 10 m, a lean (only in the view), the SVD's scope, the headshots only rule
    const float flat = Dist2D(s.pos, ts.pos);
    const bool hs = gHost.headshotsOnly && gHost.headshotsOnly();
    // (v150-host beta 24, walls3) MGO1_VDOLL_MUZZLE: its muzzle's line to where its third person bullet goes (his chest within the
    // auto aim's 10 m, beyond it level from the muzzle) stopped by a cover its eye sees over: its first person view (asked every 6
    // frames, the game's things in it; a heavy weapon apart)
    if (gMuzzle && !w.heavy && (gFrame - b.muzzleAt >= 6 || gFrame < b.muzzleAt)) {
        b.muzzleAt = gFrame;
        float to[3];
        if (flat <= kViewFrom) Chest(ts, to);
        else { to[0] = ts.pos[0]; to[1] = s.pos[1] + (b.low ? kMuzzleLow : kMuzzle); to[2] = ts.pos[2]; }
        // ((v150-host beta 24, review) a teammate's body on its gun's line and not on its eye's (MGO1_VDOLL_CQCFIGHT): its view, whose
        // bullet leaves by its eye, as a player raises his view to shoot over a crouched teammate)
        const bool hid = !MuzzleClear(b, s, Bearing(s.pos, ts.pos), to, Power(w)) || (gCqcFight && gPlayerCtl && ViewOverMate(b, s, Bearing(s.pos, ts.pos), to));
        if (hid && b.muzzleFight != b.fights && gFrame - b.seenAt <= 2) {   // (counted once a fight, its eye seeing him then)
            b.muzzleFight = b.fights; ++b.muzzleViews; ++gMuzzleViews;
            if (gDebug) BLOG("vdoll %d: its gun's line to player %d stopped (%s, %.1f m, %s): its first person view", s.k + 1, b.target + 1, b.low ? "crouched" : "standing",
                             flat / 1000.f, flat <= kViewFrom ? "his chest" : "level from its muzzle");
        }
        b.muzzleHid = hid;
    }
    const bool muzzleHid = gMuzzle && !w.heavy && b.muzzleHid;
    const bool must = sniper || b.leanWant != 0 || hs || muzzleHid;
    const bool wantView = must || (b.head && flat > kViewFrom);
    const float offView = std::fabs(Wrap(Bearing(s.pos, ts.pos) - b.yaw)) * 180.f / kPi;
    if ((b.ctl == C_VIEW || b.ctl == C_ENTERING) && (!wantView || (!must && flat < kViewNear) || (!sniper && offView > kViewOff))) {
        ReleaseTrigger(b); b.ctl = C_READIED; b.viewV[0] = b.viewV[1] = 0.f;
        for (long long& k : b.kickAt) k = -1;
    }
    const bool view = b.ctl == C_VIEW;
    const int leanNow = view ? b.leanWant : 0;
    if (b.lean != leanNow) { if (leanNow) ++b.leans; b.lean = leanNow; }
    float eye[3], at[3];
    if (view && b.head) Head(ts, at);
    else if (view || flat <= kViewFrom) Chest(ts, at);   // (the view's chest; within 10 m the auto aim's: the body)
    else { at[0] = ts.pos[0]; at[1] = s.pos[1] + kMuzzle; at[2] = ts.pos[2]; }   // (beyond it the third person's bullets go level from its muzzle)
    if (IsRocket(b.held)) RocketAim(ts, at);         // (v150-host beta 22, heavy: his chest)
    {
        float low[3];
        EyePose(s, true, b.lean, b.yaw, low);
        const bool mayCrouch = !b.low && b.level >= 4 && Dist2D(s.pos, ts.pos) >= 8000.f;
        const bool seenLow = (b.low || mayCrouch) && VisibleLow(b, low, ts);   // (beta 21, geom: the things from its memo)
        if (mayCrouch && seenLow) { b.low = true; ++b.crouches; }
        else if (b.low && !seenLow) StandUp(b);
    }
    EyeNow(b, s, eye);
    const float wantYaw = Bearing(eye, at), wantPitch = std::atan2(eye[1] - at[1], std::max(1.f, Dist2D(eye, at)));
    if (b.ctl == C_ENTERING) {}                       // (the view coming up: nothing turns)
    else if (view) {
        ViewTurn(b.yaw, wantYaw, b.viewV[0], b.viewSgn[0], L.turn, dt, true);
        ViewTurn(b.pitch, wantPitch, b.viewV[1], b.viewSgn[1], L.turn, dt, false);
        for (int i = 0; i < 4; ++i)                   // (its recoil: the view thrown up, a little aside)
            if (b.kickAt[i] >= 0 && gFrame >= b.kickAt[i]) { b.pitch -= b.kick[i][0]; b.yaw = Wrap(b.yaw + b.kick[i][1]); b.kickAt[i] = -1; }
    } else { ReadiedTurn(b, wantYaw, L.turn, dt); b.pitch = wantPitch; }   // (the third person: its bullets go level, the auto aim's to the body)
    if (b.ctl == C_READIED && wantView && std::fabs(Wrap(wantYaw - b.yaw)) * 180.f / kPi <= kViewFaced) {
        ReleaseTrigger(b);
        b.ctl = C_ENTERING; b.ctlUntil = gFrame + kEntryMin + (long long)(Rnd() % (kEntryMax - kEntryMin + 1)); b.pitch = 0.f;
        b.viewV[0] = b.viewV[1] = 0.f; b.viewSgn[0] = b.viewSgn[1] = 0;
    }
    if (!b.aiming) { b.aiming = true; b.firstAim = true; }
    // (beta 21: asked every frame of the fight, the readied flag and the view's entry included, so that its wait runs with the
    // others: the longest, not their sum)
    const bool copyHeld = CopyHold(b, s);
    if (b.ctl != C_READIED && b.ctl != C_VIEW) return true;   // (no bullet before the readied flag, nor while the view comes up)
    if (gScope && Scoped(b.held) && (!ScopeOn(b) || b.scopeRecs < 2 || !CopyAtRest(b, s))) { ReleaseTrigger(b); return true; }   // (beta 19: nor before its scope is up, its copy at rest)
    if (gFrame - b.firstSeen < L.react || gFrame < b.switchUntil) { ReleaseTrigger(b); return true; }
    const float tol = L.tol * kPi / 180.f;
    const float ey = std::fabs(Wrap(wantYaw - b.yaw)), ep = std::fabs(wantPitch - b.pitch);
    const float dist = Dist3(eye, at);
    const bool onePull = view && w.automatic && dist >= kOnePullFrom;
    float d[3];
    if (b.trigger) {                                  // Square held: the pull's first frames, an automatic's burst
        if (copyHeld) { ReleaseTrigger(b); return true; }   // (beta 21: its copy out of his line meanwhile)
        if (gFrame - b.seenAt > 8 || (b.lastBullet > b.pullAt && (ey > 3.f * tol || ep > 3.f * tol))) { ReleaseTrigger(b); return true; }
        if (gFrame - b.pullAt < (IsRocket(b.held) ? 0 : kPullFrames) || gFrame < b.nextShot) return true;   // (v150-host beta 22: the RPG-7's own pull is its 14 frames)
        Dir(b.yaw, b.pitch, d);
        if (AllyInLine(s, eye, d, dist, L.err * kPi / 180.f)) { ReleaseTrigger(b); return true; }
        if (b.mag[b.held] <= 0) { ReleaseTrigger(b); Spent(b, dist); return true; }
        Shoot(b, s, w);
        if (b.heavyHeld) { b.heavyHeld = false; ReleaseTrigger(b); b.nextShot = gFrame + 12; return true; }   // (v150-host beta 22, heavy: its rocket not fired)
        b.lastBullet = gFrame;
        b.nextShot = gFrame + w.every + (w.automatic ? 0 : L.pause / 2);
        if (view)
            for (int i = 0; i < 4; ++i)
                if (b.kickAt[i] < 0) {
                    b.kickAt[i] = gFrame + 3 + (long long)(Rnd() % 5);
                    b.kick[i][0] = (0.8f + 0.4f * Rnd01()) * kPi / 180.f; b.kick[i][1] = (Rnd01() - 0.5f) * 0.6f * kPi / 180.f;
                    break;
                }
        if (!w.automatic || --b.burstLeft <= 0) ReleaseTrigger(b);
        if (b.mag[b.held] <= 0) Spent(b, dist);
        return true;
    }
    if (gFrame - b.seenAt > 8 || gFrame < b.nextShot) return true;
    if (ey > tol || ep > tol) { b.onSince = -1; return true; }
    if (b.fightShots == 0) {                          // its aim settling on him (its level's own)
        if (b.onSince < 0) b.onSince = gFrame;
        if (gFrame - b.onSince < L.settle) return true;
    }
    if (ReflexHold(b) || copyHeld) return true;       // (beta 21, the review: the reflex and its copy shown alongside the settling)
    if (w.pellets && dist > w.far) return true;
    if (onePull && gFrame - b.lastBullet < kOnePullGap) return true;
    if (w.automatic) {
        if (gFrame - b.burstEnd < L.pause) return true;
        b.burstLeft = onePull ? 1 : IsFlame(b.held) ? 1000 : L.burst;   // (v150-host beta 22, heavy: the flame a stream)
        const float e = L.err * kPi / 180.f * 0.7f;
        b.burstErr[0] = Gauss() * e; b.burstErr[1] = Gauss() * e;
    } else {
        const float e = L.err * kPi / 180.f * 0.5f;
        b.burstErr[0] = Gauss() * e; b.burstErr[1] = Gauss() * e;
    }
    Dir(b.yaw, b.pitch, d);
    if (AllyInLine(s, eye, d, dist, L.err * kPi / 180.f)) return true;
    if (b.mag[b.held] <= 0) { Spent(b, dist); return true; }
    if (gCqcTest && b.level >= 6) return true;
    b.trigger = true; b.pullAt = gFrame;              // (the pull: its bullet 3 frames later, wherever the aim is then)
    return true;
}
// the fight: its view turned to where he runs, its reaction, then its weapon's fire while he is in sight (an automatic's
// bursts, a single shot's rate); reloading, on its way; out of sight a moment, the gun down and after him to where it
// saw him last
int GunFor(const Body& b, float d);
bool Fight(Body& b, const Self& s, int dt) {
    if (gPlayerCtl) return FightPlayer(b, s, dt);     // (beta 18: as a player)
    const Level L = RowOf(b, true);
    if (b.target >= 0 && gFrame < b.reloadUntil && RifleUp(b)) { b.speed = 0; return true; }   // (beta 19: a scoped rifle's reload in its scope, where it stands)
    if (b.target < 0 || b.carrying || b.unarmed || gFrame < b.reloadUntil) { b.aiming = false; StandUp(b); b.lean = 0; return false; }
    if (Unstall(b, s)) { b.aiming = false; StandUp(b); b.lean = 0; return false; }   // (v150-host beta 21)
    if (gFrame - b.seenAt > 20) {
        b.aiming = false; StandUp(b); b.lean = 0;
        if (gFrame >= b.chaseAt) { b.chaseAt = gFrame + 45; b.path.clear(); PlanTo(b, s, b.tpos); }
        return false;
    }
    b.speed = 0;
    float eye[3], at[3];
    PlayerNow ts = gSeen[b.target];
    const float ahead = (float)(gFrame - b.tposAt + 2);
    for (int k = 0; k < 3; ++k) ts.pos[k] = b.tpos[k] + b.tvel[k] * ahead;
    if ((b.role != R_SOLDIER || gDartTest) && gFrame >= b.switchUntil && b.burstLeft <= 0) {   // (beta 18) a sneaker's or Snake's gun for that distance
        const int want = GunFor(b, Dist2D(s.pos, ts.pos));
        if (want != b.held) Draw(b, want);
    }
    if (IsFlame(b.held) && b.burstLeft > 0 && Dist2D(s.pos, ts.pos) > kFlameKeep) { b.burstLeft = 0; b.burstEnd = gFrame; }   // (v150-host beta 22, heavy: he left its flame's reach: its stream over)
    if (b.heavyW >= 0 && gFrame >= b.switchUntil && b.burstLeft <= 0 && b.rpgPullAt < 0 && gFrame >= b.rpgShowUntil) {   // (v150-host beta 22, heavy) its RPG-7 or flamethrower for this fight
        const int want = HeavyWant(b, s, ts.pos);
        if (want >= 0 && b.held != want) Draw(b, want);
        else if (want < 0 && b.held == b.heavyW) Draw(b, b.primary);
    }
    const Weapon& w = kWeapons[b.held];
    if (gScope && Scoped(b.held)) RaiseRifle(b);      // (beta 19: a scoped rifle raised where it stands)
    if (b.head) Head(ts, at); else Chest(ts, at);
    if (IsRocket(b.held)) RocketAim(ts, at);         // (v150-host beta 22, heavy: his chest)
    // (beta 15) its stance: leaning when it sees him only so; crouched from level 4 to fire from 8 m on while it still
    // sees him crouched (a smaller target), up again when it no longer does
    if (b.lean != b.leanWant) { if (b.leanWant) ++b.leans; b.lean = b.leanWant; }
    {
        float low[3];
        EyePose(s, true, b.lean, b.yaw, low);
        // (beta 16) the line from its crouched eye asked only when its answer can change its stance: standing, a crouch needs
        // its level and 8 m first (two lines of sight a frame for nothing in a fight within 8 m or under level 4)
        const bool mayCrouch = !b.low && b.level >= 4 && Dist2D(s.pos, ts.pos) >= 8000.f;
        const bool seenLow = (b.low || mayCrouch) && VisibleLow(b, low, ts);   // (beta 21, geom: the things from its memo)
        if (mayCrouch && seenLow) { b.low = true; ++b.crouches; }
        else if (b.low && !seenLow) StandUp(b);
    }
    EyeNow(b, s, eye);
    const float wantYaw = Bearing(eye, at), wantPitch = std::atan2(eye[1] - at[1], std::max(1.f, Dist2D(eye, at)));
    // (v150-host beta 21, spin) at most the readied turn's ceiling (16 degrees a frame; level 10 turned 26: 156 in a record)
    // and no spin: TurnBody's way round, its aim never held back
    const float step = (gTurn ? std::min(L.turn, kTpsMax) : L.turn) * kPi / 180.f * dt;
    const float ey = Wrap(wantYaw - b.yaw), ep = wantPitch - b.pitch;
    if (gTurn) TurnBody(b, wantYaw, dt, std::min(L.turn, kTpsMax), true);
    else b.yaw = Wrap(b.yaw + std::max(-step, std::min(step, ey)));
    b.pitch += std::max(-step, std::min(step, ep));
    if (!b.aiming) { b.aiming = true; b.firstAim = true; }
    const bool copyHeld = CopyHold(b, s);            // (beta 21: asked every frame of the fight, so that its wait runs with the others: the longest, not their sum)
    if (gFrame - b.firstSeen < L.react || gFrame < b.switchUntil) return true;
    if (gScope && Scoped(b.held) && (!ScopeOn(b) || b.scopeRecs < 2 || !CopyAtRest(b, s))) return true;   // (beta 19: no shot before its scope is up, its copy at rest)
    const float tol = L.tol * kPi / 180.f;
    if (gFrame - b.seenAt > 8 || gFrame < b.nextShot) return true;
    if (std::fabs(Wrap(wantYaw - b.yaw)) > tol || std::fabs(wantPitch - b.pitch) > tol) { b.onSince = -1; return true; }
    if (b.fightShots == 0) {                     // its aim settling on him
        if (b.onSince < 0) b.onSince = gFrame;
        if (gFrame - b.onSince < L.settle) return true;
    }
    // (v150-host beta 21, the review) the reflex and its copy shown asked after its level's reaction and settling, so that the
    // settling runs alongside them (before, it began once they were over: the waits were partly added)
    if (ReflexHold(b) || copyHeld) return true;
    // (v150-host beta 21, spin) no shot before a record has shown its facing within 30 degrees of the shot's: the players
    // saw a doll that flicked and fired between two records face elsewhere when its shot came
    if (gTurn && std::fabs(Wrap(b.sentYaw - b.yaw)) > 30.f * kPi / 180.f) return true;
    const float dist = Dist3(eye, at);
    if (w.pellets && dist > w.far) return true;  // (the M37 out of its reach: after him, Perceive's chase)
    if (w.automatic && b.burstLeft <= 0) {
        if (gFrame - b.burstEnd < L.pause) return true;
        b.burstLeft = IsFlame(b.held) ? 1000 : L.burst;   // (v150-host beta 22, heavy: the flame a stream while he is in it)
        const float e = L.err * kPi / 180.f * 0.7f;
        b.burstErr[0] = Gauss() * e; b.burstErr[1] = Gauss() * e;
    } else if (!w.automatic) {
        const float e = L.err * kPi / 180.f * 0.5f;
        b.burstErr[0] = Gauss() * e; b.burstErr[1] = Gauss() * e;
    }
    float d[3];
    Dir(b.yaw, b.pitch, d);
    if (AllyInLine(s, eye, d, dist, L.err * kPi / 180.f)) return true;
    if (b.mag[b.held] <= 0) { Spent(b, dist); return true; }
    if (gCqcTest && b.level >= 6) return true;        // (MGO1_VDOLL_CQC_TEST: no shot, its rush and grab only)
    if (gNadeTest && b.role != R_SOLDIER) return true;   // (MGO1_VDOLL_NADE_TEST: its stun grenades only)
    Shoot(b, s, w);
    if (b.heavyHeld) { b.heavyHeld = false; b.nextShot = gFrame + 12; return true; }   // (v150-host beta 22, heavy: its rocket not fired)
    b.nextShot = gFrame + w.every + (w.automatic ? 0 : L.pause / 2);
    if (w.automatic && --b.burstLeft <= 0) b.burstEnd = gFrame;
    if (b.mag[b.held] <= 0) Spent(b, dist);
    return true;
}

// ---- (v150-host beta 21) the guard on a fight with no shot (MGO1_VDOLL_STANDOFF, docs/VIRTUAL_DOLL.md 25): aimed at a target
// it sees, neither firing nor moving, 3 s and its level's reaction and settling (the waits a player has too left out: between
// two shots, a burst's pause, a reload) or 1 s with an ally in its line, it moves as a player would: a step aside (away from
// the ally, to a place that sees him), else closer to him, else it leaves him 5 s (no way from where it stands to a shot; at
// once after 1 s when its M37 does not reach him and there is no way to him). Why it held its fire is counted (Status) ----
enum Hold { H_NONE, H_REACT, H_SCOPE, H_SIGHT, H_VIEW, H_AIM, H_REACH, H_ALLY };
const char* const kHoldName[8] = {"", "reacting", "its scope", "him out of sight", "its view", "its aim", "out of reach", "an ally in its line"};
const int kStallFrames = 180, kAllyFrames = 60, kMoveFrames = 60, kIgnoreFrames = 300;
// why Fight held its fire this frame (its own order of checks, read again from its state)
int HoldNow(const Body& b, const Self& s) {
    const Level L = RowOf(b, true);
    const Weapon& w = kWeapons[b.held];
    if (gFrame < b.switchUntil || gFrame - b.firstSeen < L.react) return H_REACT;
    if (gScope && Scoped(b.held) && (!ScopeOn(b) || b.scopeRecs < 2)) return H_SCOPE;
    if (gFrame - b.seenAt > 8) return H_SIGHT;
    if (gPlayerCtl && b.ctl != C_READIED && b.ctl != C_VIEW) return H_VIEW;
    float eye[3], c[3], d[3];
    EyeNow(b, s, eye);
    PlayerNow t = gSeen[b.target];
    memcpy(t.pos, b.tpos, sizeof t.pos);
    Chest(t, c);
    const float dist = Dist3(eye, c);
    if (w.pellets && dist > w.far) return H_REACH;
    Dir(b.yaw, b.pitch, d);
    if (AllyInLine(s, eye, d, dist, L.err * kPi / 180.f)) return H_ALLY;
    if (b.fightShots == 0 && b.onSince >= 0 && gFrame - b.onSince < L.settle) return H_REACT;
    return H_AIM;
}
// a step 2 or 3.5 m aside of its line to him (first away from the nearest ally within 3 m), on a short way, to a place at
// least 1 m off its line that sees him (his chest or his head from its standing eye there: a step out of a corridor lost
// him, and its chase brought it back behind its ally, tests/vdoll_standoff_test.cpp pair on n011a) with no ally in the line
bool StepAside(Body& b, const Self& s, bool fromAlly, float* way) {
    const float yaw = Bearing(s.pos, b.tpos);
    float side = Rnd01() < 0.5f ? 1.f : -1.f, near = 3000.f;
    if (fromAlly)
        for (int id = 0; id < 16; ++id)
            if (id != s.slot && gHave[id] && gSeen[id].object && gSeen[id].alive && gSeen[id].team == s.team && Dist2D(gSeen[id].pos, s.pos) < near) {
                near = Dist2D(gSeen[id].pos, s.pos);
                const float lat = (gSeen[id].pos[0] - s.pos[0]) * std::cos(yaw) - (gSeen[id].pos[2] - s.pos[2]) * std::sin(yaw);
                side = lat > 0.f ? -1.f : 1.f;
            }
    PlayerNow t = gSeen[b.target];
    memcpy(t.pos, b.tpos, sizeof t.pos);
    float c[3];
    Chest(t, c);
    for (int i = 0; i < 4; ++i, side = -side) {
        const float d = i < 2 ? 2000.f : 3500.f;
        const float to[3] = {s.pos[0] + std::cos(yaw) * d * side, s.pos[1], s.pos[2] - std::sin(yaw) * d * side};
        b.objWay = false;
        if (!PlanTo(b, s, to)) continue;
        float len = 0.f;
        for (size_t k = 1; k < b.path.size(); ++k) len += Dist2D(b.path[k].p, b.path[k - 1].p);
        const WayPt& e = b.path.back();
        const float lat = (e[0] - s.pos[0]) * std::cos(yaw) - (e[2] - s.pos[2]) * std::sin(yaw);
        const float eye[3] = {e[0], e[1] + kHips + kEye, e[2]}, dist = Dist3(eye, c);
        const float dir[3] = {(c[0] - eye[0]) / dist, (c[1] - eye[1]) / dist, (c[2] - eye[2]) / dist};
        if (len < 3.f * d && std::fabs(lat) >= 1000.f && dist > 1.f && Visible(eye, t) && !AllyInLine(s, eye, dir, dist, RowOf(b, true).err * kPi / 180.f)) { *way = len; return true; }
        b.path.clear();
    }
    // ((v150-host beta 24, cqcfight) a teammate between them (the enemy's hold, his throw, his punches): round him, as a player goes round to
    // a line clear of his teammate: a place 2.5 to 5 m from him, 55 then 80 degrees off its line, each side (the steps aside above are
    // short of that from 5 m on: tests/vdoll_cqcfight_test.cpp, the hold at 0 degrees, 4 to 5 s)
    if (gCqcFight && fromAlly) {
        const float r = std::max(2500.f, std::min(5000.f, Dist2D(s.pos, b.tpos))), back = Bearing(b.tpos, s.pos);
        for (float a : {55.f, 80.f})
            for (int k = 0; k < 2; ++k, side = -side) {
                const float y = back + side * a * kPi / 180.f;
                const float to[3] = {b.tpos[0] + std::sin(y) * r, s.pos[1], b.tpos[2] + std::cos(y) * r}, d = Dist2D(s.pos, to);
                b.objWay = false;
                if (!PlanTo(b, s, to)) continue;
                float len = 0.f;
                for (size_t q = 1; q < b.path.size(); ++q) len += Dist2D(b.path[q].p, b.path[q - 1].p);
                const WayPt& e = b.path.back();
                const float lat = (e[0] - s.pos[0]) * std::cos(yaw) - (e[2] - s.pos[2]) * std::sin(yaw);
                const float eye[3] = {e[0], e[1] + kHips + kEye, e[2]}, dist = Dist3(eye, c);
                const float dir[3] = {(c[0] - eye[0]) / dist, (c[1] - eye[1]) / dist, (c[2] - eye[2]) / dist};
                if (len < 3.f * d && std::fabs(lat) >= 1000.f && dist > 1.f && Visible(eye, t) && !AllyInLine(s, eye, dir, dist, RowOf(b, true).err * kPi / 180.f)) { *way = len; return true; }
                b.path.clear();
            }
    }
    return false;
}
// ((v150-host beta 24, cqcfight2) its guard on a target fighting its teammate (within 1.5 m of him) when neither a step aside nor round
// him nor a fight's way to him was found (review24 F2: cqcfight kept him with no bound, its fire held while the teammate's body stayed
// on its line; with snakehouse2's fight ways crossing no fence, so across every fence). As a player: over the fence to him (a way out
// of the fight's rule, run once to its end, as a player climbs over), else a grenade at his feet when the game's friendly fire is off
// (it wounds no teammate then, a player's neither; its other rules: level 5, 6 to 22 m, its count and pace), else it waits for a clear
// line, a round (a second); after its third round in this fight it leaves him 5 s and goes on with the round, as beta 23 did at once
int FriendlyFire();
bool ThrowAtFeet(Body& b, const Self& s, const float* hips);
const int kKeptRounds = 3;
const char* KeepOn(Body& b, const Self& s, int t, float* way) {
    CqcFightCount& q = gCqcFightCount[s.k & 15];
    const long long key = s.life * 4096 + b.fights;
    if (gKeptKey[s.k & 15] != key) { gKeptKey[s.k & 15] = key; gKeptRounds[s.k & 15] = 0; }
    const int round = ++gKeptRounds[s.k & 15];
    b.objWay = false;
    if (round <= kKeptRounds && PlanTo(b, s, b.tpos, false, false, true)) {
        float len = 0.f;
        bool fence = false;
        for (size_t i = 1; i < b.path.size(); ++i) { len += Dist2D(b.path[i].p, b.path[i - 1].p); if (b.path[i - 1].area == doll::AREA_VAULT) fence = true; }
        *way = len;
        b.moveUntil = gFrame + std::min(240LL, (long long)(len / kRun) + (fence ? 72 : 12));   // (a fence's crossing about a second)
        ++q.over;
        return fence ? "goes over to him (a fence between them)" : "goes to him by another way";
    }
    b.path.clear();
    if (round <= kKeptRounds && FriendlyFire() == 0 && ThrowAtFeet(b, s, b.tpos)) { ++q.nades; return "throws a grenade at him (friendly fire off)"; }
    if (round < kKeptRounds) { ++q.kept; return "stays on him (he fights its teammate)"; }
    ++q.left;
    b.ignore = t; b.ignoreUntil = gFrame + kIgnoreFrames; b.target = -1;
    return "leaves him 5 s (he fights its teammate where it finds no line, no way)";
}
bool Engage(Body& b, const Self& s, int dt) {
    const int shots = b.shots;
    const bool on = Fight(b, s, dt);
    if (!gStandoff) return on;
    if (!on || !b.aiming || b.target < 0 || b.shots != shots) { b.stallF = b.allyF = 0; return on; }
    if ((gCqcTest && b.level >= 6) || (gNadeTest && b.role != R_SOLDIER)) return on;   // (the bench switches that hold its fire on purpose: as they were)
    const Weapon& w = kWeapons[b.held];
    if (gFrame < b.nextShot || gFrame < b.reloadUntil || (w.automatic && b.burstLeft <= 0 && gFrame - b.burstEnd < RowOf(b, true).pause) || b.trigger) return on;
    const int why = HoldNow(b, s);
    b.holdF[why] += dt;
    b.stallF += dt;
    // (out of its M37's reach here means Unstall found no way to him and its handgun does not reach either: nothing to gain by
    // standing or by a step; after the same second as with an ally in its line it leaves him and goes on with the round)
    const bool reach = why == H_REACH;
    b.allyF = why == H_ALLY || reach ? b.allyF + dt : 0;
    // (its level's own reaction and settling come on top of the 3 s: a level 1's first shot takes up to 2 s more than a level
    // 10's, and is no standoff)
    const Level L = RowOf(b, true);
    if (b.stallF < kStallFrames + L.react + L.settle && b.allyF < kAllyFrames) return on;
    const int t = b.target;
    const float secs = b.stallF / 59.94f;
    const char* did = "steps aside";
    b.path.clear();
    float way = 0.f;
    b.moveUntil = gFrame + kMoveFrames;
    if (!reach && StepAside(b, s, why == H_ALLY, &way)) b.moveUntil = gFrame + std::min(180LL, (long long)(way / kRun) + 12);   // (the step to its end)
    else if (!reach && (b.objWay = false, PlanTo(b, s, b.tpos))) did = "closes in";
    // ((v150-host beta 24, cqcfight) never left while he fights its teammate: left 5 s, he was out of its sight for its wake too, which then
    // ran to the teammate he had just thrown, beside him)
    else if (gCqcFight && !reach && AtMate(s, t)) did = KeepOn(b, s, t, &way);   // ((v150-host beta 24, cqcfight2) bounded: KeepOn)
    else { b.ignore = t; b.ignoreUntil = gFrame + kIgnoreFrames; b.target = -1; b.path.clear(); did = "leaves him 5 s"; }
    ++b.unstalls;
    if (!b.stallSaid) BLOG("vdoll %d: aimed at player %d %.1f s with no shot (its %s, held by %s): %s", s.k + 1, t + 1, secs, w.name, kHoldName[why], did);
    b.stallSaid = true;                               // (once a life; Status counts them all)
    b.stallF = b.allyF = 0;
    b.aiming = false; StandUp(b); b.lean = 0;
    if (gPlayerCtl) CtlLower(b);
    return false;
}

// ---- its records every 100 ms, in a PC's order ----
// (v150-host beta 22, bookplay) its place record in this tick of a reading: every kBookPlaceTicks-th from the one after its long
// record, as a reader's game sends it (MGO1_VDOLL_TEST_BOOK_PLACE: every n-th, 1 every tick as 47e1fd8)
bool BookPlaceTick(const Body& b, int k) {
    const int every = gBookPlaceEvery.empty() ? kBookPlaceTicks : std::max(1, gBookPlaceEvery[std::min((size_t)std::max(0, k), gBookPlaceEvery.size() - 1)]);
    return every <= 1 || b.bookRecs % every == 1;
}
// (v150-host beta 22, support) its state's part of the records (the support section below): 0 none, else the state; the style it
// sends lying (03, crawling 43), getting up (03 then 02), laying (02); the laying's 08 kind; a book or a claymore laid
int SupRecStyle(const Self& s, const Body& b, uint8_t* style);
uint8_t SupLayKind(const Self& s);
bool SupLayBook(const Self& s);
bool SupBusy(int k);                               // (going to take a claymore or to lay: PickupFrame waits)
void Records(Body& b, const Self& s) {
    const bool lookTick = (b.recs++ & 1) == 0;
    // (v150-host beta 22, bookplay) reading: a reader's game's records (vdoll_book.h): {0E 1} (the long one first), no style,
    // no facing (one {01 0 0 0 0 0 0} at its second tick), no look: its copy reads on every screen, turned to the book
    const bool reading = gBookPose && b.bookId != 0;
    float eye[3], d[3];
    EyeNow(b, s, eye);
    const uint16_t yaw16 = Angle16(b.yaw);
    *s.facing = yaw16;
    b.sentYaw = b.yaw;                                // (beta 21: the facing the players' games have)
    const uint8_t view = (uint8_t)(b.lean > 0 ? 4 : b.lean < 0 ? 2 : 0);   // (beta 15: 4 leaning right, 2 left)
    const bool raising = gScope && Scoped(b.held) && b.aiming && !ScopeOn(b);   // (beta 19: the rifle coming up: {12 v}, 08 1)
    // (beta 19) a shot since the last record, its view with it, the fight over or not; its scope's view kept while it closes
    // (12 frames: the host relays the latest of its records about every 10 frames, and a {08 00} right after its shot took
    // its place: svd_a2_d10, the kill with no shot on the victim's screen)
    const bool closing = gScope && Scoped(b.held) && !b.aiming && gFrame < b.lowerUntil;
    const bool shown = b.aiming || (gScope && b.shotSince) || closing;
    if (!shown) s.send(s.ctx, s.slot, 2, {0x12, 0xFF});
    else if (b.firstAim || raising) { s.send(s.ctx, s.slot, 2, {0x12, view}); b.firstAim = false; }
    else {
        std::vector<uint8_t> r = {0x12, view};
        Put16(r, (uint16_t)Tenth(eye[0])); Put16(r, (uint16_t)Tenth(eye[1])); Put16(r, (uint16_t)Tenth(eye[2]));
        Put16(r, (uint16_t)(int16_t)lrintf(b.pitch / kTurn * 65536.f)); Put16(r, yaw16);
        s.send(s.ctx, s.slot, 2, r);
    }
    const bool grabbing = b.grabSent && b.grabStart >= 0, holding = grabbing && gFrame < b.grabAt + 32;   // (CQC: its hold, its throw)
    if (holding) {
        const uint16_t f16 = Angle16(b.yaw);
        s.send(s.ctx, s.slot, 1, {0x0C, (uint8_t)b.grabbed, (uint8_t)f16, (uint8_t)(f16 >> 8)});
    } else if (!reading || BookPlaceTick(b, s.k)) {
        std::vector<uint8_t> p = {0x00};
        PutF(p, s.pos[0]); PutF(p, s.pos[1]); PutF(p, s.pos[2]);
        s.send(s.ctx, s.slot, 0, p);
    }
    if (grabbing && !holding && !b.grabOverSaid) { s.send(s.ctx, s.slot, 0, {0x01, 0, 0, 0, 0, 0, 0}); b.grabOverSaid = true; }
    // ((v150-host beta 25, fallflinch) a drop's fall and its landing's 9 ticks (the ledge action): a player's game sends his state, his
    // place and his weapon's records each tick, one facing record of zeros, and no style, no other facing, no look (ff_falls1's P2P log:
    // three drops of 1.2 to 3.45 m); b16's doll sent its style, its facing and its look again from its landing on
    const bool dropHold = gHitFall && (b.fallAt >= 0 || (b.landAt >= 0 && gFrame >= b.landAt && gFrame < b.landUntil));
    const bool noStyle = b.fallAt >= 0 || (gFrame < b.climbUntil - 12 && !Vaulting(b)) || grabbing || reading || (dropHold && b.landSaid);   // (beta 24: a vault, a walker's records)   // (beta 15: a drop, a climb, a grab: no 06 nor 01; beta 22: a reading)
    if (reading && b.bookRecs == 1) s.send(s.ctx, s.slot, 0, {0x01, 0, 0, 0, 0, 0, 0});   // (v150-host beta 22, bookplay: a reader's game's, once)
    else if (!noStyle && !b.landSaid && (gFrame < b.landUntil || gFrame < b.climbUntil)) { s.send(s.ctx, s.slot, 0, {0x01, 0, 0, 0, 0, 0, 0}); b.landSaid = true; }
    else if (!noStyle) s.send(s.ctx, s.slot, 0, {0x01, 0x00, 0x00, (uint8_t)yaw16, (uint8_t)(yaw16 >> 8), 0x00, 0x00});
    if (reading) {                                    // (v150-host beta 22, bookplay) {0E 1}: its place and the book's in the first
        uint8_t r[14];
        const int n = vbook::Record(r, b.bookRecs == 0, s.pos, b.bookAt);
        s.send(s.ctx, s.slot, 1, std::vector<uint8_t>(r, r + n));
        ++b.bookRecs;
    }
    // (beta 22, bookplay) no look while it reads, nor in the tick its reading ended (this tick's state record said 2 still): the
    // look's record is the book's place on a copy, which turned to the look's point until the next state record ended its reading
    if (lookTick && !reading && !dropHold && !(gBookPose && b.bookEndF == gFrame)) {   // ((beta 25, fallflinch) no look in a drop's hold)
        if (shown) Dir(b.yaw, b.pitch, d); else Dir(b.yaw, kLookDown, d);
        const float reach = shown ? kAimReach : kLookReach;
        std::vector<uint8_t> r = {0x17};
        for (int k = 0; k < 3; ++k) Put16(r, (uint16_t)Tenth(eye[k] + d[k] * reach));
        s.send(s.ctx, s.slot, 2, r);
    }
    const int rollPart = RollPart(b);                 // ((v150-host beta 25, rolltime) a player's roll: 01 while it rolls, 00 while it gets up)
    uint8_t style = rollPart == 2 ? 0x01 : rollPart == 3 ? 0x00 : b.aiming ? (b.low ? 0x02 : 0x00) : b.speed > kRun * 0.5f ? 0x40 : b.speed > 1.f ? 0x20 : 0x00;
    const int sup = SupRecStyle(s, b, &style);         // (v150-host beta 22, support: lying, crawling, getting up, laying)
    if (!noStyle && !dropHold) s.send(s.ctx, s.slot, 1, {0x06, style});   // ((beta 25, fallflinch) none in a drop's hold, its {01 0}'s tick too)
    // its weapon's state (08): the draw 1, reloading 8 then 6 (flags 0x28), aimed 3, a shot since the last record 5
    uint8_t kind = 0, flags = 0;
    if (sup == 6) kind = SupLayKind(s);                // (v150-host beta 22, support: Square held {08 12}, the motion {08 13}, zeros after)
    else if (grabbing) kind = 0;
    else if (b.grabStart >= 0 && !b.drawn && gFrame >= b.switchUntil) kind = 0x0A;   // (Circle held: the grab)
    else if (b.throwAt >= 0) kind = ThrowKind(b);
    else if (IsRocket(b.held) && b.rpgPullAt >= 0) kind = 3;   // (v150-host beta 22, heavy: the RPG-7's pull, its rocket 14 frames on)
    else if (IsRocket(b.held) && (b.shotSince || gFrame < b.rpgShowUntil)) kind = 5;   // (its rocket shown four records, as a player's)
    else if (gScope && b.shotSince) kind = 5;         // (beta 19: a magazine's last shot shown too, its reload or draw the next record)
    else if (gFrame < b.reloadUntil && IsRocket(b.held)) { kind = 6; flags = 0x28; }   // (v150-host beta 22, heavy: the RPG-7's reload {08 06 28} from the pull on, a player's)
    else if (gFrame < b.reloadUntil) { kind = gFrame - b.reloadAt < 10 ? 8 : 6; flags = kind == 6 ? 0x28 : 0; }
    else if (b.drawn) { kind = 1; b.drawn = false; }
    else if (raising) kind = 1;
    else if (b.aiming || closing) kind = b.shotSince ? 5 : IsRocket(b.held) ? 1 : 3;   // (v150-host beta 22: the RPG-7 held in its sight 1, as a player's; 3 is its pull)
    if (kind == 0x12 || kind == 0x13) {
        s.send(s.ctx, s.slot, 1, {0x08, kind, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        b.shotSince = false;
    } else if (kind) {
        Dir(b.yaw, shown ? b.pitch : kLookDown, d);
        std::vector<uint8_t> r = {0x08, kind, flags};
        for (int k = 0; k < 3; ++k) Put16(r, (uint16_t)Tenth(eye[k] + d[k] * kAimReach));
        Put16(r, (uint16_t)(kind == 5 ? b.dev[0] : 0)); Put16(r, (uint16_t)(kind == 5 ? b.dev[1] : 0));
        s.send(s.ctx, s.slot, 1, r);
        b.shotSince = false;
    } else if (b.sent08) s.send(s.ctx, s.slot, 1, {0x08, 0x00, 0x00});
    b.sent08 = kind != 0;
    // (beta 19) its hits one record after the record that shows their shot: the host relays a datagram's events before its
    // players' states, so a hit sent with its shot's record reached the victim first (svd_a4_d10: the blood, no shot seen)
    for (size_t i = 0; i < b.pending.size();)
        if (b.pending[i].shown) {
            if (!b.pending[i].gate || CoverSend(b, s, b.pending[i].gate)) s.send(s.ctx, b.pending[i].to, b.pending[i].chan, b.pending[i].r);   // (beta 21, cover)
            b.pending.erase(b.pending.begin() + i);
        }
        else { b.pending[i].shown = true; ++i; }   // (the record of this tick shows its shot: kind 5, Records' rule above)
    if (ScopeOn(b) && b.aiming) ++b.scopeRecs;        // (beta 19: two records of its scope out before its first shot)
    b.aimedLast = b.aiming;
    // (v150-host beta 18) {13, the item equipped, the weapon, flags}: a TEAM SNEAKING sneaker holds the STEALTH CAMOUFLAGE
    // (item 0x17) as every real sneaker does from his spawn (tsne.h TsneStealthItem; a playable doll's records: {13 17 05
    // 02}); the item made its copy camouflaged on every machine (its flag 0x96: tsneA_before, never on a virtual doll's)
    const uint8_t item = b.role == R_SNEAKER ? 0x17 : b.gogglesOn ? 0x07 : 0x00;   // (v150-host beta 22, pickup: the thermal goggles worn, item 7)
    if (sup == 6) s.send(s.ctx, s.slot, 2, {0x13, item, (uint8_t)(SupLayBook(s) ? vsup::kBookCode : vsup::kClayCode), 0x00});   // (v150-host beta 22, support: the trap in its hands)
    else if (b.carrying) s.send(s.ctx, s.slot, 2, {0x13, item, 0x00, 0x00});
    else if (b.throwAt >= 0) s.send(s.ctx, s.slot, 2, {0x13, item, (uint8_t)b.nade, 0x00});
    else if (b.unarmed || (b.grabStart >= 0 && !(Allowed() & 2)))      // (beta 16: the knife, or its grenade: a player's record so)
        s.send(s.ctx, s.slot, 2, {0x13, item, (uint8_t)(b.unarmed && b.grenades > 0 && !b.melee && b.grabStart < 0 ? b.nade : 0x01), 0x00});
    else if (gScope && Scoped(b.held) && !RifleUp(b) && gFrame >= b.lowerUntil && gFrame >= b.reloadUntil) {
        // (beta 19) a scoped rifle is in its hands only raised where it stands: moving, its handgun (none if the setting
        // allows no secondary), as a player runs
        const int o = (Allowed() & 2) ? b.secondary : -1;
        s.send(s.ctx, s.slot, 2, {0x13, item, (uint8_t)(o >= 0 ? kWeapons[o].code : 0x00), (uint8_t)(o >= 0 && b.mag[o] > 0 ? 2 : 0)});
    } else {
        const bool loaded = b.mag[b.held] > 0 || (IsRocket(b.held) && b.reloadAt >= 0 && gFrame - b.reloadAt >= kRpgLoaded);   // (v150-host beta 22: the rocket in its tube 90 frames into the reload)
        s.send(s.ctx, s.slot, 2, {0x13, item, (uint8_t)kWeapons[b.held].code, (uint8_t)(loaded ? 2 : 0)});
    }
}

// (v150-host beta 18) its weapons for its role: a soldier's as before (its primary of this life, the M1911A1); TEAM
// SNEAKING's sneaker the Mk22 in its hands, the MOSIN NAGANT for a far enemy, stun grenades, the knife (tsne.h
// kTsneNonLethal: what the game lets a sneaker choose, every class); SNEAKING's Snake his XM16E1, his Mk22 close by and his
// stun grenades (his inventory, docs/PLAYABLE_DOLL.md). Within the game's weapons setting (Allowed: 1 the primary row, 2
// the secondary, 4 the support)
void SetRole(Body& b, const Self& s, int role) {
    const unsigned allowed = Allowed();
    const int was = b.role;
    b.role = role;
    b.longGun = -1;
    if (role == R_SNEAKER) {
        b.secondary = W_MK22; b.nade = kStunNade;
        b.primary = (allowed & 2) ? W_MK22 : W_MOSIN;
        if ((allowed & 1) && (allowed & 2)) b.longGun = W_MOSIN;
    } else if (role == R_SNAKE) {
        b.secondary = W_MK22; b.nade = kStunNade;
        b.primary = (allowed & 1) ? W_XM16E1 : W_MK22;
    } else {
        b.secondary = W_M1911; b.nade = kFragNade;
        b.primary = b.special >= 0 && b.specialRounds > 0 ? b.special : b.soldierPrimary;   // (beta 22, pickup: the M63 taken)
    }
    if (b.held != b.primary && b.reloadAt < 0) { b.held = b.primary; b.drawn = !b.unarmed; b.switchUntil = gFrame + 18; }
    if (b.reloadAt >= 0 && b.held != b.primary && b.held != b.secondary && b.held != b.longGun) { b.reloadAt = -1; b.reloadUntil = 0; b.held = b.primary; }
    if (role == was) return;
    if (role == R_SNEAKER)
        BLOG("vdoll %d: sneaking (team sneaking: the %s team sneaks): %s, no lethal weapon", s.k + 1, s.team ? "blue" : "red",
             b.unarmed ? "its knife and stun grenades" : b.longGun >= 0 ? "its Mk22, its MOSIN NAGANT for far shots, stun grenades and the knife" :
             b.primary == W_MK22 ? "its Mk22, stun grenades and the knife" : "its MOSIN NAGANT, stun grenades and the knife");
    else if (role == R_SNAKE)
        BLOG("vdoll %d: Snake (sneaking): %s", s.k + 1, b.unarmed ? "his knife and stun grenades" : (allowed & 1) && (allowed & 2) ? "his XM16E1, his Mk22 within 10 m, his stun grenades" :
             (allowed & 1) ? "his XM16E1 and stun grenades" : "his Mk22 and stun grenades");
    else BLOG("vdoll %d: a soldier again (%s)", s.k + 1, kWeapons[b.primary].name);
}
// (v150-host beta 18) the gun for a fight at this distance: a sneaker's MOSIN NAGANT beyond 20 m, its Mk22 within 15 m;
// Snake's Mk22 within 10 m, his XM16E1 beyond 14 m (the playable dolls' Snake, doll_brain.cpp); a soldier's as before
int GunFor(const Body& b, float d) {
    if (gDartTest && (Allowed() & 2)) return W_MK22;   // (the test switch: any role, the Mk22)
    if (b.role == R_SNEAKER) {
        if (b.longGun >= 0 && d > 20000.f) return b.longGun;
        if (b.longGun >= 0 && d < 15000.f) return W_MK22;
        return b.held == W_MK22 || b.held == W_MOSIN ? b.held : b.primary;
    }
    if (b.role == R_SNAKE && b.primary == W_XM16E1 && (Allowed() & 2)) {
        if (d < 10000.f) return W_MK22;
        if (d > 14000.f) return W_XM16E1;
        return b.held == W_MK22 || b.held == W_XM16E1 ? b.held : b.primary;
    }
    return b.held;
}
// (v150-host beta 22, book) a book of another player (vdoll_book.h, docs/VIRTUAL_DOLL.md 30): a player's own game makes him read
// the first one of its list within 1.5 m of his hips, in a clear line from his eye, when his cooldown is over: still, turned to
// it, no aim and no shot, for as long as the book lies (the read time his game starts on it; at its end the game takes the
// book away and tells the others), then 7.5 s before another. The doll the same, with the host's list, the read time started in
// the host's game, its own copy's class and state as the check reads them. true: it reads this frame (its moves and fight done)
// (v150-host beta 25, bookhold: MGO1_VDOLL_BOOK_HOLD) its drop, climb or fence holds a book back as a player's own game holds it
// (vdoll_book.h; measured on real clients, probe 52): the game's ledge action (US n003a id 0x21F3700, priority 0x16, over the book's
// 0x13: the scheduler checks no lower action while it runs) runs from a drop's edge to 9 ticks after the landing and through 24
// ticks of a climb from 2 ticks after the press (the book read 27 ticks after it); a fence crossed runs no action and sets no refused
// state: the book is read on the fence. Beta 22: the fall, its landing's 18 frames, the whole climb's or crossing's hold (60 frames,
// the crossing's length)
bool BookHeld(const Body& b) {
    if (b.fallAt >= 0 || gFrame < b.landUntil) return true;          // (the fall and the landing's 18 frames: the player's, both rules)
    if (gFrame >= b.climbUntil) return false;
    if (!gBookHold) return true;
    if (b.vaultUntil == b.climbUntil) return false;                   // (a fence: no hold)
    const long long start = b.climbUntil - 60;                        // (a climb: its 60 frames from its start, a player's press)
    return (double)(gFrame - start) * Slowed() < (double)vbook::kClimbHoldFrames;   // (a player's ticks by the clock on a slowed host)
}
// (v150-host beta 25, bookhold) a reading begun on a fence over: the crossing goes on from where it stopped, as the player's does
void VaultResume(Body& b) {
    if (b.bookVault < 0.f) return;
    const long long len = std::max(1LL, b.vaultUntil - b.vaultAt);
    b.vaultAt = gFrame - (long long)std::lrint(b.bookVault * (double)len); b.vaultUntil = b.vaultAt + len; b.climbUntil = b.vaultUntil;
    b.bookVault = -1.f;
}
bool BookFrame(Body& b, const Self& s, int dt) {
    if (!gBooks || !gHost.books || !gHost.readBook) return false;
    const unsigned long long now = RealMs();
    if (!b.bookId) VaultResume(b);                                    // (v150-host beta 25, bookhold: its reading ended elsewhere)
    if (b.bookId) {
        if (!vbook::StillReading(gBookList, gBookN, b.bookId, b.bookSince, b.bookUntil, now)) {
            // its time over by the clock (the book still in the list): the host's read time ended now, the game takes it away
            bool lies = false;
            for (int i = 0; i < gBookN; ++i) if (gBookList[i].id == b.bookId) lies = true;
            const bool ended = lies && gHost.bookDone && gHost.bookDone(b.bookId);
            BLOG("vdoll %d: done reading the book (%.1f s; the book %s)", s.k + 1, (now - b.bookSince) / 1000.0,
                 !lies ? "gone" : ended ? "taken away at its read time's end" : "still lying");
            b.bookId = 0; b.bookReadyMs = now + (unsigned long long)vbook::kCooldownMs; b.planAt = 0; b.bookEndF = gFrame;
            VaultResume(b);                                // (v150-host beta 25, bookhold)
            return false;
        }
        b.speed = 0; b.target = -1; b.aiming = false; b.pitch = 0; b.lean = 0;
        TurnBody(b, std::atan2(b.bookAt[0] - s.pos[0], b.bookAt[2] - s.pos[2]), dt);
        return true;
    }
    if (gBookN <= 0 || BookHeld(b) || b.grabStart >= 0 || b.throwAt >= 0 || gFrame < b.rollUntil || gFrame < b.rushUntil)   // ((v150-host beta 25, bookhold) BookHeld)
        return false;
    if (b.rpgPullAt >= 0 || gFrame < b.rpgShowUntil) return false;   // (heavy, at the merge: an RPG-7's pull, its rocket and its shot shown, to their end first)
    vbook::Reader r;
    r.slot = s.slot; r.cls = gHave[s.slot] ? gSeen[s.slot].cls : -1; r.alive = !gHave[s.slot] || gSeen[s.slot].alive;
    r.refused = gHave[s.slot] && gSeen[s.slot].bookRefused; memcpy(r.hips, s.pos, sizeof r.hips); r.readyMs = b.bookReadyMs;
    // (v150-host beta 22, review) the thermal goggles on: the check refuses a player whose item in use is the goggles (+0x614's 7)
    // with their effect on (the state bit 0x150, set with the goggles' frame: US n003a 0x443D28 to 0x443D50, 0x450F80 to 0x450F90)
    if (b.gogglesOn) r.refused = true;
    float eye[3];
    EyeNow(b, s, eye);
    // (the game's book test asks its collision line only: no smoke's cloud in it, as SightClear has since blast; a white
    // screen neither)
    auto clear = [&](int j) {
        float t[3]; vbook::Target(gBookList[j], t);
        gThingAt = -1.f;
        return LineClear(eye, t) && !ThingStops(eye, t, kSightPower);
    };
    const int i = vbook::Pick(r, gBookList, gBookN, now, clear);
    if (i < 0 && r.cls == vbook::kRaikov && r.alive && !r.refused && now >= r.readyMs) {   // (v150-host beta 22, bookplay2) said once a book
        vbook::Reader o = r; o.cls = 0;
        const int j = vbook::Pick(o, gBookList, gBookN, now, clear);
        if (j >= 0 && gBookList[j].id != b.bookClassSaid) {
            b.bookClassSaid = gBookList[j].id;
            BLOG("vdoll %d: a book of player %d within reach, not read: its player is Raikov (class 4), whom a player's own game never makes read", s.k + 1, gBookList[j].slot + 1);
        }
    }
    if (i < 0 || !gHost.readBook(gBookList[i].id)) return false;
    const vbook::Book& k = gBookList[i];
    b.bookId = k.id; memcpy(b.bookAt, k.pos, sizeof b.bookAt); b.bookSince = now; b.bookUntil = now + vbook::ReadMs(k.readTime); ++b.bookReads;
    b.bookRecs = 0;                                    // (v150-host beta 22, bookplay: its long record at its next tick)
    if (b.target >= 0 && b.fightShots) BLOG("vdoll %d: its fight with player %d over (%d shots, %d hits)", s.k + 1, b.target + 1, b.fightShots, b.fightHits);
    // ((v150-host beta 25, bookhold) on a fence: still where it is, its crossing and its way kept for the reading's end)
    if (gBookHold && Vaulting(b)) b.bookVault = std::min(1.f, (float)(gFrame - b.vaultAt) / std::max(1.f, (float)(b.vaultUntil - b.vaultAt)));
    b.target = -1; b.aiming = false; b.aimedLast = false; b.pitch = 0; b.lean = 0; b.speed = 0;
    if (b.bookVault < 0.f) { b.path.clear(); b.next = 0; }
    LowerRifle(b);
    if (gPlayerCtl) CtlLower(b);                       // (MGO1_VDOLL_CONTROLS=player: its weapon lowered too)
    // ((v150-host beta 25, bookhold) a fence, a climb or a landing of the last 2 s said, with its frames: the holds' measure in a log)
    char moved[96] = "";
    if (b.bookVault >= 0.f) snprintf(moved, sizeof moved, "; on a fence, %lld frames into its crossing", gFrame - b.vaultAt);
    else if (b.climbUntil >= 0 && b.vaultUntil != b.climbUntil && gFrame - (b.climbUntil - 60) < 120) snprintf(moved, sizeof moved, "; %lld frames after its climb began", gFrame - (b.climbUntil - 60));
    else if (b.vaultUntil >= 0 && b.vaultUntil == b.climbUntil && gFrame - b.vaultUntil < 120) snprintf(moved, sizeof moved, "; %lld frames after its fence crossed", gFrame - b.vaultUntil);
    else if (b.landAt >= 0 && gFrame - b.landAt < 120) snprintf(moved, sizeof moved, "; %lld frames after its landing", gFrame - b.landAt);   // ((beta 25, fallflinch) its landing's frame: the hold by the clock)
    // ((v150-host beta 22, bookplay) the game's read time said: the stage script's -t 8, 480 passes, on the 12 maps of the four discs)
    BLOG("vdoll %d: reads a book of player %d at %.0f %.0f %.0f (%.2f m from its hips), as a player's own game makes him (%d this game; the game's read time %d passes, %.1f s%s)", s.k + 1, k.slot + 1,
         k.pos[0], k.pos[1], k.pos[2], std::sqrt((s.pos[0] - k.pos[0]) * (s.pos[0] - k.pos[0]) + (s.pos[1] - k.pos[1]) * (s.pos[1] - k.pos[1]) + (s.pos[2] - k.pos[2]) * (s.pos[2] - k.pos[2])) / 1000.f,
         b.bookReads, k.readTime, vbook::ReadMs(k.readTime) / 1000.0, moved);
    return true;
}
// ---- (v150-host beta 22, wake: MGO1_VDOLL_WAKE, docs/VIRTUAL_DOLL.md 36) a teammate held by a book or knocked out, woken with the
// game's own blows. The owner (13 November): "if a bot sees a teammate of its own team, a bot or a player, caught in a book's trap,
// it can hit him or jump on him to wake him; the same if an allied teammate is stunned: it can hit him (3 hits, the third a kick)".
// The game's rule (vdoll_wake.h, US n003a): the punch combo's blows (0x97, 0x96, the kick 0x95) on a teammate lying knocked out
// give a NEGATIVE stun, his gauge's maximum x +0xB74 / 100: his own game fills his gauge at once and he gets up; on a standing
// teammate a blow is a flinch (the kick a knock down) and half its stamina. A doll does what a player does: with no enemy in its
// sight, a teammate it sees knocked out or reading, a way to him, and more time left before he is up or free by himself than its
// way takes, it runs to him and presses Circle as a player does (the combo's frames), pressing again only while its blows have not
// done it; it stops when he is up or reads no more. The records are a player's: {08 ..} its punch, {14 ..} the blow's effect on his
// channel 2, {04 ..} the stun on his channel 0, {09 ..} his reaction on his channel 1 when he stands. On a real player the effect is
// his own game's
bool gWake = true;                                 // MGO1_VDOLL_WAKE=0: no teammate woken (beta 22's)
bool gWakeTest = false;                            // MGO1_VDOLL_TEST_WAKE=1 (benches): a teammate anywhere on the map (no sight, no way's bound)
bool gWakeFollow = false;                          // ((v150-host beta 23) MGO1_VDOLL_TEST_WAKE=2, benches: and with nobody down it keeps within 4 m of a player of its team)
bool gWakeThrow = true;                            // MGO1_VDOLL_WAKE_THROW=0: never the CQC throw to free a reader (the part switch)
// (measured, wake_m4) Circle pressed by a player standing close to a teammate who reads is the CQC throw with friendly fire ON too
// (kind 0xCF, 1500, his fall: his reading ended), as it is with it off (wake_m1): no punch reaches a reader from where a player frees him
enum WakeMode { WM_NONE = 0, WM_COMBO = 1, WM_THROW = 2 };
int FriendlyFire() { return gHost.friendlyFire ? gHost.friendlyFire() : -1; }   // (1 on, 0 off, -1 not known: as off)
// how the game lets it help a teammate down for that reason (1 knocked out, 2 reading): knocked out, the combo run into him (its
// kick: the ally wake), friendly fire on or off (measured: wake_ts_on2, wake_ts_off); reading, the CQC throw whatever the setting
// (pressed running with friendly fire off: wake_m1; standing still it reached no teammate then, wake_m2), its part switch on
int ModeFor(int why) {
    if (why == 1) return WM_COMBO;
    if (why == 2) return gWakeThrow ? WM_THROW : WM_NONE;
    return WM_NONE;
}
// an enemy alive with a clear line to that player within 30 m (the throw's use: a reader in an enemy's sight)
bool EnemySees(const Self& s, int id) {
    float c[3];
    Chest(gSeen[id], c);
    for (int e = 0; e < 16; ++e) {
        if (!gHave[e] || !gSeen[e].object || !gSeen[e].alive || !Enemy(gSeen[e], s.team)) continue;
        float eye[3] = {gSeen[e].pos[0], gSeen[e].pos[1] + kEye, gSeen[e].pos[2]};
        if (Dist3(eye, c) < 30000.f && SightClear(eye, c)) return true;
    }
    return false;
}
struct DollOut { bool out = false; int stamina = 0, perTick = 0; };
DollOut gDollOut[16];                              // (a virtual doll knocked out, by its seat: vdoll.cpp's Knocked each frame)
long long gOutFrom[16], gReadFrom[16];             // (a player knocked out, reading, on the host's copy since: frames, -1 not)
unsigned long long gReadFromMs[16];
const float kWakeReach = 900.f;                    // (its hips to his on the ground: a punch's reach, docs/PLAYABLE_DOLL.md "Circle close by (under 0.9 m) punches")
const float kWakeSees = 30000.f;                   // (a teammate seen down within 30 m, in a clear line)
const float kWakeWay = 30000.f;                    // (a way of 30 m at most)
const long long kOutAlone = 2520;                  // (a player knocked out and untouched is up by himself after 42 s: the game's rate)
const unsigned long long kWakeReadMs = 8008;       // (a player's reading: the read time's 480 passes at 60 a second, the benches' book list)
const long long kWakeLook = 30, kWakeGiveUp = 1200, kWakeAfter = 120;   // (it looks every 0.5 s; 20 s at most; 2 s for a blow's effect)
const int kWakeCombos = 3;                         // (combos given at most before it gives up)
void WakeWatch(long long frame) {                  // (each 100 ms: since when each player is down or reading on the host's copy)
    static bool init = false;
    if (!init) { init = true; for (int i = 0; i < 16; ++i) gOutFrom[i] = gReadFrom[i] = -1; }
    for (int id = 0; id < 16; ++id) {
        const bool out = gHave[id] && gSeen[id].object && gSeen[id].alive && gSeen[id].asleep;
        if (!out) gOutFrom[id] = -1; else if (gOutFrom[id] < 0) gOutFrom[id] = frame;
#ifdef VDOLL_PLAYERNOW_WAKE
        const bool rd = gHave[id] && gSeen[id].object && gSeen[id].alive && gSeen[id].reading;
#else
        const bool rd = false;
#endif
        if (!rd) gReadFrom[id] = -1; else if (gReadFrom[id] < 0) { gReadFrom[id] = frame; gReadFromMs[id] = RealMs(); }
    }
}
int BodyAt(int slot) {                             // (the virtual doll in that seat, alive: its body; -1 none)
    for (size_t k = 0; k < gBodies.size(); ++k) if (gBodies[k].life >= 0 && gBodies[k].slot == slot) return (int)k;
    return -1;
}
// player id down as a teammate's blows can help him: 1 knocked out and lying, 2 reading a book; *left the frames before he is up or
// free by himself (the longest a player's own game takes untouched)
int DownWhy(int id, long long* left) {
    *left = 0;
    if (id < 0 || id >= 16 || !gHave[id] || !gSeen[id].object || !gSeen[id].alive) return 0;
    const int k = BodyAt(id);
    if (gDollOut[id].out) {
        const DollOut& o = gDollOut[id];
        *left = (long long)std::max(0, 30000 - o.stamina) / std::max(1, o.perTick) * 6;
        return 1;
    }
    if (k >= 0) {                                  // (a virtual doll: its own state, its copy is not asked)
        const Body& v = gBodies[k];
        if (!v.bookId) return 0;
        const unsigned long long now = RealMs();
        *left = v.bookUntil > now ? (long long)((double)(v.bookUntil - now) / gMsAFrame) : 0;
        return 2;
    }
    if (gSeen[id].asleep && StanceOf(gSeen[id]) == S_LIE && gOutFrom[id] >= 0) {
        // (TEAM SNEAKING, the owner's rule: a defender's own recovery a quarter of the game's, tsne.h TsneWakeTick: about 167 s
        // untouched; the doll defenders go to him whenever the game lets their blows reach him)
        const int ss = SneakSide();
        const long long alone = gRule == 5 && ss >= 0 && (gSeen[id].team == 0 || gSeen[id].team == 1) && gSeen[id].team != ss ? kOutAlone * 4 : kOutAlone;
        *left = std::max(0LL, alone - (gFrame - gOutFrom[id]));
        return 1;
    }
#ifdef VDOLL_PLAYERNOW_WAKE
    if (gSeen[id].reading && !gSeen[id].asleep && gReadFrom[id] >= 0) {
        const unsigned long long el = RealMs() - gReadFromMs[id];
        *left = el < kWakeReadMs ? (long long)((double)(kWakeReadMs - el) / gMsAFrame) : 0;
        return 2;
    }
#endif
    return 0;
}
// his copy as the attacker's game asks it (vdoll_wake.h): on the ground, knocked out, a teammate; his gauge's maximum, the wake's part
vwake::Victim VictimOf(const Self& s, int id) {
    vwake::Victim v;
    const PlayerNow& o = gSeen[id];
    v.lying = StanceOf(o) == S_LIE;
    v.out = o.asleep || gDollOut[id].out;
    if (gDollOut[id].out) v.lying = true;          // (a virtual doll knocked out lies: its {0A id 4} told, Knocked's lyingOut)
    v.teamRule = gRule >= 1 && gRule <= 6;
    v.teammate = (o.team == 0 || o.team == 1) && o.team == s.team;
#ifdef VDOLL_PLAYERNOW_WAKE
    if (o.stunMax > 0) v.stunMax = o.stunMax;
    if (o.wakePart > 0) v.wakePart = o.wakePart;
#endif
    return v;
}
bool WallBetween(const float* feet, int v);        // ((v150-host beta 25, wakeway) below, with its goal on his side)
// its blow i of the combo on player v: the records a player's game sends for it; true when it lands (within reach)
bool WakeBlow(Body& b, const Self& s, int v, int i) {
    const PlayerNow& t = gSeen[v];
    const float d = Dist2D(s.pos, t.pos);
    const vwake::Victim vic = VictimOf(s, v);
    const float hisFloor = t.pos[1] - (vic.lying ? 300.f : kHips);
    if (d > kWakeReach + 100.f || std::fabs(hisFloor - b.floorY) > 400.f) {
        BLOG("vdoll %d: its %s misses player %d (%.2f m)", s.k + 1, i == 2 ? "kick" : "punch", v + 1, d / 1000.f);
        return false;
    }
    float myFeet[3];
    Feet(s.pos, myFeet);
    if (WallBetween(myFeet, v)) {                  // ((v150-host beta 25, wakeway) a limb stopped by the wall between)
        BLOG("vdoll %d: its %s misses player %d (%.2f m, a wall between)", s.k + 1, i == 2 ? "kick" : "punch", v + 1, d / 1000.f);
        return false;
    }
    const int stun = vwake::Stun(i, vic);
    const bool front = true;                       // (his facing not read: a reader faces his book, the doll comes at him)
    int react = vwake::Reaction(i, vic, front);
    // ((v150-host beta 25, reactfix) a punch carries no reaction (measured: a real client's punch on a standing player, no {09}); the kick 9
    // at the doll's own yaw less 0x8000; none on the ground (vdoll_react.h Blow). b16: a punch on a standing teammate a flinch, 1)
    vreact::Out blowOut;
    if (gReactKind) { blowOut = vreact::Blow(ReactVictim(v), i, Angle16(b.yaw)); react = vic.lying ? 0 : blowOut.kind; }
    float c[3];
    Chest(t, c);
    const float ux = (t.pos[0] - s.pos[0]) / std::max(1.f, d), uz = (t.pos[2] - s.pos[2]) / std::max(1.f, d);
    // (a punch's effect on the chest; the kick on a teammate lying: the effect 0x0022, part 4, pushed down: wake_ts_on2's record)
    std::vector<uint8_t> fx = vic.lying && i == 2 ? std::vector<uint8_t>{0x14, (uint8_t)s.slot, 0x22, 0x00, 0x04} : std::vector<uint8_t>{0x14, (uint8_t)s.slot, 0x12, 0x01, 0x02};
    if (vic.lying) { c[0] = t.pos[0]; c[1] = t.pos[1]; c[2] = t.pos[2]; }
    Put16(fx, (uint16_t)Tenth(c[0])); Put16(fx, (uint16_t)Tenth(c[1])); Put16(fx, (uint16_t)Tenth(c[2]));
    Put16(fx, (uint16_t)(int16_t)lrintf(ux * (vic.lying ? 16.f : 30.f))); Put16(fx, (uint16_t)(int16_t)(vic.lying ? -46 : 0)); Put16(fx, (uint16_t)(int16_t)lrintf(uz * (vic.lying ? 16.f : 30.f)));
    s.send(s.ctx, v, 2, fx);
    s.send(s.ctx, v, 0, {0x04, (uint8_t)s.slot, 0x00, 0x00, (uint8_t)(stun & 0xFF), (uint8_t)((stun >> 8) & 0xFF), (uint8_t)vwake::kKind[i], 0x00, 0x00});
    if (react) {
        const uint16_t a = gReactKind ? blowOut.angle : Angle16(Bearing(t.pos, s.pos));
        s.send(s.ctx, v, 1, {0x09, (uint8_t)s.slot, (uint8_t)react, (uint8_t)a, (uint8_t)(a >> 8)});
    }
    ++b.wakeBlows; ++b.wakeLanded;
    if (vwake::Wakes(vic)) {                       // (his gauge filled by its blows: no more presses once it is full)
        if (b.wakeGauge < 0) b.wakeGauge = 0;
        b.wakeGauge = std::min(vic.stunMax, b.wakeGauge - stun);
        if (b.wakeGauge >= vic.stunMax) b.wakeDone = true;
    }
    if (b.wakeWhy == 2 && react) b.wakeDone = true;                       // (his reaction ends his reading)
    char g[64] = "";
    if (stun < 0) snprintf(g, sizeof g, ", the game's ally wake: his gauge %d of %d", b.wakeGauge, vic.stunMax);
    BLOG("vdoll %d: %s player %d (%s): the stun %d%s", s.k + 1, i == 2 ? "kicks" : "punches", v + 1, b.wakeWhy == 1 ? "knocked out" : "reading", stun,
         stun < 0 ? g : react == 9 ? ", knocked down" : react ? ", a flinch" : "");
    return true;
}
// where it stands to punch him and its way there: his place (his feet) when no book lies near, else the nearest place on the mesh
// within a punch's reach of him out of every other player's book's reach whose way stays out of it too, as a player comes at a
// reader from the side away from his book (one coming within 1.5 m of a book, in 3D from his hips, reads it too: the game's own
// check; a margin of 0.1 m). With path: its way set; else *length its way's length. False: none
bool BookFree(const Self& s, const float* feet) {          // (a standing player's hips there out of every other player's book's reach)
    const float hips[3] = {feet[0], feet[1] + kHips, feet[2]};
    for (int i = 0; i < gBookN; ++i) if (gBookList[i].slot != s.slot && Dist3(hips, gBookList[i].pos) < 1600.f) return false;
    return true;
}
bool WayBookFree(const Self& s, const std::vector<doll::Waypoint>& w) {   // (no point of the way within a book's reach: every 0.25 m)
    for (size_t i = 1; i < w.size(); ++i) {
        const float* a = w[i - 1].p; const float* c = w[i].p;
        const float l = Dist3(a, c);
        const int n = std::max(1, (int)(l / 250.f));
        for (int k = 0; k <= n; ++k) {
            const float t = (float)k / (float)n, q[3] = {a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, a[2] + (c[2] - a[2]) * t};
            if (!BookFree(s, q)) return false;
        }
    }
    return true;
}
// ---- (v150-host beta 25, wakeway: MGO1_VDOLL_WAKE_SIDE, docs/VIRTUAL_DOLL.md 46) a teammate is woken from his side of the wall he
// lies against, as a player's blows reach him. The game's blow (US n003a 0x408A10) is a sphere on the attacker's limb, a punch 400 mm
// on a hand, the kick 600 mm on a foot, the CQC throw 600 mm on a hand, that lands where it meets the victim's body (the engine's
// overlap of the two, 0x14C878 and 0x14C438: no line between the two players asked; the hit's one line test, 0x4392E0, is for the
// attacks with 0x200 in their word, not these); a player's body is kept off every wall, so his limbs pass a wall only by their
// length past its edge, and the playable doll's measured kicks were all given from his side. Beta 23 to 25's doll had no wall at
// all: its way's goal was the mesh's nearest point to his feet (review25 R3: on n003a 302 mm beyond the wall he lay against, a way
// of 31 to 53 m round to it: it never went), its straight run at him the mesh's line with 0.9 m of slack, its blows its distance
// alone. Now its goal is a place on his side (a clear line to his body from a player standing there), its run at him and its
// press ask that line, and a blow without it misses as a limb stopped by the wall. MGO1_VDOLL_WAKE_SIDE=0: as beta 25
bool gWakeSide = true;
long long gWakeWalls = 0, gWakeRound = 0;          // (blows that met a wall, goals taken round to his side: Status and the minute's line)
// the line a blow needs from a player standing with his feet at `at` (his hips, his knees) to the body of t (lying: his hips and
// 0.15 m over them; standing: his hips and chest): one pair clear through the stage's collision and, with things, every thing of
// the game (glass too: no limb goes through a pane)
bool StrikeClear(const float* at, const PlayerNow& t, bool lying, bool things) {
    const float a[2][3] = {{at[0], at[1] + kHips, at[2]}, {at[0], at[1] + 400.f, at[2]}};
    float c[3];
    Chest(t, c);
    const float v[2][3] = {{t.pos[0], t.pos[1], t.pos[2]}, {t.pos[0], lying ? t.pos[1] + 150.f : c[1], t.pos[2]}};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            if (!LineClear(a[i], v[j])) continue;
            if (things && ThingStops(a[i], v[j], kStopsAll)) continue;
            return true;
        }
    return false;
}
bool Lying(int id) { return StanceOf(gSeen[id]) == S_LIE || gDollOut[id].out; }
// a blow on player v from a player standing with his feet there meets a wall or a thing of the game first (counted); never with
// MGO1_VDOLL_WAKE_SIDE=0
bool WallBetween(const float* feet, int v) {
    if (!gWakeSide || v < 0 || v >= 16 || StrikeClear(feet, gSeen[v], Lying(v), true)) return false;
    ++gWakeWalls;
    return true;
}
void SideSpots(const Self& s, const PlayerNow& t, bool lying, bool books, std::vector<std::array<float, 5>>& spots);
// his side's anchor: the place on his side in his reach nearest him (none: false)
bool SideAnchor(const Self& s, int v, float* at) {
    std::vector<std::array<float, 5>> spots;
    SideSpots(s, gSeen[v], Lying(v), false, spots);
    if (spots.empty()) return false;
    const float* p = gSeen[v].pos;
    size_t best = 0;
    for (size_t i = 1; i < spots.size(); ++i) if (std::hypot(spots[i][0] - p[0], spots[i][2] - p[2]) < std::hypot(spots[best][0] - p[0], spots[best][2] - p[2])) best = i;
    memcpy(at, spots[best].data(), 3 * sizeof(float));
    return true;
}
// the places on his side in his reach: beta 25's goal (the mesh's nearest point to his feet) first when it is on his side, then
// the mesh's points round him (0.45, 0.65, 0.85 m; 0.65 and 0.85 m when a book lies near: a reader is come at from the side away
// from his book), each within a punch's reach of his feet, on his floor, out of every other player's book's reach, a clear line
// from it to him (the stage's collision; the game's things asked of the goal taken); x y z, its distance to the doll, 1 for
// beta 25's own
void SideSpots(const Self& s, const PlayerNow& t, bool lying, bool books, std::vector<std::array<float, 5>>& spots) {
    const float hisFloor = t.pos[1] - (lying ? 300.f : kHips);
    const float feet[3] = {t.pos[0], hisFloor, t.pos[2]};
    float q[3];
    if (!books && gMap->nav->Nearest(feet, 1500.f, q) && Dist2D(q, feet) <= kWakeReach && std::fabs(q[1] - hisFloor) <= 400.f && StrikeClear(q, t, lying, false))
        spots.push_back({q[0], q[1], q[2], Dist2D(q, s.pos), 1.f});
    for (int r = books ? 1 : 0; r < 3; ++r)
        for (int k = 0; k < 16; ++k) {
            const float a = (float)k * kPi / 8.f, rr = r == 0 ? 450.f : r == 1 ? 650.f : 850.f;
            const float c[3] = {feet[0] + std::sin(a) * rr, feet[1], feet[2] + std::cos(a) * rr};
            if (!gMap->nav->Nearest(c, 250.f, q) || std::fabs(q[1] - hisFloor) > 400.f || Dist2D(q, feet) > kWakeReach - 20.f || !BookFree(s, q)) continue;
            bool dup = false;
            for (const auto& o : spots) if (std::hypot(o[0] - q[0], o[2] - q[2]) < 150.f && std::fabs(o[1] - q[1]) < 300.f) dup = true;
            if (dup || !StrikeClear(q, t, lying, false)) continue;
            spots.push_back({q[0], q[1], q[2], Dist2D(q, s.pos), 0.f});
        }
}
// its goal on his side: beta 25's own when it is there and its way is good, else the shortest way among the others (each way
// at least as long as the straight line to its end: those that cannot beat the best so far are not searched; 6 searches at
// most). False: none
bool SideGoal(const Self& s, const PlayerNow& t, bool lying, bool books, const float* from, float* goal, std::vector<doll::Waypoint>& way, float* length) {
    std::vector<std::array<float, 5>> spots;
    SideSpots(s, t, lying, books, spots);
    std::stable_sort(spots.begin(), spots.end(), [](const std::array<float, 5>& x, const std::array<float, 5>& y) { return x[4] != y[4] ? x[4] > y[4] : x[3] < y[3]; });
    float best = 1e30f;
    int searched = 0;
    for (size_t i = 0; i < spots.size() && searched < 6; ++i) {
        const float* q = spots[i].data();
        if (spots[i][4] < 0.5f && Dist3(from, q) >= best) continue;
        std::vector<doll::Waypoint> w;
        float len = 0;
        ++searched;
        if (!FindWay(from, q, w, &len) || w.size() < 2 || !WayOk(w, true) || (books && !WayBookFree(s, w)) || len >= best) continue;
        if (!StrikeClear(q, t, lying, true)) continue;   // (a thing of the game between: not his side)
        best = len; way.swap(w); memcpy(goal, q, 3 * sizeof(float));
        if (spots[i][4] > 0.5f) break;                   // (beta 25's goal on his side: kept)
    }
    if (best > 1e29f) return false;
    *length = best;
    return true;
}
bool WakeWay(Body& b, const Self& s, const PlayerNow& t, bool path, float* length = nullptr) {
    const float hisFloor = t.pos[1] - (StanceOf(t) == S_LIE ? 300.f : kHips);
    const float feet[3] = {t.pos[0], hisFloor, t.pos[2]};
    bool books = false;
    for (int i = 0; i < gBookN; ++i) if (gBookList[i].slot != s.slot && Dist2D(gBookList[i].pos, feet) < 6000.f) books = true;
    if (gWakeSide) {                                        // (v150-host beta 25, wakeway) its goal on his side
        float from[3], goal[3], len = 0;
        bool far = false;
        std::vector<doll::Waypoint> w;
        const int id = (int)(&t - gSeen);
        const bool lying = id >= 0 && id < 16 ? Lying(id) : StanceOf(t) == S_LIE;
        if (!StartPoint(s, from, &far) || !SideGoal(s, t, lying, books, from, goal, w, &len)) return false;
        if (!path) { if (length) *length = len; return true; }
        float q0[3];                                       // (a goal other than beta 25's: counted, and told with MGO1_VDOLL_DEBUG)
        if (!books && b.wakeRoundFor != b.wakeSince && (!gMap->nav->Nearest(feet, 1500.f, q0) || std::hypot(q0[0] - goal[0], q0[2] - goal[2]) > 1.f)) {
            ++gWakeRound; b.wakeRoundFor = b.wakeSince;    // (once a waking)
            if (gDebug) BLOG("vdoll %d: its way to player %d ends on his side at %.0f %.0f %.0f (%.2f m from him, %.1f m long)", s.k + 1, id + 1, goal[0], goal[1], goal[2], Dist2D(goal, feet) / 1000.f, len / 1000.f);
        }
        b.path.clear();
        if (far) b.path.push_back({s.pos[0], s.pos[1] - kHips, s.pos[2]});
        for (const auto& p : w) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link});
        b.next = 1;
        return true;
    }
    std::vector<std::array<float, 4>> spots;                // (x y z, its distance to the doll)
    if (!books) spots.push_back({feet[0], feet[1], feet[2], 0.f});
    else
        for (int k = 0; k < 32; ++k) {                     // (two rings: 0.65 m, and 0.85 m for a reader close to his book)
            const float a = (float)(k & 15) * kPi / 8.f, r = k < 16 ? 650.f : 850.f;
            const float c[3] = {feet[0] + std::sin(a) * r, feet[1], feet[2] + std::cos(a) * r};
            float q[3];
            if (!gMap->nav->Nearest(c, 200.f, q) || std::fabs(q[1] - hisFloor) > 400.f || Dist2D(q, feet) > kWakeReach - 20.f || !BookFree(s, q)) continue;
            spots.push_back({q[0], q[1], q[2], Dist2D(q, s.pos)});
        }
    std::sort(spots.begin(), spots.end(), [](const std::array<float, 4>& x, const std::array<float, 4>& y) { return x[3] < y[3]; });
    float from[3];
    bool far = false;
    if (!StartPoint(s, from, &far)) return false;
    for (size_t i = 0; i < spots.size() && i < 6; ++i) {
        float goal[3];
        if (!gMap->nav->Nearest(spots[i].data(), books ? 300.f : 1500.f, goal)) continue;
        if (std::hypot(goal[0] - feet[0], goal[2] - feet[2]) > kWakeReach || std::fabs(goal[1] - hisFloor) > 400.f) continue;   // (the way's end in his reach)
        std::vector<doll::Waypoint> w;
        float len = 0;
        if (!FindWay(from, goal, w, &len) || w.size() < 2 || !WayOk(w, true) || (books && !WayBookFree(s, w))) continue;
        if (!path) { if (length) *length = len; return true; }
        b.path.clear();
        if (far) b.path.push_back({s.pos[0], s.pos[1] - kHips, s.pos[2]});
        for (const auto& p : w) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link});
        b.next = 1;
        return true;
    }
    return false;
}
// its CQC throw's hit on player v (45 frames after its press): the records a player's game sends for it (wake_m1); true: it reached him
bool WakeThrowHit(Body& b, const Self& s, int v) {
    const PlayerNow& t = gSeen[v];
    const float d = Dist2D(s.pos, t.pos);
    const vwake::Victim vic = VictimOf(s, v);
    float myFeet[3];
    Feet(s.pos, myFeet);
    const bool wall = !(d > 1200.f || std::fabs(t.pos[1] - s.pos[1]) > 400.f || vic.lying) && WallBetween(myFeet, v);   // ((v150-host beta 25, wakeway))
    if (d > 1200.f || std::fabs(t.pos[1] - s.pos[1]) > 400.f || vic.lying || wall) {
        BLOG("vdoll %d: its CQC throw misses player %d (%.2f m%s)", s.k + 1, v + 1, d / 1000.f, vic.lying ? ", on the ground" : wall ? ", a wall between" : "");
        return false;
    }
    const int stun = vwake::ThrowStun(vic);
    float c[3];
    Chest(t, c);
    const float ux = (t.pos[0] - s.pos[0]) / std::max(1.f, d), uz = (t.pos[2] - s.pos[2]) / std::max(1.f, d);
    std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, 0x20, 0x02, 0x0B};
    Put16(fx, (uint16_t)Tenth(c[0])); Put16(fx, (uint16_t)Tenth(c[1])); Put16(fx, (uint16_t)Tenth(c[2]));
    Put16(fx, (uint16_t)(int16_t)lrintf(ux * 20.f)); Put16(fx, (uint16_t)19); Put16(fx, (uint16_t)(int16_t)lrintf(uz * 20.f));
    s.send(s.ctx, v, 2, fx);
    s.send(s.ctx, v, 0, {0x04, (uint8_t)s.slot, 0x00, 0x00, (uint8_t)(stun & 0xFF), (uint8_t)((stun >> 8) & 0xFF), (uint8_t)vwake::kThrowKind, 0x00, 0x00});
    const uint16_t a = Angle16(Bearing(s.pos, t.pos));
    s.send(s.ctx, v, 1, {0x09, (uint8_t)s.slot, 0x09, (uint8_t)a, (uint8_t)(a >> 8)});
    ++b.wakeBlows; ++b.wakeLanded; b.wakeDone = true;
    BLOG("vdoll %d: throws player %d (reading) with CQC: the stun %d and his fall, which end his reading", s.k + 1, v + 1, stun);
    return true;
}
void WakeStop(Body& b, const Self& s, const char* why) {
    if (b.wakeTo >= 0 && (!strcmp(why, "no way to him") || !strcmp(why, "no room to run at him"))) b.wakeSkip[b.wakeTo] = gFrame + 600;   // (10 s: another place first)
    b.wakeRunUp = false; b.wakeRunSince = -1;
    if (b.wakeTo >= 0) BLOG("vdoll %d: stops waking player %d after %.1f s (%s; punch combos given: %d)", s.k + 1, b.wakeTo + 1, (gFrame - b.wakeSince) / 59.94, why, b.wakeCombos);
    b.wakeTo = -1; b.wakeWhy = 0; b.wakeCombo = -1; b.path.clear(); b.next = 0; b.planAt = 0;
}
// ---- (v150-host beta 25, wakeramp: MGO1_VDOLL_WAKE_SLOPE, docs/VIRTUAL_DOLL.md 49) a teammate lying on a ramp or stairs, woken as a
// player wakes him there. A player runs at him from anywhere on the walkable floor with a straight run, down, up or across the slope,
// at his game's pace there (GamePace: 389 mm a record on the flat and down a ramp, 250 on stairs, 225 up a ramp of 15 degrees or more),
// presses Circle within 1.0 m of him and runs on into him (measured on real clients on the n003a stairs: wakeramp_m1). Beta 23 to
// 25's doll wanted its run-up place 3 m from him within 0.4 m of his height (on the n003a stairs none from most sides: one kick or
// none, then "no room to run at him" and 10 s skipped), its straight run at him and its press within 0.4 m of his floor, and took
// its run on stairs or up a ramp (a player's pace there) for no run (under 80 % of the flat run): its press never came. Now its
// run-up place may be up or down the slope when its straight line to him is walkable on the bodies' mesh (a ledge or a drop between:
// not), its run at him and its press take such a line, it runs at a player's pace there and that pace counts as a run. The blow's
// own reach is unchanged (WakeBlow: 1.0 m and 0.4 m of floor; StrikeClear). MGO1_VDOLL_WAKE_SLOPE=0: as beta 25
bool gWakeSlope = true;
long long gWakeSlopeRuns = 0;                      // (run-ups taken up or down a slope, off his height by over 0.4 m: Status)
// a player's run pace toward (ux, uz) from where it stands (mm a frame; the flat run with the switch off)
float WakePace(const Body& b, const Self& s, float ux, float uz) { return gWakeSlope ? std::min(kRun, GamePace(b, s, ux, uz)) : kRun; }
// the least pace a player runs at on the floor under it, any way (80 % of it is a run; the flat run with the switch off)
float WakeRunPace(const Body& b, const Self& s) {
    float p = kRun;
    if (gWakeSlope) for (int k = 0; k < 4; ++k) p = std::min(p, GamePace(b, s, k == 0 ? 1.f : k == 1 ? -1.f : 0.f, k == 2 ? 1.f : k == 3 ? -1.f : 0.f));
    return p;
}
// the floor from `from` to `to` one walkable straight way on the bodies' mesh (a ramp, stairs), clear to within 0.3 m of `to`
bool SlopeLine(const float* from, const float* to) {
    const float d = Dist2D(from, to), y = Bearing(from, to);
    return gWakeSlope && gMap->nav->ClearAlong(from, std::sin(y), std::cos(y), d, true) >= d - 300.f;
}
// a run-up place at c (3 m from him) up or down the slope: the mesh's point there 0.6 to 2.4 m above or below his anchor `his`, off his
// height by over 0.4 m, whose straight line to him is walkable
bool SlopeSpot(const float* c, const float* his, float* q) {
    if (!gWakeSlope) return false;
    for (float up : {600.f, -600.f, 1200.f, -1200.f, 1800.f, -1800.f, 2400.f, -2400.f}) {
        const float c2[3] = {c[0], his[1] + up, c[2]};
        if (gMap->nav->Nearest(c2, 300.f, q) && std::fabs(q[1] - his[1]) > 400.f && SlopeLine(q, his)) return true;
    }
    return false;
}
// true: it goes to a teammate or gives him its blows this frame (its moves done)
// (v150-host beta 22, wake) a straight run toward a point on the floor (a clear line asked first), up to stopAt from it: its
// step this frame; false when it cannot step (a wall, another floor)
// ((v150-host beta 23) slack: how far short of the point the mesh's straight line may stop: a teammate lying against a wall lies off
// the bodies' mesh, which keeps a body's width off every wall; wake_b1_off_t15: the line to him cut 0.3 to 0.9 m short each time)
bool StepTo(Body& b, const Self& s, const float* to, float stopAt, int dt, float slack = 300.f) {
    float feet[3];
    Feet(s.pos, feet);
    const float d = Dist2D(s.pos, to), y = Bearing(s.pos, to);
    TurnBody(b, y, dt, kBodyTurn, true);
    // ((v150-host beta 23) there within 1 mm: its float steps of a few thousandths of a mm moved it no more, true for ever: n003a part 1)
    if (d <= stopAt + 1.f || gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), d, true) < d - slack) return false;
    const float step = std::min(WakePace(b, s, std::sin(y), std::cos(y)) * dt, d - stopAt);   // ((v150-host beta 25, wakeramp) a player's pace there)
    const float x = s.pos[0] + std::sin(y) * step, z = s.pos[2] + std::cos(y) * step, f = FloorNear(x, z, b.floorY);
    if (!(step > 0.f) || f < -1e29f || std::fabs(f - b.floorY) > 300.f) return false;
    s.pos[0] = x; s.pos[2] = z; b.floorY = f; s.pos[1] = f + kHips; b.speed = dt ? step / dt : 0; b.ran += step;
    return true;
}
// (v150-host beta 22, wake) where it goes back to so as to run at a teammate lying (the combo pressed running: wake_ts_on2): 3 m
// from him on the mesh, on his floor, a clear straight line to him, out of every book's reach; its own side first. False: none
bool LineBookFree(const Self& s, const float* a, const float* c, float shortOf) {   // (its hips along a to c, short of c by shortOf, out of every book's reach)
    const float l = Dist2D(a, c) - shortOf;
    if (l <= 0.f) return BookFree(s, a);
    const int n = std::max(1, (int)(l / 250.f));
    const float L = Dist2D(a, c);
    for (int k = 0; k <= n; ++k) {
        const float f = l * (float)k / (float)n / std::max(1.f, L), q[3] = {a[0] + (c[0] - a[0]) * f, a[1] + (c[1] - a[1]) * f, a[2] + (c[2] - a[2]) * f};
        if (!BookFree(s, q)) return false;
    }
    return true;
}
bool RunUpWay(Body& b, const Self& s, const float* spot);
bool RunUpSpot(Body& b, const Self& s, const PlayerNow& t, float* spot) {
    float his[3];
    // ((v150-host beta 25, wakeway) its run-up round the place on his side nearest him: the mesh's nearest point to him may lie
    // beyond the wall he lies against)
    if (gWakeSide ? !SideAnchor(s, (int)(&t - gSeen), his) : !gMap->nav->Nearest(t.pos, 1500.f, his)) return false;
    const float a0 = Bearing(t.pos, s.pos);
    float cut[8][3];                                       // ((v150-host beta 23) the places its straight way to is cut: by its way then)
    int cuts = 0;
    float bent[8][3];                                      // ((v150-host beta 25, wakeway) the places whose straight line to him is cut)
    int bents = 0;
    for (float da : {0.f, 0.6f, -0.6f, 1.2f, -1.2f, 1.9f, -1.9f, 3.1416f}) {
        const float a = a0 + da, c[3] = {his[0] + std::sin(a) * 3000.f, his[1], his[2] + std::cos(a) * 3000.f};
        float q[3];
        // ((v150-host beta 25, wakeramp) on a ramp or stairs: the place there up or down the slope, its straight line to him walkable)
        const bool level = gMap->nav->Nearest(c, 300.f, q) && std::fabs(q[1] - his[1]) <= 400.f;
        if ((!level && !SlopeSpot(c, his, q)) || Dist2D(q, his) < 2400.f || Dist2D(q, t.pos) < 2400.f || !BookFree(s, q)) continue;
        const float d = Dist2D(q, his), y = Bearing(q, his);
        if (gMap->nav->ClearAlong(q, std::sin(y), std::cos(y), d, true) < d - 300.f || !LineBookFree(s, q, his, 450.f)) {
            if (gWakeSide) memcpy(bent[bents++], q, sizeof q);
            continue;
        }
        float feet[3];
        Feet(s.pos, feet);
        const float d2 = Dist2D(feet, q), y2 = Bearing(feet, q);
        if (gMap->nav->ClearAlong(feet, std::sin(y2), std::cos(y2), d2, true) < d2 - 300.f || !LineBookFree(s, feet, q, 0.f)) {   // (its own straight way there)
            memcpy(cut[cuts++], q, sizeof q);
            continue;
        }
        memcpy(spot, q, sizeof q);
        b.path.clear(); b.next = 0;
        if (std::fabs(q[1] - his[1]) > 400.f) ++gWakeSlopeRuns;   // ((v150-host beta 25, wakeramp))
        return true;
    }
    // ((v150-host beta 23, wake) none straight from where its combo carried it (n003a part 3): the first it has a way to, its way set)
    for (int i = 0; i < cuts; ++i)
        if (RunUpWay(b, s, cut[i])) { memcpy(spot, cut[i], sizeof cut[i]); if (std::fabs(cut[i][1] - his[1]) > 400.f) ++gWakeSlopeRuns; return true; }
    // ((v150-host beta 25, wakeway) none with a straight line to him, a cramped place (n003a's upper floor at 26443 -7545: from 3 m
    // round him the longest line to him is 1.24 m; the wake test's part 9 met it once the doll's earlier ways changed): the first
    // whose way back to him is 6 m at most, run in by that way (the run-in follows its way when the line to him is cut), as a
    // player runs at him round the bend; beta 23 to 25 gave him up there 10 s after the first kick
    for (int i = 0; i < bents; ++i) {
        std::vector<doll::Waypoint> w;
        float len = 0;
        if (!FindWay(bent[i], his, w, &len) || w.size() < 2 || len > 6000.f || !WayOk(w, true) || !WayBookFree(s, w)) continue;
        if (RunUpWay(b, s, bent[i])) { memcpy(spot, bent[i], sizeof bent[i]); if (std::fabs(bent[i][1] - his[1]) > 400.f) ++gWakeSlopeRuns; return true; }
    }
    return false;
}
// (v150-host beta 23, wake) its way to its run-up's place when the straight line there is cut (the mesh's line from where its
// combo carried it, on or beside him, stopped short: tests/vdoll_wake_test.cpp part 1 on n024a, 3 on n001a), out of every book's
// reach, 8 m at most; as a player goes round to come at him again. False: none
bool RunUpWay(Body& b, const Self& s, const float* spot) {
    float from[3];
    bool far = false;
    if (!StartPoint(s, from, &far)) return false;
    std::vector<doll::Waypoint> w;
    float len = 0;
    if (!FindWay(from, spot, w, &len) || w.size() < 2 || len > 8000.f || !WayOk(w, true) || !WayBookFree(s, w)) return false;
    b.path.clear();
    if (far) b.path.push_back({s.pos[0], s.pos[1] - kHips, s.pos[2]});
    for (const auto& p : w) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link});
    b.next = 1;
    return true;
}
// (v150-host beta 23, wake; benches only, MGO1_VDOLL_TEST_WAKE=2) nobody down: its way to the nearest player of its team (not a
// virtual doll) until within 4 m of him, then still there, so that a teammate's knock-out or reading is within its reach (a bench's
// reader reads 8 s: a waker 40 m away would never serve him)
bool WakeFollow(Body& b, const Self& s, int dt) {
    int best = -1;
    float bestD = 1e30f;
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || gSeen[id].team != s.team || BodyAt(id) >= 0) continue;
        const float d = Dist2D(gSeen[id].pos, s.pos);
        if (d < bestD) { bestD = d; best = id; }
    }
    if (best < 0) return false;
    if (bestD < 4000.f) { b.speed = 0; b.path.clear(); b.next = 0; return true; }
    if (gFrame - b.wakePlanAt >= 60 || b.next >= b.path.size()) { b.wakePlanAt = gFrame; if (!PlanTo(b, s, gSeen[best].pos)) return false; }
    StandUp(b);
    Run(b, s, dt);
    return true;
}
// (v150-host beta 24, cqcfight) an enemy standing (alive, not knocked out) within 2.5 m of that place whom the doll sees from its eye: a
// teammate he has just thrown, knocked out or holds by a book is no wake's moment, as a player fights the enemy at him first
bool FoeAt(const Self& s, const float* eye, const float* at) {
    for (int e = 0; e < 16; ++e) {
        if (!gHave[e] || !gSeen[e].object || !gSeen[e].alive || gSeen[e].asleep || !Enemy(gSeen[e], s.team)) continue;
        if (Dist2D(gSeen[e].pos, at) > 2500.f || std::fabs(gSeen[e].pos[1] - at[1]) > 1500.f) continue;
        float c[3];
        Chest(gSeen[e], c);
        if (SightClear(eye, c)) return true;
    }
    return false;
}
bool WakeFrame(Body& b, const Self& s, int dt) {
    if (!gWake || !Ready()) return false;
    const bool busy = b.target >= 0 || gFrame - b.foeAt < 180 || b.carrying || Blind(b) || b.bookId || b.fallAt >= 0 || gFrame < b.landUntil ||
                      gFrame < b.climbUntil || b.grabStart >= 0 || b.throwAt >= 0 || gFrame < b.rollUntil || gFrame < b.rushUntil || b.rpgPullAt >= 0 ||
                      gFrame < b.avertUntil || s.team < 0 || s.team > 1 || gRule < 1 || gRule > 6;
    if (b.wakeTo >= 0) {
        const int v = b.wakeTo;
        long long left = 0;
        const int why = DownWhy(v, &left);
        if (b.wakeCombo < 0 && why != b.wakeWhy) {
            if (b.wakeLanded) { ++b.woken; BLOG("vdoll %d: player %d %s after its blows (%.1f s after it went to him)", s.k + 1, v + 1, b.wakeWhy == 1 ? "is up" : "reads no more", (gFrame - b.wakeSince) / 59.94); }
            WakeStop(b, s, b.wakeLanded ? "done" : why ? "he is something else now" : "up or free by himself");
            return false;
        }
        if (busy && b.wakeCombo < 0) { WakeStop(b, s, b.target >= 0 || gFrame - b.foeAt < 180 ? "an enemy in its sight" : "busy"); return false; }
        // ((v150-host beta 23) never in a combo's motion: wake_c1_on_t18 gave up 11 frames after a kick that woke him, its motion's end
        // record never sent)
        if (b.wakeCombo < 0 && (gFrame - b.wakeSince > kWakeGiveUp || b.wakeCombos >= kWakeCombos)) { WakeStop(b, s, "given up"); return false; }
        const PlayerNow& t = gSeen[v];
        const float d = Dist2D(s.pos, t.pos);
        static const std::vector<uint8_t> kMotion = {0x08, 0x0A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // (its punch's / throw's motion, each tick, as measured)
        if (b.wakeCombo >= 0 && b.wakeMode == WM_THROW) {   // the CQC throw: its run into him, its motion, the hit at +45, the end at +86
            b.aiming = false;
            const long long c = gFrame - b.wakeCombo;
            float feet[3];
            Feet(s.pos, feet);
            const float y = Bearing(s.pos, t.pos);
            b.speed = 0;
            if (c < 12 && d > 500.f && gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), d, true) >= d - 300.f) {
                const float step = std::min(WakePace(b, s, std::sin(y), std::cos(y)) * dt, d - 500.f);   // ((v150-host beta 25, wakeramp) a player's pace)
                const float x = s.pos[0] + std::sin(y) * step, z = s.pos[2] + std::cos(y) * step, f = FloorNear(x, z, b.floorY);
                if (step > 0.f && f > -1e29f && std::fabs(f - b.floorY) <= 300.f) { s.pos[0] = x; s.pos[2] = z; b.floorY = f; s.pos[1] = f + kHips; b.speed = dt ? step / dt : 0; b.ran += step; }
            }
            TurnBody(b, y, dt, kBodyTurn, true);
            if (s.tick && c >= vwake::kThrowMotionFrom && c <= vwake::kThrowMotionEnd) s.send(s.ctx, s.slot, 1, kMotion);
            // ((v150-host beta 23) once a combo: a frame given twice (dt 0) sent the blow twice, wake_b1_on_t16 f16168)
            if (c >= vwake::kThrowHitAt && c - std::max(1, dt) < vwake::kThrowHitAt && !(b.wakeSent & 8)) { b.wakeSent |= 8; WakeThrowHit(b, s, v); }
            if (c >= vwake::kThrowEnd) { b.wakeCombo = -1; ++b.wakeCombos; b.wakeDoneAt = gFrame; s.send(s.ctx, s.slot, 1, {0x08, 0x00, 0x00}); }
            return true;
        }
        if (b.wakeCombo >= 0) {                    // the combo pressed running into him (wake_ts_on2): its run on, its presses, the kick, the end
            b.aiming = false;
            const long long c = gFrame - b.wakeCombo;
            float feet[3];
            Feet(s.pos, feet);
            const float y = Bearing(s.pos, t.pos);
            b.speed = 0;
            if (c < vwake::kRunCarry && d > 250.f && gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), d, true) >= d - 300.f) {
                const float step = std::min(WakePace(b, s, std::sin(y), std::cos(y)) * dt, d - 250.f);   // ((v150-host beta 25, wakeramp) a player's pace)
                const float x = s.pos[0] + std::sin(y) * step, z = s.pos[2] + std::cos(y) * step, f = FloorNear(x, z, b.floorY);
                if (step > 0.f && f > -1e29f && std::fabs(f - b.floorY) <= 300.f) { s.pos[0] = x; s.pos[2] = z; b.floorY = f; s.pos[1] = f + kHips; b.speed = dt ? step / dt : 0; b.ran += step; }
            }
            TurnBody(b, y, dt, kBodyTurn, true);
            if (s.tick && c >= vwake::kRunMotionFrom && c < vwake::kRunComboEnd) s.send(s.ctx, s.slot, 1, kMotion);
            // (all three presses: on a teammate lying only the third blow, the kick, lands; a player who sees him up presses no more)
            for (int i = 1; i < vwake::kBlows; ++i)
                if (b.wakePress == i && why == b.wakeWhy && c >= vwake::kRunPressAt[i]) ++b.wakePress;
            for (int i = 0; i < vwake::kBlows; ++i) {
                const long long at = i == 2 ? vwake::kRunKickAt : vwake::kHitAt[i] + (vwake::kRunKickAt - vwake::kHitAt[2]);
                if (i < b.wakePress && c >= at && c - std::max(1, dt) < at && !(b.wakeSent & (1 << i))) {
                    b.wakeSent |= 1 << i;
                    if (vwake::Reaches(i, VictimOf(s, v))) WakeBlow(b, s, v, i);
                    else if (gDebug) BLOG("vdoll %d: its punch passes over player %d lying (no record, as a player's)", s.k + 1, v + 1);
                }
            }
            if (c >= vwake::kRunComboEnd) { b.wakeCombo = -1; ++b.wakeCombos; b.wakeDoneAt = gFrame; b.wakeRunSince = -1; s.send(s.ctx, s.slot, 1, {0x08, 0x00, 0x00}); }
            return true;
        }
        if (b.wakeDoneAt >= 0 && gFrame - b.wakeDoneAt < (b.wakeDone ? kWakeAfter : 20)) { b.speed = 0; return true; }   // (its blow's effect awaited)
        const float hisFloor = t.pos[1] - (StanceOf(t) == S_LIE ? 300.f : kHips);
        // (the combo's first press running into him within 1.0 m: standing still it is another move; the throw with friendly fire off
        // pressed running too within 0.9 m: standing still it reached no teammate then)
        const bool runIn = b.wakeMode == WM_COMBO || (b.wakeMode == WM_THROW && FriendlyFire() != 1);
        if (runIn) {
            float feet[3], his[3];
            Feet(s.pos, feet);
            const float y = Bearing(s.pos, t.pos);
            // ((v150-host beta 25, wakeramp) or one walkable straight way between it and him: a ramp, stairs)
            const bool floorOk = std::fabs(hisFloor - b.floorY) <= 400.f || (d <= 4000.f && SlopeLine(feet, t.pos));
            const float pressFrom = b.wakeMode == WM_COMBO ? vwake::kRunPressFrom : kWakeReach;
            // ((v150-host beta 23) clear to within 0.9 m of him: lying against a wall he is off the mesh, wake_b1_off_t15)
            const float slack = b.wakeMode == WM_COMBO ? vwake::kRunPressFrom - 100.f : 300.f;
            // ((v150-host beta 25, wakeway) and no wall between it and him: the mesh's line with its slack ran at a thin wall from
            // its far side, within a press of him beyond it)
            const bool side = !gWakeSide || d > 4000.f || StrikeClear(feet, t, Lying(v), false);
            const bool line = floorOk && side && gMap->nav->Nearest(t.pos, 1500.f, his) && gMap->nav->ClearAlong(feet, std::sin(y), std::cos(y), d, true) >= d - slack &&
                              LineBookFree(s, feet, his, 450.f);
            bool moved = false;
            if (b.wakeRunUp) {                     // (3 m back from him, straight, then at him)
                if (b.path.empty() && StepTo(b, s, b.wakeSpot, 250.f, dt)) return true;
                // (v150-host beta 23, wake) the straight line cut short of the place: round by its way there (RunUpWay)
                if (Dist2D(s.pos, b.wakeSpot) > 600.f) {
                    if (b.path.empty()) {
                        if (!RunUpWay(b, s, b.wakeSpot)) { WakeStop(b, s, "no room to run at him"); return false; }
                        if (gDebug) BLOG("vdoll %d: its straight line back cut %.2f m from its run-up's place: by its way there", s.k + 1, Dist2D(s.pos, b.wakeSpot) / 1000.f);
                    }
                    if (b.next < b.path.size()) { StandUp(b); Run(b, s, dt); return true; }
                    if (Dist2D(s.pos, b.wakeSpot) > 600.f) { WakeStop(b, s, "no room to run at him"); return false; }
                }
                b.wakeRunUp = false; b.wakeRunSince = -1; b.path.clear(); b.next = 0;
                b.wakeFastAt = gFrame;             // ((v150-host beta 23) it ran there: at him now, not 3 m back again)
                return true;
            }
            // ((v150-host beta 25, wakeway) on his side only: passing him beyond a wall on its way round, it runs on)
            if (d < 2000.f && side && b.wakeRunSince < 0 && gFrame - b.wakeFastAt > 12) {   // (close and not running: 3 m back first, its run-up, as a player)
                if (!RunUpSpot(b, s, t, b.wakeSpot)) { WakeStop(b, s, "no room to run at him"); return false; }
                b.wakeRunUp = true;                // (its way there set when the straight one is cut: RunUpSpot)
                if (gDebug) BLOG("vdoll %d: %.2f m from player %d, not running: 3 m back first", s.k + 1, d / 1000.f, v + 1);
                return true;
            }
            // (v150-host beta 23, wake) its straight step cut short of him (the mesh's line from a step further on): by its way, 0.5 s
            const bool cut = gFrame - b.wakeCutAt < 30;
            if (line && d <= 4000.f && !cut) {     // (straight at him)
                if (!b.path.empty()) { b.path.clear(); b.next = 0; }
                moved = StepTo(b, s, t.pos, 200.f, dt, slack);
                if (!moved && d > pressFrom) b.wakeCutAt = gFrame;
            }
            if (!moved && (!line || d > pressFrom)) {
                if (gFrame - b.wakePlanAt >= 30 || b.next >= b.path.size()) {
                    b.wakePlanAt = gFrame;
                    if (!WakeWay(b, s, t, true)) { WakeStop(b, s, "no way to him"); return false; }
                }
                StandUp(b);
                Run(b, s, dt);
                moved = true;
            }
            // ((v150-host beta 23) running: a stop of 12 frames at most on its way (a corner's turn, its copy's leash: wake_b1_off_t15, where
            // each such frame sent it 3 m back again, 14.7 s to its first kick) does not end its run; its press while it runs)
            // ((v150-host beta 25, wakeramp) a run on stairs or up a ramp at a player's pace there is a run)
            if (moved && b.speed >= 0.8f * WakeRunPace(b, s)) { if (b.wakeRunSince < 0) b.wakeRunSince = gFrame; b.wakeFastAt = gFrame; }
            else if (gFrame - b.wakeFastAt > 12) b.wakeRunSince = -1;
            if (floorOk && side && d <= pressFrom && b.wakeRunSince >= 0 && gFrame - b.wakeRunSince >= 15 && gFrame - b.wakeFastAt <= 6) {
                b.wakeCombo = gFrame; b.wakePress = 1; b.wakeLanded = 0; b.wakeDone = false; b.wakeSent = 0;
                b.wakeGauge = gDollOut[v].out ? gDollOut[v].stamina : t.stamina;   // (his gauge now: his copy's, a doll's own)
                if (gDebug) BLOG("vdoll %d: Circle running into player %d at %.2f m (running %lld frames)", s.k + 1, v + 1, d / 1000.f, gFrame - b.wakeRunSince);
            }
            return true;
        }
        float myFeet[3];
        Feet(s.pos, myFeet);
        const bool fromSide = !gWakeSide || d > kWakeReach || StrikeClear(myFeet, t, Lying(v), false);   // ((v150-host beta 25, wakeway) from his side)
        if (d <= kWakeReach && std::fabs(hisFloor - b.floorY) <= 400.f && fromSide) {   // within reach: turned to him, the throw's press
            b.speed = 0; b.path.clear();
            if (TurnBody(b, Bearing(s.pos, t.pos), dt) > 20.f) return true;
            StandUp(b);
            if (gFrame < b.standUntil) return true;
            b.wakeCombo = gFrame; b.wakePress = 1; b.wakeLanded = 0; b.wakeDone = false; b.wakeSent = 0;
            b.wakeGauge = gDollOut[v].out ? gDollOut[v].stamina : t.stamina;   // (his gauge now: his copy's, a doll's own)
            return true;
        }
        if (b.wakePlanAt > 0 && b.next >= b.path.size() && d < 2000.f) { WakeStop(b, s, "no way to him"); return false; }   // (its way's end, not in his reach: another floor)
        if (gFrame - b.wakePlanAt >= 30 || b.next >= b.path.size()) {   // to him (his feet), the way made again every 0.5 s
            b.wakePlanAt = gFrame;
            if (!WakeWay(b, s, t, true)) { WakeStop(b, s, "no way to him"); return false; }
        }
        StandUp(b);
        Run(b, s, dt);
        return true;
    }
    if (busy) return false;
    if (gFrame < b.wakeLookAt) return gWakeFollow && WakeFollow(b, s, dt);
    b.wakeLookAt = gFrame + kWakeLook;
    float eye[3];
    EyeNow(b, s, eye);
    int best = -1, bestWhy = 0, bestMode = WM_NONE;
    float bestWay = 1e30f;
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || gFrame < b.wakeSkip[id]) continue;
        const PlayerNow& o = gSeen[id];
        if ((o.team != 0 && o.team != 1) || o.team != s.team) continue;
        long long left = 0;
        const int why = DownWhy(id, &left);
        if (!why) continue;
        const int mode = ModeFor(why);               // (friendly fire off: no blow reaches a teammate; a reader: the CQC throw)
        if (mode == WM_NONE) continue;
        float c[3];
        Chest(o, c);
        if (!gWakeTest && (Dist3(eye, c) > kWakeSees || !SightClear(eye, c))) continue;   // (it sees him)
        if (gCqcFight && FoeAt(s, eye, o.pos)) { ++gCqcFightCount[s.k & 15].noWake; continue; }   // ((v150-host beta 24, cqcfight) an enemy at him: that one first)
        float len = 0;
        if (!WakeWay(b, s, o, false, &len)) continue;      // (a place in his reach, out of the books' reach, a way to it out of it too)
        if (!gWakeTest && len > kWakeWay) continue;
        const long long need = (long long)(len / kRun) + 30 + (mode == WM_COMBO ? vwake::kRunKickAt : vwake::kThrowHitAt);
        if (left < need) {                         // (up or free by himself before it could be there)
            if (gDebug) BLOG("vdoll %d: player %d %s, up or free by himself in %.1f s, its way %.1f m: left to it", s.k + 1, id + 1, why == 1 ? "knocked out" : "reading", left / 59.94, len / 1000.f);
            continue;
        }
        // (the throw, a knock-down: only when it serves him, more than 2 s of his reading left once it is there, or an enemy in his sight)
        if (mode == WM_THROW && left - need < 120 && !EnemySees(s, id)) {
            if (gDebug) BLOG("vdoll %d: player %d reading, %.1f s of it left once there, no enemy in his sight: not thrown", s.k + 1, id + 1, (left - need) / 59.94);
            continue;
        }
        if (len < bestWay) { bestWay = len; best = id; bestWhy = why; bestMode = mode; }
    }
    if (best < 0) return gWakeFollow && WakeFollow(b, s, dt);
    b.wakeTo = best; b.wakeWhy = bestWhy; b.wakeMode = bestMode; b.wakeSince = gFrame; b.wakeCombos = 0; b.wakeCombo = -1; b.wakeDoneAt = -1; b.wakeLanded = 0; b.wakeDone = false;
    b.wakeRunSince = -1; b.wakeRunUp = false;
    b.wakePlanAt = 0; b.path.clear(); b.next = 0;
    BLOG("vdoll %d: goes to wake player %d (%s, %s) %.1f m away by its way (%s)", s.k + 1, best + 1, bestWhy == 1 ? "knocked out" : "reading a book",
         BodyAt(best) >= 0 ? "a virtual doll" : "a player", bestWay / 1000.f, bestMode == WM_THROW ? "the CQC throw" : "the punch combo");
    return WakeFrame(b, s, dt);
}
void NewLife(Body& b, const Self& s) {
    const int level = b.level;
    const int darts = b.darts, sleeps = b.sleeps, scopeRaises = b.scopeRaises, scopeShots = b.scopeShots, copyWaits = b.copyWaits;
    const int unstalls = b.unstalls, closeIns = b.closeIns;   // (v150-host beta 21: the standoff's counts, Status)
    const long long aimStillMax = b.aimStillMax, aimStillLap = b.aimStillLap;
    long long holdF[8]; memcpy(holdF, b.holdF, sizeof holdF);
    const bool blastStun = b.blastStun;
    const int shots = b.shots, hits = b.hits, goals = b.goals, reached = b.reached, fights = b.fights, holds = b.holds, lates = b.lates;
    const long long aliveF = b.aliveF, stillF = b.stillF, fightF = b.fightF, copyWaitF = b.copyWaitF;
    const int objWays = b.objWays, roundAbouts = b.roundAbouts, carries = b.carries, rolls = b.rolls, leans = b.leans, crouches = b.crouches;
    const int throws = b.throws, blastHits = b.blastHits, drops = b.drops, climbs = b.climbs, rollHits = b.rollHits, grabs = b.grabs, vaults = b.vaults;
    // (a grenade thrown before its death goes off all the same: Frame's blasts, from the copy below)
    const long long blastAt = b.blastAt; float blast[3]; memcpy(blast, b.blast, sizeof blast);
    const int blastSlot = b.blastSlot, blastTeam = b.blastTeam, blastGen = b.blastGen;
    auto blastSend = b.blastSend; void* blastCtx = b.blastCtx;
    const float ran = b.ran;
    const int coverDrops = b.coverDrops, coverAtRec = b.coverAtRec, reflexHolds = b.reflexHolds, shownHolds = b.shownHolds, shownLate = b.shownLate;   // (beta 21, cover)
    const int muzzleStops = b.muzzleStops, muzzleViews = b.muzzleViews;   // (v150-host beta 24, walls3: Status)
    const Body heavyWas = b;                          // (v150-host beta 22, heavy: a rocket in flight goes off all the same; its counts)
    const int bookReads = b.bookReads;
    const int takes = b.takes, specialKills = b.specialKills;   // (beta 22, pickup)
    const int flashes = b.flashes; const long long blindF = b.blindF;   // (v150-host beta 22, blast: Status)
    const int wakeBlows = b.wakeBlows, woken = b.woken;    // (v150-host beta 22, wake: Status)
    b = Body();
    b.wakeBlows = wakeBlows; b.woken = woken;
    b.takes = takes; b.specialKills = specialKills;
    b.eats = heavyWas.eats; b.foodGains = heavyWas.foodGains;   // (v150-host beta 25, food: Status)
    b.flashes = flashes; b.blindF = blindF;
    b.rocketAt = heavyWas.rocketAt; memcpy(b.rocketEnd, heavyWas.rocketEnd, sizeof b.rocketEnd); memcpy(b.rocketFrom, heavyWas.rocketFrom, sizeof b.rocketFrom);
    b.rocketHit = heavyWas.rocketHit; b.rocketSlot = heavyWas.rocketSlot; b.rocketTeam = heavyWas.rocketTeam; b.rocketGen = heavyWas.rocketGen;
    b.rocketSend = heavyWas.rocketSend; b.rocketCtx = heavyWas.rocketCtx;
    memcpy(b.rocketDir, heavyWas.rocketDir, sizeof b.rocketDir); b.rocketWall = heavyWas.rocketWall; b.rocketDone = heavyWas.rocketDone; b.rocketLaunch = heavyWas.rocketLaunch;   // (heavy2)
    b.rockets = heavyWas.rockets; b.rocketHits = heavyWas.rocketHits; b.flames = heavyWas.flames; b.flameHits = heavyWas.flameHits; b.heavyHolds = heavyWas.heavyHolds;
    b.bookReads = bookReads;
    b.bookReadyMs = RealMs() + (unsigned long long)vbook::kCooldownMs;   // (v150-host beta 22, book: a player's game, 450 VBlanks at each spawn)
    b.coverDrops = coverDrops; b.coverAtRec = coverAtRec; b.reflexHolds = reflexHolds; b.shownHolds = shownHolds; b.shownLate = shownLate;
    b.muzzleStops = muzzleStops; b.muzzleViews = muzzleViews;
    b.level = level; b.shots = shots; b.hits = hits; b.goals = goals; b.reached = reached; b.fights = fights; b.ran = ran;
    b.holds = holds; b.lates = lates; b.aliveF = aliveF; b.stillF = stillF; b.fightF = fightF; b.copyWaitF = copyWaitF;
    b.objWays = objWays; b.roundAbouts = roundAbouts; b.carries = carries; b.rolls = rolls; b.leans = leans; b.crouches = crouches;
    b.throws = throws; b.blastHits = blastHits; b.drops = drops; b.climbs = climbs; b.rollHits = rollHits; b.grabs = grabs; b.vaults = vaults;
    b.blastAt = blastAt; memcpy(b.blast, blast, sizeof blast); b.blastSlot = blastSlot; b.blastTeam = blastTeam; b.blastSend = blastSend; b.blastCtx = blastCtx; b.blastGen = blastGen;
    b.blastStun = blastStun; b.darts = darts; b.sleeps = sleeps;
    b.scopeRaises = scopeRaises; b.scopeShots = scopeShots; b.copyWaits = copyWaits;
    b.unstalls = unstalls; b.closeIns = closeIns; memcpy(b.holdF, holdF, sizeof holdF);
    b.aimStillMax = aimStillMax; b.aimStillLap = aimStillLap;
    b.primary = b.held = WeaponOf(s.k);
    for (int i = 0; i < kWeaponCount; ++i) b.mag[i] = kWeapons[i].magazine;
    GiveSpecial(b, s.k);                              // (v150-host beta 22, heavy: the bench's weapon box, MGO1_VDOLL_WEAPONS "+rpg7")
    // (v150-host beta 16) the game's weapons setting: no primary, its M1911A1 in its place; no gun at all, unarmed; no support,
    // no grenade (all allowed, or the setting not known: as before)
    const unsigned allowed = Allowed();
    if (!(allowed & 1)) b.primary = b.held = W_M1911;
    b.unarmed = !(allowed & 3);
    if (!(allowed & 4)) b.grenades = 0;
    else if (gWeaponRule > 0) b.grenades = 5;         // (the game gives 5 of the support row's item: K_support_full2's inventories)
    b.drawn = !b.unarmed;
    b.soldierPrimary = b.primary;
    {                                                  // (beta 18) a sneaker's or Snake's weapons (the mission objects read
        if (gHost.rule) gRule = gHost.rule();          // again: the round's rule and sides, not the last round's)
        gObj.at = -1000; SeeObjectives(gFrame);
        const int role = RoleOf(s);
        b.role = R_SOLDIER;
        if (role != R_SOLDIER) { SetRole(b, s, role); b.switchUntil = -1; }
    }
    b.life = s.life; b.lifeAt = gFrame;
    b.yaw = b.yawWas = b.sentYaw = Wrap(*s.facing / 65536.f * kTurn);
    b.pauseUntil = gFrame + 4 + (long long)(Rnd() % 12);
    b.floorY = s.pos[1] - kHips;
    if (Ready()) {
        const float g = FloorAt(s.pos[0], s.pos[1] - kHips, s.pos[2]);
        if (g > b.floorY - 400.f && g < b.floorY + 400.f) { b.floorY = g; s.pos[1] = g + kHips; }
    }
}

// ---- (v150-host beta 22, pickup: docs/VIRTUAL_DOLL.md 32) the special items lying on the map: the RPG-7, the M63, the
// flamethrower and the thermal goggles. At a round's start the host's script places one of each at one of its 4 points (the
// stage's points by hash, chunk 7 of every map's .geom): records 0x51 to 0x54 of the drops' manager, sent to every player.
// A player who walks onto one takes it: his own game's pick-up (US n003a 0x4C9FC8: within 400 mm along x and z of the box,
// 1500 mm up and down from its place raised by its reach, for that game's own player only) tells it on the drops' channel,
// its claim {number, kind << 10 | code, 1, its place in tenths, its count}, then the record emptied {number, code, 3, 0,
// count}; the host passes both on to every player and every game removes the box. At his death his game drops it again (a
// record of his own number, his slot << 10 | n: kind << 10 | code, his place, its rounds left), which any player can take;
// a special taken is back at a point of its own only in the next round (the host's script). A virtual doll has no game of
// its own: it sends those records itself (vdoll.cpp BodySend kind 3), as a player's game does ----
int gPickup = 1;                                 // MGO1_VDOLL_PICKUP: 1 (the M63, the goggles and any special the body fires),
                                                 // 0 off (beta 21: nothing taken), 2 (tests) the RPG-7 and the flamethrower too, carried
float gPickNear = 15000.f;                       // MGO1_VDOLL_PICKUP_NEAR (m): how near a special it sees is taken
// (the goggles' battery back a second while they are off) (v150-host beta 22, review) the game's best rate: its battery tick (US
// n003a 0x413C98) gives a point back every 720 x 5 time units in the states that recharge (1440 x 5 in others, none in the rest:
// a player standing with them on lost exactly 5 a second, probe 47), where the goggles worn take one every 60 x 5 (measured 5 a
// second): a point every 2.4 s. It came back at 2 a second (35 s from 20 to 90, where a player needs 168 s at least)
const float kGogglesBack = 300.f / 720.f;
std::vector<DropNow> gDrops;
long long gDropsAt = -1000;
std::vector<std::pair<int, long long>> gDropTaken;   // (a record a doll took, by number: left by the others 2 s, the host's record then says so)
std::vector<std::pair<long long, long long>> gDropLeft;   // ((doll << 32 | number) a doll gave up: until then)
int gDropSeq[16] = {};                           // (a doll's own drops: its slot << 10 | 0x100 + n, as a player's game numbers its own)
// a record's kind among the four (0 the M63, 1 the RPG-7, 2 the flamethrower, 3 the thermal goggles), -1 another
int SpecialKind(const DropNow& d) {
    if (d.kind == 3) return d.code == 0x0D ? 0 : d.code == 0x11 ? 1 : d.code == 0x09 ? 2 : -1;
    if (d.kind == 1) return d.code == 0x07 ? 3 : -1;
    return -1;
}
const char* const kSpecialName[4] = {"M63", "RPG-7", "flamethrower", "thermal goggles"};
// (v150-host beta 22) the special weapon agent heavy's body holds (the RPG-7, the flamethrower: vdoll::body::Special), 0 none
int HeavyHeld(int k, int* rounds) {
#ifdef VDOLL_BODY_HEAVY
    return Special(k, rounds);
#else
    (void)k; *rounds = 0; return 0;
#endif
}
bool Lying(const DropNow& d) { return d.number >= 0 && (d.kind == 3 || d.kind == 1) && d.state == 0 && d.box; }
bool TakenNow(int number) { for (const auto& t : gDropTaken) if (t.first == number && gFrame - t.second < 120) return true; return false; }
bool LeftNow(int k, int number) {
    const long long key = (long long)k << 32 | (unsigned)number;
    for (const auto& t : gDropLeft) if (t.first == key && gFrame < t.second) return true;
    return false;
}
// (v150-host beta 22, review) the record read again from the host's game at the take: the dolls look at the drops every 30
// frames, and a player's take that reached the host since then leaves the record claimed and its box gone (every player's
// game sees that as it comes). The doll claimed such a box too: two players then held one special
bool StillLying(const DropNow& x) {
    DropNow d[16];
    const int n = gHost.drops ? std::max(0, std::min(16, gHost.drops(d, 16))) : 0;
    for (int i = 0; i < n; ++i)
        if (d[i].number == x.number) { if (Lying(d[i]) && d[i].kind == x.kind && d[i].code == x.code) return true; break; }
    gDropsAt = -1000;                                 // (the drops looked at again next frame)
    return false;
}
void SeeDrops(long long frame) {
    if (!gPickup || !gHost.drops || frame - gDropsAt < 30) return;
    gDropsAt = frame;
    DropNow d[16];
    const int n = gHost.drops(d, 16);
    gDrops.assign(d, d + std::max(0, std::min(16, n)));
    // the specials lying now, said at each change (where the round put them; one taken, one dropped)
    static std::string was;
    std::string now;
    char t[96];
    for (const DropNow& x : gDrops)
        if (Lying(x) && SpecialKind(x) >= 0) { snprintf(t, sizeof t, "%s%s (record %#x) at %.0f %.0f %.0f", now.empty() ? "" : ", ", kSpecialName[SpecialKind(x)], (unsigned)x.number, x.pos[0], x.pos[1], x.pos[2]); now += t; }
    std::string key;
    for (const DropNow& x : gDrops) if (Lying(x) && SpecialKind(x) >= 0) key += std::to_string(x.number) + ",";
    if (key != was) { was = key; BLOG("vdoll: the special items lying now (rule %d): %s", gRule, now.empty() ? "none" : now.c_str()); }
    gDropTaken.erase(std::remove_if(gDropTaken.begin(), gDropTaken.end(), [&](const std::pair<int, long long>& p) { return frame - p.second >= 120; }), gDropTaken.end());
    gDropLeft.erase(std::remove_if(gDropLeft.begin(), gDropLeft.end(), [&](const std::pair<long long, long long>& p) { return frame >= p.second; }), gDropLeft.end());
}
// what this doll takes: by its role and the game's weapons setting (TEAM SNEAKING's sneakers nothing: the game leaves the
// lethal weapons lying for them (tsne.h, the box's act) and the goggles would take their stealth camouflage off; SNEAKING's
// Snake the goggles), one special weapon at a time and one the body fires (the M63; the RPG-7 and the flamethrower with agent
// heavy's body, VDOLL_BODY_HEAVY; MGO1_VDOLL_PICKUP=2 carries them without it), not twice
bool Wants(const Body& b, int k, int kind) {
    if (kind < 0 || b.role == R_SNEAKER) return false;
    if (kind == 3) return !b.goggles;
    // (v150-host beta 22, pickup2: review22b P10) a special weapon spent blocks none: an empty one blocked every other special for
    // the rest of the life (a player's empty weapon stays in his list, he walks onto another box all the same; another M63 gives
    // the doll that box's rounds, its own being none)
    int r = 0;
    if (b.role == R_SNAKE || !(Allowed() & 1) || b.unarmed || (b.special >= 0 && b.specialRounds > 0) || b.carryCode || (HeavyHeld(k, &r) && r > 0)) return false;
    if (kind == 0) return true;
#ifdef VDOLL_BODY_HEAVY
    // (v150-host beta 22, review) MGO1_VDOLL_HEAVY=0: the body never fires them, so it leaves them lying as without heavy's body
    // (it took them and held them, never fired, out of every player's reach until its death)
    return gHeavy || gPickup == 2;
#else
    return gPickup == 2;
#endif
}
// the game's pick-up zone (0x4C94D8): its hips within 400 mm of the box along x and z, 1500 mm of its place raised by the reach
bool InZone(const float* hips, const DropNow& d) {
    return std::fabs(hips[0] - d.pos[0]) < 400.f && std::fabs(hips[2] - d.pos[2]) < 400.f && std::fabs(hips[1] - (d.pos[1] + d.reach)) < 1500.f;
}
// (v150-host beta 22, pickup2: review22b P6) a take's emptied record, due 6 frames after its claim (now: at once), sent whatever
// the doll does then: alive (each frame, its fight, its reading, the round's end screens), knocked down, held or flinching
// (Owed), at its death (Died), at its leave (Quit). It waited while the doll was down (up to 21 s knocked out: the box on every
// player's screen meanwhile) and was lost at the round's end and at a leave; a player's game completes its take in all of these
bool SendOwed(Body& b, const Self& s, long long frame, bool now) {
    if (b.takeSecond.empty() || !(now || frame >= b.takeAt)) return false;
    s.send(s.ctx, s.slot, 3, b.takeSecond); b.takeSecond.clear();
    return true;
}
void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int k = 0; k < 4; ++k) o.push_back((uint8_t)(v >> (8 * k))); }
void PutTenths(std::vector<uint8_t>& o, const float* p) { for (int k = 0; k < 3; ++k) Put16(o, (uint16_t)(int16_t)(int)(p[k] * 0.1f)); }   // (0x39F418: x 0.1, cut)
// its take, as a player's game takes it: the claim, the record emptied 6 frames later (the game's own: its next pass of the
// manager after the box's act); then it holds it
void Take(Body& b, const Self& s, const DropNow& d) {
    const int kind = SpecialKind(d);
    std::vector<uint8_t> r;
    Put16(r, (uint16_t)d.number); Put16(r, (uint16_t)(d.flags & 0x7FFF)); r.push_back(0x01);
    PutTenths(r, d.pos); Put32(r, (uint32_t)d.count);
    if (!b.takeSecond.empty()) { s.send(s.ctx, s.slot, 3, b.takeSecond); b.takeSecond.clear(); }   // (v150-host beta 22, review: an earlier take's never lost)
    s.send(s.ctx, s.slot, 3, r);
    b.takeSecond.clear();
    Put16(b.takeSecond, (uint16_t)d.number); Put16(b.takeSecond, (uint16_t)(d.code & 0x1FF)); b.takeSecond.push_back(0x03);
    Put32(b.takeSecond, 0); Put32(b.takeSecond, (uint32_t)d.count);
    b.takeAt = gFrame + 6;
    gDropTaken.push_back({d.number, gFrame});
    ++b.takes; ++gPick.taken[kind];
    bool carried = false;
    if (kind == 3) { b.goggles = true; b.battery = 300.f; }
    else if (kind == 0) {                              // the M63: its primary for this life, the box's rounds
        b.special = W_M63; b.specialRounds = std::max(1, d.count);
        b.primary = W_M63; b.mag[W_M63] = std::min(kWeapons[W_M63].magazine, b.specialRounds);
        if (b.reloadAt < 0 && b.target < 0) Draw(b, W_M63);   // (in a fight its gun kept: the M63 at its end, as its primary)
    } else {                                           // the RPG-7, the flamethrower: agent heavy's body holds and fires them
#ifdef VDOLL_BODY_HEAVY
        carried = !Give(s.k, d.code, std::max(1, d.count));
#else
        carried = true;
#endif
        if (carried) { b.carryCode = d.code; b.carryRounds = std::max(1, d.count); }
    }
    BLOG("vdoll %d: takes the %s at %.0f %.0f %.0f (record %#x, %d %s): its claim, then the record emptied, as a player's game sends them%s", s.k + 1,
         kSpecialName[kind], d.pos[0], d.pos[1], d.pos[2], (unsigned)d.number, d.count, kind == 3 ? "of them" : "rounds",
         carried ? " (carried: the body does not fire it)" : "");
    b.pickNum = -1; b.path.clear(); b.next = 0; b.pauseUntil = gFrame + 6;
}
// (v150-host beta 22, pickup) its special weapon's rounds all spent: its own primary back (a player's empty weapon stays in
// his list, he takes another)
void SpecialSpent(Body& b, const Self& s) {
    if (b.special < 0 || b.specialRounds > 0 || b.primary != b.special) return;
    b.primary = b.soldierPrimary;
    // (v150-host beta 22, review) a reload begun with its last shot (Spent, in that frame) dropped: no player reloads an empty
    // special. It went on reloading the empty M63, again and again, while its fight lasted
    if (b.held == b.special) { b.reloadAt = -1; b.reloadUntil = 0; Draw(b, b.primary); }
    BLOG("vdoll %d: its %s spent (no round left): its %s again", s.k + 1, kWeapons[b.special].name, kWeapons[b.primary].name);
}
// each frame alive: its take's second record due, the special it goes to (taken in the zone, its last steps straight onto
// it, given up when a fight, the rule's item or 15 s come first), else every 100 ms the nearest special it sees lying near
// (within MGO1_VDOLL_PICKUP_NEAR, 15 m, of its place and 3 m of its height, its eye's line to the box clear, a way to it
// at most half as long again and 3 m), with no enemy in its sight and the rule's item not on it
void PickupFrame(Body& b, const Self& s) {
    if (!gPickup || !Ready()) { b.pickNum = -1; return; }   // (its take's emptied record: Alive's SendOwed)
    // the game's own rule first: a player standing in a box's pick-up zone takes it, in a fight or not, gone to it or passing
    // by (his game's pick-up runs each frame); the doll the same, of what it takes
    if (b.takeSecond.empty() && b.fallAt < 0 && gFrame >= b.climbUntil)
        for (const DropNow& x : gDrops)
            if (Lying(x) && Wants(b, s.k, SpecialKind(x)) && !TakenNow(x.number) && InZone(s.pos, x) && StillLying(x)) { Take(b, s, x); return; }   // (review: read again)
    const bool busy = b.target >= 0 || gFrame - b.hurtAt < 120 || b.carrying || b.holding || b.throwAt >= 0 || b.grabStart >= 0 ||
                      gFrame < b.rollUntil || gFrame < b.rushUntil || b.fallAt >= 0 || gFrame < b.climbUntil || gFrame < b.landUntil || RifleHolds(b) ||
                      SupBusy(s.k);                    // (v150-host beta 22, support: going to take a claymore, to lay)
    if (b.pickNum >= 0) {
        const DropNow* d = nullptr;
        for (const DropNow& x : gDrops) if (x.number == b.pickNum) d = &x;
        const bool gone = !d || !Lying(*d) || TakenNow(b.pickNum) || !Wants(b, s.k, SpecialKind(*d));
        if (busy || gone || gFrame - b.pickSince > 900) {
            if (gDebug || !gone) BLOG("vdoll %d: leaves the %s (record %#x): %s (%.0f mm from it along x, %.0f along z, %.0f over it)", s.k + 1, d && SpecialKind(*d) >= 0 ? kSpecialName[SpecialKind(*d)] : "special item",
                                      (unsigned)b.pickNum, gone ? "taken or gone" : busy ? (b.target >= 0 ? "an enemy in its sight" : "busy") :
                                      b.pickSince < 0 ? "no way to it from where it stands" : "not reached in 15 s",   // (v150-host beta 22, review: the two apart)
                                      d ? std::fabs(s.pos[0] - d->pos[0]) : -1.f, d ? std::fabs(s.pos[2] - d->pos[2]) : -1.f, d ? s.pos[1] - d->pos[1] : 0.f);
            gDropLeft.push_back({(long long)s.k << 32 | (unsigned)b.pickNum, gFrame + (gone ? 120 : 1800)});
            b.pickNum = -1; b.path.clear(); b.next = 0;
            return;
        }
        // (v150-host beta 22, review) an earlier take's emptied record first (two specials at one place: the second take within
        // its 6 frames lost the first one's, the box then lay on every player's screen for the rest of the round), the record read again
        if (InZone(s.pos, *d)) { if (b.takeSecond.empty() && StillLying(*d)) Take(b, s, *d); return; }
        if (b.next >= b.path.size()) {                 // (its way ended short of it: the last steps straight onto it, else a way again)
            const float feet[3] = {s.pos[0], s.pos[1] - kHips, s.pos[2]};
            const float at[3] = {d->pos[0], std::max(d->pos[1], feet[1] - 300.f), d->pos[2]};
            const float knee[2][3] = {{feet[0], feet[1] + 500.f, feet[2]}, {at[0], at[1] + 500.f, at[2]}};
            if (Dist2D(feet, at) < 1500.f && std::fabs(at[1] - feet[1]) < 600.f && SightClear(knee[0], knee[1])) {
                b.path.clear(); b.path.push_back({feet[0], feet[1], feet[2]}); b.path.push_back({at[0], feet[1], at[2]}); b.next = 1;
            } else {
                std::vector<doll::Waypoint> w; float len = 0, from[3]; bool far = false;
                if (!StartPoint(s, from, &far) || !FindWay(from, d->pos, w, &len) || w.size() < 2 || !WayOk(w)) { b.pickSince = -100000; return; }   // (given up next pass)
                b.path.clear(); for (const auto& p : w) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link}); b.next = 1;
            }
            b.objWay = false; b.pauseUntil = 0;
        }
        return;
    }
    if (busy || !b.readNow || gDrops.empty()) return;   // ((v150-host beta 25, dpass) the read's own pass: Alive)
    float eye[3]; Eye(s, eye);
    const float feet[3] = {s.pos[0], s.pos[1] - kHips, s.pos[2]};
    const DropNow* best = nullptr; float bestLen = 1e30f; std::vector<doll::Waypoint> bestWay;
    for (const DropNow& x : gDrops) {
        const int kind = SpecialKind(x);
        if (!Lying(x) || !Wants(b, s.k, kind) || TakenNow(x.number) || LeftNow(s.k, x.number)) continue;
        const float dist = Dist2D(feet, x.pos);
        if (dist > gPickNear || std::fabs(x.pos[1] - feet[1]) > 3000.f) continue;
        const float box[3] = {x.pos[0], x.pos[1] + 300.f, x.pos[2]};
        if (!SightClear(eye, box)) continue;           // (it sees the box: its eye's line clear of the map and the game's things)
        std::vector<doll::Waypoint> w; float len = 0, from[3]; bool far = false;
        if (!StartPoint(s, from, &far) || !FindWay(from, x.pos, w, &len) || w.size() < 2 || len > dist * 1.5f + 3000.f || !WayOk(w)) continue;   // (a way it runs)
        if (Dist2D(w.back().p, x.pos) > 1500.f) continue;   // (the way's end too far from it for the last steps)
        if (len < bestLen) { bestLen = len; best = &x; bestWay = w; }
    }
    if (!best) return;
    b.pickNum = best->number; b.pickSince = gFrame; memcpy(b.pickPos, best->pos, sizeof b.pickPos);
    b.path.clear(); for (const auto& p : bestWay) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link}); b.next = 1;
    b.objWay = false; b.pauseUntil = 0;
    BLOG("vdoll %d: goes to take the %s lying %.1f m away (record %#x, a way of %.1f m)", s.k + 1, kSpecialName[SpecialKind(*best)], Dist2D(feet, best->pos) / 1000.f,
         (unsigned)best->number, bestLen / 1000.f);
}
// ---- (v150-host beta 25, food: docs/VIRTUAL_DOLL.md 44, vdoll_food.h) the food lying on the map: the stage script's mushrooms,
// each a food box at its point from the round's start (GOLOVA, the RUSSIAN GLOWCAP). A player standing in a box's pick-up zone
// takes it whatever his gauge and his own game eats it at once: his stun gauge gains its maximum times the food's share, and his
// game tells the host on the mushroom man's channel {the entry's number, its point in tenths, 3}; the host removes the box, passes
// the record on and makes the box again 20 s later. A virtual doll has no game of its own: it sends that record itself (vdoll.cpp
// BodySend kind 6) and vdoll.cpp adds the gain to its gauge (FoodEaten), which its state records carry as a player's do. It goes
// to a food it sees within MGO1_VDOLL_PICKUP_NEAR when it has lost at least half of what the food gives back and no enemy is in
// its sight; it takes one whose zone it stands in, as the game does ----
int gFoodOn = 1;                                 // MGO1_VDOLL_FOOD: 1 the dolls eat the food as a player's game does; 0 never (beta 24)
std::vector<FoodNow> gFoods;
long long gFoodsAt = -1000;
std::vector<std::pair<int, long long>> gFoodTaken;   // (a food a doll took, by number: left by the others 2 s, the host's entry then says so)
std::vector<std::pair<long long, long long>> gFoodLeft;   // ((doll << 32 | number) a doll gave up: until then)
int gFoodEaten = 0;                              // (all dolls: the foods eaten)
bool FoodLying(const FoodNow& f) { return f.number >= 0x100 && f.state == 2 && f.box && f.gain > 0; }
bool FoodTakenNow(int number) { for (const auto& t : gFoodTaken) if (t.first == number && gFrame - t.second < 120) return true; return false; }
bool FoodLeftNow(int k, int number) {
    const long long key = (long long)k << 32 | (unsigned)number;
    for (const auto& t : gFoodLeft) if (t.first == key && gFrame < t.second) return true;
    return false;
}
// (the entry read again from the host's game at the take: a player's take that reached the host since the last look took it)
bool FoodStillLying(const FoodNow& x) {
    FoodNow d[16];
    const int n = gHost.foods ? std::max(0, std::min(16, gHost.foods(d, 16))) : 0;
    for (int i = 0; i < n; ++i)
        if (d[i].number == x.number) { if (FoodLying(d[i]) && d[i].code == x.code) return true; break; }
    gFoodsAt = -1000;
    return false;
}
void SeeFoods(long long frame) {
    if (!gFoodOn || !gHost.foods || frame - gFoodsAt < 30) return;
    gFoodsAt = frame;
    FoodNow d[16];
    const int n = gHost.foods(d, 16);
    gFoods.assign(d, d + std::max(0, std::min(16, n)));
    static std::string was;                          // (the food lying now, said at each change)
    std::string now, key;
    char t[112];
    for (const FoodNow& f : gFoods)
        if (FoodLying(f)) {
            snprintf(t, sizeof t, "%s%s (%#x) at %.0f %.0f %.0f", now.empty() ? "" : ", ", food::Name(f.code), (unsigned)f.number, f.pos[0], f.pos[1], f.pos[2]);
            now += t; key += std::to_string(f.number) + ",";
        }
    if (key != was) { was = key; if (gDebug || !now.empty()) BLOG("vdoll: the food lying now: %s", now.empty() ? "none" : now.c_str()); }
    gFoodTaken.erase(std::remove_if(gFoodTaken.begin(), gFoodTaken.end(), [&](const std::pair<int, long long>& p) { return frame - p.second >= 120; }), gFoodTaken.end());
    gFoodLeft.erase(std::remove_if(gFoodLeft.begin(), gFoodLeft.end(), [&](const std::pair<long long, long long>& p) { return frame >= p.second; }), gFoodLeft.end());
}
int GaugeOf(const Self& s) { return s.slot >= 0 && s.slot < 16 ? gDollOut[s.slot].stamina : -1; }   // (vdoll.cpp's Knocked each frame)
// its take, as a player's game takes it: the record on the mushroom man's channel; the gain for vdoll.cpp to add to its gauge
void EatFood(Body& b, const Self& s, const FoodNow& f) {
    s.send(s.ctx, s.slot, 6, food::TakenRecord(f.number, f.at));
    gFoodTaken.push_back({f.number, gFrame});
    const int gauge = GaugeOf(s);
    b.foodGain += f.gain; ++b.eats; b.foodGains += f.gain; ++gFoodEaten;
    BLOG("vdoll %d: eats the %s at %.0f %.0f %.0f (food %#x), its stun gauge %d -> %d (+%d, the game's share of 30000): its record {%#x, its point, 3} on the mushrooms' channel, as a player's game sends it",
         s.k + 1, food::Name(f.code), f.pos[0], f.pos[1], f.pos[2], (unsigned)f.number, gauge, food::After(gauge, f.gain), f.gain, (unsigned)(f.number & 0xFF));
    if (b.foodNum == f.number) { b.foodNum = -1; b.path.clear(); b.next = 0; b.pauseUntil = gFrame + 6; }
}
// each frame alive (after the special items): a food box whose zone it stands in taken, as the game takes it; the food it goes to
// (given up when an enemy comes in its sight, it is busy, the food is gone or 15 s pass), else every 100 ms the nearest food it
// sees lying near (within MGO1_VDOLL_PICKUP_NEAR, 3 m of its height, its eye's line to the box clear, a way to it at most half as
// long again and 3 m) whose eating would give back at least half of its gain, with no enemy in its sight
void FoodFrame(Body& b, const Self& s) {
    if (!gFoodOn || !Ready() || !gHost.foods) { b.foodNum = -1; return; }
    if (b.fallAt < 0 && gFrame >= b.climbUntil)
        for (const FoodNow& f : gFoods)
            if (FoodLying(f) && !FoodTakenNow(f.number) && food::InZone(s.pos, f.pos, f.reach) && FoodStillLying(f)) { EatFood(b, s, f); return; }
    const int gauge = GaugeOf(s);
    const bool busy = b.target >= 0 || gFrame - b.foeAt < 180 || gFrame - b.hurtAt < 120 || b.carrying || b.holding || b.throwAt >= 0 || b.grabStart >= 0 ||
                      gFrame < b.rollUntil || gFrame < b.rushUntil || RifleHolds(b) || SupBusy(s.k) || b.pickNum >= 0 || b.wakeTo >= 0 || b.bookId;
    const bool moving = b.fallAt >= 0 || gFrame < b.climbUntil || gFrame < b.landUntil;   // (a drop or a climb on its way: on, not begun in one)
    if (b.foodNum >= 0) {
        const FoodNow* d = nullptr;
        for (const FoodNow& x : gFoods) if (x.number == b.foodNum) d = &x;
        const bool gone = !d || !FoodLying(*d) || FoodTakenNow(b.foodNum);
        if (busy || gone || gFrame - b.foodSince > 900) {
            if (gDebug || !gone) BLOG("vdoll %d: leaves the %s (food %#x): %s", s.k + 1, d ? food::Name(d->code) : "food", (unsigned)b.foodNum,
                                      gone ? "taken or gone" : busy ? (b.target >= 0 || gFrame - b.foeAt < 180 ? "an enemy in its sight" : b.pickNum >= 0 ? "a special item first" : "busy") :
                                      b.foodSince < 0 ? "no way to it from where it stands" : "not reached in 15 s");
            gFoodLeft.push_back({(long long)s.k << 32 | (unsigned)b.foodNum, gFrame + (gone ? 120 : 1800)});
            // ((v150-host beta 25, review25) its way left to the move it leaves the food for (a special item, a teammate to wake, the
            // support's run): cleared, the wake read it as its way's end short of a reader within 2 m and stopped, "no way to him")
            if (b.pickNum < 0 && b.wakeTo < 0 && !SupBusy(s.k)) { b.path.clear(); b.next = 0; }
            b.foodNum = -1;
            return;
        }
        if (b.next >= b.path.size() && !moving) {      // (its way ended short of it: the last steps straight onto it, else a way again)
            const float feet[3] = {s.pos[0], s.pos[1] - kHips, s.pos[2]};
            const float at[3] = {d->pos[0], std::max(d->pos[1], feet[1] - 300.f), d->pos[2]};
            const float knee[2][3] = {{feet[0], feet[1] + 500.f, feet[2]}, {at[0], at[1] + 500.f, at[2]}};
            if (Dist2D(feet, at) < 1500.f && std::fabs(at[1] - feet[1]) < 600.f && SightClear(knee[0], knee[1])) {
                b.path.clear(); b.path.push_back({feet[0], feet[1], feet[2]}); b.path.push_back({at[0], feet[1], at[2]}); b.next = 1;
            } else {
                std::vector<doll::Waypoint> w; float len = 0, from[3]; bool far = false;
                if (!StartPoint(s, from, &far) || !FindWay(from, d->pos, w, &len) || w.size() < 2 || !WayOk(w)) { b.foodSince = -100000; return; }   // (given up next pass)
                b.path.clear(); for (const auto& p : w) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link}); b.next = 1;
            }
            b.objWay = false; b.pauseUntil = 0;
        }
        return;
    }
    if (busy || moving || !b.readNow || gFoods.empty() || gauge < 0) return;   // ((v150-host beta 25, dpass) the read's own pass: Alive)
    float eye[3]; Eye(s, eye);
    const float feet[3] = {s.pos[0], s.pos[1] - kHips, s.pos[2]};
    const FoodNow* best = nullptr; float bestLen = 1e30f; std::vector<doll::Waypoint> bestWay;
    for (const FoodNow& x : gFoods) {
        if (!FoodLying(x) || !food::Wants(gauge, x.gain) || FoodTakenNow(x.number) || FoodLeftNow(s.k, x.number)) continue;
        const float dist = Dist2D(feet, x.pos);
        if (dist > gPickNear || std::fabs(x.pos[1] - feet[1]) > 3000.f) continue;
        const float box[3] = {x.pos[0], x.pos[1] + 300.f, x.pos[2]};
        if (!SightClear(eye, box)) continue;           // (it sees the box)
        std::vector<doll::Waypoint> w; float len = 0, from[3]; bool far = false;
        if (!StartPoint(s, from, &far) || !FindWay(from, x.pos, w, &len) || w.size() < 2 || len > dist * 1.5f + 3000.f || !WayOk(w)) continue;   // (a way it runs)
        if (Dist2D(w.back().p, x.pos) > 1500.f) continue;
        if (len < bestLen) { bestLen = len; best = &x; bestWay = w; }
    }
    if (!best) return;
    b.foodNum = best->number; b.foodSince = gFrame;
    b.path.clear(); for (const auto& p : bestWay) b.path.push_back({p.p[0], p.p[1], p.p[2], p.area, p.link}); b.next = 1;
    b.objWay = false; b.pauseUntil = 0;
    BLOG("vdoll %d: goes to eat the %s lying %.1f m away (food %#x, a way of %.1f m): its stun gauge %d, the food gives back %d", s.k + 1, food::Name(best->code),
         Dist2D(feet, best->pos) / 1000.f, (unsigned)best->number, bestLen / 1000.f, gauge, best->gain);
}

// (v150-host beta 22, pickup) the thermal goggles had: on (the item 7 in its records, as a player's game names the item worn)
// when an enemy was in its sight within 30 m these 10 s, or always while it defends in TEAM SNEAKING or SNEAKING (the camouflaged
// sneakers, Snake),
// their battery (the stealth camouflage's: 300 full, 5 a second on) over 90; off under 20, or after 15 s with nothing to see
// (10 s on at least). On: a camouflaged player is seen as any other (Camouflaged)
void Goggles(Body& b, const Self& s, int dt) {
    if (!b.goggles) { b.gogglesOn = false; return; }
    if (b.target >= 0 && gHave[b.target] && Dist2D(s.pos, gSeen[b.target].pos) < 30000.f) b.gogglesSeen = gFrame;
    const bool defend = (gRule == 5 && SneakSide() >= 0 && SneakSide() != s.team) || (gRule == 4 && b.role == R_SOLDIER);   // (Snake's camouflage, the sneakers')
    const bool want = defend || gFrame - b.gogglesSeen < 600;
    if (b.gogglesOn) {
        b.battery = std::max(0.f, b.battery - 5.f * dt / 59.94f);
        gPick.gogglesF += dt;
        if (b.battery < 20.f || (!want && gFrame - b.gogglesAt > 600 && gFrame - b.gogglesSeen > 900)) {
            b.gogglesOn = false; b.gogglesAt = gFrame;
            BLOG("vdoll %d: takes its thermal goggles off (battery %.0f)", s.k + 1, b.battery);
        }
    } else {
        b.battery = std::min(300.f, b.battery + kGogglesBack * dt / 59.94f);
        if (want && b.battery > 90.f) {
            b.gogglesOn = true; b.gogglesAt = gFrame;
            BLOG("vdoll %d: puts its thermal goggles on (battery %.0f)", s.k + 1, b.battery);
        }
    }
}

// ---- (v150-host beta 22, support: docs/VIRTUAL_DOLL.md 35, the rules in vdoll_support.h) the claymore and the book of the
// support row. A doll takes an enemy's claymore it saw by lying down out of its sensor and crawling onto it (its game's take:
// lying, its hips within 500 mm; the sensor quiet for a body lying), with the record a player's game sends (the list's state 4,
// Self::send kind 4); it lays its claymores and books (taken, or the support item drawn at its spawn as a player chooses one)
// at hidden places with a player's records (crouched, {08 12}, {08 13}, then the list's state 1); its claymore that goes off
// wounds as the layer's own game's blast (Laid). Its ways keep off its team's claymores and books and the enemies' it saw ----
int gSupport = 1, gSupTake = 1, gSupLay = 1;     // MGO1_VDOLL_SUPPORT (0: beta 22 exactly), _TAKE, _LAY
int gSupItem = 0;                                // MGO1_VDOLL_SUPPORT_ITEM: -1 off, 0 auto (the draw), 1 claymore, 2 book (benches)
int gClayMost = 3, gClaySpawn = 3, gBookSpawn = 3;   // (the counts a player has: 3 claymores, 3 at most (sup_p1, probe 49: the row 3 and 3); 3 books (book_r4: 3 laid))
std::vector<ClayNow> gClays;                     // the host's claymores' list now (every 6 frames, and at a take)
std::vector<uint32_t> gFreshClays;               // (the claymores new in the list this read: seen laid when in a doll's sight)
long long gClaysAt = -1000;
struct Boom { uint32_t id; float at[3]; long long frame; };
std::vector<Boom> gBooms;                        // the host's copies of a doll's claymores gone off, until its doll wounds
int gClaySeq[16] = {}, gBookSeq[16] = {};        // (a doll's own ids: its slot << 10 | 0x100 + n, as a player's game numbers)
// (the support item's draw has its own chance: the rest of a doll's chance, its ways and its fights, draws what it drew before)
unsigned gSupSeed = 0x2545F491u;
unsigned SupRnd() { gSupSeed ^= gSupSeed << 13; gSupSeed ^= gSupSeed >> 17; gSupSeed ^= gSupSeed << 5; return gSupSeed; }
struct Sup {
    long long life = -2;                          // (its life's frame: a new life resets the life's part)
    int item = 0, clays = 0, books = 0;           // this life's item (vsup::Item), the claymores and books it carries
    int state = 0;                                // 0 none, 1 going to take, 2 lying down, 3 crawling, 4 getting up, 5 going to lay, 6 laying
    long long stateAt = -1, nextCheck = 0, nextLayAt = 0;
    uint32_t clay = 0;                            // the claymore it goes to take (its id)
    float clayAt[3] = {0, 0, 0}, near[3] = {0, 0, 0};   // its place; where it lies down
    bool book = false;                            // laying a book (else a claymore)
    float lay[3] = {0, 0, 0}, stand[3] = {0, 0, 0}, layYaw = 0;   // the place, where it stands, its facing
    bool layRecSent = false, moved = false;
    std::vector<uint32_t> known;                  // the enemies' claymores it saw (laid or lying), by id
    // its own claymores lying (their ids, the frame each was laid: (v150-host beta 23, review) the host's game lists one a read and
    // its act after the doll sends it, 4 to 6 frames in the benches, and the list read in that time had dropped it: its three at most
    // were not kept, 7 lying at once in tests/vdoll_support_test.cpp part 10)
    std::vector<std::pair<uint32_t, long long>> mine;
    std::vector<std::array<float, 3>> avoid;      // where its ways do not go now (Avoid)
    std::vector<std::pair<std::array<float, 3>, long long>> bad;   // places it found no way to, left until then
    int taken = 0, laidClays = 0, laidBooks = 0, blasts = 0, blastHits = 0, gaveUp = 0;   // (its counts over its lives)
    long long pauseFrom = -1, pauseLast = -1;     // ((v150-host beta 23, review) lying, paused by vdoll.cpp: since when, the last pause)
    int crawlF = 0;                               // ((v150-host beta 23, review) its crawl's own frames (a reading or a flinch in it not counted)
};
std::vector<Sup> gSups;
const long long kListLate = 120;                 // ((v150-host beta 23, review) a claymore it laid not in the host's list yet: 2 s at most)
Sup& SupOf(int k) { if ((int)gSups.size() <= k) gSups.resize(k + 1); return gSups[k]; }
// the places of each side (red, blue, the deathmatch's) on this map and rule, found a few at a time (vsup::StepPlaces)
vsup::Places gPlaces[3];
vsup::Eyes gEyes;
vsup::Cover gCoverObj;
std::string gPlacesKey;
bool gPlacesWanted[3] = {false, false, false};
bool SupOn() { return gSupport && gOn; }
bool Prone(int k) { return k >= 0 && k < (int)gSups.size() && gSups[k].state >= 2 && gSups[k].state <= 4; }
const ClayNow* ClayById(uint32_t id) { for (const ClayNow& c : gClays) if (c.id == id) return &c; return nullptr; }
int SideOf(int team) { return gRule == 0 ? 2 : team == 0 || team == 1 ? team : -1; }
// a claymore's layer is its teammate (or itself): its team's, never taken; an enemy's: deathmatch, or the other team
bool ClayOfTeam(const ClayNow& c, int slot, int team) {
    if (c.slot == slot) return true;
    if (gRule == 0) return false;
    if (c.slot < 0 || c.slot >= 16 || !gHave[c.slot]) return true;   // (its layer not known now: left alone)
    return gSeen[c.slot].team == team;
}
void SeeClaymores(long long frame) {
    if (!SupOn() || !gHost.claymores || frame == gClaysAt) return;
    gClaysAt = frame;
    ClayNow l[16];
    const int n = std::max(0, std::min(16, gHost.claymores(l, 16)));
    static std::string was;
    std::string key;
    for (int i = 0; i < n; ++i) key += std::to_string(l[i].id) + ",";
    if (key != was && (gDebug || n)) {
        std::string t;
        char w[120];
        for (int i = 0; i < n; ++i) { snprintf(w, sizeof w, "%s%#x of player %d at %.0f %.0f %.0f", t.empty() ? "" : ", ", (unsigned)l[i].id, l[i].slot + 1, l[i].pos[0], l[i].pos[1], l[i].pos[2]); t += w; }
        BLOG("vdoll: the claymores lying now: %s", n ? t.c_str() : "none");
    }
    was = key;
    gFreshClays.clear();
    for (int i = 0; i < n; ++i) { bool had = false; for (const ClayNow& c : gClays) if (c.id == l[i].id) had = true; if (!had) gFreshClays.push_back(l[i].id); }
    gClays.assign(l, l + n);
    for (Sup& u : gSups) {                             // (gone from the list: forgotten; its own: gone off or taken)
        u.known.erase(std::remove_if(u.known.begin(), u.known.end(), [](uint32_t id) { return !ClayById(id); }), u.known.end());
        u.mine.erase(std::remove_if(u.mine.begin(), u.mine.end(), [frame](const std::pair<uint32_t, long long>& m) { return !ClayById(m.first) && frame - m.second > kListLate; }),
                     u.mine.end());
    }
}
// the doll sees a claymore: laid (it appears) in a clear line within 25 m in its field, or lying in a clear line within 10 m in
// its field (4 m whatever its facing), its eye to the claymore's top. Only the enemies' are kept
void SupSee(Body& b, const Self& s, Sup& u) {
    float eye[3]; Eye(s, eye);
    const Level L = RowOf(b, false);
    for (const ClayNow& c : gClays) {
        if (ClayOfTeam(c, s.slot, s.team) || std::find(u.known.begin(), u.known.end(), c.id) != u.known.end()) continue;
        const float top[3] = {c.pos[0], c.pos[1] + vsup::kClayTop, c.pos[2]};
        const float d = Dist3(eye, top);
        const float off = std::fabs(Wrap(Bearing(s.pos, c.pos) - b.yaw)) * 180.f / kPi;
        const bool laid = std::find(gFreshClays.begin(), gFreshClays.end(), c.id) != gFreshClays.end() && d <= vsup::kSeeLaid;
        if (!(laid || d <= vsup::kSeeLying) || (d > 4000.f && off > std::min(L.field / 2.f, vsup::kSeeField * 180.f / kPi)) || !SightClear(eye, top)) continue;
        u.known.push_back(c.id);
        BLOG("vdoll %d: sees a claymore of player %d %s at %.0f %.0f %.0f (%.1f m)", s.k + 1, c.slot + 1, laid ? "laid" : "lying", c.pos[0], c.pos[1], c.pos[2], d / 1000.f);
    }
}
// where its ways do not go: its team's claymores and books (they go off and are read for a teammate too), the enemies' claymores
// it saw (but the one it goes to take: its crawl goes there)
void Avoid(const Self& s, Sup& u) {
    u.avoid.clear();
    for (const ClayNow& c : gClays)
        if ((ClayOfTeam(c, s.slot, s.team) || std::find(u.known.begin(), u.known.end(), c.id) != u.known.end()) && !(u.state >= 1 && u.state <= 4 && c.id == u.clay))
            u.avoid.push_back({c.pos[0], c.pos[1], c.pos[2]});
    for (int i = 0; i < gBookN; ++i) {
        const int sl = gBookList[i].slot;
        if (sl == s.slot || (gRule != 0 && sl >= 0 && sl < 16 && gHave[sl] && gSeen[sl].team == s.team)) u.avoid.push_back({gBookList[i].pos[0], gBookList[i].pos[1], gBookList[i].pos[2]});
    }
}
// this life's item: the support row as a player chooses it (the draw, or the bench's), and what comes with it: the game's count
// of the claymore or the book, and no grenade that life (a player has one support item)
void SupLife(Body& b, const Self& s, Sup& u) {
    if (u.life == s.life) return;
    // (the claymores it saw stay known across its lives, as a player remembers where he saw one; what it carried is gone)
    u.life = s.life; u.state = 0; u.clay = 0; u.clays = u.books = 0; u.item = vsup::ITEM_GRENADE; u.nextLayAt = gFrame + 600;
    u.moved = false; u.pauseFrom = u.pauseLast = -1;   // ((v150-host beta 23, review) nothing of the last life's lying)
    if (!SupOn() || !(Allowed() & 4) || b.unarmed) return;
    const bool hs = gHost.headshotsOnly && gHost.headshotsOnly();
    if (gSupItem == 0) u.item = vsup::Draw(SupRnd(), hs, b.role != R_SOLDIER);
    else if (gSupItem > 0 && !hs && b.role == R_SOLDIER) u.item = gSupItem;
    // (drawn: its side's hidden places looked for from now; a side with none on this map (found and done): the grenade, as a player
    // would not bring what he has no place for)
    const int side = SideOf(s.team);
    if (side >= 0 && gSupLay) gPlacesWanted[side] = true;
    if (gSupItem == 0 && u.item != vsup::ITEM_GRENADE && side >= 0 && gPlaces[side].done && gPlaces[side].found.empty()) u.item = vsup::ITEM_GRENADE;
    if (u.item == vsup::ITEM_CLAYMORE) { u.clays = gClaySpawn; b.grenades = 0; }
    else if (u.item == vsup::ITEM_BOOK) { u.books = gBookSpawn; b.grenades = 0; }
    if (u.item != vsup::ITEM_GRENADE) BLOG("vdoll %d: its support item this life: %d %s (no grenade)", s.k + 1, u.item == vsup::ITEM_CLAYMORE ? u.clays : u.books,
                                           u.item == vsup::ITEM_CLAYMORE ? "claymores" : "books");
}
// the places of a side: started on this map and rule from the rule's spawn points (the map's own kinds), then a few a frame
void StepSidePlaces() {
    if (!Ready() || !gMap->geom) return;
    const std::string key = gMap->key + "/" + std::to_string(gRule);
    if (key != gPlacesKey) {
        gPlacesKey = key;
        for (auto& p : gPlaces) p = vsup::Places();
        vsup::MakeEyes(*gMap->nav, gEyes);
        vsup::MakeCover(*gMap->geom, gCoverObj);
    }
    for (int side = 0; side < 3; ++side) {
        if (!gPlacesWanted[side] || gPlaces[side].done) continue;
        if (!gPlaces[side].started) {
            std::vector<std::array<float, 3>> own, foe;
            if (side == 2) vsup::SideSpawns(*gMap->geom, 0, 0, foe);
            else { vsup::SideSpawns(*gMap->geom, gRule, side, own); vsup::SideSpawns(*gMap->geom, gRule, 1 - side, foe); }
            vsup::StartPlaces(gPlaces[side], own, foe, side == 2);
        }
        auto way = [](const float* a, const float* c, std::vector<doll::Waypoint>& w) { float len = 0; return FindWay(a, c, w, &len) && WayOk(w, true); };
        if (vsup::StepPlaces(gPlaces[side], *gMap->nav, gEyes, gCoverObj, 4, way, [](const float* a, const float* c) { return LineClear(a, c); }))
            BLOG("vdoll: the hidden places of the %s on %s: %zu found of %d tried (seen from 15 m and more by one eye at most, the enemies' way in seeing them from 12 m at most)",
                 side == 2 ? "deathmatch" : side ? "blue team" : "red team", gMap->key.c_str(), gPlaces[side].found.size(), gPlaces[side].tried);
        break;                                         // (one side a frame)
    }
}
// the record of the claymores' or books' list, from its own session (kind 4: the claymores' channel, 5: the books')
void SendList(const Self& s, bool book, const std::vector<uint8_t>& r) { s.send(s.ctx, s.slot, book ? 5 : 4, r); }
// an enemy in its sight from where it lies (the floor + 0.4 m), or a hurt: its crawl given up (it gets up and fights)
bool ProneThreat(const Body& b, const Self& s) {
    if (gFrame - b.hurtAt < 60) return true;
    const float eye[3] = {s.pos[0], s.pos[1] - vsup::kHipsLying + 400.f, s.pos[2]};
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], s.team)) continue;
        if (gTestTarget > 0 && id + 1 != gTestTarget) continue;
        float c[3]; Chest(gSeen[id], c);
        if (Dist3(eye, c) < 20000.f && SightClear(eye, c)) return true;
    }
    return false;
}
// it lies, crawls, gets up, lays: its own frame (true), with its place on its own floor; its moves, style and records the
// Records' support part
bool SupportFrame(Body& b, const Self& s, Sup& u, int dt) {
    if (!SupOn()) return false;
    const long long t = gFrame - u.stateAt;
    const float feet[3] = {s.pos[0], s.pos[1] - (Prone(s.k) && u.moved ? vsup::kHipsLying : kHips), s.pos[2]};
    auto setState = [&](int st) { u.state = st; u.stateAt = gFrame; u.crawlF = 0; };
    auto upAgain = [&](const char* why) {
        if (u.state == 2 || u.state == 3) { setState(4); if (why) BLOG("vdoll %d: gets up (%s)", s.k + 1, why); }
        else { setState(0); b.path.clear(); b.next = 0; }
    };
    switch (u.state) {
    case 1: {                                          // to where it lies down, on its way
        const ClayNow* c = ClayById(u.clay);
        if (!c || b.target >= 0 || gFrame - b.hurtAt < 120 || t > 1200) {
            if (!c || t > 1200) ++u.gaveUp;
            BLOG("vdoll %d: leaves the claymore of player %d (%s)", s.k + 1, (int)(u.clay >> 10) + 1, !c ? "gone" : b.target >= 0 ? "an enemy in its sight" : gFrame - b.hurtAt < 120 ? "hurt" : "not reached in 20 s");
            setState(0); b.path.clear(); b.next = 0; return false;
        }
        if (Dist2D(feet, u.near) < 400.f && std::fabs(feet[1] - u.near[1]) < 800.f) {
            b.path.clear(); b.next = 0; b.speed = 0;
            setState(2); u.moved = false;
            StandUp(b); b.aiming = false; b.target = -1;
            BLOG("vdoll %d: lies down %.1f m from the claymore of player %d, out of its sensor", s.k + 1, Dist2D(feet, c->pos) / 1000.f, c->slot + 1);
            return true;
        }
        if (b.next >= b.path.size()) {
            const float to[3] = {u.near[0], u.near[1] + kHips, u.near[2]};
            if (!PlanTo(b, s, to)) { setState(0); ++u.gaveUp; return false; }
        }
        return false;                                  // (Run along its way: the frame's own)
    }
    case 2: {                                          // down: crouched, then lying (its hips 300 mm over its floor)
        b.speed = 0; b.target = -1; b.aiming = false;
        if (t >= vsup::kLayCrouch && !u.moved) { s.pos[1] = b.floorY + vsup::kHipsLying; u.moved = true; }
        if (u.moved && t >= vsup::kLayCrouch + 6 && ProneThreat(b, s)) { upAgain("an enemy in its sight"); return true; }
        const bool copyLying = gHave[s.slot] && gSeen[s.slot].object && (gSeen[s.slot].state & vsup::kLying);
        if (copyLying && t >= vsup::kDownFrames) { setState(3); return true; }
        if (t > vsup::kDownFrames + 90) upAgain("its copy not lying");
        TurnBody(b, Bearing(s.pos, u.clayAt), dt);
        return true;
    }
    case 3: {                                          // crawling onto it, straight
        b.speed = 0;
        const ClayNow* c = ClayById(u.clay);
        if (!c) { upAgain("the claymore gone"); return true; }
        // ((v150-host beta 23, review) in its sensor's reach a player who gets up sets it off with his own game (sup_p1b): there an
        // enemy in its sight or a hurt does not stand it up, it crawls on to the take (1.1 m at most, about 1 s); a crawl that has not
        // got there in 10 s backs out of the reach first. The bench F22_specials f17127: a doll got up 1.2 m from the claymore it
        // crawled to, "an enemy in its sight", its copy set it off 42 frames later and it died by it)
        const bool inReach = Dist2D(s.pos, c->pos) < vsup::kKeepOff;
        if (!inReach && (gFrame - u.stateAt) % 6 == 0 && ProneThreat(b, s)) { upAgain("an enemy in its sight"); return true; }
        const bool copyLying = gHave[s.slot] && gSeen[s.slot].object && (gSeen[s.slot].state & vsup::kLying);
        // the game's take (0x3A8BD8): lying, its hips within 500 mm on the ground and in height; its claymores not at their most
        if (vsup::CanTake(s.pos, copyLying ? vsup::kLying : 0u, c->pos, u.clays, gClayMost)) {
            const std::vector<uint8_t> r = vsup::Record(c->id, vsup::kStateTaken, c->yaw, c->pos, c->word);
            SendList(s, false, r);
            ++u.clays; ++u.taken;
            BLOG("vdoll %d: takes the claymore of player %d at %.0f %.0f %.0f, crawling (its hips %.0f mm from it): the list's state 4, as a player's game sends it (%d claymores now)",
                 s.k + 1, c->slot + 1, c->pos[0], c->pos[1], c->pos[2], Dist2D(s.pos, c->pos), u.clays);
            gClays.erase(std::remove_if(gClays.begin(), gClays.end(), [&](const ClayNow& x) { return x.id == u.clay; }), gClays.end());
            u.known.erase(std::remove(u.known.begin(), u.known.end(), u.clay), u.known.end());
            setState(4);
            return true;
        }
        if (u.clays >= gClayMost) { upAgain("its claymores at their most"); return true; }
        // never in its sensor's reach unless its copy lies (the sensor reads the host's copy); ((beta 23, review) 10 s at most: it waited
        // there with no end)
        u.crawlF += dt;                                // ((beta 23, review) its own frames: a book read on the way (8 s) or a flinch is no crawl)
        if (!copyLying && inReach) { if (u.crawlF > 600) upAgain("its copy not lying"); return true; }
        float want = Bearing(s.pos, c->pos);
        if (u.crawlF > 600) {
            if (!inReach || u.crawlF > 900) { upAgain("not reached in 10 s"); return true; }
            want = Bearing(c->pos, s.pos);             // ((beta 23, review) backing out of the reach, then up)
            const float x = s.pos[0] + std::sin(want) * vsup::kCrawl * dt, z = s.pos[2] + std::cos(want) * vsup::kCrawl * dt;
            const float g = FloorNear(x, z, b.floorY);
            if (g > -1e29f && std::fabs(g - b.floorY) < 300.f) { s.pos[0] = x; s.pos[2] = z; b.floorY = g; s.pos[1] = g + vsup::kHipsLying; b.speed = vsup::kCrawl; }
            return true;
        }
        TurnBody(b, want, dt);
        const float d = Dist2D(s.pos, c->pos), step = std::min(d, vsup::kCrawl * dt);
        float x = s.pos[0] + std::sin(want) * step, z = s.pos[2] + std::cos(want) * step;
        const float g = FloorNear(x, z, b.floorY);
        if (g > -1e29f && std::fabs(g - b.floorY) < 300.f) { s.pos[0] = x; s.pos[2] = z; b.floorY = g; s.pos[1] = g + vsup::kHipsLying; b.speed = step / std::max(1, dt); }
        return true;
    }
    case 4:                                            // up again: lying, crouched, on its feet (its hips back to 800 at once)
        b.speed = 0;
        if (s.pos[1] < b.floorY + kHips - 1.f) s.pos[1] = b.floorY + kHips;
        if (t >= vsup::kUpFrames) { setState(0); b.path.clear(); b.next = 0; b.standUntil = gFrame + 6; u.moved = false; b.resync = false; }
        return true;
    case 5: {                                          // to the place's stand, on its way; then turned to the enemies' way in
        if (b.target >= 0 || gFrame - b.hurtAt < 120 || t > 1800) {
            BLOG("vdoll %d: leaves its laying place (%s)", s.k + 1, b.target >= 0 ? "an enemy in its sight" : t > 1800 ? "not reached in 30 s" : "hurt");
            if (t > 1800) u.bad.push_back({{u.lay[0], u.lay[1], u.lay[2]}, gFrame + 3600});
            setState(0); u.nextLayAt = gFrame + 600; return false;
        }
        if (Dist2D(feet, u.stand) < 350.f && std::fabs(feet[1] - u.stand[1]) < 800.f) {
            b.path.clear(); b.next = 0; b.speed = 0;
            if (std::fabs(Wrap(u.layYaw - b.yaw)) * 180.f / kPi > 8.f) { TurnBody(b, u.layYaw, dt); return true; }
            setState(6); u.layRecSent = false;
            return true;
        }
        if (b.next >= b.path.size()) {
            const float to[3] = {u.stand[0], u.stand[1] + kHips, u.stand[2]};
            if (!PlanTo(b, s, to)) { setState(0); u.nextLayAt = gFrame + 60; u.bad.push_back({{u.lay[0], u.lay[1], u.lay[2]}, gFrame + 3600}); return false; }
        }
        return false;
    }
    case 6: {                                          // laying: crouched, Square held {08 12}, the motion {08 13}, the record at +60
        b.speed = 0; b.target = -1; b.aiming = false;
        const long long at = vsup::kLayCrouch + vsup::kLayHold + vsup::kLayRecordAt;
        if (!u.layRecSent && t >= at) {
            u.layRecSent = true;
            float p[3] = {s.pos[0] + std::sin(b.yaw) * vsup::kLayAhead, b.floorY, s.pos[2] + std::cos(b.yaw) * vsup::kLayAhead};
            const float g = FloorNear(p[0], p[2], b.floorY);
            if (g > -1e29f) p[1] = g;
            const uint32_t id = u.book ? vsup::NewId(s.slot, gBookSeq[s.slot]++) : vsup::NewId(s.slot, gClaySeq[s.slot]++);
            SendList(s, u.book, vsup::Record(id, vsup::kStateLying, b.yaw, p));
            if (u.book) { --u.books; ++u.laidBooks; } else { --u.clays; ++u.laidClays; u.mine.push_back({id, gFrame}); }
            BLOG("vdoll %d: lays a %s at %.0f %.0f %.0f facing %.0f degrees (id %#x; %d left): its list's state 1, as a player's game sends it", s.k + 1, u.book ? "book" : "claymore",
                 p[0], p[1], p[2], b.yaw * 180.f / kPi, (unsigned)id, u.book ? u.books : u.clays);
        }
        if (t >= vsup::kLayCrouch + vsup::kLayHold + vsup::kLayMotion) { setState(0); b.standUntil = gFrame + 18; u.nextLayAt = gFrame + 1200; }
        return true;
    }
    default: break;
    }
    return false;
}
// out of a fight, every half second: an enemy's claymore it remembers to take (within 30 m, a way to a place 2.5 m short of it with
// a straight crawl from there), else a place to lay what it carries (its side's hidden places within 35 m, a way to its stand)
void SupportChoose(Body& b, const Self& s, Sup& u) {
    if (u.state || gFrame < u.nextCheck) return;
    const bool nearOne = [&]() { for (uint32_t id : u.known) { const ClayNow* c = ClayById(id); if (c && Dist2D(c->pos, s.pos) < 8000.f) return true; } return false; }();
    u.nextCheck = gFrame + (nearOne ? 6 : 30);
    const bool busy = b.target >= 0 || gFrame - b.hurtAt < 180 || b.carrying || b.holding || b.throwAt >= 0 || b.grabStart >= 0 || gFrame < b.rollUntil ||
                      gFrame < b.rushUntil || b.fallAt >= 0 || gFrame < b.climbUntil || gFrame < b.landUntil || RifleHolds(b) || Blind(b) || b.bookId || b.pickNum >= 0 ||
                      !(Allowed() & 4) || b.unarmed || b.wakeTo >= 0;   // ((v150-host beta 23, wake) a teammate to wake first)
    if (busy) return;
    const float feet[3] = {s.pos[0], s.pos[1] - kHips, s.pos[2]};
    if (gSupTake && u.clays < gClayMost) {
        const ClayNow* best = nullptr; float bestLen = 1e30f, bestNear[3] = {0, 0, 0};
        for (uint32_t id : u.known) {
            const ClayNow* c = ClayById(id);
            if (!c || Dist2D(c->pos, feet) > 30000.f || std::fabs(c->pos[1] - feet[1]) > 3000.f) continue;
            // where it lies down: kProneAt short of it, toward itself first, else round it; the crawl straight on the mesh
            for (int k = 0; k < 8; ++k) {
                const float a = Bearing(c->pos, feet) + (k / 2) * (k & 1 ? 1.f : -1.f) * kPi / 4.f;
                const float n0[3] = {c->pos[0] + std::sin(a) * vsup::kProneAt, c->pos[1], c->pos[2] + std::cos(a) * vsup::kProneAt};
                float n[3];
                // (on a slope its floor lies lower or higher: up to 1.5 m over the 2.5 m, the crawl following its floor)
                if (!gMap->nav->Nearest(n0, 400.f, n) && !gMap->nav->Nearest(n0, 800.f, n)) continue;
                if (Dist2D(n, c->pos) < vsup::kProneAt - 300.f || std::fabs(n[1] - c->pos[1]) > 1500.f || !gMap->nav->Walkable(n, c->pos)) continue;
                std::vector<doll::Waypoint> w; float len = 0, from[3]; bool far = false;
                if (!StartPoint(s, from, &far) || !FindWay(from, n, w, &len) || w.size() < 2 || !WayOk(w)) continue;
                if (len < bestLen) { bestLen = len; best = c; memcpy(bestNear, n, sizeof bestNear); }
                break;
            }
        }
        if (best && bestLen < 45000.f) {
            u.clay = best->id; memcpy(u.clayAt, best->pos, sizeof u.clayAt); memcpy(u.near, bestNear, sizeof u.near);
            u.state = 1; u.stateAt = gFrame; b.path.clear(); b.next = 0; b.objWay = false;
            const float to[3] = {u.near[0], u.near[1] + kHips, u.near[2]};
            if (!PlanTo(b, s, to)) { u.state = 0; return; }
            BLOG("vdoll %d: goes to take the claymore of player %d it saw at %.0f %.0f %.0f (a way of %.1f m, to lie down %.1f m short of it)", s.k + 1, best->slot + 1,
                 best->pos[0], best->pos[1], best->pos[2], bestLen / 1000.f, vsup::kProneAt / 1000.f);
            return;
        }
    }
    if (!gSupLay || (u.clays <= 0 && u.books <= 0) || gFrame < u.nextLayAt) return;
    const bool hsOnly = gHost.headshotsOnly && gHost.headshotsOnly();   // (a head shots only game: a claymore's kill is none; its books only)
    if (hsOnly && u.books <= 0) return;
    if ((int)gClays.size() >= vsup::kListRoom && u.books <= 0) return;
    if ((int)u.mine.size() >= 3 && u.books <= 0) return;   // (three of its own lying at most)
    const int side = SideOf(s.team);
    if (side < 0) return;
    gPlacesWanted[side] = true;
    const vsup::Places& P = gPlaces[side];
    const vsup::Place* best = nullptr; float bestScore = -1e30f;
    u.bad.erase(std::remove_if(u.bad.begin(), u.bad.end(), [](const std::pair<std::array<float, 3>, long long>& x) { return gFrame >= x.second; }), u.bad.end());
    for (const vsup::Place& p : P.found) {
        if (Dist2D(p.at, feet) > 35000.f || std::fabs(p.at[1] - feet[1]) > 6000.f) continue;
        bool bad = false;
        for (const auto& x : u.bad) if (Dist2D(x.first.data(), p.at) < 1000.f) bad = true;
        if (bad) continue;
        bool taken = false;                            // (a claymore or a book lying within 4 m, a teammate standing within 3 m)
        for (const ClayNow& c : gClays) if (Dist2D(c.pos, p.at) < 4000.f) taken = true;
        for (int i = 0; i < gBookN; ++i) if (Dist2D(gBookList[i].pos, p.at) < 4000.f) taken = true;
        for (int id = 0; id < 16 && !taken; ++id)
            if (id != s.slot && gHave[id] && gSeen[id].alive && !Enemy(gSeen[id], s.team) && gSeen[id].team != 0xFE && Dist2D(gSeen[id].pos, p.at) < 3000.f) taken = true;
        if (taken) continue;
        const float sc = p.score - Dist2D(p.at, feet) / 10000.f;
        if (sc > bestScore) { bestScore = sc; best = &p; }
    }
    if (!best) { u.nextLayAt = gFrame + 300; return; }
    u.book = u.clays <= 0 || hsOnly;
    memcpy(u.lay, best->at, sizeof u.lay); memcpy(u.stand, best->stand, sizeof u.stand); u.layYaw = best->yaw;
    u.state = 5; u.stateAt = gFrame; b.path.clear(); b.next = 0; b.objWay = false;
    const float to[3] = {u.stand[0], u.stand[1] + kHips, u.stand[2]};
    if (!PlanTo(b, s, to)) { u.state = 0; u.nextLayAt = gFrame + 60; u.bad.push_back({{u.lay[0], u.lay[1], u.lay[2]}, gFrame + 3600}); return; }
    BLOG("vdoll %d: goes to lay a %s at a hidden place %.0f %.0f %.0f (seen from %.1f m at most, %d eyes of %d; the enemies' way in sees it from %.1f m; %s%s%s%s)", s.k + 1,
         u.book ? "book" : "claymore", u.lay[0], u.lay[1], u.lay[2], best->seen.far / 1000.f, best->seen.by, best->seen.of, best->approach / 1000.f,
         best->corner ? "a corner " : "", best->narrow ? "a narrow way " : "", best->trunk ? "by a trunk " : "", best->grass ? "in the foliage" : "");
}
// its claymore gone off: each enemy within the blast's reach, its body's point in a clear line from the blast, wounded as the
// layer's own game wounds (the measured records: {14 layer 00 02 00 ...}, {04 layer 100 .. 0x1B}, {09 layer kind angle})
void ClayBlast(const Self& s, Sup& u, const Boom& m) {
    if (RoundOver()) return;
    const float c0[3] = {m.at[0], m.at[1] + vsup::kBlastUp, m.at[2]};
    int hits = 0;
    ++u.blasts;
    for (int id = 0; id < 16; ++id) {
        if (id == s.slot || !gHave[id] || !gSeen[id].object || !gSeen[id].alive || !Enemy(gSeen[id], s.team)) continue;
        float hp[3] = {gSeen[id].pos[0], gSeen[id].pos[1], gSeen[id].pos[2]};
        if (gCover != COVER_OFF && Judged(id)) memcpy(hp, gRec[id].p, sizeof hp);   // (beta 21's gate: where he last said he is)
        const float pt[3] = {hp[0], hp[1] + kBodyPointUp, hp[2]};
        const float dist = Dist3(c0, pt);
        if (dist > vsup::kBlastReach || !(gBlastCover ? BlastClear(c0, pt) : CoverFree(c0, pt))) continue;   // (beta 24, walls3: its own lines)
        auto put = [](std::vector<uint8_t>& o, float v) { const int16_t mm = (int16_t)(int)v; Put16(o, (uint16_t)(int16_t)(int)((float)mm / 10.f)); };
        std::vector<uint8_t> fx = {0x14, (uint8_t)s.slot, 0x00, 0x02, 0x00};
        put(fx, pt[0] + vsup::kFxOffX); put(fx, pt[1]); put(fx, pt[2]);
        for (int i = 0; i < 3; ++i) put(fx, pt[i] - c0[i]);
        s.send(s.ctx, id, 2, fx);
        s.send(s.ctx, id, 0, {0x04, (uint8_t)s.slot, (uint8_t)vsup::kBlastDamage, (uint8_t)(vsup::kBlastDamage >> 8), 0x00, 0x00, (uint8_t)vsup::kClayCode, 0x00, 0x00});
        const unsigned st = gSeen[id].state;
        uint8_t react = (st & 0x8000000u) ? 5 : (st & 0x10000000u) ? 4 : (st & 0x8u) ? 3 : (uint8_t)kKnockBlast;
        uint16_t to = Angle16(Bearing(pt, c0));
        if (gReactKind && vreact::Ground(st)) {         // ((v150-host beta 25, reactfix) on the ground: thrown again with room over him)
            const vreact::Out ro = vreact::Throw(ReactVictim(id), true, pt[0] - c0[0], pt[2] - c0[2]);
            react = (uint8_t)ro.kind; to = ro.angle;
        }
        s.send(s.ctx, id, 1, {0x09, (uint8_t)s.slot, react, (uint8_t)to, (uint8_t)(to >> 8)});
        ++hits; ++u.blastHits;
        BLOG("vdoll %d: its claymore's blast reaches player %d at %.1f m from it (%d, %s), as the layer's own game wounds", s.k + 1, id + 1, dist / 1000.f, vsup::kBlastDamage,
             react == kKnockBlast ? "knocked down" : "on the ground: a flinch");
    }
    BLOG("vdoll %d: its claymore goes off at %.0f %.0f %.0f (id %#x): %d players in its blast", s.k + 1, m.at[0], m.at[1], m.at[2], (unsigned)m.id, hits);
}
int SupRecStyle(const Self& s, const Body& b, uint8_t* style) {
    if (!SupOn() || s.k < 0 || s.k >= (int)gSups.size()) return 0;
    const Sup& u = gSups[s.k];
    const long long t = gFrame - u.stateAt;
    switch (u.state) {
    case 2: *style = t < vsup::kLayCrouch ? 0x02 : 0x03; return 2;   // (crouched, then lying: V21_rot_owner's 02, 03)
    case 3: *style = b.speed > 1.f ? 0x43 : 0x03; return 3;          // (crawling 43, still 03)
    case 4: *style = t < vsup::kUpFrames / 2 ? 0x03 : 0x02; return 4;   // (up: 03 with its hips back, then 02)
    case 6: *style = 0x02; return 6;                                  // (laying crouched: mines_t5_on, book_r4)
    default: return 0;
    }
}
uint8_t SupLayKind(const Self& s) {
    const Sup& u = gSups[s.k];
    const long long t = gFrame - u.stateAt;
    if (t < vsup::kLayCrouch || t >= vsup::kLayCrouch + vsup::kLayHold + vsup::kLayMotion) return 0;
    return t < vsup::kLayCrouch + vsup::kLayHold ? 0x12 : 0x13;
}
bool SupLayBook(const Self& s) { return s.k >= 0 && s.k < (int)gSups.size() && gSups[s.k].book; }
bool SupBusy(int k) { return SupOn() && k >= 0 && k < (int)gSups.size() && gSups[k].state != 0; }

// (MGO1_VDOLL_DEBUG) the bodies' own processor time, logged every 30 s
double gBusyUs = 0;
struct Busy {
    std::chrono::steady_clock::time_point t0;
    Busy() { if (gDebug) t0 = std::chrono::steady_clock::now(); }
    ~Busy() { if (gDebug) gBusyUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count(); }
};

}  // namespace

void Init(const Host& h) {
    gHost = h;
    const char* on = getenv("MGO1_VDOLL_BODY");
    gOn = !(on && !strcmp(on, "0"));
    gDebug = getenv("MGO1_VDOLL_DEBUG") != nullptr;
    gCqcTest = getenv("MGO1_VDOLL_CQC_TEST") != nullptr;
    gNadeTest = getenv("MGO1_VDOLL_NADE_TEST") != nullptr;
    if (const char* e = getenv("MGO1_VDOLL_BUDGET")) gBudget = strcmp(e, "0") != 0;
    if (const char* c = getenv("MGO1_VDOLL_CONTROLS")) gPlayerCtl = !strcmp(c, "player");   // (beta 18; anything else: direct)
    if (const char* c = getenv("MGO1_VDOLL_SCOPE")) gScope = strcmp(c, "0") != 0;          // (beta 19; 0: as beta 18)
    if (const char* c = getenv("MGO1_VDOLL_GAME_LINES")) gGameLines = strcmp(c, "0") != 0;  // (beta 21, geom; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_STANCE")) gStance = strcmp(c, "0") != 0;        // (beta 21, geom; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_STANDOFF")) gStandoff = strcmp(c, "0") != 0;    // (beta 21; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_CQCFIGHT")) gCqcFight = strcmp(c, "0") != 0;   // (v150-host beta 24, cqcfight; 0: as beta 23)
    if (const char* c = getenv("MGO1_VDOLL_TURN")) gTurn = strcmp(c, "0") != 0;            // (beta 21, spin; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_PACE")) gPace = strcmp(c, "0") != 0;            // (beta 21, spin2; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_MOVE_FRAME")) gMoveFrame = strcmp(c, "0") != 0;  // (v150-host beta 25, aimcost; 0: as beta 25)
    { const char* c = getenv("MGO1_VDOLL_CLOCK_FIX"); gClockFix = !(c && !strcmp(c, "0")); }   // (v150-host beta 25, clockfix; 0: b16's pace)
    if (const char* c = getenv("MGO1_VDOLL_TEST_ROLL")) gTestRoll = std::max(0, std::min(600, atoi(c)));   // ((v150-host beta 25, rolltime) benches only)
    if (const char* c = getenv("MGO1_VDOLL_ROLL_TIME")) gRollTime = strcmp(c, "0") != 0;   // ((v150-host beta 25, rolltime; 0: as beta 25)
    if (const char* c = getenv("MGO1_VDOLL_ONE_PASS")) gOnePass = strcmp(c, "0") != 0;      // (v150-host beta 25, dpass; 0: as beta 25)
    if (const char* c = getenv("MGO1_VDOLL_STAIRS")) gStairs = strcmp(c, "0") != 0;        // (v150-host beta 24, snakehouse; 0: as beta 23)
    if (const char* c = getenv("MGO1_VDOLL_MESH_MARGIN")) gMeshMargin = strcmp(c, "0") != 0;   // (v150-host beta 25, meshmargin; 0: beta 25's mesh)
    if (const char* c = getenv("MGO1_VDOLL_TEST_SPOT")) gSpotTest = sscanf(c, "%f,%f,%f", &gSpot[0], &gSpot[1], &gSpot[2]) == 3;   // (meshmargin, benches)
    if (const char* c = getenv("MGO1_VDOLL_VAULT")) gVault = strcmp(c, "0") != 0;          // (v150-host beta 24, snakehouse; 0: as beta 23)
    if (const char* c = getenv("MGO1_VDOLL_HIT_FALL")) gHitFall = strcmp(c, "0") != 0;    // (v150-host beta 25, fallflinch; 0: beta 15's fall, b16)
    if (const char* c = getenv("MGO1_VDOLL_ITEM_WAY")) gItemWay = strcmp(c, "0") != 0;     // (v150-host beta 24, snakehouse; 0: as beta 23)
    gBooks = vbook::On(getenv("MGO1_VDOLL_BOOKS"));                                          // (beta 22, book; 0: as beta 21)
    gBookPose = vbook::PoseOn(getenv("MGO1_VDOLL_BOOK_POSE"));                               // (beta 22, bookplay; 0: its copy stands)
    gBookHold = vbook::HoldOn(getenv("MGO1_VDOLL_BOOK_HOLD"));                               // (beta 25, bookhold; 0: beta 22's holds)
    if (const char* e = getenv("MGO1_VDOLL_TEST_BOOK_PLACE")) { gBookPlaceEvery.clear(); for (const char* q = e; *q;) { gBookPlaceEvery.push_back(atoi(q)); while (*q && *q != ',') ++q; if (*q) ++q; } }
    if (const char* c = getenv("MGO1_VDOLL_WAKE")) gWake = strcmp(c, "0") != 0;            // (v150-host beta 22, wake; 0: as beta 22 before it)
    if (const char* c = getenv("MGO1_VDOLL_WAKE_THROW")) gWakeThrow = strcmp(c, "0") != 0; // (beta 22, wake; 0: no CQC throw to free a reader)
    if (const char* c = getenv("MGO1_VDOLL_WAKE_SIDE")) gWakeSide = strcmp(c, "0") != 0;   // (v150-host beta 25, wakeway; 0: as beta 25, no wall asked)
    if (const char* c = getenv("MGO1_VDOLL_WAKE_SLOPE")) gWakeSlope = strcmp(c, "0") != 0;   // (v150-host beta 25, wakeramp; 0: as beta 25, level run-ups only)
    gCopyTrace = getenv("MGO1_VDOLL_COPY_TRACE") ? atoi(getenv("MGO1_VDOLL_COPY_TRACE")) : 0;   // ((v150-host beta 25, copyleash) benches)
    if (const char* c = getenv("MGO1_VDOLL_COPY_LEASH")) gCopyLeash = strcmp(c, "0") != 0;   // ((v150-host beta 25, copyleash); 0: as beta 25)
    if (const char* c = getenv("MGO1_VDOLL_CQC")) gCqc = strcmp(c, "0") != 0;   // (v150-host beta 24, cqc; 0: beta 23's hold and 30000 for all)
    if (const char* c = getenv("MGO1_VDOLL_COVER")) gCover = !strcmp(c, "0") ? COVER_OFF : !strcmp(c, "run") ? COVER_RUN : COVER_REACH;   // (beta 21; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_REFLEX")) gReflexMs = std::max(0, std::min(1000, atoi(c)));   // (beta 21; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_COVER_MS")) gNetMs = (float)std::max(0, std::min(500, atoi(c)));
    if (const char* c = getenv("MGO1_VDOLL_COVER_RELAY")) gRelayFrames = (float)std::max(0.0, std::min(30.0, atof(c)));
    if (const char* c = getenv("MGO1_VDOLL_KILL_HOLD")) gKillHoldMs = std::max(0, std::min(10000, atoi(c)));   // (beta 21; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_SHOWN")) gShown = strcmp(c, "0") != 0;          // (beta 21; 0: as beta 20)
    if (const char* c = getenv("MGO1_VDOLL_MUZZLE")) gMuzzle = strcmp(c, "0") != 0;        // (v150-host beta 24, walls3; 0: as beta 23)
    if (const char* c = getenv("MGO1_VDOLL_BLAST_COVER")) gBlastCover = strcmp(c, "0") != 0;   // (v150-host beta 24, walls3; 0: as beta 23)
    if (const char* c = getenv("MGO1_VDOLL_REACH_THINGS")) gReachThings = strcmp(c, "0") != 0; // (v150-host beta 24, walls3; 0: as beta 23)
    if (const char* c = getenv("MGO1_VDOLL_HEAVY")) gHeavy = strcmp(c, "0") != 0;          // (v150-host beta 22, heavy; 0: as beta 21)
    if (const char* c = getenv("MGO1_VDOLL_PICKUP")) gPickup = !strcmp(c, "0") ? 0 : !strcmp(c, "all") || !strcmp(c, "2") ? 2 : 1;   // (beta 22, pickup; 0: as beta 21)
    if (const char* c = getenv("MGO1_VDOLL_PICKUP_NEAR")) gPickNear = (float)std::max(1, std::min(60, atoi(c))) * 1000.f;
    if (const char* c = getenv("MGO1_VDOLL_FOOD")) gFoodOn = strcmp(c, "0") ? 1 : 0;      // (v150-host beta 25, food; 0: as beta 24)
    // (v150-host beta 22, support) MGO1_VDOLL_SUPPORT=0: beta 22's dolls; _TAKE=0 / _LAY=0 one part off; _ITEM off | auto | claymore | book;
    // MGO1_VDOLL_SUPPORT_CLAYS / _BOOKS: the counts a life (the game's: docs/VIRTUAL_DOLL.md 35)
    if (const char* c = getenv("MGO1_VDOLL_SUPPORT")) gSupport = strcmp(c, "0") ? 1 : 0;
    if (const char* c = getenv("MGO1_VDOLL_SUPPORT_TAKE")) gSupTake = strcmp(c, "0") ? 1 : 0;
    if (const char* c = getenv("MGO1_VDOLL_SUPPORT_LAY")) gSupLay = strcmp(c, "0") ? 1 : 0;
    if (const char* c = getenv("MGO1_VDOLL_SUPPORT_ITEM")) gSupItem = !strcmp(c, "off") || !strcmp(c, "0") ? -1 : !strcmp(c, "claymore") ? 1 : !strcmp(c, "book") ? 2 : 0;
    if (const char* c = getenv("MGO1_VDOLL_SUPPORT_CLAYS")) { gClaySpawn = std::max(1, std::min(9, atoi(c))); gClayMost = std::max(gClayMost, gClaySpawn); }
    if (const char* c = getenv("MGO1_VDOLL_SUPPORT_BOOKS")) gBookSpawn = std::max(1, std::min(9, atoi(c)));
    gGogglesCamo = !(getenv("MGO1_VDOLL_GOGGLES_CAMO") && !strcmp(getenv("MGO1_VDOLL_GOGGLES_CAMO"), "0"));   // (beta 22, pickup2: the owner's rule, not the game's)
    {                                                          // (v150-host beta 22, blast; 0: as beta 21)
        const char* all = getenv("MGO1_VDOLL_BLASTS");
        const bool on = !(all && !strcmp(all, "0"));
        gFlash = on && !(getenv("MGO1_VDOLL_FLASH") && !strcmp(getenv("MGO1_VDOLL_FLASH"), "0"));
        gSmoke = on && !(getenv("MGO1_VDOLL_SMOKE") && !strcmp(getenv("MGO1_VDOLL_SMOKE"), "0"));
        gReactOut = on && !(getenv("MGO1_VDOLL_REACT") && !strcmp(getenv("MGO1_VDOLL_REACT"), "0"));
        gAvert = gFlash && !(getenv("MGO1_VDOLL_TURN_AWAY") && !strcmp(getenv("MGO1_VDOLL_TURN_AWAY"), "0"));
    }
    // ((v150-host beta 25, reactfix) MGO1_VDOLL_REACT_KIND=0: b16's reaction kinds (the flame's by turns, none for the M37, a punch's flinch,
    // a blast's or a roll's flinch on the ground whatever the room); MGO1_VDOLL_TEST_ROOM_R: the room's radius (mm, tests)
    gReactKind = !(getenv("MGO1_VDOLL_REACT_KIND") && !strcmp(getenv("MGO1_VDOLL_REACT_KIND"), "0"));
    if (const char* r = getenv("MGO1_VDOLL_TEST_ROOM_R")) gRoomR = std::max(50.f, std::min(3000.f, (float)atof(r)));
    BLOG(gReactKind ? "vdoll: the reaction of a doll's hit picked as a shooter's game picks it (MGO1_VDOLL_REACT_KIND)"
                    : "vdoll: the reaction of a doll's hit as b16 (MGO1_VDOLL_REACT_KIND=0)");
    gCoverLog = getenv("MGO1_VDOLL_COVER_LOG") ? std::max(0, std::min(2, atoi(getenv("MGO1_VDOLL_COVER_LOG")))) : 0;
    gCoverSee = gCover == COVER_OFF && gCoverLog > 0;
    if (const char* l = getenv("MGO1_VDOLL_LEVEL")) {          // 5 or 3,7,10 ... (one a doll, the last for the rest)
        std::string s(l);
        size_t at = 0;
        while (at <= s.size()) {
            const size_t c = s.find(',', at);
            const int v = atoi(s.substr(at, c == std::string::npos ? std::string::npos : c - at).c_str());
            gLevels.push_back(std::max(1, std::min(10, v ? v : 5)));
            if (c == std::string::npos) break;
            at = c + 1;
        }
    }
    if (const char* l = getenv("MGO1_VDOLL_WEAPONS")) {        // ak47,svd,auto ... (one a doll, the last for the rest)
        std::string s(l);
        size_t at = 0;
        while (at <= s.size()) {
            const size_t c = s.find(',', at);
            std::string w = s.substr(at, c == std::string::npos ? std::string::npos : c - at);
            // (v150-host beta 22, heavy) a special weapon too, as a weapon box gives it at each life (benches, until the dolls take
            // the boxes): "rpg7" or "flamethrower" (its primary by chance), "<primary>+rpg7", "<primary>+flamethrower"
            const size_t plus = w.find('+');
            const std::string sp = plus == std::string::npos ? w : w.substr(plus + 1);
            const int special = sp == "rpg7" ? W_RPG7 : sp == "flamethrower" || sp == "flame" ? W_FLAME : -1;
            if (special >= 0) w = plus == std::string::npos ? std::string("auto") : w.substr(0, plus);
            gSpecials.push_back(special);
            gWeapons.push_back(w == "ak47" ? W_AK47 : w == "xm16e1" ? W_XM16E1 : w == "scorpion" ? W_SCORPION : w == "m37" ? W_M37 : w == "svd" ? W_SVD : -1);
            if (c == std::string::npos) break;
            at = c + 1;
        }
    }
    if (const char* l = getenv("MGO1_DOLL_LEARN")) {
        gLearnDir = l;
        while (!gLearnDir.empty() && (gLearnDir.back() == '\\' || gLearnDir.back() == '/')) gLearnDir.pop_back();
    }
    gSeed ^= (uint32_t)(uintptr_t)&gBodies ^ (uint32_t)time(nullptr);
    if (const char* e = getenv("MGO1_VDOLL_SEED")) gSeed = (uint32_t)strtoul(e, nullptr, 0);   // (tests: the same draws each run)
    gSupSeed = (gSeed ^ 0x9E3779B9u) | 1u;             // (v150-host beta 22, support: its own chance, from the same seed)
    if (!gSeed) gSeed = 0x6C078965u;
    if (gOn) {
        std::string lv;
        for (int v : gLevels) lv += (lv.empty() ? "" : ",") + std::to_string(v);
        BLOG("vdoll: the bodies on (level %s; MGO1_VDOLL_BODY=0: still at their spawn)", lv.empty() ? "5" : lv.c_str());
        if (!gTurn) BLOG("vdoll: the bodies turn the earlier way (MGO1_VDOLL_TURN=0): their facing set at once");   // (v150-host beta 21, spin)
        if (!gPace) BLOG("vdoll: the bodies run the earlier way (MGO1_VDOLL_PACE=0): slowed from 5 degrees of slope either way by their own measure");   // (v150-host beta 21, spin2)
        BLOG(gMoveFrame ? "vdoll: a doll's moves costed by its speed over each host frame, as a player's by his move bits at each tick (MGO1_VDOLL_MOVE_FRAME)"
                        : "vdoll: a doll's moves costed by its body's last pass in each host frame, the earlier way (MGO1_VDOLL_MOVE_FRAME=0)");   // (v150-host beta 25, aimcost)
        if (gRollTime) BLOG("vdoll: a doll's roll as a player's: its press, the roll %.0f ticks later, %.0f ticks rolling and %.0f getting up, %.1f m, no shot before %.0f ticks after (MGO1_VDOLL_ROLL_TIME)",
                            kRollLead, kRollTicks, kRollUpTicks, RollAlong(kRollTicks + kRollUpTicks) / 1000.f, kRollRaise);   // ((v150-host beta 25, rolltime)
        else BLOG("vdoll: a doll's roll the earlier way (MGO1_VDOLL_ROLL_TIME=0): 36 frames, 2.6 m, free at once");
        if (gTestRoll > 0) BLOG("vdoll: a test roll every %d s at most along a doll's way (MGO1_VDOLL_TEST_ROLL, benches only)", gTestRoll);
        BLOG(gCopyLeash ? "vdoll: a doll held back by the host's copy of it, never while that copy falls or lands after the game put it at the doll's place on stairs or a slope, nor while it plays the doll's blows of a wake (MGO1_VDOLL_COPY_LEASH)"
                        : "vdoll: a doll held back by the host's copy of it, its fall after the game's put and its blows too, the earlier way (MGO1_VDOLL_COPY_LEASH=0)");   // ((v150-host beta 25, copyleash))
        BLOG(gOnePass ? "vdoll: a doll's every-100-ms decisions once a read of the players, in its first pass of the host frame; no turn in a pass with no frame since the last (MGO1_VDOLL_ONE_PASS)"
                      : "vdoll: a doll's every-100-ms decisions in each of its passes of a read's host frame, a turn in a pass with no frame since the last, the earlier way (MGO1_VDOLL_ONE_PASS=0)");   // (v150-host beta 25, dpass)
        BLOG("vdoll: an enemy in a close fight with a doll's teammate: %s (MGO1_VDOLL_CQCFIGHT)", gCqcFight ?   // (v150-host beta 24, cqcfight)
             "fought as a player fights him: its fire held only where the teammate's own body is on its line, a bullet stopped by his body, no grab at a crouched or lying player, never given up, no wake beside him" :
             "the earlier way: its fire held by a teammate within 0.7 m of its line, a bullet through him");
        if (gPlayerCtl)
            BLOG("vdoll: the bodies play as a player (MGO1_VDOLL_CONTROLS=player): the readied flag %d to %d frames, the readied turn %.0f to %.0f degrees a frame, "
                 "the first person view %d to %d frames and %.2f to %.0f degrees a frame, a bullet %d frames after the pull, the head only from the view (beyond %.0f m), its recoil",
                 kArmMin, kArmMax, kTpsMin, kTpsMax, kEntryMin, kEntryMax, kViewV0, kViewMax, kPullFrames, kViewFrom / 1000.f);
        BLOG("vdoll: a hit counts only in a clear line to where the player really is: %s (MGO1_VDOLL_COVER); a fight's first shot %d ms after its sight at the soonest%s (MGO1_VDOLL_REFLEX)",
             gCover == COVER_REACH ? "on (the place he last sent and, moving, his reach from there)" : gCover == COVER_RUN ? "run (the place he last sent and straight on from there)" : "off, the earlier way",
             gReflexMs, gReflexMs ? "" : " (off, the earlier way)");
        BLOG("vdoll: no shot at a player before the doll's own copy has been in his line: %s (MGO1_VDOLL_SHOWN)", gShown ? "on" : "off, the earlier way");
        // (v150-host beta 24, walls3)
        BLOG("vdoll: as a player, a third person bullet leaves the gun's muzzle (0.38 m over the hips) and its cover stops it; a gun under a cover's top takes the first person view: %s (MGO1_VDOLL_MUZZLE)",
             !gMuzzle ? "off, the earlier way (from the eye)" : gPlayerCtl ? "on" : "on (the direct controls: not used)");
        BLOG("vdoll: a blast, a rocket, a grenade or a flame stops at the map and at the game's things as the game's own, a real player judged where he last said he is: %s (MGO1_VDOLL_BLAST_COVER)",
             gBlastCover ? "on" : "off, the earlier way");
        BLOG("vdoll: the hit's gate asks the game's things once on each line across the player's reach: %s (MGO1_VDOLL_REACH_THINGS)",
             gReachThings ? "on" : "off, the earlier way (24 game lines a verdict, the rest the map's alone)");
        BLOG("vdoll: a book of another player within 1.5 m in a clear line is read as a player's own game makes him read it: %s (MGO1_VDOLL_BOOKS)",
             gBooks ? (gHost.books && gHost.readBook ? "on" : "on, but the host gives no book list") : "off, the earlier way");
        if (gBooks)                                    // (v150-host beta 22, bookplay)
            BLOG("vdoll: a reading doll's copy reads on the players' screens as a reader's does (its reading's records, as a player's game sends them): %s (MGO1_VDOLL_BOOK_POSE)",
                 gBookPose ? "on" : "off, its copy stands (the earlier way)");
        if (gBooks)                                    // (v150-host beta 25, bookhold)
            BLOG("vdoll: a book held back by a drop and its landing, a climb's first %d frames, never by a fence crossed, as a player's own game holds it: %s (MGO1_VDOLL_BOOK_HOLD)",
                 vbook::kClimbHoldFrames, gBookHold ? "on" : "off, the earlier way (a climb's 60 frames, a fence crossed)");
        BLOG("vdoll: a teammate seen asleep or stunned (his stamina spent) or reading a book, no enemy in sight: helped as a player helps him: %s (MGO1_VDOLL_WAKE); "
             "asleep or stunned: the combo run into him, its kick (friendly fire on or off); reading: freed by the CQC throw: %s (MGO1_VDOLL_WAKE_THROW)", gWake ? "on" : "off", gWake && gWakeThrow ? "on" : "off");   // (v150-host beta 22, wake)
        BLOG("vdoll: a teammate woken from his side of the wall he lies against, as a player's blows reach him (its way's goal, its run at him and each blow ask a clear line to him): %s (MGO1_VDOLL_WAKE_SIDE)", gWake && gWakeSide ? "on" : "off");   // (v150-host beta 25, wakeway)
        BLOG("vdoll: a teammate lying on a ramp or stairs woken as a player wakes him there (its run-up up or down the slope, its run at him on a walkable straight way at a player's pace): %s (MGO1_VDOLL_WAKE_SLOPE)", gWake && gWakeSlope ? "on" : "off");   // (v150-host beta 25, wakeramp)
        BLOG("vdoll: a CQC throw from a hold, its own and on it: %s (MGO1_VDOLL_CQC)", gCqc ? "its own with the game's stun for its character and the rule (10000 a soldier's), "
             "on it its stamina spent and, its gauge not empty, a thrown player's fall" : "its own 30000 whatever its character, on it no fall, the earlier way");   // (v150-host beta 24, cqc)
        BLOG("vdoll: the doorways' panels walked through and the stairs of blocks climbed (1 m a block at most) as a player does: %s (MGO1_VDOLL_STAIRS)",
             gStairs ? "on" : "off, the earlier way");    // (v150-host beta 24, snakehouse)
        BLOG("vdoll: no standing or crouching place of the bodies' mesh nearer a wall than a player's body stands (211 mm): %s (MGO1_VDOLL_MESH_MARGIN)",
             gMeshMargin ? (gStairs ? "on" : "on, but the full dolls' meshes are read with MGO1_VDOLL_STAIRS=0 when MGO1_DOLL_LEARN holds them") : "off, the earlier way");   // (v150-host beta 25, meshmargin)
        BLOG("vdoll: a fence or a low wall crossed with the action button as a player crosses it (its top 1.55 m at most): %s (MGO1_VDOLL_VAULT)",
             gVault ? "on" : "off, the earlier way");     // (v150-host beta 24, snakehouse)
        BLOG("vdoll: a free item no way of the doll's leads to (up a ladder) passed over for the next one: %s (MGO1_VDOLL_ITEM_WAY)",
             gItemWay ? "on" : "off, the earlier way");   // (v150-host beta 24, snakehouse)
        BLOG("vdoll: the special items lying near a doll (within %.0f m, in its sight): %s (MGO1_VDOLL_PICKUP)", gPickNear / 1000.f,   // (v150-host beta 22, pickup)
             !gPickup ? "never taken, the earlier way" :
#ifdef VDOLL_BODY_HEAVY
             gHeavy || gPickup == 2 ? "taken, all four" : "taken: the M63 and the thermal goggles (the RPG-7 and the flamethrower left lying: MGO1_VDOLL_HEAVY=0)");
#else
             gPickup == 2 ? "taken, all four (tests: the RPG-7 and the flamethrower carried)" : "taken: the M63 and the thermal goggles (the RPG-7 and the flamethrower with the body that fires them)");
#endif
        BLOG("vdoll: the food lying on the map (the mushrooms' boxes: GOLOVA, RUSSIAN GLOWCAP) %s (MGO1_VDOLL_FOOD)",   // (v150-host beta 25, food)
             gFoodOn ? "eaten as a player's game eats it: taken in its pick-up zone, gone to within the special items' reach when the gauge has lost half of what it gives back and no enemy is in sight"
                     : "never eaten, the earlier way");
        // (v150-host beta 22, pickup2: review22b P9) the goggles' sight of a camouflaged player: the owner's rule, its own switch
        if (gPickup) BLOG("vdoll: a doll wearing the thermal goggles sees a camouflaged player as any other: %s (MGO1_VDOLL_GOGGLES_CAMO; the owner's rule, the game's code gives the wearer only his screen's picture)",
                          gGogglesCamo ? "on" : "off, the camouflage as without them");
        if (!gSupport) BLOG("vdoll: the claymores and books of the support row: the earlier way (MGO1_VDOLL_SUPPORT=0)");   // (v150-host beta 22, support)
        else BLOG("vdoll: an enemy's claymore a doll saw taken by crawling onto it: %s (MGO1_VDOLL_SUPPORT_TAKE); its claymores and books laid at hidden places: %s "
                  "(MGO1_VDOLL_SUPPORT_LAY); its support item at each spawn: %s, %d claymores (%d at most) or %d books (MGO1_VDOLL_SUPPORT_ITEM)",
                  gSupTake ? "on" : "off", gSupLay ? "on" : "off",
                  gSupItem < 0 ? "the grenade always" : gSupItem == 1 ? "the claymore (the bench's)" : gSupItem == 2 ? "the book (the bench's)" : "drawn as a player chooses (the claymore 8 %, the book 6 %, else the grenade)",
                  gClaySpawn, gClayMost, gBookSpawn);
        BLOG("vdoll: a stun grenade's flash whitens the dolls' eyes as a player's: %s (MGO1_VDOLL_FLASH); a smoke's cloud hides a player from them: %s (MGO1_VDOLL_SMOKE)",
             gFlash ? "on" : "off, the earlier way", gSmoke ? "on" : "off, the earlier way");
        if (gFlash) BLOG("vdoll: a doll turns away from its own stun grenade until it goes off, as a player: %s (MGO1_VDOLL_TURN_AWAY)", gAvert ? "on" : "off");
        bool any = false;                              // (v150-host beta 22, heavy)
        for (int w : gSpecials) any = any || w >= 0;
        if (!gHeavy) BLOG("vdoll: the RPG-7 and the flamethrower never in the bodies' hands (MGO1_VDOLL_HEAVY=0, the earlier way)");
        else if (any) BLOG("vdoll: each life a weapon box's special weapon (MGO1_VDOLL_WEAPONS): the RPG-7 at range or at a group, the flamethrower close by (MGO1_VDOLL_HEAVY)");
    }
}

bool On() { return gOn; }
// (v150-host beta 22, bookplay) the state record's third byte as a reader's game sends it (his +0xFB4: 2 while his book action
// runs, vdoll_book.h): a copy's reading lasts while it says so
int StateEvent(int k) {
    if (!gOn || !gBooks || !gBookPose || k < 0 || k >= (int)gBodies.size()) return 0;
    return gBodies[k].bookId ? vbook::kStateReading : 0;
}

void Live(int k, bool on, float level, int lo, int hi, const float foe[16]) {
    if (k < 0 || k >= 16) return;
    LiveNow& v = gLive[k];
    v.on = on; v.level = std::max(1.f, std::min(10.f, level)); v.lo = std::max(1, std::min(10, lo)); v.hi = std::max(v.lo, std::min(10, hi));
    for (int i = 0; i < 16; ++i) v.foe[i] = foe ? foe[i] : 0.f;
}
int LevelSet(int k) { return LevelOf(k); }
// (v150-host beta 21, geom, tests) a sight's and a shot's line as Perceive and Shoot test them; a player's points by his stance
bool SightStopped(const float a[3], const float b[3]) { return !Ready() || !gMap->nav || !SightClear(a, b); }
bool ShotStopped(const float a[3], const float b[3], int weaponCode) {
    if (!Ready() || !gMap->nav) return true;
    float wall = 1.f, n[3];
    if (gMap->nav->FirstHit(a, b, &wall, n) && wall < 0.999f) return true;
    int power = 120;
    for (const Weapon& w : kWeapons) if (w.code == weaponCode) power = Power(w);
    return ThingStops(a, b, power);
}
void BodyPoints(const PlayerNow& p, float chest[3], float head[3]) { Chest(p, chest); Head(p, head); }

// ---- (v150-host beta 21, geom; tests) MGO1_VDOLL_GEOM_AUDIT=<n>: on each map, 15 s into its first round (the things the
// game makes at a round's start are there), n lines between two points of the map (its spawn points and random points of the
// mesh, 5 to 60 m apart, each end at a standing, crouched or lying eye: 1.55, 1.10, 0.35 m over the floor) asked of the bodies'
// collision and of the game's own line test (the stage's faces and the game's things); the disagreements logged with their
// places (a line the bodies find clear and the game stopped: a doll would see or shoot through what stops a bullet), each
// thing found with how hard it is, and a line of counts; 200 lines a pass, the dolls' play going on ----
struct AuditThing { uint32_t id = 0, attr = 0; int hardness = 0, lines = 0; float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0}; };
struct Audit { std::string key; int done = 0, n = 0, both = 0, clear = 0, wall = 0, wallObj = 0, wallSoft[3] = {0, 0, 0}, wallGlass = 0, blind = 0, unanswered = 0, told = 0;
               int more = 0, moreHits = 0;
               bool said = false; long long us = 0, usThings = 0, since = -1; uint32_t seed = 0x2545F491u; std::vector<AuditThing> things; };
Audit gAudit;
// the face of the stage nearest a point of a segment (its attribute word): the bodies' hit named
uint32_t FaceAt(const float* p, float* dist) {
    const doll::Geom& g = *gMap->geom;
    uint32_t best = 0; float bd = 1e30f;
    for (const doll::Face& f : g.faces) {
        float c[3] = {0, 0, 0};
        for (int i = 0; i < 4; ++i) for (int a = 0; a < 3; ++a) c[a] += f.v[i][a] * 0.25f;
        const float plane = std::fabs((p[0] - f.v[0][0]) * f.n[0] + (p[1] - f.v[0][1]) * f.n[1] + (p[2] - f.v[0][2]) * f.n[2]);
        const float d = plane + 0.1f * Dist3(p, c);
        if (d < bd) { bd = d; best = f.attr; }
    }
    *dist = bd;
    return best;
}
void AuditFrame() {
    static const int want = getenv("MGO1_VDOLL_GEOM_AUDIT") ? atoi(getenv("MGO1_VDOLL_GEOM_AUDIT")) : 0;
    if (want <= 0 || !Ready() || !gMap->geom || !gMap->nav || !gHost.gameLine) return;
    Audit& A = gAudit;
    if (A.key != gMap->key) { A = Audit(); A.key = gMap->key; }
    if (A.said) return;
    if (gHost.phase && gHost.phase() != 2) { A.since = -1; return; }
    if (A.since < 0) A.since = gFrame;
    if (gFrame - A.since < 900) return;
    auto rnd = [&]() { A.seed ^= A.seed << 13; A.seed ^= A.seed >> 17; A.seed ^= A.seed << 5; return A.seed; };
    static uint32_t* seedOf = nullptr; seedOf = &A.seed;
    unsigned (*meshRnd)() = []() -> unsigned { uint32_t& x = *seedOf; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; };
    const doll::Geom& g = *gMap->geom;
    static const float kEyes[3] = {1550.f, 1100.f, 350.f};
    static const int kPowers[3] = {50, 120, 150};
    // (each thing found: its name word, how hard it is, the box of the places where lines met it)
    auto note = [&](const vdoll::LineHit& h) {
        AuditThing* th = nullptr;
        for (AuditThing& x : A.things) if (x.id == h.objectId && x.hardness == h.hardness) th = &x;
        if (!th && A.things.size() < 64) {
            A.things.push_back(AuditThing()); th = &A.things.back(); th->id = h.objectId; th->hardness = h.hardness; th->attr = h.attr;
            memcpy(th->lo, h.at, sizeof th->lo); memcpy(th->hi, h.at, sizeof th->hi);
        }
        if (!th) return;
        ++th->lines;
        for (int a = 0; a < 3; ++a) { th->lo[a] = std::min(th->lo[a], h.at[a]); th->hi[a] = std::max(th->hi[a], h.at[a]); }
    };
    // (after the comparison: ten times as many lines asked of the things alone, 1000 a pass, both ways along each: every
    // thing of the map found and sized; these lines go from 0.2 to 1.8 m over the floor)
    if (A.done >= want && A.more < want * 10) {
        for (int k = 0; k < 1000 && A.more < want * 10; ++k) {
            ++A.more;
            float p[3], q[3];
            if (!gMap->nav->RandomPoint(nullptr, 0.f, p, meshRnd) || !gMap->nav->RandomPoint(p, 30000.f, q, meshRnd)) continue;
            const float a[3] = {p[0], p[1] + 200.f + (float)(rnd() % 1600), p[2]}, b[3] = {q[0], q[1] + 200.f + (float)(rnd() % 1600), q[2]};
            if (Dist3(a, b) < 1000.f) continue;
            vdoll::LineHit h;
            if (gHost.gameLine(a, b, false, &h) > 0 && h.object) { ++A.moreHits; note(h); }
            if (gHost.gameLine(b, a, false, &h) > 0 && h.object) note(h);
        }
        return;
    }
    for (int k = 0; k < 200 && A.done < want; ++k) {
        ++A.done;
        float p[3], q[3];
        const bool fromSpawn = !g.spawns.empty() && (rnd() & 3) == 0;
        if (fromSpawn) { const doll::Spawn& sp = g.spawns[rnd() % g.spawns.size()]; memcpy(p, sp.p, sizeof p); }
        else if (!gMap->nav->RandomPoint(nullptr, 0.f, p, meshRnd)) continue;
        if (!gMap->nav->RandomPoint(p, 60000.f, q, meshRnd)) continue;
        const float d = Dist2D(p, q);
        if (d < 5000.f || d > 60000.f) continue;
        const float hp = kEyes[rnd() % 3], hq = kEyes[rnd() % 3];
        const float a[3] = {p[0], p[1] + hp, p[2]}, b[3] = {q[0], q[1] + hq, q[2]};
        ++A.n;
        const bool soup = gMap->nav->Clear(a, b);
        vdoll::LineHit h;
        const auto t0 = std::chrono::steady_clock::now();
        const int game = gHost.gameLine(a, b, true, &h);
        const auto t1 = std::chrono::steady_clock::now();
        vdoll::LineHit ht;
        gHost.gameLine(a, b, false, &ht);                    // (the things alone: what the bodies ask in play, its cost)
        A.us += (long long)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        A.usThings += (long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1).count();
        if (game < 0) { ++A.unanswered; continue; }
        if (game && h.object) note(h);
        if (!soup && game) ++A.both;
        else if (soup && !game) ++A.clear;
        else if (soup && game) {
            ++A.wall; if (h.object) ++A.wallObj;
            for (int i = 0; i < 3; ++i) if (h.object && h.hardness < kPowers[i]) ++A.wallSoft[i];
            if (h.object && (h.attr & kGlassFace)) ++A.wallGlass;
            if (A.told++ < 40)
                BLOG("vdoll: geom audit %s: the bodies clear, the game stopped: %.0f %.0f %.0f -> %.0f %.0f %.0f (%.1f m), at %.0f %.0f %.0f, face %08x%s",
                     A.key.c_str(), a[0], a[1], a[2], b[0], b[1], b[2], Dist3(a, b) / 1000.f, h.at[0], h.at[1], h.at[2], h.attr, h.object ? " (a thing of the game)" : "");
        } else {
            ++A.blind;
            if (A.told++ < 40) {
                float t = 1.f, n[3];
                gMap->nav->FirstHit(a, b, &t, n);
                const float at[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
                float fd = 0;
                const uint32_t fa = FaceAt(at, &fd);
                BLOG("vdoll: geom audit %s: the bodies stopped, the game clear: %.0f %.0f %.0f -> %.0f %.0f %.0f (%.1f m), at %.0f %.0f %.0f, face %08x",
                     A.key.c_str(), a[0], a[1], a[2], b[0], b[1], b[2], Dist3(a, b) / 1000.f, at[0], at[1], at[2], fa);
            }
        }
    }
    if (A.done >= want && A.more >= want * 10 && !A.said) {
        A.said = true;
        for (const AuditThing& x : A.things)
            BLOG("vdoll: geom audit %s: a thing of the game (name word %08x, hardness %d, face %08x) met by %d lines, between %.0f %.0f %.0f and %.0f %.0f %.0f",
                 A.key.c_str(), x.id, x.hardness, x.attr, x.lines, x.lo[0], x.lo[1], x.lo[2], x.hi[0], x.hi[1], x.hi[2]);
        BLOG("vdoll: geom audit %s: %d more lines asked of the things alone: %d met one", A.key.c_str(), A.more, A.moreHits);
        BLOG("vdoll: geom audit %s: %d lines (5 to 60 m, eyes 1.55 1.10 0.35 m): both stopped %d, both clear %d, the bodies clear and the game stopped %d "
                 "(on a thing of the game %d; softer than a bullet of power 50, 120, 150: %d, %d, %d; glass %d), the bodies stopped and the game clear %d, not answered %d; "
                 "%zu things found; the game's whole test %.1f us a line, its things alone %.1f us",
                 A.key.c_str(), A.n, A.both, A.clear, A.wall, A.wallObj, A.wallSoft[0], A.wallSoft[1], A.wallSoft[2], A.wallGlass, A.blind, A.unanswered, A.things.size(),
                 A.n ? (double)A.us / A.n : 0.0, A.n ? (double)A.usThings / A.n : 0.0);
    }
}

// (v150-host beta 22, blast) its eyes look through its first person view: as a player, the view up (or coming up: its white
// is the view's); direct, a fight's aim at the head beyond 10 m (the view FightPlayer takes there) or a scoped rifle up
bool InView(const Body& b) {
    if (gPlayerCtl) return b.ctl == C_VIEW || b.ctl == C_ENTERING;
    return ScopeOn(b) || (b.aiming && b.target >= 0 && b.head && Dist2D(gOwnPos[b.slot & 15], b.tpos) > kViewFrom);
}
// a stun grenade's blast of the host's game at p: each living doll's white as its own game would make it (vdoll_blast.h).
// The game asks its CAMERA: in the view, the eye and the view's direction; else the third person camera, measured on a real
// client standing (blast_t1_d, probe31 cam): 3874 mm behind his hips along his facing, 1494 mm over them, its forward pitched
// 14.4 degrees down, on his facing; so a blast ahead within some 10 m is faced the game's way in the third person too
// (measured: a real client facing a stun grenade 3.75 m and 6 m before him, white 1439 and 1438 frames, blast_b21_d,
// blast_t1_d). The game's mode test sees no wall in the third person but asks whether the blast is in the camera's area
// (0x109BF8): a stun grenade on a lower floor gave a real client no white at all (blast_b21_d); the doll has no camera area,
// so a blast its eye has no clear line to whitens nothing
const float kCamBack = 3874.f, kCamUp = 1494.f, kCamPitch = 14.4f * kPi / 180.f;
void Flash(const float* p, long long frame) {
    const float at[3] = {p[0], p[1] + 150.f, p[2]};  // (the grenade lies on its floor: the line from just over it)
    for (size_t k = 0; k < gBodies.size(); ++k) {
        Body& b = gBodies[k];
        if (b.life < 0 || b.slot < 0 || b.slot >= 16 || frame - gOwnAt[b.slot] > 12) continue;
        const float* pos = gOwnPos[b.slot];
        const float eye[3] = {pos[0], pos[1] + (b.low ? kLowEye : kEye), pos[2]};
        if (Ready() && gMap->nav && !LineClear(at, eye)) continue;   // (out of its eye's line: out of its camera's area)
        const bool view = InView(b);
        float cam[3], look[3];
        if (view) { memcpy(cam, eye, sizeof cam); Dir(b.yaw, b.pitch, look); }
        else {
            cam[0] = pos[0] - std::sin(b.yaw) * kCamBack; cam[1] = pos[1] + kCamUp; cam[2] = pos[2] - std::cos(b.yaw) * kCamBack;
            Dir(b.yaw, kCamPitch, look);
        }
        const int white = vblast::FlashFrames(cam, look, at, false);
        const int blind = vblast::BlindFrames(white);
        if (blind <= 0) continue;
        const bool was = gFrame < b.blindUntil;
        b.blindUntil = std::max(b.blindUntil, frame + blind);
        ++b.flashes;
        BLOG("vdoll %d: a stun grenade's flash %.1f m away%s: white %.1f s, nothing seen %.1f s (the game's own white)", (int)k + 1, Dist3(eye, at) / 1000.f,
             view ? ", through its view" : "", white / 59.94f, blind / 59.94f);
        if (!was && b.target >= 0 && gDebug) BLOG("vdoll %d: white: its fight with player %d goes on blind", (int)k + 1, b.target + 1);
    }
}
std::vector<uint32_t> gFlashSeen;                // (the host's stun blasts already met: each one once)
void Blasts(long long frame) {
    if (gHost.smokes && (frame - gSeenAt == 0 || gCloudN > 0)) {   // (with the players' read; each frame while a cloud is out)
        float c[8][3], r[8]; int left[8];
        const int n = gSmoke ? gHost.smokes(c, r, left, 8) : 0;
        for (int i = 0; i < n; ++i) {
            if (gDebug && left[i] > vblast::kCloudTime - 30 && (i >= gCloudN || std::fabs(gClouds[i].c[0] - c[i][0]) > 1.f))
                BLOG("vdoll: a smoke's cloud at %.0f %.0f %.0f (radius %.1f m, %.1f s)", c[i][0], c[i][1], c[i][2], r[i] / 1000.f, left[i] / 300.f);
            memcpy(gClouds[i].c, c[i], sizeof gClouds[i].c); gClouds[i].r = r[i]; gClouds[i].left = left[i];
        }
        gCloudN = std::max(0, n);
    }
    if (!gFlash || !gHost.stunBlasts) return;
    uint32_t id[8]; float at[8][3];
    const int n = gHost.stunBlasts(id, at, 8);
    std::vector<uint32_t> now;
    for (int i = 0; i < n; ++i) {
        now.push_back(id[i]);
        if (std::find(gFlashSeen.begin(), gFlashSeen.end(), id[i]) == gFlashSeen.end()) Flash(at[i], frame);
    }
    gFlashSeen.swap(now);
}
// ((v150-host beta 25, dpass) MGO1_VDOLL_DEBUG: every 3600 host frames, each doll's passes and what its every-100-ms decisions did in them
// (their looks: a chance drawn, its own clock asked), with the real seconds of the lap (docs/LOGS.md)
void DecLine(long long frame) {
    static long long from = -1; static unsigned long long fromMs = 0;
    if (from < 0 || frame < from) { from = frame; fromMs = RealMs(); return; }
    if (frame - from < 3600) return;
    const double secs = (double)(RealMs() - fromMs) / 1000.0;
    for (size_t k = 0; k < gBodies.size() && k < 16; ++k) {
        DecCount& c = gDec[k]; DecCount& l = gDecLap[k];
        const Body& b = gBodies[k];
        if (c.passes == l.passes) continue;
        BLOG("vdoll %d: its decisions these %.1f s (%lld frames%s): passes %lld (%lld with no frame since the last), the players' reads %lld (and %lld more passes in a read's "
             "frame), decided in %lld passes (%lld of them with no frame since the last): HeldBack %lld (%lld held), Perceive %lld; roll looks %lld (%lld rolls), grab looks "
             "%lld (%lld grabs begun, %d thrown), roll-into looks %lld (%lld rolls, %d knocked down), rush looks %lld (%lld rushes), grenade looks %lld (%lld past its pace, %d "
             "thrown), support looks %lld (%lld past its clock, %lld begun); turned %.0f degrees, %.0f of them in passes with no frame since the last; records %lld (%lld in "
             "a pass with no frame since the last)",
             (int)k + 1, secs, frame - from, gOnePass ? "" : ", MGO1_VDOLL_ONE_PASS=0", c.passes - l.passes, c.pass0 - l.pass0, c.reads - l.reads, c.readsAgain - l.readsAgain,
             c.blocks - l.blocks, c.blocks0 - l.blocks0, c.held - l.held, c.heldFired - l.heldFired, c.perceive - l.perceive, c.rollLooks - l.rollLooks, c.rolls - l.rolls,
             c.grabLooks - l.grabLooks, c.grabsBegun - l.grabsBegun, b.grabs - c.lastGrabs, c.intoLooks - l.intoLooks, c.intos - l.intos, b.rollHits - c.lastRollHits,
             c.rushLooks - l.rushLooks, c.rushes - l.rushes, c.throwLooks - l.throwLooks, c.throwTries - l.throwTries, b.throws - c.lastThrows, c.supLooks - l.supLooks,
             c.supTries - l.supTries, c.supBegun - l.supBegun, c.turn - l.turn, c.turn0 - l.turn0, c.recs - l.recs, c.recs0 - l.recs0);
        c.lastGrabs = b.grabs; c.lastRollHits = b.rollHits; c.lastThrows = b.throws;
        l = c;
    }
    from = frame; fromMs = RealMs();
}
void Frame(long long frame) {
    if (!gOn) return;
    Busy busy;
    gFrame = frame;
    if ((frame & 63) == 0) { gNadeTest = getenv("MGO1_VDOLL_NADE_TEST") != nullptr; gDartTest = getenv("MGO1_VDOLL_DART_TEST") != nullptr;
                             gTestTarget = getenv("MGO1_VDOLL_TEST_TARGET") ? atoi(getenv("MGO1_VDOLL_TEST_TARGET")) : 0;
                             gTrapTest = getenv("MGO1_VDOLL_TEST_TRAP") != nullptr;
                             { const char* h = getenv("MGO1_VDOLL_TEST_HARMLESS"); gTestHarmless = !h ? -1 : !strcmp(h, "real") ? -2 : atoi(h) - 1; }
                             gWakeTest = getenv("MGO1_VDOLL_TEST_WAKE") != nullptr; gWakeFollow = gWakeTest && atoi(getenv("MGO1_VDOLL_TEST_WAKE")) == 2; }   // (tests: switched on and off in a run)
    WorkPass(frame);
    std::string page;
    int extra = 0;
    if (!gHost.mapNow || !gHost.mapNow(&page, &extra)) return;
    const std::string key = extra > 12 ? page + "_x" + std::to_string(extra) : page;
    if (!gMap || gMap->key != key) {
        auto m = std::make_shared<Map>();
        m->page = page; m->extra = extra; m->key = key; m->state = 1;
        std::shared_ptr<Map> old = std::move(gMap);           // (freed on the build's thread: a map's mesh, some ms)
        gMap = m;
        gHeld.clear(); gHeldOnce.clear(); gLeave.clear();
        for (Body& b : gBodies) { b.path.clear(); b.target = -1; b.aiming = false; }
        std::thread([m, old]() mutable { old.reset(); Build(m); }).detach();
        BLOG("vdoll: map %s: building the bodies' navigation", key.c_str());
    }
    if (gMap->state >= 2 && !gMap->log.empty()) {
        BLOG("%s", gMap->log.c_str());
        gMap->log.clear();
    }
    AuditFrame();                                            // (beta 21, geom; tests only)
    {                                                  // (v150-host beta 21, cover) the host's frames in real ms, smoothed
        static long long lastF = -1; static unsigned long long lastMs = 0;
        static double msSm = 1000.0 / 59.94, frSm = 1.0;   // ((v150-host beta 25, clockfix) the passes' ms and frames, smoothed apart)
        const unsigned long long ms = RealMs();
        if (lastF >= 0 && frame > lastF && frame - lastF < 60 && ms >= lastMs) {
            // ((v150-host beta 25, clockfix) MGO1_VDOLL_CLOCK_FIX: the ratio of the passes' smoothed ms to their smoothed frames, each frame weighed
            // alike. b16 smoothed each pass's ms a frame, a pass of one frame weighed as one of two: on a host held by a processor quota the
            // one-frame passes are the longer a frame (two thirds of the passes, those across its freezes), so Slowed() ran 2.8 % ahead of the
            // real time (bench cf_k_before, pace 34: the doll's run, roll and fall that much faster than a player's); 0: b16's)
            msSm = msSm * 0.95 + (double)(ms - lastMs) * 0.05; frSm = frSm * 0.95 + (double)(frame - lastF) * 0.05;
            gMsAFrame = std::max(5.0, std::min(100.0, gClockFix ? msSm / frSm : gMsAFrame * 0.95 + (double)(ms - lastMs) / (double)(frame - lastF) * 0.05));
        }
        lastF = frame; lastMs = ms;
    }
    if (frame - gSeenAt >= 6) { SeeAll(frame); gSeenAt = frame; if (gCoverLog >= 2) CoverTrack(); WakeWatch(frame); if (gCopyTrace) CopyTrace(frame); }
    if (Ready()) Blasts(frame);                              // (v150-host beta 22, blast)
    SeeObjectives(frame);
    SeeDrops(frame);                                         // (v150-host beta 22, pickup)
    SeeFoods(frame);                                         // (v150-host beta 25, food)
    if (SupOn()) {                                           // (v150-host beta 22, support) the claymores lying, the dolls' gone off, the places
        if (frame == gSeenAt) SeeClaymores(frame);
        if (gHost.clayBlasts) { uint32_t id[16]; float at[16][3]; const int n = gHost.clayBlasts(id, at, 16); for (int i = 0; i < n; ++i) gBooms.push_back({id[i], {at[i][0], at[i][1], at[i][2]}, frame}); }
        gBooms.erase(std::remove_if(gBooms.begin(), gBooms.end(), [&](const Boom& m) { return frame - m.frame > 30; }), gBooms.end());
        gAvoidWays = nullptr;
        StepSidePlaces();
    }
    WatchItems(frame);
    ItemRule(frame);
    if (gBooks && gHost.books) {                       // (v150-host beta 22, book) the host's book list, each frame as a player's game reads it
        BookNow l[16];
        const int n = std::max(0, std::min(16, gHost.books(l, 16)));
        for (int i = 0; i < n; ++i) { gBookList[i].id = l[i].id; gBookList[i].slot = l[i].slot; gBookList[i].readTime = l[i].readTime; memcpy(gBookList[i].pos, l[i].pos, sizeof l[i].pos); }
        gBookN = n;
    } else gBookN = 0;
    for (size_t k = 0; k < gBodies.size(); ++k) if (gBodies[k].blastAt >= 0 && frame >= gBodies[k].blastAt) Blast(gBodies[k], (int)k);
    for (size_t k = 0; k < gBodies.size(); ++k) if (gBodies[k].rocketAt >= 0) RocketStep(gBodies[k], (int)k, frame);   // (v150-host beta 22, heavy; heavy2: its flight a frame at a time)
    static long long lastStatus = 0;
    if (gDebug && frame - lastStatus >= 1800) {
        BLOG("vdoll: the bodies' processor %.2f ms a second (%.0f s)", gBusyUs / 1000.0 / ((frame - lastStatus) / 59.94), (frame - lastStatus) / 59.94);
        lastStatus = frame;
        gBusyUs = 0;
        for (size_t k = 0; k < gBodies.size(); ++k) if (gBodies[k].life >= 0) { BLOG("%s", Status((int)k).c_str()); gBodies[k].aimStillLap = gBodies[k].aimStill; }   // (beta 21: its longest aim since this line)
    }
    if (gDebug) DecLine(frame);                        // ((v150-host beta 25, dpass) its decisions' count)
}

void Alive(const Self& s, long long frame) {
    if (!gOn || s.slot < 0 || s.slot >= 16 || !s.pos || !s.facing || !s.send) return;
    Busy busy;
    gFrame = frame;
    Body& b = BodyOf(s.k);
    LevelNow(b, s.k);
    if (b.life != s.life) NewLife(b, s);
    else { const int r = RoleOf(s); if (r != b.role) SetRole(b, s, r); }   // (beta 18: the round's sides known a moment later)
    b.slot = s.slot;                                  // (v150-host beta 22, blast: the flash's eye from its own place)
    // ((v150-host beta 25, dpass) the players' read of this frame (Frame, every 6 frames) taken once, by its first pass of the read's frame
    // (MGO1_VDOLL_ONE_PASS=0: by each pass of that frame, beta 25's); what reads it below asks b.readNow
    b.readNow = frame == gSeenAt && (!gOnePass || b.readAt != gSeenAt);
    if (frame == gSeenAt) { if (b.readAt == gSeenAt) ++gDec[s.k & 15].readsAgain; else ++gDec[s.k & 15].reads; }
    if (b.readNow) b.readAt = gSeenAt;
    const float yawWas = b.yaw;                       // ((dpass) its turn in this pass, the debug count)
    const bool again = b.last == frame;               // ((dpass) another pass in a frame it had one in: no frame since the last)
    const bool grabWas = b.grabStart >= 0;
    Sup& sup = SupOf(s.k);                            // (v150-host beta 22, support: this life's item; the claymores it sees; its ways' keep-off)
    SupLife(b, s, sup);
    if (sup.pauseFrom >= 0) {                          // ((v150-host beta 23, review) paused lying: a flinch (40 frames, a few hits 100 at
        if (sup.pauseLast - sup.pauseFrom > 100) { sup.state = 0; sup.moved = false; }   // most) keeps it lying; knocked down or held (2 s
        sup.pauseFrom = sup.pauseLast = -1;            // and more, its copy up after it) ends its crawl, as before
    }
    if (SupOn()) {
        if (b.readNow) SupSee(b, s, sup);
        Avoid(s, sup); gAvoidWays = &sup.avoid;
        if (b.readNow && !sup.avoid.empty() && b.next < b.path.size()) {   // (a way planned before it saw one: planned again)
            std::vector<doll::Waypoint> w;
            doll::Waypoint p0{}; p0.p[0] = s.pos[0]; p0.p[1] = s.pos[1] - kHips; p0.p[2] = s.pos[2]; w.push_back(p0);
            for (size_t i = b.next; i < b.path.size(); ++i) { doll::Waypoint q{}; memcpy(q.p, b.path[i].p, sizeof q.p); w.push_back(q); }
            if (!AvoidOk(w)) { b.path.clear(); b.next = 0; BLOG("vdoll %d: its way kept off a claymore or book it knows: planned again", s.k + 1); }
        }
    } else gAvoidWays = nullptr;
    LevelNow(b, s.k);                                 // (v150-host beta 19: again after a new life's reset)
    SendOwed(b, s, frame, false);                     // (v150-host beta 22, pickup2: a take's emptied record, whatever it does now)
    if (s.hp >= 0 && b.hp >= 0 && s.hp < b.hp && s.attacker >= 0 && s.attacker < 16 && s.attacker != s.slot) { b.hurtBy = s.attacker; b.hurtAt = frame; }
    b.hp = s.hp;
    if (b.reloadAt >= 0 && frame >= b.reloadUntil) {
        b.mag[b.held] = b.held == b.special ? std::min(kWeapons[b.held].magazine, b.specialRounds) : kWeapons[b.held].magazine;   // (beta 22: the rounds it took)
        b.reloadAt = -1;
    }
    SpecialSpent(b, s);                               // (v150-host beta 22, pickup)
    // (v150-host beta 22, heavy) no enemy (its fight over, a reading begun): the flame's stream over; and with a special weapon
    // the gun's burst left from the last fight too, which kept its next fight from choosing the RPG-7 or the flamethrower (a
    // burst runs to its end before a swap: bench heavy_m1_dm3, the flamethrower's doll fired its AK-47 at 1.6 m)
    if (b.target < 0 && (IsFlame(b.held) || b.heavyW >= 0) && (b.burstLeft > 0 || b.trigger)) { ReleaseTrigger(b); b.burstLeft = 0; b.burstEnd = frame; }
    if (b.target < 0 && b.reloadAt < 0 && frame >= b.switchUntil && b.grabStart < 0 && b.rpgPullAt < 0 && frame >= b.rpgShowUntil) {     // out of a fight: the primary back, its magazine refilled (beta 22: the RPG-7's pull and shot to their end)
        if (b.held != b.primary) Draw(b, b.primary);
        else if (b.mag[b.held] < kWeapons[b.held].magazine / 2) Spent(b, 1e30f);
    }
    const int dt = b.last < 0 ? 0 : (int)std::max(0LL, std::min(6LL, frame - b.last));
    b.last = frame;
    ++b.passN; if (dt == 0) ++b.pass0;                // ((v150-host beta 25, aimcost) the debug count of its moves)
    DecCount& dc = gDec[s.k & 15];                    // ((v150-host beta 25, dpass) the debug count of its decisions)
    ++dc.passes; if (again) ++dc.pass0;
    const float was[2] = {s.pos[0], s.pos[2]};        // (v150-host beta 21: its place and its shots before this pass, the measure below)
    const int shotsWas = b.shots;
    memcpy(gOwnPos[s.slot], s.pos, sizeof gOwnPos[s.slot]); gOwnAt[s.slot] = frame;   // (beta 19, kero: the watch)
    if (b.resync) {
        b.resync = false;
        if (gHave[s.slot] && gSeen[s.slot].object && Ready() && (b.boxOff ? Number(gSeen[s.slot].pos) : OnMap(gSeen[s.slot].pos))) {   // (its x and z: the copy lay on its floor)
            s.pos[0] = gSeen[s.slot].pos[0]; s.pos[2] = gSeen[s.slot].pos[2];
            const float hips = Prone(s.k) && sup.moved ? vsup::kHipsLying : kHips;   // ((v150-host beta 23, review) still lying after a flinch)
            const float g = FloorNear(s.pos[0], s.pos[2], gSeen[s.slot].pos[1] - hips);   // (the copy's own floor)
            if (g > -1e29f) { b.floorY = g; s.pos[1] = g + hips; }
        }
        b.pauseUntil = frame + 6;
    }
    {                                                  // (beta 15) the item in its hands now: to its goal at once
        bool c = false;
        for (int i = 0; i < gObj.n; ++i) if (gObj.it[i].holder == s.slot) c = true;
        if (c && !b.carrying) {
            ++b.carries; b.path.clear(); b.next = 0; b.target = -1; b.aiming = false; b.pauseUntil = 0; b.planAt = 0;
            BLOG("vdoll %d: has the item", s.k + 1);
        }
        b.carrying = c;
        bool by = false;                               // (2 s by a free item not given: not its side's, 30 s)
        for (int i = 0; i < gObj.n && !c; ++i)
            if (gObj.it[i].holder < 0 && gObj.it[i].state == 0 && Dist2D(gObj.it[i].pos, s.pos) < 1000.f && std::fabs(gObj.it[i].pos[1] + kHips - s.pos[1]) < 2000.f) {
                by = true;
                if (b.nearItemSince < 0) b.nearItemSince = frame;
                else if (frame - b.nearItemSince > 120 && s.team >= 0 && s.team < 2 && !NotOurs(s.team, i)) {
                    gNotFor[s.team][i] = frame;
                    if (gDebug) BLOG("vdoll %d: the item %d not given to its side", s.k + 1, i + 1);
                }
            }
        if (!by) b.nearItemSince = -1;
    }
    // (v150-host beta 16) the round over: its fight, its grab and its throw end there (it hit, held and threw on through
    // the end's screens, where no player's game does), its way goes on
    const bool over = RoundOver();
    if (over && (b.target >= 0 || b.aiming || b.throwAt >= 0 || b.grabStart >= 0 || frame < b.rushUntil || frame < b.rollUntil)) {
        b.target = -1; b.aiming = false; b.pitch = 0; b.throwAt = -1; b.rollUntil = -1; b.rollInto = -1; StandUp(b); b.lean = 0;
        b.rushUntil = b.grabStart = b.grabAt = -1; b.grabbed = -1;
    }
    if (over) LowerRifle(b);                          // (beta 19)
    if (b.bookId && (over || !Ready())) { b.bookId = 0; b.bookReadyMs = RealMs() + (unsigned long long)vbook::kCooldownMs; b.bookEndF = frame; }
    if (sup.state && (over || !Ready())) { if (Prone(s.k) && sup.moved) s.pos[1] = b.floorY + kHips; sup.state = 0; sup.moved = false; }   // (v150-host beta 22, support)
    if (!over && Ready() && BookFrame(b, s, dt)) {}   // (v150-host beta 22, book) reading: still, turned to the book (pickup: a take's second record went above)
    else if (!over && Ready() && SupportFrame(b, s, sup, dt)) {}   // (v150-host beta 22, support) lying, crawling, getting up, laying (the same)
    else if (Ready() && over) {
        const bool moving = b.fallAt >= 0 || frame < b.landUntil || frame < b.climbUntil;
        if (moving) { if (b.fallAt >= 0) Fall(b, s, dt); else if (Vaulting(b)) Vault(b, s, dt); else b.speed = 0; }
        else Run(b, s, dt);
    } else if (Ready()) {
        const bool blind = Blind(b);                   // (beta 22, blast: white, it starts nothing and stands where it is)
        if (blind) b.blindF += dt;
        if (gCopyTrace && b.readNow)                   // ((v150-host beta 25, copyleash) benches: its body at the read, the floor's pace under it along its facing)
            BLOG("vdoll %d: copytrack f=%lld body %.0f %.0f %.0f pace %.0f speed %.1f way %d held %d fight %d", s.k + 1, frame, s.pos[0], s.pos[1], s.pos[2],
                 gPace ? GamePace(b, s, std::sin(b.yaw), std::cos(b.yaw)) * 6.f : 389.f, b.speed, b.next < b.path.size() ? 1 : 0, b.heldBack, b.target >= 0 ? 1 : 0);
        if (b.readNow) {                               // (every 100 ms, the players just read; (v150-host beta 25, dpass) once a read)
            ++dc.blocks; if (again) ++dc.blocks0;
            if (b.fallAt < 0 && frame >= b.landUntil && frame >= b.climbUntil) { const int h = b.holds + b.lates; ++dc.held; HeldBack(b, s); if (b.holds + b.lates != h) ++dc.heldFired; }
            ++dc.perceive;
            Perceive(b, s);
            if (RifleHolds(b) || blind || frame < b.avertUntil) {}   // (beta 19: from a scoped rifle no roll, grab, rush: its swap first; beta 22: turned away)
            else if (b.level >= 7 && b.target >= 0 && frame - b.hurtAt < 12 && frame >= b.nextRollAt && frame >= b.rollUntil && b.grabStart < 0 && Drew(dc.rollLooks) && Rnd01() < 0.35f) { if (StartRoll(b, s)) ++dc.rolls; }
            else if (CanGrab(b, s, kGrabReach) && frame >= b.rollUntil && (gCqcTest || (Drew(dc.grabLooks) && Rnd01() < 0.6f))) StartGrab(b, s);
            else if (b.target >= 0 && frame >= b.nextRollAt && frame >= b.rollUntil && b.throwAt < 0 && b.grabStart < 0 && frame >= b.rushUntil && Drew(dc.intoLooks) && Rnd01() < 0.3f) { if (StartRollInto(b, s)) ++dc.intos; }
            else if (b.target >= 0 && frame >= b.rollUntil && b.throwAt < 0 && (gCqcTest || b.reloadAt >= 0 || (b.level >= 8 && Drew(dc.rushLooks) && Rnd01() < 0.08f))) { if (StartRush(b, s)) ++dc.rushes; }
        }
        if (SupOn() && b.readNow) {                    // (v150-host beta 22, support: a claymore to take, a place to lay)
            const long long next = sup.nextCheck; const int st = sup.state;
            ++dc.supLooks; SupportChoose(b, s, sup);
            if (sup.nextCheck != next) ++dc.supTries;
            if (!st && sup.state) ++dc.supBegun;
        }
        PickupFrame(b, s);                             // (v150-host beta 22, pickup: a special item near it; the goggles)
        FoodFrame(b, s);                               // (v150-host beta 25, food: a food box near it, eaten as a player's game eats it)
        Goggles(b, s, dt);
        if (gTestRoll > 0 && frame == gSeenAt && dt > 0) TestRoll(b, s);   // ((v150-host beta 25, rolltime) benches only)
        const bool moving = b.fallAt >= 0 || frame < b.landUntil || frame < b.climbUntil;   // (a drop, its landing, a climb)
        if (moving) { if (b.fallAt >= 0) Fall(b, s, dt); else if (Vaulting(b)) Vault(b, s, dt); else b.speed = 0; b.aiming = false; }
        else if (b.throwAt < 0 && b.grabStart < 0 && frame >= b.rollUntil && frame >= b.rushUntil && b.readNow && !RifleHolds(b) && !blind) {
            const long long next = b.nextThrowAt;
            ++dc.throwLooks; StartThrow(b, s);
            if (b.nextThrowAt != next) ++dc.throwTries;
        }
        if (moving) {}
        else if (b.grabStart >= 0) Grab(b, s);
        else if (b.throwAt >= 0) Throw(b, s, dt);
        else if (frame < b.avertUntil) Avert(b, s, dt);   // (v150-host beta 22, blast: its own stun grenade's blast)
        else if (frame < b.rollUntil) Roll(b, s, dt);
        else if (b.rollInto >= 0) b.rollInto = -1;
        else if (frame < b.rushUntil) Rush(b, s, dt);
        else if (WakeFrame(b, s, dt)) {                // (v150-host beta 22, wake: a teammate knocked out or reading, no enemy in sight)
            // (v150-host beta 23, wake) its way to take a claymore or to lay one left for the teammate (their ways fought over its path)
            if (b.wakeTo >= 0 && (sup.state == 1 || sup.state == 5)) {
                if (sup.state == 1) BLOG("vdoll %d: leaves the claymore of player %d (a teammate to wake)", s.k + 1, (int)((sup.clay >> 10) & 0xFF) + 1);
                else BLOG("vdoll %d: leaves its laying place (a teammate to wake)", s.k + 1);
                if (sup.state == 5) sup.nextLayAt = frame + 600;
                sup.state = 0;
            }
        }
        else if (b.unarmed ? !Melee(b, s, dt) : !Engage(b, s, dt)) {   // (v150-host beta 21: Fight and its guard)
            if (b.aiming) { b.aiming = false; b.pitch = 0; }
            LowerRifle(b);                             // (beta 19: a scoped rifle down, then on its way; its reload where it stands)
            if (RifleHolds(b) || ShotHold(b) || KillHold(b) || blind) b.speed = 0; else Run(b, s, dt);   // (beta 21: its last shot's record first, as it fired; MGO1_VDOLL_KILL_HOLD; beta 22: white)
        }
    } else b.speed = 0;
    // ((v150-host beta 25, aimcost) its speed over this host frame, for its moves' cost: the host's game runs its actor lists twice for a
    // tick (docs/FPS60.md), so a host frame can pass here twice, the second time with no frame since (dt 0: Run moves it nothing and
    // its speed is 0 whatever it does); that pass's 0 is not its move
    if (dt > 0) b.moveSpeed = b.speed;
    {                                                  // ((v150-host beta 25, dpass) the debug count: its turn, a grab begun (the rush's too)
        const double turned = std::fabs(Wrap(b.yaw - yawWas)) * 180.0 / kPi;
        dc.turn += turned; if (again) dc.turn0 += turned;
        if (!grabWas && b.grabStart >= 0) ++dc.grabsBegun;
    }
    if (gDebug && !b.vaultSaid && b.vaultUntil >= 0 && frame >= b.vaultUntil + 30 && s.slot >= 0 && s.slot < 16) {   // (v150-host beta 24, snakehouse)
        b.vaultSaid = true;
        const float* cp = gSeen[s.slot].pos;
        BLOG("vdoll %d: its copy %.1f m from it 30 frames after its fence, %.1f m from the fence's far side (%s)", s.k + 1, Dist2D(cp, s.pos) / 1000.f,
             Dist2D(cp, b.vaultTo) / 1000.f, Dist2D(cp, b.vaultTo) < Dist2D(cp, b.vaultFrom) ? "over it" : "NOT over it");
    }
    if (gTurn) SpunNow(b, dt);
    b.aliveF += dt;
    // (v150-host beta 21) the measure of a standoff, whatever the switch: the frames it aims at a target with no shot and no
    // move (the waits between shots are in it: some frames; runs/V20_cap9 was 446 s). The body line says the longest
    if (b.aiming && b.target >= 0 && b.shots == shotsWas && b.rpgPullAt != frame + kRpgPull && std::hypot(s.pos[0] - was[0], s.pos[2] - was[1]) < 0.5f) {   // (heavy2, review22b H3: an RPG-7's pull is its shot)
        b.aimStill += dt;
        if (b.aimStill > b.aimStillMax) b.aimStillMax = b.aimStill;
        if (b.aimStill > b.aimStillLap) b.aimStillLap = b.aimStill;
    } else b.aimStill = 0;
    if (b.target >= 0) b.fightF += dt; else if (b.speed < 1.f) { if (b.waitCopy) b.copyWaitF += dt; else b.stillF += dt; }
    b.waitCopy = false;
    // (v150-host beta 16) its place checked before it is told: one that is no number or off the map (never seen; a
    // player's game cannot be there, and the host's game reads its collision at it) gives way to its last good one
    // (the review's P1-5) a life whose first place is a number outside the box: its spawn's place, which the game gave
    // or took. The box is wrong there, not the place: not asked for this life (it was silent, or kept there, to its death)
    if (!b.okHave && !b.boxOff && Number(s.pos) && !OnMap(s.pos)) {
        b.boxOff = true;
        BLOG("vdoll %d: this life begins at %.0f %.0f %.0f, outside the map's collision box: the box is not asked this life", s.k + 1, s.pos[0], s.pos[1], s.pos[2]);
    }
    if (b.boxOff ? Number(s.pos) : OnMap(s.pos)) { memcpy(b.okPos, s.pos, sizeof b.okPos); b.okHave = true; }
    else {
        if (!b.offSaid) { b.offSaid = true; BLOG("vdoll %d: its place %.0f %.0f %.0f is off the map: %s", s.k + 1, s.pos[0], s.pos[1], s.pos[2], b.okHave ? "back to its last one" : "not told"); }
        b.path.clear(); b.fallAt = -1; b.speed = 0;
        if (!b.okHave) return;
        memcpy(s.pos, b.okPos, sizeof b.okPos);
        b.floorY = s.pos[1] - kHips;
    }
    if (b.rpgPullAt >= 0 && over) b.rpgPullAt = -1;   // (heavy2, review22b H4: the round over, no rocket of its own on the end screens; its blast would be dropped)
    if (b.rpgPullAt >= 0 && frame >= b.rpgPullAt) LaunchRocket(b, s);   // (v150-host beta 22, heavy: the pull's end)
    if (s.tick) b.tickAt = frame;                     // (v150-host beta 21, cover)
    if (b.recs == 0 || s.tick) { ++dc.recs; if (again) ++dc.recs0; Records(b, s); }   // ((dpass) counted)
    gAvoidWays = nullptr;
}

void Down(int k) {
    if (k < 0 || k >= (int)gBodies.size()) return;
    Body& b = gBodies[k];
    if (b.target >= 0 && b.fightShots) BLOG("vdoll %d: its fight with player %d over (%d shots, %d hits)", k + 1, b.target + 1, b.fightShots, b.fightHits);
    b.target = -1; b.aiming = false; b.aimedLast = false; b.path.clear(); b.life = -2; b.throwAt = -1; b.rollUntil = -1;
    b.rushUntil = b.grabStart = b.grabAt = -1; b.grabbed = -1;
    b.rpgPullAt = -1;                                 // (v150-host beta 22, heavy: a pull cut by its death)
    b.bookId = 0;                                      // (beta 22, book)
    b.wakeTo = -1; b.wakeCombo = -1;                  // (v150-host beta 22, wake)
    if (b.slot >= 0 && b.slot < 16) gDollOut[b.slot] = DollOut();
    if (k < (int)gSups.size()) { gSups[k].state = 0; gSups[k].moved = false; gSups[k].pauseFrom = gSups[k].pauseLast = -1; }   // (beta 22, support)
}

// (v150-host beta 22, pickup) its death: the special items it holds dropped where it fell, as a player's game drops them (a
// record of its own number, slot << 10 | n, the kind and code, state 0, its feet's place in tenths, the rounds left; the host
// passes it on and every game makes the box), which any player can take again; a take's second record still due goes first
// (v150-host beta 22, pickup2: review22b P7) the same drops at its own leave, as a player's game drops them when he quits: the
// pause menu's QUIT (the list's row 7), YES to its question (the page 3, US n003a 0x3CFD10), goes to the page 10 (0x3D02DC),
// which calls 0x3A21C8 (from there only): it drops what he holds of the RPG-7, the flamethrower and the M63 (the list at
// 0x547778, each with its rounds: 0x3A1EA8, a record of kind 3) and the thermal goggles (0x3A1F68, kind 1), unless he is a
// ghost (state 0x156) or in the state 0x4E; his quit (0x751002) 6 frames later. A quit has no death: these are all he leaves
int DropSpecials(Body& b, const Self& s, bool quit) {
    if (!gPickup) return 0;
    int n = 0;
    float feet[3] = {s.pos[0], s.pos[1] - kHips, s.pos[2]};
    if (Ready()) { const float g = FloorNear(s.pos[0], s.pos[2], feet[1]); if (g > -1e29f && std::fabs(g - feet[1]) < 1500.f) feet[1] = g; }
    auto drop = [&](int kind, int code, int count, const char* name) {
        const int number = (s.slot << 10) | (0x100 + (gDropSeq[s.slot]++ % 0x300));
        std::vector<uint8_t> r;
        Put16(r, (uint16_t)number); Put16(r, (uint16_t)(kind << 10 | code)); r.push_back(0x00);
        PutTenths(r, feet); Put32(r, (uint32_t)count);
        s.send(s.ctx, s.slot, 3, r);
        ++gPick.dropped; ++n;
        BLOG("vdoll %d: drops its %s at %.0f %.0f %.0f (record %#x, %d %s), as a player's game drops it %s", s.k + 1, name, feet[0], feet[1], feet[2],
             (unsigned)number, count, kind == 1 ? "of them" : "rounds", quit ? "when he quits the game" : "at his death");
    };
    if (b.special >= 0 && b.specialRounds > 0) drop(3, kWeapons[b.special].code, b.specialRounds, kWeapons[b.special].name);
    int hr = 0;
    const int hc = HeavyHeld(s.k, &hr);                // (agent heavy's: the RPG-7, the flamethrower, their rounds left)
    // (agent heavy's: one the bench's MGO1_VDOLL_WEAPONS gave at its spawn is no box's: not dropped, as the game never placed
    // it; else a doll's every death would leave one more lying than the round's script put on the map)
    if ((hc == 0x11 || hc == 0x09) && hr > 0 && !b.heavyBench) drop(3, hc, hr, hc == 0x11 ? "RPG-7" : "flamethrower");
    else if ((hc == 0x11 || hc == 0x09) && hr > 0 && gDebug) BLOG("vdoll %d: its %s not dropped: the bench's (MGO1_VDOLL_WEAPONS), no box's", s.k + 1, hc == 0x11 ? "RPG-7" : "flamethrower");
    if (b.carryCode && b.carryRounds > 0) drop(3, b.carryCode, b.carryRounds, b.carryCode == 0x11 ? "RPG-7" : "flamethrower");
    if (b.goggles) drop(1, 0x07, 1, "thermal goggles");
    b.special = -1; b.specialRounds = 0; b.carryCode = b.carryRounds = 0; b.goggles = b.gogglesOn = false; b.pickNum = -1;
    b.heavyW = -1; b.heavyLeft = 0; b.heavyBench = false;   // (heavy's: dropped or the bench's, gone with its death)
    return n;
}
void Died(const Self& s) {
    if (!gOn || s.k < 0 || s.k >= (int)gBodies.size() || !s.send || s.slot < 0 || s.slot >= 16 || !s.pos) return;
    Body& b = gBodies[s.k];
    if (b.life < 0) return;
    SendOwed(b, s, gFrame, true);
    DropSpecials(b, s, false);
}
// (v150-host beta 22, pickup2: review22b P6) knocked down, held or flinching (no Alive then): its take's emptied record when due
void Owed(const Self& s, long long frame) {
    if (!gOn || s.k < 0 || s.k >= (int)gBodies.size() || !s.send || s.slot < 0 || s.slot >= 16) return;
    Body& b = gBodies[s.k];
    if (b.life >= 0) SendOwed(b, s, frame, false);
}
// (v150-host beta 22, pickup2: review22b P6, P7) its leave: its take's emptied record at once, and on its own quit (not the
// host's kick, where a player's game drops nothing) the special items it holds, as the pause menu's QUIT drops them (above)
bool Quit(const Self& s, bool own) {
    if (!gOn || s.k < 0 || s.k >= (int)gBodies.size() || !s.send || s.slot < 0 || s.slot >= 16 || !s.pos) return false;
    Body& b = gBodies[s.k];
    if (b.life < 0) return false;
    const bool owed = SendOwed(b, s, gFrame, true);
    return (own && DropSpecials(b, s, true) > 0) || owed;
}

void Pause(int k) {
    if (k < 0 || k >= (int)gBodies.size()) return;
    Body& b = gBodies[k];
    if (b.bookId) { b.bookId = 0; b.bookReadyMs = RealMs() + (unsigned long long)vbook::kCooldownMs; }   // (beta 22, book: knocked down or held)
    if (k < (int)gSups.size()) {                       // (beta 22, support: its crawl or its laying cut)
        // ((v150-host beta 23, review) lying: a hit's flinch leaves a player lying (his ground flinch), and a doll that got up after
        // one 1.3 m from a claymore set it off (tests/vdoll_support_test.cpp 9b): the state kept, Alive ends it after a longer pause
        // (knocked down, held), as before)
        Sup& u = gSups[k];
        if (Prone(k) && u.moved) { if (u.pauseFrom < 0 || gFrame - u.pauseLast > 12) u.pauseFrom = gFrame; u.pauseLast = gFrame; }
        else { u.state = 0; u.moved = false; u.pauseFrom = u.pauseLast = -1; }
    }
    b.target = -1; b.aiming = false; b.aimedLast = false; b.path.clear(); b.speed = 0; b.moveSpeed = 0; b.throwAt = -1; b.rollUntil = -1; StandUp(b); b.lean = 0;
    b.rushUntil = b.grabStart = b.grabAt = -1; b.grabbed = -1; b.fallAt = -1;
    b.rpgPullAt = -1;                                 // (v150-host beta 22, heavy: a pull cut by its fall)
    b.wakeTo = -1; b.wakeCombo = -1;                  // (v150-host beta 22, wake: knocked down, held, flinching: its waking over)
    // (v150-host beta 19, review19) its last shot's hit and shot records held for the next record (Records) are dropped, and
    // its scoped rifle is down: they went out when it was up again, seconds after the shot (tests/vdoll_scope_test.cpp
    // VDOLL_TEST_PAUSE), and a rifle raised before stayed raised (a shot 5 frames after it was up). MGO1_VDOLL_SCOPE=0: as beta 18
    if (gScope) { b.pending.clear(); b.gates.clear(); b.shotSince = false; b.raiseAt = b.scopeAt = -1; b.scopeRecs = 0; }
}
void Resume(int k) {
    if (k < 0 || k >= (int)gBodies.size()) return;
    gBodies[k].resync = true;
}
void Knocked(int k, int slot, bool lyingOut, int stamina, int perTick) {   // (v150-host beta 22, wake)
    if (slot < 0 || slot >= 16) return;
    (void)k;
    DollOut& o = gDollOut[slot];
    o.out = lyingOut; o.stamina = stamina; o.perTick = perTick;
}
bool Wake(int k, WakeInfo* out) {
    if (k < 0 || k >= (int)gBodies.size() || !out) return false;
    const Body& b = gBodies[k];
    out->to = b.wakeTo; out->why = b.wakeWhy; out->blows = b.wakeBlows; out->woken = b.woken;
    return true;
}
// (v150-host beta 22, support) its claymores the host's game says went off: the layer's wounding blast, from its own session
void Laid(const Self& s) {
    if (!SupOn() || s.k < 0 || s.slot < 0 || s.slot >= 16 || !s.send || gBooms.empty()) return;
    Sup& u = SupOf(s.k);
    for (size_t i = 0; i < gBooms.size();) {
        if ((int)((gBooms[i].id >> 10) & 0xFF) == s.slot) { ClayBlast(s, u, gBooms[i]); gBooms.erase(gBooms.begin() + (long)i); }
        else ++i;
    }
}
bool WayRunnable(const std::vector<doll::Waypoint>& w) { return w.size() >= 2 && WayOk(w, true); }   // (v150-host beta 22, support; tests)
void StairsAsRun(const doll::NavMesh& nav, std::vector<doll::Waypoint>& w) { StairsAsClimbsOn(nav, w); }   // (v150-host beta 24, snakehouse; tests)
int VaultsLeftOut(doll::NavMesh& nav) { return VaultsSecondLook(nav); }   // (v150-host beta 24, snakehouse; tests)
bool Support(int k, SupportInfo* out) {
    if (k < 0 || k >= (int)gSups.size()) return false;
    const Sup& u = gSups[k];
    out->clays = u.clays; out->books = u.books; out->taken = u.taken; out->laidClays = u.laidClays; out->laidBooks = u.laidBooks; out->item = u.item;
    out->state = u.state; out->known = (int)u.known.size(); out->blasts = u.blasts; out->blastHits = u.blastHits; out->prone = Prone(k) && u.moved;
    return true;
}

bool WeaponAt(int i, WeaponRow* out) {
    if (i < 0 || i >= kWeaponCount) return false;
    const Weapon& w = kWeapons[i];
    *out = WeaponRow{w.name, w.code, w.magazine, w.reload, w.every, w.automatic};
    return true;
}
bool FpvOnlyWeapon(int code) { return FpvCode(code); }
// (v150-host beta 22, heavy) a weapon box's RPG-7 or flamethrower taken by doll k (pickup's take of a box: the box's code and count),
// for the rest of its life (a player's game gives his loadout again at his respawn); its special weapon now
bool Give(int k, int code, int rounds) {
    if (k < 0 || k >= (int)gBodies.size() || gBodies[k].life < 0) return false;
    int w = -1;
    for (int i = 0; i < kWeaponCount; ++i) if (kWeapons[i].heavy && kWeapons[i].code == code) w = i;
    if (w < 0 || rounds <= 0) return false;
    Body& b = gBodies[k];
    if (b.heavyW == w) b.heavyLeft = std::min(30000, b.heavyLeft + rounds);
    else { b.heavyW = w; b.heavyLeft = std::min(30000, rounds); b.mag[w] = kWeapons[w].magazine; }
    b.heavyBench = false;                             // (a box's: dropped at its death, pickup's Died)
    BLOG("vdoll %d: has the %s (%d rounds)%s", k + 1, kWeapons[w].name, b.heavyLeft, gHeavy ? "" : ", never in its hands (MGO1_VDOLL_HEAVY=0)");
    return true;
}
int Special(int k, int* rounds) {
    if (rounds) *rounds = 0;
    if (k < 0 || k >= (int)gBodies.size() || gBodies[k].heavyW < 0) return 0;
    if (rounds) *rounds = gBodies[k].heavyLeft;
    return kWeapons[gBodies[k].heavyW].code;
}
void GrenadeTiming(int* ready, int* release, int* blastAfter) { *ready = kReady; *release = kReady + kRelease; *blastAfter = kBlastAfter; }
// (v150-host beta 24, stamina2) the game's posture code of its move now (vdoll_stamina.h: what a player's tick 0x4143F0 gives the
// engine for that move): 4 crawling (its support's crawl, moving), 3 running (its records' 0x40: a player's state 0x5002, flags 1,
// 0xC, 0xE; its rolls and ledge climbs as the run they are part of), 1 walking (0x20: 0x4802, the flag 0xB), 0 still, aiming,
// crouched or lying still. ((v150-host beta 25, aimcost) MGO1_VDOLL_MOVE_FRAME: 8 rolling; its move whether it aims or not)
int MoveCode(int k) {
    if (!gOn || k < 0 || k >= (int)gBodies.size()) return 0;
    const Body& b = gBodies[k];
    if (b.life < 0) return 0;
    // ((v150-host beta 25, aimcost) MGO1_VDOLL_MOVE_FRAME: its speed over the host's last frame, its crawl's too; its last pass's was 0 in
    // every host frame that ran it twice (bench aimcost_d1: half of a running doll's ticks counted still, 9 to 11 drains a minute of its
    // ticks where it ran 50 drains' worth)
    const float sp = gMoveFrame ? b.moveSpeed : b.speed;
    // ((its support: 2 lying down, 3 crawling, 4 getting up, 6 laying crouched: prone or still; 1 going to take and 5 going to lay
    // are runs, below)
    if (SupOn() && k < (int)gSups.size()) {
        const int st = gSups[k].state;
        if (st == 2 || st == 3 || st == 4 || st == 6) return st == 3 && sp > 1.f ? 4 : 0;
    }
    // ((v150-host beta 25, aimcost) a roll: the posture code 8 a player's game gives his (its states 0x4240 then 0x4642: the bit 9 with
    // 0xE), the stage's cost 98: measured on a real client, 134 a drain with a load of 7000 where its run's is 19 (bench aimcost_p1);
    // beta 25 counted it as the run (3). Its ledge climbs and fences stay the run's (a real client's not measured)
    if (RollPart(b) >= 2) return gMoveFrame ? 8 : 3;  // ((v150-host beta 25, rolltime) its roll and its getting up; the press's run: below)
    // ((v150-host beta 25, fallflinch) a drop's fall and its landing's hold: the posture code 8, as a player's game gives his ledge action
    // (0x41447C: the state bit 0x4A, set from the edge to the hold's end; ff_falls1: drains of 137 with a load near 7200 in three drops where
    // his run's were 19 and 20); b16 counted it still (its speed 0)
    if (gHitFall && (b.fallAt >= 0 || (b.landAt >= 0 && gFrame >= b.landAt && gFrame < b.landUntil))) return 8;
    if (gFrame < b.climbUntil) return 3;
    // ((v150-host beta 25, aimcost) and the move alone, as the game's code reads a player's move bits and no bit of his aim (0x4143F0):
    // a doll that aims stands (its fight's speed 0; aimcost_d1: none of its 1161 aiming ticks moving), so this changes nothing today;
    // beta 25 (MGO1_VDOLL_MOVE_FRAME=0) counted its aim as still whatever it did
    if (b.aiming && !gMoveFrame) return 0;
    return sp > kRun * 0.5f ? 3 : sp > 1.f ? 1 : 0;
}
bool MoveFrame() { return gMoveFrame; }
void Passes(int k, int* all, int* none) {
    *all = *none = 0;
    if (k < 0 || k >= (int)gBodies.size()) return;
    Body& b = gBodies[k];
    *all = b.passN; *none = b.pass0; b.passN = b.pass0 = 0;
}
bool OnePass() { return gOnePass; }                   // ((v150-host beta 25, dpass) MGO1_VDOLL_ONE_PASS; Decisions: the tests' counts)
bool Decisions(int k, DecInfo* out) {
    if (k < 0 || k >= 16 || !out) return false;
    const DecCount& c = gDec[k];
    out->passes = c.passes; out->pass0 = c.pass0; out->reads = c.reads; out->readsAgain = c.readsAgain; out->blocks = c.blocks; out->blocks0 = c.blocks0;
    out->held = c.held; out->heldFired = c.heldFired; out->rollLooks = c.rollLooks; out->rolls = c.rolls; out->grabLooks = c.grabLooks; out->grabsBegun = c.grabsBegun;
    out->intoLooks = c.intoLooks; out->intos = c.intos; out->rushLooks = c.rushLooks; out->rushes = c.rushes; out->throwLooks = c.throwLooks; out->throwTries = c.throwTries;
    out->turn = c.turn; out->turn0 = c.turn0;
    return true;
}
// (v150-host beta 25, aimcost) what its body does at a move tick, for the debug count of its moves (vdoll.cpp MoveCostTick): the
// MoveWhy codes of vdoll_body.h
int MoveWhy(int k) {
    if (!gOn || k < 0 || k >= (int)gBodies.size()) return MW_OFF;
    const Body& b = gBodies[k];
    if (b.life < 0) return MW_OFF;
    if (SupOn() && k < (int)gSups.size()) {
        const int st = gSups[k].state;
        if (st == 2 || st == 3 || st == 4 || st == 6) return st == 3 && (gMoveFrame ? b.moveSpeed : b.speed) > 1.f ? MW_CRAWL : MW_PRONE;
    }
    if (RollPart(b) >= 2 || gFrame < b.climbUntil) return MW_ROLL;   // ((v150-host beta 25, rolltime) the press's run a run)
    const float sp = gMoveFrame ? b.moveSpeed : b.speed;
    if (b.aiming) return sp > 1.f ? MW_AIM_MOVING : MW_AIM;
    if (sp > kRun * 0.5f) return MW_RUN;
    if (sp > 1.f) return MW_WALK;
    return b.next < b.path.size() ? MW_WAY : MW_STILL;
}
// its load as a player's game weighs his (vdoll_stamina.h, the page's weights): the knife, its guns (none unarmed), its special
// weapon, its support item (its grenades, claymores or books) and the items every soldier's list holds
int Load(int k) {
    if (k < 0 || k >= (int)gBodies.size()) return 0;
    const Body& b = gBodies[k];
    int w = vstam::WeaponWeight(vstam::kKnife) + vstam::kItemsLoad;
    int guns[3] = {-1, -1, -1};
    // (a special weapon taken where it lay: an M63 in its primary's place in the body, the player's list keeps his own gun with it)
    const int prim = b.special >= 0 && b.primary == b.special ? b.soldierPrimary : b.primary;
    if (!b.unarmed) { guns[0] = kWeapons[prim].code; guns[1] = kWeapons[b.secondary].code; if (b.longGun >= 0) guns[2] = kWeapons[b.longGun].code; }
    for (int i = 0; i < 3; ++i) {
        bool twice = false;
        for (int j = 0; j < i; ++j) twice |= guns[j] == guns[i];
        if (guns[i] > 0 && !twice) w += vstam::WeaponWeight(guns[i]);
    }
    if (b.heavyW >= 0) w += vstam::WeaponWeight(kWeapons[b.heavyW].code);
    if (b.special >= 0) w += vstam::WeaponWeight(kWeapons[b.special].code);
    const int item = SupOn() && k < (int)gSups.size() ? gSups[k].item : vsup::ITEM_GRENADE;
    w += vstam::WeaponWeight(item == vsup::ITEM_CLAYMORE ? vsup::kClayCode : item == vsup::ITEM_BOOK ? vsup::kBookCode : b.nade);
    return w;
}

std::string Status(int k) {
    if (k < 0 || k >= (int)gBodies.size()) return std::string();
    const Body& b = gBodies[k];
    char s[800];
    snprintf(s, sizeof s, "vdoll %d body: level %d, %s, %d ways (%d run to their end, %.0f m run, held back %d times, its copy moving behind %d), %d fights, %d shots, %d hits; "
             "alive %.0f s, fighting %.0f %%, still out of a fight %.0f %% (and %.0f %% its copy catching up); %d ways to the objective (%d round the enemies), %d items carried; %d crouched, %d leans, %d rolls (%d into an enemy), %d grabs, %d grenades (%d hits); %d drops, %d climbs, %d fences crossed", k + 1, b.level,
             b.target >= 0 ? (b.aiming ? "aiming" : "reacting") : b.next < b.path.size() ? "running" : "still", b.goals, b.reached, b.ran / 1000.f,
             b.holds, b.lates, b.fights, b.shots, b.hits, b.aliveF / 59.94, b.aliveF ? 100.0 * b.fightF / b.aliveF : 0.0,
             b.aliveF ? 100.0 * b.stillF / b.aliveF : 0.0, b.aliveF ? 100.0 * b.copyWaitF / b.aliveF : 0.0, b.objWays, b.roundAbouts, b.carries, b.crouches, b.leans, b.rolls, b.rollHits, b.grabs, b.throws, b.blastHits, b.drops, b.climbs, b.vaults);
    std::string scope;
    // (v150-host beta 21) the weapon in its hands; its fire held with no shot (out of the waits a player has too), by reason, and
    // the moves the guard made instead (Engage), its M37's closings in (Unstall)
    {
        std::string why;
        long long all = 0;
        for (int r = 1; r < 8; ++r) all += b.holdF[r];
        for (int r = 1; r < 8; ++r)
            if (b.holdF[r] >= 6) why += (why.empty() ? "" : ", ") + std::string(kHoldName[r]) + " " + std::to_string((int)lrint(b.holdF[r] / 59.94)) + " s";
        char w[400];
        snprintf(w, sizeof w, "; its %s%s", b.unarmed ? "fists" : kWeapons[b.held].name, gStandoff ? "" : " (MGO1_VDOLL_STANDOFF=0)");
        scope += w;
        if (all >= 6) { snprintf(w, sizeof w, ", its fire held %.0f s with no shot (%s)", all / 59.94, why.c_str()); scope += w; }
        if (b.unstalls || b.closeIns) { snprintf(w, sizeof w, ", %d moves instead, %d closings in with its M37", b.unstalls, b.closeIns); scope += w; }
        {                                                  // (v150-host beta 24, cqcfight)
            const CqcFightCount& q = gCqcFightCount[k & 15];
            if (q.freed || q.stops || q.kept || q.noWake || q.noGrab || q.over || q.nades || q.left) {
                snprintf(w, sizeof w, "; a teammate near its line: %lld asks free of his body, %d bullets stopped by his body, %d targets kept, %d wakes left for the enemy at him, %d grabs not tried (crouched or lying); %d ways to him another way, %d grenades, %d left",
                         q.freed, q.stops, q.kept, q.noWake, q.noGrab, q.over, q.nades, q.left);
                scope += w;
            }
        }
        snprintf(w, sizeof w, "; its longest aim with no shot and no move %.1f s (%.1f s since the last line)", b.aimStillMax / 59.94, b.aimStillLap / 59.94);
        scope += w;
    }
    if (b.heavyW >= 0 || b.rockets || b.flames)   // (v150-host beta 22, heavy)
        scope += "; its " + std::string(b.heavyW >= 0 ? kWeapons[b.heavyW].name : "special weapon") + " (" + std::to_string(b.heavyLeft) + " rounds left): " + std::to_string(b.rockets) +
                 " rockets (" + std::to_string(b.rocketHits) + " players in their blasts, " + std::to_string(b.heavyHolds) + " held: its own feet or a teammate), " +
                 std::to_string(b.flames) + " moments of flame (" + std::to_string(b.flameHits) + " burns)";
    if (b.scopeRaises) scope += "; its scoped rifle raised " + std::to_string(b.scopeRaises) + " times, " + std::to_string(b.scopeShots) + " shots from its scope, " + std::to_string(b.copyWaits) + " frames its shot waited for its copy";   // (beta 19)
    if (b.coverDrops || b.reflexHolds || b.shownHolds)   // (v150-host beta 21, cover)
        scope += "; " + std::to_string(b.coverDrops) + " hits not counted (out of its line where the player really is or can be: " + std::to_string(b.coverAtRec) +
                 " at the place he last sent), its first shots held " + std::to_string(b.reflexHolds) + " frames for the reflex, its shots " + std::to_string(b.shownHolds) +
                 " frames for its own copy to be in the player's line (" + std::to_string(b.shownLate) + " fights fired with it still hidden after a second)";
    if (b.muzzleStops || b.muzzleViews)   // (v150-host beta 24, walls3)
        scope += "; " + std::to_string(b.muzzleStops) + " third person bullets met by the cover before its gun, its view taken in " + std::to_string(b.muzzleViews) + " fights for it";
    if (b.wakeBlows) scope += "; " + std::to_string(b.wakeBlows) + " blows to wake teammates (" + std::to_string(b.woken) + " up or freed by them)";   // (v150-host beta 22, wake)
    if (gWakeWalls || gWakeRound)                  // ((v150-host beta 25, wakeway) all the dolls': blows a wall stopped, ways taken round to his side)
        scope += "; wakes: " + std::to_string(gWakeWalls) + " blows met a wall, " + std::to_string(gWakeRound) + " ways round to his side";
    if (gWakeSlopeRuns)                            // ((v150-host beta 25, wakeramp) all the dolls': run-ups taken up or down a slope)
        scope += "; wakes: " + std::to_string(gWakeSlopeRuns) + " run-ups up or down a slope";
    if (k < (int)gSups.size() && (gSups[k].taken || gSups[k].laidClays || gSups[k].laidBooks || gSups[k].clays || gSups[k].books || gSups[k].blasts)) {   // (v150-host beta 22, support)
        const Sup& u = gSups[k];
        char w[240];
        snprintf(w, sizeof w, "; %d claymores taken crawling, %d claymores and %d books laid, %d claymores of its gone off (%d players in their blasts), carries %d claymores and %d books",
                 u.taken, u.laidClays, u.laidBooks, u.blasts, u.blastHits, u.clays, u.books);
        scope += w;
    }
    if (b.takes || b.special >= 0 || b.goggles) {      // (v150-host beta 22, pickup)
        char w[200];
        snprintf(w, sizeof w, "; %d special items taken, %d kills with them%s%s%s%s", b.takes, b.specialKills, b.special >= 0 ? ", holds the " : "",
                 b.special >= 0 ? kWeapons[b.special].name : "", b.carryCode ? (b.carryCode == 0x11 ? ", carries an RPG-7" : ", carries a flamethrower") : "",
                 b.goggles ? (b.gogglesOn ? ", its thermal goggles on" : ", its thermal goggles off") : "");
        scope += w;
        if (b.special >= 0) scope += " (" + std::to_string(b.specialRounds) + " rounds)";
    }
    if (b.flashes) { char w[120]; snprintf(w, sizeof w, "; %d stun grenades' flashes, nothing seen %.1f s in all", b.flashes, b.blindF / 59.94); scope += w; }   // (beta 22, blast)
    if (!gPlayerCtl) return s + scope;
    const CtlCount& c = gCtlCount[k & 15];
    char p[200];
    snprintf(p, sizeof p, "; as a player: %d views entered, hits %d from the third person (%d heads made body hits), %d from the view (%d heads)",
             c.entries, c.tpsHits, c.bodyNotHead, c.viewHits, c.viewHeads);
    return std::string(s) + p + scope;
}

const char* Controls() { return gPlayerCtl ? "player" : "direct"; }

bool WayTo(const float from[3], const float to[3], float* len, float end[3]) {   // (v150-host beta 22, pickup; tests)
    if (!Ready()) return false;
    std::vector<doll::Waypoint> w;
    float l = 0;
    if (!FindWay(from, to, w, &l) || w.size() < 2 || !WayOk(w)) return false;
    *len = l; memcpy(end, w.back().p, 3 * sizeof(float));
    return true;
}
// (v150-host beta 25, wakeway; tests) the goal of the way doll s.k would take from its place to wake player id, and its length (its
// body's way and counts untouched)
bool WakeGoal(const Self& s, int id, float goal[3], float* len) {
    if (!Ready() || s.k < 0 || s.k >= (int)gBodies.size() || id < 0 || id >= 16 || !gHave[id] || !gSeen[id].object) return false;
    Body b = gBodies[s.k];
    const long long round = gWakeRound;
    float l = 0;
    const bool ok = WakeWay(b, s, gSeen[id], false, &l) && WakeWay(b, s, gSeen[id], true) && !b.path.empty();
    gWakeRound = round;
    if (!ok) return false;
    memcpy(goal, b.path.back().p, 3 * sizeof(float));
    *len = l;
    return true;
}
bool Pickup(int k, PickupInfo* out) {             // (v150-host beta 22, pickup; tests)
    if (k < 0 || k >= (int)gBodies.size() || !out) return false;
    const Body& b = gBodies[k];
    out->weapon = b.special >= 0 ? kWeapons[b.special].code : 0; out->rounds = b.specialRounds; out->takes = b.takes; out->kills = b.specialKills;
    out->goggles = b.goggles; out->gogglesOn = b.gogglesOn;
    return true;
}
// (v150-host beta 25, food) the stun gauge doll k's last eating gave back, once (vdoll.cpp adds it to its own); 0 none
int FoodEaten(int k) {
    if (k < 0 || k >= (int)gBodies.size()) return 0;
    Body& b = gBodies[k];
    const int g = b.foodGain;
    b.foodGain = 0;
    return g;
}
bool Food(int k, FoodInfo* out) {                  // (v150-host beta 25, food; tests)
    if (k < 0 || k >= (int)gBodies.size() || !out) return false;
    const Body& b = gBodies[k];
    out->to = b.foodNum; out->eats = b.eats; out->gains = b.foodGains;
    return true;
}

bool Aim(int k, AimInfo* out) {
    if (k < 0 || k >= (int)gBodies.size() || !out) return false;
    const Body& b = gBodies[k];
    out->aiming = b.aiming; out->state = b.ctl; out->yaw = b.yaw; out->pitch = b.pitch; out->pullAt = b.pullAt; out->lastBullet = b.lastBullet;
    out->stateUntil = b.ctlUntil; out->shots = b.shots; out->target = b.target; out->head = b.head;
    return true;
}

}  // namespace body
}  // namespace vdoll
