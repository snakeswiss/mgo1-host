#!/bin/bash
# (build 1.00, srvbuild) The public package's install from nothing on a clean Ubuntu 24.04 (docs/DEDICATED_HOST.md section 27):
# a debootstrap root (minbase: not even python3), the package's server folder staged as MGO1 Host stages it, the disc's
# files read in place (bound read-only: nothing of the disc copied), then `env MGO1_MARKS=1 bash install.sh` as MGO1 Host
# runs it, each line with its second, the disk used every 20 s. Root only; the machine's own /opt, /etc ... untouched.
#   ROOTFS=<folder> PKG=<the public package's server folder> DISC=<the North American disc's folder> OUT=<results folder> \
#     [SUITE=noble] [DEBOOTSTRAP=<debootstrap>] [STANDIN_WINE=1] [CA=<CA bundle for a TLS proxy>] bash tools/dev150/cleanroom_build.sh [again]
# ROOTFS is made with debootstrap when it holds no system yet (about 135 MB, then the apt lists); "again" runs install.sh once
# more in the same root (an update, or the same package: no build). STANDIN_WINE=1 (default): a stand-in wine64 and
# systemctl (Wine's 800 MB are not what this checks; a chroot runs no systemd). OUT gets marks.txt, du.txt, the build's log,
# built.txt and mgo1host.exe (the built executable: it is the game, keep it on this machine). Delete ROOTFS when done.
set -u
ROOTFS=${ROOTFS:?ROOTFS}; PKG=${PKG:?PKG}; DISC=${DISC:?DISC}; OUT=${OUT:?OUT}
STANDIN_WINE=${STANDIN_WINE:-1}; SUITE=${SUITE:-noble}            # (jammy: Ubuntu 22.04)
[ "$(id -u)" = 0 ] || { echo "cleanroom_build.sh: root only"; exit 2; }
mkdir -p "$OUT" "$ROOTFS"
if [ ! -x "$ROOTFS/usr/bin/apt-get" ]; then
    ${DEBOOTSTRAP:-debootstrap} --variant=minbase "$SUITE" "$ROOTFS" https://archive.ubuntu.com/ubuntu > "$OUT/debootstrap.log" 2>&1 \
        || { echo "debootstrap failed (its log: $OUT/debootstrap.log)"; exit 1; }
    printf 'deb https://archive.ubuntu.com/ubuntu %s main universe\ndeb https://archive.ubuntu.com/ubuntu %s-updates main universe\ndeb https://security.ubuntu.com/ubuntu %s-security main universe\n' \
        "$SUITE" "$SUITE" "$SUITE" > "$ROOTFS/etc/apt/sources.list"
    if [ -n "${CA:-}" ]; then
        cp "$CA" "$ROOTFS/etc/ssl/cleanroom-proxy-ca.crt"
        printf 'Acquire::https::CAInfo "/etc/ssl/cleanroom-proxy-ca.crt";\n' > "$ROOTFS/etc/apt/apt.conf.d/99cleanroom"
        [ -n "${HTTPS_PROXY:-}" ] && printf 'Acquire::https::Proxy "%s";\n' "$HTTPS_PROXY" >> "$ROOTFS/etc/apt/apt.conf.d/99cleanroom"
    fi
    # apt's lists, as a server's image has them (install.sh updates them only when it installs Wine)
    chroot "$ROOTFS" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin ${HTTPS_PROXY:+https_proxy=$HTTPS_PROXY} apt-get update -q > "$OUT/apt_update.log" 2>&1 \
        || { echo "apt-get update in the root failed (its log: $OUT/apt_update.log)"; exit 1; }
fi
if [ "$STANDIN_WINE" = 1 ]; then
    printf '#!/bin/sh\n# (the clean-room test'"'"'s stand-in: Wine is not what it checks)\n[ "$1" = --version ] && echo "wine-9.0 (stand-in)"\nexit 0\n' > "$ROOTFS/usr/local/bin/wine64"
    printf '#!/bin/sh\necho "systemctl $*" >> /var/log/systemctl-standin.log\ncase "$1" in is-active) exit 3 ;; esac\nexit 0\n' > "$ROOTFS/usr/local/bin/systemctl"
    chmod 0755 "$ROOTFS/usr/local/bin/wine64" "$ROOTFS/usr/local/bin/systemctl"
fi
ST="$ROOTFS/root/mgo1host-install"
rm -rf "$ST"; mkdir -p "$ST/disc"
cp -r "$PKG/." "$ST/"
rm -rf "$ST/disc"; mkdir -p "$ST/disc"
N=1; [ "${1:-}" = again ] && N=$(( $(ls -d "$OUT"/run* 2>/dev/null | wc -l) + 1 ))
O="$OUT/run$N"; mkdir -p "$O"
du -sm "$ROOTFS" | cut -f1 > "$O/du_start.txt"
( while sleep 20; do echo "$(date +%s) $(du -sm "$ROOTFS" 2>/dev/null | cut -f1) $(cut -d' ' -f1 /proc/loadavg)"; done ) > "$O/du.txt" &
MON=$!
T0=$(date +%s)
# the disc's folder bound read-only at the stage's disc/ (its links resolve under the same paths: their folders bound too)
LINKS=$(find "$DISC" -type l -exec readlink -f {} \; | xargs -r -n1 dirname | sort -u)
unshare -m --propagation private bash -c '
R=$1 DISC=$2 LINKS=$3
mount -t proc proc "$R/proc"; mount --rbind /dev "$R/dev"; mount -t sysfs sys "$R/sys" 2>/dev/null; mount -t tmpfs tmpfs "$R/tmp"
for d in $LINKS; do mkdir -p "$R$d"; mount --bind "$d" "$R$d" && mount -o remount,bind,ro "$R$d"; done
mount --bind "$DISC" "$R/root/mgo1host-install/disc" && mount -o remount,bind,ro "$R/root/mgo1host-install/disc"
exec chroot "$R" /usr/bin/env -i PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin HOME=/root TERM=dumb \
    ${HTTPS_PROXY:+https_proxy=$HTTPS_PROXY} bash -c "env MGO1_MARKS=1 bash /root/mgo1host-install/install.sh 2>&1; echo MGO1HOST_INSTALL_EXIT=\$?"
' cleanroom "$ROOTFS" "$(realpath "$DISC")" "$LINKS" | while IFS= read -r l; do echo "$(( $(date +%s) - T0 )) $l"; done > "$O/marks.txt"
kill $MON 2>/dev/null
du -sm "$ROOTFS" | cut -f1 > "$O/du_end.txt"
du -sm "$ROOTFS/opt/mgo1host/build" 2>/dev/null | cut -f1 > "$O/du_tree.txt"
cp "$ROOTFS/var/log/mgo1host-build.log" "$O/build.log" 2>/dev/null
cp "$ROOTFS/opt/mgo1host/build/built.txt" "$O/built.txt" 2>/dev/null
cp "$ROOTFS/opt/mgo1host/mgo1host.exe" "$O/mgo1host.exe" 2>/dev/null
cat "$ROOTFS/opt/mgo1host/VERSION" > "$O/VERSION" 2>/dev/null
rm -rf "$ST"
echo "cleanroom run $N: $(( $(date +%s) - T0 )) s, $(tail -n 2 "$O/marks.txt" | head -n 1)"
echo "  peak $(sort -k2 -n "$O/du.txt" | tail -n 1 | cut -d' ' -f2) MB (start $(cat "$O/du_start.txt"), end $(cat "$O/du_end.txt"), tree $(cat "$O/du_tree.txt" 2>/dev/null) MB); VERSION $(cat "$O/VERSION" 2>/dev/null)"
grep -q '^[0-9]* MGO1HOST_INSTALL_EXIT=0$' "$O/marks.txt"
