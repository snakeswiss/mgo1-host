// (v150-host beta 21, spin) How a virtual doll's body turns, as the other players' games see it (MGO1Loader/vdoll_body.cpp
// TurnBody, docs/VIRTUAL_DOLL.md 24): a map's .geom, one doll at each of the map's spawn points (8 at most), run a frame
// at a time; its facing taken from the records it sends every 6 frames, as a player's game gets them.
//   runs: 60 s at each point with an enemy far off that moves from point to point (its hunts, its fights when it sees him)
//   duels: an enemy appearing 5 to 25 m away, 0 to 180 degrees off its facing, running across its sight, 240 frames each
//   VDOLL_TEST_AT=x,y,z: one more run of 60 s from that place (its hips; a spot whose mesh is small: the shuttle)
// Checks (MGO1_VDOLL_TURN on, the default), seven:
//   1 no turn wider than 96 degrees between two records (16 a frame, the readied turn's ceiling; a roll's record and a
//     jump of its place over 1.5 m aside);
//   2 no about turn (150 degrees or more) in one record;
//   3 no spin: 300 degrees one way within 90 frames, 200 or more of them out of a fight (with less it is its fight's own
//     turn, the aim shown in its records, to an enemy further round the way it had just turned: never held back,
//     counted apart and printed);
//   4 no such spin of its COPY as a player's game shows it: the copy turned toward each record's facing over about 25
//     frames, the measure of a playable doll's own screen (bench spin_b0);
//   5 every shot told as it was fired: the record before a shot's record faced within 40 degrees of it;
//   6 half the map's respawn points or more can be left by its ways (vdoll_body.h CanLeave: vdoll.cpp does not respawn a
//     doll at one that cannot; they are listed), and with VDOLL_TEST_AT the given place is one that cannot;
//   7 it hits him in 9 of 10 duels where it saw him 1.5 s or more before the duel's end, 2 misses allowed (level 10,
//     direct: a duel is 4 s long; a lower level's reaction and settling, or a player's controls, take longer).
// A drop's or a climb's records (none while it lasts, then the landing's {01 0 0 0 0 0 0}, a player's game's own) are no
// facing: the turn is taken across them, from the facing before to the one after. MGO1_VDOLL_TURN=0 (beta 20's body)
// fails 1 to 5: the "before". Also printed: the jiggle (two records in a row turning 10 degrees or more the other
// way), its returns on itself (within 1.5 m of a place 2 to 6 s before after 6 m of run) a minute, and the duels' frames
// from its sight of the enemy (its `sees player` line) to its first shot and its first hit, by the angle he appeared at
// (the cost). VDOLL_TEST_EVENTS=1 lists each event with the records around it and the body's lines (MGO1_VDOLL_DEBUG=1).
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_turn_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_turn_test
//   MGO1_VDOLL_LEVEL=10 ./vdoll_turn_test <map.geom>
#include "doll_geom.h"
#include "vdoll_body.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0, gSawAt = -1;                 // (gSawAt: its `sees player 3` line of this duel)
static std::vector<std::pair<long long, std::string>> gLines;   // (VDOLL_TEST_EVENTS: the body's lines, by frame)
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("%s\n", b);
    if (getenv("VDOLL_TEST_EVENTS")) gLines.push_back({gNow, b});
    if (strstr(b, ": sees player 3") && gSawAt < 0) gSawAt = gNow;
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return 0; }                         // (deathmatch: everyone an enemy)

// a record tick: the facing (01), the place (00), a roll (06 01), a shot (08 05), a hit on the enemy
struct Tick { long long frame; float yaw = 0, x = 0, z = 0; bool haveYaw = false, havePos = false, roll = false, shot = false, land = false, aim = false; };
static std::vector<Tick> gTicks;
static long long gFirstHit = -1;
static Tick& Cur() { if (gTicks.empty() || gTicks.back().frame != gNow) { gTicks.push_back(Tick()); gTicks.back().frame = gNow; } return gTicks.back(); }
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) {
    if (slot == 2 && kind == 0 && r.size() >= 9 && r[0] == 0x04) { if (gFirstHit < 0) gFirstHit = gNow; return; }
    if (slot != 1 || r.empty()) return;
    if (kind == 0 && r[0] == 0x01 && r.size() >= 5) { Tick& t = Cur(); t.yaw = (r[3] | r[4] << 8) * 360.f / 65536.f; t.haveYaw = true; t.land = !r[3] && !r[4]; }
    else if (kind == 0 && r[0] == 0x00 && r.size() >= 13) { Tick& t = Cur(); float p[3]; memcpy(p, &r[1], 12); t.x = p[0]; t.z = p[2]; t.havePos = true; }
    else if (kind == 1 && r[0] == 0x06 && r.size() >= 2 && r[1] == 0x01) Cur().roll = true;
    else if (kind == 1 && r[0] == 0x08 && r.size() >= 2 && r[1] == 0x05) Cur().shot = true;
    else if (kind == 2 && r[0] == 0x12 && r.size() >= 2 && r[1] != 0xFF) Cur().aim = true;   // (its aim shown: in a fight)
}
static float WrapD(float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; }

// (VDOLL_TEST_EVENTS=1) one event: the records around it (frame:facing) and the body's lines of the 2 s before
static void Event(const char* what, const std::vector<Tick>& t, size_t i) {
    if (!getenv("VDOLL_TEST_EVENTS")) return;
    printf("  %s at frame %lld:", what, t[i].frame);
    for (size_t k = i >= 6 ? i - 6 : 0; k <= i + 2 && k < t.size(); ++k) printf(" %lld:%.0f%s%s", t[k].frame, t[k].yaw, t[k].shot ? "s" : "", t[k].roll ? "r" : "");
    printf(" at %.0f %.0f\n", t[i].x, t[i].z);
    for (const auto& l : gLines)
        if (l.first > t[i].frame - 120 && l.first <= t[i].frame + 6 && !strstr(l.second.c_str(), ": hits player") && !strstr(l.second.c_str(), "vdoll: ") && !strstr(l.second.c_str(), " body: "))
            printf("      f%lld %s\n", l.first, l.second.c_str());
}
struct Figures { int pairs = 0, wide = 0, about = 0, spins = 0, track = 0, jig = 0, back = 0, shots = 0, unshown = 0, lands = 0, scr = 0, scrFight = 0; float most = 0, seconds = 0; };
// the figures of one stretch of ticks (one run or one duel)
static void Measure(const std::vector<Tick>& in, Figures& f) {
    std::vector<Tick> t;
    for (size_t i = 0; i < in.size(); ++i) {
        // (a facing of zero right after records with no facing, or after none at all: the landing's own record, or a
        // grab's end: no facing)
        if (in[i].haveYaw && in[i].land && i > 0 && (!in[i - 1].haveYaw || in[i].frame - in[i - 1].frame > 12)) { ++f.lands; continue; }
        if (in[i].haveYaw) t.push_back(in[i]);
    }
    if (t.size() < 2) return;
    f.seconds += (t.back().frame - t.front().frame) / 60.f;
    std::vector<float> d(t.size(), 0.f);
    std::vector<bool> ok(t.size(), false);
    float lx = t[0].x, lz = t[0].z;
    for (size_t i = 1; i < t.size(); ++i) {
        const float jump = std::hypot(t[i].x - lx, t[i].z - lz);
        if (t[i].havePos) { lx = t[i].x; lz = t[i].z; }
        d[i] = WrapD(t[i].yaw - t[i - 1].yaw);
        ok[i] = !t[i].roll && !t[i - 1].roll && jump < 1500.f && t[i].frame - t[i - 1].frame <= 150;   // (150: across a drop, a climb)
        if (!ok[i]) continue;
        ++f.pairs;
        const float a = std::fabs(d[i]) * 6.f / std::max(1.f, (float)(t[i].frame - t[i - 1].frame));
        f.most = std::max(f.most, a);
        if (a > 96.5f) { ++f.wide; Event("a wide turn", t, i); }
        if (std::fabs(d[i]) >= 150.f) ++f.about;
        if (i >= 2 && ok[i - 1] && std::fabs(d[i]) >= 10.f && std::fabs(d[i - 1]) >= 10.f && (d[i] > 0) != (d[i - 1] > 0)) ++f.jig;
        if (t[i].shot) { ++f.shots; if (std::fabs(d[i]) > 40.f) { ++f.unshown; Event("a shot not shown", t, i); } }
    }
    for (size_t i = 1; i < t.size();) {
        float acc = 0.f, free = 0.f;
        size_t j = i;
        bool hit = false;
        for (; j < t.size() && t[j].frame - t[i - 1].frame <= 90; ++j) {
            if (ok[j]) { acc += d[j]; if (!t[j].aim) free += std::fabs(d[j]); }
            if (std::fabs(acc) >= 300.f) { hit = true; break; }
        }
        // (a spin: 200 degrees or more of it turned out of a fight. Less: its fight's turn, the aim shown in its records, to
        // an enemy who appears further round the way it had just turned, or after one who runs round it: a player's turn
        // too, never held back; counted apart)
        if (hit && free < 200.f) { ++f.track; Event("a turn into a fight", t, j); i = j + 1; }
        else if (hit) { ++f.spins; Event("a spin", t, j); i = j + 1; } else ++i;
    }
    // the copy on a player's screen, a frame at a time: turned toward the last record's facing the short way round, each
    // frame 8.5 % of what is left and 5.5 degrees at least (bench spin_b0, a playable doll's own screen: after a record's
    // 180 degrees 42, 74, 99, 118, 137, 152 at 3 frame steps); an exact about turn the way round of its last turn (a
    // player's game takes either: the worse one here). Its spins: 300 degrees one way within 90 frames, 200 or more of them
    // out of a fight
    {
        float copy = t[0].yaw, last = 1.f;
        std::vector<float> turn, out;                  // (a frame's turn; the same out of a fight)
        size_t k = 0;
        for (long long fr = t[0].frame; fr <= t.back().frame; ++fr) {
            while (k + 1 < t.size() && t[k + 1].frame <= fr) ++k;
            if (k > 0 && !ok[k] && t[k].frame == fr) { copy = t[k].yaw; turn.push_back(0.f); out.push_back(0.f); continue; }   // (a respawn, a roll: put there)
            float e = WrapD(t[k].yaw - copy);
            if (std::fabs(e) > 179.5f) e = last * std::fabs(e);
            const float step = std::min(std::fabs(e), std::max(5.5f, 0.085f * std::fabs(e))), dd = e < 0.f ? -step : step;
            if (step > 0.01f) last = e < 0.f ? -1.f : 1.f;
            copy = WrapD(copy + dd);
            turn.push_back(dd); out.push_back(t[k].aim ? 0.f : std::fabs(dd));
        }
        for (size_t i = 0; i < turn.size();) {
            float acc = 0.f, free = 0.f;
            size_t j = i;
            bool hit = false;
            for (; j < turn.size() && j - i <= 90; ++j) { acc += turn[j]; free += out[j]; if (std::fabs(acc) >= 300.f) { hit = true; break; } }
            if (hit && free >= 200.f) { ++f.scr; i = j + 1; } else if (hit) { ++f.scrFight; i = j + 1; } else ++i;
        }
    }
    std::vector<float> cum(t.size(), 0.f);
    for (size_t i = 1; i < t.size(); ++i) cum[i] = cum[i - 1] + std::min(1500.f, std::hypot(t[i].x - t[i - 1].x, t[i].z - t[i - 1].z));
    for (size_t i = 0; i < t.size();) {
        size_t j = i + 1;
        bool found = false;
        for (; j < t.size() && t[j].frame - t[i].frame <= 360; ++j)
            if (t[j].frame - t[i].frame >= 120 && cum[j] - cum[i] >= 6000.f && std::hypot(t[j].x - t[i].x, t[j].z - t[i].z) < 1500.f) { found = true; break; }
        if (found) { ++f.back; i = j; } else ++i;
    }
}
static void Print(const char* what, const Figures& f) {
    printf("%s: %.0f s, %d record pairs; the widest turn %.1f degrees a record (over 96: %d), about turns %d, spins %d; jiggle %.2f a minute, "
           "back on itself %.2f a minute; shots %d (shown less than 40 degrees from the record before: %d off); landings %d; turns of 300 degrees into a fight %d; on a player's screen (the copy turned as his game turns it) spins %d, into a fight %d\n", what, f.seconds, f.pairs, f.most, f.wide,
           f.about, f.spins, f.jig / std::max(1e-3f, f.seconds / 60.f), f.back / std::max(1e-3f, f.seconds / 60.f), f.shots, f.unshown, f.lands, f.track, f.scr, f.scrFight);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_turn_test <map.geom>\n"); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); gGeom.resize(ftell(fp)); fseek(fp, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), fp) != gGeom.size()) return 1;
    fclose(fp);
    doll::Geom g;
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    std::vector<const doll::Spawn*> spawns;
    for (const doll::Spawn& sp : g.spawns) if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) spawns.push_back(&sp);
    if (spawns.size() < 2) { printf("fewer than 2 spawn points in the geom\n"); return 1; }
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 0);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    // (beta 21, at the merge with cover) this test measures the turns: the cover's waits before a shot (the 250 ms reflex, the
    // doll's own copy shown) and its gate are off unless set, so that check 7's duels keep the level's own reaction. With them
    // on, a target crossing its sight at 4 m a second leaves it before the first shot more often (n013a: 33 hits in 37 duels
    // seen, n024a 15 in 19, against 33 in 35 and 16 in 16 with them off); their own test is tests/vdoll_cover_test.cpp
    setenv("MGO1_VDOLL_REFLEX", "0", 0); setenv("MGO1_VDOLL_SHOWN", "0", 0); setenv("MGO1_VDOLL_COVER", "0", 0);
    const bool turnOn = !(getenv("MGO1_VDOLL_TURN") && !strcmp(getenv("MGO1_VDOLL_TURN"), "0"));
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    printf("%s: level %s, controls %s, MGO1_VDOLL_TURN %s; the cover's reflex %s, copy shown %s, gate %s\n", argv[1], getenv("MGO1_VDOLL_LEVEL"), vdoll::body::Controls(),
           turnOn ? "on" : "0 (beta 20)", getenv("MGO1_VDOLL_REFLEX"), getenv("MGO1_VDOLL_SHOWN"), getenv("MGO1_VDOLL_COVER"));
    // (beta 21) the respawn points the body's ways cannot leave (vdoll.cpp takes the next point): each kind's points
    int leftOut = 0, points = 0;
    bool atLeft = true;
#ifdef VDOLL_BODY_CANLEAVE
    for (const doll::Spawn& sp : g.spawns) {
        ++points;
        if (vdoll::body::CanLeave(sp.p)) continue;
        ++leftOut;
        printf("a respawn point with no way out (kind %d): %.0f %.0f %.0f\n", (int)sp.kind, sp.p[0], sp.p[1], sp.p[2]);
    }
    printf("respawn points: %d, with no way out for the body %d%s\n", points, leftOut, turnOn ? "" : " (MGO1_VDOLL_TURN=0: none is left out)");
    { float a[3]; if (getenv("VDOLL_TEST_AT") && sscanf(getenv("VDOLL_TEST_AT"), "%f,%f,%f", &a[0], &a[1], &a[2]) == 3) { a[1] -= 800.f; atLeft = !vdoll::body::CanLeave(a); } }
#endif
    long long fr = 600;
    float pos[3];
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
    auto frameAt = [&](long long t) { gNow = t; memcpy(gPlayers[1].pos, pos, sizeof pos); vdoll::body::Frame(t); s.tick = t % 6 == 0; vdoll::body::Alive(s, t); };
    // the runs
    Figures runs;
    std::vector<std::array<float, 3>> starts;
    for (size_t si = 0; si < spawns.size() && si < 8; ++si) starts.push_back({spawns[si]->p[0], spawns[si]->p[1] + 800.f, spawns[si]->p[2]});
    float at[3];
    const bool extra = getenv("VDOLL_TEST_AT") && sscanf(getenv("VDOLL_TEST_AT"), "%f,%f,%f", &at[0], &at[1], &at[2]) == 3;
    for (size_t si = 0; si < starts.size(); ++si) {
        memcpy(pos, starts[si].data(), sizeof pos);
        facing = (uint16_t)(si * 9000);
        s.life = fr;                                   // (a new life)
        gTicks.clear();
        gHave[2] = true; gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1; gPlayers[2].object = gPlayers[2].alive = true; gPlayers[2].life = 200;
        for (long long t = fr; t < fr + 3600; ++t) {
            if ((t - fr) % 1200 == 0) {                // (the enemy at another point every 20 s)
                const auto* sp = spawns[(si + 1 + (size_t)((t - fr) / 1200) * 3) % spawns.size()];
                gPlayers[2].pos[0] = sp->p[0]; gPlayers[2].pos[1] = sp->p[1] + 800.f; gPlayers[2].pos[2] = sp->p[2];
            }
            frameAt(t);
        }
        fr += 3600;
        Measure(gTicks, runs);
    }
    Print("runs", runs);
    Figures spot;
    if (extra) {
        memcpy(pos, at, sizeof pos);
        s.life = fr;
        gTicks.clear();
        gHave[2] = false;
        for (long long t = fr; t < fr + 3600; ++t) frameAt(t);
        fr += 3600;
        Measure(gTicks, spot);
        Print("the given place", spot);
    }
    // the duels: the enemy appears off its facing and runs across its sight (4 m a second)
    Figures duels;
    std::vector<long long> firstHit, toShot[4], toHit[4];   // (by the angle he appeared at: 0, 60, 120, 180)
    int saw = 0, sawHit = 0;
    const float dists[] = {5000.f, 12000.f, 25000.f};
    const float offs[] = {0.f, 60.f, 120.f, 180.f};
    for (size_t si = 0; si < starts.size() && si < 6; ++si)
        for (float dist : dists)
            for (int oi = 0; oi < 4; ++oi) {
                const float off = offs[oi];
                memcpy(pos, starts[si].data(), sizeof pos);
                const float yaw = (float)(si * 41 % 360) * 3.14159265f / 180.f;
                facing = (uint16_t)lrintf(yaw / 6.2831853f * 65536.f);
                s.life = fr;
                gHave[2] = false;
                for (long long t = fr; t < fr + 30; ++t) frameAt(t);
                fr += 30;
                gTicks.clear();
                const float y0 = facing / 65536.f * 6.2831853f, b = y0 + off * 3.14159265f / 180.f;
                gHave[2] = true; gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1; gPlayers[2].object = gPlayers[2].alive = true; gPlayers[2].life = 200;
                float e[3] = {pos[0] + std::sin(b) * dist, pos[1], pos[2] + std::cos(b) * dist};
                gFirstHit = -1; gSawAt = -1;
                long long shotAt = -1;
                vdoll::body::AimInfo a0;
                vdoll::body::Aim(0, &a0);
                for (long long t = fr; t < fr + 240; ++t) {
                    const float k = 4000.f / 60.f * (float)(t - fr) * (((t - fr) / 60) % 2 ? -1.f : 1.f);   // (to and fro across its line)
                    gPlayers[2].pos[0] = e[0] + std::cos(b) * k * 0.5f; gPlayers[2].pos[1] = e[1]; gPlayers[2].pos[2] = e[2] - std::sin(b) * k * 0.5f;
                    frameAt(t);
                    vdoll::body::AimInfo a;
                    if (shotAt < 0 && vdoll::body::Aim(0, &a) && a.shots > a0.shots) shotAt = t;
                }
                if (gSawAt >= fr + 150) gSawAt = -1;   // (seen in the duel's last 1.5 s: no time for a fight, not counted)
                if (gSawAt >= 0 && gFirstHit < 0 && getenv("VDOLL_TEST_EVENTS")) {
                    printf("  a duel with no hit: point %zu, he appeared at %.0f m, %.0f degrees off; seen at frame %lld (%lld after), its first shot %lld\n", si, dist / 1000.f, off,
                           gSawAt, gSawAt - fr, shotAt);
                    for (const auto& l : gLines) if (l.first >= fr && l.first < fr + 240 && !strstr(l.second.c_str(), "vdoll: ") && !strstr(l.second.c_str(), " body: ")) printf("      f%lld %s\n", l.first, l.second.c_str());
                }
                if (gSawAt >= 0) {
                    ++saw;
                    if (gFirstHit >= 0) { ++sawHit; toHit[oi].push_back(gFirstHit - gSawAt); }
                    if (shotAt >= 0) toShot[oi].push_back(shotAt - gSawAt);
                }
                if (gFirstHit >= 0) firstHit.push_back(gFirstHit - fr);
                fr += 240;
                Measure(gTicks, duels);
            }
    std::sort(firstHit.begin(), firstHit.end());
    Print("duels", duels);
    printf("duels: %zu of 72 hit him; the first hit %lld frames after he appeared (median); it saw him in %d, hit him in %d of them\n", firstHit.size(),
           firstHit.empty() ? -1LL : firstHit[firstHit.size() / 2], saw, sawHit);
    for (int oi = 0; oi < 4; ++oi) {
        std::sort(toShot[oi].begin(), toShot[oi].end()); std::sort(toHit[oi].begin(), toHit[oi].end());
        auto at = [](const std::vector<long long>& v, float q) { return v.empty() ? -1LL : v[std::min(v.size() - 1, (size_t)(q * (float)(v.size() - 1) + 0.5f))]; };
        printf("duels: he appears %3.0f degrees off its facing: from its sight to its first shot %lld frames (median; 9 of 10 within %lld; %zu duels), to its first hit %lld (%lld)\n",
               offs[oi], at(toShot[oi], 0.5f), at(toShot[oi], 0.9f), toShot[oi].size(), at(toHit[oi], 0.5f), at(toHit[oi], 0.9f));
    }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    const Figures* all[] = {&runs, &duels, &spot};
    int wide = 0, about = 0, spins = 0, unshown = 0;
    for (const Figures* f : all) { wide += f->wide; about += f->about; spins += f->spins; unshown += f->unshown; }
    check(wide == 0, "no turn wider than 96 degrees between two records (16 degrees a frame)");
    check(about == 0, "no about turn (150 degrees or more) in one record");
    check(spins == 0, "no spin (300 degrees one way within 1.5 s)");
    check(runs.scr + duels.scr + spot.scr == 0, "no spin of its copy on a player's screen (the copy turned over about 25 frames, as a player's game turns it)");
    check(unshown == 0, "every shot's facing shown by the record before it (within 40 degrees)");
#ifdef VDOLL_BODY_CANLEAVE
    if (turnOn) {
        check(points == 0 || leftOut * 2 <= points, "half its respawn points or more can be left by its ways");
        if (extra) check(atLeft, "the given place is no respawn point for it: no way leads out of it");
    }
#endif
    check(saw >= 15 && saw - sawHit <= std::max(2, saw / 10), "it hits him in 9 of 10 duels where it saw him 1.5 s or more before the duel's end (15 duels or more; 2 misses allowed)");
    return fails ? 1 : 0;
}
