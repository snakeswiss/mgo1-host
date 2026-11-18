// Runs the plugin's native ntmenu2 path (ps2world + recompiled code) against
// a mock of the MGS3 PC engine: actors ticked by hand, script options, layouts
// as host memory. Checks that MGO1's own writes reach the host layout nodes.
#include "../native/voice_dsp.h"
#include "../native/voice_opus.h"
#include "../native/ready_weapon.h"
#include "../native/version.h"                       // (build 1.00) MGO1_BUILD: the first line of the host's and a doll's log
#include <memory>
#include "ps2world.h"
#include "doll.h"
#include "gs_gpu.h"
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <x86intrin.h>
namespace ps2 { extern uint8_t* gMem; extern bool gCallTrace; extern bool gCallTraceAll; extern uint32_t gTreeRoot; extern int gTreeLines; extern bool gTreeArmed;
                extern void (*gCallHook)(uint32_t addr); void GsSetHudScale(float scale); bool GsWritePng(const char* path); uint64_t GsDisplayFlips(); extern bool gGsDraw; void GsqArm(const char* path, int afterVSyncs, int vsyncs); }
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "gcl_vm.h"
// the real ntmenu2 script, run by a transcription of the base's GCL interpreter
// (gcl_vm.h) as the PC engine runs it in game; SIM_SCRIPT_MAIN=0 keeps the older
// hand-written stand-in (root options parsed from the bytes, proc 11 and 13 by hand)
static gclvm::Vm gVm;
static bool gVmOn = false;
struct LoadRequest { std::string stage, resident; int frame = -1; };
static LoadRequest gLoad;                             // the next stage's name and resident page (the engine's slots)
static uint32_t gStageFlags = 0;                      // the engine's stage flags (PC exe+0x1E21AA0, PS2 0x2C5238)
static std::string gPage = "ntmenu2";               // the stage page whose script and overlay run
// SIM_FIRE_CALLS: the functions called in the frame of a shot (and in the frame 30 before, to compare)
static std::map<uint32_t, unsigned> gFrameCalls;
static void FrameCallsHook(uint32_t addr) { ++gFrameCalls[addr]; }
static int gFrameCallsLeft = 0;                    // frames still to record (the hook runs while > 0)
static const char* gFrameCallsTag = "";
static std::string gDir = ".";                      // images and scripts (<page>.gcx, pc_codes.txt)
#include <set>
static std::set<uint32_t> gPcCodes;                 // the classes and commands the PC engine has (tools/pc_tables.py)
// SIM_STEPS="text=buttons@delay|...": each step waits for a log line holding its text
// (an empty text waits for nothing), then presses its buttons `delay` frames later
struct Step { std::string text; unsigned buttons; int delay; std::string kb; int hold = 4;   // hold: frames held (=mask/frames@delay)
              int pressure = -1;                              // =maskpNN: the held buttons at pressure NN (hex; full otherwise)
              int stick[4] = {-1, -1, -1, -1};                // =stick:RX,RY,LX,LY (hex, 80 centred): the sticks held
              struct Chord { unsigned buttons; int from, len; int pressure = -1; }; std::vector<Chord> chords;   // +mask[pNN]:from:len inside the hold
              int autoMode = 0;                               // =chase/frames, =cqc/frames, =duel/frames, =duelcqc/frames, =ladder/frames, =watchladder/frames, =play/frames, =playcqc/frames, =rail/frames, =watchplayer/frames, =pickup/frames, =spawn/frames: the autopilot (1 fire, 2 CQC, 3 teleport then fire, 4 teleport then CQC, 5 a ladder climbed, 6 a climb watched, 7 and 8 a whole game, 9 a railing hung from, 10 a player watched, 11 his drop walked onto, 12 the round's screens answered until he is alive: any version's texts)
              int tap = 0;                                    // =masktN: the held buttons tapped, N 1/30 s pressed then N released (game time)
              int lang = 0;                                   // =lang:N: the scripts' language set to N (1 English .. 5 Spanish)
              std::string env;                                // =setenv:NAME:VALUE: an environment variable set (a switch read while running)
              float ladder[5] = {}; bool hasLadder = false; };   // =ladder:x,y,z,turn,top/frames: SIM_LADDER for this step
static std::vector<Step> gSteps;
static float gStepLadder[5] = {}; static bool gHaveStepLadder = false;   // the running step's =ladder:... (else SIM_LADDER)
static size_t gStep = 0;
static int gStepSeen = -1, gPressFrom = -1, gPressButtons = 0, gPressHold = 4;
static std::vector<Step::Chord> gPressChords;
static int gPressPressure = -1;
static int gPressStick[4] = {-1, -1, -1, -1};
static int gPressAuto = 0;
static int gPressTap = 0;                          // the tap length of the step being pressed (1/30 s), 0: held
static long long gPressDg = 0;                     // DG's frame count when it began
static int gFrameNow = 0;
static std::vector<std::pair<int, std::string>> gRecent;   // log lines since the current press began
static int gSeenTimer = -100000, gSeenRound = -100000;     // =play: the weapon choice's timer, a round's screens
// SIM_JOIN_RETRY: a join the host did not complete (the game cancels it with the gate: TX55 4322, its
// message "Failed to connect to host"): the message dismissed, the steps again from the game list's
// (the step waiting for "Please select a game to join" or for the game's details, TX55 4313), 5 times at most
// (SIM_JOIN_RETRY=<n> above 1: n times)
static int gJoinRetries = 0;
static void JoinLockRelease(const char* why);
static void JoinRetry() {
    JoinLockRelease("the join failed");
    size_t j = std::min(gStep, gSteps.size() - 1);
    auto list = [](const Step& x) { return x.text.rfind("Please select a game to join", 0) == 0 || x.text == "tx55 4313"; };
    while (j > 0 && !list(gSteps[j])) --j;
    static const int most = getenv("SIM_JOIN_RETRY") && atoi(getenv("SIM_JOIN_RETRY")) > 1 ? atoi(getenv("SIM_JOIN_RETRY")) : 5;   // SIM_JOIN_RETRY=<n>: n tries
    if (!list(gSteps[j]) || gJoinRetries >= most) return;
    ++gJoinRetries;
    Step d; d.text = ""; d.buttons = 0x2000; d.delay = 120;       // the message's OK
    gSteps.insert(gSteps.begin() + j, d);
    gStep = j; gPressFrom = -1; gPressAuto = 0; gStepSeen = gFrameNow; gRecent.clear();
    printf("steps: the join failed, again from step %zu (retry %d)\n", j, gJoinRetries);
}
static int gBcPageAt = -100000, gBcChoiceAt = -100000;   // =bc: the GAME TYPE page drawn, the weapon choice made (log lines)
static int gTsneHitAt = -100000, gTsneHitBy = -1;          // SIM_TSNE_DUEL ,1: the last hit on a sneaker made here, its attacker
static void StepsSee(const char* line) {
    static const bool retry = getenv("SIM_JOIN_RETRY") != nullptr;
    if (strstr(line, "basecap: the GAME TYPE page drawn")) gBcPageAt = gFrameNow;
    else if (strstr(line, "added weapons: the choice's rows made")) gBcChoiceAt = gFrameNow;
    if (const char* h = strstr(line, "tsne: damage on sneaker ")) {
        const char* at = strstr(h, ": attacker ");
        if (at && strstr(h, ", from ")) { gTsneHitAt = gFrameNow; gTsneHitBy = atoi(at + 11); }
    }
    if (retry && strstr(line, "send(") && strstr(line, "tx55 4322 ") && !gSteps.empty()) JoinRetry();
    if (strstr(line, "\"TIME : ") || strstr(line, "\"TEMPS : ") || strstr(line, "\"TIEMPO : ") || strstr(line, "\"ZEIT : ") ||
        strstr(line, "\"TEMPO : ")) gSeenTimer = gFrameNow;          // the weapon choice's timer (English, French, Spanish ...)
    else if (strstr(line, ": text 00")) gSeenRound = gFrameNow;        // a text of the match (SIM_TEXT_FNS): its screens
    if (gPressFrom >= 0) { if (gRecent.size() < 100000) gRecent.push_back({gFrameNow, line}); return; }   // a long autopilot step's lines kept
    if (gStep < gSteps.size() && gStepSeen < 0 && !gSteps[gStep].text.empty() && strstr(line, gSteps[gStep].text.c_str()))
        gStepSeen = gFrameNow;
}
// frame watches: argv[4] "addr:size,..." and SIM_ACTOR_WATCH "Name+off:size,..." (a field of
// an actor's work, watched from the moment the actor is made); changes logged once a frame
static std::vector<std::pair<uint32_t, int>> gWatch;
static std::vector<uint32_t> gWatchPrev;
static void ActorWatchSee(const char* line) {
    struct Spec { std::string name; uint32_t off; int size; };
    static std::vector<Spec> spec;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        for (const char* q = getenv("SIM_ACTOR_WATCH"); q && *q;) {
            const char* plus = strchr(q, '+'); if (!plus) break;
            Spec s; s.name.assign(q, plus - q); char* e = nullptr; s.off = strtoul(plus + 1, &e, 16); s.size = 4;
            if (*e == ':') s.size = (int)strtol(e + 1, &e, 10);
            spec.push_back(s);
            const char* n = strchr(e, ','); if (!n) break; q = n + 1;
        }
    }
    if (spec.empty()) return;
    // "GV_NewActor <name> ... -> <work>" (the MGS3 engine's path), "GV_SetNamedActor <name> work <work>" (native)
    const char* p = strstr(line, "GV_NewActor ");
    const char* q = p ? nullptr : strstr(line, "GV_SetNamedActor ");
    if (!p && !q) return;
    p = p ? p + 12 : q + 17;
    for (auto& s : spec)
        if (!strncmp(p, s.name.c_str(), s.name.size()) && p[s.name.size()] == ' ') {
            const char* at = q ? strstr(p, " work ") : strstr(p, "-> ");
            if (!at) continue;
            uint32_t work = strtoul(at + (q ? 6 : 3), nullptr, 16);
            gWatch.push_back({work + s.off, s.size}); gWatchPrev.push_back(0xdeadbeef);
            printf("watch: %s work %08x + %#x (%08x)\n", s.name.c_str(), work, s.off, work + s.off);
        }
}
// SIM_ARG_WATCH="41d218:a1+dc:2,...": a field of what an argument of a function points at, watched
// from the first call the platform logs (the address must be in TRACE_ADDRS too)
static void ArgWatchSee(const char* line) {
    struct Spec { uint32_t fn; int reg; uint32_t off; int size; bool done; };
    static std::vector<Spec> spec;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        for (const char* q = getenv("SIM_ARG_WATCH"); q && *q;) {
            Spec s{}; char* e = nullptr;
            s.fn = strtoul(q, &e, 16);
            if (!e || *e != ':' || e[1] != 'a') break;
            s.reg = e[2] - '0';
            if (e[3] != '+') break;
            s.off = strtoul(e + 4, &e, 16); s.size = 4;
            if (*e == ':') s.size = (int)strtol(e + 1, &e, 10);
            spec.push_back(s);
            const char* n = strchr(e, ','); if (!n) break; q = n + 1;
        }
    }
    if (spec.empty()) return;
    const char* p = strstr(line, "trace call "); if (!p) return;
    const uint32_t fn = strtoul(p + 11, nullptr, 16);
    for (auto& s : spec) {
        if (s.done || s.fn != fn) continue;
        char key[8]; snprintf(key, sizeof key, "(a%d ", s.reg);
        const char* r = strstr(p, key);
        if (!r && s.reg) { snprintf(key, sizeof key, " a%d ", s.reg); r = strstr(p, key); }
        if (!r) continue;
        const uint32_t base = strtoul(r + strlen(key), nullptr, 16);
        s.done = true;
        gWatch.push_back({base + s.off, s.size}); gWatchPrev.push_back(0xdeadbeef);
        printf("watch: %08x a%d %08x + %#x (%08x)\n", fn, s.reg, base, s.off, base + s.off);
    }
}
// SIM_SHOTLOG=<shot function, hex> (tests, the added weapons against their bases, docs/CHARACTERS.md): each shot of
// the local player logged the frame after it (the function traced too: TRACE_ADDRS; US n003a 0x416EC0, n022a
// 0x40DC88), the aim the 90 frames after a shot, each bullet made (two snapshots of its work), the held weapon's
// rounds when they change; =setenv:SIM_WKILL:1: the local player's life set to 0 once he is alive
struct ShotPending { int frame = -1; uint32_t pl = 0, wp = 0; int16_t de = 0, of0 = 0, of1 = 0; float pt[3] = {}; };
static std::vector<ShotPending> gShotPending;
static std::vector<std::pair<uint32_t, int>> gShotBullets;   // a bullet's work, the frames seen
static uint32_t gShotPl = 0, gShotWp = 0;
static int gShotAfter = 0;
static const char* ShotFn() { static const char* e = getenv("SIM_SHOTLOG"); return e; }
static void ShotLogSee(const char* line) {
    const char* fn = ShotFn();
    if (!fn) return;
    char key[32]; snprintf(key, sizeof key, "trace call %08x ", (unsigned)strtoul(fn, nullptr, 16));
    if (const char* p = strstr(line, key)) {
        const char* a0 = strstr(p, "(a0 "); const char* a1 = strstr(p, " a1 ");
        if (!a0 || !a1) return;
        ShotPending s; s.frame = gFrameNow;
        s.pl = strtoul(a0 + 4, nullptr, 16) & 0x01ffffff; s.wp = strtoul(a1 + 4, nullptr, 16) & 0x01ffffff;
        if (s.pl < 0x100000 || s.pl > 0x01fff000 || s.wp < 0x100000 || s.wp > 0x01fff000) return;
        memcpy(&s.de, ps2::gMem + s.wp + 0xde, 2);
        memcpy(&s.of0, ps2::gMem + s.pl + 0x510, 2); memcpy(&s.of1, ps2::gMem + s.pl + 0x512, 2);
        memcpy(s.pt, ps2::gMem + s.pl + 0x430, 12);
        gShotPending.push_back(s);
        return;
    }
    const char* n = strstr(line, "GV_SetNamedActor ");
    if (n && (!strncmp(n + 17, "NewBullet ", 10) || !strncmp(n + 17, "NewSniperBullet ", 16) || !strncmp(n + 17, "NewBulletRico ", 14))) {
        if (const char* w = strstr(n, " work ")) gShotBullets.push_back({(uint32_t)strtoul(w + 6, nullptr, 16) & 0x01ffffff, 0});
    }
}
static void Log(const char* fmt, ...) {
    char b[2048]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b) - 1, fmt, ap); va_end(ap);
    {   // one write a line (stdout is unbuffered for the crashes: printf wrote it a character at a time, v141 beta 4)
        size_t n = strnlen(b, sizeof(b) - 2);
        b[n] = '\n'; fwrite(b, 1, n + 1, stdout); b[n] = 0;
    }
    StepsSee(b);
    ActorWatchSee(b);
    ArgWatchSee(b);
    ShotLogSee(b);
}
// SIM_WATCH_EXPR="*(*0x28d528+8):4,...": a watched value behind pointers, read each
// frame: '*' reads a u32 at the address, '+' adds, parentheses group; ':n' = bytes shown
namespace wexpr {
static uint32_t Rd(uint32_t a) { uint32_t v = 0; if ((a & 0x01ffffff) + 4 <= 0x02000000) memcpy(&v, ps2::gMem + (a & 0x01ffffff), 4); return v; }
static uint32_t Expr(const char*& p);
static uint32_t Term(const char*& p) {
    while (*p == ' ') ++p;
    if (*p == '*') { ++p; return Rd(Term(p)); }
    if (*p == '(') { ++p; uint32_t v = Expr(p); if (*p == ')') ++p; return v; }
    char* e = nullptr; uint32_t v = strtoul(p, &e, 0); p = e; return v;
}
static uint32_t Expr(const char*& p) { uint32_t v = Term(p); while (*p == '+') { ++p; v += Term(p); } return v; }
struct W { std::string text; int size; uint64_t last; };
static std::vector<W> gList;
static void Setup() {
    const char* q = getenv("SIM_WATCH_EXPR");
    while (q && *q) {
        const char* end = strchr(q, ','); std::string one = end ? std::string(q, end - q) : std::string(q);
        W w; w.size = 4; w.last = ~0ull;
        size_t colon = one.rfind(':');
        if (colon != std::string::npos) { w.size = atoi(one.c_str() + colon + 1); one.resize(colon); }
        if (w.size < 1 || w.size > 8) w.size = 4;
        w.text = one; gList.push_back(w);
        if (!end) break; q = end + 1;
    }
}
static void Frame(int f) {
    for (auto& w : gList) {
        const char* p = w.text.c_str();
        uint32_t a = Expr(p) & 0x01ffffff;
        uint64_t v = 0; if (a + w.size <= 0x02000000) memcpy(&v, ps2::gMem + a, w.size);
        if (v != w.last) { printf("watch frame %d: %s (%08x) = %llx\n", f, w.text.c_str(), a, (unsigned long long)v); w.last = v; }
    }
}
}
// the engine's actors: twelve lists run in order each frame (list = level's top byte - 1,
// 6 when it is 0), each list in the order its actors were added (PC exe+0x10F920)
struct MockActor { void (*act)(void*); int (*die)(void*); bool live; unsigned long long level; bool dying = false; };
static std::vector<std::pair<void*, MockActor>> actors;
static void* NewActor(int size, unsigned long long level) { void* p = calloc(1, size); actors.push_back({p, {nullptr, nullptr, true, level, false}}); return p; }
static void* SetNamed(void* a, unsigned long long level, void* act, void* die) {
    for (auto& x : actors) if (x.first == a) { x.second.act = (void (*)(void*))act; x.second.die = (int (*)(void*))die; x.second.level = level; }
    return a;
}
static int ListOf(unsigned long long level) { int top = (int)(level >> 56); return top ? top - 1 : 6; }
static void RunActors() {
    for (int list = 0; list < 12; ++list)
        for (size_t k = 0; k < actors.size(); ++k)
            if (actors[k].second.live && actors[k].second.act && ListOf(actors[k].second.level) == list) actors[k].second.act(actors[k].first);
}
// the stage change of the engine (PS2 0x1043D0 -> 0x122568(0), PC exe+0x980E6 ->
// exe+0x10E9B0(0)): every actor whose persistence class (bits 20-21 of the level's
// low word) is 0 goes to the death list; the others (the session daemons, the platform's
// frame actors) stay. Lists 0 to 11 in turn, each in its order, as 0x122568 walks them:
// the items free their info nodes (0x10B728) before GM_InfoSystemStart (list 10) frees the
// node chunks (0x10B530); the other way round every node is freed twice.
static int PurgeStage() {
    int n = 0;
    for (int list = 0; list < 12; ++list)
        for (size_t k = 0; k < actors.size(); ++k) {
            if (!actors[k].second.live || ListOf(actors[k].second.level) != list) continue;
            if ((((unsigned)actors[k].second.level >> 20) & 3) > 0) continue;
            actors[k].second.live = false;
            actors[k].second.dying = true;
            ++n;
        }
    return n;
}
// the death list at the end of the engine's frame (PS2 0x122218 from 0x122768): each dying
// actor's die; one answering 1 with the level flag 0x8000 stays for the next frame.
// Returns how many stay.
static int ProcessDeaths() {
    int left = 0;
    for (int list = 0; list < 12; ++list)
        for (size_t k = 0; k < actors.size(); ++k) {          // a die may add actors: by index
            if (!actors[k].second.dying || ListOf(actors[k].second.level) != list) continue;
            const int r = actors[k].second.die ? actors[k].second.die(actors[k].first) : 0;
            if (r == 1 && ((unsigned)actors[k].second.level & 0x8000)) { ++left; continue; }
            actors[k].second.dying = false;
        }
    return left;
}
// script options as PC-style streams: [type byte][8 byte value]..., 0 ends
static std::map<char, std::vector<unsigned char>> opts;
static void AddStr(char o, const char* s) { auto& v = opts[o]; v.push_back(7); long long p = (long long)strdup(s); v.insert(v.end(), (unsigned char*)&p, (unsigned char*)&p + 8); }
static void AddInt(char o, long long n) { auto& v = opts[o]; v.push_back(9); v.insert(v.end(), (unsigned char*)&n, (unsigned char*)&n + 8); }
static unsigned char* gCursor = nullptr;       // the mock host's parse cursor
static char* GetOption(char c) {
    if (gVmOn && !gVm.optStarts.empty()) return (char*)gVm.GetOption(c);
    auto it = opts.find(c);
    if (it == opts.end()) return nullptr;
    if (it->second.empty() || it->second.back() != 0) it->second.push_back(0);
    gCursor = it->second.data();
    return (char*)it->second.data();
}
// mock bytecode: [type][8-byte value], except string resources, encoded as the real
// GCL does: [0x0E][u16 index] (the PS2 decoder returns them as type 7, the string)
static const char* StrRes(int i);
static unsigned char* NextValue(unsigned char* top, int* type, long long* val) {
    if (gVmOn && gVm.s.Holds(top)) { int t = 0; int64_t v = 0; const uint8_t* n = gVm.NextValue(top, &t, &v); *type = t; *val = v; return (unsigned char*)n; }
    if (top[0] == 0x0e) { int idx = top[1] | (top[2] << 8); *type = 7; *val = (long long)(intptr_t)StrRes(idx); return top + 3; }
    *type = top[0]; memcpy(val, top + 1, 8); return top + 9;
}
// layouts
static std::map<void*, unsigned int> layoutOf;
static std::map<std::pair<void*, unsigned>, unsigned char*> nodes;
static int loads = 0, evokes = 0;
static int Load(unsigned long long* h, unsigned hash, int, int, int, int, int) { void* p = calloc(1, 0x200); layoutOf[p] = hash; *h = (unsigned long long)p; ++loads; return 0; }
static void* GetObj(void* h, unsigned code) {
    auto& n = nodes[{h, code}];
    if (!n) { n = (unsigned char*)calloc(1, 0x80); memcpy(n + 0xc, &code, 4); unsigned f = 0x400000; memcpy(n + 0x10, &f, 4); }
    return n;
}
static int Busy(void*) { return 0; }
static int Running(void*, unsigned) { return 0; }
static int Evoke(void*, unsigned, long long, int) { ++evokes; return 0; }
static int Stop(void*, unsigned) { return 0; }
static std::vector<unsigned char> gGcx;
static const unsigned char* gStrOffs = nullptr; static const char* gStrData = nullptr; static const char* gStrEnd = nullptr;
static void LoadScriptStrings(const char* dir) {
    std::string path = std::string(dir) + "\\ntmenu2.gcx";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { printf("mock: no %s, string resources are placeholders\n", path.c_str()); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    gGcx.resize(n); fread(gGcx.data(), 1, n, f); fclose(f);
    auto u = [&](size_t o) { uint32_t v; memcpy(&v, &gGcx[o], 4); return v; };
    size_t p = 4;
    while (p + 4 <= gGcx.size() && u(p) != 0xffffffffu) p += 4;
    size_t blk = p + 4;
    size_t offs = blk + u(blk + 4), data = blk + u(blk + 8), end = blk + u(blk + 0xc);
    uint32_t seed = u(blk + 0x10);
    if (seed) for (size_t i = data; i < end; ++i) { seed = seed * 0x7d2b89ddu + 0xcf9u; gGcx[i] ^= (unsigned char)(seed >> 15); }
    gStrOffs = &gGcx[offs]; gStrData = (const char*)&gGcx[data]; gStrEnd = (const char*)&gGcx[end];
    printf("mock: ntmenu2 script strings loaded (%zu bytes)\n", end - data);
}
static const char* StrAt(int i) {
    if (!gStrOffs || i < 0 || (size_t)i >= (size_t)(gStrData - (const char*)gStrOffs) / 4) return nullptr;
    uint32_t o; memcpy(&o, gStrOffs + 4 * i, 4); o &= 0x7fffffff;
    return gStrData + o;
}
static bool Strings(const unsigned char** o, const char** d, const char** e) {
    if (gVmOn) { *o = gVm.s.strOffs; *d = gVm.s.strData; *e = gVm.s.strEnd; return *o != nullptr; }
    *o = gStrOffs; *d = gStrData; *e = gStrEnd; return gStrOffs != nullptr; }
static const char* StrRes(int i) {
    const char* s = gVmOn ? gVm.s.StrRes(i) : StrAt(i);
    if (s) return s;
    static char b[32]; snprintf(b, sizeof(b), "STRRES_%d", i); return b; }
// the ntmenu2 script's procs as the PC engine would run them, only what the
// simulation reaches: each `[82bc9] <code> args -options` calls the overlay handler
static std::vector<unsigned char> gArgs;
static void Val(std::vector<unsigned char>& v, int type, long long x) {
    v.push_back((unsigned char)type);
    if (type == 0x0e) { v.push_back((unsigned char)(x & 0xff)); v.push_back((unsigned char)((x >> 8) & 0xff)); return; }
    v.insert(v.end(), (unsigned char*)&x, (unsigned char*)&x + 8);
}
struct Opt2 { char c; std::vector<std::pair<int, long long>> vals; };
static void Call82bc9(unsigned code, uint32_t fn, std::vector<long long> pos, std::vector<Opt2> o) {
    for (auto& x : o) { opts.erase(x.c); auto& v = opts[x.c]; for (auto& pv : x.vals) Val(v, pv.first, pv.second); }
    gArgs.clear(); for (long long p : pos) Val(gArgs, 9, p); gArgs.push_back(0);
    gCursor = gArgs.data();
    printf("SCRIPT: [82bc9] [%06x] -> handler %08x\n", code, fn);
    void* r = ps2world::CallCode(fn, 0, 0);
    printf("SCRIPT: [%06x] returned %p\n", code, r);
}
// proc11: the lobby's text tables, English branch (linkvarbuf[4] == 1; the US script's
// other branch, strres 908.., is Spanish)
static void Proc11() {
    auto sr = [](int n) { return std::pair<int, long long>(0x0e, n); };
    Call82bc9(0x27b5ee, 0x3498f8, {}, {{'s', {sr(0)}}, {'i', {sr(47)}}, {'I', {sr(408)}}, {'D', {sr(545)}},
              {'e', {sr(728)}}, {'r', {sr(854)}}, {'m', {{9, 12}, sr(871), {9, 4095}, {9, 4095}, {9, 4095}, {9, 4095}, {9, 2503}}},
              {'t', {sr(907)}}});
}
static int ExecProc(int proc, const int* a, int n) {
    printf("SCRIPT: ExecProc %d (%d args: %d %d)\n", proc, n, a[0], a[1]);
    if (gVmOn) {
        std::vector<int64_t> args(a, a + n);
        int64_t r = gVm.ExecProc(proc, args);
        printf("SCRIPT: proc %d returned %lld\n", proc, (long long)r);
        return (int)r;
    }
    if (proc == 13) {
        Call82bc9(0xbf051f, 0x384250, {}, {});
        if (a[0] == 1) Call82bc9(0xe7a1d7, 0x384370, {3}, {{'v', {{9, 3}}}, {'m', {{9, 1}}}});
        if (a[0] == 4) Proc11();
        if (a[0] == 2 || a[0] == 3) printf("SCRIPT: @proc12 (not in the mock yet)\n");
    }
    return 0;
}
// a block of the script run for PS2 code (GCL ExecBlock 0x1257F8): the interpreter runs it
// (its args, the proc's, are not passed on: the interpreter's blocks read the running proc's)
static int ExecBlock(const unsigned char* blk, const int* a, int n) {
    (void)a;
    if (!gVmOn || !blk) return 0;
    printf("SCRIPT: ExecBlock %p (%d args)\n", (const void*)blk, n);
    const uint8_t* saved = gVm.Cursor();              // the command that asked goes on reading its own values
    int r = gVm.RunBlock(blk);
    gVm.SetCursor(saved);
    return r;
}
// ---- the script's commands that are not the interpreter's own (gcl_vm.h Builtin):
// the base's stage commands (table 0x23A500) and the page overlay's table
static std::map<uint32_t, uint32_t> gTable;          // code -> handler, the overlay's table (0x2DA698)
static void* gRoot = nullptr;
static std::string ScriptStr(int64_t v) {
    if (!v) return "";
    const char* p = (const char*)(intptr_t)v;
    return std::string(p);
}
// which table entries the PC engine runs itself: on ntmenu2 the base functions of its table
// (25e019, 7bc389, daf423, dc83c5; the plugin serves the overlay's own entries, MGO1's
// versions of 9474ff, fa91c, 9bc19a, bcf6ff, bf051f included); on the other pages every
// code the PC knows (its classes stay the engine's), the MGO1-only ones run recompiled
static bool PcOwn(uint32_t code, uint32_t fn) {
    if (gPage == "ntmenu2") return fn < 0x2DA680;
    return gPcCodes.count(code) != 0;
}
static int ScriptCommand(uint32_t code, const uint8_t* args) {
    (void)args;
    switch (code) {
    case 0x082bc9: {                                     // [82bc9] <code> ...: run a table entry, status = its result (0x11B1D8)
        uint32_t c = (uint32_t)gVm.NextInt() & 0xffffff;
        auto it = gTable.find(c);
        if (it == gTable.end()) { Log("SCRIPT: [82bc9] [%06x] is not in the page's table", c); gVm.status = 0; return 0; }
        if (c == 0x56ef97) {                             // the engine's map-about scope (PS2 0x109900, PC exe+0xA11B0):
            int r = 0;                                   // its -s block runs at once, inside the command
            if (gVm.GetOption('s'))
                if (const uint8_t* p = gVm.NextParam()) {
                    int bt = 0; int64_t blk = 0;
                    gVm.NextValue(p, &bt, &blk);
                    if (bt == gclvm::T_BLOCK && blk) {
                        Log("SCRIPT: [82bc9] [56ef97] -s: its block runs (the engine's own command)");
                        gVm.RunBlock((const uint8_t*)(intptr_t)blk);
                        r = 1;
                    }
                }
            gVm.status = r; return 0;
        }
        if (PcOwn(c, it->second)) {
            // the PC engine runs these commands in game for its side; the PS2 world runs its own
            // handler too, so its state (DG tables, frame rate, lights ...) is what a PS2 has.
            // SIM_PC_OWN_PS2=0 leaves them to the PC side only, SIM_PC_OWN_SKIP="code,code" skips some
            static const bool both = !getenv("SIM_PC_OWN_PS2") || atoi(getenv("SIM_PC_OWN_PS2")) != 0;
            static std::set<uint32_t> skip = [] {
                // the sound commands wait on the IOP's sound driver (0x113408): the PC plays the sound
                std::set<uint32_t> k = {0xdc83c5, 0xdaf423, 0x7bc389,
                                        0x63c1d8};       // (0x378C90 waits on the IOP through 0x182090)
                for (const char* q = getenv("SIM_PC_OWN_SKIP"); q && *q;) { char* e = nullptr; k.insert((uint32_t)strtoul(q, &e, 16)); if (!e || !*e) break; q = e + 1; }
                return k;
            }();
            if (!both || skip.count(c)) {
                Log("SCRIPT: [82bc9] [%06x] -> %08x: the PC engine's own in game, not simulated", c, it->second);
                gVm.status = 0; return 0;
            }
            Log("SCRIPT: [82bc9] [%06x] -> %08x: the PC engine's own in game, the PS2 world's too", c, it->second);
            void* r = ps2world::CallCode(it->second, 0, 0);
            gVm.status = (int32_t)(intptr_t)r;
            return 0;
        }
        Log("SCRIPT: [82bc9] [%06x] -> handler %08x", c, it->second);
        void* r = ps2world::CallCode(it->second, 0, 0);
        gVm.status = (int32_t)(intptr_t)r;
        Log("SCRIPT: [%06x] returned %d", c, (int)gVm.status);
        return 0; }
    case 0x6592a7: {                                     // chara <class> <name> -options (0x11B168)
        uint32_t c = (uint32_t)gVm.NextInt() & 0xffffff;
        int name = (int)gVm.NextInt();
        if (c == 0xccbefe) {                             // the lobby root: the plugin's class in game
            gRoot = ps2world::NewOnlineMenu(name, 0);
            Log("SCRIPT: chara [ccbefe] [%06x] -> root %p", name & 0xffffff, gRoot);
            return 0;
        }
        auto it = gTable.find(c);
        // [e4b84a] the MGO1 player (0x40A190 in n001a): -B <player id>; its own id activates the
        // local NetworkDummyPlayer made by [f69343] (0x411FC8: spawn, network records), another id
        // makes that player's puppet. In game the plugin also makes MGS3's NewSnake for the view;
        // SIM_NO_PS2_PLAYER=1 leaves it out here as the older simulations did
        if (c == 0xe4b84a && getenv("SIM_NO_PS2_PLAYER")) { Log("SCRIPT: chara [e4b84a] [%06x]: the MGO1 player, not simulated (SIM_NO_PS2_PLAYER)", name & 0xffffff); return 0; }
        if (it == gTable.end()) { Log("SCRIPT: chara [%06x] [%06x]: not in the page's table", c, name & 0xffffff); return 0; }
        if (PcOwn(c, it->second)) {
            // a class the PC engine also has (in game its own may serve the view): the PS2 world
            // makes MGO1's too, as a PS2 does (the weapon system [cd646f] fills the weapon table
            // the spawn equipment and the weapon window use). SIM_PC_OWN_PS2=0 / SIM_PC_OWN_SKIP as above
            static const bool both = !getenv("SIM_PC_OWN_PS2") || atoi(getenv("SIM_PC_OWN_PS2")) != 0;
            static std::set<uint32_t> skip = [] {
                std::set<uint32_t> k;
                for (const char* q = getenv("SIM_PC_OWN_SKIP"); q && *q;) { char* e = nullptr; k.insert((uint32_t)strtoul(q, &e, 16)); if (!e || !*e) break; q = e + 1; }
                return k;
            }();
            if (!both || skip.count(c)) { Log("SCRIPT: chara [%06x] [%06x]: the PC engine's own in game, not simulated", c, name & 0xffffff); return 0; }
            void* r = ps2world::CallCode(it->second, name, 0);
            Log("SCRIPT: chara [%06x] [%06x] -> %p (the PC engine's own in game, the PS2 world's too)", c, name & 0xffffff, r);
            return 0;
        }
        void* r = ps2world::CallCode(it->second, name, 0);
        Log("SCRIPT: chara [%06x] [%06x] -> %p", c, name & 0xffffff, r);
        return 0; }
    case 0x37c884: {                                     // load "<stage>" -r <resident> -n (0x11B088)
        int t = 0; int64_t v = 0;
        const uint8_t* p = gVm.NextParam();
        std::string stage, resident;
        if (p) { *gVm.cursor = gVm.NextValue(p, &t, &v); stage = ScriptStr(v); }
        bool n = gVm.GetOption('n') != nullptr;
        if (gVm.GetOption('r')) { const uint8_t* q = gVm.NextParam(); if (q) { gVm.NextValue(q, &t, &v); resident = ScriptStr(v); } }
        gLoad.stage = stage; gLoad.resident = resident; gLoad.frame = gFrameNow;
        gStageFlags |= 1 | (n ? 0 : 0x10);                  // as PC exe+0x9D8B0 / PS2 0x11B088
        Log("SCRIPT: load \"%s\" -r \"%s\"%s: the stage change the engine performs next", stage.c_str(), resident.c_str(), n ? " -n" : "");
        return 0; }
    case 0x6bb005: {                                     // same stage again, no reload (0x11B288: flags 0x40, -s 0x10)
        bool sopt = gVm.GetOption('s') != nullptr;
        gStageFlags |= 0x40 | (sopt ? 0x10 : 0);
        Log("SCRIPT: [6bb005] restart the current stage%s", sopt ? " -s" : "");
        return 0; }
    default:
        Log("SCRIPT: command [%06x] is not in the simulation", code);
        return 0;
    }
}
static uint32_t PsR32(uint32_t a) { uint32_t v; memcpy(&v, ps2::gMem + a, 4); return v; }
static uint16_t PsR16(uint32_t a) { uint16_t v; memcpy(&v, ps2::gMem + a, 2); return v; }
static void PsW16(uint32_t a, uint16_t v) { memcpy(ps2::gMem + a, &v, 2); }
static void PsW32(uint32_t a, uint32_t v) { memcpy(ps2::gMem + a, &v, 4); }
static void ShotLogFrame(int f) {
    if (!ShotFn()) return;
    auto s16 = [](uint32_t a) { int16_t v; memcpy(&v, ps2::gMem + a, 2); return (int)v; };
    auto f32 = [](uint32_t a) { float v; memcpy(&v, ps2::gMem + a, 4); return v; };
    const uint32_t me = ps2world::PlayerObject(0);
    const bool alive = me && s16(me + 0xb64) > 0 && !(PsR32(me + 0xb48) & 0x1000000);
    if (const char* k = getenv("SIM_WKILL")) if (*k && *k != '0' && alive) {
        printf("shotlog f%d: kill (life %d set to 0)\n", f, s16(me + 0xb64));
        PsW16(me + 0xb64, 0);
        _putenv("SIM_WKILL=");
    }
    float pos[3] = {};
    const bool havePos = ps2world::PlayerPos(0, pos);
    for (const ShotPending& s : gShotPending) {
        const uint32_t pl = s.pl, wp = s.wp, d = PsR32(pl + 0x608) & 0x01ffffff;
        const bool dOk = d >= 0x100000 && d < 0x01fff000;
        printf("shotlog f%d: shot at f%d dg %lld pl %08x wp %08x num %u desc %u all %d mag %d cap %d dflags %04x | de %d %d kick %d ang %.5f | "
               "pk %d %d of %d %d -> %d %d sn %d %d | pt %.1f %.1f %.1f -> %.1f %.1f %.1f | pos %.1f %.1f %.1f | step %zu btn %04x p %d\n",
               f, s.frame, ps2world::DgFrames(), pl, wp, PsR32(wp + 0x44), dOk ? (unsigned)ps2::gMem[d + 0x14] : 0u, dOk ? s16(d + 0x18) : -1,
               dOk ? s16(d + 0x1c) : -1, dOk ? s16(d + 0x1e) : -1, dOk ? (unsigned)PsR16(d + 0x28) : 0u,
               (int)s.de, s16(wp + 0xde), s16(wp + 0xdc), f32(wp + 0xd8), s16(pl + 0xf30), s16(pl + 0xf32), (int)s.of0, (int)s.of1,
               s16(pl + 0x510), s16(pl + 0x512), s16(pl + 0x6b0), s16(pl + 0x6b2), s.pt[0], s.pt[1], s.pt[2],
               f32(pl + 0x430), f32(pl + 0x434), f32(pl + 0x438), pos[0], pos[1], pos[2], gStep, (unsigned)gPressButtons, gPressPressure);
        gShotPl = pl; gShotWp = wp; gShotAfter = 90;
    }
    gShotPending.clear();
    if (gShotAfter > 0 && gShotPl) {                      // the aim after a shot, each change
        --gShotAfter;
        static int last[5] = {};
        const int now[5] = {s16(gShotPl + 0x510), s16(gShotPl + 0x512), s16(gShotWp + 0xde), s16(gShotPl + 0x6b0), s16(gShotPl + 0x6b2)};
        if (memcmp(now, last, sizeof now)) {
            memcpy(last, now, sizeof now);
            printf("shotlog f%d: aim of %d %d de %d sn %d %d pt %.1f %.1f %.1f pos %.1f %.1f %.1f\n", f, now[0], now[1], now[2], now[3], now[4],
                   f32(gShotPl + 0x430), f32(gShotPl + 0x434), f32(gShotPl + 0x438), pos[0], pos[1], pos[2]);
        }
    }
    for (auto& b : gShotBullets) {                       // a bullet's work +0x40..+0x13F at its 1st and 3rd frames
        ++b.second;
        if (b.second != 1 && b.second != 3) continue;
        if (b.first < 0x100000 || b.first + 0x140 > 0x02000000) continue;
        std::string hex;
        for (uint32_t o = 0x40; o < 0x140; o += 4) { char t[12]; snprintf(t, sizeof t, "%08x ", PsR32(b.first + o)); hex += t; }
        printf("shotlog f%d: bullet %08x frame %d: %s\n", f, b.first, b.second, hex.c_str());
    }
    gShotBullets.erase(std::remove_if(gShotBullets.begin(), gShotBullets.end(), [](const std::pair<uint32_t, int>& b) { return b.second >= 3; }), gShotBullets.end());
    static unsigned long long lastRounds = ~0ull;           // the held weapon's rounds (the player's +0x608: +0x18 all, +0x1C in it)
    if (me) {
        const uint32_t d = PsR32(me + 0x608) & 0x01ffffff;
        if (d >= 0x100000 && d < 0x01fff000) {
            const unsigned long long key = (unsigned long long)d << 32 | (unsigned long long)PsR16(d + 0x18) << 16 | PsR16(d + 0x1c);
            if (key != lastRounds) {
                lastRounds = key;
                printf("shotlog f%d: rounds desc %08x num %u all %d mag %d cap %d alive %d pos %.1f %.1f %.1f\n", f, d, (unsigned)ps2::gMem[d + 0x14],
                       s16(d + 0x18), s16(d + 0x1c), s16(d + 0x1e), alive ? 1 : 0, pos[0], pos[1], pos[2]);
            }
        }
    }
    (void)havePos;
}
// the script as the installed page holds it: SaveMGO's endpoints written in the US slots
// (savemgo_patch.py), the region case and the language as the PS2 base sets them
// a page's script: <dir>\<page>.gcx (ntmenu2 with the SaveMGO endpoints), the page
// overlay's table as the engine serves it
static bool SetupPageScript(const std::string& page) {
    std::string path = gDir + "\\" + page + ".gcx";
    gVm.s = gclvm::Script();
    gVm.frames.clear(); gVm.optStarts.clear(); gVm.status = 0;
    if (!gVm.s.Load(path.c_str())) { printf("SIM: no script %s\n", path.c_str()); return false; }
    gPage = page;
    if (page == "ntmenu2") {
    static const char* slots[][2] = {
        {"https://mgs3sweb.konamionline.com/us/mgs3/", "http://savemgo.com/us/mgs3/"},
        {"mgs3sgate02.konamionline.com", "107.161.80.126"},
        {"mgs3sstun.konamionline.com", "15.204.66.207"}};
    auto& g = gVm.s.g;
    for (auto& sl : slots) {
        std::string old = sl[0]; old.push_back('\0');
        for (size_t i = 2; i + old.size() <= g.size(); ++i) {
            if (memcmp(&g[i], old.data(), old.size()) != 0 || g[i - 2] != 0x07 || g[i - 1] < old.size()) continue;
            size_t ll = g[i - 1];
            memset(&g[i], 0, ll); memcpy(&g[i], sl[1], strlen(sl[1]));
        }
    }
    }
    gTable.clear();
    uint32_t codes[256], fns[256];
    int n = ps2world::OverlayTable(codes, fns, 256);
    for (int k = 0; k < n; ++k) gTable[codes[k] & 0xffffff] = fns[k];
    int pc = 0; for (auto& e : gTable) pc += gPcCodes.count(e.first) ? 1 : 0;
    printf("script: %s.gcx, %zu procs, table %d entries (%d the PC knows)\n", page.c_str(), gVm.s.procs.size(), n, pc);
    return true;
}
static bool SetupVm(const char* dir, int regionCase) {
    gDir = dir;
    if (FILE* f = fopen((gDir + "\\pc_codes.txt").c_str(), "r")) {
        char line[64]; while (fgets(line, sizeof(line), f)) { if (line[0] != '#') gPcCodes.insert((uint32_t)strtoul(line, nullptr, 16)); }
        fclose(f);
    }
    printf("script: %zu codes known to the PC engine\n", gPcCodes.size());
    if (!SetupPageScript("ntmenu2")) { printf("RESULT no ntmenu2.gcx\n"); return false; }
    gVm.cursor = (const uint8_t**)&gCursor;
    gVm.log = [](const char* m) { Log("%s", m); };
    gVm.varBuffer = [](int region) -> uint8_t* {
        uint32_t a = region == 8 ? PsR32(0x23A678) : region == 1 ? 0x2873D8 : PsR32(0x23A680);
        return a ? ps2::gMem + a : nullptr;
    };
    gVm.command = &ScriptCommand;
    uint8_t* varbuf = gVm.varBuffer(0);
    uint8_t* linkvarbuf = gVm.varBuffer(8);
    if (varbuf) varbuf[0x10F5] = (uint8_t)regionCase;
    int lang = getenv("SIM_LANG") ? atoi(getenv("SIM_LANG")) : 1;         // linkvarbuf[4]: 1 = English texts
    if (linkvarbuf) linkvarbuf[4] = (uint8_t)lang;
    printf("script: region case %d, language %d\n", regionCase, lang);
    return true;
}
static std::map<unsigned, int> vars;
static int GetVar(unsigned code, int* v) { if (gVmOn) { *v = (int)gVm.VarGet(code, 0); return 0; } *v = vars[code]; return 0; }
static int SetVar(unsigned code, int v) { if (gVmOn) gVm.VarSet(code, 0, v); else vars[code] = v; printf("SCRIPT: SetVar %08x = %d\n", code, v); return 0; }
// scripted pad: "frame:buttons" pairs from argv[3], held for 4 frames each
static int gFrame = 0;
static std::vector<std::pair<int, unsigned>> gScript;
// Autopilot (steps "=chase/<frames>" and "=cqc/<frames>"): walks the local player to the first
// remote player and fires (square held then released) or punches (circle) once close. The
// d-pad's world directions depend on the camera, so they are measured: up and right are
// pressed 10 frames each and the moves read from the NetworkDummyPlayer (ps2world::PlayerPos);
// they are measured again every 240 frames of driving.
static struct Autopilot {
    int phase = 0, t = 0, drive = 0, shots = 0; float p0[3] = {}, up[2] = {}, right[2] = {};
    float prev[3] = {}; bool havePrev = false; float face[2] = {0, 0}; bool faced = false;
    float best = 1e9f; int stuck = 0, detour = 0, side = 1;   // walls: a side step when the distance stops falling
    bool teleported = false;                                  // =duel: put next to the target (test only)
    int lastTp = -100000;                                     // the frame of the last move: again after a respawn (10 s)
    int tpTries = 0;                                          // moves again because the first left it on another level
    int paused = 0;                                           // SIM_LADDER_PAUSE done
    float tpFrom[3] = {}, tpTo[3] = {}; int tpLeft = 0;       // the copies a tick behind, moved in the frames after
    int lastTeleport = -100000;                               // =capture: the frame of the last move onto the KEROTAN / to a goal
    int still = 0, circleUntil = -1;                          // the directions move no one (a menu: TEAM SELECT): Circle
    float lastMe[3] = {}; int same = 0;                       // frames the local player has not moved
    int boxSeen = -1;                                         // =pickup with SIM_PICKUP_NOW: when the box came
    int madeAtStart = -1;                                     // =spawn: the player's weapons made when it started
    int takenAtStart = -1;                                    // =pickup with SIM_PICKUP_END: the weapons taken from boxes when it started
} gAuto;
// a menu over the game (TEAM SELECT, a round's results): the local player's position stays the same for
// 3 s; Circle then, 4 frames a second
static bool MenuCircle(const float me[3]) {
    Autopilot& a = gAuto;
    if (memcmp(me, a.lastMe, sizeof a.lastMe)) { memcpy(a.lastMe, me, sizeof a.lastMe); a.same = 0; return false; }
    return ++a.same > 180 && (a.same % 60) < 4;
}
// test only (=duel): every copy of the local player's position in PS2 RAM (its control, the network
// copies) moved to `to`, so a duel starts in one room whatever the spawns
static int TeleportPlayer(const float me[3], const float to[3], float near = 0.5f) {
    int n = 0;
    for (uint32_t a = 0x100000; a + 12 <= 0x01fe0000; a += 4) {
        float v[3]; memcpy(v, ps2::gMem + a, 12);
        if (fabsf(v[0] - me[0]) < near && fabsf(v[1] - me[1]) < near && fabsf(v[2] - me[2]) < near) { memcpy(ps2::gMem + a, to, 12); ++n; }
    }
    return n;
}
static void AutoLog(const char* what, float d) { printf("autopilot f%d: %s (distance %.0f)\n", gFrame, what, d); }
// SIM_PUNCH_PNG="count:step": from the first punches on, `count` pictures `step` frames apart (the GS drawn then)
static int gPunchPngLeft = 0, gPunchPngStep = 4, gPunchPngNext = 0;
// SIM_PNG_STEPS="<buttons hex>[p<pressure hex>]:count:step" (open list): each step that presses those buttons (the steps
// file's =<buttons>...) starts `count` pictures `step` frames apart (st_<frame>.png), only the 3 frames before each drawn
static int gStepPngLeft = 0, gStepPngStep = 10, gStepPngNext = 0;
static void StepPngStart(unsigned buttons, int pressure, int f) {
    const char* e = getenv("SIM_PNG_STEPS");                // "<buttons>[p<pressure>]:count:step[,...]" (1000a: =watchplayer ...): the first that matches
    for (; e && *e; e = strchr(e, ',') ? strchr(e, ',') + 1 : nullptr) {
        char* q = nullptr;
        const unsigned want = (unsigned)strtoul(e, &q, 16);
        int wantP = -1;
        if (q && *q == 'p') wantP = (int)strtoul(q + 1, &q, 16);
        if (want != buttons || (wantP >= 0 && wantP != pressure)) continue;
        gStepPngLeft = q && *q == ':' ? atoi(q + 1) : 60;
        const char* c2 = q && *q == ':' ? strchr(q + 1, ':') : nullptr;
        gStepPngStep = std::max(1, c2 ? atoi(c2 + 1) : 10);   // 1: every VBlank
        gStepPngNext = f + gStepPngStep;
        printf("SIM: step pictures from frame %d: %d every %d frames\n", f, gStepPngLeft, gStepPngStep);
        return;
    }
}
static void StepPngFrame(int f) {
    if (gStepPngLeft <= 0) return;
    ps2::gGsDraw = gStepPngNext - f <= 2;                  // the 3 frames up to each picture drawn
    if (f < gStepPngNext) return;
    char name[64]; snprintf(name, sizeof name, "st_%05d.png", f);
    ps2::GsWritePng(name);
    static const uint32_t clockAt = getenv("SIM_DG_CLOCK") ? (uint32_t)strtoul(getenv("SIM_DG_CLOCK"), nullptr, 16) : 0x23AFD0;
    if (gStepPngStep < 4) printf("SIM: %s clock %u dg %lld\n", name, ps2::gMem[clockAt & 0x01ffffff] & 1, ps2world::DgFrames());
    gStepPngNext += gStepPngStep;
    if (--gStepPngLeft == 0) ps2::gGsDraw = !getenv("SIM_GS_DRAW_FROM");
}
static int gLadderDrawLeft = 0;   // SIM_LADDER_DRAW=<frames>: the climber draws every frame from his move to the ladder
static void PunchPictures() {
    static bool once = false;
    const char* e = getenv("SIM_PUNCH_PNG");
    if (!e || once) return;
    once = true;
    gPunchPngLeft = atoi(e);
    if (const char* c = strchr(e, ':')) gPunchPngStep = std::max(1, atoi(c + 1));
    gPunchPngNext = gFrame + 1;
    printf("SIM: %d pictures every %d frames from frame %d\n", gPunchPngLeft, gPunchPngStep, gPunchPngNext);
}
// =capture (test only): the KEROTAN of a capture mission (NewOnlineItemChara2's item 0, its position at
// +0xb0 + 0x90, its holder at +0xb0 + 0xd4) taken by a move onto it, then carried to a goal (the
// manager's points +0x4c0 and +0x700), each goal tried for 40 s; a menu (TEAM SELECT) gets Circle
static float RdF(uint32_t addr) { float v; memcpy(&v, ps2::gMem + addr, 4); return v; }
static uint32_t RdW(uint32_t addr) { uint32_t v; memcpy(&v, ps2::gMem + addr, 4); return v; }
static uint32_t CaptureButtons() {
    Autopilot& a = gAuto;
    float me[3];
    if (!ps2world::PlayerPos(0, me)) return (gFrame % 120) < 4 ? 0x2000 : 0;
    if (gFrame < a.circleUntil) return 0x2000;
    const uint32_t man = ps2world::ActorByName("NewOnlineItemChara2");
    const uint32_t mine0 = ps2world::ActorByName("NewNetworkDummyPlayer");
    if (!(man && mine0 && RdW(man + 0xb0 + 0xd4) == mine0) && MenuCircle(me)) return 0x2000;   // not carrying it: a menu?
    const uint32_t mine = ps2world::ActorByName("NewNetworkDummyPlayer");
    if (!man || !mine) return (gFrame % 120) < 4 ? 0x2000 : 0;
    const uint32_t item = man + 0xb0;
    const float it[3] = {RdF(item + 0x90), RdF(item + 0x94), RdF(item + 0x98)};
    const uint32_t holder = RdW(item + 0xd4);
    const float dx = it[0] - me[0], dz = it[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    const bool held = holder == mine;                        // its holder: our own player
    ++a.t;
    if (a.t % 600 == 1) {
        char b[200];
        snprintf(b, sizeof b, "capture: me %.0f %.0f %.0f, KEROTAN %.0f %.0f %.0f holder %08x (mine %08x) state %u", me[0], me[1], me[2],
                 it[0], it[1], it[2], holder, mine, RdW(item + 0xd0));
        AutoLog(b, dist);
    }
    if (!held) {
        if (a.phase != 0) { a.phase = 0; AutoLog("capture: not held any more", dist); }
        if (gFrame - a.lastTeleport > 300 && dist > 150.f) {   // onto it (again at most every 5 s)
            const float to[3] = {it[0] + 40.f, it[1], it[2]};
            const int n = TeleportPlayer(me, to);
            a.lastTeleport = gFrame;
            char b[128]; snprintf(b, sizeof b, "capture: moved onto the KEROTAN %.0f %.0f %.0f (%d copies)", to[0], to[1], to[2], n);
            AutoLog(b, dist);
        }
        return 0;
    }
    // held: to a goal, the other one after 40 s
    if (a.phase == 0) { a.phase = 1; a.drive = 0; a.lastTeleport = -100000; AutoLog("capture: the KEROTAN is ours", dist); }
    if (gFrame - a.lastTeleport > 2400) {
        const uint32_t g = man + (a.drive % 2 ? 0x700 : 0x4c0);
        const float to[3] = {RdF(g), RdF(g + 4), RdF(g + 8)};
        const int n = TeleportPlayer(me, to);
        a.lastTeleport = gFrame; ++a.drive;
        char b[128]; snprintf(b, sizeof b, "capture: to goal %d at %.0f %.0f %.0f (%d copies)", (a.drive + 1) % 2, to[0], to[1], to[2], n);
        AutoLog(b, dist);
    }
    return 0;
}
// =ladder/<frames> (SIM_LADDER="x,y,z,turn,top": a map's ladder, its script's [d4d8a5] setup: -p the
// point, -r its turn, -h its bottom and top): the pad's axes measured, the local player put 350 units
// from the ladder on one side, walked into it; on it (80 units above its foot) up is held to the top,
// then 90 frames more (off at the top); the other side after 240 frames without a climb
static uint32_t LadderButtons() {
    Autopilot& a = gAuto;
    float me[3];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || (PsR32(self + 0xb48) & 0x1000000) || !ps2world::PlayerPos(0, me)) return 0;   // spawned first, in the map
    static const int wait = getenv("SIM_LADDER_WAIT") ? atoi(getenv("SIM_LADDER_WAIT")) : 0;   // frames alive before the climb
    if (a.phase == 0 && a.t == 0) {                     // SIM_LADDER_SYNC=<file>: the watcher's "in place" first
        if (const char* sync = getenv("SIM_LADDER_SYNC")) {
            FILE* f = fopen(sync, "rb");
            if (!f) return 0;
            fclose(f);
        }
        if (++a.drive < wait) return 0;
    }
    static float L[5] = {}; static bool parsed = false;
    if (!parsed) { parsed = true; if (const char* e = getenv("SIM_LADDER")) sscanf(e, "%f,%f,%f,%f,%f", &L[0], &L[1], &L[2], &L[3], &L[4]); }
    if (gHaveStepLadder) memcpy(L, gStepLadder, sizeof L);
    if (a.tpLeft > 0) { --a.tpLeft; TeleportPlayer(a.tpFrom, a.tpTo, 150.f); }
    const float dx = L[0] - me[0], dz = L[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t b = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) b |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) b |= r > 0 ? 0x20 : 0x80;
        return b;
    };
    ++a.t;
    switch (a.phase) {
    case 0: case 1: {                                   // measure up (0) or right (1)
        if (a.t == 1) { memcpy(a.p0, me, sizeof me); }
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) { a.t = 0; return 0; }
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        if (a.phase == 1) { char b[128]; snprintf(b, sizeof b, "ladder: up = (%.2f, %.2f), right = (%.2f, %.2f)", a.up[0], a.up[1], a.right[0], a.right[1]); AutoLog(b, dist); }
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0;
        return 0; }
    case 2: {                                           // put at 350 units from the ladder, on side a.side
        if (a.tpTries >= 6) { if (a.t == 1) AutoLog("ladder: no way onto it", dist); return 0; }
        const float ang = L[3] / 32768.f * 3.14159265f, nx = sinf(ang), nz = cosf(ang), d = 350.f * a.side;
        const float to[3] = {L[0] - nx * d, L[1] + 5.f, L[2] - nz * d};
        const int n = TeleportPlayer(me, to);
        memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
        // drawn as slowly as the watcher, who draws every frame for his pictures: the two screens keep pace
        if (const char* e = getenv("SIM_LADDER_DRAW")) if (!gLadderDrawLeft) gLadderDrawLeft = atoi(e);
        char b[160]; snprintf(b, sizeof b, "ladder: moved from %.0f %.0f %.0f to %.0f %.0f %.0f (%d copies), side %d", me[0], me[1], me[2], to[0], to[1], to[2], n, a.side);
        AutoLog(b, dist);
        a.phase = 3; a.t = 0;
        return 0; }
    case 3:                                             // walk to it (its foot's trap: the script's [3bd490])
        if (a.t <= 30) { a.best = me[1]; return 0; }    // the copies moved first; the standing height
        if (me[1] > a.best + 100.f) { a.phase = 4; a.t = 0; char b[96]; snprintf(b, sizeof b, "ladder: on it at %.0f: up", me[1]); AutoLog(b, dist); return 0x10; }
        if (dist < 110.f || a.t > 200) { a.phase = 6; a.t = 0; a.shots = 0; char b[96]; snprintf(b, sizeof b, "ladder: at its foot (%.0f %.0f %.0f): the action button", me[0], me[1], me[2]); AutoLog(b, dist); return 0; }
        return toward(dx, dz);
    case 6: {                                           // facing it: triangle, then circle, 4 frames every 40, with the stick toward it
        if (me[1] > a.best + 100.f) { a.phase = 4; a.t = 0; char b[96]; snprintf(b, sizeof b, "ladder: on it at %.0f after %d presses: up", me[1], a.shots); AutoLog(b, dist); return 0x10; }
        const int k = a.t % 40;
        if (k == 0) ++a.shots;
        if (a.shots > 12) { a.side = -a.side; a.phase = 2; a.t = 0; ++a.tpTries; AutoLog("ladder: no climb, the other side", dist); return 0; }
        // the first three without the stick: the facing he came with (a ladder whose trap takes one facing only,
        // passed by a little, would turn him back)
        const uint32_t button = a.shots <= 6 ? 0x1000 : 0x2000;
        return (k < 4 ? button : 0) | (k < 12 && a.shots > 3 ? toward(dx, dz) : 0); }
    case 4: {                                           // up held to the top (the height logged every 30 frames)
        // SIM_LADDER_PAUSE=<frames>: first no input that long (his getting on carries him up a stretch
        // first: to y 2040 on n033a)
        static const int pause = getenv("SIM_LADDER_PAUSE") ? atoi(getenv("SIM_LADDER_PAUSE")) : 0;
        if (pause > 0 && !a.paused) {
            if (a.t == 1) { char b[96]; snprintf(b, sizeof b, "ladder: no input for %d frames", pause); AutoLog(b, dist); }
            if (a.t <= pause) return 0;
            a.paused = 1; a.t = 1;
        }
        // SIM_LADDER_UPDOWN=<height>: once that far above the foot, down 40 frames, still 30, then up again
        static const float updown = getenv("SIM_LADDER_UPDOWN") ? (float)atof(getenv("SIM_LADDER_UPDOWN")) : 0.f;
        if (a.t % 30 == 0) { char b[96]; snprintf(b, sizeof b, "ladder: climbing at %.0f", me[1]); AutoLog(b, dist); }
        if (updown > 0.f && !a.detour && me[1] >= a.best + updown) {
            a.detour = 1; a.phase = 7; a.t = 0;
            char b[96]; snprintf(b, sizeof b, "ladder: down from %.0f", me[1]); AutoLog(b, dist);
            return 0x40;
        }
        if (me[1] >= L[4] - 30.f || (a.t > 60 && dist > 150.f)) { a.phase = 5; a.t = 0; char b[96]; snprintf(b, sizeof b, "ladder: off it at %.0f", me[1]); AutoLog(b, dist); }
        else if (a.t > 1500) { a.phase = 5; a.t = 0; AutoLog("ladder: no top after 1500 frames", dist); }
        return 0x10; }
    case 7:                                             // SIM_LADDER_UPDOWN: down, still, then up again (phase 4)
        if (a.t % 10 == 0) { char b[96]; snprintf(b, sizeof b, "ladder: %s at %.0f", a.t <= 40 ? "down" : "still", me[1]); AutoLog(b, dist); }
        if (a.t <= 40) return 0x40;
        if (a.t <= 70) return 0;
        a.phase = 4; a.t = 0;
        { char b[96]; snprintf(b, sizeof b, "ladder: up again from %.0f", me[1]); AutoLog(b, dist); }
        return 0x10;
    case 5: {                                           // off at the top
        if (a.t == 90) AutoLog("ladder: done", dist);
        // SIM_LADDER_DOWN=1: then back onto it from the top (toward its point, triangle every 40 frames) and down
        static const bool down = getenv("SIM_LADDER_DOWN") != nullptr;
        if (down && a.t > 150) { a.phase = 8; a.t = 0; a.shots = 0; a.p0[1] = me[1]; char b[96]; snprintf(b, sizeof b, "ladder: at the top at %.0f %.0f %.0f: back onto it", me[0], me[1], me[2]); AutoLog(b, dist); }
        return a.t <= 90 ? 0x10 : 0; }
    case 8: {                                           // SIM_LADDER_DOWN: on it from the top (at it: a step down near it is not)
        if (me[1] < a.p0[1] - 150.f && dist < 400.f) { a.phase = 9; a.t = 0; char b[96]; snprintf(b, sizeof b, "ladder: on it from the top at %.0f after %d presses: down", me[1], a.shots); AutoLog(b, dist); return 0x40; }
        const int k = a.t % 40;
        if (k == 0) ++a.shots;
        if (a.shots > 16) { a.phase = 10; a.t = 0; AutoLog("ladder: no way down from the top", dist); return 0; }
        const uint32_t button = a.shots % 2 ? 0x1000 : 0x2000;
        return (k < 4 ? button : 0) | (k >= 8 && k < 30 ? toward(dx, dz) : 0); }
    case 9:                                             // down held to the foot
        if (a.t % 30 == 0) { char b[96]; snprintf(b, sizeof b, "ladder: down at %.0f", me[1]); AutoLog(b, dist); }
        if (me[1] <= a.best + 60.f || a.t > 1500) { a.phase = 10; a.t = 0; char b[96]; snprintf(b, sizeof b, "ladder: %s at %.0f %.0f %.0f", a.t > 1500 ? "no foot after 1500 frames" : "down at its foot", me[0], me[1], me[2]); AutoLog(b, dist); return 0; }
        return 0x40;
    default:
        return 0;
    }
}
// =carry/<frames> (tests): alive SIM_CARRY_WAIT frames (60), the local player put on the mission object SIM_CARRY (0:
// RESCUE's GA-KO, CAPTURE's KEROTAN, TEAM SNEAKING's second one 1) and left there: he takes it; its holder logged
static uint32_t CarryButtons() {
    Autopilot& a = gAuto;
    float me[3], ob[3];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || (PsR32(self + 0xb48) & 0x1000000) || !ps2world::PlayerPos(0, me)) return 0;   // in the map
    static const int which = getenv("SIM_CARRY") ? atoi(getenv("SIM_CARRY")) : 0;
    static const int wait = getenv("SIM_CARRY_WAIT") ? atoi(getenv("SIM_CARRY_WAIT")) : 60;
    static const float dy = getenv("SIM_CARRY_DY") ? (float)atof(getenv("SIM_CARRY_DY")) : 0.f;   // -950: on the floor under it
    uint32_t holder = 0;
    if (!ps2world::MissionObjectPos(which, ob, &holder)) return 0;
    ob[1] += dy;
    ++a.t;
    if (a.phase == 0 && a.t >= wait) {
        const int n = TeleportPlayer(me, ob);
        char b[160]; snprintf(b, sizeof b, "carry: put on mission object %d at %.0f %.0f %.0f (%d copies)", which, ob[0], ob[1], ob[2], n);
        AutoLog(b, 0); a.phase = 1; a.t = 0;
    } else if (a.phase == 1 && holder == self) {
        char b[160]; snprintf(b, sizeof b, "carry: mission object %d held (holder %08x) %d frames after", which, holder, a.t);
        AutoLog(b, 0); a.phase = 2;
    } else if (a.phase == 1 && a.t % 150 == 0) {        // not taken yet (the round waits for its players): put on it again
        const int n = TeleportPlayer(me, ob);
        char b[160]; snprintf(b, sizeof b, "carry: mission object %d not taken (holder %08x): put on it again (%d copies)", which, holder, n);
        AutoLog(b, 0);
    }
    return 0;
}
// =visit/<frames> (SIM_VISIT=hash,...: points of the stage by their codes, as the game finds them): alive, the local
// player put 50 above each in turn, his place logged SIM_VISIT_WAIT frames (89) later: on its floor, or fallen
static uint32_t VisitButtons() {
    Autopilot& a = gAuto;
    float me[3], p[3] = {};
    uint32_t code = 0;
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || (PsR32(self + 0xb48) & 0x1000000) || !ps2world::PlayerPos(0, me)) return 0;   // in the map
    static const int wait = getenv("SIM_VISIT_WAIT") ? atoi(getenv("SIM_VISIT_WAIT")) : 89;
    static int gen = 0;                                  // a new map's list (VisitPoint -1: its number): from its first point
    if (const int g = ps2world::VisitPoint(-1, p, &code); g != gen) { gen = g; a.shots = 0; a.phase = 0; a.t = 0; a.tpLeft = 0; }
    if (a.tpLeft > 0) { --a.tpLeft; TeleportPlayer(a.tpFrom, a.tpTo, 150.f); }
    const int n = ps2world::VisitPoint(a.shots, p, &code);
    if (a.shots >= n) { if (n && a.phase != 9) { a.phase = 9; AutoLog("visit: done", 0); } return 0; }
    ++a.t;
    char b[192];
    if (a.phase == 0) {
        const float to[3] = {p[0], p[1] + 50.f, p[2]};
        const int c = TeleportPlayer(me, to);
        memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
        snprintf(b, sizeof b, "visit: point %06x (%.0f %.0f %.0f), %d of %d: put on it (%d copies)", code, p[0], p[1], p[2], a.shots + 1, n, c);
        AutoLog(b, 0); a.phase = 1; a.t = 0;
    } else if (a.t >= wait) {
        const float dy = me[1] - p[1], d = sqrtf((me[0] - p[0]) * (me[0] - p[0]) + (me[2] - p[2]) * (me[2] - p[2]));
        snprintf(b, sizeof b, "visit: point %06x: standing at %.0f %.0f %.0f (%+.0f, %.0f aside)%s", code, me[0], me[1], me[2], dy, d,
                 dy < -300.f ? ": fell" : d > 300.f ? ": moved off" : ": on its floor");
        AutoLog(b, d); ++a.shots; a.phase = 0; a.t = 0;
    }
    return 0;
}
// SIM_CARRY (=carry, =ladder): every 30 frames the mission object's holder and place logged (held: this player's)
static void CarryWatch() {
    static const int which = getenv("SIM_CARRY") ? atoi(getenv("SIM_CARRY")) : -1;
    if (which < 0 || gFrame % 30) return;
    float ob[3], me[3] = {};
    uint32_t holder = 0;
    if (!ps2world::MissionObjectPos(which, ob, &holder)) return;
    const uint32_t self = ps2world::PlayerObject(0);
    ps2world::PlayerPos(0, me);
    printf("carry watch f%d: mission object %d at %.0f %.0f %.0f, holder %08x (%s), the player at %.0f %.0f %.0f\n", gFrame, which,
           ob[0], ob[1], ob[2], holder, holder && holder == self ? "held by this player" : holder ? "another" : "none", me[0], me[1], me[2]);
}
// =watchladder/<frames> (SIM_LADDER as for =ladder): the local player put 1500 units from the ladder
// on the side its climber comes from, walked toward it until 1000 units away (the camera behind him
// looking at it), then left there: his pictures show the other player's climb
static uint32_t WatchLadderButtons() {
    Autopilot& a = gAuto;
    float me[3];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || !ps2world::PlayerPos(0, me)) return 0;
    static float L[5] = {}; static bool parsed = false;
    if (!parsed) { parsed = true; if (const char* e = getenv("SIM_LADDER")) sscanf(e, "%f,%f,%f,%f,%f", &L[0], &L[1], &L[2], &L[3], &L[4]); }
    if (gHaveStepLadder) memcpy(L, gStepLadder, sizeof L);
    if (a.tpLeft > 0) { --a.tpLeft; TeleportPlayer(a.tpFrom, a.tpTo, 150.f); }
    const float dx = L[0] - me[0], dz = L[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t b = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) b |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) b |= r > 0 ? 0x20 : 0x80;
        return b;
    };
    ++a.t;
    switch (a.phase) {
    case 0: case 1: {                                   // measure up (0) or right (1)
        if (a.t == 1) { memcpy(a.p0, me, sizeof me); }
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) { a.t = 0; return 0; }
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0;
        return 0; }
    case 2: {
        const float ang = L[3] / 32768.f * 3.14159265f, nx = sinf(ang), nz = cosf(ang);
        const float to[3] = {L[0] - nx * 1500.f, L[1] + 5.f, L[2] - nz * 1500.f};
        const int n = TeleportPlayer(me, to);
        memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
        char b[160]; snprintf(b, sizeof b, "watch ladder: moved from %.0f %.0f %.0f to %.0f %.0f %.0f (%d copies)", me[0], me[1], me[2], to[0], to[1], to[2], n);
        AutoLog(b, dist);
        a.phase = 3; a.t = 0;
        return 0; }
    case 3:
        if (a.t <= 30) return 0;
        if (me[1] < L[1] + 500.f) {                     // not standing there yet (in a menu before a spawn): again
            if (a.t > 90) { a.phase = 2; a.t = 0; }
            return 0;
        }
        if (dist < 1000.f || a.t > 150) {
            a.phase = 4; a.t = 0;
            char b[96]; snprintf(b, sizeof b, "watch ladder: watching from %.0f %.0f %.0f", me[0], me[1], me[2]); AutoLog(b, dist);
            if (const char* sync = getenv("SIM_LADDER_SYNC")) if (FILE* f = fopen(sync, "wb")) { fputs("ready\n", f); fclose(f); }
            return 0;
        }
        return toward(dx, dz);
    default:                                            // watching; put back after a respawn (a round restarted for a joiner)
        if (a.t > 60 && dist > 1300.f) {
            a.phase = 2; a.t = 0; AutoLog("watch ladder: moved away (a respawn): again", dist);
            if (const char* sync = getenv("SIM_LADDER_SYNC")) remove(sync);
        }
        // SIM_LADDER_PNG="count:step": the climber within 400 units of the ladder: count pictures step
        // frames apart, every frame drawn meanwhile (SIM_PUNCH_PNG's)
        if (const char* e = getenv("SIM_LADDER_PNG")) {
            static bool started = false;
            float q[3];
            if (!started && ps2world::PlayerPos(1, q) && fabsf(q[0] - L[0]) < 400.f && fabsf(q[2] - L[2]) < 400.f) {
                started = true;
                gPunchPngLeft = atoi(e);
                if (const char* c = strchr(e, ':')) gPunchPngStep = std::max(1, atoi(c + 1));
                gPunchPngNext = gFrame + 1;
                printf("SIM: the climber at the ladder at frame %d: %d pictures every %d frames\n", gFrame, gPunchPngLeft, gPunchPngStep);
            }
        }
        return 0;
    }
}
// =watchplayer/<frames> (SIM_WATCH_PLAYER="dist,angle": the other player's side the local one watches from, in
// degrees from where he faces: 0 in front, 90 his left, -90 his right, 180 behind; 1500,90 by default): the pad's
// axes measured, the local player put there, turned toward him, then first person (R1 held): his pictures show the
// other player (his stance, his weapon) from that side; put there again when the other has moved away (a respawn)
static uint32_t WatchPlayerButtons() {
    Autopilot& a = gAuto;
    float me[3], him[3];
    const uint32_t self = ps2world::PlayerObject(0), other = ps2world::PlayerObject(1);
    if (!self || !other || (int16_t)PsR16(self + 0xb64) <= 0 || !ps2world::PlayerPos(0, me) || !ps2world::PlayerPos(1, him)) return 0;
    static float W[2] = {1500.f, 90.f}; static bool parsed = false;
    if (!parsed) { parsed = true; if (const char* e = getenv("SIM_WATCH_PLAYER")) sscanf(e, "%f,%f", &W[0], &W[1]); }
    if (a.tpLeft > 0) { --a.tpLeft; TeleportPlayer(a.tpFrom, a.tpTo, 150.f); }
    const float dx = him[0] - me[0], dz = him[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t b = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) b |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) b |= r > 0 ? 0x20 : 0x80;
        return b;
    };
    char b[192];
    ++a.t;
    switch (a.phase) {
    case 0: case 1: {                                   // measure up (0) or right (1)
        if (a.t == 1) { memcpy(a.p0, me, sizeof me); }
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) { a.t = 0; return 0; }
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0;
        return 0; }
    case 2: {                                           // beside him, at his height
        const float ang = (int16_t)PsR16(other + 0x142) / 32768.f * 3.14159265f + W[1] / 180.f * 3.14159265f;
        const float to[3] = {him[0] + sinf(ang) * W[0], him[1] + 5.f, him[2] + cosf(ang) * W[0]};
        const int n = TeleportPlayer(me, to);
        memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
        snprintf(b, sizeof b, "watch player: moved from %.0f %.0f %.0f to %.0f %.0f %.0f (%d copies), the other at %.0f %.0f %.0f turn %d",
                 me[0], me[1], me[2], to[0], to[1], to[2], n, him[0], him[1], him[2], (int16_t)PsR16(other + 0x142));
        AutoLog(b, dist);
        a.phase = 3; a.t = 0;
        return 0; }
    case 3:                                             // settled, then turned toward him (a short push)
        if (a.t <= 30) return 0;
        if (a.t <= 36) return toward(dx, dz);
        if (a.t <= 60) return 0;
        a.phase = 4; a.t = 0;
        snprintf(b, sizeof b, "watch player: watching from %.0f %.0f %.0f, turn %d", me[0], me[1], me[2], (int16_t)PsR16(self + 0x142));
        AutoLog(b, dist);
        return 0;
    default:                                            // first person, held; again when he is far (a respawn)
        if (a.t > 120 && fabsf(dist - W[0]) > 900.f) { a.phase = 2; a.t = 0; AutoLog("watch player: he moved away: again", dist); return 0; }
        return 0x0800;
    }
}
// =rail/<frames> (SIM_RAIL="x,y,z,turn[,back]": a point of a railing's line (a map geom's chunk 6), the turn
// that faces it from the walkway): the pad's axes measured, the local player put `back` units (400) before it,
// walked into it, then triangle 4 frames every 40 (the stick toward it) until he hangs (below the walkway or
// beyond the line); hanging: his height logged 150 frames, then triangle (up) 4 frames every 40
static int gRailSteps = 0;                              // =rail steps begun
static uint32_t RailButtons() {
    Autopilot& a = gAuto;
    float me[3];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || !ps2world::PlayerPos(0, me)) return 0;
    static float L[5] = {0, 0, 0, 0, 400}; static bool parsed = false;
    if (!parsed) { parsed = true; if (const char* e = getenv("SIM_RAIL")) sscanf(e, "%f,%f,%f,%f,%f", &L[0], &L[1], &L[2], &L[3], &L[4]); }
    if (a.tpLeft > 0) { --a.tpLeft; TeleportPlayer(a.tpFrom, a.tpTo, 150.f); }
    const float ang = L[3] / 32768.f * 3.14159265f, fx = sinf(ang), fz = cosf(ang);
    const float dx = L[0] - me[0], dz = L[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    const float ahead = -(dx * fx + dz * fz);           // beyond the line along the facing: > 0
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t b = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) b |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) b |= r > 0 ? 0x20 : 0x80;
        return b;
    };
    char b[192];
    ++a.t;
    switch (a.phase) {
    case 0: case 1: {                                   // measure up (0) or right (1)
        if (a.t == 1) { memcpy(a.p0, me, sizeof me); }
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) { a.t = 0; return 0; }
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0;
        return 0; }
    case 2: {
        const float to[3] = {L[0] - fx * L[4], L[1] + 5.f, L[2] - fz * L[4]};
        const int n = TeleportPlayer(me, to);
        memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
        ++gRailSteps;
        snprintf(b, sizeof b, "rail: moved from %.0f %.0f %.0f to %.0f %.0f %.0f (%d copies)", me[0], me[1], me[2], to[0], to[1], to[2], n);
        AutoLog(b, dist);
        a.phase = 3; a.t = 0;
        return 0; }
    case 3:                                             // walk to it (its zone's trap: the script's [3bd490]); stopped
        if (a.t <= 30) { a.best = me[1]; a.p0[0] = dist; a.stuck = 0; return 0; }   // by the railing: at once (pushed
        if (dist < a.p0[0] - 5.f) { a.p0[0] = dist; a.stuck = 0; } else ++a.stuck;   // on, he would lean on it)
        if (dist < 120.f || a.stuck >= 6 || a.t > 150) {
            snprintf(b, sizeof b, "rail: at it (%.0f %.0f %.0f, %.0f before the line, turn %d): the action button", me[0], me[1], me[2], -ahead, (int16_t)PsR16(self + 0x142));
            static int pc = -1, ps = 10;                // SIM_RAIL_PNG="count:step": pictures from the first press on
            if (pc < 0) { pc = 0; if (const char* e = getenv("SIM_RAIL_PNG")) sscanf(e, "%d:%d", &pc, &ps); }
            if (pc > 0) { gStepPngLeft = pc; gStepPngStep = std::max(1, ps); gStepPngNext = gFrame + gStepPngStep; }
            AutoLog(b, dist); a.phase = 6; a.t = 0; a.shots = 0; return 0;
        }
        return toward(dx, dz);
    case 6: {                                           // triangle 4 frames every 40, the stick toward it 12
        if (me[1] < a.best - 300.f || ahead > 150.f) {
            snprintf(b, sizeof b, "rail: hanging after %d presses at %.0f %.0f %.0f (%.0f below the walkway, %.0f beyond the line)",
                     a.shots, me[0], me[1], me[2], a.best - me[1], ahead);
            AutoLog(b, dist); a.phase = 4; a.t = 0; return 0;
        }
        const int k = a.t % 40;
        if (k == 0) {
            ++a.shots;
            snprintf(b, sizeof b, "rail: press %d at %.0f %.0f %.0f, turn %d", a.shots, me[0], me[1], me[2], (int16_t)PsR16(self + 0x142)); AutoLog(b, dist);
        }
        if (a.shots > 8) { a.phase = 5; a.t = 0; AutoLog("rail: no hang", dist); return 0; }
        return (k < 4 ? 0x1000 | toward(dx, dz) : 0); }
    case 4: {                                           // hanging: still, then up (SIM_RAIL_DROP=<buttons>: those
        static const uint32_t drops = getenv("SIM_RAIL_DROP") ? strtoul(getenv("SIM_RAIL_DROP"), nullptr, 16) : 0;  // instead,
        static const char* dc = getenv("SIM_RAIL_DROP") ? strchr(getenv("SIM_RAIL_DROP"), ',') : nullptr;           // the fall
        const uint32_t drop = gRailSteps >= (dc ? atoi(dc + 1) : 1) ? drops : 0;       // logged; ",n": from the n-th =rail on)
        const int every = drop && a.t > 150 ? 10 : 30;
        if (a.t % every == 0) { snprintf(b, sizeof b, "rail: %s at %.0f %.0f %.0f", a.t <= 150 ? "hanging" : drop ? "dropped" : "up", me[0], me[1], me[2]); AutoLog(b, dist); }
        if (a.t <= 150) return 0;
        if (a.t > 450) { a.phase = 5; a.t = 0; AutoLog("rail: done", dist); return 0; }
        if (drop) return a.t <= 154 ? drop : 0;
        return (a.t % 40) < 4 ? 0x1000 : 0; }
    default:
        return 0;
    }
}
// =m15/<frames> (map 15's collision, test only: SIM_M15="x,z,tx,tz,act,frames[,pics];..."): stations in turn, kept
// across lives (SIM_M15_LOOP=1: from the first again at each life): the pad's axes measured, the local player put at
// x, z on the deck, then: f aims in the first person view (L1+R1 held, turned with left / right until he faces tx, tz;
// his turn written when they do not turn him),
// waits for the first remote player near SIM_M15_WAIT="x,z" (900 frames at most), fires (square, `frames`); w walks
// to tx, tz; c crouches (cross) then crawls to it; s stays (turned toward it by a short push, R1 held when pics > 0);
// p opens the START menu (closed 8 frames before the end);
// pics pictures over the action (4 frames apart at least); positions logged every 10 frames of a walk; a station whose
// action ends with the player's death is over
static uint32_t M15Buttons() {
    Autopilot& a = gAuto;
    struct St { float x, z, tx, tz; char act; int frames, pics; };
    static std::vector<St> st; static bool parsed = false; static size_t cur = 0; static uint32_t lastSelf = 0;
    static float wait[2] = {0, 0}; static bool hasWait = false; static int sign = 0, good = 0; static bool force = false;
    static int lastCall = -100000;                       // the frame of the last call while alive (the menus between lives skip it)
    static const bool loop = getenv("SIM_M15_LOOP") != nullptr;
    if (!parsed) {
        parsed = true;
        for (const char* q = getenv("SIM_M15"); q && *q;) {
            St s{}; char act = 's';
            if (sscanf(q, "%f,%f,%f,%f,%c,%d,%d", &s.x, &s.z, &s.tx, &s.tz, &act, &s.frames, &s.pics) >= 6) { s.act = act; st.push_back(s); }
            const char* n = strchr(q, ';'); if (!n) break; q = n + 1;
        }
        if (const char* e = getenv("SIM_M15_WAIT")) hasWait = sscanf(e, "%f,%f", &wait[0], &wait[1]) == 2;
        printf("m15: %zu stations\n", st.size());
    }
    float me[3];
    char b[200];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || !ps2world::PlayerPos(0, me)) {
        if (a.phase == 4 && cur < st.size()) {          // dead during a station's action: that station is over
            snprintf(b, sizeof b, "m15: station %zu (%c): dead", cur, st[cur].act); AutoLog(b, 0);
            ++cur; a.phase = 0; a.t = 0;
        }
        return 0;
    }
    const bool gap = gFrame - lastCall > 30;
    lastCall = gFrame;
    if (self != lastSelf || gap) {                      // a new life (a new object, or the same one after the menus)
        if (gap && self == lastSelf && a.phase == 4 && cur < st.size()) ++cur;   // its station's action ended with that life
        lastSelf = self; a.phase = 0; a.t = 0;
        if (loop) cur = 0;
        snprintf(b, sizeof b, "m15: alive at %.0f %.0f %.0f, station %zu", me[0], me[1], me[2], cur); AutoLog(b, 0);
        if (cur >= st.size() && !st.empty()) { gPunchPngLeft = 10; gPunchPngStep = 12; gPunchPngNext = gFrame + 1; }
    }
    if (a.tpLeft > 0) { --a.tpLeft; TeleportPlayer(a.tpFrom, a.tpTo, 150.f); }
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t k = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) k |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) k |= r > 0 ? 0x20 : 0x80;
        return k;
    };
    ++a.t;
    if (cur >= st.size()) { if (a.phase != 9) { a.phase = 9; AutoLog("m15: done", 0); } return 0; }
    const St& s = st[cur];
    const float dx = s.tx - me[0], dz = s.tz - me[2], dist = sqrtf(dx * dx + dz * dz);
    const int turn = (int16_t)PsR16(self + 0x142);
    const int want = (int)lrintf(atan2f(dx, dz) * 32768.f / 3.14159265f);
    const int err = (int16_t)(uint16_t)(want - turn);
    auto remoteNear = [&]() -> bool {
        float q[3]; const uint32_t o = ps2world::PlayerObject(1);
        return !hasWait || (o && (int16_t)PsR16(o + 0xb64) > 0 && ps2world::PlayerPos(1, q) && fabsf(q[0] - wait[0]) < 600.f && fabsf(q[2] - wait[1]) < 600.f);
    };
    switch (a.phase) {
    case 0: case 1: {                                   // measure up (0) or right (1)
        if (a.t == 1) { memcpy(a.p0, me, sizeof me); }
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) { a.t = 0; return 0; }
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0;
        return 0; }
    case 2: {                                           // put on the station
        const float to[3] = {s.x, 11505.f, s.z};
        const int n = TeleportPlayer(me, to);
        memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
        snprintf(b, sizeof b, "m15: station %zu (%c): moved from %.0f %.0f %.0f to %.0f %.0f %.0f (%d copies)", cur, s.act, me[0], me[1], me[2], to[0], to[1], to[2], n);
        AutoLog(b, dist);
        a.phase = 3; a.t = 0; a.stuck = 0; a.best = 1e9f; sign = 0; good = 0; a.shots = 0; force = false;
        return 0; }
    case 3:                                             // settled (a short push toward the target but for f)
        if (a.t <= 30) return 0;
        if (s.act != 'f' && a.t <= 36) return toward(dx, dz);
        if (a.t <= 70) return s.act == 'f' && a.t > 50 ? 0x0c00 : 0;
        snprintf(b, sizeof b, "m15: station %zu (%c) at %.0f %.0f %.0f, turn %d, %.0f from its target", cur, s.act, me[0], me[1], me[2], turn, dist);
        AutoLog(b, dist);
        a.phase = s.act == 'f' ? 5 : 4; a.t = 0;
        if (s.pics > 0 && s.act != 'f') { gPunchPngLeft = s.pics; gPunchPngStep = std::max(4, s.frames / s.pics); gPunchPngNext = gFrame + 1; }
        return s.act == 'f' ? 0x0c00 : 0;
    case 5: {                                           // f: aimed, turned toward the target, the remote player waited for
        if (sign == 0) {                                // which way right turns him
            if (a.t == 1) a.p0[0] = (float)turn;
            if (a.t <= 6) return 0x0c00 | 0x20;
            if (a.t < 12) return 0x0c00;
            const int moved = (int16_t)(uint16_t)(turn - (int)a.p0[0]);
            sign = moved > 0 ? 1 : moved < 0 ? -1 : 0;
            snprintf(b, sizeof b, "m15: station %zu: right turns %d (%d)", cur, sign, moved); AutoLog(b, dist);
            if (!sign && ++a.shots < 4) { a.t = 0; return 0x0c00; }
            if (!sign) { sign = 1; force = true; AutoLog("m15: the turn does not change in the first person view: his turn written", dist); }
            else a.t = 12;
        }
        if (force) PsW16(self + 0x142, (uint16_t)want);
        if (abs(err) < 200) ++good; else good = 0;
        const bool ready = good >= 4 && remoteNear();
        if (ready || a.t > 900) {
            snprintf(b, sizeof b, "m15: station %zu: aimed (turn %d, %d off), the remote player %s", cur, turn, err, remoteNear() ? "in place" : "not in place");
            AutoLog(b, dist);
            a.phase = 4; a.t = 0;
            if (s.pics > 0) { gPunchPngLeft = s.pics; gPunchPngStep = std::max(4, s.frames / s.pics); gPunchPngNext = gFrame + 1; }
            return 0x0c00;
        }
        if (abs(err) < 200 || force) return 0x0c00;
        const bool pulse = abs(err) < 1200 ? (a.t % 3) == 0 : true;   // near: short taps
        return 0x0c00 | (pulse ? ((err > 0) == (sign > 0) ? 0x20 : 0x80) : 0); }
    default: {                                          // the action
        bool end = a.t > s.frames;
        uint32_t k = 0;
        if (s.act == 'f') {
            k = 0x0c00 | 0x8000;
            if (force) PsW16(self + 0x142, (uint16_t)want);
            if (a.t % 30 == 1) { snprintf(b, sizeof b, "m15: station %zu: firing, turn %d (%d off)", cur, turn, err); AutoLog(b, dist); }
            if (a.t == 1) { snprintf(b, sizeof b, "m15: station %zu: fire from %.0f %.0f %.0f toward %.0f %.0f", cur, me[0], me[1], me[2], s.tx, s.tz); AutoLog(b, dist); }
        } else if (s.act == 'w' || s.act == 'c') {
            const int start = s.act == 'c' ? 40 : 0;    // crouched first (cross 4 frames)
            if (s.act == 'c' && a.t <= 4) k = 0x4000;
            else if (a.t > start) {
                if (dist < 150.f) end = true;
                else k = toward(dx, dz);
                if (dist < a.best - 10.f) { a.best = dist; a.stuck = 0; } else ++a.stuck;
                if ((a.t - start) % 10 == 0) {
                    snprintf(b, sizeof b, "m15: station %zu (%c) at %.0f %.0f %.0f, %.0f from its target%s", cur, s.act, me[0], me[1], me[2], dist, a.stuck > 30 ? " (no nearer for 30 frames)" : "");
                    AutoLog(b, dist);
                }
            }
        } else if (s.act == 'p') {                    // the START menu: opened, its pictures, closed
            if (a.t <= 4 || (a.t > s.frames - 12 && a.t <= s.frames - 8)) k = 0x0008;
        } else if (s.pics > 0) k = 0x0800;
        if (end) {
            snprintf(b, sizeof b, "m15: station %zu (%c) ends at %.0f %.0f %.0f, %.0f from its target", cur, s.act, me[0], me[1], me[2], dist);
            AutoLog(b, dist);
            ++cur; a.phase = 0; a.t = 0;
            return 0;
        }
        return k; }
    }
}
// =play/<frames> (7: fire, 8: CQC): a whole game. Alive: =chase or =cqc toward the nearest living
// remote player (a new start after each spawn); otherwise the round's screens answered: circle on its
// score and round screens, START on the weapon choice (its timer)
static int gTarget = 1;                                   // the remote player chased (PlayerPos's index)
static bool gWasAlive = false;
static int gPlayTurn = -1;                                // SIM_PLAY_TURN=<RX hex>: =play holds the right stick there while alive
// not alive (=play, =ladder): the round's screens answered; returns 0x80000000 when alive
static uint32_t MenuButtons() {
    static int holdUntil = -1, lastStart = -100000, lastCircle = -100000; static uint32_t held = 0;
    if (gFrame < holdUntil) return held;
    // the weapon choice (its player already given life): START as its timer's text comes (SIM_CHOICE_WAIT=<frames>:
    // that long after its first timer text, its page seen)
    static const int choiceWait = getenv("SIM_CHOICE_WAIT") ? atoi(getenv("SIM_CHOICE_WAIT")) : 0;
    static int choiceSince = -100000;
    if (gFrame - gSeenTimer >= 150) choiceSince = -100000;
    else if (choiceSince < 0) choiceSince = gFrame;
    if (gFrame - gSeenTimer < 30 && gFrame - lastStart > 120 && gFrame - choiceSince >= choiceWait) { lastStart = gFrame; held = 0x8; holdUntil = gFrame + 4; return held; }
    const uint32_t self = ps2world::PlayerObject(0);
    if (self && (int16_t)PsR16(self + 0xb64) > 0) return 0x80000000u;
    gWasAlive = false;
    if (gFrame - gSeenRound < 240 && gFrame - lastCircle > 100) { lastCircle = gFrame; held = 0x2000; holdUntil = gFrame + 4; return held; }
    return 0;
}
static uint32_t PlayButtons(int mode) {
    const uint32_t m = MenuButtons();
    if (m != 0x80000000u) return m;
    if (!gWasAlive) { gWasAlive = true; gAuto = Autopilot(); AutoLog("play: alive, a new start", 0); }
    float me[3]; float best = 1e18f; int pick = 0;
    if (!ps2world::PlayerPos(0, me)) return 0;
    for (int w = 1; w <= 16; ++w) {
        const uint32_t o = ps2world::PlayerObject(w);
        if (!o) break;
        float q[3];
        if ((int16_t)PsR16(o + 0xb64) <= 0 || !ps2world::PlayerPos(w, q)) continue;
        const float d = (q[0] - me[0]) * (q[0] - me[0]) + (q[2] - me[2]) * (q[2] - me[2]);
        if (d < best) { best = d; pick = w; }
    }
    if (!pick) {                                          // SIM_PLAY_FIRE=<frames>: alone, square held 25 frames every <frames>
        static const int fire = getenv("SIM_PLAY_FIRE") ? atoi(getenv("SIM_PLAY_FIRE")) : 0;
        return fire > 25 && gFrame % fire < 25 ? 0x8000 : 0;
    }
    if (pick != gTarget) { char b[64]; snprintf(b, sizeof b, "play: target remote %d", pick); AutoLog(b, sqrtf(best)); gTarget = pick; }
    return 0x80000000u;                                   // AutoButtons goes on with =chase or =cqc
}
// =tsne/<frames> (TEAM SNEAKING, docs/TEAM_SNEAKING.md): alive, the local player is put on a target (SIM_TSNE_TARGET:
// 0 the GA-KO, 1 the KEROTAN), held there SIM_TSNE_HOLD frames (150), then put on a goal (SIM_TSNE_GOAL: 0 or 1) and left
// there; SIM_TSNE_WAIT frames alive first (90)
static bool gTsneAlive = false;                          // =tsne: alive at its last pass (the menus' passes clear it)
static uint32_t TsneButtons() {
    Autopilot& a = gAuto;
    float me[3], pts[4][3];
    static int lives = 0;                                // the lives seen (a round each in TEAM SNEAKING)
    static uint32_t lastSelf = 0;
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || !ps2world::PlayerPos(0, me)) { a.phase = 0; a.t = 0; gTsneAlive = false; return 0; }
    if (!gTsneAlive || self != lastSelf) { gTsneAlive = true; lastSelf = self; ++lives; a.phase = 0; a.t = 0; }
    // SIM_TSNE_TARGET / SIM_TSNE_GOAL: 0, 1, or "a" (alternate: the GA-KO and goal 0 in odd lives, the KEROTAN and
    // goal 1 in even ones); SIM_TSNE_ROLE=sneak: only while this player sneaks
    static const char* te = getenv("SIM_TSNE_TARGET"), * ge = getenv("SIM_TSNE_GOAL");
    static const bool sneakOnly = getenv("SIM_TSNE_ROLE") && !strcmp(getenv("SIM_TSNE_ROLE"), "sneak");
    const int target = te && *te == 'a' ? (lives % 2 == 0) : te ? atoi(te) : 0;
    const int goal = ge && *ge == 'a' ? (lives % 2 == 0) : ge ? atoi(ge) : 0;
    static const int hold = getenv("SIM_TSNE_HOLD") ? atoi(getenv("SIM_TSNE_HOLD")) : 150;
    static const std::vector<int> waits = [] {          // SIM_TSNE_WAIT=w or w1,w2..: a life's (the last for the later ones)
        std::vector<int> v;
        if (const char* e = getenv("SIM_TSNE_WAIT")) for (const char* q = e; *q;) { v.push_back(atoi(q)); q = strchr(q, ','); if (!q) break; ++q; }
        if (v.empty()) v.push_back(90);
        return v;
    }();
    const int wait = waits[(size_t)std::min<int>(std::max(lives, 1), (int)waits.size()) - 1];
    if (ps2world::TsnePoints(pts) < 2) return 0;
    if (sneakOnly && ps2world::TsneRole() != 1) return 0;
    ++a.t;
    // SIM_TSNE_PAUSE=1: the START menu opened halfway through the wait and closed 60 frames later (its map: the targets)
    static const bool pause = getenv("SIM_TSNE_PAUSE") != nullptr;
    if (pause && a.phase == 0 && ((a.t >= wait / 2 && a.t < wait / 2 + 4) || (a.t >= wait / 2 + 60 && a.t < wait / 2 + 64))) return 0x0008;
    if (a.phase == 0 && a.t >= wait) {
        const float* to = pts[target & 1];
        const int n = TeleportPlayer(me, to);
        char b[160]; snprintf(b, sizeof b, "tsne: life %d: put on target %d at %.0f %.0f %.0f (%d copies)", lives, target & 1, to[0], to[1], to[2], n);
        AutoLog(b, 0); a.phase = 1; a.t = 0;
    } else if (a.phase == 1 && a.t >= hold) {
        const float* to = pts[2 + (goal & 1)];
        const int n = TeleportPlayer(me, to);
        char b[160]; snprintf(b, sizeof b, "tsne: life %d: put on goal %d at %.0f %.0f %.0f (%d copies)", lives, goal & 1, to[0], to[1], to[2], n);
        AutoLog(b, 0); a.phase = 2; a.t = 0;
    }
    return 0;
}
// =bc/<frames> (BASE CAPTURE, docs/BASE_CAPTURE.md): alive, the local player is put in the bases in turn (SIM_BC_TOUR:
// "1,3,..." the bases' numbers, default every one in order, then again; "1,2;5,4": a list a round, the last one for the
// rounds after it, an empty one: he stays where he spawns), held in each SIM_BC_HOLD frames (900), the first SIM_BC_WAIT
// frames alive (90); SIM_BC_SPOT: a step from the centre (mm, 0) along his slot's direction; SIM_BC_KILL=k: his life
// set to 0 at the end of the hold of his k-th base of the first round (his respawn at a base)
static bool gBcAlive = false;
// SIM_BC_PLAN (SIM_BC_PLAN_R / SIM_BC_PLAN_B by this player's team): "L:B,L:B;L:B" a list a round of the game (a new
// round once the last one had a result; a restart, as a joiner comes in, plays its list again): as the round's time
// left falls to L seconds the local player is put in BASE B (k: his life set to 0, he stays where he respawns; p: the
// START menu opened for 90 frames; 0: he stays); after a respawn he is put back in the plan's base SIM_BC_WAIT frames
// alive. Same time left on every PC of the game: the players of a team move together. Entries passed while out (a
// late player after a GAME RESTART, a death): only the last base among them (kills and pauses passed left out)
static struct BcPlan { uint32_t roundId = 0; int round = 0, idx = 0, target = -1, result = 0, pauseAt = -100000, aliveFor = 0; } gBcPlan;
static void BcPlanTrack() {                             // each frame of =bc: the rounds and their results
    int result = 0;
    ps2world::BcTimeLeft(&result);
    const uint32_t rid = ps2world::BcRoundId();
    if (rid && rid != gBcPlan.roundId) {
        const bool next = !gBcPlan.roundId || gBcPlan.result;
        gBcPlan.round += next; gBcPlan.roundId = rid; gBcPlan.idx = 0; gBcPlan.target = -1; gBcPlan.result = 0;
        char m[96]; snprintf(m, sizeof m, "bc plan: round %d%s", gBcPlan.round, next ? "" : " (restarted)"); AutoLog(m, 0);
    }
    if (result) gBcPlan.result = result;
}
static uint32_t BcPlanButtons(uint32_t self, const float me[3], float b[][3], int n, int team) {
    const char* e = team == 0 && getenv("SIM_BC_PLAN_R") ? getenv("SIM_BC_PLAN_R") : team == 1 && getenv("SIM_BC_PLAN_B") ? getenv("SIM_BC_PLAN_B") : getenv("SIM_BC_PLAN");
    if (!e) return 0;
    struct Entry { float left; int what; };            // what: 1..5 a base, 0 stay, -1 kill, -2 pause
    std::vector<std::vector<Entry>> lists(1);
    for (const char* q = e; *q;) {
        if (*q == ';') { lists.emplace_back(); ++q; continue; }
        if (*q == ',' || *q == ' ') { ++q; continue; }
        char* c = nullptr;
        const float l = strtof(q, &c);
        if (c == q || *c != ':') break;
        q = c + 1;
        const int w = *q == 'k' ? -1 : *q == 'p' ? -2 : atoi(q);
        while (*q && *q != ',' && *q != ';') ++q;
        lists.back().push_back({l, w});
    }
    BcPlan& p = gBcPlan;
    const std::vector<Entry>& list = lists[std::min((size_t)std::max(p.round - 1, 0), lists.size() - 1)];
    const float left = ps2world::BcTimeLeft(nullptr);
    const int sincePause = gFrame - p.pauseAt;
    if (sincePause < 4 || (sincePause >= 90 && sincePause < 94)) return 0x0008;
    ++p.aliveFor;
    auto put = [&](int k, const char* why) {
        const float ang = (float)((PsR16(self + 0xb60) & 0xff) % 8) * 0.785398f;
        static const float spot = getenv("SIM_BC_SPOT") ? (float)atof(getenv("SIM_BC_SPOT")) : 0.0f;
        const float to[3] = {b[k][0] + spot * sinf(ang), b[k][1] + 50.f, b[k][2] + spot * cosf(ang)};
        const int c = TeleportPlayer(me, to);
        char m[160]; snprintf(m, sizeof m, "bc plan: round %d, %.0f s left: put in BASE %d (%s) at %.0f %.0f %.0f (%d copies)", p.round, left, k + 1, why, to[0], to[1], to[2], c);
        AutoLog(m, 0);
    };
    while (left >= 0 && p.idx < (int)list.size() && left <= list[(size_t)p.idx].left) {
        const Entry& x = list[(size_t)p.idx++];
        const bool late = left < x.left - 3.0f;          // passed while out (a late joiner, a death): only the last base
        bool more = false;                               // a later base passed too (a kill or pause after it: not)
        for (size_t j = (size_t)p.idx; j < list.size() && left <= list[j].left; ++j) more = more || list[j].what >= 1;
        if (late && (x.what < 1 || more)) continue;
        if (x.what >= 1 && x.what <= n) { p.target = x.what - 1; put(p.target, "the plan"); p.aliveFor = 0; return 0x80000000u; }
        if (x.what == -1) {
            if (PsR32(self + 0xb48) & 0x1000000) { --p.idx; break; }   // no death in the spawn's walk: later
            PsW16(self + 0xb64, 0); p.target = -1;
            char m[96]; snprintf(m, sizeof m, "bc plan: round %d, %.0f s left: life set to 0", p.round, left); AutoLog(m, 0);
            return 0x80000000u;
        }
        if (x.what == -2) { p.pauseAt = gFrame; return 0x0008; }
    }
    static const int wait = getenv("SIM_BC_WAIT") ? atoi(getenv("SIM_BC_WAIT")) : 90;
    if (p.target >= 0 && p.target < n && p.aliveFor == wait) {   // a respawn: back to the plan's base
        const float dx = me[0] - b[p.target][0], dz = me[2] - b[p.target][2];
        if (dx * dx + dz * dz > 2000.0f * 2000.0f) put(p.target, "again after a respawn");
    }
    return 0x80000000u;
}
// =bc, alive but not in the map: after a GAME RESTART the player keeps his life through the GAME TYPE page (CIRCLE
// every 100 frames, as for a dead player) and the weapon choice (+0xB48 bit 24 set); no put until he is in the map
// (rsF1's lesson: a put in the weapon choice)
static uint32_t BcPageButtons() {
    static int holdUntil = -1, last = -100000, pageSeen = -100000, made = 0;
    if (gFrame < holdUntil) return 0x2000;
    if (gFrame - gBcPlan.pauseAt < 100) return 0;      // the plan's START map: its own presses
    if (pageSeen != gBcPageAt) { pageSeen = gBcPageAt; made = ps2world::LocalWeaponsMade(); }
    const bool page = gBcPageAt > std::max(gBcChoiceAt, gSeenTimer) && gFrame - gBcPageAt < 3600 && ps2world::LocalWeaponsMade() <= made;
    if (page && gFrame - gBcPageAt > 30 && gFrame - last > 100) {
        last = gFrame; holdUntil = gFrame + 4;
        AutoLog("bc: alive on the GAME TYPE page: CIRCLE", 0);
        return 0x2000;
    }
    const uint32_t self = ps2world::PlayerObject(0);
    return page || (self && (PsR32(self + 0xb48) & 0x1000000)) ? 0x80000000u : 0;
}
static uint32_t BcButtons() {
    Autopilot& a = gAuto;
    float me[3], b[5][3];
    static uint32_t lastSelf = 0, lastRound = 0;
    static int at = 0, rounds = 0;
    static bool killed = false;
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || !ps2world::PlayerPos(0, me)) { a.phase = 0; a.t = 0; gBcAlive = false; gBcPlan.aliveFor = 0; return 0; }
    if (!gBcAlive || self != lastSelf) { gBcAlive = true; lastSelf = self; a.phase = 0; a.t = 0; gBcPlan.aliveFor = 0; }
    const int n = ps2world::BcBases(b);
    if (n < 1) return 0;
    const uint32_t round = ps2world::BcRoundId();
    if (round != lastRound) {                          // a new round: its own list from its first base
        lastRound = round; ++rounds; at = 0; a.phase = 0; a.t = 0;
        char m[64]; snprintf(m, sizeof m, "bc: round %d", rounds); AutoLog(m, 0);
    }
    // SIM_BC_TOUR_R / SIM_BC_TOUR_B: the tour of a red / blue player (his team byte, [player + 0x920] + 1), else SIM_BC_TOUR;
    // SIM_BC_ONCE=1: the tour's last base kept (no second turn)
    auto lists = [](const char* e) {
        std::vector<std::vector<int>> v(1);
        if (e) for (const char* q = e; *q; ++q) {
            if (*q == ';') { v.emplace_back(); continue; }
            if (*q >= '0' && *q <= '9') { v.back().push_back(atoi(q)); while (q[1] >= '0' && q[1] <= '9') ++q; }
        }
        return v;
    };
    static const std::vector<std::vector<int>> tourAll = lists(getenv("SIM_BC_TOUR")), tourR = lists(getenv("SIM_BC_TOUR_R")),
                                                tourB = lists(getenv("SIM_BC_TOUR_B"));
    static const bool once = getenv("SIM_BC_ONCE") != nullptr;
    static const bool perRound = (getenv("SIM_BC_TOUR") && strchr(getenv("SIM_BC_TOUR"), ';')) || (getenv("SIM_BC_TOUR_R") && strchr(getenv("SIM_BC_TOUR_R"), ';')) ||
                                 (getenv("SIM_BC_TOUR_B") && strchr(getenv("SIM_BC_TOUR_B"), ';'));
    const uint32_t tp = PsR32(self + 0x920);
    const int team = tp >= 0x100000 && tp < 0x2000000 ? (int)(PsR32(tp) >> 8 & 0xff) : -1;
    if (const uint32_t p = BcPlanButtons(self, me, b, n, team)) return p == 0x80000000u ? 0 : p;
    const bool own = (team == 0 && getenv("SIM_BC_TOUR_R")) || (team == 1 && getenv("SIM_BC_TOUR_B"));
    const std::vector<std::vector<int>>& tours = !own ? tourAll : team == 0 ? tourR : tourB;
    const std::vector<int>& tour = tours[std::min((size_t)std::max(rounds - 1, 0), tours.size() - 1)];
    const bool stay = perRound || once;                // a list ends with its last base kept
    static const int hold = getenv("SIM_BC_HOLD") ? atoi(getenv("SIM_BC_HOLD")) : 900;
    static const int wait = getenv("SIM_BC_WAIT") ? atoi(getenv("SIM_BC_WAIT")) : 90;
    static const float spot = getenv("SIM_BC_SPOT") ? (float)atof(getenv("SIM_BC_SPOT")) : 0.0f;
    static const int killAfter = getenv("SIM_BC_KILL") ? atoi(getenv("SIM_BC_KILL")) : 0;
    ++a.t;
    // SIM_BC_PAUSE=1: the START menu (its 2D map) opened for 90 frames at the end of each hold
    static const bool pause = getenv("SIM_BC_PAUSE") != nullptr;
    if (pause && a.phase == 1 && ((a.t >= hold - 100 && a.t < hold - 96) || (a.t >= hold - 10 && a.t < hold - 6))) return 0x0008;
    if (a.phase == 1 && a.t >= hold && killAfter && !killed && rounds == 1 && at == killAfter) {
        killed = true;
        PsW16(self + 0xb64, 0);
        AutoLog("bc: life set to 0 (SIM_BC_KILL)", 0);
        a.phase = 2;
        return 0;
    }
    if ((a.phase == 0 && a.t >= wait) || (a.phase == 1 && a.t >= hold)) {
        if (stay && (tour.empty() || (size_t)at >= tour.size())) { a.phase = 2; return 0; }
        const int k = tour.empty() ? at % n : (tour[(size_t)at % tour.size()] - 1);
        ++at;
        if (k < 0 || k >= n) return 0;
        const float ang = (float)((PsR16(self + 0xb60) & 0xff) % 8) * 0.785398f;
        const float to[3] = {b[k][0] + spot * sinf(ang), b[k][1] + 50.f, b[k][2] + spot * cosf(ang)};
        const int c = TeleportPlayer(me, to);
        char m[160]; snprintf(m, sizeof m, "bc: put in BASE %d at %.0f %.0f %.0f (%d copies)", k + 1, to[0], to[1], to[2], c);
        AutoLog(m, 0); a.phase = 1; a.t = 0;
    }
    return 0;
}
// =pickup/<frames> (Snake, 23:22: the name of a weapon dropped at a death; SIM_KILL_AT kills him): the local player's
// place kept while he lives; after his death and his next spawn the pad's axes measured, he is put SIM_PICKUP_BACK
// units (150) from that place and walked onto it for 90 frames (what he dropped lies there: its pickup);
// SIM_PICKUP_AT="x,y,z" that place instead (the drop lies where the drop probe's box is made, not at the death),
// SIM_PICKUP_BOX=<code> the place of the item box of that code (hex: c the AK-47's; 0 the last made);
// SIM_PICKUP_BACK=0: put on it, then steps up and down; SIM_PICKUP_NOW=1: in his first life, once that box is made
// (another player's drop); SIM_PICKUP_END=1: the step ends once he took the weapon of an added weapon's base from a
// box (#119: ps2world::BoxesTaken), the next steps go on
static float gPickLast[3], gPickDead[3]; static bool gPickAlive = false, gPickHaveDead = false;
static void PickupTrack() {                             // each frame (the menus' frames too): his place, his death
    float me[3];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || !ps2world::PlayerPos(0, me)) {
        if (gPickAlive) {
            gPickAlive = false; gPickHaveDead = true; memcpy(gPickDead, gPickLast, sizeof gPickDead);
            if (const char* at = getenv("SIM_PICKUP_AT"))   // "x,y,z": where the drop lies (the drop probe's box), not the death's place
                sscanf(at, "%f,%f,%f", &gPickDead[0], &gPickDead[1], &gPickDead[2]);
            char b[128]; snprintf(b, sizeof b, "pickup: dead at %.0f %.0f %.0f", gPickDead[0], gPickDead[1], gPickDead[2]); AutoLog(b, 0);
            gAuto = Autopilot();
        }
        return;
    }
    if (!gPickAlive) { gPickAlive = true; gAuto = Autopilot(); }
    memcpy(gPickLast, me, sizeof me);
}
static uint32_t PickupButtons() {
    Autopilot& a = gAuto;
    static int back = -1;
    if (back < 0) back = getenv("SIM_PICKUP_BACK") ? atoi(getenv("SIM_PICKUP_BACK")) : 150;
    float me[3];
    static const bool now = getenv("SIM_PICKUP_NOW") != nullptr;   // another player's drop: no death of his own first
    if (!gPickAlive || !(gPickHaveDead || now) || !ps2world::PlayerPos(0, me)) return 0;   // his first life: nothing dropped yet
    const float* dead = gPickDead;
    if (a.tpLeft > 0) { --a.tpLeft; TeleportPlayer(a.tpFrom, a.tpTo, 150.f); }
    const float dx = dead[0] - me[0], dz = dead[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t b = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) b |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) b |= r > 0 ? 0x20 : 0x80;
        return b;
    };
    char b[192];
    ++a.t;
    switch (a.phase) {
    case 0: case 1: {                                   // measure up (0) or right (1)
        if (a.t == 1) { memcpy(a.p0, me, sizeof me); }
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) { a.t = 0; return 0; }
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0;
        return 0; }
    case 2: {                                           // `back` units before the place along the pad's up
        static const int wait = getenv("SIM_PICKUP_WAIT") ? atoi(getenv("SIM_PICKUP_WAIT")) : 0;   // frames first (the spawn's shield)
        if (a.t < wait) return 0;
        float box[3];                                   // SIM_PICKUP_BOX=1: the place of the last item box made instead
        const bool boxThere = getenv("SIM_PICKUP_BOX") && ps2world::ItemBoxPos(box, (int)strtol(getenv("SIM_PICKUP_BOX"), nullptr, 16));
        if (now && !boxThere) return 0;                 // SIM_PICKUP_NOW: until the box is made
        if (now) {                                      // and SIM_PICKUP_WAIT frames after it came (it falls, slides)
            if (a.boxSeen < 0) a.boxSeen = a.t;
            if (a.t - a.boxSeen < wait) return 0;
        }
        if (boxThere) {
            snprintf(b, sizeof b, "pickup: the last item box at %.0f %.0f %.0f", box[0], box[1], box[2]);
            AutoLog(b, 0);
            memcpy(gPickDead, box, sizeof box);
        }
        const float to[3] = {dead[0] - a.up[0] * back, dead[1] + 5.f, dead[2] - a.up[1] * back};
        const int n = TeleportPlayer(me, to);
        memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
        snprintf(b, sizeof b, "pickup: moved from %.0f %.0f %.0f to %.0f %.0f %.0f (%d copies)", me[0], me[1], me[2], to[0], to[1], to[2], n);
        AutoLog(b, dist);
        a.phase = 3; a.t = 0;
        return 0; }
    case 3:                                             // settled, then walked onto the place
        if (a.t <= 30) return 0;
        if (!back && a.t <= 150) return ((a.t - 30) / 10) % 2 ? 0x40 : 0x10;   // on it: a step up and down, over it
        if (a.t <= 120) return toward(dx, dz);
        snprintf(b, sizeof b, "pickup: stopped at %.0f %.0f %.0f", me[0], me[1], me[2]);
        AutoLog(b, dist);
        a.phase = 4; a.t = 0;
        return 0;
    default:
        return 0;
    }
}
// =wake/<frames> (TEAM SNEAKING tests): the round's screens answered (MenuButtons); alive, a teammate whose copy lies
// knocked out (the state flag 0x99: word 4 bit 25 of +0xB2C) for SIM_TSNE_WAKE_AFTER frames (120) is woken: the pad's
// axes measured, this player put 400 units beyond SIM_TSNE_WAKE_DIST (250) from him, walked toward him (the axes measured
// again when he does not close in) to that distance or 65 or 130 units nearer (by the try), R2 tapped with
// SIM_TSNE_WAKE_R2 (the weapon put away), then circle taps (punches, a kick) and triangle taps, again from another side
// until the copy is up (20 tries at most); SIM_TSNE_WAKE_MAX teammates woken at most (1); with SIM_TSNE_TARGET, =tsne's
// targets and goals while this player sneaks
static int TeamOfObject(uint32_t o) {
    const uint32_t pt = o ? PsR32(o + 0x920) & 0x01ffffff : 0;
    return pt >= 0x100000 && pt < 0x01ff0000 ? ps2::gMem[pt + 1] : -1;
}
static bool KnockedOut(uint32_t o) { return o && (int16_t)PsR16(o + 0xb64) > 0 && ((PsR32(o + 0xb2c + 16) >> 25) & 1); }
static uint32_t WakeButtons() {
    Autopilot& a = gAuto;
    static int woken = 0, outSince = -100000, tries = 0, target = 0;
    static const int after = getenv("SIM_TSNE_WAKE_AFTER") ? atoi(getenv("SIM_TSNE_WAKE_AFTER")) : 120;
    static const int most = getenv("SIM_TSNE_WAKE_MAX") ? atoi(getenv("SIM_TSNE_WAKE_MAX")) : 1;
    static const float wakeDist = getenv("SIM_TSNE_WAKE_DIST") ? (float)atof(getenv("SIM_TSNE_WAKE_DIST")) : 250.f;   // where the taps begin
    float me[3], tg[3];
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || KnockedOut(self) || !ps2world::PlayerPos(0, me)) { a.phase = 0; a.t = 0; return 0; }
    ++a.t;
    if (a.phase <= 1) {                                 // the pad's axes: up (0) then right (1)
        if (a.t == 1) memcpy(a.p0, me, sizeof me);
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        const float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) { a.t = 0; return 0; }
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0;
        return 0;
    }
    if (woken >= most) return 0;
    int pick = 0;                                       // a knocked out teammate (his copy here)
    for (int w = 1; w <= 16 && !pick; ++w) {
        const uint32_t o = ps2world::PlayerObject(w);
        if (!o) break;
        if (KnockedOut(o) && TeamOfObject(o) == TeamOfObject(self)) pick = w;
    }
    if (target && pick != target) {                     // the one being woken is up (by a hit: alive, his gauge high;
        const uint32_t o = ps2world::PlayerObject(target);   // a new round's spawn does not count)
        const bool up = o && (int16_t)PsR16(o + 0xb64) > 0 && (int16_t)PsR16(o + 0xb68) >= 15000 && tries > 0;
        char b[160]; snprintf(b, sizeof b, "wake: teammate %d %s after %d tries (%d frames knocked out here, stun gauge %d)", target,
                              up ? "up" : "no longer knocked out (not by this player)", tries, gFrame - outSince, o ? (int16_t)PsR16(o + 0xb68) : -1);
        AutoLog(b, 0); if (up) ++woken;
        target = 0; a.phase = 2; a.t = 0; return 0;
    }
    if (!pick) { outSince = -100000; target = 0; a.phase = 2; a.t = 0; return 0; }
    if (pick != target) {
        target = pick; outSince = gFrame; tries = 0; a.phase = 2; a.t = 0;
        const uint32_t o = ps2world::PlayerObject(pick);
        char b[160]; snprintf(b, sizeof b, "wake: teammate %d (slot %u) knocked out (stun gauge %d, state words %x %x)", pick, ps2::gMem[o + 0xb60],
                              (int16_t)PsR16(o + 0xb68), PsR32(o + 0xb2c), PsR32(o + 0xb2c + 16));
        AutoLog(b, 0);
    }
    if (gFrame - outSince < after || !ps2world::PlayerPos(target, tg)) return 0;
    const float dx = tg[0] - me[0], dz = tg[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t b = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) b |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) b |= r > 0 ? 0x20 : 0x80;
        return b;
    };
    switch (a.phase) {
    case 2: {                                           // 400 units beyond the stop distance, a side a try
        const float ang = 0.9f * (float)tries, far = 400.f + wakeDist;
        const float to[3] = {tg[0] + far * cosf(ang), tg[1] + 5.f, tg[2] + far * sinf(ang)};
        const int n = TeleportPlayer(me, to);
        ++tries; a.phase = 3; a.t = 0; a.shots = 0;
        char b[128]; snprintf(b, sizeof b, "wake: try %d: moved to %.0f %.0f %.0f (%d copies)", tries, to[0], to[1], to[2], n);
        AutoLog(b, dist);
        return 0; }
    case 3:                                             // settled, then steps toward him (his facing) to 300 units;
        if (a.t <= 20) { a.best = dist; return 0; }     // no nearer after 12 frames: the pad's axes measured again
        if (a.t <= 90 && dist > wakeDist - 65.f * (float)(tries % 3)) {
            if (a.t % 12 == 0) {
                if (dist > a.best - 20.f && a.shots < 3) { ++a.shots; a.phase = 7; a.t = 0; memcpy(a.p0, me, sizeof me); return 0; }
                a.best = dist;
            }
            return toward(dx, dz);
        }
        {
            const uint32_t o = ps2world::PlayerObject(target);
            char b[160]; snprintf(b, sizeof b, "wake: at %.0f units from him: circle (his state words %x %x, stun gauge %d)", dist,
                                  o ? PsR32(o + 0xb2c) : 0, o ? PsR32(o + 0xb2c + 16) : 0, o ? (int16_t)PsR16(o + 0xb68) : -1);
            AutoLog(b, dist);
        }
        PunchPictures();
        a.phase = getenv("SIM_TSNE_WAKE_R2") ? 9 : 4; a.t = 0; a.shots = 0;
        return 0;
    case 9:                                             // SIM_TSNE_WAKE_R2: R2 tapped (the weapon put away: bare hands)
        if (a.t <= 4) return 0x0200;
        if (a.t < 30) return 0;
        a.phase = 4; a.t = 0;
        return 0;
    case 7: case 8: {                                   // the axes again: up (7), then right (8)
        if (a.t == 1) memcpy(a.p0, me, sizeof me);
        if (a.t <= 8) return a.phase == 7 ? 0x10 : 0x20;
        if (a.t < 14) return 0;
        const float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n >= 1.f) { float* v = a.phase == 7 ? a.up : a.right; v[0] = vx / n; v[1] = vz / n; }
        if (a.phase == 7) { a.phase = 8; a.t = 0; return 0; }
        a.phase = 3; a.t = 21; a.best = dist;
        return 0; }
    case 4:                                             // circle taps (=cqc's): punches, a kick; then triangle (the
        if (a.t <= 36) return (a.t % 6) < 3 ? 0x2000 : 0;   // action button) taps beside him
        if (a.t <= 60) return (a.t % 8) < 4 ? 0x1000 : 0;
        a.phase = 5; a.t = 0;
        return 0;
    default:                                            // wait, then again from another side
        if (a.t < 40) return 0;
        a.phase = tries >= 20 ? 6 : 2; a.t = 0;
        if (tries >= 20) AutoLog("wake: no wake after 20 tries", dist);
        return 0;
    }
}
// SIM_TSNE_DUEL=s,n (=wake, TEAM SNEAKING tests): a defending life (not knocked out in it), from s seconds after its
// weapons' choice, =duel's n shots on the nearest living enemy (put 3.5 m from him, aimed 30 frames, fired), once a
// run; a picture in each aim (aim1, aim2 ..); SIM_TSNE_DUEL_SKIP=<slot>: that player only when no other enemy lives;
// SIM_TSNE_DUEL=s,n,1: at most n shots, the duel ends at the first hit of this player on a sneaker (one burst hits)
static bool TsneDuelNow() {
    static const char* e = getenv("SIM_TSNE_DUEL");
    if (!e) return false;
    static const int startAt = (int)(atof(e) * 60), most = strchr(e, ',') ? atoi(strchr(e, ',') + 1) : 3;
    static const bool untilHit = strchr(e, ',') && strchr(strchr(e, ',') + 1, ',') && atoi(strchr(strchr(e, ',') + 1, ',') + 1) == 1;
    static int onAt = -1;
    static int since = -1; static bool on = false, done = false, ko = false;
    const uint32_t self = ps2world::PlayerObject(0);
    if (!self || (int16_t)PsR16(self + 0xb64) <= 0 || ps2world::MenuScreen() == 4) { since = -1; on = false; ko = false; return false; }
    if (since < 0) since = gFrame;
    if (KnockedOut(self)) ko = true;
    if (done || ko || ps2world::TsneRole() != 0 || gFrame - since < startAt) return false;
    Autopilot& a = gAuto;
    if (!on) {
        static const int skip = getenv("SIM_TSNE_DUEL_SKIP") ? atoi(getenv("SIM_TSNE_DUEL_SKIP")) : -1;   // a slot left alone
        float me[3]; float best = 1e18f; int pick = 0;
        if (!ps2world::PlayerPos(0, me)) return false;
        for (int pass = 0; pass < 2 && !pick; ++pass)
            for (int w = 1; w <= 16; ++w) {
                const uint32_t o = ps2world::PlayerObject(w);
                if (!o) break;
                float q[3];
                if ((int16_t)PsR16(o + 0xb64) <= 0 || TeamOfObject(o) == TeamOfObject(self) || !ps2world::PlayerPos(w, q)) continue;
                if (pass == 0 && skip >= 0 && (PsR16(o + 0xb60) & 0xff) == skip) continue;
                const float d = (q[0] - me[0]) * (q[0] - me[0]) + (q[2] - me[2]) * (q[2] - me[2]);
                if (d < best) { best = d; pick = w; }
            }
        if (!pick) return false;
        on = true; a = Autopilot(); gTarget = pick; onAt = gFrame;
        char b[96]; snprintf(b, sizeof b, "duel: SIM_TSNE_DUEL on remote %d, %d shots", pick, most); AutoLog(b, sqrtf(best));
    }
    if (untilHit && gTsneHitAt >= onAt && gTsneHitBy == (PsR16(self + 0xb60) & 0xff)) {
        char b[96]; snprintf(b, sizeof b, "duel: shot %d hit the sneaker (f%d): no more shots", a.shots, gTsneHitAt);
        done = true; a = Autopilot(); AutoLog(b, 0); AutoLog("duel: SIM_TSNE_DUEL done", 0); return false;
    }
    if (a.shots >= most) { done = true; a = Autopilot(); AutoLog("duel: SIM_TSNE_DUEL done", 0); return false; }
    if (a.phase == 4 && a.t == 0) { char n[16]; snprintf(n, sizeof n, "aim%d", a.shots + 1); ps2world::TsneTestPicture(n, 28); }
    return true;
}
#include "fair_tour.h"                                  // =fair (FAIR's tours)
static uint32_t AutoButtons(int mode) {
    Autopilot& a = gAuto;
    if (mode == 20) {                                   // =capture: a duel in the other rules
        if (ps2world::ActorByName("NewOnlineItemChara2")) return CaptureButtons();
        mode = 3;
    }
    static const bool tsneElse = !getenv("SIM_TSNE_ELSE") || atoi(getenv("SIM_TSNE_ELSE")) != 0;
    if (mode == 13 && tsneElse && ps2world::TsneRole() < 0) { mode = 7; gTsneAlive = false; }   // =tsne in another rule's round: =play
    if (mode == 21) { gFairStick[0] = gFairStick[1] = -1; const uint32_t m = MenuButtons(); return m != 0x80000000u ? m : FairButtons(); }
    if (mode == 11) {
        if (a.takenAtStart < 0) a.takenAtStart = ps2world::BoxesTaken();
        PickupTrack(); const uint32_t m = MenuButtons(); return m != 0x80000000u ? m : PickupButtons();
    }
    if (mode == 12) {                                   // =spawn: the menus answered until he is in the map
        if (a.madeAtStart < 0) a.madeAtStart = ps2world::LocalWeaponsMade();
        const uint32_t m = MenuButtons();
        if (m != 0x80000000u) return m;
        static int lastStart = -100000;                 // alive in the menus: START on the weapon choice (any version)
        const int screen = ps2world::MenuScreen();      // (not in the map: screen 0, nor its START menu: 6)
        a.t = screen == 0 ? a.t + 1 : 0;                // alive in the map that many frames: the step ends at 60
        if (ps2world::LocalWeaponsMade() <= a.madeAtStart && screen != 0 && screen != 6 && gFrame - lastStart > 150) { lastStart = gFrame; return 0x8; }
        return 0;
    }
    if (mode == 14) { const uint32_t m = MenuButtons(); CarryWatch(); return m != 0x80000000u ? m : CarryButtons(); }
    if (mode == 15) { const uint32_t m = MenuButtons(); return m != 0x80000000u ? m : 0; }   // =idle: the screens answered, then still
    if (mode == 16) { const uint32_t m = MenuButtons(); return m != 0x80000000u ? m : VisitButtons(); }
    if (mode == 17) {                                   // =bc: the round's menus between two lives
        BcPlanTrack();
        const uint32_t m = MenuButtons();
        if (m != 0x80000000u) { gBcAlive = false; return m; }
        if (const uint32_t p = BcPageButtons()) { gBcAlive = false; return p == 0x80000000u ? 0 : p; }
        return BcButtons();
    }
    if (mode == 18) {                                   // =rot (the integration's matrix): =tsne in a TEAM SNEAKING round,
        BcPlanTrack();                                  // =bc in a BASE CAPTURE one, else =play
        float tp[4][3], bp[5][3];
        if (ps2world::TsnePoints(tp) >= 2) mode = 13;
        else if (ps2world::BcBases(bp) > 0) mode = 17;
        else mode = 7;
        if (mode != 7) {
            const uint32_t m = MenuButtons();
            if (m != 0x80000000u) { gTsneAlive = false; gBcAlive = false; return m; }
            return mode == 13 ? TsneButtons() : BcButtons();
        }
    }
    if (mode == 13) {                                   // =tsne: a round's menus between two lives (a new map, a new round)
        const uint32_t m = MenuButtons();
        if (m != 0x80000000u) { gTsneAlive = false; return m; }
        return TsneButtons();
    }
    if (mode == 19) {                                   // =wake (with SIM_TSNE_TARGET: =tsne's while this player sneaks)
        const bool duel = TsneDuelNow();                // SIM_TSNE_DUEL (its life tracked through the menus too)
        const uint32_t m = MenuButtons();
        if (m != 0x80000000u) { gTsneAlive = false; return m; }
        if (getenv("SIM_TSNE_TARGET") && ps2world::TsneRole() == 1) return TsneButtons();
        if (!duel) return WakeButtons();
        mode = 3;                                       // SIM_TSNE_DUEL: =duel's aim and shots
    }
    if (mode == 5) { const uint32_t m = MenuButtons(); CarryWatch(); return m != 0x80000000u ? m : LadderButtons(); }
    if (mode == 6) return WatchLadderButtons();
    if (mode == 9) { const uint32_t m = MenuButtons(); return m != 0x80000000u ? m : RailButtons(); }
    if (mode == 20) { const uint32_t m = MenuButtons(); return m != 0x80000000u ? m : M15Buttons(); }
    if (mode == 10) { const uint32_t m = MenuButtons(); return m != 0x80000000u ? m : WatchPlayerButtons(); }
    if (mode == 7 || mode == 8) {
        const uint32_t b = PlayButtons(mode);
        if (b != 0x80000000u) return b;
        mode = mode == 7 ? 1 : 2;
    }
    float me[3], tg[3];
    static const int duelSlot = getenv("SIM_DUEL_SLOT") ? atoi(getenv("SIM_DUEL_SLOT")) : -1;   // the target by its slot (netcode tests)
    if (duelSlot >= 0)
        for (int w = 1; w <= 16; ++w) { const uint32_t o = ps2world::PlayerObject(w); if (!o) break; if (ps2::gMem[o + 0xb60] == duelSlot) { gTarget = w; break; } }
    if (!ps2world::PlayerPos(0, me)) return (gFrame % 120) < 4 ? 0x2000 : 0;   // not in the world: the menus (circle)
    if (gFrame < a.circleUntil) return 0x2000;
    if (MenuCircle(me)) return 0x2000;
    if (!ps2world::PlayerPos(gTarget, tg)) return (gFrame % 120) < 4 ? 0x2000 : 0;   // no one to face (a menu, a respawn)
    if (a.tpLeft > 0) {                                  // =duel: a copy a tick behind (60: a picture between) moved too
        --a.tpLeft;
        if (const int n = TeleportPlayer(a.tpFrom, a.tpTo, 150.f)) { char b[96]; snprintf(b, sizeof b, "duel: %d more copies moved", n); AutoLog(b, 0); }
    }
    const float dx = tg[0] - me[0], dz = tg[2] - me[2], dist = sqrtf(dx * dx + dz * dz);
    // guns fire from close in: the facing after the approach points at the target (no turn to guess)
    const bool cqc = mode == 2 || mode == 4;
    const float range = cqc ? 900.f : mode == 3 ? 1000.f : 1600.f;
    if (a.havePrev) {                                   // the facing: the direction of the last real step
        const float mx = me[0] - a.prev[0], mz = me[2] - a.prev[2], m = sqrtf(mx * mx + mz * mz);
        if (m > 4.f) { a.face[0] = mx / m; a.face[1] = mz / m; a.faced = true; }
    }
    memcpy(a.prev, me, sizeof me); a.havePrev = true;
    auto toward = [&](float x, float z) -> uint32_t {
        const float u = x * a.up[0] + z * a.up[1], r = x * a.right[0] + z * a.right[1];
        uint32_t b = 0;
        if (fabsf(u) > 0.4f * fabsf(r)) b |= u > 0 ? 0x10 : 0x40;
        if (fabsf(r) > 0.4f * fabsf(u)) b |= r > 0 ? 0x20 : 0x80;
        return b;
    };
    auto facingError = [&]() -> float {                 // degrees between the facing and the target
        if (!a.faced || dist < 1.f) return 180.f;
        const float c = (a.face[0] * dx + a.face[1] * dz) / dist;
        return acosf(fmaxf(-1.f, fminf(1.f, c))) * 57.2958f;
    };
    ++a.t;
    if ((mode == 3 || mode == 4) && a.teleported && a.tpTries < 8 && gFrame - a.lastTp == 40 && fabsf(me[1] - tg[1]) > 150.f) {
        ++a.tpTries; a.lastTp = -100000;                  // another level than the target's: again, from another side
        char b[96]; snprintf(b, sizeof b, "duel: %.0f above the target: moved again", me[1] - tg[1]); AutoLog(b, dist);
        a.phase = 2; a.t = 0; a.drive = 0;
    }
    if (cqc && getenv("SIM_CQC_STOP")) {                // SIM_CQC_STOP=v: no more attacks once the target's stun gauge is v or less
        static bool stopped = false;
        const uint32_t o = ps2world::PlayerObject(1);
        const int stun = o ? (int16_t)PsR16(o + 0xb68) : 30000;
        if (!stopped && o && stun <= atoi(getenv("SIM_CQC_STOP"))) { stopped = true; AutoLog("target's stun gauge down: attacks stopped", dist); }
        if (stopped) return 0;
    }
    switch (a.phase) {
    case 0: case 1: {                                   // measure up (0) or right (1)
        if (a.t == 1) { memcpy(a.p0, me, sizeof me); }
        if (a.t <= 10) return a.phase == 0 ? 0x10 : 0x20;
        if (a.t < 16) return 0;
        float vx = me[0] - a.p0[0], vz = me[2] - a.p0[2], n = sqrtf(vx * vx + vz * vz);
        if (n < 1.f) {                                  // no move yet (a menu, a wall): try again; a menu's Circle every other try
            if (++a.still % 2 == 0) { a.circleUntil = gFrame + 4; AutoLog("no move: Circle (a menu?)", dist); }
            a.t = 0; return 0;
        }
        a.still = 0;
        float* v = a.phase == 0 ? a.up : a.right;
        v[0] = vx / n; v[1] = vz / n;
        if (a.phase == 1) { char b[128]; snprintf(b, sizeof b, "up = (%.2f, %.2f), right = (%.2f, %.2f)", a.up[0], a.up[1], a.right[0], a.right[1]); AutoLog(b, dist); }
        a.phase = a.phase == 0 ? 1 : 2; a.t = 0; a.drive = 0;
        return 0; }
    case 2:                                             // walk toward the target, around walls
        // =duel: 3.5 m from the target on its level (again after a respawn, at most every 10 s); =punch: 1.5 m from it
        if ((mode == 3 || mode == 4) && (dist > (mode == 4 ? 600.f : 1500.f) || a.lastTp == -100000) && gFrame - a.lastTp > 600) {
            a.teleported = true; a.lastTp = gFrame;
            const float ang = a.tpTries * 1.5708f + 0.3f * (a.tpTries / 4), k = (mode == 4 ? 150.f : 350.f) / std::max(dist, 1.f);
            const float ox = -(dx * cosf(ang) - dz * sinf(ang)) * k, oz = -(dx * sinf(ang) + dz * cosf(ang)) * k;
            const float to[3] = {tg[0] + ox, tg[1], tg[2] + oz};
            const int n = TeleportPlayer(me, to);
            memcpy(a.tpFrom, me, sizeof a.tpFrom); memcpy(a.tpTo, to, sizeof a.tpTo); a.tpLeft = 20;
            char b[128]; snprintf(b, sizeof b, "duel: moved from %.0f %.0f %.0f to %.0f %.0f %.0f (%d copies)", me[0], me[1], me[2], to[0], to[1], to[2], n);
            AutoLog(b, dist);
            return 0;
        }
        if (dist < range) { a.phase = 4; a.t = 0; a.best = 1e9f; AutoLog(cqc ? "close: punches" : "close: fire", dist); if (cqc) PunchPictures(); return 0; }
        if (++a.drive > 1200) { a.phase = 0; a.t = 0; a.best = 1e9f; return 0; }
        if (a.detour > 0) {                             // a side step along the wall, then toward it again
            --a.detour;
            return toward(-dz * a.side, dx * a.side);
        }
        if (dist < a.best - 20.f) { a.best = dist; a.stuck = 0; }
        else if (++a.stuck > 30) { a.stuck = 0; a.detour = 45; a.side = -a.side; a.best = dist; AutoLog("blocked: side step", dist); }
        return toward(dx, dz);
    case 3: {                                           // turn to face it: short taps until the facing points at it
        const float err = facingError();
        if ((err < 12.f && a.t > 4) || a.t > 40) {
            char b[96]; snprintf(b, sizeof b, "facing error %.0f degrees after %d frames: %s", err, a.t, cqc ? "punch" : "fire"); AutoLog(b, dist);
            a.phase = 4; a.t = 0; return 0;
        }
        return (a.t % 4) < 2 ? toward(dx, dz) : 0; }
    case 4:                                             // fire: square held then released / punches
        if (cqc) { if (a.t <= 24) return (a.t % 6) < 3 ? 0x2000 : 0; }
        else if (mode == 3) {                           // =duel: face it with a short walk, then aim and fire
            if ((a.t == 4 || a.t == 30) && getenv("SIM_FIRE_CALLS") && a.shots < 2 && !gFrameCallsLeft) {   // walk / firing frames' calls
                gFrameCalls.clear(); gFrameCallsLeft = 3; gFrameCallsTag = a.t == 4 ? "walk" : "fire"; ps2::gCallHook = &FrameCallsHook;
            }
            if (a.t <= 10) return toward(dx, dz);
            if (a.t <= 40) return 0x8000;
            if (a.t == 41 && getenv("SIM_FIRE_TREE") && a.shots < 2) {   // the release: that frame's calls of a function
                ps2::gTreeRoot = (uint32_t)strtoul(getenv("SIM_FIRE_TREE"), nullptr, 16); ps2::gTreeLines = 40000; ps2::gTreeArmed = true;
                AutoLog("fire: call tree armed", dist);
            }
        }
        else if (a.t <= 25) return 0x8000;
        a.phase = 5; a.t = 0; ++a.shots;
        return 0;
    default:                                            // wait, then again (the mapping measured again first)
        if (a.t < 20) return 0;
        a.phase = dist < range ? (a.shots % 4 == 0 ? 0 : 3) : 2; a.t = 0; a.drive = 0;
        return 0;
    }
}
// SIM_SQUARE="frame:pressure,...": from each frame on, Square held at that pressure (0 released),
// on top of the steps (the weapon readied at a light pressure, fired from a full one)
static std::vector<std::pair<int, int>> gSquare;
static int SquareNow() {
    int v = -1;
    for (auto& e : gSquare) if (gFrame >= e.first) v = e.second;
    return v;
}
static uint32_t PadHeldSteps(int port);
static bool gWalkNow = false;                             // SIM_WALK_AFTER_SPAWN: up held now
// SIM_VOICE_TALK="start,frames,period": SELECT held from frame start for that many frames, again every
// period frames (0: once); held a second in a match, it turns the voice chat's talking on (docs/VOICE.md)
static bool TalkHeld() {
    static int start = -2, len = 0, period = 0;
    if (start == -2) {
        start = -1;
        if (const char* t = getenv("SIM_VOICE_TALK")) sscanf(t, "%d,%d,%d", &start, &len, &period);
    }
    if (start < 0 || gFrame < start) return false;
    const int k = period > 0 ? (gFrame - start) % period : gFrame - start;
    return k < len;
}
// SIM_PRESS_FILE=<file> holding "k mask frames [mask frames ...]" (tests): a new k holds those pad bits (hex;
// START is 8, CROSS 4000, UP 10, DOWN 40) alone for that many frames, each pair after the one before, the
// steps' and the autopilot's buttons left out meanwhile (a match's START menu)
static bool PressNow(uint32_t& bits) {
    static const char* pf = getenv("SIM_PRESS_FILE");
    static int done = 0, seen = -100000;
    static std::vector<std::pair<uint32_t, int>> seq;    // (mask, until frame)
    if (!pf) return false;
    if (gFrame - seen >= 5) {
        seen = gFrame;
        if (FILE* fp = fopen(pf, "rb")) {
            int k = 0;
            if (fscanf(fp, "%d", &k) == 1 && k > done) {
                done = k; seq.clear();
                unsigned m = 0; int n = 0, at = gFrame;
                while (fscanf(fp, "%x %d", &m, &n) == 2 && n > 0) { at += n; seq.push_back({m, at}); }
                std::string d;
                for (const auto& e : seq) { char b[32]; snprintf(b, sizeof b, " %x until %d", e.first, e.second); d += b; }
                printf("SIM: press%s (k %d, from frame %d)\n", d.c_str(), k, gFrame);
            }
            fclose(fp);
        }
    }
    while (!seq.empty() && gFrame >= seq.front().second) seq.erase(seq.begin());
    if (seq.empty()) return false;
    bits = seq.front().first;
    return true;
}
// SIM_PC_READY=1 (tests): the steps' pad as the PC's controls reach MGO1.exe's front end with ready_weapon=1, turned
// into the PS2 pad by its own code (native/ready_weapon.h) once a VBlank: L3 (2) toggles the weapon readied, SELECT (1)
// is READY WEAPON held (the keyboard's), Square (8000) the fire button, R3 (4) R3; logged on each change
static int PcReady() { static const int on = getenv("SIM_PC_READY") ? atoi(getenv("SIM_PC_READY")) : 0; return on; }
static ready::Weapon gPcReady;
static ready::Out gPcReadyOut;
static uint32_t PadHeldRaw(int port);
static uint32_t PadHeld(int port) {
    if (!port && doll::Control()) return doll::CurrentPad().buttons;   // the PLAYABLE DOLL plays (doll.h)
    const uint32_t raw = PadHeldRaw(port);
    if (port || !PcReady()) return raw;
    uint32_t b = raw & ~0x3u;                                 // L3 and READY WEAPON: the front end's
    if (gPcReadyOut.own) { b &= ~0x8004u; if (gPcReadyOut.square) b |= 0x8000; if (gPcReadyOut.r3) b |= 0x4; }
    return b;
}
static void PcReadyFrame(int f) {
    const uint32_t raw = PadHeldRaw(0);
    ready::In in;
    in.match = ps2world::InMatch(); in.toggle = raw & 0x2; in.hold = raw & 0x1; in.fire = raw & 0x8000;
    in.r3 = raw & 0x4; in.windows = raw & 0x300;
    ps2world::LocalWeapon(in.aimed, in.reloadable, in.gun);
    gPcReadyOut = gPcReady.Step(in);
    if (const char* e = gPcReady.Event()) printf("pc ready frame %d dg %lld: %s\n", f, ps2world::DgFrames(), e);
    const uint32_t pl = ps2world::PlayerObject(0) & 0x01ffffff;
    const uint32_t d = pl ? PsR32(pl + 0x608) & 0x01ffffff : 0;
    const int id = d ? PsR32(d + 0x14) & 0xff : -1;
    const int mag = d ? (int16_t)PsR16(d + 0x1c) : 0, size = d ? (int16_t)PsR16(d + 0x1e) : 0, all = d ? (int16_t)PsR16(d + 0x18) : 0;
    char now[160];
    snprintf(now, sizeof now, "pad %s %02x%s, game aimed %d reloadable %d, weapon %d magazine %d of %d (all %d) (steps %04x)",
             !gPcReadyOut.own ? "controls" : gPcReadyOut.square ? "square" : "up", gPcReadyOut.square ? gPcReadyOut.pressure : 0,
             gPcReadyOut.r3 ? " r3" : "", in.aimed, in.reloadable, id, mag, size, all, raw & 0xffff);
    static std::string last;
    if (last != now) { last = now; printf("pc ready frame %d dg %lld: %s\n", f, ps2world::DgFrames(), now); }
}
static uint32_t PadHeldRaw(int port) {
    uint32_t pressed = 0;
    if (!port && ps2world::TsneTestPad() != ~0u) return ps2world::TsneTestPad();   // SIM_TSNE_PAD (tsne.h): its L2 alone
    if (!port && PressNow(pressed)) return pressed;
    uint32_t b = PadHeldSteps(port);
    if (!port && gWalkNow) b |= 0x10;
    if (!port && TalkHeld()) b |= 0x1;
    const int sq = port ? -1 : SquareNow();
    if (sq > 0) b |= 0x8000;
    return b;
}
// the pressure of a held button: a chord's of this frame, else its step's, else full
static int StepPressure(uint32_t bit) {
    if (gPressFrom < 0 || gPressAuto || gFrame < gPressFrom || gFrame >= gPressFrom + gPressHold) return 0xff;
    for (const auto& k : gPressChords)
        if ((k.buttons & bit) && gFrame >= gPressFrom + k.from && gFrame < gPressFrom + k.from + k.len) return k.pressure >= 0 ? k.pressure : 0xff;
    return ((uint32_t)gPressButtons & bit) && gPressPressure >= 0 ? gPressPressure : 0xff;
}
static bool PadAnalog(int port, uint8_t sticks[4], uint8_t pressures[12]) {
    if (port) return false;
    if (doll::Control()) {
        // the doll's pad: a held button without a pressure of its own is pressed fully (the d-pad
        // counts in the menus only with a pressure; Square light readies the weapon)
        const doll::Pad& d = doll::CurrentPad();
        memcpy(sticks, d.sticks, 4);
        for (int i = 0; i < 12; ++i) pressures[i] = d.pressures[i] ? d.pressures[i] : ((d.buttons >> (4 + i)) & 1) ? 0xFF : 0;
        return true;
    }
    memset(sticks, 0x80, 4);
    if (gPressFrom >= 0 && !gPressAuto && gFrame >= gPressFrom && gFrame < gPressFrom + gPressHold)
        for (int k = 0; k < 4; ++k) if (gPressStick[k] >= 0) sticks[k] = (uint8_t)gPressStick[k];
    // SIM_STICK_SPIN=<frames> (tests): a step's left stick pushed all the way turns round once in that many frames (a
    // run in a small circle, clear of the walls)
    static const int spin = getenv("SIM_STICK_SPIN") ? atoi(getenv("SIM_STICK_SPIN")) : 0;
    if (spin > 0 && gPressFrom >= 0 && !gPressAuto && gFrame >= gPressFrom && gFrame < gPressFrom + gPressHold && gPressStick[2] >= 0 && gPressStick[3] >= 0) {
        const double a = 6.283185307179586 * (gFrame - gPressFrom) / spin;
        sticks[2] = (uint8_t)std::lround(128 + 127 * sin(a)); sticks[3] = (uint8_t)std::lround(128 - 127 * cos(a));
    }
    if (gPlayTurn >= 0 && gPressAuto == 7 && gWasAlive && gFrame >= gPressFrom && gFrame < gPressFrom + gPressHold)
        sticks[0] = (uint8_t)gPlayTurn;                      // =play, SIM_PLAY_TURN: the camera turned (the view all round)
    if (gPressAuto == 21 && gFairStick[0] >= 0 && gFrame >= gPressFrom && gFrame < gPressFrom + gPressHold) { sticks[2] = (uint8_t)gFairStick[0]; sticks[3] = (uint8_t)gFairStick[1]; }   // =fair: the left stick
    const uint32_t b = PadHeld(port);
    for (int i = 0; i < 12; ++i) pressures[i] = (b >> (4 + i) & 1) ? (uint8_t)StepPressure(1u << (4 + i)) : 0;
    const int sq = SquareNow();
    if (sq >= 0) pressures[11] = (uint8_t)sq;
    if (PcReady() && gPcReadyOut.own) pressures[11] = gPcReadyOut.square ? gPcReadyOut.pressure : 0;   // SIM_PC_READY
    return true;
}
static uint32_t PadHeldSteps(int port) {
    if (port) return 0;
    if (const char* h = getenv("SIM_WHOLD")) if (*h && *h != '0') {   // tests (=setenv:SIM_WHOLD:<hex>): R2 tapped until that weapon is held
        const uint32_t me = ps2world::PlayerObject(0), d = me ? PsR32(me + 0x608) & 0x01ffffff : 0;
        const int want = (int)strtoul(h, nullptr, 16), held = d >= 0x100000 && d < 0x01fff000 ? ps2::gMem[d + 0x14] : -1;
        if (held != want) return gFrame % 40 < 8 ? 0x0200u : 0u;
    }
    if (gPressAuto && gPressFrom >= 0 && gFrame >= gPressFrom && gFrame < gPressFrom + gPressHold) return AutoButtons(gPressAuto);
    if (gPressFrom >= 0 && gFrame >= gPressFrom && gFrame < gPressFrom + gPressHold) {
        uint32_t b = (uint32_t)gPressButtons;
        if (gPressTap > 0) {                                 // a match at 60 counts two DG frames a 1/30 s
            static const int perTap = getenv("SIM_MATCH_FPS") && atoi(getenv("SIM_MATCH_FPS")) == 60 ? 2 : 1;
            if ((ps2world::DgFrames() - gPressDg) / (gPressTap * perTap) % 2) b = 0;
        }
        for (const auto& k : gPressChords)                   // a chord at pressure 0 releases its buttons for its frames
            if (gFrame >= gPressFrom + k.from && gFrame < gPressFrom + k.from + k.len) b = k.pressure == 0 ? (b & ~k.buttons) : (b | k.buttons);
        return b;
    }
    for (auto& s : gScript) if (gFrame >= s.first && gFrame < s.first + 4) return s.second;
    return 0;
}
// The root's options as the script passes them: the [ccbefe] call of the
// region case varbuf[0x10F5] selects (3 = US retail) in ntmenu2.gcx, read from
// the script bytes, with the SaveMGO slots written as savemgo_patch.py writes
// them on the installed page. GCL: param tag 0x5N (size N, 0xD = next byte),
// letter, values 07 LL string, 01 int16, 08 uint16, 09/0A int32, 0xC0+n+1
// immediate n.
static bool RootOptionsFromScript(const char* dir, int regionCase) {
    std::string path = std::string(dir) + "/ntmenu2.gcx";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> g; unsigned char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) g.insert(g.end(), buf, buf + n);
    fclose(f);
    static const char* slots[][2] = {
        {"https://mgs3sweb.konamionline.com/us/mgs3/", "http://savemgo.com/us/mgs3/"},
        {"mgs3sgate02.konamionline.com", "107.161.80.126"},
        {"mgs3sstun.konamionline.com", "15.204.66.207"}};
    for (auto& sl : slots) {
        std::string old = sl[0]; old.push_back('\0');
        for (size_t i = 2; i + old.size() <= g.size(); ++i) {
            if (memcmp(&g[i], old.data(), old.size()) != 0 || g[i - 2] != 0x07 || g[i - 1] < old.size()) continue;
            size_t ll = g[i - 1];
            memset(&g[i], 0, ll); memcpy(&g[i], sl[1], strlen(sl[1]));
        }
    }
    static const unsigned char sw[] = {0x12, 0x00, 0x10, 0xF5};
    size_t at = 0;
    for (size_t i = 0; i + 4 <= g.size(); ++i) if (memcmp(&g[i], sw, 4) == 0 && i > 0x10000) { at = i; break; }
    if (!at) return false;
    size_t cs = 0;
    for (size_t i = at; i + 2 <= g.size() && i < at + 0x400; ++i)
        if (g[i] == 0x63 && g[i + 1] == (unsigned char)(0xC1 + regionCase)) { cs = i; break; }
    if (!cs) return false;
    size_t p = 0;
    for (size_t i = cs; i + 4 <= g.size() && i < cs + 0x40; ++i)
        if (g[i] == 0x06 && g[i + 1] == 0xFE && g[i + 2] == 0xBE && g[i + 3] == 0xCC) { p = i + 4; break; }
    if (!p || g[p] != 0x06) return false;
    p += 4;                                                   // the class (strcode)
    printf("script: region case %d, [ccbefe] at %#zx:", regionCase, p);
    while (p < g.size() && (g[p] & 0xF0) == 0x50) {
        size_t size = g[p] & 0x0F; ++p;
        if (size == 0x0D) size = g[p++];
        char letter = (char)g[p];
        size_t v = p + 1, end = p + size;
        opts[letter];
        printf(" -%c", letter);
        while (v < end) {
            unsigned char t = g[v];
            if (t == 0x07) { const char* str = (const char*)&g[v + 2]; AddStr(letter, str); printf(" \"%s\"", str); v += 2 + g[v + 1]; }
            else if (t == 0x01) { int x = (int16_t)(g[v + 1] | (g[v + 2] << 8)); AddInt(letter, x); printf(" %d", x); v += 3; }
            else if (t == 0x08) { int x = g[v + 1] | (g[v + 2] << 8); AddInt(letter, x); printf(" %d", x); v += 3; }
            else if (t == 0x09 || t == 0x0A) { int x = (int)(g[v + 1] | (g[v + 2] << 8) | (g[v + 3] << 16) | ((unsigned)g[v + 4] << 24)); AddInt(letter, x); printf(" %d", x); v += 5; }
            else if (t >= 0xC0) { int x = (t & 0x3F) - 1; AddInt(letter, x); printf(" %d", x); v += 1; }
            else break;
        }
        p = end;
    }
    printf("\n");
    return !opts['u'].empty();
}
namespace ps2 { extern volatile uint32_t gLastCall, gLastCaller, gLoopFn; extern uint32_t gRing[64][2]; extern unsigned gRingPos, gRingThread; }
#include <windows.h>
// the machine's time in ms since 1970 (every instance reads the same)
static long long EpochMs() {
    FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
    const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)(t / 10000ULL) - 11644473600000LL;
}
// every 3 s: the last call, the PS2 thread running and the 12 calls before (a hang shows here)
static DWORD WINAPI Watchdog(LPVOID) {
    HANDLE f = CreateFileA("watchdog.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    for (;;) {
        Sleep(3000);
        char b[512]; int n = wsprintfA(b, "last call %08x from %08x, last loop in %08x, thread %u:", ps2::gLastCall, ps2::gLastCaller, ps2::gLoopFn, ps2::gRingThread);
        for (unsigned k = 0; k < 12; ++k) {
            const uint32_t* e = ps2::gRing[(ps2::gRingPos - 12 + k) & 63];
            n += wsprintfA(b + n, " %08x<%08x", e[0], e[1] - 8);
        }
        n += wsprintfA(b + n, "\r\n");
        // the last state only, no flush to the disk (v141 beta 4: it grew 4 MB a day, a disk flush every 3 s a process)
        DWORD w = 0; SetFilePointer(f, 0, nullptr, FILE_BEGIN); WriteFile(f, b, n, &w, nullptr); SetEndOfFile(f);
    }
}
// an exception nothing handles: its line in the log (an error in the bot's daily log: the module and
// offset, the address touched, the PS2 code's last calls), then the process ends with code 70
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* e) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 70);
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)r->ExceptionAddress, &mod);
    if (mod) GetModuleFileNameA(mod, name, sizeof name);
    const char* base = strrchr(name, '\\');
    base = base ? base + 1 : name;
    char b[1400];
    int n = wsprintfA(b, "CRASH: exception %08lx at %s+%lx", (unsigned long)r->ExceptionCode, base,
                      (unsigned long)((const char*)r->ExceptionAddress - (const char*)mod));
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        n += wsprintfA(b + n, " (%s %p)", r->ExceptionInformation[0] == 0 ? "reading" : r->ExceptionInformation[0] == 1 ? "writing" : "running",
                       (void*)r->ExceptionInformation[1]);
    n += wsprintfA(b + n, "; the PS2 code: last call %08x from %08x, last loop in %08x, thread %u; before it:", ps2::gLastCall,
                   ps2::gLastCaller - 8, ps2::gLoopFn, ps2::gRingThread);
    for (unsigned k = 0; k < 12; ++k) {
        const uint32_t* x = ps2::gRing[(ps2::gRingPos - 12 + k) & 63];
        n += wsprintfA(b + n, " %08x<%08x", x[0], x[1] - 8);
    }
    b[n++] = '\n';
    DWORD w = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), b, n, &w, nullptr);
    TerminateProcess(GetCurrentProcess(), 70);
    return EXCEPTION_EXECUTE_HANDLER;
}
// the steps (SIM_STEPS_FILE / SIM_STEPS): one per line, "text=buttons[/hold][+chord]@delay"
static void LoadSteps() {
    std::string stepText;
    if (const char* sf = getenv("SIM_STEPS_FILE")) {     // one step per line (texts may hold '|')
        if (FILE* f = fopen(sf, "rb")) { char buf[512]; while (fgets(buf, sizeof(buf), f)) { std::string l = buf; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back(); if (!l.empty()) { if (!stepText.empty()) stepText += '\x01'; stepText += l; } } fclose(f); }
        if (getenv("SIM_STEPS_DELETE")) DeleteFileA(sf);   // read once: a dedicated host's steps hold its password
    } else if (const char* st = getenv("SIM_STEPS")) {
        stepText = st; for (auto& ch : stepText) if (ch == '|') ch = '\x01';
    }
    if (!stepText.empty()) {
        std::string all = stepText; size_t pos = 0;
        while (pos <= all.size()) {
            size_t bar = all.find('\x01', pos); std::string one = all.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
            size_t eq = one.rfind('=');
            if (eq != std::string::npos) {
                Step x; x.text = one.substr(0, eq); x.delay = 20; x.buttons = 0;
                std::string rest = one.substr(eq + 1); size_t at = rest.rfind('@');
                if (at != std::string::npos) x.delay = atoi(rest.c_str() + at + 1);
                if (rest.compare(0, 3, "kb:") == 0) x.kb = rest.substr(3, at == std::string::npos ? std::string::npos : at - 3);
                else if (rest.compare(0, 5, "lang:") == 0) x.lang = atoi(rest.c_str() + 5);
                else if (rest.compare(0, 7, "setenv:") == 0) x.env = rest.substr(7, at == std::string::npos ? std::string::npos : at - 7);
                else if (rest.compare(0, 6, "stick:") == 0) {    // the sticks held; buttons only through chords
                    const char* q = rest.c_str() + 6;
                    for (int k = 0; k < 4; ++k) { char* e = nullptr; x.stick[k] = (int)strtoul(q, &e, 16); q = e; if (*q == ',') ++q; }
                    size_t sl = rest.find('/');
                    if (sl != std::string::npos && (at == std::string::npos || sl < at)) x.hold = atoi(rest.c_str() + sl + 1);
                    if (x.hold < 1) x.hold = 1;
                    for (size_t pl = rest.find('+'); pl != std::string::npos && (at == std::string::npos || pl < at); pl = rest.find('+', pl + 1)) {
                        Step::Chord k{}; char* e = nullptr;       // +mask[pNN]:from:len, pressed with the sticks
                        k.buttons = strtoul(rest.c_str() + pl + 1, &e, 16);
                        if (e && *e == 'p') k.pressure = (int)strtoul(e + 1, &e, 16);
                        if (e && *e == ':') { k.from = (int)strtol(e + 1, &e, 10); if (e && *e == ':') k.len = (int)strtol(e + 1, &e, 10); }
                        if (k.len < 1) k.len = 4;
                        x.chords.push_back(k);
                    }
                } else {
                    if (!rest.compare(0, 5, "chase")) x.autoMode = 1;
                    else if (!rest.compare(0, 4, "fair")) x.autoMode = 21;   // =fair: FAIR's tours (fair_tour.h)
                    else if (!rest.compare(0, 3, "cqc")) x.autoMode = 2;
                    else if (!rest.compare(0, 7, "duelcqc")) x.autoMode = 4;
                    else if (!rest.compare(0, 4, "duel")) x.autoMode = 3;
                    else if (!rest.compare(0, 5, "punch")) x.autoMode = 4;
                    else if (!rest.compare(0, 7, "capture")) x.autoMode = 20;
                    else if (!rest.compare(0, 6, "ladder")) {
                        x.autoMode = 5;
                        if (rest.size() > 7 && rest[6] == ':' && sscanf(rest.c_str() + 7, "%f,%f,%f,%f,%f", &x.ladder[0], &x.ladder[1], &x.ladder[2], &x.ladder[3], &x.ladder[4]) == 5)
                            x.hasLadder = true;
                    }
                    else if (!rest.compare(0, 11, "watchladder")) x.autoMode = 6;
                    else if (!rest.compare(0, 4, "rail")) x.autoMode = 9;
                    else if (!rest.compare(0, 11, "watchplayer")) x.autoMode = 10;
                    else if (!rest.compare(0, 6, "pickup")) x.autoMode = 11;
                    else if (!rest.compare(0, 5, "spawn")) x.autoMode = 12;
                    else if (!rest.compare(0, 4, "tsne")) x.autoMode = 13;
                    else if (!rest.compare(0, 5, "carry")) x.autoMode = 14;
                    else if (!rest.compare(0, 4, "idle")) x.autoMode = 15;
                    else if (!rest.compare(0, 5, "visit")) x.autoMode = 16;
                    else if (!rest.compare(0, 2, "bc")) x.autoMode = 17;
                    else if (!rest.compare(0, 3, "rot")) x.autoMode = 18;
                    else if (!rest.compare(0, 3, "m15")) x.autoMode = 20;
                    else if (!rest.compare(0, 7, "playcqc")) x.autoMode = 8;
                    else if (!rest.compare(0, 4, "play")) x.autoMode = 7;
                    else if (!rest.compare(0, 4, "wake")) x.autoMode = 19;   // (14 .. 18: other trees' modes)
                    char* pe = nullptr;
                    x.buttons = x.autoMode ? 0 : strtoul(rest.c_str(), &pe, 16);
                    if (!x.autoMode && pe && *pe == 'p') x.pressure = (int)strtoul(pe + 1, &pe, 16);
                    if (!x.autoMode && pe && *pe == 't') x.tap = (int)strtoul(pe + 1, nullptr, 10);
                    size_t sl = rest.find('/');
                    if (sl != std::string::npos && (at == std::string::npos || sl < at)) x.hold = atoi(rest.c_str() + sl + 1);
                    if (x.hold < 1) x.hold = 1;
                    for (size_t pl = rest.find('+'); pl != std::string::npos && (at == std::string::npos || pl < at); pl = rest.find('+', pl + 1)) {
                        Step::Chord k{}; char* e = nullptr;       // +mask:from:len, pressed with the held buttons
                        k.buttons = strtoul(rest.c_str() + pl + 1, &e, 16);
                        if (e && *e == 'p') k.pressure = (int)strtoul(e + 1, &e, 16);
                        if (e && *e == ':') { k.from = (int)strtol(e + 1, &e, 10); if (e && *e == ':') k.len = (int)strtol(e + 1, &e, 10); }
                        if (k.len < 1) k.len = 4;
                        x.chords.push_back(k);
                    }
                }
                gSteps.push_back(x);
            }
            if (bar == std::string::npos) break; pos = bar + 1;
        }
        printf("steps: %zu\n", gSteps.size());
    }
}
// SIM_JOIN_LOCK=<file>: one joiner at a time from the game list to the match (the host takes one join at a
// time, and at the simulation's pace a join lasts minutes: joiners that overlapped failed). The press of the
// list's step (the one JoinRetry goes back to) waits for the file, made with this instance's port and the
// time; it goes when the local player is in the match, when the join failed, at the end, or after 15 min
static bool gJoinLockMine = false;
static bool JoinListStep(const Step& x) { return x.text.rfind("Please select a game to join", 0) == 0 || x.text == "tx55 4313"; }
static void JoinLockRelease(const char* why) {
    const char* path = getenv("SIM_JOIN_LOCK");
    if (!path || !gJoinLockMine) return;
    DeleteFileA(path); gJoinLockMine = false;
    printf("join lock: released (%s)\n", why);
}
static bool JoinLockTake(int f) {
    const char* path = getenv("SIM_JOIN_LOCK");
    if (!path || gJoinLockMine) return true;
    const long long now = EpochMs();
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {                       // held: taken over after 15 min
        long long t = 0; int who = 0;
        if (FILE* fp = fopen(path, "rb")) { if (fscanf(fp, "%d %lld", &who, &t) != 2) t = 0; fclose(fp); }
        if (t && now - t < 900000) return false;
        DeleteFileA(path);
        h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        printf("join lock: the lock of port %d (%lld s old) taken over\n", who, t ? (now - t) / 1000 : -1);
    }
    char b[64]; const int n = snprintf(b, sizeof b, "%s %lld\n", getenv("SIM_UDP_PORT") ? getenv("SIM_UDP_PORT") : "0", now);
    DWORD w = 0; WriteFile(h, b, (DWORD)n, &w, nullptr); CloseHandle(h);
    gJoinLockMine = true;
    static bool once = false; if (!once) { once = true; atexit([] { JoinLockRelease("the end"); }); }
    printf("join lock: taken at frame %d\n", f);
    return true;
}
// one frame of the steps: a step's text seen, its press started, held, released
static void StepFrame(int f) {
    if (gJoinLockMine && ps2world::PlayerObject(0)) JoinLockRelease("in the match");
    if (gStep < gSteps.size()) {
        Step& x = gSteps[gStep];
        if (gPressFrom < 0) {
            if (x.text.empty() && gStepSeen < 0) gStepSeen = f;
            if (gStepSeen >= 0 && f >= gStepSeen + x.delay) {
                if (JoinListStep(x) && !gJoinLockMine && (f % 10 || !JoinLockTake(f))) {
                    static int told = -100000; if (f - told >= 600) { told = f; printf("join lock: waiting at frame %d\n", f); }
                    return;
                }
                gPressFrom = f; gPressButtons = (int)x.buttons; gPressHold = x.hold; gPressChords = x.chords; gPressAuto = x.autoMode; gPressPressure = x.pressure; memcpy(gPressStick, x.stick, sizeof gPressStick);
                gPressTap = x.tap; gPressDg = ps2world::DgFrames();
                if (x.autoMode) gAuto = Autopilot();
                if (x.autoMode == 5) { gHaveStepLadder = x.hasLadder; memcpy(gStepLadder, x.ladder, sizeof gStepLadder); }
                if (!x.kb.empty()) { ps2world::SimType(x.kb.c_str()); gPressButtons = 0; }
                if (x.lang) ps2world::SetGameLanguage(x.lang);
                if (!x.env.empty()) {                       // =setenv:NAME:VALUE
                    std::string kv = x.env; const size_t c = kv.find(':'); if (c != std::string::npos) kv[c] = '=';
                    _putenv(kv.c_str()); printf("step %zu: setenv %s at frame %d\n", gStep, kv.c_str(), f);
                }
                printf("step %zu: \"%s\" -> %s %x at frame %d (%d frames) dg %lld\n", gStep, x.text.c_str(), x.kb.empty() ? "press" : "type", x.buttons, f, x.hold, ps2world::DgFrames());
                StepPngStart(x.autoMode ? 0x10000u | (unsigned)x.autoMode : x.buttons, x.pressure, f);   // SIM_PNG_STEPS (an autopilot: 1000N)
            }
        } else if (gPressAuto == 12 && f < gPressFrom + gPressHold && f > gPressFrom + 1 && ps2world::PlayerObject(0) &&
                   (int16_t)PsR16(ps2world::PlayerObject(0) + 0xb64) > 0 && gAuto.madeAtStart >= 0 &&
                   (ps2world::LocalWeaponsMade() > gAuto.madeAtStart || gAuto.t >= 60)) {   // =spawn: his weapons made (in the map), the step ends
            gPressHold = f - gPressFrom;
            printf("step %zu: alive at frame %d dg %lld\n", gStep, f, ps2world::DgFrames());
        } else if (gPressAuto == 14 && f < gPressFrom + gPressHold && f > gPressFrom + 1 && gAuto.phase == 2) {
            gPressHold = f - gPressFrom;                     // =carry: the mission object held, the step ends
            printf("step %zu: the mission object held at frame %d dg %lld\n", gStep, f, ps2world::DgFrames());
        } else if (gPressAuto == 11 && getenv("SIM_PICKUP_END") && f < gPressFrom + gPressHold && f > gPressFrom + 1 && gAuto.takenAtStart >= 0 &&
                   ps2world::BoxesTaken() > gAuto.takenAtStart) {   // =pickup, SIM_PICKUP_END=1: a weapon taken from a box, the step ends
            gPressHold = f - gPressFrom;
            printf("step %zu: a weapon taken from a box at frame %d dg %lld\n", gStep, f, ps2world::DgFrames());
        } else if (f == gPressFrom + gPressHold && gPressHold >= 10 && x.kb.empty()) {   // a long hold's end, in DG frames too
            printf("step %zu: released at frame %d dg %lld\n", gStep, f, ps2world::DgFrames());
        } else if (f >= gPressFrom + gPressHold + 4 + (int)x.kb.size() * 4 && !(!x.kb.empty() && ps2world::SimTyping())) {   // held (4 frames unless /n), 4 released (typing: 4 reads per key, until all are typed)
            gPressFrom = -1; gStepSeen = -1; ++gStep;
            if (gStep < gSteps.size() && !gSteps[gStep].text.empty())   // a text that came during the press counts
                for (auto& r : gRecent) if (strstr(r.second.c_str(), gSteps[gStep].text.c_str())) { gStepSeen = r.first; break; }
            gRecent.clear();
        }
    }
}
// SIM_AUDIO_RT=1 | <wav>: the sound pulled in real time as MGO1.exe's output does (native/audio.cpp Feeder): six
// buffers of 800 frames played at 48 kHz, each mixed when the device is done with it, the IOP waited for within
// the same budget; the times the device would have run dry and the longest mix every 30 s, the mix to <wav>
static void AudioRt(const char* wavPath) {
    constexpr int kRate = 48000, kBuf = 800, kBufs = 6;
    LARGE_INTEGER qf, q; QueryPerformanceFrequency(&qf);
    auto now = [&] { QueryPerformanceCounter(&q); return (double)q.QuadPart / (double)qf.QuadPart; };
    FILE* wav = *wavPath ? fopen(wavPath, "wb") : nullptr;
    uint32_t wavBytes = 0;
    if (wav) {
        const uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                                 0x80, 0xBB, 0, 0, 0, 0xEE, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
        fwrite(hdr, 1, 44, wav);
    }
    const double bufSec = (double)kBuf / kRate;
    int queued = 0; double headEnd = 0, lastLog = now();
    long long dry = 0, mixes = 0, gapMs = 0; double worst = 0; bool started = false;
    static int16_t buf[kBuf * 2];
    for (;;) {
        const double t = now();
        while (queued > 0 && headEnd <= t) { --queued; if (queued) headEnd += bufSec; }
        if (started && queued == 0) ++dry;                    // the device played everything: a gap
        const int done = kBufs - queued;
        const int queuedUs = queued * kBuf * 1000 / (kRate / 1000);
        const int waitUs = std::max(1000, std::min(8000, (queuedUs - 12000) / std::max(1, done)));
        for (int b = 0; b < done; ++b) {
            const double m0 = now();
            memset(buf, 0, sizeof buf);
            ps2world::SoundMix(buf, kBuf, waitUs);
            const double m1 = now();
            worst = std::max(worst, (m1 - m0) * 1000); ++mixes;
            // SIM_AUDIO_RT_WINDOWS="a-b,c-d": only those seconds of the sound into the WAV (a long run's file kept small)
            static std::vector<std::pair<double, double>> windows = [] {
                std::vector<std::pair<double, double>> w;
                if (const char* e = getenv("SIM_AUDIO_RT_WINDOWS"))
                    for (const char* q = e; *q;) { double a = 0, b = 0; int n = 0; if (sscanf(q, "%lf-%lf%n", &a, &b, &n) < 2) break; w.push_back({a, b}); q += n; if (*q == ',') ++q; }
                return w;
            }();
            static const double tStart = now();
            bool inWindow = windows.empty();
            for (auto& w : windows) inWindow |= m0 - tStart >= w.first && m0 - tStart < w.second;
            if (wav && inWindow) {
                fwrite(buf, 4, kBuf, wav); wavBytes += kBuf * 4;
                if (mixes % 60 == 0) { const uint32_t riff = wavBytes + 36; fseek(wav, 4, SEEK_SET); fwrite(&riff, 4, 1, wav); fseek(wav, 40, SEEK_SET); fwrite(&wavBytes, 4, 1, wav); fseek(wav, 0, SEEK_END); fflush(wav); }
            }
            if (queued == 0) headEnd = now() + bufSec;
            ++queued; started = true;
        }
        if (t - lastLog >= 30) {
            printf("audio rt: %lld mixes in %.0f s, the longest %.1f ms for %d ms of sound, %lld times the device ran dry\n",
                   mixes, t - lastLog, worst, kBuf * 1000 / kRate, dry);
            (void)gapMs; mixes = 0; worst = 0; dry = 0; lastLog = t;
        }
        Sleep(1);
    }
}
// the native build (SIM_NATIVE=1): MGO1 boots from its own entry; each VBlank runs the steps
static int gUdpPort = 0;
// SIM_VOICE_MIC=<wav> (docs/VOICE.md): a headset whose microphone says that file (16-bit PCM, mono,
// 8 kHz; looped) at the PS2's pace; SIM_VOICE_MIC48=<wav>: a file at its own rate through the PC's
// microphone chain first (native/voice_dsp.h MicChain; SIM_VOICE_NS=0: without its noise held back);
// SIM_VOICE_OUT=<wav>: what the game plays on it (8 kHz), at the same pace; SIM_VOICE=1: a headset
// with a silent microphone. SIM_VOICE_WATCH logs the voice actor. SIM_VOICE_OPUS=1: the voice between
// PCs as MGO1.exe does it (native/voice.cpp): the microphone's file (SIM_VOICE_MIC48) also through
// WideChain and Opus while the game records; SIM_VOICE_OUT48=<wav>: the sound MGO1.exe would play (48
// kHz: the game's voice through SpeakerChain, the other PCs' Opus while the game plays them)
static bool ReadWavMono(const char* path, std::vector<int16_t>& s, int& rate) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> b; uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f);
    size_t at = 12; rate = 8000;
    while (at + 8 <= b.size() && memcmp(&b[at], "data", 4) != 0) {
        uint32_t sz; memcpy(&sz, &b[at + 4], 4);
        if (!memcmp(&b[at], "fmt ", 4) && at + 16 <= b.size()) { uint32_t r; memcpy(&r, &b[at + 12], 4); rate = (int)r; }
        at += 8 + sz + (sz & 1);
    }
    if (at + 8 > b.size()) return false;
    uint32_t sz; memcpy(&sz, &b[at + 4], 4); sz = std::min<uint32_t>(sz, (uint32_t)(b.size() - at - 8));
    s.resize(sz / 2); memcpy(s.data(), &b[at + 8], s.size() * 2);
    return true;
}
struct SimWav {                                         // a mono 16-bit WAV written as it goes
    FILE* f = nullptr; uint32_t bytes = 0, lastHeader = 0; int rate = 8000;
    bool Open(const char* path, int r) {
        rate = r; f = fopen(path, "wb");
        if (!f) return false;
        uint8_t h[44] = {'R','I','F','F',36,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a',0,0,0,0};
        const uint32_t rr = (uint32_t)r, br = (uint32_t)r * 2;
        memcpy(h + 24, &rr, 4); memcpy(h + 28, &br, 4);
        fwrite(h, 1, 44, f);
        return true;
    }
    void Put(const int16_t* x, size_t n) {
        if (!f) return;
        fwrite(x, 2, n, f); bytes += (uint32_t)n * 2;
        if (bytes - lastHeader >= (uint32_t)rate * 2) {    // the sizes kept right every second
            lastHeader = bytes;
            const uint32_t riff = bytes + 36;
            fseek(f, 4, SEEK_SET); fwrite(&riff, 4, 1, f); fseek(f, 40, SEEK_SET); fwrite(&bytes, 4, 1, f); fseek(f, 0, SEEK_END); fflush(f);
        }
    }
};
static void SimOpusEnded(int talker, voice::OpusStream::Stats s) {
    printf("SIM: voice: player %d's talk in Opus: %.2f s played, %lld packets (%lld decoded, %lld by FEC, %lld concealed, %lld late, %lld pauses shortened, %lld dropped)\n",
           talker + 1, s.samples / 48000.0, s.packets, s.decoded, s.fec, s.concealed, s.late, s.skipped, s.dropped);
    fflush(stdout);
}
static void SimVoice() {
    static int on = -1;
    static std::vector<int16_t> mic; static size_t micPos = 0; static int micRate = 8000;
    static std::unique_ptr<voice::MicChain> chain;
    static SimWav out, out48;
    static unsigned long long t0 = 0, fed = 0, pulled = 0, pulled48 = 0;
    static bool opus = false, talking = false, newTalk = false;
    static std::unique_ptr<voice::WideChain> wide;
    static std::unique_ptr<voice::OpusTalker> talker;
    static std::unique_ptr<voice::OpusListener> listener;
    static std::unique_ptr<voice::SpeakerChain> speaker;
    static std::vector<float> lpc48;                     // the game's voice made 48 kHz, not written yet
    static long long talkPackets = 0, talkBytes = 0, talkSamples = 0;
    if (on < 0) {
        const char* m = getenv("SIM_VOICE_MIC"); const char* m48 = getenv("SIM_VOICE_MIC48");
        const char* o = getenv("SIM_VOICE_OUT"); const char* o48 = getenv("SIM_VOICE_OUT48");
        on = (m || m48 || o || o48 || getenv("SIM_VOICE")) ? 1 : 0;
        if (!on) return;
        opus = getenv("SIM_VOICE_OPUS") && !strcmp(getenv("SIM_VOICE_OPUS"), "1");
        if (m && ReadWavMono(m, mic, micRate)) printf("SIM: voice: the microphone says %s (%zu samples at %d Hz)\n", m, mic.size(), micRate);
        if (m48 && ReadWavMono(m48, mic, micRate)) {
            const bool ns = !(getenv("SIM_VOICE_NS") && !strcmp(getenv("SIM_VOICE_NS"), "0"));
            chain.reset(new voice::MicChain(micRate, ns));
            if (opus) wide.reset(new voice::WideChain(micRate));
            printf("SIM: voice: the microphone says %s (%zu samples at %d Hz) through the PC's chain%s%s\n", m48, mic.size(), micRate,
                   ns ? "" : " without its noise suppression", opus ? ", and through WideChain for Opus" : "");
        }
        if (o) out.Open(o, 8000);
        if (o48) { out48.Open(o48, 48000); speaker.reset(new voice::SpeakerChain()); }
        if (opus) {
            talker.reset(new voice::OpusTalker()); listener.reset(new voice::OpusListener()); listener->OnEnd(&SimOpusEnded);
            ps2world::VoiceNetEnable(true);
            printf("SIM: voice: between PCs in Opus (%d kbps)\n", voice::kOpusBitrate / 1000);
        }
        ps2world::VoiceAttach(true);
        t0 = ps2world::PsTimeMs();
    }
    if (!on) return;
    const unsigned long long ms = ps2world::PsTimeMs() - t0, due = ms * 8;   // 8 samples a ms
    int16_t buf[1024];
    if (chain) {                                        // the file at its rate through the PC's chain
        const unsigned long long dueIn = ms * (unsigned long long)micRate / 1000;
        static std::vector<float> f, w; static std::vector<int16_t> pcm;
        while (fed < dueIn) {
            const size_t n = (size_t)std::min<unsigned long long>(dueIn - fed, 480);
            f.resize(n); for (size_t i = 0; i < n; ++i) f[i] = mic[micPos++ % mic.size()] / 32768.0f;
            pcm.clear(); chain->Run(f.data(), n, pcm);
            if (!pcm.empty()) ps2world::VoiceIn(pcm.data(), (int)pcm.size());
            fed += n;
            if (!wide) continue;                        // Opus while the game records (native/voice.cpp MicSink)
            w.clear(); wide->Run(f.data(), n, w);
            const bool now = (ps2world::VoiceState() & 4) != 0;
            if (now && !talking) { talker->Reset(); newTalk = true; talkPackets = talkBytes = talkSamples = 0; }
            if (!now && talking && talkPackets)
                printf("SIM: voice: a talk in Opus: %.2f s, %lld packets, %.1f kbps\n", talkSamples / 48000.0, talkPackets,
                       talkSamples ? talkBytes * 8.0 * 48.0 / (double)talkSamples : 0.0);
            talking = now;
            if (!now) continue;
            talkSamples += (long long)w.size();
            talker->Run(w.data(), w.size(), [](const unsigned char* p, int b) {
                ps2world::VoiceNetSend(p, b, newTalk); newTalk = false; ++talkPackets; talkBytes += b;
            });
        }
    } else while (fed < due) {                          // the microphone: the samples due since the last frame
        const int n = (int)std::min<unsigned long long>(due - fed, 1024);
        for (int i = 0; i < n; ++i) buf[i] = mic.empty() ? 0 : mic[micPos++ % mic.size()];
        ps2world::VoiceIn(buf, n);
        fed += (unsigned long long)n;
    }
    while (pulled < due) {                              // the speaker: what the game gave, silence meanwhile
        const int n = (int)std::min<unsigned long long>(due - pulled, 1024);
        const int got = ps2world::VoiceOut(buf, n);
        for (int i = got; i < n; ++i) buf[i] = 0;
        out.Put(buf, (size_t)n);
        if (speaker) speaker->Run(buf, (size_t)n, lpc48);
        pulled += (unsigned long long)n;
    }
    if (!out48.f) return;                               // MGO1.exe's sound (native/voice.cpp VoiceMix)
    const unsigned long long due48 = ms * 48;
    if (pulled48 >= due48) return;
    const int n = (int)(due48 - pulled48);
    std::vector<float> mix((size_t)n, 0.f);
    const size_t k = std::min<size_t>((size_t)n, lpc48.size());
    for (size_t i = 0; i < k; ++i) mix[i] = lpc48[i];
    lpc48.erase(lpc48.begin(), lpc48.begin() + (long)k);
    if (listener) {
        unsigned char pkt[512]; int tk = 0, talk = 0, seq = 0, got;
        while ((got = ps2world::VoiceNetRecv(pkt, (int)sizeof pkt, &tk, &talk, &seq)) > 0) listener->Push(tk, talk, seq, pkt, got);
        listener->Mix(mix.data(), n, ps2world::VoiceNetHeard(), 1.0f);
    }
    std::vector<int16_t> o((size_t)n);
    for (int i = 0; i < n; ++i) o[(size_t)i] = (int16_t)std::clamp((int)std::lrint(mix[(size_t)i] * 32767.0f), -32768, 32767);
    out48.Put(o.data(), o.size());
    pulled48 = due48;
}
// SIM_CALL_STATS=<frames> (perf): the recompiled code's calls counted (ps2::gCallHook) and the platform's bridge timed
// (ps2::gBridge wrapped, the time stamp counter around each call it does not serve): every <frames> frames the calls a
// VBlank, the bridge's time a call and a VBlank, the most called targets (every 16th call sampled)
namespace ps2 { struct Ctx; extern bool (*gBridge)(Ctx& c, uint32_t addr); }
static bool (*gBridgeOrig)(ps2::Ctx&, uint32_t) = nullptr;
static uint64_t gCallsN = 0, gBridgeNo = 0, gBridgeNoCycles = 0, gBridgeYes = 0;
static uint32_t gCallTab[1 << 16][2];
static void CountCall(uint32_t a) {
    if ((++gCallsN & 15) == 0) {
        uint32_t h = (a * 0x9E3779B1u) >> 16;
        for (int k = 0; k < 8; ++k, h = (h + 1) & 0xffff)
            if (gCallTab[h][0] == a || gCallTab[h][1] == 0) { gCallTab[h][0] = a; ++gCallTab[h][1]; break; }
    }
}
static uint64_t gBridgeNoInner = 0;                          // not served, but game code ran inside (left out of the cycles)
static thread_local uint64_t tBridgeCalls = 0;
static bool TimedBridge(ps2::Ctx& c, uint32_t a) {
    const uint64_t before = tBridgeCalls++;
    const uint64_t t0 = __rdtsc();
    const bool r = gBridgeOrig(c, a);
    const uint64_t dt = __rdtsc() - t0;
    if (r) ++gBridgeYes;
    else if (tBridgeCalls == before + 1) { ++gBridgeNo; gBridgeNoCycles += dt; }   // the bridge's own time
    else ++gBridgeNoInner;
    return r;
}
static void CallStats(int f) {
    static const int every = getenv("SIM_CALL_STATS") ? atoi(getenv("SIM_CALL_STATS")) : 0;
    if (every <= 0) return;
    static uint64_t tsc0 = 0; static LARGE_INTEGER q0{};
    if (!gBridgeOrig && ps2::gBridge && ps2::gBridge != &TimedBridge) {
        gBridgeOrig = ps2::gBridge; ps2::gBridge = &TimedBridge; ps2::gCallHook = &CountCall;
        tsc0 = __rdtsc(); QueryPerformanceCounter(&q0);
        printf("SIM: frame %d: the calls counted, the bridge timed (SIM_CALL_STATS)\n", f);
        return;
    }
    if (!gBridgeOrig || f % every) return;
    LARGE_INTEGER q, qf; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&qf);
    const uint64_t tsc = __rdtsc();
    const double secs = (double)(q.QuadPart - q0.QuadPart) / (double)qf.QuadPart, ghz = secs > 0 ? (tsc - tsc0) / secs / 1e9 : 0;
    const double perCall = gBridgeNo ? (double)gBridgeNoCycles / (double)gBridgeNo : 0;
    std::vector<std::pair<uint32_t, uint32_t>> top;
    for (auto& e : gCallTab) if (e[1]) top.push_back({e[1], e[0]});
    std::sort(top.begin(), top.end(), [](auto& x, auto& y) { return x.first > y.first; });
    std::string t;
    for (size_t k = 0; k < top.size() && k < 12; ++k) { char b[40]; snprintf(b, sizeof b, " %06x:%u", top[k].second, top[k].first * 16 / every); t += b; }
    printf("SIM: frame %d: calls %.0f a VBlank, the bridge %.0f cycles a call it does not serve (%.3f ms a VBlank at %.2f GHz), %.1f served "
           "a VBlank; the most called (a VBlank):%s\n", f, (double)gCallsN / every, perCall, ghz > 0 ? gBridgeNoCycles / (ghz * 1e6) / every : 0.0,
           ghz, (double)gBridgeYes / every, t.c_str());
    printf("SIM: frame %d: the bridge's cycles above leave out %.1f calls a VBlank that ran game code inside it\n", f, (double)gBridgeNoInner / every);
    gCallsN = gBridgeNo = gBridgeNoCycles = gBridgeYes = gBridgeNoInner = 0;
    memset(gCallTab, 0, sizeof gCallTab);
    tsc0 = tsc; q0 = q;
}
// SIM_SCREEN_HZ=<hz>: MGO1.exe's GL renderer and screen (tests): gs_gpu.cpp threaded on a GL that does nothing (as
// tests/gsq_bench.cpp --gpu-null), its swap returning at the next refresh of a screen at <hz> (vsync), the picture
// presented each VBlank as native/mgo1_exe.cpp GameFrame does. A 'screen' line every SIM_SCREEN_LOG frames (60): the
// wall clock, match ticks, DG's pictures, presents, refreshes shown and the word at SIM_CLOCK_EXPR (wexpr's syntax, an
// address: City's round time left is *(*0x2F0C90+0x3BC) on the US program, 300 a second)
namespace simscreen {
extern "C" {
static void Nop() {}
static unsigned One() { return 1; }
static const unsigned char* GetString(unsigned) { return (const unsigned char*)"a GL that does nothing"; }
static void GetIv(unsigned, unsigned, int* p) { *p = 1; }
static void GetIntegerv(unsigned, int* p) { *p = 0; }
static void Gen(int n, unsigned* p) { for (int i = 0; i < n; ++i) p[i] = (unsigned)(i + 1); }
static void InfoLog(unsigned, int, int* l, char* s) { if (l) *l = 0; if (s) *s = 0; }
static unsigned CheckFb(unsigned) { return 0x8CD5; }                       // GL_FRAMEBUFFER_COMPLETE
static int UniformLoc(unsigned, const char*) { return 1; }
static unsigned NoError() { return 0; }
static const unsigned char* GetStringi(unsigned, unsigned) { return nullptr; }
}
static void* GetProc(const char* n) {
    if (!strcmp(n, "glGetString")) return (void*)&GetString;
    if (!strcmp(n, "glGetShaderiv") || !strcmp(n, "glGetProgramiv")) return (void*)&GetIv;
    if (!strcmp(n, "glGetIntegerv")) return (void*)&GetIntegerv;
    if (!strncmp(n, "glGen", 5)) return (void*)&Gen;
    if (strstr(n, "InfoLog")) return (void*)&InfoLog;
    if (!strcmp(n, "glCheckFramebufferStatus")) return (void*)&CheckFb;
    if (!strcmp(n, "glGetUniformLocation")) return (void*)&UniformLoc;
    if (!strcmp(n, "glGetError")) return (void*)&NoError;
    if (!strcmp(n, "glGetStringi")) return (void*)&GetStringi;
    if (!strcmp(n, "glCreateShader") || !strcmp(n, "glCreateProgram")) return (void*)&One;
    return (void*)&Nop;
}
static void Size(void*, int& w, int& h) { w = 2560; h = 1440; }
static long long gFreq = 0, gPeriod = 0, gT0 = 0, gLast = 0;
static std::atomic<unsigned long long> gShown{0};
static bool gOn = false;
static unsigned long long gPresents = 0;
static long long Now() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
static void Swap(void*) {                                   // returns at the next refresh after the last one shown
    const long long now = Now(), next = std::max(gLast + gPeriod, gT0 + ((now - gT0) / gPeriod + 1) * gPeriod);
    for (long long t = now; t < next; t = Now()) { const long long ms = (next - t) * 1000 / gFreq; Sleep(ms > 1 ? (DWORD)(ms - 1) : 0); }
    gLast = next; ++gShown;
}
static void Start(double hz) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    gFreq = f.QuadPart; gPeriod = (long long)(gFreq / hz); gT0 = Now();
    ps2::gsgpu::Host h; h.getProc = &GetProc; h.size = &Size; h.swap = &Swap; h.threaded = true;
    h.aspect = 16.0f / 9.0f; h.scaleX = 5;                  // Snake's 2560 x 1440: x5 across, x4 down
    std::string err;
    gOn = ps2::GsGpuStart(h, 4, err);
    printf("screen: %s, a screen at %.3f Hz (%s)\n", gOn ? "the GL renderer on" : "the GL renderer did not start", hz, err.c_str());
}
static void Frame(long long frameNo) {                      // each VBlank (GameFrame's present)
    if (!gOn) return;
    static uint64_t shownFlips = ~0ull;
    const uint64_t flips = ps2::GsDisplayFlips();
    if (flips != shownFlips || frameNo % 6 == 0) { shownFlips = flips; ps2::GsGpuPresent(); ++gPresents; }
    static const int every = getenv("SIM_SCREEN_LOG") ? atoi(getenv("SIM_SCREEN_LOG")) : 60;
    if (every > 0 && frameNo % every == 0) {
        long long clock = -1;
        if (const char* e = getenv("SIM_CLOCK_EXPR")) { const char* p = e; clock = (int32_t)wexpr::Rd(wexpr::Expr(p)); }
        printf("screen frame %lld: wall %.3f s, %lld match ticks, %lld pictures, %llu presents (%llu left out), %llu shown, clock %lld\n", frameNo,
               (double)(Now() - gT0) / gFreq, ps2world::GameTicks(), ps2world::DgPictures(), gPresents,
               (unsigned long long)ps2::gsgpu::PresentsLeftOut(), gShown.load(), clock);
    }
    if (frameNo % 600 == 0) printf("screen gpu: %s\n", ps2::GsGpuStats().c_str());
}
}  // namespace simscreen
// SIM_TSNE_CAMO_LOG (TEAM SNEAKING tests): every player here (this machine's and the copies), each change of his life
// (alive or not), team, item (+0xE1D), battery's copy (+0xB6C: empty or not) and camouflage (flag 0x96: word 4 bit 22
// of +0xB2C, set while his NPL_NewOptcamouflage lives), logged
static void TsneCamoLog() {
    static const bool on = getenv("SIM_TSNE_CAMO_LOG") != nullptr;
    if (!on) return;
    static std::map<uint32_t, std::string> seen;
    std::map<uint32_t, std::string> now;
    for (int w = 0; w <= 16; ++w) {
        const uint32_t o = ps2world::PlayerObject(w);
        if (!o) { if (w) break; continue; }
        const int life = (int16_t)PsR16(o + 0xb64), copy = (int16_t)PsR16(o + 0xb6c);
        char k[96];
        snprintf(k, sizeof k, "slot %u%s team %d %s item %u battery's copy %s camouflage %s", ps2::gMem[o + 0xb60], w ? "" : " (this player)",
                 TeamOfObject(o), life > 0 ? "alive" : "not alive", ps2::gMem[o + 0xe1d], copy > 0 ? "full" : "empty",
                 (PsR32(o + 0xb2c + 16) >> 22) & 1 ? "on" : "off");
        now[o] = k;
        auto it = seen.find(o);
        if (it == seen.end() || it->second != k) printf("SIM_TSNE_CAMO f%d: %s (%d, life %d)\n", gFrame, k, copy, life);
    }
    for (const auto& x : seen) if (!now.count(x.first)) printf("SIM_TSNE_CAMO f%d: player %08x gone\n", gFrame, x.first);
    seen.swap(now);
}
static void NativeFrame(long long frameNo) {
    const int f = (int)frameNo;
    gFrame = f; gFrameNow = f;
    // (v150-host beta 18) SIM_LOAD_SLOW_MS=<ms> (tests): each VBlank of a game frame that lasts 3 VBlanks or more (a stage
    // set up: no actor runs, the session daemons neither) late by that much, as a machine whose processors are busy makes
    // a game's loads last (the playable doll that lost its connection when the round was played again, r7)
    if (const char* e = getenv("SIM_LOAD_SLOW_MS")) {
        static long long dgSeen = -1; static int still = 0;
        const long long dg = ps2world::DgFrames();
        still = dg == dgSeen ? still + 1 : 0; dgSeen = dg;
        if (still >= 3) Sleep((DWORD)atoi(e));
    }
    doll::Frame(frameNo);                                   // the PLAYABLE DOLL: what it sees, its pad for this frame
    CallStats(f);
    simscreen::Frame(frameNo);                              // SIM_SCREEN_HZ
    StepFrame(f);
    if (PcReady()) PcReadyFrame(f);                         // SIM_PC_READY: the front end's ready weapon, as MGO1.exe's frame
    SimVoice();
    TsneCamoLog();                                          // SIM_TSNE_CAMO_LOG
    if (const char* da = getenv("SIM_DUMP_AT")) {           // "frame,frame...": all PS2 memory at those frames
        for (const char* q = da; *q;) {
            char* end; long v = strtol(q, &end, 10); if (end == q) break;
            if (v == f) {
                char name[64]; snprintf(name, sizeof name, "dump_%d.bin", f);
                if (FILE* fd = fopen(name, "wb")) { fwrite(ps2::gMem, 1, 0x02000000, fd); fclose(fd); printf("SIM: memory at frame %d dg %lld written to %s\n", f, ps2world::DgFrames(), name); }
            }
            q = *end ? end + 1 : end;
        }
    }
    if (gLadderDrawLeft > 0) {                              // SIM_LADDER_DRAW: every frame drawn meanwhile
        ps2::gGsDraw = true;
        if (--gLadderDrawLeft == 0 && getenv("SIM_GS_DRAW_FROM")) ps2::gGsDraw = false;
    }
    if (gPunchPngLeft > 0) {                                // SIM_PUNCH_PNG: drawn every frame meanwhile
        ps2::gGsDraw = true;
        if (f >= gPunchPngNext) {
            char name[64]; snprintf(name, sizeof name, "pu_%05d.png", f);
            ps2::GsWritePng(name);
            float p[3], q[3];
            const bool hp = ps2world::PlayerPos(0, p), hq = ps2world::PlayerPos(1, q);
            printf("SIM: %s dg %lld me %.0f %.0f %.0f other %.0f %.0f %.0f\n", name, ps2world::DgFrames(), hp ? p[0] : 0, hp ? p[1] : 0, hp ? p[2] : 0, hq ? q[0] : 0, hq ? q[1] : 0, hq ? q[2] : 0);
            gPunchPngNext += gPunchPngStep;
            if (--gPunchPngLeft == 0 && getenv("SIM_GS_DRAW_FROM")) ps2::gGsDraw = false;   // drawn only for them
        }
    }
    // SIM_GSQ=<prefix>:<ticks>[:<vsyncs>[:<rounds>]]: the GS side's input recorded for the offline bench
    // (tests/gsq_bench.cpp) once a round has run <ticks> match ticks: <vsyncs> VSyncs (120) into
    // <prefix>_<n>.gsq, in the first <rounds> rounds (1)
    if (const char* e = getenv("SIM_GSQ_AT")) {             // "<file>@<frame>:<vsyncs>": a recording at that frame (the lobby)
        static bool armed = false;
        char name[256] = ""; int at = 0, vs = 120;
        if (!armed && sscanf(e, "%255[^@]@%d:%d", name, &at, &vs) >= 2 && f >= at) {
            ps2::GsqArm(name, 0, vs); armed = true;
            printf("SIM: frame %d: the GS side's input recorded into %s (%d VSyncs)\n", f, name, vs);
        }
    }
    if (const char* e = getenv("SIM_GSQ")) {
        static long long from = -1; static int done = 0; static bool armed = false;
        char prefix[256] = ""; int ticks = 300, vs = 120, rounds = 1;
        const char* c = strchr(e, ':');
        snprintf(prefix, sizeof prefix, "%.*s", c ? (int)(c - e) : (int)strlen(e), e);
        if (c) sscanf(c + 1, "%d:%d:%d", &ticks, &vs, &rounds);
        if (!ps2world::InMatch()) { from = -1; armed = false; }
        else {
            if (from < 0) from = ps2world::GameTicks();
            if (!armed && done < rounds && ps2world::GameTicks() - from >= ticks) {
                char name[300]; snprintf(name, sizeof name, "%s_%d.gsq", prefix, done);
                ps2::GsqArm(name, 0, vs);
                armed = true; ++done;
                printf("SIM: frame %d: the GS side's input recorded into %s (%d VSyncs)\n", f, name, vs);
            }
        }
    }
    if (getenv("SIM_RATES")) {                              // VBlanks, DG's pictures and match ticks a second, every 600 VBlanks
        static LARGE_INTEGER q0{}, qf{}; static long long n0 = 0, t0 = 0, d0 = 0; static uint64_t fl0 = 0;
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        if (!qf.QuadPart) { QueryPerformanceFrequency(&qf); q0 = q; n0 = f; t0 = ps2world::GameTicks(); d0 = ps2world::DgPictures(); fl0 = ps2::GsDisplayFlips(); }
        else if (f - n0 >= 600) {
            const double s = (double)(q.QuadPart - q0.QuadPart) / qf.QuadPart;
            const long long t = ps2world::GameTicks(), d = ps2world::DgPictures(); const uint64_t fl = ps2::GsDisplayFlips();
            // the game's pace: VBlanks a match tick (2.0, PAL at 60 2.4; more: its moves slower than its clocks)
            printf("rates frame %d: %.2f VBlanks, %.2f pictures, %.2f match ticks a second; %.2f VBlanks a tick, %.2f fields with DISPFB set\n", f,
                   (f - n0) / s, (d - d0) / s, (t - t0) / s, t > t0 ? (double)(f - n0) / (double)(t - t0) : 0.0, (fl - fl0) / s);
            q0 = q; n0 = f; t0 = t; d0 = d; fl0 = fl;
        }
    }
    // SIM_STUN_LOW=v: the local player's stun gauge (work + 0xB68) held at v at most from 120 frames
    // after the player is first seen alive until a blow takes it lower (test only: that blow knocks out)
    static int gaugeHeld = -100000;                         // the value held, for SIM_HIT_PNG
    if (const char* e = getenv("SIM_STUN_LOW")) {
        static bool done = false, told = false; static int alive = 0;
        const uint32_t o = ps2world::PlayerObject(0);
        const int v = atoi(e);
        if (!done && o && (int16_t)PsR16(o + 0xb64) > 0 && ++alive >= 120) {
            const int g = (int16_t)PsR16(o + 0xb68);
            if (gaugeHeld != -100000 && g < gaugeHeld) { done = true; printf("SIM: frame %d: stun gauge %d after a blow: no longer held\n", f, g); }
            else if (g > v) {
                if (!told) { told = true; printf("SIM: frame %d: stun gauge %d held at %d (slot %u)\n", f, g, v, ps2::gMem[o + 0xb60]); }
                PsW16(o + 0xb68, (uint16_t)v); gaugeHeld = v;
            } else gaugeHeld = g;
        }
    }
    // SIM_KILL_EVERY=n (the host): the first remote player's life set to 0 once he has been alive for n
    // frames (test only: the host's life record kills him, his respawn follows); "n:all": every n frames
    // the next living remote player in turn. Only a player out of the weapon choice and the walk after a
    // spawn (work + 0xB48 bit 24): the game runs no death for a life set to 0 then (its copy kept standing
    // with life 0, the host's life record held back for about a minute: db137, 30 September)
    if (const char* e = getenv("SIM_KILL_EVERY")) {
        static int aliveFor = 0, kills = 0, next = 1, wait = 0;
        auto killable = [](uint32_t o) { return o && (int16_t)PsR16(o + 0xb64) > 0 && !(PsR32(o + 0xb48) & 0x1000000); };
        if (strstr(e, ":all")) {
            if (++wait >= atoi(e))
                for (int k = 0; k < 16; ++k) {
                    const int w = 1 + (next - 1 + k) % 16;
                    const uint32_t o = ps2world::PlayerObject(w);
                    if (!killable(o)) continue;
                    printf("SIM: frame %d: player %u's life %d set to 0 (test kill %d)\n", f, ps2::gMem[o + 0xb60] + 1, (int16_t)PsR16(o + 0xb64), ++kills);
                    PsW16(o + 0xb64, 0);
                    next = w + 1; wait = 0;
                    break;
                }
        }
        uint32_t o = ps2world::PlayerObject(1);
        static const int killSlot = getenv("SIM_KILL_SLOT") ? atoi(getenv("SIM_KILL_SLOT")) : -1;   // that slot's player instead (netcode tests)
        if (killSlot >= 0) {
            o = 0;
            for (int w = 1; w <= 16; ++w) { const uint32_t q = ps2world::PlayerObject(w); if (!q) break; if (ps2::gMem[q + 0xb60] == killSlot) { o = q; break; } }
        }
        aliveFor = killable(o) && !strstr(e, ":all") ? aliveFor + 1 : 0;
        if (aliveFor >= atoi(e)) {
            aliveFor = 0;
            printf("SIM: frame %d: player %u's life %d set to 0 (test kill %d)\n", f, ps2::gMem[o + 0xb60] + 1, (int16_t)PsR16(o + 0xb64), ++kills);
            PsW16(o + 0xb64, 0);
        }
    }
    // SIM_WALK_AFTER_SPAWN="wait:n": the local player walks up for n frames, wait frames after each spawn
    // (his positions sent)
    static int walkLeft = 0, walkWait = 0;
    if (const char* e = getenv("SIM_WALK_AFTER_SPAWN")) {
        static int lastLife = 0;
        const uint32_t o = ps2world::PlayerObject(0);
        const int life = o ? (int16_t)PsR16(o + 0xb64) : 0;
        if (o && life > 0 && lastLife <= 0) {
            const char* c = strchr(e, ':');
            walkWait = c ? atoi(e) : 0; walkLeft = c ? atoi(c + 1) : atoi(e);
            printf("SIM: frame %d: spawned (life %d): walking %d frames in %d\n", f, life, walkLeft, walkWait);
        }
        if (life <= 0) walkLeft = 0;
        lastLife = life;
    }
    if (walkWait > 0) { --walkWait; gWalkNow = false; }
    else gWalkNow = walkLeft > 0 ? (--walkLeft, true) : false;
    // the pad's walk does not always move a player just spawned: 400 units along x every 20 frames of it
    if (gWalkNow && walkLeft % 20 == 0) {
        float me[3];
        if (ps2world::PlayerPos(0, me)) {
            const float to[3] = {me[0] + 400.f, me[1], me[2]};
            const int n = TeleportPlayer(me, to);
            printf("SIM: frame %d: moved 400 units (%d copies)\n", f, n);
        }
    }
    // SIM_LIVES=n: every n frames this machine's clock (ms, the same for every instance of a bench) and each player's
    // copy here: slot, life, position (tools/dbcheck.py: a player seen dead on one screen while alive on his own)
    static const int livesEvery = getenv("SIM_LIVES") ? atoi(getenv("SIM_LIVES")) : 0;
    if (livesEvery > 0 && f % livesEvery == 0) {
        char b[1024]; int n = snprintf(b, sizeof b, "lives t%llu f%d:", (unsigned long long)GetTickCount64(), f);
        for (int w = 0; w < 17 && n < (int)sizeof b - 64; ++w) {
            const uint32_t o = ps2world::PlayerObject(w);
            if (!o) { if (w) break; continue; }
            float p[3]; for (int k = 0; k < 3; ++k) { uint32_t u = PsR32(o + 0x110 + 4 * k); memcpy(&p[k], &u, 4); }
            n += snprintf(b + n, sizeof b - n, " %s%u:%d:%.0f:%.0f", w ? "" : "*", ps2::gMem[o + 0xb60], (int16_t)PsR16(o + 0xb64), p[0], p[2]);
        }
        puts(b);
    }
    // SIM_PLAYERS=n: every n frames the players' life, stun gauge, accumulated stun damage and down state
    static const int watchEvery = getenv("SIM_PLAYERS") ? atoi(getenv("SIM_PLAYERS")) : 0;
    if (watchEvery > 0 && f % watchEvery == 0) {
        char b[1024]; int n = snprintf(b, sizeof b, "watch f%d dg %lld:", f, ps2world::DgFrames());
        for (int w = 0; w < 3 && n < (int)sizeof b - 240; ++w) {
            const uint32_t o = ps2world::PlayerObject(w);
            if (!o) continue;
            float p[3]; for (int k = 0; k < 3; ++k) { uint32_t u = PsR32(o + 0x110 + 4 * k); memcpy(&p[k], &u, 4); }
            n += snprintf(b + n, sizeof b - n, " %s slot %u life %d stun %d acc %d down %u by %d pos %.0f %.0f %.0f bits", w ? "remote" : "local", ps2::gMem[o + 0xb60],
                          (int16_t)PsR16(o + 0xb64), (int16_t)PsR16(o + 0xb68), (int16_t)PsR16(o + 0xb72), ps2::gMem[o + 0xdf0], (int8_t)ps2::gMem[o + 0xef4], p[0], p[1], p[2]);
            for (int k = 0; k < 11; ++k) n += snprintf(b + n, sizeof b - n, " %x", PsR32(o + 0xb2c + 4 * k));   // the state bits (0x40D5E0)
            n += snprintf(b + n, sizeof b - n, " |");
        }
        puts(b);
    }
    // SIM_TIME_SCALE=k: from the match's first tick on, the PS2's time k times slower than the machine's:
    // the instances of a simulation keep one pace (their game clocks alike) whatever their processor share.
    // SIM_SCALE_FILE=<file>: instead once that file exists (every player joined: a host slowed during the
    // joins lost joiners whose own timers ran at the machine's pace), the network's clock slowed too (the
    // session engine's timers and send budget at the game's pace)
    // The file may hold a number: that scale instead of SIM_TIME_SCALE, taken again whenever it changes (the
    // clocks go on from where they are: a run's pace fitted to the instances in the match)
    if (const char* e = getenv("SIM_TIME_SCALE")) {
        static bool set = false; static double cur = 0;
        const char* sf = getenv("SIM_SCALE_FILE");
        if (!set && !sf && ps2world::GameTicks() > 0) { set = true; ps2world::SetTimeScale(atof(e)); }
        if (sf && f % 5 == 0)
            if (FILE* fp = fopen(sf, "rb")) {
                double k = 0;
                if (fscanf(fp, "%lf", &k) != 1 || k < 1.0 || k > 1000.0) k = atof(e);
                fclose(fp);
                if (!set || k != cur) { set = true; cur = k; ps2world::SetTimeScale(k); ps2world::SetNetTimeScale(k); }
            }
    }
    // SIM_NET_UP=<bits a second>: the session engine's upstream budget (the network base 0x23BF68 + 0x1C,
    // 512000 from its start: 0x16DA94; the US program) set each frame; SIM_NET_WATCH=n: every n frames
    // that budget, the elapsed ms (+0x20) and each open session's credit (+0x38) and rate (+0x7C)
    if (const char* e = getenv("SIM_NET_UP")) PsW32(0x23BF68 + 0x1c, (uint32_t)strtoul(e, nullptr, 10));
    if (const char* e = getenv("SIM_NET_WATCH")) {
        const int n = atoi(e);
        if (n > 0 && f % n == 0) {
            char b[512]; int k = snprintf(b, sizeof b, "net watch f%d: up %u elapsed %u queued-ms %u |", f, PsR32(0x23BF68 + 0x1c), PsR32(0x23BF68 + 0x20), PsR32(0x23BF68 + 0x98));
            const uint32_t sessions = PsR32(0x23BF68 + 0x40);
            for (int i = 0; i < 16 && sessions && k < (int)sizeof b - 40; ++i) {
                const uint32_t se = sessions + 0xa0 * i;
                if (ps2::gMem[se] < 2) continue;
                k += snprintf(b + k, sizeof b - k, " s%d st %u credit %d rate %u", i, ps2::gMem[se], (int32_t)PsR32(se + 0x38), PsR32(se + 0x7c));
            }
            puts(b);
        }
    }
    // SIM_LOBBY_WATCH=n: every n frames each player's actor on this screen with the machine's time (ms
    // since 1970, the same for every instance): tools/desync_report.py compares the screens
    static const int lobbyEvery = getenv("SIM_LOBBY_WATCH") ? atoi(getenv("SIM_LOBBY_WATCH")) : 0;
    if (lobbyEvery > 0 && f % lobbyEvery == 0) {
        std::string s; char b[256];
        snprintf(b, sizeof b, "pw %lld f%d dg %lld tk %lld:", EpochMs(), f, ps2world::DgFrames(), ps2world::GameTicks());
        s = b;
        for (int w = 0; w < 17; ++w) {
            const uint32_t o = ps2world::PlayerObject(w);
            if (!o) { if (w) break; continue; }
            float p[3]; for (int k = 0; k < 3; ++k) { uint32_t u = PsR32(o + 0x110 + 4 * k); memcpy(&p[k], &u, 4); }
            snprintf(b, sizeof b, " %c%u %d %d %u %d %.0f %.0f %.0f %u", w ? 'R' : 'L', ps2::gMem[o + 0xb60], (int16_t)PsR16(o + 0xb64), (int16_t)PsR16(o + 0xb68),
                     ps2::gMem[o + 0xdf0], (int8_t)ps2::gMem[o + 0xef4], p[0], p[1], p[2], PsR16(o + 0x142) & 0xfff);
            s += b;
            for (int k = 0; k < 11; ++k) { snprintf(b, sizeof b, " %x", PsR32(o + 0xb2c + 4 * k)); s += b; }
            float e[3]; for (int k = 0; k < 3; ++k) { uint32_t u = PsR32(o + 0xe00 + 4 * k); memcpy(&e[k], &u, 4); }
            snprintf(b, sizeof b, " e %.0f %.0f %x %x%08x %x%08x %x", e[0], e[2], ps2::gMem[o + 0xde0], PsR32(o + 0x14c), PsR32(o + 0x148),
                     PsR32(o + 0xe14), PsR32(o + 0xe10), ps2world::ActionTick(o));   // his position record, style, input (in use, received), action
            s += b;
            s += " |";
        }
        puts(s.c_str());
    }
    // SIM_SNAP_FILE=<file> holding "k time": picture k of every instance at that time (ms since 1970):
    // drawn from 6 s before (the first 30 frames drawn show nothing yet), snap<k>_<game port>.png at the
    // first frame at or after it
    if (const char* sf = getenv("SIM_SNAP_FILE")) {
        static int done = 0, want = 0, drawn = 0; static long long at = 0, seen = -100000;
        if (f - seen >= 5) {                                    // the file read every 5 frames
            seen = f;
            if (FILE* fp = fopen(sf, "rb")) { int k = 0; long long t = 0; if (fscanf(fp, "%d %lld", &k, &t) == 2 && k > done) { want = k; at = t; } fclose(fp); }
        }
        if (want > done) {
            const long long now = EpochMs();
            if (now >= at - 6000) { ps2::gGsDraw = true; ++drawn; }
            if (now >= at && drawn > 30) {
                drawn = 0;
                char name[64]; snprintf(name, sizeof name, "snap%d_%s.png", want, getenv("SIM_UDP_PORT") ? getenv("SIM_UDP_PORT") : "0");
                ps2::GsWritePng(name);
                printf("SIM: %s at %lld (%lld ms late) frame %d dg %lld\n", name, now, now - at, f, ps2world::DgFrames());
                done = want;
                if (getenv("SIM_GS_DRAW_FROM")) ps2::gGsDraw = false;
            }
        }
    }
    // SIM_LIFE_LOG=1 (test only): the life of the local player and of the first remote one logged when it changes;
    // SIM_LIFE_PNG="count:step": pictures from the local player's first loss of life
    if (getenv("SIM_LIFE_LOG")) {
        static int lastLife[2] = {-100000, -100000}; static bool pngDone = false;
        for (int w = 0; w < 2; ++w) {
            const uint32_t o = ps2world::PlayerObject(w);
            const int life = o ? (int16_t)PsR16(o + 0xb64) : -100000;
            if (life != lastLife[w]) {
                float p[3] = {0, 0, 0}; const bool hp = o && ps2world::PlayerPos(w, p);
                printf("life: f%d %s %d -> %d at %.0f %.0f %.0f\n", f, w ? "remote" : "local", lastLife[w], life, hp ? p[0] : 0.f, hp ? p[1] : 0.f, hp ? p[2] : 0.f);
                if (w == 0 && !pngDone && lastLife[0] > 0 && life < lastLife[0] && life > -100000) {
                    if (const char* e = getenv("SIM_LIFE_PNG")) {
                        pngDone = true; gPunchPngLeft = atoi(e);
                        if (const char* c = strchr(e, ':')) gPunchPngStep = std::max(1, atoi(c + 1));
                        gPunchPngNext = f + 1;
                    }
                }
                lastLife[w] = life;
            }
        }
    }
    // SIM_HIT_PNG="count:step[:r]": `count` pictures `step` frames apart from the first blow the local
    // player takes (r: the first remote player)
    if (const char* e = getenv("SIM_HIT_PNG")) {
        static int last = -100000; static bool started = false;
        const bool remote = strstr(e, ":r") != nullptr;
        const uint32_t o = ps2world::PlayerObject(remote ? 1 : 0);
        const int stun = o ? (int16_t)PsR16(o + 0xb68) : -100000;
        if (!remote && gaugeHeld != -100000 && stun == gaugeHeld) last = stun;   // the value SIM_STUN_LOW put
        if (!started && o && last != -100000 && stun < last && (!remote || (stun < 1000 && last >= 1000))) {   // remote: knocked out (the gauge held at 2000 by the victim)
            started = true;
            gPunchPngLeft = atoi(e);
            if (const char* c = strchr(e, ':')) gPunchPngStep = std::max(1, atoi(c + 1));
            gPunchPngNext = f + 1;
            printf("SIM: hit at frame %d (stun gauge %d -> %d): %d pictures every %d frames\n", f, last, stun, gPunchPngLeft, gPunchPngStep);
        }
        last = stun;
    }
    static const int posEvery = getenv("SIM_POS_EVERY") ? atoi(getenv("SIM_POS_EVERY")) : 0;   // the local player's position
    if (posEvery > 0 && f % posEvery == 0) {
        float p[3];
        if (ps2world::PlayerPos(0, p)) printf("pos frame %d dg %lld: %.1f %.1f %.1f\n", f, ps2world::DgFrames(), p[0], p[1], p[2]);
    }
    // SIM_PNG_ON_MOVE="count[:skip]": the pictures of `count` VBlanks once the local player has moved
    // for `skip` VBlanks (0), after the match's first position (the walk of a step file)
    static int pngLeft = -1, moved = 0; static float last[3] = {};
    if (const char* e = getenv("SIM_PNG_ON_MOVE")) {
        float p[3];
        if (pngLeft < 0 && ps2world::PlayerPos(0, p)) {
            const float d = fabsf(p[0] - last[0]) + fabsf(p[1] - last[1]) + fabsf(p[2] - last[2]);
            if (d > 0.5f && d < 500.0f && (last[0] != 0 || last[2] != 0)) ++moved;
            memcpy(last, p, sizeof last);
            const char* c = strchr(e, ':');
            if (moved > (c ? atoi(c + 1) : 0)) { pngLeft = atoi(e); printf("SIM: pictures of %d VBlanks from frame %d\n", pngLeft, f); }
        }
        if (pngLeft > 0) {
            --pngLeft;
            char name[64]; snprintf(name, sizeof name, "mv_%05d.png", f);
            ps2::GsWritePng(name);
            if (ps2world::PlayerPos(0, p)) printf("SIM: %s dg %lld pos %.1f %.1f %.1f\n", name, ps2world::DgFrames(), p[0], p[1], p[2]);
        }
    }
    ShotLogFrame(f);
    for (size_t k = 0; k < gWatch.size(); ++k) {
        uint32_t v = 0; memcpy(&v, ps2::gMem + gWatch[k].first, gWatch[k].second);
        if (v != gWatchPrev[k]) { printf("watch frame %d dg %lld: %08x = %x\n", f, ps2world::DgFrames(), gWatch[k].first, v); gWatchPrev[k] = v; }
    }
    // SIM_READY_WATCH=1: the local player's readied byte (the US program's [0x284390] + 0xB4C: 0x88 readied, 0x80
    // lowered) as the game leaves it, from his first frame alive (his life, + 0xB64, above 0), and his object as it
    // changes
    if (getenv("SIM_READY_WATCH")) {
        static int seen = -1; static uint32_t lastObj = 0, lastByte = 0x100;
        const uint32_t o = ps2world::PlayerObject(0) & 0x01ffffff;
        if (o != lastObj) {
            printf("ready watch frame %d: the local player's object %08x ([0x284390] %08x)\n", f, o, PsR32(0x284390));
            lastObj = o;
        }
        if (o && seen < 0 && (int16_t)PsR16(o + 0xb64) > 0) { seen = f; printf("ready watch frame %d: alive\n", f); }
        if (o && seen >= 0 && ps2::gMem[o + 0xb4c] != lastByte) {
            lastByte = ps2::gMem[o + 0xb4c];
            printf("ready watch frame %d (+%d): + 0xB4C = %02x\n", f, f - seen, lastByte);
        }
    }
    // SIM_PNG_NOW="count,step" (sixty, set by a =setenv step): pictures from this frame on, as SIM_PNG_STEPS draws them
    if (const char* e = getenv("SIM_PNG_NOW")) if (*e) {
        int n = 0, st = 1;
        if (sscanf(e, "%d,%d", &n, &st) >= 1 && n > 0) {
            gStepPngLeft = n; gStepPngStep = std::max(1, st); gStepPngNext = f + gStepPngStep;
            printf("SIM: pictures from frame %d: %d every %d frames\n", f, n, gStepPngStep);
        }
        _putenv("SIM_PNG_NOW=");
    }
    StepPngFrame(f);                                        // SIM_PNG_STEPS (open list)
    // SIM_PNG_ON_CHANGE="expr:count[:skip[:times]]" (open list, the HUD's count at 60): once the value of expr
    // (SIM_WATCH_EXPR's syntax, 2 bytes) changes after `skip` changes, the picture of each of the next `count` VBlanks
    // (ch_<frame>.png) with the value and the DG clock (SIM_DG_CLOCK, US 0x23AFD0) at that VBlank; again (`times` in
    // all) at the first change after the value stayed the same 120 frames
    if (const char* e = getenv("SIM_PNG_ON_CHANGE")) {
        static int left = -1, changes = 0, done = 0, still = 0; static uint32_t last = 0xFFFFFFFF;
        std::string ex(e); size_t c1 = ex.find(':');
        const std::string expr = ex.substr(0, c1);
        const int count = c1 == std::string::npos ? 60 : atoi(ex.c_str() + c1 + 1);
        const size_t c2 = c1 == std::string::npos ? std::string::npos : ex.find(':', c1 + 1);
        const int skip = c2 == std::string::npos ? 0 : atoi(ex.c_str() + c2 + 1);
        const size_t c3 = c2 == std::string::npos ? std::string::npos : ex.find(':', c2 + 1);
        const int times = c3 == std::string::npos ? 1 : atoi(ex.c_str() + c3 + 1);
        static const uint32_t clockAt = getenv("SIM_DG_CLOCK") ? (uint32_t)strtoul(getenv("SIM_DG_CLOCK"), nullptr, 16) : 0x23AFD0;
        const char* p = expr.c_str();
        const uint32_t a = wexpr::Expr(p) & 0x01ffffff;
        uint32_t v = 0; if (a > 0x100000 && a + 2 <= 0x02000000) memcpy(&v, ps2::gMem + a, 2);
        const bool changed = a > 0x100000 && last != 0xFFFFFFFF && v != last;
        if (left == 0 && done < times) { still = changed ? 0 : still + 1; if (still >= 120) left = -1; }   // armed again
        if (left < 0 && changed && ++changes > skip) {
            left = count; ++done; printf("SIM: the value at %08x changed (%u -> %u) at frame %d: %d pictures (%d of %d)\n", a, last, v, f, count, done, times);
        }
        last = v;
        if (left > 0) {
            ps2::gGsDraw = true;
            char name[64]; snprintf(name, sizeof name, "ch_%05d.png", f);
            ps2::GsWritePng(name);
            printf("SIM: %s value %u clock %u dg %lld\n", name, v, ps2::gMem[clockAt & 0x01ffffff] & 1, ps2world::DgFrames());
            if (--left == 0 && getenv("SIM_GS_DRAW_FROM")) ps2::gGsDraw = false;
        }
    }
    wexpr::Frame(f);
}
static int NativeMain(int argc, char** argv) {
    ps2world::Platform p{};
    p.log = &Log;
    p.padHeld = &PadHeld;
    p.frame = &NativeFrame;
    if (const char* q = getenv("SIM_SQUARE")) {           // the Square pressure timeline, and the pad's pressures from it
        for (const char* c = q; *c;) {
            char* e = nullptr; const int f = (int)strtol(c, &e, 10);
            if (!e || *e != ':') break;
            const int v = (int)strtol(e + 1, &e, 0);
            gSquare.push_back({f, v});
            if (!e || *e != ',') break;
            c = e + 1;
        }
        p.padAnalog = &PadAnalog;
        printf("square: %zu pressure changes\n", gSquare.size());
    }
    doll::Init();                                         // MGO1_DOLL: a PLAYABLE DOLL (doll.h): the pad with its sticks
    if (doll::Enabled()) p.padAnalog = &PadAnalog;
    p.matchFps = getenv("SIM_MATCH_FPS") ? atoi(getenv("SIM_MATCH_FPS")) : 0;   // 60: the matches at 60 frames a second
    // at 60: SIM_FPS_RULES=fair the engine's full rate with the fair rules (up to v123), off without
    // rules, else the PS2's own game at its pace with a picture after each pass (exact 60)
    p.fps60Kind = !getenv("SIM_FPS_RULES") ? 0 : !strcmp(getenv("SIM_FPS_RULES"), "fair") ? 1 : !strcmp(getenv("SIM_FPS_RULES"), "off") ? 2 : 0;
    p.palAt60 = !getenv("SIM_PAL60") || atoi(getenv("SIM_PAL60")) != 0;   // SIM_PAL60=0: the PAL program at its 50 fields
    p.m37Off = getenv("SIM_M37") && !strcmp(getenv("SIM_M37"), "off");                   // the M37 as the PS2 has it (measures)
    p.wideX = getenv("SIM_WIDE_X") ? (float)atof(getenv("SIM_WIDE_X")) : 0.0f;  // 0.75: a 16:9 view (0.5714: 21:9)
    p.hudX = getenv("SIM_HUD_X") ? (float)atof(getenv("SIM_HUD_X")) : 0.0f;     // the 2D elements' width (0.75: 16:9)
    p.lobbyWide = getenv("SIM_LOBBY_WIDE") && atoi(getenv("SIM_LOBBY_WIDE"));    // the lobby in the wide picture too
    if (getenv("SIM_HUD_SCALE")) ps2::GsSetHudScale((float)atof(getenv("SIM_HUD_SCALE")));   // the HUD's size (the in-game menu)
    long long frames = argc > 2 ? atoll(argv[2]) : 300;
    if (argc > 4) { const char* q = argv[4]; while (*q) { uint32_t a = strtoul(q, nullptr, 16); const char* c = strchr(q, ':'); int sz = c ? atoi(c + 1) : 4;
                    gWatch.push_back({a, sz}); gWatchPrev.push_back(0xdeadbeef); const char* n = strchr(q, ','); if (!n) break; q = n + 1; } }
    wexpr::Setup();
    LoadSteps();
    if (getenv("SIM_FAIR")) p.padAnalog = &PadAnalog;      // =fair: the left stick
    for (const Step& x : gSteps) {                        // a step with a pressure or the sticks: the pad reports them
        bool pr = x.pressure >= 0 || x.stick[0] >= 0;
        for (const auto& k : x.chords) pr = pr || k.pressure >= 0;
        if (pr && !p.padAnalog) { p.padAnalog = &PadAnalog; printf("steps: pressures from the steps\n"); }
    }
    if (const char* e = getenv("SIM_PLAY_TURN")) { gPlayTurn = (int)strtoul(e, nullptr, 16) & 0xff; p.padAnalog = &PadAnalog; }
    if (PcReady()) p.padAnalog = &PadAnalog;              // the ready weapon's pressures
    // MGO1's [UDP Port No.]: the platform sets it at the version's own address (Platform::udpPort)
    gUdpPort = getenv("SIM_UDP_PORT") ? atoi(getenv("SIM_UDP_PORT")) : 0;
    p.udpPort = gUdpPort;
    p.dnsServer = getenv("SIM_DNS");                      // the DNS server names are asked of (MGO1.exe: SaveMGO's)
    p.knownHosts = getenv("SIM_KNOWN_HOSTS");             // its answers kept for when it does not answer
    p.gameLanguage = getenv("SIM_GAME_LANG") ? atoi(getenv("SIM_GAME_LANG")) : 0;   // the scripts' language (3 German ...)
    p.scriptEnglish = getenv("SIM_SCRIPT_EN") != nullptr;                               // with it: the English texts
    p.textsFrom = getenv("SIM_TEXTS_FROM");                                             // another version's folder: its second texts
    if (const char* e = getenv("SIM_AUDIO_RT")) {          // the sound pulled in real time as MGO1.exe's device does
        static std::string wav = strcmp(e, "1") ? e : "";
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD { AudioRt(wav.c_str()); return 0; }, nullptr, 0, nullptr);
    }
    if (const char* e = getenv("SIM_SCREEN_HZ")) simscreen::Start(atof(e));   // before the GS thread, as MGO1.exe's StartGpu
    const int r = ps2world::NativeRun(p, argc > 1 ? argv[1] : ".", frames);
    printf("RESULT native run: %s, %d steps of %zu done\n", r ? "failed" : "ended", (int)gStep, gSteps.size());
    JoinLockRelease("the end");
    fflush(nullptr);
    ExitProcess((UINT)r);                                  // as MGO1.exe: the GS and render threads end before any static is destroyed
}
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("mgo1host.exe %s\n", MGO1_BUILD);              // (build 1.00) the build, the first line of the host's and of a playable doll's log
    SetUnhandledExceptionFilter(CrashFilter);
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    if (getenv("SIM_NATIVE") && atoi(getenv("SIM_NATIVE"))) return NativeMain(argc, argv);
    // the root's options come from the script (region case, default 3 = US retail);
    // SIM_HOSTS sends the names and addresses it gives to local stand-ins
    gVmOn = !getenv("SIM_SCRIPT_MAIN") || atoi(getenv("SIM_SCRIPT_MAIN")) != 0;
    int regionCase = getenv("SIM_REGION_CASE") ? atoi(getenv("SIM_REGION_CASE")) : 3;
    if (!gVmOn && !RootOptionsFromScript(argc > 1 ? argv[1] : ".", regionCase)) {
        printf("RESULT the root's options were not found in ntmenu2.gcx\n");
        return 1;
    }
    ps2world::Platform p{};
    p.log = &Log; p.gvNewActor = &NewActor; p.gvSetNamedActor = &SetNamed; p.actorLevel = 0;
    p.gclGetOption = &GetOption; p.gclNextValue = &NextValue;
    p.l2dLoad = &Load; p.l2dGetObj = &GetObj; p.l2dBusy = &Busy; p.l2dRunning = &Running; p.l2dEvoke = &Evoke; p.l2dStop = &Stop;
    p.padHeld = &PadHeld;
    p.gclStrRes = &StrRes; p.gclExecProc = &ExecProc; p.gclGetVar = &GetVar; p.gclSetVar = &SetVar;
    p.gclExecBlock = &ExecBlock;
    p.gclCursor = &gCursor;
    p.stageFlags = &gStageFlags;
    p.gclStrings = &Strings;
    p.l2dSetText = [](void*, void*, const char*) {};
    p.l2dTextPrep = [](void*, void*, const char*) {};
    if (argc > 3) {                                     // e.g. "60:40,120:40,180:800"
        const char* q = argv[3];
        while (*q) { int f = atoi(q); const char* c = strchr(q, ':'); if (!c) break; unsigned b = strtoul(c + 1, nullptr, 16);
                     gScript.push_back({f, b}); const char* n = strchr(c, ','); if (!n) break; q = n + 1; }
    }
    if (!ps2world::Init(p, argc > 1 ? argv[1] : ".")) { printf("RESULT init failed\n"); return 1; }
    if (getenv("SIM_DIVTEST")) {
        for (int v : {12345, 7, 100, -12345}) {
            void* q = ps2world::CallCode(0x1e7eb0, v, 10);
            void* r = ps2world::CallCode(0x1e8680, v, 10);
            printf("DIVTEST %d / 10 = %d, %% 10 = %d\n", v, (int)(intptr_t)q, (int)(intptr_t)r);
        }
        return 0;
    }
    void* root = nullptr;
    if (gVmOn) {                                     // the script's main: procs 10 and 11, [bf051f], the region's [ccbefe], the texts
        if (!SetupVm(argc > 1 ? argv[1] : ".", regionCase)) return 1;
        gVm.RunMain();
        root = gRoot;
    } else {
        LoadScriptStrings(argv[1]);
        Proc11();                                    // main runs @proc10 @proc11 before [ccbefe]
        root = ps2world::NewOnlineMenu(0x41b563, 0);
    }
    int frames = argc > 2 ? atoi(argv[2]) : 300;
    // watch: "addr:size" list in argv[4] (e.g. 0x10325ba:2 = NewOLMLoad state), changes logged
    if (argc > 4) { const char* q = argv[4]; while (*q) { uint32_t a = strtoul(q, nullptr, 16); const char* c = strchr(q, ':'); int sz = c ? atoi(c + 1) : 4;
                    gWatch.push_back({a, sz}); gWatchPrev.push_back(0xdeadbeef); const char* n = strchr(q, ','); if (!n) break; q = n + 1; } }
    wexpr::Setup();
    int traceFrom = getenv("SIM_TRACE_FROM") ? atoi(getenv("SIM_TRACE_FROM")) : -1;
    int traceTo = getenv("SIM_TRACE_TO") ? atoi(getenv("SIM_TRACE_TO")) : -1;
    // frames at 60 per second, as the game runs them: PS2 threads that sleep
    // (DelayThread) wake after real time, so the frames must not outrun it
    ULONGLONG t0 = GetTickCount64();
    bool pace = !getenv("SIM_FAST");
    LoadSteps();
    int udpPort = getenv("SIM_UDP_PORT") ? atoi(getenv("SIM_UDP_PORT")) : 0;   // MGO1's [UDP Port No.] setting
    for (int f = 0; f < frames; ++f, ++gFrame) {
        gFrameNow = f;
        if (udpPort) {                                 // the online data (0x28D288) + 0x84, 5730 by default
            uint16_t v; memcpy(&v, ps2::gMem + 0x28D30C, 2);
            if (v == 5730) { v = (uint16_t)udpPort; memcpy(ps2::gMem + 0x28D30C, &v, 2); }
        }
        StepFrame(f);
        if (pace) {
            // one frame per NTSC field (1001 / 60000 s) after the previous one, as the PS2's VBlank paces it: a late frame
            // is not caught up by running the next ones back to back (the game network divides by the
            // milliseconds between its ticks, 0 in such a burst: the trap at 0x16D4E4)
            static LARGE_INTEGER freq{}, last{};
            if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&last); }
            const LONGLONG step = freq.QuadPart * 1001 / 60000;
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            while (now.QuadPart - last.QuadPart < step) {
                const LONGLONG left = (step - (now.QuadPart - last.QuadPart)) * 1000 / freq.QuadPart;
                Sleep(left > 1 ? (DWORD)(left - 1) : 0);
                QueryPerformanceCounter(&now);
            }
            last = now;
            (void)t0;
        }
        if (const char* e = getenv("SIM_SLOW_MS")) Sleep((DWORD)atoi(e));   // a slow machine: every frame late by that much
        if (gFrameCallsLeft > 0 && --gFrameCallsLeft == 0) {         // SIM_FIRE_CALLS: the window ended
            ps2::gCallHook = nullptr;
            std::string t;
            for (auto& x : gFrameCalls) { char b[24]; snprintf(b, sizeof b, " %x:%u", x.first, x.second); t += b; }
            printf("frame calls (%s, frame %d, %zu functions):%s\n", gFrameCallsTag, gFrame, gFrameCalls.size(), t.c_str());
        }
        ps2::gCallTrace = (f >= traceFrom && f <= traceTo);
        ps2::gCallTraceAll = getenv("SIM_TRACE_ALL") != nullptr;
        if (ps2::gCallTrace) printf("---- frame %d\n", f);
        RunActors();
        const int dying = ProcessDeaths();
        // the engine's stage manager (PS2 0x1043D0 / 0x104630, PC exe+0x980E6) at the end of
        // the frame: flag 1 loads the next stage (its name from the last load command, else
        // the current page again), 0x40 runs the same stage again without reloading it. The
        // stage's actors go to the death list first; the load waits until the list has been
        // empty two frames in a row (0x1044B8: 0x122828, counter +0x44)
        static int stageWait = -1;
        static uint32_t stageFlags = 0;
        static LoadRequest stageNext;
        if (stageWait < 0 && (gStageFlags & (1 | 0x40))) {
            stageFlags = gStageFlags;
            gStageFlags = 0;
            stageNext = gLoad;
            gLoad = LoadRequest();
            if (stageNext.stage.empty()) stageNext.stage = gPage;
            const bool same = (stageFlags & 0x40) != 0;
            int n = PurgeStage();
            printf("SIM: stage %s \"%s\" (resident \"%s\", flags %#x) at frame %d: %d actors of class 0 to the death list\n",
                   same ? "restart of" : "change to", same ? gPage.c_str() : stageNext.stage.c_str(), stageNext.resident.c_str(), stageFlags, f, n);
            stageWait = 2;
        } else if (stageWait >= 0) {
            if (dying) stageWait = 2;
            else if (--stageWait <= 0) stageWait = -2;       // the load, below
        }
        if (stageWait == -2) {
            stageWait = -1;
            const bool same = (stageFlags & 0x40) != 0;
            const LoadRequest next = stageNext;
            printf("SIM: stage %s \"%s\": the death list is empty, the load starts at frame %d\n", same ? "restart of" : "change to",
                   same ? gPage.c_str() : next.stage.c_str(), f);
            if (!same) ps2world::OverlayUnloaded(next.stage.c_str());
            if (const char* e = getenv("SIM_AFTER_LOAD")) { int more = atoi(e); if (more >= 0 && f + 1 + more < frames) frames = f + 1 + more; }
            // the page's overlay at 0x2DA680 (read again unless the same stage runs again) and
            // its script's main, as the engine runs them once the page is in (the resident
            // page's own script is left out)
            std::string page = same ? gPage : next.stage;
            bool in = same || (gVmOn && !getenv("SIM_NO_NEXT_PAGE") && ps2world::LoadPageOverlay(page.c_str()) && SetupPageScript(page));
            if (in && !same && !getenv("SIM_NO_PAGE_DATA"))  // the page's data from the archives, as the PS2 loader reads it
                printf("SIM: page data of %s: %s\n", page.c_str(), ps2world::LoadPageData(page.c_str(), next.resident.c_str()) ? "loaded" : "not loaded (no archives)");
            if (in && same && !getenv("SIM_NO_PAGE_DATA")) ps2world::StageRestart();   // the PS2 stage manager's restart steps
            if (gVmOn && in && !getenv("SIM_NO_NEXT_PAGE")) {
                printf("SIM: %s's script main runs\n", page.c_str());
                gVm.RunMain();
                printf("SIM: %s's script main done\n", page.c_str());
            }
        }
        if (const char* da = getenv("SIM_DUMP_AT")) {       // "frame,frame...": all PS2 memory at the end of those frames
            for (const char* q = da; *q;) {
                char* end; long v = strtol(q, &end, 10); if (end == q) break;
                if (v == f) {
                    char name[64]; snprintf(name, sizeof name, "dump_%d.bin", f);
                    std::vector<uint8_t> copy(ps2::gMem, ps2::gMem + 0x02000000);   // no log-in data in a dump
                    ps2world::ScrubSecrets(copy.data(), copy.size());
                    if (FILE* fd = fopen(name, "wb")) { fwrite(copy.data(), 1, copy.size(), fd); fclose(fd); printf("SIM: memory at frame %d written to %s\n", f, name); }
                }
                q = *end ? end + 1 : end;
            }
        }
        for (size_t k = 0; k < gWatch.size(); ++k) {
            uint32_t v = 0; memcpy(&v, ps2::gMem + gWatch[k].first, gWatch[k].second);
            if (v != gWatchPrev[k]) { printf("watch frame %d: %08x = %x\n", f, gWatch[k].first, v); gWatchPrev[k] = v; }
        }
        wexpr::Frame(f);
    }
    if (const char* dump = getenv("SIM_DUMP")) {          // "addr:len": raw bytes to a file at the end of the run
        uint32_t a = strtoul(dump, nullptr, 16), n = 0; const char* c = strchr(dump, ':'); if (c) n = strtoul(c + 1, nullptr, 16);
        std::vector<uint8_t> copy(ps2::gMem + a, ps2::gMem + a + n);            // no log-in data in a dump
        ps2world::ScrubSecrets(copy.data(), copy.size());
        if (FILE* f = fopen("sim_dump.bin", "wb")) { fwrite(copy.data(), 1, n, f); fclose(f); printf("dumped %x bytes at %08x\n", n, a); }
    }
    if (const char* find = getenv("SIM_FIND")) {          // where a text sits in PS2 memory at the end of the run
        size_t n = strlen(find); int shown = 0;
        for (uint32_t a = 0x100000; a + n < 0x2000000 && shown < 12; ++a)
            if (!memcmp(ps2::gMem + a, find, n)) { printf("found \"%s\" at %08x\n", find, a); ++shown; }
    }
    int hidden = 0;
    for (auto& n : nodes) { unsigned fl; memcpy(&fl, n.second + 0x10, 4); if (!(fl & 0x400000)) ++hidden; }
    printf("RESULT root %p, %d layouts loaded, %zu objects touched, %d hidden by MGO1's code, %d evokes, %zu actors\n",
           root, loads, nodes.size(), hidden, evokes, actors.size());
    return root ? 0 : 1;
}
