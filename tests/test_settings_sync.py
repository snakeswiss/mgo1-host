#!/usr/bin/env python3
"""(v150-host beta 22, sync) MGO1 Host's game settings and the server's copies of them, off line (docs/DEDICATED_HOST.md 25).
The owner's report of 13 November: a change of the maps and modes in MGO1 Host was "pushed back" by the server. Checks:
- one copy saved, its version: settings-get's "version" (the file's SHA-256, 16 hex digits), settings-set's the same as
  the next settings-get's; settings-set --base <version> writes only over the copy the form was filled from (another
  writer's save in between, the shell's edit: "settings_changed", nothing written);
- the maps of the server: settings-get's "maps_here" (the disc's 12, and each PC map whose map.ini is in the maps folder,
  MAPS_DIR or the install's); a rotation entry on a map the server does not have is refused ("rotation_map_missing", nothing
  written): beta 21 saved it, its launcher left it out and with none left the game kept its account's last rotation from
  SaveMGO's lobby, the earlier settings;
- what the running game plays: the launcher's exact copy at its start (run/game.src; a launcher before beta 22: run/game.ini),
  settings-get's "running", "applied", "pending" (the keys saved it does not play yet) and "left_out" (the rotation's entries
  it did not make, from its status); settings-set's "takes_effect" (now with --restart, the next game while one runs, the
  next start when none does);
- the file written whole and on the disk (fsync of the file and its folder before and after the rename; a write that fails
  leaves the old file and no leftover);
- the launcher's copy (host/mgo1host.sh: run/game.src the bot's file byte for byte, run/game.ini the game's), the doll lobby
  and a playable doll naming the running game by its own settings (run/game.ini), not by a save made since.
    python3 tests/test_settings_sync.py
    SYNC_AGENT=<agent> SYNC_HOST=<host folder> python3 tests/test_settings_sync.py   (another version's scripts: beta 21's for
                                                                                     the proof that these checks fail there)
"""
import importlib.machinery, importlib.util, json, os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.environ.get("SYNC_HOST") or os.path.join(HERE, "..", "host")
AGENT = os.environ.get("SYNC_AGENT") or os.path.join(HOST, "mgo1host-agent")
FAILS = []
OKS = []


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[-500:] + ")") if detail and not cond else ""))
    (OKS if cond else FAILS).append(what)


def load(path, name):
    loader = importlib.machinery.SourceFileLoader(name, path)
    spec = importlib.util.spec_from_loader(name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


SYSTEMCTL = '#!/bin/sh\ncase "$1" in is-active) echo "${FAKE_ACTIVE:-active}" ;; is-enabled) echo enabled ;; esac\nexit 0\n'
OLD = "# MGO1 dedicated host: the game's settings of bot 1 (written by MGO1 Host)\nname = OWNER DM\nrotation = 0:7, 3:2, 1:3\nmax_players = 9\n"


class Server:
    def __init__(self):
        self.root = os.path.realpath(tempfile.mkdtemp(prefix="settingssync_"))
        self.etc = os.path.join(self.root, "etc")
        self.opt = os.path.join(self.root, "opt")
        self.run_dir = os.path.join(self.root, "var", "bot1", "run")
        os.makedirs(self.etc)
        os.makedirs(self.opt)
        with open(os.path.join(self.etc, "bot1.env"), "w") as f:
            f.write("HOST_ACCOUNT_ID=HOSTACC\nHOST_ACCOUNT_PASSWORD=TOPSECRET\nUDP_PORT=5730\n")
        with open(os.path.join(self.etc, "bot1.game"), "w") as f:
            f.write(OLD)
        sc = os.path.join(self.root, "systemctl")
        with open(sc, "w") as f:
            f.write(SYSTEMCTL)
        os.chmod(sc, 0o755)
        self.env = dict(os.environ, MGO1_CONF_DIR=self.etc, MGO1_STATE_DIR=os.path.join(self.root, "var"),
                        MGO1_LOG_DIR=os.path.join(self.root, "log"), MGO1_HOME_DIR=self.opt, MGO1_SYSTEMCTL=sc,
                        MGO1_SYSTEMD_DIR=os.path.join(self.root, "systemd"), FAKE_ACTIVE="inactive")
        self.game_proc = None

    def run(self, args, stdin=""):
        p = subprocess.run([sys.executable, AGENT] + args, input=stdin, capture_output=True, text=True, timeout=60, env=self.env)
        try:
            return p.returncode, json.loads(p.stdout.splitlines()[0])
        except (ValueError, IndexError):
            return p.returncode, {"raw": p.stdout + p.stderr}

    def game(self):
        return open(os.path.join(self.etc, "bot1.game")).read()

    def get(self):
        return self.run(["settings-get"])

    def set(self, d, *flags):
        return self.run(["settings-set"] + [str(x or "") for x in flags], json.dumps(d))

    def add_map(self, folder, mid, where=None):
        d = os.path.join(where or os.path.join(self.opt, "maps"), folder)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "map.ini"), "w") as f:
            f.write("; a PC map\nid = %d\nname = MAP %d\nstage = n003a\n" % (mid, mid))

    def start_game(self, src=None, given=None, rules=None, players=()):
        """A stand-in for the bot's running game: its process in the run folder (the agent finds it by its command line and
        folder), the launcher's copies, its status file."""
        self.stop_game()
        os.makedirs(self.run_dir, exist_ok=True)
        for name, text in (("game.src", src), ("game.ini", given)):
            p = os.path.join(self.run_dir, name)
            if text is None:
                if os.path.exists(p):
                    os.remove(p)
            else:
                with open(p, "w") as f:
                    f.write(text)
        st = {"v": 1, "state": "hosting", "lobby": 1, "game": {"name": "OWNER DM", "rules": rules or [], "max": 9},
              "players": [{"p": 1, "name": "HOSTACC", "host": 1, "kind": "host"}] + list(players)}
        with open(os.path.join(self.run_dir, "status.json"), "w") as f:
            json.dump(st, f)
        self.game_proc = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)", "mgo1host.exe"], cwd=self.run_dir)
        self.env["FAKE_ACTIVE"] = "active"
        time.sleep(0.3)

    def stop_game(self):
        if self.game_proc:
            self.game_proc.kill()
            self.game_proc.wait()
            self.game_proc = None
        self.env["FAKE_ACTIVE"] = "inactive"

    def close(self):
        self.stop_game()
        shutil.rmtree(self.root, ignore_errors=True)


def version_checks():
    srv = Server()
    try:
        rc, j = srv.get()
        v0 = j.get("version")
        check("settings-get: the saved copy's version, 16 hex digits", rc == 0 and isinstance(v0, str) and re.fullmatch(r"[0-9a-f]{16}", v0 or ""), j)
        check("settings-get: \"settings_sync\": true (MGO1 Host 3.5 sends --base and reads pending only when it sees it)",
              j.get("settings_sync") is True, j)
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:7, 3:2, 1:3, 1:8"})
        rc2, j2 = srv.get()
        check("settings-set: its answer's version is the next settings-get's", rc == 0 and j.get("version") and j.get("version") == j2.get("version")
              and j.get("version") != v0, (j, j2))
        v1 = j2.get("version")
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:12"}, "--base", v0)
        check("settings-set --base <an older version>: refused settings_changed, nothing written",
              rc == 1 and j.get("code") == "settings_changed" and "rotation = 0:7, 3:2, 1:3, 1:8" in srv.game(), (j, srv.game()))
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:12"}, "--base", v1)
        check("settings-set --base <the version read>: written", rc == 0 and "rotation = 0:12" in srv.game(), (j, srv.game()))
        v2 = j.get("version")
        with open(os.path.join(srv.etc, "bot1.game"), "a") as f:   # the shell's edit (mgo1hostctl edit)
            f.write("comment = edited by hand\n")
        rc, j = srv.set({"name": "OWNER DM", "rotation": "1:1"}, "--base", v2)
        check("settings-set --base after an edit by hand on the server: refused, the edit kept",
              rc == 1 and j.get("code") == "settings_changed" and "edited by hand" in srv.game(), (j, srv.game()))
        rc, j = srv.set({"name": "OWNER DM", "rotation": "1:1"})
        check("settings-set without --base (an older MGO1 Host, or REPLACE THEM): written as before", rc == 0 and "rotation = 1:1" in srv.game(), j)
    finally:
        srv.close()


def maps_checks():
    srv = Server()
    try:
        rc, j = srv.get()
        check("settings-get: maps_here, the disc's 12 on a server with no PC maps", rc == 0 and j.get("maps_here") == list(range(1, 13)), j)
        before = srv.game()
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:7, 1:13, 3:15"})
        check("a rotation with PC maps the server does not have: refused rotation_map_missing (the first one), nothing written",
              rc == 1 and j.get("code") == "rotation_map_missing" and j.get("args") == ["1:13"] and srv.game() == before, j)
        rc, j = srv.set({"name": "OWNER DM", "rotation": "1:13"})
        check("a rotation of PC maps only, none here: refused, nothing written (beta 21: saved, then the game kept SaveMGO's last rotation)",
              rc == 1 and j.get("code") == "rotation_map_missing" and srv.game() == before, j)
        srv.add_map("n18", 13)
        rc, j = srv.get()
        check("maps_here: a map.ini with id 13 in the install's maps folder adds 13", j.get("maps_here") == list(range(1, 14)), j)
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:7, 1:13"})
        check("map 13 here: its rotation accepted", rc == 0 and "rotation = 0:7, 1:13" in srv.game(), j)
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:7, 1:13, 3:15"})
        check("map 15 still missing: refused 3:15", rc == 1 and j.get("args") == ["3:15"], j)
        other = os.path.join(srv.root, "maps2")
        srv.add_map("heliport", 15, other)
        with open(os.path.join(srv.etc, "server.env"), "w") as f:
            f.write("MAPS_DIR=%s\n" % other)
        rc, j = srv.get()
        check("MAPS_DIR (server.env) read as the launcher reads it: 15 there, 13 not", j.get("maps_here") == list(range(1, 13)) + [15], j)
        with open(os.path.join(other, "heliport", "map.ini"), "w") as f:   # (sync2, the review's S6) two id lines: the game takes the last
            f.write("id = 13\nname = X\nid = 15\n")
        rc, j = srv.get()
        check("a map.ini with two id lines: the last one, as the game reads it", j.get("maps_here") == list(range(1, 13)) + [15], j)
    finally:
        srv.close()


def running_checks():
    srv = Server()
    try:
        rc, j = srv.get()
        check("no game running: running false, applied none, pending none", j.get("running") is False and j.get("applied") is None
              and j.get("pending") == [], j)
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:12, 1:1"})
        check("a save with no game running: takes_effect \"next start\"", rc == 0 and j.get("takes_effect") == "next start", j)
        with open(os.path.join(srv.etc, "bot1.game"), "w") as f:
            f.write(OLD)
        srv.start_game(src=OLD, given=OLD, rules=[[0, 7], [3, 2], [1, 3]],
                       players=[{"p": 2, "name": "P2", "kind": "real"}, {"p": 3, "name": "VDOLL1", "kind": "doll", "vdoll": 1}])
        rc, j = srv.get()
        check("a game running with the saved settings: running, nothing pending, nothing left out", j.get("running") is True and j.get("pending") == []
              and j.get("left_out") == [] and (j.get("applied") or {}).get("rotation") == "0:7, 3:2, 1:3", j)
        check("settings-get: the running game's real players (1)", j.get("real_players") == 1, j)
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:12, 1:1", "max_players": "9"})
        check("a save while the game runs: takes_effect \"next game\", the rotation pending (beta 21 said nothing: the panel showed it saved)",
              rc == 0 and j.get("takes_effect") == "next game" and j.get("pending") == ["rotation"], j)
        rc, j = srv.get()
        check("settings-get after it: the saved rotation, the running game's in applied, rotation pending",
              j.get("settings", {}).get("rotation") == "0:12, 1:1" and (j.get("applied") or {}).get("rotation") == "0:7, 3:2, 1:3"
              and j.get("pending") == ["rotation"] and j.get("applied_rotation") == "0:7, 3:2, 1:3", j)
        new = srv.game()
        srv.start_game(src=new, given=new, rules=[[0, 12], [1, 1]])
        rc, j = srv.get()
        check("the next game (its launcher's copy the new file): nothing pending", j.get("pending") == [] and j.get("left_out") == [], j)
        srv.start_game(src=new, given=new, rules=[[1, 1]])
        rc, j = srv.get()
        check("a rotation's entry the game did not make (its status): left_out", j.get("left_out") == ["0:12"], j)
        srv.start_game(src=None, given=OLD, rules=[[0, 7], [3, 2], [1, 3]])
        rc, j = srv.get()
        check("a game started by a launcher before beta 22 (run/game.ini alone): compared with it", j.get("pending") == ["rotation"]
              and (j.get("applied") or {}).get("rotation") == "0:7, 3:2, 1:3", j)
        srv.stop_game()                                  # (sync2, the review's S5) a start waiting, the last game's run folder there
        srv.env["FAKE_ACTIVE"] = "activating"
        rc, j = srv.get()
        check("a start waiting (the service activating, no game process, the last game's run folder): not running, nothing pending",
              j.get("running") is False and j.get("pending") == [] and j.get("applied") is None, j)
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:12, 1:1"})
        check("a save during that wait: takes_effect \"next start\"", rc == 0 and j.get("takes_effect") == "next start", j)
        srv.start_game(src=OLD, given=OLD, rules=[[0, 7], [3, 2], [1, 3]])
        rc, j = srv.set({"name": "OWNER DM", "rotation": "0:12, 1:1"}, "--restart")
        cmd = open(os.path.join(srv.run_dir, "cmd.txt")).read() if os.path.exists(os.path.join(srv.run_dir, "cmd.txt")) else ""
        check("a save with --restart: takes_effect \"now\", the game told to quit (a new game with the new settings), nothing pending",
              rc == 0 and j.get("takes_effect") == "now" and j.get("pending") == [] and (j.get("restart") or {}).get("how") == "quit"
              and "quit" in cmd, (j, cmd))
    finally:
        srv.close()


def durable_checks():
    """The file written whole, on the disk before it takes the name, the folder's entry after; a failed write leaves the old
    file and no leftover."""
    srv = Server()
    try:
        for k, v in srv.env.items():
            if k.startswith("MGO1_"):
                os.environ[k] = v
        agent = load(AGENT, "agent_sync")
        path = os.path.join(srv.etc, "bot1.game")
        calls = []
        real_fsync, real_write = os.fsync, os.write
        os.fsync = lambda fd: (calls.append(fd), real_fsync(fd))[1]
        try:
            agent.write_atomic(path, "name = NEW ONE\n")
        finally:
            os.fsync = real_fsync
        check("write_atomic: the file and its folder synced to the disk (fsync twice)", len(calls) >= 2 and open(path).read() == "name = NEW ONE\n", calls)
        state = {"n": 0}

        def failing(fd, data):
            state["n"] += 1
            if state["n"] > 1:
                raise OSError(28, "No space left on device")
            return real_write(fd, data[:5])
        os.write = failing
        try:
            agent.write_atomic(path, "name = HALF WRITTEN AND LOST\n")
            failed = False
        except OSError:
            failed = True
        finally:
            os.write = real_write
        left = [x for x in os.listdir(srv.etc) if x.endswith(".tmp")]
        check("a write that fails midway: an error, the old file whole, no leftover", failed and open(path).read() == "name = NEW ONE\n" and not left,
              (failed, open(path).read(), left))
    finally:
        for k in list(os.environ):
            if k.startswith("MGO1_") and k in srv.env:
                del os.environ[k]
        srv.close()


FAKE_WINE = r'''#!/bin/bash
[ "$1" = wineboot ] && exit 0
exit 0
'''


def launcher_checks():
    root = tempfile.mkdtemp(prefix="syncl_")
    try:
        home = os.path.join(root, "opt")
        for d in ("steps", "disc/MGS/NET", "disc/MODULES"):
            os.makedirs(os.path.join(home, d))
        for f in ("MGS3_N.ELF", "STAGE_N.DAT", "SLOT_N.DAT"):
            open(os.path.join(home, "disc/MGS/NET", f), "w").write("x")
        for f in ("login_account.txt", "host_create.txt"):
            open(os.path.join(home, "steps", f), "w").write("# step\n")
        open(os.path.join(home, "mgo1host.exe"), "w").write("x")
        wine = os.path.join(root, "wine")
        open(wine, "w").write(FAKE_WINE)
        os.chmod(wine, 0o755)
        game = os.path.join(root, "bot1.game")
        text = "name = TESTGAME\r\nrotation = 0:7, 1:13, 3:2\r\nmax_players = 9\r\n"
        open(game, "w", newline="").write(text)
        state = os.path.join(root, "bot1")
        env = dict(os.environ, HOME_DIR=home, DISC_DIR=os.path.join(home, "disc"), WINE=wine, GAME_SETTINGS=game, STATE_DIR=state,
                   LOG_DIR=os.path.join(root, "log"), MGO1LOG=os.path.join(HOST, "mgo1log"), SERVERS_FILE=os.path.join(root, "none.ini"),
                   BANS_FILE=os.path.join(root, "bans"), WHITELIST_FILE=os.path.join(root, "white"), HOST_ACCOUNT_ID="HOSTACC",
                   HOST_ACCOUNT_PASSWORD="TOPSECRET", DOLL_GATE="0", MAPS_DIR=os.path.join(root, "nomaps"))
        subprocess.run(["bash", os.path.join(HOST, "mgo1host.sh")], env=env, capture_output=True, timeout=60)
        run = os.path.join(state, "run")
        src = open(os.path.join(run, "game.src"), newline="").read() if os.path.exists(os.path.join(run, "game.src")) else None
        given = open(os.path.join(run, "game.ini")).read() if os.path.exists(os.path.join(run, "game.ini")) else ""
        check("mgo1host.sh: run/game.src the bot's file byte for byte (what this game started with)", src == text, src)
        check("mgo1host.sh: run/game.ini the game's copy, map 13 left out (no PC maps here)", "rotation = 0:7, 3:2" in given and "1:13" not in given, given)
    finally:
        shutil.rmtree(root, ignore_errors=True)


def doll_name_checks():
    """The doll lobby lists the running game by its own name (run/game.ini), a playable doll looks for that name: a save in
    MGO1 Host without a restart renamed the game in bot<N>.game only, and the dolls looked for a game that does not run."""
    root = tempfile.mkdtemp(prefix="syncd_")
    try:
        run = os.path.join(root, "var", "bot1", "run")
        os.makedirs(run)
        game = os.path.join(root, "bot1.game")
        open(game, "w").write("name = NEW NAME\npassword = NEWPW\n")
        open(os.path.join(run, "game.ini"), "w").write("name = RUNNING DM\npassword = RUNPW\n")
        gate = load(os.path.join(HOST, "mgo1-dollgate"), "dollgate_sync")

        class A:
            pass
        gate.ARGS = A()
        gate.ARGS.game_file, gate.ARGS.status = game, os.path.join(run, "status.json")
        check("mgo1-dollgate: the running game's name (run/game.ini), not a save made since", gate.game_name() == "RUNNING DM", gate.game_name())
        # a playable doll (mgo1doll.sh with no GAME_SETTINGS: the running game's copy first)
        home = os.path.join(root, "opt")
        for d in ("steps", "disc/MGS/NET", "disc/MODULES"):
            os.makedirs(os.path.join(home, d))
        for f in ("MGS3_N.ELF", "STAGE_N.DAT", "SLOT_N.DAT"):
            open(os.path.join(home, "disc/MGS/NET", f), "w").write("x")
        open(os.path.join(home, "steps", "doll_join.txt"), "w").write("# step\n")
        open(os.path.join(home, "mgo1host.exe"), "w").write("x")
        wine = os.path.join(root, "wine")
        open(wine, "w").write(FAKE_WINE)
        os.chmod(wine, 0o755)
        env = {k: v for k, v in os.environ.items() if k != "GAME_SETTINGS"}
        env.update(HOME_DIR=home, DISC_DIR=os.path.join(home, "disc"), WINE=wine, DOLL="1", DOLL_BOT="1",
                   STATE_DIR=os.path.join(root, "var", "doll1"), LOG_DIR=os.path.join(root, "log"), MGO1LOG=os.path.join(HOST, "mgo1log"),
                   DOLL_ACCOUNT_ID="DOLLACC", DOLL_ACCOUNT_PASSWORD="DOLLPASS1", SERVERS_FILE=os.path.join(root, "none.ini"))
        try:                                             # (a doll that finds no game name waits 60 s, then ends)
            subprocess.run(["bash", os.path.join(HOST, "mgo1doll.sh")], env=env, capture_output=True, timeout=30)
        except subprocess.TimeoutExpired:
            pass
        ini = os.path.join(root, "var", "doll1", "run", "doll.ini")
        text = open(ini).read() if os.path.exists(ini) else ""
        check("mgo1doll.sh: the doll looks for the running game's name and lock (run/game.ini)", "game=RUNNING DM" in text
              and "game_password=RUNPW" in text, text[:300])
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main():
    print("agent %s, launchers %s" % (AGENT, HOST))
    version_checks()
    maps_checks()
    running_checks()
    durable_checks()
    launcher_checks()
    doll_name_checks()
    print("\n%d ok, %d FAIL" % (len(OKS), len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
