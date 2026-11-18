// lobby_filter.h - the dedicated host's filter of its TX55 lobby connection, for the accountless
// PLAYABLE DOLLs (docs/PLAYABLE_DOLL.md 1, docs/DEDICATED_HOST.md). A doll logs in to its own lobby
// on the server (host/mgo1-dollgate) with a user id from a reserved range no SaveMGO account has
// (MGO1_DOLL_IDS). The host's reports about it (join 0x4340, leave 0x4342, team 0x4344, kicked 0x4346,
// stats 0x4390) and its entries of the ping report 0x4398 are handled by the listing mode (MGO1_DOLL_LIST,
// List below): kept here (off; their answer, {0, id} or {0} for the stats, put into the receive stream),
// sent with the doll's own id (own), or sent as the host's own account (host). Its statistics and ping
// entries never leave. (v141 beta 7) Unranked (SetUnranked): the other players' statistics leave with a VS rating of 0. (Host) TX-55 runs each join's row addition and each leave's removal on a goroutine of its own
// (pkg/metalgearonline1/handlers/hostgame/player_join.go, player_quit.go: `go AddPlayer`, `go RemovePlayer`), in no
// set order: the host's rows a leave deleted are put back 3 s after the last leave (Due), the dolls' joins and leaves
// meanwhile answered here and counted in those rows.
// TX-55 closes a connection on any gap in the sequence numbers it receives (pkg/konamiserver/
// processor.go, reader: "sequence mismatch"): every frame sent after a kept or added one is renumbered
// and its MD5 made again. The answers put into the receive stream take the next number there, the
// answers to the frames added here are left out, and the lobby's later frames are renumbered the same
// way, so the game reads an unbroken sequence too.
// No platform code: tests/lobby_filter_test.cpp runs it natively.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace lobbyfilter {

// ---- MD5 (RFC 1321) ----
inline void Md5(const uint8_t* msg, size_t len, uint8_t out[16]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    std::vector<uint8_t> m(msg, msg + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    const uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; ++i) m.push_back((uint8_t)(bits >> (8 * i)));
    for (size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)m[o + 4 * i] | (uint32_t)m[o + 4 * i + 1] << 8 | (uint32_t)m[o + 4 * i + 2] << 16 | (uint32_t)m[o + 4 * i + 3] << 24;
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f; int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            const uint32_t t = d;
            d = c; c = b;
            const uint32_t x = a + f + K[i] + w[g];
            b = b + (x << R[i] | x >> (32 - R[i]));
            a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) out[4 * i + k] = (uint8_t)(h[i] >> (8 * k));
}

// ---- frames: {u16 command, u16 length, u32 sequence} big-endian, MD5 of those 8 bytes and the body,
// the body; the whole frame XORed with 5A 70 85 AF from its first byte ----
static const uint8_t kKey[4] = {0x5A, 0x70, 0x85, 0xAF};
struct Frame { uint16_t cmd = 0; uint32_t seq = 0; std::string body; };
inline uint32_t BeU32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline void PutU32(std::string& s, uint32_t v) { for (int k = 3; k >= 0; --k) s += (char)(uint8_t)(v >> (8 * k)); }
inline std::string Encode(const Frame& f) {
    std::string clear;
    clear += (char)(uint8_t)(f.cmd >> 8); clear += (char)(uint8_t)f.cmd;
    clear += (char)(uint8_t)(f.body.size() >> 8); clear += (char)(uint8_t)f.body.size();
    PutU32(clear, f.seq);
    std::string hashed = clear + f.body;
    uint8_t md[16];
    Md5((const uint8_t*)hashed.data(), hashed.size(), md);
    clear.append((const char*)md, 16);
    clear += f.body;
    for (size_t i = 0; i < clear.size(); ++i) clear[i] = (char)((uint8_t)clear[i] ^ kKey[i & 3]);
    return clear;
}
// one whole frame at p (n bytes there): its size, 0 when it is not whole yet
inline size_t Decode(const uint8_t* p, size_t n, Frame& f, bool* md5ok = nullptr) {
    if (n < 24) return 0;
    uint8_t h[24];
    for (int i = 0; i < 24; ++i) h[i] = p[i] ^ kKey[i & 3];
    const size_t len = (size_t)h[2] << 8 | h[3];
    if (n < 24 + len) return 0;
    f.cmd = (uint16_t)(h[0] << 8 | h[1]);
    f.seq = BeU32(h + 4);
    f.body.resize(len);
    for (size_t i = 0; i < len; ++i) f.body[i] = (char)(p[24 + i] ^ kKey[(24 + i) & 3]);
    if (md5ok) {
        std::string hashed((const char*)h, 8);
        hashed += f.body;
        uint8_t md[16];
        Md5((const uint8_t*)hashed.data(), hashed.size(), md);
        *md5ok = !memcmp(md, h + 8, 16);
    }
    return 24 + len;
}

// ---- the dolls' user ids: "first-last" (0x... or decimal); TX-55's ids are its database's, from 1 up. A second range: the
// host's virtual dolls (MGO1_VDOLL_IDS, docs/VIRTUAL_DOLL.md) ----
struct Ids {
    uint32_t first = 0, last = 0;
    uint32_t vfirst = 0, vlast = 0;
    bool In(uint32_t id) const { return (first != 0 && id >= first && id <= last) || (vfirst != 0 && id >= vfirst && id <= vlast); }
};
inline bool ParseIds(const char* text, Ids& out) {
    if (!text || !*text) return false;
    char* end = nullptr;
    const unsigned long a = strtoul(text, &end, 0);
    unsigned long b = a;
    if (end && *end == '-') b = strtoul(end + 1, &end, 0);
    if (!end || *end || a == 0 || b < a || b > 0xFFFFFFFFul) return false;
    out.first = (uint32_t)a; out.last = (uint32_t)b;
    return true;
}

// how the dolls show in TX-55's games (MGO1_DOLL_LIST): Off, their frames kept here (v137); Own, their join, leave, team
// and kick sent with their own ids (TX-55 lists them if its database takes a user id with no account: no foreign key
// game_players.user_id -> users.id); Host, their join and leave sent as the host's own account (always taken: a row of
// the host's user id each, TX-55's list counts them), the host's rows put back after a leave (TX-55 deletes every row
// of that id). Their statistics (0x4390) never leave (no ranking for a doll), nor their entries of the ping report
enum class List { Off, Own, Host };
inline bool ParseList(const char* text, List& out) {
    if (!text || !*text) return false;
    const std::string t = text;
    if (t == "off" || t == "0") out = List::Off;
    else if (t == "own" || t == "1") out = List::Own;
    else if (t == "host") out = List::Host;
    else return false;
    return true;
}
inline const char* ListName(List l) { return l == List::Own ? "own" : l == List::Host ? "host" : "off"; }

// one TX55 connection of the game's, both ways
class Conn {
public:
    explicit Conn(const Ids& ids, List list = List::Off) : ids_(ids), list_(list) {}
    static const long long kRowsAgainMs = 3000;              // (Host) a leave's removal done by then at TX-55
    void SetNow(long long ms) { now_ = ms; }                 // the caller's clock (ms), before Out, In and Due
    // (v141 beta 6) a line a frame each way (MGO1_LOBBY_FILTER_SEQ_LOG=1): its command, its number in the game's
    // sequence and on the wire, the counters that shift them; read with TakeSeqLog
    void SetSeqLog(bool on) { seqLog_ = on; }
    void TakeSeqLog(std::vector<std::string>& out) { for (auto& l : seqLines_) out.push_back(std::move(l)); seqLines_.clear(); }
    // (v141 beta 7) unranked (docs/DEDICATED_HOST.md 11): while on, each player's 0x4390 that leaves goes with its VS rating 0 and its
    // MD5 made again, its number unchanged (TX-55 then keeps it for the game's own table, not for the rankings nor the VS rating); a
    // doll's own report is still kept here. why: the log's reason
    void SetUnranked(bool on, const char* why) { unranked_ = on; unrankedWhy_ = why ? why : ""; }
    bool UnrankedOn() const { return unranked_; }
    static const size_t kStatsBody = 120, kStatsVsAt = 116;  // 0x4390: {u32 user id, HostReportedStats (big-endian)}, its VS rating last
    // (v141 beta 6) the state at a glance (the close's log line): the sequences, the shifts, what is still awaited
    struct DebugCounters {
        uint32_t inLast, outLast, outKept, inAdded, outAdded, inSwallowed;
        uint32_t kept, answered, stripped, rowsAgain, listedSent, swallowed;
        size_t pendingJoins, pendingLeaves, listed;
        uint32_t framesOut, framesIn;                         // frames sent to the lobby, read from it
        uint16_t lastOutCmd, lastInCmd;                       // the last ones each way (0: none)
        uint32_t lastOutWire, lastInWire;                     // their numbers on the wire
        long long lastOutAt, lastInAt;                        // the caller's clock then (-1: none)
        uint32_t unranked, rounds;                            // (v141 beta 7) 0x4390 sent unranked; 0x4392 sent (new rounds)
    };
    DebugCounters Debug() const {
        return DebugCounters{inLast_, outLast_, outKept_, inAdded_, outAdded_, inSwallowed_, kept, answered, stripped, rowsAgain, listedSent,
                             swallowed, joins_.size(), leaves_.size(), listed_.size(), framesOut_, framesIn_, lastOutCmd_, lastInCmd_,
                             lastOutWire_, lastInWire_, lastOutAt_, lastInAt_, unranked, rounds};
    }
    std::string DebugLine() const {
        const DebugCounters d = Debug();
        char b[560];
        snprintf(b, sizeof b, "outLast=%u inLast=%u outKept=%u inAdded=%u outAdded=%u inSwallowed=%u kept=%u answered=%u stripped=%u rowsAgain=%u "
                 "listedSent=%u joinsPending=%zu leavesPending=%zu listed=%zu framesOut=%u framesIn=%u lastOut=%04x/%u lastIn=%04x/%u lastInAgo=%lldms "
                 "unranked=%u rounds=%u",
                 d.outLast, d.inLast, d.outKept, d.inAdded, d.outAdded, d.inSwallowed, d.kept, d.answered, d.stripped, d.rowsAgain, d.listedSent,
                 d.pendingJoins, d.pendingLeaves, d.listed, d.framesOut, d.framesIn, d.lastOutCmd, d.lastOutWire, d.lastInCmd, d.lastInWire,
                 d.lastInAt < 0 ? -1LL : now_ - d.lastInAt, d.unranked, d.rounds);
        return b;
    }
    // the game's bytes for the lobby: the frames to send, whole and renumbered, one string each (one send
    // each: TX-55 reads a frame split across two reads as a sequence error); log: a line a decision
    void Out(const uint8_t* p, size_t n, std::vector<std::string>& send, std::vector<std::string>& log) {
        Due(send, log);                                          // (the host's rows first, when due)
        txRaw_.append((const char*)p, n);
        size_t at = 0;
        for (;;) {
            Frame f;
            const size_t k = Decode((const uint8_t*)txRaw_.data() + at, txRaw_.size() - at, f);
            if (!k) break;
            const std::string raw = txRaw_.substr(at, k);
            at += k;
            char b[220];
            const uint32_t id = f.body.size() >= 4 ? BeU32((const uint8_t*)f.body.data()) : 0;
            // (the host's own user id: its session's log-in on this connection, {u32 id, 16 bytes}; the id alone is read)
            if (f.cmd == 0x3003 && f.body.size() == 20 && !hostId_ && !ids_.In(id)) hostId_ = id;
            const bool player = f.cmd == 0x4340 || f.cmd == 0x4342 || f.cmd == 0x4344 || f.cmd == 0x4346 || f.cmd == 0x4390;
            const bool doll = player && ids_.In(id);
            // (listed: Own sends the join, leave, team and kick; Host the join, and the leave of a doll it listed)
            const bool own = doll && list_ == List::Own && f.cmd != 0x4390;
            const bool host = doll && list_ == List::Host && hostId_ && (f.cmd == 0x4340 || (f.cmd == 0x4342 && listed_.count(id)));
            // (Host, the host's rows deleted by a leave and not put back yet: a doll's join or leave answered here, its
            // row with the rows to come)
            const bool waiting = host && reAddAt_ >= 0;
            if ((doll && !own && !host) || waiting) {
                ++outKept_; ++kept;
                Frame a;
                a.cmd = (uint16_t)(f.cmd + 1);
                PutU32(a.body, 0);
                if (f.cmd != 0x4390) PutU32(a.body, id);           // {result, id}; the stats' answer {result}
                a.seq = ++inLast_;
                ++inAdded_; ++answered;
                rxOut_ += Encode(a);
                if (seqLog_) { SeqLine("OUT", f.cmd, 0, f.seq, "kept here"); SeqLine("IN ", a.cmd, 0, a.seq, "answered here"); }
                if (waiting) {
                    if (f.cmd == 0x4340) listed_.insert(id); else listed_.erase(id);
                    snprintf(b, sizeof b, "lobby filter: %04x for the doll's user id %#x answered here, the host's rows to come (%zu doll%s listed)", f.cmd, id,
                             listed_.size(), listed_.size() == 1 ? "" : "s");
                } else
                    snprintf(b, sizeof b, "lobby filter: %04x for the doll's user id %#x kept here (game sequence %u), its answer %04x {0} put in the receive stream (sequence %u)",
                             f.cmd, id, f.seq, a.cmd, a.seq);
                log.push_back(b);
                continue;
            }
            bool changed = false;
            if (host) {                                              // the doll as the host's own account
                f.body.replace(0, 4, std::string(4, '\0'));
                for (int i = 0; i < 4; ++i) f.body[i] = (char)(uint8_t)(hostId_ >> (24 - 8 * i));
                changed = true; ++listedSent;
                if (f.cmd == 0x4340) listed_.insert(id); else listed_.erase(id);
                snprintf(b, sizeof b, "lobby filter: %04x for the doll's user id %#x sent as the host's own account (%zu doll%s listed)%s", f.cmd, id, listed_.size(),
                         listed_.size() == 1 ? "" : "s", f.cmd == 0x4342 ? ": the host's rows back in 3 s" : "");
                log.push_back(b);
            } else if (own) {
                ++listedSent;
                snprintf(b, sizeof b, "lobby filter: %04x for the doll's user id %#x sent (listed with its own id)", f.cmd, id);
                log.push_back(b);
            }
            // (v141 beta 7) unranked: the report's VS rating 0, its MD5 made again below (the same sequence number)
            if (f.cmd == 0x4390 && unranked_ && f.body.size() == kStatsBody) {
                const uint32_t was = BeU32((const uint8_t*)f.body.data() + kStatsVsAt);
                for (size_t i = 0; i < 4; ++i) f.body[kStatsVsAt + i] = 0;
                changed = true; ++unranked;
                snprintf(b, sizeof b, "lobby filter: 4390 for user id %#x sent unranked (vs_rating 0, was %u: %s)", id, was, unrankedWhy_.c_str());
                log.push_back(b);
            }
            if (f.cmd == 0x4392) ++rounds;
            if (f.cmd == 0x4398 && f.body.size() >= 4) {             // {u32 game ping, {u32 id, u32 ping}...}
                std::string body = f.body.substr(0, 4);
                size_t o = 4;
                int gone = 0;
                for (; o + 8 <= f.body.size(); o += 8) {
                    if (ids_.In(BeU32((const uint8_t*)f.body.data() + o))) { ++gone; continue; }
                    body += f.body.substr(o, 8);
                }
                body += f.body.substr(o);
                if (gone) {
                    f.body = body; changed = true; stripped += gone;
                    if (gone != lastStripped_) {
                        snprintf(b, sizeof b, "lobby filter: 4398 sent without its %d doll entr%s", gone, gone == 1 ? "y" : "ies");
                        log.push_back(b);
                    }
                }
                lastStripped_ = gone;
            }
            // (every join and leave sent: its answer to come, told by the id sent; a doll's answer gets its id back)
            if (f.cmd == 0x4340 || f.cmd == 0x4342) (f.cmd == 0x4340 ? joins_ : leaves_).push_back(Pend{host ? id : 0u, false, host ? hostId_ : id});
            const int64_t shift = (int64_t)outAdded_ - (int64_t)outKept_;
            const uint32_t gameSeq = f.seq;
            if (shift) { f.seq = (uint32_t)((int64_t)f.seq + shift); changed = true; }
            outLast_ = f.seq;
            send.push_back(changed ? Encode(f) : raw);
            Sent(f.cmd, gameSeq, f.seq);
            // (Host: a doll left, TX-55 deletes every row of the host's id: its own and the other dolls' put back by Due)
            if (host && f.cmd == 0x4342) reAddAt_ = now_ + kRowsAgainMs;
        }
        txRaw_.erase(0, at);
    }
    // (Host) the host's rows put back once due (kRowsAgainMs after the last leave): one 0x4340 of its id for itself and
    // each doll listed, their answers left out; called by Out first and by the caller's own clock (the frames to send)
    void Due(std::vector<std::string>& send, std::vector<std::string>& log) {
        if (reAddAt_ < 0 || now_ < reAddAt_ || !hostId_) return;
        reAddAt_ = -1;
        const size_t again = 1 + listed_.size();
        for (size_t r = 0; r < again; ++r) {
            Frame j;
            j.cmd = 0x4340; PutU32(j.body, hostId_);
            j.seq = ++outLast_;
            ++outAdded_; ++rowsAgain;
            joins_.push_back(Pend{0u, true, hostId_});
            send.push_back(Encode(j));
            Sent(j.cmd, 0, j.seq);
        }
        char b[160];
        snprintf(b, sizeof b, "lobby filter: the host's rows put back after the leave (%zu: its own and %zu doll%s)", again, listed_.size(), listed_.size() == 1 ? "" : "s");
        log.push_back(b);
    }
    bool RowsDue() const { return reAddAt_ >= 0; }
    // bytes read from the socket: whole frames go to the game (renumbered after an answer of ours; the answers to the
    // frames of ours left out, a doll's answer with its id again)
    void In(const uint8_t* p, size_t n) {
        rxRaw_.append((const char*)p, n);
        size_t at = 0;
        for (;;) {
            Frame f;
            const size_t k = Decode((const uint8_t*)rxRaw_.data() + at, rxRaw_.size() - at, f);
            if (!k) break;
            bool changed = false;
            const uint32_t wire = f.seq;
            ++framesIn_; lastInCmd_ = f.cmd; lastInWire_ = wire; lastInAt_ = now_;
            if (f.cmd == 0x4341 || f.cmd == 0x4343) {
                std::deque<Pend>& q = f.cmd == 0x4341 ? joins_ : leaves_;
                if (!q.empty()) {
                    // (TX-55 handles each frame on a goroutine of its own: the answers of frames read together come in any
                    // order. An answer takes the first one sent with the id it names, else the first one)
                    size_t i = 0;
                    if (f.body.size() >= 8) {
                        const uint32_t named = BeU32((const uint8_t*)f.body.data() + 4);
                        while (i < q.size() && q[i].wire != named) ++i;
                        if (i == q.size()) i = 0;
                    }
                    const Pend w = q[i];
                    q.erase(q.begin() + (long)i);
                    if (w.ours) {
                        ++inSwallowed_; ++swallowed; at += k;
                        if (seqLog_) SeqLine("IN ", f.cmd, wire, 0, "swallowed (an answer to a frame of the filter's)");
                        continue;
                    }
                    if (w.doll && f.body.size() >= 8) {
                        for (int i = 0; i < 4; ++i) f.body[4 + i] = (char)(uint8_t)(w.doll >> (24 - 8 * i));
                        changed = true;
                    }
                }
            }
            const int64_t shift = (int64_t)inAdded_ - (int64_t)inSwallowed_;
            if (shift) { f.seq = (uint32_t)((int64_t)f.seq + shift); changed = true; }
            if (changed) rxOut_ += Encode(f);
            else rxOut_.append(rxRaw_, at, k);
            inLast_ = f.seq;
            if (seqLog_) SeqLine("IN ", f.cmd, wire, f.seq, nullptr);
            at += k;
        }
        rxRaw_.erase(0, at);
    }
    size_t Ready() const { return rxOut_.size() - rxPos_; }
    size_t Take(uint8_t* out, size_t n, bool peek) {
        const size_t k = n < Ready() ? n : Ready();
        memcpy(out, rxOut_.data() + rxPos_, k);
        if (!peek) {
            rxPos_ += k;
            if (rxPos_ == rxOut_.size()) { rxOut_.clear(); rxPos_ = 0; }
            else if (rxPos_ > 65536) { rxOut_.erase(0, rxPos_); rxPos_ = 0; }
        }
        return k;
    }
    bool HostKnown() const { return hostId_ != 0; }
    size_t Listed() const { return listed_.size(); }
    // frames kept here, answers given, ping entries left out; (listing) doll frames sent, the host's rows put back, their
    // answers left out
    uint32_t kept = 0, answered = 0, stripped = 0, listedSent = 0, rowsAgain = 0, swallowed = 0;
    uint32_t unranked = 0, rounds = 0;                       // (v141 beta 7) 0x4390 sent unranked; 0x4392 sent (new rounds)
private:
    void Sent(uint16_t cmd, uint32_t gameSeq, uint32_t wire) {
        ++framesOut_; lastOutCmd_ = cmd; lastOutWire_ = wire; lastOutAt_ = now_;
        if (seqLog_) SeqLine("OUT", cmd, wire, gameSeq, gameSeq ? nullptr : "added here");
    }
    // OUT: wire the number sent, game the game's own; IN: wire the number read, game the one the game is given
    void SeqLine(const char* way, uint16_t cmd, uint32_t wire, uint32_t game, const char* what) {
        char b[220];
        snprintf(b, sizeof b, "lobby seq %s cmd=%04x game=%u wire=%u outKept=%u outAdded=%u inAdded=%u inSwallowed=%u%s%s", way, cmd, game, wire,
                 outKept_, outAdded_, inAdded_, inSwallowed_, what ? ": " : "", what ? what : "");
        seqLines_.push_back(b);
    }
    struct Pend { uint32_t doll; bool ours; uint32_t wire; }; // an answer to come: a doll's id to give back, or one of ours; the id sent
    Ids ids_;
    List list_;
    uint32_t hostId_ = 0;
    std::set<uint32_t> listed_;                              // (Host) the dolls with a row of the host's id
    long long now_ = 0, reAddAt_ = -1;                       // (Host) the caller's clock; when the host's rows go back (-1: none due)
    std::deque<Pend> joins_, leaves_;                        // the joins and leaves sent, their answers to come
    std::string txRaw_, rxRaw_, rxOut_;                      // the game's bytes not yet a frame; the socket's; for the game
    size_t rxPos_ = 0;
    uint32_t inLast_ = 0;                                    // the last sequence number the game was given
    uint32_t outLast_ = 0;                                   // the last one sent to the lobby
    uint32_t outKept_ = 0, inAdded_ = 0, outAdded_ = 0, inSwallowed_ = 0;
    int lastStripped_ = 0;
    bool seqLog_ = false;
    std::vector<std::string> seqLines_;
    uint32_t framesOut_ = 0, framesIn_ = 0, lastOutWire_ = 0, lastInWire_ = 0;
    uint16_t lastOutCmd_ = 0, lastInCmd_ = 0;
    long long lastOutAt_ = -1, lastInAt_ = -1;
    bool unranked_ = false;                                  // (v141 beta 7) the platform's switch, and its reason
    std::string unrankedWhy_;
};

}  // namespace lobbyfilter
