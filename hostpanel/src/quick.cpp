// (build 1.00) QUICK SETUP: the server's address and password, the bot's SaveMGO account, the disc's folder, and GO. GO runs the
// program's own steps in order, each shown with its state (waiting, running, done, failed) and the whole progress: the connection,
// the server's key (TRUST / CANCEL: the one question, as on the SERVER page), the host installed (built on the server when the
// package holds no host program: its stage, percent and time, 20 to 60 minutes), bot 1's account, its game settings, its virtual
// dolls, its start, then the dashboard once it is online. A step that fails says why and what to do; GO again starts at that
// step. Each step is what the other pages do (Connect, StartInstall, AccountSet, SaveSettings, LoadVdolls and VdollsSave,
// AgentCmd), never a second way of doing it.
#include "app.h"
#include "i18n.h"
#include "platform.h"
#include "sound.h"
#include "ui.h"
#include "version.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace ui;

namespace {
const char* const kStepKeys[QS_COUNT] = {"qs_s_connect", "qs_s_signin", "qs_s_install", "qs_s_account", "qs_s_game", "qs_s_vdolls", "qs_s_start",
                                         "qs_s_online"};
const int kBot = 1;                                   // the bot GO sets up (install.sh makes bot 1)
// the virtual dolls GO gives it (docs/VIRTUAL_DOLL.md 6, 17, 19): as many as its game's seats less the host and the seat always
// kept for a real player (7 in a game of 8), AS A PLAYER (a player's pad: fair to real players), level 5 where they start, the
// live level from 1 to 10 (each one's level follows the real players' strength)
const int kVdolls = 7, kVdollLevel = 5, kAdaptMin = 1, kAdaptMax = 10;
const double kOnlineWait = 300;                       // s: a bot not online 5 minutes after its start: failed (its reason said)
const double kAgainWait = 90;                         // s: the link back after the installation
const double kStatusWait = 30;                        // s: the server's first state (the agent's version known)

// the game settings GO gives bot 1: the program's (GameSettings: SAVEMGO HOST, every rule's times, 8 players, friendly fire off,
// the idle kick, voice chat), a rotation of team deathmatch and deathmatch on four maps, a comment that says who fills the seats
GameSettings QuickGame() {
    GameSettings g;
    g.rotation = {{1, 3}, {0, 2}, {1, 8}, {0, 12}};
    g.maxJoin = 8;
    g.comment = "Virtual dolls fill the empty seats";
    return g;
}

std::string Upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
bool NameChars(const char* s) { for (const char* p = s; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') return false; return true; }
bool LoginErr(const std::string& r) { return r.rfind("log-in error: ", 0) == 0; }
std::string Trimmed(const char* s) {
    std::string t = s;
    while (!t.empty() && t.back() == ' ') t.pop_back();
    size_t a = 0;
    while (a < t.size() && t[a] == ' ') ++a;
    return t.substr(a);
}
std::string Elapsed(long long s) {
    if (s < 0) s = 0;
    if (s >= 3600) return TF("t_hm", (int)(s / 3600), (int)(s % 3600 / 60));
    if (s >= 60) return TF("t_ms", (int)(s / 60), (int)(s % 60));
    return TF("t_s", (int)s);
}
unsigned long long Fnv(const std::string& s) {        // (the fields GO was given, kept as a number: no password copy)
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}
std::string Text(const std::string& note) { return note.empty() || note[0] != '@' ? note : TrMsg(note); }
// a wrapped paragraph, noted for the layout check as one block (the showcase's pictures leave out one their edge would cut)
void Para(const std::string& text, ImU32 col = 0) {
    if (col) ImGui::PushStyleColor(ImGuiCol_Text, col);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::TextWrapped("%s", text.c_str());
    const ImVec2 b(a.x + w, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y);
    if (col) ImGui::PopStyleColor();
    Note(ImGui::GetWindowDrawList(), a, b, text.c_str(), 0, true);
}
// a header and its paragraph, noted as one block too (a picture's edge leaves out both, never the header alone)
void Section(const char* header, const std::string& text) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    PanelHeader(header);
    Para(text);
    const ImVec2 b(a.x + w, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y);
    Note(ImGui::GetWindowDrawList(), a, b, header, 0, true);
}

// the self-test: the fields as they were before a QUICK SETUP page changed them (the SERVER page shows them too)
struct DemoFields { bool saved = false; std::string host, pass, disc; };
DemoFields gDemoFields;
}  // namespace

void App::QuickAccount(const char* id, const char* pw) {
    snprintf(qsId_, sizeof qsId_, "%s", Upper(id).c_str());
    snprintf(qsPw_, sizeof qsPw_, "%s", Upper(pw).c_str());
}

void App::QuickFill(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& disc) {
    snprintf(fHost_, sizeof fHost_, "%s", host.c_str());
    snprintf(fPort_, sizeof fPort_, "%d", port);
    snprintf(fUser_, sizeof fUser_, "%s", user.c_str());
    snprintf(fPass_, sizeof fPass_, "%s", password.c_str());
    fUseKey_ = false;
    snprintf(disc_, sizeof disc_, "%s", disc.c_str());
}

std::string App::QuickServerKey() const {
    const std::string s = Trimmed(fHost_) + "|" + fPort_ + "|" + fUser_ + "|" + (fUseKey_ ? "k|" + std::string(fKey_) + "|" + fKeyPass_ : "p|" + std::string(fPass_));
    return std::to_string(Fnv(s));
}

bool App::QuickDiscOk() {
    if (demo_) return qsDiscOk_;
    if (qsDiscFor_ != disc_) {
        qsDiscFor_ = disc_;
        qsDiscRoot_.clear();
        qsDiscOk_ = disc_[0] && FindDisc(disc_, qsDiscRoot_);
    }
    return qsDiscOk_;
}

// ---- the run -------------------------------------------------------------------------------------------------------------
void App::QuickGo() {
    if (demo_ || qs_.running) return;
    const std::string server = QuickServerKey(), account = std::to_string(Fnv(Upper(qsId_) + "|" + Upper(qsPw_))), disc = disc_;
    // GO again after a failure: at the failed step (where its reason says), sooner when a field it depends on changed since
    int from = 0;
    if (qs_.ever && !qs_.done && qs_.failed >= 0 && server == qs_.server) {
        from = qs_.resumeAt;
        if (account != qs_.account) from = std::min(from, (int)QS_ACCOUNT);
        if (disc != qs_.disc && qs_.state[QS_INSTALL] != QS_DONE) from = std::min(from, (int)QS_INSTALL);
    }
    qs_.forceInstall = from == QS_INSTALL && qs_.failed == QS_INSTALL;
    ++qs_.gen;
    qs_.ever = qs_.running = true;
    qs_.done = false;
    qs_.failed = -1;
    qs_.hint.clear();
    qs_.doneAt = -1;
    qs_.server = server; qs_.account = account; qs_.disc = disc;
    for (int i = from; i < QS_COUNT; ++i) { qs_.state[i] = QS_WAIT; qs_.note[i].clear(); }
    // the steps after the connection need it, to this server: connected again first, then on where it stopped
    const Link l = LinkState();
    const SshConfig& c = remote_.config();
    const bool up = RemoteRunning() && (l == Link::Connected || l == Link::NoAgent) && c.host == Trimmed(fHost_) && c.user == fUser_ &&
                    std::to_string(c.port) == fPort_;
    qs_.jumpTo = -1;
    if (from >= QS_SIGNIN && !up) {
        qs_.jumpTo = from > QS_SIGNIN ? from : -1;
        from = QS_CONNECT;
        for (int i = QS_CONNECT; i <= QS_SIGNIN; ++i) { qs_.state[i] = QS_WAIT; qs_.note[i].clear(); }
    }
    // the server's name kept as it was (a known one), none for a new one
    const std::string host = Trimmed(fHost_);
    snprintf(fHost_, sizeof fHost_, "%s", host.c_str());
    const int known = cfg.FindServer(host, atoi(fPort_) > 0 && atoi(fPort_) < 65536 ? atoi(fPort_) : 22);
    snprintf(fLabel_, sizeof fLabel_, "%s", known >= 0 ? cfg.servers[known].label.c_str() : "");
    cfg.discDir = disc_;
    cfg.Save();
    sound::Init(cfg.discDir);
    QuickStep(from);
}

void App::QuickCancel() {
    if (!qs_.running || qs_.step < 0) return;
    if (qs_.step == QS_INSTALL && InstallState().running && !demo_) remote_.CancelInstall();
    QuickFail(qs_.step, "@qs_f_cancelled", "qs_h_cancelled");
}

void App::QuickDone(int step, const std::string& note) {
    if (!qs_.running || qs_.step != step) return;
    qs_.state[step] = QS_DONE;
    qs_.note[step] = note;
    int next = step + 1;
    if (step == QS_SIGNIN && qs_.jumpTo > QS_SIGNIN) { next = qs_.jumpTo; qs_.jumpTo = -1; }
    QuickStep(next);
}

void App::QuickFail(int step, const std::string& reason, const char* hint, int resumeAt) {
    if (step < 0 || step >= QS_COUNT) return;
    ++qs_.gen;                                        // (a later answer of this run changes nothing)
    qs_.state[step] = QS_FAIL;
    qs_.note[step] = reason;
    qs_.hint = hint ? hint : "";
    qs_.failed = step;
    qs_.resumeAt = resumeAt >= 0 ? resumeAt : step;
    qs_.running = false;
    qs_.step = -1;
}

void App::QuickStep(int step) {
    if (step >= QS_COUNT) {                           // every step done: the dashboard 2 s later (QuickTick)
        qs_.running = false;
        qs_.done = true;
        qs_.step = -1;
        qs_.doneAt = ImGui::GetTime();
        return;
    }
    qs_.step = step;
    qs_.state[step] = QS_RUN;
    qs_.note[step].clear();
    qs_.sub = 0;
    qs_.stepAt = ImGui::GetTime();
    const unsigned gen = qs_.gen;
    auto answer = [this, gen, step](const char* hint, std::string doneNote) {
        return [this, gen, step, hint, doneNote](bool ok, const std::string& err) {
            if (gen != qs_.gen) return;
            if (ok) QuickDone(step, doneNote);
            else QuickFail(step, err, hint);
        };
    };
    switch (step) {
    case QS_CONNECT:                                  // the SERVER page's CONNECT: the server kept in the list, the link started
        qs_.note[step] = "@qs_n_connecting|" + std::string(fHost_);
        Connect();
        break;
    case QS_ACCOUNT:                                  // HOST ACCOUNT's SAVE for bot 1, the account kept on this PC (encrypted)
        AccountSet(kBot, Upper(qsId_), Upper(qsPw_), true, answer("qs_h_account", "@qs_n_account|" + Upper(qsId_)));
        break;
    case QS_GAME: {                                   // GAME SETTINGS' SAVE for bot 1 with the defaults above
        SelectBot(kBot);
        const GameSettings g = QuickGame();
        FillGame(g);
        SaveSettings(false, true, answer("qs_h_again", "@qs_n_game|" + g.name + "|" + std::to_string((int)g.rotation.size()) + "|" +
                                                           std::to_string(g.maxJoin)));
        break;
    }
    case QS_VDOLLS:                                   // VIRTUAL DOLLS: bot 1's settings read, the form set, SAVE
        LoadVdolls([this, gen](bool ok) {
            if (gen != qs_.gen) return;
            if (!ok) { QuickFail(QS_VDOLLS, vdOld_ ? std::string(T("e_vd_update")) : vdError_, "qs_h_again"); return; }
            VdollBot b;
            b.bot = kBot;
            for (const auto& x : vdolls_) if (x.bot == kBot) b = x;
            VdollsFill(b);
            vfCount_ = b.most >= 0 ? std::min(kVdolls, b.most) : kVdolls;
            for (int i = 0; i < 15; ++i) { vfLevel_[i] = kVdollLevel; vfTeam_[i] = 0; vfWeapon_[i] = 0; }
            vfControls_ = 1;
            vfAdapt_ = vdAdapt_ ? 1 : 0; vfAdaptMin_ = kAdaptMin; vfAdaptMax_ = kAdaptMax;
            const std::string note = "@qs_n_vdolls|" + std::to_string(vfCount_) + "|" + std::to_string(kVdollLevel) + "|" + std::to_string(kAdaptMin) + "|" +
                                     std::to_string(kAdaptMax);
            VdollsSave(false, [this, gen, note](bool ok2, const std::string& err) {
                if (gen != qs_.gen) return;
                if (ok2) QuickDone(QS_VDOLLS, note); else QuickFail(QS_VDOLLS, err, "qs_h_again");
            });
        });
        break;
    case QS_START: {                                  // the dashboard's START HOST (RESTART GAME when it runs: the new settings now)
        const HostStatus st = Status();
        std::string svc = st.bot == kBot ? st.service : std::string();
        for (const auto& b : st.bots) if (b.bot == kBot) svc = b.service;
        const bool active = svc == "active" || svc == "activating";
        qs_.restarted = active;
        qs_.sawDown = false;
        AgentCmd(active ? "restart" : "start", "", "", answer("qs_h_again", active ? "@qs_n_restarted" : "@qs_n_started"), kBot);
        break;
    }
    default:                                          // QS_SIGNIN, QS_INSTALL, QS_ONLINE: QuickTick follows the server
        break;
    }
}

// every frame (Frame), whatever page is shown: the step running follows the link, the installation, the bot's state
void App::QuickTick() {
    if (demo_) return;
    if (qs_.done && qs_.doneAt >= 0 && ImGui::GetTime() - qs_.doneAt > 2) {   // online: the dashboard, once
        qs_.doneAt = -1;
        if (page_ == PG_QUICK) page_ = PG_DASHBOARD;
    }
    if (!qs_.running || qs_.step < 0) return;
    std::string msg;
    const Link l = LinkState(&msg);
    const double t = ImGui::GetTime() - qs_.stepAt;
    const int step = qs_.step;
    auto linkFail = [&](int s) {
        if (l == Link::Unreachable) QuickFail(s, "@qs_f_unreachable|" + std::string(fHost_) + "|" + fPort_, "qs_h_connect");
        else if (l == Link::Error) QuickFail(s, "@qs_f_error|" + msg, "qs_h_connect");
        else if (l == Link::AuthFailed) QuickFail(s, "@qs_f_auth", "qs_h_signin");
        else return false;
        return true;
    };
    switch (step) {
    case QS_CONNECT:
        if (l == Link::NeedTrust || l == Link::KeyChanged || l == Link::Connected || l == Link::NoAgent || l == Link::AuthFailed)
            QuickDone(QS_CONNECT, "@qs_n_reached");   // (the server answered: its key next, then the sign-in)
        else if (l == Link::Unreachable || l == Link::Error) linkFail(QS_CONNECT);
        else if (l == Link::Idle && t > 1) QuickFail(QS_CONNECT, "@qs_f_stopped", "qs_h_connect");
        break;
    case QS_SIGNIN:
        if (l == Link::NeedTrust) qs_.note[step] = "@qs_n_trust";
        else if (l == Link::KeyChanged) qs_.note[step] = "@qs_n_keychanged";
        else if (l == Link::Connecting) qs_.note[step] = "@qs_n_signing";
        else if (l == Link::Connected || l == Link::NoAgent) QuickDone(QS_SIGNIN, "@qs_n_signedin|" + std::string(fUser_));
        else if (l == Link::Idle) QuickFail(QS_SIGNIN, "@qs_f_notrust", "qs_h_notrust");   // (CANCEL in the key's box)
        else linkFail(QS_SIGNIN);
        break;
    case QS_INSTALL: {
        // 0: installed already? (an agent as new as this program's and the host program there: nothing to do); 1: install-check
        // asked; 4: the installation to start; 2: it runs (the SERVER page's INSTALL THE HOST); 3: the link again, to the new agent
        if (qs_.sub == 0 && InstallState().running) { qs_.sub = 2; break; }   // (one started on the SERVER page: its end awaited)
        if (qs_.sub == 0) {
            if (qs_.forceInstall || l == Link::NoAgent) qs_.sub = 4;
            else if (l == Link::Connected) {
                const HostStatus st = Status();
                if (!st.valid) { if (t > kStatusWait) qs_.sub = 4; break; }
                if (AgentOld()) { qs_.sub = 4; break; }
                qs_.sub = 1;
                const unsigned gen = qs_.gen;
                remote_.Agent("install-check", "", [this, gen](int rc, const Json& j, const std::string&) {
                    if (gen != qs_.gen || qs_.step != QS_INSTALL) return;
                    const std::string v = j["version"].Text();
                    if (rc == 0 && j["ok"].Truthy() && j["files"]["mgo1host.exe"].kind == Json::Obj && !v.empty()) {
                        installedVersion_ = v;
                        QuickDone(QS_INSTALL, "@qs_n_already|" + v);
                    } else qs_.sub = 4;
                });
            } else if (l == Link::Connecting) { if (t > kAgainWait) QuickFail(QS_INSTALL, "@qs_f_lost", "qs_h_connect"); }
            else if (!linkFail(QS_INSTALL) && l == Link::Idle) QuickFail(QS_INSTALL, "@qs_f_stopped", "qs_h_connect");
            if (!qs_.running) break;
        }
        if (qs_.sub == 4) {
            if (LocalVersion().empty()) { QuickFail(QS_INSTALL, "@qs_f_package", "qs_h_package"); break; }
            StartInstall(l != Link::NoAgent);
            qs_.sub = 2;
            qs_.stepAt = ImGui::GetTime();
        } else if (qs_.sub == 2) {
            const InstallProgress ip = InstallState();
            if (ip.running) break;
            if (!ip.done) break;
            if (!ip.ok) {
                const std::string& e = ip.error;
                const char* hint = e.rfind("@ie_space", 0) == 0 ? "qs_h_space" : e.rfind("@ie_build", 0) == 0 ? "qs_h_build" :
                                   e.rfind("@ie_disc", 0) == 0 ? "qs_h_disc" : e.rfind("@ie_arch", 0) == 0 ? "qs_h_arch" :
                                   e.rfind("@se_cancelled", 0) == 0 ? "qs_h_cancelled" : "qs_h_install";
                QuickFail(QS_INSTALL, "@m_install_failed|" + e, hint);
                break;
            }
            std::string v;                            // install.sh's last line: @is_installed|<version>
            for (const auto& line : ip.lines) if (line.rfind("@is_installed|", 0) == 0) v = line.substr(14);
            qs_.note[step] = "@qs_n_installed|" + v;
            qs_.sub = 3;                              // (Frame connects again: the new agent)
            qs_.stepAt = ImGui::GetTime();
        } else if (qs_.sub == 3) {
            if (l == Link::Connected && Status().valid) QuickDone(QS_INSTALL, qs_.note[step]);
            else if (l == Link::NoAgent && t > 20) QuickFail(QS_INSTALL, "@qs_f_noagent", "qs_h_install");
            else if (t > kAgainWait) { if (!linkFail(QS_INSTALL)) QuickFail(QS_INSTALL, "@qs_f_lost", "qs_h_connect"); }
        }
        break;
    }
    case QS_ONLINE: {                                 // bot 1's state in the server's list of bots (whatever bot the pages show)
        const HostStatus st = Status();
        bool found = false;
        BotInfo b;
        for (const auto& x : st.bots) if (x.bot == kBot) { b = x; found = true; }
        if (!found && st.valid && st.bot == kBot) { b.online = st.online; b.reason = st.reason; b.service = st.service; found = true; }
        const std::string why = found ? Reason(b.online, b.reason, b.service) : std::string();
        // a game restarted: the old game's state shows a second or two more (online, or a log-in error of the account before);
        // the state counts once it changed since the first look, or after 30 s (a change missed between two looks)
        if (!found) { if (t > kOnlineWait) QuickFail(QS_ONLINE, "@qs_f_timeout|" + std::to_string((int)(kOnlineWait / 60)) + "|-", "qs_h_online", QS_START); break; }
        const std::string now = b.online + "|" + b.reason;
        if (qs_.sub == 0) { qs_.sub = 1; qs_.seen = now; }
        if (now != qs_.seen) qs_.sawDown = true;
        const bool fresh = !qs_.restarted || qs_.sawDown || t > 30;
        if (fresh && b.online == "online") QuickDone(QS_ONLINE, "@qs_n_online");
        else if (fresh && LoginErr(b.reason)) QuickFail(QS_ONLINE, "@qs_f_login|" + b.reason.substr(14), "qs_h_login", QS_ACCOUNT);
        else if (t > kOnlineWait) QuickFail(QS_ONLINE, "@qs_f_timeout|" + std::to_string((int)(kOnlineWait / 60)) + "|" + why, "qs_h_online", QS_START);
        else qs_.note[step] = why;
        break;
    }
    default:
        break;
    }
}

// ---- the page ------------------------------------------------------------------------------------------------------------
// the host built on the server (install.sh's @is_build marks): its stage, the whole build's percent on a bar, the time so far
void App::BuildProgress(const InstallProgress& ip) {
    const std::string& st = ip.buildStage;
    const bool known = st == "deps" || st == "gen" || st == "compile" || st == "link" || st == "done";
    const std::string stage = known ? std::string(T(("bs_" + st).c_str())) : st;
    const long long secs = demo_ ? (long long)(qsDemoBuildMs_ / 1000) : ip.buildSince ? (long long)((plat::TickMs() - ip.buildSince) / 1000) : 0;
    const std::string pct = ip.buildPct >= 0 ? TF("qs_pct", ip.buildPct) : std::string("...");
    Para(TF("qs_build", stage.c_str(), pct.c_str(), Elapsed(secs).c_str()));
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float bw = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + 14 * S), kRow);
    if (ip.buildPct > 0) dl->AddRectFilled(a, ImVec2(a.x + bw * std::min(100, ip.buildPct) / 100.0f, a.y + 14 * S), kGreen);
    dl->AddRect(a, ImVec2(a.x + bw, a.y + 14 * S), kDark, 0, 0, 1.5f * S);
    ImGui::Dummy(ImVec2(bw, 18 * S));
    Para(T("qs_build_note"));
}

// the whole progress (a line, a bar), a failure's "what to do" or the end's text, the buttons (GO, CANCEL), then a row a step with
// its state and under it what it did, does or why it failed
void App::QuickSteps(const std::function<void()>& buttons) {
    PanelHeader(T("p_qs_progress"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int done = 0;
    for (int i = 0; i < QS_COUNT; ++i) done += qs_.state[i] == QS_DONE;
    const InstallProgress ip = InstallState();
    float part = 0;                                   // the share of the step running (the installation's upload and build)
    if (qs_.step == QS_INSTALL && ip.running) {
        if (ip.phase == "upload" && ip.allTotal) part = 0.3f * (float)((double)ip.allDone / (double)ip.allTotal);
        else if (ip.phase == "build") part = 0.3f + 0.7f * std::max(0, ip.buildPct) / 100.0f;
        else if (ip.phase == "install") part = ip.noExe ? 0.95f : 0.6f;
    }
    {
        std::string line;
        if (qs_.done) line = T("qs_all_done");
        else if (qs_.failed >= 0) line = TF("qs_line_failed", qs_.failed + 1, QS_COUNT, T(kStepKeys[qs_.failed]));
        else if (qs_.step >= 0) line = TF("qs_line_step", qs_.step + 1, QS_COUNT, T(kStepKeys[qs_.step]));
        ImGui::PushStyleColor(ImGuiCol_Text, qs_.done ? kGreenText : qs_.failed >= 0 ? kRed : kText);
        Para(line);
        ImGui::PopStyleColor();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float bw = ImGui::GetContentRegionAvail().x, bh = 16 * S;
        dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + bh), kRow);
        dl->AddRectFilled(a, ImVec2(a.x + bw * std::min(1.0f, (done + part) / QS_COUNT), a.y + bh), qs_.failed >= 0 ? kAmber : kGreen);
        dl->AddRect(a, ImVec2(a.x + bw, a.y + bh), kDark, 0, 0, 1.5f * S);
        ImGui::Dummy(ImVec2(bw, bh + 4 * S));
    }
    // a failure: what to do, and where GO starts again; the end: the bot online, the dashboard
    if (qs_.failed >= 0 && !qs_.running) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        Para(Text(qs_.note[qs_.failed]));
        Para(TF("qs_fail_what", T(qs_.hint.empty() ? "qs_h_again" : qs_.hint.c_str())));
        ImGui::PopStyleColor();
        Para(TF("qs_resume", qs_.resumeAt + 1, T(kStepKeys[qs_.resumeAt])));
    }
    if (qs_.done) {
        ImGui::PushStyleColor(ImGuiCol_Text, kGreenText);
        Para(T("qs_done"));
        ImGui::PopStyleColor();
        Gap(6);
        if (Button("qsdash", T("b_qs_dashboard"), T("m_dashboard"), T("d_dashboard"))) page_ = PG_DASHBOARD;
    }
    if (buttons) buttons();
    Gap(10);
    static const char* const kStateKeys[4] = {"v_qs_wait", "v_qs_run", "v_qs_done", "v_qs_fail"};
    for (int i = 0; i < QS_COUNT; ++i) {
        const int s = qs_.state[i];
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float rw = ImGui::GetContentRegionAvail().x, rh = 30 * S;
        dl->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), s == QS_RUN ? kRowHi : kRow);
        dl->AddLine(ImVec2(a.x, a.y + rh - 1), ImVec2(a.x + rw, a.y + rh - 1), kLine, 2 * S);
        const ImU32 col = s == QS_RUN ? kAmber : s == QS_DONE ? kGreen : s == QS_FAIL ? kRed : kTextDim;
        StatusLight(dl, ImVec2(a.x + 16 * S, a.y + rh * 0.5f), 6 * S, col, s == QS_RUN);
        const char* word = T(kStateKeys[s]);
        const float ww = FitWidth(F.body, word, rw * 0.3f, 0);
        const ImU32 wc = s == QS_RUN ? kAmberText : s == QS_DONE ? kGreenText : s == QS_FAIL ? kRed : kTextDim;
        FitText(dl, F.body, ImVec2(a.x + rw - 10 * S - ww, a.y + (rh - F.body->FontSize) * 0.5f), ww + 1, wc, word, 0);
        const std::string name = TF("qs_step_n", i + 1, T(kStepKeys[i]));
        FitText(dl, F.row, ImVec2(a.x + 32 * S, a.y + (rh - F.row->FontSize) * 0.5f), rw - 32 * S - ww - 24 * S, s == QS_WAIT ? kTextDim : kText,
                name.c_str(), RowSpacing());
        ImGui::Dummy(ImVec2(rw, rh));
        // what it did, does now, or why it failed (the failure's reason is above, with what to do)
        const bool installing = i == QS_INSTALL && s == QS_RUN && ip.running;
        if ((qs_.note[i].empty() || s == QS_FAIL) && !installing) continue;
        ImGui::Indent(32 * S);
        Gap(2);
        if (installing) {
            if (ip.phase == "build") BuildProgress(ip);
            else {
                const char* ph = ip.phase == "check" ? T("i_ph_check") : ip.phase == "files" ? T("i_ph_files") : ip.phase == "compare" ? T("i_ph_compare") :
                                 ip.phase == "upload" ? T("i_ph_upload") : T("i_ph_install");
                std::string b = ph;
                if (ip.phase == "upload" && ip.allTotal)
                    b += "  " + TF("v_mb_of", std::to_string(ip.allDone >> 20).c_str(), std::to_string(ip.allTotal >> 20).c_str());
                Para(b);
                if (ip.phase == "upload" && ip.allTotal) {
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float bw = ImGui::GetContentRegionAvail().x;
                    dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + 12 * S), kRow);
                    dl->AddRectFilled(p, ImVec2(p.x + bw * (float)((double)ip.allDone / (double)ip.allTotal), p.y + 12 * S), kGreen);
                    dl->AddRect(p, ImVec2(p.x + bw, p.y + 12 * S), kDark, 0, 0, 1.5f * S);
                    ImGui::Dummy(ImVec2(bw, 16 * S));
                } else if (!ip.lines.empty() && !ip.lines.back().empty() && ip.lines.back()[0] == '@') {   // install.sh's last step
                    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                    Para(TrMsg(ip.lines.back()));
                    ImGui::PopStyleColor();
                }
            }
        } else {
            std::string note = Text(qs_.note[i]);
            if (s == QS_RUN && i == QS_ONLINE) note += "  (" + Elapsed((long long)(demo_ ? 42 : ImGui::GetTime() - qs_.stepAt)) + ")";
            Para(note);
        }
        Gap(4);
        ImGui::Unindent(32 * S);
    }
}

void App::PageQuick() {
    const bool run = qs_.running;
    if (!qs_.ever) PanelHeader(T("p_qs"));
    std::string missing;
    if (!fHost_[0]) missing = T("qs_need_host");
    else if (!fUseKey_ && !fPass_[0]) missing = T("qs_need_pw");
    else if (fUseKey_ && !fKey_[0]) missing = T("qs_need_key");
    else if (strlen(qsId_) < 3 || strlen(qsId_) > 15 || !NameChars(qsId_)) missing = T("e_acc_id");
    else if (strlen(qsPw_) < 6 || strlen(qsPw_) > 15 || !NameChars(qsPw_)) missing = T("e_acc_pw");
    else if (!disc_[0]) missing = T("qs_need_disc");
    else if (!QuickDiscOk()) missing = T("ie_disc");
    else if (!demo_ && LocalVersion().empty()) missing = T("qs_need_package");
    auto buttons = [&] {
        Gap(8);
        if (!run) {
            if (Button("qsgo", T("b_go"), T("b_go"), T("h_go"), missing.empty())) QuickGo();
            if (!missing.empty()) {
                Gap(4);
                ImGui::PushStyleColor(ImGuiCol_Text, kRed);
                Para(missing);
                ImGui::PopStyleColor();
            }
        } else if (Button("qscancel", T("b_cancel"), T("b_cancel"), T("h_qs_cancel"), true, true)) QuickCancel();
    };
    // before GO: what it does, the fields, GO; after: the progress first (the fields below it, to fix one after a failure)
    if (!qs_.ever) Para(T("qs_intro"));
    else QuickSteps(qs_.done ? std::function<void()>() : std::function<void()>(buttons));
    Gap(12);
    ImGui::BeginDisabled(run);
    // the server: the SERVER page's connection fields
    PanelHeader(T("p_qs_server"));
    TextRow("qshost", T("sv_address"), fHost_, sizeof fHost_, T("sv_address"), T("h_sv_address"));
    TextRow("qsport", T("sv_port"), fPort_, sizeof fPort_, T("sv_port"), T("h_sv_port"), false, 0.55f, ImGuiInputTextFlags_CharsDecimal);
    TextRow("qsuser", T("sv_user"), fUser_, sizeof fUser_, T("sv_user"), T("h_sv_user"), false, 0.55f, ImGuiInputTextFlags_CharsNoBlank);
    if (ValueRow("qsauth", T("sv_auth"), fUseKey_ ? T("v_keyfile") : T("v_password"), T("sv_auth"), T("h_sv_auth"), !run)) fUseKey_ = !fUseKey_;
    if (!fUseKey_) TextRow("qspass", T("sv_password"), fPass_, sizeof fPass_, T("sv_password"), T("h_qs_password"), true);
    else {
        TextRow("qskey", T("sv_key"), fKey_, sizeof fKey_, T("sv_key"), T("h_sv_key"));
        TextRow("qskeypass", T("sv_keypass"), fKeyPass_, sizeof fKeyPass_, T("sv_keypass"), T("h_sv_keypass"), true);
        Gap(4);
        if (Button("qsbrowsekey", T("b_browse_key"), T("b_browse_key"), T("h_sv_key"), !run) && !plat::Pick(PICK_KEY, true, T("b_browse_key")))
            Toast(T("m_no_picker"), true);
        std::string p;
        if (plat::Picked(PICK_KEY, p) && !run) snprintf(fKey_, sizeof fKey_, "%s", p.c_str());
    }
    // the bot's SaveMGO account (kept on this PC, encrypted, as HOST ACCOUNT's KEEP ON THIS PC); one of those kept, by its arrows
    Gap(10);
    PanelHeader(T("p_qs_account"));
    TextRow("qsid", T("a_id"), qsId_, 16, T("a_id"), T("h_a_id"), false, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    TextRow("qspw", T("a_pw"), qsPw_, 16, T("a_pw"), T("h_qs_account_pw"), true, 0.55f, ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    if (!cfg.accounts.empty()) {
        int at = -1;
        for (size_t i = 0; i < cfg.accounts.size(); ++i) if (cfg.accounts[i].id == Upper(qsId_)) at = (int)i;
        const int d = ValueRow("qskept", T("qs_kept"), at >= 0 ? cfg.accounts[at].id.c_str() : "-", T("qs_kept"), T("h_qs_kept"), !run);
        if (d) {
            const int n = (int)cfg.accounts.size();
            const int k = at < 0 ? (d > 0 ? 0 : n - 1) : ((at + d) % n + n) % n;
            QuickAccount(cfg.accounts[k].id.c_str(), cfg.accounts[k].password.c_str());
        }
    }
    // the disc's folder: the SERVER page's, checked at once (the folder holding MGS and MODULES)
    Gap(10);
    PanelHeader(T("p_qs_disc"));
    TextRow("qsdisc", T("sv_disc"), disc_, sizeof disc_, T("sv_disc"), T("h_sv_disc"), false, 0.6f);
    Gap(6);
    if (Button("qsbrowse", T("b_browse"), T("b_browse"), T("h_sv_disc"), !run) && !plat::Pick(PICK_DISC, false, T("b_browse")))
        Toast(T("m_no_picker"), true);
    {
        std::string p;
        if (plat::Picked(PICK_DISC, p) && !run) {
            snprintf(disc_, sizeof disc_, "%s", p.c_str());
            if (!demo_) { cfg.discDir = disc_; cfg.Save(); sound::Init(cfg.discDir); }
        }
    }
    if (disc_[0]) {
        Gap(4);
        const bool ok = QuickDiscOk();
        ImGui::PushStyleColor(ImGuiCol_Text, ok ? kGreenText : kRed);
        Para(ok ? TF("qs_disc_ok", HideProfile(qsDiscRoot_).c_str()).c_str() : T("ie_disc"));
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    if (!qs_.ever) buttons();
    // what GO sets up; where to rent a server
    Gap(14);
    {
        const GameSettings g = QuickGame();
        std::string rules, maps;
        std::vector<int> seenRules, seenMaps;
        for (const auto& r : g.rotation) {
            if (std::find(seenRules.begin(), seenRules.end(), r.first) == seenRules.end()) {
                rules += (rules.empty() ? "" : ", ") + RuleName(r.first);
                seenRules.push_back(r.first);
            }
            if (std::find(seenMaps.begin(), seenMaps.end(), r.second) == seenMaps.end()) {
                maps += (maps.empty() ? "" : ", ") + std::string(MapName(r.second));
                seenMaps.push_back(r.second);
            }
        }
        Section(T("p_qs_what"), TF("qs_what", kBot, g.name.c_str(), rules.c_str(), maps.c_str(), g.maxJoin, kVdolls, kVdollLevel, kAdaptMin, kAdaptMax));
    }
    Gap(10);
    Section(T("p_qs_rent"), T("qs_rent"));
}

// ---- the self-test --------------------------------------------------------------------------------------------------------
// QUICK SETUP's pages: PG_QUICK filled (never run), DEMO_QS_EMPTY, each step running, a failed build, a refused log-in, done.
// The layout test's longest names; the showcase's own server and account (mgo1-server.example, GRU_HOST)
void App::QuickDemo(int page) {
    if (!gDemoFields.saved) { gDemoFields = {true, fHost_, fPass_, disc_}; }
    const bool mine = page == PG_QUICK || page >= DEMO_QS_EMPTY;
    qsDemo_ = mine;
    snprintf(fHost_, sizeof fHost_, "%s", gDemoFields.host.c_str());
    snprintf(fPass_, sizeof fPass_, "%s", gDemoFields.pass.c_str());
    snprintf(disc_, sizeof disc_, "%s", gDemoFields.disc.c_str());
    if (!mine) return;
    toast_.clear();
    fUseKey_ = false;
    snprintf(fUser_, sizeof fUser_, "root");
    snprintf(fPort_, sizeof fPort_, "22");
    if (showcase_) QuickAccount("GRU_HOST", "SNAKEEATER64");
    else QuickAccount("WWWWWWWWWWWWWWW", "WWWWWWWWWWWWWWW");
    qsDiscOk_ = true;
    qsDiscFor_ = disc_;
    qsDiscRoot_ = showcase_ ? "%USERPROFILE%\\Documents\\MGO1\\MGS3 Subsistence disc 2 (online)" : disc_;
    if (page == DEMO_QS_EMPTY) {
        fHost_[0] = fPass_[0] = disc_[0] = qsId_[0] = qsPw_[0] = 0;
        qsDiscOk_ = false;
        qsDiscFor_ = disc_;
    }
    qs_ = QuickRun{};
    qsDemoInstall_ = InstallProgress{};
    qsDemoBuildMs_ = 0;
    if (page == PG_QUICK || page == DEMO_QS_EMPTY) return;
    const std::string host = fHost_, version = "build 1.00 d11286182942", acc = qsId_;
    const GameSettings g = QuickGame();
    const std::string notes[QS_COUNT] = {"@qs_n_reached", "@qs_n_signedin|root", "@qs_n_installed|" + version, "@qs_n_account|" + acc,
                                         "@qs_n_game|" + g.name + "|" + std::to_string((int)g.rotation.size()) + "|" + std::to_string(g.maxJoin),
                                         "@qs_n_vdolls|7|5|1|10", "@qs_n_restarted", "@qs_n_online"};
    const int running = page == DEMO_QS_CONNECT ? QS_CONNECT : page == DEMO_QS_TRUST ? QS_SIGNIN : page == DEMO_QS_UPLOAD || page == DEMO_QS_BUILD ? QS_INSTALL :
                        page == DEMO_QS_ACCOUNT ? QS_ACCOUNT : page == DEMO_QS_GAME ? QS_GAME : page == DEMO_QS_VDOLLS ? QS_VDOLLS :
                        page == DEMO_QS_START ? QS_START : page == DEMO_QS_ONLINE ? QS_ONLINE : page == DEMO_QS_FAILBUILD ? QS_INSTALL :
                        page == DEMO_QS_FAILLOGIN ? QS_ONLINE : QS_COUNT;
    qs_.ever = true;
    for (int i = 0; i < QS_COUNT; ++i) {
        qs_.state[i] = i < running ? QS_DONE : QS_WAIT;
        if (i < running) qs_.note[i] = notes[i];
    }
    if (page == DEMO_QS_DONE) { qs_.done = true; return; }
    if (page == DEMO_QS_FAILBUILD) {                  // the compiler killed (the server's memory), as install.sh says it
        qs_.state[QS_INSTALL] = QS_FAIL;
        qs_.note[QS_INSTALL] = "@m_install_failed|@ie_build|g++: fatal error: Killed signal terminated program cc1plus (MGO1Loader/gen/mgs3_n_func_0217.cpp)";
        qs_.hint = "qs_h_build"; qs_.failed = qs_.resumeAt = QS_INSTALL;
        return;
    }
    if (page == DEMO_QS_FAILLOGIN) {                  // SaveMGO refused the bot's account: GO starts again at the account
        qs_.state[QS_ONLINE] = QS_FAIL;
        qs_.note[QS_ONLINE] = "@qs_f_login|Server login failed. ( 816:-4 )";
        qs_.hint = "qs_h_login"; qs_.failed = QS_ONLINE; qs_.resumeAt = QS_ACCOUNT;
        return;
    }
    qs_.running = true;
    qs_.step = running;
    qs_.state[running] = QS_RUN;
    qs_.note[running] = running == QS_CONNECT ? "@qs_n_connecting|" + host : running == QS_SIGNIN ? "@qs_n_trust" : running == QS_ONLINE ? T("r_login") : "";
    if (running == QS_INSTALL) {
        InstallProgress& ip = qsDemoInstall_;
        ip.running = true; ip.noExe = true;
        ip.lines = {"@il_server|x86_64|ubuntu 24.04|0|36120", "@il_build_here", "@il_disc|%USERPROFILE%\\Documents\\MGO1\\MGS3 Subsistence disc 2 (online)"};
        if (page == DEMO_QS_UPLOAD) {
            ip.phase = "upload"; ip.file = "disc/MGS/NET/STAGE_N.DAT"; ip.allDone = 45ull << 20; ip.allTotal = 88ull << 20;
        } else {
            ip.phase = "build"; ip.buildStage = "compile"; ip.buildPct = 63; ip.buildSince = 1;
            qsDemoBuildMs_ = (24 * 60 + 5) * 1000ull;
            ip.lines.push_back("@is_build|@bs_compile");
        }
        qs_.sub = 2;
    }
}
