// vdoll.cpp - the virtual dolls (vdoll.h, docs/VIRTUAL_DOLL.md)
//
// The session: the datagrams sealed and opened as a PC joiner's game does (docs/VIRTUAL_DOLL.md 2); a record: channel,
// flags, length, [sequence], data.
// The join, as a PC joiner's game does it (the host's P2P log of a doll's join): connect {nonce, 3, user id} on channel 0;
// the host's number back; the hello {2, 12 zero bytes, name} on channel 1; the host's members (object messages, type 7),
// then op 2 of object 0: the membership asked (0x0A); the host's config blocks (0x0B): joined. In the match (the map's
// page): the claim {0x81, round, own id} every 64 frames until the host's 0x83; the pick {0x82, round, team, character};
// the host's 0x85 (each player's team) and the player's object (type 7: id, kind, ?, life, its channel, place, facing).
// The leave: {0x01} on channel 1 (the host's player machine 0x175B60 in 0x216 removes the player at once: 0x173600).
#include "vdoll.h"
#include "vdoll_body.h"
#include "vdoll_adapt.h"
#include "vdoll_mash.h"
#include "vdoll_stamina.h"
#include "vdoll_food.h"
#include "vdoll_cqc.h"
#include "lobby_filter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace vdoll {
namespace {

Host gHost;
bool gOn = false, gDebug = false, gAlone = false;
int gKeepSeat = 1;                                  // seats always left for a real player (MGO1_VDOLL_KEEP_SEAT)
uint32_t gKey = 0xA7130C2F, gMac = 0x48B5ED69;      // the US and JP programs' set until the platform tells
long long gFrame = 0;
const uint32_t kLcg = 0x5D588B65;
const uint32_t kFirstId = 0x7F000041;               // after the accountless dolls' 0x7F000001 .. 0x7F000040

#define VLOG(...) do { if (gHost.log) gHost.log(__VA_ARGS__); } while (0)

uint32_t Le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t Le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
float LeF(const uint8_t* p) { const uint32_t v = Le32(p); float f; memcpy(&f, &v, 4); return f; }
void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int k = 0; k < 4; ++k) o.push_back((uint8_t)(v >> (8 * k))); }
void Put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
void PutF(std::vector<uint8_t>& o, float f) { uint32_t v; memcpy(&v, &f, 4); Put32(o, v); }

// ---- the settings the lobby filter and the players' kinds read before Init ----
// (v150-host beta 15) MGO1_VDOLL_LIVE=<file>: the count wanted read from it every 2 s (mgo1host-agent vdoll-count), the 15 ids
// and seats of a bot ready for it
struct Settings { int count = 0, want = 0; uint32_t first = kFirstId; bool live = false; std::string liveFile; };
const Settings& Cfg() {
    static const Settings s = [] {
        Settings x;
        if (const char* n = getenv("MGO1_VDOLLS")) x.count = std::max(0, std::min(15, atoi(n)));
        if (const char* f = getenv("MGO1_VDOLL_IDS")) {
            const unsigned long v = strtoul(f, nullptr, 0);
            if (v && v <= 0xFFFFFFF0ul) x.first = (uint32_t)v;
        }
        x.want = x.count;
        if (const char* l = getenv("MGO1_VDOLL_LIVE")) if (*l) { x.live = true; x.liveFile = l; x.count = 15; }
        return x;
    }();
    return s;
}
int gWant = 0;                                      // the virtual dolls wanted now (the first gWant of them)
long long gLiveAt = -1000;

// ---- the datagram ----
// as the game's send function makes it (0x16F420)
void Mix(std::vector<uint8_t>& b, bool undo) {
    const size_t n = b.size();
    if (n < 11) return;
    uint32_t st = (uint32_t)n;
    size_t a[2], s[2];
    for (int i = 0; i < 2; ++i) { st = st * kLcg + 1; a[i] = (st >> 16) % (uint32_t)(n - 2) + 2; }
    for (int i = 0; i < 2; ++i) { st = st * kLcg + 1; s[i] = (st >> 16) % (uint32_t)n; }
    if (!undo) {
        for (int i = 0; i < 2; ++i) b[i] ^= b[a[i]];
        for (int i = 0; i < 2; ++i) std::swap(b[i], b[s[i]]);
    } else {
        for (int i = 1; i >= 0; --i) std::swap(b[i], b[s[i]]);
        for (int i = 1; i >= 0; --i) b[i] ^= b[a[i]];
    }
}
// (0x16F050)
void Sign(uint32_t key, const uint8_t* p, size_t n, uint8_t out[10]) {
    std::vector<uint8_t> m;
    m.reserve(n + 4);
    Put32(m, key);
    m.insert(m.end(), p, p + n);
    uint8_t h1[16], h2[16];
    lobbyfilter::Md5(m.data(), m.size(), h1);
    uint8_t m2[20];
    memcpy(m2, m.data(), 4);
    memcpy(m2 + 4, h1, 16);
    lobbyfilter::Md5(m2, sizeof m2, h2);
    for (int j = 0; j < 10; ++j) out[j] = h2[j];
    for (int j = 0; j < 6; ++j) out[j] ^= h2[10 + j];
}
// (0x16EF00, 0x16EFA8)
void Cipher(std::vector<uint8_t>& b, uint32_t key, bool decrypt) {
    const size_t n = b.size();
    if (n < 12) return;
    const uint32_t seq = (uint32_t)(b[0] | b[1] << 8) & 0x7fff;
    uint32_t run = key ^ (seq * kLcg + 1);
    size_t o = 2, rem = n - 12;
    while (rem > 0) {
        const size_t take = rem < 4 ? rem : 4;
        uint32_t w = 0;
        for (size_t k = 0; k < take; ++k) w |= (uint32_t)b[o + k] << (8 * k);
        const uint32_t x = w ^ run;
        for (size_t k = 0; k < take; ++k) b[o + k] = (uint8_t)(x >> (8 * k));
        run += decrypt ? x : w;
        o += take; rem -= take;
    }
}

// ---- records ----
struct Rec { uint16_t chan = 0; uint8_t fl = 0; int seq = -1; const uint8_t* d = nullptr; size_t n = 0; };
bool Records(const uint8_t* p, size_t n, std::vector<Rec>& out) {
    size_t i = 0;
    while (i + 3 <= n) {
        Rec r;
        r.chan = (uint16_t)(p[i] | (p[i + 1] & 0x0f) << 8);
        r.fl = p[i + 1] & 0xf0;
        size_t j = i + 2, len;
        if (r.fl & 0x20) { if (j + 2 > n) return false; len = Le16(p + j); j += 2; }
        else len = p[j++];
        if (r.fl & 0x10) { if (j >= n) return false; r.seq = p[j++]; }
        if (j + len > n) return false;
        r.d = p + j; r.n = len;
        out.push_back(r);
        i = j + len;
    }
    return i == n;
}
void PutRec(std::vector<uint8_t>& o, uint16_t chan, uint8_t fl, int seq, const uint8_t* d, size_t n) {
    if (n > 255) fl |= 0x20;
    if (seq >= 0) fl |= 0x10;
    o.push_back((uint8_t)chan);
    o.push_back((uint8_t)(fl | ((chan >> 8) & 0x0f)));
    if (fl & 0x20) Put16(o, (uint16_t)n);
    else o.push_back((uint8_t)n);
    if (seq >= 0) o.push_back((uint8_t)seq);
    o.insert(o.end(), d, d + n);
}

// ---- who is in the host's game (the platform's session table), as the full dolls count it (doll.cpp PresenceNow): the
// real players, those still loading (no team yet, their first minute), the dolls (full or virtual), every joiner. The real
// players' count is taken once it has held 1 s; the latest real player's team known since when (doll_brain.cpp JoinHold)
struct Presence {
    Seen seen[16];
    int n = 0, real = 0, loading = 0, dolls = 0, joiners = 0;
    int taken = -1, pending = -1;
    long long pendingSince = 0, teamAt = -1;
    long long seenAt[16], realTeamAt[16];
    bool known = false;
    Presence() { for (int i = 0; i < 16; ++i) { seenAt[i] = -1; realTeamAt[i] = -1; } }
};
Presence gP;

bool InSession(uint32_t uid) {
    for (int i = 0; i < gP.n; ++i) if (gP.seen[i].uid == uid) return true;
    return false;
}

void PresenceTick() {
    static long long last = -1000;                   // (the game's pass comes every other frame: no frame modulo)
    if (!gHost.players || gFrame - last < 10) return;
    last = gFrame;
    Seen s[16];
    const int n = std::max(0, std::min(16, gHost.players(s, 16)));
    Presence& p = gP;
    p.n = n; p.real = p.loading = p.dolls = p.joiners = 0;
    p.known = true;
    uint32_t ids = 0;
    long long teamAt = -1;
    const bool dm = gHost.rule && gHost.rule() == 0;   // (deathmatch: the team byte is the unit, 0 to 2)
    for (int i = 0; i < n; ++i) {
        p.seen[i] = s[i];
        const int id = s[i].id;
        if (id < 0 || id >= 16) continue;
        ids |= 1u << id;
        if (p.seenAt[id] < 0) p.seenAt[id] = gFrame;
        if (s[i].kind != 1) ++p.joiners;
        if (s[i].kind == 2) ++p.dolls;
        if (s[i].kind == 0) {
            ++p.real;
            if (s[i].team == 0 || s[i].team == 1 || (dm && s[i].team == 2)) {
                if (p.realTeamAt[id] < 0) {
                    p.realTeamAt[id] = gFrame;
                    if (gDebug) VLOG("vdoll: P%d, a real player, in the %s", id + 1, dm ? "game (deathmatch)" : s[i].team ? "blue team" : "red team");
                }
                teamAt = std::max(teamAt, p.realTeamAt[id]);
            } else p.realTeamAt[id] = -1;
        } else p.realTeamAt[id] = -1;
        if (((s[i].kind == 0 && s[i].team == 0xFF) || s[i].kind == 3) && gFrame - p.seenAt[id] < 3600) ++p.loading;
    }
    for (int id = 0; id < 16; ++id) if (!((ids >> id) & 1)) { p.seenAt[id] = -1; p.realTeamAt[id] = -1; }
    p.teamAt = teamAt;
    if (p.real == p.taken) { p.pending = -1; return; }
    if (p.real != p.pending) { p.pending = p.real; p.pendingSince = gFrame; return; }
    if (gFrame - p.pendingSince < 60) return;
    const int was = p.taken;
    p.taken = p.real; p.pending = -1;
    VLOG("vdoll: real players %d -> %d (the session: %d joiners, %d dolls, %d loading)%s", was < 0 ? 0 : was, p.taken, p.joiners, p.dolls,
         p.loading, gAlone ? ", the virtual dolls play with or without them (MGO1_VDOLL_PLAY_ALONE)" : "");
}
bool WantPlay() { return gAlone || gP.taken > 0; }
// (doll_brain.cpp JoinHold) the round's restart, the game's when a side gets its first player while the other has some
// (deathmatch: the second player), left to a moment the real players are not arriving: while they still load (two dolls
// or more), or once they play (one doll), never within 10 s of a real player's team known. The reason, or none: it joins
const char* JoinHold() {
    if (gAlone) return nullptr;
    if (gP.teamAt >= 0 && gFrame - gP.teamAt < 600) return "a real player arriving";
    if (gP.dolls < 2 && gP.loading > 0) return "a real player loading (one doll)";
    return nullptr;
}

// ---- a virtual doll ----
struct OutRel { uint16_t chan; uint8_t seq; std::vector<uint8_t> data; long long sent = -1; int tries = 0; };
struct Ack { uint16_t chan; uint8_t seq; bool probe; };
enum Phase { OUT, CONNECT, JOINING, JOINED, LEAVING };
const char* PhaseName(int p) { static const char* n[] = {"out of the game", "connecting", "joining", "in the game", "leaving"}; return p >= 0 && p < 5 ? n[p] : "?"; }
// its life in the round: none yet, waiting to deploy (spawned, not in play: state 1), alive, dead, a ghost to the round's end
// (one life: rescue, sneaking, team sneaking), going spectator (its death for it), a spectator
enum Life { NONE, WAIT, ALIVE, DEAD, GHOST, GOING_SPECTATOR, SPECTATOR };
const char* LifeName(int l) {
    static const char* n[] = {"not spawned", "spawned", "alive", "dead", "a ghost to the round's end", "going spectator", "a spectator"};
    return l >= 0 && l < 7 ? n[l] : "?";
}

struct Doll {
    int k = 0;
    uint32_t id = 0;                     // its user id (MGO1_VDOLL_IDS)
    std::string name;
    int wantTeam = -1;                   // 0 red, 1 blue, -1 its turn to the side with fewer players
    uint32_t ip = 0;                     // network order
    uint16_t port = 5730;
    long long startAt = 0, rejoinAfter = 0;
    // the session
    Phase phase = OUT;
    uint32_t nonce = 0, hostNonce = 0;
    bool keyed = false, keyAckSent = false;
    uint16_t dseq = 0;
    std::map<uint16_t, uint8_t> nextSeq;
    std::vector<OutRel> rel;             // not acknowledged yet (the new ones too: sent < 0)
    std::vector<Ack> acks;
    std::map<uint16_t, std::deque<uint8_t>> seen;
    std::vector<std::vector<uint8_t>> unrel;
    long long lastSend = -1000, lastConnect = -1000, leaveAt = -1;
    std::deque<std::vector<uint8_t>> out;
    int dgramsIn = 0, dgramsOut = 0;
    long long lastIn = -1;
    // (v150-host beta 22, relay) the host's game's read that last took one of its datagrams (Pop), its datagrams kept for a later
    // read, its flushes that waited for its last datagram to be read
    long long popPass = -1, heldBack = 0, flushWaits = 0;
    // the game
    int slot = -1, gen = -1, team = -1, life = -1;
    bool memberAsked = false;
    long long joinedAt = -1, lastClaim = -1, lastKeep = -1;
    int claims = 0, claimGen = -1, claimAckGen = -1;
    int pickGen = -1, holdGen = -1;
    int pickTeam = -1;                   // the side of its pick in round pickGen (-1 in a deathmatch)
    long long pickAt = -1;
    std::string holdWhy;
    int assignedGen = -1;
    long long assignedAt = -1;
    float spawn[3] = {0, 0, 0}, pos[3] = {0, 0, 0};
    float start[3] = {0, 0, 0};          // (v150-host beta 21) where the host's game put its player this round (its round's start)
    uint16_t facing = 0x7fff;
    bool haveSpawn = false, alive = false;
    Life st = NONE;
    long long deployAt = -1, aliveAt = -1, deadAt = -1, nextTick = 0, lastFull = -1000, spawnSentAt = -1000, specAt = -1;
    bool lifeOk = false;                 // the host's life over 0 seen since it deployed
    int killer = -1;                     // the attacker of the last damage report on its channel
    bool lastHead = false;               // (v150-host beta 19) that report's head byte (the death kind it sends: DeathKind)
    int specTeam = -1;                   // the team its spectator's join asks
    int deaths = 0, respawns = 0, leaves = 0;
    bool snakeLies = false;              // (v150-host beta 25, snakeghost) its "no ghost" line said for this death (SNEAKING's Snake)
    bool snakeDead = false;              // (v150-host beta 25, snakeghost, review S1) this death is SNEAKING's Snake's, decided at the death
    int deadKind = 1;                    // (v150-host beta 25, ghostwait) the kind of this death it tells ({07 kind}), taken at the death
    double deadMs = 0; long long deadLastF = -1, deadLastMs = 0;   // ((ghostwait) the time since its death on a player's clock)
    // ((v150-host beta 25, respawnwait) its death's host frame and machine ms: the clock from them, not step by step)
    long long deadStartF = -1, deadStartMs = 0;
    // ((v150-host beta 25, respawnwait) this death's capture second (the bigger team's 6 s), on that clock its spawn's moment, its
    // weapons page's end (its deploy) and its first free frame (-1: none due)
    int respExtra = 0; double spawnMs = -1, deployMs = -1, freeMs = -1;
    int objKind = -1, objLife = -1;      // its player object's kind and life this round (sneaking's Snake: kind 8, over 200)
    int specTries = 0;                   // its spectator changes this round the host did not take
    long long leaveWait = 1200;          // frames out of the game after its leave
    bool kicked = false;                 // the host's kick (the room's {4, id}): it leaves
    bool hostEnded = false;              // the host's {1}: its game ended
    unsigned pick = 0;
    // (v150-host beta 15) as a player's game: its stamina, knocked down (a roll into it: 4.2 s) or out (stamina spent: up
    // when it is back), held in a grab
    int stamina = 30000;
    bool slowWake = false;                       // (v150-host beta 18) knocked out while it defends in team sneaking
    int down = 0;                        // 0 up, 1 knocked down, 2 knocked out, 3 getting up
    long long downAt = -1, upAt = -1;
    bool held = false;
    int knocks = 0;
    bool fell = false;                   // its fall's {0A 1} sent
    // (v150-host beta 22, blast) a blast's fall (the reaction kinds 6 to 8: its body thrown, then lying awake until a player's
    // input stands it up), and a flinch (kinds 1 to 5: an animation a player stands through, his controls not his)
    bool thrown = false, copyThrown = false;
    bool m37Fall = false; double fallEndMs = -1;  // ((v150-host beta 25, reactfix) thrown by the M37 (6, 7): its fall's end on a player's clock)
    long long flinchUntil = -1;
    // ((v150-host beta 25, fallflinch) MGO1_VDOLL_HIT_FALL: its flinch's end on a player's clock (ms), the record's kind and frame,
    // the clock's sum (KoClock's way, its own fields))
    double flinchEndMs = -1, flClk = 0; long long flLastF = -1, flLastMs = 0, flinchAt = -1; int flinchKind = 0;
    long long flStartF = -1, flStartMs = 0; double flStepMs = 0;   // ((v150-host beta 25, clockfix) as koStartF, koStartMs, koStepMs)
    uint8_t held13[4] = {0x13, 0, 0, 0};         // ((v150-host beta 25, fallflinch) its body's last {13 item, weapon, flags}: the flinch's)
    int flinches = 0, throws = 0;
    int cqcThrows = 0;                           // (v150-host beta 24, cqc: thrown from a hold, not knocked out)
    // (v150-host beta 22, mash) knocked out: the game's ring of the last 32 ticks' presses, the hand that presses meanwhile
    // (vdoll_mash.h), the knock-out's clock (ms) and the ticks of it done
    vmash::Recovery ring;
    vmash::Hand hand;
    long long koFrame = -1;                      // (v150-host beta 22, mash2: the host's frame it went down at; its ticks from the frames)
    // (v150-host beta 23, mash3) a player's own clock since it went down: the real ms (the host's frames as the body's pace counts
    // them), the frame and the machine's ms it was last read at; its getting up's end on it (-1: by the frames, upAt)
    double koRealMs = 0; long long koLastF = -1, koLastMs = 0; double riseMs = -1;
    // ((v150-host beta 25, clockfix) its start's host frame and machine ms (the clock from them), and b16's step by step sum beside it)
    long long koStartF = -1, koStartMs = 0; double koStepMs = 0;
    int koTicks = 0;
    // (v150-host beta 24, stamina2) its moves' cost to its stamina as a player's (vdoll_stamina.h): the engine's counts, a player's
    // own clock (real ms, the host's frame it was last read at, the machine's ms then) and the ticks of it done; the drains logged
    vstam::Mover mover;
    double mvMs = 0; long long mvLastF = -1, mvLastMs = 0; int mvTicks = 0, mvSaid = 0;
    long long mvStartF = -1, mvStartMs = 0; double mvStepMs = 0;   // ((v150-host beta 25, clockfix) as koStartF, koStartMs, koStepMs)
    // ((v150-host beta 25, aimcost) the debug count of its moves over a minute of ticks: the ticks by what its body did (MoveWhy), the
    // moving ticks a restart of the engine's count dropped (31 still ticks before the 31st moving one) and the restarts, the still runs
    // between two moving ticks by length (1-2, 3-6, 7-15, 16-30, 31 and more), the drains and their cost at the minute's start)
    int mvWhy[body::MW_COUNT] = {}, mvLost = 0, mvRestarts = 0, mvStillRun = 0, mvRuns[5] = {}, mvLap = 0, mvLapDrains = 0, mvLapSpent = 0;
    bool testStam = false;                       // (v150-host beta 25, food, benches: MGO1_VDOLL_TEST_STAMINA done this life)
    int foodsOwed = 0;                           // (v150-host beta 25, food: the foods eaten since its last state record)
    bool koRise = false;                         // (getting up from a knock-out: its event 4 kept, as a client's)
    bool asleep = false;                         // (knocked out by a dart: a sleeper's longer getting up)
    bool rollPress = false;                      // (a roll's knock-down: up at its hand's first press once it lies awake)
    // (v150-host beta 16) its player's class as the host's game holds it (0 KGB, 1 GRU, 2 the Ocelot Unit, 3 to 7 a
    // special character, 8 Snake), the one last logged; a spawn held for a class not known; its weapon lowered at a
    // knock-down; the last class it had in this game (kept over the rounds), a spawn's hold begun, a class told
    // from a fallback said this round
    int cls = -1, clsSaid = -1, clsGame = -1;
    long long heldSince = -1;
    bool clsHeld = false, lowered = false, clsGuessSaid = false;
    bool unitChecked = false;            // (the configurations' audit) its team's class checked this round
};
std::vector<Doll> gDolls;
int gGap = 120;
// (v150-host beta 16, tests) MGO1_VDOLL_TEST_CLASS=<0 to 255>: every respawn tells that class in place of its player's.
// A bench of the host's own guard (ps2world.cpp, the character a spawn asks for): the host must keep the copy's class
// and play on. -1: not set
int gTestClass = -1;
// (v150-host beta 22, wake, benches) MGO1_VDOLL_TEST_KO=<n>: virtual doll n knocked out (its stamina emptied, as a stun record of 30000
// does: the same knock out) each time it has been up 30 s, so that a teammate's waking can be benched with virtual dolls alone; 0 none
int gTestKo = 0;
// (v150-host beta 21, tests) MGO1_VDOLL_TEST_SEQ_STEP=<1 to 200>: each datagram of a doll in the game is numbered that many
// further than the one before (1: as always). A bench reaches the 32768th and the 65536th number of a session in a minute
// or two instead of an hour (the game takes a jump of 256 at most for datagrams lost on the way)
int gSeqStep = 1;
// the teams of the round as the host tells them (its {0x85, round, (id, team) x 16} to each joiner, every 100 ms): at a new
// map every player out of a team until his pick, while the session's table keeps his last round's team (after a deathmatch
// round, every player's unit: 0, red; audit_flow/c1b_fifteen had sent all 15 virtual dolls to blue)
struct RoundTeams { int round = -1; long long at = -1000; uint8_t team[16]; RoundTeams() { memset(team, 0xFF, sizeof team); } };
RoundTeams gRT;

// (v150-host beta 22, blast) a blast's knock-down and a flinch as a real client's own game makes them (docs/VIRTUAL_DOLL.md 31).
// The reaction record {09 attacker, kind, angle} (US n003a: the attacker's 0x436480, the receiver 0x436A18 on every program,
// the host relaying it) makes every game play the victim's copy's reaction itself; the victim's own game then tells its own
// player's fall in its records. Measured on a real client (a playable doll, blast_b21_d, the frag at its feet: kind 8, 112 of
// damage): its body thrown 3.6 m (its game sends no place meanwhile, its state ticks with no down event), lying awake 141
// frames after the record (the state word 0's 0x8000008: then its landing's records {10 0}, its place lying, {01 0}, the down
// event and {0A id 1} each tick, its place every 20 frames), {0A id 5} each tick from the first input its game takes, {0A id 0}
// 70 frames later, then on from where it lies. Beta 21's doll told the roll's fall for every kind 6 to 9: lying 60 frames
// after the record (its copies still in the air), {0A 4}, getting up at 186 and up at 258 by its own clock, its place never
// told; the host's copy still lay when it ran on (mines_t5_on: held back at its lying place 300 mm over the floor). A flinch
// (kinds 1 to 5: the WP's, a blast at a crouched or lying player) was not taken at all: the doll ran and fired through it.
// MGO1_VDOLL_BLASTS=0 (or MGO1_VDOLL_REACT=0): as beta 21
bool gReact = true;
// (the record to lying awake when the host's copy does not tell it: the landing's records; measured 130 to 158 frames on the
// victim's game, 152 to 168 on the host with the way to him, blast_b21_d, blast_t1_d; the host's own copy of the doll tells
// its lying awake itself, the state word 0's 0x8000000 without 0x6000000, and is followed)
const long long kThrownFor = 141, kThrownMin = 90, kThrownMax = 200;
const long long kPressAfter = 8;         // (lying awake to the first input its game takes: {0A 5}; measured 7 and 9, a press every 4)
const long long kRisingFor = 70;         // ({0A 5} to {0A 0})
const long long kFlinchFor = 40;         // (a flinch's animation: the WP's on a real client, its controls back)
// (v150-host beta 25, fallflinch) a flinch as long as a player's own game holds him in it (docs/VIRTUAL_DOLL.md 55). The reaction's
// start 0x439798 plays the kind's motion to its end (kind 1 the motion 0x2E, 2 0x2F, 3 0x32, 4 0x31, 5 0x33) under the reaction
// action 0x29BD1B4 (priority 0x29: no move, no shot, no book meanwhile), then the player is free at his next tick. Measured on real
// clients (probe 52's reaction lines, benches bh_hits4, ff_hits1): kFlinchTicks below, on a player's clock (30 ticks a real
// second: the host slowed or not, mash3's way). A hit again while it runs starts the reaction again (bh_hits3: 385 ticks under
// hits again and again), as the doll's flinch did. MGO1_VDOLL_HIT_FALL=0: kFlinchFor's 40 of the host's frames whatever the kind (b16)
bool gHitFall = true;
// (by the kind 0 to 5: 1 and 2 the flamethrower's from the front and from behind, 17 ticks each (5 flinches); 3 lying, 28 ticks
// (an RPG-7's blast on a lying player, once); 0: not measured on a real client, kFlinchFor's 40 of the host's frames kept: the kinds 4
// and 5, a player already fallen (the falls' state bits), come from a blast on him or a real attacker's game)
const int kFlinchTicks[6] = {0, 17, 17, 28, 0, 0};
// ((v150-host beta 25, reactfix) the kinds 4 and 5 measured on real clients: 4 (a flinch down after a roll or a kick, 0x1C) 29 ticks (rf_ba_after:
// a doll's flame and its M37's 3 pellets on a client lying after a doll's roll, the motion 0x31, still lying, twice), 5 (down after a blast
// or a fall, 0x1B) 30 ticks (rf_m2: the M37's 3 pellets on a player lying after its 4 pellets' fall, the motions 0x39 then 0x33).
// MGO1_VDOLL_REACT_KIND=0: b16's table (40 frames for both)
bool gReactKindV = true;
int FlinchTicks(int kind) { return kind == 4 && gReactKindV ? 29 : kind == 5 && gReactKindV ? 30 : kind >= 0 && kind < 6 ? kFlinchTicks[kind] : 0; }
const int kM37FallTicks = 27;                    // ((v150-host beta 25, reactfix) the M37's fall's reaction on a real client (rf_m2: 27 ticks twice))
// (v150-host beta 22, mash) knocked out (its stamina emptied: a dart, a stun grenade, CQC, punches), a doll gets its stamina
// back by the game's own rule as a player who presses (vdoll_mash.h: each game tick, 30 a second, 3 x (8 + the ticks with a
// press among the last 32), none counted while the fall's motion runs), its presses those of a hand mashing a button at a
// person's rate (MashRate: 4 a second at level 1 to 9.5 at level 10, its gaps spread, a reaction first). Beta 22 gave it 144
// every 6 frames: twice the game's natural rate, as a player pressing about 7.5 times a second from the first frame (up 22 s
// after; a real client pressing nothing 41.5 s, reports/sneaking_weapons.md J). MGO1_VDOLL_MASH=0: beta 22's;
// MGO1_VDOLL_MASH_RATE=<presses a second, 0 to 15>: that rate for every doll (0: no press, the game's 41.7 s)
bool gMash = true;
double gMashRate = -1;                   // (-1: by its level)
const int kKoFallTicks = 0;              // (a knock-out's own fall sets no fall motion flag: a sleeper's presses count at once, mash_a1)
// (awake, its getting up: a real client's {0A 5} goes out every 3 ticks: 20 of them after a dart's sleep (mash_a1: 60 ticks, 120
// frames at the game's pace), 11 or 12 after a stun's knock-out or a roll's knock-down (mash_c2: 66 to 72 frames), its event 4
// kept through it; beta 22's 66 frames, the event cleared)
const long long kSleepRiseFor = 120, kKoRiseFor = 66;
// (v150-host beta 23, wake) woken by a teammate's blow (the game's ally wake, a negative stun that fills its gauge), a real client
// gets up in 118 frames: its own game's getting up (the state 0x2000000 to up) 118 and 118, its {0A 5} records on the host 114,
// 118 and 114 frames to {0A 0} (wake_b1_on_t16, woken by a virtual doll's kick, pace 60); the same as one up by his own recovery
// after a knock-out from standing (104 to 139, 22 samples; reports/vdoll_wake.md 6). KoWake's 66 (a roll's knock-out of a player
// already down, mash_c2) told a woken doll up 50 frames too soon. Its length on mash3's player clock (1.97 s; the host's frames
// with MGO1_VDOLL_PACE=0). MGO1_VDOLL_WAKE=0: KoWake's, as beta 22; a doll that wakes by itself keeps mash3's values
const long long kWokenRiseFor = 118;
bool gWokenRise = true;
// (v150-host beta 24, cqc) a CQC throw from a hold on the doll as a player's own game takes it (docs/VIRTUAL_DOLL.md 40). The holder's
// game sends one stun record, kind 0, no life (vdoll_cqc.h: 10000 from a soldier's, Sokolov's or Ocelot's hold, 30000 from Raikov's,
// Reiko's or Rowdy's, the whole gauge from Snake's, 7500 in SNEAKING but Snake's): spent from its stamina, a knock out only when that
// empties it (as beta 23), and when it does not, the thrown player's fall: lying, up at its first press once it lies awake, as after
// a roll, which is a thrown real client's own timing (cqc_m1: lying 66 frames after the stun, standing 140 after it). Beta 23 told
// no fall: it stood on through the throw (tsneW_wake5, Ocelot's hold: 30000 -> 20000, its state 0 after it).
// MGO1_VDOLL_CQC=0: beta 23's
bool gCqc = true;
// (v150-host beta 24, stamina) a conscious player's stun gauge never comes back (docs/VIRTUAL_DOLL.md 41). US n003a 0x4143F0, the
// local player's tick, is the gauge's only writer in play (the others: the init 0x4050F0 and the respawn 0x40D1A0, both full).
// Not knocked out (0x414778) it only takes the tick's stun off (0x4147D4), knocking him out at 0 (0x414368), and drops a
// NEGATIVE stun (a teammate's ally wake: 0x414788); knocked out (0x414540) it refills by the presses (KoTick) and a negative
// stun fills it, a POSITIVE one is not applied (0x41454C). Measured on real clients: reports/vdoll_stamina.md. Beta 15 to 23
// gave a doll that was up 144 every 6 frames (a whole gauge in 20.8 s: a stun grenade gone 8.7 s later, the players' "bots
// recover stamina even when conscious"), took a teammate's blow on it up and a stun on it knocked out.
// MGO1_VDOLL_STAMINA=0: beta 23's three ways back
bool gStaminaRule = true;
// (v150-host beta 24, stamina2) moving costs a player's stamina too (vdoll_stamina.h, docs/VIRTUAL_DOLL.md 41): the engine's stamina
// system, given a posture code by the player's tick (0x4143F0 -> 0x306B50 -> 0x3065E0), takes at the 31st tick of a move the stage's
// cost of that move times (his load / 8000 + 0.5) off his gauge. Measured on real clients (stamina2_cal): 18 every 31 ticks running
// and crawling, 9 walking, 56 on a ladder with an AK-47, an M1911 and a book (a load of 6800); 21, 10 and 63 with an SVD, a Mk22
// and stun grenades (8100); 29, 14 and 87 with an M63 taken as well. Beta 15 to 24's doll lost nothing by moving. A doll's ticks
// are a player's, 30 a real second on the machine's clock as mash3's (MGO1_VDOLL_PACE), its moves its body's, its load what it
// carries by the game's weights. MGO1_VDOLL_MOVE_COST=0 (or MGO1_VDOLL_STAMINA=0): none, as beta 24
bool gMoveCost = true;
// (v150-host beta 24, stamina2, benches) MGO1_VDOLL_TEST_STOP=<s>: once alive that long a doll stands still where it is (its body
// paused, its records a still doll's), so that a playable doll's grenades find it after it has run; 0 none
long long gTestStop = 0;
// (v150-host beta 25, food, benches) MGO1_VDOLL_TEST_STAMINA=<n>: once a life, up 10 s, a doll's stun gauge set to n (lower only),
// so that a bench sees it eat the food lying near it as a player whose gauge stun grenades or blows took; 0 none
int gTestStamina = 0;
// (a roll's knock-down, mash_c2: a real client lies awake 66 to 72 frames after the record reaches it (its landing {10} {0A 1}),
// its press then taken at once ({0A 5} the next tick), up 67 frames later; pressing nothing it lies there (v133's audit: up to
// 3 minutes until killed). Beta 22: 4.2 s by its own clock, the get-up at 180 frames)
const long long kRollAwake = 68;
// (v150-host beta 23, mash3; the owner on review23's F4: "for all the bots, it must be the same as for the players") a player's own
// game ticks 30 times a REAL second even when the host is slowed, so a doll knocked out counts its presses and its refill in a
// player's ticks on the machine's clock, as the body's pace takes it (MGO1_VDOLL_PACE, beta 21: each host frame its real length,
// 1 to 3 times its 59.94 a second one, a stall not counted beyond that): at 38 host frames a second it wakes after the same real
// seconds as a player (mash2 counted the host's frames: 25 to 34 s down at the owner's 33 to 46 frames a second for a player's
// 19). MGO1_VDOLL_PACE=0: the host's frames, a tick every two (mash2's)
bool gKoPace = true;
// (v150-host beta 25, clockfix) a player's clocks counted from their start (MGO1_VDOLL_CLOCK_FIX, on). The knock-out's (KoClock, mash3:
// a knocked-out doll's presses, its refill and its getting up) and the moves' (MoveCostTick, stamina2: their cost) added up, step by
// step, the longer of each step's host frames (16.68 ms each) and its machine ms, 3 times the frames at most, as ghostwait's dead
// clock did before respawnwait: where the host's ticks come unevenly a short step counted as its frames and a long one as its ms, so
// the sum ran ahead of the real time, and where a step came after a freeze longer than 3 times its frames (a host held by a processor
// quota, as the owner's server is while people play) it was cut and the sum fell behind. Now each is the machine's ms since its start,
// 3 times the frames' at most (a stall beyond that not counted), with no floor of the frames' ms (the dead clock's floor ends a wait up
// to one uneven step early: 17 ms of a flinch's 567 on flow flinchjitter). MGO1_VDOLL_CLOCK_FIX=0: b16's step by step sums
bool gClockFix = true;
double ClockSince(long long startF, long long startMs) {
    const double nominal = (double)(gFrame - startF) * 1000.0 / 59.94;
    if (!gHost.realMs || !gKoPace) return nominal;
    return std::max(0.0, std::min(3.0 * nominal, (double)((long long)gHost.realMs() - startMs)));
}
// (b16's step: its frames' ms, or its machine ms when longer, 3 times the frames at most; the last frame and ms moved on)
double ClockStep(long long& lastF, long long& lastMs) {
    const double nominal = (double)(gFrame - lastF) * 1000.0 / 59.94;
    double ms = nominal;
    if (gHost.realMs) {
        const long long now = (long long)gHost.realMs();
        if (gKoPace) ms = std::max(nominal, std::min(3.0 * nominal, (double)(now - lastMs)));
        lastMs = now;
    }
    lastF = gFrame;
    return ms;
}
// ((clockfix, MGO1_VDOLL_DEBUG) a clock read against the machine's ms and the host's frames since its start)
void ClockSay(const Doll& d, const char* what, double clk, long long startF, long long startMs, double step) {
    if (!gDebug || startF < 0) return;
    const double real = gHost.realMs ? (double)((long long)gHost.realMs() - startMs) : 0.0, frames = (double)(gFrame - startF) * 1000.0 / 59.94;
    VLOG("vdoll %d: %s: its clock %.2f s (MGO1_VDOLL_CLOCK_FIX %s): from its start %.2f s, step by step %.2f s; the machine's %.2f s, the host's frames %.2f s",
         d.k + 1, what, clk / 1000.0, gClockFix ? "on" : "0", ClockSince(startF, startMs) / 1000.0, step / 1000.0, real / 1000.0, frames / 1000.0);
}
double KoClock(Doll& d) {
    if (d.koLastF < 0) {
        d.koLastF = gFrame; d.koLastMs = gHost.realMs ? (long long)gHost.realMs() : 0;
        d.koStartF = gFrame; d.koStartMs = d.koLastMs; d.koStepMs = 0;   // ((clockfix) its start)
        return d.koRealMs;
    }
    if (gFrame <= d.koLastF) return d.koRealMs;
    d.koStepMs += ClockStep(d.koLastF, d.koLastMs);
    d.koRealMs = gClockFix ? std::max(d.koRealMs, ClockSince(d.koStartF, d.koStartMs)) : d.koStepMs;   // ((clockfix) from its start)
    return d.koRealMs;
}
void KoClockStart(Doll& d) { d.koRealMs = 0; d.koLastF = -1; d.riseMs = -1; KoClock(d); }
// (v150-host beta 25, clockfix, benches) MGO1_VDOLL_CLOCK_TRACE=1: the host's frames and the machine's clocks at each pass of the dolls with
// frames since the one before, 50 a line: `vdoll: clock trace f=<frame> ms=<ms>: <frames>,<ms>,<us> ...` (the frames, the machine's ms
// (Host::realMs) and the microseconds of a fine clock since the pass before), for the tests' replays of a real host's ticks
bool gClockTrace = false;
void ClockTrace(long long frame) {
    static long long lastF = -1, lastMs = 0, f0 = -1, ms0 = 0;
    static std::chrono::steady_clock::time_point lastAt;
    static std::string line;
    static int n = 0;
    if (frame <= lastF) return;
    const long long ms = gHost.realMs ? (long long)gHost.realMs() : 0;
    const std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
    if (lastF >= 0) {
        if (n == 0) { f0 = lastF; ms0 = lastMs; }
        char b[64];
        snprintf(b, sizeof b, " %lld,%lld,%lld", frame - lastF, ms - lastMs, (long long)std::chrono::duration_cast<std::chrono::microseconds>(at - lastAt).count());
        line += b;
        if (++n >= 50) { VLOG("vdoll: clock trace f=%lld ms=%lld:%s", f0, ms0, line.c_str()); line.clear(); n = 0; }
    }
    lastF = frame; lastMs = ms; lastAt = at;
}
// ((v150-host beta 25, fallflinch) the flinch's clock: a player's ms since its start, the machine's ms (MGO1_VDOLL_PACE=0 or no clock:
// the host's frames). Each frame's ms as the clock reads them, a stall counted up to 3 frames' worth: not KoClock's floor of a frame's
// nominal 16.7 ms, which ran ahead of the real time on a host whose frames come bunched (ff_ba_after2 at a pace of 59.5: its 17 ticks'
// flinches over 473 to 530 ms real for 567; a coarse clock reading 0 or 16 ms a frame does the same) where their sum is the real time
// ((v150-host beta 25, clockfix) MGO1_VDOLL_CLOCK_FIX: from its start as KoClock (that sum cut each step longer than 3 times its frames:
// behind the real time on a host frozen in bursts); 0: that sum)
double FlinchClock(Doll& d) {
    if (d.flLastF < 0) {
        d.flLastF = gFrame; d.flLastMs = gHost.realMs ? (long long)gHost.realMs() : 0;
        d.flStartF = gFrame; d.flStartMs = d.flLastMs; d.flStepMs = 0;   // ((clockfix) its start)
        return d.flClk;
    }
    if (gFrame <= d.flLastF) return d.flClk;
    const double nominal = (double)(gFrame - d.flLastF) * 1000.0 / 59.94;
    double ms = nominal;
    if (gHost.realMs) {
        const long long now = (long long)gHost.realMs();
        if (gKoPace) ms = std::min(3.0 * nominal, std::max(0.0, (double)(now - d.flLastMs)));
        d.flLastMs = now;
    }
    d.flStepMs += ms; d.flLastF = gFrame;
    d.flClk = gClockFix ? std::max(d.flClk, ClockSince(d.flStartF, d.flStartMs)) : d.flStepMs;   // ((clockfix) from its start)
    return d.flClk;
}
// (v150-host beta 24, stamina2) each host frame while it is in play: the player's ticks since the last frame (KoClock's way, on its
// own clock), each one counted by the engine's rule with its move now (up: its body's posture code; down, held, flinching or
// stopped by the bench: none) and its load; a cost taken off its stamina (never to 0)
void MoveClockStart(Doll& d) { d.mover.Reset(); d.mvMs = 0; d.mvLastF = -1; d.mvTicks = 0; d.mvStillRun = 0; }
void MoveCostTick(Doll& d, bool up) {
    if (!gMoveCost || !gStaminaRule) return;
    if (d.mvLastF < 0) {
        d.mvLastF = gFrame; d.mvLastMs = gHost.realMs ? (long long)gHost.realMs() : 0;
        d.mvStartF = gFrame; d.mvStartMs = d.mvLastMs; d.mvStepMs = 0;   // ((clockfix) its start: the life's)
        return;
    }
    if (gFrame <= d.mvLastF) return;
    d.mvStepMs += ClockStep(d.mvLastF, d.mvLastMs);
    d.mvMs = gClockFix ? std::max(d.mvMs, ClockSince(d.mvStartF, d.mvStartMs)) : d.mvStepMs;   // ((clockfix) from its start)
    const int upTo = (int)(d.mvMs * vmash::kTicksASecond / 1000.0);
    if (upTo <= d.mvTicks) return;
    const int code = up && body::On() ? body::MoveCode(d.k) : 0;
    const int load = body::Load(d.k);
    const int why = !gDebug ? 0 : up && body::On() ? body::MoveWhy(d.k) : body::MW_OFF;
    while (d.mvTicks < upTo) {
        ++d.mvTicks;
        if (gDebug) {                                    // ((v150-host beta 25, aimcost) the debug count of its moves)
            ++d.mvWhy[why]; ++d.mvLap;
            if (code <= 0) {
                ++d.mvStillRun;
                if (d.mover.hold <= 0 && d.mover.still + 1 >= vstam::kRest && d.mover.moving > 0) { d.mvLost += d.mover.moving; ++d.mvRestarts; }
            } else if (d.mvStillRun > 0) {
                const int n = d.mvStillRun;
                ++d.mvRuns[n <= 2 ? 0 : n <= 6 ? 1 : n <= 15 ? 2 : n <= 30 ? 3 : 4];
                d.mvStillRun = 0;
            }
        }
        const int c = d.mover.Tick(code, load);
        if (c > 0) d.stamina = vstam::After(d.stamina, c);
    }
    if (gDebug && d.mvLap >= 60 * vmash::kTicksASecond) {   // ((v150-host beta 25, aimcost) every minute of its ticks)
        const int* w = d.mvWhy;
        int passes = 0, passes0 = 0;
        body::Passes(d.k, &passes, &passes0);
        VLOG("vdoll %d: its moves, %d ticks: run %d, walk %d, crawl %d, roll or climb %d; aiming %d (moving %d); still %d (a way left %d, prone %d), off %d; "
             "%d drains (%d); %d moving ticks dropped by %d restarts of the count; still runs 1-2: %d, 3-6: %d, 7-15: %d, 16-30: %d, 31+: %d (load %d, its stamina %d); "
             "its body's passes %d (%d with no host frame since the last)",
             d.k + 1, d.mvLap, w[body::MW_RUN], w[body::MW_WALK], w[body::MW_CRAWL], w[body::MW_ROLL], w[body::MW_AIM] + w[body::MW_AIM_MOVING],
             w[body::MW_AIM_MOVING], w[body::MW_WAY] + w[body::MW_STILL] + w[body::MW_PRONE], w[body::MW_WAY], w[body::MW_PRONE], w[body::MW_OFF],
             d.mover.drains - d.mvLapDrains, d.mover.spent - d.mvLapSpent, d.mvLost, d.mvRestarts, d.mvRuns[0], d.mvRuns[1], d.mvRuns[2], d.mvRuns[3], d.mvRuns[4],
             load, d.stamina, passes, passes0);
        ClockSay(d, "its moves' clock this life", d.mvMs, d.mvStartF, d.mvStartMs, d.mvStepMs);   // ((v150-host beta 25, clockfix))
        d.mvLap = 0; d.mvLapDrains = d.mover.drains; d.mvLapSpent = d.mover.spent;
        for (int& v : d.mvWhy) v = 0;
        for (int& v : d.mvRuns) v = 0;
        d.mvLost = d.mvRestarts = 0;
    }
    if (gDebug && d.mover.drains >= d.mvSaid + 30) {   // (every 30 drains: what its moves cost)
        d.mvSaid = d.mover.drains;
        VLOG("vdoll %d: its moves cost its stamina %d in %d drains (move %d, load %d: %d a drain running), its stamina %d", d.k + 1, d.mover.spent, d.mover.drains,
             code, load, vstam::Cost(3, load), d.stamina);
    }
}
int KoTicksNow(Doll& d) { return (int)(KoClock(d) * vmash::kTicksASecond / 1000.0); }
// its getting up over: on the player's clock when it began on it (a knock-out's, a roll's), else by the host's frames
bool RiseDone(Doll& d) { return d.riseMs >= 0 ? KoClock(d) >= d.riseMs - 0.5 : gFrame >= d.upAt; }   // (half a ms: the frames' sums)
// at the knock-out: the ring cleared (the game's 0x4147BC), the hand's rate by its level now
void KoStart(Doll& d) {
    d.ring.Reset(); d.koTicks = 0; d.koFrame = gFrame; KoClockStart(d);
    if (!gMash) return;
    const float now = LevelNow(d.k);
    const double lv = now > 0.f ? (double)now : (double)body::LevelSet(d.k);
    const double rate = gMashRate >= 0 ? gMashRate : vmash::MashRate(lv);
    d.hand.Start(rate, vmash::ReactFrames(lv), 0x9E3779B9u * (unsigned)(d.k + 1) + 0x85EBCA6Bu * (unsigned)d.knocks + (unsigned)gFrame);
    if (gDebug) VLOG("vdoll %d: knocked out: it presses %.1f times a second (level %.2f), its stamina back by the game's rule", d.k + 1, d.hand.rate, lv);
}
// knocked out, each state tick: its stamina back as a player's game gives it (beta 22 / MGO1_VDOLL_MASH=0: 144 every 6 frames;
// 36 for a TEAM SNEAKING defender). Only adds to its stamina: a teammate's blow (a negative stun, PlayerRecord) adds on top, as
// the game sums them. falling: its body still in its fall (the game counts no press then)
void KoTick(Doll& d, bool falling) {
    if (!gMash || d.koFrame < 0) { d.stamina = std::min(30000, d.stamina + (d.slowWake ? 36 : 144)); return; }
    const int upTo = KoTicksNow(d);
    const int rate = d.slowWake ? vmash::kRateSlow : vmash::kRate;
    while (d.koTicks < upTo && d.stamina < 30000) {
        const bool fall = falling || (!d.thrown && d.koTicks < kKoFallTicks);
        const bool press = d.hand.Press(d.koTicks);
        d.stamina = std::min(30000, d.stamina + d.ring.Tick(press, fall, 30000, rate));
        ++d.koTicks;
    }
    if (d.stamina >= 30000) {
        VLOG("vdoll %d: awake after %.1f s knocked out (%d ticks, %d presses, %.1f a second, the game's rule)", d.k + 1, (double)d.koTicks / vmash::kTicksASecond, d.koTicks,
             d.hand.presses, d.hand.rate);
        ClockSay(d, "awake", d.koRealMs, d.koStartF, d.koStartMs, d.koStepMs);   // ((v150-host beta 25, clockfix, debug))
    }
}
// awake from a knock-out: its getting up (a client's length and event with the switch on; beta 22's 66 frames)
// ((v150-host beta 23, mash3) its length on the player's clock: a player's own game plays it at full pace)
void KoWake(Doll& d) {
    d.down = 3; d.upAt = gFrame + (gMash && d.asleep ? kSleepRiseFor : kKoRiseFor); d.koRise = gMash;
    d.riseMs = gMash && d.koLastF >= 0 ? KoClock(d) + (double)(d.asleep ? kSleepRiseFor : kKoRiseFor) * 1000.0 / 59.94 : -1;
}
// a roll's knock-down (kind 9): its hand mashes from the record on, as for a knock-out
void RollStart(Doll& d) {
    d.rollPress = gMash;
    if (!gMash) return;
    d.koFrame = gFrame; d.koTicks = 0; KoClockStart(d);
    const float now = LevelNow(d.k);
    const double lv = now > 0.f ? (double)now : (double)body::LevelSet(d.k);
    // (MGO1_VDOLL_MASH_RATE under 1: still once a second here, so that no doll lies to its death as a player who never presses)
    d.hand.Start(std::max(1.0, gMashRate >= 0 ? gMashRate : vmash::MashRate(lv)), vmash::ReactFrames(lv), 0x27D4EB2Fu * (unsigned)(d.k + 1) + 0x165667B1u * (unsigned)d.knocks + (unsigned)gFrame);
}
// knocked down by a roll, each state tick: up at its hand's first press once it lies awake (its fall told); true: getting up now
bool RollTick(Doll& d) {
    const int upTo = KoTicksNow(d);
    const int awake = d.thrown && d.m37Fall ? kM37FallTicks : (int)(kRollAwake / 2);   // ((v150-host beta 25, reactfix) the M37's fall: awake at its end)
    while (d.koTicks < upTo) {
        const bool p = d.hand.Press(d.koTicks);
        ++d.koTicks;
        if (p && d.koTicks > awake && d.fell) { ClockSay(d, "up from its knock-down at its press", d.koRealMs, d.koStartF, d.koStartMs, d.koStepMs); return true; }   // ((clockfix, debug))
    }
    return false;
}
int RuleNow() { return gHost.rule ? gHost.rule() : -1; }
bool OneLife(int rule) { return rule == 2 || rule == 4 || rule == 5; }      // rescue, sneaking, team sneaking: no respawn
// (v150-host beta 21) SNEAKING's soldiers come back: a player's game spawns its dead soldier again 6.6 to 10.9 s after
// his death (a playable doll's own game, 17 measures: most often 9 to 11 s), at a point of the soldiers' line, each time he
// dies, to the round's end (the owner's logs of 11 and 12 November: a real soldier killed up to 10 times in one round); Snake
// (the blue side, alone)
// has one life, his death ends the round. Until beta 20 a virtual doll soldier was a ghost to the round's end at its first
// death ("Bots sometimes wont spawn after being killed in SNE"). MGO1_VDOLL_SNE_RESPAWN=0: beta 20's way
bool gSneRespawn = true;
bool OneLifeOf(const Doll& d, int rule) { return rule == 4 && gSneRespawn ? d.team == 1 : OneLife(rule); }
// (v150-host beta 25, snakeghost) SNEAKING's Snake is never a ghost: his death ends the round, and a player's Snake lies dead
// through the results to the next round, his game sending its death ticks only (real clients: 104 deaths of a playable doll's
// Snake in 39 bench runs, the ghost bit never set, 52 of them with 630 frames or more before the next round;
// V22_rot_owner's P2P log: no spawn record from his death to the next map's). A virtual doll Snake sent its ghost spawn 630
// frames after its death, which a map's last round's results (1090 to 1140 frames) let through: the players saw him come back
// as a ghost ("Snake comes back as ghost in snk", 17 November: the owner's logs, every doll Snake killed in a map's last round
// since 8 November). A TEAM SNEAKING player's game does make him a ghost in the results (F24_tsne_d1): only Snake is kept out.
// MGO1_VDOLL_SNAKE_LIFE=0: beta 24's way (a ghost 630 frames after his death)
bool gSnakeLife = true;
bool NoGhost(const Doll& d, int rule) { return gSnakeLife && rule == 4 && d.team == 1; }
// (v150-host beta 25, ghostwait) a one-life player's ghost comes when his own game's timer says so: first his death's motion,
// whose length is set by the kind of death his game tells ({07 kind, killer}; n003a 0x3F5828 picks the motion by the kind,
// 0x3F4710 and its kin play it), then 5 s (0x3F46B0 counts 5 x 300 in 1/300 s, his 30 updates a second, and 0x409558 calls
// the stage script's respawn proc, which spawns him as a ghost in a one-life rule and in play in the others: the same timer as
// a respawn). No button is read on that path and the round's end changes nothing. Measured on real clients (1398 deaths in the
// playable dolls' own P2P logs, every rule; at full pace), from his first death record to his spawn record: a head shot (kind 2)
// 390 to 396 frames, a body kill (kind 1) 564 to 570, a blast (kind 4) 420 to 426, in TEAM SNEAKING and rescue as in the rules
// with a respawn; a real client pressing nothing while dead the same (bench gw_t1_idle: 65 or 66 of its death records, one every
// 3 of its updates). A doll sent its ghost 630 frames after its death whatever the death: later than every player's, the same every
// time. Now the middle of a player's for the kind it told, counted on a player's clock (KoClock's way, MGO1_VDOLL_PACE: his game
// ticks 30 times a real second even when the host is slowed). MGO1_VDOLL_GHOST_WAIT=0: b16's 630 host frames
bool gGhostWait = true;
// ((v150-host beta 25, respawnwait) kind 5 as kind 4: a player killed lying after a blast's throw, 420 to 426 frames too)
long long GhostWaitFrames(int kind) { return kind == 2 ? 393 : kind == 4 || kind == 5 ? 423 : 567; }
double DeadClock(Doll& d) {                           // (the real ms since its death: 1 to 3 times its host frames' length)
    // ((v150-host beta 25, respawnwait) from its death's frame and ms, not frame by frame: each step's longer of the frames' length and
    // the machine's ms (ghostwait's, mash3's KoClock's) ran fast where the host's ticks come unevenly (the dolls' tick twice in a frame,
    // none in the next: aimcost), a step short in ms counted as its frames and a long one as its ms: on the bench rw_t2_ad at full
    // pace the real client received the dolls' spawns 6.1 s (5.5 to 6.4) after a head shot, its own after 6.5 to 6.6 s)
    const long long now = gHost.realMs ? (long long)gHost.realMs() : 0;
    if (d.deadStartF < 0) { d.deadStartF = gFrame; d.deadStartMs = now; d.deadMs = 0; d.deadLastF = gFrame; return 0; }
    if (gFrame <= d.deadLastF) return d.deadMs;
    const double nominal = (double)(gFrame - d.deadStartF) * 1000.0 / 59.94;
    double ms = nominal;
    if (gHost.realMs && gKoPace) ms = std::max(nominal, std::min(3.0 * nominal, (double)(now - d.deadStartMs)));
    d.deadMs = std::max(d.deadMs, ms); d.deadLastF = gFrame; d.deadLastMs = now;
    return d.deadMs;
}
bool GhostDue(Doll& d) {
    if (!gGhostWait) return gFrame - d.deadAt >= 630;
    return DeadClock(d) >= (double)GhostWaitFrames(d.deadKind) * 1000.0 / 59.94 - 0.5;
}
// the wait from its death to its respawn (frames): 5 s, 6.5 s in capture and base capture, 10 s for a SNEAKING soldier
// (the long end of the game's own wait there: never back sooner than a player)
long long RespawnWait(int rule) { return rule == 3 || rule == 6 ? 390 : rule == 4 ? 600 : 300; }
// (v150-host beta 25, respawnwait) a player's respawn in the rules with one (deathmatch, team deathmatch, capture, base capture,
// SNEAKING's soldiers) is the one-life ghost's timer (GhostDue): his death's motion, its length by the kind his game tells, then 5 s
// (n003a 0x3F5828, 0x3F46B0, 0x409558, the stage script's respawn proc), in capture 6 s for a player whose team has more players than
// the other (0x409700 reads the teams' counts, 0x3EF968, that 0x3EF9E8 makes of the session's players). Measured on real clients (the
// playable dolls' P2P logs, every rule at full pace): his spawn record 390 to 396 frames after his first death record for a head shot,
// 564 to 570 for another death, 420 to 426 killed lying (kinds 4 and 5). Then his game's weapons page (state 1 records, about 18
// frames with its OK pressed at once) and a hold of 3 s at his spawn point (0x40D1A0 sets +0xF74 to 900, 0x40EFD8 counts it down at
// his 30 updates a second and keeps the frozen bit 0xF8 set) that an action begun by a press ends (0x41D5F0, 0x425718, 0x4359D8 clear
// +0xF74): in play 197 to 201 frames after his spawn record pressing nothing, 21 frames at the soonest measured (the fastest a player
// can be back, with his presses at once). A doll respawned 300 frames after its death (390 in capture and base capture, 600 a SNEAKING
// soldier) whatever the death and was in play 18 frames later. Now its spawn when a player's comes for the kind it told, on a player's
// clock (DeadClock: on a slowed host the same real seconds), its state 1 for the weapons page's frames, then held still at its spawn
// point to kRespawnDeploy frames after it. MGO1_VDOLL_RESPAWN_WAIT=0: b16's frames
bool gRespawnWait = true;
const long long kRespawnPage = 18;      // its spawn record to its first state record of 0 (the weapons page shown, its OK at once)
// its spawn record to its first free frame: the fastest a player is back (his presses at once); 199 is a player who presses nothing
// at the deploy (the game's hold of 3 s). One number to change to give the dolls another player's deploy
const long long kRespawnDeploy = 21;
// (v150-host beta 25, respawnwait) the kind of death a player's game tells: 0x3F5828 gives 4 when he lies with the state bit 0x1C (the
// state word 0's 0x10000000: down after a roll into him or a CQC knock-down) and 5 with the bit 0x1B (0x8000000: thrown by a blast, or
// down after a fall), before the head byte's 2 and the plain 1 (and 3 prone, 9 climbing: not measured). Measured on real clients: the
// playable dolls' kind 4 deaths lay with 0x10000000 (killed by a doll's handgun after its punches or its roll), their kind 5 with
// 0x8000000 or thrown (the RPG-7's blast, a claymore's); killed knocked out (stamina spent) they told 2 after a head hit, else 1. A doll
// told 2 or 1 by the head byte whatever it did. MGO1_VDOLL_DEATH_KIND=0: 2 or 1 as b16
bool gDeathKindRule = true;
// capture: the game's 6 s in place of 5 for a player whose team has more players than the other (the session's players in a team)
int TeamCount(const Doll& d, int team) {
    const bool told = gRT.round == (d.gen & 0xff);
    int n = 0;
    for (int i = 0; i < gP.n; ++i) {
        const Seen& s = gP.seen[i];
        if (s.id < 0 || s.id >= 16 || s.kind == 1 || s.kind == 3) continue;
        const int t = told && gRT.team[s.id] < 2 ? gRT.team[s.id] : s.team;
        if (t == team) ++n;
    }
    return n;
}
int RespawnExtraFrames(const Doll& d, int rule) {
    return rule == 3 && (d.team == 0 || d.team == 1) && TeamCount(d, d.team) > TeamCount(d, 1 - d.team) ? 60 : 0;
}
bool RespawnDue(Doll& d, int rule) {
    if (!gRespawnWait) return gFrame - d.deadAt >= RespawnWait(rule);
    return DeadClock(d) >= (double)(GhostWaitFrames(d.deadKind) + d.respExtra) * 1000.0 / 59.94 - 0.5;
}
// (v150-host beta 18) TEAM SNEAKING: this doll defends (its team's goal not set, the other's set: the side that sneaks, as
// the body reads it, vdoll_body.cpp SneakSide)
bool TsneDefends(int team) {
    if (RuleNow() != 5 || !gHost.objectives || (team != 0 && team != 1)) return false;
    ObjNow it[4]; float goals[2][3]; bool set[2] = {false, false};
    if (gHost.objectives(it, 4, goals, set) <= 0 || set[0] == set[1]) return false;
    return !set[team];
}
uint16_t PlayerChan(const Doll& d, int kind) { return (uint16_t)((0x40 + 3 * d.slot + kind) | ((d.gen & 1) << 11)); }
// the room's channel: 0x62 (the US, JP and JPS programs' games of 8), 0x72 (the EU and KR programs', a big game)
uint16_t RoomChan(const Doll& d) { return (uint16_t)((gHost.roomChannel ? gHost.roomChannel() : 0x62) | ((d.gen & 1) << 11)); }
// (v150-host beta 17) a round's own channel: the players', the objects' and the room's (0x40 and up), in the half of the
// numbers that round uses (bit 11 is the round byte's bit 0, so a round's numbers come back two rounds later)
bool RoundChan(uint16_t chan, int gen) { return (chan & 0x7ff) >= 0x40 && ((chan >> 11) & 1) == (gen & 1); }

// (v150-host beta 22, relay) one datagram of a doll a read of the host's game. The session engine reads every datagram waiting
// on its socket in one pass (0x16E440: recvfrom until none, a doll's through Pop) and of the datagrams of one session that pass
// takes the reliable records and the acknowledgments of each, but the unreliable records of the LAST one only. Measured on the
// host's P2P logs (104 benches with playable dolls): a doll's datagram read in the same pass as a later one of the same doll
// had none of its unreliable records passed on and its hits not counted (sup_s1 f13381: a claymore's blast of 100 at a real
// client, his life left at 200), while its reliable records and acknowledgments were (the host acknowledged them 4 frames
// later and sent nothing again). A doll pushed a datagram whenever it had something and 2 frames had gone (its hit's or its
// item's records at once, an acknowledgment at once, its 100 ms records after), while the host's game reads every 2 to 9
// frames: 1.0 % of the dolls' datagrams were read so (8545 of 832458), 0.2 % of the real clients' (916 of 405886, whose games
// send every 100 ms), and with them 31 of its hit records, 470 of its shots' fire records, item takes and drops.
// MGO1_VDOLL_RELAY=0: beta 22's way
bool gRelay = true;
long long gPassNo = 0;                   // the host's game's reads: one at most between two Ticks (both on the game's thread)
long long gHeldSaid = -1;
void Reliable(Doll& d, uint16_t chan, const std::vector<uint8_t>& data) {
    OutRel r;
    r.chan = chan;
    r.seq = d.nextSeq[chan]++;
    r.data = data;
    d.rel.push_back(r);
}
void Unreliable(Doll& d, uint16_t chan, const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    std::vector<uint8_t> o;
    PutRec(o, chan, 0, -1, data.data(), data.size());
    d.unrel.push_back(o);
}

void Push(Doll& d, const std::vector<uint8_t>& body, bool bootstrap) {
    const uint32_t pk = bootstrap ? gKey : (d.nonce ^ d.hostNonce);
    const uint32_t mk = bootstrap ? gMac : (d.nonce ^ d.hostNonce ^ gMac);
    // (v150-host beta 21) its datagrams numbered with 16 bits, as the game numbers a session's (the sender 0x16F420 writes the
    // session's 16-bit count +0x28 and flips its top bit when the datagram got smaller compressed; the receiver 0x16FD58
    // counts with 16 bits too and takes a number 0x8000 away from the one it awaits for a compressed datagram). Until beta
    // 20 a doll counted 0 to 32767 and began again at 0: from its 32769th datagram on, the host's game unpacked every one
    // of them and read nothing (no acknowledgment, no claim, no place: the doll stood still, then its session was ended at
    // the game's resend limit, or held 180 s as a peer loading at a round's start), every 55 to 210 minutes, all dolls
    // together (the owner's logs of 11 and 12 November: 12 times each doll in two days)
    d.out.push_back(Seal(d.dseq, body, pk, mk));
    d.dseq = (uint16_t)(d.dseq + (!bootstrap && d.phase == JOINED ? gSeqStep : 1));
    ++d.dgramsOut;
    d.lastSend = gFrame;
}

// a datagram when there is something to send: the acknowledgments, the reliable records due, the others
void Flush(Doll& d, bool force) {
    const bool due = std::any_of(d.rel.begin(), d.rel.end(), [](const OutRel& r) { return r.sent < 0 || gFrame - r.sent >= 60; });
    if (!force && d.acks.empty() && !due && d.unrel.empty()) return;
    // (v150-host beta 22, relay) its last datagram not read yet by the host's game: nothing new, its records go together in the
    // next one once that is read (a second one waiting would be read in the same pass and lose the first's unreliable records)
    if (gRelay && !force && !d.out.empty()) {
        ++d.flushWaits;
        if (d.unrel.size() > 256) d.unrel.erase(d.unrel.begin(), d.unrel.begin() + (long)(d.unrel.size() - 256));   // (a read missing for seconds)
        return;
    }
    if (gFrame - d.lastSend < 2 && !force) return;
    std::vector<uint8_t> body;
    bool probeDone = false;
    for (const Ack& a : d.acks) {
        if (a.probe && !probeDone) { const uint8_t delay = 0; PutRec(body, a.chan, 0xC0, a.seq, &delay, 1); probeDone = true; }
        else PutRec(body, a.chan, 0x40, a.seq, nullptr, 0);
    }
    d.acks.clear();
    bool probe = false;
    for (OutRel& r : d.rel) {
        if (!(r.sent < 0 || gFrame - r.sent >= 60)) continue;
        if (body.size() + r.data.size() + 8 > 1100) break;
        PutRec(body, r.chan, probe ? 0 : 0x80, r.seq, r.data.data(), r.data.size());
        probe = true;
        r.sent = gFrame;
        ++r.tries;
    }
    for (const auto& u : d.unrel) if (body.size() + u.size() < 1200) body.insert(body.end(), u.begin(), u.end());
    d.unrel.clear();
    if (body.empty() && !force) return;
    Push(d, body, false);
}

// a message of the host's game network (channel 1)
void GameMessage(Doll& d, const uint8_t* m, size_t n) {
    if (!n) return;
    const uint8_t type = m[0];
    if (type == 0x07 && n >= 7) {                  // an object message: {len u16, object, u16, op}, its data
        const size_t len = Le16(m + 1);
        const uint8_t obj = m[3], op = m[6];
        const uint8_t* p = m + 7;
        if (7 + len > n) return;
        if (obj == 0 && op == 0 && len >= 15 && Le32(p + 7) == d.id && d.slot != p[0]) {
            d.slot = p[0];
            if (gDebug) VLOG("vdoll %d: the host's members: this doll is player %d", d.k + 1, d.slot + 1);
        }
        if (obj == 0 && op == 2 && len == 0 && !d.memberAsked && d.phase == JOINING) {   // the members told: the membership asked
            Reliable(d, 1, {0x0A});
            d.memberAsked = true;
        }
        // the player's object (object 7 in most rules, 6 in sneaking): {id, kind, ?, life, its channel, x y z, facing}
        // (v150-host beta 16) its place a number within 1 km: another object's data is not taken for the player's
        if (obj && op == 0 && len >= 21 && d.slot >= 0 && p[0] == d.slot && (Le16(p + 5) & 0xff) == 0x40 + 3 * d.slot &&
            std::fabs(LeF(p + 7)) < 1e6f && std::fabs(LeF(p + 11)) < 1e6f && std::fabs(LeF(p + 15)) < 1e6f) {
            d.spawn[0] = LeF(p + 7); d.spawn[1] = LeF(p + 11); d.spawn[2] = LeF(p + 15);
            d.facing = Le16(p + 19);
            d.haveSpawn = true;
            d.objKind = p[1]; d.objLife = (int)Le16(p + 3);
            if (gDebug) VLOG("vdoll %d: its object (%d): kind %d, life %d, its place %.0f %.0f %.0f", d.k + 1, obj, p[1], (int)Le16(p + 3), d.spawn[0], d.spawn[1], d.spawn[2]);
        }
        return;
    }
    if (type == 0x01 && n == 1 && d.phase == JOINED) { d.hostEnded = true; return; }   // the host ended its game (0x172E54)
    if (type == 0x0B && d.memberAsked && d.phase == JOINING) {   // the config blocks: joined
        d.phase = JOINED;
        d.joinedAt = gFrame;
        VLOG("vdoll %d: joined the host's game", d.k + 1);
        return;
    }
    if ((type == 0x83 || type == 0x84) && n >= 3) {
        if (type == 0x83 && m[1] == (uint8_t)d.gen) d.claimAckGen = d.gen;
        return;
    }
    if (type == 0x85 && n >= 2) {                  // each player's team: {round, (id, team) ...}
        if (gRT.round != m[1]) memset(gRT.team, 0xFF, sizeof gRT.team);
        gRT.round = m[1]; gRT.at = gFrame;
        for (size_t i = 2; i + 1 < n; i += 2) if (m[i] < 16) gRT.team[m[i]] = m[i + 1];
        for (size_t i = 2; i + 1 < n; i += 2)
            if (m[i] == d.slot && d.slot >= 0) {
                const int t = m[i + 1];
                if (t != d.team && gDebug) VLOG("vdoll %d: its team %d (round %d)", d.k + 1, t, m[1]);
                d.team = t;
                if ((t == 0 || t == 1) && d.assignedGen != m[1]) { d.assignedGen = m[1]; d.assignedAt = gFrame; }
            }
        return;
    }
}

// (v150-host beta 19) a player's game keeps the head byte of the last damage report on its player (the record {04 attacker,
// damage, stun, weapon, ?, head}: 0x4367A0 copies it to the player's +0xEF5, its own reports aside) and at its death tells
// the kind of the death {07 kind, killer} (0x3F44C8): 2, a head shot, only when that byte is set, else 1 (0x3F5828 for the
// local player). The host's game makes a copy's death a head shot when the kind is 2 (0x3F5994 sets +0xEF5; 0x409360 then
// sends the HUD's 0x751010 and counts +0x3A and +0x3C), else it keeps the last report's byte. Measured on the host's P2P
// logs of beta 18's benches: the playable dolls 326 deaths of kind 2 all after a head hit, 68 of kind 1 after a body hit;
// the virtual dolls sent 2 at every death, 185 times after a body hit (and the owner's log of 11 November: 57 kills of
// virtual dolls by real players, all head shot kills, 3 of them with the knife)
void LastHit(Doll& d, const uint8_t* m, size_t n) {   // (alive only: a game still drawing it alive may hit it once more)
    if (d.st == ALIVE && n >= 2 && m[1] < 16 && m[1] != d.slot) d.lastHead = n >= 9 && m[8] != 0;
}
uint8_t DeathKind(const Doll& d) {
    if (gDeathKindRule && d.down == 1) return d.thrown ? 5 : 4;   // ((v150-host beta 25, respawnwait) lying: 4, after a blast's throw 5)
    return d.lastHead ? 2 : 1;
}
// a record of a player's channels: the host's life for this doll
void PlayerRecord(Doll& d, uint16_t chan, const uint8_t* m, size_t n) {
    if (d.slot < 0 || !n) return;
    const int lo = chan & 0xff;
    if (lo < 0x40 || lo >= 0x70) return;
    const int slot = (lo - 0x40) / 3, kind = (lo - 0x40) % 3;
    if (slot != d.slot) return;
    if (kind == 0 && m[0] == 0x05 && n >= 3) {      // the host's life for it
        const int life = (int16_t)Le16(m + 1);
        if (life != d.life && gDebug) VLOG("vdoll %d: its life %d", d.k + 1, life);
        d.life = life;
        if (d.st == ALIVE && life > 0) d.lifeOk = true;
    }
    if (kind == 0 && m[0] == 0x04 && n >= 2 && m[1] < 16) d.killer = m[1];   // a damage report on its channel: {attacker, ...}
    // ((v150-host beta 24, stamina2) a hit on its life: the engine's 60 ticks with no new start of its moves' count, +0x44C)
    if (kind == 0 && m[0] == 0x04 && n >= 4 && m[1] != d.slot && (int16_t)Le16(m + 2) > 0) d.mover.Hit();
    if (kind == 0 && m[0] == 0x04) LastHit(d, m, n);
    // (v150-host beta 15) a stun (a CQC throw, a roll, the Mk22, a stun grenade) spends its stamina, a roll into it knocks
    // it down, a grab holds it
    if (d.st != ALIVE) return;
    if (kind == 0 && m[0] == 0x04 && n >= 6 && m[1] != d.slot) {
        const int stun = (int16_t)Le16(m + 4);
        // ((v150-host beta 24, stamina) as the player's own game, 0x4143F0: knocked out, a stun is not applied (0x41454C); up, a
        // teammate's negative stun is dropped (0x414788). MGO1_VDOLL_STAMINA=0: both taken, as beta 23)
        if (gStaminaRule && ((stun > 0 && d.down == 2) || (stun < 0 && d.down != 2))) {
            if (gDebug) VLOG("vdoll %d: a stun of %d from player %d not taken (%s, as a player's game: its stamina %d)", d.k + 1, stun, m[1] + 1,
                             d.down == 2 ? "it lies out cold" : "it is up", d.stamina);
        } else if (stun > 0) {
            if (gDebug) VLOG("vdoll %d: a stun of %d from player %d: its stamina %d -> %d", d.k + 1, stun, m[1] + 1, d.stamina, std::max(0, d.stamina - stun));
            const int was = d.stamina;
            d.stamina = std::max(0, d.stamina - stun);
            // ((v150-host beta 24, cqc) the throw from a hold: kind 0, no life)
            const bool holdThrow = n >= 7 && vcqc::HoldThrowRecord((int16_t)Le16(m + 2), m[6]);
            char how[64] = "";
            if (holdThrow) snprintf(how, sizeof how, " (a CQC throw from a hold: the stun %d)", stun);
            if (d.stamina <= 0 && d.down != 2) { d.stamina = 0; d.down = 2; d.downAt = gFrame; d.held = false; ++d.knocks;
                                                 d.slowWake = TsneDefends(d.team); d.asleep = n >= 7 && (m[6] == 5 || m[6] == 0x10); KoStart(d);
                                                 VLOG("vdoll %d: knocked out by player %d%s", d.k + 1, m[1] + 1, n >= 7 && (m[6] == 5 || m[6] == 0x10) ? " (asleep: a dart)" : n >= 7 && m[6] == 0x15 ? " (a stun grenade)" : how);
                                                 if (d.slowWake) VLOG("vdoll %d: a defender knocked out in team sneaking: it wakes 4 times slower, at once by a teammate's hit (as a player, tsne.h)", d.k + 1); }
            else if (gCqc && holdThrow && d.down == 0) {   // (v150-host beta 24, cqc) thrown, not knocked out: a thrown player's fall
                d.down = 1; d.downAt = gFrame; d.held = false; d.thrown = false; ++d.knocks; ++d.cqcThrows;
                RollStart(d);                            // (up at its first press once it lies awake, as after a roll)
                VLOG("vdoll %d: thrown by player %d with CQC from a hold: the stun %d, its stamina %d -> %d, it falls (its gauge not empty)", d.k + 1, m[1] + 1, stun, was, d.stamina);
            }
        } else if (stun < 0) {                       // (v150-host beta 18) a teammate's punch or kick on it knocked out: the game's
            d.stamina = std::min(30000, d.stamina - stun);   // own ally wake, a negative stun (0x4389C0): up at once when full
            // (v150-host beta 22, wake) then its getting up as after any other knock-out (KoWake: a client's length, mash's measures)
            if (d.down == 2 && d.stamina >= 30000) {
                KoWake(d);
                // ((v150-host beta 23, wake) a woken client's getting up, 118 of a player's frames: on mash3's player clock when KoWake
                // put it there (a player's game plays it at full pace on a slowed host too), else by the host's frames)
                if (gWokenRise && !d.asleep) {
                    d.upAt = gFrame + kWokenRiseFor;
                    if (d.riseMs >= 0) d.riseMs = KoClock(d) + (double)kWokenRiseFor * 1000.0 / 59.94;
                }
                VLOG("vdoll %d: woken by player %d's hit (its getting up %lld of a player's frames)", d.k + 1, m[1] + 1, d.upAt - gFrame);
            }
        }
    }
    if (kind == 1 && m[0] == 0x09 && n >= 3 && m[1] != d.slot && m[2] >= 6 && m[2] <= 9 && d.down == 0) {
        d.down = 1; d.downAt = gFrame; ++d.knocks;
        d.thrown = gReact && m[2] == 8;                 // (beta 22, blast: a blast's fall, kind 8; 9, a roll's, as before)
        // ((v150-host beta 25, reactfix) the M37's fall (6, 7) as a real client's (rf_m2, probe 54: the reaction 27 ticks, thrown 2.3 to 3 m,
        // then lying awake, the state bit 0x1B, until he presses; up 80 frames after the press): thrown as by a blast, lying at a player's 27
        // ticks on his clock, then up at its first press. MGO1_VDOLL_REACT_KIND=0: beta 21's knock-down clock (lying 56 frames after, up at 252)
        d.m37Fall = gReact && gReactKindV && (m[2] == 6 || m[2] == 7);
        if (d.m37Fall) { d.thrown = true; d.flClk = 0; d.flLastF = -1; d.fallEndMs = FlinchClock(d) + (double)kM37FallTicks * 2.0 * 1000.0 / 59.94; }
        // (v150-host beta 22, blast; review22a B3) kinds 6 and 7, the M37's fall (0x438B60), as beta 21: a real client's is not
        // measured (only kind 8's, blast_b21_d, blast_t1_d), so they keep beta 21's own clock
        if (d.thrown) { ++d.throws; d.flinchUntil = -1; d.copyThrown = false; }
        d.rollPress = false;
        if (d.thrown || m[2] == 9) RollStart(d);         // (v150-host beta 22, mash: a roll's and a blast's, up at its first press)
        VLOG("vdoll %d: knocked down by player %d%s", d.k + 1, m[1] + 1, d.m37Fall ? " (the M37's fall: thrown)" : d.thrown ? " (a blast: thrown)" : "");
    }
    // (v150-host beta 22, blast) a flinch: still, its fight and way over, as long as a player's animation holds him
    if (gReact && kind == 1 && m[0] == 0x09 && n >= 3 && m[1] != d.slot && m[2] >= 1 && m[2] <= 5 && d.down == 0 && !d.held) {
        const bool fresh = gFrame >= d.flinchUntil;
        if (fresh) { ++d.flinches; if (gDebug) VLOG("vdoll %d: flinches (player %d, the reaction %d)", d.k + 1, m[1] + 1, m[2]); }
        d.flinchUntil = gFrame + kFlinchFor;
        d.flinchAt = gFrame; d.flinchKind = m[2];
        if (gHitFall && FlinchTicks(m[2]) > 0) {         // ((v150-host beta 25, fallflinch) the kind's ticks on a player's clock, from now)
            if (fresh) { d.flClk = 0; d.flLastF = -1; }
            d.flinchEndMs = FlinchClock(d) + (double)FlinchTicks(m[2]) * 2.0 * 1000.0 / 59.94;
            d.flinchUntil = gFrame + 6LL * FlinchTicks(m[2]);   // (an upper bound: 3 times its frames on the slowest host; the clock ends it)
        } else d.flinchEndMs = -1;                       // (b16's 40 frames: no clock's end of an earlier kind)
    }
    if (kind == 1 && m[0] == 0x0D && n >= 3 && m[1] != d.slot) {
        const bool h = m[2] != 0;
        if (h != d.held) VLOG("vdoll %d: %s player %d", d.k + 1, h ? "held by" : "let go by", m[1] + 1);
        if (d.held && !h && !d.down) body::Resume(d.k);   // (let go: on from where the host's copy is)
        if (h && !d.held && !d.down) d.downAt = gFrame;
        d.held = h;
    }
}

void Deliver(Doll& d, uint16_t chan, bool reliable, const uint8_t* m, size_t n) {
    if (chan == 0 && reliable && n == 4 && !d.keyed) {   // the host's number: the session's keys
        d.hostNonce = Le32(m);
        d.keyed = true;
        if (gDebug) VLOG("vdoll %d: the host's key in", d.k + 1);
        return;
    }
    if (chan == 1) { GameMessage(d, m, n); return; }
    const int lo = chan & 0xff;
    if ((lo == 0x62 || lo == 0x72) && n >= 2 && m[0] == 0x04 && m[1] == d.slot && d.slot >= 0) { d.kicked = true; return; }   // kicked
    PlayerRecord(d, chan, m, n);
}

void Take(Doll& d, const uint8_t* clear, size_t n) {
    if (n < 2 || d.phase == OUT) return;
    std::vector<Rec> recs;
    if (!Records(clear + 2, n - 2, recs)) { if (gDebug) VLOG("vdoll %d: a datagram of the host not read (%zu bytes)", d.k + 1, n); }
    ++d.dgramsIn;
    d.lastIn = gFrame;
    for (const Rec& r : recs) {
        if (r.fl & 0x40) {                         // an acknowledgment of one of ours
            if (r.seq < 0) continue;
            d.rel.erase(std::remove_if(d.rel.begin(), d.rel.end(), [&](const OutRel& o) { return o.chan == r.chan && o.seq == (uint8_t)r.seq && o.sent >= 0; }),
                        d.rel.end());
            continue;
        }
        if (r.seq >= 0) {                          // reliable: acknowledged, taken once
            d.acks.push_back({r.chan, (uint8_t)r.seq, (r.fl & 0x80) != 0});
            auto& s = d.seen[r.chan];
            if (std::find(s.begin(), s.end(), (uint8_t)r.seq) != s.end()) continue;
            s.push_back((uint8_t)r.seq);
            if (s.size() > 96) s.pop_front();
            Deliver(d, r.chan, true, r.d, r.n);
        } else Deliver(d, r.chan, false, r.d, r.n);
    }
}

// ---- the player's records, as a PC's game sends them (the reference captures of full dolls, docs/VIRTUAL_DOLL.md 4) ----
// (v150-host beta 16) a place that is no number, or 1 km away, is never sent (Unreliable leaves an empty record out)
bool Sane(const float p[3]) { return std::fabs(p[0]) < 1e6f && std::fabs(p[1]) < 1e6f && std::fabs(p[2]) < 1e6f; }
std::vector<uint8_t> PosRec(const Doll& d) {          // {0, x, y, z} floats, y the hips (800 over the floor standing)
    std::vector<uint8_t> p;
    if (!Sane(d.pos)) return p;
    p.push_back(0x00);
    PutF(p, d.pos[0]); PutF(p, d.pos[1]); PutF(p, d.pos[2]);
    return p;
}
// (v150-host beta 16: no number goes as 0, a value past the record's 16 bits as their end)
uint16_t Tenth(float v) {
    if (!std::isfinite(v)) return 0;
    return (uint16_t)(int16_t)std::max(-32768L, std::min(32767L, (long)lrintf(v / 10.f)));
}
// the state tick every 100 ms: {3, state (1 spawned and not deployed yet, 0 in play, dead too, 3 a spectator), event, stun
// 30000, 300, 0}
// its state tick {03, state, event (4 down), stamina u16, 0x12C, flags (8 knocked out)}
void StateTick(Doll& d, uint8_t state) {
    const uint16_t st = (uint16_t)std::max(0, std::min(30000, d.stamina));
    // (v150-host beta 22, blast) thrown: the down event once it lies (a real client's: none while its body flies), and on
    // through its getting up
    // ((v150-host beta 22, mash) a roll's knock-down as a client's too: none while it falls, from its landing to {0A 0}: V21_owner,
    // mash_c2; beta 22 from the record to its getting up)
    const bool down = d.thrown || (d.rollPress && d.down != 2) ? d.fell : (d.down == 1 || d.down == 2 || (d.down == 3 && d.koRise));
    // (v150-host beta 22, mash) its last byte, knocked out, is the recovery's count (+0xB77: 8 + the ticks with a press among the
    // last 32), as a real client's (mash_a1: 08, 09, 0a ... 10 while it mashes); beta 15 sent a constant 8 (a player pressing nothing)
    const int ko = gMash ? std::max(vmash::kBase, std::min(vmash::kBase + vmash::kWindow, d.ring.count)) : 0x08;
    // (v150-host beta 22, bookplay) reading a book, in play: 2, as a reader's game sends it (body::StateEvent): its copies read
    // while it says so and stand up again at the first record without it
    const uint8_t event = down ? 4 : state == 0 && d.st == ALIVE ? (uint8_t)body::StateEvent(d.k) : 0;
    std::vector<uint8_t> r = {0x03, state, event, (uint8_t)st, (uint8_t)(st >> 8), 0x2c, 0x01, (uint8_t)(d.down == 2 ? ko : 0x00)};
    // ((v150-host beta 25, food) a ninth byte, the foods eaten since the last one, as a player's game adds it (US n003a 0x403810:
    // its +0xFB1, counted by the eating 0x407B14, then cleared); the host's game reading it counts each as the player's food in his
    // statistics (0x405F54 -> 0x3F09D0 with 0xA, +0x44 of his variable set). Measured: a real client's {03} at its take ends 01)
    if (d.foodsOwed > 0) { r.push_back((uint8_t)std::min(255, d.foodsOwed)); d.foodsOwed = 0; }
    Unreliable(d, PlayerChan(d, 0), r);
}
void AliveRecords(Doll& d) {                           // on deploying, then every half second
    Unreliable(d, PlayerChan(d, 2), {0x12, 0xff});
    Unreliable(d, PlayerChan(d, 0), PosRec(d));
    Unreliable(d, PlayerChan(d, 0), {0x01, 0x00, 0x00, (uint8_t)d.facing, (uint8_t)(d.facing >> 8), 0x00, 0x00});
    Unreliable(d, PlayerChan(d, 1), {0x06, 0x00});
    Unreliable(d, PlayerChan(d, 2), {0x13, 0x00, 0x00, 0x00});
}
// in play, every 100 ms (the body's moves, aim and shots take this place: docs/VIRTUAL_DOLL.md 4)
void AliveTick(Doll& d) {
    StateTick(d, 0);
    Unreliable(d, PlayerChan(d, 0), PosRec(d));
    if (gFrame - d.lastFull >= 30) { AliveRecords(d); d.lastFull = gFrame; }
}
// down, every 100 ms: {7, type (2 taken down, 1 by its own hand), the attacker}
void DownTick(Doll& d, uint8_t type, int by) {
    StateTick(d, 0);
    Unreliable(d, PlayerChan(d, 2), {0x12, 0xff});
    Unreliable(d, PlayerChan(d, 1), {0x07, type, (uint8_t)(by >= 0 ? by : d.slot)});
    Unreliable(d, PlayerChan(d, 2), {0x13, 0x00, 0x00, 0x00});
}
// a place for its spawn record: a respawn point of its team (the map's .geom by the round's rule), else where it was given.
// (v150-host beta 16) The call is given where it is now (where it fell: BASE CAPTURE's nearest base of its team)
void SpawnPlace(Doll& d, int team, float p[3]) {
    p[0] = d.spawn[0]; p[1] = d.spawn[1] - 800.f; p[2] = d.spawn[2];
    if (!gHost.spawnPoint) return;
    float q[3] = {d.pos[0], d.pos[1] - 800.f, d.pos[2]};
    if (!Sane(q)) memcpy(q, p, sizeof q);
    // (v150-host beta 21, spin) not a point the body's ways cannot leave (a patch of its mesh with no way out, a place fenced
    // in: it paced there until it fell): the next point, 8 tried at most (the last one kept if none will do)
    for (int i = 0; i < 8; ++i) {
        float t[3] = {q[0], q[1], q[2]};
        if (!(gHost.spawnPoint(team, d.pick++, t) && Sane(t))) break;
        if (i > 0 && t[0] == p[0] && t[1] == p[1] && t[2] == p[2]) break;   // (the same point given again: no other to take, it is kept)
        if (i > 0) VLOG("vdoll %d: the respawn point %.0f %.0f %.0f left out: no way of its body leads out of it", d.k + 1, p[0], p[1], p[2]);
        memcpy(p, t, sizeof t);
        if (!body::On() || body::CanLeave(t)) break;
    }
}
// (v150-host beta 16) its player's class: the one the host's game holds for its copy now, else its object's kind at the
// round's start. Logged at each change (`vdoll 2: its player is the special character 5`: the game's unique characters,
// a team's best player)
int ClassNow(Doll& d) {
    int c = gHost.playerClass && d.slot >= 0 ? gHost.playerClass(d.slot) : -1;
    if (c < 0) c = d.objKind;
    if (c < 0 || c > 8) return d.cls;
    d.cls = d.clsGame = c;
    // (v150-host beta 16, the configurations' audit) once a round, a soldier in a team: its team's class asked, so that a
    // team whose soldiers play another class than the settings' unit is logged by the platform before any join needs it
    if (!d.unitChecked && c <= 2 && (d.team == 0 || d.team == 1) && RuleNow() > 0 && gHost.teamClass) { d.unitChecked = true; gHost.teamClass(d.team); }
    if (c != d.clsSaid) {
        d.clsSaid = c;
        if (c >= 3 && c <= 7) VLOG("vdoll %d: its player is the special character %d (the unique characters)", d.k + 1, c);
        else VLOG("vdoll %d: its player's class %d (%s)", d.k + 1, c, c == 8 ? "Snake" : c == 0 ? "KGB" : c == 1 ? "GRU" : "the Ocelot Unit");
    }
    return c;
}
// (v150-host beta 16) the class a spawn record tells. The game writes its player's own class there (the spawn command's
// character) and every program makes the player's body again from it when it differs from the copy's: a class whose
// body the round's rules do not hold leaves the copy without a body, and the game then reads its bones through
// nothing (the owner's bot, 8 to 10 November: a virtual doll's respawn in capture, GRU against the Ocelot Unit, told
// class 0, KGB, as every respawn until beta 15; from the spectators it told 1, GRU). In play again: the class its copy
// has (no change). Into a team from the spectators: that team's soldiers' class, else its copy's.
// When neither tells it (ClassNow: the copy's class now, its object's kind of this round, the class it had or joined
// with this round), no doll waits a round for a class:
// - its team's soldiers' class (a body every round of that team holds);
// - where the host's game checks a spawn's character (spawnGuard: a class the round does not hold keeps the copy's
//   present one): the last class it had in this game; none ever, 2 s held (asked again each frame), then its side's
//   unit by the game's default (GRU red, KGB blue; deathmatch: the unit it picked, which the host makes its class);
// - where it does not (another disc's program, the guard turned off): held, said once. A guess could be a body the
//   round does not hold.
// -1: held this frame
int SpawnClass(Doll& d, int team, bool joining, const char* what) {
    const int own = ClassNow(d);
    const int unit = gHost.teamClass ? gHost.teamClass(team) : -1;
    int c = joining && unit >= 0 ? unit : own;
    const char* from = nullptr;
    if (c < 0 && unit >= 0) { c = unit; from = "its team's soldiers' class"; }
    const bool guard = gHost.spawnGuard && gHost.spawnGuard();
    if (c < 0 && guard && d.clsGame >= 0) { c = d.clsGame; from = "the last class it had in this game"; }
    if (c < 0 && guard) {
        if (d.heldSince < 0) d.heldSince = gFrame;
        if (gFrame - d.heldSince >= 120) {
            c = RuleNow() == 0 ? (team >= 0 && team <= 2 ? team : 0) : team == 1 ? 0 : 1;
            from = "its side's default unit, 2 s on";
        }
    }
    if (c < 0) {
        if (!d.clsHeld) {
            d.clsHeld = true;
            VLOG("vdoll %d: %s held: its player's class is not known (the host's copy not read, no object this round)%s", d.k + 1, what,
                 guard ? ": 2 s at most" : "");
        }
        return -1;
    }
    d.clsHeld = false; d.heldSince = -1;
    if (from && !d.clsGuessSaid) {
        d.clsGuessSaid = true;
        VLOG("vdoll %d: its player's class is not known this round (the host's copy not read, no object): %s tells %s (%d)", d.k + 1, what, from, c);
    }
    return c;
}
// {2, kind (1 in play, 2 a ghost, 3 a spectator), team, its class, x y z in tenths, facing}, then off any ladder ({0x0F,
// 0xFF, y, y, 4, 0x80}, the form both protocols read), nothing attached, its place
void SpawnRecords(Doll& d, uint8_t kind, uint8_t team, uint8_t cls, const float p[3]) {
    d.pos[0] = p[0]; d.pos[1] = p[1] + 800.f; d.pos[2] = p[2];
    std::vector<uint8_t> r = {0x02, kind, team, cls};
    Put16(r, Tenth(d.pos[0])); Put16(r, Tenth(d.pos[1] - 10.f)); Put16(r, Tenth(d.pos[2])); Put16(r, d.facing);
    Unreliable(d, PlayerChan(d, 0), r);
    Unreliable(d, PlayerChan(d, 1), {0x0a, (uint8_t)d.slot, 0x00});
    const uint16_t y = Tenth(d.pos[1]);
    Unreliable(d, PlayerChan(d, 1), {0x0f, 0xff, (uint8_t)y, (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y >> 8), 0x04, 0x80});
    Unreliable(d, PlayerChan(d, 1), {0x10, 0x00, 0x00, 0x00, 0x00});
}
// one life (rescue, sneaking, team sneaking): after the death's wait a ghost to the round's end where it fell (its spawn of kind
// 2), then every 100 ms a state tick of 2, nothing in hand, its place and facing, as a PC's game sends its ghost
void GhostSpawn(Doll& d) {
    float p[3] = {d.pos[0], d.pos[1] - 800.f, d.pos[2]};
    const int cls = SpawnClass(d, d.team, false, "its ghost");
    if (cls < 0) return;
    SpawnRecords(d, 2, (uint8_t)(d.team == 1 ? 1 : 0), (uint8_t)cls, p);
    d.st = GHOST; d.nextTick = gFrame;
    // ((v150-host beta 25, ghostwait) the wait said: its frames since its death, and on a player's clock with the kind told)
    if (gGhostWait)
        VLOG("vdoll %d: a ghost to the round's end (one life) at %.0f %.0f %.0f, %lld frames after its death (%.1f s on a player's clock, its death's kind %d)",
             d.k + 1, d.pos[0], d.pos[1], d.pos[2], gFrame - d.deadAt, d.deadMs / 1000.0, d.deadKind);
    else VLOG("vdoll %d: a ghost to the round's end (one life) at %.0f %.0f %.0f", d.k + 1, d.pos[0], d.pos[1], d.pos[2]);
}
void GhostTick(Doll& d) {
    StateTick(d, 2);
    Unreliable(d, PlayerChan(d, 2), {0x12, 0xff});
    Unreliable(d, PlayerChan(d, 2), {0x13, 0x00, 0x00, 0x00});
    Unreliable(d, PlayerChan(d, 0), PosRec(d));
    Unreliable(d, PlayerChan(d, 0), {0x01, 0x00, 0x00, (uint8_t)d.facing, (uint8_t)(d.facing >> 8), 0x00, 0x00});
    Unreliable(d, PlayerChan(d, 1), {0x06, 0x00});
}
// back in play after a death (the rules with a respawn): a respawn point of its team, state 1, deployed 0.3 s later
void Respawn(Doll& d) {
    const int cls = SpawnClass(d, d.team, false, "its respawn");
    if (cls < 0) return;
    float p[3];
    // (v150-host beta 21) a SNEAKING soldier: one of the seven points of the stage's list of the soldiers' starts and
    // respawns (0DC68A to 0DC690), taken at random as a player's game takes it (the script's rand(8) into the list 1881:
    // doll_geom.cpp RespawnKinds, which leaves out the eighth place the game reads past the list's end, beside Snake's
    // start or his goal on some maps). The map's points not read yet: where the host's game put it at this round's start
    // (a point of that list too)
    if (RuleNow() == 4 && d.team == 0) {
        static uint32_t rnd = 0x2545F491u;
        rnd = rnd * 1103515245u + 12345u + (uint32_t)gFrame + (uint32_t)d.k * 7919u;
        // (not the point another soldier doll took in the last 2 s: two killed together would stand in each other; the
        // next point of the list then)
        bool have = false;
        for (unsigned tries = 0; tries < 7 && gHost.spawnPoint; ++tries) {
            float q[3] = {d.pos[0], d.pos[1] - 800.f, d.pos[2]};
            if (!Sane(q)) q[0] = q[1] = q[2] = 0.f;
            if (!gHost.spawnPoint(0, (rnd >> 12) + tries, q) || !Sane(q)) break;
            memcpy(p, q, sizeof p); have = true;
            bool taken = false;
            for (const Doll& o : gDolls)
                if (&o != &d && o.phase == JOINED && o.team == 0 && gFrame - o.spawnSentAt < 120 && std::fabs(o.pos[0] - q[0]) < 500.f && std::fabs(o.pos[2] - q[2]) < 500.f)
                    taken = true;
            if (!taken) break;
        }
        if (have) {}
        else if (Sane(d.start) && (d.start[0] != 0.f || d.start[2] != 0.f)) { p[0] = d.start[0]; p[1] = d.start[1] - 800.f; p[2] = d.start[2]; }
        else SpawnPlace(d, d.team, p);
    } else SpawnPlace(d, d.team, p);
    SpawnRecords(d, 1, (uint8_t)d.team, (uint8_t)(gTestClass >= 0 ? gTestClass : cls), p);
    Unreliable(d, PlayerChan(d, 0), PosRec(d));
    StateTick(d, 1);
    d.st = WAIT; d.deployAt = gFrame + 18; d.nextTick = gFrame + 6; d.spawnSentAt = gFrame;
    d.deployMs = d.freeMs = -1;
    // ((v150-host beta 25, respawnwait) on the player's clock of its death: its weapons page's frames, then held to a player's deploy)
    if (gRespawnWait) {
        d.spawnMs = DeadClock(d);
        d.deployMs = d.spawnMs + (double)kRespawnPage * 1000.0 / 59.94;
        if (kRespawnDeploy > kRespawnPage) d.freeMs = d.spawnMs + (double)kRespawnDeploy * 1000.0 / 59.94;
        d.deployAt = gFrame;
    }
    ++d.respawns;
    if (gTestClass >= 0) VLOG("vdoll %d: respawns at %.0f %.0f %.0f, telling the class %d in place of its own %d (MGO1_VDOLL_TEST_CLASS)", d.k + 1, d.pos[0], d.pos[1], d.pos[2], gTestClass, cls);
    else if (gRespawnWait)
        VLOG("vdoll %d: respawns at %.0f %.0f %.0f, %lld frames after its death (%.1f s on a player's clock, its death's kind %d%s)", d.k + 1, d.pos[0], d.pos[1], d.pos[2],
             gFrame - d.deadAt, d.spawnMs / 1000.0, d.deadKind, d.respExtra ? ", 1 s more: its team the bigger" : "");
    else VLOG("vdoll %d: respawns at %.0f %.0f %.0f", d.k + 1, d.pos[0], d.pos[1], d.pos[2]);
}
// (v150-host beta 14) a side the game does not let go: sneaking's Snake (the game spawns him again as Snake; the full dolls'
// pause menu took a death for nothing in a loop, Snake's report of 27 October), or three changes this round the host did not
// take: it stays in its side
bool SpecBarred(Doll& d, int rule) {
    const bool snake = rule == 4 && d.team == 1 && (d.objKind == 8 || d.objLife > 200);
    if ((snake || d.specTries >= 3) && d.holdWhy != "in its side") {
        d.holdWhy = "in its side";
        VLOG("vdoll %d: stays in its side, no real player in the game (%s)", d.k + 1, snake ? "sneaking's Snake" : "the host kept it there 3 times this round");
    }
    return snake || d.specTries >= 3;
}
// a spectator (no real player in the game, as the full dolls do it by the pause menu: doll_brain.cpp MenuFrame): its team
// change to 0xFE asked of the room ({9, id, 0xFE}), its life given up as a PC's game does it, down by its own hand; after the
// respawn's wait the spectator's spawn (kind 3, team 0xFE, at a camera point) and the change done ({0x0A, id, 0xFE}); then a
// state tick of 3 every 100 ms, nothing else
body::Self BodySelf(Doll& d, bool tick);
void GoSpectator(Doll& d) {
    ++d.specTries;
    Reliable(d, RoomChan(d), {0x09, (uint8_t)d.slot, 0xFE});
    std::vector<uint8_t> dmg = {0x04, (uint8_t)d.slot};
    Put16(dmg, (uint16_t)std::max(1, d.life > 0 ? d.life : 200)); Put16(dmg, 0);
    dmg.push_back(0x00); dmg.push_back(0x00); dmg.push_back(0x00);
    Unreliable(d, PlayerChan(d, 0), dmg);
    d.st = GOING_SPECTATOR; d.alive = false; d.specAt = gFrame; d.nextTick = gFrame + 6;
    // (v150-host beta 22, pickup2: review22b P7) a life given up is a death: a PC's game drops what he holds at it, as at any death
    if (body::On()) { body::Self s = BodySelf(d, false); body::Died(s); }
    body::Down(d.k);
    VLOG("vdoll %d: goes spectator (no real player in the game)", d.k + 1);
}
void SpectatorDone(Doll& d) {
    float p[3] = {d.pos[0], d.pos[1] + 8000.f, d.pos[2]};
    if (gHost.spawnPoint) { float c[3]; if (gHost.spawnPoint(0xFE, d.pick++, c) && Sane(c)) memcpy(p, c, sizeof p); }
    int cls = ClassNow(d);                            // (a spectator's spawn changes no body: its class kept all the same,
    if (cls < 0) cls = d.clsGame >= 0 ? d.clsGame : 0; // never held)
    SpawnRecords(d, 3, 0xFE, (uint8_t)cls, p);
    StateTick(d, 3);
    Reliable(d, RoomChan(d), {0x0A, (uint8_t)d.slot, 0xFE});
    d.st = SPECTATOR; d.nextTick = gFrame + 6;
    VLOG("vdoll %d: a spectator", d.k + 1);
}
// a spectator into a team: the change asked and done at once ({9, id, team}, {0x0A, id, team}), its spawn from a spectator
// at a respawn point of that team, state 1, deployed 0.3 s later
void JoinFromSpectator(Doll& d, int team) {
    const int cls = SpawnClass(d, team, true, "its join from the spectators");
    if (cls < 0) return;
    d.cls = d.clsGame = cls;                          // (what its copy is made as: its next spawn's class if nothing else tells it)
    Reliable(d, RoomChan(d), {0x09, (uint8_t)d.slot, (uint8_t)team});
    Reliable(d, RoomChan(d), {0x0A, (uint8_t)d.slot, (uint8_t)team});
    float p[3];
    SpawnPlace(d, team, p);
    SpawnRecords(d, 1, (uint8_t)team, (uint8_t)cls, p);
    StateTick(d, 1);
    d.team = team; d.specTeam = team;
    d.pickGen = d.gen; d.pickTeam = RuleNow() == 0 ? -1 : team;   // (no pick in this round: the team change is its join)
    d.st = WAIT; d.deployAt = gFrame + 18; d.nextTick = gFrame + 6; d.spawnSentAt = gFrame;
    d.deployMs = d.freeMs = -1;                          // ((v150-host beta 25, respawnwait) not a respawn)
    VLOG("vdoll %d: joins the %s team from the spectators", d.k + 1, team ? "blue" : "red");
}

// the team a doll joins (doll_brain.cpp JoinTeam): the one set, else the dolls out of a team (this one too) take their turns
// in their slots' order, each to the side with fewer players then (the session's players only), red on a tie. A player's
// team: the host's for this round (its 0x85), a virtual doll's pick on its way, else the session's
int JoinTeam(const Doll& d) {
    const bool told = gRT.round == (d.gen & 0xff) && gFrame - gRT.at <= 120;
    int n[2] = {0, 0};
    std::vector<int> out = {d.slot};
    for (int i = 0; i < gP.n; ++i) {
        const Seen& s = gP.seen[i];
        if (s.id == d.slot || s.kind == 1 || s.id < 0 || s.id >= 16) continue;
        int t = told ? gRT.team[s.id] : s.team;
        for (const Doll& o : gDolls)
            if (o.slot == s.id && o.phase == JOINED && o.pickGen == o.gen && o.gen == d.gen && (o.pickTeam == 0 || o.pickTeam == 1)) t = o.pickTeam;
        if (t == 0 || t == 1) ++n[t];
        else if (s.kind == 2) out.push_back(s.id);
    }
    // (v150-host beta 14) sneaking: the blue side is Snake's alone (one who joins it becomes a second Snake and the game plays
    // the round again: a virtual doll set blue, the scratchpad's audit_sne/after_vp): with someone there, the red team
    if (RuleNow() == 4 && n[1] > 0) return 0;
    if (d.wantTeam == 0 || d.wantTeam == 1) return d.wantTeam;
    std::sort(out.begin(), out.end());
    for (int id : out) {
        const int side = n[1] < n[0] ? 1 : 0;
        if (id == d.slot) return side;
        ++n[side];
    }
    return 0;
}

// (the body, vdoll_body.cpp) its records on a player's channels; its moves, look, aim and shots each frame alive
void BodySend(void* ctx, int slot, int kind, const std::vector<uint8_t>& rec) {
    Doll& d = *(Doll*)ctx;
    // (v150-host beta 16) nothing from a doll out of the game (its grenade going off after its leave), nor for a player
    // the game has no channel for
    // (v150-host beta 22, pickup) kind 3: the drops' channel of the host's game (the room's less 2: 0x60, 0x70 in a big game),
    // where a player's game sends its take of a special item and the weapons it drops (the host passes them on to every player)
    if (d.phase == JOINED && d.slot >= 0 && kind == 3) { Unreliable(d, (uint16_t)(RoomChan(d) - 2), rec); return; }
    // (v150-host beta 22, support) kind 4: the claymores' list's channel (the room's + 4: 0x66, US n003a 0x3A5D7C), kind 5: the
    // books' (the room's + 10: 0x6C, 0x3A6FA8), where a player's game tells its claymore or book laid, a claymore taken, gone off
    if (d.phase == JOINED && d.slot >= 0 && (kind == 4 || kind == 5)) { Unreliable(d, (uint16_t)(RoomChan(d) + (kind == 4 ? 4 : 10)), rec); return; }
    // (v150-host beta 25, food) kind 6: the mushroom man's channel (the room's + 11: 0x6D, US n003a 0x3A821C), where a player's game
    // tells a food box it took {the entry's number, its point in tenths, 3} (the host removes the box and passes it on to every player)
    if (d.phase == JOINED && d.slot >= 0 && kind == 6) { Unreliable(d, (uint16_t)(RoomChan(d) + 11), rec); return; }
    if (d.phase != JOINED || d.slot < 0 || slot < 0 || slot >= 16 || kind < 0 || kind > 2) return;
    if (slot == d.slot && kind == 2 && rec.size() == 4 && rec[0] == 0x13) memcpy(d.held13, rec.data(), 4);   // ((v150-host beta 25, fallflinch) FlinchTick's)
    Unreliable(d, (uint16_t)((0x40 + 3 * slot + kind) | ((d.gen & 1) << 11)), rec);
}
body::Self BodySelf(Doll& d, bool tick) {
    body::Self s;
    s.k = d.k; s.slot = d.slot; s.team = d.team; s.life = d.aliveAt; s.tick = tick; s.hp = d.life; s.attacker = d.killer;
    s.pos = d.pos; s.facing = &d.facing; s.send = BodySend; s.ctx = &d;
    return s;
}
void BodyTick(Doll& d, bool tick) {
    body::Self s = BodySelf(d, tick);
    body::Alive(s, gFrame);
}
// (v150-host beta 15) knocked down, out or held, every 100 ms, as a player's game sends it:
// its state tick alone a moment (held: nothing else), then its fall {10 0} {0A id 1}, lying {0A id 4}; up again: getting up
// {0A id 5} 66 frames, then {0A id 0} and its body on from where the host's copy lies. Knocked down 252 frames; knocked
// out until its stamina is back (144 a record: about 21 s from nothing)
// (v150-host beta 22, blast) a blast's fall as a real client's game tells it (gReact's note): flying kThrownFor frames with
// nothing but its state ticks, then lying where the host's game threw its copy, up at once a player's input is taken
void ThrownTick(Doll& d) {
    const long long t = gFrame - d.downAt;
    // (v150-host beta 22, review) knocked out while its body flies or lies (a stun that empties its stamina: the Mk22, a stun
    // grenade, a roll at a lying player): its stamina back as DownedTick gives it, then up (it lay to its death without this)
    if (d.down == 2) {
        KoTick(d, !d.fell);                              // (v150-host beta 22, mash: the game's rule; no press counted while it flies)
        if (d.stamina >= 30000) KoWake(d);
    }
    // its copy in the host's game: thrown (0x6000000), then lying awake (0x8000000 alone): its landing then, as a client's
    bool copyDown = false, copyAwake = false;
    if (!d.fell && gHost.see) { PlayerNow c; if (gHost.see(d.slot, &c) && c.object) { copyDown = (c.state & 0x6000000u) != 0; copyAwake = (c.state & 0xE000000u) == 0x8000000u; } }
    if (copyDown) d.copyThrown = true;
    // ((v150-host beta 25, reactfix) the M37's fall: its 27 ticks of a player's clock, its copy's state aside)
    const bool lands = !d.fell && (d.m37Fall ? FlinchClock(d) >= d.fallEndMs - 0.5 : t >= kThrownMin && (d.copyThrown ? copyAwake || t >= kThrownMax : t >= kThrownFor));
    if (d.rollPress && !d.fell && d.down == 1) RollTick(d);   // (v150-host beta 22, mash: its presses while it flies count for nothing)
    if (lands) d.fell = true;                        // (the down event in this tick's state, with its landing's records)
    StateTick(d, 0);
    Unreliable(d, PlayerChan(d, 2), {0x12, 0xff});
    Unreliable(d, PlayerChan(d, 2), {0x13, 0x00, 0x00, 0x00});
    if (!d.fell) return;
    int team = 0, life = 0; float at[3];
    const bool copy = gHost.player && gHost.player(d.slot, &team, &life, at) && Sane(at) &&
                      std::hypot(at[0] - d.pos[0], at[2] - d.pos[2]) < 12000.f && std::fabs(at[1] - d.pos[1]) < 3000.f;
    if (copy) memcpy(d.pos, at, sizeof d.pos);        // (where the host's game threw its copy: the place it lies, then rises)
    if (lands) {
        d.upAt = gFrame + kPressAfter;
        Unreliable(d, PlayerChan(d, 1), {0x10, 0x00, 0x00, 0x00, 0x00});
        Unreliable(d, PlayerChan(d, 0), PosRec(d));
        Unreliable(d, PlayerChan(d, 0), {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
        Unreliable(d, PlayerChan(d, 1), {0x0A, (uint8_t)d.slot, 0x01, 0x00, 0x00});
        VLOG("vdoll %d: lies at %.0f %.0f %.0f, %.1f s after the %s record (%s)", d.k + 1, d.pos[0], d.pos[1], d.pos[2], t / 59.94, d.m37Fall ? "M37's" : "blast's",
             d.m37Fall ? "a player's 27 ticks by his clock" : !d.copyThrown ? "its copy's fall not seen: the measured time" : copyAwake ? "its copy lying awake there" : "its copy still not awake");
        return;
    }
    // ((v150-host beta 22, mash) up at its hand's first press once it lies awake, a press's way to {0A 5} as measured; beta 22 and
    // MGO1_VDOLL_MASH=0: kPressAfter, a press every 4 frames)
    if (d.down == 1 && (d.rollPress ? RollTick(d) : gFrame >= d.upAt)) { d.down = 3; d.upAt = gFrame + kRisingFor; }
    if (d.down == 3 && RiseDone(d)) {
        Unreliable(d, PlayerChan(d, 1), {0x0A, (uint8_t)d.slot, 0x00});
        Unreliable(d, PlayerChan(d, 0), PosRec(d));
        VLOG("vdoll %d: up again after %.1f s", d.k + 1, (gFrame - d.downAt) / 59.94);
        d.down = 0; d.fell = false; d.held = false; d.thrown = false; d.koRise = false; d.riseMs = -1; d.rollPress = false; d.asleep = false;
        body::Resume(d.k);
        return;
    }
    if (((gFrame - d.downAt) / 6) % 3 == 0) Unreliable(d, PlayerChan(d, 0), PosRec(d));   // (its place every third tick, as a client's)
    Unreliable(d, PlayerChan(d, 1), {0x0A, (uint8_t)d.slot, (uint8_t)(d.down == 3 ? 5 : 1), 0x00, 0x00});
}
// (v150-host beta 22, blast) a flinch: its state ticks only, still, its fight dropped (the body's Pause); on from where the
// host's copy stands after it (the animation pushes it back a little)
void FlinchTick(Doll& d) {
    StateTick(d, 0);
    Unreliable(d, PlayerChan(d, 2), {0x12, 0xff});
    int team = 0, life = 0; float at[3];                // (its place: the host's copy's, as its animation pushes it)
    if (gHost.player && gHost.player(d.slot, &team, &life, at) && Sane(at) && std::hypot(at[0] - d.pos[0], at[2] - d.pos[2]) < 2000.f &&
        std::fabs(at[1] - d.pos[1]) < 1000.f) { memcpy(d.pos, at, sizeof d.pos); if (!gHitFall) Unreliable(d, PlayerChan(d, 0), PosRec(d)); }
    // ((v150-host beta 25, fallflinch) a player's game sends no place while his reaction runs (ff_hits1's P2P log: his state, {12 ff} and
    // his weapon's record each tick, his place again after it): the doll's place is still taken from its host copy for its next records.
    // His weapon's record is the one he sent before the hit ({13 00 0c 02}: his item, the weapon still in his hands, loaded), where the
    // doll's said nothing in its hands ({13 00 00 00}, b16: ff_ba_before's and ff_ba_after's P2P logs): its body's last one now
    if (gHitFall) Unreliable(d, PlayerChan(d, 2), {d.held13[0], d.held13[1], d.held13[2], d.held13[3]});
    else Unreliable(d, PlayerChan(d, 2), {0x13, 0x00, 0x00, 0x00});
}
void DownedTick(Doll& d) {
    if (d.thrown) { ThrownTick(d); return; }
    const long long t = gFrame - d.downAt;
    if (d.down == 2) {
        // (v150-host beta 18) a TEAM SNEAKING defender's stamina comes back 4 times slower (tsne.h TsneWakeTick gives a real
        // defender a quarter of the game's rate): about 83 s from nothing instead of 21
        KoTick(d, false);                                // (v150-host beta 22, mash: the game's rule, the doll's presses)
        if (d.stamina >= 30000) KoWake(d);
    }
    if (d.down == 1 && d.rollPress) {                    // (v150-host beta 22, mash: up at its first press once it lies awake)
        if (RollTick(d)) { d.down = 3; d.upAt = gFrame + kKoRiseFor; d.riseMs = KoClock(d) + (double)kKoRiseFor * 1000.0 / 59.94; }   // (mash3: on its clock)
    } else if (d.down == 1 && t >= 180) { d.down = 3; d.upAt = d.downAt + 252; }
    StateTick(d, 0);
    Unreliable(d, PlayerChan(d, 2), {0x12, 0xff});
    Unreliable(d, PlayerChan(d, 2), {0x13, 0x00, 0x00, 0x00});
    if (d.held && !d.down) {                         // (held with no throw 5 s on: its grabber gone, free again)
        if (t > 300) { d.held = false; VLOG("vdoll %d: free again (held 5 s with no throw)", d.k + 1); body::Resume(d.k); }
        return;
    }
    if (d.down == 3 && RiseDone(d)) {
        Unreliable(d, PlayerChan(d, 1), {0x0A, (uint8_t)d.slot, 0x00});
        VLOG("vdoll %d: up again after %.1f s", d.k + 1, (gFrame - d.downAt) / 59.94);
        d.down = 0; d.fell = false; d.held = false; d.koRise = false; d.riseMs = -1; d.rollPress = false; d.asleep = false;
        body::Resume(d.k);
        return;
    }
    if (!d.fell && (t >= 56 || d.down == 3)) {
        Unreliable(d, PlayerChan(d, 1), {0x10, 0x00, 0x00, 0x00, 0x00});
        Unreliable(d, PlayerChan(d, 1), {0x0A, (uint8_t)d.slot, 0x01, 0x00, 0x00});
        d.fell = true;
        return;
    }
    // ((v150-host beta 22, mash) lying awake after a roll told 1 as a client's, not 4: lying knocked out)
    if (d.fell) Unreliable(d, PlayerChan(d, 1), {0x0A, (uint8_t)d.slot, (uint8_t)(d.down == 3 ? 5 : d.rollPress && d.down == 1 ? 1 : 4), 0x00, 0x00});
}

void InMatch(Doll& d) {
    const int rule = RuleNow();
    switch (d.st) {
    case NONE: break;
    case WAIT:
        if (gFrame >= d.nextTick) { d.nextTick = gFrame + 6; StateTick(d, 1); }
        // ((v150-host beta 25, respawnwait) a respawn: state 1 for a player's weapons page, on the player's clock of its death)
        if (d.deployMs >= 0) { if (DeadClock(d) < d.deployMs - 0.5) break; d.deployMs = -1; }
        // (v150-host beta 16, the configurations' audit) before the round's start (the room's phase 1) it stays spawned, as
        // a player's game does until the round starts (the host's P2P log of two full dolls, real2: their state 1 from their
        // spawns, both 0 at the same frame, the round's start); it deployed 1 to 2.5 s after its spawn and walked while the
        // real players were held. 10 s at most (a phase that never moves on: deployed as before)
        if (gFrame >= d.deployAt && gHost.phase && gHost.phase() == 1 && gFrame - d.deployAt < 600) break;
        if (gFrame >= d.deployAt) {
            d.st = ALIVE; d.alive = true; d.aliveAt = gFrame; d.lifeOk = false; d.killer = -1; d.lastHead = false; d.testStam = false;   // (v150-host beta 25, review25 R5: MGO1_VDOLL_TEST_STAMINA once a life as documented)
            d.stamina = 30000; d.down = 0; d.held = false; d.fell = false; d.lowered = false; d.thrown = false; d.flinchUntil = -1; d.koRise = false; d.riseMs = -1; d.rollPress = false; d.asleep = false;
            d.flinchAt = -1; d.flinchEndMs = -1; d.held13[1] = d.held13[2] = d.held13[3] = 0;   // ((v150-host beta 25, fallflinch) no flinch of an earlier life
            MoveClockStart(d);                           // (v150-host beta 24, stamina2: a life's counts, as the engine's at a player's spawn)
            ClassNow(d);
            StateTick(d, 0);
            if (!body::On()) AliveRecords(d);
            d.lastFull = gFrame; d.nextTick = gFrame + 6;
            if (d.freeMs >= 0)                           // ((v150-host beta 25, respawnwait) then held to a player's deploy)
                VLOG("vdoll %d: deployed at %.0f %.0f %.0f, %lld frames after its spawn, still there to %lld frames after it (a player's deploy)", d.k + 1,
                     d.pos[0], d.pos[1], d.pos[2], gFrame - d.spawnSentAt, kRespawnDeploy);
            else VLOG("vdoll %d: deployed at %.0f %.0f %.0f", d.k + 1, d.pos[0], d.pos[1], d.pos[2]);
        }
        break;
    case ALIVE: {
        const bool tick = gFrame >= d.nextTick;
        if (gTestKo == d.k + 1 && !d.down && !d.held && gFrame - std::max(d.aliveAt, d.downAt + 400) >= 1800) {   // (MGO1_VDOLL_TEST_KO: benches)
            d.stamina = 0; d.down = 2; d.downAt = gFrame; ++d.knocks; d.slowWake = false; d.thrown = false; d.asleep = false;
            KoStart(d);                                  // (its presses and the game's recovery as after a real stun: mash)
            VLOG("vdoll %d: knocked out by the test (MGO1_VDOLL_TEST_KO)", d.k + 1);
        }
        body::Knocked(d.k, d.slot, d.down == 2 && d.fell, d.stamina, d.slowWake ? 36 : 144);   // (v150-host beta 22, wake: a teammate may wake it)
        // ((v150-host beta 25, fallflinch) its flinch over at the kind's ticks of a player's clock: on from its next frame)
        if (gHitFall && d.flinchEndMs >= 0 && gFrame < d.flinchUntil && !d.down && !d.held && FlinchClock(d) >= d.flinchEndMs - 0.5) d.flinchUntil = gFrame;
        if (d.flinchAt >= 0 && gFrame >= d.flinchUntil) {
            if (gDebug && !d.down && !d.held) VLOG("vdoll %d: its flinch over, %lld frames after the reaction %d (%s)", d.k + 1, gFrame - d.flinchAt, d.flinchKind,
                                                   !gHitFall ? "40 frames, MGO1_VDOLL_HIT_FALL=0" : FlinchTicks(d.flinchKind) > 0 ? "a player's ticks of that kind by his clock"
                                                   : "40 frames: that kind not measured on a real client");
            if (gHitFall && d.flinchEndMs >= 0 && !d.down && !d.held) ClockSay(d, "its flinch over", d.flClk, d.flStartF, d.flStartMs, d.flStepMs);   // ((clockfix, debug))
            d.flinchAt = -1; d.flinchEndMs = -1;
        }
        const bool flinch = gFrame < d.flinchUntil && !d.down && !d.held;   // (beta 22, blast)
        // ((v150-host beta 25, respawnwait) after a respawn's weapons page, held at its spawn point to a player's deploy as his game
        // holds him (the frozen bit): still, its records, its body paused, then on)
        bool frozen = false;
        if (d.freeMs >= 0) {
            if (DeadClock(d) < d.freeMs - 0.5) frozen = true;
            else { d.freeMs = -1; body::Resume(d.k); if (gDebug) VLOG("vdoll %d: free, %lld frames after its spawn (a player's deploy)", d.k + 1, gFrame - d.spawnSentAt); }
        }
        // ((v150-host beta 24, stamina2) MGO1_VDOLL_TEST_STOP: still where it is once alive that long; its moves' cost each frame)
        const bool stopped = frozen || (gTestStop > 0 && gFrame - d.aliveAt >= gTestStop * 60);
        MoveCostTick(d, !flinch && !d.down && !d.held && !stopped);
        if (d.flinchUntil >= 0 && !flinch && !d.down && !d.held) { d.flinchUntil = -1; body::Resume(d.k); }
        if ((flinch || d.down || d.held) && body::On()) { body::Self s = BodySelf(d, false); body::Owed(s, gFrame); }   // (v150-host beta 22, pickup2: a take completed)
        if (flinch) {
            if (tick) { d.nextTick = gFrame + 6; body::Pause(d.k); FlinchTick(d); }
        } else if (d.down || d.held) {
            // (v150-host beta 16) its weapon down first, as a player's game tells it when he is knocked down: its copy
            // was left aiming, firing or reloading with nothing in its hands
            if (tick && !d.lowered) { d.lowered = true; Unreliable(d, PlayerChan(d, 1), {0x08, 0x00, 0x00}); }
            if (tick) { d.nextTick = gFrame + 6; body::Pause(d.k); DownedTick(d); }
        } else {
            d.lowered = false;
            if (tick) ClassNow(d);
            // ((v150-host beta 24, stamina) up, its stamina stays where the stuns left it, as a player's (gStaminaRule); beta 15 to 23:
            // +144 every 6 frames)
            if (!gStaminaRule && tick && d.stamina < 30000) d.stamina = std::min(30000, d.stamina + 144);
            if (stopped) {                               // ((v150-host beta 24, stamina2, benches) a still doll's records, its body paused)
                if (tick) { d.nextTick = gFrame + 6; body::Pause(d.k); AliveTick(d); }
            } else {
                if (tick) { d.nextTick = gFrame + 6; if (body::On()) StateTick(d, 0); else AliveTick(d); }
                if (body::On()) BodyTick(d, tick);
                // ((v150-host beta 25, food) a food box it took: its gauge gains what a player's game gives him for it, at once (the
                // engine's eat, clamped at the maximum); its next state record carries it, as his own game's does)
                if (const int gain = body::On() ? body::FoodEaten(d.k) : 0) {
                    const int was = d.stamina;
                    d.stamina = food::After(d.stamina, gain);
                    ++d.foodsOwed;
                    if (gDebug) VLOG("vdoll %d: its stamina %d -> %d (a food eaten, +%d)", d.k + 1, was, d.stamina, gain);
                }
            }
            if (gTestStamina > 0 && !d.testStam && gFrame - d.aliveAt >= 600) {   // (MGO1_VDOLL_TEST_STAMINA: benches)
                d.testStam = true;
                if (d.stamina > gTestStamina) { VLOG("vdoll %d: its stamina %d -> %d by the test (MGO1_VDOLL_TEST_STAMINA)", d.k + 1, d.stamina, gTestStamina); d.stamina = gTestStamina; }
            }
        }
        if (d.lifeOk && d.life <= 0) {                   // the host's life at 0: down
            d.st = DEAD; d.alive = false; d.deadAt = gFrame; ++d.deaths; d.snakeLies = false;
            // (v150-host beta 25, snakeghost, review S1) whether he lies dead to the next round is decided now: the room's rule
            // may read the next entry's (a deathmatch) a few frames before the round byte moves on, and a test of the rule
            // in DEAD would then respawn the dead Snake at once
            d.snakeDead = OneLifeOf(d, rule) && NoGhost(d, rule);
            // ((v150-host beta 25, ghostwait) the kind it tells for this death sets its ghost's wait, as a player's death motion
            // sets his; its clock from now)
            d.deadKind = DeathKind(d); d.deadMs = 0; d.deadLastF = -1; d.deadStartF = -1; DeadClock(d);
            // ((v150-host beta 25, respawnwait) a respawn's wait set at the death as a player's game sets it: capture's bigger team 1 s
            // more (0x409700); nothing due of a respawn yet)
            d.respExtra = gRespawnWait ? RespawnExtraFrames(d, rule) : 0;
            d.spawnMs = d.deployMs = d.freeMs = -1;
            // (v150-host beta 17) a team deathmatch's ticket: at his death a player's game tells the room {7, his team} and
            // the host takes one of that team's tickets (measured on playable dolls: sent in team deathmatch only, in the
            // frame of the death). This record went in every rule with the doll's player number in the team's place: the
            // doll in seat 1 took a ticket from the blue team at each of its deaths whatever its team, the others' deaths
            // took none, and a team deathmatch between virtual dolls ended 30 to 30 (a draw: the entry played again).
            if (rule == 1 && (d.team == 0 || d.team == 1)) Reliable(d, RoomChan(d), {0x07, (uint8_t)d.team});
            Unreliable(d, PlayerChan(d, 1), {0x08, 0x00, 0x00});
            d.nextTick = gFrame;
            if (body::On()) { body::Self s = BodySelf(d, false); body::Died(s); }   // (v150-host beta 22, pickup: a special item it holds dropped, as a player's game drops it)
            body::Down(d.k);
            // ((v150-host beta 25, respawnwait) killed lying: no head shot for the game (its kind 4 or 5), the line says the kind)
            VLOG("vdoll %d: dead, by player %d (%d deaths)%s%s", d.k + 1, d.killer + 1, d.deaths, OneLifeOf(d, rule) ? ", out until the round's end (one life)" : "",
                 d.deadKind == 4 ? ", lying (its death's kind 4)" : d.deadKind == 5 ? ", lying after a blast's throw (its death's kind 5)" : d.lastHead ? ", a head shot" : "");
        } else if (!WantPlay() && !d.down && !d.held && (d.team == 0 || d.team == 1) && gFrame - d.aliveAt >= 60 && !SpecBarred(d, rule))
            GoSpectator(d);
        break;
    }
    case DEAD:                                           // (the waits: a respawn RespawnDue, a ghost GhostDue: a player's timer)
        if (gFrame >= d.nextTick) { d.nextTick = gFrame + 6; DownTick(d, DeathKind(d), d.killer); }
        // ((v150-host beta 25, snakeghost) SNEAKING's Snake: no ghost, his death ticks to the next round as a player's Snake)
        if (d.snakeDead) {
            if (gFrame - d.deadAt >= 630 && !d.snakeLies) {
                d.snakeLies = true;
                VLOG("vdoll %d: no ghost: SNEAKING's Snake lies dead to the next round, as a player's Snake (MGO1_VDOLL_SNAKE_LIFE)", d.k + 1);
            }
            break;
        }
        // ((v150-host beta 25, ghostwait) one life: its ghost when a player's game sends his for that kind of death, GhostDue)
        if (OneLifeOf(d, rule)) { if (GhostDue(d)) GhostSpawn(d); break; }
        if (RespawnDue(d, rule)) Respawn(d);             // ((v150-host beta 25, respawnwait) a player's wait for its kind of death)
        break;
    case GHOST:
        if (gFrame >= d.nextTick) { d.nextTick = gFrame + 6; GhostTick(d); }
        break;
    case GOING_SPECTATOR:
        if (gFrame >= d.nextTick) { d.nextTick = gFrame + 6; DownTick(d, 1, d.slot); }
        if (gFrame - d.specAt >= 618) SpectatorDone(d);
        break;
    case SPECTATOR:
        if (gFrame >= d.nextTick) { d.nextTick = gFrame + 6; StateTick(d, 3); }
        break;
    }
}

// the round: claimed, picked (or held, or out of the teams with no real player), spawned by the host, deployed
void RoundTick(Doll& d) {
    if (d.lastKeep < 0 || gFrame - d.lastKeep >= 300) { Reliable(d, 1, {0x00}); d.lastKeep = gFrame; }   // the keep-alive
    // the claim, every 64 frames until the host's 0x83 of this round, 40 times a round at most
    // (v150-host beta 21) a new round's first claim whatever the last round's count: the count was put back to 0 only
    // inside this test, so a doll whose 40 claims of one round had gone unanswered never claimed a round again
    const bool firstClaim = d.claimGen != d.gen;
    if (gFrame - d.joinedAt >= 60 && d.claimAckGen != d.gen && (firstClaim || (gFrame - d.lastClaim >= 64 && d.claims < 40))) {
        Reliable(d, 1, {0x81, (uint8_t)d.gen, (uint8_t)d.slot});
        if (firstClaim) d.claims = 0;
        d.claimGen = d.gen; d.lastClaim = gFrame; ++d.claims;
    }
    const int rule = RuleNow();
    const bool inTeam = d.team == 0 || d.team == 1 || (rule == 0 && d.team >= 0 && d.team <= 2);
    // the pick, once the claim is taken: a player in a team picks it again at each round (a PC's game); one out of a team
    // joins when a real player plays and none is arriving (JoinHold); a spectator by the room's team change
    if (d.claimAckGen == d.gen && d.pickGen != d.gen && gFrame - d.lastClaim >= 30 && d.team != 0xFE) {
        const char* why = inTeam ? nullptr : !WantPlay() ? "no real player in the game" : JoinHold();
        if (!why) {
            // {round, team, character}; deathmatch: the team byte is the unit, 0 for a doll with none yet (the host made it
            // class 0, KGB: the bench A2_gru_ocu_u1, v150-host beta 16) (the short
            // form {0x82, round} of a PC's game with no team: the host ended every virtual doll's player at once in a big game's
            // deathmatch, audit_flow/c1_fifteen)
            const int team = inTeam ? d.team : rule == 0 ? 0 : JoinTeam(d);
            Reliable(d, 1, {0x82, (uint8_t)d.gen, (uint8_t)team, 0x00});
            d.pickGen = d.gen; d.pickAt = gFrame; d.holdGen = -1; d.pickTeam = rule == 0 ? -1 : team;
            if (rule == 0) VLOG("vdoll %d: picks the unit %d (round %d, deathmatch)", d.k + 1, team, d.gen);
            else VLOG("vdoll %d: picks the %s team (round %d)", d.k + 1, team ? "blue" : "red", d.gen);
        } else if (d.holdGen != d.gen || d.holdWhy != why) {
            d.holdGen = d.gen; d.holdWhy = why;
            VLOG("vdoll %d: waits to join (%s, round %d)", d.k + 1, why, d.gen);
        }
    }
    // a spectator back into a team
    if (d.team == 0xFE && d.st == SPECTATOR && WantPlay() && gFrame - d.specAt >= 120) {
        const char* why = JoinHold();
        if (!why) JoinFromSpectator(d, RuleNow() == 0 ? 0 : JoinTeam(d));
        else if (d.holdGen != d.gen || d.holdWhy != why) { d.holdGen = d.gen; d.holdWhy = why; VLOG("vdoll %d: waits to join (%s, a spectator)", d.k + 1, why); }
    }
    if (d.team == 0xFE && d.st == NONE && d.claimAckGen == d.gen) { d.st = SPECTATOR; d.specAt = gFrame; d.nextTick = gFrame; }
    if (d.team == 0xFF && d.st == SPECTATOR) d.st = NONE;   // (a new map: no team, nothing sent but the claim and the keep-alive)
    if (d.assignedGen == d.gen && d.haveSpawn && d.st == NONE) {   // spawned by the host: deployed after a while
        memcpy(d.pos, d.spawn, sizeof d.pos);
        memcpy(d.start, d.spawn, sizeof d.start);
        d.st = WAIT;
        d.deployAt = gFrame + 60 + (long long)(d.nonce % 90);
        d.deployMs = d.freeMs = -1;                      // ((v150-host beta 25, respawnwait) the round's spawn, not a respawn)
        d.nextTick = gFrame;
        VLOG("vdoll %d: in the match, player %d in the %s team, at %.0f %.0f %.0f", d.k + 1, d.slot + 1, d.team ? "blue" : "red",
             d.pos[0], d.pos[1], d.pos[2]);
    }
    InMatch(d);
    // (v150-host beta 22, support) its claymores lying, alive or dead: one the host's copy says went off wounds as the layer's own
    // game's blast does (a dead player's claymore wounds too; one whose layer left the game wounds nobody)
    if (body::On() && d.slot >= 0) { body::Self s = BodySelf(d, false); body::Laid(s); }
}

// a new session from the start (its game's end, 10 s without a datagram of the host, its leave): out of the game until then
void Reset(Doll& d, long long at) {
    Doll n;
    n.k = d.k; n.id = d.id; n.name = d.name; n.wantTeam = d.wantTeam; n.ip = d.ip; n.port = d.port;
    n.nonce = d.nonce * 1103515245u + 12345u + (uint32_t)gFrame;
    n.startAt = at; n.rejoinAfter = at;
    n.deaths = d.deaths; n.respawns = d.respawns; n.leaves = d.leaves;
    d = n;
}
void Restart(Doll& d, const char* why) {
    body::Down(d.k);
    if (d.phase != OUT) VLOG("vdoll %d: joins again (%s)", d.k + 1, why);
    Reset(d, gFrame + 60 + (long long)d.k * gGap);
}
// the leave, as a PC's game leaves ({0x01} on channel 1, reliable), then out of the game: back when a seat is free again,
// wait frames later at the soonest
void Leave(Doll& d, const char* why, long long wait) {
    if (d.phase == OUT || d.phase == LEAVING) return;
    // (v150-host beta 22, pickup2: review22b P6, P7) alive in the game: its take completed, and on its own leave (not the host's
    // kick) the special items it holds dropped, as a PC's game drops them at its pause menu's QUIT; those records in a datagram
    // before its leave's (the game sends its quit 6 frames after them; the host removes a leaving player 10 frames after its leave)
    if (body::On() && d.alive && d.phase == JOINED && d.slot >= 0) {
        body::Self s = BodySelf(d, false);
        if (body::Quit(s, !d.kicked)) Flush(d, true);
    }
    body::Down(d.k);
    ++d.leaves;
    d.leaveWait = wait;
    if (!d.keyed || d.phase < JOINED) { VLOG("vdoll %d: stops its join (%s)", d.k + 1, why); Reset(d, gFrame + wait); return; }
    d.rel.clear();
    d.unrel.clear();
    Reliable(d, 1, {0x01});
    d.phase = LEAVING; d.leaveAt = gFrame;
    VLOG("vdoll %d: leaves the host's game (%s)", d.k + 1, why);
    Flush(d, true);
}

int Seats() { return gHost.seats ? gHost.seats() : 0; }
int Pending() {                                      // dolls on their way in, not in the session's table yet
    int n = 0;
    for (const Doll& d : gDolls) if ((d.phase == CONNECT || d.phase == JOINING) && !InSession(d.id)) ++n;
    return n;
}
// a seat for one more doll: one at least always left for a real player
bool SeatFree() {
    const int seats = Seats();
    return seats > 0 && gP.known && gP.joiners + Pending() + 1 <= seats - gKeepSeat;
}

void DollTick(Doll& d) {
    if (d.k >= gWant) {                                // (v150-host beta 15) fewer wanted: the last ones leave
        if (d.phase == OUT) return;
        if (d.phase != LEAVING) { Leave(d, "one virtual doll less wanted", 0); return; }
    }
    if (d.phase == OUT) {
        if (gFrame < d.startAt || gFrame < d.rejoinAfter || InSession(d.id) || !SeatFree()) return;
        d.phase = CONNECT; d.lastConnect = -1000;
    }
    if (d.phase == LEAVING) {
        // (v150-host beta 22, relay) its leave sent: nothing more but the leave again if the host's game does not acknowledge it,
        // as a PC's game that quit (an acknowledgment of what the host sent meanwhile came in a datagram after the host had
        // removed its session, once its leave waited a read: the flow test's seats)
        if (gRelay) d.acks.clear();
        Flush(d, false);
        const bool gone = !InSession(d.id);                // (the host removes the player at once, its session with it)
        if (gone || gFrame - d.leaveAt >= 180) {
            VLOG("vdoll %d: out of the host's game (%s), back in %lld s at the soonest", d.k + 1, gone ? "gone from its table" : "still in its table after 3 s",
                 d.leaveWait / 60);
            Reset(d, gFrame + d.leaveWait);
        }
        return;
    }
    if (d.keyed && d.lastIn >= 0 && gFrame - d.lastIn > 600) { Restart(d, "its session lost: 10 s without a datagram of the host"); return; }
    if (!d.keyed && d.dgramsOut > 600) { Restart(d, "its join failed: no answer to its connects"); return; }
    if (!d.keyed) {                                // the connect, every 3 frames (as NewHostJoin sends it)
        if (gFrame - d.lastConnect < 3) return;
        if (gRelay && !d.out.empty()) return;      // (v150-host beta 22, relay) its last one not read yet: no connects queued behind the key

        d.lastConnect = gFrame;
        std::vector<uint8_t> m, body;
        Put32(m, d.nonce); m.push_back(0x03); Put32(m, d.id);
        PutRec(body, 0, 0, 0, m.data(), m.size());
        Push(d, body, true);
        if (d.dgramsOut == 1) VLOG("vdoll %d: connecting to the host's game (user id %#x)", d.k + 1, d.id);
        return;
    }
    if (!d.keyAckSent) {                           // the host's number acknowledged, the hello: in the bootstrap set still
        d.keyAckSent = true;
        d.phase = JOINING;
        d.acks.clear();
        d.nextSeq[0] = 1;                          // (the connect was channel 0's first)
        std::vector<uint8_t> body;
        PutRec(body, 0, 0x40, 0, nullptr, 0);
        std::vector<uint8_t> hello = {0x02};
        hello.resize(13, 0);
        hello.insert(hello.end(), d.name.begin(), d.name.end());
        OutRel r; r.chan = 1; r.seq = d.nextSeq[1]++; r.data = hello; r.sent = gFrame; r.tries = 1;
        d.rel.push_back(r);
        PutRec(body, 1, 0, r.seq, hello.data(), hello.size());
        Push(d, body, true);
        return;
    }
    if (d.kicked) { Leave(d, "kicked by the host", 18000); return; }   // (as a PC's game: out at once; back 5 minutes later)
    if (d.hostEnded) { Restart(d, "the host ended its game"); return; }
    const int gen = gHost.gen ? gHost.gen() : 0;
    if (gen != d.gen) {                            // a new round: claimed, picked and deployed again
        if (d.gen >= 0) VLOG("vdoll %d: a new round, its byte %d -> %d (the next round, or the round played again)", d.k + 1, d.gen, gen);
        d.gen = gen;
        // (v150-host beta 17) this round's channels start again. The game makes the room's and the players' streams anew at
        // each round and counts their reliable records from 0 (the host's own start at 0 each round), under numbers that
        // come back every other round. This doll went on counting: in a game's third round its first record to the room was
        // number 3 while the host's game waited for number 0, so nothing it told the room from the third round on arrived
        // (its deaths: a team deathmatch's tickets; its way to and from the spectators), and the host's first reliable
        // records of that round had the first round's numbers and were dropped as seen (its kick).
        for (auto it = d.nextSeq.begin(); it != d.nextSeq.end();) { if (RoundChan(it->first, gen)) it = d.nextSeq.erase(it); else ++it; }
        for (auto it = d.seen.begin(); it != d.seen.end();) { if (RoundChan(it->first, gen)) it = d.seen.erase(it); else ++it; }
        d.rel.erase(std::remove_if(d.rel.begin(), d.rel.end(), [&](const OutRel& o) { return RoundChan(o.chan, gen); }), d.rel.end());
        d.alive = false; d.haveSpawn = false; d.objKind = d.objLife = -1; d.specTries = 0;
        // (v150-host beta 16) nothing of the last round's player kept: knocked down or held when it ended, its state ticks
        // said so for the next round's player until it deployed; its class read again
        d.down = 0; d.held = false; d.fell = false; d.stamina = 30000; d.lowered = false; d.killer = -1; d.lifeOk = false; d.koRise = false; d.riseMs = -1; d.rollPress = false; d.asleep = false;
        d.thrown = false; d.flinchUntil = -1;            // (v150-host beta 22, blast)
        d.deployMs = d.freeMs = -1;                      // ((v150-host beta 25, respawnwait) no respawn's deploy carried over)
        d.flinchAt = -1; d.flinchEndMs = -1;             // ((v150-host beta 25, fallflinch))
        d.cls = d.clsSaid = -1; d.clsHeld = d.clsGuessSaid = false; d.heldSince = -1; d.unitChecked = false;
        if (d.st != SPECTATOR) d.st = NONE;
        body::Down(d.k);
    }
    if (d.phase >= JOINED && d.slot >= 0) RoundTick(d);
    Flush(d, false);
}

// the seats: a doll in the game leaves when a real player took the last free seat (the newest doll first), one at a time.
// The full dolls go first (mgo1doll-manager keeps the virtual dolls' seats for them and orders a full doll out): with full
// dolls enough in the game, a virtual doll waits 45 s for it (the order, the host's time to drop a game gone), then leaves
void SeatsTick() {
    static long long lastLeave = -1000, last = -1000, overSince = -1;
    if (gFrame - last < 30 || !gP.known || gFrame - lastLeave < 120) return;
    last = gFrame;
    const int seats = Seats();
    int going = 0;                                   // (its leave sent, the host's table not updated yet)
    for (const Doll& d : gDolls) if ((d.phase == LEAVING || d.phase == OUT) && InSession(d.id)) ++going;
    const int over = gP.joiners - going - (seats - gKeepSeat);
    if (seats <= 0 || over <= 0) { overSince = -1; return; }
    int full = 0;                                    // the full dolls in the game
    for (int i = 0; i < gP.n; ++i) if (gP.seen[i].kind == 2 && !IsId(gP.seen[i].uid)) ++full;
    if (overSince < 0) {
        overSince = gFrame;
        if (full >= over) VLOG("vdoll: the game full (%d joiners of %d seats, one kept): a full doll's seat first, 45 s at most", gP.joiners - going, seats);
    }
    if (full >= over && gFrame - overSince < 2700) return;
    for (auto it = gDolls.rbegin(); it != gDolls.rend(); ++it) {
        if (it->phase == OUT || it->phase == LEAVING) continue;
        char why[96];
        snprintf(why, sizeof why, "a seat for a real player: %d joiners of %d seats", gP.joiners - going, seats);
        Leave(*it, why, 1200);
        lastLeave = gFrame;
        return;
    }
}

}  // namespace

std::vector<uint8_t> Seal(uint16_t seq, const std::vector<uint8_t>& body, uint32_t payloadKey, uint32_t macKey) {
    std::vector<uint8_t> b;
    b.reserve(body.size() + 12);
    b.push_back((uint8_t)seq);
    b.push_back((uint8_t)(seq >> 8));                 // (v150-host beta 21: the 16 bits; the top one is no flag of a doll's, it never compresses)
    b.insert(b.end(), body.begin(), body.end());
    b.resize(b.size() + 10, 0);
    Cipher(b, payloadKey, false);
    Sign(macKey, b.data(), b.size() - 10, b.data() + b.size() - 10);
    Mix(b, false);
    return b;
}

bool Open(const std::vector<uint8_t>& dgram, uint32_t payloadKey, uint32_t macKey, uint16_t* seq, std::vector<uint8_t>* body) {
    if (dgram.size() < 12) return false;
    std::vector<uint8_t> b = dgram;
    Mix(b, true);
    uint8_t sig[10];
    Sign(macKey, b.data(), b.size() - 10, sig);
    if (memcmp(sig, b.data() + b.size() - 10, 10)) return false;
    Cipher(b, payloadKey, true);
    if (seq) *seq = (uint16_t)(b[0] | b[1] << 8);
    if (body) body->assign(b.begin() + 2, b.end() - 10);
    return true;
}

namespace {
// ---- (v150-host beta 19) the live level (MGO1_VDOLL_ADAPT=1, docs/VIRTUAL_DOLL.md 19; vdoll_adapt.h): each virtual doll's
// level moves from its level set (the anchor) by its enemies' and its own side's strength and the match's stance, every 5 s,
// within MGO1_VDOLL_ADAPT_MIN .. MGO1_VDOLL_ADAPT_MAX (1 and 10); the lines of MGO1_VDOLL_LIVE's file `adapt 0|1`,
// `adapt_min <n>`, `adapt_max <n>` taken at its next read (2 s), with no restart. Off: nothing here runs, the body plays its
// level set as before ----
struct AdaptCfg { bool on = false; int lo = 1, hi = 10; bool operator!=(const AdaptCfg& o) const { return on != o.on || lo != o.lo || hi != o.hi; } };
AdaptCfg gAdEnv, gAdNow;                            // the environment's, the one in force
bool gAdSaid = false;
adapt::Ring gRings[16];
std::vector<adapt::State> gAdStates;
std::vector<float> gLvNow;                          // each doll's level now (with its offset), -1 none
std::vector<float> gLvSaid;                         // as the last "levels now" line said them
long long gAdAt = -100000, gLvSaidAt = -100000;
AdaptCfg EnvAdapt() {
    AdaptCfg c;
    const char* e = getenv("MGO1_VDOLL_ADAPT");
    c.on = e && !strcmp(e, "1");
    const int a = getenv("MGO1_VDOLL_ADAPT_MIN") ? atoi(getenv("MGO1_VDOLL_ADAPT_MIN")) : 1;
    const int b = getenv("MGO1_VDOLL_ADAPT_MAX") ? atoi(getenv("MGO1_VDOLL_ADAPT_MAX")) : 10;
    c.lo = adapt::Lo(a ? a : 1, b ? b : 10); c.hi = adapt::Hi(a ? a : 1, b ? b : 10);   // (the wrong way round: swapped)
    return c;
}
std::string LevelsSet(bool inGame) {
    std::string o;
    for (const Doll& d : gDolls) {
        if (inGame ? d.phase != JOINED : d.k >= std::max(1, gWant)) continue;
        o += (o.empty() ? "" : ",") + std::to_string(body::LevelSet(d.k));
    }
    return o.empty() ? "none" : o;
}
// the settings in force: said at the start and at each change; off, the body back to its levels set
void AdaptApply(const AdaptCfg& c) {
    if (gAdSaid && !(c != gAdNow)) return;
    const bool was = gAdNow.on;
    gAdNow = c;
    if (c.on) VLOG("vdoll: level: live, between %d and %d (the levels set %s)", c.lo, c.hi, LevelsSet(false).c_str());
    else if (gAdSaid) VLOG("vdoll: level: fixed, the live level off (the levels set %s)", LevelsSet(false).c_str());
    gAdSaid = true;
    if (!c.on) {
        for (Doll& d : gDolls) body::Live(d.k, false, 0.f, 1, 10, nullptr);
        gLvNow.assign(gDolls.size(), -1.f);
        return;
    }
    if (!was) { gAdStates.assign(gDolls.size(), adapt::State()); gLvSaid.assign(gDolls.size(), -1.f); gLvSaidAt = -100000; }
    gAdAt = -100000;                                // (judged at once: the new bounds in force)
}
// every 5 s: each player's counters sampled; each doll in the game judged; its level now to the body
void AdaptTick() {
    if (!gAdNow.on || gFrame - gAdAt < 300) return;
    gAdAt = gFrame;
    const int lo = gAdNow.lo, hi = gAdNow.hi;
    if (gAdStates.size() != gDolls.size()) gAdStates.resize(gDolls.size());
    if (gLvNow.size() != gDolls.size()) gLvNow.assign(gDolls.size(), -1.f);
    if (gLvSaid.size() != gDolls.size()) gLvSaid.assign(gDolls.size(), -1.f);
    adapt::Match m;
    m.rule = RuleNow();
    m.playing = gHost.phase && gHost.phase() == 2;
    if (gHost.room) gHost.room(m.wins, m.tickets);
    const int sec = (int)(gFrame / 60);
    bool present[16] = {};
    for (int i = 0; i < gP.n; ++i) {
        const Seen& e = gP.seen[i];
        if (e.id < 0 || e.id >= 16 || e.kind == 1 || e.kind == 3) continue;
        int k = 0, dd = 0, r = -1;
        if (!gHost.score || !gHost.score(e.id, &k, &dd, &r) || e.team == 0xFE) continue;
        present[e.id] = true;
        adapt::Sample(gRings[e.id], e.uid, sec, k, dd);
        adapt::Player p;
        p.id = e.id; p.real = e.kind == 0; p.team = e.team; p.kills = k; p.deaths = dd; p.rating = p.real ? r : -1;
        p.known = adapt::Strength(gRings[e.id], p.rating, &p.skill, &p.conf);
        m.players.push_back(p);
    }
    for (int id = 0; id < 16; ++id) if (!present[id]) gRings[id] = adapt::Ring();   // (gone, or a spectator: afresh)
    if (gHost.objectives && m.rule >= 2 && m.rule <= 5) {
        ObjNow o[4];
        float goals[2][3] = {};
        const int n = gHost.objectives(o, 4, goals, m.goalSet);
        const float* goal = m.goalSet[0] ? goals[0] : goals[1];
        for (int i = 0; i < n; ++i) {
            adapt::Obj a;
            a.holderId = o[i].holder; a.goalTeam = o[i].goalTeam;
            for (const adapt::Player& p : m.players) if (p.id == o[i].holder) a.holderTeam = p.team;
            a.holderSnake = o[i].holder >= 0 && gHost.playerClass && gHost.playerClass(o[i].holder) == 8;
            a.nearGoal = std::hypot(o[i].pos[0] - goal[0], o[i].pos[2] - goal[2]) < 3000.f;
            m.objs.push_back(a);
        }
    }
    bool moved = false;
    for (Doll& d : gDolls) {
        adapt::State& st = gAdStates[d.k];
        const int base = body::LevelSet(d.k);
        const bool inGame = d.phase == JOINED && d.slot >= 0 && d.slot < 16;
        float old = 0.f;
        bool say = false;
        if (inGame && d.team >= 0 && d.team != 0xFE && d.team != 0xFF) say = adapt::Step(st, base, lo, hi, d.slot, d.team, m, &old);
        else adapt::Anchor(st, base, lo, hi);
        if (say)
            VLOG("VDOLL ADAPT: vdoll %d old=%.2f new=%.2f target=%.2f base=%d enemy_strength=%.1f ally_strength=%.1f balance=%+.2f confidence=%.2f reason=%s "
                 "(enemies %d real, allies %d real; %s)", d.k + 1, old, st.level, st.target, base, st.enemy, st.ally, st.balance, st.conf, st.reason.c_str(),
                 st.eReal, st.aReal, st.stance.c_str());
        const float now = adapt::Effective(st.level, d.k + 1, lo, hi);
        body::Live(d.k, true, now, lo, hi, st.foe);
        gLvNow[d.k] = inGame ? now : -1.f;
        if (inGame && std::fabs(now - gLvSaid[d.k]) >= 0.05f) moved = true;
    }
    if (moved || gFrame - gLvSaidAt >= 3600) {
        std::string a;
        char b[16];
        for (const Doll& d : gDolls) if (gLvNow[d.k] >= 0.f) { snprintf(b, sizeof b, "%s%.2f", a.empty() ? "" : " ", gLvNow[d.k]); a += b; }
        if (!a.empty()) {
            VLOG("vdoll: levels now %s (set %s)", a.c_str(), LevelsSet(true).c_str());
            gLvSaid = gLvNow; gLvSaidAt = gFrame;
        }
    }
}
}  // namespace

uint32_t FirstId() { return Cfg().first; }
int Count() { return Cfg().count; }
int Wanted() { return gWant; }
bool IsId(uint32_t uid) { return Cfg().count > 0 && uid >= Cfg().first && uid < Cfg().first + (uint32_t)Cfg().count; }
int NumberOfId(uint32_t uid) { return IsId(uid) ? (int)(uid - Cfg().first) + 1 : 0; }

void Init(const Host& h) {
    gHost = h;
    const int count = Cfg().count;
    gWant = Cfg().want;
    gDebug = getenv("MGO1_VDOLL_DEBUG") != nullptr;
    if (!count) return;
    gAlone = getenv("MGO1_VDOLL_PLAY_ALONE") && atoi(getenv("MGO1_VDOLL_PLAY_ALONE")) > 0;
    if (const char* s = getenv("MGO1_VDOLL_KEEP_SEAT")) gKeepSeat = std::max(0, std::min(4, atoi(s)));
    {                                                   // (v150-host beta 22, blast; 0: as beta 21)
        const char* all = getenv("MGO1_VDOLL_BLASTS");
        gReact = !(all && !strcmp(all, "0")) && !(getenv("MGO1_VDOLL_REACT") && !strcmp(getenv("MGO1_VDOLL_REACT"), "0"));
        if (!gReact) VLOG("vdoll: a blast's fall told as a roll's and no flinch, the earlier way (MGO1_VDOLL_REACT=0 or MGO1_VDOLL_BLASTS=0)");
    }
    {                                                   // (v150-host beta 22, mash; 0: as beta 22)
        const char* pc = getenv("MGO1_VDOLL_PACE");     // (v150-host beta 23, mash3: the body's pace's switch, its clock)
        gKoPace = !(pc && *pc == '0');
        const char* m = getenv("MGO1_VDOLL_MASH");
        gMash = !(m && !strcmp(m, "0"));
        const char* r = getenv("MGO1_VDOLL_MASH_RATE");
        gMashRate = r && *r && strcmp(r, "auto") ? std::max(0.0, std::min(vmash::kRateMax, atof(r))) : -1.0;
        if (!gMash) VLOG("vdoll: a knocked-out doll's stamina back at 144 every 6 frames, the earlier way (MGO1_VDOLL_MASH=0)");
        else if (gMashRate >= 0) VLOG("vdoll: a knocked-out doll presses as a player, %.1f times a second (MGO1_VDOLL_MASH_RATE), its stamina back by the game's rule", gMashRate);
        else VLOG("vdoll: a knocked-out doll presses as a player, 4 to 9.5 times a second by its level, its stamina back by the game's rule (MGO1_VDOLL_MASH)");
    }
    if (const char* s = getenv("MGO1_VDOLL_RELAY")) if (!strcmp(s, "0")) {   // (v150-host beta 22, relay) beta 22's way
        gRelay = false;
        VLOG("vdoll: a doll's datagrams handed to the host's game as they come, two in one read too (MGO1_VDOLL_RELAY=0, the earlier way)");
    }
    if (const char* s = getenv("MGO1_VDOLL_SNE_RESPAWN")) if (*s == '0') {   // (v150-host beta 21) beta 20's way
        gSneRespawn = false;
        VLOG("vdoll: SNEAKING's soldiers a ghost to the round's end at their first death (MGO1_VDOLL_SNE_RESPAWN=0, the earlier way)");
    }
    {   // (v150-host beta 25, snakeghost) SNEAKING's Snake never a ghost; 0: beta 24's way
        const char* s = getenv("MGO1_VDOLL_SNAKE_LIFE");
        gSnakeLife = !(s && !strcmp(s, "0"));
        VLOG(gSnakeLife ? "vdoll: SNEAKING's Snake dead to the next round once killed, never a ghost, as a player's Snake: on (MGO1_VDOLL_SNAKE_LIFE)"
                        : "vdoll: SNEAKING's Snake a ghost 10.5 s after his death, the earlier way (MGO1_VDOLL_SNAKE_LIFE=0)");
    }
    {   // (v150-host beta 25, ghostwait) a one-life doll's ghost when a player's comes (GhostDue)
        const char* s = getenv("MGO1_VDOLL_GHOST_WAIT");
        gGhostWait = !(s && !strcmp(s, "0"));
        // ((v150-host beta 25, respawnwait) 7.1 s after kinds 4 and 5, killed lying: GhostWaitFrames)
        VLOG(gGhostWait ? "vdoll: a one-life doll's ghost as a player's: 6.6 s after a death of kind 2 (in the head), 7.1 s after one of kind 4 or 5 (lying), 9.5 s after one of kind 1, on a player's clock (MGO1_VDOLL_GHOST_WAIT)"
                        : "vdoll: a one-life doll's ghost 630 frames after its death whatever the death, as b16 (MGO1_VDOLL_GHOST_WAIT=0)");
    }
    {   // (v150-host beta 25, respawnwait) a doll's respawn when a player's comes, in play at a player's deploy (RespawnDue)
        const char* s = getenv("MGO1_VDOLL_RESPAWN_WAIT");
        gRespawnWait = !(s && !strcmp(s, "0"));
        if (gRespawnWait)
            VLOG("vdoll: a doll's respawn as a player's: its spawn 6.6 s after a death of kind 2 (in the head), 9.5 s after one of kind 1, 7.1 s after one "
                 "of kind 4 or 5 (lying), 1 s more in capture for the bigger team, on a player's clock; in play %lld frames after its spawn (MGO1_VDOLL_RESPAWN_WAIT)", kRespawnDeploy);
        else VLOG("vdoll: a doll's respawn 300 frames after its death (390 in capture and base capture, 600 a SNEAKING soldier), in play 18 frames later, as b16 (MGO1_VDOLL_RESPAWN_WAIT=0)");
        s = getenv("MGO1_VDOLL_DEATH_KIND");
        gDeathKindRule = !(s && !strcmp(s, "0"));
        VLOG(gDeathKindRule ? "vdoll: the kind of death a doll tells as a player's game: 4 killed lying, 5 lying after a blast's throw, else 2 after a hit in the head, 1 (MGO1_VDOLL_DEATH_KIND)"
                            : "vdoll: the kind of death a doll tells: 2 after a hit in the head, else 1, as b16 (MGO1_VDOLL_DEATH_KIND=0)");
    }
    {   // (v150-host beta 25, fallflinch) a flinch and a drop's fall as a player's; 0: b16's (the body reads the same switch for the fall)
        const char* s = getenv("MGO1_VDOLL_HIT_FALL");
        gHitFall = !(s && !strcmp(s, "0"));
        VLOG(gHitFall ? "vdoll: a flinch as long as a player's own game holds him (%d ticks of his clock), a drop's fall and landing at a player's pace: on (MGO1_VDOLL_HIT_FALL)"
                      : "vdoll: a flinch of 40 frames whatever its kind, a drop's fall at 16 frames a metre, as b16 (MGO1_VDOLL_HIT_FALL=0)", kFlinchTicks[1]);
        s = getenv("MGO1_VDOLL_REACT_KIND");               // ((v150-host beta 25, reactfix) the kind 5's 30 ticks; 0: b16's 40 frames)
        gReactKindV = !(s && !strcmp(s, "0"));
    }
    {   // (v150-host beta 25, clockfix) a player's clocks from their start (KoClock, MoveCostTick, FlinchClock)
        const char* s = getenv("MGO1_VDOLL_CLOCK_FIX");
        gClockFix = !(s && !strcmp(s, "0"));
        VLOG(gClockFix ? "vdoll: a player's clocks (a knock-out's, the moves' cost, a flinch) the machine's ms since their start, 3 times the frames' at most (MGO1_VDOLL_CLOCK_FIX)"
                       : "vdoll: a player's clocks (a knock-out's, the moves' cost, a flinch) summed step by step, as b16 (MGO1_VDOLL_CLOCK_FIX=0)");
        gClockTrace = getenv("MGO1_VDOLL_CLOCK_TRACE") && strcmp(getenv("MGO1_VDOLL_CLOCK_TRACE"), "0");
    }
    if (const char* s = getenv("MGO1_VDOLL_TEST_SEQ_STEP")) {   // (v150-host beta 21, tests: the datagrams' numbers)
        gSeqStep = std::max(1, std::min(200, atoi(s)));
        if (gSeqStep > 1) VLOG("vdoll: TEST: each datagram of a doll in the game numbered %d further than the one before (MGO1_VDOLL_TEST_SEQ_STEP)", gSeqStep);
    }
    if (const char* s = getenv("MGO1_VDOLL_WAKE")) if (!strcmp(s, "0")) gWokenRise = false;   // (v150-host beta 23, wake: KoWake's 66 frames)
    if (const char* s = getenv("MGO1_VDOLL_CQC")) if (!strcmp(s, "0")) {   // (v150-host beta 24, cqc) beta 23's way
        gCqc = false;
        VLOG("vdoll: a CQC throw from a hold spends a doll's stamina with no fall, the earlier way (MGO1_VDOLL_CQC=0)");
    }
    if (const char* s = getenv("MGO1_VDOLL_STAMINA")) if (!strcmp(s, "0")) gStaminaRule = false;   // (v150-host beta 24, stamina)
    if (const char* s = getenv("MGO1_VDOLL_MOVE_COST")) if (!strcmp(s, "0")) gMoveCost = false;    // (v150-host beta 24, stamina2)
    if (const char* s = getenv("MGO1_VDOLL_TEST_STOP")) gTestStop = std::max(0, atoi(s));
    if (const char* s = getenv("MGO1_VDOLL_TEST_STAMINA")) gTestStamina = std::max(0, std::min(30000, atoi(s)));   // (v150-host beta 25, food: benches)
    if (gTestStamina) VLOG("vdoll: TEST: a doll's stamina set to %d once a life, up 10 s (MGO1_VDOLL_TEST_STAMINA)", gTestStamina);
    if (gTestStop) VLOG("vdoll: TEST: a doll stands still once it has been alive %lld s (MGO1_VDOLL_TEST_STOP)", gTestStop);
    VLOG(gStaminaRule ? "vdoll: a doll's stamina as a player's: none of it back while it is up (MGO1_VDOLL_STAMINA)"
                      : "vdoll: a doll's stamina back while it is up, 144 every 6 frames, the earlier way (MGO1_VDOLL_STAMINA=0)");
    if (gStaminaRule) VLOG(gMoveCost ? "vdoll: its moves cost a doll's stamina as a player's: running 14, walking 7, crawling 14 a 31 ticks, times its load / 8000 + 0.5 (MGO1_VDOLL_MOVE_COST)"
                                     : "vdoll: its moves cost a doll's stamina nothing, the earlier way (MGO1_VDOLL_MOVE_COST=0)");
    if (const char* s = getenv("MGO1_VDOLL_TEST_KO")) {      // (v150-host beta 22, wake: benches)
        gTestKo = std::max(0, std::min(15, atoi(s)));
        if (gTestKo) VLOG("vdoll: TEST: vdoll %d knocked out each time it has been up 30 s (MGO1_VDOLL_TEST_KO)", gTestKo);
    }
    if (const char* s = getenv("MGO1_VDOLL_TEST_CLASS")) {   // (tests: the host's guard)
        const int v = *s >= '0' && *s <= '9' ? atoi(s) : -1;
        if (v >= 0 && v <= 255) {
            gTestClass = v;
            VLOG("vdoll: tests: every respawn tells the class %d in place of its player's (MGO1_VDOLL_TEST_CLASS): the host's guard is what is tested", v);
        }
    }
    std::vector<std::string> names;
    if (const char* f = getenv("MGO1_VDOLL_NAMES")) {      // one name a line (else VDOLL<n>)
        if (FILE* fp = fopen(f, "r")) {
            char line[64];
            while (fgets(line, sizeof line, fp)) {
                std::string s(line);
                // (v150-host beta 16) a line longer than the buffer: its rest read and left out (it gave the next doll a name
                // of its own and every name after it went one doll down)
                if (!s.empty() && s.back() != '\n' && !feof(fp)) { int ch; while ((ch = fgetc(fp)) != EOF && ch != '\n') {} }
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
                if (!s.empty()) names.push_back(s.substr(0, 15));
            }
            fclose(fp);
        }
    }
    std::vector<int> teams;
    if (const char* t = getenv("MGO1_VDOLL_TEAMS")) {       // red,blue,auto ... (one a doll, the last for the rest)
        std::string s(t);
        size_t at = 0;
        while (at <= s.size()) {
            const size_t c = s.find(',', at);
            const std::string w = s.substr(at, c == std::string::npos ? std::string::npos : c - at);
            teams.push_back(w == "red" ? 0 : w == "blue" ? 1 : -1);
            if (c == std::string::npos) break;
            at = c + 1;
        }
    }
    const int gap = getenv("MGO1_VDOLL_GAP") ? atoi(getenv("MGO1_VDOLL_GAP")) : 120;   // frames between two dolls' joins
    gGap = gap;
    const long long first = getenv("MGO1_VDOLL_AFTER") ? atoll(getenv("MGO1_VDOLL_AFTER")) : 0;
    uint32_t seed = 0x9E3779B9u ^ (uint32_t)(uintptr_t)&gDolls;
    // (v150-host beta 22, mash2) MGO1_VDOLL_SEED (tests: the same draws each run, the bodies' already) seeds the sessions' numbers
    // too: a doll's deploy waits 60 + its number % 90 frames, so a test's frames hung on the address its table got (ASLR)
    if (const char* e = getenv("MGO1_VDOLL_SEED")) seed = 0x9E3779B9u ^ (uint32_t)strtoul(e, nullptr, 0);
    for (int k = 0; k < count; ++k) {
        Doll d;
        d.k = k;
        d.id = Cfg().first + (uint32_t)k;
        d.name = k < (int)names.size() ? names[k] : "VDOLL" + std::to_string(k + 1);
        d.wantTeam = teams.empty() ? -1 : teams[std::min((size_t)k, teams.size() - 1)];
        const uint8_t a[4] = {198, 51, 100, (uint8_t)(10 + k)};
        memcpy(&d.ip, a, 4);
        d.port = 5730;
        seed = seed * 1103515245u + 12345u;
        d.nonce = seed ^ (uint32_t)(k * 0x01000193u) ^ 0x5bd1e995u;
        d.startAt = d.rejoinAfter = first + (long long)k * gap;
        gDolls.push_back(d);
    }
    body::Init(h);
    gOn = true;
    gAdEnv = EnvAdapt();                               // (v150-host beta 19: the live level, said after the line below)
    VLOG("vdoll: %d virtual doll(s)%s, user ids %#x to %#x, joining the host's own game from frame %lld (%d frames apart), %d seat%s kept for "
         "real players%s", gWant, Cfg().live ? " now, 15 at most (the count set live)" : "", Cfg().first, Cfg().first + (uint32_t)count - 1, first, gap,
         gKeepSeat, gKeepSeat == 1 ? "" : "s", gAlone ? ", playing with no real player too (MGO1_VDOLL_PLAY_ALONE)" : ", in a team while a real player plays");
    if (gAdEnv.on) AdaptApply(gAdEnv); else gAdSaid = true;
}

bool On() { return gOn; }

void HostGameGone(long long frame) {
    gFrame = frame;
    for (Doll& d : gDolls) Restart(d, "the host's game ended");
}

void SetDialect(uint32_t key, uint32_t mac) { gKey = key; gMac = mac; }

bool Ours(uint32_t ip, uint16_t port) {
    for (const Doll& d : gDolls) if (d.ip == ip && d.port == port) return true;
    return false;
}

void HostClear(uint32_t ip, uint16_t port, const uint8_t* clear, size_t n) {
    for (Doll& d : gDolls) if (d.ip == ip && d.port == port) { Take(d, clear, n); return; }
}

bool Pop(std::vector<uint8_t>& dgram, uint32_t* ip, uint16_t* port) {
    for (Doll& d : gDolls) {
        if (d.out.empty()) continue;
        // (v150-host beta 22, relay) one of its datagrams a read of the host's game: a second one waits for the next read (its
        // leave after the items it drops, a datagram pushed while one waited)
        if (gRelay && d.popPass == gPassNo) {
            if (!d.heldBack++ && gHeldSaid < 0) {
                gHeldSaid = gFrame;
                VLOG("vdoll %d: a datagram kept for the host's game's next read (one a doll a read: of two in one read the game takes the last one's unreliable records only)", d.k + 1);
            }
            continue;
        }
        d.popPass = gPassNo;
        dgram = std::move(d.out.front());
        d.out.pop_front();
        *ip = d.ip; *port = d.port;
        return true;
    }
    return false;
}

// (MGO1_VDOLL_DEBUG) the room's phase at each change, the real players' places every 2 s (tests/bench/tools/vrestart141check.py)
static void DebugTick() {
    static int phase = -2;
    const int ph = gHost.phase ? gHost.phase() : -1;
    if (ph != phase) { VLOG("vdoll: the room's phase %d -> %d (rule %d)", phase, ph, RuleNow()); phase = ph; }
    static long long last = -1000;
    if (!gHost.player || gFrame - last < 120) return;
    last = gFrame;
    std::string line;
    char b[96];
    for (int i = 0; i < gP.n; ++i) {
        if (gP.seen[i].kind != 0) continue;
        int team = 0xFF, life = 0; float pos[3];
        if (!gHost.player(gP.seen[i].id, &team, &life, pos)) continue;
        snprintf(b, sizeof b, "%sP%d team %d life %d at %.0f %.0f %.0f", line.empty() ? "" : "; ", gP.seen[i].id + 1, team, life, pos[0], pos[1], pos[2]);
        line += b;
    }
    if (!line.empty()) VLOG("vdoll: real players seen: %s", line.c_str());
}

// (v150-host beta 15) the count wanted, from MGO1_VDOLL_LIVE's file every 2 s: more join as seats are free, fewer leave (the last)
static void LiveTick() {
    if (!Cfg().live || gFrame - gLiveAt < 120) return;
    gLiveAt = gFrame;
    FILE* f = fopen(Cfg().liveFile.c_str(), "rb");
    if (!f) return;
    char b[512] = {};                                  // (v150-host beta 19: the live level's lines after the count; beta 18 read 15 bytes)
    const size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    char* e = nullptr;
    const long v = strtol(b, &e, 10);
    const int want = (int)std::max(0L, std::min(15L, v));
    if (n && e != b && want != gWant) {
        const int was = gWant;
        gWant = want;
        for (Doll& d : gDolls)
            if (d.k >= was && d.k < want && d.phase == OUT) d.startAt = d.rejoinAfter = std::max(d.rejoinAfter, gFrame + (long long)(d.k - was) * gGap);
        VLOG("vdoll: %d virtual doll(s) wanted now (%d before): %s", want, was, want > was ? "they join as seats are free" : "the last ones leave");
    }
    {   // (v150-host beta 19) `adapt 0|1`, `adapt_min <n>`, `adapt_max <n>`: each line present sets its value, else the environment's
        AdaptCfg c = gAdEnv;
        int a = -1, z = -1;
        for (const char* l = b; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : nullptr) {
            while (*l == ' ' || *l == '\t' || *l == '\r') ++l;
            if (!strncmp(l, "adapt_min", 9) && (l[9] == ' ' || l[9] == '\t' || l[9] == '=')) a = atoi(l + 10);
            else if (!strncmp(l, "adapt_max", 9) && (l[9] == ' ' || l[9] == '\t' || l[9] == '=')) z = atoi(l + 10);
            else if (!strncmp(l, "adapt", 5) && (l[5] == ' ' || l[5] == '\t' || l[5] == '=')) c.on = atoi(l + 6) == 1;
        }
        const int lo = a > 0 ? a : gAdEnv.lo, hi = z > 0 ? z : gAdEnv.hi;
        c.lo = adapt::Lo(lo, hi); c.hi = adapt::Hi(lo, hi);
        AdaptApply(c);
    }
}
// a doll wanted or still in the game: the bodies' map and the players read (none of it with no virtual doll at all)
static bool Active() {
    if (gWant > 0) return true;
    for (const Doll& d : gDolls) if (d.phase != OUT) return true;
    return false;
}

void Tick(long long frame) {
    if (!gOn) return;
    ++gPassNo;                                         // (v150-host beta 22, relay) the host's game's next read is another one
    if (gDebug) {                                      // ((v150-host beta 25, aimcost) the host's frames by how many times they ran this)
        static long long lastF = -1, since = -1, n[4] = {0, 0, 0, 0};
        static int calls = 0;
        // ((v150-host beta 25, dpass) where the host's game ticks its players between the passes: whether a player's copy on the host moved
        // since the last pass, and the real time since it, by the frames between them (0 the same frame, 1, 2 or more)
        static float copyWas[16][3]; static bool copyHad[16];
        static long long gapN[3] = {0, 0, 0}, gapMoved[3] = {0, 0, 0}; static double gapUs[3] = {0, 0, 0};
        static std::chrono::steady_clock::time_point lastAt;
        const std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
        if (lastF >= 0) {
            const int g = (int)std::min(2LL, std::max(0LL, frame - lastF));
            bool moved = false;
            for (int id = 0; id < 16; ++id) {
                PlayerNow p;
                const bool has = gHost.see && gHost.see(id, &p) && p.object;
                if (has && copyHad[id] && std::hypot(p.pos[0] - copyWas[id][0], p.pos[2] - copyWas[id][2]) > 1.f) moved = true;
                if (has) memcpy(copyWas[id], p.pos, sizeof copyWas[id]);
                copyHad[id] = has;
            }
            ++gapN[g]; if (moved) ++gapMoved[g];
            gapUs[g] += (double)std::chrono::duration_cast<std::chrono::microseconds>(at - lastAt).count();
        }
        lastAt = at;
        if (frame != lastF) {
            if (lastF >= 0) { ++n[std::min(3, calls)]; n[0] += std::max(0LL, frame - lastF - 1); }
            if (since < 0) since = frame;
            lastF = frame; calls = 0;
            if (frame - since >= 3600) {
                VLOG("vdoll: the host's frames these %lld: %lld ran its passes once, %lld twice, %lld three times or more, %lld none", frame - since, n[1], n[2], n[3], n[0]);
                VLOG("vdoll: its passes these %lld frames, by the frames since the one before: the same frame %lld (a player's copy moved since in %lld, %.1f ms after it on "
                     "average), the next %lld (moved in %lld, %.1f ms), two or more on %lld (moved in %lld, %.1f ms)", frame - since, gapN[0], gapMoved[0],
                     gapN[0] ? gapUs[0] / gapN[0] / 1000.0 : 0.0, gapN[1], gapMoved[1], gapN[1] ? gapUs[1] / gapN[1] / 1000.0 : 0.0, gapN[2], gapMoved[2],
                     gapN[2] ? gapUs[2] / gapN[2] / 1000.0 : 0.0);   // ((v150-host beta 25, dpass) docs/VIRTUAL_DOLL.md 47)
                since = frame; n[0] = n[1] = n[2] = n[3] = 0;
                for (int g = 0; g < 3; ++g) { gapN[g] = gapMoved[g] = 0; gapUs[g] = 0; }
            }
        }
        ++calls;
    }
    gFrame = frame;
    if (gClockTrace) ClockTrace(frame);                // ((v150-host beta 25, clockfix, benches))
    LiveTick();
    if (!Active()) return;
    if (gDebug) DebugTick();
    PresenceTick();
    AdaptTick();                                       // (v150-host beta 19: the live level, off by default)
    body::Frame(frame);
    for (Doll& d : gDolls) DollTick(d);
    SeatsTick();
    static long long lastStatus = 0;
    if (gDebug && frame - lastStatus >= 1800) { lastStatus = frame; VLOG("%s", Status().c_str()); }
}

std::string Status() {
    std::string s;
    char b[320];
    for (const Doll& d : gDolls) {
        if (d.k >= gWant && d.phase == OUT) continue;
        snprintf(b, sizeof b, "%svdoll %d: %s, player %d, team %d, round %d, life %d, %s, %d deaths, %d leaves, datagrams %d out %d in, %zu reliable waiting",
                 s.empty() ? "" : "\n", d.k + 1, PhaseName(d.phase), d.slot + 1, d.team, d.gen, d.life, LifeName(d.st), d.deaths, d.leaves,
                 d.dgramsOut, d.dgramsIn, d.rel.size());
        s += b;
        // (v150-host beta 22, relay) its datagrams kept for a later read of the host's game, its flushes that waited for one
        if (gRelay && (d.heldBack || d.flushWaits)) { snprintf(b, sizeof b, ", %lld kept for a later read, %lld flushes waited", d.heldBack, d.flushWaits); s += b; }
    }
    return s;
}

bool AdaptOn(int* lo, int* hi) { if (lo) *lo = gAdNow.lo; if (hi) *hi = gAdNow.hi; return gAdNow.on; }
float LevelNow(int k) { return gAdNow.on && k >= 0 && k < (int)gLvNow.size() ? gLvNow[k] : -1.f; }

std::string StatusJson() {
    std::string s = "[";
    char b[256];
    for (const Doll& d : gDolls) {
        if (d.k >= gWant && d.phase == OUT) continue;
        snprintf(b, sizeof b, "%s{\"n\":%d,\"state\":\"%s\",\"player\":%d,\"team\":%d,\"life\":\"%s\",\"deaths\":%d,\"leaves\":%d}", s.size() > 1 ? "," : "",
                 d.k + 1, d.phase == JOINED ? "in_game" : d.phase == OUT ? "out" : d.phase == LEAVING ? "leaving" : "joining", d.slot + 1, d.team,
                 LifeName(d.st), d.deaths, d.leaves);
        if (gAdNow.on) {                                // (v150-host beta 19) the live level: its level set, its level now (-1: not in the game)
            const size_t e = strlen(b);
            if (e > 0 && b[e - 1] == '}') snprintf(b + e - 1, sizeof b - e + 1, ",\"level\":%d,\"level_now\":%.2f}", body::LevelSet(d.k), LevelNow(d.k));
        }
        s += b;
    }
    return s + "]";
}

}  // namespace vdoll
