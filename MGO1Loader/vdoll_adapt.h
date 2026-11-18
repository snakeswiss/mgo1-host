// vdoll_adapt.h - (v150-host beta 19) the virtual dolls' live level (MGO1_VDOLL_ADAPT=1, docs/VIRTUAL_DOLL.md 19): the
// playable doll's (doll_brain.cpp AdaptFrame, PlayerStrength, Stance, FoeAdjust, DollOffset; docs/PLAYABLE_DOLL.md 17)
// for each virtual doll, computed in the host's process where every player's counters are at hand. Pure: no game, no
// platform; vdoll.cpp gives the players and the match every 5 s, tests/vdoll_adapt_test.cpp tests it alone.
//
// Each virtual doll: its level set is its anchor; every 5 s each player's kills and deaths are sampled (3 minutes kept);
// a player's strength on the ladder from his kills against his deaths these 3 minutes and this minute (and a real
// player's VS RATING), weighed by how much is known (the confidence: a player who has just come is known little); the
// enemies' strength against its own side's (real players weigh 1, the other dolls 0.4, itself 0.5), the match's stance
// (tickets, objectives, rounds, kills); a target from them; the level moves toward it a quarter level at a time (half a
// level up when the gap is obvious and known, 0.2 down, 0.35 when obvious), not at all within 0.3, always within
// adapt_min .. adapt_max. One part of its own (the drift, Step): while one side stays the stronger the target goes on
// moving, so that the sides end even (the owner's ask of 11 November). No real player playing: nothing judged, the levels kept. Against the foe it fights, a touch
// more: in the team rules his own play's (+-0.3 of a level, the playable doll's FoeAdjust); in DEATHMATCH, the rule of
// individuals, his strength against the enemies' (up to 3 levels either way, the owner's ask of 11 November).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vdoll {
namespace adapt {

// a player's counters every 5 s, 3 minutes: (second, kills, deaths); afresh at another user id in the slot. (Not the
// playable doll's: its ring starts afresh when the counters go down, at each new round. The same player in the next round
// is the same player: his ring goes on, the new round's counters added to what he had, so a level is not lost to a new
// round's first minute when nothing is known of anyone)
struct Ring {
    uint32_t uid = 0;
    int kOff = 0, dOff = 0, kLast = 0, dLast = 0;
    std::vector<std::array<int, 3>> s;
};
inline void Sample(Ring& r, uint32_t uid, int sec, int kills, int deaths) {
    if (r.uid != uid) { r = Ring(); r.uid = uid; }
    else if (!r.s.empty() && (kills < r.kLast || deaths < r.dLast)) { r.kOff += r.kLast; r.dOff += r.dLast; }   // (a new round)
    r.kLast = kills; r.dLast = deaths;
    r.s.push_back({sec, kills + r.kOff, deaths + r.dOff});
    if (r.s.size() > 37) r.s.erase(r.s.begin());
}
// his strength on the ladder (1 .. 10) and how much is known of it (0 .. 1); rating: a real player's VS RATING (-1 none)
inline bool Strength(const Ring& ring, int rating, float* skill, float* conf) {
    const auto& r = ring.s;
    if (r.size() < 2) return false;
    auto perf = [](int k, int d) { return std::log2((k + 1.f) / (d + 1.f)); };
    const auto& last = r.back();
    const auto& m0 = r.front();
    const auto& s0 = r[r.size() > 12 ? r.size() - 12 : 0];
    const int km = last[1] - m0[1], dm = last[2] - m0[2], ks = last[1] - s0[1], ds = last[2] - s0[2];
    const float span = (last[0] - m0[0]) / 60.f;
    float sk = 5.5f + 2.0f * (0.6f * perf(km, dm) + 0.4f * perf(ks, ds));
    float c = std::min(1.f, (km + dm) / 10.f) * std::min(1.f, span / 2.f);
    if (rating > 0 && rating != 1000) { sk += std::max(-1.5f, std::min(1.5f, (rating - 1000) / 400.f)); c = std::min(1.f, c + 0.25f); }
    *skill = std::max(1.f, std::min(10.f, sk));
    *conf = c;
    return true;
}
// a foe's play in this match and his rating: -0.5 (weak) .. +0.5 (strong) (doll_brain.cpp FoeAdjust)
inline float FoeAdjust(int kills, int deaths, int rating) {
    float a = kills + deaths < 3 ? 0.f : 0.35f * std::log2((kills + 1.f) / (deaths + 1.f));
    if (rating > 0) a += std::max(-0.25f, std::min(0.25f, (rating - 1000) / 2000.f));
    return std::max(-0.5f, std::min(0.5f, a));
}
// its own small offset (its number from 1: -0.24 .. +0.24), the side's dolls not all alike (doll_brain.cpp DollOffset)
inline float Offset(int number) { return number > 0 ? ((number * 7) % 5 - 2) * 0.12f : 0.f; }
inline int Lo(int a, int b) { return std::max(1, std::min(10, std::min(a, b))); }
inline int Hi(int a, int b) { return std::max(1, std::min(10, std::max(a, b))); }
// the level it plays at (the foe's part aside): the live level with its offset, within the bounds
inline float Effective(float level, int number, int lo, int hi) {
    return std::max((float)lo, std::min((float)hi, level + Offset(number)));
}
// the whole level of a fractional one (what switches at a tier: its CQC, rolls, grenades ...)
inline int Tier(float lv) { return std::max(1, std::min(10, (int)std::lround(lv))); }
// the table's rows taken in proportion (6.7: 70 % of level 7, 30 % of level 6), as doll_brain.cpp SkillAt
inline float MixF(float a, float b, float t) { return a + (b - a) * t; }
inline int MixI(int a, int b, float t) { return (int)std::lround(a + (b - a) * t); }
inline void Between(float lv, int* a, int* b, float* t) {
    *a = std::max(0, std::min(9, (int)std::floor(lv) - 1));
    *b = std::min(9, *a + 1);
    *t = std::max(0.f, std::min(1.f, lv - 1.f - *a));
}

// the game's rule as the live level judges it: TEAM SNEAKING as RESCUE, BASE CAPTURE as TEAM DEATHMATCH (doll.cpp SeeRoom)
inline int Judged(int rule) { return rule >= 0 && rule <= 4 ? rule : rule == 5 ? 2 : rule == 6 ? 1 : -1; }

struct Player {
    int id = -1;
    bool real = false;                            // a real player (else a doll, playable or virtual)
    int team = 0xFF;                              // 0 red, 1 blue (deathmatch: its unit), 0xFE a spectator, 0xFF none
    int kills = 0, deaths = 0, rating = -1;       // this match's counters, a real player's VS RATING (-1 none)
    bool known = false;                           // his strength known (Strength true)
    float skill = 5.5f, conf = 0.f;
};
struct Obj { int holderId = -1, holderTeam = -1; bool holderSnake = false; int goalTeam = -1; bool nearGoal = false; };
struct Match {
    int rule = -1;                                // the game's (0 .. 6)
    bool playing = false;                         // the round being played
    int wins[2] = {0, 0}, tickets[2] = {0, 0};
    bool goalSet[2] = {false, false};             // rescue, team sneaking: the side whose goal is set attacks (takes the items there)
    std::vector<Obj> objs;
    std::vector<Player> players;                  // every player of the session but the host (spectators judged nothing)
};
struct State {
    float level = -1.f;                           // the live level without its offset (1 .. 10), -1 before the first judgement
    float target = -1.f, enemy = 5.5f, ally = 5.5f, balance = 0.f, conf = 0.f, confE = 0.f, confA = 0.f;
    int base = -1;
    float drift = 0.f;                            // the part that builds while one side stays the stronger (levels, within the bounds)
    std::string reason = "even", why = "even", stance;
    int eReal = 0, aReal = 0;
    float foe[16] = {};                           // its level's part against each player it may fight (levels)
};

// the match's stance for its side (-1 ahead .. +1 behind; vs: the VS RATINGs' part), doll_brain.cpp Stance
inline bool Stance(const Match& m, int self, int team, float* b, float* vs, float* moved, std::string* why) {
    auto clamp1 = [](float x) { return std::max(-1.f, std::min(1.f, x)); };
    const int rule = Judged(m.rule);
    int mine = 0, theirs = 0, best = -1, sum = 0, n = 0, me = -1;
    int nSide[2] = {0, 0}, nReal[2] = {0, 0}, nAll = 0;
    float rSide[2] = {0.f, 0.f}, rAll = 0.f;
    *b = 0.f; *vs = 0.f; *moved = 0.f;
    for (const Player& p : m.players) {
        if (p.team == 0xFE || p.team == 0xFF) continue;
        if (p.id == self) { me = p.kills; }
        if (p.team == 0 || p.team == 1) ++nSide[p.team];
        if (p.real && p.rating > 0) { rAll += (float)p.rating; ++nAll; if (p.team == 0 || p.team == 1) { rSide[p.team] += p.rating - 1000.f; ++nReal[p.team]; } }
        if (p.id == self) continue;
        if (rule == 0) {
            if (!p.real) continue;
            best = std::max(best, p.kills); sum += p.kills; ++n;
        } else if (p.team == team) mine += p.kills; else theirs += p.kills;
    }
    char w[240];
    char vsWhy[96] = "";
    if (rule == 0) {
        if (n == 0 || me < 0) return false;
        const float avg = (float)sum / n;
        if (me >= best + 3) *b = -std::min(1.f, (me - best) / 6.f);
        else if (me + 2 <= avg) *b = std::min(1.f, (avg - me) / 6.f);
        if (nAll) { *vs = std::max(-0.75f, std::min(0.75f, (rAll / nAll - 1000.f) / 800.f)); snprintf(vsWhy, sizeof vsWhy, "; the real players' VS RATING %.0f", rAll / nAll); }
        *moved = std::min(1.f, (me + sum) / 6.f);
        snprintf(w, sizeof w, "its kills %d, the real players' best %d, their average %.1f%s", me, best, avg, vsWhy);
        *why = w;
        return true;
    }
    if (team != 0 && team != 1) return false;
    const int my = team, th = 1 - team;
    mine += std::max(0, me);
    const float sMine = rSide[my] / std::max(1, nSide[my]), sTheirs = rSide[th] / std::max(1, nSide[th]);
    if (nReal[0] + nReal[1] > 0) {
        *vs = std::max(-0.75f, std::min(0.75f, (sTheirs - sMine) / 600.f));
        snprintf(vsWhy, sizeof vsWhy, "; VS RATING %+.0f against %+.0f a player", sMine, sTheirs);
    }
    const int winsMine = m.wins[my], winsTheirs = m.wins[th];
    const float sK = clamp1((0.5f - (mine + 2.f) / (mine + theirs + 4.f)) * 2.5f);
    const float sR = clamp1((winsTheirs - winsMine) / 2.f);
    float sO = 0.f, events = (float)(mine + theirs + 3 * (winsMine + winsTheirs));
    char obj[96] = "";
    if (rule == 1) {
        const int tm = m.tickets[my], tt = m.tickets[th];
        if (tm + tt > 0) sO = clamp1((tt - tm) / std::max(4.f, 0.25f * (tm + tt)));
        events += std::abs(tm - tt) / 2.f;
        snprintf(obj, sizeof obj, "tickets %d against %d, ", tm, tt);
    } else if (rule >= 2 && rule <= 4) {
        float ours = 0.f, theirsO = 0.f;
        const bool present = !m.objs.empty();
        for (const Obj& it : m.objs) {
            const bool holder = it.holderId >= 0;
            const bool holderMine = holder && (it.holderId == self || it.holderTeam == my), holderTheirs = holder && !holderMine;
            if (rule == 3) {                                      // capture: in a goal 1, carried 0.5
                if (it.goalTeam == my) ours += 1.f; else if (it.goalTeam == th) theirsO += 1.f;
                if (holderMine) ours += 0.5f; else if (holderTheirs) theirsO += 0.5f;
            } else if (rule == 2 && present) {                    // rescue (and team sneaking): the attackers' progress
                const bool attacker = m.goalSet[my];               // (the host's goals: the attackers' side has one, vdoll_body.cpp ObjectiveGoal)
                float prog = holder ? 0.5f : 0.f;
                if (holder && it.nearGoal) prog = 1.f;
                (attacker ? ours : theirsO) += prog;
            } else if (rule == 4) {                               // sneaking: the item on Snake
                if (holder && it.holderSnake) (it.holderTeam == my ? ours : theirsO) += 0.6f;
            }
        }
        sO = clamp1((theirsO - ours) / 1.5f);
        events += 4.f * (ours + theirsO);
        snprintf(obj, sizeof obj, "the objective %.1f against %.1f, ", ours, theirsO);
    }
    *b = clamp1(rule == 1 ? 0.5f * sO + 0.25f * sR + 0.25f * sK : 0.4f * sO + 0.3f * sR + 0.3f * sK);
    *moved = std::min(1.f, events / 8.f);
    snprintf(w, sizeof w, "%sits team's kills %d against %d, rounds %d against %d%s", obj, mine, theirs, winsMine, winsTheirs, vsWhy);
    *why = w;
    return true;
}

// a new anchor or new bounds: the level follows at once (a new anchor: back to it, within the bounds)
inline void Anchor(State& s, int base, int lo, int hi) {
    if (s.base != base || s.level < 0) {
        const State fresh;
        s = fresh;
        s.base = base; s.level = (float)std::max(lo, std::min(hi, base));
    }
    s.level = std::max((float)lo, std::min((float)hi, s.level));
}

// (proposal of 12 November, logs12; OFF unless asked: the owner's choice) the part against a weaker REAL player in
// front of it in the TEAM rules. The owner's logs of 11 and 12 November (reports/owner_logs_1112.md): in team deathmatch
// a weak real player with one doll beside him against two dolls; the live level evens the TEAMS (his allied doll up to
// 10, the two facing him down to the floor), so his team wins while he goes 2-18 against dolls at the floor. teamFoe:
// in the team rules a doll's level against a real enemy is also lowered, never raised, by 0.7 x (the level it plays at
// - his strength) x his confidence, 3 levels at most (the team's balance stays the team's level; this is only toward a
// player weaker than the level he faces). foeFloor: the lowest the level against one foe may go (0: adapt_min, as the
// body clamps today); with adapt_min 5 a part against a foe cannot act on a doll already at 5 (28 to 34 % of the time
// with a real player on his server).
struct Options {
    bool teamFoe = false;
    int foeFloor = 0;
};
// the level it plays against one foe: its level now (offset in) and its part against him, within the bounds (the body's
// RowOf clamp, vdoll_body.cpp); with Options.foeFloor the lowest is that floor instead of adapt_min
inline float FoeLevel(float now, float foe, int lo, int hi, const Options& o = Options()) {
    const float floor = o.foeFloor > 0 ? (float)std::min(lo, std::max(1, o.foeFloor)) : (float)lo;
    return std::max(floor, std::min((float)hi, now + foe));
}
// one judgement (every 5 s) of virtual doll `self` (its player id) in `team` with its anchor `base`: true when the playable
// doll's rule says a line (moved 0.05 or more, or its "why" changed); *old the level before. The players' strengths must be
// in m.players (known, skill, conf). Nothing judged (false, the level kept) with no real player or no round being played
inline bool Step(State& s, int base, int lo, int hi, int self, int team, const Match& m, float* old, const Options& opt);
inline bool Step(State& s, int base, int lo, int hi, int self, int team, const Match& m, float* old) {
    return Step(s, base, lo, hi, self, team, m, old, Options());
}
inline bool Step(State& s, int base, int lo, int hi, int self, int team, const Match& m, float* old, const Options& opt) {
    Anchor(s, base, lo, hi);
    *old = s.level;
    bool anyReal = false;
    for (const Player& p : m.players) if (p.real && p.team != 0xFE && p.team != 0xFF) anyReal = true;
    if (!m.playing || !anyReal) return false;
    const int rule = Judged(m.rule);
    float eSum = 0.f, eW = 0.f, aSum = 0.f, aW = 0.f;
    s.eReal = s.aReal = 0;
    for (const Player& p : m.players) {
        if (p.team == 0xFE || p.team == 0xFF || !p.known) continue;
        const bool me = p.id == self;
        const bool enemy = !me && (rule == 0 || p.team != team);
        const float w = (me ? 0.5f : p.real ? 1.f : 0.4f) * p.conf;
        if (enemy) { eSum += w * p.skill; eW += w; if (p.real) ++s.eReal; }
        else { aSum += w * p.skill; aW += w; if (p.real) ++s.aReal; }
    }
    s.enemy = eW > 0.f ? eSum / eW : 5.5f; s.confE = std::min(1.f, eW / 1.5f);
    s.ally = aW > 0.f ? aSum / aW : 5.5f; s.confA = std::min(1.f, aW / 1.5f);
    float b = 0.f, vs = 0.f, moved = 0.f;
    std::string why;
    const bool st = Stance(m, self, team, &b, &vs, &moved, &why);
    if (!st) { b = 0.f; vs = 0.f; moved = 0.f; why = "the stance not known yet"; }
    s.stance = why;
    s.balance = b + vs;
    const bool objectiveMode = rule >= 2;
    const float kSkillW = objectiveMode ? 0.5f : 0.7f, kBal = objectiveMode ? 2.4f : 2.0f;
    s.conf = std::max(0.f, std::min(1.f, 0.6f * s.confE + 0.4f * s.confA));
    const float gap = s.confE > 0.05f ? s.enemy - s.ally : 0.f;
    // (the virtual dolls' own part, docs/VIRTUAL_DOLL.md 19) the playable doll's target is proportional: against a player who
    // stays weak it stops 1 to 3 levels under the anchor (the owner's games of 11 November: about 8.5 in team deathmatch),
    // still the anchor's quick kills. So while the gap stays over half a level, known enough (confidence 0.25), a drift
    // builds, 0.03 of the gap every 5 s (0.36 of a level a minute at a gap of one level, 3 at most): the levels go on until
    // the sides are even; within half a level of even it fades slowly (1 % every 5 s); too little known: kept; never beyond
    // the bounds
    const float e = gap * s.conf;
    if (s.conf >= 0.25f) {
        if (std::fabs(gap) > 0.5f) s.drift += 0.03f * std::max(-3.f, std::min(3.f, gap));
        else s.drift *= 0.99f;
    }
    s.drift = std::max((float)(lo - base), std::min((float)(hi - base), s.drift));
    float target = base + kSkillW * e + s.drift + kBal * (b * moved + vs);
    target = std::max((float)lo, std::min((float)hi, target));
    s.target = target;
    const float was = s.level, d = target - s.level;
    if (std::fabs(d) >= 0.3f) {
        const float up = d > 2.f && s.conf > 0.6f ? 0.5f : 0.25f, down = d < -2.f && s.conf > 0.6f ? 0.35f : 0.2f;
        s.level = std::max((float)lo, std::min((float)hi, s.level + std::max(-down, std::min(up, d))));
    }
    s.reason = std::fabs(d) < 0.3f ? "on_target" : s.conf < 0.2f && std::fabs(b) < 0.15f ? "low_confidence" :
               gap > 1.f && d > 0 ? "enemy_team_outperforming" : gap < -1.f && d < 0 ? "ally_team_dominating" :
               b > 0.15f ? "its_side_behind" : b < -0.15f ? "its_side_ahead" : d > 0 ? "enemy_stronger" : "enemy_weaker";
    // its part against each foe: the team rules a touch (his play, +-0.3); DEATHMATCH his strength against the enemies'
    for (float& f : s.foe) f = 0.f;
    for (const Player& p : m.players) {
        if (p.id < 0 || p.id >= 16 || p.id == self || p.team == 0xFE || p.team == 0xFF) continue;
        if (rule != 0 && p.team == team) continue;
        if (rule == 0 && p.real) s.foe[p.id] = p.known ? std::max(-3.f, std::min(3.f, 0.7f * (p.skill - s.enemy) * p.conf)) : 0.f;
        else s.foe[p.id] = 0.6f * FoeAdjust(p.kills, p.deaths, p.real ? p.rating : -1);
        if (opt.teamFoe && rule != 0 && p.real && p.known) {
            const float now = Effective(s.level, 0, lo, hi);          // (its own offset is the caller's: within 0.24)
            const float down = std::max(-3.f, std::min(0.f, 0.7f * (p.skill - now) * p.conf));
            s.foe[p.id] = std::min(s.foe[p.id], down);
        }
    }
    const std::string whyCode = s.balance > 0.15f || s.level > base + 0.25f ? "behind" : s.balance < -0.15f || s.level < base - 0.25f ? "ahead" : "even";
    const bool say = std::fabs(s.level - was) >= 0.05f || whyCode != s.why;
    s.why = whyCode;
    return say;
}

}  // namespace adapt
}  // namespace vdoll
