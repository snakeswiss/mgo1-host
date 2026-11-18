// The dedicated host's lobby filter for the accountless dolls (MGO1Loader/lobby_filter.h), off line:
// the frames, MD5, the frames kept here and their answers, the sequence numbers both ways, the ping
// report without the dolls. With --live <host:port> it also plays a host's lobby session through the
// filter against tests/fake_gate.py in its TX-55 mode (FAKE_TX55_STRICT=1): the lobby must answer every
// frame and never close the connection (a sequence gap closes it, as TX-55 does).
// (v141 beta 6) The soak: a long session of each listing mode against a strict TX-55 (500 cycles a mode, a few seconds;
// --soak <cycles> [seed] for another length or another run).
//   g++ -std=c++17 -O1 -o /tmp/lobby_filter_test tests/lobby_filter_test.cpp && /tmp/lobby_filter_test
//   FAKE_TX55_STRICT=1 FAKE_GATE_PORT=15731 FAKE_LOBBY_PORT=15732 python3 tests/fake_gate.py SNAKE pass1234 &
//   /tmp/lobby_filter_test --live 127.0.0.1:15732 [off|own|host]     (FAKE_DOLL_LISTED=<the same> for the stand-in)
#include "../MGO1Loader/lobby_filter.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

using namespace lobbyfilter;

static int gFails = 0;
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s\n", ok ? "ok  " : "FAIL", what, ok || detail.empty() ? "" : ("  (" + detail + ")").c_str());
    if (!ok) ++gFails;
}
static std::string Hex(const uint8_t* p, size_t n) { std::string s; char b[4]; for (size_t i = 0; i < n; ++i) { snprintf(b, sizeof b, "%02x", p[i]); s += b; } return s; }
static std::string U32(uint32_t v) { std::string s; PutU32(s, v); return s; }
static std::string Fr(uint16_t cmd, uint32_t seq, const std::string& body) { Frame f; f.cmd = cmd; f.seq = seq; f.body = body; return Encode(f); }
// every whole frame of a byte stream
static std::vector<Frame> Frames(const std::string& s, bool* allMd5 = nullptr) {
    std::vector<Frame> v;
    size_t at = 0;
    if (allMd5) *allMd5 = true;
    for (;;) {
        Frame f; bool ok = false;
        const size_t k = Decode((const uint8_t*)s.data() + at, s.size() - at, f, &ok);
        if (!k) break;
        if (allMd5 && !ok) *allMd5 = false;
        v.push_back(f);
        at += k;
    }
    return v;
}

static void Offline() {
    // MD5 (RFC 1321's test suite)
    uint8_t md[16];
    Md5((const uint8_t*)"", 0, md);
    Check("md5 of nothing", Hex(md, 16) == "d41d8cd98f00b204e9800998ecf8427e", Hex(md, 16));
    Md5((const uint8_t*)"abc", 3, md);
    Check("md5 of abc", Hex(md, 16) == "900150983cd24fb0d6963f7d28e17f72", Hex(md, 16));
    const char* ln = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    Md5((const uint8_t*)ln, strlen(ln), md);
    Check("md5 of 80 digits", Hex(md, 16) == "57edf4a22be3c955ac49da2e2107b67a", Hex(md, 16));
    // a frame as fake_gate.py makes it (0x0005, seq 7): xor(header + md5(header) + body)
    const std::string ka = Fr(0x0005, 7, "");
    Check("a keep-alive frame", Hex((const uint8_t*)ka.data(), 8) == "5a7585af5a7085a8", Hex((const uint8_t*)ka.data(), 8));
    Frame f; bool ok = false;
    const std::string sf = Fr(0x4344, 9, U32(0x1310) + std::string(1, '\x01'));
    Check("a frame decoded", Decode((const uint8_t*)sf.data(), sf.size(), f, &ok) == sf.size() && ok && f.cmd == 0x4344 && f.seq == 9 && f.body.size() == 5);
    Check("a part of a frame is not one", Decode((const uint8_t*)sf.data(), sf.size() - 1, f) == 0);
    Ids ids;
    Check("the ids parsed", ParseIds("0x7F000001-0x7F000010", ids) && ids.first == 0x7F000001 && ids.last == 0x7F000010);
    Check("a bad range refused", !ParseIds("0x10-0x1", ids) && !ParseIds("", ids) && !ParseIds("12x", ids));
    ParseIds("0x7F000001-0x7F000010", ids);

    // the host's lobby session: the log-in, its game, a real player (0x1310) and a doll (0x7F000003)
    const uint32_t human = 0x1310, doll = 0x7F000003, doll2 = 0x7F000004;
    Conn c(ids);
    std::vector<std::string> send, log;
    std::string game;                                        // the game's frames, as it sent them
    uint32_t gseq = 0;
    auto out = [&](uint16_t cmd, const std::string& body) { const std::string x = Fr(cmd, ++gseq, body); game += x; c.Out((const uint8_t*)x.data(), x.size(), send, log); };
    std::string lobby;                                       // the lobby's frames, numbered by the lobby
    uint32_t lseq = 0;
    std::string seen;                                        // what the game reads
    auto in = [&](uint16_t cmd, const std::string& body, size_t chunk) {
        const std::string x = Fr(cmd, ++lseq, body);
        for (size_t o = 0; o < x.size(); o += chunk) c.In((const uint8_t*)x.data() + o, std::min(chunk, x.size() - o));
    };
    auto read = [&](size_t n) { std::string b(n, '\0'); const size_t k = c.Take((uint8_t*)&b[0], n, false); b.resize(k); seen += b; return k; };
    out(0x3003, U32(human) + std::string(16, 'T'));
    in(0x3004, U32(0), 3);
    out(0x4310, std::string(262, 'G'));
    in(0x4311, U32(0), 1);
    out(0x4340, U32(human));
    in(0x4341, U32(0) + U32(human), 24);
    out(0x4340, U32(doll));                                  // a doll joins: kept here, answered here
    out(0x4344, U32(doll) + std::string(1, '\0'));
    out(0x4344, U32(human) + std::string(1, '\x01'));
    in(0x4345, U32(0) + U32(human), 7);
    out(0x4398, U32(40) + U32(human) + U32(31) + U32(doll) + U32(12) + U32(doll2) + U32(9));
    in(0x4399, U32(0), 100);
    out(0x4390, U32(doll) + std::string(116, '\0'));
    out(0x4390, U32(human) + std::string(116, '\0'));
    in(0x4391, U32(0), 5);
    out(0x4398, U32(41) + U32(doll) + U32(12));
    in(0x4399, U32(0), 2);
    out(0x4342, U32(doll));
    out(0x0005, "");
    in(0x0005, "", 24);
    while (read(24) == 24) {                                 // as the game reads: the header, then the body
        Frame h;
        uint8_t hd[24];
        memcpy(hd, seen.data() + seen.size() - 24, 24);
        for (int i = 0; i < 24; ++i) hd[i] ^= kKey[i & 3];
        const size_t blen = (size_t)hd[2] << 8 | hd[3];
        if (blen) read(blen);
        (void)h;
    }
    // what the lobby got: every frame of the game's but the doll's, renumbered 1.. with good MD5s
    std::string sent;
    for (const auto& s : send) sent += s;
    bool md5s = false;
    const auto sv = Frames(sent, &md5s);
    std::string cmds;
    bool seqs = true, noDoll = true;
    for (size_t i = 0; i < sv.size(); ++i) {
        char b[16]; snprintf(b, sizeof b, "%s%04x", i ? " " : "", sv[i].cmd); cmds += b;
        if (sv[i].seq != i + 1) seqs = false;
        for (size_t o = 0; o + 4 <= sv[i].body.size(); o += 4) {
            const uint32_t v = BeU32((const uint8_t*)sv[i].body.data() + o);
            if (ids.In(v)) noDoll = false;
        }
    }
    Check("the lobby got the real frames only", cmds == "3003 4310 4340 4344 4398 4390 4398 0005", cmds);
    Check("one send a frame", send.size() == 8, std::to_string(send.size()));
    Check("numbered 1 to 8 without a gap", seqs);
    Check("their MD5s good", md5s);
    Check("no doll's id in them", noDoll);
    Check("the first ping report without the dolls", sv.size() > 4 && sv[4].body == U32(40) + U32(human) + U32(31), sv.size() > 4 ? Hex((const uint8_t*)sv[4].body.data(), sv[4].body.size()) : "");
    Check("the second one with the game's ping only", sv.size() > 6 && sv[6].body == U32(41));
    Check("frames before the first doll's sent as they were", send.size() > 2 && send[0] == Fr(0x3003, 1, U32(human) + std::string(16, 'T')) && send[2] == Fr(0x4340, 3, U32(human)));
    // what the game read: the lobby's frames and the answers given here, numbered 1.. without a gap
    const auto gv = Frames(seen, &md5s);
    cmds.clear(); seqs = true;
    for (size_t i = 0; i < gv.size(); ++i) {
        char b[16]; snprintf(b, sizeof b, "%s%04x", i ? " " : "", gv[i].cmd); cmds += b;
        if (gv[i].seq != i + 1) seqs = false;
    }
    Check("the game read the lobby's frames and the answers", cmds == "3004 4311 4341 4341 4345 4345 4399 4391 4391 4399 4343 0005", cmds);
    Check("the game's frames numbered 1 to 12 without a gap", seqs && gv.size() == 12);
    Check("the game's MD5s good", md5s);
    Check("the doll's join answered {0, id}", gv.size() > 3 && gv[3].body == U32(0) + U32(doll));
    Check("its team answered {0, id}", gv.size() > 4 && gv[4].body == U32(0) + U32(doll));
    Check("its stats answered {0}", gv.size() > 7 && gv[7].body == U32(0));
    Check("its leave answered {0, id}", gv.size() > 10 && gv[10].cmd == 0x4343 && gv[10].body == U32(0) + U32(doll));
    Check("the lobby's own answers unchanged", gv.size() > 5 && gv[5].body == U32(0) + U32(human) && gv[8].body == U32(0));
    Check("the counters", c.kept == 4 && c.answered == 4 && c.stripped == 3, std::to_string(c.kept) + " " + std::to_string(c.answered) + " " + std::to_string(c.stripped));
    Check("its log names the frames kept", log.size() == 6 && log[0].find("4340") != std::string::npos && log[0].find("0x7f000003") != std::string::npos, log.empty() ? "" : log[0]);
    // a peek leaves the bytes; a game frame split across two sends waits for its end
    Conn c2(ids);
    const std::string x = Fr(0x4100, 1, "");
    c2.In((const uint8_t*)x.data(), x.size());
    uint8_t b[64];
    Check("a peek leaves the bytes", c2.Take(b, 8, true) == 8 && c2.Ready() == 24);
    std::vector<std::string> s2, l2;
    const std::string y = Fr(0x4340, 1, U32(doll)), z = Fr(0x4344, 2, U32(human) + std::string(1, '\0'));
    c2.Out((const uint8_t*)y.data(), 10, s2, l2);
    Check("a part of a frame not sent yet", s2.empty());
    c2.Out((const uint8_t*)y.data() + 10, y.size() - 10, s2, l2);
    c2.Out((const uint8_t*)z.data(), z.size(), s2, l2);
    Check("then kept, the next one renumbered", s2.size() == 1 && Frames(s2[0]).size() == 1 && Frames(s2[0])[0].seq == 1 && Frames(s2[0])[0].cmd == 0x4344);
    Check("the answer after the frame waiting", c2.Ready() == 24 + 24 + 8);
    // without dolls nothing changes, byte for byte
    Conn c3(ids);
    std::vector<std::string> s3, l3;
    std::string all;
    for (uint32_t k = 1; k <= 5; ++k) { const std::string w = Fr(0x4344, k, U32(human) + std::string(1, '\x01')); all += w; c3.Out((const uint8_t*)w.data(), w.size(), s3, l3); }
    std::string back;
    for (const auto& s : s3) back += s;
    Check("without a doll the frames go as they are", back == all && l3.empty());

    // (virtual dolls) a second range, the host's virtual dolls (MGO1_VDOLL_IDS): their frames handled as the accountless dolls'
    Ids both = ids;
    both.vfirst = 0x7F000041; both.vlast = 0x7F000042;
    Check("the second range: a virtual doll's id a doll's, an id between them not", both.In(0x7F000041) && both.In(0x7F000042) && both.In(0x7F000003) &&
          !both.In(0x7F000043) && !both.In(0x7F000020) && !ids.In(0x7F000041));
    Conn c4(both);
    std::vector<std::string> s4, l4;
    const std::string v1 = Fr(0x3003, 1, U32(human) + std::string(16, 'T')) + Fr(0x4340, 2, U32(0x7F000041)) + Fr(0x4344, 3, U32(0x7F000041) + std::string(1, '\x01')) +
                           Fr(0x4390, 4, U32(0x7F000042) + std::string(116, '\0')) + Fr(0x4340, 5, U32(human));
    c4.Out((const uint8_t*)v1.data(), v1.size(), s4, l4);
    std::string cmds4;
    for (const auto& x : s4) for (const auto& f : Frames(x)) { char b[8]; snprintf(b, sizeof b, "%s%04x", cmds4.empty() ? "" : " ", f.cmd); cmds4 += b; }
    Check("virtual dolls' join, team and stats kept here (off), the real player's join sent", cmds4 == "3003 4340" && c4.kept == 3, cmds4);
}

// (v141 beta 3) the dolls listed (MGO1_DOLL_LIST own and host) against a small TX-55: it answers every frame in order
// ({0, id} for the player reports, {0} else), keeps the game's rows as its database does (0x4310 adds the host's, 0x4340
// adds one, 0x4342 deletes every row of that id) and checks the sequence numbers it gets
// (TX-55 runs each join's row addition and each leave's removal on a goroutine: `go AddPlayer`, `go RemovePlayer`. Go
// puts a new goroutine first in line (its runnext slot) and the one there before at the end of the queue: with the
// frames of one read handled at once, the last one made runs first. Lifo: the row operations of one Got so)
struct MiniTx55 {
    std::vector<uint32_t> rows;
    uint32_t hostId = 0, next = 1, lseq = 0;
    bool gap = false, lifo = false;
    Ids dolls;
    std::string dollFrames;                                  // the frames it got naming a doll (the player reports, the ping entries)
    std::vector<std::string> answers;                        // the lobby's frames, in order
    std::string cmds;                                        // what it got
    void Got(const std::string& wire) {
        std::vector<std::pair<uint16_t, uint32_t>> ops;          // the row operations of this read, in order
        for (const Frame& f : Frames(wire)) {
            if (f.seq != next) gap = true;
            next = f.seq + 1;
            char b[16]; snprintf(b, sizeof b, "%s%04x", cmds.empty() ? "" : " ", f.cmd); cmds += b;
            const uint32_t id = f.body.size() >= 4 ? BeU32((const uint8_t*)f.body.data()) : 0;
            std::string body = U32(0);
            if (f.cmd == 0x3003) hostId = id;
            if (f.cmd >= 0x4340 && f.cmd <= 0x4390 && dolls.In(id)) { snprintf(b, sizeof b, " %04x", f.cmd); dollFrames += b; }
            for (size_t o = 4; f.cmd == 0x4398 && o + 8 <= f.body.size(); o += 8)
                if (dolls.In(BeU32((const uint8_t*)f.body.data() + o))) dollFrames += " 4398";
            if (f.cmd == 0x4310) rows.push_back(hostId);
            if (f.cmd == 0x4340 || f.cmd == 0x4342) ops.push_back({f.cmd, id});
            if (f.cmd == 0x4340 || f.cmd == 0x4342 || f.cmd == 0x4344 || f.cmd == 0x4346) body += U32(id);
            answers.push_back(Fr((uint16_t)(f.cmd + 1), ++lseq, body));
        }
        if (lifo && ops.size() > 1) std::rotate(ops.begin(), ops.end() - 1, ops.end());   // (the last one first)
        for (const auto& op : ops) {
            if (op.first == 0x4340) rows.push_back(op.second);
            else { std::vector<uint32_t> keep; for (uint32_t r : rows) if (r != op.second) keep.push_back(r); rows = keep; }
        }
    }
    size_t Count(uint32_t id) const { size_t n = 0; for (uint32_t r : rows) n += r == id; return n; }
};
static void OfflineList() {
    Ids ids; ParseIds("0x7F000001-0x7F000040", ids);
    const uint32_t host = 0x1200, human = 0x1310, d1 = 0x7F000001, d2 = 0x7F000002;
    List l = List::Off;
    Check("the listing modes parsed", ParseList("own", l) && l == List::Own && ParseList("host", l) && l == List::Host && ParseList("off", l) && l == List::Off &&
                                      !ParseList("all", l) && !ParseList("", l));
    for (int mode = 0; mode < 2; ++mode) {
        const bool own = mode == 0;
        const char* tag = own ? "own" : "host";
        Conn c(ids, own ? List::Own : List::Host);
        MiniTx55 tx;
        tx.dolls = ids;
        tx.lifo = true;
        std::vector<std::string> log;
        uint32_t gseq = 0;
        long long now = 0;
        std::string seen;
        // what the filter sends goes in one read (TX-55's goroutines of that read in their order), the lobby answers, the
        // game reads it all
        auto deliver = [&](const std::vector<std::string>& send) {
            std::string wire;
            for (const auto& w : send) wire += w;
            const size_t before = tx.answers.size();
            if (!wire.empty()) tx.Got(wire);
            for (size_t i = before; i < tx.answers.size(); ++i) c.In((const uint8_t*)tx.answers[i].data(), tx.answers[i].size());
            uint8_t b[512];
            for (size_t k; (k = c.Take(b, sizeof b, false)) > 0;) seen.append((const char*)b, k);
        };
        // the game sends a frame (0.1 s later)
        auto step = [&](uint16_t cmd, const std::string& body) {
            std::vector<std::string> send;
            const std::string x = Fr(cmd, ++gseq, body);
            c.SetNow(now += 100);
            c.Out((const uint8_t*)x.data(), x.size(), send, log);
            deliver(send);
        };
        // the platform's select, ms later: what is due
        auto tick = [&](long long ms) {
            std::vector<std::string> send;
            c.SetNow(now += ms);
            c.Due(send, log);
            deliver(send);
        };
        step(0x3003, U32(host) + std::string(16, 'T'));
        step(0x4310, std::string(262, 'G'));
        step(0x4340, U32(human));
        step(0x4340, U32(d1));
        step(0x4344, U32(d1) + std::string(1, '\0'));
        step(0x4340, U32(d2));
        step(0x4344, U32(human) + std::string(1, '\x01'));
        step(0x4398, U32(30) + U32(d1) + U32(10) + U32(human) + U32(44) + U32(d2) + U32(11));
        step(0x4390, U32(d1) + std::string(116, '\0'));
        const size_t rowsTwo = tx.rows.size();
        step(0x4342, U32(d1));
        const size_t rowsGone = tx.rows.size();
        const bool due = c.RowsDue();
        tick(1000);
        const bool early = tx.rows.size() == rowsGone;
        tick(2000);
        const size_t rowsOne = tx.rows.size();
        step(0x4344, U32(human) + std::string(1, '\0'));
        step(0x4342, U32(d2));
        tick(3000);
        step(0x0005, "");
        bool md5s = false;
        const auto gv = Frames(seen, &md5s);
        bool seqs = true;
        std::string gcmds;
        for (size_t i = 0; i < gv.size(); ++i) {
            if (gv[i].seq != i + 1) seqs = false;
            char b[16]; snprintf(b, sizeof b, "%s%04x", i ? " " : "", gv[i].cmd); gcmds += b;
        }
        const std::string want = "3004 4311 4341 4341 4345 4341 4345 4399 4391 4343 4345 4343 0006";
        Check((std::string(tag) + ": the game read one answer a frame, in order").c_str(), gcmds == want, gcmds);
        Check((std::string(tag) + ": its frames numbered without a gap, MD5s good").c_str(), seqs && md5s);
        Check((std::string(tag) + ": the lobby saw no sequence gap").c_str(), !tx.gap);
        auto ans = [&](size_t i) { return i < gv.size() && gv[i].body.size() >= 8 ? BeU32((const uint8_t*)gv[i].body.data() + 4) : 0u; };
        Check((std::string(tag) + ": each doll's answers name the doll").c_str(), ans(3) == d1 && ans(4) == d1 && ans(5) == d2 && ans(9) == d1 && ans(11) == d2,
              std::to_string(ans(3)) + " " + std::to_string(ans(9)));
        Check((std::string(tag) + ": the real player's answers name him").c_str(), ans(2) == human && ans(6) == human && ans(10) == human);
        Check((std::string(tag) + ": the dolls' stats never sent, nor their pings").c_str(), tx.cmds.find("4390") == std::string::npos && c.stripped == 2);
        if (own) {
            Check("own: the lobby got the dolls' joins, teams and leaves with their ids", tx.cmds == "3003 4310 4340 4340 4344 4340 4344 4398 4342 4344 4342 0005", tx.cmds);
            Check("own: the game's rows: the host, the player and the two dolls, then one doll, then none", rowsTwo == 4 && rowsOne == 3 && tx.rows.size() == 2 &&
                  tx.Count(d1) == 0 && tx.Count(d2) == 0, std::to_string(rowsTwo) + " " + std::to_string(rowsOne) + " " + std::to_string(tx.rows.size()));
        } else {
            Check("host: the host's id known from its log-in", c.HostKnown());
            Check("host: no doll's id ever reached the lobby", tx.dollFrames.empty(), tx.dollFrames);
            Check("host: the lobby got the joins as the host's, the leave then the host's rows again",
                  tx.cmds == "3003 4310 4340 4340 4340 4344 4398 4342 4340 4340 4344 4342 4340 0005", tx.cmds);
            Check("host: the game's rows: the host's three (its own and two dolls), then two, then its own", rowsTwo == 4 && rowsOne == 3 && tx.Count(host) == 1 &&
                  tx.Count(human) == 1 && tx.rows.size() == 2, std::to_string(rowsTwo) + " " + std::to_string(rowsOne) + " " + std::to_string(tx.rows.size()));
            Check("host: after a leave the host's rows gone, put back 3 s later (not after 1 s)", due && rowsGone == 1 && early && !c.RowsDue(),
                  std::to_string(rowsGone));
            Check("host: the answers to its own frames left out", c.swallowed == 3 && c.rowsAgain == 3, std::to_string(c.swallowed) + " " + std::to_string(c.rowsAgain));
        }
        if (own) Check("own: only the dolls' joins, teams and leaves named them", tx.dollFrames == " 4340 4344 4340 4342 4342", tx.dollFrames);
        size_t said = 0;
        for (const auto& l : log) said += l.find(own ? "listed with its own id" : "sent as the host's own account") != std::string::npos;
        Check((std::string(tag) + ": the log says each doll frame sent").c_str(), said == (own ? 5u : 4u), std::to_string(said));
    }
    {   // host mode: dolls joining and leaving while the host's rows are due, answered here, counted in those rows
        Conn c(ids, List::Host);
        MiniTx55 tx;
        tx.dolls = ids; tx.lifo = true;
        std::vector<std::string> log;
        uint32_t gseq = 0;
        long long now = 0;
        std::string seen;
        const uint32_t d3 = 0x7F000003;
        auto deliver = [&](const std::vector<std::string>& send) {
            std::string wire;
            for (const auto& w : send) wire += w;
            const size_t before = tx.answers.size();
            if (!wire.empty()) tx.Got(wire);
            for (size_t i = before; i < tx.answers.size(); ++i) c.In((const uint8_t*)tx.answers[i].data(), tx.answers[i].size());
            uint8_t b[512];
            for (size_t k; (k = c.Take(b, sizeof b, false)) > 0;) seen.append((const char*)b, k);
        };
        auto step = [&](uint16_t cmd, const std::string& body) {
            std::vector<std::string> send;
            const std::string x = Fr(cmd, ++gseq, body);
            c.SetNow(now += 100);
            c.Out((const uint8_t*)x.data(), x.size(), send, log);
            deliver(send);
        };
        step(0x3003, U32(host) + std::string(16, 'T'));
        step(0x4310, std::string(262, 'G'));
        step(0x4340, U32(d1));
        step(0x4342, U32(d1));
        step(0x4340, U32(d2));
        step(0x4340, U32(d3));
        step(0x4342, U32(d3));
        now += 3000;
        step(0x0005, "");                                        // (the game's next frame, 3 s on: the rows go first)
        bool md5s = false;
        const auto gv = Frames(seen, &md5s);
        bool seqs = true;
        std::string gcmds;
        for (size_t i = 0; i < gv.size(); ++i) {
            if (gv[i].seq != i + 1) seqs = false;
            char b[16]; snprintf(b, sizeof b, "%s%04x", i ? " " : "", gv[i].cmd); gcmds += b;
        }
        auto ans = [&](size_t i) { return i < gv.size() && gv[i].body.size() >= 8 ? BeU32((const uint8_t*)gv[i].body.data() + 4) : 0u; };
        Check("host, rows due: the game read each answer in order, numbered without a gap", gcmds == "3004 4311 4341 4343 4341 4341 4343 0006" && seqs && md5s, gcmds);
        Check("host, rows due: each doll's answer names it", ans(2) == d1 && ans(3) == d1 && ans(4) == d2 && ans(5) == d3 && ans(6) == d3);
        Check("host, rows due: the lobby got the join, the leave, then the host's two rows with the next frame", tx.cmds == "3003 4310 4340 4342 4340 4340 0005" && !tx.gap,
              tx.cmds);
        Check("host, rows due: the game's rows the host's own and doll 2's", tx.Count(host) == 2 && tx.rows.size() == 2 && c.Listed() == 1,
              std::to_string(tx.rows.size()));
        size_t here = 0;
        for (const auto& l : log) here += l.find("answered here, the host's rows to come") != std::string::npos;
        Check("host, rows due: the log says the frames answered here", here == 3, std::to_string(here));
    }
    {   // why the rows wait: sent with the leave, TX-55's goroutines in their order delete one of them
        MiniTx55 tx;
        tx.lifo = true; tx.hostId = host;
        tx.Got(Fr(0x4310, 1, std::string(262, 'G')));
        tx.Got(Fr(0x4340, 2, U32(host)));
        tx.Got(Fr(0x4340, 3, U32(host)));
        tx.Got(Fr(0x4342, 4, U32(host)) + Fr(0x4340, 5, U32(host)) + Fr(0x4340, 6, U32(host)));
        Check("TX-55's order: the rows sent with the leave, one of the two lost", tx.Count(host) == 1, std::to_string(tx.Count(host)));
    }
    // host mode before the host's id is known: the dolls kept here as before
    Conn c(ids, List::Host);
    std::vector<std::string> send, log;
    const std::string x = Fr(0x4340, 1, U32(d1));
    c.Out((const uint8_t*)x.data(), x.size(), send, log);
    Check("host mode, the host's id not known: the doll's join kept here", send.empty() && c.kept == 1 && c.Ready() == 32);
}

// ---- (v141 beta 6) the soak: a live room over a long session, each listing mode, against a strict TX-55 ----
// TX-55 as its source has it (pkg/konamiserver/processor.go, client.go, hooking.go; metalgearonline1/handlers): the
// connection closed on a sequence gap, a bad MD5 or a read ending inside a frame (a read takes 1024 bytes at most); each
// frame of a read handled on a goroutine of its own (`go c.dispatch`), so the answers of one read leave in any order
// (shuffled here), numbered one after the other by its writer; 0x0005 answered 0x0005 {} (general/ping.go). The host that
// lost its lobby after hours had 3 accountless dolls restarted every ~4 minutes: here a doll leaves and comes back each cycle
struct Rng {                                                 // xorshift64: a run is its seed
    uint64_t s;
    uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 32); }
    uint32_t Below(uint32_t n) { return n ? Next() % n : 0; }
    bool Pct(uint32_t p) { return Below(100) < p; }
};
// one whole frame with a good MD5, nothing more
static bool OneFrame(const std::string& s) {
    Frame f; bool ok = false;
    return Decode((const uint8_t*)s.data(), s.size(), f, &ok) == s.size() && ok;
}
struct StrictTx55 {
    List mode = List::Off;
    Ids dolls;
    uint32_t hostId = 0, next = 1, lseq = 0, frames = 0, notFound = 0, hostReports = 0, reads = 0, shuffled = 0;
    bool closed = false;
    std::string why;                                         // what closed it
    std::string dollSeen;                                    // a doll's id where the mode forbids it: command@offset
    std::vector<uint32_t> rows;                              // the game's rows (0x4310 the host's, 0x4340 one, 0x4342 all of the id)
    std::set<uint32_t> players;                              // its game's players (0x4390 for anyone else: -3, ErrNotFound)
    std::map<uint32_t, uint32_t> lastVs;                     // (v141 beta 7) each player's last 0x4390: its VS rating as received
    std::string toHost;                                      // its frames, not read by the host yet
    void Close(const char* w) { if (!closed) why = w; closed = true; }
    void Read(const std::string& r, Rng& rng) {
        if (closed || r.empty()) return;
        ++reads;
        if (r.size() > 1024) { Close("a read of more than 1024 bytes"); return; }
        struct Got { uint16_t cmd, op; uint32_t id; std::string body; };
        std::vector<Got> got;
        char b[128];
        for (size_t at = 0; at < r.size();) {
            Frame f; bool md5 = false;
            const size_t k = Decode((const uint8_t*)r.data() + at, r.size() - at, f, &md5);
            if (!k) { Close("a read ending inside a frame"); return; }
            at += k;
            if (!md5 || f.seq != next) {
                snprintf(b, sizeof b, "%s on %04x: expected %u, got %u", md5 ? "sequence mismatch" : "bad MD5", f.cmd, next, f.seq);
                Close(b);
                return;
            }
            ++next; ++frames;
            for (size_t o = 0; o + 4 <= f.body.size(); o += 4) {
                if (!dolls.In(BeU32((const uint8_t*)f.body.data() + o))) continue;
                if (mode == List::Own && !o && (f.cmd == 0x4340 || f.cmd == 0x4342 || f.cmd == 0x4344 || f.cmd == 0x4346)) continue;
                snprintf(b, sizeof b, " %04x@%zu", f.cmd, o);
                dollSeen += b;
            }
            const uint32_t id = f.body.size() >= 4 ? BeU32((const uint8_t*)f.body.data()) : 0;
            Got g{(uint16_t)(f.cmd + 1), 0, id, U32(0)};
            if (f.cmd == 0x4390 && f.body.size() == Conn::kStatsBody) lastVs[id] = BeU32((const uint8_t*)f.body.data() + Conn::kStatsVsAt);
            if (f.cmd == 0x3003) hostId = id;
            else if (f.cmd == 0x4310) rows.push_back(hostId);
            else if (f.cmd == 0x0005) { g.cmd = 0x0005; g.body.clear(); }
            else if (f.cmd == 0x4390 && !players.count(id)) { g.body = U32((uint32_t)-3); ++notFound; }
            if (f.cmd == 0x4340 || f.cmd == 0x4342 || f.cmd == 0x4344 || f.cmd == 0x4346) g.body += U32(id);
            if (f.cmd == 0x4340 || f.cmd == 0x4342) { g.op = f.cmd; hostReports += id == hostId; }
            got.push_back(g);
        }
        // (the goroutines of one read: their answers and row changes in any order)
        std::vector<Got> was = got;
        for (size_t i = got.size(); i > 1; --i) std::swap(got[i - 1], got[rng.Below((uint32_t)i)]);
        for (size_t i = 0; i < got.size(); ++i)
            if (got[i].cmd != was[i].cmd || got[i].id != was[i].id) { ++shuffled; break; }
        for (const Got& g : got) {
            toHost += Fr(g.cmd, ++lseq, g.body);
            if (g.op == 0x4340) { rows.push_back(g.id); players.insert(g.id); }
            if (g.op == 0x4342) { rows.erase(std::remove(rows.begin(), rows.end(), g.id), rows.end()); players.erase(g.id); }
        }
    }
    size_t Count(uint32_t id) const { return (size_t)std::count(rows.begin(), rows.end(), id); }
};
// the game (a dedicated host: its log-in, its game, a real player and 3 dolls), the filter between it and TX-55, the clock
// in steps of 10 to 150 ms; each cycle: teams, pings, stats, a new round, a doll's leave (the real player's too now and then),
// another doll's leave while the host's rows are due now and then, the rows put back (the platform's select or the game's
// next send), the dolls back (early now and then: while the rows are due), pings, a keep-alive. The game writes several
// frames at once and cuts some inside a frame; TCP joins the filter's sends into TX-55's reads; the lobby's bytes reach the
// filter cut anywhere and late; the game reads headers, bodies, any size, peeks
static void Soak(List mode, int cycles, uint64_t seed) {
    const std::string tag = std::string("soak ") + ListName(mode) + ": ";
    Ids ids; ParseIds("0x7F000001-0x7F000040", ids);
    const uint32_t host = 0x1200, human = 0x1310, dolls[3] = {0x7F000001, 0x7F000002, 0x7F000003};
    Conn c(ids, mode);
    c.SetSeqLog(true);
    StrictTx55 tx;
    tx.mode = mode; tx.dolls = ids;
    Rng rng{seed};
    long long now = 0;
    uint32_t gseq = 0;                                       // the game's numbers
    std::vector<std::string> log;
    std::deque<std::string> wire;                            // the filter's sends, not read by TX-55 yet
    size_t seqLines = 0, seqOdd = 0;                         // the sequence log's lines; those not "lobby seq ..."
    uint32_t notWhole = 0, cuts = 0;                         // sends not one whole frame; writes cut inside a frame
    std::string gbuf;                                        // the game's side: bytes read, not a whole frame yet
    uint32_t gin = 0, gaps = 0, badMd5 = 0, badResult = 0, peeks = 0, peekBad = 0;
    std::multiset<std::pair<uint16_t, uint32_t>> expect;     // the answers the game waits for: (command, id)
    std::string unexpected;                                  // those it did not wait for: command/id
    std::map<uint16_t, uint32_t> dollSent;                   // the doll frames it sent, by command
    uint32_t dollPings = 0;                                  // the doll entries of its ping reports
    int badCycles = 0, badRows = 0;                          // cycles ending with an answer awaited or wrong; with rows wrong
    std::string firstBad, firstBadRows;
    auto takeLines = [&]() {
        std::vector<std::string> l;
        c.TakeSeqLog(l);
        for (const auto& x : l) { ++seqLines; if (x.compare(0, 10, "lobby seq ")) ++seqOdd; }
    };
    auto collect = [&](std::vector<std::string>& send) {
        for (auto& s : send) { if (!OneFrame(s)) ++notWhole; wire.push_back(std::move(s)); }
        takeLines();
    };
    auto out = [&](const std::string& bytes) {
        std::vector<std::string> send;
        c.SetNow(now);
        c.Out((const uint8_t*)bytes.data(), bytes.size(), send, log);
        collect(send);
    };
    auto due = [&]() {
        std::vector<std::string> send;
        c.SetNow(now);
        c.Due(send, log);
        collect(send);
    };
    // TX-55's reads: whole sends as TCP joined them, 1024 bytes at most; all, or some (the rest later)
    auto txRead = [&](bool all) {
        while (!wire.empty()) {
            std::string r;
            for (uint32_t k = 1 + rng.Below(4); k && !wire.empty() && r.size() + wire.front().size() <= 1024; --k) { r += wire.front(); wire.pop_front(); }
            if (r.empty()) { r = wire.front(); wire.pop_front(); }
            tx.Read(r, rng);
            if (!all && rng.Pct(40)) break;
        }
    };
    // the lobby's bytes to the host, cut anywhere
    auto hostRead = [&](bool all) {
        while (!tx.toHost.empty()) {
            const size_t n = 1 + rng.Below((uint32_t)std::min<size_t>(tx.toHost.size(), 300));
            c.SetNow(now);
            c.In((const uint8_t*)tx.toHost.data(), n);
            tx.toHost.erase(0, n);
            takeLines();
            if (!all && rng.Pct(40)) break;
        }
    };
    auto gameRead = [&](bool all) {
        while (c.Ready()) {
            uint8_t b[400];
            if (rng.Pct(5)) {
                const size_t r0 = c.Ready(), k = c.Take(b, 24, true);
                ++peeks;
                if (c.Ready() != r0 || k != std::min<size_t>(24, r0)) ++peekBad;
            }
            gbuf.append((const char*)b, c.Take(b, rng.Pct(50) ? 24 : 1 + rng.Below(sizeof b), false));
            for (;;) {
                Frame f; bool md5 = false;
                const size_t n = Decode((const uint8_t*)gbuf.data(), gbuf.size(), f, &md5);
                if (!n) break;
                gbuf.erase(0, n);
                if (!md5) ++badMd5;
                if (f.seq != gin + 1) ++gaps;
                gin = f.seq;
                const bool report = f.cmd == 0x4341 || f.cmd == 0x4343 || f.cmd == 0x4345 || f.cmd == 0x4347;
                const uint32_t id = report && f.body.size() >= 8 ? BeU32((const uint8_t*)f.body.data() + 4) : 0;
                if (f.body.size() >= 4 && BeU32((const uint8_t*)f.body.data())) ++badResult;
                const auto it = expect.find({f.cmd, id});
                if (it != expect.end()) expect.erase(it);
                else if (unexpected.size() < 160) { char t[32]; snprintf(t, sizeof t, " %04x/%#x", f.cmd, id); unexpected += t; }
            }
            if (!all && rng.Pct(30)) break;
        }
    };
    auto step = [&]() {                                       // the platform's select, then what reached each side
        now += 10 + rng.Below(141);
        if (rng.Pct(50)) due();
        if (rng.Pct(70)) txRead(false);
        if (rng.Pct(70)) hostRead(false);
        if (rng.Pct(80)) gameRead(false);
    };
    auto settle = [&]() { for (int i = 0; i < 2; ++i) { txRead(true); hostRead(true); gameRead(true); } };
    // a frame of the game's, numbered as it is made, and the answer it then waits for
    auto frame = [&](uint16_t cmd, const std::string& body) {
        const uint32_t id = body.size() >= 4 ? BeU32((const uint8_t*)body.data()) : 0;
        const bool report = cmd == 0x4340 || cmd == 0x4342 || cmd == 0x4344 || cmd == 0x4346;
        expect.insert({cmd == 0x0005 ? cmd : (uint16_t)(cmd + 1), report ? id : 0u});
        if ((report || cmd == 0x4390) && ids.In(id)) ++dollSent[cmd];
        for (size_t o = 4; cmd == 0x4398 && o + 8 <= body.size(); o += 8) dollPings += ids.In(BeU32((const uint8_t*)body.data() + o));
        return Fr(cmd, ++gseq, body);
    };
    // a write: its frames at once, now and then cut inside one (the rest a step later)
    auto write = [&](const std::vector<std::string>& frames) {
        std::string bytes;
        for (const auto& x : frames) bytes += x;
        if (bytes.size() > 1 && rng.Pct(20)) {
            const size_t cut = 1 + rng.Below((uint32_t)bytes.size() - 1);
            ++cuts;
            out(bytes.substr(0, cut));
            step();
            out(bytes.substr(cut));
        } else
            out(bytes);
        step();
    };
    bool in[3] = {true, true, true}, humanIn = true;
    auto team = [&]() { return std::string(1, (char)rng.Below(3)); };
    auto pings = [&]() {                                     // {u32 game ping, {u32 id, u32 ping}...}
        std::string p = U32(30 + rng.Below(60)), h = humanIn ? U32(human) + U32(20 + rng.Below(100)) : "";
        const bool first = rng.Pct(50);
        if (first) p += h;
        for (int k = 0; k < 3; ++k) if (in[k]) p += U32(dolls[k]) + U32(5 + rng.Below(40));
        return first ? p : p + h;
    };
    auto stats = [&]() { std::string s; for (int k = 0; k < 29; ++k) s += U32(rng.Below(50)); return s; };
    write({frame(0x3003, U32(host) + std::string(16, 'T'))});
    write({frame(0x4310, std::string(262, 'G'))});
    write({frame(0x4340, U32(human))});
    write({frame(0x4340, U32(dolls[0])), frame(0x4340, U32(dolls[1]))});
    write({frame(0x4340, U32(dolls[2])), frame(0x4344, U32(human) + team())});
    settle();
    int cy = 0;
    uint32_t humanUnranked = 0;                              // (v141 beta 7) the real player's reports sent unranked
    int badVs = 0;
    std::string firstBadVs;
    for (; cy < cycles && !tx.closed; ++cy) {
        const bool unr = cy % 2 == 1;                        // (v141 beta 7) unranked every other cycle
        c.SetUnranked(unr, "soak");
        bool humanStats = false;
        uint32_t humanVs = 0;
        std::vector<std::string> fr;
        if (humanIn) fr.push_back(frame(0x4344, U32(human) + team()));
        for (uint32_t k = 0, n = 1 + rng.Below(3); k < n; ++k) fr.push_back(frame(0x4344, U32(dolls[rng.Below(3)]) + team()));
        write(fr);
        write({frame(0x4398, pings())});
        fr.clear();                                          // the round's stats, the dolls' too, in one or two writes
        const uint32_t split = rng.Below(5);
        for (uint32_t k = 0; k < 4; ++k) {
            if (k == split && !fr.empty()) { write(fr); fr.clear(); }
            if (k < 3) fr.push_back(frame(0x4390, U32(dolls[k]) + stats()));
            else if (humanIn) {
                const std::string hs = stats();
                humanStats = true; humanVs = unr ? 0u : BeU32((const uint8_t*)hs.data() + 112);
                if (unr) ++humanUnranked;
                fr.push_back(frame(0x4390, U32(human) + hs));
            }
        }
        if (!rng.Pct(30)) { if (!fr.empty()) write(fr); fr.clear(); }
        fr.push_back(frame(0x4392, std::string(1, (char)(cy % 15))));
        write(fr);
        // a doll restarts: its leave, the real player's too now and then (in the same write)
        const int L = (int)rng.Below(3);
        const bool humanToo = humanIn && rng.Pct(12), humanFirst = rng.Pct(50);
        fr.clear();
        if (humanToo && humanFirst) fr.push_back(frame(0x4342, U32(human)));
        fr.push_back(frame(0x4342, U32(dolls[L])));
        if (humanToo && !humanFirst) fr.push_back(frame(0x4342, U32(human)));
        write(fr);
        in[L] = false;
        if (humanToo) humanIn = false;
        // (host) the rows due: another doll's leave now and then, the doll back early now and then (a restart takes a
        // second or so: TX-55 has read the leave by then)
        int M = -1;
        if (rng.Pct(25)) { M = (L + 1 + (int)rng.Below(2)) % 3; step(); write({frame(0x4342, U32(dolls[M]))}); in[M] = false; }
        if (rng.Pct(30)) { txRead(true); now += 300 + rng.Below(1200); write({frame(0x4340, U32(dolls[L]))}); in[L] = true; }
        if (M >= 0 && rng.Pct(50)) { txRead(true); now += 300 + rng.Below(600); write({frame(0x4340, U32(dolls[M]))}); in[M] = true; }
        settle();
        now += Conn::kRowsAgainMs + 1 + rng.Below(400);
        if (rng.Pct(70)) { due(); step(); }                  // (else the game's next send puts them back first)
        fr.clear();
        if (!in[L]) { fr.push_back(frame(0x4340, U32(dolls[L]))); in[L] = true; }
        if (M >= 0 && !in[M]) { fr.push_back(frame(0x4340, U32(dolls[M]))); in[M] = true; }
        if (humanToo) { fr.push_back(frame(0x4340, U32(human))); humanIn = true; }
        if (fr.empty()) fr.push_back(frame(0x4344, U32(dolls[L]) + team()));
        write(fr);
        write({frame(0x4398, pings())});
        write({frame(0x0005, "")});
        settle();
        // the cycle's end: each frame answered, nothing awaited, the rows as the mode lists the players
        const auto d = c.Debug();
        const size_t hu = humanIn ? 1 : 0;
        bool rows = tx.Count(human) == hu;
        if (mode == List::Host) rows = rows && tx.Count(host) == 4 && c.Listed() == 3 && tx.rows.size() == 4 + hu;
        else if (mode == List::Own) rows = rows && tx.Count(host) == 1 && tx.Count(dolls[0]) == 1 && tx.Count(dolls[1]) == 1 && tx.Count(dolls[2]) == 1 && tx.rows.size() == 4 + hu;
        else rows = rows && tx.Count(host) == 1 && tx.rows.size() == 1 + hu;
        char t[240];
        if ((!expect.empty() || !unexpected.empty() || d.pendingJoins || d.pendingLeaves || c.RowsDue()) && !badCycles++) {
            snprintf(t, sizeof t, "cycle %d: %zu answers awaited, not awaited:%s, pending %zu joins %zu leaves%s", cy, expect.size(),
                     unexpected.empty() ? " none" : unexpected.c_str(), d.pendingJoins, d.pendingLeaves, c.RowsDue() ? ", rows due" : "");
            firstBad = t;
        }
        if (!rows && !badRows++) {
            snprintf(t, sizeof t, "cycle %d: %zu rows, the host's %zu, the player's %zu, %zu dolls listed", cy, tx.rows.size(), tx.Count(host), tx.Count(human), c.Listed());
            firstBadRows = t;
        }
        if (humanStats && tx.lastVs[human] != humanVs && !badVs++) {
            snprintf(t, sizeof t, "cycle %d (%s): the player's VS rating %u at TX-55, %u expected", cy, unr ? "unranked" : "ranked", tx.lastVs[human], humanVs);
            firstBadVs = t;
        }
    }
    const auto d = c.Debug();
    uint32_t dollFrames = 0;
    for (const auto& k : dollSent) dollFrames += k.second;
    Check((tag + "TX-55 never closed the connection (no gap, MD5s good, whole frames)").c_str(), !tx.closed && cy == cycles, tx.why);
    Check((tag + "each send one whole frame").c_str(), notWhole == 0, std::to_string(notWhole));
    Check((tag + "no doll's id where the mode forbids it").c_str(), tx.dollSeen.empty(), tx.dollSeen.substr(0, 120));
    Check((tag + "the game read an unbroken sequence, MD5s good, peeks left the bytes").c_str(), !gaps && !badMd5 && !peekBad && gin == d.inLast && gbuf.empty(),
          std::to_string(gaps) + " gaps, " + std::to_string(badMd5) + " bad");
    Check((tag + "each frame of the game's answered once by its cycle's end, a doll's with its id, result 0").c_str(),
          !badCycles && expect.empty() && unexpected.empty() && !badResult, firstBad + " (" + std::to_string(badCycles) + " cycles)");
    Check((tag + "TX-55's rows at each cycle's end as the mode lists the players").c_str(), !badRows, firstBadRows + " (" + std::to_string(badRows) + " cycles)");
    Check((tag + "nothing awaited at the end").c_str(), !d.pendingJoins && !d.pendingLeaves && !c.RowsDue() && wire.empty() && tx.toHost.empty() && !c.Ready());
    Check((tag + "the counters agree with what TX-55 saw").c_str(),
          d.framesOut == tx.frames && d.outLast == tx.next - 1 && d.outLast == gseq + d.outAdded - d.outKept && d.framesIn == tx.lseq &&
          d.inLast == tx.lseq + d.inAdded - d.inSwallowed && d.kept == d.outKept && d.answered == d.inAdded && d.kept == d.answered &&
          d.rowsAgain == d.outAdded && d.swallowed == d.inSwallowed && d.stripped == dollPings && d.lastOutWire == d.outLast,
          c.DebugLine());
    if (mode == List::Off)
        Check((tag + "every doll frame kept here, none listed").c_str(), d.kept == dollFrames && !d.listedSent && !d.rowsAgain && !d.swallowed && !tx.hostReports);
    else if (mode == List::Own)
        Check((tag + "the dolls' stats kept here, their reports sent").c_str(), d.kept == dollSent[0x4390] && d.listedSent == dollFrames - dollSent[0x4390] &&
              !d.rowsAgain && !d.swallowed);
    else
        Check((tag + "the dolls' joins and leaves sent as the host's, its rows put back, their answers left out").c_str(),
              d.kept + d.listedSent == dollFrames && d.listedSent + d.rowsAgain == tx.hostReports && d.rowsAgain && d.swallowed == d.rowsAgain);
    Check((tag + "the sequence log: a line a frame each way, two a frame kept").c_str(), !seqOdd && seqLines == (size_t)d.framesOut + 2 * d.kept + d.framesIn,
          std::to_string(seqLines));
    Check((tag + "TX-55 knew each player the host reported stats for").c_str(), !tx.notFound, std::to_string(tx.notFound));
    Check((tag + "unranked every other cycle: the real player's VS rating 0 then, as the game sent it otherwise; the rounds counted").c_str(),
          !badVs && d.unranked == humanUnranked && d.rounds == (uint32_t)cy, firstBadVs + " unranked=" + std::to_string(d.unranked) + " of " +
          std::to_string(humanUnranked) + ", rounds=" + std::to_string(d.rounds));
    printf("      %s%d cycles (seed %#llx): %u frames to TX-55 in %u reads (%u answered out of order), %u from it; %u writes cut, %u peeks\n", tag.c_str(), cy,
           (unsigned long long)seed, tx.frames, tx.reads, tx.shuffled, tx.lseq, cuts, peeks);
    printf("      %s%s\n", tag.c_str(), c.DebugLine().c_str());
}

// (v141 beta 6) TX-55 answers each frame on a goroutine of its own: two joins or two leaves read together may be answered in
// either order. Host mode: the real player's answers and the doll's (or the filter's own row) told apart by the id they name
static void OutOfOrder() {
    Ids ids; ParseIds("0x7F000001-0x7F000040", ids);
    const uint32_t host = 0x1200, human = 0x1310, d1 = 0x7F000001;
    Conn c(ids, List::Host);
    std::vector<std::string> send, log;
    uint32_t gseq = 0, lseq = 0;
    std::string seen;
    auto out = [&](const std::vector<std::pair<uint16_t, std::string>>& fs) {   // one write
        std::string x;
        for (const auto& f : fs) x += Fr(f.first, ++gseq, f.second);
        send.clear();
        c.Out((const uint8_t*)x.data(), x.size(), send, log);
    };
    auto in = [&](uint16_t cmd, const std::string& body) { const std::string x = Fr(cmd, ++lseq, body); c.In((const uint8_t*)x.data(), x.size()); };
    auto read = [&]() { uint8_t b[512]; for (size_t k; (k = c.Take(b, sizeof b, false)) > 0;) seen.append((const char*)b, k); };
    out({{0x3003, U32(host) + std::string(16, 'T')}});
    in(0x3004, U32(0));
    out({{0x4310, std::string(262, 'G')}});
    in(0x4311, U32(0));
    out({{0x4340, U32(human)}});
    in(0x4341, U32(0) + U32(human));
    out({{0x4340, U32(d1)}});
    in(0x4341, U32(0) + U32(host));
    read();
    const size_t before = Frames(seen).size();
    out({{0x4342, U32(human)}, {0x4342, U32(d1)}});          // both leave in one write: the doll's answered first
    in(0x4343, U32(0) + U32(host));
    in(0x4343, U32(0) + U32(human));
    c.SetNow(Conn::kRowsAgainMs + 1);
    out({{0x4340, U32(human)}});                             // 3 s on he is back: the host's row put back first, his answer first
    const bool both = send.size() == 2 && Frames(send[0]).size() == 1 && Frames(send[0])[0].body == U32(host) && Frames(send[1])[0].body == U32(human);
    in(0x4341, U32(0) + U32(human));
    in(0x4341, U32(0) + U32(host));
    read();
    bool md5s = false;
    const auto gv = Frames(seen, &md5s);
    bool seqs = gv.size() == before + 3;
    for (size_t i = 0; i < gv.size(); ++i) seqs = seqs && gv[i].seq == i + 1;
    auto named = [&](size_t i, uint16_t cmd) { return i < gv.size() && gv[i].cmd == cmd && gv[i].body.size() == 8 ? BeU32((const uint8_t*)gv[i].body.data() + 4) : 0u; };
    const uint32_t a = named(before, 0x4343), b = named(before + 1, 0x4343), j = named(before + 2, 0x4341);
    char t[96];
    snprintf(t, sizeof t, "leaves %#x %#x, join %#x", a, b, j);
    Check("host, answers out of order: each leave's answer names its player", (a == human && b == d1) || (a == d1 && b == human), t);
    Check("host, answers out of order: the real player's join answered with his id, the row's answer left out", both && j == human && c.swallowed == 1, t);
    Check("host, answers out of order: the game's frames numbered without a gap", seqs && md5s && !c.Debug().pendingJoins && !c.Debug().pendingLeaves);
}

// (v141 beta 7) unranked: a player's 0x4390 sent with its VS rating 0 (its MD5 made again, its number kept or shifted as any frame),
// a doll's still kept here, other frames and other sizes untouched, off again: as the game sent it
static void Unranked() {
    Ids ids; ParseIds("0x7F000001-0x7F000040", ids);
    const uint32_t host = 0x1200, human = 0x1310, doll = 0x7F000002;
    auto stats = [](uint32_t vs) { std::string s; for (int k = 0; k < 28; ++k) s += U32((uint32_t)(k * 3 + 1)); return s + U32(vs); };
    for (int withIds = 0; withIds < 2; ++withIds) {
        Conn c(withIds ? ids : Ids{}, List::Host);
        const std::string tag = withIds ? "unranked, accountless dolls: " : "unranked, no accountless doll: ";
        std::vector<std::string> send, log;
        uint32_t gseq = 0;
        auto out = [&](uint16_t cmd, const std::string& body) { send.clear(); const std::string x = Fr(cmd, ++gseq, body); c.Out((const uint8_t*)x.data(), x.size(), send, log); return x; };
        out(0x3003, U32(host) + std::string(16, 'T'));
        out(0x4310, std::string(262, 'G'));
        std::string sent = out(0x4390, U32(human) + stats(815));
        Check((tag + "off: the report as the game sent it").c_str(), send.size() == 1 && send[0] == sent && c.Debug().unranked == 0);
        c.SetUnranked(true, "test");
        Check((tag + "the switch on").c_str(), c.UnrankedOn());
        log.clear();
        sent = out(0x4390, U32(human) + stats(815));
        bool md5 = false;
        std::vector<Frame> v = send.size() == 1 ? Frames(send[0], &md5) : std::vector<Frame>();
        const Frame* g = v.size() == 1 ? &v[0] : nullptr;
        const std::string want = U32(human) + stats(0);
        Check((tag + "on: VS rating 0, the rest as sent, MD5 good, its number kept").c_str(), g && md5 && g->cmd == 0x4390 && g->seq == gseq && g->body == want,
              g ? Hex((const uint8_t*)g->body.data() + 112, 8) : "no frame");
        Check((tag + "on: its log line").c_str(), log.size() == 1 && log[0] == "lobby filter: 4390 for user id 0x1310 sent unranked (vs_rating 0, was 815: test)",
              log.empty() ? "" : log[0]);
        sent = out(0x4390, U32(human) + std::string(20, 'x'));
        Check((tag + "on: a report of another size untouched").c_str(), send.size() == 1 && send[0] == sent);
        sent = out(0x4344, U32(human) + std::string(1, '\x01'));
        Check((tag + "on: other frames untouched").c_str(), send.size() == 1 && send[0] == sent);
        sent = out(0x4392, std::string(1, '\x02'));
        Check((tag + "a new round counted").c_str(), send.size() == 1 && send[0] == sent && c.Debug().rounds == 1);
        if (withIds) {
            out(0x4390, U32(doll) + stats(700));             // (kept here and answered: the frames after it one number lower)
            Check((tag + "on: a doll's report still kept here").c_str(), send.empty() && c.Debug().kept == 1);
            out(0x4390, U32(human) + stats(816));
            v = send.size() == 1 ? Frames(send[0], &md5) : std::vector<Frame>();
            Check((tag + "on: after a kept frame, its number shifted, VS rating 0, MD5 good").c_str(),
                  v.size() == 1 && md5 && v[0].seq == gseq - 1 && v[0].body == U32(human) + stats(0));
        }
        c.SetUnranked(false, "");
        sent = out(0x4390, U32(human) + stats(817));
        v = send.size() == 1 ? Frames(send[0], &md5) : std::vector<Frame>();
        Check((tag + "off again: the VS rating as the game sent it").c_str(), v.size() == 1 && md5 && v[0].body == U32(human) + stats(817));
        const auto d = c.Debug();
        Check((tag + "the counters and the close line").c_str(), d.unranked == (withIds ? 2u : 1u) && c.unranked == d.unranked &&
              c.DebugLine().find(withIds ? "unranked=2 rounds=1" : "unranked=1 rounds=1") != std::string::npos, c.DebugLine());
    }
}

// (v141 beta 6) the sequence log: a line a frame each way when on, none when off
static void SeqLog() {
    Ids ids; ParseIds("0x7F000001-0x7F000040", ids);
    const uint32_t host = 0x1200, d1 = 0x7F000001;
    for (int on = 1; on >= 0; --on) {
        Conn c(ids, List::Host);
        c.SetSeqLog(on != 0);
        std::vector<std::string> send, log, lines;
        uint32_t gseq = 0, lseq = 0;
        auto out = [&](uint16_t cmd, const std::string& body) { const std::string x = Fr(cmd, ++gseq, body); c.Out((const uint8_t*)x.data(), x.size(), send, log); };
        auto in = [&](uint16_t cmd, const std::string& body) { const std::string x = Fr(cmd, ++lseq, body); c.In((const uint8_t*)x.data(), x.size()); };
        out(0x3003, U32(host) + std::string(16, 'T'));
        in(0x3004, U32(0));
        out(0x4310, std::string(262, 'G'));
        in(0x4311, U32(0));
        out(0x4340, U32(d1));                                    // (as the host's)
        in(0x4341, U32(0) + U32(host));
        out(0x4344, U32(d1) + std::string(1, '\0'));             // (kept here)
        out(0x4342, U32(d1));                                    // (as the host's: its row put back 3 s later)
        in(0x4343, U32(0) + U32(host));
        c.SetNow(Conn::kRowsAgainMs);
        c.Due(send, log);
        in(0x4341, U32(0) + U32(host));                          // (the row's answer, left out)
        out(0x0005, "");
        in(0x0005, "");
        c.TakeSeqLog(lines);
        const auto d = c.Debug();
        if (!on) {
            Check("seq log off: no line, the frames as with it on", lines.empty() && d.framesOut == 6 && d.framesIn == 6 && d.kept == 1 && send.size() == 6);
            continue;
        }
        auto has = [&](size_t i, const char* a, const char* b) { return i < lines.size() && lines[i].find(a) != std::string::npos && (!b || lines[i].find(b) != std::string::npos); };
        Check("seq log on: a line a frame each way, two a frame kept", lines.size() == 14 && lines.size() == (size_t)d.framesOut + 2 * d.kept + d.framesIn,
              std::to_string(lines.size()));
        Check("seq log on: each line its way, command, numbers and counters",
              has(0, "lobby seq OUT cmd=3003 game=1 wire=1 outKept=0 outAdded=0 inAdded=0 inSwallowed=0", nullptr) &&
              has(1, "lobby seq IN  cmd=3004 game=1 wire=1", nullptr) && has(6, "OUT cmd=4344 game=4 wire=0", "kept here") &&
              has(7, "IN  cmd=4345 game=4 wire=0", "answered here") && has(8, "OUT cmd=4342 game=5 wire=4 outKept=1", nullptr) &&
              has(9, "IN  cmd=4343 game=5 wire=4", nullptr) && has(10, "OUT cmd=4340 game=0 wire=5 outKept=1 outAdded=1", "added here") &&
              has(11, "IN  cmd=4341 game=0 wire=5", "swallowed") && has(12, "OUT cmd=0005 game=6 wire=6", nullptr) &&
              has(13, "IN  cmd=0005 game=6 wire=6 outKept=1 outAdded=1 inAdded=1 inSwallowed=1", nullptr),
              lines.size() > 10 ? lines[10] : "");
        lines.clear();
        c.TakeSeqLog(lines);
        Check("seq log on: taken once", lines.empty());
    }
}

#ifndef _WIN32
// the live session against the stand-in in its TX-55 mode
static bool SendAll(int s, const std::string& d) { size_t o = 0; while (o < d.size()) { const ssize_t k = send(s, d.data() + o, d.size() - o, 0); if (k <= 0) return false; o += (size_t)k; } return true; }
static int Live(const char* where, List mode) {
    std::string h = where;
    const size_t colon = h.rfind(':');
    const int port = colon == std::string::npos ? 5732 : atoi(h.c_str() + colon + 1);
    if (colon != std::string::npos) h.resize(colon);
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); inet_pton(AF_INET, h.c_str(), &a.sin_addr);
    if (s < 0 || connect(s, (sockaddr*)&a, sizeof a) != 0) { printf("FAIL  live: no lobby at %s\n", where); return 1; }
    Ids ids; ParseIds("0x7F000001-0x7F000010", ids);
    Conn c(ids, mode);
    printf("      live: the dolls' listing %s\n", ListName(mode));
    uint32_t gseq = 0;
    std::vector<std::string> log;
    bool closed = false;
    timeval t0; gettimeofday(&t0, nullptr);
    auto clock = [&]() { timeval t; gettimeofday(&t, nullptr); return (long long)(t.tv_sec - t0.tv_sec) * 1000 + (t.tv_usec - t0.tv_usec) / 1000; };
    auto out = [&](uint16_t cmd, const std::string& body) {
        const std::string x = Fr(cmd, ++gseq, body);
        std::vector<std::string> send;
        c.SetNow(clock());
        c.Out((const uint8_t*)x.data(), x.size(), send, log);
        for (const auto& f : send) if (!SendAll(s, f)) closed = true;
    };
    // (host) the host's rows put back 3 s after a leave: the platform's select calls Due
    auto settle = [&]() {
        if (mode != List::Host) return;
        usleep(3100 * 1000);
        std::vector<std::string> send;
        c.SetNow(clock());
        c.Due(send, log);
        for (const auto& f : send) if (!SendAll(s, f)) closed = true;
    };
    // the game's reading: whole frames through the filter, 2 s at most for the one wanted
    auto answer = [&](uint16_t want, Frame& got) -> bool {
        for (int tries = 0; tries < 200; ++tries) {
            if (c.Ready() >= 24) {
                uint8_t hd[24];
                c.Take(hd, 24, true);
                for (int i = 0; i < 24; ++i) hd[i] ^= kKey[i & 3];
                const size_t n = 24 + ((size_t)hd[2] << 8 | hd[3]);
                std::string b(n, '\0');
                c.Take((uint8_t*)&b[0], n, false);
                Decode((const uint8_t*)b.data(), b.size(), got);
                if (got.cmd == want) return true;
                continue;
            }
            timeval tv{0, 10000};
            fd_set rs; FD_ZERO(&rs); FD_SET(s, &rs);
            if (select(s + 1, &rs, nullptr, nullptr, &tv) > 0) {
                char buf[4096];
                const ssize_t k = recv(s, buf, sizeof buf, 0);
                if (k <= 0) { closed = true; return false; }
                c.In((const uint8_t*)buf, (size_t)k);
            }
        }
        return false;
    };
    const uint32_t human = 0x1310, d1 = 0x7F000001, d2 = 0x7F000002;
    Frame f;
    out(0x3003, U32(0x130e) + std::string(16, 'T'));
    Check("live: logged in", answer(0x3004, f));
    std::string g(262, '\0'); memcpy(&g[0], "ACCOUNTLESS", 11);
    out(0x4310, g);
    Check("live: game created", answer(0x4311, f) && f.body.substr(0, 4) == U32(0));
    out(0x4340, U32(human));
    Check("live: the real player's join answered by the lobby", answer(0x4341, f) && f.body == U32(0) + U32(human));
    out(0x4340, U32(d1));
    Check("live: doll 1's join answered with its id", answer(0x4341, f) && f.body == U32(0) + U32(d1));
    out(0x4340, U32(d2));
    Check("live: doll 2's join answered with its id", answer(0x4341, f) && f.body == U32(0) + U32(d2));
    out(0x4344, U32(d1) + std::string(1, '\0'));
    out(0x4344, U32(human) + std::string(1, '\x01'));
    Check("live: the doll's team answered with its id", answer(0x4345, f) && f.body == U32(0) + U32(d1));
    Check("live: the real player's team answered by the lobby", answer(0x4345, f) && f.body == U32(0) + U32(human));
    out(0x4398, U32(30) + U32(d1) + U32(10) + U32(human) + U32(44) + U32(d2) + U32(11));
    Check("live: the pings answered", answer(0x4399, f) && f.body == U32(0));
    out(0x4392, std::string(1, '\x01'));
    Check("live: the new round answered", answer(0x4393, f));
    out(0x4390, U32(d1) + std::string(116, '\0'));
    Check("live: the doll's stats answered here {0}", answer(0x4391, f) && f.body == U32(0));
    out(0x4390, U32(human) + std::string(116, '\0'));
    Check("live: the real player's stats taken by the lobby (0: he is in its game)", answer(0x4391, f) && f.body == U32(0));
    out(0x4342, U32(d2));
    Check("live: the doll's leave answered with its id", answer(0x4343, f) && f.body == U32(0) + U32(d2));
    settle();
    out(0x4344, U32(human) + std::string(1, '\0'));
    Check("live: the real player's team answered by the lobby after the leave", answer(0x4345, f) && f.body == U32(0) + U32(human));
    out(0x4342, U32(d1));
    Check("live: the last doll's leave answered with its id", answer(0x4343, f) && f.body == U32(0) + U32(d1));
    settle();
    out(0x0005, "");
    Check("live: a keep-alive answered after all that", answer(0x0005, f));
    Check("live: the lobby never closed the connection", !closed);
    for (const auto& l : log) printf("      %s\n", l.c_str());
    close(s);
    return 0;
}
#endif

int main(int argc, char** argv) {
    Offline();
    OfflineList();
    OutOfOrder();
    SeqLog();
    Unranked();
    int cycles = 500;
    uint64_t seed = 0x5EED141B6ull;
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--soak") && i + 1 < argc) {
            cycles = atoi(argv[i + 1]);
            if (i + 2 < argc && argv[i + 2][0] != '-') seed = strtoull(argv[i + 2], nullptr, 0);
        }
    Soak(List::Off, cycles, seed);
    Soak(List::Own, cycles, seed + 1);
    Soak(List::Host, cycles, seed + 2);
#ifndef _WIN32
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--live")) {
            List mode = List::Off;
            if (i + 2 < argc && ParseList(argv[i + 2], mode)) {}
            Live(argv[i + 1], mode);
        }
#endif
    printf("%s\n", gFails ? (std::to_string(gFails) + " failed").c_str() : "all passed");
    return gFails ? 1 : 0;
}
