#!/bin/bash
# net_channels_test.sh: MGO1Loader/netfix.h's stock joiners' channels (from its '// ---- a stock joiner' line to the end of
# NetChanLearnt) and ps2world.cpp's ChannelFamilies (from ServersChanFamily to its end) cut into a header,
# tests/net_channels_test.cpp built natively (g++) and run (then with MGO1_CHAN_PROBE=0: rc2's way); exit 0: every case passed
T=$(cd "$(dirname "$0")" && pwd); O=${TMPDIR:-/tmp}/net_channels_test.$$; mkdir -p $O
{ awk '/^\/\/ ---- a stock joiner/{on=1} on{print} on && /^void NetChanLearnt/{e=1} e && /^}$/{exit}' $T/../MGO1Loader/netfix.h
  awk '/^int ServersChanFamily\(\)/{on=1} on{print} on && /^void ChannelFamilies/{e=1} e && /^}$/{exit}' $T/../MGO1Loader/ps2world.cpp
} > $O/chan_part.h
g++ -std=c++17 -O1 -w -I$O -o $O/t $T/net_channels_test.cpp && $O/t && MGO1_CHAN_PROBE=0 $O/t off; r=$?
rm -rf $O; exit $r
