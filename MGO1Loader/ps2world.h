// ps2world.h - runs the recompiled MGO1 ntmenu2 code inside MGS3 PC: PS2
// memory, the overlay and base images, and the bridges onto the engine.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ps2world {
struct Platform {
    void (*log)(const char* fmt, ...);
    // actors
    void* (*gvNewActor)(int size, unsigned long long level);
    void* (*gvSetNamedActor)(void* actor, unsigned long long level, void* act, void* die);
    unsigned long long actorLevel;
    // script: the host GCL's option lookup (sets the host cursor), value decoder,
    // and the host's parse cursor itself (PC: exe+0x1e2c2a0)
    char* (*gclGetOption)(char c);
    unsigned char* (*gclNextValue)(unsigned char* top, int* type, long long* val);
    unsigned char** gclCursor;
    // the host script's string resources (current script): offsets table (u32 LE,
    // masked 0x7FFFFFFF), string data and its end (PC: exe+0x1D75248 / 0x1D75250 / 0x1D75258)
    bool (*gclStrings)(const unsigned char** offsets, const char** data, const char** end);
    // layouts (MGS3 PC L2D, same library lineage as the overlay's copy)
    int (*l2dLoad)(unsigned long long* handle, unsigned int hash, int two, int mode, int prio, int one, int zero);
    void* (*l2dGetObj)(void* handle, unsigned int code);
    int (*l2dBusy)(void* handle);
    int (*l2dRunning)(void* handle, unsigned int action);
    int (*l2dEvoke)(void* handle, unsigned int action, long long param, int mode);
    int (*l2dStop)(void* handle, unsigned int action);
    // pad: held buttons of port 0/1 in libpad's layout, which MGS3 keeps (select 0x1,
    // L3 0x2, R3 0x4, start 0x8, up 0x10, right 0x20, down 0x40, left 0x80, L2 0x100,
    // R2 0x200, L1 0x400, R1 0x800, triangle 0x1000, circle 0x2000 (confirm),
    // cross 0x4000 (cancel), square 0x8000)
    uint32_t (*padHeld)(int port);
    // sound: MGO1 uses MGS3's sound ids (menu move 0x1a00b, ok 0x1a00c, back 0x1a00d ...)
    // text of a layout object: set (PC exe+0x748d80 = PS2 0x443658, mode 2) and the
    // variant used before a set (PC exe+0x748bd0 = PS2 0x443160)
    void (*l2dSetText)(void* handle, void* obj, const char* text);
    void (*l2dTextPrep)(void* handle, void* obj, const char* text);
    void (*seRequest)(unsigned int id);
    void (*musicRequest)(unsigned int id);
    // script services of the current page's script (the PC GCL in game)
    const char* (*gclStrRes)(int index);                        // string resource (menu texts)
    int (*gclExecProc)(int proc, const int* args, int argc);    // run a proc of the script
    int (*gclExecBlock)(const unsigned char* block, const int* args, int argc);   // run a block of the script
    int (*gclGetVar)(unsigned int code, int* value);            // 4-byte var code (varbuf / linkvarbuf)
    int (*gclSetVar)(unsigned int code, int value);
    // the engine's stage flags (PS2 0x2C5238, PC exe+0x1E21AA0: 1 load the next stage,
    // 0x10 its -n / -s variant, 0x40 the same stage again): PS2 code that reads or
    // writes 0x2C5238 reaches this word (null: plain PS2 memory)
    uint32_t* stageFlags;
    // the native build (NativeRun): called at each VBlank start with the frame number, on the
    // platform's thread while no PS2 thread runs (the harness's steps, the host's input)
    void (*frame)(long long frameNo);
    // optional, the analog part of a port (null: sticks centred, pressures from padHeld):
    // sticks RX RY LX LY (0x80 centred), pressures of the 12 pressure buttons in the order
    // of their held bits 4..15 (up right down left L2 R2 L1 R1 triangle circle cross square)
    bool (*padAnalog)(int port, uint8_t sticks[4], uint8_t pressures[12]);
    // optional, the USB keyboard (usbkb) as a HID boot report: modifiers and up to 6 keys held
    // (HID usage codes); null: the simulation's typed text
    bool (*keyboard)(uint8_t& modifiers, uint8_t keys[6]);
    // native build: MGO1's [UDP Port No.] while the game keeps its default (5730); 0 = the game's own
    int udpPort;
    // native build: an ini file whose [servers] section (docs/SERVERS.md) replaces SaveMGO's values;
    // null = <images>\servers.ini
    const char* serversIni;
    // native build: 60 runs the matches at 60 frames a second (the engine's full rate) where their
    // script asks for 30 (0x144190); 0 = the script's own rate, as on a PS2
    int matchFps;
    // native build, a match at 60 (docs/FPS60.md): 0 the PS2's own game at its pace (its 30 Hz
    // passes, a picture drawn after each one: the default); 1 the engine's full rate with the fair
    // rules (up to v123); 2 the full rate without rules (the simulation's measures). The player
    // build (MGO1_PLAYER_BUILD) takes 0 whatever is given, and m37Off false
    int fps60Kind;
    bool fpsRulesOff;             // unused since v124 (fps60Kind 2)
    // native build, a match at 60 on a PAL program (the European one: 25 ticks a second): true shows
    // 60 pictures a second (fields at 60 while a map runs, the game's time kept at PAL's); false its
    // own 50 fields, a picture between its ticks (docs/FPS60.md)
    bool palAt60;
    bool m37Off;                  // the M37's pump owed after a weapon change left as the PS2 has it (the simulation's measures)
    // native build, widescreen in a match (the lobby's pages stay 4:3); 0 or 1: as on a PS2.
    // wideX: the 3D view's horizontal scale in the picture (DG_SetCamera's aspect X): 0.75 a 16:9
    // view, 0.5714 a 21:9 one, in the same picture, which the front end shows that much wider;
    // wideY: its vertical scale instead (1.333 in 16:9: the PS2's view across, less above and
    // below); hudX: the width of DG's 2D elements (4:3 / the picture's shape), kept at their edge
    float wideX, wideY, hudX;
    // native build: the lobby's pages in the same wide picture (docs/NATIVE_BUILD.md): what spans the
    // picture (backgrounds, bands) across it, the rest at its 4:3 proportions in the middle; false: 4:3
    bool lobbyWide = false;
    // native build: the PS2 system language the game reads (OSD, libscf): 0 Japanese, 1 English, 2 French,
    // 3 Spanish, 4 German, 5 Italian; each version's scripts hold their own languages (docs/LANGUAGES.md).
    // -1: English
    int systemLanguage = -1;
    // native build: another version's files (a folder or a disc image) whose page scripts, the texts and
    // their glyphs, run in place of this version's (docs/LANGUAGES.md); null: this version's own
    const char* scriptsFrom = nullptr;
    // native build: another version's files whose voices (VOX_2.DAT) play in place of this version's
    // (docs/LANGUAGES.md); null: this version's own
    const char* voicesFrom = nullptr;
    // native build: the DNS server a PS2 set up for the servers asks (SaveMGO's own: 107.161.80.126),
    // before Windows' resolver; null: Windows' only. knownHosts: "name=address,..." answers kept for
    // when that server does not answer (SaveMGO: its server's answer for savemgo.com). docs/SERVERS.md
    const char* dnsServer = nullptr;
    const char* knownHosts = nullptr;
    // native build: the language the pages' scripts read (link variable 4: 1 English, 2 French, 3 German,
    // 4 Italian, 5 Spanish), written over the boot page's own choice (docs/LANGUAGES.md); 0: the program's.
    // The menus' titles and pictures follow it in all five on the North American and European discs,
    // the pages' texts are in two (English and the disc's other one)
    int gameLanguage = 0;
    // native build: the pages' English texts with gameLanguage (a language the disc has no texts in: its
    // titles and pictures, the English texts), not the disc's other set
    bool scriptEnglish = false;
    // native build: another version's folder (or disc image) whose pages' second text set replaces this
    // version's in its pages' scripts, this version's code kept: the European French texts on the North
    // American program (docs/LANGUAGES.md); null: the program's own texts
    const char* textsFrom = nullptr;
    // native build: the folder of the maps 13 and up (maps\ next to MGO1.exe: a folder a map, docs/EXTRA_MAPS.md);
    // null: none (MGO1_MAPS_DIR sets one too)
    const char* mapsDir = nullptr;
    // native build: VIF1, VU1, the GIF and the GS on a thread of their own beside the game thread (gs_thread.h,
    // docs/NATIVE_BUILD.md, The GS thread); false: on the game thread, as before v139
    bool gsThread = false;
    // with gsThread: 2 = the GIF and the GS on a second thread (VIF1 and VU1 on the first), 1 = one thread (v150)
    int gsStages = 1;
};
// native build: the widescreen factors of a running game (a window whose shape changes)
void SetWidescreen(float wideX, float wideY, float hudX, bool lobbyWide = false);
// native build: a match's map is loaded (not the lobby's pages): the picture is widescreen
bool InMatch();
// seconds since the last datagram received (the game's recvfrom; -1: none yet)
double SecondsSinceRecv();
// (v141 beta 12) the lobby's last message with an error code ("Failed to connect to host.|( 1059 )"), its frame (-1: none)
std::string LastErrorText(long long* frame);
// native build: the button prompts of the device used last (docs/BUTTON_PROMPTS.md): family 0 PlayStation, 1 Xbox,
// 2 Switch, 3 keyboard; for the keyboard each PS2 button's two keys (short names) in the menus and in a match, for a
// pad each PS2 button's pad button (config.ini [pad] names)
void SetButtonPrompts(int family, const char* const menu[16][2], const char* const match[16][2], const char* const pad[16]);
// native build: DG's frames drawn so far (the game's own frame clock: 30 or 60 a second of game time)
long long DgFrames();
long long GameTicks();          // the ticks of a map at half rate (its 30 or PAL's 25 a second) so far
long long DgPictures();         // the pictures DG drew so far (its frame waits; exact 60's pictures between too)
double FieldRate();             // the fields a second the game is paced at now (60, PAL's 50 or its 60 in a map, NTSC's 59.94)
// native build, a crash report (MGO1.exe, docs/LOGS.md): the PS2 world's state in text (its frame, the page, the
// round's map, the function running and the calls before it); the characters written (at most n - 1)
int CrashState(char* out, int n);
// a match's rate from now on (the in-game menu): 60, or 0 for the map's own; applied at the next
// VBlank when the map asks for 30 frames a second
void SetMatchFps(int fps);
// tests: the PS2's time k times slower than the machine's from now on (every instance of a simulation
// at one pace)
void SetTimeScale(double k);
// tests: the network's clock (the session engine's timers, its send budget) k times slower from now on
void SetNetTimeScale(double k);
// the PS2's time in ms (SetTimeScale applies)
unsigned long long PsTimeMs();
// native build: the game's sound (the IOP's driver on the software SPU2), 48 kHz stereo frames
// added into out; its state for the log
void SoundMix(int16_t* out, int frames, int waitUs = -1);   // waitUs: iop::Mix's
// the title's Start sound (MGO1's system effect 0x1A00C, a shot) rendered from the disc's
// STAGE_N.DAT (its host file, the file's start and size in it): 48 kHz stereo frames written
// to out, 0 when its sound bank is not found (iop.h RenderStartSound)
int TitleStartSound(const char* path, long long base, long long size, int16_t* out, int maxFrames);
// a menu effect rendered the same way: 0 the cursor move, 1 OK (the Start sound), 2 cancel (iop.h RenderMenuSound)
int TitleMenuSound(const char* path, long long base, long long size, int which, int16_t* out, int maxFrames);
std::string SoundStats();
// the processor time a VBlank of the PS2 threads, the kernel loop, the GS thread and the process since the last call
// (MGO1.exe's log every 600 VBlanks; perf.h); empty at the first call
std::string CpuStats(long long vblanks);
// native build, the voice chat (docs/VOICE.md): the maps' NewVoiceChat plays a USB headset; the PC's
// microphone makes one: attached while a microphone is there (the game then opens it), its samples as
// they come (8 kHz, 16 bits, mono), the game's voice taken to be played (8 kHz; returns the samples);
// the state: 1 attached, 2 open by the game, 4 recording (this player talking), 8 playing
void VoiceAttach(bool on);
void VoiceIn(const int16_t* pcm, int n);
int VoiceOut(int16_t* pcm, int max);
int VoiceState();
// talking, the game plays his own voice back in the headset (with the start beep): heard or not (the beep is)
void VoiceSidetone(bool on);
// the voice between PCs (Opus on the game's UDP socket): on or off; an Opus packet of 20 ms while this
// player talks (newTalk: the first of a talk); a packet received (returns its bytes, 0 none: the talker's
// player id, his talk's number, the packet's number); the player ids whose voice the game plays now (a
// bit each: their Opus is played only then, and their LPC-10 is left out while it comes)
void VoiceNetEnable(bool on);
void VoiceNetSend(const uint8_t* opus, int n, bool newTalk);
int VoiceNetRecv(uint8_t* opus, int max, int* talker, int* talk, int* seq);
uint32_t VoiceNetHeard();
// the pages this build recompiled, by the FNV-1a hash of their first 64 bytes, and the base
// image it was recompiled from (gen/mgo1_page_heads.cpp, tools/page_heads.py)
struct PageHead { const char* version; const char* page; uint64_t head; };
// a version's base image: its hash and size (the word at +4 left out), where its pages load, the
// hashes of its 4 KB blocks (a patched copy is known by most of them: disc.cpp MatchProgram)
struct VersionBase { const char* version; uint64_t hash; uint32_t size; uint32_t pageBase; const uint64_t* blocks; uint32_t blockCount; };
extern const PageHead kPageHeads[];
extern const int kPageHeadCount;
extern const VersionBase kVersionBases[];           // each version this build runs (docs/VERSIONS.md)
extern const int kVersionBaseCount;
struct UsStub { int number; uint32_t addr; };
extern const UsStub kUsStubs[];                     // the US program's kernel stubs by their syscall number
extern const int kUsStubCount;
extern const uint64_t kBaseImageHash;
extern const uint32_t kBaseImageSize;
// the native build: MGO1 boots from its own entry (crt0 0x100024 -> main) with every function
// recompiled and runs `frames` VBlanks; the platform is the PS2's system only (kernel, VBlank,
// IOP modules): the PC engine services above are not used (only log, padHeld and frame)
int NativeRun(const Platform& p, const char* dir, long long frames);
// loads <dir>\ntmenu2_046341.bin (0x2DA680) and <dir>\mgs3_n_base.bin (0x100000)
bool Init(const Platform& p, const char* dir);
// the MGO1 lobby class [ccbefe]: runs the recompiled NewOnlineMenu constructor
// with the class arguments; returns the PC actor that carries it (null = failed)
void* NewOnlineMenu(int name, int map);
bool Ready();
void SimType(const char* text);                      // simulation: type this on the USB keyboard from the next frame
bool SimTyping();                                    // simulation: keys typed with SimType still to come
// the overlay's own command / class table ({code, handler} pairs after its header)
int OverlayTable(uint32_t* codes, uint32_t* fns, int max);
// runs one of them with the class arguments (name, map); returns the PC actor it
// created when it is a class, else its int result
void* CallCode(uint32_t fn, int name, int map);
// stage change: the page overlay (ntmenu2) has left PS2 memory; calls into it are refused
void OverlayUnloaded(const char* nextPage);
// the page overlay is loaded again (its image read again, its bss cleared)
void OverlayLoaded(const char* page);
// another page's overlay (a map): image read at 0x2DA680, bss cleared, its code selected
bool LoadPageOverlay(const char* page);
// the page data of a stage change, as the PS2 stage manager loads it (0x104898): the
// resident page first when it changes, then the stage page, each after the memory reset;
// read through the base's own loader from the archives (STAGE_N.DAT). False when this
// install has no archives (the page's code image is then all there is)
bool LoadPageData(const char* stage, const char* resident);
// the same stage again (the engine's flags 0x40): the PS2 stage manager's reset and
// post-load steps without a load
bool StageRestart();
// logs the live PS2 actors and threads, flagging those that run page-overlay code
void Report(const char* why);
// simulation helpers: the position (work + 0x110) of the local player's NetworkDummyPlayer
// (which = 0) or of the which-th remote NetworkPlayer (1..); false when there is none
bool PlayerPos(int which, float xyz[3]);
int TsnePoints(float out[4][3]);          // tests: TEAM SNEAKING's round: the GA-KO, the KEROTAN, the two goals (x y z); how many
int TsneRole();                            // tests: in a TEAM SNEAKING round 1 this player sneaks, 0 he defends; -1 none
void TsneTestPicture(const char* what, int frames);   // tests: a TEAM SNEAKING picture (SIM_TSNE_PNG moment) that many frames on
uint32_t TsneTestPad();                   // tests: the pad bits SIM_TSNE_PAD holds now on pad 0 (~0: none)
int MenuScreen();                          // tests: the match's menu screen shown (NewHudPause +0x326: 0 none, 4 the choice, 6 START); -1 none
int BcBases(float out[5][3]);              // tests: BASE CAPTURE's round: its bases (x y z); how many (0: none)
uint32_t BcRoundId();                      // tests: BASE CAPTURE's round: a number of its own (0: none)
float BcTimeLeft(int* result);             // tests: BASE CAPTURE's round: its time left in seconds (-1: none), its result byte
int BcBaseStates(float pos[5][3], int owner[5], int contested[5]);   // the PLAYABLE DOLL: BASE CAPTURE's bases, their owners (-1 none, 0 red, 1 blue), contested; how many
bool MissionObjectPos(int k, float xyz[3], uint32_t* holder);   // tests: mission object k's place and holder (0: none)
int VisitPoint(int k, float xyz[3], uint32_t* code);            // tests: SIM_VISIT's point k (the stage's, by code); how many
void SetGameLanguage(int lang);            // tests: the scripts' language (link variable 4) from now on (0: the platform's)
bool ItemBoxPos(float xyz[3], int code);  // tests: the place of an item box of that code (0: the last made)
// (v150-host beta 22, pickup) the drops' records of this program's game (US n003a's manager [0x2E5268]: 16 records of 0x60 from
// +0xD0): the round's special items (the host's numbers 0x51 RPG-7, 0x52 M63, 0x53 flamethrower, 0x54 thermal goggles) and the
// weapons and items players dropped. number -1: an empty record; kind 3 a weapon, 1 an item, 0 gone (taken), 2 coming back;
// count: its rounds (an item's count); state: 0 lying, 1 claimed by this program's player, 2 his, 3 gone; box: its box is in
// the world now (the place pos, the box's own); flags: the record's word (kind << 10 | code, 0x8000 the box still falling)
struct DropRec { int number = -1, kind = 0, code = 0, count = 0, state = 0; unsigned flags = 0; float pos[3] = {0, 0, 0}; bool box = false; float reach = 0; };
int DropRecords(DropRec* out, int max);
// (v150-host beta 25, food) the food of the map as this program's game holds it: the mushroom man's entries (NewOnlineMushRoomMan,
// US n003a 0x3A8280: 16 entries of 0x50 bytes from +0xC0, the same on every page). The stage script places 6 to 10 a disc map,
// each a food box lying at its point from the round's start. number: the entry's (0x100 + n); state: 1 on its wall, 2 lying,
// 3 taken (back 6000 time units later, 20 s on the host's clock); code: the food (0x5B GOLOVA, 0x56 RUSSIAN GLOWCAP); at: the
// entry's point (its records carry it); box: its box is in the world now, at pos (the box's place, +0x220) with its reach
// (+0xD0); gain: the stun gauge a player's game gives back when he eats it (the engine's 0x305F90: 30000 x its class's share,
// from its descriptor's flags); timer: the host's time units left before it is back
struct FoodRec { int number = -1, state = 0, code = 0, gain = 0, timer = 0; float at[3] = {0, 0, 0}, pos[3] = {0, 0, 0}; bool box = false; float reach = 0; };
int FoodRecords(FoodRec* out, int max);
int LocalWeaponsMade();                   // tests: the weapons made for this program's player (on a map with added weapons)
int BoxesTaken();                         // tests: the weapons of added weapons' bases this player took from boxes
// the same players' actor work (0 when there is none)
uint32_t PlayerObject(int which);
// the work of the nth live actor of that name (0: none)
uint32_t ActorByName(const char* name, int nth = 0);
// the PLAYABLE DOLL (doll.h): the page loaded (n001a ... for a map; empty before the first), one of a
// stage's resources from the disc's STAGE_N.DAT (type: 0x0C geom), the first view's camera matrix
std::string CurrentPage();
std::string LocalPlayerName();                     // the player name the log-in gave (empty before it)
// (extra: the extra map 13 .. whose stage it is, its own file then; ExtraMapNow: the one the stage loaded last is, 0 none)
bool StageResource(const std::string& stage, uint8_t type, std::vector<uint8_t>& out, std::string& err, int extra = 0);
int ExtraMapNow();
// (v150-host beta 22, sync) the stage now as ONE pair: its page (n001a ...) and the extra map served on it (13 .., 0 none),
// both of the stage asked last; false on a page that is no map's and while a stage of another page loads (one of the same page:
// at its request) (stagekey.h)
bool StageNow(std::string* page, int* extra);
bool ViewMatrix(float m[16]);
// ((v141 beta 9) the last "pace:" line (a minute's VBlanks a second, the rate due, the process's share of a core): its number
// (1, 2 ...), false before the first)
bool LastPace(int* lines, double* rate, double* due, double* cpu);
// every live actor of the game, by name (the doll's census of items and objectives)
void ForEachActor(void (*fn)(uint32_t work, const std::string& name, void* ctx), void* ctx);
// a player of the game by its id (0..15): its team (its variable set +1: 0 red, 1 blue, 0xFF none yet,
// 0xFE the dedicated host) and its name; false when the id has no variable set; the room's variables
// (set 0: +0 the phase, +0x36 the rotation's entry, +0x3C the seconds left) as bytes
bool PlayerInfo(int id, int* team, std::string* name);
// (v141 beta 6) the game network's session table (0x28B5F4, 16 entries of 0x5C: +0 the id, +2 the state, +8 the flags: 1 the
// game's host, 2 this machine's own player): the players connected now. A slot's variable set keeps its name and team after
// its player left; this table does not. False when no session is up (its count 0). name: the entry's own (+0x28, the name the
// game's messages show; empty when it holds no printable name)
struct SessionEntry { int id; uint32_t flags; uint16_t state; std::string name; };
bool SessionPlayers(std::vector<SessionEntry>& out);
uint32_t PlayerUserId(int id);                     // the TX55 user id the game holds for a player (its variable set +0xA0; 0 none)
bool RoomVariables(uint8_t* out, size_t n);
bool VariableSet(int set, uint8_t* out, size_t n);   // the game network's variable set (0 the room, 1 + id a player)
// the game's messages to its actors (0x1220E0: actor, code, argument), handed to the doll too
void SetMessageHook(void (*fn)(uint32_t actor, uint32_t code, uint32_t arg));
// the PLAYABLE DOLL's radio: the calls its radio reads from the network (0x174B20 on the radio's channel,
// its work + 0x40: a word {code << 24 | voice << 16 | player id << 8 | team} each), kept until taken
void SetRadioChannel(uint32_t channel);            // 0: none
size_t TakeRadioCalls(uint32_t* out, size_t max);  // the oldest first
// the log-in data (the account, its password, the game's password) masked in a text, or scrubbed
// out of a memory dump: nothing sent to troubleshoot ever holds them
std::string RedactSecrets(const std::string& s);
void ScrubSecrets(uint8_t* p, size_t n);
// native build: this program's player's weapon for the PC's READY WEAPON (native/ready_weapon.h): raised or coming
// down (his flag 0x2E), a gun whose magazine can take rounds (the game's rule, US n003a 0x4079C8), a gun in his hands
// (weapons 5 to 17, the gun's states); -1 unknown
void LocalWeapon(int& aimed, int& reloadable, int& gun);
// SIM_LOBBY_WATCH (tests): the handler an actor's action last ran for its tick (message 0x80), 0 if none
uint32_t ActionTick(uint32_t actor);
// one of MGO1's software keyboards is open (a text field reads the USB keyboard)
bool KeyboardTextEntry();
// the USB keyboard's keys that make MGO1's text field get ch now (a character the user typed), by the program's own
// table, its setting and its lights (MGO1Loader/usbkb.h): one key, or a dead key and a key; false: it cannot be typed
bool KeyboardStrokes(uint32_t ch, uint8_t usage[2], uint8_t mods[2], int* count);
// the addresses (IPv4, network order) the game sent UDP datagrams to in the last 3 s (STUN and
// SSDP left out): its session's peers; returns their number
int SessionPeers(uint32_t* ips, int max);
}
