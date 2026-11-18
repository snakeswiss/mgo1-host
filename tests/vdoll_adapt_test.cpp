// (v150-host beta 19) the virtual dolls' live level alone (MGO1Loader/vdoll_adapt.h, docs/VIRTUAL_DOLL.md 19): no game, the
// players' counters made here every 5 s.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_adapt_test.cpp -o vdoll_adapt_test && ./vdoll_adapt_test
//   ./vdoll_adapt_test owner   the owner's games of 11 November (their kills and deaths at an even rate): the levels it gives
#include "vdoll_adapt.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace vdoll::adapt;

namespace {
int gFails = 0;
void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++gFails; }

// a game: players with a kills and a deaths rate a minute, every 5 s their counters, the rings, the dolls judged
struct Who { int id; bool real; int team; float kpm, dpm; float k = 0, d = 0; bool here = true; uint32_t uid = 0; };
struct Sim {
    int rule = 0, sec = 0;
    std::vector<Who> who;
    Ring rings[16];
    std::vector<State> st;                          // one a doll (who[i] not real)
    int wins[2] = {0, 0}, tickets[2] = {0, 0};
    std::vector<Obj> objs;
    bool goalSet[2] = {false, false};
    bool playing = true;
    int lines = 0;
    float maxUp = 0, maxDown = 0;
    void Step5(int base, int lo, int hi) {
        sec += 5;
        Match m;
        m.rule = rule; m.playing = playing;
        for (int t = 0; t < 2; ++t) { m.wins[t] = wins[t]; m.tickets[t] = tickets[t]; m.goalSet[t] = goalSet[t]; }
        m.objs = objs;
        for (Who& w : who) {
            if (!w.here) { rings[w.id] = Ring(); continue; }
            w.k += w.kpm / 12.f; w.d += w.dpm / 12.f;
            Sample(rings[w.id], w.uid ? w.uid : 0x100u + w.id, sec, (int)w.k, (int)w.d);
            Player p;
            p.id = w.id; p.real = w.real; p.team = w.team; p.kills = (int)w.k; p.deaths = (int)w.d;
            p.known = Strength(rings[w.id], -1, &p.skill, &p.conf);
            m.players.push_back(p);
        }
        st.resize(who.size());
        for (size_t i = 0; i < who.size(); ++i) {
            if (who[i].real || !who[i].here) continue;
            float old;
            if (vdoll::adapt::Step(st[i], base, lo, hi, who[i].id, who[i].team, m, &old)) ++lines;
            maxUp = std::max(maxUp, st[i].level - old); maxDown = std::max(maxDown, old - st[i].level);
        }
    }
    float Lv(int i, int lo = 1, int hi = 10) const { return Effective(st[i].level, who[i].id, lo, hi); }
    void Run(int seconds, int base = 10, int lo = 1, int hi = 10) { for (int s = 0; s < seconds; s += 5) Step5(base, lo, hi); }
};
}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "owner")) {
        // 11 November (beta 17): three dolls at 10, three real players over 71 minutes: the first put down 120 times for 33
        // kills of dolls (22 %), the second 27 for 21 (44 %), the third 6 for 3 (33 %). A model: each player's duels with the
        // dolls at his own pace (2.15, 1.4, 1.3 a minute), his share of them at level 10 his own, each level under 10 its
        // logit up by SLOPE (argv[2], 0.3 by default: tools' ladder measure, docs/VIRTUAL_DOLL.md 19); the dolls among
        // themselves 0.5 kills and deaths a minute each (deathmatch). The first player there all along, the second minute
        // 10 to 45, the third minute 30 to 37. Printed: each minute's levels, each player's share in that minute
        const float slope = argc > 2 ? (float)atof(argv[2]) : 0.3f;
        for (int rule : {0, 1}) {
            Sim s; s.rule = rule;
            s.who = {{1, false, 0, 0, 0}, {2, false, rule ? 0 : 1, 0, 0}, {3, false, rule ? 0 : 2, 0, 0}, {5, true, 1, 0, 0}, {6, true, rule ? 1 : 0, 0, 0}, {7, true, rule ? 1 : 2, 0, 0}};
            const float pace[3] = {2.15f, 1.4f, 1.3f}, share10[3] = {0.22f, 0.44f, 0.33f};
            float won[3] = {0, 0, 0}, duels[3] = {0, 0, 0};
            printf("%s (slope %.2f a level): the first player all along, the second minute 10 to 45, the third minute 30 to 37\n",
                   rule ? "team deathmatch (the dolls red, the real players blue)" : "deathmatch", slope);
            for (int minute = 1; minute <= 60; ++minute) {
                s.who[4].here = minute > 10 && minute <= 45;
                s.who[5].here = minute > 30 && minute <= 37;
                float mw[3] = {0, 0, 0}, md[3] = {0, 0, 0};
                for (int t = 0; t < 12; ++t) {
                    for (int i = 0; i < 3; ++i) {
                        Who& p = s.who[3 + i];
                        if (!p.here) continue;
                        const float n = pace[i] / 12.f;
                        for (int j = 0; j < 3; ++j) {                  // a third of his duels with each doll, at its level against him
                            const float lv = s.st.empty() ? 10.f : std::max(1.f, std::min(10.f, s.Lv(j) + s.st[j].foe[p.id]));
                            const float x = std::log(share10[i] / (1.f - share10[i])) + slope * (10.f - lv), pw = 1.f / (1.f + std::exp(-x));
                            p.k += n / 3 * pw; p.d += n / 3 * (1 - pw); s.who[j].d += n / 3 * pw; s.who[j].k += n / 3 * (1 - pw);
                            mw[i] += n / 3 * pw; md[i] += n / 3;
                        }
                    }
                    if (rule == 0) for (int j = 0; j < 3; ++j) { s.who[j].k += 0.5f / 12; s.who[j].d += 0.5f / 12; }
                    s.Step5(10, 1, 10);
                }
                for (int i = 0; i < 3; ++i) { won[i] += mw[i]; duels[i] += md[i]; }
                if (minute <= 3 || minute % 3 == 0)
                    printf("  minute %2d: levels %.2f %.2f %.2f (drift %+.2f, confidence %.2f), against P5 %.2f; the players' share this minute %s %s %s\n", minute,
                           s.Lv(0), s.Lv(1), s.Lv(2), s.st[0].drift, s.st[0].conf, std::max(1.f, s.Lv(0) + s.st[0].foe[5]),
                           md[0] > 0 ? std::to_string((int)std::lround(100 * mw[0] / md[0])).c_str() : "-", md[1] > 0 ? std::to_string((int)std::lround(100 * mw[1] / md[1])).c_str() : "-",
                           md[2] > 0 ? std::to_string((int)std::lround(100 * mw[2] / md[2])).c_str() : "-");
            }
            printf("  over the hour: the players' share %.0f %%, %.0f %%, %.0f %% (at a fixed 10: 22, 44, 33 %%)\n", 100 * won[0] / duels[0], 100 * won[1] / duels[1], 100 * won[2] / duels[2]);
        }
        return 0;
    }
    // the ladder's rows between two levels, the tier of a fractional level, the offsets
    {
        int a, b; float t;
        Between(6.7f, &a, &b, &t);
        Check(a == 5 && b == 6 && std::fabs(t - 0.7f) < 1e-4f, "6.7: 70 % of level 7's row, 30 % of level 6's");
        Between(10.f, &a, &b, &t);
        Check(a == 9 && b == 9 && t == 0.f, "10: level 10's row exactly");
        Between(1.f, &a, &b, &t);
        Check(a == 0 && t == 0.f, "1: level 1's row exactly");
        Between(7.f, &a, &b, &t);
        Check(a == 6 && t == 0.f, "a whole level: its own row exactly");
        Check(MixI(3, 4, 0.5f) == 4 && MixI(54, 38, 0.25f) == 50 && std::fabs(MixF(0.1f, 0.2f, 0.5f) - 0.15f) < 1e-6f, "the numbers in proportion");
        Check(Tier(5.49f) == 5 && Tier(5.5f) == 6 && Tier(9.76f) == 10 && Tier(0.2f) == 1, "the tier: the nearest whole level");
        Check(Offset(1) == 0.f && std::fabs(Offset(2) - 0.24f) < 1e-6f && std::fabs(Offset(3) + 0.12f) < 1e-6f && std::fabs(Offset(4) - 0.12f) < 1e-6f && std::fabs(Offset(5) + 0.24f) < 1e-6f,
              "the dolls' own offsets (-0.24 .. +0.24), as the playable dolls'");
        Check(Effective(10.f, 2, 1, 10) == 10.f && std::fabs(Effective(10.f, 5, 1, 10) - 9.76f) < 1e-5f, "at the anchor 10 an offset never above 10");
        Check(Lo(9, 3) == 3 && Hi(9, 3) == 9 && Lo(0, 12) == 1 && Hi(0, 12) == 10, "the bounds the wrong way round: swapped; within 1 .. 10");
        Check(Judged(5) == 2 && Judged(6) == 1 && Judged(4) == 4 && Judged(0) == 0, "team sneaking judged as rescue, base capture as team deathmatch");
    }
    // a player's strength: the playable doll's PlayerStrength
    {
        Ring r;
        float sk, c;
        Sample(r, 7, 0, 0, 0);
        Check(!Strength(r, -1, &sk, &c), "one sample: not known");
        Sample(r, 7, 5, 0, 0);
        Check(Strength(r, -1, &sk, &c) && c < 0.05f, "a player who has just come: known little");
        for (int s = 10; s <= 180; s += 5) Sample(r, 7, s, s / 60, s / 15);
        Check(Strength(r, -1, &sk, &c) && sk < 3.f && c > 0.9f, "3 kills for 12 deaths in 3 minutes: weak (under 3) and known");
        const float weak = sk;
        Check(Strength(r, 1600, &sk, &c) && std::fabs(sk - weak - 1.5f) < 1e-4f, "a VS RATING of 1600: 1.5 more");
        const size_t n = r.s.size();
        Sample(r, 7, 185, 0, 0);
        Check(r.s.size() == n && r.s.back()[1] == 3 && r.s.back()[2] == 12 && Strength(r, -1, &sk, &c) && sk < 3.f, "a new round (his counters back to 0): his ring goes on");
        Sample(r, 7, 190, 1, 1);
        Check(r.s.back()[1] == 4 && r.s.back()[2] == 13, "the new round's counters added");
        Sample(r, 8, 195, 0, 0);
        Check(r.s.size() == 1, "another player in the slot: afresh");
        Check(FoeAdjust(10, 2, -1) > 0.4f && FoeAdjust(1, 9, -1) < -0.4f && FoeAdjust(1, 1, -1) == 0.f, "a foe's play: +-0.5, nothing under 3 kills and deaths");
    }
    // nothing judged with no real player, with a spectator only, out of a round
    {
        Sim s; s.rule = 1;
        s.who = {{1, false, 0, 3, 1}, {2, false, 1, 1, 3}, {3, true, 0xFE, 0, 0}};
        s.Run(300);
        Check(s.st[0].level == 10.f && s.st[1].level == 10.f && s.lines == 0, "no real player (a spectator judges nothing): the levels kept");
        Sim t; t.rule = 1; t.playing = false;
        t.who = {{1, false, 0, 3, 1}, {2, true, 1, 0.2f, 3}};
        t.Run(300);
        Check(t.st[0].level == 10.f, "between rounds: kept");
    }
    // DEATHMATCH: a weak real player against three dolls at 10 comes down, never above 10, never under adapt_min
    {
        Sim s; s.rule = 0;
        s.who = {{1, false, 0, 2, 0.4f}, {2, false, 1, 2, 0.4f}, {3, false, 2, 2, 0.4f}, {5, true, 0, 1.2f, 6}};
        float hiSeen = 0;
        int firstBelow8 = -1;
        for (int t = 0; t < 600; t += 5) {
            s.Step5(10, 1, 10);
            for (int i = 0; i < 3; ++i) hiSeen = std::max(hiSeen, s.Lv(i));
            if (firstBelow8 < 0 && s.Lv(0) < 8.f) firstBelow8 = t + 5;
        }
        char w[200];
        snprintf(w, sizeof w, "a weak player (1.2 kills, 6 deaths a minute): the dolls from 10 to %.2f %.2f %.2f in 10 minutes (under 8 after %d s), never above 10",
                 s.Lv(0), s.Lv(1), s.Lv(2), firstBelow8);
        Check(s.Lv(0) < 7.f && s.Lv(1) < 7.5f && s.Lv(2) < 7.f && hiSeen <= 10.f, w);
        Check(s.maxDown <= 0.351f && s.maxUp <= 0.501f, "a step at most 0.35 down and 0.5 up");
        Check(s.st[0].foe[5] < -0.5f, "in a deathmatch its level against the weak player lower still");
        Sim l; l.rule = 0;
        l.who = s.who;
        for (Who& w2 : l.who) { w2.k = w2.d = 0; }
        l.Run(600, 10, 6, 10);
        Check(l.Lv(0) >= 6.f && l.Lv(1) >= 6.f, "adapt_min 6: never under 6");
    }
    // a strong real player against dolls at 5 (bounds 1 .. 10): they go up; with adapt_max 5 they stay at 5 at most; at 10 they stay
    {
        Sim s; s.rule = 0;
        s.who = {{1, false, 0, 0.5f, 2}, {2, false, 1, 0.5f, 2}, {3, false, 2, 0.5f, 2}, {5, true, 0, 6, 0.5f}};
        s.Run(600, 5, 1, 10);
        char w[160];
        snprintf(w, sizeof w, "a strong player: the dolls set at 5 up to %.2f %.2f %.2f (adapt_max 10)", s.Lv(0), s.Lv(1), s.Lv(2));
        Check(s.Lv(0) > 7.f, w);
        Sim c; c.rule = 0; c.who = s.who; for (Who& w2 : c.who) { w2.k = w2.d = 0; }
        c.Run(600, 5, 1, 5);
        Check(c.Lv(0, 1, 5) <= 5.f && c.Lv(1, 1, 5) <= 5.f && c.Lv(2, 1, 5) <= 5.f && c.st[0].level <= 5.f, "adapt_max 5: never above 5");
        Sim h; h.rule = 0; h.who = s.who; for (Who& w2 : h.who) { w2.k = w2.d = 0; }
        h.Run(600, 10, 1, 10);
        Check(h.Lv(0) >= 9.5f && h.Lv(1) >= 9.5f, "a strong player against dolls at 10: they stay at 10");
        Check(h.st[0].foe[5] >= 0.f, "against him no part that lowers it");
    }
    // DEATHMATCH, one strong and one weak real player: the global level between, against each his own
    {
        Sim s; s.rule = 0;
        s.who = {{1, false, 0, 1.5f, 1.5f}, {2, false, 1, 1.5f, 1.5f}, {3, false, 2, 1.5f, 1.5f}, {5, true, 0, 5, 0.7f}, {6, true, 1, 0.5f, 5}};
        s.Run(600);
        char w[200];
        snprintf(w, sizeof w, "a strong and a weak player: doll 1 at %.2f, against the strong one %+.2f, against the weak one %+.2f", s.Lv(0), s.st[0].foe[5], s.st[0].foe[6]);
        Check(s.st[0].foe[5] > 1.f && s.st[0].foe[6] < -1.f && s.st[0].foe[5] <= 3.f && s.st[0].foe[6] >= -3.f, w);
    }
    // TEAM DEATHMATCH: the dolls facing a weak real player come down, those on his side stay at their anchor (10 at most)
    {
        Sim s; s.rule = 1;
        s.who = {{1, false, 0, 1.5f, 0.6f}, {2, false, 0, 1.5f, 0.6f}, {3, false, 1, 1.0f, 1.0f}, {5, true, 1, 0.5f, 3}};
        s.Run(600);
        char w[200];
        snprintf(w, sizeof w, "a weak player in blue: red's dolls %.2f %.2f, his teammate %.2f", s.Lv(0), s.Lv(1), s.Lv(2));
        Check(s.Lv(0) < 8.f && s.Lv(1) < 8.f && s.Lv(2) >= 9.8f, w);
        // both sides with real players of very different strength: the weak side's doll at 10, the strong side's comes down
        Sim t; t.rule = 1;
        t.who = {{1, false, 0, 1.f, 1.f}, {2, false, 1, 1.f, 1.f}, {5, true, 0, 4, 0.8f}, {6, true, 1, 0.6f, 4}};
        t.Run(600);
        snprintf(w, sizeof w, "a strong player in red, a weak one in blue: red's doll %.2f, blue's %.2f", t.Lv(0), t.Lv(1));
        Check(t.Lv(0) < 8.f && t.Lv(1) >= 9.8f, w);
        Check(t.st[0].foe[6] < 0.f && t.st[1].foe[5] > 0.f, "a touch against each foe (+-0.3 at most)");
        Check(std::fabs(t.st[0].foe[6]) <= 0.3001f && std::fabs(t.st[1].foe[5]) <= 0.3001f, "in the team rules the foe's part 0.3 at most");
    }
    // the objective rules' stance: capture (KEROTANs in red's goal), rescue and team sneaking (the attackers' progress), sneaking
    // (the item on Snake), base capture (its tickets)
    {
        for (int rule : {3, 2, 5, 4, 6}) {
            Sim s; s.rule = rule;
            s.who = {{1, false, 0, 1, 1}, {2, false, 1, 1, 1}, {5, true, 1, 1, 1}};
            Obj o;
            if (rule == 3) { o.goalTeam = 0; s.objs = {o, o}; }
            else if (rule == 2 || rule == 5) { s.goalSet[0] = true; o.holderId = 1; o.holderTeam = 0; o.nearGoal = true; s.objs = {o}; }   // red attacks: the item at red's carrier by the goal
            else if (rule == 4) { o.holderId = 1; o.holderTeam = 0; o.holderSnake = true; s.objs = {o}; }
            else { s.tickets[0] = 40; s.tickets[1] = 10; }
            s.Run(300);
            char w[200];
            snprintf(w, sizeof w, "rule %d: red ahead in the objective, red's doll %.2f (balance %+.2f), blue's %.2f (balance %+.2f)", rule, s.Lv(0),
                     s.st[0].balance, s.Lv(1), s.st[1].balance);
            Check(s.st[0].balance < -0.04f && s.st[1].balance > 0.04f && s.Lv(0) <= s.Lv(1), w);   // (sneaking: the item on Snake weighs 0.6, the least)
        }
    }
    // a real player who leaves and another who comes: the levels follow, nothing stuck
    {
        Sim s; s.rule = 0;
        s.who = {{1, false, 0, 2, 0.4f}, {2, false, 1, 2, 0.4f}, {3, false, 2, 2, 0.4f}, {5, true, 0, 0.8f, 6}};
        s.Run(480);
        const float low = s.Lv(0);
        s.who[3].here = false;
        s.Run(300);
        const float kept = s.Lv(0);
        s.who[3] = Who{5, true, 0, 6, 0.6f};
        s.who[3].uid = 0x999;
        for (int i = 0; i < 3; ++i) { s.who[i].kpm = 0.6f; s.who[i].dpm = 2; }
        s.Run(600);
        char w[200];
        snprintf(w, sizeof w, "a weak player: %.2f; gone: kept %.2f; a strong one comes: back up to %.2f", low, kept, s.Lv(0));
        Check(low < 8.f && std::fabs(kept - low) < 1e-4f && s.Lv(0) > 9.f, w);
    }
    printf("%s\n", gFails ? "SOME FAILED" : "ALL PASSED");
    return gFails ? 1 : 0;
}
