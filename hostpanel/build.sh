#!/bin/bash
# Builds "MGO1 Host.exe" (Windows x64, one file) on Linux with mingw-w64:
#   Dear ImGui 1.91.5 (Win32 + Direct3D 11), libssh2 1.11.1 and OpenSSL 3.0.15 (static).
# The three are fetched once into $DEPS (default: hostpanel/deps) and built there.
#   bash hostpanel/build.sh            -> hostpanel/build/MGO1 Host.exe
# Needs: x86_64-w64-mingw32-gcc/g++/windres (posix or win32 threads, GCC 13 or later), perl, make,
# git, python3.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/build}
# (v141 beta 6) a hostpanel/deps that is no folder here (another machine's link, from a source archive): the build's own
if [ -z "${DEPS:-}" ] && { [ -L "$HERE/deps" ] || [ -e "$HERE/deps" ]; } && [ ! -d "$HERE/deps" ]; then DEPS=$OUT/deps; fi
DEPS=${DEPS:-$HERE/deps}
JOBS=${JOBS:-$(nproc)}
T=x86_64-w64-mingw32
CC=$T-gcc CXX=$T-g++ AR=$T-ar WINDRES=$T-windres STRIP=$T-strip
mkdir -p "$DEPS" "$OUT/obj"

fetch() {   # repository, tag, folder
    [ -d "$3" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$2" "$1" "$3"
}
fetch https://github.com/ocornut/imgui.git v1.91.5 "$DEPS/imgui"
fetch https://github.com/openssl/openssl.git openssl-3.0.15 "$DEPS/openssl"
fetch https://github.com/libssh2/libssh2.git libssh2-1.11.1 "$DEPS/libssh2"
W=$DEPS/win64

# OpenSSL: libcrypto and libssl only, no DLLs
if [ ! -f "$W/lib/libcrypto.a" ]; then
    echo "== OpenSSL"
    (cd "$DEPS/openssl" && ./Configure mingw64 no-shared no-tests no-legacy no-engine no-module no-dso no-ui-console \
        --cross-compile-prefix=$T- --prefix="$W" --libdir=lib >/dev/null && make -j"$JOBS" build_libs >/dev/null && make install_dev >/dev/null)
fi

# libssh2: its sources with the OpenSSL back end (crypto.c includes openssl.c)
if [ ! -f "$W/lib/libssh2.a" ]; then
    echo "== libssh2"
    L=$DEPS/libssh2 O=$DEPS/libssh2/objw
    mkdir -p "$O"
    for f in "$L"/src/*.c; do
        case $(basename "$f") in openssl.c|wincng.c|mbedtls.c|libgcrypt.c|os400qc3.c) continue ;; esac
        $CC -O2 -DLIBSSH2_OPENSSL -DLIBSSH2_LIBRARY -I"$L/include" -I"$W/include" -c "$f" -o "$O/$(basename "${f%.c}").o"
    done
    rm -f "$W/lib/libssh2.a"; $AR rcs "$W/lib/libssh2.a" "$O"/*.o
    mkdir -p "$W/include"; cp "$L"/include/libssh2*.h "$W/include/"
fi

# Dear ImGui, with one change: lines may wrap after any Chinese or Japanese character (imgui_wrap.py)
I=$OUT/imgui
mkdir -p "$I"
cp "$DEPS"/imgui/*.h "$DEPS"/imgui/*.cpp "$DEPS"/imgui/backends/imgui_impl_win32.* "$DEPS"/imgui/backends/imgui_impl_dx11.* "$I/"
python3 "$HERE/imgui_wrap.py" "$I/imgui_draw.cpp"

# the strings of the 11 languages
python3 "$HERE/i18n/gen_i18n.py"

IMCFG="-DIMGUI_USER_CONFIG=\"imgui_user.h\" -I$HERE/src"
CXXFLAGS="-O2 -std=c++17 -DNOMINMAX -DUNICODE -D_UNICODE -D__USE_MINGW_ANSI_STDIO=1 $IMCFG -I$HERE/src -I$I -I$W/include -Wall -Wno-unused-function"
echo "== MGO1 Host.exe"
objs=()
for f in "$I"/imgui.cpp "$I"/imgui_draw.cpp "$I"/imgui_tables.cpp "$I"/imgui_widgets.cpp "$I"/imgui_impl_win32.cpp "$I"/imgui_impl_dx11.cpp; do
    o=$OUT/obj/$(basename "${f%.cpp}").o
    $CXX -O2 -std=c++17 -DNOMINMAX -DUNICODE -D_UNICODE $IMCFG -I"$I" -c "$f" -o "$o"
    objs+=("$o")
done
# (src/*_posix.cpp and main_glfw.cpp are the Linux and macOS build's: nothing in them for Windows)
for f in "$HERE"/src/*.cpp "$HERE"/../MGO1Loader/spu2.cpp; do   # (spu2: the menu sounds from the disc, sound.cpp)
    o=$OUT/obj/$(basename "${f%.cpp}").o
    $CXX $CXXFLAGS -c "$f" -o "$o"
    objs+=("$o")
done
# the SaveMGO logo: tools/launcher_art.py's savemgo_logo.png (cut from SaveMGO's title picture)
if [ ! -f "$HERE/res/savemgo.png" ]; then
    [ -f "$HERE/../native/res/savemgo_logo.png" ] || { echo "no res/savemgo.png: python3 tools/launcher_art.py first"; exit 1; }
    cp "$HERE/../native/res/savemgo_logo.png" "$HERE/res/savemgo.png"
fi
(cd "$HERE/res" && $WINDRES -O coff app.rc -o "$OUT/obj/app.res.o")
$CXX -O2 -static -static-libgcc -static-libstdc++ -municode -mwindows -o "$OUT/MGO1 Host.exe" "${objs[@]}" "$OUT/obj/app.res.o" \
    -L"$W/lib" -lssh2 -lssl -lcrypto -ld3d11 -ldxgi -ld3dcompiler_47 -ldwmapi -lwindowscodecs -limm32 -lws2_32 -lcrypt32 -lbcrypt \
    -lole32 -loleaut32 -lshell32 -luuid -luser32 -lgdi32 -ladvapi32 -lwinmm
$STRIP "$OUT/MGO1 Host.exe"
ls -la "$OUT/MGO1 Host.exe"
