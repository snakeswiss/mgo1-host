#!/bin/bash
# (build 1.00, srvbuild) The host built on the server (docs/DEDICATED_HOST.md section 27): mgo1host.exe made here from the
# sources of MGO1 Host's public package (mgo1host-src.tar.gz) and the disc's files MGO1 Host already sends, as the official
# build makes it (build_all.sh's steps for the North American disc, then host/build_host.sh). install.sh runs it when the
# package holds no mgo1host.exe; nothing of the game leaves this server.
#   bash build_server.sh --src <mgo1host-src.tar.gz> --disc <folder with MGS/NET/MGS3_N.ELF, STAGE_N.DAT, MODULES/MODNCD.IPK>
#                        --out <the exe to write> [--tree <build folder, default /opt/mgo1host/build>]
#   bash build_server.sh --need [--tree <folder>]     the room it needs here, in MB (the toolchain and the tree)
#   bash build_server.sh --check-disc --disc <folder>  the disc's version only (with tools/ps2_images.py next to host/)
# The steps (MGO1_MARKS=1: "@is_build|<step>", "@is_build_pct|<n>" every 30 s, "@ie_build|<line>", "@ie_space|<free MB>|<MB
# needed>"; else English lines):
#   deps     python3, the disc's version (the North American disc is needed), the sources into <tree>/src (only the files
#            that changed are written: what did not change is not compiled again), the MinGW-w64 toolchain (apt-get)
#   gen      the game's code from the disc: the code images, MIPS to C++ (tools/gen_versions.sh), the IOP's sound modules
#   compile  the generated code at -O2 (MGO1Loader/build_gen.sh: only the files without an up to date object)
#   link     host/build_host.sh: the VU programs, the platform, the dolls, the harness's main, the link, the strip
#   done     the executable; <tree>/built.txt says what it was built from (the same sources and disc again: no build)
# The tree is kept (about 200 MB): an update compiles only what changed. The whole log: /var/log/mgo1host-build.log.
set -uo pipefail
TREE=/opt/mgo1host/build; SRCTGZ=""; DISC=""; OUT=""; NEED_ONLY=0; CHECK_DISC=0
while [ $# -gt 0 ]; do
    case "$1" in
    --src) SRCTGZ=$2; shift 2 ;;
    --disc) DISC=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --tree) TREE=$2; shift 2 ;;
    --need) NEED_ONLY=1; shift ;;
    --check-disc) CHECK_DISC=1; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "build_server.sh: unknown option $1 (--help)"; exit 2 ;;
    esac
done
export LC_ALL=C                                  # the official build's order of files (globs, the link)
LOG=${MGO1_BUILD_LOG:-/var/log/mgo1host-build.log}
msg() {   # key, English text, arguments
    local k=$1 t=$2; shift 2
    if [ "${MGO1_MARKS:-0}" = 1 ]; then local IFS='|'; echo "@$k${*:+|$*}"; else echo "$t"; fi
}
# the room, in MB (measured on Ubuntu 24.04: docs/DEDICATED_HOST.md section 27): the toolchain, the tree at its largest
# during a first build, the tree's growth on an update, and a margin
NEED_TOOLS=600; NEED_TREE=700; NEED_UPDATE=250; NEED_MARGIN=200
TOOLS_PKGS="g++-mingw-w64-x86-64-win32 binutils-mingw-w64-x86-64"
# the official build's compilers: MinGW-w64 13's win32 threads variant (Ubuntu 24.04); an older one's win32 variant has no
# std::thread (Ubuntu 22.04's 10, Debian 12's 12: docs/BUILD.md), its posix variant then
gxx_major() { "x86_64-w64-mingw32-g++-$1" -dumpversion 2>/dev/null | sed "s/[^0-9].*//"; }
variant() {   # win32, posix, or nothing when no usable compiler is installed
    local m; m=$(gxx_major win32)
    if [ -n "$m" ] && [ "$m" -ge 13 ]; then echo win32
    elif [ -n "$(gxx_major posix)" ] && command -v x86_64-w64-mingw32-gcc-posix >/dev/null 2>&1; then echo posix; fi
}
have_tools() {
    local v; v=$(variant)
    [ -n "$v" ] && command -v "x86_64-w64-mingw32-gcc-$v" >/dev/null 2>&1 \
        && command -v x86_64-w64-mingw32-strip >/dev/null 2>&1 && command -v x86_64-w64-mingw32-ar >/dev/null 2>&1
}
need_mb() {
    local n=$NEED_MARGIN
    if ! have_tools; then
        n=$((n + NEED_TOOLS))
        . /etc/os-release 2>/dev/null
        [ "${ID:-}" = ubuntu ] && [ "${VERSION_ID%%.*}" -ge 24 ] 2>/dev/null || n=$((n + 250))    # (both variants)
    fi
    if [ -d "$TREE/src/MGO1Loader/objO2" ]; then n=$((n + NEED_UPDATE)); else n=$((n + NEED_TREE)); fi
    echo $n
}
free_mb() { local d=$1; while [ ! -d "$d" ]; do d=$(dirname "$d"); done; df -Pm "$d" | awk 'NR == 2 {print $4}'; }
disc_check() {   # the tools' folder: the North American program is the host's (the other versions are refused, by name)
    local V n
    V=$(python3 "$1/ps2_images.py" --which "$DISC/MGS/NET/MGS3_N.ELF" 2>/dev/null)
    case "$V" in
    US) return 0 ;;
    EU|JP|JPS|KR)
        case "$V" in EU) n="European (PAL)" ;; JP) n="Japanese" ;; JPS) n="Japanese standalone" ;; *) n="Korean" ;; esac
        rm -f "$STATE"
        msg ie_build "build: the disc's files are of the $n version of MGO1 ($V): the host is built from the North American disc (METAL GEAR SOLID 3: SUBSISTENCE, North America); give MGO1 Host that disc's folder" \
            "the disc's files are of the $n version of MGO1 ($V): the host is built from the North American disc (METAL GEAR SOLID 3: SUBSISTENCE, North America)"
        return 1 ;;
    *)  rm -f "$STATE"
        msg ie_build "build: the disc's MGS3_N.ELF is not one this build knows (a patched disc, or another game): the host is built from the North American MGO1 disc as it came" \
            "the disc's MGS3_N.ELF is not one this build knows (a patched disc, or another game): the host is built from the North American MGO1 disc as it came"
        return 1 ;;
    esac
}
if [ $NEED_ONLY = 1 ]; then need_mb; exit 0; fi
STATE="$TREE/.state"
if [ $CHECK_DISC = 1 ]; then          # (install.sh, before anything is installed: the tools next to this script)
    [ -n "$DISC" ] && [ -f "$DISC/MGS/NET/MGS3_N.ELF" ] || { echo "build_server.sh: --check-disc needs --disc"; exit 2; }
    command -v python3 >/dev/null 2>&1 || exit 0              # (no python3 yet: the build checks it)
    STATE=$(mktemp -u); disc_check "$(dirname "$0")/../tools"; exit $?
fi
[ -f "$SRCTGZ" ] && [ -n "$DISC" ] && [ -n "$OUT" ] || { echo "build_server.sh: --src, --disc and --out are needed (--help)"; exit 2; }
SRCTGZ=$(realpath "$SRCTGZ"); DISC=$(realpath "$DISC"); OUT=$(realpath -m "$OUT")

FREE=$(free_mb "$TREE"); NEED=$(need_mb)
if [ "$FREE" -lt "$NEED" ]; then
    msg ie_space "build: $FREE MB free on the server, the build needs $NEED MB (free some room, or a server with a larger disk)" "$FREE" "$NEED"
    exit 1
fi
mkdir -p "$TREE" "$(dirname "$LOG")" || { msg ie_build "build: cannot make $TREE" "cannot make $TREE"; exit 1; }
chmod 0755 "$TREE"
S="$TREE/src"
mkdir -p "$S"
{ echo; echo "==== $(date -u '+%Y-%m-%d %H:%M:%S') UTC: the host built on this server ($(nproc) cores, $FREE MB free)"; } >> "$LOG"
# STATE: the step, its start and its share (the progress ticker reads it)
T0=$(date +%s)
TICK=""
stage() {   # name, first percent, last percent, seconds it takes about (time-based progress), English text
    echo "$1 $2 $3 $(date +%s) $4" > "$STATE"
    echo "== $(date -u '+%H:%M:%S') $1" >> "$LOG"
    msg is_build "== building the host: $5" "$1"
    if [ -z "$TICK" ]; then ticker & TICK=$!; fi            # the progress, from the first step on
}
pct_now() {
    local name a b t0 est now n
    read -r name a b t0 est < "$STATE" 2>/dev/null || { echo 0; return; }
    now=$(date +%s)
    if [ "$name" = compile ] && [ -f "$TREE/.todo" ]; then
        local todo done
        todo=$(cat "$TREE/.todo"); done=$(find "$S/MGO1Loader/objO2" "$S/MGO1Loader/obj" -maxdepth 1 -name '*.o' -newer "$STATE" 2>/dev/null | wc -l)
        [ "$todo" -gt 0 ] || todo=1
        n=$((a + (b - a) * done / todo))
    else
        n=$((a + (b - a) * (now - t0) / (est > 0 ? est : 1)))
    fi
    [ "$n" -lt "$b" ] || n=$((b - 1))
    [ "$n" -ge "$a" ] || n=$a
    echo $n
}
ticker() {
    while sleep 30; do
        [ -f "$STATE" ] || break
        local p; p=$(pct_now)
        msg is_build_pct "building the host: $p % ($(( ($(date +%s) - T0) / 60 )) min)" "$p"
    done
}
fail() {   # step's log, English text: the most telling line of the log as @ie_build
    local line=""
    # the compiler's or the linker's first error, else the last line that says what failed, else the last line
    [ -f "$1" ] && line=$(grep -a -m 1 -E '(fatal )?error:|undefined reference|No space left|Killed|Cannot allocate' "$1")
    [ -n "$line" ] || line=$(grep -a -i -E 'error|failed|killed|not found|cannot|denied' "$1" 2>/dev/null | tail -n 1)
    [ -n "$line" ] || line=$(grep -a -v '^\s*$' "$1" 2>/dev/null | tail -n 1)
    [ -n "$line" ] || line=$2
    line=$(echo "$line" | tr '|\t' '  ' | cut -c1-220)
    [ -f "$1" ] && cat "$1" >> "$LOG"
    rm -f "$STATE"
    msg ie_build "build: $2: $line (the whole log: $LOG)" "$line"
    exit 1
}
STEP="$TREE/step.log"
run() {   # English text of the step, command...: its output in the step's log, the log's telling line on a failure
    local what=$1; shift
    "$@" > "$STEP" 2>&1 || fail "$STEP" "$what"
    cat "$STEP" >> "$LOG"
}
trap '[ -n "$TICK" ] && kill $TICK 2>/dev/null; rm -f "$STATE" "$TREE/.todo"' EXIT

# ---- deps: python3, the disc, the sources (nothing more when the host installed was built from them), the toolchain
export DEBIAN_FRONTEND=noninteractive
APT_UPDATED=0
apt_install() {   # packages: apt-get install, once updated
    if [ $APT_UPDATED = 0 ]; then apt-get update -y -q > "$STEP" 2>&1 || fail "$STEP" "apt-get update"; cat "$STEP" >> "$LOG"; APT_UPDATED=1; fi
    run "apt-get install $*" apt-get install -y -q --no-install-recommends "$@"
}
command -v python3 >/dev/null 2>&1 || apt_install python3
for f in MGS/NET/MGS3_N.ELF MGS/NET/STAGE_N.DAT MODULES/MODNCD.IPK; do
    [ -f "$DISC/$f" ] || { echo "no $DISC/$f" > "$STEP"; fail "$STEP" "the disc's files"; }
done
# the sources: only the files that changed are written (the others keep their date: their objects stay up to date)
run "the sources ($SRCTGZ)" python3 - "$SRCTGZ" "$S" <<'PY'
import hashlib, os, sys, tarfile
tgz, dst = sys.argv[1], sys.argv[2]
man = os.path.join(dst, ".sources")
old = set(open(man).read().split()) if os.path.exists(man) else set()
names, changed, whole = [], 0, hashlib.sha256()
with tarfile.open(tgz) as t:
    for m in sorted(t.getmembers(), key=lambda m: m.name):
        if not m.isfile():
            continue
        parts = m.name.split("/")[1:]                 # without the package's top folder
        if not parts or any(p in ("", ".", "..") for p in parts) or m.name.startswith("/"):
            sys.exit("a strange name in the package: " + m.name)
        rel = "/".join(parts)
        data = t.extractfile(m).read()
        whole.update(rel.encode() + b"\0" + hashlib.sha256(data).digest())
        p = os.path.join(dst, rel)
        names.append(rel)
        try:
            same = open(p, "rb").read() == data
        except OSError:
            same = False
        if same:
            continue
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p + ".new", "wb") as f:
            f.write(data)
        os.chmod(p + ".new", 0o755 if m.mode & 0o100 else 0o644)
        os.replace(p + ".new", p)
        changed += 1
for rel in sorted(old - set(names)):
    try:
        os.remove(os.path.join(dst, rel))
    except OSError:
        pass
open(man, "w").write("\n".join(names) + "\n")
open(os.path.join(dst, ".sources.sha256"), "w").write(whole.hexdigest() + "\n")
print("sources: %d files, %d written, %d removed" % (len(names), changed, len(old - set(names))))
PY
[ -f "$S/host/build_host.sh" ] && [ -f "$S/MGO1Loader/build_gen.sh" ] || { echo "the package holds no host/build_host.sh" > "$STEP"; fail "$STEP" "the sources"; }
VERSION_SRC=$(sed -n 's/^version=//p' "$S/SOURCE_INFO" 2>/dev/null)
EPOCH=$(sed -n 's/^source_date_epoch=//p' "$S/SOURCE_INFO" 2>/dev/null)
OFFICIAL=$(sed -n 's/^official_sha256=//p' "$S/SOURCE_INFO" 2>/dev/null)
disc_check "$S/tools" || exit 1
# the same sources and disc as the host installed now: nothing to build
DISC_SUM=$(cat "$DISC/MGS/NET/MGS3_N.ELF" "$DISC/MGS/NET/STAGE_N.DAT" "$DISC/MODULES/MODNCD.IPK" | sha256sum | cut -c1-64)
SRC_SUM=$(cat "$S/.sources.sha256")
if [ -f "$TREE/built.txt" ] && [ -f /opt/mgo1host/mgo1host.exe ]; then
    B_SRC=$(sed -n 's/^sources=//p' "$TREE/built.txt"); B_DISC=$(sed -n 's/^disc=//p' "$TREE/built.txt"); B_EXE=$(sed -n 's/^exe=//p' "$TREE/built.txt")
    if [ "$B_SRC" = "$SRC_SUM" ] && [ "$B_DISC" = "$DISC_SUM" ] && [ "$(sha256sum /opt/mgo1host/mgo1host.exe | cut -c1-64)" = "$B_EXE" ]; then
        cp /opt/mgo1host/mgo1host.exe "$OUT.new" && mv -f "$OUT.new" "$OUT"
        echo "up to date: the host installed was built from these sources and this disc" >> "$LOG"
        rm -f "$STATE"
        msg is_build "== building the host: up to date (built from these sources and this disc before)" done
        exit 0
    fi
fi
stage deps 0 8 150 "1/4 the tools ($(nproc) cores: the first build takes about $(( 60 / $(nproc) + 5 )) minutes, an update about 8)"
if ! have_tools; then
    [ "${MGO1_MARKS:-0}" = 1 ] || echo "(apt-get install $TOOLS_PKGS: a few minutes)"
    apt_install $TOOLS_PKGS
    have_tools || apt_install g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix     # (an older MinGW-w64: below 13)
fi
have_tools || { echo "no usable x86_64-w64-mingw32-g++ (win32 13 or later, or posix) after apt-get" > "$STEP"; fail "$STEP" "the toolchain"; }
# the compilers by their variant's name, whatever the server's alternatives point at
VARIANT=$(variant)
mkdir -p "$TREE/bin"
for t in g++ gcc; do ln -sf "$(command -v x86_64-w64-mingw32-$t-$VARIANT)" "$TREE/bin/x86_64-w64-mingw32-$t"; done
export PATH="$TREE/bin:$PATH"
x86_64-w64-mingw32-g++ --version 2>&1 | head -1 >> "$LOG"

# ---- gen: the game's code from the disc, written over the tree's only where it changed (unchanged files keep their objects)
stage gen 8 15 100 "2/4 the game's code from your disc"
IMG="$S/build/images/US"
rm -rf "$IMG" "$S/build/ipk" "$TREE/gen.new"; mkdir -p "$IMG" "$TREE/gen.new"
run "the code images" python3 "$S/tools/ps2_images.py" "$DISC/MGS/NET/MGS3_N.ELF" "$DISC/MGS/NET/STAGE_N.DAT" "$IMG"
run "MIPS to C++" bash "$S/tools/gen_versions.sh" "$TREE/gen.new" "US=$IMG/mgo1/ps2"
run "the page map" python3 "$S/tools/page_map.py" "$TREE/gen.new"
run "the IOP's modules" python3 "$S/recomp/ipk_extract.py" "$DISC/MODULES/MODNCD.IPK" "$S/build/ipk"
run "the IOP's sound to C++" python3 "$S/recomp/iop2cpp.py" "$S/build/ipk" "$TREE/gen.new/iop"
run "the generated code into the tree" python3 - "$TREE/gen.new" "$S/MGO1Loader/gen" <<'PY'
import os, shutil, sys
new, gen = sys.argv[1], sys.argv[2]
os.makedirs(gen, exist_ok=True)
names = set(os.listdir(new))
changed = 0
for n in sorted(names):
    a, b = os.path.join(new, n), os.path.join(gen, n)
    data = open(a, "rb").read()
    try:
        if open(b, "rb").read() == data:
            continue
    except OSError:
        pass
    os.replace(a, b)
    changed += 1
gone = [n for n in os.listdir(gen) if n not in names]
for n in gone:
    os.remove(os.path.join(gen, n))
shutil.rmtree(new)
print("generated code: %d files, %d new or changed, %d removed" % (len(names), changed, len(gone)))
PY

# ---- compile: the generated code at -O2 (the long part, once: an update compiles what changed)
cd "$S/MGO1Loader" || fail /dev/null "the tree"
JOBS=${JOBS:-$(nproc)}
mem=$(awk '/MemAvailable/ {print int($2 / 1048576)}' /proc/meminfo 2>/dev/null)
[ -n "$mem" ] && [ $((mem * 2)) -lt "$JOBS" ] && JOBS=$((mem * 2))      # an -O2 job on the largest files: about 0.4 GB
[ "$JOBS" -ge 1 ] || JOBS=1
export JOBS
# the number of files to compile (build_gen.sh's own rule), for the progress
TODO=0
for f in gen/s_*.cpp gen/g_*.cpp gen/mgo1_*.cpp gen/iop_*.cpp; do
    [ -f "$f" ] || continue
    b=$(basename "${f%.cpp}")
    case "$b" in iop_*) o=obj/$b.o; h=iop_rt.h ;; *) o=objO2/$b.o; h=ps2rt.h ;; esac
    [ -f "$o" ] && [ "$o" -nt "$f" ] && [ "$o" -nt "$h" ] || TODO=$((TODO + 1))
done
echo "$TODO" > "$TREE/.todo"
stage compile 15 80 1 "3/4 compiling the game's code ($TODO files, $JOBS at a time)"
run "compiling the game's code" nice -n 15 bash build_gen.sh O2

# ---- link: host/build_host.sh (its time stamps from SOURCE_INFO: the official build's)
stage link 80 100 $(( 900 / JOBS + 120 )) "4/4 the platform, the dolls and the link"
[ -n "$EPOCH" ] && export SOURCE_DATE_EPOCH=$EPOCH
rm -rf "$TREE/out"
run "the platform and the link" nice -n 15 bash "$S/host/build_host.sh" --out "$TREE/out"
[ -s "$TREE/out/mgo1host.exe" ] || { echo "no mgo1host.exe after host/build_host.sh" > "$STEP"; fail "$STEP" "the link"; }
SUM=$(sha256sum "$TREE/out/mgo1host.exe" | cut -c1-64)
mkdir -p "$(dirname "$OUT")"
cp "$TREE/out/mgo1host.exe" "$OUT.new" && mv -f "$OUT.new" "$OUT" || { echo "cannot write $OUT" > "$STEP"; fail "$STEP" "the result"; }
# what the next build makes again anyway (host/build_host.sh starts from an empty build/host): 170 MB less kept
rm -rf "$TREE/out" "$S/build/host" "$S/build/images" "$S/build/ipk"
printf 'version=%s\nsources=%s\ndisc=%s\nexe=%s\nbuilt=%s\nseconds=%s\ncores=%s\n' "$VERSION_SRC" "$SRC_SUM" "$DISC_SUM" "$SUM" \
    "$(date -u '+%Y-%m-%d %H:%M:%S')" "$(( $(date +%s) - T0 ))" "$(nproc)" > "$TREE/built.txt"
SAME=""
if [ -n "$OFFICIAL" ]; then
    # (the same compiler and sources: the same file when the official build took the same SOURCE_DATE_EPOCH; build 1.00's
    # took the clock: 12 bytes of time stamps differ, docs/DEDICATED_HOST.md section 27)
    if [ "$SUM" = "$OFFICIAL" ]; then SAME="the official build's very file"; else SAME="not the official build's file ${OFFICIAL:0:12} (its time stamps, or another compiler)"; fi
fi
echo "built: sha256 $SUM in $(( $(date +%s) - T0 )) s${SAME:+, $SAME}" >> "$LOG"
rm -f "$STATE"
msg is_build "== building the host: done in $(( ($(date +%s) - T0 + 30) / 60 )) min, mgo1host.exe ${SUM:0:12}${SAME:+ ($SAME)}" done
exit 0
