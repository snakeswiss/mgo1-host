// (3.5, v150-host beta 22) The GAME page's form and the server's copy of the settings: which settings-get answer may fill the
// form. MGO1 Host's commands to the server are answered in the order they were sent (remote.cpp: one worker, one queue). 3.4
// filled the form with every answer as it came: a RELOAD's answer that came after a SAVE sent behind it put the older copy
// back in the form (and a second SAVE wrote it to the server: "the server pushes the old version back"), and an answer for
// a bot shown before a switch filled the next bot's page. Used by app.cpp (LoadSettings) and tests/panel_settings_sync_test.cpp.
#pragma once
#include <string>

namespace gsync {

struct Ask {                  // a settings-get sent
    int bot = 0;              // for the bot shown then
    unsigned gen = 0;         // the page's count of SAVE and RELOAD sent then
    bool fill = false;        // a RELOAD or the page's first look (else the page's look every 10 s)
};

// the answer of `ask` (the server's copy at `version`) fills the form: it is for the bot shown now and no SAVE or RELOAD was
// sent after it; the page's own look only when the form holds nothing unsaved and the server's copy is another one than the
// form's (`formVersion`: changed elsewhere, shown as it is)
inline bool Fills(const Ask& ask, int botNow, unsigned genNow, bool untouched, const std::string& version, const std::string& formVersion) {
    if (ask.bot != botNow || ask.gen != genNow) return false;
    return ask.fill || (untouched && !version.empty() && version != formVersion);
}

// (sync2, the review's S3) the server's settings version as MGO1 Host takes it: 1 to 64 small hex digits (agent 3.7 sends 16, the
// first of the file's SHA-256), else none (""): it goes into the command line of SAVE (settings-set --base <version>), so an odd
// answer (a damaged line, another program's output) must not reach it. A version refused: SAVE sends no --base, and says so
inline bool VersionOk(const std::string& v) {
    if (v.empty() || v.size() > 64) return false;
    for (char c : v) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline std::string Version(const std::string& v) { return VersionOk(v) ? v : std::string(); }

// MGO1 Host 3.4's rule (app.cpp LoadSettings at 46942df): every answer filled the form (the tests' "before")
inline bool Fills34(const Ask&, int, unsigned, bool, const std::string&, const std::string&) { return true; }

}  // namespace gsync
