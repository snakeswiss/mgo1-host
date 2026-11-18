// (build 1.00, quickstart) MGO1 Host's QUICK SETUP against a stand-in server: tests/fake_ssh_host.py (SSH on 127.0.0.1, the real
// host/mgo1host-agent on a sandbox's folders, tests/quick/install.sh with the server build's marks). The program's own App and
// Remote (its SSH, its installation, its agent commands), its page drawn every frame without a window (Dear ImGui with no
// renderer), GO pressed as the page's button presses it, the server key's box answered as its buttons answer it; every step's
// state printed when it changes, with the installation's phase, the build's stage, percent and time.
// Built and run by tests/test_quick_setup.py (it prepares the program's folder, the disc's stand-in files and the server):
//   panel_quick_go --prog <folder with server/> --port <n> --password <pw> --disc <folder> --account <ID> --good <pw> --bad <pw>
//                  --scenario public|private
// public: GO with CANCEL in the key's box (failed at step 2), GO (TRUST; the build stopped by the compiler: failed at step 3), GO
// (resumed at step 3: the build goes on; SaveMGO refuses the account: failed at step 8, resumed at 4), the right password and GO
// (resumed at step 4, done). private: GO (a package with the host program: no build), done; GO again: "already installed".
#include "app.h"
#include "gui.h"
#include "i18n.h"
#include "imgui.h"
#include "platform.h"
#include "ui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
int gFails = 0, gOks = 0;
uint64_t gT0 = 0;
double Now() { return (plat::TickMs() - gT0) / 1000.0; }
void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%7.1fs %s  %s%s%s\n", Now(), ok ? "ok  " : "FAIL", what, ok || detail.empty() ? "" : "  ", ok ? "" : detail.c_str());
    fflush(stdout);
    ok ? ++gOks : ++gFails;
}
const char* const kNames[QS_COUNT] = {"CONNECT", "KEY, SIGN-IN", "INSTALL", "ACCOUNT", "GAME SETTINGS", "VIRTUAL DOLLS", "START", "ONLINE"};
const char* const kStates[4] = {"WAITING", "RUNNING", "DONE", "FAILED"};
std::string Show(const std::string& note) { return note.empty() || note[0] != '@' ? note : TrMsg(note); }

struct Printer {                                       // what changed since the last frame
    int state[QS_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::string note[QS_COUNT], phase, stage;
    int pct = -2;
    bool lastRunning = false;
    void Frame(App& app) {
        const QuickRun& q = app.Quick();
        for (int i = 0; i < QS_COUNT; ++i)
            if (q.state[i] != state[i] || q.note[i] != note[i]) {
                state[i] = q.state[i]; note[i] = q.note[i];
                printf("%7.1fs   step %d %-13s %-7s %s\n", Now(), i + 1, kNames[i], kStates[q.state[i]], Show(q.note[i]).c_str());
            }
        const InstallProgress ip = app.Installing();
        if (ip.running && ip.phase != phase) { phase = ip.phase; printf("%7.1fs     installation: %s\n", Now(), phase.c_str()); }
        if (!ip.running) phase.clear();
        if (ip.running && !ip.buildStage.empty() && (ip.buildStage != stage || ip.buildPct != pct)) {
            stage = ip.buildStage; pct = ip.buildPct;
            const unsigned long long s = ip.buildSince ? (plat::TickMs() - ip.buildSince) / 1000 : 0;
            printf("%7.1fs     build: stage %s, %d %%, %llu min %02llu s so far (the page: %s)\n", Now(), stage.c_str(), pct, s / 60, s % 60,
                   stage.empty() ? "-" : TrMsg("@is_build|@bs_" + stage).c_str());
        }
        if (lastRunning && !q.running) {
            if (q.done) printf("%7.1fs   == done: every step went through\n", Now());
            else if (q.failed >= 0)
                printf("%7.1fs   == failed at step %d (%s): %s\n           what to do: %s\n           GO starts again at step %d\n", Now(), q.failed + 1,
                       kNames[q.failed], Show(q.note[q.failed]).c_str(), T(q.hint.c_str()), q.resumeAt + 1);
        }
        lastRunning = q.running;
        fflush(stdout);
    }
};
}  // namespace

int main(int argc, char** argv) {
    std::string prog, password, disc, account, good, bad, scenario = "public";
    int port = 2222;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--prog") prog = v; else if (k == "--port") port = atoi(v.c_str()); else if (k == "--password") password = v;
        else if (k == "--disc") disc = v; else if (k == "--account") account = v; else if (k == "--good") good = v;
        else if (k == "--bad") bad = v; else if (k == "--scenario") scenario = v;
    }
    gT0 = plat::TickMs();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    App app;
    app.Init(prog);                                    // (no server kept: QUICK SETUP is the page shown)
    SetLang(L_EN);
    ui::S = 1;
    ui::Theme();
    gui::BuildFonts(prog);
    Printer pr;
    auto frame = [&] {
        io.DisplaySize = ImVec2(1280, 2400);
        io.DeltaTime = 1.0f / 30;
        ImGui::NewFrame();
        app.Frame(1280, 2400);
        ImGui::Render();
        pr.Frame(app);
        plat::SleepMs(33);
    };
    for (int i = 0; i < 3; ++i) frame();
    // a GO, its run to its end; trust: the key's box answered TRUST (else CANCEL)
    auto go = [&](const char* what, bool trust, double most) {
        printf("%7.1fs == GO (%s)\n", Now(), what);
        app.QuickGo();
        bool asked = false;
        const double end = Now() + most;
        while (app.Quick().running && Now() < end) {
            const QuickRun& q = app.Quick();
            if (!asked && q.step == QS_SIGNIN && q.note[QS_SIGNIN] == "@qs_n_trust") {
                asked = true;
                for (int i = 0; i < 15; ++i) frame();  // (the box drawn, as a user sees it)
                printf("%7.1fs   the server key's box: %s\n", Now(), trust ? "TRUST" : "CANCEL");
                app.TrustAnswer(trust);
            }
            frame();
        }
        for (int i = 0; i < 5; ++i) frame();
        return asked;
    };
    app.QuickFill("127.0.0.1", port, "root", password, disc);
    if (scenario == "public") {
        app.QuickAccount(account.c_str(), bad.c_str());
        bool asked = go("the key refused", false, 60);
        Check("the server's key asked (one box)", asked);
        Check("CANCEL: failed at step 2, GO starts again there", app.Quick().failed == QS_SIGNIN && app.Quick().resumeAt == QS_SIGNIN &&
                                                                  app.Quick().note[QS_SIGNIN] == "@qs_f_notrust");
        asked = go("trusted; the build stops", true, 1200);
        Check("the server's key asked again and trusted", asked);
        Check("connected and signed in", app.Quick().state[QS_CONNECT] == QS_DONE && app.Quick().state[QS_SIGNIN] == QS_DONE);
        Check("the build's failure: step 3 failed with install.sh's line", app.Quick().failed == QS_INSTALL &&
                                                                         app.Quick().note[QS_INSTALL].find("@ie_build|g++") != std::string::npos &&
                                                                         app.Quick().hint == "qs_h_build");
        asked = go("resumed at the installation", true, 1800);
        Check("no second key box (the key kept)", !asked);
        Check("steps 1 and 2 kept done, the installation done", app.Quick().state[QS_CONNECT] == QS_DONE && app.Quick().state[QS_INSTALL] == QS_DONE);
        Check("SaveMGO's refusal: step 8 failed, GO starts again at step 4", app.Quick().failed == QS_ONLINE && app.Quick().resumeAt == QS_ACCOUNT);
        app.QuickAccount(account.c_str(), good.c_str());
        go("the right password", true, 600);
        Check("resumed at step 4: the installation not run again", app.Quick().state[QS_INSTALL] == QS_DONE &&
                                                                    app.Quick().note[QS_INSTALL].rfind("@qs_n_installed|", 0) == 0);
        Check("every step done", app.Quick().done);
    } else {
        app.QuickAccount(account.c_str(), good.c_str());
        go("a package with the host program", true, 900);
        Check("every step done", app.Quick().done);
        Check("installed (no build)", app.Quick().note[QS_INSTALL].rfind("@qs_n_installed|", 0) == 0);
        for (int i = 0; i < 90; ++i) frame();          // (the dashboard opens 2 s after the end)
        go("again on a server set up", true, 600);
        Check("every step done again", app.Quick().done);
        Check("the installation skipped: already installed", app.Quick().note[QS_INSTALL].rfind("@qs_n_already|", 0) == 0);
    }
    printf("%7.1fs == %d ok, %d failed\n", Now(), gOks, gFails);
    app.Shutdown();
    ImGui::DestroyContext();
    return gFails ? 1 : 0;
}
