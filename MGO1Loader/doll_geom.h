// The PLAYABLE DOLL's view of a map: the collision faces of the stage's .geom (docs/PLAYABLE_DOLL.md 2).
//
// The PS2 geom (MGO1's native format): a chunk table (12 bytes an entry: size, offset, id), chunk 1
// the grid (base xyz, steps, counts, radix, descriptor table), a radix record of 6 bytes per cell
// (s16 first descriptor, 8 type nibbles: slot 1 the solid collision, slot 2 region volumes, slot 4
// small action volumes), 24 byte descriptors walked until the last bit (dtype & 1), each one's chain
// of entries (flag, next and child in 16 byte units): 0x2012 LEAF (faces), 0x042004 FIELD (a box and
// its LEAF child), 0x042005 object instances (112 byte units: box, attribute, centre, a 4x4 matrix,
// the object's first descriptor in chunk 5). A LEAF's faces are 16 bytes from +0x18: a u64 of six
// 10 bit indices (in 8 byte units from the face) to the pool: 4 vertices (i16), the normal (i16 /
// 32000), the translation (f32 xyz); then the face's attribute word. Chunk 7 holds the spawns and
// goals ([instance][type hi][type lo][0] flags, xyz).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace doll {

struct Face {
    float v[4][3];          // world coordinates (mm), the fourth vertex equal to the first on a triangle
    float n[3];             // unit normal
    uint32_t attr;          // the face's attribute word (low 28 bits flags, high nibble the material)
    bool object;            // from a placed object (chunk 5) rather than the grid
};

enum SpawnKind : uint8_t {
    SP_UNKNOWN, SP_DM, SP_TDM_FIRST_RED, SP_TDM_FIRST_BLUE, SP_TDM_RED, SP_TDM_BLUE, SP_TCM_RED, SP_TCM_BLUE,
    SP_TCM_GOAL, SP_KEROTAN, SP_CMPLUS_GOAL, SP_SR_TARGET, SP_CAMERA, SP_OBJECT_RED, SP_OBJECT_BLUE, SP_KIND_COUNT
};
const char* SpawnKindName(SpawnKind k);
// (v150-host beta 21) SNEAKING's soldiers' starts and respawns (the stage script's list 1881: 0DC68A to 0DC690, seven
// points) are in the spawns under SP_UNKNOWN, with their hash: a kind no other reader of the list takes (the navigation's
// regions, the playable doll's traps and log), so nothing but RespawnKinds' SNEAKING changes with them
constexpr SpawnKind SP_SNE_SOLDIER = SP_UNKNOWN;

struct Spawn {
    uint32_t hash;          // instance | type hi << 8 | type lo << 16
    SpawnKind kind;
    float p[3];
};

struct Volume {             // the grid's other slots: region volumes (slot 2), small solid boxes (slot 4: trees, posts)
    int slot;
    uint32_t family;        // 0x802023 / 0x802043 a turned box, 0x802024 / 0x802044 a box along the axes
    uint32_t hash, attr;
    float half[3], centre[3];
    float m[16];            // local -> world (row vectors: rows 0..2 the axes, row 3 the translation)
};

struct Geom {
    std::vector<Face> faces;
    std::vector<Spawn> spawns;
    std::vector<Volume> volumes;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};   // the faces' extent
    float gridBase[3] = {0, 0, 0}, gridStep[3] = {0, 0, 0};
    int gridN[3] = {0, 0, 0};
    int objectUnits = 0;
};

// the faces, spawns and volumes of a PS2 .geom; false (err) when it is not one
bool DecodeGeom(const uint8_t* data, size_t size, Geom& out, std::string& err);

// the game's name hash (GCL strcode, 24 bits): the volumes' and the scripts' names
uint32_t StrCode(const char* s);

// (v150-host beta 19) the kinds of point where a player of team (0 red, 1 blue, 0xFE a spectator's view) appears when he
// spawns again or joins during a round of rule (0 deathmatch .. 6 base capture), in the order tried (the first kind the
// map has); their number. The round's start is the host's game's own (the player's object), not chosen here.
int RespawnKinds(int rule, int team, SpawnKind out[4]);

// the ladders: the stage's script sets a trap on two region volumes of the grid (slot 2) named
// "ladder<NNN>_0" / "ladder<NNN>_1" (or "_01" / "_02"): the one at the foot, the one at the top.
// The trap's condition: the player standing in the volume, facing the ladder (within 45 degrees),
// and the action button (Triangle): up from the foot facing the top's side, down from the top facing
// back. dir: the facing that climbs up (radians, atan2 x z), from the foot's box toward the top's
struct Ladder {
    int number = 0;
    float foot[3], footHalf[3];              // the foot's volume: its centre, its half sizes (along the axes)
    float top[3], topHalf[3];
    float dir = 0;
};
std::vector<Ladder> FindLadders(const Geom& g);

}  // namespace doll
