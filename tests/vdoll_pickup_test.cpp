// (v150-host beta 22, pickup) A virtual doll and the special items lying on the map (MGO1Loader/vdoll_body.cpp, docs/VIRTUAL_DOLL.md
// 32), off line on a map's .geom: the map's own points of the four specials (the stage's chunk 7, by hash: M63 EE0C73..76,
// flamethrower 016282..85, RPG-7 4B5C74..77, thermal goggles 282BBE..C1), a stand-in of the host's drops' records, one doll
// started at a spawn point near one of them (6 to 14 m, the box in its sight) or far from all of them (over 25 m). Checked:
//   near    the doll goes to the special and takes it within 20 s, with the records a player's game sends: on the drops' channel
//           (Self::send kind 3) the claim {number, kind << 10 | code, 1, x y z in tenths, count}, then 6 frames later the record
//           emptied {number, code, 3, 0, count}; at the claim its hips in the game's pick-up zone (400 mm along x and z)
//   far     a special over 25 m away is never taken nor walked to in 20 s
//   rule    MGO1_VDOLL_PICKUP=0 nothing; the weapons setting with no primary: the M63 left, the goggles taken; the RPG-7 and the
//           flamethrower left while the body does not fire them (MGO1_VDOLL_PICKUP=2: taken, carried)
//   m63     its row never faster than a playable doll's M63 (6 frames a shot, 100 a magazine, 351 across its reload); taken,
//           an enemy 8 to 14 m ahead: its weapon record names the M63 (0x0D), its hits the M63's code and the game's damage (70,
//           a head 280, a leg 35), no two shots under its pace, its rounds go down from the box's count
//   fight   in a fight, a special at its feet taken at once (the game's pick-up: standing in the zone takes it)
//   goggles the goggles taken in SNEAKING: worn (item 7 in its weapon record), Snake camouflaged 15 m away seen; without them
//           (the same doll, the same Snake) not seen
//   drop    its death with the M63 held: a record of its own number (slot << 10 | 0x100), kind 3, code 0x0D, state 0, its rounds left
// (v150-host beta 22, review: reports/review22b.md P1 to P4; each fails on 656e1d8's body on the 12 disc maps)
//   pile    the M63 and the goggles lying at one place: both taken, each claim followed by its record emptied
//   race    a player's take reaching the host just after the doll's look at the drops, the doll 0.7 m away: no claim
//   spent   an M63 of 3 rounds, primary weapons only: its last round fired, its AK-47 in its hands within 60 frames
//   battery the goggles off (under 20), on again (over 90) 168 s later at the soonest (the game's best rate)
//   heavyoff (with heavy's body) MGO1_VDOLL_HEAVY=0: the RPG-7 and the flamethrower left lying (fails on 4c2e06e)
// (v150-host beta 22, pickup2: reports/review22b.md P6, P7, P9, P10; each fails on 12d949b's body on the 12 disc maps)
//   owed    a take's emptied record 6 frames after its claim whatever comes then: knocked down (no Alive: vdoll.cpp calls Owed
//           each frame), the round's end screens (the room's phase 4), its leave (Quit: at once)
//   quit    its own leave holding the M63 and the goggles: each dropped where it stands, a record of its own (the game's pause
//           menu QUIT drops them); the host's kick: nothing dropped
//   again   its M63 spent, another M63 at its feet: taken, that box's rounds (an empty special blocked every other)
//   goggles (MGO1_VDOLL_GOGGLES_CAMO=0) the goggles on, Snake camouflaged there not seen
// Beta 21's body (no take) fails near, m63, goggles and drop.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_pickup_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_pickup_test
//   ./vdoll_pickup_test <map.geom>        (MGO1_VDOLL_LEVEL as on the host; VDOLL_TEST_VERBOSE=1 the body's log)
#include "doll_geom.h"
#include "vdoll_body.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static int gRule = 1, gWeaponRule = 0;
static std::vector<std::string> gLines;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    gLines.push_back(b);
    if (getenv("VDOLL_TEST_VERBOSE")) printf("  | %s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return gRule; }
static int WeaponRule() { return gWeaponRule; }
static int gPhase = 2;                                   // (pickup2: the room's phase, 4 the round's end screens)
static int Phase() { return gPhase; }
#ifdef VDOLL_HOST_DROPS
static std::vector<vdoll::DropNow> gDrops;
// (v150-host beta 22, review) gTakenAfterRead >= 0: that record taken by a player right after the next read (his claim reaching
// the host just after the doll's look at the drops)
static int gTakenAfterRead = -1;
static int Drops(vdoll::DropNow* out, int max) {
    int n = 0; for (const auto& d : gDrops) if (n < max) out[n++] = d;
    if (gTakenAfterRead >= 0) { for (auto& d : gDrops) if (d.number == gTakenAfterRead) { d.state = 1; d.box = false; } gTakenAfterRead = -1; }
    return n;
}
#endif

struct Rec { long long frame; int slot, kind; std::vector<uint8_t> r; };
static std::vector<Rec> gRecs;
static long long gNow = 0;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) { gRecs.push_back({gNow, slot, kind, rec}); }

static int gFails = 0, gPasses = 0;
static void Check(bool ok, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("%s ", ok ? "PASS" : "FAIL");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    if (ok) ++gPasses; else ++gFails;
}
static int U16(const std::vector<uint8_t>& r, size_t at) { return r[at] | r[at + 1] << 8; }
static int S16(const std::vector<uint8_t>& r, size_t at) { return (int16_t)U16(r, at); }
static unsigned U32(const std::vector<uint8_t>& r, size_t at) { return r[at] | r[at + 1] << 8 | r[at + 2] << 16 | (unsigned)r[at + 3] << 24; }

struct Point { int kind; float p[3]; };          // (a special's point of the map: 0 M63, 1 RPG-7, 2 flamethrower, 3 goggles)
static const int kCode[4] = {0x0D, 0x11, 0x09, 0x07};
static const int kCount[4] = {401, 15, 999, 1};
static const int kNumber[4] = {0x52, 0x51, 0x53, 0x54};
static const char* kName[4] = {"M63", "RPG-7", "flamethrower", "goggles"};

#ifdef VDOLL_HOST_DROPS
static vdoll::DropNow Special(int kind, const float* p) {
    vdoll::DropNow d;
    d.number = kNumber[kind]; d.kind = kind == 3 ? 1 : 3; d.code = kCode[kind]; d.count = kCount[kind]; d.state = 0;
    d.flags = (unsigned)(d.kind << 10 | d.code); memcpy(d.pos, p, sizeof d.pos); d.box = true; d.reach = 0;
    return d;
}
#endif

static bool WayTo(const float* from, const float* to, float* len, float* end) {
#ifdef VDOLL_BODY_PICKUP
    return vdoll::body::WayTo(from, to, len, end);
#else
    *len = std::hypot(to[0] - from[0], to[2] - from[2]); memcpy(end, to, 3 * sizeof(float)); return true;   // (beta 21's body: no way asked)
#endif
}
// one run of the doll from a place for n frames (an enemy or not); the records kept. Returns the frame of its claim (-1 none)
struct Run { long long claim = -1, second = -1; std::vector<uint8_t> claimRec, secondRec; float hipsAtClaim[3] = {0, 0, 0}; bool walked = false; };
static long long gFrameAt = 600;
static int gLife = 0;
static Run RunDoll(float* pos, uint16_t* facing, int frames, int team = 0, bool newLife = true, int stopAfterClaim = -1) {
    Run r;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = team; s.pos = pos; s.facing = facing; s.send = Send;
    if (newLife) ++gLife;
    s.life = gLife;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = team; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
    for (long long t = gFrameAt; t < gFrameAt + frames; ++t) {
        gNow = t;
        if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
        const size_t r0 = gRecs.size();
        vdoll::body::Frame(t);
        s.tick = t % 6 == 0;
        vdoll::body::Alive(s, t);
        for (size_t i = r0; i < gRecs.size(); ++i) {
            const Rec& x = gRecs[i];
            if (x.kind != 3 || x.r.size() < 5) continue;
            if (x.r[4] == 0x01 && r.claim < 0) { r.claim = t; r.claimRec = x.r; memcpy(r.hipsAtClaim, pos, sizeof r.hipsAtClaim); }
            else if (x.r[4] == 0x03 && r.second < 0) { r.second = t; r.secondRec = x.r; }
        }
        if (stopAfterClaim >= 0 && r.claim >= 0 && t >= r.claim + stopAfterClaim) break;   // (stopped there: the duels' place)
#ifdef VDOLL_HOST_DROPS
        // (the host's record: taken, the box gone; (v150-host beta 22, review) every record it claimed, not its first only: two
        // specials lying at one place)
        for (size_t i = r0; i < gRecs.size(); ++i)
            if (gRecs[i].kind == 3 && gRecs[i].r.size() == 15 && gRecs[i].r[4] == 0x01)
                for (auto& d : gDrops) if (d.number == U16(gRecs[i].r, 0)) { d.kind = 0; d.state = 3; d.box = false; }
#endif
    }
    gFrameAt += frames + 600;
    for (const std::string& l : gLines) if (l.find("goes to take the") != std::string::npos) r.walked = true;
    return r;
}
// a player put where the doll sees him: r from its place (the radii in turn), within 130 degrees of its facing (its field at
// level 10: 280), his chest and head in its eye's line; false: no such place
static bool PlaceFoe(int id, const float* at, uint16_t facing, const std::vector<float>& radii, int team, int cls, int life) {
    const float yaw0 = facing / 65536.f * 6.2831853f;
    for (float r : radii)
        for (int j = 0; j < 27; ++j) {
            const float yaw = yaw0 + (j & 1 ? -1.f : 1.f) * (float)((j + 1) / 2) * 0.1745f;
            float e[3] = {at[0] + std::sin(yaw) * r, at[1], at[2] + std::cos(yaw) * r};
            const float eye[3] = {at[0], at[1] + 750.f, at[2]}, chest[3] = {e[0], e[1] + 350.f, e[2]}, head[3] = {e[0], e[1] + 850.f, e[2]};
            if (vdoll::body::SightStopped(eye, chest) || vdoll::body::SightStopped(eye, head)) continue;
            float len = 0, end[3], feet[3] = {e[0], e[1] - 800.f, e[2]};
            if (!WayTo(feet, feet, &len, end) && std::fabs(e[1] - at[1]) > 1.f) continue;
            gHave[id] = true; gPlayers[id] = vdoll::PlayerNow(); gPlayers[id].team = team; gPlayers[id].object = gPlayers[id].alive = true; gPlayers[id].life = life; gPlayers[id].cls = cls;
            memcpy(gPlayers[id].pos, e, sizeof e);
            return true;
        }
    return false;
}
static void Settle(long long frames) {                   // (the body out of the match a while: a new life next)
    for (long long t = gFrameAt; t < gFrameAt + frames; t += 2) { gNow = t; vdoll::body::Frame(t); }
    vdoll::body::Down(0);
    gFrameAt += frames;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_pickup_test <map.geom>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 1;
    fclose(f);
    doll::Geom g;
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    // the map's own points of the four specials (32 bytes each: the hash, a word, x y z, 1)
    std::vector<Point> points;
    const uint32_t first[4] = {0xEE0C73, 0x4B5C74, 0x016282, 0x282BBE};
    for (int k = 0; k < 4; ++k)
        for (uint32_t h = first[k]; h < first[k] + 4; ++h)
            for (size_t i = 0; i + 24 <= gGeom.size(); i += 4) {
                uint32_t w; memcpy(&w, &gGeom[i], 4);
                if (w != h) continue;
                Point p; p.kind = k; memcpy(p.p, &gGeom[i + 8], 12);
                if (std::fabs(p.p[0]) < 1e6f && std::fabs(p.p[2]) < 1e6f) { points.push_back(p); break; }
            }
    printf("%s: %zu points of the specials\n", argv[1], points.size());
    if (points.empty()) { printf("no special's point on this map\n"); return 1; }
    // the places a doll starts from: the map's spawn points of every rule and the specials' own points (all on the floor)
    std::vector<doll::Spawn> starts;
    for (const doll::Spawn& sp : g.spawns)
        if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE || sp.kind == doll::SP_TDM_FIRST_RED || sp.kind == doll::SP_TDM_FIRST_BLUE ||
            sp.kind == doll::SP_TCM_RED || sp.kind == doll::SP_TCM_BLUE || sp.kind == doll::SP_OBJECT_RED || sp.kind == doll::SP_OBJECT_BLUE) starts.push_back(sp);
    for (const Point& p : points) { doll::Spawn sp; sp.hash = 0; sp.kind = doll::SP_UNKNOWN; memcpy(sp.p, p.p, sizeof sp.p); starts.push_back(sp); }
    std::vector<const doll::Spawn*> spawns;
    for (const doll::Spawn& sp : starts) spawns.push_back(&sp);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);   // (the owner's server's level; its field 280 degrees)
    if (!getenv("MGO1_VDOLL_WEAPONS")) setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.weaponRule = WeaponRule; h.phase = Phase;
#ifdef VDOLL_HOST_DROPS
    h.drops = Drops;
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }

    // the near cases: for each special's point a spawn point 6 to 14 m away with the box in its sight (3 at most a kind)
    struct Case { const Point* p; const doll::Spawn* sp; };
    std::vector<Case> near, far;
    for (const Point& p : points) {
        int n = 0;
        for (const doll::Spawn* sp : spawns) {
            const float d = std::hypot(sp->p[0] - p.p[0], sp->p[2] - p.p[2]);
            const float eye[3] = {sp->p[0], sp->p[1] + 1550.f, sp->p[2]}, box[3] = {p.p[0], p.p[1] + 300.f, p.p[2]};
            if (d < 6000.f || d > 14000.f || std::fabs(sp->p[1] - p.p[1]) >= 2500.f || vdoll::body::SightStopped(eye, box) || n >= 2) continue;
            // (a way the bodies run to it: floors, drops, climbs of a metre, no ladder; half as long again at most, its end by the box)
            float len = 0, end[3];
            if (!WayTo(sp->p, p.p, &len, end) || len > d * 1.5f + 3000.f || std::hypot(end[0] - p.p[0], end[2] - p.p[2]) > 1000.f || std::fabs(end[1] - p.p[1]) > 600.f) continue;
            near.push_back({&p, sp}); ++n;
        }
    }
    // far: a special 20 to 60 m from a start (the one special of the run)
    for (const Point& p : points)
        for (const doll::Spawn* sp : spawns) {
            const float d = std::hypot(sp->p[0] - p.p[0], sp->p[2] - p.p[2]);
            if (d >= 20000.f && d <= 60000.f && far.size() < 4 && (far.empty() || far.back().p != &p)) { far.push_back({&p, sp}); break; }
        }
    printf("%zu near cases, %zu far cases\n", near.size(), far.size());
    Check(!near.empty(), "a special's point with a spawn point 6 to 14 m away in its sight on this map");

    // near: taken, a player's records
    int taken = 0, tried = 0, zoneOk = 0, recOk = 0, gapOk = 0;
    std::vector<const Case*> took;
    for (const Case& c : near) {
        if (c.p->kind == 1 || c.p->kind == 2) continue;      // (the RPG-7 and the flamethrower: rule below)
        ++tried;
#ifdef VDOLL_HOST_DROPS
        gDrops.assign(1, Special(c.p->kind, c.p->p));
#endif
        float pos[3] = {c.sp->p[0], c.sp->p[1] + 800.f, c.sp->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(c.p->p[0] - pos[0], c.p->p[2] - pos[2]) / 6.2831853f * 65536.f);
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 1200);
        if (r.claim >= 0) {
            ++taken; took.push_back(&c);
            const std::vector<uint8_t>& a = r.claimRec;
            const bool ok = a.size() == 15 && U16(a, 0) == kNumber[c.p->kind] && U16(a, 2) == ((c.p->kind == 3 ? 1 : 3) << 10 | kCode[c.p->kind]) && a[4] == 1 &&
                            S16(a, 5) == (int)(c.p->p[0] * 0.1f) && S16(a, 7) == (int)(c.p->p[1] * 0.1f) && S16(a, 9) == (int)(c.p->p[2] * 0.1f) && U32(a, 11) == (unsigned)kCount[c.p->kind];
            const std::vector<uint8_t>& e = r.secondRec;
            const bool ok2 = e.size() == 13 && U16(e, 0) == kNumber[c.p->kind] && U16(e, 2) == kCode[c.p->kind] && e[4] == 3 && U32(e, 5) == 0 && U32(e, 9) == (unsigned)kCount[c.p->kind];
            if (ok && ok2) ++recOk;
            else { printf("  the %s's records:", kName[c.p->kind]); for (uint8_t x : a) printf(" %02x", x); printf(" /"); for (uint8_t x : e) printf(" %02x", x); printf("\n"); }
            if (r.second - r.claim == 6) ++gapOk;
            if (std::fabs(r.hipsAtClaim[0] - c.p->p[0]) < 400.f && std::fabs(r.hipsAtClaim[2] - c.p->p[2]) < 400.f) ++zoneOk;
        } else printf("  the %s at %.0f %.0f %.0f not taken from %.0f %.0f %.0f (%.1f m)\n", kName[c.p->kind], c.p->p[0], c.p->p[1], c.p->p[2], c.sp->p[0], c.sp->p[1], c.sp->p[2],
                      std::hypot(c.sp->p[0] - c.p->p[0], c.sp->p[2] - c.p->p[2]) / 1000.f);
        Settle(120);
    }
    Check(tried > 0 && taken * 10 >= tried * 8, "near: %d of %d specials 6 to 14 m away in its sight taken within 20 s", taken, tried);
    Check(taken > 0 && recOk == taken, "near: each take a player's records (the claim, then the record emptied): %d of %d", recOk, taken);
    Check(taken > 0 && gapOk == taken, "near: the record emptied 6 frames after the claim: %d of %d", gapOk, taken);
    Check(taken > 0 && zoneOk == taken, "near: at each claim its hips in the game's pick-up zone (400 mm along x and z): %d of %d", zoneOk, taken);

    // far: never
    int farTaken = 0, farWalked = 0;
    for (const Case& c : far) {
#ifdef VDOLL_HOST_DROPS
        gDrops.assign(1, Special(c.p->kind == 1 || c.p->kind == 2 ? 0 : c.p->kind, c.p->p));
#endif
        float pos[3] = {c.sp->p[0], c.sp->p[1] + 800.f, c.sp->p[2]};
        uint16_t facing = 0;
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 1200);
        if (r.claim >= 0) ++farTaken;                    // (its own ways may bring it near the special: then it was near)
        for (const std::string& l : gLines) if (l.find("goes to take the") != std::string::npos) {
            float m = 0; if (sscanf(l.c_str() + l.find("lying ") + 6, "%f", &m) == 1 && m > 15.05f) ++farWalked;
        }
        Settle(120);
    }
    Check(!far.empty() && farWalked == 0, "far: no special walked to from over 15 m away (%d walks; %d taken after its own ways came near it, %zu runs)", farWalked, farTaken, far.size());

    // the rules: the switch off, no primary, the RPG-7 and the flamethrower (not fired by this body)
    const Case* m63 = nullptr; const Case* gog = nullptr; const Case* rpg = nullptr;
    // (the duels' cases: a point a doll took a special at, from where a foe can stand in its sight at the duel's range (a dry
    // run at the point, facing as it came); its own kind first, else another point, as the game might put the item there)
    auto duelOk = [&](const Case* c, const std::vector<float>& radii) {
        const float at[3] = {c->p->p[0], c->p->p[1] + 800.f, c->p->p[2]};
        const uint16_t f = (uint16_t)lrintf(std::atan2(c->p->p[0] - c->sp->p[0], c->p->p[2] - c->sp->p[2]) / 6.2831853f * 65536.f);
        const bool ok = PlaceFoe(5, at, f, radii, 1, 0, 200);
        gHave[5] = false;
        return ok;
    };
    const std::vector<float> rM63 = {12000.f, 10000.f, 14000.f, 8000.f}, rSnake = {15000.f, 13000.f, 18000.f, 12000.f, 21000.f, 25000.f, 11500.f};
    for (int pass = 0; pass < 2; ++pass)
        for (const Case* c : took) {
            if (!m63 && (pass || c->p->kind == 0) && duelOk(c, rM63)) m63 = c;
            if (!gog && (pass || c->p->kind == 3) && duelOk(c, rSnake)) gog = c;
        }
    for (const Case& c : near) if ((c.p->kind == 1 || c.p->kind == 2) && !rpg) rpg = &c;
    if (!took.empty()) { if (!m63) m63 = took[0]; if (!gog) gog = took[0]; }
    if (!near.empty() && !rpg) rpg = &near[0];
    if (rpg) {
#ifdef VDOLL_HOST_DROPS
        gDrops.assign(1, Special(rpg->p->kind == 1 || rpg->p->kind == 2 ? rpg->p->kind : 1, rpg->p->p));
#endif
        float pos[3] = {rpg->sp->p[0], rpg->sp->p[1] + 800.f, rpg->sp->p[2]};
        uint16_t facing = 0;
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 900);
#ifdef VDOLL_BODY_HEAVY
        Check(r.claim >= 0, "rule: the %s taken (agent heavy's body fires it)", kName[rpg->p->kind == 2 ? 2 : 1]);
#else
        Check(r.claim < 0, "rule: the %s left lying while the body does not fire it", kName[rpg->p->kind == 2 ? 2 : 1]);
#endif
        Settle(120);
    }
    if (m63) {
#ifdef VDOLL_HOST_DROPS
        gDrops.assign(1, Special(0, m63->p->p));
#endif
        gWeaponRule = 1;                                 // (the game's weapons setting: no primary)
        float pos[3] = {m63->sp->p[0], m63->sp->p[1] + 800.f, m63->sp->p[2]};
        uint16_t facing = 0;
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 900);
        Check(r.claim < 0, "rule: the M63 left lying when the weapons setting allows no primary");
        gWeaponRule = 0;
        Settle(120);
    }

    // the game's rule: standing on a special in a fight (an enemy in its sight from the start), it takes it at once
    if (!took.empty()) {
        const Case* c = took[0];
#ifdef VDOLL_HOST_DROPS
        gDrops.clear();
#endif
        float pos[3] = {c->p->p[0], c->p->p[1] + 800.f, c->p->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(c->p->p[0] - c->sp->p[0], c->p->p[2] - c->sp->p[2]) / 6.2831853f * 65536.f);
        const bool foe = PlaceFoe(2, pos, facing, {12000.f, 10000.f, 8000.f, 15000.f, 6000.f}, 1, 0, 200);
        gRecs.clear(); gLines.clear();
        RunDoll(pos, &facing, 40);                       // (it sees him and fights: still where it stands)
        bool fought = false;
        for (const std::string& l : gLines) if (l.find("sees player 3") != std::string::npos) fought = true;
#ifdef VDOLL_HOST_DROPS
        { const float at[3] = {pos[0], pos[1] - 800.f, pos[2]}; gDrops.assign(1, Special(c->p->kind == 3 ? 3 : 0, at)); }   // (then a special at its feet)
#endif
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 60, 0, false);
        Check(foe && fought && r.claim >= 0 && r.claim - (gFrameAt - 660) < 30, "rule: in a fight (an enemy %s), a special at its feet taken within half a second (frame %lld)",
              foe && fought ? "seen" : "not seen", r.claim >= 0 ? r.claim - (gFrameAt - 660) : -1);
        gHave[2] = false;
        Settle(120);
    }

    // m63: taken, then an enemy 12 m ahead
    if (m63) {
#ifdef VDOLL_HOST_DROPS
        gDrops.assign(1, Special(0, m63->p->p));
#endif
        float pos[3] = {m63->sp->p[0], m63->sp->p[1] + 800.f, m63->sp->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(m63->p->p[0] - pos[0], m63->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
        gRecs.clear(); gLines.clear();
        RunDoll(pos, &facing, 1200, 0, true, 12);
        bool had = false;
#ifdef VDOLL_BODY_PICKUP
        vdoll::body::PickupInfo pi;
        had = vdoll::body::Pickup(0, &pi) && pi.weapon == 0x0D && pi.rounds == 401;
#endif
        Check(had, "m63: the M63 held after its take, the box's 401 rounds");
        // an enemy where it can see him: 12 m along its facing on the mesh's floor (the duel of the other tests)
        int shots = 0, m63Rec = 0, hits = 0, badDmg = 0, fast = 0, m63Every = 6;
        {                                                // (the M63's pace: its row of the dolls' table, measured on a playable doll)
            vdoll::body::WeaponRow w;
            bool row = false;
            for (int i = 0; vdoll::body::WeaponAt(i, &w); ++i) if (w.code == 0x0D) { m63Every = w.every; row = true;
                Check(w.every >= 6 && w.magazine <= 100 && w.reload >= 351 && w.automatic, "m63: its row never faster than a playable doll's M63 (a shot %d frames, measured 6; %d a magazine, 100; %d across a reload, 351)",
                      w.every, w.magazine, w.reload); }
            if (!row) Check(false, "m63: the M63 in the dolls' table");
        }
        long long lastShot = -100000;
        const bool found = PlaceFoe(2, pos, facing, rM63, 1, 0, 200);
        Check(found, "m63: an enemy placed 8 to 14 m away in its sight");
        vdoll::body::Self s;
        s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send; s.life = gLife;
        vdoll::body::AimInfo was, now;
        vdoll::body::Aim(0, &was);
        gRecs.clear();
        for (long long t = gFrameAt; t < gFrameAt + 900 && found; ++t) {
            gNow = t;
            if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
            const size_t r0 = gRecs.size();
            vdoll::body::Frame(t);
            s.tick = t % 6 == 0;
            vdoll::body::Alive(s, t);
            vdoll::body::Aim(0, &now);
            if (now.shots > was.shots) { ++shots; if (t - lastShot < m63Every) ++fast; lastShot = t; }
            was = now;
            for (size_t i = r0; i < gRecs.size(); ++i) {
                const Rec& x = gRecs[i];
                if (x.slot == 1 && x.kind == 2 && x.r.size() >= 3 && x.r[0] == 0x13 && x.r[2] == 0x0D) ++m63Rec;
                if (x.slot == 2 && x.kind == 0 && x.r.size() >= 9 && x.r[0] == 0x04 && x.r[6] == 0x0D) {
                    ++hits;
                    const int dmg = x.r[2] | x.r[3] << 8;
                    if (dmg != 70 && dmg != 280 && dmg != 35) ++badDmg;
                }
            }
            if (gPlayers[2].life <= 0) break;
        }
        gFrameAt += 1500;
        gHave[2] = false;
#ifdef VDOLL_BODY_PICKUP
        vdoll::body::Pickup(0, &pi);
#endif
        Check(m63Rec > 0, "m63: its weapon record names the M63 (13 .. 0d ..): %d records", m63Rec);
        Check(shots > 0 && hits > 0 && badDmg == 0, "m63: %d shots, %d hits with the M63's code, the game's damage (70, 280, 35): %d other", shots, hits, badDmg);
        Check(fast == 0, "m63: no two shots under the M63's pace, %d frames, apart (%d)", m63Every, fast);
#ifdef VDOLL_BODY_PICKUP
        // (its magazine of 100 spent with him within 15 m it draws its M1911A1, as with its other guns: those shots are not the M63's)
        Check(pi.weapon == 0x0D && pi.rounds < 401 && 401 - pi.rounds <= shots && 401 - pi.rounds >= std::min(shots, 100), "m63: its rounds %d after %d shots (401 taken; the rest its handgun's)", pi.rounds, shots);
        // drop: its death with the M63
        {
            gRecs.clear();
            gNow = gFrameAt;
            vdoll::body::Died(s);
            const Rec* d = nullptr;
            for (const Rec& x : gRecs) if (x.kind == 3 && x.r.size() == 15) d = &x;
            const bool ok = d && U16(d->r, 0) == (1 << 10 | 0x100) && U16(d->r, 2) == (3 << 10 | 0x0D) && d->r[4] == 0 && U32(d->r, 11) == (unsigned)pi.rounds &&
                            std::fabs(S16(d->r, 5) * 10.f - pos[0]) < 20.f && std::fabs(S16(d->r, 9) * 10.f - pos[2]) < 20.f;
            Check(ok, "drop: its death drops the M63 where it fell, a record of its own (number %#x, %d rounds)", d ? U16(d->r, 0) : -1, d ? (int)U32(d->r, 11) : -1);
            vdoll::body::Down(0);
        }
#else
        Check(false, "m63: its rounds counted (not in this body)");
        Check(false, "drop: its death drops the M63 (not in this body)");
#endif
        Settle(120);
    } else { Check(false, "m63: no M63 point near a spawn point on this map"); }

    // goggles: in SNEAKING, the goggles taken; Snake (camouflaged: a soldier sees him within 10.5 m) 12 to 25 m away in its sight:
    // seen with the goggles on; then the goggles gone (its death's drop, the same life on) and Snake back at the same place: not seen
    if (gog) {
        gRule = 4;
        auto farSights = [&]() {                         // (the log: "sees player 4 at 15.0 m"; beyond 11 m)
            int n = 0;
            for (const std::string& l : gLines) {
                const size_t at = l.find("sees player 4 at ");
                float m = 0;
                if (at != std::string::npos && sscanf(l.c_str() + at + 17, "%f", &m) == 1 && m > 11.f) ++n;
            }
            return n;
        };
        int seenWithout = -1, seenWith = -1, worn = 0, seenOff = -1, wornOff = 0;
        bool placedOff = false;
        // (the goggles taken where Snake can then stand in its sight: its place and facing at the take decide; the next
        // point a doll took a special at when none, 4 tries)
        std::vector<const Case*> tries = {gog};
        for (const Case* c : took) if (c != gog && tries.size() < 4) tries.push_back(c);
        float pos[3] = {0, 0, 0};
        uint16_t facing = 0;
        bool tookIt = false, placed = false;
        for (const Case* c : tries) {
#ifdef VDOLL_HOST_DROPS
            gDrops.assign(1, Special(3, c->p->p));
#endif
            pos[0] = c->sp->p[0]; pos[1] = c->sp->p[1] + 800.f; pos[2] = c->sp->p[2];
            facing = (uint16_t)lrintf(std::atan2(c->p->p[0] - pos[0], c->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
            gRecs.clear(); gLines.clear();
            const Run r = RunDoll(pos, &facing, 1200, 0, true, 12);
            tookIt = r.claim >= 0;
            placed = tookIt && PlaceFoe(3, pos, facing, rSnake, 1, 8, 440);
            if (placed || !tookIt) break;
            Settle(120);
        }
        if (placed) {
            const float snake[3] = {gPlayers[3].pos[0], gPlayers[3].pos[1], gPlayers[3].pos[2]};
            const float withPos[3] = {pos[0], pos[1], pos[2]};
            const uint16_t withFacing = facing;
            gRecs.clear(); gLines.clear();
            RunDoll(pos, &facing, 180, 0, false);       // (with the goggles: they go on in SNEAKING, Snake seen)
            seenWith = farSights();
            for (const Rec& x : gRecs) if (x.slot == 1 && x.kind == 2 && x.r.size() >= 3 && x.r[0] == 0x13 && x.r[1] == 0x07) ++worn;
            gHave[3] = false;
            RunDoll(pos, &facing, 200, 0, false);       // (Snake gone: its fight over)
            // (v150-host beta 22, pickup2: review22b P9) MGO1_VDOLL_GOGGLES_CAMO=0, the goggles still on, back at its place and
            // facing of the run above (Snake where it saw him then): not seen
            setenv("MGO1_VDOLL_GOGGLES_CAMO", "0", 1);
            vdoll::body::Init(h);
            memcpy(pos, withPos, sizeof pos); facing = withFacing;
            memcpy(gPlayers[1].pos, pos, sizeof pos);
            vdoll::body::Pause(0); vdoll::body::Resume(0);
            gHave[3] = true; memcpy(gPlayers[3].pos, snake, sizeof snake);
            placedOff = true;
            gRecs.clear(); gLines.clear();
            RunDoll(pos, &facing, 180, 0, false);
            seenOff = farSights();
            for (const Rec& x : gRecs) if (x.slot == 1 && x.kind == 2 && x.r.size() >= 3 && x.r[0] == 0x13 && x.r[1] == 0x07) ++wornOff;
            gHave[3] = false;
            RunDoll(pos, &facing, 200, 0, false);
            setenv("MGO1_VDOLL_GOGGLES_CAMO", "1", 1);
            vdoll::body::Init(h);
#ifdef VDOLL_BODY_PICKUP
            { vdoll::body::Self s; s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send; s.life = gLife; vdoll::body::Died(s); }   // (the goggles gone)
#endif
            gHave[3] = true; memcpy(gPlayers[3].pos, snake, sizeof snake);
            // (the doll may have moved: Snake again where it sees him, 12 m or more away)
            if (!PlaceFoe(3, pos, facing, rSnake, 1, 8, 440)) gHave[3] = false;
            gRecs.clear(); gLines.clear();
            RunDoll(pos, &facing, 180, 0, false);
            seenWithout = farSights();
            gHave[3] = false;
        } else printf("  Snake not placed in its sight 11.5 to 25 m away from %.0f %.0f %.0f facing %d (the case %.0f %.0f %.0f)\n", pos[0], pos[1], pos[2], (int)facing, gog->p->p[0], gog->p->p[1], gog->p->p[2]);
        Check(tookIt, "goggles: the thermal goggles taken in SNEAKING");
        Check(worn > 0, "goggles: worn, item 7 in its weapon record (%d records)", worn);
        Check(seenWith > 0, "goggles: with them on, Snake camouflaged 11.5 to 25 m away seen (%d sights)", seenWith);
        Check(seenWithout == 0, "goggles: without them (the same life), Snake camouflaged there not seen (%d sights)", seenWithout);
        Check(placedOff && wornOff > 0 && seenOff == 0, "goggles: MGO1_VDOLL_GOGGLES_CAMO=0, the goggles on (%d records with item 7), Snake camouflaged %s not seen (%d sights)",
              wornOff, placedOff ? "11.5 to 25 m away" : "(not placed)", seenOff);
        gRule = 1;
        Settle(120);
    } else { Check(false, "goggles: no goggles' point near a start on this map"); }

#if defined(VDOLL_HOST_DROPS) && defined(VDOLL_BODY_PICKUP)
    // ---- (v150-host beta 22, review) four more parts: review22b's findings P1 to P4 ----
    // pile: the M63 and the goggles lying at one place (a dead player's drops, the records 0x900 and 0x901): each take's two
    // records, the claim and the record emptied, for both (a second take within 6 frames of the first lost the first one's
    // emptied record: the box then stayed on every player's screen for the rest of the round, bench pickup_t1_probe47 f10048)
    if (!took.empty()) {
        int cases = 0, claims = 0, emptied = 0, two = 0;
        for (const Case* c : took) {
            if (cases >= 4) break;
            ++cases;
            gDrops.clear();
            vdoll::DropNow a = Special(0, c->p->p), g = Special(3, c->p->p);
            a.number = 0x900; g.number = 0x901; a.count = 307;
            gDrops.push_back(a); gDrops.push_back(g);
            float pos[3] = {c->sp->p[0], c->sp->p[1] + 800.f, c->sp->p[2]};
            uint16_t facing = (uint16_t)lrintf(std::atan2(c->p->p[0] - pos[0], c->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
            gRecs.clear(); gLines.clear();
            RunDoll(pos, &facing, 1200);
            int n = 0;
            for (const Rec& x : gRecs) {
                if (x.kind != 3 || x.r.size() != 15 || x.r[4] != 0x01) continue;
                ++claims; ++n;
                for (const Rec& y : gRecs) if (y.kind == 3 && y.r.size() == 13 && y.r[4] == 0x03 && U16(y.r, 0) == U16(x.r, 0) && y.frame >= x.frame) { ++emptied; break; }
            }
            if (n == 2) ++two;
            Settle(120);
        }
        Check(two > 0 && emptied == claims, "pile: the M63 and the goggles at one place both taken (%d of %d runs), each claim followed by its record emptied: %d of %d",
              two, cases, emptied, claims);
    }
    // race: a player's take reaching the host right after the doll's look at the drops (it looks every 30 frames), the doll
    // 0.7 m from the box: no claim (a player's game sees the box gone as soon as the take comes; the doll claimed a box already
    // taken, two players then held one special)
    if (!took.empty()) {
        int tried = 0, claimed = 0;
        for (const Case* c : took) {
            if (tried >= 3) break;
            // (a place 0.7 m from the box on its floor, the box in its sight, out of the pick-up zone)
            float pos[3] = {0, 0, 0};
            bool found = false;
            for (int j = 0; j < 8 && !found; ++j) {
                const float yaw = 0.3927f + 0.7854f * (float)j;
                const float feet[3] = {c->p->p[0] + std::sin(yaw) * 700.f, c->p->p[1], c->p->p[2] + std::cos(yaw) * 700.f};
                const float eye[3] = {feet[0], feet[1] + 1550.f, feet[2]}, box[3] = {c->p->p[0], c->p->p[1] + 300.f, c->p->p[2]};
                float len = 0, end[3];
                if (vdoll::body::SightStopped(eye, box) || !WayTo(feet, c->p->p, &len, end) || len > 1500.f) continue;
                pos[0] = feet[0]; pos[1] = feet[1] + 800.f; pos[2] = feet[2]; found = true;
            }
            if (!found) continue;
            ++tried;
            gDrops.assign(1, Special(c->p->kind == 3 ? 3 : 0, c->p->p));
            gFrameAt += 100;                             // (its next frame looks at the drops: the take comes right after that look)
            gTakenAfterRead = gDrops[0].number;
            uint16_t facing = (uint16_t)lrintf(std::atan2(c->p->p[0] - pos[0], c->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
            gRecs.clear(); gLines.clear();
            const Run r = RunDoll(pos, &facing, 90);
            gTakenAfterRead = -1;
            if (r.claim >= 0) ++claimed;
            Settle(120);
        }
        Check(tried > 0 && claimed == 0, "race: a box taken by a player just after the doll's look, the doll 0.7 m from it: claimed %d times in %d runs", claimed, tried);
    }
    // spent: an M63 with 3 rounds taken (a dead player's), an enemy 8 to 14 m away, the game's weapons setting "primary only"
    // (no handgun to draw when a magazine is spent): its last round fired, its own primary (the AK-47) in its hands at once (it
    // reloaded the empty M63, again and again, for the rest of the fight)
    if (m63) {
        gWeaponRule = 4;
        long long lastM63 = -1, akAt = -1, againAt = -1;
        int m63Shots = 0, againRounds = -1, againWeapon = -1;
        bool placed = false;
        std::vector<const Case*> tries = {m63};
        for (const Case* c : took) if (c != m63 && tries.size() < 6) tries.push_back(c);
        for (const Case* c : tries) {
            lastM63 = akAt = -1; m63Shots = 0;           // (pickup2: the next place when its fight did not come: it ran on, out of his sight)
            gDrops.assign(1, Special(0, c->p->p));
            gDrops[0].number = 0x902; gDrops[0].count = 3;
            float pos[3] = {c->sp->p[0], c->sp->p[1] + 800.f, c->sp->p[2]};
            uint16_t facing = (uint16_t)lrintf(std::atan2(c->p->p[0] - pos[0], c->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
            gRecs.clear(); gLines.clear();
            const Run r = RunDoll(pos, &facing, 1200, 0, true, 12);
            placed = r.claim >= 0 && PlaceFoe(2, pos, facing, rM63, 1, 0, 200);
            if (!placed) { gHave[2] = false; Settle(120); continue; }
            vdoll::body::Self s;
            s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send; s.life = gLife;
            gRecs.clear();
            int roundsWas = 3;
            for (long long t = gFrameAt; t < gFrameAt + 900; ++t) {
                gNow = t;
                if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
                const size_t r0 = gRecs.size();
                vdoll::body::Frame(t);
                s.tick = t % 6 == 0;
                vdoll::body::Alive(s, t);
                vdoll::body::PickupInfo pi;
                vdoll::body::Pickup(0, &pi);
                if (pi.rounds < roundsWas) { m63Shots += roundsWas - pi.rounds; roundsWas = pi.rounds; if (pi.rounds == 0) lastM63 = t; }
                for (size_t i = r0; i < gRecs.size(); ++i) {
                    const Rec& x = gRecs[i];
                    if (lastM63 >= 0 && akAt < 0 && x.slot == 1 && x.kind == 2 && x.r.size() >= 3 && x.r[0] == 0x13 && x.r[2] == 0x0C) akAt = t;
                }
                if (akAt >= 0) break;
            }
            gFrameAt += 1500;
            gHave[2] = false;
            // again (v150-host beta 22, pickup2: review22b P10): its M63 spent, the enemy gone, another M63 (50 rounds, a dead
            // player's) at its feet: taken, with that box's rounds (the empty one blocked every other special to its death)
            if (akAt >= 0) {
                const float feet[3] = {pos[0], pos[1] - 800.f, pos[2]};
                vdoll::DropNow a = Special(0, feet);
                a.number = 0x903; a.count = 50;
                gDrops.push_back(a);
                for (long long t = gFrameAt; t < gFrameAt + 600 && againAt < 0; ++t) {
                    gNow = t;
                    if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
                    const size_t r0 = gRecs.size();
                    vdoll::body::Frame(t);
                    s.tick = t % 6 == 0;
                    vdoll::body::Alive(s, t);
                    for (size_t i = r0; i < gRecs.size(); ++i)
                        if (gRecs[i].kind == 3 && gRecs[i].r.size() == 15 && gRecs[i].r[4] == 0x01 && U16(gRecs[i].r, 0) == 0x903) againAt = t;
                }
                vdoll::body::PickupInfo pi;
                vdoll::body::Pickup(0, &pi);
                againRounds = pi.rounds; againWeapon = pi.weapon;
                gFrameAt += 1200;
            }
            Settle(120);
            if (akAt >= 0) break;
        }
        gWeaponRule = 0;
        Check(placed && m63Shots == 3 && lastM63 >= 0 && akAt >= 0 && akAt - lastM63 <= 60,
              "spent: its M63's 3 rounds fired (%d; primary weapons only), its AK-47 in its hands %lld frames after the last (60 at most)", m63Shots,
              lastM63 >= 0 && akAt >= 0 ? akAt - lastM63 : -1LL);
        Check(akAt >= 0 && againAt >= 0 && againWeapon == 0x0D && againRounds == 50, "again: its M63 spent, another M63 at its feet %s (its special %#x, %d rounds; 50 in the box)",
              akAt < 0 ? "(not reached: the first not spent)" : againAt >= 0 ? "taken" : "LEFT", againWeapon, againRounds);
    }
    // battery: the goggles' battery comes back no faster than the game lets a player's (US n003a 0x413C98: a point every
    // 720 x 5 time units at best, where the worn drain takes one every 60 x 5, measured 5 a second: a point every 2.4 s), so
    // from off (under 20) to on again (over 90) 168 s at least (it came back at 2 a second: 35 s)
    if (gog) {
        gRule = 4;                                       // (SNEAKING, a soldier: it wants them on all the time)
        gDrops.assign(1, Special(3, gog->p->p));
        float pos[3] = {gog->sp->p[0], gog->sp->p[1] + 800.f, gog->sp->p[2]};
        uint16_t facing = (uint16_t)lrintf(std::atan2(gog->p->p[0] - pos[0], gog->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 1200, 0, true, 12);
        long long offAt = -1, onAgain = -1;
        bool wasOn = false;
        if (r.claim >= 0) {
            vdoll::body::Self s;
            s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send; s.life = gLife;
            for (long long t = gFrameAt; t < gFrameAt + 16000 && onAgain < 0; ++t) {
                gNow = t;
                if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
                vdoll::body::Frame(t);
                s.tick = t % 6 == 0;
                vdoll::body::Alive(s, t);
                vdoll::body::PickupInfo pi;
                vdoll::body::Pickup(0, &pi);
                if (wasOn && !pi.gogglesOn && offAt < 0) offAt = t;
                if (!wasOn && pi.gogglesOn && offAt >= 0) onAgain = t;
                wasOn = pi.gogglesOn;
            }
            gFrameAt += 16600;
        }
        Check(r.claim >= 0 && offAt >= 0 && (onAgain < 0 || onAgain - offAt >= (long long)(168 * 59.94)),
              "battery: the goggles off (battery under 20), on again %.1f s later (168 s at least: the game's best rate, a point every 2.4 s)",
              offAt >= 0 && onAgain >= 0 ? (onAgain - offAt) / 59.94 : -1.0);
        gRule = 1;
        Settle(120);
    }
    // ---- (v150-host beta 22, pickup2) review22b's P6 and P7 ----
    // owed (P6): a take's emptied record 6 frames after its claim whatever comes then: knocked down at the claim (no Alive:
    // vdoll.cpp calls Owed each frame, Pause each tick), the round's end screens from the claim on (the room's phase 4: Alive
    // runs on), its leave 2 frames after the claim (Quit: at once). It waited while the doll was down (the box on every
    // player's screen meanwhile) and was lost at the round's end and at a leave
    if (!took.empty()) {
        const char* kMode[3] = {"knocked down at its claim", "the round over at its claim", "its leave 2 frames after its claim"};
        for (int mode = 0; mode < 3; ++mode) {
            long long claimAt = -1, emptiedAt = -1;
            for (const Case* c : took) {
                if (claimAt >= 0) break;
                gDrops.assign(1, Special(c->p->kind == 3 ? 3 : 0, c->p->p));
                float pos[3] = {c->sp->p[0], c->sp->p[1] + 800.f, c->sp->p[2]};
                uint16_t facing = (uint16_t)lrintf(std::atan2(c->p->p[0] - pos[0], c->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
                vdoll::body::Self s;
                s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send; s.life = ++gLife;
                gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200;
                gRecs.clear(); gLines.clear();
                bool gone = false;
                for (long long t = gFrameAt; t < gFrameAt + 1500; ++t) {
                    gNow = t;
                    if (t % 6 == 0) memcpy(gPlayers[1].pos, pos, sizeof gPlayers[1].pos);
                    const size_t r0 = gRecs.size();
                    vdoll::body::Frame(t);
                    s.tick = t % 6 == 0;
                    if (claimAt < 0 || mode == 1) vdoll::body::Alive(s, t);
                    else if (mode == 0) {                  // (down: as vdoll.cpp's ALIVE state with d.down)
#ifdef VDOLL_BODY_QUIT
                        vdoll::body::Owed(s, t);
#endif
                        if (s.tick) vdoll::body::Pause(0);
                    } else if (!gone && t >= claimAt + 2) {   // (its leave: as vdoll.cpp's Leave)
#ifdef VDOLL_BODY_QUIT
                        vdoll::body::Quit(s, true);
#endif
                        vdoll::body::Down(0);
                        gone = true;
                    }
                    for (size_t i = r0; i < gRecs.size(); ++i) {
                        const Rec& x = gRecs[i];
                        if (x.kind != 3 || x.r.size() < 5) continue;
                        if (x.r[4] == 0x01 && claimAt < 0) {
                            claimAt = t;
                            for (auto& d : gDrops) if (d.number == U16(x.r, 0)) { d.kind = 0; d.state = 3; d.box = false; }
                            if (mode == 1) gPhase = 4;     // (the round over from its claim on)
                        } else if (x.r[4] == 0x03 && emptiedAt < 0 && U16(x.r, 0) == gDrops[0].number) emptiedAt = t;
                    }
                    if (emptiedAt >= 0 || (claimAt >= 0 && t > claimAt + 900)) break;
                }
                gPhase = 2;
                gFrameAt += 2100;
                Settle(120);
            }
            const long long d = claimAt >= 0 && emptiedAt >= 0 ? emptiedAt - claimAt : -1, want = mode == 2 ? 2 : 6;
            Check(d == want, "owed: %s, its record emptied %lld frames after the claim (%lld)", kMode[mode], d, want);
        }
    }
    // quit (P7): its own leave holding the M63 and the goggles (both lying at one place, taken): each dropped where it stands,
    // a record of its own (state 0, its feet's place, the M63's rounds), as the game's pause menu QUIT drops them; the host's
    // kick holding them: nothing dropped. A doll that left dropped nothing
    if (!took.empty()) {
        int ownDrops = -1, ownWant = 0, kickDrops = -1, kickWant = 0;
        bool ownOk = false;
        for (int pass = 0; pass < 2; ++pass)
            for (const Case* c : took) {
                gDrops.clear();
                vdoll::DropNow a = Special(0, c->p->p), g = Special(3, c->p->p);
                a.number = 0x904 + 2 * pass; g.number = 0x905 + 2 * pass; a.count = 211;
                gDrops.push_back(a); gDrops.push_back(g);
                float pos[3] = {c->sp->p[0], c->sp->p[1] + 800.f, c->sp->p[2]};
                uint16_t facing = (uint16_t)lrintf(std::atan2(c->p->p[0] - pos[0], c->p->p[2] - pos[2]) / 6.2831853f * 65536.f);
                gRecs.clear(); gLines.clear();
                RunDoll(pos, &facing, 1200);
                vdoll::body::PickupInfo pi;
                vdoll::body::Pickup(0, &pi);
                const int want = (pi.weapon == 0x0D ? 1 : 0) + (pi.goggles ? 1 : 0);
                if (want == 0) { Settle(120); continue; }
                vdoll::body::Self s;
                s.k = 0; s.slot = 1; s.team = 0; s.pos = pos; s.facing = &facing; s.send = Send; s.life = gLife;
                gRecs.clear();
                gNow = gFrameAt;
#ifdef VDOLL_BODY_QUIT
                vdoll::body::Quit(s, pass == 0);
#endif
                vdoll::body::Down(0);
                int n = 0;
                bool ok = true;
                for (const Rec& x : gRecs) {
                    if (x.kind != 3 || x.r.size() != 15 || x.r[4] != 0x00) continue;
                    ++n;
                    const int num = U16(x.r, 0), kc = U16(x.r, 2);
                    const bool m63 = kc == (3 << 10 | 0x0D) && U32(x.r, 11) == (unsigned)pi.rounds, gog = kc == (1 << 10 | 0x07) && U32(x.r, 11) == 1;
                    if ((num >> 10) != 1 || (num & 0x3FF) < 0x100 || !(m63 || gog) || std::fabs(S16(x.r, 5) * 10.f - pos[0]) > 20.f || std::fabs(S16(x.r, 9) * 10.f - pos[2]) > 20.f) ok = false;
                }
                if (pass == 0) { ownDrops = n; ownWant = want; ownOk = ok; } else { kickDrops = n; kickWant = want; }
                Settle(120);
                break;
            }
        Check(ownWant > 0 && ownDrops == ownWant && ownOk, "quit: its own leave holding %d special items (of the M63 and the goggles): %d dropped where it stands, records of its own%s",
              ownWant, ownDrops, ownOk ? "" : " (a record wrong)");
        Check(kickWant > 0 && kickDrops == 0, "quit: the host's kick holding %d: nothing dropped (%d)", kickWant, kickDrops);
    }
#endif

#if defined(VDOLL_HOST_DROPS) && defined(VDOLL_BODY_HEAVY)
    // heavyoff (v150-host beta 22, review): MGO1_VDOLL_HEAVY=0, the RPG-7 and the flamethrower never in its hands: it leaves them
    // lying as without heavy's body (it took them and held them to its death, out of every player's reach, never fired)
    if (rpg) {
        setenv("MGO1_VDOLL_HEAVY", "0", 1);
        vdoll::body::Init(h);
        const int kind = rpg->p->kind == 1 || rpg->p->kind == 2 ? rpg->p->kind : 1;
        gDrops.assign(1, Special(kind, rpg->p->p));
        float pos[3] = {rpg->sp->p[0], rpg->sp->p[1] + 800.f, rpg->sp->p[2]};
        uint16_t facing = 0;
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 900);
        Check(r.claim < 0, "heavyoff: MGO1_VDOLL_HEAVY=0: the %s 6 to 14 m away left lying (never fired, so not taken)", kName[kind]);
        setenv("MGO1_VDOLL_HEAVY", "1", 1);
        vdoll::body::Init(h);
        Settle(120);
    }
#endif

    // the switch off: nothing taken
    if (m63) {
        setenv("MGO1_VDOLL_PICKUP", "0", 1);
        vdoll::body::Init(h);
#ifdef VDOLL_HOST_DROPS
        gDrops.assign(1, Special(0, m63->p->p));
#endif
        float pos[3] = {m63->sp->p[0], m63->sp->p[1] + 800.f, m63->sp->p[2]};
        uint16_t facing = 0;
        gRecs.clear(); gLines.clear();
        const Run r = RunDoll(pos, &facing, 900);
        Check(r.claim < 0, "MGO1_VDOLL_PICKUP=0: the M63 6 to 14 m away left lying (beta 21)");
    }
    printf("%s: %d checks passed, %d failed\n", gFails ? "FAILED" : "ALL PASSED", gPasses, gFails);
    return gFails ? 1 : 0;
}
