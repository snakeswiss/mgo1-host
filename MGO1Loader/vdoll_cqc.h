// vdoll_cqc.h - (v150-host beta 24, cqc) the game's own rule for a CQC throw (docs/VIRTUAL_DOLL.md 40), apart from the host's memory
// so that the tests check it. US n003a (the same tables in every map page read): the throw from a hold is decided by the HOLDER's
// character; the victim's copy on the holder's game plays a thrown motion and at one of its frames sends {04 holder, life 0, stun,
// kind 0, +0xB75, head 0} on the victim's channel 0 (0x3FBB50 -> 0x3FB420 / 0x3FB4F0 / 0x3FB628 / 0x3FB730 -> 0x3FB290 -> 0x409188);
// the victim's own game adds the stun to its pending stun (0x4367A0) and is knocked out only when that reaches its gauge (0x414368).
#pragma once
#include <algorithm>

namespace vcqc {

// the characters (+0xB62): 0 KGB, 1 GRU, 2 the Ocelot Unit, 3 Sokolov, 4 Raikov, 5 Ocelot, 6 Reiko, 7 Rowdy, 8 Snake
const int kSnake = 8;
const int kSneaking = 4;                         // (the rule SNEAKING: the map scripts' varbuf[0x10FC])
// every character holds and throws (Circle held facing an enemy close by, a CQC weapon in hand: measured on a GRU soldier, cqc_m1, and
// on Ocelot, tsneW_wake5); the thrown motion and the stun are the holder's character's
// the stun of the hold's throw (0x3FB1F0): Snake the victim's whole gauge (+0xB6A); any other holder the map script's table by its
// class (the bytes 0x2F0E90: 0 0 0 0 2 1 3 3 1) and the rule: `if varbuf[0x10FC] == 4 { -p 7500 7500 7500 7500 } -e { -p 10000
// 10000 30000 30000 }` (the page command 0x4984CF, read by 0x3FC1F8 into 0x5670A0); halved for a teammate (0x3FB338)
inline int HoldStun(int holderCls, int rule, int victimMax, bool teammate) {
    static const int kIndex[9] = {0, 0, 0, 0, 2, 1, 3, 3, 1};
    static const int kTable[4] = {10000, 10000, 30000, 30000};
    int s;
    if (holderCls == kSnake) s = victimMax;
    else if (rule == kSneaking) s = 7500;
    else s = kTable[kIndex[holderCls >= 0 && holderCls <= 8 ? holderCls : 0]];
    if (teammate) s /= 2;
    return s;
}
// the frame of the victim's thrown motion at which its stun goes, by the holder's class (0x3FBB50's tables 0x557C7C, 0x557C90): a
// soldier or Sokolov 0x3FB420 (motion 0xAF, frame 0x21), Raikov 0x3FB628 (0xC002, 0x32), Ocelot and Snake 0x3FB4F0 (0xAE, 0x32),
// Reiko and Rowdy 0x3FB730 (0xC002, 0xAE)
inline int HoldStunFrame(int holderCls) {
    if (holderCls == 6 || holderCls == 7) return 0xAE;
    if (holderCls >= 4 && holderCls <= 8) return 0x32;
    return 0x21;
}
// the stun record goes out at that frame after the hold record on the holder's own clock (cqc_m1, a GRU soldier's four holds: {0D},
// then {04 .. 10000, kind 0} 36, 30, 36 and 30 frames later: its frame 33 within its 2-frame ticks)
const int kAfterHold = 0;
// the thrown player's own game (cqc_m1, the victim's state words, four throws): lying awake 66 frames after the stun, up at his first
// press then (getting up 74 frames): standing 140 frames after the stun; a roll's knock-down's own timing (mash_c2: 66, then 141)
const int kLieAfter = 66, kUpAfter = 140;
// a stun record of the hold's throw: kind 0, no life (a dart's is 5 or 0x10, a stun grenade's 0x15, a blow's 0x95 to 0x97, 0x9C, 0xAE,
// 0xBA, the throw without a hold 0xCF)
inline bool HoldThrowRecord(int life, int kind) { return life == 0 && kind == 0; }

// the throw without a hold (Circle close to a standing player, any character, any weapon in hand: wake_m1, wake_m4): the hit 0x437F20
// on the thrower's game, kind 0xCF, the damage table's 3000 (0x2F4628), at most the gauge (0x439040), halved for a teammate, with
// the reaction 9 (a fall)
const int kThrowKind = 0xCF, kThrowAmount = 3000;

// what a stun does to a player's gauge (0x4367A0, 0x414368): spent, knocked out when it reaches the gauge; the gauge a player
// starts with (+0xB6A)
const int kGauge = 30000;
inline int After(int stamina, int stun) { return std::max(0, stamina - std::max(0, stun)); }
inline bool KnocksOut(int stamina, int stun) { return stun > 0 && stun >= stamina; }

}  // namespace vcqc
