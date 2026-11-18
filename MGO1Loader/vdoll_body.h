// vdoll_body.h - a virtual doll's body (docs/VIRTUAL_DOLL.md 7): the map's navigation mesh in the host's process
// (one for all the virtual dolls, built on a thread when the map changes), its runs on the mesh at the game's
// speeds, its look, its aim and its shots, sent as a PC's game sends its player's records.
#pragma once
#include "vdoll.h"
#include "doll_nav.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vdoll {
namespace body {

// a virtual doll alive in the match this frame, as vdoll.cpp holds it
struct Self {
    int k = 0;                                    // its number (0 based)
    int slot = -1, team = 0;
    long long life = -1;                          // its life (the frame it deployed): another one starts afresh
    bool tick = false;                            // its 100 ms tick: its records go now
    int hp = -1, attacker = -1;                   // its life as the host gives it, the player who hurt it last (-1 none)
    float* pos = nullptr;                         // its place (hips), moved here
    uint16_t* facing = nullptr;
    // a record for a player's channels (slot, kind 0 to 2), sent with the doll's next datagram; (v150-host beta 22, pickup) kind 3:
    // the host's drops' channel (the room's less 2), slot unused; (v150-host beta 25, food) kind 6: the mushroom man's (the room's + 11)
    void (*send)(void* ctx, int slot, int kind, const std::vector<uint8_t>& rec) = nullptr;
    void* ctx = nullptr;
};

void Init(const Host& h);                         // MGO1_VDOLL_BODY (1, 0: phase 1's still dolls), MGO1_VDOLL_LEVEL
bool On();
// (v150-host beta 22, bookplay) its state record's third byte: 2 while it reads a book (its copies read with it on every screen:
// MGO1_VDOLL_BOOK_POSE), else 0 (vdoll.cpp StateTick sends 4 for a fall first)
int StateEvent(int k);
#define VDOLL_BODY_BOOK_POSE 1                    // (v150-host beta 22, tests: StateEvent and the reading's records are there)
void Frame(long long frame);                      // once a frame: the map's mesh, the players seen
void Alive(const Self& s, long long frame);       // a doll alive: its moves, look, aim and shots, its records
void Down(int k);                                 // dead or out of the match: its fight over
// (v150-host beta 22, pickup) dead: the special items it took dropped where it fell, as a player's game drops them (records on
// the drops' channel, Self::send kind 3); before Down
void Died(const Self& s);
// (v150-host beta 22, pickup2) knocked down, held or flinching (no Alive): its take's emptied record when due (6 frames after
// its claim), as a player's game completes the take whatever befalls him
void Owed(const Self& s, long long frame);
// (v150-host beta 22, pickup2) its leave, before Down: its take's emptied record at once; own (its own quit, not the host's kick):
// the special items it holds dropped where it stands, as the game's pause menu QUIT drops them; true: records sent (they go before
// its leave)
bool Quit(const Self& s, bool own);
#define VDOLL_BODY_QUIT 1                         // (v150-host beta 22, pickup2, tests: Owed and Quit are there)
// (v150-host beta 22, pickup, tests) what a doll holds of the special items: its special weapon's code (0 none) and rounds, the
// thermal goggles had and worn, the specials it took and the kills made with them
struct PickupInfo { int weapon = 0, rounds = 0, takes = 0, kills = 0; bool goggles = false, gogglesOn = false; };
bool Pickup(int k, PickupInfo* out);
bool WayTo(const float from[3], const float to[3], float* len, float end[3]);   // (tests) a way the bodies run (floors), its length and end
#define VDOLL_BODY_PICKUP 1                       // (v150-host beta 22, tests: Died and Pickup are there)
// (v150-host beta 22, wake) doll k in seat slot as vdoll.cpp holds it, each frame it is in the match: knocked out and lying (its
// stun gauge now, what it gains each 6 frames), else not; a teammate's body may wake it with the game's blows (docs/VIRTUAL_DOLL.md 36)
void Knocked(int k, int slot, bool lyingOut, int stamina, int perTick);
#define VDOLL_BODY_WAKE 1                         // (v150-host beta 22, tests: Knocked and Wake are there)
// (v150-host beta 22, wake, tests) doll k's teammate waking: the teammate it goes to (-1 none), why (1 knocked out, 2 reading),
// its blows given in all, the teammates it woke or freed
struct WakeInfo { int to = -1, why = 0, blows = 0, woken = 0; };
bool Wake(int k, WakeInfo* out);
// (v150-host beta 25, wakeway, tests) the goal of the way doll s.k would take from its place to wake player id (a teammate lying or
// reading), and its length: with MGO1_VDOLL_WAKE_SIDE a place on his side of the wall he lies against
bool WakeGoal(const Self& s, int id, float goal[3], float* len);
#define VDOLL_BODY_WAKE_SIDE 1                    // (v150-host beta 25, tests: WakeGoal is there)
// (v150-host beta 22, support: docs/VIRTUAL_DOLL.md 35) each frame for a doll in the match, alive or dead: its claymores the host's
// game says went off wound as the layer's own game wounds (records on its player channels, Self::send kinds 0 to 2); kind 4 is the
// claymores' list's channel (the room's + 4), kind 5 the books' (the room's + 10)
void Laid(const Self& s);
// (tests) what a doll does with the support row: its claymores and books carried, taken, laid, its item this life (0 the
// grenade, 1 the claymore, 2 the book), its state now (0 none, 1 going to take, 2 lying down, 3 crawling, 4 getting up, 5 going
// to lay, 6 laying), the claymores it remembers, the blasts it made and the players its blasts reached
struct SupportInfo { int clays = 0, books = 0, taken = 0, laidClays = 0, laidBooks = 0, item = 0, state = 0, known = 0, blasts = 0, blastHits = 0; bool prone = false; };
bool Support(int k, SupportInfo* out);
// (tests) a way the bodies can run (its links: the drops, the climbs of a metre): the places' search plans its ways with it
bool WayRunnable(const std::vector<doll::Waypoint>& w);
#define VDOLL_BODY_SUPPORT 1
// (v150-host beta 24, snakehouse, tests) the bodies' rules on a test's own mesh (built with NavConfig::doorways as the bodies'
// with MGO1_VDOLL_STAIRS on): a way's stairs of blocks made into a player's climbs and drops (every way a body runs, with
// MGO1_VDOLL_STAIRS), and the fences no player crosses left out of the paths (the playable dolls' second look, with
// MGO1_VDOLL_VAULT; their count); the switches are the test's to follow
void StairsAsRun(const doll::NavMesh& nav, std::vector<doll::Waypoint>& w);
int VaultsLeftOut(doll::NavMesh& nav);
#define VDOLL_BODY_STAIRS 1
void Pause(int k);                                // (beta 15) knocked down or held: its fight and way over, its life kept
void Resume(int k);                               // up again: on from where the host's copy is
std::string Status(int k);
// (v150-host beta 18, tests) the weapons table: its i-th row (false past the end): its name, code, magazine, reload and
// shot pace (frames), and the grenades' own timing (the release after the throw's start, the blast after the release)
struct WeaponRow { const char* name; int code, magazine, reload, every; bool automatic; };
bool WeaponAt(int i, WeaponRow* out);
void GrenadeTiming(int* ready, int* release, int* blastAfter);
// (v150-host beta 19) a first person only weapon by the game's code (SVD 0x0F, MOSIN NAGANT 0x10, RPG-7 0x11): a player with
// one in his hands does not move, its scope or sight opens before a shot (docs/VIRTUAL_DOLL.md 20)
bool FpvOnlyWeapon(int code);
#define VDOLL_FPV_ONLY 1
// (v150-host beta 22, heavy) a weapon box's special weapon taken by doll k (the box's code and count: the RPG-7 0x11, the
// flamethrower 0x09; false for another code, a doll not alive): it holds it in a fight by the rules of docs/VIRTUAL_DOLL.md 33
// (MGO1_VDOLL_HEAVY=0: never) until its death. Its special weapon now: the code (0 none) and its rounds left
bool Give(int k, int code, int rounds);
int Special(int k, int* rounds);
#define VDOLL_BODY_HEAVY 1
// (v150-host beta 24, stamina2) what its moves cost its stamina as a player's (vdoll_stamina.h): the game's posture code of its move
// now (4 crawling, 3 running, 1 walking, 0 still) and its load (the weights of what it carries, as a player's game weighs his)
int MoveCode(int k);
int Load(int k);
#define VDOLL_BODY_MOVECODE 1
// (v150-host beta 25, aimcost) what its body does now, for the debug count of its moves: off (not alive), prone still, crawling,
// a roll or a climb, aiming still, aiming while it moves, running, walking, still with a way left (a pause, a turn, a wait for
// its copy), still with no way
enum { MW_OFF, MW_PRONE, MW_CRAWL, MW_ROLL, MW_AIM, MW_AIM_MOVING, MW_RUN, MW_WALK, MW_WAY, MW_STILL, MW_COUNT };
int MoveWhy(int k);
// (v150-host beta 25, aimcost) MGO1_VDOLL_MOVE_FRAME (on): its moves read from its speed over the host's whole frame; (debug) its body's
// passes since the last call and those with no host frame since the one before (the host's game runs its actor lists twice a tick)
bool MoveFrame();
void Passes(int k, int* all, int* none);
// (v150-host beta 25, dpass) MGO1_VDOLL_ONE_PASS (on): a doll's every-100-ms decisions once a read of the players (its first pass of the
// read's host frame), no turn in a pass with no frame since the last. (tests) doll k's counts since the start, whatever its lives: its
// passes (those with no frame since the last), the players' reads it took and its other passes in a read's frame, its decisions' runs
// (those in a pass with no frame since the last), HeldBack's runs and holds, the chances drawn (looks) and what they began, the grenade's
// looks (those past its own pace), its turn in degrees (in passes with no frame since the last)
struct DecInfo {
    long long passes = 0, pass0 = 0, reads = 0, readsAgain = 0, blocks = 0, blocks0 = 0, held = 0, heldFired = 0;
    long long rollLooks = 0, rolls = 0, grabLooks = 0, grabsBegun = 0, intoLooks = 0, intos = 0, rushLooks = 0, rushes = 0, throwLooks = 0, throwTries = 0;
    double turn = 0, turn0 = 0;
};
bool OnePass();
bool Decisions(int k, DecInfo* out);
#define VDOLL_BODY_DPASS 1
// (v150-host beta 25, food: docs/VIRTUAL_DOLL.md 44) the food lying on the map eaten as a player's game eats it (Host::foods; the
// record on the mushroom man's channel goes as Self::send kind 6, the room's + 11): the stun gauge doll k's last eating gave back,
// once (vdoll.cpp adds it to its own, clamped at 30000); 0 none. (tests) the food it goes to (its entry's number, -1 none), the
// foods it ate and what they gave back in all
int FoodEaten(int k);
struct FoodInfo { int to = -1, eats = 0, gains = 0; };
bool Food(int k, FoodInfo* out);
#define VDOLL_BODY_FOOD 1
// (v150-host beta 18) MGO1_VDOLL_CONTROLS: "player" (the virtual dolls through a player's controls) or "direct"; a doll's aim
// as the controls hold it (the tests): its state 0 lowered, 1 readying, 2 readied (third person), 3 the first person view
// coming up, 4 in it (always 0 with direct)
const char* Controls();
// (v150-host beta 19) the live level (MGO1_VDOLL_ADAPT=1, vdoll_adapt.h): doll k's level now (its offset in, within lo .. hi)
// and its part against each player (levels, by player id); on false: its level set, as before. Its level set (MGO1_VDOLL_LEVEL)
void Live(int k, bool on, float level, int lo, int hi, const float foe[16]);
int LevelSet(int k);
#define VDOLL_BODY_LIVE 1                         // (v150-host beta 19, tests: Live and LevelSet are there)
struct AimInfo { bool aiming = false, head = false; int state = 0, shots = 0, target = -1; float yaw = 0, pitch = 0; long long pullAt = -1, lastBullet = -1000, stateUntil = -1; };
bool Aim(int k, AimInfo* out);
// (v150-host beta 19, kero, tests) a place where the game held a doll back twice, kept off by the ways (as HeldBack keeps one)
void KeepOff(const float* at);
// (v150-host beta 21, spin) a respawn point (its floor) the body's ways can leave (a way of 10 m or more from it); true when
// it cannot tell (no mesh yet) and with MGO1_VDOLL_TURN=0
bool CanLeave(const float* feet);
#define VDOLL_BODY_CANLEAVE 1                     // (v150-host beta 21, tests: CanLeave is there)
#define VDOLL_BODY_KEEPOFF 1                      // (v150-host beta 19, tests: KeepOff is there; tests/vdoll_controls_same.sh builds the test on beta 18's body)
// (v150-host beta 21, geom, tests) a line as the bodies test it on the map loaded: a sight's (the stage's collision, then
// every thing the host's game made), a shot's of the weapon of that code (the stage's collision, then the things as hard
// as its bullet or harder); the points of a player a sight tests (his chest, his head) by his stance
bool SightStopped(const float a[3], const float b[3]);
bool ShotStopped(const float a[3], const float b[3], int weaponCode);
void BodyPoints(const PlayerNow& p, float chest[3], float head[3]);
#define VDOLL_BODY_LINES 1                        // (v150-host beta 21, tests: the three above are there)

}  // namespace body
}  // namespace vdoll
