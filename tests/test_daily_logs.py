#!/usr/bin/env python3
"""The daily logs (host/mgo1log, the agent's `logs`, the doll manager's log), off line in a temporary
folder. Checks: every line passed on to the journal and written with its time and level, the
launcher's notes, the day's summary (lines, errors, warnings, starts, crashes), the size cap (only
the errors and warnings past it), the old days removed, the agent's list and its pages, its level
and text filters, no account or password in what the agent returns, the manager's decisions logged;
(v150-host beta 16) the flood guard (the server's flood of 9 and 10 November 2026 as the game wrote it: the first
lines of a shape kept, then a summary with the first and the last line's own values, the journal the same, the
errors always kept, no line out of its order, the summaries every 10 s and the end said, across midnight and the cap,
the table of shapes bounded, MGO1_LOG_BURST=0 off); the cap's kept lines (past 64 MB a day the launchers', the Game
Report's, the players', the virtual dolls', the sessions' lines, the screens and the pace still written, the rest of the
ordinary lines not; past 4 times the cap the errors and warnings only; (v150-host beta 18) the joiner's hold, the direct
aim's and the virtual dolls' controls' lines kept too); the crash reports (a game that ends badly:
<folder>/crash/<UTC time>_<reason>.txt with what ended, the version, the game's last status and its last lines, the
flood summarised, the lines the cap kept out of the day's file too; no report for an end that is no failure; 2 MB and
4000 lines at most, 40 files and 31 days, 6 an hour; a stop by its service: none, unless the doll manager's watchdog asked
for one (hung)); the agent's view of a day too long for an export (a day's file written before the flood guard: its
flood lines left out first, summarised, then its oldest lines; the first line says how many; --raw: the file as it is).
    python3 tests/test_daily_logs.py
"""
import importlib.machinery, importlib.util, json, os, re, signal, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
LOGGER = os.path.join(HERE, "..", "host", "mgo1log")
AGENT = os.path.join(HERE, "..", "host", "mgo1host-agent")
MANAGER = os.path.join(HERE, "..", "host", "mgo1doll-manager")
FAILS = []


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:400] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


def run(args, stdin="", env=None):
    return subprocess.run([sys.executable] + args, input=stdin, capture_output=True, text=True, timeout=60,
                          env=dict(os.environ, **(env or {})))


def load(path, name):
    loader = importlib.machinery.SourceFileLoader(name, path)
    spec = importlib.util.spec_from_loader(name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


# (v150-host beta 16) the owner's server on 10 November 2026, bot 2 (capture on Ghost Factory with 3 virtual dolls and a real
# player): its last lines before the first wild read as the game wrote them (the real player's name and address changed),
# then the flood: 8 lines with the calls before, then two lines an address (the second one until the game's inner cap)
BEFORE = r"""ps2 f2570981: vdoll: the respawn points of n002a: 79
ps2 f2570982: players (5): P1:216HL P2:213 P3:213 P4:213 P5:213
ps2 f2570997: vdoll 1: picks the red team (round 2)
ps2 f2570997: vdoll 2: picks the blue team (round 2)
ps2 f2570997: vdoll 3: picks the red team (round 2)
ps2 f2570999: lobby filter: 4344 for the doll's user id 0x7f000052 kept here (game sequence 990), its answer 4345 {0} put in the receive stream (sequence 994)
ps2 f2570999: net: send(9) 29 bytes peer 107.161.80.126:5732: tx55 4344 len 5 seq 990
ps2 f2571000: players (5): P1:216HL P2:216 P3:216 P4:216 P5:213
ps2 f2571001: net: recv(9) 24 bytes peer 107.161.80.126:5732: tx55 4345 len 8 seq 994 (-8 bytes not a whole frame)
ps2 f2571001: vdoll 1: in the match, player 2 in the red team, at 1250 28 32000
ps2 f2571001: vdoll 2: in the match, player 3 in the blue team, at 0 817 -29500
ps2 f2571001: vdoll 3: in the match, player 4 in the red team, at 3125 28 32000
ps2 f2571060: report: {"t":1794270721934,"k":"team","a":"SaveMGO_DOLL_2","tm":"blue","pk":[{"l":"4344","d":"o","n":5,"x":"7f00005201"},{"tv":[3,1]}]}
ps2 f2571077: vdoll 1: deployed at 1250 28 32000
ps2 f2571095: vdoll 2: deployed at 0 817 -29500
ps2 f2571140: vdoll 3: deployed at 3125 28 32000
ps2 f2571205: report: {"t":1794270725579,"k":"round","i":1,"of":12,"s":480,"r":3,"m":2,"rn":1,"rof":2,"n":4,"pk":[{"ph":[1,2],"e":1,"s":480}]}
ps2 f2571208: vdoll: map n002a: 27050 faces, 709 polygons, built in 4296 ms (the bodies' mesh)
ps2 f2571505: report: {"t":1794270725579,"k":"team","a":"PLAYER ONE","tm":"red","pk":[{"tv":[5,0]}]}
ps2 f2571891: usbkb: READ #1287000 (frame 2571891) args 00 02 00 00
ps2 f2571908: vdoll 1: has the item
ps2 f2572396: net: send(9) 28 bytes peer 107.161.80.126:5732: tx55 4398 len 4 seq 991
ps2 f2572519: report: {"t":1794270750969,"k":"goal","tm":"red","a":"SaveMGO_DOLL_1","pk":[{"kr":0,"h":-1,"x":-1000,"y":1681,"z":-30500},{"s":"SaveMGO_DOLL_1","o":48,"v":2,"a":0,"b":1}]}
ps2 f2572806: vdoll 1: sees player 3 at 14.5 m
ps2 f2572812: vdoll 1: hits player 3 at 14.0 m (SVD)
ps2 f2572817: vdoll 2: dead, by player 2 (16 deaths)
ps2 f2572818: vdoll 1: player 3 down, 1 shots and 1 hits in this fight
ps2 f2573092: report: {"t":1794270762702,"k":"capleft","s":10,"tm":"red","pk":[{"cd":599}]}
ps2 f2573146: pace: 60 s: 2657 VBlanks (44.28 a second, 59.94 due), 668 late more than 4 ms, 458 more than a field (the latest 87.2 ms); the frame hook 0.03 ms a VBlank (the longest 0.3 ms); the process 66 % of a core, 105 MB (the most 120 MB) - SLOWED: the game's time lost, the processors busy (docs/DEDICATED_HOST.md, "Lag")
ps2 f2573208: vdoll 2: respawns at -2000 464 -31500""".split("\n")
CALLS = ("; calls before: 004755d8<0040ba9c 00476a28<00475630 00476968<00475648 00475a58<0040baa8 00475ab0<0040bba4 00475a58<0040bba4 "
         "0040ba50<0040bb74 004755d8<0040ba9c 00476a28<00475630 00476968<00475648 00475a58<0040baa8 00475ab0<0040bb94")
WILD = "ps2: hardware read outside the emulated registers at %08x (00000000)"
WILD2 = "ps2: hardware read %08x (00000000)"


def flood(first=0x1476ed50, count=3000, stride=0x10, inner=1500):
    """The flood's lines: `count` addresses a stride apart, the second line of each until the inner cap."""
    out = []
    for i in range(count):
        a = first + i * stride
        out.append(WILD % a + (CALLS if i < 8 else ""))
        if i < inner:
            out.append(WILD2 % a)
    return out


def flood_guard(root):
    """(v150-host beta 16) mgo1log's flood guard."""
    today = time.strftime("%Y-%m-%d", time.gmtime())
    d = os.path.join(root, "log", "bot2")
    after = ["ps2 f2573300: host watch: hosting (frame 2573300)", "CRASH: exception c0000005 at mgo1host.exe+1234 (reading 00000000)"]
    lines = BEFORE + flood() + after
    off = {"MGO1_CRASH_LINES": "0"}                            # (no crash report here: crash_reports and wild_lines below)
    r = run([LOGGER, "--dir", d], "\n".join(lines) + "\n", off)
    got = open(os.path.join(d, today + ".log")).read().splitlines()
    text = [g.split(" ", 2)[2] for g in got]
    own = [x for x in text if x.startswith("mgo1log: ")]
    kept = [x for x in text if not x.startswith("mgo1log: ")]
    check("flood: every line before it written, in its order", kept[:len(BEFORE)] == BEFORE, kept[:3])
    check("flood: the 8 lines with the calls before kept", len([x for x in kept if "calls before" in x]) == 8)
    n1 = [x for x in kept if x.startswith("ps2: hardware read outside") and "calls before" not in x]
    n2 = [x for x in kept if x.startswith("ps2: hardware read 1")]
    check("flood: 200 lines of each shape written as they are, the first ones", len(n1) == 200 and len(n2) == 200
          and n1 == [WILD % (0x1476ed50 + 0x10 * i) for i in range(8, 208)] and n2 == [WILD2 % (0x1476ed50 + 0x10 * i) for i in range(200)],
          (len(n1), len(n2)))
    s1 = [x for x in own if 'more lines like "ps2: hardware read outside the emulated registers at <n> (<n>)"' in x]
    s2 = [x for x in own if 'more lines like "ps2: hardware read <n> (<n>)"' in x]
    check("flood: one summary a shape with its count", len(s1) == 1 and len(s2) == 1 and s1[0].startswith("mgo1log: 2792 more lines like ")
          and s2[0].startswith("mgo1log: 1300 more lines like ") and s1[0].endswith("(not written one by one)"), own)
    check("flood: the summary gives the first and the last line's own values (the walk's range)",
          len(s1) == 1 and re.search(r"the first at \d\d:\d\d:\d\d\.\d{3} \(%08x 00000000\), the last at \d\d:\d\d:\d\d\.\d{3} \(%08x 00000000\)"
                                     % (0x1476ed50 + 0x10 * 208, 0x1476ed50 + 0x10 * 2999), s1[0]), s1)
    check("flood: the summaries are warnings", all(g.split(" ", 2)[1] == "W" for g in got if " mgo1log: " in g) and len(own) == 2, own)
    check("flood: the lines after it written, the error too", kept[-2:] == after and [g.split(" ", 2)[1] for g in got if "CRASH:" in g] == ["E"], got[-4:])
    it = iter(lines)
    check("flood: no line out of its order", all(any(x == y for y in it) for x in kept) and [g[:24] for g in got] == sorted(g[:24] for g in got))
    check("flood: the journal gets the same lines as the file (no flood there either)", r.stdout.splitlines() == text, len(r.stdout.splitlines()))
    sm = json.load(open(os.path.join(d, today + ".sum")))
    check("flood: the day's summary counts every line, those not written one by one too", sm.get("lines") == len(lines) + 2
          and sm.get("flood") == 2792 + 1300 and sm.get("E") == 1, sm)
    size = os.path.getsize(os.path.join(d, today + ".log"))
    check("flood: 4500 lines of flood: the day's file under 60 kB", size < 60000, size)

    # the errors are always written, whatever their number
    d2 = os.path.join(root, "log", "bot3")
    errs = ["CRASH: exception c0000005 at mgo1host.exe+%x (reading %08x)" % (0x1000 + i, i) for i in range(500)]
    r = run([LOGGER, "--dir", d2], "\n".join(errs) + "\n")
    got = open(os.path.join(d2, today + ".log")).read().splitlines()
    check("flood: 500 errors of one shape: every one written", len(got) == 500 and all(" E CRASH" in g for g in got) and len(r.stdout.splitlines()) == 500, len(got))
    # MGO1_LOG_BURST=0: no guard
    d3 = os.path.join(root, "log", "bot4")
    run([LOGGER, "--dir", d3, "--quiet"], "\n".join(flood(count=400, inner=0)) + "\n", dict(off, MGO1_LOG_BURST="0"))
    got = open(os.path.join(d3, today + ".log")).read().splitlines()
    check("flood: MGO1_LOG_BURST=0: the guard off, every line written", len(got) == 400 and "mgo1log" not in "".join(got), len(got))
    d4 = os.path.join(root, "log", "bot5")
    run([LOGGER, "--dir", d4, "--quiet"], "\n".join(flood(count=400, inner=0)) + "\n", dict(off, MGO1_LOG_BURST="50", MGO1_LOG_BURST_SECONDS="x"))
    got = open(os.path.join(d4, today + ".log")).read().splitlines()
    check("flood: MGO1_LOG_BURST=50 (a bad MGO1_LOG_BURST_SECONDS: 10): 8 + 50 lines, then the summary", len(got) == 59 and "342 more lines" in got[-1], got[-1:])

    # the time: the logger's own classes with a clock of the test's (the summaries every 10 s, the end, midnight, the cap)
    saved = dict(os.environ)
    try:
        m = load(LOGGER, "mgo1log_t")
        os.environ["MGO1_LOG_CAP_MB"] = "0.02"
        small = load(LOGGER, "mgo1log_small_t")              # (a cap of 20 kB a day)
    finally:
        os.environ.clear()
        os.environ.update(saved)

    class Out:
        def __init__(self):
            self.lines = []

        def write(self, s):
            self.lines.append(s.rstrip("\n"))

        def flush(self):
            pass

    def stream(name, m=m):
        out = Out()
        return m.Stream(m.DayLog(os.path.join(root, "log", name)), out, m.Guard(200, 10.0)), out

    def day(name, t):
        p = os.path.join(root, "log", name, time.strftime("%Y-%m-%d", time.gmtime(t)) + ".log")
        return [x.split(" ", 2) for x in open(p).read().splitlines()] if os.path.exists(p) else []

    midnight = (int(time.time()) // 86400 + 1) * 86400         # (the next one: both days' files are kept)
    t0 = midnight - 3600.0
    st, out = stream("doll7")
    st.line(b"doll f1: activity: hunting\n", t0)
    for i in range(3500):                                      # 100 lines a second for 35 s
        st.line(("ps2: hardware write outside the emulated registers at %08x (00000000)\n" % (0x108460ef + 0x10 * i)).encode(), t0 + 1 + i / 100.0)
    st.line(b"doll f2: activity: dead\n", t0 + 36.5)
    got = day("doll7", t0)
    sums = [g for g in got if g[2].startswith("mgo1log: ")]
    check("flood (clock): the first 200 lines, then a summary every 10 s while it lasts", len([g for g in got if "hardware write" in g[2] and not g[2].startswith("mgo1log: ")]) == 200
          and [g[2].split(" more")[0] for g in sums] == ["mgo1log: 1000", "mgo1log: 1000", "mgo1log: 1000"] and " in 10 s, " in sums[0][2], [g[2][:60] for g in sums])
    check("flood (clock): a summary is written before the line that follows it", [g[2] for g in got][-2:] == [sums[-1][2], "doll f2: activity: dead"]
          and got[-2][0] <= got[-1][0], got[-2:])
    st.line(b"doll f3: activity: hunting\n", t0 + 50.0)
    got = day("doll7", t0)
    check("flood (clock): its end said once the shape is quiet: the last lines' count, then written one by one again",
          [g[2][:22] for g in got[-3:]] == ["mgo1log: 300 more line", "mgo1log: no more lines", "doll f3: activity: hun"]
          and "(3300 not written one by one in " in got[-2][2] and got[-2][1] == "W", [g[2] for g in got[-3:]])
    st.line(b"ps2: hardware write outside the emulated registers at 20000000 (00000000)\n", t0 + 51.0)
    check("flood (clock): a line of that shape after its end is written as it is", day("doll7", t0)[-1][2].endswith("at 20000000 (00000000)"))
    check("flood (clock): the journal the same", out.lines == [g[2] for g in day("doll7", t0)], len(out.lines))
    # normal lines: 60 different lines a second for a minute, a few of each shape: nothing summarised
    st, out = stream("doll8")
    k = 0
    for sec in range(60):
        for j in range(60):
            st.line(("ps2 f%d: %s %d: step %d of %d\n" % (sec * 60 + j, ("vdoll", "cdbios", "net", "keys", "session", "usbkb")[j % 6], j, j, sec)).encode(), t0 + sec + j / 60.0)
            k += 1
    check("flood (clock): 3600 ordinary lines in a minute (6 shapes, 100 lines of each in 10 s): every one written",
          len(day("doll8", t0)) == k and len(out.lines) == k, len(day("doll8", t0)))
    # midnight: a flood from 23:59:50 to 00:00:25
    st, out = stream("doll9")
    for i in range(3500):
        st.line(("ps2: hardware read outside the emulated registers at %08x (00000000)\n" % (0x10886a50 + 0x10 * i)).encode(), midnight - 10 + i / 100.0)
    st.end(midnight + 25.0)
    a, b = day("doll9", midnight - 10), day("doll9", midnight + 1)
    check("flood (midnight): the first lines in the day they came, the summaries in the day they were written",
          len(a) == 200 and [g[2].split(" more")[0] for g in b] == ["mgo1log: 1000", "mgo1log: 1000", "mgo1log: 1000", "mgo1log: 300"]
          and b[0][0][11:19] == "00:00:02" and "the first at 23:59:52.000" in b[0][2], (len(a), [g[2][:40] for g in b]))
    sm = json.load(open(os.path.join(root, "log", "doll9", time.strftime("%Y-%m-%d", time.gmtime(midnight + 1)) + ".sum")))
    check("flood (midnight): the new day's summary counts them (warnings, as their lines are)", sm.get("flood") == 3300 and sm.get("lines") == 3304 and sm.get("W") == 3304, sm)
    # the cap (0.02 MB here): a flood after it is still summarised (its summaries are warnings)
    st, out = stream("doll10", small)
    for i in range(400):
        st.line(b"doll f%d: state: pos 114875 70463 -192000 life 185 stun 30000\n" % (i * 7919), t0 + i)
    capped = len(day("doll10", t0))
    for i in range(1000):                                      # (ordinary lines: the walk's second line of beta 15)
        st.line(("ps2: hardware read %08x (00000000)\n" % (0x10886a50 + 0x10 * i)).encode(), t0 + 500 + i / 100.0)
    st.end(t0 + 520.0)
    got = day("doll10", t0)
    check("flood (cap): past the day's cap the flood's summary is still written", capped < 400 and len(got) == capped + 1
          and got[-1][2].startswith("mgo1log: 800 more lines like") and len(out.lines) == 400 + 200 + 1, (capped, len(got), len(out.lines)))
    # (the review's P2-7) the server's clock set back an hour in a flood: the guard counts on a clock of its own that never goes
    # back, the lines' stamps stay the server's
    st, out = stream("doll12")
    for i in range(3000):                                      # 30 s of the guard's clock, the wall clock an hour back from the 5th
        st.line(b"ps2: hardware read %08x (00000000)\n" % (0x10886a50 + 0x10 * i), t0 + i / 100.0 - (3600 if i >= 500 else 0), 1000.0 + i / 100.0)
    sums = [x for x in out.lines if x.startswith("mgo1log: ")]
    check("flood (the clock set back an hour): a summary every 10 s all the same, in 10 s", [x.split(" more")[0] for x in sums] == ["mgo1log: 1000", "mgo1log: 1000"]
          and all(" in 10 s, " in x for x in sums), sums)
    # the table of shapes: 600 shapes flooding in turn, a few hundred kept
    st, out = stream("doll11")
    g = st.guard
    for s in range(600):
        for i in range(210):
            st.line(("ps2: shape %s: value %d\n" % ("".join(chr(ord("g") + int(c)) for c in "%04d" % s), i)).encode(), t0 + s + i / 1000.0)
    check("flood: the table of shapes is bounded (600 shapes seen)", len(g.shapes) <= 256 and len(g.seen) <= 1100 and len(g.floods) <= 256,
          (len(g.shapes), len(g.seen), len(g.floods)))
    st.end(t0 + 601)
    check("flood: each of them had its 200 lines and its summary", len([x for x in out.lines if not x.startswith("mgo1log: ")]) == 600 * 200
          and len([x for x in out.lines if x.startswith("mgo1log: 10 more lines like")]) == 600, len(out.lines))


def cap_kept(root):
    """(v150-host beta 16) Past the day's cap: the lines a post-mortem needs are still written, up to 4 times the cap."""
    today = time.strftime("%Y-%m-%d", time.gmtime())
    d = os.path.join(root, "log", "bot6")
    env = {"MGO1_LOG_CAP_MB": "0.008"}                          # 8388 bytes a day here, 33554 the hard cap
    fill = ["ps2 f%d: net: send(9) 28 bytes peer 107.161.80.126:5732: tx55 4398 len 4 seq %d" % (2478382 + 60 * i, i) for i in range(150)]
    dolls = ["doll f2520: activity: finding_game SaveMGO Host", "doll f2600: score: kills 1, deaths 0, stuns 0, CQC 0, headshots 1",
             "doll f2520: lobby: 1 games listed, ours at row 1", "ps2 f115: text: \"Checking memory card (PS2)...|\"",
             "ps2 f3767: session: the game 0 chosen holds 9 players at most: joined in the PCs' dialect",
             "ps2 f40: host watch: hosting (frame 40)", "step 3: \"Do you agree?|\" -> press 10 at frame 394 (4 frames) dg 348",
             "doll f23019330: state: pos 114875 70463 -192000 life 185 stun 30000 acc 0 down 0 slot 1",
             "ps2 f1087: cdbios: read of BGM_2.DAT sector 0x108fc: headless, no such archive, done without its callback"]
    run([LOGGER, "--dir", d, "--quiet"], "\n".join(fill + BEFORE + dolls) + "\n", env)
    path = os.path.join(d, today + ".log")
    got = [x.split(" ", 2) for x in open(path).read().splitlines()]
    text = [g[2] for g in got]
    said = [x for x in text if x.startswith("mgo1log: this day's file reached")]
    check("cap: said once, with what is still written", len(said) == 1 and "only the errors, the warnings and the lines a post-mortem needs" in said[0], said)
    after = text[text.index(said[0]) + 1:] if said else []
    kinds = ("vdoll", "players (", "report: {", "pace: ")
    want = [x for x in BEFORE if any(k in x for k in kinds)]
    check("cap: past it the virtual dolls', the players', the Game Report's lines and the pace are still written, in their order",
          [x for x in after if x in BEFORE] == want and len(want) == 25, (len(want), [x[:40] for x in after][:8]))
    check("cap: past it the technical lines (net, usbkb, lobby filter, cdbios, a doll's state) are not",
          not any(k in x for x in after for k in ("net: ", "usbkb: ", "lobby filter: ", "cdbios: ", "state: pos")), after)
    check("cap: a doll's activity, score and lobby lines, the screens, the sessions, the host watch, the log-in's steps still written",
          [x for x in after if x in dolls] == dolls[:7], [x[:30] for x in after if x in dolls])
    r = run([LOGGER, "--dir", d, "--note", "mgo1host: the game ended (a restart was asked); a new game"], env=env)
    r = run([LOGGER, "--dir", d, "--note", "mgo1doll-manager: doll 3: started for bot 1's game (1 real players, 8 seats)", "--quiet"], env=env)
    txt = open(path).read()
    check("cap: the launchers' and the manager's own lines still written past it", " I mgo1host: the game ended (a restart was asked)" in txt
          and " I mgo1doll-manager: doll 3: started" in txt, txt[-300:])
    # (v150-host beta 18) the new lines an inquiry into the two switches and the joiner's hold needs, past the cap: still
    # written, at level I (the daily view's PROBLEMS and ERRORS leave them out as they leave out every ordinary line)
    b18 = ["ps2 f73112: session: peer 192.0.2.7:5732 (P3) answers again after 6.4 s of silence, kept past the game's resend limit 3 times (loading)",
           "doll f9000: aim: direct: 1748 frames written, 52 frames of the view by the pad this minute (36 degrees a frame)",
           "doll f9100: aim: direct: the view's angles not written (no camera): the pad (camera 0 yaw 0 pitch 0, the turn 0, written 0 12 frames ago, state 0 0, weapon 0, 0x27F580 0)",
           "ps2 f2: vdoll: the bodies play as a player (MGO1_VDOLL_CONTROLS=player): the readied flag 4 to 9 frames, the readied turn 20 to 36 degrees a frame"]
    run([LOGGER, "--dir", d, "--quiet"], "\n".join(b18 + ["ps2 f73113: net: send(9) 28 bytes peer 192.0.2.7:5732: tx55 1 len 4 seq 1"]) + "\n", env)
    got18 = {g[2]: g[1] for g in (x.split(" ", 2) for x in open(path).read().splitlines())}
    check("cap: (beta 18) the joiner's hold, the direct aim's and the controls' lines still written past it, as I lines",
          all(got18.get(x) == "I" for x in b18) and "ps2 f73113: net: send(9) 28 bytes peer 192.0.2.7:5732: tx55 1 len 4 seq 1" not in got18,
          [(x[:40], got18.get(x)) for x in b18])
    # the hard cap: 4 times the cap, then the errors and warnings only (the guard off: these lines are alike)
    more = ["ps2 f%d: vdoll 2: respawns at -2000 464 %d" % (2573208 + i, -31500 - i) for i in range(600)]
    run([LOGGER, "--dir", d, "--quiet"], "\n".join(more + ["ps2 f2573900: vdoll 2: dead, by player 2 (17 deaths)", "doll f4: stuck at 1 2 3 going to 4 5 6",
                                                         "CRASH: exception c0000005 at mgo1host.exe+1234 (reading 00000000)"]) + "\n",
        dict(env, MGO1_LOG_BURST="0"))
    got = [x.split(" ", 2) for x in open(path).read().splitlines()]
    text = [g[2] for g in got]
    hard = [x for x in text if x.startswith("mgo1log: this day's file reached") and "only the errors and warnings from now on" in x]
    size = os.path.getsize(path)
    check("hard cap: said once at 4 times the cap", len(hard) == 1 and 33554 <= size < 33554 + 600, (len(hard), size))
    check("hard cap: past it a kept line is not written any more, a warning and an error are", "ps2 f2573900: vdoll 2: dead, by player 2 (17 deaths)" not in text
          and text[-2:] == ["doll f4: stuck at 1 2 3 going to 4 5 6", "CRASH: exception c0000005 at mgo1host.exe+1234 (reading 00000000)"]
          and 0 < len([x for x in text if "vdoll 2: respawns" in x]) < 600, text[-3:])
    run([LOGGER, "--dir", d, "--note", "mgo1host: bot 1, UDP port 5730", "--quiet"], env=env)
    run([LOGGER, "--dir", d, "--note", "mgo1host: the game crashed (its CRASH line tells where); it starts again", "--level", "E", "--quiet"], env=env)
    txt = open(path).read()
    check("hard cap: a new mgo1log on that file: the same (an ordinary note not written, an error written)", "UDP port 5730" not in txt
          and txt.rstrip().endswith("E mgo1host: the game crashed (its CRASH line tells where); it starts again"), txt[-200:])
    sm = json.load(open(os.path.join(d, today + ".sum")))
    check("cap: the day's summary counts every line, written or not", sm.get("lines") == 150 + len(BEFORE) + len(dolls) + 2 + len(b18) + 1 + 603 + 2 and sm.get("crashes") == 1, sm)


def vdoll_adapt_lines(root):
    """(v150-host beta 19) The virtual dolls' live level's lines (`vdoll: level: live ...`, `vdoll: levels now ...`, `VDOLL ADAPT:
    vdoll <n> ...`): ordinary lines (level I, whatever their reason code), all written under the cap and never summarised as
    a flood at the game's pace (15 dolls judged every 5 s); past the cap still written, a doll's VDOLL ADAPT line once a minute
    at most (the others counted in the day's summary)."""
    saved = dict(os.environ)
    try:
        os.environ["MGO1_LOG_CAP_MB"] = "0.06"
        small = load(LOGGER, "mgo1log_adapt_t")              # (a cap of 61 kB a day)
    finally:
        os.environ.clear()
        os.environ.update(saved)

    class Out:
        def __init__(self):
            self.lines = []

        def write(self, x):
            self.lines.append(x.rstrip("\n"))

        def flush(self):
            pass

    reasons = ("on_target", "low_confidence", "enemy_team_outperforming", "ally_team_dominating", "its_side_behind", "its_side_ahead",
               "enemy_stronger", "enemy_weaker")

    def adapt(n, i, f):
        return ("ps2 f%d: VDOLL ADAPT: vdoll %d old=%.2f new=%.2f target=%.2f base=10 enemy_strength=2.1 ally_strength=6.0 balance=-0.30 "
                "confidence=0.40 reason=%s (enemies 1 real, allies 0 real; tickets 20 against 30)" % (f, n, 10 - i * 0.05, 9.95 - i * 0.05, 7.5, reasons[i % 8]))

    folder = os.path.join(root, "log", "adapt8")
    t0 = (int(time.time()) // 86400) * 86400 + 600.0          # (today, 00:10 UTC)
    st = small.Stream(small.DayLog(folder), Out(), small.Guard(200, 10.0))
    lines = ["ps2 f10: vdoll: level: live, between 1 and 10 (the levels set 10,10,10)"]
    st.line((lines[0] + "\n").encode(), t0)
    k = 0
    for sec in range(0, 60, 5):                                # 15 virtual dolls judged every 5 s for a minute
        for n in range(1, 16):
            lines.append(adapt(n, k, 100 + k))
            st.line((lines[-1] + "\n").encode(), t0 + 1 + sec + n / 100.0)
            k += 1
        lines.append("ps2 f%d: vdoll: levels now %s (set 10)" % (200 + sec, " ".join("%.2f" % (9.5 - n * 0.1) for n in range(15))))
        st.line((lines[-1] + "\n").encode(), t0 + 1.5 + sec)
    path = os.path.join(folder, time.strftime("%Y-%m-%d", time.gmtime(t0)) + ".log")
    got = [x.split(" ", 2) for x in open(path).read().splitlines()]
    check("adapt lines: under the cap every one written, in order, at level I (every reason code), none summarised (15 dolls every 5 s)",
          [g[2] for g in got] == lines and all(g[1] == "I" for g in got), [(g[1], g[2][:50]) for g in got if g[1] != "I"][:3] or len(got))
    check("adapt lines: the day's file holds them all under the cap (here 61 kB)", os.path.getsize(path) < small.CAP and not st.log.capped, os.path.getsize(path))
    for i in range(500):                                       # past the cap
        st.line(("ps2 f%d: net: send(9) 28 bytes peer 192.0.2.7:5732: tx55 4398 len 4 seq %d\n" % (900 + i, i)).encode(), t0 + 70 + i / 20.0)
    check("adapt lines: the day's file past its cap", st.log.capped == 1, os.path.getsize(path))
    before = len(open(path).read().splitlines())
    t1 = t0 + 100.0
    sent = []
    for sec in range(0, 125, 5):                               # two minutes past the cap
        for n in (1, 2, 3):
            sent.append((t1 + sec + n / 10.0, adapt(n, sec, 5000 + sec * 10 + n)))
            st.line((sent[-1][1] + "\n").encode(), sent[-1][0])
        st.line(("ps2 f%d: vdoll: levels now 9.00 8.00 7.00 (set 10)\n" % (5000 + sec * 10)).encode(), t1 + sec + 0.5)
    st.line(b"ps2 f9000: vdoll: level: live, between 2 and 8 (the levels set 10,10,10)\n", t1 + 126)
    after = [x.split(" ", 2)[2] for x in open(path).read().splitlines()[before:]]
    kept = [x for x in after if "VDOLL ADAPT" in x]
    want = [x for t, x in sent if (t - t1) % 60 < 1]           # (each doll's at 0 s, 60 s and 120 s)
    check("adapt lines: past the cap a doll's VDOLL ADAPT line once a minute (3 dolls, 75 lines in 2 min: 9 written)", kept == want and len(kept) == 9,
          [x[:40] for x in kept])
    check("adapt lines: past the cap every `vdoll: levels now` and `vdoll: level: live` line written", len([x for x in after if "vdoll: levels now" in x]) == 25
          and after[-1].startswith("ps2 f9000: vdoll: level: live, between 2 and 8"), after[-2:])
    check("adapt lines: past the cap the net lines are not", not any("net: send" in x for x in after), after[:3])
    st.end(t1 + 130)
    sm = json.load(open(os.path.join(folder, time.strftime("%Y-%m-%d", time.gmtime(t0)) + ".sum")))
    check("adapt lines: the day's summary counts every line, written or not", sm.get("lines") == len(lines) + 500 + 75 + 25 + 1, sm)
    # a fresh mgo1log on that file (a game started again): still past the cap, each doll's first line written
    st2 = small.Stream(small.DayLog(folder), Out(), small.Guard(200, 10.0))
    st2.line((adapt(4, 1, 9100) + "\n").encode(), t1 + 140)
    st2.line((adapt(4, 2, 9101) + "\n").encode(), t1 + 145)
    st2.end(t1 + 150)
    tail = [x.split(" ", 2)[2] for x in open(path).read().splitlines()][-2:]
    check("adapt lines: a new stream on a capped file: doll 4's first line written, its next one within the minute not",
          tail[-1] == adapt(4, 1, 9100) and adapt(4, 2, 9101) not in tail, tail)
    # the real program, the line from the game's stream: the same levels
    d = os.path.join(root, "log", "adapt9")
    run([LOGGER, "--dir", d, "--quiet"], "\n".join(lines[:20]) + "\n")
    got = [x.split(" ", 2) for x in open(os.path.join(d, time.strftime("%Y-%m-%d", time.gmtime()) + ".log")).read().splitlines()]
    check("adapt lines: through mgo1log itself: written as they came, level I", [g[2] for g in got] == lines[:20] and all(g[1] == "I" for g in got), got[:2])


STATUS = {"v": 1, "frame": 2573208, "state": "hosting", "lobby": 1, "game": {"name": "SaveMGO Host", "rules": [[0, 7], [3, 2], [1, 3]], "round": 1,
                                                                         "max": 9, "age": 1200},
          "players": [{"p": 1, "name": "HOSTACC", "host": 1, "kind": "host", "doll": 0, "vdoll": 0, "team": -1, "ping": -1, "k": 0, "d": 0},
                      {"p": 2, "name": "SaveMGO_DOLL_1", "host": 0, "kind": "doll", "doll": 0, "vdoll": 1, "team": 0, "ping": 0, "k": 12, "d": 3},
                      {"p": 3, "name": "SaveMGO_DOLL_2", "host": 0, "kind": "doll", "doll": 0, "vdoll": 2, "team": 1, "ping": 0, "k": 2, "d": 16},
                      {"p": 4, "name": "DOLLACC", "host": 0, "kind": "doll", "doll": 0, "vdoll": 0, "team": 0, "ping": 30, "k": 1, "d": 1},
                      {"p": 5, "name": "PLAYER ONE", "host": 0, "kind": "real", "doll": 0, "vdoll": 0, "team": 0, "ping": 80, "k": 4, "d": 2}],
          "players_real": 1, "players_dolls": 3, "players_host": 1, "vdolls_set": 3, "pace": {"n": 720, "rate": 44.28, "due": 59.94, "cpu": 66}}


def crash_reports(root):
    """(v150-host beta 16) mgo1log's crash reports."""
    today = time.strftime("%Y-%m-%d", time.gmtime())
    d = os.path.join(root, "log", "bot7")
    run_dir = os.path.join(root, "run7")
    os.makedirs(run_dir)
    exit_file, status, version = os.path.join(run_dir, "exit"), os.path.join(run_dir, "status.json"), os.path.join(run_dir, "VERSION")
    json.dump(STATUS, open(status, "w"))
    open(version, "w").write("build 1.00 0123456789ab\n")
    env = {"MGO1_CRASH_EXIT": exit_file, "MGO1_CRASH_STATUS": status, "MGO1_CRASH_VERSION": version, "MGO1_CRASH_ENDS": "2=lost 6=lost 3="}

    def reports(folder=d):
        try:
            return sorted(x for x in os.listdir(os.path.join(folder, "crash")) if x.endswith(".txt"))
        except OSError:
            return []

    def end(code, lines, folder=d, extra=None):
        if code is None:
            if os.path.exists(exit_file):
                os.remove(exit_file)
        else:
            open(exit_file, "w").write("%d\n" % code)
        return run([LOGGER, "--dir", folder], "\n".join(lines) + "\n", dict(env, **(extra or {})))

    crash = "CRASH: exception c0000005 at mgo1host.exe+1234 (reading 00000000)"
    lines = BEFORE + flood() + [crash]
    r = end(70, lines)
    names = reports()
    check("crash: a crash (70): one report, <UTC time>_crash70.txt", len(names) == 1 and re.match(r"^%s_\d{6}_crash70\.txt$" % today, names[0]), names)
    rep = open(os.path.join(d, "crash", names[0])).read() if names else ""
    head, _, body = rep.partition("\n----\n")
    h = dict(x.split(": ", 1) for x in head.splitlines()[1:] if ": " in x and not x.startswith("player: "))
    check("crash: its header: the source, the reason, what ended, when, the version file's text", h.get("source") == "bot7" and h.get("reason") == "crash70"
          and "the game crashed (exit code 70)" in h.get("what", "") and h.get("when", "").startswith(today + "T") and h.get("version") == "build 1.00 0123456789ab", head)
    check("crash: the game's last status: its state, game, round, players and pace", h.get("state") == "hosting" and h.get("game") == "SaveMGO Host"
          and h.get("round") == "2 of 3 (rule 3, map 2)" and h.get("players") == "5 (1 real, 3 dolls, 1 host)" and h.get("frame") == "2573208"
          and h.get("pace") == "44.28 VBlanks a second (59.94 due), 66 % of a core" and "status" in h, head)
    players = [x for x in head.splitlines() if x.startswith("player: ")]
    check("crash: the players with their kinds: the dolls by their numbers, a real player by his name, no name of the host's nor of a doll's account",
          players == ["player: P1 host", "player: P2 virtual doll 1, team red, 12 kills, 3 deaths", "player: P3 virtual doll 2, team blue, 2 kills, 16 deaths",
                      "player: P4 doll (an account), team red, 1 kills, 1 deaths, ping 30 ms", 'player: P5 real "PLAYER ONE", team red, 4 kills, 2 deaths, ping 80 ms']
          and "HOSTACC" not in rep and "DOLLACC" not in rep, players)
    blines = [x.split(" ", 2) for x in body.splitlines()]
    btext = [x[2] for x in blines if len(x) == 3]
    check("crash: then the game's last lines as the daily log has them (time, level, text), in their order",
          btext[:len(BEFORE)] == BEFORE and btext[-3:-2] == [crash] and all(x.startswith("mgo1log: ") for x in btext[-2:])
          and all(len(x) == 3 and x[0].endswith("Z") and x[1] in "EWI" for x in blines), btext[-3:])
    check("crash: the flood summarised in it as in the daily log", len([x for x in btext if "hardware read" in x and not x.startswith("mgo1log")]) == 408
          and len([x for x in btext if x.startswith("mgo1log: ") and "more lines like" in x]) == 2, len(btext))
    day = open(os.path.join(d, today + ".log")).read()
    check("crash: the daily log and the journal name the report", (" W mgo1log: crash report crash/%s (crash70: " % names[0]) in day
          and ("mgo1log: crash report crash/%s" % names[0]) in r.stdout, day[-300:])
    check("crash: the report holds nothing the daily log does not", all(x in day for x in btext if not x.startswith("mgo1log: crash report")), 0)
    # the ends that are no failure: no report; the others by their reason
    for code, ends in ((0, 0), (3, 0), (None, 0), (2, 1), (6, 1), (137, 1), (5, 1)):
        before = reports()
        end(code, ["ps2 f%s: host watch: hosting (frame 40)" % code, "ps2 f41: text: \"Connecting to the server...|\""])
        new = [x for x in reports() if x not in before]
        why = {2: "lost", 6: "lost", 137: "signal9", 5: "exit5"}.get(code)
        check("crash: exit code %s: %s" % (code, "a report (%s)" % why if ends else "no report"), len(new) == ends and (not ends or new[0].endswith("_%s.txt" % why)
              or re.search(r"_%s_\d\.txt$" % why, new[0])), new)
    rep = open(os.path.join(d, "crash", [x for x in reports() if "_signal9" in x][0])).read()
    check("crash: a kill's words", "what: the game was ended by signal 9 (killed: out of memory, or forced)" in rep, rep[:300])
    # 6 reports an hour at most (a loop of crashes must not push the older reports out)
    n0 = len(reports())
    for i in range(3):
        r = end(70, ["ps2 f1: host watch: hosting (frame %d)" % i])
    check("crash: the 7th of an hour not written, said in the daily log", n0 == 5 and len(reports()) == 6
          and "mgo1log: no crash report for this end (crash70): 6 written in the last hour" in r.stdout, (n0, len(reports()), r.stdout[-200:]))
    # the lines the cap keeps out of the day's file are in the report; 4000 lines, 2 MB at most
    d2 = os.path.join(root, "log", "bot8")
    many = ["ps2 f%d: %s: step %d of the %s" % (i, "ghijklmnop"[i % 10] * (1 + i % 7), i, "qrstuvwyz"[i % 9] * (1 + i % 11)) for i in range(6000)]
    end(70, many + [crash], d2, {"MGO1_LOG_CAP_MB": "0.01"})
    rep = open(os.path.join(d2, "crash", reports(d2)[0])).read()
    body = rep.partition("\n----\n")[2].splitlines()
    day = open(os.path.join(d2, today + ".log")).read().splitlines()
    check("crash: the last 4000 lines, those the day's cap kept out of its file too", len(body) == 4000 and body[-1].endswith(crash)
          and body[0].endswith(many[6000 - 3999]) and len(day) < 400, (len(body), len(day)))
    d3 = os.path.join(root, "log", "bot9")
    end(70, ["ps2 f%d: text: \"%s\"" % (i, "ghijklmnop"[i % 10] * 900) for i in range(3000)] + [crash], d3, {"MGO1_CRASH_KB": "200", "MGO1_LOG_BURST": "0"})
    path = os.path.join(d3, "crash", reports(d3)[0])
    rep = open(path).read()
    check("crash: its size bounded (MGO1_CRASH_KB), the newest lines kept", 150000 < os.path.getsize(path) <= 200 * 1024 and rep.rstrip().endswith(crash), os.path.getsize(path))
    os.remove(status)
    end(70, [crash], d3)
    rep = open(os.path.join(d3, "crash", reports(d3)[-1])).read()
    check("crash: no status file: said, the report written all the same", "status: not readable" in rep and crash in rep, rep[:300])
    json.dump(STATUS, open(status, "w"))
    # 40 files and 31 days at most
    d4 = os.path.join(root, "log", "bot10")
    os.makedirs(os.path.join(d4, "crash"))
    old = time.strftime("%Y-%m-%d", time.gmtime(time.time() - 40 * 86400))
    recent = [time.strftime("%Y-%m-%d_%H%M%S", time.gmtime(time.time() - 7200 - 600 * i)) + "_hung.txt" for i in range(44)]
    for n in [old + "_101500_crash70.txt"] + recent:
        open(os.path.join(d4, "crash", n), "w").write("x\n")
    open(os.path.join(d4, "crash", "notes.txt"), "w").write("the owner's own file\n")
    end(70, [crash], d4)
    left = reports(d4)
    check("crash: 40 reports a folder at most (the oldest go), none older than 31 days, another file left alone",
          len(left) == 41 and "notes.txt" in left and old + "_101500_crash70.txt" not in left and sorted(recent)[-39:] == [x for x in left if x in recent]
          and len([x for x in left if "_crash70" in x]) == 1, left[:3])

    # a stop by its service (SIGTERM): no report, the flood's count written; with the doll manager's request (hung): a report
    def stopped(folder, request, lines=None):
        if os.path.exists(exit_file):                          # (a launcher's run folder is new at each start: no exit code yet)
            os.remove(exit_file)
        p = subprocess.Popen([sys.executable, LOGGER, "--dir", folder], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=dict(os.environ, **env))
        # (a flood of ordinary lines; one of accesses outside the memory leaves a report at a stop too: wild_lines)
        p.stdin.write(("\n".join(lines or BEFORE + ["ps2 f%d: usbkb: READ #%d (frame %d) args 00 02 00 00" % (i, 600 * i, i) for i in range(993)]) + "\n").encode())
        p.stdin.flush()
        deadline = time.time() + 20
        while time.time() < deadline and "usbkb: READ #118800 (frame 198)" not in (open(os.path.join(folder, today + ".log")).read() if os.path.exists(os.path.join(folder, today + ".log")) else ""):
            time.sleep(0.05)                                   # (its first 200 lines of the flood are written: it has read them all soon)
        time.sleep(0.5)
        if request:
            os.makedirs(os.path.join(folder, "crash"), exist_ok=True)
            json.dump(request, open(os.path.join(folder, "crash", ".hung"), "w"))
        p.send_signal(signal.SIGTERM)
        try:
            out, _ = p.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
            out += b"NOT ENDED"
        return p.returncode, out.decode()

    d5 = os.path.join(root, "log", "bot11")
    rc, out = stopped(d5, None)
    day = open(os.path.join(d5, today + ".log")).read()
    check("stop: its service's stop ends it (0) with no report, the flood's last count written", rc == 0 and "NOT ENDED" not in out and reports(d5) == []
          and "mgo1log: 794 more lines like" in day and "crash report" not in day, (rc, out[-200:]))
    d6 = os.path.join(root, "log", "bot12")
    name = today + "_005015_hung.txt"
    rc, out = stopped(d6, {"name": name, "what": "its game wrote nothing for 183 s (hung): restarted by the doll manager", "t": time.time()})
    rep = open(os.path.join(d6, "crash", name)).read() if os.path.exists(os.path.join(d6, "crash", name)) else ""
    check("hung: the doll manager's request, then the stop: the report under the name it gave, its words, the game's last lines",
          rc == 0 and reports(d6) == [name] and "reason: hung" in rep and "what: its game wrote nothing for 183 s (hung): restarted by the doll manager" in rep
          and BEFORE[-1] in rep and "mgo1log: 794 more lines like" in rep and "state: hosting" in rep, (rc, reports(d6), rep[:200]))
    check("hung: the request taken (removed), the daily log names the report", not os.path.exists(os.path.join(d6, "crash", ".hung"))
          and ("mgo1log: crash report crash/%s (hung: " % name) in open(os.path.join(d6, today + ".log")).read())
    d7 = os.path.join(root, "log", "bot16")
    rc, out = stopped(d7, None, BEFORE + flood(count=8, inner=0) + ["ps2 f%d: usbkb: READ #%d (frame %d) args 00 02 00 00" % (i, 600 * i, i) for i in range(200)])
    rep = open(os.path.join(d7, "crash", reports(d7)[0])).read() if reports(d7) else ""
    check("stop: after an access outside the memory: a report (wild) with the lines before it", rc == 0 and len(reports(d7)) == 1 and reports(d7)[0].endswith("_wild.txt")
          and "what: the game was stopped by its service after its code went outside its memory" in rep and BEFORE[0] in rep and "wild: " in rep, (rc, reports(d7)))
    os.makedirs(os.path.join(root, "log", "bot13", "crash"))
    stale = os.path.join(root, "log", "bot13", "crash", ".hung")
    json.dump({"name": name, "what": "old"}, open(stale, "w"))
    os.utime(stale, (time.time() - 600, time.time() - 600))
    rc, out = stopped(os.path.join(root, "log", "bot13"), None)
    check("hung: a request 10 minutes old is no request (a stop: no report)", rc == 0 and reports(os.path.join(root, "log", "bot13")) == [], reports(os.path.join(root, "log", "bot13")))


def export_filter(root, aenv):
    """(v150-host beta 16) mgo1host-agent logs get on a day's file with more lines than an export takes (MGO1 Host's SAVE: the last
    100000 lines of the view; 3000 here): the file of 10 November as beta 15 wrote it, the flood line by line."""
    day = "2026-11-10"
    d = os.path.join(aenv["MGO1_LOG_DIR"], "doll40")
    os.makedirs(d, exist_ok=True)
    out, t = [], 44.957

    def at(sec):
        return "%sT00:%02d:%06.3fZ" % (day, 32 + int(sec // 60), sec % 60)
    for i, x in enumerate(BEFORE):
        out.append("%s %s %s" % (at(i * 0.5), "W" if "SLOWED" in x else "I", x))
    fl = flood(count=30000, inner=20000)                        # 50000 lines in 10 s, across two of the clock's 10 s
    for i, x in enumerate(fl):
        out.append("%s I %s" % (at(t + i / 5000.0), x))
    out.append("%s W mgo1log: this day's file reached 64 MB: only the errors and warnings from now on" % at(t + 10.1))
    out.append("%s W mgo1doll-manager: bot2: its game wrote nothing for 182 s (hung): started again" % at(t + 150))
    out.append("%s I a line with HOSTACC and topsecret in it" % at(t + 151))
    open(os.path.join(d, day + ".log"), "w").write("\n".join(out) + "\n")
    env = dict(aenv, MGO1_LOG_VIEW_LINES="3000")

    def get(*args):
        r = run([AGENT, "logs", "get", "doll40", day] + list(args), env=env)
        return json.loads(r.stdout or "{}")
    j = get("--tail", "2000")
    total = j.get("total", 0)
    check("view: a day of 50033 lines: its view fits an export (the flood's lines left out)", j.get("ok") and 400 < total <= 3000 and j.get("file_lines") == len(out)
          and j.get("flood", 0) > 48000 and j.get("cut") == 0, {k: j.get(k) for k in ("total", "file_lines", "flood", "cut")})
    # MGO1 Host's SAVE: the last lines of the view, 5000 a call
    view, start = [], max(0, total - 100000)
    while True:
        k = get("--lines", "5000", "--from", str(start))
        view += k.get("lines", [])
        if k.get("next", -1) < 0 or not k.get("lines"):
            break
        start = k["next"]
    text = [x.split(" ", 2)[2] for x in view]
    check("view: the export's first line says how many lines the day holds and how many are left out", len(view) == total
          and re.match(r"^mgo1host-agent: this day's file holds 50033 lines: this view leaves out %d of them, the lines of floods "
                       r"\(summarised below: \"more lines like\"\); logs get \.\.\. --raw: the file as it is$" % j.get("flood", -1), text[0]), text[:1])
    check("view: every line before the flood is there, in its order, and the lines after it", [x for x in text if x in BEFORE] == BEFORE
          and "mgo1doll-manager: bot2: its game wrote nothing for 182 s (hung): started again" in text and text[-1] == "a line with *** and *** in it", text[-2:])
    sums = [x for x in text if re.match(r"^mgo1host-agent: \d+ more lines like ", x)]
    kept = [x for x in text if x.startswith("ps2: hardware read")]
    check("view: the flood: its first lines, then its counts with the first and the last line's own values", len(kept) + sum(int(x.split()[1]) for x in sums) == len(fl)
          and 400 <= len(kept) <= 900 and any('more lines like "ps2: hardware read outside the emulated registers at <n> (<n>)", the first at 00:32:4' in x for x in sums)
          and sums[-1].endswith("(%08x 00000000) (not shown one by one)" % (0x1476ed50 + 0x10 * 29999)), (len(kept), sums[-1:]))
    check("view: the lines' times in order", [x[:24] for x in view[1:]] == sorted(x[:24] for x in view[1:]))
    check("view: no account or password", "HOSTACC" not in "".join(view) and "TOPSECRET" not in "".join(view).upper())
    k = get("--raw", "--tail", "3")
    check("view: --raw: the file as it is", k.get("total") == len(out) and k.get("lines", [""])[0] == out[-3] and "flood" not in k, k.get("total"))
    k = get("--level", "EW")
    check("view: the levels' filter as before (the day's 3 warnings: the flood's lines are ordinary ones, none shown nor counted)", k.get("ok") and k.get("total") == 3
          and all(" W " in x for x in k.get("lines", [])) and any("this day's file reached 64 MB" in x for x in k["lines"]) and k.get("flood") == 0, k.get("lines", [])[:2])
    # a day with more ordinary lines than the limit: the oldest are cut, said
    d2 = os.path.join(aenv["MGO1_LOG_DIR"], "doll41")
    os.makedirs(d2, exist_ok=True)
    many = ["%s I ps2 f%d: %s: step %d of the %s" % (at(i * 0.5), i, "ghijklmnop"[i % 10] * (1 + i % 7), i, "qrstuvwyz"[i % 9] * (1 + i % 11)) for i in range(7000)]
    open(os.path.join(d2, day + ".log"), "w").write("\n".join(many) + "\n")
    r = run([AGENT, "logs", "get", "doll41", day, "--tail", "5"], env=env)
    k = json.loads(r.stdout or "{}")
    r = run([AGENT, "logs", "get", "doll41", day, "--lines", "2", "--from", "0"], env=env)
    h = json.loads(r.stdout or "{}")
    check("view: 7000 ordinary lines, 3000 a view: the oldest cut by whole blocks, the newest kept", k.get("ok") and 2700 < k.get("total", 0) <= 3000
          and k.get("cut") == 7000 - (k["total"] - 1) and k["cut"] % 300 == 0 and k.get("lines", [""])[-1] == many[-1], {x: k.get(x) for x in ("total", "cut", "flood")})
    check("view: the first line says the cut", h.get("lines") and ("this view leaves out its %d oldest lines (it shows %d at most)" % (k.get("cut", -1), 3000)) in h["lines"][0]
          and h["lines"][1] == many[k.get("cut", 0)], h.get("lines"))
    r = run([AGENT, "logs", "get", "doll41", day, "--tail", "5"], env=aenv)
    k = json.loads(r.stdout or "{}")
    check("view: a day under the limit (100000 lines): as before", k.get("total") == 7000 and "cut" not in k and k.get("lines", [""])[-1] == many[-1], k.get("total"))


def wild_lines(root):
    """(v150-host beta 16) The game's own guard (MGO1Loader/wildguard.h): its lines' levels, the crash report of a walk that never
    ends (exit code 70), the 200 lines before the first `ps2: hardware` line kept in it."""
    today = time.strftime("%Y-%m-%d", time.gmtime())
    first8 = [WILD % (0x1476ed50 + 4 * i) + CALLS for i in range(8)]
    rest = [WILD % (0x1476ed50 + 4 * i) for i in range(8, 63)]
    last = [WILD % (0x1476ed50 + 4 * 63) + ": 64 accesses outside the memory and the registers said one by one, the next ones a line every 10 s"]
    more = ("ps2: 131072 more accesses outside the memory and the registers in 10 s (from 10886a50 to 108c5d40, the last a read at 108c5d40; 131136 reads and 0 writes "
            "since the start): the PS2 code follows a wild pointer")
    walk = ["CRASH: the PS2 code walks outside its memory and never ends: 65536 accesses in a row with no call between them, from 10886a50 to 108c5d40 (the last a read; "
            "131136 reads and 0 writes in all); last call 00475a58 from 0040bba4, last loop in 0040bac0; calls before: 004755d8<0040ba9c 00476a28<00475630",
            "CRASH: the loop's registers: r 0 1476ed50 2 3 4 5 6 7 8 9 a b c d e f 10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e",
            "CRASH: the game ends here (70): its service starts it again"]
    m = load(LOGGER, "mgo1log_w")
    check("wild: the game's lines of an access outside its memory are warnings, a walk's CRASH lines and an endless loop errors",
          [m.level_of(x) for x in (first8[0], rest[0], last[0], more, walk[0], walk[1], walk[2], "ps2 f100: endless loop in 0040bac0: 1000000 passes")]
          == ["W", "W", "W", "W", "E", "E", "E", "E"] and m.level_of(WILD2 % 0x1476ed50) == "I" and m.level_of(BEFORE[0]) == "I")
    d = os.path.join(root, "log", "bot14")
    run_dir = os.path.join(root, "run14")
    os.makedirs(run_dir)
    exit_file = os.path.join(run_dir, "exit")
    env = {"MGO1_CRASH_EXIT": exit_file, "MGO1_CRASH_ENDS": "2=lost 6=lost 3=", "MGO1_CRASH_HOURLY": "0"}

    def words(n, salt):                                        # ordinary lines, no two alike in their shape more than a few times
        return ["ps2 f%d: %s: step %d of the %s" % (salt + i, "ghijklmnop"[i % 10] * (1 + i % 7), i, "qrstuvwyz"[i % 9] * (1 + i % 11)) for i in range(n)]

    def game(code, lines, folder=d, extra=None):
        open(exit_file, "w").write("%d\n" % code)
        return run([LOGGER, "--dir", folder], "\n".join(lines) + "\n", dict(env, **(extra or {})))

    def newest(folder=d):
        names = sorted(x for x in os.listdir(os.path.join(folder, "crash")) if x.endswith(".txt")) if os.path.isdir(os.path.join(folder, "crash")) else []
        return names, (open(os.path.join(folder, "crash", names[-1])).read() if names else "")

    # (the game's ordinary lines of its hardware registers, at its start: `ps2: hardware read 10001810 (00000000)`: no such access)
    boot = ["ps2: hardware read %08x (00000000)" % (0x10001810 + 0x10 * i) for i in range(20)] + ["ps2: hardware write 1000f590 (00000000)"]
    before = boot + words(300, 1000) + BEFORE
    lines = before + first8 + rest + last + [more] * 3 + walk
    r = game(70, lines)
    day = [x.split(" ", 2) for x in open(os.path.join(d, today + ".log")).read().splitlines()]
    check("wild: the game's 64 lines, its 10 s lines and its CRASH lines all written (no flood: no summary of mgo1log's)", [x[2] for x in day] == lines + [day[-1][2]]
          and day[-1][2].startswith("mgo1log: crash report crash/") and [x[1] for x in day[-4:-1]] == ["E", "E", "E"], [x[2][:50] for x in day[-5:]])
    names, rep = newest()
    head, _, body = rep.partition("\n----\n")
    check("wild: the walk's end (70): a crash70 report whose header gives the first access outside the memory", len(names) == 1 and names[0].endswith("_crash70.txt")
          and re.search(r"^wild: \d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z ps2: hardware read outside the emulated registers at 1476ed50 \(00000000\); calls before: 004755d8<0040ba9c",
                        head, re.M), head)
    btext = [x.split(" ", 2)[2] for x in body.splitlines()]
    check("wild: a short game: every line once in the report, in its order", btext == lines, (len(btext), len(lines)))
    # 6000 lines between the first access and the end: the report still has the 200 lines before it and its first lines
    time.sleep(1.1)
    filler = words(6000, 500000)
    lines = before + first8 + rest + last + filler + walk
    game(70, lines)
    names, rep = newest()
    head, _, body = rep.partition("\n----\n")
    btext = [x.split(" ", 2)[2] for x in body.splitlines()]
    cut = [i for i, x in enumerate(btext) if x.startswith("mgo1log: ") and "lines the game wrote here are not in this report" in x]
    check("wild: 6000 lines after the first access: the 200 lines before it and the 100 from it are kept at the report's head", len(cut) == 1
          and btext[:cut[0]] == (before + first8 + rest + last + filler)[len(before) - 200:len(before) + 100], (cut, len(btext)))
    check("wild: then the lines not kept are counted, then the game's last 4000 lines", cut and re.search(r"mgo1log: \d+ lines the game wrote here", btext[cut[0]])
          and btext[cut[0] + 1:] == lines[-4000:] and ("lines: 4300, " in head or "lines: 4301, " in head) and int(btext[cut[0]].split()[1]) == len(lines) - 4000 - (len(before) + 100),
          (btext[cut[0]] if cut else "", head[-200:]))
    check("wild: the header's line of the first access", "wild: " in head and "at 1476ed50 (00000000); calls before:" in head)
    # a game that ends without a failure after such an access (a restart asked): a report all the same; without one: none
    time.sleep(1.1)
    n0 = len(newest()[0])
    game(0, before + first8 + ["ps2 f9000: host watch: hosting (frame 9000)"])
    names, rep = newest()
    check("wild: a game that ends with 0 after an access outside its memory: a report (wild) with the 200 lines before it", len(names) == n0 + 1
          and names[-1].endswith("_wild.txt") and "reason: wild" in rep and "what: the game ended with no failure said (exit code 0) after its code went outside its memory" in rep
          and before[-200] in rep and before[-1] in rep and first8[0] in rep, (names[-1:], rep[:300]))
    game(0, before + ["ps2 f9000: host watch: hosting (frame 9000)"])
    check("wild: no such access (its registers' ordinary lines only), an end with 0: no report", len(newest()[0]) == n0 + 1)
    # past the day's cap the game's lines of an access outside its memory are still written (warnings)
    d2 = os.path.join(root, "log", "bot15")
    game(70, words(300, 1000) + first8 + rest + last + [more] + walk, d2, {"MGO1_LOG_CAP_MB": "0.004"})
    txt = open(os.path.join(d2, today + ".log")).read()
    check("wild: past the day's cap its lines are still written", all((" W " + x) in txt for x in first8 + rest + last + [more]) and all((" E " + x) in txt for x in walk), txt[-300:])


def summary_shared(root):
    """(the review's P2-8) A day's .sum is written by the game's stream and by the notes' own processes (the launcher's lines, the
    manager's into a bot's log) at the same time: none loses the other's counts."""
    today = time.strftime("%Y-%m-%d", time.gmtime())
    d = os.path.join(root, "log", "bot17")
    p = subprocess.Popen([sys.executable, LOGGER, "--dir", d, "--quiet"], stdin=subprocess.PIPE, env=dict(os.environ, MGO1_CRASH_LINES="0"))
    p.stdin.write(b"ps2 f1: host watch: hosting (frame 1)\nps2 f2: text: \"a\"\nCRASH: exception c0000005 at mgo1host.exe+1234 (reading 00000000)\n")
    p.stdin.flush()
    path = os.path.join(d, today + ".log")
    deadline = time.time() + 20
    while time.time() < deadline and "CRASH" not in (open(path).read() if os.path.exists(path) else ""):
        time.sleep(0.05)
    for i in range(5):                                         # the manager's warnings into this bot's log while its game runs
        run([LOGGER, "--dir", d, "--note", "mgo1doll-manager: pace guard: bot 17's game below its pace (%d)" % i, "--level", "W", "--quiet"])
    p.stdin.write(b"ps2 f3: host watch: hosting (frame 3)\ndoll f4: stuck at 1 2 3 going to 4 5 6\n")
    p.stdin.close()
    p.wait(timeout=20)
    sm = json.load(open(os.path.join(d, today + ".sum")))
    n = len(open(path).read().splitlines())
    check("summary: a stream and 5 notes at once: the day's summary counts every line of the file", n == 10 and sm.get("lines") == 10 and sm.get("W") == 6
          and sm.get("E") == 1 and sm.get("first", "") <= sm.get("last", "") and sm.get("first"), sm)
    check("summary: no temporary file left", [x for x in os.listdir(d) if x.endswith(".tmp")] == [], os.listdir(d))


def total_cap(root):
    """(the review's P2-12) The logs' total size: a source's days past MGO1_LOG_SOURCE_MB and all the sources' past MGO1_LOG_TOTAL_MB
    lose their oldest days first, never today's file, never a crash report."""
    logs = os.path.join(root, "biglog")
    today = time.strftime("%Y-%m-%d", time.gmtime())
    days = [time.strftime("%Y-%m-%d", time.gmtime(time.time() - 86400 * k)) for k in range(1, 6)]        # yesterday first
    for src in ("bot1", "doll1"):
        os.makedirs(os.path.join(logs, src, "crash"))
        for dday in days:
            open(os.path.join(logs, src, dday + ".log"), "w").write("x" * 1048576)
            open(os.path.join(logs, src, dday + ".sum"), "w").write("{}")
        open(os.path.join(logs, src, "crash", days[4] + "_101500_crash70.txt"), "w").write("x" * 1000)
    open(os.path.join(logs, "bot1", today + ".log"), "w").write("y" * (2 * 1048576))
    env = {"MGO1_LOG_SOURCE_MB": "3", "MGO1_LOG_TOTAL_MB": "7"}
    r = run([LOGGER, "--dir", os.path.join(logs, "bot1"), "--quiet"], "ps2 f1: host watch: hosting (frame 1)\n", env)

    def left(src):
        return sorted(x[:10] for x in os.listdir(os.path.join(logs, src)) if x.endswith(".log"))
    # bot1 (the stream's own source): today 2 MB and 5 days of 1 MB, 3 MB a source: today and yesterday; then all the sources:
    # 3 + 5 = 8 MB of 7: the oldest day of all, doll1's, goes
    check("total: a source past its size loses its oldest days, never today's; then the oldest day of all the sources", left("bot1") == [days[0], today]
          and left("doll1") == sorted(days[:4]), (left("bot1"), left("doll1")))
    check("total: a day's .sum goes with it, a crash report stays", sorted(x for x in os.listdir(os.path.join(logs, "doll1")) if x.endswith(".sum")) == sorted(d + ".sum" for d in days[:4])
          and os.listdir(os.path.join(logs, "bot1", "crash")) == [days[4] + "_101500_crash70.txt"])
    txt = open(os.path.join(logs, "bot1", today + ".log")).read()[2 * 1048576:]
    check("total: said in the day's log", " W mgo1log: the logs took more than 3 MB a source or 7 MB in all: 5 of the oldest days removed (5 MB)" in txt, txt[:300])
    r = run([LOGGER, "--dir", os.path.join(logs, "doll1"), "--quiet"], "doll f1: activity: hunting\n", dict(env, MGO1_LOG_TOTAL_MB="5"))
    check("total: doll1's own 3 MB, then 5 MB in all: the oldest days of all go first, today's and yesterday's left", left("doll1") == sorted([days[0], today])
          and left("bot1") == [days[0], today], (left("bot1"), left("doll1")))
    for dday in days[1:]:
        open(os.path.join(logs, "doll1", dday + ".log"), "w").write("x" * 1048576)
    r = run([LOGGER, "--dir", os.path.join(logs, "doll1"), "--quiet"], "doll f1: activity: hunting\n", {"MGO1_LOG_SOURCE_MB": "0", "MGO1_LOG_TOTAL_MB": "0"})
    check("total: 0: no limit", left("doll1") == sorted(days + [today]), left("doll1"))


def main():
    root = tempfile.mkdtemp(prefix="dailylogs_")
    logs = os.path.join(root, "log")
    bot1 = os.path.join(logs, "bot1")
    today = time.strftime("%Y-%m-%d", time.gmtime())
    lines = ["mgo1host: bot 1, UDP port 5730, disc /opt/mgo1host/disc, version build 1.00 0123456789ab",
             "ps2 f10: text: \"Connecting to the server...|\"",
             "ps2 f20: cdbios: search file \"cdrom0:\\MGS\\VOX_2.DAT;1\" -> file 5 (not in the image folder: reads refused)",
             "doll f30: stuck at 1 2 3 going to 4 5 6",
             "CRASH: exception c0000005 at mgo1host.exe+1234 (reading 00000000)",
             "ps2 f40: host watch: hosting (frame 40)"]
    r = run([LOGGER, "--dir", bot1], "\n".join(lines) + "\n")
    check("mgo1log passes every line on", r.stdout.splitlines() == lines, r.stdout + r.stderr)
    path = os.path.join(bot1, today + ".log")
    got = open(path).read().splitlines() if os.path.exists(path) else []
    check("the day's file has every line", len(got) == len(lines), got)
    levels = [g.split(" ", 2)[1] for g in got]
    check("levels: the benign cdbios line I, stuck W, CRASH E", levels == ["I", "I", "I", "W", "E", "I"], levels)
    check("each line time-stamped (UTC)", all(g[:10] == today and g[10] == "T" and g.split(" ", 1)[0].endswith("Z") for g in got), got[:1])
    r = run([LOGGER, "--dir", bot1, "--note", "mgo1host: the game crashed (its CRASH line tells where); it starts again", "--level", "E"])
    check("a note printed and written", "the game crashed" in r.stdout and "the game crashed" in open(path).read(), r.stdout)
    sm = json.load(open(os.path.join(bot1, today + ".sum")))
    check("the summary counts", sm.get("lines") == 7 and sm.get("E") == 2 and sm.get("W") == 1 and sm.get("starts") == 1 and sm.get("crashes") == 1, sm)

    # the cap: past it only the errors and warnings ((v150-host beta 16) and the lines a post-mortem needs: cap_kept below)
    cap = os.path.join(logs, "doll2")
    big = ["doll f%d: state: pos %d 70463 -192000 life 185" % (i, i) for i in range(400)] + ["doll f999: pause menu: join given up",
                                                                                           "doll f1000: state: pos 1 2 3 life 0"]
    run([LOGGER, "--dir", cap, "--quiet"], "\n".join(big) + "\n", {"MGO1_LOG_CAP_MB": "0.004"})
    txt = open(os.path.join(cap, today + ".log")).read()
    check("past the cap: the warning kept, the ordinary line not", "join given up" in txt and "pos 1 2 3 life 0" not in txt and "only the errors" in txt)
    check("--quiet: nothing on stdout", True)

    # the old days go
    old = time.strftime("%Y-%m-%d", time.gmtime(time.time() - 40 * 86400))
    open(os.path.join(bot1, old + ".log"), "w").write("x\n")
    open(os.path.join(bot1, old + ".sum"), "w").write("{}")
    run([LOGGER, "--dir", bot1, "--note", "again", "--quiet"])
    check("a day older than 31 days removed", not os.path.exists(os.path.join(bot1, old + ".log")) and not os.path.exists(os.path.join(bot1, old + ".sum")))

    # the agent: list, get, filters, pages, masking
    etc = os.path.join(root, "etc")
    os.makedirs(etc)
    open(os.path.join(etc, "bot1.env"), "w").write("HOST_ACCOUNT_ID=HOSTACC\nHOST_ACCOUNT_PASSWORD=TOPSECRET\n")
    open(os.path.join(etc, "doll2.env"), "w").write("DOLL_ACCOUNT_ID=DOLLACC\nDOLL_ACCOUNT_PASSWORD=DOLLPASS\nDOLL_BOT=1\n")
    run([LOGGER, "--dir", bot1, "--quiet"], "a line with HOSTACC and topsecret in it\ndoll DOLLACC DOLLPASS\n")
    aenv = {"MGO1_CONF_DIR": etc, "MGO1_LOG_DIR": logs, "MGO1_STATE_DIR": os.path.join(root, "var")}
    r = run([AGENT, "logs", "list"], env=aenv)
    j = json.loads(r.stdout or "{}")
    srcs = [x["source"] for x in j.get("logs", [])]
    check("logs list: the sources in order", srcs == ["bot1", "doll2"], r.stdout + r.stderr)
    d0 = (j.get("logs") or [{}])[0].get("days", [{}])[0]
    check("logs list: the day with its counts", d0.get("day") == today and d0.get("E") == 2 and d0.get("crashes") == 1, d0)
    r = run([AGENT, "logs", "get", "bot1", today], env=aenv)
    j = json.loads(r.stdout or "{}")
    body = "\n".join(j.get("lines", []))
    check("logs get: the lines", j.get("ok") and j.get("total", 0) >= 9, r.stdout[:300] + r.stderr)
    check("logs get: no account or password", "HOSTACC" not in body and "TOPSECRET" not in body.upper() and "DOLLACC" not in body
          and "DOLLPASS" not in body and "***" in body, body[-300:])
    r = run([AGENT, "logs", "get", "bot1", today, "--level", "E"], env=aenv)
    j = json.loads(r.stdout or "{}")
    check("logs get --level E: the errors only", j.get("total") == 2 and all(" E " in x for x in j.get("lines", [])), j)
    r = run([AGENT, "logs", "get", "bot1", today, "--lines", "3"], env=aenv)
    j = json.loads(r.stdout or "{}")
    check("logs get --lines: a page and where the next starts", len(j.get("lines", [])) == 3 and j.get("next") == 3, j)
    r = run([AGENT, "logs", "get", "bot1", today, "--tail", "2"], env=aenv)
    j = json.loads(r.stdout or "{}")
    check("logs get --tail: the last lines, from where they start", len(j.get("lines", [])) == 2 and j.get("from") == j.get("total", 0) - 2
          and j.get("next") == -1, j)
    r = run([AGENT, "logs", "get", "bot1", today, "--tail", "2"], env=aenv)
    body = r.stdout
    check("logs get --tail: no account or password", "HOSTACC" not in body and "TOPSECRET" not in body.upper(), body[:300])
    r = run([AGENT, "logs", "get", "bot1", today, "--find", "host watch"], env=aenv)
    j = json.loads(r.stdout or "{}")
    check("logs get --find", j.get("total") == 1 and "host watch" in j.get("lines", [""])[0], j)
    r = run([AGENT, "logs", "get", "../etc", today], env=aenv)
    j = json.loads(r.stdout or "{}")
    check("logs get: a source outside the logs refused", j.get("ok") is False and j.get("code") == "usage", r.stdout)
    r = run([AGENT, "logs", "get", "bot3", today], env=aenv)
    j = json.loads(r.stdout or "{}")
    check("logs get: a day without a log", j.get("ok") is False and j.get("code") == "no_log", r.stdout)

    export_filter(root, aenv)

    # the manager's decisions
    menv = {"MGO1_ETC": etc, "MGO1_VAR": os.path.join(root, "var"), "MGO1_LOG_DIR": logs, "MGO1LOG": LOGGER,
            "MGO1_SYSTEMCTL": "/bin/false", "DOLL_MANAGER_PERIOD": "0", "DOLL_MANAGER_ROUNDS": "1"}
    os.chmod(LOGGER, 0o755)
    r = run([MANAGER], env=menv)
    mpath = os.path.join(logs, "manager", today + ".log")
    mt = open(mpath).read() if os.path.exists(mpath) else ""
    check("the manager's log: started, its capacity", "mgo1doll-manager: started" in mt and "can run" in mt, r.stdout + r.stderr + mt)
    check("the manager's log: no account", "DOLLACC" not in mt and "DOLLPASS" not in mt)

    flood_guard(root)
    cap_kept(root)
    vdoll_adapt_lines(root)
    crash_reports(root)
    wild_lines(root)
    summary_shared(root)
    total_cap(root)
    print("%d failed" % len(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
