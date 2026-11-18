#!/bin/bash
# The dedicated host's executable, mgo1host.exe (docs/DEDICATED_HOST.md): the game's code as
# build_all.sh generated and compiled it from your MGO1 disc, the VU programs with every VU0 one in
# the static form (recomp/vu2cpp.py --vu0-static, into build/host: the player build keeps the ones
# that read their status in the full form), the platform, the dolls and the harness's main
# (tests/ps2world_test.cpp: the steps, the commands).
#   bash build_all.sh --disc <your MGO1 disc> --no-mod     once: the code generated and compiled
#   bash host/build_host.sh [--out <folder>]               then the host (default: host/)
set -e -o pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/host"
while [ $# -gt 0 ]; do
    case "$1" in
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) echo "unknown option $1 (--help)"; exit 2 ;;
    esac
done
# (build 1.00, srvbuild) the same file from the same sources (docs/DEDICATED_HOST.md section 27): the link's and the strip's
# time stamp and doll_nav.cpp's __DATE__ __TIME__ from SOURCE_DATE_EPOCH, when not given the time of the sources' commit
# (SOURCE_INFO in a server's source package, else git's HEAD when no file of it has changed), else the clock as before
if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
    if [ -f "$ROOT/SOURCE_INFO" ]; then SOURCE_DATE_EPOCH=$(sed -n 's/^source_date_epoch=//p' "$ROOT/SOURCE_INFO")
    elif git -C "$ROOT" diff --quiet HEAD -- 2>/dev/null; then SOURCE_DATE_EPOCH=$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null); fi
    if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then export SOURCE_DATE_EPOCH; else unset SOURCE_DATE_EPOCH; fi
fi
IMAGES="$ROOT/build/images/US/mgo1/ps2"          # the North American program: the host's and the dolls'
L="$ROOT/MGO1Loader"
[ -f "$IMAGES/mgs3_n_base.bin" ] && [ -f "$L/gen/g_base_table.cpp" ] \
    || { echo "build_host.sh: no generated code yet: bash build_all.sh --disc <your MGO1 disc> --no-mod first"; exit 1; }
CXX="x86_64-w64-mingw32-g++ -std=c++17 -DMINGW_HAS_SECURE_API=1 -I$L -I$L/gen -I$L/gl -w"
H="$ROOT/build/host"
rm -rf "$H"; mkdir -p "$H/vu" "$H/obj"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}

echo "== the VU programs, every VU0 one in the static form"
python3 "$ROOT/recomp/vu2cpp.py" --manifest "$ROOT/recomp/vu_programs.txt" --images "$IMAGES" --vu0-static -o "$H/vu"
ls "$H"/vu/*.cpp | xargs -P "$JOBS" -I{} sh -c "$CXX -O2 -c {} -o $H/obj/\$(basename {} .cpp).o"

echo "== the game's code at -O2 (what build_all.sh has not compiled yet)"
(cd "$L" && bash build_gen.sh O2)

echo "== the platform and the harness's main"
cd "$L"
$CXX -O1 -c ps2rt.cpp -o "$H/obj/ps2rt.o"
$CXX -O1 -c ps2world.cpp -o "$H/obj/ps2world.o"
for f in vu0 gs gs_gpu mmi inflate deflate extramaps stagedat disc iop spu2 hdtex png bcn doll doll_brain doll_probe doll_geom doll_nav doll_radio vdoll vdoll_body; do $CXX -O2 -c $f.cpp -o "$H/obj/$f.o"; done
# the PLAYABLE DOLL's navigation: Recast & Detour (zlib license, MGO1Loader/recast/License.txt)
mkdir -p "$H/obj/recast"
ls recast/*.cpp | xargs -P "$JOBS" -I{} sh -c "$CXX -O2 -c {} -o $H/obj/recast/\$(basename {} .cpp).o"
$CXX -O1 -I../third_party/opus/include -c ../tests/ps2world_test.cpp -o "$H/obj/main.o"
bash ../tools/build_opus.sh "$H/obj/libopus.a"         # the voice between PCs (the harness's SIM_VOICE_OPUS)
# the game's objects, the VU programs of the host's instead of the player's
GEN=$(bash build_gen.sh list | tr ' ' '\n' | grep -v -E '^obj/vu_prog|^obj/vu_programs' | tr '\n' ' ')
x86_64-w64-mingw32-g++ -static -o "$H/mgo1host.exe" "$H"/obj/main.o "$H"/obj/ps2world.o "$H"/obj/inflate.o "$H"/obj/deflate.o "$H"/obj/extramaps.o "$H"/obj/stagedat.o "$H"/obj/disc.o \
    "$H"/obj/iop.o "$H"/obj/spu2.o "$H"/obj/ps2rt.o "$H"/obj/vu0.o "$H"/obj/gs.o "$H"/obj/gs_gpu.o "$H"/obj/mmi.o \
    "$H"/obj/hdtex.o "$H"/obj/png.o "$H"/obj/bcn.o "$H"/obj/doll.o "$H"/obj/doll_brain.o "$H"/obj/doll_probe.o "$H"/obj/doll_geom.o "$H"/obj/doll_nav.o "$H"/obj/doll_radio.o "$H"/obj/vdoll.o "$H"/obj/vdoll_body.o "$H"/obj/recast/*.o \
    $GEN "$H"/obj/vu_prog_*.o "$H"/obj/vu_programs.o "$H"/obj/libopus.a -lws2_32 -liphlpapi -lwinmm -lpsapi
mkdir -p "$OUT"
x86_64-w64-mingw32-strip -o "$OUT/mgo1host.exe" "$H/mgo1host.exe"
ls -la "$OUT/mgo1host.exe"
