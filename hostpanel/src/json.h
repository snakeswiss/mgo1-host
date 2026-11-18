// A small JSON reader and string escaper (what the host's agent prints).
#pragma once
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json& operator[](const char* k) const {
        static const Json none;
        if (kind != Obj) return none;
        auto it = o.find(k);
        return it == o.end() ? none : it->second;
    }
    const Json& operator[](size_t i) const {
        static const Json none;
        return kind == Arr && i < a.size() ? a[i] : none;
    }
    bool IsNull() const { return kind == Null; }
    int Int(int def = 0) const { return kind == Num ? (int)n : kind == Bool ? (int)b : def; }
    double Dbl(double def = 0) const { return kind == Num ? n : def; }
    bool Truthy() const { return kind == Bool ? b : kind == Num ? n != 0 : false; }
    const std::string& Text() const { static const std::string e; return kind == Str ? s : e; }
    size_t Size() const { return kind == Arr ? a.size() : kind == Obj ? o.size() : 0; }

    static bool Parse(const std::string& text, Json& out) {
        const char* p = text.c_str();
        const char* e = p + text.size();
        if (!Value(p, e, out, 0)) return false;
        Ws(p, e);
        return p == e;
    }

private:
    static void Ws(const char*& p, const char* e) { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    static void Utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    static bool String(const char*& p, const char* e, std::string& o) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\') {
                if (++p >= e) return false;
                switch (*p) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (e - p < 5) return false;
                    unsigned cp = (unsigned)strtoul(std::string(p + 1, 4).c_str(), nullptr, 16);
                    p += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 7 && p[1] == '\\' && p[2] == 'u') {
                        unsigned lo = (unsigned)strtoul(std::string(p + 3, 4).c_str(), nullptr, 16);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                    Utf8(o, cp);
                    break;
                }
                default: o += *p;
                }
                ++p;
            } else o += *p++;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    static bool Value(const char*& p, const char* e, Json& v, int depth) {
        if (depth > 64) return false;
        Ws(p, e);
        if (p >= e) return false;
        if (*p == '{') {
            v.kind = Obj; ++p; Ws(p, e);
            if (p < e && *p == '}') { ++p; return true; }
            while (p < e) {
                std::string k;
                Ws(p, e);
                if (!String(p, e, k)) return false;
                Ws(p, e);
                if (p >= e || *p != ':') return false;
                ++p;
                if (!Value(p, e, v.o[k], depth + 1)) return false;
                Ws(p, e);
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return false;
            }
            return false;
        }
        if (*p == '[') {
            v.kind = Arr; ++p; Ws(p, e);
            if (p < e && *p == ']') { ++p; return true; }
            while (p < e) {
                v.a.emplace_back();
                if (!Value(p, e, v.a.back(), depth + 1)) return false;
                Ws(p, e);
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return false;
            }
            return false;
        }
        if (*p == '"') { v.kind = Str; return String(p, e, v.s); }
        if (e - p >= 4 && !strncmp(p, "true", 4)) { v.kind = Bool; v.b = true; p += 4; return true; }
        if (e - p >= 5 && !strncmp(p, "false", 5)) { v.kind = Bool; v.b = false; p += 5; return true; }
        if (e - p >= 4 && !strncmp(p, "null", 4)) { v.kind = Null; p += 4; return true; }
        char* end = nullptr;
        v.n = strtod(p, &end);
        if (end == p) return false;
        v.kind = Num; p = end;
        return true;
    }
};

inline std::string JsonQuote(const std::string& v) {
    std::string o = "\"";
    for (unsigned char ch : v) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch == '\n') o += "\\n";
        else if (ch < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += (char)ch;
    }
    return o + "\"";
}
