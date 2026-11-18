// (v150-host beta 22, sync) the key of a map's per-map data (MGO1Loader/stagekey.h): the virtual dolls' mesh and respawn
// points, a playable doll's mesh, built when the key changes (vdoll_body.cpp Frame, doll.cpp Frame). Replays what the host's
// game does at a stage change, as its log shows it (runs V17..V21_tdm_maps_11_15, B3_maps_res, B3_maps_tsne): the stage's
// request (0x101B28: ExtraStageLoad sets the extra map served), the rules page's code in the page area for some frames when
// the rules page loads again (r_sna01_n), then the new page's code (NativeOverlayCheck: the page's name). Checks, for every
// stage of each rotation: no build of a key whose page is not the stage of its extra map (beta 21: n033a_x13, Brown Town's
// .geom read for SILO ENTRANCE), no build of another stage's key while a stage loads (beta 21: n003a, CITY UNDER SIEGE's,
// after SILO ENTRANCE while BROWN TOWN loads), the stage's own key once its page's code runs, the disc maps' rotations (the
// owner's) built exactly as beta 21 built them, and (sync2) between two stages of one page (n003a: CITY UNDER SIEGE, SILO
// ENTRANCE, REX HANGAR; n022a: Mountaintop, STRUT E) the new stage's key at its request, as beta 21 gave it: the page's code
// is already its page, so "none while a stage loads" holds for a stage of another page only.
//     g++ -O1 -std=c++17 -I MGO1Loader tests/stage_key_test.cpp -o /tmp/stage_key_test && /tmp/stage_key_test
//     /tmp/stage_key_test --beta21     (the same checks on beta 21's pairing: the page's name, the request's extra map)
#include "stagekey.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int gFails = 0, gOks = 0;
static void Check(const char* what, bool ok, const std::string& detail = "") {
    printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", what, ok || detail.empty() ? "" : "  (", ok || detail.empty() ? "" : (detail + ")").c_str());
    ok ? ++gOks : ++gFails;
}

static const char* const kStages[12] = {"n001a", "n002a", "n003a", "n011a", "n012a", "n013a", "n021a", "n022a", "n024a", "n031a", "n032a", "n033a"};
static std::string StageOf(int map) { return map <= 12 ? kStages[map - 1] : map == 15 ? "n022a" : "n003a"; }   // 13, 14 on n003a, 15 on n022a

struct Game {                                    // the host's game as the per-map data see it
    bool beta21 = false;
    std::string page = "ntmenu2";                // the page whose code runs (CurrentPage)
    int served = 0;                              // the extra map the stage asked last is served as (ExtraMapsLoaded)
    stagekey::Tracker t;
    std::string key;                             // the key of the data built last
    std::vector<std::string> builds;             // every build, in order
    bool Now(std::string* p, int* e) const {
        if (!beta21) return t.Now(page, served, p, e);
        if (!stagekey::MapPage(page)) return false;  // beta 21: VdollMapNow, the page's name and ExtraMapNow
        *p = page; *e = served;
        return true;
    }
    void Frame() {                               // vdoll_body.cpp Frame: a new key, a new build
        std::string p; int e = 0;
        if (!Now(&p, &e)) return;
        const std::string k = stagekey::Key(p, e);
        if (k != key) { key = k; builds.push_back(k); }
    }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
};

// one stage change: the request, some frames of the old page's code (the bench's 20 to 30), the rules page's code for some
// frames when the rules page loads again, then the new page's code and some frames of it
static void Load(Game& g, int map, bool rulesPage, int lag = 25) {
    const std::string page = StageOf(map);
    g.served = map > 12 ? map : 0;               // ExtraStageLoad (ExtraMapsStageLoad: the map's own image served)
    if (!g.beta21) g.t.Asked(page, g.served);    // StageAsked, after it
    g.Frames(lag);
    if (rulesPage) { g.page = "r_sna01_n"; g.Frames(15); }
    if (g.page != page) g.page = page;           // NativeOverlayCheck: the new page's code (the same page: no change seen)
    g.Frames(60);
}

static bool Mixed(const std::string& k) {        // a key whose page is not the stage of its extra map
    const size_t x = k.find("_x");
    return x != std::string::npos && StageOf(atoi(k.c_str() + x + 2)) != k.substr(0, x);
}

struct Step { int map; bool rulesPage; };

static void Rotation(const char* name, bool beta21, const std::vector<Step>& rot, int laps = 2) {
    Game g; g.beta21 = beta21;
    std::vector<std::string> mixed, extra, wrongEnd;
    for (int lap = 0; lap < laps; ++lap)
        for (const Step& s : rot) {
            const size_t before = g.builds.size();
            Load(g, s.map, s.rulesPage);
            const std::string want = stagekey::Key(StageOf(s.map), s.map > 12 ? s.map : 0);
            for (size_t i = before; i < g.builds.size(); ++i) {
                if (Mixed(g.builds[i])) mixed.push_back(g.builds[i] + " for map " + std::to_string(s.map));
                else if (g.builds[i] != want) extra.push_back(g.builds[i] + " while map " + std::to_string(s.map) + " loads");
            }
            if (g.key != want) wrongEnd.push_back("map " + std::to_string(s.map) + ": " + g.key);
        }
    auto list = [](const std::vector<std::string>& v) {   // (the first 6, and how many)
        std::string s;
        for (size_t i = 0; i < v.size() && i < 6; ++i) s += (s.empty() ? "" : ", ") + v[i];
        return v.size() > 6 ? s + " ... " + std::to_string(v.size()) + " in all" : s;
    };
    char w[256];
    snprintf(w, sizeof w, "%s: no build of a page with another stage's extra map", name); Check(w, mixed.empty(), list(mixed));
    snprintf(w, sizeof w, "%s: no build of another stage's key while a stage loads", name); Check(w, extra.empty(), list(extra));
    snprintf(w, sizeof w, "%s: each stage's own key once its page runs", name); Check(w, wrongEnd.empty(), list(wrongEnd));
}

int main(int argc, char** argv) {
    const bool beta21 = argc > 1 && !strcmp(argv[1], "--beta21");
    printf("stage_key_test: %s\n", beta21 ? "beta 21's pairing (the page's name, the request's extra map)" : "stagekey.h (one stage's page and extra map)");
    // the bench's rotation 1:11, 1:12, 1:13, 1:14, 1:15 (V21_tdm_maps_11_15): Brown Town to SILO ENTRANCE with no rules page
    // loaded again (its rules page holds none of the map's files), REX HANGAR and STRUT E with theirs, back to High Ice
    Rotation("tdm 11 to 15 (the bench's)", beta21, {{11, true}, {12, false}, {13, false}, {14, true}, {15, true}});
    // after an extra map, a disc map of another page with no rules page between (CITY UNDER SIEGE's key while BROWN TOWN loads)
    Rotation("13 then 12, 15 then 1", beta21, {{13, true}, {12, false}, {15, true}, {1, false}});
    // the same page, another map: 3 and 13 and 14 on n003a, 8 and 15 on n022a
    Rotation("the same page's maps", beta21, {{3, true}, {13, false}, {14, false}, {3, false}, {8, true}, {15, false}, {8, false}});
    // (sync2, the review's S1) a stage of the same page, no rules page between: its key the first frame after its request (the
    // page's code is already its page; beta 21 the same), one build, never a mixed key
    {
        const int pairs[5][2] = {{3, 13}, {13, 14}, {14, 3}, {8, 15}, {15, 8}};
        std::string bad;
        for (const auto& pr : pairs) {
            Game g; g.beta21 = beta21;
            Load(g, pr[0], true);
            const size_t n = g.builds.size();
            g.served = pr[1] > 12 ? pr[1] : 0;
            if (!beta21) g.t.Asked(StageOf(pr[1]), g.served);
            g.Frame();
            const std::string want = stagekey::Key(StageOf(pr[1]), g.served);
            if (g.builds.size() != n + 1 || g.key != want) bad += (bad.empty() ? "" : ", ") + std::to_string(pr[0]) + " to " + std::to_string(pr[1]) + ": " + g.key;
        }
        Check("a stage of the same page (3 to 13, 13 to 14, 14 to 3, 8 to 15, 15 to 8): its key at its request", bad.empty(), bad);
    }
    // every pair of maps in turn (225 changes), with and without a rules page
    {
        std::vector<Step> all;
        for (int a = 1; a <= 15; ++a) for (int b = 1; b <= 15; ++b) { all.push_back({a, (a + b) % 3 == 0}); all.push_back({b, (a * b) % 4 == 1}); }
        Rotation("every pair of maps", beta21, all, 1);
    }
    // the owner's rotation of 9 to 12 November (the disc's maps only): built as beta 21 built them, map for map
    {
        const std::vector<Step> own = {{7, true}, {2, true}, {3, true}, {12, true}, {9, true}, {10, true}, {11, true}, {1, true}, {4, true}, {6, true}, {5, true}, {8, true}};
        Rotation("the owner's rotation (disc maps)", beta21, own);
        if (!beta21) {
            Game a, b; b.beta21 = true;
            for (int lap = 0; lap < 2; ++lap) for (const Step& s : own) { Load(a, s.map, s.rulesPage); Load(b, s.map, s.rulesPage); }
            Check("the owner's rotation: the same builds as beta 21's", a.builds == b.builds);
            Game c, d; d.beta21 = true;                  // and with no rules page between (the same rule: no resident load)
            for (int lap = 0; lap < 2; ++lap) for (const Step& s : own) { Load(c, s.map, false); Load(d, s.map, false); }
            Check("the disc maps with no rules page: the same builds as beta 21's", c.builds == d.builds);
        }
    }
    // a build without the request's hook (no stage asked ever): beta 21's reading kept
    if (!beta21) {
        stagekey::Tracker t;
        std::string p; int e = -1;
        Check("no stage asked: the page and the extra map as beta 21 read them", t.Now("n003a", 13, &p, &e) && p == "n003a" && e == 13);
        Check("a page that is no map's: none", !t.Now("r_sna01_n", 0, &p, &e) && !t.Now("ntmenu2", 0, &p, &e));
        t.Asked("r_team_gl1_n", 0);
        Check("a rules page asked: the stage asked kept (none)", t.asked.empty());
        t.Asked("n022a", 15);
        Check("STRUT E asked: n022a's code running gives n022a with map 15", t.Now("n022a", 0, &p, &e) && p == "n022a" && e == 15);
        Check("STRUT E asked, Brown Town's code still running: none", !t.Now("n033a", 15, &p, &e));
    }
    printf("stage_key_test: %d ok, %d FAIL\n", gOks, gFails);
    return gFails ? 1 : 0;
}
