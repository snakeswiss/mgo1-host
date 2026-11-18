// vdoll_react.h - (v150-host beta 25, reactfix) the reaction kind a shooter's game tells the victim of its hit, and its angle
// (docs/VIRTUAL_DOLL.md 58), apart from the host's memory so that the tests check it. US n003a: the hit 0x437F20 runs in the
// ATTACKER's game on its copy of the victim; from 0x438960 it picks the kind it sends in {09 attacker, kind, angle}; the victim's
// own game plays that kind's motion and turns him to the angle (0x439798: the angle into his +0x14A).
#pragma once
#include <cmath>
#include <cstdint>

namespace vreact {

// the victim as the attacker's copy of him holds him: his first state word, his yaw (+0x142) and the yaw he turns to (+0x14A), 16
// bits a turn (-1: not known); room: nothing of the map in the sphere 0x407CB0 asks (a point 1200 mm over his hips: room to be thrown)
struct Victim { unsigned state = 0; int yaw = -1, yawTo = -1; bool room = true; };
struct Out { int kind = 0; uint16_t angle = 0; };          // (kind 0: none)

// on the ground: the state bits 0x1C (down after a roll or a kick), 0x1B (down after a blast or a fall), 3 (lying on his front)
inline bool Ground(unsigned st) { return (st & 0x18000008u) != 0; }
// a flinch on the ground (0x438BD8): 4 (0x1C), 5 (0x1B), else 3
inline int GroundKind(unsigned st) { return (st & 0x10000000u) ? 4 : (st & 0x8000000u) ? 5 : 3; }
inline uint16_t Angle16(float vx, float vz) { return (uint16_t)(int32_t)lrintf(std::atan2(vx, vz) / 6.2831853f * 65536.f); }
// his front (0x438D88: his yaw's unit vector, 0x14A8D0, dotted with the attack's vector, the {14} record's): true when the vector
// runs against his facing, the attack coming at his front
inline bool FromFront(int yaw, float vx, float vz) {
    const float a = (float)(yaw & 0xFFFF) * 6.2831853f / 65536.f;
    return std::sin(a) * vx + std::cos(a) * vz < 0.f;
}
inline uint16_t YawOr(int yaw, uint16_t other) { return yaw >= 0 ? (uint16_t)yaw : other; }

// a burning hit (the flamethrower, the WP grenade: the attack word's 0x2000, the gate's sp+0x4C; 0x311C88 fails, so the angle stays his
// yaw, 0x438170): on the ground 4, 5 or 3 turned to +0x14A (0x438E48); standing 1 from his front at his yaw, 2 from behind at his yaw
// + 0x8000 (0x438DEC)
inline Out Burn(const Victim& v, float vx, float vz, uint16_t ifUnknown) {
    Out o;
    if (Ground(v.state)) { o.kind = GroundKind(v.state); o.angle = YawOr(v.yawTo >= 0 ? v.yawTo : v.yaw, ifUnknown); return o; }
    const uint16_t y = YawOr(v.yaw, ifUnknown);
    const bool front = FromFront(y, vx, vz);
    o.kind = front ? 1 : 2;
    o.angle = (uint16_t)(front ? y : y + 0x8000);
    return o;
}
// the M37 (0xE, 0x438B6C) at a power p (the attack's +0x12, 1000 for the damage table's whole 100: 250 a pellet that reached him):
// 0x311C88 passes, so the angle is the attack's vector's less 0x8000 (the bearing back to the shooter, 0x438224). From 1000: on the
// ground 8 with room over him, else 4, 5 or 3 (+0x14A); standing 6 from his front, 7 from behind (+ 0x8000). 750 to 999: on the
// ground 4, 5 or 3 (+0x14A); standing 1 or 2 the same way. Under 750: none
inline Out M37(const Victim& v, int power, float vx, float vz) {
    Out o;
    if (power < 750) return o;
    const uint16_t back = (uint16_t)(Angle16(vx, vz) + 0x8000);
    if (Ground(v.state)) {
        if (power >= 1000 && v.room) { o.kind = 8; o.angle = back; return o; }
        o.kind = GroundKind(v.state); o.angle = YawOr(v.yawTo >= 0 ? v.yawTo : v.yaw, back); return o;
    }
    const bool front = FromFront(YawOr(v.yaw, back), vx, vz);
    o.kind = power >= 1000 ? (front ? 6 : 7) : (front ? 1 : 2);
    o.angle = (uint16_t)(front ? back : back + 0x8000);
    return o;
}
// a blast's wound (the attack word's 0x200: the frag, the RPG-7, the claymore) or a roll into him (0x400): 0x311C88 passes. On the
// ground 8 with room over him (0x407CB0), else 4, 5 or 3 (+0x14A); standing 8 (a blast) or 9 (a roll), the angle the vector's less
// 0x8000 (0x438C34, 0x438E78)
inline Out Throw(const Victim& v, bool blast, float vx, float vz) {
    Out o;
    const uint16_t back = (uint16_t)(Angle16(vx, vz) + 0x8000);
    if (Ground(v.state) && !v.room) { o.kind = GroundKind(v.state); o.angle = YawOr(v.yawTo >= 0 ? v.yawTo : v.yaw, back); return o; }
    o.kind = Ground(v.state) || blast ? 8 : 9;
    o.angle = back;
    return o;
}
// the punch combo's blows (0x97, 0x96, the kick 0x95: vdoll_wake.h): no reaction on the ground (0x4389D8: the wake path or none); a
// punch carries no reaction flag (the gate 0x438960 shuts: measured, a real client's punch on a standing player sends no {09}); the
// kick knocks down, 9, at the attacker's own yaw less 0x8000 (0x438EA0)
inline Out Blow(const Victim& v, int blow, uint16_t attackerYaw) {
    Out o;
    if (Ground(v.state) || blow != 2) return o;
    o.kind = 9; o.angle = (uint16_t)(attackerYaw + 0x8000);
    return o;
}

}  // namespace vreact
