// net_channels_test.cpp: a PC host's game channels with a stock joiner (netfix.h NetChanSeed / NetChanProbe and
// ps2world.cpp ChannelFamilies, cut out by net_channels_test.sh) on a fake memory: the room state (a reliable record)
// resent until acknowledged, a stock receiver that takes only its own place's channels and acknowledges in them.
//   bash tests/net_channels_test.sh    (builds and runs it natively; exit 0 when every case passes)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
typedef unsigned long long ULONGLONG;
static ULONGLONG gNow = 1000;
static ULONGLONG GetTickCount64() { return gNow; }
namespace ps2 { static const char* gVer = "US"; inline const char* SelectedVersion() { return gVer; } }
namespace w {
static std::vector<uint8_t> gMemV(32u << 20);
static uint8_t* gMem = gMemV.data();
static int gLogs = 0;
static bool gQuiet = false;
static void Log(const char* f, ...) { va_list ap; va_start(ap, f); if (!gQuiet) { printf("    log: "); vprintf(f, ap); printf("\n"); } va_end(ap); ++gLogs; }
static std::string PeerText(uint64_t p) { char b[32]; snprintf(b, sizeof b, "peer%llx", (unsigned long long)p); return b; }
static bool gBigGame = false;
static int gOwnFamily = 0, gRegion = -1;
static bool gHost = true, gPc = false;
static const uint64_t kPeer = 0xC0000202166bULL;
static uint64_t PeerOf(uint32_t) { return kPeer; }
static int OwnChanFamily() { return gOwnFamily; }
static std::map<uint64_t, int> gPeerChanFamily;
static int PeerRegion(uint64_t) { return gRegion; }
static bool IsSessionHost() { return gHost; }
bool NetPeerPc(uint64_t) { return gPc; }
#include "chan_part.h"
}
using namespace w;
static int fails = 0;
static void Check(bool ok, const char* what) { printf("  %s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
struct Rec { uint8_t ch, fl; int seq; std::vector<uint8_t> data; };
static std::vector<uint8_t> Build(const std::vector<Rec>& rs) {   // [sequence u16][records]
    std::vector<uint8_t> d = {0, 0};
    for (const Rec& r : rs) {
        d.push_back(r.ch); d.push_back((uint8_t)(r.fl | (r.seq >= 0 ? 0x10 : 0))); d.push_back((uint8_t)r.data.size());
        if (r.seq >= 0) d.push_back((uint8_t)r.seq);
        d.insert(d.end(), r.data.begin(), r.data.end());
    }
    return d;
}
static std::vector<Rec> Parse(const std::vector<uint8_t>& d) {
    std::vector<Rec> rs;
    size_t i = 2;
    while (i + 3 <= d.size()) {
        Rec r; r.ch = d[i]; r.fl = d[i + 1]; const size_t len = d[i + 2]; size_t h = 3;
        r.seq = -1; if (r.fl & 0x10) { r.seq = d[i + 3]; h = 4; }
        r.data.assign(d.begin() + (long)(i + h), d.begin() + (long)(i + h + len));
        rs.push_back(r); i += h + len;
    }
    return rs;
}
static std::vector<uint8_t> Through(bool out, std::vector<uint8_t> d) {   // ChannelFamilies on the datagram in the fake memory
    const uint32_t buf = 0x00400000, sess = 0x00300000;
    memcpy(gMem + buf, d.data(), d.size());
    ChannelFamilies(out, sess, buf, (uint32_t)d.size());
    return std::vector<uint8_t>(gMem + buf, gMem + buf + d.size());
}
// the stock receiver of place pf: takes the game records on its place's channels, acknowledges the reliable ones there
struct Stock {
    int pf;
    std::vector<std::pair<uint8_t, int>> taken;            // (its channel, the host's send number of that record)
    std::vector<uint8_t> roomCh;                           // the channels the host's room state reached it on
    bool wrongObject = false;
    bool Opened(uint8_t ch) const { const int lo = pf ? 0x70 : 0x60; return ch < 0x60 || (ch >= lo && ch <= lo + 0x10); }
};
static int Expect(uint8_t hostCh, int hf, int pf) { return hostCh + (pf - hf) * 0x10; }
// one case: a host of place hf (version ver), a stock peer of place pf and dialect region; returns the send at which the
// room state (host 0x62 / 0x72, seq 0) was first taken by the peer (0: never in 8 sends)
static int RoomCase(const char* name, const char* ver, int hf, int pf, int region, bool host, bool pc, bool probe, bool interleave, Stock* out = nullptr) {
    printf("%s\n", name);
    ps2::gVer = ver; gOwnFamily = hf; gRegion = region; gHost = host; gPc = pc; gBigGame = false;
    gPeerChanFamily.clear(); gNetProbe.clear();
    Stock st{pf};
    const uint8_t room = (uint8_t)(hf ? 0x72 : 0x62), score = (uint8_t)(hf ? 0x70 : 0x60), wnd = (uint8_t)(hf ? 0x80 : 0x70);
    int takenAt = 0; bool acked = false; int sendNo = 0, ySeq = 1;
    for (int k = 1; k <= 8 && !acked; ++k) {
        std::vector<Rec> rs;
        rs.push_back({room, 0x00, 0, {1, 2, 3, 4}});      // the room state, seq 0, resent until acknowledged
        rs.push_back({score, 0x08, -1, {9, 9}});          // a scoreboard snapshot (unreliable)
        rs.push_back({wnd, 0x00, -1, {7}});               // a window's state (unreliable)
        rs.push_back({0x41, 0x00, -1, {5}});              // a player's record (every place's)
        ++sendNo;
        auto wire = Parse(Through(true, Build(rs)));
        std::vector<Rec> acks;
        for (size_t i = 0; i < wire.size(); ++i) {
            const Rec& r = wire[i]; const Rec& h = rs[i];
            if (r.ch == 0x3f || !st.Opened(r.ch)) continue;  // dropped (an unopened channel)
            if (r.ch >= 0x60 && r.ch != Expect(h.ch, hf, pf)) st.wrongObject = true;
            if (r.ch < 0x60 && r.ch != h.ch) st.wrongObject = true;
            st.taken.push_back({r.ch, sendNo});
            if (h.ch == room) { st.roomCh.push_back(r.ch); if (!takenAt) takenAt = k; }
            if (r.seq >= 0) acks.push_back({r.ch, 0x40, r.seq, {}});
        }
        if (interleave) {                                  // another reliable record, new each time (seq 1, 2 ...)
            std::vector<Rec> ys = {{(uint8_t)(room + 2), 0x00, ySeq++, {8}}};
            auto yw = Parse(Through(true, Build(ys)));
            if (st.Opened(yw[0].ch) && yw[0].ch != 0x3f && yw[0].ch != Expect(ys[0].ch, hf, pf)) st.wrongObject = true;
        }
        if (!acks.empty()) {                               // back to the host
            auto back = Parse(Through(false, Build(acks)));
            for (const Rec& a : back) if (a.ch == room && a.seq == 0) acked = true;
        }
        gNow += 100;
    }
    // after it: a scoreboard snapshot and a window's state reach the peer's own objects
    std::vector<Rec> rs = {{score, 0x08, -1, {9}}, {wnd, 0x00, -1, {7}}};
    auto wire = Parse(Through(true, Build(rs)));
    bool late = true;
    for (size_t i = 0; i < wire.size(); ++i) if (wire[i].ch != Expect(rs[i].ch, hf, pf)) late = false;
    if (takenAt) Check(late, "after the acknowledgement the scoreboard and the window reach the peer's own objects");
    if (probe) Check(!st.wrongObject, "no record taken on another object's channel");
    else printf("  info: rc2's way: %s\n", st.wrongObject ? "a record taken on another object's channel (0x70)" : "no record on another object's channel");
    if (out) *out = st;
    return takenAt;
}
int main(int argc, char** argv) {
    int at;
    if (argc > 1 && !strcmp(argv[1], "off")) {               // run with MGO1_CHAN_PROBE=0 (net_channels_test.sh)
        at = RoomCase("9. MGO1_CHAN_PROBE=0 (rc2's way): a US host, a stock KR joiner", "US", 0, 1, 0, true, false, false, false);
        Check(at == 0, "never taken (the J2R2 failure)");
        at = RoomCase("10. MGO1_CHAN_PROBE=0: an EU host, a stock US joiner", "EU", 1, 0, 0, true, false, false, false);
        Check(at == 0, "never taken (the J5R2 failure)");
        printf(fails ? "net_channels_test (off): %d FAILED\n" : "net_channels_test (off): all passed\n", fails);
        return fails ? 1 : 0;
    }
    at = RoomCase("1. a US host, a stock EU joiner (the EU dialect)", "US", 0, 1, 1, true, false, true, false);
    Check(at == 1, "the room state taken at its 1st send (in the EU place at once)");
    Check(gPeerChanFamily.count(kPeer) && gPeerChanFamily[kPeer] == 1, "the joiner's place learnt: EU and KR");
    at = RoomCase("2. a US host, a stock KR joiner (the US dialect, the EU place)", "US", 0, 1, 0, true, false, true, false);
    Check(at == 2, "the room state taken at its 2nd send");
    Check(gPeerChanFamily.count(kPeer) && gPeerChanFamily[kPeer] == 1, "the joiner's place learnt from its acknowledgement: EU and KR");
    Check(gNetProbe.empty(), "the probe ended");
    at = RoomCase("3. a US host, a stock JP joiner (the US dialect, the US place)", "US", 0, 0, 0, true, false, true, false);
    Check(at == 1, "the room state taken at its 1st send");
    Check(gPeerChanFamily.count(kPeer) && gPeerChanFamily[kPeer] == 0, "the joiner's place learnt: US, JP and JPS");
    at = RoomCase("4. an EU host, a stock US joiner (J5)", "EU", 1, 0, 0, true, false, true, false);
    Check(at == 1, "the room state taken at its 1st send (moved to 0x62)");
    at = RoomCase("5. an EU host, a stock KR joiner", "EU", 1, 1, 0, true, false, true, false);
    Check(at == 2, "the room state taken at its 2nd send");
    at = RoomCase("6. a KR host, a stock KR joiner", "KR", 1, 1, 0, true, false, true, false);
    Check(at == 2, "the room state taken at its 2nd send (rc2: never, the servers' place 0)");
    at = RoomCase("7. a US host, a stock KR joiner, another reliable record new at each send", "US", 0, 1, 0, true, false, true, true);
    Check(at == 2, "the room state taken at its 2nd send");
    {   // a PC peer: no probe, the place as before (it moves its own records)
        const int logs = gLogs;
        printf("8. a US host, a PC of the US dialect (it answered the hellos)\n");
        ps2::gVer = "US"; gOwnFamily = 0; gRegion = 0; gHost = true; gPc = true; gPeerChanFamily.clear(); gNetProbe.clear();
        std::vector<Rec> rs = {{0x62, 0x00, 0, {1}}, {0x70, 0x00, -1, {7}}};
        auto wire = Parse(Through(true, Build(rs)));
        Check(wire[0].ch == 0x62 && wire[1].ch == 0x70 && gNetProbe.empty() && gLogs == logs, "its records as before (no probe, nothing moved)");
    }
    {   // a PC joiner with an EU dialect host: its records in the EU place from the first
        printf("11. a US PC joiner, its host of the EU dialect\n");
        ps2::gVer = "US"; gOwnFamily = 0; gRegion = 1; gHost = false; gPc = false; gPeerChanFamily.clear(); gNetProbe.clear();
        std::vector<Rec> rs = {{0x62, 0x40, 3, {}}};
        auto wire = Parse(Through(true, Build(rs)));
        Check(wire[0].ch == 0x72, "its acknowledgement on 0x72 (rc2: 0x62 until the host's records told it)");
    }
    {   // a received 0x70 from a peer not known: dropped; a received 0x72 tells its place
        printf("12. a US host, a stock joiner of the US dialect not known yet: its records\n");
        ps2::gVer = "US"; gOwnFamily = 0; gRegion = 0; gHost = true; gPc = false; gPeerChanFamily.clear(); gNetProbe.clear();
        auto a = Parse(Through(false, Build({{0x70, 0x08, -1, {1}}, {0x41, 0x00, -1, {2}}})));
        Check(a[0].ch == 0x3f && a[1].ch == 0x41 && !gPeerChanFamily.count(kPeer), "its 0x70 dropped (either place's), its player's record kept, its place still not known");
        auto b = Parse(Through(false, Build({{0x72, 0x40, 0, {}}, {0x70, 0x08, -1, {1}}})));
        Check(b[0].ch == 0x62 && b[1].ch == 0x60 && gPeerChanFamily[kPeer] == 1, "its acknowledgement on 0x72: EU and KR, its records moved (0x72 -> 0x62, 0x70 -> 0x60)");
    }
    {   // a big game: no place to learn (every peer a PC)
        printf("13. a big game\n");
        gBigGame = true; gRegion = 0; gHost = true; gPeerChanFamily.clear(); gNetProbe.clear();
        auto a = Parse(Through(true, Build({{0x62, 0x00, 0, {1}}})));
        Check(a[0].ch == 0x62 && gNetProbe.empty(), "nothing moved, no probe");
        gBigGame = false;
    }
    {   // malformed datagrams (the audit's T_BAD_INPUT): a record longer than the datagram, a header cut short
        printf("14. a probed peer's malformed datagrams\n");
        ps2::gVer = "US"; gOwnFamily = 0; gRegion = 0; gHost = true; gPc = false; gPeerChanFamily.clear(); gNetProbe.clear();
        std::vector<uint8_t> d = Build({{0x62, 0x00, 0, {1, 2}}, {0x64, 0x00, 1, {3, 4, 5}}});
        d[2 + 4 + 2 + 2] = 200;                            // the second record's length past the end
        auto a = Through(true, d);
        Check(a.size() == d.size() && a[2] == 0x62 && a[6 + 2] == 0x64 && a[6 + 4] == 200, "the first record placed, the bad one left as it was, nothing written past the end");
        std::vector<uint8_t> e = {0, 0, 0x62, 0x10};      // a header of 2 bytes
        auto b = Through(false, e);
        Check(b == e && !gPeerChanFamily.count(kPeer), "a header cut short: nothing changed, nothing learnt");
        std::vector<uint8_t> f(2 + 255 * 4, 0);           // many empty records on channel 0
        for (size_t i = 2; i + 3 <= f.size(); i += 3) { f[i] = 0; f[i + 1] = 0; f[i + 2] = 0; }
        auto c = Through(true, f);
        Check(c.size() == f.size(), "255 empty records: walked to the end");
    }
    printf(fails ? "net_channels_test: %d FAILED\n" : "net_channels_test: all passed\n", fails);
    return fails ? 1 : 0;
}
