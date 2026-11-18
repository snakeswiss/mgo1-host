// (v150-host beta 22, wake) A virtual doll wakes a teammate knocked out or held by a book with the game's own blows: MGO1Loader/
// vdoll_wake.h (the game's rule), vdoll_body.cpp WakeFrame, docs/VIRTUAL_DOLL.md 36.
// The game's rule (US n003a, the hit 0x437F20 on the attacker's game): a blow of the combo (0x97, 0x96, the kick 0x95) on a
// teammate lying knocked out sends {04 attacker, 0, -(his gauge's maximum x +0xB74 / 100), kind, 0, 0}: his own game fills his
// gauge at once. Measured on real clients (wake_ts_on2): only the combo pressed RUNNING into him reaches him, and only its kick
// (57 frames after the first press, -15000); its punches pass over him. Circle close to a standing teammate (a reader) is the
// CQC throw (wake_m1 and wake_m4, friendly fire off and on): {04 attacker, 0 0, 1500 (3000 halved), 0xCF, 0, 0}, his fall
// {09 attacker, 9, angle}; his reading ends.
// The test: a map's .geom; the doll at each of the map's spawn points (6 at most), its own ways 2 s, then:
//   1 a teammate (a player) lying knocked out on the floor 5 m ahead (in its sight), his gauge empty: it runs at him and gives the
//     combo running into him: its motion records {08 0A} from 5 frames after its press, the kick 52 frames after them ({14 doll,
//     0x22 0, 4 ..} on his channel 2 and {04 doll, 0 0, -15000, 0x95, 0, 0} on his channel 0), no punch record, no {09}; a
//     second run at him for the rest of his gauge (+0xB74 50: half a kick); his gauge at 20000: one; the test's game makes him
//     get up 6 frames after the last record; no blow on him after that (5 s);
//   2 a teammate (a player) standing and reading a book 4 m ahead: it goes to him and throws him once (CQC): {04 doll, 0 0, 1500,
//     0xCF, 0, 0} and his fall {09 doll, 9, angle} on his channel 1; the test's game ends his reading 6 frames after; no blow on
//     him after that;
//   3 a virtual teammate knocked out lying (vdoll.cpp's Knocked, its gauge 2000) 5 m ahead: the two kicks of 1 on its seat's
//     channel 0; up (Knocked: no longer) 6 frames after: no blow after that;
//   5 a virtual teammate (a second doll) reading a book 4 m ahead (the book 1.1 m beyond it): one CQC throw, its fall, its reading
//     over by the fall (vdoll.cpp Pause, as a player's fall ends his reading), and the waker never within the book's reach (it reads none);
//   4 no blow at all: an enemy in its sight (8 m ahead, else 7, 10, 6 or 3 m) while the teammate lies knocked out (5 s); a virtual teammate knocked out
//     who is up by himself in 0.7 s (its gauge 29000 of 30000); an enemy lying knocked out; a deathmatch (no teammate);
//   6 friendly fire off: a teammate knocked out kicked as with it on; a reader thrown; 7 TEAM SNEAKING: a defender knocked out 60 s ago
//     still helped (the owner's quarter rate), none in a team deathmatch;
//   9 (v150-host beta 23) as 1 with each frame given twice to the body (the host's game passes twice in one frame: dt 0): two kicks
//     still, never two in one frame (wake_b1_on_t16 sent the second kick twice at f16168);
//   8 (v150-host beta 23) a player lying knocked out against a wall, his hips over the floor 0.25 to 0.35 m beyond the edge of the
//     bodies' mesh (which keeps a body's width off every wall), as a playable doll lay in wake_b1_off_t15: two kicks, the second
//     within 400 frames of the first, as in the open (beta 22's run: the line to him cut short, a run-up after each stop).
//  10 (v150-host beta 25, wakeway) a player lying knocked out against a thin wall (the bodies' mesh behind it within 1.5 m of him),
//     on 6 walls a map, each from both faces where it can: the goal of the doll's way (WakeGoal) has the line a blow needs (from a
//     player's hips or knees there to his hips), no blow through the wall from his side, from behind it, round to it, or running
//     at him with its way behind the wall; kicked whenever a player could reach him (MGO1_VDOLL_WAKE_SIDE; =0 and the body before it
//     fail on each of the 12 maps: blows through the wall in the last case, and on n003a his goal behind the wall).
//  11 (v150-host beta 25, wakeramp) a player lying knocked out on a ramp or stairs: the places of the map counted (a 1 m grid on the
//     bodies' mesh, the floor rising 0.3 m or more within 1 m; beta 25's run-up rule, a place 3 m off within 0.4 m of his height,
//     against a player's, up or down the slope); at the 6 places where beta 25's rule fails from the most sides a player has, the
//     doll starting above him and below him on the slope: two kicks whenever a player could reach him, each blow within its reach
//     (StrikeClear's line, 1.1 m, 0.4 m of floor); 4 ledges with him at the foot and the doll on the top: no blow from the top
//     (MGO1_VDOLL_WAKE_SLOPE; =0 and the body before it kick twice at 18 of 92 starts: FAIL on the 10 maps with such places).
// (v150-host beta 23) A teammate set down in its sight holds the waker where it is until its next look (HoldForLook).
// With MGO1_VDOLL_WAKE=0, and on the body before this change (no VDOLL_BODY_WAKE), 1, 2 and 3 fail at every spawn point.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_wake_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_wake_test
//   ./vdoll_wake_test <map.geom>        (VDOLL_TEST_VERBOSE=1: the body's lines; VDOLL_TEST_EVENTS=1: each record)
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
#include <set>
#include <string>
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static doll::Geom gG;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static bool gMeshSaid = false;
static long long gNow = 0;
static int gWakerReads = 0;
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (strstr(b, "vdoll 1: reads a book")) ++gWakerReads;   // (part 5: the waker caught by the book on its way)
    if (getenv("VDOLL_TEST_VERBOSE")) printf("%s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "test"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) { if (!gHave[id]) return false; *s = gPlayers[id]; return true; }
static int gRuleNow = 1;
static int Rule() { return gRuleNow; }
static int gFF = 1;                                      // (the game's friendly fire: parts 1 to 5 on; part 6 off)
static int FriendlyFire() { return gFF; }
// (part 7) TEAM SNEAKING's mission objects: one free object far away and the sneaking team's goal set (gObjSneak: that team;
// -1: none, the other parts)
static int gObjSneak = -1;
static int Objectives(vdoll::ObjNow* out, int max, float goals[2][3], bool set[2]) {
    if (gObjSneak < 0 || max < 1) return 0;
    out[0] = vdoll::ObjNow(); out[0].pos[0] = 900000.f; out[0].pos[1] = 0.f; out[0].pos[2] = 900000.f;
    for (int t = 0; t < 2; ++t) { goals[t][0] = 900000.f; goals[t][1] = 0.f; goals[t][2] = 900000.f; set[t] = t == gObjSneak; }
    return 1;
}
static const float kPi = 3.14159265f;

// (part 5) the test's book list: one book of player 3 a virtual teammate reads (the host's read time kept 480 frames)
struct TBook { uint32_t id; int slot; float pos[3]; long long readAt; };
static std::vector<TBook> gBooks;
#ifdef VDOLL_HOST_BOOKS
static int Books(vdoll::BookNow* out, int max) {
    int n = 0;
    for (const TBook& b : gBooks) { if (n >= max) break; out[n].id = b.id; out[n].slot = b.slot; memcpy(out[n].pos, b.pos, sizeof b.pos); out[n].readTime = 480; ++n; }
    return n;
}
static bool ReadBook(uint32_t id) { for (TBook& b : gBooks) if (b.id == id) { if (b.readAt < 0) b.readAt = gNow; return true; } return false; }
static bool BookDone(uint32_t id) { for (size_t i = 0; i < gBooks.size(); ++i) if (gBooks[i].id == id) { gBooks.erase(gBooks.begin() + (long)i); return true; } return false; }
#endif
// the records the doll sends: to whom, the channel's kind, the record, the frame; (v150-host beta 25, wakeway) the doll's place (hips)
struct Rec { int slot, kind; std::vector<uint8_t> r; long long at; float from[3]; };
static std::vector<Rec> gRecs;
static float gPos[3];
static bool gReader = false;                             // (part 5: the second doll in the test, the reader in seat 4)
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& r) {
    // (part 5) vdoll.cpp's part for the reader: a reaction {09 a, kind, angle} on its channel 1, a flinch (1 to 5) or a fall (9):
    // body::Pause each tick (FlinchTick, DownedTick), which ends its reading as a player's flinch or fall ends his
    if (gReader && slot == 4 && kind == 1 && r.size() >= 3 && r[0] == 0x09 && ((r[2] >= 1 && r[2] <= 5) || r[2] == 9)) vdoll::body::Pause(1);
    gRecs.push_back({slot, kind, r, gNow, {gPos[0], gPos[1], gPos[2]}});
    if (getenv("VDOLL_TEST_EVENTS")) { printf("    f%lld: to %d kind %d:", gNow, slot, kind); for (uint8_t c : r) printf(" %02x", c); printf("\n"); }
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

static uint16_t gFacing = 0;
static vdoll::body::Self gS;
static float FacingDeg() { return gFacing * 360.f / 65536.f; }
static float gPos2[3];
static uint16_t gFacing2 = 0;
static vdoll::body::Self gS2;
static void Send2(void*, int slot, int kind, const std::vector<uint8_t>& r) { (void)slot; (void)kind; (void)r; }
static bool gTwice = false;                              // (part 9: the host's game gives a frame twice, dt 0 the second time)
static void Step() {
    ++gNow;
    for (size_t i = 0; i < gBooks.size();) if (gBooks[i].readAt >= 0 && gNow - gBooks[i].readAt >= 480) gBooks.erase(gBooks.begin() + (long)i); else ++i;
    memcpy(gPlayers[1].pos, gPos, sizeof gPos);
    if (gReader) memcpy(gPlayers[4].pos, gPos2, sizeof gPos2);
    vdoll::body::Frame(gNow);
    gS.tick = gNow % 6 == 0;
    vdoll::body::Alive(gS, gNow);
    if (gTwice) vdoll::body::Alive(gS, gNow);
    if (gReader) { gS2.tick = gNow % 6 == 3; vdoll::body::Alive(gS2, gNow); }
}
// a point on the floor d mm ahead of the doll along its facing; false: no floor there
static bool Ahead(float d, float side, float* out) {
    const float a = (FacingDeg() + side) * kPi / 180.f;
    out[0] = gPos[0] + std::sin(a) * d; out[2] = gPos[2] + std::cos(a) * d;
    out[1] = Floor(out[0], out[2], gPos[1] - 800.f);
    return out[1] > -1e29f;
}
static int gGauge = 0;                                   // (a knocked out player's gauge on his copy)
static void Player(int id, int team, const float* feet, bool lying, bool asleep, bool reading) {
    gHave[id] = true;
    vdoll::PlayerNow& p = gPlayers[id];
    p = vdoll::PlayerNow(); p.team = team; p.object = p.alive = true; p.life = 200; p.cls = 0;
    p.pos[0] = feet[0]; p.pos[1] = feet[1] + (lying ? 300.f : 800.f); p.pos[2] = feet[2];
    p.state = lying ? 8u : 3u; p.asleep = asleep;
#ifdef VDOLL_PLAYERNOW_WAKE
    p.reading = reading; p.stunMax = 30000; p.wakePart = 50; p.stamina = asleep ? gGauge : 30000;   // (+0xB74 50: a real client's)
#else
    (void)reading;
#endif
}
static int16_t Stun(const Rec& r) { return (int16_t)(r.r[4] | (r.r[5] << 8)); }
// the doll's blows (its {04} records of the combo's kinds 0x95 to 0x97 and the CQC throw's 0xCF: not its shots) to that seat since
// index from: their count, the first one's index
static int Blows(int slot, size_t from, size_t* first) {
    int n = 0;
    for (size_t i = from; i < gRecs.size(); ++i)
        if (gRecs[i].slot == slot && gRecs[i].kind == 0 && gRecs[i].r.size() == 9 && gRecs[i].r[0] == 0x04 && ((gRecs[i].r[6] >= 0x95 && gRecs[i].r[6] <= 0x97) || gRecs[i].r[6] == 0xCF)) { if (!n && first) *first = i; ++n; }
    return n;
}
// the waker's count of teammates up or freed after its blows (none on the body before the change)
static int Woken() {
#ifdef VDOLL_BODY_WAKE
    vdoll::body::WakeInfo w;
    vdoll::body::Wake(0, &w);
    return w.woken;
#else
    return 0;
#endif
}
// (v150-host beta 23, wake) a teammate just set down in its sight: the waker kept where it is until its next look at its teammates
// (every 0.5 s) has taken him, 31 frames at most. Run free, its own ways (a book to lay, support's) had taken it out of his sight
// before that look at some spawn points (n022a part 6, n011a part 5, n012a and n013a part 7): a teammate in its sight for less
// than its look's half second is not this test's
static void HoldForLook() {
    float here[3]; memcpy(here, gPos, sizeof here);
    for (int i = 0; i < 31; ++i) {
        Step();
#ifdef VDOLL_BODY_WAKE
        vdoll::body::WakeInfo w;
        if (vdoll::body::Wake(0, &w) && w.to >= 0) break;
#endif
        memcpy(gPos, here, sizeof gPos);
    }
}
static bool Has(int slot, int kind, uint8_t type, size_t from, size_t to) {
    for (size_t i = from; i < to && i < gRecs.size(); ++i) if (gRecs[i].slot == slot && gRecs[i].kind == kind && !gRecs[i].r.empty() && gRecs[i].r[0] == type) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_wake_test <map.geom>\n"); return 2; }
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
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.objectives = Objectives;
#ifdef VDOLL_HOST_BOOKS
    h.books = Books; h.readBook = ReadBook; h.bookDone = BookDone;
#endif
#ifdef VDOLL_HOST_FF
    h.friendlyFire = FriendlyFire;
#endif
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600 && !gMeshSaid; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("the bodies' mesh was not built\n"); return 1; }
    gNow = 600;
#ifdef VDOLL_BODY_WAKE
    const bool body = true;
#else
    const bool body = false;
#endif
    printf("%s: the body's waking %s, MGO1_VDOLL_WAKE %s\n", argv[1], body ? "there" : "NOT there (the body before it)", getenv("MGO1_VDOLL_WAKE") ? getenv("MGO1_VDOLL_WAKE") : "unset (on)");
    gS.k = 0; gS.slot = 1; gS.team = 0; gS.pos = gPos; gS.facing = &gFacing; gS.send = Send;
    gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].cls = 0;
    int spots[4] = {0, 0, 0, 0}, ok1 = 0, ok2 = 0, ok3 = 0, none4 = 0, tried4 = 0;
    long long worstPace = 0;
    auto reset = [&](size_t si) {
        for (int i = 2; i < 16; ++i) gHave[i] = false;
#ifdef VDOLL_BODY_WAKE
        vdoll::body::Knocked(1, 4, false, 30000, 144);
#endif
        gRuleNow = 1;
        gPos[0] = spawns[si]->p[0]; gPos[1] = spawns[si]->p[1] + 800.f; gPos[2] = spawns[si]->p[2];
        gFacing = (uint16_t)(si * 9000);
        gS.life = gNow;
        for (int i = 0; i < 120; ++i) Step();               // (2 s on its ways)
    };
    // a place d mm ahead on the floor in its sight (none: -1)
    auto place = [&](float d, float* p) {
        for (float side : {0.f, 30.f, -30.f, 60.f, -60.f}) {
            if (!Ahead(d, side, p)) continue;
            const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, end[3] = {p[0], p[1] + 350.f, p[2]};
            if (std::fabs(p[1] - (gPos[1] - 800.f)) > 400.f || vdoll::body::SightStopped(eye, end)) continue;
            float len = 0, e[3];
            const float from[3] = {gPos[0], gPos[1] - 800.f, gPos[2]};
            if (!vdoll::body::WayTo(from, p, &len, e) || len > 2.f * d + 2000.f || std::hypot(e[0] - p[0], e[2] - p[2]) > 600.f || std::fabs(e[1] - p[1]) > 300.f) continue;   // (a place a player reaches)
            return true;
        }
        return false;
    };
    for (size_t si = 0; si < spawns.size() && si < 6; ++si) {
        // 1 a player knocked out lying 5 m ahead, his gauge empty (two punches: the combo's first two blows) and at 20000 (one punch)
        for (int g = 0; g < 2; ++g) {
        reset(si);
        float p[3];
        if (place(5000.f, p)) {
            ++spots[0];
            gGauge = g ? 20000 : 0;
            const int want = g ? 1 : 2;
            size_t from = gRecs.size(), first = 0;
            Player(3, 0, p, true, true, false);
            HoldForLook();
            long long upAt = -1;
            for (int i = 0; i < 1500; ++i) {               // (two runs at him with its run-up between: up to 25 s)
                Step();
                if (upAt < 0 && Blows(3, from, &first) >= want) upAt = gNow + 6;
                if (upAt >= 0 && gNow == upAt) Player(3, 0, p, false, false, false);   // (his own game: up)
                if (upAt >= 0 && gNow > upAt + 300) break;
            }
            const int n = Blows(3, from, &first);
            // (the kick of the combo pressed running into him: on a teammate lying only the kick lands, wake_ts_on2; its punches send nothing)
            bool good = n == want && Stun(gRecs[first]) == -15000 && gRecs[first].r[6] == 0x95 && gRecs[first].r[1] == 1;
            long long second = -1;
            if (n >= 2) for (size_t i = first + 1; i < gRecs.size(); ++i)
                if (gRecs[i].slot == 3 && gRecs[i].kind == 0 && gRecs[i].r.size() == 9 && gRecs[i].r[0] == 0x04) { second = gRecs[i].at - gRecs[first].at; good = good && gRecs[i].r[6] == 0x95 && Stun(gRecs[i]) == -15000; break; }
            int punches = 0;
            for (size_t i = from; i < gRecs.size(); ++i) if (gRecs[i].slot == 3 && gRecs[i].kind == 0 && gRecs[i].r.size() == 9 && gRecs[i].r[0] == 0x04 && (gRecs[i].r[6] == 0x96 || gRecs[i].r[6] == 0x97)) ++punches;
            bool react = Has(3, 1, 0x09, from, gRecs.size()), effect = false;
            for (size_t i = from; i <= first && i < gRecs.size(); ++i) if (gRecs[i].slot == 3 && gRecs[i].kind == 2 && gRecs[i].r.size() >= 5 && gRecs[i].r[0] == 0x14 && gRecs[i].r[2] == 0x22 && gRecs[i].r[4] == 0x04) effect = true;
            long long press = -1; int motions = 0;            // (its motion records {08 0A, 11 zeros} each tick of the combo, as measured)
            for (size_t i = from; i < gRecs.size(); ++i) if (gRecs[i].slot == 1 && gRecs[i].kind == 1 && gRecs[i].r.size() == 13 && gRecs[i].r[0] == 0x08 && gRecs[i].r[1] == 0x0A) { if (press < 0) press = gRecs[i].at; ++motions; }
            const long long pace = n && press >= 0 ? gRecs[first].at - press : -1;   // (its first motion record 5 to 10 frames after the press: the kick 47 to 52 after it)
            const float d = std::hypot(gPos[0] - p[0], gPos[2] - p[2]);
            if (n) worstPace = std::max(worstPace, pace);
            // (a second combo: a new run at him, within 400 frames: its run-up 3 m back and in again, 196 to 212 frames on the 12 maps;
            // (v150-host beta 23) 1168 when its straight way back was cut and it gave him up 10 s, never when its float steps stalled)
            good = good && punches == 0 && !react && effect && motions >= 1 && pace >= 46 && pace <= 53 && (want == 1 || (second > 117 && second <= 400));
            if (good) ++ok1;
            printf("  spawn %zu, 1 (a player knocked out 5 m ahead, his gauge %d): %d kicks%s, %d punch records, %d motion records, the first kick %lld frames after the first of them%s, %s, %s; %.2f m from him at the end\n", si, gGauge, n,
                   n ? (Stun(gRecs[first]) == -15000 ? " (the first: the stun -15000, half his gauge)" : " (the first: NOT the wake's stun)") : "", punches, motions, pace,
                   second >= 0 ? (" and the second " + std::to_string(second) + " frames later (a second run)").c_str() : "",
                   react ? "a REACTION sent" : "no reaction", effect ? "its effect (0x22, part 4) sent" : "NO kick's effect", d / 1000.f);
        }
        }
        float p[3];
        // 2 a player reading 4 m ahead: the CQC throw once (friendly fire on as off: wake_m4)
        reset(si);
        if (place(4000.f, p)) {
            ++spots[1];
            size_t from = gRecs.size(), first = 0;
            Player(3, 0, p, false, false, true);
            HoldForLook();
            long long endAt = -1;
            for (int i = 0; i < 900; ++i) {
                Step();
                if (endAt < 0 && Has(3, 1, 0x09, from, gRecs.size())) endAt = gNow + 6;
                if (endAt >= 0 && gNow == endAt) Player(3, 0, p, false, false, false);   // (his fall: his reading over)
                if (endAt >= 0 && gNow > endAt + 300) break;
            }
            const int n = Blows(3, from, &first);
            bool fall = Has(3, 1, 0x09, from, gRecs.size());
            for (size_t k = from; k < gRecs.size(); ++k) if (gRecs[k].slot == 3 && gRecs[k].kind == 1 && gRecs[k].r.size() >= 3 && gRecs[k].r[0] == 0x09 && gRecs[k].r[2] != 9) fall = false;
            const bool good = n == 1 && Stun(gRecs[first]) == 1500 && gRecs[first].r[6] == 0xCF && fall;
            if (good) ++ok2;
            printf("  spawn %zu, 2 (a player reading 4 m ahead): %d blows%s, %s\n", si, n, n ? (Stun(gRecs[first]) == 1500 && gRecs[first].r[6] == 0xCF ? " (the CQC throw: 1500, 0xCF)" : " (another blow)") : "",
                   fall ? "his fall (9) sent" : "NO fall");
        }
        // 3 a virtual teammate knocked out lying 5 m ahead
        reset(si);
        if (place(5000.f, p)) {
            ++spots[2];
            size_t from = gRecs.size(), first = 0;
            Player(4, 0, p, true, false, false);
#ifdef VDOLL_BODY_WAKE
            vdoll::body::Knocked(1, 4, true, 2000, 144);
#endif
            HoldForLook();
            long long upAt = -1;
            for (int i = 0; i < 1500; ++i) {
                Step();
                if (upAt < 0 && Blows(4, from, &first) >= 2) upAt = gNow + 6;
#ifdef VDOLL_BODY_WAKE
                if (upAt >= 0 && gNow == upAt) { vdoll::body::Knocked(1, 4, false, 30000, 144); Player(4, 0, p, false, false, false); }
#endif
                if (upAt >= 0 && gNow > upAt + 300) break;
            }
            const int n = Blows(4, from, &first);
            if (n == 2 && Stun(gRecs[first]) == -15000) ++ok3;
            printf("  spawn %zu, 3 (a virtual doll knocked out 5 m ahead, its gauge 2000): %d blows%s\n", si, n, n ? (Stun(gRecs[first]) == -15000 ? " (the first: the stun -15000)" : " (another stun)") : "");
        }
        // 4 none: an enemy in sight; up by himself soon; an enemy knocked out; a deathmatch
        reset(si);
        float e[3];
        bool enemyAt = false;                              // (the enemy in its sight: 8 m ahead, else 7, 10, 6, 3 m)
        for (float de : {8000.f, 7000.f, 10000.f, 6000.f, 3000.f}) if (!enemyAt && place(de, e)) enemyAt = true;
        if (place(5000.f, p) && enemyAt) {
            ++spots[3];
            int blows = 0;
            size_t from = gRecs.size();
            Player(3, 0, p, true, true, false); Player(2, 1, e, false, false, false);
            for (int i = 0; i < 300; ++i) Step();
            blows += Blows(3, from, nullptr);
            gHave[2] = false; gHave[3] = false;
            reset(si);
            from = gRecs.size();
            Player(4, 0, p, true, false, false);
#ifdef VDOLL_BODY_WAKE
            vdoll::body::Knocked(1, 4, true, 29000, 144);
#endif
            for (int i = 0; i < 300; ++i) Step();
            blows += Blows(4, from, nullptr);
            reset(si);
            from = gRecs.size();
            Player(5, 1, p, true, true, false);
            for (int i = 0; i < 300; ++i) Step();
            blows += Blows(5, from, nullptr);
            reset(si);
            gRuleNow = 0;
            from = gRecs.size();
            Player(3, 0, p, true, true, false);
            for (int i = 0; i < 300; ++i) Step();
            blows += Blows(3, from, nullptr);
            gRuleNow = 1;
            ++tried4;
            if (blows == 0) ++none4;
            printf("  spawn %zu, 4: %d blows where none is due\n", si, blows);
        }
    }
    // 5 a virtual teammate (a second doll, seat 4) reading a book of player 3 4 m ahead: the waker throws it (CQC), its fall ends its
    // reading (vdoll.cpp's Pause), no blow after that
    int spots5 = 0, ok5 = 0;
#ifdef VDOLL_HOST_BOOKS
    for (size_t si = 0; si < spawns.size() && si < 6; ++si) {
        reset(si);
        float p[3], bk[3];
        if (!place(4000.f, p)) continue;
        gPos2[0] = p[0]; gPos2[1] = p[1] + 800.f; gPos2[2] = p[2];
        gFacing2 = gFacing;
        gS2.k = 1; gS2.slot = 4; gS2.team = 0; gS2.pos = gPos2; gS2.facing = &gFacing2; gS2.send = Send2; gS2.life = gNow;
        Player(4, 0, p, false, false, false);
        gReader = true;
        float here[3]; memcpy(here, gPos, sizeof here);
        for (int i = 0; i < 480; ++i) { Step(); memcpy(gPos2, p, sizeof gPos2); gPos2[1] += 800.f; memcpy(gPos, here, sizeof gPos); }   // (its spawn's cooldown; both kept there)
        // (its book 1.1 m beyond it, away from the waker: a player who walks into a book's reach stops about 1.27 m from it)
        const float a = std::atan2(gPos2[0] - gPos[0], gPos2[2] - gPos[2]);
        bk[0] = gPos2[0] + std::sin(a) * 1100.f; bk[2] = gPos2[2] + std::cos(a) * 1100.f; bk[1] = Floor(bk[0], bk[2], gPos2[1] - 800.f);
        const int w0 = Woken();
        size_t from = gRecs.size(), first = 0;
        bool read = false;
        if (bk[1] > -1e29f) {
            gBooks.push_back({(3u << 10) | (uint32_t)(0x200 + si), 3, {bk[0], bk[1], bk[2]}, -1});
            for (int i = 0; i < 30 && !read; ++i) { Step(); for (const TBook& b : gBooks) if (b.readAt >= 0) read = true; }
        }
        if (read) {
            ++spots5;
            HoldForLook();
            const int r0 = gWakerReads;
            int rFreed = -1;                               // (the waker's readings up to the freeing: its approach; its ordinary way after it is not this test's)
            int w1 = w0;
            for (int i = 0; i < 480; ++i) { Step(); w1 = Woken(); if (rFreed < 0 && w1 > w0) rFreed = gWakerReads; }
            if (rFreed < 0) rFreed = gWakerReads;
            const int n = Blows(4, from, &first);
            if (n == 1 && Stun(gRecs[first]) == 1500 && gRecs[first].r[6] == 0xCF && Has(4, 1, 0x09, from, gRecs.size()) && w1 == w0 + 1 && rFreed == r0) ++ok5;
            printf("  spawn %zu, 5 (a virtual doll reading 4 m ahead): %d blows%s, %s, %s, %s\n", si, n, n ? (Stun(gRecs[first]) == 1500 && gRecs[first].r[6] == 0xCF ? " (the CQC throw: 1500, 0xCF)" : " (another blow)") : "",
                   Has(4, 1, 0x09, from, gRecs.size()) ? "its fall sent" : "NO fall", w1 == w0 + 1 ? "freed (it reads no more)" : "NOT freed",
                   rFreed == r0 ? "the waker out of the book's reach" : "the WAKER READ the book on its way");
        }
        gReader = false; gBooks.clear(); gHave[4] = false;
        vdoll::body::Down(1);
    }
#endif
    // 6 friendly fire OFF: a player knocked out 5 m ahead, the combo's kick as with it on (measured, wake_ts_off: the ally wake is
    // not friendly fire's); a player reading 4 m ahead from his
    // reading's start, the CQC throw: {04 doll, 0 0, 1500, 0xCF, 0, 0}, {09 doll, 9, angle}, {14 doll, 20 02 0B ..}, 45 frames after
    // its first motion record at the latest, once; a reader 6.7 s into his reading (1.3 s left), no enemy: no throw
    int spots6 = 0, ok6 = 0;
#ifdef VDOLL_HOST_FF
    gFF = 0;
    for (size_t si = 0; si < spawns.size() && si < 6; ++si) {
        float p[3];
        reset(si);
        if (!place(5000.f, p)) continue;
        ++spots6;
        size_t from = gRecs.size(), first = 0;
        gGauge = 0;
        Player(3, 0, p, true, true, false);
        HoldForLook();
        for (int i = 0; i < 900; ++i) Step();
        const int koBlows = Blows(3, from, nullptr);
        gHave[3] = false;
        reset(si);
        bool thrown = false, react9 = false, effect = false, placed = place(4000.f, p);
        int throws = 0;
        if (placed) {
            from = gRecs.size();
            Player(3, 0, p, false, false, true);
            HoldForLook();
            long long endAt = -1;
            for (int i = 0; i < 600; ++i) {
                Step();
                for (size_t k = from; k < gRecs.size(); ++k) if (gRecs[k].slot == 3 && gRecs[k].kind == 0 && gRecs[k].r.size() == 9 && gRecs[k].r[6] == 0xCF) { if (!thrown) first = k; thrown = true; }
                if (thrown && endAt < 0) endAt = gNow + 6;
                if (endAt >= 0 && gNow == endAt) Player(3, 0, p, false, false, false);
            }
            for (size_t k = from; k < gRecs.size(); ++k) if (gRecs[k].slot == 3 && gRecs[k].kind == 0 && gRecs[k].r.size() == 9 && gRecs[k].r[6] == 0xCF) ++throws;
            react9 = Has(3, 1, 0x09, from, gRecs.size());
            for (size_t k = from; k < gRecs.size(); ++k) if (gRecs[k].slot == 3 && gRecs[k].kind == 1 && gRecs[k].r.size() >= 3 && gRecs[k].r[0] == 0x09 && gRecs[k].r[2] != 9) react9 = false;
            effect = Has(3, 2, 0x14, from, gRecs.size());
        }
        gHave[3] = false;
        reset(si);
        int lateThrows = 0;
        if (placed && place(4000.f, p)) {               // (6.7 s into his reading: 1.3 s left, less than its way and 2 s, no enemy)
            from = gRecs.size();
            Player(3, 0, p, false, false, true);
            gPlayers[3].pos[0] += 100000.f;                // (reading 400 frames out of its sight, 100 m away: the host's copy tells his reading's start)
            for (int i = 0; i < 400; ++i) Step();
            gPlayers[3].pos[0] -= 100000.f;
            for (int i = 0; i < 300; ++i) Step();
            for (size_t k = from; k < gRecs.size(); ++k) if (gRecs[k].slot == 3 && gRecs[k].kind == 0 && gRecs[k].r.size() == 9 && gRecs[k].r[6] == 0xCF) ++lateThrows;
        }
        gHave[3] = false;
        if (!placed) { --spots6; continue; }            // (no reader's place 4 m ahead from there: not counted)
        const bool good = koBlows >= 1 && placed && throws == 1 && Stun(gRecs[first]) == 1500 && react9 && effect && lateThrows == 0;
        if (good) ++ok6;
        printf("  spawn %zu, 6 (friendly fire off): %d kicks on the player knocked out; the reader %s%s, %s, %s; %d throws on a reader 1.3 s from his reading's end\n", si, koBlows,
               !placed ? "not placed" : thrown ? "THROWN" : "not thrown", thrown ? (Stun(gRecs[first]) == 1500 ? " (the stun 1500, 0xCF)" : " (another stun)") : "",
               react9 ? "his fall (9) sent" : "NO fall", effect ? "its effect sent" : "NO effect", lateThrows);
    }
    gFF = 1;
#endif
    // 7 TEAM SNEAKING (rule 5, team 1 sneaking: the doll and its teammate defend): a player lying knocked out 60 s ago, his gauge
    // empty, comes into its sight: a defender's own recovery is a quarter of the game's (tsne.h TsneWakeTick, about 167 s
    // untouched): the running combo's kick; the same in a team deathmatch: no blow (up by himself 42 s after his knock out at the latest)
    int spots7 = 0, ok7 = 0;
    for (size_t si = 0; si < spawns.size() && si < 6; ++si) {
        int blows7[2] = {0, 0};
        bool placed = true;
        for (int pass = 0; pass < 2 && placed; ++pass) {
            reset(si);
            float p[3];
            if (!place(5000.f, p)) { placed = false; break; }
            gRuleNow = pass == 0 ? 1 : 5; gObjSneak = pass == 0 ? -1 : 1;
            gGauge = 0;
            const size_t from = gRecs.size();
            Player(3, 0, p, true, true, false);
            gPlayers[3].pos[0] += 100000.f;                // (knocked out 60 s out of its sight, 100 m away: the host's copy tells it)
            float here[3]; memcpy(here, gPos, sizeof here);
            for (int i = 0; i < 3600; ++i) { Step(); memcpy(gPos, here, sizeof gPos); }   // (the doll kept where it was)
            gPlayers[3].pos[0] -= 100000.f;
            HoldForLook();
            for (int i = 0; i < 1200; ++i) Step();
            blows7[pass] = Blows(3, from, nullptr);
            gHave[3] = false; gRuleNow = 1; gObjSneak = -1;
        }
        if (!placed) continue;
        ++spots7;
        if (blows7[0] == 0 && blows7[1] >= 1) ++ok7;
        printf("  spawn %zu, 7 (a player knocked out 60 s ago comes into its sight): %d blows in a team deathmatch, %d in TEAM SNEAKING (a defender)\n", si, blows7[0], blows7[1]);
    }
    // 8 (v150-host beta 23) a player lying knocked out against a wall: his hips 0.25 to 0.35 m beyond the mesh's edge (the floor goes on
    // there, within 150 mm of his height; the mesh 150 mm away at least), in its sight; his gauge empty: two kicks, as in part 1
    int spots8 = 0, ok8 = 0;
    doll::NavMesh nav;
    std::string nlog;
    const bool navOk = nav.Build(gG, doll::NavConfig(), nlog);
    for (size_t si = 0; si < spawns.size() && si < 6 && navOk; ++si) {
        reset(si);
        float p[3], q[3], m[3];
        if (!place(5000.f, p) || !nav.Nearest(p, 300.f, m)) continue;
        bool found = false;
        for (int k = 0; k < 16 && !found; ++k) {
            const float a = (float)k * kPi / 8.f, c = nav.ClearAlong(m, std::sin(a), std::cos(a), 4000.f, true);
            if (c < 100.f || c > 3900.f) continue;
            for (float beyond : {350.f, 300.f, 250.f}) {   // (the mesh keeps 375 mm off a wall: his hips 25 to 125 mm from it)
                q[0] = m[0] + std::sin(a) * (c + beyond); q[2] = m[2] + std::cos(a) * (c + beyond); q[1] = Floor(q[0], q[2], p[1]);
                float on[3];
                if (q[1] < -1e29f || std::fabs(q[1] - p[1]) > 150.f || nav.Nearest(q, 150.f, on)) continue;
                const float eye[3] = {gPos[0], gPos[1] + 750.f, gPos[2]}, end[3] = {q[0], q[1] + 350.f, q[2]};
                if (vdoll::body::SightStopped(eye, end) || std::hypot(q[0] - gPos[0], q[2] - gPos[2]) > 8000.f) continue;
                found = true;
                break;
            }
        }
        if (!found) continue;
        ++spots8;
        gGauge = 0;
        size_t from = gRecs.size(), first = 0;
        const long long setAt = gNow;
        Player(3, 0, q, true, true, false);
        HoldForLook();
        long long upAt = -1;
        for (int i = 0; i < 1500; ++i) {
            Step();
            if (upAt < 0 && Blows(3, from, &first) >= 2) upAt = gNow + 6;
            if (upAt >= 0 && gNow == upAt) Player(3, 0, q, false, false, false);
            if (upAt >= 0 && gNow > upAt + 300) break;
        }
        const int n = Blows(3, from, &first);
        long long kick1 = -1, kick2 = -1;
        for (size_t i = from; i < gRecs.size(); ++i)
            if (gRecs[i].slot == 3 && gRecs[i].kind == 0 && gRecs[i].r.size() == 9 && gRecs[i].r[0] == 0x04 && gRecs[i].r[6] == 0x95 && Stun(gRecs[i]) == -15000) { if (kick1 < 0) kick1 = gRecs[i].at; else if (kick2 < 0) kick2 = gRecs[i].at; }
        const float off = std::hypot(m[0] - q[0], m[2] - q[2]);
        if (n == 2 && kick1 >= 0 && kick2 > kick1 && kick2 - kick1 <= 400) ++ok8;
        printf("  spawn %zu, 8 (a player knocked out against a wall, %.2f m from the place ahead): %d kicks%s, the first %lld frames after he was set down\n", si, off / 1000.f, n,
               kick2 >= 0 ? (", the second " + std::to_string(kick2 - kick1) + " frames after the first").c_str() : "", kick1 >= 0 ? kick1 - setAt : -1);
        gHave[3] = false;
    }
    // 9 (v150-host beta 23) part 1's first case with every frame given twice to the body: two kicks, never two in one frame
    int spots9 = 0, ok9 = 0;
    for (size_t si = 0; si < spawns.size() && si < 3; ++si) {
        reset(si);
        float p[3];
        if (!place(5000.f, p)) continue;
        ++spots9;
        gGauge = 0;
        gTwice = true;
        size_t from = gRecs.size(), first = 0;
        Player(3, 0, p, true, true, false);
        HoldForLook();
        long long upAt = -1;
        for (int i = 0; i < 1500; ++i) {
            Step();
            if (upAt < 0 && Blows(3, from, &first) >= 2) upAt = gNow + 6;
            if (upAt >= 0 && gNow == upAt) Player(3, 0, p, false, false, false);
            if (upAt >= 0 && gNow > upAt + 300) break;
        }
        gTwice = false;
        int n = 0, same = 0;
        long long last = -1;
        for (size_t i = from; i < gRecs.size(); ++i)
            if (gRecs[i].slot == 3 && gRecs[i].kind == 0 && gRecs[i].r.size() == 9 && gRecs[i].r[0] == 0x04 && gRecs[i].r[6] == 0x95) { ++n; if (gRecs[i].at == last) ++same; last = gRecs[i].at; }
        if (n == 2 && same == 0) ++ok9;
        printf("  spawn %zu, 9 (a player knocked out 5 m ahead, each frame given twice): %d kicks, %d of them in the frame of the one before\n", si, n, same);
        gHave[3] = false;
    }
    // 10 (v150-host beta 25, wakeway) walls, both faces: places where a player lies against a wall (his feet 0.25 to 0.35 m beyond
    // the edge of the bodies' mesh, his floor going on there, the wall within 0.45 m beyond him at the hips' and the knees' height)
    // and the bodies' mesh goes on behind the wall within 1.5 m of him (a thin wall: its far side within a press of him). Every
    // mesh point near a wall is tried until 6 walls 3 m apart are found, each from its other face too where that face has such a
    // place within 2 m. At each place: (a) the goal of the way the doll would take to him (WakeGoal) from its places of (b) and
    // (c) has the line a blow needs (strike); (b) the doll in its sight 3 to 7 m away on his side: no blow from a place without
    // that line, and he is kicked when a player could (a place on his side within 0.88 m of his feet with that line and a way of
    // 30 m at most to it); (b2) the same from the place on his side in its sight whose way round to behind the wall is the
    // shortest (28 m at most: beta 25's goal behind the wall within its 30 m); (c) the doll on the wall's far side 1 to 2 m from it:
    // no blow through the wall (round to his side and from there is fine); (d) as (b), and once it runs at him (20 frames at a
    // run's pace, nearer each frame, 1.5 m away at least) its place moved to the mesh's point behind the wall nearest him, still
    // running, as a way round the wall's end close to him takes it there: no blow through the wall (beta 23 to 25's run at him
    // asked the mesh's line with 0.9 m of slack, its blow the distance alone)
    int spots10 = 0, ok10 = 0, goals10 = 0, badGoals10 = 0, through10 = 0, could10 = 0, kicked10 = 0, far10 = 0, round10 = 0, run10 = 0, faces10[2] = {0, 0};
    doll::NavMesh nav10;
    std::string nlog10;
    doll::NavConfig cfg10;
#ifdef DOLL_NAV_DOORWAYS
    cfg10.doorways = true;                               // (the bodies' own mesh: vdoll_body.cpp Build with MGO1_VDOLL_STAIRS)
#endif
#ifdef DOLL_NAV_MARGIN
    { const char* mg = getenv("MGO1_VDOLL_MESH_MARGIN"); cfg10.margin = !(mg && !strcmp(mg, "0")); }   // ((v150-host beta 25, meshmargin) as the bodies')
#endif
    struct WallSpot { float him[3], near[3], far[3], dir[2]; };
    std::vector<WallSpot> walls;
    // the line a blow needs (vdoll_body.cpp StrikeClear): a pair of the striker's hips or knees (his feet + 0.8 m, + 0.4 m) and the
    // lying man's hips or 0.15 m over them clear on the stage's collision
    auto strike = [&](const float* feet, const float* hips) {
        const float a[2][3] = {{feet[0], feet[1] + 800.f, feet[2]}, {feet[0], feet[1] + 400.f, feet[2]}};
        const float v[2][3] = {{hips[0], hips[1], hips[2]}, {hips[0], hips[1] + 150.f, hips[2]}};
        for (const auto& x : a) for (const auto& y : v) if (!vdoll::body::SightStopped(x, y)) return true;
        return false;
    };
    // a place against a wall from mesh point m along (dx, dz): the mesh's edge within 2.9 m, his feet 0.25 to 0.35 m beyond it off
    // the mesh on his floor, the wall within 0.45 m beyond him at both heights, the line from the edge to him, the mesh behind the
    // wall within 1.5 m of him with no line from there to him
    // (across: beta 25's goal, the mesh's nearest point to his feet within 1.5 m, behind the wall: the place kept when one of the
    // three is so)
    auto trySpot = [&](const float* m, float dx, float dz, WallSpot& w, int* across) {
        const float cl = nav10.ClearAlong(m, dx, dz, 3000.f, true);
        if (cl >= 2900.f) return false;
        const float e[3] = {m[0] + dx * cl, m[1], m[2] + dz * cl};
        bool found = false;
        *across = 0;
        for (float beyond : {300.f, 250.f, 350.f}) {
            float q[3] = {e[0] + dx * beyond, 0.f, e[2] + dz * beyond}, on[3];
            q[1] = Floor(q[0], q[2], m[1]);
            if (q[1] < -1e29f || std::fabs(q[1] - m[1]) > 150.f || nav10.Nearest(q, 150.f, on)) continue;
            const float hips[3] = {q[0], q[1] + 300.f, q[2]};
            bool wall = true;
            for (float up : {800.f, 400.f}) {
                const float x[3] = {e[0], m[1] + up, e[2]}, y[3] = {q[0] + dx * 450.f, q[1] + up, q[2] + dz * 450.f};
                if (!vdoll::body::SightStopped(x, y)) wall = false;
            }
            if (!wall || !strike(e, hips)) continue;
            for (float fd = 200.f; fd <= 1500.f; fd += 100.f) {
                const float f[3] = {q[0] + dx * fd, q[1], q[2] + dz * fd};
                float fm[3], n[3];
                if (!nav10.Nearest(f, 250.f, fm) || std::fabs(fm[1] - q[1]) > 400.f || std::hypot(fm[0] - q[0], fm[2] - q[2]) > 1500.f || strike(fm, hips)) continue;
                const int ac = nav10.Nearest(q, 1500.f, n) && !strike(n, hips) ? 1 : 0;
                if (!found || ac > *across) {
                    memcpy(w.him, q, sizeof q); memcpy(w.near, e, sizeof e); memcpy(w.far, fm, sizeof fm); w.dir[0] = dx; w.dir[1] = dz;
                    found = true; *across = ac;
                }
                break;
            }
            if (*across) break;
        }
        return found;
    };
    int across10 = 0;                                    // (the places where beta 25's goal, the mesh's nearest point to his feet, is behind the wall)
    if (navOk && nav10.Build(gG, cfg10, nlog10)) {
        std::vector<float> tris;
        std::vector<uint8_t> areas;
        nav10.Triangles(tris, areas);
        std::vector<long long> cells;
        std::vector<std::pair<int, WallSpot>> cands;     // (1: beta 25's goal behind the wall; every place found, one a 2 m cell)
        for (size_t t = 0; t + 8 < tris.size(); t += 9) {
            const float c[3] = {(tris[t] + tris[t + 3] + tris[t + 6]) / 3.f, (tris[t + 1] + tris[t + 4] + tris[t + 7]) / 3.f, (tris[t + 2] + tris[t + 5] + tris[t + 8]) / 3.f};
            const long long cell = ((long long)std::floor(c[0] / 2000.f) * 4096 + (long long)std::floor(c[2] / 2000.f)) * 64 + (long long)std::floor(c[1] / 1000.f);
            if (std::find(cells.begin(), cells.end(), cell) != cells.end()) continue;
            cells.push_back(cell);
            float m[3];
            if (!nav10.Nearest(c, 200.f, m)) continue;
            for (int k = 0; k < 16; ++k) {
                const float a = (float)k * kPi / 8.f;
                WallSpot w;
                int across = 0;
                if (!trySpot(m, std::sin(a), std::cos(a), w, &across)) continue;
                cands.push_back({across, w});
                break;
            }
        }
        std::stable_sort(cands.begin(), cands.end(), [](const std::pair<int, WallSpot>& x, const std::pair<int, WallSpot>& y) { return x.first > y.first; });
        int pairs = 0;
        for (const auto& cw : cands) {
            if (pairs >= 6) break;
            const WallSpot& w = cw.second;
            bool apart = true;
            for (const WallSpot& o : walls) if (std::hypot(o.him[0] - w.him[0], o.him[2] - w.him[2]) < 3000.f && std::fabs(o.him[1] - w.him[1]) < 1000.f) apart = false;
            if (!apart) continue;
            // (the wall's other face: from the mesh behind it back toward the wall, a place within 2 m of the first)
            WallSpot v;
            int vac = 0;
            const bool other = trySpot(w.far, -w.dir[0], -w.dir[1], v, &vac) && std::hypot(v.him[0] - w.him[0], v.him[2] - w.him[2]) <= 2000.f;
            walls.push_back(w); ++faces10[0]; across10 += cw.first;
            if (other) { walls.push_back(v); ++faces10[1]; across10 += vac; }
            ++pairs;
        }
        printf("10: %zu places against a thin wall found (one a 2 m cell), %d of them with beta 25's goal behind the wall\n", cands.size(),
               (int)std::count_if(cands.begin(), cands.end(), [](const std::pair<int, WallSpot>& x) { return x.first == 1; }));
    }
    // its place for (b) and (c): hips held there 2 s (a new life), turned toward him
    auto resetAt = [&](const float* feet, const float* him) {
        for (int i = 2; i < 16; ++i) gHave[i] = false;
#ifdef VDOLL_BODY_WAKE
        vdoll::body::Knocked(1, 4, false, 30000, 144);
#endif
        gRuleNow = 1;
        gPos[0] = feet[0]; gPos[1] = feet[1] + 800.f; gPos[2] = feet[2];
        gFacing = (uint16_t)(int)(std::atan2(him[0] - feet[0], him[2] - feet[2]) / (2.f * kPi) * 65536.f);
        gS.life = gNow;
        float here[3]; memcpy(here, gPos, sizeof here);
        for (int i = 0; i < 120; ++i) { Step(); memcpy(gPos, here, sizeof gPos); }
    };
    for (size_t wi = 0; wi < walls.size(); ++wi) {
        const WallSpot& w = walls[wi];
        const float hips[3] = {w.him[0], w.him[1] + 300.f, w.him[2]};
        float nearAt[3], farAt[3];
        bool nearOk = false, farOk = false;
        for (float d : {3000.f, 5000.f, 7000.f, 2000.f, 4000.f, 6000.f, 9000.f, 11000.f, 14000.f})   // (b): on his side, in its sight, a way to the wall's foot
            for (float da : {0.f, 0.6f, -0.6f, 1.2f, -1.2f, 1.57f, -1.57f}) {
                if (nearOk) break;
                const float a = std::atan2(-w.dir[0], -w.dir[1]) + da, p[3] = {w.near[0] + std::sin(a) * d, w.near[1], w.near[2] + std::cos(a) * d};
                float pm[3], len = 0, end[3];
                if (!nav10.Nearest(p, 300.f, pm) || std::fabs(pm[1] - w.near[1]) > 400.f) continue;
                const float eye[3] = {pm[0], pm[1] + 1550.f, pm[2]}, chest[3] = {w.him[0], w.him[1] + 350.f, w.him[2]};
                if (vdoll::body::SightStopped(eye, chest) || !strike(pm, hips) || !vdoll::body::WayTo(pm, w.near, &len, end) || len > 2.f * d + 2000.f) continue;
                memcpy(nearAt, pm, sizeof pm); nearOk = true;
            }
        for (float d : {1000.f, 2000.f, 0.f}) {          // (c): behind the wall
            if (farOk) break;
            const float p[3] = {w.far[0] + w.dir[0] * d, w.far[1], w.far[2] + w.dir[1] * d};
            float pm[3];
            if (!nav10.Nearest(p, 300.f, pm) || std::fabs(pm[1] - w.him[1]) > 400.f || strike(pm, hips)) continue;
            memcpy(farAt, pm, sizeof pm); farOk = true;
        }
        if (!nearOk) {                                  // ((b) needs it: no place on his side in its sight)
            printf("  wall %zu at %.0f %.0f %.0f (the far side's mesh %.2f m from him): no place on his side in its sight 2 to 14 m from the wall's foot: not tried\n", wi, w.him[0], w.him[1], w.him[2],
                   std::hypot(w.far[0] - w.him[0], w.far[2] - w.him[2]) / 1000.f);
            continue;
        }
        ++spots10;
        // (b2) the doll in its sight on his side, 3 to 14 m from him, where its way round to the mesh behind the wall is the shortest
        // (28 m at most): beta 25's goal behind the wall is then within its 30 m
        float roundAt[3];
        float roundLen = 28000.f;
        bool roundOk = false;
        for (float d = 3000.f; d <= 14000.f; d += 1000.f)
            for (int k = 0; k < 16; ++k) {
                const float a = (float)k * kPi / 8.f, p[3] = {w.him[0] + std::sin(a) * d, w.him[1], w.him[2] + std::cos(a) * d};
                float pm[3], len = 0, end[3];
                if (!nav10.Nearest(p, 300.f, pm) || std::fabs(pm[1] - w.him[1]) > 400.f) continue;
                const float eye[3] = {pm[0], pm[1] + 1550.f, pm[2]}, chest[3] = {w.him[0], w.him[1] + 350.f, w.him[2]};
                if (vdoll::body::SightStopped(eye, chest) || !vdoll::body::WayTo(pm, w.far, &len, end) || len >= roundLen || std::hypot(end[0] - w.far[0], end[2] - w.far[2]) > 300.f) continue;
                memcpy(roundAt, pm, sizeof pm); roundLen = len; roundOk = true;
            }
        // (a player could: a place on his side within 0.88 m of his feet with the line, a way of 30 m at most from the doll's place)
        bool could = false;
        for (int r = -1; r < 3 && !could; ++r)
            for (int k = 0; k < (r < 0 ? 1 : 16) && !could; ++k) {
                float m[3], len = 0, end[3];
                const float a = (float)k * kPi / 8.f, rr = r == 0 ? 450.f : r == 1 ? 650.f : 850.f;
                const float c[3] = {w.him[0] + (r < 0 ? 0.f : std::sin(a) * rr), w.him[1], w.him[2] + (r < 0 ? 0.f : std::cos(a) * rr)};
                if (!nav10.Nearest(c, r < 0 ? 1500.f : 250.f, m) || std::fabs(m[1] - w.him[1]) > 400.f || std::hypot(m[0] - w.him[0], m[2] - w.him[2]) > 880.f || !strike(m, hips)) continue;
                if (vdoll::body::WayTo(nearAt, m, &len, end) && len <= 30000.f && std::hypot(end[0] - m[0], end[2] - m[2]) <= 300.f) could = true;
            }
        if (could) ++could10;
        // (a) the goal from both places
        int badGoal = 0, goals = 0;
        std::string goalSay;
#ifdef VDOLL_BODY_WAKE_SIDE
        for (int side = 0; side < 2; ++side) {
            if (side == 1 && !farOk) continue;
            resetAt(side ? farAt : nearAt, w.him);
            gGauge = 0;
            Player(3, 0, w.him, true, true, false);
            float here[3]; memcpy(here, gPos, sizeof here);
            for (int i = 0; i < 7; ++i) { Step(); memcpy(gPos, here, sizeof gPos); }   // (the players are read every 6 frames)
            float goal[3], len = 0;
            if (!vdoll::body::WakeGoal(gS, 3, goal, &len)) { goalSay += side ? " far: no goal" : " near: no goal"; gHave[3] = false; continue; }
            ++goals;
            const bool good = strike(goal, hips);
            if (!good) ++badGoal;
            char g[160];
            snprintf(g, sizeof g, " %s: goal %.2f m from him%s, way %.1f m", side ? "far" : "near", std::hypot(goal[0] - w.him[0], goal[2] - w.him[2]) / 1000.f, good ? "" : " BEYOND THE WALL", len / 1000.f);
            goalSay += g;
            gHave[3] = false;
        }
#else
        goalSay = " (no WakeGoal in this body)";
#endif
        goals10 += goals; badGoals10 += badGoal;
        // (b) and (c): the doll's blows
        int kicks[4] = {0, 0, 0, 0}, thru[4] = {0, 0, 0, 0};
        bool jumped = false;
        for (int side = 0; side < 4; ++side) {
            if ((side == 1 && !farOk) || (side == 2 && !roundOk)) continue;
            if (side == 1) ++far10;
            if (side == 2) ++round10;
            resetAt(side == 1 ? farAt : side == 2 ? roundAt : nearAt, w.him);
            gGauge = 0;
            size_t from = gRecs.size();
            Player(3, 0, w.him, true, true, false);
            HoldForLook();
            long long upAt = -1;
            float prev[3]; memcpy(prev, gPos, sizeof prev);
            int runFrames = 0;
            for (int i = 0; i < (side == 1 ? 900 : 1500); ++i) {
                Step();
                if (side == 3 && !jumped) {               // (d): running at him (a run's pace, nearer each frame), 1.5 m away at least
                    const float step = std::hypot(gPos[0] - prev[0], gPos[2] - prev[2]);
                    const float dNow = std::hypot(gPos[0] - w.him[0], gPos[2] - w.him[2]), dPrev = std::hypot(prev[0] - w.him[0], prev[2] - w.him[2]);
                    runFrames = step >= 50.f && step < 200.f && dNow < dPrev ? runFrames + 1 : 0;
                    memcpy(prev, gPos, sizeof prev);
                    if (runFrames >= 20 && dNow > 1500.f && Blows(3, from, nullptr) == 0) {
                        gPos[0] = w.far[0]; gPos[1] = w.far[1] + 800.f; gPos[2] = w.far[2];   // (its way behind the wall, at the mesh's point nearest him)
                        jumped = true; from = gRecs.size();
                    }
                    if (!jumped && i > 600) break;
                    if (!jumped) continue;
                }
                if (upAt < 0 && Blows(3, from, nullptr) >= 2) upAt = gNow + 6;
                if (upAt >= 0 && gNow == upAt) Player(3, 0, w.him, false, false, false);
                if (upAt >= 0 && gNow > upAt + 300) break;
            }
            if (side == 3 && !jumped) { gHave[3] = false; continue; }
            for (size_t i = from; i < gRecs.size(); ++i) {
                const Rec& r = gRecs[i];
                if (r.slot != 3 || r.kind != 0 || r.r.size() != 9 || r.r[0] != 0x04 || !((r.r[6] >= 0x95 && r.r[6] <= 0x97) || r.r[6] == 0xCF)) continue;
                ++kicks[side];
                const float f[3] = {r.from[0], r.from[1] - 800.f, r.from[2]};
                if (!strike(f, hips)) ++thru[side];
            }
            gHave[3] = false;
        }
        through10 += thru[0] + thru[1] + thru[2] + thru[3];
        if (jumped) ++run10;
        if (could && kicks[0] > 0) ++kicked10;
        const bool good = badGoal == 0 && thru[0] == 0 && thru[1] == 0 && thru[2] == 0 && thru[3] == 0 && (!could || kicks[0] > 0) && (!roundOk || !could || kicks[2] > 0);
        if (good) ++ok10;
        char rs[96] = "none within 28 m", js[96] = "it never ran at him";
        if (roundOk) snprintf(rs, sizeof rs, "%d blows (%d through the wall)", kicks[2], thru[2]);
        if (jumped) snprintf(js, sizeof js, "%d blows (%d through the wall)", kicks[3], thru[3]);
        printf("  wall %zu at %.0f %.0f %.0f (the far side's mesh %.2f m from him):%s; on his side %d blows (%d through the wall), a player %s; behind the wall %s%d blows (%d through it); "
               "from his side %.1f m round to behind the wall: %s; running at him, its way behind the wall: %s\n", wi, w.him[0], w.him[1], w.him[2], std::hypot(w.far[0] - w.him[0], w.far[2] - w.him[2]) / 1000.f,
               goalSay.c_str(), kicks[0], thru[0], could ? "could reach him" : "could not", farOk ? "" : "(no place) ", kicks[1], thru[1], roundOk ? roundLen / 1000.f : 0.f, rs, js);
    }
    // 11 (v150-host beta 25, wakeramp) a player lying knocked out on a ramp or stairs (the floor rising 0.3 m or more within 1 m of him
    // on the bodies' mesh): every such place of the map on a 1 m grid is counted (his anchor the mesh's point; beta 25's run-up rule
    // from each of 16 sides: a place 3 m off within 0.4 m of his height with a straight walkable line to him; a player's: the same up or
    // down the slope, 0.6 to 2.4 m off his height); the 6 places (4 m apart) where beta 25's rule fails from the most sides a player has
    // are tried with the doll starting above him on the slope and below him (3.5 to 7 m away, in its sight, a way of 30 m at most): two
    // kicks (his gauge empty: two runs) whenever a player could reach him (a run-up a player has, a way to it), every blow from a place
    // with the line a blow needs (StrikeClear), within 1.1 m and 0.4 m of his floor (WakeBlow's reach, unchanged). And 4 ledges (the
    // mesh's drops of 0.6 to 2.0 m) with him lying at the ledge's foot and the doll on its top: never a blow from the top (a player
    // there cannot run into him). Beta 25's body and MGO1_VDOLL_WAKE_SLOPE=0 kick twice at 18 of the 92 starts on the 12 maps
    int spots11 = 0, ok11 = 0, could11 = 0, kicked11 = 0, bad11 = 0, ledges11 = 0, top11 = 0, starts11 = 0;
    long long places11 = 0, sloped11 = 0, noneAll11 = 0, noneSome11 = 0, gain11 = 0;
    // the run-up from approach side a0 at anchor m: level (beta 25's: within 0.4 m of his height) or any (up or down the slope too); q
    auto runUp = [&](const float* m, float a0, bool level, float* q) {
        for (float da : {0.f, 0.6f, -0.6f, 1.2f, -1.2f, 1.9f, -1.9f, 3.1416f}) {
            const float a = a0 + da, c[3] = {m[0] + std::sin(a) * 3000.f, m[1], m[2] + std::cos(a) * 3000.f};
            for (float up : {0.f, 600.f, -600.f, 1200.f, -1200.f, 1800.f, -1800.f, 2400.f, -2400.f}) {
                if (level && up != 0.f) break;
                const float c2[3] = {c[0], m[1] + up, c[2]};
                if (!nav10.Nearest(c2, 300.f, q) || (level && std::fabs(q[1] - m[1]) > 400.f)) continue;
                const float d = std::hypot(q[0] - m[0], q[2] - m[2]), y = std::atan2(m[0] - q[0], m[2] - q[2]);
                if (d < 2400.f || nav10.ClearAlong(q, std::sin(y), std::cos(y), d, true) < d - 300.f) continue;
                return true;
            }
        }
        return false;
    };
    struct Slope11 { float m[3]; int gain; };
    std::vector<Slope11> slopes;
    if (nav10.Ready()) {
        std::vector<float> tris;
        std::vector<uint8_t> areas;
        nav10.Triangles(tris, areas);
        std::set<long long> seen;
        for (size_t t = 0; t + 8 < tris.size(); t += 9) {
            if (areas[t / 9] != doll::AREA_STAND) continue;
            const float* v = &tris[t];
            const float x0 = std::min({v[0], v[3], v[6]}), x1 = std::max({v[0], v[3], v[6]}), z0 = std::min({v[2], v[5], v[8]}), z1 = std::max({v[2], v[5], v[8]});
            for (float x = std::ceil(x0 / 1000.f) * 1000.f; x <= x1; x += 1000.f)
                for (float z = std::ceil(z0 / 1000.f) * 1000.f; z <= z1; z += 1000.f) {
                    const float d0 = (v[3] - v[0]) * (z - v[2]) - (v[5] - v[2]) * (x - v[0]), d1 = (v[6] - v[3]) * (z - v[5]) - (v[8] - v[5]) * (x - v[3]), d2 = (v[0] - v[6]) * (z - v[8]) - (v[2] - v[8]) * (x - v[6]);
                    if (!((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))) continue;
                    // (his height there on the triangle's plane)
                    const float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2], bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
                    const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
                    if (std::fabs(ny) < 1e-3f) continue;
                    const float p[3] = {x, v[1] - (nx * (x - v[0]) + nz * (z - v[2])) / ny, z};
                    float m[3];
                    if (!nav10.Nearest(p, 200.f, m) || std::hypot(m[0] - x, m[2] - z) > 100.f) continue;
                    const long long key = ((long long)lrintf(x / 1000.f) * 100000 + (long long)lrintf(z / 1000.f)) * 64 + (long long)std::floor(m[1] / 1000.f);
                    if (!seen.insert(key).second) continue;
                    ++places11;
                    float rise = 0;                      // (the floor's rise within 1 m of him along a walkable line)
                    for (int k = 0; k < 16; ++k) {
                        const float a = (float)k * kPi / 8.f, e[3] = {m[0] + std::sin(a) * 1000.f, m[1], m[2] + std::cos(a) * 1000.f};
                        float q[3];
                        if (nav10.Nearest(e, 300.f, q) && std::hypot(q[0] - e[0], q[2] - e[2]) < 150.f && nav10.ClearAlong(m, std::sin(a), std::cos(a), 1000.f, true) >= 950.f) rise = std::max(rise, std::fabs(q[1] - m[1]));
                    }
                    if (rise < 300.f) continue;
                    ++sloped11;
                    int none = 0, gain = 0;
                    for (int k = 0; k < 16; ++k) {
                        float q[3];
                        const bool lv = runUp(m, (float)k * kPi / 8.f, true, q);
                        if (!lv) ++none;
                        if (!lv && runUp(m, (float)k * kPi / 8.f, false, q)) ++gain;
                    }
                    if (none == 16) ++noneAll11; else if (none) ++noneSome11;
                    if (gain) ++gain11;
                    if (gain) slopes.push_back({{m[0], m[1], m[2]}, gain});
                }
        }
        std::stable_sort(slopes.begin(), slopes.end(), [](const Slope11& a, const Slope11& b) { return a.gain > b.gain; });
    }
    printf("11: %lld places (a 1 m grid on the bodies' mesh), %lld on a ramp or stairs (the floor rising 0.3 m or more within 1 m); beta 25's run-up none from any side at %lld of them, "
           "from some sides at %lld; a player's run-up up or down the slope where beta 25 has none at %lld\n", places11, sloped11, noneAll11, noneSome11, gain11);
    std::vector<Slope11> tried;
    for (const Slope11& sl : slopes) {
        if (tried.size() >= 6) break;
        bool apart = true;
        for (const Slope11& o : tried) if (std::hypot(o.m[0] - sl.m[0], o.m[2] - sl.m[2]) < 4000.f && std::fabs(o.m[1] - sl.m[1]) < 1500.f) apart = false;
        if (apart) tried.push_back(sl);
    }
    for (size_t pi = 0; pi < tried.size(); ++pi) {
        const float* m = tried[pi].m;
        const float him[3] = {m[0], Floor(m[0], m[2], m[1]), m[2]};
        if (him[1] < -1e29f) continue;
        const float hips[3] = {him[0], him[1] + 300.f, him[2]};
        ++spots11;
        int placeOk = 1;
        std::string say;
        for (int side = 0; side < 2; ++side) {           // (its start above him on the slope, then below)
            float st[3];
            bool have = false;
            for (float d : {4000.f, 5000.f, 3500.f, 6000.f, 7000.f})
                for (int k = 0; k < 16 && !have; ++k)
                    for (float up : {600.f, 1200.f, 1800.f, 2400.f, 3000.f}) {
                        if (have) break;
                        const float a = (float)k * kPi / 8.f, c[3] = {m[0] + std::sin(a) * d, m[1] + (side ? -up : up), m[2] + std::cos(a) * d};
                        float pm[3], len = 0, end[3];
                        if (!nav10.Nearest(c, 300.f, pm) || (side ? m[1] - pm[1] : pm[1] - m[1]) < 400.f) continue;
                        // (the body's own points: its eye 1.55 m over its feet, a lying player's chest 0.2 m over his floor, vdoll_body.cpp Chest)
                        const float eye[3] = {pm[0], pm[1] + 1550.f, pm[2]}, chest[3] = {him[0], him[1] + 200.f, him[2]};
                        if (vdoll::body::SightStopped(eye, chest) || !vdoll::body::WayTo(pm, m, &len, end) || len > 30000.f || std::hypot(end[0] - m[0], end[2] - m[2]) > 300.f) continue;
                        memcpy(st, pm, sizeof pm); have = true;
                    }
            if (!have) { say += side ? "; below: no start" : " above: no start"; continue; }
            ++starts11;
            // (a player could: a run-up of his from some side with a way to it of 30 m at most)
            bool could = false;
            for (int k = 0; k < 16 && !could; ++k) {
                float q[3], len = 0, end[3];
                if (runUp(m, (float)k * kPi / 8.f, false, q) && vdoll::body::WayTo(st, q, &len, end) && len <= 30000.f && std::hypot(end[0] - q[0], end[2] - q[2]) <= 300.f) could = true;
            }
            if (could) ++could11;
            resetAt(st, him);
            gGauge = 0;
            size_t from = gRecs.size();
            Player(3, 0, him, true, true, false);
            HoldForLook();
            long long upAt = -1;
            for (int i = 0; i < 1500; ++i) {
                Step();
                if (upAt < 0 && Blows(3, from, nullptr) >= 2) upAt = gNow + 6;
                if (upAt >= 0 && gNow == upAt) Player(3, 0, him, false, false, false);
                if (upAt >= 0 && gNow > upAt + 300) break;
            }
            int kicks = 0, bad = 0;
            for (size_t i = from; i < gRecs.size(); ++i) {
                const Rec& r = gRecs[i];
                if (r.slot != 3 || r.kind != 0 || r.r.size() != 9 || r.r[0] != 0x04 || !((r.r[6] >= 0x95 && r.r[6] <= 0x97) || r.r[6] == 0xCF)) continue;
                ++kicks;
                const float f[3] = {r.from[0], r.from[1] - 800.f, r.from[2]};
                if (!strike(f, hips) || std::hypot(f[0] - him[0], f[2] - him[2]) > 1100.f || std::fabs(f[1] - him[1]) > 400.f) ++bad;
            }
            gHave[3] = false;
            bad11 += bad;
            if (could && kicks >= 2) ++kicked11;
            if (bad || (could && kicks < 2)) placeOk = 0;
            char g[160];
            snprintf(g, sizeof g, "%s from %.1f m %s (%+.2f m): %d kicks%s, a player %s", side ? ";" : "", std::hypot(st[0] - him[0], st[2] - him[2]) / 1000.f, side ? "below" : "above",
                     (st[1] - m[1]) / 1000.f, kicks, bad ? " (SOME OUT OF REACH)" : "", could ? "could" : "could not");
            say += g;
        }
        if (placeOk) ++ok11;
        printf("  slope %zu at %.0f %.0f %.0f (beta 25's run-up missing from %d sides a player has):%s\n", pi, him[0], him[1], him[2], tried[pi].gain, say.c_str());
    }
    // the ledges: him at the foot, the doll on the top in its sight (900 frames): no blow from the top
    if (nav10.Ready()) {
        std::vector<std::array<float, 6>> ls;
        for (int i = 0; i < nav10.LinkCount() && ls.size() < 4; ++i) {
            float a[3], c[3];
            uint8_t area = 0;
            if (!nav10.LinkAt(i, a, c, &area) || area != doll::AREA_DROP || a[1] - c[1] < 600.f || a[1] - c[1] > 2000.f) continue;
            bool apart = true;
            for (const auto& o : ls) if (std::hypot(o[0] - a[0], o[2] - a[2]) < 4000.f) apart = false;
            const float eye[3] = {a[0], a[1] + 1550.f, a[2]}, chest[3] = {c[0], c[1] + 200.f, c[2]};
            if (!apart || vdoll::body::SightStopped(eye, chest)) continue;
            ls.push_back({a[0], a[1], a[2], c[0], c[1], c[2]});
        }
        for (const auto& l : ls) {
            const float top[3] = {l[0], l[1], l[2]};
            const float him[3] = {l[3], Floor(l[3], l[5], l[4]), l[5]};
            if (him[1] < -1e29f) continue;
            ++ledges11;
            resetAt(top, him);
            gGauge = 0;
            size_t from = gRecs.size();
            Player(3, 0, him, true, true, false);
            HoldForLook();
            for (int i = 0; i < 900; ++i) Step();
            int fromTop = 0, all = 0;
            for (size_t i = from; i < gRecs.size(); ++i) {
                const Rec& r = gRecs[i];
                if (r.slot != 3 || r.kind != 0 || r.r.size() != 9 || r.r[0] != 0x04 || !((r.r[6] >= 0x95 && r.r[6] <= 0x97) || r.r[6] == 0xCF)) continue;
                ++all;
                if (r.from[1] - 800.f - him[1] > 400.f) ++fromTop;
            }
            gHave[3] = false;
            top11 += fromTop;
            printf("  ledge at %.0f %.0f %.0f (%.2f m high), him at its foot %.0f %.0f %.0f: %d blows, %d from the top\n", top[0], top[1], top[2], (l[1] - l[4]) / 1000.f, him[0], him[1], him[2], all, fromTop);
        }
    }
    printf("11: slope places tried %d, starts %d (a player could reach him from %d), kicked twice at %d, blows out of a blow's reach %d; ledges %d, blows from the top %d\n",
           spots11, starts11, could11, kicked11, bad11, ledges11, top11);
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    check(ok11 == spots11 && top11 == 0 && kicked11 == could11, "11 a player lying on a ramp or stairs: kicked twice whenever a player could reach him, from above and from below, every blow within a blow's reach; none from a ledge's top");
    printf("10: places %d (%d walls, %d of them from their other face too; beta 25's goal behind the wall at %d), goals %d (%d beyond the wall), blows through a wall %d, a player could reach him at %d (kicked at %d), behind the wall %d, round to it %d, running at him with its way behind the wall %d\n",
           spots10, faces10[0], faces10[1], across10, goals10, badGoals10, through10, could10, kicked10, far10, round10, run10);
    check(ok10 == spots10, "10 a player lying against a thin wall: the doll's goal on his side, no blow through the wall (from his side or behind it), kicked whenever a player could reach him");
    printf("spawn points tested: 1 %d, 2 %d, 3 %d, 4 %d; the first kick %lld frames at most after its first motion record\n", spots[0], spots[1], spots[2], spots[3], worstPace);
    check(spots[0] > 0 && ok1 == spots[0], "1 a player knocked out: the combo pressed running into him, its kick lands (the game's ally wake, -15000, +0xB74 50): two runs from an empty gauge, one from 20000; no punch record, no reaction, none after he is up");
    check(spots[1] > 0 && ok2 == spots[1], "2 a player reading: the CQC throw once (1500, 0xCF) and his fall, none after his reading ends");
    check(spots[2] > 0 && ok3 == spots[2], "3 a virtual teammate knocked out (its gauge 2000): two kicks, the game's ally wake (-15000 each), none after it is up");
    check(tried4 > 0 && none4 == tried4, "4 no blow with an enemy in sight, for a teammate up by himself soon, on an enemy, in a deathmatch");
#ifdef VDOLL_HOST_FF
    printf("6: spawn points %d\n", spots6);
    check(spots6 > 0 && ok6 == spots6, "6 friendly fire off: a teammate knocked out kicked awake as with it on; a reader thrown once with CQC (1500, 0xCF, his fall); none 1.3 s from his reading's end");
#endif
    printf("9: spawn points %d\n", spots9);
    check(spots9 > 0 && ok9 == spots9, "9 each frame given twice to the body: two kicks, never two in one frame");
    printf("8: spawn points %d\n", spots8);
    // (a map with no such place in its sight from the spawn points tried, n001a: none to check there; 22 places on the 12 maps)
    check(ok8 == spots8, "8 a player knocked out against a wall (off the bodies' mesh): two kicks, the second within 400 frames, as in the open");
    printf("7: spawn points %d\n", spots7);
    check(spots7 > 0 && ok7 == spots7, "7 TEAM SNEAKING: a defender knocked out 60 s ago (a quarter of the game's recovery) still kicked awake; none in a team deathmatch");
#ifdef VDOLL_HOST_BOOKS
    printf("5: spawn points with the second doll reading %d\n", spots5);
    check(spots5 > 0 && ok5 == spots5, "5 a virtual teammate reading: the CQC throw once (1500, 0xCF) and its fall, its reading ended by it, the waker never in the book's reach");
#endif
    return fails ? 1 : 0;
}
