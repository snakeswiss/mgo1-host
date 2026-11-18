// The server seen from the program: the host's live state (mgo1host-agent watch, its own SSH
// connection) and the commands (a worker thread, its own SSH connection; results handed back to
// the UI thread by Pump).
#pragma once
#include "json.h"
#include "ssh.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// the disc's files (MGS/NET/STAGE_N.DAT ...) in the folder given, or one or two levels below it
bool FindDisc(const std::string& dir, std::string& root);

struct HostPlayer {
    int p = 0;
    std::string name;
    bool host = false;
    int state = 0, team = -1, ping = -1, kills = 0, deaths = 0, points = 0;
    bool kicking = false;
};

struct HostKick { std::string name, why; int ago = 0; };

struct BotInfo {                                 // one of the server's bots (mgo1host-agent bots)
    int bot = 0, port = 0, players = 0, max = 0;
    std::string account, service, online = "offline", reason, name;
    bool enabled = false;
    int unranked = -1;                           // (v141 beta 7, agent 3.1) its game now out of SaveMGO's rankings: 1, 0; -1 not known
};

struct HostStatus {
    bool valid = false;
    std::string online = "offline", reason, service, account, schedule = "off", agent, state;
    bool enabled = false, installed = false, lobby = false;
    double statusAge = -1;
    int lobbyRx = -1;
    std::string gameName;
    std::vector<std::pair<int, int>> rules;
    int round = -1, maxPlayers = 0, gameAge = -1;
    std::vector<HostPlayer> players;
    std::vector<std::string> black, white;      // the blacklist, the whitelist
    bool whiteOnly = false;                      // only the whitelisted names may play
    std::vector<HostKick> kicked;
    double cpu = -1;
    int rssMb = -1, procUptime = -1;
    int memTotal = 0, memAvail = 0, sysUptime = 0, cores = 0;
    double diskFree = 0;
    std::vector<double> load;
    int bot = 1, port = 0, botsMax = 0;          // the bot watched, its UDP port; about how many bots the server runs
    std::vector<BotInfo> bots;                   // every bot of the server
    unsigned long long receivedAt = 0;
};

enum class Link { Idle, Connecting, Connected, NeedTrust, KeyChanged, AuthFailed, Unreachable, NoAgent, Error };

struct InstallProgress {
    bool running = false, done = false, ok = false;
    std::string phase, file;
    unsigned long long fileDone = 0, fileTotal = 0, allDone = 0, allTotal = 0;
    std::vector<std::string> lines;
    std::string error;
    // (build 1.00) a package with no host program: the server builds it (install.sh's marks, MGO1_MARKS=1): `@is_build|<stage>`
    // (deps, gen, compile, link, done) and `@is_build_pct|<0..100>`; phase "build" meanwhile
    std::string buildStage;                     // "" none yet
    int buildPct = -1;                          // -1 not said yet
    unsigned long long buildSince = 0;          // plat::TickMs() at the first stage's mark (0: no build)
    bool noExe = false;                         // the package had no host program (the server builds it)
};

class Remote {
public:
    using Done = std::function<void(int rc, const Json& j, const std::string& raw)>;
    ~Remote() { Stop(); }
    void Start(const SshConfig& cfg);
    void Stop();
    bool Running() const { return running_; }
    Link link(std::string* msg = nullptr) const;
    std::string pendingFingerprint() const;
    HostStatus status() const;
    const SshConfig& config() const { return cfg_; }
    // the bot the pages show and command (mgo1host-agent --bot N); the watch follows it
    void SetBot(int n);
    int Bot() const { return bot_; }
    // a bot just removed: out of the list at once (the next state says so too)
    void DropBot(int n);
    // the Game Report's new events of the bot watched, as the agent's watch sends them
    std::vector<std::pair<int, Json>> TakeEvents();

    // runs "mgo1host-agent --bot <bot> <args>" (stdin: input); done() runs on the UI thread (Pump)
    void Agent(const std::string& args, const std::string& input, Done done, int bot = 0);
    // installs or updates the host from the program's server folder and the disc folder
    void Install(const std::string& serverDir, const std::string& discDir);
    void CancelInstall() { cancelInstall_ = true; }
    InstallProgress install() const;
    void Pump();
    bool Busy() const;

private:
    struct Job { std::string cmd, input; Done done; bool install = false; std::string serverDir, discDir; };
    struct Result { Done done; int rc; std::string raw; };
    void WatchLoop();
    void WorkLoop();
    void DoInstall(Ssh& ssh, const Job& job);
    void SetLink(Link l, const std::string& msg);
    void Log(const std::string& line);
    std::string Prefix() const { return cfg_.user == "root" ? "" : "sudo -n "; }

    SshConfig cfg_;
    std::atomic<bool> running_{false}, stop_{false}, cancelInstall_{false};
    std::thread watch_, work_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    Link link_ = Link::Idle;
    std::string linkMsg_, pendingFp_;
    HostStatus status_;
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    InstallProgress inst_;
    int busy_ = 0;
    std::atomic<int> bot_{1};
    std::atomic<bool> reWatch_{false};              // the bot changed: the watch starts again for it
    std::vector<std::pair<int, Json>> events_;
};

std::string ShellQuote(const std::string& s);
std::string HideProfile(const std::string& path);      // %USERPROFILE%\... or ~/... instead of the user's folder
