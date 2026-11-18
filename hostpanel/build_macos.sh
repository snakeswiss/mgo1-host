#!/bin/bash
# Builds MGO1 Host for macOS (10.15 or later) with Apple's clang: one universal program (Apple silicon
# and Intel) in MGO1 Host.app:
#   Dear ImGui 1.91.5 (GLFW + OpenGL 3), GLFW 3.4 (Cocoa), libssh2 1.11.1 and OpenSSL 3.0.15 (static),
#   miniaudio and stb (third_party/). The fonts and the logo are built into the program.
# The four are fetched once into $DEPS (default: hostpanel/deps/macos) and built there, a prefix for each set of
# architectures ($DEPS/prefix-arm64-x86_64, $DEPS/prefix-x86_64 ...); their objects are removed once installed.
#   bash hostpanel/build_macos.sh  -> hostpanel/build-macos/MGO1 Host/MGO1 Host.app (README.txt, LISEZMOI.txt, licenses/)
#                                     hostpanel/build-macos/MGO1-Host-macos.zip (that folder)
# Needs: Xcode's command line tools (clang, make, lipo, strip, codesign, iconutil, ditto), cmake, perl,
# git, python3 (Homebrew: brew install cmake). ARCHS="arm64" or "x86_64": one architecture only.
# JOBS: parallel jobs. The bundle is signed ad hoc only (not notarized): README.md, "macOS: building, checking, signing".
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/build-macos}
case "$HERE$OUT${DEPS:-}" in *" "*) echo "build_macos.sh: a space in the folder's path ($HERE): clone the repository into a path without spaces"; exit 1 ;; esac
# (v141 beta 6) a hostpanel/deps that is no folder here (another machine's link, from a source archive): the build's own
if [ -z "${DEPS:-}" ] && { [ -L "$HERE/deps" ] || [ -e "$HERE/deps" ]; } && [ ! -d "$HERE/deps" ]; then DEPS=$OUT/deps; fi
DEPS=${DEPS:-$HERE/deps/macos}
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
ARCHS=${ARCHS:-"arm64 x86_64"}
for a in $ARCHS; do case $a in arm64|x86_64) ;; *) echo "build_macos.sh: ARCHS takes arm64 and x86_64, not $a"; exit 1 ;; esac; done
ARCHS=$(printf '%s\n' $ARCHS | sort -u | tr '\n' ' ')
TAG=$(echo $ARCHS | tr ' ' '-')
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-10.15}
CC=${CC:-clang}
CXX=${CXX:-clang++}
for t in "$CC" "$CXX" cmake make perl git python3 lipo strip codesign iconutil ditto; do
    command -v "$t" >/dev/null || { echo "build_macos.sh: $t is missing"; exit 1; }
done
AFLAGS="-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
for a in $ARCHS; do AFLAGS="$AFLAGS -arch $a"; done
CMARCHS=$(echo $ARCHS | tr ' ' ';')
S=$DEPS/src B=$DEPS/build-$TAG P=$DEPS/prefix-$TAG        # (a universal build never takes a single architecture's libraries)
mkdir -p "$S" "$B" "$P/lib" "$P/include" "$OUT/obj" "$OUT/gen"

fetch() {   # repository, tag, folder
    [ -d "$3" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$2" "$1" "$3"
}
fetch https://github.com/ocornut/imgui.git v1.91.5 "$S/imgui"
fetch https://github.com/glfw/glfw.git 3.4 "$S/glfw"
fetch https://github.com/openssl/openssl.git openssl-3.0.15 "$S/openssl"
fetch https://github.com/libssh2/libssh2.git libssh2-1.11.1 "$S/libssh2"

# OpenSSL: a build for each architecture (its assembly), out of the source tree, joined by lipo; the
# headers that differ (configuration.h) chosen by the architecture being compiled
if [ ! -f "$P/lib/libcrypto.a" ]; then
    for a in $ARCHS; do
        echo "== OpenSSL $a"
        rm -rf "$B/openssl-$a" "$B/prefix-$a"
        mkdir -p "$B/openssl-$a"
        (cd "$B/openssl-$a" && "$S/openssl/Configure" "darwin64-$a-cc" no-shared no-tests no-legacy no-engine no-module no-dso no-ui-console \
            "-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" --prefix="$B/prefix-$a" --libdir=lib >/dev/null &&
            make -j"$JOBS" build_libs >/dev/null && make install_dev >/dev/null)
    done
    set -- $ARCHS
    rm -rf "$P/include/openssl"
    cp -R "$B/prefix-$1/include/openssl" "$P/include/"
    for h in "$B/prefix-$1/include/openssl/"*.h; do
        n=$(basename "$h")
        same=1
        for a in $ARCHS; do cmp -s "$h" "$B/prefix-$a/include/openssl/$n" || same=0; done
        [ $same = 1 ] && continue
        { echo "/* $n of each architecture (build_macos.sh) */"
          for a in $ARCHS; do
              case $a in arm64) c="defined(__aarch64__) || defined(__arm64__)" ;; x86_64) c="defined(__x86_64__)" ;; *) c="0" ;; esac
              cp "$B/prefix-$a/include/openssl/$n" "$P/include/openssl/${n%.h}-$a.h"
              echo "#if $c"; echo "#include \"${n%.h}-$a.h\""; echo "#endif"
          done; } > "$P/include/openssl/$n"
    done
    for l in libcrypto libssl; do
        libs=()
        for a in $ARCHS; do libs+=("$B/prefix-$a/lib/$l.a"); done
        if [ ${#libs[@]} = 1 ]; then cp "${libs[0]}" "$P/lib/$l.a"; else lipo -create "${libs[@]}" -output "$P/lib/$l.a"; fi
    done
    rm -rf "$B"/openssl-* "$B"/prefix-*
fi

# libssh2 with the OpenSSL back end
if [ ! -f "$P/lib/libssh2.a" ]; then
    echo "== libssh2"
    cmake -S "$S/libssh2" -B "$B/libssh2" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_OSX_ARCHITECTURES="$CMARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DCRYPTO_BACKEND=OpenSSL -DOPENSSL_ROOT_DIR="$P" -DOPENSSL_USE_STATIC_LIBS=TRUE \
        -DENABLE_ZLIB_COMPRESSION=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF >/dev/null
    cmake --build "$B/libssh2" -j "$JOBS" >/dev/null
    cmake --install "$B/libssh2" >/dev/null
    rm -rf "$B/libssh2"
fi

# GLFW for Cocoa
if [ ! -f "$P/lib/libglfw3.a" ]; then
    echo "== GLFW"
    cmake -S "$S/glfw" -B "$B/glfw" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_OSX_ARCHITECTURES="$CMARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
        -DBUILD_SHARED_LIBS=OFF -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF >/dev/null
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
CXXFLAGS="$AFLAGS -O2 -std=c++17 $IMCFG -I$HERE/src -I$I -I$P/include -I$HERE/third_party/miniaudio -I$HERE/third_party/stb -Wall -Wno-unused-function"
echo "== mgo1-host ($ARCHS)"
objs=()
for f in "$I"/imgui.cpp "$I"/imgui_draw.cpp "$I"/imgui_tables.cpp "$I"/imgui_widgets.cpp "$I"/imgui_impl_glfw.cpp "$I"/imgui_impl_opengl3.cpp; do
    o=$OUT/obj/$(basename "${f%.cpp}").o
    $CXX $AFLAGS -O2 -std=c++17 -DGLFW_INCLUDE_NONE $IMCFG -I"$I" -I"$P/include" -c "$f" -o "$o"
    objs+=("$o")
done
# (main.cpp and *_win.cpp are Windows': nothing in them here)
for f in "$HERE"/src/*.cpp "$HERE"/../MGO1Loader/spu2.cpp "$OUT/gen/resources.cpp"; do   # (spu2: the menu sounds from the disc, sound.cpp)
    o=$OUT/obj/$(basename "${f%.cpp}").o
    $CXX $CXXFLAGS -c "$f" -o "$o"
    objs+=("$o")
done
$CXX $AFLAGS -o "$OUT/mgo1-host" "${objs[@]}" -L"$P/lib" -lglfw3 -lssh2 -lssl -lcrypto -Wl,-dead_strip \
    -framework Cocoa -framework IOKit -framework CoreFoundation -framework CoreAudio -framework AudioToolbox
strip "$OUT/mgo1-host"
lipo -info "$OUT/mgo1-host"
for a in $ARCHS; do lipo "$OUT/mgo1-host" -verify_arch "$a" || { echo "build_macos.sh: mgo1-host has no $a"; exit 1; }; done

# the bundle: the program, its icon (the PNG pictures of app.ico), Info.plist; signed ad hoc
# (build 1.00) src/version.h's "build 1.00": the Finder's version the number alone (Apple's form, digits and dots), the
# name with "build" in its info string
VERSION=$(sed -n 's/^#define MGO1HOST_VERSION "\([^"]*\)".*/\1/p' "$HERE/src/version.h")
VNUM=${VERSION#build }
D="$OUT/MGO1 Host"
A="$D/MGO1 Host.app"
rm -rf "$D" "$OUT/MGO1Host.iconset"
mkdir -p "$A/Contents/MacOS" "$A/Contents/Resources" "$D/licenses" "$OUT/MGO1Host.iconset"
cp "$OUT/mgo1-host" "$A/Contents/MacOS/mgo1-host"
for e in 16:icon_16x16 32:icon_16x16@2x 32:icon_32x32 64:icon_32x32@2x 128:icon_128x128 256:icon_128x128@2x 256:icon_256x256; do
    python3 "$HERE/res/embed.py" --icon "${e%%:*}" "$OUT/MGO1Host.iconset/${e#*:}.png"
done
iconutil -c icns -o "$A/Contents/Resources/MGO1Host.icns" "$OUT/MGO1Host.iconset" || echo "build_macos.sh: no icon (iconutil failed)"
rm -rf "$OUT/MGO1Host.iconset"
cat > "$A/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>mgo1-host</string>
    <key>CFBundleIconFile</key><string>MGO1Host</string>
    <key>CFBundleIdentifier</key><string>community.savemgo.mgo1host</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>MGO1 Host</string>
    <key>CFBundleDisplayName</key><string>MGO1 Host</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VNUM</string>
    <key>CFBundleVersion</key><string>$VNUM</string>
    <key>CFBundleGetInfoString</key><string>MGO1 Host $VERSION</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
    <key>LSMinimumSystemVersion</key><string>$MACOSX_DEPLOYMENT_TARGET</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
EOF
printf 'APPL????' > "$A/Contents/PkgInfo"
codesign --force --deep --sign - "$A"
codesign --verify --deep --strict "$A"
cp "$HERE/README.md" "$D/README.txt"
cp "$HERE/LISEZMOI.md" "$D/LISEZMOI.txt"
cp "$HERE/fonts/OFL-FiraSans.txt" "$D/licenses/Fira-OFL.txt"
cp "$S/imgui/LICENSE.txt" "$D/licenses/DearImGui-MIT.txt"
cp "$S/glfw/LICENSE.md" "$D/licenses/GLFW-zlib.txt"
cp "$S/libssh2/COPYING" "$D/licenses/libssh2-BSD.txt"
cp "$S/openssl/LICENSE.txt" "$D/licenses/OpenSSL-Apache2.txt"
cp "$HERE/third_party/miniaudio/LICENSE" "$D/licenses/miniaudio-MIT0.txt"
cp "$HERE/third_party/stb/LICENSE" "$D/licenses/stb-MIT.txt"
rm -f "$OUT/MGO1-Host-macos.zip"
ditto -c -k --sequesterRsrc --keepParent "$D" "$OUT/MGO1-Host-macos.zip"
ls -la "$OUT/MGO1-Host-macos.zip"
