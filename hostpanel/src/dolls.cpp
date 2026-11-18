// The PLAYABLE DOLL page: the players the server runs in its bots' games (host/mgo1doll.sh, one
// service each, started and stopped by mgo1doll-manager): what each one does now, its orders and
// settings (changed live), each bot's dolls with no account (DOLL_1, DOLL_2 ...: one more, one less; (v141 beta 5) their
// names in the game, given at their creation or changed later: RENAME),
// a new one with a SaveMGO account of its own (kept encrypted on this PC like the bots' accounts, sent
// to the server over SSH only, never shown), the seat kept for real players. Over mgo1host-agent:
// dolls, doll-count, doll-set, doll-order, doll-remove, doll-keep-seat.
#include "app.h"
#include "i18n.h"
#include "platform.h"
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ui;

namespace {
std::string Upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
int Cycle(int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; }
// (v141 beta 6) the ladder, MGO1Loader/doll_brain.cpp's kTierNames: 1 TRAINING .. 10 PRO (beta 5's 6 NIGHTMARE is ELITE)
const char* const kTierKeys[10] = {"v_tier_1", "v_tier_2", "v_tier_3", "v_tier_4", "v_tier_5", "v_tier_6", "v_tier_7", "v_tier_8", "v_tier_9", "v_tier_10"};
const char* const kTierNames[10] = {"TRAINING", "EASY", "NORMAL", "HARD", "EXPERT", "ELITE", "VETERAN", "MASTER", "EXTREME", "PRO"};
int TierClamp(int t) { return t < 1 ? 1 : t > 10 ? 10 : t; }
std::string SkillText(int d) { return DollSkillText(d); }
// a live level with its tier ("7.3 VETERAN"): the status's tier, else the level's own
std::string LiveText(float level, const std::string& tier) {
    const int t = DollTierOf(tier);
    return Dec(level, 1) + " " + DollTierText(t ? t : (int)std::lround(level));
}
// (v141 beta 6) why the live level moves (status.json live.reason)
const char* const kWhy[][2] = {
    {"on_target", "v_why_on_target"}, {"low_confidence", "v_why_low_confidence"}, {"enemy_team_outperforming", "v_why_enemy_team_outperforming"},
    {"ally_team_dominating", "v_why_ally_team_dominating"}, {"its_side_behind", "v_why_its_side_behind"}, {"its_side_ahead", "v_why_its_side_ahead"},
    {"enemy_stronger", "v_why_enemy_stronger"}, {"enemy_weaker", "v_why_enemy_weaker"}, {"fixed", "v_why_fixed"}, {"even", "v_why_even"}};
std::string WhyText(const std::string& code) { return DollWhyText(code); }
int NumOf(const Json& j, int def) { return j.kind == Json::Str && !j.s.empty() ? atoi(j.s.c_str()) : j.Int(def); }
bool AccountCharsOk(const char* s) { for (const char* p = s; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') return false; return true; }
// (v141 beta 5) an accountless doll's name in the game (mgo1host-agent's DOLL_NAME_RE): 3 to 15 letters, digits, spaces (not
// first, last or two together) and . _ - ! ? + *; DOLL_<number> only as its own (doll n's: its default name again)
bool DollNameChar(char c, bool inner) { return isalnum((unsigned char)c) || (c && strchr("._!?+*-", c)) || (inner && c == ' '); }
std::string Trimmed(const char* s) {
    std::string t = s;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    size_t a = 0;
    while (a < t.size() && t[a] == ' ') ++a;
    return t.substr(a);
}
const char* const kTeams[] = {"auto", "red", "blue", "spectator"};
const char* const kTeamLabels[] = {"v_team_auto", "v_team_red", "v_team_blue", "v_team_spect"};
const char* const kOrders[] = {"free", "objective", "hide", "defend", "follow", "target"};
const char* const kOrderLabels[] = {"do_free", "do_objective", "do_hide", "do_defend", "do_follow0", "do_target0"};
const char* const kLoadouts[] = {"auto", "scorpion", "xm16", "ak47", "m37", "svd", "mosin"};
const char* const kLoadoutNames[] = {nullptr, "SCORPION", "XM16E1", "AK-47", "M37", "SVD", "MOSIN NAGANT"};
int IndexOf(const char* const* list, int n, const std::string& v) { for (int i = 0; i < n; ++i) if (v == list[i]) return i; return 0; }
std::string LoadoutLabel(int i) { return i <= 0 ? std::string(T("dw_auto")) : std::string(kLoadoutNames[i]); }
// an order as the doll keeps it ("follow NAME", "target NAME", "hide" ...), in the program's language
std::string OrderLabel(const std::string& order) {
    const size_t sp = order.find(' ');
    const std::string k = order.substr(0, sp), who = sp == std::string::npos ? "" : order.substr(sp + 1);
    if ((k == "follow" || k == "target") && !who.empty()) return TF(k == "follow" ? "do_follow" : "do_target", who.c_str());
    return T(kOrderLabels[IndexOf(kOrders, 6, k)]);
}
// what the doll does, in the program's language ("da_<activity>", its argument a name)
std::string ActivityText(const DollInfo& d) { return DollActivityText(d.act, d.arg); }
}  // namespace

bool DollNameOk(const std::string& s, int n) {
    if (s.size() < 3 || s.size() > 15 || s.find("  ") != std::string::npos) return false;
    for (size_t i = 0; i < s.size(); ++i) if (!DollNameChar(s[i], i > 0 && i + 1 < s.size())) return false;
    const std::string u = Upper(s);
    if (u.size() > 5 && u.compare(0, 5, "DOLL_") == 0 && u.find_first_not_of("0123456789", 5) == std::string::npos) return n > 0 && u == "DOLL_" + std::to_string(n);
    return true;
}

std::string DollActivityText(const std::string& act, const std::string& arg) {
    if (act.empty()) return T("da_starting");
    if (act == "@longest") {                                          // (the self-test: the widest activity of the language)
        std::string best; float bw = -1.f;
        for (const std::string& k : KeysWithPrefix("da_")) {
            const char* t = T(k.c_str());
            const std::string text = strstr(t, "%s") ? TF(k.c_str(), arg.c_str()) : std::string(t);
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            if (w > bw) { bw = w; best = text; }
        }
        return best;
    }
    const std::string key = "da_" + act;
    const char* t = T(key.c_str());
    if (!strcmp(t, key.c_str())) return act + (arg.empty() ? "" : " " + arg);   // an activity this program does not know yet
    return strstr(t, "%s") ? TF(key.c_str(), arg.c_str()) : std::string(t);
}
std::string DollOrderText(const std::string& order) { return OrderLabel(order); }
std::string DollTierText(int tier) { return T(kTierKeys[TierClamp(tier) - 1]); }
std::string DollSkillText(int difficulty) { return std::to_string(TierClamp(difficulty)) + " " + DollTierText(difficulty); }
int DollTierOf(const std::string& name) { for (int i = 0; i < 10; ++i) if (name == kTierNames[i]) return i + 1; return 0; }
std::string DollWhyText(const std::string& code) { for (const auto& w : kWhy) if (code == w[0]) return T(w[1]); return code; }

// (the self-test) the language's widest tier on the dolls: their rows, the menus and SBMM BOTS show the longest texts
void App::DemoTiers() {
    if (!demo_ || showcase_ || demoTierLang_ == CurLang()) return;   // (build 1.00: the showcase keeps its levels)
    demoTierLang_ = CurLang();
    int wide = 1;
    float best = -1;
    for (int t = 1; t <= 10; ++t) {
        const float w = SpacedWidth(F.row, F.row->FontSize, DollSkillText(t).c_str(), RowSpacing()) +
                        SpacedWidth(F.body, F.body->FontSize, TF("v_doll_level_now", LiveText(t < 10 ? t + 0.3f : 9.7f, "").c_str()).c_str(), 0);
        if (w > best) { best = w; wide = t; }
    }
    for (auto& d : dolls_) {
        if (d.bot == 2) continue;                         // (bot 2's dolls keep theirs: MIXED)
        d.difficulty = wide; d.adaptMin = d.adaptMax = wide; d.level = wide < 10 ? wide + 0.3f : 9.7f; d.tier.clear();
    }
    ndSkill_ = wide;
}

void App::LoadDolls() {
    if (demo_) {
        if (!dollsLoaded_) {
            dollsLoaded_ = true;
            // the longest texts: 15-letter names, the longest activity, an order with a name
            DollInfo a; a.n = 1; a.bot = 1; a.difficulty = 4; a.account = "D***"; a.hasAccount = true; a.running = true; a.name = "PLAYER DOLL 1";
            a.level = 5.0f; a.levelWhy = "behind";
            a.boundsKnown = true; a.why = "enemy_team_outperforming"; a.enemy = 7.9f; a.ally = 5.8f; a.confidence = 0.81f; a.realPlayers = 1;
            a.presence = "active";
            a.act = "@longest"; a.arg = "WWWWWWWWWWWWWWW"; a.rule = "capture"; a.team_ = 0; a.life = 162; a.kills = 17; a.deaths = 12;
            a.alive = true; a.inMatch = true; a.age = 1; a.order = "follow WWWWWWWWWWWWWWW";
            a.players = {"WWWWWWWWWWWWWWW", "OCELOT", "PLAYER DOLL 1", "PLAYER DOLL 2"};
            DollInfo b = a; b.n = 2; b.difficulty = 2; b.name = "PLAYER DOLL 2"; b.act = "going_to_sniper_spot"; b.arg.clear(); b.team_ = 1;
            b.kills = 0; b.deaths = 0; b.order = "target WWWWWWWWWWWWWWW"; b.loadout = "svd"; b.team = "blue";
            b.aimDirect = true;                           // (3.3) doll 2 aims DIRECT: bot 1's DOLLS' AIM is MIXED
            DollInfo c = a; c.n = 3; c.bot = 2; c.running = false; c.name.clear(); c.act.clear(); c.order = "free";
            DollInfo e = c; e.n = 16; e.enabled = false; e.account = "W***"; e.difficulty = 2;
            DollInfo f = a; f.n = 10; f.bot = 10; f.name = "WWWWWWWWWWWWWWW"; f.act = "waiting_for_players"; f.arg.clear(); f.team_ = 2; f.order = "hide";
            f.inMatch = true; f.alive = false; f.kills = 999; f.deaths = 999;
            // two with no account: DOLL_<n>, one waiting as a spectator, one not started yet
            DollInfo g = f; g.n = 4; g.bot = 2; g.hasAccount = false; g.accountless = true; g.account.clear(); g.name = "DOLL_4"; g.order = "free";
            g.presence = "waiting_for_real"; g.realPlayers = 0;
            f.fromVdoll = true;                           // (3.2) bot 10's doll plays "as the virtual dolls"
            DollInfo h = c; h.n = 12; h.bot = 1; h.hasAccount = false; h.accountless = true; h.account.clear(); h.name = "DOLL_12";
            dolls_ = {a, b, c, g, f, h, e};
            dollsCap_ = 4;
            dollsAccountless_ = !demoAgentOld_;
            dollsVdoll_ = !demoAgentOld_;
            dollsAim_ = !demoAgentOld_;                   // (3.3: an older agent has no aim)
            dollsNames_ = !demoAgentOld_;
            dollsList_ = demoAgentOld_ ? "" : "host";
            dollsUnranked_ = demoAgentOld_ ? "" : "auto";
            dollsCount_ = {{1, 1}, {2, 1}, {3, 0}, {10, 0}};
            // (3.2) why two bots' dolls wait: no free seat (the longest numbers), the pace guard
            DollWait w1; w1.code = "seats"; w1.seats = 15; w1.real = 10; w1.vdolls = 4; w1.kept = 1; w1.dollsSet = 3; w1.dollsIn = 2;
            DollWait w2; w2.code = "pace_guard"; w2.paceCap = 10; w2.paceWaitS = 15 * 60; w2.dollsSet = 3; w2.dollsIn = 1;
            dollsWait_ = {{1, w1}, {2, w2}};
        }
        return;
    }
    dollsLoading_ = true;
    remote_.Agent("dolls", "", [this](int rc, const Json& j, const std::string&) {
        dollsLoading_ = false;
        dollsAt_ = ImGui::GetTime();
        if (rc != 0 || !j["ok"].Truthy()) { dollsError_ = ErrText(j); return; }
        dollsError_.clear();
        dollsLoaded_ = true;
        dollsKeepSeat_ = j["keep_seat"].Truthy();
        dollsList_ = j["list"].Text();                               // (agent 2.7: host, own, off; older: none)
        dollsUnranked_ = j["unranked"].Text();                       // (agent 3.1: auto, always, off; older: none)
        dollsMax_ = j["max"].Int(16);
        dollsCap_ = j["capacity"].Int(-1);
        dollsAccountless_ = j["accountless"].Truthy();                // (an agent before 2.5: false, the account dolls only)
        dollsNames_ = j["names"].Truthy();                            // (agent 2.9: the accountless dolls' names)
        // (3.2) the difficulty "as the virtual dolls": the server says it takes it ("vdoll_difficulty": true), or a doll's
        // "difficulty_from" shows it does; an older agent sends neither and the choice is not offered
        bool vdoll = j["vdoll_difficulty"].Truthy();
        dollsAim_ = j["doll_aim"].Truthy();                           // (3.3) agent 3.5 takes a doll's aim; an older one: AIM hidden
        dollsCount_.clear(); dollsMost_.clear(); dollsWait_.clear();
        for (const auto& [b, c] : j["bots"].o) {
            dollsCount_[atoi(b.c_str())] = c["accountless"].Int(); dollsMost_[atoi(b.c_str())] = c["most"].Int(-1);
            // (3.2) what the doll manager counts for the bot's dolls and why they wait (agent 3.4; a bot that is no longer
            // there and still has dolls is listed too)
            if (c["waiting_code"].IsNull()) continue;
            DollWait w;
            w.code = c["waiting_code"].Text(); w.why = c["waiting_why"].Text();
            w.seats = c["seats"].Int(); w.real = c["real"].Int(); w.vdolls = c["vdolls"].Int(); w.kept = c["kept"].Int();
            w.freeForDolls = c["free_for_dolls"].Int(); w.dollsSet = c["dolls_set"].Int(); w.dollsIn = c["dolls_in"].Int();
            w.paceCap = c["pace_cap"].Int(-1); w.paceWaitS = c["pace_wait_s"].Int();
            dollsWait_[atoi(b.c_str())] = w;
        }
        std::vector<DollInfo> v;
        const Json& ds = j["dolls"];
        for (size_t i = 0; i < ds.Size(); ++i) {
            const Json& x = ds[i];
            DollInfo d;
            d.n = x["doll"].Int(); d.bot = x["bot"].Int(1); d.difficulty = x["difficulty"].Int(3); d.age = x["age"].Int(-1);
            d.account = x["account"].Text(); d.team = x["team"].Text(); d.loadout = x["loadout"].Text(); d.order = x["order"].Text();
            if (d.team.empty()) d.team = "auto";
            if (d.loadout.empty()) d.loadout = "auto";
            if (d.order.empty()) d.order = "free";
            d.hasAccount = x["has_account"].Truthy(); d.enabled = x["enabled"].Truthy(); d.running = x["running"].Truthy();
            d.accountless = x["accountless"].Truthy();
            d.nameSet = x["name_set"].Truthy();
            d.adapt = x["adapt"].IsNull() || x["adapt"].Truthy();
            // (v141 beta 6) the live level's bounds (an older agent has none: 1 and 10, its rows left as they are)
            d.boundsKnown = !x["adapt_min"].IsNull();
            d.adaptMin = TierClamp(NumOf(x["adapt_min"], 1)); d.adaptMax = TierClamp(NumOf(x["adapt_max"], 10));
            if (d.adaptMin > d.adaptMax) std::swap(d.adaptMin, d.adaptMax);
            // (3.2) "difficulty_from": "vdoll" (its difficulty is its bot's virtual dolls' level, "difficulty" the number
            // the server resolved) or "set"; none from an older agent
            d.fromVdoll = x["difficulty_from"].Text() == "vdoll";
            d.aimDirect = x["aim"].Text() == "direct";               // (3.3) direct, else pad (an older agent: none, pad)
            if (!x["difficulty_from"].IsNull()) vdoll = true;
            for (size_t k = 0; k < x["players"].Size(); ++k) d.players.push_back(x["players"][k].Text());
            const Json& st = x["status"];
            d.name = !x["name"].Text().empty() ? x["name"].Text() : st["name"].Text(); d.act = st["act"].Text(); d.arg = st["arg"].Text(); d.rule = st["rule"].Text();
            d.team_ = st["team"].Int(-1); d.life = st["life"].Int(); d.kills = st["k"].Int(); d.deaths = st["d"].Int(); d.real = st["real"].Int();
            d.phase = st["phase"].Int(-1); d.map = st["map"].Int(-1); d.alive = st["alive"].Truthy();
            d.level = (float)st["level"].Dbl(0); d.levelWhy = st["level_why"].Text();
            const Json& lv = st["live"];                      // (v141 beta 6: an older doll has none)
            d.tier = st["tier"].Text(); d.why = lv["reason"].Text(); d.presence = st["presence"].Text(); d.realPlayers = st["real_players"].Int(-1);
            d.enemy = (float)lv["enemy_strength"].Dbl(-1); d.ally = (float)lv["ally_strength"].Dbl(-1); d.confidence = (float)lv["confidence"].Dbl(-1);
            d.inMatch = st["page"].Text().size() == 5 && st["page"].Text()[0] == 'n';
            v.push_back(d);
        }
        dollsVdoll_ = vdoll;
        dolls_.swap(v);
    });
}

// (3.2) why a bot's playable dolls wait, when fewer of them play than are set: its code's line with the manager's numbers;
// a code this program does not know: the server's own words
std::string App::DollWaitText(int bot) const {
    if (demo_ && demoAgentOld_) return std::string();          // (the self-test's older agent: none)
    const auto it = dollsWait_.find(bot);
    if (it == dollsWait_.end()) return std::string();
    const DollWait& w = it->second;
    if (w.code.empty() || w.dollsSet <= w.dollsIn) return std::string();
    if (w.code == "seats") return TF("v_dwait_seats", w.seats, w.real, w.vdolls, w.kept);
    if (w.code == "bot_stopped") return T("v_dwait_bot_stopped");
    if (w.code == "not_hosting") return T("v_dwait_not_hosting");
    if (w.code == "server_full") return T("v_dwait_server_full");
    if (w.code == "pace_guard") {
        const int cap = std::max(0, w.paceCap), minutes = (std::max(0, w.paceWaitS) + 59) / 60;
        return minutes > 0 ? TF("v_dwait_pace", cap, minutes) : TF("v_dwait_pace_now", cap);
    }
    if (w.code == "gate_off") return T("v_dwait_gate_off");
    return w.why;
}

// (3.3) the line of help under the AIM rows: what DIRECT does
void App::AimHelp() {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * S);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10 * S);
    ImGui::TextUnformatted(T("doll_aim_help"));
    ImGui::PopTextWrapPos();
}

void App::DollOrder(int n, const std::string& order, const std::string& okText) {
    if (demo_) { Toast(okText.empty() ? TF("m_doll_sent", n) : okText); return; }
    remote_.Agent("doll-order " + std::to_string(n) + " " + order, "", [this, n, okText](int rc, const Json& j, const std::string&) {
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        Toast(okText.empty() ? TF("m_doll_sent", n) : okText);
        dollsAt_ = -100;                                  // the list again at once
    });
}

void App::DollSet(int n, const std::string& json, const std::string& okText) {
    if (demo_) { Toast(okText.empty() ? TF("m_doll_sent", n) : okText); return; }
    remote_.Agent("doll-set " + std::to_string(n), json, [this, n, okText](int rc, const Json& j, const std::string&) {
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        Toast(okText.empty() ? TF("m_doll_sent", n) : okText);
        dollsAt_ = -100;
    });
}

// (v141 beta 6) a bound of the live level: the running doll takes it at once (doll-order), its settings keep it (doll-set)
void App::DollBound(int n, const char* key, int v) {
    if (demo_) { Toast(TF("m_doll_sent", n)); return; }
    const std::string k = key, js = "{" + JsonQuote(k) + ":" + std::to_string(v) + "}";
    remote_.Agent("doll-order " + std::to_string(n) + " " + k + " " + std::to_string(v), "", [this, n, js](int rc, const Json& j, const std::string&) {
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); dollsAt_ = -100; return; }
        DollSet(n, js);
    });
}

void App::DollCount(int bot, int n, const std::string& name) {
    const std::string ok = TF("m_dollcount", std::to_string(bot).c_str(), std::to_string(n).c_str());
    if (demo_) { Toast(ok); return; }
    dcBusy_ = true;
    const std::string js = name.empty() ? std::string() : "{\"name\":" + JsonQuote(name) + "}";   // (agent 2.9: the doll it adds)
    remote_.Agent("doll-count " + std::to_string(bot) + " " + std::to_string(n), js, [this, ok](int rc, const Json& j, const std::string&) {
        dcBusy_ = false;
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        Toast(ok);
        dollsAt_ = -100;                                  // the list again at once
    });
}

void App::PageDolls() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    if (!dollsLoading_ && ImGui::GetTime() - dollsAt_ > 3) LoadDolls();
    const HostStatus st = Status();
    UpdateBanner();
    PanelHeader(T("p_dolls"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DemoTiers();
    // a doll a row of three lines: its number, its player name, its bot, its team; what it does; its life and score, the
    // order it has. (v141 beta 6) Its level over and under ACTIONS: the difficulty set and its tier, the live level and its
    // tier when it plays another now (the column as wide as the language's longest, the lines of the row left whole)
    const float rw = ImGui::GetContentRegionAvail().x, rh = 74 * S;
    static int levelLang = -1;
    static float levelS = 0, levelW = 0;
    if (levelLang != CurLang() || levelS != S) {
        levelLang = CurLang(); levelS = S; levelW = 0;
        for (int t = 1; t <= 10; ++t) levelW = std::max(levelW, FitWidth(F.body, (TF("v_doll_skill", t) + " " + DollTierText(t)).c_str(), 200 * S, 0));
        for (int v = 10; v <= 100; ++v)                   // (the digits' widths differ)
            levelW = std::max(levelW, FitWidth(F.body, TF("v_doll_level_now", LiveText(v / 10.0f, "").c_str()).c_str(), 200 * S, 0));
    }
    // (3.2) the mark of a doll "as the virtual dolls", under ACTIONS: v_doll_skill_vd with no level before it ("(virtual
    // dolls' level)"); the column as wide as it needs when a doll follows
    std::string vdMark;
    if (dollsVdoll_) for (const auto& d : dolls_) if (d.fromVdoll) { vdMark = TF("v_doll_skill_vd", ""); break; }
    while (!vdMark.empty() && vdMark[0] == ' ') vdMark.erase(0, 1);
    const float markW = vdMark.empty() ? 0 : FitWidth(F.body, vdMark.c_str(), 200 * S, 0);
    const float actW = std::max(std::max(ButtonWidth(T("b_actions")), 120 * S), std::max(levelW, markW) + 8 * S);
    const float cAct = rw - actW - 4 * S, cNum = 34 * S;
    const float cName = cNum + std::max(90 * S, SpacedWidth(F.row, F.row->FontSize, TF("v_doll", 16).c_str(), RowSpacing()) + 16 * S);
    for (const auto& d : dolls_) {
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(d.n);
        ImGui::InvisibleButton("##drow", ImVec2(cAct, rh));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), hov ? kRowHi : kRow);
        dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
        const bool playing = d.running && d.inMatch && d.age >= 0 && d.age < 30;
        const bool valid = d.hasAccount || d.accountless;      // (no account, and none allowed: it cannot play)
        const ImU32 col = !d.enabled || !valid ? kRed : playing ? kGreen : kAmber;
        const float l1 = a.y + 6 * S, l3 = a.y + rh - 7 * S - F.body->FontSize, l2 = l3 - 3 * S - F.body->FontSize;
        StatusLight(dl, ImVec2(a.x + 16 * S, l1 + F.row->FontSize * 0.5f), 6 * S, col, d.running && !playing);
        const std::string num = TF("v_doll", d.n);
        FitText(dl, F.row, ImVec2(a.x + cNum, l1), cName - cNum - 8 * S, kText, num.c_str(), RowSpacing());
        // its player name (the game's), else its account (masked); its bot; its team while it plays
        const std::string whoName = !d.name.empty() ? Upper(d.name) : d.hasAccount ? Upper(d.account) : std::string(T("v_doll_noacc"));
        std::string team;
        if (d.running && d.inMatch) {
            if (d.team_ == 0) team = "   " + std::string(T("v_team_red"));
            else if (d.team_ == 1) team = "   " + std::string(T("v_team_blue"));
            else if (d.team_ == 2) team = "   " + std::string(T("v_team_spect"));
        }
        std::string who = whoName + "   " + TF("v_doll_bot", d.bot) + team;
        const float whoW = cAct - cName - 10 * S;
        if (SpacedWidth(F.row, F.row->FontSize, who.c_str(), RowSpacing()) > whoW) who = whoName + team;   // (narrow: its bot left out, its menu says it)
        FitText(dl, F.row, ImVec2(a.x + cName, l1), whoW, valid ? kText : kRed, who.c_str(), RowSpacing());
        // its level over ACTIONS: the difficulty set; under it the live level when it plays another now. (3.2) A doll "as the
        // virtual dolls": the level the server resolved for it (none until its answer) and, under ACTIONS, its mark
        // "(virtual dolls' level)" (it plays with the live level off)
        const bool vd = dollsVdoll_ && d.fromVdoll;
        const bool live = !vd && d.running && d.inMatch && d.adapt && d.level > 0 && std::fabs(d.level - d.difficulty) >= 0.1f;
        const std::string tag = vd && d.difficulty < 1 ? std::string() : TF("v_doll_skill", d.difficulty) + " " + DollTierText(d.difficulty);
        const std::string now = live ? TF("v_doll_level_now", LiveText(d.level, d.tier).c_str()) : vd ? vdMark : std::string();
        if (!tag.empty()) {
            const float tagW = FitWidth(F.body, tag.c_str(), actW, 0);
            FitText(dl, F.body, ImVec2(a.x + cAct + (actW - tagW) * 0.5f, l1 + (F.row->FontSize - F.body->FontSize) * 0.5f), actW, kText, tag.c_str(), 0);
        }
        if (!now.empty()) {
            const float nowW = FitWidth(F.body, now.c_str(), actW, 0);
            FitText(dl, F.body, ImVec2(a.x + cAct + (actW - nowW) * 0.5f, l3), actW, kBlueText, now.c_str(), 0);
        }
        // what it does now
        std::string state, score;
        if (!d.enabled) state = T("v_doll_off");
        else if (!d.running) state = T("v_doll_idle");
        else {
            state = ActivityText(d);
            if (d.inMatch && d.alive) score = TF("v_doll_life", d.life) + ", ";
            if (d.inMatch) score += TF("v_doll_kd", d.kills, d.deaths);
        }
        const ImU32 sc = !d.enabled ? kRed : playing ? kGreenText : kAmberText;
        const float textW = cAct - cNum - 10 * S;
        FitText(dl, F.body, ImVec2(a.x + cNum, l2), textW, sc, state.c_str(), 0);
        // its life and score; on the right, the order it has (but "free"), apart from the live level under ACTIONS (both blue)
        const std::string order = d.order != "free" ? OrderLabel(d.order) : std::string();
        const float orderR = cAct - (live || vd ? 26 : 10) * S;
        float orderW = 0;
        if (!order.empty()) {
            orderW = FitWidth(F.body, order.c_str(), textW * 0.5f, 0);
            FitText(dl, F.body, ImVec2(a.x + orderR - orderW, l3), orderW + 1, kBlueText, order.c_str(), 0);
        }
        if (!score.empty()) FitText(dl, F.body, ImVec2(a.x + cNum, l3), orderR - cNum - orderW - (orderW > 0 ? 12 * S : 0), kText, score.c_str(), 0);
        // the help: its level (and now), the live level's judgment or its wait for a real player, what it does, its score and order
        if (hov) {
            std::string h = vd ? (tag.empty() ? std::string(T("v_diff_vdoll")) : TF("v_doll_skill_vd", tag.c_str())) : tag + (live ? "   " + now : std::string());
            if (d.running && d.presence == "waiting_for_real") h += "\n" + std::string(T("v_live_wait"));
            else if (vd) h += "\n" + std::string(T("v_doll_vd_live"));
            else if (d.running && d.inMatch && d.adapt && d.enemy >= 0 && d.ally >= 0 && !d.why.empty())
                h += "\n" + TF("v_live_judge", Dec(d.enemy, 1).c_str(), Dec(d.ally, 1).c_str(), std::to_string((int)std::lround(d.confidence * 100)).c_str(),
                               WhyText(d.why).c_str());
            h += "\n" + state;
            if (!score.empty() || !order.empty()) h += "\n" + score + (score.empty() || order.empty() ? "" : "   ") + order;
            SetHelp(num, h);
        }
        const ImVec2 after(a.x, a.y + rh);
        ImGui::SetCursorScreenPos(ImVec2(a.x + cAct, a.y + (rh - 28 * S) * 0.5f));
        const std::string id = "dact" + std::to_string(d.n);
        if (Button(id.c_str(), T("b_actions"), T("b_actions"), num, true, false, actW)) { dollMenuN_ = d.n; dollMenuOpen_ = true; dmPlayer_ = 0; dmNameFor_ = 0; }
        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy(ImVec2(rw, 0));
    }
    if (!dollsError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", TF("m_cmd_failed", dollsError_.c_str()).c_str()); ImGui::PopStyleColor();
    } else if (dolls_.empty()) InfoRow(T("p_dolls"), dollsLoaded_ || demo_ ? (dollsAccountless_ ? T("v_nodolls_acl") : T("v_nodolls")) : T("i_working"));
    Gap(8);
    // SBMM BOTS: the live level of every doll of a bot's game at once (on: each one's moves over the ladder after its enemies'
    // and its side's strength; off: the difficulty set), and their difficulty; a pair of rows a bot that has dolls
    {
        std::vector<int> dbots;
        for (const auto& d : dolls_) if (std::find(dbots.begin(), dbots.end(), d.bot) == dbots.end()) dbots.push_back(d.bot);
        std::sort(dbots.begin(), dbots.end());
        auto all = [this](int b, const std::string& what, const std::string& ok) {
            if (demo_) { Toast(ok); return; }
            remote_.Agent("doll-all " + std::to_string(b) + " " + what, "", [this, ok](int rc, const Json& j, const std::string&) {
                if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
                Toast(ok);
                dollsAt_ = -100;                                  // the list again at once
            });
        };
        for (int b : dbots) {
            // (3.2) its dolls "as the virtual dolls" apart: they have no difficulty of their own and play with the live level off
            int on = 0, off = 0, diff = -1, vds = 0, own = 0;
            bool same = true;
            for (const auto& d : dolls_) {
                if (d.bot != b) continue;
                if (dollsVdoll_ && d.fromVdoll) { ++vds; continue; }
                ++own;
                ++(d.adapt ? on : off);
                if (diff < 0) diff = d.difficulty; else if (diff != d.difficulty) same = false;
            }
            const bool allVd = vds > 0 && own == 0;
            const std::string suffix = dbots.size() > 1 ? "   " + TF("v_doll_bot", b) : std::string();
            const std::string l1 = std::string(T("i_sbmm")) + suffix, l2 = std::string(T("i_sbmm_skill")) + suffix;
            const std::string id1 = "dsbmm" + std::to_string(b), id2 = "dsbmmskill" + std::to_string(b);
            if (ValueRow(id1.c_str(), l1.c_str(), on && off ? T("v_mixed") : on ? T("v_on") : T("v_off"), T("i_sbmm"), T("h_sbmm"), !allVd)) {
                const bool to = !(on > 0 && off == 0);               // all on: off; else on
                for (auto& x : dolls_) if (x.bot == b) x.adapt = to;
                all(b, to ? "adapt on" : "adapt off", TF(to ? "m_sbmm_on" : "m_sbmm_off", std::to_string(b).c_str()));
            }
            // their difficulty: (3.2) AS THE VIRTUAL DOLLS first when the server takes it, then (v141 beta 6) 1 TRAINING .. 10 PRO
            const bool one = vds == 0 && same && diff > 0;
            const std::string v2 = allVd ? std::string(T("v_diff_vdoll")) : one ? SkillText(diff) : std::string(T("v_mixed"));
            const std::string h2 = allVd ? std::string(T("h_diff_vdoll")) : std::string(T("h_sbmm_skill")) + " " + T("h_tier10");
            if (const int dd = ValueRow(id2.c_str(), l2.c_str(), v2.c_str(), T("i_sbmm_skill"), h2)) {
                const int nv = Cycle((allVd ? 0 : one ? TierClamp(diff) : 3) + dd, dollsVdoll_ ? 0 : 1, 10);
                for (auto& x : dolls_) if (x.bot == b) { x.fromVdoll = nv == 0; x.difficulty = nv; }   // (0: the server's answer tells their level)
                const std::string what = nv ? SkillText(nv) : std::string(T("v_diff_vdoll"));
                all(b, nv ? "difficulty " + std::to_string(nv) : std::string("difficulty vdoll"), TF("m_sbmm_skill", std::to_string(b).c_str(), what.c_str()));
            }
            // (3.3) their aim, when the server takes it (agent 3.5): PAD (through the stick, as before) or DIRECT (the view turned
            // at a virtual doll's speed of its level, without the stick); MIXED when they differ. All DIRECT: PAD; else DIRECT
            if (dollsAim_) {
                int direct = 0, pad = 0;
                for (const auto& d : dolls_) if (d.bot == b) ++(d.aimDirect ? direct : pad);
                const std::string l3 = std::string(T("i_dolls_aim")) + suffix, id3 = "dollsaim" + std::to_string(b);
                if (ValueRow(id3.c_str(), l3.c_str(), direct && pad ? T("v_mixed") : direct ? T("v_aim_direct") : T("v_aim_pad"), T("i_dolls_aim"), T("h_dolls_aim"))) {
                    const bool to = !(direct > 0 && pad == 0);
                    for (auto& x : dolls_) if (x.bot == b) x.aimDirect = to;
                    all(b, to ? "aim direct" : "aim pad", TF("m_dolls_aim", std::to_string(b).c_str(), T(to ? "v_aim_direct" : "v_aim_pad")));
                }
            }
        }
        if (!dbots.empty() && dollsAim_) AimHelp();
        if (!dbots.empty()) Gap(8);
    }
    if (dollsCap_ >= 0) InfoRow(T("i_dollcap"), TF("v_dollcap", dollsCap_).c_str(), dollsCap_ == 0 ? kRed : 0);
    if (ValueRow("dkeep", T("i_keepseat"), dollsKeepSeat_ ? T("v_on") : T("v_off"), T("i_keepseat"), T("h_keepseat"))) {
        dollsKeepSeat_ = !dollsKeepSeat_;
        AgentCmd(std::string("doll-keep-seat ") + (dollsKeepSeat_ ? "on" : "off"));
    }
    // (v141 beta 3) how TX-55's game list shows the dolls with no account (agent 2.7: server.env DOLL_LIST)
    if (dollsAccountless_ && !dollsList_.empty()) {
        static const char* const kModes[3] = {"host", "own", "off"};
        static const char* const kNames[3] = {"v_dl_host", "v_dl_own", "v_dl_off"};
        int i = dollsList_ == "own" ? 1 : dollsList_ == "off" ? 2 : 0;
        if (const int d = ValueRow("dlist", T("i_dolllist"), T(kNames[i]), T("i_dolllist"), T("h_dolllist"))) {
            i = Cycle(i + 1 + d, 1, 3) - 1;
            dollsList_ = kModes[i];
            AgentCmd(std::string("doll-list ") + kModes[i], "", TF("m_dolllist", T(kNames[i])));
        }
    }
    // (v141 beta 7) SaveMGO's rankings and the dolls (agent 3.1: server.env HOST_UNRANKED): the games with dolls left out (auto, the
    // default), every game ranked (off) or none (always); each bot takes it at its game's next start
    if (!dollsUnranked_.empty()) {
        static const char* const kModes[3] = {"auto", "off", "always"};
        static const char* const kNames[3] = {"v_ur_auto", "v_ur_off", "v_ur_always"};
        int i = dollsUnranked_ == "off" ? 1 : dollsUnranked_ == "always" ? 2 : 0;
        if (const int d = ValueRow("dunranked", T("i_unranked"), T(kNames[i]), T("i_unranked"), T("h_unranked"))) {
            i = Cycle(i + 1 + d, 1, 3) - 1;
            dollsUnranked_ = kModes[i];
            AgentCmd(std::string("host-unranked ") + kModes[i], "", TF("m_unranked", T(kNames[i])));
        }
    }
    // (3.2) why a bot's playable dolls wait: one wrapped line in the warning colour, under its row (named: the bot said first)
    auto waitLine = [this](int b, bool named) {
        const std::string t = DollWaitText(b);
        if (t.empty()) return false;
        Gap(3);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * S);
        ImGui::PushStyleColor(ImGuiCol_Text, kAmberText);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10 * S);
        ImGui::TextUnformatted((named ? TF("v_doll_bot", b) + "   " + t : t).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        Gap(5);
        return true;
    };
    std::vector<int> rowed;                               // the bots with a row in the block below
    // each bot's dolls with no account (DOLL_<number>, docs/PLAYABLE_DOLL.md 1): how many, one less, one more. (v141) An agent
    // before 2.5 has none: the block says so, the update above (MGO1 Host v141's beta: the block was not there, nothing said why)
    if (!dollsAccountless_ && AgentOld()) {
        Gap(14);
        PanelHeader(T("p_dollcount"));
        ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", T("e_acl_update")); ImGui::PopStyleColor();
    }
    if (dollsAccountless_ && !st.bots.empty()) {
        Gap(14);
        PanelHeader(T("p_dollcount"));
        // (v141 beta 5) the name in the game of the next doll ONE MORE adds (empty: DOLL_<number>)
        std::string nextName;
        bool nextOk = true;
        if (dollsNames_) {
            TextRow("ndaclname", T("i_doll_nextname"), ndAclName_, 16, T("i_doll_nextname"), T("h_doll_nextname"), false, 0.55f, 0);
            nextName = Trimmed(ndAclName_);
            nextOk = nextName.empty() || DollNameOk(nextName, 0);
            if (nextOk && !nextName.empty()) for (const auto& d : dolls_) if (Upper(d.name) == Upper(nextName)) nextOk = false;
            if (!nextOk) { Gap(4); ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", T("e_doll_name")); ImGui::PopStyleColor(); }
            Gap(6);
        }
        const float rw2 = ImGui::GetContentRegionAvail().x, rh2 = 40 * S;
        const float wLess = ButtonWidth(T("b_dolls_less")), wMore = ButtonWidth(T("b_dolls_more"));
        const int room = dollsMax_ - (int)dolls_.size();         // (the server's 16 dolls, with an account or not)
        for (const auto& bi : st.bots) {
            const int b = bi.bot;
            int k = 0;
            if (dollsCount_.count(b)) k = dollsCount_[b];
            else for (const auto& d : dolls_) if (d.bot == b && d.accountless) ++k;
            const ImVec2 a = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(a, ImVec2(a.x + rw2, a.y + rh2), kRow);
            dl->AddLine(ImVec2(a.x, a.y + rh2 - 1), ImVec2(a.x + rw2, a.y + rh2 - 1), kLine, 2 * S);
            const float xb = a.x + rw2 - wLess - wMore - 16 * S, ty = a.y + (rh2 - F.row->FontSize) * 0.5f;
            const int most = dollsMost_.count(b) ? dollsMost_[b] : -1;     // (agent 2.6: the most its game takes)
            const std::string label = TF("v_doll_bot", b), value = most >= 0 ? TF("v_dollcount_of", k, most) : TF("v_dollcount", k);
            const float lw = std::min((xb - a.x) * 0.4f, SpacedWidth(F.row, F.row->FontSize, TF("v_doll_bot", 10).c_str(), RowSpacing()) + 16 * S);
            FitText(dl, F.row, ImVec2(a.x + 10 * S, ty), lw - 10 * S, kText, label.c_str(), RowSpacing());
            FitText(dl, F.row, ImVec2(a.x + lw + 10 * S, ty), xb - a.x - lw - 20 * S, k ? kText : kTextDim, value.c_str(), RowSpacing());
            ImGui::SetCursorScreenPos(ImVec2(xb, a.y + (rh2 - 28 * S) * 0.5f));
            const std::string idl = "dless" + std::to_string(b), idm = "dmore" + std::to_string(b);
            if (Button(idl.c_str(), T("b_dolls_less"), T("b_dolls_less"), T("h_dolls_less"), k > 0 && !dcBusy_, false, wLess)) DollCount(b, k - 1);
            ImGui::SameLine(0, 6 * S);
            if (Button(idm.c_str(), T("b_dolls_more"), T("b_dolls_more"), T("h_dolls_more"), room > 0 && (most < 0 || k < most) && !dcBusy_ && nextOk, false, wMore)) {
                DollCount(b, k + 1, nextName);
                ndAclName_[0] = 0;
            }
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh2));
            ImGui::Dummy(ImVec2(rw2, 0));
            rowed.push_back(b);
            waitLine(b, false);
        }
        // (a bot that is no longer there and still has dolls: no row, its line with its number)
        for (const auto& bw : dollsWait_) if (std::find(rowed.begin(), rowed.end(), bw.first) == rowed.end()) waitLine(bw.first, true);
        if (room <= 0) { Gap(4); ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", T("e_dolls_full")); ImGui::PopStyleColor(); }
        Gap(8);
        ImGui::TextWrapped("%s", T("dollcount_help"));
    } else {
        // (3.2) no rows (DOLL_GATE=0, or no bot): the lines of the bots whose dolls wait, under the block's header
        bool head = !dollsAccountless_ && AgentOld();
        for (const auto& bw : dollsWait_) {
            if (DollWaitText(bw.first).empty()) continue;
            if (!head) { Gap(14); PanelHeader(T("p_dollcount")); head = true; }
            waitLine(bw.first, true);
        }
    }
    VdollCountBlock();                                    // (v150-host beta 15) the virtual dolls apart, live
    // a new doll: its SaveMGO account, its bot, its level, team and weapons
    Gap(14);
    PanelHeader(dollsAccountless_ ? T("p_adddoll_acc") : T("p_adddoll"));
    if (!cfg.accounts.empty()) {
        const int n = (int)cfg.accounts.size();
        const std::string v = ndSaved_ > 0 && ndSaved_ <= n ? cfg.accounts[ndSaved_ - 1].id : std::string(T("v_new_account"));
        const int dd = ValueRow("ndsaved", T("a_saved"), v.c_str(), T("a_saved"), T("h_a_saved"));
        if (dd) {
            ndSaved_ = Cycle(ndSaved_ + dd, 0, n);
            if (ndSaved_ > 0) {
                snprintf(ndId_, sizeof ndId_, "%s", cfg.accounts[ndSaved_ - 1].id.c_str());
                snprintf(ndPw_, sizeof ndPw_, "%s", cfg.accounts[ndSaved_ - 1].password.c_str());
            } else { ndId_[0] = 0; plat::Wipe(ndPw_, sizeof ndPw_); }
        }
    }
    TextRow("ndid", T("a_id"), ndId_, 16, T("a_id"), T("h_doll_account"), false, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    TextRow("ndpw", T("a_pw"), ndPw_, 16, T("a_pw"), T("h_doll_account"), true, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    std::vector<int> bots;
    for (const auto& b : st.bots) bots.push_back(b.bot);
    if (!bots.empty() && std::find(bots.begin(), bots.end(), ndBot_) == bots.end()) ndBot_ = bots.front();
    {
        const std::string v = bots.empty() ? std::string(T("v_none")) : TF("v_doll_bot", ndBot_);
        const int dd = ValueRow("ndbot", T("i_bot"), v.c_str(), T("i_bot"), T("h_doll_bot"), bots.size() > 1);
        if (dd && !bots.empty()) {
            auto it = std::find(bots.begin(), bots.end(), ndBot_);
            const int i = (int)(it - bots.begin()) + dd;
            ndBot_ = bots[(i + (int)bots.size()) % (int)bots.size()];
        }
    }
    // its difficulty: (3.2) 0 AS THE VIRTUAL DOLLS (sent as "vdoll") when the server takes it, else 1 TRAINING .. 10 PRO
    if (!dollsVdoll_ && ndSkill_ < 1) ndSkill_ = 3;
    {
        const std::string v = ndSkill_ < 1 ? std::string(T("v_diff_vdoll")) : SkillText(ndSkill_);
        const std::string hx = ndSkill_ < 1 ? std::string(T("h_diff_vdoll")) : std::string(T("h_dl_skill")) + " " + T("h_tier10");
        if (const int dd = ValueRow("ndskill", T("dl_skill"), v.c_str(), T("dl_skill"), hx)) ndSkill_ = Cycle(ndSkill_ + dd, dollsVdoll_ ? 0 : 1, 10);
    }
    if (const int dd = ValueRow("ndteam", T("dl_team"), T(kTeamLabels[ndTeam_]), T("dl_team"), T("h_dl_team"))) ndTeam_ = Cycle(ndTeam_ + dd, 0, 3);
    if (const int dd = ValueRow("ndweap", T("dl_loadout"), LoadoutLabel(ndLoadout_).c_str(), T("dl_loadout"), T("h_dl_loadout"))) ndLoadout_ = Cycle(ndLoadout_ + dd, 0, 6);
    if (dollsAim_ && ValueRow("ndaim", T("dl_aim"), T(ndAimDirect_ ? "v_aim_direct" : "v_aim_pad"), T("dl_aim"), T("h_dl_aim"))) ndAimDirect_ = !ndAimDirect_;   // (3.3)
    if (dollsAim_ && dolls_.empty()) AimHelp();          // (no doll yet: no AIM row above it, its help here)
    if (ValueRow("ndkeep", T("a_keep"), aKeep_ ? T("v_on") : T("v_off"), T("a_keep"), T("h_a_keep"))) aKeep_ = !aKeep_;
    Gap(6);
    std::string err;
    const size_t il = strlen(ndId_), pl = strlen(ndPw_);
    int freeN = 0;
    for (int k = 1; k <= dollsMax_ && !freeN; ++k) { bool used = false; for (const auto& d : dolls_) if (d.n == k) used = true; if (!used) freeN = k; }
    if (bots.empty()) err = T("e_doll_nobot");
    else if (!freeN) err = T("e_dolls_full");
    else if (il < 3 || il > 15 || !AccountCharsOk(ndId_)) err = T("e_acc_id");
    else if (pl < 6 || pl > 15 || !AccountCharsOk(ndPw_)) err = T("e_acc_pw");
    else for (const auto& b : st.bots) if (Upper(b.account) == Upper(ndId_)) err = T("e_doll_accbot");
    if (!err.empty() && (il || pl || bots.empty())) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", err.c_str()); ImGui::PopStyleColor(); Gap(6); }
    if (Button("adddoll", T("b_adddoll"), T("b_adddoll"), T("h_adddoll"), err.empty() && !ndBusy_)) {
        const std::string id = Upper(ndId_), pw = Upper(ndPw_);
        const bool keep = aKeep_;
        const std::string js = "{\"id\":" + JsonQuote(id) + ",\"password\":" + JsonQuote(pw) + ",\"bot\":" + std::to_string(ndBot_) +
                               ",\"difficulty\":" + (ndSkill_ < 1 ? std::string("\"vdoll\"") : std::to_string(ndSkill_)) + ",\"team\":" + JsonQuote(kTeams[ndTeam_]) +
                               ",\"loadout\":" + JsonQuote(kLoadouts[ndLoadout_]) + (dollsAim_ ? std::string(",\"aim\":") + (ndAimDirect_ ? "\"direct\"" : "\"pad\"") : std::string()) +
                               ",\"enabled\":true}";
        const int n = freeN;
        if (demo_) Toast(TF("m_doll_added", n));
        else {
            ndBusy_ = true;
            remote_.Agent("doll-set " + std::to_string(n), js, [this, n, id, pw, keep](int rc, const Json& j, const std::string&) {
                ndBusy_ = false;
                if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
                if (keep) { cfg.KeepAccount(id, pw); cfg.Save(); }
                Toast(TF("m_doll_added", n));
                dollsAt_ = -100;
            });
        }
        ndId_[0] = 0; plat::Wipe(ndPw_, sizeof ndPw_); ndSaved_ = 0;
    }
    if (ndBusy_) { Gap(4); ImGui::TextWrapped("%s", T("i_working")); }
    Gap(10);
    ImGui::TextWrapped("%s", dollsAccountless_ ? T("dolls_help_acl") : T("dolls_help"));
}

// a doll's menu: its level, team, order (and the player for follow / target), weapons, playing or not,
// leaving now, removing it; each change goes to the server at once (the running doll takes it live)
void App::DollMenu() {
    const DollInfo* dp = nullptr;
    for (const auto& d : dolls_) if (d.n == dollMenuN_) dp = &d;
    if (!dp) { dollMenuOpen_ = false; return; }
    const bool opening = dollMenuOpen_;
    if (dollMenuOpen_) {
        ImGui::OpenPopup("##dmenu");
        dollMenuOpen_ = false;
    }
    const DollInfo d = *dp;
    const int n = d.n;
    const std::string title = TF("v_doll", n) + (d.name.empty() ? std::string() : "   " + Upper(d.name));
    // (3.2) as wide as AS THE VIRTUAL DOLLS between the arrows needs, when the server offers that difficulty
    float mw = 460 * S;
    if (dollsVdoll_) mw = std::max(mw, std::ceil((SpacedWidth(F.row, F.row->FontSize, T("v_diff_vdoll"), RowSpacing()) + 54 * S) / 0.55f));
    if (demo_ && opening) {
        const float vw = ImGui::GetMainViewport()->Size.x;
        ImGui::SetNextWindowPos(ImVec2(std::min(vw * 0.45f, vw - mw - 8 * S), 200 * S));
    }
    ImGui::SetNextWindowSize(ImVec2(mw, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kBg);
    if (ImGui::BeginPopup("##dmenu")) {
        PanelHeader(title.c_str());
        // the level: (3.2) AS THE VIRTUAL DOLLS first when the server takes it ("difficulty vdoll"), then (v141 beta 6)
        // 1 TRAINING .. 10 PRO
        const bool vd = dollsVdoll_ && d.fromVdoll;
        {
            const std::string v = vd ? std::string(T("v_diff_vdoll")) : SkillText(d.difficulty);
            const std::string hx = vd ? std::string(T("h_diff_vdoll")) : std::string(T("h_dl_skill")) + " " + T("h_tier10");
            if (const int dd = ValueRow("dm_skill", T("dl_skill"), v.c_str(), T("dl_skill"), hx)) {
                const int nv = Cycle((vd ? 0 : TierClamp(d.difficulty)) + dd, dollsVdoll_ ? 0 : 1, 10);
                for (auto& x : dolls_) if (x.n == n) { x.fromVdoll = nv == 0; x.difficulty = nv; }   // (0: the server's answer tells its level)
                DollOrder(n, nv ? "difficulty " + std::to_string(nv) : std::string("difficulty vdoll"));
            }
        }
        if (vd) {
            // (3.2) as the virtual dolls: the server starts it with the live level off. Its controls shown, not changed, then
            // the level the server resolved for it and why they are off
            const int lo = TierClamp(std::min(d.adaptMin, d.adaptMax)), hi = TierClamp(std::max(d.adaptMin, d.adaptMax));
            ValueRow("dm_adapt", T("dl_adapt"), T("v_off"), T("dl_adapt"), "", false);
            ValueRow("dm_amin", T("dl_adapt_min"), SkillText(lo).c_str(), T("dl_adapt_min"), "", false);
            ValueRow("dm_amax", T("dl_adapt_max"), SkillText(hi).c_str(), T("dl_adapt_max"), "", false);
            auto line = [](const std::string& t) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * S);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10 * S);
                ImGui::TextUnformatted(t.c_str());
                ImGui::PopTextWrapPos();
            };
            Gap(4);
            if (d.difficulty >= 1) line(TF("v_doll_skill_vd", SkillText(d.difficulty).c_str()));
            line(T("v_doll_vd_live"));
            Gap(4);
        }
        // the live level: on (it moves over the ladder after its enemies' and its side's strength), off (the difficulty set);
        // its value with its tier when that fits the row ("ON: 7.3 VETERAN", else "ON: 7.3")
        if (!vd) {
            const bool shown = d.adapt && d.running && d.inMatch && d.level > 0;
            std::string v = !d.adapt ? std::string(T("v_off")) : shown ? TF("v_adapt_live", LiveText(d.level, d.tier).c_str()) : std::string(T("v_on"));
            const float valW = ImGui::GetContentRegionAvail().x * 0.55f - 52 * S;
            if (shown && SpacedWidth(F.row, F.row->FontSize, v.c_str(), 0) > valW) v = TF("v_adapt_live", Dec(d.level, 1).c_str());
            if (ValueRow("dm_adapt", T("dl_adapt"), v.c_str(), T("dl_adapt"), T("h_dl_adapt"))) {
                for (auto& x : dolls_) if (x.n == n) x.adapt = !d.adapt;
                DollOrder(n, d.adapt ? "adapt off" : "adapt on");
            }
        }
        // (v141 beta 6) its bounds, the server's control (min never above max); an older agent has none: shown, not changed
        if (d.adapt && !vd) {
            const int lo = TierClamp(std::min(d.adaptMin, d.adaptMax)), hi = TierClamp(std::max(d.adaptMin, d.adaptMax));
            const bool can = d.boundsKnown || demo_;
            if (const int dd = ValueRow("dm_amin", T("dl_adapt_min"), SkillText(lo).c_str(), T("dl_adapt_min"), T("h_dl_adapt_bounds"), can)) {
                const int v = Cycle(lo + dd, 1, hi);
                for (auto& x : dolls_) if (x.n == n) x.adaptMin = v;
                DollBound(n, "adapt_min", v);
            }
            if (const int dd = ValueRow("dm_amax", T("dl_adapt_max"), SkillText(hi).c_str(), T("dl_adapt_max"), T("h_dl_adapt_bounds"), can)) {
                const int v = Cycle(hi + dd, lo, 10);
                for (auto& x : dolls_) if (x.n == n) x.adaptMax = v;
                DollBound(n, "adapt_max", v);
            }
        }
        // the team
        const int ti = IndexOf(kTeams, 4, d.team);
        if (const int dd = ValueRow("dm_team", T("dl_team"), T(kTeamLabels[ti]), T("dl_team"), T("h_dl_team"))) {
            const int v = Cycle(ti + dd, 0, 3);
            for (auto& x : dolls_) if (x.n == n) x.team = kTeams[v];
            DollOrder(n, std::string("team ") + kTeams[v]);
        }
        // the order; follow / target take the player picked below
        const std::string ok = d.order.substr(0, d.order.find(' '));
        const int oi = IndexOf(kOrders, 6, ok);
        if (const int dd = ValueRow("dm_order", T("dl_order"), T(kOrderLabels[oi]), T("dl_order"), T("h_dl_order"))) {
            const int v = Cycle(oi + dd, 0, 5);
            std::string o = kOrders[v];
            if ((v == 4 || v == 5)) {
                if (d.players.empty()) o = "free";
                else o += " " + d.players[std::min((int)d.players.size() - 1, std::max(0, dmPlayer_))];
            }
            for (auto& x : dolls_) if (x.n == n) x.order = o;
            DollOrder(n, "order " + o);
        }
        if ((oi == 4 || oi == 5) && !d.players.empty()) {
            dmPlayer_ = std::min((int)d.players.size() - 1, std::max(0, dmPlayer_));
            const size_t sp = d.order.find(' ');
            const std::string cur = sp == std::string::npos ? d.players[dmPlayer_] : d.order.substr(sp + 1);
            if (const int dd = ValueRow("dm_player", T("dl_player"), cur.c_str(), T("dl_player"), T("h_dl_player"), d.players.size() > 1)) {
                auto it = std::find(d.players.begin(), d.players.end(), cur);
                int i = (it == d.players.end() ? 0 : (int)(it - d.players.begin())) + dd;
                i = (i + (int)d.players.size()) % (int)d.players.size();
                dmPlayer_ = i;
                const std::string o = std::string(kOrders[oi]) + " " + d.players[i];
                for (auto& x : dolls_) if (x.n == n) x.order = o;
                DollOrder(n, "order " + o);
            }
        }
        // its weapons (the next spawn)
        const int li = IndexOf(kLoadouts, 7, d.loadout.substr(0, d.loadout.find(',')));
        if (const int dd = ValueRow("dm_weap", T("dl_loadout"), LoadoutLabel(li).c_str(), T("dl_loadout"), T("h_dl_loadout"))) {
            const int v = Cycle(li + dd, 0, 6);
            for (auto& x : dolls_) if (x.n == n) x.loadout = kLoadouts[v];
            DollSet(n, std::string("{\"loadout\":") + JsonQuote(kLoadouts[v]) + "}");
        }
        // (3.3) its aim, when the server takes it: PAD or DIRECT, at once (doll-order <n> aim) and kept for its next starts
        if (dollsAim_ && ValueRow("dm_aim", T("dl_aim"), T(d.aimDirect ? "v_aim_direct" : "v_aim_pad"), T("dl_aim"), T("h_dl_aim"))) {
            for (auto& x : dolls_) if (x.n == n) x.aimDirect = !d.aimDirect;
            DollOrder(n, d.aimDirect ? "aim pad" : "aim direct");
        }
        // playing or not
        if (ValueRow("dm_on", T("dl_enabled"), d.enabled ? T("v_on") : T("v_off"), T("dl_enabled"), T("h_dl_enabled"))) {
            for (auto& x : dolls_) if (x.n == n) x.enabled = !d.enabled;
            DollSet(n, std::string("{\"enabled\":") + (d.enabled ? "false" : "true") + "}");
        }
        // (v141 beta 5) its name in the game (no account): typed here, RENAME sends it (a doll in a game leaves it and comes
        // back under it); empty: DOLL_<n> again
        const bool naming = d.accountless && dollsNames_;
        std::string newName;
        bool nameOk = false;
        if (naming) {
            if (dmNameFor_ != n) { dmNameFor_ = n; snprintf(dmName_, sizeof dmName_, "%s", d.nameSet ? d.name.c_str() : ""); }
            TextRow("dm_name", T("dl_name"), dmName_, 16, T("dl_name"), T("h_dl_name"), false, 0.55f, 0);
            newName = Trimmed(dmName_);
            if (Upper(newName) == "DOLL_" + std::to_string(n)) newName.clear();
            nameOk = newName.empty() || DollNameOk(newName, n);
            if (nameOk && !newName.empty()) for (const auto& x : dolls_) if (x.n != n && Upper(x.name) == Upper(newName)) nameOk = false;
            if (!nameOk) { Gap(4); ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", T("e_doll_name")); ImGui::PopStyleColor(); }
        }
        Gap(6);
        int pick = -1;
        if (naming && MenuRow("dm_rename", T("b_doll_rename"), false, T("b_doll_rename"), T("h_doll_rename"),
                              nameOk && newName != (d.nameSet ? d.name : std::string()))) pick = 3;
        if (MenuRow("dm_leave", T("b_doll_leave"), false, T("b_doll_leave"), T("h_doll_leave"), d.running)) pick = 0;
        const std::string rmText = d.accountless ? TF("c_doll_remove_acl", n) : TF("c_doll_remove", n);   // (no account: its bot one doll less)
        if (MenuRow("dm_remove", T("b_doll_remove"), false, T("b_doll_remove"), rmText)) pick = 1;
        if (MenuRow("dm_cancel", T("b_cancel"), false, T("b_cancel"), "")) pick = 2;
        if (pick >= 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (pick == 0) DollOrder(n, "leave");
        if (pick == 3) {
            for (auto& x : dolls_) if (x.n == n) { x.name = newName.empty() ? "DOLL_" + std::to_string(n) : newName; x.nameSet = !newName.empty(); }
            DollSet(n, "{\"name\":" + JsonQuote(newName) + "}",
                    newName.empty() ? TF("m_doll_named_default", n) : TF("m_doll_renamed", n, newName.c_str()));
            dmNameFor_ = 0;
        }
        if (pick == 1) Ask(rmText, [this, n] {
            if (demo_) { Toast(TF("m_doll_removed", n)); return; }
            remote_.Agent("doll-remove " + std::to_string(n), "", [this, n](int rc, const Json& j, const std::string&) {
                if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
                Toast(TF("m_doll_removed", n));
                dolls_.erase(std::remove_if(dolls_.begin(), dolls_.end(), [n](const DollInfo& x) { return x.n == n; }), dolls_.end());
                dollsAt_ = -100;
            });
        });
    }
    ImGui::PopStyleColor();
}
