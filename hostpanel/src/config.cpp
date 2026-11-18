#include "config.h"
#include "platform.h"
#include <algorithm>
#include <cstdio>
#include <vector>

std::string Config::Dir() { return plat::ConfigDir(); }
std::string Config::DocumentsDir() { return plat::DocumentsDir(); }

std::string Config::ReportDir() const {
    if (!reportDir.empty()) return reportDir;
    return plat::Join(DocumentsDir(), "MGO1 Host", "Game Report");
}

using plat::Protect;
using plat::Unprotect;

// "a|b|c" -> {a, b, c} (the values never hold '|': addresses, names, paths)
static std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> v;
    size_t a = 0;
    while (true) {
        const size_t e = s.find('|', a);
        v.push_back(s.substr(a, e == std::string::npos ? std::string::npos : e - a));
        if (e == std::string::npos) break;
        a = e + 1;
    }
    return v;
}

void Config::Load() {
    FILE* f = plat::Open(plat::Join(Dir(), "config.ini"), "rb");
    if (!f) return;
    char line[8192];
    std::map<std::string, std::string> kv;
    std::vector<std::pair<std::string, std::string>> multi;          // server.N, account.N, report.K, preset.N.field
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const size_t eq = l.find('=');
        if (eq == std::string::npos || l[0] == '#') continue;
        const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k.rfind("server.", 0) == 0 || k.rfind("account.", 0) == 0 || k.rfind("report.", 0) == 0 || k.rfind("preset.", 0) == 0)
            multi.push_back({k, v});
        else kv[k] = v;
    }
    fclose(f);
    auto get = [&](const char* k, const std::string& d) { auto it = kv.find(k); return it == kv.end() ? d : it->second; };
    language = get("language", language);
    scale = (float)atof(get("scale", "1.0").c_str());
    if (scale < 0.75f || scale > 2.5f) scale = 1.0f;
    fingerprints = get("fingerprints", "");
    discDir = get("disc", "");
    sound = get("sound", "1") == "1";
    soundVolume = std::clamp(atoi(get("sound_volume", "60").c_str()), 0, 100);
    lastPage = get("page", "");
    reportDir = get("report_dir", "");
    reportPackets = get("report_packets", "1") == "1";
    reportFilter = atoi(get("report_filter", "0").c_str());
    current = atoi(get("current", "-1").c_str());
    std::map<int, GamePreset> preset;                                 // preset.<n>.name / settings / password
    for (const auto& [k, v] : multi) {
        if (k.rfind("preset.", 0) == 0) {
            const size_t dot = k.find('.', 7);
            if (dot == std::string::npos) continue;
            GamePreset& g = preset[atoi(k.substr(7, dot - 7).c_str())];
            const std::string field = k.substr(dot + 1);
            if (field == "name") g.name = v;
            else if (field == "settings") g.settings = v;
            else if (field == "password") g.password = Unprotect(v);
            continue;
        }
        const std::vector<std::string> p = Split(v);
        if (k.rfind("server.", 0) == 0 && p.size() >= 9) {
            ServerEntry s;
            s.label = p[0]; s.host = p[1]; s.port = atoi(p[2].c_str()); s.user = p[3]; s.useKey = p[4] == "key"; s.keyPath = p[5];
            s.remember = p[6] == "1"; s.password = s.remember && !s.useKey ? Unprotect(p[7]) : ""; s.bot = std::max(1, atoi(p[8].c_str()));
            if (s.port <= 0 || s.port > 65535) s.port = 22;
            if (!s.host.empty()) servers.push_back(s);
        } else if (k.rfind("account.", 0) == 0 && p.size() >= 2 && !p[0].empty()) {
            // the ID encrypted too ("dp:"): an older file keeps it in the clear
            const std::string id = p[0].rfind("dp:", 0) == 0 ? Unprotect(p[0].substr(3)) : p[0];
            if (!id.empty()) accounts.push_back({id, Unprotect(p[1])});
        } else if (k.rfind("report.", 0) == 0) {
            reportAt[k.substr(7)] = atoll(v.c_str());
        }
    }
    for (const auto& [n, g] : preset)
        if (!g.name.empty() && !g.settings.empty() && (int)presets.size() < kMaxPresets && FindPreset(g.name) < 0) presets.push_back(g);
    lastGame.settings = get("game_last.settings", "");
    lastGame.password = lastGame.settings.empty() ? "" : Unprotect(get("game_last.password", ""));
    // the settings of the one-server program (before the bots): its server becomes the first of the list
    if (servers.empty() && !get("host", "").empty()) {
        ServerEntry s;
        s.host = get("host", ""); s.port = atoi(get("port", "22").c_str()); s.user = get("user", "root");
        s.useKey = get("auth", "password") == "key"; s.keyPath = get("key", ""); s.remember = get("remember", "1") == "1";
        s.password = s.remember ? Unprotect(get("password", "")) : "";
        if (s.port <= 0 || s.port > 65535) s.port = 22;
        servers.push_back(s);
        current = 0;
    }
    if (current >= (int)servers.size()) current = servers.empty() ? -1 : 0;
}

void Config::Save() const {
    FILE* f = plat::OpenPrivate(plat::Join(Dir(), "config.ini"), "wb");
    if (!f) return;
    fprintf(f, "# MGO1 Host\nlanguage=%s\nscale=%.2f\ncurrent=%d\nfingerprints=%s\ndisc=%s\nsound=%d\nsound_volume=%d\npage=%s\nreport_dir=%s\nreport_packets=%d\nreport_filter=%d\n",
            language.c_str(), scale, current, fingerprints.c_str(), discDir.c_str(), sound ? 1 : 0, soundVolume, lastPage.c_str(), reportDir.c_str(),
            reportPackets ? 1 : 0, reportFilter);
    for (size_t i = 0; i < servers.size(); ++i) {
        const ServerEntry& s = servers[i];
        fprintf(f, "server.%zu=%s|%s|%d|%s|%s|%s|%d|%s|%d\n", i, s.label.c_str(), s.host.c_str(), s.port, s.user.c_str(), s.useKey ? "key" : "password",
                s.keyPath.c_str(), s.remember ? 1 : 0, s.remember && !s.useKey ? Protect(s.password).c_str() : "", s.bot);
    }
    for (size_t i = 0; i < accounts.size(); ++i)        // no account in the clear in the file, its ID included
        fprintf(f, "account.%zu=dp:%s|%s\n", i, Protect(accounts[i].id).c_str(), Protect(accounts[i].password).c_str());
    // the game settings kept: their password locks encrypted, never in the settings' JSON
    if (!lastGame.settings.empty())
        fprintf(f, "game_last.settings=%s\ngame_last.password=%s\n", lastGame.settings.c_str(), Protect(lastGame.password).c_str());
    for (size_t i = 0; i < presets.size(); ++i)
        fprintf(f, "preset.%zu.name=%s\npreset.%zu.settings=%s\npreset.%zu.password=%s\n", i, presets[i].name.c_str(), i, presets[i].settings.c_str(), i,
                Protect(presets[i].password).c_str());
    for (const auto& [k, t] : reportAt) fprintf(f, "report.%s=%lld\n", k.c_str(), t);
    fclose(f);
}

int Config::FindPreset(const std::string& name) const {
    auto up = [](std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; };
    for (size_t i = 0; i < presets.size(); ++i) if (up(presets[i].name) == up(name)) return (int)i;
    return -1;
}

int Config::FindServer(const std::string& h, int p) const {
    for (size_t i = 0; i < servers.size(); ++i) if (servers[i].host == h && servers[i].port == p) return (int)i;
    return -1;
}

void Config::KeepAccount(const std::string& id, const std::string& password) {
    for (auto& a : accounts) if (a.id == id) { if (!password.empty()) a.password = password; return; }
    accounts.push_back({id, password});
}

void Config::ForgetAccount(const std::string& id) {
    accounts.erase(std::remove_if(accounts.begin(), accounts.end(), [&](const AccountEntry& a) { return a.id == id; }), accounts.end());
}

std::string Config::TrustedFp(const std::string& h, int p) const {
    const std::string key = h + ":" + std::to_string(p) + "=";
    size_t at = 0;
    while (at < fingerprints.size()) {
        size_t e = fingerprints.find(';', at);
        if (e == std::string::npos) e = fingerprints.size();
        const std::string item = fingerprints.substr(at, e - at);
        if (item.compare(0, key.size(), key) == 0) return item.substr(key.size());
        at = e + 1;
    }
    return {};
}

void Config::Trust(const std::string& h, int p, const std::string& fp) {
    const std::string key = h + ":" + std::to_string(p) + "=";
    std::string out;
    size_t at = 0;
    while (at < fingerprints.size()) {
        size_t e = fingerprints.find(';', at);
        if (e == std::string::npos) e = fingerprints.size();
        const std::string item = fingerprints.substr(at, e - at);
        if (!item.empty() && item.compare(0, key.size(), key) != 0) out += item + ";";
        at = e + 1;
    }
    fingerprints = out + key + fp;
}
