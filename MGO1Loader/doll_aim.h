// (v150-host beta 18) The PLAYABLE DOLL's direct aim (MGO1_DOLL_AIM=direct, the ini's aim=direct, the order `aim direct|pad`;
// docs/PLAYABLE_DOLL.md 26): with it the doll turns its player's facing and its first person view by writing their angles in
// its own game, at the VIRTUAL DOLL's rate for its level, instead of pushing the stick. The turn's arithmetic, kept apart from
// the game's memory so that tests/doll_aim_test.cpp runs it on Linux. Everything else (the buttons, the weapon's rate, the
// bullet, the recoil, the damage) stays the game's.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace doll {
namespace aim {

constexpr float kPi = 3.14159265f;
// vdoll_body.cpp kLevel's turn column (degrees a frame, levels 1 to 10): keep the two alike
constexpr float kTurnDeg[10] = {3.f, 4.f, 5.f, 6.f, 8.f, 9.f, 10.f, 13.f, 18.f, 26.f};
inline float RateDeg(int level) { return kTurnDeg[std::max(1, std::min(10, level)) - 1]; }
inline float Rate(int level) { return RateDeg(level) * kPi / 180.f; }   // (radians a frame)
inline float Wrap(float a) { while (a > kPi) a -= 2.f * kPi; while (a < -kPi) a += 2.f * kPi; return a; }
// one frame of the turn, as the virtual doll's Fight turns its view: toward want by at most rate, no overshoot (the settle: the
// last step is what is left, then it stays on want). Yaw wraps, pitch does not
inline float StepYaw(float cur, float want, float rate) { const float e = Wrap(want - cur); return Wrap(cur + std::max(-rate, std::min(rate, e))); }
inline float StepPitch(float cur, float want, float rate) { const float e = want - cur; return cur + std::max(-rate, std::min(rate, e)); }
// the gun's way in the third person follows the facing: what is left of a turn shrinks to about 0.77 of itself each frame (probe 44:
// the facing written 30 degrees round, the bullet 9.6 degrees behind it when pulled at once, 5.4 two frames later, 2.9 at 4, 1.8 at
// 6, 0.9 at 8, 0.4 at 12, none at 20; the bullet leaves 3 frames after the pull). The game's own lag, not written: the shot waits
inline constexpr float kGunFollow = 0.77f;
inline float GunLagAfter(float lag, int frames) { float l = lag; for (int i = 0; i < frames; ++i) l *= kGunFollow; return l; }
// the third person's bullet (probe 44 on City Under Siege and Graniny Gorki Lab): along the facing, 3.37 degrees down from the muzzle
// (0.45 m ahead of the hips), whatever the height of the one aimed at; it can hit him only where its height at his distance is within
// his body (his ground + 0.15 m to his head's top)
constexpr float kTpvDropDeg = 3.37f;
inline float TpvBulletY(float muzzleY, float dist) { return muzzleY - std::tan(kTpvDropDeg * kPi / 180.f) * std::max(0.f, dist - 450.f); }
inline bool TpvReaches(float muzzleY, float dist, float hisGround, float hisHeadTop) { const float y = TpvBulletY(muzzleY, dist); return y >= hisGround + 150.f && y <= hisHeadTop; }
// the game's angles: s16, 0x10000 a turn (the player's turn at work +0x142: atan2(x, z), as Facing(); the view's yaw the same, its
// pitch over 0 looking down: the view's own pitch is minus it)
inline int To16(float rad) { return (int)(int16_t)(int)std::lrint(Wrap(rad) / (2.f * kPi) * 65536.f); }
inline float From16(int v) { return (float)(int16_t)v / 65536.f * 2.f * kPi; }

}  // namespace aim
}  // namespace doll
