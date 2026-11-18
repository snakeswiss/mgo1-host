#!/usr/bin/env python3
"""The accountless dolls' lobby (host/mgo1-dollgate), off line: the gate started on free ports of 127.0.0.1 with a
bot's status file and game file, and a client that speaks its protocol as a doll's game does (TX-55's frames: the gate's
0x3003 log-in as DOLL_<n>, the lobby's 0x3003, 0x4300 the game list, 0x4320 the join). Checks: the game list's entry
(0x4302, 45 bytes: +20 the password lock, +21 Host Only, +25 the maximum, +27 the options, +28 the players; the game keeps
it one byte further from +20: +0x16 Host Only, +0x1A the maximum, +0x1C the options, +0x1D the players) for each game a bot
hosts, and the dialect a v150 doll's game then joins in (a copy of MGO1Loader/netfix.h NetBigChosen and openfix.h
JoinHintFrom, as ps2world.cpp JoinChosen uses them) against the one its host speaks (ps2world.cpp BigGameCreate): a Host
Only game of 9 (8 players and the host) is a game of the stock programs (v150-host beta 15 listed it with Host Only 0:
every doll joined it in the PCs' dialect and failed, 1059, for ever), told as 8 too (HOST_LIST_9_AS_8=1), a game of 10 to
16, TEAM SNEAKING, BASE CAPTURE and maps 13 to 15 are games of the PCs; the entry's players with virtual dolls in the
game; no game listed while the bot does not host or its status is old; the address a join is given.
    python3 tests/test_dollgate.py
"""
import hashlib, json, os, socket, struct, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, "..", "host", "mgo1-dollgate")
KEY = bytes.fromhex("5a7085af")
FAILS = []


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:600] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


# ---- the game's own rules (copies: MGO1Loader/netfix.h, openfix.h, ps2world.cpp) ----
def net_playing_cap(host_only):
    """netfix.h NetPlayingCap: a Host Only game's maximum counts its host."""
    return 9 if host_only else 8


def net_big_chosen(most, host_only):
    """netfix.h NetBigChosen: the game chosen in the list is a game of the PCs by its maximum."""
    return most > net_playing_cap(host_only)


def join_hint(opts):
    """openfix.h JoinHintFrom: the entry's options tell a game of the PCs (maps 13 to 15: bits 2 to 4, more than 8
    players: bit 5, BASE CAPTURE: bit 6, TEAM SNEAKING: bit 7); True: its dialect tried first (gJoinHintSet >= 2)."""
    return bool(opts & 0x1C) or bool(opts & 0xE0)


def doll_dialect(entry):
    """ps2world.cpp JoinChosen, from the list's entry as a doll's game keeps it: `pcs` (the PCs' dialect alone, from the
    first connect) or `stock` (the regions' dialects, a PS2's)."""
    most, host_only, opts = entry[25], entry[21], entry[27]
    return "pcs" if join_hint(opts) or net_big_chosen(most, host_only) else "stock"


def host_dialect(rotation, most):
    """ps2world.cpp BigGameCreate, a dedicated host (always Host Only): a game of the PCs when its rotation holds TEAM
    SNEAKING (5), BASE CAPTURE (6) or a map 13 and up, or with more than 9 players at most (8 and the host)."""
    return "pcs" if any(r >= 5 or m >= 13 for r, m in rotation) or most > net_playing_cap(1) else "stock"


def host_hint(rotation, most):
    """What the host tells SaveMGO's list in its options (ps2world.cpp LobbyCreateMessage: openfix.h MapsListHint, tsne.h
    TsneListMark, basecap.h BcListMark)."""
    o = 0x20 if most > net_playing_cap(1) else 0
    for r, m in rotation:
        o |= {13: 0x04, 14: 0x08, 15: 0x10}.get(m, 0) | (0x80 if r == 5 else 0) | (0x40 if r == 6 else 0)
    return o


# ---- a doll's game as the gate sees it: TX-55's frames ----
def frame(cmd, seq, body):
    h = struct.pack(">HHI", cmd, len(body), seq)
    raw = h + hashlib.md5(h + body).digest() + body
    return bytes(c ^ KEY[i & 3] for i, c in enumerate(raw))


def read_frame(s):
    raw = b""
    while len(raw) < 24:
        c = s.recv(24 - len(raw))
        if not c:
            return None
        raw += c
    hd = bytes(c ^ KEY[i & 3] for i, c in enumerate(raw))
    cmd, n, _ = struct.unpack(">HHI", hd[:8])
    body = b""
    while len(body) < n:
        c = s.recv(n - len(body))
        if not c:
            return None
        body += c
    return cmd, bytes(c ^ KEY[(24 + i) & 3] for i, c in enumerate(body))


def free_ports():
    """Two free TCP ports of 127.0.0.1 next to each other."""
    for _ in range(50):
        a = socket.socket()
        a.bind(("127.0.0.1", 0))
        p = a.getsockname()[1]
        b = socket.socket()
        try:
            b.bind(("127.0.0.1", p + 1))
        except OSError:
            a.close()
            continue
        a.close()
        b.close()
        return p
    raise SystemExit("no two free ports")


class Gate:
    """A bot's doll lobby: its status file, its game file, the gate started on two free ports."""

    def __init__(self, bot=2, env=None, live_file=None):
        self.dir = tempfile.mkdtemp(prefix="dollgate_")
        self.bot = bot
        self.run = os.path.join(self.dir, "var", "bot%d" % bot, "run")
        os.makedirs(self.run)
        self.status = os.path.join(self.run, "status.json")
        self.game_file = live_file or os.path.join(self.dir, "bot%d.game" % bot)
        self.env = dict(os.environ, MGO1LOG=os.path.join(self.dir, "none"), **(env or {}))
        self.env.pop("HOST_LIST_9_AS_8", None)
        self.env.update(env or {})
        self.proc = None
        self.seq = {}

    def game(self, most=9, rotation="0:7, 3:2, 1:3", state="hosting", players=None, name="SaveMGO Host", status_most=None,
             status_rules=True, run_copy=True, live=None):
        """The bot's game: its settings (the run's copy, as mgo1host.sh makes it, and the file of /etc) and its status."""
        text = "name = %s\nrotation = %s\nmax_players = %d\n" % (name, rotation, most)
        with open(self.game_file, "w") as f:
            f.write(text if live is None else live)
        if run_copy:
            with open(os.path.join(self.run, "game.ini"), "w") as f:
                f.write(text)
        elif os.path.exists(os.path.join(self.run, "game.ini")):
            os.remove(os.path.join(self.run, "game.ini"))
        rules = [[int(a), int(b)] for a, b in (x.split(":") for x in rotation.replace(" ", "").split(","))]
        st = {"v": 1, "frame": 5000, "state": state,
              "game": {"name": name, "rules": rules if status_rules else [], "round": 0, "max": most if status_most is None else status_most},
              "players": [{"p": 1, "name": "HBOT", "host": 1, "kind": "host", "doll": 0, "vdoll": 0}] + (players or [])}
        with open(self.status, "w") as f:
            json.dump(st, f)
        return [(r, m) for r, m in rules]

    def start(self):
        self.port = free_ports()
        self.proc = subprocess.Popen([sys.executable, GATE, "--bot", str(self.bot), "--gate-port", str(self.port), "--lobby-port", str(self.port + 1),
                                      "--host-addr", "192.0.2.7", "--host-port", str(5729 + self.bot), "--game-file", self.game_file,
                                      "--status", self.status, "--state-root", os.path.join(self.dir, "var")],
                                     env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for _ in range(100):
            try:
                socket.create_connection(("127.0.0.1", self.port + 1), timeout=0.2).close()
                return self
            except OSError:
                time.sleep(0.1)
        raise SystemExit("the doll gate did not start: " + (self.proc.stdout.read() if self.proc.poll() is not None else "no listener"))

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            out = self.proc.stdout.read()
            self.proc.stdout.close()
            self.proc = None
            return out
        return ""

    def ask(self, s, cmd, body, until):
        """A request, its answers up to the frame `until`: [(command, body)]."""
        self.seq[s] = self.seq.get(s, 0) + 1
        s.sendall(frame(cmd, self.seq[s], body))
        got = []
        while True:
            p = read_frame(s)
            if p is None:
                return got
            got.append(p)
            if p[0] == until:
                return got

    def login(self, n):
        """Doll n's log-in at the gate (no password is checked), then at the lobby: the lobby's connection."""
        g = socket.create_connection(("127.0.0.1", self.port), timeout=5)
        a = self.ask(g, 0x3003, ("DOLL_%d" % n).encode().ljust(16, b"\0") + bytes(16), 0x3004)
        res, token = struct.unpack(">i", a[-1][1][:4])[0], a[-1][1][4:20]
        b = self.ask(g, 0x3040, b"", 0x3041)
        uid = struct.unpack(">I", b[-1][1][4:8])[0]
        g.close()
        s = socket.create_connection(("127.0.0.1", self.port + 1), timeout=5)
        c = self.ask(s, 0x3003, struct.pack(">I", uid) + token, 0x3004)
        return s, res, uid, struct.unpack(">i", c[-1][1][:4])[0]

    def entries(self, s):
        """The game list as the doll's game asks it: the 0x4302 entries (45 bytes each)."""
        got = self.ask(s, 0x4300, b"", 0x4303)
        return [body for cmd, body in got if cmd == 0x4302]


def main():
    VD = [{"p": 2 + k, "name": "SaveMGO_DOLL_%d" % (k + 1), "host": 0, "kind": "doll", "doll": 0, "vdoll": k + 1} for k in range(3)]
    g = Gate()
    rot = g.game(most=9, players=VD)
    g.start()
    s, res, uid, res2 = g.login(1)
    check("the gate's log-in as DOLL_1: accepted, user id 0x7F000001, the lobby's too", res == 0 and uid == 0x7F000001 and res2 == 0, (res, hex(uid), res2))
    ents = g.entries(s)
    e = ents[0] if ents else b""
    check("the list: one game, an entry of 45 bytes, the bot's game by its name, no password lock", len(ents) == 1 and len(e) == 45
          and e[4:20].split(b"\0")[0] == b"SaveMGO Host" and e[20] == 0, ents)
    # the owner's game of 10 November: max_players 9 (8 players and the host), the five stock rules, 3 virtual dolls in it
    check("a game of 9 (8 players and the host): Host Only 1, the maximum 9, no option", len(e) == 45 and (e[21], e[25], e[26], e[27]) == (1, 9, 0, 0),
          list(e[20:29]))
    check("a game of 9: a v150 doll joins it in the stock programs' dialects, as its host speaks (beta 15: the PCs', 1059 for ever)",
          len(e) == 45 and doll_dialect(e) == "stock" == host_dialect(rot, 9), (len(e) == 45 and doll_dialect(e), host_dialect(rot, 9)))
    check("the entry's players: the 3 virtual dolls in the game (the host not counted), not a full game", len(e) == 45 and e[28] == 3 and e[28] < e[25] - 1,
          list(e[20:29]))
    # the address a join gets: the bot's own (here given) and its UDP port, twice
    got = g.ask(s, 0x4320, struct.pack(">I", 1000) + bytes(16), 0x4321)
    body = got[-1][1] if got else b""
    check("the join's answer: the bot's address and UDP port, twice", len(body) == 40 and struct.unpack(">i", body[:4])[0] == 0
          and body[4:20].split(b"\0")[0] == b"192.0.2.7" and struct.unpack(">H", body[20:22])[0] == 5731 and body[22:40] == body[4:22], body)
    # with playable dolls and real players too: every joiner counted
    g.game(most=9, players=VD + [{"p": 5, "name": "DOLL_1", "host": 0, "kind": "doll", "doll": 1, "vdoll": 0},
                                 {"p": 6, "name": "REAL", "host": 0, "kind": "real", "doll": 0, "vdoll": 0},
                                 {"p": 7, "name": "", "host": 0, "kind": "connecting", "doll": 0, "vdoll": 0}])
    e = (g.entries(s) or [b""])[0]
    check("the entry's players: virtual dolls, a playable doll, a real player, one connecting: 6", len(e) == 45 and e[28] == 6, list(e[20:29]))
    # each game a bot hosts: the entry and the dialect a doll's game takes against its host's
    cases = (("max_players 8 (7 players and the host)", 8, "0:7, 3:2"), ("max_players 5", 5, "1:3"),
             ("a big game of 10", 10, "0:7, 3:2"), ("a big game of 12", 12, "1:3"), ("a big game of 16", 16, "0:7, 1:3"),
             ("TEAM SNEAKING (rule 5)", 9, "5:3, 1:3"), ("BASE CAPTURE (rule 6)", 9, "1:3, 6:2"),
             ("map 13 (Silo Entrance)", 9, "1:13"), ("map 14 (REX Hangar)", 9, "0:7, 1:14"), ("map 15 (Big Shell Strut E)", 9, "4:15"),
             ("maps 13, 14 and 15 with TEAM SNEAKING in a game of 16", 16, "5:13, 1:14, 0:15"), ("TEAM SNEAKING in a game of 8", 8, "5:1"))
    for what, most, rotation in cases:
        rot = g.game(most=most, rotation=rotation, players=VD)
        e = (g.entries(s) or [b""])[0]
        ok = len(e) == 45
        check("%s: Host Only 1, the maximum %d, the options the host's own (%02x)" % (what, most, host_hint(rot, most)),
              ok and (e[21], e[25], e[27]) == (1, most, host_hint(rot, most)), list(e[20:29]))
        check("%s: a doll joins in the %s dialect, its host's" % (what, "PCs'" if host_dialect(rot, most) == "pcs" else "stock programs'"),
              ok and doll_dialect(e) == host_dialect(rot, most), (ok and doll_dialect(e), host_dialect(rot, most)))
    # a big game whose status tells 9 (the Create Game screens' most): the settings' max_players tells the big game
    rot = g.game(most=16, rotation="0:7", status_most=9)
    e = (g.entries(s) or [b""])[0]
    check("a game of 16 whose status says 9: the settings' maximum told (16, bit 5), a game of the PCs", len(e) == 45 and e[25] == 16 and e[27] & 0x20
          and doll_dialect(e) == "pcs", list(e[20:29]))
    # the running game's own settings (its run folder's copy, its status's rotation), not the file of /etc changed since
    rot = g.game(most=9, rotation="0:7, 3:2", live="name = SaveMGO Host\nrotation = 5:3\nmax_players = 16\n")
    e = (g.entries(s) or [b""])[0]
    check("the settings file changed since the game's start (TEAM SNEAKING, 16): the running game's told (9, no option)",
          len(e) == 45 and (e[21], e[25], e[27]) == (1, 9, 0) and doll_dialect(e) == "stock", list(e[20:29]))
    rot = g.game(most=9, rotation="5:3, 1:14", status_rules=False)
    e = (g.entries(s) or [b""])[0]
    check("a status without the rotation: the run's settings' one (TEAM SNEAKING, map 14)", len(e) == 45 and e[27] == 0x88 and doll_dialect(e) == "pcs",
          list(e[20:29]))
    rot = g.game(most=12, rotation="6:2", status_rules=False, run_copy=False, status_most=0)
    e = (g.entries(s) or [b""])[0]
    check("no run copy, a status with neither: the settings file's (BASE CAPTURE, 12)", len(e) == 45 and (e[25], e[27]) == (12, 0x60), list(e[20:29]))
    # no game listed while the bot does not host, or its status is older than a minute (a hung game)
    for state in ("starting", "lost"):
        g.game(most=9, state=state)
        check("the bot's status says %s: no game listed" % state, g.entries(s) == [], state)
    g.game(most=9)
    os.utime(g.status, (time.time() - 90, time.time() - 90))
    check("a status 90 s old: no game listed", g.entries(s) == [])
    g.game(most=9)
    check("a fresh status again: listed", len(g.entries(s)) == 1)
    s.close()
    out = g.stop()
    check("the gate's log: the log-in and the join by the doll's number, nothing else of it", "DOLL_1 logged in (user id 0x7f000001)" in out
          and "joins bot 2's game at 192.0.2.7:5731" in out, out[-600:])
    # HOST_LIST_9_AS_8=1 (server.env, developers): a game of 9 told as 8, still a game of the stock programs
    g = Gate(env={"HOST_LIST_9_AS_8": "1"})
    rot = g.game(most=9, players=VD)
    g.start()
    s = g.login(2)[0]
    e = (g.entries(s) or [b""])[0]
    check("HOST_LIST_9_AS_8=1: a game of 9 told as 8, Host Only 1, joined in the stock programs' dialects", len(e) == 45
          and (e[21], e[25], e[27]) == (1, 8, 0) and doll_dialect(e) == "stock" == host_dialect(rot, 9), list(e[20:29]))
    rot = g.game(most=10)
    e = (g.entries(s) or [b""])[0]
    check("HOST_LIST_9_AS_8=1: a game of 10 told as it is, a game of the PCs", len(e) == 45 and (e[25], e[27]) == (10, 0x20) and doll_dialect(e) == "pcs",
          list(e[20:29]))
    s.close()
    g.stop()
    # the copies' own rule, as netfix.h's comment gives it
    check("NetBigChosen: 9 Host Only no, 9 not Host Only yes, 10 Host Only yes, 8 no", (net_big_chosen(9, 1), net_big_chosen(9, 0),
          net_big_chosen(10, 1), net_big_chosen(8, 0)) == (False, True, True, False))
    print("%d failed" % len(FAILS) if FAILS else "all passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
