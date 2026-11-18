#include "app.h"
#include "platform.h"
#include "sound.h"
#include "i18n.h"
#include "ui.h"
#include "imgui_internal.h"
#include "settings_sync.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ui;

static const char* const kProgramVersion = MGO1HOST_VERSION;   // (build 1.00: src/version.h)
const char* ProgramVersion() { return kProgramVersion; }
static const char* const kPageKeys[PG_COUNT] = {"m_dashboard", "m_bots", "m_dolls", "m_vdolls", "m_players", "m_report", "m_game", "m_account", "m_lists",
                                                "m_network", "m_log", "m_server", "m_options", "m_about", "m_quick"};
static const char* const kPageTitles[PG_COUNT] = {"t_dashboard", "t_bots", "t_dolls", "t_vdolls", "t_players", "t_report", "t_game", "t_account", "t_lists",
                                                  "t_network", "t_log", "t_server", "t_options", "t_about", "t_quick"};

static long long NowMs() { return plat::NowMs(); }
static std::string Up(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
static int Wrap(int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; }
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static std::string Dur(int s) {
    if (s < 0) return "-";
    if (s >= 86400) return TF("t_dhm", s / 86400, s % 86400 / 3600, s % 3600 / 60);
    if (s >= 3600) return TF("t_hm", s / 3600, s % 3600 / 60);
    if (s >= 60) return TF("t_ms", s / 60, s % 60);
    return TF("t_s", s);
}
static bool AsciiOk(const char* s) { for (const unsigned char* p = (const unsigned char*)s; *p; ++p) if (*p < 32 || *p > 126) return false; return true; }
static bool NameCharsOk(const char* s) { for (const char* p = s; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') return false; return true; }
static bool PlayerNameOk(const char* s) { const size_t n = strlen(s); return n >= 1 && n <= 16 && s[0] != '#' && s[0] != ' ' && AsciiOk(s); }
// an error of the server's program in the program's language: its code and arguments, else its text
static const std::map<std::string, const char*>& KeyLabels() {   // a settings key: its row's name on the program's pages
    static const std::map<std::string, const char*> label = {
        {"name", "g_name"}, {"comment", "g_comment"}, {"password", "g_lock"}, {"red_team", "g_red"}, {"blue_team", "g_blue"},
        {"max_players", "g_max"}, {"weapons", "g_weapons"}, {"rating_restriction", "g_rr"}, {"rating", "g_rating"},
        {"unique_characters", "g_unique"}, {"rumble_roses", "g_rumble"}, {"ghosts", "g_ghosts"}, {"friendly_fire", "g_ff"}, {"team_auto_balance", "g_bal"},
        {"auto_balance", "g_bald"}, {"idle_kick", "g_idle"}, {"idle_kick_minutes", "g_idlem"}, {"team_kill_kick", "g_tk"},
        {"team_kill_count", "g_tkc"}, {"voice_chat", "g_voice"},
        {"web", "n_web"}, {"gate", "n_gate"}, {"gate_port", "n_gate_port"}, {"stun", "n_stun"}, {"stun_port", "n_stun_port"}};
    return label;
}
// (3.5) a game setting's name as the GAME page shows it (the rotation: RULE & MAP ORDER; a rule's own: "TEAM DEATHMATCH: TIME LIMIT")
static std::string SettingLabel(const std::string& key) {
    if (key == "rotation") return T("p_order");
    const auto it = KeyLabels().find(key);
    if (it != KeyLabels().end()) return T(it->second);
    for (const GameRule& r : GameRules())
        for (int k = 0; k < RV_COUNT; ++k)
            if (r.values[k].hi > 0 && key == RuleKey(r, k)) return TF(kRuleValueKinds[k].label, RuleName(r.id).c_str());
    return key;
}
std::string ErrText(const Json& j) {
    const std::map<std::string, const char*>& label = KeyLabels();
    const std::string code = j["code"].Text();
    if (!code.empty()) {
        std::string m = "@ae_" + code;
        for (size_t i = 0; i < j["args"].Size() && i < 4; ++i) {
            std::string a = j["args"][i].Text();
            const auto it = label.find(a);
            if (it != label.end()) a = T(it->second);
            else for (const GameRule& r : GameRules())                            // a rule's setting: "TEAM DEATHMATCH: TIME LIMIT"
                for (int k = 0; k < RV_COUNT; ++k)
                    if (r.values[k].hi > 0 && a == RuleKey(r, k)) a = TF(kRuleValueKinds[k].label, RuleName(r.id).c_str());
            if (!a.empty() && a[0] == '@') a = " " + a;                           // not a message of ours
            m += "|" + a;
        }
        const std::string t = TrMsg(m);
        if (t != m.substr(1)) return t;
    }
    return TrMsg(j["error"].Text());
}
static bool Listed(const std::vector<std::string>& l, const std::string& name) { return std::find(l.begin(), l.end(), Up(name)) != l.end(); }

// ---- the game's settings, as the settings file holds them -----------------------------------
GameSettings::GameSettings() {
    for (const GameRule& r : GameRules())
        for (int k = 0; k < RV_COUNT; ++k)
            if (r.values[k].hi > 0) ruleValues[RuleKey(r, k)] = r.values[k].def;
}

int& GameSettings::Value(const GameRule& r, int kind) {
    const auto it = ruleValues.find(RuleKey(r, kind));
    return it != ruleValues.end() ? it->second : (ruleValues[RuleKey(r, kind)] = r.values[kind].def);
}

int GameSettings::ValueOf(const GameRule& r, int kind) const {
    const auto it = ruleValues.find(RuleKey(r, kind));
    return it != ruleValues.end() ? it->second : r.values[kind].def;
}

void GameSettings::FromMap(const Json& kv) {
    auto str = [&](const char* k, std::string& v) { if (kv[k].kind == Json::Str) v = kv[k].s; };
    auto num = [&](const char* k, int& v, int lo, int hi) { if (kv[k].kind == Json::Str && !kv[k].s.empty()) v = Clamp(atoi(kv[k].s.c_str()), lo, hi); };
    auto sw = [&](const char* k, bool& v) { if (kv[k].kind == Json::Str) v = kv[k].s == "on"; };
    str("name", name); str("comment", comment); str("password", password);
    if (kv["rotation"].kind == Json::Str) {
        rotation.clear();
        const std::string& r = kv["rotation"].s;
        size_t a = 0;
        while (a < r.size() && rotation.size() < 15) {
            size_t e = r.find(',', a); if (e == std::string::npos) e = r.size();
            int ru = -1, ma = -1;
            if (sscanf(r.substr(a, e - a).c_str(), " %d:%d", &ru, &ma) == 2 && RulePlays(ru, ma)) rotation.push_back({ru, ma});
            a = e + 1;
        }
        if (rotation.empty()) rotation.push_back(FirstPair());
    }
    num("red_team", red, 0, 2); num("blue_team", blue, 0, 2);
    int mp = maxJoin + 1; num("max_players", mp, 3, 16); maxJoin = mp - 1;   // (v141: above 9, a game only PCs play)
    num("weapons", weapons, 0, 7);
    for (const GameRule& r : GameRules())
        for (int k = 0; k < RV_COUNT; ++k)
            if (r.values[k].hi > 0) num(RuleKey(r, k).c_str(), Value(r, k), r.values[k].lo, r.values[k].hi);
    sw("unique_characters", unique); sw("rumble_roses", rumble); sw("ghosts", ghosts); sw("friendly_fire", ff);
    sw("team_auto_balance", autoBal); sw("idle_kick", idle); sw("team_kill_kick", tkKick); sw("voice_chat", voice);
    num("auto_balance", balanceDiff, 2, 6); num("idle_kick_minutes", idleMin, 1, 99); num("team_kill_count", tkCount, 1, 99);
    num("rating_restriction", ratingRestr, 0, 2); num("rating", rating, 100, 9900);
}

std::string GameSettings::ToJson() const {
    std::string rot;
    for (size_t i = 0; i < rotation.size(); ++i) rot += (i ? ", " : "") + std::to_string(rotation[i].first) + ":" + std::to_string(rotation[i].second);
    std::string o = "{";
    auto add = [&](const char* k, const std::string& v) { if (o.size() > 1) o += ","; o += JsonQuote(k) + ":" + JsonQuote(v); };
    auto n = [&](const char* k, int v) { add(k, std::to_string(v)); };
    auto b = [&](const char* k, bool v) { add(k, v ? "on" : "off"); };
    add("name", name); add("comment", comment); add("password", password); add("rotation", rot);
    n("red_team", red); n("blue_team", blue); n("max_players", maxJoin + 1); n("weapons", weapons);
    for (const GameRule& r : GameRules())                    // dm_minutes, tdm_minutes, tdm_rounds, tdm_tickets ...
        for (int k = 0; k < RV_COUNT; ++k)
            if (r.values[k].hi > 0) n(RuleKey(r, k).c_str(), ValueOf(r, k));
    // ((v141 beta 5) REIKO HINOMOTO/ROWDY REIKO take two unique characters' places: with them only)
    b("unique_characters", unique); b("rumble_roses", rumble && unique); b("ghosts", ghosts); b("friendly_fire", ff);
    b("team_auto_balance", autoBal); b("idle_kick", idle); b("team_kill_kick", tkKick); b("voice_chat", voice);
    n("auto_balance", balanceDiff); n("idle_kick_minutes", idleMin); n("team_kill_count", tkCount);
    n("rating_restriction", ratingRestr); n("rating", rating);
    return o + "}";
}

// game settings kept on this PC (config.h GamePreset): the password lock apart, encrypted in the file
static GameSettings GameFromPc(const GamePreset& p) {
    GameSettings g;
    Json kv;
    if (Json::Parse(p.settings, kv)) g.FromMap(kv);
    g.password = p.password;
    return g;
}

static GamePreset GameToPc(const GameSettings& g, const std::string& name) {
    GameSettings s = g;
    s.password.clear();
    return {name, s.ToJson(), g.password};
}

// ---- the application --------------------------------------------------------------------------
// the connection form from a saved server
static void FormFrom(const ServerEntry& e, char* label, char* host, char* user, char* port, char* pass, char* key, bool& useKey, bool& remember) {
    snprintf(label, 64, "%s", e.label.c_str());
    snprintf(host, 256, "%s", e.host.c_str());
    snprintf(user, 64, "%s", e.user.c_str());
    snprintf(port, 8, "%d", e.port);
    snprintf(pass, 128, "%s", e.password.c_str());
    snprintf(key, 520, "%s", e.keyPath.c_str());
    useKey = e.useKey;
    remember = e.remember;
}

// the AGENT_VERSION of the agent next to this program (server/mgo1host-agent), "" when there is none
static std::string BundledAgentVersion(const std::string& exeDir) {
    FILE* f = plat::Open(plat::Join(exeDir, "server", "mgo1host-agent"), "rb");
    if (!f) return "";
    char b[256];
    std::string v;
    for (int i = 0; i < 200 && fgets(b, sizeof b, f); ++i)
        if (!strncmp(b, "AGENT_VERSION = \"", 17)) {
            const char* e = strchr(b + 17, '"');
            if (e) v.assign(b + 17, (size_t)(e - (b + 17)));
            break;
        }
    fclose(f);
    return v;
}
// (build 1.00) the server's agent older than the one carried: src/version.h's rule (an agent before build 1.00, 3.7 the
// last, is older than every build; builds by their number), tests/panel_version_test.cpp
static bool VersionLess(const std::string& a, const std::string& b) { return hostver::Less(a, b); }

void App::Init(const std::string& dir) {
    exeDir = dir;
    bundledAgent_ = BundledAgentVersion(dir);
    cfg.Load();
    SetLang(LangFromCode(cfg.language));
    S = cfg.scale;
    reportFilter_ = cfg.reportFilter;
    snprintf(disc_, sizeof disc_, "%s", cfg.discDir.c_str());
    sound::Set(cfg.sound, cfg.soundVolume);
    sound::Init(cfg.discDir);
    // (build 1.00) no server yet: QUICK SETUP first (its account fields from the first account this PC keeps)
    if (cfg.servers.empty()) page_ = PG_QUICK;
    if (!cfg.accounts.empty()) QuickAccount(cfg.accounts[0].id.c_str(), cfg.accounts[0].password.c_str());
    if (ServerEntry* e = cfg.Current()) {
        FormFrom(*e, fLabel_, fHost_, fUser_, fPort_, fPass_, fKey_, fUseKey_, fRemember_);
        if (!e->password.empty() || e->useKey) { page_ = PG_DASHBOARD; Connect(); }
    }
}

void App::Shutdown() {
    sound::Shutdown();
    if (demo_) return;
    remote_.Stop();
    cfg.Save();
}

std::string App::ServerKey() const {
    if (demo_) return "demo:22";
    const SshConfig& c = remote_.config();
    return c.host + ":" + std::to_string(c.port);
}

std::string App::ServerName() const {
    if (demo_) return showcase_ ? "mgo1-server" : "hetzner-cx23";
    const SshConfig& c = remote_.config();
    const int i = cfg.FindServer(c.host, c.port);
    return i >= 0 ? cfg.servers[i].Name() : c.host;
}

HostStatus App::Status() const {
    if (!demo_) return remote_.status();
    HostStatus s = demoStatus_;
    if (demoBot_ == s.bot) return s;
    for (const auto& b : s.bots)                  // another bot of the demo: its line of the list
        if (b.bot == demoBot_) {
            s.bot = b.bot; s.port = b.port; s.online = b.online; s.reason = b.reason; s.service = b.service; s.enabled = b.enabled;
            s.account = b.account; s.gameName = b.name; s.maxPlayers = b.max; s.lobby = b.online == "online";
            s.players.clear(); s.kicked.clear(); s.rules.clear(); s.round = -1; s.gameAge = -1;
            s.cpu = -1; s.rssMb = -1; s.procUptime = -1;
        }
    return s;
}

static bool LoginError(const std::string& r) { return r.rfind("log-in error: ", 0) == 0; }

Link App::LinkState(std::string* msg) const {
    if (!demo_) return remote_.link(msg);
    if (msg) msg->clear();
    if (qsDemo_)                                  // (build 1.00) QUICK SETUP's states: the link as it is at that step
        return demoDialog_ == DEMO_QS_TRUST ? Link::NeedTrust : demoDialog_ == DEMO_QS_CONNECT ? Link::Connecting
             : demoDialog_ == DEMO_QS_UPLOAD || demoDialog_ == DEMO_QS_BUILD || demoDialog_ == DEMO_QS_FAILBUILD ? Link::NoAgent
             : demoDialog_ == 0 || demoDialog_ == DEMO_QS_EMPTY ? Link::Idle : Link::Connected;
    return demoDialog_ == DEMO_TRUST ? Link::NeedTrust : demoDialog_ == DEMO_KEYCHANGED ? Link::KeyChanged : Link::Connected;
}

bool App::Animating() const {
    const Link l = LinkState();
    if (l == Link::Connecting) return true;
    if (l == Link::Connected && Status().online == "starting") return true;
    if (InstallState().running) return true;
    if (qs_.running) return true;                 // (build 1.00) QUICK SETUP's steps and their time
    return ImGui::GetTime() - toastAt_ < 6.5;
}

void App::Toast(const std::string& t, bool err) { toast_ = t; toastErr_ = err; toastAt_ = ImGui::GetTime(); }

void App::Ask(const std::string& text, std::function<void()> yes) {
    askText_ = text;
    askYes_ = std::move(yes);
    askOpen_ = true;
}

void App::AgentCmd(const std::string& args, const std::string& input, const std::string& okText,
                   std::function<void(bool, const std::string&)> done, int bot) {
    if (demo_) { if (!okText.empty()) Toast(okText); return; }
    remote_.Agent(args, input, [this, okText, done](int rc, const Json& j, const std::string&) {
        const bool ok = rc == 0 && j["ok"].Truthy();
        if (ok) { if (!okText.empty()) Toast(okText); }
        else if (!done) Toast(TF("m_cmd_failed", ErrText(j).c_str()), true);
        if (done) done(ok, ok ? std::string() : ErrText(j));
    }, bot);
}

// (build 1.00) a bot's SaveMGO account: account-set (MGO1 accounts in capitals), kept on this PC first when asked (encrypted, as
// HOST ACCOUNT's KEEP ON THIS PC); HOST ACCOUNT's SAVE and QUICK SETUP's account step
void App::AccountSet(int bot, const std::string& id, const std::string& pw, bool keep, std::function<void(bool, const std::string&)> done) {
    if (keep && !pw.empty()) { cfg.KeepAccount(id, pw); if (!demo_) cfg.Save(); }
    if (demo_) { if (done) done(true, ""); return; }
    const std::string js = "{\"id\":" + JsonQuote(id) + ",\"password\":" + JsonQuote(pw) + "}";
    remote_.Agent("account-set", js, [done](int rc, const Json& j, const std::string&) {
        const bool ok = rc == 0 && j["ok"].Truthy();
        if (done) done(ok, ok ? std::string() : ErrText(j));
    }, bot);
}

// (build 1.00) the installation (or the update) from this program's server folder and the disc's folder, kept for next time:
// the SERVER page's INSTALL THE HOST, the banner's UPDATE THE HOST, QUICK SETUP's install step
void App::StartInstall(bool update) {
    installUpdate_ = update;
    cfg.discDir = disc_; cfg.Save();
    if (!demo_) { sound::Init(cfg.discDir); remote_.Install(plat::Join(exeDir, "server"), disc_); }
}

// the form's server: kept in the list (a new one added, a known one updated), then connected
void App::Connect() {
    if (demo_) return;
    checkAt_ = -100;                                  // the host's version again at once
    crashes_.clear(); crLoaded_ = crOld_ = false; crAt_ = -100; crOpen_.clear(); crLines_.clear();   // (another server's reports)
    ServerEntry e;
    e.label = fLabel_; e.host = fHost_;
    e.user = fUser_[0] ? fUser_ : "root";
    e.port = atoi(fPort_) > 0 && atoi(fPort_) < 65536 ? atoi(fPort_) : 22;
    e.useKey = fUseKey_; e.keyPath = fKey_; e.password = fPass_; e.remember = fRemember_;
    while (!e.host.empty() && e.host.back() == ' ') e.host.pop_back();
    int i = cfg.FindServer(e.host, e.port);
    if (i >= 0) { e.bot = cfg.servers[i].bot; cfg.servers[i] = e; }
    else { cfg.servers.push_back(e); i = (int)cfg.servers.size() - 1; }
    cfg.current = i;
    cfg.Save();
    SshConfig sc;
    sc.host = e.host; sc.port = e.port; sc.user = e.user; sc.useKey = e.useKey;
    sc.password = e.password; sc.keyPath = e.keyPath; sc.keyPass = fKeyPass_;
    sc.trustedFp = cfg.TrustedFp(e.host, e.port);
    gsLoaded_ = aLoaded_ = netLoaded_ = false;
    ++gsGen_; gsSync_ = gsRunning_ = gsElsewhere_ = gsVersionOdd_ = false; gsVersion_.clear(); gsPending_.clear(); gsLeftOut_.clear(); gsMapsHere_.clear();   // (3.5)
    schedWant_ = -1; schedSent_.clear();
    installedVersion_.clear();
    firewall_.clear(); firewallAt_ = -100;
    log_.clear();
    syncBusy_.clear(); syncMore_.clear(); syncAt_ = -100;
    remote_.SetBot(std::max(1, e.bot));
    remote_.Start(sc);
    report_.Show(ServerKey(), remote_.Bot());
    reportShownLoaded_ = false;
}

void App::ConnectTo(int i) {
    if (i < 0 || i >= (int)cfg.servers.size()) return;
    FormFrom(cfg.servers[i], fLabel_, fHost_, fUser_, fPort_, fPass_, fKey_, fUseKey_, fRemember_);
    fKeyPass_[0] = 0;
    if (!fUseKey_ && !fPass_[0]) { page_ = PG_SERVER; Toast(T("m_need_password"), true); return; }
    Connect();
    page_ = PG_DASHBOARD;
}

void App::Disconnect() { if (!demo_) remote_.Stop(); }

// the bot the pages show: its state, its settings, its account, its Game Report
void App::SelectBot(int bot) {
    if (bot < 1) return;
    if (demo_) { demoBot_ = bot; return; }
    if (bot == remote_.Bot()) return;
    remote_.SetBot(bot);
    if (ServerEntry* e = cfg.Current()) { e->bot = bot; cfgDirty_ = true; }
    gsLoaded_ = aLoaded_ = false;
    ++gsGen_; gsRunning_ = gsElsewhere_ = gsVersionOdd_ = false; gsVersion_.clear(); gsPending_.clear(); gsLeftOut_.clear();   // (3.5: another bot's)
    log_.clear(); logAt_ = -100;
    report_.Show(ServerKey(), bot);
    reportShownLoaded_ = false;
}

// ---- the Game Report ----------------------------------------------------------------------------
// the bot shown: its last day, for the page (the files have their own cursor)
void App::ReportFetchShown() {
    reportShownLoaded_ = true;
    const int bot = remote_.Bot();
    const long long since = NowMs() - 24LL * 3600 * 1000;
    remote_.Agent("events --since " + std::to_string(since) + " --limit 5000", "", [this, bot](int rc, const Json& j, const std::string&) {
        if (rc != 0 || !j["ok"].Truthy() || bot != remote_.Bot()) return;
        for (size_t i = 0; i < j["events"].Size(); ++i) report_.Add(j["events"][i], false);
    }, bot);
}

// every frame: the watch's events (the bot shown) to the page; the files are written from the
// server's own list, in order, from each bot's cursor: every 30 s for every bot of the server, at
// once for the bot the watch has news of
void App::ReportTick() {
    if (demo_) return;
    for (const auto& [bot, e] : remote_.TakeEvents()) {
        if (bot == remote_.Bot()) report_.Add(e, true);
        syncMore_[bot] = true;
    }
    if (LinkState() != Link::Connected) return;
    const HostStatus st = remote_.status();
    if (!st.valid) return;
    if (!reportShownLoaded_) ReportFetchShown();
    const double now = ImGui::GetTime();
    const bool all = now - syncAt_ > 30;
    if (all) syncAt_ = now;
    {
        for (const auto& b : st.bots) {
            const bool again = syncAgainAt_[b.bot] > 0 && now >= syncAgainAt_[b.bot];
            if (syncBusy_[b.bot] || !(all || syncMore_[b.bot] || again)) continue;
            syncBusy_[b.bot] = true; syncMore_[b.bot] = false; syncAgainAt_[b.bot] = 0;
            const long long cur = report_.Cursor(cfg, ServerKey(), b.bot);
            const long long since = cur > 0 ? cur : NowMs() - 32LL * 86400 * 1000;   // the first time: all the server keeps
            const std::string key = ServerKey(), name = ServerName();
            remote_.Agent("events --since " + std::to_string(since) + " --limit 2000", "", [this, bot = b.bot, key, name](int rc, const Json& j, const std::string&) {
                syncBusy_[bot] = false;
                if (rc != 0 || !j["ok"].Truthy() || key != ServerKey()) return;
                // the files get the lines older than 3 s only: the host writes a line up to a second
                // after its moment, so a newer one may still come before them
                const long long settled = (long long)j["now"].Dbl() - 3000;
                std::vector<Json> list;
                bool held = false;
                for (size_t i = 0; i < j["events"].Size(); ++i) {
                    if ((long long)j["events"][i]["t"].Dbl() <= settled) list.push_back(j["events"][i]);
                    else held = true;
                }
                if (held) syncAgainAt_[bot] = ImGui::GetTime() + 4;
                if (report_.Write(cfg, key, name, bot, list)) cfgDirty_ = true;
                if (report_.Cursor(cfg, key, bot) == 0) { cfg.reportAt[key + "#" + std::to_string(bot)] = NowMs() - 32LL * 86400 * 1000; cfgDirty_ = true; }
                syncMore_[bot] = j["more"].Truthy();
            }, b.bot);
        }
    }
    if (cfgDirty_ && now - cfgSaveAt_ > 10) { cfgSaveAt_ = now; cfgDirty_ = false; cfg.Save(); }
}

bool App::AgentOld() const {
    if (demo_) return demoAgentOld_;
    if (LinkState() != Link::Connected || bundledAgent_.empty()) return false;
    const HostStatus st = Status();
    return !st.agent.empty() && VersionLess(st.agent, bundledAgent_);
}

// (v141) the server's program older than this one's: what that costs and the update right here (the SERVER page's,
// the disc files' folder needed)
void App::StartUpdate() {
    if (disc_[0] == 0 || LocalVersion().empty()) { Toast(T("e_update_disc"), true); page_ = PG_SERVER; return; }
    Ask(T("c_update"), [this] { StartInstall(true); });
}
void App::UpdateBanner() {
    if (!AgentOld()) return;
    const HostStatus st = Status();
    PanelHeader(T("p_agent_old"));
    ImGui::PushStyleColor(ImGuiCol_Text, kRed);
    ImGui::TextWrapped("%s", TF("e_agent_old", demo_ ? "3.7" : st.agent.c_str(), demo_ ? MGO1HOST_VERSION : bundledAgent_.c_str()).c_str());
    ImGui::PopStyleColor();
    Gap(6);
    if (!demo_ && InstallState().running) {
        ImGui::TextWrapped("%s", T("i_working"));
        Gap(4);
        if (Button("agentoldsrv", T("b_goserver"), T("m_server"), T("d_server"))) page_ = PG_SERVER;
    } else if (Button("agentoldupd", T("b_update"), T("b_update"), T("h_install"))) StartUpdate();
    Gap(14);
}

std::string App::LocalVersion() const {
    FILE* f = plat::Open(plat::Join(exeDir, "server", "VERSION"), "rb");
    if (!f) return "";
    char b[128] = "";
    if (!fgets(b, sizeof b, f)) b[0] = 0;
    fclose(f);
    std::string v = b;
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
    return v;
}

std::string App::Reason(const std::string& online, const std::string& r, const std::string& service) const {
    if (online == "online") return T("r_online");
    // (beta 16) the service "activating": the launcher's wait before a start when the last ones ended early (30 s more
    // each time, 10 minutes at most: host/mgo1host.sh), not a fault
    if (service == "activating") return T("r_wait_start");
    if (r.rfind("service", 0) == 0) return service == "failed" ? T("r_failed") : T("r_stopped");
    if (r.rfind("no status", 0) == 0) return T("r_launch");
    if (r.rfind("logging in", 0) == 0) return T("r_login");
    if (r.rfind("log-in error: ", 0) == 0) return TF("r_login_error", r.substr(14).c_str());
    if (r.rfind("lobby", 0) == 0) return T("r_lobby");
    if (r.rfind("game ended", 0) == 0) return T("r_lost");
    if (r.rfind("status not updated", 0) == 0) return T("r_stale");
    return r;
}

// ---- the frame ----------------------------------------------------------------------------------
void App::Frame(float w, float h) {
    ui::Frame();
    if (!demo_) {
        remote_.Pump();
        const InstallProgress ip = remote_.install();
        if (installRunning_ && !ip.running && ip.done && ip.ok) Connect();     // the installed agent from now on
        installRunning_ = ip.running;
        ReportTick();
        QuickTick();                                  // (build 1.00) QUICK SETUP's run, whatever page is shown
        // the host's version on the server (SERVER page, the saved log's first line), every 30 s; (build 1.00) not while an
        // installation runs (the server's build: up to an hour of the one worker, these would wait in its queue)
        if (LinkState() == Link::Connected && !ip.running && ImGui::GetTime() - checkAt_ > 30) {
            checkAt_ = ImGui::GetTime();
            remote_.Agent("install-check", "", [this](int rc, const Json& j, const std::string&) { if (rc == 0) installedVersion_ = j["version"].Text(); });
        }
        // (beta 16) the crash reports, every minute (an agent without them: every 10): a bot that crashed in the last 24
        // hours says so on its row and on the dashboard
        if (LinkState() == Link::Connected && !ip.running && !crLoading_ && ImGui::GetTime() - crAt_ > (crOld_ ? 600 : 60)) LoadCrashes();
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    Chrome(w, h);
    ImGui::End();
    PlayerMenu();
    BotMenu();
    DollMenu();
    // the confirmation box
    if (askOpen_) { OpenConfirm("##ask"); askOpen_ = false; }
    const int r = Confirm("##ask", T("c_title"), askText_, T("b_yes"), T("b_no"));
    if (r == 1 && askYes_) { auto f = askYes_; askYes_ = nullptr; f(); }
    if (r == 0) askYes_ = nullptr;
    // the host key of a new or changed server
    const Link l = LinkState();
    if (l == Link::NeedTrust || l == Link::KeyChanged) {
        if (!trustShown_) { OpenConfirm("##trust"); trustShown_ = true; }
        const std::string fp = demo_ ? "SHA256:5ild3cYUc1JI7F+Q+GvB53MG1jD3fwcUZSbHNVhfzmk" : remote_.pendingFingerprint();
        const std::string text = l == Link::NeedTrust ? TF("hk_new", fp.c_str()) : TF("hk_changed", fp.c_str());
        const int t = Confirm("##trust", T(l == Link::NeedTrust ? "hk_new_title" : "hk_changed_title"), text, T(l == Link::NeedTrust ? "b_trust" : "b_accept_key"), T("b_cancel"));
        if (t >= 0) TrustAnswer(t == 1);
    }
}

void App::TrustAnswer(bool trust) {
    trustShown_ = false;
    if (demo_) return;
    if (!trust) { remote_.Stop(); return; }
    const SshConfig& c = remote_.config();
    cfg.Trust(c.host, c.port, remote_.pendingFingerprint());
    cfg.Save();
    Connect();
}

void App::Chrome(float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = 12 * S, headH = 44 * S, lineH = 7 * S, titleH = 38 * S, helpH = 104 * S, botH = 26 * S;
    const HostStatus st = Status();
    std::string linkMsg;
    const Link link = LinkState(&linkMsg);
    // the top dark band, the header band ("ONLINE MODE" in the game)
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, top), kDark);
    dl->AddRectFilled(ImVec2(0, top), ImVec2(w, top + headH), kHeader);
    dl->AddLine(ImVec2(0, top), ImVec2(w, top), kEdge, 2 * S);
    // the state on the right: a light, a word, the host's account in red (the game shows the player's name there)
    ImU32 col = kTextDim;
    const char* word = T("s_noconn");
    bool pulse = false;
    if (link == Link::Connected && st.valid) {
        if (st.online == "online") { col = kGreen; word = T("s_online"); }
        else if (st.online == "starting") { col = kAmber; word = T("s_starting"); pulse = true; }
        else { col = kRed; word = T("s_offline"); }
    } else if (link == Link::NoAgent) { col = kRed; word = T("s_notinstalled"); }
    else if (link == Link::Connecting) { col = kAmber; word = T("s_connecting"); pulse = true; }
    else if (RemoteRunning() && link != Link::Idle) { col = kRed; word = T("s_noconn"); }
    const float rowY = top + (headH - F.row->FontSize) * 0.5f;
    float x = w - 22 * S;
    if (!st.account.empty() && link == Link::Connected) {
        const std::string acc = TF("v_bot_acc", CurBot(), Up(st.account).c_str());
        const float aw = FitWidth(F.row, acc.c_str(), w * 0.2f, RowSpacing());
        x -= aw;
        FitText(dl, F.row, ImVec2(x, rowY), aw + 1, kRed, acc.c_str(), RowSpacing());
        x -= 30 * S;
    }
    const float ww = FitWidth(F.row, word, w * 0.25f, RowSpacing());
    x -= ww;
    const ImU32 wordCol = col == kTextDim ? kText : col == kGreen ? kGreenText : col == kAmber ? kAmberText : col;
    FitText(dl, F.row, ImVec2(x, rowY), ww + 1, wordCol, word, RowSpacing());
    StatusLight(dl, ImVec2(x - 18 * S, top + headH * 0.5f), 7 * S, col, pulse);
    const float rightStart = x - 34 * S;
    // the title and the SaveMGO logo on a dark plate (a click opens the portal), left of the state
    const float titleSp = LangIsCjk() ? 0 : 1.2f * S;
    const float plateW = logoTex_ ? logoW_ + 20 * S : 0;
    const float tw = FitText(dl, F.title, ImVec2(22 * S, top + (headH - F.title->FontSize) * 0.5f),
                             std::max(60 * S, rightStart - 22 * S - (plateW ? plateW + 26 * S : 0)), kText, T("app_title"), titleSp);
    if (logoTex_ && 22 * S + tw + 26 * S + plateW <= rightStart) {
        const float px = 10 * S, py = 5 * S;
        const ImVec2 a(22 * S + tw + 26 * S, top + (headH - logoH_ - 2 * py) * 0.5f), b(a.x + logoW_ + 2 * px, a.y + logoH_ + 2 * py);
        ImGui::SetCursorScreenPos(a);
        const bool click = ImGui::InvisibleButton("##savemgo", ImVec2(b.x - a.x, b.y - a.y));
        const bool hov = ImGui::IsItemHovered();
        if (click) ClickSound("savemgo");
        dl->AddRectFilled(a, b, hov ? C(0x3a3a32) : kDark, 3 * S);
        dl->AddRect(a, b, hov ? kRed : kEdge, 3 * S, 0, 1.5f * S);
        dl->AddImage(logoTex_, ImVec2(a.x + px, a.y + py), ImVec2(a.x + px + logoW_, a.y + py + logoH_));
        Note(dl, a, b, "SAVEMGO", 0, true);
        if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); SetHelp(T("lg_title"), T("h_logo")); }
        if (click) plat::OpenUrl("https://mgo2pc.com/portal/");
    }
    // the dark line, the title row: the page's title, the server in a box as the game's name box
    const float y1 = top + headH;
    dl->AddRectFilled(ImVec2(0, y1), ImVec2(w, y1 + lineH), kDark);
    const float y2 = y1 + lineH;
    dl->AddRectFilled(ImVec2(0, y2), ImVec2(w, y2 + titleH), kTitleRow);
    float boxX = w - 22 * S;
    const SshConfig& conn = demo_ ? SshConfig{showcase_ ? "mgo1-server.example" : "static.10.113.0.203.clients.your-server.de"} : remote_.config();
    if (!conn.host.empty() && RemoteRunning()) {
        const std::string srv = Up(conn.user + "@" + conn.host);
        const float tw2 = FitWidth(F.row, srv.c_str(), w * 0.45f - 40 * S, RowSpacing());
        const float bw = std::max(tw2 + 40 * S, 200 * S);
        const ImVec2 a(w - 22 * S - bw, y2 + 6 * S), b(w - 22 * S, y2 + titleH - 6 * S);
        dl->AddRect(a, b, kDark, 0, 0, 2 * S);
        Note(dl, a, b, srv.c_str(), 0, true);
        FitText(dl, F.row, ImVec2(a.x + 20 * S, a.y + (b.y - a.y - F.row->FontSize) * 0.5f), b.x - a.x - 40 * S, kText, srv.c_str(), RowSpacing());
        boxX = a.x;
    }
    FitText(dl, F.page, ImVec2(28 * S, y2 + (titleH - F.page->FontSize) * 0.5f), boxX - 48 * S, kText, T(kPageTitles[page_]), 0);
    // the body: the menu on the left, the page on the right
    const float y3 = y2 + titleH, yHelp = h - botH - helpH;
    Pattern(dl, ImVec2(0, y3), ImVec2(w, yHelp));
    dl->AddLine(ImVec2(0, y3), ImVec2(w, y3), kLine, 2 * S);
    const float menuW = 280 * S, pad = 16 * S;
    dl->AddLine(ImVec2(pad * 0.5f, y3), ImVec2(pad * 0.5f, yHelp), kLine, 2 * S);
    ImGui::SetCursorScreenPos(ImVec2(pad, y3 + pad));
    ImGui::BeginChild("##menu", ImVec2(menuW, yHelp - y3 - pad * 2), false);
    Menu();
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(pad * 2 + menuW, y3 + pad));
    ImGui::BeginChild("##page", ImVec2(w - menuW - pad * 3, yHelp - y3 - pad * 2), false);
    ImGui::PushFont(F.body);
    const bool scCut = showcase_ && (showcaseCutY_ > 0 || showcaseCutTop_ > 0);
    if (showcase_) {                                  // (build 1.00) the showcase: the page's draw list, and the page without the rows
        ImDrawList* pl = ImGui::GetWindowDrawList();  // the window's edges would cut (gui.cpp Showcase)
        const ImVec2 c0 = pl->GetClipRectMin(), c1 = pl->GetClipRectMax();
        showcaseDrawn_ = {pl, c0.y, c1.y, ImGui::GetWindowWidth()};
        if (scCut) ImGui::PushClipRect(ImVec2(c0.x, showcaseCutTop_ > 0 ? showcaseCutTop_ : c0.y), ImVec2(c1.x, showcaseCutY_ > 0 ? showcaseCutY_ : c1.y), true);
    }
    switch (page_) {
    case PG_DASHBOARD: PageDashboard(); break;
    case PG_BOTS: PageBots(); break;
    case PG_DOLLS: PageDolls(); break;
    case PG_VDOLLS: PageVdolls(); break;
    case PG_PLAYERS: PagePlayers(); break;
    case PG_REPORT: PageReport(); break;
    case PG_GAME: PageGame(); break;
    case PG_ACCOUNT: PageAccount(); break;
    case PG_LISTS: PageLists(); break;
    case PG_NETWORK: PageNetwork(); break;
    case PG_LOG: PageLog(); break;
    case PG_SERVER: PageServer(); break;
    case PG_OPTIONS: PageOptions(); break;
    case PG_ABOUT: PageAbout(); break;
    case PG_QUICK: PageQuick(); break;
    default: break;
    }
    ImGui::Dummy(ImVec2(0, 20 * S));
    if (demoScrollEnd_) ImGui::SetScrollHereY(1.0f);
    if (scCut) ImGui::PopClipRect();
    ImGui::PopFont();
    ImGui::EndChild();
    // the help area ("[Host Only] Host-Only Settings..." in the game)
    dl->AddRectFilled(ImVec2(0, yHelp), ImVec2(w, h - botH), kBg);
    dl->AddLine(ImVec2(0, yHelp), ImVec2(w, yHelp), kLine, 2 * S);
    std::string ht = HelpTitle(), hx = HelpText();
    if (ht.empty() && hx.empty()) { ht = T(kPageKeys[page_]); hx = T((std::string("d_") + (kPageKeys[page_] + 2)).c_str()); }
    dl->PushClipRect(ImVec2(0, yHelp), ImVec2(w, h - botH), true);
    if (!ht.empty()) FitText(dl, F.body, ImVec2(28 * S, yHelp + 8 * S), w - 56 * S, kText, ("[" + ht + "]").c_str(), 0);
    {
        const ImVec2 p(28 * S, yHelp + 8 * S + F.body->FontSize * 1.2f);
        dl->AddText(F.body, F.body->FontSize, p, kText, hx.c_str(), nullptr, w - 56 * S);
        const ImVec2 sz = F.body->CalcTextSizeA(F.body->FontSize, FLT_MAX, w - 56 * S, hx.c_str());
        Note(dl, p, ImVec2(p.x + sz.x, p.y + sz.y), hx.c_str());
    }
    dl->PopClipRect();
    // the bottom band: the link on the left, a message on the right
    dl->AddRectFilled(ImVec2(0, h - botH), ImVec2(w, h), kDark);
    std::string foot;
    switch (link) {
    case Link::Idle: foot = page_ == PG_QUICK ? T("f_idle_qs") : T("f_idle"); break;   // (build 1.00: QUICK SETUP's fields)
    case Link::Connecting: foot = TF("f_connecting", conn.host.c_str()); break;
    case Link::Connected: foot = TF("f_connected", conn.user.c_str(), conn.host.c_str(), conn.port, st.agent.c_str()); break;
    case Link::NeedTrust: case Link::KeyChanged: foot = T("f_trust"); break;
    case Link::AuthFailed: foot = T("f_auth"); break;
    case Link::Unreachable: foot = TF("f_unreachable", conn.host.c_str()); break;
    case Link::NoAgent: {                             // (build 1.00) the host being installed or built: that, not the button to press
        const InstallProgress ip = InstallState();
        foot = !ip.running ? T("f_noagent") : ip.phase == "build" ? T("i_ph_build") : T("i_ph_install");
        break;
    }
    case Link::Error: foot = TF("f_error", TrMsg(linkMsg).c_str()); break;
    }
    const float fy = h - botH + (botH - F.small->FontSize) * 0.5f;
    float footMax = w - 44 * S;
    if (ImGui::GetTime() - toastAt_ < 6.0 && !toast_.empty()) {
        const float tw3 = FitWidth(F.small, toast_.c_str(), w * 0.5f, 0);
        FitText(dl, F.small, ImVec2(w - 22 * S - tw3, fy), tw3 + 1, toastErr_ ? C(0xff7a6a) : C(0xd8d8c0), toast_.c_str(), 0);
        footMax -= tw3 + 24 * S;
    }
    FitText(dl, F.small, ImVec2(22 * S, fy), footMax, kLight, foot.c_str(), 0);
}

void App::Menu() {
    PanelHeader(T("p_menu"));
    // (build 1.00) QUICK SETUP first while it is wanted: no server connected (none kept, not reached, no host on it), a setup not
    // finished, its own page shown; then the pages in their order. Once a server answers, the menu is as before (the SERVER page
    // has QUICK SETUP's button), so that it fits a window of 800 pixels with the bot's block under it
    const HostStatus st = Status();
    const bool up = LinkState() == Link::Connected && st.valid;
    const bool quick = page_ == PG_QUICK || !up || (qs_.ever && !qs_.done);
    for (int k = 0; k < PG_COUNT; ++k) {
        const int i = k == 0 ? PG_QUICK : k <= PG_QUICK ? k - 1 : k;
        if (i == PG_QUICK && !quick) continue;
        if ((i == PG_DASHBOARD && quick) || i == PG_PLAYERS || i == PG_NETWORK || i == PG_OPTIONS) Gap(8);     // the server, one bot's pages, the server's, the program's
        const std::string d = std::string("d_") + (kPageKeys[i] + 2);
        if (MenuRow(kPageKeys[i], T(kPageKeys[i]), page_ == i, T(kPageKeys[i]), T(d.c_str()))) { page_ = i; cfg.lastPage = kPageKeys[i]; }
    }
    // the bot the pages show (the arrows: another bot of the server), under the menu; (build 1.00) not on QUICK SETUP (bot 1's)
    Gap(14);
    if (up && page_ != PG_QUICK) {
        PanelHeader(T("p_bot"));
        const int cur = CurBot();
        int idx = -1;
        for (size_t i = 0; i < st.bots.size(); ++i) if (st.bots[i].bot == cur) idx = (int)i;
        const std::string acc = idx >= 0 && !st.bots[idx].account.empty() ? Up(st.bots[idx].account) : std::string(T("v_noaccount"));
        const std::string label = TF("v_bot", cur);
        const int d = ValueRow("botsel", label.c_str(), acc.c_str(), T("i_bot"), T("h_botsel"), st.bots.size() > 1, 0.30f);
        if (d && !st.bots.empty()) {
            const int n = (int)st.bots.size();
            SelectBot(st.bots[((idx < 0 ? 0 : idx) + d + n) % n].bot);
        }
        int players = 0;
        for (const auto& p : st.players) if (!p.host) ++players;
        char b[64];
        snprintf(b, sizeof b, "%d / %d", players, st.maxPlayers > 1 ? st.maxPlayers - 1 : 8);
        InfoRow(T("i_players"), b);
    }
}

// ---- dashboard ----------------------------------------------------------------------------------
void App::PageDashboard() {
    const HostStatus st = Status();
    const Link link = LinkState();
    if (link != Link::Connected) {
        PanelHeader(T("p_state"));
        InfoRow(T("i_link"), link == Link::NoAgent ? T("s_notinstalled") : T("s_noconn"), kRed);
        Gap(10);
        if (Button("goserver", T("b_goserver"), T("m_server"), T("d_server"))) page_ = PG_SERVER;
        return;
    }
    UpdateBanner();
    // the state, large
    PanelHeader(TF("p_state_bot", CurBot()).c_str());
    {
        ImVec2 a = ImGui::GetCursorScreenPos();
        const float bw = ImGui::GetContentRegionAvail().x, bh = 64 * S;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + bh), kRow);
        dl->AddRect(a, ImVec2(a.x + bw, a.y + bh), kLine, 0, 0, 2 * S);
        ImU32 col = st.online == "online" ? kGreen : st.online == "starting" ? kAmber : kRed;
        const char* word = st.online == "online" ? T("s_online") : st.online == "starting" ? T("s_starting") : T("s_offline");
        StatusLight(dl, ImVec2(a.x + 34 * S, a.y + bh * 0.5f), 14 * S, col, st.online == "starting");
        const float tx = a.x + 64 * S, tmax = bw - 64 * S - 12 * S;
        FitText(dl, F.title, ImVec2(tx, a.y + 6 * S), tmax, col == kGreen ? kGreenText : col == kAmber ? kAmberText : col, word, LangIsCjk() ? 0 : 2 * S);
        FitText(dl, F.body, ImVec2(tx, a.y + 8 * S + F.title->FontSize), tmax, kText, Reason(st.online, st.reason, st.service).c_str(), 0);
        ImGui::Dummy(ImVec2(bw, bh));
    }
    CrashNote(CurBot());                          // (beta 16) its crash reports of the last 24 hours
    if (LoginError(st.reason)) {                  // what to do about it
        Gap(6);
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("%s", T("h_login_error"));
        ImGui::PopStyleColor();
    }
    Gap(12);
    PanelHeader(T("p_game"));
    int n = 0;
    for (const auto& p : st.players) if (!p.host) ++n;
    char b[256];
    InfoRow(T("i_game"), st.gameName.empty() ? "-" : st.gameName.c_str());
    if (st.round >= 0 && st.round < (int)st.rules.size()) {
        snprintf(b, sizeof b, "%d / %d   %s, %s", st.round + 1, (int)st.rules.size(), RuleName(st.rules[st.round].first).c_str(), MapName(st.rules[st.round].second));
        InfoRow(T("i_round"), b);
    } else InfoRow(T("i_round"), "-");
    snprintf(b, sizeof b, "%d / %d", n, st.maxPlayers > 1 ? st.maxPlayers - 1 : 8);
    InfoRow(T("i_players"), b);
    InfoRow(T("i_lobby"), st.lobby ? T("v_connected") : T("v_closed"), st.lobby ? kGreenText : kRed);
    InfoRow(T("i_gametime"), Dur(st.gameAge).c_str());
    Gap(12);
    PanelHeader(T("p_host"));
    snprintf(b, sizeof b, "%s", st.cpu < 0 ? "-" : TF("v_cpu", st.cpu).c_str());
    InfoRow(T("i_cpu"), b);
    snprintf(b, sizeof b, "%s", st.rssMb < 0 ? "-" : TF("v_mb", st.rssMb).c_str());
    InfoRow(T("i_mem"), b);
    InfoRow(T("i_uptime"), Dur(st.procUptime).c_str());
    snprintf(b, sizeof b, "%s", TF("v_server", Dec(st.load.empty() ? 0.0 : st.load[0], 2).c_str(), st.memAvail, st.memTotal).c_str());
    InfoRow(T("i_server"), b);
    Gap(14);
    const bool active = st.service == "active" || st.service == "activating";
    const bool ready = ImGui::GetTime() - serviceCmdAt_ > 8;      // the last command's effect shows first
    if (Button("restart", T("b_restart"), T("b_restart"), T("h_restart"), active && ready))
        Ask(T("c_restart"), [this] { serviceCmdAt_ = ImGui::GetTime(); AgentCmd("restart", "", T("m_restarting")); });
    if (active) {
        SameLineFit(ButtonWidth(T("b_stop")));
        if (Button("stop", T("b_stop"), T("b_stop"), T("h_stop"), ready, true))
            Ask(T("c_stop"), [this] { serviceCmdAt_ = ImGui::GetTime(); AgentCmd("stop", "", T("m_stopped")); });
    } else {
        SameLineFit(ButtonWidth(T("b_start")));
        if (Button("start", T("b_start"), T("b_start"), T("h_start"), ready)) { serviceCmdAt_ = ImGui::GetTime(); AgentCmd("start", "", T("m_started")); }
    }
    // the players, short
    Gap(14);
    PanelHeader(T("p_players"));
    bool any = false;
    for (const auto& p : st.players) {
        if (p.host) continue;
        any = true;
        const float rh = 26 * S;
        ImVec2 a = ImGui::GetCursorScreenPos();
        const float rw = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), kRow);
        dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
        const float pingX = a.x + rw * 0.55f;
        FitText(dl, F.row, ImVec2(a.x + 10 * S, a.y + (rh - F.row->FontSize) * 0.5f), pingX - a.x - 20 * S, kText, p.name.c_str(), RowSpacing());
        PingBars(dl, ImVec2(pingX, a.y + 4 * S), p.ping);
        snprintf(b, sizeof b, "%s", p.ping < 0 ? "-" : TF("v_ms", p.ping).c_str());
        FitText(dl, F.body, ImVec2(pingX + 32 * S, a.y + (rh - F.body->FontSize) * 0.5f), a.x + rw - pingX - 40 * S, PingColor(p.ping), b, 0);
        ImGui::Dummy(ImVec2(rw, rh));
    }
    if (!any) InfoRow(T("i_players"), T("v_noplayers"));
}

// ---- bots: the server's games, one SaveMGO account and one UDP port each ----------------------------
static const char* FirewallName(const std::string& f) { return f == "ufw" ? "ufw" : f == "firewalld" ? "firewalld" : f.empty() ? "-" : T("v_fw_none"); }

void App::PageBots() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    UpdateBanner();
    const HostStatus st = Status();
    if (!demo_ && ImGui::GetTime() - firewallAt_ > 60) {
        firewallAt_ = ImGui::GetTime();
        remote_.Agent("firewall", "", [this](int rc, const Json& j, const std::string&) { if (rc == 0) firewall_ = j["firewall"].Text(); });
    }
    PanelHeader(T("p_bots"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // a bot a row of two lines: its number, account and UDP port; then its game or its state
    const float rw = ImGui::GetContentRegionAvail().x, rh = 52 * S;
    const float actW = std::max(ButtonWidth(T("b_actions")), 120 * S);
    const float cAct = rw - actW - 4 * S, cBot = 34 * S;
    const float cAcc = cBot + std::max(90 * S, SpacedWidth(F.row, F.row->FontSize, TF("v_bot", 10).c_str(), RowSpacing()) + 16 * S);
    const float portW = FitWidth(F.body, TF("v_udp", 5739).c_str(), 200 * S, 0);
    const float cPort = cAct - 14 * S - portW;
    std::set<int> used;
    for (const auto& b : st.bots) {
        used.insert(b.port);
        ImVec2 a = ImGui::GetCursorScreenPos();
        const bool cur = b.bot == CurBot();
        ImGui::PushID(b.bot);
        const bool click = ImGui::InvisibleButton("##brow", ImVec2(cAct, rh));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (click) ClickSound("brow");
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), cur ? kRowHi : hov ? kRowHi : kRow);
        if (cur) dl->AddRect(a, ImVec2(a.x + rw, a.y + rh), kDark, 0, 0, 2 * S);
        dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
        const ImU32 col = b.online == "online" ? kGreen : b.online == "starting" ? kAmber : kRed;
        const float l1 = a.y + 6 * S, l2 = a.y + rh - 6 * S - F.body->FontSize;
        StatusLight(dl, ImVec2(a.x + 16 * S, l1 + F.row->FontSize * 0.5f), 6 * S, col, b.online == "starting");
        const std::string name = TF("v_bot", b.bot);
        FitText(dl, F.row, ImVec2(a.x + cBot, l1), cAcc - cBot - 8 * S, kText, name.c_str(), RowSpacing());
        const std::string acc = b.account.empty() ? std::string(T("v_noaccount")) : Up(b.account);
        FitText(dl, F.row, ImVec2(a.x + cAcc, l1), cPort - cAcc - 10 * S, b.account.empty() ? kRed : kText, acc.c_str(), RowSpacing());
        const std::string port = TF("v_udp", b.port);
        FitText(dl, F.body, ImVec2(a.x + cPort, l1 + (F.row->FontSize - F.body->FontSize) * 0.5f), portW + 1, kText, port.c_str(), 0);
        std::string state;
        // (v141 beta 7) a game with dolls out of SaveMGO's rankings (HOST_UNRANKED) says so
        if (b.online == "online") state = TF(b.unranked == 1 ? "v_bot_game_ur" : "v_bot_game", b.name.c_str(), b.players, b.max > 1 ? b.max - 1 : 8);
        else state = Reason(b.online, b.reason, b.service);
        const ImU32 sc = b.online == "online" ? kGreenText : b.online == "starting" ? kAmberText : kRed;
        FitText(dl, F.body, ImVec2(a.x + cBot, l2), cAct - cBot - 10 * S, sc, state.c_str(), 0);
        if (hov) SetHelp(name, LoginError(b.reason) ? T("h_login_error") : cur ? T("h_bot_current") : T("h_bot_row"));
        const ImVec2 after(a.x, a.y + rh);
        ImGui::SetCursorScreenPos(ImVec2(a.x + cAct, a.y + (rh - 28 * S) * 0.5f));
        const std::string id = "bact" + std::to_string(b.bot);
        if (Button(id.c_str(), T("b_actions"), T("b_actions"), TF("h_bot_actions", b.bot), true, false, actW)) { botMenuN_ = b.bot; botMenuOpen_ = true; }
        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy(ImVec2(rw, 0));
        CrashNote(b.bot);                                 // (beta 16) its crash reports of the last 24 hours
        if (click && !cur) SelectBot(b.bot);
    }
    if (st.bots.empty()) InfoRow(T("i_bots"), T("v_none"));
    Gap(8);
    char b[160];
    const std::string gb = Dec(st.memTotal / 1024.0, 1);
    if (st.cores == 1) snprintf(b, sizeof b, "%s", TF("v_capacity_1", gb.c_str()).c_str());
    else if (st.botsMax == 1) snprintf(b, sizeof b, "%s", TF("v_capacity_b1", st.cores, gb.c_str()).c_str());
    else snprintf(b, sizeof b, "%s", TF("v_capacity", st.cores, gb.c_str(), st.botsMax).c_str());
    InfoRow(T("i_capacity"), b, (int)st.bots.size() > st.botsMax ? kRed : 0);
    InfoRow(T("i_firewall"), FirewallName(firewall_));
    // a new bot: its SaveMGO account, its UDP port
    Gap(14);
    PanelHeader(T("p_addbot"));
    if (!cfg.accounts.empty()) {
        const int n = (int)cfg.accounts.size();
        const std::string v = nbSaved_ > 0 && nbSaved_ <= n ? cfg.accounts[nbSaved_ - 1].id : std::string(T("v_new_account"));
        const int d = ValueRow("nbsaved", T("a_saved"), v.c_str(), T("a_saved"), T("h_a_saved"));
        if (d) {
            nbSaved_ = Wrap(nbSaved_ + d, 0, n);
            if (nbSaved_ > 0) {
                snprintf(nbId_, sizeof nbId_, "%s", cfg.accounts[nbSaved_ - 1].id.c_str());
                snprintf(nbPw_, sizeof nbPw_, "%s", cfg.accounts[nbSaved_ - 1].password.c_str());
            } else { nbId_[0] = 0; plat::Wipe(nbPw_, sizeof nbPw_); }
        }
    }
    TextRow("nbid", T("a_id"), nbId_, 16, T("a_id"), T("h_a_id"), false, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    TextRow("nbpw", T("a_pw"), nbPw_, 16, T("a_pw"), T("h_nb_pw"), true, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    // the UDP port: the next free one of 5730 to 5739 (nbPort_ 0), or one picked while it is free
    std::vector<int> free;
    for (int p = 5730; p <= 5739; ++p) if (!used.count(p)) free.push_back(p);
    if (std::find(free.begin(), free.end(), nbPort_) == free.end()) nbPort_ = 0;
    const int port = nbPort_ ? nbPort_ : free.empty() ? 0 : free.front();
    {
        const std::string v = free.empty() ? std::string(T("v_none")) : TF("v_udp", port);
        const int d = ValueRow("nbport", T("i_udp_port"), v.c_str(), T("i_udp_port"), T("h_nb_port"), free.size() > 1);
        if (d && !free.empty()) {
            auto it = std::find(free.begin(), free.end(), port);
            int i = (int)(it - free.begin()) + d;
            nbPort_ = free[(i + (int)free.size()) % (int)free.size()];
        }
    }
    if (ValueRow("nbkeep", T("a_keep"), aKeep_ ? T("v_on") : T("v_off"), T("a_keep"), T("h_a_keep"))) aKeep_ = !aKeep_;
    Gap(6);
    std::string err;
    const size_t il = strlen(nbId_), pl = strlen(nbPw_);
    if ((int)st.bots.size() >= 10 || free.empty()) err = T("e_bots_full");
    else if (il < 3 || il > 15 || !NameCharsOk(nbId_)) err = T("e_acc_id");
    else if (pl < 6 || pl > 15 || !NameCharsOk(nbPw_)) err = T("e_acc_pw");
    else for (const auto& x : st.bots) if (Up(x.account) == Up(nbId_)) err = TF("e_acc_used", x.bot);
    if (!err.empty() && (il || pl)) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", err.c_str()); ImGui::PopStyleColor(); Gap(6); }
    const bool last = !cfg.lastGame.settings.empty();
    if (Button("addbot", T("b_addbot"), T("b_addbot"), last ? T("h_addbot_last") : T("h_addbot"), err.empty() && !nbBusy_)) {
        const std::string id = Up(nbId_), pw = Up(nbPw_);
        const bool keep = aKeep_;
        nbPort_ = 0;                                  // the next bot: the next free port again
        if (demo_) { nbResult_ = TF("m_fw_open", "ufw", port) + " " + TF("m_fw_provider", port); Toast(TF(last ? "m_bot_added_last" : "m_bot_added", 3, port)); }
        else {
            nbBusy_ = true; nbResult_.clear();
            const GamePreset lastGame = cfg.lastGame;
            remote_.Agent("bot-add " + std::to_string(port), "", [this, id, pw, keep, lastGame](int rc, const Json& j, const std::string&) {
                if (rc != 0 || !j["ok"].Truthy()) { nbBusy_ = false; Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
                const int n = j["bot"].Int(), p = j["port"].Int();
                const std::string fw = j["firewall"].Text();
                const bool fwOk = j["firewall_done"].Truthy();
                nbResult_ = (fw == "none" ? TF("m_fw_none", p) : fwOk ? TF("m_fw_open", fw.c_str(), p) : TF("m_fw_failed", fw.c_str(), p)) + " " + TF("m_fw_provider", p);
                // its account, then the bot started (sent: the last game settings went first; lastErr: they could not)
                auto account = [this, n, p, id, pw, keep](bool sent, const std::string& lastErr) {
                    const std::string js = "{\"id\":" + JsonQuote(id) + ",\"password\":" + JsonQuote(pw) + "}";
                    remote_.Agent("account-set", js, [this, n, p, id, pw, keep, sent, lastErr](int rc2, const Json& j2, const std::string&) {
                        if (rc2 != 0 || !j2["ok"].Truthy()) { nbBusy_ = false; Toast(TF("m_cmd_failed", ErrText(j2).c_str()), true); return; }
                        if (keep) { cfg.KeepAccount(id, pw); cfg.Save(); }
                        remote_.Agent("start", "", [this, n, p, sent, lastErr](int rc3, const Json& j3, const std::string&) {
                            nbBusy_ = false;
                            if (rc3 != 0 || !j3["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j3).c_str()), true); return; }
                            if (!lastErr.empty()) Toast(TF("m_bot_added_nolast", n, lastErr.c_str()), true);
                            else Toast(TF(sent ? "m_bot_added_last" : "m_bot_added", n, p));
                            SelectBot(n);
                        }, n);
                    }, n);
                };
                // the new bot's game: the last settings saved from this PC, before its first game
                if (lastGame.settings.empty()) { account(false, ""); return; }
                remote_.Agent("settings-set", GameFromPc(lastGame).ToJson(), [account](int rc2, const Json& j2, const std::string&) {
                    const bool ok = rc2 == 0 && j2["ok"].Truthy();
                    const std::string e = ok ? std::string() : ErrText(j2);
                    account(ok, ok ? e : e.empty() ? std::string("-") : e);
                }, n);
            });
            nbId_[0] = 0; plat::Wipe(nbPw_, sizeof nbPw_); nbSaved_ = 0;
        }
    }
    if (nbBusy_) { Gap(4); ImGui::TextWrapped("%s", T("i_working")); }
    if (!nbResult_.empty()) { Gap(6); ImGui::PushStyleColor(ImGuiCol_Text, kGreenText); ImGui::TextWrapped("%s", nbResult_.c_str()); ImGui::PopStyleColor(); }
    Gap(10);
    ImGui::TextWrapped("%s", T("bots_help"));
}

// a bot's menu: manage it, start or stop it, restart its game, remove it
void App::BotMenu() {
    if (botMenuOpen_) {
        ImGui::OpenPopup("##bmenu");
        botMenuOpen_ = false;
        if (demo_) ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x * 0.55f, 260 * S));
    }
    const HostStatus st = Status();
    const BotInfo* bi = nullptr;
    for (const auto& b : st.bots) if (b.bot == botMenuN_) bi = &b;
    const bool active = bi && (bi->service == "active" || bi->service == "activating");
    const char* items[] = {T("b_manage"), active ? T("b_stop") : T("b_start"), T("b_restart"), T("b_remove_bot"), T("b_cancel")};
    const std::string title = TF("v_bot", botMenuN_);
    float mw = 300 * S;
    for (const char* it : items) mw = std::max(mw, SpacedWidth(F.row, F.row->FontSize, it, RowSpacing()) + 50 * S);
    ImGui::SetNextWindowSize(ImVec2(mw, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kBg);
    if (ImGui::BeginPopup("##bmenu")) {
        PanelHeader(title.c_str());
        int pick = -1;
        const int n = botMenuN_;
        if (MenuRow("bm_manage", items[0], false, items[0], T("h_bm_manage"), n != CurBot())) pick = 0;
        if (MenuRow("bm_srv", items[1], false, items[1], active ? T("h_stop") : T("h_start"))) pick = 1;
        if (MenuRow("bm_restart", items[2], false, items[2], T("h_restart"), active)) pick = 2;
        if (MenuRow("bm_remove", items[3], false, items[3], T("h_bm_remove"), st.bots.size() > 1)) pick = 3;
        if (MenuRow("bm_cancel", items[4], false, items[4], "")) pick = 4;
        if (pick >= 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        auto cmd = [this, n](const std::string& args, const std::string& ok) {
            if (demo_) { Toast(ok); return; }
            remote_.Agent(args, "", [this, ok](int rc, const Json& j, const std::string&) {
                if (rc == 0 && j["ok"].Truthy()) Toast(ok); else Toast(TF("m_cmd_failed", ErrText(j).c_str()), true);
            }, n);
        };
        if (pick == 0) { SelectBot(n); page_ = PG_DASHBOARD; }
        if (pick == 1) {
            if (active) Ask(TF("c_stop_bot", n), [cmd] { cmd("stop", T("m_stopped")); });
            else cmd("start", T("m_started"));
        }
        if (pick == 2) Ask(TF("c_restart_bot", n), [cmd] { cmd("restart", T("m_restarting")); });
        if (pick == 3) Ask(TF("c_remove_bot", n), [this, n] {
            if (demo_) { Toast(TF("m_bot_removed", n)); return; }
            remote_.Agent("bot-remove " + std::to_string(n), "", [this, n](int rc, const Json& j, const std::string&) {
                if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
                Toast(TF("m_bot_removed", n));
                nbResult_.clear();                    // the last bot added's ports: maybe this one's
                remote_.DropBot(n);
                if (n == CurBot()) {
                    const HostStatus s2 = Status();
                    for (const auto& b : s2.bots) if (b.bot != n) { SelectBot(b.bot); break; }
                }
            });
        });
    }
    ImGui::PopStyleColor();
}

// ---- players ------------------------------------------------------------------------------------
void App::PagePlayers() {
    const HostStatus st = Status();
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    PanelHeader(T("p_players"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rw = ImGui::GetContentRegionAvail().x, rh = 30 * S;
    const float actW = std::max(ButtonWidth(T("b_actions")), 120 * S);
    const float cAct = rw - actW - 4 * S, avail = cAct - 10 * S;
    const float cP = 0, cName = 44 * S, cTeam = avail * 0.42f, cPing = avail * 0.57f, cKd = avail * 0.77f, cPts = avail * 0.90f;
    const float xs[] = {cP + 12 * S, cName, cTeam, cPing, cKd, cPts, avail};
    // a rule without teams (deathmatch)
    const GameRule* rule = st.round >= 0 && st.round < (int)st.rules.size() ? FindRule(st.rules[st.round].first) : nullptr;
    const bool dm = rule && !rule->teams;
    {   // the columns' names
        ImVec2 a = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + 24 * S), kSub);
        const char* heads[] = {"P", T("h_name"), T("h_team"), T("h_ping"), T("h_kd"), T("h_points")};
        for (int i = 0; i < 6; ++i)
            FitText(dl, F.small, ImVec2(a.x + xs[i], a.y + (24 * S - F.small->FontSize) * 0.5f), xs[i + 1] - xs[i] - 8 * S, kText, heads[i], RowSpacing() * 0.6f);
        ImGui::Dummy(ImVec2(rw, 24 * S));
    }
    char b[128];
    bool any = false;
    for (const auto& p : st.players) {
        ImVec2 a = ImGui::GetCursorScreenPos();
        // a click on a player's row opens its menu, as the button does
        bool open = false;
        if (!p.host) {
            ImGui::PushID(p.p);
            open = ImGui::InvisibleButton("##prow", ImVec2(cAct, rh));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            if (open) ClickSound("prow");
            dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), hov ? kRowHi : kRow);
            if (hov) SetHelp(T("b_actions"), TF("h_actions", p.name.c_str()));
            ImGui::SetCursorScreenPos(a);
        } else dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), kTitleRow);
        dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
        const float ty = a.y + (rh - F.row->FontSize) * 0.5f, by = a.y + (rh - F.body->FontSize) * 0.5f;
        snprintf(b, sizeof b, "%d", p.p);
        FitText(dl, F.row, ImVec2(a.x + xs[0], ty), xs[1] - xs[0] - 6 * S, kText, b, 0);
        FitText(dl, F.row, ImVec2(a.x + cName, ty), cTeam - cName - 8 * S, p.kicking ? kRed : kText, p.name.c_str(), RowSpacing());
        const std::string team = p.host ? T("v_host") : p.team == 254 ? T("v_spectator") : dm ? "-" : p.team == 0 ? T("v_red") : p.team == 1 ? T("v_blue") : "-";
        const ImU32 teamCol = !p.host && !dm && p.team == 0 ? kRed : !p.host && !dm && p.team == 1 ? kBlueText : kText;
        FitText(dl, F.small, ImVec2(a.x + cTeam, a.y + (rh - F.small->FontSize) * 0.5f), cPing - cTeam - 8 * S, teamCol, team.c_str(), RowSpacing() * 0.6f);
        if (!p.host) {
            PingBars(dl, ImVec2(a.x + cPing, a.y + 6 * S), p.ping);
            snprintf(b, sizeof b, "%s", p.ping < 0 ? "-" : TF("v_ms", p.ping).c_str());
            FitText(dl, F.body, ImVec2(a.x + cPing + 30 * S, by), cKd - cPing - 38 * S, PingColor(p.ping), b, 0);
            snprintf(b, sizeof b, "%d / %d", p.kills, p.deaths);
            FitText(dl, F.body, ImVec2(a.x + cKd, by), cPts - cKd - 8 * S, kText, b, 0);
            snprintf(b, sizeof b, "%d", p.points);
            FitText(dl, F.body, ImVec2(a.x + cPts, by), avail - cPts, kText, b, 0);
        }
        ImGui::Dummy(ImVec2(rw, rh));
        if (!p.host) {
            any = true;
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(a.x + cAct, a.y + 1 * S));
            const std::string id = "act" + std::to_string(p.p);
            if (Button(id.c_str(), T("b_actions"), T("b_actions"), TF("h_actions", p.name.c_str()), true, false, actW) || open) {
                menuP_ = p.p; menuName_ = p.name; menuOpen_ = true;
            }
            ImGui::SetCursorScreenPos(after);
        }
    }
    if (!any) InfoRow(T("i_players"), T("v_noplayers"));
    Gap(16);
    PanelHeader(T("p_kicks"));
    if (st.kicked.empty()) InfoRow(T("i_kicks"), T("v_none"));
    for (auto it = st.kicked.rbegin(); it != st.kicked.rend(); ++it) {
        const char* why = it->why == "black" || it->why == "ban" ? T("v_why_black") : it->why == "white" ? T("v_why_white") : T("v_why_kick");
        snprintf(b, sizeof b, "%s  (%s)", why, TF("v_ago", Dur(it->ago).c_str()).c_str());
        InfoRow(it->name.c_str(), b);
    }
}

// the game's player menu: kick, blacklist, whitelist
void App::PlayerMenu() {
    if (menuOpen_) {
        ImGui::OpenPopup("##pmenu");
        menuOpen_ = false;
        if (demo_) ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x * 0.55f, 260 * S));
    }
    const HostStatus st = Status();
    const bool white = Listed(st.white, menuName_);
    const char* items[] = {T("b_kick"), T("b_black_add"), white ? T("b_white_remove") : T("b_white_add"), T("b_cancel")};
    float mw = 280 * S;
    for (const char* it : items) mw = std::max(mw, SpacedWidth(F.row, F.row->FontSize, it, RowSpacing()) + 50 * S);
    mw = std::max(mw, SpacedWidth(F.small, F.small->FontSize, menuName_.c_str(), RowSpacing()) + 30 * S);
    ImGui::SetNextWindowSize(ImVec2(mw, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kBg);
    if (ImGui::BeginPopup("##pmenu")) {
        PanelHeader(menuName_.c_str());
        const std::string name = menuName_, pn = "P" + std::to_string(menuP_);
        int pick = -1;
        if (MenuRow("pm_kick", items[0], false, T("b_kick"), TF("h_kick", name.c_str()))) pick = 0;
        if (MenuRow("pm_black", items[1], false, T("b_black_add"), T("h_black_add"))) pick = 1;
        if (MenuRow("pm_white", items[2], false, items[2], white ? TF("h_white_remove", name.c_str()) : T("h_white_add"))) pick = 2;
        if (MenuRow("pm_cancel", items[3], false, T("b_cancel"), "")) pick = 3;
        if (pick >= 0) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (pick == 0) Ask(TF("c_kick", name.c_str()), [this, pn, name] { AgentCmd("kick " + ShellQuote(pn), "", TF("m_kicked", name.c_str())); });
        if (pick == 1) Ask(TF("c_blacklist", name.c_str()), [this, name] { AgentCmd("blacklist-add " + ShellQuote(name), "", TF("m_blacklisted", name.c_str())); });
        if (pick == 2) {
            if (white) AgentCmd("whitelist-remove " + ShellQuote(name), "", TF("m_unwhitelisted", name.c_str()));
            else AgentCmd("whitelist-add " + ShellQuote(name), "", TF("m_whitelisted", name.c_str()));
        }
    }
    ImGui::PopStyleColor();
}

// ---- the Game Report ------------------------------------------------------------------------------
void App::PageReport() {
    const char* cats[RC_COUNT] = {T("rc_all"), T("rc_fights"), T("rc_players"), T("rc_game"), T("rc_bot")};
    const int d = ValueRow("rfilter", T("r_filter"), cats[reportFilter_ >= 0 && reportFilter_ < RC_COUNT ? reportFilter_ : 0], T("r_filter"), T("h_r_filter"));
    if (d) { reportFilter_ = Wrap(reportFilter_ + d, 0, RC_COUNT - 1); cfg.reportFilter = reportFilter_; cfgDirty_ = true; }
    if (ValueRow("rpackets", T("r_packets"), cfg.reportPackets ? T("v_on") : T("v_off"), T("r_packets"), T("h_r_packets"))) { cfg.reportPackets = !cfg.reportPackets; cfgDirty_ = true; }
    const std::string folder = Report::Folder(cfg, ServerName(), CurBot());
    Gap(6);
    if (Button("ropen", T("b_open_folder"), T("b_open_folder"), T("h_open_folder")) && !demo_) {
        plat::MakeDirs(folder);
        plat::OpenFolder(folder);
    }
    Gap(4);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::TextWrapped("%s", TF("r_files_at", HideProfile(folder).c_str()).c_str());     // a long path wraps; no user name
    ImGui::PopStyleColor();
    Gap(10);
    PanelHeader(TF("p_report_bot", CurBot()).c_str());
    // the lines: the time, the sentence (a color a kind), the packets and messages behind it
    static const ImU32 catCol[RC_COUNT] = {C(0xe0e0c8), C(0xff9a88), C(0xa8e0a0), C(0xe8d8a0), C(0xb8c8e0)};
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C(0x2a2a24));
    ImGui::BeginChild("##report", ImVec2(0, std::max(240.0f * S, ImGui::GetContentRegionAvail().y - 10 * S)), false);
    report_.Relang();
    const auto& lines = report_.Lines();
    const float timeW = F.mono->CalcTextSizeA(F.mono->FontSize, FLT_MAX, 0, "00:00:00").x + 14 * S;
    int shown = 0;
    std::string day;
    for (const auto& l : lines) {
        if (reportFilter_ != RC_ALL && l.cat != reportFilter_) continue;
        ++shown;
        const std::string d = ReportClock(l.t, true).substr(0, 10);   // the date above the first line of each day
        if (d != day) {
            day = d;
            ImGui::PushFont(F.mono);
            ImGui::PushStyleColor(ImGuiCol_Text, C(0xc8b070));
            ImGui::TextUnformatted(d.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::PushFont(F.mono);
        ImGui::PushStyleColor(ImGuiCol_Text, C(0x9a9a88));
        ImGui::TextUnformatted(ReportClock(l.t, false).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(timeW);
        ImGui::PushStyleColor(ImGuiCol_Text, catCol[l.cat >= 0 && l.cat < RC_COUNT ? l.cat : 0]);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(l.text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (cfg.reportPackets && !l.details.empty()) {
            ImGui::PushFont(F.small);
            ImGui::PushStyleColor(ImGuiCol_Text, C(0x8f8f80));
            for (const auto& dt : l.details) {
                ImGui::SetCursorPosX(timeW + 12 * S);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(dt.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0, 3 * S));
    }
    if (!shown) {
        ImGui::PushStyleColor(ImGuiCol_Text, C(0xd0d0b8));
        ImGui::TextWrapped("%s", LinkState() == Link::Connected ? T("r_empty") : T("s_noconn"));
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40 * S) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---- network: the game's servers (SaveMGO's or others), the bots' UDP ports ----------------------------
static const char* const kSaveMgoWeb = "http://savemgo.com/us/mgs3/", *const kSaveMgoGate = "107.161.80.126", *const kSaveMgoStun = "15.204.66.207";
static void NetToBuffers(const NetServers& n, char* web, char* gate, char* gport, char* stun, char* sport) {
    snprintf(web, 64, "%s", n.web.c_str()); snprintf(gate, 40, "%s", n.gate.c_str()); snprintf(stun, 40, "%s", n.stun.c_str());
    snprintf(gport, 8, "%d", n.gatePort); snprintf(sport, 8, "%d", n.stunPort);
}

void App::LoadNetwork() {
    netLoading_ = true;
    remote_.Agent("servers-get", "", [this](int rc, const Json& j, const std::string&) {
        netLoading_ = false;
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        const Json& v = j["servers"];
        NetServers n;
        n.custom = v["custom"].Truthy(); n.web = v["web"].Text(); n.gate = v["gate"].Text(); n.stun = v["stun"].Text();
        n.gatePort = v["gate_port"].Int(5731); n.stunPort = v["stun_port"].Int(3478);
        net_ = netServer_ = n;
        NetToBuffers(n, nWeb_, nGate_, nGatePort_, nStun_, nStunPort_);
        netLoaded_ = true;
    });
    if (ImGui::GetTime() - firewallAt_ > 60) {
        firewallAt_ = ImGui::GetTime();
        remote_.Agent("firewall", "", [this](int rc, const Json& j, const std::string&) { if (rc == 0) firewall_ = j["firewall"].Text(); });
    }
}

static bool HostOk(const char* s) {
    const size_t n = strlen(s);
    if (!n || s[0] == '-' || s[0] == '.' || s[n - 1] == '-' || s[n - 1] == '.') return false;
    for (const char* p = s; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') return false;
    return true;
}

void App::PageNetwork() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    if (!netLoaded_) { if (!netLoading_ && !demo_) LoadNetwork(); InfoRow(T("i_loading"), "..."); return; }
    const HostStatus st = Status();
    PanelHeader(T("p_net_servers"));
    if (ValueRow("netmode", T("n_servers"), net_.custom ? T("v_custom") : T("v_savemgo"), T("n_servers"), T("h_n_servers"), true, 0.38f)) {
        net_.custom = !net_.custom;
        if (!net_.custom) {                               // SaveMGO's own values, shown as they are
            net_.web = kSaveMgoWeb; net_.gate = kSaveMgoGate; net_.stun = kSaveMgoStun; net_.gatePort = 5731; net_.stunPort = 3478;
            NetToBuffers(net_, nWeb_, nGate_, nGatePort_, nStun_, nStunPort_);
        }
    }
    if (net_.custom) {
        TextRow("nweb", T("n_web"), nWeb_, 43, T("n_web"), T("h_n_web"), false, 0.62f, ImGuiInputTextFlags_CharsNoBlank);
        TextRow("ngate", T("n_gate"), nGate_, 29, T("n_gate"), T("h_n_gate"), false, 0.62f, ImGuiInputTextFlags_CharsNoBlank);
        TextRow("ngport", T("n_gate_port"), nGatePort_, 6, T("n_gate_port"), T("h_n_port"), false, 0.62f, ImGuiInputTextFlags_CharsDecimal);
        TextRow("nstun", T("n_stun"), nStun_, 27, T("n_stun"), T("h_n_stun"), false, 0.62f, ImGuiInputTextFlags_CharsNoBlank);
        TextRow("nsport", T("n_stun_port"), nStunPort_, 6, T("n_stun_port"), T("h_n_port"), false, 0.62f, ImGuiInputTextFlags_CharsDecimal);
        net_.web = nWeb_; net_.gate = nGate_; net_.stun = nStun_; net_.gatePort = atoi(nGatePort_); net_.stunPort = atoi(nStunPort_);
    } else {
        InfoRow(T("n_web"), net_.web.c_str(), 0, 0.38f);
        InfoRow(T("n_gate"), (net_.gate + " : " + std::to_string(net_.gatePort)).c_str(), 0, 0.38f);
        InfoRow(T("n_stun"), (net_.stun + " : " + std::to_string(net_.stunPort)).c_str(), 0, 0.38f);
    }
    Gap(8);
    std::string err;
    if (net_.custom) {
        if (net_.web.rfind("http://", 0) != 0 && net_.web.rfind("https://", 0) != 0) err = T("e_n_web");
        else if (!HostOk(nGate_)) err = T("e_n_gate");
        else if (!HostOk(nStun_)) err = T("e_n_stun");
        else if (net_.gatePort < 1 || net_.gatePort > 65535 || net_.stunPort < 1 || net_.stunPort > 65535) err = T("e_n_port");
    }
    auto same = [](const NetServers& a, const NetServers& b) {
        return a.custom == b.custom && (!a.custom || (a.web == b.web && a.gate == b.gate && a.stun == b.stun && a.gatePort == b.gatePort && a.stunPort == b.stunPort));
    };
    const bool changed = !same(net_, netServer_);
    if (!err.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", err.c_str()); ImGui::PopStyleColor(); Gap(6); }
    else if (changed) { ImGui::TextWrapped("%s", T("m_net_unsaved")); Gap(6); }
    if (Button("netsave", T("b_net_save"), T("b_net_save"), T("h_net_save"), err.empty() && changed)) {
        Ask(T("c_net_save"), [this] {
            std::string js = "{\"custom\":" + std::string(net_.custom ? "true" : "false");
            if (net_.custom)
                js += ",\"web\":" + JsonQuote(net_.web) + ",\"gate\":" + JsonQuote(net_.gate) + ",\"gate_port\":" + std::to_string(net_.gatePort) +
                      ",\"stun\":" + JsonQuote(net_.stun) + ",\"stun_port\":" + std::to_string(net_.stunPort);
            js += "}";
            if (demo_) { Toast(T("m_net_saved")); return; }
            remote_.Agent("servers-set --restart", js, [this](int rc, const Json& j, const std::string&) {
                if (rc == 0 && j["ok"].Truthy()) { netServer_ = net_; Toast(T("m_net_saved")); } else Toast(TF("m_cmd_failed", ErrText(j).c_str()), true);
            });
        });
    }
    SameLineFit(ButtonWidth(T("b_restore_savemgo")));
    if (Button("netsavemgo", T("b_restore_savemgo"), T("b_restore_savemgo"), T("h_restore_savemgo"), net_.custom)) {
        net_.custom = false; net_.web = kSaveMgoWeb; net_.gate = kSaveMgoGate; net_.stun = kSaveMgoStun; net_.gatePort = 5731; net_.stunPort = 3478;
        NetToBuffers(net_, nWeb_, nGate_, nGatePort_, nStun_, nStunPort_);
    }
    Gap(10);
    ImGui::TextWrapped("%s", T("n_help"));
    // the bots' UDP ports, and the firewalls
    Gap(14);
    PanelHeader(T("p_net_ports"));
    for (const auto& b : st.bots) {
        const std::string label = TF("v_bot", b.bot), v = TF("v_udp", b.port);
        InfoRow(label.c_str(), v.c_str());
    }
    InfoRow(T("i_firewall"), FirewallName(firewall_));
    Gap(6);
    ImGui::TextWrapped("%s", T("n_ports_help"));
}

// ---- game settings ------------------------------------------------------------------------------
// (3.5, v150-host beta 22) the owner's report of 13 November: a change of the maps and modes was "pushed back". 3.4 filled the
// form with every settings-get's answer as it came: a RELOAD answered after a SAVE sent behind it put the older copy back in
// the form (and SAVE again wrote it to the server), an answer for the bot shown before a switch filled the other bot's page.
// Now an answer fills the form only when it is the last SAVE or RELOAD sent for the bot shown, and the page's own look every
// 10 s (agent 3.7) refreshes the form only when it holds nothing unsaved.
void App::LoadSettings(bool fill) {
    const int bot = CurBot();
    const unsigned gen = fill ? ++gsGen_ : gsGen_;
    (fill ? gsLoading_ : gsInfoLoading_) = true;
    gsInfoAt_ = ImGui::GetTime();
    remote_.Agent("settings-get", "", [this, fill, bot, gen](int rc, const Json& j, const std::string&) {
        (fill ? gsLoading_ : gsInfoLoading_) = false;
        gsInfoAt_ = ImGui::GetTime();
        if (bot != CurBot() || gen != gsGen_) return;            // another bot shown, or a SAVE or RELOAD sent since
        if (rc != 0 || !j["ok"].Truthy()) { if (fill) Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        GameSettings g;
        g.FromMap(j["settings"]);
        // (sync2, the review's S3) the version only as small hex digits (it goes into SAVE's command line): an odd one is none
        const std::string raw = j["version"].Text(), version = gsync::Version(raw);
        gsVersionOdd_ = !raw.empty() && version.empty();
        const bool untouched = gsLoaded_ && gs_ == gsServer_;    // the form holds nothing unsaved
        gsync::Ask ask; ask.bot = bot; ask.gen = gen; ask.fill = fill;
        if (gsync::Fills(ask, CurBot(), gsGen_, untouched, version, gsVersion_)) {   // (settings_sync.h; changed elsewhere: shown)
            FillGame(g);
            gsServer_ = g;
            gsVersion_ = version;
            gsElsewhere_ = false;
            gsLoaded_ = true;
            gsError_.clear();
        } else if (!version.empty() && version != gsVersion_) gsElsewhere_ = true;   // (unsaved changes kept; SAVE asks first)
        SettingsInfo(j);
    }, bot);
}

void App::SettingsInfo(const Json& j) {
    gsSync_ = j["settings_sync"].Truthy();
    if (!gsSync_) return;
    gsRunning_ = j["running"].Truthy();
    gsReal_ = j["real_players"].Int();
    gsPending_.clear(); gsLeftOut_.clear(); gsMapsHere_.clear();
    for (size_t i = 0; i < j["pending"].Size(); ++i) gsPending_.push_back(j["pending"][i].Text());
    for (size_t i = 0; i < j["left_out"].Size(); ++i) gsLeftOut_.push_back(j["left_out"][i].Text());
    for (size_t i = 0; i < j["maps_here"].Size(); ++i) gsMapsHere_.push_back(j["maps_here"][i].Int());
}

bool App::MapHere(int map) const {
    if (demo_ && !showcase_) return map != 7;                    // (the self-test: one map of the rotation not on the server)
    return gsMapsHere_.empty() || std::find(gsMapsHere_.begin(), gsMapsHere_.end(), map) != gsMapsHere_.end();
}

// SAVE, SAVE AND RESTART: settings-set for the bot shown, with --base the version of the copy the form came from (agent 3.7):
// a copy changed on the server since (another MGO1 Host, the shell) is not replaced without asking (force: replaced)
void App::SaveSettings(bool restart, bool force, std::function<void(bool, const std::string&)> answer) {
    const GameSettings sent = gs_;
    const int bot = CurBot();
    const char* done = restart ? "m_saved_restart" : "m_saved";
    if (demo_) { if (answer) answer(true, ""); else Toast(T(done)); return; }
    const unsigned gen = ++gsGen_;                               // (a settings-get answered after this fills nothing)
    std::string args = restart ? "settings-set --restart" : "settings-set";
    // (sync2, the review's S3) --base only with a version of small hex digits (gsync::VersionOk); a version the server sent that
    // is not one: saved without the check of a copy changed elsewhere, and said (m_gs_no_base)
    if (gsSync_ && !force && gsync::VersionOk(gsVersion_)) args += " --base " + gsVersion_;
    const bool noBase = gsSync_ && !force && !gsync::VersionOk(gsVersion_) && gsVersionOdd_;
    remote_.Agent(args, sent.ToJson(), [this, sent, bot, done, restart, gen, noBase, answer](int rc, const Json& j, const std::string&) {
        if (j["code"].Text() == "settings_changed") {
            if (answer) { answer(false, ErrText(j)); return; }
            if (bot == CurBot()) { gsElsewhere_ = true; Ask(T("c_replace_settings"), [this, restart] { SaveSettings(restart, true); }); }
            return;
        }
        if (rc != 0 || !j["ok"].Truthy()) { if (answer) answer(false, ErrText(j)); else Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        if (bot == CurBot()) {
            gsServer_ = sent;
            const std::string rv = j["version"].Text();
            if (!rv.empty()) { gsVersion_ = gsync::Version(rv); gsVersionOdd_ = gsVersion_.empty(); }
            gsElsewhere_ = false;
            if (gen == gsGen_) SettingsInfo(j);
        }
        GameSaved(sent);
        if (answer) answer(true, "");
        else Toast(T(noBase ? "m_gs_no_base" : j["takes_effect"].Text() == "next game" ? "m_saved_next" : done));
    }, bot);
}

void App::FillGame(const GameSettings& g) {
    gs_ = g;
    snprintf(gName_, sizeof gName_, "%s", g.name.c_str());
    snprintf(gComment_, sizeof gComment_, "%s", g.comment.c_str());
    snprintf(gPass_, sizeof gPass_, "%s", g.password.c_str());
}

// settings-set done for a bot: the page's copy of the server's, and this PC's last game settings
void App::GameSaved(const GameSettings& g) {
    cfg.lastGame = GameToPc(g, "");
    if (!demo_) cfg.Save();
}

// a map only the PC players have (the self-test: one of the longest names with its tag)
bool App::PcOnlyMap(int map) const {
    const GameMap* m = FindMap(map);
    return (m && m->pcOnly) || (demo_ && !showcase_ && map == 7);
}

void App::PageGame() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    if (!gsLoaded_) { if (!gsLoading_) LoadSettings(); InfoRow(T("i_loading"), "..."); return; }
    // (3.5) every 10 s while the page is shown, with agent 3.7: what the running game plays, the server's copy's version
    if (!demo_ && gsSync_ && !gsLoading_ && !gsInfoLoading_ && ImGui::GetTime() - gsInfoAt_ > 10) LoadSettings(false);
    GameSettings& g = gs_;
    // ((v141 beta 6) a value out of the row's range, an older file's 15 players: back into it at the nearer end)
    auto num = [&](const char* id, const char* label, int& v, int lo, int hi, const std::string& text, const std::string& help) {
        const int d = ValueRow(id, label, text.c_str(), label, help);
        if (d) v = v > hi ? (d < 0 ? hi : lo) : v < lo ? (d > 0 ? lo : hi) : Wrap(v + d, lo, hi);
    };
    auto sw = [&](const char* id, const char* label, bool& v, const std::string& help) {
        if (ValueRow(id, label, v ? T("v_on") : T("v_off"), label, help)) v = !v;
    };
    // Create Game 1
    PanelHeader(T("p_basic"));
    TextRow("name", T("g_name"), gName_, 16, T("g_name"), T("h_g_name"));
    TextRow("lock", T("g_lock"), gPass_, 16, T("g_lock"), T("h_g_lock"), false, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    TextRow("comment", T("g_comment"), gComment_, 76, T("g_comment"), T("h_g_comment"));
    g.name = gName_; g.password = gPass_; g.comment = gComment_;
    Gap(12);
    // Create Game 2
    PanelHeader(T("p_order"));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rw = ImGui::GetContentRegionAvail().x;
        ImVec2 a = ImGui::GetCursorScreenPos();
        const float hx1 = a.x + 60 * S, hx2 = hx1 + (rw - 60 * S - 40 * S) * 0.5f;
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + 22 * S), kSub);
        FitText(dl, F.small, ImVec2(a.x + 10 * S, a.y + 4 * S), hx1 - a.x - 14 * S, kText, T("h_order"), RowSpacing() * 0.6f);
        FitText(dl, F.small, ImVec2(hx1 + 10 * S, a.y + 4 * S), hx2 - hx1 - 16 * S, kText, T("h_rule"), RowSpacing() * 0.6f);
        FitText(dl, F.small, ImVec2(hx2 + 10 * S, a.y + 4 * S), a.x + rw - 40 * S - hx2 - 16 * S, kText, T("h_map"), RowSpacing() * 0.6f);
        ImGui::Dummy(ImVec2(rw, 22 * S));
        int remove = -1;
        for (int i = 0; i < (int)g.rotation.size(); ++i) {
            const float rh = 26 * S;
            a = ImGui::GetCursorScreenPos();
            const float x1 = a.x + 60 * S, x2 = x1 + (rw - 60 * S - 40 * S) * 0.5f, x3 = a.x + rw - 40 * S;
            char id[32];
            dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), kRow);
            dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
            for (float xx : {x1, x2, x3}) dl->AddLine(ImVec2(xx, a.y), ImVec2(xx, a.y + rh), kLine, 2 * S);
            char num[16]; snprintf(num, sizeof num, "%02d", i + 1);
            FitText(dl, F.row, ImVec2(a.x + 16 * S, a.y + (rh - F.row->FontSize) * 0.5f), x1 - a.x - 20 * S, kText, num, 0);
            // the rule and the map cells: a click changes them (the left arrow or a right click: back); the
            // rules and the maps each plays on come from the table (rules.h)
            for (int cell = 0; cell < 2; ++cell) {
                const float cx0 = cell ? x2 : x1, cx1 = cell ? x3 : x2;
                ImGui::SetCursorScreenPos(ImVec2(cx0, a.y));
                snprintf(id, sizeof id, "rot%d_%d", i, cell);
                ImGui::InvisibleButton(id, ImVec2(cx1 - cx0, rh));
                const bool hov = ImGui::IsItemHovered();
                const bool pc = cell == 1 && PcOnlyMap(g.rotation[i].second);
                int d = 0;
                if (hov) {
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) d = ImGui::GetIO().MousePos.x < cx0 + 30 * S ? -1 : 1;
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) d = -1;
                    if (d) CursorSound();
                    dl->AddRectFilled(ImVec2(cx0 + 2 * S, a.y), ImVec2(cx1 - 2 * S, a.y + rh - 2 * S), kRowHi);
                    const float ay = a.y + rh * 0.5f, s = 5 * S;
                    dl->AddTriangleFilled(ImVec2(cx0 + 8 * S, ay), ImVec2(cx0 + 18 * S, ay - s), ImVec2(cx0 + 18 * S, ay + s), kRed);
                    dl->AddTriangleFilled(ImVec2(cx1 - 8 * S, ay), ImVec2(cx1 - 18 * S, ay - s), ImVec2(cx1 - 18 * S, ay + s), kRed);
                    if (cell == 0) SetHelp(T("h_rule"), T("h_g_rule"));
                    else SetHelp(T("h_map"), std::string(T("h_g_map")) + (pc ? std::string(" ") + T("h_g_map_pc") : std::string()) +
                                             (MapHere(g.rotation[i].second) ? std::string() : std::string(" ") + T("h_g_map_missing")));
                }
                std::pair<int, int>& line = g.rotation[i];
                if (cell == 0 && d) {                     // the map kept when the new rule is played on it, else its first
                    line.first = NextRule(line.first, d);
                    const std::vector<int> maps = RuleMaps(line.first);
                    if (!RulePlays(line.first, line.second) && !maps.empty()) line.second = maps.front();
                }
                if (cell == 1 && d) line.second = NextMap(line.first, line.second, d);
                const std::string txt = cell ? MapName(line.second) : RuleName(line.first);
                float tw = cx1 - cx0 - 50 * S;
                if (cell == 1 && PcOnlyMap(line.second)) {    // a map only the PC players have: its tag on the right
                    const char* tag = T("v_pc_map");
                    const float tagW = FitWidth(F.small, tag, 80 * S, 0);
                    FitText(dl, F.small, ImVec2(cx1 - 24 * S - tagW, a.y + (rh - F.small->FontSize) * 0.5f), tagW + 1, kBlueText, tag, 0);
                    tw -= tagW + 8 * S;
                }
                // (3.5) a map this server does not have (agent 3.7's maps_here): its name in red, the reason in its help and
                // under the form (e_rot_missing)
                const ImU32 col = cell == 1 && !MapHere(line.second) ? kRed : kText;
                FitText(dl, F.row, ImVec2(cx0 + 26 * S, a.y + (rh - F.row->FontSize) * 0.5f), tw, col, txt.c_str(), RowSpacing());
            }
            // remove this line
            ImGui::SetCursorScreenPos(ImVec2(x3, a.y));
            snprintf(id, sizeof id, "rotdel%d", i);
            if (ImGui::InvisibleButton(id, ImVec2(40 * S, rh)) && g.rotation.size() > 1) { remove = i; ClickSound("cancel_line"); }
            const bool dh = ImGui::IsItemHovered();
            if (dh) SetHelp(T("b_remove"), T("h_g_remove"));
            const ImU32 xc = g.rotation.size() > 1 ? (dh ? kRed : kText) : kTextDim;
            const float cx = x3 + 20 * S, cy = a.y + rh * 0.5f, r = 5 * S;
            dl->AddLine(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), xc, 2.5f * S);
            dl->AddLine(ImVec2(cx - r, cy + r), ImVec2(cx + r, cy - r), xc, 2.5f * S);
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh));
            ImGui::Dummy(ImVec2(rw, 0));
        }
        if (remove >= 0) g.rotation.erase(g.rotation.begin() + remove);
        Gap(6);
        if (Button("addrot", T("b_add"), T("b_add"), T("h_g_add"), g.rotation.size() < 15)) g.rotation.push_back(g.rotation.back());
    }
    Gap(12);
    // Create Game 3
    PanelHeader(T("p_details"));
    SubHeader(T("p_shared"));
    if (ValueRow("hostonly", T("g_hostonly"), T("v_on"), T("g_hostonly"), T("h_g_hostonly"), false)) {}
    char b[64];
    num("red", T("g_red"), g.red, 0, 2, T((std::string("team_") + std::to_string(g.red)).c_str()), T("h_g_team"));
    num("blue", T("g_blue"), g.blue, 0, 2, T((std::string("team_") + std::to_string(g.blue)).c_str()), T("h_g_team"));
    if (ValueRow("unique", T("g_unique"), g.unique ? T("v_appear") : T("v_noappear"), T("g_unique"), T("h_g_unique"))) {
        g.unique = !g.unique;
        if (!g.unique) g.rumble = false;
    }
    // (v141 beta 5) the game's REIKO HINOMOTO/ROWDY REIKO (its Create Game's "???", rumble_roses): they take two unique
    // characters' places, so they come with the unique characters (on: those on too; the unique characters off: they go)
    if (ValueRow("rumble", T("g_rumble"), g.rumble && g.unique ? T("v_appear") : T("v_noappear"), T("g_rumble"), T("h_g_rumble"))) {
        g.rumble = !(g.rumble && g.unique);
        if (g.rumble) g.unique = true;
    }
    // the game's own HOST SETTINGS row: 2 to 8 players, the dedicated host not counted (its slot added to max_players by
    // ToMap: 8 here is the game's 9). (v141 beta 6) the only player-count row: the list's 00/08 is the host's own business
    snprintf(b, sizeof b, "%s", TF("v_players", g.maxJoin).c_str());
    num("max", T("g_max"), g.maxJoin, 2, 8, b, T("h_g_max"));
    num("rr", T("g_rr"), g.ratingRestr, 0, 2, T((std::string("rr_") + std::to_string(g.ratingRestr)).c_str()), T("h_g_rr"));
    {
        snprintf(b, sizeof b, "%d", g.rating);
        const int d = ValueRow("rating", T("g_rating"), b, T("g_rating"), T("h_g_rating"), g.ratingRestr != 0);
        if (d) g.rating = Clamp(g.rating + d * 100, 100, 9900);
    }
    num("weapons", T("g_weapons"), g.weapons, 0, 7, T((std::string("weap_") + std::to_string(g.weapons)).c_str()), T("h_g_weapons"));
    sw("ff", T("g_ff"), g.ff, T("h_g_ff"));
    sw("tk", T("g_tk"), g.tkKick, T("h_g_tk"));
    if (g.tkKick) { snprintf(b, sizeof b, "%d", g.tkCount); num("tkc", T("g_tkc"), g.tkCount, 1, 99, b, T("h_g_tkc")); }
    sw("idle", T("g_idle"), g.idle, T("h_g_idle"));
    if (g.idle) { snprintf(b, sizeof b, "%s", TF("v_min", g.idleMin).c_str()); num("idlem", T("g_idlem"), g.idleMin, 1, 99, b, T("h_g_idlem")); }
    sw("bal", T("g_bal"), g.autoBal, T("h_g_bal"));
    if (g.autoBal) { snprintf(b, sizeof b, "%d", g.balanceDiff); num("bald", T("g_bald"), g.balanceDiff, 2, 6, b, T("h_g_bald")); }
    sw("voice", T("g_voice"), g.voice, T("h_g_voice"));
    sw("ghosts", T("g_ghosts"), g.ghosts, T("h_g_ghosts"));
    Gap(12);
    PanelHeader(T("p_rules"));
    for (const GameRule& r : GameRules()) {           // each rule's rows: its time, rounds, tickets (rules.h)
        const std::string rn = RuleName(r.id);
        for (int k = 0; k < RV_COUNT; ++k) {
            const RuleValue& rv = r.values[k];
            if (rv.hi <= 0) continue;
            int& v = g.Value(r, k);
            const RuleValueKind& kind = kRuleValueKinds[k];
            const std::string id = std::string(r.prefix) + kind.row, label = TF(kind.label, rn.c_str());
            num(id.c_str(), label.c_str(), v, rv.lo, rv.hi, k == RV_MINUTES ? TF("v_min", v) : std::to_string(v), T(kind.help));
        }
    }
    Gap(14);
    // the checks the server makes too
    gsError_.clear();
    if (strlen(gName_) < 3) gsError_ = T("e_name");
    else if (!AsciiOk(gName_) || !AsciiOk(gComment_)) gsError_ = T("e_ascii");
    else if (gPass_[0] && !NameCharsOk(gPass_)) gsError_ = T("e_lock");
    else if (g.red == g.blue) gsError_ = T("e_teams");
    else for (const auto& r : g.rotation)                 // (3.5) a map this server does not have (agent 3.7 refuses it too)
        if (!MapHere(r.second)) { gsError_ = TF("e_rot_missing", MapName(r.second)); break; }
    const bool changed = !(gs_ == gsServer_);
    if (!gsError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("%s", gsError_.c_str());
        ImGui::PopStyleColor();
        Gap(6);
    } else if (changed) {
        ImGui::TextWrapped("%s", T("m_unsaved"));
        Gap(6);
    }
    const bool ok = gsError_.empty();
    // (3.5, v150-host beta 22, agent 3.7) the settings on the server changed elsewhere since the form came (kept: SAVE asks
    // before it replaces them); the game running now still plays the settings it started with (what was saved: its next game,
    // or RESTART THE GAME NOW); the rotation's entries it does not play
    if (gsElsewhere_) {
        ImGui::PushStyleColor(ImGuiCol_Text, kAmberText);
        ImGui::TextWrapped("%s", T("m_gs_elsewhere"));
        ImGui::PopStyleColor();
        Gap(6);
    }
    if (gsSync_ && gsRunning_ && !gsPending_.empty()) {
        std::string what;
        for (const std::string& k : gsPending_) {
            const std::string l = SettingLabel(k);
            if (what.find(l) == std::string::npos) what += (what.empty() ? "" : ", ") + l;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kAmberText);
        ImGui::TextWrapped("%s", TF("m_gs_pending", what.c_str()).c_str());
        ImGui::PopStyleColor();
        Gap(6);
    }
    if (gsSync_ && gsRunning_ && !gsLeftOut_.empty()) {
        std::string what;
        for (const std::string& p : gsLeftOut_) {
            int r = -1, m = -1;
            if (sscanf(p.c_str(), "%d:%d", &r, &m) == 2) what += (what.empty() ? "" : ", ") + RuleName(r) + " / " + MapName(m);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("%s", TF("m_gs_left_out", what.c_str()).c_str());
        ImGui::PopStyleColor();
        Gap(6);
    }
    if (Button("save", T("b_save"), T("b_save"), T("h_save"), ok && changed)) SaveSettings(false, false);
    SameLineFit(ButtonWidth(T("b_save_restart")));
    if (Button("saverestart", T("b_save_restart"), T("b_save_restart"), T("h_save_restart"), ok)) Ask(T("c_restart"), [this] { SaveSettings(true, false); });
    SameLineFit(ButtonWidth(T("b_reload")));
    if (Button("reload", T("b_reload"), T("b_reload"), T("h_reload")) && !demo_) LoadSettings();
    // (3.5) the saved settings to the running game at once: a new game (agent 3.7 says what it does not play yet)
    if (gsSync_ && gsRunning_ && !gsPending_.empty()) {
        SameLineFit(ButtonWidth(T("b_restart_now")));
        if (Button("restartnow", T("b_restart_now"), T("b_restart_now"), T("h_restart_now"), !changed)) {
            const int bot = CurBot();
            Ask(TF("c_restart_now", gsReal_), [this, bot] {
                if (demo_) { Toast(T("m_restarting")); return; }
                remote_.Agent("restart", "", [this](int rc, const Json& j, const std::string&) {
                    if (rc == 0 && j["ok"].Truthy()) { Toast(T("m_restarting")); gsInfoAt_ = ImGui::GetTime() - 5; }
                    else Toast(TF("m_cmd_failed", ErrText(j).c_str()), true);
                }, bot);
            });
        }
    }
    if (!demo_ || demoScrollEnd_) GamePresets();       // (the self-test: the page's end is a picture of its own)
}

// the game settings this PC keeps: the last ones saved to a bot (a new bot gets them), the presets; USE
// fills the form, SAVE sends it
void App::GamePresets() {
    Gap(16);
    PanelHeader(T("p_presets"));
    if (!cfg.lastGame.settings.empty()) {
        const GameSettings last = GameFromPc(cfg.lastGame);
        if (ListRow("plast", TF("v_last_game", last.name.c_str()), T("b_use"), TF("h_use_last", last.name.c_str()))) {
            FillGame(last);
            Toast(TF("m_last_filled", CurBot()));
        }
    } else InfoRow(T("i_last_game"), T("v_none"));
    std::string forget;
    for (size_t i = 0; i < cfg.presets.size(); ++i) {
        const GamePreset& p = cfg.presets[i];
        bool f2 = false;
        const std::string id = "preset" + std::to_string(i);
        if (ListRow(id.c_str(), p.name, T("b_use"), TF("h_use_preset", p.name.c_str()), T("b_forget"), &f2)) {
            FillGame(GameFromPc(p));
            snprintf(gPreset_, sizeof gPreset_, "%s", p.name.c_str());     // SAVE AS PRESET: this one again
            Toast(TF("m_preset_filled", p.name.c_str(), CurBot()));
        }
        if (f2) forget = p.name;
    }
    if (!forget.empty())
        Ask(TF("c_forget_preset", forget.c_str()), [this, forget] {
            const int i = cfg.FindPreset(forget);
            if (i >= 0) cfg.presets.erase(cfg.presets.begin() + i);
            if (!demo_) cfg.Save();
        });
    // the form kept under a name: a name kept already is replaced (asked first)
    Gap(6);
    TextRow("presetname", T("g_preset_name"), gPreset_, 25, T("g_preset_name"), T("h_preset_name"));
    std::string name = gPreset_;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name[0] == ' ') name.erase(0, 1);
    const int at = name.empty() ? -1 : cfg.FindPreset(name);
    std::string err;                                  // (the form's own errors: shown above, SAVE AS PRESET waits for them)
    if (!name.empty() && !AsciiOk(name.c_str())) err = T("e_preset_name");
    else if (at < 0 && (int)cfg.presets.size() >= Config::kMaxPresets) err = TF("e_presets_full", Config::kMaxPresets);
    Gap(6);
    if (!err.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", err.c_str()); ImGui::PopStyleColor(); Gap(6); }
    if (Button("presetsave", T("b_preset_save"), T("b_preset_save"), TF("h_preset_save", Config::kMaxPresets), err.empty() && !name.empty() && gsError_.empty())) {
        auto keep = [this, name] {
            const GamePreset p = GameToPc(gs_, name);
            const int i = cfg.FindPreset(name);
            if (i >= 0) cfg.presets[i] = p;
            else if ((int)cfg.presets.size() < Config::kMaxPresets) cfg.presets.push_back(p);
            else return;
            if (!demo_) cfg.Save();
            gPreset_[0] = 0;
            Toast(TF("m_preset_saved", name.c_str()));
        };
        if (at >= 0) Ask(TF("c_preset_replace", cfg.presets[at].name.c_str()), keep);
        else keep();
    }
    Gap(10);
    ImGui::TextWrapped("%s", T("presets_hint"));
}

// ---- host account -------------------------------------------------------------------------------
void App::LoadAccount() {
    remote_.Agent("account-get", "", [this](int rc, const Json& j, const std::string&) {
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        snprintf(aId_, sizeof aId_, "%s", j["id"].Text().c_str());
        for (char* p = aId_; *p; ++p) *p = (char)toupper((unsigned char)*p);     // MGO1 accounts are in capitals
        aPwSet_ = j["password_set"].Truthy();
        aPw_[0] = 0;
    });
}

void App::PageAccount() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    if (!aLoaded_) { aLoaded_ = true; LoadAccount(); }
    PanelHeader(TF("p_account_bot", CurBot()).c_str());
    TextRow("aid", T("a_id"), aId_, 16, T("a_id"), T("h_a_id"), false, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    TextRow("apw", T("a_pw"), aPw_, 16, T("a_pw"), T("h_a_pw"), !aShow_, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    if (ValueRow("ashow", T("a_show"), aShow_ ? T("v_on") : T("v_off"), T("a_show"), T("h_a_show"))) aShow_ = !aShow_;
    if (ValueRow("akeep", T("a_keep"), aKeep_ ? T("v_on") : T("v_off"), T("a_keep"), T("h_a_keep"))) aKeep_ = !aKeep_;
    InfoRow(T("a_state"), aPwSet_ ? T("v_pwset") : T("v_pwnotset"), aPwSet_ ? kText : kRed);
    Gap(10);
    ImGui::TextWrapped("%s", T("a_hint"));
    Gap(10);
    std::string err;
    const size_t il = strlen(aId_), pl = strlen(aPw_);
    if (il < 3 || il > 15 || !NameCharsOk(aId_)) err = T("e_acc_id");
    else if (pl && (pl < 6 || pl > 15 || !NameCharsOk(aPw_))) err = T("e_acc_pw");
    else if (!pl && !aPwSet_) err = T("e_acc_pw");
    if (!err.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", err.c_str()); ImGui::PopStyleColor(); Gap(6); }
    auto save = [this](bool restart) {
        const std::string id = Up(aId_), pw = Up(aPw_);
        if (demo_) { AccountSet(CurBot(), id, pw, aKeep_, nullptr); Toast(T(restart ? "m_acc_restart" : "m_acc_saved")); return; }
        AccountSet(CurBot(), id, pw, aKeep_, [this, restart](bool ok, const std::string& err) {   // (build 1.00: QUICK SETUP's too)
            if (ok) {
                aPwSet_ = aPwSet_ || aPw_[0];
                plat::Wipe(aPw_, sizeof aPw_);
                if (restart) AgentCmd("restart", "", T("m_acc_restart")); else Toast(T("m_acc_saved"));
            } else Toast(TF("m_cmd_failed", err.c_str()), true);
        });
    };
    if (Button("asave", T("b_save"), T("b_save"), T("h_a_save"), err.empty())) save(false);
    SameLineFit(ButtonWidth(T("b_save_restart")));
    if (Button("asaver", T("b_save_restart"), T("b_save_restart"), T("h_a_save_restart"), err.empty())) Ask(T("c_restart"), [save] { save(true); });
    // the accounts this PC keeps: one click fills the form (then SAVE)
    Gap(16);
    PanelHeader(T("p_saved_accounts"));
    if (cfg.accounts.empty()) InfoRow(T("i_accounts"), T("v_none"));
    std::string forget;
    for (const auto& acc : cfg.accounts) {
        bool f2 = false;
        if (ListRow(("acc_" + acc.id).c_str(), acc.id, T("b_use"), TF("h_use_account", acc.id.c_str()), T("b_forget"), &f2)) {
            snprintf(aId_, sizeof aId_, "%s", acc.id.c_str());
            snprintf(aPw_, sizeof aPw_, "%s", acc.password.c_str());
            Toast(TF("m_account_filled", acc.id.c_str()));
        }
        if (f2) forget = acc.id;
    }
    if (!forget.empty()) Ask(TF("c_forget_account", forget.c_str()), [this, forget] { cfg.ForgetAccount(forget); if (!demo_) cfg.Save(); });
    Gap(6);
    ImGui::TextWrapped("%s", T("a_saved_hint"));
}

// ---- the player lists: blacklist, whitelist -------------------------------------------------------
// a name and a button on the right; true when the button is clicked
bool App::ListRow(const char* id, const std::string& name, const char* button, const std::string& help, const char* button2, bool* click2,
                  const std::string& shortName) {
    const float h = 28 * S;
    ImVec2 a = ImGui::GetCursorScreenPos();
    const float rw = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + h), kRow);
    dl->AddLine(ImVec2(a.x, a.y + h - 1), ImVec2(a.x + rw, a.y + h - 1), kLine, 2 * S);
    const float bw = std::max(ButtonWidth(button), 130 * S);
    const float bw2 = button2 ? std::max(ButtonWidth(button2), 110 * S) : 0;
    const float gap2 = button2 ? bw2 + 6 * S : 0;
    const float nw = rw - bw - gap2 - 30 * S;
    ImFont* sf = F.small ? F.small : F.row;
    const std::string& shown = !shortName.empty() && SpacedWidth(sf, sf->FontSize, name.c_str(), 0) > nw ? shortName : name;
    FitText(dl, F.row, ImVec2(a.x + 10 * S, a.y + (h - F.row->FontSize) * 0.5f), nw, kText, shown.c_str(), RowSpacing());
    if (button2) {
        ImGui::SetCursorScreenPos(ImVec2(a.x + rw - bw - gap2 - 2 * S, a.y));
        const bool c2 = Button((std::string(id) + "_2").c_str(), button2, button2, help, true, true, bw2);
        if (click2) *click2 = c2;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x + rw - bw - 2 * S, a.y));
    const bool click = Button(id, button, button, help, true, false, bw);
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h));
    ImGui::Dummy(ImVec2(rw, 0));
    return click;
}

void App::PageLists() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    const HostStatus st = Status();
    // the blacklist
    PanelHeader(T("p_black"));
    if (st.black.empty()) InfoRow(T("i_names"), T("v_none"));
    for (const auto& n : st.black)
        if (ListRow(("bl_" + n).c_str(), n, T("b_remove"), TF("h_black_remove", n.c_str())))
            AgentCmd("blacklist-remove " + ShellQuote(n), "", TF("m_unblacklisted", n.c_str()));
    TextRow("blackname", T("i_name"), blackName_, 17, T("i_name"), T("h_list_name"), false, 0.55f, ImGuiInputTextFlags_CharsUppercase);
    Gap(6);
    if (Button("blackadd", T("b_black_add"), T("b_black_add"), T("h_black_add"), PlayerNameOk(blackName_), true)) {
        const std::string n = Up(blackName_);
        AgentCmd("blacklist-add " + ShellQuote(n), "", TF("m_blacklisted", n.c_str()));
        blackName_[0] = 0;
    }
    Gap(16);
    // the whitelist
    PanelHeader(T("p_white"));
    int notWhite = 0;
    std::vector<std::string> inGame;
    for (const auto& p : st.players) {
        if (p.host || p.name.empty()) continue;
        inGame.push_back(Up(p.name));
        if (!Listed(st.white, p.name)) ++notWhite;
    }
    const bool canOn = !st.white.empty() || st.whiteOnly;
    if (ValueRow("whiteonly", T("g_white_only"), st.whiteOnly ? T("v_on") : T("v_off"), T("g_white_only"), canOn ? T("h_white_only") : T("e_white_empty"), canOn)) {
        if (st.whiteOnly) AgentCmd("whitelist-only off", "", T("m_white_off"));
        else if (notWhite) Ask(TF("c_white_on", notWhite), [this] { AgentCmd("whitelist-only on", "", T("m_white_on")); });
        else AgentCmd("whitelist-only on", "", T("m_white_on"));
    }
    if (st.white.empty()) {
        InfoRow(T("i_names"), T("v_none"));
        Gap(4);
        ImGui::TextWrapped("%s", T("e_white_empty"));
    }
    for (const auto& n : st.white)
        if (ListRow(("wl_" + n).c_str(), n, T("b_remove"), TF("h_white_remove", n.c_str())))
            AgentCmd("whitelist-remove " + ShellQuote(n), "", TF("m_unwhitelisted", n.c_str()));
    TextRow("whitename", T("i_name"), whiteName_, 17, T("i_name"), T("h_list_name"), false, 0.55f, ImGuiInputTextFlags_CharsUppercase);
    Gap(6);
    if (Button("whiteadd", T("b_white_add"), T("b_white_add"), T("h_white_add"), PlayerNameOk(whiteName_))) {
        const std::string n = Up(whiteName_);
        AgentCmd("whitelist-add " + ShellQuote(n), "", TF("m_whitelisted", n.c_str()));
        whiteName_[0] = 0;
    }
    SameLineFit(ButtonWidth(T("b_white_players")));
    if (Button("whiteplayers", T("b_white_players"), T("b_white_players"), T("h_white_players"), !inGame.empty())) {
        std::string args = "whitelist-add";
        for (const auto& n : inGame) args += " " + ShellQuote(n);
        AgentCmd(args, "", T("m_white_players"));
    }
}

// ---- log -------------------------------------------------------------------------------------------
void App::LoadLog() {
    logLoading_ = true;
    logAt_ = ImGui::GetTime();
    const bool tech = logTech_;
    remote_.Agent(tech ? "log 400" : "log 400 --essential", "", [this, tech](int rc, const Json& j, const std::string&) {
        logLoading_ = false;
        if (rc != 0 || !j["ok"].Truthy() || tech != logTech_) return;
        log_.clear();
        for (size_t i = 0; i < j["lines"].Size(); ++i) log_.push_back(j["lines"][i].Text());
        logViewOf_ = (size_t)-1;                      // to make again
        if (logAtEnd_) logToEnd_ = true;              // the newest lines stay in view
    });
}

// a line of the host's journal ("<ISO time> <host> <name>: <message>"): its time on this PC's clock
// and its text in the program's language; text "" for a technical line (TECHNICAL LINES shows it)
static void LogLineView(const std::string& raw, std::string& time, std::string& text) {
    time.clear(); text.clear();
    const size_t sp1 = raw.find(' '), sp2 = sp1 == std::string::npos ? sp1 : raw.find(' ', sp1 + 1);
    const size_t col = sp2 == std::string::npos ? sp2 : raw.find(": ", sp2 + 1);
    const std::string m = col == std::string::npos ? raw : raw.substr(col + 2);
    int Y, Mo, D, h, mi, se, zh = 0, zm = 0; char sign = '+';
    if (sp1 != std::string::npos && sscanf(raw.substr(0, sp1).c_str(), "%d-%d-%dT%d:%d:%d%c%2d%2d", &Y, &Mo, &D, &h, &mi, &se, &sign, &zh, &zm) >= 6) {
        struct tm t{}; t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = mi; t.tm_sec = se;
        const time_t utc = plat::TimeGm(&t) - (sign == '-' ? -1 : 1) * (zh * 3600 + zm * 60);
        time = ReportClock((long long)utc * 1000, false);
    }
    text = LogMessageText(m);
}

// a message of the host's or a doll's (the journal's, a daily log's) in the program's language; "" for a
// technical one
std::string LogMessageText(std::string m) {
    std::string text;
    if (m.rfind("ps2 f", 0) == 0) { const size_t c = m.find(": "); if (c != std::string::npos) m = m.substr(c + 2); }
    if (m.rfind("doll f", 0) == 0) { const size_t c = m.find(": "); if (c != std::string::npos) return DollLogText(m.substr(c + 2)); }
    if (m.rfind("mgo1doll", 0) == 0 || m.rfind("mgo1-dollgate", 0) == 0 || m.rfind("CRASH: ", 0) == 0 || m.rfind("network: ", 0) == 0 ||
        m.rfind("mgo1host: the game ", 0) == 0 || m.rfind("mgo1host: stopped", 0) == 0)
        return DollLogText(m);
    auto starts = [&](const char* p) { return m.rfind(p, 0) == 0; };
    auto has = [&](const char* p) { return m.find(p) != std::string::npos; };
    auto quoted = [&](size_t from = 0) {                   // the first "..." of the message
        const size_t a = m.find('"', from);
        if (a == std::string::npos) return std::string();
        const size_t b = m.find('"', a + 1);
        return b == std::string::npos ? m.substr(a + 1) : m.substr(a + 1, b - a - 1);
    };
    auto screen = [](std::string x) {                      // the game's own words, its line breaks as " / "
        std::string o;
        for (char c : x) { if (c == '|') { while (!o.empty() && o.back() == ' ') o.pop_back(); if (!o.empty()) o += " / "; } else o += c; }
        while (o.size() >= 3 && o.compare(o.size() - 3, 3, " / ") == 0) o.erase(o.size() - 3);
        return o;
    };
    int a = 0, b = 0;
    if (starts("mgo1host: bot ") && sscanf(m.c_str(), "mgo1host: bot %d", &a) == 1) {
        const size_t u = m.find("UDP port ");
        text = TF("lg_start", std::to_string(a).c_str(), u == std::string::npos ? "-" : std::to_string(atoi(m.c_str() + u + 9)).c_str());
    } else if (starts("mgo1host: ") && has(" is missing")) text = TF("lg_no_disc", m.substr(10, m.find(" is missing") - 10).c_str());
    else if (starts("mgo1host: ") && has("may only hold")) text = T("lg_bad_account");
    else if (has("HOST_ACCOUNT_ID is not set") || has("HOST_ACCOUNT_PASSWORD is not set")) text = T("lg_no_account");
    else if (starts("Started ")) text = T("lg_svc_started");
    else if (starts("Stopping ")) text = T("lg_svc_stopping");
    else if (starts("Stopped") || has("Deactivated successfully")) text = T("lg_svc_stopped");
    else if (has("Scheduled restart job")) text = T("lg_restart_job");
    else if (has("Failed with result")) text = T("lg_svc_failed");
    else if (has("Main process exited")) {
        const size_t st = m.find("status="), co = m.find("code=");
        const int code = st != std::string::npos ? atoi(m.c_str() + st + 7) : co != std::string::npos ? atoi(m.c_str() + co + 5) : -1;
        text = TF("lg_exit", std::to_string(code).c_str());
    } else if (starts("text: \"")) text = TF("lg_screen", screen(quoted()).c_str());
    else if (starts("step ") && sscanf(m.c_str(), "step %d: \"", &a) == 1) {
        const std::string x = screen(quoted());
        text = x.empty() ? TF("lg_step_blank", std::to_string(a).c_str()) : TF("lg_step", std::to_string(a).c_str(), x.c_str());
    } else if (starts("host watch: hosting")) text = T("lg_hosting");
    // (v141 beta 6) the lobby's connection lost and the grace before the process ends (exit 6), or back in time
    else if (starts("host watch: the lobby's connection is closed")) {
        const size_t p = m.rfind("a new process in ");
        text = TF("lg_lobby_closed", p == std::string::npos ? "?" : std::to_string(atoi(m.c_str() + p + 17)).c_str());
    } else if (starts("host watch: the lobby's connection still closed after ") && sscanf(m.c_str(), "host watch: the lobby's connection still closed after %d", &a) == 1)
        text = TF("lg_lobby_closed_end", std::to_string(a).c_str());
    else if (starts("host watch: the lobby's connection is open again")) {
        const size_t p = m.find(") after ");
        text = TF("lg_lobby_open", p == std::string::npos ? "?" : std::to_string(atoi(m.c_str() + p + 8)).c_str());
    }
    else if (starts("host watch: the host entry is gone")) text = T("lg_gone");
    // (v141 beta 7) SaveMGO's rankings and the dolls (HOST_UNRANKED): the mode at the start, a doll in the game, the new rounds, a
    // report sent unranked
    else if (starts("unranked: auto")) text = T("lg_unranked_auto");
    else if (starts("unranked: always")) text = T("lg_unranked_always");
    else if (starts("unranked: off")) text = T("lg_unranked_off");
    else if (starts("unranked: a doll in the game")) text = T("lg_unranked_doll");
    else if (starts("unranked: a new round") && has("this game had dolls")) text = T("lg_unranked_round_kept");
    else if (starts("unranked: a new round") && has("no doll in the game")) text = T("lg_unranked_round_none");
    else if (starts("unranked: a new round") && has("a doll in the game")) text = T("lg_unranked_round_doll");
    else if (starts("lobby filter: 4390 for user id ") && has(" sent unranked")) text = T("lg_unranked_sent");
    else if (starts("host watch: no game of its own after ") && sscanf(m.c_str(), "host watch: no game of its own after %d", &a) == 1)
        text = TF("lg_nogame", std::to_string(a).c_str());
    else if (starts("host watch: no game for")) text = T("lg_lost");
    else if (starts("host settings from ")) text = T("lg_settings");
    else if (starts("host settings: 0x4310")) text = TF("lg_created", quoted().c_str());
    else if (starts("host settings: host only")) text = T("lg_settings_none");
    else if (starts("host settings: ") && has("not readable")) text = T("lg_settings_bad");
    else if (starts("lists: P") && has("is on the blacklist")) text = TF("lg_black", quoted().c_str());
    else if (starts("lists: P") && has("is not on the whitelist")) text = TF("lg_white", quoted().c_str());
    else if ((starts("blacklist: ") || starts("whitelist: ")) && sscanf(m.c_str() + 11, "%d name", &b) == 1) {
        const std::string n = b == 0 ? std::string(T("v_names_0")) : b == 1 ? std::string(T("v_names_1")) : TF("v_names_n", std::to_string(b).c_str());
        text = TF(starts("blacklist") ? "lg_black_n" : has("only these") ? "lg_white_only_n" : "lg_white_n", n.c_str());
    }
    else if (starts("kick P") && has("kick requested")) text = TF("lg_kick", quoted().c_str());
    else if (starts("kick P") && has("gone before")) text = T("lg_kick_gone");
    else if (starts("command kick ") && has("the host itself")) text = T("lg_kick_host");
    else if (starts("command kick ") && has("no such player")) text = TF("lg_kick_none", m.substr(13, m.find(':', 13) - 13).c_str());
    else if (starts("command players:")) {
        std::string names;
        for (size_t p = m.find(" name \""); p != std::string::npos; p = m.find(" name \"", p + 1)) {
            const size_t q = m.rfind(" P", p);
            const bool host = m.find("(host)", q) < p;
            if (host) continue;
            if (!names.empty()) names += T("sep_list");
            names += quoted(p);
        }
        text = names.empty() ? std::string(T("lg_players_none")) : TF("lg_players", names.c_str());
    } else if (starts("command quit")) text = T("lg_quit");
    else if (starts("RESULT ") && has("init failed")) text = T("lg_init_failed");
    return text;
}

// the journal in a file to send to someone who helps: every line as the server sent it (no log-in
// data: the server masks it), in <Documents>/MGO1 Host/Logs
void App::SaveLog() {
    if (demo_ || log_.empty()) return;
    const std::string dir = plat::Join(Config::DocumentsDir(), "MGO1 Host", "Logs");
    plat::MakeDirs(dir);
    char stamp[32];
    const time_t now = time(nullptr); struct tm lt{}; plat::LocalTime(now, lt);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &lt);
    const std::string path = plat::Join(dir, "Bot " + std::to_string(CurBot()) + " " + stamp + ".txt");
    FILE* f = plat::Open(path, "wb");
    if (!f) { Toast(TF("m_log_save_failed", HideProfile(path).c_str()), true); return; }
    const HostStatus st = Status();
    fputs("\xEF\xBB\xBF", f);                                         // UTF-8, for Notepad
    fprintf(f, "MGO1 Host %s, agent %s, bot %d, host %s\n\n", kProgramVersion, st.agent.empty() ? "-" : st.agent.c_str(), CurBot(),
            installedVersion_.empty() ? "-" : installedVersion_.c_str());
    for (const auto& l : log_) {                                     // the lines in the program's language
        std::string t, x;
        LogLineView(l, t, x);
        if (!x.empty()) fprintf(f, "%s  %s\n", t.c_str(), x.c_str());
    }
    fprintf(f, "\n%s\n", T("lg_file_tech"));                       // then as the server wrote them
    for (const auto& l : log_) fprintf(f, "%s\n", l.c_str());
    fclose(f);
    Toast(TF("m_log_saved", HideProfile(path).c_str()));
    plat::OpenFolder(dir);
}

void App::PageLog() {
    if (LinkState() != Link::Connected) { InfoRow(T("i_link"), T("s_noconn"), kRed); return; }
    // the bot's journal as it is written, or the daily logs of every bot, doll and the doll manager
    // (beta 16) or the crash reports the server keeps for its bots' and dolls' games (logs.cpp)
    {
        static const char* const kViews[3] = {"v_lg_live", "v_lg_daily", "v_lg_crash"};
        const int view = logCrash_ ? 2 : logDaily_ ? 1 : 0;
        if (const int d = ValueRow("logview", T("lg_view"), T(kViews[view]), T("lg_view"), std::string(T("h_lg_view")) + " " + T("h_lg_crash"))) {
            const int nv = Wrap(view + d, 0, 2);
            logDaily_ = nv == 1; logCrash_ = nv == 2;
            if (logDaily_ && !demo_) LoadLogList();
            if (logCrash_ && !demo_) LoadCrashes();
        }
    }
    Gap(4);
    if (logCrash_) { PageLogCrash(); return; }
    if (logDaily_) { PageLogDaily(); return; }
    if (!demo_ && !logLoading_ && (log_.empty() && logAt_ < 0 ? true : logAuto_ && ImGui::GetTime() - logAt_ > 5)) LoadLog();
    if (Button("logref", T("b_refresh"), T("b_refresh"), T("h_log_refresh")) && !demo_) LoadLog();
    SameLineFit(ButtonWidth(T("b_auto_on")));
    if (Button("logauto", logAuto_ ? T("b_auto_on") : T("b_auto_off"), T("b_auto_on"), T("h_log_auto"))) logAuto_ = !logAuto_;
    SameLineFit(ButtonWidth(T("b_log_save")));
    if (Button("logsave", T("b_log_save"), T("b_log_save"), T("h_log_save"), !log_.empty())) SaveLog();
    Gap(6);
    if (ValueRow("logtech", T("lg_tech"), logTech_ ? T("v_on") : T("v_off"), T("lg_tech"), T("h_lg_tech"))) {
        logTech_ = !logTech_;
        logViewOf_ = (size_t)-1;
        if (!demo_) { log_.clear(); LoadLog(); }
    }
    Gap(8);
    PanelHeader(T("p_log"));
    // the lines in the program's language (made again when the lines or the language change)
    if (logViewOf_ != log_.size() || logViewLang_ != CurLang() || logViewTech_ != logTech_) {
        logView_.clear();
        for (const auto& l : log_) {
            LogView v;
            LogLineView(l, v.time, v.text);
            if (v.text.empty()) { if (!logTech_) continue; v.text = "\x01" + l; }   // a technical line, as it is
            logView_.push_back(v);
        }
        logViewOf_ = log_.size(); logViewLang_ = CurLang(); logViewTech_ = logTech_;
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C(0x2a2a24));
    ImGui::BeginChild("##log", ImVec2(0, std::max(200.0f * S, ImGui::GetContentRegionAvail().y - 10 * S)), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, C(0xd0d0b8));
    if (logView_.empty()) ImGui::TextWrapped("%s", T(logLoading_ ? "i_working" : "lg_empty"));
    const float tw = ImGui::CalcTextSize("00:00:00  ").x;
    for (const auto& v : logView_) {
        if (!v.text.empty() && v.text[0] == '\x01') {               // technical: the monospace font, as the server wrote it
            ImGui::PushFont(F.mono);
            ImGui::TextUnformatted(v.text.c_str() + 1);
            ImGui::PopFont();
            continue;
        }
        const float x0 = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, C(0x9a9a86));
        ImGui::TextUnformatted(v.time.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(x0 + tw);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(v.text.c_str());
        ImGui::PopTextWrapPos();
    }
    if (logToEnd_) { ImGui::SetScrollHereY(1.0f); logToEnd_ = false; }
    else logAtEnd_ = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40 * S;   // the user scrolled up: the view stays
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---- server --------------------------------------------------------------------------------------

void App::PageServer() {
    const Link link = LinkState();
    const HostStatus st = Status();
    // the servers this PC keeps: one click connects
    PanelHeader(T("p_servers"));
    if (cfg.servers.empty()) InfoRow(T("i_servers"), T("v_none"));
    int forget = -1;
    for (size_t i = 0; i < cfg.servers.size(); ++i) {
        const ServerEntry& e = cfg.servers[i];
        const bool here = (int)i == cfg.current && RemoteRunning() && link != Link::Idle;
        std::string name = e.Name();
        if (!e.label.empty()) name += "  (" + e.host + ")";
        bool f2 = false;
        const std::string id = "srv" + std::to_string(i);
        if (ListRow(id.c_str(), name, here ? T("b_reconnect") : T("b_connect"), TF("h_connect_to", e.host.c_str()), T("b_forget"), &f2, e.Name()) && !demo_)
            ConnectTo((int)i);
        if (f2) forget = (int)i;
    }
    if (forget >= 0) {
        const std::string h = cfg.servers[forget].host;
        Ask(TF("c_forget_server", h.c_str()), [this, forget] {
            if (forget >= (int)cfg.servers.size()) return;
            if (forget == cfg.current) { Disconnect(); cfg.current = -1; }
            else if (forget < cfg.current) --cfg.current;
            cfg.servers.erase(cfg.servers.begin() + forget);
            if (!demo_) cfg.Save();
        });
    }
    Gap(14);
    // the connection
    PanelHeader(T("p_conn"));
    TextRow("label", T("sv_label"), fLabel_, 25, T("sv_label"), T("h_sv_label"));
    TextRow("host", T("sv_address"), fHost_, sizeof fHost_, T("sv_address"), T("h_sv_address"));
    TextRow("port", T("sv_port"), fPort_, sizeof fPort_, T("sv_port"), T("h_sv_port"), false, 0.55f, ImGuiInputTextFlags_CharsDecimal);
    TextRow("user", T("sv_user"), fUser_, sizeof fUser_, T("sv_user"), T("h_sv_user"), false, 0.55f, ImGuiInputTextFlags_CharsNoBlank);
    if (ValueRow("auth", T("sv_auth"), fUseKey_ ? T("v_keyfile") : T("v_password"), T("sv_auth"), T("h_sv_auth"))) fUseKey_ = !fUseKey_;
    if (!fUseKey_) {
        TextRow("pass", T("sv_password"), fPass_, sizeof fPass_, T("sv_password"), T("h_sv_password"), true);
        if (ValueRow("remember", T("sv_remember"), fRemember_ ? T("v_on") : T("v_off"), T("sv_remember"), T("h_sv_remember"))) fRemember_ = !fRemember_;
    } else {
        TextRow("key", T("sv_key"), fKey_, sizeof fKey_, T("sv_key"), T("h_sv_key"));
        TextRow("keypass", T("sv_keypass"), fKeyPass_, sizeof fKeyPass_, T("sv_keypass"), T("h_sv_keypass"), true);
    }
    Gap(8);
    if (!RemoteRunning() || link == Link::Idle) {
        if (Button("connect", T("b_connect"), T("b_connect"), T("h_connect"), fHost_[0] != 0)) Connect();
    } else {
        if (Button("reconnect", T("b_reconnect"), T("b_reconnect"), T("h_connect"), fHost_[0] != 0)) Connect();
        SameLineFit(ButtonWidth(T("b_disconnect")));
        if (Button("disconnect", T("b_disconnect"), T("b_disconnect"), T("h_disconnect"))) Disconnect();
    }
    SameLineFit(ButtonWidth(T("m_quick")));                       // (build 1.00) QUICK SETUP: the server, the account, the disc, GO
    if (Button("srvquick", T("m_quick"), T("m_quick"), T("d_quick"))) page_ = PG_QUICK;
    if (fUseKey_) {
        SameLineFit(ButtonWidth(T("b_browse_key")));
        if (Button("browsekey", T("b_browse_key"), T("b_browse_key"), T("h_sv_key")) && !plat::Pick(PICK_KEY, true, T("b_browse_key")))
            Toast(T("m_no_picker"), true);
    }
    {
        std::string p;
        if (plat::Picked(PICK_KEY, p)) snprintf(fKey_, sizeof fKey_, "%s", p.c_str());
    }
    Gap(6);
    {
        std::string msg; const Link l = LinkState(&msg);
        const char* k = l == Link::Connected ? "l_connected" : l == Link::Connecting ? "l_connecting" : l == Link::AuthFailed ? "l_auth" :
                        l == Link::Unreachable ? "l_unreachable" : l == Link::NoAgent ? "l_noagent" : l == Link::NeedTrust || l == Link::KeyChanged ? "l_trust" :
                        l == Link::Error ? "l_error" : "l_idle";
        ImGui::PushStyleColor(ImGuiCol_Text, l == Link::Connected ? kGreenText : l == Link::Connecting || l == Link::Idle ? kText : kRed);
        ImGui::TextWrapped("%s", l == Link::Error && !msg.empty() ? TF("l_error_msg", TrMsg(msg).c_str()).c_str() : T(k));
        ImGui::PopStyleColor();
    }
    if (!(link == Link::Connected || link == Link::NoAgent)) {
        Gap(14);
        PanelHeader(T("p_help_server"));
        ImGui::TextWrapped("%s", T("sv_help"));
        return;
    }
    // the host on the server
    Gap(14);
    PanelHeader(T("p_hostsrv"));
    const std::string local = demo_ ? "build 1.00 d11286182942" : LocalVersion();
    InfoRow(T("sv_installed"), link == Link::NoAgent ? T("v_notinstalled") : installedVersion_.empty() ? "-" : installedVersion_.c_str(), link == Link::NoAgent ? kRed : 0);
    InfoRow(T("sv_program"), local.empty() ? T("v_nopackage") : local.c_str(), local.empty() ? kRed : 0);
    if (link == Link::Connected) {
        InfoRow(T("sv_service"), st.service == "active" ? T("v_running") : st.service == "failed" ? T("v_failed") : T("v_stopped"), st.service == "active" ? kGreenText : kRed);
        InfoRow(T("sv_boot"), st.enabled ? T("v_on") : T("v_off"));
        char b[128];
        const std::string disk = Dec(st.diskFree, 1);
        snprintf(b, sizeof b, "%s", st.cores == 1 ? TF("v_sys_1", st.memTotal, disk.c_str()).c_str() : TF("v_sys", st.cores, st.memTotal, disk.c_str()).c_str());
        InfoRow(T("sv_system"), b);
        InfoRow(T("sv_uptime"), Dur(st.sysUptime).c_str());
        // the daily restart
        int idx = 0;                                  // 0 off, 1..48: HH:MM every 30 min
        if (st.schedule != "off" && st.schedule.size() == 5) idx = 1 + atoi(st.schedule.c_str()) * 2 + (atoi(st.schedule.c_str() + 3) >= 30 ? 1 : 0);
        if (!schedSent_.empty() && (st.schedule == schedSent_ || ImGui::GetTime() - schedSentAt_ > 30)) schedSent_.clear();
        if (schedWant_ < 0 && schedSent_.empty()) schedWant_ = idx;
        const int want = schedWant_ < 0 ? idx : schedWant_;
        snprintf(b, sizeof b, "%s", want == 0 ? T("v_off") : TF("v_hhmm", (want - 1) / 2, (want - 1) % 2 * 30).c_str());
        const int d = ValueRow("sched", T("sv_schedule"), b, T("sv_schedule"), T("h_sv_schedule"));
        if (d) schedWant_ = Wrap(want + d, 0, 48);
        if (schedWant_ >= 0 && schedWant_ != idx && schedSent_.empty()) {
            Gap(4);
            if (Button("applysched", T("b_apply"), T("b_apply"), T("h_sv_schedule"))) {
                char arg[16];
                if (want == 0) snprintf(arg, sizeof arg, "off"); else snprintf(arg, sizeof arg, "%02d:%02d", (want - 1) / 2, (want - 1) % 2 * 30);
                AgentCmd(std::string("schedule-set ") + arg, "", T("m_saved"));
                schedSent_ = arg;
                schedSentAt_ = ImGui::GetTime();
            }
        } else if (schedSent_.empty()) schedWant_ = -1;
    }
    // installation and updates
    Gap(14);
    PanelHeader(T("p_install"));
    TextRow("disc", T("sv_disc"), disc_, sizeof disc_, T("sv_disc"), T("h_sv_disc"), false, 0.6f);
    Gap(6);
    const InstallProgress ip = InstallState();
    if (Button("browsedisc", T("b_browse"), T("b_browse"), T("h_sv_disc"), !ip.running) && !plat::Pick(PICK_DISC, false, T("b_browse")))
        Toast(T("m_no_picker"), true);
    {
        std::string p;
        if (plat::Picked(PICK_DISC, p) && !ip.running) {
            snprintf(disc_, sizeof disc_, "%s", p.c_str());
            if (!demo_) { cfg.discDir = disc_; cfg.Save(); sound::Init(cfg.discDir); }
        }
    }
    if (!ip.running) {
        const char* label = link == Link::NoAgent ? T("b_install") : T("b_update");
        SameLineFit(ButtonWidth(label));
        if (Button("install", label, T("b_install"), T("h_install"), disc_[0] != 0 && !local.empty()))
            Ask(link == Link::NoAgent ? T("c_install") : T("c_update"), [this, link] { StartInstall(link != Link::NoAgent); });
    } else {
        SameLineFit(ButtonWidth(T("b_cancel")));
        if (Button("cancelinst", T("b_cancel"), T("b_cancel"), T("h_cancel_install"), true, true) && !demo_) remote_.CancelInstall();
    }
    if (ip.running || ip.done) {
        Gap(8);
        std::string b;
        const char* ph = ip.phase == "check" ? T("i_ph_check") : ip.phase == "files" ? T("i_ph_files") : ip.phase == "compare" ? T("i_ph_compare") :
                         ip.phase == "upload" ? T("i_ph_upload") : ip.phase == "install" ? T("i_ph_install") : ip.phase == "build" ? T("i_ph_build") : T("i_ph_done");
        if (ip.done) b = ip.ok ? T(installUpdate_ ? "m_update_ok" : "m_install_ok") : TF("m_install_failed", TrMsg(ip.error).c_str());
        else if (ip.phase == "upload" && ip.allTotal)
            b = std::string(ph) + "  " + ip.file + "  " + TF("v_mb_of", std::to_string(ip.allDone >> 20).c_str(), std::to_string(ip.allTotal >> 20).c_str());
        else b = ph;
        ImGui::PushStyleColor(ImGuiCol_Text, ip.done ? (ip.ok ? kGreenText : kRed) : kText);
        ImGui::TextWrapped("%s", b.c_str());
        ImGui::PopStyleColor();
        if (ip.phase == "upload" && ip.allTotal) {
            ImVec2 a = ImGui::GetCursorScreenPos();
            const float bw = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + 14 * S), kRow);
            dl->AddRectFilled(a, ImVec2(a.x + bw * (float)((double)ip.allDone / (double)ip.allTotal), a.y + 14 * S), kGreen);
            dl->AddRect(a, ImVec2(a.x + bw, a.y + 14 * S), kDark, 0, 0, 1.5f * S);
            ImGui::Dummy(ImVec2(bw, 18 * S));
        }
        if (ip.running && ip.phase == "build") BuildProgress(ip);   // (build 1.00) the host built on the server (quick.cpp)
        Gap(4);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, C(0x2a2a24));
        ImGui::BeginChild("##instlog", ImVec2(0, 180 * S), false, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleColor(ImGuiCol_Text, C(0xd0d0b8));
        for (const auto& l : ip.lines) {
            if (!l.empty() && l[0] == '@') { ImGui::PushTextWrapPos(0.0f); ImGui::TextUnformatted(TrMsg(l).c_str()); ImGui::PopTextWrapPos(); }
            else { ImGui::PushFont(F.mono); ImGui::TextUnformatted(l.c_str()); ImGui::PopFont(); }   // a tool's own line
        }
        if (ip.running || (showcase_ && ip.done)) ImGui::SetScrollHereY(1.0f);   // (build 1.00: the showcase's update done, as it ended)
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    Gap(14);
    PanelHeader(T("p_help_server"));
    ImGui::TextWrapped("%s", T("sv_help"));
}

// ---- options, about --------------------------------------------------------------------------------
void App::PageOptions() {
    PanelHeader(T("p_options"));
    const int d = ValueRow("lang", T("o_lang"), kLangNames[CurLang()], T("o_lang"), T("h_o_lang"));
    if (d) {
        SetLang(Wrap(CurLang() + d, 0, L_COUNT - 1));
        cfg.language = kLangCodes[CurLang()];
        if (!demo_) cfg.Save();
        fontsDirty = true;
    }
    char b[32];
    snprintf(b, sizeof b, "%d %%", (int)std::lround(cfg.scale * 100));
    const int ds = ValueRow("scale", T("o_scale"), b, T("o_scale"), T("h_o_scale"));
    if (ds) {
        cfg.scale = std::min(2.0f, std::max(0.75f, cfg.scale + ds * 0.125f));
        if (!demo_) cfg.Save();
        fontsDirty = true;
    }
    // the game's menu sounds, from the disc folder (the SERVER page)
    const sound::State ss = demo_ ? sound::READY : sound::Get();
    const char* sv = !cfg.sound ? T("v_off") : ss == sound::READY ? T("v_on") : ss == sound::SEARCHING ? T("v_snd_loading") :
                     ss == sound::NO_DISC ? T("v_snd_nodisc") : T("v_snd_notfound");
    if (ValueRow("sound", T("o_sound"), sv, T("o_sound"), T("h_o_sound"))) {
        cfg.sound = !cfg.sound;
        sound::Set(cfg.sound, cfg.soundVolume);
        if (!demo_) cfg.Save();
        CursorSound();                                             // (heard when just turned on)
    }
    snprintf(b, sizeof b, "%d %%", cfg.soundVolume);
    if (const int dv = ValueRow("volume", T("o_volume"), b, T("o_volume"), T("h_o_volume"), cfg.sound)) {
        cfg.soundVolume = std::clamp(cfg.soundVolume + dv * 10, 0, 100);
        sound::Set(cfg.sound, cfg.soundVolume);
        if (!demo_) cfg.Save();
        CursorSound();                                             // (at the new volume)
    }
    Gap(12);
    PanelHeader(T("p_languages"));
    for (int i = 0; i < L_COUNT; ++i) {
        const std::string id = std::string("lg") + kLangCodes[i];
        if (MenuRow(id.c_str(), kLangNames[i], CurLang() == i, kLangNames[i], T("h_o_lang"))) {
            SetLang(i); cfg.language = kLangCodes[i]; if (!demo_) cfg.Save(); fontsDirty = true;
        }
    }
}

void App::PageAbout() {
    PanelHeader(T("p_about"));
    InfoRow(T("ab_program"), (std::string("MGO1 Host ") + kProgramVersion).c_str());
    const std::string local = demo_ ? "build 1.00 d11286182942" : LocalVersion();
    InfoRow(T("ab_package"), local.empty() ? T("v_nopackage") : local.c_str());
    Gap(10);
    ImGui::TextWrapped("%s", T("ab_text"));
    Gap(10);
    PanelHeader(T("p_credits"));
    ImGui::TextWrapped("%s", T("ab_credits"));
    Gap(10);
    PanelHeader(T("p_licenses"));
#ifdef _WIN32
    ImGui::TextWrapped("%s", "Dear ImGui (MIT), libssh2 (BSD-3-Clause), OpenSSL (Apache-2.0), Fira Sans Condensed, Fira Mono (SIL Open Font License 1.1).");
#else
    ImGui::TextWrapped("%s", "Dear ImGui (MIT), GLFW (zlib), libssh2 (BSD-3-Clause), OpenSSL (Apache-2.0), miniaudio (MIT-0), stb (MIT), "
                             "Fira Sans Condensed, Fira Mono (SIL Open Font License 1.1).");
#endif
}

// ---- the self-test's made-up host: the longest names and values the pages can meet ---------------------
void App::InitDemo(const std::string& dir) {
    exeDir = dir;
    demo_ = true;
    // (build 1.00) MGO1_HOST_SHOWCASE=1 (or all): the release's pictures, showcase.cpp's plausible host instead of this one
    if (const char* sc = getenv("MGO1_HOST_SHOWCASE"); sc && *sc && strcmp(sc, "0") != 0) {
        showcase_ = true;
        showcaseAll_ = !strcmp(sc, "all");
        InitShowcase();
        return;
    }
    {
        ServerEntry e;
        e.label = "HETZNER CX23 FALKENSTEIN"; e.host = "static.10.113.0.203.clients.your-server.de"; e.bot = 1;
        cfg.servers.push_back(e);
        e.label = ""; e.host = "203.0.113.77"; cfg.servers.push_back(e);
        e.label = "LAN"; e.host = "192.168.1.20"; cfg.servers.push_back(e);
        cfg.current = 0;
        cfg.accounts = {{"SNAKEEATER1964", "X"}, {"WWWWWWWWWWWWWWW", "X"}, {"BOT2", "X"}};
    }
    snprintf(fLabel_, sizeof fLabel_, "%s", cfg.servers[0].label.c_str());
    snprintf(fHost_, sizeof fHost_, "%s", cfg.servers[0].host.c_str());
    snprintf(fPass_, sizeof fPass_, "%s", "not-a-password");
    snprintf(disc_, sizeof disc_, "%s", "C:\\Users\\Snake\\Documents\\MGO1\\MGS3 Subsistence disc 2 (online)");
    HostStatus& s = demoStatus_;
    s.valid = true; s.online = "online"; s.service = "active"; s.enabled = true; s.installed = true;
    s.account = "SNAKEEATER1964"; s.schedule = "04:30"; s.agent = MGO1HOST_VERSION; s.lobby = true;
    s.gameName = "WWWWWWWWWWWWWWW";
    s.rules = {{1, 2}, {0, 8}, {4, 7}, {2, 10}, {3, 3}};
    s.round = 2; s.maxPlayers = 9; s.gameAge = 5423;
    const char* names[] = {"SNAKEEATER1964", "WWWWWWWWWWWWWWW", "RevolverOcelot", "The_Boss_1964", "EVA", "Raikov.KGB", "VOLGIN-ELEC", "sokolov", "MMMMMMMMMMMMMMM"};
    const int pings[] = {0, 12, 85, 140, 230, 480, -1, 60, 999};
    const int teams[] = {-1, 0, 1, 0, 1, 254, 0, 1, 1};
    for (int i = 0; i < 9; ++i) {
        HostPlayer p;
        p.p = i + 1; p.name = names[i]; p.host = i == 0; p.state = 534; p.team = teams[i]; p.ping = i ? pings[i] : -1;
        p.kills = i ? 100 + i * 37 : 0; p.deaths = i ? 99 - i : 0; p.points = i ? 1250 + i * 111 : 0; p.kicking = i == 5;
        s.players.push_back(p);
    }
    s.black = {"TROLL123", "CHEATER_X", "WWWWWWWWWWWWWWW"};
    s.white = {"SNAKEEATER1964", "EVA", "RAIKOV.KGB", "MMMMMMMMMMMMMMM"};
    s.kicked = {{"CHEATER_X", "black", 65}, {"AFK_PLAYER", "kick", 3700}, {"GUEST42", "white", 12}};
    s.cpu = 41.5; s.rssMb = 283; s.procUptime = 190000; s.memTotal = 3813; s.memAvail = 2950; s.sysUptime = 864000; s.cores = 2; s.diskFree = 31.4;
    s.load = {0.41};
    s.bot = 1; s.port = 5730; s.botsMax = 3;
    {
        BotInfo b;
        b.bot = 1; b.port = 5730; b.account = "SNAKEEATER1964"; b.service = "active"; b.online = "online"; b.name = "WWWWWWWWWWWWWWW"; b.players = 8; b.max = 9; b.enabled = true;
        b.unranked = 1;
        s.bots.push_back(b);
        b.unranked = -1;
        b.bot = 2; b.port = 5731; b.account = "WWWWWWWWWWWWWWW"; b.online = "starting"; b.reason = "logging in / creating the game"; b.name = ""; b.players = 0;
        s.bots.push_back(b);
        b.bot = 3; b.port = 5732; b.account = "BOT2"; b.online = "offline"; b.reason = "log-in error: Server login failed. ( 816:-4 )";
        s.bots.push_back(b);
        // (beta 16) a bot whose launcher waits before its next start (its service "activating")
        b.bot = 4; b.port = 5733; b.account = "BOT4"; b.service = "activating"; b.online = "starting"; b.reason = "no status yet";
        s.bots.push_back(b);
        b.bot = 10; b.port = 5739; b.account = ""; b.service = "inactive"; b.online = "offline"; b.reason = "service inactive"; b.enabled = false;
        s.bots.push_back(b);
    }
    firewall_ = "ufw";
    nbResult_ = TF("m_fw_open", "ufw", 5732) + " " + TF("m_fw_provider", 5732);
    net_.custom = true; net_.web = "https://www.example-mgo1-server.org/mgs3/"; net_.gate = "gate.example-mgo1-server.org";
    net_.stun = "stun.example-mgo1-server.org"; net_.gatePort = 65535; net_.stunPort = 65535;
    netServer_ = net_; netServer_.gatePort = 5731;          // not saved yet: the page shows it
    NetToBuffers(net_, nWeb_, nGate_, nGatePort_, nStun_, nStunPort_);
    netLoaded_ = true;
    // the Game Report: every kind of line, the longest names
    {
        const long long t0 = 1790449000000LL;
        const char* evs[] = {
            R"J({"t":0,"k":"start","bot":1,"port":5730,"v":"build 1.00 1b29dfddb4cb","pk":[]})J",
            R"J({"t":0,"k":"game","name":"WWWWWWWWWWWWWWW","max":9,"rot":"1:2,0:8,4:7,2:10,3:3","pk":[{"l":"4310","d":"o","n":262,"x":"4d474f3120455520484f5354000000004465646963617465"}]})J",
            R"J({"t":0,"k":"open","pk":[{"l":"4316","d":"o","n":0,"x":""}]})J",
            R"J({"t":0,"k":"join","a":"WWWWWWWWWWWWWWW","pk":[{"l":"4340","d":"o","n":4,"x":"00001319"},{"g":"75100f","p":[1]}]})J",
            R"J({"t":0,"k":"team","a":"WWWWWWWWWWWWWWW","tm":"red","pk":[{"l":"4344","d":"o","n":5,"x":"0000131900"}]})J",
            R"J({"t":0,"k":"restart","pk":[{"g":"760121"}]})J",
            R"J({"t":0,"k":"round","i":0,"of":5,"s":5940,"r":1,"m":2,"rn":2,"rof":3,"n":8,"pk":[{"ph":[1,2],"e":0,"s":5940}]})J",
            R"J({"t":0,"k":"kill","w":"scorpion","hs":1,"pts":2,"a":"WWWWWWWWWWWWWWW","b":"MMMMMMMMMMMMMMM","pk":[{"g":"751010","p":[1,2]},{"s":"MMMMMMMMMMMMMMM","o":22,"v":3,"a":98,"b":99},{"s":"WWWWWWWWWWWWWWW","o":20,"v":2,"a":136,"b":137},{"s":"WWWWWWWWWWWWWWW","o":64,"v":2,"a":7,"b":8},{"s":"WWWWWWWWWWWWWWW","o":58,"v":2,"a":1,"b":2},{"s":"WWWWWWWWWWWWWWW","o":30,"v":2,"a":1360,"b":1362}]})J",
            R"J({"t":0,"k":"stun","cqc":1,"a":"RevolverOcelot","b":"The_Boss_1964","pk":[{"g":"751006","p":[3,4]},{"s":"RevolverOcelot","o":24,"v":4,"a":0,"b":1}]})J",
            R"J({"t":0,"k":"teamkill","a":"Raikov.KGB","b":"VOLGIN-ELEC","pk":[{"g":"751005","p":[6,7]}]})J",
            R"J({"t":0,"k":"suicide","a":"sokolov","pk":[{"g":"751005","p":[8,8]}]})J",
            R"J({"t":0,"k":"selfstun","a":"EVA","pk":[{"g":"751006","p":[5,5]}]})J",
            R"J({"t":0,"k":"grab","first":1,"a":"The_Boss_1964","pk":[{"g":"750014","p":[4]}]})J",
            R"J({"t":0,"k":"capleft","s":15,"tm":"blue","pk":[{"cd":899}]})J",
            R"J({"t":0,"k":"goal","tm":"blue","a":"The_Boss_1964","pk":[{"kr":0,"h":-1,"x":-11000,"y":2150,"z":-11000},{"s":"The_Boss_1964","o":48,"v":5,"a":0,"b":1}]})J",
            R"J({"t":0,"k":"grab","stop":7,"a":"RevolverOcelot","pk":[{"kr":0,"h":3,"x":-10883,"y":1561,"z":-9873}]})J",
            R"J({"t":0,"k":"drop","a":"RevolverOcelot","pk":[{"kr":0,"h":-1,"x":-9012,"y":1502,"z":-8870}]})J",
            R"J({"t":0,"k":"capleft","s":10,"tm":"blue","pk":[{"cd":599}]})J",
            R"J({"t":0,"k":"capture","a":"The_Boss_1964","pk":[{"s":"The_Boss_1964","o":44,"v":5,"a":0,"b":1}]})J",
            R"J({"t":0,"k":"radio","a":"EVA","n":3,"pk":[{"s":"EVA","o":50,"v":6,"a":4,"b":7}]})J",
            R"J({"t":0,"k":"kicked","a":"CHEATER_X","why":"black","pk":[{"l":"4346","d":"o","n":4,"x":"0000131a"}]})J",
            R"J({"t":0,"k":"admin","do":"blacklist-add","a":"CHEATER_X"})J",
            R"J({"t":0,"k":"end","i":0,"left":118,"r":1,"m":2,"rn":2,"rof":3,"wins":[1,2],"tickets":[0,17],"win":"blue","pk":[{"tw":"blue"},{"ph":[2,4],"e":0,"s":118}]})J",
            R"J({"t":0,"k":"score","a":"WWWWWWWWWWWWWWW","kills":137,"deaths":99,"pts":1362,"stuns":12,"hs":2,"pk":[{"l":"4390","d":"o","n":120,"x":"000013190000008900000063000200000000000c00000000"}]})J",
            R"J({"t":0,"k":"next","i":1,"of":5,"r":0,"m":8,"pk":[{"l":"4392","d":"o","n":1,"x":"01"}]})J",
            R"J({"t":0,"k":"leave","a":"MMMMMMMMMMMMMMM","pk":[{"l":"4342","d":"o","n":4,"x":"0000131b"}]})J",
            R"J({"t":0,"k":"error","x":"Server login failed. ( 816:-4 )","pk":[{"sc":"Server login failed. ( 816:-4 )"}]})J",
            R"J({"t":0,"k":"stop","why":"restart"})J"};
        int i = 0;
        report_.Show("demo:22", 1);
        for (const char* e : evs) {
            Json j;
            if (!Json::Parse(e, j)) continue;
            j.o["t"].kind = Json::Num; j.o["t"].n = (double)(t0 + 61000LL * i++);
            report_.Add(j, false);
        }
    }
    demoInstall_.running = true; demoInstall_.phase = "upload"; demoInstall_.file = "mgo1host.exe.gz";
    demoInstall_.allDone = 45ull << 20; demoInstall_.allTotal = 88ull << 20;
    demoInstall_.lines = {"@il_server|x86_64|ubuntu 24.04|0|36120", "@il_disc|%USERPROFILE%\\Documents\\MGO1\\MGS3 Subsistence disc 2 (online)",
                          "@il_disc_same", "@il_sent|13|84", "@il_gzip|-2|@se_timeout", "@il_unpacked", "@is_wine", "@is_wine_version|wine-9.0 (Ubuntu 9.0~repack-4build3)",
                          "@is_files", "@is_prefix", "@is_firewall", "@is_fw_open|ufw|5730", "@is_fw_provider|5730", "@is_restart|1",
                          "@is_installed|build 1.00 d11286182942"};
    GameSettings& g = gs_;
    g.name = "WWWWWWWWWWWWWWW";
    g.comment = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
    g.password = "ABCDEFGHIJKLMNO";
    g.rotation.clear();
    const int rot[15][2] = {{4, 7}, {1, 10}, {2, 3}, {3, 9}, {0, 1}, {4, 10}, {2, 7}, {3, 3}, {1, 9}, {0, 12}, {4, 9}, {2, 10}, {3, 7}, {1, 3}, {4, 3}};
    for (const auto& r : rot) g.rotation.push_back({r[0], r[1]});
    g.red = 2; g.blue = 0; g.maxJoin = 8; g.weapons = 3; g.unique = false; g.ff = true; g.tkKick = true; g.idle = true; g.autoBal = true;
    g.voice = true; g.ghosts = true; g.ratingRestr = 1; g.rating = 9900; g.tkCount = 99; g.idleMin = 99; g.balanceDiff = 6;
    for (const GameRule& r : GameRules())
        for (int k = 0; k < RV_COUNT; ++k)
            if (r.values[k].hi > 0) g.Value(r, k) = r.values[k].hi;
    gsServer_ = g;
    gsServer_.rating = 1000;                  // not saved yet: the page shows it
    // (3.5) agent 3.7's account: the settings changed elsewhere, the running game still on its own (three of the longest
    // names), a rotation entry it left out; map 7 not on the server (MapHere: its NOT ON SERVER tag, the form's error)
    gsSync_ = gsRunning_ = gsElsewhere_ = true;
    gsPending_ = {"rotation", "idle_kick_minutes", "tdm_tickets", "rumble_roses"};
    gsLeftOut_ = {"5:13", "6:15"};
    gsReal_ = 7;
    snprintf(gName_, sizeof gName_, "%s", g.name.c_str());
    snprintf(gComment_, sizeof gComment_, "%s", g.comment.c_str());
    snprintf(gPass_, sizeof gPass_, "%s", g.password.c_str());
    // the game settings this PC keeps: the last ones saved, as many presets as it keeps, the longest names
    cfg.lastGame = GameToPc(g, "");
    for (int i = 0; i < Config::kMaxPresets; ++i)
        cfg.presets.push_back(GameToPc(g, i == 0 ? std::string(24, 'W') : i == 1 ? "EU TDM MOUNTAINTOP 10MIN" : "PRESET " + std::to_string(i + 1)));
    snprintf(gPreset_, sizeof gPreset_, "%s", std::string(24, 'M').c_str());     // a new name, the list full: its error shown
    gsLoaded_ = aLoaded_ = aPwSet_ = true;
    snprintf(aId_, sizeof aId_, "%s", "SNAKEEATER1964");
    installedVersion_ = "build 1.00 d11286182942";
    logAuto_ = false;
    // the host's journal: every kind of line the LOG page shows in the program's language
    const char* lines[] = {
        "Started MGO1 dedicated host (bot 1)",
        "mgo1host: bot 1, UDP port 5730, disc /opt/mgo1host/disc",
        "ps2 f248: text: \"[Play Using Detailed Settings]|Configure network and account settings|before starting\"",
        "step 10: \"[Account ID]|Enter your account ID\" -> press 2000 at frame 1394 (4 frames) dg 1352",
        "step 11: \"\" -> type 0 at frame 1443 (4 frames) dg 1400",
        "ps2 f1790: text: \"Server login failed.|( 816:-4 )\"",
        "ps2 f2453: host settings from Z:\\var\\lib\\mgo1host\\bot1\\run\\game.ini: name comment password rotation; host only",
        "ps2 f2781: host settings: 0x4310 from 01418770: \"WWWWWWWWWWWWWWW\", host only 1, rotation 01:02",
        "ps2 f2820: host watch: hosting (frame 2820)",
        "ps2 f3000: blacklist: 3 name(s)",
        "ps2 f3000: whitelist: 4 name(s), only these may play",
        "ps2 f26700: lists: P2 \"CHEATER_X\" is on the blacklist: kicked",
        "ps2 f26701: kick P2 \"CHEATER_X\": room message 04 sent, kick requested",
        "ps2 f30000: command players: P1 state 216 flags 3 (host) name \"SNAKE\"; P2 state 216 flags 0 name \"WWWWWWWWWWWWWWW\"; P3 state 216 flags 0 name \"EVA\";",
        "ps2 f36000: host watch: the host entry is gone (frame 36000)",
        "ps2 f37800: host watch: no game for 30 s: the process ends",
        "Main process exited, code=2",
        "Scheduled restart job, restart counter is at 1.",
        "ps2 f36000: host watch: no game of its own after 10 minutes: the process ends",
        "ps2 f40000: host watch: the lobby's connection is closed (descriptor 9, frame 40000; its last data 12 s ago; last sent 4392 seq 77 12 s ago, "
        "last received 4393 seq 76 12 s ago; host entry still there): a new process in 10 s unless the game opens a new one",
        "ps2 f40180: host watch: the lobby's connection is open again (descriptor 10) after 3 s: the process goes on",
        "ps2 f41000: host watch: the lobby's connection still closed after 10 s: the process ends (6), the service starts a new one",
        "mgo1host: the game ended: the lobby connection was lost (closed and not opened again in time); a new game",
        // (v141 beta 7) SaveMGO's rankings and the dolls
        "ps2 f60: unranked: auto, the statistics of a game with a doll sent with a VS rating of 0 (MGO1_HOST_UNRANKED)",
        "ps2 f5640: unranked: a doll in the game (P2, accountless doll 1, user id 0x7f000001): its statistics go unranked to the game's end",
        "ps2 f28731: lobby filter: 4390 for user id 0x130f sent unranked (vs_rating 0, was 819: a doll in this game)",
        "ps2 f28790: unranked: a new round (0x4392 sent): this game had dolls, its later rounds stay unranked (the VS rating carries over)",
        "/opt/mgo1host/mgo1host.sh: line 15: HOST_ACCOUNT_ID: HOST_ACCOUNT_ID is not set (/etc/mgo1host/bot1.env)",
        "mgo1host: /opt/mgo1host/disc/MGS/NET/STAGE_N.DAT is missing (the disc's files)",
        "ps2 f18000: usbkb: READ #1200 (frame 1306) args 00 00 00 00"};
    int k = 0;
    for (const char* l : lines) {
        char b[64]; snprintf(b, sizeof b, "2026-09-26T19:%02d:12+0200 vm mgo1host@1: ", k++);
        log_.push_back(std::string(b) + l);
    }
    // the daily logs: every kind of line they show in the program's language, the longest names
    {
        LogSourceInfo b1; b1.name = "bot1";
        LogDayInfo d; d.day = "2026-09-28"; d.lines = 88888; d.errors = 999; d.warnings = 9999; d.crashes = 99; b1.days.push_back(d);
        d.day = "2026-09-27"; d.crashes = 0; b1.days.push_back(d);
        d.day = "2026-09-26"; d.errors = d.warnings = 0; b1.days.push_back(d);
        LogSourceInfo d3; d3.name = "doll3"; d.day = "2026-09-28"; d3.days.push_back(d);
        LogSourceInfo m; m.name = "manager"; m.days.push_back(d);
        logSources_ = {b1, d3, m};
        logSrc_ = 1; logDay_ = 0; logLevel_ = 1; logListLoaded_ = true;
        const char* dl[] = {
            "2026-09-28T10:00:01.001Z I mgo1doll: doll 16 for bot 10, UDP port 5755, difficulty 5, team auto, version build 1.00 0123456789ab",
            "2026-09-28T10:00:02.001Z I doll f0: on (status in Z:\\var\\lib\\mgo1host\\doll16); difficulty 5, team spectator",
            "2026-09-28T10:00:03.001Z I doll f2000: lobby: joining WWWWWWWWWWWWWWW",
            "2026-09-28T10:00:04.001Z I ps2 f2914: network: upstream bandwidth 512 kbps (256 on the card, the game's default), the send budget 512000 bits a second (the game's maximum)",
            "2026-09-28T10:00:05.001Z I doll f3098: map n024a: building its navigation",
            "2026-09-28T10:00:06.001Z I doll f4918: deathmatch: the doll's unit Ocelot unit",
            "2026-09-28T10:00:07.001Z I doll f4918: deathmatch: wearing GRU, to Ocelot unit",
            "2026-09-28T10:00:08.001Z I doll f5789: deathmatch: wearing Ocelot unit (its unit)",
            "2026-09-28T10:00:09.001Z I doll f6000: activity: fighting WWWWWWWWWWWWWWW",
            "2026-09-28T10:00:10.001Z I doll f6001: activity: @longest WWWWWWWWWWWWWWW",
            "2026-09-28T10:00:11.001Z I doll f6100: score: kills 999, deaths 999, stuns 0, CQC 0, headshots 0",
            "2026-09-28T10:00:12.001Z I doll f6200: order: follow WWWWWWWWWWWWWWW",
            "2026-09-28T10:00:13.001Z I doll f6201: order: team spectator",
            "2026-09-28T10:00:14.001Z I doll f6202: order: difficulty 5",
            "2026-09-28T10:00:15.001Z I doll f6203: order: weapons mosin,mk22 (the next spawn)",
            "2026-09-28T10:00:16.001Z I doll f6204: order: leave the game",
            "2026-09-28T10:00:17.001Z W doll f6300: stuck at -846 306 2399 going to 1370 38 5370 (corner 1 of 2, 3707 away, area 1, hips 300, state 8000008 800000)",
            "2026-09-28T10:00:18.001Z E CRASH: exception c0000005 at mgo1host.exe+12a4f0 (reading 0000000000000000); the PS2 code: last call 001f55c0 from 0017d610",
            "2026-09-28T10:00:19.001Z E mgo1doll: the game crashed (its CRASH line tells where); it starts again",
            "2026-09-28T10:00:20.001Z E mgo1doll: the game was ended by signal 9 (killed: out of memory, or forced)",
            "2026-09-28T10:00:21.001Z E mgo1doll: the game ended with code 255",
            "2026-09-28T10:00:22.001Z I mgo1doll: back in the lobby after a match; it starts again",
            "2026-09-28T10:00:23.001Z W mgo1doll: its game is not in the list; it starts again",
            "2026-09-28T10:00:24.001Z I mgo1doll: stopped (its service was stopped: the manager or the program)",
            "2026-09-28T10:00:25.001Z I mgo1doll-manager: doll 16: started for bot 10's game (8 real players, 8 seats)",
            "2026-09-28T10:00:26.001Z I mgo1doll-manager: doll 16: told to leave bot 10's game (8 real players, 8 seats, one kept for a real player)",
            "2026-09-28T10:00:27.001Z W mgo1doll-manager: doll 16: stopped (still there 30 s after its leave order)",
            "2026-09-28T10:00:28.001Z I mgo1doll-manager: the server can run 16 dolls at once besides its 10 bots",
            "2026-09-28T10:00:29.001Z E mgo1doll-manager: stopped by an error: OSError: [Errno 28] No space left on device",
            "2026-09-28T10:00:30.001Z W mgo1host: the game ended: its game was lost (no game for 30 s); a new game",
            "2026-09-28T10:00:31.001Z I ps2 f40: vu0: program at 0010 (micro memory f91a215596f561de) not recompiled: interpreted",
            "2026-09-28T10:00:32.001Z I doll f7000: roll: into WWWWWWWWWWWWWWW (the objective's carrier): knocked down",
            "2026-09-28T10:00:33.001Z I doll f7001: roll: into WWWWWWWWWWWWWWW (no rounds left): missed",
            "2026-09-28T10:00:34.001Z I doll f7002: roll: into WWWWWWWWWWWWWWW (before a close attack): not reached",
            "2026-09-28T10:00:35.001Z I doll f7003: emote: after a kill (WWWWWWWWWWWWWWW's kill camera)",
            "2026-09-28T10:00:36.001Z I doll f7004: emote: answering WWWWWWWWWWWWWWW's emote",
            "2026-09-28T10:00:37.001Z I doll f7005: emote: greeting WWWWWWWWWWWWWWW",
            "2026-09-28T10:00:38.001Z I doll f7006: blinded by a flash (its white 128)",
            "2026-09-28T10:00:39.001Z I doll f7007: goggles: the thermal goggles on (battery 300)",
            "2026-09-28T10:00:40.001Z I doll f7008: goggles: the thermal goggles off (battery 20)",
            "2026-09-28T10:00:41.001Z I doll f7009: chaff: a chaff grenade (enemies close)",
            "2026-09-28T10:00:42.001Z I doll f7010: grenade: a smoke between it and WWWWWWWWWWWWWWW: yaw 29 pitch -27.5, goes off 0.1 m from the point (0 bounces)",
            "2026-09-28T10:00:43.001Z I doll f7011: codec: \"enemy sighted\" from WWWWWWWWWWWWWWW not heard (jammed by a chaff grenade)",
            "2026-09-28T10:00:44.001Z I doll f7012: codec: \"enemy sighted\" not sent (jammed by a chaff grenade)",
            // (v141 beta 6) the ladder, the live level by enemies and allies, the real players, the lobby's loss
            "2026-09-28T10:00:45.001Z I doll f8000: level: 5 EXPERT (the difficulty set)",
            "2026-09-28T10:00:46.001Z I doll f8001: level: live, between 1 TRAINING and 10 PRO",
            "2026-09-28T10:00:47.001Z I doll f8002: DOLL ADAPT: old=6.40 new=6.80 target=7.50 base=5 enemy_strength=7.9 ally_strength=5.8 balance=+0.40 "
            "confidence=0.81 reason=enemy_team_outperforming (enemies 1 real, allies 0 real; tickets 12 against 30)",
            "2026-09-28T10:00:48.001Z I doll f8003: level: tier VETERAN (7.02), was ELITE",
            "2026-09-28T10:00:49.001Z I doll f8004: level: the elite moves on (level 6.02)",
            "2026-09-28T10:00:50.001Z I doll f8005: level: 5 EXPERT (the difficulty set, the live level off)",
            "2026-09-28T10:00:51.001Z I doll f8006: real players: 1 -> 0",
            "2026-09-28T10:00:52.001Z I doll f8007: doll: entering spectator, no real player in the game",
            "2026-09-28T10:00:53.001Z I doll f8008: real players: 0 -> 1: WWWWWWWWWWWWWWW",
            "2026-09-28T10:00:54.001Z I doll f8009: doll: real player joined, leaving spectator",
            "2026-09-28T10:00:55.001Z I doll f8010: rival: WWWWWWWWWWWWWWW dominates the dolls 1.20 (4 deaths by him, 2 at one shot): his strength in the live level",
            "2026-09-28T10:00:56.001Z I doll f8011: order: difficulty 7 (VETERAN)",
            "2026-09-28T10:00:57.001Z I doll f8012: order: live level between 3 (NORMAL) and 8 (MASTER)",
            // (v141 beta 7) level 10's own play
            "2026-09-28T10:00:57.501Z I doll f8013: DOLL CHEAT: on at level 10.00 (PRO, the difficulty set 10, the live level, enemy strength 8.9, ally strength 5.5, confidence 0.74)",
            "2026-09-28T10:00:57.801Z I doll f8014: DOLL CHEAT: off at level 9.42 (EXTREME, the difficulty set 10, the live level, enemy strength 6.1, ally strength 5.5, confidence 0.80)",
            "2026-09-28T10:00:58.001Z W mgo1host: the game ended: the lobby connection was lost (closed and not opened again in time); a new game",
            "2026-09-28T10:00:59.001Z W mgo1doll-manager: doll 3: join timeout: expected DOLL_3 (DOLL_NAME ''), the host's players [WWWWWWWWWWWWWWW (real)]; "
            "the doll's status (2 s old): name DOLL_3, identity uid, activity waiting_for_players, real players 1; waiting 45 s"};
        for (const char* l : dl) dayLines_.push_back(l);
        dayTotal_ = 88888; dayNext_ = 2000;
    }
    DemoCrashes();
}

void App::SetDemoPage(int page) {
    demoDialog_ = page >= PG_COUNT ? page : 0;
    page_ = page == DEMO_MENU ? PG_PLAYERS : page == DEMO_ADDBOT ? PG_BOTS : page == DEMO_CONFIRM ? PG_SERVER : page == DEMO_LOGINERR ? PG_DASHBOARD
          : page == DEMO_DOLLMENU || page == DEMO_AGENTOLD || page == DEMO_DOLLNAME || page == DEMO_DOLLVD ? PG_DOLLS
          : page == DEMO_VDOLLSOLD || page == DEMO_VDOLLS35 ? PG_VDOLLS
          : page == DEMO_DAILYLOG || page == DEMO_CRASHES || page == DEMO_CRASH || page == DEMO_CRASHOLD ? PG_LOG : page == DEMO_PRESETS ? PG_GAME
          : page >= DEMO_QS_EMPTY ? PG_QUICK : page >= PG_COUNT ? PG_SERVER : page;
    demoAgentOld_ = page == DEMO_AGENTOLD || page == DEMO_CRASHOLD;
    // (beta 16) the crash reports: the list, one report open, an agent that has none
    logCrash_ = page == DEMO_CRASHES || page == DEMO_CRASH || page == DEMO_CRASHOLD;
    crOld_ = page == DEMO_CRASHOLD;
    crOpen_ = page == DEMO_CRASH ? "bot2/2026-11-10_005015_hung.txt" : "";
    dollsAccountless_ = !demoAgentOld_;
    dollsVdoll_ = !demoAgentOld_;                     // (3.2: an older agent has no "as the virtual dolls")
    dollsAim_ = !demoAgentOld_;                       // (3.3: an older agent has no aim)
    vdControls_ = page != DEMO_VDOLLSOLD;             // (3.3: agent 3.4 has no virtual dolls' controls: CONTROLS hidden)
    vdAdapt_ = page != DEMO_VDOLLSOLD && page != DEMO_VDOLLS35;   // (3.4: an agent before 3.6: the LIVE LEVEL greyed, its reason said)
    vfBot_ = 0;                                       // (the VIRTUAL DOLLS form filled again for each page)
    logDaily_ = page == DEMO_DAILYLOG;
    demoScrollEnd_ = page == DEMO_PRESETS;
    demoBot_ = page == DEMO_LOGINERR ? 3 : 1;
    askYes_ = nullptr;
    trustShown_ = false;
    if (GImGui && GImGui->OpenPopupStack.Size > 0) ImGui::ClosePopupToLevel(0, false);
    if (page == DEMO_CONFIRM) Ask(T("c_update"), [] {});
    if (page == DEMO_MENU) { menuP_ = 2; menuName_ = "WWWWWWWWWWWWWWW"; menuOpen_ = true; }
    if (page == DEMO_ADDBOT) { botMenuN_ = 2; botMenuOpen_ = true; }
    if (page == DEMO_DOLLMENU) { dollMenuN_ = 2; dollMenuOpen_ = true; dmPlayer_ = 0; }
    nbResult_ = TF("m_fw_open", "ufw", 5732) + " " + TF("m_fw_provider", 5732);
    toast_ = TF("m_blacklisted", "WWWWWWWWWWWWWWW");
    if (page == DEMO_LOGINERR) {                      // an error of the server's program, in the program's language
        Json j;
        Json::Parse(R"J({"ok":false,"error":"UDP port 5731 is already bot 2's","code":"port_used","args":["5731","2"]})J", j);
        toast_ = TF("m_cmd_failed", ErrText(j).c_str());
    }
    if (page == DEMO_PRESETS) toast_ = TF("m_preset_filled", "EU TDM MOUNTAINTOP 10MIN", 10);
    if (page == DEMO_DOLLNAME) {                      // (v141 beta 5) a doll with no account renamed: the longest name typed
        dollMenuN_ = 4; dollMenuOpen_ = true; dmPlayer_ = 0; dmNameFor_ = 4;
        snprintf(dmName_, sizeof dmName_, "%s", "WWWWWWWWWWWWWW2");   // (doll 10's is WWWWWWWWWWWWWWW: taken)
        toast_ = TF("m_doll_renamed", 4, "WWWWWWWWWWWWWW2");
    }
    if (page == DEMO_DOLLVD) { dollMenuN_ = 10; dollMenuOpen_ = true; dmPlayer_ = 0; dmNameFor_ = 0; }   // (3.2) a doll "as the virtual dolls": its menu
    if (page == PG_BOTS) toast_ = TF("m_bot_added_last", 10, 5739);
    QuickDemo(page);                                  // (build 1.00) QUICK SETUP's state (quick.cpp), its own pages only
    if (showcase_) ShowcasePage(page);                // (build 1.00) the showcase's names, no layout test's message
    toastAt_ = ImGui::GetTime();
    ui::SetHelp("", "");
}
