// vdoll_adapt_teamfoe_test.cpp - (logs12, 12 November) the team rules' part against a weaker real player
// (MGO1Loader/vdoll_adapt.h Options.teamFoe, Options.foeFloor, FoeLevel; a proposal, OFF by default: the owner's choice).
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_adapt_teamfoe_test.cpp -o t && ./t
// Checks: the default options change nothing (the same levels and parts against each foe as the call without options,
// in team deathmatch and in deathmatch); with teamFoe the two dolls facing a weak real player in team deathmatch play
// lower against him, never higher against anyone, and their level (the team's) is the same; deathmatch is untouched by
// teamFoe; FoeLevel keeps adapt_min as the floor unless foeFloor says lower, never under 1 nor over adapt_max.
// Before this branch it does not build (no Options, no FoeLevel in vdoll_adapt.h).
#include "vdoll_adapt.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace vdoll::adapt;
static int gFail = 0;
static void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++gFail; }

// a team deathmatch of 8 minutes: the real player (id 4, blue) dies 9 times for each kill, his allied doll (id 2, blue)
// wins, the two dolls facing him (ids 1 and 3, red) in between; or a deathmatch with the same counters
struct Sim {
    Ring rings[16];
    State st[3];
    int rule;
    explicit Sim(int r) : rule(r) {}
    Match At(int sec) {
        Match m;
        m.rule = rule; m.playing = true;
        const int mins10 = sec / 6;                                       // (tenths of a minute)
        struct { int id, team; bool real; int k, d; } who[4] = {
            {1, 0, false, mins10 * 2 / 10, mins10 * 2 / 10}, {2, 1, false, mins10 * 5 / 10, mins10 * 2 / 10},
            {3, 0, false, mins10 * 2 / 10, mins10 * 3 / 10}, {4, 1, true, mins10 / 20, mins10 * 9 / 20}};
        if (rule == 1) { m.tickets[0] = 30 - mins10 / 4; m.tickets[1] = 30 - mins10 / 3; }
        for (auto& w : who) {
            Sample(rings[w.id], (uint32_t)(w.id + 1), sec, w.k, w.d);
            Player p;
            p.id = w.id; p.real = w.real; p.team = rule == 0 ? 0 : w.team; p.kills = w.k; p.deaths = w.d; p.rating = w.real ? 1000 : -1;
            p.known = Strength(rings[w.id], p.rating, &p.skill, &p.conf);
            m.players.push_back(p);
        }
        return m;
    }
};
static const int kSelf[3] = {1, 2, 3}, kTeam[3] = {0, 1, 0};

int main() {
    // 1. the default options: the same as the call without options (team deathmatch and deathmatch, bounds 5..10 and 1..10)
    for (int rule = 0; rule <= 1; ++rule)
        for (int lo : {5, 1}) {
            Sim a(rule), b(rule);
            bool same = true;
            for (int sec = 0; sec <= 480; sec += 5) {
                const Match ma = a.At(sec), mb = b.At(sec);
                for (int i = 0; i < 3; ++i) {
                    float o1, o2;
                    Step(a.st[i], 10, lo, 10, kSelf[i], kTeam[i], ma, &o1);
                    Step(b.st[i], 10, lo, 10, kSelf[i], kTeam[i], mb, &o2, Options());
                    if (a.st[i].level != b.st[i].level || a.st[i].drift != b.st[i].drift || memcmp(a.st[i].foe, b.st[i].foe, sizeof a.st[i].foe)) same = false;
                }
            }
            char w[160];
            snprintf(w, sizeof w, "default options: the same levels, drift and parts against each foe as before (%s, bounds %d..10)", rule ? "team deathmatch" : "deathmatch", lo);
            Check(same, w);
        }
    // 2. teamFoe in team deathmatch (bounds 1..10): the red dolls lower against the weak real player, the team's level the same
    {
        Sim a(1), b(1);
        Options on; on.teamFoe = true;
        bool levelSame = true, neverUp = true;
        for (int sec = 0; sec <= 480; sec += 5) {
            const Match ma = a.At(sec), mb = b.At(sec);
            for (int i = 0; i < 3; ++i) {
                float o1, o2;
                Step(a.st[i], 10, 1, 10, kSelf[i], kTeam[i], ma, &o1);
                Step(b.st[i], 10, 1, 10, kSelf[i], kTeam[i], mb, &o2, on);
                if (a.st[i].level != b.st[i].level) levelSame = false;
                for (int f = 0; f < 16; ++f) if (b.st[i].foe[f] > a.st[i].foe[f] + 1e-6f) neverUp = false;
            }
        }
        const float red1 = b.st[0].foe[4], red3 = b.st[2].foe[4], def1 = a.st[0].foe[4];
        printf("     after 8 minutes: red doll 1 at %.2f, against the real player %+.2f (default %+.2f); red doll 3 %+.2f; blue doll 2 against red doll 1 %+.2f (default %+.2f)\n",
               b.st[0].level, red1, def1, red3, b.st[1].foe[1], a.st[1].foe[1]);
        Check(levelSame, "teamFoe: each doll's own level (the team's balance) unchanged");
        Check(neverUp, "teamFoe: no part against any foe higher than the default one");
        Check(red1 <= -1.0f && red3 <= -1.0f && red1 >= -3.0f, "teamFoe: the two dolls facing the weak real player 1 to 3 levels lower against him");
        Check(b.st[1].foe[1] == a.st[1].foe[1] && b.st[1].foe[3] == a.st[1].foe[3], "teamFoe: nothing against a doll (only real players)");
        Check(b.st[1].foe[4] == 0.f, "teamFoe: nothing against an ally");
    }
    // 3. teamFoe leaves deathmatch alone (its own part against a foe already acts)
    {
        Sim a(0), b(0);
        Options on; on.teamFoe = true;
        bool same = true;
        for (int sec = 0; sec <= 480; sec += 5) {
            const Match ma = a.At(sec), mb = b.At(sec);
            for (int i = 0; i < 3; ++i) {
                float o1, o2;
                Step(a.st[i], 10, 1, 10, kSelf[i], kTeam[i], ma, &o1);
                Step(b.st[i], 10, 1, 10, kSelf[i], kTeam[i], mb, &o2, on);
                if (a.st[i].level != b.st[i].level || memcmp(a.st[i].foe, b.st[i].foe, sizeof a.st[i].foe)) same = false;
            }
        }
        Check(same, "teamFoe: deathmatch unchanged");
    }
    // 4. FoeLevel: the floor
    {
        Options d, f2, f0; f2.foeFloor = 2; f0.foeFloor = -4;
        Check(FoeLevel(5.0f, -2.0f, 5, 10) == 5.0f && FoeLevel(5.0f, -2.0f, 5, 10, d) == 5.0f, "FoeLevel: adapt_min the floor by default (a doll at 5 with bounds 5..10 cannot go lower against a foe)");
        Check(std::fabs(FoeLevel(5.0f, -2.0f, 5, 10, f2) - 3.0f) < 1e-6f, "FoeLevel: foeFloor 2 lets the part against a foe take a doll at 5 to 3");
        Check(FoeLevel(5.0f, -5.0f, 5, 10, f2) == 2.0f && FoeLevel(2.0f, -3.0f, 1, 10) == 1.0f, "FoeLevel: never under foeFloor, never under 1");
        Check(FoeLevel(9.5f, 2.0f, 5, 10, f2) == 10.0f && FoeLevel(5.0f, -1.0f, 5, 10, f0) == 5.0f, "FoeLevel: never over adapt_max; a foeFloor of 0 or less is adapt_min");
        Check(FoeLevel(5.0f, -2.0f, 3, 10, f2) == 3.0f && FoeLevel(4.0f, -3.0f, 3, 10, f2) == 2.0f, "FoeLevel: foeFloor under adapt_min only lowers it");
    }
    printf(gFail ? "FAILED %d\n" : "ALL PASSED\n", gFail);
    return gFail ? 1 : 0;
}
