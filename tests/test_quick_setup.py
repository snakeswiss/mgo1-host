#!/usr/bin/env python3
"""(build 1.00, quickstart) MGO1 Host's QUICK SETUP, GO from the first field to the bot online, against a stand-in server.

For each package (public: server/ with mgo1host-src.tar.gz and no host program, the server builds it; private: server/ with
mgo1host.exe.gz and its SHA-256): a program folder (hostpanel/package.sh's server/ files, tests/quick/install.sh as install.sh),
a disc's stand-in folder (a line of text a file: no file of the game), a sandbox with tests/fake_ssh_host.py on 127.0.0.1, the
real host/mgo1host-agent installed there by the stand-in install.sh; then tests/panel_quick_go.cpp (the program's own App,
Remote and page, built from hostpanel/build-linux/obj) presses GO and answers the key's box, and prints every step's state.
Afterwards the sandbox's files are checked: the uploads (the archive or the host program), bot 1's account, its game settings
and its virtual dolls as GO set them.
    bash hostpanel/build_linux.sh          (once: the objects the test links)
    python3 tests/test_quick_setup.py [--python <a python with paramiko>] [public] [private]
DEPS: hostpanel/build_linux.sh's (default hostpanel/deps/linux; DEPS=... as for the build).
"""
import argparse, gzip, hashlib, io, os, shutil, signal, socket, subprocess, sys, tarfile, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PANEL = os.path.join(ROOT, "hostpanel")
FAILS = []
PKG = ["mgo1host.sh", "mgo1host-agent", "mgo1hostctl", "mgo1host@.service", "bot.env.example", "server.env.example", "game.ini.example",
       "README.md", "mgo1doll.sh", "mgo1doll-manager", "mgo1doll@.service", "mgo1doll-manager.service", "doll.env.example", "mgo1log",
       "mgo1-dollgate"]


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:300] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


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


def build_harness(out):
    obj = os.path.join(PANEL, "build-linux", "obj")
    deps = os.environ.get("DEPS") or os.path.join(PANEL, "deps", "linux")
    prefix = os.path.join(deps, "prefix")
    imgui = os.path.join(PANEL, "build-linux", "imgui")
    if not os.path.isfile(os.path.join(obj, "app.o")) or not os.path.isdir(prefix):
        sys.exit("test_quick_setup: build the Linux program first (bash hostpanel/build_linux.sh, DEPS as for it)")
    src = os.path.join(PANEL, "src")
    flags = ["-O1", "-std=c++17", "-pthread", '-DIMGUI_USER_CONFIG="imgui_user.h"', "-I" + src, "-I" + imgui, "-I" + os.path.join(prefix, "include")]
    harness_o = out + ".o"
    subprocess.run(["g++"] + flags + ["-c", os.path.join(HERE, "panel_quick_go.cpp"), "-o", harness_o], check=True)
    skip = {"main_glfw.o", "imgui_impl_glfw.o", "imgui_impl_opengl3.o", "main.o", "platform_win.o"}
    objs = [os.path.join(obj, f) for f in sorted(os.listdir(obj)) if f.endswith(".o") and f not in skip]
    subprocess.run(["g++", "-pthread", "-o", out, harness_o] + objs + ["-L" + os.path.join(prefix, "lib"), "-lssh2", "-lssl", "-lcrypto",
                                                                       "-ldl", "-lm", "-lrt"], check=True)


def program_folder(base, kind):
    prog = os.path.join(base, "prog")
    srv = os.path.join(prog, "server")
    os.makedirs(os.path.join(srv, "steps"))
    for f in PKG:
        shutil.copy(os.path.join(ROOT, "host", f), srv)
    shutil.copy(os.path.join(HERE, "quick", "install.sh"), os.path.join(srv, "install.sh"))
    for f in ("login_account.txt", "host_create.txt", "doll_join.txt"):
        shutil.copy(os.path.join(HERE, "steps", f), os.path.join(srv, "steps", f))
    if kind == "public":                           # srvbuild's public folder: the sources' archive, no host program
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as t:
            data = b"#!/bin/bash\n# stand-in of host/build_server.sh\n"
            info = tarfile.TarInfo("host/build_server.sh"); info.size = len(data)
            t.addfile(info, io.BytesIO(data))
        open(os.path.join(srv, "mgo1host-src.tar.gz"), "wb").write(buf.getvalue())
        open(os.path.join(srv, "VERSION"), "w").write("build 1.00 source\n")
    else:                                          # the private folder: the host program, compressed, its SHA-256
        exe = b"stand-in mgo1host.exe of the private package\n"
        h = hashlib.sha256(exe).hexdigest()
        open(os.path.join(srv, "mgo1host.exe.gz"), "wb").write(gzip.compress(exe))
        open(os.path.join(srv, "mgo1host.exe.sha256"), "w").write(h + "\n")
        open(os.path.join(srv, "VERSION"), "w").write("build 1.00 %s\n" % h[:12])
    return prog


def disc_folder(base):
    d = os.path.join(base, "disc")
    os.makedirs(os.path.join(d, "MGS", "NET"))
    os.makedirs(os.path.join(d, "MODULES"))
    for f in ("MGS3_N.ELF", "STAGE_N.DAT", "SLOT_N.DAT"):
        open(os.path.join(d, "MGS", "NET", f), "w").write("stand-in %s (not a file of the game)\n" % f)
    open(os.path.join(d, "MODULES", "MODNCD.IPK"), "w").write("stand-in module\n")
    return d


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def scenario(kind, harness, python):
    print("\n==== %s package ====" % kind)
    base = os.path.realpath(tempfile.mkdtemp(prefix="quick_%s_" % kind))
    prog, disc = program_folder(base, kind), disc_folder(base)
    sand = os.path.join(base, "server")
    os.makedirs(os.path.join(sand, "bin"))
    for f in ("systemctl", "fake_game.py"):
        shutil.copy(os.path.join(HERE, "quick", f), os.path.join(sand, "bin", f))
        os.chmod(os.path.join(sand, "bin", f), 0o755)
    # the build: 4 steps of 40 s, a percent every 10 s, 70 s with no line at all in gen; the compile fails once (public)
    open(os.path.join(sand, "test.env"), "w").write("BUILD_STEP_S=40\nPCT_EVERY_S=10\nQUIET_S=70\nFAIL_BUILD_ONCE=%d\n" % (kind == "public"))
    port, pw = free_port(), "Root-pw-for-the-test-1"
    srv = subprocess.Popen([python, os.path.join(HERE, "fake_ssh_host.py"), "--root", sand, "--port", str(port), "--password", pw],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(srv.stdout.readline().rstrip())
    env = dict(os.environ, XDG_CONFIG_HOME=os.path.join(base, "config"), HOME=os.path.join(base, "home"))
    os.makedirs(env["HOME"])
    t0 = time.time()
    try:
        p = subprocess.run([harness, "--prog", prog, "--port", str(port), "--password", pw, "--disc", disc, "--account", "GRU_HOST",
                            "--good", "SNAKEEATER64", "--bad", "WRONGPASS1", "--scenario", kind], env=env, timeout=3600)
        check("%s: the program's run (panel_quick_go)" % kind, p.returncode == 0, "rc %d" % p.returncode)
    finally:
        srv.send_signal(signal.SIGTERM)
        srv.wait(10)
        for f in os.listdir(os.path.join(sand, "svc")) if os.path.isdir(os.path.join(sand, "svc")) else []:
            if f.endswith(".pid"):
                try:
                    os.kill(int(open(os.path.join(sand, "svc", f)).read()), signal.SIGTERM)
                except (OSError, ValueError):
                    pass
    print("(%d s)" % (time.time() - t0))
    stage = os.path.join(sand, "stage")
    env1 = kv(os.path.join(sand, "etc", "bot1.env"))
    game = kv(os.path.join(sand, "etc", "bot1.game"))
    log = open(os.path.join(sand, "commands.log")).read() if os.path.isfile(os.path.join(sand, "commands.log")) else ""
    if kind == "public":
        check("public: mgo1host-src.tar.gz sent next to install.sh", os.path.isfile(os.path.join(sand, "opt", "build", "built.txt")))
        check("public: no host program sent, no unpacking", "mgo1host.exe.gz" not in log and "gzip -dc" not in log)
        check("public: the archive's upload", "mgo1host-src.tar.gz" in log)
    else:
        check("private: the host program sent, unpacked and checked", "mgo1host.exe.gz" in log and "gzip -dc" in log and "sha256sum mgo1host.exe" in log)
        check("private: no build", not os.path.isfile(os.path.join(sand, "opt", "build", "built.txt")))
    check("%s: the disc's files sent" % kind, os.path.isfile(os.path.join(sand, "opt", "disc", "MGS", "NET", "STAGE_N.DAT")))
    check("%s: bot 1's account (HOST ACCOUNT's account-set)" % kind,
          env1.get("HOST_ACCOUNT_ID") == "GRU_HOST" and env1.get("HOST_ACCOUNT_PASSWORD") == "SNAKEEATER64", {k: v for k, v in env1.items() if "PASSWORD" not in k})
    check("%s: the game settings (rotation, 8 players + the host)" % kind,
          game.get("rotation") == "1:3, 0:2, 1:8, 0:12" and game.get("max_players") == "9" and game.get("name") == "SAVEMGO HOST", game)
    check("%s: the virtual dolls (7, as a player, level 5, the live level 1 to 10)" % kind,
          env1.get("VDOLL_COUNT") == "7" and env1.get("VDOLL_CONTROLS") == "player" and env1.get("VDOLL_LEVEL", "5") == "5" and
          env1.get("VDOLL_ADAPT") == "1" and env1.get("VDOLL_ADAPT_MIN", "1") == "1" and env1.get("VDOLL_ADAPT_MAX", "10") == "10",
          {k: v for k, v in env1.items() if k.startswith("VDOLL")})
    st = os.path.join(sand, "svc", "mgo1host@1.state")
    check("%s: bot 1 started" % kind, os.path.isfile(st) and open(st).read().strip() == "active")
    cfgdir = os.path.join(base, "config", "MGO1 Host")
    text = "".join(open(os.path.join(cfgdir, f), errors="replace").read() for f in os.listdir(cfgdir) if f.endswith(".ini"))
    check("%s: no password in clear in the program's settings" % kind, pw not in text and "SNAKEEATER64" not in text)
    shutil.rmtree(base, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--python", default=sys.executable, help="a python with paramiko, for tests/fake_ssh_host.py")
    ap.add_argument("kinds", nargs="*", default=["public", "private"])
    a = ap.parse_args()
    if subprocess.run([a.python, "-c", "import paramiko"], capture_output=True).returncode:
        sys.exit("test_quick_setup: tests/fake_ssh_host.py needs paramiko (pip install paramiko; --python <that python>)")
    harness = os.path.join(tempfile.mkdtemp(prefix="quick_go_"), "panel_quick_go")
    build_harness(harness)
    for k in a.kinds:
        scenario(k, harness, a.python)
    shutil.rmtree(os.path.dirname(harness), ignore_errors=True)
    print("\n%d failed" % len(FAILS) if FAILS else "\nall passed")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
