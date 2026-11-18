#!/bin/bash
# (build 1.00, quickstart) Stand-in of host/install.sh for MGO1 Host's QUICK SETUP test (tests/test_quick_setup.py), run by the
# program as it runs the real one (env MGO1_MARKS=1 bash <stage>/install.sh) on tests/fake_ssh_host.py's sandbox: the same
# marks in the same order; with mgo1host-src.tar.gz and no mgo1host.exe in the stage (the public package) the host "built" in
# a few minutes with @is_build|deps, gen, compile, link, done and @is_build_pct|<n>, a stopped build going on from the steps it
# finished, the same sources and disc again: @is_build|done alone (as host/build_server.sh); then the real agent installed on
# the sandbox's folders. <root>/test.env: BUILD_STEP_S (seconds a step), PCT_EVERY_S, QUIET_S (gen's first stretch with no
# line at all), FAIL_BUILD_ONCE=1 (the compile fails once, as on a server whose memory ran out).
set -u
R=${FAKE_ROOT:?}
ST=$(cd "$(dirname "$0")" && pwd)
[ -f "$R/test.env" ] && . "$R/test.env"
BUILD_STEP_S=${BUILD_STEP_S:-20}; PCT_EVERY_S=${PCT_EVERY_S:-10}; QUIET_S=${QUIET_S:-0}; FAIL_BUILD_ONCE=${FAIL_BUILD_ONCE:-0}
msg() { local k=$1; shift; local IFS='|'; if [ $# -gt 0 ]; then echo "@$k|$*"; else echo "@$k"; fi; }

msg is_wine
msg is_wine_version "wine-9.0 (stand-in)"
msg is_files
mkdir -p "$R/opt/disc" "$R/opt/build"
[ -d "$ST/disc" ] && cp -r "$ST/disc/." "$R/opt/disc/"
if [ -f "$ST/mgo1host.exe" ]; then
    cp "$ST/mgo1host.exe" "$R/opt/mgo1host.exe"
elif [ -f "$ST/mgo1host-src.tar.gz" ]; then
    SUM=$(cat "$ST/mgo1host-src.tar.gz" "$R/opt/disc/MGS/NET/MGS3_N.ELF" | sha256sum | cut -c1-64)
    if [ -f "$R/opt/build/built.txt" ] && [ "$(cat "$R/opt/build/built.txt")" = "$SUM" ]; then
        msg is_build done
    else
        step() {   # name, first percent, last percent
            grep -qx "$1" "$R/opt/build/steps.done" 2>/dev/null && return 0
            msg is_build "$1"
            [ "$1" = gen ] && [ "$QUIET_S" -gt 0 ] && sleep "$QUIET_S"
            local t=0 p
            while [ $t -lt "$BUILD_STEP_S" ]; do
                sleep "$PCT_EVERY_S"; t=$((t + PCT_EVERY_S))
                p=$(($2 + ($3 - $2) * t / BUILD_STEP_S)); [ $p -gt "$3" ] && p=$3
                msg is_build_pct "$p"
                if [ "$1" = compile ] && [ "$FAIL_BUILD_ONCE" = 1 ] && [ ! -f "$R/failed_once" ] && [ $((t * 2)) -ge "$BUILD_STEP_S" ]; then
                    touch "$R/failed_once"
                    echo "g++: fatal error: Killed signal terminated program cc1plus"
                    msg ie_build "g++: fatal error: Killed signal terminated program cc1plus (MGO1Loader/gen/mgs3_n_func_0217.cpp)"
                    exit 1
                fi
            done
            echo "$1" >> "$R/opt/build/steps.done"
        }
        step deps 0 5
        step gen 5 30
        step compile 30 90
        step link 90 99
        echo "$SUM" > "$R/opt/build/built.txt"
        rm -f "$R/opt/build/steps.done"
        echo "stand-in host built from $SUM" > "$R/opt/mgo1host.exe"
        msg is_build done
    fi
else
    msg ie_missing mgo1host.exe
    exit 1
fi
# the agent, on the sandbox's folders; bot 1
cp "$ST/mgo1host-agent" "$R/opt/mgo1host-agent"
cat > "$R/bin/mgo1host-agent" <<WRAP
#!/bin/sh
export MGO1_CONF_DIR=$R/etc MGO1_STATE_DIR=$R/var MGO1_LOG_DIR=$R/log MGO1_HOME_DIR=$R/opt MGO1_SYSTEMCTL=$R/bin/systemctl MGO1_SYSTEMD_DIR=$R/systemd
exec python3 $R/opt/mgo1host-agent "\$@"
WRAP
chmod 0755 "$R/bin/mgo1host-agent"
[ -f "$R/etc/bot1.env" ] || printf 'UDP_PORT=5730\n' > "$R/etc/bot1.env"
echo "build 1.00 $(sha256sum "$R/opt/mgo1host.exe" | cut -c1-12)" > "$R/opt/VERSION"
msg is_firewall
if [ "$("$R/bin/systemctl" is-active mgo1host@1)" = active ]; then "$R/bin/systemctl" restart mgo1host@1; msg is_restart 1; fi
echo "== installed $(cat "$R/opt/VERSION")"
msg is_installed "$(cat "$R/opt/VERSION")"
exit 0
