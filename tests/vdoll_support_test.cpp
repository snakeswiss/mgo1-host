// (v150-host beta 22, support) A virtual doll and the support row's claymore and book: MGO1Loader/vdoll_support.h, vdoll_body.cpp
// SupportFrame / SupportChoose / ClayBlast, docs/VIRTUAL_DOLL.md 35.
// The game (US n003a): a player takes a lying claymore with his own game when he lies on his front (state bit 3) with his hips
// within 500 mm of it on the ground and in height (0x3A8BD8), and his game sends the claymores' list's state 4 (every game removes
// its copy, no blast); the sensor (0x3A8740) goes off only for a player not lying. A layer's game sends the state 1 for a claymore
// or a book laid 0.7 m ahead of him; his game's own claymore wounds when it goes off (100, kind 0x1B, its reach 2.5 m).
// The test: a map's .geom; the test's game holds the claymores' list, its copy of the doll (lying 50 frames after its first {06 03},
// as the host's copy of a real client), and the sensor as the game's: a sphere of 750 mm, 750 mm ahead of the claymore and 375 mm
// up; a body point of the doll in it while its copy does not lie sets it off.
// Checks:
//   1 take: at each spawn point (8 at most) an enemy's claymore laid 7 to 9 m ahead in its sight (the layer dead, away): the doll
//     takes it within 30 s with the list's state 4 for its id, its hips within 500 mm of it and its copy lying at that moment;
//   2 never set off: no body point of the doll in the sensor while its copy does not lie, in 1 (and its style lying {06 03} or
//     crawling {06 43} from 1.6 m on);
//   3 none taken: a teammate's claymore, its own, an enemy's one behind it (not seen), and an enemy's with an enemy in its sight
//     ((v150-host beta 25, meshmargin) one it went for when its eye's line to that enemy had been stopped over 1.5 s is not counted:
//     the test's enemy never falls, and a doll that lost sight of him has no enemy in its sight);
//   4 lay: with MGO1_VDOLL_SUPPORT_ITEM=claymore (book) it lays its claymore (book) with the list's state 1 on the claymores' (books')
//     channel, 0.7 m ahead of where it stands, at a place of its side's hidden places (seen from over 15 m by one eye at most, the
//     enemies' way in seeing it from 12 m at most, 4 m from every spawn point); where its side has none within 35 m, it lays none
//     ((v150-host beta 25, meshmargin) a place within reach: a way to its stand 1.6 m off every claymore lying, as the body's);
//   5 the places: on each side (red, blue, the deathmatch) the hidden places found, each one's measures printed (how far it is seen
//     from: the farthest standing eye of the map that sees it, how many see it; the enemies' way in), their median against the median
//     of the enemies' way samples (the open places they walk);
//   6 the blast: the doll's claymore gone off (the host's copy) wounds an enemy 1.5 m from it in a clear line with the layer's records
//     ({14 slot 00 02 ..}, {04 slot 100 0 0 0x1B 0 0}, {09 slot 8 ..}), the doll dead or alive; none to an enemy 3 m away nor a teammate;
//   7 keep off: a teammate's claymore on its way (the bench's trap switch sends it there): it never comes within 1 m of it;
//   8 the switch: MGO1_VDOLL_SUPPORT=0, no record on the lists' channels in 1 and 4, and 7's claymore walked onto.
//   9 (v150-host beta 23, review) in the sensor's reach it never gets up: (a) an enemy in its sight, (b) a hit's flinch (body::Pause
//     40 frames, then body::Resume) while it crawls 1.3 m from the claymore: it crawls on and takes it, never sets it off.
//     (c) an enemy's book read on its crawl (8 s): its crawl's time limit counts its crawl only, it crawls on and takes it.
//   10 (v150-host beta 23, review) three of its own claymores lying at most, the host's list 9 frames late (VDOLL_TEST_LIST_DELAY).
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_support_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_support_test
//   ./vdoll_support_test <map.geom>      (VDOLL_TEST_VERBOSE=1: the body's lines; VDOLL_TEST_PARTS=1,4: some parts only)
#include "doll_geom.h"
#include "doll_nav.h"
#include "vdoll_body.h"
#include "vdoll_support.h"
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
static doll::Geom gG;
static doll::NavMesh gNav;                               // (the test's own mesh: its ways and lines for the measures)
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0;
static int gRuleNow = 1;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (getenv("VDOLL_TEST_VERBOSE")) printf("%s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (id < 0 || id >= 16 || !gHave[id]) return false; *s = gPlayers[id]; return true; }
static int Rule() { return gRuleNow; }
static unsigned long long RealMsNow() { return (unsigned long long)((double)gNow * 1000.0 / 59.94); }
static const float kPi = 3.14159265f;

// ---- the test's game: the claymores' list, the blasts of the host's copies, the traps for the bench's switch
struct TClay { uint32_t id; int slot; float pos[3]; float yaw; };
static std::vector<TClay> gClayList;
static std::vector<std::pair<uint32_t, std::array<float, 3>>> gBoomQ;
#ifdef VDOLL_HOST_CLAYS
static int Claymores(vdoll::ClayNow* out, int max) {
    int n = 0;
    for (const TClay& c : gClayList) { if (n >= max) break; out[n].id = c.id; out[n].slot = c.slot; memcpy(out[n].pos, c.pos, sizeof c.pos); out[n].yaw = c.yaw; out[n].word = vsup::kWord; ++n; }
    return n;
}
static int ClayBlasts(uint32_t id[], float at[][3], int max) {
    int n = 0;
    for (const auto& b : gBoomQ) { if (n >= max) break; id[n] = b.first; memcpy(at[n], b.second.data(), 12); ++n; }
    gBoomQ.clear();
    return n;
}
#endif
#ifdef VDOLL_HOST_BOOKS
// ((v150-host beta 23, review) part 9c: an enemy's book by the claymore, the host's book list as the body reads it)
struct TBook { uint32_t id; int slot; float pos[3]; };
static std::vector<TBook> gTBooks;
static int gTBookReads = 0;
static int Books(vdoll::BookNow* out, int max) {
    int n = 0;
    for (const TBook& b : gTBooks) { if (n >= max) break; out[n].id = b.id; out[n].slot = b.slot; memcpy(out[n].pos, b.pos, sizeof b.pos); out[n].readTime = 480; ++n; }
    return n;
}
static bool ReadBook(uint32_t id) { for (const TBook& b : gTBooks) if (b.id == id) { ++gTBookReads; return true; } return false; }
static bool BookDone(uint32_t id) {
    for (size_t i = 0; i < gTBooks.size(); ++i) if (gTBooks[i].id == id) { gTBooks.erase(gTBooks.begin() + (long)i); return true; }
    return false;
}
#endif
static std::vector<std::array<float, 3>> gTrapAt;        // (the bench's MGO1_VDOLL_TEST_TRAP: the traps' places)
static int Traps(float out[][3], int max) { int n = 0; for (const auto& t : gTrapAt) if (n < max) { memcpy(out[n], t.data(), 12); ++n; } return n; }
// ((v150-host beta 23, review) the host's game lists a doll's claymore some frames after the doll sends it (its datagram waits
// for the game's next read, 2 to 9 frames, then the list's act): part 10's delay, 0 elsewhere)
static int gListDelay = 0;
static std::vector<std::pair<long long, TClay>> gListPending;
static uint32_t gNextClay = 0x100;
static uint32_t AddClay(int slot, const float* p, float yaw) {
    TClay c; c.id = ((uint32_t)slot << 10) | (0x100 + (gNextClay++ & 0xFF)); c.slot = slot; memcpy(c.pos, p, sizeof c.pos); c.yaw = yaw;
    gClayList.push_back(c);
    return c.id;
}

// ---- what the doll sends
struct Sent { long long frame; int slot, kind; std::vector<uint8_t> r; };
static std::vector<Sent> gSent;
static int gStyle = 0;                                    // (its last {06} style)
static long long gLyingSince = -1;                        // (its copy: lying 50 frames after its first {06 03} / {06 43})
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) {
    gSent.push_back({gNow, slot, kind, r});
    if (kind == 1 && r.size() == 2 && r[0] == 0x06) gStyle = r[1];
    if (kind == 4 && r.size() == 20) {                    // (the test's game: a take removes the claymore, a claymore laid is listed)
        uint32_t id; int st; float p[3];
        if (vsup::ReadRecord(r, &id, &st, p)) {
            if (st == vsup::kStateTaken) gClayList.erase(std::remove_if(gClayList.begin(), gClayList.end(), [&](const TClay& c) { return c.id == id; }), gClayList.end());
            if (st == vsup::kStateLying) {
                TClay c; c.id = id; c.slot = (int)(id >> 10); memcpy(c.pos, p, sizeof p); c.yaw = 0;
                if (gListDelay > 0) gListPending.push_back({gNow + gListDelay, c}); else gClayList.push_back(c);
            }
        }
    }
}
static float gPos[3];
static uint16_t gFacing = 0;
static vdoll::body::Self gS;
// the copy's state: lying (9) 50 frames after the doll's first lying style, standing (3) at once when it sends another
static void CopyFrame() {
    const bool lyingStyle = (gStyle & 0x0F) == 3;
    if (lyingStyle && gLyingSince < 0) gLyingSince = gNow;
    if (!lyingStyle) gLyingSince = -1;
    gPlayers[1].state = gLyingSince >= 0 && gNow - gLyingSince >= 50 ? 0x9u : 0x3u;
    memcpy(gPlayers[1].pos, gPos, sizeof gPos);
}
static void Step(bool alive = true) {
    ++gNow;
    for (size_t i = 0; i < gListPending.size();) {         // (the delayed list: part 10)
        if (gNow >= gListPending[i].first) { gClayList.push_back(gListPending[i].second); gListPending.erase(gListPending.begin() + (long)i); } else ++i;
    }
    CopyFrame();
    vdoll::body::Frame(gNow);
    gS.tick = gNow % 6 == 0;
    if (alive) vdoll::body::Alive(gS, gNow);
#ifdef VDOLL_BODY_SUPPORT
    vdoll::body::Laid(gS);
#endif
}
static bool InsideFace(const doll::Face& f, float x, float z) {
    const int n = (f.v[3][0] == f.v[0][0] && f.v[3][1] == f.v[0][1] && f.v[3][2] == f.v[0][2]) ? 3 : 4;
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float xi = f.v[i][0], zi = f.v[i][2], xj = f.v[j][0], zj = f.v[j][2];
        if (((zi > z) != (zj > z)) && (x < (xj - xi) * (z - zi) / (zj - zi) + xi)) in = !in;
    }
    return in;
}
static float Floor(float x, float z, float near) {
    float best = -1e30f;
    for (const doll::Face& f : gG.faces) {
        if (f.n[1] < 0.3f || !InsideFace(f, x, z)) continue;
        const float y = f.v[0][1] - (f.n[0] * (x - f.v[0][0]) + f.n[2] * (z - f.v[0][2])) / f.n[1];
        if (y > near + 450.f || y < near - 450.f || y <= best) continue;
        best = y;
    }
    return best;
}
static float FacingRad() { return gFacing * 2.f * kPi / 65536.f; }
// a point on the floor d mm along the doll's facing (side: radians off it), on the test's mesh, walkable from the doll's feet
static bool Ahead(float d, float side, float* out) {
    const float a = FacingRad() + side;
    const float p[3] = {gPos[0] + std::sin(a) * d, gPos[1] - 800.f, gPos[2] + std::cos(a) * d};
    float m[3];
    if (!gNav.Nearest(p, 400.f, m) || std::hypot(m[0] - p[0], m[2] - p[2]) > 400.f) return false;
    const float f[3] = {gPos[0], gPos[1] - 800.f, gPos[2]};
    if (!gNav.Walkable(f, m)) return false;
    memcpy(out, m, sizeof m);
    return true;
}
static void Player(int id, int team, bool alive, const float* feet) {
    gHave[id] = true;
    gPlayers[id] = vdoll::PlayerNow(); gPlayers[id].team = team; gPlayers[id].object = true; gPlayers[id].alive = alive; gPlayers[id].life = alive ? 200 : 0;
    gPlayers[id].cls = 0; gPlayers[id].state = 3;
    gPlayers[id].pos[0] = feet[0]; gPlayers[id].pos[1] = feet[1] + 800.f; gPlayers[id].pos[2] = feet[2];
}
// the game's sensor (0x3A9408: 750 mm ahead along the claymore's facing, 375 mm up, a sphere of 750): the doll's body points in it
static bool InSensor(const TClay& c) {
    const float o[3] = {c.pos[0] + std::sin(c.yaw) * 750.f, c.pos[1] + 375.f, c.pos[2] + std::cos(c.yaw) * 750.f};
    const bool lying = (gStyle & 0x0F) == 3;
    const float hips = gPos[1];
    const float pts[3][3] = {{gPos[0], hips + (lying ? -200.f : 350.f), gPos[2]}, {gPos[0], hips + (lying ? 40.f : 850.f), gPos[2]}, {gPos[0], hips - (lying ? 250.f : 700.f), gPos[2]}};
    for (const auto& p : pts) if (std::sqrt((p[0] - o[0]) * (p[0] - o[0]) + (p[1] - o[1]) * (p[1] - o[1]) + (p[2] - o[2]) * (p[2] - o[2])) < 750.f) return true;
    return false;
}
static int Records(int kind, int state, long long from, uint32_t* id = nullptr) {
    int n = 0;
    for (const Sent& x : gSent)
        if (x.frame >= from && x.kind == kind && x.r.size() == 20 && x.r[2] == state) { ++n; if (id) *id = ((uint32_t)x.r[1] << 10) | (0x100u + x.r[0]); }
    return n;
}
static bool Want(int part) {
    const char* p = getenv("VDOLL_TEST_PARTS");
    if (!p || !*p) return true;
    char b[8]; snprintf(b, sizeof b, "%d", part);
    for (const char* q = p; *q; ++q) if (strncmp(q, b, strlen(b)) == 0 && (q == p || q[-1] == ',') && (q[strlen(b)] == 0 || q[strlen(b)] == ',')) return true;
    return false;
}
static void Place(const doll::Spawn* sp, int si) {
    gPos[0] = sp->p[0]; gPos[1] = sp->p[1] + 800.f; gPos[2] = sp->p[2];
    gFacing = (uint16_t)(si * 9000);
    gS.life = gNow;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_support_test <map.geom>\n"); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); gGeom.resize(ftell(fp)); fseek(fp, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), fp) != gGeom.size()) return 1;
    fclose(fp);
    std::string err, log;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), gG, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    doll::NavConfig cfg;
#ifdef VDOLL_BODY_STAIRS
    // (v150-host beta 24, snakehouse) the test's mesh as the bodies' own: the doorways out of the walking (MGO1_VDOLL_STAIRS), the
    // fences no player crosses left out (MGO1_VDOLL_VAULT); its ways' stairs made into climbs below
    auto on = [](const char* v) { const char* e = getenv(v); return !(e && !strcmp(e, "0")); };
    cfg.doorways = on("MGO1_VDOLL_STAIRS");
#endif
#ifdef DOLL_NAV_MARGIN
    cfg.margin = on("MGO1_VDOLL_MESH_MARGIN");               // ((v150-host beta 25, meshmargin) the walls' margin as the bodies')
#endif
    if (!gNav.Build(gG, cfg, log)) { printf("no mesh: %s\n", log.c_str()); return 1; }
#ifdef VDOLL_BODY_STAIRS
    if (on("MGO1_VDOLL_VAULT")) vdoll::body::VaultsLeftOut(gNav);
#endif
    std::vector<const doll::Spawn*> spawns;
    for (const doll::Spawn& sp : gG.spawns) if (sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_FIRST_RED) spawns.push_back(&sp);
    if (spawns.empty()) { printf("no red spawn point in the geom\n"); return 1; }
    const bool off = getenv("MGO1_VDOLL_SUPPORT") && !strcmp(getenv("MGO1_VDOLL_SUPPORT"), "0");
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 0);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    if (!getenv("MGO1_VDOLL_SUPPORT_ITEM")) setenv("MGO1_VDOLL_SUPPORT_ITEM", "off", 1);   // (1 to 3: no item of its own; 4 sets it)
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.realMs = RealMsNow; h.traps = Traps;
#ifdef VDOLL_HOST_CLAYS
    h.claymores = Claymores; h.clayBlasts = ClayBlasts;
#ifdef VDOLL_HOST_BOOKS
    h.books = Books; h.readBook = ReadBook; h.bookDone = BookDone;   // (part 9c's book; none lies elsewhere)
#endif
    const bool hostClays = true;
#else
    const bool hostClays = false;
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    gNow = 600;
    printf("%s: the host's claymores' list %s, MGO1_VDOLL_SUPPORT %s\n", argv[1], hostClays ? "given" : "NOT in this body's Host (beta 22)", off ? "0" : "on");
    gS.k = 0; gS.slot = 1; gS.team = 0; gS.pos = gPos; gS.facing = &gFacing; gS.send = Send;
    float far[3] = {spawns[0]->p[0], spawns[0]->p[1], spawns[0]->p[2]};   // (the layers' place: dead, out of play)
    for (const doll::Spawn& sp : gG.spawns) if (sp.kind == doll::SP_TDM_BLUE && std::hypot(sp.p[0] - far[0], sp.p[2] - far[2]) > 10000.f) { memcpy(far, sp.p, sizeof far); break; }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };

    // ---- 1, 2: the take; 3: none taken
    int spots = 0, taken1 = 0, zone1 = 0, lyingAt1 = 0, setOff2 = 0, styled2 = 0, tried3 = 0, none3 = 0;
    for (size_t si = 0; si < spawns.size() && si < 8 && (Want(1) || Want(2) || Want(3)); ++si) {
        gClayList.clear(); gSent.clear(); gTrapAt.clear();
        for (int i = 2; i < 16; ++i) gHave[i] = false;
        Player(1, 0, true, spawns[si]->p);
        Player(3, 1, false, far);                          // (the layer: an enemy, dead now)
        Player(2, 0, false, far);                          // (a teammate, dead now)
        Place(spawns[si], (int)si);
        for (int i = 0; i < 120; ++i) Step();              // (2 s on its spawn's ways)
        float cp[3];
        bool okp = false;
        for (float d : {8000.f, 7000.f, 9000.f}) if (Ahead(d, 0.f, cp)) { okp = true; break; }
        const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]};
        const float top[3] = {cp[0], cp[1] + 150.f, cp[2]};
        if (!okp || vdoll::body::SightStopped(eye, top)) { printf("  spawn %zu: no floor 7 to 9 m ahead in its sight, not counted\n", si); continue; }
        ++spots;
        const float yaw = std::atan2(gPos[0] - cp[0], gPos[2] - cp[2]);   // (facing the doll: its sensor toward it)
        const long long from = gNow;
        const float startD = std::hypot(gPos[0] - cp[0], gPos[2] - cp[2]);
        const uint32_t id = AddClay(3, cp, yaw);
        TClay clay = gClayList.back();
        bool took = false, inZone = false, lying = false, setOff = false, styleOk = true;
        float hipsAt = 0;
        for (int i = 0; i < 1800 && !took; ++i) {
            Step();
            const bool in = InSensor(clay);
            if (in && (gPlayers[1].state & 8u) == 0) setOff = true;
            if (std::hypot(gPos[0] - cp[0], gPos[2] - cp[2]) < 1600.f && (gStyle & 0x0F) != 3) styleOk = false;
            uint32_t tid = 0;
            if (Records(4, vsup::kStateTaken, from, &tid) && tid == id) {
                took = true; hipsAt = std::hypot(gPos[0] - cp[0], gPos[2] - cp[2]);
                inZone = vsup::InTakeZone(gPos, cp); lying = (gPlayers[1].state & 8u) != 0;
            }
        }
        if (took) ++taken1;
        if (took && inZone) ++zone1;
        if (took && lying) ++lyingAt1;
        if (setOff) ++setOff2;
        if (styleOk) ++styled2;
        printf("  spawn %zu: an enemy's claymore %.1f m ahead %s%s; its hips %.0f mm from it, its copy %s; the sensor %s; lying or crawling within 1.6 m: %s\n", si,
               startD / 1000.f, took ? "TAKEN" : "NOT taken", took ? "" : "", hipsAt, lying ? "lying" : "not lying",
               setOff ? "SET OFF (a body point in it, its copy not lying)" : "never asked of a standing body", styleOk ? "yes" : "NO");
        for (int i = 0; i < 120; ++i) Step();              // (up again)
        // 3: none taken: a teammate's, its own, an enemy's behind it, an enemy's with an enemy in sight
        ++tried3;
        int none = 0;
        for (int k = 0; k < 4; ++k) {
            gClayList.clear();
            float p[3];
            const bool behind = k == 2;
            if (!Ahead(6000.f, behind ? kPi : 0.f, p) && !Ahead(5000.f, behind ? kPi : 0.f, p)) { ++none; continue; }
            const int slot = k == 0 ? 2 : k == 1 ? 1 : 3;
            if (k == 3) { float e[3]; if (Ahead(12000.f, 0.3f, e) || Ahead(10000.f, -0.3f, e)) Player(4, 1, true, e); else { ++none; continue; } }
            const long long f0 = gNow;
            const uint32_t x = AddClay(slot, p, 0.f);
            bool t = false;
            // (behind it, 6 m away, out of its field: not seen in the next 0.5 s, so not known; later on its ways it may see it, and
            // then take it, as it should: the claymore removed after the half second)
            int known = 0;
            // ((v150-host beta 25, meshmargin) the enemy's case: the frames since the doll's eye last had a clear line to his head, when
            // it went for the claymore (its support state 1). The test's enemy never falls (his life stays 200): a doll that loses
            // sight of him (the body drops a target unseen 90 frames, vdoll_body.cpp Perceive) has no enemy in its sight: taken then,
            // it is not counted. n003a with the margin on, spawn point 7: 37 hits, then its eye's line to him stopped 109 frames when
            // it went (the body's `lost sight of player 5`); the switch at 0 met that spawn point's case at another place (its earlier
            // ways differ) and kept him in its line, never going)
            long long lastLine = 0, wentAt = -1, unseenThen = 0; int targetThen = -2;   // ((review M3) the body's own target when it went: -2 not read)
            for (int i = 0; i < (behind ? 30 : 900); ++i) {
                Step(); uint32_t tid = 0; if (Records(4, vsup::kStateTaken, f0, &tid) && tid == x) t = true;
                if (k != 3) continue;
                const float eyeNow[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, head[3] = {gPlayers[4].pos[0], gPlayers[4].pos[1] + 750.f, gPlayers[4].pos[2]};
                if (!vdoll::body::SightStopped(eyeNow, head)) lastLine = i;
#ifdef VDOLL_BODY_SUPPORT
                vdoll::body::SupportInfo sn;
                if (wentAt < 0 && vdoll::body::Support(0, &sn) && sn.state == 1) {
                    wentAt = i; unseenThen = i - lastLine;
                    vdoll::body::AimInfo ai; if (vdoll::body::Aim(0, &ai)) targetThen = ai.target;   // ((review M3) the body's own state)
                }
#endif
            }
#ifdef VDOLL_BODY_SUPPORT
            { vdoll::body::SupportInfo si3; if (vdoll::body::Support(0, &si3)) known = si3.known; }
#endif
            if (behind && known) t = true;
            // ((v150-host beta 25, meshreview M3) not counted only when the BODY itself had dropped him: its own target when it went for
            // the claymore was not this enemy (player 4), and its eye's line to him had been stopped over 1.5 s; a doll that went for a
            // claymore while it still held him as its target is counted, whatever the test's own line says)
            if (k == 3 && t && wentAt >= 0 && unseenThen > 90 && targetThen != -2 && targetThen != 4) {
                printf("    taken in part 3 after the enemy left its sight: its eye's line to him stopped for %.1f s when it went for the claymore, its target then %d\n", unseenThen / 60.f, targetThen);
                t = false;
            }
            if (!t) ++none;
            else printf("    TAKEN in part 3 (%s)\n", k == 0 ? "a teammate's" : k == 1 ? "its own" : k == 2 ? "behind it: SEEN" : "an enemy in its sight");
            if (k == 3 && getenv("VDOLL_TEST_VERBOSE")) printf("    (the enemy's case: went for the claymore at frame %lld, its eye's line to him stopped %lld frames then)\n", wentAt, unseenThen);
            gHave[4] = false;
        }
        if (none == 4) ++none3;
        gClayList.clear();
    }
    if (Want(1) || Want(2) || Want(3)) {
        printf("spawn points %d: taken %d, its hips in the game's zone %d, its copy lying %d; the sensor set off %d; lying or crawling within 1.6 m %d; none taken where it must not %d of %d\n",
               spots, taken1, zone1, lyingAt1, setOff2, styled2, none3, tried3);
        if (off) check(spots > 0 && taken1 == 0 && Records(4, vsup::kStateTaken, 0) == 0, "8 the switch at 0: no claymore taken, no record on the claymores' channel");
        else {
            check(spots > 0 && taken1 == spots && zone1 == taken1 && lyingAt1 == taken1, "1 an enemy's claymore it saw taken by crawling onto it, with the list's state 4, its hips in the game's zone, its copy lying");
            check(spots > 0 && setOff2 == 0 && styled2 == spots, "2 never set off: no body point in the sensor while its copy does not lie; lying or crawling within 1.6 m");
            check(tried3 > 0 && none3 == tried3, "3 none taken: a teammate's, its own, one it did not see, one with an enemy in its sight");
        }
    }

    // ---- 5: the places of each side (measured: how far each is seen from)
    std::vector<std::array<float, 3>> red, blue, dm;     // (the body's own sides: team deathmatch, and the deathmatch's points)
    vsup::SideSpawns(gG, 1, 0, red); vsup::SideSpawns(gG, 1, 1, blue); vsup::SideSpawns(gG, 0, 0, dm);
    vsup::Eyes eyes; vsup::MakeEyes(gNav, eyes);
    vsup::Cover cover; vsup::MakeCover(gG, cover);
    auto clear = [](const float* a, const float* b) { return gNav.Clear(a, b); };
    // (the bodies' own way rule: their search plans the enemies' ways so; tests/vdoll_support_test before it had more places)
#if defined(VDOLL_BODY_STAIRS)
    static const bool stairsOn = on("MGO1_VDOLL_STAIRS");
    auto way = [](const float* a, const float* b, std::vector<doll::Waypoint>& w) {
        float len = 0;
        if (!gNav.Path(a, b, w, &len)) return false;
        if (stairsOn) vdoll::body::StairsAsRun(gNav, w);
        return vdoll::body::WayRunnable(w);
    };
#elif defined(VDOLL_BODY_SUPPORT)
    auto way = [](const float* a, const float* b, std::vector<doll::Waypoint>& w) { float len = 0; return gNav.Path(a, b, w, &len) && vdoll::body::WayRunnable(w); };
#else
    auto way = [](const float* a, const float* b, std::vector<doll::Waypoint>& w) { float len = 0; return gNav.Path(a, b, w, &len); };
#endif
    int sidesWith = 0, placesAll = 0, badPlaces = 0;
    double medPlaces = 0, medWays = 0;
    if (Want(5) || Want(4)) {
        printf("  the map: %zu standing eyes (one an 8 m cell), %zu trunks (the slot 4 volumes), %zu foliage faces 2 m tall at most\n", eyes.p.size(), cover.trunks.size(), cover.leaves.size());
        const char* names[3] = {"red", "blue", "deathmatch"};
        for (int side = 0; side < 3; ++side) {
            vsup::Places P;
            if (side == 2) { if (dm.empty()) continue; vsup::StartPlaces(P, {}, dm, true); }
            else vsup::StartPlaces(P, side ? blue : red, side ? red : blue, false);
            const auto t0 = std::chrono::steady_clock::now();
            while (!vsup::StepPlaces(P, gNav, eyes, cover, 1000, way, clear)) {}
            const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            std::vector<float> wf, pf;
            for (const auto& w : P.enemy) for (size_t i = 0; i < w.size(); i += 10) { const float t[3] = {w[i].p[0], w[i].p[1] + 150.f, w[i].p[2]}; wf.push_back(vsup::SeenFrom(eyes, t, clear).far); }
            for (const auto& p : P.found) { pf.push_back(p.seen.far); if (p.seen.farBy > 1 || p.seen.far > 25000.f || p.approach > 12000.f) ++badPlaces; }
            std::sort(wf.begin(), wf.end()); std::sort(pf.begin(), pf.end());
            const float mw = wf.empty() ? 0.f : wf[wf.size() / 2], mp = pf.empty() ? 0.f : pf[pf.size() / 2];
            printf("  side %s: %zu ways, %d candidates measured, %zu places in %ld ms; seen from (the farthest eye): places median %.1f m, max %.1f m; the enemies' way samples median %.1f m (%zu)\n",
                   names[side], P.enemy.size(), P.tried, P.found.size(), ms, mp / 1000.f, pf.empty() ? 0.f : pf.back() / 1000.f, mw / 1000.f, wf.size());
            printf("    candidates refused by: link %d, spawn %d, trap %d, ends %d, open %d/%d, stand %d, its team's way %d, seen from far %d, the way in %d\n", P.why[0] + P.why[5], P.why[2], P.why[3], P.why[4],
                   P.why[6], P.why[7], P.why[8] + P.why[9], P.why[10], P.why[11], P.why[12]);
            int shown = 0;
            for (const auto& p : P.found) {
                if (shown++ >= 6 && !getenv("VDOLL_TEST_VERBOSE")) break;
                printf("    place %.0f %.0f %.0f: seen from %.1f m (by %d of %d eyes, %d over 15 m), the enemies' way in %.1f m; %s%s%s%s walls %d/16; enemy ways %d, its team's %d\n", p.at[0], p.at[1], p.at[2],
                       p.seen.far / 1000.f, p.seen.by, p.seen.of, p.seen.farBy, p.approach / 1000.f, p.corner ? "corner " : "", p.narrow ? "narrow " : "", p.trunk ? "trunk " : "", p.grass ? "foliage " : "", p.walls,
                       p.enemyPass, p.friendPass);
            }
            if (!P.found.empty()) { ++sidesWith; placesAll += (int)P.found.size(); medPlaces += mp; medWays += mw; }
        }
        if (Want(5)) {
            printf("sides with places %d; places %d; out of the rule (seen from over 15 m by more than one eye, or the way in over 12 m) %d; the places' median %.1f m against the ways' %.1f m\n", sidesWith, placesAll,
                   badPlaces, sidesWith ? medPlaces / sidesWith / 1000.f : 0.f, sidesWith ? medWays / sidesWith / 1000.f : 0.f);
            check(badPlaces == 0 && (sidesWith == 0 || medPlaces < medWays || medPlaces / sidesWith <= 10000.0),
                  "5 every hidden place out of the long sight lines; the places seen from nearer than the enemies' ways are (or from 10 m at most on a close map)");
        }
    }

    // ---- 4: lay (a claymore, then a book), on the first side with hidden places: red, blue (team deathmatch), else the deathmatch's
    int layRule = 1, layTeam = 0;
    std::vector<std::array<float, 3>> layOwn = red, layFoe = blue;
    {
        auto count = [&](const std::vector<std::array<float, 3>>& o, const std::vector<std::array<float, 3>>& f, bool dmSide) {
            vsup::Places P; vsup::StartPlaces(P, o, f, dmSide);
            while (!vsup::StepPlaces(P, gNav, eyes, cover, 1000, way, clear)) {}
            return P.found.size();
        };
        if (!red.empty() && count(red, blue, false) > 0) {}
        else if (!blue.empty() && count(blue, red, false) > 0) { layTeam = 1; layOwn = blue; layFoe = red; }
        else if (!dm.empty() && count(dm, dm, true) > 0) { layRule = 0; layOwn = dm; layFoe = dm; }
        printf("the lay's side: %s\n", layRule == 0 ? "the deathmatch's" : layTeam ? "blue (team deathmatch)" : "red (team deathmatch)");
    }
    for (int item = 1; item <= 2 && Want(4); ++item) {
        setenv("MGO1_VDOLL_SUPPORT_ITEM", item == 1 ? "claymore" : "book", 1);
        gRuleNow = layRule;
        vdoll::body::Init(h);
        gS.team = layTeam;
        gClayList.clear(); gSent.clear(); gTrapAt.clear();
        for (int i = 2; i < 16; ++i) gHave[i] = false;
        int laid = 0, atPlace = 0, ahead = 0, lives = 0;
        vsup::Places P; vsup::StartPlaces(P, layOwn, layFoe, layRule == 0);
        while (!vsup::StepPlaces(P, gNav, eyes, cover, 1000, way, clear)) {}
        const bool sideHas = !P.found.empty();
        for (size_t si = 0; si < layOwn.size() && si < 4; ++si) {
            const size_t pick = si * std::max<size_t>(1, layOwn.size() / 4);   // (spread over the side's points)
            const float* sp = layOwn[std::min(pick, layOwn.size() - 1)].data();
            Player(1, layTeam, true, sp);
            gPos[0] = sp[0]; gPos[1] = sp[1] + 800.f; gPos[2] = sp[2];
            gFacing = (uint16_t)(si * 9000);
            gS.life = gNow;
            ++lives;
            const float born[3] = {gPos[0], gPos[1], gPos[2]};   // (where its life began: the places within its reach from there)
            const long long from = gNow + 1;
            float standAt[3] = {0, 0, 0};
            bool did = false;
            for (int i = 0; i < 90 * 60 && !did; ++i) {
                Step();
#ifdef VDOLL_BODY_SUPPORT
                vdoll::body::SupportInfo si2;
                if (vdoll::body::Support(0, &si2) && si2.state == 6) memcpy(standAt, gPos, sizeof standAt);
#endif
                uint32_t lid = 0;
                if (Records(item == 1 ? 4 : 5, vsup::kStateLying, from, &lid)) {
                    did = true;
                    const Sent* x = nullptr;
                    for (const Sent& y : gSent) if (y.frame >= from && y.kind == (item == 1 ? 4 : 5) && y.r.size() == 20 && y.r[2] == 1) x = &y;
                    uint32_t id; int st; float p[3];
                    vsup::ReadRecord(x->r, &id, &st, p);
                    const float d = std::hypot(p[0] - standAt[0], p[2] - standAt[2]);
                    if (std::fabs(d - vsup::kLayAhead) < 150.f) ++ahead;
                    // its place among its side's measured places, within 1.5 m
                    bool found = false;
                    const vsup::Place* best = nullptr; float bd = 1e30f;
                    for (const auto& q : P.found) { const float e = std::hypot(q.at[0] - p[0], q.at[2] - p[2]); if (e < bd) { bd = e; best = &q; } }
                    if (best && bd < 1500.f) found = true;
                    if (found) ++atPlace;
                    printf("  life %zu: its %s laid at %.0f %.0f %.0f, %.2f m ahead of where it stood (id %#x); %s\n", si, item == 1 ? "claymore" : "book", p[0], p[1], p[2], d / 1000.f, id,
                           found ? "at a hidden place of its side" : "NOT at a hidden place");
                    if (found) {
                        const float top[3] = {p[0], p[1] + 150.f, p[2]};
                        const vsup::Seen sn = vsup::SeenFrom(eyes, top, clear);
                        printf("    seen from %.1f m at most (by %d of %d eyes within 40 m, %d over 15 m); the enemies' way in %.1f m\n", sn.far / 1000.f, sn.by, sn.of, sn.farBy, best->approach / 1000.f);
                    }
                }
            }
            if (did) ++laid;
            else {
                int within = 0;
                for (const auto& q : P.found) {             // (a place with a claymore lying within 4 m of it is not one: taken)
                    bool lying = false;
                    for (const TClay& c : gClayList) if (std::hypot(c.pos[0] - q.at[0], c.pos[2] - q.at[2]) < 4000.f) lying = true;
                    // ((v150-host beta 25, meshmargin) and the way 1.6 m off every claymore lying, as the body keeps its ways off those
                    // it knows, its own too (vdoll_body.cpp AvoidOk). n003a with the margin: its life 0 laid at 24890 620 21490, 4138 mm
                    // from the place 21009 2610 22926 (not taken by the 4 m rule; 3689 mm at 0, where it lies at 24310 870 21280), and
                    // every way to that place's stand passes 80 to 1587 mm from it: the body refused them, this count took the place)
                    std::vector<doll::Waypoint> w; float len = 0; float f[3] = {born[0], born[1] - 800.f, born[2]}, a[3], b[3];
                    bool way = gNav.Nearest(f, 600.f, a) && gNav.Nearest(q.stand, 600.f, b) && gNav.Path(a, b, w, &len) && w.size() >= 2 &&
#ifdef VDOLL_BODY_SUPPORT
                               vdoll::body::WayRunnable(w) &&
#endif
                               std::hypot(w.back().p[0] - q.stand[0], w.back().p[2] - q.stand[2]) < 500.f;
                    for (const TClay& c : gClayList)
                        for (size_t k = 1; way && k < w.size(); ++k) {
                            const float* u0 = w[k - 1].p; const float* u1 = w[k].p;
                            const float ux = u1[0] - u0[0], uz = u1[2] - u0[2], l2 = ux * ux + uz * uz;
                            const float tt = l2 > 1.f ? std::max(0.f, std::min(1.f, ((c.pos[0] - u0[0]) * ux + (c.pos[2] - u0[2]) * uz) / l2)) : 0.f;
                            if (std::hypot(u0[0] + ux * tt - c.pos[0], u0[2] + uz * tt - c.pos[2]) < 1600.f && std::fabs(u0[1] + (u1[1] - u0[1]) * tt - c.pos[1]) < 1500.f) way = false;
                        }
                    if (!lying && way && std::hypot(q.at[0] - born[0], q.at[2] - born[2]) < 35000.f) ++within;
                }
                printf("  life %zu: no %s laid in 90 s (%d hidden places of its side within 35 m of its spawn, a way to each)\n", si, item == 1 ? "claymore" : "book", within);
                if (within == 0) --lives;              // (none within reach: kept, as it must)
            }
            vdoll::body::Down(0);
            gS.life = -1;
        }
        printf("lives %d with a place within reach: laid %d, at a hidden place %d, 0.7 m ahead %d%s\n", lives, laid, atPlace, ahead, sideHas ? "" : " (no side with a hidden place on this map: not counted)");
        if (off) check(laid == 0, item == 1 ? "8 the switch at 0: no claymore laid" : "8 the switch at 0: no book laid");
        else check(!sideHas || (laid > 0 && atPlace == laid && ahead == laid && laid >= lives - 1),
                   item == 1 ? "4 its claymore laid with the list's state 1, 0.7 m ahead, at a hidden place of its side" : "4 its book laid with the books' list's state 1, 0.7 m ahead, at a hidden place");
    }
    gRuleNow = 1; gS.team = 0;
    setenv("MGO1_VDOLL_SUPPORT_ITEM", "off", 1);
    vdoll::body::Init(h);

    // ---- 6: its claymore's blast (the doll dead: Laid alone)
    if (Want(6) && hostClays && !off) {
        gClayList.clear(); gSent.clear();
        for (int i = 2; i < 16; ++i) gHave[i] = false;
        Player(1, 0, true, spawns[0]->p);
        Place(spawns[0], 0);
        for (int i = 0; i < 30; ++i) Step();
        float cp[3], e1[3], e2[3], m1[3];
        bool ok = Ahead(4000.f, 0.f, cp);
        if (ok) {
            const float yaw = FacingRad();
            auto at = [&](float d, float side, float* o) { const float p[3] = {cp[0] + std::sin(yaw + side) * d, cp[1], cp[2] + std::cos(yaw + side) * d}; float m[3];
                                                          if (!gNav.Nearest(p, 300.f, m)) return false; memcpy(o, m, sizeof m); return gNav.Walkable(cp, m); };
            ok = at(1500.f, 0.f, e1) && at(1000.f, -1.2f, m1);
            // (the far one: its body's point over the blast's reach in 3D once on the mesh: 3 m, else 3.5 m, else 4 m)
            bool farOk = false;
            for (float d : {3000.f, 3500.f, 4000.f}) {
                if (!at(d, 1.2f, e2)) continue;
                const float dx = e2[0] - cp[0], dy = e2[1] + 800.f + 230.f - (cp[1] + 200.f), dz = e2[2] - cp[2];
                if (std::sqrt(dx * dx + dy * dy + dz * dz) > 2700.f) { farOk = true; break; }
            }
            ok = ok && farOk;
        }
        int hit1 = 0, hit2 = 0, hitM = 0, rec14 = 0, rec09 = 0;
        if (ok) {
            Player(5, 1, true, e1); Player(6, 1, true, e2); Player(7, 0, true, m1);
            const uint32_t id = AddClay(1, cp, 0.f);
            vdoll::body::Down(0);                          // (dead: its claymore still wounds, as a dead layer's game's)
            for (int i = 0; i < 6; ++i) Step(false);
            gBoomQ.push_back({id, {cp[0], cp[1], cp[2]}});
            const long long f0 = gNow;
            for (int i = 0; i < 12; ++i) Step(false);
            for (const Sent& x : gSent) {
                if (x.frame < f0) continue;
                if (x.kind == 0 && x.r.size() == 9 && x.r[0] == 0x04 && x.r[1] == 1 && x.r[2] == 100 && x.r[3] == 0 && x.r[6] == 0x1B) { if (x.slot == 5) ++hit1; if (x.slot == 6) ++hit2; if (x.slot == 7) ++hitM; }
                if (x.kind == 2 && x.r.size() == 17 && x.r[0] == 0x14 && x.r[1] == 1 && x.r[2] == 0 && x.r[3] == 2 && x.slot == 5) ++rec14;
                if (x.kind == 1 && x.r.size() == 5 && x.r[0] == 0x09 && x.r[1] == 1 && x.r[2] == 8 && x.slot == 5) ++rec09;
            }
            printf("  its claymore gone off (the doll dead): the enemy at 1.5 m %d damage records (100, 0x1B), %d effect, %d knock-down; the enemy at 3 m %d; the teammate at 1 m %d\n", hit1, rec14, rec09, hit2, hitM);
        } else printf("  no floor for the blast's players: not counted\n");
        check(!ok || (hit1 == 1 && rec14 == 1 && rec09 == 1 && hit2 == 0 && hitM == 0), "6 its claymore's blast: the layer's three records to an enemy 1.5 m away, the doll dead; none at 3 m, none to a teammate");
        for (int i = 5; i < 8; ++i) gHave[i] = false;
    }

    // ---- 7: keep off a teammate's claymore on its way (the bench's trap switch: its way planned to 2.5 m short of it)
    if (Want(7) && hostClays) {
        setenv("MGO1_VDOLL_TEST_TRAP", "1", 1);
        int tried = 0, kept = 0;
        for (size_t si = 0; si < spawns.size() && si < 4; ++si) {
            gClayList.clear(); gSent.clear(); gTrapAt.clear();
            for (int i = 2; i < 16; ++i) gHave[i] = false;
            Player(1, 0, true, spawns[si]->p); Player(2, 0, false, far);
            Place(spawns[si], (int)si);
            for (int i = 0; i < 60; ++i) Step();
            float p[3];
            if (!Ahead(6000.f, 0.f, p) && !Ahead(5000.f, 0.f, p)) continue;
            ++tried;
            AddClay(2, p, std::atan2(gPos[0] - p[0], gPos[2] - p[2]));   // (its sensor toward the doll)
            const TClay tc = gClayList.back();
            gTrapAt.push_back({p[0], p[1], p[2]});
            float nearest = 1e30f;
            bool inSensor = false;
            for (int i = 0; i < 1500; ++i) { Step(); nearest = std::min(nearest, std::hypot(gPos[0] - p[0], gPos[2] - p[2])); if (InSensor(tc)) inSensor = true; }
            if (nearest >= 1500.f && !inSensor) ++kept;
            printf("  spawn %zu: a teammate's claymore 6 m ahead, its ways sent near it: it came %.2f m from it at the nearest%s\n", si, nearest / 1000.f,
                   inSensor ? ", a body point IN ITS SENSOR" : ", never in its sensor");
        }
        unsetenv("MGO1_VDOLL_TEST_TRAP");
        if (off) check(tried > 0 && kept < tried, "8 the switch at 0: a teammate's claymore walked onto (beta 22)");
        else check(tried > 0 && kept == tried, "7 a teammate's claymore kept off: never within 1.5 m of it, never in its sensor");
    }

    // ---- 9 (v150-host beta 23, review) in the claymore's sensor's reach (1.6 m) it never gets up while the claymore lies: a player
    // who gets up there sets it off with his own game (sup_p1b). (a) an enemy comes into its sight, (b) a hit's flinch (vdoll.cpp:
    // body::Pause each tick for 40 frames, then body::Resume, its life lowered) while it crawls 1.3 m from it: it crawls on and takes
    // it, no body point in the sensor while its copy does not lie. The bench F22_specials (f17127 to f17172): a doll got up 1.2 m from
    // the claymore it crawled to ("an enemy in its sight"), its copy set it off 42 frames later and it died by it
    if (Want(9) && hostClays && !off) {
#ifdef VDOLL_BODY_SUPPORT
        for (int sub = 0; sub < 3; ++sub) {
#ifndef VDOLL_HOST_BOOKS
            if (sub == 2) break;
#endif
            int tried = 0, took9 = 0, setOff9 = 0, events = 0;
            for (size_t si = 0; si < spawns.size() && si < 8; ++si) {
                gClayList.clear(); gSent.clear(); gTrapAt.clear();
                for (int i = 2; i < 16; ++i) gHave[i] = false;
                Player(1, 0, true, spawns[si]->p); Player(3, 1, false, far); Player(2, 0, false, far);
                Place(spawns[si], (int)si);
                gS.hp = 200; gS.attacker = -1;
                for (int i = 0; i < (sub == 2 ? 480 : 120); ++i) Step();   // (9c: its book cooldown over, 450 frames after its spawn)
                float cp[3];
                bool okp = false;
                for (float d : {8000.f, 7000.f, 9000.f}) if (Ahead(d, 0.f, cp)) { okp = true; break; }
                const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]};
                const float top[3] = {cp[0], cp[1] + 150.f, cp[2]};
                bool bad = !okp || vdoll::body::SightStopped(eye, top) || std::fabs(cp[1] - (gPos[1] - 800.f)) > 1500.f;   // (on its floor)
                // ((v150-host beta 25, fallflinch) 9c: its place after its 480 frames' roam is where its ways took it (a drop's fall of a
                // player's length since MGO1_VDOLL_HIT_FALL put it elsewhere on n033a: no spawn point of 8 left a floor 7 to 9 m straight
                // ahead in sight); 29 degrees to each side are tried too, still in its field)
                for (float side : {0.5f, -0.5f})
                    for (float d : {8000.f, 7000.f, 9000.f}) {
                        if (sub != 2 || !bad || !Ahead(d, side, cp)) continue;
                        const float t2[3] = {cp[0], cp[1] + 150.f, cp[2]};
                        bad = vdoll::body::SightStopped(eye, t2) || std::fabs(cp[1] - (gPos[1] - 800.f)) > 1500.f;
                    }
                if (bad) continue;
                ++tried;
                const long long from = gNow;
                const uint32_t id = AddClay(3, cp, std::atan2(gPos[0] - cp[0], gPos[2] - cp[2]));
                const TClay clay = gClayList.back();
#ifdef VDOLL_HOST_BOOKS
                const int readsWas = gTBookReads;
                gTBooks.clear();
#endif
                float lay[3] = {0, 0, 0};
                bool laid = false, done = false, took = false, setOff = false, chose = false, upAfter = false;
                for (int i = 0; i < 1800 && !took; ++i) {
                    vdoll::body::SupportInfo u;
                    vdoll::body::Support(0, &u);
                    if (u.state >= 1 && u.state <= 4) chose = true;
                    if (done && u.state == 4) upAfter = true;           // (got up after the event, before the take)
                    if (u.state == 2 && !laid) { laid = true; lay[0] = gPos[0]; lay[1] = gPos[1] - 800.f; lay[2] = gPos[2]; }
#ifdef VDOLL_HOST_BOOKS
                    // (9c: once it crawls, an enemy's book 1.6 m ahead of it on its crawl: read at once, its 8 s on the way, then the crawl's end)
                    if (sub == 2 && u.state == 3 && gTBooks.empty() && gTBookReads == readsWas) {
                        const float dx = cp[0] - gPos[0], dz = cp[2] - gPos[2], l = std::max(1.f, std::hypot(dx, dz));
                        if (l > 2000.f) gTBooks.push_back({(3u << 10) | (0x200u + (uint32_t)si), 3, {gPos[0] + dx / l * 1600.f, cp[1], gPos[2] + dz / l * 1600.f}});
                    }
                    if (sub == 2 && !done && gTBookReads > readsWas) { done = true; ++events; }
#endif
                    if (sub < 2 && !done && u.state == 3 && std::hypot(gPos[0] - cp[0], gPos[2] - cp[2]) < 1300.f) {
                        done = true; ++events;
                        if (sub == 0) Player(4, 1, true, lay);       // (an enemy standing where it lay down: in its sight, 1.2 m behind it)
                        else {                                       // (a hit: its life lowered, then the flinch's pause as vdoll.cpp runs it)
                            gS.hp = 150; gS.attacker = 3;
                            Step();
                            for (int f = 0; f < 40; ++f) {
                                ++gNow; CopyFrame(); vdoll::body::Frame(gNow);
                                if (gNow % 6 == 0) vdoll::body::Pause(0);
                                if (InSensor(clay) && (gPlayers[1].state & 8u) == 0) setOff = true;
                            }
                            vdoll::body::Resume(0);
                        }
                    }
                    Step();
                    if (InSensor(clay) && (gPlayers[1].state & 8u) == 0) setOff = true;
                    uint32_t tid = 0;
                    if (Records(4, vsup::kStateTaken, from, &tid) && tid == id) took = true;
                }
                if (!chose || (sub == 2 && !done)) {         // (no way the body runs to a place 2.5 m short of it, as part 1's spots: a ledge;
                    // 9c: the book not read, its floor on a slope off the claymore's height: out of the 1.5 m)
                    printf("  spawn %zu: %s, not counted\n", si, !chose ? "the claymore not gone for (no way of the body's to a place 2.5 m short of it)" : "the book not read on its crawl");
                    --tried;
                    if (done) --events;
                } else {
                    if (took && !upAfter) ++took9;
                    if (setOff) ++setOff9;
                }
                if (chose) printf("  spawn %zu (%s): %s, %s\n", si, sub == 0 ? "an enemy in its sight 1.3 m from the claymore" : sub == 1 ? "a hit's flinch 1.3 m from the claymore" :
                       "an enemy's book read on its crawl (8 s)",
                       took ? (upAfter ? "taken, but only after it got up and lay down again" : "taken in that crawl") : "NOT taken",
                       setOff ? "SET OFF (a body point in its sensor, its copy not lying)" : "never set off");
                for (int i = 0; i < 120; ++i) Step();
                gHave[4] = false;
                gS.hp = 200; gS.attacker = -1;
#ifdef VDOLL_HOST_BOOKS
                gTBooks.clear();
#endif
            }
            printf("spawn points %d, the event %d: taken in that crawl %d, set off %d\n", tried, events, took9, setOff9);
            check(tried > 0 && events == tried && took9 == tried && setOff9 == 0,
                  sub == 0 ? "9a an enemy in its sight while it crawls in the sensor's reach: it crawls on and takes it, never sets it off"
                  : sub == 1 ? "9b a hit's flinch while it crawls in the sensor's reach: it stays lying, crawls on and takes it, never sets it off"
                             : "9c an enemy's book read on its crawl (8 s): it crawls on after it and takes the claymore in that crawl, never sets it off");
        }
#endif
    }

    // ---- 10 (v150-host beta 23, review) three of its own claymores lying at most (docs/VIRTUAL_DOLL.md 35), the host's game listing
    // each 9 frames after the doll sends it (a read's wait and the list's act; the benches: 4 to 6 frames to the next list line): with 4
    // claymores to lay (MGO1_VDOLL_SUPPORT_CLAYS=4) on its side's places, never 4 of its own lying
    if (Want(10) && hostClays && !off) {
        setenv("MGO1_VDOLL_SUPPORT_ITEM", "claymore", 1);
        setenv("MGO1_VDOLL_SUPPORT_CLAYS", "4", 1);
        gRuleNow = layRule;
        vdoll::body::Init(h);
        gS.team = layTeam;
        gClayList.clear(); gSent.clear(); gTrapAt.clear(); gListPending.clear();
        for (int i = 2; i < 16; ++i) gHave[i] = false;
        gListDelay = getenv("VDOLL_TEST_LIST_DELAY") ? atoi(getenv("VDOLL_TEST_LIST_DELAY")) : 9;
        int most = 0, laidAll = 0;
        for (size_t si = 0; si < layOwn.size() && si < 2; ++si) {
            const float* sp = layOwn[std::min(si * std::max<size_t>(1, layOwn.size() / 2), layOwn.size() - 1)].data();
            Player(1, layTeam, true, sp);
            gPos[0] = sp[0]; gPos[1] = sp[1] + 800.f; gPos[2] = sp[2];
            gFacing = (uint16_t)(si * 9000);
            gS.life = gNow;
            for (int i = 0; i < 300 * 60; ++i) {
                Step();
                int own = 0;
                for (const TClay& c : gClayList) if (c.slot == gS.slot) ++own;
                most = std::max(most, own);
            }
            vdoll::body::Down(0);
            gS.life = -1;
        }
        laidAll = Records(4, vsup::kStateLying, 0);
        const int late = gListDelay;
        gListDelay = 0;
        printf("its claymores laid %d, of its own lying at most %d at once (the list %d frames late)\n", laidAll, most, late);
        check(most <= 3, "10 three of its own claymores lying at most, the host's list a read late");
        unsetenv("MGO1_VDOLL_SUPPORT_CLAYS");
        setenv("MGO1_VDOLL_SUPPORT_ITEM", "off", 1);
        gRuleNow = 1; gS.team = 0;
        vdoll::body::Init(h);
    }
    return fails ? 1 : 0;
}
