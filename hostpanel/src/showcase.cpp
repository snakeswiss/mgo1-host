// (build 1.00) The showcase: the self-test's made-up host for the release's pictures (MGO1_HOST_SHOWCASE=1 with --selftest;
// gui.cpp Showcase draws it at a window's size). Plausible names, numbers and logs, a generic server (an example. domain),
// none of the layout self-test's longest strings. Every name here is made up in the spirit of the game (MGS3's places and
// units); none is a player's or an account's.
#include "app.h"
#include "i18n.h"
#include "platform.h"
#include "version.h"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
// "2026-11-18T19:40:00Z" (UTC) -> ms
long long Utc(const char* s) {
    int Y, Mo, D, h, mi, se;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &mi, &se) != 6) return 0;
    struct tm t{}; t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = mi; t.tm_sec = se;
    return (long long)plat::TimeGm(&t) * 1000;
}
const char* const kHash = "build 1.00 d11286182942";      // (the version the self-test's server runs, as InitDemo's)
}  // namespace

void App::InitShowcase() {
    // the servers this PC knows, the SaveMGO accounts it keeps (the bots')
    {
        ServerEntry e;
        e.label = "MGO1 SERVER"; e.host = "mgo1-server.example"; e.bot = 1;
        cfg.servers.push_back(e);
        e.label = "TEST SERVER"; e.host = "test-server.example"; cfg.servers.push_back(e);
        cfg.current = 0;
        cfg.accounts = {{"GRU_HOST", "X"}, {"KGB_HOST", "X"}, {"OCELOT_HOST", "X"}};
    }
    snprintf(fLabel_, sizeof fLabel_, "%s", cfg.servers[0].label.c_str());
    snprintf(fHost_, sizeof fHost_, "%s", cfg.servers[0].host.c_str());
    snprintf(fPass_, sizeof fPass_, "%s", "not-a-password");
    snprintf(disc_, sizeof disc_, "%s", "C:\\Users\\Snake\\Documents\\MGO1\\MGS3 Subsistence disc 2 (online)");
    // bot 1's state: a team deathmatch on MOUNTAINTOP, 4 real players, a PLAYABLE DOLL and 2 virtual dolls, a seat kept free
    HostStatus& s = demoStatus_;
    s.valid = true; s.online = "online"; s.service = "active"; s.enabled = true; s.installed = true;
    s.account = "GRU_HOST"; s.schedule = "05:00"; s.agent = MGO1HOST_VERSION; s.lobby = true;
    s.gameName = "Snake Eater TDM";
    s.rules = {{1, 8}, {3, 3}, {0, 2}, {4, 9}, {2, 11}, {1, 12}};
    s.round = 1; s.maxPlayers = 9; s.gameAge = 675;           // (the second round: the Game Report's capture)
    {
        struct P { const char* name; int team, ping, kills, deaths, points; };
        const P ps[] = {{"GRU_HOST", -1, -1, 0, 0, 0},       {"Ocelot_Unit", 0, 38, 9, 4, 21},  {"ThePain_Bees", 1, 74, 7, 6, 16},
                        {"Tselinoyarsk", 0, 52, 6, 5, 14},   {"Paramedic", 1, 121, 4, 7, 11},   {"DOLL_1", 1, 0, 6, 6, 13},
                        {"Sokrovenno", 0, 0, 5, 5, 10},      {"Rassvet", 0, 0, 3, 7, 7}};
        int i = 0;
        for (const P& x : ps) {
            HostPlayer p;
            p.p = ++i; p.name = x.name; p.host = i == 1; p.state = 534; p.team = x.team; p.ping = i == 1 ? -1 : x.ping;
            if (i >= 6) p.ping = 16 + 3 * i;                // (the dolls play on the server itself: a small ping)
            p.kills = x.kills; p.deaths = x.deaths; p.points = x.points;
            s.players.push_back(p);
        }
    }
    s.black = {"Griefer_99", "TK_Spammer"};
    s.kicked = {{"Griefer_99", "black", 372}, {"AFK_Johnny", "kick", 78}};
    s.cpu = 23.4; s.rssMb = 241; s.procUptime = 729; s.memTotal = 3813; s.memAvail = 2410; s.sysUptime = 1043940; s.cores = 2; s.diskFree = 31.4;
    s.load = {0.62};
    s.bot = 1; s.port = 5730; s.botsMax = 3;
    {
        BotInfo b;
        b.bot = 1; b.port = 5730; b.account = "GRU_HOST"; b.service = "active"; b.online = "online"; b.name = s.gameName; b.players = 7; b.max = 9;
        b.enabled = true; b.unranked = 1;                 // (dolls in its game: out of SaveMGO's rankings, as the server does)
        s.bots.push_back(b);
        b.bot = 2; b.port = 5731; b.account = "KGB_HOST"; b.name = "Night Capture"; b.players = 5;
        s.bots.push_back(b);
        b.bot = 3; b.port = 5732; b.account = "OCELOT_HOST"; b.online = "starting"; b.reason = "logging in / creating the game"; b.name = ""; b.players = 0;
        b.unranked = -1;
        s.bots.push_back(b);
    }
    firewall_ = "ufw";
    // the network: SaveMGO's own servers (the program's defaults, as a server that never changed them shows)
    net_.custom = false; net_.web = "http://savemgo.com/us/mgs3/"; net_.gate = "107.161.80.126"; net_.stun = "15.204.66.207";
    net_.gatePort = 5731; net_.stunPort = 3478;
    netServer_ = net_;
    snprintf(nWeb_, sizeof nWeb_, "%s", net_.web.c_str()); snprintf(nGate_, sizeof nGate_, "%s", net_.gate.c_str());
    snprintf(nStun_, sizeof nStun_, "%s", net_.stun.c_str());
    snprintf(nGatePort_, sizeof nGatePort_, "%d", net_.gatePort); snprintf(nStunPort_, sizeof nStunPort_, "%d", net_.stunPort);
    netLoaded_ = true;
    // the Game Report: the game's opening, a round of team deathmatch, the next round's capture
    {
        const long long t0 = Utc("2026-11-18T19:08:42Z");      // (now: 19:20:00 UTC, 20:20 on the server)
        struct E { int t; const char* j; };
        const E evs[] = {
            {0, R"J({"k":"start","bot":1,"port":5730,"v":"build 1.00 d11286182942"})J"},
            {3, R"J({"k":"game","name":"Snake Eater TDM","max":9,"rot":"1:8,3:3,0:2,4:9,2:11,1:12"})J"},
            {4, R"J({"k":"open","pk":[{"l":"4316","d":"o","n":0,"x":""}]})J"},
            {41, R"J({"k":"join","a":"Ocelot_Unit","pk":[{"l":"4340","d":"o","n":4,"x":"00002a17"},{"g":"75100f","p":[2]}]})J"},
            {43, R"J({"k":"team","a":"Ocelot_Unit","tm":"red"})J"},
            {66, R"J({"k":"join","a":"ThePain_Bees","pk":[{"l":"4340","d":"o","n":4,"x":"00003b02"},{"g":"75100f","p":[3]}]})J"},
            {68, R"J({"k":"team","a":"ThePain_Bees","tm":"blue"})J"},
            {97, R"J({"k":"join","a":"Tselinoyarsk","pk":[{"l":"4340","d":"o","n":4,"x":"0000419c"},{"g":"75100f","p":[4]}]})J"},
            {131, R"J({"k":"join","a":"Paramedic","pk":[{"l":"4340","d":"o","n":4,"x":"000046d0"},{"g":"75100f","p":[5]}]})J"},
            {150, R"J({"k":"restart","pk":[{"g":"760121"}]})J"},
            {152, R"J({"k":"round","i":0,"of":6,"s":300,"r":1,"m":8,"rn":1,"rof":2,"n":7,"pk":[{"ph":[1,2],"e":0,"s":300}]})J"},
            {197, R"J({"k":"kill","hs":1,"pts":2,"a":"Ocelot_Unit","b":"ThePain_Bees","pk":[{"g":"751010","p":[2,3]},{"s":"Ocelot_Unit","o":20,"v":2,"a":0,"b":1}]})J"},
            {224, R"J({"k":"stun","cqc":1,"a":"Paramedic","b":"Tselinoyarsk","pk":[{"g":"751006","p":[5,4]},{"s":"Paramedic","o":24,"v":4,"a":0,"b":1}]})J"},
            {251, R"J({"k":"kill","pts":1,"a":"DOLL_1","b":"Sokrovenno","pk":[{"g":"751005","p":[6,7]},{"s":"DOLL_1","o":20,"v":1,"a":2,"b":3}]})J"},
            {279, R"J({"k":"kill","w":"knife","pts":1,"a":"Tselinoyarsk","b":"Paramedic","pk":[{"g":"751005","p":[4,5]}]})J"},
            {305, R"J({"k":"join","a":"Griefer_99","pk":[{"l":"4340","d":"o","n":4,"x":"00005e11"}]})J"},
            {306, R"J({"k":"kicked","a":"Griefer_99","why":"black","pk":[{"l":"4346","d":"o","n":4,"x":"00005e11"}]})J"},
            {332, R"J({"k":"radio","a":"ThePain_Bees","n":2})J"},
            {358, R"J({"k":"kill","w":"scorpion","pts":1,"a":"Rassvet","b":"DOLL_1","pk":[{"g":"751005","p":[8,6]}]})J"},
            {386, R"J({"k":"kill","hs":1,"pts":2,"a":"ThePain_Bees","b":"Ocelot_Unit","pk":[{"g":"751010","p":[3,2]}]})J"},
            {431, R"J({"k":"kill","pts":1,"a":"Ocelot_Unit","b":"Paramedic","pk":[{"g":"751005","p":[2,5]},{"s":"Ocelot_Unit","o":20,"v":2,"a":8,"b":9}]})J"},
            {452, R"J({"k":"end","i":0,"left":0,"r":1,"m":8,"rn":1,"rof":2,"wins":[1,0],"tickets":[12,0],"win":"red","pk":[{"tw":"red"}]})J"},
            {455, R"J({"k":"score","a":"Ocelot_Unit","kills":9,"deaths":4,"pts":21,"stuns":1,"hs":3})J"},
            {455, R"J({"k":"score","a":"ThePain_Bees","kills":7,"deaths":6,"pts":16,"stuns":0,"hs":2})J"},
            {470, R"J({"k":"next","i":1,"of":6,"r":3,"m":3})J"},
            {492, R"J({"k":"round","i":1,"of":6,"s":240,"r":3,"m":3,"rn":1,"rof":5,"n":7,"pk":[{"ph":[1,2],"e":1,"s":240}]})J"},
            {533, R"J({"k":"grab","first":1,"a":"Tselinoyarsk","pk":[{"g":"750014","p":[4]}]})J"},
            {548, R"J({"k":"join","a":"AFK_Johnny","pk":[{"l":"4340","d":"o","n":4,"x":"000061c4"}]})J"},
            {561, R"J({"k":"goal","tm":"red","a":"Tselinoyarsk"})J"},
            {590, R"J({"k":"capleft","s":15,"tm":"red","pk":[{"cd":899}]})J"},
            {598, R"J({"k":"grab","stop":7,"a":"ThePain_Bees","pk":[{"kr":0,"h":3,"x":-10883,"y":1561,"z":-9873}]})J"},
            {600, R"J({"k":"kicked","a":"AFK_Johnny","why":"kick","pk":[{"l":"4346","d":"o","n":4,"x":"000061c4"}]})J"},
            {620, R"J({"k":"kill","pts":1,"a":"Ocelot_Unit","b":"ThePain_Bees","pk":[{"g":"751005","p":[2,3]}]})J"},
            {621, R"J({"k":"drop","a":"ThePain_Bees","pk":[{"kr":0,"h":-1,"x":-9012,"y":1502,"z":-8870}]})J"}};
        report_.Show("demo:22", 1);
        for (const E& e : evs) {
            Json j;
            if (!Json::Parse(e.j, j)) continue;
            j.o["t"].kind = Json::Num; j.o["t"].n = (double)(t0 + 1000LL * e.t);
            report_.Add(j, false);
        }
    }
    // the SERVER page: the last update done, its log
    demoInstall_.done = true; demoInstall_.ok = true; demoInstall_.phase = "done";
    demoInstall_.lines = {"@il_server|x86_64|ubuntu 24.04|0|36120", "@il_disc|%USERPROFILE%\\Documents\\MGO1\\MGS3 Subsistence disc 2 (online)",
                          "@il_disc_same", "@il_sent|13|84", "@il_unpacked", "@is_wine", "@is_wine_version|wine-9.0 (Ubuntu 9.0~repack-4build3)",
                          "@is_files", "@is_prefix", "@is_firewall", "@is_fw_open|ufw|5730", "@is_fw_provider|5730", "@is_restart|1",
                          std::string("@is_installed|") + kHash};
    installUpdate_ = true;
    installedVersion_ = kHash;
    // the GAME page: bot 1's settings, saved and played by its game
    GameSettings& g = gs_;
    g.name = s.gameName;
    g.comment = "All modes in rotation. Play fair, have fun!";
    g.password.clear();
    g.rotation = s.rules;
    g.red = 0; g.blue = 1; g.maxJoin = 8; g.weapons = 0; g.unique = false; g.ff = false; g.tkKick = true; g.idle = true; g.autoBal = true;
    g.voice = true; g.ghosts = false; g.ratingRestr = 0; g.rating = 1000; g.tkCount = 3; g.idleMin = 5; g.balanceDiff = 2;
    gsServer_ = g;
    gsSync_ = gsRunning_ = true; gsElsewhere_ = false;
    gsReal_ = 4;
    snprintf(gName_, sizeof gName_, "%s", g.name.c_str());
    snprintf(gComment_, sizeof gComment_, "%s", g.comment.c_str());
    snprintf(gPass_, sizeof gPass_, "%s", g.password.c_str());
    auto keep = [](const GameSettings& x, const std::string& name) { GameSettings c = x; c.password.clear(); return GamePreset{name, c.ToJson(), x.password}; };
    cfg.lastGame = keep(g, "");
    {
        GameSettings p = g;
        p.name = "Mountaintop TDM"; p.rotation = {{1, 8}}; cfg.presets.push_back(keep(p, "MOUNTAINTOP TDM"));
        p.name = "Night Capture"; p.rotation = {{3, 3}, {3, 12}, {3, 9}}; cfg.presets.push_back(keep(p, "CAPTURE NIGHTS"));
        p.name = "Sneaking Only"; p.rotation = {{4, 9}, {4, 2}, {4, 11}}; cfg.presets.push_back(keep(p, "SNEAKING ONLY"));
        p.name = "Snake Eater TDM"; p.rotation = g.rotation; cfg.presets.push_back(keep(p, "WEEKEND ROTATION"));
    }
    gsLoaded_ = aLoaded_ = aPwSet_ = true;
    snprintf(aId_, sizeof aId_, "%s", "GRU_HOST");
    logAuto_ = false;
    // the LOG page: bot 1's journal (its times in the server's zone), from its start to the second round
    {
        struct L { const char* at; const char* msg; };
        const L lines[] = {
            {"20:07:51", "Started MGO1 dedicated host (bot 1)"},
            {"20:07:52", "mgo1host: bot 1, UDP port 5730, disc /opt/mgo1host/disc"},
            {"20:07:58", "ps2 f60: unranked: auto, the statistics of a game with a doll sent with a VS rating of 0 (MGO1_HOST_UNRANKED)"},
            {"20:08:03", "ps2 f248: text: \"[Play Using Detailed Settings]|Configure network and account settings|before starting\""},
            {"20:08:24", "step 10: \"[Account ID]|Enter your account ID\" -> press 2000 at frame 1394 (4 frames) dg 1352"},
            {"20:08:25", "step 11: \"\" -> type 0 at frame 1443 (4 frames) dg 1400"},
            {"20:08:41", "ps2 f2453: host settings from Z:\\var\\lib\\mgo1host\\bot1\\run\\game.ini: name comment password rotation; host only"},
            {"20:08:45", "ps2 f2781: host settings: 0x4310 from 01418770: \"Snake Eater TDM\", host only 1, rotation 01:08"},
            {"20:08:46", "ps2 f2820: host watch: hosting (frame 2820)"},
            {"20:08:48", "ps2 f2914: network: upstream bandwidth 512 kbps (256 on the card, the game's default), the send budget 512000 bits a second (the game's maximum)"},
            {"20:08:49", "ps2 f3000: blacklist: 2 name(s)"},
            {"20:09:52", "ps2 f5640: unranked: a doll in the game (P6, accountless doll 1, user id 0x7f000001): its statistics go unranked to the game's end"},
            {"20:13:48", "ps2 f17580: lists: P9 \"Griefer_99\" is on the blacklist: kicked"},
            {"20:13:48", "ps2 f17581: kick P9 \"Griefer_99\": room message 04 sent, kick requested"},
            {"20:15:00", "ps2 f25920: command players: P1 state 216 flags 3 (host) name \"GRU_HOST\"; P2 state 216 flags 0 name \"Ocelot_Unit\"; "
                         "P3 state 216 flags 0 name \"ThePain_Bees\"; P4 state 216 flags 0 name \"Tselinoyarsk\"; P5 state 216 flags 0 name \"Paramedic\";"},
            {"20:16:17", "ps2 f27900: lobby filter: 4390 for user id 0x2a17 sent unranked (vs_rating 0, was 1240: a doll in this game)"},
            {"20:16:17", "ps2 f27901: lobby filter: 4390 for user id 0x3b02 sent unranked (vs_rating 0, was 1105: a doll in this game)"},
            {"20:16:17", "ps2 f27902: lobby filter: 4390 for user id 0x419c sent unranked (vs_rating 0, was 980: a doll in this game)"},
            {"20:16:17", "ps2 f27903: lobby filter: 4390 for user id 0x46d0 sent unranked (vs_rating 0, was 1012: a doll in this game)"},
            {"20:16:32", "ps2 f28920: unranked: a new round (0x4392 sent): this game had dolls, its later rounds stay unranked (the VS rating carries over)"},
            {"20:18:42", "ps2 f36100: kick P9 \"AFK_Johnny\": room message 04 sent, kick requested"}};
        for (const L& l : lines) log_.push_back(std::string("2026-11-18T") + l.at + "+0100 mgo1-server mgo1host@1: " + l.msg);
    }
    // the daily logs: bot 1, bot 2, the playable dolls, the doll manager; PLAYABLE DOLL 1's day shown
    {
        LogDayInfo d;
        LogSourceInfo b1; b1.name = "bot1";
        d.day = "2026-11-18"; d.lines = 6210; d.errors = 0; d.warnings = 3; d.crashes = 0; b1.days.push_back(d);
        d.day = "2026-11-17"; d.lines = 11842; d.warnings = 5; b1.days.push_back(d);
        d.day = "2026-11-16"; d.lines = 10377; d.warnings = 2; b1.days.push_back(d);
        LogSourceInfo b2 = b1; b2.name = "bot2";
        LogSourceInfo d1; d1.name = "doll1";
        d.day = "2026-11-18"; d.lines = 4385; d.warnings = 2; d1.days.push_back(d);
        d.day = "2026-11-17"; d.lines = 9120; d.warnings = 4; d1.days.push_back(d);
        LogSourceInfo d2 = d1; d2.name = "doll2";
        LogSourceInfo m; m.name = "manager"; d.day = "2026-11-18"; d.lines = 312; d.warnings = 0; m.days.push_back(d);
        logSources_ = {b1, b2, d1, d2, m};
        logSrc_ = 2; logDay_ = 0; logLevel_ = 0; logListLoaded_ = true;
        const char* dl[] = {
            "2026-11-18T19:09:12.104Z I mgo1doll: doll 1 for bot 1, UDP port 5741, difficulty 6, team auto, version build 1.00 d11286182942",
            "2026-11-18T19:09:12.380Z I doll f0: on (status in Z:\\var\\lib\\mgo1host\\doll1); difficulty 6, team auto",
            "2026-11-18T19:09:14.902Z I doll f8000: level: 6 ELITE (the difficulty set)",
            "2026-11-18T19:09:14.903Z I doll f8001: level: live, between 4 HARD and 8 MASTER",
            "2026-11-18T19:09:47.551Z I doll f2000: lobby: joining Snake Eater TDM",
            "2026-11-18T19:09:50.120Z I ps2 f2914: network: upstream bandwidth 512 kbps (256 on the card, the game's default), the send budget 512000 bits a second (the game's maximum)",
            "2026-11-18T19:09:52.004Z I doll f3098: map n022a: building its navigation",
            "2026-11-18T19:10:54.448Z I doll f8006: real players: 3 -> 4",
            "2026-11-18T19:10:58.214Z I doll f7003: emote: after a kill (Sokrovenno's kill camera)",
            "2026-11-18T19:11:20.733Z I doll f7007: goggles: the thermal goggles on (battery 300)",
            "2026-11-18T19:11:41.090Z I doll f7010: grenade: a smoke between it and Ocelot_Unit: yaw 29 pitch -27.5, goes off 0.1 m from the point (0 bounces)",
            "2026-11-18T19:12:05.512Z I doll f8002: DOLL ADAPT: old=5.40 new=5.80 target=6.40 base=6 enemy_strength=6.8 ally_strength=5.9 balance=+0.30 "
            "confidence=0.74 reason=its_side_behind (enemies 2 real, allies 2 real; tickets 18 against 24)",
            "2026-11-18T19:12:05.513Z I doll f8003: level: tier ELITE (5.80), was EXPERT",
            "2026-11-18T19:12:40.877Z I doll f7008: goggles: the thermal goggles off (battery 120)",
            "2026-11-18T19:13:02.301Z I doll f7009: chaff: a chaff grenade (enemies close)",
            "2026-11-18T19:13:30.016Z I doll f6100: score: kills 6, deaths 6, stuns 1, CQC 0, headshots 2",
            "2026-11-18T19:15:03.972Z I doll f8011: order: difficulty 6 (ELITE)",
            "2026-11-18T19:15:04.010Z I doll f8012: order: live level between 4 (HARD) and 8 (MASTER)",
            "2026-11-18T19:15:40.289Z I doll f7005: emote: greeting ThePain_Bees",
            "2026-11-18T19:16:12.731Z I doll f6200: order: follow ThePain_Bees",
            "2026-11-18T19:17:40.640Z I doll f7000: roll: into Tselinoyarsk (the objective's carrier): knocked down"};
        for (const char* l : dl) dayLines_.push_back(l);
        dayTotal_ = 4385; dayNext_ = -1;
    }
    // the PLAYABLE DOLL page: a doll in each running game, one with its own account waiting for bot 3
    {
        DollInfo a; a.n = 1; a.bot = 1; a.difficulty = 6; a.hasAccount = false; a.accountless = true; a.running = true; a.name = "DOLL_1";
        a.adaptMin = 4; a.adaptMax = 8; a.boundsKnown = true; a.level = 5.8f; a.levelWhy = "behind";
        a.why = "its_side_behind"; a.enemy = 6.8f; a.ally = 5.9f; a.confidence = 0.74f; a.realPlayers = 4; a.presence = "active";
        a.act = "fighting"; a.arg = "Sokrovenno"; a.rule = "tdm"; a.team_ = 1; a.life = 96; a.kills = 6; a.deaths = 6;
        a.alive = true; a.inMatch = true; a.age = 1; a.order = "free";
        for (const auto& p : s.players) a.players.push_back(p.name);
        DollInfo b = a; b.n = 2; b.bot = 2; b.difficulty = 5; b.name = "DOLL_2"; b.adaptMin = 3; b.adaptMax = 7; b.level = 5.2f; b.levelWhy = "even";
        b.why = "on_target"; b.enemy = 5.4f; b.ally = 5.1f; b.confidence = 0.68f; b.realPlayers = 3;
        b.act = "going_to_sniper_spot"; b.arg.clear(); b.rule = "capture"; b.team_ = 0; b.kills = 3; b.deaths = 4; b.order = "objective";
        b.loadout = "svd"; b.aimDirect = false;
        b.players = {"KGB_HOST", "Granin64", "CQC_Master", "Viper_Fury", "DOLL_2", "Krasnogorje"};
        DollInfo c = a; c.n = 3; c.bot = 3; c.difficulty = 7; c.hasAccount = true; c.accountless = false; c.account = "V***"; c.name.clear();
        c.running = false; c.act.clear(); c.arg.clear(); c.order = "free"; c.inMatch = false; c.alive = false; c.kills = c.deaths = 0;
        c.level = 0; c.presence.clear(); c.players.clear(); c.adaptMin = 5; c.adaptMax = 9;
        dolls_ = {a, b, c};
        dollsLoaded_ = true;
        dollsCap_ = 4; dollsMax_ = 16; dollsKeepSeat_ = true;
        dollsAccountless_ = dollsVdoll_ = dollsAim_ = dollsNames_ = true;
        dollsList_ = "host"; dollsUnranked_ = "auto";
        dollsCount_ = {{1, 1}, {2, 1}, {3, 0}};
        DollWait w; w.code = "not_hosting"; w.dollsSet = 1; w.dollsIn = 0;
        dollsWait_ = {{3, w}};
        ndSkill_ = 5;
    }
    // the VIRTUAL DOLLS page: bot 1's two (as a player, the live level on), bot 2's one, none for bot 3
    {
        VdollBot a;
        a.bot = 1; a.count = 2; a.most = 3; a.teams = "auto"; a.level = "6"; a.weapons = "auto";
        a.names = {"Sokrovenno", "Rassvet"};
        a.controls = "player";
        a.adapt = true; a.adaptMin = 4; a.adaptMax = 8;
        a.levelsNow = {6.35f, 5.80f}; a.levelsNowSet = {6, 6}; a.appliedLevel = a.level;
        a.setInGame = 2; a.inGame = 2; a.applied = true; a.appliedNames = a.names; a.pending = 0;
        {
            VdollLive v; v.n = 1; v.player = 7; v.team = 0; v.deaths = 5; v.state = "in_game"; v.life = "alive"; a.live.push_back(v);
            v.n = 2; v.player = 8; v.team = 0; v.deaths = 7; v.life = "dead"; a.live.push_back(v);
        }
        VdollBot b;
        b.bot = 2; b.count = 1; b.most = 4; b.level = "5"; b.names = {"Krasnogorje"}; b.controls = "player";
        b.levelsNow = {5.0f}; b.levelsNowSet = {5}; b.appliedLevel = b.level; b.setInGame = 1; b.inGame = 1; b.applied = true;
        b.appliedNames = b.names; b.pending = 0;
        {
            VdollLive v; v.n = 1; v.player = 6; v.team = 1; v.deaths = 2; v.state = "in_game"; v.life = "alive"; b.live.push_back(v);
        }
        VdollBot c; c.bot = 3; c.count = 0; c.most = -1; c.pending = 0;
        vdolls_ = {a, b, c};
        vdLoaded_ = true;
    }
    ShowcaseCrashes();
}

// the crash reports the server keeps: two of last week, none of the last 24 hours (no bot's row says one)
void App::ShowcaseCrashes() {
    crNow_ = Utc("2026-11-18T19:20:00Z");
    crLoaded_ = true;
    auto add = [this](const char* src, const char* name, const char* reason, const char* time, int size, const char* what, const char* game, int rule,
                      int map, const char* players) {
        CrashInfo c;
        c.source = src; c.name = name; c.reason = reason; c.t = Utc(time); c.size = size; c.what = what;
        c.game = game; c.rule = rule; c.map = map; c.players = players;
        crashes_.push_back(c);
    };
    add("bot2", "2026-11-15_031502_hung.txt", "hung", "2026-11-15T03:15:02Z", 412345, "its game wrote nothing for 183 s (hung): restarted by the doll manager",
        "Night Capture", 3, 3, "6 (3 real, 2 dolls, 1 host)");
    add("doll2", "2026-11-12_224010_signal9.txt", "signal9", "2026-11-12T22:40:10Z", 180224, "the game was ended by signal 9 (killed: out of memory, or forced)",
        "Night Capture", 3, 12, "5 (3 real, 1 doll, 1 host)");
    static const char* const lines[] = {
        "MGO1 crash report (host/mgo1log, docs/CRASH_REPORTS.md)", "source: bot2", "reason: hung",
        "what: its game wrote nothing for 183 s (hung): restarted by the doll manager", "when: 2026-11-15T03:15:02.017Z",
        "version: build 1.00 d11286182942", "status: written 183 s before the end", "state: hosting", "frame: 1573208", "game: Night Capture",
        "round: 2 of 5 (rule 3, map 3)", "players: 6 (3 real, 2 dolls, 1 host)", "player: P1 host",
        "player: P2 real \"Granin64\", team red, 6 kills, 3 deaths, ping 64 ms", "player: P3 real \"CQC_Master\", team blue, 4 kills, 5 deaths, ping 88 ms",
        "vdoll: 1 in_game, player 6, team blue, spawned, 4 deaths", "pace: 59.94 VBlanks a second (59.94 due), 61 % of a core",
        "lines: 4000, the last ones before the end, from 2026-11-15T03:11:58.507Z", "----",
        "2026-11-15T03:11:59.011Z I ps2 f1572817: vdoll 1: dead, by player 3 (4 deaths)",
        "2026-11-15T03:12:06.954Z I ps2 f1573208: vdoll 1: respawns at -2000 464 -31500",
        "2026-11-15T03:15:01.001Z E mgo1doll-manager: bot2: its game wrote nothing for 183 s (hung): restarted",
        "2026-11-15T03:15:02.017Z W mgo1host: the game ended: its game was lost (no game for 30 s); a new game"};
    for (const char* l : lines) crLines_.push_back(l);
    crTotal_ = (int)crLines_.size();
}

// a page's own state over SetDemoPage's: the showcase's names in the menus, no message of the layout test
void App::ShowcasePage(int page) {
    toast_.clear();
    nbResult_.clear();
    demoScrollEnd_ = demoScrollEnd_ || showcaseEnd_;
    if (page == DEMO_MENU) { menuP_ = 3; menuName_ = "ThePain_Bees"; }
    if (page == DEMO_DOLLMENU) { dollMenuN_ = 1; dmPlayer_ = 0; }
    if (page == DEMO_CRASH) crOpen_ = "bot2/2026-11-15_031502_hung.txt";
}
