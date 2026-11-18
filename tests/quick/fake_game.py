#!/usr/bin/env python3
"""(build 1.00, quickstart) A stand-in of bot n's game (mgo1host.exe under Wine) for tests/fake_ssh_host.py's sandbox: its run
folder's status.json every second as the host writes it, from bot<n>.env and bot<n>.game: logging in 3 s, then hosting its game
in the lobby 6 s after its start; an account whose password starts with WRONG: SaveMGO's refusal on the screen instead. The
`quit` command (run/cmd.txt) starts a new game."""
import json, os, sys, time

n = sys.argv[1]
R = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
run = os.getcwd()


def kv(path):
    d = {}
    try:
        for line in open(path):
            if "=" in line and not line.lstrip().startswith("#"):
                k, v = line.split("=", 1)
                d[k.strip()] = v.strip()
    except OSError:
        pass
    return d


t0 = time.time()
while True:
    try:
        cmd = open(os.path.join(run, "cmd.txt")).read()
        if "quit" in cmd:
            open(os.path.join(run, "cmd.txt"), "w").close()
            t0 = time.time()
    except OSError:
        pass
    env, game = kv(os.path.join(R, "etc", "bot%s.env" % n)), kv(os.path.join(R, "etc", "bot%s.game" % n))
    t = time.time() - t0
    acc, pw = env.get("HOST_ACCOUNT_ID", ""), env.get("HOST_ACCOUNT_PASSWORD", "")
    st = {"state": "login"}
    if t > 3 and pw.startswith("WRONG"):
        st = {"state": "login", "error": "Server login failed. ( 816:-4 )"}
    elif t > 6 and acc:
        rules = [[int(a), int(b)] for a, b in (x.strip().split(":") for x in game.get("rotation", "1:2").split(",") if ":" in x)]
        st = {"state": "hosting", "lobby": 1, "game": {"name": game.get("name", "SaveMGO Host"), "max": int(game.get("max_players", "9")),
              "rules": rules, "round": 0, "age": int(t - 6)}, "players": [{"p": 1, "name": acc, "host": 1, "state": 534}]}
    tmp = os.path.join(run, "status.json.tmp")
    with open(tmp, "w") as f:
        json.dump(st, f)
    os.replace(tmp, os.path.join(run, "status.json"))
    time.sleep(1)
