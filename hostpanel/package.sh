#!/bin/bash
# The program's folder for Windows, "MGO1 Host": MGO1 Host.exe and server\ (what it installs on the
# server: the host compressed with its SHA-256, the scripts, the agent, the steps, VERSION).
#   bash hostpanel/build.sh
#   bash hostpanel/package.sh --host-exe host/mgo1host.exe [--version "build 1.00"] [--out hostpanel/dist]
#   bash hostpanel/package.sh --source mgo1host-src.tar.gz [--version "build 1.00"] [--out hostpanel/dist]
# (build 1.00) server/VERSION: "<version> <the host's SHA-256, 12 digits>", the version src/version.h's when not given
# The host executable holds the game's code built from your disc: the folder is for your own use.
# (build 1.00, srvbuild) --source (the public package, tools/server_src.py): no host executable but the sources the server
# builds it from (server/mgo1host-src.tar.gz, install.sh runs host/build_server.sh), server/VERSION "<version> source".
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
HOSTEXE=$ROOT/host/mgo1host.exe
GUI=${GUI:-$HERE/build/MGO1 Host.exe}
DEPS=${DEPS:-$HERE/deps}
OUT=$HERE/dist
VERSION=""
SRCTGZ=""
while [ $# -gt 0 ]; do
    case "$1" in
    --host-exe) HOSTEXE=$2; shift 2 ;;
    --source) SRCTGZ=$2; shift 2 ;;
    --version) VERSION=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    *) echo "unknown option $1"; exit 2 ;;
    esac
done
[ -f "$GUI" ] || { echo "package: no $GUI (bash hostpanel/build.sh)"; exit 1; }
if [ -n "$SRCTGZ" ]; then
    [ -f "$SRCTGZ" ] || { echo "package: no $SRCTGZ (python3 tools/server_src.py --out ...)"; exit 1; }
else
    [ -f "$HOSTEXE" ] || { echo "package: no $HOSTEXE (bash host/build_host.sh)"; exit 1; }
fi
[ -n "$VERSION" ] || VERSION=$(sed -n 's/^#define MGO1HOST_VERSION "\([^"]*\)".*/\1/p' "$HERE/src/version.h")
[ -n "$SRCTGZ" ] || HASH=$(sha256sum "$HOSTEXE" | cut -c1-64)
D="$OUT/MGO1 Host"
rm -rf "$D"
mkdir -p "$D/server/steps" "$D/licenses"
cp "$GUI" "$D/"
for f in mgo1host.sh mgo1host-agent mgo1hostctl mgo1host@.service install.sh bot.env.example server.env.example game.ini.example README.md \
         mgo1doll.sh mgo1doll-manager mgo1doll@.service mgo1doll-manager.service doll.env.example mgo1log mgo1-dollgate; do
    cp "$ROOT/host/$f" "$D/server/"
done
chmod 0755 "$D/server/mgo1host.sh" "$D/server/mgo1host-agent" "$D/server/mgo1hostctl" "$D/server/install.sh" "$D/server/mgo1doll.sh" "$D/server/mgo1doll-manager" "$D/server/mgo1log" "$D/server/mgo1-dollgate"
cp "$ROOT/tests/steps/login_account.txt" "$ROOT/tests/steps/host_create.txt" "$ROOT/tests/steps/doll_join.txt" "$D/server/steps/"
if [ -n "$SRCTGZ" ]; then
    cp "$SRCTGZ" "$D/server/mgo1host-src.tar.gz"
    echo "$VERSION source" > "$D/server/VERSION"
else
    gzip -6 -n -c "$HOSTEXE" > "$D/server/mgo1host.exe.gz"
    echo "$HASH" > "$D/server/mgo1host.exe.sha256"
    echo "$VERSION ${HASH:0:12}" > "$D/server/VERSION"
fi
cp "$HERE/README.md" "$D/README.txt"
cp "$HERE/LISEZMOI.md" "$D/LISEZMOI.txt"
cp "$HERE/fonts/OFL-FiraSans.txt" "$D/licenses/Fira-OFL.txt"
[ -f "$DEPS/imgui/LICENSE.txt" ] && cp "$DEPS/imgui/LICENSE.txt" "$D/licenses/DearImGui-MIT.txt"
[ -f "$DEPS/libssh2/COPYING" ] && cp "$DEPS/libssh2/COPYING" "$D/licenses/libssh2-BSD.txt"
[ -f "$DEPS/openssl/LICENSE.txt" ] && cp "$DEPS/openssl/LICENSE.txt" "$D/licenses/OpenSSL-Apache2.txt"
(cd "$D" && find . -type f -printf "%12s  %p\n" | sort -k2)
if [ -n "$SRCTGZ" ]; then echo "package: $D ($(du -sh "$D" | cut -f1)), host $VERSION source (built on the server)"
else echo "package: $D ($(du -sh "$D" | cut -f1)), host $VERSION ${HASH:0:12}"; fi
