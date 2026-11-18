#!/bin/bash
# (v150-host beta 24, snakehouse) SNEAKING's Snake and every point of the game on each map given: tests/vdoll_stairs_test.cpp
# (docs/VIRTUAL_DOLL.md 43), the switches on (beta 24) and, with BEFORE=1, also MGO1_VDOLL_STAIRS=0 MGO1_VDOLL_VAULT=0
# MGO1_VDOLL_ITEM_WAY=0 (beta 23's bodies: the lines marked "0").
#   bash tests/vdoll_stairs_maps.sh /path/to/geom/*.geom
# The maps' collision files come from the owner's disc (python3 -I tools/stage_dat.py extract ...) and the added maps' folder;
# they are never put in the repository. RECAST_OBJS: Recast's objects already built (else compiled from MGO1Loader/recast). OUT:
# the folder for the build (default a temporary one). The exit code is 0 only when every map passed with the switch on.
set -u
cd "$(dirname "$0")/.."
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
RC=${RECAST_OBJS:-MGO1Loader/recast/*.cpp}
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_stairs_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
    MGO1Loader/doll_nav.cpp $RC -lpthread -o "$OUT/vdoll_stairs_test" || exit 2
fails=0
for m in "$@"; do
    name="$(basename "$(dirname "$m")")/$(basename "$m" | cut -c1-5)"
    "$OUT/vdoll_stairs_test" "$m" > "$OUT/one.txt" 2>&1; r=$?
    printf '%-16s 1 %s %s\n' "$name" "$([ $r -eq 0 ] && echo PASS || echo FAIL)" "$(grep -o '[0-9]* with no way out' "$OUT/one.txt") $(grep -c 'Snake at his start' "$OUT/one.txt") Snake starts"
    [ $r -eq 0 ] || { fails=$((fails + 1)); grep -E 'FAIL|no way out of|Snake at' "$OUT/one.txt"; }
    if [ "${BEFORE:-0}" != 0 ]; then
        MGO1_VDOLL_STAIRS=0 MGO1_VDOLL_VAULT=0 MGO1_VDOLL_ITEM_WAY=0 "$OUT/vdoll_stairs_test" "$m" > "$OUT/one0.txt" 2>&1; r=$?
        printf '%-16s 0 %s %s\n' "$name" "$([ $r -eq 0 ] && echo PASS || echo FAIL)" "$(grep -o '[0-9]* with no way out' "$OUT/one0.txt")"
        [ $r -eq 0 ] || grep -E 'FAIL|no way out of|Snake at' "$OUT/one0.txt"
    fi
done
echo "$([ $fails -eq 0 ] && echo ALL PASSED || echo "$fails FAILED")"
[ $fails -eq 0 ]
