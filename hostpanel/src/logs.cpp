// The LOG page's daily logs: the files the server keeps for each bot, each PLAYABLE DOLL and the doll
// manager (host/mgo1log: /var/log/mgo1host/<bot1|doll1|manager>/<YYYY-MM-DD>.log, UTC days, 31 kept),
// over mgo1host-agent logs list / logs get. What a bot or a doll did, its errors (a crash, a failure)
// and warnings, in the program's language where the line is known, as written otherwise; saved on
// this PC to be sent to someone who helps (no log-in data: the server masks it, twice).
#include "app.h"
#include "i18n.h"
#include "platform.h"
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ui;

namespace {
int Cycle(int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; }
std::string Num(int v) { return std::to_string(v); }
bool Starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
// "n011a" -> 4 (the lobby's map numbers, rules.h)
int MapOfPage(const std::string& page) {
    const GameMap* m = FindMapByPage(page);
    return m ? m->id : 0;
}
const char* UnitLabel(const std::string& u) {
    return u == "GRU" ? "GRU" : u == "KGB" ? "KGB" : T("v_unit_ocelot");
}
std::string After(const std::string& s, const char* p) { const size_t a = s.find(p); return a == std::string::npos ? "" : s.substr(a + strlen(p)); }
int IntAfter(const std::string& s, const char* p) { const size_t a = s.find(p); return a == std::string::npos ? -1 : atoi(s.c_str() + a + strlen(p)); }
// (v141 beta 6) a format with one %d matched whole, up to its closing %n (sscanf counts conversions only: "doll %d: stopped
// (still there" took every manager line of a doll)
bool Whole(const std::string& m, const char* fmt, int* a) { int n = 0; return sscanf(m.c_str(), fmt, a, &n) == 1 && n > 0; }
}  // namespace

// a message of a doll's, of a launcher's or of the doll manager's, in the program's language ("": as it is)
std::string DollLogText(const std::string& m) {
    int a = 0, b = 0, c = 0, d = 0;
    if (Starts(m, "activity: ")) {
        const std::string rest = m.substr(10);
        const size_t sp = rest.find(' ');
        std::string arg = sp == std::string::npos ? "" : rest.substr(sp + 1);
        while (!arg.empty() && arg.back() == ' ') arg.pop_back();
        return DollActivityText(rest.substr(0, sp), arg);
    }
    if (sscanf(m.c_str(), "score: kills %d, deaths %d", &a, &b) == 2) return TF("lg_score", Num(a).c_str(), Num(b).c_str());
    // (v141 beta 6) "order: difficulty 7 (VETERAN)" (before: "order: difficulty 5"), its tier in the program's language
    if (Starts(m, "order: difficulty ")) return TF("lg_order_difficulty", DollSkillText(atoi(m.c_str() + 18)).c_str());
    if (Starts(m, "order: team ")) {
        const std::string t = m.substr(12);
        return TF("lg_order_team", T(t == "red" ? "v_team_red" : t == "blue" ? "v_team_blue" : t == "spectator" ? "v_team_spect" : "v_team_auto"));
    }
    if (Starts(m, "order: leave")) return T("lg_order_leave");
    // (v141 beta 6) "order: live level between 3 (NORMAL) and 8 (MASTER)": the bounds
    if (Starts(m, "order: live level between ") && sscanf(m.c_str() + 26, "%d (%*[A-Z]) and %d", &a, &b) == 2)
        return TF("lg_order_bounds", DollSkillText(a).c_str(), DollSkillText(b).c_str());
    if (Starts(m, "order: live level ")) return TF("lg_order_adapt", T(m.substr(18) == "on" ? "v_on" : "v_off"));
    if (Starts(m, "order: weapons ")) { std::string w = m.substr(15); const size_t p = w.find(" ("); if (p != std::string::npos) w.resize(p); return TF("lg_order_weapons", w.c_str()); }
    if (Starts(m, "order: ")) return TF("lg_order", DollOrderText(m.substr(7)).c_str());
    {   // (v141 beta 6) the ladder: "level: 5 EXPERT (the difficulty set[, the live level off])", "level: live, between 1 TRAINING
        // and 10 PRO", "level: tier VETERAN (7.02), was ELITE", "level: the elite moves on (level 6.02)"
        char t1[16] = "", t2[16] = "";
        float f = 0;
        if (sscanf(m.c_str(), "level: tier %15[A-Z] (%f), was %15[A-Z]", t1, &f, t2) == 3 && DollTierOf(t1) && DollTierOf(t2))
            return TF("lg_level_tier", DollTierText(DollTierOf(t1)).c_str(), Dec(f, 2).c_str(), DollTierText(DollTierOf(t2)).c_str());
        if (sscanf(m.c_str(), "level: the elite moves %15[a-z] (level %f", t1, &f) == 2) return TF(!strcmp(t1, "on") ? "lg_elite_on" : "lg_elite_off", Dec(f, 2).c_str());
        if (sscanf(m.c_str(), "level: live, between %d %15[A-Z] and %d", &a, t1, &b) == 3) return TF("lg_level_bounds", DollSkillText(a).c_str(), DollSkillText(b).c_str());
        if (sscanf(m.c_str(), "level: %d %15[A-Z] (the difficulty set", &a, t1) == 2 && DollTierOf(t1) && m.find(" (the difficulty set") != std::string::npos)
            return TF(m.find(", the live level off") != std::string::npos ? "lg_level_set_off" : "lg_level_set", DollSkillText(a).c_str());
    }
    {   // (v141 beta 7) tier 10's own play on and off: "DOLL CHEAT: on at level 10.00 (PRO, ...)", "DOLL CHEAT: off at level 9.42 (...)"
        char t1[8] = "";
        float f = 0;
        if (sscanf(m.c_str(), "DOLL CHEAT: %7[a-z] at level %f", t1, &f) == 2) return TF(!strcmp(t1, "on") ? "lg_cheat_on" : "lg_cheat_off", Dec(f, 2).c_str());
    }
    // (v141 beta 6) the live level judged: "DOLL ADAPT: old=6.40 new=6.80 target=7.50 base=5 enemy_strength=7.9 ally_strength=5.8
    // balance=+0.40 confidence=0.81 reason=enemy_team_outperforming (enemies 1 real, allies 0 real; <the stance>)"
    if (Starts(m, "DOLL ADAPT: ")) {
        float o = 0, n = 0, tg = 0, en = 0, al = 0, bal = 0, cf = 0;
        char why[48] = "";
        if (sscanf(m.c_str(), "DOLL ADAPT: old=%f new=%f target=%f base=%d enemy_strength=%f ally_strength=%f balance=%f confidence=%f reason=%47[a-z_]",
                   &o, &n, &tg, &a, &en, &al, &bal, &cf, why) == 9) {
            std::string text = TF("lg_adapt", Dec(n, 2).c_str(), DollTierText((int)std::lround(n)).c_str(), Dec(o, 2).c_str(), Dec(tg, 2).c_str(),
                                  DollSkillText(a).c_str(), Dec(en, 1).c_str(), Dec(al, 1).c_str(), Num((int)std::lround(cf * 100)).c_str(), DollWhyText(why).c_str());
            if (sscanf(After(m, " (enemies ").c_str(), "%d real, allies %d real", &b, &c) == 2) text += " " + TF("lg_adapt_real", Num(b).c_str(), Num(c).c_str());
            return text;
        }
    }
    // (v141 beta 6) the real players of its game ("real players: 1 -> 0", "real players: 0 -> 1: NAME"; the first count: "real players:
    // 1: NAME (session: 3 dolls, 1 dedicated host)"), a spectator for want of one, a rival
    if (Starts(m, "real players: ")) {
        if (sscanf(m.c_str(), "real players: %d -> %d", &a, &b) == 2) {
            const size_t c2 = m.find(": ", 14);
            if (c2 == std::string::npos) return TF("lg_real_change", Num(b).c_str(), Num(a).c_str());
            return TF("lg_real_change_who", Num(b).c_str(), Num(a).c_str(), m.substr(c2 + 2).c_str());
        }
        if (sscanf(m.c_str(), "real players: %d", &a) == 1) {
            const size_t c2 = m.find(": ", 14), s = m.rfind(" (session: ");
            if (c2 != std::string::npos && s != std::string::npos && s > c2) return TF("lg_real_now_who", Num(a).c_str(), m.substr(c2 + 2, s - c2 - 2).c_str());
            return TF("lg_real_now", Num(a).c_str());
        }
    }
    if (Starts(m, "doll: entering spectator")) return T("lg_spect_enter");
    if (Starts(m, "doll: real player joined")) return T("lg_spect_leave");
    if (Starts(m, "rival: ") && m.find(" dominates the dolls ") != std::string::npos) {
        const size_t p = m.find(" dominates the dolls ");
        return TF("lg_rival", m.substr(7, p - 7).c_str(), Dec(atof(m.c_str() + p + 21), 2).c_str());
    }
    {   // the live level: "level: 3.25 (the difficulty set 3; the stance 3.00, the VS RATINGs +0.25): <why>
        // (<numbers>)" (v132: "(the difficulty set 3)"), "level: 3 (the difficulty set)"
        float lv = 0, f1 = 0, f2 = 0;
        if (sscanf(m.c_str(), "level: %f (the difficulty set %d", &lv, &a) == 2 && m.find("(the difficulty set)") == std::string::npos) {
            const bool up = m.find("behind") != std::string::npos, down = m.find("is ahead") != std::string::npos;
            std::string text = TF(up ? "lg_level_up" : down ? "lg_level_down" : "lg_level_even", Dec(lv, 2).c_str(), Num(a).c_str());
            float avg = 0;
            if (sscanf(After(m, "(tickets ").c_str(), "%d against %d", &b, &c) == 2) text += " " + TF("lg_level_tickets", Num(b).c_str(), Num(c).c_str());
            else if (sscanf(After(m, "(the objective ").c_str(), "%f against %f", &f1, &f2) == 2)
                text += " " + TF("lg_level_objective", Dec(f1, 1).c_str(), Dec(f2, 1).c_str());
            if (sscanf(After(m, "its team's kills ").c_str(), "%d against %d, rounds %d against %d", &b, &c, &d, &a) == 4)
                text += " " + TF("lg_level_team", Num(b).c_str(), Num(c).c_str(), Num(d).c_str(), Num(a).c_str());
            else if (sscanf(After(m, "(its kills ").c_str(), "%d, the real players' best %d, their average %f", &b, &c, &avg) == 3)
                text += " " + TF("lg_level_dm", Num(b).c_str(), Num(c).c_str(), Dec(avg, 1).c_str());
            if (sscanf(After(m, "; VS RATING ").c_str(), "%d against %d a player", &b, &c) == 2)
                text += " " + TF("lg_level_vs", (b > 0 ? "+" + Num(b) : Num(b)).c_str(), (c > 0 ? "+" + Num(c) : Num(c)).c_str());
            else if (sscanf(After(m, "; the real players' VS RATING ").c_str(), "%d", &b) == 1) text += " " + TF("lg_level_vs_dm", Num(b).c_str());
            return text;
        }
        if (sscanf(m.c_str(), "level: %d (the difficulty set)", &a) == 1 && m.find(" (the difficulty set)") == 7 + Num(a).size())
            return TF("lg_level_set", DollSkillText(a).c_str());
    }
    // the headshot rule, the suppressors, "defend the target" (v133)
    if (Starts(m, "rules: headshots only")) return T("lg_rules_hs");
    if (Starts(m, "rules: the headshot rule is off")) return T("lg_rules_hs_off");
    if (Starts(m, "suppressor: on the ")) return TF("lg_suppressor_on", m.substr(19).c_str());
    if (Starts(m, "suppressor: not put on the ")) { std::string g = m.substr(27); const size_t p = g.find(" ("); if (p != std::string::npos) g.resize(p); return TF("lg_suppressor_failed", g.c_str()); }
    if (sscanf(m.c_str(), "objective: played harder %d s", &a) == 1) {
        std::string who = After(m, " s (");
        const size_t q = who.rfind("'s \"");
        if (q != std::string::npos) who.resize(q);
        return TF("lg_obj_focus", Num(a).c_str(), who.c_str());
    }
    if (Starts(m, "deathmatch: wearing ")) {
        const std::string rest = m.substr(20);
        const size_t to = rest.find(", to ");
        if (to != std::string::npos) return TF("lg_dm_change", UnitLabel(rest.substr(0, to)), UnitLabel(rest.substr(to + 5)));
        const size_t p = rest.find(" (");
        return TF("lg_dm_unit", UnitLabel(p == std::string::npos ? rest : rest.substr(0, p)));
    }
    if (Starts(m, "deathmatch: the doll's unit ")) return TF("lg_dm_drawn", UnitLabel(m.substr(28)));
    if (Starts(m, "leaving the game (ordered)") || Starts(m, "mgo1doll: left the game")) return T("lg_doll_left");
    if (Starts(m, "back in the lobby after a match") || Starts(m, "mgo1doll: back in the lobby")) return T("lg_doll_lobby");
    if (Starts(m, "lobby: the game is not listed") || Starts(m, "mgo1doll: its game is not in the list")) return T("lg_doll_unlisted");
    if (Starts(m, "match: nothing received from the host") || Starts(m, "mgo1doll: its match was lost")) return T("lg_doll_lost");
    if (Starts(m, "lobby: joining ")) return TF("lg_doll_joining", m.substr(15).c_str());
    if (Starts(m, "on (status in ")) {
        a = IntAfter(m, "difficulty ");
        std::string t = After(m, "team ");
        if (t.find(',') != std::string::npos) t.resize(t.find(','));
        return TF("lg_doll_on", (a > 0 ? DollSkillText(a) : Num(a)).c_str(), T(t == "red" ? "v_team_red" : t == "blue" ? "v_team_blue" : t == "spectator" ? "v_team_spect" : "v_team_auto"));
    }
    if (Starts(m, "map ") && m.find(": building its navigation") != std::string::npos) {
        const int map = MapOfPage(m.substr(4, 5));
        return map ? TF("lg_doll_map", MapName(map)) : std::string();
    }
    if (Starts(m, "network: upstream bandwidth ")) {
        a = atoi(m.c_str() + 28);
        return m.find("NOT the maximum") != std::string::npos ? TF("lg_bw_low", Num(a).c_str()) : TF("lg_bw", Num(a).c_str());
    }
    if (Starts(m, "CRASH: ")) return TF("lg_crash", m.substr(7).c_str());
    if (Starts(m, "stuck at ")) return T(m.find("the action button") != std::string::npos ? "lg_stuck_action" : "lg_stuck");
    // the launchers' (host/mgo1host.sh, host/mgo1doll.sh) and the manager's lines
    if (m.find(": the game crashed") != std::string::npos) return T("lg_end_crash");
    if ((a = IntAfter(m, "the game was ended by signal ")) >= 0) return TF(a == 9 ? "lg_end_killed" : "lg_end_signal", Num(a).c_str());
    if ((a = IntAfter(m, "the game ended with code ")) >= 0) return TF("lg_end_code", Num(a).c_str());
    if (Starts(m, "mgo1host: the game ended (a restart")) return T("lg_end_restart");
    if (Starts(m, "mgo1host: the game ended: its game was lost")) return T("lg_end_lost");
    if (Starts(m, "mgo1host: the game ended: the lobby connection was lost")) return T("lg_end_lobby");   // (v141 beta 6: exit 6)
    if (Starts(m, "mgo1host: the game ended: no game of its own")) return T("lg_end_nogame");
    if (Starts(m, "mgo1host: stopped") || Starts(m, "mgo1doll: stopped")) return T("lg_end_stopped");
    if (sscanf(m.c_str(), "mgo1doll: doll %d for bot %d", &a, &b) == 2) return TF("lg_doll_start", Num(a).c_str(), Num(b).c_str());
    if (Starts(m, "mgo1doll: DOLL_ACCOUNT_")) return T("lg_doll_no_account");
    if (Starts(m, "mgo1doll: ") && m.find("may only hold") != std::string::npos) return T("lg_bad_account");
    if (Starts(m, "mgo1doll: ") && m.find(" is missing") != std::string::npos) return TF("lg_no_disc", m.substr(10, m.find(" is missing") - 10).c_str());
    if (Whole(m, "mgo1doll: bot %d has no game name%n", &a)) return TF("lg_doll_no_game", Num(a).c_str());
    if (Starts(m, "mgo1doll: DOLL_BOT must be")) return T("lg_doll_bad_bot");
    if (sscanf(m.c_str(), "mgo1doll-manager: started (%d dolls", &a) == 1) return TF("lg_mgr_started", Num(a).c_str());
    if (sscanf(m.c_str(), "mgo1doll-manager: the server can run %d dolls at once besides its %d bots", &a, &b) == 2)
        return TF("lg_mgr_capacity", Num(a).c_str(), Num(b).c_str());
    if (sscanf(m.c_str(), "mgo1doll-manager: doll %d: started for bot %d's game (%d real players, %d seats", &a, &b, &c, &d) == 4)
        return TF("lg_mgr_start", Num(a).c_str(), Num(b).c_str(), Num(c).c_str(), Num(d).c_str());
    if (sscanf(m.c_str(), "mgo1doll-manager: doll %d: told to leave bot %d's game (%d real players, %d seats", &a, &b, &c, &d) == 4)
        return TF("lg_mgr_leave", Num(a).c_str(), Num(b).c_str(), Num(c).c_str(), Num(d).c_str());
    if (Whole(m, "mgo1doll-manager: doll %d: stopped (still there%n", &a)) return TF("lg_mgr_stop_late", Num(a).c_str());
    if (Whole(m, "mgo1doll-manager: doll %d: stopped (disabled%n", &a)) return TF("lg_mgr_stop_off", Num(a).c_str());
    if (Starts(m, "mgo1doll-manager: stopped by an error: ")) return TF("lg_mgr_error", m.substr(39).c_str());
    // v137: the dolls with no account (a bot's DOLL_COUNT, docs/PLAYABLE_DOLL.md 1), the seats, the identities
    if (sscanf(m.c_str(), "mgo1doll-manager: doll %d: added for bot %d as DOLL_%d", &a, &b, &c) == 3)
        return TF("lg_mgr_added", Num(a).c_str(), Num(b).c_str(), Num(c).c_str());
    if (sscanf(m.c_str(), "mgo1doll-manager: doll %d: removed (bot %d has DOLL_COUNT=%d", &a, &b, &c) == 3)
        return TF("lg_mgr_removed", Num(a).c_str(), Num(b).c_str(), Num(c).c_str());
    if (sscanf(m.c_str(), "mgo1doll-manager: doll %d: not in bot %d's game %d s after its start", &a, &b, &c) == 3)
        return TF("lg_mgr_join_late", Num(a).c_str(), Num(b).c_str(), Num(c).c_str());
    if (sscanf(m.c_str(), "mgo1doll-manager: doll %d: stopped (not in bot %d's game yet", &a, &b) == 2)
        return TF("lg_mgr_stop_waiting", Num(a).c_str(), Num(b).c_str());
    if (Whole(m, "mgo1doll-manager: doll %d: stopped (no settings file%n", &a)) return TF("lg_mgr_stop_orphan", Num(a).c_str());
    // (v141 beta 6) what the manager looked for and found before it starts a doll again (its diagnostic as written)
    if (Whole(m, "mgo1doll-manager: doll %d: join timeout:%n", &a))
        return TF("lg_mgr_join_timeout", Num(a).c_str(), After(m, ": join timeout: ").c_str());
    if (Starts(m, "mgo1doll-manager: dolls ") && m.find(" report the same name ") != std::string::npos) {
        const size_t at = m.find(" report the same name ");
        std::string who = m.substr(at + 22);
        if (who.find(':') != std::string::npos) who.resize(who.find(':'));
        return TF("lg_mgr_same_name", m.substr(24, at - 24).c_str(), who.c_str());
    }
    // the dolls' own lobby (host/mgo1-dollgate, one a bot)
    if (Whole(m, "mgo1-dollgate: DOLL_%d logged in%n", &a)) return TF("lg_gate_login", Num(a).c_str());
    if (sscanf(m.c_str(), "mgo1-dollgate: DOLL_%d joins bot %d's game", &a, &b) == 2) return TF("lg_gate_join", Num(a).c_str(), Num(b).c_str());
    if (sscanf(m.c_str(), "mgo1-dollgate: bot %d: the accountless dolls' lobby on 127.0.0.1:%d", &a, &b) == 2)
        return TF("lg_gate_start", Num(a).c_str(), Num(b).c_str());
    if (sscanf(m.c_str(), "mgo1-dollgate: bot %d: 127.0.0.1:%d or", &a, &b) == 2) return TF("lg_gate_taken", Num(a).c_str(), Num(b).c_str());
    if (Starts(m, "mgo1-dollgate: a log-in refused")) return T("lg_gate_refused");
    {
        char who[16] = "";
        if (sscanf(m.c_str(), "mgo1doll-manager: %15[a-z0-9]: its game wrote nothing for %d s", who, &a) == 2) {
            const std::string w = who;
            const std::string label = w.rfind("bot", 0) == 0 ? TF("lg_src_bot", w.substr(3).c_str()) : w.rfind("doll", 0) == 0 ? TF("lg_src_doll", w.substr(4).c_str()) : w;
            return TF("lg_mgr_hung", label.c_str(), Num(a).c_str());
        }
    }
    // v134: the rolls into an enemy, the emotes, a stun grenade's flash, the thermal goggles, the chaff, the smoke
    auto ends = [&](const char* e) { const size_t n = strlen(e); return m.size() >= n && m.compare(m.size() - n, n, e) == 0; };
    if (Starts(m, "roll: into ")) {
        std::string who = m.substr(11);
        const size_t p = who.find(" (");
        if (p != std::string::npos) who.resize(p);
        return TF(ends(": knocked down") ? "lg_roll_hit" : ends(": missed") ? "lg_roll_miss" : "lg_roll_off", who.c_str());
    }
    if (Starts(m, "emote: after a kill (")) {
        std::string who = m.substr(21);
        const size_t q = who.rfind("'s kill camera)");
        if (q != std::string::npos) who.resize(q);
        return TF("lg_emote_kill", who.c_str());
    }
    if (Starts(m, "emote: answering ")) {
        std::string who = m.substr(17);
        const size_t q = who.rfind("'s emote");
        if (q != std::string::npos) who.resize(q);
        return TF("lg_emote_answer", who.c_str());
    }
    if (Starts(m, "emote: greeting ")) return TF("lg_emote_greet", m.substr(16).c_str());
    if (Starts(m, "blinded by a flash")) return T("lg_blinded");
    if (Starts(m, "goggles: the thermal goggles ")) return TF(Starts(m.substr(29), "on") ? "lg_goggles_on" : "lg_goggles_off", Num(std::max(0, IntAfter(m, "(battery "))).c_str());
    if (Starts(m, "chaff: Snake's chaff grenade") || Starts(m, "chaff: a chaff grenade")) return T("lg_chaff");
    if (Starts(m, "grenade: a smoke between it and ")) {
        std::string who = m.substr(32);
        const size_t q = who.rfind(": yaw ");
        if (q != std::string::npos) who.resize(q);
        return TF("lg_smoke", who.c_str());
    }
    if (Starts(m, "codec: ")) return CodecLogText(m.substr(7));
    // v136: out of a dead end of the map, a corner held, the cardboard box
    if (Starts(m, "escape: out onto ")) {
        const size_t p = m.find(" in ");
        return TF("lg_escape_out", p == std::string::npos ? "?" : m.substr(p + 4, m.find(" s ", p) == std::string::npos ? 0 : m.find(" s ", p) - p - 4).c_str());
    }
    if (Starts(m, "escape: no way out found")) return T("lg_escape_none");
    if (Starts(m, "tactic: ")) {
        const std::string r = m.substr(8);
        size_t q = r.find(" came round the corner held");
        if (q != std::string::npos) return TF("lg_ambush", r.substr(0, q).c_str());
        if ((q = r.find(" passed by: out of the box")) != std::string::npos) return TF("lg_box_pass", r.substr(0, q).c_str());
        if ((q = r.find(" found it: out of the box")) != std::string::npos) return TF("lg_box_found", r.substr(0, q).c_str());
    }
    return std::string();
}

std::string App::LogSourceLabel(const std::string& name) const {
    if (name == "manager") return T("lg_src_manager");
    if (name.rfind("bot", 0) == 0) return TF("lg_src_bot", name.substr(3).c_str());
    if (name.rfind("doll", 0) == 0) {
        const int n = atoi(name.c_str() + 4);
        for (const DollInfo& d : dolls_)
            if (d.n == n && !d.name.empty()) return TF("lg_src_doll_named", Num(n).c_str(), d.name.c_str());
        return TF("lg_src_doll", Num(n).c_str());
    }
    return name;
}

static std::string DayLabel(const App::LogDayInfo& d) {
    if (d.crashes > 0) return TF("lg_day_crash", d.day.c_str(), Num(d.crashes).c_str(), Num(d.errors).c_str(), Num(d.warnings).c_str());
    if (d.errors > 0 || d.warnings > 0) return TF("lg_day_n", d.day.c_str(), Num(d.errors).c_str(), Num(d.warnings).c_str());
    return TF("lg_day_ok", d.day.c_str());
}

void App::LoadLogList() {
    if (demo_) return;
    logListLoading_ = true;
    remote_.Agent("logs list", "", [this](int rc, const Json& j, const std::string&) {
        logListLoading_ = false;
        logListLoaded_ = true;
        if (rc != 0 || !j["ok"].Truthy()) {                 // an agent before 2.4 has no daily logs: UPDATE THE HOST
            logListError_ = j["code"].Text() == "unknown_cmd" ? std::string() : ErrText(j);
            logSources_.clear();
            return;
        }
        logListError_.clear();
        const std::string was = logSrc_ < (int)logSources_.size() ? logSources_[logSrc_].name : std::string();
        const std::string wasDay = logSrc_ < (int)logSources_.size() && logDay_ < (int)logSources_[logSrc_].days.size() ? logSources_[logSrc_].days[logDay_].day : std::string();
        logSources_.clear();
        for (size_t i = 0; i < j["logs"].Size(); ++i) {
            const Json& s = j["logs"][i];
            LogSourceInfo src;
            src.name = s["source"].Text();
            for (size_t k = 0; k < s["days"].Size(); ++k) {
                const Json& d = s["days"][k];
                LogDayInfo di;
                di.day = d["day"].Text();
                di.lines = d["lines"].Int(); di.errors = d["E"].Int(); di.warnings = d["W"].Int();
                di.crashes = d["crashes"].Int(); di.starts = d["starts"].Int();
                src.days.push_back(di);
            }
            logSources_.push_back(src);
        }
        // the same source and day as before when they are still there, else the current bot's newest
        logSrc_ = 0; logDay_ = 0;
        const std::string want = was.empty() ? "bot" + Num(CurBot()) : was;
        for (size_t i = 0; i < logSources_.size(); ++i)
            if (logSources_[i].name == want) {
                logSrc_ = (int)i;
                for (size_t k = 0; k < logSources_[i].days.size(); ++k) if (logSources_[i].days[k].day == wasDay) logDay_ = (int)k;
            }
        if (!logSources_.empty()) LoadLogDay(false);
    });
}

// (v141 beta 2) the day's view opens on its last 2000 lines (what went wrong just now; v141 beta 1 showed its first ones, a
// doll's start); EARLIER LINES adds the 2000 before them, MORE the ones after (an agent before 2.6 has no --tail: the first)
void App::LoadLogDay(bool more) {
    if (demo_ || logSrc_ >= (int)logSources_.size() || logDay_ >= (int)logSources_[logSrc_].days.size()) return;
    const std::string src = logSources_[logSrc_].name, day = logSources_[logSrc_].days[logDay_].day;
    const int from = more ? std::max(0, dayNext_) : 0;
    std::string args = "logs get " + src + " " + day + (more ? " --lines 2000 --from " + Num(from) : std::string(" --tail 2000"));
    if (logLevel_ == 1) args += " --level EW";
    if (logLevel_ == 2) args += " --level E";
    if (!more) { dayLines_.clear(); dayNext_ = -1; dayTotal_ = 0; dayFrom_ = 0; dayViewOf_ = (size_t)-1; }
    logDayLoading_ = true;
    const int level = logLevel_;
    remote_.Agent(args, "", [this, src, day, level, more](int rc, const Json& j, const std::string&) {
        logDayLoading_ = false;
        const bool same = logSrc_ < (int)logSources_.size() && logSources_[logSrc_].name == src && logDay_ < (int)logSources_[logSrc_].days.size() &&
                          logSources_[logSrc_].days[logDay_].day == day && logLevel_ == level;
        if (!same) return;                                  // the view changed meanwhile
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        if (!more) { dayLines_.clear(); dayFrom_ = j["from"].Int(0); dayScrollEnd_ = true; }
        for (size_t i = 0; i < j["lines"].Size(); ++i) dayLines_.push_back(j["lines"][i].Text());
        dayNext_ = j["next"].Int(-1);
        dayTotal_ = j["total"].Int();
        dayViewOf_ = (size_t)-1;
    });
}
void App::LoadLogEarlier() {
    if (demo_ || dayFrom_ <= 0 || logSrc_ >= (int)logSources_.size() || logDay_ >= (int)logSources_[logSrc_].days.size()) return;
    const std::string src = logSources_[logSrc_].name, day = logSources_[logSrc_].days[logDay_].day;
    const int from = std::max(0, dayFrom_ - 2000), n = dayFrom_ - from;
    std::string args = "logs get " + src + " " + day + " --lines " + Num(n) + " --from " + Num(from);
    if (logLevel_ == 1) args += " --level EW";
    if (logLevel_ == 2) args += " --level E";
    logDayLoading_ = true;
    const int level = logLevel_;
    remote_.Agent(args, "", [this, src, day, level, from](int rc, const Json& j, const std::string&) {
        logDayLoading_ = false;
        const bool same = logSrc_ < (int)logSources_.size() && logSources_[logSrc_].name == src && logDay_ < (int)logSources_[logSrc_].days.size() &&
                          logSources_[logSrc_].days[logDay_].day == day && logLevel_ == level;
        if (!same) return;
        if (rc != 0 || !j["ok"].Truthy()) { Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        std::vector<std::string> add;
        for (size_t i = 0; i < j["lines"].Size(); ++i) add.push_back(j["lines"][i].Text());
        dayLines_.insert(dayLines_.begin(), add.begin(), add.end());
        dayFrom_ = from;
        dayViewOf_ = (size_t)-1;
    });
}

// a line of a daily log ("<ISO time UTC> <E|W|I> <text>"): its time on this PC's clock, its level, its text
static void DayLineView(const std::string& raw, App::DayView& v) {
    v = App::DayView();
    const size_t sp1 = raw.find(' ');
    const size_t sp2 = sp1 == std::string::npos ? sp1 : raw.find(' ', sp1 + 1);
    std::string msg = raw;
    int Y, Mo, D, h, mi, se;
    if (sp1 != std::string::npos && sscanf(raw.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se) == 6) {
        struct tm t{}; t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = mi; t.tm_sec = se;
        v.time = ReportClock((long long)plat::TimeGm(&t) * 1000, false);
        if (sp2 != std::string::npos && sp2 == sp1 + 2) { v.level = raw[sp1 + 1]; msg = raw.substr(sp2 + 1); }
        else msg = raw.substr(sp1 + 1);
    }
    v.text = LogMessageText(msg);
    if (v.text.empty()) { v.text = msg; v.raw = true; }
}

// the day in a file on this PC, to send to someone who helps: the lines in the program's language,
// then as the server wrote them (no log-in data: the server masks it), in <Documents>/MGO1 Host/Logs
// (v141 beta 2) the whole day (its last 100000 lines), not only the lines shown: fetched 5000 at a time, then written
void App::SaveLogDay() {
    if (demo_ || saveBusy_ || logSrc_ >= (int)logSources_.size() || logDay_ >= (int)logSources_[logSrc_].days.size()) return;
    const std::string src = logSources_[logSrc_].name, day = logSources_[logSrc_].days[logDay_].day;
    saveBusy_ = true; saveLines_.clear();
    const int level = logLevel_;
    const int from = dayTotal_ > 100000 ? dayTotal_ - 100000 : 0;
    Toast(T("i_working"));
    SaveLogChunk(src, day, level, from);
}
void App::SaveLogChunk(const std::string& src, const std::string& day, int level, int from) {
    std::string args = "logs get " + src + " " + day + " --lines 5000 --from " + Num(from);
    if (level == 1) args += " --level EW";
    if (level == 2) args += " --level E";
    remote_.Agent(args, "", [this, src, day, level](int rc, const Json& j, const std::string&) {
        if (rc != 0 || !j["ok"].Truthy()) { saveBusy_ = false; Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        for (size_t i = 0; i < j["lines"].Size(); ++i) saveLines_.push_back(j["lines"][i].Text());
        const int next = j["next"].Int(-1);
        if (next >= 0 && saveLines_.size() < 100000 && j["lines"].Size() > 0) { SaveLogChunk(src, day, level, next); return; }
        WriteSavedLog(src, day, level);
        saveBusy_ = false;
    });
}
void App::WriteSavedLog(const std::string& src, const std::string& day, int level) {
    const std::string dir = plat::Join(Config::DocumentsDir(), "MGO1 Host", "Logs");
    plat::MakeDirs(dir);
    std::string label = LogSourceLabel(src);
    for (char& ch : label) if (strchr("\\/:*?\"<>|", ch)) ch = '_';
    const std::string path = plat::Join(dir, label + " " + day + (level ? " (" + std::string(T(level == 1 ? "v_lg_problems" : "v_lg_errors")) + ")" : "") + ".log");
    FILE* f = plat::Open(path, "wb");
    if (!f) { Toast(TF("m_log_save_failed", HideProfile(path).c_str()), true); return; }
    const HostStatus st = Status();
    fputs("\xEF\xBB\xBF", f);
    fprintf(f, "MGO1 Host %s, agent %s, %s, %s (UTC), host %s\n\n", ProgramVersion(), st.agent.empty() ? "-" : st.agent.c_str(), LogSourceLabel(src).c_str(),
            day.c_str(), installedVersion_.empty() ? "-" : installedVersion_.c_str());
    for (const auto& l : saveLines_) {
        DayView v;
        DayLineView(l, v);
        fprintf(f, "%s %c %s\n", v.time.c_str(), v.level, v.text.c_str());
    }
    fprintf(f, "\n%s\n", T("lg_file_tech"));
    for (const auto& l : saveLines_) fprintf(f, "%s\n", l.c_str());
    saveLines_.clear(); saveLines_.shrink_to_fit();
    fclose(f);
    Toast(TF("m_log_saved", HideProfile(path).c_str()));
    plat::OpenFolder(dir);
}

void App::PageLogDaily() {
    if (!demo_ && !logListLoading_ && !logListLoaded_) LoadLogList();
    if (Button("dlref", T("b_refresh"), T("b_refresh"), T("h_dl_refresh")) && !demo_) LoadLogList();
    SameLineFit(ButtonWidth(T("b_log_save")));
    if (Button("dlsave", T("b_log_save"), T("b_log_save"), T("h_dl_save"), !dayLines_.empty() && !saveBusy_)) SaveLogDay();
    Gap(6);
    if (logSources_.empty()) {
        InfoRow(T("lg_of"), logListLoading_ ? T("i_working") : !logListError_.empty() ? logListError_.c_str() : T("lg_no_daily"), logListError_.empty() ? 0 : kRed);
        return;
    }
    logSrc_ = std::min(logSrc_, (int)logSources_.size() - 1);
    const LogSourceInfo& s = logSources_[logSrc_];
    if (int dv = ValueRow("dlsrc", T("lg_of"), LogSourceLabel(s.name).c_str(), T("lg_of"), T("h_lg_of"))) {
        logSrc_ = Cycle(logSrc_ + dv, 0, (int)logSources_.size() - 1);
        logDay_ = 0;
        LoadLogDay(false);
    }
    const LogSourceInfo& s2 = logSources_[logSrc_];
    logDay_ = s2.days.empty() ? 0 : std::min(logDay_, (int)s2.days.size() - 1);
    const std::string dayText = s2.days.empty() ? std::string("-") : DayLabel(s2.days[logDay_]);
    if (int dv = ValueRow("dlday", T("lg_day"), dayText.c_str(), T("lg_day"), T("h_lg_day"), s2.days.size() > 1)) {
        logDay_ = Cycle(logDay_ - dv, 0, (int)s2.days.size() - 1);          // right: the day before (newest first)
        LoadLogDay(false);
    }
    static const char* const levels[] = {"v_lg_all", "v_lg_problems", "v_lg_errors"};
    if (int dv = ValueRow("dllvl", T("lg_show"), T(levels[logLevel_]), T("lg_show"), T("h_lg_show"))) {
        logLevel_ = Cycle(logLevel_ + dv, 0, 2);
        LoadLogDay(false);
    }
    Gap(8);
    PanelHeader(dayTotal_ > (int)dayLines_.size() ? TF("p_dl_part", Num((int)dayLines_.size()).c_str(), Num(dayTotal_).c_str()).c_str()
                                                 : TF("p_dl", Num((int)dayLines_.size()).c_str()).c_str());
    if (dayViewOf_ != dayLines_.size() || dayViewLang_ != CurLang()) {
        dayView_.clear();
        for (const auto& l : dayLines_) { DayView v; DayLineView(l, v); dayView_.push_back(v); }
        dayViewOf_ = dayLines_.size(); dayViewLang_ = CurLang();
    }
    const bool moreBtn = dayNext_ >= 0;
    if (dayFrom_ > 0) {
        if (Button("dlearlier", T("b_lg_earlier"), T("b_lg_earlier"), T("h_lg_earlier"), !logDayLoading_)) LoadLogEarlier();
        Gap(4);
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C(0x2a2a24));
    ImGui::BeginChild("##daylog", ImVec2(0, std::max(200.0f * S, ImGui::GetContentRegionAvail().y - (moreBtn ? 50.f : 10.f) * S)), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, C(0xd0d0b8));
    if (dayView_.empty()) ImGui::TextWrapped("%s", T(logDayLoading_ ? "i_working" : "lg_day_empty"));
    const float tw = ImGui::CalcTextSize("00:00:00  ").x;
    for (const auto& v : dayView_) {
        const ImU32 col = v.level == 'E' ? C(0xf07060) : v.level == 'W' ? C(0xe8c050) : C(0xd0d0b8);
        const float x0 = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, C(0x9a9a86));
        ImGui::TextUnformatted(v.time.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(x0 + tw);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (v.raw) { ImGui::PushFont(F.mono); ImGui::TextUnformatted(v.text.c_str()); ImGui::PopFont(); }
        else { ImGui::PushTextWrapPos(0.0f); ImGui::TextUnformatted(v.text.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::PopStyleColor();
    }
    if (dayScrollEnd_ && !dayView_.empty()) { ImGui::SetScrollHereY(1.0f); dayScrollEnd_ = false; }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (moreBtn) {
        Gap(4);
        if (Button("dlmore", T("b_more"), T("b_more"), T("h_dl_more"), !logDayLoading_)) LoadLogDay(true);
    }
}

// the codec calls in a doll's log (MGO1Loader/doll_brain.cpp, docs/PLAYABLE_DOLL.md 7), after "codec: ":
//   heard "<phrase>" from <name>             sent "<phrase>"[: <why>]
//   following <name> for <n> s ("<phrase>")  holding for <name> for <n> s ("<phrase>")  going to <name> ("<phrase>")
//   end of <name>'s "<phrase>" (<why>)       <name>'s "<phrase>": not done (<why>)
//   sending "<phrase>" failed (<how>)        "<phrase>" not sent (<why>)
// in the program's language ("": as it is). The phrases by their log names (the game's texts' order)
namespace {
const char* const kPhraseLog[][2] = {
    {"go go go", "lg_ph_go"}, {"cover me", "lg_ph_cover"}, {"follow me", "lg_ph_follow"}, {"circle around", "lg_ph_circle"},
    {"hit, enemy unknown", "lg_ph_hit"}, {"grenade", "lg_ph_grenade"}, {"defend the target", "lg_ph_defend"}, {"pull out", "lg_ph_pull_out"},
    {"hold", "lg_ph_hold"}, {"stay together", "lg_ph_together"}, {"take cover", "lg_ph_take_cover"}, {"roger", "lg_ph_roger"},
    {"negative", "lg_ph_negative"}, {"enemy sighted", "lg_ph_enemy"}, {"all clear", "lg_ph_clear"}, {"come here", "lg_ph_come_here"},
    {"sorry", "lg_ph_sorry"}, {"nice job", "lg_ph_nice"}, {"don't die", "lg_ph_dont_die"}, {"counting on you", "lg_ph_counting"},
    {"huh", "lg_ph_huh"}};
// a phrase's text ("" when the name is not a phrase's)
std::string PhraseText(const std::string& name) {
    for (const auto& p : kPhraseLog) if (name == p[0]) return T(p[1]);
    return std::string();
}
// "<phrase>" at the start of s: its text, the rest after its closing quote
bool QuotedPhrase(const std::string& s, std::string& text, std::string& rest) {
    if (s.empty() || s[0] != '"') return false;
    const size_t q = s.find('"', 1);
    if (q == std::string::npos) return false;
    text = PhraseText(s.substr(1, q - 1));
    rest = s.substr(q + 1);
    return !text.empty();
}
// "<name>'s "<phrase>"<rest>": the name (it may hold anything), the phrase's text, the rest
bool NamePhrase(const std::string& s, std::string& name, std::string& text, std::string& rest) {
    for (size_t at = s.find("'s \""); at != std::string::npos; at = s.find("'s \"", at + 1))
        if (QuotedPhrase(s.substr(at + 3), text, rest)) { name = s.substr(0, at); return true; }
    return false;
}
// a reason in words of the doll's: in the program's language ("" when not known)
std::string CodecWhy(const std::string& w) {
    static const char* const known[][2] = {
        {"the caller died", "lg_ce_died"}, {"the caller left", "lg_ce_left"}, {"the caller changed sides", "lg_ce_sides"},
        {"its time is up", "lg_ce_time"}, {"the objective on it", "lg_cr_objective"}, {"another order", "lg_ce_order"},
        {"arrived", "lg_ce_arrived"}, {"it died", "lg_ce_self_died"}, {"dead", "lg_ce_self_died"}, {"the round is over", "lg_ce_round"},
        {"it leads now", "lg_ce_leads"}, {"another doll is closer", "lg_cd_closer"}, {"too far", "lg_cd_far"}, {"its level", "lg_cd_level"},
        {"carrying the objective", "lg_cr_objective"}, {"in a fight", "lg_cd_fight"}, {"defending (ordered)", "lg_cd_defend"},
        {"hiding (ordered)", "lg_cd_hide"}, {"on another order", "lg_ce_order"}, {"Snake", "lg_cd_snake"},
        {"the radio did not open", "lg_cf_open"}, {"the radio closed", "lg_cf_closed"}, {"its list did not open", "lg_cf_list"},
        {"the call did not go", "lg_cf_go"}, {"the radio not usable now", "lg_cn_unusable"}, {"no moment to make it", "lg_cn_moment"},
        {"a menu", "lg_cn_menu"}, {"a new map", "lg_cn_map"}, {"the radio suspended by the aim", "lg_cn_aim"}, {"hurt in a fight", "lg_cr_hurt"}, {"the fight is over", "lg_cr_fight_over"},
        {"the round starts", "lg_cr_round"}, {"test", "lg_cr_test"}, {"two dolls follow him already", "lg_cd_two"},
        {"spread out", "lg_ce_spread"}, {"stunned", "lg_cd_stunned"}, {"hit, the enemy not seen", "lg_cr_hit_unseen"},
        {"jammed by a chaff grenade", "lg_cn_jammed"}};
    for (const auto& k : known) if (w == k[0]) return T(k[1]);
    if (w == "go go go") return PhraseText(w);
    std::string name, text, rest;
    if (Starts(w, "answering ") && NamePhrase(w.substr(10), name, text, rest)) return TF("lg_cr_answer", name.c_str(), text.c_str());
    if (Starts(w, "enemy ") && w.size() > 11 && w.compare(w.size() - 5, 5, " seen") == 0)
        return TF("lg_cr_enemy", w.substr(6, w.size() - 11).c_str());
    if (w.size() > 7 && w.compare(w.size() - 7, 7, "'s kill") == 0) return TF("lg_cr_kill", w.substr(0, w.size() - 7).c_str());
    if (w.size() > 5 && w.compare(w.size() - 5, 5, " hurt") == 0) return TF("lg_cr_ally_hurt", w.substr(0, w.size() - 5).c_str());
    return std::string();
}
// "... (<why>)" at the end of s: the reason's text (as written when not known), s without it
bool TrailingWhy(const std::string& s, std::string& head, std::string& why) {
    if (s.size() < 3 || s.back() != ')') return false;
    const size_t open = s.rfind(" (");
    if (open == std::string::npos) return false;
    head = s.substr(0, open);
    const std::string w = s.substr(open + 2, s.size() - open - 3);
    why = CodecWhy(w);
    if (why.empty()) why = w;
    return true;
}
}  // namespace

std::string CodecLogText(const std::string& m) {
    std::string text, rest, name, head, why;
    if (QuotedPhrase(m, text, rest) && Starts(rest, " from ")) {           // (v134: a call heard under a chaff's jam)
        const size_t e = rest.rfind(" not heard (");
        if (e != std::string::npos && e > 6) return TF("lg_codec_jammed", text.c_str(), rest.substr(6, e - 6).c_str());
    }
    if (Starts(m, "heard ") && QuotedPhrase(m.substr(6), text, rest) && Starts(rest, " from "))
        return TF("lg_codec_heard", rest.substr(6).c_str(), text.c_str());
    if (Starts(m, "sent ") && QuotedPhrase(m.substr(5), text, rest)) {
        if (rest.empty()) return TF("lg_codec_sent", text.c_str());
        if (!Starts(rest, ": ")) return std::string();
        why = CodecWhy(rest.substr(2));
        return TF("lg_codec_sent_why", text.c_str(), why.empty() ? rest.substr(2).c_str() : why.c_str());
    }
    int n = 0;
    auto phraseIn = [&](const std::string& s, std::string& before) {    // "<before> ("<phrase>")"
        const size_t p = s.rfind(" (\"");
        if (p == std::string::npos || s.size() < p + 4 || s.compare(s.size() - 2, 2, "\")") != 0) return false;
        before = s.substr(0, p);
        text = PhraseText(s.substr(p + 3, s.size() - p - 5));
        return !text.empty();
    };
    if (Starts(m, "following ") && phraseIn(m.substr(10), head)) {
        const size_t f = head.rfind(" for ");
        if (f != std::string::npos && sscanf(head.c_str() + f + 5, "%d s", &n) == 1)
            return TF("lg_codec_follow", head.substr(0, f).c_str(), Num(n).c_str(), text.c_str());
    }
    if (Starts(m, "holding for ") && phraseIn(m.substr(12), head)) {
        const size_t f = head.rfind(" for ");
        if (f != std::string::npos && sscanf(head.c_str() + f + 5, "%d s", &n) == 1)
            return TF("lg_codec_hold", head.substr(0, f).c_str(), Num(n).c_str(), text.c_str());
    }
    if (Starts(m, "going to ") && phraseIn(m.substr(9), head)) return TF("lg_codec_goto", head.c_str(), text.c_str());
    if (Starts(m, "end of ") && TrailingWhy(m.substr(7), head, why) && NamePhrase(head, name, text, rest) && rest.empty())
        return TF("lg_codec_end", name.c_str(), text.c_str(), why.c_str());
    if (Starts(m, "sending ") && QuotedPhrase(m.substr(8), text, rest) && Starts(rest, " failed") && TrailingWhy(rest, head, why))
        return TF("lg_codec_failed", text.c_str(), why.c_str());
    if (QuotedPhrase(m, text, rest) && Starts(rest, " not sent") && TrailingWhy(rest, head, why))
        return TF("lg_codec_not_sent", text.c_str(), why.c_str());
    {
        std::string t2;
        if (TrailingWhy(m, head, why) && head.size() > 10 && head.compare(head.size() - 10, 10, ": not done") == 0 &&
            NamePhrase(head.substr(0, head.size() - 10), name, t2, rest) && rest.empty())
            return TF("lg_codec_declined", name.c_str(), t2.c_str(), why.c_str());
        if (TrailingWhy(m, head, why) && head.size() > 9 && head.compare(head.size() - 9, 9, ": nothing") == 0 &&
            NamePhrase(head.substr(0, head.size() - 9), name, t2, rest) && rest.empty())
            return TF("lg_codec_nothing", name.c_str(), t2.c_str(), why.c_str());
    }
    return std::string();
}

// ---- (v150-host beta 16) the crash reports (docs/CRASH_REPORTS.md) ------------------------------------------------
// What the server keeps when a bot's or a doll's game ends badly: mgo1host-agent crashes (the list, asked with no --bot:
// every bot's and every doll's) and crash <source>/<name> (a report's lines, 5000 a call). The LOG page's third view:
// the list, then one report in the daily log's view; the bots' rows say the reports of the last 24 hours.
namespace {
// "2026-11-10T00:50:15Z" (UTC) -> ms; 0: no time
long long IsoMs(const std::string& s) {
    int Y, Mo, D, h, mi, se;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se) != 6) return 0;
    struct tm t{}; t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = mi; t.tm_sec = se;
    return (long long)plat::TimeGm(&t) * 1000;
}
std::string UpperAscii(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
// what goes into the agent's command line: a source's and a report's names as the agent makes them
bool PlainName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') return false;
    return true;
}
// "bot2", "doll3" in the rows' capitals (BOT 2, DOLL 3)
std::string CrashSource(const std::string& src) {
    if (src.rfind("bot", 0) == 0) return TF("v_bot", atoi(src.c_str() + 3));
    if (src.rfind("doll", 0) == 0) return TF("v_doll", atoi(src.c_str() + 4));
    return UpperAscii(src);
}
}  // namespace

std::string App::CrashReasonText(const std::string& r) const {
    if (r == "crash70") return T("v_cr_crash");
    if (r == "signal9") return T("v_cr_killed");
    if (r.rfind("signal", 0) == 0) return TF("v_cr_signal", r.substr(6).c_str());
    if (r == "hung") return T("v_cr_hung");
    if (r == "lost") return T("v_cr_lost");
    if (r.rfind("exit", 0) == 0) return TF("v_cr_exit", r.substr(4).c_str());
    if (r == "wild") return T("v_cr_wild");
    return r;                                               // (a reason this program does not know yet: as it is)
}

void App::LoadCrashes() {
    if (demo_) return;
    crLoading_ = true;
    crAt_ = ImGui::GetTime();
    remote_.Agent("crashes", "", [this](int rc, const Json& j, const std::string&) {
        crLoading_ = false;
        crAt_ = ImGui::GetTime();
        crLoaded_ = true;
        if (rc != 0 || !j["ok"].Truthy()) {
            crOld_ = j["code"].Text() == "unknown_cmd";          // (an agent before 3.4: UPDATE THE HOST)
            crError_ = crOld_ ? std::string() : ErrText(j);
            crashes_.clear();
            return;
        }
        crOld_ = false; crError_.clear();
        crNow_ = (long long)j["now"].Dbl(0);
        std::vector<CrashInfo> v;
        for (size_t i = 0; i < j["crashes"].Size(); ++i) {
            const Json& x = j["crashes"][i];
            CrashInfo c;
            c.source = x["source"].Text(); c.name = x["name"].Text(); c.reason = x["reason"].Text();
            if (!PlainName(c.source) || !PlainName(c.name)) continue;
            c.t = IsoMs(x["time"].Text()); c.size = x["size"].Int();
            c.rule = x["rule"].Int(-1); c.map = x["map"].Int(-1);
            c.what = x["what"].Text(); c.game = x["game"].Text(); c.players = x["players"].Text(); c.wild = x["wild"].Text();
            v.push_back(c);
        }
        crashes_.swap(v);
    }, -1);
}

// a report's lines, 5000 a call until its end (a report is 2 MB at most)
void App::LoadCrashReport(int from) {
    if (demo_ || crOpen_.empty()) return;
    const std::string id = crOpen_;
    crLineLoading_ = true;
    remote_.Agent("crash " + id + " --lines 5000 --from " + Num(from), "", [this, id](int rc, const Json& j, const std::string&) {
        if (crOpen_ != id) return;                               // (another one, or the list, meanwhile)
        if (rc != 0 || !j["ok"].Truthy()) { crLineLoading_ = false; Toast(TF("m_cmd_failed", ErrText(j).c_str()), true); return; }
        for (size_t i = 0; i < j["lines"].Size(); ++i) crLines_.push_back(j["lines"][i].Text());
        crTotal_ = j["total"].Int();
        const int next = j["next"].Int(-1);
        if (next >= 0 && j["lines"].Size() > 0 && crLines_.size() < 100000) { LoadCrashReport(next); return; }
        crLineLoading_ = false;
    }, -1);
}

// a bot's reports of the last 24 hours (the server's clock at the list's answer): how many, the last one's time and reason
std::string App::CrashNoteText(int bot) const {
    if (demo_ && demoAgentOld_) return std::string();
    const std::string src = "bot" + Num(bot);
    int n = 0;
    const CrashInfo* last = nullptr;
    for (const auto& c : crashes_) {                         // (the newest first)
        if (c.source != src || c.t <= 0 || crNow_ - c.t >= 24LL * 3600 * 1000) continue;
        if (!last) last = &c;
        ++n;
    }
    if (!last) return std::string();
    return TF("v_cr_recent", n, ReportClock(last->t, false).c_str(), CrashReasonText(last->reason).c_str());
}

void App::CrashNote(int bot) {
    const std::string t = CrashNoteText(bot);
    if (t.empty()) return;
    Gap(3);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * S);
    ImGui::PushStyleColor(ImGuiCol_Text, kRed);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10 * S);
    ImGui::TextUnformatted(t.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    Gap(5);
}

// the report shown in a file on this PC, to send to someone who helps: its header and its lines in the program's
// language, then the report as the server wrote it, in <Documents>/MGO1 Host/Logs
void App::SaveCrashReport() {
    const size_t sl = crOpen_.find('/');
    if (demo_ || crLines_.empty() || sl == std::string::npos) return;
    const std::string src = crOpen_.substr(0, sl), name = crOpen_.substr(sl + 1);
    const std::string dir = plat::Join(Config::DocumentsDir(), "MGO1 Host", "Logs");
    plat::MakeDirs(dir);
    std::string label = LogSourceLabel(src);
    for (char& ch : label) if (strchr("\\/:*?\"<>|", ch)) ch = '_';
    const std::string path = plat::Join(dir, label + " " + name);     // ("Bot 2 2026-11-10_005015_hung.txt")
    FILE* f = plat::Open(path, "wb");
    if (!f) { Toast(TF("m_log_save_failed", HideProfile(path).c_str()), true); return; }
    const HostStatus st = Status();
    fputs("\xEF\xBB\xBF", f);
    fprintf(f, "MGO1 Host %s, agent %s, %s, %s, host %s\n\n", ProgramVersion(), st.agent.empty() ? "-" : st.agent.c_str(), label.c_str(), name.c_str(),
            installedVersion_.empty() ? "-" : installedVersion_.c_str());
    bool body = false;
    for (const auto& l : crLines_) {
        if (!body) { fprintf(f, "%s\n", l.c_str()); body = l == "----"; continue; }
        DayView v;
        DayLineView(l, v);
        fprintf(f, "%s %c %s\n", v.time.c_str(), v.level, v.text.c_str());
    }
    fprintf(f, "\n%s\n", T("lg_file_tech"));
    for (const auto& l : crLines_) fprintf(f, "%s\n", l.c_str());
    fclose(f);
    Toast(TF("m_log_saved", HideProfile(path).c_str()));
    plat::OpenFolder(dir);
}

void App::PageLogCrash() {
    if (!demo_ && !crLoading_ && !crLoaded_) LoadCrashes();
    if (crOld_) {                                            // an agent before 3.4: the update, right here
        UpdateBanner();
        PanelHeader(T("v_lg_crash"));
        ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", T("e_cr_update")); ImGui::PopStyleColor();
        return;
    }
    const CrashInfo* open = nullptr;
    if (!crOpen_.empty()) {
        for (const auto& c : crashes_) if (c.source + "/" + c.name == crOpen_) open = &c;
        if (!open) crOpen_.clear();                          // (the server no longer keeps it: the list)
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!open) {
        // ---- the list, the newest first: a report a row of two lines (its time on this PC's clock, its bot or doll, its
        // size; why its game ended), a click opens it
        if (Button("crref", T("b_refresh"), T("b_refresh"), "") && !demo_) LoadCrashes();
        Gap(6);
        PanelHeader(T("v_lg_crash"));
        if (!crError_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::TextWrapped("%s", TF("m_cmd_failed", crError_.c_str()).c_str()); ImGui::PopStyleColor();
            return;
        }
        if (crashes_.empty()) { Gap(4); ImGui::TextWrapped("%s", T(crLoaded_ || demo_ ? "lg_cr_none" : "i_working")); return; }
        const float rw = ImGui::GetContentRegionAvail().x, rh = 50 * S;
        const float whenW = SpacedWidth(F.row, F.row->FontSize, "0000-00-00 00:00:00", RowSpacing()) + 18 * S;
        std::string pick;
        int n = 0;
        for (const auto& c : crashes_) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::PushID(n++);
            const bool click = ImGui::InvisibleButton("##crrow", ImVec2(rw, rh));
            const bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), hov ? kRowHi : kRow);
            dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
            const float l1 = a.y + 6 * S, l2 = a.y + rh - 7 * S - F.body->FontSize;
            const std::string when = ReportClock(c.t, true), who = CrashSource(c.source), size = TF("v_kb", (c.size + 1023) / 1024);
            const float sizeW = FitWidth(F.body, size.c_str(), 120 * S, 0);
            FitText(dl, F.body, ImVec2(a.x + rw - 10 * S - sizeW, l1 + (F.row->FontSize - F.body->FontSize) * 0.5f), sizeW + 1, kText, size.c_str(), 0);
            FitText(dl, F.row, ImVec2(a.x + 10 * S, l1), whenW - 8 * S, kText, when.c_str(), RowSpacing());
            FitText(dl, F.row, ImVec2(a.x + 10 * S + whenW, l1), rw - whenW - sizeW - 40 * S, kText, who.c_str(), RowSpacing());
            FitText(dl, F.body, ImVec2(a.x + 10 * S, l2), rw - 20 * S, kRed, CrashReasonText(c.reason).c_str(), 0);
            if (hov) {
                // the help: what ended in the server's words, its game and round, the first access outside the memory
                std::string h = c.what;
                std::string g = c.game;
                if (c.rule >= 0 && c.map >= 0) g += (g.empty() ? "" : "   ") + RuleName(c.rule) + ", " + MapName(c.map);
                if (!c.players.empty()) g += (g.empty() ? "" : "   ") + c.players;
                if (!g.empty()) h += (h.empty() ? "" : "\n") + g;
                if (!c.wild.empty()) h += (h.empty() ? "" : "\n") + c.wild.substr(0, 200);
                SetHelp(who + "   " + when, h);
            }
            if (click) { ClickSound("crrow"); pick = c.source + "/" + c.name; }
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + rh));
            ImGui::Dummy(ImVec2(rw, 0));
        }
        if (!pick.empty() && !demo_) {
            crOpen_ = pick; crLines_.clear(); crTotal_ = 0; crViewOf_ = (size_t)-1; crFind_[0] = 0;
            LoadCrashReport(0);
        }
        return;
    }
    // ---- one report: back to the list, SAVE (a file on this PC), COPY (as the server wrote it); who and when, why;
    // SEARCH; its lines as a daily log's (the header as written, then each line's time on this PC's clock, its level's
    // colour and its text in the program's language)
    const bool ready = !crLines_.empty() && !crLineLoading_;
    if (Button("crback", T("b_back"), T("b_back"), "")) { crOpen_.clear(); return; }
    SameLineFit(ButtonWidth(T("b_log_save")));
    if (Button("crsave", T("b_log_save"), T("b_log_save"), "", ready)) SaveCrashReport();
    SameLineFit(ButtonWidth(T("b_copy")));
    if (Button("crcopy", T("b_copy"), T("b_copy"), "", ready) && !demo_) {
        std::string all;
        for (const auto& l : crLines_) { all += l; all += '\n'; }
        ImGui::SetClipboardText(all.c_str());
        Toast(T("m_copied"));
    }
    Gap(6);
    PanelHeader((CrashSource(open->source) + "   " + ReportClock(open->t, true)).c_str());
    Gap(4);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10 * S);
    ImGui::PushStyleColor(ImGuiCol_Text, kRed);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10 * S);
    ImGui::TextUnformatted(CrashReasonText(open->reason).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    Gap(4);
    if (!open->game.empty()) InfoRow(T("i_game"), open->game.c_str());
    if (open->rule >= 0 && open->map >= 0) InfoRow(T("i_round"), (RuleName(open->rule) + ", " + MapName(open->map)).c_str());
    TextRow("crfind", T("lg_cr_search"), crFind_, sizeof crFind_, T("lg_cr_search"), "", false, 0.55f, 0);
    Gap(8);
    if (crViewOf_ != crLines_.size() || crViewLang_ != CurLang()) {
        crView_.clear();
        bool body = false;
        for (const auto& l : crLines_) {
            DayView v;
            if (body) DayLineView(l, v);
            else { v.text = l; v.raw = true; body = l == "----"; }
            crView_.push_back(v);
        }
        crViewOf_ = crLines_.size(); crViewLang_ = CurLang();
    }
    const std::string find = UpperAscii(crFind_);
    std::vector<size_t> shown;
    for (size_t i = 0; i < crView_.size(); ++i)
        if (find.empty() || UpperAscii(crLines_[i]).find(find) != std::string::npos || UpperAscii(crView_[i].text).find(find) != std::string::npos) shown.push_back(i);
    PanelHeader(shown.size() < crLines_.size() ? TF("p_dl_part", Num((int)shown.size()).c_str(), Num((int)crLines_.size()).c_str()).c_str()
                                               : TF("p_dl", Num((int)crLines_.size()).c_str()).c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C(0x2a2a24));
    ImGui::BeginChild("##crlog", ImVec2(0, std::max(200.0f * S, ImGui::GetContentRegionAvail().y - 10.f * S)), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_Text, C(0xd0d0b8));
    if (crView_.empty()) ImGui::TextWrapped("%s", T(crLineLoading_ ? "i_working" : "lg_day_empty"));
    const float tw = ImGui::CalcTextSize("00:00:00  ").x;
    for (size_t i : shown) {
        const DayView& v = crView_[i];
        if (v.time.empty()) {                                // the header's lines, and a line with no time: as written
            ImGui::PushFont(F.mono); ImGui::TextUnformatted(v.text.c_str()); ImGui::PopFont();
            continue;
        }
        const ImU32 col = v.level == 'E' ? C(0xf07060) : v.level == 'W' ? C(0xe8c050) : C(0xd0d0b8);
        const float x0 = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, C(0x9a9a86));
        ImGui::TextUnformatted(v.time.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(x0 + tw);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        if (v.raw) { ImGui::PushFont(F.mono); ImGui::TextUnformatted(v.text.c_str()); ImGui::PopFont(); }
        else { ImGui::PushTextWrapPos(0.0f); ImGui::TextUnformatted(v.text.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// the self-test's reports: every reason, bots and dolls, the longest numbers; two bots with reports of the last 24 hours
// (their rows and the dashboard say so); bot 2's hung report open with its lines (docs/CRASH_REPORTS.md's example)
void App::DemoCrashes() {
    const long long now = IsoMs("2026-11-10T12:00:00Z");
    crNow_ = now;
    crLoaded_ = true;
    auto add = [this](const char* src, const char* name, const char* reason, const char* time, int size, const char* what) {
        CrashInfo c;
        c.source = src; c.name = name; c.reason = reason; c.t = IsoMs(time); c.size = size; c.what = what;
        c.game = "WWWWWWWWWWWWWWW"; c.rule = 3; c.map = 2; c.players = "8 (5 real, 2 dolls, 1 host)";
        crashes_.push_back(c);
    };
    add("bot1", "2026-11-10_093012_crash70.txt", "crash70", "2026-11-10T09:30:12Z", 2097152, "the game crashed (exit code 70): its CRASH lines tell where");
    add("bot2", "2026-11-10_005015_hung.txt", "hung", "2026-11-10T00:50:15Z", 412345, "its game wrote nothing for 183 s (hung): restarted by the doll manager");
    add("bot2", "2026-11-09_230501_signal9.txt", "signal9", "2026-11-09T23:05:01Z", 180000, "the game was ended by signal 9 (killed: out of memory, or forced)");
    add("doll16", "2026-11-09_181500_lost.txt", "lost", "2026-11-09T18:15:00Z", 9000, "its match was lost (exit code 5)");
    add("bot10", "2026-11-08_120000_exit255.txt", "exit255", "2026-11-08T12:00:00Z", 1024, "the game ended with code 255");
    add("bot10", "2026-11-07_120000_signal15.txt", "signal15", "2026-11-07T12:00:00Z", 2048, "the game was ended by signal 15");
    add("doll3", "2026-10-31_235959_wild.txt", "wild", "2026-10-31T23:59:59Z", 65536, "the game ended with no failure; its code had gone outside its memory before");
    crashes_[1].wild = "2026-11-10T00:32:44.957Z ps2: hardware read outside the emulated registers at 1476ed50 (00000000); calls before: 004755d8<0040ba9c";
    static const char* const lines[] = {
        "MGO1 crash report (host/mgo1log, docs/CRASH_REPORTS.md)", "source: bot2", "reason: hung",
        "what: its game wrote nothing for 183 s (hung): restarted by the doll manager", "when: 2026-11-10T00:50:15.017Z",
        "version: build 1.00 0123456789ab", "status: written 183 s before the end", "state: hosting", "frame: 2573208", "game: WWWWWWWWWWWWWWW",
        "round: 2 of 12 (rule 3, map 2)", "players: 5 (1 real, 3 dolls, 1 host)", "player: P1 host",
        "player: P2 virtual doll 1, team red, 12 kills, 3 deaths", "player: P5 real \"WWWWWWWWWWWWWWW\", team red, 4 kills, 2 deaths, ping 80 ms",
        "vdoll: 2 in_game, player 3, team blue, spawned, 16 deaths", "pace: 44.28 VBlanks a second (59.94 due), 66 % of a core",
        "wild: 2026-11-10T00:32:44.957Z ps2: hardware read outside the emulated registers at 1476ed50 (00000000); calls before: 004755d8<0040ba9c",
        "lines: 4000, the last ones before the end, from 2026-11-10T00:31:55.507Z", "----",
        "2026-11-10T00:32:37.011Z I ps2 f2572817: vdoll 2: dead, by player 2 (16 deaths)",
        "2026-11-10T00:32:44.954Z I ps2 f2573208: vdoll 2: respawns at -2000 464 -31500",
        "2026-11-10T00:32:44.957Z W ps2: hardware read outside the emulated registers at 1476ed50 (00000000); calls before: 004755d8<0040ba9c 00476a28<00475630",
        "2026-11-10T00:32:55.000Z W mgo1log: 48211 more lines like \"ps2: hardware read outside the emulated registers at <n> (<n>)\" in 10 s, the first at "
        "00:32:45.418 (10886a50 00000000), the last at 00:32:55.417 (108c5d40 00000000) (not written one by one)",
        "2026-11-10T00:50:14.001Z E mgo1doll-manager: bot2: its game wrote nothing for 183 s (hung): restarted",
        "2026-11-10T00:50:15.001Z E CRASH: exception c0000005 at mgo1host.exe+12a4f0 (reading 0000000000000000); the PS2 code: last call 001f55c0 from 0017d610",
        "2026-11-10T00:50:15.017Z W mgo1host: the game ended: its game was lost (no game for 30 s); a new game"};
    for (const char* l : lines) crLines_.push_back(l);
    crTotal_ = (int)crLines_.size();
}
