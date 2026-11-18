#include "remote.h"
#include "platform.h"
#include <openssl/evp.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>

std::string ShellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

static HostStatus ParseStatus(const Json& j) {
    HostStatus s;
    s.valid = true;
    s.online = j["online"].Text();
    s.reason = j["reason"].Text();
    s.service = j["service"].Text();
    s.enabled = j["enabled"].Truthy();
    s.installed = j["installed"].Truthy();
    s.statusAge = j["status_age"].IsNull() ? -1 : j["status_age"].Dbl();
    s.account = j["account"].Text();
    s.schedule = j["schedule"].Text();
    s.agent = j["agent"].Text();
    const Json& pr = j["proc"];
    s.cpu = pr["cpu"].IsNull() ? -1 : pr["cpu"].Dbl();
    s.rssMb = pr["rss_mb"].Int(-1);
    s.procUptime = pr["uptime_s"].Int(-1);
    const Json& sy = j["sys"];
    s.memTotal = sy["mem_total_mb"].Int();
    s.memAvail = sy["mem_avail_mb"].Int();
    s.diskFree = sy["disk_free_gb"].Dbl();
    s.sysUptime = sy["uptime_s"].Int();
    s.cores = sy["cores"].Int();
    for (size_t i = 0; i < sy["load"].Size(); ++i) s.load.push_back(sy["load"][i].Dbl());
    const Json& h = j["host"];
    if (!h.IsNull()) {
        s.state = h["state"].Text();
        s.lobby = h["lobby"].Int() == 1;
        s.lobbyRx = h["lobby_rx"].Int(-1);
        const Json& g = h["game"];
        s.gameName = g["name"].Text();
        for (size_t i = 0; i < g["rules"].Size(); ++i) s.rules.push_back({g["rules"][i][(size_t)0].Int(), g["rules"][i][(size_t)1].Int()});
        s.round = g["round"].Int(-1);
        s.maxPlayers = g["max"].Int();
        s.gameAge = g["age"].Int(-1);
        for (size_t i = 0; i < h["players"].Size(); ++i) {
            const Json& p = h["players"][i];
            HostPlayer hp;
            hp.p = p["p"].Int(); hp.name = p["name"].Text(); hp.host = p["host"].Int() == 1; hp.state = p["state"].Int();
            hp.team = p["team"].Int(-1); hp.ping = p["ping"].Int(-1); hp.kills = p["k"].Int(); hp.deaths = p["d"].Int();
            hp.points = p["pts"].Int(); hp.kicking = p["kick"].Int() == 1;
            s.players.push_back(hp);
        }
        for (size_t i = 0; i < h["bans"].Size(); ++i) s.black.push_back(h["bans"][i].Text());
        for (size_t i = 0; i < h["kicked"].Size(); ++i)
            s.kicked.push_back({h["kicked"][i]["name"].Text(), h["kicked"][i]["why"].Text(), h["kicked"][i]["ago"].Int()});
    }
    s.bot = j["bot"].Int(1);
    s.port = j["port"].Int();
    s.botsMax = sy["bots_max"].Int();
    for (size_t i = 0; i < j["bots"].Size(); ++i) {
        const Json& b = j["bots"][i];
        BotInfo bi;
        bi.bot = b["bot"].Int(); bi.port = b["port"].Int(); bi.players = b["players"].Int(); bi.max = b["max"].Int();
        bi.account = b["account"].Text(); bi.service = b["service"].Text(); bi.online = b["online"].Text();
        bi.reason = b["reason"].Text(); bi.name = b["name"].Text(); bi.enabled = b["enabled"].Truthy();
        bi.unranked = b["unranked"].Int(-1);
        s.bots.push_back(bi);
    }
    const Json& ls = j["lists"];                 // the lists as their files hold them (agent 1.1)
    if (ls.kind == Json::Obj) {
        s.black.clear();
        for (size_t i = 0; i < ls["black"].Size(); ++i) s.black.push_back(ls["black"][i].Text());
        for (size_t i = 0; i < ls["white"].Size(); ++i) s.white.push_back(ls["white"][i].Text());
        s.whiteOnly = ls["white_only"].Truthy();
    }
    s.receivedAt = plat::TickMs();
    return s;
}

void Remote::Start(const SshConfig& cfg) {
    Stop();
    cfg_ = cfg;
    stop_ = false;
    running_ = true;
    {
        std::lock_guard<std::mutex> g(mu_);
        link_ = Link::Connecting; linkMsg_.clear(); pendingFp_.clear();
        status_ = HostStatus{};
    }
    watch_ = std::thread([this] { WatchLoop(); });
    work_ = std::thread([this] { WorkLoop(); });
}

void Remote::Stop() {
    if (!running_) return;
    stop_ = true;
    cancelInstall_ = true;
    cv_.notify_all();
    if (watch_.joinable()) watch_.join();
    if (work_.joinable()) work_.join();
    running_ = false;
    std::lock_guard<std::mutex> g(mu_);
    link_ = Link::Idle;
    jobs_.clear();
    busy_ = 0;
}

Link Remote::link(std::string* msg) const {
    std::lock_guard<std::mutex> g(mu_);
    if (msg) *msg = linkMsg_;
    return link_;
}
std::string Remote::pendingFingerprint() const { std::lock_guard<std::mutex> g(mu_); return pendingFp_; }
HostStatus Remote::status() const { std::lock_guard<std::mutex> g(mu_); return status_; }
InstallProgress Remote::install() const { std::lock_guard<std::mutex> g(mu_); return inst_; }
bool Remote::Busy() const { std::lock_guard<std::mutex> g(mu_); return busy_ > 0; }

void Remote::SetLink(Link l, const std::string& msg) {
    std::lock_guard<std::mutex> g(mu_);
    link_ = l;
    linkMsg_ = msg;
}

static void SleepStop(const std::atomic<bool>& stop, int ms) {
    for (int t = 0; t < ms && !stop; t += 100) plat::SleepMs(100);
}

void Remote::WatchLoop() {
    Ssh ssh;
    while (!stop_) {
        SetLink(Link::Connecting, "");
        if (!ssh.Open(cfg_, 10000)) {
            const SshErr e = ssh.err();
            if (e == SshErr::HostKeyUnknown || e == SshErr::HostKeyChanged) {
                { std::lock_guard<std::mutex> g(mu_); pendingFp_ = ssh.fingerprint(); }
                SetLink(e == SshErr::HostKeyUnknown ? Link::NeedTrust : Link::KeyChanged, ssh.errText());
                return;                                   // the UI asks, then starts again
            }
            if (e == SshErr::Auth) { SetLink(Link::AuthFailed, ssh.errText()); return; }
            SetLink(e == SshErr::Resolve || e == SshErr::Connect ? Link::Unreachable : Link::Error, ssh.errText());
            SleepStop(stop_, 5000);
            continue;
        }
        std::string out;
        const int rc = ssh.Exec(Prefix() + "sh -c 'command -v mgo1host-agent'", "", out, nullptr, 15000);
        if (rc != 0) {
            if (!ssh.IsOpen()) { SetLink(Link::Error, ssh.errText()); SleepStop(stop_, 3000); continue; }
            SetLink(Link::NoAgent, "");
            { std::lock_guard<std::mutex> g(mu_); status_ = HostStatus{}; }
            SleepStop(stop_, 8000);
            continue;
        }
        SetLink(Link::Connected, "");
        // the bot's state every second, and its Game Report's new events; again for another bot when it changes
        while (!stop_) {
            reWatch_ = false;
            const int bot = bot_;
            ssh.Stream(Prefix() + "mgo1host-agent --bot " + std::to_string(bot) + " watch 1", [this, bot](const std::string& line) {
                Json j;
                if (Json::Parse(line, j) && j["ok"].Truthy()) {
                    HostStatus s = ParseStatus(j);
                    std::lock_guard<std::mutex> g(mu_);
                    if (bot == bot_) status_ = std::move(s);
                    for (size_t i = 0; i < j["events"].Size(); ++i) events_.push_back({bot, j["events"][i]});
                    if (events_.size() > 20000) events_.erase(events_.begin(), events_.begin() + 10000);
                    link_ = Link::Connected;
                }
                return !stop_ && !reWatch_;
            }, stop_, 20000);
            if (!reWatch_ || !ssh.IsOpen()) break;
        }
        if (!stop_) { SetLink(Link::Error, ssh.errText()); SleepStop(stop_, 2000); }
    }
}

void Remote::SetBot(int n) {
    if (n < 1 || n == bot_) return;
    bot_ = n;
    std::lock_guard<std::mutex> g(mu_);
    // the old bot's own state is not this one's: the server's part stays, the new bot's comes
    // from the list of bots until its watch answers (no "not connected" in between)
    const HostStatus& o = status_;
    HostStatus s;
    s.valid = o.valid; s.installed = o.installed; s.schedule = o.schedule; s.agent = o.agent;
    s.memTotal = o.memTotal; s.memAvail = o.memAvail; s.sysUptime = o.sysUptime; s.cores = o.cores;
    s.diskFree = o.diskFree; s.load = o.load; s.botsMax = o.botsMax; s.bots = o.bots; s.black = o.black;
    s.receivedAt = o.receivedAt;
    s.bot = n;
    for (const auto& b : o.bots)
        if (b.bot == n) {
            s.port = b.port; s.online = b.online; s.reason = b.reason; s.service = b.service; s.enabled = b.enabled;
            s.account = b.account; s.gameName = b.name; s.maxPlayers = b.max;
        }
    status_ = std::move(s);
    reWatch_ = true;
}

void Remote::DropBot(int n) {
    std::lock_guard<std::mutex> g(mu_);
    auto& v = status_.bots;
    v.erase(std::remove_if(v.begin(), v.end(), [n](const BotInfo& b) { return b.bot == n; }), v.end());
}

std::vector<std::pair<int, Json>> Remote::TakeEvents() {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::pair<int, Json>> v;
    v.swap(events_);
    return v;
}

void Remote::WorkLoop() {
    Ssh ssh;
    uint64_t lastUse = 0;
    while (!stop_) {
        Job job;
        {
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait_for(g, std::chrono::milliseconds(500), [this] { return stop_ || !jobs_.empty(); });
            if (stop_) break;
            if (jobs_.empty()) continue;
            job = jobs_.front();
            jobs_.pop_front();
        }
        // a link quiet for a minute may be gone without a word (a router's NAT): a new one
        if (ssh.IsOpen() && plat::TickMs() - lastUse > 60000) ssh.Close();
        lastUse = plat::TickMs();
        if (!ssh.IsOpen() && !ssh.Open(cfg_, 10000)) {
            std::lock_guard<std::mutex> g(mu_);
            if (job.install) { inst_.running = false; inst_.done = true; inst_.ok = false; inst_.error = ssh.errText(); }
            else results_.push_back({job.done, -1, "{\"ok\":false,\"error\":" + JsonQuote(ssh.errText()) + "}"});
            --busy_;
            continue;
        }
        if (job.install) {
            DoInstall(ssh, job);
            lastUse = plat::TickMs();
            std::lock_guard<std::mutex> g(mu_);
            --busy_;
            continue;
        }
        std::string out, err;
        int rc = ssh.Exec(job.cmd, job.input, out, &err, 120000);
        // not run (the link was gone): once more on a new link; a command that ran is not run twice
        if (rc == -1 && !ssh.IsOpen() && ssh.Open(cfg_, 10000)) rc = ssh.Exec(job.cmd, job.input, out, &err, 120000);
        lastUse = plat::TickMs();
        if (rc < 0) out = "{\"ok\":false,\"error\":" + JsonQuote(ssh.errText()) + "}";
        else if (out.empty()) out = "{\"ok\":false,\"error\":" + JsonQuote(err.empty() ? "no answer" : err) + "}";
        std::lock_guard<std::mutex> g(mu_);
        results_.push_back({job.done, rc, out});
        --busy_;
    }
}

void Remote::Agent(const std::string& args, const std::string& input, Done done, int bot) {
    std::lock_guard<std::mutex> g(mu_);
    // (beta 16) bot < 0: no --bot, a command of the whole server (crashes: every bot's and every doll's reports)
    const std::string which = bot < 0 ? std::string() : "--bot " + std::to_string(bot > 0 ? bot : (int)bot_) + " ";
    jobs_.push_back({Prefix() + "mgo1host-agent " + which + args, input, std::move(done)});
    ++busy_;
    cv_.notify_all();
}

void Remote::Pump() {
    std::deque<Result> rs;
    {
        std::lock_guard<std::mutex> g(mu_);
        rs.swap(results_);
    }
    for (auto& r : rs) {
        if (!r.done) continue;
        Json j;
        const size_t nl = r.raw.find('\n');
        if (!Json::Parse(nl == std::string::npos ? r.raw : r.raw.substr(0, nl), j)) {
            // not the agent's answer: its last line (a shell's or Python's error), as a message
            std::string last;
            for (size_t a = 0; a < r.raw.size();) {
                size_t e = r.raw.find('\n', a); if (e == std::string::npos) e = r.raw.size();
                std::string l = r.raw.substr(a, e - a);
                while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
                if (!l.empty()) last = l;
                a = e + 1;
            }
            for (auto& c : last) if (c == '|') c = ' ';
            if (!last.empty() && last[0] == '@') last = " " + last;
            j = Json{};
            j.kind = Json::Obj;
            j.o["ok"].kind = Json::Bool;
            j.o["error"].kind = Json::Str;
            j.o["error"].s = last.empty() ? "@ae_noanswer" : "@ae_answer|" + last.substr(0, 200);
        }
        r.done(r.rc, j, r.raw);
    }
}

// ---- installation --------------------------------------------------------------------------
void Remote::Install(const std::string& serverDir, const std::string& discDir) {
    std::lock_guard<std::mutex> g(mu_);
    inst_ = InstallProgress{};
    inst_.running = true;
    inst_.phase = "check";
    cancelInstall_ = false;
    Job j;
    j.install = true; j.serverDir = serverDir; j.discDir = discDir;
    jobs_.push_back(j);
    ++busy_;
    cv_.notify_all();
}

void Remote::Log(const std::string& line) {
    std::lock_guard<std::mutex> g(mu_);
    inst_.lines.push_back(line);
    if (inst_.lines.size() > 400) inst_.lines.erase(inst_.lines.begin());
}

// a path shown without the user's own folder: %USERPROFILE%\... (Windows), ~/... (Linux, macOS)
std::string HideProfile(const std::string& path) { return plat::HideHome(path); }

static std::string Sha256File(const std::string& p, const std::atomic<bool>& cancel) {
    FILE* f = plat::Open(p, "rb");
    if (!f) return {};
    EVP_MD_CTX* c = EVP_MD_CTX_new();
    EVP_DigestInit_ex(c, EVP_sha256(), nullptr);
    std::vector<unsigned char> buf(1 << 20);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0 && !cancel) EVP_DigestUpdate(c, buf.data(), n);
    fclose(f);
    unsigned char md[32]; unsigned len = 0;
    EVP_DigestFinal_ex(c, md, &len);
    EVP_MD_CTX_free(c);
    std::string hex;
    char b[3];
    for (unsigned i = 0; i < len; ++i) { snprintf(b, sizeof b, "%02x", md[i]); hex += b; }
    return hex;
}

// a disc file's path under the disc's folder (the names in any letter case: a disc mounted on Linux)
static std::string DiscFile(const std::string& disc, const std::string& rel) { return plat::Find(disc, rel); }

// the disc's files in the folder given, or one or two levels below it (a mounted ISO, MGO1_PC\ps2\US)
bool FindDisc(const std::string& dir, std::string& root) {
    std::vector<std::string> cands{plat::ExpandHome(dir)};
    for (int depth = 0; depth < 3 && !cands.empty(); ++depth) {
        std::vector<std::string> next;
        for (const auto& d : cands) {
            if (plat::IsFile(DiscFile(d, "MGS/NET/MGS3_N.ELF")) && plat::IsFile(DiscFile(d, "MGS/NET/STAGE_N.DAT")) &&
                plat::IsFile(DiscFile(d, "MGS/NET/SLOT_N.DAT")) && plat::IsDir(DiscFile(d, "MODULES"))) { root = d; return true; }
            for (const auto& n : plat::List(d, true)) next.push_back(plat::Join(d, n));
        }
        cands.swap(next);
    }
    return false;
}

// (v141) the PC maps' folder (maps 13 and up, docs/EXTRA_MAPS.md: maps\<map>\map.ini next to MGO1.exe): in the disc's
// folder or up to two levels above it (MGO1_PC\ps2\US: MGO1_PC\maps); empty when none
static std::string FindMaps(const std::string& disc) {
    std::string d = disc;
    for (int up = 0; up < 3 && !d.empty(); ++up) {
        const std::string m = plat::Find(d, "maps");
        if (plat::IsDir(m))
            for (const auto& n : plat::List(m, true)) if (plat::IsFile(plat::Join(m, n, "map.ini"))) return m;
        const size_t k = d.find_last_of("/\\");
        if (k == std::string::npos || k == 0) break;
        d = d.substr(0, k);
    }
    return std::string();
}
// every file under a folder, its path relative to it with '/' (the maps' rules\ and sounds\ folders too)
static void ListTree(const std::string& dir, const std::string& rel, std::vector<std::string>& out, int depth = 0) {
    if (depth > 4) return;
    for (const auto& n : plat::List(dir, false)) out.push_back(rel.empty() ? n : rel + "/" + n);
    for (const auto& n : plat::List(dir, true)) ListTree(plat::Join(dir, n), rel.empty() ? n : rel + "/" + n, out, depth + 1);
}

void Remote::DoInstall(Ssh& ssh, const Job& job) {
    auto phase = [&](const char* p) { std::lock_guard<std::mutex> g(mu_); inst_.phase = p; };
    auto fail = [&](const std::string& e) {
        std::lock_guard<std::mutex> g(mu_);
        inst_.running = false; inst_.done = true; inst_.ok = false; inst_.error = e;
    };
    // 1. the server
    phase("check");
    std::string out;
    int rc = ssh.Exec(Prefix() + "sh -c 'uname -m; id -u; . /etc/os-release; echo \"$ID $VERSION_ID\"; df -Pk /root /tmp | tail -1 | awk \"{print \\$4}\"'", "", out, nullptr, 20000);
    if (rc != 0) { fail("@ie_checks|" + ssh.errText()); return; }
    std::vector<std::string> v;
    { size_t a = 0; while (a < out.size()) { size_t e = out.find('\n', a); if (e == std::string::npos) e = out.size(); v.push_back(out.substr(a, e - a)); a = e + 1; } }
    if (v.size() < 4) { fail("@ie_answer"); return; }
    Log("@il_server|" + v[0] + "|" + v[2] + "|" + v[1] + "|" + std::to_string(atoll(v[3].c_str()) / 1024));
    if (v[0] != "x86_64") { fail("@ie_arch|" + v[0]); return; }
    if (v[1] != "0") { fail("@ie_root"); return; }
    if (v[2].rfind("ubuntu", 0) != 0 && v[2].rfind("debian", 0) != 0) Log("@il_os_warn|" + v[2]);
    if (atoll(v[3].c_str()) < 2LL * 1024 * 1024) { fail("@ie_space|" + std::to_string(atoll(v[3].c_str()) / 1024) + "|2048"); return; }   // (build 1.00: MB free, needed)
    // 2. the files
    phase("files");
    struct Up { std::string local; std::string remote; int mode; };
    std::vector<Up> ups;
    const char* pkg[] = {"mgo1host.sh", "mgo1host-agent", "mgo1hostctl", "mgo1host@.service", "install.sh", "bot.env.example",
                         "server.env.example", "game.ini.example", "README.md", "VERSION", "steps/login_account.txt", "steps/host_create.txt",
                         "mgo1doll.sh", "mgo1doll-manager", "mgo1doll@.service", "mgo1doll-manager.service", "doll.env.example", "steps/doll_join.txt",
                         "mgo1log", "mgo1-dollgate"};
    const std::string stage = cfg_.user == "root" ? "/root/mgo1host-install" : "/tmp/mgo1host-install";
    for (const char* f : pkg) {
        std::string rel = f;
        for (auto& ch : rel) if (ch == '/') ch = plat::kSep;
        const std::string lp = plat::Join(job.serverDir, rel);
        if (!plat::IsFile(lp)) { fail(std::string("@ie_missing|") + f); return; }
        const std::string n = f;
        ups.push_back({lp, stage + "/" + n, (n.find(".sh") != std::string::npos || n == "mgo1host-agent" || n == "mgo1hostctl" || n == "mgo1doll-manager" || n == "mgo1log" || n == "mgo1-dollgate") ? 0755 : 0644});
    }
    // the host program: mgo1host.exe.gz and its SHA-256 (mgo1host.exe.sha256), or mgo1host.exe as it is; (build 1.00) neither but
    // the sources, mgo1host-src.tar.gz (the public package holds no game code): the server builds it from them and the disc's files
    // sent below (install.sh builds when the stage has no exe and has the archive; its @is_build marks)
    const std::string exe = plat::Join(job.serverDir, "mgo1host.exe"), exeGz = exe + ".gz";
    const bool gz = !plat::IsFile(exe) && plat::IsFile(exeGz);
    const bool noExe = !plat::IsFile(exe) && !gz;
    if (noExe && !plat::IsFile(plat::Join(job.serverDir, "mgo1host-src.tar.gz"))) { fail("@ie_missing|mgo1host.exe.gz"); return; }
    if (noExe) { { std::lock_guard<std::mutex> g(mu_); inst_.noExe = true; } Log("@il_build_here"); }
    // (build 1.00) the server folder's other files (mgo1host-src.tar.gz, whatever else the package holds there) go next to install.sh,
    // in their own folders; the sources only when there is no host program to send
    std::vector<std::string> extra, extraDirs;
    {
        std::vector<std::string> all;
        ListTree(job.serverDir, "", all);
        for (const auto& f : all) {
            bool known = f == "mgo1host.exe" || f == "mgo1host.exe.gz" || f == "mgo1host.exe.sha256" || (!noExe && f == "mgo1host-src.tar.gz");
            for (const char* k : pkg) known = known || f == k;
            if (known) continue;
            extra.push_back(f);
            const size_t k = f.find_last_of('/');
            if (k != std::string::npos && std::find(extraDirs.begin(), extraDirs.end(), f.substr(0, k)) == extraDirs.end()) extraDirs.push_back(f.substr(0, k));
        }
    }
    for (const auto& f : extra) {
        std::string local = job.serverDir;
        size_t a2 = 0;
        while (a2 <= f.size()) { size_t e = f.find('/', a2); if (e == std::string::npos) e = f.size(); local = plat::Join(local, f.substr(a2, e - a2)); a2 = e + 1; }
        const bool script = f.size() > 3 && f.compare(f.size() - 3, 3, ".sh") == 0;
        ups.push_back({local, stage + "/" + f, script ? 0755 : 0644});
    }
    std::string exeHash;
    if (gz) {
        FILE* f = plat::Open(exe + ".sha256", "rb");
        char b[80] = "";
        if (f) { b[fread(b, 1, 64, f)] = 0; fclose(f); }
        exeHash.assign(b, strnlen(b, 64));
        if (exeHash.size() != 64) { fail("@ie_missing|mgo1host.exe.sha256"); return; }
    }
    std::string disc;
    if (!FindDisc(job.discDir, disc)) { fail("@ie_disc"); return; }
    Log("@il_disc|" + HideProfile(disc));                    // no user name in what is shown
    const std::string maps = FindMaps(disc);
    std::vector<std::string> mapFiles;
    if (!maps.empty()) ListTree(maps, "", mapFiles);
    if (mapFiles.empty()) Log("@il_no_maps"); else Log("@il_maps|" + HideProfile(maps) + "|" + std::to_string(mapFiles.size()));
    // what the server has already
    phase("compare");
    out.clear();
    ssh.Exec(Prefix() + "sh -c 'cd /opt/mgo1host 2>/dev/null && sha256sum mgo1host.exe disc/MGS/NET/MGS3_N.ELF disc/MGS/NET/STAGE_N.DAT disc/MGS/NET/SLOT_N.DAT 2>/dev/null; true'", "", out, nullptr, 120000);
    std::map<std::string, std::string> remoteHash;
    { size_t a = 0; while (a < out.size()) { size_t e = out.find('\n', a); if (e == std::string::npos) e = out.size();
        const std::string l = out.substr(a, e - a); const size_t sp = l.find("  "); if (sp != std::string::npos) remoteHash[l.substr(sp + 2)] = l.substr(0, sp); a = e + 1; } }
    bool sameExe = false, sameDisc = true;
    if (!gz && !noExe) exeHash = Sha256File(exe, cancelInstall_);
    if (cancelInstall_) { fail("@se_cancelled"); return; }
    sameExe = !noExe && !exeHash.empty() && remoteHash["mgo1host.exe"] == exeHash;
    const char* discFiles[] = {"MGS3_N.ELF", "STAGE_N.DAT", "SLOT_N.DAT"};
    for (const char* f : discFiles) {
        const std::string h = Sha256File(DiscFile(disc, std::string("MGS/NET/") + f), cancelInstall_);
        if (cancelInstall_) { fail("@se_cancelled"); return; }
        if (h.empty() || remoteHash[std::string("disc/MGS/NET/") + f] != h) sameDisc = false;
    }
    if (noExe) {}                                       // (built on the server)
    else if (!sameExe) ups.push_back({gz ? exeGz : exe, stage + (gz ? "/mgo1host.exe.gz" : "/mgo1host.exe"), 0644});
    else Log("@il_exe_same");
    if (!sameDisc) {
        for (const char* f : discFiles) ups.push_back({DiscFile(disc, std::string("MGS/NET/") + f), stage + "/disc/MGS/NET/" + f, 0644});
        const std::string modules = DiscFile(disc, "MODULES");
        for (std::string n : plat::List(modules, false)) {
            const std::string local = plat::Join(modules, n);
#ifndef _WIN32
            for (auto& c : n) c = (char)toupper((unsigned char)c);       // the disc's own names (a Linux mount shows lower case)
#endif
            ups.push_back({local, stage + "/disc/MODULES/" + n, 0644});
        }
    } else Log("@il_disc_same");
    std::vector<std::string> mapDirs;                      // (the maps' folders on the server, made before the upload)
    for (const auto& f : mapFiles) {
        std::string local = maps;
        size_t a2 = 0;
        while (a2 <= f.size()) { size_t e = f.find('/', a2); if (e == std::string::npos) e = f.size(); local = plat::Join(local, f.substr(a2, e - a2)); a2 = e + 1; }
        ups.push_back({local, stage + "/maps/" + f, 0644});
        const size_t k = f.find_last_of('/');
        if (k != std::string::npos && std::find(mapDirs.begin(), mapDirs.end(), f.substr(0, k)) == mapDirs.end()) mapDirs.push_back(f.substr(0, k));
    }
    unsigned long long total = 0;
    for (const auto& u : ups) total += plat::FileSize(u.local);
    { std::lock_guard<std::mutex> g(mu_); inst_.allTotal = total; inst_.allDone = 0; }
    // 3. the upload
    phase("upload");
    std::string mk;
    for (const auto& d : mapDirs) mk += " " + ShellQuote(stage + "/maps/" + d);
    for (const auto& d : extraDirs) mk += " " + ShellQuote(stage + "/" + d);
    if (!mapFiles.empty()) mk += " " + stage + "/maps";
    rc = ssh.Exec(Prefix() + "sh -c " + ShellQuote("rm -rf " + stage + " && mkdir -p " + stage + "/steps " + stage + "/disc/MGS/NET " + stage + "/disc/MODULES" + mk +
                                                    (sameExe ? " && cp /opt/mgo1host/mgo1host.exe " + stage + "/" : "")), "", out, nullptr, 60000);
    if (rc != 0) { fail("@ie_prepare|" + stage); return; }
    unsigned long long base = 0;
    for (const auto& u : ups) {
        const unsigned long long size = plat::FileSize(u.local);
        { std::lock_guard<std::mutex> g(mu_); inst_.file = u.remote.substr(stage.size() + 1); inst_.fileDone = 0; inst_.fileTotal = size; }
        const bool ok = ssh.Upload(u.local, u.remote, u.mode, [&](unsigned long long d, unsigned long long t) {
            std::lock_guard<std::mutex> g(mu_); inst_.fileDone = d; inst_.fileTotal = t; inst_.allDone = base + d;
        }, cancelInstall_);
        if (!ok) { fail(cancelInstall_ ? std::string("@se_cancelled") : "@ie_upload|" + ssh.errText()); return; }
        base += size;
    }
    if (total < 1024 * 1024) Log("@il_sent_kb|" + std::to_string(ups.size()) + "|" + std::to_string((total + 1023) / 1024));
    else Log("@il_sent|" + std::to_string(ups.size()) + "|" + std::to_string(total / (1024 * 1024)));
    // 4. install.sh, its output live
    phase("install");
    if (gz && !sameExe) {
        // unpacked, then checked by a command of its own: the file's SHA-256 is the proof
        auto first = [](const std::string& s) { const size_t e = s.find('\n'); return s.substr(0, std::min<size_t>(e == std::string::npos ? s.size() : e, 160)); };
        std::string err;
        rc = ssh.Exec(Prefix() + "sh -c " + ShellQuote("cd " + stage + " && gzip -dc mgo1host.exe.gz > mgo1host.exe && rm -f mgo1host.exe.gz"),
                      "", out, &err, 600000);
        if (rc != 0) Log("@il_gzip|" + std::to_string(rc) + "|" + first(err.empty() ? ssh.errText() : err));
        if (!ssh.IsOpen() && !ssh.Open(cfg_, 10000)) { fail("@ie_lost|" + ssh.errText()); return; }
        rc = ssh.Exec(Prefix() + "sh -c " + ShellQuote("cd " + stage + " && sha256sum mgo1host.exe"), "", out, &err, 300000);
        if (rc != 0 || out.compare(0, 64, exeHash) != 0) {
            Log("@il_sha|" + std::to_string(rc) + "|" + first(out.empty() ? (err.empty() ? ssh.errText() : err) : out));
            fail("@ie_damaged");
            return;
        }
        Log("@il_unpacked");
    }
    int exitCode = -1;
    std::string why;                                   // install.sh's last error ("@ie_...")
    // (build 1.00) the host built on the server takes 20 to 60 minutes: its marks (at least one a minute) show on the pages, the
    // link kept alive (Stream's keepalives every 15 s), given up only after 60 minutes with no line at all
    const bool streamed = ssh.Stream(Prefix() + "env MGO1_MARKS=1 bash " + stage + "/install.sh 2>&1; echo MGO1HOST_INSTALL_EXIT=$?", [&](const std::string& line) {
        if (line.rfind("MGO1HOST_INSTALL_EXIT=", 0) == 0) { exitCode = atoi(line.c_str() + 22); return false; }
        if (line.rfind("@ie_", 0) == 0) why = line;
        if (line.rfind("@is_build_pct|", 0) == 0) {          // the build's percent: shown, not logged (one a minute or more)
            std::lock_guard<std::mutex> g(mu_);
            inst_.buildPct = std::max(0, std::min(100, atoi(line.c_str() + 14)));
            return !cancelInstall_;
        }
        if (line.rfind("@is_build|", 0) == 0) {              // a stage of the build: the phase "build" until "done"
            std::string st = line.substr(10);
            while (!st.empty() && (st.back() == '\r' || st.back() == ' ')) st.pop_back();
            {
                std::lock_guard<std::mutex> g(mu_);
                if (!inst_.buildSince) inst_.buildSince = plat::TickMs();
                if (st == "done") inst_.buildPct = 100;              // (the percent is the whole build's: kept across the stages)
                inst_.buildStage = st;
                inst_.phase = st == "done" ? "install" : "build";
            }
            const bool known = st == "deps" || st == "gen" || st == "compile" || st == "link" || st == "done";
            Log(known ? "@is_build|@bs_" + st : line);
            return !cancelInstall_;
        }
        Log(line);
        return !cancelInstall_;
    }, cancelInstall_, 3600000);
    (void)streamed;
    if (exitCode != 0) { fail(exitCode < 0 ? "@ie_unfinished|" + ssh.errText() : !why.empty() ? why : "@ie_failed|" + std::to_string(exitCode)); return; }
    if (!ssh.IsOpen()) ssh.Open(cfg_, 10000);
    ssh.Exec(Prefix() + "rm -rf " + stage, "", out, nullptr, 60000);
    std::lock_guard<std::mutex> g(mu_);
    inst_.running = false; inst_.done = true; inst_.ok = true; inst_.phase = "done";
}
