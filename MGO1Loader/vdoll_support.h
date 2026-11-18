// (v150-host beta 22, support) the claymore and the book as a player's own game takes and lays them (docs/VIRTUAL_DOLL.md
// 35), and the hidden places a virtual doll lays them at. The game's rules here are apart from its memory, so that they are
// tested off line (tests/vdoll_support_test.cpp); vdoll_body.cpp applies them with the host's claymores' list
// (ps2world.cpp VdollClaymores) and sends the records a player's game sends (vdoll.cpp BodySend kinds 4 and 5).
//
// What the game does (US n003a, every map page's own copy of the same code):
// - The layer's game makes the claymore (0x3FD398 -> NewOnlineClaymore) 0.7 m ahead of him on the floor, facing where he
//   faces, and its claymores' list (NewOnlineClaymoreMan, 16 entries) tells the others on its channel (the room's + 4):
//   {id & 0xFF, id >> 10, state, 0}, three angles (s16, 0x10000 a turn), the place (s16 tenths of mm), a word. State 1: lying
//   (every other game makes a copy, whose blast wounds nobody); 3: gone off (every copy blasts, the layer's own blast
//   wounds); 4: taken (every copy removed, no blast). The host passes every record on to every player.
// - A player takes a claymore with his own game, for his own player only (0x3A8BD8, each copy's act, every frame from its
//   making): he lies on his front (state bit 3), his hips within 500 mm of it on the ground and 500 mm in height, not in his
//   first person view, his claymores not at their most; no owner and no team test (his own, a teammate's, an enemy's). His
//   game removes its copy, adds a round, and sends the state 4.
// - The sensor (0x3A8740) goes off only for a player standing or crouched: a player lying (the same bit 3) sets nothing off,
//   so the crawl onto it never does, and its take's zone lies inside the sensor.
// - The book: the same list's shape on the room's + 10 (NewOnlineBookMan), states 1 lying, 2 gone; nothing takes it back.
#pragma once
#include "doll_geom.h"
#include "doll_nav.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vsup {

// ---- the game's numbers ----
const float kTakeReach = 500.f;                  // his hips to the claymore on the ground (x, z), at most (0x547A94, 0x3A8C50)
const float kTakeHigh = 500.f;                   // his hips to its place in height, under (0x547A94, 0x3A8DC4)
const unsigned kLying = 0x8u;                    // the first state word's bit 3: lying on his front (still 9, going down 0x48,
                                                 // crawling 0x4008 / 0x4018): the take wants it, the sensor refuses it
const unsigned kFpv = 0x80u;                     // the first state word's 0x80: the first person view (no take)
const float kLayAhead = 700.f;                   // the claymore's place ahead of the layer (measured 678 mm, mines_t5_on)
const float kBlastUp = 200.f;                    // the blast's centre over the claymore's place (0x547A74)
const float kBlastReach = 2500.f;                // the wound's sphere (NewBlast's 0x9C4 at 0x3A8AC0)
const int kBlastDamage = 100;                    // the damage table's kind 0x1B (0x2F4360); measured 100 at 1.30 and 2.14 m
const int kClayCode = 0x1B, kBookCode = 0x1C;    // the weapons' codes (their kind in the damage record too)
const float kFxOffX = -1250.f;                   // the effect record's x: his point less half the blast's reach (measured)
const uint32_t kWord = 0x40000000u;              // the record's last word (the layer's +0x100; measured on every record)
const int kStateLying = 1, kStateOff = 3, kStateTaken = 4, kStateGone = 2;
const int kListMax = 16;                         // the list's entries (16): a doll lays none past kListRoom of them in use
const int kListRoom = 12;

// ---- the body's numbers (measured on real clients; docs/VIRTUAL_DOLL.md 35) ----
const float kHipsLying = 300.f;                  // the hips over the floor lying (V21_rot_owner, geom_a1: 300 mm)
const float kCrawl = 15.f;                       // mm a frame crawling (V21_rot_owner: 14 to 15.4 mm a frame)
const int kDownFrames = 50;                      // the copy lying 50 frames after the first {06 03} (geom_a1: 0x44, 0x48, 9)
const int kUpFrames = 45;                        // lying -> crouched -> on its feet (a player's two presses 45 frames apart)
const float kProneAt = 2500.f;                   // it lies down this far from the claymore (out of every sensor: 1.5 m)
const float kKeepOff = 1600.f;                   // its ways keep off its team's claymores and books, and the enemies' it saw
const float kSeeLying = 10000.f;                 // a claymore lying seen in a clear line within 10 m, in its field
const float kSeeLaid = 25000.f;                  // a claymore laid in a clear line within 25 m, in its field: seen laid
const float kSeeField = 70.f * 3.14159265f / 180.f;   // half its field (a third person view's screen)
const int kLayCrouch = 8, kLayHold = 34, kLayMotion = 78, kLayRecordAt = 60;   // {06 02}, {08 12}, {08 13}, its record at +60
                                                 // of the motion (mines_t5_on, book_r4: the host's frames of a real layer)
// a doll's support item drawn at its spawn (MGO1_VDOLL_SUPPORT_ITEM=auto): the playable dolls' own draw (docs/PLAYABLE_DOLL.md:
// the claymore 8 %, the book 6 % of the support row); the grenade otherwise
const int kDrawClayPct = 8, kDrawBookPct = 6;
enum Item { ITEM_GRENADE = 0, ITEM_CLAYMORE = 1, ITEM_BOOK = 2 };
inline Item Draw(unsigned r, bool headshotsOnly, bool sneakRole) {
    if (headshotsOnly || sneakRole) return ITEM_GRENADE;
    const unsigned p = r % 100u;
    return p < (unsigned)kDrawClayPct ? ITEM_CLAYMORE : p < (unsigned)(kDrawClayPct + kDrawBookPct) ? ITEM_BOOK : ITEM_GRENADE;
}

// ---- the take: the game's own test (0x3A8BD8) for a player whose hips, first state word and claymores are these ----
inline bool InTakeZone(const float* hips, const float* clay) {
    const float dx = hips[0] - clay[0], dz = hips[2] - clay[2];
    return std::sqrt(dx * dx + dz * dz) <= kTakeReach && std::fabs(clay[1] - hips[1]) < kTakeHigh;
}
inline bool CanTake(const float* hips, unsigned state0, const float* clay, int had, int most) {
    if (!(state0 & kLying) || (state0 & kFpv)) return false;
    if (had >= most) return false;               // (at his most: the game's message, no take)
    return InTakeZone(hips, clay);
}
// the sensor's own refusal of a player lying (0x1793C8(3, ...)): true when it would go off for him in its space
inline bool SensorWants(unsigned state0) { return !(state0 & kLying); }

// ---- the list's record (0x3A54C8, 0x3A52A0) ----
inline uint32_t NewId(int slot, int seq) { return ((uint32_t)(slot & 0xFF) << 10) | (0x100u + (uint32_t)(seq & 0xFF)); }
inline void Put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back((uint8_t)v); o.push_back((uint8_t)(v >> 8)); }
inline uint16_t Angle16(float a) { return std::isfinite(a) ? (uint16_t)((long)lrintf(a / 6.2831853f * 65536.f) & 0xFFFF) : 0; }
// the place in tenths as the game writes it (0x3A52A0: x / 10.0, cut toward 0)
inline int16_t Tenths(float v) { return std::isfinite(v) ? (int16_t)(int)(v / 10.f) : 0; }
inline std::vector<uint8_t> Record(uint32_t id, int state, float yaw, const float* pos, uint32_t word = kWord) {
    std::vector<uint8_t> r = {(uint8_t)(id & 0xFF), (uint8_t)((id >> 10) & 0xFF), (uint8_t)state, 0x00};
    Put16(r, 0); Put16(r, Angle16(yaw)); Put16(r, 0);
    for (int k = 0; k < 3; ++k) Put16(r, (uint16_t)Tenths(pos[k]));
    for (int k = 0; k < 4; ++k) r.push_back((uint8_t)(word >> (8 * k)));
    return r;
}
// a record read back (tests): its id (the game's decoding 0x3A54F0: low byte + 0x100, the slot << 10), its state, its place
inline bool ReadRecord(const std::vector<uint8_t>& r, uint32_t* id, int* state, float* pos) {
    if (r.size() != 20) return false;
    *id = ((uint32_t)r[1] << 10) | (0x100u + r[0]);
    *state = r[2];
    for (int k = 0; k < 3; ++k) pos[k] = (float)(int16_t)(r[10 + 2 * k] | (r[11 + 2 * k] << 8)) * 10.f;
    return true;
}

// ---- the hidden places (docs/VIRTUAL_DOLL.md 35) ----
// Where a doll lays: on a way the enemies walk (from their spawns toward its side or its objective), at a corner of it, in a
// narrow passage, against a tree's trunk (the stage's small solid volumes: trees, posts) or in the low foliage where the map
// has it (the faces of the foliage kind), out of the long sight lines (seen only from near: the farthest standing eye on the
// map that sees it), never in the open, never by a spawn point, never on its own team's ways out of its spawns, never by
// another claymore. The doll stands 0.7 m short of the place on the way, facing the enemies' way in, and the claymore lies
// there facing them.
struct Eyes { std::vector<std::array<float, 3>> p; };   // standing eyes over the mesh (its floor + 1.55 m), one an 8 m cell
const float kEyeUp = 1550.f, kClayTop = 150.f;
// one eye a cell of the mesh's triangles (the triangle's middle nearest the cell's middle)
inline void MakeEyes(const doll::NavMesh& nav, Eyes& e, float cell = 8000.f) {
    std::vector<float> t; std::vector<uint8_t> a;
    nav.Triangles(t, a);
    struct C { float d2 = 1e30f; std::array<float, 3> p{}; };
    std::vector<std::pair<long long, C>> cells;
    std::vector<long long> keys;
    for (size_t i = 0; i + 9 <= t.size(); i += 9) {
        const uint8_t ar = a[i / 9];
        if (ar != doll::AREA_STAND && ar != doll::AREA_CROUCH) continue;
        const float m[3] = {(t[i] + t[i + 3] + t[i + 6]) / 3.f, (t[i + 1] + t[i + 4] + t[i + 7]) / 3.f, (t[i + 2] + t[i + 5] + t[i + 8]) / 3.f};
        const long long cx = (long long)std::floor(m[0] / cell), cy = (long long)std::floor(m[1] / 2500.f), cz = (long long)std::floor(m[2] / cell);
        const long long k = (cx & 0xFFFFF) | ((cz & 0xFFFFF) << 20) | ((cy & 0xFFFFF) << 40);
        const float mx = (cx + 0.5f) * cell, mz = (cz + 0.5f) * cell;
        const float d2 = (m[0] - mx) * (m[0] - mx) + (m[2] - mz) * (m[2] - mz);
        auto it = std::lower_bound(keys.begin(), keys.end(), k);
        const size_t at = (size_t)(it - keys.begin());
        if (it == keys.end() || *it != k) { keys.insert(it, k); C c; cells.insert(cells.begin() + (long)at, {k, c}); }
        C& c = cells[at].second;
        if (d2 < c.d2) { c.d2 = d2; c.p = {m[0], m[1] + kEyeUp, m[2]}; }
    }
    e.p.clear();
    for (auto& c : cells) e.p.push_back(c.second.p);
}
// how far a point is seen from: the farthest eye within reach with a clear line to it, how many see it, of how many in reach
struct Seen { float far = 0.f; int by = 0, of = 0, farBy = 0; };   // (farBy: the eyes over kLongSight away that see it)
const float kLongSight = 15000.f;
template <class Clear>
Seen SeenFrom(const Eyes& e, const float* top, Clear clear, float reach = 40000.f) {
    Seen s;
    for (const auto& p : e.p) {
        const float dx = p[0] - top[0], dy = p[1] - top[1], dz = p[2] - top[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > reach || d < 300.f) continue;
        ++s.of;
        if (!clear(p.data(), top)) continue;
        ++s.by;
        if (d > kLongSight) ++s.farBy;
        s.far = std::max(s.far, d);
    }
    return s;
}
// a way sampled every 500 mm along the ground (its links, drops and climbs, kept out: nothing is laid on a link)
struct Sample { float p[3]; float dir; bool link; };
inline void SampleWay(const std::vector<doll::Waypoint>& w, std::vector<Sample>& out, float step = 500.f) {
    out.clear();
    float carry = 0.f;
    for (size_t i = 1; i < w.size(); ++i) {
        const float* a = w[i - 1].p;
        const float* b = w[i].p;
        const float dx = b[0] - a[0], dz = b[2] - a[2], l = std::sqrt(dx * dx + dz * dz);
        if (l < 1.f) continue;
        const float dir = std::atan2(dx, dz);
        const bool link = w[i - 1].link;
        for (float t = carry; t < l; t += step) {
            Sample s;
            const float u = t / l;
            s.p[0] = a[0] + dx * u; s.p[1] = a[1] + (b[1] - a[1]) * u; s.p[2] = a[2] + dz * u; s.dir = dir; s.link = link;
            out.push_back(s);
            carry = t + step - l;
        }
    }
}
// the stage's small solid volumes (trees, posts: the grid's slot 4) and the low foliage's faces (their middles), for the places
struct Cover {
    std::vector<std::array<float, 4>> trunks;    // x y z, the volume's half width across
    std::vector<std::array<float, 3>> leaves;    // the foliage faces' middles (2 m tall at most)
    bool HasGrass() const { return !leaves.empty(); }
};
// the foliage kind: a face a bullet passes (neither of the bits 0x42), the attribute's low 28 bits 0x0808000 (Lost Forest's
// bushes and grass, and the like on the 12 disc maps' n001a and n032a), 2 m tall at most
inline bool Foliage(const doll::Face& f) {
    if ((f.attr & 0x0FFFFFFFu) != 0x0808000u) return false;
    float lo = 1e30f, hi = -1e30f;
    for (int k = 0; k < 4; ++k) { lo = std::min(lo, f.v[k][1]); hi = std::max(hi, f.v[k][1]); }
    return hi - lo <= 2000.f;
}
inline void MakeCover(const doll::Geom& g, Cover& c) {
    c.trunks.clear(); c.leaves.clear();
    for (const doll::Volume& v : g.volumes)
        if (v.slot == 4 && v.half[0] < 1200.f && v.half[2] < 1200.f && v.half[1] > 600.f)
            c.trunks.push_back({v.centre[0], v.centre[1], v.centre[2], std::max(v.half[0], v.half[2])});
    for (const doll::Face& f : g.faces)
        if (Foliage(f)) c.leaves.push_back({(f.v[0][0] + f.v[2][0]) * 0.5f, std::min(f.v[0][1], f.v[2][1]), (f.v[0][2] + f.v[2][2]) * 0.5f});
}
inline bool TrunkBy(const Cover& c, const float* p, float r) {
    for (const auto& t : c.trunks)
        if (std::fabs(t[1] - p[1]) < 2500.f && std::hypot(t[0] - p[0], t[2] - p[2]) < r + t[3]) return true;
    return false;
}
inline int LeavesBy(const Cover& c, const float* p, float r) {
    int n = 0;
    for (const auto& l : c.leaves) if (std::fabs(l[1] - p[1]) < 1500.f && std::hypot(l[0] - p[0], l[2] - p[2]) < r) ++n;
    return n;
}
// a place's inputs and its measures
struct Place {
    float at[3] = {0, 0, 0}, stand[3] = {0, 0, 0}, yaw = 0.f;   // the claymore's place, where the layer stands, its facing
    Seen seen;                                    // how far it is seen from (its top, 0.15 m up)
    float approach = 0.f;                         // the farthest point of the enemies' way in (30 m before it) that sees it
    int walls = 0;                                // directions of 16 stopped within 3 m on the mesh
    bool corner = false, narrow = false, trunk = false, grass = false;
    int enemyPass = 0, friendPass = 0;            // the enemies' and its team's way samples within 1 m
    float score = -1e30f;
};
struct PickIn {
    const std::vector<std::vector<Sample>>* enemyWays = nullptr;   // the ways the enemies walk toward its side
    const std::vector<std::vector<Sample>>* friendWays = nullptr;  // its team's ways out of its spawns (their first 15 m)
    const std::vector<std::array<float, 3>>* spawns = nullptr;     // every spawn point of the map (never on one: minSpawn)
    const std::vector<std::array<float, 3>>* ownSpawns = nullptr;  // its team's (its teammates come out there: minOwn)
    const std::vector<std::array<float, 3>>* foeSpawns = nullptr;  // the enemies' (no claymore by their spawn: minFoe)
    const std::vector<std::array<float, 3>>* traps = nullptr;      // the claymores and books lying, and its team's players now
    const float* near = nullptr;                  // only places within nearR of this point (the doll), when given
    float nearR = 35000.f;
    float minSpawn = 4000.f, minOwn = 4000.f, minFoe = 4000.f, minTrap = 4000.f, maxApproach = 12000.f;
    int maxFarBy = 1;                             // (out of the long sight lines: seen from over 15 m by one eye at most,
    float maxFar = 25000.f;                       // and from 25 m by none)
};
// the place's walls: the directions of 16 the mesh stops within r (a wall, an edge, a crate)
inline int WallsAround(const doll::NavMesh& nav, const float* p, float r, int* nearMask = nullptr) {
    int n = 0, m = 0;
    for (int k = 0; k < 16; ++k) {
        const float a = k * 6.2831853f / 16.f;
        const float c = nav.ClearAlong(p, std::sin(a), std::cos(a), r);
        if (c < r - 1.f) ++n;
        if (c < 1200.f) m |= 1 << k;
    }
    if (nearMask) *nearMask = m;
    return n;
}
// the candidates on the enemies' ways (cheap tests), then the measured ones: a place by every test, best score first
template <class Clear>
bool Measure(const doll::NavMesh& nav, const Eyes& eyes, const Cover& cover, const PickIn& in, const std::vector<Sample>& way, size_t i, Place* out, Clear clear, int* why = nullptr, bool* costly = nullptr) {
    const Sample& s = way[i];
    int dummy[16] = {};
    int* w = why ? why : dummy;
    if (s.link) { ++w[0]; return false; }
    if (in.near && std::hypot(s.p[0] - in.near[0], s.p[2] - in.near[2]) > in.nearR) { ++w[1]; return false; }
    auto by = [&](const std::vector<std::array<float, 3>>* v, float r) {
        if (v) for (const auto& q : *v) if (std::hypot(q[0] - s.p[0], q[2] - s.p[2]) < r && std::fabs(q[1] - s.p[1]) < 4000.f) return true;
        return false;
    };
    if (by(in.spawns, in.minSpawn) || by(in.ownSpawns, in.minOwn) || by(in.foeSpawns, in.minFoe)) { ++w[2]; return false; }
    if (in.traps) for (const auto& q : *in.traps) if (std::hypot(q[0] - s.p[0], q[2] - s.p[2]) < in.minTrap && std::fabs(q[1] - s.p[1]) < 3000.f) { ++w[3]; return false; }
    Place pl;
    // the enemies' way in: the samples before it (they come from there); its corner: the way's turn within 3 m around it
    const int back = 6, fwd = 6;
    if (i < (size_t)back || i + fwd >= way.size()) { ++w[4]; return false; }
    for (int k = -back; k <= fwd; ++k) if (way[i + k].link) { ++w[5]; return false; }
    const float a0 = std::atan2(s.p[0] - way[i - back].p[0], s.p[2] - way[i - back].p[2]);
    const float a1 = std::atan2(way[i + fwd].p[0] - s.p[0], way[i + fwd].p[2] - s.p[2]);
    float turn = a1 - a0;
    while (turn > 3.14159265f) turn -= 6.2831853f;
    while (turn < -3.14159265f) turn += 6.2831853f;
    pl.corner = std::fabs(turn) >= 35.f * 3.14159265f / 180.f;
    int mask = 0;
    pl.walls = WallsAround(nav, s.p, 3000.f, &mask);
    // narrow: stopped within 1.2 m on both sides across the way
    const int side = (int)lrintf((s.dir + 3.14159265f / 2.f) / (6.2831853f / 16.f)) & 15;
    const bool l = (mask >> side) & 1, r = (mask >> ((side + 8) & 15)) & 1;
    pl.narrow = l && r;
    pl.trunk = TrunkBy(cover, s.p, 1500.f);
    pl.grass = cover.HasGrass() && LeavesBy(cover, s.p, 1200.f) >= 3;
    // never in the open: a corner, a narrow way, a trunk or the foliage, and the mesh stopped in 4 directions of 16 at least
    if (!(pl.corner || pl.narrow || pl.trunk || pl.grass)) { ++w[6]; return false; }
    if (pl.walls < 4 && !pl.trunk && !pl.grass) { ++w[7]; return false; }
    // on the way, against its wall when one is within 1.2 m on a side (0.3 m toward it): a player cutting the corner walks onto it
    float at[3] = {s.p[0], s.p[1], s.p[2]};
    if (l != r) {
        const float a = (l ? side : (side + 8) & 15) * 6.2831853f / 16.f;
        const float c = nav.ClearAlong(s.p, std::sin(a), std::cos(a), 1200.f);
        const float m = std::max(0.f, std::min(300.f, c - 450.f));
        at[0] += std::sin(a) * m; at[2] += std::cos(a) * m;
    }
    float onMesh[3];
    if (!nav.Nearest(at, 300.f, onMesh)) { ++w[8]; return false; }
    memcpy(pl.at, onMesh, sizeof pl.at);
    // the layer 0.7 m further along the way (its side), facing the enemies' way in
    pl.yaw = std::atan2(way[i - back].p[0] - s.p[0], way[i - back].p[2] - s.p[2]);
    const float st[3] = {pl.at[0] - std::sin(pl.yaw) * kLayAhead, pl.at[1], pl.at[2] - std::cos(pl.yaw) * kLayAhead};
    float stOn[3];
    if (!nav.Nearest(st, 300.f, stOn) || std::fabs(stOn[1] - pl.at[1]) > 400.f || !nav.Walkable(stOn, pl.at)) { ++w[9]; return false; }
    memcpy(pl.stand, stOn, sizeof pl.stand);
    // the traffic: the enemies' ways and its team's ways out of its spawns within 1 m
    if (in.enemyWays) for (const auto& ew : *in.enemyWays) { for (const Sample& q : ew) if (std::hypot(q.p[0] - pl.at[0], q.p[2] - pl.at[2]) < 1000.f && std::fabs(q.p[1] - pl.at[1]) < 1500.f) { ++pl.enemyPass; break; } }
    if (in.friendWays) for (const auto& fw : *in.friendWays) { for (const Sample& q : fw) if (std::hypot(q.p[0] - pl.at[0], q.p[2] - pl.at[2]) < 1500.f && std::fabs(q.p[1] - pl.at[1]) < 1500.f) { ++pl.friendPass; break; } }
    if (pl.friendPass > 0 && pl.friendPass >= pl.enemyPass) { ++w[10]; return false; }
    // how far it is seen from (the costly part): the map's standing eyes, then the enemies' way in (30 m before it)
    const float top[3] = {pl.at[0], pl.at[1] + kClayTop, pl.at[2]};
    if (costly) *costly = true;
    pl.seen = SeenFrom(eyes, top, clear);
    if (pl.seen.farBy > in.maxFarBy || pl.seen.far > in.maxFar) { ++w[11]; return false; }
    float walked = 0.f;
    for (size_t k = i; k-- > 0 && walked < 30000.f;) {
        walked += 500.f;
        if (way[k].link) continue;
        const float e[3] = {way[k].p[0], way[k].p[1] + kEyeUp, way[k].p[2]};
        const float d = std::sqrt((e[0] - top[0]) * (e[0] - top[0]) + (e[1] - top[1]) * (e[1] - top[1]) + (e[2] - top[2]) * (e[2] - top[2]));
        if (d > pl.approach && clear(e, top)) pl.approach = d;
    }
    if (pl.approach > in.maxApproach) { ++w[12]; return false; }
    pl.score = 2.f * pl.enemyPass - 3.f * pl.friendPass + (pl.corner ? 2.f : 0.f) + (pl.narrow ? 1.f : 0.f) + (pl.trunk ? 2.f : 0.f) + (pl.grass ? 3.f : 0.f) +
               pl.walls / 4.f - std::min(pl.seen.far, 20000.f) / 5000.f - pl.seen.farBy - pl.approach / 4000.f;
    *out = pl;
    return true;
}
// the best place over the enemies' ways (every 2nd sample, every way): the off line test's and the measures' search
template <class Clear>
bool PickAll(const doll::NavMesh& nav, const Eyes& eyes, const Cover& cover, const PickIn& in, Place* best, Clear clear, std::vector<Place>* all = nullptr, int* why = nullptr) {
    bool any = false;
    if (!in.enemyWays) return false;
    for (const auto& w : *in.enemyWays)
        for (size_t i = 0; i < w.size(); i += 2) {
            Place p;
            if (!Measure(nav, eyes, cover, in, w, i, &p, clear, why)) continue;
            if (all) all->push_back(p);
            if (!any || p.score > best->score) { *best = p; any = true; }
        }
    return any;
}

// a side's spawn points for a rule: where its players appear again (the first kind of doll::RespawnKinds the map has) and where
// they start a round (the team deathmatch's and the capture's first points of that team); a deathmatch: its points
inline void SideSpawns(const doll::Geom& g, int rule, int team, std::vector<std::array<float, 3>>& out) {
    out.clear();
    doll::SpawnKind k[4];
    const int n = doll::RespawnKinds(rule < 0 ? 1 : rule, team, k);
    doll::SpawnKind first = doll::SP_UNKNOWN;
    bool have = false;
    for (int i = 0; i < n && !have; ++i) for (const doll::Spawn& p : g.spawns) if (p.kind == k[i] && (k[i] != doll::SP_SNE_SOLDIER || p.hash)) { first = k[i]; have = true; break; }
    const bool dm = rule == 0;
    for (const doll::Spawn& p : g.spawns) {
        const bool start = !dm && (p.kind == (team ? doll::SP_TDM_FIRST_BLUE : doll::SP_TDM_FIRST_RED) || p.kind == (team ? doll::SP_TCM_BLUE : doll::SP_TCM_RED));
        if ((have && p.kind == first && (first != doll::SP_SNE_SOLDIER || p.hash)) || start) out.push_back({p.p[0], p.p[1], p.p[2]});
    }
}
// ---- the places of one side on a map, found a few at a time (the body's frames: StepPlaces with a small budget; the off line
// test: until done). Its enemies' ways: from their spawn points (every 2nd) to its own (every 3rd); in a deathmatch from every
// 2nd spawn point to every 3rd (everyone is an enemy, no side of its own). Its team's ways out of its spawns: the same legs the
// other way, their first 15 m
struct Places {
    bool started = false, done = false;
    std::vector<std::array<float, 3>> all, own, foe;   // every spawn point of the rule, its team's, the enemies'
    std::vector<std::array<float, 6>> legs, outs;      // the ways to plan: from, to (the enemies' in; its team's out)
    size_t legAt = 0, outAt = 0, wayAt = 0, sampleAt = 0;
    std::vector<std::vector<Sample>> enemy, friendW;
    std::vector<Place> found;
    int tried = 0, why[16] = {}, debt = 0;      // (debt: a costly measure's units over a step's budget, paid by the next steps)
};
inline void StartPlaces(Places& P, const std::vector<std::array<float, 3>>& own, const std::vector<std::array<float, 3>>& foe, bool deathmatch) {
    P = Places();
    P.started = true;
    P.own = deathmatch ? std::vector<std::array<float, 3>>() : own;
    P.foe = foe;
    P.all = own; P.all.insert(P.all.end(), foe.begin(), foe.end());
    const auto& to = deathmatch ? foe : own;
    // (16 legs at most: 4 of the enemies' points spread over their list, 4 of its own; the corridors between them repeat)
    const size_t fi = std::max<size_t>(1, foe.size() / 4), ti = std::max<size_t>(1, to.size() / 4);
    for (size_t i = 0; i < foe.size(); i += fi)
        for (size_t j = deathmatch ? fi / 2 : 0; j < to.size(); j += ti) {
            if (deathmatch && i == j) continue;
            P.legs.push_back({foe[i][0], foe[i][1], foe[i][2], to[j][0], to[j][1], to[j][2]});
            if (!deathmatch) P.outs.push_back({to[j][0], to[j][1], to[j][2], foe[i][0], foe[i][1], foe[i][2]});
        }
}
// a step: budget units (a way planned 1, a candidate measured 1); true when every candidate was measured. findWay(from, to, w)
// the bodies' way; clear(a, b) the bodies' line
template <class FindWay, class Clear>
bool StepPlaces(Places& P, const doll::NavMesh& nav, const Eyes& eyes, const Cover& cover, int budget, FindWay findWay, Clear clear) {
    if (!P.started || P.done) return P.done;
    budget -= P.debt; P.debt = 0;
    if (budget <= 0) { P.debt = -budget; return false; }
    while (budget > 0 && P.legAt < P.legs.size()) {
        const auto& l = P.legs[P.legAt++];
        --budget;
        std::vector<doll::Waypoint> w;
        float a[3] = {l[0], l[1], l[2]}, b[3] = {l[3], l[4], l[5]}, from[3], to[3];
        if (!nav.Nearest(a, 1500.f, from) || !nav.Nearest(b, 1500.f, to) || !findWay(from, to, w) || w.size() < 2) continue;
        P.enemy.emplace_back();
        SampleWay(w, P.enemy.back());
    }
    while (budget > 0 && P.outAt < P.outs.size()) {
        const auto& l = P.outs[P.outAt++];
        --budget;
        std::vector<doll::Waypoint> w;
        float a[3] = {l[0], l[1], l[2]}, b[3] = {l[3], l[4], l[5]}, from[3], to[3];
        if (!nav.Nearest(a, 1500.f, from) || !nav.Nearest(b, 1500.f, to) || !findWay(from, to, w) || w.size() < 2) continue;
        P.friendW.emplace_back();
        SampleWay(w, P.friendW.back());
        if (P.friendW.back().size() > 30) P.friendW.back().resize(30);   // (its first 15 m)
    }
    PickIn in;
    in.enemyWays = &P.enemy; in.friendWays = &P.friendW; in.spawns = &P.all; in.ownSpawns = &P.own; in.foeSpawns = &P.foe;
    while (budget > 0 && P.wayAt < P.enemy.size()) {
        const auto& w = P.enemy[P.wayAt];
        if (P.sampleAt >= w.size()) { ++P.wayAt; P.sampleAt = 0; continue; }
        const size_t i = P.sampleAt;
        P.sampleAt += 4;                             // (every 2 m along the way)
        bool dup = false;                            // (the ways share their corridors: a place within 2 m of one found is that one)
        for (const Place& f : P.found) if (std::hypot(f.at[0] - w[i].p[0], f.at[2] - w[i].p[2]) < 2000.f && std::fabs(f.at[1] - w[i].p[1]) < 1500.f) { dup = true; break; }
        if (dup) continue;
        ++P.tried;
        Place pl;
        bool costly = false;                         // (its sight measured: 10 units, the eyes' lines; a refusal before it: 1)
        if (Measure(nav, eyes, cover, in, w, i, &pl, clear, P.why, &costly)) P.found.push_back(pl);
        budget -= costly ? 10 : 1;
    }
    if (budget < 0) P.debt = -budget;
    P.done = P.legAt >= P.legs.size() && P.outAt >= P.outs.size() && P.wayAt >= P.enemy.size();
    return P.done;
}

}  // namespace vsup
