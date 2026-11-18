# MGO1 Host

MGO1 Host.exe installs, sets and watches your SaveMGO MGO1 dedicated hosts: the game's own code,
built from your disc, hosting games around the clock on a Linux server you rent. The program
talks to that server over SSH only.

- Bots: one server runs up to 10 bots, each hosting its own game with its own SaveMGO account and
  UDP port (5730 to 5739). BOTS adds one in two clicks (with an account this PC keeps), opens its
  UDP port on the server's firewall, starts, stops, restarts or removes it; the other pages show
  and command the bot picked in the menu.
- Live state: a green (ONLINE), amber (STARTING) or red (OFFLINE) light, the game, the round,
  the lobby link, the host's CPU and memory.
- Live players: name, ping in ms (every second), team, kills / deaths, points; ACTIONS:
  kick, add to the blacklist, add to the whitelist.
- GAME REPORT: what happens in the bot's game, with the time: joins, teams and team changes,
  kills (weapon, headshot, points), stuns, CQC, suicides, the KEROTAN taken, dropped and placed in
  a goal, the capture's countdown (15, 10 and 5 s left), rounds, GAME RESTART, round ends and
  winners, each player's round score, kicks, your own actions. Under each line, the packets the
  host sent to the lobby server and the game's own messages behind it. One file a day on this PC
  (`Documents\MGO1 Host\Game Report\<server>\Bot <N>\<date>.log`), in the program's language.
- LOG: the host's journal in the program's language (TECHNICAL LINES adds the raw lines); SAVE
  writes it in a file to send to whoever helps you. No log-in data in it, nor anywhere else. The
  daily logs open on their last lines (EARLIER LINES for the ones before); SAVE writes the whole day.
- NETWORK: the game's servers for every bot (SaveMGO's by default, or another gate and STUN
  server: a new service if SaveMGO ever stops, a server of your own, a local one), the bots' UDP
  ports and the firewalls.
- RESTART GAME, STOP HOST, START HOST, a daily restart when no one plays.
- The host's SaveMGO account, all the Create Game settings (name, password lock, comment, up to
  15 rules and maps, teams, players, rating, weapons, friendly fire, kicks, balance, voice chat,
  times, rounds, tickets). Host Only is always on.
- The game settings this PC keeps: the last ones saved (a new bot gets them; USE puts them back in
  the form, on a reinstalled server for one) and up to 20 presets of your own, their password locks
  encrypted.
- The servers and the SaveMGO accounts this PC keeps (passwords encrypted for your Windows
  account), installation and updates from the program.
- The game's own menu sounds on the clicks (the cursor, OK, cancel), played from your disc's
  STAGE_N.DAT (the DISC FILES folder of the SERVER page): the program carries none of the game's
  sounds. OPTIONS turns them off or sets their volume.
- 11 languages, everything included (messages, errors, the log): English (the default), French,
  German, Italian, Spanish, Portuguese (Brazil), Russian, Japanese, Korean, Chinese (Simplified
  and Traditional). OPTIONS picks one.
- The SaveMGO logo at the top opens https://mgo2pc.com/portal/.

## Quick start: rent a server, then QUICK SETUP

Rent a server, then QUICK SETUP: three fields and GO.

1. Rent an x86-64 Linux server with Ubuntu 24.04 (2 cores, 2 GB of memory or more; which one and where: Which server,
   below). Its provider gives you its IPv4 address and its root password.
2. Start MGO1 Host. With no server yet it opens on QUICK SETUP (in the menu too, until a server answers; then the SERVER
   page has its button). Fill in the server's ADDRESS and PASSWORD (SSH PORT 22 and USER root are there already; SIGN-IN:
   SSH KEY for a key), the bot's SaveMGO ACCOUNT ID and PASSWORD (capitals; kept on this PC, encrypted, as HOST ACCOUNT
   keeps accounts; KEPT ON THIS PC picks one you kept before), and your disc's folder (CHOOSE FOLDER: the one holding
   `MGS` and `MODULES`; the page says at once whether the disc's files are there).
3. GO. Each step shows with its state (WAITING, RUNNING, DONE, FAILED) and the whole progress:
   1. CONNECT TO THE SERVER.
   2. ITS KEY, THE SIGN-IN: the server's key shows once, TRUST (the one question asked; never trusted without you).
   3. INSTALL THE HOST: the host's files and your disc's files go to the server. When the package holds no host program
      (the public download: no code of the game in it), the server builds it from your disc's files: 20 to 60 minutes
      the first time, its stage, percent and time so far on the page; the program keeps the connection open and the other
      pages work meanwhile; an update builds only what changed. A server that has this version already: skipped.
   4. BOT 1'S ACCOUNT.
   5. GAME SETTINGS: SAVEMGO HOST, TEAM DEATHMATCH and DEATHMATCH on CITY UNDER SIEGE, GHOST FACTORY, MOUNTAINTOP and
      BROWN TOWN, 8 players, the comment `Virtual dolls fill the empty seats`, the program's other defaults (5 minute
      rounds, friendly fire off, the idle kick, voice chat).
   6. VIRTUAL DOLLS: 7 (the game's 8 seats less the one always kept for a real player: they fill the empty seats, the
      newest leaves when a real player comes), AS A PLAYER, level 5, the LIVE LEVEL on from 1 to 10.
   7. START BOT 1.
   8. BOT 1 ONLINE: the game is in SaveMGO's lobby; the dashboard opens.

A step that fails says why in red and what to do; GO again starts at that step (a stopped build goes on where it
stopped; an account SaveMGO refuses starts again at the account, once you corrected it). CANCEL stops at the step running.
Afterwards each part is changed on its own page (GAME SETTINGS, VIRTUAL DOLLS, HOST ACCOUNT, BOTS for more games), as
before. From A to Z, below, does each step by hand.

## Build 1.00

This is build 1.00, the release after the betas. Every version you see says `build 1.00`: the window's title and ABOUT
(PROGRAM: `MGO1 Host build 1.00`, HOST PACKAGE: `build 1.00` and 12 hex digits), the bottom line once connected
(`agent build 1.00`, the server's program), the SERVER page (INSTALLED VERSION and VERSION IN THIS PROGRAM: `build 1.00`
and the same 12 digits), the saved logs' first line (`MGO1 Host build 1.00, agent build 1.00, ...`), the host's own log
(`version build 1.00 ...`), and the packages: `MGO1_Host_build-1.00_windows.zip`, `MGO1_Host_build-1.00_linux-x86_64.tar.gz`,
`SHA256SUMS_build-1.00.txt`.

From a beta: replace the program's folder with this one, connect, SERVER page, UPDATE THE HOST (about a minute, each
bot's game starts again; the settings, the accounts and the player lists stay). Until then the server's program is
older than this one and the pages say SERVER PROGRAM OUT OF DATE with the button right there. On the server,
`cat /opt/mgo1host/VERSION` then gives `build 1.00 ...` and `mgo1host-agent version` `"agent": "build 1.00"`. Keep this
program with a server at build 1.00: a beta's MGO1 Host would take the server's program for an older one than its own
and offer to install its beta again.

What each beta brought, before build 1.00:

## Before build 1.00: the v150 betas

- The host and its dolls run the v150 engine; the PS2 and PCSX2 players join its games (up to 8 players).
- VIRTUAL DOLLS (their own page under PLAYABLE DOLL): players the bot's own game runs, with no game of their own and no
  account. A PLAYABLE DOLL is a whole game (about 0.23 of a core); a virtual doll costs the bot about 0.03 to 0.05 of a
  core while it plays: up to 7 in a game of 8 (about half a core for the bot). One seat always stays free for a real
  player, and a virtual doll leaves at once when a real player needs its seat. (beta 15) They play as players of their level: their weapon (AK-47, XM16E1, SCORPION, M37,
  SVD or AUTO, the M1911A1 at hand), crouching, leaning out and rolling in a fight, grenades, CQC and rolls into an
  enemy at close range, the rule's objective (KEROTAN, GA-KO, microfilm) by ways round the enemies; they die and
  come back in every rule; their level 1 to 10 sets their reaction, aim, sight and these moves. Set them per bot (count,
  teams, level, weapon, names): the bot's next game takes them, or SAVE AND RESTART; the count also live. Their games
  stay out of SaveMGO's rankings as the dolls' do.
- A PS2 player who comes back from the same connection with another disc (a Korean or North American one, then a PAL
  one) gets in: beta 13's bot kept the first disc's session dialect for that address until its game ended (the PAL one
  then failed with error 1059).
- Beta 16 (MGO1 Host 3.2; the server's agent 3.4: UPDATE THE HOST once). A PLAYABLE DOLL's DIFFICULTY has one more
  choice, before 1 TRAINING: AS THE VIRTUAL DOLLS (in a doll's ACTIONS, in DOLLS' DIFFICULTY for all the dolls of a bot,
  in the form of a new doll). The doll then plays at the level set for its bot's virtual dolls (VIRTUAL DOLLS page; the
  bot's first doll takes the first virtual doll's level, the second the second's, the last level for the rest) and takes
  a new level at once when you save one there; its live level is off, as the virtual dolls do not adapt.
- Beta 16: the PLAYABLE DOLL page says why a bot's playable dolls wait, in a line under the bot's row (PLAYABLE DOLLS
  WITHOUT AN ACCOUNT) when fewer of them play than are set: no free seat (the game's seats, its real players, the seats
  held for the virtual dolls and the one kept for a real player), the bot stopped, its game not on the list yet, the
  server's processors and memory, the pace guard (its game ran under its pace: fewer dolls for some minutes), or the
  dolls without an account turned off on the server (DOLL_GATE=0).
- Beta 16: CRASH REPORTS, the third value of the LOG page's VIEW row (the server's agent 3.4). When a bot's or a doll's
  game ends badly (a crash, killed, hung and restarted by the server, its game or the lobby lost, another exit code, or
  its code gone outside its memory), the server keeps a small report: what ended, its game and round, its players, the
  game's last lines. The list shows each one's date and time on this PC's clock, its bot or doll, its size and the
  reason in your language; a click opens it as a daily log is shown, with BACK, SAVE (a file in
  `Documents\MGO1 Host\Logs`), COPY and SEARCH. A bot with reports in the last 24 hours says so in red on its row of
  the BOTS page and on the DASHBOARD: how many, the last one's time and reason.
- Beta 16: a bot whose starts end early (a setting or a disc file missing, a game dead in its first minutes) is no
  longer started every 5 seconds: the server waits before the next start, 30 s more each time and 10 minutes at most.
  Its row and the dashboard then say "Waiting for the next start: the last ones ended early (see LOG)." and no fault.
- Beta 18 (MGO1 Host 3.3; the server's agent 3.5: UPDATE THE HOST once). Two switches, each off by default, meant to
  bring the two kinds of dolls closer in strength. VIRTUAL DOLLS page, CONTROLS: DIRECT (as before) or AS A PLAYER (the
  virtual dolls turn, aim and fire within what the pad allows; with a PLAYABLE DOLL's DIRECT aim the two kinds are of
  about equal strength at level 5), saved
  with the bot's other virtual doll settings. PLAYABLE DOLL page, AIM: PAD (as before) or DIRECT (the doll turns its view
  at the speed of a virtual doll of its level, without the stick), in a doll's ACTIONS, in DOLLS' AIM for all the dolls of a bot and in the form of a
  new doll; a doll in a game takes it at once. With an older agent on the server neither choice is shown.
- Beta 19 (MGO1 Host 3.4; the server's agent 3.6: UPDATE THE HOST once). The virtual dolls' LIVE LEVEL, off by default:
  on the VIRTUAL DOLLS page, LIVE LEVEL ON makes the level of each virtual doll follow the strength of the game's real
  players, between its LOWEST LEVEL and HIGHEST LEVEL (1 and 10 by default), the LEVEL set being where it starts. A game
  running takes a change within 2 s, no restart. Each virtual doll in the game then shows its level now, as the PLAYABLE
  DOLL page does: `LEVEL 10 (NOW 7.25)`. With an older agent the three rows are greyed and the page says why.
- Beta 22 (MGO1 Host 3.5; the server's agent 3.7: UPDATE THE HOST once). GAME SETTINGS and the server's copy of them:
  the form is no longer filled by an older answer of the server (a RELOAD answered after a
  SAVE put the old rotation back in it) nor by another bot's; when the settings on the server were changed elsewhere the
  page says so, and SAVE asks before it replaces them. Under the form the page says when the game running now still plays
  the settings it started with (what was saved takes effect at its next game) and offers RESTART THE GAME NOW; the
  rotation's entries the game does not play are named in red. With an older agent the page is as before.

## Before build 1.00: the v141 public beta

- The host and the dolls run the v141 engine; the PS2 and PCSX2 players join its games.
- Up to 8 players a game, as the game's own Create Game offers (NUMBER OF PLAYERS; the host's own seat is not counted).
- The dolls' ten difficulty levels (beta 6): 1 TRAINING, 2 EASY, 3 NORMAL, 4 HARD, 5 EXPERT, 6 ELITE, 7 VETERAN, 8 MASTER,
  9 EXTREME, 10 PRO. 6 ELITE is beta 5's NIGHTMARE (a doll set to 6 before is ELITE now): from it up the elite moves, the
  lean, the cardboard box to slip away after a kill or out of a quick fight, fast moves. With the LIVE LEVEL on, a doll's
  level moves over the whole ladder, within its LIVE LEVEL MIN and MAX, after the strength of its enemies and of its own
  team.
- The dolls need no account: the host makes them (DOLL_1, DOLL_2 ...) and adds or removes them at once in a game
  that runs. The dolls with a SaveMGO account stay available, with the same brain.
- Beta 7: 10 PRO plays superhuman: it reacts at once, aims without error, never backs off and knows where its enemies
  are (it still shoots only what it can see). Levels 1 to 9 are unchanged and human-like. With the LIVE LEVEL on, a doll
  set to 10 plays so only while its level stays near 10 (against strong players); against beginners its level comes
  down and it plays human again.
- Beta 7: the games with dolls stay out of SaveMGO's rankings (SAVEMGO RANKINGS on the PLAYABLE DOLL page, agent 3.1):
  once a doll is in a game, the players' results of that game are sent with a VS RATING of 0, as SaveMGO does for a
  game with a password: they show in the game's own scores, never in the rankings nor the VS RATING, so nobody boosts
  his ranking against the bots. BOTS says "unranked" after the players of such a game.

## What you need

- A Windows 10 or 11 PC (64-bit).
- The files of your own MGO1 disc (the US MGS3 Subsistence online disc): `MGS\NET\MGS3_N.ELF`,
  `MGS\NET\STAGE_N.DAT`, `MGS\NET\SLOT_N.DAT` and the folder `MODULES`.
- A SaveMGO account for each bot, used by that bot alone (SaveMGO keeps one session an account).
  MGO1 accounts are in CAPITALS: the ID and the password are made of capital letters, digits and
  `. _ -`.
- A server, as below.

## Which server (Hetzner Cloud, prices of September 2026)

A bot is one MGO1 game: the players connect to it, so its place decides their ping. Hetzner has
no data center in Japan. Round trips between the regions (data center to data center; a
player's own line adds a little):

| server in | Japan | US East | US West | Europe |
|---|---|---|---|---|
| Hillsboro (US West) | 100 to 135 ms | 60 to 80 ms | (local) | 145 to 170 ms |
| Ashburn (US East) | 150 to 195 ms | (local) | 60 to 80 ms | 95 to 120 ms |
| Falkenstein, Nuremberg (Germany) | 235 to 250 ms | 95 to 120 ms | 145 to 170 ms | (local) |
| Singapore | 85 to 110 ms | | about 180 ms | 160 to 180 ms |

The types, prices before tax (Hetzner, from 15 June 2026), plus about 0.50 EUR a month for the
IPv4. A bot needs about 0.6 of a core and 350 MB of memory:

| type | where | vCPU, memory | bots | a month |
|---|---|---|---|---|
| CX23 | Germany, Finland | 2, 4 GB | 3 | 5.49 EUR |
| CX33 | Germany, Finland | 4, 8 GB | 6 | 8.49 EUR |
| CPX11 | Ashburn, Hillsboro | 2, 2 GB | 3 | 20.49 USD |
| CPX21 | Ashburn, Hillsboro | 3, 4 GB | 5 | 37.49 USD |
| CPX12 | Singapore | 1, 2 GB | 1 | 17.99 USD |

What to take:

- **For Japanese, American and European players, the best: two servers.** A CX23 in
  Falkenstein or Nuremberg for Europe, a CPX11 in Hillsboro for the Americas and Japan: about
  28 USD a month for 6 games. Every game is in the same SaveMGO lobby: name them by region
  (GAME SETTINGS, HOST NAME, for example `EU DM 1`, `US JP DM 1`) and each player joins the
  nearest one.
- **One server for everyone: a CPX11 in Hillsboro** (US West): Japan and Europe both at about
  100 to 170 ms, US East 60 to 80 ms. If most of your players are in Europe and the
  Americas, Ashburn (US East) instead: Europe about 100 ms, Japan about 150 to 195 ms.
- **Europe only: a CX23** (3 games), a CX33 for 6. The CX types are sometimes sold out in one
  location: take another German or Finnish location, or a CX33.
- Not the Arm types (CAX): the host needs x86-64.

Any other provider works the same way: an x86-64 VPS with Ubuntu 22.04 or 24.04 (or Debian 12),
2 cores, 2 GB of memory, a public IPv4 and the bots' UDP ports open. With the public package (the
host built on the server): Ubuntu 24.04 (the official build's compiler: the same file), about
2.7 GB of free disk the first time, and the more cores the shorter the first build. A provider with a Tokyo
data center puts Japanese players at a few milliseconds and the US West Coast at about 100 ms.

## From A to Z

Each step by hand, for those who want to see it (QUICK SETUP does steps 2 to 6 at once, above).

### 1. Rent the server

1. hetzner.com, Cloud, create an account; in the Cloud Console, a project, then **Add Server**:
   - **Location**: as above (Hillsboro, Ashburn, Falkenstein, Nuremberg, Helsinki).
   - **Image**: Ubuntu 24.04.
   - **Type**: Shared vCPU, **x86 (AMD/Intel)**, the type chosen above.
   - **Networking**: public IPv4 on.
   - **SSH keys**: yours, or none: Hetzner then emails you the root password.
   - Volumes, backups, placement groups: not needed.
2. **Firewall**: none is needed. If you add a Hetzner Cloud Firewall, allow inbound TCP 22
   (this program) and UDP 5730 to 5739 (the players, one port a bot).
3. Note the server's **IPv4 address** (the server's page in the Cloud Console).

### 2. Connect the program

1. Unzip the folder anywhere; keep `server` next to `MGO1 Host.exe`. Start `MGO1 Host.exe`
   (English at first: OPTIONS, LANGUAGE for yours).
2. SERVER page: NAME (yours, optional), ADDRESS (the IPv4), SSH PORT 22, USER root, SIGN-IN:
   PASSWORD (the one Hetzner emailed; REMEMBER on keeps it on this PC, encrypted) or SSH KEY
   (KEY FILE and KEY PASSPHRASE), CONNECT.
3. The first time, the program shows the server's key: TRUST. "Connected." shows in green.

### 3. Install the host

SERVER page, INSTALL / UPDATE: CHOOSE FOLDER (the folder of your disc's files: the one holding
`MGS` and `MODULES`), INSTALL THE HOST, YES. A few minutes: the program checks the server, sends
the host and the disc's files (about 90 MB), installs Wine and a service for bot 1. Each step
shows below, then "The host is installed"; INSTALLED VERSION then says `build 1.00` and 12 hex digits.

With the public package (the GitHub release: its `server` folder holds `mgo1host-src.tar.gz` and no
`mgo1host.exe`, its VERSION says `build 1.00 source`), the host is built on your server from these
sources and your disc's files: the first time takes about 35 minutes on 2 cores (less with more cores), its
steps and percent shown as it goes; an update about 8 minutes; the same package again builds
nothing. It needs the North American disc (another version is refused by name) and about 2.7 GB
free on the server the first time (195 MB stay for the updates). Nothing of the game leaves
your server.

### 4. The bot's SaveMGO account

HOST ACCOUNT: the ACCOUNT ID and the PASSWORD in capitals, KEEP ON THIS PC on (the program keeps
it, encrypted, for later bots), SAVE AND RESTART.

### 5. The game

GAME SETTINGS: the HOST NAME the players see in the lobby, a PASSWORD LOCK if the game is
private, a COMMENT, the RULE & MAP ORDER (up to 15 rules and maps), teams, players (up to 8),
rating, weapons, friendly fire, kicks, balance, voice chat, the RULE SETTINGS (times, rounds,
tickets). SAVE AND RESTART. REIKO HINOMOTO / ROWDY REIKO (the game's own Create Game shows it as ???):
Reiko Hinomoto and Rowdy Reiko play in place of two UNIQUE CHARACTERS (GRU against KGB: Raikov and Sokolov), so
turning it on turns UNIQUE CHARACTERS on, and UNIQUE CHARACTERS off turns it off.

Every SAVE (and SAVE AND RESTART) also keeps these settings on this PC as the last ones saved: GAME
SETTINGS KEPT ON THIS PC, at the end of the page. A new bot gets them at once; on a reinstalled
server, USE on LAST SAVED puts them back in the form, then SAVE. SAVE AS PRESET keeps the form under a
name of yours (up to 20 presets, for any bot of any server); USE fills the form with one, FORGET
deletes it. Nothing goes to the server before SAVE.

### 6. The game starts by itself

There is nothing to launch in the game: the bot logs in to SaveMGO with its account, creates
its game with these settings (Host Only: the bot does not play) and keeps it open around the
clock. About a minute later DASHBOARD shows the green light, ONLINE, and the game in the lobby.
If a game ends or the lobby link is lost, the bot creates a new one by itself.

- SAVE AND RESTART (GAME SETTINGS): the new settings now. SAVE alone: for the next game (the page then says what
  the game running now does not play yet, with RESTART THE GAME NOW).
- RESTART GAME (DASHBOARD or the bot's ACTIONS): a new game now.
- STOP HOST / START HOST: the bot off or on; it starts with the server.

### 7. How the players join

In MGO1 (their PS2 or PCSX2 set up for SaveMGO): the online mode, log in with their own SaveMGO
account, the lobby, then **Join Game** and the game by its HOST NAME in the host list (the
password lock if you set one), or **Quick Join** and the game's rule. The players must use the
same servers as the bots (SaveMGO's, unless you changed them in NETWORK).

## More games: more bots

BOTS, ADD A BOT: an account this PC keeps (or a new one), the UDP port (the next free one of
5730 to 5739), ADD THE BOT. The program creates the bot, opens its UDP port on the server's own
firewall (ufw or firewalld, when there is one), sends it the last game settings saved from this PC
(the same name, password lock and comment too: change them on GAME SETTINGS) and the account, and
starts it: its game is in the lobby a minute later. If your provider has a firewall (Hetzner Cloud Firewall), allow the
port there too. The bot picked in the menu (BOT, its arrows) is the one DASHBOARD, PLAYERS, GAME
REPORT, GAME SETTINGS, HOST ACCOUNT and LOG show and command. ACTIONS on a bot's row: manage,
start or stop, restart its game, remove it. The blacklist is shared by all the bots; each bot has
its own settings, account and whitelist. More servers: the SERVER page keeps them all (one
click connects).

## Players run by the server: PLAYABLE DOLL

A PLAYABLE DOLL is a player the server itself runs in one of its bots' games, so that a game is
never empty: it logs in with no account at all (DOLL_1, DOLL_2 ...) or with a SaveMGO account of its
own (never a bot's, never yours), joins its bot's game, takes a team, chooses its weapons and plays
the rule (fights, the objective, food, hiding), and keeps playing with this PC off. As many as its
bot's game has seats for (64 a server at most, its cores and memory permitting).

After a new MGO1 Host, UPDATE THE HOST once (SERVER page): while the server runs an older program,
the DASHBOARD, BOTS and PLAYABLE DOLL pages say so in red with the button right there (the dolls
without an account, the ten difficulty levels, the live level's bounds and the newer settings need it).

DOLLS WITHOUT AN ACCOUNT (an older server program: UPDATE THE HOST once): a row for each bot,
its number of dolls and - ONE DOLL / + ONE DOLL. A doll added there is DOLL_<number> (the lowest free
one): it logs in to a small lobby the server runs for its bots' dolls (on the server itself, nothing to
open, nothing to create on SaveMGO) and joins its bot's game like any player. DOLLS IN TX-55'S GAME LIST
says how SaveMGO's game list and its public API show them: AS THE HOST'S ACCOUNT (the
default: each doll counts as a player of the game, under the host bot's account name, so the game shows
as played), WITH THEIR OWN IDS (each with its own number and no name, only if the lobby's database takes a
player without an account), NOT LISTED (as before). Each bot takes it when its game starts again. Listed,
one seat always stays free for a real player. The dolls' results are never recorded there. SAVEMGO RANKINGS:
GAMES WITHOUT DOLLS (the default: once a doll is in a game, that game's results go out of SaveMGO's rankings
and VS RATING to its end, as a game with a password's), EVERY GAME (ranked, dolls or not, as before), NO GAME (every
game of the server out of the rankings); each bot takes it when its game starts again. - ONE DOLL
removes the highest number first; REMOVE THE DOLL in its ACTIONS does the same for that one. Their names:
NAME OF THE NEXT DOLL gives the doll + ONE DOLL adds a name of your choice; NAME IN GAME and
RENAME in a doll's ACTIONS change it later (empty: DOLL_<number> again). 3 to 15 characters: letters A to Z, digits,
spaces and . _ - ! ? + *, never another doll's name; it is the name every player sees in the game. A doll in a game
leaves and comes back under its new name within about half a minute. Pick names real players do not use: the dolls
take any player of a doll's name for one of them.

PLAYABLE DOLL, ADD A DOLL WITH A SAVEMGO ACCOUNT (ADD A PLAYABLE DOLL with an older agent): an account this PC keeps (or a new one), its bot, its
DIFFICULTY (1 TRAINING to 10 PRO), TEAM (AUTO, RED, BLUE, SPECTATOR), WEAPONS (BY ITS ROLE, or one), ADD THE
DOLL. It plays as soon as its bot's game has a seat for it: SEAT FOR A REAL PLAYER (on by default)
always leaves one seat free, and when a real player comes into a full game, the last doll leaves at
once. When fewer of a bot's dolls play than are set, a line under the bot's row of PLAYABLE DOLLS WITHOUT AN ACCOUNT
says why: no free seat, with the game's seats, its real players, the seats held for its virtual dolls
and the one kept for a real player; the bot stopped; its game not on the list yet; the server's processors and memory
allow no more doll; the pace guard (the game ran under its pace: the server lets fewer dolls in for some minutes;
DOLL_PACE_GUARD in the server's server.env); the dolls without an account turned off (DOLL_GATE=0). PLAYING AT ONCE: how many dolls the server's cores and memory allow besides its bots (a doll
is a whole game: 0.6 of a core counted and 250 MB, a quarter of the cores kept free: a game short of
processor runs slower than the clock and its players see lag; the bots' games get the processor first). Each doll's row shows what it does, live; ACTIONS
changes its difficulty, team, ORDER (PLAY FREELY, OBJECTIVE, HIDE, DEFEND, FOLLOW or TARGET a player
of its game), weapons, PLAYS on or off, LEAVE THE GAME NOW, REMOVE THE DOLL. If only dolls are in a
game, they watch as spectators until a real player comes: their accounts never earn points against
each other. Their passwords are kept encrypted on this PC and never appear in any log.

They play as teammates do: they answer your codec calls (Affirmative, Negative, or nothing, by their
level and what they are doing; Negative when too far or fighting; nothing when stunned) and do what
you ask: FOLLOW ME, BACKUP (two dolls at most with you; when you stop to shoot, they take cover by you
and shoot what you aim at), HALT, COME HERE, DEFEND THE TARGET (the objective played harder), GO GO
GO! (on again, your followers spread out); they call ENEMY SIGHTED, BACKUP, ALL CLEAR, NICE JOB
themselves. They climb over fences, low walls and the lab's windows (a roll over the low ones), put
their suppressors on at each spawn (M1911A1, Mk22, XM16E1; not from ELITE up), lean and crouch while shooting from the
first person view at the higher levels, fire the AK-47 and the M63 a bullet or a short burst at a
time at range, finish an enemy put to sleep, and play a game named HEAD SHOT... (or HEAD_SHOT...) for
headshots only. They know only what a player could: an enemy seen, his steps close by, his shots
within 35 m; they never aim at nor follow an enemy through a wall. Their difficulties play apart as
they should (measured: three dolls with the same rifle, level 5 35 kills and 20 deaths, level 3 25 and
29, level 1 19 and 36). With SBMM
BOTS on (PLAYABLE DOLL page, a bot's game at a time; or LIVE LEVEL in a doll's ACTIONS) their level moves over the
whole ladder after the strength of their enemies and of their own team (each player's kills and deaths these last
minutes, a real player's VS RATING, a rival who dominates the dolls, weighed by how much is known) and the match's
stance (the tickets or the objective, the rounds), judged every 5 s while real players play. LIVE LEVEL MIN and MAX (a
doll's ACTIONS, 1 and 10 by default) are the server's control: the live level never leaves them. The row shows the
difficulty set over ACTIONS (LEVEL 5 EXPERT) and the live level under it (NOW 7.3 VETERAN); its help, the strengths, how
sure that is and why the level moves, or that the doll waits for a real player. Off, DOLLS' DIFFICULTY sets all of them at
once. AS THE VIRTUAL DOLLS, the first choice of DIFFICULTY in a doll's ACTIONS, of DOLLS' DIFFICULTY
and of a new doll's form: the doll has no difficulty of its own, it plays at the level set for its bot's virtual dolls
(VIRTUAL DOLLS page: the bot's first doll takes the first virtual doll's level, the second the second's, the last level
for the rest) and takes a new one at once when you save it there. Its row shows that level over ACTIONS and
(virtual dolls' level) under it; in its ACTIONS, LIVE LEVEL, MIN and MAX are shown off and cannot be changed (the live level is
off: the virtual dolls do not adapt), and SBMM BOTS is off for a bot whose dolls all follow. In deathmatch they wear the unit the game gives them. AIM, in a doll's ACTIONS, in DOLLS'
AIM for all the dolls of a bot and in a new doll's form: PAD (the default, as before: the doll turns its view through
the stick, as a player does) or DIRECT (it turns its view at the speed of a virtual doll of its level, without the stick); a doll in a game takes it at
once, and it keeps it for its next games. The rows are there only when the server's agent takes it. LOG, LOG OF and DAY:
what each bot and doll did, day by day, in your language.

They salute (the emote: Triangle held) for their victim's kill camera when nothing threatens them,
answer an emote you make at them (an enemy's too, sparing you while they answer) and now and then
greet you; they roll into an enemy to knock him down (the carrier of the KEROTAN, the GA-KO or the
microfilm drops it; with no rounds left; before a CQC or stun attack). A stun grenade's flash blinds
them as long as their screen is white; a chaff grenade takes their codec and radar away (no call
made or heard, the objective where it was last seen); a smoke hides you from them beyond 4 m and
ruins their aim through it (their own smoke covers their retreat); with the thermal goggles from a
weapon box they see through a smoke and aim finer. Their support weapon (grenade, stun, smoke, chaff,
WP, claymore, book) is drawn at each spawn.

They fight as players move, at every level: their magazine empty, they reload running to cover, then
hold the corner toward the enemy; in a long exchange of fire at range they change place (another angle,
a low cover); losing, they fall back out of sight toward their allies, now and then in their cardboard
box, and wait for you with the gun on the corner you would come round, or in the box until you pass;
now and then they feign a retreat to draw you round a corner they hold; an enemy gone round a corner,
they watch it a moment; a still cardboard box far away fools them as it fools a player. They walk up
to a sleeping enemy before finishing him, and find their own way out of the maps' dead ends (Snake's
pit on Mountaintop, a closed room on Graniny Gorki Lab).

## Players the bot's own game runs: VIRTUAL DOLLS

The PLAYABLE DOLL page has two count blocks apart, each with its own explanation: PLAYABLE DOLLS
WITHOUT AN ACCOUNT (- ONE DOLL, + ONE DOLL: a whole game each on the server) and VIRTUAL DOLLS, LIVE (- ONE VIRTUAL DOLL,
+ ONE VIRTUAL DOLL a bot: its running game takes the count within seconds, no restart; a new one joins when a seat is
free, the one with the highest number leaves; the bot's next games keep the count).

A virtual doll is a player a bot's own game runs: no game, no account and no service of its own, about 0.05 of a core
each while it plays (a PLAYABLE DOLL is a whole game: about a quarter of a core). It joins the bot's game as a real player does, takes a
team and plays as a player of its level: it runs and shoots with its weapon (for the head at the top levels), crouches,
leans out, rolls, throws grenades, grabs an enemy at close range (CQC) or rolls into him, plays the rule's objective
(KEROTAN, GA-KO, microfilm) by ways round the enemies and plays on after a fall as the rule says. With no real player
in the game they wait; they play while a real player does, hold their join while one arrives and always leave a seat
free for a real player (when one takes it, the newest leaves at once and comes back when a seat is free again). SAVEMGO
RANKINGS and DOLLS IN TX-55'S GAME LIST (PLAYABLE DOLL page) apply to them too. They need the server's program of this
MGO1 Host (UPDATE THE HOST once).

VIRTUAL DOLLS (under PLAYABLE DOLL in the menu) shows every bot's (SHOW: that bot, as BOT under the menu), then for the
bot shown: IN THE GAME NOW, each of its virtual dolls in the game (its player, team, life and deaths, or that it joins,
leaves or waits out of the game); then their settings: NUMBER OF VIRTUAL DOLLS (0 to the game's players less the host
and the seat kept), a row for each with its NAME IN GAME (empty: VDOLL1, VDOLL2 ...; the rules of the dolls' names), its
TEAM (AUTO: its turn to the side with fewer players; RED, BLUE) and its LEVEL (1 to 10, 5 by default: at 1 it reacts in
about a second, aims wide and sees 16 m; at 10 it reacts at once, aims for the head and sees 85 m; crouching and
leaning from 4, grenades from 5, CQC and rolls into an enemy from 6; not the PLAYABLE DOLLS' ladder), its WEAPON (AUTO:
one by chance at each life; AK-47, XM16E1, SCORPION, M37, SVD; the M1911A1 when its magazine is spent near an enemy),
and EVERY VIRTUAL DOLL for the team, the level or the weapon of all at once. SAVE: the bot's next game takes them (a blue
line says so while its running game has others); SAVE AND RESTART: now (the game starts again, its players are
disconnected); RELOAD: the server's settings again.

CONTROLS, under NUMBER OF VIRTUAL DOLLS, is for all of the bot's virtual dolls: DIRECT (the default, as
before) or AS A PLAYER, with which they turn, aim and fire within what the pad lets a player do. With a PLAYABLE
DOLL's DIRECT aim the two kinds are of about equal strength at level 5 (10-minute duels). It is saved with the rest (SAVE: the bot's next game; SAVE AND RESTART: now). The row is
there only when the server's agent takes it.

LIVE LEVEL, under CONTROLS, is for all of the bot's virtual dolls: OFF (the default, as before: each one
plays at its LEVEL) or ON: during the game the level of each virtual doll follows the strength of the game's real players
(their kills and deaths these last minutes, a real player's VS RATING, the teams' score), between the LOWEST LEVEL and
the HIGHEST LEVEL (1 to 10; 1 and 10 by default); its LEVEL is where it starts. Too strong for your players at level 10?
Turn it on, or lower the HIGHEST LEVEL. SAVE sends it to the game running at once (within 2 s, no restart) and keeps it
for the next games. While it is on, each virtual doll in the game shows `LEVEL 10 (NOW 7.25)`: its LEVEL, then its
level now. A PLAYABLE DOLL set to AS THE VIRTUAL DOLLS keeps their LEVEL, not their level now. With an older server's
agent the three rows are greyed and a line says to UPDATE THE HOST.

## Day to day

- DASHBOARD and PLAYERS show the bot live (every second).
- A kick uses the game's own kick: the player leaves at once. PLAYER LISTS: a blacklisted
  name is kicked now and every time it joins; with WHITELIST ONLY on, only the whitelisted names
  may play (the others are kicked as soon as they join). The host is not SaveMGO's server: these
  lists only apply to its own games.
- GAME REPORT: SHOW picks the lines (everything, fights, players, game, the bot and you); PACKETS
  AND MESSAGES shows the details under each line. The files on this PC are written while the
  program is connected, the server keeps the last 31 days: the missing days are written at the
  next connection. OPEN THE FOLDER opens them.
- LOG: what the host does, in the program's language: the screens and menus it goes through,
  the game created, the lobby, the lists, the kicks, the restarts. TECHNICAL LINES: every line
  as the server wrote it (in English).
- NETWORK: if SaveMGO's servers ever stop, or to host on another service, enter its gate
  (log-in and lobbies) and STUN server, SAVE AND RESTART THE BOTS. The players must use the same
  servers. RESTORE SAVEMGO puts SaveMGO's back.
- The daily restart (SERVER page) waits until no one plays (up to an hour).
- UPDATE THE HOST installs a newer host from a newer program folder; the settings, the accounts
  and the player lists stay, each bot's game restarts.

## Logs and help: no log-in data

- LOG, SAVE: `Documents\MGO1 Host\Logs\Bot <N> <date>.txt`, the lines in the program's
  language then as the server wrote them, to send to whoever helps you. The Game Report files are in `Documents\MGO1 Host\Game Report`.
- LOG, VIEW, CRASH REPORTS: the reports the server keeps when a bot's or a doll's game ended badly
  (its 40 newest for each, 31 days), the newest first: date and time on this PC's clock, BOT or DOLL and its number,
  the size, and why (the game crashed; it was killed, the server short of memory; it hung and the server restarted it;
  its game or its lobby connection was lost; it ended with a code; its code went outside its memory). The line under
  the page shows what the server wrote about it, its game, round and players. A click opens the report: its header
  (what ended, when, the version, the game, each player), then the game's last lines before the end, read as a daily
  log (time on this PC's clock, errors in red, warnings in yellow, the lines this program knows in your language).
  SEARCH keeps the lines that hold a text; COPY puts the report on the clipboard as the server wrote it; SAVE writes
  `Documents\MGO1 Host\Logs\Bot <N> <date>_<reason>.txt` (the lines in your language, then as the server wrote them);
  BACK returns to the list. With an older server program the page says UPDATE THE HOST. A bot that had such an end
  in the last 24 hours shows a red line on its row (BOTS) and under its state (DASHBOARD): how many reports, the last
  one's time and reason.
- None of them holds an account ID or a password: the host masks the bot's account ID, its
  password and the game's password lock (`***`) in everything it writes (its log, the Game
  Report, its state, the P2P packet log for troubleshooting); the server's program masks them
  again in what it sends; the program's own files never hold them (the kept accounts and the kept
  game settings' password locks are encrypted).
- A server that ran a much older host may still have the account ID in old log lines: LOG
  masks them too. To delete them on the server: `journalctl --rotate` then
  `journalctl --vacuum-time=1s`.

## Security

- The program only uses SSH (TCP 22). It checks the server's key on every connection and warns
  when it changes.
- The server's password stays on your PC, encrypted for your Windows account (DPAPI), only if
  you choose REMEMBER. A key's passphrase is never saved. The SaveMGO accounts this PC keeps are
  encrypted the same way (ID and password).
- A bot's SaveMGO password goes to the server, into `/etc/mgo1host/bot<N>.env` (root only);
  the program never shows it again.
- The program's settings: `%APPDATA%\MGO1 Host\config.ini`, with the game settings it keeps (the
  last ones saved, the presets): their password locks are encrypted there like the accounts.
- The release's `server` folder holds no code of the game (the server builds the host from your disc's files); a
  `server` folder with a host you built from your disc (`mgo1host.exe.gz`) is for your own use.

## When something goes wrong

- The server does not answer: check the address, that the server is running and that TCP 22 is
  allowed.
- Sign-in refused: the user (root) and the password or the key.
- Red OFFLINE, "Log-in error" with the game's message ("Server login failed. ( 816:-4 )"): the
  bot's account on HOST ACCOUNT (ID and password in capitals), then SAVE AND RESTART. Red OFFLINE,
  "The host failed": LOG and GAME REPORT.
- The game is ONLINE but no one can join: the bot's UDP port (NETWORK) must be open in the
  provider's firewall (Hetzner Cloud Firewall).
- To ask for help: LOG, SAVE, and send the file (it holds no log-in data).
- On the server itself: `mgo1hostctl bots`, `mgo1hostctl -b 2 status`, `mgo1hostctl players`,
  `mgo1hostctl log`, `mgo1hostctl report`.

## On Linux and macOS

The same program for Linux (`mgo1-host`: X11 or XWayland, OpenGL 3) and macOS (`MGO1 Host.app`:
macOS 10.15 or later, Apple silicon and Intel), with the same pages and the same server side. What
differs:

- `server` goes next to `mgo1-host` (Linux) or next to `MGO1 Host.app` (macOS).
- The settings: `~/.config/MGO1 Host/config.ini` (Linux), `~/Library/Application Support/MGO1
  Host/config.ini` (macOS); the Game Report and the saved logs in `~/Documents/MGO1 Host`.
- The passwords this computer keeps (REMEMBER, KEEP ON THIS PC, the password locks) are encrypted
  with AES-256-GCM, its key random, in `secret.key` next to `config.ini`: a file only your user can
  read. Neither file holds a password or an account ID in the clear.
- CHOOSE FOLDER and CHOOSE KEY FILE: zenity or kdialog on Linux (without them, type the path), the
  Mac's own box on macOS. A disc mounted on Linux shows lower case names: the program finds them.
- The menu sounds play through PulseAudio, PipeWire or ALSA (Linux), Core Audio (macOS).
- Japanese, Korean and Chinese need a CJK font of the system (Linux: Noto Sans CJK, package
  `fonts-noto-cjk`), or `NotoSansCJK-Regular.ttc` in a `fonts` folder next to the program.

## Building it

The sources build on Ubuntu 24.04, or in WSL on Windows (in PowerShell, once: `wsl --install -d
Ubuntu-24.04`; Windows may ask to restart; then the lines below in Ubuntu, a drive `X:` is `/mnt/x`
there). In the repository's folder:

    sudo apt install g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 g++ cmake make perl git python3 \
        libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev
    bash hostpanel/build.sh
    python3 tools/server_src.py --out mgo1host-src.tar.gz
    bash hostpanel/package.sh --source mgo1host-src.tar.gz --out out

The folder is then `out/MGO1 Host`: `MGO1 Host.exe` and `server` with the host's sources (its `server/VERSION`:
`build 1.00 source`), as the release's package: the server builds the host at INSTALL THE HOST. `hostpanel/build.sh` alone
makes `MGO1 Host.exe` (no disc needed); it fetches Dear ImGui, OpenSSL and libssh2 from GitHub the first time.

The host itself, from your disc's files (the folder holding `MGS` and `MODULES`), as a server builds it:

    sudo bash host/build_server.sh --src mgo1host-src.tar.gz --disc /path/to/disc --out mgo1host.exe --tree ~/mgo1host-build
    bash hostpanel/package.sh --host-exe mgo1host.exe --out out

That folder's `server` then holds the host built from your disc (`server/VERSION`: `build 1.00` and the host's 12
digits): keep it for your own servers.

Linux and macOS (no disc needed either): `bash hostpanel/build_linux.sh` (gcc, cmake, perl, git,
python3 and the X11 headers: `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
libxext-dev`) makes `hostpanel/build-linux/mgo1-host` and `MGO1-Host-linux-<arch>.tar.gz`;
`bash hostpanel/build_macos.sh` (on a Mac: below) makes `hostpanel/build-macos/MGO1 Host/MGO1 Host.app` and
`MGO1-Host-macos.zip`. They fetch GLFW too. `mgo1-host --selftest <folder>` draws every page in every language and
checks the layout, as `MGO1 Host.exe --selftest <folder>` does (on Linux without a desktop: `xvfb-run -a`).
With `MGO1_HOST_SHOWCASE=1` it draws the release's screenshots instead: a plausible made-up server (every name invented,
the server `mgo1-server.example`), each page at the window's size, in `<folder>/1280` (1280 x 800) and `<folder>/1600`
(1600 x 1000 at 125 %), in English (`MGO1_HOST_SHOWCASE=all`: every language).

## macOS: building, checking, signing

`MGO1 Host.app` is one universal program (Apple silicon and Intel) for macOS 10.15 or later. It is built on a Mac, or
by the GitHub Actions workflow `.github/workflows/hostpanel-macos.yml` (a macOS 15 runner, Apple silicon): the Linux
machine that builds the Windows and Linux programs cannot build or run it.

What the build needs:

- Xcode's Command Line Tools: `xcode-select --install` (clang, make, lipo, strip, codesign, iconutil, ditto).
- cmake from Homebrew: `brew install cmake`.
- perl, git and python3 (the Command Line Tools have them; or `brew install git python`).
- GitHub the first time: Dear ImGui, GLFW, OpenSSL and libssh2 are fetched and built into `hostpanel/deps/macos`, one
  folder for each set of architectures (`DEPS=<folder>` puts them elsewhere; `JOBS=<n>` sets the parallel jobs).

The build, universal (arm64 and x86_64, the default) or one architecture:

    bash hostpanel/build_macos.sh
    ARCHS="arm64" bash hostpanel/build_macos.sh
    ARCHS="x86_64" bash hostpanel/build_macos.sh

Each makes `hostpanel/build-macos/MGO1 Host/MGO1 Host.app` (`Contents/MacOS/mgo1-host`,
`Contents/Resources/MGO1Host.icns`, `Contents/Info.plist`, `Contents/PkgInfo`), signed ad hoc, and
`hostpanel/build-macos/MGO1-Host-macos.zip`: the folder `MGO1 Host` with the app, README.txt, LISEZMOI.txt and
`licenses`. The checks (the workflow makes them too):

    A="hostpanel/build-macos/MGO1 Host/MGO1 Host.app"
    file "$A/Contents/MacOS/mgo1-host"                    # Mach-O universal binary with 2 architectures
    lipo -info "$A/Contents/MacOS/mgo1-host"              # x86_64 arm64 (one of them for ARCHS=...)
    codesign --verify --deep --strict --verbose=2 "$A"    # valid on disk, satisfies its Designated Requirement

On a Mac with a screen, `"$A/Contents/MacOS/mgo1-host" --selftest /tmp/mgo1-selftest` draws every page in every
language (`layout_report.txt`: 0 errors).

The first start: unzip, put the `server` folder next to `MGO1 Host.app`. A zip downloaded with a browser is
quarantined: macOS then starts the app from a hidden copy and the `server` folder is not found. Before the first start,
in Terminal: `xattr -dr com.apple.quarantine "<the unzipped MGO1 Host folder>"`. Then check, on a test server:

1. The app opens: its window, the SaveMGO logo, the icon in the Dock and in Finder.
2. The fonts are sharp (Retina too); OPTIONS, LANGUAGE: each of the 11 languages reads (Japanese, Korean and Chinese
   with the Mac's own fonts).
3. SERVER, CONNECT with a password (REMEMBER on: the next start connects by itself), then with an SSH key (CHOOSE KEY
   FILE: the Mac's own file box; the key's passphrase is asked, never kept).
4. INSTALL / UPDATE, CHOOSE FOLDER: the Mac's own folder box; the disc's files are found.
5. The menu sounds on the clicks once the DISC FILES folder is set (OPTIONS: off, the volume).
6. GAME SETTINGS: the server's settings shown; SAVE; SAVE AND RESTART asks first (YES, NO).
7. LOG (live and daily; SAVE writes `~/Documents/MGO1 Host/Logs` and opens it in Finder) and GAME REPORT (its files
   in `~/Documents/MGO1 Host/Game Report`). macOS asks once whether MGO1 Host may use the Documents folder: OK (asked
   again after each new ad hoc build).
8. Quit and start again: the servers, the accounts and the options are kept.

Where it keeps things: `~/Library/Application Support/MGO1 Host` (`config.ini`, and `secret.key`: the AES-256-GCM key,
32 random bytes, mode 0600, only your user reads it; the program sets a wider file back to 0600). The passwords it keeps
(REMEMBER, KEEP ON THIS PC, the password locks) are encrypted with that key; neither file holds a password or an account
ID in the clear; without `secret.key` the kept passwords are forgotten. The Game Report and the saved logs:
`~/Documents/MGO1 Host`.

Signing: build_macos.sh signs ad hoc (`codesign --sign -`), enough for the Mac that built it and, the quarantine
removed, for others; Gatekeeper does not know who made it. A release for everyone is signed with a Developer ID
Application certificate and the hardened runtime, notarized and stapled, a separate step on the publisher's own Apple
account:

    codesign --force --deep --options runtime --timestamp --sign "Developer ID Application: <name> (<team>)" "$A"
    ditto -c -k --keepParent "$A" notarize.zip
    xcrun notarytool submit notarize.zip --keychain-profile <profile> --wait
    xcrun stapler staple "$A"

then the zip made again from the stapled app.

## For developers: a game of 8 in the lobby's list

A Host Only game of 8 players is the game's 9 (the host's own seat): NUMBER OF PLAYERS 8 writes `max_players = 9`. The
host tells such a game to SaveMGO's lobby as 9 (00/08 in the host list), as the game expects. For
players whose game fails to join that form (1059), `HOST_LIST_9_AS_8=1` in the server's
`/etc/mgo1host/server.env` (or, on the server, `mgo1host-agent host-list-9 8`; back: `host-list-9 9`) tells it as 8,
the older form (7 seats); every bot takes it at its next start. MGO1 Host has no row for it.
