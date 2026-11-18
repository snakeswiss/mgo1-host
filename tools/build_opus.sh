#!/bin/bash
# libopus for the voice between PCs (docs/VOICE.md): third_party/opus (Opus 1.5.2, BSD, its COPYING), the
# float build of its C sources, without the x86 intrinsics nor the neural extensions. Rebuilt only when a
# source or a header changed. Argument: the archive (default native/obj/libopus.a). OPUS_CC / OPUS_AR: another
# compiler (tests/voice_dsp_test.cpp natively: OPUS_CC=gcc OPUS_AR=ar).
set -e -o pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/native/obj/libopus.a}")
OBJ=$(dirname "$OUT")/opus
mkdir -p "$OBJ"
cd "$ROOT/third_party/opus"
CC="${OPUS_CC:-x86_64-w64-mingw32-gcc} -O2 -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINTF -DHAVE_LRINT -Iinclude -Icelt -Isilk -Isilk/float -w"
[ -f "$OUT" ] && [ -n "$(find . -name '*.h' -newer "$OUT" | head -1)" ] && rm -f "$OBJ"/*.o
built=0
for f in celt/*.c silk/*.c silk/float/*.c src/*.c; do
    o="$OBJ/$(echo "${f%.c}" | tr / _).o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then $CC -c "$f" -o "$o"; built=1; fi
done
if [ $built = 1 ] || [ ! -f "$OUT" ]; then
    rm -f "$OUT"
    ${OPUS_AR:-x86_64-w64-mingw32-ar} rcs "$OUT" "$OBJ"/*.o
    echo "libopus: $(ls "$OBJ"/*.o | wc -l) objects in $OUT"
fi
