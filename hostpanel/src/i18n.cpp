#include "i18n.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "i18n_table.inc"

const char* const kLangCodes[L_COUNT] = {"en", "fr", "de", "it", "es", "pt-BR", "ru", "ja", "ko", "zh-Hans", "zh-Hant"};
const char* const kLangNames[L_COUNT] = {"English", "Français", "Deutsch", "Italiano", "Español", "Português (Brasil)",
                                         "Русский", "日本語", "한국어", "简体中文", "繁體中文"};

static int gLang = L_EN;

static const std::unordered_map<std::string, int>& Index() {
    static std::unordered_map<std::string, int> idx;
    if (idx.empty()) {
        const int n = (int)(sizeof kStrings / sizeof kStrings[0]);
        for (int i = 0; i < n; ++i) idx.emplace(kStrings[i][0], i);
#ifndef _WIN32
        // Linux and macOS: a "<key>@posix" row in place of <key> (no Windows account, no .exe)
        for (int i = 0; i < n; ++i) {
            const char* at = strchr(kStrings[i][0], '@');
            if (at && !strcmp(at, "@posix")) idx[std::string(kStrings[i][0], at)] = i;
        }
#endif
    }
    return idx;
}

void SetLang(int lang) { gLang = lang >= 0 && lang < L_COUNT ? lang : L_EN; }
int CurLang() { return gLang; }

int LangFromCode(const std::string& code) {
    for (int i = 0; i < L_COUNT; ++i)
        if (code == kLangCodes[i]) return i;
    return L_EN;
}

bool LangIsCjk() { return gLang == L_JA || gLang == L_KO || gLang == L_ZHS || gLang == L_ZHT; }

const char* T(const char* key) {
    const auto& idx = Index();
    const auto it = idx.find(key);
    if (it == idx.end()) return key;
    const char* s = kStrings[it->second][1 + gLang];
    return s && *s ? s : kStrings[it->second][1];
}

std::string TF(const char* key, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, key);
    vsnprintf(buf, sizeof buf, T(key), ap);
    va_end(ap);
    return buf;
}

std::vector<std::string> KeysWithPrefix(const char* prefix) {
    std::vector<std::string> v;
    const size_t n = strlen(prefix);
    for (const auto& row : kStrings) if (!strncmp(row[0], prefix, n) && !strchr(row[0], '@')) v.push_back(row[0]);
    return v;
}

std::string AllText(int lang) {
    std::string s;
    if (lang < 0 || lang >= L_COUNT) return s;
    for (const auto& row : kStrings) s += row[1 + lang];
    return s;
}

std::string TrMsg(const std::string& m) {
    if (m.size() < 2 || m[0] != '@') return m;
    std::string parts[5];
    int n = 0;
    size_t a = 1;
    while (n < 5) {
        if (n > 0 && m[a] == '@') { parts[n++] = m.substr(a); break; }    // a message inside: the rest is its own
        const size_t e = m.find('|', a);
        parts[n++] = m.substr(a, e == std::string::npos ? std::string::npos : e - a);
        if (e == std::string::npos) break;
        a = e + 1;
    }
    for (int i = 1; i < n; ++i) if (!parts[i].empty() && parts[i][0] == '@') parts[i] = TrMsg(parts[i]);
    if (T(parts[0].c_str()) == parts[0].c_str()) return m.substr(1);     // not a key: as it came
    return TF(parts[0].c_str(), parts[1].c_str(), parts[2].c_str(), parts[3].c_str(), parts[4].c_str());
}

std::string Dec(double v, int places) {
    char b[32];
    snprintf(b, sizeof b, "%.*f", places, v);
    const bool comma = gLang == L_FR || gLang == L_DE || gLang == L_IT || gLang == L_ES || gLang == L_PT || gLang == L_RU;
    if (comma) for (char* p = b; *p; ++p) if (*p == '.') *p = ',';
    return b;
}
