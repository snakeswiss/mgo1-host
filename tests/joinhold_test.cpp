// (v150-host beta 18) MGO1Loader/joinhold.h: a model of the game's reliable layer (0x170E18) on the host's side of one
// joiner: a reliable record sent at R s, the joiner silent from 0 for S seconds (its stage loads), then it answers (the
// record acknowledged). Each time-out counts one more for the record (the time-out 1.8 s, 0.15 s longer at each one up to
// 3.6 s, as the bandwidth estimate falls: the 11th about 28 s after the send); the 11th asks joinhold::Decide, which ends
// the session or puts the count back. The player's tick (0x175B60) asks too once the joiner has been silent 30 s
// (0x1710D0): ended, or kept.
//   g++ -O1 -std=c++17 -I MGO1Loader tests/joinhold_test.cpp -o /tmp/joinhold_test && /tmp/joinhold_test
#include "joinhold.h"
#include <cstdio>
#include <initializer_list>

static int gFail = 0;
static void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++gFail; }

struct Outcome { bool ended; double at; int kept; };
// silentS: the joiner's silence; state: its player entry; holdS: MGO1_JOINER_HOLD_S
static Outcome Run(double silentS, uint16_t state, unsigned holdS, double recordAt = 0) {
    double t = recordAt, rto = 1.8; int count = 0, kept = 0;
    for (;;) {
        const double next = t + rto;                    // the record's time-out
        // the player's tick, every frame: past 30 s of silence (checked here at each 0.1 s)
        for (double u = t; u < next && u < silentS; u += 0.1)
            if (u * 1000 > joinhold::kSilentMs && joinhold::Decide(true, state, (unsigned long long)(u * 1000), holdS) != joinhold::kKeep)
                return {true, u, kept};
        t = next;
        if (t >= silentS) return {false, silentS, kept};   // the joiner answered first: acknowledged
        ++count;
        if (count >= joinhold::kResendLimit) {
            const joinhold::Verdict v = joinhold::Decide(true, state, (unsigned long long)(t * 1000), holdS);
            if (v != joinhold::kKeep) return {true, t, kept};
            count = joinhold::kCountKept; ++kept;
        }
        rto = rto + 0.15 > 3.6 ? 3.6 : rto + 0.15;
    }
}

int main() {
    // the game's rule (hold 0): 35 s of silence while loading ends the session (at 25 to 30 s), 20 s does not
    Outcome a = Run(35, 0x212, 0);
    Check(a.ended && a.at > 25 && a.at < 30, "the game's rule: a loading joiner silent 35 s is ended (25 to 30 s)");
    Check(!Run(20, 0x212, 0).ended, "the game's rule: a loading joiner silent 20 s is kept");
    // the hold: kept through 35 s and 45 s of silence, the record resent, ended after 60 s of silence
    Outcome b = Run(35, 0x212, joinhold::kHoldS);
    Check(!b.ended && b.kept >= 1, "held: a loading joiner silent 35 s is kept");
    Check(!Run(45, 0x212, joinhold::kHoldS).ended, "held: a loading joiner silent 45 s is kept (the bench's load70)");
    // the player's own limit: a record sent 10 s into the silence (its resend limit at about 38 s), 35 s of silence: the
    // game's rule ends him at 30 s (0x1710D0), the hold keeps him (f2_t1_load120: ended by it at 30 s, before this)
    Outcome p = Run(35, 0x212, 0, 10);
    Check(p.ended && p.at > 30 && p.at < 31, "the game's rule: a loading joiner silent 35 s, a record 10 s in: ended at 30 s by the player's limit");
    Check(!Run(35, 0x212, joinhold::kHoldS, 10).ended, "held: the same joiner is kept");
    Outcome c = Run(1000, 0x212, joinhold::kHoldS);
    Check(c.ended && c.at > 60 && c.at < 70, "held: a loading joiner that never answers is ended between 60 and 70 s");
    // only a loading entry: in play (0x213, 0x216), connecting (0x202, 0x211), ended (0x204): the game's rule
    for (uint16_t s : {0x213, 0x216, 0x202, 0x211, 0x204}) {
        char w[96]; snprintf(w, sizeof w, "an entry at %03x silent 35 s: the game's rule (ended)", s);
        Check(Run(35, s, joinhold::kHoldS).ended, w);
    }
    Check(joinhold::Decide(false, 0x212, 0, joinhold::kHoldS) == joinhold::kEnd, "no player entry for the session: the game's rule");
    Check(joinhold::Decide(true, 0x212, 61000, 60) == joinhold::kEndSilent, "silent 61 s with a hold of 60 s: ended");
    Check(joinhold::Decide(true, 0x212, 59000, 60) == joinhold::kKeep, "silent 59 s with a hold of 60 s: kept");
    // (v150-host beta 18, review) MGO1_JOINER_HOLD_S read: beta 18's first build took (unsigned)atoi, -1 a hold of 136 years
    Check(joinhold::HoldFromEnv(nullptr) == 60, "MGO1_JOINER_HOLD_S unset: 60 s");
    Check(joinhold::HoldFromEnv("0") == 0, "MGO1_JOINER_HOLD_S=0: off (the game's rules)");
    Check(joinhold::HoldFromEnv("90") == 90, "MGO1_JOINER_HOLD_S=90: 90 s");
    Check(joinhold::HoldFromEnv("-1") == 60, "MGO1_JOINER_HOLD_S=-1: 60 s, not for ever");
    Check(joinhold::HoldFromEnv("100000") == joinhold::kHoldMaxS, "MGO1_JOINER_HOLD_S=100000: 600 s at most");
    Check(joinhold::HoldFromEnv("abc") == 60 && joinhold::HoldFromEnv("") == 60 && joinhold::HoldFromEnv("30s") == 60, "MGO1_JOINER_HOLD_S not a number: 60 s");
    Check(joinhold::Decide(true, 0x212, 601000ull, joinhold::HoldFromEnv("-1")) == joinhold::kEndSilent, "MGO1_JOINER_HOLD_S=-1: a peer silent 601 s while it loads is ended");
    // (review P2-3) an episode of holds: three holds at most whatever the peer sends; a gap longer than the hold starts anew
    {
        joinhold::Episode e;
        bool over = false; unsigned long long t = 1000;
        for (; t <= 181000 && !over; t += 5000) over = joinhold::EpisodeOver(e, t, 60);
        Check(!over, "a peer kept every 5 s for 180 s: still kept (a slow joiner's three loads)");
        Check(joinhold::EpisodeOver(e, 186000, 60), "kept again after 185 s in all: the episode is over, the game's rule applies");
        joinhold::Episode f;
        Check(!joinhold::EpisodeOver(f, 1000, 60) && !joinhold::EpisodeOver(f, 50000, 60), "two keeps 49 s apart: one episode, kept");
        Check(!joinhold::EpisodeOver(f, 400000, 60), "a keep 350 s after the last one: a new episode, kept");
        joinhold::Episode g;
        (void)joinhold::EpisodeOver(g, 1000, 60);
        Check(joinhold::EpisodeOver(g, 1000 + 59000, 60) == false && joinhold::EpisodeOver(g, 1000 + 118000, 60) == false && joinhold::EpisodeOver(g, 1000 + 177000, 60) == false && joinhold::EpisodeOver(g, 1000 + 236000, 60) == true,
              "keeps 59 s apart: over after the third hold");
    }
    printf("%s: %d failed\n", gFail ? "FAIL" : "PASS", gFail);
    return gFail ? 1 : 0;
}
