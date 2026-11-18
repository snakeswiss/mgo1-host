// vdoll.h - the virtual dolls: players the dedicated host runs itself, with no game of their own
// (docs/VIRTUAL_DOLL.md). Each one joins the host's own game as a PC joiner does: its datagrams are made
// here (the session's cipher and signature, the records) and handed to the game's recvfrom as from an
// address of its own (198.51.100.<n>, a documentation range no player has); the host's datagrams to it
// are read in the clear at the packet recorder (0x178928) and never leave the machine.
// No platform code: the platform (ps2world.cpp) calls these and gives the host's state through Host.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vdoll {

// a player of the host's session as the platform sees it
struct Seen {
    int id = -1;                                  // the game's player id
    int kind = 0;                                 // 0 a real player, 1 the dedicated host, 2 a doll (full or virtual), 3 connecting
    int team = 0xFF;                              // 0 red, 1 blue, 0xFE a spectator or the host, 0xFF none yet
    uint32_t uid = 0;                             // its user id
};

// (the body, vdoll_body.h) a player of the host's game as the host's own game holds him now
struct PlayerNow {
    int team = 0xFF;                              // 0 red, 1 blue, 0xFF none, 0xFE the host
    bool object = false;                          // in the world
    bool alive = false;                           // his life over 0, not a ghost, not held at his spawn
    int life = 0;
    float pos[3] = {0, 0, 0};                     // his hips
    int style = 0;                                // his style record (0x40 running, 0x20 walking; low nibble 2 crouched)
    int cls = -1;                                 // (v150-host beta 16) his class (+0xB62: 8 Snake), -1 no object
    bool asleep = false;                          // (v150-host beta 18) knocked out: asleep or stunned (the state flag 0x99)
    bool camo = false;                            // (v150-host beta 18) a TEAM SNEAKING sneaker whose stealth camouflage works now
                                                  // (his flag 0x96, no alert: tsne.h TsneStealthed)
    // (v150-host beta 21, cover) the place he really sent (his last position record; pos is the host's copy, which the game
    // moves on by his input between records) and its age in ms of real time (-1: none from him, a virtual doll's own)
    float sent[3] = {0, 0, 0};
    int sentAge = -1;
    int rtt = -1;                                 // (beta 21) the session's own round trip to him in ms (-1: none known)
    unsigned state = 0;                           // (v150-host beta 21, geom) his first state word as the host's copy holds it: its
                                                  // bit 3 lying on his front (still, crawling, thrown down), its bit 2 crouched
    bool bookRefused = false;                     // (v150-host beta 22, book) one of the state bits a player's book check refuses
                                                  // (vdoll_book.h Refused) on the host's copy of him
    // (v150-host beta 22, wake) reading a book: the host's copy's +0xDE9 (every copy has it from the reader's own record {0E 1
    // ...}, US n003a 0x442FF8); his stun gauge's maximum (+0xB6A) and the ally wake's part of it (+0xB74, 100: all of it at
    // once, 0x438A30), as a teammate's game reads them on its copy of him
    bool reading = false;
    int stunMax = 0, wakePart = 0;
    int stamina = -1;                             // (v150-host beta 22, wake) his stun gauge now on the host's copy (+0xB68: his own game's,
                                                  // kept on every copy), -1 not known
    // (v150-host beta 25, reactfix) his yaw on the host's copy (+0x142, 16 bits a turn) and the yaw he turns to (+0x14A): what a
    // shooter's game reads on its copy of him to pick the reaction of its hit (vdoll_react.h); -1 not known
    int yaw = -1, yawTo = -1;
};
#define VDOLL_PLAYERNOW_YAW 1                     // (v150-host beta 25, tests: PlayerNow::yaw and yawTo are there)
#define VDOLL_PLAYERNOW_WAKE 1                    // (v150-host beta 22, tests: PlayerNow::reading, stunMax and wakePart are there)
#define VDOLL_PLAYERNOW_BOOK 1                    // (v150-host beta 22, tests: PlayerNow::bookRefused is there)
// (v150-host beta 22, book) a book lying in the host's game (docs/VIRTUAL_DOLL.md 30): an entry of its book list
struct BookNow {
    uint32_t id = 0;                              // the game's id of it (its layer's slot << 10, a count)
    int slot = -1;                                // its layer's slot
    float pos[3] = {0, 0, 0};                     // its place (on the floor)
    int readTime = 0;                             // the read time a reading starts on it, in the game's passes (60 a second on a
                                                  // player's own game; 0 not known)
};
#define VDOLL_PLAYERNOW_STATE 1                   // (v150-host beta 21, tests: PlayerNow::state is there)
// (v150-host beta 21, geom) where the host's game's own line test stopped a line: the place, the face's attribute word, and,
// when a thing the game made while the round runs holds the face (a tree trunk, a prop, a cardboard box, a player's shield:
// not in the stage's collision file), that thing's name word and how hard it is for a bullet (a bullet goes through a thing
// softer than its power)
struct LineHit {
    float at[3] = {0, 0, 0};
    uint32_t attr = 0;
    bool object = false;
    uint32_t objectId = 0;
    int hardness = 220;                           // (a face of the stage itself: 220, more than any bullet's power)
};
#define VDOLL_PLAYERNOW_SENT 1                    // (v150-host beta 21, tests: PlayerNow::sent and sentAge are there)
#define VDOLL_PLAYERNOW_ASLEEP 1                  // (v150-host beta 18, tests: PlayerNow::asleep is there)
#define VDOLL_PLAYERNOW_CAMO 1                    // (v150-host beta 18, tests: PlayerNow::camo is there)

// (v150-host beta 15, the body) a mission object (KEROTAN, GA-KO, the microfilm) as the host's game holds it: the host
// gives it to the nearest allowed player within reach, carries it with him, drops it at his fall and scores it in a goal
struct ObjNow {
    float pos[3] = {0, 0, 0};
    int state = 0;                                // 0 free, 1 held, 2 dropped (falling, not to be taken yet), 3 in a goal
    int holder = -1;                              // the player holding it (-1 none)
    int goalTeam = -1;                            // the team whose goal holds it (-1 none)
};

// (v150-host beta 22, pickup) a record of the host's game's drops (the round's special items, the weapons and items players
// dropped): its number (-1 none), its kind (3 a weapon, 1 an item, 0 taken, 2 coming back) and code, its count (rounds), its
// state (0 lying), its word as the game sends it (kind << 10 | code), its place, its box in the world now and its reach
struct DropNow { int number = -1, kind = 0, code = 0, count = 0, state = 0; unsigned flags = 0; float pos[3] = {0, 0, 0}; bool box = false; float reach = 0; };
#define VDOLL_HOST_DROPS 1                        // (v150-host beta 22, tests: Host::drops is there)
// (v150-host beta 22, support) a claymore lying in the host's game (docs/VIRTUAL_DOLL.md 35): an entry of its claymores' list
// (NewOnlineClaymoreMan) in state 1: its id (its layer's slot << 10 | 0x100 + n), its layer's slot, its place on the floor,
// its facing (radians, atan2 x z, from its matrix), the record's last word
struct ClayNow { uint32_t id = 0; int slot = -1; float pos[3] = {0, 0, 0}; float yaw = 0; uint32_t word = 0; };
#define VDOLL_HOST_CLAYS 1                        // (v150-host beta 22, tests: Host::claymores and Host::clayBlasts are there)
// (v150-host beta 25, food) a food of the host's game (docs/VIRTUAL_DOLL.md 44): an entry of its mushroom man (NewOnlineMushRoomMan):
// its number (0x100 + n), its state (1 on its wall, 2 lying, 3 taken), the food's code (0x5B GOLOVA, 0x56 RUSSIAN GLOWCAP), the
// stun gauge eating it gives back (a player's game: 30000 x its class's share), the host's time units left before it is back,
// the entry's point (its records carry it), its box in the world now at pos with its reach
struct FoodNow { int number = -1, state = 0, code = 0, gain = 0, timer = 0; float at[3] = {0, 0, 0}, pos[3] = {0, 0, 0}; bool box = false; float reach = 0; };
#define VDOLL_HOST_FOODS 1                        // (v150-host beta 25, tests: Host::foods is there)

// what the platform gives
struct Host {
    void (*log)(const char* fmt, ...) = nullptr;
    int (*gen)() = nullptr;                       // the game network's round number byte (0x28BBCB)
    // a player of the game by its id: its team (0 red, 1 blue, 0xFF none, 0xFE the host), its life and its
    // place as the host's game holds them; false when that id has no player
    bool (*player)(int id, int* team, int* life, float pos[3]) = nullptr;
    // a respawn point of the map loaded for that team (0 red, 1 blue, else the free-for-all ones): its floor; false: none
    bool (*spawnPoint)(int team, unsigned pick, float out[3]) = nullptr;
    int (*players)(Seen* out, int max) = nullptr; // the session's players now (their count)
    int (*rule)() = nullptr;                      // the round's rule (0 DM, 1 TDM, 2 rescue, 3 capture, 4 sneaking, 5 team sneaking,
                                                  // 6 base capture), -1 not known
    int (*phase)() = nullptr;                     // the room's phase (1 before a round, 2 playing, 4 over, 5 the next one coming)
    int (*seats)() = nullptr;                     // the players the game takes besides the host, 0 not known yet
    int (*roomChannel)() = nullptr;               // the room's channel in the host's game (0x62, or 0x72: its game objects moved)
    // (the body) the map loaded now: its page and extra map; false: none (the lobby)
    bool (*mapNow)(std::string* page, int* extra) = nullptr;
    // (the body) a map's collision (its .geom), read on the body's own thread
    bool (*stageGeom)(const std::string& page, int extra, std::vector<uint8_t>* out, std::string* err) = nullptr;
    // (the body) a player of the host's game by his id; false: no such id
    bool (*see)(int id, PlayerNow* out) = nullptr;
    // (beta 15, the body) the mission objects now (their count, 4 at most) and the two teams' goals (red, blue; set false: none
    // in this rule for that team)
    int (*objectives)(ObjNow* out, int max, float goals[2][3], bool set[2]) = nullptr;
    // (beta 15, the body) BASE CAPTURE's bases now: their places, owners (-1 none, 0 red, 1 blue), contested; their count
    int (*bases)(float pos[5][3], int owner[5], int contested[5]) = nullptr;
    // (v150-host beta 16) the class of that player as the host's game holds him (0 KGB, 1 GRU, 2 the Ocelot Unit, 3 to
    // 7 a special character, 8 Snake); -1: no object of his now
    int (*playerClass)(int id) = nullptr;
    // (v150-host beta 16) the class of that team's soldiers in this round (the game's units); -1 not known
    int (*teamClass)(int team) = nullptr;
    // (v150-host beta 16, the configurations' audit) the game's weapons setting (0 all, 1 no primary, 2 no secondary, 3 no
    // support, 4 primary only, 5 secondary only, 6 support only, 7 knife only); -1 not known
    int (*weaponRule)() = nullptr;
    // (v150-host beta 16, the configurations' audit) the game's headshot rule (a game named "HEAD SHOT...": the room's flag
    // 0x800, a kill that is not a headshot not counted)
    bool (*headshotsOnly)() = nullptr;
    // (v150-host beta 16) true: the host's game checks the character a spawn asks for (one the round does not hold keeps the
    // copy's present one, ps2world.cpp): a class told from a guess is then safe. False or none: nothing is guessed
    bool (*spawnGuard)() = nullptr;
    // (v150-host beta 19, the live level: docs/VIRTUAL_DOLL.md 19) a player's counters in this match as the host's game holds
    // them (his variable set: kills, deaths, his VS RATING); false: no such player. Read only with MGO1_VDOLL_ADAPT on
    bool (*score)(int id, int* kills, int* deaths, int* rating) = nullptr;
    // (v150-host beta 19) the room's round wins and tickets (red, blue); false: not known
    bool (*room)(int wins[2], int tickets[2]) = nullptr;
    // (v150-host beta 19, kero) mission object i, lying free, put back at its home as the game's own return does (capture's,
    // 180 s after nobody holds it: the manager's home point copied into its place); false: not done (no such item, held)
    bool (*itemHome)(int i) = nullptr;
    // (v150-host beta 21, geom) a line through the map as the host's game's own collision has it (its line test, the faces
    // that stop a bullet). stage false: only the things the game made while the round runs (they are not in the stage's
    // collision file, which the bodies hold whole); true: the stage's own faces too (the comparison of MGO1_VDOLL_GEOM_AUDIT).
    // 1 blocked (the nearest hit in *hit when asked), 0 clear, -1 not asked (no game map, not on the game's thread)
    int (*gameLine)(const float a[3], const float b[3], bool stage, LineHit* hit) = nullptr;
    // (v150-host beta 21, cover) the machine's clock in ms (real time, not the game's: the host's frames are longer when it is
    // slowed); none: the frames at 59.94 a second (tests)
    unsigned long long (*realMs)() = nullptr;
    // (v150-host beta 21, tests: MGO1_VDOLL_TEST_TRAP) the traps lying in the host's game (claymores, books): their places, their count
    int (*traps)(float out[][3], int max) = nullptr;
    // (v150-host beta 22, book) the books lying in the host's game, in its list's order (the entries a player's own game reads
    // for his book action): their count
    int (*books)(BookNow* out, int max) = nullptr;
    // (v150-host beta 22, book) a book a doll reads: its read time started in the host's game as a reader's own game starts it
    // (once a book); at its end the game takes the book away and tells the players. false: no such book in the list
    bool (*readBook)(uint32_t id) = nullptr;
    // (v150-host beta 22, book) a doll's reading over by the machine's clock (a slowed host's game counts the read time slower
    // than a player's own game): the book's read time in the host's game ended now, so that the game takes it away at its next
    // pass and tells the players, as at the end of a reader's own read time. false: no such book
    bool (*bookDone)(uint32_t id) = nullptr;
    // (v150-host beta 22, pickup) the drops' records of the host's game now (their count, 16 at most): what a virtual doll may
    // take as a player's game takes it (vdoll_body.cpp, docs/VIRTUAL_DOLL.md 32)
    int (*drops)(DropNow* out, int max) = nullptr;
    // (v150-host beta 22, blast) the smoke grenades' clouds the host's game holds now (its NewNetworkSmokeManager: a middle, a
    // radius, the time left, 5 a frame), their count; and its stun grenades' blasts (NewStunBlast: each copy's place, an id
    // the blast keeps while it lives), their count
    int (*smokes)(float c[][3], float r[], int left[], int max) = nullptr;
    int (*stunBlasts)(uint32_t id[], float at[][3], int max) = nullptr;
    // (v150-host beta 22, wake) the game's friendly fire setting (the host's settings at the game's creation): 1 on, 0 off, -1 not known.
    // Measured on real clients (docs/VIRTUAL_DOLL.md 36): with it off a player's punches and kick do not reach a teammate
    int (*friendlyFire)() = nullptr;
    // (v150-host beta 22, support) the claymores lying in the host's game (its list's entries in state 1, their count), and the
    // host's copies of a virtual doll's claymores that went off since the last call (their ids and places, their count): the
    // layer's own game would make the wounding blast then, so the doll does (docs/VIRTUAL_DOLL.md 35)
    int (*claymores)(ClayNow* out, int max) = nullptr;
    int (*clayBlasts)(uint32_t id[], float at[][3], int max) = nullptr;
    // (v150-host beta 25, food) the food of the host's game now (its mushroom man's entries, their count, 16 at most): what a
    // virtual doll may take and eat as a player's game does (vdoll_body.cpp, docs/VIRTUAL_DOLL.md 44)
    int (*foods)(FoodNow* out, int max) = nullptr;
};
#define VDOLL_HOST_FF 1                           // (v150-host beta 22, tests: Host::friendlyFire is there)
#define VDOLL_HOST_BOOKS 1                        // (v150-host beta 22, tests: Host::books, Host::readBook and Host::bookDone are there)
#define VDOLL_HOST_BLASTS 1                       // (v150-host beta 22, tests: Host::smokes and Host::stunBlasts are there)
#define VDOLL_HOST_GAME_LINE 1                    // (v150-host beta 21, tests: Host::gameLine is there)
#define VDOLL_HOST_SCORE 1                        // (v150-host beta 19, tests: Host::score and Host::room are there)

void Init(const Host& h);                         // reads MGO1_VDOLLS (how many) and the others (vdoll.cpp)
bool On();
void Tick(long long frame);                       // once a frame, on the game's thread (the actor pass)
void HostGameGone(long long frame);               // the host's game ended: every doll joins the next one
void SetDialect(uint32_t key, uint32_t mac);      // the game's set: its bootstrap key and its MAC constant
bool Ours(uint32_t ip, uint16_t port);            // a virtual doll's address (ip in network order)
// the virtual dolls' user ids (MGO1_VDOLL_IDS, the first one): set before Init by the platform's settings, read by the
// lobby filter and the players' kinds; k from 0
uint32_t FirstId();
int Count();                                      // MGO1_VDOLLS (15 with MGO1_VDOLL_LIVE: the ids kept for them)
int Wanted();                                     // the virtual dolls wanted now
bool IsId(uint32_t uid);                          // a virtual doll's user id
int NumberOfId(uint32_t uid);                     // its number (1 ..), 0 none
// the host's datagram to a virtual doll, in the clear as the packet recorder has it: [seq 2][records]
void HostClear(uint32_t ip, uint16_t port, const uint8_t* clear, size_t n);
// the next datagram of a virtual doll for the game's session socket (the recvfrom of 0x16E440)
bool Pop(std::vector<uint8_t>& dgram, uint32_t* ip, uint16_t* port);
std::string Status();                             // one line per doll (the log's "vdoll:" lines)
std::string StatusJson();                         // the status file's "vdolls" entry: [{"n","state","player","team",...}]
// (v150-host beta 19) the live level: on, its bounds; each doll's level now (-1: not in the game or the live level off)
bool AdaptOn(int* lo, int* hi);
float LevelNow(int k);

// the session's datagrams (tests): made with a key pair, read back
std::vector<uint8_t> Seal(uint16_t seq, const std::vector<uint8_t>& body, uint32_t payloadKey, uint32_t macKey);
bool Open(const std::vector<uint8_t>& dgram, uint32_t payloadKey, uint32_t macKey, uint16_t* seq, std::vector<uint8_t>* body);

}  // namespace vdoll
