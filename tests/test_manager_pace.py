#!/usr/bin/env python3
"""mgo1doll-manager and a server short of processor (v150-host beta 16), off line: the manager's own classes with a stand-in
/proc/stat and status files, then the service run a few rounds. Checks: the steal of one processor (the cpu<N> lines: a
game's thread runs on one at a time, and its steal is a quarter of itself in a 4-processor server's figure) past 30 % two
minutes in a row said once, and its end; the whole server's warning as before with the first line alone, and alone when
every processor is high; a bot whose game runs under 90 % of its pace two minutes in a row while its virtual dolls play:
one warning a spell with what to do (one virtual doll less), its end after two minutes at its pace; no warning while its
virtual dolls are spectators, without a pace line, for the same pace line read twice; a new game's own two minutes; the
service looks at a bot that has no playable doll at all, the warning in its daily log and the bot's.
    python3 tests/test_manager_pace.py
"""
import contextlib, importlib.machinery, importlib.util, io, json, os, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
MANAGER = os.path.join(HERE, "..", "host", "mgo1doll-manager")
sys.path.insert(0, HERE)
import test_doll_manager as T                         # its Box: the folders, the stand-in systemctl
FAILS = []


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:700] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


def status(n, rate, vd=3, real=1, life="alive", cpu=72):
    """A bot's status as its game writes it: its last pace line, its virtual dolls and what each does."""
    return {"state": "hosting", "game": {"max": 9}, "players": [{"name": "host", "host": 1, "kind": "host"}],
            "players_real": real, "vdolls_set": vd, "players_vdolls": vd,
            "vdolls": [{"n": i + 1, "state": "in_game", "player": i + 2, "team": 0, "life": life, "deaths": 0, "leaves": 0} for i in range(vd)],
            "pace": {"n": n, "rate": rate, "due": 59.94, "cpu": cpu}}


def main():
    d = tempfile.mkdtemp(prefix="mgrpace_")
    saved = dict(os.environ)
    os.environ.update({"MGO1_PROC_STAT": os.path.join(d, "stat"), "MGO1LOG": os.path.join(d, "none"), "MGO1_VAR": d, "MGO1_ETC": d,
                       "MGO1_LOG_DIR": os.path.join(d, "log")})
    try:
        loader = importlib.machinery.SourceFileLoader("mgo1doll_manager_p", MANAGER)
        spec = importlib.util.spec_from_loader(loader.name, loader)
        mgr = importlib.util.module_from_spec(spec)
        loader.exec_module(mgr)

        def stat(cores):
            """cores: [(all its time, stolen)] cumulative for each processor."""
            tot, st = sum(a for a, b in cores), sum(b for a, b in cores)
            lines = ["cpu  %d 0 0 0 0 0 0 %d 0 0" % (tot - st, st)]
            lines += ["cpu%d %d 0 0 0 0 0 0 %d 0 0" % (i, a - b, b) for i, (a, b) in enumerate(cores)]
            with open(os.environ["MGO1_PROC_STAT"], "w") as f:
                f.write("\n".join(lines + ["intr 1 2 3", "ctxt 5"]) + "\n")

        # 1. one processor of four at 44 % of steal, the others at 1 %: all of them 12 % (no warning before)
        st, out = mgr.Steal(), io.StringIO()
        c = [[1000, 0], [1000, 0], [1000, 0], [1000, 0]]

        def minute(steals, t):
            for i, s in enumerate(steals):
                c[i][0] += 6000
                c[i][1] += s
            stat(c)
            st.look(t)
        with contextlib.redirect_stdout(out):
            stat(c)
            st.look(0.0)
            minute([60, 2640, 60, 60], 61.0)
            first = out.getvalue()
            minute([60, 2640, 60, 60], 122.0)
            second = out.getvalue()
            minute([60, 2640, 60, 60], 183.0)
            third = out.getvalue()
            minute([60, 60, 60, 60], 244.0)
        check("one processor at 44 %: nothing after one minute", first == "", first)
        check("one processor at 44 % two minutes: its warning, with all 4 at 12 %", "the server's processor cpu1: 44 % of its time taken by the machine under the VPS"
              in second and "(all 4 processors: 12 %)" in second and "mpstat -P ALL 5" in second, second)
        check("said once", third == second, third)
        check("its end said", "the VPS's machine takes 1 % of the worst one's time again" in out.getvalue(), out.getvalue())
        check("the steal's words for another line", "took 1 % of processor cpu" in st.text() and "1 % of all)" in st.text(), st.text())
        # 2. a /proc/stat with its first line alone: as before, no worst processor
        def one(total, stolen):
            with open(os.environ["MGO1_PROC_STAT"], "w") as f:
                f.write("cpu  %d 0 0 0 0 0 0 %d 0 0\n" % (total - stolen, stolen))
        st, out = mgr.Steal(), io.StringIO()
        with contextlib.redirect_stdout(out):
            one(1000, 0)
            st.look(0.0)
            one(2000, 300)
            st.look(61.0)
            one(3000, 600)
            st.look(122.0)
        check("the first line alone: the whole server's warning as before", "30 % of their time taken by the machine under the VPS" in out.getvalue()
              and st.worst is None, out.getvalue())
        check("its words", st.text() == " (the VPS's machine took 30 % of the processors)", st.text())
        # 3. every processor high: the whole server's warning alone
        st, out = mgr.Steal(), io.StringIO()
        c = [[1000, 0], [1000, 0]]
        with contextlib.redirect_stdout(out):
            stat(c)
            st.look(0.0)
            for t in (61.0, 122.0, 183.0):
                minute([3000, 3000], t)
        check("every processor at 50 %: one warning, the whole server's", out.getvalue().count("2 minutes in a row") == 1 and "their time" in out.getvalue(),
              out.getvalue())
        # 4. a bot slowed with its virtual dolls in play (the owner's night of 10 November: 3 virtual dolls, one real player)
        vp, out = mgr.VdollPace(), io.StringIO()
        st = mgr.Steal()
        st.pct, st.worst = 11.0, ("cpu2", 41.0)
        with contextlib.redirect_stdout(out):
            vp.look(2, status(10, 59.5, life="a spectator", cpu=14), st)
            vp.look(2, status(11, 42.5), st)
            a = out.getvalue()
            vp.look(2, status(11, 42.5), st)                 # (the same pace line read again: not a new minute)
            b = out.getvalue()
            vp.look(2, status(12, 36.0), st)
            c2 = out.getvalue()
            vp.look(2, status(13, 33.4), st)
            d2 = out.getvalue()
            vp.look(2, status(14, 59.6, life="a spectator", cpu=14), st)
            e = out.getvalue()
            vp.look(2, status(15, 59.7, life="a spectator", cpu=14), st)
            f = out.getvalue()
        check("one slow minute (and the same line read twice): nothing", a == "" and b == "", b)
        check("two slow minutes with virtual dolls in play: the warning, what to do", "bot 2: its game at 42.5 then 36.0 VBlanks a second (59.94 due) with 3 virtual "
              "doll(s) and 1 real player(s) in play, its process at 72 % of a core" in c2 and "took 41 % of processor cpu2, 11 % of all" in c2
              and "mgo1host-agent vdoll-count 2 2" in c2 and "CPU_PIN=1" in c2, c2)
        check("said once a spell", d2 == c2, d2)
        check("one minute at its pace: nothing yet", e == c2, e)
        check("two minutes at its pace: said", "bot 2: its game at its pace again (59.7 VBlanks a second, 59.94 due)" in f, f)
        # 5. slowed with its virtual dolls spectators (a map's load, nobody real): no warning; no status, no pace: nothing
        vp, out = mgr.VdollPace(), io.StringIO()
        with contextlib.redirect_stdout(out):
            vp.look(1, status(1, 40.0, life="a spectator"), None)
            vp.look(1, status(2, 40.0, life="a spectator"), None)
            vp.look(3, {}, None)
            vp.look(3, {"pace": {"n": "x"}}, None)
            vp.look(3, {"pace": {"n": 1, "rate": 30, "due": 59.94}}, None)
            vp.look(3, {"pace": {"n": 2, "rate": 30, "due": 59.94}}, None)
        check("no virtual doll in play: no warning", out.getvalue() == "", out.getvalue())
        # 6. the game started again (its pace lines from 1): the spell forgotten
        vp, out = mgr.VdollPace(), io.StringIO()
        with contextlib.redirect_stdout(out):
            vp.look(2, status(50, 30.0), None)
            vp.look(2, status(1, 30.0), None)
            g1 = out.getvalue()
            vp.look(2, status(2, 30.0), None)
        check("a new game: its own two minutes", g1 == "" and "bot 2: its game at 30.0 then 30.0" in out.getvalue()
              and "mgo1host-agent vdoll-count 2 2" in out.getvalue(), out.getvalue())
    finally:
        os.environ.clear()
        os.environ.update(saved)

    # 7. the service: a bot with virtual dolls and no playable doll at all is looked at (the owner's server), its two slow
    # minutes read while it runs; the warning in the manager's daily log and the bot's
    b = T.Box()
    b.write("etc/bot2.env", "HOST_ACCOUNT_ID=BOT2\nVDOLL_COUNT=3\n")
    b.write("etc/bot2.game", "name = GAME2\nmax_players = 9\n")
    b.write("etc/server.env", "DOLL_KEEP_SEAT=0\nDOLL_PACE_GUARD=0\n")
    b.write("stat0", "cpu  1000 0 1000 8000 0 0 0 0 0 0\n")
    b.write("var/bot2/run/status.json", json.dumps(status(7, 33.4)))
    b.set_units({"mgo1host@2"})
    env = b.env(DOLL_MANAGER_PERIOD="0.3", DOLL_START_GAP="0", DOLL_MANAGER_ROUNDS="10", MGO1_CORES="4", MGO1_MEM_MB="16000",
                MGO1_PROC_STAT=os.path.join(b.dir, "stat0"))
    p = subprocess.Popen([sys.executable, MANAGER], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1.0)
    b.write("var/bot2/run/status.json", json.dumps(status(8, 36.0)))
    out, _ = p.communicate(timeout=60)
    line = "bot 2: its game at 33.4 then 36.0 VBlanks a second (59.94 due) with 3 virtual doll(s) and 1 real player(s) in play"
    check("the service: a bot with no playable doll, two slow minutes with its virtual dolls in play: the warning, once", p.returncode == 0
          and out.count(line) == 1 and "mgo1host-agent vdoll-count 2 2" in out, out)
    check("in the manager's daily log and the bot's, a warning", " W mgo1doll-manager: " + line in b.day_log("manager")
          and " W mgo1doll-manager: " + line in b.day_log("bot2"), b.day_log("manager"))
    print("%d failed" % len(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
