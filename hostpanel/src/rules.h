// The game's rules and maps by the lobby's numbers (a rotation's "rule:map" pairs): the one table the
// program takes them from (the GAME page's rotation and RULE SETTINGS, the PLAYERS page, the Game Report,
// the logs). A rule or a map more: a row in rules.cpp, its name's string (i18n/strings*.py) and the same
// row in host/mgo1host-agent (RULES, MAPS).
#pragma once
#include <string>
#include <utility>
#include <vector>

// a rule's settings in the settings file: "<prefix>_minutes", "<prefix>_rounds", "<prefix>_tickets"
enum { RV_MINUTES, RV_ROUNDS, RV_TICKETS, RV_COUNT };

struct RuleValueKind {
    const char* key;                 // the settings key's end: "rounds" (tdm_rounds)
    const char* row;                 // the GAME page row's id after the prefix: "r" (tdmr)
    const char* label;               // the row's label ("g_rounds": "%s: ROUNDS")
    const char* help;                // the row's help
};
extern const RuleValueKind kRuleValueKinds[RV_COUNT];

struct RuleValue { int lo = 0, hi = 0, def = 0; };       // hi 0: the rule has no such setting

struct GameRule {
    int id;                          // the lobby's rule number
    const char* prefix;              // its settings' keys: "tdm" (tdm_minutes, tdm_rounds, tdm_tickets)
    const char* name;                // its name's string key
    bool teams;                      // played in two teams (deathmatch: each for himself)
    RuleValue values[RV_COUNT];      // its RULE SETTINGS rows: minutes, rounds, tickets
    std::vector<int> maps;           // the maps it is played on, in the editor's order (empty: every map)
};

struct GameMap {
    int id;                          // the lobby's map number
    const char* page;                // its stage page
    const char* name;                // its name as the game shows it
    bool pcOnly;                     // only the PC players have it
};

const std::vector<GameRule>& GameRules();
const std::vector<GameMap>& GameMaps();
const GameRule* FindRule(int id);
const GameMap* FindMap(int id);
const GameMap* FindMapByPage(const std::string& page);
std::vector<int> RuleMaps(int rule);                 // the maps a rule is played on, in order
bool RulePlays(int rule, int map);                   // a pair the game plays (both known, the map the rule's)
std::pair<int, int> FirstPair();                     // the first rule on its first map
int NextRule(int rule, int step);                    // the rotation editor: the next (step 1) or previous (-1) rule
int NextMap(int rule, int map, int step);            // the next or previous map of a rule
std::string RuleKey(const GameRule& r, int kind);    // a rule setting's key: "tdm_rounds"
std::string RuleName(int r);                         // in the program's language ("-": not in the table)
const char* MapName(int m);                          // as the game shows it ("-": not in the table)
