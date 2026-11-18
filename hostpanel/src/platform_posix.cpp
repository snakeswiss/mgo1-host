// platform.h on Linux and macOS: POSIX files and folders, xdg-open or open, zenity, kdialog or
// AppleScript for the file boxes. The secrets: secret_posix.cpp; the sounds: audio_posix.cpp.
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#include "platform.h"
#include <openssl/crypto.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace plat {
namespace {

std::string Home() {
    const char* h = getenv("HOME");
    if (h && *h) return h;
    const passwd* pw = getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : "/tmp";
}

bool MakeDirsMode(const std::string& path, mode_t mode) {
    if (path.empty()) return false;
    for (size_t at = 1; at <= path.size(); ++at) {
        if (at < path.size() && path[at] != '/') continue;
        const std::string p = path.substr(0, at);
        if (mkdir(p.c_str(), mode) != 0 && errno != EEXIST) return false;
    }
    return IsDir(path);
}

bool LowerEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// a program started with these arguments (no shell), its input and error output on /dev/null, its
// output on a pipe when outFd is given; SIGPIPE back to its default for it
pid_t Start(const std::vector<std::string>& args, int* outFd) {
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    int p[2] = {-1, -1};
    if (outFd) {
        if (pipe(p) != 0) return -1;
        fcntl(p[0], F_SETFD, FD_CLOEXEC);
        fcntl(p[1], F_SETFD, FD_CLOEXEC);
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (outFd) posix_spawn_file_actions_adddup2(&fa, p[1], 1);
    else posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigdefault(&at, &def);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF);
    pid_t pid = -1;
    if (posix_spawnp(&pid, argv[0], &fa, &at, argv.data(), environ) != 0) pid = -1;
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    if (outFd) {
        close(p[1]);
        if (pid < 0) { close(p[0]); p[0] = -1; }
        *outFd = p[0];
    }
    return pid;
}

// started and left to run; waited for in the background (no zombie)
void Launch(const std::vector<std::string>& args) {
    const pid_t pid = Start(args, nullptr);
    if (pid > 0) std::thread([pid] { int st; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {} }).detach();
}

bool InPath(const char* name) {
    const char* path = getenv("PATH");
    std::string all = path ? path : "/usr/bin:/bin";
    size_t a = 0;
    while (a <= all.size()) {
        size_t e = all.find(':', a);
        if (e == std::string::npos) e = all.size();
        const std::string d = all.substr(a, e - a);
        if (!d.empty() && access((d + "/" + name).c_str(), X_OK) == 0) return true;
        a = e + 1;
    }
    return false;
}

// the file boxes' state: one at a time (never freed: its thread may still run at exit)
struct PickState {
    std::mutex mu;
    bool busy = false, done = false;
    int id = 0;
    std::string path;
};
PickState& Picks() { static PickState* s = new PickState; return *s; }

#ifdef __APPLE__
std::string AppleQuote(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}
#endif

std::vector<std::string> PickCommand(bool file, const std::string& title) {
#ifdef __APPLE__
    return {"osascript", "-e", std::string("POSIX path of (choose ") + (file ? "file" : "folder") + " with prompt \"" + AppleQuote(title) + "\")"};
#else
    const char* desk = getenv("XDG_CURRENT_DESKTOP");
    const bool kde = desk && strstr(desk, "KDE");
    std::vector<std::string> z{"zenity", "--file-selection", "--title=" + title};
    if (!file) z.push_back("--directory");
    const std::vector<std::string> k{"kdialog", "--title", title, file ? "--getopenfilename" : "--getexistingdirectory", Home()};
    if (kde && InPath("kdialog")) return k;
    if (InPath("zenity")) return z;
    if (InPath("kdialog")) return k;
    return {};
#endif
}

// the fonts' folders: the program's own, the user's, the system's (searched in this order)
std::vector<std::string> FontDirs(const std::string& programDir) {
    const std::string h = Home();
#ifdef __APPLE__
    return {programDir + "/fonts", h + "/Library/Fonts", "/Library/Fonts", "/System/Library/Fonts", "/System/Library/Fonts/Supplemental"};
#else
    std::vector<std::string> d{programDir + "/fonts"};
    const char* xdh = getenv("XDG_DATA_HOME");
    d.push_back(xdh && *xdh == '/' ? std::string(xdh) + "/fonts" : h + "/.local/share/fonts");
    d.push_back(h + "/.fonts");
    const char* xdd = getenv("XDG_DATA_DIRS");
    std::string all = xdd && *xdd ? xdd : "/usr/local/share:/usr/share";
    size_t a = 0;
    while (a <= all.size()) {
        size_t e = all.find(':', a);
        if (e == std::string::npos) e = all.size();
        if (e > a && all[a] == '/') d.push_back(all.substr(a, e - a) + "/fonts");
        a = e + 1;
    }
    return d;
#endif
}

// the font files under a folder and its subfolders (4 levels), by their lower case name; the first
// found kept
void IndexFonts(const std::string& dir, int depth, std::map<std::string, std::string>& idx) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> subs;
    while (const dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string p = dir + "/" + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth < 4) subs.push_back(p); continue; }
        std::string n = e->d_name;
        for (auto& c : n) c = (char)tolower((unsigned char)c);
        idx.emplace(n, p);
    }
    closedir(d);
    std::sort(subs.begin(), subs.end());
    for (const auto& s : subs) IndexFonts(s, depth + 1, idx);
}

}  // namespace

std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

// ---- folders -------------------------------------------------------------------------------------
std::string ConfigDir() {
#ifdef __APPLE__
    const std::string d = Home() + "/Library/Application Support/MGO1 Host";
#else
    const char* x = getenv("XDG_CONFIG_HOME");
    const std::string d = (x && *x == '/' ? std::string(x) : Home() + "/.config") + "/MGO1 Host";
#endif
    MakeDirsMode(d, 0700);
    return d;
}

std::string DocumentsDir() {
    const std::string h = Home();
#ifndef __APPLE__
    // the desktop's own documents folder (XDG user dirs), else ~/Documents
    std::string v;
    if (const char* e = getenv("XDG_DOCUMENTS_DIR")) v = e;
    if (v.empty()) {
        const char* x = getenv("XDG_CONFIG_HOME");
        if (FILE* f = fopen(((x && *x == '/' ? std::string(x) : h + "/.config") + "/user-dirs.dirs").c_str(), "rb")) {
            char line[1024];
            while (fgets(line, sizeof line, f)) {
                std::string l = line;
                if (l.rfind("XDG_DOCUMENTS_DIR=", 0) != 0) continue;
                l = l.substr(18);
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
                if (l.size() >= 2 && l.front() == '"' && l.back() == '"') l = l.substr(1, l.size() - 2);
                if (l.rfind("$HOME", 0) == 0) l = h + l.substr(5);
                v = l;
            }
            fclose(f);
        }
    }
    while (v.size() > 1 && v.back() == '/') v.pop_back();
    if (!v.empty() && v[0] == '/' && v != h) return v;
#endif
    return h + "/Documents";
}

std::string ProgramDir() {
    std::string exe;
#ifdef __APPLE__
    uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string raw(n, 0);
    if (n && _NSGetExecutablePath(&raw[0], &n) == 0) {
        char real[PATH_MAX];
        exe = realpath(raw.c_str(), real) ? real : raw.c_str();
    }
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) exe.assign(buf, (size_t)n);
#endif
    if (exe.empty()) {
        char cwd[PATH_MAX];
        return getcwd(cwd, sizeof cwd) ? cwd : ".";
    }
    std::string dir = exe.substr(0, exe.find_last_of('/'));
    // inside a bundle (MGO1 Host.app/Contents/MacOS): the folder holding the bundle
    const std::string tail = ".app/Contents/MacOS";
    if (dir.size() > tail.size() && dir.compare(dir.size() - tail.size(), tail.size(), tail) == 0) {
        const std::string app = dir.substr(0, dir.size() - std::string("/Contents/MacOS").size());
        dir = app.substr(0, app.find_last_of('/'));
    }
    return dir.empty() ? "/" : dir;
}

// a path shown without the user's own folder: ~/...
std::string HideHome(const std::string& path) {
    std::string h = Home();
    while (h.size() > 1 && h.back() == '/') h.pop_back();
    if (h.size() <= 1 || path.compare(0, h.size(), h) != 0) return path;
    if (path.size() == h.size()) return "~";
    return path[h.size()] == '/' ? "~" + path.substr(h.size()) : path;
}

std::string ExpandHome(const std::string& path) {
    if (path == "~") return Home();
    return path.rfind("~/", 0) == 0 ? Join(Home(), path.substr(2)) : path;
}

// ---- files ---------------------------------------------------------------------------------------
FILE* Open(const std::string& path, const char* mode) { return fopen(path.c_str(), mode); }

FILE* OpenPrivate(const std::string& path, const char* mode) {
    if (mode[0] != 'w') return fopen(path.c_str(), mode);
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return nullptr;
    fchmod(fd, 0600);                                   // (an older file made with wider rights)
    FILE* f = fdopen(fd, mode);
    if (!f) close(fd);
    return f;
}

int Seek(FILE* f, int64_t at, int whence) { return fseeko(f, (off_t)at, whence); }
int64_t Tell(FILE* f) { return (int64_t)ftello(f); }
bool MakeDirs(const std::string& path) { return MakeDirsMode(path, 0755); }
bool IsFile(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
bool IsDir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
uint64_t FileSize(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0; }

std::vector<std::string> List(const std::string& dir, bool dirs) {
    std::vector<std::string> v;
    DIR* d = opendir(dir.c_str());
    if (!d) return v;
    while (const dirent* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        struct stat st;
        if (stat((dir + "/" + e->d_name).c_str(), &st) != 0) continue;
        if (dirs ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode)) v.push_back(e->d_name);
    }
    closedir(d);
    std::sort(v.begin(), v.end());
    return v;
}

std::string Find(const std::string& dir, const std::string& rel) {
    std::string at = dir;
    size_t a = 0;
    while (a < rel.size()) {
        size_t e = rel.find_first_of("/\\", a);
        if (e == std::string::npos) e = rel.size();
        const std::string part = rel.substr(a, e - a);
        a = e + 1;
        if (part.empty()) continue;
        struct stat st;
        if (stat(Join(at, part).c_str(), &st) == 0) { at = Join(at, part); continue; }
        std::string found;
        if (DIR* d = opendir(at.c_str())) {
            while (const dirent* x = readdir(d))
                if (LowerEq(x->d_name, part)) { found = x->d_name; break; }
            closedir(d);
        }
        if (found.empty()) return {};
        at = Join(at, found);
    }
    struct stat st;
    return stat(at.c_str(), &st) == 0 ? at : "";
}

std::string FindFont(const std::string& programDir, const char* name) {
    static std::mutex mu;
    static std::map<std::string, std::map<std::string, std::string>> cache;   // program folder -> lower case name -> path
    std::lock_guard<std::mutex> g(mu);
    auto it = cache.find(programDir);
    if (it == cache.end()) {
        it = cache.emplace(programDir, std::map<std::string, std::string>{}).first;
        for (const auto& d : FontDirs(programDir)) IndexFonts(d, 0, it->second);
    }
    std::string n = name;
    for (auto& c : n) c = (char)tolower((unsigned char)c);
    const auto f = it->second.find(n);
    if (f != it->second.end()) return f->second;
    // (v141 beta 6) the name as asked in each folder: macOS finds a file stored decomposed (NFD: Hiragino's Japanese name)
    // by its composed name, the folder's listing does not
    for (const auto& d : FontDirs(programDir)) {
        struct stat st;
        const std::string p = Join(d, name);
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return p;
    }
    return "";
}

// ---- time ----------------------------------------------------------------------------------------
uint64_t TickMs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
long long NowMs() {
    timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
bool LocalTime(time_t t, struct tm& out) { return localtime_r(&t, &out) != nullptr; }
time_t TimeGm(struct tm* t) { return timegm(t); }

// ---- the desktop ---------------------------------------------------------------------------------
#ifdef __APPLE__
void OpenUrl(const std::string& url) { Launch({"open", url}); }
void OpenFolder(const std::string& path) { Launch({"open", path}); }
void Alert(const std::string& text) {
    fprintf(stderr, "MGO1 Host: %s\n", text.c_str());
    Launch({"osascript", "-e", "display alert \"MGO1 Host\" message \"" + AppleQuote(text) + "\" as critical"});
}
#else
void OpenUrl(const std::string& url) { Launch({"xdg-open", url}); }
void OpenFolder(const std::string& path) { Launch({"xdg-open", path}); }
void Alert(const std::string& text) {
    fprintf(stderr, "MGO1 Host: %s\n", text.c_str());
    if (InPath("zenity")) Launch({"zenity", "--error", "--title=MGO1 Host", "--text=" + text});
    else if (InPath("kdialog")) Launch({"kdialog", "--title", "MGO1 Host", "--error", text});
}
#endif

bool Pick(int id, bool file, const std::string& title) {
    PickState& s = Picks();
    std::lock_guard<std::mutex> g(s.mu);
    if (s.busy) return true;                            // a box is open already
    const std::vector<std::string> cmd = PickCommand(file, title);
    if (cmd.empty()) return false;
    s.busy = true; s.done = false; s.id = id; s.path.clear();
    std::thread([cmd] {
        PickState& s = Picks();
        std::string out;
        int fd = -1;
        const pid_t pid = Start(cmd, &fd);
        if (pid > 0) {
            char buf[4096];
            for (;;) {
                const ssize_t n = read(fd, buf, sizeof buf);
                if (n > 0) { out.append(buf, (size_t)n); continue; }
                if (n < 0 && errno == EINTR) continue;
                break;
            }
            close(fd);
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) out.clear();      // cancelled
        }
        const size_t nl = out.find('\n');
        if (nl != std::string::npos) out.resize(nl);
        while (out.size() > 1 && out.back() == '/') out.pop_back();      // (AppleScript ends a folder with /)
        std::lock_guard<std::mutex> g(s.mu);
        s.path = out;
        s.done = true;
        s.busy = false;
    }).detach();
    return true;
}

bool Picked(int id, std::string& path) {
    PickState& s = Picks();
    std::lock_guard<std::mutex> g(s.mu);
    if (!s.done || s.id != id) return false;
    s.done = false;
    if (s.path.empty()) return false;
    path = s.path;
    s.path.clear();
    return true;
}

void Wipe(void* p, size_t n) { OPENSSL_cleanse(p, n); }

}  // namespace plat
#endif
