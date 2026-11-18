#!/usr/bin/env python3
"""The server's launchers (host/mgo1host.sh, host/mgo1doll.sh) with a stand-in for Wine, off line:
the game's lines in the journal and in the daily log with the account and passwords masked, the
start line, the end in words (a crash, a signal, a code, a stop by the service), the errors before
the game starts (a missing account, a missing disc file) in the daily log; (v141 beta 5) an accountless doll's name
(DOLL_NAME: given to the game whole, written for its bot's doll lobby; a bad one left for DOLL_<N>); (v141 beta 6) the
lobby connection lost (6) and HOST_LOBBY_GRACE, a doll's levels (1 to 10) and live bounds in its doll.ini and start line;
(v141 beta 7) HOST_UNRANKED given to the game (auto by default, a bad value warned); (virtual dolls) VDOLL_COUNT, VDOLL_TEAMS,
VDOLL_LEVEL (a bad one warned) and the names given to the game, what it was given kept in its run folder; (v150-host beta
16) the seat the virtual dolls' game keeps (MGO1_VDOLL_KEEP_SEAT), a doll at its bot's virtual dolls' level
(DOLL_DIFFICULTY=vdoll, the manager's dolls.levels), a doll's settings with carriage returns and quotes (the review's P2-11),
the virtual dolls' names file checked (P2-13); (v150-host beta 18) VDOLL_CONTROLS and DOLL_AIM given to the games (the
defaults, a bad word warned, a file from Windows), JOINER_HOLD_S (0 to 300, else nothing given); (v150-host beta 19) the virtual
dolls' live level (VDOLL_ADAPT, VDOLL_ADAPT_MIN, VDOLL_ADAPT_MAX: given to the game, the live file's lines after the count, the
run's vdolls.env; none set: nothing, the live file the count alone);
(v150-host beta 16) the crash reports (docs/CRASH_REPORTS.md): a game that ends badly (a crash, a kill, its game lost)
leaves <log>/<bot|doll>/crash/<UTC time>_<reason>.txt with its version, its last status and its last lines, masked as
the daily log; none for an end that is no failure nor for a stop by its service, unless the doll manager asked for one
(a hung game it restarts); (the review's P2-5) a bot's starts that end early (no account, a disc file missing, a game dead
in its first minutes) wait longer each time before the next one (run/../.next_start, waited by the service's ExecStartPre),
their reason written once; (P2-4) a rotation's maps 13 and up on a server without the PC maps left out of the game's settings.
    python3 tests/test_launchers.py
"""
import json, os, re, signal, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.join(HERE, "..", "host")
FAILS = []


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[-600:] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


FAKE_WINE = r'''#!/bin/bash
# a stand-in for wine: the game's lines (with the account and the passwords in them), then its end
[ "$1" = wineboot ] && exit 0
echo "ps2 f1: text: \"[Account ID]|Enter your account ID\" typed HOSTACC then TOPSECRET"
echo "ps2 f2: the game password GAMEPW in the settings"
echo "doll f3: activity: fighting PLAYER1"
echo "doll f4: stuck at 1 2 3 going to 4 5 6"
[ -n "${MGO1_DOLL_UID:-}" ] && echo "doll f5: identity [${MGO1_DOLL_NAME:-}] ${MGO1_DOLL_UID}"
[ -n "${MGO1_HOST_LOBBY_GRACE:-}" ] && echo "ps2 f6: host watch: lobby grace ${MGO1_HOST_LOBBY_GRACE} s"
[ -n "${MGO1_HOST_UNRANKED:-}" ] && echo "ps2 f7: unranked mode ${MGO1_HOST_UNRANKED}"
[ -n "${MGO1_VDOLLS:-}" ] && echo "ps2 f8: vdolls ${MGO1_VDOLLS} teams ${MGO1_VDOLL_TEAMS:-} level ${MGO1_VDOLL_LEVEL:-unset} names ${MGO1_VDOLL_NAMES:+given} live ${MGO1_VDOLL_LIVE:+given} keep ${MGO1_VDOLL_KEEP_SEAT:-1}"
echo "ps2 f9: switches controls ${MGO1_VDOLL_CONTROLS:-unset} aim ${MGO1_DOLL_AIM:-unset} hold ${MGO1_JOINER_HOLD_S-unset}"
echo "ps2 f10: vdoll adapt ${MGO1_VDOLL_ADAPT-unset} min ${MGO1_VDOLL_ADAPT_MIN-unset} max ${MGO1_VDOLL_ADAPT_MAX-unset}"
# (its status file: a bot's in its run folder, where it runs; a doll's in its own folder, above)
if [ -n "${FAKE_STATUS:-}" ]; then if [ -n "${MGO1_DOLL:-}" ]; then printf '%s\n' "$FAKE_STATUS" > ../status.json; else printf '%s\n' "$FAKE_STATUS" > status.json; fi; fi
if [ -n "${FAKE_SLEEP:-}" ]; then sleep "$FAKE_SLEEP"; fi
if [ "${FAKE_CODE:-0}" = 70 ]; then echo "CRASH: exception c0000005 at mgo1host.exe+1234 (reading 00000000)"; fi
exit "${FAKE_CODE:-0}"
'''


def setup():
    root = tempfile.mkdtemp(prefix="launchers_")
    home = os.path.join(root, "opt")
    for d in ("steps", "disc/MGS/NET", "disc/MODULES"):
        os.makedirs(os.path.join(home, d))
    for f in ("MGS3_N.ELF", "STAGE_N.DAT", "SLOT_N.DAT"):
        open(os.path.join(home, "disc/MGS/NET", f), "w").write("x")
    open(os.path.join(home, "VERSION"), "w").write("build 1.00 0123456789ab\n")
    for f in ("login_account.txt", "host_create.txt", "doll_join.txt"):
        open(os.path.join(home, "steps", f), "w").write("# step\n")
    open(os.path.join(home, "mgo1host.exe"), "w").write("x")
    wine = os.path.join(root, "wine")
    open(wine, "w").write(FAKE_WINE)
    os.chmod(wine, 0o755)
    game = os.path.join(root, "bot1.game")
    open(game, "w").write("name = TESTGAME\npassword = GAMEPW\n")
    return root, home, wine, game


def launch(script, root, home, wine, game, extra, code=0, sleep=None, term_after=None, before_term=None):
    env = dict(os.environ, HOME_DIR=home, DISC_DIR=os.path.join(home, "disc"), WINE=wine, GAME_SETTINGS=game,
               LOG_DIR=os.path.join(root, "log"), MGO1LOG=os.path.join(HOST, "mgo1log"), FAKE_CODE=str(code),
               SERVERS_FILE=os.path.join(root, "none.ini"), BANS_FILE=os.path.join(root, "bans"),
               WHITELIST_FILE=os.path.join(root, "white"), **extra)
    if sleep:
        env["FAKE_SLEEP"] = str(sleep)
    p = subprocess.Popen(["bash", os.path.join(HOST, script)], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         start_new_session=True)
    if term_after:
        time.sleep(term_after)
        if before_term:
            before_term()
        os.killpg(p.pid, signal.SIGTERM)             # as systemd stops a service: its whole group
    out, _ = p.communicate(timeout=60)
    return p.returncode, out


def day_log(root, name):
    p = os.path.join(root, "log", name, time.strftime("%Y-%m-%d", time.gmtime()) + ".log")
    return open(p).read() if os.path.exists(p) else ""


def reports(root, name):
    try:
        return sorted(x for x in os.listdir(os.path.join(root, "log", name, "crash")) if x.endswith(".txt"))
    except OSError:
        return []


def crash_reports(root, home, wine, game, hostenv):
    """(v150-host beta 16) A bot's crash reports (bot 2 here: a folder of its own)."""
    env = dict(hostenv, BOT="2", STATE_DIR=os.path.join(root, "bot2"), MGO1_CRASH_HOURLY="0",
               FAKE_STATUS=json.dumps({"v": 1, "frame": 7200, "state": "hosting", "game": {"name": "TESTGAME", "rules": [[3, 2]], "round": 0, "max": 9},
                                       "players": [{"p": 1, "name": "HOSTACC", "host": 1, "kind": "host"},
                                                   {"p": 2, "name": "PLAYER1", "host": 0, "kind": "real", "team": 1, "k": 2, "d": 1}],
                                       "players_real": 1, "players_dolls": 0, "players_host": 1}))
    today = time.strftime("%Y-%m-%d", time.gmtime())
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, code=70)
    names = reports(root, "bot2")
    rep = open(os.path.join(root, "log", "bot2", "crash", names[0])).read() if names else ""
    log = day_log(root, "bot2")
    check("host: a crash: its code as before, one crash report", rc == 70 and len(names) == 1 and re.match(r"^%s_\d{6}_crash70\.txt$" % today, names[0] if names else ""),
          (rc, names))
    check("host: the report: what ended, the version, the game's status", "source: bot2" in rep and "reason: crash70" in rep and "version: build 1.00 0123456789ab" in rep
          and "state: hosting" in rep and "round: 1 of 1 (rule 3, map 2)" in rep and 'player: P2 real "PLAYER1", team blue, 2 kills, 1 deaths' in rep, rep[:600])
    check("host: the report: the game's last lines, the crash's", "doll f3: activity: fighting PLAYER1" in rep and " E CRASH: exception c0000005" in rep, rep[-600:])
    check("host: the report: no account or password", not any(x in rep.upper() for x in ("HOSTACC", "TOPSECRET", "GAMEPW")), rep)
    check("host: the daily log names the report, then says the crash", names and log.find("mgo1log: crash report crash/" + names[0]) > 0
          and log.find(" E mgo1host: the game crashed") > log.find("mgo1log: crash report crash/"), log[-600:])
    check("host: the exit code in its run folder", open(os.path.join(root, "bot2", "run", "exit")).read() == "70\n")
    for code, why in ((0, None), (3, None), (2, "lost"), (6, "lost"), (137, "signal9"), (5, "exit5")):
        before = reports(root, "bot2")
        rc, out = launch("mgo1host.sh", root, home, wine, game, env, code=code)
        new = [x for x in reports(root, "bot2") if x not in before]
        check("host: exit code %d: %s, its code as before" % (code, "a report (%s)" % why if why else "no report"), rc == code and len(new) == (1 if why else 0)
              and (not why or re.search(r"_%s(_\d)?\.txt$" % why, new[0])), (rc, new))
    before = reports(root, "bot2")
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, sleep=20, term_after=5)
    check("host: stopped by its service: no report", rc == 0 and reports(root, "bot2") == before and "mgo1host: stopped" in day_log(root, "bot2")[-200:], (rc, reports(root, "bot2")))
    # the doll manager's own request (host/mgo1doll-manager crash_request), before it restarts a hung game
    import importlib.machinery, types
    saved = dict(os.environ)
    os.environ.update({"MGO1_LOG_DIR": os.path.join(root, "log"), "MGO1LOG": os.path.join(root, "none")})
    try:
        loader = importlib.machinery.SourceFileLoader("mgo1doll_manager_l", os.path.join(HOST, "mgo1doll-manager"))
        mgr = types.ModuleType(loader.name)
        loader.exec_module(mgr)
    finally:
        os.environ.clear()
        os.environ.update(saved)
    asked = {}

    def request():
        asked["report"] = mgr.crash_request("bot2", "its game wrote nothing for 183 s (hung): restarted by the doll manager")
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, sleep=20, term_after=5, before_term=request)
    path = asked.get("report") or ""
    rep = open(path).read() if os.path.exists(path) else ""
    check("host: a hung game restarted by the doll manager (its request, then the stop): the report where the manager says", rc == 0
          and os.path.dirname(path) == os.path.join(root, "log", "bot2", "crash") and mgr.crash_requested(path) and "reason: hung" in rep
          and "what: its game wrote nothing for 183 s (hung)" in rep and "doll f4: stuck at 1 2 3" in rep and "state: hosting" in rep
          and not os.path.exists(os.path.join(root, "log", "bot2", "crash", ".hung"))
          and not any(x in rep.upper() for x in ("HOSTACC", "TOPSECRET", "GAMEPW")), (rc, path, rep[:300]))
    # the logger of an older package (no crash reports) with this launcher: the game's lines as before
    old = os.path.join(root, "oldlog")
    with open(old, "w") as f:
        f.write("#!/bin/sh\n# stands for beta 15's mgo1log: --dir <folder> [--note <text> --level <L>]; anything else is an error\n"
                "[ \"$1\" = --dir ] || exit 2\nif [ $# -gt 2 ] && [ \"$3\" != --note ]; then exit 2; fi\n"
                "mkdir -p \"$2\"; if [ \"$3\" = --note ]; then echo \"$4\" >> \"$2/old.log\"; echo \"$4\"; else tee -a \"$2/old.log\"; fi\n")
    os.chmod(old, 0o755)
    envold = dict(env, BOT="3", STATE_DIR=os.path.join(root, "bot3"))
    e = dict(os.environ, HOME_DIR=home, DISC_DIR=os.path.join(home, "disc"), WINE=wine, GAME_SETTINGS=game, LOG_DIR=os.path.join(root, "log"), MGO1LOG=old,
             FAKE_CODE="70", SERVERS_FILE=os.path.join(root, "none.ini"), BANS_FILE=os.path.join(root, "bans"), WHITELIST_FILE=os.path.join(root, "white"), **envold)
    r = subprocess.run(["bash", os.path.join(HOST, "mgo1host.sh")], env=e, capture_output=True, text=True, timeout=60)
    oldlog = open(os.path.join(root, "log", "bot3", "old.log")).read() if os.path.exists(os.path.join(root, "log", "bot3", "old.log")) else ""
    check("host: an older mgo1log with this launcher: the game's lines and its end as before", r.returncode == 70 and "CRASH: exception" in oldlog
          and "mgo1host: the game crashed" in oldlog, (r.returncode, oldlog[-300:], r.stdout[-300:]))


def doll_crash_reports(root, home, wine, game, dollenv):
    """(v150-host beta 16) A doll's crash reports: its match lost (5) and a crash; back in the lobby (3), its game not listed (4)
    and a leave (0) are no failure."""
    env = dict(dollenv, MGO1_CRASH_HOURLY="0", FAKE_STATUS=json.dumps({"v": 1, "frame": 5400, "name": "DOLLACC", "game": "TESTGAME", "rule": "capture", "map": 2,
                                                                      "team": 1, "alive": 1, "k": 3, "d": 1, "act": "hunting", "identity": "uid"}))
    for code, why in ((3, None), (4, None), (0, None), (5, "lost"), (70, "crash70")):
        before = reports(root, "doll3")
        rc, out = launch("mgo1doll.sh", root, home, wine, game, env, code=code)
        new = [x for x in reports(root, "doll3") if x not in before]
        check("doll: exit code %d: %s, its code as before" % (code, "a report (%s)" % why if why else "no report"), rc == code and len(new) == (1 if why else 0)
              and (not why or re.search(r"_%s(_\d)?\.txt$" % why, new[0])), (rc, new))
    rep = open(os.path.join(root, "log", "doll3", "crash", reports(root, "doll3")[-1])).read() if reports(root, "doll3") else ""
    check("doll: the report: its source, its status (what it did, no name of its own), its lines, no account", "source: doll3" in rep and "game: TESTGAME" in rep
          and "act hunting" in rep and "rule capture" in rep and "doll f4: stuck at 1 2 3" in rep
          and not any(x in rep.upper() for x in ("DOLLACC", "DOLLPASS", "GAMEPW")), rep[:500])


def early_ends(root, home, wine, game, hostenv):
    """(the review's P2-5) A bot whose starts end early: the next start waits 30 s more each time (600 s at most), the wait made by
    the service's ExecStartPre from <state>/.next_start; a game that lasted, an end asked or a stop: no wait."""
    state = os.path.join(root, "bot4")
    env = dict(hostenv, BOT="4", STATE_DIR=state, MGO1_CRASH_LINES="0")

    def nxt():
        p = os.path.join(state, ".next_start")
        return int(open(p).read().strip()) - int(time.time()) if os.path.exists(p) else None
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, code=1)
    check("early end: the first one: no wait", rc == 1 and nxt() is None and "starts in a row" not in day_log(root, "bot4"), (rc, nxt()))
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, code=1)
    w2 = nxt()
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, code=70)
    w3 = nxt()
    log = day_log(root, "bot4")
    check("early end: the second: the next start in 30 s, the third: in 60 s, said as warnings", w2 is not None and 27 <= w2 <= 30 and 57 <= w3 <= 60
          and " W mgo1host: 2 starts in a row ended early: the next one in 30 s" in log and " W mgo1host: 3 starts in a row ended early: the next one in 60 s" in log, (w2, w3))
    open(os.path.join(state, ".quick_ends"), "w").write("40\n")
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, code=1)
    check("early end: 600 s at most", 595 <= (nxt() or 0) <= 600, nxt())
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(env, MGO1_QUICK_SECONDS="0"), code=2)
    check("early end: a game that lasted (10 minutes; 0 s here): no wait any more", nxt() is None and not os.path.exists(os.path.join(state, ".quick_ends")), nxt())
    launch("mgo1host.sh", root, home, wine, game, env, code=1)
    launch("mgo1host.sh", root, home, wine, game, env, code=1)
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, code=0)
    check("early end: an end that was asked (0): no wait any more", nxt() is None and not os.path.exists(os.path.join(state, ".quick_ends")), nxt())
    launch("mgo1host.sh", root, home, wine, game, env, code=1)
    launch("mgo1host.sh", root, home, wine, game, env, code=1)
    rc, out = launch("mgo1host.sh", root, home, wine, game, env, sleep=20, term_after=5)
    check("early end: a stop by its service: no wait any more", rc == 0 and nxt() is None and not os.path.exists(os.path.join(state, ".quick_ends")), (rc, nxt()))
    # a start that cannot work: its reason once in the bot's log, each time in the journal, the wait the same way
    state5 = os.path.join(root, "bot5")
    bad = dict(hostenv, BOT="5", STATE_DIR=state5, HOST_ACCOUNT_ID="")
    outs = [launch("mgo1host.sh", root, home, wine, game, bad) for _ in range(3)]
    log = day_log(root, "bot5")
    p5 = os.path.join(state5, ".next_start")
    w = int(open(p5).read().strip()) - int(time.time()) if os.path.exists(p5) else None
    check("no account three times: 1 each time, the reason once in the bot's log and each time in the journal, the third start in 60 s",
          [o[0] for o in outs] == [1, 1, 1] and log.count(" E mgo1host: HOST_ACCOUNT_ID is not set") == 1 and all("HOST_ACCOUNT_ID is not set" in o[1] for o in outs)
          and w is not None and 57 <= w <= 60 and " W mgo1host: 3 starts in a row ended early: the next one in 60 s (the same reason)" in log, (log[-400:], w))
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(bad, HOST_ACCOUNT_ID="bad name!"))
    log = day_log(root, "bot5")
    check("another reason (the account's characters): said, no 60 s of sleep in the script any more", rc == 1 and " E mgo1host: HOST_ACCOUNT_ID / HOST_ACCOUNT_PASSWORD may only hold" in log
          and "4 starts in a row ended early: the next one in 90 s" in log, log[-300:])
    # the service's ExecStartPre: the wait itself (systemd gives the command with $$ for $ and %% for %)
    unit = open(os.path.join(HOST, "mgo1host@.service")).read()
    m = re.search(r"^ExecStartPre=-/bin/sh -c '(.*)'$", unit, re.M)
    script = m.group(1).replace("$$", "$").replace("%%", "%") if m else "exit 9"
    check("the service: ExecStartPre (its failure ignored), a start's time limit above the longest wait", m is not None and re.search(r"^TimeoutStartSec=(7\d\d|[89]\d\d)$", unit, re.M)
          and "$" not in m.group(1).replace("$$", "") and "%" not in m.group(1).replace("%%", ""), unit[-600:])
    bindir = os.path.join(root, "bin")
    os.makedirs(bindir, exist_ok=True)
    with open(os.path.join(bindir, "sleep"), "w") as f:
        f.write("#!/bin/sh\necho \"$1\" >> \"$SLEPT\"\n")
    os.chmod(os.path.join(bindir, "sleep"), 0o755)
    slept = os.path.join(root, "slept")

    def pre(text):
        if os.path.exists(slept):
            os.remove(slept)
        if text is None:
            if os.path.exists(os.path.join(state, ".next_start")):
                os.remove(os.path.join(state, ".next_start"))
        else:
            open(os.path.join(state, ".next_start"), "w").write(text)
        r = subprocess.run(["/bin/sh", "-c", script], env=dict(os.environ, STATE_DIR=state, SLEPT=slept, PATH=bindir + ":" + os.environ["PATH"]), timeout=30)
        return r.returncode, (open(slept).read().split() if os.path.exists(slept) else []), os.path.exists(os.path.join(state, ".next_start"))
    rc, sl, there = pre("%d\n" % (int(time.time()) + 40))
    check("the service's wait: 40 s to the next start: slept, the file taken", rc == 0 and len(sl) == 1 and 37 <= int(sl[0]) <= 40 and not there, (rc, sl, there))
    for text, what in ((None, "no file"), ("%d\n" % (int(time.time()) - 5), "a time gone by"), ("soon\n", "no number"), ("%d\n" % (int(time.time()) + 5000), "more than 600 s ahead")):
        rc, sl, there = pre(text)
        check("the service's wait: %s: none" % what, rc == 0 and sl == [] and not there, (rc, sl, there))


def pc_maps_rotation(root, home, wine, game, hostenv):
    """(the review's P2-4) No PC maps on the server (maps 13 and up): a rotation's entries on them are left out of the game's copy
    of the settings; none left: no rotation given (the game keeps its account's last one, it was left with an empty one)."""
    def run_with(rotation, maps=False):
        g = os.path.join(root, "bot6.game")
        open(g, "w").write("name = TESTGAME\nrotation = %s\nmax_players = 9\n" % rotation)
        env = dict(hostenv, BOT="6", STATE_DIR=os.path.join(root, "bot6"), MGO1_CRASH_LINES="0", MAPS_DIR=os.path.join(root, "maps" if maps else "nomaps"))
        rc, out = launch("mgo1host.sh", root, home, wine, g, env)
        ini = open(os.path.join(root, "bot6", "run", "game.ini")).read()
        return [x for x in ini.splitlines() if x.startswith("rotation")], day_log(root, "bot6"), ini
    rot, log, ini = run_with("0:7, 3:2, 1:13, 4:15")
    check("rotation: maps 13 and up without the PC maps: left out, said", rot == ["rotation = 0:7, 3:2"] and "name = TESTGAME" in ini and "max_players = 9" in ini
          and " W mgo1host: no PC maps on this server (maps 13 and up): 2 of the rotation's 4 entries left out, the game gets 0:7, 3:2" in log, (rot, log[-300:]))
    rot, log, ini = run_with("1:13, 3:14,4:15")
    check("rotation: maps 13 and up only: no rotation given to the game, said", rot == [] and "name = TESTGAME" in ini
          and " W mgo1host: no PC maps on this server (maps 13 and up): none of the rotation's 3 entries can be played, the game keeps its account's last rotation" in log, (rot, log[-300:]))
    rot, log, ini = run_with("0:7, 3:2")
    check("rotation: no map above 12: as it is", rot == ["rotation = 0:7, 3:2"] and log.count("no PC maps on this server") == 2, rot)
    os.makedirs(os.path.join(root, "maps", "silo"))
    open(os.path.join(root, "maps", "silo", "map.ini"), "w").write("[map]\n")
    rot, log, ini = run_with("1:13, 3:14", maps=True)
    check("rotation: the PC maps are here: as it is", rot == ["rotation = 1:13, 3:14"] and log.count("no PC maps on this server") == 2, rot)


def main():
    os.chmod(os.path.join(HOST, "mgo1log"), 0o755)
    root, home, wine, game = setup()
    hostenv = {"BOT": "1", "STATE_DIR": os.path.join(root, "bot1"), "HOST_ACCOUNT_ID": "hostacc", "HOST_ACCOUNT_PASSWORD": "topsecret"}
    rc, out = launch("mgo1host.sh", root, home, wine, game, hostenv, code=70)
    log = day_log(root, "bot1")
    check("host: the start line in the daily log", "mgo1host: bot 1, UDP port 5730" in log and "version build 1.00 0123456789ab" in log, log)
    check("host: the game's lines there", "activity: fighting PLAYER1" in log and " W doll f4: stuck" in log, log)
    check("host: no account or password in the journal or the log", not any(x in (out + log).upper() for x in ("HOSTACC", "TOPSECRET", "GAMEPW")), out + log)
    check("host: a crash said as an error", " E CRASH:" in log and " E mgo1host: the game crashed" in log and rc == 70, log)
    rc, out = launch("mgo1host.sh", root, home, wine, game, hostenv, code=137)
    check("host: a kill said", "ended by signal 9 (killed" in day_log(root, "bot1"), day_log(root, "bot1"))
    rc, out = launch("mgo1host.sh", root, home, wine, game, hostenv, code=2)
    check("host: its game lost said as a warning", " W mgo1host: the game ended: its game was lost" in day_log(root, "bot1"))
    # (v141 beta 6) the lobby connection lost: the game ends with 6 after HOST_LOBBY_GRACE seconds (10 by default)
    check("host: HOST_LOBBY_GRACE 10 by default to the game", "host watch: lobby grace 10 s" in day_log(root, "bot1"), day_log(root, "bot1")[-800:])
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, HOST_LOBBY_GRACE="25"), code=6)
    log = day_log(root, "bot1")
    check("host: HOST_LOBBY_GRACE given to the game", "host watch: lobby grace 25 s" in log, log[-800:])
    check("host: the lobby connection lost (6) said as a warning", rc == 6 and
          " W mgo1host: the game ended: the lobby connection was lost (closed and not opened again in time); a new game" in log, log[-800:])
    # (v141 beta 7) SaveMGO's rankings and the dolls: HOST_UNRANKED, auto by default
    check("host: HOST_UNRANKED auto by default to the game", "unranked mode auto" in log, log[-800:])
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, HOST_UNRANKED="always"))
    log = day_log(root, "bot1")
    check("host: HOST_UNRANKED given to the game", "unranked mode always" in log, log[-800:])
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, HOST_UNRANKED="never"))
    log = day_log(root, "bot1")
    check("host: a bad HOST_UNRANKED warned, auto taken", " W mgo1host: HOST_UNRANKED must be auto, always or off (server.env): auto" in log and
          log.count("unranked mode auto") >= 2, log[-800:])
    # (virtual dolls) VDOLL_COUNT, VDOLL_TEAMS, VDOLL_LEVEL (1 to 10, or one a doll; a bad one warned, the game's 5) and the names
    # file given to the game; what it was given kept in its run folder (mgo1host-agent vdolls: "applied", "pending")
    applied = os.path.join(root, "bot1", "run", "vdolls.env")
    # (v150-host beta 15) none wanted: the settings and the live file given all the same (vdoll-count adds them live)
    check("host: no virtual doll wanted: 0 given with the live file (0), the run's vdolls.env says 0", "ps2 f8: vdolls 0 teams auto level unset names  live given" in log
          and open(os.path.join(root, "bot1", "run", "vdolls.live")).read() == "0\n"
          and open(applied).read() == "VDOLL_COUNT=0\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=direct\nVDOLL_ADAPT=0\nVDOLL_ADAPT_MIN=1\nVDOLL_ADAPT_MAX=10\n", log[-800:])
    vnames = os.path.join(root, "bot1.vdolls")
    open(vnames, "w").write("ALPHA\nBravo Two\n")
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="3", VDOLL_TEAMS="red,blue", VDOLL_LEVEL=" 3, 5,7",
                                                                 VDOLL_NAMES_FILE=vnames))
    log = day_log(root, "bot1")
    check("host: VDOLL_COUNT, VDOLL_TEAMS, VDOLL_LEVEL and the names given to the game", "vdolls 3 teams red,blue level 3,5,7 names given" in log
          and "mgo1host: 3 virtual doll(s) now (the count changes live), teams red,blue, level 3,5,7, user ids 0x7F000041 up" in log
          and open(os.path.join(root, "bot1", "run", "vdolls.live")).read() == "3\n", log[-800:])
    check("host: what the game was given in its run folder", open(applied).read() == "VDOLL_COUNT=3\nVDOLL_TEAMS=red,blue\nVDOLL_LEVEL=3,5,7\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=direct\nVDOLL_ADAPT=0\nVDOLL_ADAPT_MIN=1\nVDOLL_ADAPT_MAX=10\n"
          and open(os.path.join(root, "bot1", "run", "vdolls.names")).read() == "ALPHA\nBravo Two\n", open(applied).read())
    # (v150-host beta 16, review P2-13) a names file written by hand: the launcher gives the game the names the agent
    # would take, VDOLL<k> for the others (said, by the dolls' numbers: a log never holds a name nobody chose to show)
    run_names = os.path.join(root, "bot1", "run", "vdolls.names")
    before = day_log(root, "bot1")
    open(vnames, "wb").write(b"ALPHA\r\n\r\nAB\r\nBravo  Two\nalpha\n Lead\nTrail  \nDOLL_3\nABCDEFGHIJKLMNOP\nCaf\xc3\xa9 Noir\nBad;Name\n"
                             b"vdoll2\nOk.Name_1!?+*\n" + b"X" * 80 + b"\nN14\nN15\nN16\nN17")
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="3", VDOLL_NAMES_FILE=vnames))
    log = day_log(root, "bot1")[len(before):]
    want = ["ALPHA", "VDOLL2", "VDOLL3", "VDOLL4", "VDOLL5", "Trail", "VDOLL7", "VDOLL8", "VDOLL9", "VDOLL10", "VDOLL11", "Ok.Name_1!?+*",
            "VDOLL13", "N14", "N15"]
    got = open(run_names).read()
    check("host: a names file by hand: the valid names kept at their doll's rank, VDOLL<k> for the others, 15 at most, no carriage return",
          got == "".join(x + "\n" for x in want), repr(got))
    check("host: the refused names said once, by the dolls' numbers (2 letters, two spaces, taken, a space first, DOLL_<n>, 16 letters, "
          "a letter of two bytes, a sign, a default name taken, 80 letters)",
          log.count(" W mgo1host: bot1.vdolls: the name of virtual doll 2, 3, 4, 5, 7, 8, 9, 10, 11, 13 is not one the game shows") == 1
          and "names given" in log and "Caf" not in log and "Bad;Name" not in log, log[-900:])
    before = day_log(root, "bot1")
    open(vnames, "w").write("ALPHA\nBravo Two\n")
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="3", VDOLL_NAMES_FILE=vnames))
    check("host: the names the agent writes: given as they are, nothing said", open(run_names).read() == "ALPHA\nBravo Two\n"
          and "is not one the game shows" not in day_log(root, "bot1")[len(before):], day_log(root, "bot1")[len(before):][-500:])
    before = day_log(root, "bot1")
    open(vnames, "w").write("")
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="3", VDOLL_NAMES_FILE=vnames, LC_ALL="C.UTF-8", LANG="en_US.UTF-8"))
    check("host: an empty names file: an empty copy (every doll VDOLL<n>), nothing said", open(run_names).read() == ""
          and "names given" in day_log(root, "bot1")[len(before):] and "is not one the game shows" not in day_log(root, "bot1")[len(before):])
    open(vnames, "wb").write("Caf\u00e9 Noir\n\u00c9LAN\nGood One\n".encode())
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="3", VDOLL_NAMES_FILE=vnames, LC_ALL="C.UTF-8", LANG="en_US.UTF-8"))
    check("host: under a UTF-8 locale too a letter outside A to Z is refused", open(run_names).read() == "VDOLL1\nVDOLL2\nGood One\n", repr(open(run_names).read()))
    open(vnames, "w").write("ALPHA\nBravo Two\n")
    warning = " W mgo1host: VDOLL_LEVEL must be 1 to 10, or one a doll (3,5,7) (bot1.env): 5"
    for bad in ("11", "0", "3,,5", "5;7", ",".join(["5"] * 16)):
        warned = day_log(root, "bot1").count(warning)
        rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="2", VDOLL_LEVEL=bad))
        log = day_log(root, "bot1")
        check("host: VDOLL_LEVEL %r warned, not given (the game's 5)" % bad[:12], log.count(warning) == warned + 1
              and "vdolls 2 teams auto level unset" in log.split("VDOLL_LEVEL must")[-1]
              and open(applied).read() == "VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=direct\nVDOLL_ADAPT=0\nVDOLL_ADAPT_MIN=1\nVDOLL_ADAPT_MAX=10\n", log[-600:])
    rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="1", VDOLL_LEVEL="10"))
    check("host: VDOLL_LEVEL 10 given", "vdolls 1 teams auto level 10 names" in day_log(root, "bot1").split("VDOLL_LEVEL must")[-1], day_log(root, "bot1")[-600:])
    # the seat kept for a real player, the virtual dolls' side of it: the game keeps one (MGO1_VDOLL_KEEP_SEAT, 1) unless
    # DOLL_KEEP_SEAT=0 and the dolls are off the lobby's list (DOLL_LIST=off), as mgo1doll-manager counts its free seats
    for env, keep in (({}, 1), ({"DOLL_KEEP_SEAT": "0"}, 1), ({"DOLL_KEEP_SEAT": "0", "DOLL_LIST": "host"}, 1), ({"DOLL_LIST": "off"}, 1),
                      ({"DOLL_KEEP_SEAT": "0", "DOLL_LIST": "off"}, 0)):
        before = day_log(root, "bot1")
        rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="3", **env))
        check("host: %s: the virtual dolls' game keeps %d seat" % (" ".join("%s=%s" % kv for kv in env.items()) or "the defaults", keep),
              "vdolls 3 teams auto level unset names  live given keep %d" % keep in day_log(root, "bot1")[len(before):], day_log(root, "bot1")[len(before):])
    # (v150-host beta 18) VDOLL_CONTROLS: player given to the game as MGO1_VDOLL_CONTROLS=player; direct, unset (an old
    # server's bot<N>.env) or another word (warned): nothing given; kept in the run's vdolls.env; carriage returns and quotes
    # of a file from Windows taken off
    cwarn = " W mgo1host: VDOLL_CONTROLS must be player or direct (bot1.env): direct"
    for given, want in ((None, "direct"), ("direct", "direct"), ("player", "player"), ("PLAYER", "player"), ('"player"\r', "player"),
                        ("'direct'\r", "direct"), ("pad", "bad"), ("as a player", "bad")):
        before = day_log(root, "bot1")
        e = dict(hostenv, VDOLL_COUNT="2")
        if given is not None:
            e["VDOLL_CONTROLS"] = given
        rc, out = launch("mgo1host.sh", root, home, wine, game, e)
        log = day_log(root, "bot1")[len(before):]
        got = "player" if want == "player" else "direct"
        check("host: VDOLL_CONTROLS %r: %s" % (given, "MGO1_VDOLL_CONTROLS=player given" if got == "player" else "nothing given (direct)" + (", warned" if want == "bad" else "")),
              ("switches controls %s aim unset" % ("player" if got == "player" else "unset")) in log
              and open(applied).read() == "VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=%s\nVDOLL_ADAPT=0\nVDOLL_ADAPT_MIN=1\nVDOLL_ADAPT_MAX=10\n" % got
              and "user ids 0x7F000041 up, controls %s" % got in log and (cwarn in log) == (want == "bad"), log[-700:] + open(applied).read())
    # (v150-host beta 19) the virtual dolls' live level: VDOLL_ADAPT=1 given as MGO1_VDOLL_ADAPT=1, 0 or unset nothing; the bounds
    # 1 to 10 given when set (swapped when the wrong way round, a bad one warned and not given); none of the three set: nothing
    # given and the live file the count alone (beta 18's); any set: the live file's three lines after the count (the count
    # first and whole: a game of beta 18 reads it alone), the run's vdolls.env and the start line say them
    vlive = os.path.join(root, "bot1", "run", "vdolls.live")
    aw = " W mgo1host: VDOLL_ADAPT must be 1 or 0 (bot1.env): 0"
    for given, want in (({}, ("unset", "unset", "unset", None)),
                        ({"VDOLL_ADAPT": "1"}, ("1", "unset", "unset", (1, 1, 10))),
                        ({"VDOLL_ADAPT": "0"}, ("unset", "unset", "unset", (0, 1, 10))),
                        ({"VDOLL_ADAPT": '"1"\r', "VDOLL_ADAPT_MIN": " 3\r", "VDOLL_ADAPT_MAX": "'08'"}, ("1", "3", "8", (1, 3, 8))),
                        ({"VDOLL_ADAPT": "1", "VDOLL_ADAPT_MIN": "9", "VDOLL_ADAPT_MAX": "4"}, ("1", "4", "9", (1, 4, 9))),
                        ({"VDOLL_ADAPT": "1", "VDOLL_ADAPT_MAX": "6"}, ("1", "unset", "6", (1, 1, 6))),
                        ({"VDOLL_ADAPT_MIN": "2"}, ("unset", "2", "unset", (0, 2, 10))),
                        ({"VDOLL_ADAPT": "", "VDOLL_ADAPT_MIN": "", "VDOLL_ADAPT_MAX": ""}, ("unset", "unset", "unset", None)),
                        ({"VDOLL_ADAPT": "on"}, ("unset", "unset", "unset", (0, 1, 10)))):
        before = day_log(root, "bot1")
        rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="2", VDOLL_LEVEL="10", **given))
        log = day_log(root, "bot1")[len(before):]
        a, lo, hi, lv = want
        lt = open(vlive).read()
        on = lv is not None and lv[0] == 1
        check("host: live level %s: the game gets adapt %s min %s max %s, the live file %r" % (given or "unset", a, lo, hi, lt),
              "vdoll adapt %s min %s max %s" % (a, lo, hi) in log
              and lt == ("2\n" if lv is None else "2\nadapt %d\nadapt_min %d\nadapt_max %d\n" % lv)
              and open(applied).read().endswith("VDOLL_ADAPT=%d\nVDOLL_ADAPT_MIN=%d\nVDOLL_ADAPT_MAX=%d\n" % (lv or (0, 1, 10)))
              and ("controls direct, live level %s" % ("on between %d and %d" % lv[1:] if on else "off")) in log
              and (aw in log) == (given.get("VDOLL_ADAPT") == "on"), log[-700:] + lt)
    for key, bad in (("VDOLL_ADAPT_MIN", "0"), ("VDOLL_ADAPT_MIN", "11"), ("VDOLL_ADAPT_MAX", "x"), ("VDOLL_ADAPT_MAX", "-3"), ("VDOLL_ADAPT_MIN", "5.5")):
        before = day_log(root, "bot1")
        rc, out = launch("mgo1host.sh", root, home, wine, game, dict(hostenv, VDOLL_COUNT="1", VDOLL_ADAPT="1", **{key: bad}))
        log = day_log(root, "bot1")[len(before):]
        dflt = 1 if key.endswith("MIN") else 10
        check("host: %s=%s warned, not given, the default %d in the live file" % (key, bad, dflt),
              " W mgo1host: %s must be 1 to 10 (bot1.env): %d" % (key, dflt) in log and "vdoll adapt 1 min unset max unset" in log
              and open(vlive).read() == "1\nadapt 1\nadapt_min 1\nadapt_max 10\n", log[-600:])
    # (v150-host beta 18) JOINER_HOLD_S (server.env, a bot's bot<N>.env over it): 0 to 300 given as MGO1_JOINER_HOLD_S and
    # said; unset or empty (an old server's files): nothing given (the game's 60); out of range or not a number: warned,
    # nothing given; a value from a Windows file (carriage return, quotes) read as systemd gives it
    hwarn = " W mgo1host: JOINER_HOLD_S must be 0 to 300 (seconds) (server.env or bot1.env): the game's 60"
    for given, want in ((None, None), ("", None), ("60", "60"), ("0", "0"), ("300", "300"), ("045", "45"), ('"120"\r', "120"),
                        ("301", "bad"), ("-5", "bad"), ("1e2", "bad"), ("sixty", "bad"), ("9999", "bad")):
        before = day_log(root, "bot1")
        e = dict(hostenv)
        if given is not None:
            e["JOINER_HOLD_S"] = given
        rc, out = launch("mgo1host.sh", root, home, wine, game, e)
        log = day_log(root, "bot1")[len(before):]
        ok = want not in (None, "bad")
        check("host: JOINER_HOLD_S %r: %s" % (given, "MGO1_JOINER_HOLD_S=%s given and said" % want if ok else "nothing given (the game's 60)" + (", warned" if want == "bad" else "")),
              ("hold %s" % (want if ok else "unset")) in log and (hwarn in log) == (want == "bad")
              and (("mgo1host: a joiner still loading kept until %s s of silence (JOINER_HOLD_S" % want) in log) == ok, log[-600:])
    unit = open(os.path.join(HOST, "mgo1host@.service")).read()
    check("host: the service reads server.env, then bot<N>.env (a bot's JOINER_HOLD_S over the server's)",
          0 < unit.find("EnvironmentFile=-/etc/mgo1host/server.env") < unit.find("EnvironmentFile=/etc/mgo1host/bot%i.env"), unit)
    for ex, line in (("server.env.example", "# JOINER_HOLD_S=60"), ("bot.env.example", "#JOINER_HOLD_S=60")):
        check("host: %s tells JOINER_HOLD_S" % ex, line in open(os.path.join(HOST, ex)).read())
    rc, out = launch("mgo1host.sh", root, home, wine, game, hostenv, sleep=20, term_after=3)
    check("host: stopped by its service said", "mgo1host: stopped (its service was stopped)" in day_log(root, "bot1"), day_log(root, "bot1")[-500:])
    crash_reports(root, home, wine, game, hostenv)
    early_ends(root, home, wine, game, hostenv)
    pc_maps_rotation(root, home, wine, game, hostenv)
    bad = dict(hostenv, HOST_ACCOUNT_ID="")
    rc, out = launch("mgo1host.sh", root, home, wine, game, bad)
    check("host: no account: an error in the log", " E mgo1host: HOST_ACCOUNT_ID is not set" in day_log(root, "bot1") and rc == 1, out)

    dollenv = {"DOLL": "3", "STATE_DIR": os.path.join(root, "doll3"), "DOLL_ACCOUNT_ID": "dollacc", "DOLL_ACCOUNT_PASSWORD": "dollpass",
               "DOLL_BOT": "1"}
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dollenv, code=3)
    log = day_log(root, "doll3")
    check("doll: the start line", "mgo1doll: doll 3 for bot 1, UDP port 5742" in log, log)
    check("doll: back in the lobby said", "mgo1doll: back in the lobby after a match" in log, log)
    check("doll: no account or password", not any(x in (out + log).upper() for x in ("DOLLACC", "DOLLPASS", "GAMEPW")), out + log)
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dollenv, code=0)
    check("doll: left the game said", "mgo1doll: left the game (ordered)" in day_log(root, "doll3"))
    # (v141 beta 6) the ladder 1 to 10 and the live level's bounds in doll.ini (adapt_min, adapt_max: 1 to 10, else 1 and 10,
    # the lower first) and in the start line with their tiers' names
    ini = os.path.join(root, "doll3", "run", "doll.ini")
    text = open(ini).read() if os.path.exists(ini) else ""
    check("doll: doll.ini's levels by default", "difficulty=3\nadapt=1\nadapt_min=1\nadapt_max=10\n" in text, text)
    check("doll: the start line's levels by default", "difficulty 3 (NORMAL), live level on between 1 (TRAINING) and 10 (PRO), team auto"
          in day_log(root, "doll3"), day_log(root, "doll3")[-800:])
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dict(dollenv, DOLL_DIFFICULTY="8", DOLL_ADAPT="0", DOLL_ADAPT_MIN="9",
                                                                 DOLL_ADAPT_MAX="4"), code=0)
    text = open(ini).read() if os.path.exists(ini) else ""
    log = day_log(root, "doll3")
    check("doll: doll.ini's levels set, the bounds the lower first", "difficulty=8\nadapt=0\nadapt_min=4\nadapt_max=9\n" in text, text)
    check("doll: the start line says them", "difficulty 8 (MASTER), live level off between 4 (HARD) and 9 (EXTREME)" in log, log[-800:])
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dict(dollenv, DOLL_DIFFICULTY="11", DOLL_ADAPT_MIN="0", DOLL_ADAPT_MAX="x"), code=0)
    text = open(ini).read() if os.path.exists(ini) else ""
    log = day_log(root, "doll3")
    check("doll: levels out of range: 3, 1 and 10", "difficulty=3\nadapt=1\nadapt_min=1\nadapt_max=10\n" in text, text)
    check("doll: levels out of range said as warnings", " W mgo1doll: DOLL_DIFFICULTY must be 1 to 10" in log and " W mgo1doll: DOLL_ADAPT_MIN must be 1 to 10"
          in log and " W mgo1doll: DOLL_ADAPT_MAX must be 1 to 10" in log, log[-1200:])
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dict(dollenv, DOLL_DIFFICULTY="10", DOLL_ADAPT_MIN="10", DOLL_ADAPT_MAX="10"), code=0)
    text = open(ini).read() if os.path.exists(ini) else ""
    check("doll: 10 PRO at both bounds", "difficulty=10\nadapt=1\nadapt_min=10\nadapt_max=10\n" in text
          and "difficulty 10 (PRO), live level on between 10 (PRO) and 10 (PRO)" in day_log(root, "doll3"), text)
    # (v150-host beta 16) DOLL_DIFFICULTY=vdoll: its bot's virtual dolls' level, from the manager's dolls.levels beside the dolls'
    # folders (`<doll> <level> <bot> <the bot's VDOLL_LEVEL>`: the doll's own user reads neither its bot's settings nor the
    # other dolls'), the live level off whatever DOLL_ADAPT says, said in its start line; no line for it: 5, a warning
    levels = os.path.join(root, "dolls.levels")
    open(levels, "w").write("1 10 2 10,9,8\n3 9 2 10,9,8\n13 8 2 10,9,8\n")
    follow = dict(dollenv, DOLL_BOT="2", DOLL_DIFFICULTY="vdoll", DOLL_ADAPT="1", DOLL_ADAPT_MIN="7", DOLL_ADAPT_MAX="10")
    rc, out = launch("mgo1doll.sh", root, home, wine, game, follow, code=0)
    text = open(ini).read() if os.path.exists(ini) else ""
    log = day_log(root, "doll3")
    check("doll: DOLL_DIFFICULTY=vdoll: its line's level (doll 3: 9, not doll 13's) and the live level off in doll.ini, DOLL_ADAPT=1 or not",
          "difficulty=9\nadapt=0\nadapt_min=7\nadapt_max=10\n" in text and "bot=2\n" in text, text)
    check("doll: its start line says whose level it is", "mgo1doll: doll 3 for bot 2, UDP port 5742, difficulty 9 (EXTREME), as bot 2's virtual dolls "
          "(VDOLL_LEVEL 10,9,8), live level off, team auto" in log, log[-600:])
    open(levels, "w").write("3 10 2 10\n")
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dict(follow, DOLL_ADAPT="0"), code=0)
    check("doll: the level 10 for all (PRO)", "difficulty=10\nadapt=0\n" in open(ini).read()
          and "difficulty 10 (PRO), as bot 2's virtual dolls (VDOLL_LEVEL 10), live level off" in day_log(root, "doll3"), day_log(root, "doll3")[-600:])
    for content in ("1 10 2 10\n", "3 vdoll 2 x\n", None):
        if content is None:
            os.remove(levels)
        else:
            open(levels, "w").write(content)
        before = day_log(root, "doll3")
        rc, out = launch("mgo1doll.sh", root, home, wine, game, follow, code=0)
        log = day_log(root, "doll3")[len(before):]
        check("doll: %s: 5 (the virtual dolls' own), the live level off, a warning" % ("no levels file" if content is None else "no level for it in the file (%r)" % content),
              "difficulty=5\nadapt=0\n" in open(ini).read() and " W mgo1doll: DOLL_DIFFICULTY=vdoll: no level for doll 3 in " in log
              and "difficulty 5 (EXPERT), as bot 2's virtual dolls (their level not known: 5), live level off" in log, log[-700:])
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dict(dollenv, DOLL_DIFFICULTY="vdolls"), code=0)
    check("doll: another word than vdoll: 3, the warning as before", "difficulty=3\nadapt=1\n" in open(ini).read()
          and " W mgo1doll: DOLL_DIFFICULTY must be 1 to 10" in day_log(root, "doll3"), day_log(root, "doll3")[-500:])
    # (v150-host beta 18) DOLL_AIM: direct given to the game as MGO1_DOLL_AIM=direct and aim=direct in doll.ini; pad, unset (an
    # old server's doll<N>.env) or another word (warned): nothing given, aim=pad; said in its start line
    awarn = " W mgo1doll: DOLL_AIM must be direct or pad (/etc/mgo1host/doll3.env): pad"
    for given, want in ((None, "pad"), ("pad", "pad"), ("direct", "direct"), ("Direct", "direct"), ("stick", "bad"), ("player", "bad")):
        before = day_log(root, "doll3")
        e = dict(dollenv)
        if given is not None:
            e["DOLL_AIM"] = given
        rc, out = launch("mgo1doll.sh", root, home, wine, game, e, code=0)
        log = day_log(root, "doll3")[len(before):]
        got = "direct" if want == "direct" else "pad"
        text = open(ini).read() if os.path.exists(ini) else ""
        check("doll: DOLL_AIM %r: %s" % (given, "MGO1_DOLL_AIM=direct given, aim=direct" if got == "direct" else "nothing given, aim=pad" + (", warned" if want == "bad" else "")),
              ("switches controls unset aim %s" % ("direct" if got == "direct" else "unset")) in log and "aim=%s\n" % got in text
              and "team auto, aim %s, its account" % got in log and (awarn in log) == (want == "bad"), log[-700:] + text)
    # (v141 beta 5) an accountless doll with a name of its own: the game gets it whole (spaces and all), its folder has it
    # for the doll lobby; a name the game would not show: DOLL_<N>, said
    acl = {"DOLL": "4", "STATE_DIR": os.path.join(root, "doll4"), "DOLL_BOT": "1", "DOLL_NAME": "Big Boss 2!"}
    rc, out = launch("mgo1doll.sh", root, home, wine, game, acl, code=0)
    log = day_log(root, "doll4")
    shown = open(os.path.join(root, "doll4", "display_name")).read() if os.path.exists(os.path.join(root, "doll4", "display_name")) else ""
    check("doll: an accountless doll's name given to the game whole", "identity [Big Boss 2!] 0x7F000004" in log, log)
    check("doll: its name written for the doll lobby", shown == "Big Boss 2!\n", repr(shown))
    check("doll: its start line says the name", "named Big Boss 2!" in log, log)
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dict(acl, DOLL_NAME="Bad;Name"), code=0)
    log = day_log(root, "doll4")
    shown = open(os.path.join(root, "doll4", "display_name")).read() if os.path.exists(os.path.join(root, "doll4", "display_name")) else ""
    check("doll: a bad name: DOLL_<N>, a warning", "identity [DOLL_4] 0x7F000004" in log and " W mgo1doll: DOLL_NAME is not a name" in log
          and shown == "DOLL_4\n", log[-800:] + repr(shown))
    # (v150-host beta 16, review P2-11) a doll's settings written by hand on Windows: carriage returns and quotes about
    # the values, as a reader that does not take them off gives them: the launcher takes them off (as mgo1doll-manager does
    # when it reads the same file), so the game gets the name the manager lists
    before = day_log(root, "doll4")
    crlf = dict(acl, DOLL_NAME='"Big Boss 2!"\r', DOLL_BOT="1\r", DOLL_DIFFICULTY="'7'\r", DOLL_TEAM='"red"\r', DOLL_ADAPT="0\r",
                DOLL_ADAPT_MIN="2\r", DOLL_ADAPT_MAX='"9"', DOLL_AIM='"direct"\r')
    rc, out = launch("mgo1doll.sh", root, home, wine, game, crlf, code=0)
    log = day_log(root, "doll4")[len(before):]
    shown = open(os.path.join(root, "doll4", "display_name")).read() if os.path.exists(os.path.join(root, "doll4", "display_name")) else ""
    ini4 = open(os.path.join(root, "doll4", "run", "doll.ini")).read() if os.path.exists(os.path.join(root, "doll4", "run", "doll.ini")) else ""
    check("doll: a quoted name with a carriage return: the name itself to the game and to the doll lobby, no warning",
          "identity [Big Boss 2!] 0x7F000004" in log and shown == "Big Boss 2!\n" and " W mgo1doll: " not in log, log[-800:] + repr(shown))
    check("doll: its other settings with carriage returns and quotes: the difficulty 7, the live level off, bounds 2 to 9, the red team",
          "difficulty=7\nadapt=0\nadapt_min=2\nadapt_max=9\n" in ini4 and "team=red" in ini4 and "\r" not in ini4 and '"' not in ini4
          and "doll 4 for bot 1," in log and "team red" in log, ini4 + log[-500:])
    check("doll: (beta 18) DOLL_AIM=\"direct\" with a carriage return: direct aim given, aim=direct", "aim=direct\n" in ini4
          and "switches controls unset aim direct" in log and "team red, aim direct," in log, ini4 + log[-500:])
    sys.path.insert(0, os.path.join(HERE, "..", "host"))
    import importlib.machinery, importlib.util
    ldr = importlib.machinery.SourceFileLoader("mgr_unquote", os.path.join(HERE, "..", "host", "mgo1doll-manager"))
    mgr = importlib.util.module_from_spec(importlib.util.spec_from_loader("mgr_unquote", ldr))
    ldr.exec_module(mgr)
    envp = os.path.join(root, "doll4.crlf.env")
    with open(envp, "wb") as f:
        f.write(b'DOLL_BOT=1\r\nDOLL_NAME="Big Boss 2!"\r\nDOLL_DIFFICULTY=\'7\'\r\n')
    got = mgr.env_file(envp)
    check("doll: mgo1doll-manager reads the same lines the same way (the name it lists is the game's)",
          got == {"DOLL_BOT": "1", "DOLL_NAME": "Big Boss 2!", "DOLL_DIFFICULTY": "7"}, got)
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dict(acl, DOLL_NAME='"'), code=0)
    check("doll: a lone quote as a name: refused as before (DOLL_<N>, a warning)", " W mgo1doll: DOLL_NAME is not a name" in day_log(root, "doll4")[len(before):]
          and "identity [DOLL_4] 0x7F000004" in day_log(root, "doll4")[len(before) + len(log):], day_log(root, "doll4")[-400:])
    doll_crash_reports(root, home, wine, game, dollenv)
    os.remove(os.path.join(home, "disc/MGS/NET/SLOT_N.DAT"))
    rc, out = launch("mgo1doll.sh", root, home, wine, game, dollenv)
    check("doll: a missing disc file: an error in the log", " E mgo1doll: " in day_log(root, "doll3") and "SLOT_N.DAT is missing" in day_log(root, "doll3"), out)
    print("%d failed" % len(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
