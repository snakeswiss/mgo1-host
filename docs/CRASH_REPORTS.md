# Crash reports (v150-host beta 16)

For the server's owner. When a bot's or a PLAYABLE DOLL's game ends badly, the server keeps a small text file with what
ended, the game it was hosting, its players and the last lines the game wrote. One file tells what came before the
failure: no shell command to write, no day's log to search.

Why: on 9 and 10 November 2026 a bot's game wrote 5000 lines a second until the watchdog restarted it; the day's log was
full in two minutes and its export held those lines only. The game, the players and the virtual dolls' last actions had
to be dug out by hand.

## Where, and how long

    /var/log/mgo1host/bot<N>/crash/<YYYY-MM-DD_HHMMSS>_<reason>.txt     (a bot; the time is UTC)
    /var/log/mgo1host/doll<N>/crash/<YYYY-MM-DD_HHMMSS>_<reason>.txt    (a PLAYABLE DOLL)

A report is 2 MB at most. A folder keeps its 40 newest reports and none older than 31 days (the oldest go). At most 6
reports are written in an hour for one game: when a game crashes in a loop the first ones are kept and the day's log
says `mgo1log: no crash report for this end (...): 6 written in the last hour`.

## When one is written

| reason in the file's name | what happened | who notices |
|---|---|---|
| `crash70` | the game crashed, or ended itself because its code walked outside its memory without end (its `CRASH:` lines tell where; exit code 70) | the game's launcher |
| `signal<N>` | the game was ended by a signal (`signal9`: killed, out of memory or forced) | the launcher |
| `hung` | the game stopped writing its status and the doll manager's watchdog restarted it | the doll manager |
| `lost` | a bot: its game was lost (exit code 2) or the lobby's connection was (6); a doll: its match was lost (5) | the launcher |
| `exit<N>` | another exit code | the launcher |
| `wild` | the game ended with no failure (a restart you asked, a stop) but its code had gone outside its memory before: the lines around that moment are kept | the launcher |

No report for an end that is no failure: a restart you asked, a stop, a bot that found no game in time (3), a doll back in
the lobby after a match (3), a doll whose game is not listed (4) or told to leave (0).

The day's log names each report: `mgo1log: crash report crash/2026-11-10_005015_hung.txt (hung: the game's last 4000
lines before its end; docs/CRASH_REPORTS.md)`, and for a hung game the manager adds `mgo1doll-manager: bot2: crash report
/var/log/mgo1host/bot2/crash/2026-11-10_005015_hung.txt (...)`.

## What a report holds

    MGO1 crash report (host/mgo1log, docs/CRASH_REPORTS.md)
    source: bot2
    reason: hung
    what: its game wrote nothing for 183 s (hung): restarted by the doll manager
    when: 2026-11-10T00:50:15.017Z
    version: v150-beta16 0123456789ab
    status: written 183 s before the end
    state: hosting
    frame: 2573208
    game: SaveMGO Host
    round: 2 of 12 (rule 3, map 2)
    players: 5 (1 real, 3 dolls, 1 host)
    player: P1 host
    player: P2 virtual doll 1, team red, 12 kills, 3 deaths
    player: P3 virtual doll 2, team blue, 2 kills, 16 deaths
    player: P5 real "PLAYER ONE", team red, 4 kills, 2 deaths, ping 80 ms
    vdoll: 2 in_game, player 3, team blue, spawned, 16 deaths
    pace: 44.28 VBlanks a second (59.94 due), 66 % of a core
    wild: 2026-11-10T00:32:44.957Z ps2: hardware read outside the emulated registers at 1476ed50 (00000000); calls before: ...
    lines: 4000, the last ones before the end, from 2026-11-10T00:31:55.507Z
    ----
    2026-11-10T00:32:37.011Z I ps2 f2572817: vdoll 2: dead, by player 2 (16 deaths)
    2026-11-10T00:32:44.954Z I ps2 f2573208: vdoll 2: respawns at -2000 464 -31500
    2026-11-10T00:32:44.957Z W ps2: hardware read outside the emulated registers at 1476ed50 (00000000); calls before: ...
    ...

- The header: where it comes from, the reason, what ended in words, when (UTC), the installed version
  (`/opt/mgo1host/VERSION`), then the game's last status file: its state, its game, the round (the rule and the map by
  their numbers: host/README.md lists them; rule 3 capture, map 2 Ghost Factory), each player with his kind (the host,
  a virtual doll or a doll by its number, a real player by the name the game shows), each virtual doll's own state
  (`vdoll:`: in the game or not, its team, alive or not, its deaths), the last minute's pace. `status: not
  readable` when the game left no status file. A doll's report has a `doll:` line instead (its rule, map, team, life,
  kills, deaths and what it was doing).
- `wild:` is there when the game's code went outside its memory: the first `ps2: hardware read|write outside the emulated
  registers ...` line (the same words without "outside" are the game's ordinary lines). The 200 lines before it and the
  100 from it are always in the report, even when the game wrote thousands of lines afterwards (a line of `mgo1log:` then
  says how many lines between are left out).
- Below `----`: the game's last lines (4000 at most) as the daily log writes them, time (UTC), level (E error, W warning,
  I), text. A flood is in it as in the daily log: the first 200 lines of a kind, then `mgo1log: 48211 more lines like
  "..." in 10 s, the first at ... (10886a50 00000000), the last at ... (108c5d40 00000000)`. The lines the day's log left
  out past its 64 MB cap are in the report all the same.

No account, no password and no game password lock are in a report: its lines are the masked lines of the daily log, a
doll is named by its number, and `mgo1host-agent crash` masks once more what it sends.

## Fetch one from Windows (PowerShell)

With your key and your server as for your other SSH commands (each command on one line; another
bot than 2: `bot<N>`; a doll: `doll<N>`).

The reports of the server, the newest first:

    ssh -i "$env:USERPROFILE\.ssh\id_ed25519" root@<server> 'ls -lt /var/log/mgo1host/*/crash/'

The newest report of bot 2, written on your Desktop:

    ssh -i "$env:USERPROFILE\.ssh\id_ed25519" root@<server> 'cat $(ls -t /var/log/mgo1host/bot2/crash/*.txt | head -1)' | Set-Content -Encoding utf8 "$env:USERPROFILE\Desktop\bot2_crash.txt"

One report by its name, or the whole folder:

    scp -i "$env:USERPROFILE\.ssh\id_ed25519" root@<server>:/var/log/mgo1host/bot2/crash/2026-11-10_005015_hung.txt "$env:USERPROFILE\Desktop\"
    scp -i "$env:USERPROFILE\.ssh\id_ed25519" -r root@<server>:/var/log/mgo1host/bot2/crash "$env:USERPROFILE\Desktop\bot2_crash"

Send the file as it is to whoever helps you: it holds what a first look needs.

## For MGO1 Host and other programs: mgo1host-agent

| command | what it gives (one JSON object) |
|---|---|
| `mgo1host-agent crashes` | every report, the newest first: `source`, `name`, `time` (UTC), `reason`, `size` and the header's fields (`what`, `when`, `version`, `state`, `game`, `round`, `rule`, `rule_name`, `map`, `map_name`, `players`, `who`: the players' lines, `pace`, `wild`, `lines`) |
| `mgo1host-agent crashes -b 2`, `crashes --doll 3` | one bot's, one doll's |
| `mgo1host-agent crash bot2/2026-11-10_005015_hung.txt` | that report: the same fields, and its `lines` by pages as `logs get` (`--from <n>`, `--lines <n>` up to 5000, `--tail <n>`; `total`, `next`); the name alone finds it in any folder |

## How it works

- `mgo1log`, which writes each game's daily log, keeps in memory the last 4000 lines it handled for that game, with the
  floods' summaries in their place, whatever the day's cap let into the file.
- The launchers (`mgo1host.sh`, `mgo1doll.sh`) write the game's exit code into `run/exit` before the log's pipe closes:
  `mgo1log` reads it at the end and writes the report when the code is a failure.
- A hung game does not end by itself: the doll manager's watchdog restarts it (a bot's game 45 s after its last status,
  a doll's 180 s). Just before, the manager leaves a request
  (`crash/.hung`) in the game's log folder; `mgo1log` takes it when the restart stops it and writes the report under
  the name the manager chose.
- Since v150-host beta 16 the game itself ends a walk outside its memory that never ends (three `CRASH:` lines, exit
  code 70): the report is then a `crash70` one, written seconds after the first access and not after the watchdog's wait.

Settings (the environment of the game's service; the defaults are meant to be kept): `MGO1_CRASH_LINES` (4000; 0: no
report), `MGO1_CRASH_KB` (2048), `MGO1_CRASH_KEEP` (40), `MGO1_CRASH_HOURLY` (6; 0: no limit), `MGO1_LOG_KEEP_DAYS` (31).

What a report cannot hold: lines the game never wrote, and anything when the server itself goes down (a power cut, the
logger killed with the game by the kernel): the daily log is then all there is.
