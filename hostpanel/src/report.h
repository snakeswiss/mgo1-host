// The Game Report: what happens in a bot's game (the host's own events, mgo1host-agent events /
// watch), in the program's language, with the packets and the game's messages behind each line;
// shown on the GAME REPORT page and written on this PC, one file a day for each bot:
// <Documents>/MGO1 Host/Game Report/<server>/Bot <N>/<YYYY-MM-DD>.log (local dates).
#pragma once
#include "config.h"
#include "json.h"
#include "rules.h"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

struct ReportLine {
    long long t = 0;                 // ms since 1970 (UTC)
    int cat = 0;                     // 1 fights, 2 players, 3 game, 4 the bot and its owner
    std::string text;                // the sentence, in the program's language
    std::vector<std::string> details;  // the packets and messages behind it
    std::string key;                 // the event as the server wrote it (duplicates)
    Json src;                        // the event itself: its words again in another language
    int lang = -1;                   // the language of text and details
};

enum { RC_ALL = 0, RC_FIGHTS, RC_PLAYERS, RC_GAME, RC_BOT, RC_COUNT };

// one event of the server, in words (the rules' and maps' names: rules.h)
ReportLine ReportDescribe(const Json& e);
std::string ReportClock(long long t, bool date);               // local "HH:MM:SS" (or "YYYY-MM-DD HH:MM:SS")

class Report {
public:
    // the server and the bot shown; the lines shown are cleared when they change
    void Show(const std::string& serverKey, int bot);
    // events of the bot shown (its last day, then the watch's): the page's lines
    void Add(const Json& e, bool fromWatch);
    const std::deque<ReportLine>& Lines() const { return lines_; }
    void Relang();                   // the language changed: every line in the new one
    // the files on this PC: events of any bot of the server written once, in order (the cursor
    // Config::reportAt keeps the last one written); returns how many were written
    int Write(Config& cfg, const std::string& serverKey, const std::string& serverName, int bot, const std::vector<Json>& events);
    static std::string Folder(const Config& cfg, const std::string& serverName, int bot);
    static std::string SafeName(const std::string& s);
    long long Cursor(const Config& cfg, const std::string& serverKey, int bot) const;

private:
    std::string server_;
    int bot_ = 0;
    std::deque<ReportLine> lines_;
    std::set<std::string> shown_;
    std::map<std::string, std::set<std::string>> writtenAtCursor_;   // "server#bot" -> the events of the cursor's millisecond
};
