// cpuclass.h - the processor's model, its logical processors by efficiency class (a hybrid CPU's performance and
// efficiency cores) and where the game's threads ran (MGO1.log's cpu: lines, docs/LOGS.md). The tables and texts
// are pure (tests/cpu_class_test.cpp feeds them fake sets); the readers of the system are under _WIN32.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#endif

namespace cpuclass {
constexpr int kMaxLp = 2048;                                  // 32 groups of 64 logical processors (Windows' limit)
inline int LpIndex(int group, int number) { return group >= 0 && group < 32 && number >= 0 && number < 64 ? group * 64 + number : -1; }

struct Lp { int group, number, eff; };                        // a logical processor and its EfficiencyClass

// the classes present, the highest EfficiencyClass (the fastest) first, and each logical processor's class
struct Map {
    std::vector<int> classes;                                 // EfficiencyClass values, descending
    std::vector<std::vector<int>> members;                    // each class's logical processors (indexes), ascending
    uint8_t of[kMaxLp];                                       // the class's place by index, 0xFF: not in the table
    int total = 0;
};
inline Map Build(const std::vector<Lp>& lps) {
    Map m;
    memset(m.of, 0xFF, sizeof m.of);
    for (const Lp& l : lps)
        if (LpIndex(l.group, l.number) >= 0 && std::find(m.classes.begin(), m.classes.end(), l.eff) == m.classes.end()) m.classes.push_back(l.eff);
    std::sort(m.classes.rbegin(), m.classes.rend());
    if (m.classes.size() > 255) m.classes.resize(255);
    m.members.assign(m.classes.size(), {});
    for (const Lp& l : lps) {
        const int i = LpIndex(l.group, l.number);
        const auto k = std::find(m.classes.begin(), m.classes.end(), l.eff);
        if (i < 0 || m.of[i] != 0xFF || k == m.classes.end()) continue;   // out of range or listed twice
        m.of[i] = (uint8_t)(k - m.classes.begin());
        m.members[m.of[i]].push_back(i);
        ++m.total;
    }
    for (auto& v : m.members) std::sort(v.begin(), v.end());
    return m;
}
// "0-7,16,18-19"; another group than the first: "1:0-15"
inline std::string Ranges(const std::vector<int>& v) {
    std::string s;
    char b[40];
    for (size_t i = 0; i < v.size();) {
        size_t j = i;
        while (j + 1 < v.size() && v[j + 1] == v[j] + 1 && v[j + 1] / 64 == v[i] / 64) ++j;
        if (v[i] / 64) snprintf(b, sizeof b, "%s%d:%d", s.empty() ? "" : ",", v[i] / 64, v[i] % 64);
        else snprintf(b, sizeof b, "%s%d", s.empty() ? "" : ",", v[i]);
        s += b;
        if (j > i) { snprintf(b, sizeof b, "-%d", v[j] % 64); s += b; }
        i = j + 1;
    }
    return s;
}
// the start line's part after the model
inline std::string Describe(const Map& m) {
    if (m.classes.empty()) return "the logical processors' classes unknown (no CPU set nor core table)";
    std::string s = std::to_string(m.total) + " logical processors";
    if (m.classes.size() == 1) return s + ", all of one class (no performance / efficiency cores)";
    const bool two = m.classes.size() == 2;
    s += two ? ": " : " in " + std::to_string(m.classes.size()) + " classes: ";
    for (size_t k = 0; k < m.classes.size(); ++k) {
        const std::string cls = std::to_string(m.classes[k]);
        s += (k ? ", " : "") + std::to_string(m.members[k].size()) +
             (two ? (k == 0 ? " performance (class " : " efficiency (class ") + cls + ": " : " of class " + cls + " (") + Ranges(m.members[k]) + ")";
    }
    return two ? s : s + ", the highest the fastest";
}
// printable ASCII, runs of spaces as one, none at the ends
inline std::string Clean(const char* p) {
    std::string s;
    for (; p && *p; ++p) {
        const char c = *p;
        if (c == ' ' || c == '\t') { if (!s.empty() && s.back() != ' ') s += ' '; }
        else if (c > ' ' && c < 127) s += c;
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
// CPUID 0x80000002..4's registers (EAX, EBX, ECX, EDX each) as the brand string
inline std::string BrandFromRegs(const uint32_t r[12]) {
    char s[49] = {};
    memcpy(s, r, 48);
    return Clean(s);
}
inline std::string StartLine(const std::string& brand, const Map& m) {
    return (brand.empty() ? std::string("the model unknown (no CPUID brand string)") : brand) + ", " + Describe(m);
}
inline std::string WindowsText(unsigned major, unsigned minor, unsigned build, unsigned ubr, const std::string& display, const std::string& wine) {
    if (!major) return "Windows, its version unknown" + (wine.empty() ? std::string() : ", under Wine " + wine);
    const std::string name = major == 10 && build >= 22000 ? "11" : major == 10 ? "10" : major == 6 && minor == 3 ? "8.1" :
                             major == 6 && minor == 2 ? "8" : major == 6 && minor == 1 ? "7" : std::to_string(major) + "." + std::to_string(minor);
    char b[64];
    if (ubr) snprintf(b, sizeof b, "%u.%u.%u.%u", major, minor, build, ubr);
    else snprintf(b, sizeof b, "%u.%u.%u", major, minor, build);
    return "Windows " + name + (display.empty() ? "" : " " + display) + " (build " + b + ")" + (wine.empty() ? "" : ", under Wine " + wine);
}

// a thread's samples by logical processor, the last slot for one outside the table
struct Samples {
    std::atomic<uint32_t> n[kMaxLp + 1];
    Samples() { for (auto& x : n) x.store(0, std::memory_order_relaxed); }
    void Add(int group, int number) { const int i = LpIndex(group, number); n[i >= 0 ? i : kMaxLp].fetch_add(1, std::memory_order_relaxed); }
    void Take(uint32_t* out) { for (int i = 0; i <= kMaxLp; ++i) out[i] = n[i].exchange(0, std::memory_order_relaxed); }
};
inline std::string Pct(uint32_t n, uint32_t total) {          // rounded; "<1" / ">99" rather than a false 0 / 100
    if (!total) return "0";
    const uint32_t p = (uint32_t)((200ull * n + total) / (2ull * total));
    if (n && !p) return "<1";
    if (n < total && p >= 100) return ">99";
    return std::to_string(p);
}
struct Who { const char* name; const uint32_t* raw; bool on; };   // a thread, its samples (Samples::Take), off: not running
// the cpu: line's part: each thread's share of samples by class
inline std::string SharesText(const Map& m, const std::vector<Who>& who) {
    std::string s;
    const bool split = m.classes.size() >= 2;
    if (!split) s = m.classes.empty() ? "the cores' classes unknown (samples: " : "one class of cores (samples: ";
    else if (m.classes.size() == 2) s = "on performance / efficiency cores: ";
    else {
        s = "on cores of class ";
        for (size_t k = 0; k < m.classes.size(); ++k) s += (k ? " / " : "") + std::to_string(m.classes[k]);
        s += " (the first the fastest): ";
    }
    for (size_t w = 0; w < who.size(); ++w) {
        s += (w ? ", " : "") + std::string(who[w].name) + " ";
        if (!who[w].on) { s += "off"; continue; }
        std::vector<uint32_t> by(m.classes.size(), 0);
        uint32_t total = 0, unknown = 0;
        for (int i = 0; i <= kMaxLp; ++i) {
            const uint32_t c = who[w].raw[i];
            if (!c) continue;
            total += c;
            if (i < kMaxLp && m.of[i] != 0xFF) by[m.of[i]] += c; else unknown += c;
        }
        if (!total) { s += "no sample"; continue; }
        if (!split) { s += std::to_string(total) + (unknown && !m.classes.empty() ? " (" + std::to_string(unknown) + " outside the table)" : ""); continue; }
        for (size_t k = 0; k < by.size(); ++k) s += (k ? " / " : "") + Pct(by[k], total);
        s += " % of " + std::to_string(total) + (unknown ? " (" + Pct(unknown, total) + " % outside the table)" : "");
    }
    return split ? s : s + ")";
}
inline std::string PriorityText(uint32_t cls) {               // GetPriorityClass's value
    switch (cls) {
    case 0x40: return "idle";
    case 0x4000: return "below normal";
    case 0x20: return "normal";
    case 0x8000: return "above normal";
    case 0x80: return "high";
    case 0x100: return "realtime";
    case 0: return "not readable";
    }
    char b[24];
    snprintf(b, sizeof b, "%#x", (unsigned)cls);
    return b;
}
// PROCESS_POWER_THROTTLING_STATE: bit 1 the execution speed (EcoQoS), bit 4 the timer resolution ignored
inline std::string ProcessText(uint32_t priorityClass, bool readable, uint32_t control, uint32_t state) {
    std::string s = "the process: priority " + PriorityText(priorityClass) + ", power throttling ";
    if (!readable) return s + "not readable";
    s += !(control & 1) ? "left to Windows" : (state & 1) ? "on (EcoQoS, efficiency mode)" : "off";
    if (control & 4) s += (state & 4) ? ", the timer resolution ignored" : ", the timer resolution kept";
    return s;
}

#ifdef _WIN32
// the logical processors and their classes: GetSystemCpuSetInformation, else GetLogicalProcessorInformationEx
inline std::vector<Lp> SystemLps() {
    std::vector<Lp> out;
    HMODULE k = GetModuleHandleA("kernel32.dll");
    typedef BOOL(WINAPI * CpuSetFn)(void*, ULONG, PULONG, HANDLE, ULONG);
    if (auto f = k ? (CpuSetFn)(void*)GetProcAddress(k, "GetSystemCpuSetInformation") : nullptr) {
        ULONG len = 0;
        f(nullptr, 0, &len, nullptr, 0);
        std::vector<uint8_t> b(len);
        if (len && f(b.data(), len, &len, nullptr, 0))
            for (size_t p = 0; p + 8 <= len;) {                   // SYSTEM_CPU_SET_INFORMATION: Size, Type, then CpuSet
                uint32_t size, type;
                memcpy(&size, &b[p], 4); memcpy(&type, &b[p + 4], 4);
                if (size < 8 || p + size > len) break;
                if (type == 0 && size >= 20) { uint16_t g; memcpy(&g, &b[p + 12], 2); out.push_back({g, b[p + 14], b[p + 18]}); }
                p += size;
            }
    }
    if (!out.empty()) return out;
    typedef BOOL(WINAPI * GlpiFn)(int, void*, PDWORD);
    if (auto f = k ? (GlpiFn)(void*)GetProcAddress(k, "GetLogicalProcessorInformationEx") : nullptr) {
        DWORD len = 0;
        f(0, nullptr, &len);                                  // RelationProcessorCore
        std::vector<uint8_t> b(len);
        if (len && f(0, b.data(), &len))
            for (size_t p = 0; p + 8 <= len;) {                   // Relationship, Size, PROCESSOR_RELATIONSHIP
                uint32_t rel, size;
                memcpy(&rel, &b[p], 4); memcpy(&size, &b[p + 4], 4);
                if (size < 8 || p + size > len) break;
                if (rel == 0 && size >= 48) {
                    uint16_t groups;
                    memcpy(&groups, &b[p + 30], 2);
                    for (uint16_t gi = 0; gi < groups && 32 + 16 * (gi + 1) <= size; ++gi) {   // GROUP_AFFINITY: Mask, Group
                        uint64_t mask; uint16_t grp;
                        memcpy(&mask, &b[p + 32 + 16 * gi], 8); memcpy(&grp, &b[p + 40 + 16 * gi], 2);
                        for (int n = 0; n < 64; ++n) if (mask >> n & 1) out.push_back({grp, n, b[p + 9]});
                    }
                }
                p += size;
            }
    }
    return out;
}
inline std::string Brand() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(0x80000000, &a, &b, &c, &d) || a < 0x80000004) return "";
    uint32_t r[12];
    for (unsigned i = 0; i < 3; ++i) { __get_cpuid(0x80000002 + i, &a, &b, &c, &d); r[4 * i] = a; r[4 * i + 1] = b; r[4 * i + 2] = c; r[4 * i + 3] = d; }
    return BrandFromRegs(r);
#else
    return "";
#endif
}
inline std::string WindowsBuild() {
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    typedef LONG(WINAPI * RtlFn)(OSVERSIONINFOW*);
    auto rtl = nt ? (RtlFn)(void*)GetProcAddress(nt, "RtlGetVersion") : nullptr;
    if (!rtl || rtl(&v) != 0) v.dwMajorVersion = 0;
    DWORD ubr = 0;
    char disp[64] = "";
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD n = sizeof ubr, t = 0;
        if (RegQueryValueExA(key, "UBR", nullptr, &t, (BYTE*)&ubr, &n) != ERROR_SUCCESS || t != REG_DWORD) ubr = 0;
        n = sizeof disp - 1; t = 0;
        if (RegQueryValueExA(key, "DisplayVersion", nullptr, &t, (BYTE*)disp, &n) != ERROR_SUCCESS || t != REG_SZ) disp[0] = 0;
        disp[sizeof disp - 1] = 0;
        RegCloseKey(key);
    }
    typedef const char*(__cdecl * WineFn)();
    auto wine = nt ? (WineFn)(void*)GetProcAddress(nt, "wine_get_version") : nullptr;
    return WindowsText(v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, ubr, Clean(disp), wine ? Clean(wine()) : "");
}
inline std::vector<std::string> StartLines() { return {StartLine(Brand(), Build(SystemLps())), WindowsBuild()}; }
// the calling thread's processor now (GetCurrentProcessorNumberEx)
inline void Here(int& group, int& number) {
    struct Pn { WORD group; BYTE number, reserved; };
    typedef void(WINAPI * Fn)(Pn*);
    static const Fn f = (Fn)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetCurrentProcessorNumberEx");
    Pn p{0, 0, 0};
    if (f) f(&p); else p.number = (BYTE)GetCurrentProcessorNumber();
    group = p.group; number = p.number;
}
inline std::string ProcessNow() {
    struct { ULONG version, control, state; } s{1, 0, 0};    // PROCESS_POWER_THROTTLING_STATE
    typedef BOOL(WINAPI * GpiFn)(HANDLE, int, void*, DWORD);
    static const GpiFn f = (GpiFn)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcessInformation");
    const bool ok = f && f(GetCurrentProcess(), 4, &s, sizeof s);   // ProcessPowerThrottling
    return ProcessText(GetPriorityClass(GetCurrentProcess()), ok, s.control, s.state);
}
#endif
}  // namespace cpuclass
