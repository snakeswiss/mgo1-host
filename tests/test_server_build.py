#!/usr/bin/env python3
"""(build 1.00, srvbuild) MGO1 Host's public package: no mgo1host.exe (it is the game recompiled from the disc), the host
built on the user's server from the sources it carries (mgo1host-src.tar.gz) and the disc's files (host/build_server.sh,
docs/DEDICATED_HOST.md section 27). install.sh and build_server.sh run for real, as root, in a mount namespace whose /etc,
/opt, /var and /usr/local are overlays on folders of this test (nothing of the machine is written; a machine with an
install of its own is skipped). No disc file and no game code: the steps that need them (the generators, the compiles,
the link) are stand-ins in a stand-in source package around the real build_server.sh (the real build from a clean
Ubuntu 24.04: tools/dev150/cleanroom_build.sh).
  1. tools/server_src.py (with MinGW-w64 here): every file host/build_host.sh compiles, and the generated code's headers,
     in the sources' tarball; the same bytes twice; SOURCE_INFO; --check refuses a file generated from the game, a host
     executable and a disc's file, accepts the tarball;
  2. hostpanel/package.sh --source: server/ without mgo1host.exe.gz or mgo1host.exe.sha256, with mgo1host-src.tar.gz,
     VERSION "build 1.00 source"; tools/server_src.py --check accepts the folder;
  3. install.sh with neither an executable nor the sources: @ie_missing|mgo1host.exe;
  4. the disc's files of another version (EU): @ie_build|... says it and names the North American disc, before Wine,
     nothing installed;
  5. too little room (a 64 MB /opt): @ie_space|<free MB>|<MB needed>, the need over the free room, nothing installed;
  6. the build: the marks @is_build|deps, gen, compile, link, done in that order, @is_build_pct at least once a minute,
     the stand-in build_host.sh given the sources' SOURCE_DATE_EPOCH, /opt/mgo1host/mgo1host.exe the built file,
     VERSION "build 1.00 <its SHA-256, 12 digits>", @is_installed with it, build/built.txt, the agent's install-check
     "build" (the tree, the executable built);
  7. the same package and disc again: @is_build|done alone (no step), the same executable;
  8. an update with one source changed: only that file written into the tree (the others keep their date), the
     generated code not compiled again (unchanged), a new executable and VERSION;
  9. a failing compile: @ie_build|<the compiler's error line>, exit 1, the installed executable unchanged;
 10. the owner's package WITH an executable: no @is_build at all, installed as before.
    sudo python3 tests/test_server_build.py
"""
import gzip, hashlib, io, json, os, re, shutil, subprocess, sys, tarfile, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.realpath(os.path.join(HERE, ".."))
FAILS = []

FAKE_WINE = "#!/bin/sh\n[ \"$1\" = --version ] && echo 'wine-9.0 (stand-in)'\nexit 0\n"
FAKE_SYSTEMCTL = "#!/bin/sh\ncase \"$1\" in is-active) exit 3 ;; esac\nexit 0\n"
FAKE_APT = "#!/bin/sh\necho \"apt-get $* (the test's stand-in refuses)\" >&2\nexit 1\n"
FAKE_TOOL = "#!/bin/sh\n[ \"$1\" = -dumpversion ] && { echo 13-win32; exit 0; }\necho 'stand-in MinGW-w64 tool'\nexit 0\n"

# the stand-in source package: the real host/build_server.sh, stand-ins for what needs the disc or the toolchain
STANDIN = {
    "tools/ps2_images.py": r'''import os, sys
if sys.argv[1] == "--which":
    t = open(sys.argv[2]).read()
    for v in ("US", "EU", "JP", "KR"):
        if v + "-STANDIN" in t:
            print(v); sys.exit(0)
    sys.exit(sys.argv[2] + ": not a version of MGO1 this build knows")
out = os.path.join(sys.argv[3], "mgo1", "ps2"); os.makedirs(out, exist_ok=True)
open(os.path.join(out, "mgs3_n_base.bin"), "w").write("stand-in image\n")
''',
    "tools/stage_dat.py": "# stand-in\n",
    "tools/gen_versions.sh": '''#!/bin/bash
# stand-in: three generated files from the package's SEED (the same seed: the same files)
H=$(cd "$(dirname "$0")/.." && pwd)
for n in s_a s_b g_base_table; do echo "// stand-in $n $(cat "$H/SEED")" > "$1/$n.cpp"; done
''',
    "tools/page_map.py": "import sys\nopen(sys.argv[1] + '/g_page_map.cpp', 'w').write('// stand-in page map\\n')\n",
    "recomp/ipk_extract.py": "import os, sys\nos.makedirs(sys.argv[2], exist_ok=True)\nopen(sys.argv[2] + '/sdlib.irx', 'w').write('x')\n",
    "recomp/iop2cpp.py": "import sys\nopen(sys.argv[2] + '_table.cpp', 'w').write('// stand-in iop\\n')\n",
    "MGO1Loader/ps2rt.h": "// stand-in runtime header\n",
    "MGO1Loader/iop_rt.h": "// stand-in\n",
    "MGO1Loader/build_gen.sh": '''#!/bin/bash
# stand-in for the generated code's compile: build_gen.sh's rule (an object newer than its source and ps2rt.h)
cd "$(dirname "$0")"; mkdir -p obj objO2
for f in gen/*.cpp; do
    b=$(basename "${f%.cpp}"); case "$b" in iop_*) o=obj/$b.o ;; *) o=objO2/$b.o ;; esac
    [ -f "$o" ] && [ "$o" -nt "$f" ] && [ "$o" -nt ps2rt.h ] && continue
    if grep -q BREAK "$f"; then echo "$f:1:1: error: stand-in compile error"; echo "generated code: some files failed"; exit 1; fi
    sleep 1; echo "object of $f" > "$o"; echo "compiled $f" >> ../compiled.log
done
echo "generated code: done"
''',
    "MGO1Loader/x.cpp": "// a platform file, version 1\n",
    "host/build_host.sh": '''#!/bin/bash
# stand-in for the link: the executable from the objects and the platform file, with SOURCE_DATE_EPOCH in it
ROOT=$(cd "$(dirname "$0")/.." && pwd); OUT=$2
sleep "$(cat "$ROOT/LINK_SECONDS")"
mkdir -p "$OUT"
{ echo "stand-in mgo1host.exe"; cat "$ROOT"/MGO1Loader/objO2/*.o "$ROOT/MGO1Loader/x.cpp"; echo "epoch=${SOURCE_DATE_EPOCH:-none}"; } > "$OUT/mgo1host.exe"
''',
}


def check(what, cond, detail=""):
    print(("ok    " if cond else "FAIL  ") + what + (("  (" + str(detail)[:700] + ")") if detail and not cond else ""))
    if not cond:
        FAILS.append(what)


def standin_tarball(path, seed, xcpp, link_seconds):
    files = dict(STANDIN)
    files["host/build_server.sh"] = open(os.path.join(ROOT, "host", "build_server.sh")).read()
    files["SOURCE_INFO"] = "version=build 1.00\nsource_date_epoch=1795012951\n"
    files["SEED"] = seed + "\n"
    files["LINK_SECONDS"] = "%d\n" % link_seconds
    files["MGO1Loader/x.cpp"] = xcpp
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as t:
        for rel in sorted(files):
            data = files[rel].encode()
            ti = tarfile.TarInfo("mgo1host-src/" + rel)
            ti.size, ti.mode, ti.mtime = len(data), 0o755 if rel.endswith(".sh") else 0o644, 1795012951
            t.addfile(ti, io.BytesIO(data))
    with gzip.GzipFile(path, "wb", mtime=0) as g:
        g.write(buf.getvalue())


def server_folder(dst, source=None, exe_text=None):
    """hostpanel/package.sh's server folder: --source <tarball> (the public package) or --host-exe (the owner's)"""
    work = dst + ".pkg"
    os.makedirs(work)
    gui = os.path.join(work, "MGO1 Host.exe")
    open(gui, "w").write("stand-in\n")
    args = ["--source", source] if source else ["--host-exe", os.path.join(work, "mgo1host.exe")]
    if not source:
        open(os.path.join(work, "mgo1host.exe"), "w").write(exe_text)
    p = subprocess.run(["bash", os.path.join(ROOT, "hostpanel", "package.sh")] + args + ["--version", "build 1.00", "--out", work],
                       capture_output=True, text=True, env=dict(os.environ, GUI=gui, DEPS=os.path.join(work, "nodeps")))
    if p.returncode != 0:
        raise SystemExit("package.sh failed: " + p.stdout + p.stderr)
    shutil.move(os.path.join(work, "MGO1 Host", "server"), dst)
    shutil.rmtree(work)
    if not source:                                   # as MGO1 Host's stage: the executable unpacked
        with open(os.path.join(dst, "mgo1host.exe.gz"), "rb") as f:
            open(os.path.join(dst, "mgo1host.exe"), "wb").write(gzip.decompress(f.read()))
        os.remove(os.path.join(dst, "mgo1host.exe.gz"))


def disc(dst, version):
    for d in ("MGS/NET", "MODULES"):
        os.makedirs(os.path.join(dst, "disc", d), exist_ok=True)
    open(os.path.join(dst, "disc/MGS/NET/MGS3_N.ELF"), "w").write("%s-STANDIN, not a disc file\n" % version)
    for f in ("STAGE_N.DAT", "SLOT_N.DAT"):
        open(os.path.join(dst, "disc/MGS/NET", f), "w").write("stand-in, not a disc file\n")
    open(os.path.join(dst, "disc/MODULES/MODNCD.IPK"), "w").write("stand-in\n")


INSIDE = r'''
set -u
R=$1
for d in etc opt var usr/local; do
    n=${d//\//_}
    mkdir -p "$R/ovl/$n/up" "$R/ovl/$n/work"
    mount -t overlay overlay -o lowerdir=/$d,upperdir=$R/ovl/$n/up,workdir=$R/ovl/$n/work /$d || { echo "MOUNT_FAILED $d"; exit 3; }
done
export PATH=$R/bin:$PATH
run() {   # name, stage folder: install.sh as MGO1 Host runs it, each line with its second
    local t0=$(date +%s)
    env MGO1_MARKS=1 bash "$2/install.sh" 2>&1 | while IFS= read -r l; do echo "$(( $(date +%s) - t0 )) $l"; done > "$R/$1.txt"
    echo "EXIT=${PIPESTATUS[0]}" >> "$R/$1.txt"
    sha256sum /opt/mgo1host/mgo1host.exe > "$R/$1.exe" 2>/dev/null; cat /opt/mgo1host/VERSION > "$R/$1.version" 2>/dev/null
}
run out3 "$R/s3"
run out4 "$R/s4"
ls /opt/mgo1host > "$R/out4.ls" 2>&1
mount -t tmpfs -o size=64m tmpfs /opt && { run out5 "$R/s6"; ls -A /opt > "$R/out5.ls"; umount /opt; }
run out6 "$R/s6"
cp /opt/mgo1host/build/built.txt "$R/out6.built" 2>/dev/null
cp /opt/mgo1host/mgo1host.exe "$R/out6.exefile" 2>/dev/null
mgo1host-agent install-check > "$R/out6.check" 2>&1
run out7 "$R/s6"
T=/opt/mgo1host/build/src
stat -c '%Y %n' $T/tools/gen_versions.sh $T/MGO1Loader/x.cpp $T/MGO1Loader/objO2/s_a.o > "$R/out8.before"
: > $T/compiled.log
sleep 1
run out8 "$R/s8"
stat -c '%Y %n' $T/tools/gen_versions.sh $T/MGO1Loader/x.cpp $T/MGO1Loader/objO2/s_a.o > "$R/out8.after"
cp $T/compiled.log "$R/out8.compiled"
run out9 "$R/s9"
run out10 "$R/s10"
'''


def part_sources(R):
    gxx = shutil.which("x86_64-w64-mingw32-g++")
    if not gxx:
        print("(1. skipped: no x86_64-w64-mingw32-g++ to find the headers)")
        return
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import server_src
    a, b = os.path.join(R, "a.tar.gz"), os.path.join(R, "b.tar.gz")
    pa = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "server_src.py"), "--out", a], capture_output=True, text=True)
    pb = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "server_src.py"), "--out", b], capture_output=True, text=True)
    check("1. tools/server_src.py makes the sources' tarball", pa.returncode == 0 and os.path.exists(a), pa.stdout + pa.stderr)
    if pa.returncode != 0:
        return
    names = set(n.split("/", 1)[1] for n in tarfile.open(a).getnames())
    units = server_src.host_units()
    check("1. ... every file host/build_host.sh compiles is in it (%d)" % len(units), all(u in names for u in units), [u for u in units if u not in names])
    need = ["host/build_server.sh", "host/build_host.sh", "MGO1Loader/build_gen.sh", "MGO1Loader/ps2rt.h", "MGO1Loader/vu_micro.h",
            "MGO1Loader/iop_rt.h", "tools/gen_versions.sh", "tools/build_opus.sh", "recomp/vu_programs.txt", "SOURCE_INFO", "README.txt",
            "third_party/opus/COPYING", "MGO1Loader/recast/License.txt"]
    check("1. ... the build's scripts, the generated code's headers, the lists, the licenses", all(n in names for n in need), [n for n in need if n not in names])
    check("1. ... no test data, no document, no MGO1 PC program (" + str(len(names)) + " files)",
          not any(n.startswith(("docs/", "tests/bench", "tests/steps", "launcher/", "mod/", "hostpanel/")) for n in names) and len(names) < 450,
          sorted(n for n in names if n.startswith(("docs/", "launcher/", "mod/", "hostpanel/")))[:10])
    check("1. ... the same bytes twice (the same sources)", pb.returncode == 0 and open(a, "rb").read() == open(b, "rb").read())
    info = tarfile.open(a).extractfile("mgo1host-src/SOURCE_INFO").read().decode()
    check("1. ... SOURCE_INFO: the version and the commit's time stamp", "version=build 1.00" in info and re.search(r"^source_date_epoch=\d{10}$", info, re.M), info)
    c = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "server_src.py"), "--check", a], capture_output=True, text=True)
    check("1. --check accepts the tarball", c.returncode == 0, c.stdout)
    for name, rel, text in (("a file generated from the game", "x/MGO1Loader/gen/s_x.cpp", "// generated by mips2cpp from the disc\n"),
                            ("a generated file outside gen/", "x/s_y.cpp", "// generated by recomp/vu2cpp\n"),
                            ("a host executable", "x/server/mgo1host.exe.gz", "stand-in\n"),
                            ("a disc's file", "x/disc/MGS/NET/STAGE_N.DAT", "stand-in\n")):
        d = os.path.join(R, "chk_" + re.sub(r"\W", "_", name))
        os.makedirs(os.path.dirname(os.path.join(d, rel)))
        open(os.path.join(d, rel), "w").write(text)
        c = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "server_src.py"), "--check", d], capture_output=True, text=True)
        check("1. --check refuses " + name, c.returncode == 1 and "REFUSED" in c.stdout, c.stdout)


def main():
    if os.geteuid() != 0 or not shutil.which("unshare"):
        print("skipped: needs root and unshare (a mount namespace with overlays)")
        return 0
    for p in ("/opt/mgo1host", "/etc/mgo1host", "/var/lib/mgo1host"):
        if os.path.exists(p):
            print("skipped: this machine has an install of its own (%s)" % p)
            return 0
    R = os.path.realpath(tempfile.mkdtemp(prefix="serverbuild_"))
    try:
        part_sources(R)
        os.makedirs(os.path.join(R, "bin"))
        for name, text in (("wine64", FAKE_WINE), ("systemctl", FAKE_SYSTEMCTL), ("apt-get", FAKE_APT),
                           ("x86_64-w64-mingw32-g++-win32", FAKE_TOOL), ("x86_64-w64-mingw32-gcc-win32", FAKE_TOOL),
                           ("x86_64-w64-mingw32-strip", FAKE_TOOL), ("x86_64-w64-mingw32-ar", FAKE_TOOL)):
            open(os.path.join(R, "bin", name), "w").write(text)
            os.chmod(os.path.join(R, "bin", name), 0o755)
        for n, seed, xcpp, link in (("A", "seed 1", "// a platform file, version 1\n", 40), ("B", "seed 1", "// a platform file, version 2\n", 1),
                                    ("C", "seed 2 BREAK", "// a platform file, version 2\n", 1)):
            standin_tarball(os.path.join(R, "src%s.tar.gz" % n), seed, xcpp, link)
        server_folder(os.path.join(R, "s3"), source=os.path.join(R, "srcA.tar.gz"))
        p2 = os.path.join(R, "s3")
        listing = sorted(os.listdir(p2))
        check("2. package.sh --source: server/ holds mgo1host-src.tar.gz, no mgo1host.exe.gz nor .sha256",
              "mgo1host-src.tar.gz" in listing and not any(n.startswith("mgo1host.exe") for n in listing), listing)
        check("2. ... VERSION \"build 1.00 source\"", open(os.path.join(p2, "VERSION")).read().strip() == "build 1.00 source")
        c = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "server_src.py"), "--check", p2], capture_output=True, text=True)
        check("2. tools/server_src.py --check accepts the public server folder", c.returncode == 0, c.stdout)
        os.remove(os.path.join(p2, "mgo1host-src.tar.gz"))                     # 3: neither the executable nor the sources
        disc(p2, "US")
        for s, tgz, v in (("s4", "srcA", "EU"), ("s6", "srcA", "US"), ("s8", "srcB", "US"), ("s9", "srcC", "US")):
            server_folder(os.path.join(R, s), source=os.path.join(R, tgz + ".tar.gz"))
            disc(os.path.join(R, s), v)
        server_folder(os.path.join(R, "s10"), exe_text="the owner's host (stand-in)\n")
        disc(os.path.join(R, "s10"), "US")
        p = subprocess.run(["unshare", "-m", "--propagation", "private", "bash", "-c", INSIDE, "inside", R],
                           capture_output=True, text=True, timeout=1200)
        if "MOUNT_FAILED" in p.stdout:
            print("skipped: no overlay mount here (" + p.stdout.strip() + ")")
            return 0
        rd = lambda n: open(os.path.join(R, n), encoding="utf-8", errors="replace").read() if os.path.exists(os.path.join(R, n)) else ""

        def marks(n):
            return [l.split(" ", 1)[1] for l in rd(n + ".txt").splitlines() if " @" in l]

        def timed(n):
            return [(int(l.split(" ", 1)[0]), l.split(" ", 1)[1]) for l in rd(n + ".txt").splitlines() if re.match(r"\d+ ", l)]
        m3 = marks("out3")
        check("3. neither an executable nor the sources: @ie_missing|mgo1host.exe, exit 1", "@ie_missing|mgo1host.exe" in m3 and "EXIT=1" in rd("out3.txt"), rd("out3.txt")[-500:])
        m4 = marks("out4")
        e4 = [l for l in m4 if l.startswith("@ie_build|")]
        check("4. the EU disc: @ie_build names the version found and the North American disc",
              len(e4) == 1 and "European" in e4[0] and "(EU)" in e4[0] and "North American" in e4[0] and "EXIT=1" in rd("out4.txt"), rd("out4.txt")[-600:])
        check("4. ... before Wine, nothing installed", "@is_wine" not in m4 and "mgo1host.exe" not in rd("out4.ls"), (m4, rd("out4.ls")))
        e5 = [l for l in marks("out5") if l.startswith("@ie_space|")]
        ok5 = len(e5) == 1 and len(e5[0].split("|")) == 3 and int(e5[0].split("|")[2]) > int(e5[0].split("|")[1])
        check("5. 64 MB free: @ie_space|<free MB>|<MB needed>, the need over the free room", ok5 and "EXIT=1" in rd("out5.txt"), rd("out5.txt")[-400:])
        check("5. ... nothing installed (no Wine step, /opt empty)", "@is_wine" not in marks("out5") and rd("out5.ls").strip() == "", (marks("out5"), rd("out5.ls")))
        m6, t6 = marks("out6"), timed("out6")
        steps = [l.split("|")[1] for l in m6 if l.startswith("@is_build|")]
        check("6. the build: @is_build|deps, gen, compile, link, done in that order", steps == ["deps", "gen", "compile", "link", "done"], steps)
        bt = [t for t, l in t6 if l.startswith("@is_build|")]
        pt = [t for t, l in t6 if l.startswith(("@is_build|", "@is_build_pct|"))]
        gaps = [b - a for a, b in zip(pt, pt[1:])]
        pcts = [int(l.split("|")[1]) for t, l in t6 if l.startswith("@is_build_pct|")]
        check("6. ... @is_build_pct at least once a minute while it builds (%d lines, the longest gap %s s)" % (len(pcts), max(gaps or [0])),
              pcts and max(gaps or [99]) <= 60 and bt[-1] - bt[0] >= 40, (t6[:30], gaps))
        check("6. ... the percents between 0 and 100, never going back", all(0 <= p <= 100 for p in pcts) and pcts == sorted(pcts), pcts)
        exe6 = rd("out6.exefile")
        sha6 = hashlib.sha256(exe6.encode()).hexdigest()
        check("6. ... /opt/mgo1host/mgo1host.exe the built file, linked with the sources' SOURCE_DATE_EPOCH",
              exe6.startswith("stand-in mgo1host.exe") and "epoch=1795012951" in exe6 and "version 1" in exe6, exe6[:300])
        check("6. ... VERSION \"build 1.00 %s\" and @is_installed with it" % sha6[:12],
              rd("out6.version").strip() == "build 1.00 " + sha6[:12] and ("@is_installed|build 1.00 " + sha6[:12]) in m6 and "EXIT=0" in rd("out6.txt"),
              (rd("out6.version"), m6[-3:]))
        check("6. ... build/built.txt: the sources, the disc, the executable", "exe=" + sha6 in rd("out6.built") and "sources=" in rd("out6.built"), rd("out6.built"))
        try:
            b6 = json.loads(rd("out6.check").splitlines()[0]).get("build", {})
        except (ValueError, IndexError):
            b6 = {"raw": rd("out6.check")[:300]}
        check("6. ... the agent's install-check: \"build\": the tree, built, its executable",
              b6.get("tree") is True and b6.get("built") is True and b6.get("exe") == sha6[:12] and b6.get("version") == "build 1.00", b6)
        m7 = marks("out7")
        s7 = [l for l in m7 if l.startswith(("@is_build|", "@is_build_pct|"))]
        check("7. the same package and disc again: @is_build|done alone, the same executable",
              s7 == ["@is_build|done"] and rd("out7.exe") == rd("out6.exe") and "EXIT=0" in rd("out7.txt"), (s7, rd("out7.txt")[-400:]))
        before = dict(reversed(l.split(" ", 1)) for l in rd("out8.before").splitlines())
        after = dict(reversed(l.split(" ", 1)) for l in rd("out8.after").splitlines())
        gv = [n for n in before if n.endswith("gen_versions.sh")][0]
        xc = [n for n in before if n.endswith("x.cpp")][0]
        so = [n for n in before if n.endswith("s_a.o")][0]
        check("8. an update with one source changed: only that file written (the others keep their date)",
              before[gv] == after[gv] and before[xc] != after[xc], (before, after))
        check("8. ... the generated code unchanged: not compiled again", before[so] == after[so] and rd("out8.compiled").strip() == "", (rd("out8.compiled"), before, after))
        steps8 = [l.split("|")[1] for l in marks("out8") if l.startswith("@is_build|")]
        check("8. ... built again (deps to done), a new executable and VERSION",
              steps8 == ["deps", "gen", "compile", "link", "done"] and rd("out8.exe") != rd("out6.exe") and rd("out8.version").strip().split()[-1] == rd("out8.exe")[:12],
              (steps8, rd("out8.exe"), rd("out8.version")))
        e9 = [l for l in marks("out9") if l.startswith("@ie_build|")]
        check("9. a failing compile: @ie_build|<the compiler's error line>, exit 1", len(e9) == 1 and "error: stand-in compile error" in e9[0] and "EXIT=1" in rd("out9.txt"), rd("out9.txt")[-500:])
        check("9. ... the installed executable unchanged", rd("out9.exe") == rd("out8.exe"))
        m10 = marks("out10")
        sha10 = hashlib.sha256(b"the owner's host (stand-in)\n").hexdigest()[:12]
        check("10. the owner's package with an executable: no @is_build, installed as before (@is_installed|build 1.00 %s)" % sha10,
              not any(l.startswith(("@is_build", "@ie_build")) for l in m10) and ("@is_installed|build 1.00 " + sha10) in m10 and "EXIT=0" in rd("out10.txt"), m10)
        check("the machine untouched (no /opt/mgo1host, /etc/mgo1host after the namespace)", not os.path.exists("/opt/mgo1host") and not os.path.exists("/etc/mgo1host"))
        if os.environ.get("SERVER_BUILD_SHOW"):
            for n in ("out3", "out4", "out5", "out6", "out7", "out8", "out9", "out10"):
                print("---- %s\n%s" % (n, rd(n + ".txt").rstrip()))
    finally:
        shutil.rmtree(R, ignore_errors=True)
    print("\n%d check(s) failed" % len(FAILS) if FAILS else "\nall checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
