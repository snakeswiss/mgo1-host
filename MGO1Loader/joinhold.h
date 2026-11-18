// (v150-host beta 18) a dedicated host keeps a peer that is still loading past the game's two silence limits
// (docs/DEDICATED_HOST.md, "A joiner that loads slowly"). The game ends a joiner (its player entry 0x204) when (1) its
// session's reliable layer (0x170E18, each session's tick) gives up a reliable record at its 11th time-out (the record's
// +8; the time-out 1.8 s and more, the session's +0x6C: 25 to 30 s after the record's send), or (2) the player's tick
// (0x175B60) finds no datagram of the session for 30 s (0x1710D0 with 30000 ms). A joiner's game sends nothing while it
// sets a stage up (one long game frame: no actor runs, the session daemons neither), and a round played again just after
// its join gives it two or three such loads in a row: a PS2 does each in a few seconds, a PC whose processors or disc are
// busy (a playable doll beside the host and its virtual dolls) in 10 to 30 s, and the host ended it (r7). Here: a peer
// whose player entry is at 0x212 (connected, its stage loading or waiting for the round) is kept until it has been silent
// MGO1_JOINER_HOLD_S seconds (60; 0: the game's rules); a peer in play or still connecting, the game's rules as they are
#pragma once
#include <cstdint>
#include <cstdlib>

namespace joinhold {
constexpr int kResendLimit = 11;                // the game's (1): the 11th time-out of a reliable record ends its session
constexpr unsigned kSilentMs = 30000;           // the game's (2): a player whose session sent nothing for 30 s is ended
constexpr int kCountKept = kResendLimit - 2;    // the record's count put back: the next time-out resends it (10), the one after asks again
constexpr unsigned kHoldS = 60;                 // the silence a loading peer is kept for, by default
constexpr unsigned kHoldMaxS = 600;             // (v150-host beta 18, review) the longest hold MGO1_JOINER_HOLD_S gives
// (v150-host beta 18, review P2-3) an episode of holds (one keep after another, less than the hold apart) lasts three holds
// at most (180 s by default) whatever the peer's datagrams: a peer at 0x212 that goes on sending and never acknowledges
// (the case the game's resend limit is for) is not kept for ever. A slow joiner's three loads in a row fit in it
struct Episode { unsigned long long first = 0, last = 0; };
inline bool EpisodeOver(Episode& e, unsigned long long nowMs, unsigned holdS) {
    const unsigned long long hold = (unsigned long long)holdS * 1000ull;
    if (!e.first || nowMs - e.last > hold) e.first = nowMs;       // (a new episode)
    e.last = nowMs;
    return nowMs - e.first > 3 * hold;
}
// (v150-host beta 18, review) MGO1_JOINER_HOLD_S as the hold takes it: unset 60; 0 off; 1 to 600 s; more than 600: 600; a
// negative number or not a number: 60 (atoi made -1 a hold of 136 years: a peer gone while it loads kept its seat for ever)
inline unsigned HoldFromEnv(const char* e) {
    if (!e) return kHoldS;
    char* end = nullptr;
    const long v = std::strtol(e, &end, 10);
    if (end == e || *end || v < 0) return kHoldS;
    return v > (long)kHoldMaxS ? kHoldMaxS : (unsigned)v;
}
constexpr uint16_t kLoading = 0x212;            // a player entry connected, its stage loading or waiting for the round
enum Verdict { kEnd, kKeep, kEndSilent };       // the game's rule; kept; ended all the same (silent too long)
// at either limit: the peer's entry (found: its state), its silence (since its last datagram), the hold (s, 0: off)
inline Verdict Decide(bool entryFound, uint16_t entryState, unsigned long long silentMs, unsigned holdS) {
    if (!holdS || !entryFound || entryState != kLoading) return kEnd;
    return silentMs > (unsigned long long)holdS * 1000ull ? kEndSilent : kKeep;
}
}
