// netfix.h - the netcode fixes of v141 (docs/NETWORK_STACK.md, docs/BIG_GAMES.md): a PC joining the games of PS2 and
// PCSX2 hosts of every program (the connects in both regions' dialects at once), the join's outcome in the log, and the
// simulation's stand-ins for a stock PS2 (SIM_PS2) and for a poor link (SIM_NET_DELAY, SIM_NET_RLOSS).
// Included by ps2world.cpp inside its namespace, after SessionKeys.
#pragma once

namespace net { SOCKET Sock(uint32_t fd); }

// ---- tests: this instance plays a stock PS2 or PCSX2 program: its own dialect alone (SIM_PS2_SESSIONS), none of the
// PC's translations of the records, no side channel of the PCs (voice, added weapons); the run sets the other switches
// a PS2 lacks (MGO1_PROTOCOL_BRIDGE=0, MGO1_SPAWN_FIX=0, MGO1_COPY_REST=0, MGO1_COPY_AIMWALK=0, MGO1_UPSTREAM=0)
bool Ps2Like() { static const bool on = getenv("SIM_PS2") || getenv("SIM_PS2_SESSIONS") || SimPlainPs2(); return on; }   // (+ the host line's stand-ins)

// ---- a Host Only game (docs/BIG_GAMES.md): its host does not play and the menu gives its maximum one more (US ntmenu2
// 0x35F9A0: settings + 0x2A7 += 1 when + 0xF7 is set), so a stock game of 8 is told and listed with 9 (the host list
// entry's + 0x1A; + 0x16 its Host Only flag, tx55 GameListEntry.IsHostOnly). rc1 took any maximum above 8 for a game
// of the PCs: a PC never joined a PS2 or PCSX2 Host Only game (its connects in the PCs' dialects alone: 1059 at each
// try), and a PC's own Host Only game of 8 spoke the PCs' dialect (no PS2 could join it). The players are the maximum
// less the host who does not play.
uint32_t NetPlayingCap(uint8_t hostOnly) { return hostOnly ? 9u : 8u; }
bool NetBigChosen(uint32_t most, uint8_t hostOnly) {
    if (hostOnly && most == NetPlayingCap(1)) Log("session: the game chosen is Host Only: %u at most in the list, 8 players: a game of the stock programs", most);
    return most > NetPlayingCap(hostOnly);
}
// tests: SIM_HOST_ONLY=1, Create Game's [Host Only] set at its confirmation (ntmenu2 0x35F9A0, a0 its work: + 0xF7),
// as the menu's item sets it (its page clears it at 0x3B86F4)
void NetSimHostOnly(uint32_t menu) {
    static const bool on = DevEnv("SIM_HOST_ONLY") != nullptr;
    if (!on || menu < 0x100000 || menu + 0x300 >= 0x02000000 || R8(menu + 0xf7)) return;
    W8(menu + 0xf7, 1);
    Log("SIM_HOST_ONLY: Create Game's Host Only set (%u players at most before the menu's one more)", R8(menu + 0x2a7));
}

// ---- the session datagram's layers (base 0x16F420 send, 0x16FD58 receive): [seq u16][payload][check 10], then the
// whole scrambled (0x16F170, inverse 0x16F218). The payload cipher (0x16EF00 / 0x16EFA8): k = key ^ ((seq & 0x7FFF) *
// 0x5D588B65 + 1), each 32-bit word w (little-endian) XORed with k, then k += the word in the clear. The check
// (0x16F050): d1 = MD5(le32 constant | seq and payload), d2 = MD5(le32 constant | d1), d2[0..9] with d2[10..15] XORed
// into its bytes 0..5.
struct NetMd5 {
    uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    uint8_t buf[64] = {}; uint64_t n = 0;
    static uint32_t Rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
    void Block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
            0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
            0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
            0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
            0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
            0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const int S[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
        uint32_t m[16];
        for (int i = 0; i < 16; ++i) m[i] = p[4 * i] | p[4 * i + 1] << 8 | p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f; int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
            else { f = c ^ (b | ~d); g = (7 * i) & 15; }
            const uint32_t t = d; d = c; c = b;
            b = b + Rol(a + f + K[i] + m[g], S[(i >> 4) * 4 + (i & 3)]);
            a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }
    void Add(const uint8_t* p, size_t len) {
        for (size_t i = 0; i < len; ++i) { buf[n & 63] = p[i]; if ((++n & 63) == 0) Block(buf); }
    }
    void End(uint8_t out[16]) {
        const uint64_t bits = n * 8;
        const uint8_t one = 0x80, zero = 0;
        Add(&one, 1);
        while ((n & 63) != 56) Add(&zero, 1);
        for (int i = 0; i < 8; ++i) { const uint8_t b = (uint8_t)(bits >> (8 * i)); Add(&b, 1); }
        for (int i = 0; i < 16; ++i) out[i] = (uint8_t)(h[i / 4] >> (8 * (i & 3)));
    }
};
void NetCipher(uint8_t* b, size_t len, uint32_t key, bool encrypt) {   // b: [seq][payload], len: both
    uint32_t k = key ^ ((uint32_t)((b[0] | b[1] << 8) & 0x7fff) * 0x5D588B65u + 1u);
    for (size_t i = 2; i < len; i += 4) {
        uint8_t w[4] = {0, 0, 0, 0};
        const size_t m = std::min<size_t>(4, len - i);
        memcpy(w, b + i, m);
        const uint32_t x = w[0] | w[1] << 8 | w[2] << 16 | (uint32_t)w[3] << 24, y = x ^ k;
        for (size_t j = 0; j < m; ++j) b[i + j] = (uint8_t)(y >> (8 * j));
        k += encrypt ? x : y;
    }
}
void NetCheck(const uint8_t* b, size_t len, uint32_t constant, uint8_t out[10]) {
    uint8_t le[4] = {(uint8_t)constant, (uint8_t)(constant >> 8), (uint8_t)(constant >> 16), (uint8_t)(constant >> 24)}, d1[16], d2[16];
    NetMd5 a; a.Add(le, 4); a.Add(b, len); a.End(d1);
    NetMd5 z; z.Add(le, 4); z.Add(d1, 16); z.End(d2);
    for (int i = 0; i < 10; ++i) out[i] = d2[i];
    for (int i = 10; i < 16; ++i) out[i % 10] ^= d2[i];
}
void NetScrambleIdx(size_t len, size_t idx[4]) {        // the bytes 0 and 1 XORed with idx[0], idx[1]; swapped with idx[2], idx[3]
    uint32_t seed = (uint32_t)len;
    for (int t = 0; t < 2; ++t) { seed = seed * 0x5D588B65u + 1u; idx[t] = (seed >> 16) % (uint32_t)(len - 2) + 2; }
    for (int t = 0; t < 2; ++t) { seed = seed * 0x5D588B65u + 1u; idx[2 + t] = (seed >> 16) % (uint32_t)len; }
}
void NetScramble(uint8_t* d, size_t len) {
    size_t x[4]; NetScrambleIdx(len, x);
    d[0] ^= d[x[0]]; d[1] ^= d[x[1]];
    std::swap(d[0], d[x[2]]); std::swap(d[1], d[x[3]]);
}
void NetUnscramble(uint8_t* d, size_t len) {
    size_t x[4]; NetScrambleIdx(len, x);
    std::swap(d[1], d[x[3]]); std::swap(d[0], d[x[2]]);
    d[1] ^= d[x[1]]; d[0] ^= d[x[0]];
}

// ---- a joiner's connects (docs/BIG_GAMES.md, PS2 and PCSX2 hosts). A host not known yet gets this PC's connects in one
// set each, in turn (JoinCandidates: up to 18 with the extra maps and TEAM SNEAKING's); a stock PS2 or PCSX2 host
// verifies its region's set alone, so it got one connect a join (two with the other region's), the next one in its set
// 18 sets (some 33 s) later, after the join's timeout. Each connect to a host not known yet now goes out once more in
// each set the PC has not put it in among both regions' and the host list's hint (NetJoinCopies): the host takes its
// own at the first connect whatever its program, as from a PS2 of its region. The copies' check fails at any other
// host (dropped as a datagram of a wrong set: a PC host takes the first that verifies, a PS2 the one of its region).
// MGO1_JOIN_COPIES=0: the connects in one set each (rc1's way, tests).
bool JoinCopiesOn() { static const bool off = DevEnv("MGO1_JOIN_COPIES") && !strcmp(DevEnv("MGO1_JOIN_COPIES"), "0"); return !off; }
struct NetJoinPeer { uint32_t connects = 0, copies = 0, got = 0, verified = 0; std::map<int, uint32_t> bySet; ULONGLONG first = 0; bool told = false; };
std::map<uint64_t, NetJoinPeer> gNetJoin;                 // a host joined: this PC's connects and its datagrams
struct { uint64_t peer = 0; int set = -1, own = 0; bool armed = false; } gNetBoot;   // the bootstrap datagram being built
void NetBootBuilt(uint64_t peer, int set, int own) {     // SessionKeys: a connect to a host not known yet, in that set
    gNetBoot.peer = peer; gNetBoot.set = set; gNetBoot.own = own; gNetBoot.armed = true;
    NetJoinPeer& j = gNetJoin[peer];
    if (!j.first) j.first = GetTickCount64();
    ++j.connects; ++j.bySet[set];
}
void NetJoinCopies(SOCKET s, const sockaddr_in& to, const uint8_t* d, int len) {   // the game's sendto went out
    if (!gNetBoot.armed) return;
    gNetBoot.armed = false;
    const uint64_t peer = ((uint64_t)to.sin_addr.s_addr << 16) | ntohs(to.sin_port);
    if (peer != gNetBoot.peer || len < 14 || len > 1200 || !JoinCopiesOn() || PeerRegion(peer) >= 0) return;
    const int own = gNetBoot.own, sent = gNetBoot.set;
    std::vector<int> sets;
    if (gJoinSet != 2) { sets.push_back(own); sets.push_back(1 - own); }   // a big game chosen: the PCs' sets alone
    if (gJoinHintSet >= 0) sets.push_back(gJoinHintSet);
    std::vector<uint8_t> clear(d, d + len);
    NetUnscramble(clear.data(), clear.size());
    const size_t body = (size_t)len - 10;
    {   // the datagram rebuilt in its own set must be the game's to the byte (else no copy: a layout not understood)
        std::vector<uint8_t> t(clear.begin(), clear.begin() + (long)body);
        uint8_t chk[10]; NetCheck(t.data(), body, SetMac(sent), chk);
        std::vector<uint8_t> again(t); again.insert(again.end(), chk, chk + 10);
        NetScramble(again.data(), again.size());
        static int bad = 0;
        if (memcmp(again.data(), d, (size_t)len)) { if (bad++ < 3) Log("session: a connect to %s not rebuilt as the game made it: sent in its %s dialect only", PeerText(peer).c_str(), SetName(sent)); return; }
    }
    NetCipher(clear.data(), body, SetKey(sent), false);   // its payload in the clear
    for (int r : sets) {
        if (r == sent || r < 0 || (size_t)r >= gSets.size()) continue;
        std::vector<uint8_t> c(clear.begin(), clear.begin() + (long)body);
        NetCipher(c.data(), body, SetKey(r), true);
        uint8_t chk[10]; NetCheck(c.data(), body, SetMac(r), chk);
        c.insert(c.end(), chk, chk + 10);
        NetScramble(c.data(), c.size());
        if (sendto(s, (const char*)c.data(), (int)c.size(), 0, (const sockaddr*)&to, sizeof to) == (int)c.size()) {
            NetJoinPeer& j = gNetJoin[peer];
            ++j.copies; ++j.bySet[r];
            static int told = 0;
            if (told++ < 4) Log("session: a connect to %s sent in its %s dialect, again in the %s one", PeerText(peer).c_str(), SetName(sent), SetName(r));
        }
    }
}
void NetJoinGot(uint64_t peer, bool verified) {           // a bootstrap datagram from a host joined (SessionKeys)
    auto it = gNetJoin.find(peer);
    if (it == gNetJoin.end()) return;
    ++it->second.got;
    if (verified) ++it->second.verified;
}
void NetJoinLearnt(uint64_t peer, int set) {               // its first datagram that verifies: the join's outcome
    auto it = gNetJoin.find(peer);
    if (it == gNetJoin.end() || it->second.told) return;
    it->second.told = true;
    const NetJoinPeer& j = it->second;
    Log("session: the host %s answers in the %s dialect, %llu ms after this PC's first connect (%u connects, %u copies in the other dialects)",
        PeerText(peer).c_str(), SetName(set), (unsigned long long)(GetTickCount64() - j.first), j.connects, j.copies);
}
std::string NetSetsText(const NetJoinPeer& j) {
    std::string t;
    for (const auto& kv : j.bySet) { char b[64]; snprintf(b, sizeof b, "%s%s %u", t.empty() ? "" : ", ", kv.first >= 0 && (size_t)kv.first < gSets.size() ? SetName(kv.first) : "?", kv.second); t += b; }
    return t;
}
void NetJoinFailed() {                                     // NewHostJoin's failure (US ntmenu2 0x36DB18): "( 1059 )"
    for (auto& kv : gNetJoin) {
        NetJoinPeer& j = kv.second;
        if (j.told || !j.connects) continue;
        Log("session: the join of %s failed: %u connects sent in %llu ms (%s), %u datagrams from it, %u of them verifying%s", PeerText(kv.first).c_str(),
            j.connects + j.copies, (unsigned long long)(GetTickCount64() - j.first), NetSetsText(j).c_str(), j.got, j.verified,
            !j.got ? ": no answer at all (the host's address or port unreachable, or a PC-only game's host)" : ": its answers in none of this PC's dialects");
        j.told = true;
    }
}
void NetJoinReset() { gNetJoin.clear(); gNetBoot.armed = false; }   // a game chosen in the lobby (JoinChosen)

// ---- a stock joiner's game channels (docs/VERSIONS.md, ps2world.cpp ChannelFamilies). The US, JP and JPS programs have
// the match's objects at 0x60..0x70, the EU and KR programs at 0x70..0x80; a PC learns a peer's place from its records. A
// stock joiner sends none before it holds the host's room state, which it drops in the other place: a PC host never
// kept an EU or KR stock joiner (US host, J2R2) nor a US or JPS one (EU host, J5R2). So: a peer of the EU dialect is in
// the EU program's place; a stock joiner of the US dialect (US, JP, JPS or KR) gets each reliable record of this host's
// in the US programs' place at its 1st send, the other at its 2nd, and so on (the other records by datagram, in turn)
// until its first record in one place (its acknowledgement) tells it; until then the records on 0x70 (both places
// use it) are dropped both ways (the reliable ones come again). MGO1_CHAN_PROBE=0: rc2's way.
bool NetChanProbeOn() { static const bool off = getenv("MGO1_CHAN_PROBE") && !strcmp(getenv("MGO1_CHAN_PROBE"), "0"); return !off; }
struct NetProbe { std::map<uint32_t, uint32_t> sends; uint32_t datagrams = 0, reliable = 0; ULONGLONG first = 0; bool said = false; };
std::map<uint64_t, NetProbe> gNetProbe;                    // the stock joiners whose place is not known yet (this host)
bool NetPeerPc(uint64_t peer);                             // ps2world.cpp (vnet): it answered this PC's hellos
template <class F> void NetRecords(uint8_t* p, size_t n, F fn) {   // each record: (record, its sequence byte or -1, data length)
    size_t i = 0;
    while (i + 3 <= n) {
        const uint8_t fl = p[i + 1];
        const size_t hdr = 3 + (fl & 0x20 ? 1 : 0) + (fl & 0x10 ? 1 : 0);
        if (i + hdr > n) return;
        const size_t len = fl & 0x20 ? (size_t)(p[i + 2] | p[i + 3] << 8) : p[i + 2];
        if (i + hdr + len > n) return;
        fn(p + i, fl & 0x10 ? (int)p[i + hdr - 1] : -1, len);
        i += hdr + len;
    }
}
int NetChanSeed(int region, int def) { return NetChanProbeOn() && region == 1 ? 1 : def; }   // the EU dialect: the EU program
// a datagram with a peer whose place is not known (ChannelFamilies): true when this probe took it (own: this program's
// place, 0 or 1); a sent one's records moved record by record, a received one's 0x70 dropped
bool NetChanProbe(bool out, uint64_t peer, int region, bool host, uint8_t* p, size_t n, int own) {
    if (!NetChanProbeOn() || region != 0 || !host || NetPeerPc(peer)) return false;
    NetProbe& pr = gNetProbe[peer];
    if (!pr.first) pr.first = GetTickCount64();
    const uint32_t lo = own ? 0x70 : 0x60;
    auto neutral = [](uint8_t* r) { r[0] = 0x3f; r[1] &= (uint8_t)~7; };   // an unopened channel: dropped on receipt
    if (!out) {
        NetRecords(p, n, [&](uint8_t* r, int, size_t) { if (!(r[1] & 7) && r[0] == 0x70) neutral(r); });
        return true;
    }
    const int turn = (int)(pr.datagrams++ & 1);
    bool rel = false;
    NetRecords(p, n, [&](uint8_t* r, int seq, size_t len) {
        if ((r[1] & 7) || r[0] < lo || r[0] > lo + 0x10) return;
        int place = turn;                                  // 0 the US programs', 1 the EU and KR programs'
        if (seq >= 0 && !(r[1] & 0x40) && len) { place = (int)(pr.sends[(uint32_t)r[0] << 8 | (uint32_t)seq]++ & 1); rel = true; }
        r[0] = (uint8_t)(r[0] + (place - own) * 0x10);
        if (r[0] == 0x70) neutral(r);
    });
    if (rel) ++pr.reliable;
    if (rel && !pr.said) {
        pr.said = true;
        Log("session: peer %s (the US dialect, not a PC: a US, JP, JPS or KR program): this host's game records in both programs' places in turn until it answers in one", PeerText(peer).c_str());
    }
    return true;
}
void NetChanLearnt(uint64_t peer, int place) {             // ChannelFamilies: its first record in one place
    auto it = gNetProbe.find(peer);
    if (it == gNetProbe.end()) return;
    if (it->second.reliable)
        Log("session: peer %s answers in the %s programs' place after %u datagrams of this host's with reliable game records (%llu ms)", PeerText(peer).c_str(),
            place ? "EU and KR" : "US, JP and JPS", it->second.reliable, (unsigned long long)(GetTickCount64() - it->second.first));
    gNetProbe.erase(it);
}

// ---- tests: a poor link on the way in (each instance's session datagrams received late or lost; tests/run_multi.sh
// instances on one machine): SIM_NET_DELAY=<ms>[:<jitter ms>] (each datagram from a session port, 5730..5799, held
// that long, plus 0..jitter: they may come in another order), SIM_NET_RLOSS=<percent> (lost on arrival),
// SIM_NET_FROM=<port>[,<port>...] (only those senders' datagrams). The network's clock (NetQpc): the game's timers'
struct NetHeld { long long due; sockaddr_in from; std::vector<char> data; };
struct NetLink { int delay = 0, jitter = 0, loss = 0; std::vector<int> from; bool on = false; };
const NetLink& NetLinkCfg() {
    static NetLink l; static bool parsed = false;
    if (parsed) return l;
    parsed = true;
    if (const char* e = DevEnv("SIM_NET_DELAY")) { l.delay = atoi(e); if (const char* c = strchr(e, ':')) l.jitter = atoi(c + 1); }
    if (const char* e = DevEnv("SIM_NET_RLOSS")) l.loss = atoi(e);
    if (const char* e = DevEnv("SIM_NET_FROM")) for (const char* q = e; *q;) { l.from.push_back(atoi(q)); q = strchr(q, ','); if (!q) break; ++q; }
    l.on = l.delay > 0 || l.jitter > 0 || l.loss > 0;
    if (l.on) Log("net: simulation: datagrams from the session ports %d ms late (+0..%d), %d %% lost on arrival%s", l.delay, l.jitter, l.loss, l.from.empty() ? "" : " (some senders only)");
    return l;
}
long long NetMsNow() { return NetQpc() / (QpcFreq() / 1000); }
std::map<SOCKET, std::deque<NetHeld>> gNetHeld;
// recvfrom of a non-blocking call with the link's delay: every datagram waiting is taken from the socket, those of the
// session's senders held until due; the first due one returned (-1 and WSAEWOULDBLOCK when none)
int NetRecvDelayed(SOCKET s, char* buf, int cap, sockaddr_in* from, int* fl, int& err) {
    const NetLink& l = NetLinkCfg();
    std::deque<NetHeld>& q = gNetHeld[s];
    static uint32_t lcg = 0x2468ACE1u;
    const long long now = NetMsNow();
    for (;;) {
        char tmp[2048]; sockaddr_in a{}; int al = sizeof a;
        const int r = recvfrom(s, tmp, (int)sizeof tmp, 0, (sockaddr*)&a, &al);
        if (r < 0) break;
        const int port = ntohs(a.sin_port);
        bool held = port >= 5730 && port < 5800 && (l.from.empty() || std::find(l.from.begin(), l.from.end(), port) != l.from.end());
        long long due = now;
        if (held) {
            lcg = lcg * 1103515245u + 12345u;
            if (l.loss > 0 && (int)((lcg >> 16) % 100) < l.loss) { static long long lost = 0; if (++lost <= 3 || !(lost & (lost - 1))) Log("net: simulation: a datagram from port %d lost on arrival (%lld so far)", port, lost); continue; }
            lcg = lcg * 1103515245u + 12345u;
            due = now + l.delay + (l.jitter > 0 ? (long long)((lcg >> 16) % (uint32_t)(l.jitter + 1)) : 0);
        }
        q.push_back({due, a, std::vector<char>(tmp, tmp + r)});
    }
    size_t best = q.size();
    for (size_t i = 0; i < q.size(); ++i) if (q[i].due <= now && (best == q.size() || q[i].due < q[best].due)) best = i;
    if (best == q.size()) { err = WSAEWOULDBLOCK; return -1; }
    NetHeld h = std::move(q[best]);
    q.erase(q.begin() + (long)best);
    const int n = (int)std::min<size_t>(h.data.size(), (size_t)cap);
    memcpy(buf, h.data.data(), (size_t)n);
    *from = h.from; *fl = sizeof(sockaddr_in);
    if ((int)h.data.size() > cap) { err = WSAEMSGSIZE; return -1; }
    err = 0;
    return n;
}

// ---- the grenades: one explosion, the thrower's, on every PC's screen (US n003a addresses, the maps paired with it
// through A3). A thrown grenade (NewBulletGrenadeWithBuller 0x4F1278, its init 0x4F0BB0) is made by each program from
// its copy of the thrower (the throw 0x3FF828: a0 the player, a1 the weapon); the thrower's own (owner +0x2E4 >= 0) is
// the one whose explosion wounds (NewBlastWound 0x356E78 -> 0x356840: no wound area for an owner < 0), and the damage
// goes in records 04 to the victims; the others' copies are pictures only, flown and timed by their own fuse
// (+0x2EC, the act 0x4F08B8: flags +0x2E8 bit 2 explode now, 0x4F0448 at the model's place, [+0x204] + 0x40). A copy
// could explode elsewhere than the thrower's grenade or before its damage came. Between PCs (the vnet channel, type 7):
// the thrower's PC tells each grenade it made (0) and where it exploded (1); a copy of a PC thrower's grenade then
// explodes where his exploded, as soon as told (held at most kNadeHoldActs of its acts past its own fuse while it waits:
// game time, as a slowed PC or a test instance's game runs apart from the machine's clock). PS2 players: the game's own.
// MGO1_BLAST_SYNC=0: the game's own between PCs too. SIM_BLAST_TRACE=1 (tests): each step logged, the machine's time
// (ms) on each line so screens compare on one clock
constexpr int kNadeHoldActs = 60;                         // 2 s of the game (30 acts a second)
bool NetBlastTraceOn() { static const bool on = getenv("SIM_BLAST_TRACE") != nullptr; return on; }
bool NetBlastSyncOn() { static const bool off = getenv("MGO1_BLAST_SYNC") && !strcmp(getenv("MGO1_BLAST_SYNC"), "0"); return !off && !Ps2Like(); }
int OwnSlot();
float NetF32(uint32_t a) { const uint32_t u = R32(a); float f; memcpy(&f, &u, 4); return f; }
void NetF32Set(uint32_t a, float f) { uint32_t u; memcpy(&u, &f, 4); W32(a, u); }
bool NetPtr(uint32_t a) { return a >= 0x100000 && a + 0x60 <= 0x02000000; }
std::string NetVec(uint32_t a) {
    if (a < 0x100000 || a + 12 > 0x02000000) { char b[24]; snprintf(b, sizeof b, "(%08x)", a); return b; }
    char b[64]; snprintf(b, sizeof b, "(%.0f %.0f %.0f)", NetF32(a), NetF32(a + 4), NetF32(a + 8)); return b;
}
struct NetNade { int slot = -1; bool own = false, told = false, done = false; uint16_t seq = 0; ULONGLONG made = 0, holdFrom = 0; float fuse = 0, step = 0; int acts = 0, heldActs = 0; };
struct NetNadeTold { int slot; uint8_t kind; uint16_t seq; float pos[3]; ULONGLONG at; bool used; };
struct NetNadeOut { std::vector<uint8_t> body; int sends; ULONGLONG last; };
std::map<uint32_t, NetNade> gNetNades;                    // the grenade actors made -> whose, which
std::deque<NetNadeTold> gNetNadesTold;                    // what the throwers' PCs told
std::deque<NetNadeOut> gNetNadesOut;                      // this PC's messages, sent by vnet::Pump
std::mutex gNetNadesLock;                                 // gNetNadesTold / gNetNadesOut (the vnet thread and the game's)
struct NetThrow { int slot = -1; bool copy = false; ULONGLONG at = 0; } gNetThrow;
uint16_t gNetNadeSeq = 0;
struct NetNadeStats { int made = 0, copies = 0, moved = 0, held = 0, alone = 0; float maxMove = 0; ULONGLONG maxHold = 0; } gNetNadeStats;
void NetNadeSay(uint8_t kind, uint16_t seq, const float* pos) {      // to the PCs (vnet::Pump: 3 times, 50 ms apart)
    std::vector<uint8_t> b = {kind, (uint8_t)seq, (uint8_t)(seq >> 8)};
    float z[3] = {0, 0, 0};
    const uint8_t* f = (const uint8_t*)(pos ? pos : z);
    b.insert(b.end(), f, f + 12);
    std::lock_guard<std::mutex> g(gNetNadesLock);
    gNetNadesOut.push_back({b, 0, 0});
    while (gNetNadesOut.size() > 32) gNetNadesOut.pop_front();
}
// a message of a thrower's PC (vnet, gVn.mu held): false if one of its copies came already
bool NetNadeHeard(int slot, const uint8_t* b, size_t n) {
    if (n < 15 || slot < 0 || slot >= 32 || slot == OwnSlot() || b[0] > 1) return false;
    NetNadeTold t{slot, b[0], (uint16_t)(b[1] | b[2] << 8), {0, 0, 0}, GetTickCount64(), false};
    memcpy(t.pos, b + 3, 12);
    for (float v : t.pos) if (!std::isfinite(v) || fabsf(v) > 1e7f) return false;   // not a place of a map
    std::lock_guard<std::mutex> g(gNetNadesLock);
    for (const auto& x : gNetNadesTold) if (x.slot == t.slot && x.kind == t.kind && x.seq == t.seq) return false;
    gNetNadesTold.push_back(t);
    while (gNetNadesTold.size() > 64 || (!gNetNadesTold.empty() && t.at - gNetNadesTold.front().at > 60000)) gNetNadesTold.pop_front();
    if (NetBlastTraceOn()) Log("blast: told at %llu ms: player %d's grenade %u %s (%.0f %.0f %.0f)", t.at, slot + 1, t.seq, t.kind ? "exploded at" : "made", t.pos[0], t.pos[1], t.pos[2]);
    return true;
}
// a copy's act: its making told by its thrower's PC (the one told nearest the copy's making, 20 s at most apart: the
// throw's motion runs at each screen's pace), then that grenade's explosion
NetNadeTold* NetNadeFind(const NetNade& n, uint8_t kind) {
    NetNadeTold* best = nullptr;
    for (auto& x : gNetNadesTold) {
        if (x.used || x.slot != n.slot || x.kind != kind) continue;
        if (kind) { if (x.seq == n.seq) return &x; continue; }
        const ULONGLONG apart = x.at > n.made ? x.at - n.made : n.made - x.at;
        if (apart <= 20000 && (!best || apart < (best->at > n.made ? best->at - n.made : n.made - best->at))) best = &x;
    }
    return best;
}
void NetNadeCopyAct(uint32_t g, NetNade& n) {
    const ULONGLONG now = GetTickCount64();
    const float fuse = NetF32(g + 0x2ec);
    if (n.acts++ && n.fuse > fuse) n.step = n.fuse - fuse;
    if (n.done) return;
    const uint32_t model = R32(g + 0x204);
    std::lock_guard<std::mutex> lk(gNetNadesLock);
    if (!n.told) if (NetNadeTold* m = NetNadeFind(n, 0)) { m->used = true; n.told = true; n.seq = m->seq; }   // its thrower is a PC
    if (NetNadeTold* b = n.told ? NetNadeFind(n, 1) : nullptr) {
        b->used = true;
        if (!NetPtr(model)) return;
        const float d = sqrtf((NetF32(model + 0x40) - b->pos[0]) * (NetF32(model + 0x40) - b->pos[0]) + (NetF32(model + 0x44) - b->pos[1]) * (NetF32(model + 0x44) - b->pos[1]) +
                              (NetF32(model + 0x48) - b->pos[2]) * (NetF32(model + 0x48) - b->pos[2]));
        for (int k = 0; k < 3; ++k) NetF32Set(model + 0x40 + 4 * k, b->pos[k]);
        W32(g + 0x2e8, R32(g + 0x2e8) | 4);              // this act explodes it there
        ++gNetNadeStats.moved; gNetNadeStats.maxMove = std::max(gNetNadeStats.maxMove, d);
        if (n.holdFrom) { ++gNetNadeStats.held; gNetNadeStats.maxHold = std::max(gNetNadeStats.maxHold, now - n.holdFrom); }
        if (gNetNadeStats.moved <= 3 || NetBlastTraceOn())
            Log("grenades: player %d's grenade %u exploded where his PC made it explode (%.0f %.0f %.0f, %.0f from this copy's place)%s", n.slot + 1, b->seq,
                b->pos[0], b->pos[1], b->pos[2], d, n.holdFrom ? ", held past its own fuse until told" : ", before its own fuse ran out");
        n.done = true;
        return;
    }
    if (n.told && n.step > 0 && fuse - n.step <= 0) {    // its fuse ends this act: held while the thrower's explosion is not told
        if (!n.holdFrom) n.holdFrom = now;
        if (++n.heldActs <= kNadeHoldActs) { NetF32Set(g + 0x2ec, n.step + 1e-4f); n.fuse = n.step + 1e-4f; return; }
        ++gNetNadeStats.alone; n.done = true;
        if (gNetNadeStats.alone <= 3 || NetBlastTraceOn())
            Log("grenades: player %d's grenade %u: its explosion not told in %d acts past this copy's fuse: exploded by itself", n.slot + 1, n.seq, kNadeHoldActs);
    }
    n.fuse = fuse;
}
// the hooks' n003a address of a call on another map page (gen/g_page_map.cpp pairs them on all 11: N3Page() holds only
// for n003a and a page with an extra map): its own addresses found once a page (P3), then a table of 64 (0: none)
uint32_t NetBlastPaged(uint32_t addr) {
    static const uint32_t kHooks[] = {0x3FF828, 0x4F1278, 0x4F0BB0, 0x4F08B8, 0x4F0448, 0x356E78, 0x358FA8, 0x356B98, 0x41EAA0, 0x4367A0};
    static uint32_t gen = 0xffffffffu, own[64], n3[64];
    if (gen != gPageGen) {
        gen = gPageGen;
        memset(own, 0, sizeof own);
        for (uint32_t h : kHooks) {
            const uint32_t p = P3(h);
            if (!p || p == h) continue;
            uint32_t k = (p >> 2) & 63;
            while (own[k]) k = (k + 1) & 63;
            own[k] = p; n3[k] = h;
        }
    }
    for (uint32_t k = (addr >> 2) & 63; own[k]; k = (k + 1) & 63) if (own[k] == addr) return n3[k];
    return 0;
}
// the hooks (DispatchBridge, a3 the call's n003a address): true when served here
bool NetBlast(Ctx& c, uint32_t a3, uint32_t va) {
    const bool trace = NetBlastTraceOn(), sync = NetBlastSyncOn();
    const unsigned long long ms = GetTickCount64();
    auto args = [&](const char* what) {
        Log("blast: %s at %llu ms: a0 %08x %s a1 %08x %s a2 %08x a3 %08x t0 %08x t1 %08x t2 %08x t3 %08x f12 %.3f ra %08x", what, ms,
            (uint32_t)c.r[4], NetVec((uint32_t)c.r[4]).c_str(), (uint32_t)c.r[5], NetVec((uint32_t)c.r[5]).c_str(), (uint32_t)c.r[6], (uint32_t)c.r[7],
            (uint32_t)c.r[8], (uint32_t)c.r[9], (uint32_t)c.r[10], (uint32_t)c.r[11], c.f[12], (uint32_t)c.r[31]);
    };
    switch (a3) {
    case 0x3FF828: {                                       // a throw (a0 the player, a1 the weapon): whose grenade is made next
        const uint32_t pl = (uint32_t)c.r[4];
        if (!NetPtr(pl)) return false;
        gNetThrow.slot = (uint32_t)c.r[5] == 0x13 ? (int)R8(pl + 0xb60) : -1;
        gNetThrow.copy = (R32(pl + 0xb5c) & 1) != 0; gNetThrow.at = ms;
        if (trace) Log("blast: player %08x (slot %u, %s) throws weapon %#x at %llu ms, facing %d pitch %d, at %s", pl, R8(pl + 0xb60), gNetThrow.copy ? "a copy" : "this PC's",
                       (uint32_t)c.r[5], ms, (int16_t)R16(pl + 0x14a), (int16_t)R16(pl + 0x140), NetVec(pl + 0x20).c_str());
        return false; }
    case 0x4F1278:                                         // the grenade made (a0 its matrix, a1 its direction)
        if (trace) {
            Log("blast: a grenade made at %llu ms: start %s direction %s pitch %d speed %.3f owner %d flags %08x t2 %d", ms, NetVec((uint32_t)c.r[4] + 0x30).c_str(),
                NetVec((uint32_t)c.r[5]).c_str(), (int16_t)c.r[9], c.f[12], (int32_t)c.r[8], (uint32_t)c.r[7], (int32_t)c.r[10]);
            args("a grenade thrown");
        }
        return false;
    case 0x4F0BB0: {                                       // its init (a0 the actor, t1 the owner: < 0 a copy)
        const uint32_t g = (uint32_t)c.r[4];
        const bool own = (int32_t)c.r[9] >= 0;
        NetNade n;
        n.made = ms;
        if (gNetThrow.slot >= 0 && ms - gNetThrow.at < 100) { n.slot = gNetThrow.slot; n.own = own && !gNetThrow.copy; }
        gNetThrow.slot = -1;
        for (auto it = gNetNades.begin(); it != gNetNades.end();) it = ms - it->second.made > 120000 ? gNetNades.erase(it) : std::next(it);
        if (n.slot >= 0) {
            if (n.own) { n.seq = ++gNetNadeSeq; ++gNetNadeStats.made; if (sync) NetNadeSay(0, n.seq, nullptr); }
            else ++gNetNadeStats.copies;
            gNetNades[g] = n;
        }
        if (trace) Log("blast: the grenade's actor %08x (%s, player %d, number %u)", g, n.slot < 0 ? "no throw known" : n.own ? "this PC's" : "a copy", n.slot + 1, n.seq);
        return false; }
    case 0x4F08B8: {                                       // a grenade's act
        const uint32_t g = (uint32_t)c.r[4];
        auto it = gNetNades.find(g);
        if (it == gNetNades.end()) return false;
        if (trace && (it->second.acts < 6 || it->second.acts % 3 == 0)) {
            std::string s;
            for (uint32_t off : {0x20u, 0x30u, 0x260u, 0x290u, 0x2a0u}) s += " +" + std::to_string(off) + NetVec(g + off);
            const uint32_t model = R32(g + 0x204);
            Log("blast: grenade %08x act %d at %llu ms:%s model+64 %s +2e0 %08x +2e4 %08x +2e8 %08x fuse %.4f +2f0 %d +300 %d", g, it->second.acts, ms, s.c_str(),
                NetPtr(model) ? NetVec(model + 0x40).c_str() : "-", R32(g + 0x2e0), R32(g + 0x2e4), R32(g + 0x2e8), NetF32(g + 0x2ec), (int32_t)R32(g + 0x2f0), (int32_t)R32(g + 0x300));
        }
        if (!it->second.own && sync) NetNadeCopyAct(g, it->second);
        else ++it->second.acts;
        return false; }
    case 0x4F0448: {                                       // it explodes (at [+0x204] + 0x40) when +0x2E0 bit 21
        const uint32_t g = (uint32_t)c.r[4];
        auto it = gNetNades.find(g);
        if (it == gNetNades.end()) return false;
        const uint32_t model = R32(g + 0x204);
        if (it->second.own && sync && (R32(g + 0x2e0) & 0x200000) && NetPtr(model)) {
            const float pos[3] = {NetF32(model + 0x40), NetF32(model + 0x44), NetF32(model + 0x48)};
            NetNadeSay(1, it->second.seq, pos);
        }
        if (trace) Log("blast: grenade %08x (%s, player %d, number %u) explodes at %llu ms at %s", g, it->second.own ? "this PC's" : "a copy", it->second.slot + 1, it->second.seq, ms,
                       NetPtr(model) ? NetVec(model + 0x40).c_str() : "-");
        gNetNades.erase(it);
        return false; }
    case 0x356E78:                                         // a0 the grenade model's matrix, a3 the owner (< 0: no damage), t0 the radius
        if (trace) Log("blast: an explosion at %llu ms at %s, owner %d (wound: radius %d, weapon %#x)", ms, NetVec((uint32_t)c.r[4] + 0x30).c_str(), (int32_t)c.r[7],
                       (int32_t)c.r[8], (uint32_t)c.r[10]);
        return false;
    case 0x358FA8:                                         // the explosion's effect (a0 its place)
        if (trace) Log("blast: an explosion's effect at %llu ms at %s, weapon %#x", ms, NetVec((uint32_t)c.r[4]).c_str(), (uint32_t)c.r[7]);
        return false;
    case 0x356B98:                                         // a0 its matrix; t0 < 0: no damage
        if (trace) Log("blast: an explosion at %llu ms at %s, owner %d", ms, NetVec((uint32_t)c.r[4] + 0x30).c_str(), (int32_t)c.r[8]);
        return false;
    case 0x41EAA0: {                                       // tests: a shot or throw record of a player's copy
        if (!trace) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t pl = (uint32_t)c.r[4];
        const uint64_t ra = c.r[31]; f(c); c.r[31] = ra;
        Log("blast: record 08 for player %08x (slot %u) at %llu ms: kind %u, the point %s, +f30 %08x", pl, R8(pl + 0xb60), ms, R8(pl + 0xde1), NetVec(pl + 0x430).c_str(), R32(pl + 0xf30));
        return true; }
    case 0x4367A0: {                                       // tests: a damage record (04) on the victim's copy: what it took
        if (!trace) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t v = (uint32_t)c.r[4];
        const uint16_t was = R16(v + 0xfbc);
        const uint64_t ra = c.r[31]; f(c); c.r[31] = ra;
        Log("blast: record 04 at %llu ms: player slot %u (health %d) hit by slot %d, type %u, damage %u", ms, R8(v + 0xb60), (int16_t)R16(v + 0xb64),
            (int8_t)R8(v + 0xef4), R8(v + 0xfb3), (unsigned)(uint16_t)(R16(v + 0xfbc) - was));
        return true; }
    default: return false;
    }
}
void NetNadeReport() {                                    // a match's end (vnet::Report)
    NetNadeStats& s = gNetNadeStats;
    if (s.made || s.copies)
        Log("grenades: %d thrown here, %d copies of others'; %d copies exploded where their thrower's PC made them explode (moved %.0f at most), %d of them held past their fuse (%llu ms at most), %d exploded by themselves after %d acts",
            s.made, s.copies, s.moved, s.maxMove, s.held, (unsigned long long)s.maxHold, s.alone, kNadeHoldActs);
    s = NetNadeStats{};
}

// ---- the socket calls that wait (docs/NETWORK_STACK.md, the audit's B_WAIT / B_SELECT). A PS2 thread's blocking call
// (net::Blocking) gives the baton back with state 6, the state of DelayThread's sleep, and its wakeAt as its last sleep
// left it (0 if it never slept): the kernel loop (NativeKernelLoop: state 6 and wakeAt <= now -> ready) took it back at
// once and gave it the baton, and every PS2 thread, the game's included, then waited for the call's end. Now its
// wakeAt is put out of reach while it waits (the kernel never takes it for a due sleep) and it makes itself ready when
// the call ends, the kernel woken. MGO1_NET_WAIT=0: rc1's way (tests). A select with a time-out waits on the calling
// thread with the baton (20 ms at most). Both measured: a line every 60 s with blocking calls or selects
struct NetWaits { long long calls = 0, held = 0, heldUs = 0, maxUs = 0, sel = 0, selUs = 0, selMaxUs = 0; ULONGLONG since = 0; };
NetWaits gNetWaits;
void NetKernelWake();                                     // ps2world.cpp, after gKernelWake
bool NetWaitOn() { static const bool off = DevEnv("MGO1_NET_WAIT") && !strcmp(DevEnv("MGO1_NET_WAIT"), "0"); return !off; }
void NetWaitsReport() {
    NetWaits& w = gNetWaits;
    const ULONGLONG now = GetTickCount64();
    if (!w.since) w.since = now;
    if (now - w.since < 60000) return;
    if (w.calls || w.sel)
        Log("net: in %llu s: %lld blocking socket calls, %lld of them taken back by the kernel while they waited (the game held %.1f ms, at most %.1f); %lld selects with a time-out waited %.1f ms (at most %.1f) with the baton",
            (unsigned long long)((now - w.since) / 1000), w.calls, w.held, w.heldUs / 1000.0, w.maxUs / 1000.0, w.sel, w.selUs / 1000.0, w.selMaxUs / 1000.0);
    w = NetWaits{}; w.since = now;
}
long long NetUsNow() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return t.QuadPart * 1000000 / f.QuadPart; }
void NetWaitBegin(EeThread* t) { if (NetWaitOn()) t->wakeAt = 0x7fffffffffffffffLL; }   // no due sleep while it waits
void NetWaitEnd(EeThread* t, long long wakeWas, long long t0, bool held) {   // its state ready again (2)
    const long long us = NetUsNow() - t0;
    ++gNetWaits.calls;
    if (held) { ++gNetWaits.held; gNetWaits.heldUs += us; gNetWaits.maxUs = std::max(gNetWaits.maxUs, us); }
    if (NetWaitOn()) { t->wakeAt = wakeWas; NetKernelWake(); }
    NetWaitsReport();
}
void NetSelectWaited(long long us) { ++gNetWaits.sel; gNetWaits.selUs += us; gNetWaits.selMaxUs = std::max(gNetWaits.selMaxUs, us); NetWaitsReport(); }
