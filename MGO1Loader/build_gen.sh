#!/bin/bash
# The generated code (gen/) into objects, JOBS at a time (default: one per core, fewer when
# memory is short). A file is compiled when it has no object up to date at this level or
# above (newer than its source and than the runtime header ps2rt.h):
#   build_gen.sh        -O0 into obj/     (quick to build: development)
#   build_gen.sh O2     -O2 into objO2/   (the release: build_all.sh)
#   build_gen.sh list   the objects to link: objO2/<file>.o when up to date, else obj/<file>.o
# The shapes (gen/s_*.cpp, recomp/mips2shape.py) and the tables of the base and of each page
# (gen/g_*_table.cpp); the VU programs (gen/vu_prog_*.cpp, vu_programs.cpp) always -O2 into
# obj/ (their header vu_micro.h), the IOP's sound modules (gen/iop_*.cpp) as well (iop_rt.h).
# GEN / OBJ: another folder of generated code and its objects (GEN=gen2 OBJ=obj2: obj2, obj2O2).
cd "$(dirname "$0")"
MODE=${1:-O0}
GEN=${GEN:-gen}
OBJ=${OBJ:-obj}
fresh() { [ -f "$1" ] && [ "$1" -nt "$2" ] && [ "$1" -nt ps2rt.h ]; }
sources() { for f in $GEN/s_*.cpp $GEN/g_*.cpp $GEN/mgo1_*.cpp; do [ -f "$f" ] && echo "$f"; done; }
if [ "$MODE" = list ]; then
    for f in $(sources); do
        b=$(basename "${f%.cpp}")
        if fresh ${OBJ}O2/$b.o "$f"; then echo ${OBJ}O2/$b.o; else echo $OBJ/$b.o; fi
    done
    for f in $GEN/vu_prog_*.cpp $GEN/vu_programs.cpp $GEN/iop_*.cpp; do [ -f "$f" ] && echo $OBJ/$(basename "${f%.cpp}").o; done
    exit 0
fi
case "$MODE" in O0|O2) ;; *) echo "build_gen.sh [O0|O2|list]"; exit 2 ;; esac
mkdir -p $OBJ ${OBJ}O2
CXX=${CXX:-x86_64-w64-mingw32-g++}
if [ -z "$JOBS" ]; then
    JOBS=$(nproc 2>/dev/null || echo 2)
    mem=$(awk '/MemAvailable/ {print int($2 / 1048576)}' /proc/meminfo 2>/dev/null)
    # an -O2 job on the largest files takes about 0.4 GB
    [ "$MODE" = O2 ] && [ -n "$mem" ] && [ $((mem * 2)) -lt "$JOBS" ] && JOBS=$((mem * 2))
    [ "$JOBS" -ge 1 ] || JOBS=1
fi
todo=()
for f in $(sources); do
    b=$(basename "${f%.cpp}")
    fresh ${OBJ}O2/$b.o "$f" && continue
    [ "$MODE" = O0 ] && fresh $OBJ/$b.o "$f" && continue
    todo+=("$f")
done
for f in $GEN/vu_prog_*.cpp $GEN/vu_programs.cpp $GEN/iop_*.cpp; do
    [ -f "$f" ] || continue
    o=$OBJ/$(basename "${f%.cpp}").o
    case "$f" in */iop_*) h=iop_rt.h ;; *) h=vu_micro.h ;; esac
    [ -f "$o" ] && [ "$o" -nt "$f" ] && [ "$o" -nt $h ] || todo+=("$f")
done
[ ${#todo[@]} -gt 0 ] || { echo "generated code: up to date"; exit 0; }
echo "generated code: ${#todo[@]} file(s) at -$MODE, $JOBS at a time"
one() {
    f="$1"; b=$(basename "${f%.cpp}")
    case "$b" in vu_*|iop_*) lvl=-O2; o=$OBJ/$b.o ;; *) if [ "$MODE" = O2 ]; then lvl=-O2; o=${OBJ}O2/$b.o; else lvl=-O0; o=$OBJ/$b.o; fi ;; esac
    $CXX -std=c++17 -DMINGW_HAS_SECURE_API=1 -I. -I$GEN $lvl -w -c "$f" -o "$o.tmp" && mv "$o.tmp" "$o" || { echo "FAILED $f"; rm -f "$o.tmp"; return 1; }
}
export -f one
export MODE CXX GEN OBJ
printf '%s\n' "${todo[@]}" | xargs -P "$JOBS" -n 1 bash -c 'one "$0"' || { echo "generated code: some files failed"; exit 1; }
echo "generated code: done"
