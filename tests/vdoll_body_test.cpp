// The virtual dolls' bodies off line (MGO1Loader/vdoll_body.cpp, docs/VIRTUAL_DOLL.md 7): a map's .geom, a doll run on
// its mesh for some minutes (its places every 100 ms: the steps, the climbs, the hips over the floor), then an enemy
// standing in its sight (its aim, its shots, its hits), then the host's copy of it held back 3 s while it runs (put
// back there, then on). MGO1_VDOLL_LEVEL as on the host.
//   g++ -O2 -std=c++17 -I MGO1Loader tests/vdoll_body_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
//       MGO1Loader/doll_nav.cpp MGO1Loader/recast/*.cpp -lpthread -o vdoll_body_test
//   ./vdoll_body_test <map.geom> [minutes]
#include "vdoll_body.h"
#include "doll_geom.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

static std::vector<uint8_t> gGeom;
static vdoll::PlayerNow gPlayers[16];
static bool gHave[16];
static int gRuleNow = 1;

static bool gMeshSaid = false;                         // the bodies' mesh ready (its line)
static std::vector<float> gCamoSights;                 // (beta 18) the distances a camouflaged sneaker was first seen at
static bool gCamoPart = false;                         // (the parts with a camouflaged sneaker: every sight of him counted)
static void Log(const char* fmt, ...) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (strstr(b, "(the bodies' mesh)")) gMeshSaid = true;
    if (gCamoPart && strstr(b, ": sees player ")) { float d = 0; if (sscanf(strstr(b, " at ") + 4, "%f", &d) == 1) gCamoSights.push_back(d); }
    printf("%s\n", b);
}
static bool MapNow(std::string* page, int* extra) { *page = "n003a"; *extra = 0; return true; }
static bool StageGeom(const std::string&, int, std::vector<uint8_t>* out, std::string*) { *out = gGeom; return true; }
static bool See(int id, vdoll::PlayerNow* s) {
    if (!gHave[id]) return false;
    *s = gPlayers[id];
#ifdef VDOLL_PLAYERNOW_SENT
    // (v150-host beta 21, cover) VDOLL_TEST_SENT=1: every player a real one's records (his place sent 50 ms ago, a round
    // trip of 100 ms): what the cover gate reads (tests/vdoll_cover_same.sh: off, the records are beta 20's all the same)
    static const bool sent = getenv("VDOLL_TEST_SENT") != nullptr;
    if (sent && s->object) { memcpy(s->sent, s->pos, sizeof s->sent); s->sentAge = 50; s->rtt = 100; }
#endif
    return true;
}
static int Rule() { return gRuleNow; }

struct Sent { long long frame; int slot, kind; std::vector<uint8_t> rec; };
static std::vector<Sent> gSent;
static long long gNow = 0;
// (v150-host beta 16) VDOLL_TEST_DUMP=<file>: every record sent, a line each (frame slot kind hex), to compare two builds with
// MGO1_VDOLL_SEED set; VDOLL_TEST_WEAPON_RULE=<0..7>: the game's weapons setting the bodies are given
static FILE* gDump = nullptr;
static void Send(void*, int slot, int kind, const std::vector<uint8_t>& rec) {
    gSent.push_back({gNow, slot, kind, rec});
    if (gDump) { fprintf(gDump, "%lld %d %d ", gNow, slot, kind); for (uint8_t b : rec) fprintf(gDump, "%02x", b); fputc('\n', gDump); }
}
static int gWeaponRule = -1;
static int WeaponRule() { return gWeaponRule; }
// (v150-host beta 18) the mission objects of a TEAM SNEAKING round: one item, the goal of the side that sneaks (gSneak, -1 none)
static int gSneak = -1;
// (v150-host beta 19, kero) `item`: a CAPTURE round's free KEROTAN at gItem, both sides' goals set (both take it)
static bool gItemOn = false;
static float gItem[3] = {0, 0, 0}, gItemGoal[3] = {0, 0, 0};
static int Objectives(vdoll::ObjNow* out, int max, float goals[2][3], bool set[2]) {
    if (max < 1) return 0;
    if (gItemOn) {
        out[0] = vdoll::ObjNow(); memcpy(out[0].pos, gItem, sizeof gItem);
        for (int g = 0; g < 2; ++g) { memcpy(goals[g], gItemGoal, sizeof gItemGoal); set[g] = true; }
        return 1;
    }
    out[0] = vdoll::ObjNow(); out[0].pos[0] = 1e5f; out[0].pos[1] = -1e5f; out[0].pos[2] = 1e5f;   // (far away: no way there)
    for (int g = 0; g < 2; ++g) { goals[g][0] = 5000.f; goals[g][1] = 0.f; goals[g][2] = 5000.f; set[g] = g == gSneak; }
    return 1;
}

// (v150-host beta 18) `vdoll_body_test <map.geom> tsne`: the weapons of a TEAM SNEAKING sneaker, of its defender and of
// SNEAKING's Snake. The doll at the map's first team spawn, an enemy standing ahead of it in its sight; each part a new
// life; the records it sends counted by the weapon they name: a sneaker's name no lethal weapon and take no life (darts:
// stun only), a defender's are lethal, Snake's are his own. Fails on beta 17's code (every doll fired its AK-47).
static bool Lethal(int code) { return code == 0x0A || code == 0x0B || code == 0x0C || code == 0x0E || code == 0x0F || code == 0x06 || code == 0x08 || code == 0x13 || code == 0x14 || code == 0x1B; }
struct Tally { int stealth13 = 0; int recs13 = 0, lethal13 = 0, dart13 = 0, hits = 0, lethalHits = 0, lifeHits = 0, darts = 0, stunMax = 0; std::map<int, int> names; };
static int TsneParts(const char* geomPath) {
    doll::Geom g; std::string err;
    if (!doll::DecodeGeom(gGeom.data(), gGeom.size(), g, err)) { printf("FAIL %s: %s\n", geomPath, err.c_str()); return 1; }
    float start[3] = {0, 0, 0}; bool have = false;
    for (const auto& sp : g.spawns) if (sp.kind == doll::SP_TDM_RED || sp.kind == doll::SP_DM) { memcpy(start, sp.p, sizeof start); have = true; break; }
    if (!have) { printf("FAIL no team spawn in %s\n", geomPath); return 1; }
    start[1] += 800.f;
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.objectives = Objectives;
    vdoll::body::Init(h);
    for (long long fr = 0; fr < 600; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("FAIL the bodies' mesh was not built\n"); return 1; }
    gHave[0] = true; gPlayers[0].team = 0xFE;
    float pos[3]; uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.pos = pos; s.facing = &facing; s.send = Send;
    long long fr = 600;
    int fails = 0;
    // one part: the rule, the doll's team and class, the side that sneaks, the enemy's distance; the enemy asleep or not
    bool camoNow = false;                             // (the enemy a camouflaged sneaker: PlayerNow::camo)
    auto part = [&](const char* what, int rule, int team, int cls, int sneak, float dist, bool asleep) {
        gRuleNow = rule; gSneak = sneak;
        memcpy(pos, start, sizeof pos);
        gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = team; gPlayers[1].object = true; gPlayers[1].alive = true;
        gPlayers[1].life = cls == 8 ? 440 : 200; gPlayers[1].cls = cls;
        s.team = team; s.life = fr;                       // (a new life)
        Tally t;
        for (int tries = 0; tries < 40 && t.hits < 4; ++tries) {
            const float yaw = facing / 65536.f * 6.2831853f;
            gHave[2] = true; gPlayers[2] = vdoll::PlayerNow(); gPlayers[2].team = 1 - team; gPlayers[2].object = true; gPlayers[2].alive = true;
            gPlayers[2].life = 200; gPlayers[2].cls = 1;
#ifdef VDOLL_PLAYERNOW_ASLEEP
            gPlayers[2].asleep = asleep;
#endif
#ifdef VDOLL_PLAYERNOW_CAMO
            gPlayers[2].camo = camoNow;
#endif
            gPlayers[2].pos[0] = pos[0] + std::sin(yaw) * dist; gPlayers[2].pos[1] = pos[1]; gPlayers[2].pos[2] = pos[2] + std::cos(yaw) * dist;
            gSent.clear();
            for (long long f = fr; f < fr + 400; f += 2) {
                gNow = f;
                memcpy(gPlayers[1].pos, pos, sizeof pos);
                vdoll::body::Frame(f);
                s.tick = f % 6 == 0;
                vdoll::body::Alive(s, f);
            }
            fr += 400;
            for (const Sent& m : gSent) {
                if (m.slot == 1 && m.kind == 2 && m.rec.size() >= 3 && m.rec[0] == 0x13) {
                    ++t.recs13; ++t.names[m.rec[2]];
                    if (m.rec[1] == 0x17) ++t.stealth13;
                    if (Lethal(m.rec[2])) ++t.lethal13;
                    if (m.rec[2] == 0x05 || m.rec[2] == 0x10) ++t.dart13;
                }
                if (m.slot == 2 && m.kind == 0 && m.rec.size() >= 7 && m.rec[0] == 0x04) {
                    ++t.hits;
                    const int life = m.rec[2] | m.rec[3] << 8, stun = m.rec[4] | m.rec[5] << 8;
                    if (life > 0) ++t.lifeHits;
                    if (Lethal(m.rec[6])) ++t.lethalHits;
                    if (m.rec[6] == 0x05 || m.rec[6] == 0x10) { ++t.darts; t.stunMax = std::max(t.stunMax, stun); }
                }
            }
        }
        gHave[2] = false;
        printf("%s: weapon records %d (", what, t.recs13);
        for (auto& kv : t.names) printf(" %02x x%d", kv.first, kv.second);
        printf(" ), lethal %d; hits %d (darts %d, the most stun %d), with life %d, lethal %d\n", t.lethal13, t.hits, t.darts, t.stunMax, t.lifeHits, t.lethalHits);
        return t;
    };
    auto check = [&](bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    // TEAM SNEAKING, its team (red) sneaks, the enemy 12 m away: the Mk22
    Tally a = part("team sneaking, sneaking, an enemy 12 m away", 5, 0, 1, 0, 12000.f, false);
    check(a.recs13 > 0 && a.lethal13 == 0, "a sneaker's weapon records name no lethal weapon");
    check(a.stealth13 == a.recs13, "a sneaker holds the STEALTH CAMOUFLAGE (item 0x17) in every record, as a real sneaker");
    check(a.hits > 0 && a.lethalHits == 0 && a.lifeHits == 0, "a sneaker's hits are darts: no lethal weapon, no life taken");
    check(a.darts > 0 && a.stunMax >= 6000 && a.stunMax <= 30000, "its darts carry the Mk22's stun (6000 to 30000)");
    // the enemy 35 m away: the MOSIN NAGANT
    Tally b = part("team sneaking, sneaking, an enemy 25 m away", 5, 0, 1, 0, 25000.f, false);
    check(b.lethal13 == 0 && b.lethalHits == 0 && b.lifeHits == 0, "far away, still nothing lethal");
    printf("(far away: its MOSIN NAGANT %s; it needs him in sight beyond 20 m on this map)\n", b.names.count(0x10) ? "taken" : "not taken");
#ifdef VDOLL_PLAYERNOW_ASLEEP
    // the enemy asleep (the host's game holds him knocked out): no dart on him
    Tally c = part("team sneaking, sneaking, an enemy asleep 12 m away", 5, 0, 1, 0, 12000.f, true);
    check(c.hits == 0, "no dart on an enemy already asleep");
#endif
    // its team defends (blue sneaks): lethal as before
    Tally d = part("team sneaking, defending, an enemy 12 m away", 5, 0, 1, 1, 12000.f, false);
    check(d.hits > 0 && d.lethalHits == d.hits && d.lifeHits == d.hits, "a defender's hits are lethal, as before");
    check(d.dart13 == 0, "a defender holds no dart gun");
    check(d.stealth13 == 0, "a defender holds no STEALTH CAMOUFLAGE");
    // (beta 18) a defender and a camouflaged sneaker (his stealth camouflage on, no alert): seen close by only, as SNEAKING's
    // Snake and as the playable dolls see Snake (3 m, 1.5 m more a level up to 5: 10.5 m at level 10)
    camoNow = true; gCamoPart = true;
    Tally g1 = part("team sneaking, defending, a camouflaged sneaker 25 m away", 5, 0, 1, 1, 25000.f, false);
    Tally g2 = part("team sneaking, defending, a camouflaged sneaker 6 m away", 5, 0, 1, 1, 6000.f, false);
    camoNow = false; gCamoPart = false;
    float farthest = 0; for (float d : gCamoSights) farthest = std::max(farthest, d);
    printf("a camouflaged sneaker first seen at %zu times, at %.1f m at most\n", gCamoSights.size(), farthest);
    check(!gCamoSights.empty() && farthest <= 10.6f, "a camouflaged sneaker is first seen within 10.5 m only (level 10)");
    check(g2.hits > 0, "a camouflaged sneaker 6 m away is seen and fought");
    // SNEAKING, the doll is Snake (blue, class 8): his own weapons
    Tally e = part("sneaking, Snake, an enemy 20 m away", 4, 1, 8, -1, 20000.f, false);
    bool own = e.recs13 > 0;
    for (auto& kv : e.names) if (kv.first != 0x0B && kv.first != 0x05 && kv.first != 0x15 && kv.first != 0x01 && kv.first != 0x00) own = false;
    check(own, "Snake's weapon records name only his own (XM16E1, Mk22, stun grenades, the knife)");
    Tally f2 = part("sneaking, Snake, an enemy 6 m away", 4, 1, 8, -1, 6000.f, false);
    check(f2.names.count(0x05) > 0 && f2.darts > 0, "Snake's Mk22 close by: darts");
    // (v150-host beta 18) a stun grenade (MGO1_VDOLL_NADE_TEST: thrown at an enemy seen 4 to 15 m away): 12500 of the
    // stamina, no life, to every enemy within 8 m of the blast (the game's wound of 8000), here one 10 m away and one
    // 4.5 m away on the same line, 5.5 m from him (a frag's 3 m would miss the one it is not thrown at)
    {
        setenv("MGO1_VDOLL_NADE_TEST", "1", 1);          // (its stun grenades only)
        gRuleNow = 5; gSneak = 0;
        memcpy(pos, start, sizeof pos);
        gHave[1] = true; gPlayers[1] = vdoll::PlayerNow(); gPlayers[1].team = 0; gPlayers[1].object = true; gPlayers[1].alive = true; gPlayers[1].life = 200; gPlayers[1].cls = 1;
        s.team = 0; s.life = fr;
        int stun12500[16] = {0}, other = 0;
        for (int tries = 0; tries < 40 && !(stun12500[2] && stun12500[3]); ++tries) {
            const float yaw = facing / 65536.f * 6.2831853f;
            for (int id : {2, 3}) { gHave[id] = true; gPlayers[id] = vdoll::PlayerNow(); gPlayers[id].team = 1; gPlayers[id].object = true; gPlayers[id].alive = true; gPlayers[id].life = 200; gPlayers[id].cls = 1; }
            gPlayers[2].pos[0] = pos[0] + std::sin(yaw) * 10000.f; gPlayers[2].pos[1] = pos[1]; gPlayers[2].pos[2] = pos[2] + std::cos(yaw) * 10000.f;
            // (the second on the throw's own line, 5.5 m short of the first: a clear line from either blast to the other)
            gPlayers[3].pos[0] = pos[0] + std::sin(yaw) * 4500.f; gPlayers[3].pos[1] = pos[1]; gPlayers[3].pos[2] = pos[2] + std::cos(yaw) * 4500.f;
            gSent.clear();
            for (long long f = fr; f < fr + 400; f += 2) { gNow = f; memcpy(gPlayers[1].pos, pos, sizeof pos); vdoll::body::Frame(f); s.tick = f % 6 == 0; vdoll::body::Alive(s, f); }
            fr += 400;
            for (const Sent& m : gSent)
                if ((m.slot == 2 || m.slot == 3) && m.kind == 0 && m.rec.size() >= 7 && m.rec[0] == 0x04) {
                    const int life = m.rec[2] | m.rec[3] << 8, stun = m.rec[4] | m.rec[5] << 8;
                    if (m.rec[6] == 0x15 && life == 0 && stun == 12500) ++stun12500[m.slot]; else if (m.rec[6] == 0x15) ++other;
                }
        }
        gHave[2] = gHave[3] = false;
        printf("stun grenades: the one 10 m away reached %d times, the one 4.5 m away %d times, other stun grenade records %d\n", stun12500[2], stun12500[3], other);
        check(stun12500[2] > 0 && other == 0, "a stun grenade's record: 12500 of the stamina, no life, the STUN G's code");
        check(stun12500[2] > 0 && stun12500[3] > 0, "a stun grenade reaches both, 5.5 m apart (the game's 8 m)");
    }
    printf("%s\n", vdoll::body::Status(0).c_str());
    return fails ? 1 : 0;
}

// (v150-host beta 19, kero) `vdoll_body_test <map.geom> item x,y,z hx,hy,hz start_x,start_y,start_z [seconds]`: a capture
// round, a free item lying at x y z (the item's own place), a place the game held a doll back twice at hx hy hz (kept off by
// the ways), the doll's hips at the start. PASS when the doll comes within the game's pick-up reach of the item (1.5 m)
// in time (60 s). Lost Forest's case of kero_t1_caprot: the item on the hill 20433 5913 -5654 left 105 s, the carrier
// held twice at 20360 5313 331 on the way up: `item 20433,5913,-5654 20360,5313,331 18054,2683,7067` on n001a's .geom
static int ItemPart(int argc, char** argv) {
    float start[3], held[3];
    if (argc < 6 || sscanf(argv[3], "%f,%f,%f", &gItem[0], &gItem[1], &gItem[2]) != 3 || sscanf(argv[4], "%f,%f,%f", &held[0], &held[1], &held[2]) != 3 ||
        sscanf(argv[5], "%f,%f,%f", &start[0], &start[1], &start[2]) != 3) { fprintf(stderr, "usage: vdoll_body_test <map.geom> item x,y,z hx,hy,hz sx,sy,sz [seconds]\n"); return 2; }
    const double seconds = argc > 6 ? atof(argv[6]) : 60.0;
    setenv("MGO1_VDOLL_DEBUG", "1", 1);
    setenv("MGO1_VDOLL_LEVEL", getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10", 1);
    if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
    gItemOn = true; gRuleNow = 3;
    gItemGoal[0] = start[0] + 60000.f; gItemGoal[1] = start[1]; gItemGoal[2] = start[2];   // (far: the goal never chosen first)
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.objectives = Objectives;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    gHave[1] = true; gPlayers[1].team = 0; gPlayers[1].object = true; gPlayers[1].alive = true; gPlayers[1].life = 200;
    for (long long fr = 0; fr < 600; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("FAIL the bodies' mesh was not built\n"); return 1; }
#ifdef VDOLL_BODY_KEEPOFF
    vdoll::body::KeepOff(held);
#endif
    float pos[3] = {start[0], start[1], start[2]};
    uint16_t facing = 0;
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.life = 600; s.pos = pos; s.facing = &facing; s.send = Send;
    float best = 1e30f; long long at = -1;
    const long long end = 600 + (long long)(seconds * 60);
    for (long long fr = 600; fr < end && at < 0; fr += 2) {
        gNow = fr;
        memcpy(gPlayers[1].pos, pos, sizeof pos);
        vdoll::body::Frame(fr);
        s.tick = fr % 6 == 0;
        vdoll::body::Alive(s, fr);
        const float d = std::sqrt((pos[0] - gItem[0]) * (pos[0] - gItem[0]) + (pos[1] - gItem[1]) * (pos[1] - gItem[1]) + (pos[2] - gItem[2]) * (pos[2] - gItem[2]));
        best = std::min(best, d);
        if (d < 1500.f) at = fr;
    }
    printf("%s a free item a player can take at %.0f %.0f %.0f (a held place at %.0f %.0f %.0f on the way): %s (the nearest %.1f m)\n", at >= 0 ? "PASS" : "FAIL",
           gItem[0], gItem[1], gItem[2], held[0], held[1], held[2], at >= 0 ? ("reached in " + std::to_string((at - 600) / 60) + " s").c_str() : "not reached", best / 1000.f);
    return at >= 0 ? 0 : 1;
}

// (v150-host beta 19, kero) `vdoll_body_test <map.geom> itemhome hx,hy,hz x,y,z ...`: a capture round's free item (its home
// hx hy hz) lying at each place given in turn (the game's state 0, nobody holding it), no doll alive; the host's itemHome
// puts it home and is counted. PASS when the item is sent home within 5 s from a place marked `!` (no player can take it
// there) and never from the others. Ghost Factory: `itemhome 17409,80,4543 '!-18913,-4171,16446' 13101,6,15140
// '!-9766,-2088,20717' -12417,-177,15415 5939,3093,16424 10164,-462,-22088`
static int gHomes = 0;
static float gHome[3];
static bool ItemHome(int i) { if (i != 0) return false; ++gHomes; memcpy(gItem, gHome, sizeof gHome); return true; }
static int ItemHomePart(int argc, char** argv) {
    setenv("MGO1_VDOLL_DEBUG", "1", 1);
    gItemOn = true; gRuleNow = 3;
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule; h.objectives = Objectives; h.itemHome = ItemHome;
    vdoll::body::Init(h);
    gHave[0] = true; gPlayers[0].team = 0xFE;
    for (long long fr = 0; fr < 600; fr += 2) { gNow = fr; vdoll::body::Frame(fr); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) { vdoll::body::Frame(598); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (!gMeshSaid) { printf("FAIL the bodies' mesh was not built\n"); return 1; }
    int fails = 0;
    long long fr = 600;
    if (argc < 4 || sscanf(argv[3], "%f,%f,%f", &gHome[0], &gHome[1], &gHome[2]) != 3) return 2;
    for (int a = 4; a < argc; ++a) {
        const bool bad = argv[a][0] == '!';
        if (sscanf(argv[a] + (bad ? 1 : 0), "%f,%f,%f", &gItem[0], &gItem[1], &gItem[2]) != 3) return 2;
        const float was[3] = {gItem[0], gItem[1], gItem[2]};
        gHomes = 0;
        long long sent = -1;
        for (const long long from = fr; fr < from + 600; fr += 2) {      // (10 s lying there)
            gNow = fr; vdoll::body::Frame(fr);
            if (gHomes && sent < 0) sent = fr - from;
        }
        const bool ok = bad ? sent >= 0 && sent <= 300 && gHomes == 1 : gHomes == 0;   // (once: never again from its home)
        if (!ok) ++fails;
        printf("%s the item lying at %.0f %.0f %.0f (%s): %s\n", ok ? "PASS" : "FAIL", was[0], was[1], was[2], bad ? "no player can take it there" : "a player can take it",
               sent >= 0 ? ("sent home after " + std::to_string(sent) + " frames").c_str() : "left where it lies");
        fr += 600;                                    // (another place: 10 s with no item first)
        gItem[0] = 1e5f; gItem[1] = -1e5f; gItem[2] = 1e5f;
        for (const long long from = fr; fr < from + 120; fr += 2) { gNow = fr; vdoll::body::Frame(fr); }
    }
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: vdoll_body_test <map.geom> [minutes]\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); gGeom.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(gGeom.data(), 1, gGeom.size(), f) != gGeom.size()) return 1;
    fclose(f);
    if (argc > 2 && !strcmp(argv[2], "item")) return ItemPart(argc, argv);   // (beta 19, kero)
    if (argc > 2 && !strcmp(argv[2], "itemhome")) return ItemHomePart(argc, argv);   // (beta 19, kero)
    if (argc > 2 && !strcmp(argv[2], "tsne")) {          // (beta 18) the sneaking weapons' parts only
        setenv("MGO1_VDOLL_LEVEL", getenv("MGO1_VDOLL_LEVEL") ? getenv("MGO1_VDOLL_LEVEL") : "10", 1);
        if (!getenv("MGO1_VDOLL_SEED")) setenv("MGO1_VDOLL_SEED", "7", 1);
        return TsneParts(argv[1]);
    }
    const double minutes = argc > 2 ? atof(argv[2]) : 3.0;
    setenv("MGO1_VDOLL_DEBUG", "1", 1);
    if (!getenv("MGO1_VDOLL_WEAPONS")) setenv("MGO1_VDOLL_WEAPONS", "ak47", 1);   // (the hits' damages below: the AK-47's)
    if (const char* d = getenv("VDOLL_TEST_DUMP")) gDump = fopen(d, "w");
    vdoll::Host h;
    h.log = Log; h.mapNow = MapNow; h.stageGeom = StageGeom; h.see = See; h.rule = Rule;
    if (const char* w = getenv("VDOLL_TEST_WEAPON_RULE")) { gWeaponRule = atoi(w); h.weaponRule = WeaponRule; }
    vdoll::body::Init(h);
    // the host: id 0; the doll: slot 1, red, at City Under Siege's first red spawn
    gHave[0] = true; gPlayers[0].team = 0xFE;
    gHave[1] = true; gPlayers[1].team = 0; gPlayers[1].object = true; gPlayers[1].alive = true; gPlayers[1].life = 200;
    float pos[3] = {34125.f, 800.f, -5500.f};
    if (const char* e = getenv("VDOLL_TEST_POS")) sscanf(e, "%f,%f,%f", &pos[0], &pos[1], &pos[2]);   // (its hips; another map's start)
    uint16_t facing = 0x7fff;
    for (long long fr = 0; fr < 600; fr += 2) {      // the mesh built
        gNow = fr; vdoll::body::Frame(fr);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (int w = 0; w < 6000 && !gMeshSaid; ++w) {   // (a busy machine: a minute more at most)
        vdoll::body::Frame(598);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!gMeshSaid) printf("the bodies' mesh was not built: the parts below run without it\n");
    vdoll::body::Self s;
    s.k = 0; s.slot = 1; s.team = 0; s.life = 600; s.pos = pos; s.facing = &facing; s.send = Send;
    // (1) running: its places every 100 ms
    const long long end = 600 + (long long)(minutes * 3600);
    long long nextTick = 600;
    for (long long fr = 600; fr < end; fr += 2) {
        gNow = fr;
        memcpy(gPlayers[1].pos, pos, sizeof pos);
        vdoll::body::Frame(fr);
        s.tick = fr >= nextTick;
        if (s.tick) nextTick = fr + 6;
        vdoll::body::Alive(s, fr);
    }
    // the places sent ({0 x y z}), the steps between two
    std::vector<std::array<float, 4>> places;
    std::map<int, int> styles;
    for (const Sent& m : gSent) {
        if (m.slot == 1 && m.kind == 0 && m.rec[0] == 0 && m.rec.size() == 13) {
            float p[3]; memcpy(p, &m.rec[1], 12);
            places.push_back({(float)m.frame, p[0], p[1], p[2]});
        }
        if (m.slot == 1 && m.kind == 1 && m.rec[0] == 6) ++styles[m.rec[1]];
    }
    int flat = 0, up = 0, down = 0, still = 0, big = 0, climbs = 0;
    double sumFlat = 0, sumUp = 0, sumDown = 0;
    float maxDy = 0;
    for (size_t i = 1; i < places.size(); ++i) {
        const float dh = std::hypot(places[i][1] - places[i - 1][1], places[i][3] - places[i - 1][3]), dy = places[i][2] - places[i - 1][2];
        if (dh < 5.f) { ++still; continue; }
        if (dh > 400.f) ++big;
        maxDy = std::max(maxDy, std::fabs(dy));
        const float slope = std::atan2(std::fabs(dy), dh) * 180.f / 3.14159265f;
        if (slope < 5.f) { ++flat; sumFlat += dh; }
        else if (dy > 0) { ++up; sumUp += dh; ++climbs; }
        else { ++down; sumDown += dh; ++climbs; }
    }
    printf("running %.1f min: %zu places; still %d; flat %d (%.0f mm a record), up %d (%.0f), down %d (%.0f); steps over 400 mm %d; the most height a record %.0f mm\n",
           minutes, places.size(), still, flat, flat ? sumFlat / flat : 0, up, up ? sumUp / up : 0, down, down ? sumDown / down : 0, big, maxDy);
    printf("styles:"); for (auto& kv : styles) printf(" %02x x%d", kv.first, kv.second); printf("\n");
    printf("%s\n", vdoll::body::Status(0).c_str());
    // (2) an enemy standing 15 m away in its sight: its aim, shots and hits
    gSent.clear();
    float feet[3] = {pos[0], pos[1], pos[2]};
    gHave[2] = true; gPlayers[2].team = 1; gPlayers[2].object = true; gPlayers[2].alive = true; gPlayers[2].life = 200;
    int hits[3] = {0, 0, 0}, shots = 0;
    for (int tries = 0; tries < 40 && !hits[0] && !hits[1] && !hits[2]; ++tries) {
        // the enemy ahead of the doll along its facing, on open ground (a clear line: the body's own sight test)
        const float yaw = facing / 65536.f * 6.2831853f;
        gPlayers[2].pos[0] = feet[0] + std::sin(yaw) * 15000.f; gPlayers[2].pos[1] = feet[1]; gPlayers[2].pos[2] = feet[2] + std::cos(yaw) * 15000.f;
        for (long long fr = end + tries * 400; fr < end + tries * 400 + 400; fr += 2) {
            gNow = fr;
            memcpy(gPlayers[1].pos, pos, sizeof pos);
            vdoll::body::Frame(fr);
            s.tick = fr % 6 == 0;
            vdoll::body::Alive(s, fr);
        }
        for (const Sent& m : gSent) {
            if (m.slot == 1 && m.kind == 1 && m.rec[0] == 8 && m.rec.size() > 1 && m.rec[1] == 5) ++shots;
            if (m.slot == 2 && m.kind == 0 && m.rec[0] == 4) { const int d = m.rec[2] | m.rec[3] << 8; ++hits[d == 240 ? 0 : d == 60 ? 1 : 2]; }
        }
        gSent.clear();
        memcpy(feet, pos, sizeof feet);
    }
    printf("an enemy 15 m away: shot records with a shot %d, hits head %d body %d legs %d\n", shots, hits[0], hits[1], hits[2]);
    printf("%s\n", vdoll::body::Status(0).c_str());
    // (4) (beta 15) an enemy 0.9 m before it, from level 6: its CQC (the grab on his channel, then the throw's stun)
    {
        const float yaw = facing / 65536.f * 6.2831853f;
        gPlayers[2].pos[0] = pos[0] + std::sin(yaw) * 900.f; gPlayers[2].pos[1] = pos[1]; gPlayers[2].pos[2] = pos[2] + std::cos(yaw) * 900.f;
        gSent.clear();
        const long long from = end + 40 * 400 + 2;
        for (long long fr = from; fr < from + 600; fr += 2) {
            gNow = fr;
            memcpy(gPlayers[1].pos, pos, sizeof pos);
            vdoll::body::Frame(fr);
            s.tick = fr % 6 == 0;
            vdoll::body::Alive(s, fr);
        }
        int grabs = 0, stuns = 0, holds = 0;
        for (const Sent& m : gSent) {
            if (m.slot == 2 && m.kind == 1 && m.rec[0] == 0x0D) ++grabs;
            if (m.slot == 2 && m.kind == 0 && m.rec[0] == 4 && m.rec.size() > 5 && m.rec[4] == 0x30 && m.rec[5] == 0x75) ++stuns;
            if (m.slot == 1 && m.kind == 1 && m.rec[0] == 0x0C) ++holds;
        }
        printf("an enemy 0.9 m away: grabs %d, its hold records %d, throws (stun 30000) %d\n", grabs, holds, stuns);
        gSent.clear();
    }
    // (3) the host's copy of the doll held back (frozen where it is) while the doll runs: put back there, then on
    gHave[2] = false;
    long long fr = end + 40 * 400 + 1200;
    for (long long t = fr; t < fr + 600; t += 2) { gNow = t; memcpy(gPlayers[1].pos, pos, sizeof pos); vdoll::body::Frame(t); s.tick = t % 6 == 0; vdoll::body::Alive(s, t); }
    fr += 600;
    float held[3] = {0, 0, 0}, was[3]; memcpy(was, pos, sizeof was);
    long long heldFrom = -1;
    int puts = 0;
    float ran = 0, last[3]; memcpy(last, pos, sizeof last);
    for (long long t = fr; t < fr + 1800; t += 2) {
        gNow = t;
        if (heldFrom < 0 && std::hypot(pos[0] - was[0], pos[2] - was[2]) > 50.f) { heldFrom = t; memcpy(held, pos, sizeof held); }   // (running: held from here)
        memcpy(was, pos, sizeof was);
        if (heldFrom >= 0 && t < heldFrom + 180) memcpy(gPlayers[1].pos, held, sizeof held); else memcpy(gPlayers[1].pos, pos, sizeof pos);
        vdoll::body::Frame(t);
        s.tick = t % 6 == 0;
        vdoll::body::Alive(s, t);
        const float d = std::hypot(pos[0] - last[0], pos[2] - last[2]);
        if (d > 1000.f) ++puts; else ran += d;
        memcpy(last, pos, sizeof last);
    }
    printf("its copy held 3 s from a run: put back %d time(s), then %.1f m run in 28 s\n", puts, ran / 1000.f);
    // (5) (beta 16) a life that begins outside the map's collision box (a spawn point 300 m over it): its place is still told
    {
        gSent.clear();
        fr += 1800;
        s.life = fr;                                  // (another life)
        pos[1] += 300000.f;
        for (long long t = fr; t < fr + 300; t += 2) { gNow = t; memcpy(gPlayers[1].pos, pos, sizeof pos); vdoll::body::Frame(t); s.tick = t % 6 == 0; vdoll::body::Alive(s, t); }
        int told = 0;
        for (const Sent& m : gSent) if (m.slot == 1 && m.kind == 0 && m.rec[0] == 0 && m.rec.size() == 13) ++told;
        printf("a life begun 300 m over the map's box: %d position records in 5 s (%s%s)\n", told, told >= 40 ? "told, the box not asked this life" : "SILENT",
               gMeshSaid ? "" : "; no mesh: the box was never asked");
        if (told < 40) return 1;
    }
    return 0;
}
