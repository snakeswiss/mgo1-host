// vdoll_blast.h - (v150-host beta 22, blast) what a stun grenade's flash and a smoke grenade's cloud do to a player's eyes,
// by the game's own numbers, for the virtual dolls (docs/VIRTUAL_DOLL.md 31). No platform code: the host gives the blasts and
// the clouds its own game holds (ps2world.cpp), the body (vdoll_body.cpp) asks these rules; tests/vdoll_blast_test.cpp.
//
// The flash (US n003a, the stun blast's act 0x4FF5E0, its mode 0x4FF318, the camera's fade 0x484658 / 0x483EC0): each game
// makes its own copy of a stun grenade's blast and, 16 passes in, whitens its own screen: the distance d from its camera to
// the blast gives a strength (1 within 10 m, none beyond 30 m, (d - 10 m) / 20 m between with a floor of 0.25: the game's
// constants 0x560498 to 0x5604A8, as they are); half of it when the blast is hidden from the camera in the mode the game
// tests the line in; the white lasts 1440 frames times the strength (24 s) when the camera looks within 45 degrees of the
// blast in the full mode, 120 frames times it (2 s) otherwise. The white starts opaque (128) and fades evenly to nothing over
// that time; the game also takes the player's auto aim away for the first half of it (0x800005 to the player, his +0xF3C).
// A player sees nothing through it while it is over 40 of 128 (the playable dolls' rule, doll_brain.cpp Blind): the first
// 88/128 of the white's time.
//
// The smoke (the smoke grenade's manager NewNetworkSmokeManager, 0x455830 adds a cloud, 0x4558A0 its time each pass): 8
// clouds, each a middle, a radius (4500) and a time (3250, 5 a frame: 650 frames). The game's own auto aim refuses a target
// behind a cloud (0x4163B8 and 0x45F790 ask 0x455710: a line from the eye within the radius of a cloud's middle). A player's
// eyes: the cloud as it is drawn, spread over its first second and thinner in its last two (the playable dolls' rule,
// doll_brain.cpp SeeSmokes, measured on the game's clouds by probe 31), a shape seen through it within 4 m.
#pragma once
#include <algorithm>
#include <cmath>

namespace vblast {

// ---- the flash ----
const float kFullWithin = 10000.f, kNoneBeyond = 30000.f, kRamp = 20000.f, kFloor = 0.25f;   // (0x560498, 0x5604A0, 0x5604A8, 0x5604A4)
const float kFacingCos = 0.70710677f;                    // (0x5604B0: within 45 degrees)
const int kWhiteFacing = 1440, kWhiteElse = 120;         // (0x5604B4, 0x5604B8: frames at 60 a second)
const float kWhiteStart = 128.f, kBlindOver = 40.f;      // (the fade's start alpha 0x55BA30; the playable dolls' threshold)

// the strength of the white at that distance (mm) from the eye, by the game's own constants
inline float FlashStrength(float d) {
    if (!(d >= 0.f)) return 0.f;
    if (d < kFullWithin) return 1.f;
    if (d > kNoneBeyond) return 0.f;
    return std::max(kFloor, (d - kFullWithin) / kRamp);
}
// the white's frames for a camera at e looking along f (any length) at a blast at p; hidden: the game's mode 1 (its line test
// in the state 0xBA: half the strength, and never the long white); else its mode 2 (a player in the third person, standing:
// his state words 4 = 0x4000000, 5 = 0, blast_b21_d)
inline int FlashFrames(const float e[3], const float f[3], const float p[3], bool hidden) {
    const float v[3] = {p[0] - e[0], p[1] - e[1], p[2] - e[2]};
    const float d = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float s = FlashStrength(d);
    if (hidden) s *= 0.5f;
    if (s <= 0.f) return 0;
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    const float dot = d > 1.f && fl > 1e-6f ? (v[0] * f[0] + v[1] * f[1] + v[2] * f[2]) / (d * fl) : 0.f;
    const bool facing = !hidden && dot >= kFacingCos;
    return (int)((float)(facing ? kWhiteFacing : kWhiteElse) * s);
}
// the frames of that white a player sees nothing through (it fades evenly from 128: over 40 the first 88/128 of it)
inline int BlindFrames(int white) {
    if (white <= 0) return 0;
    return (int)((float)white * (kWhiteStart - kBlindOver) / kWhiteStart);
}

// ---- the smoke ----
struct Cloud { float c[3]; float r; int left; };         // the game's cloud: its middle, its radius, its time left (5 a frame)
const int kCloudTime = 3250;                             // (a new cloud's time: 650 frames)
const float kShapeWithin = 4000.f;                       // (a shape seen through a cloud this close)
// the cloud as drawn (the radius over its first second and its last two); 0: none now
inline float CloudRadius(const Cloud& k) {
    if (k.left <= 0 || k.left > 20000 || !(k.r > 100.f && k.r < 20000.f)) return 0.f;
    const float age = (float)(kCloudTime - k.left) / 5.f;   // (frames since it went off)
    return k.r * std::max(0.3f, std::min(1.f, age / 60.f)) * std::min(1.f, (float)k.left / 600.f);
}
// the distance from p to the segment a-b
inline float SegDist(const float a[3], const float b[3], const float p[3]) {
    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    const float uu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    float t = uu > 1e-6f ? (w[0] * u[0] + w[1] * u[1] + w[2] * u[2]) / uu : 0.f;
    t = std::max(0.f, std::min(1.f, t));
    const float q[3] = {a[0] + u[0] * t - p[0], a[1] + u[1] * t - p[1], a[2] + u[2] * t - p[2]};
    return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}
// a cloud hides b from an eye at a (a shape is seen within 4 m)
inline bool SmokeHides(const float a[3], const float b[3], const Cloud* k, int n) {
    if (n <= 0) return false;
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < kShapeWithin * kShapeWithin) return false;
    for (int i = 0; i < n; ++i) {
        const float r = CloudRadius(k[i]);
        if (r > 0.f && SegDist(a, b, k[i].c) < r) return true;
    }
    return false;
}

}  // namespace vblast
