// The PLAYABLE DOLL (doll.h): what it sees (the players, the room, the camera, the map's mesh), how it
// moves (the stick from the camera, the paths, the postures), its status and its orders. What it
// decides is in doll_brain.cpp, the measures of the controls in doll_probe.cpp (doll_int.h).
#include "doll_int.h"
#include "ps2world.h"
#include "ps2rt.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <thread>

namespace doll {
namespace in {

bool gOn = false;
std::string gDir;                               // MGO1_DOLL: status and commands
int gProbe = 0;                                 // MGO1_DOLL_PROBE: measures of the controls (tests)
int gDebug = 0;                                 // MGO1_DOLL_DEBUG: the game's actors, messages and the player's state in the log
bool gObserve = false;                          // MGO1_DOLL_OBSERVE: never take the pad (the steps play)
static bool gOldWays = false;                   // MGO1_DOLL_OLD_WAYS (tests: v134's ways, a before and after)
static bool gOldFight = false;                  // MGO1_DOLL_OLD_FIGHT (tests: v135's fights, no moves)
static int gTrack = 0;                          // MGO1_DOLL_TRACK: every player's place logged every n frames (tests)
static int gMotion = 0;                         // MGO1_DOLL_MOTION: the doll's moves and pad every n frames, its spawns' events (tests)
static const float kWalkPush = 0.7f;            // the stick's larger axis for a walk (0.75 runs: docs 3)
Pad gPad;
bool gControl = false;
long long gFrame = 0;
static unsigned gSeed = 0x2545F491u;
unsigned Rnd() { gSeed ^= gSeed << 13; gSeed ^= gSeed >> 17; gSeed ^= gSeed << 5; return gSeed; }
float Rnd01() { return (Rnd() & 0xFFFFFF) / 16777216.f; }

// ---- its settings (MGO1_DOLL_INI, read once then deleted: it can hold the game's password) and its
// orders (the `command` file in its folder, appended by the server's agent, read and emptied here) ----
Settings gSet;
bool Parity() { return gSet.parity != 0; }      // ((v150-host beta 16) doll_brain.cpp kVdollLevel)
bool DirectAim() { return gSet.aim != 0; }      // ((v150-host beta 18) doll_aim.h)
static void Order(const std::string& line);
static void ReadIni() {
    const char* path = getenv("MGO1_DOLL_INI");
    if (!path) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "game") gSet.game = v;
        else if (k == "game_password") gSet.gamePassword = v;
        else if (k == "difficulty") gSet.difficulty = std::max(1, std::min(10, atoi(v.c_str())));
        else if (k == "adapt") gSet.adapt = v != "0" && v != "off";
        else if (k == "adapt_min") gSet.adaptMin = std::max(1, std::min(10, atoi(v.c_str())));
        else if (k == "adapt_max") gSet.adaptMax = std::max(1, std::min(10, atoi(v.c_str())));
        else if (k == "team") gSet.team = v;
        else if (k == "bot") gSet.bot = atoi(v.c_str());
        else if (k == "doll") gSet.number = atoi(v.c_str());
        else if (k == "loadout") gSet.loadout = v;
        else if (k == "order") Order("order " + v);
        else if (k == "parity") gSet.parity = v == "vdoll" || v == "1" || v == "on" ? 1 : 0;   // ((v150-host beta 16) the virtual dolls' level)
        else if (k == "aim") gSet.aim = v == "direct" ? 1 : 0;   // ((v150-host beta 18) the direct aim; anything else: the pad)
    }
    fclose(f);
    DeleteFileA(path);                           // the game's password is not left on the disk
}
static void ReadCommands() {
    if (gDir.empty()) return;
    const std::string path = gDir + "\\command", work = gDir + "\\command.work";
    if (!MoveFileExA(path.c_str(), work.c_str(), MOVEFILE_REPLACE_EXISTING)) return;
    FILE* f = fopen(work.c_str(), "rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (!l.empty()) Order(l);
    }
    fclose(f);
    DeleteFileA(work.c_str());
}
// the names of the server's dolls (MGO1_DOLL_NAMES, written by mgo1doll-manager, copied at the start
// and read again from the server's own file when it changes): anyone else is a real player
static std::vector<std::string> gDollNames;
static void ReadDollNames() {
    const char* path = getenv("MGO1_DOLL_NAMES");
    if (!path) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    gDollNames.clear();
    char line[64];
    while (fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        for (char& c : l) c = (char)toupper((unsigned char)c);
        if (!l.empty()) gDollNames.push_back(l);
    }
    fclose(f);
}
bool IsDollName(std::string n) {
    for (char& c : n) c = (char)toupper((unsigned char)c);
    for (const auto& d : gDollNames) if (d == n) return true;
    return false;
}

// ---- the game's memory ----
uint32_t R32(uint32_t a) { a &= 0x01FFFFFF; uint32_t v; memcpy(&v, ps2::gMem + a, 4); return v; }
uint16_t R16(uint32_t a) { a &= 0x01FFFFFF; uint16_t v; memcpy(&v, ps2::gMem + a, 2); return v; }
int16_t R16s(uint32_t a) { a &= 0x01FFFFFF; int16_t v; memcpy(&v, ps2::gMem + a, 2); return v; }
uint8_t R8(uint32_t a) { return ps2::gMem[a & 0x01FFFFFF]; }
float RF(uint32_t a) { a &= 0x01FFFFFF; float v; memcpy(&v, ps2::gMem + a, 4); return v; }
const uint8_t* GameMem() { return ps2::gMem; }
void W16(uint32_t a, int v) { a &= 0x01FFFFFF; const int16_t x = (int16_t)v; memcpy(ps2::gMem + a, &x, 2); }   // ((v150-host beta 18) doll_brain.cpp DirectFacing, DirectView)

// ---- the vaults' second look (v137): over a sheet (a fence, a railing: under 60 mm thick) the generator's 1.55 m
// stand; over a wall the action button climbs only a top within climbFacesMax (1.2 m) of the floor at its foot on
// both sides (docs/PLAYABLE_DOLL.md 2). The others left out of the paths when the mesh is built ----
struct VaultLook { bool judged = false; float thick = 0.f, foot[2] = {0.f, 0.f}, top = 0.f; };
static VaultLook LookAtVault(const NavMesh& nav, const float* a, const float* b) {
    VaultLook v;
    const float l = std::hypot(b[0] - a[0], b[2] - a[2]);
    if (l < 1.f) return v;
    const float ux = (b[0] - a[0]) / l, uz = (b[2] - a[2]) / l, len = l + 200.f;
    auto at = [&](float r, float y, float* out) { out[0] = a[0] + ux * r; out[1] = y; out[2] = a[2] + uz * r; };
    // (its near and far faces on the line: the first hits from each side)
    float ta = 0.f, tb = 0.f, n[3];
    bool hit = false;
    for (float h : {350.f, 250.f, 650.f}) {
        const float p[3] = {a[0] - ux * 100.f, a[1] + h, a[2] - uz * 100.f}, q[3] = {b[0] + ux * 100.f, b[1] + h, b[2] + uz * 100.f};
        if (nav.FirstHit(p, q, &ta, n) && nav.FirstHit(q, p, &tb, n)) { hit = true; break; }
    }
    if (!hit) return v;
    const float nearAt = ta * len - 100.f, farAt = (1.f - tb) * len - 100.f;
    v.thick = farAt - nearAt;
    float p[3], q[3];
    at(nearAt - 200.f, a[1] + 600.f, p); v.foot[0] = nav.GroundBelow(p, 2000.f);
    at(farAt + 200.f, b[1] + 600.f, p); v.foot[1] = nav.GroundBelow(p, 2000.f);
    if (v.foot[0] < -1e29f || v.foot[1] < -1e29f) return v;
    // (its top: the first height over the higher foot clear across it, 0.1 m above too)
    const float base = std::max(v.foot[0], v.foot[1]);
    for (float h = 100.f; h <= 2600.f; h += 50.f) {
        float p2[3], q2[3];
        at(nearAt - 250.f, base + h, p); at(farAt + 250.f, base + h, q);
        at(nearAt - 250.f, base + h + 100.f, p2); at(farAt + 250.f, base + h + 100.f, q2);
        if (nav.Clear(p, q) && nav.Clear(p2, q2)) { v.top = base + h - 25.f; v.judged = true; break; }
    }
    return v;
}
static int VaultsSecondLook(NavMesh& nav, std::string& lines) {    // the vaults left out (their lines in debug)
    static const bool crossLog = getenv("NAV_CROSS_LOG") != nullptr;
    int left = 0;
    for (int i = 0; i < nav.LinkCount(); ++i) {
        float a[3], b[3]; uint8_t area = 0;
        if (!nav.LinkAt(i, a, b, &area) || area != AREA_VAULT) continue;
        const VaultLook v = LookAtVault(nav, a, b);
        if (!v.judged || v.thick < 60.f || std::max(v.top - v.foot[0], v.top - v.foot[1]) <= nav.Config().climbFacesMax) continue;
        if (!nav.DisableLink(a, b, 50.f)) continue;
        ++left;
        if (!gDebug && !crossLog) continue;
        char s[256];
        snprintf(s, sizeof s, "\ncrossing: the vault at %.0f %.0f %.0f to %.0f %.0f %.0f left out: a wall %.0f mm thick, its top %.0f and %.0f mm over the floors at its foot",
                 a[0], a[1], a[2], b[0], b[1], b[2], v.thick, v.top - v.foot[0], v.top - v.foot[1]);
        lines += s;
    }
    return left;
}

// ---- the map: its mesh, built on a thread when a map's page comes in ----
std::shared_ptr<MapState> gMap;
bool MapReady() { return gMap && gMap->state == 2; }
static std::string gLearnDir;                        // (MGO1_DOLL_LEARN: the server's dolls' folder)
// ---- (v141 beta 4) the map's meshes kept in the server's dolls' folder: every doll built every map at its start, all
// at the same moment (1.5 to 20 s of a core each, the bot's game short of processor meanwhile); now the first one
// builds it (the others wait for it, 90 s at most) and saves it, the others and the next games read it.
// <key>_<the collision's hash>.nav: the mesh, the wide one, the vaults left out. A file of another build of doll_nav.cpp
// is built again (its stamp); a change of VaultsSecondLook here needs doll_nav.cpp built again too (or the .nav files
// deleted) ----
static uint64_t Fnv64(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
static bool ReadWhole(const std::string& path, std::string& out) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (256LL << 20);
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr) && got == out.size();
    }
    CloseHandle(h);
    return ok;
}
static bool WriteWhole(const std::string& path, const std::string& data) {   // (a new file, then in place of the old one)
    const std::string tmp = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD done = 0;
    const bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &done, nullptr) && done == data.size();
    CloseHandle(h);
    if (!ok || !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp.c_str()); return false; }
    return true;
}
struct MapMeshes { std::unique_ptr<NavMesh> nav, wide; std::string log, vaultLines; int vaultsOut = 0, wideOut = 0; };
static void PutPart(std::string& o, const std::string& part) { const uint32_t n = (uint32_t)part.size(); o.append((const char*)&n, 4); o += part; }
static bool GetPart(const std::string& s, size_t& at, std::string& part) {
    uint32_t n = 0;
    if (at + 4 > s.size()) return false;
    memcpy(&n, s.data() + at, 4); at += 4;
    if (at + n > s.size()) return false;
    part.assign(s.data() + at, n); at += n;
    return true;
}
static bool MeshesRead(const std::string& path, const Geom& g, MapMeshes& mm) {
    std::string all, a, b, v, counts;
    size_t at = 0;
    if (!ReadWhole(path, all) || !GetPart(all, at, a) || !GetPart(all, at, b) || !GetPart(all, at, v) || !GetPart(all, at, counts) || counts.size() != 8) return false;
    auto nav = std::make_unique<NavMesh>(), wide = std::make_unique<NavMesh>();
    std::string log, log2;
    if (!nav->Load(g, a, log)) return false;
    if (!b.empty() && !wide->Load(g, b, log2)) return false;
    mm.nav = std::move(nav); mm.wide = b.empty() ? nullptr : std::move(wide);
    mm.log = log; mm.vaultLines = v;
    memcpy(&mm.vaultsOut, counts.data(), 4); memcpy(&mm.wideOut, counts.data() + 4, 4);
    return true;
}
static void MeshesWrite(const std::string& path, const MapMeshes& mm) {
    std::string a, b, all;
    if (!mm.nav->Save(a, mm.log) || (mm.wide && !mm.wide->Save(b, ""))) return;
    PutPart(all, a); PutPart(all, b); PutPart(all, mm.vaultLines);
    char counts[8]; memcpy(counts, &mm.vaultsOut, 4); memcpy(counts + 4, &mm.wideOut, 4);
    PutPart(all, std::string(counts, 8));
    WriteWhole(path, all);
}
static void BuildMap(std::shared_ptr<MapState> m) {
    std::vector<uint8_t> bytes; std::string err;
    const DWORD t0 = GetTickCount();
    if (!ps2world::StageResource(m->page, 0x0C, bytes, err, m->extra)) { m->log = m->key + ": no collision (" + err + ")"; m->state = 3; return; }
    auto g = std::make_unique<Geom>();
    if (!DecodeGeom(bytes.data(), bytes.size(), *g, err)) { m->log = m->key + ": " + err; m->state = 3; return; }
    NavConfig cfg;
    MapMeshes mm;
    std::string cache, how;
    HANDLE lk = INVALID_HANDLE_VALUE;
    if (!gLearnDir.empty()) {
        char hex[24]; snprintf(hex, sizeof hex, "%016llx", (unsigned long long)Fnv64(bytes.data(), bytes.size()));
        // ((v141 beta 6) a mesh with the tables or the crawl-space vaults kept, NAV_KEEP_TABLES, NAV_KEEP_LOW_VAULTS: a file of its own)
        cache = gLearnDir + "\\" + m->key + "_" + hex + (getenv("NAV_KEEP_TABLES") ? "_t" : "") + (getenv("NAV_KEEP_LOW_VAULTS") ? "_v" : "") + ".nav";
        if (MeshesRead(cache, *g, mm)) how = "read from the server's dolls' folder";
        else {
            // (one doll at a time builds it: the others wait for its file)
            lk = CreateFileA((gLearnDir + "\\" + m->key + ".navlock").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool locked = false;
            for (int k = 0; lk != INVALID_HANDLE_VALUE && k < 360 && !locked; ++k) {
                OVERLAPPED ov{};
                locked = LockFileEx(lk, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov) != 0;
                if (!locked) Sleep(250);
            }
            if (!locked && lk != INVALID_HANDLE_VALUE) { CloseHandle(lk); lk = INVALID_HANDLE_VALUE; }
            if (MeshesRead(cache, *g, mm)) how = "read from the server's dolls' folder (another doll built it)";
        }
    }
    if (!mm.nav) {
        auto nav = std::make_unique<NavMesh>(), wide = std::make_unique<NavMesh>();
        std::string log, log2;
        if (!nav->Build(*g, cfg, log)) {
            m->log = m->key + ": " + log; m->state = 3;
            if (lk != INVALID_HANDLE_VALUE) { OVERLAPPED ov{}; UnlockFileEx(lk, 0, 1, 0, &ov); CloseHandle(lk); }
            return;
        }
        NavConfig cw = cfg; cw.radius = 500.f;
        if (!wide->Build(*g, cw, log2)) wide.reset();
        mm.vaultsOut = VaultsSecondLook(*nav, mm.vaultLines); mm.wideOut = wide ? VaultsSecondLook(*wide, mm.vaultLines) : 0;
        mm.nav = std::move(nav); mm.wide = std::move(wide); mm.log = log;
        if (!cache.empty()) { MeshesWrite(cache, mm); how = "built, kept for the server's dolls"; }
    }
    if (lk != INVALID_HANDLE_VALUE) { OVERLAPPED ov{}; UnlockFileEx(lk, 0, 1, 0, &ov); CloseHandle(lk); }
    auto nav = std::move(mm.nav), wide = std::move(mm.wide);
    const std::string& log = mm.log;
    std::string vaultLines = mm.vaultLines;
    const int vaultsOut = mm.vaultsOut, wideOut = mm.wideOut;
    char b[512];
    snprintf(b, sizeof b, "%s: %zu faces, %zu spawns; %s; wide: %d polygons; %lu ms%s%s", m->key.c_str(), g->faces.size(), g->spawns.size(), log.c_str(),
             wide ? wide->Polys() : 0, GetTickCount() - t0, how.empty() ? "" : ", ", how.c_str());
    m->log = b;
    // the objectives' points of the map (goals, KEROTANs, the sneaking / rescue target), for the log
    static const char* kKind[] = {"?", "dm", "tdm1r", "tdm1b", "tdmr", "tdmb", "tcmr", "tcmb", "tcm_goal", "kerotan", "cmplus_goal", "sr_target", "camera", "obj_red", "obj_blue"};
    for (const Spawn& sp : g->spawns)
        if (sp.kind >= SP_TCM_GOAL && sp.kind != SP_CAMERA && sp.kind < SP_KIND_COUNT) {
            snprintf(b, sizeof b, " [%s %.0f %.0f %.0f]", kKind[sp.kind], sp.p[0], sp.p[1], sp.p[2]);
            m->log += b;
        }
    if (vaultsOut || wideOut) {                          // (lines apart: Frame logs them one by one)
        snprintf(b, sizeof b, "\nnavigation: %d vaults left out on a second look (the wide mesh: %d), walls whose top is over %.1f m above a floor at their foot",
                 vaultsOut, wideOut, cfg.climbFacesMax / 1000.f);
        m->log += b + vaultLines;
    }
    m->geom = std::move(g);
    m->nav = std::move(nav);
    m->wide = std::move(wide);
    m->state = 2;
}

// ---- what the doll sees ----
Me gMe;
static void See() {
    gMe.obj = ps2world::PlayerObject(0);
    if (!gMe.obj) { gMe.alive = false; gMe.life = 0; return; }
    for (int k = 0; k < 3; ++k) gMe.pos[k] = RF(gMe.obj + 0x110 + 4 * k);
    gMe.life = R16s(gMe.obj + 0xB64);
    gMe.stun = R16s(gMe.obj + 0xB68);
    for (int k = 0; k < 11; ++k) gMe.bits[k] = R32(gMe.obj + 0xB2C + 4 * k);
    // (v141 beta 3) a ghost (one life: RESCUE, SNEAKING, TEAM SNEAKING; the state word 10's 0x400000, v136's rescue audit:
    // a dead doll's ghost drifting with its life 0, then 200 a while) is not alive: no weapon, nothing to fight for
    gMe.ghost = (gMe.bits[10] & 0x400000) != 0;
    gMe.alive = gMe.life > 0 && !gMe.ghost;
    // (0x1000000 of the state word 7 is also set on a ladder's automatic moves: not the round's start there)
    gMe.frozen = (gMe.bits[7] & 0x1000000) != 0 && !(gMe.bits[2] & 0x40000);
}

// a chaff grenade out: its actor (NewChaffGrenade, n011a 0x4AA744) sets 0x10 of the game's status word
// 0x2C5248 every frame of its 24 s, on every machine of the game (v134, probe 31: 10 m from it as at its
// foot); the radio does not open meanwhile
bool Jammed() { return (R32(0x2C5248) & 0x10) != 0; }

// the other players: every player object of the game (the local one is gMe)
std::vector<Player> gPlayers;
int gMyId = -1, gMyTeam = 0xFF;
static int gMyIdKnown = -1;                     // the local player's id, once it has been in the world
static std::string gMyNameKnown;
static void AddPlayer(uint32_t w, const std::string& name, void*) {
    const bool local = name == "NewNetworkDummyPlayer";
    if (!local && name != "NewNetworkPlayer") return;
    Player p;
    p.obj = w; p.local = local;
    p.id = R8(w + 0xB60);
    for (int k = 0; k < 3; ++k) p.pos[k] = RF(w + 0x110 + 4 * k);
    p.life = R16s(w + 0xB64); p.stun = R16s(w + 0xB68);
    p.down = R8(w + 0xDF0) != 0;
    for (int k = 0; k < 11; ++k) p.bits[k] = R32(w + 0xB2C + 4 * k);
    p.ghost = (p.bits[10] & 0x400000) != 0;          // ((v141 beta 3) a ghost: ignored, as the dead)
    p.alive = p.life > 0 && !p.ghost;
    p.facing = FacingOf(w);
    // the local player's object is made before its slot is set (+0xB60 reads 0, the host's id): its id
    // is the one it had in the world
    if (local && !p.alive) p.id = gMyId;
    int team = 0xFF; std::string nm;
    if (p.id >= 0 && ps2world::PlayerInfo(p.id, &team, &nm)) { p.team = team; p.name = nm; }
    gPlayers.push_back(p);
}
// my id (v137, the audit's identity): the slot the game's own data gives the doll. An accountless doll (host/
// mgo1doll.sh: MGO1_DOLL_UID, MGO1_DOLL_NAME) is the slot holding its user id (its variable set +0xA0), else the
// slot with its name; then the id its local player has in the world (alive: its slot set); then the session's
// local player (the network's player table 0x28B5F4, 0x5C bytes an entry: +0 id, +2 state, +8 flags: 2 local,
// 1 the host). Not the name at 0x28C6D2 (LocalPlayerName): that is the slot block of id 1 (0x28C608 + 0xC8 + 2),
// so a doll that never spawned (a spectator) took the first joiner's slot and name (three dolls named PLAYER_DOLL1)
static const char* gMyIdBy = "unknown";
static uint32_t MyUid() { static const uint32_t u = getenv("MGO1_DOLL_UID") ? (uint32_t)strtoul(getenv("MGO1_DOLL_UID"), nullptr, 0) : 0; return u; }
static const std::string& MyLogin() { static const std::string n = getenv("MGO1_DOLL_NAME") ? getenv("MGO1_DOLL_NAME") : ""; return n; }
static int SlotOfUid(uint32_t uid) {
    for (int id = 1; id < 16 && uid; ++id) {
        uint8_t v[0xA4];
        uint32_t u = 0;
        if (ps2world::VariableSet(id + 1, v, sizeof v)) { memcpy(&u, v + 0xA0, 4); if (u == uid) return id; }
    }
    return -1;
}
static int SlotNamed(const std::string& name) {
    for (int id = 1; id < 16 && !name.empty(); ++id) {
        int t = 0xFF; std::string nm;
        if (ps2world::PlayerInfo(id, &t, &nm) && nm == name) return id;
    }
    return -1;
}
static int SessionLocalId() {
    const uint32_t count = R32(0x28B5F0);
    if (!count || count > 16) return -1;
    for (uint32_t k = 0; k < 16; ++k) {
        const uint32_t e = 0x28B5F4 + 0x5C * k, fl = R32(e + 8);
        const int id = R8(e);
        if (id > 0 && id < 16 && R16(e + 2) && (fl & 2) && !(fl & 1)) return id;
    }
    return -1;
}
static int WhoAmI(const char** by) {
    int s;
    if ((s = SlotOfUid(MyUid())) > 0) { *by = "uid"; return s; }
    if ((s = SlotNamed(MyLogin())) > 0) { *by = "name"; return s; }
    if (gMyIdKnown > 0) { *by = "world"; return gMyIdKnown; }
    if ((s = SessionLocalId()) > 0) { *by = "session"; return s; }
    *by = "unknown";
    return -1;
}
static void SeePlayers() {
    gMyId = WhoAmI(&gMyIdBy);                     // (the local object not alive takes this id: AddPlayer)
    gPlayers.clear();
    ps2world::ForEachActor(&AddPlayer, nullptr);
    int world = -1;
    for (const Player& p : gPlayers)
        if (p.local && p.alive && p.id > 0) world = gMyIdKnown = p.id;
    gMyId = WhoAmI(&gMyIdBy);
    {
        static int said = -2, saidWorld = -2;
        static const char* saidBy = "";
        if (gMyId != said || strcmp(gMyIdBy, saidBy)) {
            said = gMyId; saidBy = gMyIdBy;
            int t = 0xFF; std::string nm;
            if (gMyId >= 0) ps2world::PlayerInfo(gMyId, &t, &nm);
            Log("identity: id %d (%s), its name %s", gMyId, gMyIdBy, nm.empty() ? "-" : nm.c_str());
        }
        if (world > 0 && world != gMyId && world != saidWorld) {
            saidWorld = world;
            Log("identity: the local player's object says id %d, the doll is id %d (%s): kept", world, gMyId, gMyIdBy);
        }
    }
    // each one's move since the last frame (a jump of 1.5 m and more: a spawn, none)
    static float wasPos[16][3], wasVel[16][2]; static long long wasAt[16] = {};
    for (Player& p : gPlayers) {
        if (p.local || p.id < 0 || p.id >= 16) continue;
        float* v = wasVel[p.id];
        if (wasAt[p.id] == gFrame - 1 && Dist2D(wasPos[p.id], p.pos) < 1500.f) {
            v[0] += (p.pos[0] - wasPos[p.id][0] - v[0]) * 0.25f; v[1] += (p.pos[2] - wasPos[p.id][2] - v[1]) * 0.25f;
        } else if (wasAt[p.id] != gFrame) { v[0] = v[1] = 0.f; }
        memcpy(wasPos[p.id], p.pos, sizeof wasPos[p.id]); wasAt[p.id] = gFrame;
        p.vel[0] = v[0]; p.vel[1] = v[1];
    }
    gMyTeam = 0xFF;
    if (gMyId >= 0) {
        int t = 0xFF; std::string nm;
        if (ps2world::PlayerInfo(gMyId, &t, &nm)) { gMyTeam = t; if (!nm.empty()) gMyNameKnown = nm; }
    }
}
const Player* PlayerById(int id) {
    for (const Player& p : gPlayers) if (p.id == id && !p.local) return &p;
    return nullptr;
}
const Player* PlayerByName(const std::string& name) {
    for (const Player& p : gPlayers)
        if (!p.local && p.name.size() == name.size() && std::equal(p.name.begin(), p.name.end(), name.begin(), [](char a, char b) { return toupper((unsigned char)a) == toupper((unsigned char)b); }))
            return &p;
    return nullptr;
}
// an enemy: another player, alive, of the other team (0 against 1); in a rule without teams (Deathmatch)
// everyone else. A player whose team is not known yet is nobody's enemy: an ally is never aimed at.
int gRule = -1;
bool Enemy(const Player& p) {
    if (p.local || !p.alive || p.id == gMyId || p.team == 0xFE) return false;
    if (gRule == 0) return true;                // Deathmatch: all against all (everyone is in team 0 there)
    const bool mineKnown = gMyTeam == 0 || gMyTeam == 1, theirsKnown = p.team == 0 || p.team == 1;
    return mineKnown && theirsKnown && p.team != gMyTeam;
}
bool Ally(const Player& p) {
    if (p.local || p.id == gMyId || gRule == 0) return false;
    return (gMyTeam == 0 || gMyTeam == 1) && p.team == gMyTeam;
}
// every player's place, team and life in one line (the tests' paths: tests/bench/tools/trails.py)
static void TrackLog() {
    std::string s;
    char b[64];
    // (v150-host beta 21, cover) MGO1_DOLL_TRACK_MS=1 (benches): the line stamped with the machine's clock (ms, the host's
    // own cover lines' clock), each player's life after his fields, this game's own player marked ":me"
    static const bool ms = getenv("MGO1_DOLL_TRACK_MS") && !strcmp(getenv("MGO1_DOLL_TRACK_MS"), "1");
    for (const Player& p : gPlayers) {
        if (!p.obj) continue;
        if (ms) snprintf(b, sizeof b, " %d:%.0f:%.0f:%.0f:%d:%d:%d%s", p.id, p.pos[0], p.pos[1], p.pos[2], p.team, p.alive ? 1 : 0, p.life, p.local ? ":me" : "");
        else snprintf(b, sizeof b, " %d:%.0f:%.0f:%.0f:%d:%d", p.id, p.pos[0], p.pos[1], p.pos[2], p.team, p.alive ? 1 : 0);
        s += b;
    }
    if (ms) Log("track@%llu:%s", (unsigned long long)GetTickCount64(), s.c_str());
    else Log("track:%s", s.c_str());
}
// (v150-host beta 19, measures) MGO1_DOLL_WATCH=1: every player's work on this game (its own and the others' copies), each
// frame: its state words 0 to 2 when they change, its run every 6 frames (mm a frame); each actor made on this game (its
// name; the players' own aside). What a copy of another player does on this screen (docs/VIRTUAL_DOLL.md 20)
// ((v150-host beta 22, bookplay2) a player's book action: the action of id 0x2EADB2E in his list (0x30EE68 compares the low 24 bits: 0xEADB2E))
static uint32_t WatchBookAction(uint32_t n, int depth) {
    for (int guard = 0; n >= 0x100000 && n < 0x02000000 && guard < 200; ++guard, n = R32(n)) {
        if ((R32(n + 0x14) & 0xFFFFFF) == 0xEADB2E) return n;
        if (depth < 4) if (const uint32_t c = WatchBookAction(R32(n + 8), depth + 1)) return c;
    }
    return 0;
}
static void WatchLog() {
    static uint32_t was[16][3];
    static float at[16][3];
    static bool have[16];
    for (const Player& p : gPlayers) {
        if (!p.obj || p.id < 0 || p.id >= 16) continue;
        const int i = p.id;
        const float run = have[i] ? Dist2D(at[i], p.pos) : 0.f;
        if (!have[i] || was[i][0] != p.bits[0] || was[i][1] != p.bits[1] || was[i][2] != p.bits[2])
            Log("watch: p%d%s state %x %x %x at %.0f %.0f %.0f life %d", i, p.local ? " me" : "", p.bits[0], p.bits[1], p.bits[2], p.pos[0], p.pos[1], p.pos[2], p.life);
        if (gFrame % 6 == 0) {
            if (have[i] && run > 1.f) Log("watch: p%d%s runs %.0f mm a frame (state %x)", i, p.local ? " me" : "", run / 6.f, p.bits[0]);
            memcpy(at[i], p.pos, sizeof at[i]);
        }
        if (!have[i]) memcpy(at[i], p.pos, sizeof at[i]);
        have[i] = true; was[i][0] = p.bits[0]; was[i][1] = p.bits[1]; was[i][2] = p.bits[2];
        // (v150-host beta 22, bookplay) its book action on this game (US n003a 0x443718: a copy's reading too): +0xDE9 (reading),
        // +0xFB4 (the state record's byte), its state bits 0xDD, 0x126, 0x127, 0x153, 0x158, 0x159 (set while it reads, cleared
        // at its end), when they change
        static int bookWas[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
        static uint32_t motionWas[16];
        // ((v150-host beta 22, bookplay2) its class and its book action's motion (+0x30: 0xB2 the look, 0xB3 crouched over it,
        // 0xC003 to 0xC006 Reiko's and Rowdy's own; WatchBookAction)
        const uint32_t act = WatchBookAction(R32(p.obj + 0x44), 0);
        const uint32_t motion = act ? R32(act + 0x30) : 0;
        const int bk = (R8(p.obj + 0xDE9) ? 1 : 0) | (R8(p.obj + 0xFB4) & 0xF) << 1 | ((p.bits[6] >> 29) & 1) << 5 | ((p.bits[9] >> 6) & 3) << 6 |
                       ((p.bits[10] >> 19) & 1) << 8 | ((p.bits[10] >> 24) & 3) << 9;
        if ((bk != bookWas[i] && (bookWas[i] >= 0 || bk)) || ((bk & 1) && motion != motionWas[i]))
            Log("watch: p%d%s book: de9 %d fb4 %d, bits dd %d 126 %d 127 %d 153 %d 158 %d 159 %d (state %x; class %d, motion %x)", i, p.local ? " me" : "", bk & 1, (bk >> 1) & 0xF,
                (bk >> 5) & 1, (bk >> 6) & 1, (bk >> 7) & 1, (bk >> 8) & 1, (bk >> 9) & 1, (bk >> 10) & 1, p.bits[0], R8(p.obj + 0xB62), motion);
        bookWas[i] = bk; motionWas[i] = motion;
    }
    static std::map<uint32_t, std::string> last;
    std::map<uint32_t, std::string> now;
    ps2world::ForEachActor([](uint32_t w, const std::string& name, void* c) { (*(std::map<uint32_t, std::string>*)c)[w] = name; }, &now);
    std::string added;
    for (auto& a : now) {
        if (a.second == "NewNetworkPlayer" || a.second == "NewNetworkDummyPlayer") continue;
        auto it = last.find(a.first);
        if (it == last.end() || it->second != a.second) added += " " + a.second;
    }
    if (!added.empty() && !last.empty()) Log("watch: actors +%s", added.c_str());
    last.swap(now);
}
static void PlayersLog() {
    std::string s;
    char b[200];
    for (const Player& p : gPlayers) {
        snprintf(b, sizeof b, " [%d%s %s team %d type %d life %d at %.0f %.0f %.0f%s%s]", p.id, p.local ? " me" : "", p.name.c_str(), p.team,
                 p.obj ? R8(p.obj + 0xB62) : -1, p.life, p.pos[0], p.pos[1], p.pos[2], p.ghost ? " GHOST" : "", Enemy(p) ? " ENEMY" : Ally(p) ? " ALLY" : "");
        s += b;
    }
    Log("players:%s", s.c_str());
}

// ---- the room: the rule and map of the entry being played, its phase, the seconds left, its flags, the
// teams' round wins and tickets (the game network's variable set 0, doll_int.h). The rotation is a list of
// {map, rule} from +2; the entry played is +0x36 (n011a 0x178A50 reads the rule so) ----
Room gRoom;
static bool gHeadshotsLogged = false;
static void SeeRoom() {
    uint8_t v[0x4C];
    if (!ps2world::RoomVariables(v, sizeof v)) { gRoom = Room(); return; }
    gRoom.entry = v[0x36];
    const int e = gRoom.entry < 16 ? gRoom.entry : 0;
    gRoom.phase = v[0]; gRoom.map = v[2 + 2 * e]; gRoom.rule = v[3 + 2 * e];
    uint32_t left; memcpy(&left, v + 0x3C, 4); gRoom.secondsLeft = (int)left;
    memcpy(&gRoom.flags, v + 0x30, 4);
    for (int t = 0; t < 2; ++t) { gRoom.wins[t] = v[0x42 + 4 * t]; gRoom.tickets[t] = v[0x44 + 4 * t] | (v[0x45 + 4 * t] << 8); }
    // (v141) TEAM SNEAKING (5) plays as RESCUE (one life, the targets carried to the goals: doll_brain.cpp TsneView),
    // BASE CAPTURE (6) as TEAM DEATHMATCH with its bases (BcObjective)
    gRule = gRoom.rule <= 4 ? gRoom.rule : gRoom.rule == 5 ? 2 : gRoom.rule == 6 ? 1 : -1;
    static int phaseLogged = -1;                           // ((v141 beta 10) debug: the round's phase at each change, for the benches' reports)
    if (gDebug && gRoom.phase != phaseLogged) {
        Log("round: phase %d -> %d (%d s left, map %d, rule %d)", phaseLogged, gRoom.phase, gRoom.secondsLeft, gRoom.map, gRoom.rule);
        phaseLogged = gRoom.phase;
    }
    if (HeadshotsOnly() != gHeadshotsLogged) {
        gHeadshotsLogged = HeadshotsOnly();
        Log(gHeadshotsLogged ? "rules: headshots only (the game's name begins with HEAD SHOT): the head aimed at in the first person view, no body shot"
                             : "rules: the headshot rule is off");
    }
}
const char* RuleName(int r) { static const char* n[] = {"dm", "tdm", "rescue", "capture", "sneaking", "tsne", "bc"}; return r >= 0 && r <= 6 ? n[r] : "unknown"; }
// the game's headshot rule (ntmenu2 0x35F2E0: a game named "HEAD SHOT..." or "HEAD_SHOT..." gets the room's
// flag 0x800; n011a 0x3EC484: a kill that is not a headshot is not counted)
bool HeadshotsOnly() { return (gRoom.flags & 0x800) != 0; }
// ---- (v141 beta 6) who is in the game: the game network's session table (the players connected now) decides, as the
// dedicated host's status does (players_real, players_dolls). A slot's variable set keeps its name and team after its
// player left (the live room of October: the last real player gone, his name and team still there, the dolls played
// on alone, round after round; the presence bench: the departed player's name and team still in his slot's set when the
// game dropped him). Each entry: the dedicated host (the game's host flag, its team 0xFE), this doll, a doll (an accountless
// doll's user id, 0x7F000001 to 0x7F000040, a bot's virtual doll's, 0x7F000041 to 0x7F0000FF (docs/VIRTUAL_DOLL.md), or a name
// of the server's dolls), not known yet (below), else a real player ----
static const uint32_t kDollUidFirst = 0x7F000001, kDollUidLast = 0x7F0000FF;
bool IsDollUid(uint32_t uid) { return uid >= kDollUidFirst && uid <= kDollUidLast; }
static bool SameNameCi(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return false;
    return true;
}
// the session counted: its real players, dolls, dedicated hosts, and the entries not known yet: a player whose variable set is
// not there yet, or still names the slot's last player (the session entry's own name another one: 3 s at most, then the
// variable set decides), a host whose team is not known yet (the bench's first readings: the dedicated host counted as a real
// player for 10 to 70 frames, its team not 0xFE yet). ids: the entries' player ids (InSession)
struct PresenceCount { int real = 0, dolls = 0, hosts = 0, unknown = 0, loading = 0; long long teamAt = -1; uint32_t ids = 0; std::string who, detail; };
static long long gSeenAt[16];                                  // since when each player id has had a session entry
static uint32_t gSeenIds = 0;
// (v141 beta 13) since when each real player's team has been known (0 or 1: his arrival in the game), -1 not known
static long long gRealTeamAt[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
// MGO1_DOLL_PRESENCE_OLD=1: b7410f3's count (an entry not known yet counted as a real player, a host by its team 0xFE alone)
// and the variable sets read whatever the session (InSession always true)
static bool PresenceOld() { static const bool o = getenv("MGO1_DOLL_PRESENCE_OLD") && *getenv("MGO1_DOLL_PRESENCE_OLD") == '1'; return o; }
static bool PresenceNow(PresenceCount& c) {
    std::vector<ps2world::SessionEntry> es;
    if (!ps2world::SessionPlayers(es)) return false;
    const std::string me = MyName();
    for (const auto& e : es) {
        if (!((gSeenIds >> e.id) & 1)) gSeenAt[e.id] = gFrame;
        c.ids |= 1u << e.id;
        if ((e.flags & 2) || e.id == gMyId) continue;            // this doll
        int t = 0xFF; std::string nm;
        const bool vs = ps2world::PlayerInfo(e.id, &t, &nm) && !nm.empty();
        const bool clash = vs && !e.name.empty() && !SameNameCi(e.name, nm), young = gFrame - gSeenAt[e.id] < 180;
        const std::string shown = vs ? nm : !e.name.empty() ? e.name : "P" + std::to_string(e.id + 1);
        char kind;
        if ((e.flags & 1 || PresenceOld()) && t == 0xFE) kind = 'h';   // the dedicated host (Host Only)
        else if (!PresenceOld() && (!vs || ((e.flags & 1) && t != 0 && t != 1) || (clash && young))) kind = 'u';
        else if (!me.empty() && SameNameCi(nm, me)) kind = 's';
        else if (IsDollUid(ps2world::PlayerUserId(e.id)) || IsDollName(nm)) kind = 'd';
        else kind = 'r';
        if (kind == 'h') ++c.hosts; else if (kind == 'u') ++c.unknown; else if (kind == 'd') ++c.dolls;
        else if (kind == 'r') { ++c.real; if (!c.who.empty()) c.who += ", "; c.who += shown; }
        // (v141 beta 13) a real player's arrival: his team known since when; still loading (no team yet, or the entry not
        // known yet) for his first minute in the session
        if (kind == 'r' && (t == 0 || t == 1)) { if (gRealTeamAt[e.id] < 0) gRealTeamAt[e.id] = gFrame; c.teamAt = std::max(c.teamAt, gRealTeamAt[e.id]); }
        else gRealTeamAt[e.id] = -1;
        if (((kind == 'r' && t == 0xFF) || kind == 'u') && gFrame - gSeenAt[e.id] < 3600) ++c.loading;
        char b[160];
        snprintf(b, sizeof b, "%sP%d %03x%s %s team %d%s%s%s: %s", c.detail.empty() ? "" : "; ", e.id + 1, e.state, (e.flags & 1) ? "H" : "", shown.c_str(), t,
                 clash ? ", the session's name " : "", clash ? e.name.c_str() : "", e.name.empty() ? ", no session name" : "",
                 kind == 'h' ? "host" : kind == 'u' ? "not known yet" : kind == 's' ? "itself" : kind == 'd' ? "doll" : "real");
        c.detail += b;
    }
    gSeenIds = c.ids;
    for (int id = 0; id < 16; ++id) if (!((c.ids >> id) & 1)) gRealTeamAt[id] = -1;
    return true;
}
// beta 5's count, from the variable sets alone (a slot keeps its player's name and team after he left), told with each change
static int VarSetCount(std::string* who) {
    int n = 0;
    for (int id = 0; id < 16; ++id) {
        int t = 0xFF; std::string nm;
        if (id == gMyId || !ps2world::PlayerInfo(id, &t, &nm) || nm.empty() || t == 0xFE) continue;
        bool present = false;
        for (const Player& p : gPlayers) if (p.id == id) present = true;
        if ((!present && t == 0xFF) || IsDollName(nm) || IsDollUid(ps2world::PlayerUserId(id))) continue;
        ++n;
        if (!who->empty()) *who += ", ";
        *who += nm;
    }
    return n;
}
// the count the brain goes by: a change (the first reading too) taken once it has held 1 s; the session not readable (a stage's
// load): the last count. The presence (section 31 of the beta 5 handoff): WAITING_FOR_REAL with no real player (the dolls
// spectators: no fight, no objective, no move, still connected), ACTIVE with one; tests (MGO1_DOLL_PLAY_ALONE) and the probes
// always ACTIVE. Each change of it logged once with its reason
struct Presence { int real = -1, pending = -1; long long pendingSince = 0, since = 0; int dolls = 0, hosts = 0, unknown = 0, loading = 0; long long teamAt = -1; uint32_t ids = 0; bool idsKnown = false, waiting = true; std::string who, detail; };
static Presence gPresence;
static bool PlayAlone() { static const bool a = getenv("MGO1_DOLL_PLAY_ALONE") != nullptr; return a; }
static bool PresenceDebug() { static const bool d = getenv("MGO1_DOLL_PRESENCE_DEBUG") && atoi(getenv("MGO1_DOLL_PRESENCE_DEBUG")) > 0; return d || gDebug; }
static void PresenceFrame() {
    if (gFrame % 10) return;
    PresenceCount c;
    if (!PresenceNow(c)) return;
    gPresence.dolls = c.dolls; gPresence.hosts = c.hosts; gPresence.unknown = c.unknown; gPresence.ids = c.ids; gPresence.idsKnown = true;
    gPresence.loading = c.loading; gPresence.teamAt = c.teamAt;
    if (PresenceDebug() && c.detail != gPresence.detail) Log("presence: %s", c.detail.empty() ? "nobody else" : c.detail.c_str());
    gPresence.detail = c.detail;
    if (c.real == gPresence.real) { gPresence.pending = -1; gPresence.who = c.who; return; }
    if (c.real != gPresence.pending) { gPresence.pending = c.real; gPresence.pendingSince = gFrame; return; }
    if (gFrame - gPresence.pendingSince < 60) return;
    const int was = gPresence.real;
    gPresence.real = c.real; gPresence.pending = -1; gPresence.since = gFrame; gPresence.who = c.who;
    std::string vw;
    const int vn = VarSetCount(&vw);
    char tail[256];
    snprintf(tail, sizeof tail, "(session: %d doll%s, %d dedicated host%s, %d not known yet; the variable sets alone would count %d%s%s)", c.dolls,
             c.dolls == 1 ? "" : "s", c.hosts, c.hosts == 1 ? "" : "s", c.unknown, vn, vn ? ": " : "", vw.c_str());
    if (was < 0) Log("real players: %d%s%s %s", c.real, c.real ? ": " : "", c.who.c_str(), tail);
    else Log("real players: %d -> %d%s%s %s", was, c.real, c.real ? ": " : "", c.who.c_str(), tail);
    if (gProbe || PlayAlone()) {
        if (was > 0 && c.real == 0) Log("doll: no real player, playing on (%s)", gProbe ? "a probe" : "MGO1_DOLL_PLAY_ALONE");
        return;
    }
    const bool waiting = c.real == 0;
    if (waiting == gPresence.waiting) return;
    gPresence.waiting = waiting;
    if (waiting) Log("doll: entering spectator, no real player in the game (reason no-real-player%s)", gSet.team == "spectator" ? "; ordered a spectator too" : "");
    else Log("doll: real player joined, leaving spectator%s", gSet.team == "spectator" ? " (ordered a spectator: it stays one)" : "");
}
int RealPlayers() { return gPresence.real < 0 ? 0 : gPresence.real; }
int RealLoading() { return gPresence.loading; }
long long RealTeamKnownAt() { return gPresence.teamAt; }
int DollsInSession() { return gPresence.dolls + 1; }
bool PresenceWaiting() { return gPresence.waiting && !gProbe && !PlayAlone(); }
bool InSession(int id) { return PresenceOld() || !gPresence.idsKnown || (id >= 0 && id < 16 && ((gPresence.ids >> id) & 1)); }
const char* SpectatorReason() {
    if (gSet.team == "spectator") return "ordered";
    if (PresenceWaiting()) return "no-real-player";
    return "";
}
// its player name: its slot's (above), else the accountless doll's own (DOLL_<n>), else the last one its slot had
std::string MyName() {
    int t = 0xFF; std::string nm;
    if (gMyId >= 0 && ps2world::PlayerInfo(gMyId, &t, &nm) && !nm.empty()) return nm;
    return !MyLogin().empty() ? MyLogin() : gMyNameKnown;
}

// ---- the camera: the first view's matrix (DG's chanl), the stick's directions from it ----
Camera gCam;
static void ReadCamera() {
    float m[16];
    if (!ps2world::ViewMatrix(m)) { gCam.ok = false; return; }
    // the eye's forward axis (row 2), flattened; right: row 0 flattened
    float fx = m[8], fz = m[10], n = std::sqrt(fx * fx + fz * fz);
    if (n < 1e-3f) { gCam.ok = false; return; }
    gCam.fwd[0] = fx / n; gCam.fwd[1] = fz / n;
    float rx = m[0], rz = m[2]; n = std::sqrt(rx * rx + rz * rz);
    if (n < 1e-3f) { gCam.ok = false; return; }
    gCam.right[0] = rx / n; gCam.right[1] = rz / n;
    gCam.ok = true;
}

// ---- geometry ----
float FacingOf(uint32_t obj) { return obj ? std::atan2(RF(obj + 0x230), RF(obj + 0x238)) : 0.f; }
float Facing() { return FacingOf(gMe.obj); }
float AngleTo(const float* from, const float* to) { return std::atan2(to[0] - from[0], to[2] - from[2]); }
float Wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
float Dist2D(const float* a, const float* b) { const float dx = a[0] - b[0], dz = a[2] - b[2]; return std::sqrt(dx * dx + dz * dz); }
bool LineClear(const float* a, const float* b) { return MapReady() && gMap->nav->Clear(a, b); }

// ---- the stick: the left stick for a world direction (x, z) at a speed 0..1. The camera gives the
// stick's axes; what the player really does corrects them: each frame of a clear run, the angle between
// the direction asked and the one walked goes into a slow average added to the next ones ----
struct SteerLearn { float offset = 0.f; float lastPos[3] = {0, 0, 0}; float asked = 0.f; bool asking = false; int samples = 0; float askedPrev = 0.f; int steady = 0; };
static SteerLearn gSL;
void Steer(float dx, float dz, float speed) {
    const float n = std::sqrt(dx * dx + dz * dz);
    if (n < 1e-3f || speed <= 0) { gPad.sticks[2] = gPad.sticks[3] = 0x80; gSL.asking = false; return; }
    const float want = std::atan2(dx, dz);
    const float a = want - gSL.offset;          // the direction to push for, corrected by what was learnt
    dx = std::sin(a); dz = std::cos(a);
    float sx = dx * gCam.right[0] + dz * gCam.right[1];
    float sy = dx * gCam.fwd[0] + dz * gCam.fwd[1];
    // the game takes the stick's larger axis for its tilt (v134, probe 31: pushed full at 45 degrees, 0.71
    // on each axis, the player walked, 0.75 ran): that axis at the speed asked, the other in proportion
    const float big = std::max(std::fabs(sx), std::fabs(sy));
    if (big > 1e-3f) { sx /= big; sy /= big; }
    auto axis = [&](float v, bool invert) {
        v = std::max(-1.f, std::min(1.f, v * speed));
        if (invert) v = -v;
        return (uint8_t)(v >= 0 ? 0x80 + (int)(127 * v) : 0x80 - (int)(128 * -v));
    };
    gPad.sticks[2] = axis(sx, false);
    gPad.sticks[3] = axis(sy, true);            // up is toward 0
    gSL.asked = want; gSL.asking = speed > 0.9f;
}
// after the frame: what the push did (called with the new position, before the next Steer). Learned on a steady
// straight run only (v137): during a turn the walk lags the direction asked, and that lag taken for the camera's
// offset (any error up to 69 degrees at 0.05 a frame) made the next straight runs bend (arcs, spirals at a goal)
static void SteerObserve() {
    const float mx = gMe.pos[0] - gSL.lastPos[0], mz = gMe.pos[2] - gSL.lastPos[2];
    memcpy(gSL.lastPos, gMe.pos, sizeof gSL.lastPos);
    const bool same = gSL.asking && std::fabs(Wrap(gSL.asked - gSL.askedPrev)) < 0.035f;
    gSL.steady = same ? gSL.steady + 1 : 0;
    gSL.askedPrev = gSL.asked;
    const bool asking = gSL.asking;
    gSL.asking = false;                          // (a frame with no Steer: nothing asked)
    if (!asking || gSL.steady < 12) return;      // (the same direction asked 12 frames: the turn is over)
    const float m = std::sqrt(mx * mx + mz * mz);
    if (m < 45.f || m > 150.f) return;          // not a clear run (starting, a wall, a teleport by a respawn)
    const float err = Wrap(std::atan2(mx, mz) - gSL.asked);
    if (std::fabs(err) > 0.4f) return;          // a turn still, a wall's slide
    gSL.offset = Wrap(gSL.offset + 0.03f * err);
    gSL.offset = std::max(-0.35f, std::min(0.35f, gSL.offset));   // (a camera's skew, not a turn: 20 degrees at most)
    ++gSL.samples;
}

// ---- a life's frames (v137): its spawn (alive again), its deploy (the brain's first frame after the menu's: the
// respawn's wait or the round's start over) ----
struct LifeRun { long long spawnAt = -1, deployAt = -1; bool deployLogged = false; };
static LifeRun gLife;
long long LifeFrames() { return gLife.deployAt >= 0 ? gFrame - gLife.deployAt : -1; }

// ---- following a path ----
struct Follow {
    std::vector<Waypoint> path;
    size_t next = 0;
    float goal[3] = {0, 0, 0};
    float nearFor[3] = {0, 0, 0};                    // PlanNear: the point the path goes near (none: nearHave false)
    float asked[3] = {0, 0, 0};                      // (v141 beta 3) the point the path was asked for (its goal: that point on the mesh)
    bool nearHave = false;
    bool active = false;
    long long since = 0, lastProgress = 0;
    float best = 1e30f;
    int stuck = 0, replans = 0, walls = 0;
    long long sideUntil = 0; float side = 1.f;
    long long actionUntil = 0; int actions = 0;      // the action button against an obstacle (a climb)
    long long viewUntil = 0; int viewTries = 0;      // a view state not asked for: R1, then Cross, tapped, then R1 held (stuck)
    uint32_t viewState = 0;
    float actionFrom[3] = {0, 0, 0}; uint32_t actionBits = 0, actionBits0 = 0;
    float lastPos[3] = {0, 0, 0};                    // a respawn (a jump of metres): the path is planned again
    long long upSince = -1;                          // low where the way is walked standing: getting up since
    long long pivotUntil = -100, pivotAt = -100;     // (v141 beta 3) a sharp turn on the run: the stick let go until, last at
};
static Follow gF;
NavStats gNavStats;
bool OnLadder() { return (gMe.bits[2] & 0x40000) != 0; }
// (v141 beta 6) the careful press at a face (a ledge, a block of stairs, a vault's obstacle: PressFrame): the line (its origin,
// its way), the face along it, the launch's distance from it, the presses and what came of them
struct PressRun {
    float o[3] = {0, 0, 0}, u[2] = {0, 1}, face = 0.f, soupFace = 0.f, launch = 900.f, pushFrom = 0.f, pressDf = 0.f;
    int ph = 0, presses = 0, flats = 0, nothings = 0, pushes = 0, turns = 0, backs = 0, held = 0, stillFor = 0, pushLen = 2, pushNow = 2;
    long long phAt = 0, start = 0, pressAt = -1, pushAt = -1, backAt = -1, waitFrom = 0;
    bool felt = false, noBack = false;
    const char* tag = "climb";
    int mv = 0; float relDf = 0.f, runOn = 100.f;                   // (the move in: 1 a walk, 2 a pulse, 3 / 4 their stop; the walk's run on)
    float topH = 1000.f;                                           // (the top over the floor: its presses' distances)
    int flatRow = 0;                                               // (pressed flat while moved in, no press since)
};
struct LadderRun { const Ladder* l = nullptr; size_t seg = 0; bool down = false; int phase = 0, tries = 0; long long at = 0, stillSince = -1, offSince = -1, busySince = -1;
                   long long start = 0, onAt = -1, waitSince = -1; int life0 = 0; bool hurt = false, crowded = false, back = false, committed = false; float look = 0.f;
                   bool waitOff = false; };   // (v137: its log, its causes; (v141 beta 6) waitOff: still on it at this end, getting off)
static LadderRun gLR;                                // the ladder of the path's segment being taken (FollowPath)
struct CrossRun { size_t seg = 0; int phase = 0, tries = 0; long long at = 0, rollAt = -1, bestAt = 0; bool active = false, roll = false; float runup[3] = {0, 0, 0}, best = -1e30f, runupLen = 1500.f;
                  float top = -1.f; int pressRuns = 0; bool prOn = false; PressRun pr; };   // ((v141 beta 6) the vault's press runs)
static CrossRun gCR;                                 // the crossing of the path's segment being taken (a vault, a roll)

// ---- the map knowledge learned (v137, MGO1_DOLL_LEARN: a folder shared by the server's dolls): a text file a
// map page (<page>.txt, then <page>.old.txt once rotated), a fact a line, appended by any doll, read when the
// map's mesh is ready (LearnApply). A line: "v1 <UTC date> f<frame> d<doll> <fact> #<check>"; the facts (mm):
// "link <vault|roll|climb|drop> a b", "trap a b" (the wall's ends), "escape p <degrees>" (p on the pocket) ----
// (gLearnDir: declared with the map's build, which keeps its meshes there too)
static const long long kLearnMaxAge = 7 * 86400;     // seconds: older facts ignored
static const long long kLearnCap = 64 * 1024;        // bytes: the file rotated past it
static std::set<std::string> gLearnKnown;            // this map's facts read or written (each once)
static long long FileTimeSeconds(const FILETIME& ft) {   // Unix seconds
    const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)(t / 10000000ULL) - 11644473600LL;
}
static unsigned LearnCheck(const char* s, size_t n) {   // FNV-1a folded to 16 bits
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return (h >> 16) ^ (h & 0xFFFF);
}
static std::string LearnPath(const std::string& page, bool old) { return gLearnDir + "\\" + page + (old ? ".old.txt" : ".txt"); }
static HANDLE LearnOpenAppend(const std::string& path) {
    return CreateFileA(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}
extern long long gHangOverAt;
// (v141 beta 4) the lines waiting for the lock of their map's file (another doll writing): written at the next try, a
// second later; never a wait on the frame (it slept up to 40 times 50 ms)
static std::vector<std::pair<std::string, std::string>> gLearnPend;   // map key, line
static long long gLearnTryAt = 0;
static void LearnFlush() {
    if (gLearnPend.empty() || gLearnDir.empty()) return;
    gLearnTryAt = gFrame;
    std::vector<std::pair<std::string, std::string>> later;
    for (size_t i = 0; i < gLearnPend.size();) {
        const std::string key = gLearnPend[i].first;
        std::string lines;
        size_t j = i;
        for (; j < gLearnPend.size() && gLearnPend[j].first == key; ++j) lines += gLearnPend[j].second;
        // (the dolls of a server write the same files: the append and the rotation under a lock of <page>.lock)
        HANDLE lk = CreateFileA((gLearnDir + "\\" + key + ".lock").c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool locked = false;
        if (lk != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov{};
            locked = LockFileEx(lk, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov) != 0;
            if (!locked) { CloseHandle(lk); for (size_t k = i; k < j; ++k) later.push_back(gLearnPend[k]); i = j; continue; }
        }
        auto unlock = [&]() {
            if (lk == INVALID_HANDLE_VALUE) return;
            if (locked) { OVERLAPPED ov{}; UnlockFileEx(lk, 0, 1, 0, &ov); }
            CloseHandle(lk);
        };
        const std::string path = LearnPath(key, false);
        HANDLE h = LearnOpenAppend(path);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            if (GetFileSizeEx(h, &size) && size.QuadPart + (long long)lines.size() > kLearnCap) {
                CloseHandle(h);
                MoveFileExA(path.c_str(), LearnPath(key, true).c_str(), MOVEFILE_REPLACE_EXISTING);
                h = LearnOpenAppend(path);
            }
            if (h != INVALID_HANDLE_VALUE) {
                DWORD done = 0;
                WriteFile(h, lines.data(), (DWORD)lines.size(), &done, nullptr);   // (whole lines, append only)
                CloseHandle(h);
            }
        }
        unlock();
        i = j;
    }
    if (later.size() > 64) later.erase(later.begin(), later.end() - 64);
    gLearnPend.swap(later);
}
static void LearnWrite(const std::string& fact) {
    if (gLearnDir.empty() || !gMap || gProbe || gFrame - gHangOverAt < 300 || !gLearnKnown.insert(fact).second) return;
    FILETIME ft; SYSTEMTIME st;
    GetSystemTimeAsFileTime(&ft); FileTimeToSystemTime(&ft, &st);
    char line[256];
    int n = snprintf(line, sizeof line, "v1 %04u-%02u-%02uT%02u:%02u:%02uZ f%lld d%d %s", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                     gFrame, gSet.number, fact.c_str());
    if (n <= 0 || n > (int)sizeof line - 10) return;
    n += snprintf(line + n, sizeof line - n, " #%04x\n", LearnCheck(line, (size_t)n));
    gLearnPend.push_back({gMap->key, std::string(line, (size_t)n)});
    LearnFlush();
}
static const char* LinkName(uint8_t a) { return a == AREA_VAULT ? "vault" : a == AREA_ROLL ? "roll" : a == AREA_CLIMB ? "climb" : a == AREA_DROP ? "drop" : nullptr; }
static std::string FactLink(uint8_t area, const float* a, const float* b) {
    char f[160];
    snprintf(f, sizeof f, "link %s %.0f %.0f %.0f %.0f %.0f %.0f", LinkName(area) ? LinkName(area) : "vault", a[0], a[1], a[2], b[0], b[1], b[2]);
    return f;
}
static std::string FactTrap(const float* a, const float* b) {
    char f[160];
    snprintf(f, sizeof f, "trap %.0f %.0f %.0f %.0f %.0f %.0f", a[0], a[1], a[2], b[0], b[1], b[2]);
    return f;
}
static std::string FactEscape(const float* p, float angle) {
    int deg = (int)std::lround(angle * 57.29578f) % 360;
    if (deg < 0) deg += 360;
    char f[96];
    snprintf(f, sizeof f, "escape %.0f %.0f %.0f %d", p[0], p[1], p[2], deg);
    return f;
}

// where the doll stands on the mesh: its position is its hips' (800 above the ground standing, 300 low)
bool Ground(float* out, uint8_t* area) {
    if (!MapReady()) return false;
    const float feet[3] = {gMe.pos[0], gMe.pos[1] - 700.f, gMe.pos[2]};
    return gMap->nav->Nearest(feet, 500.f, out, area);
}
bool RandomReachable(float radius, float* out) {
    float g[3];
    if (!Ground(g)) return false;
    // (v141 beta 4) a few picks over the whole mesh first, kept on the doll's own part within the radius: the search
    // around a circle goes through every polygon within it (0.1 to 0.25 ms each; the sniper spots asked 52 a frame)
    const int part = gMap->nav->ComponentAt(g);
    if (part >= 0)
        for (int k = 0; k < 8; ++k) {
            float q[3];
            if (gMap->nav->RandomPoint(nullptr, 0.f, q, Rnd) && Dist2D(q, g) <= radius && gMap->nav->ComponentAt(q) == part) { memcpy(out, q, sizeof q); return true; }
        }
    return gMap->nav->RandomPoint(g, radius, out, Rnd);
}
// a point of the map onto the mesh: as it is, else on the ground under it (a goal's marker floats above
// its floor), else the nearest polygon farther around
static bool SnapToMesh(const float* p, float* out) {
    if (!MapReady()) return false;
    if (gMap->nav->Nearest(p, 400.f, out)) return true;
    const float above[3] = {p[0], p[1] + 300.f, p[2]};
    const float gy = gMap->nav->GroundBelow(above, 6000.f);
    if (gy > -1e29f) { const float q[3] = {p[0], gy, p[2]}; if (gMap->nav->Nearest(q, 700.f, out)) return true; }
    return gMap->nav->Nearest(p, 2500.f, out);
}
// the other points a goal can stand for (v133's audit: Ghost Factory's sneaking goal, a marker 2 m above
// its floor, snapped to a crate's top 3 m away, out of every path, and Snake stood with the item): the
// floor under it, then the mesh nearest to it farther around. Their count
static int SnapOthers(const float* p, float out[2][3]) {
    if (!MapReady()) return 0;
    int n = 0;
    const float above[3] = {p[0], p[1] + 300.f, p[2]};
    const float gy = gMap->nav->GroundBelow(above, 6000.f);
    if (gy > -1e29f) { const float q[3] = {p[0], gy, p[2]}; if (gMap->nav->Nearest(q, 700.f, out[n])) ++n; }
    if (gMap->nav->Nearest(p, 1500.f, out[n]) && (n == 0 || Dist2D(out[0], out[n]) > 300.f || std::fabs(out[0][1] - out[n][1]) > 300.f)) ++n;
    return n;
}
// the mesh's traps (v133, the audit: a creek's bank on Lost Forest, walkable on the mesh and in the
// .geom, where the game's player is stopped as by a wall along 7 m: something the .geom does not hold):
// a leg given up (flattened against the same wall three times, or its path planned again three times)
// leaves a wall 3 m wide across its way, 0.5 m ahead of where the doll stood, 10 minutes; a path with a
// leg across it is refused, and a way around looked for (PlanTo: by a point of the mesh away from it)
struct MeshTrap { float a[3], b[3]; long long at; };
static std::vector<MeshTrap> gMeshTraps;
static bool TrapNear(const float* c) {               // a trap's middle within 0.7 m of c
    for (const MeshTrap& t : gMeshTraps) {
        const float m[3] = {(t.a[0] + t.b[0]) * 0.5f, t.a[1], (t.a[2] + t.b[2]) * 0.5f};
        if (Dist2D(m, c) < 700.f && std::fabs(m[1] - c[1]) < 1500.f) return true;
    }
    return false;
}
static void AddMeshTrap(const float* from, const float* to) {
    if (Dist2D(from, to) < 500.f) return;
    const float dir = AngleTo(from, to), ux = std::sin(dir), uz = std::cos(dir);
    const float c[3] = {from[0] + ux * 500.f, from[1], from[2] + uz * 500.f};
    if (TrapNear(c)) return;
    MeshTrap t;
    t.a[0] = c[0] - uz * 1500.f; t.a[1] = c[1]; t.a[2] = c[2] + ux * 1500.f;
    t.b[0] = c[0] + uz * 1500.f; t.b[1] = c[1]; t.b[2] = c[2] - ux * 1500.f;
    t.at = gFrame;
    gMeshTraps.push_back(t);
    Log("path: a trap of the mesh at %.0f %.0f %.0f toward %.0f %.0f %.0f: a wall across that way 10 minutes", from[0], from[1], from[2], to[0], to[1], to[2]);
    for (const Player& p : gPlayers)                     // (a player in its way, not the map: nothing learned)
        if (!p.local && p.alive && Dist2D(p.pos, from) < 2500.f && std::fabs(p.pos[1] - from[1]) < 2000.f) return;
    LearnWrite(FactTrap(t.a, t.b));
}
// 2D: the segments p0-p1 and q0-q1 cross
static bool Crosses(const float* p0, const float* p1, const float* q0, const float* q1) {
    auto side = [](const float* a, const float* b, const float* c) { return (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]); };
    const float d1 = side(q0, q1, p0), d2 = side(q0, q1, p1), d3 = side(p0, p1, q0), d4 = side(p0, p1, q1);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}
bool PathTrapped(const std::vector<Waypoint>& path) {
    for (auto it = gMeshTraps.begin(); it != gMeshTraps.end();) { if (gFrame - it->at > 36000) it = gMeshTraps.erase(it); else ++it; }
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        if (path[i].link) continue;                                // (a crossing, a ladder: their own checks)
        const float* a = path[i].p; const float* b = path[i + 1].p;
        for (const MeshTrap& t : gMeshTraps)
            if (std::fabs(t.a[1] - std::min(a[1], b[1])) < 2500.f && Crosses(a, b, t.a, t.b)) return true;
    }
    return false;
}
// a path refused by a trap: around it, by a point of the mesh 5 to 15 m away (12 tries, the shortest)
static bool PathAround(const NavMesh& nav, const float* from, const float* goal, std::vector<Waypoint>& out, float* len) {
    if (gMeshTraps.empty()) return false;
    bool found = false; float best = 1e30f;
    for (int i = 0; i < 12; ++i) {
        float c[3];
        if (!nav.RandomPoint(from, 15000.f, c, Rnd) || Dist2D(c, from) < 5000.f) continue;
        std::vector<Waypoint> p1, p2; float l1 = 0, l2 = 0;
        if (!nav.Path(from, c, p1, &l1) || p1.size() < 2 || PathTrapped(p1)) continue;
        if (!nav.Path(c, goal, p2, &l2) || p2.size() < 2 || PathTrapped(p2) || l1 + l2 >= best) continue;
        best = l1 + l2;
        out = std::move(p1);
        out.back() = p2[0];                                        // (the point between: its own link on)
        out.insert(out.end(), p2.begin() + 1, p2.end());
        found = true;
    }
    if (found && len) *len = best;
    return found;
}
static bool PlanWay(const float* goalIn, std::vector<Waypoint>& p, float& len, float* goal, bool around = true) {
    if (!MapReady()) return false;
    float g[3];
    if (!Ground(g)) return false;
    if (!SnapToMesh(goalIn, goal)) memcpy(goal, goalIn, sizeof(float) * 3);
    // the wide mesh first (off the walls), the player's own where it has no way (a narrow passage); a path
    // across a trap of the mesh refused: around it
    bool wideOk = gMap->wide && gMap->wide->Path(g, goal, p, &len) && p.size() >= 2 && !PathTrapped(p);
    if (!wideOk && (!gMap->nav->Path(g, goal, p, &len) || p.size() < 2 || PathTrapped(p))) {
        const bool trapped = p.size() >= 2 && PathTrapped(p);
        p.clear();
        float others[2][3];
        const int n = trapped ? 0 : SnapOthers(goalIn, others);
        bool found = false;
        for (int i = 0; i < n && !found; ++i)
            if (gMap->nav->Path(g, others[i], p, &len) && p.size() >= 2 && !PathTrapped(p)) { memcpy(goal, others[i], sizeof(float) * 3); found = true; }
            else p.clear();
        if (!found) {
            if (!trapped || !around || !PathAround(*gMap->nav, g, goal, p, &len)) return false;
            static long long logged = -1000;                          // (a guard's tries around the item: one line a second)
            if (gDebug && gFrame - logged >= 60) { logged = gFrame; Log("path: around a trap of the mesh to %.0f %.0f %.0f (%.0f m)", goal[0], goal[1], goal[2], len / 1000.f); }
        }
    }
    return true;
}
static void SetWay(std::vector<Waypoint>&& p, const float* goal, const float* asked = nullptr) {
    const bool same = gF.active && Dist2D(gF.goal, goal) < 1.f;
    gF.path = std::move(p); gF.next = 1; gF.active = true; gF.since = gFrame; gF.lastProgress = gFrame; gF.best = 1e30f; gF.walls = 0; gF.upSince = -1;
    gLR = LadderRun();
    if (!same) { gF.replans = 0; gF.actions = 0; gF.viewTries = 0; ++gNavStats.goals; }
    memcpy(gF.goal, goal, sizeof gF.goal);
    memcpy(gF.asked, asked ? asked : goal, sizeof gF.asked);
    memcpy(gF.lastPos, gMe.pos, sizeof gF.lastPos);
    gF.nearHave = false;
}
bool PlanTo(const float* goalIn) {
    std::vector<Waypoint> p; float len = 0, goal[3];
    if (!PlanWay(goalIn, p, len, goal)) return false;
    SetWay(std::move(p), goal, goalIn);
    return true;
}
// ---- the doll's own ways: its way among the map's (the shortest or another), a lane beside the way's line, room
// kept from an ally ahead ----
static float gLane = 0.f, gLaneWant = 0.f; static long long gLaneAt = -100000, gLaneEvery = 600;
static float gLaneNow = 0.f;                         // (v137) the offset aside applied (eased toward the one wanted)
static int gAheadId = -1; static float gAheadSide = 1.f; static long long gAheadAt = -100000;   // (v137) the ally ahead, the side kept off him
static int gFlankSide = 1; static float gHabitAngle = 0.f;
struct FlankTrip {
    float goal[3] = {0, 0, 0}, via[3] = {0, 0, 0}, viaFor[3] = {0, 0, 0}; long long at = -100000; bool haveVia = false, habit = false;
    std::vector<unsigned> avoid;                     // (a way of the map's: the polygons it keeps out of, on the wide mesh)
};
static FlankTrip gTrip;
static float gKeepOff[3]; static long long gKeepOffAt = -100000;
void NewWays() {
    gFlankSide = Rnd() % 2 ? 1 : -1; gHabitAngle = (Rnd01() - 0.5f) * 1.2f;
    gLaneAt = -100000; gTrip = FlankTrip(); gLaneNow = 0.f; gAheadId = -1;
}
int HabitSide() { return gFlankSide; }
bool OldWays() { return gOldWays; }
bool OldFight() { return gOldFight; }
float HabitAngle() { return Wrap(std::max(0, gMyId) * 2.0944f + gHabitAngle); }   // (a third of the turn by its number: allies apart)
void KeepOff(const float* p) { memcpy(gKeepOff, p, sizeof gKeepOff); gKeepOffAt = gFrame; }
static float WayLength(const std::vector<Waypoint>& p) {
    float l = 0;
    for (size_t i = 1; i < p.size(); ++i) l += std::sqrt((p[i].p[0] - p[i - 1].p[0]) * (p[i].p[0] - p[i - 1].p[0]) + (p[i].p[2] - p[i - 1].p[2]) * (p[i].p[2] - p[i - 1].p[2]));
    return l;
}
// the point halfway along a way
static bool WayMiddle(const std::vector<Waypoint>& w, float* out) {
    float half = WayLength(w) * 0.5f;
    for (size_t i = 1; i < w.size(); ++i) {
        const float* a = w[i - 1].p; const float* b = w[i].p;
        const float l = std::hypot(b[0] - a[0], b[2] - a[2]);
        if (l >= half && l > 1.f) { const float t = half / l; for (int k = 0; k < 3; ++k) out[k] = a[k] + (b[k] - a[k]) * t; return true; }
        half -= l;
    }
    return false;
}
// the way to the goal through via (no way around a trap for its legs); the second leg's length in viaLeft
static bool WayVia(const float* via, const float* goal, std::vector<Waypoint>& out, float* viaLeft) {
    std::vector<Waypoint> a, b; float la = 0, lb = 0, v2[3];
    if (!PlanWay(via, a, la, v2, false) || a.size() < 2) return false;
    const NavMesh& nav = gMap->wide ? *gMap->wide : *gMap->nav;
    if (!(nav.Path(v2, goal, b, &lb) && b.size() >= 2) && !(gMap->nav->Path(v2, goal, b, &lb) && b.size() >= 2)) return false;
    if (PathTrapped(b)) return false;
    if (viaLeft) *viaLeft = WayLength(b);
    out = std::move(a);
    out.back() = b[0];                                              // (the point between: its own link on)
    out.insert(out.end(), b.begin() + 1, b.end());
    return true;
}
// the distance from p to a way (on the ground)
static float DistToWay(const float* p, const std::vector<Waypoint>& w) {
    float best = 1e30f;
    for (size_t i = 0; i + 1 < w.size(); ++i) {
        const float* a = w[i].p; const float* b = w[i + 1].p;
        const float abx = b[0] - a[0], abz = b[2] - a[2], l2 = abx * abx + abz * abz;
        const float t = l2 > 1.f ? std::max(0.f, std::min(1.f, ((p[0] - a[0]) * abx + (p[2] - a[2]) * abz) / l2)) : 0.f;
        best = std::min(best, std::hypot(a[0] + abx * t - p[0], a[2] + abz * t - p[2]));
    }
    return best;
}
static int LinkCount(const std::vector<Waypoint>& w) { int n = 0; for (const Waypoint& p : w) n += p.link; return n; }
// a flank's point: 40 to 60 % along the straight line, 15 to 45 % of the distance aside (4 to 25 m), on the
// standing floor about its height, 3 m off the shortest way at least; the way through it maxK times the shortest
// at most, with no more links (ladders, climbs, crossings) than it (the least aside first)
static bool FlankVia(const float* goal, int side, const std::vector<Waypoint>& shortest, float shortLen, float maxK, float* viaOut, std::vector<Waypoint>& out) {
    float g[3];
    if (!Ground(g)) return false;
    const float d = Dist2D(g, goal);
    if (d < 1.f) return false;
    const float ux = (goal[0] - g[0]) / d, uz = (goal[2] - g[2]) / d;
    static const float kAside[4] = {0.15f, 0.25f, 0.35f, 0.45f};
    for (int k = 0; k < 4; ++k) {
        const float along = 0.4f + 0.2f * Rnd01(), aside = std::max(4000.f, std::min(25000.f, d * kAside[k]));
        const float m[3] = {g[0] + ux * d * along + uz * side * aside, (g[1] + goal[1]) * 0.5f, g[2] + uz * d * along - ux * side * aside};
        float on[3], via[3];
        if (!gMap->nav->Nearest(m, 5000.f, on) || !gMap->nav->RandomPoint(on, 3000.f, via, Rnd) || std::fabs(via[1] - m[1]) > 2000.f ||
            gMap->nav->AreaAt(via) != AREA_STAND || DistToWay(via, shortest) < 3000.f) continue;
        std::vector<Waypoint> w;
        if (!WayVia(via, goal, w, nullptr) || WayLength(w) > shortLen * maxK || LinkCount(w) > LinkCount(shortest)) continue;
        memcpy(viaOut, via, sizeof(float) * 3);
        out = std::move(w);
        return true;
    }
    return false;
}
// the distance from p to the middle of a way (its points 8 m and more from both ends, every 2 m): where the map's
// ways part
static float DistToMiddle(const float* p, const std::vector<Waypoint>& w) {
    if (w.size() < 2) return 1e30f;
    const float* s = w.front().p; const float* e = w.back().p;
    float best = 1e30f;
    for (size_t i = 1; i < w.size(); ++i) {
        const float* a = w[i - 1].p; const float* b = w[i].p;
        const int n = std::max(1, (int)(std::hypot(b[0] - a[0], b[2] - a[2]) / 2000.f));
        for (int k = 0; k <= n; ++k) {
            const float q[3] = {a[0] + (b[0] - a[0]) * k / n, a[1], a[2] + (b[2] - a[2]) * k / n};
            if (Dist2D(q, s) < 8000.f || Dist2D(q, e) < 8000.f) continue;
            best = std::min(best, Dist2D(q, p));
        }
    }
    return best;
}
// the map's other ways to the goal (the wide mesh, else the player's): the middle of the ways found (their polygons
// 8 m and more from both ends) left out and a way found again, four times at most; each maxK times the shortest at
// most, with no more links than it, its middle 3 m and more off the ways before
struct WayChoice { std::vector<Waypoint> way; std::vector<unsigned> avoid; float via[3] = {0, 0, 0}; float len = 0.f; int allies = 0; };
static const NavMesh& WaysMesh() { return gMap->wide ? *gMap->wide : *gMap->nav; }
static void MapWays(const float* goal, float maxK, std::vector<WayChoice>& c) {
    const NavMesh& nav = WaysMesh();
    float g[3];
    if (!Ground(g)) return;
    std::vector<Waypoint> w; std::vector<unsigned> corr, avoid; float l = 0;
    if (!nav.PathAvoiding(g, goal, avoid, w, &l, &corr)) return;
    for (int k = 0; k < 4; ++k) {
        const size_t had = avoid.size();
        for (size_t i = 2; i + 2 < corr.size(); ++i) {
            float pc[3];
            if (nav.PolyCenter(corr[i], pc) && Dist2D(pc, g) > 8000.f && Dist2D(pc, goal) > 8000.f) avoid.push_back(corr[i]);
        }
        if (avoid.size() == had) return;                           // (nothing more to keep out of)
        WayChoice o;
        if (!nav.PathAvoiding(g, goal, avoid, o.way, &l, &corr) || o.way.size() < 2) return;
        o.len = WayLength(o.way);
        if (o.len > c[0].len * maxK || LinkCount(o.way) > LinkCount(c[0].way) || PathTrapped(o.way) || !WayMiddle(o.way, o.via)) return;
        for (const WayChoice& e : c) if (DistToWay(o.via, e.way) < 3000.f) return;
        o.avoid = avoid;
        c.push_back(std::move(o));
    }
}
// its way for a trip beyond 20 m: the shortest or another one, the map's other ways (MapWays) and flank points on
// either side (FlankVia), five at most. An item's, a carrier's, an intruder's way (not habit): the shortest unless
// allies go that way (near the doll, or on its middle); else its taste (its number: allies leaving together take
// different ways; one time in five a way drawn), away from the ways allies are on (their place a second ahead on
// their moves). False: the shortest
static bool ChooseWay(const float* goal, const std::vector<Waypoint>& shortest, float shortLen, bool habit, std::vector<Waypoint>& out, float* via,
                      std::vector<unsigned>& avoid, int* which, int* ways) {
    const float maxK = habit ? 1.5f : 1.35f;
    // (the allies' places a second ahead on their moves)
    std::vector<std::array<float, 3>> allies;
    bool crowded = false;
    for (const Player& a : gPlayers) {
        if (a.local || !a.alive || !Ally(a)) continue;
        if (Dist2D(a.pos, gMe.pos) < 6000.f) crowded = true;
        allies.push_back({a.pos[0] + a.vel[0] * 60.f, a.pos[1], a.pos[2] + a.vel[1] * 60.f});
        if (DistToMiddle(allies.back().data(), shortest) < 4000.f) crowded = true;
    }
    if (!habit && !crowded) return false;
    std::vector<WayChoice> c(1);
    c[0].way = shortest; c[0].len = shortLen;
    MapWays(goal, maxK, c);
    const size_t mapWays = c.size() - 1;
    for (int side : {gFlankSide, -gFlankSide}) {                  // (up to five ways: flank points besides, its side only after a map's way)
        if (c.size() >= 5 || (mapWays && side != gFlankSide)) break;
        WayChoice f;
        if (!FlankVia(goal, side, shortest, shortLen, maxK, f.via, f.way)) continue;
        bool apart = true;
        for (const WayChoice& e : c) if (DistToWay(f.via, e.way) < 3000.f) apart = false;
        if (apart) { f.len = WayLength(f.way); c.push_back(std::move(f)); }
    }
    if (c.size() < 2) return false;
    for (const auto& at : allies) for (WayChoice& k : c) if (DistToMiddle(at.data(), k.way) < 4000.f) ++k.allies;
    const size_t n = c.size();
    size_t pick = Rnd01() < 0.2f ? Rnd() % n : (size_t)std::max(0, gMyId) % n;
    int fewest = c[0].allies;
    for (const WayChoice& k : c) fewest = std::min(fewest, k.allies);
    for (size_t i = 0; i < n && c[pick].allies > fewest; ++i) if (c[(pick + i) % n].allies == fewest) pick = (pick + i) % n;   // (the next one fewer allies are on)
    *which = (int)pick; *ways = (int)n;
    if (pick == 0) return false;
    out = std::move(c[pick].way);
    avoid = std::move(c[pick].avoid);
    memcpy(via, c[pick].via, sizeof(float) * 3);
    return true;
}
// a trip (the same kind of way, the goal within 20 m of the last one, planned again within 30 s): its way chosen
// once (ChooseWay) and kept (a way of the map's: out of the same polygons; a flank's: through its point until
// passed); over when the goal moved 8 m, when it no longer serves (1.6 times the shortest), when a leg is given up
bool PlanFlank(const float* goalIn, bool habit) {
    if (gOldWays) return PlanTo(goalIn);
    std::vector<Waypoint> p; float len = 0, goal[3];
    if (!PlanWay(goalIn, p, len, goal)) return false;
    const bool sameTrip = gFrame - gTrip.at < 1800 && Dist2D(gTrip.goal, goal) < 20000.f && gTrip.habit == habit;
    gTrip.at = gFrame; memcpy(gTrip.goal, goal, sizeof gTrip.goal); gTrip.habit = habit;
    float g[3];
    if (!Ground(g) || p.size() < 2) { SetWay(std::move(p), goal, goalIn); return true; }
    const float shortLen = WayLength(p), d = Dist2D(g, goal);
    std::vector<Waypoint> f;
    if (!sameTrip) {
        gTrip.haveVia = false; gTrip.avoid.clear();
        int which = 0, ways = 1;
        if (d > 20000.f && ChooseWay(goal, p, shortLen, habit, f, gTrip.via, gTrip.avoid, &which, &ways)) {
            gTrip.haveVia = true; memcpy(gTrip.viaFor, goal, sizeof gTrip.viaFor);
            if (gDebug) Log("path: way %d of %d to %.0f %.0f %.0f by %.0f %.0f %.0f (%.0f m, the shortest %.0f m)", which + 1, ways, goal[0], goal[1], goal[2],
                            gTrip.via[0], gTrip.via[1], gTrip.via[2], WayLength(f) / 1000.f, shortLen / 1000.f);
            SetWay(std::move(f), goal, goalIn);
            return true;
        }
        if (gDebug && d > 20000.f && ways > 1) Log("path: way 1 of %d to %.0f %.0f %.0f (the shortest, %.0f m)", ways, goal[0], goal[1], goal[2], shortLen / 1000.f);
    } else if (gTrip.haveVia && Dist2D(gTrip.viaFor, goal) <= 8000.f) {
        float left = 0, l = 0;
        if (!gTrip.avoid.empty()) {
            if (WaysMesh().PathAvoiding(g, goal, gTrip.avoid, f, &l, nullptr) && f.size() >= 2 && WayLength(f) <= shortLen * 1.6f && !PathTrapped(f) &&
                Dist2D(f.back().p, goal) < 1000.f && std::fabs(f.back().p[1] - goal[1]) < 1000.f) {   // (to the goal itself)
                SetWay(std::move(f), goal, goalIn);
                return true;
            }
        } else if (Dist2D(g, gTrip.via) >= 3000.f && WayVia(gTrip.via, goal, f, &left) && shortLen > left + 1000.f && WayLength(f) <= shortLen * 1.6f) {
            SetWay(std::move(f), goal, goalIn);                             // (passed: within 3 m of it, or no farther from the goal than it is)
            return true;
        }
        gTrip.haveVia = false; gTrip.avoid.clear();
    } else { gTrip.haveVia = false; gTrip.avoid.clear(); }
    SetWay(std::move(p), goal, goalIn);
    return true;
}
// no way to p itself (an item on a log or a crate, a spot the mesh does not reach): the nearest point
// reachable within maxR of it (rings 0.75 m apart, 8 ways each), the rest tried straight (TakeItem)
bool PlanNear(const float* p, float maxR) {
    if (!MapReady()) return false;
    // (v137) a point a climb under p's floor (1.2 m) to a drop over it (3 m), not any within 3 m of p's height: one 2.8 m
    // under Killhouse B's catwalk KEROTAN, 3.3 m under Pillbox Purgatory's ledge, was gone to and stood at
    const float above[3] = {p[0], p[1] + 300.f, p[2]};
    const float g = gMap->nav->GroundBelow(above, 3000.f), floorY = g > -1e29f ? g : p[1] - 1000.f;
    for (float r = 750.f; r <= maxR + 1.f; r += 750.f)
        for (int k = 0; k < 8; ++k) {
            const float a = k * 0.7853982f;
            const float q[3] = {p[0] + std::sin(a) * r, p[1], p[2] + std::cos(a) * r};
            float s[3];
            if (!SnapToMesh(q, s) || Dist2D(s, p) > maxR + 500.f || s[1] < floorY - 1200.f || s[1] > floorY + 3000.f || !PlanTo(s)) continue;
            memcpy(gF.nearFor, p, sizeof gF.nearFor); gF.nearHave = true;
            return true;
        }
    return false;
}
// p on another part of the mesh than the doll's ground (an island no path nor link joins: v133's audit,
// Killhouse B's boxes on a 3 m block, five tried in turn every 30 s): never reached by a path
bool OtherPart(const float* p) {
    if (!MapReady()) return false;
    float g[3], s[3];
    if (!Ground(g) || !SnapToMesh(p, s)) return false;
    const int a = gMap->nav->ComponentAt(g), b = gMap->nav->ComponentAt(s);
    return a >= 0 && b >= 0 && a != b;
}
// ---- out of a pocket of the mesh (v136): a part of a few polygons with no link out, a larger part around
// (Snake's start on Mountaintop, a pit whose steps are too high and too small for the mesh; on Graniny Gorki
// Lab, a room at the foot of two stairs). The ways toward the larger parts nearest first, then the eight
// compass points: walked into, the action button (a climb) while blocked, 6 s a way at most; out when the
// ground under the doll is on a larger part; none worked: again in 10 s ----
struct EscapeRun { int part = -1; long long at = -1, tryAt = 0, checkAt = 0, offUntil = -100000, wallSince = -1, releaseUntil = -1, wallAt = -100000, ticks = 0, tryTicks = 0; std::vector<float> dirs; size_t idx = 0; float tryFrom[3] = {0, 0, 0}, checkFrom[3] = {0, 0, 0}, hips0 = 0, partAt[3] = {0, 0, 0}; int climbs = 0; bool action = false; long long actionAt = 0, settleAt = -1; };
static EscapeRun gEs;
static std::map<int, float> gEscapeWay;              // this map's pockets: the way that got out of each (tried first)
static void KnownWayFirst(int part) {                // (the way that got out of this pocket before: first)
    const auto known = gEscapeWay.find(part);
    if (known == gEscapeWay.end()) return;
    gEs.dirs.erase(std::remove_if(gEs.dirs.begin(), gEs.dirs.end(), [&](float a) { return std::fabs(Wrap(a - known->second)) < 0.35f; }), gEs.dirs.end());
    gEs.dirs.insert(gEs.dirs.begin(), known->second);
}
static int LargestPart() { int m = 0; for (int c = 0; c < gMap->nav->ComponentCount(); ++c) m = std::max(m, gMap->nav->ComponentSize(c)); return m; }
// the ways out: along each of 32 directions, the ground's profile (the first surface under a point 1.4 m above
// the last ground, every 0.25 m, 4 m at most): a rise over 1.3 m ends it (a wall), a larger part reached makes it
// a way (its cost: its length, 0.4 m more for each step to climb, 1.5 m more when the knees meet something on
// the way: a door, a thin wall). The ways by cost, then the eight compass points
static std::vector<float> EscapeDirs(const float* gMesh, int part) {
    // (from the ground under the doll: on a step above its pocket's mesh, from that step)
    const float under = gMap->nav->GroundBelow(gMe.pos, 2500.f);
    const float g[3] = {gMesh[0], under > -1e29f ? under : gMesh[1], gMesh[2]};
    std::vector<std::pair<float, float>> cand;          // (cost, angle)
    for (int a = 0; a < 32; ++a) {
        const float ang = a * 6.2831853f / 32.f, sx = std::sin(ang), sz = std::cos(ang);
        float prev[3] = {g[0], g[1], g[2]};
        int climbs = 0; bool knees = false;
        for (float r = 250.f; r <= 4000.f; r += 250.f) {
            // (the ground from 1.4 m over the last one: under a lintel, a ceiling; a wall over the chest ends the way)
            const float above[3] = {g[0] + sx * r, prev[1] + 1400.f, g[2] + sz * r};
            const float gy = gMap->nav->GroundBelow(above, 6000.f);
            if (gy < -1e29f) break;
            if (gy - prev[1] > 1300.f) break;                 // (too high a step)
            const float c0[3] = {prev[0], prev[1] + 1450.f, prev[2]}, c1[3] = {above[0], std::max(prev[1], gy) + 1450.f, above[2]};
            if (!gMap->nav->Clear(c0, c1)) break;             // (a wall)
            if (gy - prev[1] > 350.f) ++climbs;
            const float k0[3] = {prev[0], prev[1] + 450.f, prev[2]}, k1[3] = {above[0], gy + 450.f, above[2]};
            if (std::fabs(gy - prev[1]) <= 350.f && !gMap->nav->Clear(k0, k1)) knees = true;   // (a sill, a thin wall at the knees: a door? not a step's own edge)
            const float here[3] = {above[0], gy, above[2]};
            float q[3];
            if (gMap->nav->Nearest(here, 300.f, q)) {
                const int c = gMap->nav->ComponentAt(q);
                if (c != part && gMap->nav->ComponentSize(c) >= 20 && std::fabs(q[1] - gy) < 600.f) { cand.push_back({r + 400.f * climbs + (knees ? 1500.f : 0.f), ang}); break; }
            }
            prev[0] = here[0]; prev[1] = gy; prev[2] = here[2];
        }
    }
    std::sort(cand.begin(), cand.end());
    std::vector<float> dirs;
    auto apart = [&](float a) { for (float d : dirs) if (std::fabs(Wrap(a - d)) < 0.35f) return false; return true; };
    for (const auto& c : cand) if (dirs.size() < 8 && apart(c.second)) dirs.push_back(c.second);
    for (int k = 0; k < 8; ++k) { const float a = k * 0.7853982f; if (apart(a)) dirs.push_back(a); }
    return dirs;
}
static void EscapeTry() {
    gEs.tryAt = gEs.checkAt = gFrame + 12; gEs.action = false; gEs.hips0 = gMe.pos[1];   // (12 frames let go first: off a wall it was pressed to)
    gEs.tryTicks = gEs.ticks;
    memcpy(gEs.tryFrom, gMe.pos, sizeof gEs.tryFrom); memcpy(gEs.checkFrom, gMe.pos, sizeof gEs.checkFrom);
}
void EscapeForget() { gEs = EscapeRun(); }
// (v137) off the mesh: no polygon within 0.5 m under the feet (a spawn on a ledge the mesh's edge left out; the spawn's
// strike slid the doll there). Every way then failed ("no way to") and the doll stood: dm/d3 f97236, 158 s at its spawn.
// The nearest polygon within 6 m (rings 0.75 m apart, 16 ways; from 2.5 m under the feet to 0.5 m over them; a clear
// line at the knees first), walked to; 4 s at most, none: again in 10 s
static bool OffMeshFrame(bool on) {
    static long long since = -1, at = -1;
    static float to[3];
    if (on || OnLadder() || (gMe.bits[2] & 0x10000000) || Following() || !gMe.alive) { since = at = -1; return false; }
    if (since < 0) since = gFrame;
    if (gFrame - since < 60) return false;               // (a step off an edge, a drop, a link: a moment first)
    if (at < 0) {
        const float feet[3] = {gMe.pos[0], gMe.pos[1] - 700.f, gMe.pos[2]};
        float best = 1e30f; bool clear = false, found = false;
        for (float r = 750.f; r <= 6000.f && !(found && clear); r += 750.f)
            for (int k = 0; k < 16; ++k) {
                const float a = k * 0.3926991f;
                const float q[3] = {feet[0] + std::sin(a) * r, feet[1], feet[2] + std::cos(a) * r};
                float s[3];
                if (!gMap->nav->Nearest(q, 400.f, s) || s[1] - feet[1] > 500.f || feet[1] - s[1] > 2500.f) continue;
                const float knee0[3] = {feet[0], feet[1] + 450.f, feet[2]}, knee1[3] = {s[0], std::max(s[1], feet[1]) + 450.f, s[2]};
                const bool c = gMap->nav->Clear(knee0, knee1);
                const float d = Dist2D(feet, s);
                if ((c && !clear) || (c == clear && d < best)) { best = d; clear = c; found = true; memcpy(to, s, sizeof to); }
            }
        if (!found) { Log("escape: off the mesh at %.0f %.0f %.0f, no polygon within 6 m: again in 10 s", gMe.pos[0], gMe.pos[1], gMe.pos[2]); since = -1; gEs.offUntil = gFrame + 600; return false; }
        at = gFrame;
        Log("escape: off the mesh at %.0f %.0f %.0f: to %.0f %.0f %.0f (%.1f m%s)", gMe.pos[0], gMe.pos[1], gMe.pos[2], to[0], to[1], to[2], best / 1000.f, clear ? "" : ", a wall on the line");
    }
    if (gFrame - at > 240) {
        Log("escape: off the mesh, %.0f %.0f %.0f not reached in 4 s: again in 10 s", to[0], to[1], to[2]);
        since = at = -1; gEs.offUntil = gFrame + 600; Steer(0, 0, 0);
        return false;
    }
    Act("escaping");
    StopPath();
    Steer(to[0] - gMe.pos[0], to[2] - gMe.pos[2], Dist2D(gMe.pos, to) < 1200.f ? kWalkPush : 1.f);
    return true;
}
bool EscapeFrame() {
    if (!MapReady() || gFrame < gEs.offUntil) return false;
    float g[3];
    const bool on = Ground(g);
    if (gEs.at < 0 && OffMeshFrame(on)) return true;
    const int part = on ? gMap->nav->ComponentAt(g) : -1, size = on ? gMap->nav->ComponentSize(part) : 0;
    if (gEs.at < 0) {
        if (!on || size > 6 || LargestPart() < 30) return false;
        const long long off = gEs.offUntil;
        gEs = EscapeRun(); gEs.offUntil = off; gEs.part = part; gEs.at = gFrame; gEs.dirs = EscapeDirs(g, part);
        memcpy(gEs.partAt, g, sizeof gEs.partAt);
        KnownWayFirst(part);
        std::string first;
        for (size_t k = 0; k < gEs.dirs.size() && k < 4; ++k) first += " " + std::to_string((int)std::lround(gEs.dirs[k] * 57.29578f));
        Log("escape: on a part of %d polygons (the largest %d) at %.0f %.0f %.0f: %zu ways out to try (the first at%s degrees)", size, LargestPart(), gMe.pos[0], gMe.pos[1],
            gMe.pos[2], gEs.dirs.size(), first.c_str());
        StopPath(); EscapeTry();
    }
    if (on && part != gEs.part && size >= 20) {
        Log("escape: out onto a part of %d polygons in %.1f s (way %zu of %zu, %d climbs) at %.0f %.0f %.0f", size, gEs.ticks / 60.0, gEs.idx + 1, gEs.dirs.size(),
            gEs.climbs, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
        gEscapeWay[gEs.part] = gEs.dirs[std::min(gEs.idx, gEs.dirs.size() - 1)];
        LearnWrite(FactEscape(gEs.partAt, gEscapeWay[gEs.part]));
        gEs.at = -1; gEs.offUntil = gFrame + 300;
        return false;
    }
    if (on && part != gEs.part && size <= 6) {           // (another pocket: its ways)
        gEs.part = part; gEs.dirs = EscapeDirs(g, part); gEs.idx = 0; memcpy(gEs.partAt, g, sizeof gEs.partAt);
        KnownWayFirst(part); EscapeTry();
    }
    if (gEs.idx >= gEs.dirs.size() || gEs.ticks > 3600) {
        Log("escape: no way out found (%zu tried in %.1f s): again in 10 s", gEs.dirs.size(), gEs.ticks / 60.0);
        gEs.at = -1; gEs.offUntil = gFrame + 600;
        Steer(0, 0, 0);
        return false;
    }
    Act("escaping");
    if (++gEs.ticks < 20) { Steer(0, 0, 0); return true; }   // (its first frames: the spawn's own)
    const float a = gEs.dirs[gEs.idx];
    if (gFrame < gEs.tryAt || gFrame < gEs.releaseUntil) { Steer(0, 0, 0); return true; }
    // (pressed to a wall, its back to it: let go a moment, then on with the action button)
    const uint32_t st = gMe.bits[0] & 0xFFFF;
    if (st == 2 || st == 0x4002 || st == 4 || st == 0x4004) {
        gEs.wallAt = gFrame;
        if (gEs.wallSince < 0) gEs.wallSince = gFrame;
        if (gFrame - gEs.wallSince > 20) {
            gEs.wallSince = -1; gEs.releaseUntil = gFrame + 10;
            if (!gEs.action) { gEs.action = true; gEs.actionAt = gFrame; }
            Steer(0, 0, 0); return true;
        }
    } else gEs.wallSince = -1;
    Steer(std::sin(a), std::cos(a), 1.f);
    if (gEs.settleAt >= 0 && gFrame >= gEs.settleAt) { gEs.hips0 = gMe.pos[1]; gEs.settleAt = -1; }   // (the climb's own rise counted once: from where it ended)
    if (gEs.settleAt < 0 && gMe.pos[1] - gEs.hips0 > 300.f) {   // (up a step: a climb, the ways from there)
        ++gEs.climbs; gEs.action = false; gEs.checkAt = gFrame; gEs.settleAt = gFrame + 45; memcpy(gEs.checkFrom, gMe.pos, sizeof gEs.checkFrom);
        if (gDebug) Log("escape: up a step (%.0f) toward %.0f degrees", gMe.pos[1], a * 57.29578f);
        if (float g2[3]; Ground(g2) && gEs.climbs <= 6) {
            gEs.dirs = EscapeDirs(g2, gEs.part); gEs.idx = 0; gEs.tryAt = gFrame;
            gEs.tryTicks = gEs.ticks; memcpy(gEs.tryFrom, gMe.pos, sizeof gEs.tryFrom);   // (its 6 s from the step)
        }
    }
    if (gEs.action && gFrame % 15 < 3) gPad.buttons |= 0x1000;   // (the action button while pushed: a climb)
    if (gFrame - gEs.checkAt >= 30) {
        const bool blocked = Dist2D(gMe.pos, gEs.checkFrom) < 150.f || gFrame - gEs.wallAt < 30;   // (along a wall, pressed to it by turns: blocked)
        gEs.checkAt = gFrame; memcpy(gEs.checkFrom, gMe.pos, sizeof gEs.checkFrom);
        if (blocked && !gEs.action) { gEs.action = true; gEs.actionAt = gFrame; }
        else if (blocked && gFrame - gEs.actionAt >= 60) {
            if (gDebug) Log("escape: way %zu (%.0f degrees) blocked: %.1f m from where it began", gEs.idx + 1, a * 57.29578f, Dist2D(gMe.pos, gEs.tryFrom) / 1000.f);
            ++gEs.idx; EscapeTry();
        }
    }
    if (gEs.ticks - gEs.tryTicks > 360) {               // (6 s a way at most)
        if (gDebug) Log("escape: way %zu (%.0f degrees) given up after 6 s: %.1f m from where it began", gEs.idx + 1, a * 57.29578f, Dist2D(gMe.pos, gEs.tryFrom) / 1000.f);
        ++gEs.idx; EscapeTry();
    }
    return true;
}
// the path being followed goes to p, or near it (PlanNear)
bool PathServes(const float* p) {
    // ((v141 beta 3) or the point it was asked for: a goal snapped to the mesh 1.5 m away and more made the way planned
    // again every frame, its first corner reached at once, no step taken: three dolls still 9 minutes at Pillbox
    // Purgatory's ladder, its KEROTAN asked for)
    return gF.active && (Dist2D(gF.goal, p) <= 1500.f || Dist2D(gF.asked, p) <= 300.f || (gF.nearHave && Dist2D(gF.nearFor, p) < 300.f));
}
bool Following() { return gF.active; }
size_t PathCorners() { return gF.path.size(); }
bool PathHasLink(uint8_t area) { for (const Waypoint& w : gF.path) if (w.link && w.area == area) return true; return false; }
// (v141 beta 3) the way left along the path to its next link's start (a ladder, a climb, the stairs, a vault, a drop): 0 on
// one, 1e30 with none within 30 m (NIGHTMARE's box runner: the box off before it, no ladder nor ledge taken in it)
float NextLinkDist() {
    if (!gF.active || gF.next >= gF.path.size()) return 1e30f;
    if (gF.next > 0 && gF.path[gF.next - 1].link) return 0.f;
    float d = 0.f;
    const float* a = gMe.pos;
    for (size_t i = gF.next; i < gF.path.size() && d < 30000.f; ++i) {
        d += Dist2D(a, gF.path[i].p);
        if (gF.path[i].link) return d;
        a = gF.path[i].p;
    }
    return 1e30f;
}
// the path left passes within r of p (on the same floor): a guard's way across the item it must not take
bool PathNear(const float* p, float r) {
    if (!gF.active || gF.next >= gF.path.size()) return false;
    const float* a = gMe.pos;
    for (size_t i = gF.next; i < gF.path.size(); ++i) {
        const float* b = gF.path[i].p;
        const float abx = b[0] - a[0], abz = b[2] - a[2], len2 = abx * abx + abz * abz;
        float t = len2 > 1.f ? ((p[0] - a[0]) * abx + (p[2] - a[2]) * abz) / len2 : 0.f;
        t = std::max(0.f, std::min(1.f, t));
        const float cx = a[0] + abx * t - p[0], cz = a[2] + abz * t - p[2];
        if (cx * cx + cz * cz < r * r && std::fabs(b[1] - p[1]) < 2500.f) return true;
        a = b;
    }
    return false;
}
void StopPath() { gF.active = false; gLR = LadderRun(); gCR = CrossRun(); }
const float* PathGoal() { return gF.active ? gF.goal : nullptr; }
const float* PathAsked() { return gF.active ? gF.asked : nullptr; }

// the posture the path asks for: crawling in the low passages (Cross toggles crouching; moving
// crouched crawls), standing elsewhere; the hips' height above the ground tells which one it is
static long long gPostureAt = -1000;
float HipHeight() {                                  // the hips above the ground under them (800 standing, 300 low)
    if (!MapReady()) return 800.f;
    const float gy = gMap->nav->GroundBelow(gMe.pos, 2500.f);
    return gy > -1e29f ? gMe.pos[1] - gy : 800.f;
}
// lying on its front (the state word 0: 9 still, 0x48 going down, 0x4008 and 0x4018 crawling, 0xC8 in a
// first person view; v133's audit: Cross makes it 0x44 then 5, crouched, Cross again 0x42 then 3). The
// word tells it where the mesh's ground under the hips is off (a slope: a doll lay 167 s in a fight)
bool Prone() { const uint32_t st = gMe.bits[0] & 0xFFFF; return st == 9 || st == 0x48 || st == 0xC8 || st == 0x4008 || st == 0x4018; }
// low: crouched (the state word 0 is 5 still, 0x85 in a first person view, 4, 0x4004 and 0x84 against a wall:
// the hips' point does not go down crouched) or lying / crawling (the hips 300 above the ground); never on its
// feet (the word 0: 3 still, 0x4802 walking, 0x5002 running, 0x6002 turning, 2 and 0x4002 against a wall, 0x82
// and 0x83 in a first person view: v134, the ground under the hips misread on a slope made a running doll
// press Cross, a roll). (v137) 0x100: the first person view's way in or out, a few frames (5 > 0x185 > 0x85, 0x85 >
// 0x105 > 5, 3 > 0x183 > 0x83, 0x83 > 0x103 > 3): 0x105 and 0x185 read as standing made SetLow press Cross, the
// player stood up and crouched again (29 times in the v137 bench's DM and TDM: a sniper taking its scope, a fight's aim)
bool Low() {
    const uint32_t st = gMe.bits[0] & 0xFFFF;
    if (st == 3 || st == 0x4802 || st == 0x5002 || st == 0x6002 || st == 2 || st == 0x4002 || st == 0x82 || st == 0x83 || st == 0x102 || st == 0x103 ||
        st == 0x183) return false;
    return st == 5 || st == 0x85 || st == 4 || st == 0x4004 || st == 0x84 || st == 0x105 || st == 0x185 || Prone() || HipHeight() < 550.f;
}
// running or turning (the state word 0: 0x5002, 0x6002): Cross there may roll (probe 31), never pressed to crouch
static bool RunningNow() { const uint32_t st = gMe.bits[0] & 0xFFFF; return st == 0x5002 || st == 0x6002; }
// (v137) the state word 0 between two postures (0x42 getting up, 0x44 crouching, 0x48 lying down), or the first person
// view's way in or out (0x100): no press then
static bool PostureMoving() { const uint32_t st = gMe.bits[0] & 0xFFFF; return (st & 0xFFF0) == 0x40 || (st & 0xFF00) == 0x100; }
bool SetLow(bool wantLow) {                          // Cross toward the posture wanted: true when it is there
    const bool low = Low();
    if (low == wantLow) return true;
    if (wantLow && RunningNow()) return false;       // (running, Cross rolls: v134, probe 31; walking it crouches)
    // (v137) a press of 3 frames (PostureHold), 45 apart: the one-frame tap was missed by the game 337 times in 550
    // (v136's audit: the posture asked again every 31 frames, the doll walking on crouched, crawling)
    if (gFrame - gPostureAt > 45 && !PostureMoving()) { gPad.buttons |= 0x4000; gPostureAt = gFrame; }
    return false;
}
static void PostureHold() { if (gFrame - gPostureAt >= 1 && gFrame - gPostureAt < 3) gPad.buttons |= 0x4000; }   // (after the brain's frame)
static void Posture(uint8_t area) {
    const bool wantLow = area == AREA_CRAWL;
    const bool low = Low();
    if (wantLow && !low && RunningNow()) return;     // (running: a walk first, FollowPath)
    if (low != wantLow && gFrame - gPostureAt > 45 && !PostureMoving()) {
        if (gDebug) Log("posture: %s (hips %.0f above the ground, area %u)", wantLow ? "down" : "up", HipHeight(), area);
        SetLow(wantLow);
    }
}

// ---- ladders (doll_geom.h, probe 12): standing in the foot's volume (the top's), still, facing the
// ladder (back toward it from the top), the action button takes it (the state word 2 has 0x40000 while on
// it, the hips 1.2 m up at once); the stick up (down) climbs at 1.3 m/s; the player gets off by himself
// at the other end. (v137) Two failures of its own: out of the paths 3 minutes (then 6, 12, 24); a known enemy
// at an end: its cost x4; its other end covered: a wait, else a different way; on it, back while under 55 % ----
struct LadderNote { int hard = 0, outs = 0, up = 0, down = 0, failed = 0, waited = 0, avoided = 0, back = 0; long long outUntil = -1, avoidUntil = -1; float k = 1.f; };
static std::map<int, LadderNote> gLadderNotes;       // (v137) by ladder number, this map's
static std::string gLadderPage;                      // (their map)
static bool gLadderDown = false;                     // the way of the last ladder taken (a climb finished after a fight)
struct LadderOn { int number = -1; float fromY = 0.f, toY = 0.f; bool back = false; };
static LadderOn gLO;                                 // (v137) the ladder it is on (LadderFrame, out of a path)
static const long long kLadderWait = 360;            // (v137) its other end covered: frames waited by it
static void LadderCost(int number, LadderNote& note, float k, const char* why) {
    if (k == note.k || !MapReady()) return;
    note.k = k;
    gMap->nav->SetLadderCost(number, k);
    if (gMap->wide) gMap->wide->SetLadderCost(number, k);
    if (gDebug) Log("ladder %03d: its cost x%.0f (%s)", number, k, why);
}
// (v137) on a ladder: back the way it came when a known enemy covers its other end and under 55 % of the climb is done
static bool LadderBack(const Ladder& l, float fromY, float toY, bool down) {
    if (gFrame % 10 || std::fabs(toY - fromY) < 500.f) return false;
    const float done = (gMe.pos[1] - 800.f - fromY) / (toY - fromY);
    float where[3]; int who = -1; float ago = 0.f;
    if (done >= 0.55f || LadderThreat(down ? l.foot : l.top, where, &who, &ago) == 0) return false;
    const Player* e = PlayerById(who);
    Log("ladder %03d: %s at its %s (known %.1f s ago), %.0f %% of the way: back %s", l.number, e ? e->name.c_str() : "an enemy", down ? "foot" : "top", ago,
        std::max(0.f, done) * 100.f, down ? "up" : "down");
    return true;
}

// ---- (v137) stairs of small blocks (AREA_STEPS, doll_nav.cpp's pass 4): each block walked to in turn, one higher than
// the ground climbed with the action button (3 frames in 8 within 1.5 m, facing it), a lower one walked off. No nearer
// for 3 s: failed; twice: out of the paths 3 minutes (then 6, 12, 24) ----
struct StepsRun { float from[3] = {0, 0, 0}, to[3] = {0, 0, 0}; size_t idx = 0; long long at = 0, bestAt = 0, backAt = -1; float best = 1e30f; bool active = false;
                  std::vector<std::array<float, 3>> pts;
                  int variant = 0; size_t uIdx = (size_t)-1; float u[2] = {0, 0}; bool aligned = false; float alignBest = 1e30f;
                  int ph = 0, tries = 0, stillFor = 0; long long phAt = 0, stepAt = 0, pressAt = -1; float c[3] = {0, 0, 0}; bool upSeen = false;   // (way 2)
                  PressRun pr; };                                    // ((v141 beta 6) way 3)
static StepsRun gSR;
static Pad gLastPad;                                 // the pad of the frame before (the state's log, the stairs' trace)
const Pad& LastPad() { return gLastPad; }            // ((v141 beta 9) probe 31's choke step)
static int gStairsTries = 0;                         // (v141) the tries (MGO1_STAIRS_WAY r: a way each in turn)
// (v141) the ways a block is climbed. 0 (v137): walked to, the action button 3 frames in 8 within 1.5 m facing it within 40
// degrees. 1, the square approach: straight at a side of the block (the axis of its larger offset), first in front of that
// side's middle 1.1 m out (on a block: in its middle), then the stick along that axis and the action button facing it within
// 25 degrees. 2, the careful climb (StepsCareful). 3 (v141 beta 6), the press run (StepsPress: the block's face from the
// collision, walked in to the press's distance). MGO1_STAIRS_WAY: 0, 1, 2 or 3; r each in turn; default 3
static int StairsVariant() {
    static int mode = -1;
    if (mode < 0) {
        const char* e = getenv("MGO1_STAIRS_WAY");
        mode = !e ? 3 : *e == 'r' ? 4 : std::max(0, std::min(3, atoi(e)));
    }
    return mode == 4 ? gStairsTries % 4 : mode;
}
// (v141) pressed flat against a wall by its back (the state word 0 2 or 0x4002, the word 6's 0x40), not a climb's frames
// (the word 7's 2: the climb and its last frames, the word 0 is 2 then too: v141's KHB trace, a climb's end taken for a
// wall made the doll step back to the block's edge and turn round on it)
static bool PressedFlat() {
    const uint32_t st0 = gMe.bits[0] & 0xFFFF;
    return (st0 == 2 || st0 == 0x4002) && (gMe.bits[6] & 0x40) && !(gMe.bits[7] & 2);
}

// ---- (v141 beta 6) the careful press at a face (a ledge, a block of stairs, a vault's obstacle). Probe 41's traces on
// Killhouse B's 1 m blocks, their faces taken from the collision (beta 2 put a face 0.45 m from a block's middle, 0.04 to
// 0.06 m off): the climb starts from standing, facing the face, the hips 0.41 to 0.65 m from it; pressed from 0.654 m and
// farther the player walked into the face and pressed his back to it, the hips then 0.433 m from it (33 presses of 33).
// Beta 3's climbs pressed from 0.62 to 0.67 m (a launch 0.64 m out, the turn's pulses moving the player), after a back
// pressed to the face again from where the turn left them (0.63 to 0.67 m) 5 times. Now: the face from the collision at
// every height under the top (FaceAlong), the launch 0.9 m out on the link's line, turned there, moved in to the press's
// window (by the top's height, PressFrame: a walk from afar, let go its run on before the window, then pulses of the stick
// full; closer than 0.36 m: a step back, 3 at most), still, the press; a back pressed
// to the face gives its place (0.433 m from the hips) to the next presses. The first links bench of beta 6: 1 m ledges up
// from 0.633 to 0.648 m, Svyatogornyj East's 0.65 m top from 0.38 m (nothing from 0.53 m), Killhouse B's blocks from 0.41
// and from 0.64 m; a press for nothing moved the player 0.03 to 0.16 m nearer. A climb under way: the state word 2's 0x2C00, the word 7's 2, or the word 0 at 0xC002 (a low
// top's step up: Ghost Factory's 0.6 m ledges, Svyatogornyj East's 0.65 m top, taken for nothing by beta 3).
// MGO1_CLIMB_AT: the first press's distance (mm) ----
static bool ClimbingNow() { return (gMe.bits[2] & 0x2C00) || (gMe.bits[7] & 2) || (gMe.bits[0] & 0xFFFF) == 0xC002; }
static float gClimbAt = -1.f;                        // (MGO1_CLIMB_AT)
static float gLowTop = 680.f;                        // (MGO1_CLIMB_LOW_TOP: under it a top's presses from 0.38 to 0.435 m)
static void PressStart(PressRun& r, const float* o, float ux, float uz, float face, float launch, const char* tag, bool noBack) {
    static bool read = false;
    if (!read) {
        read = true;
        if (const char* e = getenv("MGO1_CLIMB_AT")) gClimbAt = std::max(380.f, std::min(700.f, (float)atof(e)));
        if (const char* e = getenv("MGO1_CLIMB_LOW_TOP")) gLowTop = std::max(0.f, std::min(2000.f, (float)atof(e)));
    }
    r = PressRun();
    memcpy(r.o, o, sizeof r.o); r.u[0] = ux; r.u[1] = uz;
    r.face = r.soupFace = face; r.launch = launch; r.tag = tag; r.noBack = noBack;
    r.start = r.phAt = gFrame;
}
// a frame of it: 1 a climb under way, 0 at work (the pad set), -1 given up (why). The press's distance by the top's height
// (the first links bench of beta 6, climbs and stairs: up from 0.60 to 0.65 m 24 presses of 35, from 0.45 to 0.60 m 2 of 25,
// from 0.40 to 0.45 m 9 of 11; tops of 0.6 and 0.65 m up from 0.38 and 0.40 m, nothing from 0.53 and 0.54 m): a top of 0.68 m
// and more from 0.6 to 0.645 m (then 0.42 m), a lower one from 0.38 to 0.435 m (MGO1_CLIMB_LOW_TOP: that height, mm)
static int PressFrame(PressRun& r, const char** why) {
    static const float kAt[6] = {620.f, 610.f, 625.f, 420.f, 615.f, 400.f}, kAtLow[6] = {410.f, 395.f, 420.f, 400.f, 415.f, 390.f};
    if (ClimbingNow()) { Steer(0, 0, 0); return 1; }
    if (gFrame - r.start > 900) { *why = "15 s"; return -1; }
    Posture(AREA_STAND);
    const long long pt = gFrame - r.phAt;
    const float rx = gMe.pos[0] - r.o[0], rz = gMe.pos[2] - r.o[2];
    const float along = rx * r.u[0] + rz * r.u[1], aside = rx * r.u[1] - rz * r.u[0], df = r.face - along;
    const float faceAng = std::atan2(r.u[0], r.u[1]), off = std::fabs(Wrap(faceAng - Facing()));
    const uint32_t st0 = gMe.bits[0] & 0xFFFF;
    const float kDeg = 3.14159265f / 180.f;
    // (the second links bench: SILO ENTRANCE's 0.71 m top, 12 presses from 0.36 to 0.39 m for nothing, up from 0.57 m; the
    // first: tops of 0.6 to 0.65 m up from 0.38 and 0.40 m, 2 tops of 4, nothing from 0.47 to 0.56 m)
    const bool high = r.topH >= gLowTop;
    const float tgt = r.presses == 0 && gClimbAt > 0.f ? gClimbAt : (high ? kAt : kAtLow)[r.presses % 6];
    const float dx = r.u[0] - r.u[1] * aside / 800.f, dz = r.u[1] + r.u[0] * aside / 800.f;   // (toward the face, back onto the line)
    auto phase = [&](int p) { r.ph = p; r.phAt = r.waitFrom = gFrame; r.stillFor = 0; r.pushAt = -1; r.backAt = -1; };
    auto feel = [&]() {                                  // (a back pressed to the face: the face 0.433 m from the hips; 0.2 m
        const float f = along + 433.f;                   //  from the collision's at most: another wall else)
        if (std::fabs(f - r.soupFace) > 200.f) return;
        if (gDebug && std::fabs(f - r.face) > 30.f) Log("%s: the face felt %.0f mm %s than the collision's", r.tag, std::fabs(f - r.soupFace), f > r.soupFace ? "farther" : "nearer");
        r.face = f; r.felt = true;
    };
    switch (r.ph) {
    case 0: {                                            // to the launch point: on the line, launch mm before the face
        const float l[3] = {r.o[0] + r.u[0] * (r.face - r.launch), r.o[1], r.o[2] + r.u[1] * (r.face - r.launch)};
        const float dl = Dist2D(gMe.pos, l);
        const bool on = std::fabs(aside) < 250.f && df < r.launch + 150.f && df > 380.f;   // (on the line and nearer: from here)
        if (dl > 200.f && !on && pt < 300) { Steer(l[0] - gMe.pos[0], l[2] - gMe.pos[2], dl < 900.f ? kWalkPush : 1.f); return 0; }
        Steer(0, 0, 0);
        // (not reached, off the line: the first links bench of beta 6 pressed 1.1 and 1.4 m aside of Mountaintop's blocks, 15 s each)
        if (std::fabs(aside) > 300.f) { *why = "its launch point not reached"; return -1; }
        phase(1); return 0;
    }
    case 1:                                              // still, turned to the face by pulses (2 frames in 6; the last 25 degrees
        Steer(0, 0, 0);                                  // 1 frame in 6: near the face 2 walked the player into it, Killhouse B's block 3)
        if (off > 6.f * kDeg && pt < 150) { if (gFrame % 6 < (off > 25.f * kDeg ? 2 : 1)) Steer(r.u[0], r.u[1], 1.f); return 0; }
        if (pt < 8) return 0;
        phase(2); return 0;
    case 2: {                                            // walked in to the press's window, still, the press
        if (PressedFlat()) { feel(); ++r.flats; ++r.flatRow; r.mv = 0; phase(4); return 0; }
        const float lo = std::max(360.f, tgt - 30.f), hi = std::min(645.f, tgt + 25.f);
        // the move in: from afar a walk, let go its run on (learned, 0.1 m at first) before the window; near it a pulse of the
        // stick full (2 frames at first, 1 to 6 learned: the turn's own pulses moved a still player 16 to 115 mm on Killhouse B's
        // blocks, a walk's push of 2 to 4 frames did not move him at all, beta 6's first links bench)
        if (r.mv == 1) {
            if (df > hi + r.runOn && gFrame - r.pushAt < 240) { Steer(dx, dz, kWalkPush); return 0; }
            r.relDf = df; r.mv = 3; r.pushAt = gFrame; r.stillFor = 0;
        }
        if (r.mv == 2) {
            if (gFrame - r.pushAt < r.pushNow) { Steer(dx, dz, 1.f); return 0; }
            r.mv = 4; r.pushAt = gFrame; r.stillFor = 0;
        }
        if (r.mv >= 3) {                                 // its stop: still 3 frames, 40 at most
            Steer(0, 0, 0);
            r.stillFor = st0 == 3 ? r.stillFor + 1 : 0;
            if (r.stillFor < 3 && gFrame - r.pushAt < 40) return 0;
            const float moved = r.pushFrom - df;
            if (r.mv == 3) {
                r.runOn = std::max(0.f, std::min(300.f, r.relDf - df));
                r.held = moved < 30.f ? 3 : 0;
            } else {
                if (moved < 15.f && r.pushLen < 6) ++r.pushLen; else if (moved > 90.f && r.pushLen > 1) --r.pushLen;
                r.held = moved < 10.f && r.pushNow >= 4 ? r.held + 1 : r.held;
            }
            r.mv = 0; r.waitFrom = gFrame;
        }
        Steer(0, 0, 0);
        r.stillFor = st0 == 3 ? r.stillFor + 1 : 0;
        if (r.stillFor < 3 && gFrame - r.waitFrom < 60) return 0;           // (still 3 frames: a second at most)
        if (off > 10.f * kDeg) {
            if (++r.turns > 5) { *why = "turned off the face"; return -1; }
            phase(1); return 0;
        }
        // (pressed flat twice moving in: the press from where the turn left it; the second links bench, 14 times flat in 15 s
        // and no press, its face felt 0 and 0.15 m off by turns 0.45 m aside along SILO ENTRANCE's 0.71 m top)
        if (df > hi && r.held < 3 && r.pushes < 40 && r.flatRow < 2) {     // farther: a walk in, or a pulse near the window
            ++r.pushes; r.pushFrom = df; r.pushAt = gFrame; r.stillFor = 0;
            if (df > hi + r.runOn + 150.f) { r.mv = 1; Steer(dx, dz, kWalkPush); return 0; }
            r.mv = 2; r.pushNow = r.pushLen; Steer(dx, dz, 1.f);
            return 0;
        }
        // (a step back: closer than 0.36 m, or a high top's 0.46 to 0.57 m)
        if ((df < 360.f || (high && df < lo - 30.f && df > 460.f)) && !r.noBack && r.backs < 3 && r.held < 3) { phase(5); return 0; }
        r.pressDf = df;
        if (gDebug) Log("%s: the press %d, %.0f from the face%s, facing %.0f degrees off, %.0f aside%s", r.tag, r.presses + 1, df, r.felt ? " (felt)" : "", off / kDeg, aside,
                        r.held >= 3 ? ", held there" : "");
        phase(3); r.pressAt = gFrame; r.flatRow = 0;
        return 0;
    }
    case 3: {                                            // the action button and the stick 3 frames, the stick 3 more, let go
        const long long pp = gFrame - r.pressAt;
        if (pp < 3) { Steer(r.u[0], r.u[1], kWalkPush); gPad.buttons |= 0x1000; return 0; }
        if (pp < 6) { Steer(r.u[0], r.u[1], kWalkPush); return 0; }
        Steer(0, 0, 0);
        const bool flat = PressedFlat();
        if (!flat && pp < 24) return 0;
        ++r.presses;
        if (flat) { ++r.flats; feel(); } else ++r.nothings;
        if (gDebug) Log("%s: not up (press %d from %.0f: %s, %.0f from the face now)", r.tag, r.presses, r.pressDf, flat ? "pressed flat" : "nothing", r.face - along);
        if (r.presses >= 6) { *why = "6 presses"; return -1; }
        if (r.held >= 3 && r.presses >= 2) { *why = "held short of the face"; return -1; }
        phase(flat ? 4 : 2); return 0;
    }
    case 4:                                              // off the face: let go, then turned again (the player steps off it)
        Steer(0, 0, 0);
        if (pt < 12) return 0;
        phase(1); return 0;
    default: {                                           // 5: a step back: turned away by pulses, a push, turned again
        const float offA = std::fabs(Wrap(faceAng + 3.14159265f - Facing()));
        if (r.backAt < 0) {
            Steer(0, 0, 0);
            if (offA > 25.f * kDeg && pt < 120) { if (gFrame % 6 < 2) Steer(-r.u[0], -r.u[1], 1.f); return 0; }
            r.backAt = gFrame;
        }
        const long long bt = gFrame - r.backAt;
        if (bt < 3) { Steer(-r.u[0], -r.u[1], 1.f); return 0; }
        Steer(0, 0, 0);
        if (bt < 24) return 0;
        ++r.backs;
        if (gDebug) Log("%s: a step back, %.0f from the face", r.tag, df);
        phase(1); return 0;
    }
    }
}
// (v141 beta 6) a link's obstacle: the nearest face along a -> b at the heights from 0.35 m over the floor at a (over a curb the
// player walks) to 30 mm under topY, a ray each 0.1 m from 0.25 m behind a (-1e30: none). Beta 3 looked at 0.5 m over the mesh
// only, by segments of 25 mm whose ends hid a face lying on them (walls on round coordinates: "not found" on 23 of the 25
// climbs of SILO ENTRANCE and REX HANGAR), and over a 0.49 m step on stairs whose mesh lies 0.19 m over the step (City Under
// Siege: its face 1.5 m off)
static float FaceAlong(const float* a, const float* b, float topY, float* nrm = nullptr, float* hit = nullptr) {
    if (!MapReady()) return -1e30f;
    const float l = std::max(1.f, Dist2D(a, b)), ux = (b[0] - a[0]) / l, uz = (b[2] - a[2]) / l;
    const float up[3] = {a[0], a[1] + 300.f, a[2]};
    float fy = gMap->nav->GroundBelow(up, 1000.f);
    if (fy < -1e29f) fy = a[1];
    float best = 1e30f;
    for (float h = 350.f; h <= std::max(350.f, topY - fy - 30.f); h += 100.f) {
        const float p[3] = {a[0] - ux * 250.f, fy + h, a[2] - uz * 250.f}, q[3] = {a[0] + ux * (l + 500.f), fy + h, a[2] + uz * (l + 500.f)};
        float t = 0.f, n[3];
        if (!gMap->nav->FirstHit(p, q, &t, n) || std::fabs(n[1]) > 0.7f) continue;
        const float d = -250.f + t * (l + 750.f);
        if (d >= best) continue;
        best = d;
        if (nrm) memcpy(nrm, n, sizeof n);
        if (hit) { hit[0] = a[0] + ux * d; hit[1] = fy + h; hit[2] = a[2] + uz * d; }
    }
    return best < 1e29f ? best : -1e30f;
}
// (v141 beta 6) a press square to its face: the face 25 to 65 degrees off the link (a box's corner taken across, beta 3's
// links bench: Brown Town's two vaults at 45 degrees not crossed), the press run's line along the face's normal through the
// point met, the face 1 m along it; true when so
static bool SquareToFace(const float* n, const float* hit, float baseY, float* o, float* u, float* face) {
    const float nl = std::hypot(n[0], n[2]);
    if (nl < 0.5f) return false;
    const float sx = -n[0] / nl, sz = -n[2] / nl, c = sx * u[0] + sz * u[1];
    if (c > 0.906f || c < 0.423f) return false;
    u[0] = sx; u[1] = sz;
    o[0] = hit[0] - sx * 1000.f; o[1] = baseY; o[2] = hit[2] - sz * 1000.f;
    *face = 1000.f;
    return true;
}
struct StepsNote { float a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; int fails = 0, outs = 0; long long outUntil = -1; };
static std::vector<StepsNote> gStepsNotes;           // (this map's stairs that failed)
static void StepsDone(bool ok, const char* why) {
    if (ok) {
        Log("stairs: %s in %.1f s (way %d)", gSR.to[1] > gSR.from[1] ? "up" : "down", (gFrame - gSR.at) / 60.0, gSR.variant);
        gSR.active = false;
        ++gF.next; gF.best = 1e30f; gF.lastProgress = gFrame;
        if (gF.next >= gF.path.size()) { gF.active = false; ++gNavStats.reached; }
        return;
    }
    StepsNote* note = nullptr;
    for (StepsNote& n : gStepsNotes)
        if ((Dist2D(n.a, gSR.from) < 500.f && Dist2D(n.b, gSR.to) < 500.f) || (Dist2D(n.a, gSR.to) < 500.f && Dist2D(n.b, gSR.from) < 500.f)) note = &n;
    if (!note) { gStepsNotes.push_back(StepsNote()); note = &gStepsNotes.back(); memcpy(note->a, gSR.from, sizeof note->a); memcpy(note->b, gSR.to, sizeof note->b); }
    ++note->fails;
    Log("stairs: failed at block %zu of %zu (%s) after %.1f s (way %d)", gSR.idx + 1, gSR.pts.size() - 1, why, (gFrame - gSR.at) / 60.0, gSR.variant);
    if (note->fails >= 2 && MapReady() && gProbe != 41 && gProbe != 43) {
        const long long out = 10800LL << std::min(note->outs, 3);
        ++note->outs; note->fails = 0; note->outUntil = gFrame + out;
        gMap->nav->DisableLink(note->a, note->b, 700.f);
        if (gMap->wide) gMap->wide->DisableLink(note->a, note->b, 700.f);
        Log("stairs at %.0f %.0f %.0f left out of the paths %lld s", note->a[0], note->a[1], note->a[2], out / 60);
    }
    gSR.active = false; gF.active = false; ++gNavStats.failed;
}
// (v141) way 2, the careful climb (v141's KHB traces, probe 41): the climb starts when the action button and the stick
// toward the face come together from standing still, facing it within 8 degrees, the hips 0.45 to 0.69 m from the face;
// walking into the face from 0.65 m the player presses his back to it instead (the action button pressed while walking
// does nothing then). The blocks are 0.9 x 1 m, the railing's gap to the catwalk the last block's width. The side: the axis
// of the larger offset to the block (from the block it stands on: that block's middle); the face 0.45 m before the block's
// middle (on a block: 0.47 m past its own middle). On the floor first to 0.6 m out from the face; on a block, back to its
// middle when 0.25 m off it. Then still, turned by pulses (2 frames in 6, the stick full: the ladders' way), walked in
// to 0.64 m when the turn left it farther, still 3 frames, the press (the action button and the stick 3 frames, the stick
// 3 more), let go; not up 14 frames after: again from still; pressed flat: let go, turned again. 5 presses, 12 s a block
// the turn on the spot: the weapon readied (Square at 0x3F) and the stick, which turns the player without a step (docs 3);
// MGO1_STAIRS_TURN=pulse: pulses of the stick (2 frames in 6, the ladders' way; v141's trace: a 90 degree turn moved the
// player up to 0.2 m)
static bool TurnReadied() {
    static int mode = -1;
    if (mode < 0) { const char* e = getenv("MGO1_STAIRS_TURN"); mode = e && !strcmp(e, "ready") ? 1 : 0; }   // (v141 probe 41: readied, the stick walks him into the face)
    return mode == 1;
}
static float FaceDist() { return gSR.c[1] - (gMe.pos[0] * gSR.u[0] + gMe.pos[2] * gSR.u[1]); }   // (c[1]: the face along u)
static void StepsCareful(const float* t, float ground, float d) {
    const bool onBlock = gSR.idx > 0 && std::fabs(ground - gSR.pts[gSR.idx - 1][1]) < 350.f;
    auto phase = [](int p) { gSR.ph = p; gSR.phAt = gFrame; gSR.bestAt = gFrame; gSR.upSeen = false; gSR.stillFor = 0; gSR.pressAt = -1; };
    if (gSR.uIdx != gSR.idx) {                           // a new block: its side, where it is climbed from
        const float* o = onBlock ? gSR.pts[gSR.idx - 1].data() : gMe.pos;
        const float vx = t[0] - o[0], vz = t[2] - o[2];
        if (std::fabs(vx) >= std::fabs(vz)) { gSR.u[0] = vx >= 0 ? 1.f : -1.f; gSR.u[1] = 0.f; } else { gSR.u[0] = 0.f; gSR.u[1] = vz >= 0 ? 1.f : -1.f; }
        // c[0], c[2]: where it starts from (the launch point, or the middle of the block it stands on); c[1]: the face along u
        if (onBlock) {                                   // (its middle: 0.47 m from the face; v141 probe 41: pressed from 0.65 m on a
            const float* m = gSR.pts[gSR.idx - 1].data(); //  block, the player pressed his back to the face instead)
            gSR.c[1] = m[0] * gSR.u[0] + m[2] * gSR.u[1] + 470.f;
            gSR.c[0] = m[0]; gSR.c[2] = m[2];
        } else {
            gSR.c[1] = t[0] * gSR.u[0] + t[2] * gSR.u[1] - 450.f;
            gSR.c[0] = t[0] - gSR.u[0] * 1100.f; gSR.c[2] = t[2] - gSR.u[1] * 1100.f;   // (0.65 m from the face)
        }
        gSR.uIdx = gSR.idx; gSR.tries = 0; gSR.stepAt = gFrame;
        phase(0);
    }
    if (gFrame - gSR.stepAt > 720) { StepsDone(false, "not up in 12 s"); return; }
    Posture(AREA_STAND);
    const long long pt = gFrame - gSR.phAt;
    const float face = std::atan2(gSR.u[0], gSR.u[1]), off = std::fabs(Wrap(face - Facing()));
    const float df = FaceDist();
    const uint32_t st0 = gMe.bits[0] & 0xFFFF;
    const float kDeg = 3.14159265f / 180.f;
    switch (gSR.ph) {
    case 0: {                                            // to the launch point
        const float dc = Dist2D(gMe.pos, gSR.c);
        if (dc > (onBlock ? 150.f : 200.f) && pt < 360) { Steer(gSR.c[0] - gMe.pos[0], gSR.c[2] - gMe.pos[2], dc < 900.f ? kWalkPush : 1.f); return; }
        Steer(0, 0, 0);
        phase(1);
        return;
    }
    case 1:                                              // still, turned to the side: the weapon readied, the stick (no step)
        Steer(0, 0, 0);
        if (off > 5.f * kDeg && pt < 150) {
            if (TurnReadied()) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; Steer(gSR.u[0], gSR.u[1], 0.75f); }
            else if (gFrame % 6 < 2) Steer(gSR.u[0], gSR.u[1], 1.f);   // (MGO1_STAIRS_TURN=pulse: pulses, the ladders' way)
            return;
        }
        if (pt < 8 || ((gMe.bits[1] & 0x4000) && pt < 60)) return;   // (the weapon lowered first: the readied flag off)
        phase(2);
        return;
    case 2: {                                            // walked in when farther than 0.66 m, still, the press
        if (PressedFlat()) {
            ++gSR.tries;
            if (gDebug) Log("stairs: block %zu not climbed (press %d: pressed flat, %.0f from the face)", gSR.idx + 1, gSR.tries, df);
            if (gSR.tries >= 5) { StepsDone(false, "not up in 5 presses"); return; }
            phase(3);
            return;
        }
        if (gSR.pressAt < 0) {
            if (off > 12.f * kDeg) { phase(1); return; }  // (turned off the side meanwhile)
            // farther than 0.7 m: walked in, the action button 3 frames in 6 (a climb on the way, else pressed flat 0.47 m
            // from the face: in reach once turned again)
            if (df > 700.f && pt < 90) { Steer(gSR.u[0], gSR.u[1], 0.62f); if (gFrame & 1) gPad.buttons |= 0x1000; gSR.stillFor = 0; return; }   // (a slow walk, a press every other frame)
            Steer(0, 0, 0);
            gSR.stillFor = st0 == 3 ? gSR.stillFor + 1 : 0;
            if (gSR.stillFor < 3 && pt < 120) return;
            gSR.pressAt = gFrame;
            if (gDebug) Log("stairs: block %zu: the press %d, %.0f from the face, facing %.0f degrees off", gSR.idx + 1, gSR.tries + 1, df, off / kDeg);
        }
        const long long pp = gFrame - gSR.pressAt;
        if (pp < 3) { Steer(gSR.u[0], gSR.u[1], kWalkPush); gPad.buttons |= 0x1000; return; }
        if (pp < 6) { Steer(gSR.u[0], gSR.u[1], kWalkPush); return; }
        Steer(0, 0, 0);
        if (pp < 20) return;
        ++gSR.tries;                                     // (not up: again from still)
        if (gDebug) Log("stairs: block %zu not climbed (press %d: nothing, %.0f from the face)", gSR.idx + 1, gSR.tries, df);
        if (gSR.tries >= 5) { StepsDone(false, "not up in 5 presses"); return; }
        phase(off > 8.f * kDeg ? 1 : 2);
        return;
    }
    default:                                             // 3: off the wall: the stick let go, back to the launch point, turned again
        Steer(0, 0, 0);                                  // (turned on the spot pressed to it, each pulse pressed him again: v141 probe 41)
        if (pt < 10) return;
        phase(1);                                        // (v141 probe 41: back to the launch point first did no better)
        return;
    }
}
// (v141 beta 3) a climb link (a ledge, a box: AREA_CLIMB) climbed as the stairs' blocks (StepsCareful): the face found on
// the map's collision along the link at 0.5 m over the lower floor; to 0.64 m out from it, turned by pulses, the press
// from standing (Triangle and the stick together), pressed flat: turned again; 5 presses, 12 s. True while it has the
// pad (on the lower floor); up, or given up: false (FollowPath's walk on, its own failure rules)
// (v141 beta 3, the links bench) the climb under way: known by its ends, not by the path's corner number (a climb given up
// at corner 2 left every later path's climb at its corner 2 skipped, walked into: n021a's ledges); back to phase 1 at most
// twice from the press's wait, then pressed within 20 degrees
struct ClimbRun { size_t seg = (size_t)-1; float u[2] = {0, 0}, face = 0, launch[3] = {0, 0, 0}, a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
                  int ph = 0, presses = 0, stillFor = 0, turns = 0; long long phAt = 0, start = 0, pressAt = -1, lastAt = -100; bool upSeen = false, failed = false;
                  PressRun pr; int runs = 0; bool low = false; };   // ((v141 beta 6) the press run, the runs made, a low top walked onto)
static ClimbRun gCl;
static bool ClimbSame(const Waypoint& from, const Waypoint& to) {
    return gCl.seg != (size_t)-1 && Dist2D(gCl.a, from.p) < 300.f && Dist2D(gCl.b, to.p) < 300.f && std::fabs(gCl.a[1] - from.p[1]) < 300.f;
}
// (beta 3's: MGO1_CLIMB_WAY=b3)
static bool ClimbSegmentB3(const Waypoint& from, const Waypoint& to) {
    const float ground = gMe.pos[1] - 800.f;
    // (the same climb given up: again only after 20 s away from it, the path's own stuck rules meanwhile)
    if (ClimbSame(from, to) && gCl.failed && gFrame - gCl.lastAt < 1200) { gCl.lastAt = gFrame; return false; }
    if ((gMe.bits[2] & 0x2C00) || (gMe.bits[7] & 2)) {  // (a climb under way: nothing pressed)
        if (ClimbSame(from, to) && gCl.ph == 2 && !gCl.upSeen && (gMe.bits[2] & 0x400)) {
            gCl.upSeen = true;
            if (gDebug) Log("climb: up at %.0f %.0f %.0f (press %d, %.1f s)", from.p[0], from.p[1], from.p[2], gCl.presses + 1, (gFrame - gCl.start) / 60.0);
        }
        Steer(0, 0, 0); gF.lastProgress = gFrame;
        return true;
    }
    if (ground > from.p[1] + 350.f) return false;        // (up: the walk on to the top)
    if (!ClimbSame(from, to) || (gCl.failed && gFrame - gCl.lastAt >= 1200)) {
        gCl = ClimbRun(); gCl.seg = gF.next; gCl.start = gFrame; gCl.phAt = gFrame;
        memcpy(gCl.a, from.p, sizeof gCl.a); memcpy(gCl.b, to.p, sizeof gCl.b);
        const float vx = to.p[0] - from.p[0], vz = to.p[2] - from.p[2], n = std::sqrt(vx * vx + vz * vz);
        if (n < 1.f) { gCl.failed = true; gCl.lastAt = gFrame; return false; }
        gCl.u[0] = vx / n; gCl.u[1] = vz / n;
        // (the face: the first wall along the link from 0.4 m behind its start, then from 1.5 m behind; v141 beta 3's from 1.5 m
        // took a wall behind the start for it, n002a: pressed 1.4 m away from the ledge)
        float fd = 1e30f;
        for (const float from0 : {-400.f, -1500.f}) {
            for (float sAt = from0; sAt < n + 500.f; sAt += 25.f) {
                const float a[3] = {from.p[0] + gCl.u[0] * sAt, from.p[1] + 500.f, from.p[2] + gCl.u[1] * sAt};
                const float b[3] = {a[0] + gCl.u[0] * 25.f, a[1], a[2] + gCl.u[1] * 25.f};
                if (!gMap->nav->Clear(a, b)) { fd = sAt + 25.f; break; }
            }
            if (fd < 1e29f) break;
        }
        gCl.face = fd < 1e29f ? fd : n * 0.5f;
        gCl.launch[0] = from.p[0] + gCl.u[0] * (gCl.face - 640.f); gCl.launch[1] = from.p[1]; gCl.launch[2] = from.p[2] + gCl.u[1] * (gCl.face - 640.f);
        if (gDebug) Log("climb: at %.0f %.0f %.0f to %.0f %.0f %.0f, the face %.0f along%s", from.p[0], from.p[1], from.p[2], to.p[0], to.p[1], to.p[2], gCl.face,
                        fd < 1e29f ? "" : " (not found: halfway)");
    }
    gCl.lastAt = gFrame;
    if (gFrame - gCl.start > 720) { Log("climb: at %.0f %.0f %.0f not up in 12 s (phase %d, %d presses)", from.p[0], from.p[1], from.p[2], gCl.ph, gCl.presses); gCl.failed = true; return false; }
    gF.lastProgress = gFrame;                            // (its own clock: not FollowPath's stuck rules)
    Posture(AREA_STAND);
    const long long pt = gFrame - gCl.phAt;
    const float face = std::atan2(gCl.u[0], gCl.u[1]), off = std::fabs(Wrap(face - Facing()));
    const float df = gCl.face - ((gMe.pos[0] - from.p[0]) * gCl.u[0] + (gMe.pos[2] - from.p[2]) * gCl.u[1]);
    const float kDeg = 3.14159265f / 180.f;
    auto phase = [](int p) { gCl.ph = p; gCl.phAt = gFrame; gCl.stillFor = 0; gCl.pressAt = -1; gCl.upSeen = false; };
    switch (gCl.ph) {
    case 0: {
        const float dc = Dist2D(gMe.pos, gCl.launch);
        if (dc > 200.f && pt < 300) { Steer(gCl.launch[0] - gMe.pos[0], gCl.launch[2] - gMe.pos[2], dc < 900.f ? kWalkPush : 1.f); return true; }
        Steer(0, 0, 0); phase(1); return true;
    }
    case 1:
        Steer(0, 0, 0);
        if (off > 8.f * kDeg && pt < 150) { if (gFrame % 6 < 2) Steer(gCl.u[0], gCl.u[1], 1.f); return true; }
        if (pt < 8) return true;
        phase(2); return true;
    case 2: {
        if (PressedFlat()) {
            ++gCl.presses;
            if (gDebug) Log("climb: not up (press %d: pressed flat, %.0f from the face)", gCl.presses, df);
            if (gCl.presses >= 5) { Log("climb: at %.0f %.0f %.0f not up in 5 presses", from.p[0], from.p[1], from.p[2]); gCl.failed = true; return false; }
            phase(3); return true;
        }
        if (gCl.pressAt < 0) {
            if (off > (gCl.turns >= 2 ? 20.f : 12.f) * kDeg) { ++gCl.turns; phase(1); return true; }
            if (df > 700.f && pt < 90) { Steer(gCl.u[0], gCl.u[1], 0.62f); if (gFrame & 1) gPad.buttons |= 0x1000; gCl.stillFor = 0; return true; }
            Steer(0, 0, 0);
            gCl.stillFor = (gMe.bits[0] & 0xFFFF) == 3 ? gCl.stillFor + 1 : 0;
            if (gCl.stillFor < 3 && pt < 120) return true;
            gCl.pressAt = gFrame;
        }
        const long long pp = gFrame - gCl.pressAt;
        if (pp < 3) { Steer(gCl.u[0], gCl.u[1], kWalkPush); gPad.buttons |= 0x1000; return true; }
        if (pp < 6) { Steer(gCl.u[0], gCl.u[1], kWalkPush); return true; }
        Steer(0, 0, 0);
        if (pp < 20) return true;
        ++gCl.presses;
        if (gDebug) Log("climb: not up (press %d: nothing, %.0f from the face)", gCl.presses, df);
        if (gCl.presses >= 5) { Log("climb: at %.0f %.0f %.0f not up in 5 presses", from.p[0], from.p[1], from.p[2]); gCl.failed = true; return false; }
        phase(off > 8.f * kDeg ? 1 : 2); return true;
    }
    default:
        Steer(0, 0, 0);
        if (pt < 10) return true;
        phase(1); return true;
    }
}
// (v141 beta 6) a climb link by the press run (PressFrame): the face along the link from the collision (FaceAlong, up to the
// top's height), the launch 0.9 m out; 6 presses, 15 s. True while it has the pad (on the lower floor); up, or given up:
// false (FollowPath's walk on, its own failure rules). MGO1_CLIMB_WAY=b3: beta 3's (ClimbSegmentB3); MGO1_CLIMB_OLD: v137's
// ((v141 beta 6, the merged build's probe 43) a ledge that held the player short of its face: Brown Town's at x -22 to -25 m,
// 4 climbs of 9 failed so, "held short of the face", then the second run aside to the probe's 30 s. Handed over to beta 3's
// careful climb (its walk in, the action button pulsed) with MGO1_CLIMB_B3_HANDOVER=1 only: measured (climb4) it took none of
// them (5 presses for nothing each) and lost one the run aside made: off by default. The link handed over
static float gB3a[3], gB3b[3];
static bool gB3On = false;
static bool ClimbSegment(const Waypoint& from, const Waypoint& to) {
    static const bool on = !getenv("MGO1_CLIMB_OLD");
    static const bool b3 = getenv("MGO1_CLIMB_WAY") && !strcmp(getenv("MGO1_CLIMB_WAY"), "b3");
    if (!on || !MapReady()) return false;
    if (b3) return ClimbSegmentB3(from, to);
    if (gB3On) {
        if (Dist2D(gB3a, from.p) < 300.f && Dist2D(gB3b, to.p) < 300.f && std::fabs(gB3a[1] - from.p[1]) < 300.f) return ClimbSegmentB3(from, to);
        gB3On = false;                                   // (another link: the hand-over is over)
    }
    const float ground = gMe.pos[1] - 800.f;
    // ((merge review) a run cut a second or more (a fight's StopPath, a death, a way out): a new run, not the old one's clock and
    // phases resumed (it failed at once, "not up (15 s)", and counted toward the link's leaving)
    static long long lastCall = -100000;
    const bool cut = gFrame - lastCall > 60;
    lastCall = gFrame;
    // (the same climb given up: again only after 20 s away from it, the path's own stuck rules meanwhile)
    if (ClimbSame(from, to) && gCl.failed && gFrame - gCl.lastAt < 1200) { gCl.lastAt = gFrame; return false; }
    if (ClimbSame(from, to) && gCl.low) { gCl.lastAt = gFrame; return false; }
    if (ClimbingNow()) {                                 // (a climb under way: nothing pressed)
        if (ClimbSame(from, to) && !gCl.upSeen && !gCl.failed) {
            gCl.upSeen = true;
            if (gDebug) Log("climb: up at %.0f %.0f %.0f (press %d from %.0f, %.1f s)", from.p[0], from.p[1], from.p[2], gCl.pr.presses + 1, gCl.pr.pressDf,
                            (gFrame - gCl.start) / 60.0);
        }
        Steer(0, 0, 0); gF.lastProgress = gFrame;
        return true;
    }
    if (ground > from.p[1] + 350.f) return false;        // (up: the walk on to the top)
    if (!ClimbSame(from, to) || (gCl.failed && gFrame - gCl.lastAt >= 1200) || (cut && !gCl.failed)) {
        gCl = ClimbRun(); gCl.seg = gF.next; gCl.start = gFrame;
        memcpy(gCl.a, from.p, sizeof gCl.a); memcpy(gCl.b, to.p, sizeof gCl.b);
        const float n = Dist2D(from.p, to.p);
        if (n < 1.f) { gCl.failed = true; gCl.lastAt = gFrame; return false; }
        const float ux = (to.p[0] - from.p[0]) / n, uz = (to.p[2] - from.p[2]) / n;
        const float top[3] = {to.p[0], to.p[1] + 300.f, to.p[2]};
        const float topY0 = gMap->nav->GroundBelow(top, 1000.f), topY = topY0 > -1e29f ? topY0 : to.p[1];
        // (a top under 0.55 m over the floor: walked onto, FollowPath's walk in with the action button. Graniny Gorki Lab's
        // 0.45 m step: beta 3's walk 0.4 and 0.5 s, 4 of 4; the first links bench of beta 6 pressed from standing 0.28 to 0.51 m
        // from it, 24 presses, nothing, then FollowPath's walk up in 0.3 s; Svyatogornyj East's 0.65 m top was up by a press from 0.38 m)
        const float base[3] = {from.p[0], from.p[1] + 300.f, from.p[2]};
        const float fy0 = gMap->nav->GroundBelow(base, 1000.f), fy = fy0 > -1e29f ? fy0 : from.p[1];
        if (topY - fy < 550.f) {
            gCl.low = true; gCl.lastAt = gFrame;
            if (gDebug) Log("climb: at %.0f %.0f %.0f a low top (%.0f mm over the floor): walked onto", from.p[0], from.p[1], from.p[2], topY - fy);
            return false;
        }
        float nrm[3] = {0, 0, 0}, hit[3] = {0, 0, 0}, o[3] = {from.p[0], from.p[1], from.p[2]}, u[2] = {ux, uz};
        const float fa = FaceAlong(from.p, to.p, topY, nrm, hit);
        gCl.face = fa > -1e29f ? fa : n * 0.5f;
        const bool square = fa > -1e29f && SquareToFace(nrm, hit, from.p[1], o, u, &gCl.face);
        PressStart(gCl.pr, o, u[0], u[1], gCl.face, 900.f, "climb", false);
        gCl.pr.topH = topY - fy;
        if (gDebug) Log("climb: at %.0f %.0f %.0f to %.0f %.0f %.0f (its top %.0f), the face %.0f along%s", from.p[0], from.p[1], from.p[2], to.p[0], to.p[1], to.p[2], topY,
                        fa, fa > -1e29f ? (square ? " (pressed square to it)" : "") : " (none on the collision: halfway)");
    }
    gCl.lastAt = gFrame;
    gF.lastProgress = gFrame;                            // (its own clock: not FollowPath's stuck rules)
    const char* why = "";
    if (PressFrame(gCl.pr, &why) >= 0) return true;
    Log("climb: at %.0f %.0f %.0f not up (%s: %d presses, %d pressed flat, %d nothing; the face %s, %d pushes)%s", from.p[0], from.p[1], from.p[2], why, gCl.pr.presses,
        gCl.pr.flats, gCl.pr.nothings, gCl.pr.felt ? "felt" : "the collision's", gCl.pr.pushes, gCl.runs == 0 ? ": again 0.45 m aside if there is room" : "");
    // (v141 beta 6) another approach point: the same face from 0.45 m aside along it (something holding the player short of
    // the face, a part of the ledge the action button does not take), once
    if (gCl.runs++ == 0) {
        static const bool handOver = getenv("MGO1_CLIMB_B3_HANDOVER") != nullptr;
        if (handOver && !strcmp(why, "held short of the face")) {   // (beta 3's careful climb, its walk in: ClimbSegmentB3's own run)
            memcpy(gB3a, from.p, sizeof gB3a); memcpy(gB3b, to.p, sizeof gB3b); gB3On = true;
            gCl = ClimbRun();
            Log("climb: at %.0f %.0f %.0f held short of the face: the careful climb (MGO1_CLIMB_B3_HANDOVER: its walk in with the action button)", from.p[0], from.p[1], from.p[2]);
            return true;
        }
        const PressRun r = gCl.pr;
        for (const float side : {450.f, -450.f}) {           // (the side with walkable floor 0.6 m before the face)
            const float o[3] = {r.o[0] + r.u[1] * side, r.o[1], r.o[2] - r.u[0] * side};
            const float l[3] = {o[0] + r.u[0] * (r.face - 600.f), o[1], o[2] + r.u[1] * (r.face - 600.f)};
            if (!gMap->nav->Walkable(from.p, l)) continue;
            PressStart(gCl.pr, o, r.u[0], r.u[1], r.face, 900.f, "climb", false);
            gCl.pr.topH = r.topH;
            return true;
        }
    }
    gCl.failed = true;
    return false;
}
// (v141 beta 6) stairs' way 3: each block by the press run, its face from the collision along the block's side (the axis of
// the larger offset to it, as way 2) within 0.3 m of way 2's (0.45 m before the block's middle); from the floor the launch
// 0.9 m out, from a block the block's middle (no step back off it)
static void StepsPress(const float* t, float ground) {
    const bool onBlock = gSR.idx > 0 && std::fabs(ground - gSR.pts[gSR.idx - 1][1]) < 350.f;
    if (gSR.uIdx != gSR.idx) {
        const float* o = onBlock ? gSR.pts[gSR.idx - 1].data() : gMe.pos;
        const float vx = t[0] - o[0], vz = t[2] - o[2];
        float ux = 0.f, uz = 0.f;
        if (std::fabs(vx) >= std::fabs(vz)) ux = vx >= 0 ? 1.f : -1.f; else uz = vz >= 0 ? 1.f : -1.f;
        float org[3] = {t[0] - ux * 1200.f, ground, t[2] - uz * 1200.f};   // (the line: 1.2 m before the block's middle)
        float fa = FaceAlong(org, t, t[1]);
        // (no face there and the other offset at least half the larger: the other side. Mountaintop's blocks taken from a
        // corner, offsets 1.05 and 1.96 m, 1.35 and 1.3 m: no face on the larger's side, pressed 1.1 and 1.4 m aside of it)
        if (!(fa > -1e29f && std::fabs(fa - 750.f) < 300.f) && !onBlock && std::min(std::fabs(vx), std::fabs(vz)) > 0.5f * std::max(std::fabs(vx), std::fabs(vz))) {
            const float wx = uz != 0.f ? (vx >= 0 ? 1.f : -1.f) : 0.f, wz = ux != 0.f ? (vz >= 0 ? 1.f : -1.f) : 0.f;
            const float org2[3] = {t[0] - wx * 1200.f, ground, t[2] - wz * 1200.f};
            const float fa2 = FaceAlong(org2, t, t[1]);
            if (fa2 > -1e29f && std::fabs(fa2 - 750.f) < 300.f) {
                ux = wx; uz = wz; memcpy(org, org2, sizeof org); fa = fa2;
                if (gDebug) Log("stairs: block %zu taken from its other side (a face there)", gSR.idx + 1);
            }
        }
        const float face = fa > -1e29f && std::fabs(fa - 750.f) < 300.f ? fa : 750.f;
        const float launch = onBlock ? std::max(450.f, face - ((o[0] - org[0]) * ux + (o[2] - org[2]) * uz)) : 900.f;
        PressStart(gSR.pr, org, ux, uz, face, launch, "stairs", onBlock);
        gSR.pr.topH = t[1] - ground;
        gSR.uIdx = gSR.idx; gSR.stepAt = gFrame;
        if (gDebug) Log("stairs: block %zu, the face %.0f before its middle%s, the launch %.0f out", gSR.idx + 1, 1200.f - face, fa > -1e29f ? "" : " (way 2's)", launch);
    }
    if (gFrame - gSR.stepAt > 900) { StepsDone(false, "not up in 15 s"); return; }
    const char* why = "";
    if (PressFrame(gSR.pr, &why) < 0) StepsDone(false, why);
}
static void StepsSegment(const Waypoint& from, const Waypoint& to) {
    // ((merge review) a run cut a second or more (a fight, a death, a way out): a new one, not the old one's clock resumed)
    static long long lastCall = -100000;
    const bool cut = gFrame - lastCall > 60;
    lastCall = gFrame;
    if (!gSR.active || cut || Dist2D(gSR.from, from.p) > 300.f || Dist2D(gSR.to, to.p) > 300.f) {
        gSR = StepsRun(); gSR.active = true; gSR.at = gSR.bestAt = gFrame;
        memcpy(gSR.from, from.p, sizeof gSR.from); memcpy(gSR.to, to.p, sizeof gSR.to);
        bool rev = false;
        const NavMesh::Steps* s = MapReady() ? gMap->nav->StepsAt(from.p, to.p, &rev) : nullptr;
        if (!s && MapReady() && gMap->wide) s = gMap->wide->StepsAt(from.p, to.p, &rev);
        if (s) { gSR.pts = s->stones; if (rev) std::reverse(gSR.pts.begin(), gSR.pts.end()); }
        gSR.pts.push_back({to.p[0], to.p[1], to.p[2]});
        gSR.variant = StairsVariant(); ++gStairsTries;
        std::string st;
        for (size_t k = 0; k + 1 < gSR.pts.size(); ++k) { char b[48]; snprintf(b, sizeof b, " [%.0f %.0f %.0f]", gSR.pts[k][0], gSR.pts[k][1], gSR.pts[k][2]); st += b; }
        Log("stairs: %s from %.0f %.0f %.0f to %.0f %.0f %.0f by %zu blocks%s (way %d)", to.p[1] > from.p[1] ? "up" : "down", from.p[0], from.p[1], from.p[2], to.p[0], to.p[1], to.p[2],
            gSR.pts.size() - 1, st.c_str(), gSR.variant);
    }
    const float* t = gSR.pts[gSR.idx].data();
    const float ground = gMe.pos[1] - 800.f, d = Dist2D(gMe.pos, t);
    // (v141) MGO1_STAIRS_TRACE: every 3rd frame, the block aimed at, the player's state words, its facing, the way to the
    // block, the camera's, the stick and buttons of the frame before
    static const bool trace = getenv("MGO1_STAIRS_TRACE") != nullptr;
    if (trace && gFrame % 3 == 0)
        Log("stairs trace: way %d ph %d idx %zu d %.0f dy %.0f pos %.0f %.0f %.0f bits %x %x %x %x facing %.0f to %.0f asked %.0f cam %.0f right %.0f off %.0f stick %02x %02x pad %04x back %lld",
            gSR.variant, gSR.ph, gSR.idx, d, t[1] - ground, gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.bits[0], gMe.bits[2], gMe.bits[6], gMe.bits[7], Facing() * 57.2958f,
            AngleTo(gMe.pos, t) * 57.2958f, gSL.asked * 57.2958f, std::atan2(gCam.fwd[0], gCam.fwd[1]) * 57.2958f, std::atan2(gCam.right[0], gCam.right[1]) * 57.2958f,
            gSL.offset * 57.2958f, gLastPad.sticks[2], gLastPad.sticks[3], gLastPad.buttons, gSR.backAt >= 0 ? gFrame - gSR.backAt : -1LL);
    const bool last = gSR.idx + 1 >= gSR.pts.size();
    if (d < (last ? 500.f : 450.f) && std::fabs(ground - t[1]) < 350.f) {
        if (last) { StepsDone(true, ""); return; }
        ++gSR.idx; gSR.best = 1e30f; gSR.bestAt = gFrame; gSR.upSeen = false;
        return;
    }
    // (v141) a climb under way (the state word 2: 0x400 its move, 0x800 its end, 0x2000 a fall): the stick let go, nothing
    // pressed (the state word 0 is 2 then, as against a wall: v141's KHB audit, a climb's frames taken for its back pressed
    // to the block, the doll walked back meanwhile)
    // ((v141 beta 6) the word 0 at 0xC002 too: a low top's step up)
    if ((gMe.bits[2] & 0x2C00) || (gMe.bits[7] & 2) || (gSR.variant == 3 && ClimbingNow())) {
        if (gSR.variant == 2 && gSR.ph == 2 && !gSR.upSeen && (gMe.bits[2] & 0x400)) {
            gSR.upSeen = true;
            if (gDebug) Log("stairs: block %zu climbed (way 2 press %d, %.0f from the face, facing %.0f degrees off)", gSR.idx + 1, gSR.tries + 1, FaceDist(),
                            std::fabs(Wrap(std::atan2(gSR.u[0], gSR.u[1]) - Facing())) * 57.2958f);
        }
        if (gSR.variant == 3 && gSR.uIdx == gSR.idx && !gSR.upSeen) {
            gSR.upSeen = true;
            if (gDebug) Log("stairs: block %zu climbed (way 3 press %d from %.0f)", gSR.idx + 1, gSR.pr.presses + 1, gSR.pr.pressDf);
        }
        Steer(0, 0, 0); gSR.bestAt = gFrame; gSR.backAt = -1; return;
    }
    if (d < gSR.best - 50.f) { gSR.best = d; gSR.bestAt = gFrame; }
    const bool climb = t[1] - ground > 300.f;
    if (climb && gSR.variant == 2) { StepsCareful(t, ground, d); return; }
    if (climb && gSR.variant == 3) { gSR.bestAt = gFrame; StepsPress(t, ground); return; }
    if (gFrame - gSR.bestAt > 180) { StepsDone(false, "no nearer 3 s"); return; }
    Posture(AREA_STAND);
    // (its back pressed to the block, the action button does nothing: let go, a step back, again: as the vault's)
    if (gSR.backAt >= 0 && gFrame - gSR.backAt < 26) { if (gFrame - gSR.backAt < 10) Steer(0, 0, 0); else Steer(gMe.pos[0] - t[0], gMe.pos[2] - t[2], kWalkPush); return; }
    gSR.backAt = -1;
    if (climb && d < 1500.f && PressedFlat()) { gSR.backAt = gFrame; return; }
    if (climb && gSR.variant == 1) {                     // (v141) the square approach
        const bool onBlock = gSR.idx > 0 && std::fabs(ground - gSR.pts[gSR.idx - 1][1]) < 350.f;
        if (gSR.uIdx != gSR.idx) {                       // (the side: the axis of the larger offset to it, once a step)
            const float* o = onBlock ? gSR.pts[gSR.idx - 1].data() : gMe.pos;
            const float vx = t[0] - o[0], vz = t[2] - o[2];
            if (std::fabs(vx) >= std::fabs(vz)) { gSR.u[0] = vx >= 0 ? 1.f : -1.f; gSR.u[1] = 0.f; } else { gSR.u[0] = 0.f; gSR.u[1] = vz >= 0 ? 1.f : -1.f; }
            gSR.uIdx = gSR.idx; gSR.aligned = false; gSR.alignBest = 1e30f;
        }
        if (!gSR.aligned) {
            float p[3];
            if (onBlock) { memcpy(p, gSR.pts[gSR.idx - 1].data(), sizeof p); }   // (in the block's middle)
            else { p[0] = t[0] - gSR.u[0] * 1100.f; p[1] = ground; p[2] = t[2] - gSR.u[1] * 1100.f; }
            const float dp = Dist2D(gMe.pos, p);
            if (dp < gSR.alignBest - 50.f) { gSR.alignBest = dp; gSR.bestAt = gFrame; }   // (getting there: progress)
            if (dp > (onBlock ? 180.f : 250.f)) { Steer(p[0] - gMe.pos[0], p[2] - gMe.pos[2], onBlock ? 0.3f : kWalkPush); return; }
            gSR.aligned = true;
        }
        Steer(gSR.u[0], gSR.u[1], 0.5f);
        const bool square = std::fabs(Wrap(std::atan2(gSR.u[0], gSR.u[1]) - Facing())) < 25.f * 3.14159265f / 180.f;
        if (d < 1600.f && square && gFrame % 8 < 3) gPad.buttons |= 0x1000;
        return;
    }
    Steer(t[0] - gMe.pos[0], t[2] - gMe.pos[2], kWalkPush);
    const bool facing = std::fabs(Wrap(AngleTo(gMe.pos, t) - Facing())) < 40.f * 3.14159265f / 180.f;
    if (climb && d < 1500.f && facing && gFrame % 8 < 3) gPad.buttons |= 0x1000;
}

// (v137) a map's ladders and stairs as used, in the log when the map changes
static void LadderSummary() {
    std::string s;
    for (const auto& kv : gLadderNotes) {
        const LadderNote& n = kv.second;
        if (!n.up && !n.down && !n.failed && !n.waited && !n.avoided && !n.back) continue;
        char b[160];
        snprintf(b, sizeof b, "%s%03d up %d down %d failed %d waited %d a different way %d back %d", s.empty() ? "" : "; ", kv.first, n.up, n.down, n.failed, n.waited,
                 n.avoided, n.back);
        s += b;
    }
    if (!s.empty()) Log("ladders on %s: %s", gLadderPage.c_str(), s.c_str());
}
// (v137) every half second: the ladders and stairs out of the paths back after their time, each ladder's cost from the
// danger at its ends (a known enemy there or with a line to it: LadderThreat) and its "different way" time
static void LadderWatch() {
    if (!MapReady()) return;
    static const MapState* map = nullptr;
    if (map != gMap.get() || gLadderPage != gMap->key) {
        LadderSummary();
        map = gMap.get(); gLadderPage = gMap->key; gLadderNotes.clear(); gStepsNotes.clear(); gLO = LadderOn(); gSR = StepsRun();
    }
    if (gFrame % 30) return;
    for (StepsNote& n : gStepsNotes)
        if (n.outUntil >= 0 && gFrame >= n.outUntil) {
            n.outUntil = -1;
            gMap->nav->EnableLink(n.a, n.b, 700.f);
            if (gMap->wide) gMap->wide->EnableLink(n.a, n.b, 700.f);
            Log("stairs at %.0f %.0f %.0f: back in the paths", n.a[0], n.a[1], n.a[2]);
        }
    for (const Ladder& l : gMap->nav->Ladders()) {
        LadderNote& note = gLadderNotes[l.number];
        if (note.outUntil >= 0 && gFrame >= note.outUntil) {
            note.outUntil = -1;
            gMap->nav->EnableLadder(l.number);
            if (gMap->wide) gMap->wide->EnableLadder(l.number);
            Log("ladder %03d: back in the paths", l.number);
        }
        float where[3]; int who = -1; float ago = 0.f;
        const bool covered = LadderThreat(l.top, where, &who, &ago) > 0 || LadderThreat(l.foot, where, &who, &ago) > 0;
        const bool avoid = gFrame < note.avoidUntil;
        char why[96] = "clear";
        const Player* e = covered ? PlayerById(who) : nullptr;
        if (avoid) snprintf(why, sizeof why, "a different way a while");
        else if (covered) snprintf(why, sizeof why, "%s known by an end %.1f s ago", e ? e->name.c_str() : "an enemy", ago);
        LadderCost(l.number, note, avoid ? 8.f : covered ? 4.f : 1.f, why);
    }
}
static void LadderFail(const char* why, bool own) {
    const int n = gLR.l ? gLR.l->number : -1;
    LadderNote& note = gLadderNotes[n];
    ++note.failed;
    if (own) ++note.hard;
    Log("ladder %03d: failed %s (%s) after %.1f s%s", n, gLR.down ? "down" : "up", why, (gFrame - gLR.start) / 60.0, own ? "" : ", not its own");
    if (!own) { note.avoidUntil = gFrame + 1200; LadderCost(n, note, 8.f, "a different way a while"); }   // (not its own: a nudge, no time out)
    if (gLR.l && own && note.hard >= 2 && MapReady() && gProbe != 43) {
        const long long out = 10800LL << std::min(note.outs, 3);
        ++note.outs; note.hard = 0; note.outUntil = gFrame + out;
        gMap->nav->DisableLadder(*gLR.l);            // (the paths go without it a while, in both meshes)
        if (gMap->wide) gMap->wide->DisableLadder(n);
        Log("ladder %03d left out of the paths %lld s (twice %s)", n, out / 60, why);
    }
    if (!OnLadder()) gLO = LadderOn();
    gLR = LadderRun();
    gF.active = false; ++gNavStats.failed;
}
static void LadderSegment(const Waypoint& from, const Waypoint& to) {
    if (!gLR.l || gLR.seg != gF.next) {
        gLR = LadderRun(); gLR.seg = gF.next; gLR.at = gLR.start = gFrame; gLR.life0 = gMe.life;
        bool atTop = false;
        gLR.l = gMap->nav->LadderAt(from.p, 1500.f, &atTop);
        gLR.down = to.p[1] < from.p[1];
        if (!gLR.l) { Log("ladder: none at %.0f %.0f %.0f", from.p[0], from.p[1], from.p[2]); ++gF.next; return; }
        Log("ladder %03d: %s, %.1f m (its cost x%.0f)", gLR.l->number, gLR.down ? "down" : "up", std::fabs(to.p[1] - from.p[1]) / 1000.f, gLadderNotes[gLR.l->number].k);
        // (v141 beta 6) on it at its top, getting off from the last climb up (beta 3's links bench: a path back down the ladder
        // just climbed began during its top's exit, then "back where it began", its cost x8: 9 times): off it first
        gLR.waitOff = OnLadder() && gLR.down && std::fabs(gMe.pos[1] - 800.f - from.p[1]) < 900.f;
    }
    const Ladder& l = *gLR.l;
    LadderNote& note = gLadderNotes[l.number];
    const float* end = gLR.down ? l.foot : l.top;
    if (gMe.life < gLR.life0) gLR.hurt = true;
    const float* box = gLR.down ? l.top : l.foot;
    const float face = gLR.down ? Wrap(l.dir + 3.14159265f) : l.dir;
    const long long t = gFrame - gLR.at;
    if (gLR.waitOff) {
        if (OnLadder() && gFrame - gLR.start < 300) { Steer(0, 0, 0); gLR.at = gFrame; Act("climbing"); return; }
        gLR.waitOff = false; gLR.at = gFrame;
    }
    if (OnLadder() && gLR.phase < 3) { gLR.phase = 3; gLR.at = gFrame; gLadderDown = gLR.down; Act("climbing"); }
    switch (gLR.phase) {
    case 0: {                                        // into the middle of its volume
        const float d = Dist2D(gMe.pos, box);
        // (v137) another player on this ladder or in its volume: waited for 1.2 m off it, 10 s at most (then the path
        // given up, the ladder not blamed); not shouldered aside
        bool busy = false;
        for (const Player& p : gPlayers) {
            if (p.local || !p.alive) continue;
            const bool onIt = (p.bits[2] & 0x40000) && (Dist2D(p.pos, l.foot) < 2500.f || Dist2D(p.pos, l.top) < 2500.f);
            if (onIt || (Dist2D(p.pos, box) < 700.f && std::fabs(p.pos[1] - gMe.pos[1]) < 1500.f)) busy = true;
            if (Dist2D(p.pos, box) < 1500.f && std::fabs(p.pos[1] - gMe.pos[1]) < 1500.f) gLR.crowded = true;   // (v137: a failure then not its own)
        }
        if (busy) {
            if (gLR.busySince < 0) { gLR.busySince = gFrame; if (gDebug) Log("ladder %03d: a player on it: waiting", l.number); }
            if (gFrame - gLR.busySince > 600) { LadderFail("a player on it 10 s", false); return; }
            gLR.at = gFrame;
            if (d < 1200.f) { Steer(0, 0, 0); return; }
        } else gLR.busySince = -1;
        // (v137) its other end covered (LadderThreat): waited by it 6 s (an ally coming, the enemy gone, the grenade logic's
        // moment), then on with an ally by it, carrying the objective or back within 20 s of a different way; else a different way
        if (gLR.waitSince != -2 && d < 2500.f) {
            float where[3]; int who = -1; float ago = 0.f;
            if (LadderThreat(end, where, &who, &ago) > 0) {
                bool ally = false;
                for (const Player& p : gPlayers)
                    if (!p.local && p.alive && Ally(p) && Dist2D(p.pos, box) < 6000.f && std::fabs(p.pos[1] - gMe.pos[1]) < 2000.f) ally = true;
                const bool again = gFrame < note.avoidUntil, carrying = CarryingObjective();
                if (gLR.waitSince < 0) {
                    gLR.waitSince = gFrame; ++note.waited;
                    const Player* e = PlayerById(who);
                    Log("ladder %03d: its %s covered by %s (known %.1f s ago, %.0f m from it): waiting by it", l.number, gLR.down ? "foot" : "top", e ? e->name.c_str() : "an enemy",
                        ago, Dist2D(where, end) / 1000.f);
                }
                if (gFrame - gLR.waitSince < kLadderWait && !ally && !again && !carrying) { Steer(0, 0, 0); gLR.at = gFrame; Act("climbing"); return; }
                if (ally || again || carrying) {
                    Log("ladder %03d: %s all the same after %.1f s (%s)", l.number, gLR.down ? "down" : "up", (gFrame - gLR.waitSince) / 60.0,
                        ally ? "an ally by it" : carrying ? "carrying the objective" : "no better way");
                    gLR.waitSince = -2; gLR.at = gFrame; gLR.committed = true;
                } else {
                    ++note.avoided; note.avoidUntil = gFrame + 1200;
                    LadderCost(l.number, note, 8.f, "a different way a while");
                    Log("ladder %03d: still covered after %.1f s: a different way (its cost x8 20 s)", l.number, (gFrame - gLR.waitSince) / 60.0);
                    gLR = LadderRun(); gF.active = false;
                    return;
                }
            } else if (gLR.waitSince >= 0) {
                Log("ladder %03d: clear after %.1f s", l.number, (gFrame - gLR.waitSince) / 60.0);
                gLR.waitSince = -2; gLR.at = gFrame;
            }
        }
        if (d > 300.f) {
            if (t > 300) { LadderFail("its volume not reached", !gLR.hurt && !gLR.crowded); return; }
            Steer(box[0] - gMe.pos[0], box[2] - gMe.pos[2], d < 1200.f ? kWalkPush : 1.f);   // (v137: walked in, not run round)
            return;
        }
        gLR.phase = 1; gLR.at = gFrame; gLR.stillSince = -1;
        Steer(0, 0, 0);
        return;
    }
    case 1: {                                        // still, facing it (short full pushes turn a standing player)
        const float err = Wrap(face - Facing());
        Steer(0, 0, 0);
        if (std::fabs(err) > 12.f * 3.14159265f / 180.f && t < 90) {
            if (gFrame % 6 < 2) Steer(std::sin(face), std::cos(face), 1.f);
            gLR.stillSince = -1;
            return;
        }
        if (gLR.stillSince < 0) gLR.stillSince = gFrame;
        if (gFrame - gLR.stillSince < 10) return;
        gPad.buttons |= 0x1000;                      // the action button (4 frames)
        gLR.phase = 2; gLR.at = gFrame;
        return;
    }
    case 2:                                          // taken?
        Steer(0, 0, 0);
        if (t < 4) gPad.buttons |= 0x1000;
        if (t > 60) {
            if (++gLR.tries >= 3) { LadderFail("not taken", !gLR.hurt && !gLR.crowded); return; }
            gLR.phase = 0; gLR.at = gFrame;
        }
        return;
    case 3: {                                        // on it: up (down) until off it at the other end
        if (OnLadder()) {
            gLR.offSince = -1;
            if (gLR.onAt < 0) {
                gLR.onAt = gFrame;
                gLO = LadderOn(); gLO.number = l.number; gLO.fromY = from.p[1]; gLO.toY = to.p[1]; gLO.back = gLR.committed;   // (committed: no way back)
                if (gDebug) Log("ladder %03d: on it after %.1f s (%d tries)", l.number, (gFrame - gLR.start) / 60.0, gLR.tries + 1);
            }
            if (!gLR.back && !gLR.committed && LadderBack(l, from.p[1], to.p[1], gLR.down)) { gLR.back = gLO.back = true; ++note.back; }
            gLadderDown = gLR.down != gLR.back;
            gPad.sticks[3] = gLadderDown ? 0xFF : 0x00;
            Act("climbing");
            if (t > 900) LadderFail("too long on it", false);
            return;
        }
        if (gLR.offSince < 0) gLR.offSince = gFrame;
        Steer(0, 0, 0);
        if (gFrame - gLR.offSince < 10) return;
        const bool there = std::fabs(gMe.pos[1] - 800.f - to.p[1]) < 900.f;
        if (!there) {                                // (fallen off, or back: planned again)
            const bool home = std::fabs(gMe.pos[1] - 800.f - from.p[1]) < 900.f;
            if (gLR.back && home) {
                note.avoidUntil = gFrame + 1200;
                LadderCost(l.number, note, 8.f, "a different way a while");
                Log("ladder %03d: back at its %s in %.1f s (its cost x8 20 s)", l.number, gLR.down ? "top" : "foot", (gFrame - gLR.start) / 60.0);
                gLO = LadderOn(); gLR = LadderRun(); gF.active = false;
                return;
            }
            LadderFail(home ? "back where it began" : "off it elsewhere", false);
            return;
        }
        ++(gLR.down ? note.down : note.up);
        gLO = LadderOn();                            // (off it: no back rule on a stale climb)
        Log("ladder %03d: %s in %.1f s (on it %.1f s)", l.number, gLR.down ? "down" : "up", (gFrame - gLR.start) / 60.0, gLR.onAt >= 0 ? (gFrame - gLR.onAt) / 60.0 : 0.0);
        // (v137) the likely threat faced first (LadderExitLook: the fight's pre-aim set too); at a top, only ahead of the edge
        float look[3];
        if (LadderExitLook(to.p, look)) {
            gLR.look = AngleTo(gMe.pos, look);
            if (gLR.down || std::fabs(Wrap(gLR.look - l.dir)) < 1.75f) {
                if (gDebug) Log("ladder %03d: off it, faced toward %.0f %.0f %.0f first", l.number, look[0], look[1], look[2]);
                gLR.phase = 4; gLR.at = gFrame;
                return;
            }
        }
        gLR = LadderRun();
        ++gF.next; gF.best = 1e30f; gF.lastProgress = gFrame;
        if (gF.next >= gF.path.size()) { gF.active = false; ++gNavStats.reached; }
        return;
    }
    default: {                                       // (v137) off it at the far end: the likely threat faced, 24 frames at most
        const float err = Wrap(gLR.look - Facing());
        Steer(0, 0, 0);
        if (std::fabs(err) > 15.f * 3.14159265f / 180.f && t < 24) { if (gFrame % 6 < 2) Steer(std::sin(gLR.look), std::cos(gLR.look), 1.f); return; }
        gLR = LadderRun();
        ++gF.next; gF.best = 1e30f; gF.lastProgress = gFrame;
        if (gF.next >= gF.path.size()) { gF.active = false; ++gNavStats.reached; }
        return;
    }
    }
}
// on a ladder out of a path (a fight began, the path was dropped): the climb goes on the same way; (v137) back the way
// it came when its other end is covered (LadderBack). Each brain frame: the ladders' watch
bool LadderFrame() {
    LadderWatch();
    if (!OnLadder()) return false;
    if (gF.active && gF.next > 0 && gF.next < gF.path.size() && gF.path[gF.next - 1].link && gF.path[gF.next - 1].area == AREA_LADDER) return false;
    if (!gLO.back && gLO.number >= 0 && MapReady())
        for (const Ladder& l : gMap->nav->Ladders())
            if (l.number == gLO.number && LadderBack(l, gLO.fromY, gLO.toY, gLO.toY < gLO.fromY)) {
                gLO.back = true; gLadderDown = !(gLO.toY < gLO.fromY); ++gLadderNotes[l.number].back;
            }
    gPad.sticks[3] = gLadderDown ? 0xFF : 0x00;
    Act("climbing");
    return true;
}

// ---- hanging from a ledge (v137, docs/PLAYABLE_DOLL.md 2: the state word 2 has 0x10000000): the inputs of
// kHangOrder tried in turn, 1.5 s each, until it ends; up or dropped told by the hips a second later ----
enum HangInput { HANG_TRIANGLE, HANG_STICK_UP, HANG_CROSS, HANG_STICK_AWAY };
static const HangInput kHangOrder[] = {HANG_TRIANGLE, HANG_STICK_UP, HANG_CROSS, HANG_STICK_AWAY};   // (the order tried)
static const char* const kHangName[] = {"Triangle", "the stick up", "Cross", "the stick away"};      // (by HangInput)
static const int kHangSteps = (int)(sizeof kHangOrder / sizeof kHangOrder[0]);
static const long long kHangStep = 90;               // frames an input
struct HangRun {
    long long since = -1, stepAt = 0, offAt = -1, endAt = -1, endedInto = 0;
    int onRun = 0, step = 0, cycles = 0, ended = -1;
    float hips = 0.f, face = 0.f, at[3] = {0, 0, 0}, lo = 0.f, hi = 0.f;
};
static HangRun gHang;
long long gHangOverAt = -100000;                     // (a hang's end: nothing learned 5 s after it)
// every frame in a match: true while it has the pad (alive, the brain's frame, hanging)
static bool HangFrame(bool brain) {
    if (!gMe.alive) {
        if (gHang.since >= 0) Log("hang: over after %.1f s: dead", (gFrame - gHang.since) / 60.0);
        gHang = HangRun();
        return false;
    }
    const bool bit = (gMe.bits[2] & 0x10000000) != 0 && !OnLadder();
    if (gHang.endAt >= 0) {                              // (over: judged a second later; on it again where it hung: the same hang)
        if (bit && std::fabs(gMe.pos[1] - gHang.hips) < 400.f && Dist2D(gMe.pos, gHang.at) < 1500.f) { gHang.endAt = gHang.offAt = -1; }
        else {
            gHang.lo = std::min(gHang.lo, gMe.pos[1]); gHang.hi = std::max(gHang.hi, gMe.pos[1]);
            if (gFrame - gHang.endAt < 60) return false;
            const char* how = Dist2D(gMe.pos, gHang.at) > 3000.f ? "elsewhere" : gHang.hi > gHang.hips + 600.f ? "up" : gHang.lo < gHang.hips - 400.f ? "dropped" : "let go";
            Log("hang: over after %.1f s, %s (%s %.1f s into it; the hips %.0f, then %.0f to %.0f)", (gHang.endAt - gHang.since) / 60.0, how,
                gHang.ended >= 0 ? kHangName[gHang.ended] : "nothing", gHang.endedInto / 60.0, gHang.hips, gHang.lo, gHang.hi);
            gHang = HangRun();
        }
    }
    if (gHang.since < 0) {                               // (30 frames on: a hang)
        gHang.onRun = bit && brain ? gHang.onRun + 1 : 0;
        if (gHang.onRun < 30) return false;
        gHang.since = gFrame - gHang.onRun; gHang.stepAt = gFrame; gHang.step = 0;
        gHang.hips = gMe.pos[1]; gHang.face = Facing(); memcpy(gHang.at, gMe.pos, sizeof gHang.at);
        std::string order;
        for (int k = 0; k < kHangSteps; ++k) order += std::string(k ? ", " : "") + kHangName[kHangOrder[k]];
        Log("hang: hanging from a ledge at %.0f %.0f %.0f (state %x %x %x, stamina %d): %s in turn, %.1f s each", gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.bits[0],
            gMe.bits[2], gMe.bits[6], gMe.stun, order.c_str(), kHangStep / 60.0);
        RadioStop("hanging from a ledge");               // (no call's presses meanwhile)
        StopPath();                                      // (its crossing's timers from before it not counted on)
    }
    // (off a moment: the input goes on; off 20 frames, or moved: over)
    if (!bit && gHang.offAt < 0) { gHang.offAt = gFrame; gHang.ended = kHangOrder[gHang.step]; gHang.endedInto = gFrame - gHang.stepAt; }
    if (bit) gHang.offAt = -1;
    if (!brain || (gHang.offAt >= 0 && (gFrame - gHang.offAt >= 20 || std::fabs(gMe.pos[1] - gHang.hips) > 500.f || Dist2D(gMe.pos, gHang.at) > 1500.f))) {
        gHang.endAt = gHang.offAt >= 0 ? gHang.offAt : gFrame;
        gHang.lo = gHang.hi = gMe.pos[1];
        gHangOverAt = gFrame;
        return false;
    }
    BrainWatchFrame();                                   // (its sight, its hurts, the lives, the level: kept up)
    gControl = true;
    if (gHang.offAt >= 0) { Act("escaping"); return true; }   // (off it a moment: the pad let go while it is judged)
    if (gFrame - gHang.stepAt >= kHangStep) {
        gHang.stepAt = gFrame;
        if (++gHang.step >= kHangSteps) { gHang.step = 0; ++gHang.cycles; }
        if (gDebug) Log("hang: %s (%.1f s on it, every input tried %d times)", kHangName[kHangOrder[gHang.step]], (gFrame - gHang.since) / 60.0, gHang.cycles);
    }
    Act("escaping");
    const long long t = gFrame - gHang.stepAt;
    switch (kHangOrder[gHang.step]) {
    case HANG_TRIANGLE: if (t % 15 < 4) gPad.buttons |= 0x1000; break;
    case HANG_CROSS: if (t % 15 < 4) gPad.buttons |= 0x4000; break;
    case HANG_STICK_UP:                                  // (the stick's up, then its facing through the camera)
        if (t < kHangStep / 2) gPad.sticks[3] = 0x00; else Steer(std::sin(gHang.face), std::cos(gHang.face), 1.f);
        break;
    case HANG_STICK_AWAY:
        if (t < kHangStep / 2) gPad.sticks[3] = 0xFF; else Steer(-std::sin(gHang.face), -std::cos(gHang.face), 1.f);
        break;
    }
    return true;
}

// ---- crossings (doll_nav.h: a fence, a low wall, a railing, the lab's windows between two floors).
// Measured (probe 24, Svyatogornyj East, Graniny Gorki Lab, City Under Siege): walking into the obstacle
// with the action button (Triangle) pressed climbs over tops of 0.7 to 1.5 m, both ways, in about 20
// frames; a roll (Cross while running at it) goes over the low ones (0.7, 0.8 m) and carries on a metre
// or two; neither goes through a window's hole under a lintel (no link there). The doll rolls over the
// low ones (under 0.9 m) at its level 3 and up, seven times in ten, from a run-up of 1.5 m straight at
// it, the roll pressed 0.9 m before the obstacle; otherwise, and when a roll did not go over, the action
// button. Crossed: past the obstacle, on the other floor. Not crossed twice (three tries each): that link
// is left out of the paths (this map) ----
static std::map<long long, int> gCrossFails;         // a link's start (rounded to 0.1 m) -> its failures
// (a crossing's place to 0.4 m: the two meshes' links over one obstacle count together)
static long long CrossKey(const float* p) { return ((long long)std::lround(p[0] / 400.f) << 40) ^ ((long long)std::lround(p[2] / 400.f) << 16) ^ std::lround(p[1] / 400.f); }
float gCrossRollAt = 900.f;                          // (the roll pressed this far before the obstacle: probe 24 sets it)
static void CrossDone(bool ok, const Waypoint& from, const Waypoint& to, bool roll, const char* why) {
    const long long frames = gFrame - gCR.at;
    if (ok) {
        if (gDebug) Log("crossing: %s at %.0f %.0f %.0f crossed in %lld frames (tries %d)", roll ? "roll" : "vault", from.p[0], from.p[1], from.p[2], frames, gCR.tries + 1);
        gCR = CrossRun();
        ++gF.next; gF.best = 1e30f; gF.lastProgress = gFrame;
        if (gF.next >= gF.path.size()) { gF.active = false; ++gNavStats.reached; }
        return;
    }
    Log("crossing: %s at %.0f %.0f %.0f not crossed (%s)", roll ? "roll" : "vault", from.p[0], from.p[1], from.p[2], why);
    if (++gCrossFails[CrossKey(from.p)] >= 2 && MapReady() && gProbe != 43) {   // (both meshes: the path may be the wide one's)
        const bool a = gMap->nav->DisableLink(from.p, to.p, 700.f), b = gMap->wide && gMap->wide->DisableLink(from.p, to.p, 700.f);
        if (a || b) {
            Log("crossing: %s at %.0f %.0f %.0f left out of the paths", roll ? "roll" : "vault", from.p[0], from.p[1], from.p[2]);
            LearnWrite(FactLink(from.area == AREA_ROLL ? AREA_ROLL : AREA_VAULT, from.p, to.p));
        }
    }
    gCR = CrossRun();
    gF.active = false; ++gNavStats.failed;
}
static void CrossSegment(const Waypoint& from, const Waypoint& to, bool forceRoll) {
    if (!gCR.active || gCR.seg != gF.next) {
        gCR = CrossRun(); gCR.active = true; gCR.seg = gF.next; gCR.at = gCR.bestAt = gFrame;
        float top = MapReady() ? gMap->nav->CrossingTop(from.p, to.p) : -1.f;      // (the path's link: either mesh's)
        if (top < 0.f && MapReady() && gMap->wide) top = gMap->wide->CrossingTop(from.p, to.p);
        // (a roll carries on 2 to 4 m past the obstacle: open floor there, else the action button)
        const float l0 = std::max(1.f, Dist2D(from.p, to.p));
        const float vx = (to.p[0] - from.p[0]) / l0, vz = (to.p[2] - from.p[2]) / l0;
        // the run-up: up to 1.5 m before the edge, 0.3 m short of what is behind (a wall there: the doll
        // pressed its back to it and did not run, probe 24), 0.7 m at least; the landing: a metre of floor
        // past the far point at least (the roll carries on)
        const float behind = top >= 0.f ? gMap->nav->ClearAlong(from.p, -vx, -vz, 1800.f) : 0.f;
        const float landing = top >= 0.f ? gMap->nav->ClearAlong(to.p, vx, vz, 3000.f) : 0.f;
        gCR.runupLen = behind - 300.f >= 700.f ? std::min(1500.f, behind - 300.f) : 0.f;
        if (forceRoll && gCR.runupLen <= 0.f) gCR.runupLen = 1500.f;
        gCR.roll = forceRoll || (top >= 0.f && top <= 900.f && gCR.runupLen > 0.f && landing >= 1000.f && LevelShown() >= 2.5f && Rnd() % 10 < 7);
        gCR.top = top;
        if (gDebug) Log("crossing: %s from %.0f %.0f %.0f to %.0f %.0f %.0f (its top %.0f; level %.1f, %.0f clear behind, %.0f beyond)", gCR.roll ? "roll" : "vault",
                        from.p[0], from.p[1], from.p[2], to.p[0], to.p[1], to.p[2], top, LevelShown(), behind, landing);
    }
    const bool roll = gCR.roll;
    const float len = std::max(1.f, Dist2D(from.p, to.p));
    const float ux = (to.p[0] - from.p[0]) / len, uz = (to.p[2] - from.p[2]) / len;
    // how far along the crossing (0 at the near edge, len at the far floor's edge), how far aside
    const float along = (gMe.pos[0] - from.p[0]) * ux + (gMe.pos[2] - from.p[2]) * uz;
    const float aside = (gMe.pos[0] - from.p[0]) * uz - (gMe.pos[2] - from.p[2]) * ux;
    // across: past the obstacle, on the far floor (standing on it, not on the obstacle), near the line
    const bool there = along > len - 450.f && std::fabs(aside) < 900.f && std::fabs(gMe.pos[1] - 800.f - to.p[1]) < 450.f;
    const long long t = gFrame - gCR.at;
    if (there) { CrossDone(true, from, to, roll, ""); return; }
    if (gFrame - gCR.bestAt > 900) { CrossDone(false, from, to, roll, "too long"); return; }
    if (along > gCR.best + 100.f) { gCR.best = along; gCR.bestAt = gFrame; }
    // (v141 beta 6) MGO1_VAULT_PRESS=1: the vault by the press run first (PressFrame: the obstacle's face from the collision, the
    // press from standing: 6 presses, 15 s), then the walk in below (beta 3's, 3 tries); over the obstacle and on the far floor's
    // height past its face: crossed. Off: the first links bench of beta 6, Svyatogornyj East's fences, 8 presses from standing
    // 0.29 to 0.59 m from them did nothing, the walk in with the action button then crossed them in 32 and 35 frames
    static const bool vaultPress = getenv("MGO1_VAULT_PRESS") && *getenv("MGO1_VAULT_PRESS") == '1';
    if (!roll && vaultPress && gCR.pressRuns < 1) {
        if (!gCR.prOn) {
            float nrm[3] = {0, 0, 0}, hit[3] = {0, 0, 0}, o[3] = {from.p[0], from.p[1], from.p[2]}, u[2] = {ux, uz};
            const float fa = FaceAlong(from.p, to.p, from.p[1] + (gCR.top > 0.f ? gCR.top : 1000.f), nrm, hit);
            float face = fa > -1e29f ? std::min(fa, len - 200.f) : 375.f;
            const bool square = fa > -1e29f && SquareToFace(nrm, hit, from.p[1], o, u, &face);
            PressStart(gCR.pr, o, u[0], u[1], face, 900.f, "vault", false);
            gCR.prOn = true;
            if (gDebug) Log("crossing: vault at %.0f %.0f %.0f by the press, the obstacle's face %.0f along%s", from.p[0], from.p[1], from.p[2], fa,
                            fa > -1e29f ? (square ? " (pressed square to it)" : "") : " (none on the collision)");
        }
        gCR.bestAt = gFrame;                                       // (its own clock)
        const float alongPr = (gMe.pos[0] - gCR.pr.o[0]) * gCR.pr.u[0] + (gMe.pos[2] - gCR.pr.o[2]) * gCR.pr.u[1];   // (on the press's own line)
        if (alongPr > gCR.pr.face + 250.f && std::fabs(gMe.pos[1] - 800.f - to.p[1]) < 450.f && !ClimbingNow()) { CrossDone(true, from, to, roll, ""); return; }
        const char* why = "";
        if (PressFrame(gCR.pr, &why) >= 0) return;
        ++gCR.pressRuns; gCR.prOn = false;
        Log("crossing: vault at %.0f %.0f %.0f not over by the press (%s: %d presses, %d pressed flat, %d nothing)%s", from.p[0], from.p[1], from.p[2], why,
            gCR.pr.presses, gCR.pr.flats, gCR.pr.nothings, ": the walk in");
        gCR.phase = 0; gCR.at = gFrame; gCR.best = -1e30f; gCR.tries = 0;
        return;
    }
    if (!roll) {
        // the vault: straight at it (back onto the line first), walked over its last 1.5 m, the action button 3 frames
        // in 8 from 1.2 m before the edge, facing it; its back pressed to it (the state word 0 at 2) or no nearer for
        // 2 s: let go, a short step back walking, again (3 times). (v137: run at it with a press every quarter second,
        // the player met the obstacle between two presses and pressed its back to it, where the action button is not
        // pressed: 65 of 69 tries not over in v136's audit were so)
        Posture(AREA_STAND);
        if (gCR.phase == 1) {                                  // let go (off the obstacle), then the step back
            if (t < 10) { Steer(0, 0, 0); return; }
            Steer(-ux, -uz, kWalkPush);
            if (t > 26) { gCR.phase = 0; gCR.at = gFrame; gCR.bestAt = gFrame; }
            return;
        }
        Steer(ux - uz * aside / 800.f, uz + ux * aside / 800.f, along > -1500.f ? kWalkPush : 1.f);
        // (facing the obstacle only: sideways, the action button takes a ladder close by)
        const bool facing = std::fabs(Wrap(std::atan2(ux, uz) - Facing())) < 35.f * 3.14159265f / 180.f;
        if (along > -1200.f && facing && gFrame % 8 < 3) gPad.buttons |= 0x1000;
        const uint32_t st0 = gMe.bits[0] & 0xFFFF;
        const bool pressedTo = (st0 == 2 || st0 == 0x4002) && along > -1500.f && t > 20;
        if (pressedTo || gFrame - gCR.bestAt > 120) {
            if (++gCR.tries >= 3) { CrossDone(false, from, to, roll, "the action button did nothing"); return; }
            if (gDebug) Log("crossing: vault not over (%.0f along, %.0f aside, hips %.0f, %s): a step back, again", along, aside, gMe.pos[1],
                            pressedTo ? "its back to it" : "no nearer 2 s");
            gCR.phase = 1; gCR.at = gFrame; gCR.best = -1e30f;
        }
        return;
    }
    switch (gCR.phase) {
    case 0: {                                                  // the run-up's start: runupLen before the edge on the line
        const float r[3] = {from.p[0] - ux * gCR.runupLen, from.p[1], from.p[2] - uz * gCR.runupLen};
        memcpy(gCR.runup, r, sizeof r);
        const bool walkable = MapReady() && gMap->nav->Walkable(from.p, r);
        const float dr = Dist2D(gMe.pos, r);
        if (!walkable || dr < 250.f || along < -gCR.runupLen || t > 240) { gCR.phase = 1; gCR.at = gFrame; gCR.bestAt = gFrame; Steer(0, 0, 0); return; }
        Posture(AREA_STAND);
        Steer(r[0] - gMe.pos[0], r[2] - gMe.pos[2], dr < 700.f ? 0.45f : 1.f);   // (slower at the end: no step past it)
        return;
    }
    case 1: {                                                  // facing the window (short full pushes turn a standing player)
        const float face = std::atan2(ux, uz), err = Wrap(face - Facing());
        Steer(0, 0, 0);
        if (std::fabs(err) > 10.f * 3.14159265f / 180.f && t < 60) { if (gFrame % 6 < 2) Steer(ux, uz, 1.f); return; }
        gCR.phase = 2; gCR.at = gFrame; gCR.rollAt = -1;
        return;
    }
    case 2: {                                                  // the run, the roll this far before the obstacle (its face 375 mm past the edge)
        Steer(ux - uz * aside / 800.f, uz + ux * aside / 800.f, 1.f);
        const float toObstacle = 375.f - along;
        if (gCR.rollAt < 0 && toObstacle <= gCrossRollAt) { gCR.rollAt = gFrame; if (gDebug) Log("crossing: roll pressed %.0f mm before the obstacle (state %x)", toObstacle, gMe.bits[0]); }
        if (gCR.rollAt >= 0 && gFrame - gCR.rollAt < 4) gPad.buttons |= 0x4000;
        if ((gCR.rollAt >= 0 && gFrame - gCR.rollAt > 70) || t > 180) {   // over, not across: the action button (or again)
            if (gDebug) Log("crossing: roll ended %.0f along, %.0f aside, hips %.0f (state %x %x)", along, aside, gMe.pos[1], gMe.bits[0], gMe.bits[6]);
            if (++gCR.tries >= 3) { CrossDone(false, from, to, roll, "the roll did not go through"); return; }
            if (!forceRoll) { gCR.roll = false; gCR.phase = 0; gCR.at = gFrame; gCR.bestAt = gFrame; gCR.best = -1e30f; return; }
            gCR.phase = 0; gCR.at = gFrame;
        }
        return;
    }
    }
}
// (probe 24) a crossing taken now from where the doll stands: the path [from, to] through it
bool ForceCrossing(const float* a, const float* b, uint8_t area) {
    if (!MapReady()) return false;
    Waypoint w0{}, w1{};
    memcpy(w0.p, a, sizeof w0.p); w0.area = area; w0.link = true;
    memcpy(w1.p, b, sizeof w1.p); w1.area = AREA_STAND; w1.link = false;
    gF = Follow(); gF.path = {w0, w1}; gF.next = 1; gF.active = true; gF.since = gF.lastProgress = gFrame;
    memcpy(gF.goal, b, sizeof gF.goal); memcpy(gF.lastPos, gMe.pos, sizeof gF.lastPos);
    gCR = CrossRun(); gLR = LadderRun();
    return true;
}

// ---- the map knowledge of earlier games (LearnApply: once a map, its mesh ready): each fact applied as one
// learned in this game; a line skipped when its check, date or fields are bad, or older than 7 days ----
struct LearnFact { char kind = 0; uint8_t area = 0; float a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; int deg = 0, doll = 0; long long when = 0; };
static bool ReadTail(const std::string& path, long long maxBytes, std::string& out) {   // the file's last maxBytes
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}, pos{};
    if (!GetFileSizeEx(h, &size)) { CloseHandle(h); return false; }
    pos.QuadPart = size.QuadPart > maxBytes ? size.QuadPart - maxBytes : 0;
    out.assign((size_t)(size.QuadPart - pos.QuadPart), '\0');
    DWORD got = 0;
    const bool ok = out.empty() || (SetFilePointerEx(h, pos, nullptr, FILE_BEGIN) && ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr));
    CloseHandle(h);
    if (!ok) return false;
    out.resize(got);
    if (pos.QuadPart > 0) { const size_t nl = out.find('\n'); out.erase(0, nl == std::string::npos ? out.size() : nl + 1); }   // (a line cut)
    return true;
}
static bool ParseFact(const std::string& l, long long now, LearnFact& f, bool* old) {
    const size_t h = l.rfind(" #");
    unsigned chk = 0;
    if (h == std::string::npos || h + 6 != l.size() || sscanf(l.c_str() + h + 2, "%4x", &chk) != 1 || chk != LearnCheck(l.data(), h)) return false;
    int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0, doll = 0, used = 0;
    long long frame = 0;
    char kind[16] = {};
    if (sscanf(l.c_str(), "v1 %4d-%2d-%2dT%2d:%2d:%2dZ f%lld d%d %15s %n", &y, &mo, &d, &hh, &mi, &ss, &frame, &doll, kind, &used) != 9 || used <= 0 ||
        (size_t)used > h)
        return false;
    SYSTEMTIME st{};
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d; st.wHour = (WORD)hh; st.wMinute = (WORD)mi; st.wSecond = (WORD)ss;
    FILETIME ft;
    if (y < 2000 || y > 9999 || !SystemTimeToFileTime(&st, &ft)) return false;
    const long long when = FileTimeSeconds(ft);
    if (when > now + 86400) return false;                // (a date ahead)
    if (now - when > kLearnMaxAge) { *old = true; return false; }
    f.doll = doll; f.when = when;
    const std::string rest = l.substr((size_t)used, h - (size_t)used);
    char an[16] = {};
    int n = 0;
    if (!strcmp(kind, "link")) {
        if (sscanf(rest.c_str(), "%15s %f %f %f %f %f %f%n", an, &f.a[0], &f.a[1], &f.a[2], &f.b[0], &f.b[1], &f.b[2], &n) != 7) return false;
        f.kind = 'l';
        f.area = !strcmp(an, "vault") ? AREA_VAULT : !strcmp(an, "roll") ? AREA_ROLL : !strcmp(an, "climb") ? AREA_CLIMB : !strcmp(an, "drop") ? AREA_DROP : 0;
        if (!f.area) return false;
    } else if (!strcmp(kind, "trap")) {
        if (sscanf(rest.c_str(), "%f %f %f %f %f %f%n", &f.a[0], &f.a[1], &f.a[2], &f.b[0], &f.b[1], &f.b[2], &n) != 6) return false;
        f.kind = 't';
    } else if (!strcmp(kind, "escape")) {
        if (sscanf(rest.c_str(), "%f %f %f %d%n", &f.a[0], &f.a[1], &f.a[2], &f.deg, &n) != 4 || f.deg < 0 || f.deg >= 360) return false;
        f.kind = 'e';
    } else return false;
    if (n != (int)rest.size()) return false;            // (nothing after its fields)
    for (int k = 0; k < 3; ++k) if (!(std::fabs(f.a[k]) < 1e6f) || !(std::fabs(f.b[k]) < 1e6f)) return false;
    return true;
}
static void LearnApply() {
    gLearnKnown.clear();
    if (gLearnDir.empty() || !MapReady()) return;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const long long now = FileTimeSeconds(ft);
    int lines = 0, old = 0, bad = 0, gone = 0, links = 0, traps = 0, single = 0;
    std::set<int> pockets;
    std::string where;
    // (a trap told by one game alone may have been a player in a doorway: applied once two games, or two dolls, told it;
    // its tellers by its middle to 0.7 m: the doll's number and the game's ten minutes)
    std::map<long long, std::set<std::pair<int, long long>>> trapTellers;
    std::map<long long, LearnFact> trapFact;
    for (bool rotated : {true, false}) {
        std::string text;
        if (!ReadTail(LearnPath(gMap->key, rotated), 4 * kLearnCap, text)) continue;
        for (size_t s = 0; s < text.size();) {
            size_t e = text.find('\n', s);
            if (e == std::string::npos) e = text.size();
            std::string l = text.substr(s, e - s);
            s = e + 1;
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
            if (l.empty()) continue;
            ++lines;
            LearnFact f;
            bool tooOld = false;
            if (!ParseFact(l, now, f, &tooOld)) { ++(tooOld ? old : bad); continue; }
            if (f.kind == 'l') {
                if (!gLearnKnown.insert(FactLink(f.area, f.a, f.b)).second) continue;
                const bool a = gMap->nav->DisableLink(f.a, f.b, 700.f), b = gMap->wide && gMap->wide->DisableLink(f.a, f.b, 700.f);
                if (!a && !b) { ++gone; continue; }
                for (const float* p : {f.a, f.b}) { int& c = gCrossFails[CrossKey(p)]; c = std::max(c, 2); }
                if (++links <= 3) { char w[96]; snprintf(w, sizeof w, "%sthe %s at %.0f %.0f %.0f", links > 1 ? ", " : "", LinkName(f.area), f.a[0], f.a[1], f.a[2]); where += w; }
            } else if (f.kind == 't') {
                const float c[3] = {(f.a[0] + f.b[0]) * 0.5f, f.a[1], (f.a[2] + f.b[2]) * 0.5f};
                const long long key = ((long long)std::lround(c[0] / 700.f) << 40) ^ ((long long)std::lround(c[2] / 700.f) << 16) ^ std::lround(c[1] / 1500.f);
                trapTellers[key].insert({f.doll, f.when / 600});
                trapFact[key] = f;
            } else {
                const float angle = f.deg / 57.29578f;
                if (!gLearnKnown.insert(FactEscape(f.a, angle)).second) continue;
                const int c = gMap->nav->ComponentAt(f.a);
                if (c < 0 || gMap->nav->ComponentSize(c) > 6) { ++gone; continue; }
                gEscapeWay[c] = angle;                   // (the newest line last: it stands)
                pockets.insert(c);
            }
        }
    }
    for (const auto& tt : trapTellers) {
        std::set<int> dolls; std::set<long long> games;
        for (const auto& w : tt.second) { dolls.insert(w.first); games.insert(w.second); }
        if (dolls.size() < 2 && games.size() < 2) { ++single; continue; }
        const LearnFact& f = trapFact[tt.first];
        if (!gLearnKnown.insert(FactTrap(f.a, f.b)).second) continue;
        const float c[3] = {(f.a[0] + f.b[0]) * 0.5f, f.a[1], (f.a[2] + f.b[2]) * 0.5f};
        if (TrapNear(c)) continue;
        MeshTrap t;
        memcpy(t.a, f.a, sizeof t.a); memcpy(t.b, f.b, sizeof t.b); t.at = gFrame;
        gMeshTraps.push_back(t);
        ++traps;
    }
    if (!links && !traps && pockets.empty() && !(gDebug && lines)) return;
    char b[700];
    snprintf(b, sizeof b, "learned: %d links left out%s%s%s, %d traps of the mesh (%d told once, not yet), %zu ways out of a pocket, from earlier games (%s: %d lines, %d older than %lld days, %d bad, %d no longer on the mesh)",
             links, links ? " (" : "", where.c_str(), links > 3 ? ", ...)" : links ? ")" : "", traps, single, pockets.size(), gMap->key.c_str(), lines, old, kLearnMaxAge / 86400,
             bad, gone);
    Log("%s", b);
}

// stuck: the path planned again from here (3 times for a goal), then the goal given up
static void Replan(const float* corner, float d, bool wall = false) {
    Log("stuck at %.0f %.0f %.0f going to %.0f %.0f %.0f (corner %zu of %zu, %.0f away, area %u, hips %.0f, state %x %x %x %x)",
        gMe.pos[0], gMe.pos[1], gMe.pos[2], corner[0], corner[1], corner[2], gF.next, gF.path.size(), d, gF.path[gF.next - 1].area, HipHeight(),
        gMe.bits[0], gMe.bits[5], gMe.bits[6], gMe.bits[9]);
    const bool ground = !gF.path[gF.next - 1].link;
    if (wall && ground) AddMeshTrap(gMe.pos, corner);             // (the same wall three times: a trap of the mesh)
    // a climb or a drop not taken twice (v133's audit: a terrace edge on Mountaintop pressed into 8 times,
    // higher than the mesh said): that link left out of the paths, in both meshes
    const Waypoint& lf = gF.path[gF.next - 1];
    if (!ground && (lf.area == AREA_CLIMB || lf.area == AREA_DROP) && ++gCrossFails[CrossKey(lf.p)] >= 2 && MapReady() && gProbe != 43) {
        const bool a = gMap->nav->DisableLink(lf.p, corner, 700.f), b = gMap->wide && gMap->wide->DisableLink(lf.p, corner, 700.f);
        if (a || b) {
            Log("path: the %s at %.0f %.0f %.0f not taken twice: left out of the paths", lf.area == AREA_CLIMB ? "climb" : "drop", lf.p[0], lf.p[1], lf.p[2]);
            LearnWrite(FactLink(lf.area, lf.p, corner));
        }
    }
    float goal[3]; memcpy(goal, gF.goal, sizeof goal);
    const int replans = gF.replans;
    gTrip.haveVia = false; gTrip.avoid.clear();                   // (the shortest from here on)
    if (replans < 3 && PlanTo(goal)) {
        gF.replans = replans + 1; ++gNavStats.replans;
        gF.side = (gF.replans & 1) ? 1.f : -1.f; gF.sideUntil = gFrame + 28;   // first let go, then a step aside
        return;
    }
    if (ground && replans >= 3) AddMeshTrap(gMe.pos, corner);     // (planned again three times: that way left out)
    gF.active = false; gF.replans = 0; ++gNavStats.failed;
}

// (v141 beta 3) the path's state in a line (the brain's still watch)
std::string PathState() {
    char b[360];
    if (!gF.active || gF.next >= gF.path.size()) {
        snprintf(b, sizeof b, "no path (ladder %d phase %d, climb phase %d, crossing %d)", gLR.l ? gLR.l->number : -1, gLR.phase, gCl.seg != (size_t)-1 ? gCl.ph : -1, gCR.active ? 1 : 0);
        return b;
    }
    const Waypoint& w = gF.path[gF.next];
    const Waypoint& pv = gF.path[gF.next - 1];
    snprintf(b, sizeof b, "corner %zu of %zu at %.0f %.0f %.0f (%.1f m), from a %s of area %u, goal %.0f %.0f %.0f, ladder %d phase %d, climb phase %d, crossing %d, "
             "progress %.1f s ago, side %d, view %d, action %d", gF.next, gF.path.size(), w.p[0], w.p[1], w.p[2], Dist2D(gMe.pos, w.p) / 1000.f, pv.link ? "link" : "corner",
             pv.area, gF.goal[0], gF.goal[1], gF.goal[2], gLR.l ? gLR.l->number : -1, gLR.phase, gCl.seg != (size_t)-1 ? gCl.ph : -1, gCR.active ? 1 : 0,
             (gFrame - gF.lastProgress) / 60.0, gF.sideUntil > gFrame ? 1 : 0, gF.viewUntil > gFrame ? 1 : 0, gF.actionUntil > gFrame ? 1 : 0);
    return b;
}
void FollowPath() {
    if (!gF.active || gF.next >= gF.path.size()) { gF.active = false; Steer(0, 0, 0); return; }
    // a jump of metres since the last frame (a respawn, a new round): the path from here
    if (Dist2D(gMe.pos, gF.lastPos) > 2500.f && !OnLadder()) {
        float goal[3]; memcpy(goal, gF.goal, sizeof goal);
        if (gDebug) Log("moved %.0f m at once (a respawn?): the path planned again", Dist2D(gMe.pos, gF.lastPos) / 1000.f);
        gLR = LadderRun(); gTrip.haveVia = false; gTrip.avoid.clear();
        if (!PlanTo(goal)) { gF.active = false; Steer(0, 0, 0); return; }
    }
    memcpy(gF.lastPos, gMe.pos, sizeof gF.lastPos);
    const Waypoint& w = gF.path[gF.next];
    const Waypoint& prev = gF.path[gF.next - 1];
    if (prev.link && prev.area == AREA_LADDER) { LadderSegment(prev, w); return; }
    if (prev.link && prev.area == AREA_STEPS) { StepsSegment(prev, w); return; }   // (v137: stairs of blocks)
    if (prev.link && prev.area == AREA_CLIMB && ClimbSegment(prev, w)) return;     // (v141 beta 3: a ledge, the press from standing)
    if (prev.link && (prev.area == AREA_VAULT || prev.area == AREA_ROLL)) { CrossSegment(prev, w, prev.area == AREA_ROLL); return; }
    float dx = w.p[0] - gMe.pos[0], dz = w.p[2] - gMe.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    const bool last = gF.next + 1 == gF.path.size();
    // its lane: a point 3 m ahead on the line (the corner at most), 0.3 to 1 m aside (its lane, drawn every 10 to
    // 20 s) or 1.2 m off an ally 1 to 15 m ahead on its way (walking behind him within 2.5 m without room); where
    // the mesh leaves room, 2 m off a point kept off; never the way's end itself, a link, a low passage
    float tx = w.p[0], tz = w.p[2], speedK = 1.f;
    bool aside = false, passed = false;                  // (the corner itself moved: reached at its point aside, or passed by)
    bool laneOn = false;
    float proj = 0.f, segLen = 0.f;                      // (v137) the doll's place along this segment, its length
    {
        const float sx = w.p[0] - prev.p[0], sz = w.p[2] - prev.p[2], sl = std::sqrt(sx * sx + sz * sz);
        segLen = sl;
        proj = sl > 1.f ? ((gMe.pos[0] - prev.p[0]) * sx + (gMe.pos[2] - prev.p[2]) * sz) / sl : 0.f;
        const float along = sl > 1.f ? std::max(0.f, proj) + 3000.f : 0.f;
        const bool atCorner = along >= sl;
        if (!gOldWays && !(last && atCorner) && !w.link && !prev.link && sl > 1500.f && prev.area == AREA_STAND && (last || w.area == AREA_STAND) && MapReady()) {
            laneOn = true;
            if (gFrame - gLaneAt > gLaneEvery) { gLaneAt = gFrame; gLaneEvery = 600 + Rnd() % 600; gLaneWant = (Rnd() % 2 ? 1.f : -1.f) * (300.f + 700.f * Rnd01()); }
            gLane += (gLaneWant - gLane) * 0.03f;
            const float ux = sx / sl, uz = sz / sl, nx = uz, nz = -ux;             // (along the way; beside it)
            const float k = atCorner ? 1.f : along / sl;
            const float l[3] = {prev.p[0] + sx * k, prev.p[1] + (w.p[1] - prev.p[1]) * k, prev.p[2] + sz * k};
            const bool keepOff = gFrame - gKeepOffAt <= 30;
            float want = gLane;
            // (the nearest ally on its way ahead: this segment's rest and 2 m, 15 m at most)
            const float reach = std::min(15000.f, sl - (along - 3000.f) + 2000.f);
            const Player* ahd = nullptr; float ahdAt = 0.f, ahdLat = 0.f;
            for (const Player& a : gPlayers) {
                if (a.local || !a.alive || !Ally(a) || std::fabs(a.pos[1] - gMe.pos[1]) > 1500.f) continue;
                const float ax = a.pos[0] - gMe.pos[0], az = a.pos[2] - gMe.pos[2], ahead = ax * ux + az * uz;
                const float alat = (a.pos[0] - prev.p[0]) * nx + (a.pos[2] - prev.p[2]) * nz;
                if (ahead < 1000.f || ahead > reach || std::fabs(alat) > 2000.f || (ahd && ahead >= ahdAt)) continue;
                ahd = &a; ahdAt = ahead; ahdLat = alat;
            }
            if (ahd) {
                // (v137) the side off him kept while he stays ahead, changed when he is 0.5 m over the other side: an ally on
                // the line flipped it every frame (the target 2.4 m across at 3 m: the doll weaving)
                if (ahd->id != gAheadId || gFrame - gAheadAt > 30 || (gAheadSide > 0.f ? ahdLat > 500.f : ahdLat < -500.f))
                    gAheadSide = ahdLat > 0.f ? -1.f : 1.f;
                gAheadId = ahd->id; gAheadAt = gFrame;
                want = gAheadSide * 1200.f;
                // (walking behind him only when he is not running on: no walk into an item)
                if (ahdAt < 2500.f && ahd->vel[0] * ux + ahd->vel[1] * uz < 25.f && !(keepOff && Dist2D(gMe.pos, gKeepOff) < 3000.f)) speedK = 0.6f;
            }
            const float sgn = want >= 0.f ? 1.f : -1.f;
            float off = std::max(0.f, std::min(std::fabs(want), gMap->nav->ClearAlong(l, nx * sgn, nz * sgn, std::fabs(want) + 300.f, true) - 300.f)) * sgn;
            float gnd[3];
            const bool onGround = Ground(gnd);
            if (std::fabs(off) > 100.f && onGround) {
                const float t3[3] = {l[0] + nx * off, l[1], l[2] + nz * off};
                const bool offOk = !keepOff || Dist2D(t3, gKeepOff) >= 2000.f || std::fabs(t3[1] - gKeepOff[1]) > 2500.f;
                if (!offOk || !gMap->nav->Walkable(gnd, t3, true) || (atCorner && !gMap->nav->Walkable(t3, gF.path[gF.next + 1].p, true))) off = 0.f;
            } else off = 0.f;
            // (v137) the offset eased, 25 mm a frame at most: the point aside came and went with the room the mesh left
            // (the target jumping between the corner and a point 1 m aside 3 m ahead, frame to frame)
            gLaneNow += std::max(-25.f, std::min(25.f, off - gLaneNow));
            const float t3[3] = {l[0] + nx * gLaneNow, l[1], l[2] + nz * gLaneNow};
            if (std::fabs(gLaneNow) > 100.f && onGround && gMap->nav->Walkable(gnd, t3, true) &&
                (!atCorner || gMap->nav->Walkable(t3, gF.path[gF.next + 1].p, true))) {
                tx = t3[0]; tz = t3[2]; aside = atCorner; passed = aside && along - 3000.f > sl + 200.f && d < 1500.f;
                if (std::fabs(off) >= 900.f) speedK = 1.f;
            } else if (!atCorner && onGround && gMap->nav->Walkable(gnd, l, true)) { tx = l[0]; tz = l[2]; }   // (on the line, 3 m ahead)
        }
    }
    if (!laneOn) gLaneNow += std::max(-25.f, std::min(25.f, -gLaneNow));
    // a corner cut as a player does: within 1.2 m of it, the next corner straight from here on the standing floor
    // (the wide mesh), about level
    bool cut = false;
    if (!gOldWays && !last && !w.link && !prev.link && d < 1200.f && prev.area == AREA_STAND && w.area == AREA_STAND && MapReady()) {
        float gnd[3];
        const float* on = gF.path[gF.next + 1].p;
        cut = Ground(gnd) && std::fabs(on[1] - gnd[1]) < 1000.f && (gMap->wide ? *gMap->wide : *gMap->nav).Walkable(gnd, on, true);
    }
    // (v137) a corner passed by (beyond its foot on the segment, within 1.1 m; the way's end 0.9 m): reached. A run turns
    // wider than the 0.3 m it had to come within: the doll circled the corner
    bool passedBy = !w.link && !prev.link && segLen > 1.f && proj >= segLen - 150.f && d < (last ? 900.f : 1100.f);
    if (passedBy && !last) { float gnd[3]; passedBy = MapReady() && Ground(gnd) && gMap->nav->Walkable(gnd, gF.path[gF.next + 1].p); }   // (round a wall's end: the next corner straight from here)
    const float tdx = tx - gMe.pos[0], tdz = tz - gMe.pos[2];
    if (d < (last ? 500.f : 300.f) || cut || passedBy || (aside && (passed || tdx * tdx + tdz * tdz < 350.f * 350.f))) {   // reached this corner (or its point aside)
        ++gF.next; gF.best = 1e30f; gF.lastProgress = gFrame; gF.walls = 0;
        if (gF.next >= gF.path.size()) { gF.active = false; ++gNavStats.reached; Steer(0, 0, 0); }
        return;
    }
    if (d < gF.best - 40.f) { gF.best = d; gF.lastProgress = gFrame; }
    // the action button (Triangle) toward the corner: a ledge of a climb link, or an obstacle met on the way
    // (a low wall, a fence the player climbs over); what it did is logged once it is over
    const bool climbing = prev.link && prev.area == AREA_CLIMB;
    if (gF.actionUntil > gFrame || (climbing && d < 1600.f)) {
        Steer(dx, dz, kWalkPush);                        // (v137: walked, the button 3 frames in 8: run into it, the player pressed its back to it)
        if (gFrame % 8 < 3) gPad.buttons |= 0x1000;
        if (gF.actionUntil == gFrame + 1 && gDebug)
            Log("action button: hips %+.0f, %.0f closer (state %x, %x before; %x, %x now)", gMe.pos[1] - gF.actionFrom[1], Dist2D(gF.actionFrom, w.p) - d,
                gF.actionBits0, gF.actionBits, gMe.bits[0], gMe.bits[6]);
        if (!climbing || gFrame - gF.lastProgress <= 150) return;
    }
    if (gF.viewUntil > gFrame) {                         // (a tap against a view state: nothing else meanwhile)
        Steer(0, 0, 0);
        if (gF.viewUntil - gFrame > 20) gPad.buttons |= gF.viewTries == 2 ? 0x4000 : 0x0800;
        if (gF.viewUntil == gFrame + 1 && gDebug)
            Log("stuck in the state %x: %s, the state %x now", gF.viewState, gF.viewTries == 1 ? "R1 tapped" : gF.viewTries == 2 ? "Cross tapped" : "R1 held 40 frames", gMe.bits[0]);
        return;
    }
    if (gF.sideUntil > gFrame) {                         // getting unstuck: a side step, the stick turned
        if (gF.sideUntil - gFrame > 16) { Steer(0, 0, 0); return; }         // first let go (leaves the wall)
        Steer(-dz * gF.side + dx * 0.3f, dx * gF.side + dz * 0.3f, kWalkPush);   // (v137: a walk; run, the step was a turn and a dash)
        return;
    }
    // (v137) held standing in a life's first 3 s (the deploy's moment, the game's own): not stuck (v136's audit: the
    // action button pressed at the spawn 1.5 s after the deploy, the stick dead meanwhile)
    if (LifeFrames() >= 0 && LifeFrames() < 180 && (gMe.bits[0] & 0xFFFF) == 3) gF.lastProgress = gFrame;
    if (gMe.bits[0] == 2 && gFrame - gF.lastProgress > 20) {   // flattened against a wall: let go, then aside
        if (gDebug) Log("against a wall at %.0f %.0f %.0f: letting go", gMe.pos[0], gMe.pos[1], gMe.pos[2]);
        ++gF.stuck; ++gNavStats.stuck;
        if (++gF.walls >= 3) { gF.walls = 0; Replan(w.p, d, true); return; }   // the same wall again and again: another way
        gF.side = (gF.stuck & 1) ? 1.f : -1.f; gF.sideUntil = gFrame + 28;
        gF.lastProgress = gFrame;
        return;
    }
    if (gFrame - gF.lastProgress > 75) {                 // 1.25 s without getting closer: stuck
        ++gF.stuck; ++gNavStats.stuck;
        // a view state the doll did not ask for (the state word 0 with 0x80 walking: v133's audit, fallen into
        // a pit on Pillbox Purgatory against its wall, a doll stood 64 s in the state 0x82, the stick turning
        // its view): R1 tapped, then Cross (4 frames each, 20 still after), then R1 held 40 frames (v134's
        // audit: the wall's view left when R1 held a while was let go)
        if ((gMe.bits[0] & 0x80) && gF.viewTries < 3) {
            ++gF.viewTries; gF.viewState = gMe.bits[0]; gF.viewUntil = gFrame + (gF.viewTries == 3 ? 60 : 24); gF.lastProgress = gFrame;
            return;
        }
        if (gF.actions < 1) {                            // first the action button toward the corner (an obstacle to climb?)
            ++gF.actions; gF.actionUntil = gFrame + 45; gF.lastProgress = gFrame;
            memcpy(gF.actionFrom, gMe.pos, sizeof gF.actionFrom); gF.actionBits = gMe.bits[6]; gF.actionBits0 = gMe.bits[0];
            if (gDebug) Log("stuck at %.0f %.0f %.0f: the action button toward %.0f %.0f %.0f (state %x, %x)", gMe.pos[0], gMe.pos[1], gMe.pos[2], w.p[0], w.p[1], w.p[2],
                            gMe.bits[0], gMe.bits[6]);
            return;
        }
        Replan(w.p, d);
        return;
    }
    // the area of the segment being walked: the one of the corner left behind; crouched or lying where
    // the way is walked standing (after a fight's crouch): still while it gets up (moving crouched is
    // crawling), 2.5 s at most (v137: lying, two presses 45 frames apart; 1.5 s, the doll crawled off still low)
    const uint8_t area = gF.path[gF.next - 1].area;
    Posture(area);
    if (area != AREA_CRAWL && (Low() || PostureMoving())) {
        if (gF.upSince < 0) gF.upSince = gFrame;
        if (gFrame - gF.upSince < 150) { Steer(0, 0, 0); gF.lastProgress = gFrame; return; }   // (not stuck: getting up)
    } else gF.upSince = -1;
    // (v137) a vault's or a ladder's start walked into over the last 2 m (the vault's button before the obstacle is met;
    // the ladder's volume, 0.3 m to reach, not run round)
    if (w.link && (w.area == AREA_VAULT || w.area == AREA_LADDER) && d < 2000.f) speedK = std::min(speedK, kWalkPush);
    // (v141 beta 3) a sharp turn on the run (its way behind it: a corner passed, a box taken, a new goal): the stick let go 4
    // frames, the run stops, then pushed again: the player turns where it stands instead of running a loop round the point
    // (motion lines of the deploy bench: 2 to 8 turns of 270 degrees and more in 5 minutes, at the boxes, the U-turns).
    // Once a second at most; MGO1_DOLL_PIVOT=0: as before
    static const bool pivot = !(getenv("MGO1_DOLL_PIVOT") && *getenv("MGO1_DOLL_PIVOT") == '0');
    if (pivot && area != AREA_CRAWL && !Low()) {
        const uint32_t st0 = gMe.bits[0] & 0xFFFF;
        const float need = std::fabs(Wrap(std::atan2(tdx, tdz) - Facing()));
        if ((st0 == 0x5002 || st0 == 0x4802) && need > 1.92f && gFrame - gF.pivotAt > 60 && tdx * tdx + tdz * tdz > 800.f * 800.f) {
            gF.pivotAt = gFrame; gF.pivotUntil = gFrame + 4;
            if (gDebug >= 2) Log("path: a turn of %.0f degrees on the run: stopped first", need * 57.2958f);
        }
        if (gFrame < gF.pivotUntil) { Steer(0, 0, 0); gF.lastProgress = gFrame; return; }
    }
    // a crawl passage not crouched yet: walked into (walking, Cross crouches; running, it would roll), crawled
    // once crouched
    Steer(tdx, tdz, area == AREA_CRAWL && !Low() ? 0.6f : speedK);
}

// ---- (v141 beta 6) the ways out of a still spell (the brain's still watch: a moving activity pushing the stick, under 0.3 m
// in 5 s, no link under way): tried in turn, each logged once ("still: unstick <way> ..."), each a move of its own then the
// path again 1.5 s; 0.6 m from where it stood: over. back: a step back, then toward 40 degrees aside of the way on; side: a
// step to a side (the other one the next time); climb: a ledge in front within the action button's reach (a face within 1 m
// at the knees, open above it under 1.25 m): the press run at its face; low: something at head height in front, open at the
// knees: crouched on; avoid: the link or the leg in the way left out of the paths, planned again. The ways ran out twice at a
// place (1.5 m, this map): its leg left out of the paths for the map (a trap of the mesh, learned for the next games as the
// others are). MGO1_DOLL_UNSTICK=0: none (the still watch as in beta 3) ----
struct Unstick { int way = -1, sub = 0, side = 1; long long at = 0, start = 0; float from[3] = {0, 0, 0}, dir[2] = {0, 1}; PressRun pr; const MapState* map = nullptr; };
static Unstick gUs;
struct StuckPlace { float p[3]; int n; };
static std::vector<StuckPlace> gStuckPlaces;
static std::string gStuckMap;
static const char* const kUnstickWay[] = {"back", "side", "climb", "low", "avoid"};
bool OnLinkNow() { return gF.active && gF.next > 0 && gF.next < gF.path.size() && gF.path[gF.next - 1].link; }
bool UnstickOn() { static const bool on = !(getenv("MGO1_DOLL_UNSTICK") && *getenv("MGO1_DOLL_UNSTICK") == '0'); return on; }
static void UnstickDir(float* d) {                   // (the way on: the path's next corner, else the stick's last ask)
    if (gF.active && gF.next < gF.path.size()) {
        const float* c = gF.path[gF.next].p;
        const float dx = c[0] - gMe.pos[0], dz = c[2] - gMe.pos[2], n = std::sqrt(dx * dx + dz * dz);
        if (n > 100.f) { d[0] = dx / n; d[1] = dz / n; return; }
    }
    d[0] = std::sin(gSL.asked); d[1] = std::cos(gSL.asked);
}
void UnstickStart() {
    static int side = 1;
    side = -side;
    gUs = Unstick(); gUs.way = 0; gUs.side = side; gUs.at = gUs.start = gFrame; gUs.map = gMap.get();
    memcpy(gUs.from, gMe.pos, sizeof gUs.from);
    UnstickDir(gUs.dir);
}
// a ledge in front: a face within 1 m at 0.35 or 0.6 m over the ground, a height under 1.25 m clear past it
static bool LedgeAhead(const float* d, float* faceDist) {
    if (!MapReady()) return false;
    const float g = gMe.pos[1] - 800.f;
    float best = 1e30f;
    for (float h : {350.f, 600.f}) {
        const float a[3] = {gMe.pos[0], g + h, gMe.pos[2]}, b[3] = {gMe.pos[0] + d[0] * 1100.f, g + h, gMe.pos[2] + d[1] * 1100.f};
        float t = 0.f, n[3];
        if (gMap->nav->FirstHit(a, b, &t, n) && std::fabs(n[1]) < 0.7f) best = std::min(best, t * 1100.f);
    }
    if (best > 1000.f) return false;
    for (float h = 700.f; h <= 1250.f; h += 100.f) {
        const float a[3] = {gMe.pos[0], g + h, gMe.pos[2]}, b[3] = {gMe.pos[0] + d[0] * (best + 600.f), g + h, gMe.pos[2] + d[1] * (best + 600.f)};
        if (gMap->nav->Clear(a, b)) { *faceDist = best; return true; }
    }
    return false;
}
static bool LowAhead(const float* d) {               // (a face at 1.5 m within 1 m in front, none at 0.4 m)
    if (!MapReady()) return false;
    const float g = gMe.pos[1] - 800.f;
    const float a1[3] = {gMe.pos[0], g + 1500.f, gMe.pos[2]}, b1[3] = {gMe.pos[0] + d[0] * 1000.f, g + 1500.f, gMe.pos[2] + d[1] * 1000.f};
    const float a2[3] = {gMe.pos[0], g + 400.f, gMe.pos[2]}, b2[3] = {gMe.pos[0] + d[0] * 1000.f, g + 400.f, gMe.pos[2] + d[1] * 1000.f};
    return !gMap->nav->Clear(a1, b1) && gMap->nav->Clear(a2, b2);
}
// a frame of the ways: 1 the pad its own, 2 at work (the path again: the pad the brain's), 0 over (moved, or not its
// business: dead, on a ladder, no longer moving), -1 the ways ran out
int UnstickFrame(bool moving) {
    if (gUs.way < 0) return 0;
    // (dead, a new life since, another map, on a ladder, no longer moving: not its business)
    if (!gMe.alive || OnLadder() || !moving || gMap.get() != gUs.map || (LifeFrames() >= 0 && gFrame - LifeFrames() > gUs.start)) { gUs.way = -1; return 0; }
    const float moved = Dist2D(gMe.pos, gUs.from);
    if (moved > 600.f && !ClimbingNow()) {
        Log("still: unstick %s worked: %.1f m from where it stood after %.1f s", kUnstickWay[std::min(gUs.way, 4)], moved / 1000.f, (gFrame - gUs.start) / 60.0);
        gUs.way = -1;
        return 0;
    }
    auto next = [&]() { ++gUs.way; gUs.sub = 0; gUs.at = gFrame; UnstickDir(gUs.dir); };
    for (int guard = 0; guard < 6 && gUs.sub == 0; ++guard) {
        if (gUs.way >= 5) { gUs.way = -1; return -1; }
        gUs.sub = 1; gUs.at = gFrame;
        const float* d = gUs.dir;
        const float* c = gF.active && gF.next < gF.path.size() ? gF.path[gF.next].p : nullptr;
        switch (gUs.way) {
        case 0: Log("still: unstick back at %.0f %.0f %.0f (a step back, then the way on from 40 degrees aside)", gMe.pos[0], gMe.pos[1], gMe.pos[2]); break;
        case 1: Log("still: unstick side at %.0f %.0f %.0f (a step to its %s)", gMe.pos[0], gMe.pos[1], gMe.pos[2], gUs.side > 0 ? "right" : "left"); break;
        case 2: {
            float fd = 0.f;
            if (!LedgeAhead(d, &fd)) { next(); continue; }
            Log("still: unstick climb at %.0f %.0f %.0f: a ledge %.2f m in front, the press run at it", gMe.pos[0], gMe.pos[1], gMe.pos[2], fd / 1000.f);
            const float o[3] = {gMe.pos[0], gMe.pos[1] - 800.f, gMe.pos[2]};
            PressStart(gUs.pr, o, d[0], d[1], fd, std::max(fd, 650.f), "unstick", false);
            break;
        }
        case 3:
            if (!LowAhead(d)) { next(); continue; }
            Log("still: unstick low at %.0f %.0f %.0f: low in front, crouched on", gMe.pos[0], gMe.pos[1], gMe.pos[2]);
            break;
        default: {
            // the link in the way (the path's segment from a link) out of both meshes, learned; else that leg: a trap of the mesh
            const Waypoint* lk = gF.active && gF.next > 0 && gF.next < gF.path.size() && gF.path[gF.next - 1].link ? &gF.path[gF.next - 1] : nullptr;
            if (lk && c && MapReady() && lk->area != AREA_LADDER && lk->area != AREA_STEPS) {
                gMap->nav->DisableLink(lk->p, c, 700.f);
                if (gMap->wide) gMap->wide->DisableLink(lk->p, c, 700.f);
                if (LinkName(lk->area)) LearnWrite(FactLink(lk->area, lk->p, c));
                Log("still: unstick avoid at %.0f %.0f %.0f: the %s at %.0f %.0f %.0f left out of the paths, planned again", gMe.pos[0], gMe.pos[1], gMe.pos[2],
                    LinkName(lk->area) ? LinkName(lk->area) : "link", lk->p[0], lk->p[1], lk->p[2]);
            } else if (c) {
                AddMeshTrap(gMe.pos, c);
                Log("still: unstick avoid at %.0f %.0f %.0f: the way toward %.0f %.0f %.0f left out (a trap of the mesh), planned again", gMe.pos[0], gMe.pos[1], gMe.pos[2],
                    c[0], c[1], c[2]);
            } else { next(); continue; }
            float goal[3]; memcpy(goal, gF.goal, sizeof goal);
            gTrip.haveVia = false; gTrip.avoid.clear();
            if (!PlanTo(goal)) { gUs.way = -1; return -1; }
            break;
        }
        }
    }
    const long long t = gFrame - gUs.at;
    const float* d = gUs.dir;
    auto own = [&]() { gPad.buttons &= ~0x5000u; };       // (the brain's Triangle and Cross off: the way's own)
    switch (gUs.way) {
    case 0:                                              // back 0.6 s, 40 degrees aside 0.7 s, the path 1.5 s
        if (t < 36) { own(); Steer(-d[0], -d[1], kWalkPush); return 1; }
        if (t < 78) { own(); const float a = std::atan2(d[0], d[1]) + 0.7f * gUs.side; Steer(std::sin(a), std::cos(a), kWalkPush); return 1; }
        if (t < 168) return 2;
        next(); return 2;
    case 1:                                              // a step aside 0.6 s, the path 1.5 s
        if (t < 36) { own(); Steer(d[1] * gUs.side, -d[0] * gUs.side, kWalkPush); return 1; }
        if (t < 126) return 2;
        next(); return 2;
    case 2: {                                            // the press run (8 s at most), the path 1.5 s
        if (gUs.sub == 1) {
            own();
            const char* why = "";
            const int r = PressFrame(gUs.pr, &why);
            if (r == 1) { if (gFrame - gUs.at > 480) gUs.sub = 2; return 1; }
            if (r == 0 && gFrame - gUs.at < 480) return 1;
            gUs.sub = 2; gUs.at = gFrame;
            return 1;
        }
        if (ClimbingNow()) { own(); Steer(0, 0, 0); gUs.at = gFrame; return 1; }
        if (t < 90) return 2;
        next(); return 2;
    }
    case 3:                                              // crouched (Cross), on 1.5 s, the path 1.5 s
        if (t < 30) { own(); Steer(0, 0, 0); SetLow(true); PostureHold(); return 1; }
        if (t < 120) { own(); Steer(d[0], d[1], 0.6f); return 1; }
        if (t < 210) return 2;
        next(); return 2;
    default:                                             // the new plan 2 s
        if (t < 120) return 2;
        gUs.way = -1;
        return -1;
    }
}
// the ways ran out at this place: twice within 1.5 m (this map), the leg left out for the map
void UnstickGaveUp() {
    if (!MapReady()) return;
    if (gStuckMap != gMap->key) { gStuckMap = gMap->key; gStuckPlaces.clear(); }
    StuckPlace* sp = nullptr;
    for (StuckPlace& s : gStuckPlaces) if (Dist2D(s.p, gMe.pos) < 1500.f && std::fabs(s.p[1] - gMe.pos[1]) < 1000.f) sp = &s;
    if (!sp) { gStuckPlaces.push_back({{gMe.pos[0], gMe.pos[1], gMe.pos[2]}, 0}); sp = &gStuckPlaces.back(); }
    if (++sp->n < 2) return;
    int kept = 0;
    for (MeshTrap& t : gMeshTraps) {
        const float m[3] = {(t.a[0] + t.b[0]) * 0.5f, t.a[1], (t.a[2] + t.b[2]) * 0.5f};
        if (Dist2D(m, sp->p) < 2000.f && std::fabs(m[1] - sp->p[1]) < 1500.f) { t.at = gFrame + 100000000LL; ++kept; }   // (never old)
    }
    Log("still: the ways ran out twice at %.0f %.0f %.0f: %d leg%s there left out of the paths for this map", sp->p[0], sp->p[1], sp->p[2], kept, kept == 1 ? "" : "s");
}

// ---- debugging: what the game does around the doll ----
static std::map<std::string, int> gActorCounts;
static void CountActor(uint32_t, const std::string& name, void* ctx) { ++(*(std::map<std::string, int>*)ctx)[name]; }
static void ActorDiff() {
    std::map<std::string, int> now;
    ps2world::ForEachActor(&CountActor, &now);
    std::string added, removed;
    for (auto& a : now) { auto it = gActorCounts.find(a.first); if (it == gActorCounts.end() || it->second < a.second) added += " " + a.first + (a.second > 1 ? "x" + std::to_string(a.second) : ""); }
    for (auto& a : gActorCounts) { auto it = now.find(a.first); if (it == now.end() || it->second < a.second) removed += " " + a.first; }
    if (!added.empty()) Log("actors +%s", added.c_str());
    if (!removed.empty()) Log("actors -%s", removed.c_str());
    gActorCounts.swap(now);
}
static std::map<uint32_t, std::string> gActorNames;
static void NameActor(uint32_t w, const std::string& name, void* ctx) { (*(std::map<uint32_t, std::string>*)ctx)[w] = name; }
static void OnMessage(uint32_t actor, uint32_t code, uint32_t arg) {
    // in a match only, and never the software keyboard's messages (0xB0 .. 0xB2: the keys typed, the
    // log-in's included): nothing typed ever reaches the doll or the log
    if (!ps2world::InMatch() || (code >> 16) - 0xB0u <= 2u) return;
    if ((code >> 8) == 0x7510) OnKillMessage(code, arg);
    if (!gDebug) return;
    if (code == 0x751007 && arg >= 0x100000 && arg < 0x02000000)       // (the radio, to the HUD: {sender, _, phrase u16, flag})
        Log("radio: from %u phrase %u flag %u (%02x %02x %02x %02x %02x %02x)", R8(arg), R16(arg + 2), R8(arg + 4),
            R8(arg), R8(arg + 1), R8(arg + 2), R8(arg + 3), R8(arg + 4), R8(arg + 5));
    static std::map<uint64_t, int> seen;
    const uint64_t key = ((uint64_t)actor << 32) | code;
    const int n = ++seen[key];
    if (n > 3 && (code >> 16) != 0x75 && (code >> 16) != 0x76) return;
    // sent every other frame in a match (0x751008 and 0x75100D: 57000 and 18000 lines in a 30-minute audit)
    if ((code == 0x751009 || code == 0x751008 || code == 0x75100D) && n > 3) return;
    if (gActorNames.find(actor) == gActorNames.end()) { gActorNames.clear(); ps2world::ForEachActor(&NameActor, &gActorNames); }
    auto it = gActorNames.find(actor);
    Log("message %06x arg %08x to %08x %s", code, arg, actor, it != gActorNames.end() ? it->second.c_str() : "?");
}
static void RoomLog() {
    uint8_t v[0x80];
    if (!ps2world::RoomVariables(v, sizeof v)) { Log("room: no variables"); return; }
    char b[400]; int n = snprintf(b, sizeof b, "room:");
    for (int i = 0; i < (int)sizeof v && n < (int)sizeof b - 4; ++i) n += snprintf(b + n, sizeof b - n, "%s%02x", i % 16 ? "" : " ", v[i]);
    Log("%s", b);
    std::string teams;
    for (int id = 0; id < 16; ++id) { int t = -1; std::string nm; if (ps2world::PlayerInfo(id, &t, &nm) && (t != 0xFF || !nm.empty())) teams += " " + std::to_string(id) + ":" + nm + "/" + std::to_string(t); }
    Log("players:%s", teams.c_str());
    if (gDebug >= 2)                                   // each named player's variable set (after the name)
        for (int id = 0; id < 16; ++id) {
            uint8_t pv[0x40]; int t = -1; std::string nm;
            if (!ps2world::PlayerInfo(id, &t, &nm) || nm.empty() || !ps2world::VariableSet(id + 1, pv, sizeof pv)) continue;
            char pb[200]; int pn = snprintf(pb, sizeof pb, "player %d vars:", id);
            for (int i = 0; i < 0x40 && pn < (int)sizeof pb - 4; ++i) if (i < 2 || i >= 0x12) pn += snprintf(pb + pn, sizeof pb - pn, "%s%02x", i % 16 ? "" : " ", pv[i]);
            Log("%s", pb);
        }
}
// the items of the map (debug): the float triples of their works that look like map positions
static void ItemActor(uint32_t w, const std::string& name, void* ctx) {
    if (name != "NewItemBox2" && name != "NewItemBox_Food" && name != "NewOnlineMushroom" && name != "NewOnlineItemChara2" &&
        name != "NewOnlineStageCBox" && name != "NewItemBox" && name.rfind("NewWeapon", 0) != 0) return;
    int& n = (*(std::map<std::string, int>*)ctx)[name];
    if (++n > 3) return;
    std::string s;
    for (uint32_t o = 0x10; o + 12 <= 0x300; o += 4) {
        const float x = RF(w + o), y = RF(w + o + 4), z = RF(w + o + 8);
        auto ok = [](float v) { return std::fabs(v) >= 50.f && std::fabs(v) < 60000.f && v == v; };
        if (ok(x) && ok(z) && std::fabs(y) < 20000.f) { char b[64]; snprintf(b, sizeof b, " +%x(%.0f %.0f %.0f)", o, x, y, z); s += b; o += 8; }
    }
    Log("item %s %08x:%s", name.c_str(), w, s.c_str());
}
static void ItemsLog() {
    std::map<std::string, int> n;
    ps2world::ForEachActor(&ItemActor, &n);
}
static bool gLastControl = false;
static void StateLog() {
    const uint32_t o = gMe.obj;
    if (!o) { Log("state: no local player"); return; }
    char b[512];
    int n = snprintf(b, sizeof b, "state: pos %.0f %.0f %.0f life %d stun %d acc %d down %u slot %u bits", gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.life, gMe.stun,
                     R16s(o + 0xB72), R8(o + 0xDF0), R8(o + 0xB60));
    for (int k = 0; k < 11 && n < (int)sizeof b - 12; ++k) n += snprintf(b + n, sizeof b - n, " %x", R32(o + 0xB2C + 4 * k));
    n += snprintf(b + n, sizeof b - n, " facing %.0f pad %s%04x stick %02x %02x", Facing() * 57.2958f, gLastControl ? "" : "(free) ", gLastPad.buttons,
                  gLastPad.sticks[2], gLastPad.sticks[3]);
    Log("%s | %s", b, BrainState());
}

// ---- status: what the doll does, for the program's PLAYABLE DOLL page (status.json in its folder) ----
struct Activity { std::string code = "starting", arg; };   // a code and its argument: the program says it in its language
static Activity gAct;
const std::string& ActCode() { return gAct.code; }
int gKills = 0, gDeaths = 0;
// its score, from its own variable set (1 + its id; 32 u16 at +0x14: kills, deaths, stuns ...; food eaten
// +0x44, CQC +0x36, headshots +0x3A: docs/DEDICATED_HOST.md)
static int gFoodEaten = -1;
int MyScore(int offset) {
    if (gMyId < 0 || offset < 0 || offset + 2 > 0x60) return -1;
    uint8_t v[0x60];
    if (!ps2world::VariableSet(gMyId + 1, v, sizeof v)) return -1;
    return (int)(v[offset] | (v[offset + 1] << 8));
}
static void SeeScore() {
    if (gMyId < 0) return;
    uint8_t v[0x60];
    if (!ps2world::VariableSet(gMyId + 1, v, sizeof v)) return;
    auto u16 = [&](int o) { return (int)(v[o] | (v[o + 1] << 8)); };
    const int kills = u16(0x14), deaths = u16(0x16);
    if (kills != gKills || deaths != gDeaths)                // (its daily log: every kill and death)
        Log("score: kills %d, deaths %d, stuns %d, CQC %d, headshots %d", kills, deaths, u16(0x18), u16(0x36), u16(0x3A));
    gKills = kills; gDeaths = deaths;
    const int food = u16(0x44);
    if (gDebug && food != gFoodEaten && gFoodEaten >= 0) Log("score: food eaten %d (kills %d, deaths %d, stuns %d, CQC %d, headshots %d)", food, gKills, gDeaths,
                                                              u16(0x18), u16(0x36), u16(0x3A));
    gFoodEaten = food;
}
static Activity gActLogged;                       // the last one in the log
static long long gActSince = 0;
void Act(const char* code, const std::string& arg) {
    if (gAct.code == "climbing" && strcmp(code, "climbing") != 0 && OnLadder()) return;   // (on a ladder: climbing, whatever called)
    if (gAct.code != code || gAct.arg != arg) {
        if (gDebug) { Log("activity: %s %s", code, arg.c_str()); gActLogged.code = code; gActLogged.arg = arg; }
        gAct.code = code; gAct.arg = arg;
        gActSince = gFrame;
    }
}
// what it does, in its daily log without the debug too: an activity once it has lasted a second
static void ActJournal() {
    if (gDebug || gFrame - gActSince < 60 || (gAct.code == gActLogged.code && gAct.arg == gActLogged.arg)) return;
    gActLogged = gAct;
    Log("activity: %s %s", gAct.code.c_str(), gAct.arg.c_str());
}
static std::string JsonText(const std::string& v) {
    std::string o = "\"";
    for (unsigned char c : v) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20 || c > 0x7e) { char h[8]; snprintf(h, sizeof h, "\\u%04x", c); o += h; }
        else o += (char)c;
    }
    return o + "\"";
}
static void WriteStatus() {
    if (gDir.empty()) return;
    SeeScore();
    char b[1024];
    std::string o = "{\"v\":1";
    snprintf(b, sizeof b, ",\"frame\":%lld,\"page\":%s,\"nav\":%d", gFrame, JsonText(gMap ? gMap->key : "").c_str(), gMap ? gMap->state.load() : 0); o += b;
    o += ",\"name\":" + JsonText(MyName()) + ",\"game\":" + JsonText(gSet.game);
    // its slot (the game's id, -1 not known) and what gave it: uid, name, world, session, unknown (the manager trusts
    // the name of a doll whose identity is known only)
    snprintf(b, sizeof b, ",\"id\":%d,\"identity\":\"%s\"", gMyId, gMyIdBy); o += b;
    snprintf(b, sizeof b, ",\"rule\":%s,\"map\":%d,\"phase\":%d,\"left\":%d,\"team\":%d,\"alive\":%d,\"life\":%d,\"stun\":%d",
             JsonText(RuleName(gRoom.rule <= 6 ? gRoom.rule : gRule)).c_str(), gRoom.map, gRoom.phase, gRoom.secondsLeft, gMyTeam == 0 || gMyTeam == 1 ? gMyTeam : gMyTeam == 0xFE ? 2 : -1,
             gMe.alive ? 1 : 0, gMe.life, gMe.stun);
    o += b;
    snprintf(b, sizeof b, ",\"difficulty\":%d,\"level\":%.1f,\"level_why\":\"%s\",\"adapt\":%d,\"k\":%d,\"d\":%d,\"real\":%d,\"pos\":[%.0f,%.0f,%.0f]",
             gSet.difficulty, LevelShown(), LevelWhy(), gSet.adapt ? 1 : 0, gKills, gDeaths, RealPlayers(), gMe.pos[0], gMe.pos[1], gMe.pos[2]);
    o += b;
    // (v141 beta 6) the presence: the real players of the session, waiting for one (spectator) or playing, and why
    snprintf(b, sizeof b, ",\"real_players\":%d,\"session_dolls\":%d,\"session_unknown\":%d,\"presence\":\"%s\",\"spectator_reason\":\"%s\"", RealPlayers(),
             gPresence.dolls, gPresence.unknown, PresenceWaiting() ? "waiting_for_real" : "active", SpectatorReason());
    o += b;
    // (v141 beta 6) the ladder: its tier now and the live level's block; the respawn; the fight's aim mode and last decision
    o += ",\"tier\":" + JsonText(TierName(LevelShown())) + ",\"aim\":" + std::string(gSet.aim ? "\"direct\"" : "\"pad\"") + ",\"live\":" + AdaptJson() + ",\"respawn\":" + RespawnJson() + ",\"fight\":" + FightJson();
    o += ",\"fpv\":" + FpvJson();                          // ((v141 beta 6) the first person view weapons' state machine)
    {   // ((v141 beta 9) the last "pace:" line (the doll manager's pace guard): its number, VBlanks a second, due, the share of a core)
        int pl = 0; double pr = 0, pd = 0, pc = 0;
        if (ps2world::LastPace(&pl, &pr, &pd, &pc)) { snprintf(b, sizeof b, ",\"pace\":{\"n\":%d,\"rate\":%.2f,\"due\":%.2f,\"cpu\":%.0f}", pl, pr, pd, pc); o += b; }
    }
    o += ",\"order\":" + JsonText(gSet.order) + ",\"target\":" + JsonText(gSet.target) + ",\"team_want\":" + JsonText(gSet.team);
    o += ",\"act\":" + JsonText(gAct.code) + ",\"arg\":" + JsonText(gAct.arg) + "}\n";
    const std::string tmp = gDir + "\\status.tmp", fin = gDir + "\\status.json";
    if (FILE* f = fopen(tmp.c_str(), "wb")) { fputs(o.c_str(), f); fclose(f); MoveFileExA(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING); }
}

// ---- (v137) a life's start: the spawn (alive again), the deploy (the brain's first frame after the menu's; the menu
// again before it ran 10 frames: a free frame of the wait, the deploy still to come) ----
static void LifeWatch(bool brain) {
    if (!gMe.alive) { gLife = LifeRun(); return; }
    if (gLife.spawnAt < 0) {
        gLife.spawnAt = gFrame;
        if (gMotion) Log("motion: spawn p=%.0f,%.0f,%.0f h=%.0f %s ph=%d", gMe.pos[0], gMe.pos[1], gMe.pos[2], Facing() * 57.29578f, gMe.frozen ? "frozen" : "free", gRoom.phase);
    }
    if (!brain) { gLife.deployAt = -1; gLife.deployLogged = false; return; }   // (the menu's frame: the wait, a round's start)
    if (gLife.deployAt < 0) gLife.deployAt = gFrame;
    if (!gLife.deployLogged && gFrame - gLife.deployAt >= 10) {
        gLife.deployLogged = true;
        if (gMotion) Log("motion: deploy f%lld after %lld frames alive p=%.0f,%.0f,%.0f h=%.0f ph=%d", gLife.deployAt, gLife.deployAt - gLife.spawnAt, gMe.pos[0], gMe.pos[1],
                         gMe.pos[2], Facing() * 57.29578f, gRoom.phase);
    }
}
// ---- (v141 beta 6) the match's text chat: a map's chat keyboard (NewSoftwareKeyboard_*, "SELECT opens them", docs/MC_PC_INPUT.md
// 3). Measured (probe 31's step k): a second SELECT while the radio's menu is up opens it; Triangle, Cross, Circle leave it open;
// SELECT closes it with nothing sent; START closes it too (and START is the weapons page's OK, the pause menu's key: a blank line
// sent by a doll that never typed). A doll never types: the chat open in a match is a mistake (beta 5: the radio "left on" closed
// by SELECT opened it every time). Closed by SELECT at once, nothing else pressed while it is open (no START, Circle, Cross) ----
struct ChatGuard { bool open = false; long long openAt = -1, pressAt = -100000; int presses = 0, sent0 = 0; uint32_t before = 0; };
static ChatGuard gChat;
static int gChatOpened = 0, gChatClosed = 0;                // (this run: the openings seen, the ones closed)
static bool ChatFrame(bool mapPage) {
    if (!mapPage) { gChat = ChatGuard(); return false; }
    const bool open = ps2world::KeyboardTextEntry();
    if (!open) {
        if (gChat.open) {
            ++gChatClosed;
            // (v141 beta 6) its chat counter (the variable set's +0x34, probe 31's step k) before and after: a message sent or not
            const int sent1 = MyScore(0x34);
            Log("DOLL CHAT: mode=GAMEPLAY after %lld frames: closed by %d SELECT press%s, %s (chat counter %d -> %d; %d of %d this run)", gFrame - gChat.openAt,
                gChat.presses, gChat.presses == 1 ? "" : "es", sent1 == gChat.sent0 ? "nothing typed nor sent" : "A MESSAGE SENT", gChat.sent0, sent1, gChatClosed, gChatOpened);
            gChat = ChatGuard();
        }
        return false;
    }
    if (!gChat.open) {
        gChat.open = true; gChat.openAt = gFrame; gChat.before = gLastPad.buttons; gChat.sent0 = MyScore(0x34); ++gChatOpened;
        const bool radioClose = gFrame - RadioCloseAt() < 40;    // ((v141 beta 6) the radio's close: its SELECT opened it, on purpose)
        Log("DOLL CHAT: mode_before=%s mode_after=FREE_TEXT_CHAT_OPEN source=%s (the last frame's buttons %04x) decision=CANCEL_MODAL",
            radioClose ? "RADIO_MENU" : "GAMEPLAY", radioClose ? "radio_close" : "unexpected", gChat.before);
    }
    gControl = true; gPad = Pad();                               // (nothing else while it is open)
    const long long t = gFrame - gChat.openAt;
    if (t >= 6 && gFrame - gChat.pressAt >= 60 && gChat.presses < 6) { gChat.pressAt = gFrame; ++gChat.presses; }
    if (gFrame - gChat.pressAt < 4) gPad.buttons = 0x0001;
    if (t > 0 && t % 600 == 0) Log("DOLL CHAT: still open after %lld s (%d SELECT presses): its inputs held back", t / 60, gChat.presses);
    Act("closing_chat");
    return true;
}

// ---- (v137, MGO1_DOLL_MOTION=n) the doll's moves for tests/bench/tools/motion_report.py: every n frames its place,
// facing, sticks, buttons, posture, activity, the path's next corner and goal, the fight's target; the events: the
// spawn, the deploy, the round's phase, each posture change, each press from the spawn to 3 s after the deploy ----
static char PostureClass(bool brain) {
    if (!gMe.obj || !gMe.alive) return 'D';
    if (OnLadder()) return 'L';
    if (gMe.bits[2] & 0x10000000) return 'H';
    if (!brain && gMe.frozen) return 'F';
    if (Prone()) return 'P';
    return Low() ? 'C' : 'S';
}
static void MotionFrame(bool brain) {
    if (!gMotion) return;
    static int phase = -2; static char posture = '?'; static uint32_t lastButtons = 0;
    if (gRoom.phase != phase) { Log("motion: round ph %d -> %d", phase, gRoom.phase); phase = gRoom.phase; }
    const char pc = PostureClass(brain);
    if (pc != posture) {
        if (pc != 'D' && posture != '?' && posture != 'D') Log("motion: posture %c -> %c s=%x a=%s", posture, pc, gMe.bits[0], gAct.code.c_str());
        posture = pc;
    }
    const uint32_t b = gControl ? gPad.buttons : 0, pressed = b & ~lastButtons;
    lastButtons = b;
    if (pressed && gLife.spawnAt >= 0 && (gLife.deployAt < 0 || gFrame - gLife.deployAt <= 180)) {
        if (gLife.deployAt < 0) Log("motion: press %04x a=%s frozen +%lld", pressed, gAct.code.c_str(), gFrame - gLife.spawnAt);
        else Log("motion: press %04x a=%s +%lld", pressed, gAct.code.c_str(), gFrame - gLife.deployAt);
    }
    if (gFrame % gMotion) return;
    char c[48] = "-", g[48] = "-";
    if (gF.active && gF.next < gF.path.size()) {
        snprintf(c, sizeof c, "%.0f,%.0f", gF.path[gF.next].p[0], gF.path[gF.next].p[2]);
        snprintf(g, sizeof g, "%.0f,%.0f", gF.goal[0], gF.goal[2]);
    }
    int target = -1;
    sscanf(BrainState(), "target %d", &target);
    const Pad& p = gControl ? gPad : Pad();
    // (s5: the state word 5; its 0x20000 running was a run held back: 0.69 m in 30 frames against 1.56, v136's audit)
    Log("motion: p=%.0f,%.0f,%.0f h=%.0f l=%02x,%02x r=%02x,%02x b=%04x s=%x s1=%x s5=%x ps=%c a=%s c=%s g=%s t=%d ph=%d o=%.0f", gMe.pos[0], gMe.pos[1],
        gMe.pos[2], Facing() * 57.29578f, p.sticks[2], p.sticks[3], p.sticks[0], p.sticks[1], p.buttons, gMe.bits[0], gMe.bits[1], gMe.bits[5], pc,
        gAct.code.c_str(), c, g, target, gRoom.phase, gSL.offset * 57.29578f);
}

static bool gWasInMatch = false;
void ExitNow(int code) {
    Act(code == 0 ? "left" : "restarting");
    WriteStatus();
    fflush(stdout);
    ExitProcess((UINT)code);
}

// ---- orders (the `command` file): difficulty <1-6> (6: NIGHTMARE), team <auto|red|blue|spectator>, order <free|
// objective|hide|defend|follow <name>|target <name>>, loadout <auto|primary[,secondary]>, leave, (v150-host beta 18) aim <direct|pad> ----
static void Order(const std::string& line) {
    const size_t sp = line.find(' ');
    const std::string k = line.substr(0, sp), v = sp == std::string::npos ? "" : line.substr(sp + 1);
    if (k == "difficulty") {
        gSet.difficulty = std::max(1, std::min(10, atoi(v.c_str()))); Log("order: difficulty %d (%s)", gSet.difficulty, TierName((float)gSet.difficulty));
        if (gSet.parity) Log("parity: %s", ParitySaid().c_str());   // ((v150-host beta 16) the virtual doll's level of the new number)
    }
    else if ((k == "adapt_min" || k == "adapt_max") && !v.empty() && isdigit((unsigned char)v[0])) {
        (k == "adapt_min" ? gSet.adaptMin : gSet.adaptMax) = std::max(1, std::min(10, atoi(v.c_str())));
        Log("order: live level between %d (%s) and %d (%s)", std::min(gSet.adaptMin, gSet.adaptMax), TierName((float)std::min(gSet.adaptMin, gSet.adaptMax)),
            std::max(gSet.adaptMin, gSet.adaptMax), TierName((float)std::max(gSet.adaptMin, gSet.adaptMax)));
    }
    else if (k == "team" && (v == "auto" || v == "red" || v == "blue" || v == "spectator")) { gSet.team = v; Log("order: team %s", v.c_str()); }
    else if (k == "order") {
        const size_t sp2 = v.find(' ');
        const std::string o = v.substr(0, sp2);
        if (o == "free" || o == "objective" || o == "hide" || o == "defend" || o == "follow" || o == "target") {
            gSet.order = o;
            gSet.target = (o == "target" || o == "follow") && sp2 != std::string::npos ? v.substr(sp2 + 1) : std::string();
            Log("order: %s%s%s", o.c_str(), gSet.target.empty() ? "" : " ", gSet.target.c_str());
        }
    } else if (k == "leave") { gSet.leave = true; Log("order: leave the game"); }
    else if (k == "adapt" && (v == "on" || v == "off")) {
        gSet.adapt = v == "on" && !gSet.parity;              // ((v150-host beta 16) the parity mode: the level as set)
        Log("order: live level %s%s", gSet.adapt ? "on" : "off", v == "on" && gSet.parity ? " (the parity mode keeps the level as set)" : "");
    }
    else if (k == "aim" && (v == "direct" || v == "pad")) {   // ((v150-host beta 18) the direct aim, live)
        gSet.aim = v == "direct" ? 1 : 0;
        Log("order: aim %s", v.c_str());
        if (gSet.aim) Log("aim: %s", DirectAimSaid().c_str());
    }
    else if (k == "loadout" && !v.empty() && v.size() < 32) { gSet.loadout = v; Log("order: weapons %s (the next spawn)", v.c_str()); }
    else Log("order not understood: %s", line.c_str());
}

}  // namespace in

using namespace in;

void Log(const char* fmt, ...) {
    char b[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    char line[1100];                                     // one write a line (stdout unbuffered: printf wrote a character at a time)
    int n = snprintf(line, sizeof line, "doll f%lld: %s\n", gFrame, b);
    if (n <= 0) return;
    if (n >= (int)sizeof line) { n = (int)sizeof line - 1; line[n - 1] = '\n'; }
    fwrite(line, 1, (size_t)n, stdout);
}

bool Enabled() { return gOn; }
bool Control() { return gControl; }
const Pad& CurrentPad() { return gPad; }

void Init() {
    const char* d = getenv("MGO1_DOLL");
    if (!d) return;
    gOn = true;
    gDir = d;
    gProbe = getenv("MGO1_DOLL_PROBE") ? atoi(getenv("MGO1_DOLL_PROBE")) : 0;
    gDebug = getenv("MGO1_DOLL_DEBUG") ? atoi(getenv("MGO1_DOLL_DEBUG")) : 0;
    gObserve = getenv("MGO1_DOLL_OBSERVE") != nullptr;
    gTrack = getenv("MGO1_DOLL_TRACK") ? atoi(getenv("MGO1_DOLL_TRACK")) : 0;
    gMotion = getenv("MGO1_DOLL_MOTION") ? std::max(0, atoi(getenv("MGO1_DOLL_MOTION"))) : 0;
    gOldWays = getenv("MGO1_DOLL_OLD_WAYS") != nullptr;
    gOldFight = getenv("MGO1_DOLL_OLD_FIGHT") != nullptr;
    if (const char* l = getenv("MGO1_DOLL_LEARN")) {    // (the map knowledge shared by the server's dolls)
        gLearnDir = l;
        while (!gLearnDir.empty() && (gLearnDir.back() == '\\' || gLearnDir.back() == '/')) gLearnDir.pop_back();
        if (!gLearnDir.empty()) CreateDirectoryA(gLearnDir.c_str(), nullptr);
    }
    ps2world::SetMessageHook(&OnMessage);
    gSeed ^= GetTickCount() ^ (GetCurrentProcessId() * 2654435761u);   // (dolls started within the same tick)
    if (!gSeed) gSeed = 0x2545F491u;
    CreateDirectoryA(gDir.c_str(), nullptr);
    ReadIni();
    if (const char* g = getenv("MGO1_DOLL_GAME")) if (gSet.game.empty()) gSet.game = g;   // tests: the game without an ini
    // ((v150-host beta 16) MGO1_DOLL_PARITY=vdoll (or 1, on; 0 off) over the ini's parity=: the difficulty plays by the virtual doll's
    // level of that number (doll_brain.cpp kVdollLevel), the live level off (a virtual doll's level does not move)
    if (const char* e = getenv("MGO1_DOLL_PARITY")) gSet.parity = !strcmp(e, "vdoll") || !strcmp(e, "1") || !strcmp(e, "on") ? 1 : 0;
    if (gSet.parity) gSet.adapt = false;
    // ((v150-host beta 18) MGO1_DOLL_AIM=direct over the ini's aim=: the direct aim (anything else: the pad)
    if (const char* e = getenv("MGO1_DOLL_AIM")) gSet.aim = !strcmp(e, "direct") ? 1 : 0;
    ReadDollNames();
    Log("on (status in %s)%s; difficulty %d, team %s, live level %s%s%s", gDir.c_str(), gProbe ? ", probe" : "", gSet.difficulty, gSet.team.c_str(),
        gSet.adapt ? "on" : "off", gLearnDir.empty() ? "" : "; the maps learned in ", gLearnDir.c_str());
    if (gSet.parity) Log("parity: %s", ParitySaid().c_str());
    if (gSet.aim) Log("aim: %s", DirectAimSaid().c_str());
}

void Frame(long long frame) {
    if (!gOn) return;
    gFrame = frame;
    gLastPad = gPad; gLastControl = gControl;
    gPad = Pad();
    gControl = false;
    // the map: a new map page starts its mesh
    const std::string page = ps2world::CurrentPage();
    const bool mapPage = page.size() == 5 && page[0] == 'n' && page[1] == '0';
    static unsigned mapGen = 0, learnGen = 0;
    // an extra map on this stage (docs/EXTRA_MAPS.md); (v150-host beta 22, sync) the page and the extra map of one stage
    // (ps2world::StageNow): none while a stage of another page loads (a stage of the same page: at its request), never the old
    // page with the new stage's extra map
    std::string stage;
    int extra = 0;
    const bool stageNow = mapPage && ps2world::StageNow(&stage, &extra);
    const std::string key = extra > 12 ? stage + "_x" + std::to_string(extra) : stage;
    if (stageNow && (!gMap || gMap->key != key)) {
        gMap = std::make_shared<MapState>();
        gMap->page = stage; gMap->extra = extra; gMap->key = key;
        gMap->state = 1;
        std::thread(BuildMap, gMap).detach();
        Log("map %s: building its navigation", key.c_str());
        gF = Follow(); gMeshTraps.clear(); gCrossFails.clear(); NewWays(); gEscapeWay.clear(); gEs = EscapeRun(); gLearnKnown.clear(); ++mapGen;
        BrainNewMap();
    }
    if (gMap && gMap->state >= 2 && !gMap->log.empty()) {   // (its lines one by one)
        const std::string& l = gMap->log;
        for (size_t s = 0; s < l.size();) {
            size_t e = l.find('\n', s);
            if (e == std::string::npos) e = l.size();
            if (e > s) Log("%s", l.substr(s, e - s).c_str());
            s = e + 1;
        }
        gMap->log.clear();
    }
    if (MapReady() && learnGen != mapGen) { learnGen = mapGen; LearnApply(); }   // (the map knowledge of earlier games)
    See();
    SeePlayers();
    PresenceFrame();
    SeeRoom();
    ReadCamera();
    SteerObserve();
    if (frame % 30 == 0) ReadCommands();
    if (frame % 600 == 0) ReadDollNames();
    if (frame % 60 == 0) WriteStatus();
    if (!gLearnPend.empty() && frame - gLearnTryAt >= 60) LearnFlush();   // (v141 beta 4: the learned lines the lock kept)
    ActJournal();
    if (gDebug && mapPage) {
        if (frame % 15 == 0) ActorDiff();
        if (frame % 30 == 0) StateLog();
        if (frame % 600 == 0) { RoomLog(); PlayersLog(); }
        if (frame % 1800 == 900) ItemsLog();
    }
    if (gTrack > 0 && mapPage && frame % gTrack == 0) TrackLog();
    { static const bool watch = getenv("MGO1_DOLL_WATCH") != nullptr; if (watch && mapPage) WatchLog(); }   // (beta 19, measures)
    // (v150-host beta 21, measures) MGO1_DOLL_FACINGS=1: each frame the facing (degrees) and place of every other player's
    // copy on this screen, as its game turns it between that player's records (tools/dev150/vdoll_moves.py --screen)
    { static const bool facings = getenv("MGO1_DOLL_FACINGS") != nullptr;
      if (facings && mapPage) {
          std::string s;
          char b[64];
          for (const Player& p : gPlayers) {
              if (!p.obj || p.local) continue;
              snprintf(b, sizeof b, " %d:%.1f:%.0f:%.0f:%d", p.id, p.facing * 57.29578f, p.pos[0], p.pos[2], p.alive ? 1 : 0);
              s += b;
          }
          if (!s.empty()) Log("facings:%s", s.c_str());
      } }
    if (gSet.leave && !gObserve && !gProbe) { Log("leaving the game (ordered)"); ExitNow(0); }
    if (!mapPage && (!gProbe || gProbe >= 7)) {      // the lobby (an observer too: it finds its game the same way)
        if (gWasInMatch && page == "ntmenu2" && !gObserve) { Log("back in the lobby after a match: the doll starts again"); ExitNow(3); }
        // (v141 beta 12) a join that failed (1059): the doll starts again at once (it stayed on the message until the
        // manager's timeout, minutes on a slow server)
        long long errAt = -1;
        const std::string err = ps2world::LastErrorText(&errAt);
        if (!gObserve && !gProbe && errAt >= 0 && frame - errAt < 600 && err.find("( 1059 )") != std::string::npos) {
            Log("lobby: the join failed (1059): the doll starts again");
            ExitNow(4);
        }
        if (!LobbyFrame()) Act(page == "ntmenu2" ? "lobby" : "loading");
        return;
    }
    if (gObserve) return;
    if (!gProbe && ChatFrame(mapPage)) return;           // ((v141 beta 6) the text chat open by mistake: closed first)
    if (ProbeFrame(mapPage)) return;
    if (!mapPage) { Act("loading"); return; }
    gWasInMatch = true;
    // the match lost: nothing received from the host for a minute (v133's audit: a joiner the host dropped
    // stood dead 11 minutes in a match frozen for it): the doll starts again
    static long long matchSince = -1;
    if (matchSince < 0) matchSince = frame;
    if (frame - matchSince > 3600 && !gProbe && ps2world::SecondsSinceRecv() > 60.0) {
        Log("match: nothing received from the host for %.0f s: the doll starts again", ps2world::SecondsSinceRecv());
        ExitNow(5);
    }
    // the match frozen for it, some traffic still coming (v136's audit, capture on Graniny Gorki Lab: every
    // player's place unchanged for minutes, its own stick pushed, the host gone on to its next map): nobody
    // moved 90 s, its own stick pushed 10 s of them for nothing, the doll starts again
    {
        static uint64_t sig0 = 0;
        static long long sameSince = -1;
        static int pushes = 0;
        uint64_t sig = 1469598103934665603ULL;
        for (const Player& p : gPlayers)
            for (int k = 0; k < 3; ++k) { sig ^= (uint64_t)(int64_t)std::lround(p.pos[k] / 100.f); sig *= 1099511628211ULL; }
        const bool pushed = gLastControl && (std::abs((int)gLastPad.sticks[2] - 0x80) > 0x30 || std::abs((int)gLastPad.sticks[3] - 0x80) > 0x30);
        if (sig != sig0 || gPlayers.size() < 2 || sameSince < 0) { sig0 = sig; sameSince = frame; pushes = 0; }
        else if (pushed) ++pushes;
        if (!gProbe && frame - sameSince > 5400 && pushes > 600) {
            Log("match: nobody moved for %.0f s, its own stick pushed: the game frozen for it, the doll starts again", (frame - sameSince) / 60.0);
            ExitNow(5);
        }
    }
    if (!MapReady()) { Act("loading"); return; }
    // a life's frames: the menu's frame while frozen before its first half second free (the spawn and its
    // wait, the round's start: a free frame or two may come between); frozen after it (knocked down, held
    // by a CQC grab: the word 7's 0x1000000 a moment), the brain's frame still (the menu's frame is a
    // spawn's: it forgets the life's runs, presses Circle)
    static bool freeThisLife = false;
    static int phaseSeen = -1, freeRun = 0;
    if (!gMe.alive || gRoom.phase != phaseSeen) freeThisLife = false;
    phaseSeen = gRoom.phase;
    if (gMe.alive && !gMe.frozen) { if (++freeRun >= 30) freeThisLife = true; } else freeRun = 0;
    const bool brain = gMe.alive && (!gMe.frozen || freeThisLife || (gMe.bits[0] & 0x1A000000));
    static bool inLife = false;
    if (brain && !inLife) { NewWays(); StopPath(); gEs = EscapeRun(); }   // (a life's own ways: the way before it planned again; its escape afresh)
    LifeWatch(brain);
    inLife = brain;
    if (!HangFrame(brain)) {                          // (hanging from a ledge: its inputs, not the brain's)
        if (brain) BrainFrame();
        else MenuFrame();
        if (brain) PostureHold();                     // (a posture's Cross held its 3 frames)
    }
    MotionFrame(brain);
}

}  // namespace doll
