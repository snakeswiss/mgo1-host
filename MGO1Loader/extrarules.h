// The rules' play on the extra maps and their mission objects (docs/EXTRA_MAPS.md, The points of RESCUE and
// SNEAKING): included by ps2world.cpp after tsne.h.

// the mission objects' actor (NewOnlineItemChara2, class bf76c9; US n003a: +0x410 their count, each 0x120 from
// +0xB0: +0x90 x y z, +0xD0 its state, +0xD4 its holder)
uint32_t MissionObjects() {
    for (const auto& a : gNativeActors)
        if (a.second == "NewOnlineItemChara2" && a.first >= 0x100000 && a.first + 0x8C0 < 0x02000000) return a.first;
    return 0;
}

// ---- #120 (docs/CHARACTERS.md, The added weapons: Snake's class): in a SNEAKING round of an extra map, Snake (the
// player of team 1, [player + 0x920]: the script's spawn gives team 1 Snake's starts, list 1886) holds the added
// weapons whose weapons.ini says snake= in place of their bases. He has no weapons' choice before a spawn: AddedOf
// answers them for his slot on every program (each knows his team), so his models, stance, icons, names and sounds
int gSnakeSlot = -1;                                   // this round's Snake (-1: none)
int PlayerTeam(uint32_t pl) {                          // a player's team: [player + 0x920] -> +1 (as tsne.h TsneTeamOf)
    const uint32_t pt = pl ? R32(pl + 0x920) : 0;
    return pt >= 0x100000 && pt < 0x02000000 ? (int)R8(pt + 1) : -1;
}
const AddedWeapon* SnakeWeaponOf(int slot, int base) {
    if (slot < 0 || slot != gSnakeSlot) return nullptr;
    for (const auto& w : ExtraMapsWeapons())
        if (w.snake && w.base == base) return &w;
    return nullptr;
}
void SnakeTick() {
    int snake = -1;
    if (RoundRule() == 4 && !ExtraMapsWeapons().empty())
        for (int s = 0; s < 16 && snake < 0; ++s)
            if (const uint32_t p = PlayerOfSlot(s); p && PlayerTeam(p) == 1) snake = s;
    if (snake == gSnakeSlot) return;
    gSnakeSlot = snake;
    if (snake < 0) return;
    std::string held;
    for (const auto& w : ExtraMapsWeapons())
        if (w.snake) {
            char b[40];
            snprintf(b, sizeof b, " for weapon %#x", w.base);
            held += (held.empty() ? "" : ", ") + w.name + b;
        }
    Log("extra rules: player %d is Snake (SNEAKING): his weapons shown as %s", snake + 1, held.c_str());
}

// ---- tests (SIM_VISIT=hash,...): those points of the stage as the game finds them (0x161200: the geom's tree with
// points.txt's), for the autopilot's =visit (the local player put on each in turn: its floor seen).
// "m13=hash,...;m14=hash,...": a list per map (the others: none); the list made again at each map, gVisitGen counts it
struct VisitPt { uint32_t code; float x, y, z; };
std::vector<VisitPt> gVisit;
int gVisitMap = -1, gVisitGen = 0;
void VisitTick(Ctx& c) {
    static const char* all = getenv("SIM_VISIT");
    if (!all) return;
    const int map = RoundMap(), self = OwnSlot();
    if (map == gVisitMap || map != gLoadedMap || self < 0 || !PlayerOfSlot(self)) return;   // the round's map's stage loaded
    std::string mine = all;                             // this map's part
    if (strstr(all, "m") == all) {
        mine.clear();
        char key[16]; snprintf(key, sizeof key, "m%d=", map);
        for (const char* q = all; q && *q; q = strchr(q, ';') ? strchr(q, ';') + 1 : nullptr)
            if (!strncmp(q, key, strlen(key))) { const char* e = strchr(q, ';'); mine.assign(q + strlen(key), e ? (size_t)(e - q) - strlen(key) : strlen(q + strlen(key))); }
    }
    const char* want = mine.c_str();
    std::vector<VisitPt> v;
    for (const char* q = want; *q;) {
        float x, y, z;
        const char* end = strchr(q, ',');
        if (std::find(q, end ? end : q + strlen(q), ':') != (end ? end : q + strlen(q)) && sscanf(q, "%f:%f:%f", &x, &y, &z) == 3) {
            v.push_back({0, x, y, z});                 // a place x:y:z (a candidate)
        } else {
            const uint32_t h = (uint32_t)strtoul(q, nullptr, 16) & 0xFFFFFF;
            const TsnePoint p = TsneLookup(c, h);
            if (p.ok) v.push_back({h, p.x, p.y, p.z});
            else Log("SIM_VISIT: point %06x not in map %d", h, map);
        }
        q = strchr(q, ',');
        if (!q) break;
        ++q;
    }
    gVisit = v;
    gVisitMap = map;
    ++gVisitGen;
    Log("SIM_VISIT: %zu points found in map %d", v.size(), map);
}

void ExtraRulesTick(Ctx& c) {                          // a pass of the actor lists (ps2world.cpp, 0x122768)
    SnakeTick();
    if (gSnakeSlot >= 0 && gSnakeSlot == OwnSlot() && gFrameNo % 60 == 0 && N3HudIcons()) AddedIcons(c);   // his weapons' icons
    VisitTick(c);
}
