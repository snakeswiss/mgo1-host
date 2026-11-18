// MGO1 Host: the pages (dashboard, bots, players, Game Report, game settings, host account, player
// lists, network, log, server, options, about) over the server's agent.
#pragma once
#include "config.h"
#include "remote.h"
#include "report.h"
#include "rules.h"
#include "imgui.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

struct GameSettings {
    std::string name = "SAVEMGO HOST", comment, password;
    std::vector<std::pair<int, int>> rotation{{1, 2}, {0, 8}};
    int red = 0, blue = 1, maxJoin = 8, weapons = 0;
    std::map<std::string, int> ruleValues;      // each rule's minutes, rounds, tickets by their keys ("tdm_rounds", rules.h)
    bool unique = false, rumble = false, ghosts = false, ff = false, autoBal = true, idle = true, tkKick = false, voice = true;
    int balanceDiff = 2, idleMin = 5, tkCount = 3, ratingRestr = 0, rating = 1000;
    GameSettings();                             // the rules' values at their defaults
    int& Value(const GameRule& r, int kind);    // a rule's minutes, rounds or tickets (RV_*)
    int ValueOf(const GameRule& r, int kind) const;
    void FromMap(const Json& kv);
    std::string ToJson() const;
    bool operator==(const GameSettings& o) const { return ToJson() == o.ToJson(); }
};

struct NetServers {                             // the game's servers, for every bot of the server (NETWORK)
    bool custom = false;
    std::string web, gate, stun;
    int gatePort = 5731, stunPort = 3478;
};

enum Page { PG_DASHBOARD, PG_BOTS, PG_DOLLS, PG_VDOLLS, PG_PLAYERS, PG_REPORT, PG_GAME, PG_ACCOUNT, PG_LISTS, PG_NETWORK, PG_LOG, PG_SERVER, PG_OPTIONS,
           PG_ABOUT, PG_QUICK, PG_COUNT };       // (build 1.00) PG_QUICK: QUICK SETUP (quick.cpp), first in the menu

enum { PICK_KEY = 1, PICK_DISC };               // the file boxes of the SERVER page and QUICK SETUP (plat::Pick)

// (build 1.00) QUICK SETUP's steps (quick.cpp), in their order, and a step's state
enum QsStep { QS_CONNECT, QS_SIGNIN, QS_INSTALL, QS_ACCOUNT, QS_GAME, QS_VDOLLS, QS_START, QS_ONLINE, QS_COUNT };
enum QsState { QS_WAIT, QS_RUN, QS_DONE, QS_FAIL };
struct QuickRun {
    int state[QS_COUNT] = {};                   // QsState of each step
    std::string note[QS_COUNT];                 // what a step did, does now, or why it failed ("@key|arg" or a text)
    std::string hint;                           // the failed step's "what to do" (a string key)
    int step = -1;                              // the step running (-1: none)
    int failed = -1, resumeAt = 0;              // the step that failed last, the step GO starts again at
    int jumpTo = -1;                            // after the connection again: the step to go on with (-1: the next one)
    bool running = false, done = false, ever = false;
    int sub = 0;                                // the step's own progress (install: 0 deciding, 1 asked, 2 installing, 3 reconnecting)
    bool forceInstall = false;                  // GO again after a failed installation: installed whatever the server has
    bool restarted = false, sawDown = false;    // START restarted a running game; ONLINE saw its state change since (a new state)
    std::string seen;                           // ONLINE's first look at bot 1's state (the old game's, after a restart)
    double stepAt = 0, doneAt = -1;             // when the step started; when the bot came online (the dashboard 2 s later)
    unsigned gen = 0;                           // one more at each GO and CANCEL: an older answer changes nothing
    std::string server, account, disc;          // what GO was given (a change since: GO starts again where it matters)
};

struct VdollLive {                              // a virtual doll in its bot's game now (the status' "vdolls")
    int n = 0, player = 0, team = -1, deaths = 0, leaves = 0;
    std::string state, life;                    // in_game, joining, leaving, out; the engine's words for its life
};

struct VdollBot {                               // a bot's virtual dolls (mgo1host-agent vdolls, docs/VIRTUAL_DOLL.md)
    int bot = 0, count = 0, most = -1;          // most: its game's players less the host and a seat kept (-1 not known)
    std::string teams = "auto", level = "5";    // one a doll, the last for the rest
    std::string weapons = "auto";               // (v150-host beta 15) auto, ak47, xm16e1, scorpion, m37, svd: the same way
    std::string controls = "direct";            // (3.3) direct or player (agent 3.5's "controls"; an older agent: direct)
    bool adapt = false;                         // (3.4) the live level (agent 3.6's "adapt"; an older agent: off)
    int adaptMin = 1, adaptMax = 10;            // (3.4) its lowest and highest level ("adapt_min", "adapt_max")
    std::vector<float> levelsNow;               // (3.4) the level now of each virtual doll in the game ("in_game"), in their numbers'
    std::vector<int> levelsNowSet;              // order ("levels_now"; empty: not said), and the levels set they move from
    std::string appliedLevel;                   // (3.4) the levels its running game was given ("" not known)
    std::vector<std::string> names;             // the names file's (VDOLL<n> past its end)
    int setInGame = -1, inGame = 0;             // its running game's count (-1: none set there), those in it now
    std::vector<VdollLive> live;
    std::vector<std::string> appliedNames;      // the names its running game was given
    bool applied = false;                       // (an older start: not known)
    int pending = -1;                           // 1: the saved settings are not the running game's, 0 they are, -1 not known
};

struct DollInfo {                               // a PLAYABLE DOLL of the server (mgo1host-agent dolls)
    int n = 0, bot = 1, difficulty = 3, age = -1;   // (v141 beta 6) difficulty 1 TRAINING .. 10 PRO (beta 5's 6: ELITE)
    int adaptMin = 1, adaptMax = 10;            // (v141 beta 6) the live level's bounds (the server's control)
    bool boundsKnown = false;                   // (the agent reports them: an older one does not)
    std::string account, team = "auto", loadout = "auto", order = "free";
    bool hasAccount = false, enabled = true, running = false, adapt = true;   // adapt: the live level
    bool accountless = false;                   // no account: DOLL_<n> in its bot's own lobby (docs/PLAYABLE_DOLL.md 1)
    bool nameSet = false;                       // (v141 beta 5) an accountless doll renamed (its DOLL_NAME, agent 2.9)
    bool aimDirect = false;                     // (3.3) its aim: direct (agent 3.5's "aim"), else pad
    bool fromVdoll = false;                     // (3.2) its difficulty is "as the virtual dolls" (the agent's difficulty_from: vdoll);
                                                // difficulty is then the level the server resolved (0: not known yet)
    float level = 0;                            // the level it plays at now (status.json), 0 not known
    std::string levelWhy;                       // even, behind (stronger), ahead (softer), fixed
    // (v141 beta 6) status.json: its tier now ("VETERAN"), the live level's judgment (the strengths 1 .. 10, how sure, why),
    // the real players of its game and its presence (waiting_for_real: a spectator until one comes)
    std::string tier, why, presence;
    float enemy = -1, ally = -1, confidence = -1;
    int realPlayers = -1;
    std::vector<std::string> players;           // its bot's game's players now (for "follow" / "target")
    // what it reports (status.json): its player name, what it does, the room, its team, life, score
    std::string name, act, arg, rule;
    int team_ = -1, life = 0, kills = 0, deaths = 0, real = 0, phase = -1, map = -1;
    bool alive = false, inMatch = false;
};

// (3.2) why a bot's PLAYABLE DOLLS wait (mgo1host-agent dolls, each of its "bots" from agent 3.4; an older one has none):
// what mgo1doll-manager counts for them
struct DollWait {
    std::string code, why;                      // waiting_code ("" none: bot_stopped, not_hosting, seats, server_full,
                                                // pace_guard, gate_off), waiting_why (the manager's own words)
    int seats = 0, real = 0, vdolls = 0, kept = 0, freeForDolls = 0;   // its game's seats and who holds them
    int dollsSet = 0, dollsIn = 0;              // its dolls set to play, those in its game now
    int paceCap = -1, paceWaitS = 0;            // the pace guard's most for the bot (-1 none), the seconds before one more
};

// (beta 16) a crash report the server keeps for a bot's or a doll's game that ended badly (mgo1host-agent crashes, agent
// 3.4; docs/CRASH_REPORTS.md)
struct CrashInfo {
    std::string source, name, reason;           // bot2 / doll3; <UTC time>_<reason>.txt; crash70, signal<N>, hung, lost, exit<N>, wild
    long long t = 0;                            // its time (UTC, ms)
    int size = 0, rule = -1, map = -1;          // bytes; the round's rule and map by their numbers (-1 not known)
    std::string what, game, players, wild;      // the header's words as the server wrote them
};

std::string ErrText(const Json& j);             // an error of the server's program in the program's language
const char* ProgramVersion();                   // this program's version ("build 1.00", src/version.h)
std::string LogMessageText(std::string m);      // a journal's or a daily log's message in the program's language ("": technical)
std::string DollLogText(const std::string& m);  // a doll's, a launcher's, the doll manager's message (logs.cpp)
std::string CodecLogText(const std::string& m); // a codec call in a doll's log (logs.cpp)
std::string DollActivityText(const std::string& act, const std::string& arg);   // what a doll does (dolls.cpp)
std::string DollOrderText(const std::string& order);                          // a doll's order (dolls.cpp)
std::string DollSkillText(int difficulty);      // (v141 beta 6) "7 VETERAN" in the program's language (dolls.cpp)
std::string DollTierText(int tier);             // a tier's name, 1 TRAINING .. 10 PRO
int DollTierOf(const std::string& name);        // a tier's English name (the dolls' logs and status) -> 1 .. 10, 0 not one
std::string DollWhyText(const std::string& code);   // why the live level moves (status live.reason, DOLL ADAPT's reason=)
bool DollNameOk(const std::string& s, int n);   // a DOLL's name in the game (the agent's DOLL_NAME_RE); n > 0: DOLL_<n> its own

class App {
public:
    void Init(const std::string& exeDir);
    void Frame(float width, float height);      // the whole window, every frame
    void Shutdown();
    bool fontsDirty = true;                     // the language or the scale changed: the fonts are built again
    Config cfg;
    std::string exeDir;                         // the program's folder (its server folder, its fonts folder)
    void SetLogo(ImTextureID tex, float w, float h) { logoTex_ = tex; logoW_ = w; logoH_ = h; }
    bool Animating() const;                     // something moves on screen (a pulsing light, an upload)
    // the layout self-test: every page with made-up data, no server
    void InitDemo(const std::string& exeDir);
    enum { DEMO_TRUST = PG_COUNT, DEMO_CONFIRM, DEMO_MENU, DEMO_KEYCHANGED, DEMO_ADDBOT, DEMO_LOGINERR, DEMO_DOLLMENU, DEMO_DAILYLOG, DEMO_PRESETS, DEMO_AGENTOLD, DEMO_DOLLNAME, DEMO_DOLLVD,
           DEMO_CRASHES, DEMO_CRASH, DEMO_CRASHOLD, DEMO_VDOLLSOLD, DEMO_VDOLLS35,
           DEMO_QS_EMPTY, DEMO_QS_CONNECT, DEMO_QS_TRUST, DEMO_QS_UPLOAD, DEMO_QS_BUILD, DEMO_QS_ACCOUNT, DEMO_QS_GAME, DEMO_QS_VDOLLS,
           DEMO_QS_START, DEMO_QS_ONLINE, DEMO_QS_FAILBUILD, DEMO_QS_FAILLOGIN, DEMO_QS_DONE, DEMO_COUNT };   // (3.3) DEMO_VDOLLSOLD: agent 3.4's
                                                // VIRTUAL DOLLS page; (3.4) DEMO_VDOLLS35: agent 3.5's (CONTROLS, the LIVE LEVEL greyed);
                                                // (build 1.00) DEMO_QS_*: QUICK SETUP empty, at each step, failed, done (quick.cpp)
    void SetDemoPage(int page);
    // (build 1.00) QUICK SETUP (quick.cpp): GO (the page's button; tests/panel_quick_go.cpp presses it too), CANCEL, the run's steps
    void QuickGo();
    void QuickCancel();
    const QuickRun& Quick() const { return qs_; }
    void QuickAccount(const char* id, const char* pw);   // the bot's SaveMGO account typed in QUICK SETUP's fields
    // (tests/panel_quick_go.cpp) the server's and the disc's fields typed, the installation's progress as the page shows it
    void QuickFill(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& disc);
    InstallProgress Installing() const { return InstallState(); }
    // the host key's box answered (TRUST: the key kept for this server, connected again; CANCEL: not connected), as its buttons do
    void TrustAnswer(bool trust);
    // (build 1.00) the showcase (MGO1_HOST_SHOWCASE=1 with --selftest; "all": every language): the release's pictures,
    // showcase.cpp's plausible made-up host at a window's size (gui.cpp Showcase)
    bool Showcase() const { return showcase_; }
    bool ShowcaseAllLanguages() const { return showcaseAll_; }
    struct ShowcaseDrawn { const ImDrawList* page = nullptr; float clipTop = 0, clipBottom = 0, width = 0; };   // the page's own draw list
    const ShowcaseDrawn& ShowcasePageDrawn() const { return showcaseDrawn_; }
    void ShowcaseCut(float top, float bottom) { showcaseCutTop_ = top; showcaseCutY_ = bottom; }   // the page drawn between (0: its edge)
    void ShowcaseEnd(bool end) { showcaseEnd_ = end; }  // the next page set shown from its end

private:
    void Chrome(float w, float h);
    void Menu();
    void PageDashboard();
    void PageBots();
    void PageDolls();
    void DollMenu();
    void LoadDolls();
    void DollOrder(int n, const std::string& order, const std::string& okText = "");
    void DollSet(int n, const std::string& json, const std::string& okText = "");
    void DollBound(int n, const char* key, int v);  // (v141 beta 6) adapt_min / adapt_max: the order live, then kept (doll-set)
    void DemoTiers();                           // (the self-test: the language's widest tier on the dolls)
    void DollCount(int bot, int n, const std::string& name = "");   // a bot's dolls with no account: n of them (doll-count;
                                                // (v141 beta 5) name: the one it adds)
    // the virtual dolls (vdolls.cpp): each bot's, the form of the bot shown
    void PageVdolls();
    void LoadVdolls(std::function<void(bool ok)> done = nullptr);   // (build 1.00) done: QUICK SETUP's, once answered
    void VdollsFill(const VdollBot& b);         // the form from a bot's saved settings
    // the form's changed keys to the server (vdoll-set; restart: now); (build 1.00) done: the answer to QUICK SETUP, no message
    void VdollsSave(bool restart, std::function<void(bool ok, const std::string& error)> done = nullptr);
    void VdollCount(int bot, int n);            // (v150-host beta 15) a bot's virtual dolls wanted now (vdoll-count: live)
    void VdollCountBlock();                     // the DOLLS page's VIRTUAL DOLLS block: each bot's count, one less, one more
    void AimHelp();                             // (3.3) the PLAYABLE DOLL page's line of help under AIM
    void PagePlayers();
    void PageReport();
    void PageGame();
    void PageAccount();
    void PageLists();
    void PageNetwork();
    void PageLog();
    void PageLogDaily();
    void PageLogCrash();                        // (beta 16) the LOG page's CRASH REPORTS: the list, one report
    void LoadCrashes();                         // mgo1host-agent crashes (every minute while connected: the bots' rows)
    void LoadCrashReport(int from);             // mgo1host-agent crash <source>/<name>, 5000 lines a call
    void SaveCrashReport();                     // the report shown in a file of <Documents>/MGO1 Host/Logs
    std::string CrashReasonText(const std::string& reason) const;   // a reason code in the program's language
    std::string CrashNoteText(int bot) const;   // a bot's crash reports of the last 24 hours in a line ("" none)
    void CrashNote(int bot);                    // that line in red, wrapped, under the bot's row or state
    void DemoCrashes();                         // (the self-test's reports: every reason, a bot's and a doll's, one open)
    void LoadLogList();
    void LoadLogDay(bool more);
    void LoadLogEarlier();                      // (v141 beta 2) the 2000 lines before those shown
    void SaveLogDay();
    void SaveLogChunk(const std::string& src, const std::string& day, int level, int from);
    void WriteSavedLog(const std::string& src, const std::string& day, int level);
    std::string LogSourceLabel(const std::string& name) const;
    void PageServer();
    void PageOptions();
    void PageAbout();
    // (build 1.00) QUICK SETUP (quick.cpp): the page, its run every frame, a step's start and end, the self-test's states
    void PageQuick();
    void QuickTick();
    void QuickStep(int step);                   // the step starts (QS_COUNT: the run is done)
    void QuickDone(int step, const std::string& note);
    void QuickFail(int step, const std::string& reason, const char* hint, int resumeAt = -1);
    void QuickSteps(const std::function<void()>& buttons);   // the whole progress, a failure's or the end's text, the buttons, the steps
    void QuickDemo(int page);
    void BuildProgress(const InstallProgress& ip);   // the host's build on the server: its stage, a bar, the time so far
    std::string QuickServerKey() const;         // the connection's fields as one text (a change: GO connects again)
    bool QuickDiscOk();                         // the disc's folder holds MGS and MODULES (FindDisc, checked when it changes)
    void PlayerMenu();
    void BotMenu();
    bool ListRow(const char* id, const std::string& name, const char* button, const std::string& help, const char* button2 = nullptr, bool* click2 = nullptr,
                 const std::string& shortName = "");   // shortName: shown when the name does not fit
    void Connect();
    void ConnectTo(int server);
    void Disconnect();
    void SelectBot(int bot);
    void Ask(const std::string& text, std::function<void()> yes);
    void Toast(const std::string& text, bool error = false);
    // (build 1.00) done: the answer (ok, the error in the program's language) for QUICK SETUP; bot: another than the one shown
    void AgentCmd(const std::string& args, const std::string& input = "", const std::string& okText = "",
                  std::function<void(bool ok, const std::string& error)> done = nullptr, int bot = 0);
    // (build 1.00) a bot's SaveMGO account (account-set), kept on this PC (encrypted) when keep: HOST ACCOUNT's SAVE, QUICK SETUP
    void AccountSet(int bot, const std::string& id, const std::string& pw, bool keep, std::function<void(bool ok, const std::string& error)> done);
    void StartInstall(bool update);             // (build 1.00) INSTALL THE HOST, UPDATE THE HOST, QUICK SETUP: the disc's folder kept, sent
    // (3.5, v150-host beta 22) settings-get: fill (the page's first look, RELOAD): the form from the server's copy, unless a
    // SAVE or another RELOAD was sent after it or the bot shown changed; else (every 10 s on the page): what the running game
    // plays, this server's maps, and the form refreshed only when it holds nothing unsaved
    void LoadSettings(bool fill = true);
    void SettingsInfo(const Json& j);           // (3.5) agent 3.7's account: version, running, pending, left out, maps here
    // (3.5) settings-set, --base the copy read unless force; (build 1.00) done: the answer to QUICK SETUP, no message
    void SaveSettings(bool restart, bool force, std::function<void(bool ok, const std::string& error)> done = nullptr);
    bool MapHere(int map) const;                // (3.5) the server has this map (agent 3.7's maps_here; not known: yes)
    void GameSaved(const GameSettings& g);      // saved on the server: the last game settings on this PC too
    void FillGame(const GameSettings& g);       // the GAME page's form from kept settings (saved by SAVE only)
    void GamePresets();                         // the GAME page: the settings this PC keeps (the last saved, the presets)
    bool PcOnlyMap(int map) const;
    void LoadAccount();
    void LoadLog();
    void LoadNetwork();
    void ReportTick();                          // the Game Report: the watch's events, the files of every bot
    void ReportFetchShown();                    // the bot shown: its last day
    std::string ServerKey() const;
    std::string ServerName() const;
    std::string Reason(const std::string& online, const std::string& reason, const std::string& service) const;
    std::string LocalVersion() const;
    // (v141) the agent this program carries (server/mgo1host-agent's AGENT_VERSION) and the server's older than it: a banner
    // with the update on the pages that need the new one (the dashboard, the bots, the dolls)
    std::string bundledAgent_;
    bool demoAgentOld_ = false;                 // (the self-test's page: the server's agent 3.7, this program's build 1.00)
    bool AgentOld() const;
    void UpdateBanner();
    void StartUpdate();
    // the server's state, or the self-test's made-up one
    HostStatus Status() const;
    Link LinkState(std::string* msg = nullptr) const;
    InstallProgress InstallState() const { return demo_ ? (qsDemo_ ? qsDemoInstall_ : demoInstall_) : remote_.install(); }
    bool RemoteRunning() const { return demo_ || remote_.Running(); }
    int CurBot() const { return demo_ ? demoBot_ : remote_.Bot(); }

    Remote remote_;
    Report report_;
    int page_ = PG_SERVER;
    // the connection form
    char fLabel_[64] = "", fHost_[256] = "", fUser_[64] = "root", fPort_[8] = "22", fPass_[128] = "", fKey_[520] = "", fKeyPass_[128] = "";
    bool fUseKey_ = false, fRemember_ = true;
    // pages' state
    GameSettings gs_, gsServer_;
    bool gsLoaded_ = false, gsLoading_ = false;
    std::string gsError_;
    // (3.5, v150-host beta 22) the server's copy of the settings and the game running (agent 3.7's settings sync)
    std::string gsVersion_;                     // the version of the server's copy the form came from (settings-set --base)
    unsigned gsGen_ = 0;                        // one more at each SAVE and RELOAD sent: an older settings-get fills nothing
    bool gsSync_ = false;                       // the agent tells the version and what the running game plays (3.7)
    bool gsRunning_ = false, gsElsewhere_ = false, gsInfoLoading_ = false;
    bool gsVersionOdd_ = false;                 // (sync2) the server's last version was not small hex digits: SAVE without --base
    std::vector<std::string> gsPending_, gsLeftOut_;   // the keys saved the running game does not play yet; its entries left out
    std::vector<int> gsMapsHere_;               // this server's maps (empty: not known, an older agent)
    int gsReal_ = 0;                            // the running game's real players
    double gsInfoAt_ = -100;
    char gName_[32] = "", gComment_[80] = "", gPass_[32] = "";
    char gPreset_[32] = "";                     // the name of the preset to keep the form under
    char aId_[32] = "", aPw_[32] = "";
    bool aShow_ = false, aLoaded_ = false, aPwSet_ = false, aKeep_ = true;
    char blackName_[32] = "", whiteName_[32] = "";
    std::vector<std::string> log_;              // the host's journal as the server sends it (log-in data masked)
    bool logAuto_ = true, logLoading_ = false, logTech_ = false;   // logTech_: every line, as it is
    bool logAtEnd_ = true, logToEnd_ = true;                       // the view follows the newest lines
    double logAt_ = -100;
    struct LogView { std::string time, text; };
    std::vector<LogView> logView_;              // log_ in the program's language
    int logViewLang_ = -1; size_t logViewOf_ = 0; bool logViewTech_ = false;
    void SaveLog();
public:
    // the daily logs (mgo1host-agent logs): a folder per bot, doll and the doll manager, a file per day (UTC)
    struct LogDayInfo { std::string day; int lines = 0, errors = 0, warnings = 0, starts = 0, crashes = 0; };
    struct LogSourceInfo { std::string name; std::vector<LogDayInfo> days; };
    struct DayView { std::string time, text; char level = 'I'; bool raw = false; };
private:
    bool logDaily_ = false;                     // the LOG page shows the daily logs (else the bot's journal, live)
    // (beta 16) the crash reports: the list (asked every minute while connected), the report shown
    bool logCrash_ = false;                     // the LOG page shows the crash reports
    std::vector<CrashInfo> crashes_;            // the newest first
    bool crLoading_ = false, crLoaded_ = false, crOld_ = false;   // crOld_: an agent without `crashes` (before 3.4)
    std::string crError_;
    double crAt_ = -100;
    long long crNow_ = 0;                       // the server's clock at the list's answer (ms, UTC)
    std::string crOpen_;                        // "<source>/<name>" of the report shown ("": the list)
    std::vector<std::string> crLines_;          // its lines as the server sent them (the header, "----", the game's last lines)
    bool crLineLoading_ = false;
    int crTotal_ = 0;
    std::vector<DayView> crView_;
    size_t crViewOf_ = (size_t)-1;
    int crViewLang_ = -1;
    char crFind_[48] = "";                      // SEARCH: only the lines that hold it
    std::vector<LogSourceInfo> logSources_;
    bool logListLoading_ = false, logListLoaded_ = false, logDayLoading_ = false;
    std::string logListError_;
    int logSrc_ = 0, logDay_ = 0, logLevel_ = 0;   // the source shown, its day (newest first), 0 everything 1 problems 2 errors
    std::vector<std::string> dayLines_;         // the day's lines as the server sent them
    int dayNext_ = -1, dayTotal_ = 0, dayFrom_ = 0;   // (dayFrom_: the number of the first line shown: the view opens on the day's end)
    std::vector<std::string> saveLines_;        // (v141 beta 2) the day being saved, fetched 5000 lines at a time
    bool saveBusy_ = false, dayScrollEnd_ = false;  // (the view scrolled to its end once the day's last lines come)
    std::vector<DayView> dayView_;
    size_t dayViewOf_ = (size_t)-1;
    int dayViewLang_ = -1;
    char disc_[520] = "";
    std::string installedVersion_;
    double checkAt_ = -100;
    // bots: the one being added
    char nbId_[32] = "", nbPw_[32] = "";
    int nbPort_ = 0, nbSaved_ = 0;              // its UDP port (0: the next free one); the saved account picked (0: none)
    bool nbBusy_ = false;
    std::string nbResult_;                      // what the last bot added opened, and what is left to open
    int botMenuN_ = 0;                          // a bot's menu (manage, start / stop, restart, remove)
    bool botMenuOpen_ = false;
    std::string firewall_;                      // the server's own firewall (ufw, firewalld, none)
    double firewallAt_ = -100;
    // the PLAYABLE DOLLs (mgo1host-agent dolls, every 3 s while their page is shown)
    std::vector<DollInfo> dolls_;
    bool dollsKeepSeat_ = true, dollsLoading_ = false, dollsLoaded_ = false;
    std::string dollsList_;                     // (v141 beta 3) TX-55's list of the accountless dolls: host, own, off (empty: an older agent)
    std::string dollsUnranked_;                 // (v141 beta 7) SaveMGO's rankings and the dolls: auto, always, off (empty: an agent before 3.1)
    std::string dollsError_;                    // the server's answer to "dolls" when it failed (an old agent: update the host)
    int dollsMax_ = 16, dollsCap_ = -1;         // the most dolls; the most playing at once (the server's cores and memory)
    bool dollsAccountless_ = false, dcBusy_ = false;   // the server's dolls with no account (DOLL_GATE); a count being set
    std::map<int, int> dollsCount_;             // bot -> its dolls with no account
    std::map<int, int> dollsMost_;              // bot -> the most dolls with no account its game takes (agent 2.6)
    std::map<int, DollWait> dollsWait_;         // (3.2) bot -> why its dolls wait (agent 3.4)
    std::string DollWaitText(int bot) const;    // its line in the program's language ("": none waits, or nothing known)
    double dollsAt_ = -100;
    int dollMenuN_ = 0, dmPlayer_ = 0;          // the doll whose menu is open; the player picked for follow / target
    bool dollMenuOpen_ = false;
    char ndId_[32] = "", ndPw_[32] = "";        // the doll being added
    bool dollsNames_ = false;                   // (v141 beta 5) the server's dolls with no account can be named (agent 2.9)
    bool dollsVdoll_ = false;                   // (3.2) the server takes the difficulty "vdoll" (as the virtual dolls): the choice is offered
    bool dollsAim_ = false;                     // (3.3) the server takes a doll's aim (the agent's "doll_aim": true, 3.5): AIM is shown
    char dmName_[24] = "";                      // the name typed in a doll's menu (dmNameFor_: the doll it was filled for)
    int dmNameFor_ = 0;
    char ndAclName_[24] = "";                   // the name of the next doll with no account ONE MORE adds
    int ndSaved_ = 0, ndBot_ = 0, ndSkill_ = 3, ndTeam_ = 0, ndLoadout_ = 0;
    bool ndAimDirect_ = false;                  // (3.3) the new doll's aim (sent only when the server takes it)
    bool ndBusy_ = false;
    int demoTierLang_ = -1;                     // (the self-test: the language DemoTiers last set)
    // the virtual dolls (mgo1host-agent vdolls, every 3 s while their page is shown)
    std::vector<VdollBot> vdolls_;
    bool vcBusy_ = false;                       // (a live count being set)
    bool vdLoading_ = false, vdLoaded_ = false, vdBusy_ = false, vdOld_ = false;   // vdOld_: an agent without vdolls (before 3.2)
    bool vdControls_ = false;                   // (3.3) the server takes the virtual dolls' controls (the agent's "vdoll_controls": true, 3.5)
    bool vdAdapt_ = false;                      // (3.4) the server takes their live level (the agent's "vdoll_adapt": true, 3.6)
    std::string vdError_;
    double vdAt_ = -100;
    // their form: the bot it holds (0 none yet), one value a doll (15 slots, those past the count kept for a larger one), the
    // server's settings it was filled from
    int vfBot_ = 0, vfCount_ = 0, vfLevel_[15] = {};
    int vfTeam_[15] = {};                       // 0 auto, 1 red, 2 blue
    int vfWeapon_[15] = {};                     // (beta 15) 0 auto, 1 AK-47, 2 XM16E1, 3 SCORPION, 4 M37, 5 SVD
    char vfName_[15][16] = {};
    int vfControls_ = 0;                        // (3.3) 0 DIRECT, 1 AS A PLAYER
    int vfAdapt_ = 0, vfAdaptMin_ = 1, vfAdaptMax_ = 10;   // (3.4) the live level OFF (0) or ON (1), its lowest and highest level
    VdollBot vfFrom_;
    // the network: the game's servers
    NetServers net_, netServer_;
    bool netLoaded_ = false, netLoading_ = false;
    char nWeb_[64] = "", nGate_[40] = "", nStun_[40] = "", nGatePort_[8] = "", nStunPort_[8] = "";
    // the Game Report
    std::map<int, bool> syncBusy_;              // bot -> its events being fetched for the files
    std::map<int, bool> syncMore_;              // bot -> more to fetch at once
    std::map<int, double> syncAgainAt_;         // bot -> lines held back (too recent): fetched again then
    double syncAt_ = -100, cfgSaveAt_ = -100;
    bool cfgDirty_ = false;
    bool reportShownLoaded_ = false;
    int reportFilter_ = 0;
    // the player menu (the game's own: kick, blacklist, whitelist)
    int menuP_ = 0;
    std::string menuName_;
    bool menuOpen_ = false;
    // dialogs, messages
    std::string askText_;
    std::function<void()> askYes_;
    bool askOpen_ = false;
    std::string toast_;
    bool toastErr_ = false;
    double toastAt_ = -100;
    int schedWant_ = -1;                        // the daily restart being chosen (0 off, 1..48 half hours)
    std::string schedSent_;                     // the one sent, until the server reports it
    double schedSentAt_ = -100;
    bool installUpdate_ = false;                // the installation running is an update
    bool installRunning_ = false;               // (seen last frame) a finished one reconnects: the new agent
    double serviceCmdAt_ = -100;                // restart / stop / start sent: those buttons wait a moment
    ImTextureID logoTex_ = 0;
    float logoW_ = 0, logoH_ = 0;
    // the self-test
    bool trustShown_ = false;                   // the host key's box is open
    bool demo_ = false;
    bool demoScrollEnd_ = false;                // the page shown from its end (the GAME page's presets)
    int demoDialog_ = 0, demoBot_ = 1;
    HostStatus demoStatus_;
    InstallProgress demoInstall_;
    // (build 1.00) QUICK SETUP (quick.cpp): its run, the bot's account typed, the disc's folder checked, the self-test's states
    QuickRun qs_;
    char qsId_[32] = "", qsPw_[32] = "";
    std::string qsDiscFor_ = "\x01", qsDiscRoot_;  // the folder last checked (none yet), the disc's root found there
    bool qsDiscOk_ = false;
    bool qsDemo_ = false;                       // (the self-test: a QUICK SETUP state shown)
    InstallProgress qsDemoInstall_;
    unsigned long long qsDemoBuildMs_ = 0;      // (the self-test: the build's time so far)
    // (build 1.00) the showcase (showcase.cpp)
    bool showcase_ = false, showcaseAll_ = false;
    ShowcaseDrawn showcaseDrawn_;
    float showcaseCutTop_ = 0, showcaseCutY_ = 0;
    bool showcaseEnd_ = false;
    void InitShowcase();                        // its made-up host, instead of the layout test's
    void ShowcaseCrashes();                     // its crash reports: none of the last 24 hours
    void ShowcasePage(int page);                // a page's own state over SetDemoPage's
};
