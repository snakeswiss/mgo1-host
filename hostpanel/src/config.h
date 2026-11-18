// The program's settings (config.ini in plat::ConfigDir: %APPDATA%\MGO1 Host on Windows): the servers
// and the SaveMGO accounts it remembers, their passwords kept encrypted (plat::Protect: DPAPI for the
// Windows user; AES-256-GCM with a key only the user can read on Linux and macOS), the game settings it
// keeps (the last ones saved to a bot, the presets; their password locks encrypted the same way), and
// where the Game Report of each bot has been written on this PC.
#pragma once
#include <map>
#include <string>
#include <vector>

struct ServerEntry {                 // a server the program connects to (SSH)
    std::string label;               // a name of the owner's, else the address
    std::string host;
    int port = 22;
    std::string user = "root";
    bool useKey = false;
    std::string keyPath;
    bool remember = true;
    std::string password;            // clear in memory only
    int bot = 1;                     // the bot managed last
    std::string Key() const { return host + ":" + std::to_string(port); }
    std::string Name() const { return label.empty() ? host : label; }
};

struct AccountEntry {                // a SaveMGO account kept for the bots
    std::string id;
    std::string password;            // clear in memory only
};

struct GamePreset {                  // game settings kept on this PC (the GAME page)
    std::string name;                // a preset's name (the last settings saved: none)
    std::string settings;            // the settings as JSON (GameSettings::ToJson), the password lock left out
    std::string password;            // the password lock: clear in memory only
};

struct Config {
    std::string language = "en";
    float scale = 1.0f;
    std::vector<ServerEntry> servers;
    int current = -1;                // the server connected last (index in servers)
    std::vector<AccountEntry> accounts;
    std::string fingerprints;        // "host:port=SHA256:...;..."
    std::string discDir;
    bool sound = true;               // the game's menu sounds on the clicks (rendered from the disc: sound.h)
    int soundVolume = 60;            // 0..100
    std::string lastPage;
    std::string reportDir;           // the Game Report's folder on this PC (empty: <Documents>/MGO1 Host/Game Report)
    bool reportPackets = true;       // the Game Report page shows the packets and messages under each line
    int reportFilter = 0;
    std::map<std::string, long long> reportAt;   // "host:port#bot" -> the last event written to this PC's files (ms)
    GamePreset lastGame;             // the last game settings saved to a bot (settings empty: none yet); a new bot gets them
    std::vector<GamePreset> presets; // named game settings, kMaxPresets at most
    static constexpr int kMaxPresets = 20;

    void Load();
    void Save() const;
    std::string TrustedFp(const std::string& host, int port) const;
    void Trust(const std::string& host, int port, const std::string& fp);
    ServerEntry* Current() { return current >= 0 && current < (int)servers.size() ? &servers[current] : nullptr; }
    int FindServer(const std::string& host, int port) const;
    void KeepAccount(const std::string& id, const std::string& password);
    void ForgetAccount(const std::string& id);
    int FindPreset(const std::string& name) const;   // its index (the name in any case), -1: none
    std::string ReportDir() const;
    static std::string Dir();
    static std::string DocumentsDir();           // the user's Documents
};
