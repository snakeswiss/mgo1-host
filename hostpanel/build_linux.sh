#!/bin/bash
# Builds MGO1 Host for Linux (x86_64 or arm64, the machine's own), natively:
#   Dear ImGui 1.91.5 (GLFW + OpenGL 3), GLFW 3.4 (X11), libssh2 1.11.1 and OpenSSL 3.0.15 (static),
#   miniaudio and stb (third_party/). The fonts, the logo and the icon are built into the program.
# The four are fetched once into $DEPS (default: hostpanel/deps/linux; Windows' hostpanel/deps is not
# used) and built there; their objects are removed once installed in $DEPS/prefix.
#   bash hostpanel/build_linux.sh  -> hostpanel/build-linux/mgo1-host
#                                     hostpanel/build-linux/MGO1-Host-linux-<arch>.tar.gz
# Needs: gcc and g++ (C++17), cmake, make, perl, git, python3, the X11 headers (Debian, Ubuntu:
# libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev; Fedora: libX11-devel
# libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libXext-devel). JOBS=1: one job at a time.
# The program needs glibc (the build machine's version or later); X11 (or XWayland), OpenGL 3 and the
# sound libraries are loaded when it runs.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/build-linux}
# (v141 beta 6) a hostpanel/deps that is no folder here (another machine's link, from a source archive): the build's own
if [ -z "${DEPS:-}" ] && { [ -L "$HERE/deps" ] || [ -e "$HERE/deps" ]; } && [ ! -d "$HERE/deps" ]; then DEPS=$OUT/deps; fi
DEPS=${DEPS:-$HERE/deps/linux}
JOBS=${JOBS:-$(nproc)}
CC=${CC:-gcc}
CXX=${CXX:-g++}
ARCH=$(uname -m)
for t in "$CC" "$CXX" cmake make perl git python3 strip; do command -v "$t" >/dev/null || { echo "build_linux.sh: $t is missing"; exit 1; }; done
S=$DEPS/src B=$DEPS/build P=$DEPS/prefix
mkdir -p "$S" "$B" "$P" "$OUT/obj" "$OUT/gen"

fetch() {   # repository, tag, folder
    [ -d "$3" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$2" "$1" "$3"
}
fetch https://github.com/ocornut/imgui.git v1.91.5 "$S/imgui"
fetch https://github.com/glfw/glfw.git 3.4 "$S/glfw"
fetch https://github.com/openssl/openssl.git openssl-3.0.15 "$S/openssl"
fetch https://github.com/libssh2/libssh2.git libssh2-1.11.1 "$S/libssh2"

# OpenSSL: libcrypto and libssl only, no shared libraries
if [ ! -f "$P/lib/libcrypto.a" ]; then
    echo "== OpenSSL"
    (cd "$S/openssl" && ./Configure no-shared no-tests no-legacy no-engine no-module no-dso no-ui-console \
        --prefix="$P" --libdir=lib >/dev/null && make -j"$JOBS" build_libs >/dev/null && make install_dev >/dev/null && make clean >/dev/null)
fi

# libssh2 with the OpenSSL back end
if [ ! -f "$P/lib/libssh2.a" ]; then
    echo "== libssh2"
    cmake -S "$S/libssh2" -B "$B/libssh2" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DCRYPTO_BACKEND=OpenSSL -DOPENSSL_ROOT_DIR="$P" -DOPENSSL_USE_STATIC_LIBS=TRUE \
        -DENABLE_ZLIB_COMPRESSION=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF >/dev/null
    cmake --build "$B/libssh2" -j "$JOBS" >/dev/null
    cmake --install "$B/libssh2" >/dev/null
    rm -rf "$B/libssh2"
fi

# GLFW for X11 (on a Wayland desktop through XWayland)
if [ ! -f "$P/lib/libglfw3.a" ]; then
    echo "== GLFW"
    cmake -S "$S/glfw" -B "$B/glfw" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=OFF -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
        -DGLFW_BUILD_X11=ON -DGLFW_BUILD_WAYLAND=OFF >/dev/null
    cmake --build "$B/glfw" -j "$JOBS" >/dev/null
    cmake --install "$B/glfw" >/dev/null
    rm -rf "$B/glfw"
fi

# Dear ImGui, with one change: lines may wrap after any Chinese or Japanese character (imgui_wrap.py)
I=$OUT/imgui
mkdir -p "$I"
cp "$S"/imgui/*.h "$S"/imgui/*.cpp "$S"/imgui/backends/imgui_impl_glfw.* "$S"/imgui/backends/imgui_impl_opengl3.* \
   "$S"/imgui/backends/imgui_impl_opengl3_loader.h "$I/"
python3 "$HERE/imgui_wrap.py" "$I/imgui_draw.cpp"

# the strings of the 11 languages; the fonts, the logo and the icon (res/app.rc) in a source file
python3 "$HERE/i18n/gen_i18n.py"
python3 "$HERE/res/embed.py" "$OUT/gen/resources.cpp"

IMCFG="-DIMGUI_USER_CONFIG=\"imgui_user.h\" -I$HERE/src"
CXXFLAGS="-O2 -std=c++17 -pthread $IMCFG -I$HERE/src -I$I -I$P/include -I$HERE/third_party/miniaudio -I$HERE/third_party/stb -Wall -Wno-unused-function"
echo "== mgo1-host"
objs=()
for f in "$I"/imgui.cpp "$I"/imgui_draw.cpp "$I"/imgui_tables.cpp "$I"/imgui_widgets.cpp "$I"/imgui_impl_glfw.cpp "$I"/imgui_impl_opengl3.cpp; do
    o=$OUT/obj/$(basename "${f%.cpp}").o
    $CXX -O2 -std=c++17 -DGLFW_INCLUDE_NONE $IMCFG -I"$I" -I"$P/include" -c "$f" -o "$o"
    objs+=("$o")
done
# (main.cpp and *_win.cpp are Windows': nothing in them here)
for f in "$HERE"/src/*.cpp "$HERE"/../MGO1Loader/spu2.cpp "$OUT/gen/resources.cpp"; do   # (spu2: the menu sounds from the disc, sound.cpp)
    o=$OUT/obj/$(basename "${f%.cpp}").o
    $CXX $CXXFLAGS -c "$f" -o "$o"
    objs+=("$o")
done
$CXX -O2 -pthread -o "$OUT/mgo1-host" "${objs[@]}" -L"$P/lib" -lglfw3 -lssh2 -lssl -lcrypto -ldl -lm -lrt -static-libgcc -static-libstdc++
strip "$OUT/mgo1-host"
ls -la "$OUT/mgo1-host"

# the folder to unpack anywhere: the program, its icon (a menu entry: INSTALL-LINUX.md), README.txt, the licenses
D="$OUT/package/MGO1 Host"
rm -rf "$OUT/package"
mkdir -p "$D/licenses"
cp "$OUT/mgo1-host" "$D/"
python3 "$HERE/res/embed.py" --icon 256 "$D/mgo1-host.png"
cp "$HERE/README.md" "$D/README.txt"
cp "$HERE/fonts/OFL-FiraSans.txt" "$D/licenses/Fira-OFL.txt"
cp "$S/imgui/LICENSE.txt" "$D/licenses/DearImGui-MIT.txt"
cp "$S/glfw/LICENSE.md" "$D/licenses/GLFW-zlib.txt"
cp "$S/libssh2/COPYING" "$D/licenses/libssh2-BSD.txt"
cp "$S/openssl/LICENSE.txt" "$D/licenses/OpenSSL-Apache2.txt"
cp "$HERE/third_party/miniaudio/LICENSE" "$D/licenses/miniaudio-MIT0.txt"
cp "$HERE/third_party/stb/LICENSE" "$D/licenses/stb-MIT.txt"
tar -C "$OUT/package" --owner=0 --group=0 -czf "$OUT/MGO1-Host-linux-$ARCH.tar.gz" "MGO1 Host"
rm -rf "$OUT/package"
ls -la "$OUT/MGO1-Host-linux-$ARCH.tar.gz"
