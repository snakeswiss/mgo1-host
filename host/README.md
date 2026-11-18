# SaveMGO MGO1 dedicated host

MGO1 itself, the game's own code built from your disc (this source), run with no picture and no
sound as a **Host Only** host: it logs in to SaveMGO with its own account,
creates a game with the settings of a file and hosts it around the clock. The
joiners play against the game's own host code. A server runs up to 10 of them
(bots 1 to 10), each with its own SaveMGO account, UDP port and game. MGO1 Host.exe (hostpanel/) installs
and drives all of this over SSH; the commands below do the same by hand.

## What you need

- An x86-64 Linux server (Debian 12, Ubuntu 22.04 or 24.04) with a public IPv4:
  one bot takes about half of one core (39 % median, 51 % at most with 2
  joiners, at 60 fps) and 280 MB of memory; 2 cores run 3 bots. On Hetzner
  Cloud: a CX plan (x86), not CAX (ARM).
- One UDP port a bot, 5730 to 5739 (bot N: 5729 + N), open in the server's
  firewall and, if you use one, the Hetzner Cloud Firewall. Nothing to open
  for the dolls: a playable doll (UDP 5739 + N, 5740 to 5803) talks with its
  host only, on the same server, and the dolls' lobby listens on 127.0.0.1
  alone (TCP 5760 to 5779: numbers inside the dolls' UDP range, no clash,
  TCP against UDP; a firewall rule written by number for both protocols
  would open the dolls' UDP ports too, which nothing needs).
- A SaveMGO account for each bot, used by that bot only (MGO1 accounts are in
  capitals: capital letters, digits, . _ -).
- The files of your own MGO1 disc (the US online disc of MGS3 Subsistence):
  `MGS/NET/MGS3_N.ELF`, `MGS/NET/STAGE_N.DAT`, `MGS/NET/SLOT_N.DAT` and the
  folder `MODULES/`. They stay on your server: do not share them.
- Disk: with MGO1 Host's public package (no mgo1host.exe: the host is built on
  the server, below) about 2.7 GB free the first time (Wine 800 MB, the
  build's toolchain 530 MB, its tree 430 MB at the most, the disc's
  files 320 MB); 195 MB of the tree stay for the updates. The first build
  takes about 35 minutes on 2 cores (33 measured on a busy machine; less with more cores), an update about 8.

## Install

On your PC, the package folder next to your disc's files:

```
mgo1host/
  mgo1host.exe  mgo1host.sh  mgo1host-agent  mgo1hostctl  mgo1host@.service  install.sh
  server.env.example  bot.env.example  game.ini.example  README.md  VERSION
  steps/login_account.txt  steps/host_create.txt
  disc/MGS/NET/MGS3_N.ELF  disc/MGS/NET/STAGE_N.DAT  disc/MGS/NET/SLOT_N.DAT  disc/MODULES/...
```

Copy it to the server and install (as root):

```
scp -r mgo1host root@<server>:/root/
ssh root@<server>
cd /root/mgo1host && bash install.sh
nano /etc/mgo1host/bot1.env    # HOST_ACCOUNT_ID, HOST_ACCOUNT_PASSWORD (only readable by root)
nano /etc/mgo1host/bot1.game   # the game: name, password, rules and maps, times, switches
systemctl enable --now mgo1host@1
mgo1hostctl log                # "host watch: hosting" once the game is on the lobby's list
```

install.sh ends with `== installed build 1.00 <12 hex digits>` (the package's VERSION, the start of the host
executable's SHA-256); afterwards `cat /opt/mgo1host/VERSION` gives the same and `mgo1host-agent version`
`{"ok": true, "agent": "build 1.00", ...}`.

With MGO1 Host's public package (the GitHub release) the folder holds no `mgo1host.exe` but
`mgo1host-src.tar.gz`: install.sh then builds the host on the server from these sources and your
disc's files (the North American disc only: another version is
refused by name), the first time in about 35 minutes on 2 cores, and installs it as usual with
`VERSION` `build 1.00 <its SHA-256, 12 digits>`. The steps show as they go
(`== building the host: 3/4 compiling the game's code (279 files, 2 at a time)`, `building the
host: 45 % (12 min)`); the whole log is /var/log/mgo1host-build.log. The build's tree stays in
/opt/mgo1host/build: an update compiles only what changed (about 8 minutes), the same package
again builds nothing. Nothing of the game leaves the server.

Update (from a beta to build 1.00, or a later build): the new package folder, `bash install.sh` again (MGO1
Host's UPDATE THE HOST does the same). The settings, the accounts and the lists stay; each running bot restarts
with the new version (`== restarting bot <N> (the new version)`). MGO1 Host takes a server's program from
before build 1.00 (a beta's) for an older one and shows its banner until the update.

Another bot: `mgo1host-agent bot-add` (the next free UDP port, opened in ufw or
firewalld when there is one), then its account in /etc/mgo1host/bot2.env and
`systemctl enable --now mgo1host@2`. Each service starts with the server and
starts its host again whenever it ends (a new game after about a minute). An
older one-bot install (/etc/mgo1host.env, mgo1host.service) becomes bot 1.

## Files

| path | what it holds |
|---|---|
| /etc/mgo1host/server.env | every bot's: the disc's folder, P2P_LOG, HOST_LOBBY_GRACE, JOINER_HOLD_S, the dolls' (DOLL_KEEP_SEAT, DOLL_GATE, DOLL_LIST, DOLL_PACE_GUARD), the watchdog's HANG_FAST_SECONDS |
| /etc/mgo1host/bot<N>.env | bot N's SaveMGO account and UDP port (root only); its virtual dolls (VDOLL_COUNT, VDOLL_TEAMS, VDOLL_LEVEL, VDOLL_WEAPONS, VDOLL_CONTROLS, VDOLL_ADAPT, VDOLL_ADAPT_MIN, VDOLL_ADAPT_MAX); its own JOINER_HOLD_S over server.env's |
| /etc/mgo1host/bot<N>.vdolls | bot N's virtual dolls' names, a name a line; a name the game should not show becomes VDOLL<n>, with a warning in the bot's log |
| /etc/mgo1host/bot<N>.game | bot N's game settings |
| /etc/mgo1host/bot<N>.whitelist, blacklist | the players' lists (the blacklist: every bot's) |
| /etc/mgo1host/servers.ini | the game's servers when they are not SaveMGO's (the gate, STUN) |
| /var/lib/mgo1host/bot<N>/run | bot N's run folder: status.json, its memory card |
| /var/lib/mgo1host/bot<N>/events | bot N's Game Report: <date>.jsonl (the game), <date>.admin.jsonl (the owner's actions), 31 days |
| /etc/mgo1host/doll<N>.env | PLAYABLE DOLL N (1 to 64): its SaveMGO account (none: DOLL_<N>), bot, difficulty, live level and its bounds, team, weapons, order (root only) |
| /var/lib/mgo1host/doll<N> | doll N's folder: status.json (what it does), `command` (its orders), its run |
| /var/log/mgo1host/bot<N>, doll<N>, manager | the daily logs, `<YYYY-MM-DD>.log` (31 days), and `crash/<UTC time>_<reason>.txt`: a game that ended badly, its last status and lines (40 files, 31 days; docs/CRASH_REPORTS.md) |
| /var/lib/mgo1host/dolls.sums.json, dolls.levels | the doll manager's: what it counted for each bot's dolls (the seats, why some wait), the level of each doll that follows its bot's virtual dolls |

## Commands

On the server, or from your PC with `ssh root@<server> mgo1hostctl ...`;
`-b N` picks bot N (1 by default):

| command | what it does |
|---|---|
| `mgo1hostctl bots` | the bots: account, UDP port, state, game and players |
| `mgo1hostctl players` | the players in the game: P1 the host, P2.. with their names |
| `mgo1hostctl kick P3` or `kick <name>` | the game's own kick: the player leaves at once, the lobby is told |
| `mgo1hostctl blacklist add <name>` | kicked now and each time the name joins any bot |
| `mgo1hostctl whitelist add <name>`, `whitelist only on` | with only on, only these names may play |
| `mgo1hostctl restart` | ends this game; the service creates a new one |
| `mgo1hostctl settings` | shows the game's settings |
| `mgo1hostctl edit` | edits them, then restarts to create the game with them |
| `mgo1hostctl stop` / `start` | stops the bot / starts it again |
| `mgo1hostctl status` / `log` | the service's state / its log |
| `mgo1hostctl report` | today's Game Report (JSON lines) |
| `mgo1host-agent crashes` / `crash <name>` | the crash reports (a crash, a kill, a hung game restarted, a lost game): the list with what ended, the game and its players, and one report's lines (docs/CRASH_REPORTS.md) |

## PLAYABLE DOLLs: players run by the server

A doll is the game itself as a player of one of the bots' games, with its
own SaveMGO account or with none (`DOLL_<N>`, logged in to the bot's own doll lobby,
`mgo1-dollgate` on 127.0.0.1, started by mgo1host.sh; `DOLL_GATE=0` in server.env turns that off): it
joins, takes a team, plays the rule, obeys orders. `mgo1doll-manager` (a service) starts the dolls while
their bot's game has seats for them (one kept for a real player: `DOLL_KEEP_SEAT=1` in server.env) and
as many as the server's cores and memory allow (fewer while a bot's game runs under its pace: the pace
guard, `DOLL_PACE_GUARD=0` in server.env turns it off), and keeps each bot's `DOLL_COUNT` (bot<B>.env) dolls
with no account; each doll is `mgo1doll@<N>`. MGO1 Host.exe's PLAYABLE DOLL page does everything; by
hand:

| command | what it does |
|---|---|
| `mgo1host-agent dolls` | the dolls: bot, settings, what each one does (accounts masked); each bot's `waiting_code` and `waiting_why` when its dolls do not start, and the sums behind them |
| `echo '{"id":"...","password":"...","bot":1}' \| mgo1host-agent doll-set 3` | doll 3's account and settings |
| `mgo1host-agent doll-order 3 difficulty 7` | also `difficulty vdoll`, `adapt off`, `adapt_min 4`, `adapt_max 8`, `team red`, `order hide`, `order target <name>`, `aim direct` / `aim pad`, `leave` |
| `mgo1host-agent doll-all 1 difficulty 7` | every doll of bot 1's game at once (also `adapt on` / `off`, `difficulty vdoll`, `aim direct` / `aim pad`) |
| `mgo1host-agent doll-remove 3` | doll 3 stopped and removed |
| `mgo1host-agent doll-count 1 3` | bot 1 with 3 dolls with no account (DOLL_<N>), at once |
| `journalctl -u mgo1doll@3` | doll 3's log (no log-in data in it) |

A doll's difficulty (`DOLL_DIFFICULTY`) is 1 to 10: 1 TRAINING, 2 EASY, 3 NORMAL,
4 HARD, 5 EXPERT, 6 ELITE (the former NIGHTMARE), 7 VETERAN, 8 MASTER, 9 EXTREME, 10 PRO. With the
live level on (`DOLL_ADAPT=1`, the default) the doll starts from it and moves over the ladder after
the strength of its enemies and of its own side, never under `DOLL_ADAPT_MIN` or over
`DOLL_ADAPT_MAX` (1 and 10 by default; the wrong way round, they are swapped); `DOLL_ADAPT=0`: the
difficulty alone. The three change live (`doll-order`, or doll-set's `"difficulty"`, `"adapt_min"`,
`"adapt_max"`); `mgo1host-agent dolls` gives each doll's `difficulty`, its `tier` name, `adapt_min`
and `adapt_max`, and the doll's start line in its log says them.

`DOLL_DIFFICULTY=vdoll`: the doll plays at the level of its bot's virtual dolls and
keeps it. The k-th of the bot's dolls set to play takes the k-th level of the bot's `VDOLL_LEVEL`
(`10,9,8`: the first at 10, the second at 9, the others at 8; none set: 5) and its live level is off
whatever `DOLL_ADAPT` says; its start line: `difficulty 10 (PRO), as bot 2's virtual dolls (VDOLL_LEVEL
10,9,8), live level off`. A doll added for a bot that has virtual dolls set (`doll-count`, `DOLL_COUNT`)
is made so; for the dolls there: `mgo1host-agent doll-all 2 difficulty vdoll` (one: `doll-order 3
difficulty vdoll`; back to a level of its own: `doll-order 3 difficulty 7`). `dolls` gives the number
with `"difficulty_from": "vdoll"`, and `"vdoll_difficulty": true` at its top (a server that takes that
difficulty); a new `VDOLL_LEVEL` (`vdoll-set`) reaches the running dolls at once.

A doll's aim (`DOLL_AIM`): `pad` (the default, as before) turns its view through the stick as a
player does; `direct` turns it at the speed of a virtual doll of its level, without the stick (the game is given `MGO1_DOLL_AIM=direct`). It changes
live (`doll-order 3 aim direct`, `doll-all 1 aim pad`, doll-set's `"aim"`) and is kept in doll<N>.env for the doll's
next starts (the manager's restarts included); `dolls` gives each doll's `"aim"` and `"doll_aim": true` at its top (a
server that takes it: MGO1 Host shows AIM only then). Another word in the file: pad, said in the doll's log.

When a bot's dolls do not start, the manager says why, once a reason, in its
daily log and the bot's (`mgo1host-agent logs get manager <day>`), and `mgo1host-agent
dolls` gives it for each bot (`"bots": {"2": {"waiting_code": "seats", "waiting_why": "bot 2: 6
playable dolls set, only 4 can start: its game has 8 seats (max_players 9), 3 for its virtual dolls, 1
kept for a real player ...", "seats": 8, "real": 0, "vdolls": 3, "kept": 1, "free_for_dolls": 4,
"dolls_set": 6, "dolls_in": 4, "pace_cap": null, "pace_wait_s": 0}}`): `seats` (its game's seats less
its virtual dolls', the kept one and the real players), `bot_stopped` (the dolls' bot is stopped, or
was removed), `not_hosting` (its game is being created), `server_full` (the server's cores and
memory), `pace_guard` (its cap, and for how long), `gate_off` (accountless dolls with `DOLL_GATE=0`).
A seat is always kept for a real player unless `DOLL_KEEP_SEAT=0` and `DOLL_LIST=off`.

The agent's commands that write a setting take their turn (a lock,
/etc/mgo1host/.agent.lock): two at once (MGO1 Host and a shell) lose nothing; one that waited 150 s
answers the code `busy`. An order to a bot or a doll whose `command` file cannot be written safely
(a symbolic link put there) answers `unsafe_file`. `settings-set` with a `max_players` that leaves
fewer seats than the bot's `VDOLL_COUNT` lowers it with it (`"vdoll_count_lowered": n`).

## Virtual dolls: players a bot's game runs itself

A virtual doll is a player the bot's own game speaks for: no
game, no account, no service of its own. `VDOLL_COUNT=<0-15>`, `VDOLL_TEAMS=auto` (or `red,blue,auto`) and
`VDOLL_LEVEL=5` (1 to 10, or `3,5,7`: one a doll, the last for the rest) and `VDOLL_WEAPONS=auto` (`auto`,
`ak47`, `xm16e1`, `scorpion`, `m37`, `svd`, one a doll the same way) in bot<N>.env, their names in bot<N>.vdolls; the
bot's next game takes them, its running game the count (`vdoll-count`). They play while a real player does, keep one seat free for a real
player and leave at once when one takes it. MGO1 Host.exe's VIRTUAL DOLLS page sets them.

`VDOLL_CONTROLS=direct` (the default, as before) or `player`: with `player` the bot's virtual dolls
turn, aim and fire within what the pad lets a player do (the game is given `MGO1_VDOLL_CONTROLS=player`); with a
PLAYABLE DOLL's `DOLL_AIM=direct` too the two kinds were of about equal strength at level 5 in 10-minute duels. vdoll-set's `"controls"` writes it; as the level, the bot's next game takes it
(`--restart`: now); `vdolls` gives each bot's `"controls"` (and in `"applied"` for a game started with it) and
`"vdoll_controls": true` at its top (MGO1 Host shows CONTROLS only then). Another word in the file: direct, said in the
bot's log.

Their live level: `VDOLL_ADAPT=1` makes each virtual doll's level follow
the strength of the game's real players, between `VDOLL_ADAPT_MIN` and `VDOLL_ADAPT_MAX` (1 to 10; 1 and 10 by default),
from its `VDOLL_LEVEL` (where it starts); `0` or unset: off, the level set (the default, as before). None of the three
set: nothing is given to the game. vdoll-set's `"adapt"`, `"adapt_min"`, `"adapt_max"` write them and tell the running
game at once (the lines `adapt 1`, `adapt_min 3`, `adapt_max 8` after the count in `run/vdolls.live`, read every 2 s; an
older game reads the count alone); `vdolls` gives each bot's three, `"levels_now"` (the level now of each
virtual doll in the game, from the game's last `vdoll: levels now` line of the day's log; `"levels_now_set"` their levels
set) and `"vdoll_adapt": true` at its top. A
PLAYABLE DOLL at the virtual dolls' level (`DOLL_DIFFICULTY=vdoll`) follows the level SET, not the level now.

| command | what |
|---|---|
| `mgo1host-agent vdolls` | each bot's virtual dolls: count, teams, level, names, the most its game takes, those in it now, what its running game was given and whether the saved ones are still for its next game (`pending`) |
| `echo '{"count":3,"teams":"auto","level":"3,5,7","weapons":"auto,svd","names":["ALPHA","BRAVO"]}' \| mgo1host-agent vdoll-set 1 --restart` | bot 1's virtual dolls, its game started again with them |
| `echo '{"controls":"player"}' \| mgo1host-agent vdoll-set 1` | bot 1's virtual dolls as a player from its next game (`"direct"`: as before) |
| `echo '{"adapt":true,"adapt_min":2,"adapt_max":8}' \| mgo1host-agent vdoll-set 1` | bot 1's virtual dolls' live level on, between 2 and 8: its running game within 2 s, its next games too (`"adapt":false`: off) |
| `mgo1host-agent vdoll-count 1 5` | bot 1's virtual dolls wanted now: its running game takes the count within seconds, its next games keep it |

## The game's settings (/etc/mgo1host/bot<N>.game)

`key = value` lines, see game.ini.example: name (3 to 15 characters),
comment, password lock, the rotation as `rule:map` pairs (rules 0 DM, 1 TDM,
2 rescue, 3 capture, 4 sneaking; maps 1 Lost Forest, 2 Ghost Factory, 3 City
Under Siege, 4-6 Killhouse A-C, 7 Svyatogornyj East, 8 Mountaintop, 9 Graniny
Gorki Lab, 10 Pillbox Purgatory, 11 High Ice, 12 Brown Town), teams,
max_players, weapons, minutes and rounds per rule, the switches (on / off).
A key left out keeps the account's last settings. Host Only is always set.
Changes apply to the next game: `mgo1hostctl restart`.

This file is the one copy of a bot's settings. Each game's launcher keeps
the copy it started with in /var/lib/mgo1host/bot<N>/run/game.src: `diff` the two to see what a save
changed that the running game does not play yet (`mgo1host-agent --bot N settings-get` says it as
"pending", with "takes_effect" on a save). A rotation entry on a map the server does not
have (no map.ini naming it in /opt/mgo1host/maps, or MAPS_DIR) is refused by the agent
(`rotation_map_missing`); written by hand it is left out of the game's copy, and a rotation with no
entry left gives the game none: it then keeps its account's last one from SaveMGO's lobby.

`HOST_LIST_9_AS_8` (server.env, 0 by default) and `mgo1host-agent host-list-9`
are for developer compatibility only: keep 0 (a Host Only game of 9 told to the
lobby as 9).

## The game's servers

SaveMGO's by default (the log-in gate 107.161.80.126:5731, STUN
15.204.66.207:3478). /etc/mgo1host/servers.ini (written by MGO1 Host, NETWORK)
points every bot at another service: `web`, `gate`, `gate_port`, `stun`,
`stun_port` under `[servers]`, then restart the bots. The players must use the
same servers to see the games.

## The bot's status

The game writes /var/lib/mgo1host/bot<N>/run/status.json every second
(`mgo1host-agent status` gives it as `host`): its `state` (starting, hosting,
lost), the game, the players, the lists. Each player has a
`kind` (`host`, `doll` or `real`) and `doll` (an accountless doll's number,
else 0); `players_real`, `players_dolls`, `players_host` and
`players_total_network` count them (`mgo1host-agent bots`: each bot's `real`
and `dolls`). The connection to the lobby:

| field | what it holds |
|---|---|
| `lobby` | 1: the connection to the lobby is open |
| `lobby_rx_age` | seconds since the lobby's last data, -1 none yet (`lobby_rx`: the same, kept) |
| `lobby_closed` | 1: the connection is closed (by the lobby, or given up by the game) |
| `lobby_fd` | the connection's descriptor, -1 none |
| `lobby_lost_for` | seconds since the host watch saw the lobby closed, -1 while it is not |

`mgo1host-agent status` adds `lobby_restart_in`: the seconds left before the
game ends with code 6 (HOST_LOBBY_GRACE less `lobby_lost_for`), null while the
lobby is not lost; its `reason` is then `lobby connection closed`.

## How a bot's game ends

mgo1host.sh ends with the game's code and says it in words in the bot's log;
the service starts a new game 5 s later (unless it was stopped):

| code | what happened |
|---|---|
| 0 | a restart was asked (`mgo1hostctl restart`, MGO1 Host, the daily restart) |
| 2 | its game was lost (no game of its own for 30 s) |
| 3 | no game of its own in time (the log-in or the lobby failed for 10 minutes) |
| 6 | the lobby connection was lost: closed, and not opened again within HOST_LOBBY_GRACE seconds (server.env, 10 by default, 5 to 60) |
| 70 | the game crashed (its CRASH line tells where) |

Another code is said as it is; above 128, the signal that ended the game (9:
out of memory, or forced). Before the game starts, a missing setting (the
account, a disc file) ends it with 1.

Starts that end early (such a missing setting, or a game
that ends by itself in its first 10 minutes with another code than 0) no longer
come every 5 s: nothing after the first, then the next start waits 30 s more
each time (30, 60 ... 600 s at most; the service is "activating" meanwhile),
until a game lasts 10 minutes, ends as asked or the service is stopped. The
bot's log says `mgo1host: 3 starts in a row ended early: the next one in 60 s`
and a missing setting's line once. After a fix, `mgo1hostctl restart` (or
`systemctl restart mgo1host@<N>`) starts it at once: a wait is taken once.

## The Game Report

With MGO1_EVENTS_DIR set (mgo1host.sh sets it), the host writes what happens
in its game as JSON lines: joins, teams, kills and stuns with the counters they
changed and the game's HUD message, the KEROTAN (taken, dropped, placed in a
goal, the capture's countdown), rounds and their ends, the players' round
scores and the lobby frames the host sent (never the log-in's). MGO1 Host shows
them in the program's language and writes one .log a day on the PC.

## No log-in data in the logs

The bot's account ID, its password and the game's password lock never reach a log: the host
masks them (`***`) in its log, the Game Report, status.json, the P2P packet log and its memory
dumps; mgo1host.sh filters the game's output once more (line by line, `awk -W interactive` with
mawk); mgo1host-agent and mgo1hostctl mask them again in what they print (`log`, `status`,
`bots`, the events, `redact` filters any text). The owner's actions (`<date>.admin.jsonl`) no
longer name the account. Lines written by a much older host are masked when read; to
delete them: `journalctl --rotate` then `journalctl --vacuum-time=1s`.

## When something goes wrong

- `mgo1hostctl log`: the screens the game shows (`text: ...`), the log-in, the
  lobby. A log-in that fails (wrong account or password) ends the process after
  10 minutes without a game (`host watch: no game of its own`), and the service
  tries again; meanwhile `mgo1hostctl bots` shows the bot offline with the
  game's error message (`log-in error: Server login failed. ( 816:-4 )`), as
  the Game Report and MGO1 Host do.
- `P2P_LOG=1` in /etc/mgo1host/server.env: the game's P2P traffic in the clear
  in /var/lib/mgo1host/bot<N>/run/p2p.tsv (about 50 MB an hour: for
  troubleshooting only).
