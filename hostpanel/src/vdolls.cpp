// The VIRTUAL DOLLS page: the players each bot's own game runs (docs/VIRTUAL_DOLL.md: no game, no account, no service of
// their own). Every bot's at a glance, then the bot shown (BOT under the menu): those in its game now, and its settings (how
// many, each one's name, team and level) saved for its next game or saved with the game restarted, as GAME SETTINGS does.
// Over mgo1host-agent: vdolls, vdoll-set.
#include "app.h"
#include "i18n.h"
#include "ui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ui;

namespace {
const int kMax = 15;                                  // a bot's virtual dolls at most (MGO1_VDOLLS)
const char* const kTeams[3] = {"auto", "red", "blue"};
const char* const kTeamKeys[3] = {"v_team_auto", "v_team_red", "v_team_blue"};
// (v150-host beta 15) the weapons a virtual doll may hold (its primary; AUTO: one by chance each life), the game's own names
const char* const kWeapons[6] = {"auto", "ak47", "xm16e1", "scorpion", "m37", "svd"};
const char* const kWeaponNames[6] = {nullptr, "AK-47", "XM16E1", "SCORPION", "M37", "SVD"};
const char* WeaponText(int i) { return i <= 0 || i > 5 ? T("v_team_auto") : kWeaponNames[i]; }
// a virtual doll's state and life as its bot's status says them (vdoll.cpp StatusJson, LifeName), and their texts
const char* const kStates[][2] = {{"in_game", "v_vds_in_game"}, {"joining", "v_vds_joining"}, {"leaving", "v_vds_leaving"}, {"out", "v_vds_out"}};
const char* const kLives[][2] = {{"not spawned", "v_vdl_none"}, {"spawned", "v_vdl_wait"}, {"alive", "v_vdl_alive"}, {"dead", "v_vdl_dead"},
                                 {"a ghost to the round's end", "v_vdl_ghost"}, {"going spectator", "v_vdl_going_spect"}, {"a spectator", "v_vdl_spect"}};

std::string Upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
std::string Trim(const char* s) {
    std::string t = s;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    size_t a = 0;
    while (a < t.size() && t[a] == ' ') ++a;
    return t.substr(a);
}
std::string OwnName(int i) { return "VDOLL" + std::to_string(i + 1); }     // doll i's (from 0) name when it has none
int TeamIndex(const std::string& t) { return t == "red" ? 1 : t == "blue" ? 2 : 0; }
int LevelOf(const std::string& t) { const int v = atoi(t.c_str()); return v < 1 ? 5 : v > 10 ? 10 : v; }
int WeaponIndex(const std::string& t) { for (int i = 1; i < 6; ++i) if (t == kWeapons[i]) return i; return 0; }
int Cycle(int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; }

// a list's value for each of the 15 dolls ("red,blue": red, blue, blue ...: the last for the rest)
std::vector<std::string> Slots(const std::string& list) {
    std::vector<std::string> items, v(kMax);
    size_t at = 0;
    while (at <= list.size()) {
        const size_t c = list.find(',', at);
        items.push_back(list.substr(at, c == std::string::npos ? std::string::npos : c - at));
        if (c == std::string::npos) break;
        at = c + 1;
    }
    for (int i = 0; i < kMax; ++i) v[i] = items[std::min((size_t)i, items.size() - 1)];
    return v;
}
// the shortest list that gives these 15 values
std::string ListOf(const std::vector<std::string>& v) {
    size_t n = v.size();
    while (n > 1 && v[n - 1] == v[n - 2]) --n;
    std::string s;
    for (size_t i = 0; i < n; ++i) s += (i ? "," : "") + v[i];
    return s;
}
std::string TeamsList(const std::string& teams) { std::vector<std::string> v = Slots(teams); for (auto& t : v) t = kTeams[TeamIndex(t)]; return ListOf(v); }
std::string LevelList(const std::string& level) { std::vector<std::string> v = Slots(level); for (auto& t : v) t = std::to_string(LevelOf(t)); return ListOf(v); }
std::string WeaponsList(const std::string& w) { std::vector<std::string> v = Slots(w); for (auto& t : v) t = kWeapons[WeaponIndex(t)]; return ListOf(v); }

// a name doll i may have: a DOLL's name, and not another virtual doll's own VDOLL<n>
bool NameOk(const std::string& s, int i) {
    const std::string u = Upper(s);
    if (u.size() > 5 && u.compare(0, 5, "VDOLL") == 0 && u.find_first_not_of("0123456789", 5) == std::string::npos && u != OwnName(i)) return false;
    return DollNameOk(s, 0);
}
// the names file 15 names make ("": the doll's own VDOLL<n>): VDOLL<n> in a gap, nothing after the last one named; a name that
// cannot stay (not a name, twice) left out
std::vector<std::string> NamesFile(std::vector<std::string> s) {
    std::vector<std::string> seen;
    for (int i = 0; i < kMax; ++i) {
        if (Upper(s[i]) == OwnName(i)) s[i].clear();
        if (!s[i].empty() && (!NameOk(s[i], i) || std::find(seen.begin(), seen.end(), Upper(s[i])) != seen.end())) s[i].clear();
        seen.push_back(Upper(s[i].empty() ? OwnName(i) : s[i]));
    }
    int last = kMax - 1;
    while (last >= 0 && s[last].empty()) --last;
    std::vector<std::string> out;
    for (int i = 0; i <= last; ++i) out.push_back(s[i].empty() ? OwnName(i) : s[i]);
    return out;
}
std::vector<std::string> NameSlots(const std::vector<std::string>& names) {
    std::vector<std::string> v(kMax);
    for (int i = 0; i < kMax && i < (int)names.size(); ++i) v[i] = names[i];
    return v;
}

VdollBot ParseBot(const Json& x) {
    VdollBot b;
    b.bot = x["bot"].Int(); b.count = std::max(0, std::min(kMax, x["count"].Int())); b.most = x["most"].Int(-1);
    b.teams = x["teams"].Text().empty() ? "auto" : x["teams"].Text();
    b.level = x["level"].Text().empty() ? "5" : x["level"].Text();          // (an agent 3.2 before the level: 5, the game's)
    b.weapons = x["weapons"].Text().empty() ? "auto" : x["weapons"].Text();  // (an agent before 3.3: auto)
    b.controls = x["controls"].Text() == "player" ? "player" : "direct";      // (3.3: an agent before 3.5, direct)
    // (3.4) the live level (an agent before 3.6: off, 1 to 10) and each doll's level now (only when the server says them)
    b.adapt = x["adapt"].Truthy();
    b.adaptMin = std::max(1, std::min(10, x["adapt_min"].Int(1)));
    b.adaptMax = std::max(1, std::min(10, x["adapt_max"].Int(10)));
    if (b.adaptMin > b.adaptMax) std::swap(b.adaptMin, b.adaptMax);
    for (size_t k = 0; k < x["levels_now"].Size() && k < (size_t)kMax; ++k) b.levelsNow.push_back((float)x["levels_now"][k].Dbl(0));
    for (size_t k = 0; k < x["levels_now_set"].Size() && k < (size_t)kMax; ++k) b.levelsNowSet.push_back(x["levels_now_set"][k].Int(0));
    for (size_t k = 0; k < x["names"].Size(); ++k) b.names.push_back(x["names"][k].Text());
    b.setInGame = x["set_in_game"].IsNull() ? -1 : x["set_in_game"].Int(-1);
    b.inGame = x["in_game"].Int();
    for (size_t k = 0; k < x["live"].Size(); ++k) {
        const Json& l = x["live"][k];
        VdollLive v;
        v.n = l["n"].Int(); v.player = l["player"].Int(); v.team = l["team"].Int(-1); v.deaths = l["deaths"].Int(); v.leaves = l["leaves"].Int();
        v.state = l["state"].Text(); v.life = l["life"].Text();
        b.live.push_back(v);
    }
    const Json& a = x["applied"];
    b.applied = a.kind == Json::Obj;
    b.appliedLevel = b.applied ? a["level"].Text() : std::string();
    for (size_t k = 0; k < a["names"].Size(); ++k) b.appliedNames.push_back(a["names"][k].Text());
    b.pending = x["pending"].IsNull() ? -1 : x["pending"].Truthy() ? 1 : 0;
    return b;
}

std::string StateText(const VdollLive& v) {
    if (v.state == "in_game") {
        std::string life = v.life;
        for (const auto& l : kLives) if (v.life == l[0]) life = T(l[1]);
        return TF("v_vds_in_game", v.player, life.c_str(), v.deaths);
    }
    for (const auto& s : kStates) if (v.state == s[0]) return T(s[1]);
    return v.state;
}
}  // namespace

void App::LoadVdolls(std::function<void(bool)> done) {
    if (demo_) {
        // (the self-test) the longest texts: 15-letter names, every state and life, mixed teams and levels, saved settings
        // not in the running game yet, a bot of 15 virtual dolls
        if (!vdLoaded_) {
            vdLoaded_ = true;
            VdollBot a;
            a.bot = 1; a.count = 7; a.most = 7; a.teams = "auto,red,blue"; a.level = "1,10,5"; a.weapons = "svd,scorpion,auto,xm16e1";
            a.names = {"WWWWWWWWWWWWWWW", "MMMMMMMMMMMMMMM", "VDOLL3", "Big Boss 2!"};
            a.controls = "player";                                // (3.3) AS A PLAYER
            a.adapt = true; a.adaptMin = 3; a.adaptMax = 10;      // (3.4) the LIVE LEVEL on, each doll's level now (the widest numbers)
            a.levelsNow = {3.0f, 7.25f, 5.5f, 9.95f}; a.levelsNowSet = {1, 10, 5, 5}; a.appliedLevel = a.level;   // (the 4 in the game)
            a.setInGame = 7; a.inGame = 4; a.applied = true; a.appliedNames = a.names; a.pending = 1;
            const char* lives[] = {"a ghost to the round's end", "going spectator", "a spectator", "spawned"};
            for (int i = 0; i < 7; ++i) {
                VdollLive v;
                v.n = i + 1; v.player = i + 2; v.team = i == 2 ? 254 : i % 2; v.deaths = i < 2 ? 999 : i; v.leaves = i == 6 ? 99 : 0;
                v.state = i < 4 ? "in_game" : i == 4 ? "joining" : i == 5 ? "leaving" : "out";
                v.life = i < 4 ? lives[i] : "not spawned";
                a.live.push_back(v);
            }
            VdollBot b; b.bot = 2; b.most = 7;
            VdollBot c; c.bot = 3; c.count = 3; c.most = 7; c.level = "3"; c.pending = 0;
            VdollBot d; d.bot = 10; d.count = 15; d.most = 15; d.level = "10"; d.teams = "red,blue";
            vdolls_ = {a, b, c, d};
        }
        if (done) done(true);
        return;
    }
    vdLoading_ = true;
    remote_.Agent("vdolls", "", [this, done](int rc, const Json& j, const std::string&) {
        vdLoading_ = false;
        vdAt_ = ImGui::GetTime();
        if (rc != 0 || !j["ok"].Truthy()) {
            vdOld_ = j["code"].Text() == "unknown_cmd";              // (an agent before 3.2: UPDATE THE HOST)
            vdError_ = vdOld_ ? std::string() : ErrText(j);
            if (done) done(false);
            return;
        }
        vdOld_ = false; vdError_.clear(); vdLoaded_ = true;
        vdControls_ = j["vdoll_controls"].Truthy();                  // (3.3: agent 3.5 takes "controls"; an older one, CONTROLS hidden)
        vdAdapt_ = j["vdoll_adapt"].Truthy();                        // (3.4: agent 3.6 takes the live level; an older one, greyed)
        std::vector<VdollBot> v;
        for (size_t i = 0; i < j["bots"].Size(); ++i) v.push_back(ParseBot(j["bots"][i]));
        vdolls_.swap(v);
        if (done) done(true);
    });
}

void App::VdollsFill(const VdollBot& b) {
    vfBot_ = b.bot; vfFrom_ = b; vfCount_ = b.count; vfControls_ = b.controls == "player" ? 1 : 0;
    vfAdapt_ = b.adapt && vdAdapt_ ? 1 : 0; vfAdaptMin_ = vdAdapt_ ? b.adaptMin : 1; vfAdaptMax_ = vdAdapt_ ? b.adaptMax : 10;   // (an older agent: off, 1 to 10)
    const std::vector<std::string> teams = Slots(b.teams), levels = Slots(b.level), weapons = Slots(b.weapons);
    for (int i = 0; i < kMax; ++i) {
        vfTeam_[i] = TeamIndex(teams[i]);
        vfWeapon_[i] = WeaponIndex(weapons[i]);
        vfLevel_[i] = LevelOf(levels[i]);
        const std::string n = i < (int)b.names.size() && Upper(b.names[i]) != OwnName(i) ? b.names[i] : std::string();
        snprintf(vfName_[i], sizeof vfName_[i], "%s", n.c_str());
    }
}

// the form against the settings it was filled from: the keys changed, as vdoll-set takes them ("" none); (3.3) controls: 0
// DIRECT, 1 AS A PLAYER, -1 a server that does not take them (never sent); (3.4) adapt: the live level 0 OFF, 1 ON, -1 a server
// that does not take it (none of its three keys sent), amin and amax its lowest and highest level
static std::string VdollsChanges(int count, const int* team, const int* level, const int* weapon, char (*name)[16], int controls, int adapt,
                                 int amin, int amax, const VdollBot& from) {
    std::vector<std::string> t(kMax), l(kMax), w(kMax), n = NameSlots(from.names);
    for (int i = 0; i < kMax; ++i) {
        t[i] = kTeams[team[i]];
        l[i] = std::to_string(level[i]);
        w[i] = kWeapons[weapon[i]];
        if (i < count) n[i] = Trim(name[i]);                 // (past the count: the names saved, kept)
    }
    std::string js;
    auto add = [&js](const std::string& kv) { js += (js.empty() ? "{" : ",") + kv; };
    if (count != from.count) add("\"count\":" + std::to_string(count));
    if (ListOf(t) != TeamsList(from.teams)) add("\"teams\":" + JsonQuote(ListOf(t)));
    if (ListOf(l) != LevelList(from.level)) add("\"level\":" + JsonQuote(ListOf(l)));
    if (ListOf(w) != WeaponsList(from.weapons)) add("\"weapons\":" + JsonQuote(ListOf(w)));
    if (controls >= 0 && (controls ? "player" : "direct") != from.controls) add(std::string("\"controls\":") + (controls ? "\"player\"" : "\"direct\""));
    if (adapt >= 0 && (adapt != 0) != from.adapt) add(std::string("\"adapt\":") + (adapt ? "true" : "false"));
    if (adapt >= 0 && amin != from.adaptMin) add("\"adapt_min\":" + std::to_string(amin));
    if (adapt >= 0 && amax != from.adaptMax) add("\"adapt_max\":" + std::to_string(amax));
    const std::vector<std::string> names = NamesFile(n);
    if (names != NamesFile(NameSlots(from.names))) {
        std::string a;
        for (const auto& x : names) a += (a.empty() ? "" : ",") + JsonQuote(x);
        add("\"names\":[" + a + "]");
    }
    return js.empty() ? js : js + "}";
}

void App::VdollsSave(bool restart, std::function<void(bool, const std::string&)> answer) {
    const int bot = vfBot_;
    std::string js = VdollsChanges(vfCount_, vfTeam_, vfLevel_, vfWeapon_, vfName_, vdControls_ ? vfControls_ : -1, vdAdapt_ ? vfAdapt_ : -1, vfAdaptMin_, vfAdaptMax_, vfFrom_);
    if (js.empty()) js = "{}";                               // (SAVE AND RESTART alone: the saved settings now)
    // (3.4) the live level's keys alone, a running game that takes them at once: said so (the others wait for the next game)
    const bool adaptOnly = js.find("\"adapt") != std::string::npos && js.find("\"count\"") == std::string::npos && js.find("\"teams\"") == std::string::npos
                        && js.find("\"level\"") == std::string::npos && js.find("\"weapons\"") == std::string::npos
                        && js.find("\"controls\"") == std::string::npos && js.find("\"names\"") == std::string::npos;
    const char* done = restart ? "m_saved_restart" : "m_vd_saved";
    if (demo_) { if (answer) answer(true, ""); else Toast(T(done)); return; }
    vdBusy_ = true;
    remote_.Agent("vdoll-set " + std::to_string(bot) + (restart ? " --restart" : ""), js, [this, bot, done, adaptOnly, answer](int rc, const Json& j, const std::string&) {
        vdBusy_ = false;
        if (rc != 0 || !j["ok"].Truthy()) { if (answer) answer(false, ErrText(j)); else Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        const VdollBot b = ParseBot(j["vdolls"]);
        for (auto& x : vdolls_) if (x.bot == b.bot) x = b;
        if (vfBot_ == bot) VdollsFill(b);                    // (the bot still shown: its form as saved)
        if (answer) answer(true, "");
        else Toast(T(adaptOnly && j["adapt_takes_effect"].Text() == "now" ? "m_vd_adapt_now" : done));
        vdAt_ = -100;                                       // the list again at once
    });
}

// (v150-host beta 15) a bot's virtual dolls wanted now: its game reads the count within 2 s (no restart), its next games too
void App::VdollCount(int bot, int n) {
    const std::string sb = std::to_string(bot), sn = std::to_string(n);
    if (demo_) { Toast(TF("m_vdollcount", sb.c_str(), sn.c_str())); return; }
    vcBusy_ = true;
    remote_.Agent("vdoll-count " + sb + " " + sn, "", [this, sb, sn](int rc, const Json& j, const std::string&) {
        vcBusy_ = false;
        if (rc != 0 || !j["ok"].Truthy()) {
            Toast(j["code"].Text() == "unknown_cmd" ? std::string(T("e_vd_update")) : TF("m_cmd_failed", ErrText(j).c_str()), true);
            return;
        }
        const VdollBot b = ParseBot(j["vdolls"]);
        for (auto& x : vdolls_) if (x.bot == b.bot) x = b;
        Toast(TF(j["takes_effect"].Text() == "now" ? "m_vdollcount" : "m_vdollcount_next", sb.c_str(), sn.c_str()));
        vdAt_ = -100;                                       // the list again at once
    });
}

// the DOLLS page's VIRTUAL DOLLS block, apart from the PLAYABLE DOLLS': a row a bot (the count wanted, of its most, those
// in its game now), one less, one more, then what a virtual doll is
void App::VdollCountBlock() {
    if (!vdLoading_ && ImGui::GetTime() - vdAt_ > 3) LoadVdolls();
    const HostStatus st = Status();
    Gap(14);
    PanelHeader(T("p_vdollcount"));
    if (vdOld_) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", T("e_vd_update")); ImGui::PopStyleColor(); return; }
    if (!vdLoaded_ && vdolls_.empty()) { InfoRow(T("i_loading"), "..."); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rw = ImGui::GetContentRegionAvail().x, rh = 40 * S;
    const float wLess = ButtonWidth(T("b_vdolls_less")), wMore = ButtonWidth(T("b_vdolls_more"));
    auto running = [&st](int b) { for (const auto& x : st.bots) if (x.bot == b) return x.online == "online"; return false; };
    for (const auto& vb : vdolls_) {
        const int b = vb.bot, k = vb.count, most = vb.most >= 0 ? std::min(kMax, vb.most) : kMax;
        const int in = running(b) ? vb.inGame : 0;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), kRow);
        dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
        const float xb = a.x + rw - wLess - wMore - 16 * S, ty = a.y + (rh - F.row->FontSize) * 0.5f;
        const std::string label = TF("v_doll_bot", b), value = vb.most >= 0 ? TF("v_vdollcount_of", k, most, in) : TF("v_vdollcount", k, in);
        const float lw = std::min((xb - a.x) * 0.4f, SpacedWidth(F.row, F.row->FontSize, TF("v_doll_bot", 10).c_str(), RowSpacing()) + 16 * S);
        FitText(dl, F.row, ImVec2(a.x + 10 * S, ty), lw - 10 * S, kText, label.c_str(), RowSpacing());
        FitText(dl, F.row, ImVec2(a.x + lw + 10 * S, ty), xb - a.x - lw - 20 * S, k ? kText : kTextDim, value.c_str(), RowSpacing());
        ImGui::SetCursorScreenPos(ImVec2(xb, a.y + (rh - 28 * S) * 0.5f));
        const std::string idl = "vless" + std::to_string(b), idm = "vmore" + std::to_string(b);
        if (Button(idl.c_str(), T("b_vdolls_less"), T("b_vdolls_less"), T("h_vdolls_less"), k > 0 && !vcBusy_, false, wLess)) VdollCount(b, k - 1);
        ImGui::SameLine(0, 6 * S);
        if (Button(idm.c_str(), T("b_vdolls_more"), T("b_vdolls_more"), T("h_vdolls_more"), k < most && !vcBusy_, false, wMore)) VdollCount(b, k + 1);
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh));
        ImGui::Dummy(ImVec2(rw, 0));
    }
    Gap(8);
    ImGui::TextWrapped("%s", T("vdollcount_help"));
}

void App::PageVdolls() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    if (!vdLoading_ && ImGui::GetTime() - vdAt_ > 3) LoadVdolls();
    const HostStatus st = Status();
    UpdateBanner();
    const int bot = CurBot();
    if (vdOld_) {                                             // (an agent before 3.2: no vdolls)
        PanelHeader(TF("p_vd_of", bot).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", T("e_vd_update")); ImGui::PopStyleColor();
        return;
    }
    if (!vdError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", TF("m_cmd_failed", vdError_.c_str()).c_str()); ImGui::PopStyleColor();
        Gap(8);
    }
    auto running = [&st](int b) { for (const auto& x : st.bots) if (x.bot == b) return x.online == "online"; return false; };
    // every bot's (a server of several bots): how many each has, how many are in its game now; SHOW: that bot below
    if (vdolls_.size() > 1) {
        PanelHeader(T("p_vd_bots"));
        for (const auto& b : vdolls_) {
            const std::string sum = b.count == 0 ? std::string(T("v_vd_sum_none"))
                                  : running(b.bot) && b.setInGame >= 0 ? TF("v_vd_sum", b.count, b.inGame) : TF("v_vd_sum_set", b.count);
            const std::string id = "vdshow" + std::to_string(b.bot);
            if (ListRow(id.c_str(), TF("v_doll_bot", b.bot) + "   " + sum, T("b_vd_show"), T("h_vd_show"))) SelectBot(b.bot);
        }
        Gap(14);
    }
    const VdollBot* vb = nullptr;
    for (const auto& b : vdolls_) if (b.bot == bot) vb = &b;
    PanelHeader(TF("p_vd_of", bot).c_str());
    if (!vb) {                                                // (not loaded yet, or a bot the server no longer has)
        if (vdLoaded_) InfoRow(T("i_vd_now"), T("v_vd_sum_none"));
        else InfoRow(T("i_loading"), "...");
        return;
    }
    // the form follows the bot shown; settings changed elsewhere fill it again while it holds no change of its own
    if (vfBot_ != bot) VdollsFill(*vb);
    else if (VdollsChanges(vfCount_, vfTeam_, vfLevel_, vfWeapon_, vfName_, vdControls_ ? vfControls_ : -1, vdAdapt_ ? vfAdapt_ : -1, vfAdaptMin_, vfAdaptMax_, vfFrom_).empty() &&
             !VdollsChanges(vfCount_, vfTeam_, vfLevel_, vfWeapon_, vfName_, vdControls_ ? vfControls_ : -1, vdAdapt_ ? vfAdapt_ : -1, vfAdaptMin_, vfAdaptMax_, *vb).empty()) VdollsFill(*vb);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rw = ImGui::GetContentRegionAvail().x;
    // ---- those in its game now: a doll a row of two lines (its number and name, its team; what it does)
    if (!running(bot)) InfoRow(T("i_vd_now"), T("v_vd_nogame"), kTextDim);
    else if (vb->setInGame <= 0) InfoRow(T("i_vd_now"), T("v_vd_now_none"));
    else {
        InfoRow(T("i_vd_now"), TF("v_vd_of", vb->inGame, vb->setInGame).c_str());
        const bool dm = st.bot == bot && st.round >= 0 && st.round < (int)st.rules.size() && st.rules[st.round].first == 0;   // (deathmatch: no team)
        const std::vector<std::string>& shown = vb->applied ? vb->appliedNames : vb->names;
        const float rh = 50 * S, cNum = 34 * S;
        size_t inRank = 0;                                    // (3.4) levels_now: one a doll "in_game", in their numbers' order
        for (const auto& v : vb->live) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::PushID(v.n);
            ImGui::InvisibleButton("##vdlive", ImVec2(rw, rh));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), hov ? kRowHi : kRow);
            dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
            const bool in = v.state == "in_game", moving = v.state == "joining" || v.state == "leaving";
            const ImU32 col = in ? (v.life == "alive" ? kGreen : kAmber) : moving ? kAmber : kTextDim;
            const float l1 = a.y + 6 * S, l2 = a.y + rh - 7 * S - F.body->FontSize;
            StatusLight(dl, ImVec2(a.x + 16 * S, l1 + F.row->FontSize * 0.5f), 6 * S, col, v.state == "joining");
            const std::string num = std::to_string(v.n);
            FitText(dl, F.row, ImVec2(a.x + cNum, l1), 30 * S, kText, num.c_str(), RowSpacing());
            const std::string name = v.n >= 1 && v.n <= (int)shown.size() ? shown[v.n - 1] : OwnName(v.n - 1);
            const char* team = !in || dm ? nullptr : v.team == 0 ? T("v_team_red") : v.team == 1 ? T("v_team_blue") : v.team == 254 ? T("v_team_spect") : nullptr;
            float teamW = 0;
            if (team) {
                teamW = FitWidth(F.row, team, rw * 0.3f, RowSpacing());
                FitText(dl, F.row, ImVec2(a.x + rw - 10 * S - teamW, l1), teamW + 1, kText, team, RowSpacing());
            }
            FitText(dl, F.row, ImVec2(a.x + cNum + 34 * S, l1), rw - cNum - 34 * S - teamW - 24 * S, kText, name.c_str(), RowSpacing());
            const std::string what = StateText(v);
            // (3.4) its level with the live level on, as the PLAYABLE DOLL page shows a doll's: "LEVEL 10 (NOW 7.25)", at the right of
            // its second line (the level its game was given; the level now as the server last said it)
            std::string lvNow;
            float lvW = 0;
            const size_t rank = in ? inRank++ : 0;
            if (vdAdapt_ && in && rank < vb->levelsNow.size()) {
                const int set = rank < vb->levelsNowSet.size() && vb->levelsNowSet[rank] >= 1 ? std::min(10, vb->levelsNowSet[rank])
                              : LevelOf(Slots(vb->appliedLevel.empty() ? vb->level : vb->appliedLevel)[std::min(std::max(v.n, 1) - 1, kMax - 1)]);
                const float now = vb->levelsNow[rank];
                lvNow = TF("v_vd_level_now", std::to_string(set).c_str(), Dec(now, 2).c_str());
                lvW = FitWidth(F.body, lvNow.c_str(), (rw - cNum) * 0.45f, 0);
                FitText(dl, F.body, ImVec2(a.x + rw - 10 * S - lvW, l2), lvW + 1, kBlueText, lvNow.c_str(), 0);
            }
            FitText(dl, F.body, ImVec2(a.x + cNum, l2), rw - cNum - 10 * S - (lvW > 0 ? lvW + 16 * S : 0), in ? kGreenText : moving ? kAmberText : kTextDim, what.c_str(), 0);
            if (hov) SetHelp(TF("v_vd_title", v.n), name + (team ? std::string("   ") + team : std::string()) + "\n" + what +
                             (lvNow.empty() ? std::string() : "\n" + lvNow) + (v.leaves > 0 ? "\n" + TF("v_vd_leaves", v.leaves) : std::string()));
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh));
            ImGui::Dummy(ImVec2(rw, 0));
        }
    }
    if (vb->pending == 1) {                                  // saved since its game started: the next game takes them
        Gap(6);
        ImGui::PushStyleColor(ImGuiCol_Text, kBlueText); ImGui::TextWrapped("%s", T("v_vd_pending")); ImGui::PopStyleColor();
    }
    Gap(12);
    // ---- its settings: how many, then a row a doll (its name, team and level), EVERY VIRTUAL DOLL's team and level at once
    SubHeader(T("p_vd_set"));
    const int most = vb->most >= 0 ? std::min(kMax, vb->most) : kMax;
    {
        const std::string v = vb->most >= 0 ? TF("v_vd_of", vfCount_, most) : std::to_string(vfCount_);
        if (const int d = ValueRow("vdcount", T("i_vd_count"), v.c_str(), T("i_vd_count"), TF("h_vd_count", most), most > 0 || vfCount_ > 0))
            vfCount_ = vfCount_ > most ? (d < 0 ? most : 0) : Cycle(vfCount_ + d, 0, most);
    }
    // (3.3) how they turn, aim and fire: DIRECT (as before) or AS A PLAYER (within what the pad allows), for the whole bot;
    // only when the server takes it (agent 3.5), its line of help under it
    if (vdControls_) {
        if (ValueRow("vdcontrols", T("i_vd_controls"), T(vfControls_ ? "v_vd_ctl_player" : "v_vd_ctl_direct"), T("i_vd_controls"), T("h_vd_controls")))
            vfControls_ = !vfControls_;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * S);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10 * S);
        ImGui::TextUnformatted(T("vd_controls_help"));
        ImGui::PopTextWrapPos();
        Gap(6);
    }
    // (3.4) the live level, for the whole bot: OFF (each doll at its level set, as before) or ON (each doll's level follows the
    // strength of the game's real players between the LOWEST and the HIGHEST LEVEL, from its level set), taken by the running
    // game within 2 s; its line of help under it. Greyed, its reason said, when the server's agent does not take it (before 3.6)
    {
        const std::string lo = std::to_string(vfAdaptMin_), hi = std::to_string(vfAdaptMax_);
        if (ValueRow("vdadapt", T("i_vd_adapt"), T(vfAdapt_ ? "v_on" : "v_off"), T("i_vd_adapt"), T("h_vd_adapt"), vdAdapt_)) vfAdapt_ = !vfAdapt_;
        if (const int d = ValueRow("vdadaptmin", T("i_vd_adapt_min"), lo.c_str(), T("i_vd_adapt_min"), T("h_vd_adapt_min"), vdAdapt_))
            vfAdaptMin_ = Cycle(vfAdaptMin_ + d, 1, vfAdaptMax_);
        if (const int d = ValueRow("vdadaptmax", T("i_vd_adapt_max"), hi.c_str(), T("i_vd_adapt_max"), T("h_vd_adapt_max"), vdAdapt_))
            vfAdaptMax_ = Cycle(vfAdaptMax_ + d, vfAdaptMin_, 10);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * S);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10 * S);
        if (!vdAdapt_) ImGui::PushStyleColor(ImGuiCol_Text, kAmberText);
        ImGui::TextUnformatted(vdAdapt_ ? T("vd_adapt_help") : T("vd_adapt_old"));
        if (!vdAdapt_) ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        Gap(6);
    }
    // the columns: its number, its name, its team, its level (the widest value of the language in each)
    float teamW = SpacedWidth(F.row, F.row->FontSize, T("v_mixed"), RowSpacing()), levelW = teamW;
    for (const char* k : kTeamKeys) teamW = std::max(teamW, SpacedWidth(F.row, F.row->FontSize, T(k), RowSpacing()));
    levelW = std::max(levelW, SpacedWidth(F.row, F.row->FontSize, "10", RowSpacing()));
    teamW = std::max(std::max(teamW + 60 * S, 120 * S), SpacedWidth(F.small, F.small->FontSize, T("dl_team"), RowSpacing() * 0.6f) + 24 * S);
    levelW = std::max(std::max(levelW + 60 * S, 104 * S), SpacedWidth(F.small, F.small->FontSize, T("v_vd_col_level"), RowSpacing() * 0.6f) + 24 * S);
    float weaponW = SpacedWidth(F.row, F.row->FontSize, T("v_mixed"), RowSpacing());     // (beta 15) its weapon
    for (int i = 0; i < 6; ++i) weaponW = std::max(weaponW, SpacedWidth(F.row, F.row->FontSize, WeaponText(i), RowSpacing()));
    weaponW = std::max(std::max(weaponW + 60 * S, 120 * S), SpacedWidth(F.small, F.small->FontSize, T("v_vd_col_weapon"), RowSpacing() * 0.6f) + 24 * S);
    const float cName = 44 * S;
    std::string err;
    std::vector<std::string> seen;
    if (vfCount_ > 0) {
        ImVec2 a = ImGui::GetCursorScreenPos();
        const float xTeam = a.x + rw - teamW - weaponW - levelW, xWeapon = a.x + rw - weaponW - levelW, xLevel = a.x + rw - levelW;
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + 22 * S), kSub);
        FitText(dl, F.small, ImVec2(a.x + 10 * S, a.y + 4 * S), cName - 14 * S, kText, "#", 0);
        FitText(dl, F.small, ImVec2(a.x + cName + 10 * S, a.y + 4 * S), xTeam - a.x - cName - 16 * S, kText, T("dl_name"), RowSpacing() * 0.6f);
        FitText(dl, F.small, ImVec2(xTeam + 10 * S, a.y + 4 * S), teamW - 16 * S, kText, T("dl_team"), RowSpacing() * 0.6f);
        FitText(dl, F.small, ImVec2(xWeapon + 10 * S, a.y + 4 * S), weaponW - 16 * S, kText, T("v_vd_col_weapon"), RowSpacing() * 0.6f);
        FitText(dl, F.small, ImVec2(xLevel + 10 * S, a.y + 4 * S), levelW - 16 * S, kText, T("v_vd_col_level"), RowSpacing() * 0.6f);
        ImGui::Dummy(ImVec2(rw, 22 * S));
        // a value between the game's red arrows: a click on its left quarter (or a right click) goes back
        auto cell = [&](const char* id, float x0, float x1, float y, float h, const char* text, const char* title, const std::string& help) {
            ImGui::SetCursorScreenPos(ImVec2(x0, y));
            ImGui::InvisibleButton(id, ImVec2(x1 - x0, h));
            int d = 0;
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) d = ImGui::GetIO().MousePos.x < x0 + (x1 - x0) * 0.25f ? -1 : 1;
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) d = -1;
                if (d) CursorSound();
                dl->AddRectFilled(ImVec2(x0 + 2 * S, y), ImVec2(x1 - 2 * S, y + h - 2 * S), kRowHi);
                const float ay = y + h * 0.5f, s = 5 * S;
                dl->AddTriangleFilled(ImVec2(x0 + 8 * S, ay), ImVec2(x0 + 18 * S, ay - s), ImVec2(x0 + 18 * S, ay + s), kRed);
                dl->AddTriangleFilled(ImVec2(x1 - 8 * S, ay), ImVec2(x1 - 18 * S, ay - s), ImVec2(x1 - 18 * S, ay + s), kRed);
                SetHelp(title, help);
            }
            const float tw = FitWidth(F.row, text, x1 - x0 - 52 * S, RowSpacing());
            FitText(dl, F.row, ImVec2((x0 + x1 - tw) * 0.5f, y + (h - F.row->FontSize) * 0.5f), x1 - x0 - 52 * S, kText, text, RowSpacing());
            return d;
        };
        const float rh = 26 * S;
        auto row = [&](ImVec2& ra) {
            ra = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(ra, ImVec2(ra.x + rw, ra.y + rh), kRow);
            dl->AddLine(ImVec2(ra.x, ra.y + rh - 1), ImVec2(ra.x + rw, ra.y + rh - 1), kLine, 2 * S);
            for (float xx : {ra.x + cName, xTeam, xWeapon, xLevel}) dl->AddLine(ImVec2(xx, ra.y), ImVec2(xx, ra.y + rh), kLine, 2 * S);
        };
        // every virtual doll at once: their team and level when the same, else MIXED
        if (vfCount_ > 1) {
            row(a);
            FitText(dl, F.row, ImVec2(a.x + cName + 10 * S, a.y + (rh - F.row->FontSize) * 0.5f), xTeam - a.x - cName - 18 * S, kText, T("v_vd_all"), RowSpacing());
            bool sameT = true, sameL = true, sameW = true;
            for (int i = 1; i < vfCount_; ++i) { sameT = sameT && vfTeam_[i] == vfTeam_[0]; sameL = sameL && vfLevel_[i] == vfLevel_[0]; sameW = sameW && vfWeapon_[i] == vfWeapon_[0]; }
            const std::string lv = sameL ? std::to_string(vfLevel_[0]) : std::string(T("v_mixed"));
            if (const int d = cell("vdteamall", xTeam, xWeapon, a.y, rh, sameT ? T(kTeamKeys[vfTeam_[0]]) : T("v_mixed"), T("dl_team"), T("h_vd_all_team"))) {
                const int t = Cycle((sameT ? vfTeam_[0] : 0) + d, 0, 2);
                for (int& x : vfTeam_) x = t;
            }
            if (const int d = cell("vdweaponall", xWeapon, xLevel, a.y, rh, sameW ? WeaponText(vfWeapon_[0]) : T("v_mixed"), T("v_vd_col_weapon"), T("h_vd_all_weapon"))) {
                const int w = Cycle((sameW ? vfWeapon_[0] : 0) + d, 0, 5);
                for (int& x : vfWeapon_) x = w;
            }
            if (const int d = cell("vdlevelall", xLevel, a.x + rw, a.y, rh, lv.c_str(), T("v_vd_col_level"), T("h_vd_all_level"))) {
                const int l = Cycle((sameL ? vfLevel_[0] : 5) + d, 1, 10);
                for (int& x : vfLevel_) x = l;
            }
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh));
            ImGui::Dummy(ImVec2(rw, 0));
        }
        for (int i = 0; i < vfCount_; ++i) {
            row(a);
            const std::string num = std::to_string(i + 1);
            FitText(dl, F.row, ImVec2(a.x + 12 * S, a.y + (rh - F.row->FontSize) * 0.5f), cName - 16 * S, kText, num.c_str(), RowSpacing());
            // its name: typed here (empty: its own VDOLL<n>, shown greyed), checked as the server checks it
            const std::string own = OwnName(i), typed = Trim(vfName_[i]);
            const bool bad = !typed.empty() && Upper(typed) != own && !NameOk(typed, i);
            const std::string shown = Upper(typed.empty() ? own : typed);
            const bool twice = std::find(seen.begin(), seen.end(), shown) != seen.end();
            seen.push_back(shown);
            if ((bad || twice) && err.empty()) err = T("e_vd_name");
            ImGui::SetCursorScreenPos(ImVec2(a.x + cName + 4 * S, a.y + 2 * S));
            ImGui::PushID(i);
            ImGui::PushFont(F.body);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kRow);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kRowHi);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kInput);
            ImGui::SetNextItemWidth(xTeam - a.x - cName - 8 * S);
            ImGui::InputTextWithHint("##vdname", own.c_str(), vfName_[i], sizeof vfName_[i]);
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) SetHelp(T("dl_name"), T("h_vd_name"));
            ImGui::PopStyleColor(3);
            ImGui::PopFont();
            ImGui::PopID();
            if (bad || twice) dl->AddRect(ImVec2(a.x + cName + 2 * S, a.y + 1 * S), ImVec2(xTeam - 2 * S, a.y + rh - 3 * S), kRed, 0, 0, 2 * S);
            const std::string tid = "vdteam" + std::to_string(i), lid = "vdlevel" + std::to_string(i), lv = std::to_string(vfLevel_[i]);
            if (const int d = cell(tid.c_str(), xTeam, xWeapon, a.y, rh, T(kTeamKeys[vfTeam_[i]]), T("dl_team"), T("h_vd_team"))) vfTeam_[i] = Cycle(vfTeam_[i] + d, 0, 2);
            const std::string wid = "vdweapon" + std::to_string(i);
            if (const int d = cell(wid.c_str(), xWeapon, xLevel, a.y, rh, WeaponText(vfWeapon_[i]), T("v_vd_col_weapon"), T("h_vd_weapon"))) vfWeapon_[i] = Cycle(vfWeapon_[i] + d, 0, 5);
            if (const int d = cell(lid.c_str(), xLevel, a.x + rw, a.y, rh, lv.c_str(), T("v_vd_col_level"), T("h_vd_level"))) vfLevel_[i] = Cycle(vfLevel_[i] + d, 1, 10);
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh));
            ImGui::Dummy(ImVec2(rw, 0));
        }
    }
    Gap(10);
    if (vfCount_ > most && vb->most >= 0 && err.empty()) err = TF("e_vd_count", most);
    const bool changed = !VdollsChanges(vfCount_, vfTeam_, vfLevel_, vfWeapon_, vfName_, vdControls_ ? vfControls_ : -1, vdAdapt_ ? vfAdapt_ : -1, vfAdaptMin_, vfAdaptMax_, vfFrom_).empty();
    if (!err.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", err.c_str()); ImGui::PopStyleColor(); Gap(6); }
    else if (changed) { ImGui::TextWrapped("%s", T("m_unsaved")); Gap(6); }
    const bool ok = err.empty() && !vdBusy_;
    if (Button("vdsave", T("b_save"), T("b_save"), T("h_save"), ok && changed)) VdollsSave(false);
    SameLineFit(ButtonWidth(T("b_save_restart")));
    if (Button("vdsaverestart", T("b_save_restart"), T("b_save_restart"), T("h_save_restart"), ok)) Ask(T("c_restart"), [this] { VdollsSave(true); });
    SameLineFit(ButtonWidth(T("b_reload")));
    if (Button("vdreload", T("b_reload"), T("b_reload"), T("h_reload"), !vdBusy_)) { vfBot_ = 0; vdAt_ = -100; }
    if (vdBusy_) { Gap(4); ImGui::TextWrapped("%s", T("i_working")); }
    Gap(10);
    ImGui::TextWrapped("%s", T("vdolls_help"));
}
