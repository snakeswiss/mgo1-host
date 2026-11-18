#!/bin/bash
# Installs or updates the dedicated host on an x86-64 Debian / Ubuntu server (as root), from this
# folder: mgo1host.exe (host/build_host.sh), mgo1host.sh, mgo1host-agent, mgo1hostctl,
# mgo1host@.service, mgo1log (the daily logs), the PLAYABLE DOLLs' mgo1doll.sh, mgo1doll@.service, mgo1doll-manager and its
# service, mgo1-dollgate (the accountless dolls' lobby, one a bot on 127.0.0.1), steps/ (login_account.txt, host_create.txt, doll_join.txt), bot.env.example,
# doll.env.example, server.env.example, game.ini.example, VERSION, and the disc's files in ./disc
# (MGS/NET/MGS3_N.ELF, STAGE_N.DAT, SLOT_N.DAT, MODULES/): without them there, the ones already
# installed stay.
# (build 1.00, srvbuild) MGO1 Host's public package holds no mgo1host.exe (it is the game recompiled from the disc) but
# mgo1host-src.tar.gz: the host is then built here, from these sources and the disc's files (host/build_server.sh,
# docs/DEDICATED_HOST.md section 27: the first time 20 to 60 minutes; an update compiles only what changed).
# The server runs up to 10 bots (mgo1host@1 ... mgo1host@10), each with its own SaveMGO account,
# game and UDP port (5730 + bot - 1); the first install makes bot 1. An install of the single-host
# layout (/etc/mgo1host.env, mgo1host.service) becomes bot 1.
# MGO1 Host.exe runs it over SSH; by hand: bash install.sh
set -euo pipefail
SRC=$(cd "$(dirname "$0")" && pwd)
# a step or an error: MGO1 Host.exe asks for them as "@key|argument" (MGO1_MARKS=1) and shows them
# in its own language; by hand, the English text
msg() {   # key, English text, arguments
    local k=$1 t=$2; shift 2
    if [ "${MGO1_MARKS:-0}" = 1 ]; then local IFS='|'; echo "@$k${*:+|$*}"; else echo "$t"; fi
}
APTLOG=/var/log/mgo1host-install.log
apt_get() {   # apt-get's output in APTLOG for MGO1 Host.exe (its last line on an error), else on the screen
    export DEBIAN_FRONTEND=noninteractive
    if [ "${MGO1_MARKS:-0}" = 1 ]; then
        apt-get "$@" >>"$APTLOG" 2>&1 || { msg ie_apt "" "$(tail -n 1 "$APTLOG" | tr '|' ' ')"; return 1; }
    else
        apt-get "$@"
    fi
}
[ "$(id -u)" = 0 ] || { msg ie_root "install: run it as root"; exit 1; }
[ "$(uname -m)" = x86_64 ] || { msg ie_arch "install: this server is $(uname -m); the host needs x86_64 (Hetzner CX / CPX, not CAX)" "$(uname -m)"; exit 1; }
EXE="$SRC/mgo1host.exe"; VERFILE="$SRC/VERSION"; BUILD=0
if [ ! -f "$EXE" ]; then
    [ -f "$SRC/mgo1host-src.tar.gz" ] || { msg ie_missing "install: mgo1host.exe is missing next to install.sh" mgo1host.exe; exit 1; }
    BUILD=1
fi
DISC_NEW=1
for f in MGS/NET/MGS3_N.ELF MGS/NET/STAGE_N.DAT MGS/NET/SLOT_N.DAT; do [ -f "$SRC/disc/$f" ] || DISC_NEW=0; done
[ -d "$SRC/disc/MODULES" ] || DISC_NEW=0
if [ $DISC_NEW = 0 ]; then
    for f in MGS/NET/MGS3_N.ELF MGS/NET/STAGE_N.DAT MGS/NET/SLOT_N.DAT; do
        [ -f "/opt/mgo1host/disc/$f" ] || { msg ie_disc_missing "install: the disc's files are missing (copy them into $SRC/disc: $f)" "$f"; exit 1; }
    done
fi
WINE_MISSING=0
command -v wine64 >/dev/null 2>&1 || [ -x /usr/lib/wine/wine64 ] || WINE_MISSING=1
if [ $BUILD = 1 ]; then
    # (build 1.00, srvbuild) the host built here: the North American disc's files (the sound modules too), then the room for
    # all of it before anything is installed (the toolchain, the build's tree, Wine, the disc's copy)
    DISC_DIR=/opt/mgo1host/disc; [ $DISC_NEW = 1 ] && DISC_DIR=$SRC/disc
    [ -f "$DISC_DIR/MODULES/MODNCD.IPK" ] || { msg ie_disc_missing "install: the disc's files are missing (copy them into $SRC/disc: MODULES/MODNCD.IPK)" MODULES/MODNCD.IPK; exit 1; }
    BTMP=$(mktemp -d); trap 'rm -rf "$BTMP"' EXIT
    tar -xzf "$SRC/mgo1host-src.tar.gz" -C "$BTMP" mgo1host-src/host/build_server.sh mgo1host-src/tools/ps2_images.py mgo1host-src/tools/stage_dat.py 2>"$BTMP/tar.txt" \
        || { msg ie_build "install: mgo1host-src.tar.gz is damaged ($(tail -n 1 "$BTMP/tar.txt"))" "mgo1host-src.tar.gz: $(tail -n 1 "$BTMP/tar.txt" | tr '|' ' ')"; exit 1; }
    BUILDER="$BTMP/mgo1host-src/host/build_server.sh"
    bash "$BUILDER" --check-disc --disc "$DISC_DIR" || exit 1           # the North American disc (said by name if not)
    NEED=$(bash "$BUILDER" --need)
    [ $WINE_MISSING = 1 ] && NEED=$((NEED + 900))                         # wine64 and its libraries (Ubuntu 24.04)
    [ $DISC_NEW = 1 ] && NEED=$((NEED + $(du -smL "$SRC/disc" | cut -f1)))  # the disc's copy in /opt/mgo1host/disc
    d=/opt/mgo1host; while [ ! -d "$d" ]; do d=$(dirname "$d"); done
    FREE=$(df -Pm "$d" | awk 'NR == 2 {print $4}')
    if [ "$FREE" -lt "$NEED" ]; then
        msg ie_space "install: $FREE MB free on the server, building and installing the host needs $NEED MB (free some room, or a server with a larger disk)" "$FREE" "$NEED"
        exit 1
    fi
fi

msg is_wine "== Wine"
if [ $WINE_MISSING = 1 ]; then
    msg is_wine_get "(apt-get install wine64: a few minutes)"
    apt_get update -y -q
    apt_get install -y -q --no-install-recommends wine64 || apt_get install -y -q --no-install-recommends wine
fi
command -v python3 >/dev/null 2>&1 || apt_get install -y -q python3
WINE=$(command -v wine64 || echo /usr/lib/wine/wine64)
WV=$("$WINE" --version 2>/dev/null | tr '|' ' ')
msg is_wine_version "$WV" "$WV"

if [ $BUILD = 1 ]; then
    # (build 1.00, srvbuild) the host from the sources and the disc: its marks @is_build|deps|gen|compile|link|done,
    # @is_build_pct|<n> every 30 s, @ie_build|<line> or @ie_space|<free MB>|<MB needed> on an error
    bash "$BUILDER" --src "$SRC/mgo1host-src.tar.gz" --disc "$DISC_DIR" --out /opt/mgo1host/build/mgo1host.exe || exit 1
    EXE=/opt/mgo1host/build/mgo1host.exe
    # VERSION: the package's "build 1.00 source" becomes "build 1.00 <the built executable's SHA-256, 12 digits>"
    V=$(head -n 1 "$SRC/VERSION" 2>/dev/null | sed 's/ source$//')
    [ -n "$V" ] || V=$(sed -n 's/^version=//p' /opt/mgo1host/build/src/SOURCE_INFO 2>/dev/null)
    VERFILE="$BTMP/VERSION"
    echo "${V:-build} $(sha256sum "$EXE" | cut -c1-12)" > "$VERFILE"
fi

msg is_files "== the user and the files"
id mgo1host >/dev/null 2>&1 || useradd --system --home-dir /var/lib/mgo1host --create-home --shell /usr/sbin/nologin mgo1host
install -d -m 0755 -o root -g root /opt/mgo1host /opt/mgo1host/steps
install -m 0644 "$EXE" /opt/mgo1host/mgo1host.exe.new && mv -f /opt/mgo1host/mgo1host.exe.new /opt/mgo1host/mgo1host.exe
if [ $BUILD = 1 ]; then rm -f "$EXE"; fi                 # (build 1.00, srvbuild) the build's copy: the installed one is it
install -m 0755 "$SRC/mgo1host.sh" /opt/mgo1host/mgo1host.sh
install -m 0644 "$SRC"/steps/*.txt /opt/mgo1host/steps/
install -m 0644 "$SRC/bot.env.example" "$SRC/game.ini.example" /opt/mgo1host/
[ -f "$VERFILE" ] && install -m 0644 "$VERFILE" /opt/mgo1host/VERSION
install -m 0755 "$SRC/mgo1hostctl" /usr/local/bin/mgo1hostctl
install -m 0755 "$SRC/mgo1host-agent" /usr/local/bin/mgo1host-agent
# the daily logs of every bot, doll and the doll manager: /var/log/mgo1host/<bot1|doll1|manager>/<day>.log
[ -f "$SRC/mgo1log" ] && install -m 0755 "$SRC/mgo1log" /usr/local/bin/mgo1log
# the PLAYABLE DOLLs (docs/PLAYABLE_DOLL.md): players the server runs in its bots' games
DOLLS=0
if [ -f "$SRC/mgo1doll.sh" ]; then
    DOLLS=1
    install -m 0755 "$SRC/mgo1doll.sh" /opt/mgo1host/mgo1doll.sh
    install -m 0644 "$SRC/doll.env.example" /opt/mgo1host/
    install -m 0755 "$SRC/mgo1doll-manager" /usr/local/bin/mgo1doll-manager
fi
# the accountless dolls' lobby (docs/PLAYABLE_DOLL.md 1): each bot starts one on 127.0.0.1 (mgo1host.sh)
[ -f "$SRC/mgo1-dollgate" ] && install -m 0755 "$SRC/mgo1-dollgate" /opt/mgo1host/mgo1-dollgate
if [ $DISC_NEW = 1 ]; then
    rm -rf /opt/mgo1host/disc.new; cp -r "$SRC/disc" /opt/mgo1host/disc.new
    rm -rf /opt/mgo1host/disc; mv /opt/mgo1host/disc.new /opt/mgo1host/disc
fi
chmod -R a+rX /opt/mgo1host/disc
# (v141) the PC maps (maps 13 and up, docs/EXTRA_MAPS.md): ./maps as MGO1.exe has them, replaced as a whole; the host and the
# dolls build them from the disc's own stages at their start (no folder: the twelve maps only)
if [ -d "$SRC/maps" ] && ls "$SRC"/maps/*/map.ini >/dev/null 2>&1; then
    rm -rf /opt/mgo1host/maps.new; cp -r "$SRC/maps" /opt/mgo1host/maps.new
    rm -rf /opt/mgo1host/maps; mv /opt/mgo1host/maps.new /opt/mgo1host/maps
    chmod -R a+rX /opt/mgo1host/maps
fi
install -d -m 0755 -o mgo1host -g mgo1host /var/lib/mgo1host
install -d -m 0750 -o mgo1host -g mgo1host /var/log/mgo1host
install -d -m 0755 -o root -g root /etc/mgo1host
[ -f /etc/mgo1host/server.env ] || install -m 0644 "$SRC/server.env.example" /etc/mgo1host/server.env

# the single-host layout (before bots): it becomes bot 1
if [ -f /etc/mgo1host.env ] && [ ! -f /etc/mgo1host/bot1.env ]; then
    msg is_migrate "== the host becomes bot 1"
    WAS=$(systemctl is-active mgo1host 2>/dev/null || true)
    systemctl disable --now mgo1host >/dev/null 2>&1 || true
    grep -v -E '^(WINE|DISC_DIR|P2P_LOG|EXTRA_ENV)=' /etc/mgo1host.env > /etc/mgo1host/bot1.env
    grep -E '^(DISC_DIR|P2P_LOG|EXTRA_ENV)=' /etc/mgo1host.env >> /etc/mgo1host/server.env || true
    grep -q '^UDP_PORT=' /etc/mgo1host/bot1.env || echo "UDP_PORT=5730" >> /etc/mgo1host/bot1.env
    chmod 0600 /etc/mgo1host/bot1.env
    [ -f /etc/mgo1host.game ] && mv /etc/mgo1host.game /etc/mgo1host/bot1.game
    [ -f /etc/mgo1host.whitelist ] && mv /etc/mgo1host.whitelist /etc/mgo1host/bot1.whitelist
    [ -f /etc/mgo1host.bans ] && mv /etc/mgo1host.bans /etc/mgo1host/blacklist
    rm -f /etc/mgo1host.env /etc/systemd/system/mgo1host.service
    install -d -m 0755 -o mgo1host -g mgo1host /var/lib/mgo1host/bot1
    for d in run wine events; do [ -d /var/lib/mgo1host/$d ] && mv /var/lib/mgo1host/$d /var/lib/mgo1host/bot1/; done
    [ "$WAS" = active ] && touch /var/lib/mgo1host/bot1/.was-active
fi
grep -q '^WINE=' /etc/mgo1host/server.env || echo "WINE=$WINE" >> /etc/mgo1host/server.env
[ -f /etc/mgo1host/blacklist ] || printf '# MGO1 dedicated host: the blacklist of every bot of this server, names kicked as soon as they join\n' > /etc/mgo1host/blacklist
chmod 0644 /etc/mgo1host/server.env /etc/mgo1host/blacklist
# bot 1, the first time (its account comes from MGO1 Host.exe, or by hand in /etc/mgo1host/bot1.env)
if ! ls /etc/mgo1host/bot*.env >/dev/null 2>&1; then
    sed "s/^UDP_PORT=.*/UDP_PORT=5730/" "$SRC/bot.env.example" > /etc/mgo1host/bot1.env; chmod 0600 /etc/mgo1host/bot1.env
fi
for e in /etc/mgo1host/bot*.env; do
    n=$(basename "$e" .env); n=${n#bot}
    [ -f /etc/mgo1host/bot$n.game ] || install -m 0644 "$SRC/game.ini.example" /etc/mgo1host/bot$n.game
    [ -f /etc/mgo1host/bot$n.whitelist ] || printf '# MGO1 dedicated host: the whitelist of bot %s\n# only = on: only these names may play (the others are kicked as soon as they join)\nonly = off\n' "$n" > /etc/mgo1host/bot$n.whitelist
    chmod 0644 /etc/mgo1host/bot$n.game /etc/mgo1host/bot$n.whitelist
    install -d -m 0755 -o mgo1host -g mgo1host /var/lib/mgo1host/bot$n /var/lib/mgo1host/bot$n/events
    install -d -m 0750 -o mgo1host -g mgo1host /var/log/mgo1host/bot$n
done
# the owner's actions written before v126 held the account ID and the servers' addresses: without them now
python3 - <<'PYEOF' || true
import glob, json, os
for p in glob.glob("/var/lib/mgo1host/bot*/events/*.admin.jsonl"):
    out, changed = [], False
    for line in open(p, encoding="utf-8", errors="replace"):
        try:
            e = json.loads(line)
        except ValueError:
            out.append(line); continue
        drop = ("a",) if e.get("do") == "account" else ("gate", "stun") if e.get("do") == "servers" else ()
        if any(k in e for k in drop):
            for k in drop:
                e.pop(k, None)
            line, changed = json.dumps(e, separators=(",", ":")) + "\n", True
        out.append(line)
    if changed:
        with open(p + ".new", "w", encoding="utf-8") as f:
            f.writelines(out)
        os.replace(p + ".new", p)
PYEOF
for e in /etc/mgo1host/doll*.env; do                 # the dolls' folders: theirs (status, orders)
    [ -f "$e" ] || continue
    n=$(basename "$e" .env); n=${n#doll}
    chmod 0600 "$e"
    install -d -m 0755 -o mgo1host -g mgo1host /var/lib/mgo1host/doll$n
    install -d -m 0750 -o mgo1host -g mgo1host /var/log/mgo1host/doll$n
done
chown -R mgo1host:mgo1host /var/lib/mgo1host
sed "s|^ExecStart=.*|ExecStart=/opt/mgo1host/mgo1host.sh|" "$SRC/mgo1host@.service" > /etc/systemd/system/mgo1host@.service
chmod 0644 /etc/systemd/system/mgo1host@.service
# (v141 beta 4) the bots' games first when the processor is short, the dolls' after (docs/DEDICATED_HOST.md, "Lag"): their
# slices, ten times the weight for the bots' (cgroup v2; the units' Nice= does the same where cgroups are not delegated)
cat > /etc/systemd/system/mgo1host.slice <<'SLICE'
[Unit]
Description=SaveMGO MGO1 dedicated host bots (their games first when the processor is short)
Before=slices.target

[Slice]
CPUWeight=1000
SLICE
cat > /etc/systemd/system/mgo1doll.slice <<'SLICE'
[Unit]
Description=SaveMGO MGO1 PLAYABLE DOLLs (after the bots when the processor is short)
Before=slices.target

[Slice]
CPUWeight=100
SLICE
chmod 0644 /etc/systemd/system/mgo1host.slice /etc/systemd/system/mgo1doll.slice
if [ $DOLLS = 1 ]; then
    sed "s|^ExecStart=.*|ExecStart=/opt/mgo1host/mgo1doll.sh|" "$SRC/mgo1doll@.service" > /etc/systemd/system/mgo1doll@.service
    install -m 0644 "$SRC/mgo1doll-manager.service" /etc/systemd/system/mgo1doll-manager.service
    chmod 0644 /etc/systemd/system/mgo1doll@.service
fi

msg is_prefix "== the Wine prefix of bot 1 (once)"
[ -d /var/lib/mgo1host/bot1/wine ] || runuser -u mgo1host -- env HOME=/var/lib/mgo1host WINEPREFIX=/var/lib/mgo1host/bot1/wine WINEDEBUG=-all "$WINE" wineboot -i >/dev/null 2>&1 || true

msg is_firewall "== firewall"
for e in /etc/mgo1host/bot*.env; do
    PORT=$(sed -n 's/^UDP_PORT=//p' "$e"); PORT=${PORT:-5730}
    if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q "Status: active"; then
        ufw allow "$PORT/udp" >/dev/null && msg is_fw_open "ufw: UDP $PORT open" ufw "$PORT"
    elif command -v firewall-cmd >/dev/null 2>&1 && firewall-cmd --state >/dev/null 2>&1; then
        firewall-cmd -q --permanent --add-port="$PORT/udp" && firewall-cmd -q --reload && msg is_fw_open "firewalld: UDP $PORT open" firewalld "$PORT"
    fi
    msg is_fw_provider "(a Hetzner Cloud Firewall, if you use one, needs an inbound rule UDP $PORT too)" "$PORT"
done

systemctl daemon-reload
for e in /etc/mgo1host/bot*.env; do
    n=$(basename "$e" .env); n=${n#bot}
    if [ -f /var/lib/mgo1host/bot$n/.was-active ]; then
        rm -f /var/lib/mgo1host/bot$n/.was-active
        systemctl enable --now "mgo1host@$n" >/dev/null 2>&1 || true
    elif systemctl is-active --quiet "mgo1host@$n"; then
        msg is_restart "== restarting bot $n (the new version)" "$n"; systemctl restart "mgo1host@$n"
    fi
done
if [ $DOLLS = 1 ]; then
    # the manager starts the dolls to their bots' free seats; the running ones play the new version
    systemctl enable mgo1doll-manager >/dev/null 2>&1 || true
    systemctl restart mgo1doll-manager >/dev/null 2>&1 || true
    for e in /etc/mgo1host/doll*.env; do
        [ -f "$e" ] || continue
        n=$(basename "$e" .env); n=${n#doll}
        systemctl is-active --quiet "mgo1doll@$n" && systemctl restart "mgo1doll@$n"
    done
fi
V=$(cat /opt/mgo1host/VERSION 2>/dev/null | tr '|' ' ' || true)
msg is_installed "== installed $V" "$V"
if [ "${MGO1_MARKS:-0}" != 1 ]; then
    echo "Next: a bot's SaveMGO account in /etc/mgo1host/bot1.env, its game in /etc/mgo1host/bot1.game, then"
    echo "      systemctl enable --now mgo1host@1 ; mgo1hostctl -b 1 log   (MGO1 Host.exe does all of it)"
fi
