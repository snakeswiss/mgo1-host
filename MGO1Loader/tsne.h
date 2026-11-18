// tsne.h - TEAM SNEAKING (TSNE), the lobby's sixth rule (docs/TEAM_SNEAKING.md). Included by ps2world.cpp inside its
// namespace, after the helpers it uses (memory, versions, pages, the dialects).
//
// The rotation holds TSNE as rule 5 (the lobby, the game's settings, sync object 0); every read the game makes of the
// rotation (0x178490) gets RESCUE MISSION (2) for it, so the game runs RESCUE's machinery (one life, the GA-KO, the
// goal, the team pages, the round's end); what TSNE adds is put in by the hooks below while the round's rule is 5.
#pragma once

const uint8_t kTsneRule = 5;                           // the rotation's rule number of TEAM SNEAKING
const uint8_t kTsneAs = 2;                             // the rule the game plays it as (RESCUE MISSION)
const uint32_t kTsneMark = 0x7A53E1C9u;                // the dialect's mark (FNV-1a of "TEAM SNEAKING 1")
const uint32_t kTsneNonLethal = 0x9920;                // the choice's bits SNEAKING keeps for Snake (procedure 46: -w 39200):
                                                       // MOSIN NAGANT 5, Mk22 8, STUN G 11, CHAFF G 12, BOOK 15
const uint32_t kTsneLethal = 0x46DF;                   // the defenders': SCORPION 0, XM16E1 1, AK-47 2, M37 3, SVD 4, M1911A1 6,
                                                       // SAA 7, GRENADE 9, WP G 10, CLAYMORE 14 (no SMOKE G 13: no harm)
// a weapon's code (the choice's 0x3C8F20 tables, a box's +0x44): 1 lethal, 0 non-lethal, -1 not a choice's weapon
int TsneWeaponLethal(uint32_t code) {
    switch (code) {
    case 0x0A: case 0x0B: case 0x0C: case 0x0E: case 0x0F: case 0x06: case 0x08: case 0x13: case 0x14: case 0x1B: return 1;
    case 0x10: case 0x05: case 0x15: case 0x16: case 0x17: case 0x1C: return 0;
    default: return -1;
    }
}

int RoundRuleRaw() {                                   // the round's rule as the rotation holds it (5: TEAM SNEAKING)
    const uint32_t d = ConfigData0();
    if (d < 0x100000 || d + 0x40 >= 0x02000000) return -1;
    const uint32_t i = R8(d + 0x36);
    return i < 16 ? R8(d + 3 + 2 * i) : -1;
}
bool TsneRound() {                                     // the round played is TEAM SNEAKING (read once a frame)
    static long long frame = -1;
    static bool on = false;
    if (frame != gFrameNo) { frame = gFrameNo; on = gNative && RoundRuleRaw() == kTsneRule; }
    return on;
}
bool TsneMapPage() { return gPageN3 || gPageMap; }      // a map's page is loaded (its functions paired with n003a's)

// the HUD's game record (n003a 0x3C6DE0: its global 0x2F0AD8, set by the HUD's init 0x3C6928, cleared by its die;
// none before the HUD): +0xBC the round's number, +0xC4 / +0xC8 the red and blue teams' wins, +0xCC the stage's rounds
// (a pointer each), +0xDC
// the first of the rules' explanations less 0x1D (0x125408's index: the language's texts)
uint32_t TsneHud() {
    const uint32_t at = PageData(0x3C6DE0, 0, 0x2F0AD8);
    const uint32_t g = at ? R32(at) & 0x01FFFFFF : 0;
    return g >= 0x100000 && g + 0x100 < 0x02000000 ? g : 0;
}
uint32_t TsneRecordByte(uint32_t g, uint32_t off) {
    const uint32_t p = g ? R32(g + off) & 0x01FFFFFF : 0;
    return p >= 0x100000 && p < 0x02000000 ? R8(p) : 0;
}

// ---- the sides: RESCUE's attackers are the blue team (its goals are side B's, team 1's: the stages' scripts give
// `-b [387fc5]`). TSNE switches the sides each round: in the even rounds the red team sneaks, its goals side A's and
// the two teams' RESCUE spawns swapped (the point lookup 0x161200 given a red spawn's code takes the blue one's and
// back), so the sneakers always come from RESCUE's attackers' side. The round's number is the HUD's (its record's
// +0xBC), which is made after the mission objects: the sides are settled once it is there (TsneSides), before the
// weapons' choice and the spawns
bool gTsneSwap = false;                                // this round the red team sneaks
bool gTsneSideDone = false;                            // this round's sides settled
int gTsneRoundNo = 0;                                  // the round's number (the HUD's)
uint32_t TsneSpawnCode(uint32_t h) {                   // RESCUE's spawns: red 6A5B84 .. 6A5B8B, blue 6A5784 .. 6A578B
    const uint32_t c = h & 0xFFFFFF;
    if ((c >= 0x6A5B84 && c <= 0x6A5B8B) || (c >= 0x6A5784 && c <= 0x6A578B)) return (h & 0xFF000000u) | (c ^ 0xC00);
    return h;
}

// ---- the round's two targets and two goals (the mission objects, class [bf76c9] NewOnlineItemChara2, US n003a's
// options at 0x39E0B0: +0x410 the objects (3 at most, 0x120 each from +0xB0: +4 the model's code, +0x90 x y z w),
// +0x8B0 / +0x8B4 the goals of either side (3 each, 0xC0 from +0x430 / +0x670, the place at +0x90))
struct TsnePoint { float x, y, z; bool ok; };
uint32_t gTsneScratch = 0;                             // 64 bytes for the point lookups (0x161200's place, turn, scale)
TsnePoint TsneLookup(Ctx& c, uint32_t hash) {          // a point of the stage's geom by its code (0x161200, as 0x161278 asks)
    TsnePoint p{0, 0, 0, false};
    if (!hash) return p;
    if (!gTsneScratch) gTsneScratch = Alloc(64);
    ps2::Fn f = ps2::FindFn(V(0x161200));
    if (!gTsneScratch || !f) return p;
    Ctx t = c;
    t.r[4] = gTsneScratch; t.r[5] = gTsneScratch + 0x20; t.r[6] = gTsneScratch + 0x10; t.r[7] = hash;
    f(t);
    if (!(uint32_t)t.r[2]) return p;
    p.x = F32(gTsneScratch); p.y = F32(gTsneScratch + 4); p.z = F32(gTsneScratch + 8); p.ok = true;
    return p;
}
float TsneDist(const TsnePoint& a, const TsnePoint& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
void WF32x(uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); W32(a, u); }
// each stock map's KEROTAN and second goal, points of its stage by code (docs/TEAM_SNEAKING.md, The two targets and
// the two goals: why each). An extra map's: its points.txt's 7A53E1 (the KEROTAN) and 7A53E2 (the second goal), else
// TsneChoose's
const uint32_t kTsneKerotanCode = 0x7A53E1, kTsneGoalCode = 0x7A53E2;
struct TsneStagePoints { uint32_t kerotan, goal2; float gx, gy, gz, kx, ky, kz; };   // goal2 0: the place gx gy gz; kerotan 0: kx ky kz
// chosen by walking distance (docs/TEAM_SNEAKING.md, Fairness; docs/FAIRNESS.md): the KEROTAN nearer the defenders'
// starts, the second goal on the sneakers' side, both 5 m or more from every start
const TsneStagePoints kTsneStages[12] = {
    {0xFA828C, 0, -11600, 1590, 28945},   // 1 Lost Forest: a film's place (west); the sneakers' side, 9 m from their starts
    {0xC74156, 0, -22450, 0, 35950},   // 2 Ghost Factory: SNEAKING's first goal (west); the sneakers' south-west corner
    {0xFA828A, 0x2B16C4},   // 3 City Under Siege: a film's place (west); a CAPTURE spawn by the blue starts
    {0x167E9F, 0, -1761, 6, -9950},   // 4 Killhouse A: SNEAKING's film place (red half); the blue half, 5 m from the starts
    {0x08B6E4, 0, -6438, 0, -8012},   // 5 Killhouse B: the corner across from the GA-KO (RESCUE's own layout is symmetric); the blue side, 7 m from the starts
    {0x08B6E3, 0, -6838, 0, -15638},   // 6 Killhouse C: CAPTURE's goal A; the blue side, 6 m from the starts
    {0x08B6E3, 0, 34925, 29, -15004},   // 7 Svyatogornyj East: CAPTURE's goal A; the sneakers' side, 52 m from RESCUE's goal
    {0, 0x08B6E4, 0, 0, 0, 135706, 80875, -210250},   // 8 Mountaintop: below the summit (its top is not walked from the starts); CAPTURE's goal B
    {0xF5B721, 0x08B6E4},   // 9 Graniny Gorki Lab: CAPTURE's KEROTAN; CAPTURE's goal B
    {0x08B6E3, 0x08B6E4},   // 10 Pillbox Purgatory: CAPTURE's goal A and goal B (the map's two mirrored halves)
    {0, 0, -24950, 1754, -29050, -1850, 0, 20350},   // 11 High Ice: the red side, 23 m from the GA-KO; the blue base, 6 m or more from the starts
    {0x08B6E4, 0, -21450, 0, -9675},   // 12 Brown Town: CAPTURE's goal B (by the red starts); the blue side, 6 m from the starts
};
// the rule the table above was made by (docs/TEAM_SNEAKING.md), for a map without them (the extra maps): the sides
// from RESCUE's spawns (red 6A5B84.., the defenders; blue 6A5784.., the attackers), the distances scaled to the
// stage (L: the two sides' spawns apart, 40 m their unit). The KEROTAN: CAPTURE's, 5 m or more from the GA-KO and
// 12 m from RESCUE's goal and not on the attackers' side; else the point of the defenders' side farthest from both.
// The second goal: on the attackers' side, 12 m or more from RESCUE's goal and 15 m from both targets, CAPTURE's
// goals first, then SNEAKING's, then the films' and the KEROTANs' places, then the blue spawns; the farthest from
// RESCUE's goal
void TsneChoose(Ctx& c, const TsnePoint& gako, const TsnePoint& goal, TsnePoint& kero, uint32_t& keroAt, TsnePoint& goal2, uint32_t& goal2At) {
    auto centre = [&](uint32_t base) {
        TsnePoint m{0, 0, 0, false}; int n = 0;
        for (uint32_t k = 0; k < 8; ++k) { const TsnePoint p = TsneLookup(c, base + k); if (p.ok) { m.x += p.x; m.y += p.y; m.z += p.z; ++n; } }
        if (n) { m.x /= n; m.y /= n; m.z /= n; m.ok = true; }
        return m;
    };
    const TsnePoint red = centre(0x6A5B84), blue = centre(0x6A5784);
    if (!red.ok || !blue.ok) return;
    const float u = std::min(1.f, TsneDist(red, blue) / 40000.f);   // the stage's scale (1 from 40 m apart)
    const uint32_t capture[] = {0x08B6E3, 0x08B6E4}, sneaking[] = {0xC74156, 0xC74157, 0xC971CE},
                   places[] = {0xFA828A, 0xFA828B, 0xFA828C, 0x167E9F, 0xF5B721, 0xDC8E07, 0xDC8E08};
    if (!kero.ok) {
        for (uint32_t h : {0xDC8E07u, 0xF5B721u}) {
            const TsnePoint p = TsneLookup(c, h);
            if (p.ok && TsneDist(p, gako) >= 5000.f * u && TsneDist(p, goal) >= 12000.f * u && TsneDist(p, red) <= TsneDist(p, blue) + 5000.f * u) {
                kero = p; keroAt = h; break;
            }
        }
        float best = -1.f;
        for (const auto* list : {capture, sneaking, places})
            for (size_t i = 0; !kero.ok && i < (list == capture ? 2u : list == sneaking ? 3u : 7u); ++i) {
                const TsnePoint p = TsneLookup(c, list[i]);
                if (!p.ok || TsneDist(p, red) >= TsneDist(p, blue) || TsneDist(p, gako) < 8000.f * u || TsneDist(p, goal) < 12000.f * u) continue;
                const float d = std::min(TsneDist(p, gako), TsneDist(p, goal));
                if (d > best) { best = d; keroAt = list[i]; }
            }
        if (!kero.ok && best > 0) kero = TsneLookup(c, keroAt);
    }
    if (goal2.ok || !kero.ok) return;
    uint32_t spawns[8];
    for (uint32_t k = 0; k < 8; ++k) spawns[k] = 0x6A5784 + k;
    const std::pair<const uint32_t*, size_t> classes[] = {{capture, 2}, {sneaking, 3}, {places, 7}, {spawns, 8}};
    for (const auto& cl : classes) {
        float best = -1.f;
        for (size_t i = 0; i < cl.second; ++i) {
            if (cl.first[i] == keroAt) continue;
            const TsnePoint p = TsneLookup(c, cl.first[i]);
            if (!p.ok || TsneDist(p, blue) >= TsneDist(p, red) || TsneDist(p, goal) < 12000.f * u || TsneDist(p, gako) < 15000.f * u ||
                TsneDist(p, kero) < 15000.f * u) continue;
            if (TsneDist(p, goal) > best) { best = TsneDist(p, goal); goal2 = p; goal2At = cl.first[i]; }
        }
        if (goal2.ok) break;
    }
}
uint32_t gTsneObjects = 0;                              // the mission objects' actor of this round (0: none yet)
uint32_t gTsneGen = 0;                                  // the rounds' count (a GAME RESTART's objects keep their address)
void TsneObjects(Ctx& c, uint32_t mo) {                 // after the options of RESCUE's mission objects: TSNE's two and two
    if (mo < 0x100000 || mo + 0x8C0 >= 0x02000000) return;
    gTsneObjects = mo;
    ++gTsneGen;
    gTsneSwap = false; gTsneSideDone = false; gTsneRoundNo = 0;
    const uint32_t n = R32(mo + 0x410), nB = R32(mo + 0x8B4);
    const uint32_t o0 = mo + 0xB0, o1 = mo + 0xB0 + 0x120;
    const int map = RoundMap();
    Log("tsne: mission objects %08x on map %d: %u objects (the GA-KO %06x at %.0f %.0f %.0f), goals %u/%u", mo, map, n,
        R32(o0 + 4) & 0xFFFFFF, F32(o0 + 0x90), F32(o0 + 0x94), F32(o0 + 0x98), R32(mo + 0x8B0), nB);
    if (n != 1 || nB != 1 || R32(mo + 0x8B0)) { Log("tsne: not RESCUE's one GA-KO and one goal of side B: left as it is"); gTsneSideDone = true; return; }
    const uint32_t g0 = mo + 0x670, g1 = mo + 0x670 + 0xC0;
    TsnePoint gako{F32(o0 + 0x90), F32(o0 + 0x94), F32(o0 + 0x98), true}, goal{F32(g0 + 0x90), F32(g0 + 0x94), F32(g0 + 0x98), true};
    uint32_t keroAt = kTsneKerotanCode, goal2At = kTsneGoalCode;
    TsnePoint kero = TsneLookup(c, keroAt), goal2 = TsneLookup(c, goal2At);
    if (!kero.ok && (map == 13 || map == 14)) {          // the extra maps' KEROTAN when their points.txt has none
        static const float kExtraKerotan[2][3] = {{18000, -6000, -23000}, {-5350, -1000, 8550}};   // 13 SILO, 14 REX: on a floor
        const float* k = kExtraKerotan[map - 13];
        kero = TsnePoint{k[0], k[1], k[2], true}; keroAt = 0;
    }
    if (!goal2.ok && (map == 13 || map == 14)) {         // and their second goal (docs/FAIRNESS.md)
        static const float kExtraGoal2[2][3] = {{25150, -6000, 10150}, {-850, 4000, -12850}};   // 13 SILO, 14 REX: on a floor
        const float* g = kExtraGoal2[map - 13];
        goal2 = TsnePoint{g[0], g[1], g[2], true}; goal2At = 0;
    }
    if (map >= 1 && map <= 12) {
        const TsneStagePoints& t = kTsneStages[map - 1];
        if (!kero.ok) {
            if (t.kerotan) kero = TsneLookup(c, keroAt = t.kerotan);
            else { kero = TsnePoint{t.kx, t.ky, t.kz, true}; keroAt = 0; }
        }
        if (!goal2.ok) {
            if (t.goal2) goal2 = TsneLookup(c, goal2At = t.goal2);
            else { goal2 = TsnePoint{t.gx, t.gy, t.gz, true}; goal2At = 0; }
        }
    }
    if (!kero.ok || !goal2.ok) TsneChoose(c, gako, goal, kero, keroAt, goal2, goal2At);
    if (kero.ok) {
        W32(mo + 0x410, 2);
        W32(o1 + 4, 0x59781F);                             // kerotan
        WF32x(o1 + 0x90, kero.x); WF32x(o1 + 0x94, kero.y); WF32x(o1 + 0x98, kero.z); WF32x(o1 + 0x9C, 1.0f);
        Log("tsne: the KEROTAN at %06x (%.0f %.0f %.0f)", keroAt, kero.x, kero.y, kero.z);
    } else Log("tsne: no KEROTAN point on this stage: the GA-KO alone");
    if (goal2.ok) {
        W32(mo + 0x8B4, 2);
        WF32x(g1 + 0x90, goal2.x); WF32x(g1 + 0x94, goal2.y); WF32x(g1 + 0x98, goal2.z); WF32x(g1 + 0x9C, 1.0f);
        Log("tsne: the second goal at %06x (%.0f %.0f %.0f)", goal2At, goal2.x, goal2.y, goal2.z);
    } else Log("tsne: no second goal point on this stage: RESCUE's goal alone");
}
// the round's sides, once the HUD gives its number (each frame and at the GAME TYPE page until settled)
void TsneSides(uint32_t g) {
    const uint32_t mo = gTsneObjects;
    if (gTsneSideDone || !g || mo < 0x100000 || mo + 0x8C0 >= 0x02000000) return;
    const int rn = (int)TsneRecordByte(g, 0xBC);
    if (rn < 1) return;
    gTsneRoundNo = rn;
    // the sides alternate each round of a map (its rounds count from 1) and each TSNE entry of the rotation starts
    // with the side the previous one did not (a map may end before its rounds +0xCC are played): the rotation's TSNE
    // entries before this one (key 0x36 the entry played), from the game's settings, which every player has
    const uint32_t d = ConfigData0();
    const int rounds = (int)TsneRecordByte(g, 0xCC);
    int before = 0;
    if (d >= 0x100000 && d + 0x40 < 0x02000000)
        for (uint32_t j = 0; j < R8(d + 0x36) && j < 16; ++j) if (R8(d + 3 + 2 * j) == kTsneRule) ++before;
    gTsneSwap = (before + rn) % 2 == 0;
    gTsneSideDone = true;
    const uint32_t k = R32(mo + 0x8B4);
    if (gTsneSwap && k && !R32(mo + 0x8B0) && k <= 3) {      // the red team sneaks: the goals are side A's
        for (uint32_t i = 0; i < k * 0xC0; i += 4) W32(mo + 0x430 + i, R32(mo + 0x670 + i));
        W32(mo + 0x8B0, k); W32(mo + 0x8B4, 0);
    }
    // the entry ends at a number of team victories (config 0x2D, RESCUE's NUMBER OF TEAM VICTORIES, key 0x4E), as RESCUE
    // (the supervisor's 0x3EBBF8 sets it, 0x3E9F6C ends the entry): the HUD record's +0xCC shows it
    const bool dOk = d >= 0x100000 && d + 0x50 < 0x02000000;
    Log("tsne: round %d (the entry ends at %u team victories; red %u, blue %u; the rotation's TSNE entries before it: %d): the %s team sneaks%s; entry %u",
        rn, dOk ? R8(d + 0x2D) : (uint32_t)rounds, dOk ? R8(d + 0x42) : TsneRecordByte(g, 0xC4), dOk ? R8(d + 0x46) : TsneRecordByte(g, 0xC8), before,
        gTsneSwap ? "red" : "blue", gTsneSwap ? " (the goals side A's, the RESCUE spawns swapped)" : "", dOk ? R8(d + 0x36) : 0xFF);
}
// the side that sneaks this round: the one whose goals the mission objects hold (a goal counts for a holder of its
// side: 0x39A818, team byte [player + 0x920] + 1 against the goal's side), RESCUE's B (team 1, the BLUE team)
int TsneSneakSide() {
    const uint32_t mo = gTsneObjects;
    if (mo < 0x100000 || mo + 0x8C0 >= 0x02000000) return 1;
    return R32(mo + 0x8B4) ? 1 : R32(mo + 0x8B0) ? 0 : 1;
}
uint32_t TsneSelf() { const int s = OwnSlot(); return s >= 0 ? PlayerOfSlot(s) : 0; }
int TsneTeamOf(uint32_t pl) {
    const uint32_t pt = pl ? R32(pl + 0x920) : 0;
    return pt >= 0x100000 && pt < 0x02000000 ? (int)R8(pt + 1) : -1;
}
bool TsneSneaker() { return TsneTeamOf(TsneSelf()) == TsneSneakSide(); }
// the sneakers' camouflage: MGO1's STEALTH CAMOUFLAGE (item 23, the optical camouflage), the item equipped being the
// link variables' + 0x5D6 (0x3388F8 turns its effect on and off as it changes). The game's equip (0x2FCC60) keeps the
// item before it as the last one (+ 0x5DA, its record [0x56E920]): L2's quick equip (0x377498) takes the held item off,
// or puts the last one back on when none is held. A sneaker's life starts with STEALTH on, held through the life's
// set-up (the game clears the item as the choice's menu closes) till he is 0.25 s in the map with the item window
// free; then his items are his own (the item window, the quick equip). Its battery (link variables + 0xA4E, full
// + 0xA50; 0x179200 drains it; each player's + 0xB6C its copy, 0x414840 for this machine's player; the item window's
// gauge) is kept full, but for the alert (an enemy's hit on any sneaker, TsneAlertTick): 0 while it lasts, the
// game's empty battery (STEALTH stays equipped, its effect off: TsneCamoFrame), full again at its end
void TsnePngAsk(const char* what, int frames);   // tests (SIM_TSNE_PNG)
bool TsneDecided();                              // the round's result is known here (no alert, no camouflage change)
const int kTsneStealth = 23;
long long gTsneAlertUntil = 0;                         // the alert's end (TsneAlertTick): every sneaker's camouflage inactive till then
struct TsneCamo {
    uint32_t player = 0; bool on = false, worn = false, alert = false;
    bool own = false; long long inMapAt = -1;            // the items his own (the set-up over); in the map since
    long long lifeAt = -1; size_t tests = 0, mates = 0;  // tests (TsneCamoTest)
    bool itemDone = false, itemGone = false, itemRefused = false; uint16_t itemCode = 0; long long itemEnd = -1, itemAlertAt = -1;
    int itemAsk = -1; long long itemAskAt = -1;           // an equip (code, 0 put away) asked of the item window (tests)
    double itemLifeS = 0;
    int padLife = -1, padPhase = 0, padMoves = 0; size_t padStep = 0;   // tests (SIM_TSNE_PAD)
    long long padAt = -1, padStart = -1, padHitAt = -1; uint16_t padItem = 0, padLast = 0;
};
uint32_t gTsnePad = ~0u;                               // tests: the pad bits SIM_TSNE_PAD holds now (~0: none)
TsneCamo gTsneCamo;
int gTsneItemLives = 0;                                // tests: the lives SIM_TSNE_ITEM has tested
void TsneCamoTest(const Ctx& c, TsneCamo& k, uint32_t me, uint32_t lv);
void TsneItemServe(const Ctx& c, bool window);
void TsneCamoTick(const Ctx& c) {
    TsneCamo& k = gTsneCamo;
    const uint32_t me = TsneSelf();
    const uint32_t lv = R32(V(0x23A678)) & 0x01FFFFFF;
    if (!TsneRound() || !TsneMapPage() || !me || lv < 0x100000 || lv + 0xA60 >= 0x02000000) { k = TsneCamo(); gTsnePad = ~0u; return; }
    const int life = (int16_t)R16(me + 0xB64);
    if (me != k.player || life <= 0 || !TsneSneaker()) {     // a new life, or not a sneaker: nothing held
        if (k.on && life > 0 && R16(lv + 0x5D6) == kTsneStealth) W16(lv + 0x5D6, 0);
        k = TsneCamo(); k.player = me; gTsnePad = ~0u;
        return;
    }
    if (k.lifeAt < 0) k.lifeAt = gFrameNo;
    if (TsneDecided()) {                                 // the round's end: as it is, an alert's empty battery full again
        gTsnePad = ~0u;
        const int16_t full = (int16_t)R16(lv + 0xA50);
        if (k.alert && full > 0) {
            Log("tsne: the alert's end (the round's result): this sneaker's battery %d -> %d (item %u)", (int16_t)R16(lv + 0xA4E), full, R16(lv + 0x5D6));
            W16(lv + 0xA4E, (uint16_t)full);
        }
        k.alert = false;
        return;
    }
    TsneCamoTest(c, k, me, lv);
    const uint16_t item = R16(lv + 0x5D6), last = R16(lv + 0x5DA);
    const bool alert = gFrameNo < gTsneAlertUntil;
    if (!k.own) {                                        // the life's set-up: STEALTH on, whatever the class or the game holds
        if (item != kTsneStealth && (item == 0 || !k.worn)) {
            W16(lv + 0x5D6, kTsneStealth);
            Log("tsne: this sneaker's camouflage on (%s)%s", !k.worn ? "the spawn" : "the life's set-up", alert ? ", inactive till the alert's end" : "");
            if (!k.worn) TsnePngAsk("item", 400);
        }
        k.worn = true;
        const uint32_t pause = PauseMenuWork(), gm = V(0x2C5238);   // a menu screen (NewHudPause + 0x326); the item window
        const bool menu = pause && pause + 0x340 <= 0x02000000 && R16(pause + 0x326) != 0;   // blocked (GM flags 2, 0x100)
        const bool blocked = gm && ((R32(gm + 0x18) | R32(gm + 0x1C)) & 0x102);
        if (menu || blocked) k.inMapAt = -1;
        else if (k.inMapAt < 0) k.inMapAt = gFrameNo;
        if (item == kTsneStealth && k.inMapAt >= 0 && gFrameNo >= k.inMapAt + (long long)(FieldRate() / 4)) {
            k.own = true;
            Log("tsne: this sneaker's items his own now (in the map, STEALTH held, his last item %u): the item window's and L2's", last);
        }
    }
    const int16_t full = (int16_t)R16(lv + 0xA50), bat = (int16_t)R16(lv + 0xA4E);
    const uint16_t held = R16(lv + 0x5D6);
    if (alert) {                                         // the alert: his battery empty, STEALTH kept (the gauge empty)
        if (bat != 0) W16(lv + 0xA4E, 0);
        if (!k.alert) {
            Log("tsne: the alert: this sneaker's camouflage inactive (his battery %d -> 0 of %d; item %u kept, his last item %u)", bat, full, held, last);
            TsnePngAsk("inactive", 45);
        }
        k.alert = true;
    } else {
        if (k.alert) {                                   // its end: the battery full again, the item as he holds it
            if (held == kTsneStealth) Log("tsne: the alert's end: this sneaker's camouflage works again (his battery %d -> %d; item %u, his last item %u)", bat, full, held, last);
            else Log("tsne: the alert's end: this sneaker holds item %u, his battery %d -> %d (his last item %u)", held, bat, full, last);
            if (held == kTsneStealth) TsnePngAsk("works", 45);
        }
        k.alert = false;
        if (full > 0 && bat < full) W16(lv + 0xA4E, (uint16_t)full);
    }
    k.on = held == kTsneStealth;
}
// 0x4511F8 (STEALTH's frame for each player holding it, every copy, on every machine: his NPL_NewOptcamouflage made
// while his + 0xB6C, the battery's copy, is not 0 (0x451108), ended at 0 (0x451180)): while an alert runs here a
// sneaker's copy is kept 0, the game's empty battery, and given back at its end (a copy's battery comes from the
// network only at his spawn, 0x403810 with flag 0x15F)
struct TsneCopyBat { uint32_t player = 0, gen = 0; int16_t kept = 0; bool zero = false, ended = false; };
TsneCopyBat gTsneCopyBat[16];
bool TsneCamoFrame(Ctx& c, uint32_t va) {
    const uint32_t pl = (uint32_t)c.r[4] & 0x01FFFFFF, w = (uint32_t)c.r[5] & 0x01FFFFFF;
    const uint32_t lv = R32(V(0x23A678)) & 0x01FFFFFF;
    if (!gTsneSideDone || pl < 0x100000 || pl + 0xF00 >= 0x02000000 || w < 0x100000 || w + 0x40 >= 0x02000000) return false;
    if (lv < 0x100000 || lv + 0xA60 >= 0x02000000 || TsneTeamOf(pl) != TsneSneakSide()) return false;
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    TsneCopyBat& b = gTsneCopyBat[R8(pl + 0xB60) & 15];
    if (b.player != pl || b.gen != gTsneGen) { b = TsneCopyBat(); b.player = pl; b.gen = gTsneGen; }
    const bool alert = gFrameNo < gTsneAlertUntil && !TsneDecided(), own = !(R32(pl + 0xB5C) & 1);
    const int16_t copy = (int16_t)R16(pl + 0xB6C);
    if (alert) {
        if (!b.zero) { b.zero = true; b.kept = copy; }
        if (copy) W16(pl + 0xB6C, 0);
    } else {                                             // no alert: a copy never empty (this player's follows his battery)
        b.zero = false;
        const int16_t full = (int16_t)R16(lv + 0xA50), back = b.kept > 0 ? b.kept : full;
        if (!own && copy <= 0 && back > 0) W16(pl + 0xB6C, (uint16_t)back);
    }
    const uint32_t was = R32(w + 0x30);
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    const uint32_t now = R32(w + 0x30);
    if (alert && was && !now) {
        b.ended = true;
        Log("tsne: sneaker %u's camouflage inactive here (the alert: his battery's copy %d -> 0, item %u kept; actor %08x ended)",
            R8(pl + 0xB60), copy, R8(pl + 0xE1D), was);
    } else if (!alert && b.ended && now) {
        b.ended = false;
        Log("tsne: sneaker %u's camouflage works again here (his battery's copy %d, item %u; actor %08x)", R8(pl + 0xB60),
            (int16_t)R16(pl + 0xB6C), R8(pl + 0xE1D), now);
    }
    return true;
}
bool TsnePlayerFlag(uint32_t pl, uint32_t f);
// this machine's player's item changed by the game's equip (n003a 0x2FCC60) in a TSNE round, logged with its caller:
// L2's quick equip (0x377498), the item window's choice (0x377720), the inventory daemon (0x2FE968: the item written,
// by TSNE or the game), or the game's own (a state taking it off: a box's 0x450A10 when the box flags 0xF0, 0xF5 go)
bool TsneEquipCall(Ctx& c, uint32_t va) {
    const uint32_t lv = R32(V(0x23A678)) & 0x01FFFFFF, me = TsneSelf();
    ps2::Fn f = ps2::FindFn(va);
    if (!f || lv < 0x100000 || lv + 0xA60 >= 0x02000000) return false;
    const uint16_t was = R16(lv + 0x5D6), wasLast = R16(lv + 0x5DA);
    const uint32_t ra = (uint32_t)c.r[31], us = gPageMap ? 0 : U(ra);
    f(c);
    c.r[31] = ra;
    const uint16_t now = R16(lv + 0x5D6), last = R16(lv + 0x5DA);
    if (now == was && last == wasLast) return true;
    const char* by = us == 0x377524 || us == 0x377604 ? "L2's quick equip" : us == 0x377768 || us == 0x3777AC ? "the item window's choice"
                   : us == 0x2FE9BC ? "the inventory daemon: the item written" : "the game";
    std::string flags;
    if (was >= 14 && was <= 17 && me) {
        char b[48]; snprintf(b, sizeof b, "; box flags f0 %d f5 %d 12 %d 13 %d", TsnePlayerFlag(me, 0xF0u), TsnePlayerFlag(me, 0xF5u),
                             TsnePlayerFlag(me, 0x12u), TsnePlayerFlag(me, 0x13u));
        flags = b;
    }
    Log("tsne: this %s's item %u -> %u (%s, the game's equip called from %08x), his last item %u%s", TsneSneaker() ? "sneaker" : "player",
        was, now, by, ra, last, flags.c_str());
    return true;
}
// the game's own damage (n003a 0x409188: the player, the attacker's slot, life, stun, its kind, +0xB75, +0xEF5; the
// record it sends makes the other machines apply it, 0x4367A0)
bool TsneDamage(const Ctx& c, uint32_t pl, int attacker, int life, int stun) {
    ps2::Fn f = ps2::FindFn(V3(0x409188));
    if (!f || pl < 0x100000 || pl + 0xF00 >= 0x02000000) return false;
    Ctx t = c;
    t.r[4] = pl; t.r[5] = (uint64_t)(int64_t)attacker; t.r[6] = (uint64_t)(int64_t)life; t.r[7] = (uint64_t)(int64_t)stun;
    t.r[8] = 0; t.r[9] = R8(pl + 0xB75); t.r[10] = 0;
    f(t);
    return true;
}
// the game's own equip (n003a 0x2FCC60, the item window's: a0 the item's record N3ItemDescs() + 0x38 x code, record 0
// none): + 0x5D6 the code, + 0x5DA the previous one, the record kept at [0x56E918] (tests only)
bool TsneEquipItem(const Ctx& c, uint32_t code) {
    ps2::Fn f = ps2::FindFn(V3(0x2FCC60));
    const uint32_t descs = N3ItemDescs();
    if (!f || !descs || code >= 64) return false;
    Ctx t = c;
    t.r[4] = descs + 0x38 * code;
    f(t);
    return true;
}
bool gTsneItemCheckCall = false;                       // TsneItemCheck's own call of 0x402848
// tests: the game's verdict on an item (n003a 0x402848(code) != 0: the item window's check 0x377960 takes it off) and
// a box's inputs to it: the node 0x1EB1FD under [[0x23C800] + 0x44] (+0x3C, +0x40 another code: refused) and the 19
// action flags of 0x1793C8's list (bits of 0x23C7AC)
std::string TsneItemCheck(const Ctx& c, uint32_t code) {
    static const int kBoxFlags[19] = {3, 0x3B, 0x52, 0x35, 0x7B, 0xA9, 0xAA, 0xA7, 0xA8, 0x5C, 0x5F, 0x60, 0x78, 0x79, 0x63, 0x64, 0x12, 0x6C, 0x6A};
    gTsneItemCheckCall = true;                           // TsneItemVerdict: not this call
    const uint32_t verdict = CallPage(c, 0x402848, code);
    gTsneItemCheckCall = false;
    const uint32_t pc = R32(V(0x23C800)) & 0x01FFFFFF;
    const uint32_t root = pc >= 0x100000 && pc + 0x48 < 0x02000000 ? R32(pc + 0x44) : 0;
    const uint32_t node = (root & 0x01FFFFFF) >= 0x100000 ? CallPage(c, 0x30EE68, root, 0x1EB1FD) & 0x01FFFFFF : 0;
    const bool nodeOk = node >= 0x100000 && node + 0x44 < 0x02000000;
    std::string set;
    const uint32_t fl = V(0x23C7AC);
    for (int n : kBoxFlags)
        if ((R32(fl + 4 * (uint32_t)(n >> 5)) >> (n & 31)) & 1) { char b[8]; snprintf(b, sizeof b, " %x", n); set += b; }
    char b[200];
    snprintf(b, sizeof b, "0x402848(%u) says %u; node %08x (+0x3C %08x, +0x40 %u); box flags set:%s", code, verdict, node,
             nodeOk ? R32(node + 0x3C) : 0, nodeOk ? R32(node + 0x40) : 0, set.empty() ? " none" : set.c_str());
    return b;
}
// tests (change the game's state): SIM_TSNE_SELF=s1,s2.. at those seconds of this sneaker's life a damage no enemy
// made, through the game's own damage: the odd ones his own (3000 off the stun gauge), the even ones the stage's
// (attacker -1, 5 off the life); SIM_TSNE_MATE=s: at s seconds he hits every other sneaker (10 off, a teammate's hit);
// SIM_TSNE_ITEM=code or a name's word (BOX), or a list (one a tested life): SIM_TSNE_ITEM_AT seconds (1) into his life's
// first alert, while another sneaker lives, he chooses that item as the item window does (from inside its act), and
// puts it away the same way 3 s after the alert's end once no alert runs
std::vector<double> TsneSecs(const char* name) {
    std::vector<double> v;
    if (const char* e = getenv(name)) for (const char* q = e; *q;) { v.push_back(atof(q)); q = strchr(q, ','); if (!q) break; ++q; }
    return v;
}
std::string TsneCString(uint32_t a);
// tests (SIM_TSNE_PAD=<steps>[;<steps> of his next sneaking life ..., the last for the later ones], a comma list): this
// sneaker's L2 as a player presses it (the test pad: ps2world::TsneTestPad, the harness's pad 0), from 1 s after his
// items are his own, in a life with an enemy in the game: "tap" L2 8 frames (the quick equip); "pick<code>" L2 held,
// the item window open, RIGHT till its choice (+0x54) is that item, L2 let go (the window's choice); "w<s>" s seconds;
// "alert[<s>]" till an alert runs (at most s seconds); "end" till none runs; "hit" an enemy's hit on him through the
// game's damage (10 off his life, the attacker an enemy's slot: the alert on every machine), "hits" one every 4 s
// till the life's end. Each step logged with the item and the last item before and after it; a 'pad' picture after
// each tap and pick
int gTsnePadLives = 0;
void TsnePadTest(const Ctx& c, TsneCamo& k, uint32_t lv) {
    static const char* env = getenv("SIM_TSNE_PAD");
    static std::vector<std::vector<std::string>> lives;
    gTsnePad = ~0u;
    if (!env || !k.own) return;
    if (lives.empty()) {
        std::vector<std::string> one; std::string w;
        for (const char* q = env;; ++q) {
            if (*q == ',' || *q == ';' || !*q) { if (!w.empty()) one.push_back(w); w.clear(); }
            else w += *q;
            if (*q == ';' || !*q) { lives.push_back(one); one.clear(); }
            if (!*q) break;
        }
    }
    if (k.padStart < 0) {                                // a life counts with an enemy in the game (not the host's solo life)
        const int team = TsneTeamOf(TsneSelf());
        bool enemy = false;
        for (const auto& a : gNativeActors)
            if ((a.second == "NewNetworkPlayer" || a.second == "NewNetworkDummyPlayer") && TsneTeamOf(a.first) >= 0 && TsneTeamOf(a.first) != team) enemy = true;
        k.padStart = gFrameNo + (long long)FieldRate();
        k.padLife = enemy ? std::min<int>(gTsnePadLives++, (int)lives.size() - 1) : -1;
        if (!enemy) Log("SIM_TSNE_PAD: no enemy in the game: this life not tested");
    }
    if (gFrameNo < k.padStart || k.padLife < 0) return;
    const std::vector<std::string>& steps = lives[(size_t)k.padLife];
    const size_t n = k.padStep;
    if (n >= steps.size()) return;
    const std::string& s = steps[n];
    const uint16_t item = R16(lv + 0x5D6), last = R16(lv + 0x5DA);
    const long long rate = (long long)FieldRate();
    if (k.padPhase == 0) {
        k.padItem = item; k.padLast = last; k.padPhase = 1; k.padAt = gFrameNo; k.padMoves = 0;
        Log("SIM_TSNE_PAD: life %d step %zu '%s' (item %u, his last item %u)", k.padLife + 1, n + 1, s.c_str(), item, last);
    }
    auto done = [&](const char* how) {
        Log("SIM_TSNE_PAD: life %d step %zu '%s' done%s (item %u -> %u, his last item %u -> %u)", k.padLife + 1, n + 1, s.c_str(), how,
            k.padItem, item, k.padLast, last);
        if (s == "tap" || s.compare(0, 4, "pick") == 0) TsnePngAsk("pad", 20);
        ++k.padStep; k.padPhase = 0;
    };
    const long long t = gFrameNo - k.padAt;
    uint32_t win = 0;
    for (const auto& a : gNativeActors) if (a.second == "NewItemChangeLinear") { win = a.first; break; }
    if (s == "tap") {
        if (t < 8) gTsnePad = 0x100;
        else if (t < 40) gTsnePad = 0;
        else done("");
    } else if (s.compare(0, 4, "pick") == 0) {
        const uint32_t code = (uint32_t)atoi(s.c_str() + 4);
        const uint32_t state = win ? R32(win + 0x48) : 0, choice = win ? R32(win + 0x54) : 0;
        if (k.padPhase == 1) {                           // L2 held till the window is open
            gTsnePad = 0x100;
            if (win && state == 2 && t >= 40) { k.padPhase = 2; k.padAt = gFrameNo; }
            else if (t > 150) { k.padPhase = 4; k.padAt = gFrameNo; Log("SIM_TSNE_PAD: the item window not open (state %u)", state); }
        } else if (k.padPhase == 2) {                    // open: its choice, or RIGHT
            gTsnePad = 0x100;
            if (state != 2) { if (t > 90) { k.padPhase = 4; k.padAt = gFrameNo; } }
            else if (choice == code || k.padMoves >= 12) {
                Log("SIM_TSNE_PAD: the item window's choice %u after %d moves: L2 let go", choice, k.padMoves);
                k.padPhase = 4; k.padAt = gFrameNo;
            } else { k.padPhase = 3; k.padAt = gFrameNo; }
        } else if (k.padPhase == 3) {                    // RIGHT 4 frames, then L2 alone
            gTsnePad = t < 4 ? 0x120 : 0x100;
            if (t >= 24) { ++k.padMoves; k.padPhase = 2; k.padAt = gFrameNo; }
        } else {                                         // let go: the window closes, its choice equipped
            gTsnePad = 0;
            if (t >= 60) done(item == code ? "" : " (not equipped)");
        }
    } else if (s[0] == 'w') {
        if (t >= (long long)(atof(s.c_str() + 1) * (double)rate)) done("");
    } else if (s.compare(0, 5, "alert") == 0) {
        if (gFrameNo < gTsneAlertUntil) done("");
        else if (s.size() > 5 && t >= (long long)(atof(s.c_str() + 5) * (double)rate)) done(" (no alert)");
    } else if (s == "hit" || s == "hits") {
        if (s == "hit" || gFrameNo >= k.padHitAt) {
            k.padHitAt = gFrameNo + 4 * rate;
            const uint32_t me = TsneSelf();
            int enemy = -1;
            for (const auto& a : gNativeActors)
                if ((a.second == "NewNetworkPlayer" || a.second == "NewNetworkDummyPlayer") && TsneTeamOf(a.first) >= 0 && TsneTeamOf(a.first) != TsneTeamOf(me)) {
                    enemy = (int)R8(a.first + 0xB60); break;
                }
            const bool hit = enemy >= 0 && TsneDamage(c, me, enemy, 10, 0);
            Log("SIM_TSNE_PAD: this sneaker hit by player %d through the game's damage%s", enemy, hit ? "" : " (not done)");
        }
        if (s == "hit") done("");
    } else if (s == "end") {
        if (gFrameNo >= gTsneAlertUntil) done("");
    } else done(" (unknown)");
}
void TsneCamoTest(const Ctx& c, TsneCamo& k, uint32_t me, uint32_t lv) {
    TsnePadTest(c, k, lv);
    static const std::vector<double> selfAt = TsneSecs("SIM_TSNE_SELF");
    static const char* itemList = getenv("SIM_TSNE_ITEM");  // a code, or a word of the item's short name (BOX); a list:
    std::string itemTok;                                 // one a tested life, the last for the later ones
    if (itemList) {
        const char* q = itemList;
        for (int n = 0; n < gTsneItemLives && strchr(q, ','); ++n) q = strchr(q, ',') + 1;
        itemTok.assign(q, strchr(q, ',') ? (size_t)(strchr(q, ',') - q) : strlen(q));
    }
    const char* itemEnv = itemList ? itemTok.c_str() : nullptr;
    const uint32_t descs = N3ItemDescs();
    auto nameOf = [&](uint32_t code) { return descs && code < 64 ? TsneCString(R32(descs + 0x38 * code) & 0x01FFFFFF) : std::string(); };
    int item = 0;
    if (itemEnv && isdigit((unsigned char)*itemEnv)) item = atoi(itemEnv);
    else if (itemEnv)
        for (uint32_t i = 0; i < std::min<uint32_t>(R32(lv + 0x5B0), 8) && !item; ++i) {
            const uint16_t e = R16(lv + 0x5C4 + 2 * i);
            if (e && e != kTsneStealth && nameOf(e).find(itemEnv) != std::string::npos) item = e;
        }
    if (itemEnv && !item)                                // no name found: 14 (a soldier's C BOX) or his first other item
        for (uint32_t i = 0; i < std::min<uint32_t>(R32(lv + 0x5B0), 8); ++i) {
            const uint16_t e = R16(lv + 0x5C4 + 2 * i);
            if (e == 14) { item = 14; break; }
            if (e && e != kTsneStealth && !item) item = e;
        }
    const double s = (double)(gFrameNo - k.lifeAt) / FieldRate();
    if (k.tests < selfAt.size() && s >= selfAt[k.tests]) {
        const bool own = k.tests % 2 == 0;
        TsneDamage(c, me, own ? (int)R8(me + 0xB60) : -1, own ? 0 : 5, own ? 3000 : 0);
        Log("SIM_TSNE_SELF: this sneaker damaged by %s (%.1f s of his life)", own ? "himself (3000 off his stun)" : "the stage (5 off his life)", s);
        ++k.tests;
    }
    static const std::vector<double> mateAt = TsneSecs("SIM_TSNE_MATE");
    if (k.mates < mateAt.size() && s >= mateAt[k.mates]) {
        int n = 0;
        for (const auto& a : gNativeActors)
            if (a.second == "NewNetworkPlayer" && a.first != me && TsneTeamOf(a.first) == TsneSneakSide() && (int16_t)R16(a.first + 0xB64) > 10)
                n += TsneDamage(c, a.first, (int)R8(me + 0xB60), 10, 0);
        Log("SIM_TSNE_MATE: this sneaker hits %d teammates (10 off, %.1f s of his life)", n, s);
        ++k.mates;
    }
    const long long rate = (long long)FieldRate();
    const uint32_t heldRec = N3HeldDesc() ? R32(N3HeldDesc() + 4) : 0;   // the game's record of the equipped item
    if (k.itemCode && !k.itemGone && k.itemAsk < 0 && R16(lv + 0x5D6) != k.itemCode) {
        k.itemGone = true;
        Log("SIM_TSNE_ITEM: item %u gone before its time (now %u, the game's record %08x, %.1f s of his life, %s; %s)", k.itemCode,
            R16(lv + 0x5D6), heldRec, s, k.itemEnd < 0 ? "during the alert" : "after the alert's end", TsneItemCheck(c, k.itemCode).c_str());
    }
    if (k.itemCode && k.itemEnd < 0 && k.itemAsk < 0 && gFrameNo >= gTsneAlertUntil) k.itemEnd = gFrameNo;
    if (k.itemCode && k.itemEnd >= 0 && k.itemAsk < 0 && gFrameNo >= k.itemEnd + 3 * rate && gFrameNo >= gTsneAlertUntil) {
        if (R16(lv + 0x5D6) == k.itemCode) { k.itemAsk = 0; k.itemAskAt = gFrameNo; }   // put away, no alert running
        else k.itemCode = 0;
    }
    if (k.itemAsk >= 0 && gFrameNo - k.itemAskAt > 20) TsneItemServe(c, false);   // the item window's act not run
    static const double itemAt = getenv("SIM_TSNE_ITEM_AT") ? atof(getenv("SIM_TSNE_ITEM_AT")) : 1.0;
    if (gFrameNo < gTsneAlertUntil && k.itemAlertAt < 0) k.itemAlertAt = gFrameNo;
    if (gFrameNo >= gTsneAlertUntil && !k.itemDone) k.itemAlertAt = -1;    // an alert passed without the test: the next one's start
    if (item && !k.itemDone && gFrameNo < gTsneAlertUntil && gFrameNo - k.itemAlertAt >= (long long)(itemAt * rate)) {
        int others = 0;
        for (const auto& a : gNativeActors)
            if (a.second == "NewNetworkPlayer" && a.first != me && TsneTeamOf(a.first) == TsneSneakSide() && (int16_t)R16(a.first + 0xB64) > 0) ++others;
        k.itemDone = true; ++gTsneItemLives;
        if (others) {
            Log("SIM_TSNE_ITEM: before the equip: %s", TsneItemCheck(c, (uint32_t)item).c_str());
            k.itemCode = (uint16_t)item; k.itemEnd = -1; k.itemAsk = item; k.itemAskAt = gFrameNo; k.itemLifeS = s;
        }
    }
}
// tests: the item SIM_TSNE_ITEM asked (k.itemAsk: a code, 0 put away) chosen as the player's item window chooses it
// (US n003a 0x377720, from inside its act: TsneMatchCall's 0x377960), or written when that act does not run within 20
// frames (the game's sync 0x2FE968 then equips it)
void TsneItemServe(const Ctx& c, bool window) {
    TsneCamo& k = gTsneCamo;
    const uint32_t lv = R32(V(0x23A678)) & 0x01FFFFFF;
    if (k.itemAsk < 0 || lv < 0x100000 || lv + 0xA60 >= 0x02000000) return;
    const uint32_t code = (uint32_t)k.itemAsk, was = R16(lv + 0x5D6);
    k.itemAsk = -1;
    uint32_t v = 0;
    if (window) v = CallPage(c, 0x377720, code);
    else W16(lv + 0x5D6, (uint16_t)code);
    const uint32_t descs = N3ItemDescs();
    const char* how = window ? "the item window's choice" : "a direct write (the item window's act not run)";
    if (code) {
        Log("SIM_TSNE_ITEM: this sneaker equips item %u (%s) through %s during the alert (%.1f s into it, %.1f s of his life; "
            "0x377720 says %u; item now %u, previous %u, the game's record %08x, its + 0x18 %d)", code,
            descs ? TsneCString(R32(descs + 0x38 * code) & 0x01FFFFFF).c_str() : "", how, (double)(gFrameNo - k.itemAlertAt) / FieldRate(),
            k.itemLifeS, v, R16(lv + 0x5D6), R16(lv + 0x5DA), N3HeldDesc() ? R32(N3HeldDesc() + 4) : 0,
            descs ? (int16_t)R16(descs + 0x38 * code + 0x18) : -1);
    } else {
        Log("SIM_TSNE_ITEM: this sneaker puts item %u away through %s (3 s after the alert's end, no alert running; item now %u)",
            was, how, R16(lv + 0x5D6));
        k.itemCode = 0;
    }
}
// tests: the game's check refusing the test's item (0x402848 != 0 for it): its inputs logged once an item
bool TsneItemVerdict(Ctx& c, uint32_t va) {
    TsneCamo& k = gTsneCamo;
    if (gTsneItemCheckCall || !k.itemCode || k.itemRefused || (uint32_t)c.r[4] != k.itemCode) return false;
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    if ((uint32_t)c.r[2]) {
        k.itemRefused = true;
        Log("SIM_TSNE_ITEM: the game's check refuses item %u (0x402848 says %u, called from %08x): %s", k.itemCode,
            (uint32_t)c.r[2], (uint32_t)ra, TsneItemCheck(c, k.itemCode).c_str());
    }
    return true;
}
// the sneakers' STEALTH held as the classes that own it hold it (Sokolov's): in the item list (link variables + 0x5C4,
// + 0x5B0 entries, the L2 window's) and its icon in the HUD's slots. The HUD loads the list's items' icons as the
// choice's menu opens (0x371BC0: entry i in slot i, then 0x371DB8 the script's own list over the first slots); an item
// only equipped (+0x5D6) had none (the item window's icon black). Once the round's sides are settled a sneaker's list
// gets STEALTH in a free entry, and its icon (its descriptor's + 0xC, 5f73b4.img in every rules page) is loaded in a
// free slot whenever no slot holds it, as the game loads its own (0x371AD0: the slot, the image 0x122F18(code |
// 0x69000000), the code); a defender's list never keeps it (Sokolov's own taken out too, Snake 6 October)
struct TsneStealthState { long long gen = -1; bool failed = false; };
TsneStealthState gTsneStealth;
int TsneIconSlot(uint32_t slots, uint32_t code) {      // the HUD's slot holding an icon (-1: none)
    for (int k = 0; k < 40; ++k) if (R16(slots + 8 * (uint32_t)k + 4) && R32(slots + 8 * (uint32_t)k) == code) return k;
    return -1;
}
void TsneStealthItem(const Ctx& c) {
    TsneStealthState& h = gTsneStealth;
    if (h.gen != (long long)gPageGen) { h = TsneStealthState(); h.gen = (long long)gPageGen; }   // a page loaded: the game's list
    gHudIconKept = 0;
    if (!TsneRound() || !TsneMapPage() || !gTsneSideDone) return;
    const uint32_t me = TsneSelf(), lv = R32(V(0x23A678)) & 0x01FFFFFF;
    if (!me || lv < 0x100000 || lv + 0xA60 >= 0x02000000) return;
    const uint32_t list = lv + 0x5C4, n = std::min<uint32_t>(R32(lv + 0x5B0), 8);
    int at = -1, free = -1;
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t e = R16(list + 2 * i);
        if (e == kTsneStealth && at < 0) at = (int)i;
        if (!e && free < 0) free = (int)i;
    }
    if (!TsneSneaker()) {                               // a defender: no STEALTH, whatever his character (Sokolov's too)
        if (R16(lv + 0x5D6) == kTsneStealth) W16(lv + 0x5D6, 0);
        for (uint32_t i = 0; i < n; ++i)
            if (R16(list + 2 * i) == kTsneStealth) {
                W16(list + 2 * i, 0);
                Log("tsne: the STEALTH item taken out of this defender's items (entry %u, character %u)", i, R8(me + 0xB62));
            }
        static uint32_t shot = 0;                        // tests: his item window, once a round
        if (shot != gTsneGen && (int16_t)R16(me + 0xB64) > 0) { shot = gTsneGen; TsnePngAsk("item", 400); }
        return;
    }
    if (at < 0 && free >= 0) {
        W16(list + 2 * (uint32_t)free, kTsneStealth);
        at = free;
        Log("tsne: the STEALTH item put in this sneaker's items (entry %d of %u)", free, n);
    } else if (at < 0 && Once(((uint64_t)143 << 40) | (uint64_t)gPageGen)) Log("tsne: this sneaker's %u items all taken: STEALTH only equipped", n);
    const uint32_t slots = N3HudIcons(), descs = N3ItemDescs();
    const uint32_t desc = descs ? descs + 0x38 * (uint32_t)kTsneStealth : 0;
    const uint32_t code = desc ? R32(desc + 0xC) : 0;
    if (!slots || !code || h.failed) return;
    gHudIconKept = code;
    if (TsneIconSlot(slots, code) >= 0) return;
    int slot = at >= 0 && at < 8 && !R16(slots + 8 * (uint32_t)at + 4) ? at : -1;
    for (int k = 0; k < 8 && slot < 0; ++k) if (!R16(slots + 8 * (uint32_t)k + 4)) slot = k;
    for (int k = 36; k < 40 && slot < 0; ++k) if (!R16(slots + 8 * (uint32_t)k + 4)) slot = k;
    const uint32_t img = slot >= 0 ? CallPage(c, 0x122F18, code | 0x69000000u) : 0;
    if (img) CallPage(c, 0x371AD0, (uint32_t)slot, img, code);
    if (!img || TsneIconSlot(slots, code) != slot) {
        h.failed = true;
        Log("tsne: the STEALTH icon %06x not loaded in the HUD (%s)", code, slot < 0 ? "no free slot" : !img ? "no such image in the page" : "not taken");
        return;
    }
    W32(desc + 0x10, CallPage(c, 0x371B38, code));     // as 0x371BC0 keeps it for the list's items
    Log("tsne: the STEALTH icon %06x loaded in the HUD's slot %d", code, slot);
}
// ---- a defender knocked out (his stun gauge emptied: a tranquilizer, a stun grenade, CQC) wakes up much later by
// himself: the knocked out recovery (n003a 0x414540, each tick the gauge + its maximum x (8 + the presses of the last
// 32 ticks) x the rate +0xB78 / 1 000 000; the rate 100 from the player's init, 0x4051C8) runs at a quarter of its rate
// while he defends. An ally's hit (a punch or a kick on a knocked out teammate, 0x4389C0: the gauge + its maximum x
// +0xB74 / 100 at once) is the game's own, unchanged: the fast way. Each machine for its own player (the gauge is his)
const int kTsneWakeSlow = 4;
const uint32_t kTsneKnockedOut = 0x99;                 // the state flag (the +0xB2C bitmap) of a knocked out player
bool TsnePlayerFlag(uint32_t pl, uint32_t f) {
    return pl >= 0x100000 && pl + 0xB60 < 0x02000000 && ((R32(pl + 0xB2C + 4 * (f >> 5)) >> (f & 31)) & 1);
}
struct TsneWake {
    uint32_t player = 0; uint16_t orig = 0, set = 0; bool slowed = false;
    bool out = false, alone = false; long long since = 0, lifeAt = -1, inMapAt = -1, logAt = 0; int16_t gauge = 0; int jump = 0; size_t kos = 0;
};
TsneWake gTsneWake;
void TsneWakeTick() {
    TsneWake& w = gTsneWake;
    const uint32_t me = TsneSelf();
    const bool on = TsneRound() && TsneMapPage() && gTsneSideDone && me >= 0x100000 && me + 0xB80 < 0x02000000;
    if (w.player && (w.player != me || !on)) {         // another player object, or out of a TSNE round: the rate given back
        if (w.slowed && w.player + 0xB80 < 0x02000000 && R16(w.player + 0xB78) == w.set) W16(w.player + 0xB78, w.orig);
        w = TsneWake();
    }
    if (!on) return;
    w.player = me;
    const uint16_t rate = R16(me + 0xB78);
    if (!w.slowed || rate != w.set) w.orig = rate;     // the game's own (a new init sets it again)
    const bool defend = !TsneSneaker();
    if (defend) {
        w.set = (uint16_t)std::max(1, w.orig / kTsneWakeSlow);
        if (rate != w.set) W16(me + 0xB78, w.set);
        w.slowed = true;
    } else if (w.slowed) { W16(me + 0xB78, w.orig); w.slowed = false; }
    const int16_t life = (int16_t)R16(me + 0xB64), gauge = (int16_t)R16(me + 0xB68), full = (int16_t)R16(me + 0xB6A);
    const double rate60 = FieldRate();
    if (life <= 0) { w.lifeAt = -1; w.inMapAt = -1; w.out = false; return; }
    if (w.lifeAt < 0) { w.lifeAt = gFrameNo; w.kos = 0; w.alone = false; }
    // tests (SIM_TSNE_KO=s1,s2.. and SIM_TSNE_KO_ROLE=defend|sneak): at those seconds of this player's life in the map he
    // takes a stun hit that empties his gauge (the pending stun damage +0xB72, which the next update applies as a hit's)
    static std::vector<double> koAt = [] {
        std::vector<double> v;
        if (const char* e = getenv("SIM_TSNE_KO")) for (const char* q = e; *q;) { v.push_back(atof(q)); q = strchr(q, ','); if (!q) break; ++q; }
        return v;
    }();
    static const char* koRole = getenv("SIM_TSNE_KO_ROLE");
    bool mate = false;                                   // defend2: a defending teammate alive beside him
    for (const auto& a : gNativeActors)
        if (a.second == "NewNetworkPlayer" && a.first != me && TsneTeamOf(a.first) == TsneTeamOf(me) && (int16_t)R16(a.first + 0xB64) > 0) mate = true;
    const bool roleOk = !koRole || !strcmp(koRole, "any") || (!strcmp(koRole, "defend2") ? defend && mate : (!strcmp(koRole, "defend") == defend));
    // SIM_TSNE_KO_ALONE=s: a defender with no teammate knocked out at s seconds of his life (every defender out)
    static const std::vector<double> aloneAt = TsneSecs("SIM_TSNE_KO_ALONE");
    if (!aloneAt.empty() && defend && !mate && !w.out && !w.alone && w.inMapAt >= 0 && gFrameNo >= w.inMapAt + (long long)(aloneAt[0] * rate60)) {
        W16(me + 0xB72, (uint16_t)std::min<int>(0x7FFF, std::max<int>(1, gauge + 1)));
        Log("SIM_TSNE_KO_ALONE: this defender, alone, takes a stun hit of %d (%.1f s of his life)", gauge + 1, aloneAt[0]);
        w.alone = true;
    }
    const uint32_t pause = PauseMenuWork();              // in the map since (no menu screen: NewHudPause +0x326 0)
    if (pause && pause + 0x340 <= 0x02000000 && R16(pause + 0x326) != 0) w.inMapAt = -1;
    else if (w.inMapAt < 0) w.inMapAt = gFrameNo;
    if (w.kos < koAt.size() && roleOk && !w.out && w.inMapAt >= 0 && gFrameNo >= w.inMapAt + (long long)(koAt[w.kos] * rate60)) {
        W16(me + 0xB72, (uint16_t)std::min<int>(0x7FFF, std::max<int>(1, gauge + 1)));
        Log("SIM_TSNE_KO: this %s takes a stun hit of %d (%.1f s of his life)", defend ? "defender" : "sneaker", gauge + 1, koAt[w.kos]);
        ++w.kos;
    }
    // tests (SIM_TSNE_BARE=1, changes the game's state): a defender beside whom a teammate lies knocked out puts his
    // weapon away (bare hands: the link variables' + 0x5D4 0, as the weapon window does), once a knock out
    static const bool bare = getenv("SIM_TSNE_BARE") != nullptr;
    static uint32_t bareFor = 0;
    const uint32_t lvw = R32(V(0x23A678)) & 0x01FFFFFF;
    if (bare && defend && lvw >= 0x100000 && lvw + 0x5E0 < 0x02000000) {
        uint32_t ko = 0;
        for (const auto& act : gNativeActors)
            if (act.second == "NewNetworkPlayer" && act.first != me && TsneTeamOf(act.first) == TsneTeamOf(me) && TsnePlayerFlag(act.first, kTsneKnockedOut)) ko = act.first;
        if (!ko) bareFor = 0;
        else if (bareFor != ko && R16(lvw + 0x5D4)) {
            Log("SIM_TSNE_BARE: this defender puts his weapon %u away beside his knocked out teammate %u", R16(lvw + 0x5D4), R8(ko + 0xB60));
            W16(lvw + 0x5D4, 0); bareFor = ko;
        }
    }
    // the log: knocked out, his gauge's rise, awake again (by an ally's hit: the gauge up a quarter or more at once)
    const bool out = TsnePlayerFlag(me, kTsneKnockedOut);
    if (w.out && full > 0 && gauge - w.gauge >= full / 5) w.jump = gauge - w.gauge;   // a hit's wake (a kick's: a fifth)
    if (out && !w.out) {
        w.since = gFrameNo; w.jump = 0; w.logAt = gFrameNo;
        Log("tsne: this %s knocked out (stun gauge %d of %d): the recovery's rate %u (the game's %u)%s", defend ? "defender" : "sneaker",
            gauge, full, R16(me + 0xB78), w.orig, defend ? ": his natural wake-up 4 times slower" : "");
        TsnePngAsk("ko", 90);
    } else if (out) {
        static const bool watch = getenv("SIM_TSNE_WATCH") != nullptr;
        if (watch && gFrameNo - w.logAt >= (long long)(5 * rate60)) {
            w.logAt = gFrameNo;
            Log("tsne watch: knocked out %.1f s, stun gauge %d of %d", (double)(gFrameNo - w.since) / rate60, gauge, full);
        }
    } else if (w.out) {
        Log("tsne: this %s awake again after %.1f s (%s)", defend ? "defender" : "sneaker", (double)(gFrameNo - w.since) / rate60,
            w.jump ? "woken by an ally's hit: his gauge up at once" : "the natural recovery");
        TsnePngAsk("wake", 20);
    }
    w.out = out; w.gauge = gauge;
}
uint32_t TsneSupervisor();
uint32_t TsneAnnouncer();
// SIM_TSNE_WATCH=1 (tests): the mission objects each second (holder, state, place) and the local player's team
void TsneWatch() {
    static const bool on = getenv("SIM_TSNE_WATCH") != nullptr;
    static long long last = 0;
    if (!on || !gTsneObjects || gFrameNo - last < 60) return;
    last = gFrameNo;
    const uint32_t mo = gTsneObjects;
    if (R32(mo + 0x410) > 3) return;
    std::string s;
    for (uint32_t k = 0; k < R32(mo + 0x410); ++k) {
        const uint32_t o = mo + 0xB0 + 0x120 * k;
        char b[160];
        snprintf(b, sizeof b, " [%u %06x st %u holder %02x/%02x/%02x at %.0f %.0f %.0f d4 %08x e0 %u]", k, R32(o + 4) & 0xFFFFFF, R32(o + 0xD0),
                 R8(o + 0xF8), R8(o + 0xF9), R8(o + 0xFA), F32(o + 0x90), F32(o + 0x94), F32(o + 0x98), R32(o + 0xD4), R32(o + 0xE0));
        s += b;
    }
    const int self = OwnSlot();
    const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0, lv = R32(V(0x23A678)) & 0x01FFFFFF;
    const bool lvOk = lv > 0x100000 && lv + 0xA60 < 0x02000000;
    std::string items;                                    // the item list, and the HUD's slot of STEALTH's icon
    const uint32_t slots = N3HudIcons(), descs = N3ItemDescs();
    for (uint32_t i = 0; lvOk && i < std::min<uint32_t>(R32(lv + 0x5B0), 8); ++i) items += (i ? " " : "") + std::to_string(R16(lv + 0x5C4 + 2 * i));
    const int iconSlot = slots && descs ? TsneIconSlot(slots, R32(descs + 0x38 * (uint32_t)kTsneStealth + 0xC)) : -2;
    std::string copies;                                   // every player's copy here: slot team life stun character
    for (const auto& a : gNativeActors)
        if (a.second == "NewNetworkPlayer" || a.second == "NewNetworkDummyPlayer") {
            char b[64];
            snprintf(b, sizeof b, " %u:%d:%d:%d:%u", R8(a.first + 0xB60), TsneTeamOf(a.first), (int16_t)R16(a.first + 0xB64), (int16_t)R16(a.first + 0xB68), R8(a.first + 0xB62));
            copies += b;
        }
    const uint32_t sup = TsneSupervisor(), box = TsneAnnouncer();
    Log("tsne watch: objects%s; goals %u/%u; this player slot %d team %d (sneakers %d) life %d stun %d item %d weapon %d battery %d/%d; items [%s], STEALTH's icon in slot %d; copies%s; music %d, box %d",
        s.c_str(), R32(mo + 0x8B0), R32(mo + 0x8B4), self, TsneTeamOf(me), TsneSneakSide(), me ? (int)(int16_t)R16(me + 0xB64) : -1,
        me ? (int)(int16_t)R16(me + 0xB68) : -1, lvOk ? (int)R16(lv + 0x5D6) : -1, lvOk ? (int)R16(lv + 0x5D4) : -1,
        lvOk ? (int)(int16_t)R16(lv + 0xA4E) : -1, lvOk ? (int)(int16_t)R16(lv + 0xA50) : -1, items.c_str(), iconSlot, copies.c_str(),
        sup ? (int)R32(sup + 0xF0) : -1, box ? (int)R32(box + 0x3B8) : -1);
}
// the object a RESCUE message is about (TsneBaseCall): the one whose state or holder changed last
struct TsneObjSnap { uint32_t state = 0, holder = 0; };
TsneObjSnap gTsneSnap[2];
int gTsneLastObj = -1;
long long gTsneLastObjFrame = -1000;
int TsneObjChanged() {                                 // an object changed since the last pass (-1: none)
    const uint32_t mo = gTsneObjects;
    if (mo < 0x100000 || mo + 0x8C0 >= 0x02000000) return -1;
    int which = -1;
    for (uint32_t k = 0; k < 2 && k < R32(mo + 0x410); ++k) {
        const uint32_t o = mo + 0xB0 + 0x120 * k, st = R32(o + 0xD0), h = R32(o + 0xD4);
        if (st != gTsneSnap[k].state || h != gTsneSnap[k].holder) { gTsneSnap[k].state = st; gTsneSnap[k].holder = h; which = (int)k; }
    }
    if (which >= 0) { gTsneLastObj = which; gTsneLastObjFrame = gFrameNo; }
    return which;
}
bool TsneKerotanEvent() { TsneObjChanged(); return gTsneLastObj == 1 && gFrameNo - gTsneLastObjFrame < 90; }
// ---- the radar's and the START menu map's marks. The mission objects' init registers them with the radar manager
// ([0x2E5228]: +0x6C the marks, +0x70 0x1C each: +0 the thing, +0xC its kind): the first object 0x2000, the second
// 0x4000, the first goal of side B 0x10000, of side A 0x8000, any other goal 0x40000 (an X). The radar (layout
// c7d4d9, n003a 0x5204E8) and the map (d2bbc0, 0x3AC898) draw each kind with one group of their layout: 0x2000 by the
// rule (RESCUE 83d782 the GA-KO, CAPTURE 83f890 the KEROTAN, SNEAKING 63d184 a film), 0x4000 a1b47c (a film, teal),
// 0x8000 73bba1 (circle 784aa2), 0x10000 83bba1 (circle 784aa4); each frame their circle takes its colour by an
// action (0x481B68: 516054 red, 516055 blue; SNEAKING's first goals blue). In a TSNE round the second object's group is
// CAPTURE's KEROTAN (the lookup of a1b47c answered with 83f890, unused in a RESCUE round), the first goal's kind is the
// sneakers' side's and the second goal's the other side's, and both circles take the sneakers' colour's action
// (TsneMatchCall)
bool gTsneGoalsB = true;                               // the sneakers' goals are side B's (blue): their circles blue
void TsneMarks() {
    const uint32_t mo = gTsneObjects;
    if (!TsneRound() || !TsneMapPage() || !gTsneSideDone || mo < 0x100000 || mo + 0x8C0 >= 0x02000000) return;
    const uint32_t at = PageData(0x399F90, 0, 0x2E5228), rm = at ? R32(at) & 0x01FFFFFF : 0;
    if (rm < 0x100000 || rm + 0x70 + 0x1C * 96 >= 0x02000000) return;
    const bool sideB = R32(mo + 0x8B4) != 0;
    gTsneGoalsB = sideB;
    const uint32_t n = std::min<uint32_t>(R32(mo + (sideB ? 0x8B4 : 0x8B0)), 2), base = mo + (sideB ? 0x670 : 0x430);
    const uint32_t marks = std::min<uint32_t>(R32(rm + 0x6C), 96);
    static uint32_t listed = 0;                          // tests (SIM_TSNE_WATCH): the radar's marks once a round
    if (listed != gTsneGen && getenv("SIM_TSNE_WATCH")) {
        listed = gTsneGen;
        std::string t;
        for (uint32_t i = 0; i < marks; ++i) {
            const uint32_t e = rm + 0x70 + 0x1C * i, thing = R32(e) & 0x01FFFFFF;
            auto it = gNativeActors.find(thing);
            char b[96];
            snprintf(b, sizeof b, " [%u %#x %08x %s]", i, R32(e + 0xC) & 0xFFFFF, thing, it != gNativeActors.end() ? it->second.c_str() : "-");
            t += b;
        }
        Log("tsne watch: the radar's %u marks:%s", marks, t.c_str());
    }
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t idx = R32(base + 0xC0 * k + 0xAC);
        if (idx >= marks) continue;
        const uint32_t e = rm + 0x70 + 0x1C * idx, kind = R32(e + 0xC);
        const uint32_t want = (k == 0) == sideB ? 0x10000u : 0x8000u;
        if ((kind & 0xFFFFF) != want) {
            W32(e + 0xC, (kind & ~0xFFFFFu) | want);
            Log("tsne: goal %u's mark %u made kind %#x (was %#x)", k, idx, want, kind & 0xFFFFF);
        }
    }
}
// the film group (a1b47c) of the radar (NewOnlineRadar +0x44) and the START map (NewOnline2DMap +0x40): every lookup
// of it is answered with the KEROTAN's (TsneMatchCall), the set-up's too (US n003a 0x5212A0 hides each group: flag
// 0x400000 shown, 0x800000 hidden), so the film stayed shown at the layout's centre, on the player's arrow: hidden here
void TsneFilmMark(const Ctx& c) {
    if (!TsneRound() || !TsneMapPage() || !gTsneSideDone || !gTsneObjects || R32(gTsneObjects + 0x410) < 2) return;
    for (const auto& a : gNativeActors) {
        const uint32_t off = a.second == "NewOnlineRadar" ? 0x44 : a.second == "NewOnline2DMap" ? 0x40 : 0;
        if (!off || a.first < 0x100000 || a.first + off + 4 >= 0x02000000) continue;
        const uint32_t lay = R32(a.first + off) & 0x01FFFFFF;
        if (lay < 0x100000 || lay >= 0x02000000) continue;
        const uint32_t node = CallPage(c, 0x481DA0, lay, 0xA1B47C) & 0x01FFFFFF;
        if (node < 0x100000 || node + 0x14 >= 0x02000000) continue;
        const uint32_t f = R32(node + 0x10);
        if (!(f & 0x400000)) continue;
        W32(node + 0x10, (f & ~0x400000u) | 0x800000u);
        if (Once(((uint64_t)153 << 40) | (uint64_t)gTsneGen << 8 | (off & 0xFF)))
            Log("tsne: the %s's film mark (a1b47c) hidden (the KEROTAN's is drawn in its place)", off == 0x44 ? "radar" : "START map");
    }
}
// tests (SIM_TSNE_PNG=1): a picture of each TSNE moment (gs_<frame>_<what>.png: a host list row of a TSNE game, the
// GAME TYPE page, a sneaker's spawn and his item window, the alert's display, the START menu's map, the round's
// result), the GS drawing from the request on; with SIM_GS_DRAW_FROM a run draws nothing else
struct TsnePng { long long at; std::string what; };
std::vector<TsnePng> gTsnePngs;
bool TsnePngOn() { static const bool on = getenv("SIM_TSNE_PNG") != nullptr; return on; }
bool TsnePngWants(const char* what) {                  // SIM_TSNE_PNG=1: every moment; else a list (item,map,...)
    static const char* e = getenv("SIM_TSNE_PNG");
    if (!e) return false;
    if (!strcmp(e, "1")) return true;
    const size_t n = strlen(what);
    for (const char* q = strstr(e, what); q; q = strstr(q + n, what))
        if ((q == e || q[-1] == ',') && (q[n] == 0 || q[n] == ',')) return true;
    return false;
}
const long long kTsnePngLead = 45;                     // the frames drawn before a picture (the GS's picture complete)
void TsnePngAsk(const char* what, int frames) {
    if (!TsnePngWants(what) || gTsnePngs.size() >= 8) return;
    gTsnePngs.push_back({gFrameNo + frames, what});
}
void TsnePngTick() {
    if (gTsnePngs.empty()) return;
    bool draw = false;
    for (size_t k = 0; k < gTsnePngs.size();) {
        if (gFrameNo < gTsnePngs[k].at) { draw |= gFrameNo >= gTsnePngs[k].at - kTsnePngLead; ++k; continue; }
        char name[96];
        snprintf(name, sizeof name, "gs_%05lld_%s.png", gFrameNo, gTsnePngs[k].what.c_str());
        Log("tsne: picture %s %s", name, ps2::GsWritePng(name) ? "written" : "not written");
        gTsnePngs.erase(gTsnePngs.begin() + (long)k);
    }
    static const long long from = getenv("SIM_GS_DRAW_FROM") ? atoll(getenv("SIM_GS_DRAW_FROM")) : 0;
    if (draw) ps2::gGsDraw = true;
    else if (from && gFrameNo < from) ps2::gGsDraw = false;
}
void TsnePngMap() {                                    // the START menu's map opened (alive: not the GAME TYPE page): once a round
    static uint32_t gen = 0;
    const uint32_t pause = TsnePngOn() && TsneRound() ? PauseMenuWork() : 0, me = pause ? TsneSelf() : 0;
    if (gen != gTsneGen && pause && pause + 0x340 <= 0x02000000 && R16(pause + 0x326) == 6 && me && (int16_t)R16(me + 0xB64) > 0) {
        gen = gTsneGen; TsnePngAsk("map", 40);
    }
}
void TsneAlertTick(const Ctx& c);
void TsneCjkTick();
void TsneEntryEnd();
void TsneTick(const Ctx& c) {                          // a pass of the actor lists
    if (TsneRound() && TsneMapPage()) { TsneSides(TsneHud()); TsneObjChanged(); TsneEntryEnd(); }
    TsneAlertTick(c); TsneCamoTick(c); TsneStealthItem(c); TsneWakeTick(); TsneMarks(); TsneFilmMark(c); TsneCjkTick(); TsneWatch(); TsnePngMap(); TsnePngTick();
}

// ---- the dialect: a game whose rotation holds TSNE is played in a set of its own (the PCs' or the extra maps' set
// with kTsneMark mixed in): no PS2, PCSX2 or MGO1.exe without TSNE gets in (docs/BIG_GAMES.md, docs/EXTRA_MAPS.md)
int TsneSet(int base) {
    if (base < 2 || (size_t)base >= gSets.size()) base = 2;
    if (gSets[(size_t)base].tsne) return base;
    for (size_t r = 3; r < gSets.size(); ++r)
        if (gSets[r].tsne && gSets[r].maps == gSets[(size_t)base].maps) return (int)r;
    DialectSet s = gSets[(size_t)base];
    s.key ^= kTsneMark;
    s.mac ^= kTsneMark * 0x9E3779B1u + 0x2545F491u;
    s.name += " (TEAM SNEAKING)";
    s.tsne = true;
    gSets.push_back(s);
    return (int)gSets.size() - 1;
}
bool TsneInRotation(uint32_t rules, uint32_t maps) {    // the Create Game work's rotation (+0x284 rules, +0x294 maps)
    for (int i = 0; i < 16; ++i) { if (!R8(maps + i)) break; if (R8(rules + i) >= kTsneRule) return true; }   // any PC-only rule
    return false;
}

// ---- the texts of the match (US n003a): the rule's name is the HUD text 0x3AFBA0(0x22 + rule) (the GAME TYPE page's
// title through 0x3C82B8, the START menu's MISSION INFO); its explanation the script's string 0x125408(L + 0x1D + rule),
// L the game record's +0xDC (the GAME TYPE page through 0x3C8238): RESCUE's give TSNE's in a TSNE round. The scripts
// hold English and one more (the North American program Spanish, the European French, the Japanese and Korean their
// own); TSNE's texts follow RESCUE's there, German and Italian the HUD's language over the European English ones, the
// Japanese ones spliced from the page's own glyphs. The scripts' accented letters are \x1F and (Mac Roman - 0x7F):
// the base's font (0x1202FC); the page's font holds only its kanji and hangul (0x8Cxx, 0x8Dxx: 24x24 glyphs in order)
const char* kTsneName = "TEAM SNEAKING";
bool TsneVersionKr() { const char* v = ps2::SelectedVersion(); return v && !strcmp(v, "KR"); }
bool TsneVersionJp() { const char* v = ps2::SelectedVersion(); return v && (!strcmp(v, "JP") || !strcmp(v, "JPS")); }
enum { kTsneEn, kTsneFr, kTsneEs, kTsneDe, kTsneIt, kTsneJp, kTsneLangs };
const char* kTsneHelp[5] = {                         // four lines, as RESCUE's (the page shows four)
    "Two teams take turns to sneak in and to defend.\nThe camouflaged Sneaking Team must take the GA-KO or\n"
    "the Kerotan to a goal, or put every defender out. The\nDefense Team wins by putting them out or at time up.\n",
    "Les \x1F\x0Fquipes s'infiltrent et d\x1F\x0F" "fendent \x1F\x09 tour de r\x1F\x1A" "le.\nCamoufl\x1F\x0F" "e, l'\x1F\x0F" "quipe d'infiltration gagne si elle am\x1F\x10ne\n"
    "le GA-KO ou le Kerotan \x1F\x09 un objectif ou \x1F\x0F" "limine la\nd\x1F\x0F" "fense, qui gagne en l'\x1F\x0F" "liminant ou en tenant bon.\n",
    "Los equipos se turnan para infiltrarse y defender.\nEl Equipo de Infiltraci\x1F\x18n, camuflado, gana si lleva el\n"
    "GA-KO o el Kerotan a una meta o elimina a la defensa.\nLa defensa gana si lo elimina o resiste hasta el final.\n",
    "Zwei Teams schleichen und verteidigen im Wechsel.\nDas getarnte Schleichteam bringt GA-KO oder Kerotan\n"
    "zu einem Ziel oder schaltet alle Verteidiger aus. Die\nVerteidigung siegt durch Ausschalten oder Zeitablauf.\n",
    "Due squadre si alternano tra infiltrazione e difesa.\nLa squadra infiltrata, mimetizzata, porta il GA-KO o\n"
    "il Kerotan a un obiettivo o elimina tutti i difensori.\nLa difesa vince eliminandola o allo scadere del tempo.\n",
};
uint32_t gTsneHelpAt[kTsneLangs] = {};
uint32_t TsneString(uint32_t& at, const char* s) {
    if (!at) { at = Alloc((uint32_t)strlen(s) + 1); if (at) memcpy(gMem + at, s, strlen(s) + 1); }
    return at;
}
uint32_t TsneStringOf(const std::string& s) {          // a new copy in the PS2's memory (0: none)
    const uint32_t at = Alloc((uint32_t)s.size() + 1);
    if (at) memcpy(gMem + at, s.c_str(), s.size() + 1);
    return at;
}
std::string TsneCString(uint32_t a) {
    std::string t;
    for (; a >= 0x100000 && a < 0x02000000 && gMem[a] && t.size() < 400; ++a) t += (char)gMem[a];
    return t;
}
// the rule's name in the match's capitals and in the lobby's lists, a language each (English, French, German, Italian,
// Spanish: the HUD's tables 0x3AF7E8 by link variable 4, the lobby's by its own; their letters: h for É, p for Ó),
// told by RESCUE's own name there
const char* kTsneNames[5] = {"TEAM SNEAKING", "INFILTRATION PAR hQUIPE", "TEAM-SCHLEICHMISSION", "INFILTRAZIONE A SQUADRE",
                             "MISIpN FURTIVA EQUIPO"};
const char* kTsneRescueNames[5] = {"RESCUE MISSION", "MISSION DE SAUVETAGE", "RETTUNGSMISSION", "MISSIONE DI RECUPERO",
                                   "MISIpN RESCATE"};
uint32_t gTsneHudName[5] = {};
int TsneNameLanguage(const std::string& rescue) {
    for (int k = 0; k < 5; ++k) if (rescue == kTsneRescueNames[k]) return k;
    return 0;
}
// the HUD's language (link variables + 4: 2 French, 3 German, 4 Italian, 5 Spanish; English else, the Japanese and
// Korean programs' HUD texts too): 0 en, 1 fr, 2 de, 3 it, 4 es (the HUD tables' order)
int TsneHudLanguage() {
    const uint32_t lv = R32(V(0x23A678)) & 0x01FFFFFF;
    if (lv < 0x100000 || lv + 8 >= 0x02000000) return 0;
    const uint16_t l = R16(lv + 4);
    return l >= 2 && l <= 5 ? (int)l - 1 : 0;
}
// the Japanese (JP, JPS) and Korean texts in the codes of the rules pages' font file (383c45.raw, base 0x11F408: 194
// base glyphs 0x8101.. (JP kana and signs, KR the commonest syllables), then pages from 0x8400: JP the JIS level-1
// kanji, KR the jamo and the 2350 KS X 1001 syllables), resident in every match; JP flags 0x4000 (half width) and
// 0x2000 (an opening bracket) as the scripts have them. Made by tools/tsne_cjk.py
const char kTsneJpHelp[] = "\x82I\x82\x06\x82Q\x82)\x81\x14\x81(\x81+\x9a>\x9cz\x81(\x9e\x1d\x84\x90\x81P\x87" "E\x9a\xe7\x81\x19\x81K\x82!\x83\x12\x82@\x9a" "5\x81P\x87\x85\x81\x04\x81>\x81\x19\xc3\x09\n\x9et\x87\xdb\x81.\x9a>\x9cz\x82!\x83\x12\x82@\x81/\xa3\x16\x82\x0c\x83\x12\x82\x13\xc3\n\x81\x0b\xa3\x16\x82\x11\x82M\x82\x1f\x82Q\xc3\n\x81P\x82\x14\x83\x12\x82K\x81>\x81'\n\x84}\x81" "6\x81\x0b\x9e\x1d\x84\x90\x82!\x83\x12\x82@\x81P\x9aX\x9ey\x81\x15\x81\x1b\x81L\x81" "0\x99*\x9f\x09\x81'\x81\x19\xc3\x09\x9e\x1d\x84\x90\x82!\x83\x12\x82@\x81/\n\x9a>\x9cz\x82!\x83\x12\x82@\x81P\x9aX\x9ey\x81\x15\x81\x1b\x81K\x81\x0b\x99\xee\x87\x15\x98l\x85\xaf\x81>\x81'\x98\xb5\x81J\x81\x0d\x81L\x81" "0\x99*\x9f\x09\x81'\x81\x19\xc3\x09\n";   // 220 bytes
const char kTsneJpSneak[] = "\x82\x0c\x83\x12\x82\x13\x81\x0b\x82\x11\x82M\x82\x1f\x82Q\x81P\x82\x14\x83\x12\x82K\x81>\x81'\x98k\x81!\x85\xd9\x81L\xc3\x06";   // 38 bytes
const char kTsneJpDefend[] = "\x82\x0c\x83\x12\x82\x13\x81(\x82\x11\x82M\x82\x1f\x82Q\x81P\x98N\x98\xb5\x81\x1b\x81H\xc3\x06";   // 28 bytes
const char kTsneKrHelp[] = "\x81\x16\x82\x1c\x81" "D\x81<\x81\x02 \x9a\x85\x9ax\x81L \x82" "F\x81\n\x81\x15 \x84\xcf\x81\x18\x81\x09\x81\x05 \x9c\x9a\x81)\x81\x07 \x9d\x0e\x81&\x81\x02.\n\x82\x04\x81:\x81\x11 \x9a\x85\x9ax\x9c\x9a\x81\x0f \x81\x03\x9b\xf7\x81\x0d \x9b\xe6\x81\x13\x82%\x81\x07 \x84\xaf\x82J\x81\x04 \x82\x1c\x87\xec\x81\x09\x81" "5\x81\x0d\n\x82" "F\x81\n\x9c\x9a\x81\x07 \x81)\x87\x9b\x81%\x9c,\x81? \x99K\x81\x0c\x9d\x0e\x81&\x81\x02. \x82" "F\x81\n\x9c\x9a\x81\x0f \x9a\x85\x9ax\x9c\x9a\x81\x07\n\x81)\x87\x9b\x81%\x9c,\x81" "5\x81\x0d \x81" "4\x81\x11\x81%\x82@ \x82\x07\x81G \x81\x04\x9c,\x81? \x99K\x81\x0c\x9d\x0e\x81&\x81\x02.\n";   // 195 bytes
const char kTsneKrSneak[] = "\x81\x03\x9b\xf7\x81\x0d \x9b\xe6\x81\x13\x82%\x81\x07 \x9c" "7\x9b\xaa\x81\x09\x81\x12 \x81\x1e\x82" "2 \x81\x04\x9a\xa3\x81/\x81\x13 \x9d\x1b\x81\x09\x81\x16!";   // 46 bytes
const char kTsneKrDefend[] = "\x81\x03\x9b\xf7\x82\x18 \x9b\xe6\x81\x13\x82%\x81\x07 \x81\x04\x9b\xee\x81\x16!";   // 23 bytes
// In a match the resident font is the rules page's 383c45.raw: its 194 base glyphs only; the kanji and hangul pages
// (codes 0x8400 .., the lookup 0x11F350's pages 0, 5, 6) are the lobby's (r_menu_n's file). While the lobby is up
// (TsneLobby) the glyphs of the texts above are copied into a page of our own, the font's page 3 (codes 0x9001 ..,
// [0x23A5A0 + 12], which no page sets), pointed at during a TSNE round; the texts are given in its codes (TsneCjk)
const char* const kTsneCjkJp[] = {kTsneJpHelp, kTsneJpSneak, kTsneJpDefend};
const char* const kTsneCjkKr[] = {kTsneKrHelp, kTsneKrSneak, kTsneKrDefend};
const uint32_t kTsneCjkPage = 3, kTsneGlyph = 0x90;
struct TsneCjkFont {
    std::map<uint16_t, uint16_t> code;                 // a text's code (flags off) -> its code on page 3
    uint32_t buf = 0, was = 0;                         // the copy; page 3's word before it was pointed at
    bool ready = false, set = false;
};
TsneCjkFont gTsneCjk;
uint32_t TsneGlyphAt(uint32_t code) {                  // the glyph of a code in the font set up now (as 0x11F350)
    const uint32_t baseAt = PageData(0x11F350, 0, 0x284030), pagesAt = PageData(0x11F350, 1, 0x23A5A0);
    if (baseAt < 0x100000 || pagesAt < 0x100000 || baseAt >= 0x02000000 || pagesAt >= 0x02000000) return 0;
    const uint32_t c = code & ~0x6000u, hi = c >> 8;
    uint32_t tab = 0, idx = 0;
    if (hi == 0x81) { tab = R32(baseAt); idx = c - 0x8101; }
    else if (hi == 0x82) { tab = R32(baseAt); idx = c - 0x81B0; }
    else if (hi == 0x83) { tab = R32(baseAt); idx = c - 0x825C; }
    else if (hi >= 0x84 && hi < 0xA0) {
        const uint32_t page = (c - 0x8400) >> 10, a = c - (page << 10) - 0x8401;
        tab = R32(pagesAt + 4 * page); idx = a - (a >> 8);
    } else return 0;
    tab &= 0x01FFFFFF;
    return tab >= 0x100000 && idx < 1024 && tab + kTsneGlyph * (idx + 1) < 0x02000000 ? tab + kTsneGlyph * idx : 0;
}
#define TSNE_CJK 1                                     // TsneCjkAdd / TsneCjkCopy / TsneCjk offered (basecap.h)
void TsneCjkAdd(const char* s) {                       // a text's codes given a place on page 3 (other rules' texts too)
    TsneCjkFont& f = gTsneCjk;
    for (const uint8_t* p = (const uint8_t*)s; *p; ++p)
        if (*p >= 0x80 && p[1]) {
            const uint16_t c = (uint16_t)((p[0] << 8 | p[1]) & ~0x6000);
            if (!f.code.count(c) && f.code.size() < 1020) {
                const uint32_t k = (uint32_t)f.code.size();
                f.code[c] = (uint16_t)(((0x84 + 4 * kTsneCjkPage + k / 255) << 8) | (k % 255 + 1));
                f.ready = false;
            }
            ++p;
        }
}
void TsneCjkCopy() {                                   // the lobby's font: the texts' glyphs copied (JP, JPS, KR)
    TsneCjkFont& f = gTsneCjk;
    if (!(TsneVersionJp() || TsneVersionKr())) return;
    static bool own = false;
    if (!own) { own = true; for (const char* s : TsneVersionKr() ? kTsneCjkKr : kTsneCjkJp) TsneCjkAdd(s); }
    if (f.ready || f.code.empty()) return;
    static uint32_t room = 0;
    if (f.code.size() > room) {
        const uint32_t was = f.buf;
        room = (uint32_t)f.code.size() + 32;
        f.buf = Alloc(kTsneGlyph * room);
        if (was) Free(was);
    }
    if (!f.buf) { room = 0; return; }
    int blank = 0, none = 0;
    for (const auto& e : f.code) {
        const uint32_t src = TsneGlyphAt(e.first), k = (e.second >> 8) - 0x84 - 4 * kTsneCjkPage, dst = f.buf + kTsneGlyph * (k * 255 + (e.second & 0xFF) - 1);
        if (!src) { ++none; continue; }
        bool any = false;
        for (uint32_t b = 0; b < kTsneGlyph && !any; ++b) any = gMem[src + b] != 0;
        blank += !any;
        memcpy(gMem + dst, gMem + src, kTsneGlyph);
    }
    f.ready = !blank && !none;
    Log("tsne: the JP/KR texts' %zu glyphs %s from the lobby's font into page %u (%d blank, %d not found)", f.code.size(),
        f.ready ? "copied" : "NOT copied", kTsneCjkPage, blank, none);
}
std::string TsneCjk(const char* s) {                   // a JP/KR text in page 3's codes ("": the copy not made)
    const TsneCjkFont& f = gTsneCjk;
    if (!f.ready) return std::string();
    std::string t;
    for (const uint8_t* p = (const uint8_t*)s; *p; ++p) {
        if (*p < 0x80 || !p[1]) { t += (char)*p; continue; }
        const uint16_t c = (uint16_t)(p[0] << 8 | p[1]), flags = c & 0x6000;
        const auto it = f.code.find((uint16_t)(c & ~0x6000));
        const uint16_t n = it != f.code.end() ? (uint16_t)(it->second | flags) : c;
        t += (char)(n >> 8); t += (char)(n & 0xFF);
        ++p;
    }
    return t;
}
void TsneCjkTick() {                                   // page 3 pointed at the copy while a map's page is loaded
    TsneCjkFont& f = gTsneCjk;
    const bool want = f.ready && TsneMapPage();
    if (!want && !f.set) return;
    const uint32_t pagesAt = PageData(0x11F350, 1, 0x23A5A0);
    if (pagesAt < 0x100000 || pagesAt + 0x20 >= 0x02000000) return;
    const uint32_t slot = pagesAt + 4 * kTsneCjkPage;
    if (want && R32(slot) != f.buf) {
        if (!f.set) { f.was = R32(slot); Log("tsne: the font's page %u pointed at the JP/KR texts' glyphs (was %08x)", kTsneCjkPage, f.was); }
        W32(slot, f.buf); f.set = true;
    } else if (!want && f.set) { W32(slot, f.was); f.set = false; }
}

// ---- the alert (MGO2's): a sneaker hit by an enemy (TsneDamageEvent) starts an alert of kTsneAlertSecs for everyone,
// renewed while defenders keep hitting sneakers: every sneaker's camouflage inactive till it ends (his battery empty,
// STEALTH kept: TsneCamoTick, TsneCamoFrame), the
// game's own alert music while it lasts (the supervisor's BGM state +0xF0: 1 as SNEAKING sets it when Snake is hurt,
// 0x3F1350 -> 0x3F2B18; the BGM manager 0x3F2FD0 plays its set's alert piece for 1 and 2), and the game's own ALERT
// display with its countdown while it lasts: MGS3's phase gauge (TsneGauge), else ALERT and its seconds in the HUD's
// announcer box (the one of SNAKE FIRST DAMAGE
// and of the HEADSHOT rule, 0x3D78F0 / 0x3DA700: the object [0x2F0B8C], its layout +0x3AC, its timer +0x3B8 in the
// game's time units, the box node 3D12AE, the text node A5C8D6). The same box tells each player his role when his
// round starts (Snake's optional request: the HEADSHOT rule's box for TSNE's instructions)
const double kTsneAlertSecs = 5.0, kTsneRoleSecs = 4.0;
struct TsneAlert {
    long long until = 0;                               // the alert's end
    int shown = -1;                                    // the seconds shown
    bool music = false, roleShown = false, gauge = false;
    uint32_t gen = 0;                                  // the round (gTsneGen)
    uint32_t count = 0;                                // the alerts of the round
    long long aliveAt = -1;                            // this player alive since
    long long inMapAt = -1;                            // alive in the map (no menu screen) since
    size_t hitsDone = 0;                               // SIM_TSNE_HIT's hits made in this life
};
TsneAlert gTsneAlert;
// a sneaker hit by an enemy: the rules' event 4 (0x3F09D0(4, the attacker's slot, the victim's)), raised by the game's
// damage (0x409188, on the hitting machine) and by a damage record from the network (0x4367A0, on the others); a
// damage by himself, a teammate or the stage (attacker -1: a fall's trap) is no alert
struct TsneHit { int victim = -1, attacker = -1; uint32_t gen = 0; };
TsneHit gTsneHit;
void TsneDamageEvent(int attacker, int victim) {
    if (!TsneRound() || !gTsneSideDone || TsneDecided() || victim < 0 || victim >= 16) return;
    const uint32_t vp = PlayerOfSlot(victim);
    const int side = TsneSneakSide(), vt = vp ? TsneTeamOf(vp) : -1;
    if (vt != side) return;
    const bool other = attacker >= 0 && attacker < 16 && attacker != victim;   // another player (-1: the stage)
    const uint32_t ap = other ? PlayerOfSlot(attacker) : 0;
    const int at = ap ? TsneTeamOf(ap) : -1;
    if ((at >= 0 && at != vt) || (other && at < 0)) {   // a defender (no copy of him here: not a teammate either)
        gTsneHit = {victim, attacker, gTsneGen};
        if (at < 0 && Once(((uint64_t)156 << 40) | (uint64_t)gTsneGen << 8 | (uint64_t)attacker))
            Log("tsne: the attacker %d of sneaker %d has no copy here: taken as a defender", attacker, victim);
        return;
    }
    if (Once(((uint64_t)152 << 40) | (uint64_t)(gFrameNo / 30) << 8 | (uint64_t)victim))
        Log("tsne: sneaker %d damaged by %s (attacker %d): no alert, the camouflage kept", victim,
            attacker == victim ? "himself" : at == vt ? "a teammate" : "the stage", attacker);
}
// the game's damage on this machine (0x409188): a sneaker's logged with its source (its caller: US n003a 0x438614 a
// hit, 0x3FB3C0 a throw, 0x40934C his own (a fall), 0x3F5D04 a trap's kill)
void TsneDamageLog(const Ctx& c) {
    const uint32_t pl = (uint32_t)c.r[4] & 0x01FFFFFF;
    if (!TsneRound() || !gTsneSideDone || pl < 0x100000 || pl + 0xF00 >= 0x02000000 || TsneTeamOf(pl) != TsneSneakSide()) return;
    if (!Once(((uint64_t)154 << 40) | (uint64_t)(gFrameNo / 30) << 8 | R8(pl + 0xB60))) return;
    Log("tsne: damage on sneaker %u: attacker %d, life %d, stun %d, kind %#x, from %08x, at %.0f %.0f %.0f", R8(pl + 0xB60),
        (int)(int8_t)(c.r[5] & 0xFF), (int)(int32_t)c.r[6], (int)(int32_t)c.r[7], (uint32_t)c.r[8] & 0xFF, (uint32_t)c.r[31] - 8,
        F32(pl + 0x110), F32(pl + 0x114), F32(pl + 0x118));
}
// MGS3's phase gauge (NewPhaseGauge: n003a constructor 0x3C1578, init 0x3C1458, act 0x3C10A8, its work's global
// 0x2E5630): ALERT (phase 1, red), EVASION (2), CAUTION (3) and the time left, its layout by the HUD's language (link
// variables + 4: 3592cc English, 2 f47fd1, 3 f4823e, 4 f48c0d, 5 f4b38d) in every map's stage of the five discs. The
// online pages link it but never make it (no class entry); its act reads the phase from the GM status (base 0x2C5238
// + 0x30) and the time from + 0x64 (1/100 s, 9999 at most) each frame and shows or hides itself as the phase leaves or
// returns to 0; the online game never writes those two words (the stage's init clears them, 0x103600). TSNE makes one
// on each machine for its rounds. MGO1_TSNE_GAUGE=0: the announcer box instead
uint32_t TsneGm() { return PageData(0x3C10A8, 0, 0x2C5238); }
uint32_t gTsneGaugeTried = 0;                          // the round and alert a gauge was made for
uint32_t TsneGauge(const Ctx& c) {
    const uint32_t at = PageData(0x3C1668, 0, 0x2E5630);
    if (!at) return 0;
    uint32_t g = R32(at) & 0x01FFFFFF;
    if (g >= 0x100000 && g + 0x90 < 0x02000000) return g;
    static const bool off = getenv("MGO1_TSNE_GAUGE") && atoi(getenv("MGO1_TSNE_GAUGE")) == 0;
    const uint32_t tried = (gTsneGen << 12) | (gTsneAlert.count & 0xFFF);
    if (off || gTsneGaugeTried == tried) return 0;
    gTsneGaugeTried = tried;
    const uint32_t lv = R32(V(0x23A678)) & 0x01FFFFFF;
    if (lv < 0x100000 || lv + 8 >= 0x02000000) return 0;
    const int16_t lang = (int16_t)R16(lv + 4);
    static const uint32_t kLayouts[6] = {0x3592CC, 0x3592CC, 0xF47FD1, 0xF4823E, 0xF48C0D, 0xF4B38D};
    const uint32_t want = lang >= 0 && lang < 6 ? kLayouts[lang] : 0x3592CC;
    const bool own = CallPage(c, 0x122F18, want | 0x5F000000u) != 0;   // the stage's file of that layout
    if (!own && !CallPage(c, 0x122F18, 0x3592CC | 0x5F000000u)) { Log("tsne: no phase gauge's layout in this stage: the alert in the announcer box"); return 0; }
    if (!own) W16(lv + 4, 1);                          // the English one while it is made
    const uint32_t actor = CallPage(c, 0x3C1578, 0);
    if (!own) W16(lv + 4, (uint16_t)lang);
    g = R32(at) & 0x01FFFFFF;
    Log("tsne: the alert's display made (MGS3's phase gauge): actor %08x, work %08x, layout %06x%s", actor, g, own ? want : 0x3592CC,
        own ? "" : " (this stage lacks its language's: English)");
    return g >= 0x100000 && g + 0x90 < 0x02000000 ? g : 0;
}
void TsneGaugeSet(int phase, int centis) {             // the GM status's phase and time (the gauge's act shows them)
    const uint32_t gm = TsneGm();
    if (!gm) return;
    W32(gm + 0x30, (uint32_t)phase);
    W32(gm + 0x64, (uint32_t)std::max(0, std::min(9999, centis)));
}
uint32_t TsneSupervisor() {                            // NewOnlineGameSupervisor's work (its global 0x2F0C90)
    const uint32_t at = PageData(0x3ECBA8, 0, 0x2F0C90), s = at ? R32(at) & 0x01FFFFFF : 0;
    return s >= 0x100000 && s + 0x400 < 0x02000000 ? s : 0;
}
uint32_t gTsneResultGen = 0;                           // the round (gTsneGen) whose result text the HUD showed here
// the round decided: the supervisor's round record holds its result (+0x3BC: +4, the host's decider's; the others' from
// it) or its state (+0xC8, the table 0x557834: 0 the start, 1 the play, 2 the round's end, 3 the entry's end, 4 its
// results, 5 a restart) is past the play, or the HUD showed the result's text
bool TsneDecided() {
    if (gTsneResultGen == gTsneGen && gTsneGen) return true;
    const uint32_t sup = TsneSupervisor();
    if (!sup) return false;
    const uint32_t st = R32(sup + 0xC8), blk = R32(sup + 0x3BC) & 0x01FFFFFF;
    return (st >= 2 && st <= 5) || (blk >= 0x100000 && blk + 8 < 0x02000000 && R8(blk + 4) != 0);
}
// the entry's end: the supervisor's state 3 (0x3E9F6C: a team's victories reached config 0x2D, or its rounds 0x2C)
void TsneEntryEnd() {
    static uint32_t told = 0;
    const uint32_t sup = TsneSupervisor(), d = ConfigData0();
    if (told == gTsneGen || !sup || R32(sup + 0xC8) != 3 || d < 0x100000 || d + 0x50 >= 0x02000000) return;
    told = gTsneGen;
    Log("tsne: the entry's end (GAME SET) after round %d: red %u, blue %u team victories, %u needed; the rotation's next entry follows",
        gTsneRoundNo, R8(d + 0x42), R8(d + 0x46), R8(d + 0x2D));
    TsnePngAsk("end", 60);
}
uint32_t TsneAnnouncer() {                             // the HUD's announcer box ([0x2F0B8C])
    const uint32_t at = PageData(0x3D7A20, 0, 0x2F0B8C), s = at ? R32(at) & 0x01FFFFFF : 0;
    return s >= 0x100000 && s + 0x3C0 < 0x02000000 ? s : 0;
}
// the box's text (the HUD's letters: capitals, the accented ones Latin-1 - 0x20) and colour (R G B A from the low
// byte), shown at least `frames` more frames
bool TsneAnnounce(const Ctx& c, const std::string& text, uint32_t color, long long frames) {
    const uint32_t box = TsneAnnouncer(), lay = box ? R32(box + 0x3AC) & 0x01FFFFFF : 0;
    if (lay < 0x100000 || lay >= 0x02000000) return false;
    static uint32_t buf = 0;
    if (!buf) buf = Alloc(128);
    if (!buf) return false;
    const size_t n = std::min<size_t>(text.size(), 120);
    memcpy(gMem + buf, text.data(), n); gMem[buf + n] = 0;
    const uint32_t node = CallPage(c, 0x481DA0, lay, 0x3D12AE);
    if (node >= 0x100000 && node + 0x40 < 0x02000000 && (int16_t)R16(node + 0x3E) != -0x320) {
        W16(node + 0x3E, (uint16_t)(int16_t)-0x320); W32(node + 0x10, R32(node + 0x10) | 0x800000);
    }
    CallPage(c, 0x3C7110, lay, 0xA5C8D6, buf, 0);
    CallPage(c, 0x3C80B0, lay, 0xA5C8D6, color);
    const uint32_t step = R32(kTimeStep) ? R32(kTimeStep) : 10;
    if ((int32_t)R32(box + 0x3B8) < (int32_t)(step * frames)) W32(box + 0x3B8, (uint32_t)(step * frames));
    return true;
}
// the texts (the HUD's language: 0 en, 1 fr, 2 de, 3 it, 4 es)
const char* kTsneAlertWord[5] = {"ALERT", "ALERTE", "ALARM", "ALLARME", "ALERTA"};
const char* kTsneRole[5][2] = {
    {"SNEAKING TEAM: GA-KO OR KEROTAN TO A GOAL", "DEFENSE TEAM: PROTECT THE GA-KO AND KEROTAN"},
    {"INFILTRATION : GA-KO OU KEROTAN \xA0 UN OBJECTIF", "D\xA9" "FENSE : PROT\xA9" "GEZ LE GA-KO ET LE KEROTAN"},
    {"SCHLEICHTEAM: GA-KO ODER KEROTAN ZUM ZIEL", "VERTEIDIGUNG: SCH\xBC" "TZT GA-KO UND KEROTAN"},
    {"INFILTRAZIONE: GA-KO O KEROTAN A UN OBIETTIVO", "DIFESA: PROTEGGETE IL GA-KO E IL KEROTAN"},
    {"INFILTRACI\xB3" "N: GA-KO O KEROTAN A UNA META", "DEFENSA: PROTEGED EL GA-KO Y EL KEROTAN"},
};
const uint32_t kTsneAlertColor = 0x802828D8u, kTsneRoleColor = 0x808CA0A0u;   // red; the box's own
// the gauge ended with the alert (GV_DestroyActor, base 0x1224A0; its die 0x3C1430 clears 0x2E5630): nothing of it
// shows between two alerts (its own phase steps drew EVASION after an ALERT)
void TsneGaugeEnd(const Ctx& c) {
    const uint32_t at = PageData(0x3C1668, 0, 0x2E5630), g = at ? R32(at) & 0x01FFFFFF : 0;
    ps2::Fn f = ps2::FindFn(V(0x1224A0));
    if (!f || g < 0x100000 || g + 0x90 >= 0x02000000) return;
    Ctx t = c; t.r[4] = g; f(t);
}
void TsneAlertTick(const Ctx& c) {
    TsneAlert& a = gTsneAlert;
    const uint32_t sup = TsneSupervisor();
    if (!TsneRound() || !TsneMapPage() || !gTsneSideDone || !TsneHud() || a.gen != gTsneGen) {
        if (a.music && sup && R32(sup + 0xF0) && TsneMapPage()) { W32(sup + 0xF0, 0); W32(sup + 0xF4, 0); }   // the round's music
        if (a.gauge && TsneMapPage()) { TsneGaugeSet(0, 0); TsneGaugeEnd(c); }
        a = TsneAlert(); a.gen = gTsneGen; gTsneAlertUntil = 0;
        return;
    }
    const long long now = gFrameNo;
    const double rate = FieldRate();
    const uint32_t me = TsneSelf();
    const bool alive = me && (int16_t)R16(me + 0xB64) > 0;
    if (!alive) a.aliveAt = -1;
    else if (a.aliveAt < 0) { a.aliveAt = now; a.hitsDone = 0; }
    const uint32_t pause = PauseMenuWork();              // a menu screen (the choice, START: NewHudPause +0x326)
    if (!alive || (pause && pause + 0x340 <= 0x02000000 && R16(pause + 0x326) != 0)) a.inMapAt = -1;
    else if (a.inMapAt < 0) a.inMapAt = now;
    const int side = TsneSneakSide();
    // tests (SIM_TSNE_HIT=s1,s2..): at those seconds of its player's life a defender hits every sneaker through the
    // game's own damage (10 off the life, the attacker his slot): the record goes to the others as a hit's
    static const std::vector<double> hitAt = TsneSecs("SIM_TSNE_HIT");
    if (alive && a.hitsDone < hitAt.size() && TsneTeamOf(me) != side && now >= a.aliveAt + (long long)(hitAt[a.hitsDone] * rate)) {
        int n = 0;
        for (const auto& act : gNativeActors) {
            if (act.second != "NewNetworkPlayer" && act.second != "NewNetworkDummyPlayer") continue;
            const uint32_t pl = act.first;
            if (TsneTeamOf(pl) != side || (int16_t)R16(pl + 0xB64) <= 10) continue;
            n += TsneDamage(c, pl, (int)R8(me + 0xB60), 10, 0);
        }
        Log("SIM_TSNE_HIT: this defender hits %d sneakers (10 off their lives, %.1f s of his life)", n, (double)(now - a.aliveAt) / rate);
        ++a.hitsDone;
    }
    // tests (SIM_TSNE_OUT=s1,s2..): in the k-th round the red team sneaks, at s_k seconds of its player's life the host
    // puts every sneaker out (his life 0: the round to the defenders); the last value for the later rounds
    static const std::vector<double> outAt = TsneSecs("SIM_TSNE_OUT");
    static uint32_t outGen = 0, outDone = 0; static size_t outRound = 0;
    if (!outAt.empty() && gTsneSwap && IsSessionHost() && alive && outDone != gTsneGen) {
        if (outGen != gTsneGen) { outGen = gTsneGen; ++outRound; }
        const double s = outAt[std::min(outRound, outAt.size()) - 1];
        if (now >= a.aliveAt + (long long)(s * rate)) {
            outDone = gTsneGen;
            int n = 0;
            for (const auto& act : gNativeActors) {
                if (act.second != "NewNetworkPlayer" && act.second != "NewNetworkDummyPlayer") continue;
                if (TsneTeamOf(act.first) != side || (int16_t)R16(act.first + 0xB64) <= 0) continue;
                W16(act.first + 0xB64, 0); ++n;
            }
            Log("SIM_TSNE_OUT: the host puts %d sneakers out (%.1f s of its player's life, red sneaking round %zu)", n, s, outRound);
        }
    }
    // an enemy's hit on a sneaker (TsneDamageEvent): the alert, renewed by each hit
    if (gTsneHit.victim >= 0 && gTsneHit.gen == gTsneGen && !TsneDecided()) {
        if (now >= a.until) {
            ++a.count;
            Log("tsne: ALERT (sneaker %d hit by player %d, a defender): %.0f s for everyone, every sneaker's camouflage inactive",
                gTsneHit.victim, gTsneHit.attacker, kTsneAlertSecs);
        } else Log("tsne: the alert renewed (sneaker %d hit by player %d, a defender): %.0f s again, %.1f s were left", gTsneHit.victim,
                   gTsneHit.attacker, kTsneAlertSecs, (double)(a.until - now) / std::max(1.0, rate));
        a.until = now + (long long)(kTsneAlertSecs * rate);
    }
    gTsneHit = TsneHit();
    if (a.until > now && TsneDecided()) {                // the round's result ends a running alert (its music, its display)
        a.until = now;
        Log("tsne: the alert ends with the round's result");
    }
    gTsneAlertUntil = a.until;
    // the music (the supervisor's BGM state +0xF0, its time +0xF4 in the game's units; the BGM manager 0x3F2FD0 plays
    // its set's alert piece for 1 and 2): 1 while the alert lasts, 0 at its end (RESCUE's own, 0x3F2B80, is not run in
    // a TSNE round: TsneMatchCall)
    if (sup) {
        const uint32_t step = R32(kTimeStep) ? R32(kTimeStep) : 5, st = R32(sup + 0xF0);
        if (now < a.until) {
            const uint32_t rest = (uint32_t)(a.until - now) * step;
            if (st != 1) { W32(sup + 0xF0, 1); Log("tsne: the music's state 1 (the alert)"); }
            if ((int32_t)R32(sup + 0xF4) < (int32_t)rest) W32(sup + 0xF4, rest);
            a.music = true;
        } else if ((st == 1 || st == 2) && (a.music || !TsneDecided())) {
            W32(sup + 0xF0, 0); W32(sup + 0xF4, 0);
            if (a.music || Once(((uint64_t)155 << 40) | (uint64_t)(now / 600)))
                Log("tsne: the music's state 0 (%s, was %u)", a.music ? "the alert's end" : "no alert", st);
            a.music = false;
        }
    }
    const int lang = TsneHudLanguage();
    const uint32_t box = TsneAnnouncer(), step = R32(kTimeStep) ? R32(kTimeStep) : 10;
    if (now < a.until) {                                 // ALERT and its seconds left (the gauge made for this alert)
        const uint32_t gauge = TsneGauge(c);
        if (gauge) {
            const int centis = (int)((double)(a.until - now) * 100.0 / std::max(1.0, rate));
            TsneGaugeSet(1, centis);
            if (!a.gauge) { Log("tsne: the alert's display shows ALERT (%d.%02d s)", centis / 100, centis % 100); TsnePngAsk("alert", 40); }
            a.gauge = true;
            return;
        }
        const int secs = (int)((a.until - now + (long long)rate - 1) / (long long)std::max(1.0, rate));
        if (secs != a.shown) {
            TsneAnnounce(c, std::string(kTsneAlertWord[lang]) + "  " + std::to_string(secs), kTsneAlertColor, 3);
            if (a.shown < 0) TsnePngAsk("alert", 40);
            a.shown = secs;
        } else if (box && (int32_t)R32(box + 0x3B8) < (int32_t)(3 * step)) W32(box + 0x3B8, 3 * step);
        return;
    }
    if (a.gauge) {
        TsneGaugeSet(0, 0); TsneGaugeEnd(c); a.gauge = false;
        Log("tsne: the alert's display off (the gauge ended)");
        TsnePngAsk("alertend", 60);
    } else if (a.shown >= 0) TsnePngAsk("alertend", 60);
    a.shown = -1;
    // the round's start: this player's role, once, a second after he is in the map (the weapons' choice closed) and
    // the box free. The box's text node draws one character a byte (0x4826B0): ASCII only, so the Japanese and Korean
    // programs show it in English (their first messages say it in their language)
    if (!a.roleShown && a.inMapAt >= 0 && now >= a.inMapAt + (long long)rate && box && (int32_t)R32(box + 0x3B8) <= 0) {
        const bool sneak = TsneSneaker();
        const std::string role = kTsneRole[lang][sneak ? 0 : 1];
        if (TsneAnnounce(c, role, kTsneRoleColor, (long long)(kTsneRoleSecs * rate))) {
            a.roleShown = true;
            Log("tsne: the round's role shown in the announcer box: %s (language %d)", sneak ? "sneaking" : "defending", lang);
            TsnePngAsk("role", 60);
        }
    }
}
// ---- the GAME TYPE picture: TSNE's composed from RESCUE's and CAPTURE's sprites of the rules pages' layout
// (69849c.la2, LA2 1.7, tools/la2v7.py): the GA-KO's and the KEROTAN's bubbles side by side, the guards, the sneakers
// seen through; CAPTURE's soldiers left out (size 0). Made on the layout's node objects (the HUD's +0x40: +0x3C their
// count, +0x48 their array from 1; an object: +0xC the node's code, +0x34 its colour (R G B A), +0x38 its place (x, y
// in 1/16 pixel), +0x3C its size), which the HUD makes again each round
struct TsneSprite { uint32_t node; int x, y; bool ghost, hide; };
const TsneSprite kTsneSprites[] = {
    {0x358EE4, 64, 212, false, false},  {0x358EE5, 101, 278, false, false},   // the GA-KO's bubble, the GA-KO
    {0x9F364A, 120, 212, false, false}, {0x9F364B, 155, 278, false, false},   // the KEROTAN's bubble, the KEROTAN
    {0x358EE2, 178, 228, false, false},                                       // the guards
    {0x358EE3, 300, 226, true, false},                                        // the sneakers, camouflaged
    {0x9F3648, 0, 0, false, true},      {0x9F3649, 0, 0, false, true},        // CAPTURE's soldiers
};
uint32_t La2Object(uint32_t lay, uint32_t hash) {
    if (lay < 0x100000 || lay + 0x50 >= 0x02000000) return 0;
    const uint32_t n = R32(lay + 0x3C), arr = R32(lay + 0x48);
    if (n > 4096 || arr < 0x100000 || arr + 4 * n + 4 >= 0x02000000) return 0;
    for (uint32_t k = 1; k <= n; ++k) {
        const uint32_t o = R32(arr + 4 * k);
        if (o >= 0x100000 && o + 0x48 < 0x02000000 && (R32(o + 0xC) & 0xFFFFFF) == hash) return o;
    }
    return 0;
}
struct TsnePatch { uint32_t at, now; };
std::vector<TsnePatch> gTsnePatches;                   // the words put (made again when the HUD changed them)
uint32_t gTsnePictureLay = 0;
void TsnePicture(uint32_t lay) {
    if (lay < 0x100000 || lay + 0x50 >= 0x02000000) return;
    if (lay == gTsnePictureLay && !gTsnePatches.empty()) {
        bool still = true;
        for (const TsnePatch& t : gTsnePatches) still &= R32(t.at) == t.now;
        if (still) return;
    }
    gTsnePatches.clear();
    const bool fresh = lay != gTsnePictureLay;
    gTsnePictureLay = lay;
    static const uint32_t ghost = getenv("SIM_TSNE_GHOST") ? (uint32_t)strtoul(getenv("SIM_TSNE_GHOST"), nullptr, 16) : 0x48505858u;
    int put = 0;
    auto set = [](uint32_t at, uint32_t v) { W32(at, v); gTsnePatches.push_back({at, v}); };
    for (const TsneSprite& sp : kTsneSprites) {
        const uint32_t o = La2Object(lay, sp.node);
        if (!o) continue;
        if (sp.hide) set(o + 0x3C, 0);
        else {
            set(o + 0x38, (uint32_t)(sp.x * 16) | (uint32_t)(sp.y * 16) << 16);
            if (sp.ghost) set(o + 0x34, ghost);
        }
        ++put;
    }
    if (fresh) Log("tsne: the GAME TYPE page's picture made (layout object %08x, %d of %zu sprites)", lay, put, sizeof kTsneSprites / sizeof *kTsneSprites);
}

// RESCUE's round decider (n003a 0x3EAA78, from the supervisor's RESCUE state 0x3EA910; the host's: 0x173840): the
// round's result (its record [supervisor + 0x3BC]: +0 the time left, +4 the result 2 red wins / 3 blue wins, +8 the
// flags, +0xE / +0xF the red and blue players still in) is blue's when blue delivered (flag 2, set by 0x3F1E20 for a
// blue deliverer; red's delivery sets 1, which RESCUE does not look at), when no red player is left, on the flags
// 0x100 or 4 (0x3F1A98, 0x3F20E8); red's when no blue player is left, on 0x200 or 8, and when the time is out (the
// defenders'). In a round the red team sneaks the two attackers' cases are turned: red's delivery (flag 1) wins it
// for red, the time out for blue
bool TsneDecide(Ctx& c, uint32_t sup) {
    ps2::Fn host = ps2::FindFn(V(0x173840));
    if (!host || sup < 0x100000 || sup + 0x3C0 >= 0x02000000) return false;
    Ctx t = c;
    host(t);
    if (!(uint32_t)t.r[2]) return false;
    const uint32_t blk = R32(sup + 0x3BC);
    if (blk < 0x100000 || blk + 0x20 >= 0x02000000) return false;
    const uint32_t flags = R32(blk + 8);
    uint8_t result = 0;
    if (flags & 1) result = 2;                         // red delivered
    else if (flags & 2) result = 3;
    else if (!R8(blk + 0xE)) result = 3;                // no red player left
    else if (!R8(blk + 0xF)) result = 2;                // no blue player left
    else if (flags & 0x300) result = (flags & 0x200) ? 2 : 3;
    else if ((int32_t)R32(blk) <= 0) result = 3;        // the time out: the defenders, blue
    else if (flags & 4) result = 3;
    else if (flags & 8) result = 2;
    if (!result) return false;
    W8(blk + 4, result);
    Log("tsne: round %d: %s TEAM WINS (flags %#x, players in %u/%u, time %d)", gTsneRoundNo, result == 2 ? "RED" : "BLUE", flags,
        R8(blk + 0xE), R8(blk + 0xF), (int32_t)R32(blk));
    return true;
}

// MGO2's elimination: a team whose players are all out, dead or knocked out (asleep or stunned: the flag 0x99, on the
// host's copies too), loses the round (RESCUE counts only the living, +0xE / +0xF). The host's, from the decider
// 0x3EAA78; MGO1_TSNE_KO_OUT=0: RESCUE's count only
bool TsneSideOut(Ctx& c, uint32_t sup) {
    static const bool off = getenv("MGO1_TSNE_KO_OUT") && atoi(getenv("MGO1_TSNE_KO_OUT")) == 0;
    ps2::Fn host = ps2::FindFn(V(0x173840));
    if (off || !host || sup < 0x100000 || sup + 0x3C0 >= 0x02000000) return false;
    Ctx t = c;
    host(t);
    if (!(uint32_t)t.r[2]) return false;
    const uint32_t blk = R32(sup + 0x3BC);
    if (blk < 0x100000 || blk + 0x20 >= 0x02000000 || R8(blk + 4)) return false;
    int in[2] = {0, 0}, up[2] = {0, 0}, ko[2] = {0, 0};
    for (const auto& a : gNativeActors) {
        if (a.second != "NewNetworkPlayer" && a.second != "NewNetworkDummyPlayer") continue;
        const int team = TsneTeamOf(a.first);
        if (team < 0 || team > 1 || (int16_t)R16(a.first + 0xB64) <= 0) continue;
        ++in[team];
        if (TsnePlayerFlag(a.first, kTsneKnockedOut)) ++ko[team]; else ++up[team];
    }
    int loser = -1;
    for (int t2 = 0; t2 < 2; ++t2) if (in[t2] && !up[t2] && up[1 - t2]) loser = t2;   // every one of them knocked out
    if (loser < 0) return false;
    const uint8_t result = loser == 0 ? 3 : 2;           // 2 red wins, 3 blue wins
    W8(blk + 4, result);
    Log("tsne: round %d: %s TEAM WINS (every %s out: %d knocked out)", gTsneRoundNo, result == 2 ? "RED" : "BLUE",
        loser == TsneSneakSide() ? "sneaker" : "defender", ko[loser]);
    return true;
}

// the KEROTAN carried as CAPTURE's item (0xA: its model in the hand, its mark) where the mission objects give every
// object RESCUE's GA-KO item (+0x8B8 = 0xD, set by their act each pass): 0x39AD88 (a target taken: the item into the
// taker's items) and 0x39ADF8 (given up) see the KEROTAN's item. MGO1_TSNE_KEROTAN_ITEM=0: the GA-KO's for both
uint32_t gTsneHeld[16] = {};                           // the item each slot was given (to give the same one up)
bool TsneKerotanItem() {
    static const bool on = !getenv("MGO1_TSNE_KEROTAN_ITEM") || atoi(getenv("MGO1_TSNE_KEROTAN_ITEM")) != 0;
    return on;
}

// ---- a stealthed sneaker for the defense (Snake, 7 October): while his camouflage is on (his player's flag 0x96,
// set and cleared with NPL_NewOptcamouflage: US n003a 0x451108 / 0x451180) and no alert runs on this machine, a
// defender's auto-aim never picks him and the HUD never names him; during an alert (or with another item) the game's own.
// The auto-aim: 0x45FAC0 (the guns' states 0x41666C, 0x417538, 0x41FD70; a0 the searcher's +0x200) walks the aim
// targets (kind 0x100, [+0xC] the owner's +0x200) and scores each with 0x45F790 (f0 > 0 a candidate). The name:
// NewHudDispName (act 0x3D42F8, 8 entries) is given a player by 0x3D3F70 (aimed at: his aim's ray, kind 0xAD of
// NewNetWeaponLaserSight, touched him: flag 0x15E) and 0x3D3F58; 0x3D3F88 takes one off
bool TsnePlayerFlag(uint32_t pl, int bit) { return (R32(pl + 0xB2C + 4 * (uint32_t)(bit >> 5)) >> (bit & 31)) & 1; }
bool TsnePlayerRecord(uint32_t pl) {
    const auto it = pl >= 0x100000 && pl + 0xF00 < 0x02000000 ? gNativeActors.find(pl) : gNativeActors.end();
    return it != gNativeActors.end() && (it->second == "NewNetworkPlayer" || it->second == "NewNetworkDummyPlayer");
}
bool TsneDefender() { const uint32_t me = TsneSelf(); return gTsneSideDone && me && TsneTeamOf(me) >= 0 && TsneTeamOf(me) != TsneSneakSide(); }
bool TsneStealthed(uint32_t pl) {                      // a sneaker the defense does not see
    return TsneTeamOf(pl) == TsneSneakSide() && TsnePlayerFlag(pl, 0x96) && gFrameNo >= gTsneAlertUntil;
}
struct TsneSeen { uint32_t gen = 0, alerts = 0; int state = -1; };
TsneSeen gTsneAimSeen[16], gTsneNameSeen[16];          // per sneaker's slot: the last state logged (0 hidden, 1 seen)
bool TsneSeenChange(TsneSeen& s, int state) {          // a change of state, or a new alert while seen
    const uint32_t alerts = gTsneAlert.count;
    if (s.gen == gTsneGen && s.state == state && (state == 0 || s.alerts == alerts)) return false;
    s.gen = gTsneGen; s.state = state; s.alerts = alerts;
    return true;
}
// 0x45F790 (an aim target's score), on a defender's machine for his own search: a stealthed sneaker scores 0
bool TsneAimScore(Ctx& c, uint32_t va) {
    const uint32_t me = TsneSelf(), entry = (uint32_t)c.r[16] & 0x01FFFFFF;
    if (!TsneDefender() || ((uint32_t)c.r[4] & 0x01FFFFFF) != me + 0x200 || entry < 0x100000 || entry + 0x10 >= 0x02000000) return false;
    const uint32_t owner = R32(entry + 0xC) & 0x01FFFFFF, pl = owner >= 0x100200 ? owner - 0x200 : 0;
    if (!TsnePlayerRecord(pl) || TsneTeamOf(pl) != TsneSneakSide()) return false;
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    const int slot = (int)R8(pl + 0xB60) & 15;
    const bool hide = TsneStealthed(pl);
    if (c.f[0] > 0.f && TsneSeenChange(gTsneAimSeen[slot], hide ? 0 : 1))
        Log(hide ? "tsne: this defender's auto-aim finds stealthed sneaker %d (score %.3f): skipped, no lock"
                 : "tsne: this defender's auto-aim finds sneaker %d (score %.3f): his camouflage off, the lock is the game's", slot, c.f[0]);
    if (hide) c.f[0] = 0.f;
    return true;
}
// 0x45FAC0 (the auto-aim's search), on a defender's machine: the sneaker it picks logged (tests' proof)
bool TsneAimPick(Ctx& c, uint32_t va) {
    const uint32_t me = TsneSelf();
    if (!TsneDefender() || ((uint32_t)c.r[4] & 0x01FFFFFF) != me + 0x200) return false;
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    const uint32_t t = (uint32_t)c.r[2] & 0x01FFFFFF, owner = t >= 0x100000 && t + 0x10 < 0x02000000 ? R32(t + 0xC) & 0x01FFFFFF : 0;
    const uint32_t pl = owner >= 0x100200 ? owner - 0x200 : 0;
    if (TsnePlayerRecord(pl) && TsneTeamOf(pl) == TsneSneakSide()) {
        static uint32_t told = 0;
        const uint32_t k = gTsneGen << 12 | (gTsneAlert.count & 0xFFF);
        if (told != k) {
            told = k;
            Log("tsne: this defender's auto-aim locks on sneaker %u (%s)", R8(pl + 0xB60),
                TsneStealthed(pl) ? "stealthed: not expected" : gFrameNo < gTsneAlertUntil ? "the alert" : "his camouflage off");
            TsnePngAsk("aimlock", 12);
        }
    }
    return true;
}
// 0x3D3F70 / 0x3D3F58 (a player's name shown over him), on a defender's machine: refused for a stealthed sneaker,
// his name taken off (0x3D3F88)
bool TsneNameCall(Ctx& c) {
    const uint32_t pl = (uint32_t)c.r[4] & 0x01FFFFFF;
    if (!TsneDefender() || !TsnePlayerRecord(pl) || TsneTeamOf(pl) != TsneSneakSide()) return false;
    const int slot = (int)R8(pl + 0xB60) & 15;
    if (!TsneStealthed(pl)) {
        if (TsneSeenChange(gTsneNameSeen[slot], 1)) {
            Log("tsne: sneaker %d's name shown to this defender (%s)", slot, gFrameNo < gTsneAlertUntil ? "the alert" : "his camouflage off");
            TsnePngAsk("aimname", 12);
        }
        return false;
    }
    if (TsneSeenChange(gTsneNameSeen[slot], 0)) {
        Log("tsne: sneaker %d's name not shown to this defender (his camouflage on)", slot);
        TsnePngAsk("aimhide", 12);
    }
    if (ps2::Fn off = ps2::FindFn(V3(0x3D3F88))) { Ctx t = c; t.r[4] = pl; off(t); }
    c.r[2] = 0;
    return true;
}

// ---- the in-match hooks (a call's n003a address, the round TSNE); true when served
bool TsneMatchCall(Ctx& c, uint32_t va, uint32_t a3) {
    if (a3 == 0x377960) { if (gTsneCamo.itemAsk >= 0) TsneItemServe(c, true); return false; }   // tests: in the item window's act
    if (a3 == 0x2FCC60) return TsneEquipCall(c, va);   // this player's item changes logged (by whom)
    if (a3 == 0x402848) return TsneItemVerdict(c, va);   // tests: the game's check of the test's item
    if (a3 == 0x45F790) return TsneAimScore(c, va);     // the auto-aim: a stealthed sneaker no candidate for a defender
    if (a3 == 0x4511F8) return TsneCamoFrame(c, va);   // STEALTH's frame: a sneaker's battery copy 0 in an alert
    if (a3 == 0x45FAC0) return TsneAimPick(c, va);
    if (a3 == 0x3D3F70 || a3 == 0x3D3F58) return TsneNameCall(c);   // a name over a player: not a stealthed sneaker's
    if (a3 == 0x481DA0) {                                // a layout's group by its code: the KEROTAN's mark (TsneMarks)
        if (((uint32_t)c.r[5] & 0xFFFFFF) == 0xA1B47C && gTsneObjects && R32(gTsneObjects + 0x410) >= 2) c.r[5] = 0x83F890;
        return false;
    }
    if (a3 == 0x481B68) {                                // an action on a node: the goals' circles in the sneakers' colour
        const uint32_t act = (uint32_t)c.r[5] & 0xFFFFFF;
        if ((act == 0x516054 || act == 0x516055) && gTsneSideDone) c.r[5] = gTsneGoalsB ? 0x516055 : 0x516054;
        return false;
    }
    if (a3 == 0x3F09D0) {                                // the rules' events: 4 a player damaged (the attacker's, the victim's slots)
        if (((uint32_t)c.r[4] & 0xFF) == 4) TsneDamageEvent((int)(int8_t)(c.r[5] & 0xFF), (int)(c.r[6] & 0xFF));
        return false;
    }
    if (a3 == 0x409188) { TsneDamageLog(c); return false; }   // the game's damage on this machine (its source logged)
    if (a3 == 0x3F3A90) {                                // the BGM manager starts a piece (its set's: the alert's for 1, 2)
        Log("tsne: the BGM manager plays piece %08x (%u), the music's state %d", (uint32_t)c.r[4], (uint32_t)c.r[5],
            TsneSupervisor() ? (int)R32(TsneSupervisor() + 0xF0) : -1);
        return false;
    }
    if (a3 == 0x39E0B0) {                                // the mission objects' options
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t mo = (uint32_t)c.r[4];
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        if ((int32_t)(uint32_t)c.r[2] >= 0) TsneObjects(c, mo);
        return true;
    }
    if (a3 == 0x3AFBA0) {                                // a HUD text: RESCUE's name is TSNE's, in its language
        if ((int32_t)(uint32_t)c.r[4] != 0x22 + kTsneAs) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        const int lang = TsneNameLanguage(TsneCString((uint32_t)c.r[2] & 0x01FFFFFF));
        if (const uint32_t at = TsneString(gTsneHudName[lang], kTsneNames[lang])) { c.r[2] = at; c.r[3] = 0; }
        return true;
    }
    if (a3 == 0x3C9080) {                                // the GAME TYPE page: RESCUE's picture with CAPTURE's KEROTAN
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t hud = (uint32_t)c.r[4];
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        const uint64_t v0 = c.r[2];
        const uint32_t lay = hud >= 0x100000 && hud + 0x44 < 0x02000000 ? R32(hud + 0x40) : 0;
        TsneSides(TsneHud());
        if (ps2::Fn show = ps2::FindFn(V3(0x3C7090))) { Ctx t = c; t.r[4] = lay; t.r[5] = 0xC8AB3E; show(t); }   // CAPTURE's group: its KEROTAN
        TsnePicture(lay);
        static uint32_t shot = 0;                        // tests: a picture of the page, once a round
        if (shot != gTsneGen) { shot = gTsneGen; TsnePngAsk("type", 80); }
        c.r[2] = v0;
        return true;
    }
    if (a3 == 0x3C7110) {                                // a HUD text: the round's result shown (and its picture, tests)
        const std::string t = gTsneResultGen != gTsneGen ? TsneCString((uint32_t)c.r[6] & 0x01FFFFFF) : std::string();
        for (const char* w : {"TEAM WINS", "TEAM WON", "GAGN", "GEWINNT", "VINCE", "GANA"})
            if (t.find(w) != std::string::npos) { gTsneResultGen = gTsneGen; TsnePngAsk("result", 45); break; }
        return false;
    }
    // a target taken (0x3F1BA8 raises the event its announcement comes from): RESCUE's rule flags (the supervisor's
    // +0x50) let only blue takers raise it (0x10000000: team not 0); while red sneaks, red ones (0x20000000: not 1)
    if (a3 == 0x3F1BA8 && gTsneSwap) {
        ps2::Fn f = ps2::FindFn(va);
        const uint32_t at = PageData(0x3ECBA8, 0, 0x2F0C90);
        const uint32_t sup = at ? R32(at) & 0x01FFFFFF : 0;
        if (!f || sup < 0x100000 || sup + 0x60 >= 0x02000000) return false;
        const uint32_t was = R32(sup + 0x50);
        const uint64_t ra = c.r[31];
        if (was & 0x10000000) W32(sup + 0x50, (was & ~0x10000000u) | 0x20000000u);
        f(c);
        c.r[31] = ra;
        if (was & 0x10000000) W32(sup + 0x50, was);
        return true;
    }
    // RESCUE's music (0x3F2B80, from its state 0x3EAA2C: the alert of its last quarter for the blue team 0x3F2D70, the
    // alert while the other side holds the target 0x3F2CB0, the count 0x3F2DD0 turning 1 into 2, its evasion): not run
    // in a TSNE round, whose alert music lasts only while its alert does (TsneAlertTick)
    if (a3 == 0x3F2B80) { c.r[2] = 0; c.r[3] = 0; return true; }
    if (a3 == 0x3EAA78) {                                // RESCUE's round decider: a side all out (TsneSideOut); the red team
        if (TsneSideOut(c, (uint32_t)c.r[4])) { c.r[2] = 1; c.r[3] = 0; return true; }   // sneaking: TsneDecide
        if (!gTsneSwap) return false;
        c.r[2] = TsneDecide(c, (uint32_t)c.r[4]) ? 1 : 0; c.r[3] = 0;
        return true;
    }
    if (a3 == 0x39AD88 || a3 == 0x39ADF8) {              // a target taken (into the taker's items) or given up
        ps2::Fn f = ps2::FindFn(va);
        const uint32_t mo = (uint32_t)c.r[4], slot = (uint32_t)c.r[5] & 0xFF;
        if (!f || mo != gTsneObjects || slot >= 16 || !TsneKerotanItem()) return false;
        int held = -1;                                   // the object that player holds (its holder at +0xD4)
        for (uint32_t k = 0; k < R32(mo + 0x410) && k < 3; ++k) {
            const uint32_t pl = R32(mo + 0xB0 + 0x120 * k + 0xD4);
            if (pl >= 0x100000 && pl + 0xB64 < 0x02000000 && R8(pl + 0xB60) == slot) held = (int)k;
        }
        const uint32_t item = a3 == 0x39AD88 ? (held == 1 ? 0xAu : 0xDu) : (gTsneHeld[slot] ? gTsneHeld[slot] : held == 1 ? 0xAu : 0xDu);
        const uint32_t was = R32(mo + 0x8B8);
        const uint64_t ra = c.r[31];
        W32(mo + 0x8B8, item);
        f(c);
        c.r[31] = ra;
        W32(mo + 0x8B8, was);
        gTsneHeld[slot] = a3 == 0x39AD88 ? item : 0;
        if (item == 0xA && Once(((uint64_t)146 << 40) | (uint64_t)(a3 & 0xFFFF) << 8 | slot))
            Log("tsne: slot %u %s the KEROTAN (item 0xA, not the GA-KO's 0xD)", slot, a3 == 0x39AD88 ? "takes" : "gives up");
        return true;
    }
    if (a3 == 0x4C9FC8) {                                // an item box's act (its taker: this machine's player, 0x4C9D40):
        const uint32_t box = (uint32_t)c.r[4];           // a weapon of the other side's kind is left where it lies (its
        if (box < 0x100000 || box + 0xE0 >= 0x02000000 || R32(box + 0x40) != 2) return false;   // reach +0xD0 0 for him)
        const int lethal = TsneWeaponLethal(R32(box + 0x44));
        const uint32_t me = TsneSelf();
        if (lethal < 0 || !me || (int16_t)R16(me + 0xB64) <= 0 || (lethal == 1) == !TsneSneaker()) return false;   // his kind
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t reach = R32(box + 0xD0);
        const uint64_t ra = c.r[31];
        W32(box + 0xD0, 0);
        f(c);
        c.r[31] = ra;
        if (box + 0xD4 < 0x02000000 && R32(box + 0xD0) == 0) W32(box + 0xD0, reach);
        if (Once(((uint64_t)151 << 40) | (box & 0xFFFFFF)))
            Log("tsne: the box of weapon %#x left where it lies (this player %s: %s weapons only)", R32(box + 0x44),
                TsneSneaker() ? "sneaks" : "defends", TsneSneaker() ? "non-lethal" : "lethal");
        return true;
    }
    if (a3 == 0x3CBDC8) {                                // the weapons' rows: TSNE's sneakers choose non-lethal ones
        const uint32_t menu = (uint32_t)c.r[4];
        if (menu < 0x100000 || menu + 0x340 >= 0x02000000) return false;
        const int team = TsneTeamOf(TsneSelf());
        const uint32_t had = R32(menu + 0x334);
        const bool sneak = team == TsneSneakSide();
        W32(menu + 0x334, had & (sneak ? kTsneNonLethal : kTsneLethal));
        Log("tsne: the weapons' choice: this player's team %d, character %d (%s), the weapons %#x of %#x", team,
            TsneSelf() ? (int)R8(TsneSelf() + 0xB62) : -1, sneak ? "sneaking: the non-lethal ones" : "defending: the lethal ones",
            R32(menu + 0x334), had);
        static uint32_t shot = 0;                        // tests: a picture of the choice, once a round
        if (shot != gTsneGen) { shot = gTsneGen; TsnePngAsk("weapons", 50); }
        return false;
    }
    return false;
}
// RESCUE's messages (the script's strings L + 12 .. L + 21, chosen by the HUD's 0x3D51C8 (the round's first) and
// 0x3D52C8 (a target taken or lost) by colour: blue gets the attackers' L + 12 .. 16, red the defenders' L + 17 .. 21).
// In a round the red team sneaks the two sets are given by role: 12 <-> 17 (the first), 13 <-> 20 (own side took it),
// 14 <-> 21 (own side lost it), 15 <-> 18 (the other side took it), 16 <-> 19 (the other side lost it)
const uint8_t kTsneRoleSwap[10] = {17, 20, 21, 18, 19, 12, 15, 16, 13, 14};
// the messages about the KEROTAN: the GA-KO's name made the KEROTAN's (English, French, Spanish; the Japanese
// program's glyphs of ガーコ and ケロタン, both in its script's font; the Korean ones left as they are)
std::map<uint32_t, uint32_t> gTsneMessages;            // a string's address: its KEROTAN copy
uint32_t TsneKerotanMessage(uint32_t was, uint32_t captureFirst) {
    if (auto it = gTsneMessages.find(was); it != gTsneMessages.end()) return it->second;
    if (was < 0x100000 || was >= 0x02000000) return 0;
    std::string t = TsneCString(was);
    static const std::string gakoJp("\x82\x0c\x83\x12\x82\x13", 6), keroJp("\x82\x11\x82\x4d\x82\x1f\x82\x51", 8);
    const bool jp = TsneCString(captureFirst).rfind(keroJp, 0) == 0;   // this script's glyphs: CAPTURE's first message
    const std::pair<std::string, std::string> names[] = {{"GA-KO", "Kerotan"}, {"Gacko", "Kerotan"}, {jp ? gakoJp : std::string("GA-KO"), jp ? keroJp : std::string("Kerotan")}};
    bool changed = false;
    for (const auto& n : names)
        for (size_t p = t.find(n.first); p != std::string::npos; p = t.find(n.first, p + n.second.size())) {
            t.replace(p, n.first.size(), n.second); changed = true;
        }
    const uint32_t at = changed ? TsneStringOf(t) : 0;
    gTsneMessages[was] = at;
    return at;
}
// RESCUE's first messages of a round (L + 12 the sneakers', L + 17 the defenders'): TSNE's two targets and goals
// named; the language told by RESCUE's own text (English, French, Spanish), German and Italian by the HUD's
const char* kTsneRescueStart[3][2] = {
    {"Rescue the GA-KO and reach the goal!", "Protect the GA-KO at all costs!"},
    {"Secourez le GA-KO puis foncez vers l'objectif !", "Prot\x1F\x0Fgez le GA-KO \x1F\x09 tout prix !"},
    {"\x1F" "BRescata al GA-KO y llega a la meta!", "\x1F" "BProtege el GA-KO a toda costa!"},     // \x1F B: the inverted !
};
// TSNE's messages L + 12 .. 21 (kTsneEn .. kTsneIt): the GA-KO's, then the KEROTAN's where it differs; none: the
// script's own (its KEROTAN copy). German and Italian have all of them (the European scripts give English there)
struct TsneMsg { const char* gako; const char* kero; };
const TsneMsg kTsneMsgs[5][10] = {
    {{"Take the GA-KO or the Kerotan to a goal!", nullptr}, {}, {}, {}, {},
     {"Protect the GA-KO and the Kerotan at all costs!", nullptr}, {}, {}, {}, {}},
    {{"Emmenez le GA-KO ou le Kerotan vers un objectif !", nullptr}, {}, {}, {}, {},
     {"Prot\x1F\x0Fgez le GA-KO et le Kerotan \x1F\x09 tout prix !", nullptr}, {}, {}, {}, {}},
    {{"\x1F" "BLleva el GA-KO o el Kerotan a una meta!", nullptr}, {}, {}, {}, {},
     {"\x1F" "BProtege el GA-KO y el Kerotan a toda costa!", nullptr}, {}, {}, {}, {}},
    {{"Bringt das GA-KO oder den Kerotan zu einem Ziel!", nullptr},
     {"%s hat das GA-KO! Ab zum Ziel!", "%s hat den Kerotan! Ab zum Ziel!"},
     {"%s hat das GA-KO verloren! Holt es zur\x1F\x20" "ck!", "%s hat den Kerotan verloren! Holt ihn zur\x1F\x20" "ck!"},
     {"Das GA-KO wurde von %s aufgehoben!", "Der Kerotan wurde von %s aufgehoben!"},
     {"%s hat das GA-KO verloren!", "%s hat den Kerotan verloren!"},
     {"Besch\x1F\x20tzt das GA-KO und den Kerotan um jeden Preis!", nullptr},
     {"%s hat das GA-KO gestohlen! Holt es zur\x1F\x20" "ck!", "%s hat den Kerotan gestohlen! Holt ihn zur\x1F\x20" "ck!"},
     {"%s hat das GA-KO verloren! Besch\x1F\x20tzt es um jeden Preis!", "%s hat den Kerotan verloren! Besch\x1F\x20tzt ihn um jeden Preis!"},
     {"%s hat das GA-KO aufgehoben!", "%s hat den Kerotan aufgehoben!"},
     {"%s hat das GA-KO verloren!", "%s hat den Kerotan verloren!"}},
    {{"Portate il GA-KO o il Kerotan a un obiettivo!", nullptr},
     {"%s ha preso il GA-KO! Verso l'obiettivo!", "%s ha preso il Kerotan! Verso l'obiettivo!"},
     {"%s ha perso il GA-KO! Riprendetelo!", "%s ha perso il Kerotan! Riprendetelo!"},
     {"Il GA-KO \x1F\x10 stato raccolto da %s!", "Il Kerotan \x1F\x10 stato raccolto da %s!"},
     {"%s ha perso il GA-KO!", "%s ha perso il Kerotan!"},
     {"Proteggete il GA-KO e il Kerotan a ogni costo!", nullptr},
     {"Il GA-KO \x1F\x10 stato rubato da %s! Riprendetelo!", "Il Kerotan \x1F\x10 stato rubato da %s! Riprendetelo!"},
     {"%s ha perso il GA-KO! Proteggetelo a ogni costo!", "%s ha perso il Kerotan! Proteggetelo a ogni costo!"},
     {"%s ha raccolto il GA-KO!", "%s ha raccolto il Kerotan!"},
     {"%s ha perso il GA-KO!", "%s ha perso il Kerotan!"}},
};
// the scripts' language of this page (kTsneEn ...; -1 the Korean, whose texts stay the game's), German or Italian
// for English ones by the HUD's: told by RESCUE's explanation (L + 0x1F), once a page
template <class Get> int TsneTextLanguage(Get get) {
    static uint32_t gen = 0xFFFFFFFFu; static int lang = kTsneEn;
    if (gen != gPageGen) {
        gen = gPageGen;
        const std::string r = get(0x1D + kTsneAs);
        lang = r.rfind("Les deux", 0) == 0 ? kTsneFr : r.rfind("Dos equipos", 0) == 0 ? kTsneEs : kTsneEn;
        if (!r.empty() && (uint8_t)r[0] >= 0x80) lang = TsneVersionKr() ? -1 : kTsneJp;
    }
    if (lang == kTsneEn || lang == kTsneFr) { const int h = TsneHudLanguage(); if (h == 2) return kTsneDe; if (h == 3) return kTsneIt; }
    return lang;
}
std::map<std::string, uint32_t> gTsneTexts;            // TSNE's texts made (by their bytes): their copies
uint32_t TsneTextAt(const std::string& s) {
    if (s.empty()) return 0;
    auto it = gTsneTexts.find(s);
    if (it != gTsneTexts.end()) return it->second;
    return gTsneTexts[s] = TsneStringOf(s);
}
// the base's calls in a TSNE round on a map's page (US addresses); true when served
bool TsneBaseCall(Ctx& c, uint32_t addr, uint32_t va) {
    if (addr == 0x161200) {                              // a point by its code: the RESCUE spawns swapped when red sneaks
        if (gTsneSwap) {
            const uint32_t h = (uint32_t)c.r[7], to = TsneSpawnCode(h);
            if (to != h) {
                c.r[7] = to;
                if (Once(((uint64_t)145 << 40) | (uint64_t)(gTsneRoundNo & 0xFF) << 24 | (h & 0xFFFFFF))) Log("tsne: the spawn point %06x taken for %06x (the red team sneaks)", to & 0xFFFFFF, h & 0xFFFFFF);
            }
        }
        return false;
    }
    if (addr == 0x125408) {                              // a script string: RESCUE's explanation, messages (and name)
        const uint32_t g = TsneHud();                    // TSNE's
        if (!g) return false;
        const uint32_t base = R32(g + 0xDC);
        uint32_t i = (uint32_t)c.r[4];
        if (gTsneSwap && i >= base + 12 && i <= base + 21) {   // the red team sneaks: the messages by role
            const uint32_t was = i;
            i = base + kTsneRoleSwap[i - base - 12];
            c.r[4] = i;
            if (Once(((uint64_t)147 << 40) | (uint64_t)(gTsneRoundNo & 0xFF) << 8 | (was - base))) Log("tsne: RESCUE's message L+%u given as L+%u (by role: the red team sneaks)", was - base, i - base);
        }
        const bool help = i == base + 0x1D + kTsneAs, messages = i >= base + 12 && i <= base + 21;
        const bool name = i == base + 0x22 + kTsneAs;    // the rule's name: the Japanese programs' pages read it here
                                                         // (their 0x3C82B8 has no 0x3AFBA0: 0x1250D0(L + 0x22 + rule))
        static bool dumped = !getenv("SIM_TSNE_WATCH");     // tests: RESCUE's and CAPTURE's messages, the rules'
        if (!dumped && (help || (i >= base + 12 && i <= base + 22))) {   // explanations and names, once
            dumped = true;
            if (ps2::Fn f = ps2::FindFn(va))
                for (uint32_t k = 12; k <= 38; ++k) {
                    Ctx t = c; t.r[4] = base + k; f(t);
                    Log("tsne: message L+%u: \"%s\"", k, TsneCString((uint32_t)t.r[2] & 0x01FFFFFF).c_str());
                }
        }
        if (!help && !messages && !name) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        auto get = [&](uint32_t rel) { Ctx t = c; t.r[4] = base + rel; f(t); return TsneCString((uint32_t)t.r[2] & 0x01FFFFFF); };
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        const uint32_t was = (uint32_t)c.r[2] & 0x01FFFFFF;
        const std::string t = TsneCString(was);
        const size_t end = t.find_last_not_of("\r\n");
        const std::string body = end == std::string::npos ? std::string() : t.substr(0, end + 1), tail = t.substr(body.size());
        uint32_t at = 0;
        if (name) {                                      // "RESCUE MISSION\r\n" (the scripts' strings end so)
            if (body == "RESCUE MISSION") at = TsneTextAt(std::string(kTsneName) + tail);
        } else if (help) {
            const int lang = TsneTextLanguage(get);
            if (lang == kTsneJp) at = TsneTextAt(TsneCjk(kTsneJpHelp));
            else if (lang < 0) at = TsneTextAt(TsneCjk(kTsneKrHelp));
            else if (lang < 5) at = TsneString(gTsneHelpAt[lang], kTsneHelp[lang]);
            if (!at) at = TsneString(gTsneHelpAt[kTsneEn], kTsneHelp[kTsneEn]);
        } else {
            const uint32_t k = i - base;                 // 12 .. 21
            const bool start = k == 12 || k == 17, kero = !start && TsneKerotanEvent();
            const int lang = TsneTextLanguage(get);
            if ((lang == kTsneJp || lang < 0) && start) {
                const std::string own = TsneCjk(lang < 0 ? (k == 12 ? kTsneKrSneak : kTsneKrDefend) : (k == 12 ? kTsneJpSneak : kTsneJpDefend));
                if (!own.empty()) at = TsneTextAt(own + tail);
            } else if (lang < 0 && kero && (k == 13 || k == 18)) {   // Korean: CAPTURE's message of the same sense (its
                Ctx x = c;                               // particles follow the noun); the others say "the cargo"
                x.r[4] = base + (k == 13 ? 23 : 24);
                f(x);
                at = (uint32_t)x.r[2] & 0x01FFFFFF;
            } else if (lang >= 0 && lang < 5) {
                const TsneMsg& m = kTsneMsgs[lang][k - 12];
                const char* own = kero && m.kero ? m.kero : m.gako;
                // the first messages in English, French, Spanish where the script's is RESCUE's known one
                if (own && (lang >= kTsneDe || body == kTsneRescueStart[lang][k == 17 ? 1 : 0])) at = TsneTextAt(own + tail);
            }
            if (!at && kero && lang >= 0) {
                Ctx x = c;                               // CAPTURE's first message (L + 22): the script's KEROTAN glyphs
                x.r[4] = base + 22;
                f(x);
                at = TsneKerotanMessage(was, (uint32_t)x.r[2] & 0x01FFFFFF);
            }
            if (at && start && Once(((uint64_t)149 << 40) | (uint64_t)(gTsneRoundNo & 0xFF) << 8 | k)) Log("tsne: the round's first message L+%u made TSNE's (language %d)", k, lang);
        }
        if (at) { c.r[2] = at; c.r[3] = 0; }
        return true;
    }
    return false;
}
// a call while a map's page is loaded (DispatchBridge): the TSNE round's hooks
bool TsneBridge(Ctx& c, uint32_t addr, uint32_t va) {
    if (!TsneMapPage() || !TsneRound()) return false;
    if (addr < 0x2DA680) return TsneBaseCall(c, addr, va);
    const uint32_t a3 = gPageMap ? A3(addr) : addr;
    if (!a3) return false;
    return TsneMatchCall(c, va, a3);
}

// ---- the lobby (ntmenu2's Create Game 2, US addresses): TSNE first in the list of rules (list kind 7: 5 rows of the
// 15 its layout has, the rules 4 .. 0 from the top; 0x3605C0 a rule's row, 0x360610 a row's rule), offered on the maps
// whose bit its mask holds (the mask getters 0x349AD0 .. 0x349BA0 for rule 5), named from strings of ours
const uint32_t kTsneStrBase = 0x7100;                  // its texts as script strings (0x125408): name, short name, help line
uint32_t gTsneMask = 0;                                // the maps offering it (bit map - 1); 0: the lobby not set up
uint32_t gTsneRowsEn = 0;                              // the rotation's rows' tables of rule names (5 languages), the
uint32_t gTsneRowsLang[5] = {};                        // words before them: rule 5's (0x37B828: the table + 8 (4 - rule))
uint32_t gTsneRowEntries = 0;                          // TSNE's {key, name} in the 5 languages
const char* const* kTsneLobbyName = kTsneNames;   // en fr de it es (the menus' own letters: h É, p Ó)
const char* TsneVersion() { return ps2::SelectedVersion() ? ps2::SelectedVersion() : "US"; }
// kind 0 the name, 1 the short one, 2 the help line (this program's; the North American and European ones' in the
// language of the script's texts shown: English, French or Spanish, lang 0 1 2)
std::string TsneLobbyText(int kind, int lang) {
    if (kind == 0) return kTsneName;
    if (kind == 1) return "TSNE";
    const char* v = TsneVersion();
    if (!strcmp(v, "JP") || !strcmp(v, "JPS"))         // チーム・スニーキング in ntmenu2's own glyphs
        return std::string("\x82\x21\x83\x12\x82\x40\x9f\xa1\x82\x19\x82\x2b\x83\x12\x82\x0d\x82\x51\x82\x10", 20);
    if (!strcmp(v, "KR"))                               // 팀 잠입 미션 in ntmenu2's own glyphs
        return std::string("\x9c\x9a\x20\x9a\x85\x9a\x78\x20\x87\xdb\x98\xf1", 12);
    static const char* help[3] = {"Team Sneaking", "Infiltration par \x1F\x0F" "quipe", "Misi\x1F\x18n Furtiva en Equipo"};
    return help[lang >= 0 && lang < 3 ? lang : 0];
}
uint32_t gTsneLobbyStr[5] = {};                        // name, short name, help line (English, French, Spanish)
uint32_t TsneLobbyString(int kind, int lang = 0) {
    const int k = kind == 2 ? 2 + (lang >= 0 && lang < 3 ? lang : 0) : kind;
    if (!gTsneLobbyStr[k]) {
        const std::string t = TsneLobbyText(kind, lang);
        gTsneLobbyStr[k] = Alloc((uint32_t)t.size() + 1);
        if (gTsneLobbyStr[k]) memcpy(gMem + gTsneLobbyStr[k], t.c_str(), t.size() + 1);
    }
    return gTsneLobbyStr[k];
}
uint32_t gTsneRescueHelp = 0;                          // RESCUE's help line's string (its language: the help's)
int TsneLobbyLanguage(Ctx& c) {                        // 0 English, 1 French, 2 Spanish
    if (!gTsneRescueHelp) return 0;
    ps2::Fn f = ps2::FindFn(V(0x125408));
    if (!f) return 0;
    Ctx t = c;
    t.r[4] = gTsneRescueHelp;
    f(t);
    const uint32_t a = (uint32_t)t.r[2] & 0x01FFFFFF;
    if (a < 0x100000 || a + 16 >= 0x02000000) return 0;
    if (!memcmp(gMem + a, "Mission ", 8) || !memcmp(gMem + a, "Combat ", 7)) return 1;
    if (!memcmp(gMem + a, "Misi", 4) || !memcmp(gMem + a, "Partida", 7)) return 2;
    return 0;
}
bool TsneMapOffers(int id) {                           // a map offers TSNE: the twelve, an extra map that offers RESCUE
    if (id >= 1 && id <= 12) return true;                // (the rule TSNE plays as) or lists rule 5
    const ExtraMap* m = ExtraMapById(id);
    if (!m || !m->ready) return false;
    for (int r : m->rules) if (r == kTsneAs || r == kTsneRule) return true;
    return false;
}
void TsneLobby(uint32_t basesAt) {                     // after [27b5ee] ran (ntmenu2's script starts)
    gTsneMask = 0;
    for (int id = 1; id <= 32; ++id) if (TsneMapOffers(id)) gTsneMask |= 1u << (id - 1);
    const uint32_t kinds = PageData(0x3751FC, 0, 0x2DB958), langs = PageData(0x3751FC, 6, 0x6142F8);
    const uint32_t k7 = kinds + 7 * 0x2C;
    if (kinds < 0x100000 || R32(k7 + 0x1C) != 5 || !basesAt) { Log("tsne: the lobby's list of rules not found (%08x): TEAM SNEAKING not offered", kinds); gTsneMask = 0; return; }
    const uint32_t base6 = R32(basesAt + 6 * 4), items = R32(k7 + 0x20), idx = R32(k7 + 0x28);
    gTsneRescueHelp = base6 + R32(idx + 8 * (4 - kTsneAs) + 4);   // the rows hold the rules 4 .. 0
    static uint32_t pairs = 0, langTables[5] = {}, langNames[5] = {};   // made once, filled at each load of the page
    if (!pairs) pairs = Alloc(15 * 8);
    if (!pairs) { gTsneMask = 0; return; }
    W32(pairs, kTsneStrBase - base6); W32(pairs + 4, kTsneStrBase + 2 - base6);
    for (uint32_t k = 0; k < 14; ++k) { W32(pairs + 8 + 8 * k, R32(idx + 8 * k)); W32(pairs + 12 + 8 * k, R32(idx + 8 * k + 4)); }
    W32(k7 + 0x28, pairs);
    W32(k7 + 0x1C, 6);
    // the rows' texts in the North American and European programs (5 languages): TSNE in the top row, the others a row down
    int langsMoved = 0;
    for (uint32_t k = 0; k < 5 && langs >= 0x100000; ++k) {
        const uint32_t old = R32(langs + 7 * 0x14 + 4 * k);
        if (old < 0x100000 || old >= 0x02000000 || R32(old) != R32(items + 8)) continue;
        if (!langTables[k]) langTables[k] = Alloc(7 * 8);
        if (!langNames[k]) langNames[k] = Alloc((uint32_t)strlen(kTsneLobbyName[k]) + 1);
        const uint32_t t = langTables[k], name = langNames[k];
        if (!t || !name) break;
        memcpy(gMem + name, kTsneLobbyName[k], strlen(kTsneLobbyName[k]) + 1);
        W32(t, R32(items + 8)); W32(t + 4, name);
        for (uint32_t j = 0; j < 5; ++j) { W32(t + 8 + 8 * j, R32(items + 16 * (j + 1) + 8)); W32(t + 12 + 8 * j, R32(old + 8 * j + 4)); }
        W32(langs + 7 * 0x14 + 4 * k, t);
        ++langsMoved;
    }
    // the rotation's rows (0x37B828): each language's table, rule 5's entry just before it
    gTsneRowsEn = PageData(0x37B828, 9, 0x2E65C8);
    const uint32_t rowTables[5] = {gTsneRowsEn, PageData(0x37B828, 11, 0x2E73B8), PageData(0x37B828, 12, 0x2E8F98),
                                   PageData(0x37B828, 13, 0x2E81A8), PageData(0x37B828, 10, 0x2E9D88)};
    if (!gTsneRowEntries) gTsneRowEntries = Alloc(5 * 8);
    for (int k = 0; k < 5 && gTsneRowEntries; ++k) {
        gTsneRowsLang[k] = rowTables[k] ? rowTables[k] - 8 : 0;
        W32(gTsneRowEntries + 8 * k, rowTables[k] ? R32(rowTables[k]) : 0); W32(gTsneRowEntries + 8 * k + 4, langNames[k]);
    }
    Log("tsne: the lobby offers TEAM SNEAKING (the list of rules: 6 rows, %d languages' rows moved), its maps %#x", langsMoved, gTsneMask);
    TsneCjkCopy();                                     // the JP/KR texts' glyphs from the lobby's font
}
// ---- the lobby server's host list (Snake, 1 October): a game whose rotation holds TSNE is told as RESCUE MISSION (the
// server knows the rules 0 .. 4) with bit 7 of its options' second byte (0x4310: settings +0x2AA, the byte after the
// friendly fire one), which the server keeps and relays in each 0x4302 entry (tx55 GameOptions.Bitfield: +0x1B, +0x1C
// of the entry MGO1 keeps, 52 bytes each: +0x17 the round's rule, +0x18 its map). The stock programs read only 0x10
// of +0x1B and 0x02 of +0x1C (the host list's row 0x35A8C0, its filter 0x359EF0); a PC shows TSNE for its RES rounds
const uint8_t kTsneListBit = 0x80;
uint8_t TsneListMark(const uint8_t* rules, const uint8_t* maps) {   // the mark of a rotation (16 pairs)
    for (int i = 0; i < 16 && maps[i]; ++i) if (rules[i] == kTsneRule) return kTsneListBit;
    return 0;
}
bool gTsneRowMark = false;                             // a host list row of a TSNE game is drawn (0x37A900)
uint32_t gTsneShortEntries = 0;                        // TSNE's short name {key, text} in the five languages' tables
int TsneMaskCount() { int n = 0; for (int b = 0; b < 32; ++b) n += (gTsneMask >> b) & 1; return n; }
// the lobby's hooks (ntmenu2 loaded); true when served
bool TsneLobbyCall(Ctx& c, uint32_t addr, uint32_t va) {
    if (!gTsneMask) return false;
    const uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5];
    if (addr == 0x443658 && TsnePngOn()) {             // tests: pictures of Create Game 3 (a screen's text, a2): its
        static int state = 0, shots = 0;               // title, then each new help line while it is up (its rows)
        static uint32_t last = 0;
        const uint32_t t = (uint32_t)c.r[6] & 0x01FFFFFF;
        if (t < 0x100000 || t + 16 >= 0x02000000) return false;
        if (state == 0 && !memcmp(gMem + t, "Create Game 3", 13)) { state = 1; ++shots; TsnePngAsk("cg3", 60); }
        else if (state == 1 && !memcmp(gMem + t, "A game will be created", 22)) state = 2;
        else if (state == 1 && gMem[t] == '[' && shots < 10) {
            uint32_t h = 2166136261u;                  // a new help line: its picture
            for (uint32_t k = 0; k < 48 && gMem[t + k]; ++k) h = (h ^ gMem[t + k]) * 16777619u;
            if (h != last) { last = h; ++shots; TsnePngAsk("cg3", 20); }
        }
        return false;
    }
    switch (addr) {
    case 0x37A900: {                                   // a host list row (a1 its entry's name: entry + 4; t0 its round's rule)
        const uint32_t e = a1 - 4;
        if (e < 0x100000 || e + 0x34 >= 0x02000000 || !(R8(e + 0x1C) & kTsneListBit) || (c.r[8] & 0xFF) != kTsneAs) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint64_t ra = c.r[31];
        gTsneRowMark = true;
        f(c);
        gTsneRowMark = false;
        c.r[31] = ra;
        if (Once(((uint64_t)150 << 40) | R32(e))) {
            Log("tsne: the host list's game %u (%s) shown as TSNE (its options' mark)", R32(e), TsneCString(e + 4).c_str());
            TsnePngAsk("list", 50);
        }
        return true;
    }
    case 0x3605C0: if ((a0 & 0xFFFF) <= 5) { c.r[2] = 5 - (a0 & 0xFFFF); return true; } return false;   // a rule's row
    case 0x360610: if ((a0 & 0xFFFF) <= 5) { c.r[2] = 5 - (a0 & 0xFFFF); return true; } return false;   // a row's rule
    case 0x349AD0: if ((a0 & 0xFFFF) == 5) { c.r[2] = gTsneMask; return true; } return false;           // its maps' mask
    case 0x349AF0: if ((a0 & 0xFFFF) == 5) { c.r[2] = (uint32_t)TsneMaskCount(); return true; } return false;
    case 0x349B40:                                     // its n-th map (0xFFFF: none)
        if ((a0 & 0xFFFF) == 5) {
            uint32_t n = a1 & 0xFFFF, v = 0xFFFF;
            for (uint32_t b = 0; b < 32; ++b) if ((gTsneMask >> b) & 1) { if (!n) { v = b; break; } --n; }
            c.r[2] = v; return true;
        }
        return false;
    case 0x349BA0:                                     // a map's place in its list (0xFFFF: not offered)
        if ((a0 & 0xFFFF) == 5) {
            const uint32_t m = a1 & 0xFFFF;
            uint32_t v = 0xFFFF;
            if (m < 32 && ((gTsneMask >> m) & 1)) { v = 0; for (uint32_t b = 0; b < m; ++b) v += (gTsneMask >> b) & 1; }
            c.r[2] = v; return true;
        }
        return false;
    case 0x349AA0: {                                   // the rows' rule name in the Japanese and Korean programs (their
        const uint32_t ra = (uint32_t)c.r[31];         // rows' function 0x37B828: the Japanese one's shape differs)
        if (a0 == 6 && (a1 & 0xFFFF) == 5 && (ra == V(0x37B948) || (!gUsProgram && ps2::UsFnOf(ra) == 0x37B828))) {
            c.r[2] = kTsneStrBase; return true;
        }
        if (gTsneRowMark && a0 == 6 && (a1 & 0xFFFF) == kTsneAs + 5) { c.r[2] = kTsneStrBase + 1; return true; }   // RES: TSNE
        return false;
    }
    case 0x373DA0:                                     // a row's rule name in the five languages (rule 5: before the tables)
        if (gTsneRowEntries && gTsneRowsLang[0] && (uint32_t)c.r[6] == gTsneRowsLang[0]) {
            c.r[6] = gTsneRowEntries; c.r[7] = gTsneRowEntries + 8; c.r[8] = gTsneRowEntries + 16; c.r[9] = gTsneRowEntries + 24;
            c.r[10] = gTsneRowEntries + 32;
        }
        if (gTsneRowMark && (uint32_t)c.r[31] == V(0x37AF58)) {   // a marked host list row's rule column (US 0x37AF50): TSNE
            if (!gTsneShortEntries) gTsneShortEntries = Alloc(5 * 8);
            const uint32_t name = TsneLobbyString(1);
            for (int k = 0; k < 5 && gTsneShortEntries && name; ++k) {
                const uint32_t e = (uint32_t)c.r[6 + k];
                W32(gTsneShortEntries + 8 * (uint32_t)k, e >= 0x100000 && e < 0x02000000 ? R32(e) : 0);
                W32(gTsneShortEntries + 8 * (uint32_t)k + 4, name);
                c.r[6 + k] = gTsneShortEntries + 8 * (uint32_t)k;
            }
        }
        return false;
    case 0x125408:                                     // its strings
        if (a0 >= kTsneStrBase && a0 < kTsneStrBase + 3) {
            const int kind = (int)(a0 - kTsneStrBase);
            c.r[2] = TsneLobbyString(kind, kind == 2 ? TsneLobbyLanguage(c) : 0); c.r[3] = 0;
            return c.r[2] != 0;
        }
        return false;
    }
    return false;
}

// tests: the round's targets and goals (the GA-KO, the KEROTAN, the first and the second goal: x y z each), the side
// this player is on (1: his team carries the targets to the goals, 0: it defends them, -1: not known)
int TsneTestPoints(float out[4][3]) {
    const uint32_t mo = gTsneObjects;
    if (!TsneRound() || mo < 0x100000 || R32(mo + 0x410) > 3) return 0;
    int n = 0;
    for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t o = k < R32(mo + 0x410) ? mo + 0xB0 + 0x120 * k : 0;
        for (int j = 0; j < 3; ++j) out[k][j] = o ? F32(o + 0x90 + 4 * j) : 0.f;
        n += o != 0;
    }
    const bool sideB = R32(mo + 0x8B4) != 0;           // the sneakers' goals (side A's when the red team sneaks)
    const uint32_t goals = R32(mo + (sideB ? 0x8B4 : 0x8B0)), at = mo + (sideB ? 0x670 : 0x430);
    for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t g = k < goals ? at + 0xC0 * k : 0;
        for (int j = 0; j < 3; ++j) out[2 + k][j] = g ? F32(g + 0x90 + 4 * j) : 0.f;
        n += g != 0;
    }
    return n;
}
