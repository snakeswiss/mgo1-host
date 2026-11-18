#!/bin/bash
# The recompiled code of every version in one generated folder (docs/VERSIONS.md):
#   tools/gen_versions.sh <out gen dir> US=<images> [EU=<images>] [JP=<images>] [JPS=<images>] [KR=<images>]
# <images>: a version's code images (tools/ps2_images.py). The US closures are recomp/closure_*.txt;
# another version's are computed here (build/versions/<v>/): every function of its texts, its roots
# (recomp/roots.txt found in it), then tools/versions.py pairs its functions with the US ones.
set -e -o pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$1; shift
PAGES="ntmenu2 init_n r_menu_n r_sna01_n ntmenu n001a n002a n003a n011a n012a n013a n021a n022a n024a n031a n032a n033a"
declare -A IMG
for a in "$@"; do IMG[${a%%=*}]=${a#*=}; done
US=${IMG[US]}
[ -n "$US" ] || { echo "gen_versions.sh: US=<images> is needed"; exit 2; }
ARGS=""
for p in $PAGES; do
    img=$(ls $US/${p}_*.bin | head -1)
    fn=$ROOT/recomp/closure_$p.txt; [ $p = ntmenu2 ] && fn=$ROOT/recomp/closure_functions.txt
    al=$ROOT/recomp/alias_$p.txt; [ -f $al ] || al=""
    ARGS="$ARGS --page $p $img $fn $al"
done
# the US program's every function (the bridges too: the platform serves them at their US
# addresses, so another version's copies must be paired with them): the functions the versions
# are aligned against (build/versions/us)
UW=$ROOT/build/versions/us
mkdir -p $UW/rootsdir; : > $UW/rootsdir/bridges.txt; cp $ROOT/recomp/roots.txt $UW/rootsdir/roots.txt
for p in $PAGES; do
    img=$(ls $US/${p}_*.bin | head -1)
    extra=""; [ $p != ntmenu2 ] && extra="--page $p"
    fn=$UW/closure_$p.txt; [ $p = ntmenu2 ] && fn=$UW/closure_functions.txt
    python3 $ROOT/recomp/closure.py $img $US/mgs3_n_base.bin $UW/rootsdir/bridges.txt $fn $UW/flags_$p.txt $extra --all > /dev/null
done
python3 - $UW <<'PY'
import sys, glob, os
out = sys.argv[1]; base = {}
for f in glob.glob(os.path.join(out, 'closure_*.txt')):
    if f.endswith('closure_base.txt'): continue
    for l in open(f):
        p = l.split()
        if len(p) < 2: continue
        s, e = int(p[0], 16), int(p[1], 16)
        if s < 0x2d0000: base[s] = min(e, base.get(s, e))
open(os.path.join(out, 'closure_base.txt'), 'w').write(''.join(f'{s:#x} {base[s]:#x}\n' for s in sorted(base)))
PY
VARGS=""; HARGS=""
for v in EU JP JPS KR; do
    d=${IMG[$v]}; [ -n "$d" ] || continue
    lv=$(echo $v | tr A-Z a-z); W=$ROOT/build/versions/$lv
    mkdir -p $W/rootsdir; : > $W/rootsdir/bridges.txt
    # the base text's end: _init's code (the same in every version) + 0x40
    TE=$(python3 -c "
us = open('$US/mgs3_n_base.bin', 'rb').read(); b = open('$d/mgs3_n_base.bin', 'rb').read()
print(hex(0x100000 + b.find(us[0x101b08:0x101b14]) + 0x40))")
    closures() {
        for p in $PAGES; do
            img=$(ls $d/${p}_*.bin 2>/dev/null | head -1); [ -n "$img" ] || continue
            extra=""; [ $p != ntmenu2 ] && extra="--page $p"
            fn=$W/closure_$p.txt; [ $p = ntmenu2 ] && fn=$W/closure_functions.txt
            BASE_TEXT_END=$TE OVL_FROM_HEADER=1 python3 $ROOT/recomp/closure.py $img $d/mgs3_n_base.bin $W/rootsdir/bridges.txt $fn $W/flags_$p.txt $extra --all > /dev/null
        done
        python3 - $W <<'PY'
import sys, glob, os
out = sys.argv[1]; base = {}
for f in glob.glob(os.path.join(out, 'closure_*.txt')):
    if f.endswith('closure_base.txt'): continue
    for l in open(f):
        p = l.split()
        if len(p) < 2: continue
        s, e = int(p[0], 16), int(p[1], 16)
        if s < 0x2d0000: base[s] = min(e, base.get(s, e))
open(os.path.join(out, 'closure_base.txt'), 'w').write(''.join(f'{s:#x} {base[s]:#x}\n' for s in sorted(base)))
PY
    }
    : > $W/rootsdir/roots.txt
    closures
    python3 $ROOT/tools/versions.py --us $US $UW --other $v $d $W --out $W/map.txt > $W/versions.log
    python3 $ROOT/tools/versions_roots.py $US $UW $d $W/rootsdir/roots.txt $W/map.txt > $W/roots.log
    closures
    python3 $ROOT/tools/versions.py --us $US $UW --other $v $d $W --out $W/map.txt > $W/versions.log
    echo "$v: $(head -2 $W/versions.log | tr '\n' ' ')"
    echo "$v: $(cat $W/roots.log | cut -c1-200)"
    VARGS="$VARGS --version $v $d $W $W/map.txt"
    HARGS="$HARGS --version $v $d"
done
python3 $ROOT/recomp/mips2shape.py --base $US/mgs3_n_base.bin --base-functions $ROOT/recomp/closure_base.txt $ARGS $VARGS --out $OUT
python3 $ROOT/tools/page_heads.py $US $OUT/mgo1_page_heads.cpp $PAGES $HARGS
