// vdoll_wake.h - (v150-host beta 22, wake) the game's own rule for a player's blows on a teammate (docs/VIRTUAL_DOLL.md 36), apart
// from the host's memory so that the tests check it. US n003a: the hit 0x437F20 runs on the ATTACKER's game for its copy of the
// victim and sends {04 attacker, life, stun, kind, ?, head} (0x409188); the victim's own game applies the stun (0x4367A0).
#pragma once
#include <algorithm>

namespace vwake {

// the punch combo (the action 0x41B558): Circle pressed, its motion's blows at the motion's frames 11, 23 and 48 (a punch 0x97, a
// punch 0x96, the kick 0x95), each after the first only when its press came before the frame the action asks it at (16, 28)
const int kBlows = 3;
const long long kHitAt[kBlows] = {11, 23, 48};
const long long kPressBy[kBlows] = {0, 16, 28};
const int kKind[kBlows] = {0x97, 0x96, 0x95};
// the damage table (0x2F4360: 3000, 3000, 8000) by the attack's factor (0x408980: 1.5 for both punches with a gun in hand): to the
// stamina, never to the life (0x439040)
const int kAmount[kBlows] = {4500, 4500, 8000};

// the victim as the attacker's copy of him holds him
struct Victim {
    bool lying = false;                          // one of the state flags 0x1C, 0x1B, 3 (on the ground)
    bool out = false;                            // knocked out: the flag 0x99
    bool teammate = false;                       // of the attacker's team (0x438324: the teams' byte of +0x920)
    bool teamRule = true;                        // a team rule (the game's 0x174158); in a deathmatch nobody is a teammate
    int stunMax = 30000;                         // his gauge's maximum (+0xB6A)
    int wakePart = 50;                           // the ally wake's part of it (+0xB74: 50 on a real client, measured: bench wake_m1)
};
inline bool Teammate(const Victim& v) { return v.teamRule && v.teammate; }
// the ally wake (0x43898C): a blow of the combo on a teammate lying knocked out
inline bool Wakes(const Victim& v) { return v.lying && v.out && Teammate(v); }
// the stun of the record a blow sends: negative for the ally wake, his gauge's maximum x +0xB74 / 100 (0x438A30 .. 0x438A60);
// else the blow's amount halved for a teammate (0x43835C), at most his gauge's maximum (0x439078)
inline int Stun(int blow, const Victim& v) {
    if (blow < 0 || blow >= kBlows) return 0;
    if (Wakes(v)) return -(v.stunMax * v.wakePart / 100);
    const int a = Teammate(v) ? kAmount[blow] / 2 : kAmount[blow];
    return std::min(a, v.stunMax);
}
// the reaction kind on a standing victim (0x438E14 on): the kick knocks down (9, as a roll: the attack word's 0x400), a punch
// flinches him (1 seen from his front, 2 from behind); none on a victim lying knocked out (the wake path sets none)
inline int Reaction(int blow, const Victim& v, bool fromFront) {
    if (v.lying) return 0;
    if (blow == 2) return 9;
    return fromFront ? 1 : 2;
}

// (measured, wake_m2) with the game's friendly fire OFF a player's punches, kick and roll do not reach a teammate at all (no hit
// record goes: the ally wake above never runs)
// the CQC throw a player makes pressing Circle close to a standing player, running at him or standing still (wake_m1, friendly fire
// OFF; wake_m4, ON: it reached the teammate and ended his reading): the press, its motion records from 7 frames on every 6 frames, the throw's hit 45
// frames after the press (the effect 0x0220 part 0x0B, kind 0xCF: the table's 3000, halved for a teammate: 1500 measured, no life,
// the reaction 9: he falls), the motion over 86 frames after the press
const long long kThrowHitAt = 45, kThrowEnd = 86, kThrowMotionFrom = 7, kThrowMotionEnd = 80;
const int kThrowKind = 0xCF, kThrowAmount = 3000;
inline int ThrowStun(const Victim& v) { return std::min(Teammate(v) ? kThrowAmount / 2 : kThrowAmount, v.stunMax); }

// (measured, wake_ts_on2: TEAM SNEAKING, friendly fire on, a real client with empty hands; wake_m4, wake_ts_on: the same in a team
// deathmatch and with a rifle) Circle pressed STANDING STILL beside a teammate lying knocked out, once or three times, is another
// move (about 90 to 105 frames, the attacker stepping over him): no record reaches him. Pressed while RUNNING into him (0.92 m)
// and again 12 and 24 frames later it is the combo: its two punches pass over him (no record), its KICK reaches him 57 frames
// after the first press: {14 attacker, 0x2200, part 4, ..} and {04 attacker, 0 0, -15000, 0x95, 0, 0}: the ally wake, half his
// gauge (+0xB74 50); the attacker runs on into him; its motion records from 5 frames on, the end {08 00 00} at 117
const long long kRunPressAt[kBlows] = {0, 12, 24}, kRunKickAt = 57, kRunMotionFrom = 5, kRunCarry = 40, kRunComboEnd = 117;
const float kRunPressFrom = 1000.f;              // (its hips within 1.0 m of his, running)
inline bool Reaches(int blow, const Victim& v) { return !v.lying || blow == 2; }   // (on a teammate lying only the kick lands)

}  // namespace vwake
