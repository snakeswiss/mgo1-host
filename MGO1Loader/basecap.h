// basecap.h - BASE CAPTURE (BC), the lobby's seventh rule (docs/BASE_CAPTURE.md). Included by ps2world.cpp inside
// its namespace, after tsne.h (it uses its helpers: RoundRuleRaw, TsneLookup, TsneHud, TsneTeamOf, TsneString,
// TsneCString, La2Object).
//
// The rotation holds BC as rule 6; every read the game makes of the rotation (0x178490) gets CAPTURE MISSION (3), so
// the game runs CAPTURE's machinery (two teams, respawns, the team pages, the round's end and results); the mission
// objects of CAPTURE (class [bf76c9]) lose their KEROTAN and hold the bases in their goals' slots (3 of side A, 3 of
// side B: their models and radar marks), and the host decides each base's owner from the players standing in it.
#pragma once

const uint8_t kBcRule = 6;                             // the rotation's rule number of BASE CAPTURE
const uint8_t kBcAs = 3;                               // the rule the game plays it as (CAPTURE MISSION)
const int kBcMax = 5;                                  // bases a map at most
const float kBcRadius = 2300.0f;                       // a base's zone: horizontal distance (CAPTURE's goal check)
const float kBcBelow = 800.0f, kBcAbove = 2800.0f;     // and height band about its floor point
const float kBcStepTime[4] = {0.0f, 10.0f, 7.0f, 5.0f}; // seconds a step takes with 1, 2, 3 or more players inside
const float kBcDecayTime = 20.0f;                      // seconds an abandoned progress takes to empty
const uint32_t kBcCode = 0xBA5E00;                     // points.txt codes of an extra map's bases: 0xBA5E01 ..

int BcPlayedAs(int r) { return r == kBcRule ? kBcAs : r; }   // the rule the game plays (rotation reads, the lobby server)
bool BcPs2(uint32_t a, uint32_t n = 4) { return a >= 0x1000 && a + n < 0x02000000; }   // a PS2 address (the platform's heap is low)
bool BcRound() {                                       // the round played is BASE CAPTURE (read once a frame)
    static long long frame = -1;
    static bool on = false;
    if (frame != gFrameNo) { frame = gFrameNo; on = gNative && RoundRuleRaw() == kBcRule; }
    return on;
}

// ---- the bases of each map (docs/BASE_CAPTURE.md, The bases): x y z of their floor points (the world's unit is the
// millimetre), numbered BASE 1 (the red side) to BASE n (the blue side); chosen by tools/basecap_points.py on each
// map's collision, each zone over the floor (no drop beside it: map 15's BASE 1 and 4 moved in from the deck's edges in
// rc2); checked by tools/fair_sim.py (docs/FAIRNESS.md); a map folder's points.txt codes BA5E01 .. win
struct BcPoint { float x, y, z; };
struct BcStage { int n; BcPoint p[kBcMax]; };
const BcStage kBcStages[15] = {
    {5, {{-6500, 250, 18245}, {-16800, 152, 5145}, {-1200, 6, 7245}, {5200, -444, -355}, {-3000, 396, -14755}}},   // 1 Lost Forest
    {5, {{14850, -709, 27850}, {-27950, -191, 17150}, {-1150, 3675, 1850}, {-29550, -300, -10950}, {12850, -1788, -27750}}},   // 2 Ghost Factory
    {5, {{30240, 0, 8850}, {31040, 0, 35250}, {13840, 4000, 22850}, {-16160, 0, 12450}, {-6160, 2000, 38450}}},   // 3 City Under Siege
    {4, {{539, 6, -6750}, {-12961, 0, 1050}, {12439, 0, 50}, {39, 6, 8050}}},                   // 4 Killhouse A
    {4, {{-5038, 0, -4012}, {-15638, 0, 6688}, {16062, 0, -6812}, {5162, 0, 3988}}},               // 5 Killhouse B
    {4, {{-3738, 0, -11738}, {-15038, 0, 5762}, {15962, 0, -5138}, {4162, 0, 12962}}},             // 6 Killhouse C
    {5, {{46025, 1000, -72004}, {39925, -1960, -42204}, {16525, 4181, -60704}, {50525, 1014, -23504}, {14025, 3969, -35604}}},   // 7 Svyatogornyj East
    {5, {{134006, 72598, -184750}, {125106, 70438, -190150}, {124106, 72576, -200350}, {108706, 74875, -202750}, {117706, 80875, -226350}}},   // 8 Mountaintop
    {4, {{21225, 0, -9851}, {26825, 0, -24651}, {27025, 3000, -37751}, {16025, 6000, -29451}}},    // 9 Graniny Gorki Lab
    {4, {{-13100, 0, -15150}, {13300, -3132, -18750}, {14400, -3386, 18050}, {-12700, 0, 15250}}},   // 10 Pillbox Purgatory
    {5, {{25050, -1028, -16550}, {-7650, -216, -23150}, {3050, 0, -950}, {3450, 0, 24250}, {-20150, -625, 15450}}},   // 11 High Ice
    {5, {{17650, 0, -18175}, {26350, 0, 17125}, {-3150, -4000, -40175}, {-3250, 125, 28025}, {-36350, 0, -23875}}},   // 12 Brown Town
    {4, {{23850, -6000, 7850}, {5650, -6000, 2750}, {7450, -6000, -14150}, {32650, -8325, -23750}}},   // 13 SILO ENTRANCE
    {4, {{-7150, 8000, -1750}, {-6350, 4000, -10950}, {6450, 4000, -10950}, {7250, 8000, -1750}}},   // 14 REX HANGAR
    {4, {{44050, 11500, -85350}, {47250, 11500, -110150}, {66150, 11500, -81050}, {68850, 11500, -106150}}},   // 15 BIG SHELL STRUT E
};

int gBcPngMap = -1;                                    // SIM_BC_PNG_FIRST=1: pictures in each map's first round only
bool gBcPngOpen = true;
bool gBcHarness = false;                               // the test harness asked for the bases (BcTestBases ..)
float BcStepScale() {                                  // tests (SIM_BC_STEP_SCALE, the harness only): shorter steps
    static const float scale = getenv("SIM_BC_STEP_SCALE") ? (float)atof(getenv("SIM_BC_STEP_SCALE")) : 1.0f;
    return gBcHarness && scale >= 0.05f && scale < 1.0f ? scale : 1.0f;
}

// ---- the round's state
struct BcBase {
    float x = 0, y = 0, z = 0;
    int owner = -1;                                    // -1 neutral (grey), 0 red, 1 blue
    int toward = -1;                                   // the team whose progress fills (-1 none)
    float prog = 0;                                    // 0 .. 1 of a step (neutralize an enemy base, or capture a neutral one)
    bool contested = false;
    int in[2] = {0, 0};                                // players of each team inside (the host's count)
    long long since[32] = {};                          // the frame each slot came in (0: out)
    int capturer = -1;                                 // the slot of the last capture (or neutralization)
    uint8_t events = 0;                                // a count of its captures and neutralizations (the network's)
    int shownOwner = -1;                               // the colour shown (models, radar)
};
struct BcState {
    uint32_t mo = 0;                                   // CAPTURE's mission objects of this round
    int map = 0, n = 0;
    BcBase b[kBcMax];
    long long lastFrame = 0;                           // the host's last step (fields: the game's own time)
    uint32_t round = 0;                                // a number for this round (the network's), the host's
    bool decided = false;
    long long aliveAt = -1;                            // this player alive since (the round's first instruction)
    bool hintShown = false;
    float death[3] = {};                               // where this player was last alive (the respawn's base)
    bool haveDeath = false;
    unsigned long long heardMs = 0;                    // a joiner: the host's last state
    uint32_t hostRound = 0;                            // a joiner: the host's number of this round (its states')
    uint32_t pngDone = 0;                              // tests (SIM_BC_PNG): the moments pictured this round
    int lives = 0;                                     // this player's lives this round
};
BcState gBc;
uint32_t gBcPrevHostRound = 0;                         // a joiner: the host's number of the round before (its late states left)
unsigned long long gBcPrevAt = 0;                      // when it was set (its states left for 3 s only: the host may not restart)
bool gBcWarned[kBcMax] = {};                           // a base of this player's team under attack, told (BcAttackWarn)
long long gBcPass = 0, gBcMapPass = -100, gBcRadarPass = -100;   // the actor passes (BcTick): the marks' last draws
bool BcObjectsAlive() {                                // this round's mission objects still there (their class's die not run)
    const auto it = gNativeActors.find(gBc.mo);
    return gBc.mo && it != gNativeActors.end() && it->second == "NewOnlineItemChara2";
}
uint32_t BcSup() {                                     // the supervisor (n003a 0x2F0C90)
    const uint32_t at = PageData(0x3ECBA8, 0, 0x2F0C90);
    const uint32_t s = at ? R32(at) & 0x01FFFFFF : 0;
    return s >= 0x100000 && s + 0x400 < 0x02000000 ? s : 0;
}
uint32_t BcRecord() {                                  // the round's record ([supervisor + 0x3BC]: +0 the time left, +4 the result)
    const uint32_t s = BcSup(), r = s ? R32(s + 0x3BC) & 0x01FFFFFF : 0;
    return r >= 0x100000 && r + 0x20 < 0x02000000 ? r : 0;
}
bool BcRunning() {                                     // the round's time is set and it has no result yet
    const uint32_t r = BcRecord();
    return r && (int32_t)R32(r) > 0 && !R8(r + 4);
}
uint32_t gBcPhase = 0;                                 // the host's supervisor phase (n003a 0x3ECBA8, +0x3AC): the time runs at 2 and 3
bool BcStarted() { return gBcPhase >= 2 && gBcPhase < 4; }   // the round started (both teams in: no capture before)
int BcOwned(int team) { int k = 0; for (int i = 0; i < gBc.n; ++i) k += gBc.b[i].owner == team; return k; }

// a base's points: the table's, or an extra map's points.txt's (kBcCode + 1 ..)
int BcPlace(Ctx& c, int map, BcPoint* out) {
    int n = 0;
    for (int k = 1; k <= kBcMax; ++k) {
        const TsnePoint p = TsneLookup(c, kBcCode + (uint32_t)k);
        if (!p.ok) break;
        out[n++] = {p.x, p.y, p.z};
    }
    if (n >= 3) return n;
    if (map >= 1 && map <= 15 && kBcStages[map - 1].n) {
        for (int k = 0; k < kBcStages[map - 1].n; ++k) out[k] = kBcStages[map - 1].p[k];
        return kBcStages[map - 1].n;
    }
    return 0;
}

// ---- the mission objects (CAPTURE's class [bf76c9], US n003a options 0x39E0B0: +0x410 the objects, +0x8B0 / +0x8B4
// the goals of side A / B, 3 each, 0xC0 from +0x430 / +0x670, the place at +0x90 x y z w; the init that follows makes
// each goal's models (side A red, B blue: 0x39E6F8) and radar mark (0x399F90)): the KEROTAN taken out, the bases in
// the goals' slots (1 .. 3 side A, 4 .. 5 side B)
void BcWF(uint32_t a, float v) { uint32_t u; memcpy(&u, &v, 4); W32(a, u); }
void BcObjects(Ctx& c, uint32_t mo) {
    if (mo < 0x100000 || mo + 0x8C0 >= 0x02000000) return;
    const int map = RoundMap();
    BcPoint p[kBcMax];
    const int n = BcPlace(c, map, p);
    if (gBc.hostRound) { gBcPrevHostRound = gBc.hostRound; gBcPrevAt = PsMs(); }
    gBc = BcState();
    for (bool& w : gBcWarned) w = false;
    gBc.mo = mo; gBc.map = map;
    if (map != gBcPngMap) { gBcPngMap = map; gBcPngOpen = true; }   // tests: a new map's first round pictured
    gBc.round = (uint32_t)(gFrameNo & 0x7FFFFFFF) | 1;
    if (n < 3) { Log("basecap: map %d has no bases (%d points): CAPTURE's objects left as they are", map, n); return; }
    gBc.n = n;
    W32(mo + 0x410, 0);                                // no KEROTAN
    const int a = n > 3 ? 3 : n, bside = n - a;
    W32(mo + 0x8B0, (uint32_t)a); W32(mo + 0x8B4, (uint32_t)bside);
    for (int k = 0; k < n; ++k) {
        const uint32_t g = k < 3 ? mo + 0x430 + 0xC0 * (uint32_t)k : mo + 0x670 + 0xC0 * (uint32_t)(k - 3);
        BcWF(g + 0x90, p[k].x); BcWF(g + 0x94, p[k].y); BcWF(g + 0x98, p[k].z); BcWF(g + 0x9C, 1.0f);
        gBc.b[k].x = p[k].x; gBc.b[k].y = p[k].y; gBc.b[k].z = p[k].z;
        gBc.b[k].shownOwner = k < 3 ? 0 : 1;           // the init makes side A's models red, side B's blue
    }
    Log("basecap: map %d: %d bases (the mission objects %08x: the KEROTAN out, goals %d/%d)", map, n, mo, a, bside);
    for (int k = 0; k < n; ++k) Log("basecap:   BASE %d at %.0f %.0f %.0f", k + 1, p[k].x, p[k].y, p[k].z);
}

// ---- the players (each pass): every player actor of this program (the local one and the others' copies)
struct BcPlayer { uint32_t pl; int slot, team; float x, y, z; bool alive; };
int BcPlayers(BcPlayer* out, int most) {
    int n = 0;
    for (const auto& a : gNativeActors) {
        if (n >= most) break;
        if (a.second != "NewNetworkPlayer" && a.second != "NewNetworkDummyPlayer") continue;
        const uint32_t pl = a.first;
        if (pl < 0x100000 || pl + 0xC00 >= 0x02000000) continue;
        BcPlayer& p = out[n];
        p.pl = pl; p.slot = R8(pl + 0xB60); p.team = TsneTeamOf(pl);
        p.x = F32(pl + 0x110); p.y = F32(pl + 0x114); p.z = F32(pl + 0x118);
        p.alive = (int16_t)R16(pl + 0xB64) > 0 && !(R32(pl + 0xB48) & 0x1000000);
        if (p.slot >= 32 || (p.team != 0 && p.team != 1)) continue;
        ++n;
    }
    return n;
}
bool BcInside(const BcBase& b, float x, float y, float z) {
    const float dx = x - b.x, dz = z - b.z, dy = y - b.y;
    return dx * dx + dz * dz <= kBcRadius * kBcRadius && dy >= -kBcBelow && dy <= kBcAbove;
}
std::string BcName(int slot) {                         // a player's name (the player table's entry + 0x28, 0x1738E8)
    const uint32_t table = V(0x28B5F4);
    for (int k = 0; k < 16; ++k) {
        const uint32_t e = table + 0x5C * (uint32_t)k;
        if (R16(e + 2) && R8(e) == (uint32_t)slot) return TsneCString(e + 0x28);
    }
    return "PLAYER" + std::to_string(slot);
}

// ---- the texts' language: the round's scripts' (English and the program's second one: the North American Spanish,
// the European French; the Japanese programs' Japanese, the Korean program's Korean); where the scripts' is English,
// the HUD's (link variables + 4: the European program's German, Italian, Spanish)
enum { kBcEn, kBcFr, kBcEs, kBcDe, kBcIt, kBcJp, kBcKr, kBcLangs };
std::string BcScriptText(Ctx& c, uint32_t rel) {       // the script's string L + rel (L: the HUD's game record + 0xDC)
    const uint32_t g = TsneHud();
    ps2::Fn f = ps2::FindFn(V(0x125408));
    if (!g || !f) return std::string();
    Ctx t = c;
    t.r[4] = R32(g + 0xDC) + rel;
    f(t);
    return TsneCString((uint32_t)t.r[2] & 0x01FFFFFF);
}
int BcHudLanguage() {                                  // 0 English, 1 French, 2 German, 3 Italian, 4 Spanish
    const uint32_t lv = R32(V(0x23A678)) & 0x01FFFFFF;
    if (!BcPs2(lv, 8)) return 0;
    const uint16_t l = R16(lv + 4);
    return l >= 2 && l <= 5 ? (int)l - 1 : 0;
}
int BcLanguage(Ctx& c) {                               // told by CAPTURE's explanation (L + 0x1D + 3), once a page
    static uint32_t gen = 0xFFFFFFFFu;
    static int lang = kBcEn;
    if (gen != gPageGen) {
        const std::string r = BcScriptText(c, 0x1D + kBcAs);
        if (r.empty()) return kBcEn;                   // no HUD yet: asked again
        gen = gPageGen;
        lang = r.rfind("Deux ", 0) == 0 ? kBcFr : r.rfind("Dos ", 0) == 0 ? kBcEs : kBcEn;
        if ((uint8_t)r[0] >= 0x80) lang = strcmp(TsneVersion(), "KR") ? kBcJp : kBcKr;
    }
    if (lang == kBcEn) {                               // English scripts: the HUD's language where we have it
        static const int of[5] = {kBcEn, kBcFr, kBcDe, kBcIt, kBcEs};
        return of[BcHudLanguage()];
    }
    return lang;
}
// the Japanese and Korean texts (tools/basecap_cjk.py, TEAM SNEAKING's encoder tools/tsne_cjk.py): in a match the rules
// pages' resident font holds only its base glyphs (the kana and signs, the Korean program's commonest syllables): the
// base set (kBcJp*, kBcKr*: the Japanese in kana, the Korean explanation English); the full set (kanji, hangul) where
// TEAM SNEAKING copies the lobby font's glyphs into the font's page 3 (tsne.h TSNE_CJK: TsneCjkAdd, TsneCjk)
const char kBcJpHelp[] = "\x81" "5\x81\x1f\x81$\x81.\x82!\x83\x12\x82@\x81\x0c\x82>\xc2#\x82" "7\x81.\x82" "9\x83\x12\x82\x19\x81P\x81\x06\x81" "0\x81\x04\x81\x02\x81\x06\xc3\x09\n\x82" "9\x83\x12\x82\x19\x81+\x81(\x81)\x81>\x81K\x81(\x81\x1b\x81\x04\x81\x02\x81$\xc3\x09\x81*\x81\x0b\x81>\x81\x0c\x81\n\x81\n\x81\x04\x81;\x81)\x81/\x81" "D\x81\x04\xc3\x09\n\x81\x1b\x81\x04\x81\x02\x81$\x81\x17\x81\x1f\x82" "9\x83\x12\x82\x19\x81/\x82J\x82\x19\x82=\x83\x12\x82Q\x81.\x81" "0\x81\x17\xc1G\x81+\x81*\x81K\xc3\x09\n\x81\x19\x81" "9\x81&\x81(\x81K\x81\x0b\xc3\x08\x82\x1f\x82\x04\x82@\x82\x02\xc2#\x82" "7\x81'\x81\n\x81\n\x81\x0f\x81" "B\x81$\x82!\x83\x12\x82@\x81.\x81\x0b\x81!\xc3\x09\n";   // 200 bytes
const char kBcJpStart[] = "\x82" "9\x83\x12\x82\x19\x81P\x81\x1b\x81\x04\x81\x02\x81$\x81\x1b\x81H\xc3\x06\x81\n\x81\n\x81\x0f\x81" "B\x81$\x82!\x83\x12\x82@\x81.\x81\x0b\x81!\x81 \xc3\x06";   // 48 bytes
const char kBcJpCaptured[] = "%s\x81\x0c" "BASE %d\x81P\x81\x1b\x81\x04\x81\x02\x81$\xc3\x06";   // 23 bytes
const char kBcJpNeutral[] = "%s\x81\x0c" "BASE %d\x81P\x81!\xc1" "E\x81\x06\x81J\x81$\x81+\x81\x17\x81\x1f\xc3\x06";   // 31 bytes
const char kBcKrStart[] = "\x81\x0e\x81\x04\x81\x15 \x81:\x82" "D\x81\x09\x81\x16!";   // 16 bytes
const char kBcKrCaptured[] = "%s, BASE %d \x81:\x82" "D!";   // 17 bytes
const char kBcKrNeutral[] = "%s, BASE %d \x81\x1e\x81" "4!";   // 17 bytes
const char kBcJpFullHelp[] = "2\x82!\x83\x12\x82@\x81'\x82>\xc2#\x82" "7\x99n\x81.\x82" "9\x83\x12\x82\x19\x81P\x9b\x06\x81\x04\x87\x98\x81\x04\x81>\x81\x19\xc3\x09\x82" "9\x83\x12\x82\x19\x81.\x9bG\x81+\n\x81(\x81)\x81>\x81K\x81(\x99\xee\x84\x15\x81'\x81\x0d\xc3\x08\x9eU\x9d\xf2\x81\x0c\x9a\xc1\x81\x04\x81;\x81)\x9a\x85\x81\x0f\x99\xee\x84\x15\x81'\x81\x0d\x81>\x81\x19\xc3\x09\n\x99\xee\x84\x15\x81\x17\x81\x1f\x82" "9\x83\x12\x82\x19\x81/\x82J\x82\x19\x82=\x83\x12\x82Q\x9b" "0\x9b\xdd\x81+\x81*\x81J\x81>\x81\x19\xc3\x09\x9aX\x82" "9\x83\x12\x82\x19\x81P\n\x99\xee\x84\x15\x81\x19\x81K\x81\x0b\xc3\x08\x98l\x85\xaf\x9a \x81L\x81.\x98l\x81+\x9a\xc1\x81\x0f\x98k\x81$\x82!\x83\x12\x82@\x81.\x99*\x9f\x09\x81'\x81\x19\xc3\x09\n";   // 213 bytes
const char kBcJpFullStart[] = "\x82" "9\x83\x12\x82\x19\x81P\x99\xee\x84\x15\x81\x1b\x81H\xc3\x06\x9a\xc1\x81\x0f\x98k\x81$\x82!\x83\x12\x82@\x81.\x99*\x9f\x09\x81 \xc3\x06";   // 42 bytes
const char kBcJpFullCaptured[] = "%s\x81\x0c" "BASE %d\x81P\x99\xee\x84\x15\xc3\x06";   // 19 bytes
const char kBcJpFullNeutral[] = "%s\x81\x0c" "BASE %d\x81P\x9bG\x9f\x18\x84\xd9\xc3\x06";   // 21 bytes
const char kBcKrFullHelp[] = "\x82\x1a \x9c\x9a\x81\x01 \x87{ \x82\x04\x81\x10 \x81\x0e\x81\x04\x81\x15 \x82\x1a\x81\x12 \x99W\x9a" "9\x81&\x81\x02. \x81\x0e\x81\x04 \x81G\x81\x0b\n\x87\x84\x81,\x82" "0\x81? \x9a\xa3\x82\x03\x81; \x81\" \x81\x17\x81\x12, \x81\x08\x81" "0\x81\x01 \x87j\x81\x07\x81\"\x87\" \x98j\x87Y\x81&\x81\x02.\n\x9a\xa3\x82\x03\x81\x11 \x81\x0e\x81\x04\x81\x05 \x81\x0c\x81\x14\x9c\xd3 \x81\x04\x9a\xa3\x81\x01 \x86" "a\x81&\x81\x02. \x81" "1\x82\x0f \x81\x0e\x81\x04\x81\x15\n\x9a\xa3\x82\x03\x81\x09\x81" "5\x81\x0d \x81%\x82@\x81\x01 \x85z\x82\x19 \x82" "B \x82\" \x87j\x81\x01 \x81\x03\x82\x14 \x9c\x9a\x81\x01 \x99K\x81\x0c\x9d\x0e\x81&\x81\x02.\n";   // 203 bytes
const char kBcKrFullStart[] = "\x81\x0e\x81\x04\x81\x15 \x9a\xa3\x82\x03\x81\x09\x81\x16! \x82\" \x87j\x81\x01 \x81\x03\x82\x14 \x9c\x9a\x81\x01 \x99K\x81\x0c\x81\x11\x81\x02!";   // 44 bytes
const char kBcKrFullCaptured[] = "%s, BASE %d \x9a\xa3\x82\x03!";   // 17 bytes
const char kBcKrFullNeutral[] = "%s, BASE %d \x82(\x87" "d\x9d" "D!";   // 19 bytes
bool BcJpProgram() { const char* v = TsneVersion(); return !strcmp(v, "JP") || !strcmp(v, "JPS"); }
bool BcKrProgram() { return !strcmp(TsneVersion(), "KR"); }
#ifdef TSNE_CJK
void BcCjkAdd() {                                      // at the lobby (BcLobby): the full texts' glyphs copied to page 3 too
    static const char* const jp[] = {kBcJpFullHelp, kBcJpFullStart, kBcJpFullCaptured, kBcJpFullNeutral};
    static const char* const kr[] = {kBcKrFullHelp, kBcKrFullStart, kBcKrFullCaptured, kBcKrFullNeutral};
    if (!BcJpProgram() && !BcKrProgram()) return;
    for (const char* t : BcKrProgram() ? kr : jp) TsneCjkAdd(t);
    TsneCjkCopy();
}
std::string BcCjk(const char* full, const char* base) { const std::string t = TsneCjk(full); return t.empty() ? std::string(base) : t; }
#else
void BcCjkAdd() {}
std::string BcCjk(const char*, const char* base) { return base; }
#endif
std::map<std::string, uint32_t> gBcTexts;              // the texts made (by their bytes): their copies in the PS2's memory
uint32_t BcTextAt(const std::string& s) {
    if (s.empty()) return 0;
    auto it = gBcTexts.find(s);
    if (it != gBcTexts.end()) return it->second;
    const uint32_t at = Alloc((uint32_t)s.size() + 1);
    if (at) memcpy(gMem + at, s.c_str(), s.size() + 1);
    gBcTexts[s] = at;
    return at;
}

// ---- tests (SIM_BC_PNG=1, or a list of: event,contested,gauge,map,result,spawn,respawn,type,list,hostlist,attack,menu,hint): a picture of each moment
// (gs_<frame>_<what>.png), the GS drawing for the kBcPngLead frames before it (ps2world.cpp gTestDrawUntil)
extern long long gTestDrawUntil;
struct BcPng { long long at; std::string what; };
std::vector<BcPng> gBcPngs;
bool BcPngWants(const char* what) {
    static const char* e = getenv("SIM_BC_PNG");
    if (!e) return false;
    if (!strcmp(e, "1")) return true;
    const size_t n = strlen(what);
    for (const char* q = strstr(e, what); q; q = strstr(q + n, what))
        if ((q == e || q[-1] == ',') && (q[n] == 0 || q[n] == ',')) return true;
    return false;
}
const long long kBcPngLead = 12;                       // frames drawn before a picture (a slow tick's build in them)
bool BcPngFirst() { static const bool on = getenv("SIM_BC_PNG_FIRST") != nullptr; return on; }
void BcPngAsk(const char* what, int frames, uint32_t once = 0) {   // once: a bit of gBc.pngDone (a moment once a round)
    if (!BcPngWants(what) || gBcPngs.size() >= 8 || (once && (gBc.pngDone & once)) || (BcPngFirst() && !gBcPngOpen)) return;
    gBc.pngDone |= once;
    gBcPngs.push_back({gFrameNo + frames, what});
}
// tests: SIM_BC_DRAWMODE="m@L,m@L": the HUD's texts drawn from the start of every pass (0), of a pass that starts at DG's
// phase 1 (1) or 0 (2), from the radar's and the 2D map's hooks (3), after a tick's pass (4) or both 0 and 4 (5, the
// default), once the round's time left is L seconds or less; SIM_BC_BURST="L:count,...": a picture of each of `count`
// frames from the time left L (gs_<frame>_b<mode>.png), the passes logged
float BcTimeLeftNow() { const uint32_t r = BcRecord(); return r && !R8(r + 4) ? (float)(int32_t)R32(r) / 300.0f : 1e9f; }
std::vector<std::pair<float, long long>> BcTestPairs(const char* env) {
    std::vector<std::pair<float, long long>> v;
    if (const char* e = getenv(env))
        for (const char* q = e; *q;) {
            char* end = nullptr; const float a = strtof(q, &end);
            if (end == q || (*end != '@' && *end != ':')) break;
            const long long b = strtoll(end + 1, &end, 10);
            v.push_back({a, b});
            q = *end ? end + 1 : end;
        }
    return v;
}
const int kBcDrawDefault = 5;                          // the HUD's texts: from the passes' start and once a tick's pass ran (BcPassAfter)
int BcDrawMode() {                                     // SIM_BC_DRAWMODE: "m@L" read as {m, L}
    static const std::vector<std::pair<float, long long>> at = BcTestPairs("SIM_BC_DRAWMODE");
    if (at.empty()) return kBcDrawDefault;
    const float left = BcTimeLeftNow();
    int m = kBcDrawDefault;
    for (const auto& p : at) if (left <= (float)p.second) m = (int)p.first;
    return m;
}
bool BcInBurst(long long f) {
    static const std::vector<std::pair<float, long long>> b = BcTestPairs("SIM_BC_BURST");
    static std::vector<long long> from;
    if (b.empty()) return false;
    if (from.size() != b.size()) from.assign(b.size(), -1);
    const float left = BcTimeLeftNow();
    for (size_t k = 0; k < b.size(); ++k) {
        if (from[k] < 0 && left <= b[k].first && BcRound() && gBc.n) from[k] = gFrameNo + 3;
        if (from[k] >= 0 && f >= from[k] && f < from[k] + b[k].second) return true;
    }
    return false;
}
bool gBcInMarks = false;                               // in the radar's or the 2D map's hook (BcMatchCall)
bool gBcAfterTick = false;                             // after a tick's pass ran its actors (BcPassAfter)
bool BcDrawPass() {                                    // this pass draws the HUD's texts (tests: SIM_BC_DRAWMODE)
    const int m = BcDrawMode();
    const uint32_t phase = R32(kHalfPhase);
    const bool draw = m == 3 ? gBcInMarks : m == 4 ? gBcAfterTick : m == 5 ? !gBcInMarks :
                      !gBcInMarks && !gBcAfterTick && (m == 0 || (m == 1 && phase == 1) || (m == 2 && phase == 0));
    static long long logged = -1;
    if (BcInBurst(gFrameNo) && logged != gFrameNo * 4 + (long long)phase) {
        logged = gFrameNo * 4 + (long long)phase;
        const uint32_t font = V(0x27F340), pack = R32(font + 0x44) & 0x01FFFFFF;
        Log("basecap draw: frame %lld phase %u clock %u half %u mode %d: %s (the font's write %08x, its packets %08x %08x)", gFrameNo, phase,
            R32(V(0x23AFD0)) & 1, R32(kHalfRate), m, draw ? "drawn" : "not drawn", R32(font + 0x1C0), BcPs2(pack) ? R32(pack + 0x18) : 0, BcPs2(pack) ? R32(pack + 0x1C) : 0);
    }
    return draw;
}
void BcPngTick() {
    static long long lastBurst = -1;
    if (BcInBurst(gFrameNo + 3) || BcInBurst(gFrameNo)) { gTestDrawUntil = std::max(gTestDrawUntil, gFrameNo + 1); ps2::gGsDraw = true; }
    if (BcInBurst(gFrameNo) && lastBurst != gFrameNo) {
        lastBurst = gFrameNo;
        char name[96];
        snprintf(name, sizeof name, "gs_%05lld_b%d.png", gFrameNo, BcDrawMode());
        Log("basecap: picture %s %s", name, ps2::GsWritePng(name) ? "written" : "not written");
    }
    if (gBcPngs.empty()) return;
    for (size_t k = 0; k < gBcPngs.size();) {
        if (gFrameNo < gBcPngs[k].at) {
            if (gFrameNo >= gBcPngs[k].at - kBcPngLead) { gTestDrawUntil = std::max(gTestDrawUntil, gBcPngs[k].at); ps2::gGsDraw = true; }
            ++k; continue;
        }
        char name[96];
        snprintf(name, sizeof name, "gs_%05lld_%s.png", gFrameNo, gBcPngs[k].what.c_str());
        Log("basecap: picture %s %s", name, ps2::GsWritePng(name) ? "written" : "not written");
        gBcPngs.erase(gBcPngs.begin() + (long)k);
    }
    static const long long from = getenv("SIM_GS_DRAW_FROM") ? atoll(getenv("SIM_GS_DRAW_FROM")) : 0;
    if (gBcPngs.empty() && from && gFrameNo < from) ps2::gGsDraw = false;
}

// ---- the messages (the HUD's message strip at the top, n003a 0x3D5050(strip, L-relative index, name)): served as the
// scripts' strings kBcStrBase .. (0x125408), in the texts' language
const uint32_t kBcStrBase = 0x7200;                    // lobby: name, short, help (3); match: messages from + 0x10
enum BcMsg { kBcMsgStart = 0, kBcMsgCaptured, kBcMsgNeutral, kBcMsgCount };
const char* kBcMessages[5][kBcMsgCount] = {           // en fr es de it ("\x1F" and a code: the scripts' accents)
    {"Capture the bases! The team holding the most wins.", "%s captured BASE %d!", "%s neutralized BASE %d!"},
    {"Capturez les bases ! L'\x1F\x0Fquipe qui en tient le plus gagne.", "%s a captur\x1F\x0F la BASE %d !", "%s a neutralis\x1F\x0F la BASE %d !"},
    {"\x1F" "BCaptura las bases! Gana el equipo con m\x1F\x08s bases.", "\x1F" "B%s ha capturado la BASE %d!", "\x1F" "B%s ha neutralizado la BASE %d!"},
    {"Erobert die Basen! Das Team mit den meisten gewinnt.", "%s hat BASIS %d erobert!", "%s hat BASIS %d neutralisiert!"},
    {"Conquistate le basi! Vince la squadra che ne tiene di pi\x1F\x1E.", "%s ha conquistato la BASE %d!", "%s ha neutralizzato la BASE %d!"},
};
uint32_t BcMessageString(Ctx& c, int kind, int base) {   // the message's string (its %d made the base's number; %s kept)
    const int lang = BcLanguage(c), b = base >= 0 && base <= kBcMax ? base : 0;
    std::string s;
    if (lang == kBcJp) s = kind == kBcMsgStart ? BcCjk(kBcJpFullStart, kBcJpStart) : kind == kBcMsgCaptured ? BcCjk(kBcJpFullCaptured, kBcJpCaptured) : BcCjk(kBcJpFullNeutral, kBcJpNeutral);
    else if (lang == kBcKr) s = kind == kBcMsgStart ? BcCjk(kBcKrFullStart, kBcKrStart) : kind == kBcMsgCaptured ? BcCjk(kBcKrFullCaptured, kBcKrCaptured) : BcCjk(kBcKrFullNeutral, kBcKrNeutral);
    else s = kBcMessages[lang][kind];
    const size_t d = s.find("%d");
    if (d != std::string::npos) s.replace(d, 2, std::to_string(b));
    return BcTextAt(s + "\r\n");
}
uint32_t gBcMsgIndex = 0;                              // the string the strip is asked for next (0x125408)
// the strip's text node (its layout [strip + 0x40]'s node C40945: +0x7C its colour, R G B A, 0x80 full) in the
// capturer's team's colour, white for the others
const uint32_t kBcStripColour[3] = {0x803838D8u, 0x80D86040u, 0x80808080u};   // red, blue, white (A B G R)
uint32_t gBcStripColour = 0;                           // the colour of the message of ours the strip shows
long long gBcStripUntil = 0;                           // until then (a message of the game's own ends it: 0x3D5050)
void BcStripTint() {                                   // each pass while it shows (the strip's action may set its colour)
    if (gFrameNo >= gBcStripUntil) return;
    const uint32_t stripAt = PageData(0x3D51C8, 0, 0x2F0B88);
    const uint32_t strip = stripAt ? R32(stripAt) & 0x01FFFFFF : 0;
    if (strip < 0x100000 || strip + 0x50 >= 0x02000000) return;
    const uint16_t state = R16(strip + 0x48);          // 1, 2 shown; 0, 3 not
    if (state != 1 && state != 2) { if (gFrameNo > gBcStripUntil - (long long)(3.5 * FieldRate())) gBcStripUntil = 0; return; }
    if (const uint32_t o = La2Object(R32(strip + 0x40) & 0x01FFFFFF, 0xC40945))
        W32(o + 0x7C, (R32(o + 0x7C) & 0xFF000000u) | (gBcStripColour & 0x00FFFFFFu));
}
bool BcStripShown() {                                  // the round's strip shows a message (ours or the game's)
    const uint32_t stripAt = PageData(0x3D51C8, 0, 0x2F0B88);
    const uint32_t strip = stripAt ? R32(stripAt) & 0x01FFFFFF : 0;
    if (strip < 0x100000 || strip + 0x50 >= 0x02000000) return false;
    const uint16_t state = R16(strip + 0x48);
    return state == 1 || state == 2;
}
// ---- the sounds: the game's own effects, played as its menus play theirs (0x114DA0: heard on this PC only)
const uint32_t kBcSeOurs = 0x1A00E;                    // a base taken by this player's team: CAPTURE's KEROTAN placed (n003a 0x39B990)
const uint32_t kBcSeTheirs = 0x300F;                   // a base taken by the other team, or one of ours lost: the match's menus' refusal (0x3CFCBC)
const uint32_t kBcSeTick = 0x1A00B;                    // the gauge filling toward this player's team: the match's menus' cursor (0x3CA668)
const uint32_t kBcSeStrip = 0x117;                     // the message strip's own effect (0x3D5184), left out for BC's events
long long gBcSeAt = -100000;                           // this PC's last event sound (one at a time)
bool gBcStripQuiet = false;                            // the strip shows a BC event: its own effect left out
void BcSe(const Ctx& c, uint32_t code) {
    ps2::Fn f = ps2::FindFn(V(0x114DA0));
    if (!f) return;
    Ctx x = c;
    x.r[4] = code;
    f(x);
}
void BcSeSampler(const Ctx& c) {                       // tests (SIM_BC_SE_DEMO=code,code..): each played 2 s apart, the round not started
    static const char* e = getenv("SIM_BC_SE_DEMO");
    static size_t at = 0;
    static long long next = 0;
    if (!e || !e[at] || gFrameNo < next) return;
    char* end = nullptr;
    const uint32_t code = (uint32_t)strtoul(e + at, &end, 16);
    at = end && end > e + at ? (size_t)(end - e) + (*end == ',') : strlen(e);
    next = gFrameNo + (long long)(2.0 * FieldRate());
    if (!code) return;
    BcSe(c, code);
    Log("basecap: sound sampler %05x", code);
}
uint32_t BcEventSe(int kind, int team) {               // an event's sound for this player (0: the strip's own)
    const int self = OwnSlot();
    const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
    const int mine = me ? TsneTeamOf(me) : -1;
    if (mine != 0 && mine != 1) return 0;
    if (kind == kBcMsgCaptured) return team == mine ? kBcSeOurs : kBcSeTheirs;
    return kind == kBcMsgNeutral && team != mine ? kBcSeTheirs : 0;   // an enemy base neutralized: the strip's own
}

void BcShowMessage(Ctx& c, int kind, int base, int slot, int team = -1) {
    const uint32_t g = TsneHud();
    const uint32_t stripAt = PageData(0x3D51C8, 0, 0x2F0B88);
    const uint32_t strip = stripAt ? R32(stripAt) & 0x01FFFFFF : 0;
    const uint32_t se = g ? BcEventSe(kind, team) : 0;
    const bool apart = gFrameNo - gBcSeAt >= (long long)(0.5 * FieldRate());   // two events at once: one sound
    if (se && apart) {
        gBcSeAt = gFrameNo;
        BcSe(c, se);
        Log("basecap: sound %05x for BASE %d (%s)", se, base, se == kBcSeOurs ? "taken by this player's team" : "taken or lost to the other team");
    }
    if (!g || strip < 0x100000 || strip + 0x50 >= 0x02000000) return;
    const uint32_t L = R32(g + 0xDC);
    gBcMsgIndex = BcMessageString(c, kind, base);
    if (!gBcMsgIndex) return;
    uint32_t nameAt = 0;
    if (slot >= 0) {
        static uint32_t buf = 0;
        if (!buf) buf = Alloc(64);
        const std::string nm = BcName(slot);
        if (buf) { memset(gMem + buf, 0, 64); memcpy(gMem + buf, nm.c_str(), std::min<size_t>(nm.size(), 63)); nameAt = buf; }
    }
    gBcStripQuiet = se || !apart;
    CallPage(c, 0x3D5050, strip, kBcStrBase + 0x10 - L, nameAt);
    gBcMsgIndex = 0;
    gBcStripQuiet = false;
    if (!se && apart) gBcSeAt = gFrameNo;              // the strip's own effect heard
    gBcStripColour = kBcStripColour[team == 0 ? 0 : team == 1 ? 1 : 2];
    gBcStripUntil = gFrameNo + (long long)(4.0 * FieldRate());
    BcStripTint();
    BcPngAsk("event", 45);
    Log("basecap: message %d about BASE %d (player %d, team %d)", kind, base, slot + 1, team);
}

// ---- the host: the players in each base, the bases' owners
struct BcEvent { int kind, base, slot, team; };
std::vector<BcEvent> gBcEvents;                        // this pass's captures and neutralizations (messages)
std::vector<std::pair<int, int>> gBcCredits;           // this pass's capturers (slot, base): a KEROTAN placed each (BcCredit)
void BcHostTick() {
    BcState& s = gBc;
    float dt = s.lastFrame ? (float)((double)(gFrameNo - s.lastFrame) / FieldRate()) : 0.0f;
    s.lastFrame = gFrameNo;
    if (dt < 0 || dt > 0.25f) dt = 0.0f;
    BcPlayer pl[32];
    const int np = BcPlayers(pl, 32);
    for (int i = 0; i < s.n; ++i) {
        BcBase& b = s.b[i];
        b.in[0] = b.in[1] = 0;
        bool inside[32] = {};
        for (int k = 0; k < np; ++k)
            if (pl[k].alive && BcInside(b, pl[k].x, pl[k].y, pl[k].z)) { ++b.in[pl[k].team]; inside[pl[k].slot] = true; }
        for (int k = 0; k < 32; ++k) {
            if (!inside[k]) b.since[k] = 0;
            else if (!b.since[k]) b.since[k] = gFrameNo;
        }
        b.contested = b.in[0] && b.in[1];
        if (b.contested) continue;
        if (!b.in[0] && !b.in[1]) {                    // nobody: an abandoned step empties slowly
            if (b.prog > 0) { b.prog -= dt / kBcDecayTime; if (b.prog <= 0) { b.prog = 0; b.toward = -1; } }
            continue;
        }
        const int T = b.in[0] ? 0 : 1, count = std::min(b.in[T], 3);
        const float rate = dt / (kBcStepTime[count] * BcStepScale());
        if (b.owner == T) {                            // its own team: an enemy's neutralizing undone
            if (b.toward != T && b.prog > 0) { b.prog -= rate; if (b.prog <= 0) { b.prog = 0; b.toward = -1; } }
            continue;
        }
        if (b.toward != T && b.prog > 0) {             // another team's step undone first
            b.prog -= rate;
            if (b.prog <= 0) { b.prog = 0; b.toward = T; }
            continue;
        }
        b.toward = T;
        b.prog += rate;
        if (b.prog < 1.0f) continue;
        int who = -1; long long first = 0;             // the team's player inside the longest
        for (int k = 0; k < np; ++k)
            if (pl[k].team == T && b.since[pl[k].slot] && (who < 0 || b.since[pl[k].slot] < first)) { who = pl[k].slot; first = b.since[pl[k].slot]; }
        b.capturer = who;
        ++b.events;
        if (b.owner >= 0) {                            // an enemy base neutralized: grey, the next step toward T
            gBcEvents.push_back({kBcMsgNeutral, i + 1, who, T});
            Log("basecap: BASE %d neutralized by player %d (team %d), %d of team %d inside", i + 1, who + 1, T, b.in[T], T);
            b.owner = -1; b.prog = 0; b.toward = T;
        } else {
            gBcEvents.push_back({kBcMsgCaptured, i + 1, who, T});
            Log("basecap: BASE %d captured by player %d (team %d), %d inside; red %d, blue %d", i + 1, who + 1, T, b.in[T],
                BcOwned(0) + (T == 0), BcOwned(1) + (T == 1));
            b.owner = T; b.prog = 0; b.toward = -1;
            for (int k = 0; k < np; ++k)               // every capturer inside is credited
                if (pl[k].team == T && inside[pl[k].slot]) gBcCredits.push_back({pl[k].slot, i + 1});
        }
    }
}

// a capture's points: CAPTURE's KEROTANS PLACED + 1 (3 points on the round's score sheet; the lobby server's
// KerotansPlaced and points), as the host credits a KEROTAN placed (n003a 0x3F1F80: the player's sync object
// 0x178658(slot + 1), its counter at key 0x30 written by 0x178300; the score follows it, 0x3EC010)
void BcCredit(Ctx& c, int slot, int base) {
    const uint32_t obj = CallPage(c, 0x178658, (uint32_t)slot + 1) & 0x01FFFFFF;
    static uint32_t buf = 0;
    if (!buf) buf = Alloc(16);
    if (!BcPs2(obj, 0x60) || !buf) return;
    W32(buf, std::min<uint32_t>(R16(obj + 0x30) + 1u, 0xFFFF));
    CallPage(c, 0x178300, obj, 0x30, 2, buf);
    Log("basecap: player %d credited a KEROTAN placed for BASE %d (KEROTANS PLACED %u, score %u)", slot + 1, base, R16(obj + 0x30), R16(obj + 0x1E));
}
void BcScores(Ctx& c) {                                // the round's result: each player's counters as the host reports them
    for (int slot = 0; slot < 16; ++slot) {
        const uint32_t obj = CallPage(c, 0x178658, (uint32_t)slot + 1) & 0x01FFFFFF;
        if (!BcPs2(obj, 0x60) || !PlayerOfSlot(slot)) continue;
        Log("basecap: player %d at the round's result: KEROTANS PLACED %u, score %u, kills %u", slot + 1, R16(obj + 0x30), R16(obj + 0x1E), R16(obj + 0x14));
    }
}

// ---- the round's result (the host's: CAPTURE's decider, n003a 0x3EAC68, from the supervisor's CAPTURE state 0x3EAB98;
// the round's record [supervisor + 0x3BC]: +0 the time left, +4 the result: 1 a draw, 2 red wins, 3 blue wins): a team
// holding every base wins at once; at the time's end the team holding more bases wins, as many: a draw; a team
// without players loses (as CAPTURE)
bool BcDecide(Ctx& c, uint32_t sup) {
    ps2::Fn host = ps2::FindFn(V(0x173840));
    if (!host || sup < 0x100000 || sup + 0x3C0 >= 0x02000000) return false;
    Ctx t = c;
    host(t);
    if (!(uint32_t)t.r[2]) return false;
    const uint32_t rec = R32(sup + 0x3BC) & 0x01FFFFFF;
    if (rec < 0x100000 || rec + 0x20 >= 0x02000000 || !gBc.n) return false;
    const int red = BcOwned(0), blue = BcOwned(1);
    uint8_t result = 0;
    const char* why = "";
    if (red == gBc.n) { result = 2; why = "every base red"; }
    else if (blue == gBc.n) { result = 3; why = "every base blue"; }
    else if ((int32_t)R32(rec) <= 0) {
        W32(sup + 0xE0, R32(sup + 0xE0) | 0x10000);   // the time up (CAPTURE's decider sets it)
        result = red > blue ? 2 : blue > red ? 3 : 1;
        why = "the time's end";
    } else {
        ps2::Fn count = ps2::FindFn(V3(0x3EF968));     // the players of a team still in (0 none)
        if (count) {
            Ctx q = c; q.r[4] = 0; count(q);
            if (!(uint32_t)q.r[2]) { result = 3; why = "no red player"; }
            else { q = c; q.r[4] = 1; count(q); if (!(uint32_t)q.r[2]) { result = 2; why = "no blue player"; } }
        }
    }
    if (!result) return false;
    W8(rec + 4, result);
    gBc.decided = true;
    Log("basecap: the round's result: %s (%s; bases red %d, blue %d of %d, time %d)", result == 2 ? "RED TEAM WINS" : result == 3 ? "BLUE TEAM WINS" : "A DRAW",
        why, red, blue, gBc.n, (int32_t)R32(rec));
    return true;
}

// ---- the entry's end (the supervisor's 0x3E9EE8 after each round, every PC, from sync object 0): K team victories
// (+0x2D, CAPTURE's lobby key 0x4D) or the rounds' limit (+0x2C: DEATHMATCH 1, SNEAKING key 0x4C, CAPTURE 0; the
// round's number +0x40). A draw counts no victory (TEAM DEATHMATCH's as BC's; CAPTURE's own decider never draws), so
// BC's rounds limit is 2K - 1: an entry with no draw ends at K victories as before, draws count as rounds played
uint32_t BcEntryRoundsFor(uint32_t k) { return k ? std::min<uint32_t>(2 * k - 1, 255) : 0; }
void BcEntryRounds(Ctx& c) {                           // the host, each pass of a BC round: +0x2C set (0x178300: sent to every PC)
    static long long at = -1000;
    if (gFrameNo >= at && gFrameNo - at < 30) return;   // twice a second
    at = gFrameNo;
    const uint32_t cfg = CallPage(c, 0x178658, 0) & 0x01FFFFFF;
    if (!BcPs2(cfg, 0x50)) return;
    const uint32_t k = R8(cfg + 0x2D), want = BcEntryRoundsFor(k);
    if (!want || R8(cfg + 0x2C) == want) return;
    static uint32_t buf = 0;
    if (!buf) buf = Alloc(16);
    if (!buf) return;
    const uint32_t was = R8(cfg + 0x2C);
    W32(buf, want);
    CallPage(c, 0x178300, cfg, 0x2C, 1, buf);
    if (Once(((uint64_t)185 << 40) | gBc.round))
        Log("basecap: the entry ends at %u team victories or after %u rounds (its limit %u -> %u; round %u of the entry)", k, want, was, R8(cfg + 0x2C), R8(cfg + 0x40) + 1);
}

// ---- the respawns: CAPTURE's respawn points (red 2B16C2 .. 2B16C9, blue 2B1AC2 .. 2B1AC9, looked up by 0x161200 as
// the player spawns again) made a base of his team's: one it holds, not contested, the nearest to where he was last
// alive; a few steps from its centre by his slot (no two in one place)
int BcRespawnTeam(uint32_t code) {
    const uint32_t c = code & 0xFFFFFF;
    if (c >= 0x2B16C2 && c <= 0x2B16C9) return 0;
    if (c >= 0x2B1AC2 && c <= 0x2B1AC9) return 1;
    return -1;
}
// CAPTURE's starts (red BCD39F .., blue BCD79F ..) and respawns (2B16C2 .., 2B1AC2 ..) are TEAM DEATHMATCH's (258BA6 ..,
// 258FA6 ..; 32155F .., 32195F ..): the teams on the two sides of the whole map, where CAPTURE's lie close together on
// some stages (Svyatogornyj East's 20 m apart, Graniny Gorki Lab's 14 m)
uint32_t BcTdmCode(uint32_t code) {
    const uint32_t c = code & 0xFFFFFF;
    static const uint32_t from[4] = {0xBCD39F, 0xBCD79F, 0x2B16C2, 0x2B1AC2}, to[4] = {0x258BA6, 0x258FA6, 0x32155F, 0x32195F};
    for (int k = 0; k < 4; ++k) if (c >= from[k] && c < from[k] + 8) return to[k] + (c - from[k]);
    return 0;
}
int BcSpawnBase(int team) {
    int best = -1; float bd = 0;
    for (int i = 0; i < gBc.n; ++i) {
        const BcBase& b = gBc.b[i];
        if (b.owner != team || b.contested || (b.toward >= 0 && b.toward != team && b.prog > 0)) continue;
        const float dx = b.x - gBc.death[0], dz = b.z - gBc.death[2];
        const float d = gBc.haveDeath ? dx * dx + dz * dz : (float)i;
        if (best < 0 || d < bd) { best = i; bd = d; }
    }
    return best;
}
// a TEAM DEATHMATCH respawn (a team holding no base) in a base's zone or a step beyond it (FAIR's 3.8 m) is replaced by
// one of the team's respawns clear of every base, the asked one's number among them (so the replaced ones spread);
// none clear: the asked one kept
const float kBcRespawnStep = 1500.0f;
int BcByBase(float x, float y, float z) {             // the base whose zone and a step hold the point, -1 none
    const float r = kBcRadius + kBcRespawnStep;
    for (int i = 0; i < gBc.n; ++i) {
        const BcBase& b = gBc.b[i];
        const float dx = x - b.x, dz = z - b.z, dy = y - b.y;
        if (dx * dx + dz * dz < r * r && dy >= -kBcBelow && dy <= kBcAbove) return i;
    }
    return -1;
}
int BcRespawnForce() {                                 // tests (SIM_BC_RESPAWN_FORCE=0..7, the harness only): the respawn code asked
    static const int k = getenv("SIM_BC_RESPAWN_FORCE") ? atoi(getenv("SIM_BC_RESPAWN_FORCE")) & 7 : -1;
    return gBcHarness ? k : -1;
}
uint32_t BcRespawnClear(const Ctx& c, ps2::Fn f, Ctx& t, uint32_t h, uint32_t tdm, int team) {   // the code taken
    const uint32_t out = (uint32_t)c.r[4];
    if (!BcPs2(out, 12)) return tdm;
    const int by = BcByBase(F32(out), F32(out + 4), F32(out + 8));
    if (by < 0) return tdm;
    const uint32_t first = team ? 0x32195F : 0x32155F, j = (tdm - first) & 7;
    uint32_t clear[8];
    uint32_t n = 0;
    for (uint32_t k = 0; k < 8; ++k) {                 // the team's respawns clear of every base, in order
        Ctx u = c;
        u.r[7] = (h & 0xFF000000u) | (first + k);
        f(u);
        if ((uint32_t)u.r[2] && BcByBase(F32(out), F32(out + 4), F32(out + 8)) < 0) clear[n++] = first + k;
    }
    const uint32_t code = n ? clear[j % n] : tdm;
    Ctx u = c;
    u.r[7] = (h & 0xFF000000u) | code;
    f(u);
    t = u;
    if (n) Log("basecap: the respawn point %06x lies by BASE %d: %06x taken (team %d, %u of the team's clear)", tdm, by + 1, code, team, n);
    else Log("basecap: every respawn point of team %d lies by a base: %06x kept", team, tdm);
    return code;
}

// ---- the network (between PCs: the host's states to the joiners, vnet type 6, ps2world.cpp): the round, then per base
// owner, toward, progress (0..255), flags (1 contested), the events' count, the last capturer, the players in
uint8_t gBcInbox[64];                                  // the last state received (filled under vnet's lock)
size_t gBcInboxLen = 0;
std::mutex gBcInboxMu;
std::vector<uint8_t> BcStateBytes() {
    std::vector<uint8_t> d;
    const uint32_t r = gBc.round;
    d.push_back((uint8_t)r); d.push_back((uint8_t)(r >> 8)); d.push_back((uint8_t)(r >> 16)); d.push_back((uint8_t)(r >> 24));
    d.push_back((uint8_t)gBc.n);
    for (int i = 0; i < gBc.n; ++i) {
        const BcBase& b = gBc.b[i];
        d.push_back((uint8_t)(int8_t)b.owner); d.push_back((uint8_t)(int8_t)b.toward);
        d.push_back((uint8_t)std::clamp((int)lrintf(b.prog * 255.0f), 0, 255));
        d.push_back((uint8_t)(b.contested ? 1 : 0)); d.push_back(b.events); d.push_back((uint8_t)(int8_t)b.capturer);
        d.push_back((uint8_t)std::min(b.in[0], 255)); d.push_back((uint8_t)std::min(b.in[1], 255));
    }
    return d;
}
bool BcLink() { return BcRound() && gBc.n > 0; }       // the PCs' link wanted (vnet hellos)
void BcApply(Ctx& c) {                                 // a joiner: the host's last state
    uint8_t d[64]; size_t n;
    { std::lock_guard<std::mutex> l(gBcInboxMu); n = gBcInboxLen; memcpy(d, gBcInbox, sizeof d); gBcInboxLen = 0; }
    if (n < 5 || d[4] != gBc.n || n < 5 + 8 * (size_t)gBc.n) return;
    const uint32_t hr = d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
    if (hr == gBcPrevHostRound && PsMs() - gBcPrevAt < 3000) return;   // the round before's (the host not in this round yet)
    if (gBc.hostRound && hr != gBc.hostRound) gBc.heardMs = 0;   // the host's round changed: its first state again
    gBc.hostRound = hr;
    const bool first = !gBc.heardMs;                   // the first state heard this round: no messages for what came before
    gBc.heardMs = PsMs();
    for (int i = 0; i < gBc.n; ++i) {
        BcBase& b = gBc.b[i];
        const uint8_t* q = d + 5 + 8 * i;
        const int owner = (int8_t)q[0];
        const uint8_t ev = q[4];
        if (ev != b.events && !first) {                // a capture or neutralization the host made
            const int who = (int8_t)q[5];
            const bool neutral = owner < 0;
            BcShowMessage(c, neutral ? kBcMsgNeutral : kBcMsgCaptured, i + 1, who, neutral ? (int8_t)q[1] : owner);
            Log("basecap: from the host: BASE %d %s by player %d", i + 1, neutral ? "neutralized" : owner ? "captured (blue)" : "captured (red)", who + 1);
        }
        b.events = ev;
        b.owner = owner; b.toward = (int8_t)q[1]; b.prog = q[2] / 255.0f; b.contested = q[3] & 1; b.capturer = (int8_t)q[5];
        b.in[0] = q[6]; b.in[1] = q[7];
    }
}

// ---- each pass of the actor lists (a BC round on a map's page)
void BcVisuals(Ctx& c);
void BcHud(Ctx& c);
void BcWatch();
struct BcPicNode { uint32_t o, rec, uv0, uv1, xy, size, colour; bool move; };   // a node's object, its wanted words (move: its place only)
struct BcPic {                                         // the GAME TYPE page's picture (BcPicture)
    uint32_t bank = 0, rec = 0, cellX = 0, cellY = 0, lay = 0, round = 0, hud = 0;
    uint32_t iconRec = 0;                              // the icon's own record (BcPicIconRecord)
    bool icon = false, complete = false, tried = false, drawn = false, iconOk = false;
    int iconTries = 0;                                 // the icon's GS checks since the page was drawn
    long long hooked = -1000000;                       // the frame of its page's last set-up
    std::vector<BcPicNode> nodes;
};
BcPic gBcPic;
void BcPicture(Ctx& c, uint32_t lay);
void BcTick(Ctx& c) {
    ++gBcPass;
    BcPngTick();
    if (BcRound() && gBcPic.lay && gBcPic.hud && TsneHud() == gBcPic.hud && gFrameNo - gBcPic.hooked < 30 * (long long)FieldRate())
        BcPicture(c, gBcPic.lay);                      // kept while its page may be up (30 s after its set-up, its HUD alive)
    if (gBc.mo && !BcObjectsAlive()) {                 // the round's objects gone (its end, a stage change)
        Log("basecap: the mission objects %08x are gone: the bases left", gBc.mo);
        gBc.mo = 0; gBc.n = 0;
    }
    if (!BcRound() || !TsneMapPage() || !gBc.mo || !gBc.n) return;
    BcState& s = gBc;
    const int self = OwnSlot();
    const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
    if (me && (int16_t)R16(me + 0xB64) > 0) { s.death[0] = F32(me + 0x110); s.death[1] = F32(me + 0x114); s.death[2] = F32(me + 0x118); s.haveDeath = true; }
    if (IsSessionHost()) {
        const uint32_t phase = CallPage(c, 0x3ECBA8, 0);
        if (phase != gBcPhase) { Log("basecap: the round's phase %u (%s)", phase, phase >= 2 && phase < 4 ? "started" : "not started"); gBcPhase = phase; }
        BcEntryRounds(c);
        if (BcRunning() && BcStarted()) BcHostTick();
        else {
            s.lastFrame = 0;
            if (BcRunning() && !BcStarted()) {         // the round not started: no progress (logged once a base)
                BcSeSampler(c);
                BcPlayer pl[32];
                const int np = BcPlayers(pl, 32);
                for (int i = 0; i < s.n; ++i)
                    for (int k = 0; k < np; ++k)
                        if (pl[k].alive && BcInside(s.b[i], pl[k].x, pl[k].y, pl[k].z) && Once(((uint64_t)181 << 40) | (uint64_t)i << 32 | s.round)) {
                            Log("basecap: player %d in BASE %d before the round's start (phase %u): no progress", pl[k].slot + 1, i + 1, phase);
                            BcPngAsk("wait", 240, 1u << 26);   // tests: in a base, the time frozen
                        }
            }
        }
        for (const BcEvent& e : gBcEvents) BcShowMessage(c, e.kind, e.base, e.slot, e.team);
        gBcEvents.clear();
        for (const auto& k : gBcCredits) BcCredit(c, k.first, k.second);
        gBcCredits.clear();
    } else BcApply(c);
    BcVisuals(c);
    BcStripTint();
    BcHud(c);
    BcWatch();
    if (const uint32_t r = BcRecord()) if (R8(r + 4)) {   // tests: the round's result (SIM_BC_PNG_FIRST: the map's pictures end)
        static uint32_t seen = 0; static long long at = 0; static bool told = false;
        if (seen != s.round) { seen = s.round; at = gFrameNo; told = false; }
        if (IsSessionHost() && !told && gFrameNo - at >= 60) { told = true; BcScores(c); }
        BcPngAsk("result", 150, 1u << 20);
        BcPngAsk("score", 240, 1u << 25);              // tests: the round's score sheet
        if (BcPngFirst()) gBcPngOpen = false;
    }
}

// ---- a neutral base's grey models (CAPTURE's goals are red or blue only): copies of the red goal's three models
// (139EEA the sign, 139EEB, BF0888 the floor) whose team-coloured textures are grey copies, served by 0x122F18 under
// codes of ours. A model's resource: [size, 0x0D000000 | code, ..] then its data ('MDB ': +0xC the meshes, +0x18
// their table, 0x40 each: +8 the submeshes, +0xC their list, 0x20 each: +0xC the texture's record). A texture's
// record is its bank's entry (0xA0: +0x10 its code, +0x14 the bank, +0x1C the bank-relative CLUT slot << 16, +0x40
// TEX2: the slot's CLUT loaded as PSMT8, +0x50 TEX0: a PSMT4 texture's 16 colours at CSA in it). The banks are sent
// to the GS each frame from their memory (header +0x1C: the CLUT pages, 64 x 32 PSMCT32 each), so a CLUT written in a
// free part of a bank's CLUT pages is the GS's from the next frame on
const uint32_t kBcGreyCodes[3] = {0xBA5E31, 0xBA5E32, 0xBA5E33};   // the grey sign, its second part, its floor
const uint32_t kBcGreyLevel = 0xA0;                    // the grey (the red is 147 0 0, the blue 0 0 210)
struct BcGreyTex { uint32_t from, to; };
struct BcGrey {
    uint32_t round = 0;                                // the round they were made for (gBc.round)
    bool tried = false, ready = false;
    uint32_t mem[3] = {}, def[3] = {};                 // the models' copies (their resource, their data)
    std::vector<BcGreyTex> tex;                        // the grey records made (each from a red one)
    std::vector<uint32_t> recs;
};
BcGrey gBcGrey;
uint32_t BcBankOf(uint32_t rec) {                      // the bank (its data: the TRI header) holding a texture's record
    const uint32_t code = R32(rec + 0x14) & 0xFFFFFF;
    for (uint32_t k = 0; k < 256 && rec >= 0x100040 + 0xA0 * k; ++k) {
        const uint32_t b = rec - 0x20 - 0xA0 * k;
        if (R32(b - 0xC) == (0x03000000u | code) && R32(b + 0x10) > k && R32(b + 0x10) < 1024) return b;
    }
    return 0;
}
uint32_t BcClutWord(uint32_t bank, uint32_t slot, uint32_t i) {   // colour i (0..255) of the CLUT slot (4 blocks, CSM1)
    static const uint8_t bt1[8] = {0, 1, 4, 5, 16, 17, 20, 21}, bt2[4] = {0, 2, 8, 10};   // PSMCT32 blocks in a page
    const uint32_t bb = slot % 32;
    int bx = -1, by = -1;
    for (int y = 0; y < 4 && bx < 0; ++y) for (int x = 0; x < 8; ++x) if (bt1[x] + bt2[y] == bb) { bx = x; by = y; break; }
    if (bx < 0 || (slot & 3)) return 0;
    const uint32_t j = i / 16, e = i % 16;
    const uint32_t px = (uint32_t)bx * 8 + (j & 1) * 8 + e % 8, py = (uint32_t)by * 8 + (j >> 1) * 2 + e / 8;
    return bank + R32(bank + 0x1C) + 4 * ((slot / 32) * 2048 + py * 64 + px);
}
bool BcTeamColour(uint32_t c) {                        // a CLUT colour (A B G R) of a team: red or blue
    const int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    return (r >= 0x40 && r > 2 * g + 0x20 && r > 2 * b + 0x20) || (b >= 0x40 && b > 2 * r + 0x20 && b > 2 * g + 0x20);
}
uint32_t BcGreyRecord(uint32_t rec) {                  // a texture's record with its team colours grey (rec: none to change)
    for (const BcGreyTex& t : gBcGrey.tex) if (t.from == rec) return t.to;
    const uint32_t bank = BcBankOf(rec);
    const uint64_t tex0 = R64(rec + 0x50), tex2 = R64(rec + 0x40);
    const uint32_t cbp = (uint32_t)(tex0 >> 37) & 0x3FFF, csa = (uint32_t)(tex0 >> 56) & 0x1F, rel = (R32(rec + 0x1C) >> 16) & 0x3FFF;
    if (!bank || ((tex0 >> 20) & 0x3F) != 0x14 || ((tex2 >> 20) & 0x3F) != 0x13 || ((tex2 >> 37) & 0x3FFF) != cbp || rel > cbp || (rel & 3) || csa > 15) {
        Log("basecap: the texture %06x's CLUT not as known (bank %08x, TEX0 %016llx, TEX2 %016llx): kept", R32(rec + 0x10) & 0xFFFFFF, bank,
            (unsigned long long)tex0, (unsigned long long)tex2);
        return rec;
    }
    uint32_t grey[16];
    bool team = false;
    for (uint32_t e = 0; e < 16; ++e) {
        const uint32_t w = R32(BcClutWord(bank, rel, csa * 16 + e));
        team |= (w >> 24) && BcTeamColour(w);
        grey[e] = (w & 0xFF000000u) | kBcGreyLevel * 0x010101u;
    }
    if (!team) { gBcGrey.tex.push_back({rec, rec}); return rec; }
    const uint32_t blocks = R32(bank + 0xC), n = R32(bank + 0x10);
    auto zero = [&](uint32_t slot, uint32_t j) {       // 16 colours of a slot all 0, or the grey ones (an earlier round's)
        bool z = true, same = true;
        for (uint32_t e = 0; e < 16; ++e) { const uint32_t w = R32(BcClutWord(bank, slot, j * 16 + e)); z &= !w; same &= w == grey[e]; }
        return z || same;
    };
    uint64_t t0 = tex0, t2 = tex2;
    int at = -1;
    for (uint32_t j = 8; j < 16 && at < 0; ++j) if (j != csa && zero(rel, j)) at = (int)j;   // a free group in its slot
    if (at >= 0) {
        for (uint32_t e = 0; e < 16; ++e) W32(BcClutWord(bank, rel, (uint32_t)at * 16 + e), grey[e]);
        t0 = (t0 & ~(0x1Full << 56)) | ((uint64_t)at << 56);
    } else {                                           // else a free slot of the bank's CLUT pages
        for (uint32_t s = 0; s + 4 <= blocks && at < 0; s += 4) {
            bool free = true;
            for (uint32_t k = 0; k < n && free; ++k) { const uint32_t r = (R32(bank + 0x20 + 0xA0 * k + 0x1C) >> 16) & 0x3FFF; free = r + 4 <= s || r >= s + 4; }
            for (uint32_t i = 16; i < 256 && free; ++i) free = !R32(BcClutWord(bank, s, i));
            free = free && zero(s, 0);
            if (free) at = (int)s;
        }
        if (at < 0) { Log("basecap: no free CLUT for the texture %06x's grey copy: kept", R32(rec + 0x10) & 0xFFFFFF); return rec; }
        for (uint32_t e = 0; e < 16; ++e) W32(BcClutWord(bank, (uint32_t)at, e), grey[e]);
        const uint64_t nc = (uint64_t)(cbp - rel + (uint32_t)at);
        t0 = (t0 & ~((0x3FFFull << 37) | (0x1Full << 56))) | (nc << 37);
        t2 = (t2 & ~((0x3FFFull << 37) | (0x1Full << 56))) | (nc << 37);
    }
    const uint32_t g = Alloc(0xA0);
    if (!g) return rec;
    memcpy(gMem + g, gMem + rec, 0xA0);
    W32(g + 0x10, (R32(rec + 0x10) & 0xFF000000u) | (0xBA5E40u + (uint32_t)gBcGrey.tex.size()));
    W64(g + 0x40, t2); W64(g + 0x50, t0);
    gBcGrey.tex.push_back({rec, g});
    gBcGrey.recs.push_back(g);
    Log("basecap: the texture %06x's grey copy %08x (%s %d of the bank %06x)", R32(rec + 0x10) & 0xFFFFFF, g,
        t2 == tex2 ? "the colours at CSA" : "a CLUT slot of its own at block", at, R32(bank - 0xC) & 0xFFFFFF);
    return g;
}
uint32_t BcGreyModel(Ctx& c, uint32_t red, uint32_t code) {   // a copy of a model with its team colours grey (0: none)
    const uint32_t def = CallPage(c, 0x122F18, 0x0D000000u | red) & 0x01FFFFFF;
    if (def < 0x100010 || def >= 0x01F00000 || R32(def) != 0x2042444Du || R32(def - 0xC) != (0x0D000000u | red)) return 0;
    const uint32_t size = R32(def - 0x10), data = size - 0x10;
    if (size < 0x80 || size > 0x40000) return 0;
    const uint32_t mem = Alloc(size);
    if (!mem) return 0;
    const uint32_t copy = mem + 0x10;
    memcpy(gMem + mem, gMem + def - 0x10, size);
    W32(mem + 4, 0x0D000000u | code);
    int moved = 0, greyed = 0;
    for (uint32_t o = 0; o + 4 <= data; o += 4) {      // its pointers into itself (16-byte aligned): to the copy
        const uint32_t w = R32(def + o);
        if (w >= def && w < def + data && !((w - def) & 15)) { W32(copy + o, copy + (w - def)); ++moved; }
    }
    const uint32_t meshes = R32(def + 0xC), table = R32(copy + 0x18);
    for (uint32_t m = 0; m < meshes && m < 64 && table >= copy && table < copy + data; ++m) {
        const uint32_t me = table + 0x40 * m, subs = R32(me + 8), list = R32(me + 0xC);
        for (uint32_t s = 0; s < subs && s < 256 && list >= copy && list + 0x20 * s + 0x20 <= copy + data; ++s) {
            const uint32_t at = list + 0x20 * s + 0xC, rec = R32(at);
            if (rec < 0x100040 || rec >= 0x01FFFF00) continue;
            const uint32_t g = BcGreyRecord(rec);
            if (g != rec) { W32(at, g); ++greyed; }
        }
    }
    gBcGrey.mem[code - kBcGreyCodes[0]] = mem;
    Log("basecap: the grey model %06x made from %06x (%u bytes, %d pointers moved, %d textures grey)", code, red, data, moved, greyed);
    return copy;
}
void BcGreyMake(Ctx& c) {                              // once a page load, at the first grey base
    BcGrey& g = gBcGrey;
    if (g.round != gBc.round) {                        // a new round: its page's models and banks
        for (uint32_t m : g.mem) if (m) Free(m);
        for (uint32_t r : g.recs) Free(r);
        g = BcGrey(); g.round = gBc.round;
    }
    if (g.tried) return;
    g.tried = true;
    static const uint32_t red[3] = {0x139EEA, 0x139EEB, 0xBF0888};
    for (int k = 0; k < 3; ++k) g.def[k] = BcGreyModel(c, red[k], kBcGreyCodes[k]);
    g.ready = g.def[0] && g.def[1] && g.def[2];
    if (!g.ready) Log("basecap: the grey models not made (%08x %08x %08x): the neutral bases shown without their signs", g.def[0], g.def[1], g.def[2]);
}

// ---- the colours shown (each pass): a base's models and radar mark follow its owner. A goal record (0xC0: +0 +4 +8
// +0xC its DG objects, +0x90 its place, +0xAC its radar mark, +0xB0 the mark's handle) gets the models of its colour
// again (n003a 0x39E6F8(goal, model, model, model): its place raised 2150 for the floating sign, the floor's laid on the
// ground below), the old ones taken out of DG's queue and freed as the class's die does (0x138608, 0x1384B8). The radar
// marks' table ([0x2E5228] + 0x70, 0x1C each: +0xC the flags): red 0x8000, blue 0x10000, grey 0x40000 (the flags the
// init gives side A's first goal, side B's first goal, the others)
const uint32_t kBcModels[3][3] = {{0x139EEA, 0x139EEB, 0xBF0888}, {0x024241, 0x024242, 0x9C4F36}, {kBcGreyCodes[0], kBcGreyCodes[1], kBcGreyCodes[2]}};
const uint32_t kBcMarkBits = 0x2000 | 0x4000 | 0x8000 | 0x10000 | 0x40000;
uint32_t BcGoal(int i) { return i < 3 ? gBc.mo + 0x430 + 0xC0 * (uint32_t)i : gBc.mo + 0x670 + 0xC0 * (uint32_t)(i - 3); }
// The floor (+8) and its shadow (+0xC) are laid by 0x39E6F8 on what a ray down from the raised place meets first: at a
// capture the capturers stand there, so the floor came out diagonal or vertical (Snake 23:56); both keep the matrices
// (DG object + 0x10, 4 rows) the class's init gave them
void BcRecolor(Ctx& c, int i, int owner) {
    const uint32_t g = BcGoal(i);
    float keep[2][16]; bool have[2] = {false, false};
    for (int k = 0; k < 2; ++k) {
        const uint32_t o = R32(g + 8 + 4 * (uint32_t)k);
        if (o >= 0x100000 && o + 0x60 < 0x02000000) { memcpy(keep[k], gMem + o + 0x10, 64); have[k] = true; }
    }
    for (uint32_t off : {0u, 4u, 8u, 0xCu}) {
        const uint32_t o = R32(g + off);
        if (o >= 0x100000 && o < 0x02000000) { CallPage(c, 0x138608, o); CallPage(c, 0x1384B8, o); }
        W32(g + off, 0);
    }
    BcWF(g + 0x94, F32(g + 0x94) - 2150.0f);           // the place as the options gave it (0x39E6F8 raises it again)
    const int k = owner < 0 ? 2 : owner;
    if (owner < 0) BcGreyMake(c);
    if (owner < 0 && !gBcGrey.ready) { BcWF(g + 0x94, F32(g + 0x94) + 2150.0f); return; }
    CallPage(c, 0x39E6F8, g, kBcModels[k][0], kBcModels[k][1], kBcModels[k][2]);
    float made[3] = {0, 0, 0};
    for (int m = 0; m < 2; ++m) {
        const uint32_t o = R32(g + 8 + 4 * (uint32_t)m);
        if (!have[m] || o < 0x100000 || o + 0x60 >= 0x02000000) continue;
        if (!m) memcpy(made, gMem + o + 0x20, 12);     // the floor's second row as made (its up: 0 1 0 when flat)
        memcpy(gMem + o + 0x10, keep[m], 64);
    }
    Log("basecap: BASE %d shown %s (its objects %08x %08x %08x %08x; the floor's up %.2f %.2f %.2f as made, %.2f %.2f %.2f kept)", i + 1,
        owner < 0 ? "grey" : owner ? "blue" : "red", R32(g), R32(g + 4), R32(g + 8), R32(g + 0xC), made[0], made[1], made[2],
        have[0] ? keep[0][4] : 0.0f, have[0] ? keep[0][5] : 0.0f, have[0] ? keep[0][6] : 0.0f);
}
// every base's mark the radar's and the 2D map's fifth kind (flags 0x40000, the marks of the goals after the first of
// a side): each of the five drawn by a node group of its own ([radar + 0x70 + 4k], its sprite [radar + 0x84 + 4k]; the
// 2D map's [map + 0xD4 + 4k], [map + 0xE8 + 4k]), in the order of the marks' table; the sprite given the goal's icon
// (its texture's rectangle +0x40 / +0x44, the goals' 0x3080108 / 0x3F801F8) and its base's colour (+0x34 R G B A:
// the red goal's 0x801D2850, the blue's 0x8066382D, the mark's own grey 0x80465050)
void BcMark(int i) {
    const uint32_t at = PageData(0x399F90, 0, 0x2E5228);
    const uint32_t t = at ? R32(at) & 0x01FFFFFF : 0;
    const uint32_t idx = R32(BcGoal(i) + 0xAC);
    if (t < 0x100000 || idx >= 0x60) return;
    const uint32_t e = t + 0x70 + 0x1C * idx;
    const uint32_t f = R32(e + 0xC), want = (f & ~kBcMarkBits) | 0x40000;
    if (f != want) W32(e + 0xC, want);
}
int BcMarkRank(int i) {                                // the base's rank among the marks of the fifth kind
    const uint32_t idx = R32(BcGoal(i) + 0xAC);
    int r = 0;
    for (int k = 0; k < gBc.n; ++k) if (k != i && R32(BcGoal(k) + 0xAC) < idx) ++r;
    return r;
}
uint32_t BcMarkColour(const BcBase& b, bool blinkOff) {
    if (blinkOff || b.owner < 0) return 0x80465050u;
    return b.owner ? 0x8066382Du : 0x801D2850u;
}
struct BcMarkAt { bool shown = false; float x = 0, y = 0; };
BcMarkAt gBcRadarAt[kBcMax], gBcMapAt[kBcMax];         // where the marks were placed last (1/16 pixel from the element's origin)
uint32_t gBcRadarObj = 0, gBcMapObj = 0;               // the radar's and the 2D map's objects (their hooks' a0; tests)
long long gBcMapFrame = -100;                          // the 2D map's marks placed last (the START menu shown)
bool BcMapShown() { return gBcPass - gBcMapPass <= 2; }   // drawn in this tick's passes (two a tick)
bool BcRadarShown() { return !BcMapShown() && gBcPass - gBcRadarPass <= 2; }
void BcMarkNodes(uint32_t groups, uint32_t sprites, BcMarkAt* at) {
    const bool blink = (gFrameNo / 15) & 1;            // a contested base's mark blinks grey
    for (int i = 0; i < gBc.n; ++i) {
        const int k = BcMarkRank(i);
        const uint32_t g = R32(groups + 4 * (uint32_t)k) & 0x01FFFFFF, sp = R32(sprites + 4 * (uint32_t)k) & 0x01FFFFFF;
        if (g < 0x100000 || g + 0x48 >= 0x02000000 || sp < 0x100000 || sp + 0x48 >= 0x02000000) continue;
        W32(sp + 0x34, BcMarkColour(gBc.b[i], gBc.b[i].contested && blink));
        W32(sp + 0x40, 0x03080108); W32(sp + 0x44, 0x03F801F8);
        at[i].shown = (R32(g + 0x10) & 0x400000) != 0;  // the node shown (the radar's and the map's 0x400000)
        at[i].x = (float)(int16_t)R16(g + 0x3C) / 16.0f; at[i].y = (float)(int16_t)R16(g + 0x3E) / 16.0f;
    }
}
void BcVisuals(Ctx& c) {
    for (int i = 0; i < gBc.n; ++i) {
        BcBase& b = gBc.b[i];
        if (b.shownOwner != b.owner) { b.shownOwner = b.owner; BcRecolor(c, i, b.owner); }
        BcMark(i);
    }
}
// ---- the HUD (each pass of the actor lists, while the round runs): through the game's font system (the font's
// context 0x27F340, as the labels over the items draw: 0x1079D0 the blend, 0x107918 the place (slot, x, y, 2:
// centred), 0x107850 the colour (slot, r, g, b, a), 0x107E18 the text (slot, format, argument)), each text placed
// where the HUD's elements go in a wide picture (the third of the 4:3 screen it sits in: its distance to that edge
// or the centre times the 2D elements' width) and given to the GS side as a label, its glyphs narrowed about it
struct BcRgba { uint8_t r, g, b, a; };
const BcRgba kBcRed = {0xE0, 0x30, 0x20, 0x80}, kBcBlue = {0x40, 0x70, 0xFF, 0x80}, kBcGrey = {0xB0, 0xB0, 0xB0, 0x80},
             kBcWhite = {0xE8, 0xE8, 0xE8, 0x80}, kBcYellow = {0xF0, 0xD0, 0x40, 0x80};
float BcHudX(float x, bool page = false) {             // a 4:3 HUD place in the picture (its third's edge kept; a page's
    const float k = (P.hudX > 0.0f ? P.hudX : 1.0f) * (page ? 1.0f : ps2::GsHudScale());   // about the centre)
    if (k == 1.0f) return x;
    const float pivot = page ? 256.0f : x < 512.0f / 3 ? 0.0f : x > 1024.0f / 3 ? 512.0f : 256.0f;
    return pivot + (x - pivot) * k;
}
float BcHudY(float y, bool page = false) {
    const float s = page ? 1.0f : ps2::GsHudScale();
    if (s == 1.0f) return y;
    const float pivot = y < 448.0f / 3 ? 0.0f : y > 896.0f / 3 ? 448.0f : 224.0f;
    return pivot + (y - pivot) * s;
}
uint32_t gBcText = 0;                                  // 128 bytes: the text drawn
int BcGsChars(size_t n) { return std::max(1, (int)ceilf((5.5f * (float)n - 16.0f) / 14.0f)); }   // a GS label's size for n of ours
float BcGsReach(size_t n) { return 14.0f * (float)BcGsChars(n) + 24.0f; }
enum BcPlace { kBcHudPlace = 0, kBcPagePlace = 1, kBcWorldPlace = 2, kBcTimerPlace = 3, kBcRadarPlace = 4 };   // a HUD element's, a
// page's, a projected point's, beside the timer, by the radar's marks (these two at their 4:3 places, narrowed by the GS
// about the timer's centre or the right edge as the timer's and the radar's elements are)
void BcText(Ctx& c, float x, float y, const BcRgba& col, const std::string& t, int place = kBcHudPlace) {
    if (!gBcText) gBcText = Alloc(128);
    if (!gBcText || t.empty() || !BcDrawPass()) return;
    const bool page = place == kBcPagePlace, world = place == kBcWorldPlace, timer = place == kBcTimerPlace, radar = place == kBcRadarPlace;
    const float ly = world ? y : BcHudY(y, page);
    const int px = (int)lrintf(world || timer || radar ? x : BcHudX(x, page)), py = (int)lrintf(gPal ? ly * (512.0f / 448.0f) : ly);   // PAL: 512 lines
    memset(gMem + gBcText, 0, 128);
    memcpy(gMem + gBcText, t.c_str(), std::min<size_t>(t.size(), 120));
    static uint32_t fmt = 0;
    if (!fmt && (fmt = Alloc(16))) memcpy(gMem + fmt, "%s", 3);
    CallPage(c, 0x1079D0, 0x44);
    CallPage(c, 0x107918, 0, (uint32_t)px, (uint32_t)py, 2);
    { Ctx w = c; w.r[8] = col.a; w.r[4] = 0; w.r[5] = col.r; w.r[6] = col.g; w.r[7] = col.b; if (ps2::Fn f = ps2::FindFn(V(0x107850))) f(w); }   // t0: alpha
    CallPage(c, 0x107E18, 0, fmt, gBcText);
    // the GS narrows a label's glyphs about its place (gs.cpp: rc2's every textured sprite within 14 a character + 24 of
    // it in its row, SIXTY's the font's own glyphs, as many as its characters): reaches fitted to our glyphs (about 10 px
    // each), a long text two or three labels; the counts' at the timer's centre, the radar numbers' at the right edge
    // (the pivots of the timer's and the radar's elements: rc2's labels at 213 / 299 took the timer's digits)
    if (timer) ps2::GsLabelAt(256, py, 2);
    else if (radar) ps2::GsLabelAt(512, py, 8);
    else {
        const int n = (int)t.size(), ch = std::max(BcGsChars(t.size()), (n + 2) / 3);
        for (int k = 0; k * ch < n; ++k) ps2::GsLabelAt(px, py, ch);
    }
}
BcRgba BcTeamColour(int team) { return team == 0 ? kBcRed : team == 1 ? kBcBlue : kBcGrey; }
const char* kBcBaseWord[5] = {"BASE", "BASE", "BASIS", "BASE", "BASE"};   // the HUD's language (capitals, no accents)
const char* kBcContested[5] = {"CONTESTED", "CONTESTEE", "UMKAEMPFT", "CONTESA", "DISPUTADA"};
std::string BcBaseName(int i) { return std::string(kBcBaseWord[BcHudLanguage()]) + " " + std::to_string(i + 1); }
// the radar's centre and the 2D map's origin in the 4:3 HUD (pixels; the marks' places are relative to them)
float gBcRadarX = 430.0f, gBcRadarY = 90.0f, gBcMapX = 301.0f, gBcMapY = 56.0f;
long long gBcRadarFrame = -100;
void BcNumbers(Ctx& c) {                               // each base's number by its mark (the radar, the START menu's map)
    static bool read = false;
    if (!read) {                                       // tests: SIM_BC_MARKS="radar x,y,map x,y"
        read = true;
        if (const char* e = getenv("SIM_BC_MARKS")) sscanf(e, "%f,%f,%f,%f", &gBcRadarX, &gBcRadarY, &gBcMapX, &gBcMapY);
    }
    const bool map = BcMapShown(), radar = BcRadarShown(), strip = radar && BcStripShown();
    static const float side[4][2] = {{1, -1}, {-2, -1}, {1, 1}, {-2, 1}};   // the number's corner of its mark: the first
    float at[kBcMax][2];                                                     // one clear of the numbers placed (close marks)
    int placed = 0;
    for (int i = 0; i < gBc.n; ++i) {
        const BcMarkAt& a = map ? gBcMapAt[i] : gBcRadarAt[i];
        if ((!map && !radar) || !a.shown) continue;
        const BcBase& b = gBc.b[i];
        const BcRgba col = b.owner < 0 ? kBcWhite : BcTeamColour(b.owner);
        const float d = map ? 7.0f : 6.0f, x0 = (map ? gBcMapX : gBcRadarX) + a.x, y0 = (map ? gBcMapY : gBcRadarY) + a.y;
        float x = x0 + d, y = y0 - d;
        for (const auto& k : side) {
            const float cx = x0 + k[0] * d, cy = y0 + k[1] * d;
            bool clear = map || cx >= 380.0f;          // the radar's: within its label's reach of the right edge (BcText)
            for (int j = 0; j < placed && clear; ++j) clear = fabsf(cx - at[j][0]) >= 7.0f || fabsf(cy - at[j][1]) >= 7.0f;
            if (clear) { x = cx; y = cy; break; }
        }
        if (!map) x = std::max(x, 380.0f);
        if (!map && strip && y > 36.0f && y < 72.0f) continue;   // its label's row would take the strip's last letters
        at[placed][0] = x; at[placed][1] = y; ++placed;
        BcText(c, x, y, col, std::to_string(i + 1), map ? kBcPagePlace : kBcRadarPlace);
    }
}
// each base's name over its sign, as the game's labels over the items are placed (US n003a 0x5357F0: the matrix
// 0x2CF8E0 + 0xC0 (the address it builds), 4 rows: clip = x r0 + y r1 + z r2 + r3; the picture's x 256 + 256 clip.x / clip.w, y 224 + 224 clip.y /
// clip.w in the 512 x 448 picture); the bases within 45 m of this player, the START menu closed
// A label is kept off the HUD's own elements (Snake 23:56: BASE 1 over his name and the timer, their glyphs moved by its
// GS label): not in the top band (the name, the life bar, the timer and the counts, the strip, the gauge), on the
// radar, or within its reach of the item and weapon boxes' texts; not for the base this player stands in (its gauge)
bool BcLabelClear(float x, float y, size_t chars) {
    const float half = 5.5f * (float)chars, reach = BcGsReach(chars);
    if (y < 104.0f) return false;
    if (x + half > 352.0f && y < 176.0f) return false;
    if (y + 28.0f > 330.0f && (x - reach < 136.0f || x + reach > 376.0f)) return false;
    return true;
}
void BcLabels(Ctx& c, float px, float py, float pz) {
    const uint32_t mb = PageData(0x5357F0, 1, 0x2CF8E0), m = mb ? mb + 0xC0 : 0;   // the labels' own reference to it
    if (m < 0x100000 || BcMapShown()) return;
    for (int i = 0; i < gBc.n; ++i) {
        const BcBase& b = gBc.b[i];
        const float x = b.x, y = b.y + 3400.0f, z = b.z, dx = x - px, dz = z - pz;
        if (dx * dx + dz * dz > 45000.0f * 45000.0f || BcInside(b, px, py, pz)) continue;
        float cl[4];
        for (uint32_t k = 0; k < 4; ++k) cl[k] = x * F32(m + 4 * k) + y * F32(m + 16 + 4 * k) + z * F32(m + 32 + 4 * k) + F32(m + 48 + 4 * k);
        if (cl[3] < 200.0f || fabsf(cl[0]) > cl[3] || fabsf(cl[1]) > cl[3]) continue;
        const BcRgba col = b.contested ? kBcYellow : b.owner < 0 ? kBcWhite : BcTeamColour(b.owner);
        const std::string name = BcBaseName(i);
        const float sx = 256.0f + 256.0f * cl[0] / cl[3], sy = 224.0f + 224.0f * cl[1] / cl[3];
        if (!BcLabelClear(sx, sy, name.size())) continue;
        BcText(c, sx, sy, col, name, kBcWorldPlace);
    }
}
// ---- the round's first instruction (Snake's option: the HEADSHOT rule's box): the HUD's announcer box ([0x2F0B8C], n003a
// 0x3D78F0 / 0x3DA700: its layout +0x3AC, its time +0x3B8 in the game's units; the box node 3D12AE, the text node
// A5C8D6), in the HUD's letters (capitals; the accented ones Latin-1 - 0x20), once this player's first second alive is
// past and the box is free (TEAM SNEAKING's way, docs/TEAM_SNEAKING.md)
uint32_t BcAnnouncer() {
    const uint32_t at = PageData(0x3D7A20, 0, 0x2F0B8C), s = at ? R32(at) & 0x01FFFFFF : 0;
    return s >= 0x100000 && s + 0x3C0 < 0x02000000 ? s : 0;
}
bool BcAnnounce(Ctx& c, const std::string& text, uint32_t color, long long frames) {
    const uint32_t box = BcAnnouncer(), lay = box ? R32(box + 0x3AC) & 0x01FFFFFF : 0;
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
const char* kBcHint[5] = {"STAND IN THE BASES TO CAPTURE THEM", "RESTEZ DANS LES BASES POUR LES PRENDRE",   // the HUD's
                          "BLEIBT IN DEN BASEN, UM SIE EINZUNEHMEN", "RESTATE NELLE BASI PER CONQUISTARLE",     // language
                          "QUEDAOS EN LAS BASES PARA CAPTURARLAS"};
void BcHint(Ctx& c) {
    BcState& s = gBc;
    if (s.hintShown || s.aliveAt < 0 || gFrameNo < s.aliveAt + (long long)FieldRate()) return;
    const uint32_t box = BcAnnouncer();
    if (!box || (int32_t)R32(box + 0x3B8) > 0) return;  // the box busy (the HEADSHOT rule's own)
    const int lang = BcHudLanguage();
    const char* hint = kBcHint[BcJpProgram() || BcKrProgram() ? 0 : lang];   // the box: one byte a character
    if (BcAnnounce(c, hint, 0x808CA0A0u, (long long)(4.0 * FieldRate()))) {
        s.hintShown = true;
        Log("basecap: the round's instruction shown in the announcer box (language %d)", lang);
        BcPngAsk("hint", 40);                          // tests
    }
}
// a key moment in the same box: a base of this player's team being neutralized by the other ("BASE 2 UNDER ATTACK!"),
// once an attack (until its progress is undone or the base changes hands)
const char* kBcAttack[5] = {"BASE %d UNDER ATTACK!", "BASE %d ATTAQUEE !", "BASIS %d WIRD ANGEGRIFFEN!", "BASE %d SOTTO ATTACCO!",
                            "BASE %d BAJO ATAQUE!"};
void BcAttackWarn(Ctx& c, uint32_t me) {
    const int team = TsneTeamOf(me);
    for (int i = 0; i < gBc.n; ++i) {
        const BcBase& b = gBc.b[i];
        const bool attacked = team >= 0 && b.owner == team && b.toward >= 0 && b.toward != team && b.prog > 0;
        if (!attacked) { gBcWarned[i] = false; continue; }
        if (gBcWarned[i] || !gBc.hintShown) continue;
        const uint32_t box = BcAnnouncer();
        if (!box || (int32_t)R32(box + 0x3B8) > 0) continue;   // the box busy
        char t[64];
        snprintf(t, sizeof t, kBcAttack[BcJpProgram() || BcKrProgram() ? 0 : BcHudLanguage()], i + 1);
        if (BcAnnounce(c, t, 0x8040D0F0u, (long long)(3.0 * FieldRate()))) {
            gBcWarned[i] = true;
            Log("basecap: \"%s\" shown in the announcer box", t);
            BcPngAsk("attack", 30);
        }
        break;
    }
}
void BcHudTexts(Ctx& c, uint32_t me);
// ---- the capture gauge: flat quads as the HUD's life bar and timer box (ps2::GsHudQuads; docs/BASE_CAPTURE.md, The HUD)
const float kBcGaugeW = 128.0f;                        // the box's width (4:3 pixels), wider for a long text
const float kBcGaugeTop = 75.0f, kBcGaugeBottom = 103.0f, kBcGaugeBand = 3.0f;   // the box's rows, its bands above and below
const float kBcGaugeBarTop = 92.0f, kBcGaugeBarBottom = 99.0f, kBcGaugeBarIn = 112.0f;   // the trough's rows, the fill's width
const float kBcGaugeTextY = 76.0f;                     // the name's row (its glyphs about 2 to 14 below)
const uint32_t kBcGaugeBox = 0x601C2020, kBcGaugeBandOut = 0x001C2020;   // the timer's box (R G B A bytes) and its bands' far edge
const uint32_t kBcGaugeTrough = 0x58101010, kBcGaugeTick = 0x80141414;   // the life bar's frame and ticks
uint32_t BcPacked(const BcRgba& c, int a = -1, float k = 1.0f) {   // packed R G B A, darkened by k
    auto ch = [&](uint8_t v) { return (uint32_t)std::clamp((int)lrintf(v * k), 0, 255); };
    return ch(c.r) | ch(c.g) << 8 | ch(c.b) << 16 | (uint32_t)(a < 0 ? c.a : a) << 24;
}
bool gBcGaugeSent = false, gBcGaugeShown = false;      // the quads given this tick; the last ones given were not empty
void BcGaugeSend(const ps2::HudQuad* q, int n) {
    gBcGaugeSent = true;
    if (!n && !gBcGaugeShown) return;
    ps2::GsHudQuads(q, n);
    gBcGaugeShown = n > 0;
}
size_t BcGaugeChars(int i, const BcBase& b) { return BcBaseName(i).size() + (b.contested ? 2 + strlen(kBcContested[BcHudLanguage()]) : 0); }
void BcGaugeDraw(int base, const BcBase& b, size_t chars) {
    const float w = std::max(kBcGaugeW, 10.0f * (float)chars + 24.0f), x0 = 256.0f - w / 2, x1 = 256.0f + w / 2;
    const float pal = gPal ? 512.0f / 448.0f : 1.0f, k = (P.hudX > 0.0f ? P.hudX : 1.0f) * ps2::GsHudScale();
    auto X = [&](float x) { return 256.0f + (x - 256.0f) * k; };   // about the centre, as BcHudX places the middle third
    auto Y = [&](float y) { return BcHudY(y) * pal; };
    ps2::HudQuad q[16]; int n = 0;
    auto quad = [&](float ax, float ay, float bx, float by, uint32_t top, uint32_t bottom) {
        if (n < 16 && bx > ax && by > ay) q[n++] = {X(ax), Y(ay), X(bx), Y(by), top, bottom};
    };
    quad(x0, kBcGaugeTop - kBcGaugeBand, x1, kBcGaugeTop, kBcGaugeBandOut, kBcGaugeBox);           // the box and its bands
    quad(x0, kBcGaugeTop, x1, kBcGaugeBottom, kBcGaugeBox, kBcGaugeBox);
    quad(x0, kBcGaugeBottom, x1, kBcGaugeBottom + kBcGaugeBand, kBcGaugeBox, kBcGaugeBandOut);
    const float bx0 = 256.0f - kBcGaugeBarIn / 2 - 1.0f, bx1 = 256.0f + kBcGaugeBarIn / 2 + 1.0f;
    quad(bx0, kBcGaugeBarTop, bx1, kBcGaugeBarBottom, kBcGaugeTrough, kBcGaugeTrough);           // the trough (1 px round the fill)
    const float fy0 = kBcGaugeBarTop + 1.0f, fy1 = kBcGaugeBarBottom - 1.0f, fx0 = bx0 + 1.0f;
    const bool step = b.toward >= 0 && b.prog > 0;
    const float p = step ? std::clamp(b.prog, 0.0f, 1.0f) : b.owner >= 0 ? 1.0f : 0.0f;    // an owned base at rest: full
    const int team = step ? b.toward : b.owner;
    const bool blink = (gFrameNo / std::max(1LL, (long long)(FieldRate() / 4))) & 1;
    const BcRgba col = b.contested ? kBcYellow : BcTeamColour(team);
    const int alpha = b.contested && blink ? 0x50 : 0x80;
    if (step && b.owner >= 0 && b.owner != b.toward)                                            // a neutralization: the owner's rest
        quad(fx0 + kBcGaugeBarIn * p, fy0, fx0 + kBcGaugeBarIn, fy1, BcPacked(BcTeamColour(b.owner), 0x60), BcPacked(BcTeamColour(b.owner), 0x60, 0.6f));
    if (p > 0) quad(fx0, fy0, fx0 + kBcGaugeBarIn * p, fy1, BcPacked(col, alpha), BcPacked(col, alpha, 0.6f));   // the fill
    for (int t = 1; t < 8 && n < 16; ++t) {                                                        // the ticks: one picture pixel each
        const float tx = floorf(X(fx0 + kBcGaugeBarIn * t / 8) + 0.5f);
        q[n++] = {tx, Y(fy0), tx + 1.0f, Y(fy1), kBcGaugeTick, kBcGaugeTick};
    }
    BcGaugeSend(q, n);
    static int last = -1; static float lastP = -1.0f; static bool lastC = false;
    if (base != last || lastC != b.contested || fabsf(p - lastP) >= 0.25f || (p == 1.0f) != (lastP == 1.0f)) {
        last = base; lastP = p; lastC = b.contested;
        Log("basecap: the gauge drawn for BASE %d: %.0f %% %s%s (%d quads, box %.0f-%.0f)", base + 1, p * 100.0f,
            b.contested ? "contested" : team == 0 ? "red" : team == 1 ? "blue" : "empty", step ? " (a step)" : "", n, x0, x1);
    }
}
// the gauge's sound: this player in a base whose step fills toward his team (not contested), a tick every 0.6 s while
// the bar rose since the last one (the host's states on a joiner: every 250 ms); none before the round's start
void BcGaugeSound(Ctx& c, uint32_t me) {
    static float last[kBcMax] = {};                    // the progress at the last tick
    static long long next = 0;
    static int base = -1, ticks = 0;
    const int team = TsneTeamOf(me);
    const float x = F32(me + 0x110), y = F32(me + 0x114), z = F32(me + 0x118);
    int in = -1;
    for (int i = 0; i < gBc.n && in < 0; ++i) if (BcInside(gBc.b[i], x, y, z)) in = i;
    for (int i = 0; i < gBc.n; ++i) if (i != in || gBc.b[i].prog < last[i]) last[i] = std::min(last[i], gBc.b[i].prog);
    const BcBase* b = in >= 0 ? &gBc.b[in] : nullptr;
    const bool filling = b && (team == 0 || team == 1) && b->toward == team && !b->contested && b->prog > 0;
    if (!filling || in != base) {
        if (base >= 0) Log("basecap: the gauge's sound for BASE %d ended (%d ticks)", base + 1, ticks);
        base = -1; ticks = 0;
        if (!filling) return;
    }
    if (gFrameNo < next || b->prog <= last[in] + 0.004f || gFrameNo - gBcSeAt < (long long)(0.5 * FieldRate())) return;
    if (base < 0) { base = in; Log("basecap: the gauge's sound (%05x) for BASE %d, %.0f %%", kBcSeTick, in + 1, b->prog * 100.0f); }
    BcSe(c, kBcSeTick);
    ++ticks;
    last[in] = b->prog;
    next = gFrameNo + (long long)(0.6 * FieldRate());
}
int BcMenuScreen() {                                   // the match's menu screen (NewHudPause +0x326: 0 none, 6 START)
    const uint32_t w = PauseMenuWork();
    return w >= 0x100000 && w + 0x340 <= 0x02000000 ? (int)R16(w + 0x326) : 0;
}
void BcHudBody(Ctx& c);
void BcHud(Ctx& c) {                                   // the gauge's quads: given each tick, none when nothing is drawn
    gBcGaugeSent = false;
    BcHudBody(c);
    if (!gBcGaugeSent) BcGaugeSend(nullptr, 0);
}
void BcHudBody(Ctx& c) {                               // while the round runs and this player is in the map (alive, no
    if (!TsneHud() || !BcRunning()) return;            // weapons' choice nor select over it: the START menu is)
    const int self = OwnSlot();
    const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
    if (!me || (int16_t)R16(me + 0xB64) <= 0) { gBc.aliveAt = -1; return; }
    const int screen = BcMenuScreen();
    if (screen != 0 && screen != 6) {                  // a menu screen of the match up: BC's texts hidden
        if (gBc.aliveAt >= 0 && gFrameNo - gBc.aliveAt < 2 * (long long)FieldRate()) gBc.aliveAt = -1;
        BcPngAsk("menu", 40, 1u << 23);                // tests: a menu screen without BC's texts, once a round
        return;
    }
    if (gBc.aliveAt < 0) {
        gBc.aliveAt = gFrameNo;
        ++gBc.lives;                                   // tests: the round's first spawn, the respawns (two)
        if (gBc.lives == 1) BcPngAsk("spawn", 150, 1u << 16);
        else if (gBc.lives <= 3) BcPngAsk("respawn", 90, 1u << (15 + gBc.lives));
    }
    BcHint(c);
    BcAttackWarn(c, me);
    BcGaugeSound(c, me);
    if (!BcMapShown()) {                               // the gauge's quads: the base this player stands in (the texts: BcHudTexts)
        const float x = F32(me + 0x110), y = F32(me + 0x114), z = F32(me + 0x118);
        for (int i = 0; i < gBc.n; ++i) if (BcInside(gBc.b[i], x, y, z)) { BcGaugeDraw(i, gBc.b[i], BcGaugeChars(i, gBc.b[i])); break; }
    }
    if (BcDrawMode() < 3 || BcDrawMode() == 5) BcHudTexts(c, me);
}
void BcHudTexts(Ctx& c, uint32_t me) {                 // the HUD's texts (tests: SIM_BC_DRAWMODE 3 from the marks' hook)
    BcNumbers(c);
    const int red = BcOwned(0), blue = BcOwned(1);
    BcText(c, 213, 22, kBcRed, std::to_string(red), kBcTimerPlace);   // the bases held beside the timer, as TEAM DEATHMATCH's tickets
    BcText(c, 299, 22, kBcBlue, std::to_string(blue), kBcTimerPlace);
    if (BcMapShown()) return;                          // the START menu shown: its 2D map's numbers only
    const float x = F32(me + 0x110), y = F32(me + 0x114), z = F32(me + 0x118);
    BcLabels(c, x, y, z);
    for (int i = 0; i < gBc.n; ++i) {
        const BcBase& b = gBc.b[i];
        if (!BcInside(b, x, y, z)) continue;
        const std::string name = BcBaseName(i);        // the gauge: the base's name over the bar (BcGaugeDraw)
        const bool step = b.toward >= 0 && b.prog > 0;
        if (b.contested) BcPngAsk("contested", 20, 1u << i);
        else if (step && b.prog >= 0.4f && b.prog <= 0.6f) BcPngAsk("gauge", 12, 1u << (8 + i));
        else if (!step && b.owner < 0) BcPngAsk("gauges", 6, 1u << 28);   // tests (gauges): the bar at 0, 25, 75 and 100 %
        else if (step && b.prog >= 0.2f && b.prog <= 0.28f) BcPngAsk("gauges", 6, 1u << 29);
        else if (step && b.prog >= 0.7f && b.prog <= 0.78f) BcPngAsk("gauges", 6, 1u << 30);
        else if (!step && b.owner >= 0) BcPngAsk("gauges", 20, 1u << 31);
        const std::string text = b.contested ? name + "  " + kBcContested[BcHudLanguage()] : name;
        BcText(c, 256, kBcGaugeTextY, b.contested ? kBcYellow : step ? BcTeamColour(b.toward) : b.owner >= 0 ? BcTeamColour(b.owner) : kBcWhite, text);
        break;
    }
    static const bool glyphs = getenv("SIM_BC_GLYPHS") != nullptr;   // tests: the font's glyphs
    if (glyphs) BcText(c, 256, 100, kBcWhite, "#=|-_[]IO*+:/()%!?<>.,");
}
void BcWatch() {                                       // SIM_BC_WATCH=1 (tests): the bases each second
    static const bool on = getenv("SIM_BC_WATCH") != nullptr;
    static long long last = 0;
    if (!on || gFrameNo - last < 60) return;
    last = gFrameNo;
    std::string t;
    for (int i = 0; i < gBc.n; ++i) {
        const BcBase& b = gBc.b[i];
        char x[96];
        snprintf(x, sizeof x, " [%d %s %s%3.0f%%%s in %d/%d]", i + 1, b.owner < 0 ? "grey" : b.owner ? "BLUE" : "RED",
                 b.toward < 0 ? "" : b.toward ? "to blue " : "to red ", b.prog * 100.0f, b.contested ? " contested" : "", b.in[0], b.in[1]);
        t += x;
    }
    const uint32_t rec = BcRecord();
    Log("basecap watch: red %d blue %d, time %d%s;%s", BcOwned(0), BcOwned(1), rec ? (int32_t)R32(rec) : -1,
        IsSessionHost() ? (BcStarted() ? " (host)" : " (host, not started)") : "", t.c_str());
    static long long marks = -1000;                    // every 5 s: the marks (the table's entries, the radar's and map's nodes)
    if (gFrameNo - marks < 300) return;
    marks = gFrameNo;
    const uint32_t at = PageData(0x399F90, 0, 0x2E5228), tb = at ? R32(at) & 0x01FFFFFF : 0;
    std::string m;
    for (int i = 0; i < gBc.n && tb >= 0x100000; ++i) {
        const uint32_t idx = R32(BcGoal(i) + 0xAC), e = tb + 0x70 + 0x1C * idx;
        char x[96];
        snprintf(x, sizeof x, " [%d: mark %u flags %05x at %d %d rank %d]", i + 1, idx, idx < 0x60 ? R32(e + 0xC) : 0,
                 idx < 0x60 ? (int)R32(e + 4) : 0, idx < 0x60 ? (int)R32(e + 8) : 0, BcMarkRank(i));
        m += x;
    }
    Log("basecap marks: table %08x (%u) radar %08x (f%lld) map %08x (f%lld);%s", tb, tb ? R32(tb + 0x6C) : 0, gBcRadarObj, gBcRadarFrame,
        gBcMapObj, gBcMapFrame, m.c_str());
    for (int w = 0; w < 2; ++w) {
        const uint32_t o = w ? gBcMapObj : gBcRadarObj;
        if (o < 0x100000) continue;
        std::string q;
        for (uint32_t k = 0; k < 5; ++k) {
            const uint32_t g = R32(o + (w ? 0xD4 : 0x70) + 4 * k) & 0x01FFFFFF, sp = R32(o + (w ? 0xE8 : 0x84) + 4 * k) & 0x01FFFFFF;
            char x[160];
            snprintf(x, sizeof x, " [%u: group %08x fl %08x at %d %d sprite %08x col %08x rect %08x %08x size %08x]", k, g,
                     g >= 0x100000 ? R32(g + 0x10) : 0, g >= 0x100000 ? (int16_t)R16(g + 0x3C) : 0, g >= 0x100000 ? (int16_t)R16(g + 0x3E) : 0,
                     sp, sp >= 0x100000 ? R32(sp + 0x34) : 0, sp >= 0x100000 ? R32(sp + 0x40) : 0, sp >= 0x100000 ? R32(sp + 0x44) : 0,
                     sp >= 0x100000 ? R32(sp + 0x3C) : 0);
            q += x;
        }
        Log("basecap marks %s:%s", w ? "map" : "radar", q.c_str());
    }
}

// ---- the texts of the match: the rule's name (the HUD text 0x3AFBA0(0x22 + rule), the Japanese programs the script's
// string L + 0x22 + rule), its explanation (the script's string L + 0x1D + rule), CAPTURE's first message (L + 22)
const char* kBcNames[5] = {"BASE CAPTURE", "CAPTURE DE BASES", "BASISEROBERUNG", "CONQUISTA DELLE BASI", "CAPTURA DE BASES"};
const char* kBcCaptureNames[5] = {"CAPTURE MISSION", "MISSION DE CAPTURE", "GEFANGENNAHME MISSION", "MISSIONE DI CATTURA", "MISIpN CAPTURA"};
uint32_t gBcHudName[5] = {};
int BcNameLanguage(const std::string& capture) {
    for (int k = 0; k < 5; ++k) if (capture == kBcCaptureNames[k]) return k;
    return 0;
}
const char* kBcHelp[5] = {                             // four lines, as CAPTURE's (the page shows four): en fr es de it
    "Two teams fight for the bases of the map. Stay in a\nbase to capture it; the more of you, the faster. Your\n"
    "bases are your spawn points. Win by taking them all,\nor by holding the most when time runs out.\n",
    "Deux \x1F\x0Fquipes se disputent les bases de la carte.\nRestez dans une base pour la prendre (plus vite \x1F\x09\n"
    "plusieurs) ; vos bases sont vos points d'apparition.\nPrenez-les toutes, ou tenez-en le plus \x1F\x09 la fin.\n",
    "Dos equipos luchan por las bases del mapa. Qu\x1F\x0F" "date\nen una base para capturarla (m\x1F\x08s r\x1F\x08pido si sois\n"
    "varios); reapareces en tus bases. Gana quien las tome\ntodas o tenga m\x1F\x08s al acabar el tiempo.\n",
    "Zwei Teams k\x1F\x0Bmpfen um die Basen der Karte. Bleibt\nin einer Basis, um sie einzunehmen (zu mehreren\n"
    "schneller); eure Basen sind eure Startpunkte. Nehmt\nalle ein oder haltet am Ende die meisten.\n",
    "Due squadre si contendono le basi della mappa.\nRestate in una base per conquistarla (in pi\x1F\x1E, pi\x1F\x1E\n"
    "in fretta); rinascete nelle vostre basi. Vince chi\nle prende tutte o ne tiene di pi\x1F\x1E alla fine.\n",
};
uint32_t BcHelpText(Ctx& c) {                          // the explanation in the texts' language
    const int lang = BcLanguage(c);
    const std::string s = lang == kBcJp ? BcCjk(kBcJpFullHelp, kBcJpHelp) : lang == kBcKr ? BcCjk(kBcKrFullHelp, kBcHelp[kBcEn]) : std::string(kBcHelp[lang]);
    return BcTextAt(s);
}

// ---- the GAME TYPE page's picture (the rules pages' layout 69849c.la2, US n003a 0x3C9080): CAPTURE's two teams running
// toward the numbered bases, in two lanes (red toward BASE 1 .., blue toward BASE n ..). A base is CAPTURE's goal floor
// (bank 092AFC's 103494: the opaque texels of its outer quarters, four to one: its brackets) written into a free 32x32
// cell of the sheet of CAPTURE's soldiers (bank 127DB6, the Japanese programs' 984C8C), its number the page's own big
// digits (0A9BF3). DM's and TDM's picture nodes are shown too and given these sprites. A node object: +0x2C its draw
// block (op 0x11: the texture's record), +0x34 its colour, +0x38 its place, +0x3C its size (1/16 pixel, y high), +0x40 /
// +0x44 the texture's rectangle (1/16 texel). A record: +0 .. +0xC the rectangle (u v, its size, of the texture), +0x1C
// the bank-relative CLUT slot << 16 | TBP, +0x50 TEX0; a bank's texels at + [+0x18] (64 x 32 PSMCT32 pages)
struct BcPicAt { int x, y; };
const BcPicAt kBcPicRed = {48, 170}, kBcPicBlue = {312, 170};   // the two groups on one line (their sizes kept: 144 and 152 x 80)
const BcPicAt kBcPicBases[2] = {{222, 210}, {282, 210}};   // two numbered bases between them, at the soldiers' height
const int kBcPicIcon = 32, kBcPicIconSize = 40, kBcPicDigitW = 15, kBcPicDigitH = 25;   // the cell, its drawn size, a digit's
const uint32_t kBcPicNodes[] = {0x9F364A, 0x9F364B, 0x99C1CE, 0x99C1CF, 0x99C1D0, 0x99C1D1, 0x99C1D2, 0xC364A3, 0xC364A4, 0xC364A5, 0xC364A6};
uint32_t BcBankEntry(uint32_t bank, uint32_t code) {   // a bank's record of a texture (0: none)
    const uint32_t n = R32(bank + 0x10);
    for (uint32_t k = 0; k < n && k < 1024; ++k) if ((R32(bank + 0x20 + 0xA0 * k + 0x10) & 0xFFFFFF) == code) return bank + 0x20 + 0xA0 * k;
    return 0;
}
uint32_t BcT4At(uint32_t bank, uint32_t rec, uint32_t x, uint32_t y, uint32_t& shift) {   // a PSMT4 texel's word in a bank
    static const uint8_t bt1[8] = {0, 1, 4, 5, 16, 17, 20, 21}, bt2[4] = {0, 2, 8, 10};
    const uint32_t tbp = R32(rec + 0x1C) & 0xFFFF, w = (uint32_t)((R64(rec + 0x50) >> 14) & 0x3F) * 64;
    const uint32_t bx = x / 32, by = y / 16, xi = x % 32, yi = y % 16;
    const uint32_t blk = bt2[bx & 3] + bt1[by & 7] + (bx / 4 + (by / 8) * (w / 128)) * 32;
    uint32_t o = (yi / 4) * 16 + (xi & 7), s = xi & 0x18;
    if (yi & 1) o += 8;
    if (yi & 2) { o ^= 4; s += 4; }
    if (yi & 4) o ^= 4;
    shift = s;
    const uint32_t v = (tbp + blk) * 64 + o, b = (v % 2048) / 64, in = v % 64;
    uint32_t px = 0, py = 0;
    for (uint32_t yy = 0; yy < 4; ++yy) for (uint32_t xx = 0; xx < 8; ++xx) if (bt1[xx] + bt2[yy] == b) { px = xx; py = yy; }
    return bank + R32(bank + 0x18) + 4 * ((v / 2048) * 2048 + (py * 8 + in / 8) * 64 + px * 8 + in % 8);
}
uint32_t BcT4(uint32_t bank, uint32_t rec, uint32_t x, uint32_t y) { uint32_t s; return (R32(BcT4At(bank, rec, x, y, s)) >> s) & 0xF; }
int BcOpaque(uint32_t bank, uint32_t rec, bool all[16]) {   // a 4-bit record's opaque colours (alpha 0x80); the first
    const uint64_t tex0 = R64(rec + 0x50);
    const uint32_t slot = (R32(rec + 0x1C) >> 16) & 0x3FFF, csa = (uint32_t)(tex0 >> 56) & 0x1F;
    int first = -1;
    for (uint32_t e = 0; e < 16; ++e) {
        all[e] = (R32(BcClutWord(bank, slot, csa * 16 + e)) >> 24) >= 0x80;
        if (all[e] && first < 0) first = (int)e;
    }
    return first;
}
bool BcPicIcon(Ctx& c, uint32_t bank, uint32_t rec) {  // the base's brackets in a free cell of rec's sheet (once a bank)
    const uint64_t tex0 = R64(rec + 0x50);
    const uint32_t W = 1u << ((tex0 >> 26) & 0xF), H = 1u << ((tex0 >> 30) & 0xF), tbp = R32(rec + 0x1C) & 0xFFFF;
    if (((tex0 >> 20) & 0x3F) != 0x14 || W > 1024 || H > 1024) return false;
    bool on[16];
    const int ink = BcOpaque(bank, rec, on);
    const uint32_t src = CallPage(c, 0x122F18, 0x03092AFC) & 0x01FFFFFF, sr = BcPs2(src, 0x40) ? BcBankEntry(src, 0x103494) : 0;
    if (ink < 0 || !sr || ((R64(sr + 0x50) >> 20) & 0x3F) != 0x14) { Log("basecap: the GAME TYPE picture's base icon not made (ink %d, goal floor %08x)", ink, sr); return false; }
    bool solid[16];
    BcOpaque(src, sr, solid);
    uint8_t mask[32][32] = {};
    int lit = 0;
    for (uint32_t y = 0; y < 32; ++y)
        for (uint32_t x = 0; x < 32; ++x) {
            if (x >= 8 && x < 24) continue;            // the goal's text in the middle left out
            int k = 0;
            for (uint32_t j = 0; j < 16; ++j) k += solid[BcT4(src, sr, 4 * x + j % 4, 4 * y + j / 4)];
            mask[y][x] = k >= 4; lit += mask[y][x];
        }
    if (lit < 40) { Log("basecap: the goal floor's brackets not found (%d of 32 x 32 lit)", lit); return false; }
    auto mine = [&](uint32_t cx, uint32_t cy) {
        for (uint32_t y = 0; y < 32; ++y) for (uint32_t x = 0; x < 32; ++x) if ((BcT4(bank, rec, cx + x, cy + y) != 0) != (mask[y][x] != 0)) return false;
        return true;
    };
    if (gBcPic.bank == bank && gBcPic.icon && mine(gBcPic.cellX, gBcPic.cellY)) return true;
    // the free cell lying farthest into the bank's memory: the GS holds the font over the bank's first pages (rc3BC:
    // an icon in page 8 was wiped)
    const uint32_t n = R32(bank + 0x10), beg = bank + R32(bank + 0x18), end = bank + R32(bank + 0x1C);   // texels, CLUTs next
    int bestX = -1, bestY = -1;
    uint32_t bestPage = 0;
    for (uint32_t cy = 4; cy + 36 <= H; cy += 4) {
        for (uint32_t cx = 4; cx + 36 <= W; cx += 4) {
            bool free = true;
            for (uint32_t k = 0; k < n && k < 1024 && free; ++k) {   // no texture's rectangle of this sheet over it
                const uint32_t r = bank + 0x20 + 0xA0 * k;
                if ((R32(r + 0x1C) & 0xFFFF) != tbp) continue;
                const float u0 = F32(r) * W - 0.5f, v0 = F32(r + 4) * H - 0.5f, u1 = (F32(r) + F32(r + 8)) * W + 0.5f, v1 = (F32(r + 4) + F32(r + 0xC)) * H + 0.5f;
                free = u1 <= cx || u0 >= cx + 32 || v1 <= cy || v0 >= cy + 32;
            }
            uint32_t low = ~0u;                        // the cell's lowest page
            for (uint32_t j = 0; j < 34 * 34 && free; ++j) {   // its texels and a border: 0, in the bank
                uint32_t s;
                const uint32_t x = j % 34, y = j / 34, at = BcT4At(bank, rec, cx - 1 + x, cy - 1 + y, s);
                free = at + 4 <= end && !((R32(at) >> s) & 0xF);
                if (x >= 1 && x <= 32 && y >= 1 && y <= 32) low = std::min(low, (at - beg) / 8192);
            }
            if (free && (bestX < 0 || low > bestPage)) { bestX = (int)cx; bestY = (int)cy; bestPage = low; }
        }
    }
    if (bestX >= 0) {
        const uint32_t cx = (uint32_t)bestX, cy = (uint32_t)bestY;
        for (uint32_t y = 0; y < 32; ++y)
            for (uint32_t x = 0; x < 32; ++x) {
                uint32_t s;
                const uint32_t at = BcT4At(bank, rec, cx + x, cy + y, s);
                W32(at, (R32(at) & ~(0xFu << s)) | ((mask[y][x] ? (uint32_t)ink : 0u) << s));
            }
        gBcPic.bank = bank; gBcPic.rec = rec; gBcPic.cellX = cx; gBcPic.cellY = cy; gBcPic.icon = true;
        Log("basecap: the GAME TYPE picture's base icon (the goal floor 103494's brackets, %d texels) in the sheet of %06x at %u,%u (pages from %u, bank %06x)",
            lit, R32(rec + 0x10) & 0xFFFFFF, cx, cy, bestPage, R32(rec + 0x14) & 0xFFFFFF);
        return true;
    }
    Log("basecap: no free 32 x 32 cell in the sheet of %06x for the base icon", R32(rec + 0x10) & 0xFFFFFF);
    return false;
}
// the icon's own record: rec's with the cell's rectangle. A record's +0x20 is the GIF packet op 0x11 sends (TEX1 TEX2
// TEX0 ALPHA CLAMP_1); 487E8F's CLAMP_1 is REGION_CLAMP to its own rectangle (127DB6: 292..435 x 320..399), so a sprite
// of the cell drew nothing (bcW1)
uint32_t BcPicIconRecord(uint32_t rec) {
    BcPic& p = gBcPic;
    if (R64(rec + 0x78) != 0x08) {                     // CLAMP_1's address
        if (Once(((uint64_t)180 << 40) | gBc.round)) Log("basecap: the texture %06x's packet has no CLAMP_1 at +0x70: the icon drawn with it", R32(rec + 0x10) & 0xFFFFFF);
        return rec;
    }
    const uint64_t tex0 = R64(rec + 0x50), x = p.cellX, y = p.cellY, e = kBcPicIcon - 1;
    if (!p.iconRec && !(p.iconRec = Alloc(0xA0))) return rec;
    const float W = (float)(1u << ((tex0 >> 26) & 0xF)), H = (float)(1u << ((tex0 >> 30) & 0xF)), s = (float)kBcPicIcon;
    const uint64_t clamp = 2 | 2 << 2 | x << 4 | (x + e) << 14 | y << 24 | (y + e) << 34;   // REGION_CLAMP: the cell
    const float rect[4] = {x / W, y / H, s / W, s / H}, half[2] = {-s / H / 2, -s / W / 2}, mid[2] = {(y + s / 2) / H, (x + s / 2) / W};
    const uint32_t code = (R32(rec + 0x10) & 0xFF000000u) | 0xBA5E50u;
    uint8_t r[0xA0];
    memcpy(r, gMem + rec, sizeof r);
    memcpy(r, rect, 16); memcpy(r + 0x10, &code, 4); memcpy(r + 0x70, &clamp, 8); memcpy(r + 0x80, half, 8); memcpy(r + 0x90, mid, 8);
    if (memcmp(gMem + p.iconRec, r, sizeof r)) {
        memcpy(gMem + p.iconRec, r, sizeof r);
        if (Once(((uint64_t)181 << 40) | gBc.round))
            Log("basecap: the GAME TYPE picture's base icon drawn with its own record %08x (CLAMP_1 %016llx: the cell; %06x's %016llx)", p.iconRec,
                (unsigned long long)clamp, R32(rec + 0x10) & 0xFFFFFF, (unsigned long long)R64(rec + 0x70));
    }
    return p.iconRec;
}
// the bank's texels are sent to the GS memory when the bank is loaded (one 64-wide PSMCT32 image from its base block); its
// first pages (the font's) may hold other texels later. The sheet is there when the red team's own texels are; the icon
// written later is sent then (PATH3: BITBLTBUF .. TRXDIR, then IMAGE) if its cell holds no texel of another
char gBcGsWhy[160] = "";                              // the last BcGsIcon's comparison (logged)
int BcGsIcon(uint32_t bank, uint32_t rec, bool send) {  // -1: the sheet not there, 0: the icon there, 1: sent now, 2: not there
    const uint64_t tex0 = R64(rec + 0x50);
    const uint32_t base = ((uint32_t)tex0 & 0x3FFF) - (R32(rec + 0x1C) & 0xFFFF), rows = (R32(bank + 0x1C) - R32(bank + 0x18)) / 256;
    if (base % 32 || !rows || rows > 2048 || !gBcPic.icon) return -1;
    const std::vector<uint32_t> gs = GsFrameRgba((uint64_t)(base / 32) | 1ull << 16, 64, (int)rows);   // PSMCT32, 64 wide
    const uint32_t tex = bank + R32(bank + 0x18), W = 1u << ((tex0 >> 26) & 0xF), H = 1u << ((tex0 >> 30) & 0xF);
    auto texel = [&](uint32_t x, uint32_t y, uint32_t& g, uint32_t& e) {   // the GS's and the bank's 4-bit texel
        uint32_t s;
        const uint32_t w = (BcT4At(bank, rec, x, y, s) - tex) / 4;
        if (w >= gs.size()) return false;
        g = (gs[w] >> s) & 0xF; e = (R32(tex + 4 * w) >> s) & 0xF;
        return true;
    };
    const uint32_t u0 = (uint32_t)(F32(rec) * (float)W), v0 = (uint32_t)(F32(rec + 4) * (float)H);
    const uint32_t u1 = std::min(W, u0 + (uint32_t)(F32(rec + 8) * (float)W)), v1 = std::min(H, v0 + (uint32_t)(F32(rec + 0xC) * (float)H));
    uint32_t seen = 0, off = 0, busy = 0, missing = 0, g, e;
    for (uint32_t y = v0; y < v1; y += 2)              // the red team's texels (every second one)
        for (uint32_t x = u0; x < u1; x += 2) {
            if (!texel(x, y, g, e)) return -1;
            ++seen; off += g != e;
        }
    for (uint32_t j = 0; j < 32 * 32; ++j) {           // the cell: free or the icon's
        if (!texel(gBcPic.cellX + j % 32, gBcPic.cellY + j / 32, g, e)) return -1;
        if (g != e) { if (g) ++busy; else ++missing; }
    }
    snprintf(gBcGsWhy, sizeof gBcGsWhy, "base block %u: %u of the red team's %u texels differ; the cell: %u texels of another, %u missing",
             base, off, seen, busy, missing);
    if (seen < 64 || off * 50 > seen || busy) return -1;   // the sheet not there (2 % may differ)
    if (!missing || !send) return missing ? 2 : 0;
    uint64_t q[2 * 38] = {};
    q[0] = 4 | 1ull << 60; q[1] = 0xE;                                                        // GIFtag: 4 A+D
    q[2] = ((tex0 & 0x3FFF) << 32) | (((tex0 >> 14) & 0x3F) << 48) | (0x14ull << 56); q[3] = 0x50;   // BITBLTBUF: the sheet, PSMT4
    q[4] = (uint64_t)gBcPic.cellX << 32 | (uint64_t)gBcPic.cellY << 48; q[5] = 0x51;          // TRXPOS
    q[6] = 32 | 32ull << 32; q[7] = 0x52;                                                     // TRXREG: 32 x 32
    q[8] = 0; q[9] = 0x53;                                                                    // TRXDIR: host -> local
    q[10] = 32 | 1ull << 15 | 2ull << 58;                                                     // GIFtag: IMAGE, 32 quadwords, the end
    uint8_t* px = (uint8_t*)&q[12];
    for (uint32_t y = 0; y < 32; ++y)
        for (uint32_t x = 0; x < 32; x += 2)
            px[y * 16 + x / 2] = (uint8_t)(BcT4(bank, rec, gBcPic.cellX + x, gBcPic.cellY + y) | BcT4(bank, rec, gBcPic.cellX + x + 1, gBcPic.cellY + y) << 4);
    if (GsThreadOn()) GsQueueGif((const uint8_t*)q, 38); else GifPath(3, (const uint8_t*)q, 38);
    return 1;
}
void BcGsIconLog(int gs, int when) {                   // each outcome once a round (when: 0 the set-up, 1 the page drawn, 2 later)
    if (Once(((uint64_t)178 << 40) | (uint64_t)(gs + 1) << 36 | (uint64_t)when << 32 | gBc.round))
        Log("basecap: the GAME TYPE picture's base icon in the GS memory %s: %s (%s)",
            when == 0 ? "at the page's set-up" : when == 1 ? "once the page is drawn" : "while the page is up",
            gs == 1 ? "sent now" : gs == 0 ? "there already" : gs == 2 ? "not there" : "not sent (the sheet not there)", gBcGsWhy);
}
uint32_t BcBlockRecord(uint32_t o) {                   // the word of a node's draw block that holds its texture's record
    const uint32_t blk = R32(o + 0x2C) & 0x01FFFFFF;
    if (!BcPs2(blk, 0x40)) return 0;
    for (uint32_t k = 0; k < 10; ++k) {               // op 0x11 in its low byte (map 15's page n022a: 68012f11), then the record
        const uint32_t op = R32(blk + 4 * k), rec = R32(blk + 4 * k + 4) & 0x01FFFFFF;
        if ((op & 0xFF) != 0x11 || !BcPs2(rec, 0xA0)) continue;
        const uint32_t bank = R32(rec + 0x14) & 0xFFFFFF;
        if (op == 0x11 || bank == 0x127DB6 || bank == 0x984C8C) return blk + 4 * k + 4;
    }
    return 0;
}
uint32_t BcPicBank(Ctx& c) {                           // the page's bank: the one of the resource tree holding its sprites
    for (uint32_t code : {0x127DB6u, 0x984C8Cu}) {
        const uint32_t d = CallPage(c, 0x122F18, 0x03000000u | code) & 0x01FFFFFF;
        if (BcPs2(d, 0x40) && R32(d + 0x10) && R32(d + 0x10) < 1024 && BcBankEntry(d, 0x487E8F) && BcBankEntry(d, 0x0A9BF3)) return d;
    }
    return 0;
}
void BcPicture(Ctx& c, uint32_t lay) {                 // the picture made (a new layout or round) and kept: each node's fields and
    BcPic& p = gBcPic;                                 // its draw block's record (the blocks are made after the page's set-up)
    if (lay != p.lay || p.round != gBc.round) {
        p.lay = lay; p.round = gBc.round; p.nodes.clear(); p.complete = false; p.tried = false; p.drawn = false;
        p.iconOk = false; p.iconTries = 0;
    }
    if (p.nodes.empty()) {
        const uint32_t red = La2Object(lay, 0x9F3648), blue = La2Object(lay, 0x9F3649), bank = BcPicBank(c);
        const uint32_t redRec = bank ? BcBankEntry(bank, 0x487E8F) : 0, digits = bank ? BcBankEntry(bank, 0x0A9BF3) : 0;
        if (!red || !blue || !redRec || !digits) {     // CAPTURE's picture then, without its KEROTAN (its bubble, the KEROTAN)
            for (uint32_t node : {0x9F364Au, 0x9F364Bu}) if (const uint32_t o = La2Object(lay, node)) W32(o + 0x3C, 0);
            if (!p.tried) Log("basecap: the GAME TYPE picture's nodes or bank not found (layout %08x: %08x %08x, bank %08x)", lay, red, blue, bank);
            p.tried = true;
            return;
        }
        if (ps2::Fn show = ps2::FindFn(V3(0x3C7090)))  // DM's and TDM's pictures' nodes: the bases and their numbers
            for (uint32_t g : {0xE0956Cu, 0x353BF8u}) { Ctx t = c; t.r[4] = lay; t.r[5] = g; show(t); }
        const bool icon = BcPicIcon(c, bank, redRec);
        const uint32_t iconRec = icon ? BcPicIconRecord(redRec) : 0;
        if (icon) BcGsIconLog(BcGsIcon(bank, redRec, true), 0);   // the bank was sent there before
        auto fx = [](float v) { return (uint32_t)(int32_t)(v * 16.0f) & 0xFFFF; };
        auto sprite = [&](uint32_t o, uint32_t rec, float u, float v, float w, float h, int x, int y, int sw, int sh) {
            p.nodes.push_back({o, rec, fx(v + 0.5f) << 16 | fx(u + 0.5f), fx(v + h - 0.5f) << 16 | fx(u + w - 0.5f),
                               fx((float)y) << 16 | fx((float)x), fx((float)sh) << 16 | fx((float)sw), 0x80141818u, false});   // CAPTURE's colour
        };
        p.nodes.push_back({red, 0, 0, 0, fx((float)kBcPicRed.y) << 16 | fx((float)kBcPicRed.x), 0, 0, true});
        p.nodes.push_back({blue, 0, 0, 0, fx((float)kBcPicBlue.y) << 16 | fx((float)kBcPicBlue.x), 0, 0, true});
        const BcPicAt* at = kBcPicBases;
        const uint32_t dW = 1u << ((R64(digits + 0x50) >> 26) & 0xF), dH = 1u << ((R64(digits + 0x50) >> 30) & 0xF);
        const float du = F32(digits) * dW - 0.5f, dv = F32(digits + 4) * dH - 0.5f, dw = (F32(digits + 8) * dW + 1) / 10, dh = F32(digits + 0xC) * dH + 1;
        const int count = (int)(sizeof kBcPicNodes / sizeof *kBcPicNodes), bases = 2;
        for (int k = 0; k < count; ++k) {
            const uint32_t o = La2Object(lay, kBcPicNodes[k]);
            if (!o) continue;
            const int b = k / 2;
            if (b >= bases || (k % 2 == 0 && !icon)) p.nodes.push_back({o, 0, R32(o + 0x40), R32(o + 0x44), R32(o + 0x38), 0, R32(o + 0x34), false});   // size 0
            else if (k % 2 == 0) sprite(o, iconRec, (float)p.cellX, (float)p.cellY, (float)kBcPicIcon, (float)kBcPicIcon,
                                         at[b].x - kBcPicIconSize / 2, at[b].y - kBcPicIconSize / 2, kBcPicIconSize, kBcPicIconSize);
            else sprite(o, digits, du + dw * (float)(b + 1), dv, dw, dh, at[b].x - kBcPicDigitW / 2, at[b].y - kBcPicDigitH / 2, kBcPicDigitW, kBcPicDigitH);
        }
        Log("basecap: the GAME TYPE page's picture: CAPTURE's teams on one line toward %d numbered bases (layout %08x, bank %06x, %zu sprites)",
            bases, lay, R32(bank + 0x20 + 0x14) & 0xFFFFFF, p.nodes.size());
    }
    int shown = 0;                                     // a sprite or group hidden by the layout's data shown (0x3C7090's way)
    auto show = [&](uint32_t o) { if (o && !(R32(o + 0x10) & 0x400000)) { W32(o + 0x10, R32(o + 0x10) | 0xC00000); ++shown; } };
    for (const BcPicNode& q : p.nodes) if (!q.move && q.size) show(q.o);
    for (uint32_t g : {0xE0956Cu, 0x353BF8u}) show(La2Object(lay, g));
    if (shown && Once(((uint64_t)179 << 40) | gBc.round)) Log("basecap: the GAME TYPE picture: %d of its nodes were hidden: shown", shown);
    if (!p.drawn && !p.nodes.empty() && BcBlockRecord(p.nodes[0].o)) {   // CAPTURE's red team drawn: the page is up
        p.drawn = true;
        Log("basecap: the GAME TYPE page drawn (frame %lld)", gFrameNo);
        if (p.icon && p.bank && p.rec) { const int r = BcGsIcon(p.bank, p.rec, true); p.iconOk = r == 0 || r == 1; BcGsIconLog(r, 1); }
        if (!(gBc.pngDone & (1u << 24))) BcPngAsk("type", 15, 1u << 24);   // tests: the page drawn
    } else if (p.drawn && !p.iconOk && p.icon && p.bank && p.rec && p.iconTries < 40 && gFrameNo % 15 == 0) {
        ++p.iconTries;                                 // the sheet sent there later: the icon with it
        const int r = BcGsIcon(p.bank, p.rec, true);
        p.iconOk = r == 0 || r == 1;
        if (p.iconOk || p.iconTries == 40) BcGsIconLog(r, 2);
        if (p.iconOk) BcPngAsk("type", 15, 1u << 27);  // tests: the page with its icon
    }
    static const bool picLog = getenv("SIM_BC_PICLOG") != nullptr;   // tests: the nodes' state each second while kept
    if (picLog && gFrameNo % 60 == 0) {
        std::string t;
        char b[64];
        for (const BcPicNode& q : p.nodes) {
            snprintf(b, sizeof b, " %06x:%08x/%08x/%c", R32(q.o + 0xC) & 0xFFFFFF, R32(q.o + 0x10), R32(q.o + 0x2C), BcBlockRecord(q.o) ? 'r' : '-');
            t += b;
        }
        Log("basecap: picture nodes (code:flags/block/record)%s", t.c_str());
        for (const BcPicNode& q : p.nodes) {
            const uint32_t blk = R32(q.o + 0x2C) & 0x01FFFFFF;
            if (BcBlockRecord(q.o) || !BcPs2(blk, 0x40)) continue;
            t.clear();
            for (uint32_t k = 0; k < 12; ++k) { snprintf(b, sizeof b, " %08x", R32(blk + 4 * k)); t += b; }
            Log("basecap: picture node %06x's block without a record:%s", R32(q.o + 0xC) & 0xFFFFFF, t.c_str());
            break;
        }
    }
    if (p.iconRec && p.rec) BcPicIconRecord(p.rec);    // kept as the red team's record is
    int waiting = 0, put = 0;
    for (const BcPicNode& q : p.nodes) {               // the fields, then the draw block's record once the block is made
        if (R32(q.o + 0x38) != q.xy) { W32(q.o + 0x38, q.xy); ++put; }
        if (q.move) continue;
        if (R32(q.o + 0x40) != q.uv0) { W32(q.o + 0x40, q.uv0); ++put; }
        if (R32(q.o + 0x44) != q.uv1) { W32(q.o + 0x44, q.uv1); ++put; }
        if (R32(q.o + 0x3C) != q.size) { W32(q.o + 0x3C, q.size); ++put; }
        if (R32(q.o + 0x34) != q.colour) { W32(q.o + 0x34, q.colour); ++put; }
        if (!q.rec || !q.size) continue;
        if (const uint32_t w = BcBlockRecord(q.o)) { if ((R32(w) & 0x01FFFFFF) != q.rec) { W32(w, q.rec); ++put; } }
        else ++waiting;
    }
    if (!waiting && !p.complete) {
        p.complete = true;
        Log("basecap: the GAME TYPE page's picture: every sprite's draw block given its texture (frame %lld)", gFrameNo);
    } else if (put && p.complete && Once(((uint64_t)175 << 40) | gBc.round)) Log("basecap: the GAME TYPE picture's words were changed by the HUD: put again");
    if (waiting && gFrameNo - p.hooked > 20 * (long long)FieldRate() && Once(((uint64_t)177 << 40) | gBc.round))
        Log("basecap: the GAME TYPE picture: %d sprites still without a draw block 20 s after the page", waiting);
}

// ---- the in-match hooks (a call's n003a address, the round BC); true when served
bool BcMatchCall(Ctx& c, uint32_t va, uint32_t a3) {
    if (a3 == 0x39E0B0) {                                // the mission objects' options: the bases in the goals' slots
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t mo = (uint32_t)c.r[4];
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        if ((int32_t)(uint32_t)c.r[2] >= 0) BcObjects(c, mo);
        return true;
    }
    if (a3 == 0x3AFBA0) {                                // a HUD text: CAPTURE's name is BC's, in its language
        if ((int32_t)(uint32_t)c.r[4] != 0x22 + kBcAs) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        const int lang = BcNameLanguage(TsneCString((uint32_t)c.r[2] & 0x01FFFFFF));
        if (const uint32_t at = TsneString(gBcHudName[lang], kBcNames[lang])) { c.r[2] = at; c.r[3] = 0; }
        return true;
    }
    if ((a3 == 0x5204E8 || a3 == 0x3AC898) && gBc.n && BcObjectsAlive()) {   // the radar's marks, the 2D map's: the bases' colours, their places
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t o = (uint32_t)c.r[4];
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        if (o >= 0x100000 && o + 0x100 < 0x02000000) {
            if (a3 == 0x5204E8) { BcMarkNodes(o + 0x70, o + 0x84, gBcRadarAt); gBcRadarFrame = gFrameNo; gBcRadarPass = gBcPass; gBcRadarObj = o; }
            else { BcMarkNodes(o + 0xD4, o + 0xE8, gBcMapAt); gBcMapFrame = gFrameNo; gBcMapPass = gBcPass; gBcMapObj = o; BcPngAsk("map", 20, 1u << 19); }
            if (BcDrawMode() == 3 && TsneHud() && BcRunning()) {   // the HUD's texts drawn with the marks' own
                const int self = OwnSlot();
                const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
                const int screen = BcMenuScreen();
                if (me && (int16_t)R16(me + 0xB64) > 0 && (screen == 0 || screen == 6)) { gBcInMarks = true; BcHudTexts(c, me); gBcInMarks = false; }
            }
        }
        return true;
    }
    if (a3 == 0x3C9080) {                                // the GAME TYPE page: its picture made BC's (BcPicture)
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint32_t hud = (uint32_t)c.r[4];
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        const uint64_t v0 = c.r[2];
        const uint32_t lay = hud >= 0x100000 && hud + 0x44 < 0x02000000 ? R32(hud + 0x40) & 0x01FFFFFF : 0;
        if (lay) { gBcPic.hooked = gFrameNo; gBcPic.hud = TsneHud(); BcPicture(c, lay); }
        if (const uint32_t g = TsneHud()) if (TsneRecordByte(g, 0xBC) && Once(((uint64_t)176 << 40) | gBc.round))
            Log("basecap: round %u of the entry (wins red %u, blue %u; it ends at %u team victories or after %u rounds)", TsneRecordByte(g, 0xBC),
                TsneRecordByte(g, 0xC4), TsneRecordByte(g, 0xC8), TsneRecordByte(g, 0xCC), TsneRecordByte(g, 0xC0));
        c.r[2] = v0;
        if (!(gBc.pngDone & (1u << 22))) { BcPngAsk("type", 30); BcPngAsk("type", 240, 1u << 22); }   // tests: the page, once a round
        return true;
    }
    if (a3 == 0x3D5050) { gBcStripUntil = 0; return false; }   // a message of the game's own in the strip: not tinted
    if (a3 == 0x3C7110 && gBc.n) {                       // a layout text: the round's result title CAPTURE says by its goals
        static uint32_t gen = 0xFFFFFFFFu; static int lang = -1;   // (the language's layout texts 158 / 159 RED GOAL / BLUE
        static std::string goal[3]; static uint32_t win[3] = {};   // GOAL, US n003a 0x3AF7E8) said with 155 / 156, RED / BLUE
        if (gen != gPageGen || lang != BcHudLanguage()) {           // TEAM WINS!, a draw's 154 MISSION COMPLETE! with 157 DRAW!
            gen = gPageGen; lang = BcHudLanguage();                 // (read once a page and language)
            for (uint32_t k = 0; k < 3; ++k) {
                const uint32_t g = CallPage(c, 0x3AF7E8, k < 2 ? 158 + k : 154) & 0x01FFFFFF, w = CallPage(c, 0x3AF7E8, k < 2 ? 155 + k : 157) & 0x01FFFFFF;
                goal[k] = BcPs2(g, 4) && BcPs2(w, 4) && g != w ? TsneCString(g) : std::string();
                win[k] = w;
            }
        }
        const uint32_t t = (uint32_t)c.r[6] & 0x01FFFFFF;
        if (!BcPs2(t, 4)) return false;
        static const char* kGoal[3] = {"RED GOAL", "BLUE GOAL", "MISSION COMPLETE!"}, *kWin[3] = {"RED TEAM WINS!", "BLUE TEAM WINS!", "DRAW!"};
        static uint32_t ours[3] = {};                  // the Japanese programs' (English)
        for (uint32_t k = 0; k < 3; ++k) {
            if (k == 2 && (BcRunning() || BcOwned(0) != BcOwned(1))) break;   // CAPTURE's result 1: BC's draw
            const bool table = !goal[k].empty() && gMem[t] == (uint8_t)goal[k][0] && TsneCString(t) == goal[k];
            if (!table && (gMem[t] != (uint8_t)kGoal[k][0] || TsneCString(t) != kGoal[k])) continue;
            if (!table && !ours[k]) ours[k] = BcTextAt(kWin[k]);
            c.r[6] = table ? win[k] : ours[k];
            if (Once(((uint64_t)173 << 40) | gBc.round)) Log("basecap: the round's result title \"%s\" said as \"%s\"", TsneCString(t).c_str(), TsneCString((uint32_t)c.r[6]).c_str());
            break;
        }
        return false;
    }
    if (a3 == 0x3EAC68 && gBc.n) {                       // CAPTURE's round decider: the bases'
        c.r[2] = BcDecide(c, (uint32_t)c.r[4]) ? 1 : 0; c.r[3] = 0;
        return true;
    }
    return false;
}
// the base's calls in a BC round on a map's page (US addresses); true when served
bool BcBaseCall(Ctx& c, uint32_t addr, uint32_t va) {
    if (addr == 0x122F18 && gBcGrey.ready && gBcGrey.round == gBc.round && ((uint32_t)c.r[4] >> 24) == 0x0D) {   // the grey models
        const uint32_t code = (uint32_t)c.r[4] & 0xFFFFFF;
        for (int k = 0; k < 3; ++k)
            if (code == kBcGreyCodes[k]) { c.r[2] = gBcGrey.def[k]; c.r[3] = 0; return true; }
        return false;
    }
    if (addr == 0x161200) {                              // a point by its code: the spawns (BcTdmCode, BcSpawnBase, BcRespawnClear)
        uint32_t h = (uint32_t)c.r[7];
        const int team = BcRespawnTeam(h);
        const int i = team >= 0 && gBc.n ? BcSpawnBase(team) : -1;
        if (team >= 0 && i < 0 && BcRespawnForce() >= 0) h = (h & 0xFF000000u) | ((team ? 0x2B1AC2u : 0x2B16C2u) + (uint32_t)BcRespawnForce());
        const uint32_t tdm = i < 0 ? BcTdmCode(h) : 0;
        if (i < 0 && !tdm) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        if (i < 0) {                                     // CAPTURE's start or respawn: TEAM DEATHMATCH's, when the stage has it
            Ctx t = c;
            t.r[7] = (h & 0xFF000000u) | tdm;
            f(t);
            if (!(uint32_t)t.r[2]) return false;
            const uint32_t took = team >= 0 && gBc.n ? BcRespawnClear(c, f, t, h, tdm, team) : tdm;
            c.r[2] = t.r[2]; c.r[3] = t.r[3];
            if (Once(((uint64_t)170 << 40) | ((uint64_t)gBc.round << 24) | (h & 0xFFFFFF)))
                Log("basecap: the spawn point %06x taken for CAPTURE's %06x", took, h & 0xFFFFFF);
            const uint32_t out = (uint32_t)c.r[4];
            if (team >= 0 && BcPs2(out, 12) && Once(((uint64_t)184 << 40) | (uint64_t)(gFrameNo / 30)))
                Log("basecap: this player respawns at TEAM DEATHMATCH's %06x (team %d, no base held), %.0f %.0f %.0f", took, team, F32(out), F32(out + 4), F32(out + 8));
            return true;
        }
        const uint32_t out = (uint32_t)c.r[4];
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        if (!(uint32_t)c.r[2] || out < 0x100000 || out + 16 >= 0x02000000) return true;
        const int self = OwnSlot();
        const float a = (float)((self < 0 ? 0 : self) % 8) * 0.785398f, r = 500.0f;
        const BcBase& b = gBc.b[i];
        BcWF(out, b.x + r * sinf(a)); BcWF(out + 4, b.y); BcWF(out + 8, b.z + r * cosf(a));
        if (Once(((uint64_t)171 << 40) | (uint64_t)(gFrameNo / 30)))
            Log("basecap: this player respawns at BASE %d (team %d), %.0f %.0f %.0f", i + 1, team, F32(out), F32(out + 4), F32(out + 8));
        return true;
    }
    if (addr == 0x114DA0 && gBcStripQuiet && gBcMsgIndex && ((uint32_t)c.r[4] & 0x7FF) == kBcSeStrip) {   // the strip's own effect
        c.r[2] = 0; c.r[3] = 0;
        Log("basecap: the message strip's own sound left out");
        return true;
    }
    if (addr == 0x125408) {                              // a script string: BC's messages, explanation, first message, name
        const uint32_t i = (uint32_t)c.r[4];
        if (gBcMsgIndex && i >= kBcStrBase + 0x10 && i < kBcStrBase + 0x20) { c.r[2] = gBcMsgIndex; c.r[3] = 0; return true; }
        const uint32_t g = TsneHud();
        if (!g) return false;
        const uint32_t base = R32(g + 0xDC);
        const bool help = i == base + 0x1D + kBcAs, name = i == base + 0x22 + kBcAs, start = i == base + 22;
        if (!help && !name && !start) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
        const uint32_t was = (uint32_t)c.r[2] & 0x01FFFFFF;
        if (name) {                                      // "CAPTURE MISSION\r\n" (the Japanese programs' pages read it here)
            const std::string t = TsneCString(was);
            if (t.rfind("CAPTURE MISSION", 0) == 0 && t.find_first_not_of("\r\n", 15) == std::string::npos) {
                static uint32_t at = 0;
                const std::string s = std::string(kBcNames[0]) + t.substr(15);
                if (!at && (at = Alloc((uint32_t)s.size() + 1))) memcpy(gMem + at, s.c_str(), s.size() + 1);
                if (at) { c.r[2] = at; c.r[3] = 0; }
            }
        } else if (help) {
            if (const uint32_t at = BcHelpText(c)) { c.r[2] = at; c.r[3] = 0; }
        } else if (start) {                              // CAPTURE's first message ("Capture the Kerotan ..."): BC's
            if (const uint32_t at = BcMessageString(c, kBcMsgStart, 0)) { c.r[2] = at; c.r[3] = 0; }
        }
        return true;
    }
    return false;
}
// a call while a map's page is loaded (DispatchBridge): the BC round's hooks
bool BcBridge(Ctx& c, uint32_t addr, uint32_t va) {
    if (!TsneMapPage() || !BcRound()) return false;
    if (addr < 0x2DA680) return BcBaseCall(c, addr, va);
    const uint32_t a3 = gPageMap ? A3(addr) : addr;
    if (!a3) return false;
    return BcMatchCall(c, va, a3);
}

// ---- the lobby (ntmenu2's Create Game 2, US addresses): BC first in the list of rules (above TEAM SNEAKING: 7 rows,
// the rules 6 .. 0 from the top), offered on the maps that offer CAPTURE, named from strings of ours
uint32_t gBcMask = 0;                                  // the maps offering it (bit map - 1); 0: the lobby not set up
uint32_t gBcRowEntries = 0;                            // BC's {key, name} in the 5 languages (the rotation's rows)
uint32_t gBcRowsLang0 = 0;                             // the English rows' table less 16 (0x37B828: rule 6's entry)
const char* kBcLobbyNames[5] = {"BASE CAPTURE", "CAPTURE DE BASES", "BASISEROBERUNG", "CONQUISTA DELLE BASI", "CAPTURA DE BASES"};
std::string BcLobbyText(int kind, int lang) {
    if (kind == 0) return kBcLobbyNames[0];
    if (kind == 1) return "BASE";
    const char* v = TsneVersion();
    if (!strcmp(v, "JP") || !strcmp(v, "JPS"))         // ベース・キャプチャー in ntmenu2's own glyphs
        return std::string("\x82\x39\x83\x12\x82\x19\x9f\xa1\x82\x0d\x82\x43\x82\x37\x82\x21\x82\x43\x83\x12", 20);
    if (!strcmp(v, "KR")) return "Base Capture";
    static const char* help[3] = {"Base Capture", "Capture de bases", "Captura de bases"};
    return help[lang >= 0 && lang < 3 ? lang : 0];
}
uint32_t gBcLobbyStr[5] = {};
uint32_t BcLobbyString(int kind, int lang = 0) {
    const int k = kind == 2 ? 2 + (lang >= 0 && lang < 3 ? lang : 0) : kind;
    if (!gBcLobbyStr[k]) {
        const std::string t = BcLobbyText(kind, lang);
        gBcLobbyStr[k] = Alloc((uint32_t)t.size() + 1);
        if (gBcLobbyStr[k]) memcpy(gMem + gBcLobbyStr[k], t.c_str(), t.size() + 1);
    }
    return gBcLobbyStr[k];
}
bool BcMapOffers(int id) {                             // a map offers BC: the twelve, an extra map that offers CAPTURE
    if (id >= 1 && id <= 12) return true;
    const ExtraMap* m = ExtraMapById(id);
    if (!m || !m->ready) return false;
    for (int r : m->rules) if (r == kBcAs || r == kBcRule) return true;
    return false;
}
int gBcRows = 0;                                       // the list's rows (7: BC added above TSNE's 6)
uint32_t gBcCaptureHelp = 0;                           // CAPTURE's help line's string (the Korean help: "BASE " and it)
void BcLobby(uint32_t basesAt) {                       // after TsneLobby (ntmenu2's [27b5ee])
    gBcMask = 0;
    const uint32_t kinds = PageData(0x3751FC, 0, 0x2DB958), langs = PageData(0x3751FC, 6, 0x6142F8);
    const uint32_t k7 = kinds + 7 * 0x2C;
    const uint32_t rows = kinds >= 0x100000 ? R32(k7 + 0x1C) : 0;
    if (kinds < 0x100000 || rows != 6 || !gTsneMask || !basesAt) {   // only above TSNE's list: its rows are then 6 - rule
        Log("basecap: the lobby's list of rules not found (%08x, %u rows, TEAM SNEAKING's 6 wanted): BASE CAPTURE not offered", kinds, rows);
        return;
    }
    for (int id = 1; id <= 32; ++id) if (BcMapOffers(id)) gBcMask |= 1u << (id - 1);
    const uint32_t base6 = R32(basesAt + 6 * 4), items = R32(k7 + 0x20), idx = R32(k7 + 0x28);
    gBcCaptureHelp = base6 + R32(idx + 8 * (rows - 1 - kBcAs) + 4);   // the rows hold the rules rows - 1 .. 0
    static uint32_t pairs = 0, langTables[5] = {}, langNames[5] = {};
    if (!pairs) pairs = Alloc(16 * 8);
    if (!pairs) { gBcMask = 0; return; }
    W32(pairs, kBcStrBase - base6); W32(pairs + 4, kBcStrBase + 2 - base6);
    for (uint32_t k = 0; k < 15; ++k) { W32(pairs + 8 + 8 * k, R32(idx + 8 * k)); W32(pairs + 12 + 8 * k, R32(idx + 8 * k + 4)); }
    W32(k7 + 0x28, pairs);
    W32(k7 + 0x1C, rows + 1);
    gBcRows = (int)rows + 1;
    int langsMoved = 0;                                // the rows' texts (North American, European): BC on top, the others a row down
    for (uint32_t k = 0; k < 5 && langs >= 0x100000; ++k) {
        const uint32_t old = R32(langs + 7 * 0x14 + 4 * k);   // TSNE's table (the platform's heap)
        if (!BcPs2(old, 64) || R32(old) != R32(items + 8)) continue;
        if (!langTables[k]) langTables[k] = Alloc(8 * 8);
        if (!langNames[k]) langNames[k] = Alloc((uint32_t)strlen(kBcLobbyNames[k]) + 1);
        const uint32_t t = langTables[k], name = langNames[k];
        if (!t || !name) break;
        memcpy(gMem + name, kBcLobbyNames[k], strlen(kBcLobbyNames[k]) + 1);
        W32(t, R32(items + 8)); W32(t + 4, name);
        for (uint32_t j = 0; j < rows; ++j) { W32(t + 8 + 8 * j, R32(items + 16 * (j + 1) + 8)); W32(t + 12 + 8 * j, R32(old + 8 * j + 4)); }
        W32(langs + 7 * 0x14 + 4 * k, t);
        ++langsMoved;
    }
    const uint32_t en = PageData(0x37B828, 9, 0x2E65C8);   // the rotation's rows: rule 6's entry two before each table
    const uint32_t rowTables[5] = {en, PageData(0x37B828, 11, 0x2E73B8), PageData(0x37B828, 12, 0x2E8F98),
                                   PageData(0x37B828, 13, 0x2E81A8), PageData(0x37B828, 10, 0x2E9D88)};
    if (!gBcRowEntries) gBcRowEntries = Alloc(5 * 8);
    gBcRowsLang0 = en ? en - 16 : 0;
    for (int k = 0; k < 5 && gBcRowEntries; ++k) {
        W32(gBcRowEntries + 8 * k, rowTables[k] ? R32(rowTables[k]) : 0); W32(gBcRowEntries + 8 * k + 4, langNames[k]);
    }
    Log("basecap: the lobby offers BASE CAPTURE (the list of rules: %d rows, %d languages' rows moved), its maps %#x", gBcRows, langsMoved, gBcMask);
    BcCjkAdd();                                        // the JP/KR texts' glyphs (TEAM SNEAKING's copy), when it has one
}
// [27b5ee] again on the same page (proc13(4) runs proc11: NewOLMLoad after Load game settings, US 0x34BF04) meets
// the list as TsneLobby and BcLobby left it (7 rows): both refused it, their hooks went off and the moved rows gave
// the page's rules 4 - row (BC's row SNEAKING, TSNE's CAPTURE). The words they wrote are put back first.
struct RulesListWords { int n; uint32_t at[7], stock[7], ours[7]; };
RulesListWords gRulesList = {};
int RulesListAt(uint32_t at[7]) {                      // kind 7's rows and pairs, the five languages' row tables
    const uint32_t kinds = PageData(0x3751FC, 0, 0x2DB958), langs = PageData(0x3751FC, 6, 0x6142F8);
    if (!BcPs2(kinds + 7 * 0x2C, 0x2C)) return 0;
    int n = 0;
    at[n++] = kinds + 7 * 0x2C + 0x1C; at[n++] = kinds + 7 * 0x2C + 0x28;
    for (uint32_t k = 0; k < 5 && BcPs2(langs + 7 * 0x14, 0x14); ++k) at[n++] = langs + 7 * 0x14 + 4 * k;
    return n;
}
void RulesListRestore() {                              // before TsneLobby: a word still as the set-ups left it -> stock
    RulesListWords& w = gRulesList;
    uint32_t at[7] = {};
    const int n = RulesListAt(at);
    int put = 0;
    for (int i = 0; i < n; ++i) {
        const uint32_t v = R32(at[i]);
        if (n == w.n && at[i] == w.at[i] && v == w.ours[i] && v != w.stock[i]) { W32(at[i], w.stock[i]); ++put; }
        else w.stock[i] = v;                           // the page's own (a page loaded again)
        w.at[i] = at[i]; w.ours[i] = w.stock[i];
    }
    w.n = n;
    if (put) Log("lobby: the list of rules set up again on the same page: %d words put back first (%u rows)", put, n ? R32(at[0]) : 0);
}
void RulesListKeep() {                                 // after BcLobby: the rows always the ones the hooks serve
    RulesListWords& w = gRulesList;
    if (w.n < 2) return;
    const uint32_t rows = R32(w.at[0]), served = gBcMask ? (uint32_t)gBcRows : gTsneMask ? 6 : 5;
    bool wrote = false;
    for (int i = 0; i < w.n; ++i) wrote |= R32(w.at[i]) != w.stock[i];
    if (wrote && rows != served) {
        for (int i = 0; i < w.n; ++i) W32(w.at[i], w.stock[i]);
        gTsneMask = gBcMask = 0;
        Log("lobby: the list of rules has %u rows, its hooks serve %u: the page's own list put back (TEAM SNEAKING, BASE CAPTURE not offered)", rows, served);
    }
    for (int i = 0; i < w.n; ++i) w.ours[i] = R32(w.at[i]);
}
std::string RulesRowName(uint32_t row) {               // a row's name in the English rows' table ("": none)
    const uint32_t langs = PageData(0x3751FC, 6, 0x6142F8);
    const uint32_t t = BcPs2(langs + 7 * 0x14) ? R32(langs + 7 * 0x14) : 0;
    uint32_t s = row < 8 && BcPs2(t + 8 * row, 8) ? R32(t + 8 * row + 4) : 0;
    std::string n;                                     // (TSNE's and BC's names are in the platform's heap)
    for (; BcPs2(s, 1) && gMem[s] && n.size() < 40; ++s) n += (char)gMem[s];
    return n;
}
int BcMaskCount() { int n = 0; for (int b = 0; b < 32; ++b) n += (gBcMask >> b) & 1; return n; }
// the host list (the lobby server's 0x4302 entries, 52 bytes: +0x17 the round's rule, +0x1C the options' second byte,
// relayed from the create-game message): a row of a game whose mark (BcListMark) is set, its round CAPTURE, named BC
bool gBcRowMark = false;                               // a host list row of a BC game is drawn (0x37A900)
uint32_t gBcShortEntries = 0;                          // BC's short name {key, text} in the five languages' tables
// the lobby's hooks (ntmenu2 loaded), before TSNE's; true when served
bool BcLobbyCall(Ctx& c, uint32_t addr, uint32_t va) {
    if (!gBcMask) return false;
    const uint32_t a0 = (uint32_t)c.r[4], a1 = (uint32_t)c.r[5];
    const uint32_t top = (uint32_t)gBcRows - 1;          // the top row's rule (6)
    switch (addr) {
    case 0x37A900: {                                   // a host list row (a1 its entry's name: entry + 4; t0 its round's rule)
        const uint32_t e = a1 - 4;
        if (e < 0x100000 || e + 0x34 >= 0x02000000 || !(R8(e + 0x1C) & 0x40) || (c.r[8] & 0xFF) != kBcAs) return false;
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint64_t ra = c.r[31];
        gBcRowMark = true;
        f(c);
        gBcRowMark = false;
        c.r[31] = ra;
        if (Once(((uint64_t)160 << 40) | R32(e))) {
            Log("basecap: the host list's game %u (%s) shown as BASE CAPTURE (its options' mark)", R32(e), TsneCString(e + 4).c_str());
            BcPngAsk("hostlist", 50);                    // tests
        }
        return true;
    }
    case 0x3605C0:                                     // a rule's row (tests: the list's pictures)
        if ((a0 & 0xFFFF) <= top) {
            if (!(gBc.pngDone & (1u << 21))) { BcPngAsk("list", 40); BcPngAsk("list", 250); BcPngAsk("list", 560, 1u << 21); }
            c.r[2] = top - (a0 & 0xFFFF); return true;
        }
        return false;
    case 0x360610: if ((a0 & 0xFFFF) <= top) { c.r[2] = top - (a0 & 0xFFFF); return true; } return false;   // a row's rule
    case 0x349AD0: if ((a0 & 0xFFFF) == kBcRule) { c.r[2] = gBcMask; return true; } return false;
    case 0x349AF0: if ((a0 & 0xFFFF) == kBcRule) { c.r[2] = (uint32_t)BcMaskCount(); return true; } return false;
    case 0x349B40:
        if ((a0 & 0xFFFF) == kBcRule) {
            uint32_t n = a1 & 0xFFFF, v = 0xFFFF;
            for (uint32_t b = 0; b < 32; ++b) if ((gBcMask >> b) & 1) { if (!n) { v = b; break; } --n; }
            c.r[2] = v; return true;
        }
        return false;
    case 0x349BA0:
        if ((a0 & 0xFFFF) == kBcRule) {
            const uint32_t m = a1 & 0xFFFF;
            uint32_t v = 0xFFFF;
            if (m < 32 && ((gBcMask >> m) & 1)) { v = 0; for (uint32_t b = 0; b < m; ++b) v += (gBcMask >> b) & 1; }
            c.r[2] = v; return true;
        }
        return false;
    case 0x349AA0: {                                   // the rows' rule name in the Japanese and Korean programs
        const uint32_t ra = (uint32_t)c.r[31];
        if (a0 == 6 && (a1 & 0xFFFF) == kBcRule && (ra == V(0x37B948) || (!gUsProgram && ps2::UsFnOf(ra) == 0x37B828))) {
            c.r[2] = kBcStrBase; return true;
        }
        if (gBcRowMark && a0 == 6 && (a1 & 0xFFFF) == kBcAs + 5) { c.r[2] = kBcStrBase + 1; return true; }   // CAP: BC
        return false;
    }
    case 0x373DA0:                                     // a row's rule name in the five languages (rule 6: two before the tables)
        if (gBcRowEntries && gBcRowsLang0 && (uint32_t)c.r[6] == gBcRowsLang0) {
            c.r[6] = gBcRowEntries; c.r[7] = gBcRowEntries + 8; c.r[8] = gBcRowEntries + 16; c.r[9] = gBcRowEntries + 24;
            c.r[10] = gBcRowEntries + 32;
        }
        if (gBcRowMark && (uint32_t)c.r[31] == V(0x37AF58)) {   // a marked host list row's rule column (US 0x37AF50): BASE
            if (!gBcShortEntries) gBcShortEntries = Alloc(5 * 8);
            const uint32_t name = BcLobbyString(1);
            for (int k = 0; k < 5 && gBcShortEntries && name; ++k) {
                const uint32_t e = (uint32_t)c.r[6 + k];
                W32(gBcShortEntries + 8 * (uint32_t)k, BcPs2(e) ? R32(e) : 0);
                W32(gBcShortEntries + 8 * (uint32_t)k + 4, name);
                c.r[6 + k] = gBcShortEntries + 8 * (uint32_t)k;
            }
        }
        return false;
    case 0x125408:                                     // its strings
        if (a0 == kBcStrBase + 2 && !strcmp(TsneVersion(), "KR") && gBcCaptureHelp) {   // the Korean help: BASE and
            static uint32_t at = 0;                                                         // CAPTURE's own (탈취 미션)
            ps2::Fn f = ps2::FindFn(V(0x125408));
            if (!at && f) { Ctx t = c; t.r[4] = gBcCaptureHelp; f(t); at = BcTextAt("BASE " + TsneCString((uint32_t)t.r[2] & 0x01FFFFFF)); }
            if (at) { c.r[2] = at; c.r[3] = 0; return true; }
        }
        if (a0 >= kBcStrBase && a0 < kBcStrBase + 3) {
            const int kind = (int)(a0 - kBcStrBase);
            c.r[2] = BcLobbyString(kind, kind == 2 ? TsneLobbyLanguage(c) : 0); c.r[3] = 0;
            return c.r[2] != 0;
        }
        return false;
    }
    return false;
}
// Create Game 2's list of rules (US 0x360610: the chosen row's rule, its one caller 0x361504 commits it; 0x3605C0: a
// rule's row, where the list opens): BC's, TSNE's or the page's own answer, logged
bool LobbyRulesRow(Ctx& c, uint32_t addr, uint32_t va) {
    const uint32_t a0 = (uint32_t)c.r[4] & 0xFFFF;
    if (!BcLobbyCall(c, addr, va) && !TsneLobbyCall(c, addr, va)) {
        ps2::Fn f = ps2::FindFn(va);
        if (!f) return false;
        const uint64_t ra = c.r[31];
        f(c);
        c.r[31] = ra;
    }
    const uint32_t v = (uint32_t)c.r[2] & 0xFFFF;
    const std::string shown = RulesRowName(addr == 0x360610 ? a0 : v);
    if (addr == 0x360610) Log("lobby: Create Game 2: the list of rules' row %u (%s) chosen: %s (rule %u)", a0, shown.empty() ? "-" : shown.c_str(), RuleName(v), v);
    else Log("lobby: Create Game 2: the list of rules at row %u (%s), the entry's %s (rule %u)", v, shown.empty() ? "-" : shown.c_str(), RuleName(a0), a0);
    return true;
}
// Create Game's check of the settings the lobby server gave back (US 0x35EE70, once they are copied into its work:
// rules +0x284, maps +0x294) makes every rule of 5 and up DEATHMATCH: the last game's TSNE and BC entries
// (LastSettingsRead's own rotation) came back as DEATHMATCH. They are kept while the lobby offers them
bool LobbySettingsCheck(Ctx& c, uint32_t va) {
    const uint32_t w = (uint32_t)c.r[4];
    ps2::Fn f = ps2::FindFn(va);
    if (!f || !BcPs2(w + 0x284, 0x20)) return false;
    uint8_t keep[16];
    for (uint32_t i = 0; i < 16; ++i) {
        const uint8_t r = R8(w + 0x284 + i);
        keep[i] = (r == kTsneRule && gTsneMask) || (r == kBcRule && gBcMask) ? r : 0;
    }
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    int kept = 0;
    for (uint32_t i = 0; i < 16; ++i) if (keep[i] && R8(w + 0x294 + i)) { W8(w + 0x284 + i, keep[i]); ++kept; }
    if (kept) Log("lobby: Create Game's loaded settings checked: %d entries of TEAM SNEAKING or BASE CAPTURE kept (the check makes them DEATHMATCH)", kept);
    return true;
}

// tests: the bases (x y z each) and this player's team; the count
int BcTestBases(float out[kBcMax][3]) {
    gBcHarness = true;
    if (!BcRound() || !gBc.n) return 0;
    for (int i = 0; i < gBc.n; ++i) { out[i][0] = gBc.b[i].x; out[i][1] = gBc.b[i].y; out[i][2] = gBc.b[i].z; }
    return gBc.n;
}
uint32_t BcTestRound() { gBcHarness = true; return BcRound() && gBc.n ? gBc.round : 0; }   // tests: this round's number (0: none)
float BcTestTimeLeft(int* result) {                    // tests: the round's time left in seconds (300 units a second), its result
    gBcHarness = true;
    const uint32_t r = BcRound() && gBc.n ? BcRecord() : 0;
    if (result) *result = r ? (int)R8(r + 4) : 0;
    return r ? (float)(int32_t)R32(r) / 300.0f : -1.0f;
}

// the HUD's texts drawn once a tick's pass ran its actors (the pass that turns DG's phase from 1 back to 0; every pass
// at the full rate): DG's font is begun at the tick and its packets swap at each picture, so a text drawn at a pass's
// start missed both pictures at 60 (bcP2); ps2world.cpp's pass hook (0x122768) lets this call the pass; true when it did
bool BcPassAfter(Ctx& c, uint32_t va) {
    if ((BcDrawMode() != 4 && BcDrawMode() != 5) || !BcRound() || !gBc.n || !TsneMapPage()) return false;
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    const uint32_t before = R32(kHalfPhase);
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    if (((before == 1 && R32(kHalfPhase) == 0) || !R32(kHalfRate)) && BcObjectsAlive() && TsneHud() && BcRunning()) {
        const int self = OwnSlot();
        const uint32_t me = self >= 0 ? PlayerOfSlot(self) : 0;
        const int screen = BcMenuScreen();
        if (me && (int16_t)R16(me + 0xB64) > 0 && (screen == 0 || screen == 6)) { gBcAfterTick = true; BcHudTexts(c, me); gBcAfterTick = false; }
    }
    return true;
}

// ---- the PC-only dialect: a game whose rotation holds BC is played in TEAM SNEAKING's set (TsneSet: the PCs' or the
// extra maps' set with kTsneMark mixed in), so no PS2, PCSX2 or older MGO1.exe gets in
bool BcInRotation(uint32_t rules, uint32_t maps) {      // the Create Game work's rotation (+0x284 rules, +0x294 maps)
    for (int i = 0; i < 16; ++i) { if (!R8(maps + i)) break; if (R8(rules + i) == kBcRule) return true; }
    return false;
}

// ---- the host's states to the other PCs (vnet type 6, gVn.mu held): every 250 ms, at once after a change
namespace vnet {
void BasesOut(unsigned long long now) {
    static unsigned long long sentAt = 0;
    static std::vector<uint8_t> last;
    if (!IsSessionHost() || !BcRound() || !gBc.n) return;
    const std::vector<uint8_t> st = BcStateBytes();
    if (st == last && now - sentAt < 250) return;
    last = st; sentAt = now;
    std::vector<uint8_t> d = Head(kBases, 1);
    d.insert(d.end(), st.begin(), st.end());
    Seal(d);
    for (auto& p : gVn.peers) if (p.pc && now - p.seen < 5000) SendTo(d, p);
}
void BasesIn(Peer* p, const uint8_t* d, size_t n) {    // a joiner: the host's (applied on the game's thread, BcApply)
    (void)p;
    if (IsSessionHost() || n < kHead + 5 + 2 || n - kHead - 2 > sizeof gBcInbox) return;
    std::lock_guard<std::mutex> l(gBcInboxMu);
    memcpy(gBcInbox, d + kHead, n - kHead - 2);
    gBcInboxLen = n - kHead - 2;
}
}  // namespace vnet
// the host list's mark of a rotation holding BC: bit 6 of the create-game message's second option byte (settings
// + 0x2AA; TEAM SNEAKING's is bit 7), relayed by the lobby server in the host list (a 0x4302 entry's + 0x1C)
uint8_t BcListMark(const uint8_t* rules, const uint8_t* maps) {
    for (int i = 0; i < 16; ++i) { if (!maps[i]) break; if (rules[i] == kBcRule) return 0x40; }
    return 0;
}
