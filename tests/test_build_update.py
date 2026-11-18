#!/usr/bin/env python3
"""(build 1.00, build1panel) The update of a server from beta 25 (agent 3.7, VERSION "v150-beta25 ...") to build 1.00, as
MGO1 Host's UPDATE THE HOST does it: host/install.sh run for real, as root, in a mount namespace of its own where /etc,
/opt, /var and /usr/local are overlays on folders of this test (nothing of the machine is written; a machine with an
install of its own is skipped). Stand-ins: the host executable, the disc's three files (a few bytes of text, no disc data),
wine64 and systemctl (bot 1's service "active", as on a running server).
  1. the beta 25 layout: hostpanel/package.sh's server folder with VERSION "v150-beta25 593d64629f86" and the agent's
     AGENT_VERSION "3.7", installed; the agent answers "agent": "3.7", install-check "v150-beta25 ...";
  2. the update: the build 1.00 server folder (hostpanel/package.sh --version "build 1.00", its VERSION
     "build 1.00 <the executable's SHA-256, 12 digits>"), mgo1host.exe.gz unpacked and checked as MGO1 Host does, then
     `env MGO1_MARKS=1 bash install.sh` (MGO1 Host's command): its marks end with @is_installed|build 1.00 ...; once more by
     hand: "== installed build 1.00 ...", and no line of either names a beta, v150 or 3.7;
  3. after it: version, status and install-check answer build 1.00 (the agent "build 1.00", the host
     "build 1.00 <12 digits>"), the old agent gone;
  4. MGO1 Host's judgement (tests/panel_version_test.cpp's pair mode, hostpanel/src/version.h): the server's agent before
     the update older than the one carried (the banner, UPDATE THE HOST), after it not older (no banner).
    sudo python3 tests/test_build_update.py
"""
import hashlib, json, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.realpath(os.path.join(HERE, ".."))
FAILS = []

FAKE_WINE = "#!/bin/sh\n[ \"$1\" = --version ] && echo 'wine-9.0 (stand-in)'\nexit 0\n"
FAKE_SYSTEMCTL = ("#!/bin/sh\necho \"systemctl $*\" >> \"$MGO1_TEST_LOG\"\n"
                  "case \"$1\" in is-active) [ \"$2\" = --quiet ] || echo active ;; is-enabled) echo enabled ;; esac\nexit 0\n")
FAKE_APT = "#!/bin/sh\necho \"apt-get $* (the test's stand-in refuses)\" >&2\nexit 1\n"
OLD_WORDS = re.compile(r"\bbeta\b|v150|\b3\.7\b|\b3\.5\b", re.I)


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:600] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


def server_folder(dst, version, exe_text, agent_version=None):
    """hostpanel/package.sh's server folder (what MGO1 Host carries and uploads) for a stand-in executable."""
    work = os.path.join(dst + ".pkg")
    os.makedirs(work)
    exe, gui = os.path.join(work, "mgo1host.exe"), os.path.join(work, "MGO1 Host.exe")
    open(exe, "w").write(exe_text)
    open(gui, "w").write("stand-in\n")
    p = subprocess.run(["bash", os.path.join(ROOT, "hostpanel", "package.sh"), "--host-exe", exe, "--version", version, "--out", work],
                       capture_output=True, text=True, env=dict(os.environ, GUI=gui, DEPS=os.path.join(work, "nodeps")))
    if p.returncode != 0:
        raise SystemExit("package.sh failed: " + p.stdout + p.stderr)
    shutil.move(os.path.join(work, "MGO1 Host", "server"), dst)
    shutil.rmtree(work)
    if agent_version:
        a = os.path.join(dst, "mgo1host-agent")
        t = re.sub(r'^AGENT_VERSION = "[^"]*"', 'AGENT_VERSION = "%s"' % agent_version, open(a).read(), count=1, flags=re.M)
        open(a, "w").write(t)
    # as MGO1 Host's stage: the executable unpacked, then its SHA-256 checked against mgo1host.exe.sha256
    with open(os.path.join(dst, "mgo1host.exe.gz"), "rb") as f:
        import gzip
        data = gzip.decompress(f.read())
    open(os.path.join(dst, "mgo1host.exe"), "wb").write(data)
    os.remove(os.path.join(dst, "mgo1host.exe.gz"))
    want = open(os.path.join(dst, "mgo1host.exe.sha256")).read().strip()
    return hashlib.sha256(data).hexdigest() == want, open(os.path.join(dst, "VERSION")).read().strip()


INSIDE = r'''
set -u
R=$1
for d in etc opt var usr/local; do
    n=${d//\//_}
    mkdir -p "$R/ovl/$n/up" "$R/ovl/$n/work"
    mount -t overlay overlay -o lowerdir=/$d,upperdir=$R/ovl/$n/up,workdir=$R/ovl/$n/work /$d || { echo "MOUNT_FAILED $d"; exit 3; }
done
export PATH=$R/bin:$PATH MGO1_TEST_LOG=$R/systemctl.log
# bot 1 with an account, as on the owner's server before the update (install.sh keeps it)
step() { echo "=== $1"; }
step "install beta 25"
bash "$R/src25/install.sh" > "$R/out25.txt" 2>&1; echo "rc=$?" >> "$R/out25.txt"
printf 'HOST_ACCOUNT_ID=HOSTACC\nHOST_ACCOUNT_PASSWORD=TOPSECRET\nUDP_PORT=5730\n' > /etc/mgo1host/bot1.env
mgo1host-agent version > "$R/v25_version.json" 2>&1
mgo1host-agent install-check > "$R/v25_check.json" 2>&1
step "update to build 1.00 (MGO1 Host's command)"
env MGO1_MARKS=1 bash "$R/src100/install.sh" > "$R/out100_marks.txt" 2>&1; echo "MGO1HOST_INSTALL_EXIT=$?" >> "$R/out100_marks.txt"
step "the same by hand"
bash "$R/src100/install.sh" > "$R/out100_hand.txt" 2>&1; echo "rc=$?" >> "$R/out100_hand.txt"
mgo1host-agent version > "$R/v100_version.json" 2>&1
mgo1host-agent --bot 1 status > "$R/v100_status.json" 2>&1
mgo1host-agent install-check > "$R/v100_check.json" 2>&1
cat /opt/mgo1host/VERSION > "$R/v100_VERSION.txt" 2>&1
grep -c '^AGENT_VERSION = "build 1.00"' /usr/local/bin/mgo1host-agent > "$R/v100_agentfile.txt" 2>&1
'''


def main():
    if os.geteuid() != 0 or not shutil.which("unshare"):
        print("skipped: needs root and unshare (a mount namespace with overlays)")
        return 0
    for p in ("/opt/mgo1host", "/etc/mgo1host", "/var/lib/mgo1host"):
        if os.path.exists(p):
            print("skipped: this machine has an install of its own (%s)" % p)
            return 0
    R = os.path.realpath(tempfile.mkdtemp(prefix="buildupdate_"))
    try:
        os.makedirs(os.path.join(R, "bin"))
        for name, text in (("wine64", FAKE_WINE), ("systemctl", FAKE_SYSTEMCTL), ("apt-get", FAKE_APT)):
            open(os.path.join(R, "bin", name), "w").write(text)
            os.chmod(os.path.join(R, "bin", name), 0o755)
        ok25, ver25 = server_folder(os.path.join(R, "src25"), "v150-beta25", "beta 25's host (stand-in)\n", agent_version="3.7")
        # (the beta 25 package's VERSION as the owner's server has it)
        open(os.path.join(R, "src25", "VERSION"), "w").write("v150-beta25 593d64629f86\n")
        disc = os.path.join(R, "src25", "disc")
        for d in ("MGS/NET", "MODULES"):
            os.makedirs(os.path.join(disc, d))
        for f in ("MGS3_N.ELF", "STAGE_N.DAT", "SLOT_N.DAT"):
            open(os.path.join(disc, "MGS/NET", f), "w").write("stand-in, not a disc file\n")
        open(os.path.join(disc, "MODULES", "STANDIN.IRX"), "w").write("stand-in\n")
        exe100 = "build 1.00's host (stand-in)\n"
        ok100, ver100 = server_folder(os.path.join(R, "src100"), "build 1.00", exe100)
        sha12 = hashlib.sha256(exe100.encode()).hexdigest()[:12]
        check("the build 1.00 server folder: VERSION \"build 1.00 <12 digits of the executable's SHA-256>\"", ver100 == "build 1.00 " + sha12, ver100)
        check("the executables unpacked and checked as MGO1 Host does (mgo1host.exe.sha256)", ok25 and ok100)
        p = subprocess.run(["unshare", "-m", "--propagation", "private", "bash", "-c", INSIDE, "inside", R],
                           capture_output=True, text=True, timeout=600)
        if "MOUNT_FAILED" in p.stdout:
            print("skipped: no overlay mount here (" + p.stdout.strip() + ")")
            return 0
        rd = lambda n: open(os.path.join(R, n), encoding="utf-8", errors="replace").read() if os.path.exists(os.path.join(R, n)) else ""

        def js(n):
            try:
                return json.loads(rd(n).splitlines()[0])
            except (ValueError, IndexError):
                return {"raw": rd(n)}
        out25 = rd("out25.txt")
        check("1. beta 25 installed (install.sh by hand)", out25.rstrip().endswith("rc=0") and "== installed v150-beta25 593d64629f86" in out25, out25[-800:])
        v25, c25 = js("v25_version.json"), js("v25_check.json")
        check("1. before the update: the agent answers 3.7, the host v150-beta25 ...", v25.get("agent") == "3.7" and c25.get("version") == "v150-beta25 593d64629f86", (v25, c25))
        marks = rd("out100_marks.txt")
        check("2. the update as MGO1 Host runs it: exit 0, its last mark @is_installed|build 1.00 " + sha12,
              "MGO1HOST_INSTALL_EXIT=0" in marks and ("@is_installed|build 1.00 " + sha12) in marks.splitlines(), marks[-800:])
        check("2. ... and it said the restart of the running bot 1 (@is_restart|1)", "@is_restart|1" in marks.splitlines(), marks[-600:])
        hand = rd("out100_hand.txt")
        check("2. the same by hand: \"== installed build 1.00 " + sha12 + "\"", hand.rstrip().endswith("rc=0") and ("== installed build 1.00 " + sha12) in hand, hand[-800:])
        old = [l for l in (marks + hand).splitlines() if OLD_WORDS.search(l)]
        check("2. no line of the update names a beta, v150, 3.5 or 3.7", not old, old)
        v, s, c = js("v100_version.json"), js("v100_status.json"), js("v100_check.json")
        check("3. after it: version answers \"agent\": \"build 1.00\"", v.get("ok") is True and v.get("agent") == "build 1.00", v)
        check("3. ... status too (what MGO1 Host reads: its footer, the banner's test)", s.get("ok") is True and s.get("agent") == "build 1.00", s)
        check("3. ... install-check's host version \"build 1.00 " + sha12 + "\" (SERVER page, INSTALLED VERSION)", c.get("version") == "build 1.00 " + sha12, c)
        check("3. /opt/mgo1host/VERSION is \"build 1.00 " + sha12 + "\"", rd("v100_VERSION.txt").strip() == "build 1.00 " + sha12, rd("v100_VERSION.txt"))
        check("3. the installed agent's file says build 1.00 (the old 3.7 replaced)", rd("v100_agentfile.txt").strip() == "1", rd("v100_agentfile.txt"))
        check("the machine untouched (no /opt/mgo1host, /etc/mgo1host after the namespace)", not os.path.exists("/opt/mgo1host") and not os.path.exists("/etc/mgo1host"))
        # 4. MGO1 Host's judgement of the two answers against the agent it carries (host/mgo1host-agent's AGENT_VERSION)
        gxx = shutil.which("g++")
        if gxx:
            exe = os.path.join(R, "panel_version")
            b = subprocess.run([gxx, "-O1", "-std=c++17", "-I", os.path.join(ROOT, "hostpanel", "src"), os.path.join(HERE, "panel_version_test.cpp"), "-o", exe],
                               capture_output=True, text=True)
            if b.returncode == 0:
                carried = re.search(r'^AGENT_VERSION = "([^"]*)"', open(os.path.join(ROOT, "host", "mgo1host-agent")).read(), re.M).group(1)
                before = subprocess.run([exe, str(v25.get("agent")), carried], capture_output=True, text=True).stdout.strip()
                after = subprocess.run([exe, str(s.get("agent")), carried], capture_output=True, text=True).stdout.strip()
                check("4. MGO1 Host before the update: the server's %s older than its %s (the banner, UPDATE THE HOST)" % (v25.get("agent"), carried), before == "older", before)
                check("4. MGO1 Host after it: the server's %s not older (no banner)" % s.get("agent"), after == "not older", after)
            else:
                check("4. tests/panel_version_test.cpp compiled", False, b.stderr[-400:])
        else:
            print("(4. skipped: no g++)")
        if os.environ.get("BUILD_UPDATE_SHOW"):
            for n in ("out25.txt", "v25_version.json", "out100_marks.txt", "out100_hand.txt", "v100_version.json", "v100_check.json", "v100_VERSION.txt"):
                print("---- %s\n%s" % (n, rd(n).rstrip()))
    finally:
        shutil.rmtree(R, ignore_errors=True)
    print("\n%d check(s) failed" % len(FAILS) if FAILS else "\nall checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
