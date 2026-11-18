// (v150-host beta 22, sync) MGO1 Host's GAME page and the server's copy of the settings (hostpanel/src/settings_sync.h): the
// page's commands answered in the order they were sent (remote.cpp: one worker, one queue), the server's file per bot (the
// agent's settings-get and settings-set, 3.7's --base check). Replays the ways a change of the maps and modes came back on
// the page ("the server pushes the old version back", the owner's report of 13 November) and checks the page and the file:
//   1. RELOAD, an edit of the rotation and SAVE before RELOAD's answer: the form keeps the edit, the file has it, nothing unsaved
//      (3.4: the older copy back in the form, "not saved" shown, a second SAVE wrote the old rotation to the server);
//   2. the bot shown changed and back before the first answer: the page shows its own bot's settings (3.4: the other bot's,
//      and SAVE wrote them into this bot);
//   3. the file changed on the server (the shell) while the page holds nothing unsaved: the page's look shows it, a SAVE AND
//      RESTART keeps it (3.4: the page kept the old copy and wrote it over the new one);
//   4. the same with an edit not saved: the edit kept, the file not replaced without asking (3.4: replaced silently);
//   5. (sync2, the review's S3) the server's version taken only as 1 to 64 small hex digits (it goes into SAVE's command line):
//      an odd one (a damaged answer, another program's output) is none, SAVE then sends no --base and says so.
//     g++ -O1 -std=c++17 -I hostpanel/src tests/panel_settings_sync_test.cpp -o /tmp/panel_sync && /tmp/panel_sync
//     /tmp/panel_sync --3.4     (the same checks on MGO1 Host 3.4's rule: every answer fills the form)
//     /tmp/panel_sync --sync    (3.5 as merged before sync2: the server's version taken as it came, pasted into --base)
#include "settings_sync.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

static int gOks = 0, gFails = 0;
static bool gTakeAnyVersion = false;                 // (--sync: the version as it came, before sync2's gsync::VersionOk)
static bool Ok(const std::string& v) { return gTakeAnyVersion ? !v.empty() : gsync::VersionOk(v); }
static std::string Ver(const std::string& v) { return gTakeAnyVersion ? v : gsync::Version(v); }
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s%s%s\n", ok ? "ok  " : "FAIL", what, ok || detail.empty() ? "" : "  (", ok ? "" : detail.c_str(), ok || detail.empty() ? "" : ")");
    ok ? ++gOks : ++gFails;
}

struct Server {                                       // the bots' files and their versions (the agent's settings_version)
    std::map<int, std::string> file;
    std::string odd;                                  // (5: the version this server answers instead, when set)
    std::string Version(int bot) const {              // 16 small hex digits of the text (an FNV-1a here; the agent's SHA-256)
        auto it = file.find(bot);
        if (it == file.end()) return "";
        if (!odd.empty()) return odd;
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : it->second) { h ^= c; h *= 1099511628211ULL; }
        char b[17]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
        return b;
    }
};

struct Page {                                         // the GAME page (app.cpp LoadSettings, SaveSettings, SelectBot)
    bool v34 = false;
    Server* srv = nullptr;
    int bot = 1;
    unsigned gen = 0;
    std::string form, copy, formVersion;              // gs_, gsServer_, gsVersion_
    bool loaded = false, loading = false, elsewhere = false;
    int asked = 0;                                    // the times SAVE was refused and the page asked to replace
    bool odd = false, saidNoBase = false;             // (5) gsVersionOdd_; m_gs_no_base shown
    std::string lastBase = "-";                       // (5) the --base the last SAVE sent ("" none)
    struct Job { bool get; gsync::Ask ask; int bot; std::string sent, base; };
    std::deque<Job> queue;
    void Get(bool fill) {
        gsync::Ask a; a.bot = bot; a.fill = fill; a.gen = fill ? (v34 ? gen : ++gen) : gen;
        if (fill) loading = true;
        queue.push_back({true, a, bot, "", ""});
    }
    void Frame() { if (!loaded && !loading) Get(true); }   // PageGame: the first look
    void Reload() { Get(true); }
    void Edit(const std::string& s) { form = s; }
    void Save(bool force = false) {                   // (app.cpp SaveSettings: --base only a version gsync::VersionOk takes)
        if (!v34) ++gen;
        const std::string base = v34 || force || !Ok(formVersion) ? "" : formVersion;
        if (!v34 && !force && base.empty() && odd) saidNoBase = true;
        lastBase = base;
        queue.push_back({false, {}, bot, form, base});
    }
    void Switch(int b) {                              // SelectBot
        bot = b; loaded = false;
        if (!v34) { ++gen; elsewhere = false; formVersion.clear(); }
    }
    void Look() { if (!v34 && loaded) Get(false); }   // (3.5: every 10 s while the page is shown, agent 3.7)
    void AnswerOne() {
        Job j = queue.front(); queue.pop_front();
        if (j.get) {
            const std::string text = srv->file[j.bot], raw = srv->Version(j.bot), version = v34 ? raw : Ver(raw);
            if (!v34) odd = !raw.empty() && version.empty();
            if (j.ask.fill) loading = false;
            const bool untouched = loaded && form == copy;
            const bool fills = v34 ? gsync::Fills34(j.ask, bot, gen, untouched, version, formVersion)
                                   : gsync::Fills(j.ask, bot, gen, untouched, version, formVersion);
            if (fills) { form = copy = text; formVersion = version; loaded = true; elsewhere = false; }
            else if (!v34 && j.ask.bot == bot && j.ask.gen == gen && version != formVersion) elsewhere = true;
        } else {
            if (!j.base.empty() && j.base != srv->Version(j.bot)) {   // agent 3.7: settings_changed, nothing written
                ++asked; elsewhere = true;
                return;
            }
            srv->file[j.bot] = j.sent;
            if (j.bot == bot) { copy = j.sent; formVersion = v34 ? srv->Version(j.bot) : Ver(srv->Version(j.bot)); elsewhere = false; }
        }
    }
    void AnswerAll() { while (!queue.empty()) AnswerOne(); }
};

int main(int argc, char** argv) {
    const bool v34 = argc > 1 && !strcmp(argv[1], "--3.4");
    gTakeAnyVersion = argc > 1 && !strcmp(argv[1], "--sync");
    printf("panel_settings_sync_test: %s\n", v34 ? "MGO1 Host 3.4's rule (every answer fills the form)" : gTakeAnyVersion
           ? "MGO1 Host 3.5 before sync2 (the server's version taken as it came)" : "settings_sync.h (MGO1 Host 3.5)");
    const std::string A = "rotation = 0:7, 3:2", B = "rotation = 1:13, 0:12", C = "rotation = 4:12", D = "rotation = 3:9";
    {   // 1
        Server s; s.file[1] = A;
        Page p; p.v34 = v34; p.srv = &s;
        p.Frame(); p.AnswerAll();
        p.Reload(); p.Edit(B); p.Save(); p.AnswerAll();
        Check("1. RELOAD, an edit, SAVE before the answer: the form keeps the edit", p.form == B, p.form);
        Check("1. ... the server's file has it", s.file[1] == B, s.file[1]);
        Check("1. ... nothing shown as not saved", p.form == p.copy, p.form + " / " + p.copy);
        p.Save(); p.AnswerAll();                       // (the owner saves again, as the page says it is not saved)
        Check("1. ... a second SAVE keeps the edit on the server", s.file[1] == B, s.file[1]);
    }
    {   // 2
        Server s; s.file[1] = A; s.file[2] = C;
        Page p; p.v34 = v34; p.srv = &s;
        p.Frame(); p.AnswerAll();
        p.Switch(2); p.Frame(); p.Switch(1); p.Frame(); p.AnswerAll(); p.Frame(); p.AnswerAll();
        Check("2. the bot shown changed and back before the answer: its own settings on the page", p.bot == 1 && p.form == A, p.form);
        p.Save(true); p.AnswerAll();
        Check("2. ... SAVE leaves each bot's file its own", s.file[1] == A && s.file[2] == C, s.file[1] + " / " + s.file[2]);
    }
    {   // 3
        Server s; s.file[1] = A;
        Page p; p.v34 = v34; p.srv = &s;
        p.Frame(); p.AnswerAll();
        s.file[1] = D;                                 // the server's shell (mgo1hostctl edit), another MGO1 Host
        p.Look(); p.AnswerAll();
        Check("3. the file changed on the server, nothing unsaved: the page shows it", p.form == D, p.form);
        p.Save(); p.AnswerAll();                       // (SAVE AND RESTART)
        Check("3. ... SAVE AND RESTART keeps it", s.file[1] == D, s.file[1]);
    }
    {   // 4
        Server s; s.file[1] = A;
        Page p; p.v34 = v34; p.srv = &s;
        p.Frame(); p.AnswerAll();
        p.Edit(B);
        s.file[1] = D;
        p.Look(); p.AnswerAll();
        Check("4. the file changed on the server, an edit not saved: the edit kept", p.form == B, p.form);
        p.Save(); p.AnswerAll();
        Check("4. ... SAVE asks before it replaces the server's new copy", p.asked == 1 && s.file[1] == D, std::to_string(p.asked) + " " + s.file[1]);
        p.Save(true); p.AnswerAll();                   // (YES: replaced)
        Check("4. ... replaced when asked to", s.file[1] == B && p.copy == B, s.file[1]);
    }
    if (!v34) {   // 5 (sync2): the version's own rule, then a server that answers an odd one
        Check("5. versions taken: 16 and 64 small hex digits, one digit", Ok("0123456789abcdef") && Ok(std::string(64, 'a')) && Ok("7"));
        const char* refused[] = {"", "0123456789ABCDEF", "0123456789abcdeg", "0123456789abcdef; reboot", "0123456789abcdef\n",
                                 " 0123456789abcdef", "$(id)", "0123456789abcdef --restart"};
        bool none = !Ok(std::string(65, 'a'));
        for (const char* r : refused) none = none && !Ok(r) && Ver(r).empty();
        Check("5. versions refused: empty, capitals, other letters, spaces, a line's end, shell signs, 65 digits", none);
        Server s; s.file[1] = A; s.odd = "0123456789abcdef; reboot";
        Page p; p.v34 = v34; p.srv = &s;
        p.Frame(); p.AnswerAll();
        Check("5. an odd version from the server: the form filled all the same, no version kept", p.form == A && p.formVersion.empty() && p.odd);
        p.Edit(B); p.Save(); p.AnswerAll();
        Check("5. ... SAVE sends no --base (nothing of the answer in its command line) and says so", p.lastBase.empty() && p.saidNoBase && s.file[1] == B,
              p.lastBase + " / " + s.file[1]);
        s.odd.clear();
        p.Look(); p.AnswerAll(); p.Edit(C); p.Save(); p.AnswerAll();
        Check("5. ... the server's versions right again: --base sent again, nothing said", p.lastBase.size() == 16 && !p.odd && s.file[1] == C, p.lastBase);
    }
    printf("panel_settings_sync_test: %d ok, %d FAIL\n", gOks, gFails);
    return gFails ? 1 : 0;
}
