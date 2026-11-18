#!/bin/bash
# net_grenades_test.sh: MGO1Loader/netfix.h's grenade section (from its '// ---- the grenades' line to the end of
# NetNadeReport) cut into a header, tests/net_grenades_test.cpp built natively (g++) and run; exit 0: every case passed
T=$(cd "$(dirname "$0")" && pwd); O=${TMPDIR:-/tmp}/net_grenades_test.$$; mkdir -p $O
awk '/^\/\/ ---- the grenades/{on=1} on{print} on && /^void NetNadeReport/{rep=1} rep && /^}$/{exit}' $T/../MGO1Loader/netfix.h > $O/nade_part.h
g++ -std=c++17 -O1 -w -I$O -o $O/t $T/net_grenades_test.cpp && $O/t; r=$?
rm -rf $O; exit $r
