// vdoll_food.h - (v150-host beta 25, food: docs/VIRTUAL_DOLL.md 44) the food lying on the maps as a player's game takes and eats
// it, the rule a virtual doll follows (vdoll_body.cpp FoodFrame). Read in the game's code (US n003a) and measured on real clients
// (playable dolls, probe 48: reports/vdoll_food.md):
// - the only food of an online map is the stage script's mushroom command (NewOnlineMushRoomMan): 6 to 10 on each disc map, each a
//   food box lying at its point from the round's start (its option -s 1), the food GOLOVA (code 0x5B) or RUSSIAN GLOWCAP (0x56);
// - a player standing in a box's pick-up zone (400 mm either way along x and z of its place, 1500 mm up and down from it raised by
//   its reach; not knocked out, not a ghost) takes it whatever his gauge: his own game eats it at once (no action, no wait), its
//   engine adding to his stun gauge its maximum times the food's share (0x305F90: 0.45 for GOLOVA, 0.15 for the RUSSIAN GLOWCAP),
//   clamped at the maximum; nothing to his life;
// - his game tells the others on the mushroom man's channel (the room's + 11) {u8 the entry's number, s16 its point x 0.1 (x, y, z),
//   u8 3}; the host removes the box, sends the record on to every player and makes the box again at its place 6000 time units later
//   (20 s on its clock).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vdoll {
namespace food {

const int kGaugeMax = 30000;                      // (a player's stun gauge's maximum, +0xB6A)

// the share of the stun gauge's maximum a food gives back by its class (its descriptor's flags, bits 20 to 22: the engine's eat
// 0x305F90 with its constants 0x542DB0 to 0x542DC0)
inline float Share(int cls) { return cls == 1 ? 0.15f : cls == 2 ? 0.30f : cls == 3 ? 0.45f : cls == 4 ? 1.0f : 0.25f; }
// what eating that food gives back (the engine's cut: the float product made a whole number toward zero)
inline int Gain(int cls) { return (int)((float)kGaugeMax * Share(cls)); }
// the gauge after eating: the gain added, clamped at the maximum (0x303380)
inline int After(int gauge, int gain) { const int g = gauge + gain; return g > kGaugeMax ? kGaugeMax : g < 0 ? 0 : g; }
// the box's pick-up zone (0x4C94D8 with 0x2F5A80's half sizes): the player's hips within 400 mm of its place along x and z, within
// 1500 mm of its place raised by its reach
inline bool InZone(const float* hips, const float* place, float reach) {
    return std::fabs(hips[0] - place[0]) < 400.f && std::fabs(hips[2] - place[2]) < 400.f && std::fabs(hips[1] - (place[1] + reach)) < 1500.f;
}
// a doll goes to a food it sees when eating it would give back at least half of what it gives (the gauge at most its maximum less
// half the gain: a GOLOVA at 23250 or under, a RUSSIAN GLOWCAP at 27750 or under); the game takes a box whatever the gauge of the
// player standing in its zone, so a doll that passes over one eats it as he would
inline bool Wants(int gauge, int gain) { return gain > 0 && gauge >= 0 && gauge <= kGaugeMax - gain / 2; }
// the record a player's game sends on the mushroom man's channel when it takes a box (0x3A7E60): the entry's number (its low
// byte), its point in tenths cut toward zero (x 0.1, cvt.w.s), the state 3
inline std::vector<uint8_t> TakenRecord(int number, const float* at) {
    std::vector<uint8_t> r;
    r.push_back((uint8_t)(number & 0xFF));
    for (int k = 0; k < 3; ++k) {
        const float v = at[k] * 0.1f;
        const int t = std::isfinite(v) ? (int)std::max(-32768.f, std::min(32767.f, v)) : 0;
        r.push_back((uint8_t)(t & 0xFF)); r.push_back((uint8_t)((t >> 8) & 0xFF));
    }
    r.push_back(0x03);
    return r;
}
inline const char* Name(int code) { return code == 0x5B ? "GOLOVA" : code == 0x56 ? "RUSSIAN GLOWCAP" : code == 0x5E ? "RATION" : "food"; }

}  // namespace food
}  // namespace vdoll
