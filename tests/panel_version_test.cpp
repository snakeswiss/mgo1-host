// (build 1.00, build1panel) MGO1 Host's version and how it compares the server's agent with the one it carries
// (hostpanel/src/version.h, app.cpp AgentOld: the banner SERVER PROGRAM OUT OF DATE and its UPDATE THE HOST). Checks:
//   1. this program's version is "build 1.00" (lower-case "build", a space, two decimals), and the agent it carries
//      (host/mgo1host-agent's AGENT_VERSION, read as app.cpp's BundledAgentVersion reads it) is the same build;
//   2. every agent before build 1.00 (the betas' 2.x and 3.x, 3.7 the last) is older than build 1.00, so a server still on
//      beta 25 gets the banner; build 1.00 is older than build 1.01, 1.01 than 1.10, 1.10 than 2.00 (later updates);
//      an equal version is never older (no banner once updated); build 1.00 is not older than any 3.x;
//   3. the betas' rule between themselves as before ("2.4" < "2.10"), and what is not a build is not read as one;
//   4. (--old) the rule MGO1 Host 3.5 had (the numbers between the dots): it fails part 2 (3.7 not older than 1.00).
//     g++ -O1 -std=c++17 -I hostpanel/src tests/panel_version_test.cpp -o /tmp/panel_version && /tmp/panel_version
//     /tmp/panel_version --old                       (the same checks on 3.5's rule: they fail)
//     /tmp/panel_version <server's agent> <carried>  (one pair: "older" or "not older", as the banner would judge it)
#include "version.h"
#include <cstdio>
#include <cstring>
#include <string>

static int gOks = 0, gFails = 0;
static bool gOld = false;

// MGO1 Host 3.5's VersionLess (app.cpp before build 1.00): the numbers between the dots, any other character skipped
static bool OldLess(const std::string& a, const std::string& b) {
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
static bool Less(const std::string& a, const std::string& b) { return gOld ? OldLess(a, b) : hostver::Less(a, b); }

static void Check(const std::string& what, bool ok) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    ok ? ++gOks : ++gFails;
}
static void Older(const char* a, const char* b) { Check(std::string(a) + " older than " + b, Less(a, b)); }
static void NotOlder(const char* a, const char* b) { Check(std::string(a) + " not older than " + b, !Less(a, b)); }

// host/mgo1host-agent's AGENT_VERSION as app.cpp's BundledAgentVersion reads it (its first 200 lines)
static std::string AgentFileVersion(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return "";
    char b[256];
    std::string v;
    for (int i = 0; i < 200 && fgets(b, sizeof b, f); ++i)
        if (!strncmp(b, "AGENT_VERSION = \"", 17)) {
            const char* e = strchr(b + 17, '"');
            if (e) v.assign(b + 17, (size_t)(e - (b + 17)));
            break;
        }
    fclose(f);
    return v;
}

int main(int argc, char** argv) {
    if (argc == 3) {                                  // one pair, as the banner judges it
        printf("%s\n", hostver::Less(argv[1], argv[2]) ? "older" : "not older");
        return 0;
    }
    gOld = argc > 1 && !strcmp(argv[1], "--old");
    const char* agentPath = "host/mgo1host-agent";

    // 1. the versions this tree carries
    Check("the program's version is \"build 1.00\"", std::string(MGO1HOST_VERSION) == "build 1.00");
    Check("the program's version is a build (code 100)", hostver::BuildCode(MGO1HOST_VERSION) == 100);
    const std::string carried = AgentFileVersion(agentPath);
    if (carried.empty()) Check(std::string("host/mgo1host-agent read (run from the source tree's root)"), false);
    else {
        Check("the agent carried (" + carried + ") is the program's build", carried == MGO1HOST_VERSION);
        Check("the agent carried is not older than itself (no banner on an updated server)", !Less(carried, carried));
    }

    // 2. the update from the betas, and the later builds
    Older("3.7", "build 1.00");                       // the owner's server on beta 25
    Older("3.6", "build 1.00");
    Older("3.10", "build 1.00");
    Older("2.4", "build 1.00");
    Older("9.9", "build 1.00");                       // (any plain number: from before build 1.00)
    NotOlder("build 1.00", "3.7");
    NotOlder("build 1.00", "9.9");
    Older("build 1.00", "build 1.01");
    Older("build 1.01", "build 1.10");
    Older("build 1.09", "build 1.10");
    Older("build 1.99", "build 2.00");
    Older("build 1.5", "build 1.51");                 // (one decimal: tenths, 1.5 is 1.50)
    NotOlder("build 1.5", "build 1.50");
    NotOlder("build 1.01", "build 1.00");
    NotOlder("build 1.00", "build 1.00");
    NotOlder("3.7", "3.7");

    // 3. the betas' rule between themselves, and what is not a build
    Older("2.4", "2.10");
    Older("3.6", "3.7");
    NotOlder("3.7", "3.6");
    Check("\"build 1.00\" is a build", hostver::IsBuild("build 1.00"));
    Check("\"3.7\" is not a build", !hostver::IsBuild("3.7"));
    Check("\"1.00\" (no \"build \") is not a build", !hostver::IsBuild("1.00"));
    Check("\"Build 1.00\" (capital) is not a build", !hostver::IsBuild("Build 1.00"));
    Check("\"build 1.00 593d64629f86\" (a VERSION file's line) is not an agent's build", !hostver::IsBuild("build 1.00 593d64629f86"));
    Check("\"build 1.000\" is not a build", !hostver::IsBuild("build 1.000"));
    Check("\"build .5\" is not a build", !hostver::IsBuild("build .5"));
    Check("build 1.01's code is 101", hostver::BuildCode("build 1.01") == 101);
    Check("build 2.00's code is 200", hostver::BuildCode("build 2.00") == 200);

    printf("\n%d ok, %d failed%s\n", gOks, gFails, gOld ? " (MGO1 Host 3.5's rule)" : "");
    return gFails ? 1 : 0;
}
