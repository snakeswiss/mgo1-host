// stagekey.h - (v150-host beta 22, sync) the map a game's per-map data follow: the page and the extra map of ONE stage.
//
// A map's per-map data (the virtual dolls' collision and navigation, their respawn points, a playable doll's mesh) are
// keyed by the stage's page (n003a) and the extra map served on it (13 SILO ENTRANCE: n003a_x13). Beta 21 took the two
// from two clocks: the page from the page's code (it changes when the new page's code first runs, NativeOverlayCheck) and
// the extra map from the stage's request (0x101B28, ExtraStageLoad, some 20 to 30 frames earlier). Between them the pair
// was the OLD page with the NEW stage's extra map (n033a_x13 after Brown Town: Brown Town's .geom and respawn points read
// for SILO ENTRANCE, 6 bench runs of beta 17 to 21), or after an extra map the old page with no extra map (n003a: CITY UNDER
// SIEGE's built while the next stage loads). The tracker keeps the stage asked (its page, the extra map served for it) and
// gives a pair only once the page whose code runs is that stage's page: none while a stage of another page loads; between
// two stages of one page (n003a: CITY UNDER SIEGE, SILO ENTRANCE, REX HANGAR; n022a: Mountaintop, STRUT E) the page's code
// is the same page's, so the new stage's pair comes at its request, as in beta 21 (the right stage's, never a mixed one).
// Included by ps2world.cpp (inside its namespace) and by tests/stage_key_test.cpp.
#pragma once
#include <string>

namespace stagekey {

inline bool MapPage(const std::string& p) { return p.size() == 5 && p[0] == 'n' && p[1] == '0'; }   // n001a .. n033a

struct Tracker {
    std::string asked;          // the map page asked last (empty: none yet, or a build that does not call Asked)
    int askedExtra = 0;         // the extra map (13 ..) served for it, 0 none
    // a map page's load asked (its name), with the extra map served for it (0: its own stage); another page (a rules page,
    // the lobby's) changes nothing
    void Asked(const std::string& page, int extra) {
        if (!MapPage(page)) return;
        asked = page; askedExtra = extra > 12 ? extra : 0;
    }
    // the stage now: its page and extra map when the page whose code runs (current) is the stage asked; false while a stage
    // of another page loads (the old page's code still runs) or on a page that is no map's; a stage of the same page: its pair
    // at its request (current is already its page). legacyExtra: the extra map as beta 21 read it, taken only when no stage
    // was ever asked (a build without the request's hook)
    bool Now(const std::string& current, int legacyExtra, std::string* page, int* extra) const {
        if (!MapPage(current)) return false;
        if (!asked.empty() && current != asked) return false;
        *page = current;
        *extra = asked.empty() ? legacyExtra : askedExtra;
        return true;
    }
};

// the key of a map's data: the page, and "_x<n>" for an extra map (vdoll_body.cpp, doll.cpp)
inline std::string Key(const std::string& page, int extra) { return extra > 12 ? page + "_x" + std::to_string(extra) : page; }

}  // namespace stagekey
