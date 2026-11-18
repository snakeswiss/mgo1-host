#include "report.h"
#include "i18n.h"
#include "platform.h"
#include <cstdio>
#include <ctime>
#include <iterator>

std::string ReportClock(long long t, bool date) {
    const time_t s = (time_t)(t / 1000);
    struct tm lt{};
    plat::LocalTime(s, lt);
    char b[32];
    strftime(b, sizeof b, date ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S", &lt);
    return b;
}

static std::string Dur(int s) {
    if (s < 0) return "-";
    if (s >= 3600) return TF("t_hm", s / 3600, s % 3600 / 60);
    if (s >= 60) return s % 60 ? TF("t_ms", s / 60, s % 60) : TF("t_m", s / 60);
    return TF("t_s", s);
}

static const char* Team(const std::string& t) { return t == "red" ? T("v_red") : t == "blue" ? T("v_blue") : "-"; }

// a counter of the players' statistics by its offset in their variable set (0x14 + 2 i)
static std::string StatName(int off) {
    if (off < 0x14 || off > 0x52 || (off & 1)) return "?";
    char k[16];
    snprintf(k, sizeof k, "st_%02x", off);
    return T(k);
}

// the rotation "r:m,r:m" in words: "DEATHMATCH, KILLHOUSE A; ..."
static std::string Rotation(const std::string& rot) {
    std::string out;
    size_t a = 0;
    while (a < rot.size()) {
        size_t e = rot.find(',', a); if (e == std::string::npos) e = rot.size();
        int r = -1, m = -1;
        if (sscanf(rot.substr(a, e - a).c_str(), "%d:%d", &r, &m) == 2) {
            if (!out.empty()) out += T("sep_list");
            out += RuleName(r) + ", " + MapName(m);
        }
        a = e + 1;
    }
    return out;
}

static std::string Detail(const Json& d) {
    if (d.kind == Json::Str) return d.s;                     // an older host's words
    if (d.kind != Json::Obj) return "";
    if (!d["g"].IsNull()) {                                  // a message of the game to its HUD, sent on to every player
        const std::string code = d["g"].Text();
        char k[16];
        snprintf(k, sizeof k, "gm_%s", code.c_str());
        const char* name = T(k);
        if (!strcmp(name, k)) name = "?";
        std::string ps;
        for (size_t i = 0; i < d["p"].Size(); ++i) ps += (i ? ", " : "") + std::to_string(d["p"][i].Int());
        return ps.empty() ? TF("dt_msg0", code.c_str(), name) : TF("dt_msg", code.c_str(), name, ps.c_str());
    }
    if (!d["ph"].IsNull()) {                                 // the room's variables: the round's phase
        auto ph = [](int p) { char k[16]; snprintf(k, sizeof k, "ph_%d", p >= 0 && p <= 5 ? p : 0); return std::string(T(k)); };
        return TF("dt_room", ph(d["ph"][(size_t)0].Int()).c_str(), ph(d["ph"][(size_t)1].Int()).c_str(), d["e"].Int() + 1, Dur(d["s"].Int()).c_str());
    }
    if (!d["o"].IsNull()) {                                  // a counter of a player's statistics
        return TF("dt_stat", d["s"].Text().c_str(), StatName(d["o"].Int()).c_str(), d["a"].Int(), d["b"].Int(), d["v"].Int(), d["o"].Int());
    }
    if (!d["l"].IsNull()) {                                  // a frame of the lobby server
        const std::string c = d["l"].Text();
        if (d["d"].Text() == "i") return TF("dt_lobby_in", c.c_str(), d["r"].Int());
        char k[16];
        snprintf(k, sizeof k, "pk_%s", c.c_str());
        const char* name = T(k);
        if (!strcmp(name, k)) name = "?";
        const int n = d["n"].Int();
        if (n == 0) return TF("dt_lobby0", c.c_str(), name);
        if (n == 1) return TF("dt_lobby1", c.c_str(), name, d["x"].Text().c_str());
        return TF("dt_lobby", c.c_str(), name, n, d["x"].Text().c_str());
    }
    if (!d["cd"].IsNull()) return TF("dt_capture", d["cd"].Int());   // the capture's countdown
    if (!d["kr"].IsNull())                                   // the KEROTAN: where it is
        return TF("dt_kerotan", d["x"].Int(), d["y"].Int(), d["z"].Int(), d["kr"].Int() + 1, d["h"].Int());
    if (!d["tw"].IsNull()) return TF("dt_teamwin", Team(d["tw"].Text()));
    if (!d["tv"].IsNull()) {                                 // a player's team in its variable set
        const int t = d["tv"][(size_t)1].Int();
        return TF("dt_teamvar", d["tv"][(size_t)0].Int(), t == 0 ? T("v_red") : t == 1 ? T("v_blue") : "-");
    }
    if (!d["sc"].IsNull()) return TF("dt_screen", d["sc"].Text().c_str());
    return "";
}

ReportLine ReportDescribe(const Json& e) {
    ReportLine l;
    l.t = (long long)e["t"].Dbl();
    const std::string k = e["k"].Text(), a = e["a"].Text(), b = e["b"].Text();
    const char* A = a.empty() ? "?" : a.c_str();
    const char* B = b.empty() ? "?" : b.c_str();
    // the extras of a kill or a stun: "(Scorpion, headshot, +2 points)"
    std::vector<std::string> extra;
    if (e["w"].Text() == "knife") extra.push_back(T("ev_knife"));
    else if (e["w"].Text() == "scorpion") extra.push_back("Scorpion");
    if (e["hs"].Truthy()) extra.push_back(T("ev_headshot"));
    if (e["cqc"].Truthy()) extra.push_back("CQC");
    if (e["snake"].Truthy()) extra.push_back(T("ev_was_snake"));
    if (e["pts"].Int() > 0 && k != "score" && k != "end") extra.push_back(e["pts"].Int() == 1 ? std::string(T("ev_point1")) : TF("ev_points", e["pts"].Int()));
    // counters no message claimed, changed more than once in a moment (a round's own "n" is its players)
    if (e["n"].Int() > 1 && k != "round") extra.push_back(TF("ev_times", e["n"].Int()));
    std::string ex;
    for (size_t i = 0; i < extra.size(); ++i) ex += (i ? ", " : "") + extra[i];
    auto with = [&](const std::string& s) { return ex.empty() ? s : s + " (" + ex + ")"; };
    if (k == "kill") { l.cat = RC_FIGHTS; l.text = with(b.empty() ? TF("ev_kill1", A) : TF("ev_kill", A, B)); }
    else if (k == "teamkill") { l.cat = RC_FIGHTS; l.text = with(TF("ev_teamkill", A, B)); }
    else if (k == "suicide") { l.cat = RC_FIGHTS; l.text = with(TF("ev_suicide", A)); }
    else if (k == "death") { l.cat = RC_FIGHTS; l.text = with(TF("ev_death", A)); }
    else if (k == "stun") { l.cat = RC_FIGHTS; l.text = with(TF("ev_stun", A, B)); }
    else if (k == "teamstun") { l.cat = RC_FIGHTS; l.text = with(TF("ev_teamstun", A, B)); }
    else if (k == "selfstun") { l.cat = RC_FIGHTS; l.text = TF("ev_selfstun", A); }
    else if (k == "cqc") { l.cat = RC_FIGHTS; l.text = with(b.empty() ? TF("ev_cqc1", A) : TF("ev_cqc", A, B)); }
    else if (k == "join") { l.cat = RC_PLAYERS; l.text = TF("ev_join", A); }
    else if (k == "leave") { l.cat = RC_PLAYERS; l.text = TF("ev_leave", A); }
    else if (k == "kicked") {
        l.cat = RC_PLAYERS;
        const std::string w = e["why"].Text();
        l.text = TF(w == "black" ? "ev_kicked_black" : w == "white" ? "ev_kicked_white" : w == "kick" ? "ev_kicked_you" : "ev_kicked_game", A);
    }
    else if (k == "team") { l.cat = RC_PLAYERS; l.text = TF(e["change"].Truthy() ? "ev_team_change" : "ev_team", A, Team(e["tm"].Text())); }
    else if (k == "score") { l.cat = RC_PLAYERS; l.text = TF("ev_score", A, e["kills"].Int(), e["deaths"].Int(), e["pts"].Int(), e["stuns"].Int()); }
    else if (k == "chat") { l.cat = RC_PLAYERS; l.text = with(TF("ev_chat", A)); }
    else if (k == "radio") { l.cat = RC_PLAYERS; l.text = with(TF("ev_radio", A)); }
    else if (k == "food") { l.cat = RC_PLAYERS; l.text = with(TF("ev_food", A)); }
    else if (k == "game") {
        l.cat = RC_GAME;
        l.text = TF("ev_game", e["name"].Text().c_str(), e["max"].Int() > 1 ? e["max"].Int() - 1 : 8, Rotation(e["rot"].Text()).c_str());
    }
    else if (k == "open") { l.cat = RC_GAME; l.text = T("ev_open"); }
    else if (k == "round") {
        l.cat = RC_GAME;
        if (e["rof"].Int() > 1)
            l.text = TF("ev_round_sub", RuleName(e["r"].Int()).c_str(), MapName(e["m"].Int()), e["rn"].Int(), e["rof"].Int(), e["i"].Int() + 1, e["of"].Int(),
                        Dur(e["s"].Int()).c_str(), e["n"].Int());
        else l.text = TF("ev_round", RuleName(e["r"].Int()).c_str(), MapName(e["m"].Int()), e["i"].Int() + 1, e["of"].Int(), Dur(e["s"].Int()).c_str(), e["n"].Int());
    }
    else if (k == "end") {
        l.cat = RC_GAME;
        if (!e["win"].IsNull()) l.text = TF("ev_end_team", Team(e["win"].Text()));
        else if (!a.empty()) l.text = TF("ev_end_best", A, e["pts"].Int(), e["kills"].Int());
        else l.text = T("ev_end");
        // the round of its rule, the clock left, the teams' round wins and tickets
        std::vector<std::string> x;
        if (e["rof"].Int() > 1) x.push_back(TF("ev_round_of", e["rn"].Int(), e["rof"].Int()));
        if (e["left"].Int() > 0) x.push_back(TF("ev_left", Dur(e["left"].Int()).c_str()));
        if (e["wins"].Size() == 2) x.push_back(TF("ev_wins", T("v_red"), e["wins"][(size_t)0].Int(), T("v_blue"), e["wins"][(size_t)1].Int()));
        if (e["tickets"].Size() == 2) x.push_back(TF("ev_tickets", T("v_red"), e["tickets"][(size_t)0].Int(), T("v_blue"), e["tickets"][(size_t)1].Int()));
        std::string xs;
        for (size_t i = 0; i < x.size(); ++i) xs += (i ? T("sep_list") : "") + x[i];
        if (!xs.empty()) l.text += " (" + xs + ")";
    }
    else if (k == "next") { l.cat = RC_GAME; l.text = TF("ev_next", RuleName(e["r"].Int()).c_str(), MapName(e["m"].Int()), e["i"].Int() + 1, e["of"].Int()); }
    else if (k == "restart") { l.cat = RC_GAME; l.text = T("ev_restart"); }
    else if (k == "grab") {
        l.cat = RC_GAME;
        l.text = e["stop"].Int() > 0 ? TF("ev_grab_stop", A, e["stop"].Int()) : TF(e["first"].Truthy() ? "ev_grab_first" : "ev_grab", A);
    }
    else if (k == "drop") { l.cat = RC_GAME; l.text = TF("ev_drop", A); }
    else if (k == "goal") { l.cat = RC_GAME; l.text = with(e["tm"].IsNull() ? TF("ev_goal", A) : TF("ev_goal_team", A, Team(e["tm"].Text()))); }
    else if (k == "capleft") { l.cat = RC_GAME; l.text = TF("ev_capleft", Team(e["tm"].Text()), e["s"].Int()); }
    else if (k == "capture") { l.cat = RC_GAME; l.text = with(TF("ev_capture", A)); }
    else if (k == "quit") { l.cat = RC_GAME; l.text = T("ev_quit"); }
    else if (k == "start") { l.cat = RC_BOT; l.text = TF("ev_start", e["bot"].Int(1), e["port"].Int(), e["v"].Text().c_str()); }
    else if (k == "stop") {
        l.cat = RC_BOT;
        const std::string w = e["why"].Text();
        l.text = T(w == "restart" ? "ev_stop_restart" : w == "no-game" ? "ev_stop_nogame" : "ev_stop_lost");
    }
    else if (k == "error") { l.cat = RC_BOT; l.text = TF("ev_error", e["x"].Text().c_str()); }
    else if (k == "refused") { char c[8]; snprintf(c, sizeof c, "%04x", e["c"].Int()); l.cat = RC_BOT; l.text = TF("ev_refused", c, e["r"].Int()); }
    else if (k == "packet") { char c[8]; snprintf(c, sizeof c, "%04x", e["c"].Int()); l.cat = RC_BOT; l.text = TF("ev_packet", c); }
    else if (k == "admin") {
        l.cat = RC_BOT;
        const std::string d = e["do"].Text();
        if (d == "restart") l.text = T("ev_ad_restart");
        else if (d == "stop") l.text = T("ev_ad_stop");
        else if (d == "start") l.text = T("ev_ad_start");
        else if (d == "settings") l.text = T(e["restart"].Truthy() ? "ev_ad_settings_restart" : "ev_ad_settings");
        else if (d == "account") l.text = T("ev_ad_account");      // never the account itself
        else if (d == "kick") l.text = TF("ev_ad_kick", A);
        else if (d == "blacklist-add") l.text = TF("ev_ad_black_add", A);
        else if (d == "blacklist-remove") l.text = TF("ev_ad_black_remove", A);
        else if (d == "whitelist-add") l.text = TF("ev_ad_white_add", A);
        else if (d == "whitelist-remove") l.text = TF("ev_ad_white_remove", A);
        else if (d == "white-only") l.text = T(e["on"].Truthy() ? "ev_ad_white_on" : "ev_ad_white_off");
        else if (d == "bot-add") l.text = TF("ev_ad_bot_add", e["port"].Int());
        else if (d == "port") l.text = TF("ev_ad_port", e["port"].Int());
        else if (d == "servers") l.text = T(e["custom"].Truthy() ? "ev_ad_servers" : "ev_ad_savemgo");
        else if (d == "daily-restart") l.text = T("ev_ad_daily");
        else if (d == "doll-rename") l.text = TF("ev_ad_doll_rename", e["doll"].Int(), A);   // (v141 beta 5)
        else if (d == "vdoll-set") l.text = T(e["restart"].Truthy() ? "ev_ad_vdolls_restart" : "ev_ad_vdolls");   // (virtual dolls)
        else if (d == "vdoll-count") l.text = T("ev_ad_vdolls_count");          // (v150-host beta 15: live)
        else l.text = d;
    }
    else { l.cat = RC_BOT; l.text = k; }
    for (size_t i = 0; i < e["pk"].Size(); ++i) {
        const std::string d = Detail(e["pk"][i]);
        if (!d.empty()) l.details.push_back(d);
    }
    // the event's identity: its time, kind, players and details (the same event twice is written once)
    l.key = std::to_string(l.t) + "|" + k + "|" + a + "|" + b + "|" + std::to_string(e["pk"].Size()) + "|" + e["do"].Text();
    l.lang = CurLang();
    return l;
}

// ---- the page's lines ---------------------------------------------------------------------------
void Report::Show(const std::string& serverKey, int bot) {
    if (serverKey == server_ && bot == bot_) return;
    server_ = serverKey; bot_ = bot;
    lines_.clear(); shown_.clear();
}

void Report::Add(const Json& e, bool) {
    ReportLine l = ReportDescribe(e);
    if (!shown_.insert(l.key).second) return;
    l.src = e;
    // in time order: the watch's lines come after the day's ones, a late one finds its place
    auto it = lines_.end();
    while (it != lines_.begin() && std::prev(it)->t > l.t) --it;
    lines_.insert(it, std::move(l));
    while (lines_.size() > 5000) { shown_.erase(lines_.front().key); lines_.pop_front(); }
}

void Report::Relang() {
    for (auto& l : lines_) {
        if (l.lang == CurLang()) continue;
        ReportLine n = ReportDescribe(l.src);
        l.text = std::move(n.text); l.details = std::move(n.details); l.lang = n.lang;
    }
}

// ---- the files on this PC ------------------------------------------------------------------------
std::string Report::SafeName(const std::string& s) {
    std::string o;
    for (char c : s) o += (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) ? '_' : c;
    while (!o.empty() && (o.back() == '.' || o.back() == ' ')) o.pop_back();
    return o.empty() ? "server" : o;
}

std::string Report::Folder(const Config& cfg, const std::string& serverName, int bot) {
    return plat::Join(cfg.ReportDir(), SafeName(serverName), "Bot " + std::to_string(bot));
}

long long Report::Cursor(const Config& cfg, const std::string& serverKey, int bot) const {
    const auto it = cfg.reportAt.find(serverKey + "#" + std::to_string(bot));
    return it == cfg.reportAt.end() ? 0 : it->second;
}

int Report::Write(Config& cfg, const std::string& serverKey, const std::string& serverName, int bot, const std::vector<Json>& events) {
    const std::string ck = serverKey + "#" + std::to_string(bot);
    long long& cursor = cfg.reportAt[ck];
    std::set<std::string>& atCursor = writtenAtCursor_[ck];
    const std::string dir = Folder(cfg, serverName, bot);
    plat::MakeDirs(dir);
    int n = 0;
    FILE* f = nullptr;
    std::string day;
    for (const Json& e : events) {
        ReportLine l = ReportDescribe(e);
        if (l.t < cursor || (l.t == cursor && (atCursor.count(l.key) || atCursor.empty()))) continue;   // written before
        const std::string when = ReportClock(l.t, true);
        if (when.substr(0, 10) != day || !f) {
            if (f) fclose(f);
            day = when.substr(0, 10);
            const std::string path = plat::Join(dir, day + ".log");
            const bool fresh = !plat::IsFile(path);
            f = plat::Open(path, "ab");
            if (!f) return n;
            if (fresh) fputs("\xEF\xBB\xBF", f);                    // UTF-8, for Notepad
        }
        fprintf(f, "%s  %s\r\n", when.c_str(), l.text.c_str());
        for (const auto& d : l.details) fprintf(f, "                     > %s\r\n", d.c_str());
        if (l.t > cursor) { cursor = l.t; atCursor.clear(); }
        atCursor.insert(l.key);
        ++n;
    }
    if (f) fclose(f);
    return n;
}
