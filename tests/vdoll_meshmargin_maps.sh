#!/bin/bash
# (v150-host beta 25, meshmargin) the bodies' mesh against the walls on each map given: tests/vdoll_meshmargin_test.cpp
# (docs/VIRTUAL_DOLL.md 50), the switch on and, with BEFORE=1, also MGO1_VDOLL_MESH_MARGIN=0 (beta 25's mesh: the lines marked "0").
#   bash tests/vdoll_meshmargin_maps.sh /path/to/geom/*.geom
# The maps' collision files come from the owner's disc (python3 -I tools/stage_dat.py extract ...) and the added maps' folder;
# they are never put in the repository. RECAST_OBJS: Recast's objects already built (else compiled from MGO1Loader/recast). OUT:
# the folder for the build and each map's full output (default a temporary one). The exit code is 0 only when every map passed
# with the switch on.
set -u
cd "$(dirname "$0")/.."
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
RC=${RECAST_OBJS:-MGO1Loader/recast/*.cpp}
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_meshmargin_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
    MGO1Loader/doll_nav.cpp $RC -lpthread -o "$OUT/vdoll_meshmargin_test" || exit 2
fails=0
for m in "$@"; do
    name="$(basename "$m" | cut -c1-5)"
    "$OUT/vdoll_meshmargin_test" "$m" > "$OUT/$name.txt" 2>&1; r=$?
    printf '%-6s 1 %s %s; %s\n' "$name" "$([ $r -eq 0 ] && echo PASS || echo FAIL)" "$(grep -o 'the margin: [0-9]* standing places ([^)]*), [0-9]* nearer' "$OUT/$name.txt")" \
        "$(grep -o 'the standing area .*stairs [0-9]* -> [0-9]*' "$OUT/$name.txt")"
    [ $r -eq 0 ] || { fails=$((fails + 1)); grep -E '^FAIL|LOST|no way now' "$OUT/$name.txt"; }
    if [ "${BEFORE:-0}" != 0 ]; then
        MGO1_VDOLL_MESH_MARGIN=0 "$OUT/vdoll_meshmargin_test" "$m" > "$OUT/${name}_0.txt" 2>&1; r=$?
        printf '%-6s 0 %s %s\n' "$name" "$([ $r -eq 0 ] && echo PASS || echo FAIL)" "$(grep -o 'beta 25: [0-9]* standing places .* places\(, the nearest [0-9]* mm\)\?' "$OUT/${name}_0.txt")"
    fi
done
echo "$([ $fails -eq 0 ] && echo ALL PASSED || echo "$fails FAILED")"
[ $fails -eq 0 ]
