// (v150-host beta 22, book) A virtual doll and a book (the support row's girlie magazine, the weapon 0x1C): MGO1Loader/vdoll_book.h,
// vdoll_body.cpp BookFrame, docs/VIRTUAL_DOLL.md 30.
// A player's own game makes him read another player's book (US n003a: the local player's action, its check 0x443C18): the first
// book of the game's list not laid by himself whose place is within 1.5 m of his hips, in a clear line from his head to 0.2 m over
// it, when his cooldown is over (450 VBlanks after his spawn and after each reading); he stops, turns to it and reads it, his
// stick moves him no more, while the book lies (the read time his game started on it: 480 passes, 8 s, on the benches); then the
// game takes the book away. Raikov (class 4) reads none. Measured on real clients (benches book_r1, book_r2, book_r4).
// The test: a map's .geom; one doll at each of the map's spawn points (8 at most), its own ways (no enemy) for 8 s, then a book of
// player 3 on the floor 0.7 m ahead of it (in its sight: the body's own line test), and once it reads an enemy (player 2) 6 m
// ahead; the test's game keeps a book that was read 480 frames, then takes it away. Then out of reach (1.6 m on the floor), its
// own book (player 1's), a book during its cooldown, a Raikov doll, a doll in a state the check refuses.
// (v150-host beta 25, booktest) Each book is laid when the doll stands on its floor out of a drop's, a climb's or a fence's hold
// (none begun in the last 60 frames: the body reads no book in them, as before), and the enemy stands on the floor 6 m ahead
// within 2.5 m of the doll's (a slope), or there is none at that spawn point (its checks 2 and 3 then without him). Since the
// stairs of blocks (cf5eebb) and the fences no player crosses (9b219fa) the dolls' 8 s end on other places: on n022a's hills the
// floor 6 m ahead was 0.70 to 1.92 m off the doll's at all 8 spawn points (none tested: 6 FAIL), on n012a check 9's book met a
// drop down the catwalk's blocks (not tried: 1 FAIL); n001a had 2 spawn points of 8 tested. Now 7 or 8 on each of the 12 maps.
// Checks:
//   1 it reads the book (the host's read time started on it: Host::readBook) within 12 frames (0.2 s), at every spawn point;
//   2 while the book lies after that: it moves 30 mm at most in all, fires no shot though an enemy stands 6 m ahead, faces the
//     book within 20 degrees from 1 s on;
//   3 the book taken away: it moves on (500 mm or more within 3 s), and the enemy it did not fire at is fought (a shot within 5 s:
//     a spawn where none comes is not counted for 2's "no shot", the enemy was not in its sight);
//   4 it reads none: a book 1.6 m away on the floor (out of reach, in the 2 frames after it is laid), a book of its own, a book
//     1 s after a reading's end (its cooldown), as Raikov, in a refused state; and it reads one again after its cooldown (8 s
//     after the end);
//   5 a reading lasts the read time (480 passes) at a player's own 60 passes a second by the machine's clock, 2 frames' slack:
//     VDOLL_TEST_SLOW=1.5 (the host's frames 1.5 times longer, its own game's count slower) the same 8.0 s.
//   6 (v150-host beta 22, review) wearing the thermal goggles (taken where they lay; SNEAKING, a soldier: on at once) it reads
//     none: the check refuses a player whose item in use is the goggles (+0x614's 7) with their effect on (state bit 0x150, set
//     with the goggles' frame 0x450D40 at 0x450F8C). 656e1d8's body read it.
//   7 (v150-host beta 22, bookplay) its reading told as a reader's game tells it (vdoll_book.h; docs/VIRTUAL_DOLL.md 34), at every
//     spawn point: at its first record tick {0E 1, its hips / 10, the book's place / 10} on its kind 1 channel, then {0E 1} at
//     each tick while it reads; its state record's byte 2 (body::StateEvent) at each tick from its first record on; no style
//     {06}, no look {17}, no facing {01} but one {01 0 0 0 0 0 0}, its place {00} every 3rd tick from the one after the long
//     record (none with it: a reader's game's, about every 20 frames); at its end no {0E} more, the byte 0 at the next tick, its
//     style and facing back within 2 ticks. A copy as the game's code runs it (the check 0x443C18's copy path, the act
//     0x443718, the records' readers 0x442FF8 and 0x405E48, modelled below: the copy reads from a {0E 1} on, its byte set to 2,
//     and ends at the first state record whose byte is not 2) fed with those records: it reads from the long record's tick,
//     to within 2 ticks of the doll's end, never ends in between, never reads again in the 2 s after, and its turn's point
//     (+0xE50: the long record's book, which a look record would overwrite) stays the book's place. MGO1_VDOLL_BOOK_POSE=0 (the
//     copy stands, beta 22's first build) and 12d949b's body: 7 fails (no {0E}, the byte 0: the copy never reads).
//   8 (v150-host beta 22, bookplay) a teammate's book is read as an enemy's (the game's check asks nothing of teams nor of the
//     friendly fire setting), and in deathmatch any other player's.
//   10 (v150-host beta 22, bookplay2) the class rule: its copy's class (+0xB62, the host's) 0 KGB, 1 GRU, 2 Ocelot Unit, 3
//     Sokolov, 5 Ocelot, 6 Reiko, 7 Rowdy, 8 Snake read a book 0.5 m ahead; 4 Raikov none (the check's first test, the only
//     class it asks; measured on real clients, reports/vdoll_bookplay.md 6).
//   9 (v150-host beta 22, bookplay) a hit's flinch (vdoll.cpp: body::Pause, as the damage action of priority 0x29 ends a
//     reader's book action of 0x13 in his game) and its death (body::Down) end a reading: the byte 0 at the next tick, no {0E}
//     more, its cooldown then (no reading of a book laid from 5 s after the flinch to 0.2 s before its cooldown's end).
// On beta 21's body (no Host::books: VDOLL_HOST_BOOKS absent) and with MGO1_VDOLL_BOOKS=0, 1 fails at every spawn point.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_book_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_book_test
//   ./vdoll_book_test <map.geom>        (VDOLL_TEST_VERBOSE=1: the body's lines)
#include "doll_geom.h"
#include "doll_nav.h"
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
static doll::Geom gG;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0;
// (VDOLL_TEST_SLOW=k) a slowed host: its frames k times a 59.94th of a second on the machine's clock (Host::realMs); a player's own
// game counts the read time at 60 passes a second whatever the host's pace
static float gSlow = 1.f;
static unsigned long long RealMsNow() { return (unsigned long long)((double)gNow * 1000.0 / 59.94 * gSlow); }
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
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int gRuleNow = 1;                                  // (v150-host beta 22, review: check 6 in SNEAKING)
static int Rule() { return gRuleNow; }
static const float kPi = 3.14159265f;
static float WrapD(float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; }

// ---- the test's game: its book list (the host's NewOnlineBookMan as the body reads it) and the read time
struct TBook { uint32_t id; int slot; float pos[3]; long long readAt; };
static std::vector<TBook> gBooks;
static uint32_t gNextId = 0x100;
static const long long kReadFrames = 480;                // (the read time a reading starts: 480 passes, the benches' list)
static int gReads = 0;                                    // (Host::readBook calls that found the book)
static uint32_t gLastRead = 0;
static uint32_t AddBook(int slot, const float* p) {
    TBook b; b.id = ((uint32_t)slot << 10) | (gNextId++ & 0x3FF); b.slot = slot; memcpy(b.pos, p, sizeof b.pos); b.readAt = -1;
    gBooks.push_back(b);
    return b.id;
}
static void GameFrame() {                                  // a read book leaves the list when its read time is over
    for (size_t i = 0; i < gBooks.size();)
        if (gBooks[i].readAt >= 0 && gNow - gBooks[i].readAt >= kReadFrames) gBooks.erase(gBooks.begin() + (long)i); else ++i;
}
#ifdef VDOLL_HOST_BOOKS
static int Books(vdoll::BookNow* out, int max) {
    int n = 0;
    for (const TBook& b : gBooks) { if (n >= max) break; out[n].id = b.id; out[n].slot = b.slot; memcpy(out[n].pos, b.pos, sizeof b.pos); out[n].readTime = (int)kReadFrames; ++n; }
    return n;
}
static int gDone = 0;
static bool BookDone(uint32_t id) {                      // (the doll's time over by the clock: the test's game takes the book away at once)
    for (size_t i = 0; i < gBooks.size(); ++i) if (gBooks[i].id == id) { gBooks.erase(gBooks.begin() + (long)i); ++gDone; return true; }
    return false;
}
static bool ReadBook(uint32_t id) {
    for (TBook& b : gBooks) if (b.id == id) { if (b.readAt < 0) b.readAt = gNow; ++gReads; gLastRead = id; return true; }
    return false;
}
#endif

#ifdef VDOLL_HOST_DROPS
// (v150-host beta 22, review: check 6) the drops' records of the test's game: the thermal goggles where they lie (record 0x54)
static std::vector<vdoll::DropNow> gDrops;
static int Drops(vdoll::DropNow* out, int max) { int n = 0; for (const auto& d : gDrops) if (n < max) out[n++] = d; return n; }
static void Send3(void*, int, int kind, const std::vector<uint8_t>& r) {   // (its claim: the record taken, the box gone)
    if (kind == 3 && r.size() == 15 && r[4] == 1) for (auto& d : gDrops) if (d.number == (r[0] | r[1] << 8)) { d.kind = 0; d.state = 3; d.box = false; }
}
#endif
// the floor under (x, z) near y from the .geom's faces (-1e30: none)
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
// (v150-host beta 25, booktest) the floor under (x, z) nearest the height near, within win of it (-1e30: none): the enemy's place
// on a slope (n022a's hills: the floor 6 m ahead of the doll 0.70 to 1.92 m off its own at every spawn point since 9b219fa)
static float FloorNear(float x, float z, float near, float win) {
    float best = -1e30f, off = win;
    for (const doll::Face& f : gG.faces) {
        if (f.n[1] < 0.3f || !InsideFace(f, x, z)) continue;
        const float y = f.v[0][1] - (f.n[0] * (x - f.v[0][0]) + f.n[2] * (z - f.v[0][2])) / f.n[1];
        if (std::fabs(y - near) <= off) { off = std::fabs(y - near); best = y; }
    }
    return best;
}

static float gPos[3];
static uint16_t gFacing = 0;
static vdoll::body::Self gS;
static long long gShotsAt = -1;
// (v150-host beta 22, bookplay) its records, each with its frame and channel kind; the state record's byte at each tick (asked
// before the body's frame, as vdoll.cpp's StateTick goes before its BodyTick)
struct TRec { long long f; int kind; std::vector<uint8_t> r; };
static std::vector<TRec> gRecs;
struct TState { long long f; int event; };
static std::vector<TState> gStates;
static void Send(void*, int, int kind, const std::vector<uint8_t>& r) { gRecs.push_back({gNow, kind, r}); }
static int StateEventNow() {
#ifdef VDOLL_BODY_BOOK_POSE
    return vdoll::body::StateEvent(0);
#else
    return 0;
#endif
}
static float FacingDeg() { return gFacing * 360.f / 65536.f; }
// (v150-host beta 25, booktest) its drops, climbs and fences crossed so far (the body's Status counts: each counted as it begins)
static int LinkCount() {
    const std::string st = vdoll::body::Status(0);
    int n = 0;
    for (const char* w : {" drops,", " climbs,", " fences crossed"}) {
        const size_t at = st.find(w);
        if (at == std::string::npos) continue;
        size_t b = at;
        while (b > 0 && st[b - 1] >= '0' && st[b - 1] <= '9') --b;
        n += atoi(st.c_str() + b);
    }
    return n;
}
static bool gWatchLinks = false;                          // (Step follows its counts while on)
static int gLinks = 0;
static long long gLinkAt = 0;                             // (the frame its last drop, climb or fence began, or the watch began)
static void WatchLinks() { gWatchLinks = true; gLinks = LinkCount(); gLinkAt = gNow; }
static void Step() {
    ++gNow;
    GameFrame();
    memcpy(gPlayers[1].pos, gPos, sizeof gPos);
    vdoll::body::Frame(gNow);
    gS.tick = gNow % 6 == 0;
    if (gS.tick) gStates.push_back({gNow, StateEventNow()});
    vdoll::body::Alive(gS, gNow);
    if (gWatchLinks) { const int c = LinkCount(); if (c != gLinks) { gLinks = c; gLinkAt = gNow; } }
}
// ---- (v150-host beta 22, bookplay) a copy of the doll on another game, as the game's code runs it (US n003a): the {0E} record's
// reader 0x442FF8 sets +0xDE9 (its second byte) and, the long one, +0xE00 / +0xE50 (his place, the book's, x 10); the look
// record's 0x449778 sets +0xE50 too; the state record's 0x405E48 sets +0xFB4 (its third byte); the check 0x443C18 (copy path)
// starts the reading when +0xDE9 is set and sets +0xFB4 to 2; the act 0x443718 (copy path) ends it when +0xFB4 is not 2; the
// end 0x443A08 clears +0xDE9
struct Copy {
    bool reading = false; int de9 = 0, fb4 = 0, starts = 0, ends = 0; long long from = -1, to = -1; float target[3] = {0, 0, 0};
    float worst = 0.f; bool haveTarget = false;
};
static int16_t Le16s(const uint8_t* p) { return (int16_t)(p[0] | p[1] << 8); }
// the records of frames [f0, f1) in order, each tick's state record first; the book's place b; the copy's frames (its check and
// act each frame after the records)
static void RunCopy(Copy& c, long long f0, long long f1, const float* b) {
    size_t ri = 0, si = 0;
    for (long long f = f0; f < f1; ++f) {
        while (si < gStates.size() && gStates[si].f < f) ++si;
        if (si < gStates.size() && gStates[si].f == f) c.fb4 = gStates[si].event;
        while (ri < gRecs.size() && gRecs[ri].f < f) ++ri;
        for (; ri < gRecs.size() && gRecs[ri].f == f; ++ri) {
            const std::vector<uint8_t>& r = gRecs[ri].r;
            if (gRecs[ri].kind == 1 && r.size() >= 2 && r[0] == 0x0E) {
                c.de9 = r[1];
                if (r.size() >= 14) { for (int k = 0; k < 3; ++k) c.target[k] = Le16s(&r[8 + 2 * k]) * 10.f; c.haveTarget = true; }
            }
            if (gRecs[ri].kind == 2 && r.size() >= 7 && r[0] == 0x17) { for (int k = 0; k < 3; ++k) c.target[k] = Le16s(&r[1 + 2 * k]) * 10.f; c.haveTarget = true; }
        }
        if (!c.reading && c.de9) { c.reading = true; c.fb4 = 2; ++c.starts; if (c.from < 0) c.from = f; }
        else if (c.reading && c.fb4 != 2) { c.reading = false; c.de9 = 0; ++c.ends; c.to = f; }
        if (c.reading && c.haveTarget) c.worst = std::max(c.worst, std::hypot(c.target[0] - b[0], c.target[2] - b[2]));
        if (c.reading && !c.haveTarget) c.worst = 1e9f;
    }
}
static int Shots() { vdoll::body::AimInfo a; vdoll::body::Aim(0, &a); return a.shots; }
// a point on the floor d mm ahead of the doll along its facing (side: degrees off it); false: no floor there
static bool Ahead(float d, float side, float* out) {
    const float a = (FacingDeg() + side) * kPi / 180.f;
    out[0] = gPos[0] + std::sin(a) * d; out[2] = gPos[2] + std::cos(a) * d;
    out[1] = Floor(out[0], out[2], gPos[1] - 800.f);
    return out[1] > -1e29f;
}
// (v150-host beta 25, booktest) the enemy's place d mm ahead: the floor within 0.45 m of the doll's as Ahead, else the one nearest
// its own within 2.5 m (a slope); false: none (a wall's top, a pit), the spawn point tested with no enemy
static bool EnemyAhead(float d, float* out) {
    if (Ahead(d, 0.f, out)) return true;
    out[1] = FloorNear(out[0], out[2], gPos[1] - 800.f, 2500.f);
    return out[1] > -1e29f;
}
// (v150-host beta 25, booktest) out of a drop's, a climb's and a fence's hold: no drop, climb or fence begun in the last 60 frames
// (the body's climb holds it 60 frames, a fall then its landing's 18; BookFrame reads no book in them, "a drop or a climb under
// way first, as a player's"), its hips 0.8 m over the floor under it within 0.2 m (the mesh's height against the faces'). Since
// cf5eebb and 9b219fa the dolls climb and drop more (the stairs of blocks; other ways round the fences): a book laid the frame it
// landed from a 2.8 m drop (n001a) or 43 frames after its climb onto a 1 m block (n022a) was read 18 and 17 frames later, at its
// hold's end, past the 12 frames
static bool OnFloor() {
    const float under = Floor(gPos[0], gPos[2], gPos[1] - 800.f);
    return under > -1e29f && std::fabs(gPos[1] - 800.f - under) <= 200.f;
}
static bool Settled() { return gWatchLinks && gNow - gLinkAt >= 60 && OnFloor(); }
static void Enemy(bool on, const float* at = nullptr) {
    gHave[2] = on;
    if (!on) return;
    gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1; gPlayers[2].object = gPlayers[2].alive = true; gPlayers[2].life = 200; gPlayers[2].cls = 0;
    gPlayers[2].pos[0] = at[0]; gPlayers[2].pos[1] = at[1] + 800.f; gPlayers[2].pos[2] = at[2];
}
// the doll reads the book id within n frames
static bool ReadWithin(uint32_t id, int n) { for (int i = 0; i < n; ++i) { Step(); if (gLastRead == id) return true; } return false; }
// (v150-host beta 25, bookhold: check 11) the place its MGO1_VDOLL_TEST_TRAP ways go to (Host::traps), none when off
static bool gLureOn = false;
static float gLure[3];
static int Traps(float out[][3], int max) { if (!gLureOn || max < 1) return 0; memcpy(out[0], gLure, sizeof gLure); return 1; }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_book_test <map.geom>\n"); return 2; }
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 1; }
    fseek(fp, 0, SEEK_END); gGeom.resize(ftell(fp)); fseek(fp, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), fp) != gGeom.size()) return 1;
    fclose(fp);
    std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), gG, err)) { printf("not a geom: %s\n", err.c_str()); return 1; }
    std::vector<const doll::Spawn*> spawns;
    for (const doll::Spawn& sp : gG.spawns) if (sp.kind == doll::SP_DM || sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_TDM_BLUE) spawns.push_back(&sp);
    if (spawns.size() < 1) { printf("no spawn point in the geom\n"); return 1; }
    setenv("MGO1_VDOLL_WEAPONS", "ak47", 0);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "4242", 1);
    if (!getenv("MGO1_VDOLL_LEVEL")) setenv("MGO1_VDOLL_LEVEL", "10", 1);
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.realMs = RealMsNow;
    h.traps = Traps;                                     // (v150-host beta 25, bookhold: check 11's ways)
    if (const char* sl = getenv("VDOLL_TEST_SLOW")) gSlow = std::max(1.f, std::min(3.f, (float)atof(sl)));
#ifdef VDOLL_HOST_BOOKS
    h.books = Books; h.readBook = ReadBook; h.bookDone = BookDone;
    const bool hostBooks = true;
#else
    const bool hostBooks = false;
#endif
#ifdef VDOLL_HOST_DROPS
    h.drops = Drops;                                     // (v150-host beta 22, review: check 6)
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    gNow = 600;
    printf("%s: the host's book list %s, MGO1_VDOLL_BOOKS %s, the host's frames %.2f times a 59.94th of a second\n", argv[1],
           hostBooks ? "given" : "NOT in this body's Host (beta 21)", getenv("MGO1_VDOLL_BOOKS") ? getenv("MGO1_VDOLL_BOOKS") : "unset (on)", gSlow);
    gS.k = 0; gS.slot = 1; gS.team = 0; gS.pos = gPos; gS.facing = &gFacing; gS.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].cls = 0;
    int spots = 0, read1 = 0, still2 = 0, faced2 = 0, noShot2 = 0, shotCounted = 0, moved3 = 0, fought3 = 0, timed5 = 0;
    double worstMs = 0;
    int far4 = 0, own4 = 0, cool4 = 0, again4 = 0, raikov4 = 0, refused4 = 0, tried4 = 0, coolTried = 0, againTried = 0;
    int told7 = 0, copy7 = 0;                              // (v150-host beta 22, bookplay)
    for (size_t si = 0; si < spawns.size() && si < 8; ++si) {
        gBooks.clear(); gLastRead = 0; Enemy(false);
        gPlayers[1].cls = 0;
#ifdef VDOLL_PLAYERNOW_BOOK
        gPlayers[1].bookRefused = false;
#endif
        gPos[0] = spawns[si]->p[0]; gPos[1] = spawns[si]->p[1] + 800.f; gPos[2] = spawns[si]->p[2];
        gFacing = (uint16_t)(si * 9000);
        gS.life = gNow;
        for (int i = 0; i < 480; ++i) { if (i == 420) WatchLinks(); Step(); }   // (8 s: its spawn's cooldown over, on its ways)
        for (int i = 0; i < 240 && !Settled(); ++i) Step();   // ((v150-host beta 25, booktest) out of a drop's or a climb's hold)
        gWatchLinks = false;
        float bp[3], ep[3];
        if (!Ahead(700.f, 0.f, bp)) { printf("  spawn %zu: no floor ahead, not counted\n", si); continue; }
        // ((v150-host beta 25, booktest) the enemy on a slope too, or none: the book's checks at every spawn point with a floor 0.7 m
        // ahead. Since 9b219fa (the fences no player crosses left out of the bodies' ways) the doll's 8 s on n022a's hills end where
        // the floor 6 m ahead is 0.70 to 1.92 m off its own at all 8 spawn points (before it, at 7 of 8): none was tested)
        const bool haveEnemy = EnemyAhead(6000.f, ep);
        {                                                  // (a railing or a wall between its eye and the book: the game's own line refuses it too)
            const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, end[3] = {bp[0], bp[1] + 200.f, bp[2]};
            if (vdoll::body::SightStopped(eye, end)) { printf("  spawn %zu: the book's place 0.7 m ahead out of its sight (a wall between), not counted\n", si); continue; }
        }
        ++spots;
        gRecs.clear(); gStates.clear();                    // (v150-host beta 22, bookplay: check 7)
        const uint32_t id = AddBook(3, bp);
        const bool r = ReadWithin(id, 12);
        Enemy(haveEnemy, ep);                              // (the enemy comes once it reads: no shot at him while it reads)
        const int shots0 = Shots();
        if (r) ++read1;
        else if (getenv("VDOLL_TEST_EVENTS")) printf("    not read: the doll at %.0f %.0f %.0f, the book at %.0f %.0f %.0f (%.0f mm in 3D), %s\n", gPos[0], gPos[1], gPos[2], bp[0], bp[1], bp[2],
                                                     std::sqrt((gPos[0] - bp[0]) * (gPos[0] - bp[0]) + (gPos[1] - bp[1]) * (gPos[1] - bp[1]) + (gPos[2] - bp[2]) * (gPos[2] - bp[2])),
                                                     vdoll::body::Status(0).c_str());
        float from[3]; memcpy(from, gPos, sizeof from);
        float ran = 0.f, last[3]; memcpy(last, gPos, sizeof last);
        float worst = 0.f;
        long long readAt = gNow;
        bool shot = false;
        while (r && gNow - readAt < kReadFrames + 10) {
            Step();
            bool lies = false;
            for (const TBook& b : gBooks) if (b.id == id) lies = true;
            if (!lies) break;                                // (taken away in this frame: its move then is 3's)
            const float m = std::hypot(gPos[0] - last[0], gPos[2] - last[2]);
            if (m > 1.f && getenv("VDOLL_TEST_EVENTS")) printf("    frame +%lld of its reading: moved %.0f mm\n", gNow - readAt, m);
            ran += m; memcpy(last, gPos, sizeof last);
            if (Shots() != shots0) shot = true;
            if (gNow - readAt >= 60) worst = std::max(worst, std::fabs(WrapD(FacingDeg() - std::atan2(bp[0] - gPos[0], bp[2] - gPos[2]) * 180.f / kPi)));
        }
        const long long readFrames = gNow - readAt;
        // 5: its reading lasts the read time at a player's own 60 passes a second by the machine's clock (2 frames' slack)
        const double readMs = (double)readFrames * 1000.0 / 59.94 * gSlow, wantMs = (double)kReadFrames * 1000.0 / 59.94;
        if (r && std::fabs(readMs - wantMs) <= 2.0 * 1000.0 / 59.94 * gSlow) ++timed5;
        if (r) worstMs = std::max(worstMs, std::fabs(readMs - wantMs));
        if (r && ran <= 30.f) ++still2;
        if (r && worst <= 20.f) ++faced2;
        // 3: moves on, fights the enemy it did not fire at
        float mv = 0.f; memcpy(last, gPos, sizeof last);
        bool fought = false;
        const int shots1 = Shots();
        for (int i = 0; i < 300; ++i) { Step(); if (i < 180) mv = std::max(mv, std::hypot(gPos[0] - last[0], gPos[2] - last[2])); if (Shots() != shots1) fought = true; }
        if (r && (mv >= 500.f || fought)) ++moved3;
        if (fought) { ++fought3; ++shotCounted; if (!shot) ++noShot2; }
        // 7 (v150-host beta 22, bookplay): its reading's records and the state byte, and a copy fed with them
        if (r) {
            const long long endF = readAt + readFrames;
            int longs = 0, shorts = 0, badLong = 0, styles = 0, looks = 0, faces = 0, zeros = 0, after0E = 0, ticks = 0, bad2 = 0, bad0 = 0;
            bool styleBack = false, faceBack = false;
            long long firstRec = -1;
            const long long t0 = (readAt + 5) / 6 * 6;        // (its first record tick in the reading: the long record's)
            int places = 0, badPlaces = 0;
            for (const TRec& x : gRecs)
                if (x.f >= readAt && x.f < endF && x.kind == 0 && x.r.size() == 13 && x.r[0] == 0x00) { if (((x.f - t0) / 6) % 3 == 1) ++places; else ++badPlaces; }
            int wantPlaces = 0;
            for (long long f = t0; f < endF; f += 6) if (((f - t0) / 6) % 3 == 1) ++wantPlaces;
            for (const TRec& x : gRecs) {
                const bool in = x.f >= readAt && x.f < endF;
                if (x.kind == 1 && !x.r.empty() && x.r[0] == 0x0E) {
                    if (!in) { if (x.f >= endF) ++after0E; continue; }
                    if (firstRec < 0) {
                        firstRec = x.f;
                        bool ok = x.r.size() == 14 && x.r[1] == 1;
                        for (int k = 0; ok && k < 3; ++k) ok = std::abs(Le16s(&x.r[2 + 2 * k]) - (int16_t)(int)(from[k] / 10.f)) <= 1 && Le16s(&x.r[8 + 2 * k]) == (int16_t)(int)(bp[k] / 10.f);
                        if (ok) ++longs; else ++badLong;
                    } else if (x.r.size() == 2 && x.r[1] == 1) ++shorts; else ++badLong;
                    continue;
                }
                if (in && x.kind == 1 && !x.r.empty() && x.r[0] == 0x06) ++styles;
                if (in && x.kind == 2 && !x.r.empty() && x.r[0] == 0x17) ++looks;
                if (in && x.kind == 0 && x.r.size() == 7 && x.r[0] == 0x01) { bool z = true; for (size_t k = 1; k < 7; ++k) z = z && !x.r[k]; if (z) ++zeros; else ++faces; }
                if (x.f > endF && x.f <= endF + 13 && x.kind == 1 && !x.r.empty() && x.r[0] == 0x06) styleBack = true;
                if (x.f > endF && x.f <= endF + 13 && x.kind == 0 && x.r.size() == 7 && x.r[0] == 0x01) faceBack = true;   // (its facing may be 0)
            }
            for (const TState& t : gStates) {
                if (t.f > readAt && t.f < endF) { ++ticks; if (t.event != 2) ++bad2; }
                if (t.f > endF && t.f <= endF + 120 && t.event != 0) ++bad0;
            }
            const int recTicks = (int)((endF - 1) / 6 - (readAt - 1) / 6);   // (the tick frames in [readAt, endF))
            const bool told = longs == 1 && badLong == 0 && shorts == recTicks - 1 && styles == 0 && looks == 0 && faces == 0 && zeros <= 1 && after0E == 0 &&
                              bad2 == 0 && bad0 == 0 && ticks > 0 && styleBack && faceBack && places == wantPlaces && badPlaces == 0;
            Copy c;
            RunCopy(c, readAt - 6, endF + 120, bp);
            const bool cp = c.starts == 1 && c.ends == 1 && c.from >= readAt && c.from <= readAt + 6 && c.to > endF && c.to <= endF + 12 && c.worst <= 15.f;
            if (told) ++told7;
            if (cp) ++copy7;
            if (!told || !cp || getenv("VDOLL_TEST_EVENTS"))
                printf("    7: {0E} long %d (bad %d), short %d of %d ticks, style %d, look %d, facing %d, zeros %d, place %d of %d (off cadence %d), {0E} after %d; the byte 2 at %d ticks (not %d), 0 after (not %d), "
                       "style back %d, facing back %d; the copy read %d time(s) from +%lld to +%lld (the doll's end +%lld), its turn's point off the book by %.0f mm\n", longs, badLong, shorts, recTicks - 1,
                       styles, looks, faces, zeros, places, wantPlaces, badPlaces, after0E, ticks, bad2, bad0, styleBack, faceBack, c.starts, c.from - readAt, c.to - readAt, endF - readAt, c.worst);
        }
        printf("  spawn %zu: the book 0.7 m ahead %s; read for %lld frames, moved %.0f mm, its facing off the book by %.0f degrees at most, %s; then %s, the enemy %s\n",
               si, r ? "READ" : "NOT read", r ? readFrames : 0, ran, worst, shot ? "SHOTS while reading" : "no shot", mv >= 500.f ? "on its way" : "still",
               fought ? "fought" : haveEnemy ? "not fought (not in its sight)" : "none (no floor 6 m ahead within 2.5 m of its own)");
        Enemy(false);
        // 4: none read out of reach, its own, in its cooldown
        ++tried4;
        // (out of reach: asked over 2 frames, the doll moving 0.2 m at most in them: 1.6 m on the floor is 1.8 m from its hips)
        { float p[3]; gLastRead = 0; if (Ahead(1600.f, 0.f, p)) { const uint32_t x = AddBook(3, p); if (!ReadWithin(x, 2)) ++far4; gBooks.clear(); } else ++far4; }
        { float p[3]; gLastRead = 0; if (Ahead(500.f, 0.f, p)) { const uint32_t x = AddBook(1, p); if (!ReadWithin(x, 30)) ++own4; gBooks.clear(); } else ++own4; }
        // its cooldown: a reading now (the last one's cooldown over first), then a book at once after its end, then 8 s later
        for (int i = 0; i < 480; ++i) Step();
        { float p[3]; gLastRead = 0;
          if (Ahead(500.f, 0.f, p)) {
              const uint32_t x = AddBook(3, p);
              if (ReadWithin(x, 30)) {                    // (the cooldown's own test counted where this reading came: not in a drop, a climb)
                  while (!gBooks.empty()) Step();
                  const double fms = 1000.0 / 59.94 * gSlow;     // (a host frame on the clock)
                  const int coolF = (int)(7508.0 / fms), waitF = 60, lookF = coolF - waitF - 10, againF = (int)(8000.0 / fms) + 1;
                  for (int i = 0; i < waitF; ++i) Step();
                  float q[3];
                  if (Ahead(500.f, 0.f, q)) { ++coolTried; const uint32_t y = AddBook(3, q); if (!ReadWithin(y, lookF)) ++cool4; gBooks.clear(); }
                  else for (int i = 0; i < lookF; ++i) Step();   // (no floor ahead now: the same time all the same)
                  for (int i = waitF + lookF; i < againF; ++i) Step();   // (8 s after the end in all, on the clock)
                  // (read again: a book 0.5 m ahead, laid again 1 s later up to 5 times: one laid while it drops down a slope or
                  // climbs is not read, as a player's in his drop)
                  ++againTried;
                  bool again = false;
                  for (int t = 0; t < 5 && !again; ++t) {
                      if (t) for (int i = 0; i < 60; ++i) Step();
                      if (!Ahead(500.f, 0.f, q)) continue;
                      const uint32_t y = AddBook(3, q);
                      again = ReadWithin(y, 12);
                      if (!again) {
                          if (getenv("VDOLL_TEST_EVENTS")) {
                              const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, end[3] = {q[0], q[1] + 200.f, q[2]};
                              printf("    not read again (try %d): the doll at %.0f %.0f %.0f, the book at %.0f %.0f %.0f, the sight's line %s\n", t + 1, gPos[0], gPos[1], gPos[2],
                                     q[0], q[1], q[2], vdoll::body::SightStopped(eye, end) ? "STOPPED" : "clear");
                          }
                          gBooks.clear();
                      }
                  }
                  if (again) ++again4;
                  while (!gBooks.empty()) Step();
              }
              gBooks.clear();
          } }
        for (int i = 0; i < 480; ++i) Step();               // (its cooldown over)
        { float p[3]; gLastRead = 0; gPlayers[1].cls = 4; for (int i = 0; i < 6; ++i) Step();
          if (Ahead(500.f, 0.f, p)) { const uint32_t x = AddBook(3, p); if (!ReadWithin(x, 30)) ++raikov4; gBooks.clear(); } else ++raikov4;
          gPlayers[1].cls = 0; }
#ifdef VDOLL_PLAYERNOW_BOOK
        { float p[3]; gLastRead = 0; gPlayers[1].bookRefused = true; for (int i = 0; i < 6; ++i) Step();
          if (Ahead(500.f, 0.f, p)) { const uint32_t x = AddBook(3, p); if (!ReadWithin(x, 30)) ++refused4; gBooks.clear(); } else ++refused4;
          gPlayers[1].bookRefused = false; }
#else
        ++refused4;
#endif
    }
    // 8 and 9 (v150-host beta 22, bookplay): a teammate's book, deathmatch; a reading ended by a flinch and by its death (each spawn
    // point in turn until one has the floor and its sight 0.5 m ahead)
    int mate8 = -1, dm8 = -1, flinch9 = -1, cool9 = -1, death9 = -1;
    int cls10[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};   // (v150-host beta 22, bookplay2: 1 read, 0 not, -1 not tried)
    for (size_t si = 0; si < spawns.size() && si < 8 && mate8 < 0; ++si) {
        gBooks.clear(); gLastRead = 0; Enemy(false);
        gPos[0] = spawns[si]->p[0]; gPos[1] = spawns[si]->p[1] + 800.f; gPos[2] = spawns[si]->p[2];
        gFacing = (uint16_t)(si * 9000);
        gS.life = gNow;
        WatchLinks();                                      // ((v150-host beta 25, booktest) its drops and climbs followed: Settled)
        for (int i = 0; i < 480; ++i) Step();
        float p[3];
        if (!Ahead(500.f, 0.f, p)) continue;
        { const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, end[3] = {p[0], p[1] + 200.f, p[2]}; if (vdoll::body::SightStopped(eye, end)) continue; }
        // a teammate (player 3, the doll's team 0) laid it, alive 4 m behind
        gHave[3] = true; gPlayers[3] = vdoll::PlayerNow(); gPlayers[3].team = 0; gPlayers[3].object = gPlayers[3].alive = true; gPlayers[3].life = 200;
        gPlayers[3].pos[0] = gPos[0] - 4000.f * std::sin(FacingDeg() * kPi / 180.f); gPlayers[3].pos[1] = gPos[1]; gPlayers[3].pos[2] = gPos[2] - 4000.f * std::cos(FacingDeg() * kPi / 180.f);
        // (a book laid 1 s later up to 5 times: one laid in a drop or a climb is not read, as a player's in his; check 4's way)
        auto startRead = [&]() {                            // (true: it reads the book laid, which still lies)
            for (int t = 0; t < 5; ++t) {
                if (t) for (int i = 0; i < 60; ++i) Step();
                // ((v150-host beta 25, booktest) laid out of a drop's or a climb's hold, 4 s at most: on n022a's hills a slowed host's
                // (VDOLL_TEST_SLOW=1.5) 5 tries met Snake's climbs, check 10 failed there)
                for (int i = 0; i < 240 && !Settled(); ++i) Step();
                float q[3];
                if (!Ahead(500.f, 0.f, q)) continue;
                const uint32_t x = AddBook(3, q);
                if (ReadWithin(x, 12)) return true;
                gBooks.clear();
            }
            return false;
        };
        auto tries = [&]() { if (!startRead()) return 0; while (!gBooks.empty()) Step(); return 1; };
        mate8 = tries();
        // deathmatch: no teams (0xFF), any other player's book (its cooldown over first: 8 s on the clock)
        gRuleNow = 0; gS.team = 0xFF; gPlayers[1].team = 0xFF; gPlayers[3].team = 0xFF;
        const double fms = 1000.0 / 59.94 * gSlow;          // (a host frame on the clock)
        for (int i = 0; i < (int)(8000.0 / fms) + 1; ++i) Step();
        dm8 = tries();
        gRuleNow = 1; gS.team = 0; gPlayers[1].team = 0; gHave[3] = false;
        // 9: a flinch 1 s into a reading, then a book 5 s later (its cooldown); after it, its death 1 s into a reading
        for (int i = 0; i < (int)(8000.0 / fms) + 1; ++i) Step();   // (its cooldown over)
        {
            if (startRead()) {
                for (int i = 0; i < 60; ++i) Step();
                gRecs.clear(); gStates.clear();
                const long long at = gNow;
                vdoll::body::Pause(0);
                for (int i = 0; i < 30; ++i) Step();
                vdoll::body::Resume(0);
                int e2 = 0, oe = 0;
                for (const TState& t : gStates) if (t.f > at && t.event != 0) ++e2;
                for (const TRec& y : gRecs) if (y.kind == 1 && !y.r.empty() && y.r[0] == 0x0E) ++oe;
                flinch9 = e2 == 0 && oe == 0 ? 1 : 0;
                gBooks.clear();
                const int to5 = (int)(5000.0 / fms) - 30;
                for (int i = 0; i < to5; ++i) Step();   // (5 s after the flinch on the clock: its cooldown runs)
                // ((v150-host beta 25, booktest) laid when it stands on its floor out of a drop's or a climb's hold (Settled) with the
                // floor 0.5 m ahead and a clear line to it, tried every 6 frames from 5 s on, its 12 frames' look (check 1's) over 2
                // frames before its 7.5 s cooldown ends; a look in which a drop or a climb began tried again. Since cf5eebb (the stairs of blocks climbed
                // as a player climbs them) the doll on n012a is at 5 s in a drop down the catwalk's blocks (its hips 0.68 m over a
                // block's top, the floor 0.5 m ahead 0.88 m lower): no book was laid, the check not tried. A book laid in a drop, a
                // climb or out of its sight shows nothing of the cooldown)
                const long long lastAt = at + (long long)(7508.0 / fms) - 14;   // (its cooldown from the flinch: 7508 ms, as check 4's)
                for (bool first = true; gNow <= lastAt && cool9 < 0; first = false) {
                    if (!first) for (int i = 0; i < 6; ++i) Step();
                    float q[3];
                    const bool settled = Settled(), ahead = Ahead(500.f, 0.f, q);
                    const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, end[3] = {q[0], q[1] + 200.f, q[2]};
                    const bool seen = settled && ahead && !vdoll::body::SightStopped(eye, end);
                    if (getenv("VDOLL_TEST_EVENTS") && !seen)
                        printf("    9: +%lld frames, at %.0f %.0f %.0f facing %.0f: %s\n", gNow - at, gPos[0], gPos[1], gPos[2], FacingDeg(),
                               !settled ? (OnFloor() ? "a drop, a climb or a fence begun in the last 60 frames" : "not on its floor") : !ahead ? "no floor 0.5 m ahead" : "the place 0.5 m ahead out of its sight");
                    if (!seen) continue;
                    const long long linkWas = gLinkAt;
                    const uint32_t y = AddBook(3, q);
                    const bool read = ReadWithin(y, 12);
                    gBooks.clear();
                    if (!read && gLinkAt != linkWas) continue;
                    cool9 = read ? 0 : 1;
                }
            }
            gBooks.clear();
        }
        for (int i = 0; i < (int)(8000.0 / fms) + 1; ++i) Step();   // (its cooldown over)
        {
            if (startRead()) {
                for (int i = 0; i < 60; ++i) Step();
                gRecs.clear(); gStates.clear();
                const long long at = gNow;
                vdoll::body::Down(0);                          // (its death: vdoll.cpp's DEAD, its ticks are DownTick's)
                gS.life = gNow + 1000000;                      // (a new life later)
                for (int i = 0; i < 30; ++i) { ++gNow; GameFrame(); if (gNow % 6 == 0) gStates.push_back({gNow, StateEventNow()}); }
                int e2 = 0, oe = 0;
                for (const TState& t : gStates) if (t.f > at && t.event != 0) ++e2;
                for (const TRec& y : gRecs) if (y.kind == 1 && !y.r.empty() && y.r[0] == 0x0E) ++oe;
                death9 = e2 == 0 && oe == 0 ? 1 : 0;
            }
            gBooks.clear();
        }
        // 10 (bookplay2): each class in turn, its cooldown over first (its death above: a new life now)
        gS.life = gNow;
        for (int c = 0; c <= 8; ++c) {
            gPlayers[1].cls = c;
            for (int i = 0; i < (int)(8000.0 / fms) + 1; ++i) Step();
            cls10[c] = startRead() ? 1 : 0;
            while (!gBooks.empty()) Step();
        }
        gPlayers[1].cls = 0;
    }
    gWatchLinks = false;
    // 6 (v150-host beta 22, review): the goggles on, no reading (each spawn point in turn until one has the floor and sight)
    int gogTried = 0, gogRead = 0;
#if defined(VDOLL_HOST_DROPS) && defined(VDOLL_BODY_PICKUP)
    gRuleNow = 4;
    for (size_t si = 0; si < spawns.size() && si < 8 && gogTried == 0; ++si) {
        gBooks.clear(); gLastRead = 0; Enemy(false); gDrops.clear();
        gPos[0] = spawns[si]->p[0]; gPos[1] = spawns[si]->p[1] + 800.f; gPos[2] = spawns[si]->p[2];
        gFacing = (uint16_t)(si * 9000);
        gS.life = gNow; gS.send = Send3;
        for (int i = 0; i < 480; ++i) Step();               // (its spawn's cooldown over)
        vdoll::DropNow g;                                  // (the goggles at its feet: the game's zone takes them at once)
        g.number = 0x54; g.kind = 1; g.code = 7; g.count = 1; g.state = 0; g.flags = 1 << 10 | 7; g.box = true;
        g.pos[0] = gPos[0]; g.pos[1] = gPos[1] - 800.f; g.pos[2] = gPos[2];
        gDrops.push_back(g);
        vdoll::body::PickupInfo pi;                        // (SNEAKING, a soldier: it wears them as soon as it has them)
        for (int i = 0; i < 120 && !(vdoll::body::Pickup(0, &pi) && pi.gogglesOn); ++i) Step();
        float p[3];
        vdoll::body::Pickup(0, &pi);
        if (pi.gogglesOn && Ahead(500.f, 0.f, p)) {
            const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, end[3] = {p[0], p[1] + 200.f, p[2]};
            if (!vdoll::body::SightStopped(eye, end)) {
                ++gogTried;
                const uint32_t x = AddBook(3, p);
                if (ReadWithin(x, 30)) ++gogRead;
                printf("  spawn %zu: the thermal goggles on, a book 0.5 m ahead %s\n", si, gogRead ? "READ" : "not read");
            }
        }
        gBooks.clear(); gDrops.clear(); gS.send = Send;
    }
    gRuleNow = 1;
#endif
    // 11 (v150-host beta 25, bookhold): its drops, climbs and fences hold a book back as a player's own game holds it (measured on
    // real clients, the playable dolls' probe 52: reports/vdoll_bookhold.md). The doll 1.2 m before a link of the body's own (the
    // doorways, the fences' second look, WayRunnable), sent over it (MGO1_VDOLL_TEST_TRAP, a place 2 m past it); a book laid when the
    // link begins, 0.6 m past its end (in reach and sight there). A climb of 0.8 to 1 m: read 54 frames after the climb's start (a
    // player's press: 27 ticks; beta 22's hold 60); a drop: 18 frames after its landing (both; (v150-host beta 25, fallflinch) a player's 9 ticks by the clock, fewer frames slowed); a fence crossed: read on the fence (a
    // player's crossing runs no action, no refused state), still there while it reads, then over it (beta 22: at the crossing's end).
    // Up to 3 places of each kind a map; a place where its link ends elsewhere or it takes another way: not counted
    int tried11[3] = {0, 0, 0}, ok11[3] = {0, 0, 0};
    std::string bad11;
    {
        const bool holdOn = !(getenv("MGO1_VDOLL_BOOK_HOLD") && !strcmp(getenv("MGO1_VDOLL_BOOK_HOLD"), "0"));
        doll::NavMesh nav; doll::NavConfig cfg; std::string nl;
        cfg.doorways = !(getenv("MGO1_VDOLL_STAIRS") && !strcmp(getenv("MGO1_VDOLL_STAIRS"), "0"));
        nav.Build(gG, cfg, nl);
        if (!(getenv("MGO1_VDOLL_VAULT") && !strcmp(getenv("MGO1_VDOLL_VAULT"), "0"))) vdoll::body::VaultsLeftOut(nav);
        struct Site { float a[3], b[3]; };
        std::vector<Site> sites[3];
        for (int i = 0; i < nav.LinkCount(); ++i) {
            Site st; uint8_t area = 0;
            if (!nav.LinkAt(i, st.a, st.b, &area)) continue;
            int kind = -1;
            if (area == doll::AREA_CLIMB) { if (st.a[1] > st.b[1]) std::swap(st.a, st.b); if (st.b[1] - st.a[1] >= 800.f && st.b[1] - st.a[1] <= 1000.f) kind = 0; }
            else if (area == doll::AREA_DROP) { if (st.a[1] - st.b[1] >= 700.f && st.a[1] - st.b[1] <= 3500.f) kind = 1; }
            else if (area == doll::AREA_VAULT) { if (std::fabs(st.b[1] - st.a[1]) <= 300.f) kind = 2; }
            if (kind < 0 || sites[kind].size() >= 12) continue;
            std::vector<doll::Waypoint> w(2);
            memcpy(w[0].p, st.a, sizeof st.a); w[0].area = area; w[0].link = true;
            memcpy(w[1].p, st.b, sizeof st.b); w[1].area = doll::AREA_STAND; w[1].link = false;
            if (!vdoll::body::WayRunnable(w)) continue;
            bool near = false;
            for (const Site& o : sites[kind]) near |= std::hypot(o.a[0] - st.a[0], o.a[2] - st.a[2]) < 6000.f;
            if (!near) sites[kind].push_back(st);
        }
        static const char* const kKind[3] = {"a climb", "a drop", "a fence"};
        const bool ev = getenv("VDOLL_TEST_EVENTS") != nullptr;
        if (ev) printf("  11: the body's links here: %zu climbs of 0.8 to 1 m, %zu drops, %zu fences (6 m apart)\n", sites[0].size(), sites[1].size(), sites[2].size());
        const int climbF = (int)std::ceil(54.0 / gSlow);         // (the player's 27 ticks by the clock: fewer of a slowed host's frames)
        for (int kind = 0; kind < 3; ++kind)
            for (size_t si = 0; si < sites[kind].size() && tried11[kind] < 3; ++si) {
                const Site& st = sites[kind][si];
                const float dx = st.b[0] - st.a[0], dz = st.b[2] - st.a[2], dl = std::max(1.f, std::hypot(dx, dz)), ux = dx / dl, uz = dz / dl;
                // its start 1.2 m before the link on the link's near floor; its way's end (the lure) 2 m past it on the far floor; the book
                // laid when the link begins, 0.6 m past its end (in reach there; a climb's top, a drop's landing, the fence's far side)
                float s0[3] = {st.a[0] - ux * 1200.f, 0.f, st.a[2] - uz * 1200.f}, lure[3] = {st.b[0] + ux * 2000.f, 0.f, st.b[2] + uz * 2000.f};
                float bk[3] = {st.b[0] + ux * 600.f, 0.f, st.b[2] + uz * 600.f};
                s0[1] = Floor(s0[0], s0[2], st.a[1]); lure[1] = Floor(lure[0], lure[2], st.b[1]); bk[1] = Floor(bk[0], bk[2], st.b[1]);
                if (s0[1] < -1e29f || lure[1] < -1e29f || bk[1] < -1e29f || std::fabs(s0[1] - st.a[1]) > 200.f || std::fabs(bk[1] - st.b[1]) > 200.f) {
                    if (ev) printf("  11 %s at %.0f %.0f %.0f: no floor before it, past it or 0.6 m past its end\n", kKind[kind], st.a[0], st.a[1], st.a[2]);
                    continue;
                }
                if (kind != 2) {                               // (a climb's top, a drop's landing: the book in sight there, as the game's line asks)
                    const float eye[3] = {st.b[0], st.b[1] + 1550.f, st.b[2]}, end[3] = {bk[0], bk[1] + 200.f, bk[2]};
                    if (vdoll::body::SightStopped(eye, end)) { if (ev) printf("  11 %s at %.0f %.0f %.0f: the book out of sight from its end\n", kKind[kind], st.a[0], st.a[1], st.a[2]); continue; }
                }
                gBooks.clear(); gLastRead = 0; Enemy(false); gPlayers[1].cls = 0;
                gPos[0] = s0[0]; gPos[1] = s0[1] + 800.f; gPos[2] = s0[2];
                gFacing = (uint16_t)(int)(std::atan2(dx, dz) / (2.f * kPi) * 65536.f);
                memcpy(gLure, lure, sizeof gLure); gLureOn = true; setenv("MGO1_VDOLL_TEST_TRAP", "1", 1);
                gS.life = gNow + 1;
                Step();                                        // (a new life there)
                gNow += 470;                                   // (its spawn's cooldown over by the clock: 470 frames with no pass, the host's
                                                               // frame time measured from passes 60 frames apart at most is not changed)
                const int links0 = LinkCount();
                uint32_t id = 0;
                long long start = -1, land = -1, read = -1;
                float endAt[3] = {0, 0, 0};                    // (where its link put it: a climb's top at once, a drop's landing)
                for (int i = 0; i < 900 && read < 0; ++i) {
                    Step();
                    if (start < 0 && LinkCount() != links0) {
                        if (std::hypot(gPos[0] - st.a[0], gPos[2] - st.a[2]) > 2000.f && std::hypot(gPos[0] - st.b[0], gPos[2] - st.b[2]) > 2000.f) break;   // (another link)
                        start = gNow;
                        id = AddBook(3, bk);
                    }
                    if (start < 0 && i >= 600) break;
                    if (kind == 0 && start == gNow) memcpy(endAt, gPos, sizeof endAt);
                    if (kind == 1 && start >= 0 && land < 0 && gPos[1] - 800.f <= st.b[1] + 30.f) { land = gNow; memcpy(endAt, gPos, sizeof endAt); }
                    if (id && gLastRead == id) read = gNow;
                }
                gLureOn = false; unsetenv("MGO1_VDOLL_TEST_TRAP");
                if (start >= 0 && kind != 2 && std::hypot(endAt[0] - st.b[0], endAt[2] - st.b[2]) > 1000.f) {   // (another link's end, or its landing elsewhere)
                    if (ev) printf("  11 %s at %.0f %.0f %.0f: its link ended at %.0f %.0f %.0f, not at this one's end\n", kKind[kind], st.a[0], st.a[1], st.a[2], endAt[0], endAt[1], endAt[2]);
                    gBooks.clear(); continue;
                }
                if (start < 0 || read < 0 || (kind == 1 && land < 0)) {
                    if (ev) printf("  11 %s at %.0f %.0f %.0f: not taken (its start %lld, read %lld; the doll at %.0f %.0f %.0f)\n", kKind[kind], st.a[0], st.a[1], st.a[2], start, read, gPos[0], gPos[1], gPos[2]);
                    gBooks.clear(); continue;
                }
                ++tried11[kind];
                bool ok = false; char why[200] = "";
                if (kind == 0) {
                    ok = read - start >= climbF && read - start <= climbF + 2;
                    snprintf(why, sizeof why, "read %lld frames after the climb's start", read - start);
                } else if (kind == 1) {
                    // ((v150-host beta 25, fallflinch) the landing's hold a player's 9 ticks by the clock (MGO1_VDOLL_HIT_FALL): 18 of the host's
                    // frames at full pace, fewer on a slowed host; b16's 18 frames whatever the pace)
                    const int landF = (int)std::lrint(18.0 / gSlow);
                    ok = read - land >= landF - 1 && read - land <= landF + 3;
                    snprintf(why, sizeof why, "read %lld frames after its landing (%lld after the drop's start)", read - land, read - start);
                } else {
                    const long long len = std::max(12L, std::lrint(std::hypot(st.b[0] - st.a[0], st.b[2] - st.a[2]) / 26.f));
                    const bool onFence = read - start < len;
                    float ran = 0.f, last[3]; memcpy(last, gPos, sizeof last);
                    for (int i = 0; i < 120; ++i) { Step(); ran += std::hypot(gPos[0] - last[0], gPos[2] - last[2]); memcpy(last, gPos, sizeof last); }
                    bool over = false;
                    for (int i = 0; i < 900 && !over; ++i) { Step(); bool lies = false; for (const TBook& x : gBooks) lies |= x.id == id; if (!lies && std::hypot(gPos[0] - st.b[0], gPos[2] - st.b[2]) < std::hypot(gPos[0] - st.a[0], gPos[2] - st.a[2]) - 300.f) over = true; }
                    ok = onFence && ran <= 30.f && over;
                    snprintf(why, sizeof why, "read %lld frames after the crossing's start (its %lld frames: %s), %.0f mm moved in the 2 s after, %s", read - start, len,
                             onFence ? "on the fence" : "after it", ran, over ? "over it after" : "NOT over it after");
                }
                if (ok) ++ok11[kind];
                else { char t[320]; snprintf(t, sizeof t, "\n    %s at %.0f %.0f %.0f: %s", kKind[kind], st.a[0], st.a[1], st.a[2], why); bad11 += t; }
                if (ev || !ok) printf("  11 %s at %.0f %.0f %.0f: %s%s\n", kKind[kind], st.a[0], st.a[1], st.a[2], why, ok ? "" : " (FAIL)");
                gBooks.clear();
                for (int i = 0; i < 60; ++i) Step();
            }
        printf("the holds (MGO1_VDOLL_BOOK_HOLD %s): climbs %d of %d, drops %d of %d, fences %d of %d as a player's game holds a book%s\n", holdOn ? "on" : "0",
               ok11[0], tried11[0], ok11[1], tried11[1], ok11[2], tried11[2], bad11.c_str());
    }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    printf("spawn points tested %d: read %d; still while the book lay %d, facing it %d; no shot while reading %d of %d where the enemy was fought after; moved on %d\n",
           spots, read1, still2, faced2, noShot2, shotCounted, moved3);
    printf("read none: out of reach %d, its own %d, as Raikov %d, in a refused state %d (of %d), in its cooldown %d of %d; read again after its cooldown %d of %d\n",
           far4, own4, raikov4, refused4, tried4, cool4, coolTried, again4, againTried);
    check(spots > 0 && read1 == spots, "1 it reads a book of another player 0.7 m ahead, within 0.2 s, at every spawn point");
    check(read1 > 0 && still2 == read1 && faced2 == read1 && noShot2 == shotCounted, "2 while the book lies: still, facing it, no shot at the enemy 6 m ahead");
    check(read1 > 0 && moved3 == read1, "3 the book taken away: it moves on (or fights)");
    printf("its readings' length against the read time's %.0f ms: off by %.0f ms at most (%d of %d within 2 frames)\n", kReadFrames * 1000.0 / 59.94, worstMs, timed5, read1);
    check(read1 > 0 && timed5 == read1, "5 a reading lasts the read time at a player's own pace by the machine's clock (the host's frames slowed or not)");
    check(tried4 > 0 && far4 == tried4 && own4 == tried4 && raikov4 == tried4 && refused4 == tried4 && coolTried > 0 && cool4 == coolTried && again4 == againTried,
          "4 none read out of reach, its own, in its cooldown, as Raikov, in a refused state; one read again after its cooldown");
#if defined(VDOLL_HOST_DROPS) && defined(VDOLL_BODY_PICKUP)
    check(gogTried > 0 && gogRead == 0, "6 the thermal goggles on, a book 0.5 m ahead not read");
#endif
    printf("its readings told as a reader's game tells them %d of %d; a copy fed with its records read with it %d of %d (MGO1_VDOLL_BOOK_POSE %s)\n", told7, read1, copy7, read1,
           getenv("MGO1_VDOLL_BOOK_POSE") ? getenv("MGO1_VDOLL_BOOK_POSE") : "unset (on)");
    check(read1 > 0 && told7 == read1 && copy7 == read1, "7 its reading told to the other games as a reader's own game tells it: its copy reads with it and stands up at its end");
    printf("a teammate's book %s, in deathmatch another player's %s\n", mate8 < 0 ? "not tried" : mate8 ? "READ" : "not read", dm8 < 0 ? "not tried" : dm8 ? "READ" : "not read");
    check(mate8 == 1 && dm8 == 1, "8 a teammate's book read as an enemy's (no team, no friendly fire test), and in deathmatch any other player's");
    printf("a flinch while reading: %s, no reading 5 s later %s; its death while reading: %s\n", flinch9 < 0 ? "not tried" : flinch9 ? "ended (the byte 0, no {0E})" : "NOT ended",
           cool9 < 0 ? "not tried" : cool9 ? "yes" : "NO", death9 < 0 ? "not tried" : death9 ? "ended (the byte 0, no {0E})" : "NOT ended");
    check(flinch9 == 1 && cool9 == 1 && death9 == 1, "9 a flinch (a hit's reaction) and its death end a reading for its copies, then its cooldown");
    {
        static const char* const kName[9] = {"KGB", "GRU", "Ocelot Unit", "Sokolov", "Raikov", "Ocelot", "Reiko", "Rowdy", "Snake"};
        std::string l; bool ok = true;
        for (int c = 0; c <= 8; ++c) { l += std::string(c ? ", " : "") + kName[c] + (cls10[c] < 0 ? " not tried" : cls10[c] ? " reads" : " does not read"); ok = ok && cls10[c] == (c == 4 ? 0 : 1); }
        printf("the classes: %s\n", l.c_str());
        check(ok, "10 every class reads a book in reach but Raikov (class 4), as the game's check");
    }
    // ((v150-host beta 25, bookhold) a map with none of a kind the body runs (n024a: none at all): told, not counted. MGO1_VDOLL_BOOK_HOLD=0 and
    // b16's body (beta 22's holds: a climb's 60 frames, the whole crossing) fail it)
    check(ok11[0] == tried11[0] && ok11[1] == tried11[1] && ok11[2] == tried11[2],
          "11 its drops, climbs and fences hold a book back as a player's own game holds it (a climb 54 frames, a landing 18, a fence none)");
    return fails ? 1 : 0;
}
