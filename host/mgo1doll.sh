#!/bin/bash
# One PLAYABLE DOLL (docs/PLAYABLE_DOLL.md): MGO1 recompiled (the dedicated host's mgo1host.exe),
# headless, as a player of one of this server's bots. Started by systemd (mgo1doll@<doll>.service)
# with /etc/mgo1host/server.env and /etc/mgo1host/doll<doll>.env, and by mgo1doll-manager, which
# starts a doll when its bot's game has a seat for it and stops it when a real player needs the seat.
# The doll logs in with its own SaveMGO account, or with none (accountless: DOLL_<N> in its bot's own lobby,
# host/mgo1-dollgate), joins its bot's game by name, and plays; its state is in
# /var/lib/mgo1host/doll<N>/status.json, its orders in /var/lib/mgo1host/doll<N>/command.
set -u
DOLL=${DOLL:-1}
case "$DOLL" in [1-9]|[1-5][0-9]|6[0-4]) ;; *) echo "mgo1doll: DOLL must be 1 to 64"; exit 1 ;; esac
HOME_DIR=${HOME_DIR:-/opt/mgo1host}
STATE=${STATE_DIR:-/var/lib/mgo1host/doll$DOLL}
# the doll's daily log (docs/LOGS.md): /var/log/mgo1host/doll<N>/<YYYY-MM-DD>.log, what it did (its
# activities, kills and deaths, orders), its problems, its starts and ends; kept 31 days
LOGDIR=${LOG_DIR:-/var/log/mgo1host}/doll$DOLL
MGO1LOG=${MGO1LOG:-/usr/local/bin/mgo1log}
say() {   # a line of this script's: the journal and the daily log (level I, W or E)
    if [ -x "$MGO1LOG" ]; then "$MGO1LOG" --dir "$LOGDIR" --note "$1" --level "${2:-I}" || echo "$1"; else echo "$1"; fi
}
RUN=$STATE/run
EXE=$HOME_DIR/mgo1host.exe
# (v150-host beta 16) a settings file written by hand on Windows (lines ending in a carriage return, a value in
# quotes): the carriage returns and the quotes about a value go, as mgo1doll-manager takes them off when it reads the
# same file. systemd does so for most forms; a value it left as it was (or a launcher started another way) made
# DOLL_NAME="A B" fail the name's check below while the manager listed the name: the game and dolls.names disagreed
for v in DOLL_ACCOUNT_ID DOLL_ACCOUNT_PASSWORD DOLL_BOT DOLL_NAME DOLL_DIFFICULTY DOLL_ADAPT DOLL_ADAPT_MIN DOLL_ADAPT_MAX \
         DOLL_TEAM DOLL_LOADOUT DOLL_ORDER DOLL_AIM DOLL_GATE DOLL_GATE_PORT_BASE; do
    x=${!v-}; [ -n "$x" ] || continue
    x=${x//$'\r'/}
    case "$x" in \"*\") x=${x#\"}; x=${x%\"} ;; \'*\') x=${x#\'}; x=${x%\'} ;; esac
    printf -v "$v" '%s' "$x"
done
# no account: an accountless doll (docs/PLAYABLE_DOLL.md 1): DOLL_<N>, user id 0x7F000000 + N, in its bot's own
# lobby on 127.0.0.1 (mgo1-dollgate, started with the bot); DOLL_GATE=0 in server.env: an account is needed
ACCOUNTLESS=0
if [ -z "${DOLL_ACCOUNT_ID:-}" ]; then
    [ "${DOLL_GATE:-1}" != 0 ] || { say "mgo1doll: DOLL_ACCOUNT_ID is not set (/etc/mgo1host/doll$DOLL.env) and DOLL_GATE=0" E; exit 1; }
    ACCOUNTLESS=1; DOLL_ACCOUNT_ID="DOLL_$DOLL"; DOLL_ACCOUNT_PASSWORD="ACCOUNTLESS"
fi
[ -n "${DOLL_ACCOUNT_PASSWORD:-}" ] || { say "mgo1doll: DOLL_ACCOUNT_PASSWORD is not set (/etc/mgo1host/doll$DOLL.env)" E; exit 1; }
DOLL_BOT=${DOLL_BOT:-1}
case "$DOLL_BOT" in [1-9]|10) ;; *) say "mgo1doll: DOLL_BOT must be 1 to 10 (/etc/mgo1host/doll$DOLL.env)" E; exit 1 ;; esac
for v in "$DOLL_ACCOUNT_ID" "$DOLL_ACCOUNT_PASSWORD"; do
    case "$v" in *[!A-Za-z0-9._-]*)
        say "mgo1doll: DOLL_ACCOUNT_ID / DOLL_ACCOUNT_PASSWORD may only hold letters, digits, - _ . (/etc/mgo1host/doll$DOLL.env)" E
        sleep 60; exit 1 ;;
    esac
done
DOLL_ACCOUNT_ID=$(printf '%s' "$DOLL_ACCOUNT_ID" | tr a-z A-Z)
DOLL_ACCOUNT_PASSWORD=$(printf '%s' "$DOLL_ACCOUNT_PASSWORD" | tr a-z A-Z)
# the doll's own UDP port and the game's other UDP ports moved past the bots' (5730 to 5739, shifts 0 to 900)
UDP_PORT=${UDP_PORT:-$((5739 + DOLL))}
SHIFT=$((100 * (9 + DOLL)))
DISC_DIR=${DISC_DIR:-$HOME_DIR/disc}
for f in MGS/NET/MGS3_N.ELF MGS/NET/STAGE_N.DAT MGS/NET/SLOT_N.DAT; do
    [ -f "$DISC_DIR/$f" ] || { say "mgo1doll: $DISC_DIR/$f is missing (the disc's files)" E; exit 1; }
done
mkdir -p "$STATE"
# (v141 beta 5) an accountless doll's name in the game: its DOLL_NAME (MGO1 Host.exe's RENAME), else DOLL_<N>; written into
# its folder for its bot's doll lobby (mgo1-dollgate answers it at the log-in)
SHOWN="DOLL_$DOLL"
if [ "$ACCOUNTLESS" = 1 ] && [ -n "${DOLL_NAME:-}" ]; then
    if printf '%s' "$DOLL_NAME" | grep -Eq '^[A-Za-z0-9._!?+*-][A-Za-z0-9._!?+* -]{1,13}[A-Za-z0-9._!?+*-]$' && ! printf '%s' "$DOLL_NAME" | grep -q '  '; then
        SHOWN=$DOLL_NAME
    else say "mgo1doll: DOLL_NAME is not a name the game shows (3 to 15 letters, digits, spaces, . _ - ! ? + *): DOLL_$DOLL" W; fi
fi
if [ "$ACCOUNTLESS" = 1 ]; then printf '%s\n' "$SHOWN" > "$STATE/display_name"; else rm -f "$STATE/display_name"; fi
# (v141 beta 4) back after a match: the bot's dolls all ended at the same moment and systemd starts them again together,
# each start about a core for some seconds while the bot's next match runs: 4 s apart by their numbers
if [ -f "$STATE/.after-match" ]; then
    rm -f "$STATE/.after-match"
    sleep $(( (DOLL - 1) % 8 * 4 ))
fi
# (v141) the PC maps (maps 13 and up), as its host has them: MAPS_DIR, else the install's maps folder
MAPS_DIR=${MAPS_DIR:-$HOME_DIR/maps}; MAPS=""

# its bot's game: the name to join, its password if it has one
GAME=${GAME_SETTINGS:-/etc/mgo1host/bot$DOLL_BOT.game}
# (v150-host beta 22, sync) the running game's own settings first (its launcher's copy, run/game.ini): a save in MGO1 Host without
# a restart changes the bot's file, not the game that runs (its name and its password lock as the lobby has them)
RUNGAME="$(dirname "$STATE")/bot$DOLL_BOT/run/game.ini"
[ -z "${GAME_SETTINGS:-}" ] && [ -r "$RUNGAME" ] && GAME=$RUNGAME
val() { sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$GAME" 2>/dev/null | tr -d '\r' | tail -n 1 | sed 's/[[:space:]]*$//'; }
GAME_NAME=$(val name); GAME_PW=$(val password)
# (no name in the bot's settings: the name of the game it made, from its status file)
[ -n "$GAME_NAME" ] || GAME_NAME=$(python3 -c 'import json, sys; print((json.load(open(sys.argv[1])).get("game") or {}).get("name") or "")' \
    "$(dirname "$STATE")/bot$DOLL_BOT/run/status.json" 2>/dev/null | tr -d '\r\n')
[ -n "$GAME_NAME" ] || { say "mgo1doll: bot $DOLL_BOT has no game name ($GAME)" E; sleep 60; exit 1; }

# (v141 beta 6) the difficulty (1 TRAINING .. 10 PRO) and the live level's bounds (DOLL_ADAPT_MIN .. DOLL_ADAPT_MAX, the
# lower first): 1 to 10, else their defaults (3, 1 and 10), said
TIERS=(TRAINING EASY NORMAL HARD EXPERT ELITE VETERAN MASTER EXTREME PRO)
DIFF=${DOLL_DIFFICULTY:-3}; AMIN=${DOLL_ADAPT_MIN:-1}; AMAX=${DOLL_ADAPT_MAX:-10}
# (v150-host beta 16) DOLL_DIFFICULTY=vdoll: its bot's virtual dolls' level (bot<B>.env's VDOLL_LEVEL: the k-th of the bot's
# dolls set to play takes the k-th level of the list, the last for the rest; none set: 5, the virtual dolls' own), the live
# level off whatever DOLL_ADAPT says (a virtual doll's level does not move). mgo1doll-manager reads the bot's settings and
# the other dolls' (this script's user reads neither) and writes each such doll's level into /var/lib/mgo1host/dolls.levels
# (`<doll> <level> <bot> <the bot's VDOLL_LEVEL>`) before it starts one
FOLLOW=""
if [ "$DIFF" = vdoll ]; then
    LEVELS=${DOLL_LEVELS_FILE:-$(dirname "$STATE")/dolls.levels}
    L=$(awk -v d="$DOLL" '$1 == d { print $2 " " $4; exit }' "$LEVELS" 2>/dev/null)
    DIFF=${L%% *}
    case "$DIFF" in
        [1-9]|10) FOLLOW="as bot $DOLL_BOT's virtual dolls (VDOLL_LEVEL ${L#* })" ;;
        *) say "mgo1doll: DOLL_DIFFICULTY=vdoll: no level for doll $DOLL in $LEVELS (mgo1doll-manager writes it from bot $DOLL_BOT's VDOLL_LEVEL): 5" W
           DIFF=5; FOLLOW="as bot $DOLL_BOT's virtual dolls (their level not known: 5)" ;;
    esac
fi
case "$DIFF" in [1-9]|10) ;; *) say "mgo1doll: DOLL_DIFFICULTY must be 1 to 10 (/etc/mgo1host/doll$DOLL.env): 3" W; DIFF=3 ;; esac
case "$AMIN" in [1-9]|10) ;; *) say "mgo1doll: DOLL_ADAPT_MIN must be 1 to 10 (/etc/mgo1host/doll$DOLL.env): 1" W; AMIN=1 ;; esac
case "$AMAX" in [1-9]|10) ;; *) say "mgo1doll: DOLL_ADAPT_MAX must be 1 to 10 (/etc/mgo1host/doll$DOLL.env): 10" W; AMAX=10 ;; esac
[ "$AMIN" -le "$AMAX" ] || { T=$AMIN; AMIN=$AMAX; AMAX=$T; }
ADAPT=${DOLL_ADAPT:-1}; [ -n "$FOLLOW" ] && ADAPT=0
case "$ADAPT" in 0|off) LIVE=off ;; *) LIVE=on ;; esac
LEVEL_SAID="difficulty $DIFF (${TIERS[DIFF-1]}), live level $LIVE between $AMIN (${TIERS[AMIN-1]}) and $AMAX (${TIERS[AMAX-1]})"
[ -n "$FOLLOW" ] && LEVEL_SAID="difficulty $DIFF (${TIERS[DIFF-1]}), $FOLLOW, live level off"
# (v150-host beta 18) DOLL_AIM: direct, the doll turns its view at the speed of a virtual doll of its level, without the stick
# (MGO1_DOLL_AIM=direct and aim=direct in doll.ini); pad (the default), through the stick as before (nothing given, aim=pad);
# another word: pad, said. MGO1 Host's AIM and `aim direct|pad` orders change it while it plays (mgo1host-agent doll-order)
AIM=$(printf '%s' "${DOLL_AIM:-}" | tr A-Z a-z)
case "$AIM" in
    direct|pad) ;;
    "") AIM=pad ;;
    *) say "mgo1doll: DOLL_AIM must be direct or pad (/etc/mgo1host/doll$DOLL.env): pad" W; AIM=pad ;;
esac
AIM_ENV=(); [ "$AIM" = direct ] && AIM_ENV=("MGO1_DOLL_AIM=direct")

rm -rf "$RUN"; mkdir -p "$RUN"
ln -s "$DISC_DIR/MGS" "$RUN/MGS"; ln -s "$DISC_DIR/MODULES" "$RUN/MODULES"
umask 077
esc() { printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'; }
# the log-in with the doll's account (read then deleted by the game), then [Join Game]: the doll finds its
# game in the list itself (by its name) and types its password if it has one (an accountless doll's lobby
# lists its bot's game without its password lock: no password)
sed -e "s|@ID@|$(esc "$DOLL_ACCOUNT_ID")|" -e "s|@PASSWORD@|$(esc "$DOLL_ACCOUNT_PASSWORD")|" "$HOME_DIR/steps/login_account.txt" > "$RUN/steps.txt"
grep -v '^#' "$HOME_DIR/steps/doll_join.txt" >> "$RUN/steps.txt"
# what the doll joins and how it plays at the start: the game, the difficulty, the live level and its bounds, the team; the
# orders come later through `command`
{
    printf 'game=%s\n' "$GAME_NAME"
    [ -n "$GAME_PW" ] && [ "$ACCOUNTLESS" = 0 ] && printf 'game_password=%s\n' "$GAME_PW"
    printf 'difficulty=%s\nadapt=%s\nadapt_min=%s\nadapt_max=%s\nteam=%s\nbot=%s\ndoll=%s\n' "$DIFF" "$ADAPT" "$AMIN" "$AMAX" \
        "${DOLL_TEAM:-auto}" "$DOLL_BOT" "$DOLL"
    [ -n "${DOLL_LOADOUT:-}" ] && printf 'loadout=%s\n' "$DOLL_LOADOUT"
    [ -n "${DOLL_ORDER:-}" ] && printf 'order=%s\n' "$DOLL_ORDER"
    printf 'aim=%s\n' "$AIM"
} > "$RUN/doll.ini"
# the names of every doll of the server (the others are real players: a game of dolls only is not
# played), kept up to date by mgo1doll-manager; the doll reads it again every 10 s
NAMES=/var/lib/mgo1host/dolls.names
[ -f "$NAMES" ] || NAMES="$RUN/dolls.names"
# the other dolls' folders (their status.json: what each one does, for the radio: two dolls at most
# follow a caller, the free ones closest to him go)
PEERS_DIR=$(dirname "$STATE")
# the map knowledge the server's dolls learn and share (a text file a map page: the crossings and ledges
# not taken, the mesh's traps, the ways out of its pockets; no account data), kept 7 days
LEARN_DIR=${LEARN_DIR:-$PEERS_DIR/dolls.learn}
mkdir -p "$LEARN_DIR" 2>/dev/null || true

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
    PIN=(taskset -c $((NCPU - 1 - (DOLL - 1) % NCPU)))
fi
win() { echo "Z:$1" | tr / '\\'; }
[ -d "$WINEPREFIX" ] || ${PIN[@]+"${PIN[@]}"} "$WINE" wineboot -i >/dev/null 2>&1 || true
SERVERS=${SERVERS_FILE:-/etc/mgo1host/servers.ini}
WHO="its account"; ID_ENV=()
if [ "$ACCOUNTLESS" = 1 ]; then
    # the gate: its bot's doll lobby on 127.0.0.1 (the game's menu script takes it, docs/SERVERS.md); the web
    # and STUN servers stay the server's (the terms of use, the NAT check)
    GB=${DOLL_GATE_PORT_BASE:-5760}; case "$GB" in ''|*[!0-9]*) GB=5760 ;; esac
    GATE_PORT=$((GB + 2 * (DOLL_BOT - 1)))
    { echo "[servers]"
      [ -f "$SERVERS" ] && grep -i -E '^[[:space:]]*(case|web|stun|stun_port)[[:space:]]*=' "$SERVERS" | tr -d '\r'
      printf 'gate = 127.0.0.1\ngate_port = %s\n' "$GATE_PORT"; } > "$RUN/servers.ini"
    WHO="accountless: DOLL_$DOLL in bot $DOLL_BOT's doll lobby 127.0.0.1:$GATE_PORT"
    [ "$SHOWN" = "DOLL_$DOLL" ] || WHO="$WHO, named $SHOWN"
    # who it is (MGO1Loader/doll.cpp, its identity: the slot with its user id, else with its name)
    ID_ENV=("MGO1_DOLL_NAME=$SHOWN" "MGO1_DOLL_UID=$(printf '0x%08X' $((0x7F000000 + DOLL)))")
    DOLL_ACCOUNT_ID=""; DOLL_ACCOUNT_PASSWORD=""          # (nothing to mask: no account)
elif [ -f "$SERVERS" ]; then cp "$SERVERS" "$RUN/servers.ini"; fi
cd "$RUN" || exit 1
say "mgo1doll: doll $DOLL for bot $DOLL_BOT, UDP port $UDP_PORT, $LEVEL_SAID, team ${DOLL_TEAM:-auto}, aim $AIM, $WHO, version $(tr -dc 'A-Za-z0-9 ._-' < "$HOME_DIR/VERSION" 2>/dev/null)"
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
AWK=(awk)
awk -W version 2>/dev/null | grep -qi mawk && AWK=(awk -W interactive)
# (v150-host beta 16) a crash report when the doll's game ends badly (docs/CRASH_REPORTS.md): mgo1log learns its end from
# run/exit, the exit code written below before the pipe closes (0 a leave, 3 back in the lobby and 4 its game not listed: no
# report; 5 its match lost: "lost"; 70: "crash70"; a signal: "signal<N>")
LOGCMD=(cat)
[ -x "$MGO1LOG" ] && LOGCMD=(env MGO1_CRASH_EXIT="$RUN/exit" MGO1_CRASH_ENDS="3= 4= 5=lost" MGO1_CRASH_STATUS="$STATE/status.json" MGO1_CRASH_VERSION="$HOME_DIR/VERSION" "$MGO1LOG" --dir "$LOGDIR")
STOPPED=0
trap 'STOPPED=1' TERM INT
[ -d "$MAPS_DIR" ] && ls "$MAPS_DIR"/*/map.ini >/dev/null 2>&1 && MAPS="MGO1_MAPS_DIR=$(win "$MAPS_DIR")"
# the game as a player: no picture, no sound; the doll's folder (status, command, its settings)
{ ${PIN[@]+"${PIN[@]}"} env MGO1_HEADLESS=1 MGO1_QUIET_P2P=1 SIM_NO_IOP=1 SIM_NATIVE=1 SIM_TEXTS=1 \
    SIM_STEPS_FILE="$(win "$RUN/steps.txt")" SIM_STEPS_DELETE=1 \
    MGO1_DOLL="$(win "$STATE")" MGO1_DOLL_INI="$(win "$RUN/doll.ini")" MGO1_DOLL_NAMES="$(win "$NAMES")" \
    MGO1_DOLL_PEERS="$(win "$PEERS_DIR")\\doll%d" MGO1_DOLL_LEARN="$(win "$LEARN_DIR")" \
    SIM_UDP_PORT="$UDP_PORT" SIM_PORT_SHIFT="$SHIFT" ${ID_ENV[@]+"${ID_ENV[@]}"} ${AIM_ENV[@]+"${AIM_ENV[@]}"} $MAPS ${EXTRA_ENV:-} \
    "$WINE" "$EXE" . 2000000000 "" ""; R=$?; echo "$R" > "$RUN/exit"; exit "$R"; } 2>&1 | MASK_ID="$DOLL_ACCOUNT_ID" MASK_PW="$DOLL_ACCOUNT_PASSWORD" MASK_GPW="$GAME_PW" "${AWK[@]}" "$MASK" | "${LOGCMD[@]}"
RC=${PIPESTATUS[0]}
if [ "$STOPPED" = 1 ]; then say "mgo1doll: stopped (its service was stopped: the manager or the program)"; exit 0; fi
case "$RC" in 3|4|5) : > "$STATE/.after-match" 2>/dev/null ;; esac
case "$RC" in
    0) say "mgo1doll: left the game (ordered)" ;;
    3) say "mgo1doll: back in the lobby after a match; it starts again" ;;
    4) say "mgo1doll: its game is not in the list; it starts again" W ;;
    5) say "mgo1doll: its match was lost (nothing from the host for a minute); it starts again" W ;;
    70) say "mgo1doll: the game crashed (its CRASH line tells where); it starts again" E ;;
    *) if [ "$RC" -gt 128 ]; then
           S=$((RC - 128)); W=""; [ "$S" = 9 ] && W=" (killed: out of memory, or forced)"
           say "mgo1doll: the game was ended by signal $S$W" E
       else say "mgo1doll: the game ended with code $RC" E; fi ;;
esac
exit "$RC"
