#!/usr/bin/env python3
"""mgo1doll-manager's decisions, off line: a stand-in systemctl (the units' states in a file), the
bots' status files and the dolls' env files in a temporary folder, the manager run a few rounds each
time. Checks: the dolls started to the free seats (one seat kept for a real player), the server's
capacity, the dolls' names (not real players), a doll not in the game yet stopped first and the last
ones told to leave when real players come, then stopped, disabled dolls stopped, the keep-seat setting
read live; the accountless dolls (DOLL_<N>, a bot's DOLL_COUNT, DOLL_GATE=0); the identity (a name not
trusted while the doll does not know who it is, two dolls with one name); a unit with no status file
restarted; systemctl's failures and time limit; a doll starting holds its seat (activating), one never
in the game started again; a service without its settings file stopped; mgo1host-agent's dolls JSON
and doll-count; (v141 beta 5) the accountless dolls' names (doll-set "name", doll-count's, the names file); (v141 beta 6)
the ladder 1 to 10 and the live level's bounds (doll-set, doll-order, doll-all, the summaries); (v141 beta 9) the pace guard
(the dolls or the bot below the game's pace 2 minutes in a row with a real player in: one doll less; at its pace again: one
more, then no cap; the bot slowed: down to none; DOLL_PACE_GUARD=0: off; no real player: nothing); the join timeout in the doll's own
game's time (its status file's frames), the steal of the VPS's machine; (v150-host beta 16) why a bot's playable dolls do not
start (the manager's line, once a reason, and mgo1host-agent's `dolls` with the same sums: both given the same folders and
compared), a doll at its bot's virtual dolls' level (DOLL_DIFFICULTY=vdoll: the manager's dolls.levels, the agent's orders).
    python3 tests/test_doll_manager.py
"""
import json, os, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
MANAGER = os.path.join(HERE, "..", "host", "mgo1doll-manager")
AGENT = os.path.join(HERE, "..", "host", "mgo1host-agent")

# the stand-in: is-active answers a state a unit (one or several), <db>.states {unit: state} gives one
# (activating ...); FAIL_UNITS: their start fails; SLOW: an action that answers in that many seconds
STANDIN = r'''#!/usr/bin/env python3
import json, os, sys, time
db = os.environ["UNITS_DB"]
units = set(open(db).read().split()) if os.path.exists(db) else set()
forced = json.load(open(db + ".states")) if os.path.exists(db + ".states") else {}
args = [a for a in sys.argv[1:] if not a.startswith("--")]
quiet = "--quiet" in sys.argv
cmd, rest = args[0], args[1:]
def state(u):
    return forced.get(u) or ("active" if u in units else "inactive")
if cmd == "is-active":
    if not quiet:
        print("\n".join(state(u) for u in rest))
    sys.exit(0 if all(state(u) == "active" for u in rest) else 3)
unit = rest[0] if rest else ""
if os.environ.get("SLOW"):
    time.sleep(float(os.environ["SLOW"]))
if cmd == "start" and unit in os.environ.get("FAIL_UNITS", "").split(","):
    sys.stderr.write("Job for %s failed because the control process exited with error code.\n" % unit)
    sys.exit(1)
if cmd in ("start", "restart"):
    units.add(unit)
elif cmd == "stop":
    units.discard(unit)
with open(db, "w") as f:
    f.write("\n".join(sorted(units)))
with open(db + ".log", "a") as f:
    f.write(cmd + " " + unit + "\n")
'''


class Box:
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="dollman_")
        self.etc = os.path.join(self.dir, "etc")
        self.var = os.path.join(self.dir, "var")
        os.makedirs(self.etc)
        os.makedirs(self.var)
        self.db = os.path.join(self.dir, "units")
        self.systemctl = os.path.join(self.dir, "systemctl")
        with open(self.systemctl, "w") as f:
            f.write(STANDIN)
        os.chmod(self.systemctl, 0o755)
        self.server = {}

    def units(self):
        return set(open(self.db).read().split()) if os.path.exists(self.db) else set()

    def set_units(self, units):
        with open(self.db, "w") as f:
            f.write("\n".join(sorted(units)))

    def force(self, states):
        with open(self.db + ".states", "w") as f:
            json.dump(states, f)

    def log(self):
        return open(self.db + ".log").read() if os.path.exists(self.db + ".log") else ""

    def write(self, path, text):
        full = os.path.join(self.dir, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w") as f:
            f.write(text)

    def bot(self, n, players, most=9, hosting=True, count=None, vdolls=None):
        self.write(f"etc/bot{n}.env", "HOST_ACCOUNT_ID=BOT%d\n" % n + ("DOLL_COUNT=%d\n" % count if count is not None else ""))
        self.write(f"etc/bot{n}.game", "name = GAME%d\nmax_players = %d\n" % (n, most))
        # (v141 beta 6) a player given as a dict: its fields as the host writes them (kind, doll)
        st = {"state": "hosting" if hosting else "starting", "game": {"max": most},
              "players": [{"name": "host", "host": True}] + [p if isinstance(p, dict) else {"name": p} for p in players]}
        if vdolls is not None:                              # (the bot's virtual dolls set: the host's status, docs/VIRTUAL_DOLL.md)
            st["vdolls_set"] = vdolls
        self.write(f"var/bot{n}/run/status.json", json.dumps(st))
        self.set_units(self.units() | {f"mgo1host@{n}"})

    def doll(self, n, bot=1, enabled=True, name=None, account=True, identity=None, shown=None):
        acc = "DOLL_ACCOUNT_ID=DOLLACC%d\nDOLL_ACCOUNT_PASSWORD=SECRET%d\n" % (n, n) if account else ""
        acc += "DOLL_NAME=%s\n" % shown if shown else ""
        self.write(f"etc/doll{n}.env", acc + "DOLL_BOT=%d\nDOLL_ENABLED=%d\n" % (bot, 1 if enabled else 0))
        if name:
            st = {"name": name}
            if identity:
                st["identity"] = identity
            self.write(f"var/doll{n}/status.json", json.dumps(st))

    def env(self, **kw):
        return dict(os.environ, MGO1_ETC=self.etc, MGO1_VAR=self.var, MGO1_SYSTEMCTL=self.systemctl, UNITS_DB=self.db,
                    MGO1_LOG_DIR=os.path.join(self.dir, "log"), MGO1LOG=os.path.join(HERE, "..", "host", "mgo1log"), **kw)

    def run(self, rounds, cores=8, mem=16000, keep_seat=True, leave_wait=30, hang=180, grace=180, gate=True, period=0,
            join_wait=240, extra=None, listed=None, server_extra=""):
        self.write("etc/server.env", "DOLL_KEEP_SEAT=%d\n" % (1 if keep_seat else 0) + ("" if gate else "DOLL_GATE=0\n") +
                   ("" if listed is None else "DOLL_LIST=%s\n" % listed) + server_extra)
        stat0 = os.path.join(self.dir, "stat0")             # (v141 beta 12) no steal unless a test gives its own /proc/stat
        if not os.path.exists(stat0):
            self.write("stat0", "cpu  1000 0 1000 8000 0 0 0 0 0 0\n")
        extra = dict(extra or {})
        extra.setdefault("MGO1_PROC_STAT", stat0)
        extra.setdefault("MGO1_HANG_FAST_SECONDS", "0")     # (v150-host beta 16) a bot's 45 s: part 6c's own
        env = self.env(DOLL_MANAGER_PERIOD=str(period), DOLL_START_GAP="0", DOLL_LEAVE_WAIT=str(leave_wait),
                       DOLL_MANAGER_ROUNDS=str(rounds), MGO1_CORES=str(cores), MGO1_MEM_MB=str(mem), MGO1_HANG_SECONDS=str(hang),
                       MGO1_HANG_GRACE=str(grace), DOLL_JOIN_TIMEOUT=str(join_wait), **extra)
        r = subprocess.run([sys.executable, MANAGER], env=env, capture_output=True, text=True, timeout=120)
        if r.returncode != 0:
            raise SystemExit("the manager failed:\n" + r.stdout + r.stderr)
        return r.stdout + r.stderr

    def agent(self, *args, stdin="", extra=None):
        # (the machine's cores, memory and steal as run() gives them to the manager: the same budget counted)
        env = self.env(**dict({"MGO1_CONF_DIR": self.etc, "MGO1_STATE_DIR": self.var, "MGO1_DOLL_MANAGER": MANAGER, "MGO1_CORES": "8",
                               "MGO1_MEM_MB": "16000", "MGO1_PROC_STAT": os.path.join(self.dir, "stat0")}, **(extra or {})))
        r = subprocess.run([sys.executable, AGENT, *args], env=env, input=stdin, capture_output=True, text=True, timeout=120)
        try:
            return json.loads(r.stdout.strip().splitlines()[-1])
        except (ValueError, IndexError):
            return {"ok": False, "raw": r.stdout + r.stderr}

    def dolls_on(self):
        return sorted(int(u.split("@")[1]) for u in self.units() if u.startswith("mgo1doll@"))

    def day_log(self, name):
        p = os.path.join(self.dir, "log", name, time.strftime("%Y-%m-%d", time.gmtime()) + ".log")
        return open(p).read() if os.path.exists(p) else ""

    def sums(self):
        """What the manager counted for each bot's playable dolls in its last round (/var/lib/mgo1host/dolls.sums.json)."""
        p = os.path.join(self.var, "dolls.sums.json")
        return json.load(open(p)) if os.path.exists(p) else {}

    def command(self, n):
        p = os.path.join(self.var, f"doll{n}", "command")
        return open(p).read() if os.path.exists(p) else ""

    def names(self):
        p = os.path.join(self.var, "dolls.names")
        return open(p).read().split() if os.path.exists(p) else []

    def env_files(self):
        return sorted(int(f[4:-4]) for f in os.listdir(self.etc) if f.startswith("doll") and f.endswith(".env"))


FAILS = []


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:600] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


def main():
    # 1. 9 players at most (8 seats), one real player, one seat kept: 6 free; 4 dolls enabled, 1 disabled
    b = Box()
    b.bot(1, ["REAL1"])
    for n in (1, 2, 3, 4):
        b.doll(n)
    b.doll(5, enabled=False)
    out = b.run(6)
    check("the enabled dolls started to the free seats", b.dolls_on() == [1, 2, 3, 4], str(b.dolls_on()))
    check("nothing secret in the manager's output", "SECRET" not in out and "DOLLACC" not in out, out)

    # 2. the dolls' names are not real players: in the game with 5 more real players, 8 seats: 8 - 6 - 1 = 1.
    # Dolls 3 and 4 are not in the game yet: stopped first (nothing to leave); doll 2, the last in it, leaves
    for n in (1, 2, 3, 4):
        b.doll(n, name=f"PLAYER DOLL {n}")
    b.bot(1, ["REAL1", "REAL2", "REAL3", "REAL4", "REAL5", "REAL6", "PLAYER DOLL 1", "PLAYER DOLL 2"])
    out = b.run(2)
    names = b.names()
    check("the dolls' names written", "PLAYER" in names and "DOLL" in names and
          "PLAYER DOLL 1\n" in open(os.path.join(b.var, "dolls.names")).read(), str(names))
    check("the dolls not in the game yet stopped first", b.dolls_on() == [1, 2] and "not in bot 1's game yet" in out, str(b.dolls_on()) + out)
    check("then the last one in the game told to leave", "leave" in b.command(2) and "leave" not in b.command(1), str([b.command(n) for n in (1, 2)]))
    b.run(2, leave_wait=0)
    check("stopped once its time is over", b.dolls_on() == [1], str(b.dolls_on()))

    # 3. the seat kept or not (read live): 8 seats, 6 real: 1 doll with the seat kept, 2 without
    b2 = Box()
    b2.bot(1, ["R1", "R2", "R3", "R4", "R5", "R6"])
    for n in (1, 2, 3):
        b2.doll(n)
    b2.run(4, keep_seat=True)
    check("one seat kept for a real player", b2.dolls_on() == [1], str(b2.dolls_on()))
    b2.run(4, keep_seat=False, listed="off")
    check("every seat taken without it", b2.dolls_on() == [1, 2], str(b2.dolls_on()))
    # (v141 beta 3) the accountless dolls listed in TX-55's game list (DOLL_LIST host, the default, or own): a seat kept anyway
    # (the list would show the game full)
    b2.run(4, keep_seat=False, leave_wait=0)
    check("listed (the default): a seat kept even without DOLL_KEEP_SEAT", b2.dolls_on() == [1], str(b2.dolls_on()))
    b2.run(4, keep_seat=False, listed="off")
    b2.run(4, keep_seat=False, listed="own", leave_wait=0)
    check("listed with their own ids: a seat kept too", b2.dolls_on() == [1], str(b2.dolls_on()))
    b2.run(4, keep_seat=False, listed="off", gate=True)
    check("not listed again: every seat taken", b2.dolls_on() == [1, 2], str(b2.dolls_on()))

    # 4. the server's capacity (v141 beta 12): 2 cores, a quarter kept free (1.5), 1 bot (0.35): 1.15 left, 0.4 a doll: 2 dolls
    b3 = Box()
    b3.bot(1, ["R1"])
    for n in (1, 2, 3, 4, 5):
        b3.doll(n)
    b3.run(10, cores=2)
    check("no more dolls than the cores allow", len(b3.dolls_on()) == 2, str(b3.dolls_on()))
    # 4 cores whose VPS's machine has taken half their time since the start: as 2
    b3s = Box()
    b3s.bot(1, ["R1"])
    for n in (1, 2, 3, 4, 5):
        b3s.doll(n)
    b3s.write("stat50", "cpu  1000 0 1000 3000 0 0 0 5000 0 0\n")
    b3s.run(10, cores=4, extra={"MGO1_PROC_STAT": os.path.join(b3s.dir, "stat50")})
    check("the VPS's steal taken off the cores", len(b3s.dolls_on()) == 2, str(b3s.dolls_on()))
    b4 = Box()
    b4.bot(1, ["R1"])
    for n in (1, 2, 3, 4, 5):
        b4.doll(n)
    b4.run(8, cores=16, mem=1500)                       # 1500 - 400 - 350 = 750: 3 dolls of 250 MB
    check("no more dolls than the memory allows", len(b4.dolls_on()) == 3, str(b4.dolls_on()))

    # 5. two bots share the capacity; a bot that does not host yet gets none; a disabled doll is stopped
    b5 = Box()
    b5.bot(1, ["R1"])
    b5.bot(2, ["R2"], hosting=False)
    for n in (1, 2):
        b5.doll(n, bot=1)
    for n in (3, 4):
        b5.doll(n, bot=2)
    b5.run(6)
    check("no doll for a bot that does not host", b5.dolls_on() == [1, 2], str(b5.dolls_on()))
    b5.doll(2, bot=1, enabled=False)
    b5.run(2)
    check("a disabled doll stopped", b5.dolls_on() == [1], str(b5.dolls_on()))

    # 6. the watchdog: a bot whose status is old (hung) restarted, logged in its daily log; a fresh one left
    b6 = Box()
    b6.bot(1, ["R1"])
    b6.bot(2, ["R2"])
    old = os.path.join(b6.var, "bot1", "run", "status.json")
    os.utime(old, (1000000000, 1000000000))
    b6.run(1, hang=60, grace=0)
    log = b6.log()
    check("a hung bot restarted", "restart mgo1host@1" in log and "restart mgo1host@2" not in log, log)
    day = time.strftime("%Y-%m-%d", time.gmtime())
    blog = os.path.join(b6.dir, "log", "bot1", day + ".log")
    text = open(blog).read() if os.path.exists(blog) else ""
    check("the restart in the bot's daily log", "hung" in text and " W " in text, text)

    # 6b. (v150-host beta 16) a hung game's crash report (docs/CRASH_REPORTS.md): before the restart the manager leaves its request
    # in the game's log folder (crash/.hung: the report's name, what happened); the game's own mgo1log takes it at its stop and
    # writes the report (the stand-in does it here), and the manager's line in the bot's log names the file; none written: said
    hung_standin = '''
import glob
if cmd == "restart":
    for req in glob.glob(os.path.join(os.environ["MGO1_LOG_DIR"], "*", "crash", ".hung")):
        j = json.load(open(req))
        with open(db + ".hung", "a") as f:
            f.write(json.dumps(j) + "\\n")
        if not os.environ.get("NO_REPORT"):
            with open(os.path.join(os.path.dirname(req), j["name"]), "w") as f:
                f.write("MGO1 crash report\\nreason: hung\\n")
            os.remove(req)
'''
    import re
    for no_report in (False, True):
        b6b = Box()
        with open(b6b.systemctl, "a") as f:
            f.write(hung_standin)
        b6b.bot(1, ["R1"])
        os.utime(os.path.join(b6b.var, "bot1", "run", "status.json"), (1000000000, 1000000000))
        os.makedirs(os.path.join(b6b.dir, "log", "bot1"))       # (the game's log folder, its mgo1log's)
        out = b6b.run(1, hang=60, grace=0, extra={"NO_REPORT": "1"} if no_report else None)
        req = [json.loads(x) for x in open(b6b.db + ".hung").read().splitlines()] if os.path.exists(b6b.db + ".hung") else []
        crash = os.path.join(b6b.dir, "log", "bot1", "crash")
        text = open(os.path.join(b6b.dir, "log", "bot1", day + ".log")).read()
        if not no_report:
            check("a hung bot: the manager's request for its crash report is there when its unit is restarted", len(req) == 1
                  and re.match(r"^\d{4}-\d\d-\d\d_\d{6}_hung\.txt$", req[0].get("name", "")) and "its game wrote nothing for" in req[0].get("what", "")
                  and "(hung): restarted by the doll manager" in req[0]["what"], req)
            check("a hung bot: the manager's line in the bot's log names the report", req and os.path.isfile(os.path.join(crash, req[0]["name"]))
                  and (" W mgo1doll-manager: bot1: crash report %s " % os.path.join(crash, req[0]["name"])) in text
                  and text.find("(hung): started again") < text.find("bot1: crash report"), text)
        else:
            check("a hung bot whose logger wrote no report: said, the request removed", len(req) == 1 and " W mgo1doll-manager: bot1: no crash report" in text
                  and os.listdir(crash) == [], (text, os.listdir(crash)))
    b6c = Box()
    b6c.bot(1, ["R1"])
    os.utime(os.path.join(b6c.var, "bot1", "run", "status.json"), (1000000000, 1000000000))
    b6c.run(1, hang=60, grace=0)
    check("a hung bot with no log folder yet: restarted, the folder made by its note, no request left in it", "restart mgo1host@1" in b6c.log()
          and not os.path.exists(os.path.join(b6c.dir, "log", "bot1", "crash")), b6c.log())

    # (the review's P2-1 on that request: the log folders are the games' user's, the manager is root) a `crash` folder or a
    # `.hung` file that is a symbolic link there: nothing is written through it, the game is restarted all the same
    for what in ("the crash folder", "the request file"):
        b6d = Box()
        b6d.bot(1, ["R1"])
        os.utime(os.path.join(b6d.var, "bot1", "run", "status.json"), (1000000000, 1000000000))
        logs = os.path.join(b6d.dir, "log", "bot1")
        os.makedirs(logs)
        elsewhere = os.path.join(b6d.dir, "elsewhere")
        os.makedirs(elsewhere)
        victim = os.path.join(elsewhere, "victim")
        open(victim, "w").write("KEEP\n")
        if what == "the crash folder":
            os.symlink(elsewhere, os.path.join(logs, "crash"))
        else:
            os.makedirs(os.path.join(logs, "crash"))
            os.symlink(victim, os.path.join(logs, "crash", ".hung"))
        b6d.run(1, hang=60, grace=0)
        check("a hung bot, %s a symbolic link to another place: nothing written there, the bot restarted" % what,
              sorted(os.listdir(elsewhere)) == ["victim"] and open(victim).read() == "KEEP\n" and "restart mgo1host@1" in b6d.log(),
              (os.listdir(elsewhere), open(victim).read(), b6d.log()))

    # 6c. (v150-host beta 16) a bot's game that has written its status and then not for 45 s (MGO1_HANG_FAST_SECONDS), seen so on
    # the manager's rounds for two periods more, is restarted without the 180 s; a doll's keeps the 180 s (its status file
    # outlives its restarts); one sight alone is not enough; 0: off
    def aged(box, name, seconds):
        path = os.path.join(box.var, name, "run", "status.json") if name.startswith("bot") else os.path.join(box.var, name, "status.json")
        os.utime(path, (time.time() - seconds, time.time() - seconds))
    fast = {"MGO1_HANG_FAST_SECONDS": "45"}
    b6d = Box()
    b6d.bot(1, ["R1"])
    b6d.bot(2, ["R2"])
    b6d.doll(1, bot=2, name="PLAYER_DOLL1")
    b6d.set_units(b6d.units() | {"mgo1doll@1"})
    aged(b6d, "bot1", 60)
    aged(b6d, "bot2", 30)
    aged(b6d, "doll1", 60)
    out = b6d.run(1, period=0.3, grace=0, extra=fast)
    check("a bot silent 60 s seen once: not restarted yet", "restart" not in b6d.log(), b6d.log())
    aged(b6d, "bot1", 60)
    aged(b6d, "bot2", 30)
    aged(b6d, "doll1", 60)
    out = b6d.run(5, period=0.3, grace=0, extra=fast)
    text = open(os.path.join(b6d.dir, "log", "bot1", day + ".log")).read() if os.path.exists(os.path.join(b6d.dir, "log", "bot1", day + ".log")) else ""
    check("a bot silent 60 s on the manager's rounds: restarted before the 180 s, once, said as hung", b6d.log().count("restart mgo1host@1") == 1
          and re.search(r" W mgo1doll-manager: bot1: its game wrote nothing for 6\d s \(hung\): started again", text), b6d.log() + text)
    check("a bot silent 30 s and a doll silent 60 s: left alone", "restart mgo1host@2" not in b6d.log() and "restart mgo1doll@1" not in b6d.log(), b6d.log())
    b6e = Box()
    b6e.bot(1, ["R1"])
    aged(b6e, "bot1", 60)
    b6e.run(5, period=0.3, grace=0)
    check("MGO1_HANG_FAST_SECONDS=0: the 180 s only", "restart" not in b6e.log(), b6e.log())
    b6e.run(5, period=0.3, extra=fast)
    check("a bot the manager has seen running for less than 45 s (MGO1_HANG_GRACE at most): not yet", "restart" not in b6e.log(), b6e.log())
    # 6d. (v150-host beta 18) server.env's HANG_FAST_SECONDS (15 to 600) sets that delay, read at each round: the manager's
    # own MGO1_HANG_FAST_SECONDS (here 0: off) only when server.env has none (an old server's); a value out of range or not a
    # number said once, the default kept; a line from Windows (a carriage return, quotes) read as systemd reads it
    def fast_box(line, silent):
        bx = Box()
        bx.bot(1, ["R1"])
        aged(bx, "bot1", silent)
        bx.run(1, period=0.3, grace=0, server_extra=line)
        aged(bx, "bot1", silent)
        bx.run(5, period=0.3, grace=0, server_extra=line)
        return bx, bx.day_log("manager")
    warn = " W mgo1doll-manager: server.env: HANG_FAST_SECONDS must be 15 to 600 (seconds): 0"
    for line, silent, restarted, warned in (("HANG_FAST_SECONDS=60\n", 75, True, False), ("HANG_FAST_SECONDS=60\n", 50, False, False),
                                            ("HANG_FAST_SECONDS=15\n", 30, True, False), ("HANG_FAST_SECONDS=600\n", 170, False, False),
                                            ('HANG_FAST_SECONDS="60"\r\n', 75, True, False), ("HANG_FAST_SECONDS=14\n", 75, False, True),
                                            ("HANG_FAST_SECONDS=601\n", 75, False, True), ("HANG_FAST_SECONDS=sixty\n", 75, False, True),
                                            ("HANG_FAST_SECONDS=-30\n", 75, False, True), ("HANG_FAST_SECONDS=\n", 75, False, False)):
        bx, mlog = fast_box(line, silent)
        check("server.env %s, a bot silent %d s: %s%s" % (line.strip().replace("\r", "\\r"), silent, "restarted before the 180 s" if restarted else "left alone",
                                                         ", the value said as wrong once in each of the manager's two runs (not each round)" if warned else ""),
              (bx.log().count("restart mgo1host@1") == 1) == restarted and ("restart" in bx.log()) == restarted
              and mlog.count(warn) == (2 if warned else 0), bx.log() + mlog[-500:])
    bx = Box()
    bx.bot(1, ["R1"])
    aged(bx, "bot1", 50)
    bx.run(1, period=0.3, grace=0, server_extra="HANG_FAST_SECONDS=120\n", extra=fast)
    aged(bx, "bot1", 50)
    bx.run(5, period=0.3, grace=0, server_extra="HANG_FAST_SECONDS=120\n", extra=fast)
    check("server.env's HANG_FAST_SECONDS=120 before the manager's own 45: a bot silent 50 s left alone", "restart" not in bx.log(), bx.log())
    check("server.env.example tells HANG_FAST_SECONDS (15 to 600, 45 by default)",
          re.search(r"^#\s*HANG_FAST_SECONDS=45\b", open(os.path.join(HERE, "..", "host", "server.env.example")).read(), re.M) is not None)

    # 6e. (v150-host beta 18) a doll's DOLL_AIM kept over the manager's restarts of it: its settings file left as it was (the
    # launcher reads DOLL_AIM again at each start), no aim order of the manager's own
    b6f = Box()
    b6f.bot(1, ["R1"])
    b6f.write("etc/doll1.env", "DOLL_ACCOUNT_ID=DOLLACC1\nDOLL_ACCOUNT_PASSWORD=SECRET1\nDOLL_BOT=1\nDOLL_ENABLED=1\nDOLL_AIM=direct\n")
    b6f.write("var/doll1/status.json", json.dumps({"name": "PLAYER_DOLL1"}))
    b6f.set_units(b6f.units() | {"mgo1doll@1"})
    aged(b6f, "doll1", 400)
    before = open(os.path.join(b6f.etc, "doll1.env")).read()
    b6f.run(2, hang=60, grace=0)
    check("a hung doll with DOLL_AIM=direct restarted by the manager: its settings file kept whole (DOLL_AIM=direct), no aim order",
          "restart mgo1doll@1" in b6f.log() and open(os.path.join(b6f.etc, "doll1.env")).read() == before and "aim" not in b6f.command(1),
          (b6f.log(), b6f.command(1)))

    # 7. the accountless dolls: no account, DOLL_<N>; their names known before any status; none with DOLL_GATE=0
    b7 = Box()
    b7.bot(1, ["REAL1"])
    b7.doll(1, account=False)
    b7.doll(2, account=False)
    b7.doll(3)                                         # (one with an account beside them)
    out = b7.run(4)
    check("accountless dolls started like the others", b7.dolls_on() == [1, 2, 3], str(b7.dolls_on()))
    check("their names DOLL_<N> in the names file at once", {"DOLL_1", "DOLL_2"} <= set(b7.names()), str(b7.names()))
    b7.doll(1, account=False, name="PLAYER_DOLL1")     # (a status naming another player: not taken for them)
    b7.run(1)
    check("an accountless doll's name never taken from its status", "PLAYER_DOLL1" not in b7.names() and "DOLL_1" in b7.names(), str(b7.names()))
    out = b7.run(2, gate=False)
    check("DOLL_GATE=0: the accountless ones stopped, the other kept", b7.dolls_on() == [3] and "no account, and DOLL_GATE=0" in out,
          str(b7.dolls_on()) + out)

    # 8. a bot's DOLL_COUNT: accountless dolls added (the first free numbers, an account doll left alone),
    # the last ones removed (their units stopped); the agent's doll-count does it at once
    b8 = Box()
    b8.bot(1, ["REAL1"], count=3)
    b8.doll(2)                                         # an account doll of bot 1: not one of the count
    out = b8.run(1)
    check("DOLL_COUNT=3: three accountless dolls added", b8.env_files() == [1, 2, 3, 4] and "added for bot 1 as DOLL_4" in out,
          str(b8.env_files()) + out)
    e1 = open(os.path.join(b8.etc, "doll1.env")).read()
    check("their settings: the bot, no account, 0600", "DOLL_BOT=1" in e1 and "DOLL_ACCOUNT" not in e1 and
          oct(os.stat(os.path.join(b8.etc, "doll1.env")).st_mode & 0o777) == "0o600", e1)
    b8.run(4)
    b8.bot(1, ["REAL1"], count=1)
    out = b8.run(1)
    check("DOLL_COUNT=1: the last ones removed, the account doll kept", b8.env_files() == [1, 2], str(b8.env_files()) + out)
    check("their units stopped", "stop mgo1doll@4" in b8.log() and "stop mgo1doll@3" in b8.log(), b8.log())
    j = b8.agent("doll-count", "1", "2")
    check("the agent's doll-count: added at once", j.get("ok") and b8.env_files() == [1, 2, 3], str(j)[:300] + str(b8.env_files()))
    check("its DOLL_COUNT written", "DOLL_COUNT=2" in open(os.path.join(b8.etc, "bot1.env")).read())
    acc = [d for d in j.get("dolls", []) if d.get("accountless")]
    check("the agent's JSON: the accountless dolls, their names, the counts", j.get("accountless") is True and
          sorted(d["name"] for d in acc) == ["DOLL_1", "DOLL_3"] and j.get("bots", {}).get("1", {}).get("accountless") == 2
          and j["bots"]["1"].get("with_account") == 1 and j["bots"]["1"].get("doll_count") == 2, str(j)[:600])
    j = b8.agent("doll-remove", "3")
    check("a removed accountless doll: its bot's DOLL_COUNT one less", j.get("ok") and "DOLL_COUNT=1" in open(os.path.join(b8.etc, "bot1.env")).read()
          and b8.env_files() == [1, 2], str(b8.env_files()))
    j = b8.agent("doll-count", "1", "40")
    check("too many dolls refused", not j.get("ok") and j.get("code") == "dolls_full", str(j))
    j = b8.agent("doll-keep-seat", "off")
    check("doll-keep-seat in server.env (the manager's)", "DOLL_KEEP_SEAT=0" in open(os.path.join(b8.etc, "server.env")).read()
          and "DOLL_KEEP_SEAT" not in open(os.path.join(b8.etc, "bot1.env")).read(), open(os.path.join(b8.etc, "server.env")).read())
    # (v141 beta 3) the accountless dolls in TX-55's game list: host by default, doll-list writes server.env's DOLL_LIST
    check("the dolls' listing host by default", j.get("list") == "host", str(j.get("list")))
    j = b8.agent("doll-list", "own")
    check("doll-list own in server.env, said back", j.get("ok") and j.get("list") == "own" and
          "DOLL_LIST=own" in open(os.path.join(b8.etc, "server.env")).read(), str(j)[:300])
    j = b8.agent("doll-list", "all")
    check("doll-list refuses another word", not j.get("ok") and j.get("code") == "usage", str(j))
    j = b8.agent("doll-list", "off")
    check("doll-list off", j.get("ok") and j.get("list") == "off", str(j)[:200])
    # (v141 beta 7) SaveMGO's rankings and the dolls: auto by default, host-unranked writes server.env's HOST_UNRANKED
    check("the games with dolls unranked: auto by default", j.get("unranked") == "auto", str(j.get("unranked")))
    j = b8.agent("host-unranked", "off")
    senv = open(os.path.join(b8.etc, "server.env")).read()
    check("host-unranked off in server.env, said back", j.get("ok") and j.get("unranked") == "off" and "HOST_UNRANKED=off" in senv
          and "DOLL_LIST=off" in senv, str(j)[:300] + senv)
    j = b8.agent("host-unranked", "always")
    check("host-unranked always", j.get("ok") and j.get("unranked") == "always", str(j)[:200])
    j = b8.agent("host-unranked", "never")
    check("host-unranked refuses another word", not j.get("ok") and j.get("code") == "usage", str(j))
    j = b8.agent("host-unranked", "auto")
    check("host-unranked auto back", j.get("ok") and j.get("unranked") == "auto" and
          "HOST_UNRANKED=auto" in open(os.path.join(b8.etc, "server.env")).read(), str(j)[:200])

    # 9. identity: a doll that does not know who it is yet ("identity": "unknown") is not taken at its word;
    # two dolls with one name are said
    b9 = Box()
    b9.bot(1, ["REAL1"])
    b9.doll(1, name="PLAYER_DOLL7", identity="name")
    b9.run(1)
    b9.doll(1, name="PLAYER_DOLL1", identity="unknown")
    b9.run(1)
    check("a name with an unknown identity not taken (the last known kept)", "PLAYER_DOLL7" in b9.names() and "PLAYER_DOLL1" not in b9.names(), str(b9.names()))
    b9.doll(2, name="PLAYER_DOLL7", identity="world")
    out = b9.run(1)
    check("two dolls with one name: said in the log", "dolls 1, 2 report the same name PLAYER_DOLL7" in out, out)

    # 10. a game with no status file at all: restarted once its unit has run HANG seconds without one
    b10 = Box()
    b10.bot(1, ["REAL1"])
    b10.doll(1, account=False)
    b10.set_units(b10.units() | {"mgo1doll@1"})
    out = b10.run(6, hang=0.5, grace=0, period=0.2)
    check("a doll that never wrote its status restarted", "restart mgo1doll@1" in b10.log() and "no status file" in out, b10.log() + out)
    os.remove(os.path.join(b10.var, "bot1", "run", "status.json"))
    b10.set_units({"mgo1host@1"})
    open(b10.db + ".log", "w").close()
    b10.run(1, hang=0.5, grace=0)
    check("a bot's status file just gone (its new game): not restarted at once", "restart mgo1host@1" not in b10.log(), b10.log())

    # 11. systemctl: a start that fails is said and another doll tried; one that does not answer in time
    b11 = Box()
    b11.bot(1, ["REAL1"])
    b11.doll(1)
    b11.doll(2)
    out = b11.run(2, extra={"FAIL_UNITS": "mgo1doll@1"})
    check("a failed start said", "systemctl start mgo1doll@1 failed (code 1)" in out, out)
    check("the next doll tried meanwhile", b11.dolls_on() == [2], str(b11.dolls_on()) + out)
    t0 = time.time()
    out = b11.run(1, extra={"SLOW": "3", "MGO1_SYSTEMCTL_TIMEOUT": "1"})
    check("a systemctl with no answer: said, the round goes on", "no answer in 1 s" in out and time.time() - t0 < 30, out)

    # 12. a doll starting (activating) holds its seat: 8 seats, 6 real, one kept: 1 doll, no second one
    b12 = Box()
    b12.bot(1, ["R1", "R2", "R3", "R4", "R5", "R6"])
    b12.doll(1, account=False)
    b12.doll(2, account=False)
    b12.force({"mgo1doll@1": "activating"})
    b12.run(3)
    check("a doll activating holds its seat", "start mgo1doll@2" not in b12.log() and "start mgo1doll@1" not in b12.log(), b12.log())

    # 13. a doll running and never in its bot's game: started again after DOLL_JOIN_TIMEOUT
    b13 = Box()
    b13.bot(1, ["REAL1"])
    b13.doll(1, account=False)
    b13.set_units(b13.units() | {"mgo1doll@1"})
    out = b13.run(4, join_wait=0.3, period=0.2)
    check("a doll never in its game started again", "restart mgo1doll@1" in b13.log() and "not in bot 1's game" in out, b13.log() + out)
    b13.bot(1, ["REAL1", "DOLL_1"])
    b13.set_units({"mgo1host@1", "mgo1doll@1"})
    open(b13.db + ".log", "w").close()
    b13.run(4, join_wait=0.3, period=0.2)
    check("one in its game left alone", "restart mgo1doll@1" not in b13.log(), b13.log())

    # 13b. (v141 beta 6) the live false join timeout: a doll whose DOLL_NAME is quoted in its settings file (systemd drops the
    # quotes: its name in the game is the bare one) found by its name; a doll whose name the host shows otherwise found by its
    # number (the host's kind and doll fields); a doll really absent: the diagnostic line before its restart
    b13b = Box()
    b13b.bot(1, [{"name": "REAL1", "kind": "real", "doll": 0}, {"name": "SaveMGO_DOLL_1"},
                 {"name": "Renamed2", "kind": "doll", "doll": 2}])
    b13b.write("etc/doll1.env", 'DOLL_NAME="SaveMGO_DOLL_1"\nDOLL_BOT=1\nDOLL_ENABLED=1\n')
    b13b.write("etc/doll2.env", "DOLL_NAME=SaveMGO_DOLL_2\nDOLL_BOT=1\nDOLL_ENABLED=1\n")
    b13b.write("etc/doll3.env", "DOLL_NAME=SaveMGO_DOLL_3\nDOLL_BOT=1\nDOLL_ENABLED=1\n")
    b13b.write("var/doll3/status.json", json.dumps({"name": "SaveMGO_DOLL_3", "identity": "uid", "act": "lobby", "real_players": 0}))
    b13b.set_units(b13b.units() | {"mgo1doll@1", "mgo1doll@2", "mgo1doll@3"})
    out = b13b.run(4, join_wait=0.3, period=0.2)
    check("a quoted DOLL_NAME: found by its bare name, not restarted", "restart mgo1doll@1" not in b13b.log(), b13b.log() + out)
    check("a doll the host shows under another name: found by its number", "restart mgo1doll@2" not in b13b.log(), b13b.log() + out)
    check("a doll really absent: restarted after the diagnostic line", "restart mgo1doll@3" in b13b.log() and "doll 3: join timeout: expected SAVEMGO_DOLL_3" in out
          and "REAL1 (real)" in out and "identity uid" in out, out[-1500:])
    check("the names file: the bare name of a quoted DOLL_NAME", "SAVEMGO_DOLL_1" in b13b.names(), b13b.names())
    check("no account, no password in the manager's lines", "SECRET" not in out and "DOLLACC" not in out, out[-800:])

    # 14. a doll service running without its settings file: stopped
    b14 = Box()
    b14.bot(1, ["REAL1"])
    b14.set_units(b14.units() | {"mgo1doll@9"})
    out = b14.run(1)
    check("a doll without its settings file stopped", "stop mgo1doll@9" in b14.log() and "no settings file" in out, b14.log() + out)

    # 15. (v141 beta 5) the accountless dolls' names: doll-count's for the doll it adds, doll-set's "name" (checked, a running
    # doll told to leave, "" its own again), the names file with them
    b15 = Box()
    b15.bot(1, ["REAL1"], count=1)
    b15.doll(1, account=False)
    b15.doll(2, account=True, name="ACCPLAYER", identity="uid")
    j = b15.agent("doll-count", "1", "2", stdin=json.dumps({"name": "Big Boss"}))
    env3 = open(os.path.join(b15.etc, "doll3.env")).read() if os.path.exists(os.path.join(b15.etc, "doll3.env")) else ""
    check("doll-count's name: the doll it adds has it", j.get("ok") and "DOLL_NAME=Big Boss" in env3, str(j)[:300] + env3)
    shown = {d["doll"]: (d["name"], d.get("name_set")) for d in j.get("dolls", [])}
    check("the agent's JSON: its name, set", shown.get(3) == ("Big Boss", True) and shown.get(1) == ("DOLL_1", False), str(shown))
    b15.run(2)
    check("the names file: the name given, the others'", {"BIG BOSS", "DOLL_1", "ACCPLAYER"} <= set(open(os.path.join(b15.var, "dolls.names")).read().splitlines()),
          open(os.path.join(b15.var, "dolls.names")).read())
    b15.set_units(b15.units() | {"mgo1doll@1"})
    j = b15.agent("doll-set", "1", stdin=json.dumps({"name": "Snake Eater!"}))
    check("doll-set's name: written, the running doll told to leave", j.get("ok") and "DOLL_NAME=Snake Eater!" in open(os.path.join(b15.etc, "doll1.env")).read()
          and "leave" in b15.command(1) and j.get("doll", {}).get("name") == "Snake Eater!", str(j)[:300] + b15.command(1))
    check("the rename noted for the GUI's report", "doll-rename" in "".join(open(os.path.join(dp, f)).read() for dp, _, fs in os.walk(b15.var) for f in fs if f.endswith(".admin.jsonl")))
    j = b15.agent("doll-set", "1", stdin=json.dumps({"name": "  Lead "}))
    check("a name's spaces around it dropped", j.get("ok") and j.get("doll", {}).get("name") == "Lead", str(j)[:200])
    for bad, code in (("ab", "doll_name"), ("Two  Spaces", "doll_name"), ("Bad#Name", "doll_name"), ('Quote"d', "doll_name"),
                      ("Sixteen chars xx", "doll_name"), ("doll_7", "doll_name_default"), ("big boss", "doll_name_taken"),
                      ("accplayer", "doll_name_taken")):
        j = b15.agent("doll-set", "1", stdin=json.dumps({"name": bad}))
        check("a name refused: %r (%s)" % (bad, code), not j.get("ok") and j.get("code") == code, str(j)[:200])
    j = b15.agent("doll-set", "2", stdin=json.dumps({"name": "Other"}))
    check("a doll with an account: no name of its own", not j.get("ok") and j.get("code") == "doll_name_account", str(j)[:200])
    open(os.path.join(b15.var, "doll1", "command"), "w").close()
    b15.set_units(b15.units() - {"mgo1doll@1"})
    j = b15.agent("doll-set", "1", stdin=json.dumps({"name": ""}))
    check("an empty name: DOLL_<n> again, a doll not running not told to leave", j.get("ok") and "DOLL_NAME" not in open(os.path.join(b15.etc, "doll1.env")).read()
          and j.get("doll", {}).get("name") == "DOLL_1" and "leave" not in b15.command(1), str(j)[:300])
    j = b15.agent("doll-set", "1", stdin=json.dumps({"name": "DOLL_1"}))
    check("its own DOLL_<n> taken as its own name", j.get("ok") and j.get("doll", {}).get("name") == "DOLL_1", str(j)[:200])

    # 16. (v141 beta 6) the ladder 1 TRAINING .. 10 PRO and the live level's bounds: doll-set's "difficulty", "adapt_min",
    # "adapt_max" (refused out of range, swapped the wrong way round, sent live), doll-order's and doll-all's, the summaries
    b16 = Box()
    b16.bot(1, ["REAL1"])
    b16.doll(1, account=False)
    b16.doll(2, account=False)
    b16.doll(3, account=False, bot=2)

    def env_of(n):
        p = os.path.join(b16.etc, "doll%d.env" % n)
        return open(p).read() if os.path.exists(p) else ""

    def clear(n):
        p = os.path.join(b16.var, "doll%d" % n, "command")
        if os.path.exists(p):
            open(p, "w").close()

    def levels(j):
        d = j.get("doll", {})
        return d.get("difficulty"), d.get("tier"), d.get("adapt_min"), d.get("adapt_max")

    j = b16.agent("dolls")
    first = {d["doll"]: d for d in j.get("dolls", [])}.get(1, {})
    check("the summaries: a doll without the keys at 3 NORMAL, its bounds 1 and 10",
          (first.get("difficulty"), first.get("tier"), first.get("adapt_min"), first.get("adapt_max")) == (3, "NORMAL", 1, 10), str(first)[:300])
    for v, tier in ((1, "TRAINING"), (6, "ELITE"), (10, "PRO"), ("8", "MASTER")):
        clear(1)
        j = b16.agent("doll-set", "1", stdin=json.dumps({"difficulty": v}))
        check("doll-set difficulty %r accepted, its tier %s, sent live" % (v, tier), j.get("ok") and levels(j)[:2] == (int(v), tier)
              and "DOLL_DIFFICULTY=%s\n" % v in env_of(1) and "difficulty %s\n" % v in b16.command(1), str(j)[:300] + env_of(1) + b16.command(1))
    before = env_of(1)
    for bad in (0, 11, -1, "11", "0", "x", "", True, 5.5, None):
        j = b16.agent("doll-set", "1", stdin=json.dumps({"difficulty": bad}))
        check("doll-set difficulty %r refused, nothing written" % (bad,), not j.get("ok") and j.get("code") == "usage" and env_of(1) == before, str(j)[:200])
    clear(1)
    j = b16.agent("doll-set", "1", stdin=json.dumps({"adapt_min": 3, "adapt_max": 8}))
    check("doll-set adapt_min 3, adapt_max 8: written, in the summary, sent live", j.get("ok") and levels(j)[2:] == (3, 8)
          and "DOLL_ADAPT_MIN=3\n" in env_of(1) and "DOLL_ADAPT_MAX=8\n" in env_of(1)
          and "adapt_min 3\n" in b16.command(1) and "adapt_max 8\n" in b16.command(1), str(j)[:300] + env_of(1) + b16.command(1))
    clear(1)
    j = b16.agent("doll-set", "1", stdin=json.dumps({"adapt_min": "9", "adapt_max": 2}))
    check("doll-set adapt_min 9, adapt_max 2: swapped", j.get("ok") and levels(j)[2:] == (2, 9) and "DOLL_ADAPT_MIN=2\n" in env_of(1)
          and "adapt_min 2\n" in b16.command(1) and "adapt_max 9\n" in b16.command(1), str(j)[:300] + b16.command(1))
    j = b16.agent("doll-set", "1", stdin=json.dumps({"adapt_min": 10}))
    check("doll-set adapt_min 10 alone: the max kept, swapped (9 to 10)", j.get("ok") and levels(j)[2:] == (9, 10), str(j)[:300])
    before = env_of(1)
    for bad in ({"adapt_min": 0}, {"adapt_max": 11}, {"adapt_min": "ten"}, {"adapt_max": None}, {"adapt_min": 2, "adapt_max": 12}):
        j = b16.agent("doll-set", "1", stdin=json.dumps(bad))
        check("doll-set %s refused, nothing written" % json.dumps(bad), not j.get("ok") and j.get("code") == "usage" and env_of(1) == before, str(j)[:200])
    clear(1)
    j = b16.agent("doll-set", "1", stdin=json.dumps({"adapt": False}))
    check("doll-set adapt false: written and sent live", j.get("ok") and "DOLL_ADAPT=0\n" in env_of(1) and "adapt off\n" in b16.command(1)
          and "adapt_min" not in b16.command(1), str(j)[:200] + b16.command(1))
    clear(2)
    j = b16.agent("doll-order", "2", "difficulty", "7")
    check("doll-order difficulty 7: VETERAN, kept, sent", j.get("ok") and levels(j)[:2] == (7, "VETERAN") and "DOLL_DIFFICULTY=7\n" in env_of(2)
          and "difficulty 7\n" in b16.command(2), str(j)[:300])
    for bad in ("0", "11", "x", "07"):
        j = b16.agent("doll-order", "2", "difficulty", bad)
        check("doll-order difficulty %s refused" % bad, not j.get("ok") and j.get("code") == "usage", str(j)[:200])
    clear(2)
    j = b16.agent("doll-order", "2", "adapt_max", "4")
    check("doll-order adapt_max 4: 1 to 4, both sent", j.get("ok") and levels(j)[2:] == (1, 4) and "adapt_min 1\nadapt_max 4\n" in b16.command(2)
          and "DOLL_ADAPT_MAX=4\n" in env_of(2), str(j)[:300] + b16.command(2))
    clear(2)
    j = b16.agent("doll-order", "2", "adapt_min", "6")
    check("doll-order adapt_min 6 over the max 4: swapped, 4 to 6", j.get("ok") and levels(j)[2:] == (4, 6) and "adapt_min 4\nadapt_max 6\n" in b16.command(2)
          and "DOLL_ADAPT_MIN=4\n" in env_of(2) and "DOLL_ADAPT_MAX=6\n" in env_of(2), str(j)[:300] + b16.command(2))
    for k, bad in (("adapt_min", "0"), ("adapt_max", "11"), ("adapt_min", "")):
        j = b16.agent("doll-order", "2", k, bad)
        check("doll-order %s %r refused" % (k, bad), not j.get("ok") and j.get("code") == "usage", str(j)[:200])
    j = b16.agent("doll-all", "1", "difficulty", "9")
    tiers = {d["doll"]: (d.get("difficulty"), d.get("tier")) for d in j.get("dolls", [])}
    check("doll-all 1 difficulty 9: bot 1's dolls at 9 EXTREME, bot 2's left", j.get("ok") and tiers.get(1) == (9, "EXTREME")
          and tiers.get(2) == (9, "EXTREME") and tiers.get(3) == (3, "NORMAL") and "difficulty 9\n" in b16.command(2), str(tiers))
    for bad in ("0", "11"):
        j = b16.agent("doll-all", "1", "difficulty", bad)
        check("doll-all difficulty %s refused" % bad, not j.get("ok") and j.get("code") == "usage", str(j)[:200])
    j = b16.agent("dolls")
    sums = {d["doll"]: (d.get("difficulty"), d.get("tier"), d.get("adapt_min"), d.get("adapt_max")) for d in j.get("dolls", [])}
    check("dolls: each doll's difficulty, tier and bounds", sums.get(1) == (9, "EXTREME", 9, 10) and sums.get(2) == (9, "EXTREME", 4, 6)
          and sums.get(3) == (3, "NORMAL", 1, 10), str(sums))
    b16.write("etc/doll3.env", "DOLL_BOT=2\nDOLL_DIFFICULTY=12\nDOLL_ADAPT_MIN=8\nDOLL_ADAPT_MAX=3\nDOLL_ENABLED=1\n")
    j = b16.agent("dolls")
    sums = {d["doll"]: (d.get("difficulty"), d.get("tier"), d.get("adapt_min"), d.get("adapt_max")) for d in j.get("dolls", [])}
    check("a file edited by hand: a difficulty out of range as 3 (as mgo1doll.sh), the bounds the lower first", sums.get(3) == (3, "NORMAL", 3, 8), str(sums))

    # 17. (v141 beta 9) the pace guard: 3 dolls in bot 1's game with a real player; their "pace:" minutes in their status files
    def pace(box, n, minute, rate, bot=False):
        path = "var/bot%d/run/status.json" % n if bot else "var/doll%d/status.json" % n
        st = json.loads(open(os.path.join(box.dir, path)).read()) if os.path.exists(os.path.join(box.dir, path)) else {}
        st["pace"] = {"n": minute, "rate": rate, "due": 59.94, "cpu": 50}
        box.write(path, json.dumps(st))
    fast = {"DOLL_PACE_SETTLE": "0", "DOLL_PACE_HOLD": "0"}
    b17 = Box()
    for n in (1, 2, 3):
        b17.doll(n, name="PACE DOLL %d" % n)
    b17.bot(1, ["REAL1", "PACE DOLL 1", "PACE DOLL 2", "PACE DOLL 3"])
    b17.run(3)
    check("pace: the three dolls in the game", b17.dolls_on() == [1, 2, 3], str(b17.dolls_on()))
    for n in (1, 2, 3):
        pace(b17, n, 1, 54.2 if n != 3 else 59.9)
    pace(b17, 1, 1, 59.9, bot=True)
    out = b17.run(1, extra=fast)
    check("pace: one slowed minute: nothing yet", "pace guard" not in out and not any("leave" in b17.command(n) for n in (1, 2, 3)), out)
    for n in (1, 2, 3):
        pace(b17, n, 2, 53.8 if n != 3 else 59.9)
    pace(b17, 1, 2, 59.9, bot=True)
    out = b17.run(1, extra=fast)
    check("pace: two of three dolls slowed 2 minutes: one doll less (2 at most), the last one told to leave",
          "below its pace 2 minutes in a row" in out and "2 at most" in out and "leave" in b17.command(3) and "leave" not in b17.command(2), out)
    check("pace: the guard's line names the dolls by their number only", "doll1 53.8" in out and "PACE DOLL" not in out, out)
    b17.run(2, leave_wait=0, extra=fast)
    check("pace: the doll gone", b17.dolls_on() == [1, 2], str(b17.dolls_on()))
    b17.bot(1, ["REAL1", "PACE DOLL 1", "PACE DOLL 2"])
    for n in (1, 2):
        pace(b17, n, 3, 59.9)
    pace(b17, 1, 3, 59.9, bot=True)
    out = b17.run(2, extra=fast)
    check("pace: at its pace again (hold 0 in the test): one doll more, then no cap, the doll back",
          "one doll more, 3 at most" in out and "no cap any more" in out and b17.dolls_on() == [1, 2, 3], out + str(b17.dolls_on()))
    # the bot itself slowed with one doll: down to none; DOLL_PACE_GUARD=0 and no real player: nothing
    b18 = Box()
    b18.doll(1, name="PACE DOLL 1")
    b18.bot(1, ["REAL1", "PACE DOLL 1"])
    b18.run(2)
    for m in (1, 2):
        pace(b18, 1, m, 50.5, bot=True)
        pace(b18, 1, m, 59.9)
        out = b18.run(1, extra=fast, server_extra="DOLL_PACE_GUARD=0\n")
    check("pace: DOLL_PACE_GUARD=0: off", "pace guard" not in out and "leave" not in b18.command(1), out)
    for m in (3, 4):
        pace(b18, 1, m, 50.5, bot=True)
        pace(b18, 1, m, 59.9)
        out = b18.run(1, extra=fast)
    check("pace: the bot slowed 2 minutes, one doll: none at most, it leaves", "0 at most" in out and "leave" in b18.command(1), out)
    # (v141 beta 12) the bot's game a minute at a third of its pace (a VPS's machine freezing its processors): one doll less at once
    b18c = Box()
    for n in (1, 2):
        b18c.doll(n, name="PACE DOLL %d" % n)
    b18c.bot(1, ["REAL1", "PACE DOLL 1", "PACE DOLL 2"])
    b18c.run(2)
    pace(b18c, 1, 1, 58.9, bot=True)
    for n in (1, 2):
        pace(b18c, n, 1, 59.0)
    out = b18c.run(1, extra=fast)
    check("pace: a minute at its pace: nothing", "pace guard" not in out, out)
    pace(b18c, 1, 2, 19.9, bot=True)
    for n in (1, 2):
        pace(b18c, n, 2, 18.5)
    out = b18c.run(1, extra=fast)
    check("pace: the bot's game a minute at a third of its pace: one doll less at once (1 at most)",
          "a minute far below its pace" in out and "1 at most" in out and "leave" in b18c.command(2), out)
    b19 = Box()
    b19.doll(1, name="PACE DOLL 1")
    b19.bot(1, ["PACE DOLL 1"])
    b19.run(2, keep_seat=False, listed="off")
    for m in (1, 2, 3):
        pace(b19, 1, m, 50.5)
        out = b19.run(1, extra=fast, keep_seat=False, listed="off")
    check("pace: no real player in the game: nothing", "pace guard" not in out and "leave" not in b19.command(1), out)

    # 20. (v141 beta 9) the join timeout in the doll's own game's time; the steal of the VPS's machine (the manager's functions)
    import contextlib, importlib.machinery, io, types
    b20 = Box()
    saved = dict(os.environ)
    os.environ.update({"MGO1_VAR": b20.var, "MGO1_ETC": b20.etc, "DOLL_JOIN_TIMEOUT": "240", "MGO1_LOG_DIR": os.path.join(b20.dir, "log"),
                       "MGO1LOG": os.path.join(b20.dir, "none"), "MGO1_PROC_STAT": os.path.join(b20.dir, "stat")})
    try:
        loader = importlib.machinery.SourceFileLoader("mgo1doll_manager_t", MANAGER)
        mgr = types.ModuleType(loader.name)
        loader.exec_module(mgr)
        def status(n, frame, rate=None):
            st = {"frame": frame}
            if rate:
                st["pace"] = {"n": 1, "rate": rate, "due": 59.94, "cpu": 30}
            b20.write("var/doll%d/status.json" % n, json.dumps(st))
        pf = {1: {"t0": 0.0, "at": 1000, "said": False}}
        status(1, 4000, 14.2)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            late = mgr.join_late(1, 300.0, 0.0, pf)
        check("join: 300 s of real time, 50 s of its own game: not late, said once", not late and "its own game 50 s" in out.getvalue()
              and "14.2 VBlanks a second" in out.getvalue(), out.getvalue())
        with contextlib.redirect_stdout(io.StringIO()) as o2:
            mgr.join_late(1, 310.0, 0.0, pf)
        check("join: said once only", o2.getvalue() == "", o2.getvalue())
        status(1, 1000 + 14400)
        check("join: its own game past 240 s: late", mgr.join_late(1, 400.0, 0.0, pf))
        status(1, 600)
        check("join: started again since (its frames from 0): 10 s of its own game, not late", not mgr.join_late(1, 400.0, 0.0, pf))
        check("join: past 20 minutes of real time: late", mgr.join_late(1, 1201.0, 0.0, pf))
        os.remove(os.path.join(b20.var, "doll1", "status.json"))
        check("join: no status file: late (as before)", mgr.join_late(1, 300.0, 0.0, pf))
        # the steal: /proc/stat's first line, cumulative; 30 % of the time stolen two minutes in a row: one warning, then its end
        def stat(total, stolen):
            b20.write("stat", "cpu  %d 0 0 0 0 0 0 %d 0 0\n" % (total - stolen, stolen))
        st = mgr.Steal()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            stat(1000, 0); st.look(0.0)
            stat(2000, 300); st.look(61.0)
            first = out.getvalue()
            stat(3000, 600); st.look(122.0)
            stat(4000, 610); st.look(183.0)
        check("steal: one minute at 30 %: nothing yet", "steal" not in first, first)
        check("steal: two minutes at 30 %: the warning; 1 % then: its end", "30 % of their time taken by the machine under the VPS" in out.getvalue()
              and "takes 1 % of their time again" in out.getvalue(), out.getvalue())
    finally:
        os.environ.clear()
        os.environ.update(saved)

    # 21. the bot's virtual dolls hold their seats: 8 seats, 1 real player, 3 virtual dolls set (2 in the game), one seat kept:
    # 8 - 1 - 3 - 1 = 3 full dolls of 5
    b21 = Box()
    b21.bot(1, ["REAL1", {"name": "VDOLL1", "kind": "doll", "vdoll": 1}, {"name": "VDOLL2", "kind": "doll", "vdoll": 2},
                {"name": "REAL1", "kind": "real"}][1:], vdolls=3)
    for n in (1, 2, 3, 4, 5):
        b21.doll(n)
    out = b21.run(8)
    check("virtual dolls: their seats kept from the full dolls (3 of 5 started)", b21.dolls_on() == [1, 2, 3] and "for its virtual dolls" in out,
          str(b21.dolls_on()) + out)
    b21.bot(1, [{"name": "REAL1", "kind": "real"}])
    b21.run(4)
    check("no virtual doll set: every free seat for the full dolls (5 of 5)", b21.dolls_on() == [1, 2, 3, 4, 5], str(b21.dolls_on()))

    # 22. the agent's virtual dolls: vdoll-set into bot1.env and bot1.vdolls, its checks, vdolls
    b22 = Box()
    b22.bot(1, [], most=9)
    j = b22.agent("vdoll-set", "1", stdin='{"count": 3, "teams": "red,blue", "names": ["ALPHA", "Bravo Two"]}')
    env1 = open(os.path.join(b22.etc, "bot1.env")).read()
    names1 = open(os.path.join(b22.etc, "bot1.vdolls")).read().split("\n")
    check("vdoll-set: VDOLL_COUNT, VDOLL_TEAMS and the names file", j.get("ok") and "VDOLL_COUNT=3" in env1 and "VDOLL_TEAMS=red,blue" in env1
          and names1[:2] == ["ALPHA", "Bravo Two"] and j.get("takes_effect") == "next game", str(j) + env1)
    j = b22.agent("vdoll-set", "1", stdin='{"count": 8}')
    check("vdoll-set: 7 at most in a game of 9 (the host, a seat for a real player)", not j.get("ok") and j.get("code") == "vdoll_count", str(j))
    j = b22.agent("vdoll-set", "1", stdin='{"teams": "green"}')
    check("vdoll-set: a team is red, blue or auto", not j.get("ok") and j.get("code") == "vdoll_teams", str(j))
    j = b22.agent("vdolls")
    check("vdolls: each bot's count, teams, names, most", j.get("ok") and j["bots"][0]["count"] == 3 and j["bots"][0]["most"] == 7
          and j["bots"][0]["names"] == ["ALPHA", "Bravo Two"], str(j))

    # 23. the virtual dolls' processor in the capacity: 2 cores (1.5 kept to the games), 2 bots (0.7): 0.8 left, 2 dolls of 0.4;
    # bot 1's 12 virtual dolls (0.03 each: 0.36) leave 0.44: 1 doll
    b23 = Box()
    b23.bot(1, [{"name": "REAL1", "kind": "real"}], most=16)
    b23.bot(2, [{"name": "REAL2", "kind": "real"}])
    for n in (1, 2, 3):
        b23.doll(n, bot=2)
    b23.run(6, cores=2)
    check("no virtual doll: 2 full dolls on 2 cores with 2 bots", len(b23.dolls_on()) == 2, str(b23.dolls_on()))
    b23c = Box()
    b23c.bot(1, [{"name": "REAL1", "kind": "real"}], most=16, vdolls=12)
    b23c.bot(2, [{"name": "REAL2", "kind": "real"}])
    for n in (1, 2, 3):
        b23c.doll(n, bot=2)
    out = b23c.run(6, cores=2)
    check("bot 1's 12 virtual dolls counted: 1 full doll", len(b23c.dolls_on()) == 1 and "and their 12 virtual dolls" in out,
          str(b23c.dolls_on()) + out)

    # 24. (v150-host beta 16) why a bot's playable dolls do not start: a line in the manager's daily log and the bot's, once a
    # reason (W: a setting the owner must change), and mgo1host-agent's `dolls` with the same sums for each bot (waiting_code,
    # waiting_why, seats, real, vdolls, kept, free_for_dolls, dolls_set, dolls_in, pace_cap, pace_wait_s): the manager's own
    # (its /var/lib/mgo1host/dolls.sums.json) and the agent's, from the same folders, compared
    VD = [{"name": "VDOLL%d" % k, "host": 0, "kind": "doll", "doll": 0, "vdoll": k} for k in (1, 2, 3)]
    REAL = [{"name": "REAL%d" % k, "host": 0, "kind": "real", "doll": 0, "vdoll": 0} for k in range(1, 9)]

    def same(box, what, code, bot="2", extra=None, **want):
        """The manager's sums for the bot and the agent's: the same, the code and the numbers given."""
        m = (box.sums().get("bots") or {}).get(bot, {})
        j = box.agent("dolls", extra=extra)
        a = (j.get("bots") or {}).get(bot, {})
        keys = ("waiting_code", "waiting_why", "seats", "real", "vdolls", "kept", "free_for_dolls", "dolls_set", "dolls_in", "pace_cap")
        ok = bool(m) and all(a.get(k) == m.get(k) for k in keys) and abs((a.get("pace_wait_s") or 0) - (m.get("pace_wait_s") or 0)) <= 5 \
            and j.get("budget") == box.sums().get("budget")
        check("%s: the manager's sums and the agent's the same, %s" % (what, code or "no wait"), ok and m.get("waiting_code") == code
              and all(m.get(k) == v for k, v in want.items()), "manager %s budget %s / agent %s budget %s" % (m, box.sums().get("budget"), a, j.get("budget")))
        return m, j

    def said(box, out, text, level, bot="bot2"):
        line = " %s mgo1doll-manager: %s" % (level, text)
        return out.count("mgo1doll-manager: " + text) == 1 and line in box.day_log("manager") and line in box.day_log(bot)

    # the owner's server of 10 November with playable dolls again: bot 2 alone, a game of 9, 3 virtual dolls in it,
    # DOLL_KEEP_SEAT=0 (the dolls listed, DOLL_LIST host: a seat kept all the same), 4 cores a fifth taken by the VPS's machine
    b24 = Box()
    b24.bot(2, VD, most=9, vdolls=3)
    b24.write("stat20", "cpu  4000 0 2000 2000 0 0 0 2000 0 0\n")
    own = {"MGO1_PROC_STAT": os.path.join(b24.dir, "stat20")}
    for n in range(1, 7):
        b24.doll(n, bot=2, account=False)
    out = b24.run(8, cores=4, keep_seat=False, extra=own)
    why = ("bot 2: 6 playable dolls set, only 4 can start: its game has 8 seats (max_players 9), 3 for its virtual dolls, 1 kept for a real player; "
           "only 4 of them can ever play whatever the players (more max_players or fewer virtual dolls)")
    check("a game of 9 with 3 virtual dolls, DOLL_KEEP_SEAT=0 and the dolls listed: 4 playable dolls of 6 started, 7 joiners of 8 seats",
          b24.dolls_on() == [1, 2, 3, 4], str(b24.dolls_on()) + out)
    check("the two that never fit: said once in 8 rounds, a warning, in the manager's log and the bot's", said(b24, out, why, "W"), out)
    same(b24, "the owner's game of 9", "seats", extra=dict(own, MGO1_CORES="4"), seats=8, real=0, vdolls=3, kept=1, free_for_dolls=4, dolls_set=6,
         dolls_in=0, pace_cap=None, waiting_why=why)
    check("the server's budget there: 4 dolls besides its bot and its 3 virtual dolls", b24.sums().get("budget") == 4
          and "the server can run 4 dolls at once besides its 1 bots and their 3 virtual dolls" in out, out)
    # with the dolls kept off the lobby's list (DOLL_LIST=off) no seat is kept: the virtual dolls' game keeps none either
    # (mgo1host.sh gives MGO1_VDOLL_KEEP_SEAT=0 only then, tests/test_launchers.py): 5 playable dolls, the game full
    b24b = Box()
    b24b.bot(2, VD, most=9, vdolls=3)
    for n in range(1, 7):
        b24b.doll(n, bot=2, account=False)
    out = b24b.run(8, keep_seat=False, listed="off")
    check("DOLL_KEEP_SEAT=0 and DOLL_LIST=off: no seat kept, 5 playable dolls of 6 started (8 joiners of 8 seats)", b24b.dolls_on() == [1, 2, 3, 4, 5]
          and "bot 2: 6 playable dolls set, only 5 can start: its game has 8 seats (max_players 9), 3 for its virtual dolls; only 5 of them" in out, out)
    same(b24b, "no seat kept", "seats", seats=8, vdolls=3, kept=0, free_for_dolls=5, dolls_set=6)
    # real players take the seats: said (not a warning: no setting to change), the dolls in the game counted
    b24c = Box()
    b24c.bot(2, VD + REAL[:3] + [{"name": "DOLL_1", "host": 0, "kind": "doll", "doll": 1, "vdoll": 0}], most=9, vdolls=3)
    for n in (1, 2, 3):
        b24c.doll(n, bot=2, account=False)
    out = b24c.run(4)
    why = "bot 2: 3 playable dolls set, only 1 can start: its game has 8 seats (max_players 9), 3 for its virtual dolls, 1 kept for a real player, 3 real players"
    check("3 real players in: 1 doll of 3, said once, not a warning", b24c.dolls_on() == [1] and said(b24c, out, why, "I"), str(b24c.dolls_on()) + out)
    same(b24c, "real players in the seats", "seats", real=3, free_for_dolls=1, dolls_set=3, dolls_in=1, waiting_why=why)
    b24c.bot(2, VD + REAL[:4], most=9, vdolls=3)
    out = b24c.run(2)
    check("a fourth real player: none can start, said", "bot 2: 3 playable dolls set, none can start: its game has 8 seats (max_players 9), 3 for its virtual dolls, "
          "1 kept for a real player, 4 real players" in out and " I mgo1doll-manager: bot 2: 3 playable dolls set, none can start" in b24c.day_log("bot2"), out)
    # a game too small for them (max_players 5: 4 seats, 3 virtual dolls, 1 kept): a warning; the agent's doll-count took them
    b24d = Box()
    b24d.bot(2, VD, most=5, vdolls=3)
    j = b24d.agent("doll-count", "2", "2")
    out = b24d.run(3)
    why = ("bot 2: 2 playable dolls set, none can start: its game has 4 seats (max_players 5), 3 for its virtual dolls, 1 kept for a real player; "
           "none of them can ever play whatever the players (more max_players or fewer virtual dolls)")
    check("max_players 5 with 3 virtual dolls: doll-count 2 accepted, none ever starts, a warning says why", j.get("ok") and b24d.dolls_on() == []
          and said(b24d, out, why, "W"), str(j)[:200] + out)
    check("doll-count's answer tells it at once (the bot's waiting_code seats, none free)", j.get("bots", {}).get("2", {}).get("waiting_code") == "seats"
          and j["bots"]["2"].get("free_for_dolls") == 0 and j["bots"]["2"].get("dolls_set") == 2, str(j.get("bots"))[:400])
    same(b24d, "a game too small", "seats", seats=4, vdolls=3, kept=1, free_for_dolls=0, dolls_set=2)
    # the dolls' bot is not there, or is stopped (its last status says hosting): a warning; bot 2 runs
    b24e = Box()
    b24e.bot(2, VD, most=9, vdolls=3)
    for n in (1, 2):
        b24e.doll(n, bot=1, account=False)
    out = b24e.run(3)
    why = "bot 1's playable dolls (2) wait: there is no bot 1 on this server (their DOLL_BOT: remove them, or give them another bot)"
    check("dolls of a bot that is not there (DOLL_BOT=1, bot 2 alone): none started, a warning says why", b24e.dolls_on() == []
          and out.count(why) == 1 and " W mgo1doll-manager: " + why in b24e.day_log("manager"), out)
    m, j = same(b24e, "no such bot", "bot_stopped", bot="1", dolls_set=2, waiting_why=why)
    check("the agent lists that bot for its dolls (exists false), bot 2 with its seats and no wait", j.get("bots", {}).get("1", {}).get("exists") is False
          and j["bots"]["1"].get("accountless") == 2 and j["bots"]["2"].get("exists") is True and j["bots"]["2"].get("waiting_code") == ""
          and j["bots"]["2"].get("free_for_dolls") == 4 and j["bots"]["2"].get("dolls_set") == 0, str(j.get("bots"))[:600])
    b24e.bot(1, [], most=9)
    b24e.set_units({"mgo1host@2"})
    out = b24e.run(3)
    why = "bot 1's playable dolls (2) wait: bot 1 is not running (its service is inactive)"
    check("the same bot there and stopped (its last status: hosting): a warning, in its own log too", b24e.dolls_on() == [] and said(b24e, out, why, "W", "bot1"), out)
    same(b24e, "a stopped bot", "bot_stopped", bot="1", dolls_set=2, real=0, dolls_in=0, waiting_why=why)
    # its game not hosting yet (its status: starting, lost, none)
    for state, text in (("starting", "state starting"), ("lost", "state lost"), (None, "no status yet")):
        b24f = Box()
        b24f.bot(2, VD, most=9, vdolls=3)
        st = json.loads(open(os.path.join(b24f.var, "bot2", "run", "status.json")).read())
        st["state"] = state
        b24f.write("var/bot2/run/status.json", json.dumps(st) if state else "{}")
        for n in (1, 2):
            b24f.doll(n, bot=2, account=False)
        out = b24f.run(3)
        why = "bot 2's playable dolls (2) wait: its game is not hosting yet (%s)" % text
        check("its bot's game not hosting (%s): said once, not a warning" % text, b24f.dolls_on() == [] and said(b24f, out, why, "I"), out)
        same(b24f, "not hosting (%s)" % text, "not_hosting", dolls_set=2, waiting_why=why)
    # DOLL_GATE=0 with accountless dolls: a warning; one with an account plays, the others said
    b24g = Box()
    b24g.bot(2, VD, most=9, vdolls=3)
    for n in (1, 2):
        b24g.doll(n, bot=2, account=False)
    out = b24g.run(3, gate=False)
    why = "bot 2's playable dolls (2) wait: no account, and DOLL_GATE=0 in server.env (no accountless doll on this server)"
    check("DOLL_GATE=0, accountless dolls: none started, a warning says why", b24g.dolls_on() == [] and said(b24g, out, why, "W"), out)
    same(b24g, "the gate off", "gate_off", dolls_set=2, waiting_why=why)
    b24g.doll(3, bot=2)
    out = b24g.run(3, gate=False)
    why = "bot 2: 3 playable dolls set, only 1 can start: 2 of them have no account, and DOLL_GATE=0 in server.env"
    check("with a doll that has an account: it plays, the two others said", b24g.dolls_on() == [3] and said(b24g, out, why, "W"), out)
    check("no account, no password in the lines", "SECRET" not in out and "DOLLACC" not in out and "SECRET" not in b24g.day_log("manager"), out)
    same(b24g, "the gate off for two of three", "gate_off", dolls_set=3)
    # dolls the owner turned off (DOLL_ENABLED=0): none set, nothing said
    b24h = Box()
    b24h.bot(2, VD, most=9, vdolls=3)
    for n in (1, 2):
        b24h.doll(n, bot=2, account=False, enabled=False)
    out = b24h.run(3)
    check("dolls turned off by their owner: nothing said", "playable doll" not in out, out)
    same(b24h, "dolls turned off", "", dolls_set=0, free_for_dolls=4)
    # the server's budget: 2 cores, bot 2 and its 3 virtual dolls leave 1.06 of a core: 2 dolls of 4
    b24i = Box()
    b24i.bot(2, VD, most=9, vdolls=3)
    for n in (1, 2, 3, 4):
        b24i.doll(n, bot=2, account=False)
    out = b24i.run(8, cores=2)
    why = "bot 2: 4 playable dolls set, only 2 can start: the server can run 2 dolls now (2 in all besides its bots and their virtual dolls)"
    check("2 cores: 2 dolls of 4, said once, not a warning", b24i.dolls_on() == [1, 2] and said(b24i, out, why, "I"), str(b24i.dolls_on()) + out)
    same(b24i, "the server's budget", "server_full", extra={"MGO1_CORES": "2"}, free_for_dolls=4, dolls_set=4, waiting_why=why)
    b24j = Box()
    b24j.bot(2, VD, most=9, vdolls=3)
    b24j.doll(1, bot=2, account=False)
    out = b24j.run(3, cores=1, mem=700)
    why = "bot 2's playable doll (1) waits: the server can run 0 dolls now (0 in all besides its bots and their virtual dolls)"
    check("a server too small for any doll: a warning", b24j.dolls_on() == [] and said(b24j, out, why, "W"), out)
    same(b24j, "no budget at all", "server_full", extra={"MGO1_CORES": "1", "MGO1_MEM_MB": "700"}, waiting_why=why)
    # the pace guard's cap kept from before (pace_guard.json): said, with the wait; DOLL_PACE_GUARD=0: no cap
    b24k = Box()
    b24k.bot(2, VD, most=9, vdolls=3)
    for n in (1, 2, 3):
        b24k.doll(n, bot=2, account=False)
    guard = {"2": {"cap": 0, "at": time.time() - 86400, "hold": 3600.0, "ok_since": time.time() - 1320, "seen": {}, "slow": {}}}
    b24k.write("var/pace_guard.json", json.dumps(guard))
    out = b24k.run(3)
    why = "bot 2's playable dolls (3) wait: the pace guard allows 0 for 38 more minutes at least (its game ran below its pace; DOLL_PACE_GUARD=0 in server.env: no guard)"
    check("a cap of 0 kept by the pace guard: none started, said with its wait", b24k.dolls_on() == [] and said(b24k, out, why, "I"), out)
    m, j = same(b24k, "the pace guard's cap", "pace_guard", dolls_set=3, pace_cap=0, free_for_dolls=4)
    check("its wait: about 38 minutes, the manager's and the agent's", 2200 <= m.get("pace_wait_s", 0) <= 2280
          and 2200 <= j["bots"]["2"].get("pace_wait_s", 0) <= 2280, (m.get("pace_wait_s"), j["bots"]["2"].get("pace_wait_s")))
    guard["2"].update(cap=1, ok_since=None)
    b24k.write("var/pace_guard.json", json.dumps(guard))
    out = b24k.run(3)
    check("a cap of 1, its game not at its pace: 1 doll of 3, the whole hold to come", b24k.dolls_on() == [1]
          and "bot 2: 3 playable dolls set, only 1 can start: the pace guard allows 1 for 60 more minutes at least" in out, str(b24k.dolls_on()) + out)
    same(b24k, "a cap of 1", "pace_guard", pace_cap=1)
    out = b24k.run(6, server_extra="DOLL_PACE_GUARD=0\n")
    check("DOLL_PACE_GUARD=0: the cap not kept to, the 3 dolls started, nothing said", b24k.dolls_on() == [1, 2, 3] and "playable doll" not in out, out)
    b24k.write("etc/server.env", "DOLL_KEEP_SEAT=1\nDOLL_PACE_GUARD=0\n")
    j = b24k.agent("dolls")
    check("the agent then: no cap, no wait", j.get("bots", {}).get("2", {}).get("pace_cap") is None and j["bots"]["2"].get("waiting_code") == ""
          and j["bots"]["2"].get("pace_wait_s") == 0, str(j.get("bots"))[:400])
    # two bots share the budget in their order: bot 1's dolls first (2 cores, 2 bots: 2 dolls), bot 2's wait for the server
    b24l = Box()
    b24l.bot(1, [REAL[0]], most=9)
    b24l.bot(2, [REAL[1]], most=9)
    for n in (1, 2):
        b24l.doll(n, bot=1, account=False)
    for n in (3, 4):
        b24l.doll(n, bot=2, account=False)
    out = b24l.run(8, cores=2)
    why = "bot 2's playable dolls (2) wait: the server can run 0 more dolls now (2 in all besides its bots and their virtual dolls)"
    check("two bots, a budget of 2: bot 1's dolls play, bot 2's wait for the server, said", b24l.dolls_on() == [1, 2] and said(b24l, out, why, "I"), out)
    same(b24l, "the budget shared, bot 1", "", bot="1", extra={"MGO1_CORES": "2"}, dolls_set=2)
    same(b24l, "the budget shared, bot 2", "server_full", extra={"MGO1_CORES": "2"}, dolls_set=2, waiting_why=why)
    j = b24l.agent("dolls", extra={"MGO1_CORES": "2"})
    check("the agent's old fields kept, the new ones beside them", all(k in j for k in ("dolls", "max", "capacity", "accountless", "bots", "keep_seat", "list",
          "names", "unranked", "budget", "manager")) and all(k in j["bots"]["1"] for k in ("accountless", "with_account", "doll_count", "most", "exists",
          "waiting_code", "waiting_why", "seats", "real", "vdolls", "kept", "free_for_dolls", "dolls_set", "dolls_in", "pace_cap", "pace_wait_s")), str(j)[:600])
    # a virtual doll with a playable doll's name (the owner's SaveMGO_DOLL_1 to 3, both kinds): the playable doll is not taken
    # for one in the game by that name (never started again before, counted in the game)
    b24m = Box()
    named = [dict(v, name="SaveMGO_DOLL_%d" % v["vdoll"]) for v in VD]
    b24m.bot(2, named, most=9, vdolls=3)
    b24m.write("etc/doll1.env", "DOLL_NAME=SaveMGO_DOLL_1\nDOLL_BOT=2\nDOLL_ENABLED=1\n")
    b24m.set_units(b24m.units() | {"mgo1doll@1"})
    out = b24m.run(4, join_wait=0.3, period=0.2)
    check("a virtual doll named as a playable doll: the playable one, not in the game, started again after its join timeout",
          "restart mgo1doll@1" in b24m.log() and "doll 1: join timeout" in out, b24m.log() + out[-600:])
    same(b24m, "a virtual doll's name", "", dolls_set=1, dolls_in=0)
    b24m.bot(2, named + [{"name": "SaveMGO_DOLL_1", "host": 0, "kind": "doll", "doll": 1, "vdoll": 0}], most=9, vdolls=3)
    b24m.set_units({"mgo1host@2", "mgo1doll@1"})
    open(b24m.db + ".log", "w").close()
    out = b24m.run(4, join_wait=0.3, period=0.2)
    check("the playable doll in the game beside it (found by its number): left alone, counted in", "restart mgo1doll@1" not in b24m.log(), b24m.log() + out[-400:])
    same(b24m, "both in the game", "", dolls_set=1, dolls_in=1)
    # the line again when the reason changes, and once each day while it lasts (the manager's own function)
    saved = dict(os.environ)
    os.environ.update({"MGO1_VAR": b24l.var, "MGO1_ETC": b24l.etc, "MGO1_LOG_DIR": os.path.join(b24l.dir, "log"), "MGO1LOG": os.path.join(b24l.dir, "none")})
    try:
        loader = importlib.machinery.SourceFileLoader("mgo1doll_manager_w", MANAGER)
        mgr = types.ModuleType(loader.name)
        loader.exec_module(mgr)

        def sums(real, day=None):
            return mgr.doll_sums(2, True, "active", "hosting", 9, real, 3, True, 3, 0, 8, 8, None, 0, 0)

        class Clock:
            day = "2026-11-10"
            gmtime = staticmethod(time.gmtime)

            @classmethod
            def strftime(cls, fmt, t=None):
                return cls.day
        mgr.time = Clock
        memo, out = {}, io.StringIO()
        with contextlib.redirect_stdout(out):
            for real in (0, 0, 2, 2, 3, 4, 4, 3, 2, 0):          # (4 seats for them: 3 play, 2 of 3, 1, none, none, 1, 2, 3)
                mgr.say_why(2, sums(real), memo)
        lines = out.getvalue().splitlines()
        check("a reason said when it comes, when none can start any more and when some can again (the dolls' own lines tell each leave), not each round",
              len(lines) == 3 and "only 2 can start" in lines[0] and "none can start" in lines[1] and "only 1 can start" in lines[2], lines)
        memo, out = {}, io.StringIO()
        with contextlib.redirect_stdout(out):
            for day in ("2026-11-10", "2026-11-10", "2026-11-11", "2026-11-11", "2026-11-12"):
                Clock.day = day
                mgr.say_why(2, sums(4), memo)
        check("a reason that lasts: once each day (every day's log tells it)", len(out.getvalue().splitlines()) == 3, out.getvalue())
        s = mgr.doll_sums(2, True, "active", "hosting", 9, 0, 3, True, 4, 0, 8, 8, None, 0, 4)
        check("every doll set may play: no code, no line", s["code"] == "" and s["why"] == "" and s["want"] == 4 and s["sig"] is None, s)
    finally:
        os.environ.clear()
        os.environ.update(saved)

    # 25. (v150-host beta 16) a playable doll at its bot's virtual dolls' level (DOLL_DIFFICULTY=vdoll): the manager writes each
    # such doll's level into dolls.levels for its start (the k-th of the bot's dolls set to play takes the k-th level of
    # VDOLL_LEVEL, the last for the rest; none: 5), a doll it adds for a bot with virtual dolls follows them; the agent's
    # doll-set, doll-order and doll-all take "vdoll" (the number and `adapt off` sent live), `dolls` gives the number and
    # where it comes from, vdoll-set's level tells the running dolls that follow
    b25 = Box()
    b25.bot(2, VD, most=9, vdolls=3)
    b25.write("etc/bot2.env", "HOST_ACCOUNT_ID=BOT2\nVDOLL_COUNT=3\nVDOLL_LEVEL=10,9,8\n")

    def env25(n):
        p = os.path.join(b25.etc, "doll%d.env" % n)
        return open(p).read() if os.path.exists(p) else ""

    def clear25(*ns):
        for n in ns:
            p = os.path.join(b25.var, "doll%d" % n, "command")
            if os.path.exists(p):
                open(p, "w").close()

    def levels25():
        p = os.path.join(b25.var, "dolls.levels")
        return open(p).read() if os.path.exists(p) else None
    b25.write("etc/doll1.env", "DOLL_BOT=2\nDOLL_DIFFICULTY=3\nDOLL_ENABLED=1\n")
    b25.write("etc/doll2.env", "DOLL_BOT=2\nDOLL_DIFFICULTY=vdoll\nDOLL_ENABLED=1\n")
    b25.write("etc/doll3.env", "DOLL_BOT=2\nDOLL_DIFFICULTY=vdoll\nDOLL_ENABLED=0\n")
    b25.write("etc/doll5.env", "DOLL_BOT=2\nDOLL_DIFFICULTY=vdoll\nDOLL_ADAPT=1\nDOLL_ENABLED=1\n")
    b25.write("etc/doll6.env", "DOLL_BOT=7\nDOLL_DIFFICULTY=vdoll\nDOLL_ENABLED=1\n")
    out = b25.run(1)
    check("the manager's dolls.levels: the 2nd of the bot's dolls at the 2nd level, the 3rd (one turned off not counted) at the 3rd, "
          "a bot with no VDOLL_LEVEL: 5; before any start", levels25() == "2 9 2 10,9,8\n3 8 2 10,9,8\n5 8 2 10,9,8\n6 5 7 5\n", repr(levels25()))
    j = b25.agent("dolls")
    shown = {d["doll"]: (d.get("difficulty"), d.get("tier"), d.get("difficulty_from"), d.get("adapt")) for d in j.get("dolls", [])}
    check("dolls: the same numbers, their tiers, where they come from (vdoll, set), the live level off for those that follow",
          shown == {1: (3, "NORMAL", "set", True), 2: (9, "EXTREME", "vdoll", False), 3: (8, "MASTER", "vdoll", False), 5: (8, "MASTER", "vdoll", False),
                    6: (5, "EXPERT", "vdoll", False)}, shown)
    check("dolls: the answer tells that this server takes the difficulty vdoll (\"vdoll_difficulty\": true)", j.get("vdoll_difficulty") is True, j.get("vdoll_difficulty"))
    for bad, want in (("VDOLL_LEVEL=11\n", "2 5 2 5\n"), ("", "2 5 2 5\n"), ("VDOLL_LEVEL= 4 \n", "2 4 2 4\n"), ("VDOLL_LEVEL=\"7,6\"\n", "2 6 2 7,6\n")):
        b25.write("etc/bot2.env", "HOST_ACCOUNT_ID=BOT2\nVDOLL_COUNT=3\n" + bad)
        b25.run(1)
        j = b25.agent("dolls")
        check("VDOLL_LEVEL %r: doll 2 at %s, the manager's file and the agent" % (bad.strip(), want.split()[1]), (levels25() or "").startswith(want)
              and {d["doll"]: d["difficulty"] for d in j.get("dolls", [])}.get(2) == int(want.split()[1]), repr(levels25()))
    b25.write("etc/bot2.env", "HOST_ACCOUNT_ID=BOT2\nVDOLL_COUNT=3\nVDOLL_LEVEL=10,9,8\n")
    # the agent: doll-set, doll-order, doll-all
    j = b25.agent("doll-set", "1", stdin=json.dumps({"difficulty": "vdoll"}))
    d = j.get("doll", {})
    check("doll-set difficulty vdoll: DOLL_DIFFICULTY=vdoll kept, the first doll at the first level (10 PRO), the number and `adapt off` sent live",
          j.get("ok") and "DOLL_DIFFICULTY=vdoll\n" in env25(1) and (d.get("difficulty"), d.get("tier"), d.get("difficulty_from"), d.get("adapt")) ==
          (10, "PRO", "vdoll", False) and "difficulty 10\nadapt off\n" in b25.command(1), str(j)[:300] + b25.command(1))
    clear25(1)
    j = b25.agent("doll-set", "1", stdin=json.dumps({"adapt": True}))
    check("its live level set on meanwhile: kept in its settings (DOLL_ADAPT=1), off in play (no `adapt on` sent)", j.get("ok") and "DOLL_ADAPT=1\n" in env25(1)
          and "adapt on" not in b25.command(1) and j.get("doll", {}).get("adapt") is False, env25(1) + b25.command(1))
    clear25(1)
    j = b25.agent("doll-order", "1", "difficulty", "6")
    check("a number again (doll-order difficulty 6): its own level, its live level back as set (`adapt on`)", j.get("ok") and "DOLL_DIFFICULTY=6\n" in env25(1)
          and "difficulty 6\nadapt on\n" in b25.command(1) and (j["doll"].get("difficulty"), j["doll"].get("difficulty_from"), j["doll"].get("adapt")) == (6, "set", True),
          str(j)[:300] + b25.command(1))
    clear25(5)
    j = b25.agent("doll-order", "5", "difficulty", "vdoll")
    check("doll-order 5 difficulty vdoll: its place's level (8), `adapt off`", j.get("ok") and "difficulty 8\nadapt off\n" in b25.command(5)
          and j["doll"].get("difficulty_from") == "vdoll", str(j)[:300] + b25.command(5))
    clear25(5)
    j = b25.agent("doll-order", "5", "adapt", "on")
    check("doll-order adapt on for one that follows: kept, not sent", j.get("ok") and "DOLL_ADAPT=1\n" in env25(5) and b25.command(5) == "", b25.command(5))
    for bad in ("vdolls", "VDOLL", "11", ""):
        j = b25.agent("doll-order", "5", "difficulty", bad)
        check("doll-order difficulty %r refused" % bad, not j.get("ok") and j.get("code") == "usage", str(j)[:200])
    before = env25(5)
    for bad in ("vdolls", "auto", ["vdoll"], 0):
        j = b25.agent("doll-set", "5", stdin=json.dumps({"difficulty": bad}))
        check("doll-set difficulty %r refused, nothing written" % (bad,), not j.get("ok") and j.get("code") == "usage" and env25(5) == before, str(j)[:200])
    clear25(1, 2, 3, 5)
    j = b25.agent("doll-all", "2", "difficulty", "vdoll")
    lv = {d["doll"]: (d.get("difficulty"), d.get("difficulty_from")) for d in j.get("dolls", [])}
    check("doll-all 2 difficulty vdoll: each doll of bot 2 at its place's level (10, 9, 8, 8), bot 7's left, each told its number and `adapt off`",
          j.get("ok") and lv == {1: (10, "vdoll"), 2: (9, "vdoll"), 3: (8, "vdoll"), 5: (8, "vdoll"), 6: (5, "vdoll")}
          and "difficulty 10\nadapt off\n" in b25.command(1) and "difficulty 9\nadapt off\n" in b25.command(2) and "difficulty 8\nadapt off\n" in b25.command(5)
          and b25.command(6) == "", str(lv) + b25.command(1))
    j = b25.agent("doll-all", "2", "difficulty", "easy")
    check("doll-all difficulty easy refused", not j.get("ok") and j.get("code") == "usage", str(j)[:200])
    # the owner changes the virtual dolls' level: the running dolls that follow it are told at once, the others at their start
    b25.set_units({"mgo1host@2", "mgo1doll@1", "mgo1doll@5"})
    clear25(1, 2, 3, 5)
    j = b25.agent("vdoll-set", "2", stdin=json.dumps({"level": "7,7,4"}))
    check("vdoll-set level 7,7,4: the running dolls 1 and 5 told 7 and 4 with `adapt off`, the stopped ones not", j.get("ok")
          and b25.command(1) == "difficulty 7\nadapt off\n" and b25.command(5) == "difficulty 4\nadapt off\n" and b25.command(2) == "" and b25.command(3) == "",
          str(j)[:200] + repr([b25.command(n) for n in (1, 2, 3, 5)]))
    b25.run(1)
    b25.set_units({"mgo1host@2", "mgo1doll@1", "mgo1doll@5"})
    check("the manager's file follows within a round", levels25() == "1 7 2 7,7,4\n2 7 2 7,7,4\n3 4 2 7,7,4\n5 4 2 7,7,4\n6 5 7 5\n", repr(levels25()))
    clear25(1, 5)
    j = b25.agent("vdoll-set", "2", stdin=json.dumps({"count": 2}))
    check("vdoll-set without a level: nobody told", j.get("ok") and b25.command(1) == "" and b25.command(5) == "", b25.command(1))
    # a doll before them turned off or removed: those after it move up a place, the running ones told
    clear25(1, 5)
    j = b25.agent("doll-set", "2", stdin=json.dumps({"enabled": False}))
    check("doll 2 turned off: doll 5 the 2nd now (7), told at once", j.get("ok") and b25.command(5) == "difficulty 7\nadapt off\n"
          and {d["doll"]: d["difficulty"] for d in b25.agent("dolls").get("dolls", [])}.get(5) == 7, b25.command(5))
    # a doll the manager adds for a bot's DOLL_COUNT: at the virtual dolls' level when the bot has some set, else 3
    b25b = Box()
    b25b.bot(1, [], most=9, count=1)
    b25b.bot(2, VD, most=9, vdolls=3, count=1)
    b25b.bot(3, [], most=9, count=1)
    b25b.bot(4, [], most=9, count=1)
    b25b.write("etc/bot2.env", "HOST_ACCOUNT_ID=BOT2\nDOLL_COUNT=1\nVDOLL_COUNT=3\n")
    b25b.write("etc/bot3.env", "HOST_ACCOUNT_ID=BOT3\nDOLL_COUNT=1\nVDOLL_COUNT=0\nVDOLL_LEVEL=10,10,10,10,10,5\n")
    b25b.write("etc/bot4.env", "HOST_ACCOUNT_ID=BOT4\nDOLL_COUNT=1\nVDOLL_COUNT=0\n")
    out = b25b.run(1)
    made = {n: open(os.path.join(b25b.etc, "doll%d.env" % n)).read() for n in (1, 2, 3, 4)}
    check("DOLL_COUNT's new dolls: 3 for a bot with no virtual doll set, vdoll for a bot with VDOLL_COUNT above 0 or a VDOLL_LEVEL",
          "DOLL_DIFFICULTY=3\n" in made[1] and "DOLL_DIFFICULTY=vdoll\n" in made[2] and "DOLL_DIFFICULTY=vdoll\n" in made[3] and "DOLL_DIFFICULTY=3\n" in made[4],
          str(made))
    check("their levels written in the same round, before their start", open(os.path.join(b25b.var, "dolls.levels")).read() == "2 5 2 5\n3 10 3 10,10,10,10,10,5\n",
          open(os.path.join(b25b.var, "dolls.levels")).read())
    j = b25b.agent("doll-count", "3", "6")
    lv = sorted(d["difficulty"] for d in j.get("dolls", []) if d["bot"] == 3)
    check("the owner's VDOLL_LEVEL 10,10,10,10,10,5 and six dolls: five at 10, the sixth at 5", j.get("ok") and lv == [5, 10, 10, 10, 10, 10]
          and all(d.get("difficulty_from") == "vdoll" for d in j["dolls"] if d["bot"] == 3), str(lv))

    # 26. (v150-host beta 16) root never writes or reads through a symbolic link in the folders the games' user owns (the
    # dolls' folders, a bot's run folder, /var/lib/mgo1host itself): a doll's command file, its folder, its kept name, the
    # manager's own files, a bot's cmd.txt and vdolls.live, the admin's events; a pipe left as a status file holds nothing up
    b26 = Box()
    out_dir = os.path.join(b26.dir, "outside")
    os.makedirs(out_dir)

    def outside(name, text="ROOT ONLY\n"):
        p = os.path.join(out_dir, name)
        with open(p, "w") as f:
            f.write(text)
        return p

    def intact(p, text="ROOT ONLY\n"):
        return open(p).read() == text
    for n in (1, 2):
        b26.doll(n, name="PLAYER DOLL %d" % n)
    b26.doll(3, name=None)
    b26.bot(1, ["REAL%d" % k for k in range(1, 8)] + ["PLAYER DOLL 1", "PLAYER DOLL 2"])
    b26.set_units(b26.units() | {"mgo1doll@1", "mgo1doll@2"})
    cmd1, name3, tmp_names, guard_file = outside("cmd1"), outside("name3", "TOPSECRETNAME\n"), outside("names_tmp"), outside("guard", "{}")
    os.symlink(cmd1, os.path.join(b26.var, "doll1", "command"))
    os.makedirs(os.path.join(b26.var, "doll3"))
    os.symlink(name3, os.path.join(b26.var, "doll3", "name"))
    os.symlink(tmp_names, os.path.join(b26.var, "dolls.names.tmp"))
    os.symlink(guard_file, os.path.join(b26.var, "pace_guard.json"))
    os.makedirs(os.path.join(out_dir, "doll5dir"))
    b26.write("etc/doll5.env", "DOLL_ACCOUNT_ID=DOLLACC5\nDOLL_ACCOUNT_PASSWORD=SECRET5\nDOLL_BOT=1\nDOLL_ENABLED=1\n")
    os.symlink(os.path.join(out_dir, "doll5dir"), os.path.join(b26.var, "doll5"))
    with open(os.path.join(out_dir, "doll5dir", "status.json"), "w") as f:
        json.dump({"name": "LINKED NAME", "identity": "uid"}, f)
    b26.doll(6, name=None)
    os.makedirs(os.path.join(b26.var, "doll6"))
    os.mkfifo(os.path.join(b26.var, "doll6", "status.json"))
    t0 = time.time()
    out = b26.run(3, leave_wait=0)
    check("a link left as a doll's command file: the manager's leave order not written through it, said, the doll stopped after its time",
          intact(cmd1) and 'doll 2: told to leave' in out and 'doll 1: its order "leave" could not be written' in out and b26.dolls_on() == [],
          out + open(cmd1).read() + str(b26.dolls_on()))
    check("the other doll's order written as before (a plain file)", "leave" in b26.command(2) and not os.path.islink(os.path.join(b26.var, "doll2", "command")),
          b26.command(2))
    check("a link left as a doll's kept name: not read (nothing of that file in dolls.names), not written", intact(name3, "TOPSECRETNAME\n")
          and "TOPSECRETNAME" not in " ".join(b26.names()), b26.names())
    check("a doll's folder that is a link: its status not read through it, nothing written there", "LINKED" not in " ".join(b26.names())
          and sorted(os.listdir(os.path.join(out_dir, "doll5dir"))) == ["status.json"], str(b26.names()) + str(os.listdir(os.path.join(out_dir, "doll5dir"))))
    check("links left under the manager's own files' old temporary names and its pace guard's file: the files outside untouched, its own written",
          intact(tmp_names) and intact(guard_file, "{}") and os.path.isfile(os.path.join(b26.var, "dolls.names"))
          and not os.path.islink(os.path.join(b26.var, "dolls.names")) and "PLAYER" in b26.names(), str(b26.names()))
    check("a pipe left as a doll's status file: the manager's rounds not held up", time.time() - t0 < 60, time.time() - t0)
    check("no temporary file of the manager's left behind", not [f for f in os.listdir(b26.var) if f.endswith(".tmp") and not os.path.islink(os.path.join(b26.var, f))],
          os.listdir(b26.var))
    # the agent (root over SSH): a bot's cmd.txt, a doll's command, the live count, the admin's events
    b26.write("var/bot1/run/vdolls.env", "VDOLL_COUNT=0\n")
    cmdtxt, live, ev, cmd2 = outside("cmdtxt"), outside("live", "0\n"), outside("events"), outside("cmd2")
    os.symlink(cmdtxt, os.path.join(b26.var, "bot1", "run", "cmd.txt"))
    os.symlink(live, os.path.join(b26.var, "bot1", "run", "vdolls.live"))
    os.makedirs(os.path.join(b26.var, "bot1", "events"), exist_ok=True)
    os.symlink(ev, os.path.join(b26.var, "bot1", "events", time.strftime("%Y-%m-%d", time.gmtime()) + ".admin.jsonl"))
    j = b26.agent("kick", "P3")
    check("the agent's kick with a link left as the bot's cmd.txt: refused (unsafe_file), the file outside untouched", not j.get("ok")
          and j.get("code") == "unsafe_file" and intact(cmdtxt), str(j)[:300])
    os.remove(os.path.join(b26.var, "bot1", "run", "cmd.txt"))
    j = b26.agent("kick", "P3")
    check("with a plain cmd.txt: written as before", j.get("ok") and open(os.path.join(b26.var, "bot1", "run", "cmd.txt")).read() == "kick P3\n"
          and intact(ev), str(j)[:300])
    j = b26.agent("vdoll-count", "1", "2")
    lv = os.path.join(b26.var, "bot1", "run", "vdolls.live")
    check("vdoll-count with a link left as vdolls.live: the link replaced by the count's own file, the file outside untouched", j.get("ok")
          and intact(live, "0\n") and not os.path.islink(lv) and open(lv).read() == "2\n", str(j)[:300])
    os.remove(os.path.join(b26.var, "doll2", "command"))
    os.symlink(cmd2, os.path.join(b26.var, "doll2", "command"))
    j = b26.agent("doll-order", "2", "team", "red")
    check("doll-order with a link left as the doll's command file: refused (unsafe_file), the file outside untouched", not j.get("ok")
          and j.get("code") == "unsafe_file" and intact(cmd2), str(j)[:300])
    j = b26.agent("doll-order", "5", "team", "blue")
    check("doll-order for a doll whose folder is a link: refused, nothing written there", not j.get("ok") and j.get("code") == "unsafe_file"
          and sorted(os.listdir(os.path.join(out_dir, "doll5dir"))) == ["status.json"], str(j)[:300])
    check("no temporary file left in the bot's settings folder nor its run folder", not [f for f in os.listdir(b26.etc) + os.listdir(os.path.join(b26.var, "bot1", "run"))
          if f.endswith(".tmp")], os.listdir(b26.etc))

    # 26b. (v150-host beta 16) the seat kept for a real player, the manager's rule and the game's own for its virtual dolls
    # the same: with DOLL_GATE=0 the manager kept none once DOLL_KEEP_SEAT=0, while the bot's game keeps one for its
    # virtual dolls unless the dolls are off the lobby's list too (DOLL_LIST=off): 5 playable dolls and 3 virtual ones
    # filled the 8 seats, and the game then sent its newest virtual doll away for good
    b26b = Box()
    b26b.bot(2, VD, most=9, vdolls=3)
    for n in range(1, 7):
        b26b.doll(n, bot=2)
    out = b26b.run(8, keep_seat=False, gate=False)
    check("DOLL_GATE=0, DOLL_KEEP_SEAT=0, the dolls listed, 3 virtual dolls: a seat kept as the game keeps it, 4 playable dolls (7 joiners of 8 seats)",
          b26b.dolls_on() == [1, 2, 3, 4] and "1 kept for a real player" in out, str(b26b.dolls_on()) + out)
    b26b.write("etc/server.env", "DOLL_KEEP_SEAT=0\nDOLL_GATE=0\n")
    same(b26b, "the gate off with virtual dolls", "seats", kept=1, vdolls=3, free_for_dolls=4, dolls_set=6)
    out = b26b.run(8, keep_seat=False, gate=False, listed="off")
    check("the same with DOLL_LIST=off: no seat kept on either side, 5 playable dolls", b26b.dolls_on() == [1, 2, 3, 4, 5], str(b26b.dolls_on()) + out)
    b26c = Box()
    b26c.bot(2, [], most=9)
    for n in range(1, 10):
        b26c.doll(n, bot=2)
    out = b26c.run(10, keep_seat=False, gate=False)
    check("DOLL_GATE=0, DOLL_KEEP_SEAT=0 and no virtual doll: every seat for the dolls, as before (8 of 9)", b26c.dolls_on() == list(range(1, 9)),
          str(b26c.dolls_on()) + out)
    b26c.write("etc/server.env", "DOLL_KEEP_SEAT=0\nDOLL_GATE=0\n")
    same(b26c, "the gate off, no virtual doll", "seats", kept=0, vdolls=0, free_for_dolls=8, dolls_set=9)

    # 27. (v150-host beta 16) a file the manager cannot write (a full disk, a read-only folder: here a folder in each file's
    # place, as the tests run as root) never stops it: its rounds go on, the watchdog restarts a hung bot all the same, each
    # cause is said once (not each round), and said again when the file is written again
    b27 = Box()
    b27.bot(1, ["REAL1"], count=1)
    b27.bot(2, ["REAL2"])
    b27.doll(2, bot=2, name="ACC DOLL 2", identity="uid")
    b27.write("etc/bot2.env", "HOST_ACCOUNT_ID=BOT2\nVDOLL_LEVEL=7\n")
    b27.write("etc/doll3.env", "DOLL_BOT=2\nDOLL_DIFFICULTY=vdoll\nDOLL_ENABLED=1\n")
    for blocked in ("var/dolls.names", "var/dolls.levels", "var/dolls.sums.json", "var/pace_guard.json", "etc/doll1.env.tmp"):
        os.makedirs(os.path.join(b27.dir, blocked, "x"))
    b27.write("var/doll3", "a file where doll 3's folder should be\n")
    os.utime(os.path.join(b27.var, "bot1", "run", "status.json"), (1000000000, 1000000000))
    out = b27.run(4, hang=60, grace=0)
    check("files that cannot be written: the manager's 4 rounds end well, the hung bot restarted by its watchdog", "restart mgo1host@1" in b27.log()
          and "stopped by an error" not in out, b27.log() + out[-900:])
    check("each cause said once in 4 rounds, an error: the names, the levels, a new doll's settings",
          out.count("dolls.names could not be written") == 1 and out.count("dolls.levels could not be written") == 1
          and out.count("doll<N>.env could not be written") == 1 and "bot 1's DOLL_COUNT=1 not reached, no doll added" in out
          and " E mgo1doll-manager: " in b27.day_log("manager"), out)
    check("the dolls that have their files are started all the same", "start mgo1doll@2" in b27.log(), b27.log())
    for blocked in ("var/dolls.names", "var/dolls.levels", "etc/doll1.env.tmp"):
        import shutil as _sh
        _sh.rmtree(os.path.join(b27.dir, blocked))
    out = b27.run(2)
    check("the files written once they can be: the names, the levels, the doll of the bot's DOLL_COUNT", "ACC" in b27.names()
          and os.path.isfile(os.path.join(b27.var, "dolls.levels")) and "doll 1: added for bot 1" in out, out + str(b27.names()))
    # the manager's own function: a cause said once, again when it changes, and when the file is written again
    saved = dict(os.environ)
    os.environ.update({"MGO1_VAR": b27.var, "MGO1_ETC": b27.etc, "MGO1_LOG_DIR": os.path.join(b27.dir, "log"), "MGO1LOG": os.path.join(b27.dir, "none")})
    try:
        loader = importlib.machinery.SourceFileLoader("mgo1doll_manager_f", MANAGER)
        mgr = types.ModuleType(loader.name)
        loader.exec_module(mgr)
        o = io.StringIO()
        with contextlib.redirect_stdout(o):
            for e in (OSError(28, "No space left on device"), OSError(28, "No space left on device"), OSError(30, "Read-only file system"), None, None):
                mgr.write_failed("/var/lib/mgo1host/dolls.names", e, "the dolls keep the names they have")
        lines = o.getvalue().splitlines()
        check("a full disk said once, a read-only one once, written again once", len(lines) == 3 and "No space left on device" in lines[0]
              and "Read-only file system" in lines[1] and lines[2].endswith("dolls.names: written again"), lines)
    finally:
        os.environ.clear()
        os.environ.update(saved)

    print("%d failed" % len(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
