// The PLAYABLE DOLL's insides (doll.h), shared by its parts: doll.cpp (what it sees, how it moves, its
// status and orders), doll_brain.cpp (what it decides: fights, objectives, orders, menus),
// doll_probe.cpp (the measures of the controls, in tests).
#pragma once
#include "doll.h"
#include "doll_geom.h"
#include "doll_nav.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace doll {
namespace in {

// ---- the run ----
extern bool gOn;
extern std::string gDir;                 // MGO1_DOLL: status, commands
extern int gProbe, gDebug;
extern bool gObserve;
extern Pad gPad;                         // this frame's pad
const Pad& LastPad();                    // ((v141 beta 9) the pad of the frame before)
extern bool gControl;                    // the doll drives the pad this frame
extern long long gFrame;
unsigned Rnd();
float Rnd01();                           // 0 .. 1

// ---- its settings and orders ----
struct Settings {
    std::string game, gamePassword;      // the game to join (its bot's), its password lock
    int difficulty = 3;                  // (v141 beta 6) the ladder: 1 TRAINING .. 5 EXPERT (as before), 6 ELITE (beta 5's NIGHTMARE),
                                         // 7 VETERAN, 8 MASTER, 9 EXTREME, 10 PRO
    std::string team = "auto";           // auto, red, blue, spectator
    std::string order = "free";          // free, objective, target, hide, defend, follow
    std::string target;                  // the player of "order target" / "order follow"
    int bot = 0, number = 0;
    bool leave = false;                  // leave the game now (a real player needs the seat)
    std::string loadout;                 // the equipment it asks for (docs/PLAYABLE_DOLL.md)
    bool adapt = true;                   // the live level (its side's balance, its foe's play); off: the difficulty set
    int adaptMin = 1, adaptMax = 10;     // (v141 beta 6) the live level's bounds (the server's own control)
    int parity = 0;                      // (v150-host beta 16) 1: difficulty N plays by the VIRTUAL DOLL's level N (MGO1_DOLL_PARITY=vdoll,
                                         // the ini's parity=vdoll; doll_brain.cpp kVdollLevel), the live level off; 0: as before
    int aim = 0;                         // (v150-host beta 18) 1: the direct aim (MGO1_DOLL_AIM=direct, the ini's aim=direct, the order
                                         // `aim direct`): its facing and its view turned by writing their angles (doll_aim.h); 0: the pad
};
bool Parity();                           // (v150-host beta 16) gSet.parity
std::string ParitySaid();                // (v150-host beta 16) its numbers at the difficulty set, for the log ("" off)
bool DirectAim();                        // (v150-host beta 18) gSet.aim
std::string DirectAimSaid();             // (v150-host beta 18) the direct aim's line for the log (its rate at the level)
extern Settings gSet;

// ---- the game's memory ----
uint32_t R32(uint32_t a);
uint16_t R16(uint32_t a);
int16_t R16s(uint32_t a);
uint8_t R8(uint32_t a);
float RF(uint32_t a);
const uint8_t* GameMem();                // (v150-host beta 18) its own game's memory (the direct aim's own search of the view's place)
void W16(uint32_t a, int v);             // (v150-host beta 18) the direct aim's only write: its own player's turn, its own view's angles

// ---- the map ----
struct MapState {
    std::string page;                    // n001a ...
    int extra = 0;                       // an extra map (13 ..) on that stage: its own geometry (docs/EXTRA_MAPS.md)
    std::string key;                     // the map: its page, an extra map's "<page>_x<n>" (the learning, the notes)
    std::unique_ptr<Geom> geom;
    std::unique_ptr<NavMesh> nav;        // the player's radius (375): every passage
    std::unique_ptr<NavMesh> wide;       // a wider one (500): the paths keep off the walls where they can
    std::atomic<int> state{0};           // 0 none, 1 building, 2 ready, 3 failed
    std::string log;
};
extern std::shared_ptr<MapState> gMap;
bool MapReady();

// ---- what it sees ----
struct Me {
    uint32_t obj = 0;
    float pos[3] = {0, 0, 0};            // hips (800 above the ground standing, 300 low)
    int life = 0, stun = 0;
    bool alive = false;                  // (v141 beta 3) its life over 0 and not a ghost
    bool ghost = false;                  // (v141 beta 3) a ghost of the one-life rules (state word 10, 0x400000), its life 0 or 200
    bool frozen = true;                  // spawned but the round has not started (state word 7, 0x1000000)
    uint32_t bits[11] = {};              // the player's state words (work + 0xB2C)
};
extern Me gMe;
bool Jammed();                           // a chaff grenade out: no radio, no radar (the game's status word)

struct Player {
    int id = -1;                         // the game's player id (0..15)
    uint32_t obj = 0;
    bool local = false;
    float pos[3] = {0, 0, 0};            // hips
    int life = 0, stun = 0;
    int team = 0xFF;                     // 0 red, 1 blue, 0xFF none, 0xFE the dedicated host
    std::string name;
    bool alive = false, down = false;    // (v141 beta 3) alive: its life over 0 and not a ghost
    bool ghost = false;                  // (v141 beta 3) a ghost of the one-life rules: no weapon, ignored by the living
    uint32_t bits[11] = {};
    float facing = 0;                    // radians (atan2 x, z)
    float vel[2] = {0, 0};               // its move on the ground, mm a frame (x, z; a few frames' average)
};
extern std::vector<Player> gPlayers;
extern int gMyId, gMyTeam;
extern int gRule;                        // 0 DM, 1 TDM, 2 rescue, 3 capture, 4 sneaking (-1 unknown)
bool Enemy(const Player& p);             // never an ally, never a player whose team is not known (but in DM)
bool Ally(const Player& p);
const Player* PlayerById(int id);
const Player* PlayerByName(const std::string& name);   // case does not matter
std::string MyName();
int RealPlayers();                       // (v141 beta 6) the real players of the session table (not the host, a doll), debounced
bool PresenceWaiting();                  // (v141 beta 6) WAITING_FOR_REAL: no real player (not with MGO1_DOLL_PLAY_ALONE, a probe)
bool InSession(int id);                  // (v141 beta 6) a player id with a session entry now (true before the first reading)
const char* SpectatorReason();           // (v141 beta 6) "ordered", "no-real-player", or "" (it plays)
int RealLoading();                       // (v141 beta 13) the real players still loading (no team yet), each his first minute
long long RealTeamKnownAt();             // (v141 beta 13) the frame the latest real player's team became known, -1 none
int DollsInSession();                    // (v141 beta 13) the dolls in the session, this one too
bool IsDollUid(uint32_t uid);            // (v141 beta 6) an accountless doll's user id (0x7F000001 .. 0x7F000040)
bool IsDollName(std::string n);

// the room (the variable set 0, docs/DEDICATED_HOST.md): +0 the phase, +2 the rotation ({map, rule} a
// pair, 16 at most; +0x36 the entry played), +0x30 its flags (0x800: headshots only, a game named
// "HEAD SHOT..." or "HEAD_SHOT..."), +0x3C the seconds left, +0x42 red {round wins u8, team u8,
// tickets u16}, +0x46 blue
struct Room { int phase = -1, map = -1, rule = -1, entry = -1, secondsLeft = -1; uint32_t flags = 0; int wins[2] = {0, 0}, tickets[2] = {0, 0}; };
extern Room gRoom;
const char* RuleName(int r);
bool HeadshotsOnly();                    // the game's headshot rule (a body shot's kill is not counted)

struct Camera { float fwd[2] = {0, 1}, right[2] = {1, 0}; bool ok = false; };
extern Camera gCam;

// ---- geometry ----
float Facing();                          // the local player's facing (radians)
float FacingOf(uint32_t obj);
float AngleTo(const float* from, const float* to);
float Wrap(float a);
float Dist2D(const float* a, const float* b);
float HipHeight();                       // the hips above the ground under them
bool LineClear(const float* a, const float* b);        // nothing of the map's collision between a and b

// ---- moving ----
void Steer(float dx, float dz, float speed);           // the left stick for a world direction
bool PlanTo(const float* goal);
bool PlanFlank(const float* goal, bool habit);   // a trip's way among the map's (habit: its taste too, else only off the allies' ways)
void NewWays();                          // the doll's own ways drawn again (a map, a life): its lane, its flank side
int HabitSide();                         // its side this life (1, -1): its flanks, its place by one it follows
float HabitAngle();                      // its bearing (radians, by its number and this life): its sector around what it guards
bool OldWays();                          // (tests, MGO1_DOLL_OLD_WAYS) v134's ways: no flank, lane, place by one followed, sector
bool OldFight();                         // (tests, MGO1_DOLL_OLD_FIGHT) v135's fights: no cover to reload, move, fall back, feint, corner held
void KeepOff(const float* p);            // a point its lane keeps 2 m off this frame (an item a guard must not take)
bool PlanNear(const float* p, float maxR);   // no way to p: the nearest reachable point within maxR of it
bool PathServes(const float* p);         // the path followed goes to p (1.5 m) or near it (PlanNear)
bool OtherPart(const float* p);          // p on another part of the mesh than the doll (no path ever)
bool PathTrapped(const std::vector<Waypoint>& path);   // a path across a trap of the mesh learned (a place the player is stopped)
float WayLen(const float* to);           // the brain's: a path's length from the doll as GoTo takes it (-1: none, or a link on it)
int MagNow();                            // the brain's: the magazine of the gun in the hands (-1 not known)
bool ProbeAimAt(const Player& p, int part, float* ey, float* ep, float* ry, float* rp, float above = 0.f);   // the brain's view aim (probe 31's precision step; above > 0: that share of his hips' height above them)
void ProbeAimReset();                    // the view aim's rates afresh
bool EscapeFrame();                      // on a pocket of the mesh (a few polygons, no link out): the ways out tried (true: the pad)
void EscapeForget();                     // (probe 31) the escape's state forgotten
void FollowPath();                       // along the path planned (posture, walls, getting unstuck)
bool Following();                        // a path is being followed
size_t PathCorners();                    // (v141 beta 3) the corners of the path planned (probe 43: a way up the mesh has none)
bool PathHasLink(uint8_t area);          // (v141 beta 3) the path planned takes a link of that area
float NextLinkDist();                    // (v141 beta 3) the way left to the path's next link (0 on one, 1e30 none within 30 m)
std::string PathState();                 // (v141 beta 3) the path's state in a line (the still watch)
bool OnLinkNow();                        // (v141 beta 6) the path's segment now is a link's (a ladder, stairs, a climb, a crossing)
bool UnstickOn();                        // (v141 beta 6) the still watch's ways out (MGO1_DOLL_UNSTICK=0: none)
void UnstickStart();                     // (v141 beta 6) the ways out begun from here
int UnstickFrame(bool moving);           // (v141 beta 6) 1 the pad theirs, 2 at work (the path's pad), 0 over, -1 they ran out
void UnstickGaveUp();                    // (v141 beta 6) they ran out here: twice at a place, its leg left out for the map
const std::string& ActCode();            // (v141 beta 3) its activity's code
bool PathNear(const float* p, float r);  // the path left passes within r of p
void StopPath();
const float* PathGoal();
const float* PathAsked();                // (v141 beta 3) the point the path was asked for
bool OnLadder();                         // on a ladder (the state word 2: 0x40000)
bool LadderFrame();                      // on a ladder out of a path: the climb goes on (true: the pad)
bool ForceCrossing(const float* a, const float* b, uint8_t area);   // (probe 24) a vault / a roll from here
extern float gCrossRollAt;               // the roll pressed this far before a window (mm)
bool Ground(float* out, uint8_t* area = nullptr);       // the point of the mesh under the doll
bool RandomReachable(float radius, float* out);         // a random point of the mesh reachable from here
bool Low();                                             // crouched, lying or crawling
bool Prone();                                           // lying on its front (the state word 0)
bool SetLow(bool wantLow);                              // Cross toward crouching / standing: true when there
long long LifeFrames();                                 // (v137) frames since this life's deploy (the brain's first free frame; -1 none yet)
struct NavStats { int goals = 0, reached = 0, stuck = 0, replans = 0, failed = 0; };
extern NavStats gNavStats;

// ---- status ----
void Act(const char* code, const std::string& arg = std::string());   // what it does (the program says it)
[[noreturn]] void ExitNow(int code);     // the status written, the process ends (the service decides what next)
extern int gKills, gDeaths;

// ---- the brain (doll_brain.cpp) ----
bool LobbyFrame();                       // in the lobby: its game found in the Join Game list (true: the pad)
void BrainNewMap();                      // a map's page came in
void BrainFrame();                       // in a match, alive, in the world: the pad
void MenuFrame();                        // in a match, out of the world (a menu, dead, spectating): the pad
void BrainWatchFrame();                  // hanging from a ledge (doll.cpp's inputs): the brain's watches only
const char* BrainState();                // for the status
float LevelShown();                      // the level it plays at now (1.0 .. 10.0)
const char* LevelWhy();                  // why: even, behind (stronger), ahead (softer), fixed (adapt off)
std::string AdaptJson();                 // (v141 beta 6) the status's "difficulty" block: base, effective, tier, the estimates
const char* TierName(float level);       // (v141 beta 6) TRAINING .. PRO
std::string RespawnJson();               // (v141 beta 6) the status's respawn fields
std::string FightJson();                 // (v141 beta 6) the status's aim mode, FPV state, last decision
std::string FpvJson();                   // (v141 beta 6) the status's first person view weapon block (SVD, MOSIN NAGANT, RPG-7)
void OnKillMessage(uint32_t code, uint32_t arg);        // the game's kill / stun messages (0x7510xx)
int RestWeapon();                        // the weapon held out of a fight
// (v137) the ladders' side of the brain (doll.cpp's ladder code)
int LadderThreat(const float* end, float* where, int* who, float* ago);   // known enemies covering a floor point: count, the nearest
bool LadderExitLook(const float* at, float* look);      // off a ladder there: the likely threat (the fight's pre-aim set 2 s)
bool CarryingObjective();                // the objective's item in its hands

bool ProbeSuicideFrame();                // (probes) the pause menu's SUICIDE: true while it has the pad
int SupportNow();                        // the support row's item taken on the weapons page
bool ProbeGrenadeAt(int id, float* predicted);   // (probe 19) a grenade at a player: true while it runs
bool ProbeWindowTo(int win, int code);   // (probes) a window's code taken as the brain does: true while it works
bool ProbeIAmSnake();                    // ((v141 beta 9) probes) Snake in sneaking
int ProbeMyType();                       // ((v141 beta 9) probes) the character's type (3 to 7 special, 8 Snake)
bool ProbeThrowAt(const float* point, int code, float* predicted);   // (probe 31) a grenade of a kind at a point
bool ProbeTeamChangeFrame();             // (probe 31) TEAM SELECT's TEAM CHANGE: true while it has the pad
bool ProbeTakeObjective();               // (probe 31) the nearest free objective item taken: true while not held
std::string ProbeObjectives();           // (probe 31) the objective items: where, who holds them

// ---- the inventory (doll_brain.cpp): the game's own lists, as its windows build them. The local
// player's equipment [0x23A678]: +0x5AC the weapons' count, +0x5B4 their codes, +0x5B0 the items'
// count, +0x5C4 their codes (s16), +0x5D4 the weapon in the hands, +0x5D6 the item; each weapon's and
// item's state in the map's page (0x38 bytes a code): +0x18 a weapon's rounds (under 0: not had), an
// item's count (0: not had). The page's tables are found in its code (docs/PLAYABLE_DOLL.md 3) ----
struct Inventory {
    bool ok = false;
    std::vector<int> weapons, items;     // the codes the windows list
    int rounds[0x40];                    // a weapon's rounds by code (-1 not had)
    int count[0x40];                     // an item's count by code (0 not had)
    int weapon = -1, item = -1;          // in the hands
};
const Inventory& Inv();                  // read once a frame
uint32_t WeaponState(int code);          // a weapon's state in the page (0: not found)
uint32_t ItemState(int code);            // an item's
uint32_t EquipBase();                    // [0x23A678] (0: none)
int MyScore(int offset);                 // the doll's own score word (its variable set: +0x36 CQC)

// ---- the radio (doll_radio.cpp): MGO1's codec calls, made with the pad as a player makes them and heard
// from its radio's channel. The phrases in the order of the game's texts (a voice's first + the phrase) ----
enum Phrase {
    PH_GO, PH_COVER, PH_FOLLOW_ME, PH_CIRCLE, PH_HIT, PH_GRENADE, PH_DEFEND, PH_PULL_OUT, PH_HOLD, PH_TOGETHER,
    PH_TAKE_COVER, PH_ROGER, PH_NEGATIVE, PH_ENEMY, PH_CLEAR, PH_COME_HERE, PH_SORRY, PH_NICE, PH_DONT_DIE,
    PH_COUNTING, PH_HUH, kPhrases
};
extern const char* const kPhraseNames[kPhrases];       // in the log ("follow me" ...)
struct Call { int from = -1, team = -1, phrase = -1; };
void RadioFrame();                       // every frame in a match: its channel watched
bool RadioHeard(Call* c);                // a call read from the network since the last (the oldest first)
bool RadioOnMenu(int phrase);            // the phrase is on its radio's list (the voice's table)
bool RadioSay(int phrase, const std::string& why, bool urgent = false);   // a call to make now (false: busy, not on the list)
bool RadioBusy();                        // a call asked, not made yet
bool RadioPressing();                    // its presses begun (SELECT tapped): no window meanwhile
bool RadioUrgent();                      // an urgent call asked (made even from the first person view)
bool RadioPad(bool calm, bool quiet);    // the call's presses this frame (calm: no window, menu, throw; quiet: no shot)
void RadioStop(const char* why);         // the call given up (the doll dead, a menu)
uint32_t RadioHud();                     // (v141 beta 6) the page's radio work (0: none): +0x180 its flags, +0x78C its state
bool ChatDebug();                        // (v141 beta 6) MGO1_DOLL_CHAT_DEBUG=1: each SELECT and the text chat's opening and closing
bool RadioCloseFrame(bool may);          // (v141 beta 6) the radio's menu left on with no call: closed (SELECT to the chat, the chat guard's SELECT out)
long long RadioCloseAt();                // (v141 beta 6) the frame of the last close's SELECT (the chat guard: the chat it opened)

// ---- probes (doll_probe.cpp): measures of the controls, in tests ----
bool ProbeFrame(bool mapPage);           // true: the probe had the frame

}  // namespace in
}  // namespace doll
