#include "rules.h"
#include "i18n.h"

const RuleValueKind kRuleValueKinds[RV_COUNT] = {
    {"minutes", "t", "g_time", "h_g_time"}, {"rounds", "r", "g_rounds", "h_g_rounds"}, {"tickets", "k", "g_tickets", "h_g_tickets"}};

// (function statics: the App's GameSettings read them while the program's globals are made)
const std::vector<GameRule>& GameRules() {
    // id, settings' prefix, name, teams, {minutes, rounds, tickets: lowest, highest, default}, maps (empty: every map)
    static const std::vector<GameRule> rules = {
        {0, "dm", "rule_0", false, {{1, 99, 5}, {}, {}}, {}},
        {1, "tdm", "rule_1", true, {{1, 99, 5}, {1, 99, 2}, {1, 99, 30}}, {}},
        {2, "res", "rule_2", true, {{1, 99, 4}, {1, 99, 7}, {}}, {}},
        {3, "cap", "rule_3", true, {{1, 99, 4}, {1, 99, 5}, {}}, {}},
        {4, "sne", "rule_4", true, {{1, 99, 4}, {1, 99, 7}, {}}, {}},
        // (v141, games between PCs only) TEAM SNEAKING plays with RESCUE's minutes and rounds, BASE CAPTURE with
        // CAPTURE's (the game reads them so): no rows of their own
        {5, "tsne", "rule_5", true, {{}, {}, {}}, {}},
        {6, "bc", "rule_6", true, {{}, {}, {}}, {}},
    };
    return rules;
}

const std::vector<GameMap>& GameMaps() {
    // id, stage page, name, PC players only
    static const std::vector<GameMap> maps = {
        {1, "n001a", "LOST FOREST", false},       {2, "n002a", "GHOST FACTORY", false},     {3, "n003a", "CITY UNDER SIEGE", false},
        {4, "n011a", "KILLHOUSE A", false},       {5, "n012a", "KILLHOUSE B", false},       {6, "n013a", "KILLHOUSE C", false},
        {7, "n021a", "SVYATOGORNYJ EAST", false}, {8, "n022a", "MOUNTAINTOP", false},       {9, "n024a", "GRANINY GORKI LAB", false},
        {10, "n031a", "PILLBOX PURGATORY", false}, {11, "n032a", "HIGH ICE", false},         {12, "n033a", "BROWN TOWN", false},
        // (v141) the PC maps (docs/EXTRA_MAPS.md: built on a stage of the twelve, its page's name)
        {13, "n003a", "SILO ENTRANCE", true},     {14, "n003a", "REX HANGAR", true},        {15, "n022a", "BIG SHELL STRUT E", true},
    };
    return maps;
}

const GameRule* FindRule(int id) {
    for (const GameRule& r : GameRules()) if (r.id == id) return &r;
    return nullptr;
}

const GameMap* FindMap(int id) {
    for (const GameMap& m : GameMaps()) if (m.id == id) return &m;
    return nullptr;
}

const GameMap* FindMapByPage(const std::string& page) {
    for (const GameMap& m : GameMaps()) if (page == m.page) return &m;
    return nullptr;
}

std::vector<int> RuleMaps(int rule) {
    std::vector<int> v;
    const GameRule* r = FindRule(rule);
    if (!r) return v;
    if (r->maps.empty()) { for (const GameMap& m : GameMaps()) v.push_back(m.id); }
    else for (int id : r->maps) if (FindMap(id)) v.push_back(id);
    return v;
}

bool RulePlays(int rule, int map) {
    for (int m : RuleMaps(rule)) if (m == map) return true;
    return false;
}

std::pair<int, int> FirstPair() {
    const int r = GameRules().front().id;
    const std::vector<int> ms = RuleMaps(r);
    return {r, ms.empty() ? GameMaps().front().id : ms.front()};
}

static int Step(int at, int step, int n) { return ((at + step) % n + n) % n; }

int NextRule(int rule, int step) {
    const std::vector<GameRule>& rs = GameRules();
    int at = 0;
    for (size_t i = 0; i < rs.size(); ++i) if (rs[i].id == rule) at = (int)i;
    return rs[Step(at, step, (int)rs.size())].id;
}

int NextMap(int rule, int map, int step) {
    const std::vector<int> ms = RuleMaps(rule);
    if (ms.empty()) return map;
    int at = 0;
    for (size_t i = 0; i < ms.size(); ++i) if (ms[i] == map) at = (int)i;
    return ms[Step(at, step, (int)ms.size())];
}

std::string RuleKey(const GameRule& r, int kind) { return std::string(r.prefix) + "_" + kRuleValueKinds[kind].key; }

std::string RuleName(int r) {
    const GameRule* x = FindRule(r);
    return x ? T(x->name) : "-";
}

const char* MapName(int m) {
    const GameMap* x = FindMap(m);
    return x ? x->name : "-";
}
