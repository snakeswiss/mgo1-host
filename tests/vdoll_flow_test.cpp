// the virtual dolls' join, round and seats (docs/VIRTUAL_DOLL.md) against a small stand-in of the host's game: it opens
// their datagrams, answers as the host's game does (the key, the members, the config blocks, the claim's 0x83, the pick's
// team and object, the room's team changes, the close) and keeps a session table with real players the scenarios add.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_flow_test.cpp MGO1Loader/vdoll.cpp MGO1Loader/vdoll_body.cpp \
//       MGO1Loader/doll_geom.cpp MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_flow_test && ./vdoll_flow_test
#include "vdoll.h"
#include "vdoll_body.h"
#include "vdoll_mash.h"
#include "vdoll_stamina.h"
#include "vdoll_support.h"
#include "doll_geom.h"
#include "doll_nav.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

namespace {

const uint32_t kKey = 0xA7130C2F, kMac = 0x48B5ED69;
long long gNow = 0;
std::vector<std::string> gLog;
std::vector<long long> gLogF;                        // (v150-host beta 24, stamina2: each log line's frame)
void Log(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    gLog.push_back(b); gLogF.push_back(gNow);
    if (getenv("VDOLL_TEST_VERBOSE")) printf("  f%lld %s\n", gNow, b);
}

// team: the host's for the round (its 0x85); shown: the session table's when it still holds the last round's (-1: the same)
struct Player { int id; int kind; int team; uint32_t uid; int life = 200; bool closed = false; int shown = -1; };
struct Peer {                                        // a virtual doll as the stand-in host knows it
    uint32_t nonce = 0, hostNonce = 0; int slot = -1; bool keyed = false;
    std::map<uint16_t, uint8_t> nextSeq;
    std::map<uint16_t, int> want; int wantGen = -1;   // the next reliable record awaited on each of the round's channels
    std::vector<uint8_t> pending;                     // the records for the next datagram to it
    uint16_t seq = 0;
    uint16_t expect = 0;                              // (v150-host beta 21) the number the host's game awaits for its next datagram
    long dgrams = 0, packed = 0, far = 0;             // its datagrams in all, those read as compressed, those numbered too far
};
struct Game {
    int gen = 0, rule = 1, phase = 2, seats = 8, roomChan = 0x62;
    int kind = 1;                                     // the class the host's player object tells (its second byte)
    int copyClass = -1;                               // the class the host's game holds for its copies (-1: not read)
    int units[2] = {-1, -1};                          // the classes of the red and the blue team's soldiers (-1: not known)
    bool guard = false;                               // the host's game checks the character a spawn asks for
    std::map<int, Player> players;                    // by id
    std::map<uint32_t, Peer> peers;                   // by the doll's ip
    std::vector<std::string> events;                  // what the dolls sent of note: "pick 2 1", "room 09 2 fe", "close 2"
    int NextId() { for (int id = 1; id < 16; ++id) if (!players.count(id)) return id; return -1; }
    void Ev(const char* fmt, ...) {
        char b[160];
        va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        events.push_back(std::string("f") + std::to_string(gNow) + " " + b);
        if (getenv("VDOLL_TEST_VERBOSE")) printf("  f%lld event %s\n", gNow, b);
    }
};
Game G;

void PutRec(std::vector<uint8_t>& o, uint16_t chan, uint8_t fl, int seq, const std::vector<uint8_t>& d) {
    if (seq >= 0) fl |= 0x10;
    o.push_back((uint8_t)chan); o.push_back((uint8_t)(fl | ((chan >> 8) & 0x0f)));
    o.push_back((uint8_t)d.size());
    if (seq >= 0) o.push_back((uint8_t)seq);
    o.insert(o.end(), d.begin(), d.end());
}
void Rel(Peer& p, uint16_t chan, const std::vector<uint8_t>& d) { PutRec(p.pending, chan, 0, p.nextSeq[chan]++, d); }
std::vector<uint8_t> Team85(int gen) {
    std::vector<uint8_t> m = {0x85, (uint8_t)gen, 0x00, 0xFE};
    for (auto& [id, pl] : G.players) { m.push_back((uint8_t)id); m.push_back((uint8_t)pl.team); }
    return m;
}
int SideWithFewer() {
    int n[2] = {0, 0};
    for (auto& [id, pl] : G.players) if (pl.team == 0 || pl.team == 1) ++n[pl.team];
    return n[1] < n[0] ? 1 : 0;
}
void FollowCopy(int slot, const float* xyz);         // (v150-host beta 22, mash: the host's copy at the doll's place records)
bool gHaveSpawnAt = false, gFollowCopy = false;      // (v150-host beta 22, mash: a map's point for the spawns; the copy follows)
int gKoCountMax[16] = {}, gKoCountMin[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
int gStamNow[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // (v150-host beta 24, stamina: its last state tick's)
std::vector<std::pair<long long, int>> gStamAt[16];  // ((stamina2) each state tick's stamina by its frame)
std::vector<long long> gPlaceAt[16];                  // ((v150-host beta 25, fallflinch) each place record's frame)
std::vector<std::pair<long long, uint32_t>> gWeapAt[16];   // ((v150-host beta 25, fallflinch) each {13}'s frame, its item, weapon, flags)
int StamAt(int sl, long long f) { int v = -1; for (const auto& x : gStamAt[sl]) { if (x.first > f) break; v = x.second; } return v; }
float gSpawnAt[3] = {0, 0, 0};
long long gDeathRecLast[16] = {};                    // (v150-host beta 25, snakeghost) each player's last death record {07}, their count
long gDeathRecs[16] = {};
int gDeathKindLast[16] = {};                         // ((v150-host beta 25, respawnwait) the kind each player's last {07} told
bool gDeaf = false;                                  // the host's game answers no claim (tests)
int gClaims = 0;                                     // the claims of the round being played it was sent
void SendAll(Peer& p, uint32_t ip) {
    if (p.pending.empty()) return;
    std::vector<uint8_t> clear = {(uint8_t)p.seq, (uint8_t)(p.seq >> 8)};
    ++p.seq;
    clear.insert(clear.end(), p.pending.begin(), p.pending.end());
    p.pending.clear();
    vdoll::HostClear(ip, 5730, clear.data(), clear.size());
}
void Message(Peer& p, uint16_t chan, const uint8_t* d, size_t n) {
    const int lo = chan & 0xff;
    if (chan == 0 && n == 9 && !p.keyed) {           // the connect: a player added, the host's number back
        memcpy(&p.nonce, d, 4);
        uint32_t uid; memcpy(&uid, d + 5, 4);
        if (p.slot < 0) {
            p.slot = G.NextId();
            G.players[p.slot] = Player{p.slot, 3, 0xFF, uid};
            G.Ev("connect %d uid %#x", p.slot, uid);
        }
        p.hostNonce = 0x12345678u ^ (uint32_t)p.slot;
        p.keyed = true;
        PutRec(p.pending, 0, 0, p.nextSeq[0]++, {(uint8_t)p.hostNonce, (uint8_t)(p.hostNonce >> 8), (uint8_t)(p.hostNonce >> 16), (uint8_t)(p.hostNonce >> 24)});
        return;
    }
    if (chan == 1 && n == 1 && d[0] == 1) {          // the leave: the player gone (0x173600)
        G.Ev("close %d", p.slot);
        G.players.erase(p.slot);
        Peer fresh;
        fresh.pending = p.pending;                    // (its acknowledgment still goes)
        p = fresh;
        return;
    }
    if (chan == 1 && n >= 1) {
        Player& me = G.players[p.slot];
        if (d[0] == 0x02) {                          // the hello: the members, then op 2
            std::vector<uint8_t> m = {0x07, 15, 0, 0, 0, 0, 0, (uint8_t)p.slot, 0, 0, 0, 0, 0, 0};
            m.push_back((uint8_t)me.uid); m.push_back((uint8_t)(me.uid >> 8)); m.push_back((uint8_t)(me.uid >> 16)); m.push_back((uint8_t)(me.uid >> 24));
            m.resize(7 + 15, 0);
            Rel(p, 1, m);
            Rel(p, 1, {0x07, 0, 0, 0, 0, 0, 2});
        } else if (d[0] == 0x0A) {
            me.kind = 2;
            Rel(p, 1, {0x0B, 0, 1, 0, 0, 0, 0, 0});
        } else if (d[0] == 0x81 && n >= 3 && d[1] == (uint8_t)G.gen) {
            ++gClaims;
            if (gDeaf) return;                           // (the scenario claimagain: a round whose claims get no answer)
            Rel(p, 1, {0x83, (uint8_t)G.gen, 1});
            Rel(p, 1, Team85(G.gen));
        } else if (d[0] == 0x82 && n >= 2 && d[1] == (uint8_t)G.gen) {
            const int team = n >= 4 && d[2] != 0xFF ? d[2] : (G.rule == 0 ? 0 : SideWithFewer());
            G.Ev("pick %d %s%d", p.slot, n >= 4 ? "" : "auto ", team);
            me.team = team; me.life = 200; me.shown = -1;
            Rel(p, 1, Team85(G.gen));
            std::vector<uint8_t> o = {0x07, 21, 0, 7, 0, 0, 0, (uint8_t)p.slot, (uint8_t)G.kind, 1, 200, 0, (uint8_t)(0x40 + 3 * p.slot), 0};
            float xyz[3] = {1000.f * p.slot, 800.f, -500.f};
            if (gHaveSpawnAt) { xyz[0] = gSpawnAt[0]; xyz[1] = gSpawnAt[1] + 800.f; xyz[2] = gSpawnAt[2]; }   // (beta 22, mash: on the map)
            for (float f : xyz) { uint8_t b[4]; memcpy(b, &f, 4); o.insert(o.end(), b, b + 4); }
            o.push_back(0xff); o.push_back(0x7f);
            Rel(p, 1, o);
            Rel(p, 1, {0x83, (uint8_t)G.gen, 2});
        }
        return;
    }
    if ((lo == 0x62 || lo == 0x72) && n >= 3 && (d[0] == 0x09 || d[0] == 0x0A)) {
        G.Ev("room %02x %d %02x", d[0], d[1], d[2]);
        if (d[0] == 0x0A) { G.players[p.slot].team = d[2]; G.players[p.slot].shown = -1; Rel(p, 1, Team85(G.gen)); }
        return;
    }
    if ((lo == 0x62 || lo == 0x72) && n >= 2 && d[0] == 0x07) { G.Ev("room death %d", d[1]); return; }
    // (v150-host beta 19) its death record on its player's second channel {07 kind, killer}: each change logged
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 1 && n >= 3 && d[0] == 0x07) {
        static std::map<int, int> said;
        const int sl = (lo - 0x40) / 3, v = d[1] << 8 | d[2];
        if (sl >= 0 && sl < 16) { gDeathRecLast[sl] = gNow; ++gDeathRecs[sl]; gDeathKindLast[sl] = d[1] & 15; }   // ((v150-host beta 25, snakeghost) every one)
        if (!said.count(sl) || said[sl] != v) { said[sl] = v; G.Ev("death record %d death %d by %d", sl, d[1] & 15, d[2]); }
    }
    if (lo >= 0x40 && lo < 0x70 && n >= 1 && (lo - 0x40) % 3 == 0 && d[0] == 0x02 && n >= 4) G.Ev("spawn %d kind %d team %02x class %d", p.slot, d[1], d[2], d[3]);
    // (v150-host beta 22, blast) its fall's records ({0A id state} at each change, {10} each), its state tick's down event at
    // each change, its places (the last one kept)
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 1 && n >= 3 && d[0] == 0x0A) {
        static std::map<int, int> fall;
        const int sl = (lo - 0x40) / 3;
        if (!fall.count(sl) || fall[sl] != d[2]) { fall[sl] = d[2]; G.Ev("fall %d %d", sl, d[2]); }
    }
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 1 && n >= 1 && d[0] == 0x10) G.Ev("landing %d", (lo - 0x40) / 3);
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 0 && n >= 3 && d[0] == 0x03) {
        static std::map<int, int> ev;
        const int sl = (lo - 0x40) / 3;
        if (!ev.count(sl) || ev[sl] != d[2]) { ev[sl] = d[2]; G.Ev("down event %d %d", sl, d[2]); }
    }
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 0 && n >= 13 && d[0] == 0x00) {
        float xyz[3]; memcpy(xyz, d + 1, 12);
        static std::map<int, std::string> was;
        char b[64]; snprintf(b, sizeof b, "place %d %.0f %.0f %.0f", (lo - 0x40) / 3, xyz[0], xyz[1], xyz[2]);
        if (was[(lo - 0x40) / 3] != b) { was[(lo - 0x40) / 3] = b; G.Ev("%s", b); }
        if ((lo - 0x40) / 3 < 16) gPlaceAt[(lo - 0x40) / 3].push_back(gNow);
        FollowCopy((lo - 0x40) / 3, xyz);
    }
    // ((v150-host beta 25, fallflinch) each weapon's record {13 item, weapon, flags} with its frame)
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 2 && n >= 4 && d[0] == 0x13 && (lo - 0x40) / 3 < 16)
        gWeapAt[(lo - 0x40) / 3].push_back({gNow, (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]});
    // (v150-host beta 22, mash) its state tick's last byte while knocked out (the recovery's count, 8 + its presses of the window):
    // the most seen, the last
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 0 && n >= 8 && d[0] == 0x03 && d[7]) {
        const int sl = (lo - 0x40) / 3;
        if (sl >= 0 && sl < 16) { gKoCountMax[sl] = std::max(gKoCountMax[sl], (int)d[7]); gKoCountMin[sl] = std::min(gKoCountMin[sl], (int)d[7]); }
    }
    // (v150-host beta 24, stamina) its stamina as its state tick tells it (bytes 3 and 4), the last one
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 0 && n >= 5 && d[0] == 0x03) {
        const int sl = (lo - 0x40) / 3;
        if (sl >= 0 && sl < 16) { gStamNow[sl] = d[3] | d[4] << 8; gStamAt[sl].push_back({gNow, gStamNow[sl]}); }
    }
    // (the configurations' audit) its state tick's state at each change: 1 spawned, 0 in play (deployed)
    static std::map<int, int> state;
    if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 0 && n >= 2 && d[0] == 0x03) {
        const int sl = (lo - 0x40) / 3;
        if (!state.count(sl) || state[sl] != d[1]) { state[sl] = d[1]; G.Ev("state %d %d", sl, d[1]); }
    }
}
// (v150-host beta 17) the game's streams of a round (the room's, the players': 0x40 and up): made anew at each round, their
// reliable records taken in order from 0, under numbers that come back every other round (bit 11: the round's bit 0)
bool InOrder(Peer& p, uint16_t chan, int s) {
    if (s < 0 || (chan & 0x7ff) < 0x40) return true;
    if (p.wantGen != G.gen) {
        for (auto it = p.want.begin(); it != p.want.end();) { if (((it->first >> 11) & 1) == (G.gen & 1)) it = p.want.erase(it); else ++it; }
        p.wantGen = G.gen;
    }
    int& w = p.want[chan];
    if (s < w) return false;                          // (sent again)
    if (s != w) { G.Ev("stream %03x: record %d while %d is awaited, not taken", chan, s, w); return false; }
    ++w;
    return true;
}
// (v150-host beta 22, relay) the host's game's read as measured on its P2P logs (reports/vdoll_relay.md): it reads every datagram
// waiting in one pass, and of the datagrams of one doll read in one pass it takes the reliable records and the acknowledgments
// of each, the unreliable records of the LAST one only (gReadRule: Run's reads and RunPaced's follow it). The counts: the
// unreliable records of the dolls taken and left, the reads with two datagrams of one doll, the hits {04} taken on each
// player's first channel, the frames each player's unreliable records were taken at
bool gReadRule = false;
long gUnrelTaken = 0, gUnrelLost = 0, gTwoInRead = 0;
std::map<int, long> gLostKind;
long gHitsTaken[16] = {}, gClayHitsTaken[16] = {};   // (the claymore's: kind 0x1B)
std::map<int, std::vector<long long>> gTakenAt;
void Datagram(const std::vector<uint8_t>& dg, uint32_t ip, bool unrel = true) {
    Peer& p = G.peers[ip];
    uint16_t seq; std::vector<uint8_t> body;
    const bool boot = vdoll::Open(dg, kKey, kMac, &seq, &body);
    bool ok = boot;
    if (!ok && p.keyed) ok = vdoll::Open(dg, p.nonce ^ p.hostNonce, p.nonce ^ p.hostNonce ^ kMac, &seq, &body);
    if (!ok) { printf("FAIL a datagram not opened\n"); exit(1); }
    // (v150-host beta 21) the datagram's number as the host's game reads it (the receiver 0x16FD58): 16 bits, counted by
    // the receiver too; a number 0x4000 to 0xBFFF away from the one awaited is a COMPRESSED datagram (the sender flips
    // the top bit, 0x16F764) whose body the game unpacks: a plain body read so is lost. More than 256 ahead: dropped
    ++p.dgrams;
    if (boot) p.expect = (uint16_t)(seq + 1);         // (the connect and the hello: the session's first numbers)
    else {
        int diff = (int16_t)(uint16_t)(seq - p.expect);
        const bool packed = ((diff - 0x4000) & 0x8000) == 0;
        if (packed) diff ^= (int)0xFFFF8000;
        if ((diff & 0xffff) >= 0x101) { ++p.far; return; }
        p.expect = (uint16_t)(p.expect + (diff & 0xffff) + 1);
        if (packed) {
            if (!p.packed++) G.Ev("datagram %ld of player %d (number %u) read as compressed: nothing of it read", p.dgrams, p.slot, seq);
            return;
        }
    }
    size_t i = 0;
    bool took = false;
    while (i + 3 <= body.size()) {
        const uint16_t chan = (uint16_t)(body[i] | (body[i + 1] & 0x0f) << 8);
        const uint8_t fl = body[i + 1] & 0xf0;
        size_t j = i + 2, len = body[j++];
        int s = -1;
        if (fl & 0x10) s = body[j++];
        if (!(fl & 0x40) && s < 0 && len) {             // (v150-host beta 22, relay) an unreliable record: taken or left by the read
            if (!unrel) { ++gUnrelLost; ++gLostKind[body[j]]; i = j + len; continue; }
            ++gUnrelTaken; took = true;
            const int lo = chan & 0xff;
            if (lo >= 0x40 && lo < 0x70 && (lo - 0x40) % 3 == 0 && body[j] == 0x04) { ++gHitsTaken[(lo - 0x40) / 3]; if (len >= 7 && body[j + 6] == 0x1B) ++gClayHitsTaken[(lo - 0x40) / 3]; }
        }
        if (!(fl & 0x40) && s >= 0) PutRec(p.pending, chan, 0x40, s, {});   // acknowledged
        if (!(fl & 0x40) && InOrder(p, chan, s)) Message(p, chan, body.data() + j, len);
        i = j + len;
    }
    if (took) gTakenAt[p.slot].push_back(gNow);
    SendAll(G.peers[ip], ip);
}

// the platform's side
int Gen() { return G.gen; }
float gCopyAt[16][3];                                // (v150-host beta 22, blast) the host's copies' places (0 0 0 unless set)
unsigned gCopyState[16];                             // (v150-host beta 22, blast) their first state words (Host::see)
bool SeeCopy(int id, vdoll::PlayerNow* out) {
    if (id < 0 || id >= 16 || !G.players.count(id)) return false;
    *out = vdoll::PlayerNow(); out->object = true; out->alive = true; out->state = gCopyState[id];
    memcpy(out->pos, gCopyAt[id], sizeof out->pos);
    return true;
}
// (v150-host beta 22, mash) a disc map for the doll's body (a scenario's optional second argument, never in the repository): its
// collision, a point of its mesh with open ground round it for the spawns, and the host's copy of the doll following its place
// records as the host's game moves it (Message)
std::vector<uint8_t> gMapGeom;
std::string gMapPage = "n003a";
bool MapNowT(std::string* page, int* extra) { *page = gMapPage; *extra = 0; return true; }
// (v150-host beta 22, mash2) the machine's clock of a host slowed to 38 frames a second: 1000 / 38 ms each of its frames
unsigned long long SlowMs() { return (unsigned long long)((double)gNow * 1000.0 / 38.0); }
// ((v150-host beta 25, respawnwait) a host at full pace whose ticks come unevenly: the machine's ms 9 ms early, then 9 ms late, every
// other pass (a pass 15 ms, the next 51 ms; the same 59.94 frames a real second on the whole)
unsigned long long JitterMs() { return (unsigned long long)((double)gNow * 1000.0 / 59.94 + ((gNow / 2) % 2 ? 18.0 : 0.0)); }
// ((v150-host beta 25, fallflinch) a host at full pace whose frames come bunched, as ff_ba_after2's (pace 59.5, 2500 frames a minute more
// than 4 ms late): each four frames 66.7 ms; the doll's tick (every other frame) sees 16 ms, then 50.7, again and again)
double BunchAt(long long f) { return (double)(f / 4) * 4000.0 / 59.94 + (f % 4 >= 2 ? 16.0 : 0.0); }
unsigned long long BunchMs() { return (unsigned long long)BunchAt(gNow); }
// ((v150-host beta 25, clockfix) JitterMs's clock at a frame; a host held by a processor quota (the owner's server while people play:
// frozen 40 to 70 ms in every 100 ms, reports/pace.md; the benches' throttle): each of its frames QUOTA_WORK ms of work done only in the
// first QUOTA_RUN ms of every 100 ms, so its frames come in bursts a few ms apart, then a freeze (CLOCK_QUOTA_WORK, CLOCK_QUOTA_RUN)
double JitterAt(long long f) { return (double)f * 1000.0 / 59.94 + ((f / 2) % 2 ? 18.0 : 0.0); }
double gQuotaWork = 8.2, gQuotaRun = 34.0;
double QuotaAt(long long f) { const double cpu = (double)f * gQuotaWork, n = std::floor(cpu / gQuotaRun); return n * 100.0 + (cpu - n * gQuotaRun); }
unsigned long long QuotaMs() { return (unsigned long long)QuotaAt(gNow); }
// ((clockfix) the number after `key` in the last log line that has it (-1: none)
double LogNum(const char* key) {
    for (auto it = gLog.rbegin(); it != gLog.rend(); ++it) { const size_t p = it->find(key); if (p != std::string::npos) return atof(it->c_str() + p + strlen(key)); }
    return -1;
}
// (v150-host beta 25, fallflinch) a player's flinch by the reaction's kind, in his game's ticks (30 a second), measured on real clients
// (probe 52's REACTION lines: bh_hits4 and ff_hits1; reports/vdoll_fallflinch.md 2)
const int kPlayerFlinchTicks[6] = {0, 17, 17, 28, 29, 30};       // (0: not measured; the doll keeps b16's 40 frames. (v150-host beta 25, reactfix) 4: 29, rf_ba_after; 5: 30, rf_m2)
bool StageGeomT(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gMapGeom; return true; }
bool LoadMap(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) gMapGeom.insert(gMapGeom.end(), b, b + n);
    fclose(f);
    const char* base = strrchr(path, '/'); base = base ? base + 1 : path;
    if (strlen(base) >= 5) gMapPage.assign(base, 5);
    doll::Geom g; doll::NavMesh nav; doll::NavConfig cfg; std::string err, log;
    if (gMapGeom.empty() || !doll::DecodeGeom(gMapGeom.data(), gMapGeom.size(), g, err) || !nav.Build(g, cfg, log)) return false;
    static uint32_t seed = 0x2545F491u;
    unsigned (*rnd)() = []() -> unsigned { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    for (int k = 0; k < 5000; ++k) {                 // (a point with points of the mesh 8 m round it on its floor: room to walk on)
        float p[3], q[3];
        if (!nav.RandomPoint(nullptr, 0.f, p, rnd) || !nav.RandomPoint(p, 9000.f, q, rnd)) continue;
        const float d = std::hypot(p[0] - q[0], p[2] - q[2]);
        if (d < 7000.f || std::fabs(p[1] - q[1]) > 300.f) continue;
        memcpy(gSpawnAt, p, sizeof gSpawnAt);
        gHaveSpawnAt = true;
        return true;
    }
    return false;
}
bool PlayerOf(int id, int* team, int* life, float pos[3]) {
    auto it = G.players.find(id);
    if (it == G.players.end()) return false;
    *team = it->second.team; *life = it->second.life; pos[0] = pos[1] = pos[2] = 0;
    if (id >= 0 && id < 16) memcpy(pos, gCopyAt[id], 3 * sizeof(float));
    return true;
}
// (v150-host beta 21) SNEAKING's soldiers (the red side of rule 4): the seven points of the stage's list, as the host's
// VdollSpawnPoint gives them by doll_geom.cpp RespawnKinds (20000 + 1000 k, 0, -30000); gNoPoints: not read yet (none)
bool gNoPoints = false;
void FollowCopy(int slot, const float* xyz) { if (gFollowCopy && slot >= 0 && slot < 16) memcpy(gCopyAt[slot], xyz, sizeof gCopyAt[slot]); }
bool Spawn(int team, unsigned pick, float out[3]) {
    if (gNoPoints) return false;
    if (gHaveSpawnAt) { memcpy(out, gSpawnAt, 3 * sizeof(float)); return true; }   // (v150-host beta 22, mash: a point of the map's mesh)
    if (G.rule == 4 && team == 0) { out[0] = 20000.f + 1000.f * (float)(pick % 7); out[1] = 0; out[2] = -30000.f; return true; }
    out[0] = team == 0xFE ? 0.f : 5000.f * team; out[1] = 0; out[2] = 0; return true;
}
int Players(vdoll::Seen* out, int max) {
    int n = 0;
    out[n].id = 0; out[n].kind = 1; out[n].team = 0xFE; out[n].uid = 0x1000; ++n;
    for (auto& [id, pl] : G.players) {
        if (n >= max) break;
        out[n].id = id; out[n].kind = pl.kind; out[n].team = pl.shown >= 0 ? pl.shown : pl.team; out[n].uid = pl.uid; ++n;
    }
    return n;
}
int Rule() { return G.rule; }
int ClassOf(int) { return G.copyClass; }
int TeamClass(int team) { return team >= 0 && team < 2 ? G.units[team] : -1; }
bool Guard() { return G.guard; }
int Phase() { return G.phase; }
// (v150-host beta 18) the mission objects of a TEAM SNEAKING round: one item, the goal of the side that sneaks (G.sneak)
int gSneak = -1;
int Objectives(vdoll::ObjNow* out, int max, float goals[2][3], bool set[2]) {
    if (max < 1) return 0;
    out[0] = vdoll::ObjNow(); out[0].pos[0] = 20000.f; out[0].pos[2] = 20000.f;
    for (int g = 0; g < 2; ++g) { goals[g][0] = -20000.f; goals[g][1] = 0.f; goals[g][2] = -20000.f; set[g] = g == gSneak; }
    return 1;
}
// (v150-host beta 19) the players' counters (kills, deaths) for the live level; how often the host was asked
std::map<int, std::pair<int, int>> gScore;
int gScoreAsked = 0;
bool Score(int id, int* k, int* d, int* r) {
    ++gScoreAsked;
    if (!G.players.count(id)) return false;
    *k = gScore[id].first; *d = gScore[id].second; *r = -1;
    return true;
}
bool RoomScore(int wins[2], int tickets[2]) { wins[0] = wins[1] = 0; tickets[0] = tickets[1] = 0; return true; }
// the last "levels now" line's numbers
std::vector<float> LevelsNow() {
    std::vector<float> v;
    for (auto it = gLog.rbegin(); it != gLog.rend(); ++it) {
        const size_t a = it->find("vdoll: levels now ");
        if (a == std::string::npos) continue;
        const char* p = it->c_str() + a + 18;
        char* e = nullptr;
        for (float x = strtof(p, &e); e != p; x = strtof(p, &e)) { v.push_back(x); p = e; }
        break;
    }
    return v;
}
void WriteFile(const char* path, const char* text) { FILE* f = fopen(path, "wb"); if (f) { fputs(text, f); fclose(f); } }
int LogCount(const char* what) { int n = 0; for (const auto& l : gLog) if (l.find(what) != std::string::npos) ++n; return n; }
int Seats() { return G.seats; }
int Room() { return G.roomChan; }

// one read of the host's game: every datagram waiting; with gReadRule, of one doll's datagrams the unreliable records of the
// last one only (reports/vdoll_relay.md)
void ReadPass() {
    std::vector<std::pair<std::vector<uint8_t>, uint32_t>> got;
    std::vector<uint8_t> dg; uint32_t ip; uint16_t port;
    while (vdoll::Pop(dg, &ip, &port)) got.push_back({dg, ip});
    for (size_t k = 0; k < got.size(); ++k) {
        bool later = false;
        for (size_t m = k + 1; m < got.size(); ++m) if (got[m].second == got[k].second) later = true;
        if (later) ++gTwoInRead;
        Datagram(got[k].first, got[k].second, !(gReadRule && later));
    }
}
void Team85All() {                                   // the round's teams to each joined doll (a reliable record)
    for (auto& [pip, p] : G.peers)
        if (p.keyed && p.slot >= 0 && G.players.count(p.slot) && G.players[p.slot].kind == 2) { Rel(p, 1, Team85(G.gen)); SendAll(p, pip); }
}
// ((v150-host beta 25, aimcost) gTwice: each pass run twice in its frame, as a real host runs them: its game runs its actor lists
// twice for a tick (docs/FPS60.md), and the dolls' tick at the start of each (aimcost_d2's host: 7577 frames twice, 2559 once, 7857
// none); the second has no frame since the first)
bool gTwice = false;
// ((v150-host beta 25, clockfix) gMixed: the passes 1, 1 then 2 frames apart in turn, 36, 20 and 44 ms of the machine's clock (MixedMs): a
// host held by a processor quota (bench cf_k_before at pace 34: two passes in three one frame apart, the freezes in those), 25 ms a frame
// on the whole, the one-frame passes the longer a frame
// ((clockfix) gMixedStep: with gFreeze, 70, 10 and 20 ms: the one-frame pass after a freeze longer than 3 frames' worth (50 ms), as 28 % of
// cf_k_before's one-frame passes)
bool gMixed = false;
unsigned long long gMixedMs = 0;
int gMixedStep[3] = {36, 20, 44};
std::map<long long, unsigned long long> gMixedAt;   // (each pass's frame and its machine ms)
unsigned long long MixedMs() { return gMixedMs; }
double MixedAt(long long f) { auto it = gMixedAt.upper_bound(f); return it == gMixedAt.begin() ? 0.0 : (double)std::prev(it)->second; }
void Run(long long frames) {
    static long long last85 = 0;
    static int mixed = 0;
    for (long long f = 0; f < frames; f += 2) {     // (the host's game passes every other frame, on odd frames)
        if (gMixed) { const int k = mixed++ % 3; gNow += k == 2 ? 2 : 1; gMixedMs += gMixedStep[k]; gMixedAt[gNow] = gMixedMs; if (k != 2) f -= 1; }
        else gNow += gNow % 2 ? 2 : 1;
        vdoll::Tick(gNow);
        if (gTwice) vdoll::Tick(gNow);
        ReadPass();
        if (gNow - last85 >= 6) {                     // the round's teams to each joined doll every 100 ms
            last85 = gNow;
            Team85All();
        }
    }
}
// (v150-host beta 22, relay) the dolls tick each frame (the host's actor pass) while the host's game reads its sessions every 2
// to 7 frames, the gaps of its reads measured on the host's P2P logs (heavy_m3_dm3: 2 and 4 most, then 6, 3, 5, 7), and sends
// its reliable records at its reads
void RunPaced(long long frames) {
    static const int kGaps[] = {2, 4, 2, 4, 6, 3, 2, 4, 5, 2, 4, 7, 4, 2, 6};
    static size_t gi = 0;
    static long long nextRead = 0, last85 = 0;
    for (long long f = 0; f < frames; ++f) {
        ++gNow;
        vdoll::Tick(gNow);
        if (gNow < nextRead) continue;
        nextRead = gNow + kGaps[gi++ % (sizeof kGaps / sizeof kGaps[0])];
        ReadPass();
        if (gNow - last85 >= 6) { last85 = gNow; Team85All(); }
    }
}
// a new round of another rule: every player out of a team for the host, his last team still in the session's table
void NewRound(int rule) {
    ++G.gen; G.rule = rule;
    for (auto& [id, pl] : G.players) if (pl.kind != 1) { pl.shown = pl.team; pl.team = 0xFF; }
}
long long EvAt(const char* what, long long from) {      // (v150-host beta 22) the frame of the first such event from a frame on
    for (const auto& e : G.events) {
        const long long f = atoll(e.c_str() + 1);
        if (f >= from && e.find(what) != std::string::npos) return f;
    }
    return -1;
}
std::string EvText(const char* what, long long from) {
    for (const auto& e : G.events) if (atoll(e.c_str() + 1) >= from && e.find(what) != std::string::npos) return e;
    return std::string();
}
int Count(const char* what) {
    int n = 0;
    for (const auto& e : G.events) if (e.find(what) != std::string::npos) ++n;
    return n;
}
int gFails = 0;
void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++gFails; }
int AddReal(int team) {                                // a real player connecting (his team 0xFF until he plays)
    const int id = G.NextId();
    G.players[id] = Player{id, 3, team, 0x2000u + (uint32_t)id};
    return id;
}
int TeamOf(int id) { return G.players.count(id) ? G.players[id].team : -1; }
// (v150-host beta 22, relay) relayblast: a real player standing by a doll's claymore (Host::see), the host's copies of the doll's
// claymores gone off (Host::clayBlasts: each id once)
int gVictim = -1;
float gVictimAt[3] = {0, 0, 0};
std::vector<std::pair<uint32_t, std::vector<float>>> gBlastsDue;
bool SeeRelay(int id, vdoll::PlayerNow* out) {
    if (id < 0 || id >= 16 || !G.players.count(id)) return false;
    *out = vdoll::PlayerNow(); out->object = true; out->alive = true; out->life = 200;
    if (id == gVictim) { out->team = G.players[id].team; out->state = 3; memcpy(out->pos, gVictimAt, sizeof out->pos); return true; }
    out->team = G.players[id].team; out->state = gCopyState[id];
    memcpy(out->pos, gCopyAt[id], sizeof out->pos);
    return true;
}
int BlastsT(uint32_t id[], float at[][3], int max) {
    int n = 0;
    for (const auto& b : gBlastsDue) { if (n >= max) break; id[n] = b.first; at[n][0] = b.second[0]; at[n][1] = b.second[1]; at[n][2] = b.second[2]; ++n; }
    gBlastsDue.clear();
    return n;
}
// the gaps between the frames a player's unreliable records were taken, from a frame on: the largest, the mean
void TakenGaps(int slot, long long from, long long* most, double* mean) {
    long long last = -1, sum = 0, n = 0; *most = 0;
    for (long long f : gTakenAt[slot]) {
        if (f < from) continue;
        if (last >= 0) { *most = std::max(*most, f - last); sum += f - last; ++n; }
        last = f;
    }
    *mean = n ? (double)sum / (double)n : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "";
    // each scenario runs in its own process (the module keeps its dolls): ./vdoll_flow_test <scenario>
    if (which.empty()) {
        int fails = 0;
        for (const char* s : {"two", "one", "seats", "spectator", "onelife", "alone", "kick", "nextgame", "dmtotdm", "fullfirst", "class", "classjoin", "onelife2", "prestart", "classway", "classjoin2", "tickets", "roundkick", "tsnewake", "pace", "deathkind", "adaptoff", "adapt", "adaptlive", "livecount", "snerespawn", "snerespawn0", "snetwo", "snakelife", "snakelife0", "snakerule", "snakerule1", "snakerule60", "ghostwait", "ghostwaitslow", "ghostwait0", "ghostwaitjitter", "respawnwait", "respawnwaitslow", "respawnwait0", "respawnwaitjitter", "seqwrap", "seqstep", "claimagain", "blast", "blastfall", "blastfall0", "blastfallreact0", "blastcopy", "flinch", "blastko", "mash", "mashlow", "mashnone", "mash0", "mashslow", "mashslow0", "mashroll", "mashrolllow", "mashroll0", "relay", "relay0", "allywake", "allywake0", "allywakeslow", "stamina", "stamina0", "cqcthrow", "cqcthrow0", "flinchlen", "flinchslow", "flinchjitter", "flinchlen0", "flinchreact0", "mashjitter", "mashbunch", "mashquota", "mashbunch0", "flinchquota", "flinchquota0", "flinchfreeze"}) {   // (movecost, movecost0, movecostclay, movecostko, movecostko0: with a disc map, tests/vdoll_mash_maps.sh)
            const std::string cmd = std::string(argv[0]) + " " + s;
            printf("== %s\n", s);
            fflush(stdout);
            if (system(cmd.c_str()) != 0) ++fails;
        }
        printf("%s\n", fails ? "SOME FAILED" : "ALL PASSED");
        return fails ? 1 : 0;
    }
    if (const char* q = getenv("CLOCK_QUOTA_WORK")) gQuotaWork = std::max(1.0, atof(q));   // ((v150-host beta 25, clockfix) QuotaAt's model)
    if (const char* q = getenv("CLOCK_QUOTA_RUN")) gQuotaRun = std::max(gQuotaWork, std::min(100.0, atof(q)));
    vdoll::Host h;
    h.log = Log; h.gen = Gen; h.player = PlayerOf; h.spawnPoint = Spawn; h.players = Players; h.rule = Rule; h.phase = Phase;
    h.seats = Seats; h.roomChannel = Room;
#ifdef VDOLL_HOST_SCORE
    h.score = Score; h.room = RoomScore;
#endif
    if (which == "two") {
        setenv("MGO1_VDOLLS", "2", 1);
        vdoll::Init(h);
        Run(1200);
        Check(Count("connect") == 2, "two dolls in the game");
        Check(Count("pick") == 0, "no pick with no real player");
        const int r = AddReal(0xFF);
        Run(60);
        G.players[r].kind = 0;                         // in the session, loading (no team yet)
        Run(300);
        Check(Count("pick") == 2, "both picked while the real player loads (two dolls)");
        int t1 = -1, t2 = -1;
        for (auto& [id, pl] : G.players) if (pl.kind == 2) { if (t1 < 0) t1 = pl.team; else t2 = pl.team; }
        Check(t1 >= 0 && t2 >= 0 && t1 != t2, "one doll a side");
        G.players[r].team = SideWithFewer();
        Run(600);
        Check(Count("pick") == 2, "no pick again at his arrival");
    } else if (which == "one") {
        setenv("MGO1_VDOLLS", "1", 1);
        vdoll::Init(h);
        Run(600);
        const int r = AddReal(0xFF);
        Run(60);
        G.players[r].kind = 0;
        Run(600);
        Check(Count("pick") == 0, "a lone doll holds while the real player loads");
        G.players[r].team = 1;
        Run(300);
        Check(Count("pick") == 0, "it holds within 10 s of his team known");
        Run(400);
        Check(Count("pick") == 1 && TeamOf(1) == 0, "then it joins the other side");
    } else if (which == "seats") {
        setenv("MGO1_VDOLLS", "8", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.seats = 8;
        vdoll::Init(h);
        Run(3000);
        Check(Count("connect") == 7, "seven of eight dolls in a game of eight seats (one kept free)");
        const int r = AddReal(0xFF);
        Run(200);
        Check(Count("close") == 1, "a doll leaves when a real player takes the free seat");
        Check((int)G.players.size() == 7, "six dolls and the real player: a seat free again");
        G.players.erase(r);
        Run(1500);
        Check(Count("connect") == 8, "the doll back once the seat is free again");
    } else if (which == "spectator") {
        setenv("MGO1_VDOLLS", "2", 1);
        vdoll::Init(h);
        Run(600);
        const int r = AddReal(0xFF);
        G.players[r].kind = 0;
        Run(400);
        G.players[r].team = 0;
        Run(900);
        Check(Count("pick") == 2, "both in a team with a real player");
        G.players.erase(r);
        Run(1200);
        Check(Count("room 09") == 2 && Count("room 0a") == 2, "both go spectator when the last real player leaves");
        Check(TeamOf(1) == 0xFE && TeamOf(2) == 0xFE, "both spectators");
        const int r2 = AddReal(0xFF);
        G.players[r2].kind = 0;
        Run(300);
        Check(Count("room 0a") == 4, "both back into a team while the new real player loads");
        Check(TeamOf(1) != TeamOf(2) && TeamOf(1) < 2 && TeamOf(2) < 2, "one a side");
    } else if (which == "onelife") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 2;
        vdoll::Init(h);
        Run(900);
        // its life 0 from the host: dead, no respawn in a one-life rule
        Peer& p = G.peers.begin()->second;
        PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
        SendAll(p, G.peers.begin()->first);
        Run(30);
        PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 0, 0});
        SendAll(p, G.peers.begin()->first);
        Run(1200);
        Check(Count("room death") == 0, "no ticket's record in a one-life rule (a team deathmatch's only)");
        Check(Count("kind 1") == 0, "no respawn in a one-life rule");
        Check(Count("kind 2") == 1, "a ghost to the round's end (its spawn of kind 2)");
        G.gen = 1;
        Run(600);
        Check(Count("pick") == 2, "picked again at the next round");
    } else if (which == "alone") {
        setenv("MGO1_VDOLLS", "3", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_KEEP_SEAT", "0", 1);
        G.seats = 3;
        vdoll::Init(h);
        Run(1500);
        Check(Count("connect") == 3 && Count("pick") == 3, "with MGO1_VDOLL_PLAY_ALONE and no seat kept: three dolls in three seats, all picked");
        int n0 = 0, n1 = 0;
        for (auto& [id, pl] : G.players) { if (pl.team == 0) ++n0; if (pl.team == 1) ++n1; }
        Check(n0 == 2 && n1 == 1, "their sides by their slots: red, blue, red");
    }
    if (which == "kick") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        vdoll::Init(h);
        Run(900);
        Peer& p = G.peers.begin()->second;
        Rel(p, 0x62, {0x04, (uint8_t)p.slot});        // the host's kick, on the room's channel
        SendAll(p, G.peers.begin()->first);
        Run(600);
        Check(Count("close") == 1, "kicked: it leaves at once (its close)");
        Run(6000);
        Check(Count("connect") == 1, "not back within 100 s");
        Run(13000);
        Check(Count("connect") == 2, "back 5 minutes later");
    }
    if (which == "nextgame") {
        setenv("MGO1_VDOLLS", "2", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        vdoll::Init(h);
        Run(1500);
        Check(Count("connect") == 2 && Count("pick") == 2, "two dolls in the first game");
        vdoll::HostGameGone(gNow);                     // the host's game ended: a new one, empty
        G.players.clear(); G.peers.clear(); G.gen = 0;
        Run(2000);
        Check(Count("connect") == 4 && Count("pick") == 4, "both join the next game and pick again");
    }
    if (which == "dmtotdm") {
        setenv("MGO1_VDOLLS", "4", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 0;
        vdoll::Init(h);
        Run(1800);
        int units = 0;
        for (auto& [id, pl] : G.players) if (pl.kind == 2 && pl.team == 0) ++units;
        Check(Count("pick") == 4 && units == 4, "a deathmatch: four dolls, each its unit 0");
        NewRound(1);                                   // a team deathmatch next: the session still shows every unit 0
        Run(900);
        int n0 = 0, n1 = 0;
        for (auto& [id, pl] : G.players) { if (pl.team == 0) ++n0; if (pl.team == 1) ++n1; }
        Check(Count("pick") == 8 && n0 == 2 && n1 == 2, "the next round's team deathmatch: two dolls a side (the host's teams of the round, not the session's last)");
    }
    // (v150-host beta 16) the spawn record's fourth byte is the player's class: the one the host's game holds for its copy,
    // else its object's kind; from the spectators the joined team's soldiers'; none sent while it is not known
    // ((v150-host beta 25, respawnwait) a respawn 567 frames after a plain death, as a player's: the waits after each kill from it)
    if (which == "class") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        h.playerClass = ClassOf; h.teamClass = TeamClass;
        G.kind = 2;                                    // a soldier of the Ocelot Unit
        vdoll::Init(h);
        Run(900);
        auto kill = [&](long long frames) {              // its life from the host, then 0: dead; the frames after
            Peer& p = G.peers.begin()->second;
            PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
            SendAll(p, G.peers.begin()->first);
            Run(30);
            PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 0, 0});
            SendAll(p, G.peers.begin()->first);
            Run(frames);
        };
        kill(800);
        Check(Count("kind 1 team 00 class 2") == 1 && Count("kind 1") == 1, "a soldier respawns with its object's class (the Ocelot Unit, 2)");
        G.copyClass = 5;                               // the host's game holds it as a special character now
        kill(800);
        Check(Count("kind 1 team 00 class 5") == 1 && Count("kind 1") == 2, "a special character respawns with the class of the host's copy (5)");
        G.copyClass = -1; G.kind = 0; G.gen = 1;       // the next round: KGB
        Run(600);
        kill(800);
        Check(Count("kind 1 team 00 class 0") == 1 && Count("kind 1") == 3, "the next round's class (KGB, 0)");
        G.kind = 9; G.gen = 2;                         // a class that is none: not known
        Run(600);
        kill(1100);
        Check(Count("room death") == 4 && Count("kind 1") == 3, "no respawn sent while its class is not known");
        G.copyClass = 1;                               // read at last (GRU): the respawn goes
        Run(120);
        Check(Count("kind 1 team 00 class 1") == 1, "its respawn once the class is known (GRU, 1)");
    }
    if (which == "classjoin") {
        setenv("MGO1_VDOLLS", "2", 1);
        h.playerClass = ClassOf; h.teamClass = TeamClass;
        G.kind = 1; G.units[0] = 1; G.units[1] = 2;    // GRU against the Ocelot Unit
        vdoll::Init(h);
        Run(600);
        const int r = AddReal(0xFF);
        G.players[r].kind = 0;
        Run(400);
        G.players[r].team = 0;
        Run(900);
        G.players.erase(r);
        Run(1200);
        Check(Count("kind 3 team fe class 1") == 2, "a spectator's spawn keeps its class");
        const int r2 = AddReal(0xFF);
        G.players[r2].kind = 0;
        Run(300);
        Check(Count("kind 1 team 00 class 1") == 1 && Count("kind 1 team 01 class 2") == 1,
              "from the spectators: the joined team's soldiers' class (red GRU 1, blue the Ocelot Unit 2)");
    }
    // (v150-host beta 16, the review's P1-1 to P1-3) no doll waits a round for a class: its team's soldiers' class, then,
    // where the host's game checks a spawn's character, the last class it had in this game, then its side's default unit
    // after 2 s; where it does not, held as before (the `class` scenario above)
    // ((v150-host beta 25, respawnwait) its respawn's time 567 frames after a plain death: the kills' waits from it)
    if (which == "classway") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        h.playerClass = ClassOf; h.teamClass = TeamClass; h.spawnGuard = Guard;
        G.guard = true;
        G.kind = 9; G.units[0] = 2; G.units[1] = 0;    // an object that tells no class; the Ocelot Unit against KGB
        vdoll::Init(h);
        Run(900);
        auto kill = [&](long long frames) {
            Peer& p = G.peers.begin()->second;
            PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
            SendAll(p, G.peers.begin()->first);
            Run(30);
            PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 0, 0});
            SendAll(p, G.peers.begin()->first);
            Run(frames);
        };
        kill(800);
        Check(Count("kind 1 team 00 class 2") == 1 && Count("kind 1") == 1, "no class known, its team's unit known: a respawn with its team's soldiers' class (2)");
        G.units[0] = G.units[1] = -1; G.gen = 1;       // the next round: the units not known either, it never had a class
        Run(600);
        kill(607);
        Check(Count("room death") == 2 && Count("kind 1") == 1, "nothing known at all: held at first (40 frames after the respawn's time)");
        Run(200);
        Check(Count("kind 1 team 00 class 1") == 1 && Count("kind 1") == 2, "then, 2 s on, its side's default unit (red: GRU, 1)");
        G.kind = 5; G.gen = 2;                         // a round in which its object tells a class
        Run(600);
        kill(800);
        Check(Count("kind 1 team 00 class 5") == 1, "a round with its class known (5)");
        G.kind = 9; G.gen = 3;                         // then one that tells none again
        Run(600);
        kill(597);
        Check(Count("kind 1 team 00 class 5") == 2 && Count("kind 1") == 4, "nothing known this round: the last class it had in this game, at once (5)");
    }
    if (which == "classjoin2") {
        setenv("MGO1_VDOLLS", "2", 1);
        h.playerClass = ClassOf; h.teamClass = TeamClass; h.spawnGuard = Guard;
        G.guard = true;
        G.kind = 1; G.units[0] = 1; G.units[1] = 2;    // GRU against the Ocelot Unit
        vdoll::Init(h);
        Run(600);
        const int r = AddReal(0xFF);
        G.players[r].kind = 0;
        Run(400);
        G.players[r].team = 0;
        Run(900);
        G.players.erase(r);
        Run(1200);
        Check(Count("kind 3 team fe") == 2, "both spectators");
        ++G.gen; G.units[0] = G.units[1] = -1;         // a new round while they are spectators: no object, and no unit known
        Run(300);
        const int r2 = AddReal(0xFF);
        G.players[r2].kind = 0;
        Run(300);
        Check(Count("kind 1 team 00 class") + Count("kind 1 team 01 class") == 2, "from the spectators with nothing told this round: both join (none held)");
        Check(Count("kind 1 team 00 class 1") + Count("kind 1 team 01 class 1") == 2, "each with the last class it had in this game (GRU, 1: the red one's, and the blue one's object of the first round)");
        // the class of a join is kept for its next respawn, with no guess allowed any more
        G.guard = false;
        Run(120);
        Peer& p = G.peers.begin()->second;
        const int spawns = Count("kind 1 team");
        PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
        SendAll(p, G.peers.begin()->first);
        Run(30);
        PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 0, 0});
        SendAll(p, G.peers.begin()->first);
        Run(800);                                        // ((v150-host beta 25, respawnwait) its respawn 567 frames after a plain death)
        Check(Count("kind 1 team") == spawns + 1 && Count("class 1") >= 3, "its next respawn tells the class it joined with (not held)");
    }
    // (the configurations' audit) spawned before the round's start (the room's phase 1): no deploy until it starts, as a
    // player's game; a phase 1 that never ends: deployed 10 s later all the same
    if (which == "prestart") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.phase = 1;
        vdoll::Init(h);
        Run(500);
        const int sl = G.peers.empty() ? -1 : G.peers.begin()->second.slot;
        char spawned[32], deployed[32];
        snprintf(spawned, sizeof spawned, "state %d 1", sl); snprintf(deployed, sizeof deployed, "state %d 0", sl);
        Check(Count("pick") == 1 && Count(spawned) == 1 && Count(deployed) == 0, "spawned (state 1), not deployed while the round has not started");
        G.phase = 2;
        Run(30);
        Check(Count(deployed) == 1, "deployed at the round's start");
        NewRound(1); G.phase = 1;
        Run(500);
        Check(Count("pick") == 2 && Count(spawned) == 2 && Count(deployed) == 1, "the next round: held again before its start");
        Run(700);
        Check(Count(deployed) == 2, "a phase 1 that never ends: deployed 10 s later");
    }
    if (which == "onelife2") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 2; G.kind = 4;                        // rescue, a special character
        vdoll::Init(h);
        Run(900);
        Peer& p = G.peers.begin()->second;
        PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
        SendAll(p, G.peers.begin()->first);
        Run(30);
        PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 0, 0});
        SendAll(p, G.peers.begin()->first);
        Run(1200);
        Check(Count("kind 2 team 00 class 4") == 1 && Count("kind 1") == 0, "a ghost's spawn keeps its class (4)");
    }
    // (v150-host beta 21) SNEAKING: a soldier (the red side) comes back about 10 s after each death, as a player's game spawns
    // its soldier again (a playable doll's own game: 9.8 to 10.3 s), each time, at its side's spawn; Snake (the blue side) has
    // one life (a ghost if the round goes on); MGO1_VDOLL_SNE_RESPAWN=0: beta 20's ghost at the first death. Fails on beta 20.
    // ((v150-host beta 25, respawnwait) the wait a player's: 567 frames after a plain death, the respawnwait scenario)
    if (which == "snerespawn" || which == "snerespawn0") {
        const bool off = which == "snerespawn0";
        if (off) setenv("MGO1_VDOLL_SNE_RESPAWN", "0", 1);
        setenv("MGO1_VDOLLS", "2", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_TEAMS", "red,blue", 1);
        G.rule = 4;
        vdoll::Init(h);
        Run(1500);
        Check(Count("pick") == 2 && TeamOf(1) == 0 && TeamOf(2) == 1, "sneaking: a soldier (red) and Snake (blue)");
        auto kill = [&](int slot) {                      // its life from the host, then 0: dead (the frame returned)
            for (auto& [ip, p] : G.peers) {
                if (p.slot != slot) continue;
                PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
                SendAll(p, ip);
                Run(30);
                PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 0, 0});
                SendAll(p, ip);
                break;
            }
            return gNow;
        };
        auto spawnAt = [&](const char* what, long long after) {   // the frame of the first such spawn event after a frame
            for (const auto& e : G.events) {
                const long long f = atoll(e.c_str() + 1);
                if (f > after && e.find(what) != std::string::npos) return f;
            }
            return -1LL;
        };
        bool timed = true, placed = true;
        int back = 0;
        std::vector<int> points;                         // the soldiers' points taken (0 to 6), in order
        const int deaths = off ? 3 : 7;
        for (int death = 0; death < deaths; ++death) {
            gNoPoints = death == 6;                      // (the seventh death: the map's points not read yet)
            const long long at = kill(1);
            Run(540);
            const bool early = spawnAt("spawn 1 kind", at) >= 0;
            Run(240);
            const long long f = spawnAt("spawn 1 kind 1 team 00", at);
            if (f >= 0) ++back;
            // ((v150-host beta 25, respawnwait) a player's soldier after a plain death: his spawn 564 to 570 frames after his death
            // record (beta 21 to 25: 600 frames)
            if (early || f < 0 || f - at < 564 || f - at > 580) timed = false;
            // (the stand-in host: the soldiers' list at 20000 + 1000 k, 0, -30000; player 1's start of the round at 1000 800
            // -500; the red team's team deathmatch point at 0 0 0)
            if (f >= 0 && !gLog.empty()) {
                std::string last;
                for (const auto& l : gLog) if (l.find("vdoll 1: respawns at ") != std::string::npos) last = l;
                float x = 0, y = 0, z = 0;
                if (sscanf(last.c_str(), "vdoll 1: respawns at %f %f %f", &x, &y, &z) != 3) placed = false;
                else if (death < 6) {
                    const int k = (int)lrintf((x - 20000.f) / 1000.f);
                    if (k < 0 || k > 6 || x != 20000.f + 1000.f * (float)k || y != 800.f || z != -30000.f) placed = false;
                    points.push_back(k);
                } else if (x != 1000.f || y != 800.f || z != -500.f) placed = false;
            }
            Run(300);
        }
        if (!off) {
            int distinct = 0;
            for (int k = 0; k < 7; ++k) if (std::find(points.begin(), points.end(), k) != points.end()) ++distinct;
            printf("  (the soldiers' points taken:");
            for (int k : points) printf(" %d", k);
            printf(")\n");
            Check(back == 7 && Count("spawn 1 kind 2") == 0, "a SNEAKING soldier comes back after each of its 7 deaths, never a ghost");
            Check(timed, "each time 9.5 s after its death (564 to 580 frames), as a player's soldier after a plain death, not sooner");
            Check(placed && points.size() == 6, "at a point of the stage's soldiers' list (the host's points for SNEAKING's red side), never a team deathmatch point; the points not read: its round's start place");
            Check(distinct >= 3, "not always the same point (taken at random, as the game's)");
            Check(LogCount("out until the round's end") == 0, "no 'one life' said for a soldier");
        } else {
            Check(back == 0 && Count("spawn 1 kind 2") == 1, "MGO1_VDOLL_SNE_RESPAWN=0: a ghost to the round's end at its first death (beta 20)");
        }
        const long long at = kill(2);
        Run(900);
        // ((v150-host beta 25, snakeghost) never a ghost: a player's Snake is none, his death ending the round; beta 24: a ghost)
        Check(spawnAt("spawn 2 kind 1", at) < 0 && spawnAt("spawn 2 kind 2", at) < 0, "Snake: one life (no respawn, and no ghost, as a player's Snake)");
        G.rule = 5; G.gen = 1;                          // TEAM SNEAKING next: one life for both sides
        Run(900);
        const long long t1 = kill(1);
        Run(900);
        Check(spawnAt("spawn 1 kind 1", t1) < 0 && spawnAt("spawn 1 kind 2", t1) >= 0, "TEAM SNEAKING: one life, a ghost (unchanged)");
    }
    // (v150-host beta 21) SNEAKING: two soldiers killed in the same frame, 20 times: each time both come back, never at the
    // same point of the soldiers' list (a point another doll took within 2 s is passed over)
    if (which == "snetwo") {
        setenv("MGO1_VDOLLS", "3", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_TEAMS", "red,red,blue", 1);
        G.rule = 4;
        vdoll::Init(h);
        Run(1800);
        Check(Count("pick") == 3 && TeamOf(1) == 0 && TeamOf(2) == 0 && TeamOf(3) == 1, "sneaking: two soldiers (red) and Snake (blue)");
        int both = 0, same = 0;
        for (int death = 0; death < 20; ++death) {
            for (int life : {200, 0}) {
                for (auto& [ip, p] : G.peers) {
                    if (p.slot != 1 && p.slot != 2) continue;
                    PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, (uint8_t)life, 0});
                    SendAll(p, ip);
                }
                if (life) Run(30);
            }
            const size_t from = gLog.size();
            Run(900);
            float at[3][3] = {}; int n[3] = {0, 0, 0};
            for (size_t i = from; i < gLog.size(); ++i) {
                int k = 0; float x, y, z;
                if (sscanf(gLog[i].c_str(), "vdoll %d: respawns at %f %f %f", &k, &x, &y, &z) == 4 && k >= 1 && k <= 2) { at[k][0] = x; at[k][1] = y; at[k][2] = z; ++n[k]; }
            }
            if (n[1] == 1 && n[2] == 1) ++both;
            if (n[1] && n[2] && at[1][0] == at[2][0] && at[1][2] == at[2][2]) ++same;
        }
        Check(both == 20, "both soldiers come back after each of 20 deaths together");
        Check(same == 0, "never at the same point");
    }
    // (v150-host beta 25, snakeghost) SNEAKING's Snake is never a ghost ("Snake comes back as ghost in snk", 17 November). His
    // death ends the round (the room's phase 2 -> 4 a few frames after it); a map's last round has results of 1090 to 1140
    // frames (the owner's logs and the benches), longer than the 630 frames after which a virtual doll Snake sent its ghost
    // spawn: he came back as a ghost on every screen. A real client's Snake lies dead through them (104 deaths measured, no ghost,
    // no spawn record to the next round's). snakelife: Snake killed, the round over 7 frames later, 1130 frames of results: no
    // spawn of his, his death ticks to their end, a soldier killed meanwhile still back (the game's soldiers come back in the
    // results too); the next round: Snake again, in play; then Snake killed in the results of a round already over (a goal, the
    // time): no ghost either. snakelife0: MGO1_VDOLL_SNAKE_LIFE=0 and MGO1_VDOLL_GHOST_WAIT=0, beta 24's way (his ghost 630 frames after his death)
    if (which == "snakelife" || which == "snakelife0") {
        const bool off = which == "snakelife0";
        if (off) setenv("MGO1_VDOLL_SNAKE_LIFE", "0", 1);
        // ((v150-host beta 25, ghostwait) beta 24's way whole: its ghost's wait b16's 630 frames too, MGO1_VDOLL_GHOST_WAIT=0; with
        // that switch on, a ghost comes at a player's wait for the kind of death told: the ghostwait scenarios)
        if (off) setenv("MGO1_VDOLL_GHOST_WAIT", "0", 1);
        setenv("MGO1_VDOLLS", "3", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_TEAMS", "red,red,blue", 1);
        G.rule = 4; G.phase = 2;
        vdoll::Init(h);
        Run(1800);
        Check(Count("pick") == 3 && TeamOf(1) == 0 && TeamOf(2) == 0 && TeamOf(3) == 1, "sneaking: two soldiers (red) and Snake (blue)");
        auto life = [&](int slot, int v) {               // its life from the host (the {05} record on its first channel)
            for (auto& [ip, p] : G.peers) {
                if (p.slot != slot) continue;
                PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, (uint8_t)v, 0});
                SendAll(p, ip);
            }
        };
        auto spawnAfter = [&](const char* what, long long after) {
            for (const auto& e : G.events) { const long long f = atoll(e.c_str() + 1); if (f > after && e.find(what) != std::string::npos) return f; }
            return -1LL;
        };
        life(1, 200); life(3, 200);
        Run(60);
        life(1, 0);                                      // a soldier killed 300 frames before Snake: he comes back in the results
        const long long soldierDead = gNow;
        Run(300);
        life(3, 0);                                      // Snake killed
        const long long snakeDead = gNow;
        Run(8);
        G.phase = 4;                                     // his death ends the round: the results
        Run(1130);
        const long long resultsEnd = gNow;
        const long long ghostAt = spawnAfter("spawn 3 kind 2", snakeDead);
        printf("  (Snake's spawn after his death: kind 2 at %lld frames, kind 1 at %lld; his last death record %lld frames before the results' end)\n",
               ghostAt < 0 ? -1 : ghostAt - snakeDead, spawnAfter("spawn 3 kind 1", snakeDead) < 0 ? -1 : spawnAfter("spawn 3 kind 1", snakeDead) - snakeDead,
               resultsEnd - gDeathRecLast[3]);
        if (!off) {
            Check(ghostAt < 0 && spawnAfter("spawn 3 kind 1", snakeDead) < 0, "Snake killed, the round over: no ghost, no respawn through 1130 frames of results (a player's Snake)");
            Check(resultsEnd - gDeathRecLast[3] <= 12, "his death told every 100 ms to the results' end, as a player's Snake's game tells it");
            Check(LogCount("vdoll 3: no ghost: SNEAKING's Snake lies dead") == 1 && LogCount("vdoll 3: a ghost") == 0, "the line 'no ghost' once, no ghost line");
        } else {
            Check(ghostAt >= snakeDead + 624 && ghostAt <= snakeDead + 642, "MGO1_VDOLL_SNAKE_LIFE=0: his ghost 630 frames after his death, in the results (beta 24)");
        }
        const long long back = spawnAfter("spawn 1 kind 1 team 00", soldierDead);
        Check(back >= soldierDead + 570 && back <= soldierDead + 660 && Count("spawn 1 kind 2") == 0, "a soldier killed before still comes back 10 s after his death, in the results (unchanged)");
        NewRound(4); G.phase = 2;                        // the next round
        const long long next = gNow;
        Run(900);
        Check(TeamOf(3) == 1 && spawnAfter("spawn 3 kind 2", next) < 0 && LogCount("vdoll 3: deployed") >= 2, "the next round: Snake again, in play, no ghost");
        if (!off) {
            life(3, 200);
            Run(60);
            G.phase = 4;                                 // the round over (a goal, the time), then Snake killed in its results
            Run(120);
            life(3, 0);
            const long long late = gNow;
            Run(1130);
            Check(spawnAfter("spawn 3 kind", late) < 0, "Snake killed in the results of a round already over: no ghost either");
        }
    }
    // (v150-host beta 25, snakeghost, review S1) the owner's rotation: SNEAKING's last round, then a deathmatch. The room's rule
    // may read the next entry's a few frames before the round byte moves on (the owner's logs: the old rule still read 2 and 12
    // frames before it; a test of the rule in DEAD would respawn the dead Snake at once, a kind-1 spawn on every screen). The
    // dead Snake doll must send nothing in those frames: snakerule 3 frames early, snakerule1 1, snakerule60 60 (fails on
    // snakeghost's first fix, a69b048, which tested the rule each frame in DEAD)
    if (which == "snakerule" || which == "snakerule1" || which == "snakerule60") {
        const int lead = which == "snakerule1" ? 1 : which == "snakerule60" ? 60 : 3;
        setenv("MGO1_VDOLLS", "3", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_TEAMS", "red,red,blue", 1);
        G.rule = 4; G.phase = 2;
        vdoll::Init(h);
        Run(1800);
        auto life = [&](int slot, int v) {
            for (auto& [ip, p] : G.peers) {
                if (p.slot != slot) continue;
                PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, (uint8_t)v, 0});
                SendAll(p, ip);
            }
        };
        auto after = [&](const char* what, long long from) {
            for (const auto& e : G.events) { const long long f = atoll(e.c_str() + 1); if (f > from && e.find(what) != std::string::npos) return f; }
            return -1LL;
        };
        life(3, 200);
        Run(60);
        life(3, 0);                                      // Snake killed, the round over
        const long long dead = gNow;
        Run(8);
        G.phase = 4;
        Run(1100);                                       // the results of the entry's last round
        G.rule = 0;                                      // the room's rule: the next entry's (deathmatch), the round byte not yet
        const long long sw = gNow;
        Run(lead);
        const long long k1 = after("spawn 3 kind 1", sw), k2 = after("spawn 3 kind 2", dead);
        printf("  (the rule read as deathmatch %d frames before the new round: Snake's spawn kind 1 at %lld, kind 2 at %lld frames after his death)\n",
               lead, k1 < 0 ? -1 : k1 - dead, k2 < 0 ? -1 : k2 - dead);
        Check(k1 < 0, "no respawn of the dead Snake in the frames the room's rule reads the next entry's before the new round");
        Check(k2 < 0, "no ghost of the dead Snake in the results");
        NewRound(0); G.phase = 2;
        Run(600);
    }
    // (v150-host beta 25, ghostwait) a one-life player's game sends his ghost spawn 5 s after his death's motion, whose length is
    // set by the kind of death it tells: from his first death record, a head shot (kind 2) 390 to 396 frames, a body kill (kind 1)
    // 564 to 570 (1398 deaths of real clients, docs/VIRTUAL_DOLL.md 54); the round's end changes nothing. A doll sent its ghost
    // 630 frames after its death whatever the death. ghostwait: TEAM SNEAKING, a doll killed by a head hit, the other by a body
    // hit: their ghosts 393 and 567 frames after their death records (fails on b16 88b4417: 630 both); a head shot as its round
    // ends and a body kill in the results: 393 and 567 again, in the results; a new round before the wait: no ghost; rescue the
    // same. ghostwaitslow: the host at 38 frames a real second: the same real seconds as a player (249 and 359 host frames, b16:
    // 630). ghostwait0: MGO1_VDOLL_GHOST_WAIT=0, b16's 630 frames whatever the death
    // ((v150-host beta 25, respawnwait) ghostwaitjitter: the host's ticks uneven at full pace: the same frames as a player's (fails on
    // ghostwait's step by step clock: the ghosts about a quarter early)
    if (which == "ghostwait" || which == "ghostwaitslow" || which == "ghostwait0" || which == "ghostwaitjitter") {
        const bool off = which == "ghostwait0", slow = which == "ghostwaitslow";
        if (off) setenv("MGO1_VDOLL_GHOST_WAIT", "0", 1);
        if (slow) h.realMs = SlowMs;
        if (which == "ghostwaitjitter") h.realMs = JitterMs;
        setenv("MGO1_VDOLLS", "2", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_TEAMS", "red,blue", 1);
        G.rule = 5; G.phase = 2;
        h.objectives = Objectives;
        vdoll::Init(h);
        Run(1500);
        Check(Count("pick") == 2, "team sneaking: two dolls in the round");
        std::vector<int> slots;
        for (auto& [ip, p] : G.peers) slots.push_back(p.slot);
        std::sort(slots.begin(), slots.end());
        const int A = slots[0], B = slots[1];
        auto send = [&](int slot, std::vector<uint8_t> rec) {
            for (auto& [ip, p] : G.peers) if (p.slot == slot) { PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, rec); SendAll(p, ip); }
        };
        auto at = [&](const std::string& what, long long from) { return EvAt(what.c_str(), from); };
        // its life 200, a hit {04 attacker, damage, stun, weapon, ?, head} (player 9's AK-47), then its life 0 from the host
        auto kill = [&](int slot, bool head) {
            send(slot, {0x05, 200, 0});
            Run(30);
            send(slot, {0x04, 9, 200, 0, 0, 0, 0x0C, 0, (uint8_t)(head ? 1 : 0)});
            Run(4);
            const long long from = gNow;
            send(slot, {0x05, 0, 0});
            return from;
        };
        // the frames from its death (its line, the frame of its first death record) to its ghost (its line, the frame of its
        // spawn of kind 2): the first death after a frame; -1 none. Its kind told: its line says a head shot (2) or not (1)
        auto wait = [&](long long from, int* kind) {
            long long d = -1; std::string who;
            for (size_t i = 0; i < gLog.size(); ++i)
                if (gLogF[i] >= from && gLog[i].find(": dead, by player") != std::string::npos) {
                    d = gLogF[i]; who = gLog[i].substr(0, gLog[i].find(':') + 1);
                    if (kind) *kind = gLog[i].find(", a head shot") != std::string::npos ? 2 : 1;
                    break;
                }
            if (d < 0) return -1LL;
            for (size_t i = 0; i < gLog.size(); ++i)
                if (gLogF[i] >= d && gLog[i].rfind(who + " a ghost to the round's end", 0) == 0) return gLogF[i] - d;
            return -1LL;
        };
        // no spawn of its in play between two frames (no respawn in a one-life rule)
        auto noRespawn = [&](int slot, long long from, long long to) {
            const long long f = at("spawn " + std::to_string(slot) + " kind 1", from);
            return f < 0 || f >= to;
        };
        const long long head = off ? 630 : slow ? 249 : 393, body = off ? 630 : slow ? 359 : 567;
        const char* how = off ? "MGO1_VDOLL_GHOST_WAIT=0: b16's 630 frames" : slow ? "on a slowed host (38 frames a second) the same real seconds as a player's" : "as a player's game";
        auto near = [&](long long w, long long want) { return w >= want - 4 && w <= want + 6; };
        char m[200];
        // 1. in play: a head shot, a body kill
        long long fa = kill(A, true);
        long long fb = kill(B, false);
        Run(900);
        int ka = -1, kb = -1;
        long long wa = wait(fa, &ka), wb = wait(fb, &kb);
        printf("  (team sneaking in play: the head shot's ghost %lld frames after its death record (kind %d), the body kill's %lld (kind %d))\n", wa, ka, wb, kb);
        Check(ka == 2 && kb == 1, "the kinds told: 2 for the head shot, 1 for the body kill");
        snprintf(m, sizeof m, "a head shot's ghost %lld frames after the death (%s)", head, how); Check(near(wa, head), m);
        snprintf(m, sizeof m, "a body kill's ghost %lld frames after the death (%s)", body, how); Check(near(wb, body), m);
        Check(noRespawn(A, fa, gNow) && noRespawn(B, fb, gNow), "no respawn of either in a one-life rule");
        // 2. the next round: a head shot as its round ends, a body kill in the results
        NewRound(5); G.phase = 2;
        Run(900);
        fa = kill(A, true);
        Run(8);
        G.phase = 4;                                     // the round over 8 frames after its death: the results
        fb = kill(B, false);
        Run(1100);
        wa = wait(fa, nullptr); wb = wait(fb, nullptr);
        printf("  (team sneaking at the round's end: %lld, killed in the results: %lld)\n", wa, wb);
        snprintf(m, sizeof m, "killed as the round ends: its ghost in the results, %lld frames after (the round's end changes nothing)", head); Check(near(wa, head), m);
        snprintf(m, sizeof m, "killed in the results: its ghost %lld frames after", body); Check(near(wb, body), m);
        Check(noRespawn(A, fa, gNow) && noRespawn(B, fb, gNow), "no respawn in the results");
        // 3. a new round before the wait is over: no ghost, in play again
        NewRound(5); G.phase = 2;
        Run(900);
        fa = kill(A, true);
        Run(200);
        NewRound(5); G.phase = 2;
        const long long nr = gNow;
        Run(900);
        Check(at("spawn " + std::to_string(A) + " kind 2", fa) < 0 && LogCount(": deployed at") >= 8 && noRespawn(A, fa, nr),
              "a new round before the wait is over: no ghost, in play again by the round");
        // 4. rescue: a body kill, a head shot
        NewRound(2); G.phase = 2;
        Run(900);
        fa = kill(A, false);
        fb = kill(B, true);
        Run(900);
        wa = wait(fa, &ka); wb = wait(fb, &kb);
        printf("  (rescue: the body kill's ghost %lld (kind %d), the head shot's %lld (kind %d))\n", wa, ka, wb, kb);
        snprintf(m, sizeof m, "rescue: a body kill's ghost %lld frames after, a head shot's %lld", body, head);
        Check(ka == 1 && kb == 2 && near(wa, body) && near(wb, head), m);
        Check(noRespawn(A, fa, gNow) && noRespawn(B, fb, gNow), "rescue: no respawn");
        if (!off) Check(LogCount("frames after its death (") == 6 && LogCount("its death's kind 2)") == 3 && LogCount("its death's kind 1)") == 3, "each ghost's line says its wait and its kind");
    }
    // (v150-host beta 25, respawnwait) a player's respawn in the rules with one is the ghost's timer: his spawn record 390 to 396
    // frames after his first death record for a head shot (kind 2), 564 to 570 after a plain death (1), 420 to 426 killed lying (4
    // after a roll's or a CQC knock-down, 5 thrown by a blast), in capture 1 s more for a player whose team has more players than the
    // other (n003a 0x409700); then his weapons page (state 1, 18 frames) and in play 21 frames after his spawn at the soonest (docs/
    // VIRTUAL_DOLL.md 56). A doll respawned 300 frames after its death (390 in capture and base capture, 600 a SNEAKING soldier) whatever
    // the death, told 2 or 1, deployed 18 frames later. respawnwait: three dolls (red, red, blue) killed by a head hit, a body hit, after
    // a roll's knock-down, after a blast's throw, knocked out then hit in the head, in deathmatch, team deathmatch, capture (the two red
    // dolls the bigger team: 1 s more, the blue one not), base capture (no second more) and SNEAKING (a soldier); each spawn 393, 567,
    // 423 (453 for capture's bigger team) frames after its death line, its kind told 2, 1, 4, 5, 2; deployed 18 frames after its spawn
    // and free at 21 (fails on b16 72e9a31: 300, 390 or 600 every time, kinds 2 and 1 only). respawnwaitslow: the host at 38 frames a real
    // second: the same real seconds (249 and 359 host frames). respawnwait0: MGO1_VDOLL_RESPAWN_WAIT=0 and MGO1_VDOLL_DEATH_KIND=0, b16's
    // frames and kinds
    // respawnwaitjitter: the host at full pace with its ticks uneven (a pass 15 ms, the next 51): a player's frames all the same (fails on
    // c5ee42c's step by step clock, ghostwait's: each step's longer of its frames and its ms made the dolls' spawns a quarter early)
    if (which == "respawnwait" || which == "respawnwaitslow" || which == "respawnwait0" || which == "respawnwaitjitter") {
        const bool off = which == "respawnwait0", slow = which == "respawnwaitslow";
        if (off) { setenv("MGO1_VDOLL_RESPAWN_WAIT", "0", 1); setenv("MGO1_VDOLL_DEATH_KIND", "0", 1); }
        if (slow) h.realMs = SlowMs;
        if (which == "respawnwaitjitter") h.realMs = JitterMs;
        setenv("MGO1_VDOLLS", "3", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_TEAMS", "red,red,blue", 1);
        setenv("MGO1_VDOLL_DEBUG", "1", 1);
        G.rule = 0; G.phase = 2;
        h.objectives = Objectives;
        vdoll::Init(h);
        Run(1500);
        Check(Count("pick") == 3, "three dolls in the game");
        auto peerOf = [&](int slot) -> Peer* { for (auto& [ip, p] : G.peers) if (p.slot == slot) return &p; return nullptr; };
        auto send = [&](int slot, int chan, std::vector<uint8_t> rec) {
            for (auto& [ip, p] : G.peers) if (p.slot == slot) { PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + chan), 0, -1, rec); SendAll(p, ip); }
        };
        // its life 200; then how it dies (player 9's AK-47 {04 attacker, damage, stun, weapon, ?, head}; a reaction {09 attacker, kind,
        // angle}: 9 a roll's knock-down, 8 a blast's throw; a stun that empties its stamina); its life 0 from the host
        enum How { HEAD, BODY, ROLL, BLAST, KOHEAD };
        auto kill = [&](int slot, How how) {
            send(slot, 0, {0x05, 200, 0});
            Run(30);
            if (how == ROLL) { send(slot, 1, {0x09, 9, 9, 0x00, 0x00}); Run(20); }
            if (how == BLAST) { send(slot, 1, {0x09, 9, 8, 0x00, 0x00}); Run(20); }
            if (how == KOHEAD) { send(slot, 0, {0x04, 9, 0, 0, 0x30, 0x75, 0x0C, 0, 0}); Run(20); }
            send(slot, 0, {0x04, 9, 200, 0, 0, 0, 0x0C, 0, (uint8_t)(how == HEAD || how == KOHEAD ? 1 : 0)});
            Run(4);
            const long long from = gNow;
            send(slot, 0, {0x05, 0, 0});
            return from;
        };
        // from its death line (the frame of its first death record) to its respawn line (its spawn record) and its deploy line; its
        // kind told (the stand-in's death record event)
        struct Way { long long spawn = -1, deploy = -1, free = -1; int kind = -1; };
        auto way = [&](int slot, long long from) {
            Way w; long long d = -1; std::string who = "vdoll ";
            for (size_t i = 0; i < gLog.size(); ++i)
                if (gLogF[i] >= from && gLog[i].find(": dead, by player") != std::string::npos) {
                    const std::string me = gLog[i].substr(0, gLog[i].find(':') + 1);
                    // (the doll of that slot: its 'in the match, player <slot + 1>' line)
                    char tag[64]; snprintf(tag, sizeof tag, "in the match, player %d ", slot + 1);
                    bool mine = false;
                    for (const auto& l : gLog) if (l.rfind(me, 0) == 0 && l.find(tag) != std::string::npos) mine = true;
                    if (!mine) continue;
                    d = gLogF[i]; who = me; break;
                }
            if (d < 0) return w;
            for (size_t i = 0; i < gLog.size(); ++i) {
                if (gLogF[i] < d || gLog[i].rfind(who, 0) != 0) continue;
                if (w.spawn < 0 && gLog[i].find(" respawns at ") != std::string::npos) w.spawn = gLogF[i] - d;
                else if (w.spawn >= 0 && w.deploy < 0 && gLog[i].find(" deployed at ") != std::string::npos) w.deploy = gLogF[i] - d - w.spawn;
                else if (w.deploy >= 0 && w.free < 0 && gLog[i].find(" free, ") != std::string::npos) w.free = gLogF[i] - d - w.spawn;
            }
            if (gDeathRecLast[slot] >= d) w.kind = gDeathKindLast[slot];
            return w;
        };
        auto near = [&](long long w, long long want) { return w >= want - 4 && w <= want + 6; };
        auto f2 = [&](long long frames) { return slow ? (long long)llround((double)frames * 38.0 / 59.94) : frames; };
        const long long kHead = off ? 300 : f2(393), kBody = off ? 300 : f2(567), kLying = off ? 300 : f2(423);
        const long long kPage = off ? 18 : f2(18), kFree = off ? -1 : f2(21);
        const int kRoll = off ? 1 : 4, kBlast = off ? 1 : 5;
        std::vector<int> slots;
        for (auto& [ip, p] : G.peers) slots.push_back(p.slot);
        std::sort(slots.begin(), slots.end());
        const int A = slots[0], B = slots[1], C = slots[2];   // (red, red, blue in the rules with teams)
        char m[240];
        int bad = 0;
        auto one = [&](const char* rule, int slot, How how, long long want, int kind) {
            const long long f = kill(slot, how);
            Run(800);
            const Way w = way(slot, f);
            const bool ok = near(w.spawn, want) && w.kind == kind && near(w.deploy, kPage) && (kFree < 0 ? w.free < 0 : near(w.free, kFree));
            snprintf(m, sizeof m, "%s, %s: its spawn %lld frames after its death (want %lld), its kind %d (want %d), deployed %lld frames after its spawn (want %lld), free at %lld (want %lld)",
                     rule, how == HEAD ? "a head hit" : how == BODY ? "a body hit" : how == ROLL ? "after a roll's knock-down" : how == BLAST ? "after a blast's throw" : "knocked out, then a head hit",
                     w.spawn, want, w.kind, kind, w.deploy, kPage, w.free, kFree);
            Check(ok, m);
            if (!ok) ++bad;
            Run(300);
        };
        // deathmatch: every way to die
        one("deathmatch", A, HEAD, kHead, 2);
        one("deathmatch", A, BODY, kBody, 1);
        one("deathmatch", B, ROLL, kLying, kRoll);
        one("deathmatch", C, BLAST, kLying, kBlast);
        one("deathmatch", A, KOHEAD, kHead, 2);
        // team deathmatch
        NewRound(1); G.phase = 2; Run(1200);
        one("team deathmatch", A, HEAD, kHead, 2);
        one("team deathmatch", C, BODY, kBody, 1);
        // capture: the red team two players, the blue one: a red doll 1 s more, the blue one not
        NewRound(3); G.phase = 2; Run(1200);
        Check(TeamOf(A) == 0 && TeamOf(B) == 0 && TeamOf(C) == 1, "capture: red, red and blue");
        one("capture, the bigger team", A, HEAD, off ? 390 : kHead + f2(60), 2);
        one("capture, the bigger team", B, BODY, off ? 390 : kBody + f2(60), 1);
        one("capture, the smaller team", C, HEAD, off ? 390 : kHead, 2);
        // base capture: no second more
        NewRound(6); G.phase = 2; Run(1200);
        one("base capture", A, HEAD, off ? 390 : kHead, 2);
        // SNEAKING: a red doll is a soldier (it comes back)
        NewRound(4); G.phase = 2; Run(1200);
        if (TeamOf(A) == 0) one("SNEAKING, a soldier", A, BODY, off ? 600 : kBody, 1);
        else if (TeamOf(B) == 0) one("SNEAKING, a soldier", B, BODY, off ? 600 : kBody, 1);
        else Check(false, "SNEAKING: a soldier doll");
        if (!off) Check(LogCount("respawns at ") >= 12 && LogCount("its death's kind 4") >= 1 && LogCount("its death's kind 5") >= 2 && LogCount("1 s more: its team the bigger") == 2,
                        "each respawn's line says its wait, its kind and capture's second");
        (void)peerOf;
        printf("  (%d of the ways back not a player's)\n", bad);
    }
    // (v150-host beta 21) a session's datagrams are numbered with 16 bits, as the game numbers them: the host's game goes on
    // reading a doll after its 32768th datagram (until beta 20 it read every later one as a compressed datagram: no
    // acknowledgment, no claim, no pick; the owner's logs: every doll's session dead every 70 to 210 minutes, held 180 s as
    // "loading" when that met a round's start). seqwrap: 32768 datagrams and more, one by one (fails on beta 20); seqstep:
    // the test switch MGO1_VDOLL_TEST_SEQ_STEP=16, past the 65536th number (the 16 bits roll over)
    if (which == "seqwrap" || which == "seqstep") {
        const bool step = which == "seqstep";
        if (step) setenv("MGO1_VDOLL_TEST_SEQ_STEP", "16", 1);
        setenv("MGO1_VDOLLS", "2", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_TEAMS", "red,blue", 1);
        G.rule = 1;
        vdoll::Init(h);
        Run(1500);
        Check(Count("pick") == 2, "two dolls in a team deathmatch");
        auto sent = [&]() { long n = 1L << 30; for (auto& [ip, p] : G.peers) n = std::min(n, p.dgrams); return n; };
        auto number = [&]() { unsigned n = 0xFFFF; for (auto& [ip, p] : G.peers) n = std::min<unsigned>(n, p.expect); return n; };
        long long guard = 0;
        if (!step) while (sent() < 33500 && guard++ < 400) Run(1200);          // (about 200000 frames: 10 datagrams a second)
        else {                                                                  // past 0x8000, then past 0xFFFF back to a small number
            while (number() < 0x9000 && guard++ < 400) Run(600);
            Check(number() >= 0x9000, "the step switch: the numbers past 32768 within a few thousand frames");
            while (number() >= 0x8000 && guard++ < 800) Run(600);
            Run(1200);
        }
        long packed = 0, far = 0;
        for (auto& [ip, p] : G.peers) { packed += p.packed; far += p.far; }
        for (auto& [ip, p] : G.peers) printf("  peer %08x slot %d: %ld datagrams, awaited %u, packed %ld, far %ld\n", ip, p.slot, p.dgrams, p.expect, p.packed, p.far);
        printf("  (%ld datagrams of each doll at least, the number awaited now %u; %ld read as compressed, %ld numbered too far)\n", sent(), number(), packed, far);
        Check(step || sent() >= 33500, "each doll sent more than 32768 datagrams");
        Check(packed == 0 && far == 0, step ? "past the 65536th number: every datagram read (the 16 bits roll over as the game's)"
                                           : "after its 32768th datagram the host's game still reads the doll (none taken for a compressed datagram)");
        const int picks = Count("pick");
        NewRound(1);
        Run(900);
        Check(Count("pick") == picks + 2, "the next round: both claims answered, both picks taken");
        Check(LogCount("joins again") == 0, "no doll lost its session");
    }
    // (v150-host beta 21) a round whose claims the host's game never answers (40 claims, 43 s), then the next round: the
    // doll claims and picks again. Until beta 20 the count of claims was put back to 0 only once a claim was sent, which
    // the count itself forbade: the doll never claimed a round again and stayed out of every later round of that session
    if (which == "claimagain") {
        setenv("MGO1_VDOLLS", "2", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 1;
        vdoll::Init(h);
        Run(1500);
        Check(Count("pick") == 2, "two dolls in a team deathmatch");
        gDeaf = true; gClaims = 0;
        NewRound(1);
        Run(3600);
        Check(gClaims == 80 && Count("pick") == 2, "a round whose claims get no answer: 40 claims each, no pick");
        gDeaf = false; gClaims = 0;
        NewRound(1);
        Run(900);
        Check(gClaims >= 2 && Count("pick") == 4, "the next round: both dolls claim and pick again");
    }
    if (which == "fullfirst") {
        setenv("MGO1_VDOLLS", "5", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.seats = 8;
        vdoll::Init(h);
        Run(2000);
        for (int k = 0; k < 2; ++k) { const int id = G.NextId(); G.players[id] = Player{id, 2, 0, 0x7F000001u + (uint32_t)k}; }   // two full dolls
        Run(200);
        Check(Count("connect") == 5 && (int)G.players.size() == 7, "five virtual dolls and two full dolls: 7 joiners of 8 seats, one kept");
        int r = AddReal(0xFF);
        Run(60);
        G.players[r].kind = 0;
        Run(1200);
        Check(Count("close") == 0, "a real player in the kept seat: a full doll's seat first (none of the virtual dolls leaves in 20 s)");
        for (auto& [id, pl] : G.players) if (pl.uid == 0x7F000002u) { G.players.erase(id); break; }   // the manager's full doll gone
        Run(3000);
        Check(Count("close") == 0, "the full doll gone: the virtual dolls stay");
        r = AddReal(0xFF);
        Run(60);
        G.players[r].kind = 0;
        Run(2400);
        Check(Count("close") == 0, "another real player: 40 s waiting for the last full doll's seat");
        Run(600);
        Check(Count("close") == 1, "still full after 45 s: the newest virtual doll leaves");
    }
    // (v150-host beta 17) a team deathmatch's tickets: each death tells the room {7, the doll's team}, in every round of a game
    // (the third and later rounds too: the round's streams start again), and no such record in the other rules
    if (which == "tickets") {
        setenv("MGO1_VDOLLS", "2", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 1;
        vdoll::Init(h);
        Run(900);
        auto kill = [&](int slot) {                      // its life from the host, then 0: dead; then its respawn's time
            for (auto& [ip, p] : G.peers) {
                if (p.slot != slot) continue;
                PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
                SendAll(p, ip);
                Run(30);
                PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 0, 0});
                SendAll(p, ip);
                Run(500);
                return;
            }
        };
        Check(TeamOf(1) != TeamOf(2) && TeamOf(1) < 2 && TeamOf(2) < 2, "two dolls, one a side");
        int deaths = 0;
        bool byTeam = true;
        for (int round = 0; round < 5; ++round) {
            if (round) { NewRound(1); Run(600); }
            for (int slot = 1; slot <= 2; ++slot) {
                const int team = TeamOf(slot);
                kill(slot);
                ++deaths;
                const std::string want = "room death " + std::to_string(team);
                if (team > 1 || G.events.empty() || Count("room death") != deaths) byTeam = false;
                else {
                    std::string last;
                    for (const auto& e : G.events) if (e.find("room death") != std::string::npos) last = e;
                    if (last.size() < want.size() || last.compare(last.size() - want.size(), want.size(), want) != 0) byTeam = false;
                }
            }
            char what[120];
            snprintf(what, sizeof what, "round %d: each death tells the room {7, the doll's team} (%d of %d taken)", round + 1, Count("room death"), deaths);
            Check(byTeam, what);
        }
        Check(Count("stream") == 0, "none of its records to the room out of the round's order");
        for (int rule : {0, 3, 2, 4}) {
            NewRound(rule); Run(600);
            kill(1); kill(2);
        }
        Check(Count("room death") == deaths, "no such record in deathmatch, capture, rescue and sneaking");
    }
    // the host's own reliable records on a round's channel count from 0 at each round: the third round's first ones have
    // the first round's numbers and are new all the same (its kick taken)
    if (which == "roundkick") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        vdoll::Init(h);
        Run(900);
        {
            Peer& p = G.peers.begin()->second;
            Rel(p, 0x62, {0x01, 0xa0, 0x8c, 0x00, 0x00});     // the room's first reliable record of the round (number 0)
            SendAll(p, G.peers.begin()->first);
        }
        Run(60);
        NewRound(1); Run(600);
        NewRound(1); Run(600);                           // the third round: the room's channel has the first round's number
        Check(Count("close") == 0, "in the game for three rounds");
        {
            Peer& p = G.peers.begin()->second;
            p.nextSeq.erase(0x62);                        // (the host's stream of the round starts again)
            Rel(p, 0x62, {0x04, (uint8_t)p.slot});
            SendAll(p, G.peers.begin()->first);
        }
        Run(600);
        Check(Count("close") == 1, "the host's kick in a game's third round is taken: it leaves");
    }
    // (v150-host beta 18) a virtual doll put to sleep in TEAM SNEAKING: as a player's game, a defender's stamina comes back 4
    // times slower (tsne.h gives a real defender a quarter of the game's rate), a sneaker's at its own rate, and a
    // teammate's hit (the game's ally wake: a negative stun) wakes it at once
    if (which == "tsnewake") {
        setenv("MGO1_VDOLL_MASH", "0", 1);               // (v150-host beta 22, mash: beta 18's numbers; a doll that presses: the mash scenarios)
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 5;
        h.objectives = Objectives;
        vdoll::Init(h);
        Run(900);
        Peer& p = G.peers.begin()->second;
        const uint32_t ip = G.peers.begin()->first;
        const int team = TeamOf(p.slot);
        Check(team == 0 || team == 1, "the doll in a team of a team sneaking round");
        auto hit = [&](int stun, int from) {
            PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x05, 200, 0});
            PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x04, (uint8_t)from, 0x00, 0x00, (uint8_t)stun, (uint8_t)(stun >> 8), 0x05, 0x00, 0x01});
            SendAll(p, ip);
        };
        // its team defends (the other side's goal set): a Mk22 dart to the head knocks it out, 21 s later still down
        gSneak = 1 - team;
        hit(30000, 9);
        Run(30);
        Check(LogCount("knocked out by player 10 (asleep: a dart)") == 1, "a defender knocked out by a dart (asleep)");
        Check(LogCount("it wakes 4 times slower") == 1, "a defender in team sneaking wakes 4 times slower");
        Run(1500);
        Check(LogCount("up again") == 0, "still down 25 s later (a doll out of team sneaking is up after about 21 s)");
        // a teammate's punch (a negative stun of the full gauge): up at once
        hit(-30000, 8);
        Run(200);
        Check(LogCount("woken by player 9's hit") == 1, "woken at once by a teammate's hit (the game's ally wake)");
        Check(LogCount("up again") == 1, "up again after its getting up");
        // knocked out again with no teammate: up after about 83 s, not before 70 s
        Run(600);
        hit(30000, 9);
        Run(4200);
        Check(LogCount("up again") == 1, "a defender left alone still down 70 s later");
        Run(1200);
        Check(LogCount("up again") == 2, "and up by itself about 83 s after (a quarter of its rate)");
        // its team sneaks: the rate as before (about 21 s)
        gSneak = team;
        Run(600);
        hit(30000, 9);
        Run(1500);
        Check(LogCount("up again") == 3, "a sneaker knocked out is up again about 21 s later, as before");
    }
    // (v150-host beta 18) the weapons' pace measured on a playable doll's own game (probe 10, docs/VIRTUAL_DOLL.md 18): the
    // virtual dolls' table never faster than the game (a shot's frames, the magazine, the frames across a reload)
    // (v150-host beta 19) its death's kind as a player's game tells it: a head shot (2) only when the last damage report on
    // it had the head byte, else 1 (beta 18 sent 2 at every death: the host's game counted each a head shot kill)
    if (which == "deathkind") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 0;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        auto hitAndDie = [&](std::vector<uint8_t> rec, int hits) {   // its life 200; the reports; its life 0; the frames after
            Peer& p = G.peers[ip];
            const uint16_t ch = (uint16_t)(0x40 + 3 * p.slot);
            PutRec(p.pending, ch, 0, -1, {0x05, 200, 0}); SendAll(p, ip);
            Run(30);
            for (int i = 0; i < hits; ++i) { PutRec(p.pending, ch, 0, -1, rec); SendAll(p, ip); Run(6); }
            PutRec(p.pending, ch, 0, -1, {0x05, 0, 0}); SendAll(p, ip);
            Run(200);
        };
        const uint8_t a = (uint8_t)r;
        // the AK-47 in the body three times then once more (60 each), {04 attacker, damage, stun, weapon, 0, head}
        hitAndDie({0x04, a, 60, 0, 0, 0, 0x0C, 0, 0}, 4);
        {   // a game that still drew it alive hits its head once more after its death (seen on the host, aahs_aa_after f10669)
            Peer& p = G.peers[ip];
            PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x04, a, 240, 0, 0, 0, 0x0C, 0, 1}); SendAll(p, ip);
            Run(60);
        }
        Check(Count("death 1 by") == 1 && Count("death 2 by") == 0, "killed by body hits: a death of kind 1 (not a head shot)");
        Check(LogCount("a head shot") == 0, "its log line says no head shot");
        Run(400);                                      // (back in play)
        hitAndDie({0x04, a, 240, 0, 0, 0, 0x0C, 0, 1}, 1);
        Check(Count("death 2 by") == 1, "killed by a hit in the head: a death of kind 2");
        Check(LogCount("a head shot") == 1, "its log line says a head shot");
        Run(400);
        // a head hit of the SCORPION (152), then the knife in the body: the last report counts, as in a player's game
        Peer& p = G.peers[ip];
        PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, {0x04, a, 152, 0, 0, 0, 0x0A, 0, 1}); SendAll(p, ip);
        Run(6);
        hitAndDie({0x04, a, 100, 0, 0, 0, 0x01, 0, 0}, 1);
        Check(Count("death 1 by") == 2 && Count("death 2 by") == 1, "a knife after a head hit: a death of kind 1");
        Check(Count("death record") == 3, "three deaths, each told once");
    }
    // (v150-host beta 21) a blast's damage report (a claymore the doll set off, a grenade, TNT, C3, the RPG-7: made by the
    // program of the player who laid or threw it, with his player number): the doll's life is the host's, its death is told
    // once with that player as its killer and the kind 1 (a real player's game tells 1 after a blast: docs/VIRTUAL_DOLL.md
    // 29); a blast's report carries no stun (as the grenade's measured ones): it is not knocked out. The same for a claymore
    // of its own team's (the game sets it off for an ally too): the record names the layer
    // (v150-host beta 22, blast) a blast's fall (the reaction record {09 attacker, 8, angle}) as a real client's game tells its
    // player's (measured: blast_b21_d, a frag at a playable doll's feet): nothing but its state ticks while its body flies (no
    // fall record, no down event) for 141 frames, then its landing ({10}, its place where the host's game threw its copy, {0A
    // id 1}, the down event), {0A id 5} as soon as a player's input is taken, {0A id 0} 70 frames later, up. A roll's (kind 9)
    // as before. blastfall0: MGO1_VDOLL_BLASTS=0, beta 21's (lying 56 frames after the record, up at 252 by its own clock)
    // ((v150-host beta 25, reactfix) blastfallreact0: MGO1_VDOLL_REACT_KIND=0, the M37's fall (6, 7) beta 21's clock as before)
    if (which == "blastfall" || which == "blastfall0" || which == "blastfallreact0") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        if (which == "blastfall0") setenv("MGO1_VDOLL_BLASTS", "0", 1);
        if (which == "blastfallreact0") setenv("MGO1_VDOLL_REACT_KIND", "0", 1);
        G.rule = 0;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        rec(0, {0x05, 200, 0}); Run(30);
        const long long at = gNow;
        rec(1, {0x09, a, 0x08, 0xc4, 0x33});                     // the thrower's reaction record and his blast's damage, then the host's life
        rec(0, {0x04, a, 112, 0, 0, 0, 0x13, 0, 0});
        rec(0, {0x05, 88, 0});
        gCopyAt[sl][0] = -3400.f; gCopyAt[sl][1] = 300.f; gCopyAt[sl][2] = -1258.f;   // (its copy thrown 3.6 m, lying)
        Run(600);
        char t[32];
        snprintf(t, sizeof t, "fall %d 1", sl); const long long f1 = EvAt(t, at);
        snprintf(t, sizeof t, "fall %d 5", sl); const long long f5 = EvAt(t, at);
        snprintf(t, sizeof t, "fall %d 0", sl); const long long f0 = EvAt(t, f1 >= 0 ? f1 : at);
        snprintf(t, sizeof t, "down event %d 4", sl); const long long fe = EvAt(t, at);
        snprintf(t, sizeof t, "landing %d", sl); const long long fl = EvAt(t, at);
        printf("  the record at f%lld: {10} +%lld, {0A 1} +%lld, the down event +%lld, {0A 5} +%lld, {0A 0} +%lld\n", at, fl - at, f1 - at, fe - at, f5 - at, f0 - at);
        if (which != "blastfall0") {
            Check(f1 >= at + 135 && f1 <= at + 150, "no fall record while its body flies: {0A id 1} 135 to 150 frames after the blast's record (a client's 141)");
            Check(fe >= at + 135 && fe <= at + 150 && fl >= at + 135 && fl <= at + 150, "its landing's {10} and the down event with it, none before");
            snprintf(t, sizeof t, "place %d -3400 300 -1258", sl);
            Check(EvAt(t, at) >= 0 && EvAt(t, at) <= fl + 2, "its place at its landing where the host's game threw its copy");
            Check(f5 > f1 && f5 <= f1 + 20, "{0A id 5} as soon as a player's input is taken (within 20 frames of lying)");
            Check(f0 >= f5 + 64 && f0 <= f5 + 76, "{0A id 0} 70 frames after {0A id 5}");
            Check(LogCount("up again") == 1 && LogCount("(a blast: thrown)") == 1, "up again; its log says a blast threw it");
            Check(Count("fall 1 4") == 0 || sl != 1, "no {0A id 4} (a client lying awake tells 1)");
        } else {
            Check(f1 >= at + 50 && f1 <= at + 70 && f0 >= at + 245 && f0 <= at + 265, "MGO1_VDOLL_BLASTS=0: beta 21's fall (lying 56 frames after, up at 252)");
        }
        // a roll's knock-down (kind 9): as before whatever the switch
        Run(300);
        rec(0, {0x05, 200, 0}); Run(30);
        const long long at2 = gNow;
        rec(1, {0x09, a, 0x09, 0x00, 0x10});
        Run(400);
        snprintf(t, sizeof t, "fall %d 1", sl); const long long g1 = EvAt(t, at2);
        snprintf(t, sizeof t, "fall %d 0", sl); const long long g0 = EvAt(t, g1 >= 0 ? g1 : at2);
        printf("  a roll at f%lld: {0A 1} +%lld, {0A 0} +%lld\n", at2, g1 - at2, g0 - at2);
        // ((v150-host beta 22, mash) up at its hand's first press once it lies awake, as a real client's (mash_c2: up 141 frames after
        // the roll's record reached it); the mashroll scenarios check it, MGO1_VDOLL_MASH=0 beta 22's 252)
        Check(g1 >= at2 + 50 && g1 <= at2 + 70 && g0 >= at2 + 130 && g0 <= at2 + 175, "a roll's knock-down: lying 56 frames after, up at its first press once lying awake (130 to 175)");
        // (v150-host beta 22, blast; review22a B3) the M37's fall (kinds 6 and 7, not measured on a real client): as before too.
        // ((v150-host beta 25, reactfix) measured now (rf_m2, probe 54): a real client's fall 27 ticks, then lying awake until he presses, up
        // 80 frames after the press: the doll lies 54 frames after the record (27 ticks, one tick's slack), {0A 5} at its first press once
        // lying awake (within 20 frames), {0A 0} 70 frames after it; MGO1_VDOLL_BLASTS=0 and MGO1_VDOLL_REACT_KIND=0: beta 21's clock)
        const bool m37New = which == "blastfall";
        for (int kind = 6; kind <= 7; ++kind) {
            Run(300);
            rec(0, {0x05, 200, 0}); Run(30);
            const long long at3 = gNow;
            rec(1, {0x09, a, (uint8_t)kind, 0x00, 0x10});
            Run(400);
            snprintf(t, sizeof t, "fall %d 1", sl); const long long h1 = EvAt(t, at3);
            snprintf(t, sizeof t, "fall %d 0", sl); const long long h0 = EvAt(t, h1 >= 0 ? h1 : at3);
            snprintf(t, sizeof t, "fall %d 5", sl); const long long h5 = EvAt(t, h1 >= 0 ? h1 : at3);
            printf("  the M37's fall (kind %d) at f%lld: {0A 1} +%lld, {0A 5} +%lld, {0A 0} +%lld\n", kind, at3, h1 - at3, h5 - at3, h0 - at3);
            if (m37New) Check(h1 >= at3 + 53 && h1 <= at3 + 58 && h5 > h1 && h5 <= h1 + 20 && h0 >= h5 + 64 && h0 <= h5 + 76 && LogCount("(the M37's fall: thrown)") == kind - 5,
                              "the M37's fall (kinds 6, 7) as a real client's: lying a player's 27 ticks after the record, up at its first press, 70 frames to stand");
            else Check(h1 >= at3 + 50 && h1 <= at3 + 70 && h0 >= at3 + 245 && h0 <= at3 + 265, "the M37's knock-down (kinds 6, 7) as beta 21 (lying 56 frames after, up at 252)");
        }
    }
    // (v150-host beta 22, blast) the host's own copy of the doll tells its fall (Host::see, its first state word): thrown
    // (0x6000002), landed (0xE000008), lying awake (0x8000008) 120 frames after the record: the doll's landing's records then,
    // not at the measured 141 frames
    if (which == "blastcopy") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 0;
        h.see = SeeCopy;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        rec(0, {0x05, 200, 0}); gCopyState[sl] = 3; Run(30);
        const long long at = gNow;
        rec(1, {0x09, a, 0x08, 0x00, 0x40});
        gCopyState[sl] = 0x6000002; Run(50);
        gCopyState[sl] = 0xE000008; Run(70);
        gCopyState[sl] = 0x8000008; Run(30);
        gCopyState[sl] = 0x48; Run(300);
        char t[32]; snprintf(t, sizeof t, "fall %d 1", sl); const long long f1 = EvAt(t, at);
        printf("  the record at f%lld: {0A 1} +%lld (its copy lying awake from +%lld)\n", at, f1 - at, 120LL);
        Check(f1 >= at + 120 && f1 <= at + 128, "its landing when its copy in the host's game lies awake");
        Check(LogCount("its copy lying awake there") == 1, "its log says so");
    }
    // (v150-host beta 22, blast) a flinch (the reaction kinds 1 to 5: the WP's, a blast at a crouched player): held as a
    // player's animation holds him (its body paused 40 frames), never knocked down; MGO1_VDOLL_BLASTS=0: not taken
    if (which == "flinch") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_DEBUG", "1", 1);
        G.rule = 0;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        rec(0, {0x05, 200, 0}); Run(30);
        const long long at = gNow;
        rec(1, {0x09, a, 0x02, 0x00, 0x80});
        rec(0, {0x04, a, 75, 0, 0, 0, 0x14, 0, 0});
        Run(200);
        char t[32]; snprintf(t, sizeof t, "fall %d", sl);
        Check(LogCount("flinches (player") == 1, "a WP's reaction (kind 2): it flinches");
        Check(EvAt(t, at) < 0 && LogCount("knocked down") == 0, "a flinch is no knock-down: no fall record");
        rec(1, {0x09, a, 0x01, 0x00, 0x00}); Run(60);
        rec(1, {0x09, a, 0x04, 0x00, 0x00}); Run(100);
        Check(LogCount("flinches (player") == 3, "kinds 1 and 4 too (a front, a crouched one's)");
    }
    // (v150-host beta 25, fallflinch) a flinch as long as a player's own game holds him in it: each kind 1 to 5 from the reaction
    // record to its end (the doll's "its flinch over" line), the player's ticks of that kind (probe 52, ff_hits1: FlinchTicks below)
    // at 2 of the host's frames a tick, one frame's slack; flinchslow: the host slowed to 38 frames a second (the machine's clock
    // injected), the same REAL time (fewer of its frames); flinchjitter: a full pace host whose frames come bunched (8 and 25 ms), the
    // same real time (KoClock's way, each frame at least its nominal 16.7 ms, ran ahead: 27 frames, ff_ba_after2's 473 to 530 ms for
    // 567); a hit again within one starts it again; flinchlen0: MGO1_VDOLL_HIT_FALL=0,
    // b16's 40 frames (flinchlen's check fails with it, and on b16's vdoll.cpp, which has no end line)
    // ((v150-host beta 25, reactfix) the kind 4 a player's 29 ticks (rf_ba_after), 5 his 30 (rf_m2: the M37's 3 pellets on him lying after its
    // fall); flinchreact0, MGO1_VDOLL_REACT_KIND=0: b16's 40 frames for both (flinchlen fails on b16's vdoll.cpp for them)
    // ((v150-host beta 25, clockfix) flinchquota: a host held by a processor quota (QuotaMs, about 41 frames a second in bursts), the same
    // real time; flinchquota0: the same with MGO1_VDOLL_CLOCK_FIX=0 (fallflinch's sum of the machine's ms, each step 3 times its frames at most)
    // flinchfreeze: passes 1, 1 then 2 frames apart, 70, 10 and 20 ms (gMixed with gMixedStep: the first one after a freeze longer than 3
    // frames' worth, as cf_k_before's at pace 34): the same real time; fallflinch's sum cut each such pass to 50 ms and ran 20 % behind
    // (on cf_k_before's own ticks 18.5 %: its flinches 672 ms real for 567): FAILS on b16 c559164's vdoll.cpp and with MGO1_VDOLL_CLOCK_FIX=0
    const bool flQuota = which == "flinchquota" || which == "flinchquota0", flFreeze = which == "flinchfreeze";
    if (which == "flinchlen" || which == "flinchslow" || which == "flinchjitter" || which == "flinchlen0" || which == "flinchreact0" || flQuota || flFreeze) {
        if (which == "flinchreact0") setenv("MGO1_VDOLL_REACT_KIND", "0", 1);
        if (which == "flinchslow") h.realMs = SlowMs;
        if (flQuota) h.realMs = QuotaMs;
        if (flFreeze) { h.realMs = MixedMs; gMixed = true; gMixedStep[0] = 70; gMixedStep[1] = 10; gMixedStep[2] = 20; }
        if (which == "flinchquota0") setenv("MGO1_VDOLL_CLOCK_FIX", "0", 1);
        if (which == "flinchjitter") h.realMs = BunchMs;   // (the same real time as at full pace, the clock not run ahead by the bunched frames)
        if (which == "flinchlen0") setenv("MGO1_VDOLL_HIT_FALL", "0", 1);
        gFollowCopy = true;                              // (its host copy where its place records put it: the flinch reads its place there)
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_DEBUG", "1", 1);
        G.rule = 0;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        rec(0, {0x05, 200, 0}); Run(30);
        // (a player's flinch by its kind, in his ticks: kinds 1 and 2 the flamethrower's front and back flinches, 3 a lying player's)
        const int kTicks[6] = {0, kPlayerFlinchTicks[1], kPlayerFlinchTicks[2], kPlayerFlinchTicks[3], which == "flinchreact0" ? 0 : kPlayerFlinchTicks[4], which == "flinchreact0" ? 0 : kPlayerFlinchTicks[5]};
        const bool off = which == "flinchlen0", slow = which == "flinchslow";
        auto realAt = [&](long long f) { return slow ? (double)f * 1000.0 / 38.0 : which == "flinchjitter" ? BunchAt(f) : flQuota ? QuotaAt(f) : flFreeze ? MixedAt(f) : (double)f * 1000.0 / 59.94; };   // (the machine's ms at a frame)
        auto overAt = [&](size_t from) { for (size_t i = from; i < gLog.size(); ++i) if (gLog[i].find("its flinch over") != std::string::npos) return gLogF[i]; return -1LL; };
        int good = 0, quiet = 0, armed = 0;
        const int sl = G.peers[ip].slot;
        for (int kind = 1; kind <= 5; ++kind) {
            const size_t from = gLog.size();
            const long long at = gNow;
            rec(1, {0x09, a, (uint8_t)kind, 0x00, 0x00});
            Run(200);
            const long long end = overAt(from);
            // (a player's game sends no place while his reaction runs: his state, his view and his weapon's records only, bench
            // ff_hits1's P2P log; the doll's place there before, its host copy's, pushed by the animation)
            int places = 0;
            const long long until = end >= 0 ? end : at + 40;   // (b16's vdoll.cpp says no end: its 40 frames)
            // ((v150-host beta 25, clockfix) flinchfreeze's passes one frame apart: a place record made at the pass before the reaction can
            // go out in the datagram two passes later, 2 frames after it: not counted)
            for (long long f : gPlaceAt[sl]) if (f > at + (flFreeze ? 2 : 1) && f < until) ++places;
            if (getenv("VDOLL_TEST_PLACES")) { printf("  places of slot %d from f%lld:", sl, at); for (long long f : gPlaceAt[sl]) if (f >= at - 12 && f < at + 60) printf(" f%lld", f); printf("\n"); }
            if (places == 0 || off) ++quiet;
            if (places && !off) printf("  the kind %d: %d place records while it flinched\n", kind, places);
            // (his weapon's record through it the one he sent before the hit: the weapon in his hands, ff_hits1; b16's said none)
            uint32_t before = 0; int weap = 0, same = 0;
            for (auto& w : gWeapAt[sl]) { if (w.first <= at) before = w.second; else if (w.first < until) { ++weap; if (w.second == before) ++same; } }
            if (weap && same == weap && (before & 0xff00)) ++armed;
            printf("  the kind %d: %d weapon records while it flinched, %d of them its record before the hit (%06x)\n", kind, weap, same, before);
            const double ms = end < 0 ? -1.0 : realAt(end) - realAt(at);
            const bool frames40 = off || !kTicks[kind];      // (b16's 40 of the host's frames: the switch at 0, or a kind not measured)
            const double want = frames40 ? realAt(at + 40) - realAt(at) : kTicks[kind] * 2.0 * 1000.0 / 59.94, slack = slow ? 2000.0 / 38.0 + 1.0 : which == "flinchjitter" ? 51.0 : flQuota ? 2.0 * gQuotaWork + 100.0 - gQuotaRun + 1.0 : flFreeze ? 71.0 : 1000.0 / 59.94 + 1.0;   // (the doll's tick: its longest step; slowed, its ticks 2 frames apart here: (v150-host beta 25, reactfix) the kind 5's 30 ticks end at the 39th frame, the tick at the 40th)
            const bool ok = end >= 0 && ms >= want - 1.0 && ms <= want + slack;
            printf("  the kind %d: its flinch over %lld frames after the record (%.0f ms; %s)%s\n", kind, end < 0 ? -1 : end - at, ms,
                   frames40 ? "40 of the host's frames wanted" : (std::string("a player's ") + std::to_string(kTicks[kind]) + " ticks wanted").c_str(), ok ? "" : " (off)");
            if (ok) ++good;
        }
        Check(good == 5, off ? "MGO1_VDOLL_HIT_FALL=0: every kind's flinch 40 of the host's frames, as b16" : which == "flinchreact0" ? "MGO1_VDOLL_REACT_KIND=0: the kinds 1 to 3 a player's, 4 and 5 b16's 40 frames"
                                                                                                        : "the kinds 1 to 5 as long as a player's own game holds him, by his clock");
        if (!off) Check(quiet == 5, "no place record while it flinches, as a player's game sends none in his reaction");
        if (!off) Check(armed == 5, "its weapon's record while it flinches the one it sent before the hit (its weapon in its hands), as a player's");
        if (!off) {                                      // (a hit again 10 frames into one: its whole length again from the second record)
            const size_t from = gLog.size();
            rec(1, {0x09, a, 0x01, 0x00, 0x00}); Run(10);
            const long long second = gNow;
            rec(1, {0x09, a, 0x02, 0x00, 0x00}); Run(200);
            const long long end = overAt(from);
            const double ms = end < 0 ? -1.0 : realAt(end) - realAt(second), want = kTicks[2] * 2.0 * 1000.0 / 59.94;
            printf("  a hit again 10 frames into a flinch: over %lld frames after the second record (%.0f ms)\n", end < 0 ? -1 : end - second, ms);
            Check(end >= 0 && ms >= want - 1.0 && ms <= want + (which == "flinchjitter" ? 51.0 : flQuota ? 2.0 * gQuotaWork + 101.0 - gQuotaRun : flFreeze ? 71.0 : slow ? 28.0 : 18.0), "a hit again within a flinch starts it again, as the player's reaction");
        }
    }
    // (v150-host beta 22, review) thrown by a blast, then knocked out while its body flies or lies (a stun that empties its
    // stamina: the Mk22's darts, a stun grenade, a roll at a lying player): its stamina comes back and it gets up, as a
    // knocked-out player does. Before the review ThrownTick had no way out of the knocked-out state (only DownedTick
    // gives the stamina back): the doll lay to its death or the round's end
    if (which == "blastko") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 0;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        char t[32]; snprintf(t, sizeof t, "fall %d 0", sl);
        for (int lie = 0; lie < 2; ++lie) {             // knocked out 60 frames after the blast (flying), then 200 after it (lying)
            rec(0, {0x05, 200, 0}); Run(30);
            const long long at = gNow;
            rec(1, {0x09, a, 0x08, 0xc4, 0x33});
            rec(0, {0x04, a, 112, 0, 0, 0, 0x13, 0, 0});
            rec(0, {0x05, 88, 0});
            Run(lie ? 200 : 60);
            rec(0, {0x04, a, 0, 0, 0x30, 0x75, 0x15, 0, 0});   // (a stun of 30000: its stamina emptied)
            Run(2000);                                         // (knocked out: its stamina back in about 21 s, 1250 frames)
            const long long up = EvAt(t, at);
            printf("  thrown at f%lld, knocked out %s: {0A id 0} +%lld\n", at, lie ? "lying" : "flying", up >= 0 ? up - at : -1LL);
            Check(LogCount("knocked out by player") == lie + 1, lie ? "thrown, landed, then knocked out" : "thrown, then knocked out while its body flies");
            Check(up >= 0 && LogCount("up again") == lie + 1, lie ? "... up again once its stamina is back (lying)" : "... up again once its stamina is back (flying)");
            Run(300);
        }
    }
    // (v150-host beta 22, wake) a teammate's blow on the doll knocked out: the game's ally wake (a negative stun, US n003a
    // 0x438A30) adds to its gauge as its own recovery does (the game's own sum, whatever that recovery's rate): the full gauge at
    // once wakes it (up after its getting up); a part of it (6000) brings its getting up nearer; the stun's record of a blow on
    // it standing (half a punch, 2250) only spends its stamina
    // (v150-host beta 23, wake) woken so, it gets up as a woken real client does: {0A 0} 118 frames after the blow (wake_b1_on_t16:
    // the client's own 118, its records 114 to 118; reports/vdoll_wake.md 6); allywake0, MGO1_VDOLL_WAKE=0: KoWake's 66 frames, as
    // beta 22 (beta 22's code: 68, allywake FAILS)
    // allywakeslow (mash3's clock): on a host slowed to 38 frames a second the woken doll's getting up lasts a player's 118 frames
    // at full pace, 1.97 real s: 75 of the host's frames (118 of them on beta 23's first code, by the host's frames: FAILS)
    // (v150-host beta 24, cqc) a CQC throw from a hold on the doll: the holder's game sends {0D holder 1 ..} then {04 holder, life 0, stun,
    // kind 0} (vdoll_cqc.h). Its stamina spent; a knock out only when that empties it; not knocked out, a thrown player's fall: lying
    // ({10}, {0A id 1}), up at its first press once it lies awake ({0A 5}, {0A 0}). Then a second throw once it is up (20000 -> 10000:
    // no stamina back while up, section 41, as the real victim of cqc_m1) and a third: knocked out by the third, as a player; one of
    // 30000 (Raikov's): knocked out. Beta 23's code (and cqcthrow0, MGO1_VDOLL_CQC=0):
    // the stamina spent, no fall told (cqcthrow FAILS on beta 23: no `thrown by` line, no fall record after the throw)
    if (which == "cqcthrow" || which == "cqcthrow0") {
        const bool off = which == "cqcthrow0";
        if (off) setenv("MGO1_VDOLL_CQC", "0", 1);
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_DEBUG", "1", 1);
        G.rule = 1;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        auto hold = [&]() { rec(1, {0x0D, a, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0, 0}); };
        auto thr = [&](int v) { rec(0, {0x04, a, 0, 0, (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), 0x00, 0x00, 0x00}); };
        rec(0, {0x05, 200, 0}); Run(60);
        hold(); Run(56);
        const long long at = gNow;
        thr(10000);                                        // (Ocelot's hold: a third of the gauge)
        Run(500);
        char t1[32], t0[32];
        snprintf(t1, sizeof t1, "fall %d 1", sl); snprintf(t0, sizeof t0, "fall %d 0", sl);
        const long long lie = EvAt(t1, at), up = lie >= 0 ? EvAt(t0, lie) : -1;
        printf("  thrown at f%lld: lying %+lld, up %+lld\n", at, lie >= 0 ? lie - at : -1LL, up >= 0 ? up - at : -1LL);
        Check(LogCount("knocked out by player") == 0, "a throw of 10000 from a full gauge: not knocked out");
        if (off) {
            Check(LogCount("thrown by player") == 0 && lie < 0, "MGO1_VDOLL_CQC=0: no fall told, as beta 23");
        } else {
            char w[128]; snprintf(w, sizeof w, "thrown by player %d with CQC from a hold: the stun 10000, its stamina 30000 -> 20000", a + 1);
            Check(LogCount(w) == 1, "... its stamina 30000 -> 20000, said");
            Check(lie >= 0 && lie - at <= 80, "... it falls: lying told within 80 frames of the stun");
            Check(up >= 0 && up - at >= 100 && up - at <= 300, "... up again at its first press once it lies awake (100 to 300 frames after the stun)");
        }
        Run(400);                                          // (up again: none of its stamina back while it stands)
        hold(); Run(56);
        thr(10000);                                        // (the second throw)
        Run(400);
        if (!off) {
            char w2[128]; snprintf(w2, sizeof w2, "thrown by player %d with CQC from a hold: the stun 10000, its stamina 20000 -> 10000", a + 1);
            Check(LogCount(w2) == 1, "the second throw once it is up: 20000 -> 10000, it falls again");
        }
        Check(LogCount("knocked out by player") == 0, "... not knocked out by two throws");
        hold(); Run(56);
        thr(10000);                                        // (the third: the gauge emptied)
        Run(30);
        char k1[96], k3[96];
        snprintf(k1, sizeof k1, "knocked out by player %d (a CQC throw from a hold: the stun 10000)", a + 1);
        snprintf(k3, sizeof k3, "knocked out by player %d (a CQC throw from a hold: the stun 30000)", a + 1);
        Check(LogCount(k1) == 1, "the third throw of 10000: knocked out, said with its stun");
        Run(3000);                                         // (up again by its own recovery)
        hold(); Run(56);
        thr(30000);                                        // (Raikov's hold: the whole gauge)
        Run(30);
        Check(LogCount(k3) == 1, "one throw of 30000 (Raikov's): knocked out");
    }
    if (which == "allywake" || which == "allywake0" || which == "allywakeslow") {
        if (which == "allywake0") setenv("MGO1_VDOLL_WAKE", "0", 1);
        if (which == "allywakeslow") h.realMs = SlowMs;
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 1;
        vdoll::Init(h);
        Run(900);
        uint32_t ip = G.peers.begin()->first;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        auto stun = [&](int v, int from, uint8_t kind) { rec(0, {0x05, 200, 0}); rec(0, {0x04, (uint8_t)from, 0, 0, (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), kind, 0, 0}); };
        char t[32]; snprintf(t, sizeof t, "fall %d 0", sl);
        stun(2250, 9, 0x97);                               // (a teammate's punch on it standing: its stamina only)
        Run(30);
        Check(LogCount("knocked out by player") == 0 && LogCount("woken by player") == 0, "half a punch on it standing: not knocked out, nothing woken");
        Run(600);
        long long at = gNow;                               // (alone: knocked out by a stun of 30000, up by its own recovery)
        stun(30000, 8, 0x15);
        Run(3000);
        const long long alone = EvAt(t, at) >= 0 ? EvAt(t, at) - at : -1;
        Check(alone > 600, "knocked out and left alone: up by itself (its own recovery)");
        Run(300);
        at = gNow;                                         // (a teammate's punch 300 frames in: the full gauge)
        stun(30000, 8, 0x15);
        Run(300);
        const long long hit = gNow;
        stun(-30000, 9, 0x97);
        Run(400);
        const long long full = EvAt(t, hit) >= 0 ? EvAt(t, hit) - hit : -1;
        Check(LogCount("woken by player 10's hit") == 1, "a teammate's punch on it knocked out (-30000): woken at once");
        if (which == "allywake") Check(full >= 118 && full <= 130, "... up after a woken client's getting up only ({0A 0} 118 to 130 frames after the blow)");
        else if (which == "allywakeslow") Check(full >= 73 && full <= 85, "... up after a woken client's getting up on a player's clock (host at 38 frames a second: {0A 0} 73 to 85 of its frames after the blow)");
        else Check(full >= 66 && full <= 78, "... up after KoWake's getting up only (66 to 78 frames after the blow: MGO1_VDOLL_WAKE=0, beta 22's)");
        Run(300);
        at = gNow;                                         // (a part of the gauge 300 frames in: its own recovery and the part, summed)
        stun(30000, 8, 0x15);
        Run(300);
        stun(-6000, 9, 0xAE);
        Run(60);
        Check(LogCount("woken by player 10's hit") == 1, "a part of the gauge (-6000): not up at once");
        Run(3000);
        const long long part = EvAt(t, at) >= 0 ? EvAt(t, at) - at : -1;
        printf("  up after %lld frames alone, %lld after the full gauge's blow, %lld with a part of it given 300 frames in\n", alone, full, part);
        Check(part > 300 && alone > 0 && part < alone - 100, "... up sooner than alone: the part and its own recovery summed");
    }
    // (v150-host beta 24, stamina) a conscious player's stun gauge never comes back (US n003a 0x4143F0, docs/VIRTUAL_DOLL.md 41;
    // measured on real clients, reports/vdoll_stamina.md): stun grenades (12500) 20 s apart leave him 17500, then 5000, and the third
    // knocks him out; knocked out, a stun is not applied; up, a teammate's negative stun is dropped; full again at a respawn.
    // Beta 23's doll got 144 every 6 frames back while up (each grenade found it full again: never knocked out), lost a stun's
    // stamina knocked out and took the negative stun up: stamina FAILS on 3363fd4's vdoll.cpp. stamina0: MGO1_VDOLL_STAMINA=0,
    // beta 23's three ways (passes on both codes)
    if (which == "stamina" || which == "stamina0") {
        const bool old = which == "stamina0";
        if (old) setenv("MGO1_VDOLL_STAMINA", "0", 1);
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_LEVEL", "1", 1);                // (knocked out it presses 4 times a second: about 27.5 s from nothing)
        G.rule = 1;
        vdoll::Init(h);
        Run(900);
        uint32_t ip = G.peers.begin()->first;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        auto stun = [&](int v, int from, uint8_t kind) { rec(0, {0x05, 200, 0}); rec(0, {0x04, (uint8_t)from, 0, 0, (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), kind, 0, 0}); };
        Check(gStamNow[sl] == 30000, "deployed with a full gauge (30000)");
        stun(12500, 9, 0x15);                              // (a stun grenade's record: {04 attacker, 0 0, 12500, 0x15, 0, 0})
        Run(30);
        const int a1 = gStamNow[sl];
        Run(1200);
        const int a20 = gStamNow[sl];
        stun(12500, 9, 0x15);
        Run(30);
        const int b1 = gStamNow[sl];
        Run(1200);
        const int b20 = gStamNow[sl];
        printf("  stun grenade 1: its stamina %d, 20 s later %d; stun grenade 2: %d, 20 s later %d\n", a1, a20, b1, b20);
        Check(old ? a1 >= 17500 && a1 <= 18400 : a1 == 17500, old ? "a stun grenade (12500): its stamina 17500, rising (half a second later)" : "a stun grenade (12500): its stamina 17500");
        if (!old) {
            Check(a20 == 17500, "up 20 s after it: still 17500 (a conscious player's never comes back)");
            Check(b1 == 5000 && b20 == 5000, "a second one: 5000, still 5000 20 s later");
        } else {
            Check(a20 == 30000 && b1 >= 17500 && b1 <= 18400 && b20 == 30000, "MGO1_VDOLL_STAMINA=0: full again within 20 s each time (beta 23's 144 every 6 frames)");
        }
        Check(LogCount("knocked out by player") == 0, "two stun grenades: not knocked out");
        long long ko = gNow;
        stun(12500, 9, 0x15);
        Run(30);
        if (!old) Check(LogCount("knocked out by player 10 (a stun grenade)") == 1, "a third one 20 s after the second: knocked out (5000 left), as a player");
        else {
            Check(LogCount("knocked out by player") == 0, "MGO1_VDOLL_STAMINA=0: the third finds it full again, not knocked out (beta 23's)");
            ko = gNow; stun(30000, 9, 0x05); Run(30);      // (knocked out all the same for the next part: a dart's 30000)
            Check(LogCount("knocked out by player 10") == 1, "... knocked out by a stun of 30000");
        }
        Run(300);                                          // (5 s knocked out: its stamina rising by its presses)
        const int k0 = gStamNow[sl];
        stun(12500, 9, 0x15);
        Run(6);
        const int k1 = gStamNow[sl];
        printf("  knocked out, a stun of 12500: its stamina %d -> %d\n", k0, k1);
        if (!old) Check(k1 >= k0, "knocked out, another stun grenade is not applied (its stamina only rises: the game's 0x41454C)");
        else Check(k1 < k0, "MGO1_VDOLL_STAMINA=0: knocked out, the stun is taken off (beta 23's)");
        char t[32]; snprintf(t, sizeof t, "fall %d 0", sl);
        Run(3000);
        const long long up = EvAt(t, ko) >= 0 ? EvAt(t, ko) - ko : -1;
        printf("  knocked out at f%lld, up ({0A id 0}) %lld frames later\n", ko, up);
        if (!old) Check(up >= 1560 && up <= 1860, "up 26 to 31 s after the knock out (level 1's presses), the stun meanwhile changing nothing");
        Check(LogCount("up again") == 1 && gStamNow[sl] == 30000, "up again, its gauge full");
        Run(120);
        stun(12500, 9, 0x15);                              // (up: then a teammate's kick's ally wake, -15000, as a late record)
        Run(30);
        const int c1 = gStamNow[sl];
        stun(-15000, 8, 0x95);
        Run(30);
        const int c2 = gStamNow[sl];
        printf("  up: a stun grenade, its stamina %d; a teammate's negative stun of 15000: %d\n", c1, c2);
        Check(old ? c1 >= 17500 && c1 <= 18400 : c1 == 17500, "up, a stun grenade: 17500");
        if (!old) Check(c2 == 17500 && LogCount("woken by player") == 0, "up, a teammate's negative stun is dropped (the game's 0x414788)");
        else Check(c2 == 30000, "MGO1_VDOLL_STAMINA=0: up, the negative stun fills it (beta 23's)");
        rec(0, {0x05, 0, 0});                              // (killed: then its respawn, full again as the game's 0x40D1A0)
        Run(1200);
        Check(LogCount("dead, by player") == 1 && LogCount("deployed at") >= 2, "killed, then deployed again");
        Check(gStamNow[sl] == 30000, "respawned: its gauge full (the game's respawn 0x40D1A0)");
    }
    // (v150-host beta 24, stamina2) its moves cost its stamina as a player's (vdoll_stamina.h; on real clients 18 every 31 ticks running
    // with a load of 6800, bench stamina2_cal): its body on a disc map (the second argument), it roams two minutes; its stamina falls
    // by what its lines say its moves cost, a run's drain the rule's for its load (vstam::Cost(3, load)), no more than 116 drains (a
    // player's ticks in two minutes / 31). On a094728's vdoll.cpp (no move's cost) it stays 30000: FAILS. movecost0
    // (MGO1_VDOLL_MOVE_COST=0): 30000, as beta 24
    // movecostclay: the same with claymores as its support item (MGO1_VDOLL_SUPPORT_ITEM=claymore): its runs to lay them cost as any
    // run (stamina2's first code counted its support's states 2 to 6 as still, 5 "going to lay" among them: FAILS there)
    // ((v150-host beta 25, aimcost) movecostpass: the same with each pass run twice in its frame as a real host runs them (gTwice): its
    // moves paid as in movecost (beta 25 read its body's last pass, whose speed is 0 there: about one drain in five on the bench
    // aimcost_d1, none here: FAILS on 65673d2's body); movecostpass0 (MGO1_VDOLL_MOVE_FRAME=0): beta 25's, a quarter of the drains at most
    // ((v150-host beta 25, clockfix) movecostjitter, movecostbunch, movecostquota: movecostpass on a host whose ticks come unevenly (JitterMs,
    // BunchMs: full pace) and on one held by a processor quota (QuotaMs): its moves counted in a player's ticks of the real clock, its first
    // minute's line ("its moves, 1799 ticks") 60 real seconds after its deploy, its drains no more than one every 31 of those ticks. b16's
    // step by step clock, read every pass (2 frames apart), ran ahead: the minute at 47.3 s (JitterMs), 47.7 s (BunchMs), 50.8 s
    // (QuotaMs): FAIL there. movecostbunch0 (MGO1_VDOLL_CLOCK_FIX=0): b16's, its minute sooner than 60 real seconds
    const bool mvModel = which == "movecostjitter" || which == "movecostbunch" || which == "movecostquota" || which == "movecostbunch0";
    const bool twice = which == "movecostpass" || which == "movecostpass0" || mvModel;
    if (which == "movecost" || which == "movecost0" || which == "movecostclay" || twice) {
        if (argc < 3) { printf("FAIL %s needs a disc map (the second argument)\n", which.c_str()); return 1; }
        if (which == "movecostjitter") h.realMs = JitterMs;
        if (which == "movecostbunch" || which == "movecostbunch0") h.realMs = BunchMs;
        if (which == "movecostquota") h.realMs = QuotaMs;
        if (which == "movecostbunch0") setenv("MGO1_VDOLL_CLOCK_FIX", "0", 1);
        auto realAt = [&](long long f) { return which == "movecostjitter" ? JitterAt(f) : which == "movecostquota" ? QuotaAt(f)
                                               : which == "movecostbunch" || which == "movecostbunch0" ? BunchAt(f) : (double)f * 1000.0 / 59.94; };
        if (which == "movecost0") setenv("MGO1_VDOLL_MOVE_COST", "0", 1);
        if (which == "movecostpass0") setenv("MGO1_VDOLL_MOVE_FRAME", "0", 1);
        gTwice = twice;
        if (which == "movecostclay") setenv("MGO1_VDOLL_SUPPORT_ITEM", "claymore", 1);
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_LEVEL", "10", 1);
        setenv("MGO1_VDOLL_DEBUG", "1", 1);
        if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
        G.rule = 1;
        if (!LoadMap(argv[2])) { printf("FAIL the map %s not read\n", argv[2]); return 1; }
        h.mapNow = MapNowT; h.stageGeom = StageGeomT; h.see = SeeCopy; gFollowCopy = true;
        vdoll::Init(h);
        Run(2);
        for (int w = 0; w < 36000 && LogCount("(the bodies' mesh)") == 0; ++w) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); vdoll::body::Frame(gNow); }
        Check(LogCount("(the bodies' mesh)") == 1, "the bodies' mesh built on the map");
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        const int sl = G.peers.begin()->second.slot;
        Run(which == "movecostclay" ? 14400 : 7200);     // (two minutes more roaming; four with claymores to lay)
        const int now = gStamNow[sl];
        char ev[32]; snprintf(ev, sizeof ev, "state %d 0", sl);
        const long long dep = EvAt(ev, 0);               // (deployed: its life's count begins)
        // ((clockfix) a player's ticks of the real clock since: the frames' at 59.94 but on the models' clocks)
        const int most = dep >= 0 ? (int)((realAt(gNow) - realAt(dep)) / 1000.0 * vmash::kTicksASecond / vstam::kPeriod) + 1 : 0;   // (a player's ticks since / 31)
        int spent = -1, drains = -1, move = -1, load = -1, run = -1, said = -1;
        for (const auto& l : gLog) {
            const size_t p = l.find("its moves cost its stamina ");
            if (p != std::string::npos) sscanf(l.c_str() + p, "its moves cost its stamina %d in %d drains (move %d, load %d: %d a drain running), its stamina %d", &spent, &drains, &move, &load, &run, &said);
        }
        printf("  deployed at f%lld; %.0f s later its stamina %d; its last line: %d in %d drains (%d at most), load %d, %d a drain running, its stamina %d\n", dep,
               (gNow - dep) / 59.94, now, spent, drains, most, load, run, said);
        if (which == "movecostclay") {
            // (each way to a laying place of 5 s or more, at least four of the 31 ticks: its stamina at its start and when it lays; a map
            // where no such way came in four minutes says so)
            int runs = 0, paid = 0;
            for (size_t i = 0; i < gLog.size(); ++i) {
                if (gLog[i].find("goes to lay a claymore") == std::string::npos) continue;
                for (size_t j = i + 1; j < gLog.size(); ++j) {
                    if (gLog[j].find("lays a claymore") == std::string::npos) continue;
                    if (gLogF[j] - gLogF[i] >= 300) {
                        ++runs; const int a = StamAt(sl, gLogF[i]), b = StamAt(sl, gLogF[j]);
                        if (b < a) ++paid;
                        printf("  a way to lay f%lld to f%lld: its stamina %d -> %d\n", gLogF[i], gLogF[j], a, b);
                    }
                    break;
                }
            }
            if (!runs) printf("  no way of 5 s or more to a laying place on this map in four minutes\n");
            Check(paid == runs && load >= 6000 + vstam::WeaponWeight(vsup::kClayCode) - 300, "its runs to lay its claymores cost as any run (its load with them)");
        }
        if (mvModel) {                                   // ((clockfix) its first minute of ticks against the real clock)
            long long minute = -1;
            for (size_t i = 0; i < gLog.size() && minute < 0; ++i) if (gLog[i].find("its moves, 17") != std::string::npos || gLog[i].find("its moves, 18") != std::string::npos) minute = gLogF[i];   // (a minute's 1799 ticks, or one more)
            const double secs = minute >= 0 && dep >= 0 ? (realAt(minute) - realAt(dep)) / 1000.0 : -1.0;
            printf("  its first minute of ticks at f%lld: %.2f real s after its deploy (%.2f s of the host's frames)\n", minute, secs, minute >= 0 ? (minute - dep) / 59.94 : -1.0);
            if (which == "movecostbunch0") Check(secs > 0 && secs < 59.0, "MGO1_VDOLL_CLOCK_FIX=0: its first minute of ticks sooner than 60 real seconds (b16's step clock ran ahead)");
            else Check(secs >= 59.95 && secs <= 60.2, "its first minute of a player's ticks 60 real seconds after its deploy (the next pass at most)");
        }
        if (which == "movecost" || which == "movecostclay" || which == "movecostpass" || (mvModel && which != "movecostbunch0")) {
            Check(dep >= 0 && now < 30000, "roaming: its stamina below 30000 (its moves cost it)");
            Check(drains >= 30 && drains <= most, "30 drains or more, never more than one every 31 of a player's ticks since its deploy");
            Check(load >= 6000 && load <= 14000 && run == vstam::Cost(3, load), "a run's drain the rule's for its load (its load / 8000 + 0.5 times 14)");
            // ((v150-host beta 25, fallflinch) the costliest drain a doll's move pays: the posture code 8 of a roll and, with MGO1_VDOLL_HIT_FALL,
            // of a drop's fall and landing (a player's ledge action); the crawl's 4 before)
            Check(said == 30000 - spent && now <= said && now >= 30000 - most * vstam::Cost(8, load), "its stamina is 30000 less what its moves cost");
            // ((its lines say the drains every 30: the gauge itself, 30000 less what they took, against a run's cost of that long)
            if (twice) Check(dep >= 0 && (30000 - now) * 10 >= most * vstam::Cost(3, load > 0 ? load : 7000) * 8,
                             "each pass twice in its frame (a real host's): its stamina down by 80 % or more of what a run of that long costs");
        } else if (which == "movecostbunch0") Check(dep >= 0 && drains >= 30, "MGO1_VDOLL_CLOCK_FIX=0: its moves still paid");
        else if (which == "movecostpass0") Check(dep >= 0 && drains * 4 <= most, "MGO1_VDOLL_MOVE_FRAME=0: a quarter of the drains at most (beta 25: its body's last pass, still)");
        else Check(dep >= 0 && now == 30000 && drains < 0, "MGO1_VDOLL_MOVE_COST=0: still 30000 after two minutes roaming (beta 24)");
    }
    // ((v150-host beta 25, clockfix) pacemixed <map>: its body roams a minute on a host held by a processor quota whose passes come 1, 1 then 2
    // frames apart (gMixed, MixedMs: 25 ms a frame on the whole, 40 frames a second): the body's pace (vdoll_body.cpp Slowed(), the frames'
    // real length that its run, roll, fall and holds take) 25 ms x 59.94 / 1000 = 1.4985 within 1 %, read from its copy trace's lines.
    // b16 smoothed each pass's ms a frame (36, 20 and 22): 4 % over (its run, roll and fall that much faster than a player's): FAILS there.
    // pacemixed0 (MGO1_VDOLL_CLOCK_FIX=0): b16's, 2 % over or more
    if (which == "pacemixed" || which == "pacemixed0") {
        if (argc < 3) { printf("FAIL %s needs a disc map (the second argument)\n", which.c_str()); return 1; }
        if (which == "pacemixed0") setenv("MGO1_VDOLL_CLOCK_FIX", "0", 1);
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_LEVEL", "10", 1);
        setenv("MGO1_VDOLL_COPY_TRACE", "1", 1);
        if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
        G.rule = 1;
        if (!LoadMap(argv[2])) { printf("FAIL the map %s not read\n", argv[2]); return 1; }
        h.mapNow = MapNowT; h.stageGeom = StageGeomT; h.see = SeeCopy; gFollowCopy = true; h.realMs = MixedMs;
        gMixed = true; gTwice = true;
        vdoll::Init(h);
        Run(2);
        for (int w = 0; w < 36000 && LogCount("(the bodies' mesh)") == 0; ++w) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); vdoll::body::Frame(gNow); }
        Check(LogCount("(the bodies' mesh)") == 1, "the bodies' mesh built on the map");
        Run(600);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        const size_t from = gLog.size();
        Run(2400);                                       // (a minute of its frames at 40 a second)
        double sum = 0; int n = 0;
        for (size_t i = from + 200; i < gLog.size(); ++i) { const size_t p = gLog[i].find(" slowed "); if (gLog[i].find("copytrack") != std::string::npos && p != std::string::npos) { sum += atof(gLog[i].c_str() + p + 8); ++n; } }
        const double mean = n ? sum / n : 0.0, want = 25.0 * 59.94 / 1000.0;
        printf("  the body's pace over %d reads: %.4f (the host's frames %.4f times a 59.94th of a second: %+.1f %%)\n", n, mean, want, n ? 100.0 * (mean / want - 1.0) : 0.0);
        if (which == "pacemixed0") Check(n > 50 && mean > want * 1.02, "MGO1_VDOLL_CLOCK_FIX=0: the body's pace 2 % or more over the frames' real length (b16's mean of each pass's ms a frame)");
        else Check(n > 50 && std::fabs(mean / want - 1.0) <= 0.01, "the body's pace the frames' real length within 1 % (their ms over their number)");
    }
    // (v150-host beta 24, stamina2) the bench's question on a disc map: a doll that has run a long way, then stun grenades. Its body roams
    // six minutes (a real client exploring lost 5000 to 6000 in that time: benches stamina2_after2, stamina2_after4), then two stun
    // grenades' records (12500 each) 20 s apart: knocked out by the second, as a running player. a094728's doll, and movecostko0
    // (MGO1_VDOLL_MOVE_COST=0): 30000, 17500, 5000, up after the second (FAILS on a094728's vdoll.cpp)
    if (which == "movecostko" || which == "movecostko0") {
        if (argc < 3) { printf("FAIL %s needs a disc map (the second argument)\n", which.c_str()); return 1; }
        if (which == "movecostko0") setenv("MGO1_VDOLL_MOVE_COST", "0", 1);
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_LEVEL", "10", 1);
        setenv("MGO1_VDOLL_DEBUG", "1", 1);
        if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
        G.rule = 1;
        if (!LoadMap(argv[2])) { printf("FAIL the map %s not read\n", argv[2]); return 1; }
        h.mapNow = MapNowT; h.stageGeom = StageGeomT; h.see = SeeCopy; gFollowCopy = true;
        vdoll::Init(h);
        Run(2);
        for (int w = 0; w < 36000 && LogCount("(the bodies' mesh)") == 0; ++w) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); vdoll::body::Frame(gNow); }
        Check(LogCount("(the bodies' mesh)") == 1, "the bodies' mesh built on the map");
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        auto stun = [&](int v) { rec(0, {0x05, 200, 0}); rec(0, {0x04, (uint8_t)r, 0, 0, (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), 0x15, 0, 0}); };
        Run(21600);                                      // (six minutes roaming)
        const int ran = gStamNow[sl];
        stun(12500); Run(30);
        const int one = gStamNow[sl];
        Run(1170);
        const int before2 = gStamNow[sl];
        stun(12500); Run(30);
        const int two = gStamNow[sl];
        const int kos = LogCount("knocked out by player");
        printf("  after six minutes roaming its stamina %d; a stun grenade: %d; 20 s later %d; the second: %d, %s\n", ran, one, before2, two, kos ? "knocked out" : "still up");
        if (which == "movecostko") {
            Check(ran < 25000, "six minutes roaming cost it more than 5000, as a running player (5000 to 6000 on the real clients)");
            Check(kos == 1, "the second stun grenade knocks it out, as it knocks out a running player");
        } else Check(ran == 30000 && before2 == 17500 && two == 5000 && kos == 0, "MGO1_VDOLL_MOVE_COST=0: 30000, 17500, then 5000: the second does not knock it out (beta 24)");
    }
    // (v150-host beta 22, mash) knocked out by a dart (its stamina emptied), a doll presses as a player and its stamina comes back
    // by the game's rule (vdoll_mash.h): level 10 presses 9.5 times a second (awake 16.4 to 21.4 s after the dart), level 1 4 times
    // (25.0 to 29.7 s), MGO1_VDOLL_MASH_RATE=0 none (the game's 1250 ticks: 41.7 s), MGO1_VDOLL_MASH=0 beta 22's 144 every 6
    // frames (20.9 s whatever its level). Then its getting up as a client's after a knock-out: {0A 5} 115 frames, its knocked-out
    // event kept, {0A 0} (beta 22: 66 frames, the event cleared at {0A 5}). With a disc map (the second argument) its body is
    // on that map: no place told moves while it is down, the first after {0A 0} is where its copy lies, and it walks on.
    // Beta 22's code: mashlow and mashnone FAIL (20.9 s), and no `awake after` line
    // (v150-host beta 23, mash3; the owner on review23's F4) mashslow: as mash on a host slowed to 38 frames a second (the injected
    // machine's clock 1000 / 38 ms a frame): a player's game ticks 30 times a real second, so the doll wakes after the same REAL
    // seconds as at 60, 16.4 to 21.4 s of that clock (about 720 of its frames), and its getting up lasts a client's 2 s of it. On
    // mash2's code (the host's frames) it lies 1.58 times longer: FAILS. mashslow0: MGO1_VDOLL_PACE=0, the host's frames (mash2's)
    // ((v150-host beta 25, clockfix) mashjitter, mashbunch, mashquota: as mash on a host whose ticks come unevenly (JitterMs, BunchMs: full
    // pace) and on one held by a processor quota (QuotaMs: about 41 frames a second in bursts): awake after the REAL seconds of its ticks
    // (the ticks its line says / 29.97, the {0A 5} at its next state tick), its getting up a client's 2 s of the real clock. b16's step
    // by step KoClock (read at its state ticks, 6 frames apart) ran ahead at full pace: mashjitter and mashbunch FAIL there (awake 17.45 s
    // after 568 ticks, 18.95 s of a player's game), mashquota passes (its 6-frame steps all longer than their frames). mashbunch0
    // (MGO1_VDOLL_CLOCK_FIX=0): b16's, awake sooner than its ticks' real seconds
    const bool koModel = which == "mashjitter" || which == "mashbunch" || which == "mashquota" || which == "mashbunch0";
    if (which == "mash" || which == "mashlow" || which == "mashnone" || which == "mash0" || which == "mashslow" || which == "mashslow0" || koModel) {
        if (which == "mashslow" || which == "mashslow0") h.realMs = SlowMs;
        if (which == "mashslow0") setenv("MGO1_VDOLL_PACE", "0", 1);
        if (which == "mashjitter") h.realMs = JitterMs;
        if (which == "mashbunch" || which == "mashbunch0") h.realMs = BunchMs;
        if (which == "mashquota") h.realMs = QuotaMs;
        if (which == "mashbunch0") setenv("MGO1_VDOLL_CLOCK_FIX", "0", 1);
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_LEVEL", which == "mashlow" ? "1" : "10", 1);
        if (which == "mashnone") setenv("MGO1_VDOLL_MASH_RATE", "0", 1);
        if (which == "mash0") setenv("MGO1_VDOLL_MASH", "0", 1);
        // (v150-host beta 22, mash2) the same run whatever the machine's load: the bodies' draws seeded (else the time seeds them),
        // and their mesh built before the doll's frames run on (below)
        if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
        G.rule = 1;
        const bool map = argc > 2;
        if (map) {
            if (!LoadMap(argv[2])) { printf("FAIL the map %s not read\n", argv[2]); return 1; }
            h.mapNow = MapNowT; h.stageGeom = StageGeomT; h.see = SeeCopy; gFollowCopy = true;
            printf("  the map %s, the spawn %.0f %.0f %.0f\n", gMapPage.c_str(), gSpawnAt[0], gSpawnAt[1], gSpawnAt[2]);
        }
        vdoll::Init(h);
        if (map) {
            // ((v150-host beta 22, mash2) the bodies' mesh is built on a thread of its own: waited for at one frame (the body asked at
            // the same frame, no frame of the doll's run meanwhile), 3 minutes at most, so that its build's time under any load
            // never moves the doll's frames; the first version ran the doll's frames while it waited, 15 s at most)
            Run(2);
            for (int w = 0; w < 36000 && LogCount("(the bodies' mesh)") == 0; ++w) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); vdoll::body::Frame(gNow); }
            Check(LogCount("(the bodies' mesh)") == 1, "the bodies' mesh built on the map");
        }
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        if (map) Run(1200);                              // (the doll walking a while)
        rec(0, {0x05, 200, 0}); Run(30);
        float lying[3]; memcpy(lying, gCopyAt[sl], sizeof lying);
        gFollowCopy = false;                             // (the host's copy lies where it was: the doll tells no place while down)
        const long long at = gNow;
        rec(0, {0x04, a, 0, 0, 0x30, 0x75, 0x05, 0, 1});   // (a Mk22 dart to the head: a stun of 30000, no life)
        char t[64];
        snprintf(t, sizeof t, "fall %d 0", sl);
        for (int w = 0; w < 3200; w += 2) { Run(2); if (!gFollowCopy && EvAt(t, at) >= 0) gFollowCopy = map; }   // (up: its copy follows it again)
        snprintf(t, sizeof t, "fall %d 5", sl); const long long f5 = EvAt(t, at);
        snprintf(t, sizeof t, "fall %d 0", sl); const long long f0 = EvAt(t, f5 >= 0 ? f5 : at);
        // ((mash3) the seconds on the machine's clock: the injected one at 38 frames a second, else the frames at 59.94)
        auto realS = [&](long long f) { return which == "mashjitter" ? JitterAt(f) / 1000.0 : which == "mashbunch" || which == "mashbunch0" ? BunchAt(f) / 1000.0 : koModel ? QuotaAt(f) / 1000.0
                                               : h.realMs ? (double)f / 38.0 : (double)f / 59.94; };
        const double awake = which == "mashslow0" ? (f5 - at) / 59.94 : realS(f5) - realS(at), rise = realS(f0) - realS(f5);
        printf("  the dart at f%lld: {0A 5} +%lld (%.2f s), {0A 0} +%lld\n", at, f5 - at, awake, f0 - at);
        snprintf(t, sizeof t, "down event %d 0", sl); const long long fe0 = EvAt(t, at + 1);
        if (which == "mash0") Check(LogCount("knocked out by player") == 1 && f5 > at && f0 >= f5 + 64 && f0 <= f5 + 70 && fe0 >= 0 && fe0 <= f5 + 2,
                                    "MGO1_VDOLL_MASH=0: knocked out, awake, its getting up 66 frames with no knocked-out event, up (beta 22)");
        else if (which == "mashslow0") Check(LogCount("knocked out by player") == 1 && f5 > at && f0 >= f5 + 113 && f0 <= f5 + 121, "MGO1_VDOLL_PACE=0: its getting up 120 of the host's frames (mash2's)");
        else if (which == "mashbunch0") Check(LogCount("knocked out by player") == 1 && f5 > at && rise < 1.95, "MGO1_VDOLL_CLOCK_FIX=0: its getting up shorter than a client's 2 s of the real clock (b16's step clock)");
        else Check(LogCount("knocked out by player") == 1 && f5 > at && rise >= 1.95 && rise <= 2.2 && fe0 >= f0 - 2 && fe0 <= f0 + 8,
                   "knocked out, awake, its getting up 2 s of the machine's clock (a client's after a dart's sleep) with its knocked-out event kept to {0A 0}, up");
        if (which == "mash" || which == "mashslow" || which == "mashslow0") Check(awake >= 16.4 && awake <= 21.5, which == "mash" ? "level 10: awake 16.4 to 21.4 s after the dart (9.5 presses a second, the game's rule)"
                                                                        : which == "mashslow" ? "a host slowed to 38 frames a second: awake 16.4 to 21.4 REAL seconds after the dart, as a player at full pace"
                                                                        : "MGO1_VDOLL_PACE=0 on a slowed host: awake 16.4 to 21.4 s of the host's frames at 59.94 (mash2's)");
        if (koModel) {                                   // ((clockfix) its ticks' real seconds against the real clock's)
            const double ticks = LogNum("s knocked out ("), tickS = ticks / vmash::kTicksASecond;
            printf("  awake after %.0f of its ticks (%.2f s of a player's game), the machine's clock %.2f s from the dart to its {0A 5}; its getting up %.2f s\n", ticks, tickS, awake, rise);
            if (which == "mashbunch0") Check(ticks > 0 && awake < tickS - 0.5, "MGO1_VDOLL_CLOCK_FIX=0 on a full pace host whose frames come bunched: awake sooner than the real seconds of its ticks (b16's step clock ran ahead)");
            else Check(ticks > 0 && awake >= 16.4 && awake <= 21.5 && awake >= tickS - 0.05 && awake <= tickS + 0.25,
                       which == "mashquota" ? "a host held by a processor quota: awake after the real seconds of its ticks (16.4 to 21.4 s), as a player"
                                            : "a full pace host whose ticks come unevenly: awake after the real seconds of its ticks (16.4 to 21.4 s), as a player");
        }
        if (which == "mashlow") Check(awake >= 25.0 && awake <= 29.8, "level 1: awake 25.0 to 29.7 s after the dart (4 presses a second, the game's rule)");
        if (which == "mashnone") Check(awake >= 41.6 && awake <= 42.0, "no press (MGO1_VDOLL_MASH_RATE=0): the game's 1250 ticks, 41.7 s");
        if (which == "mash0") Check(awake >= 20.7 && awake <= 21.1 && LogCount("awake after") == 0, "MGO1_VDOLL_MASH=0: beta 22's 144 every 6 frames, 20.9 s, no new line");
        else Check(LogCount("awake after") == 1, "its log says when it woke and how it pressed");
        printf("  its state tick's count while knocked out: %d to %d\n", gKoCountMin[sl], gKoCountMax[sl]);
        if (which == "mash" || which == "mashslow" || which == "mashslow0" || koModel) Check(gKoCountMin[sl] == 8 && gKoCountMax[sl] >= 16 && gKoCountMax[sl] <= 24, "its state tick tells its count as a pressing client's (8, rising to 16 to 24 at 9.5 a second)");
        else Check(gKoCountMin[sl] == 8 && gKoCountMax[sl] <= (which == "mashlow" ? 14 : 8), "its state tick's count: 8 with no press (and beta 22's constant 8), up to 14 at 4 a second");
        if (map) {
            bool still = true; long long firstAfter = -1; float after[3] = {0, 0, 0}, far = 0.f;
            for (const auto& e : G.events) {
                const long long f = atoll(e.c_str() + 1);
                float x, y, z; int s2; const char* pl = strstr(e.c_str(), " place ");
                if (f < at || !pl || sscanf(pl, " place %d %f %f %f", &s2, &x, &y, &z) != 4 || s2 != sl) continue;
                const float dl = std::hypot(x - lying[0], z - lying[2]);
                if (f < f0 && dl > 300.f) still = false;
                if (f >= f0 && firstAfter < 0) { firstAfter = f; after[0] = x; after[1] = y; after[2] = z; }
                if (f >= f0 && f <= f0 + 1800) far = std::max(far, dl);
            }
            printf("  lying at %.0f %.0f %.0f; the first place after {0A 0} %.0f %.0f %.0f (+%lld), %.1f m from it in 30 s\n", lying[0], lying[1], lying[2], after[0], after[1], after[2],
                   firstAfter - f0, far / 1000.f);
            // ((mash2) one line of what must come out the same in every run: the frames and the places)
            printf("  digest: dart f%lld, {0A 5} +%lld (%.2f s), {0A 0} +%lld, count %d to %d, lying %.0f %.0f, first %.0f %.0f +%lld, walked %.0f mm\n", at, f5 - at, awake, f0 - at,
                   gKoCountMin[sl], gKoCountMax[sl], lying[0], lying[2], after[0], after[2], firstAfter - f0, far);
            Check(still, "no place told moves while it is down");
            // ((mash2) its body runs from {0A 0} on: its first place may come a tick or two later, 70 mm a frame at most)
            // ((mash3) on a slowed host its pace runs it farther a frame: 59.94 / 38 times)
            Check(firstAfter >= 0 && std::hypot(after[0] - lying[0], after[2] - lying[2]) < 500.f + 70.f * (h.realMs ? 1.6f : 1.f) * (float)(firstAfter - f0), "up, on from where its copy lies");
            Check(far > 1000.f, "its body walks on after it is up (a metre or more in 30 s)");
        }
    }
    // (v150-host beta 22, mash) a roll's knock-down (kind 9): a real client lies awake 66 to 72 frames after the record and his press
    // then stands him up, {0A 0} 141 frames after it (mash_c2); pressing nothing he lies there. The doll mashes from the record: up
    // at its first press once it lies awake, its getting up 66 frames: level 10 {0A 0} 130 to 160 frames after, level 1 130 to 180,
    // lying awake told {0A 1} (a client's), not {0A 4} (lying knocked out). MGO1_VDOLL_MASH=0 (mashroll0): beta 22's 252
    if (which == "mashroll" || which == "mashrolllow" || which == "mashroll0") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        setenv("MGO1_VDOLL_LEVEL", which == "mashrolllow" ? "1" : "10", 1);
        if (which == "mashroll0") setenv("MGO1_VDOLL_MASH", "0", 1);
        G.rule = 1;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        const int sl = G.peers[ip].slot;
        auto rec = [&](int kind, std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot + kind), 0, -1, d); SendAll(p, ip); };
        long long lo = 1 << 30, hi = 0;
        bool no4 = true, evOk = true;
        for (int k = 0; k < 5; ++k) {
            // ((v150-host beta 24, stamina) 10000 a roll and none of it back while up, as a real client's (mash_c2: 30000, 20000 and
            // 10000 kept 8.6 and 16.9 s, the third roll a knock out): killed after two rolls, full again at its respawn)
            if (k == 2 || k == 4) { rec(0, {0x05, 0, 0}); Run(900); }
            rec(0, {0x05, 200, 0}); Run(1000);           // (its stamina full before the first roll of a life: 10000 a roll)
            const long long at = gNow;
            rec(1, {0x09, a, 0x09, 0x00, 0x10});        // (a roll into it: the roller's reaction record)
            rec(0, {0x04, a, 0, 0, 0x10, 0x27, 0x9C, 0, 0});   // (its stun of 10000: knocked down awake)
            Run(400);
            char t[32];
            snprintf(t, sizeof t, "fall %d 1", sl); const long long g1 = EvAt(t, at);
            snprintf(t, sizeof t, "fall %d 0", sl); const long long g0 = EvAt(t, g1 >= 0 ? g1 : at);
            snprintf(t, sizeof t, "fall %d 4", sl); const long long g4 = EvAt(t, at);
            if (g4 >= 0 && g4 < g0) no4 = false;
            snprintf(t, sizeof t, "down event %d 4", sl); const long long e4 = EvAt(t, at);
            snprintf(t, sizeof t, "down event %d 0", sl); const long long e0 = EvAt(t, e4 >= 0 ? e4 : at);
            if (which != "mashroll0" && !(e4 >= g1 - 2 && e4 <= g1 + 8 && e0 >= g0 - 2 && e0 <= g0 + 8)) evOk = false;
            printf("    its down event +%lld to +%lld\n", e4 - at, e0 - at);
            printf("  a roll at f%lld: {0A 1} +%lld, {0A 0} +%lld\n", at, g1 - at, g0 - at);
            if (g1 >= 0 && g0 > g1) { lo = std::min(lo, g0 - at); hi = std::max(hi, g0 - at); }
        }
        char what[160];
        if (which == "mashroll0") { snprintf(what, sizeof what, "MGO1_VDOLL_MASH=0: up %lld to %lld frames after a roll (beta 22's 252)", lo, hi); Check(lo >= 245 && hi <= 265, what); }
        else {
            snprintf(what, sizeof what, "level %s: up %lld to %lld frames after a roll (its first press once lying awake; a pressing client's 141)", which == "mashroll" ? "10" : "1", lo, hi);
            Check(which == "mashroll" ? lo >= 130 && hi <= 160 : lo >= 130 && hi <= 180, what);
            Check(no4, "lying awake told {0A 1} as a client's, never {0A 4}");
            Check(evOk, "its down event from its landing to {0A 0}, as a client's (none while it falls)");
        }
    }
    // (v150-host beta 22, relay) the host's game reads its sessions every 2 to 7 frames while the dolls tick each frame, and of the
    // datagrams of one doll read in one pass it takes the unreliable records of the last one only (measured: reports/vdoll_relay.md).
    // Three dolls play two minutes of team deathmatch: no doll has two datagrams in one read, every unreliable record of theirs is
    // taken, and their records are taken as often as before (no queue). relay0 (MGO1_VDOLL_RELAY=0, beta 22's way): two datagrams
    // of a doll in one read, the first one's unreliable records left. Beta 22's code FAILS relay
    if (which == "relay" || which == "relay0") {
        setenv("MGO1_VDOLLS", "3", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        if (which == "relay0") setenv("MGO1_VDOLL_RELAY", "0", 1);
        G.rule = 1;
        gReadRule = true;
        vdoll::Init(h);
        RunPaced(1500);
        Check(Count("pick ") >= 3, "the three dolls joined and picked a side");
        gUnrelTaken = gUnrelLost = gTwoInRead = 0; gLostKind.clear();
        const long long from = gNow;
        RunPaced(7200);
        std::string kinds;
        for (const auto& [k, n] : gLostKind) { char b[24]; snprintf(b, sizeof b, " {%02x} %ld", k, n); kinds += b; }
        printf("  2 minutes: the dolls' unreliable records taken %ld, left %ld (%s ), reads with two datagrams of one doll %ld\n", gUnrelTaken, gUnrelLost,
               kinds.empty() ? " none" : kinds.c_str(), gTwoInRead);
        long long worst = 0; double meanAll = 0; int dolls = 0;
        for (const auto& [ip, p] : G.peers) {
            if (p.slot < 0) continue;
            long long most; double mean;
            TakenGaps(p.slot, from, &most, &mean);
            printf("  player %d: its unreliable records taken every %.2f frames, %lld at most\n", p.slot, mean, most);
            worst = std::max(worst, most); meanAll += mean; ++dolls;
        }
        meanAll = dolls ? meanAll / dolls : 0;
        if (which == "relay") {
            Check(gTwoInRead == 0, "no doll has two datagrams in one read of the host's game");
            Check(gUnrelLost == 0 && gUnrelTaken > 10000, "every unreliable record of the dolls taken by the host's game");
            Check(meanAll >= 5.0 && meanAll <= 7.0 && worst <= 15, "their records taken every 5 to 7 frames, never more than 15 apart (no queue)");
            Check(LogCount("kept for the host's game's next read") <= 1, "said once at most in the log");
        } else {
            Check(gTwoInRead > 0 && gUnrelLost > 0, "MGO1_VDOLL_RELAY=0: two datagrams of a doll in one read, the first one's unreliable records left (beta 22's way)");
            Check(LogCount("MGO1_VDOLL_RELAY=0") == 1, "its start line says so");
        }
    }
    // (v150-host beta 22, relay) the sup_s1 case off line, with a disc map (the second argument, never in the repository): a doll's
    // claymore goes off by a real player 40 times, at every phase of its datagrams and of the host's reads; each blast's records
    // ({14}, {04} of 100, {09}) go to his channels. The host's game must take each blast's hit: 40 of 40. relayblast0
    // (MGO1_VDOLL_RELAY=0): beta 22's way, some blasts' hits left unread (as in sup_s1, the real client's life left at 200)
    if (which == "relayblast" || which == "relayblast0") {
        if (argc < 3) { printf("SKIP relayblast wants a disc map (.geom) as its second argument\n"); return 0; }
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        if (which == "relayblast0") setenv("MGO1_VDOLL_RELAY", "0", 1);
        G.rule = 0;
        if (!LoadMap(argv[2])) { printf("FAIL the map %s not read\n", argv[2]); return 1; }
        h.mapNow = MapNowT; h.stageGeom = StageGeomT; h.see = SeeRelay; h.clayBlasts = BlastsT; gFollowCopy = true;
        gReadRule = true;
        vdoll::Init(h);
        RunPaced(900);
        gVictim = AddReal(0);
        G.players[gVictim].kind = 2;
        for (int w = 0; w < 3000 && LogCount("(the bodies' mesh)") == 0; ++w) { RunPaced(2); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        Check(LogCount("(the bodies' mesh)") == 1, "the bodies' mesh built on the map");
        const int sl = G.peers.begin()->second.slot;
        gVictimAt[0] = gSpawnAt[0] + 1000.f; gVictimAt[1] = gSpawnAt[1] + 800.f; gVictimAt[2] = gSpawnAt[2];   // (1 m from it, standing)
        RunPaced(120);
        const long long hits0 = gClayHitsTaken[gVictim], shots0 = gHitsTaken[gVictim] - gClayHitsTaken[gVictim];
        gUnrelLost = gTwoInRead = 0;
        const int kBlasts = 40;
        for (int k = 0; k < kBlasts; ++k) {
            RunPaced(37 + (k * 5) % 11);                 // (every phase against its 100 ms records and the host's reads)
            gBlastsDue.push_back({(uint32_t)((sl << 10) | (0x100 + k)), {gSpawnAt[0], gSpawnAt[1], gSpawnAt[2]}});
            RunPaced(30);
        }
        RunPaced(60);
        const long long taken = gClayHitsTaken[gVictim] - hits0, shots = gHitsTaken[gVictim] - gClayHitsTaken[gVictim] - shots0;
        printf("  %d blasts (%d said in the doll's log): their hits taken by the host's game %lld (its shots' hits taken meanwhile %lld); unreliable records left %ld, reads with two datagrams of the doll %ld\n",
               kBlasts, LogCount("its claymore's blast reaches player"), taken, shots, gUnrelLost, gTwoInRead);
        Check(LogCount("its claymore's blast reaches player") == kBlasts, "each blast reaches the player 1 m from it");
        if (which == "relayblast") Check(taken == kBlasts && gUnrelLost == 0, "every blast's hit taken by the host's game (40 of 40)");
        else Check(taken < kBlasts, "MGO1_VDOLL_RELAY=0: some blasts' hits left unread (beta 22's way, sup_s1)");
    }
    if (which == "blast") {
        setenv("MGO1_VDOLLS", "1", 1);
        setenv("MGO1_VDOLL_PLAY_ALONE", "1", 1);
        G.rule = 0;
        vdoll::Init(h);
        Run(900);
        const int r = AddReal(0);
        G.players[r].kind = 2;
        uint32_t ip = G.peers.begin()->first;
        const uint8_t a = (uint8_t)r;
        auto rec = [&](std::vector<uint8_t> d) { Peer& p = G.peers[ip]; PutRec(p.pending, (uint16_t)(0x40 + 3 * p.slot), 0, -1, d); SendAll(p, ip); };
        rec({0x05, 200, 0}); Run(30);
        // a claymore: {04 layer, 100, no stun, the claymore's code, 0, no head}; the host's life for it 100 after it
        rec({0x04, a, 100, 0, 0, 0, 0x1B, 0, 0}); Run(6);
        rec({0x05, 100, 0}); Run(120);
        Check(Count("death record") == 0, "a claymore's 100 on a life of 200: wounded, not dead");
        Check(LogCount("knocked out") == 0 && LogCount("knocked down") == 0, "a blast's report does not knock it out or down");
        // a second one: its life 0
        rec({0x04, a, 100, 0, 0, 0, 0x1B, 0, 0}); Run(6);
        rec({0x05, 0, 0}); Run(200);
        char want[64]; snprintf(want, sizeof want, "death 1 by %d", r);
        Check(Count(want) == 1 && Count("death 2 by") == 0, "killed by a claymore: a death of kind 1, its killer the claymore's layer");
        Check(Count("death record") == 1, "its death told once");
        char line[64]; snprintf(line, sizeof line, "dead, by player %d", r + 1);
        Check(LogCount(line) == 1 && LogCount("a head shot") == 0, "its log line names the layer, no head shot");
        Run(400);                                      // (back in play)
        // a head hit, then a grenade's blast kills it: the last report counts (no head shot), as in a player's game
        rec({0x05, 200, 0}); Run(30);
        rec({0x04, a, 152, 0, 0, 0, 0x0A, 0, 1}); Run(6);
        rec({0x04, a, 150, 0, 0, 0, 0x13, 0, 0}); Run(6);
        rec({0x05, 0, 0}); Run(200);
        // (the stand-in tells a death record when it changes: the same kind and killer again is the doll's own line)
        Check(LogCount(line) == 2 && LogCount("a head shot") == 0 && Count("death 2 by") == 0, "a grenade's blast after a head hit: a death of kind 1");
    }
    if (which == "pace") {
        struct M { const char* name; int code, magazine, reload, every; } want[] = {
            {"AK-47", 0x0C, 30, 126, 6}, {"XM16E1", 0x0B, 20, 121, 6}, {"SCORPION", 0x0A, 30, 166, 7}, {"M37", 0x0E, 4, 310, 94},
            {"SVD", 0x0F, 10, 192, 16}, {"M1911A1", 0x06, 7, 106, 14}, {"Mk22", 0x05, 8, 166, 68}, {"MOSIN NAGANT", 0x10, 5, 192, 180}};
        for (const M& m : want) {
            vdoll::body::WeaponRow r{}; bool found = false;
            for (int i = 0; vdoll::body::WeaponAt(i, &r); ++i) if (r.code == m.code) { found = true; break; }
            char what[200];
            snprintf(what, sizeof what, "%s: a shot every %d frames (measured %d), %d a magazine (%d), %d frames across a reload (%d)", m.name,
                     found ? r.every : -1, m.every, found ? r.magazine : -1, m.magazine, found ? r.reload : -1, m.reload);
            Check(found && r.every >= m.every && r.magazine <= m.magazine && r.reload >= m.reload, what);
        }
        int ready, release, blast;
        vdoll::body::GrenadeTiming(&ready, &release, &blast);
        Check(blast >= 108, "a grenade's blast 108 frames after its release at the soonest (its copy on a victim's game)");
    }
    // (v150-host beta 19) the live level (docs/VIRTUAL_DOLL.md 19). Off (unset): nothing said, the counters never asked
    if (which == "adaptoff") {
        setenv("MGO1_VDOLLS", "3", 1); setenv("MGO1_VDOLL_LEVEL", "10", 1); unsetenv("MGO1_VDOLL_ADAPT");
        G.rule = 0;
        vdoll::Init(h);
        Run(600);
        const int r = AddReal(0xFF); G.players[r].kind = 0; G.players[r].team = 2;
        Run(3000);
        Check(LogCount("vdoll: level:") == 0 && LogCount("VDOLL ADAPT") == 0 && LogCount("levels now") == 0, "the live level off: no line of it");
        Check(gScoreAsked == 0, "the live level off: the players' counters never asked");
    }
    // a weak real player in a deathmatch against three dolls at 10: their levels come down, never above 10, a step at most 0.35;
    // gone, the levels kept
    if (which == "adapt") {
        setenv("MGO1_VDOLLS", "3", 1); setenv("MGO1_VDOLL_LEVEL", "10", 1); setenv("MGO1_VDOLL_ADAPT", "1", 1);
        G.rule = 0;
        vdoll::Init(h);
        Check(LogCount("vdoll: level: live, between 1 and 10 (the levels set 10,10,10)") == 1, "the live level said at the start");
        Run(1200);
        Check(LogCount("VDOLL ADAPT") == 0, "no real player: nothing judged");
        const int r = AddReal(0xFF); G.players[r].kind = 0;
        Run(400);
        G.players[r].team = 2;
        Run(1200);
        std::vector<int> dolls;
        for (auto& [id, pl] : G.players) if (pl.kind == 2) dolls.push_back(id);
        Check(dolls.size() == 3, "three dolls in the game");
        float lowest = 10.f, highest = 0.f;
        for (int t = 0; t < 36; ++t) {                    // 3 minutes: he dies every 10 s, kills one doll a minute
            gScore[r].second += 1;
            gScore[dolls[t % 3]].first += 1;
            if (t % 6 == 5) { gScore[r].first += 1; gScore[dolls[(t / 6) % 3]].second += 1; }
            Run(600);
            for (float x : LevelsNow()) { lowest = std::min(lowest, x); highest = std::max(highest, x); }
        }
        char what[160];
        snprintf(what, sizeof what, "the levels came down (lowest %.2f, highest %.2f) and never above 10", lowest, highest);
        Check(lowest < 8.f && highest <= 10.f, what);
        Check(LogCount("VDOLL ADAPT: vdoll 1 old=") > 0 && LogCount("reason=") > 0, "the VDOLL ADAPT lines said");
        bool steps = true;
        for (const auto& l : gLog) {
            float o, n;
            if (sscanf(l.c_str(), "VDOLL ADAPT: vdoll %*d old=%f new=%f", &o, &n) == 2 && (n - o > 0.51f || o - n > 0.351f)) steps = false;
        }
        Check(steps, "every step at most half a level up and 0.35 down");
        const std::vector<float> before = LevelsNow();
        const int adaptLines = LogCount("VDOLL ADAPT");
        G.players.erase(r);
        Run(6000);
        Check(LogCount("VDOLL ADAPT") == adaptLines, "no real player: nothing judged more");
        const std::vector<float> after = LevelsNow();
        Check(LogCount("levels now") >= 2, "the levels said again within 60 s (and none moved)");
        bool kept = after.size() == before.size() || after.empty();
        for (size_t i = 0; i < after.size() && i < before.size(); ++i) if (std::fabs(after[i] - before[i]) > 0.001f) kept = false;
        Check(kept, "no real player: the levels kept");
    }
    // MGO1_VDOLL_LIVE's file: the count and the live level's lines taken with no restart; a file with only the count as before
    if (which == "adaptlive" || which == "livecount") {
        unsetenv("MGO1_VDOLL_ADAPT");
        setenv("MGO1_VDOLL_LEVEL", "10,7", 1);
        char path[] = "/tmp/vdoll_live_XXXXXX";
        const int fd = mkstemp(path);
        if (fd >= 0) close(fd);
        WriteFile(path, "2\nadapt 1\nadapt_min 4\nadapt_max 9\n");
        setenv("MGO1_VDOLL_LIVE", path, 1);
        vdoll::Init(h);
        Run(400);
        Check(vdoll::Wanted() == 2, "the count read from a file with the live level's lines");
        if (which == "adaptlive") {
#ifdef VDOLL_HOST_SCORE
            Check(LogCount("vdoll: level: live, between 4 and 9 (the levels set 10,7)") == 1, "the live level on from the file, its bounds");
            int lo = 0, hi = 0;
            Check(vdoll::AdaptOn(&lo, &hi) && lo == 4 && hi == 9, "on, between 4 and 9");
            WriteFile(path, "2\nadapt_max 2\nadapt_min 6\nadapt 1\n");
            Run(300);
            Check(vdoll::AdaptOn(&lo, &hi) && lo == 2 && hi == 6, "the bounds the wrong way round: swapped");
            WriteFile(path, "3\nadapt 0\n");
            Run(300);
            Check(!vdoll::AdaptOn(nullptr, nullptr) && LogCount("vdoll: level: fixed") == 1 && vdoll::Wanted() == 3, "off from the file, the count taken too");
            WriteFile(path, "2\n");
            Run(300);
            Check(!vdoll::AdaptOn(nullptr, nullptr) && vdoll::Wanted() == 2, "a file with only the count: the environment's (off)");
#endif
        }
        unlink(path);
    }
    if (gFails && !getenv("VDOLL_TEST_VERBOSE")) for (const auto& e : G.events) printf("  %s\n", e.c_str());
    return gFails ? 1 : 0;
}
