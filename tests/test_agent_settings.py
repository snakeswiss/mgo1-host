#!/usr/bin/env python3
"""The agent's game settings (host/mgo1host-agent settings-set / settings-get), off line in a temporary
folder. Checks: valid and invalid rotations and values (the error codes and arguments MGO1 Host.exe
translates), the file written in the keys' order, a refused set leaving the file as it was, the keys and
limits as before the rules and maps became a table (RULES, MAPS), host/game.ini.example valid, the
program's table (hostpanel/src/rules.cpp) the same as the agent's, and a table edit (a rule and two maps
more, some maps not in every rule) followed by the checks with nothing else changed; (v141 beta 6) status and bots
with a stand-in game process (the players' kinds counted, the lobby lost's countdown); (virtual dolls) vdoll-set's level and
vdolls' applied settings and pending; (v150-host beta 16) crashes and crash (the crash reports host/mgo1log writes: the list
with each header's fields, one report's lines by pages, the log-in data masked, the names checked); (v150-host beta 18) the
virtual dolls' controls and the playable dolls' aim (switches_check).
    python3 tests/test_agent_settings.py
"""
import importlib.machinery, importlib.util, json, os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
AGENT = os.path.join(HERE, "..", "host", "mgo1host-agent")
EXAMPLE = os.path.join(HERE, "..", "host", "game.ini.example")
RULES_CPP = os.path.join(HERE, "..", "hostpanel", "src", "rules.cpp")
FAILS = []

# the keys, kinds and limits of agent 2.4 before the table (settings-get's "keys", in this order)
KEYS_2_4 = [
    ("name", ["text", 3, 15]), ("comment", ["text", 0, 75]), ("password", ["text", 0, 15]), ("rotation", ["rotation", 1, 15]),
    ("red_team", ["int", 0, 2]), ("blue_team", ["int", 0, 2]), ("max_players", ["int", 3, 16]), ("weapons", ["int", 0, 7]),
    ("dm_minutes", ["int", 1, 99]), ("tdm_minutes", ["int", 1, 99]), ("tdm_rounds", ["int", 1, 99]), ("tdm_tickets", ["int", 1, 99]),
    ("res_minutes", ["int", 1, 99]), ("res_rounds", ["int", 1, 99]), ("cap_minutes", ["int", 1, 99]), ("cap_rounds", ["int", 1, 99]),
    ("sne_minutes", ["int", 1, 99]), ("sne_rounds", ["int", 1, 99]), ("auto_balance", ["int", 2, 6]), ("idle_kick_minutes", ["int", 1, 99]),
    ("team_kill_count", ["int", 1, 99]), ("rating_restriction", ["int", 0, 2]), ("rating", ["int", 100, 9900]),
    ("unique_characters", ["switch"]), ("rumble_roses", ["switch"]), ("ghosts", ["switch"]), ("friendly_fire", ["switch"]),
    ("team_auto_balance", ["switch"]), ("idle_kick", ["switch"]), ("team_kill_kick", ["switch"]), ("voice_chat", ["switch"])]


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:400] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


def load(path, name):
    loader = importlib.machinery.SourceFileLoader(name, path)
    spec = importlib.util.spec_from_loader(name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


# (v141 beta 6) a stand-in systemctl: every unit active and enabled
SYSTEMCTL = '#!/bin/sh\ncase "$1" in is-active) echo active ;; is-enabled) echo enabled ;; esac\nexit 0\n'


class Server:
    """A server's folders for the agent: bot 1 with an account, its game file."""
    def __init__(self, agent):
        self.agent = agent
        self.root = os.path.realpath(tempfile.mkdtemp(prefix="agentsettings_"))
        self.etc = os.path.join(self.root, "etc")
        os.makedirs(self.etc)
        with open(os.path.join(self.etc, "bot1.env"), "w") as f:
            f.write("HOST_ACCOUNT_ID=HOSTACC\nHOST_ACCOUNT_PASSWORD=TOPSECRET\nUDP_PORT=5730\n")
        systemctl = os.path.join(self.root, "systemctl")
        with open(systemctl, "w") as f:
            f.write(SYSTEMCTL)
        os.chmod(systemctl, 0o755)
        self.env = dict(os.environ, MGO1_CONF_DIR=self.etc, MGO1_STATE_DIR=os.path.join(self.root, "var"),
                        MGO1_LOG_DIR=os.path.join(self.root, "log"), MGO1_HOME_DIR=os.path.join(self.root, "opt"),
                        MGO1_SYSTEMCTL=systemctl, MGO1_SYSTEMD_DIR=os.path.join(self.root, "systemd"))

    def run(self, args, stdin=""):
        p = subprocess.run([sys.executable, self.agent] + args, input=stdin, capture_output=True, text=True, timeout=60, env=self.env)
        try:
            return p.returncode, json.loads(p.stdout.splitlines()[0])
        except (ValueError, IndexError):
            return p.returncode, {"raw": p.stdout + p.stderr}

    def game(self):
        try:
            return open(os.path.join(self.etc, "bot1.game")).read()
        except OSError:
            return ""

    def set(self, d):
        return self.run(["settings-set"], json.dumps(d))


def refused(srv, what, d, code, args=None):
    before = srv.game()
    rc, j = srv.set(d)
    ok = rc == 1 and j.get("ok") is False and j.get("code") == code and (args is None or j.get("args") == args)
    check(what, ok and srv.game() == before, j)


def status_check(srv):
    """(v141 beta 6) status and bots with a stand-in game process in bot 1's run folder (the agent finds the game by its command
    line and folder): bots' "real" and "dolls" from the players' "kind" (else from the dolls' names), status's "lobby_restart_in"
    (HOST_LOBBY_GRACE less the host's lobby_lost_for), its reason unchanged."""
    run = os.path.join(srv.root, "var", "bot1", "run")
    os.makedirs(run, exist_ok=True)

    def status(players, cmd="status", **fields):
        st = dict({"v": 1, "state": "hosting", "lobby": 1, "lobby_rx": 3, "lobby_rx_age": 3, "lobby_closed": 0, "lobby_fd": 7,
                   "lobby_lost_for": -1, "game": {"name": "EU DM 1", "max": 9},
                   "players": [{"p": 1, "name": "HOSTACC", "host": 1, "kind": "host", "doll": 0}] + players}, **fields)
        with open(os.path.join(run, "status.json"), "w") as f:
            json.dump(st, f)
        return srv.run([cmd])

    def grace(server=None, bot=None):
        with open(os.path.join(srv.etc, "server.env"), "w") as f:
            f.write("" if server is None else "HOST_LOBBY_GRACE=%s\n" % server)
        with open(os.path.join(srv.etc, "bot1.env"), "w") as f:
            f.write("HOST_ACCOUNT_ID=HOSTACC\nHOST_ACCOUNT_PASSWORD=TOPSECRET\nUDP_PORT=5730\n" +
                    ("" if bot is None else "HOST_LOBBY_GRACE=%s\n" % bot))

    kinds = [{"p": 2, "name": "SNAKE", "host": 0, "kind": "real", "doll": 0}, {"p": 3, "name": "DOLL_1", "host": 0, "kind": "doll", "doll": 1},
             {"p": 4, "name": "Big Boss", "host": 0, "kind": "doll", "doll": 2}, {"p": 5, "name": "RAIDEN", "host": 0, "kind": "real", "doll": 0}]
    lost = {"lobby": 0, "lobby_closed": 1, "lobby_lost_for": 4}
    game = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)", "mgo1host.exe"], cwd=run)
    try:
        time.sleep(0.3)
        rc, j = status(kinds)
        b1 = (j.get("bots") or [{}])[0]
        check("status: online, no lobby countdown (null)", rc == 0 and j.get("online") == "online" and "lobby_restart_in" in j
              and j["lobby_restart_in"] is None, j)
        check("status's bots: the real players and the dolls from the players' kinds", (b1.get("players"), b1.get("real"), b1.get("dolls")) == (4, 2, 2), b1)
        rc, j = status(kinds, "bots")
        b1 = (j.get("bots") or [{}])[0]
        check("bots: real 2, dolls 2", rc == 0 and (b1.get("players"), b1.get("real"), b1.get("dolls")) == (4, 2, 2), j)
        # (v141 beta 7) the game now unranked: the host's "unranked" (1, 0), null for an older host
        check("bots: unranked null for a host that does not say", "unranked" in b1 and b1["unranked"] is None, b1)
        rc, j = status(kinds, "bots", unranked_mode="auto", unranked=1, unranked_sent=3)
        b1 = (j.get("bots") or [{}])[0]
        check("bots: the game unranked (1) as its host says", b1.get("unranked") == 1, b1)
        rc, j = status(kinds, "bots", unranked_mode="auto", unranked=0, unranked_sent=0)
        check("bots: ranked (0)", ((j.get("bots") or [{}])[0]).get("unranked") == 0, j)
        rc, j = status([{k: v for k, v in p.items() if k not in ("kind", "doll")} for p in kinds], "bots")
        b1 = (j.get("bots") or [{}])[0]
        check("bots: no kinds and no dolls' names: every player real", (b1.get("players"), b1.get("real"), b1.get("dolls")) == (4, 4, 0), b1)
        with open(os.path.join(srv.root, "var", "dolls.names"), "w") as f:
            f.write("BIG BOSS\nDOLL_1\n")
        rc, j = status([{k: v for k, v in p.items() if k not in ("kind", "doll")} for p in kinds], "bots")
        b1 = (j.get("bots") or [{}])[0]
        check("bots: no kinds (an older host): the dolls by the server's dolls' names", (b1.get("players"), b1.get("real"), b1.get("dolls")) == (4, 2, 2), b1)
        for server, bot, lost_for, want in ((None, None, 4, 6), (20, None, 4, 16), (20, 12, 4, 8), (20, "", 4, 6), (20, None, 30, 0),
                                            (200, None, 4, 56), (2, None, 1, 4), ("x", None, 1, 4)):
            grace(server, bot)
            rc, j = status(kinds, **dict(lost, lobby_lost_for=lost_for))
            check("the lobby lost %d s ago, HOST_LOBBY_GRACE %r (server.env) %r (bot1.env): lobby_restart_in %d" % (lost_for, server, bot, want),
                  rc == 0 and j.get("lobby_restart_in") == want, {k: j.get(k) for k in ("online", "reason", "lobby_restart_in", "status_age")})
        check("the lobby lost: the reason as before (an external watchdog matches it)", j.get("online") == "offline"
              and j.get("reason") == "lobby connection closed", j)
        b1 = (j.get("bots") or [{}])[0]
        check("the lobby lost: the bot's players not counted (as before)", (b1.get("players"), b1.get("real"), b1.get("dolls")) == (0, 0, 0), b1)
    finally:
        game.kill()
        game.wait()
    grace(20, None)
    rc, j = status(kinds, **lost)
    check("no game process (its status the last one's): no countdown", rc == 0 and j.get("lobby_restart_in") is None
          and j.get("reason") == "no status yet", {k: j.get(k) for k in ("online", "reason", "lobby_restart_in")})


def crash_check(srv):
    """(v150-host beta 16) crashes, crash: the reports of host/mgo1log (written here by mgo1log itself: a game's lines, its exit code
    in the file a launcher writes, its status file)."""
    logger = os.path.join(HERE, "..", "host", "mgo1log")
    run = os.path.join(srv.root, "run_crash")
    os.makedirs(run, exist_ok=True)
    with open(os.path.join(run, "status.json"), "w") as f:
        json.dump({"v": 1, "frame": 2573208, "state": "hosting", "game": {"name": "EU DM 1", "rules": [[0, 7], [3, 2]], "round": 1, "max": 9},
                   "players": [{"p": 1, "name": "HOSTACC", "host": 1, "kind": "host"}, {"p": 2, "name": "V1", "host": 0, "kind": "doll", "vdoll": 1, "team": 0, "k": 1, "d": 2},
                               {"p": 3, "name": "SNAKE", "host": 0, "kind": "real", "team": 1, "k": 4, "d": 0, "ping": 80}],
                   "players_real": 1, "players_dolls": 1, "players_host": 1, "pace": {"n": 3, "rate": 59.9, "due": 59.94, "cpu": 20}}, f)
    with open(os.path.join(run, "VERSION"), "w") as f:
        f.write("build 1.00 0123456789ab\n")

    def game(source, code, lines):
        with open(os.path.join(run, "exit"), "w") as f:
            f.write("%d\n" % code)
        env = dict(srv.env, MGO1_CRASH_EXIT=os.path.join(run, "exit"), MGO1_CRASH_STATUS=os.path.join(run, "status.json"),
                   MGO1_CRASH_VERSION=os.path.join(run, "VERSION"), MGO1_CRASH_ENDS="2=lost 5=lost")
        subprocess.run([sys.executable, logger, "--dir", os.path.join(srv.root, "log", source), "--quiet"], input="\n".join(lines) + "\n", text=True, env=env, timeout=60)

    rc, j = srv.run(["crashes"])
    check("crashes: none yet", rc == 0 and j.get("ok") and j.get("crashes") == [], j)
    lines = ["ps2 f%d: vdoll 2: respawns at -2000 464 %d" % (2573000 + i, -31500 - i) for i in range(150)]
    wild = ["ps2: hardware read outside the emulated registers at %08x (00000000)" % (0x1476ed50 + 4 * i) for i in range(64)]
    end = ["ps2: 131072 more accesses outside the memory and the registers in 10 s (from 10886a50 to 108c5d40, the last a read at 108c5d40; 131136 reads and 0 writes "
           "since the start): the PS2 code follows a wild pointer",
           "CRASH: the PS2 code walks outside its memory and never ends: 65536 accesses in a row with no call between them, from 10886a50 to 108c5d40 (the last a read; "
           "131136 reads and 0 writes in all); last call 00475a58 from 0040bba4, last loop in 0040bac0; calls before: 004755d8<0040ba9c",
           "CRASH: the loop's registers: r 0 1 2 3", "CRASH: the game ends here (70): its service starts it again", "a line with HOSTACC and topsecret in it"]
    game("doll3", 5, ["doll f2520: activity: finding_game EU DM 1", "doll f3769: lobby: the match was lost"])
    time.sleep(1.1)                                            # (a report's name has its second: three different ones)
    game("bot1", 70, lines + wild + end)
    time.sleep(1.1)
    game("bot1", 2, ["ps2 f40: host watch: no game for 30 s: the process ends"])
    rc, j = srv.run(["crashes"])
    cs = j.get("crashes", [])
    check("crashes: the three reports, the newest first, each with its source, reason, time and size", rc == 0 and [(c.get("source"), c.get("reason")) for c in cs]
          == [("bot1", "lost"), ("bot1", "crash70"), ("doll3", "lost")] and all(re.match(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$", c.get("time", "")) and c.get("size", 0) > 200
                                                                                   and c.get("name", "").endswith("_%s.txt" % c["reason"]) for c in cs), cs)
    c = cs[1] if len(cs) == 3 else {}
    check("crashes: the header's fields: what ended, the version, the game, the round with its rule's and its map's names, the players",
          "the game crashed (exit code 70)" in c.get("what", "") and c.get("version") == "build 1.00 0123456789ab" and c.get("state") == "hosting"
          and c.get("game") == "EU DM 1" and (c.get("rule"), c.get("rule_name"), c.get("map"), c.get("map_name")) == (3, "capture", 2, "Ghost Factory")
          and c.get("players") == "3 (1 real, 1 dolls, 1 host)" and c.get("who") == ["P1 host", "P2 virtual doll 1, team red, 1 kills, 2 deaths",
                                                                                   'P3 real "SNAKE", team blue, 4 kills, 0 deaths, ping 80 ms'], c)
    check("crashes: the game's first access outside its memory (the header's wild line)", re.match(
        r"^\d{4}-\d\d-\d\dT[0-9:.]+Z ps2: hardware read outside the emulated registers at 1476ed50 \(00000000\)$", c.get("wild", "")), c.get("wild"))
    rc, j = srv.run(["crashes", "--doll", "3"])
    check("crashes --doll 3: that doll's only", rc == 0 and [x.get("source") for x in j.get("crashes", [])] == ["doll3"], j)
    rc, j = srv.run(["crashes", "-b", "1"])
    check("crashes -b 1: that bot's only", rc == 0 and [x.get("source") for x in j.get("crashes", [])] == ["bot1", "bot1"], j)
    rc, j = srv.run(["--bot", "1", "crashes"])
    check("--bot 1 crashes: the same", rc == 0 and [x.get("source") for x in j.get("crashes", [])] == ["bot1", "bot1"], j)
    rc, j = srv.run(["crashes", "-b", "11"])
    check("crashes -b 11: refused", rc == 1 and j.get("code") == "usage", j)
    name = c.get("name", "x")
    rc, j = srv.run(["crash", "bot1/" + name])
    body = j.get("lines", [])
    check("crash: one report: its header's fields and its lines (the header, ----, the game's last lines)", rc == 0 and j.get("ok") and j.get("reason") == "crash70"
          and j.get("source") == "bot1" and j.get("total") == len(body) and j.get("next") == -1 and "----" in body
          and body[0].startswith("MGO1 crash report") and any("vdoll 2: respawns at -2000 464 -31500" in x for x in body)
          and any(" E CRASH: the PS2 code walks outside its memory and never ends" in x for x in body)
          and len([x for x in body if "vdoll 2: respawns" in x]) == 150 and j.get("wild", "").endswith("at 1476ed50 (00000000)"), (rc, j.get("total"), body[:2]))
    check("crash: no account or password in it", "HOSTACC" not in json.dumps(j) and "TOPSECRET" not in json.dumps(j).upper() and body[-1].endswith(" I a line with *** and *** in it"), body[-2:])
    rc, j2 = srv.run(["crash", name, "--lines", "5", "--from", "3"])
    check("crash <name> alone, by pages", rc == 0 and j2.get("source") == "bot1" and j2.get("lines") == body[3:8] and j2.get("next") == 8 and j2.get("from") == 3, j2.get("lines"))
    rc, j2 = srv.run(["crash", "bot1/" + name, "--tail", "2"])
    check("crash --tail", rc == 0 and j2.get("lines") == body[-2:] and j2.get("from") == len(body) - 2, j2.get("lines"))
    for bad, code in (("bot1/../../etc/passwd", "usage"), ("manager/" + name, "usage"), ("bot1/2026-11-10_000000_hung.txt", "no_crash"), ("notes.txt", "usage")):
        rc, j2 = srv.run(["crash", bad])
        check("crash %s: refused (%s)" % (bad, code), rc == 1 and j2.get("ok") is False and j2.get("code") == code, j2)


def vdolls_check(srv):
    """(virtual dolls) vdoll-set's level (1 to 10, or one a doll: "3,5,7" or [3, 5, 7]) into bot1.env's VDOLL_LEVEL, the values
    refused (vdoll_level) leaving the files as they were, a set refused on one key writing none; vdolls' level (5 by default, a
    bad one written by hand: 5, as mgo1host.sh gives it), its game's virtual dolls now (the status), the settings its running game
    was given (mgo1host.sh's run/vdolls.env and vdolls.names) and pending when the saved ones differ for a doll."""
    env, names = os.path.join(srv.etc, "bot1.env"), os.path.join(srv.etc, "bot1.vdolls")
    run = os.path.join(srv.root, "var", "bot1", "run")

    def text(p):
        return open(p).read() if os.path.exists(p) else ""

    def vd():
        rc, j = srv.run(["vdolls"])
        return (j.get("bots") or [{}])[0] if rc == 0 else j

    b = vd()
    check("vdolls: level 5 by default, nothing applied yet (applied and pending null)", b.get("level") == "5" and b.get("count") == 0
          and "applied" in b and b["applied"] is None and "pending" in b and b["pending"] is None, b)
    for given, want in ((7, "7"), ("10", "10"), (" 3, 5,7 ", "3,5,7"), ([3, 5, 7], "3,5,7"), (["1", 10], "1,10"), (None, "5"), ("", "5"),
                        ([10] * 15, ",".join(["10"] * 15))):
        rc, j = srv.run(["vdoll-set", "1"], json.dumps({"level": given}))
        check("vdoll-set: level %r written as VDOLL_LEVEL=%s" % (given, want[:12]), rc == 0 and "VDOLL_LEVEL=%s\n" % want in text(env)
              and text(env).count("VDOLL_LEVEL=") == 1 and (j.get("vdolls") or {}).get("level") == want, j)
    srv.run(["vdoll-set", "1"], json.dumps({"level": "3,5,7"}))
    for bad in (0, 11, -1, "x", "3,,5", "3;5", "5,", 5.5, True, [], [3, 0], [11], [5] * 16, {"a": 1}):
        before = text(env)
        rc, j = srv.run(["vdoll-set", "1"], json.dumps({"level": bad}))
        check("vdoll-set: level %r refused (vdoll_level), bot1.env unchanged" % (bad,), rc == 1 and j.get("code") == "vdoll_level"
              and len(j.get("args", [])) == 1 and text(env) == before, j)
    before = text(env)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"count": 2, "teams": "red", "level": 12, "names": ["ALPHA"]}))
    check("vdoll-set: one key refused, none written (count, teams, the names file)", rc == 1 and j.get("code") == "vdoll_level"
          and text(env) == before and not os.path.exists(names), j)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"count": 3, "teams": "red,blue", "names": ["ALPHA", "Bravo Two"]}))
    check("vdoll-set: count, teams and names keep the level", rc == 0 and "VDOLL_LEVEL=3,5,7\n" in text(env) and "VDOLL_COUNT=3\n" in text(env)
          and text(names) == "ALPHA\nBravo Two\n", j)
    for bad, code in (({"count": 8}, "vdoll_count"), ({"count": -1}, "vdoll_count"), ({"count": "x"}, "vdoll_count"), ({"count": True}, "vdoll_count"),
                      ({"teams": "red,green"}, "vdoll_teams"), ({"names": ["ALPHA", "alpha"]}, "vdoll_name_taken"), ({"names": ["DOLL_3"]}, "doll_name"),
                      ({"names": ["AB"]}, "doll_name"), ({"names": ["N%02d" % i for i in range(16)]}, "vdoll_names")):
        before, nbefore = text(env), text(names)
        rc, j = srv.run(["vdoll-set", "1"], json.dumps(bad))
        check("vdoll-set: %s refused (%s), the files unchanged" % (json.dumps(bad)[:40], code), rc == 1 and j.get("code") == code
              and text(env) == before and text(names) == nbefore, j)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"count": 8}))
    check("vdoll-set: the count's error gives the bot and its most (7 in a game of 9)", j.get("args") == ["1", "7"], j)
    check("the help tells vdoll-set's level", '"level": 1-10 or "3,5,7"' in load(AGENT, "agent_help").__doc__)
    # by hand: a bad VDOLL_LEVEL is shown as the game gets it (5)
    with open(env, "a") as f:
        f.write("VDOLL_LEVEL=12\n")
    b = vd()
    check("vdolls: a bad VDOLL_LEVEL written by hand shown as 5", b.get("level") == "5" and b.get("teams") == "red,blue", b)
    srv.run(["vdoll-set", "1"], json.dumps({"level": "3,5,7"}))
    # the game now (its status), the settings it was given (as mgo1host.sh writes them) and pending
    os.makedirs(run, exist_ok=True)
    live = [{"n": 1, "state": "in_game", "player": 2, "team": 0, "life": "alive", "deaths": 1, "leaves": 0},
            {"n": 2, "state": "joining", "player": 0, "team": -1, "life": "not spawned", "deaths": 0, "leaves": 1}]
    with open(os.path.join(run, "status.json"), "w") as f:
        json.dump({"state": "hosting", "players": [], "vdolls_set": 3, "players_vdolls": 1, "vdolls": live}, f)
    with open(os.path.join(run, "vdolls.env"), "w") as f:
        f.write("VDOLL_COUNT=3\nVDOLL_TEAMS=red,blue\nVDOLL_LEVEL=3,5,7\n")
    with open(os.path.join(run, "vdolls.names"), "w") as f:
        f.write("ALPHA\nBravo Two\n")
    b = vd()
    check("vdolls: its game now (set 3, 1 in it, each one's state) and its status' age", b.get("set_in_game") == 3 and b.get("in_game") == 1
          and b.get("live") == live and isinstance(b.get("status_age"), int) and 0 <= b["status_age"] < 60, b)
    check("vdolls: the settings its game was given, none pending", b.get("applied") == {"count": 3, "teams": "red,blue", "level": "3,5,7",
          "names": ["ALPHA", "Bravo Two"]} and b.get("pending") is False, b)
    for change, want in (({"level": 4}, True), ({"level": "3,5,7"}, False), ({"teams": "red,blue,blue"}, False), ({"teams": "red,blue,red"}, True),
                         ({"teams": "red,blue"}, False), ({"names": ["ALPHA", "Bravo 2"]}, True), ({"names": ["ALPHA", "Bravo Two", "VDOLL3"]}, False),
                         ({"names": ["ALPHA", "Bravo Two"]}, False), ({"count": 2}, True), ({"count": 3}, False), ({"level": "3,5,7,9"}, False)):
        rc, j = srv.run(["vdoll-set", "1"], json.dumps(change))
        check("vdolls: %s saved: pending %s" % (json.dumps(change), want), rc == 0 and vd().get("pending") is want, vd())
    with open(os.path.join(run, "vdolls.env"), "w") as f:
        f.write("VDOLL_COUNT=0\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\n")
    os.remove(os.path.join(run, "vdolls.names"))
    check("vdolls: a game started with none, 3 saved since: pending", vd().get("pending") is True and vd().get("applied", {}).get("count") == 0, vd())
    # (v150-host beta 15) vdoll-count: the count now (run/vdolls.live, read by the game every 2 s) and for the next games
    rc, j = srv.run(["vdoll-count", "1", "2"])
    check("vdoll-count before a game of this version: VDOLL_COUNT=2, the next game", rc == 0 and "VDOLL_COUNT=2\n" in text(env)
          and j.get("takes_effect") == "next game" and not os.path.exists(os.path.join(run, "vdolls.live")), j)
    with open(os.path.join(run, "vdolls.live"), "w") as f:
        f.write("0\n")
    rc, j = srv.run(["vdoll-count", "1", "5"])
    check("vdoll-count in a game of this version: vdolls.live 5, VDOLL_COUNT=5, the game's settings count 5, now", rc == 0
          and text(os.path.join(run, "vdolls.live")) == "5\n" and "VDOLL_COUNT=5\n" in text(env)
          and "VDOLL_COUNT=5\n" in text(os.path.join(run, "vdolls.env")) and j.get("takes_effect") == "now"
          and (j.get("vdolls") or {}).get("count") == 5, j)
    for args, code in ((["1", "8"], "vdoll_count"), (["1", "x"], "usage"), (["9", "1"], "no_bot"), (["1"], "usage")):
        rc, j = srv.run(["vdoll-count"] + args)
        check("vdoll-count %s refused (%s), vdolls.live unchanged" % (" ".join(args), code), rc == 1 and j.get("code") == code
              and text(os.path.join(run, "vdolls.live")) == "5\n", j)
    check("the help tells vdoll-count", "vdoll-count <bot> <n>" in load(AGENT, "agent_help").__doc__)
    # (v150-host beta 15) the weapons: one a doll (ak47, xm16e1, scorpion, m37, svd, auto), pending when they differ
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"weapons": "svd,M37,auto"}))
    check("vdoll-set: weapons written as VDOLL_WEAPONS (small letters)", rc == 0 and "VDOLL_WEAPONS=svd,m37,auto\n" in text(env)
          and (j.get("vdolls") or {}).get("weapons") == "svd,m37,auto", j)
    for bad in ("rpg7", "ak47,,svd", ",".join(["svd"] * 16)):
        rc, j = srv.run(["vdoll-set", "1"], json.dumps({"weapons": bad}))
        check("vdoll-set: weapons %r refused (vdoll_weapons)" % bad[:12], rc == 1 and j.get("code") == "vdoll_weapons" and "VDOLL_WEAPONS=svd,m37,auto\n" in text(env), j)
    with open(os.path.join(run, "vdolls.env"), "w") as f:
        f.write("VDOLL_COUNT=5\nVDOLL_TEAMS=red,blue\nVDOLL_LEVEL=3,5,7,9\nVDOLL_WEAPONS=svd,m37,auto\n")
    with open(os.path.join(run, "vdolls.names"), "w") as f:
        f.write("ALPHA\nBravo Two\n")
    check("vdolls: the weapons its game was given, none pending", vd().get("applied", {}).get("weapons") == "svd,m37,auto" and vd().get("pending") is False, vd())
    srv.run(["vdoll-set", "1"], json.dumps({"weapons": "ak47"}))
    check("vdolls: other weapons saved: pending", vd().get("pending") is True, vd())
    # (v150-host beta 16, review P2-9) max_players lowered after the virtual dolls were set: VDOLL_COUNT follows (the
    # players less the host's entry and the kept seat), the answer says so; a game that still holds them changes nothing
    game_before, env_before = srv.game(), text(env)
    day = os.path.join(srv.root, "var", "bot1", "events")

    def admin_lines():
        out = []
        for n in sorted(os.listdir(day)) if os.path.isdir(day) else ():
            if n.endswith(".admin.jsonl"):
                out += [json.loads(x) for x in open(os.path.join(day, n)).read().splitlines() if x.strip()]
        return [x for x in out if x.get("do") == "settings"]

    rc, j = srv.set({"max_players": 16})
    check("settings-set: a game of 16, 5 virtual dolls: the count kept, no vdoll_count_lowered in the answer", rc == 0
          and "vdoll_count_lowered" not in j and "VDOLL_COUNT=5\n" in text(env), j)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"count": 12}))
    check("vdoll-set: 12 virtual dolls in a game of 16", rc == 0 and "VDOLL_COUNT=12\n" in text(env), j)
    rc, j = srv.set({"max_players": 9})
    check("settings-set: max_players 16 -> 9 with 12 virtual dolls: VDOLL_COUNT 7, vdoll_count_lowered 7", rc == 0
          and j.get("vdoll_count_lowered") == 7 and "VDOLL_COUNT=7\n" in text(env) and text(env).count("VDOLL_COUNT=") == 1
          and "VDOLL_LEVEL=3,5,7,9\n" in text(env) and "HOST_ACCOUNT_PASSWORD=TOPSECRET\n" in text(env), (j, text(env)))
    check("vdolls: the count shown is the lowered one, its most 7", vd().get("count") == 7 and vd().get("most") == 7, vd())
    last = (admin_lines() or [{}])[-1]
    check("settings-set: the admin event tells the lowered count (and the earlier one does not)", last.get("vdoll_count_lowered") == 7
          and all("vdoll_count_lowered" not in x for x in admin_lines()[:-1]), admin_lines()[-3:])
    rc, j = srv.set({"max_players": 9})
    check("settings-set: the same again: nothing lowered", rc == 0 and "vdoll_count_lowered" not in j and "VDOLL_COUNT=7\n" in text(env), j)
    rc, j = srv.set({"max_players": 3})
    check("settings-set: a game of 3 (the host, a real player, one more): 1 virtual doll", rc == 0 and j.get("vdoll_count_lowered") == 1
          and "VDOLL_COUNT=1\n" in text(env), j)
    rc, j = srv.set({"rotation": "4:12"})
    check("settings-set: no max_players in the file (9): 1 virtual doll kept", rc == 0 and "vdoll_count_lowered" not in j
          and "VDOLL_COUNT=1\n" in text(env), j)
    with open(env, "w") as f:
        f.write(env_before.replace("VDOLL_COUNT=5\n", "VDOLL_COUNT=many\n"))
    rc, j = srv.set({"max_players": 5})
    check("settings-set: a VDOLL_COUNT the launcher takes as 0 (not a number, by hand) is left alone", rc == 0 and "vdoll_count_lowered" not in j
          and "VDOLL_COUNT=many\n" in text(env), j)
    with open(env, "w") as f:
        f.write(env_before.replace("VDOLL_COUNT=5\n", "VDOLL_COUNT=99\n"))
    rc, j = srv.set({"max_players": 5})
    check("settings-set: a VDOLL_COUNT of 99 by hand (the launcher's 15): 3 in a game of 5", rc == 0 and j.get("vdoll_count_lowered") == 3
          and "VDOLL_COUNT=3\n" in text(env), j)
    with open(env, "w") as f:
        f.write(env_before)
    with open(os.path.join(srv.etc, "bot1.game"), "w") as f:
        f.write(game_before)


def switches_check():
    """(v150-host beta 18) The two switches, each off by default: a bot's virtual dolls "as a player" (vdoll-set's "controls":
    player or direct, bot<N>.env VDOLL_CONTROLS; vdolls' "controls", its "applied" and "pending", the top's "vdoll_controls")
    and a playable doll's direct aim (doll-set's "aim", doll-order aim, doll-all aim: direct or pad, doll<N>.env DOLL_AIM, the
    live order `aim direct|pad`; dolls' "aim", the top's "doll_aim"): the defaults, the refusals (nothing written), a settings
    file from Windows (carriage returns, quotes), an old server's files with no such key."""
    srv = Server(AGENT)
    env = os.path.join(srv.etc, "bot1.env")
    run = os.path.join(srv.root, "var", "bot1", "run")

    def text(p):
        return open(p).read() if os.path.exists(p) else ""

    def vd():
        rc, j = srv.run(["vdolls"])
        return (j.get("bots") or [{}])[0] if rc == 0 else j

    # ---- A. the virtual dolls' controls
    rc, j = srv.run(["vdolls"])
    check("vdolls: \"vdoll_controls\": true at its top (MGO1 Host then shows CONTROLS)", rc == 0 and j.get("vdoll_controls") is True, j)
    check("vdolls: an old server's bot1.env (no VDOLL_CONTROLS): controls direct", vd().get("controls") == "direct", vd())
    for given, want in (("player", "player"), ("direct", "direct"), (" PLAYER ", "player"), ("Direct", "direct"), (None, "direct"), ("", "direct")):
        rc, j = srv.run(["vdoll-set", "1"], json.dumps({"controls": given}))
        check("vdoll-set: controls %r written as VDOLL_CONTROLS=%s, told back" % (given, want), rc == 0 and "VDOLL_CONTROLS=%s\n" % want in text(env)
              and text(env).count("VDOLL_CONTROLS=") == 1 and (j.get("vdolls") or {}).get("controls") == want and j.get("vdoll_controls") is True, j)
    srv.run(["vdoll-set", "1"], json.dumps({"controls": "player", "level": 7}))
    for bad in ("pad", "players", "as a player", "1", 1, True, ["player"], {"a": 1}):
        before = text(env)
        rc, j = srv.run(["vdoll-set", "1"], json.dumps({"controls": bad}))
        check("vdoll-set: controls %r refused (vdoll_controls, the value given back), bot1.env unchanged" % (bad,), rc == 1 and j.get("code") == "vdoll_controls"
              and len(j.get("args", [])) == 1 and "player or direct" in j.get("error", "") and text(env) == before, j)
    before = text(env)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"count": 2, "level": 3, "controls": "auto"}))
    check("vdoll-set: controls refused with other keys: none written", rc == 1 and j.get("code") == "vdoll_controls" and text(env) == before, j)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"count": 2, "level": 3}))
    check("vdoll-set: other keys keep the controls", rc == 0 and "VDOLL_CONTROLS=player\n" in text(env) and "VDOLL_COUNT=2\n" in text(env), text(env))
    # by hand, a file from Windows: CRLF and quotes; another word: direct, as mgo1host.sh takes it
    acct = "HOST_ACCOUNT_ID=HOSTACC\r\nHOST_ACCOUNT_PASSWORD=TOPSECRET\r\nUDP_PORT=5730\r\nVDOLL_COUNT=2\r\n"
    for line, want in (('VDOLL_CONTROLS="player"\r\n', "player"), ("VDOLL_CONTROLS=player\r\n", "player"), ("VDOLL_CONTROLS='PLAYER'\r\n", "player"),
                       ("VDOLL_CONTROLS=pad\r\n", "direct"), ("VDOLL_CONTROLS=\r\n", "direct")):
        with open(env, "w", newline="") as f:
            f.write(acct + line)
        check("vdolls: %r written by hand on Windows: controls %s" % (line, want), vd().get("controls") == want, vd())
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"level": 4}))
    check("vdoll-set on a file from Windows: the account kept, the controls (direct) not lost", rc == 0 and "HOST_ACCOUNT_PASSWORD=TOPSECRET\n" in text(env)
          and vd().get("controls") == "direct", text(env))
    # what the running game was given (mgo1host.sh's vdolls.env) and pending
    with open(env, "w") as f:
        f.write("HOST_ACCOUNT_ID=HOSTACC\nHOST_ACCOUNT_PASSWORD=TOPSECRET\nUDP_PORT=5730\nVDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\n")
    os.makedirs(run, exist_ok=True)
    with open(os.path.join(run, "vdolls.env"), "w") as f:
        f.write("VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\nVDOLL_WEAPONS=auto\n")       # (a game started by agent 3.4's launcher)
    b = vd()
    check("vdolls: a game started before 3.5 (no VDOLL_CONTROLS in its vdolls.env): no \"controls\" in applied, nothing pending",
          "controls" not in (b.get("applied") or {}) and b.get("pending") is False, b)
    srv.run(["vdoll-set", "1"], json.dumps({"controls": "player"}))
    check("vdolls: player saved, that old game runs direct: pending", vd().get("pending") is True, vd())
    with open(os.path.join(run, "vdolls.env"), "w") as f:
        f.write("VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=player\n")
    b = vd()
    check("vdolls: a game started with controls player: applied controls player, nothing pending", (b.get("applied") or {}).get("controls") == "player"
          and b.get("pending") is False, b)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"controls": "direct"}))
    check("vdoll-set: controls direct while its game plays as a player: the next game (as the level), pending", rc == 0
          and j.get("takes_effect") == "next game" and vd().get("pending") is True, j)
    check("vdoll-set: nothing written to the live file (it holds the count only)", not os.path.exists(os.path.join(run, "vdolls.live")), os.listdir(run))
    srv.run(["vdoll-count", "1", "0"])
    with open(os.path.join(run, "vdolls.env"), "w") as f:
        f.write("VDOLL_COUNT=0\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=5\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=player\n")
    check("vdolls: no virtual doll set nor in its game: the controls alone are not pending", vd().get("count") == 0 and vd().get("pending") is False, vd())
    check("the help tells vdoll-set's controls", '"controls"' in load(AGENT, "agent_help2").__doc__)

    # ---- B. a playable doll's aim
    rc, j = srv.run(["dolls"])
    check("dolls: \"doll_aim\": true at its top (MGO1 Host then shows AIM)", rc == 0 and j.get("doll_aim") is True, j)
    cmd = os.path.join(srv.root, "var", "doll1", "command")
    denv = os.path.join(srv.etc, "doll1.env")

    def doll(n=1):
        rc, j = srv.run(["dolls"])
        return ([d for d in j.get("dolls", []) if d.get("doll") == n] or [{}])[0]

    rc, j = srv.run(["doll-set", "1"], json.dumps({"bot": 1, "difficulty": 5}))
    check("doll-set without aim: aim pad, no DOLL_AIM written, no aim order", rc == 0 and (j.get("doll") or {}).get("aim") == "pad"
          and "DOLL_AIM" not in text(denv) and "aim " not in text(cmd), (j, text(denv), text(cmd)))
    for given, want in (("direct", "direct"), ("pad", "pad"), (" DIRECT ", "direct"), (None, "pad"), ("", "pad")):
        open(cmd, "w").close()
        rc, j = srv.run(["doll-set", "1"], json.dumps({"aim": given}))
        check("doll-set: aim %r: DOLL_AIM=%s, the order `aim %s`, told back" % (given, want, want), rc == 0 and "DOLL_AIM=%s\n" % want in text(denv)
              and text(denv).count("DOLL_AIM=") == 1 and "aim %s\n" % want in text(cmd) and (j.get("doll") or {}).get("aim") == want, (j, text(denv), text(cmd)))
    srv.run(["doll-set", "1"], json.dumps({"aim": "direct"}))
    for bad in ("stick", "player", "auto", 1, True, ["direct"]):
        before, cbefore = text(denv), text(cmd)
        rc, j = srv.run(["doll-set", "1"], json.dumps({"aim": bad, "difficulty": 9}))
        check("doll-set: aim %r refused (doll_aim), doll1.env unchanged, no order" % (bad,), rc == 1 and j.get("code") == "doll_aim"
              and "direct or pad" in j.get("error", "") and text(denv) == before and text(cmd) == cbefore, j)
    rc, j = srv.run(["doll-set", "1"], json.dumps({"difficulty": 8, "team": "red"}))
    check("doll-set of other keys: DOLL_AIM=direct kept", rc == 0 and "DOLL_AIM=direct\n" in text(denv) and doll().get("aim") == "direct", text(denv))
    open(cmd, "w").close()
    rc, j = srv.run(["doll-order", "1", "aim", "pad"])
    check("doll-order 1 aim pad: the order, DOLL_AIM=pad kept for its next start", rc == 0 and text(cmd) == "aim pad\n" and "DOLL_AIM=pad\n" in text(denv)
          and (j.get("doll") or {}).get("aim") == "pad", (j, text(cmd)))
    rc, j = srv.run(["doll-order", "1", "aim", "direct"])
    check("doll-order 1 aim direct", rc == 0 and text(cmd) == "aim pad\naim direct\n" and "DOLL_AIM=direct\n" in text(denv), (j, text(cmd)))
    for bad in (["aim", "stick"], ["aim", "1"]):
        before, cbefore = text(denv), text(cmd)
        rc, j = srv.run(["doll-order", "1"] + bad)
        check("doll-order 1 %s refused (doll_aim), nothing written" % " ".join(bad), rc == 1 and j.get("code") == "doll_aim"
              and text(denv) == before and text(cmd) == cbefore, j)
    rc, j = srv.run(["doll-order", "1", "aim"])
    check("doll-order 1 aim (no value) refused (usage)", rc == 1 and j.get("code") == "usage", j)
    srv.run(["doll-set", "2"], json.dumps({"bot": 1}))
    open(cmd, "w").close()
    rc, j = srv.run(["doll-all", "1", "aim", "pad"])
    cmd2 = os.path.join(srv.root, "var", "doll2", "command")
    check("doll-all 1 aim pad: every doll of bot 1, the order and DOLL_AIM=pad", rc == 0 and text(cmd) == "aim pad\n" and "aim pad\n" in text(cmd2)
          and "DOLL_AIM=pad\n" in text(denv) and "DOLL_AIM=pad\n" in text(os.path.join(srv.etc, "doll2.env"))
          and [d.get("aim") for d in j.get("dolls", [])] == ["pad", "pad"], j)
    rc, j = srv.run(["doll-all", "1", "aim", "direct"])
    check("doll-all 1 aim direct", rc == 0 and [d.get("aim") for d in j.get("dolls", [])] == ["direct", "direct"], j)
    before = text(denv)
    rc, j = srv.run(["doll-all", "1", "aim", "sideways"])
    check("doll-all 1 aim sideways refused (doll_aim), nothing written", rc == 1 and j.get("code") == "doll_aim" and text(denv) == before, j)
    # a doll's file written by hand on Windows, and an old server's file with no DOLL_AIM
    for line, want in (('DOLL_AIM="direct"\r\n', "direct"), ("DOLL_AIM=DIRECT\r\n", "direct"), ("DOLL_AIM=stick\r\n", "pad"), ("", "pad")):
        with open(denv, "w", newline="") as f:
            f.write("DOLL_BOT=1\r\nDOLL_DIFFICULTY=7\r\nDOLL_ENABLED=1\r\n" + line)
        check("dolls: doll1.env %s: aim %s" % ("with %r" % line if line else "of an old server (no DOLL_AIM)", want), doll().get("aim") == want, doll())
    with open(denv, "w", newline="") as f:
        f.write('DOLL_BOT=1\r\nDOLL_DIFFICULTY=7\r\nDOLL_ENABLED=1\r\nDOLL_AIM="direct"\r\n')
    rc, j = srv.run(["doll-order", "1", "difficulty", "6"])
    check("an order on a doll's file from Windows: written again plainly, DOLL_AIM=direct kept", rc == 0 and "DOLL_AIM=direct\n" in text(denv)
          and "\r" not in text(denv) and "DOLL_DIFFICULTY=6\n" in text(denv), text(denv))
    doc = load(AGENT, "agent_help3").__doc__
    check("the help tells the aim (doll-set, doll-order, doll-all)", "aim <direct|pad>" in doc and "doll-all <bot> adapt <on|off> | difficulty <1-10|vdoll> | aim <direct|pad>" in doc)
    shutil.rmtree(srv.root, ignore_errors=True)

def adapt_check():
    """(v150-host beta 19) The virtual dolls' live level: vdoll-set's "adapt", "adapt_min", "adapt_max" (bot<N>.env VDOLL_ADAPT,
    VDOLL_ADAPT_MIN, VDOLL_ADAPT_MAX; the refusals, nothing written), told live to the running game through run/vdolls.live's
    lines after the count (a game of beta 18 reads the count alone: kept first and whole), kept by vdoll-count; vdolls' keys,
    "applied", "levels_now" from the game's last `vdoll: levels now` line since its start, the top's "vdoll_adapt"; a
    PLAYABLE DOLL at the virtual dolls' level follows the level set, not the level now."""
    srv = Server(AGENT)
    env = os.path.join(srv.etc, "bot1.env")
    run = os.path.join(srv.root, "var", "bot1", "run")
    live = os.path.join(run, "vdolls.live")
    applied = os.path.join(run, "vdolls.env")

    def text(p):
        return open(p).read() if os.path.exists(p) else ""

    def vd():
        rc, j = srv.run(["vdolls"])
        return (j.get("bots") or [{}])[0] if rc == 0 else j

    rc, j = srv.run(["vdolls"])
    check("adapt: vdolls: \"vdoll_adapt\": true at its top (MGO1 Host then offers the LIVE LEVEL)", rc == 0 and j.get("vdoll_adapt") is True, j)
    b = vd()
    check("adapt: an old server's bot1.env (no VDOLL_ADAPT*): off, 1 to 10, no levels_now", b.get("adapt") is False and b.get("adapt_min") == 1
          and b.get("adapt_max") == 10 and "levels_now" not in b, b)
    # no running game: written for the next one
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"adapt": True}))
    check("adapt: vdoll-set adapt true, no running game: VDOLL_ADAPT=1, told back, the next game", rc == 0 and "VDOLL_ADAPT=1\n" in text(env)
          and (j.get("vdolls") or {}).get("adapt") is True and j.get("adapt_takes_effect") == "next game" and j.get("vdoll_adapt") is True
          and "VDOLL_ADAPT_MIN" not in text(env), (j, text(env)))
    for given, want in ((False, "0"), ("on", "1"), ("OFF", "0"), (1, "1"), (0, "0"), ("true", "1"), (None, "0"), ("", "0"), (" 1 ", "1")):
        rc, j = srv.run(["vdoll-set", "1"], json.dumps({"adapt": given}))
        check("adapt: vdoll-set adapt %r: VDOLL_ADAPT=%s" % (given, want), rc == 0 and "VDOLL_ADAPT=%s\n" % want in text(env)
              and text(env).count("VDOLL_ADAPT=") == 1 and (j.get("vdolls") or {}).get("adapt") is (want == "1"), (j, text(env)))
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"adapt_min": 3, "adapt_max": "8"}))
    check("adapt: vdoll-set adapt_min 3, adapt_max \"8\": written, told back", rc == 0 and "VDOLL_ADAPT_MIN=3\n" in text(env) and "VDOLL_ADAPT_MAX=8\n" in text(env)
          and (j.get("vdolls") or {}).get("adapt_min") == 3 and (j.get("vdolls") or {}).get("adapt_max") == 8, (j, text(env)))
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"adapt_max": 2}))
    check("adapt: adapt_max 2 under the kept min 3: swapped (2 to 3)", rc == 0 and "VDOLL_ADAPT_MIN=2\n" in text(env) and "VDOLL_ADAPT_MAX=3\n" in text(env), text(env))
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"adapt_min": 9, "adapt_max": 4}))
    check("adapt: adapt_min 9, adapt_max 4: the lower first", rc == 0 and "VDOLL_ADAPT_MIN=4\n" in text(env) and "VDOLL_ADAPT_MAX=9\n" in text(env), text(env))
    srv.run(["vdoll-set", "1"], json.dumps({"adapt": True, "count": 2, "level": 10}))
    for key, bad in (("adapt", "maybe"), ("adapt", 2), ("adapt", [True]), ("adapt", {"a": 1}), ("adapt", "yes please"),
                     ("adapt_min", 0), ("adapt_min", 11), ("adapt_max", "x"), ("adapt_max", True), ("adapt_max", 5.5), ("adapt_min", None)):
        before = text(env)
        rc, j = srv.run(["vdoll-set", "1"], json.dumps({key: bad, "level": 3, "count": 1}))
        code = "vdoll_adapt" if key == "adapt" else "vdoll_adapt_level"
        check("adapt: vdoll-set %s %r refused (%s), bot1.env unchanged (the other keys neither)" % (key, bad, code), rc == 1 and j.get("code") == code
              and text(env) == before and (key == "adapt" or (j.get("args") or [None])[0] == key), j)
    # a running game started by the beta 19 launcher: its live file and its vdolls.env
    os.makedirs(run, exist_ok=True)
    with open(live, "w") as f:
        f.write("2\n")
    with open(applied, "w") as f:
        f.write("VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=10\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=direct\nVDOLL_ADAPT=0\nVDOLL_ADAPT_MIN=1\nVDOLL_ADAPT_MAX=10\n")
    b = vd()
    check("adapt: a game started with the live level off: applied off, no levels_now", (b.get("applied") or {}).get("adapt") is False and "levels_now" not in b, b)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"adapt": True}))
    check("adapt: vdoll-set adapt true with a running game: now; the live file's count first and whole, then the three lines",
          rc == 0 and j.get("adapt_takes_effect") == "now" and text(live) == "2\nadapt 1\nadapt_min 4\nadapt_max 9\n", (j, text(live)))
    v = text(live)
    check("adapt: a game of beta 18 reads the count from that file as before (its first 15 bytes, strtol)", re.match(r"^\s*([0-9]+)", v[:15]).group(1) == "2", v)
    check("adapt: the run's vdolls.env says what the game has now (applied on, 4 to 9), nothing pending",
          "VDOLL_ADAPT=1\n" in text(applied) and (vd().get("applied") or {}).get("adapt") is True and (vd().get("applied") or {}).get("adapt_min") == 4
          and vd().get("pending") is False, (text(applied), vd()))
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"adapt_min": 1, "adapt_max": 7}))
    check("adapt: the bounds live", rc == 0 and j.get("adapt_takes_effect") == "now" and text(live) == "2\nadapt 1\nadapt_min 1\nadapt_max 7\n", text(live))
    rc, j = srv.run(["vdoll-count", "1", "1"])
    check("adapt: vdoll-count 1: the count changed, the live level's lines kept", rc == 0 and text(live) == "1\nadapt 1\nadapt_min 1\nadapt_max 7\n", text(live))
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"level": 6}))
    check("adapt: other keys: the live file untouched, no adapt_takes_effect", rc == 0 and "adapt_takes_effect" not in j
          and text(live) == "1\nadapt 1\nadapt_min 1\nadapt_max 7\n", (j, text(live)))
    rc, j = srv.run(["vdoll-set", "1", "--restart"], json.dumps({"adapt": False}))
    check("adapt: with --restart: the next game reads bot1.env (no live write)", rc == 0 and "VDOLL_ADAPT=0\n" in text(env)
          and text(live) == "1\nadapt 1\nadapt_min 1\nadapt_max 7\n", (j, text(live)))
    srv.run(["vdoll-set", "1"], json.dumps({"adapt": False}))
    check("adapt: adapt false live: `adapt 0`", text(live) == "1\nadapt 0\nadapt_min 1\nadapt_max 7\n", text(live))
    # a file from Windows written by hand
    acct = "HOST_ACCOUNT_ID=HOSTACC\r\nHOST_ACCOUNT_PASSWORD=TOPSECRET\r\nUDP_PORT=5730\r\nVDOLL_COUNT=2\r\n"
    for lines, want in (('VDOLL_ADAPT="1"\r\nVDOLL_ADAPT_MIN=\'3\'\r\nVDOLL_ADAPT_MAX=08\r\n', (True, 3, 8)),
                        ("VDOLL_ADAPT=yes\r\nVDOLL_ADAPT_MIN=0\r\nVDOLL_ADAPT_MAX=11\r\n", (False, 1, 10)),
                        ("VDOLL_ADAPT=1\r\nVDOLL_ADAPT_MIN=9\r\nVDOLL_ADAPT_MAX=2\r\n", (True, 2, 9)), ("", (False, 1, 10))):
        with open(env, "w", newline="") as f:
            f.write(acct + lines)
        b = vd()
        check("adapt: bot1.env by hand %r: %s" % (lines[:40], want), (b.get("adapt"), b.get("adapt_min"), b.get("adapt_max")) == want, b)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"level": 4}))
    check("adapt: vdoll-set on a file from Windows: the account kept", rc == 0 and "HOST_ACCOUNT_PASSWORD=TOPSECRET\n" in text(env), text(env))
    # levels_now: the game's last `vdoll: levels now` line since its start, the live level on in the running game
    srv.run(["vdoll-set", "1"], json.dumps({"adapt": True, "adapt_min": 1, "adapt_max": 10, "count": 2}))
    logdir = os.path.join(srv.root, "log", "bot1")
    os.makedirs(logdir, exist_ok=True)
    day = os.path.join(logdir, time.strftime("%Y-%m-%d", time.gmtime()) + ".log")
    t = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime())
    with open(day, "w") as f:
        f.write("%s I mgo1host: bot 1, UDP port 5730, disc /opt/mgo1host/disc, version build 1.00 0123456789ab\n" % t)
        f.write("%s I ps2 f100: vdoll: level: live, between 1 and 10 (the levels set 10,10)\n" % t)
        f.write("%s I ps2 f200: vdoll: levels now 10.00 10.00 (set 10,10)\n" % t)
        f.write("%s I ps2 f300: VDOLL ADAPT: vdoll 1 old=10.00 new=9.75 target=8.10 base=10 enemy_strength=2.1 ally_strength=6.0 balance=-0.30 confidence=0.40 reason=ally_team_dominating\n" % t)
        f.write("%s I ps2 f400: vdoll: levels now 9.75 10 (set 10,10)\n" % t)
        f.write("%s I ps2 f401: net: something else\n" % t)
    b = vd()
    check("adapt: levels_now from the last `vdoll: levels now` line, two decimals, in the dolls' order, with its levels set",
          b.get("levels_now") == [9.75, 10.0] and b.get("levels_now_set") == [10, 10], b)
    with open(day, "a") as f:
        f.write("%s I mgo1host: bot 1, UDP port 5730, disc /opt/mgo1host/disc, version build 1.00 0123456789ab\n" % t)
    check("adapt: a new start since that line: no levels_now (not said by this game yet)", "levels_now" not in vd(), vd())
    with open(day, "a") as f:
        f.write(("%s I ps2 f500: net: a long line %s\n" % (t, "x" * 200)) * 3000)       # (more than a block of the backward read)
        f.write("%s I ps2 f600: vdoll: levels now 7.25 8.5 6 (set 10,10,5)\n" % t)
        f.write(("%s I ps2 f700: net: a long line %s\n" % (t, "y" * 200)) * 3000)
    check("adapt: a line 650 kB from the file's end found", vd().get("levels_now") == [7.25, 8.5, 6.0] and vd().get("levels_now_set") == [10, 10, 5], vd())
    with open(day, "a") as f:
        f.write("%s I ps2 f800: vdoll: level: fixed, the live level off (the levels set 10,10)\n" % t)
    check("adapt: the game turned the live level off since (`vdoll: level: fixed`): no levels_now", "levels_now" not in vd(), vd())
    with open(day, "a") as f:
        f.write("%s I ps2 f900: vdoll: level: live, between 1 and 10 (the levels set 10,10)\n" % t)
        f.write("%s I ps2 f901: vdoll: levels now 9.50 (set 10)\n" % t)
        f.write("%s I ps2 f902: vdoll: levels now 9.00 8.00 (set none)\n" % t)
    check("adapt: a line whose set is not one a level: none taken from it (the one before not taken either)", "levels_now" not in vd(), vd())
    with open(day, "a") as f:
        f.write("%s I ps2 f903: vdoll: levels now 9.00 8.00 (set 10)\n" % t)
    check("adapt: a set list of another length: levels_now without levels_now_set", vd().get("levels_now") == [9.0, 8.0] and "levels_now_set" not in vd(), vd())
    with open(applied, "w") as f:
        f.write("VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=10\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=direct\nVDOLL_ADAPT=0\nVDOLL_ADAPT_MIN=1\nVDOLL_ADAPT_MAX=10\n")
    check("adapt: the running game's live level off: no levels_now", "levels_now" not in vd(), vd())
    with open(applied, "w") as f:
        f.write("VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=4\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=direct\n")     # (a 3.5 launcher's game)
    b = vd()
    check("adapt: a game started by the 3.5 launcher: no adapt in applied, no levels_now, nothing pending", "adapt" not in (b.get("applied") or {})
          and "levels_now" not in b and b.get("pending") is False, b)
    os.remove(day)
    with open(applied, "w") as f:
        f.write("VDOLL_COUNT=2\nVDOLL_TEAMS=auto\nVDOLL_LEVEL=10\nVDOLL_WEAPONS=auto\nVDOLL_CONTROLS=direct\nVDOLL_ADAPT=1\nVDOLL_ADAPT_MIN=1\nVDOLL_ADAPT_MAX=10\n")
    check("adapt: no log today: no levels_now", "levels_now" not in vd(), vd())
    # a playable doll at the virtual dolls' level: the level SET (10), whatever the levels now say
    with open(day, "w") as f:
        f.write("%s I ps2 f200: vdoll: levels now 3.00 3.00 (set 10,10)\n" % t)
    srv.run(["vdoll-set", "1"], json.dumps({"level": 10}))
    rc, j = srv.run(["doll-set", "1"], json.dumps({"bot": 1, "difficulty": "vdoll"}))
    d1 = ([d for d in srv.run(["dolls"])[1].get("dolls", []) if d.get("doll") == 1] or [{}])[0]
    check("adapt: a PLAYABLE DOLL at the virtual dolls' level follows the level set (10), not the level now (3.00)", vd().get("levels_now") == [3.0, 3.0]
          and d1.get("difficulty") == 10 and d1.get("difficulty_from") == "vdoll", (d1, vd().get("levels_now")))
    doc = load(AGENT, "agent_help4").__doc__
    check("adapt: the help tells vdoll-set's adapt, adapt_min, adapt_max, levels_now and vdoll_adapt", '"adapt_min", "adapt_max"' in doc.split("The virtual dolls' live level:")[1]
          and '"levels_now"' in doc and '"vdoll_adapt": true' in doc and "level\n  SET" in doc or "level SET" in doc, doc[-1500:])
    shutil.rmtree(srv.root, ignore_errors=True)


def parallel_check():
    """(v150-host beta 16) The commands that write the settings, many at once (the admin's program and its owner, two admins,
    the daily restart's timer): each waits for the one before (the agent's lock), so no key is lost (each read a file,
    changed its own keys and wrote it back: the last one won), each writes a file of its own (one .tmp name for all before)
    and none answers an error; a settings file that cannot be read is never written anew with the new keys alone."""
    srv = Server(AGENT)
    env, senv = os.path.join(srv.etc, "bot1.env"), os.path.join(srv.etc, "server.env")
    with open(senv, "w") as f:
        f.write("DISC_DIR=/opt/mgo1host/disc\n")
    with open(os.path.join(srv.etc, "bot1.game"), "w") as f:
        f.write("name = GAME\nmax_players = 9\n")
    calls = [(["vdoll-set", "1"], json.dumps({"level": "7,8"})), (["vdoll-set", "1"], json.dumps({"teams": "red,blue"})),
             (["vdoll-set", "1"], json.dumps({"weapons": "svd"})), (["vdoll-count", "1", "2"], ""), (["host-unranked", "always"], ""),
             (["doll-list", "own"], ""), (["doll-keep-seat", "off"], ""), (["host-list-9", "8"], "")]
    bad = 0
    for turn in range(3):                                   # (the same 8 commands at once, three times over)
        ps = [subprocess.Popen([sys.executable, AGENT] + a, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=srv.env)
              for a, _ in calls]
        for q, (_, body) in zip(ps, calls):
            q.stdin.write(body)
            q.stdin.close()
        for q in ps:
            o = q.stdout.read()
            q.stdout.close()
            q.stderr.close()
            if q.wait() != 0 or '"ok":true' not in o:
                bad += 1
    etext, stext = open(env).read(), open(senv).read()
    check("24 writing commands, 8 at once: every one answered ok", bad == 0, bad)
    check("bot1.env: every command's key there, the account kept, each key once", all(k in etext for k in ("VDOLL_LEVEL=7,8\n", "VDOLL_TEAMS=red,blue\n",
          "VDOLL_WEAPONS=svd\n", "VDOLL_COUNT=2\n", "HOST_ACCOUNT_ID=HOSTACC\n", "HOST_ACCOUNT_PASSWORD=TOPSECRET\n", "UDP_PORT=5730\n"))
          and etext.count("VDOLL_LEVEL=") == 1 and etext.count("VDOLL_COUNT=") == 1, etext)
    check("server.env: every command's key there, the line it had kept", all(k in stext for k in ("HOST_UNRANKED=always\n", "DOLL_LIST=own\n",
          "DOLL_KEEP_SEAT=0\n", "HOST_LIST_9_AS_8=1\n", "DISC_DIR=/opt/mgo1host/disc\n")), stext)
    check("no temporary file left, the lock's file there", not [f for f in os.listdir(srv.etc) if f.endswith(".tmp")]
          and os.path.exists(os.path.join(srv.etc, ".agent.lock")), os.listdir(srv.etc))
    # a settings file that is there and cannot be read (here: a folder in its place): the command ends with an error and
    # writes nothing (before: the file written anew with the new keys alone, the bot's account gone)
    os.rename(env, env + ".kept")
    os.makedirs(env)
    rc, j = srv.run(["vdoll-set", "1"], json.dumps({"level": 3}))
    check("a bot's settings that cannot be read: an error, nothing written in their place", rc == 1 and j.get("ok") is False and os.path.isdir(env)
          and not [f for f in os.listdir(srv.etc) if f.endswith(".tmp")], j)
    os.rmdir(env)
    os.rename(env + ".kept", env)
    rc, j = srv.run(["doll-list", "host"])
    os.remove(senv)
    rc, j = srv.run(["doll-list", "off"])
    check("a settings file that is not there yet: made with the key, as before", rc == 0 and open(senv).read() == "DOLL_LIST=off\n", j)
    # a command that only reads never waits for the lock
    import fcntl
    with open(os.path.join(srv.etc, ".agent.lock"), "a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        t0 = time.time()
        rc, j = srv.run(["vdolls"])
        check("a command that only reads does not wait for a writer", rc == 0 and time.time() - t0 < 20, j)
        env2 = dict(srv.env, MGO1_AGENT_LOCK_WAIT="1")
        p = subprocess.run([sys.executable, AGENT, "doll-list", "own"], capture_output=True, text=True, timeout=60, env=env2)
        j = json.loads(p.stdout.splitlines()[0]) if p.stdout.strip() else {}
        check("a writer whose turn does not come in its time: refused (busy), nothing written", p.returncode == 1 and j.get("code") == "busy"
              and open(senv).read() == "DOLL_LIST=off\n", p.stdout + p.stderr)


def main():
    srv = Server(AGENT)
    # ---- the CLI and the file as before ------------------------------------------------------------
    rc, j = srv.run(["settings-get"])
    check("settings-get with no file: ok, no settings", rc == 0 and j.get("ok") is True and j.get("settings") == {}, j)
    check("settings-get: the keys, kinds and limits of agent 2.4 (v141: 16 players), in order", list(j.get("keys", {}).items()) == KEYS_2_4, j.get("keys"))
    full = {"name": "EU DM 1", "comment": "Dedicated", "password": "", "rotation": "1:2,0:8", "red_team": "0", "blue_team": "1",
            "max_players": "9", "weapons": "0", "dm_minutes": "5", "tdm_minutes": "5", "tdm_rounds": "2", "tdm_tickets": "30",
            "res_minutes": "4", "res_rounds": "7", "cap_minutes": "4", "cap_rounds": "5", "sne_minutes": "4", "sne_rounds": "7",
            "auto_balance": "2", "idle_kick_minutes": "5", "team_kill_count": "3", "rating_restriction": "0", "rating": "1000",
            "unique_characters": "off", "rumble_roses": "off", "ghosts": "off", "friendly_fire": "ON", "team_auto_balance": "on",
            "idle_kick": "on", "team_kill_kick": "off", "voice_chat": "on"}
    rc, j = srv.set(dict(reversed(list(full.items()))))
    check("settings-set: every key (as MGO1 Host sends them)", rc == 0 and j.get("ok") is True, j)
    lines = [l for l in srv.game().splitlines() if not l.startswith("#")]
    check("the file: the keys in the table's order", [l.split(" = ")[0] for l in lines] == [k for k, _ in KEYS_2_4], lines)
    check("the file: the rotation as pairs, the switches in small letters", "rotation = 1:2, 0:8" in lines and "friendly_fire = on" in lines, lines)
    rc, j = srv.run(["settings-get"])
    check("settings-get: what was set", rc == 0 and j["settings"].get("rotation") == "1:2, 0:8" and j["settings"].get("tdm_tickets") == "30", j)
    rc, j = srv.run(["--bot", "1", "settings-set"], json.dumps({"rotation": "4:12"}))
    check("--bot 1 settings-set: a key alone", rc == 0 and j.get("settings") == {"rotation": "4:12"}, j)
    rc, j = srv.run(["--bot", "2", "settings-get"])
    check("--bot 2 (none): no_bot", rc == 1 and j.get("code") == "no_bot", j)
    rc, j = srv.run(["settings-set"], "not json")
    check("settings-set: not JSON refused", rc == 1 and j.get("code") == "json", j)

    # ---- rotations -------------------------------------------------------------------------------
    good = {"1:2, 0:8": "1:2, 0:8", "1:2,0:8": "1:2, 0:8", " 4:12 ": "4:12", "3:01": "3:01", "0:1,,2:3": "0:1, 2:3",
            ", ".join("%d:%d" % (i % 5, i % 12 + 1) for i in range(15)): ", ".join("%d:%d" % (i % 5, i % 12 + 1) for i in range(15))}
    for rot, want in good.items():
        rc, j = srv.set({"rotation": rot})
        check("rotation %r accepted as %r" % (rot[:40], want[:40]), rc == 0 and j.get("settings", {}).get("rotation") == want, j)
    srv.set({"rotation": "1:2, 0:8"})
    for rot in ("", " , ", ", ".join(["1:2"] * 16)):
        refused(srv, "rotation %r: 1 to 15 pairs" % rot[:30], {"rotation": rot}, "rotation", [])
    for rot, pair in (("7:1", "7:1"), ("0:0", "0:0"), ("0:16", "0:16"), ("1:2, 9:9", "9:9"), ("00:1", "00:1"), ("1:012", "1:012"),
                      ("1:2:3", "1:2:3"), ("a:b", "a:b"), ("1-2", "1-2"), ("-1:2", "-1:2"), ("1:2, 4:100", "4:100")):
        refused(srv, "rotation %r: the pair %r refused" % (rot, pair), {"rotation": rot}, "rotation_pair", [pair])
    rc, j = srv.set({"rotation": "0:16"})
    check("the rotation's error text (v141: rules 0 to 6, maps 1 to 15)", j.get("error") == "rotation: rule 0-6 : map 1-15 (0:16)", j)
    # (v150-host beta 22, sync) a PC map's entry is taken only on a server that has the map (a map.ini naming it in the maps
    # folder: tests/test_settings_sync.py); with none, refused rotation_map_missing
    refused(srv, "beta 22: rotation '0:13' on a server without the PC maps refused", {"rotation": "0:13"}, "rotation_map_missing", ["0:13"])
    for folder, mid in (("n18", 13), ("n21", 14), ("heliport", 15)):
        os.makedirs(os.path.join(srv.root, "opt", "maps", folder), exist_ok=True)
        with open(os.path.join(srv.root, "opt", "maps", folder, "map.ini"), "w") as f:
            f.write("id = %d\n" % mid)
    for rot in ("5:1", "6:12", "0:13", "1:14", "3:15", "5:13, 6:15"):
        rc, j = srv.set({"rotation": rot})
        check("v141: rotation %r accepted (TEAM SNEAKING, BASE CAPTURE, the PC maps)" % rot, rc == 0, j)
    srv.set({"rotation": "1:2, 0:8"})

    # ---- values ----------------------------------------------------------------------------------
    agent = load(AGENT, "agent")
    for rule in agent.RULES:
        for kind, (lo, hi) in rule["settings"].items():
            key = "%s_%s" % (rule["prefix"], kind)
            rc, j = srv.set({key: str(lo)})
            rc2, j2 = srv.set({key: str(hi)})
            check("%s: %d and %d accepted" % (key, lo, hi), rc == 0 and rc2 == 0, (j, j2))
            for bad in (str(lo - 1), str(hi + 1), "-1", "5.5", "x", ""):
                refused(srv, "%s = %r refused" % (key, bad), {key: bad}, "setting_range", [key, str(lo), str(hi)])
    refused(srv, "dm_rounds (deathmatch has no rounds): unknown", {"dm_rounds": "3"}, "setting_unknown", ["dm_rounds"])
    refused(srv, "res_tickets (rescue has no tickets): unknown", {"res_tickets": "3"}, "setting_unknown", ["res_tickets"])
    refused(srv, "max_players 17 refused", {"max_players": "17"}, "setting_range", ["max_players", "3", "16"])
    rc, j = srv.set({"max_players": "16"})
    check("v141: max_players 16 accepted", rc == 0, j)
    refused(srv, "rating 99 refused", {"rating": "99"}, "setting_range", ["rating", "100", "9900"])
    refused(srv, "auto_balance 1 refused", {"auto_balance": "1"}, "setting_range", ["auto_balance", "2", "6"])
    refused(srv, "a name of 2 characters refused", {"name": "AB"}, "setting_len", ["name", "3", "15"])
    refused(srv, "a name of 16 characters refused", {"name": "A" * 16}, "setting_len", ["name", "3", "15"])
    refused(srv, "a comment of 76 characters refused", {"comment": "c" * 76}, "setting_len", ["comment", "0", "75"])
    refused(srv, "a name with an accent refused", {"name": "CAF\u00c9 DM"}, "setting_ascii", ["name"])
    refused(srv, "a password lock with a sign refused", {"password": "AB!CD"}, "setting_pw", [])
    refused(srv, "a switch neither on nor off refused", {"ghosts": "maybe"}, "setting_onoff", ["ghosts"])
    rc, j = srv.set({"name": "ABC", "comment": "", "password": "LOCK.1_-", "ghosts": "OFF"})
    check("the limits themselves accepted", rc == 0 and j["settings"] == {"name": "ABC", "comment": "", "password": "LOCK.1_-", "ghosts": "off"}, j)
    with open(EXAMPLE) as f:
        example = agent.settings_parse(f.read())
    try:
        agent.settings_check(example)
        check("host/game.ini.example passes the checks", True)
    except agent.Fail as e:
        check("host/game.ini.example passes the checks", False, e)

    # ---- the tables ------------------------------------------------------------------------------
    ids = [r["id"] for r in agent.RULES]
    mids = [m["id"] for m in agent.MAPS]
    check("RULES: numbers and prefixes once each", len(set(ids)) == len(ids) and len({r["prefix"] for r in agent.RULES}) == len(ids))
    stock = [m["page"] for m in agent.MAPS if not m["pc_only"]]
    check("MAPS: numbers once each, the stock maps' pages once each (a PC map takes a stock map's slot)",
          len(set(mids)) == len(mids) and len(set(stock)) == len(stock) and all(m["page"] in stock for m in agent.MAPS if m["pc_only"]))
    check("RULES: each rule's maps in MAPS", all(r["maps"] is None or set(r["maps"]) <= set(mids) for r in agent.RULES))
    src = open(RULES_CPP, encoding="utf-8").read()
    crules = re.findall(r'\{(\d+), "([a-z0-9]+)", "(rule_[a-z0-9_]+)", (?:true|false), \{((?:\{[^{}]*\}(?:, )?)*)\}, \{([^{}]*)\}\}', src)
    cmaps = re.findall(r'\{(\d+), "(n[0-9a-z]+)", "([^"]+)", (true|false)\}', src)
    prog = []
    for rid, prefix, _, vals, maps in crules:
        v = re.findall(r'\{([^{}]*)\}', vals)
        lims = {}
        for kind, cell in zip(("minutes", "rounds", "tickets"), v):
            if cell.strip():
                lo, hi, _ = (int(x) for x in cell.split(","))
                if hi > 0:
                    lims[kind] = (lo, hi)
        prog.append((int(rid), prefix, lims, [int(x) for x in maps.split(",")] if maps.strip() else None))
    mine = [(r["id"], r["prefix"], {k: tuple(v) for k, v in r["settings"].items()}, r["maps"]) for r in agent.RULES]
    check("the program's rules (rules.cpp) are the agent's", prog == mine, (prog, mine))
    pmaps = [(int(i), p, n, f == "true") for i, p, n, f in cmaps]
    amaps = [(m["id"], m["page"], m["name"].upper(), m["pc_only"]) for m in agent.MAPS]
    check("the program's maps (rules.cpp) are the agent's", pmaps == amaps, (pmaps, amaps))

    # ---- v141: the table edited for real (team sneaking 5 and base capture 6, the PC maps 13 to 15): checked as it is --------
    check("v141: rules 5 TEAM SNEAKING and 6 BASE CAPTURE", [(r["id"], r["prefix"]) for r in agent.RULES][5:] == [(5, "tsne"), (6, "bc")], agent.RULES)
    check("v141: maps 13 to 15, PC players only", [(m["id"], m["pc_only"]) for m in agent.MAPS][12:] == [(13, True), (14, True), (15, True)], agent.MAPS)
    rc, j = srv.set(full)
    check("v141: the settings of before still accepted", rc == 0, j)
    # (v141 beta 5) REIKO HINOMOTO/ROWDY REIKO take two unique characters' places: the unique characters on with them
    rc, j = srv.set(dict(full, rumble_roses="on", unique_characters="off"))
    check("v141 beta 5: rumble_roses on sets unique_characters on", rc == 0 and j.get("settings", {}).get("unique_characters") == "on"
          and "unique_characters = on" in srv.game(), j)
    rc, j = srv.set(dict(full, rumble_roses="off", unique_characters="off"))
    check("v141 beta 5: rumble_roses off leaves unique_characters as asked", rc == 0 and j.get("settings", {}).get("unique_characters") == "off", j)
    # (v141 beta 5) a Host Only game of 9 told to the lobby as 9 (the default) or 8 (server.env HOST_LIST_9_AS_8)
    rc, j = srv.run(["settings-get"])
    check("v141 beta 5: settings-get tells host_list_9, 9 by default", rc == 0 and j.get("host_list_9") == 9, j)
    rc, j = srv.run(["host-list-9", "8"])
    senv = open(os.path.join(srv.etc, "server.env")).read() if os.path.exists(os.path.join(srv.etc, "server.env")) else ""
    check("v141 beta 5: host-list-9 8 writes HOST_LIST_9_AS_8=1", rc == 0 and j.get("host_list_9") == 8 and "HOST_LIST_9_AS_8=1" in senv, (j, senv))
    rc, j = srv.run(["settings-get"])
    check("v141 beta 5: settings-get tells 8 then", rc == 0 and j.get("host_list_9") == 8, j)
    rc, j = srv.run(["host-list-9", "9"])
    check("v141 beta 5: host-list-9 9 back", rc == 0 and j.get("host_list_9") == 9, j)
    rc, j = srv.run(["host-list-9", "7"])
    check("v141 beta 5: host-list-9 7 refused", rc == 1 and j.get("code") == "usage", j)

    # ---- (v141 beta 6) the bot's status: its players' kinds counted, the lobby lost's countdown. (v141 beta 7) agent 3.1, (virtual dolls) 3.2 -
    rc, j = srv.run(["version"])
    check("agent build 1.00 (the version every answer and MGO1 Host show, no beta number)", rc == 0 and j.get("agent") == "build 1.00", j)
    rc, j = srv.run(["status"])
    check("build 1.00: the status says the agent as \"build 1.00\" too", rc == 0 and j.get("agent") == "build 1.00", j)
    rc, j = srv.run(["dolls"])
    check("dolls on a server with no doll yet: \"vdoll_difficulty\": true (MGO1 Host then offers AS THE VIRTUAL DOLLS), the agent build 1.00",
          rc == 0 and j.get("dolls") == [] and j.get("vdoll_difficulty") is True and load(AGENT, "agent_v").AGENT_VERSION == "build 1.00", j)
    # (build 1.00) the help (the agent's first lines) without the betas' tags or the agents' old numbers, its content kept
    tags = re.findall(r"[^\n]{0,40}(?:\bbeta\b|\bv1[0-9][0-9]\b|\bv150\b|\bagent [0-9]|\b3\.[0-9]\b)[^\n]{0,20}", agent.__doc__, re.I)
    check("build 1.00: the help names no beta and no old version", not tags, tags)
    check("build 1.00: the help still tells crashes, vdoll-count, doll-list and the live level",
          all(k in agent.__doc__ for k in ("(the crash reports, a game that ended badly:", "(the bot's virtual dolls wanted now",
                                            "(how TX-55's game list", "The virtual dolls' live level:", "an older game reads the count alone")))
    check("v141 beta 7: the help tells host-unranked", "host-unranked <auto|always|off>" in agent.__doc__, agent.__doc__[:200])
    check("v141 beta 6: the help marks host-list-9 as developer compatibility", "host-list-9 <9|8>" in agent.__doc__
          and "developer compatibility" in agent.__doc__.split("host-list-9 <9|8>")[1].split("account-get")[0])
    status_check(srv)
    crash_check(srv)
    vdolls_check(srv)
    switches_check()
    adapt_check()
    parallel_check()

    shutil.rmtree(srv.root, ignore_errors=True)
    print("\n%d check(s) failed" % len(FAILS) if FAILS else "\nall checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
