// MGO1 Host's version and the rule by which it compares the server's agent with the one it carries (build 1.00).
// One place for the number: src/app.cpp (the About page, the window's title, the saved reports' first line), res/app.rc
// (the file's version resource), build_macos.sh (Info.plist) and tests/panel_version_test.cpp read it from here.
#pragma once
#define MGO1HOST_VERSION "build 1.00"
#define MGO1HOST_VERSION_RC 1,0,0,0          // (the resource's numbers: build 1.00 is 1.0.0.0, build 1.01 would be 1.1.0.0)

#ifndef RC_INVOKED
#include <string>

namespace hostver {

// A build's number: "build 1.00" (the canonical form: "build", a space, the number with two decimals) gives 100,
// "build 1.01" 101, "build 2.00" 200 (a single decimal read as tenths: "build 1.5" is 150). -1 when it is not a build.
inline long BuildCode(const std::string& v) {
    if (v.compare(0, 6, "build ") != 0) return -1;
    size_t i = 6;
    long major = 0, minor = 0;
    int nm = 0, nd = 0;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9' && nm < 6) major = major * 10 + (v[i++] - '0'), ++nm;
    if (nm == 0) return -1;
    if (i < v.size() && v[i] == '.') {
        ++i;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9' && nd < 2) minor = minor * 10 + (v[i++] - '0'), ++nd;
        if (nd == 0) return -1;
        if (nd == 1) minor *= 10;
    }
    if (i != v.size()) return -1;                     // (anything after the number: not a build)
    return major * 100 + minor;
}

inline bool IsBuild(const std::string& v) { return BuildCode(v) >= 0; }

// The agents before build 1.00 (the betas' "2.4", "2.10", "3.7"): the numbers between the dots, "2.4" < "2.10".
inline bool DottedLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') { if (a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i] - '0'); ++i; }
        while (j < b.size() && b[j] != '.') { if (b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j] - '0'); ++j; }
        if (x != y) return x < y;
        ++i; ++j;
    }
    return false;
}

// a older than b. Every agent before build 1.00 (a plain number, 3.7 the last one) is older than every build; builds
// by their number (build 1.00 < build 1.01 < build 1.10 < build 2.00); the plain numbers between themselves as before.
// Equal versions: not older.
inline bool Less(const std::string& a, const std::string& b) {
    const long x = BuildCode(a), y = BuildCode(b);
    if (x >= 0 && y >= 0) return x < y;
    if (x >= 0 || y >= 0) return y >= 0;              // (one build: the other one, a plain number, is the older)
    return DottedLess(a, b);
}

}  // namespace hostver
#endif
