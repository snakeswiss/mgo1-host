// The PLAYABLE DOLL's navigation mesh (docs/PLAYABLE_DOLL.md 2): Recast voxelizes the map's collision
// faces (doll_geom.h) into tiles, the clearance above each walkable cell gives its posture (standing,
// crouching, crawling: the vents and the low passages), the ledges get one-way links down (a drop) and
// up (a climb with the action button), the thin obstacles open above between two floors links both ways
// (a fence, a low wall, a railing, the lab's windows: over with the action button, or a roll over a low
// one: the path follower's choice, doll.cpp), the ladders a link both ways between their foot and their
// top, and Detour plans the paths.
#pragma once
#include "doll_geom.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

namespace doll {

// (AREA_ROLL: a crossing always taken with a roll; the mesh makes none (probe 24's measures), the probe forces it;
// AREA_STEPS (v137): stairs of small blocks, each climbed with the action button)
enum NavArea : uint8_t { AREA_STAND = 1, AREA_CROUCH = 2, AREA_CRAWL = 3, AREA_DROP = 4, AREA_CLIMB = 5, AREA_LADDER = 6, AREA_VAULT = 7, AREA_ROLL = 8, AREA_STEPS = 9 };
enum NavFlag : uint16_t { NF_STAND = 1, NF_CROUCH = 2, NF_CRAWL = 4, NF_DROP = 8, NF_CLIMB = 16, NF_LADDER = 32, NF_VAULT = 64, NF_ROLL = 128, NF_STEPS = 256 };

struct NavConfig {
    float cs = 125.f, ch = 50.f;             // voxel size (mm)
    float radius = 375.f;                    // the player's radius
    float standHeight = 1800.f;              // clearance to stand, to crouch, to crawl
    float crouchHeight = 1100.f;
    float crawlHeight = 550.f;
    float climb = 350.f;                     // a step walked up
    float slope = 50.f;                      // degrees
    float dropMax = 3500.f;                  // a ledge dropped down (one way)
    float climbUpMax = 1300.f;               // a ledge climbed up with the action button
    float climbFacesMax = 1200.f;            // the same measured on the faces at the wall's foot (the log's 1.0 m climbed, a 1.4 m edge never)
    float crossSillMax = 1550.f;             // an obstacle's top (a fence, a wall, the lab's windows) crossed at most
    float openingMin = 800.f;                // (the openings under a lintel measured: none crossed)
    float thickMax = 350.f;                  // an obstacle crossed: its thickness at most (thicker: a table, a crate)
    float stoneMin = 560.f;                  // (v137) a block's top stood on: this wide at least (a stone of the stairs)
    int tileCells = 256;
    // (v150-host beta 24, snakehouse) the doorways' panels: a wall face whose attribute word has 0x100000 (kDoorway; 18
    // faces on the 15 maps, on n021a, n022a and n024a, each a panel 1 to 8 m wide filling a doorway or a passage) is
    // left out of the walking (the tiles, their links, the stairs) and (snakehouse3) of the lines (Clear, FirstHit): a player
    // walks through it and his bullets and the game's auto aim pass it (measured). Off: as before (the playable dolls' meshes);
    // the virtual dolls' bodies set it (MGO1_VDOLL_STAIRS, docs/VIRTUAL_DOLL.md 43)
    bool doorways = false;
    // (v150-host beta 25, meshmargin) a player's distance: after Recast's erosion as before, each tile's standing and crouching
    // places nearer than 212 mm to a wall of the stage's faces (a face too steep to walk, from 0.4 m over the floor to the posture's
    // height) are found and the cells round them that are nearer a wall go, the tile's polygons made again (doll_nav.cpp
    // MarginFaults, MarginCut): the erosion by voxels missed such walls near the objectives (the floor given back), under faces
    // that do not reach the floor (a rail, a leaning rock) and round thin posts, and a doll stood there nearer a wall than a
    // player's body can (a real client's is held 211 mm off a wall). Everywhere else the mesh is beta 25's. Off: as before (the
    // playable dolls' meshes); the virtual dolls' bodies set it (MGO1_VDOLL_MESH_MARGIN, docs/VIRTUAL_DOLL.md 50). (In the padding
    // after `doorways`: the struct's size and its other fields' places are unchanged)
    bool margin = false;
};
constexpr uint32_t kDoorway = 0x100000u;
#define DOLL_NAV_DOORWAYS 1                      // (v150-host beta 24, tests: NavConfig::doorways is there)
#define DOLL_NAV_MARGIN 1                        // (v150-host beta 25, tests: NavConfig::margin and NavMesh::WallGap are there)

struct Waypoint {
    float p[3];
    uint8_t area;                            // the area from this point on (NavArea)
    bool link;                               // a drop or a climb starts here (to the next point)
};

class NavMesh {
public:
    NavMesh();
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    // the mesh of a map: tiles, postures, links; log: what was built
    bool Build(const Geom& g, const NavConfig& c, std::string& log);
    // (v141 beta 4) the mesh as built (its links left out included) and its log line, in bytes; read back over the same
    // map's geometry (false: another build of this file, or the bytes damaged)
    bool Save(std::string& out, const std::string& log) const;
    bool Load(const Geom& g, const std::string& in, std::string& log);
    bool Ready() const { return query_ != nullptr; }

    // the point of the mesh nearest to p (within ext mm across, 2 * ext up and down); its area
    bool Nearest(const float* p, float ext, float* out, uint8_t* area = nullptr) const;
    // a path (corners) from `from` to `to`; false when either is off the mesh or no path exists
    bool Path(const float* from, const float* to, std::vector<Waypoint>& out, float* length = nullptr) const;
    // the same out of the polygons listed (another way through the map), its polygons (start to end) in corridor
    bool PathAvoiding(const float* from, const float* to, const std::vector<unsigned>& avoid, std::vector<Waypoint>& out, float* length,
                      std::vector<unsigned>* corridor) const;
    bool PolyCenter(unsigned ref, float* out) const;   // the middle of a polygon's corners
    // (v150-host beta 15) the polygons within r across (1.5 m up and down) of p, added to out (PathAvoiding's list)
    void PolysNear(const float* p, float r, std::vector<unsigned>& out) const;
    // a random reachable point from `from` (within radius mm of it when radius > 0)
    bool RandomPoint(const float* from, float radius, float* out, unsigned (*rnd)()) const;
    // walking along the mesh from `from` toward `to` is free of walls (a straight line on the ground); standing:
    // on the standing floor only (no crouching nor crawling polygon)
    bool Walkable(const float* from, const float* to, bool standing = false) const;
    float ClearAlong(const float* from, float dx, float dz, float maxDist, bool standing = false) const;   // the walkable way straight on (mm, maxDist at most)
    uint8_t AreaAt(const float* p) const;
    const NavConfig& Config() const { return cfg_; }
    int Tiles() const { return tiles_; }
    int Polys() const { return polys_; }
    int Links() const { return links_; }
    // a link (a drop, a climb, a ladder, a vault, a roll): its start on the ledge's edge (a ladder's foot,
    // one side of a crossing), its end on the other floor (the ladder's top, the other side), its area
    int LinkCount() const { return (int)linkArea_.size(); }
    bool LinkAt(int i, float* a, float* b, uint8_t* area) const;
    bool LinkShape(int i, float* sill, float* lintel) const;   // a crossing's obstacle: its top, its lintel (0: open above)
    float CrossingTop(const float* a, const float* b) const;    // the obstacle's top of the crossing a <-> b (-1: none)
    // the map's ladders (doll_geom.h), those linked in the mesh; the one whose foot or top is nearest p
    // (within r), and whether p is at its top
    const std::vector<Ladder>& Ladders() const { return ladders_; }
    const Ladder* LadderAt(const float* p, float r, bool* atTop = nullptr) const;
    void DisableLadder(const Ladder& l);          // its link left out of the paths
    void DisableLadder(int number);               // (the same ladder in another mesh: by its number)
    void EnableLadder(int number);                // (v137) back in the paths
    // (v137) a ladder's cost in the paths: its own (its time from its height, no shot meanwhile) times k (1: as built)
    void SetLadderCost(int number, float k);
    float LadderCost(int number) const;          // its k (1 as built, 0 no such ladder)
    bool DisableLink(const float* a, const float* b, float tol = 300.f);   // a crossing's link (either way) left out of the paths
    bool EnableLink(const float* a, const float* b, float tol = 300.f);    // (v137) such a link back in the paths
    // (v137) the stairs of small blocks (each top too small for the mesh): a link both ways between a floor and a higher
    // one through the blocks' tops (the stones, from a's side), each step 0.35 m to climbFacesMax
    struct Steps { float a[3], b[3]; std::vector<std::array<float, 3>> stones; };
    const std::vector<Steps>& AllSteps() const { return steps_; }
    const Steps* StepsAt(const float* a, const float* b, bool* reversed, float tol = 400.f) const;
    // the mesh's polygons for pictures: triangles (x y z each), their areas, their component (the
    // polygons joined by edges and links, either way)
    void Triangles(std::vector<float>& tris, std::vector<uint8_t>& areas, std::vector<int>* components = nullptr) const;
    int ComponentAt(const float* p) const;       // -1 off the mesh
    int ComponentSize(int c) const { return c >= 0 && c < (int)compSize_.size() ? compSize_[c] : 0; }
    int ComponentCount() const { return (int)compSize_.size(); }
    // the map's collision itself (every face, not the mesh): nothing between a and b (a line of sight,
    // a shot); the first surface below p within maxDown (the ground under the feet), -1e30 if none
    // (one thread at a time: the lines mark the faces they met in the mesh itself)
    bool Clear(const float* a, const float* b) const;
    // the first surface between a and b: its fraction along a-b and its normal (toward a)
    bool FirstHit(const float* a, const float* b, float* t, float* normal) const;
    float GroundBelow(const float* p, float maxDown) const;
    // (v150-host beta 25, meshmargin) the distance across (x z) from p, a point of the floor, to the nearest wall of the mesh's
    // faces (too steep to walk; the walk-through layer and, with NavConfig::doorways, the doorways' panels left out) between
    // p's height + lo and + hi, looked for within maxR (more than maxR: none that near)
    float WallGap(const float* p, float lo, float hi, float maxR) const;

private:
    dtNavMesh* mesh_ = nullptr;
    dtNavMeshQuery* query_ = nullptr;
    dtQueryFilter* filter_ = nullptr;
    dtQueryFilter* standFilter_ = nullptr;           // (the standing floor only)
    bool PathWith(const float* from, const float* to, const dtQueryFilter* f, std::vector<Waypoint>& out, float* length,
                  std::vector<unsigned>* corridor) const;
    NavConfig cfg_;
    int tiles_ = 0, polys_ = 0, links_ = 0;
    std::vector<int> compSize_;
    std::vector<float> linkPts_;                 // a xyz, b xyz per link
    std::vector<uint8_t> linkArea_;
    std::vector<float> linkShape_;               // a crossing's sill and lintel per link (0 for the others)
    std::vector<Ladder> ladders_;                // their foot and top as linked (on the mesh)
    std::vector<float> ladderBase_, ladderK_;    // (v137) each linked ladder's own cost, its multiplier (by index)
    std::vector<Steps> steps_;                   // (v137) the stairs linked
    void Components();
    void Finish(dtNavMesh* mesh);                // the mesh in use: its queries, its costs, its parts
    int CompOf(uint64_t ref) const;
    std::vector<std::vector<int>> comp_;         // per tile index, per polygon
    struct SoupHolder;
    SoupHolder* soup_ = nullptr;                 // the collision triangles, for Clear and GroundBelow
};

}  // namespace doll
