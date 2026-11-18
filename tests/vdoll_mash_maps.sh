#!/bin/bash
# (v150-host beta 22, mash) a knocked-out virtual doll presses as a player (docs/VIRTUAL_DOLL.md 37): tests/vdoll_mash_test.cpp
# once (the rule, the hand), then tests/vdoll_flow_test.cpp's mash scenarios on each disc map given (the doll's body on it).
#   bash tests/vdoll_mash_maps.sh /path/to/geom/*.geom
# The maps' collision files come from the owner's disc (python3 -I tools/stage_dat.py extract ...) and are never put in the
# repository. RECAST_OBJS: Recast's objects already built (else compiled from MGO1Loader/recast). OUT: the folder for the
# builds (default a temporary one). The exit code is 0 only when everything passed. (mash2) Each case's digest (its frames and
# places) comes out the same in every run whatever the machine's load: two runs' outputs can be compared line for line.
set -u
cd "$(dirname "$0")/.."
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
RC=${RECAST_OBJS:-MGO1Loader/recast/*.cpp}
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_mash_test.cpp -o "$OUT/vdoll_mash_test" || exit 2
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_stamina_test.cpp -o "$OUT/vdoll_stamina_test" || exit 2   # ((v150-host beta 24, stamina2) a move's cost)
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_flow_test.cpp MGO1Loader/vdoll.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
    MGO1Loader/doll_nav.cpp $RC -lpthread -o "$OUT/vdoll_flow_test" || exit 2
# ((v150-host beta 25, aimcost) the moves as their cost reads them, each host frame run twice: tests/vdoll_aimcost_test.cpp)
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_aimcost_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
    MGO1Loader/doll_nav.cpp $RC -lpthread -o "$OUT/vdoll_aimcost_test" || exit 2
# ((v150-host beta 25, dpass) the decisions once a read and the turn, each host frame run twice: tests/vdoll_dpass_test.cpp)
# ((v150-host beta 25, copyleash) the host's copy of a doll on a slowed host, HeldBack: tests/vdoll_copyleash_test.cpp, each map)
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_dpass_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
    MGO1Loader/doll_nav.cpp $RC -lpthread -o "$OUT/vdoll_dpass_test" || exit 2
# ((v150-host beta 25, rolltime) a doll's roll as a player's, each host frame run twice: tests/vdoll_rolltime_test.cpp)
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_rolltime_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
    MGO1Loader/doll_nav.cpp $RC -lpthread -o "$OUT/vdoll_rolltime_test" || exit 2
g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_copyleash_test.cpp MGO1Loader/vdoll_body.cpp MGO1Loader/doll_geom.cpp \
    MGO1Loader/doll_nav.cpp $RC -lpthread -o "$OUT/vdoll_copyleash_test" || exit 2   # ((v150-host beta 25, copyleash) the host's copy on a slowed host)
fails=0
"$OUT/vdoll_mash_test" > "$OUT/rules.txt"; r=$?; tail -1 "$OUT/rules.txt"; [ $r -eq 0 ] || { fails=$((fails + 1)); grep FAIL "$OUT/rules.txt"; }
"$OUT/vdoll_stamina_test" > "$OUT/moves.txt"; r=$?; tail -1 "$OUT/moves.txt"; [ $r -eq 0 ] || { fails=$((fails + 1)); grep FAIL "$OUT/moves.txt"; }
for m in "$@"; do
    for s in mash mashlow mashnone mash0 mashslow mashslow0 movecost movecost0 movecostclay movecostko movecostko0 movecostpass movecostpass0 mashjitter mashbunch mashquota movecostjitter movecostbunch movecostquota movecostbunch0 pacemixed pacemixed0; do   # ((v150-host beta 24, stamina2) a move's cost; (v150-host beta 25, aimcost) each pass twice in its frame; (clockfix) uneven and quota clocks)
        "$OUT/vdoll_flow_test" $s "$m" > "$OUT/one.txt" 2>&1; r=$?
        printf '%-10s %-8s %s %s\n' "$(basename "$m" | cut -c1-5)" "$s" "$([ $r -eq 0 ] && echo PASS || echo FAIL)" "$(grep -o 'digest: .*' "$OUT/one.txt")"
        [ $r -eq 0 ] || { fails=$((fails + 1)); grep FAIL "$OUT/one.txt"; }
    done
done
if [ $# -gt 0 ]; then                              # ((v150-host beta 25, aimcost) run, aim, roll on every map given; (dpass) its parts too)
    "$OUT/vdoll_aimcost_test" "$@" > "$OUT/aimcost.txt" 2>&1; r=$?; tail -1 "$OUT/aimcost.txt"; [ $r -eq 0 ] || { fails=$((fails + 1)); grep FAIL "$OUT/aimcost.txt"; }
    "$OUT/vdoll_dpass_test" "$@" > "$OUT/dpass.txt" 2>&1; r=$?; tail -1 "$OUT/dpass.txt"; [ $r -eq 0 ] || { fails=$((fails + 1)); grep FAIL "$OUT/dpass.txt"; }   # ((v150-host beta 25, dpass) held, turn, roll, reads)
    "$OUT/vdoll_rolltime_test" "$@" > "$OUT/rolltime.txt" 2>&1; r=$?; tail -1 "$OUT/rolltime.txt"; [ $r -eq 0 ] || { fails=$((fails + 1)); grep FAIL "$OUT/rolltime.txt"; }   # ((v150-host beta 25, rolltime) test, into, shot)
    for m in "$@"; do                              # ((v150-host beta 25, copyleash) slopes, pace, held: one map a run)
        "$OUT/vdoll_copyleash_test" "$m" > "$OUT/copyleash.txt" 2>&1; r=$?; echo "copyleash $(basename "$m" | cut -c1-5): $(tail -1 "$OUT/copyleash.txt")"; [ $r -eq 0 ] || { fails=$((fails + 1)); grep FAIL "$OUT/copyleash.txt"; }
    done
fi
echo "$([ $fails -eq 0 ] && echo ALL PASSED || echo "$fails FAILED")"
[ $fails -eq 0 ]
