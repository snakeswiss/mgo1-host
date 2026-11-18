#!/bin/bash
# One bot of the dedicated host: MGO1 recompiled (mgo1host.exe), headless, Host Only, under Wine.
# Started by systemd (mgo1host@<bot>.service) with /etc/mgo1host/server.env and
# /etc/mgo1host/bot<bot>.env; ends when the game ends or fails (the service starts it again: a new
# game). Each bot has its own SaveMGO account, UDP port, game settings, whitelist and state; the
# blacklist is the server's, for all of its bots.
set -u
BOT=${BOT:-1}
case "$BOT" in [1-9]|10) ;; *) echo "mgo1host: BOT must be 1 to 10"; exit 1 ;; esac
HOME_DIR=${HOME_DIR:-/opt/mgo1host}
STATE=${STATE_DIR:-/var/lib/mgo1host/bot$BOT}
# the bot's daily log (docs/LOGS.md): /var/log/mgo1host/bot<N>/<YYYY-MM-DD>.log, the game's lines and
# this script's, time-stamped with their level (E error, W warning, I); kept 31 days
LOGDIR=${LOG_DIR:-/var/log/mgo1host}/bot$BOT
MGO1LOG=${MGO1LOG:-/usr/local/bin/mgo1log}
say() {   # a line of this script's: the journal and the daily log (level I, W or E)
    if [ -x "$MGO1LOG" ]; then "$MGO1LOG" --dir "$LOGDIR" --note "$1" --level "${2:-I}" || echo "$1"; else echo "$1"; fi
}
RUN=$STATE/run
EVENTS=$STATE/events
EXE=$HOME_DIR/mgo1host.exe
# (v150-host beta 16) starts that end early (a setting or a disc file missing, a game dead in its first minutes) came every 5 s
# for ever, each one a Wine start and its lines in the log. Now the next start waits: nothing after the first early end, then
# 30 s more each time (30, 60 ... 600 s at most), until a game lasts 10 minutes, ends as asked or the service is stopped.
# This script writes when the next start may come ($STATE/.next_start); the wait itself is the service's ExecStartPre
# (mgo1host@.service: "activating" meanwhile, no game for the watchdog to take for a hung one)
QUICK=$STATE/.quick_ends
early_end() {   # $1: what to add to the line (a start that cannot work: "the same reason")
    mkdir -p "$STATE" 2>/dev/null
    N=$(( 10#0$(tr -cd 0-9 < "$QUICK" 2>/dev/null | head -c 6) + 1 ))
    P=$(( (N - 1) * 30 )); [ "$P" -gt 600 ] && P=600
    echo "$N" > "$QUICK" 2>/dev/null
    [ "$P" -gt 0 ] || return 0
    echo $(( $(date +%s) + P )) > "$STATE/.next_start" 2>/dev/null
    say "mgo1host: $N starts in a row ended early: the next one in $P s${1:+ ($1)}" W
}
lasted() { rm -f "$QUICK" "$STATE/.quick_why" "$STATE/.next_start" 2>/dev/null; }
fail() {        # a start that cannot work: its reason once in the bot's log (each time in the journal), then the wait
    mkdir -p "$STATE" 2>/dev/null
    if [ "$(cat "$STATE/.quick_why" 2>/dev/null)" = "$1" ]; then echo "$1"; else say "$1" E; printf '%s' "$1" > "$STATE/.quick_why" 2>/dev/null; fi
    early_end "the same reason"
    exit 1
}
[ -n "${HOST_ACCOUNT_ID:-}" ] || fail "mgo1host: HOST_ACCOUNT_ID is not set (/etc/mgo1host/bot$BOT.env)"
[ -n "${HOST_ACCOUNT_PASSWORD:-}" ] || fail "mgo1host: HOST_ACCOUNT_PASSWORD is not set (/etc/mgo1host/bot$BOT.env)"
# the game types them on its keyboard: letters, digits, - _ . only
for v in "$HOST_ACCOUNT_ID" "$HOST_ACCOUNT_PASSWORD"; do
    case "$v" in *[!A-Za-z0-9._-]*)
        fail "mgo1host: HOST_ACCOUNT_ID / HOST_ACCOUNT_PASSWORD may only hold letters, digits, - _ . (/etc/mgo1host/bot$BOT.env)" ;;
    esac
done
# MGO1 accounts are in capitals (the game turns Caps Lock on): typed as capitals
HOST_ACCOUNT_ID=$(printf '%s' "$HOST_ACCOUNT_ID" | tr a-z A-Z)
HOST_ACCOUNT_PASSWORD=$(printf '%s' "$HOST_ACCOUNT_PASSWORD" | tr a-z A-Z)
# the game's UDP port (MGO1's [UDP Port No.]: 5730 to 5739), one per bot; the game's other UDP
# ports (UPnP 1900...) move by 100 per bot so that bots never share one
UDP_PORT=${UDP_PORT:-$((5729 + BOT))}
SHIFT=$((100 * (BOT - 1)))
DISC_DIR=${DISC_DIR:-$HOME_DIR/disc}
for f in MGS/NET/MGS3_N.ELF MGS/NET/STAGE_N.DAT MGS/NET/SLOT_N.DAT; do
    [ -f "$DISC_DIR/$f" ] || fail "mgo1host: $DISC_DIR/$f is missing (the disc's files)"
done
[ -d "$DISC_DIR/MODULES" ] || fail "mgo1host: $DISC_DIR/MODULES is missing"
# (v141) the PC maps (maps 13 and up): MAPS_DIR, else the install's maps folder; none: the twelve maps only
MAPS_DIR=${MAPS_DIR:-$HOME_DIR/maps}; MAPS=""
mkdir -p "$STATE" "$EVENTS"

# the Game Report: this start, as the first line of the day's events (the game writes the others)
note() {   # kind, extra JSON fields
    printf '{"t":%s,"k":"%s"%s}\n' "$(date +%s%3N)" "$1" "${2:-}" >> "$EVENTS/$(date -u +%Y-%m-%d).jsonl"
}
note start ",\"bot\":$BOT,\"port\":$UDP_PORT,\"v\":\"$(tr -dc 'A-Za-z0-9 ._-' < "$HOME_DIR/VERSION" 2>/dev/null)\""

# a fresh run folder each start: an empty memory card (the log-in always takes the same screens)
rm -rf "$RUN"; mkdir -p "$RUN"
ln -s "$DISC_DIR/MGS" "$RUN/MGS"; ln -s "$DISC_DIR/MODULES" "$RUN/MODULES"
# the steps: log in with the account, then Create Game (Host Only set by MGO1_HOST_ONLY); the password is
# only in this file, which only the service's user reads and the game deletes once read (SIM_STEPS_DELETE)
umask 077
esc() { printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'; }      # the values as sed replacements
sed -e "s|@ID@|$(esc "$HOST_ACCOUNT_ID")|" -e "s|@PASSWORD@|$(esc "$HOST_ACCOUNT_PASSWORD")|" "$HOME_DIR/steps/login_account.txt" > "$RUN/steps.txt"
cat "$HOME_DIR/steps/host_create.txt" >> "$RUN/steps.txt"
: > "$RUN/cmd.txt"; rm -f "$RUN/cmd.txt"

export WINEPREFIX=$STATE/wine WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
# (v141 beta 9) no D-Bus for Wine: its drive manager (mountmgr, in winedevice.exe) follows udisks2 on the system bus, and a
# busy udisks2 kept each prefix's winedevice.exe on most of a core (a server of 14 October; Wine bug 49623)
export DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent
WINE=${WINE:-wine64}
# (v141 beta 12) CPU_PIN=1 (server.env): the game, its Wine server and helpers on one processor (its threads take
# turns: a turn handed over on the same processor does not wait for a virtual processor the hosting machine has not
# scheduled); the bots from the first processor up, the dolls from the last one down. 0 (the default): off
PIN=()
if [ "${CPU_PIN:-0}" = 1 ] && command -v taskset >/dev/null 2>&1; then
    NCPU=$(nproc 2>/dev/null || echo 1); case "$NCPU" in ""|*[!0-9]*|0) NCPU=1 ;; esac
    PIN=(taskset -c $(((BOT - 1) % NCPU)))
fi
win() { echo "Z:$1" | tr / '\\'; }
# the bot's own Wine prefix, made the first time
[ -d "$WINEPREFIX" ] || ${PIN[@]+"${PIN[@]}"} "$WINE" wineboot -i >/dev/null 2>&1 || true
P2P=""; [ "${P2P_LOG:-0}" = 1 ] && P2P="MGO1_P2P_TSV=$(win "$RUN/p2p.tsv")"
# the servers (gate, STUN, web) when not SaveMGO's: /etc/mgo1host/servers.ini, for every bot (the game's
# menu script takes them, as the PC launcher's Server tab does)
SERVERS=${SERVERS_FILE:-/etc/mgo1host/servers.ini}
[ -f "$SERVERS" ] && cp "$SERVERS" "$RUN/servers.ini"
# the game's settings (name, password, rules and maps, times...): GAME_SETTINGS, else the lobby's
GAME=${GAME_SETTINGS:-/etc/mgo1host/bot$BOT.game}; SET=""
# (v150-host beta 22, sync) run/game.src: the settings this game starts with, an exact copy of the bot's file (game.ini is what
# the game reads: less a rotation's maps this server does not have, below); mgo1host-agent settings-get compares the bot's file
# with it to say what a save changed that the running game does not play yet ("pending": its next game, or a restart now).
# One read of the file for both copies, so they are the same version even when MGO1 Host saves at that moment
if [ -f "$GAME" ] && cp "$GAME" "$RUN/game.src"; then cp "$RUN/game.src" "$RUN/game.ini"; SET="MGO1_HOST_SETTINGS=$(win "$RUN/game.ini")"; fi
# the blacklist (the server's: kicked as soon as they join) and this bot's whitelist, read again when they change
BANS=${BANS_FILE:-/etc/mgo1host/blacklist}; WHITE=${WHITELIST_FILE:-/etc/mgo1host/bot$BOT.whitelist}
[ -d "$MAPS_DIR" ] && ls "$MAPS_DIR"/*/map.ini >/dev/null 2>&1 && MAPS="MGO1_MAPS_DIR=$(win "$MAPS_DIR")"
# (v150-host beta 16) no PC maps on this server (maps 13 and up, $MAPS_DIR): the rotation's entries on them are left out of the
# game's own copy of the settings, and with none left the rotation line itself: the game keeps its account's last rotation
# (given a rotation of such maps only it was left with an empty one). Said in a warning; the settings file is not changed
if [ -z "$MAPS" ] && [ -f "$RUN/game.ini" ]; then
    ROT=$(sed -n 's/^[[:space:]]*rotation[[:space:]]*=[[:space:]]*//p' "$RUN/game.ini" | tr -d '\r' | tail -n 1)
    ALL=$(printf '%s' "$ROT" | grep -oE '[0-9]+:[0-9]+' | wc -l)
    KEPT=$(printf '%s' "$ROT" | grep -oE '[0-9]+:[0-9]+' | awk -F: '$2 + 0 <= 12 { printf "%s%s", n++ ? ", " : "", $0 }')
    HERE=$(printf '%s' "$KEPT" | grep -oE '[0-9]+:[0-9]+' | wc -l)
    if [ "$ALL" -gt "$HERE" ]; then
        awk -v keep="$KEPT" '/^[[:space:]]*rotation[[:space:]]*=/ { if (keep != "") print "rotation = " keep; next } { print }' "$RUN/game.ini" > "$RUN/game.ini.new" \
            && mv -f "$RUN/game.ini.new" "$RUN/game.ini"
        if [ "$HERE" -gt 0 ]; then
            say "mgo1host: no PC maps on this server (maps 13 and up): $((ALL - HERE)) of the rotation's $ALL entries left out, the game gets $KEPT" W
        else
            say "mgo1host: no PC maps on this server (maps 13 and up): none of the rotation's $ALL entries can be played, the game keeps its account's last rotation" W
        fi
    fi
fi
cd "$RUN" || exit 1
say "mgo1host: bot $BOT, UDP port $UDP_PORT, disc $DISC_DIR, ${MAPS:+PC maps $MAPS_DIR, }version $(tr -dc 'A-Za-z0-9 ._-' < "$HOME_DIR/VERSION" 2>/dev/null)"
# the accountless PLAYABLE DOLLs (docs/PLAYABLE_DOLL.md 1): their lobby on 127.0.0.1 while this game runs
# (mgo1-dollgate: the dolls log in there as DOLL_<n>, user id 0x7F000000 + n, and find this game alone), and the
# game handling the frames that name those ids as DOLL_LIST says (server.env, v141 beta 3: host, the default, the dolls
# listed in TX-55's game as the host's own account; own, with their ids; off, kept off the lobby; MGO1_DOLL_IDS and
# MGO1_DOLL_LIST); DOLL_GATE=0 (server.env): neither
DOLL_IDS=""; GATE_PID=""
DL=${DOLL_LIST:-host}; case "$DL" in off|own|host) ;; *) DL=host ;; esac
if [ "${DOLL_GATE:-1}" != 0 ] && [ -f "$HOME_DIR/mgo1-dollgate" ]; then
    GB=${DOLL_GATE_PORT_BASE:-5760}; case "$GB" in ''|*[!0-9]*) GB=5760 ;; esac
    GP=$((GB + 2 * (BOT - 1)))
    python3 "$HOME_DIR/mgo1-dollgate" --bot "$BOT" --gate-port "$GP" --lobby-port $((GP + 1)) --host-port "$UDP_PORT" \
        --game-file "$GAME" --status "$RUN/status.json" --log-dir "$LOGDIR" --state-root "$(dirname "$STATE")" \
        ${DOLL_GATE_HOST_ADDR:+--host-addr "$DOLL_GATE_HOST_ADDR"} &
    GATE_PID=$!
    DOLL_IDS="MGO1_DOLL_IDS=0x7F000001-0x7F000040 MGO1_DOLL_LIST=$DL"
fi
# the bot's virtual dolls (docs/VIRTUAL_DOLL.md): VDOLL_COUNT (bot<N>.env, 0 to 15) players the game itself runs, no game
# and no account of their own; user ids 0x7F000041 + 16 x (bot - 1) up (the full dolls take them for dolls, the lobby filter
# keeps them off SaveMGO as DOLL_LIST says), their teams VDOLL_TEAMS (red, blue, auto: one a doll, the last for the rest),
# their names bot<N>.vdolls (a name a line, else VDOLL<n>), their level VDOLL_LEVEL (1 to 10, or one a doll: 3,5,7; else the
# game's 5); one seat always left for a real player (none only with DOLL_KEEP_SEAT=0 and DOLL_LIST=off)
VDOLLENV=""
VC=${VDOLL_COUNT:-0}; case "$VC" in ''|*[!0-9]*) VC=0 ;; esac; [ "$VC" -gt 15 ] && VC=15
VT=$(printf '%s' "${VDOLL_TEAMS:-auto}" | tr -cd 'a-z,'); VT=${VT:-auto}
VL=$(printf '%s' "${VDOLL_LEVEL:-}" | tr -d ' \t\r'); VRE='^([1-9]|10)(,([1-9]|10)){0,14}$'
if [ -n "$VL" ] && ! [[ "$VL" =~ $VRE ]]; then
    say "mgo1host: VDOLL_LEVEL must be 1 to 10, or one a doll (3,5,7) (bot$BOT.env): 5" W; VL=""
fi
# (v150-host beta 15) the count also live: run/vdolls.live, read by the game every 2 s (mgo1host-agent vdoll-count), so the
# virtual dolls' settings go to every game, none of them wanted (0) included
printf '%s\n' "$VC" > "$RUN/vdolls.live"
VW=$(printf '%s' "${VDOLL_WEAPONS:-auto}" | tr -cd 'a-z0-9,'); VW=${VW:-auto}   # (beta 15) auto, ak47, xm16e1, scorpion, m37, svd
VDOLLENV="MGO1_VDOLLS=$VC MGO1_VDOLL_LIVE=$(win "$RUN/vdolls.live") MGO1_VDOLL_IDS=$(printf '0x%08X' $((0x7F000041 + 16 * (BOT - 1)))) MGO1_VDOLL_TEAMS=$VT${VL:+ MGO1_VDOLL_LEVEL=$VL} MGO1_VDOLL_WEAPONS=$VW"
# (v150-host beta 18) VDOLL_CONTROLS (bot<N>.env): player, the virtual dolls turn, aim and fire within what the pad lets a
# player do (MGO1_VDOLL_CONTROLS=player); direct (the default), as before: nothing given. A carriage return or quotes from a
# file written on Windows taken off; another word: direct, said
VCTL=$(printf '%s' "${VDOLL_CONTROLS:-}" | tr -d " \t\r\"'" | tr A-Z a-z)
case "$VCTL" in
    player) VDOLLENV="$VDOLLENV MGO1_VDOLL_CONTROLS=player" ;;
    ""|direct) VCTL=direct ;;
    *) say "mgo1host: VDOLL_CONTROLS must be player or direct (bot$BOT.env): direct" W; VCTL=direct ;;
esac
# (v150-host beta 19) the virtual dolls' live level (docs/VIRTUAL_DOLL.md, docs/DEDICATED_HOST.md 22): VDOLL_ADAPT=1 (bot<N>.env),
# each virtual doll's level follows the strength of the game's real players, from its VDOLL_LEVEL (the anchor) between
# VDOLL_ADAPT_MIN and VDOLL_ADAPT_MAX (1 and 10, swapped when the wrong way round); 0, unset or another word (said): off, as
# before. None of the three set: nothing given, nothing added to the live file (the game as beta 18). A value set: the game
# gets MGO1_VDOLL_ADAPT=1 (on only), MGO1_VDOLL_ADAPT_MIN, MGO1_VDOLL_ADAPT_MAX (when set and valid), and the live file the
# three lines after its count (mgo1host-agent vdoll-set changes them in a running game; a game of beta 18 or older reads
# the count alone). A carriage return, spaces or quotes from a file written on Windows taken off
VA=$(printf '%s' "${VDOLL_ADAPT:-}" | tr -d " \t\r\"'")
VAMIN=$(printf '%s' "${VDOLL_ADAPT_MIN:-}" | tr -d " \t\r\"'"); VAMAX=$(printf '%s' "${VDOLL_ADAPT_MAX:-}" | tr -d " \t\r\"'")
VALIVE=""
if [ -n "$VA$VAMIN$VAMAX" ]; then
    case "$VA" in
        1) VDOLLENV="$VDOLLENV MGO1_VDOLL_ADAPT=1" ;;
        ""|0) VA=0 ;;
        *) say "mgo1host: VDOLL_ADAPT must be 1 or 0 (bot$BOT.env): 0" W; VA=0 ;;
    esac
    vbound() {   # <name> <value> <default>: VB 1 to 10 (leading zeros read as decimal), else said and empty
        VB=""
        if [ -z "$2" ]; then return 0; fi
        if [[ "$2" =~ ^[0-9]{1,3}$ ]] && [ $((10#$2)) -ge 1 ] && [ $((10#$2)) -le 10 ]; then VB=$((10#$2)); return 0; fi
        say "mgo1host: $1 must be 1 to 10 (bot$BOT.env): $3" W
    }
    vbound VDOLL_ADAPT_MIN "$VAMIN" 1; VAMIN=$VB; vbound VDOLL_ADAPT_MAX "$VAMAX" 10; VAMAX=$VB
    if [ -n "$VAMIN" ] && [ -n "$VAMAX" ] && [ "$VAMIN" -gt "$VAMAX" ]; then VSW=$VAMIN; VAMIN=$VAMAX; VAMAX=$VSW; fi
    [ -n "$VAMIN" ] && VDOLLENV="$VDOLLENV MGO1_VDOLL_ADAPT_MIN=$VAMIN"
    [ -n "$VAMAX" ] && VDOLLENV="$VDOLLENV MGO1_VDOLL_ADAPT_MAX=$VAMAX"
    VAMIN=${VAMIN:-1}; VAMAX=${VAMAX:-10}
    VALIVE=$(printf 'adapt %s\nadapt_min %s\nadapt_max %s\n' "$VA" "$VAMIN" "$VAMAX")
    printf '%s\n%s\n' "$VC" "$VALIVE" > "$RUN/vdolls.live"
else
    VA=0; VAMIN=1; VAMAX=10
fi
VN=${VDOLL_NAMES_FILE:-/etc/mgo1host/bot$BOT.vdolls}
# (v150-host beta 16) the names the game is given are checked here too, as mgo1host-agent vdoll-set checks them (a file
# written by hand was copied as it was: the game cuts a name at 15 bytes, in the middle of a letter of two bytes, and
# takes a name of 1 letter or twice the same). The game's reading kept: a line's carriage return and its last spaces
# go, an empty line is no doll's; the k-th name left is doll k's, 15 at most. One the game should not show (not 3 to 15
# of A-Z a-z 0-9 space . _ - ! ? + *, a space first or two together, DOLL_<n>, a name already taken): VDOLL<k>, said
vdoll_names() {   # <file> <copy>: the copy written, the dolls whose name was refused in BADN
    local LC_ALL=C line k=0 seen="|" re='^[A-Za-z0-9._!?+*-][A-Za-z0-9._!?+* -]{1,13}[A-Za-z0-9._!?+*-]$'
    BADN=""; : > "$2" || return 1
    while IFS= read -r line || [ -n "$line" ]; do
        line=${line//$'\r'/}; line=${line%"${line##*[! ]}"}
        [ -n "$line" ] || continue
        k=$((k + 1)); [ "$k" -le 15 ] || break
        if ! [[ "$line" =~ $re ]] || [[ "$line" == *"  "* ]] || [[ "${line^^}" =~ ^DOLL_[0-9]+$ ]] || [[ "$seen" == *"|${line^^}|"* ]]; then
            BADN="$BADN${BADN:+, }$k"; line="VDOLL$k"
        fi
        seen="$seen${line^^}|"
        printf '%s\n' "$line" >> "$2"
    done < "$1"
}
if [ -f "$VN" ] && vdoll_names "$VN" "$RUN/vdolls.names"; then
    VDOLLENV="$VDOLLENV MGO1_VDOLL_NAMES=$(win "$RUN/vdolls.names")"
    [ -n "$BADN" ] && say "mgo1host: $(basename "$VN"): the name of virtual doll $BADN is not one the game shows (3 to 15 letters, digits, spaces, . _ - ! ? + *, not DOLL_<n>, no two the same): VDOLL<n> instead" W
fi
[ "${DOLL_KEEP_SEAT:-1}" = 0 ] && [ "$DL" = off ] && VDOLLENV="$VDOLLENV MGO1_VDOLL_KEEP_SEAT=0"
[ -z "$DOLL_IDS" ] && VDOLLENV="$VDOLLENV MGO1_DOLL_LIST=$DL"
say "mgo1host: $VC virtual doll(s) now (the count changes live), teams $VT, level ${VL:-5}, user ids $(printf '0x%08X' $((0x7F000041 + 16 * (BOT - 1)))) up, controls $VCTL, live level $([ "$VA" = 1 ] && echo "on between $VAMIN and $VAMAX" || echo off)"
# what this game was given (mgo1host-agent vdolls: settings saved since then are "pending" until the next game; (beta 19) the
# live level's three as the game has them now: the agent's vdoll-set writes them here too when it changes them live)
printf 'VDOLL_COUNT=%s\nVDOLL_TEAMS=%s\nVDOLL_LEVEL=%s\nVDOLL_WEAPONS=%s\nVDOLL_CONTROLS=%s\nVDOLL_ADAPT=%s\nVDOLL_ADAPT_MIN=%s\nVDOLL_ADAPT_MAX=%s\n' \
    "$VC" "$VT" "${VL:-5}" "$VW" "$VCTL" "$VA" "$VAMIN" "$VAMAX" > "$RUN/vdolls.env"
# (v141 beta 5) the game's maximum as the lobby server is told it (docs/DEDICATED_HOST.md 9): a Host Only game of 9 (8
# players) told as 9; HOST_LIST_9_AS_8=1 (server.env): as 8, for the MGO1.exe of v141 (the host list then shows 7 seats);
# LOBBY_BIG_GAMES=1: a lobby server that takes more than 9 is told the real maximum
LISTENV=""
[ "${HOST_LIST_9_AS_8:-0}" = 1 ] && LISTENV="MGO1_HOST_TELL_8=1"
[ "${LOBBY_BIG_GAMES:-0}" = 1 ] && LISTENV="$LISTENV MGO1_LOBBY_BIG_GAMES=1"
# (v141 beta 6) the server's dolls' names (the doll manager's list): the status tells the real players from the dolls
DNAMES="$(dirname "$STATE")/dolls.names"
[ -f "$DNAMES" ] && LISTENV="$LISTENV MGO1_DOLL_NAMES=$(win "$DNAMES")"
# (v141 beta 7) SaveMGO's rankings and the dolls (server.env HOST_UNRANKED): auto (the default) once a doll is in the game, its
# players' statistics are sent unranked to the game's end (a VS rating of 0: TX-55 keeps them for the game's own scores only);
# always, every game; off, none
UNRANKED=${HOST_UNRANKED:-auto}
case "$UNRANKED" in auto|always|off) ;; *) say "mgo1host: HOST_UNRANKED must be auto, always or off (server.env): auto" W; UNRANKED=auto ;; esac
LISTENV="$LISTENV MGO1_HOST_UNRANKED=$UNRANKED"
# (v150-host beta 18) JOINER_HOLD_S (server.env for every bot, a bot's bot<N>.env over it: systemd reads that one last): the
# seconds the game keeps a joiner still loading its stage past its silence rules (MGO1_JOINER_HOLD_S, docs/DEDICATED_HOST.md
# 21), 0 to 300, 0 the game's rules as they are; unset or empty: nothing given, the game's 60; another value: said, nothing
# given. A carriage return, spaces or quotes from a file written on Windows taken off
JH=$(printf '%s' "${JOINER_HOLD_S:-}" | tr -d " \t\r\"'")
if [ -n "$JH" ]; then
    if [[ "$JH" =~ ^[0-9]{1,3}$ ]] && [ $((10#$JH)) -le 300 ]; then
        LISTENV="$LISTENV MGO1_JOINER_HOLD_S=$((10#$JH))"
        say "mgo1host: a joiner still loading kept until $((10#$JH)) s of silence (JOINER_HOLD_S$([ $((10#$JH)) = 0 ] && echo ': 0, the game'"'"'s own rules'))"
    else
        say "mgo1host: JOINER_HOLD_S must be 0 to 300 (seconds) (server.env or bot$BOT.env): the game's 60" W
    fi
fi
# no log-in data in the log, ever (logs are sent to others to troubleshoot): every line the game
# writes goes through this filter, which masks the account, its password and the game's password
# (the game masks them in its Game Report itself)
GAME_PW=$(sed -n 's/^[[:space:]]*password[[:space:]]*=[[:space:]]*//p' "$RUN/game.ini" 2>/dev/null | tr -d '\r' | tail -n 1 | sed 's/[[:space:]]*$//')
MASK='
function mask(line, s, word,    u, out, i, a, b, n) {
    if (s == "") return line
    n = length(s); u = toupper(line); out = ""
    while ((i = index(u, s)) > 0) {
        a = (i > 1) ? substr(u, i - 1, 1) : ""
        b = substr(u, i + n, 1)
        if (word && (a ~ /[A-Z0-9_]/ || b ~ /[A-Z0-9_]/)) out = out substr(line, 1, i + n - 1)
        else out = out substr(line, 1, i - 1) "***"
        line = substr(line, i + n); u = substr(u, i + n)
    }
    return out line
}
BEGIN { id = toupper(ENVIRON["MASK_ID"]); pw = toupper(ENVIRON["MASK_PW"]); gp = toupper(ENVIRON["MASK_GPW"]) }
{ print mask(mask(mask($0, pw, 0), id, 1), gp, 1); fflush() }'
set -o pipefail
# mawk (Debian's and Ubuntu's awk) reads a pipe by blocks: -W interactive passes each line at once
AWK=(awk)
awk -W version 2>/dev/null | grep -qi mawk && AWK=(awk -W interactive)
# the masked lines: the journal and the daily log
# (v150-host beta 16) and a crash report when the game ends badly (docs/CRASH_REPORTS.md): mgo1log keeps the game's last
# lines in memory and learns its end from run/exit, the exit code written below before the pipe closes (0 a restart asked
# and 3 no game in time: no report; 2 its game lost and 6 the lobby lost: "lost"; 70: "crash70"; a signal: "signal<N>");
# stopped by its service: no exit code, no report, unless the doll manager asked for one (a hung game it restarts)
LOGCMD=(cat)
[ -x "$MGO1LOG" ] && LOGCMD=(env MGO1_CRASH_EXIT="$RUN/exit" MGO1_CRASH_ENDS="2=lost 6=lost 3=" MGO1_CRASH_STATUS="$RUN/status.json" MGO1_CRASH_VERSION="$HOME_DIR/VERSION" "$MGO1LOG" --dir "$LOGDIR")
# stopped by its service: the game and the pipe end, then this script says so
STOPPED=0
trap 'STOPPED=1' TERM INT
# the game: no picture, no sound; a frame count of a year; commands through cmd.txt (mgo1hostctl);
# the P2P datagrams not logged once it hosts
{ ${PIN[@]+"${PIN[@]}"} env MGO1_HEADLESS=1 MGO1_HOST_ONLY=1 MGO1_QUIET_P2P=1 SIM_NO_IOP=1 SIM_NATIVE=1 SIM_TEXTS=1 \
    SIM_STEPS_FILE="$(win "$RUN/steps.txt")" SIM_STEPS_DELETE=1 MGO1_CMD_FILE="$(win "$RUN/cmd.txt")" \
    MGO1_STATUS_FILE="$(win "$RUN/status.json")" MGO1_BANS_FILE="$(win "$BANS")" MGO1_WHITELIST_FILE="$(win "$WHITE")" \
    MGO1_EVENTS_DIR="$(win "$EVENTS")" SIM_UDP_PORT="$UDP_PORT" SIM_PORT_SHIFT="$SHIFT" \
    MGO1_HOST_WATCH="${HOST_WATCH_MINUTES:-10}" MGO1_HOST_LOBBY_GRACE="${HOST_LOBBY_GRACE:-10}" $P2P $SET $DOLL_IDS $VDOLLENV $MAPS $LISTENV ${EXTRA_ENV:-} \
    "$WINE" "$EXE" . 2000000000 "" ""; R=$?; echo "$R" > "$RUN/exit"; exit "$R"; } 2>&1 | MASK_ID="$HOST_ACCOUNT_ID" MASK_PW="$HOST_ACCOUNT_PASSWORD" MASK_GPW="$GAME_PW" "${AWK[@]}" "$MASK" | "${LOGCMD[@]}"
RC=${PIPESTATUS[0]}
[ -n "$GATE_PID" ] && kill "$GATE_PID" 2>/dev/null     # (the dolls' lobby ends with this game)
# how the game ended, in words (the service starts it again but when it was stopped)
if [ "$STOPPED" = 1 ]; then lasted; say "mgo1host: stopped (its service was stopped)"; exit 0; fi
case "$RC" in
    0) say "mgo1host: the game ended (a restart was asked); a new game" ;;
    2) say "mgo1host: the game ended: its game was lost (no game for 30 s); a new game" W ;;
    3) say "mgo1host: the game ended: no game of its own in time (the log-in or the lobby); it starts again" W ;;
    6) say "mgo1host: the game ended: the lobby connection was lost (closed and not opened again in time); a new game" W ;;
    70) say "mgo1host: the game crashed (its CRASH line tells where); it starts again" E ;;
    *) if [ "$RC" -gt 128 ]; then
           S=$((RC - 128)); W=""; [ "$S" = 9 ] && W=" (killed: out of memory, or forced)"
           say "mgo1host: the game was ended by signal $S$W; it starts again" E
       else say "mgo1host: the game ended with code $RC; it starts again" E; fi ;;
esac
# (v150-host beta 16) an end that was not asked in the game's first 10 minutes: the next start waits (early_end above)
if [ "$RC" != 0 ] && [ "$SECONDS" -lt "${MGO1_QUICK_SECONDS:-600}" ]; then early_end ""; else lasted; fi
exit "$RC"
