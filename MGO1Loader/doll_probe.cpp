// The PLAYABLE DOLL's probes (doll_int.h): measures of the controls and of the menus, in tests
// (MGO1_DOLL_PROBE=<n>); each one logs what it measured ("probe<n> ..." lines). 1: the stick and the
// camera; 2: the facing and the weapon; 3: aiming; 4: hits (the bullets, the first person view,
// Circle close by); 5: the actors around a menu's presses; 6: memory signatures of a menu
// (MGO1_DOLL_SEQ); 7: the navigation (random reachable goals, the stuck counts); 8: the objective's item
// (held, then each button: dropped or not); 9: the mesh's links (climbs, drops); 10: the trigger (the
// bullets of Square's patterns, a weapon each run); 11: the stage's cardboard box.
#include "doll_int.h"
#include "ps2world.h"
#include "ps2rt.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace doll {
namespace in {
namespace {

// ---- probe: measures of the controls, in tests ----
struct Probe { int phase = 0; long long t0 = 0; float p0[3]; };
Probe gPr;
void Probe1Frame() {
    auto logPos = [&](const char* what) {
        float m[16] = {};
        ps2world::ViewMatrix(m);
        Log("probe %s: pos %.1f %.1f %.1f cam fwd %.2f %.2f right %.2f %.2f eye %.0f %.0f %.0f", what, gMe.pos[0], gMe.pos[1], gMe.pos[2], gCam.fwd[0], gCam.fwd[1],
            gCam.right[0], gCam.right[1], m[12], m[13], m[14]);
        // the player's work, for the offline search of its facing (MGO1_DOLL_DEBUG=2)
        if (gDebug >= 2 && gMe.obj) {
            char name[128]; snprintf(name, sizeof name, "%s\\probe_%lld.bin", gDir.c_str(), gFrame);
            if (FILE* f = fopen(name, "wb")) { fwrite(ps2::gMem + (gMe.obj & 0x01FFFFFF), 1, 0x1000, f); fclose(f); }
            snprintf(name, sizeof name, "%s\\mem_%lld.bin", gDir.c_str(), gFrame);
            if (gDebug >= 3) if (FILE* f = fopen(name, "wb")) { fwrite(ps2::gMem, 1, 0x02000000, f); fclose(f); }
        }
    };
    const long long t = gFrame - gPr.t0;
    switch (gPr.phase) {
    case 0: gPr.t0 = gFrame; memcpy(gPr.p0, gMe.pos, 12); gPr.phase = 1; logPos("start"); break;
    case 1:                                             // stick up, full, 60 frames
        gPad.sticks[3] = 0x00;
        if (t % 10 == 0) logPos("up");
        if (t == 60) { logPos("after 60 frames up"); gPr.phase = 2; gPr.t0 = gFrame; memcpy(gPr.p0, gMe.pos, 12); }
        break;
    case 2:                                             // stick right, full
        gPad.sticks[2] = 0xFF;
        if (t % 10 == 0) logPos("right");
        if (t == 60) { logPos("after 60 frames right"); gPr.phase = 3; gPr.t0 = gFrame; memcpy(gPr.p0, gMe.pos, 12); }
        break;
    case 3:                                             // stick up, half (walk)
        gPad.sticks[3] = 0x40;
        if (t == 60) { logPos("after 60 frames half up"); gPr.phase = 4; gPr.t0 = gFrame; }
        break;
    case 4:                                             // cross: crouch
        if (t < 3) gPad.buttons |= 0x4000;
        if (t == 30) logPos("after cross");
        if (t > 30 && t <= 90) gPad.sticks[3] = 0x00;
        if (t == 90) { logPos("after 60 frames up, crouched"); gPr.phase = 5; gPr.t0 = gFrame; }
        break;
    case 5:                                             // cross again: stand?
        if (t < 3) gPad.buttons |= 0x4000;
        if (t == 30) { logPos("after cross again"); gPr.phase = 6; }
        break;
    default: gControl = false; break;
    }
}

// probe 2: the facing and the weapon. Runs in the four world directions (the player's work dumped at
// the end of each, to find its facing), then goes to the nearest other player and tries the weapon:
// Square light (readied), the stick toward the target while readied, Square full (a shot), L1, R1, Circle
void Dump(const char* tag) {
    if (!gMe.obj) return;
    char name[160]; snprintf(name, sizeof name, "%s\\p2_%s_%lld.bin", gDir.c_str(), tag, gFrame);
    if (FILE* f = fopen(name, "wb")) { fwrite(ps2::gMem + (gMe.obj & 0x01FFFFFF), 1, 0x1000, f); fclose(f); }
}
struct Probe2 { int phase = -1; long long t0 = 0; int step = 0; const Player* tgt = nullptr; int tgtId = -1; };
Probe2 gP2;
void Probe2Frame() {
    if (gP2.phase < 0) { gP2.phase = 0; gP2.t0 = gFrame; Log("probe2: start"); }
    const long long t = gFrame - gP2.t0;
    auto next = [&]() { ++gP2.phase; gP2.t0 = gFrame; gP2.step = 0; Log("probe2: phase %d", gP2.phase); };
    const Player* tgt = nullptr;
    float best = 1e30f;
    for (const Player& p : gPlayers)
        if (!p.local && p.alive) { const float dx = p.pos[0] - gMe.pos[0], dz = p.pos[2] - gMe.pos[2], d = dx * dx + dz * dz; if (d < best) { best = d; tgt = &p; } }
    const float dist = tgt ? std::sqrt(best) : -1.f;
    auto say = [&](const char* what) {
        Log("probe2 %s: me %.0f %.0f %.0f life %d | target %s at %.0f %.0f %.0f life %d stun %d dist %.0f | bits %x %x %x %x %x", what, gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.life,
            tgt ? tgt->name.c_str() : "-", tgt ? tgt->pos[0] : 0, tgt ? tgt->pos[1] : 0, tgt ? tgt->pos[2] : 0, tgt ? tgt->life : 0, tgt ? tgt->stun : 0, dist,
            gMe.bits[0], gMe.bits[1], gMe.bits[5], gMe.bits[6], gMe.bits[9]);
    };
    static const float dirs[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
    switch (gP2.phase) {
    case 0: case 1: case 2: case 3:                     // run +z, +x, -z, -x for 40 frames each; dump at the end
        Steer(dirs[gP2.phase][0], dirs[gP2.phase][1], 1.f);
        if (t >= 25) { char tag[16]; snprintf(tag, sizeof tag, "dir%d", gP2.phase); Dump(tag); say(tag); next(); }
        break;
    case 4:                                             // stand still 20 frames, dump (facing -x still)
        if (t >= 20) { Dump("still"); say("still"); next(); }
        break;
    case 5:                                             // walk to 5 m from the nearest other player
        if (!tgt) { if (t >= gP2.step * 120) { say("no one yet"); ++gP2.step; } break; }
        if (dist < 5000.f) { say("near the target"); Steer(0, 0, 0); gP2.step = 0; next(); break; }
        if (!Following() || t % 90 == 0) PlanTo(tgt->pos);
        FollowPath();
        if (t > 3000) { say("could not reach the target"); next(); }
        break;
    case 6:                                             // Square light: readied, 40 frames
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
        if (t >= 5 && gP2.step == 0) { say("square light 5"); Dump("ready5"); gP2.step = 1; }
        if (t >= 39) { say("square light 39"); Dump("ready39"); gP2.step = 0; next(); }
        break;
    case 7:                                             // readied, the stick toward the target 40 frames
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
        if (tgt) Steer(tgt->pos[0] - gMe.pos[0], tgt->pos[2] - gMe.pos[2], 1.f);
        if (t >= gP2.step * 10) { say("readied + stick toward"); ++gP2.step; }
        if (t >= 40) { Dump("aimed"); gP2.step = 0; next(); }
        break;
    case 8:                                             // a shot: Square full 4 frames, then released
        if (t < 4) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }
        if (t >= 30) { say("after a shot"); next(); }
        break;
    case 9:                                             // three more shots from readied
        if ((t % 20) < 10) { gPad.buttons |= 0x8000; gPad.pressures[11] = (t % 20) < 4 ? 0xFF : 0x3F; }
        if (t >= 60) { say("after 3 shots"); next(); }
        break;
    case 10:                                            // L1 held 30 frames, then with Square light
        gPad.buttons |= 0x0400;
        if (t >= 15) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
        if (t >= 14 && gP2.step == 0) { say("L1 held"); Dump("l1"); gP2.step = 1; }
        if (t >= 40 && gP2.step == 1) { say("L1 + square light"); Dump("l1sq"); gP2.step = 2; }
        if (t >= 45) { gP2.step = 0; next(); }
        break;
    case 11:                                            // R1 held 40 frames (first person view?)
        gPad.buttons |= 0x0800;
        if (t >= 20 && gP2.step == 0) { float m[16]; ps2world::ViewMatrix(m); Log("probe2 R1: eye %.0f %.0f %.0f fwd %.2f %.2f %.2f", m[12], m[13], m[14], m[8], m[9], m[10]); say("R1 held"); Dump("r1"); gP2.step = 1; }
        if (t >= 40) { gP2.step = 0; next(); }
        break;
    case 12:                                            // walk into the target and press Circle three times
        if (!tgt) { next(); break; }
        if (dist > 900.f && t < 300) { if (!Following() || t % 60 == 0) PlanTo(tgt->pos); FollowPath(); break; }
        if ((t % 12) < 4) gPad.buttons |= 0x2000;
        if (t >= gP2.step * 12) { say("circle near"); ++gP2.step; }
        if (t > 340) { gP2.step = 0; next(); }
        break;
    default:
        if (t >= gP2.step * 300) { say("probe done"); ++gP2.step; }
        break;
    }
}

// probe 3: aiming. At 6 m from the nearest other player: the weapon readied, turned toward it with
// light pushes of the stick, fired when facing it within 2 degrees (three times); then the first person
// view (R1): the stick's effect on the view, aimed the same way, fired; then L1.
struct Probe3 { int phase = -1; long long t0 = 0; int step = 0, rounds = 0; float mag = 0.25f; };
Probe3 gP3;
void Probe3Frame() {
    if (gP3.phase < 0) { gP3.phase = 0; gP3.t0 = gFrame; Log("probe3: start"); }
    const long long t = gFrame - gP3.t0;
    auto next = [&](int ph) { gP3.phase = ph; gP3.t0 = gFrame; gP3.step = 0; Log("probe3: phase %d", ph); };
    const Player* tgt = nullptr;
    float best = 1e30f;
    for (const Player& p : gPlayers)
        if (!p.local && p.alive) { const float dx = p.pos[0] - gMe.pos[0], dz = p.pos[2] - gMe.pos[2], d = dx * dx + dz * dz; if (d < best) { best = d; tgt = &p; } }
    if (!tgt) { if (t >= gP3.step * 120) { Log("probe3: no one"); ++gP3.step; } return; }
    const float dist = std::sqrt(best);
    const float want = AngleTo(gMe.pos, tgt->pos), face = Facing(), err = Wrap(want - face);
    float m[16] = {}; ps2world::ViewMatrix(m);
    const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    const float camErr = Wrap(want - camYaw);
    auto say = [&](const char* what) {
        Log("probe3 %s: me %.0f %.0f life %d | %s at %.0f %.0f life %d stun %d dist %.0f | facing %.1f want %.1f err %.1f | view yaw %.1f pitch %.1f err %.1f | bits %x %x %x",
            what, gMe.pos[0], gMe.pos[2], gMe.life, tgt->name.c_str(), tgt->pos[0], tgt->pos[2], tgt->life, tgt->stun, dist, face * 57.3f, want * 57.3f, err * 57.3f,
            camYaw * 57.3f, camPitch * 57.3f, camErr * 57.3f, gMe.bits[0], gMe.bits[1], gMe.bits[5]);
    };
    const bool ready = gP3.phase >= 1 && gP3.phase <= 3;
    if (ready) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
    switch (gP3.phase) {
    case 0:                                             // 6 m from it
        if (dist < 6500.f && dist > 4000.f) { Steer(0, 0, 0); say("in place"); next(1); break; }
        if (dist <= 4000.f) { Steer(gMe.pos[0] - tgt->pos[0], gMe.pos[2] - tgt->pos[2], 1.f); break; }   // back off
        if (!Following() || t % 90 == 0) PlanTo(tgt->pos);
        FollowPath();
        break;
    case 1:                                             // readied, 20 frames
        if (t >= 20) { say("readied"); next(2); }
        break;
    case 2: {                                           // turn with light pushes
        if (t >= gP3.step * 4) { say("turning"); ++gP3.step; }
        if (std::fabs(err) < 2.f / 57.3f) { say("facing it"); next(3); break; }
        if (t > 90) { say("turn: gave up"); gP3.mag = gP3.mag < 0.5f ? gP3.mag + 0.15f : 0.2f; next(3); break; }
        const float a = want;
        Steer(std::sin(a), std::cos(a), gP3.mag);
        break; }
    case 3:                                             // fire: full for 12 frames (about two bullets), then readied
        if (t < 12) { gPad.pressures[11] = 0xFF; }
        if (t >= 40) {
            say("after the burst");
            if (++gP3.rounds < 3) next(2); else next(4);
        }
        break;
    case 4:                                             // R1 held: the first person view; the stick right 12 frames, up 12 frames
        gPad.buttons |= 0x0800;
        if (t >= 10 && t < 22) gPad.sticks[2] = 0xFF;
        if (t >= 26 && t < 38) gPad.sticks[3] = 0x00;
        if (t >= gP3.step * 4) { say(t < 10 ? "R1" : t < 26 ? "R1 stick right" : "R1 stick up"); ++gP3.step; }
        if (t >= 44) next(5);
        break;
    case 5: {                                           // R1: aim at it with the stick, then fire
        gPad.buttons |= 0x0800;
        if (t >= gP3.step * 4) { say("R1 aiming"); ++gP3.step; }
        const float pitchWant = std::atan2((tgt->pos[1] + 200.f) - (gMe.pos[1] + 800.f), std::max(1.f, dist));
        const float pe = pitchWant - camPitch;
        if (std::fabs(camErr) < 1.f / 57.3f && std::fabs(pe) < 2.f / 57.3f) { say("R1 on it"); next(6); break; }
        if (t > 150) { say("R1 aim: gave up"); next(6); break; }
        const float sx = std::max(-1.f, std::min(1.f, camErr * 4.f)), sy = std::max(-1.f, std::min(1.f, pe * 4.f));
        gPad.sticks[2] = (uint8_t)(0x80 + (int)(sx * 127));
        gPad.sticks[3] = (uint8_t)(0x80 - (int)(sy * 127));
        break; }
    case 6:                                             // R1 + fire
        gPad.buttons |= 0x0800 | 0x8000;
        gPad.pressures[11] = t < 12 ? 0xFF : 0x3F;
        if (t >= 40) { say("R1 after the burst"); next(7); }
        break;
    case 7:                                             // L1 + readied + the stick toward it
        gPad.buttons |= 0x0400 | 0x8000; gPad.pressures[11] = 0x3F;
        if (t < 30) Steer(std::sin(want), std::cos(want), 0.3f);
        if (t >= gP3.step * 5) { say("L1 readied"); ++gP3.step; }
        if (t >= 40) next(8);
        break;
    default:
        if (t >= gP3.step * 300) { say("probe3 done"); ++gP3.step; }
        break;
    }
}

// probe 4: hits. The bullets (NewBullet, their work when they appear), single shots readied and in the
// first person view (the stick's rates there first), at the chest then the head, then Circle close by.
struct BulletSeen { uint32_t work = 0; long long at = -100; };
BulletSeen gBullet;
void FindBullet(uint32_t w, const std::string& name, void* ctx) { if (name == "NewBullet") *(uint32_t*)ctx = w; }
void WatchBullets() {
    uint32_t w = 0;
    ps2world::ForEachActor(&FindBullet, &w);
    if (!w) { gBullet.work = 0; return; }
    if (w == gBullet.work && gFrame - gBullet.at < 3) return;
    const bool fresh = w != gBullet.work;
    gBullet.work = w; if (fresh) gBullet.at = gFrame;
    char b[1400]; int n = snprintf(b, sizeof b, "bullet %08x (%s):", w, fresh ? "new" : "again");
    for (uint32_t o = 0x20; o < 0x140 && n < (int)sizeof b - 16; o += 4) {
        const float f = RF(w + o); const uint32_t u = R32(w + o);
        if (std::fabs(f) > 1e-3f && std::fabs(f) < 1e7f) n += snprintf(b + n, sizeof b - n, " +%x=%.1f", o, f);
        else if (u) n += snprintf(b + n, sizeof b - n, " +%x=#%x", o, u);
    }
    Log("%s", b);
}
struct Probe4 { int phase = -1; long long t0 = 0; int step = 0, shots = 0, sub = 0; float yaw0 = 0, pitch0 = 0; };
Probe4 gP4;
void Probe4Frame() {
    if (gP4.phase < 0) { gP4.phase = 0; gP4.t0 = gFrame; Log("probe4: start"); }
    const long long t = gFrame - gP4.t0;
    auto next = [&](int ph) { gP4.phase = ph; gP4.t0 = gFrame; gP4.step = 0; gP4.shots = 0; gP4.sub = 0; Log("probe4: phase %d", ph); };
    WatchBullets();
    const Player* tgt = nullptr;
    float best = 1e30f;
    for (const Player& p : gPlayers)
        if (!p.local && p.alive) { const float dx = p.pos[0] - gMe.pos[0], dz = p.pos[2] - gMe.pos[2], d = dx * dx + dz * dz; if (d < best) { best = d; tgt = &p; } }
    if (!tgt) { if (t >= gP4.step * 120) { Log("probe4: no one"); ++gP4.step; } return; }
    const float dist = std::sqrt(best);
    const float want = AngleTo(gMe.pos, tgt->pos), face = Facing(), err = Wrap(want - face);
    float m[16] = {}; ps2world::ViewMatrix(m);
    const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    const float eye[3] = {m[12], m[13], m[14]};
    auto say = [&](const char* what) {
        Log("probe4 %s: me %.0f %.0f %.0f life %d | %s at %.0f %.0f %.0f life %d stun %d state %x dist %.0f | facing %.1f want %.1f err %.1f | view yaw %.1f pitch %.1f eye %.0f %.0f %.0f | bits %x %x %x",
            what, gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.life, tgt->name.c_str(), tgt->pos[0], tgt->pos[1], tgt->pos[2], tgt->life, tgt->stun, tgt->bits[0], dist,
            face * 57.3f, want * 57.3f, err * 57.3f, camYaw * 57.3f, camPitch * 57.3f, eye[0], eye[1], eye[2], gMe.bits[0], gMe.bits[1], gMe.bits[5]);
    };
    // the first person view aimed at a height above the target's hips (chest +450, head +850)
    auto fpvAim = [&](float above, float* ye, float* pe) {
        const float tx = tgt->pos[0] - eye[0], tz = tgt->pos[2] - eye[2], ty = tgt->pos[1] + above - eye[1];
        *ye = Wrap(std::atan2(tx, tz) - camYaw);
        *pe = std::atan2(ty, std::sqrt(tx * tx + tz * tz)) - camPitch;
    };
    auto stickFpv = [&](float ye, float pe) {         // stick right turns the view right (yaw down), up raises it
        auto s = [](float deg) { const float a = std::fabs(deg); float v = a < 0.3f ? 0.f : std::min(1.f, 0.35f + a * 0.05f); return deg < 0 ? -v : v; };
        const float sx = -s(ye * 57.3f), sy = s(pe * 57.3f);
        gPad.sticks[2] = (uint8_t)(0x80 + (int)(sx * 127));
        gPad.sticks[3] = (uint8_t)(0x80 - (int)(sy * 127));
    };
    switch (gP4.phase) {
    case 0:                                             // 5 to 7 m from it
        if (dist < 7000.f && dist > 4500.f) { Steer(0, 0, 0); say("in place"); next(1); break; }
        if (dist <= 4500.f) { Steer(gMe.pos[0] - tgt->pos[0], gMe.pos[2] - tgt->pos[2], 1.f); break; }
        if (!Following() || t % 90 == 0) PlanTo(tgt->pos);
        FollowPath();
        break;
    case 1:                                             // readied, turned toward it (a push of 0.45)
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
        if (t < 10) break;
        if (std::fabs(err) < 1.5f / 57.3f) { Steer(0, 0, 0); say("facing it"); next(2); break; }
        if (t > 120) { say("turn: gave up"); next(2); break; }
        Steer(std::sin(want), std::cos(want), std::fabs(err) > 8.f / 57.3f ? 0.62f : 0.56f);
        break;
    case 2:                                             // readied: single shots (full 8 frames: a bullet at the 4th), 30 frames apart
        gPad.buttons |= 0x8000; gPad.pressures[11] = (t % 30) < 8 && t >= 10 ? 0xFF : 0x3F;
        if (t % 30 == 5 && t >= 10) say("shot");
        if (t % 30 == 29 && t >= 10) { say("after the shot"); if (++gP4.shots >= 4) next(3); else if (std::fabs(err) > 2.f / 57.3f) { gP4.phase = 1; gP4.t0 = gFrame; } }
        break;
    case 3: {                                           // R1: the stick's rates (0.3, 0.5, 0.7, 1.0 right, 12 frames each, 8 frames between)
        gPad.buttons |= 0x0800;
        if (t < 12) break;
        const int k = (int)((t - 12) / 20), r = (int)((t - 12) % 20);
        static const float mags[4] = {0.3f, 0.5f, 0.7f, 1.0f};
        if (k >= 4) { next(4); break; }
        if (r == 0) { gP4.yaw0 = camYaw; gP4.pitch0 = camPitch; }
        if (r < 12) gPad.sticks[2] = (uint8_t)(0x80 + (int)(mags[k] * 127));
        if (r == 12 || r == 4 || r == 8) Log("probe4 R1 rate: stick %.1f after %d frames: yaw %.2f deg", mags[k], r, Wrap(camYaw - gP4.yaw0) * 57.3f);
        if (r == 19) Log("probe4 R1 rate: stick %.1f, 12 frames then 8 free: yaw %.2f deg", mags[k], Wrap(camYaw - gP4.yaw0) * 57.3f);
        break; }
    case 4: case 5: {                                   // R1 aimed at the chest (4) then the head (5): 3 single shots each
        gPad.buttons |= 0x0800;
        float ye, pe; fpvAim(gP4.phase == 4 ? 450.f : 850.f, &ye, &pe);
        if (gP4.sub == 0) {                             // aiming
            if (t % 6 == 0) say(gP4.phase == 4 ? "R1 aiming (chest)" : "R1 aiming (head)");
            if (std::fabs(ye) < 0.4f / 57.3f && std::fabs(pe) < 0.5f / 57.3f) { gP4.sub = 1; gP4.t0 = gFrame; say("R1 on it"); break; }
            if (t > 200) { say("R1 aim: gave up"); gP4.sub = 1; gP4.t0 = gFrame; break; }
            stickFpv(ye, pe);
            break;
        }
        gPad.buttons |= 0x8000; gPad.pressures[11] = t < 8 ? 0xFF : 0x3F;       // a shot, then 30 frames
        if (t == 5) say("R1 shot");
        if (t >= 30) { say("R1 after the shot"); gP4.sub = 0; gP4.t0 = gFrame; if (++gP4.shots >= 3) next(gP4.phase + 1); }
        break; }
    case 6:                                             // close by (under 1 m), Circle 3 times, then held 40 frames
        if (gP4.sub == 0) {
            if (dist < 1000.f) { Steer(0, 0, 0); gP4.sub = 1; gP4.t0 = gFrame; say("close"); break; }
            if (t > 600) { say("could not come close"); next(7); break; }
            Steer(tgt->pos[0] - gMe.pos[0], tgt->pos[2] - gMe.pos[2], dist > 2500.f ? 1.f : 0.6f);
            break;
        }
        if (t < 36) { if ((t % 12) < 3) gPad.buttons |= 0x2000; if (t % 12 == 6) say("circle"); break; }
        if (t < 80) { gPad.buttons |= 0x2000; if (t % 8 == 0) say("circle held"); break; }
        if (t < 120) { if (t % 10 == 0) say("after circle"); break; }
        next(7);
        break;
    default:
        if (t >= gP4.step * 120) { say("probe4 done"); ++gP4.step; }
        if (t > 600) next(0);                           // again, from wherever it is now
        break;
    }
}

// probe 5: the menus of a match (TEAM SELECT, the equipment). While the local player is not in the
// world, a press every 100 frames (down, down, up, right, left, then nothing: the steps' Circle
// chooses); the actors' works compared around each press: the fields that changed with it and kept
// their new value (a cursor) are logged
struct Snap { std::map<uint32_t, std::pair<std::string, std::vector<uint8_t>>> w; };
void SnapActor(uint32_t w, const std::string& name, void* ctx) {
    if (name.rfind("NewNetwork", 0) == 0 || name.rfind("GM_", 0) == 0 || name.rfind("DG_", 0) == 0) return;
    auto& e = ((Snap*)ctx)->w[w];
    e.first = name;
    const uint32_t a = w & 0x01FFFFFF;
    const uint32_t size = (name.rfind("NewHud", 0) == 0 || name.rfind("NewOLM", 0) == 0 || name.rfind("NewSuper", 0) == 0 || name.find("Menu") != std::string::npos) ? 0x3000 : 0x800;
    if (a + size <= 0x02000000) e.second.assign(ps2::gMem + a, ps2::gMem + a + size);
}
struct Probe5 { int k = 0; long long next = -1, mapAt = -1; Snap before, after; bool world = false; };
Probe5 gP5;
void Probe5Frame() {
    // before the first spawn (the menu of the join): down, down, up, right, left, Circle, then every 300
    // frames Circle; in the world: START, down x4, up x2, START
    static const uint32_t pre[] = {0x40, 0x40, 0x10, 0x20, 0x80, 0x2000};
    static const char* preNames[] = {"down", "down", "up", "right", "left", "circle"};
    static const uint32_t post[] = {0x08, 0x40, 0x40, 0x40, 0x40, 0x10, 0x10, 0x08};
    static const char* postNames[] = {"start", "down", "down", "down", "down", "up", "up", "start"};
    const bool inWorld = gMe.obj && gMe.alive && !gMe.frozen;
    gControl = true;                                    // the steps' presses held off: only the probe's
    if (gP5.mapAt < 0) gP5.mapAt = gFrame;
    if (!gP5.world && inWorld) { gP5.world = true; gP5.k = 0; gP5.next = gFrame + 300; Log("probe5: in the world"); }
    const uint32_t* seq = gP5.world ? post : pre;
    const char** names = gP5.world ? postNames : preNames;
    const int n = gP5.world ? 8 : 6;
    if (!gP5.world && gP5.next < 0) gP5.next = gP5.mapAt + 900;
    if (gP5.k >= n) {
        if (!gP5.world && (gFrame - gP5.next) % 300 == 0) { gControl = true; gPad.buttons = 0x2000; Log("probe5: circle again"); }
        if (!gP5.world && (gFrame - gP5.next) % 300 < 4) { gControl = true; gPad.buttons = 0x2000; }
        return;
    }
    if (gFrame == gP5.next - 5) { gP5.before = Snap(); ps2world::ForEachActor(&SnapActor, &gP5.before); }
    if (gFrame >= gP5.next && gFrame < gP5.next + 4) { gControl = true; gPad.buttons = seq[gP5.k]; }
    if (gFrame == gP5.next) Log("probe5: %s", names[gP5.k]);
    if (gFrame == gP5.next + 30) { gP5.after = Snap(); ps2world::ForEachActor(&SnapActor, &gP5.after); }
    if (gFrame == gP5.next + 60) {
        Snap now; ps2world::ForEachActor(&SnapActor, &now);
        for (auto& a : gP5.after.w) {
            auto b = gP5.before.w.find(a.first); auto c = now.w.find(a.first);
            if (b == gP5.before.w.end()) { Log("probe5 %s: new %s %08x", names[gP5.k], a.second.first.c_str(), a.first); continue; }
            if (c == now.w.end() || b->second.first != a.second.first) continue;
            const auto &x = b->second.second, &y = a.second.second, &z = c->second.second;
            if (x.size() != y.size() || y.size() != z.size()) continue;
            std::string s; int m = 0;
            for (size_t o = 0; o + 2 <= y.size(); o += 2) {
                uint16_t vx, vy, vz; memcpy(&vx, &x[o], 2); memcpy(&vy, &y[o], 2); memcpy(&vz, &z[o], 2);
                if (vx != vy && vy == vz) { char t[48]; snprintf(t, sizeof t, " +%zx:%x->%x", o, vx, vy); s += t; ++m; }
            }
            if (m > 0 && m <= 24) Log("probe5 %s: %s %08x%s", names[gP5.k], a.second.first.c_str(), a.first, s.c_str());
            else if (m > 24) Log("probe5 %s: %s %08x: %d fields", names[gP5.k], a.second.first.c_str(), a.first, m);
        }
        for (auto& b : gP5.before.w) if (!gP5.after.w.count(b.first)) Log("probe5 %s: gone %s %08x", names[gP5.k], b.second.first.c_str(), b.first);
        ++gP5.k; gP5.next = gFrame + 40;
    }
}

// probe 6: memory signatures of a menu. MGO1_DOLL_SEQ="w:08,40,40,..." (w: in the world, m: before the
// spawn): each button pressed 4 frames, then 40 frames later all of the PS2 memory written to
// <dir>\seq_<i>.bin (seq_0.bin before the first press): a menu's cursor is the byte that follows the
// presses (tools: a search for a value going 0 1 2 3 4 3 2 ...)
struct Probe6 { std::vector<uint32_t> seq; bool world = true; bool parsed = false; int k = -1; long long next = -1; };
Probe6 gP6;
void DumpMem(const char* tag) {
    char name[200]; snprintf(name, sizeof name, "%s\\%s.bin", gDir.c_str(), tag);
    if (FILE* f = fopen(name, "wb")) { fwrite(ps2::gMem, 1, 0x02000000, f); fclose(f); }
}
void Probe6Frame() {
    if (!gP6.parsed) {
        gP6.parsed = true;
        const char* e = getenv("MGO1_DOLL_SEQ");
        if (e && (e[0] == 'w' || e[0] == 'm') && e[1] == ':') {
            gP6.world = e[0] == 'w';
            for (const char* q = e + 2; *q;) { char* end; const unsigned long v = strtoul(q, &end, 16); if (end == q) break; gP6.seq.push_back((uint32_t)v); q = *end ? end + 1 : end; }
        }
        Log("probe6: %zu presses %s", gP6.seq.size(), gP6.world ? "in the world" : "before the spawn");
    }
    const bool inWorld = gMe.obj && gMe.alive && !gMe.frozen;
    gControl = true;
    if (gP6.k < 0) {
        static long long outSince = -1;
        if (gP6.world && !inWorld) {                    // the menu before the spawn: Circle every 300 frames
            if (outSince < 0) outSince = gFrame;
            if ((gFrame - outSince) % 300 >= 296) gPad.buttons = 0x2000;
        } else outSince = -1;
        if (inWorld != gP6.world) { gP6.next = -1; return; }
        if (gP6.next < 0) gP6.next = gFrame + (gP6.world ? 300 : 600);
        if (gFrame < gP6.next) return;
        DumpMem("seq_0"); Log("probe6: seq_0 written"); gP6.k = 0; gP6.next = gFrame + 10;
        return;
    }
    if (gP6.k >= (int)gP6.seq.size()) return;
    if (gFrame >= gP6.next && gFrame < gP6.next + 4) gPad.buttons = gP6.seq[gP6.k];
    if (gFrame == gP6.next) Log("probe6: press %x", gP6.seq[gP6.k]);
    if (gFrame == gP6.next + 44) {
        char tag[32]; snprintf(tag, sizeof tag, "seq_%d", gP6.k + 1);
        DumpMem(tag);
        const uint32_t hp = ps2world::ActorByName("NewHudPause"), sp = ps2world::ActorByName("NewSuperPlayer");
        char f[256]; int n = 0;
        for (uint32_t o = 0x326; o < 0x348 && hp; o += 2) n += snprintf(f + n, sizeof f - n, " %x", R16(hp + o));
        Log("probe6: %s written; pause+326:%s | super %x %x %x", tag, f, sp ? R32(sp + 0x58) : 0, sp ? R32(sp + 0x5C) : 0, sp ? R32(sp + 0x64) : 0);
        ++gP6.k; gP6.next = gFrame + 6;
        if (gP6.k >= (int)gP6.seq.size()) Log("probe6: done");
    }
}

// probe 7: the navigation. Random reachable goals one after the other; every 10 s the counts (goals,
// reached, stuck, replanned, given up) and the distance walked
struct Probe7 { int goals = 0; double dist = 0; float last[3] = {0, 0, 0}; bool have = false; long long stillSince = -1; int tryStep = -1; long long tryAt = 0; float tryFrom[3]; };
Probe7 gP7;
// frozen (not a millimetre in 200 frames with the stick pushed): each button in turn, the stick toward
// the path, the position logged after each
bool Probe7Unfreeze() {
    static const struct { uint16_t bit; uint8_t pressure; const char* name; int frames; } kTry[] = {
        {0x4000, 0, "cross", 4}, {0x1000, 0, "triangle", 4}, {0x0800, 0, "r1", 20}, {0x0400, 0, "l1", 20}, {0x2000, 0, "circle", 4},
        {0x8000, 0x3F, "square light", 30}, {0x4000, 0, "cross again", 4}, {0x0100, 0, "l2", 4}, {0x0200, 0, "r2", 4}};
    const int n = (int)(sizeof kTry / sizeof kTry[0]);
    if (gP7.tryStep < 0) return false;
    const long long t = gFrame - gP7.tryAt;
    const float* g = PathGoal();
    if (g) Steer(g[0] - gMe.pos[0], g[2] - gMe.pos[2], 1.f);
    if (t < kTry[gP7.tryStep].frames) { gPad.buttons |= kTry[gP7.tryStep].bit; if (kTry[gP7.tryStep].pressure) gPad.pressures[11] = kTry[gP7.tryStep].pressure; }
    if (t == 60) {
        const float moved = Dist2D(gMe.pos, gP7.tryFrom);
        Log("probe7: frozen, %s: moved %.0f (hips %+.0f), state %x %x %x", kTry[gP7.tryStep].name, moved, gMe.pos[1] - gP7.tryFrom[1], gMe.bits[0], gMe.bits[6], gMe.bits[9]);
        if (moved > 300.f || ++gP7.tryStep >= n) { gP7.tryStep = -1; gP7.stillSince = -1; return false; }
        gP7.tryAt = gFrame; memcpy(gP7.tryFrom, gMe.pos, sizeof gP7.tryFrom);
    }
    return true;
}
void Probe7Frame() {
    if (gP7.have) {
        const float m = Dist2D(gMe.pos, gP7.last);
        if (m < 400.f) gP7.dist += m;
        if (m < 1.f && std::fabs(gMe.pos[1] - gP7.last[1]) < 1.f) { if (gP7.stillSince < 0) gP7.stillSince = gFrame; } else gP7.stillSince = -1;
    }
    memcpy(gP7.last, gMe.pos, sizeof gP7.last); gP7.have = true;
    if (Probe7Unfreeze()) return;
    if (gP7.stillSince >= 0 && gFrame - gP7.stillSince > 200 && Following()) {
        Log("probe7: frozen at %.0f %.0f %.0f for 200 frames (state %x %x %x): trying the buttons", gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.bits[0], gMe.bits[6], gMe.bits[9]);
        gP7.tryStep = 0; gP7.tryAt = gFrame; memcpy(gP7.tryFrom, gMe.pos, sizeof gP7.tryFrom);
        return;
    }
    if (!Following()) {
        float to[3];
        for (int k = 0; k < 8; ++k) {
            if (!RandomReachable(40000.f, to)) continue;
            if (PlanTo(to)) { ++gP7.goals; Log("going to %.0f %.0f %.0f", to[0], to[1], to[2]); break; }
        }
    }
    if (gFrame % 600 == 0)
        Log("explore: %d goals, %d reached, %d stuck (%d replanned, %d given up), %.0f m walked", gP7.goals, gNavStats.reached, gNavStats.stuck,
            gNavStats.replans, gNavStats.failed, gP7.dist / 1000.0);
    FollowPath();
}

// probe 8: the objective's item (capture's KEROTAN, rescue's GA-KO: NewOnlineItemChara2). Walks to the
// first item until it holds it, then presses each button once (6 frames), then holds it (45 frames),
// logging whether the item is still held; dropped, it walks back to it and goes on with the next button
struct Probe8 { int phase = 0, button = 0, step = 0; long long at = 0, holdSince = -1; float item[3] = {0, 0, 0}; };
Probe8 gP8;
void Probe8Frame() {
    static const struct { uint16_t bit; const char* name; } kButtons[] = {
        {0x1000, "triangle"}, {0x2000, "circle"}, {0x4000, "cross"}, {0x8000, "square"}, {0x0800, "r1"}, {0x0400, "l1"},
        {0x0200, "r2"}, {0x0100, "l2"}, {0x0004, "r3"}, {0x0002, "l3"}, {0x0001, "select"}, {0x0010, "up"}, {0x0040, "down"}};
    const int nb = (int)(sizeof kButtons / sizeof kButtons[0]);
    const uint32_t man = ps2world::ActorByName("NewOnlineItemChara2", 0);
    if (!man) { if (gFrame % 300 == 0) Log("probe8: no objective item"); return; }
    const uint32_t it = man + 0xB0;
    float pos[3]; for (int i = 0; i < 3; ++i) pos[i] = RF(it + 0x90 + 4 * i);
    const uint32_t holder = R32(it + 0xD4);
    const bool mine = holder && holder == gMe.obj;
    if (gFrame % 120 == 0)
        Log("probe8: phase %d button %d step %d, item at %.0f %.0f %.0f holder %08x (me %08x) goal team %d, my team %d, bits %x", gP8.phase, gP8.button, gP8.step,
            pos[0], pos[1], pos[2], holder, gMe.obj, R8(it + 0xF8), gMyTeam, gMe.bits[0]);
    if (gP8.button >= nb) { if (gFrame % 600 == 0) Log("probe8: done"); return; }
    switch (gP8.phase) {
    case 0:                                             // to the item until it is held
        if (mine) { Log("probe8: holding it at %.0f %.0f %.0f", gMe.pos[0], gMe.pos[1], gMe.pos[2]); StopPath(); gP8.phase = 1; gP8.at = gFrame; break; }
        if (holder) { if (gFrame % 120 == 0) Log("probe8: someone else holds it"); }
        if (!Following() || Dist2D(PathGoal() ? PathGoal() : pos, pos) > 600.f) PlanTo(pos);
        if (Dist2D(gMe.pos, pos) < 700.f) { const float a = AngleTo(gMe.pos, pos); Steer(std::sin(a), std::cos(a), 0.5f); }
        else FollowPath();
        break;
    case 1: {                                           // still 40 frames, then the button (6 frames), 60 frames to see
        const long long t = gFrame - gP8.at;
        const uint16_t bit = kButtons[gP8.button].bit;
        const int press = gP8.step == 0 ? 6 : 45;
        if (t >= 40 && t < 40 + press) gPad.buttons |= bit;
        if (t == 40 + press + 60) {
            Log("probe8: %s %s: %s (holder %08x, bits %x, life %d)", kButtons[gP8.button].name, gP8.step == 0 ? "pressed" : "held", mine ? "STILL HELD" : "DROPPED",
                holder, gMe.bits[0], gMe.life);
            if (gP8.step == 0) gP8.step = 1; else { gP8.step = 0; ++gP8.button; }
            gP8.at = gFrame;
            if (!mine) gP8.phase = 0;
        }
        break;
    }
    }
}

// probe 9: the mesh's links. The nearest climbs (a ledge up to 1.3 m, the action button) and drops: to
// the link's start, then 60 frames pushing toward its end without a button, then 150 frames pushing and
// pressing Triangle (3 frames every 20); the hips' height and the distance to the end logged
struct Probe9 { int phase = 0, tried = 0, climbs = 0, drops = 0; long long at = 0; float a[3], b[3]; uint8_t area = 0; float y0 = 0; std::vector<int> done; };
Probe9 gP9;
bool Probe9Pick() {
    const NavMesh& nav = *gMap->nav;
    int best = -1; float bd = 1e30f;
    for (int i = 0; i < nav.LinkCount(); ++i) {
        if (std::find(gP9.done.begin(), gP9.done.end(), i) != gP9.done.end()) continue;
        float a[3], b[3]; uint8_t ar = 0;
        nav.LinkAt(i, a, b, &ar);
        if (ar == AREA_CLIMB ? gP9.climbs >= 5 : gP9.drops >= 3) continue;
        const float d = Dist2D(gMe.pos, a) + (ar == AREA_CLIMB ? 0.f : 15000.f);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return false;
    gP9.done.push_back(best);
    nav.LinkAt(best, gP9.a, gP9.b, &gP9.area);
    if (!PlanTo(gP9.a)) { Log("probe9: link %d (%s) at %.0f %.0f %.0f: no way there", best, gP9.area == AREA_CLIMB ? "climb" : "drop", gP9.a[0], gP9.a[1], gP9.a[2]); return true; }
    Log("probe9: link %d (%s) from %.0f %.0f %.0f to %.0f %.0f %.0f (%.0f up), %.0f m away", best, gP9.area == AREA_CLIMB ? "climb" : "drop", gP9.a[0], gP9.a[1], gP9.a[2],
        gP9.b[0], gP9.b[1], gP9.b[2], gP9.b[1] - gP9.a[1], bd / 1000.f);
    gP9.phase = 1; gP9.at = gFrame;
    return true;
}
void Probe9Frame() {
    const long long t = gFrame - gP9.at;
    switch (gP9.phase) {
    case 0:
        if (gP9.tried >= 8 || !Probe9Pick()) { if (gFrame % 600 == 0) Log("probe9: done (%d climbs, %d drops tried)", gP9.climbs, gP9.drops); return; }
        break;
    case 1:                                             // to the link's start
        if (Dist2D(gMe.pos, gP9.a) < 350.f || (!Following() && Dist2D(gMe.pos, gP9.a) < 900.f)) {
            StopPath(); gP9.phase = 2; gP9.at = gFrame; gP9.y0 = gMe.pos[1];
            Log("probe9: at the start (%.0f %.0f %.0f), hips %.0f above the ground", gMe.pos[0], gMe.pos[1], gMe.pos[2], HipHeight());
            break;
        }
        if (!Following() || t > 1800) { Log("probe9: could not reach the start (%.0f m away)", Dist2D(gMe.pos, gP9.a) / 1000.f); gP9.phase = 0; ++gP9.tried; break; }
        FollowPath();
        break;
    case 2: case 3: {                                   // 2: the stick only, 3: the stick and Triangle
        const float dx = gP9.b[0] - gMe.pos[0], dz = gP9.b[2] - gMe.pos[2];
        Steer(dx, dz, 0.7f);
        if (gP9.phase == 3 && t % 20 < 3) gPad.buttons |= 0x1000;
        if (t % 15 == 0)
            Log("probe9: %s t %lld: pos %.0f %.0f %.0f (%+.0f), %.0f from the end, bits %x", gP9.phase == 2 ? "stick" : "stick+triangle", t, gMe.pos[0], gMe.pos[1], gMe.pos[2],
                gMe.pos[1] - gP9.y0, Dist2D(gMe.pos, gP9.b), gMe.bits[0]);
        const bool there = Dist2D(gMe.pos, gP9.b) < 400.f && std::fabs(gMe.pos[1] - 800.f - gP9.b[1]) < 400.f;
        if (there || t >= (gP9.phase == 2 ? 60 : 150)) {
            if (there || gP9.phase == 3) {
                Log("probe9: RESULT %s %s: %s (hips %+.0f, %.0f from the end)", gP9.area == AREA_CLIMB ? "climb" : "drop", gP9.phase == 2 ? "stick only" : "with triangle",
                    there ? "MADE IT" : "FAILED", gMe.pos[1] - gP9.y0, Dist2D(gMe.pos, gP9.b));
                (gP9.area == AREA_CLIMB ? gP9.climbs : gP9.drops)++; ++gP9.tried;
                gP9.phase = 0;
            } else { gP9.phase = 3; gP9.at = gFrame; }
        }
        break;
    }
    }
}

// probe 10: the trigger. Patterns of Square's pressure (held, let go, taps) at nothing, the bullets
// counted for each (NewBullet actors born: a new work, or one whose point is back at its muzzle): how
// each weapon fires (MGO1_DOLL_INI's loadout picks it), whether it runs empty, R2 twice
struct BulletBorn { uint32_t w; float p[3]; };
std::vector<BulletBorn> gBornLast;
void CollectBullets(uint32_t w, const std::string& name, void* ctx) {
    if (name != "NewBullet") return;
    BulletBorn b; b.w = w;
    for (int i = 0; i < 3; ++i) b.p[i] = RF(w + 0x100 + 4 * i);
    ((std::vector<BulletBorn>*)ctx)->push_back(b);
}
int BulletsBorn() {
    std::vector<BulletBorn> now;
    ps2world::ForEachActor(&CollectBullets, &now);
    int born = 0;
    for (const BulletBorn& b : now) {
        const BulletBorn* was = nullptr;
        for (const BulletBorn& o : gBornLast) if (o.w == b.w) was = &o;
        float m[3]; for (int i = 0; i < 3; ++i) m[i] = RF(b.w + 0xE0 + 4 * i);
        const float fromMuzzle = std::sqrt((b.p[0] - m[0]) * (b.p[0] - m[0]) + (b.p[2] - m[2]) * (b.p[2] - m[2]));
        if (!was || (fromMuzzle < 1.f && Dist2D(was->p, b.p) > 1.f)) ++born;
    }
    gBornLast.swap(now);
    return born;
}
struct Probe10 { int phase = 0, step = 0, got = 0, total = 0; long long at = 0; };
Probe10 gP10;
bool Give46(int code, int rounds);              // (v150-host beta 22, heavy: probe 46's give, below)
// the trigger patterns, each a list of (frames, Square pressure): which ones fire, how many bullets
struct TrigStep { int frames; int pressure; };
struct TrigPattern { const char* name; std::vector<TrigStep> steps; };
const std::vector<TrigPattern>& Patterns() {
    // MGO1_DOLL_TRIG=long: taps (light 20, full 8, let go 20) for 30 s, the bullets of each second logged
    // (the magazine emptied: a reload by the game itself when the trigger is pulled?)
    // (v150-host beta 18) MGO1_DOLL_TRIG=fast: Square light 4 frames, full 4, let go 4, for 40 s (faster than any gun takes it): each
    // bullet's frame and the reloads logged, the gun's own fastest rate and reload
    // (v150-host beta 18) MGO1_DOLL_TRIG=hold: Square light 6, full held 240 frames, let go 12, six times (the automatics'
    // own rate, their magazines, the reload at the next pull)
    if (getenv("MGO1_DOLL_TRIG") && !strcmp(getenv("MGO1_DOLL_TRIG"), "hold")) {
        static std::vector<TrigPattern> hp;
        if (hp.empty()) {
            TrigPattern t; t.name = "held: light 6, full 240, let go 12, six times";
            for (int k = 0; k < 6; ++k) { t.steps.push_back({6, 0x3F}); t.steps.push_back({240, 0xFF}); t.steps.push_back({12, 0}); }
            hp.push_back(t);
        }
        return hp;
    }
    // MGO1_DOLL_TRIG=swap: fast taps 3 s, R2 tapped (4 frames), fast taps 5 s, five times: from the tap to the other
    // weapon in the hands and to its first bullet (the quick swap's own time)
    if (getenv("MGO1_DOLL_TRIG") && !strcmp(getenv("MGO1_DOLL_TRIG"), "swap")) {
        static std::vector<TrigPattern> sp;
        if (sp.empty()) {
            TrigPattern t; t.name = "swap: taps 3 s, R2 tapped, taps 5 s, five times";
            for (int k = 0; k < 5; ++k) {
                for (int j = 0; j < 15; ++j) { t.steps.push_back({4, 0x3F}); t.steps.push_back({4, 0xFF}); t.steps.push_back({4, 0}); }
                t.steps.push_back({4, -2});
                for (int j = 0; j < 25; ++j) { t.steps.push_back({4, 0x3F}); t.steps.push_back({4, 0xFF}); t.steps.push_back({4, 0}); }
            }
            sp.push_back(t);
        }
        return sp;
    }
    // MGO1_DOLL_TRIG=fast<n>: first R2 tapped n times (4 frames, 60 apart: the weapon list's previous one each time)
    if (getenv("MGO1_DOLL_TRIG") && !strncmp(getenv("MGO1_DOLL_TRIG"), "fast", 4)) {
        static std::vector<TrigPattern> fp;
        if (fp.empty()) {
            TrigPattern t; t.name = "taps as fast as light 4, full 4, let go 4 frames for 40 s";
            for (int k = atoi(getenv("MGO1_DOLL_TRIG") + 4); k > 0; --k) { t.steps.push_back({4, -2}); t.steps.push_back({60, 0}); }
            if (getenv("MGO1_DOLL_TRIG_SCOPE")) { t.steps.push_back({4, -2}); t.steps.push_back({10, 0}); t.steps.push_back({4, -2}); t.steps.push_back({60, 0}); }   // (R2 twice: a sniper's scope view, state 0x83)
            // (v150-host beta 19) MGO1_DOLL_TRIG_WALK=<n>: the left stick pushed full forward n frames first (a scoped rifle in the
            // hands: does the player move?), then 60 frames still; MGO1_DOLL_TRIG_WALKFIRE=1: the stick pushed with the taps too
            // (v150-host beta 19) MGO1_DOLL_TRIG_SWAPTEST=1: 120 frames still (the scope on), R2 tapped (out of the rifle: the scope's
            // closing), 40 frames, R2 tapped (back: the scope's opening), then the taps at once (a shot before the scope is up?)
            if (getenv("MGO1_DOLL_TRIG_SWAPTEST")) { t.steps.push_back({120, 0}); t.steps.push_back({4, -2}); t.steps.push_back({40, 0}); t.steps.push_back({4, -2}); }
            if (getenv("MGO1_DOLL_TRIG_WALK")) { t.steps.push_back({std::max(1, atoi(getenv("MGO1_DOLL_TRIG_WALK"))), -3}); t.steps.push_back({60, 0}); }
            const int off = getenv("MGO1_DOLL_TRIG_LETGO") ? std::max(1, atoi(getenv("MGO1_DOLL_TRIG_LETGO"))) : 4;   // (the snipers' rifles: 20)
            for (int k = 0; k < 2400 / (8 + off); ++k) { t.steps.push_back({4, 0x3F}); t.steps.push_back({4, 0xFF}); t.steps.push_back({off, 0}); }
            fp.push_back(t);
        }
        return fp;
    }
    if (getenv("MGO1_DOLL_TRIG") && !strcmp(getenv("MGO1_DOLL_TRIG"), "long")) {
        static std::vector<TrigPattern> lp;
        if (lp.empty()) {
            TrigPattern t; t.name = "taps for 30 s";
            for (int k = 0; k < 38; ++k) { t.steps.push_back({20, 0x3F}); t.steps.push_back({8, 0xFF}); t.steps.push_back({20, 0}); }
            lp.push_back(t);
        }
        return lp;
    }
    static const std::vector<TrigPattern> p = {
        {"light 30, full held 40", {{30, 0x3F}, {40, 0xFF}}},
        {"let go after it", {{30, 0}}},
        {"light, full 8, let go 20, x3", {{20, 0x3F}, {8, 0xFF}, {20, 0}, {20, 0x3F}, {8, 0xFF}, {20, 0}, {20, 0x3F}, {8, 0xFF}, {20, 0}}},
        {"light, full 8, light 20, x3", {{20, 0x3F}, {8, 0xFF}, {20, 0x3F}, {8, 0xFF}, {20, 0x3F}, {8, 0xFF}, {20, 0x3F}}},
        {"full from nothing 8, let go 30, x3", {{8, 0xFF}, {30, 0}, {8, 0xFF}, {30, 0}, {8, 0xFF}, {30, 0}}},
        {"light 30, full held 120", {{30, 0x3F}, {120, 0xFF}, {30, 0}}},
        {"R2 twice (a reload?), then light, full 8, let go, x3", {{4, -2}, {10, 0}, {4, -2}, {40, 0}, {20, 0x3F}, {8, 0xFF}, {20, 0}, {20, 0x3F}, {8, 0xFF}, {20, 0}, {20, 0x3F}, {8, 0xFF}, {20, 0}}},
    };
    return p;
}
void Probe10Frame() {
    // (v150-host beta 18) MGO1_DOLL_TRIG_WEAPON=<code, hex>: that weapon taken first with the weapon window, as the brain
    // takes one (15 s at most), then the patterns
    if (gP10.phase == 0 && getenv("MGO1_DOLL_TRIG_WEAPON")) {
        static long long from = -1;
        const int want = (int)strtol(getenv("MGO1_DOLL_TRIG_WEAPON"), nullptr, 16);
        if (from < 0) { from = gFrame; Log("probe10: taking the weapon %x (in the hands %x)", want, Inv().weapon); }
        // (v150-host beta 22, heavy) a weapon not had (the RPG-7, the flamethrower: a weapon box's) given as a box gives it
        static long long gaveAt = -1000;
        if (Inv().ok && Inv().rounds[want] < 0 && gFrame - gaveAt > 120 && Give46(want, getenv("MGO1_DOLL_PROBE_ROUNDS") ? atoi(getenv("MGO1_DOLL_PROBE_ROUNDS")) : 30)) gaveAt = gFrame;
        if (Inv().weapon != want && gFrame - from < 900) { ProbeWindowTo(0, want); BulletsBorn(); return; }
        Log("probe10: the weapon in the hands %x (%s, %lld frames)", Inv().weapon, Inv().weapon == want ? "taken" : "NOT TAKEN", gFrame - from);
    }
    const int born = BulletsBorn();
    const auto& P = Patterns();
    if (gP10.phase == 0) { gP10.phase = 1; gP10.step = 0; gP10.at = gFrame; gP10.got = 0; Log("probe10: the trigger patterns (%zu)", P.size()); }
    if (gP10.step >= (int)P.size()) { if (gFrame % 600 == 0) Log("probe10: done, %d bullets in all", gP10.total); return; }
    const TrigPattern& tp = P[gP10.step];
    long long t = gFrame - gP10.at, acc = 0;
    int pressure = -1; bool over = true;
    for (const TrigStep& st : tp.steps) { if (t < acc + st.frames) { pressure = st.pressure; over = false; break; } acc += st.frames; }
    if (born) { gP10.got += born; gP10.total += born; }
    {                                                                    // (beta 18) each bullet's frame, each reload's span
        static long long lastBorn = -1, reloadFrom = -1;
        if (born) { Log("probe10: bullet at frame %lld (+%lld), the weapon in the hands %x, its rounds %d", gFrame, lastBorn >= 0 ? gFrame - lastBorn : -1, Inv().weapon, Inv().ok && Inv().weapon > 0 && Inv().weapon < 0x40 ? Inv().rounds[Inv().weapon] : -1); lastBorn = gFrame; }
        static int roundsWas = -1, roundsHand = -2;   // (each round spent: a shot, whatever its actor (a scoped rifle's))
        const int hnow = Inv().weapon, rnow = Inv().ok && hnow > 0 && hnow < 0x40 ? Inv().rounds[hnow] : -1;
        if (hnow == roundsHand && rnow >= 0 && roundsWas >= 0 && rnow < roundsWas) Log("probe10: a round spent at frame %lld, the weapon %x, rounds %d", gFrame, hnow, rnow);
        roundsHand = hnow; roundsWas = rnow;
        static int handWas = -2;
        if (Inv().weapon != handWas) { Log("probe10: in the hands %x at frame %lld (was %x)", Inv().weapon, gFrame, handWas); handWas = Inv().weapon; }
        const bool rl = (gMe.bits[1] & 0x80) != 0;
        if (rl && reloadFrom < 0) reloadFrom = gFrame;
        if (!rl && reloadFrom >= 0) { Log("probe10: a reload from frame %lld to %lld (%lld frames)", reloadFrom, gFrame, gFrame - reloadFrom); reloadFrom = -1; }
    }
    static int windowBullets = 0; windowBullets += born;
    if (t > 0 && t % 60 == 0 && tp.steps.size() > 20 && getenv("MGO1_DOLL_TRIG_SCOPE")) Log("probe10: state %x %x %x", gMe.bits[0], gMe.bits[1], gMe.bits[2]);
    if (t > 0 && t % 60 == 0 && tp.steps.size() > 20) { Log("probe10: second %lld: %d bullets (state %x %x, in the hands %x)", t / 60, windowBullets, gMe.bits[0], gMe.bits[1], Inv().weapon); windowBullets = 0; }
    if (!over) {
        if (t == 12 || t == acc + 12) Log("probe10: %s, frame %lld: Square %d, state %x %x", tp.name, t, pressure, gMe.bits[0], gMe.bits[1]);
        if (getenv("MGO1_DOLL_TRIG_WALKFIRE") && pressure != -2) gPad.sticks[3] = 0x00;   // (beta 19: walking while it taps)
        if (pressure == -3) { gPad.sticks[3] = 0x00; if (t == acc) Log("probe10: the stick pushed forward at frame %lld (in the hands %x, state %x %x)", gFrame, Inv().weapon, gMe.bits[0], gMe.bits[1]); }
        else if (pressure == -2) { gPad.buttons |= 0x0200; if (t == acc) Log("probe10: R2 tapped at frame %lld (in the hands %x)", gFrame, Inv().weapon); }   // R2
        else if (pressure > 0) { gPad.buttons |= 0x8000; gPad.pressures[11] = (uint8_t)pressure; }
        return;
    }
    if (t < acc + 30) return;                                            // the last bullets of it
    Log("probe10: %s: %d bullets (state %x %x)", tp.name, gP10.got, gMe.bits[0], gMe.bits[1]);
    ++gP10.step; gP10.at = gFrame; gP10.got = 0;
}

// probe 11: the stage's cardboard box (NewOnlineStageCBox, its place at +0x2D0). To it, Triangle next
// to it (the actor goes: taken), then the item window (L2 held: NewItemChangeLinear, the weapon window's
// layout: +0x44 12 open, +0x48 2 still, +0x50 the item held, +0x54 the one under the cursor): its list
// (right until the first code comes back), then each item taken in turn (L2 let go on it): the player's
// state, a walk with it, logged; the item "none" (0) at the end
struct Probe11 { int phase = 0, step = 0, sel = 0; long long at = 0, moveAt = -1000; float box[3] = {0, 0, 0}; std::vector<int> codes; int want = -1; };
Probe11 gP11;
void FindCBox(uint32_t w, const std::string& name, void* ctx) { if (name == "NewOnlineStageCBox" && !*(uint32_t*)ctx) *(uint32_t*)ctx = w; }
void Probe11Say(const char* what) {
    char b[400]; int k = snprintf(b, sizeof b, "probe11 %s: me %.0f %.0f %.0f (hips %.0f) state", what, gMe.pos[0], gMe.pos[1], gMe.pos[2], HipHeight());
    for (int i = 0; i < 11; ++i) k += snprintf(b + k, sizeof b - k, " %x", gMe.bits[i]);
    Log("%s", b);
}
void Probe11Frame() {
    Probe11& P = gP11;
    const long long t = gFrame - P.at;
    auto go = [&](int ph) { P.phase = ph; P.at = gFrame; P.step = 0; };
    const uint32_t il = ps2world::ActorByName("NewItemChangeLinear");
    const int open = il ? R16(il + 0x44) : -1, still = il ? R16(il + 0x48) : -1, held = il ? R16(il + 0x50) : -1, cur = il ? R16(il + 0x54) : -1;
    switch (P.phase) {
    case 0: {                                            // the box: to it
        bool other = false;
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2) return;
        uint32_t w = 0; ps2world::ForEachActor(&FindCBox, &w);
        if (!w) { if (gFrame % 600 == 0) Log("probe11: no cardboard box on this map"); return; }
        for (int i = 0; i < 3; ++i) P.box[i] = RF(w + 0x2D0 + 4 * i);
        const float to[3] = {P.box[0], P.box[1], P.box[2]};
        Log("probe11: a box at %.0f %.0f %.0f, %.0f m away", to[0], to[1], to[2], Dist2D(gMe.pos, to) / 1000.f);
        if (!PlanTo(to)) Log("probe11: no way to the box");
        go(1);
        break;
    }
    case 1: {                                            // walked to it, then Triangle facing it until the actor goes
        uint32_t w = 0; ps2world::ForEachActor(&FindCBox, &w);
        bool here = false;
        if (w) { float b[3]; for (int i = 0; i < 3; ++i) b[i] = RF(w + 0x2D0 + 4 * i); here = Dist2D(b, P.box) < 300.f; }
        if (!here) { Probe11Say("the box taken"); go(2); break; }
        const float d = Dist2D(gMe.pos, P.box);
        if (d > 900.f) { if (!Following()) PlanTo(P.box); FollowPath(); if (t > 3000) { Log("probe11: the box not reached"); go(9); } break; }
        StopPath();
        const float a = AngleTo(gMe.pos, P.box);
        if (std::fabs(Wrap(a - Facing())) > 15.f * 3.14159265f / 180.f) { if (gFrame % 6 < 2) Steer(std::sin(a), std::cos(a), 1.f); break; }
        if (gFrame % 40 < 4) gPad.buttons |= 0x1000;
        if (t > 1200) { Probe11Say("not taken"); go(9); }
        break;
    }
    case 2: {                                            // the item window's list
        gPad.buttons |= 0x0100;                          // L2 held
        if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; break; }
        if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) { if (t > 300) { Log("probe11: the item window did not open (%d %d)", open, still); go(9); } break; }
        if (!P.codes.empty() && cur == P.codes[0]) {
            std::string l; for (int c : P.codes) { char b[16]; snprintf(b, sizeof b, " %x", c); l += b; }
            Log("probe11: the items:%s (held %x)", l.c_str(), held);
            gPad.buttons &= ~0x0100u; go(3); P.sel = 0; break;
        }
        P.codes.push_back(cur);
        if (P.codes.size() > 16) { Log("probe11: the list does not come round"); gPad.buttons &= ~0x0100u; go(3); break; }
        P.moveAt = gFrame; gPad.buttons |= 0x0020;
        break;
    }
    case 3: {                                            // each item in turn: taken, the state, a walk
        if (P.sel >= (int)P.codes.size() + 1) { go(9); break; }
        const int want = P.sel < (int)P.codes.size() ? P.codes[P.sel] : 0;
        if (P.step == 0) {                               // L2 held, to it, let go
            gPad.buttons |= 0x0100;
            if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; break; }
            if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) { if (t > 400) { Log("probe11: %x not reached", want); ++P.sel; P.at = gFrame; } break; }
            if (cur == want) { gPad.buttons &= ~0x0100u; P.step = 1; P.at = gFrame; Log("probe11: item %x taken (held %x before)", want, held); break; }
            P.moveAt = gFrame; gPad.buttons |= 0x0020;
            break;
        }
        if (t == 60) Probe11Say("still with it");
        if (t > 60 && t < 150) Steer(1, 0, 1.f);         // a walk with it (+x)
        if (t == 150) Probe11Say("after a walk");
        if (t > 150 && t < 180) gPad.buttons |= 0x4000 * (t < 154);   // Cross
        if (t == 200) Probe11Say("after Cross");
        if (t > 220) { ++P.sel; P.step = 0; P.at = gFrame; }
        break;
    }
    default:
        if (gFrame % 600 == 0) Probe11Say("done");
        break;
    }
}

// probe 12: the ladders (the traps on the volumes "ladder<NNN>_0" / "_1", doll_geom.h). To the nearest
// ladder's foot, then the ways to take it, each logged: walked into it (60 frames); still, facing it,
// Triangle; walked into it with Triangle every 15 frames. Taken (the hips rise, the state changes), the
// stick up (the hips and the state every 15 frames) until the top; there, to the top's volume, facing
// back: the same tries, the stick down. Then the next ladder
struct Probe12 {
    int phase = 0, step = 0, ladder = -1, tries = 0;
    long long at = 0, lastRise = 0;
    float y0 = 0, best = 0, floorFoot = 0, floorTop = 0;
    std::vector<Ladder> ladders;
    std::vector<int> done;
    bool down = false;                               // the way down (from the top)
    int lg = 0; long long lgAt = -1; float lgY = 0.f; bool lgDone = false;   // (v137) the let-go test of this climb
};
Probe12 gP12;
// (v137) MGO1_DOLL_PROBE_LETGO (1: all, or names "cross,triangle,..."): one input a climb, pressed at 45 % of it, then 60
// frames with nothing: does the player let go of the ladder (the state word 2's 0x40000, the hips)? One RESULT line each
struct LetGo { uint32_t buttons; int stick; int frames; const char* name; };
const LetGo kLetGo[] = {{0x4000, 0, 4, "cross"}, {0x1000, 0, 4, "triangle"}, {0x2000, 0, 4, "circle"}, {0x8000, 0, 20, "square"},
                        {0x0800, 0, 20, "r1"}, {0x0400, 0, 20, "l1"}, {0, 1, 30, "stickleft"}, {0, 2, 30, "stickright"}};
const LetGo* LetGoNext() {
    const char* e = getenv("MGO1_DOLL_PROBE_LETGO");
    if (!e || !*e) return nullptr;
    const int n = (int)(sizeof kLetGo / sizeof kLetGo[0]);
    for (; gP12.lg < n; ++gP12.lg)
        if (!strcmp(e, "1") || strstr(e, kLetGo[gP12.lg].name)) return &kLetGo[gP12.lg];
    return nullptr;
}
// true while the test has the pad (on the ladder, at 45 % of the climb)
void Probe12Say(const char* what);
bool LetGoFrame(const Ladder& l, bool on) {
    Probe12& P = gP12;
    if (P.lgDone) return false;
    const LetGo* g = LetGoNext();
    if (!g) return false;
    if (P.lgAt < 0) {
        const float from = P.down ? P.floorTop : P.floorFoot, to = P.down ? P.floorFoot : P.floorTop;
        if (!on || std::fabs(to - from) < 1000.f || (gMe.pos[1] - 800.f - from) / (to - from) < 0.45f) return false;
        P.lgAt = gFrame; P.lgY = gMe.pos[1];
        Probe12Say((std::string("LETGO ") + g->name + " pressed").c_str());
    }
    const long long t = gFrame - P.lgAt;
    if (t < g->frames) {
        gPad.buttons |= g->buttons;
        if (g->stick) gPad.sticks[2] = g->stick == 1 ? 0x00 : 0xFF;
    }
    if (t % 10 == 0) Probe12Say(g->name);
    if (t < g->frames + 60) return true;
    Log("probe12: RESULT LETGO %s on ladder %03d %s: %s (hips %+.0f in %d frames, state %x %x)", g->name, l.number, P.down ? "down" : "up", on ? "still on it" : "OFF IT",
        gMe.pos[1] - P.lgY, g->frames + 60, gMe.bits[0], gMe.bits[2]);
    P.lgDone = true; ++P.lg; P.lgAt = -1;
    if (!on) P.lastRise = gFrame - 21;               // (off it: the climb's own end, judged at once)
    return false;
}
void Probe12Say(const char* what) {
    char b[400]; int k = snprintf(b, sizeof b, "probe12 %s: pos %.0f %.0f %.0f (%+.0f) facing %.0f state", what, gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.pos[1] - gP12.y0,
                                  Facing() * 57.2958f);
    for (int i = 0; i < 11; ++i) k += snprintf(b + k, sizeof b - k, " %x", gMe.bits[i]);
    Log("%s", b);
}
void Probe12Frame() {
    Probe12& P = gP12;
    const long long t = gFrame - P.at;
    auto go = [&](int phase) { P.phase = phase; P.at = gFrame; P.step = 0; P.y0 = P.best = gMe.pos[1]; };
    if (P.phase == 0) {
        bool other = false;                              // the round runs (a second player in the world: before, the round starts again)
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2) return;
        static bool listed = false;
        if (!listed) P.ladders = FindLadders(*gMap->geom);
        for (const Ladder& l : P.ladders) if (!listed)
            Log("probe12: ladder %03d foot %.0f %.0f %.0f (half %.0f %.0f %.0f) top %.0f %.0f %.0f (half %.0f %.0f %.0f) up facing %.0f", l.number, l.foot[0], l.foot[1], l.foot[2],
                l.footHalf[0], l.footHalf[1], l.footHalf[2], l.top[0], l.top[1], l.top[2], l.topHalf[0], l.topHalf[1], l.topHalf[2], l.dir * 57.2958f);
        listed = true;
        if (P.ladders.empty()) { if (gFrame % 600 == 0) Log("probe12: no ladder on this map"); return; }
        // the nearest one not done yet whose foot can be walked to
        int best = -1; float bd = 1e30f;
        for (int i = 0; i < (int)P.ladders.size(); ++i) {
            if (std::find(P.done.begin(), P.done.end(), i) != P.done.end()) continue;
            const float d = Dist2D(gMe.pos, P.ladders[i].foot);
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0) { if (gFrame % 600 == 0) Log("probe12: done"); return; }
        P.ladder = best; P.done.push_back(best); P.down = false;
        const Ladder& l = P.ladders[best];
        const float fa[3] = {l.foot[0], l.foot[1] + l.footHalf[1], l.foot[2]}, ta[3] = {l.top[0], l.top[1] + l.topHalf[1], l.top[2]};
        P.floorFoot = gMap->nav->GroundBelow(fa, 2 * l.footHalf[1] + 1500.f);
        P.floorTop = gMap->nav->GroundBelow(ta, 2 * l.topHalf[1] + 1500.f);
        const float foot[3] = {l.foot[0], P.floorFoot, l.foot[2]};
        Log("probe12: to ladder %03d (floors %.0f and %.0f), %.0f m away", l.number, P.floorFoot, P.floorTop, bd / 1000.f);
        if (!PlanTo(foot)) Log("probe12: no way to its foot");
        go(1);
        return;
    }
    const Ladder& l = P.ladders[P.ladder];
    const float* box = P.down ? l.top : l.foot;
    const float face = P.down ? Wrap(l.dir + 3.14159265f) : l.dir;
    switch (P.phase) {
    case 1:                                              // to the box (then the try P.tries)
        if (Dist2D(gMe.pos, box) < 450.f && std::fabs(gMe.pos[1] - 800.f - (P.down ? P.floorTop : P.floorFoot)) < 600.f) {
            StopPath(); const int s = P.tries; go(2); P.step = s; P.tries = 0; Probe12Say("at the box"); break;
        }
        if (t > 3600) { Log("probe12: could not reach the box"); go(0); break; }
        if (!Following()) {
            if (Dist2D(gMe.pos, box) < 2500.f) { Steer(box[0] - gMe.pos[0], box[2] - gMe.pos[2], 1.f); break; }
            const float to[3] = {box[0], P.down ? P.floorTop : P.floorFoot, box[2]};
            if (t % 120 == 0 && !PlanTo(to)) Log("probe12: no way to the box (%.0f m)", Dist2D(gMe.pos, box) / 1000.f);
        }
        FollowPath();
        break;
    case 2: {                                            // the tries
        static const char* kTry[3] = {"facing it, Triangle", "walked into it", "walked into it with Triangle"};
        if (t == 1) Log("probe12: %s (%s)", kTry[P.step], P.down ? "down" : "up");
        const float err = Wrap(face - Facing());
        if (P.step == 1 || P.step == 2) Steer(std::sin(face), std::cos(face), 1.f);
        if (P.step == 0) {
            if (t < 60 && std::fabs(err) > 15.f * 3.14159265f / 180.f) { if (gFrame % 6 < 2) Steer(std::sin(face), std::cos(face), 1.f); }
            else if (t >= 60 && t < 64) gPad.buttons |= 0x1000;
        }
        if (P.step == 2 && t % 15 < 4) gPad.buttons |= 0x1000;
        if (t % 15 == 0) Probe12Say(kTry[P.step]);
        const bool moved = std::fabs(gMe.pos[1] - P.y0) > 250.f || (gMe.bits[2] & 0x40000) != 0;
        if (moved || (gMe.bits[2] & 0x40000) || t >= (P.step == 0 ? 130 : 90)) {
            Probe12Say(moved ? "TAKEN" : "nothing");
            if (moved) { go(3); P.lastRise = gFrame; P.best = gMe.pos[1]; P.lgDone = false; P.lgAt = -1; break; }
            if (P.step + 1 >= 3) { Log("probe12: RESULT ladder %03d %s: NOT TAKEN", l.number, P.down ? "down" : "up"); go(0); break; }
            P.tries = P.step + 1;                        // back into the box's middle before the next try
            go(1);
        }
        break;
    }
    case 3: {                                            // on it (state word 2: 0x40000): the stick up (down), until off it
        const bool on = (gMe.bits[2] & 0x40000) != 0;
        if (LetGoFrame(l, on)) { if (on) P.lastRise = gFrame; break; }   // (v137: the let-go test)
        if (on || t < 20) gPad.sticks[3] = P.down ? 0xFF : 0x00;    // off it: the stick let go
        if (t % 15 == 0) Probe12Say(P.down ? "stick down" : "stick up");
        if (on) P.lastRise = gFrame;
        if (!on && gFrame - P.lastRise > 20) {
            const float moved = gMe.pos[1] - P.y0;
            Probe12Say("OFF");
            const bool ok = P.down ? moved < -1500.f : moved > 1500.f;
            Log("probe12: RESULT ladder %03d %s: %s in %.1f s (hips %+.0f)", l.number, P.down ? "down" : "up", ok ? "MADE IT" : "OFF WITHOUT IT", t / 60.f, moved);
            if (!P.down && ok) { P.down = true; go(1); }
            else go(0);
            break;
        }
        if (t > 900) { Probe12Say("STUCK"); Log("probe12: RESULT ladder %03d %s: STUCK", l.number, P.down ? "down" : "up"); go(0); }
        break;
    }
    }
}

// probe 13: the weapon and item windows (R2, L2). In the world, standing: a list of steps (buttons held a
// number of frames), and at the end of each the works of the actors whose names hold Weapon, Item, Equip,
// Linear, Hud or Player (the local player's too) compared with the step before: the u16 fields that
// changed are logged (a window's state, its cursor, the weapon in the hands), but those that change by
// themselves (two looks 10 frames apart with nothing pressed)
struct Snap13 { std::map<uint32_t, std::pair<std::string, std::vector<uint8_t>>> w; };
void Snap13Actor(uint32_t w, const std::string& name, void* ctx) {
    const bool want = name.find("Weapon") != std::string::npos || name.find("Item") != std::string::npos || name.find("Equip") != std::string::npos ||
                      name.find("Linear") != std::string::npos || name.rfind("NewHud", 0) == 0 || name.find("Player") != std::string::npos;
    if (!want) return;
    auto& e = ((Snap13*)ctx)->w[w];
    e.first = name;
    const uint32_t a = w & 0x01FFFFFF, size = name == "NewNetworkDummyPlayer" ? 0x1400 : 0x800;
    if (a + size <= 0x02000000) e.second.assign(ps2::gMem + a, ps2::gMem + a + size);
}
struct Probe13Step { uint32_t buttons; int frames; const char* name; };
struct Probe13 { int step = -2; long long at = 0; Snap13 last, noiseA; std::map<uint32_t, std::vector<bool>> noisy; };
Probe13 gP13;
void Probe13Frame() {
    static const Probe13Step kSteps[] = {
        {0, 40, "nothing"},
        {0x0200, 30, "R2 held"}, {0x0200 | 0x0020, 4, "R2 + right"}, {0x0200, 26, "R2 held"}, {0, 60, "R2 let go"},
        {0x0200, 30, "R2 held"}, {0x0200 | 0x0080, 4, "R2 + left"}, {0x0200, 26, "R2 held"}, {0, 60, "R2 let go"},
        {0x0200, 4, "R2 tap"}, {0, 60, "after the tap"}, {0x0200, 4, "R2 tap"}, {0, 60, "after the tap"}, {0x0200, 4, "R2 tap"}, {0, 60, "after the tap"},
        {0x0200, 30, "R2 held"}, {0x0200 | 0x0020, 4, "R2 + right"}, {0x0200, 12, "R2 held"}, {0x0200 | 0x0020, 4, "R2 + right"}, {0x0200, 26, "R2 held"}, {0, 60, "R2 let go"},
        {0x0100, 30, "L2 held"}, {0x0100 | 0x0020, 4, "L2 + right"}, {0x0100, 26, "L2 held"}, {0, 60, "L2 let go"},
        {0x0100, 4, "L2 tap"}, {0, 60, "after the tap"},
    };
    const int n = (int)(sizeof kSteps / sizeof kSteps[0]);
    Probe13& P = gP13;
    const long long t = gFrame - P.at;
    auto snap = [&](Snap13& s) { s = Snap13(); ps2world::ForEachActor(&Snap13Actor, &s); };
    if (P.step == -2) {
        bool other = false;                              // the round runs (a second player in the world)
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2 || gFrame % 60) return;
        snap(P.noiseA); P.step = -1; P.at = gFrame; return;
    }
    if (P.step == -1) {                                  // the fields that change by themselves
        if (t < 10) return;
        Snap13 b; snap(b);
        for (auto& a : b.w) {
            auto it = P.noiseA.w.find(a.first);
            std::vector<bool> nz(a.second.second.size() / 2, false);
            if (it != P.noiseA.w.end() && it->second.second.size() == a.second.second.size())
                for (size_t o = 0; o + 2 <= a.second.second.size(); o += 2) nz[o / 2] = memcmp(&it->second.second[o], &a.second.second[o], 2) != 0;
            P.noisy[a.first] = nz;
        }
        P.last = b; P.step = 0; P.at = gFrame;
        Log("probe13: %zu actors watched", b.w.size());
        return;
    }
    if (P.step >= n) { if (gFrame % 600 == 0) Log("probe13: done"); return; }
    const Probe13Step& st = kSteps[P.step];
    if (t < st.frames) { gPad.buttons |= st.buttons; return; }
    Snap13 now; snap(now);
    Log("probe13: after %s (state %x %x %x, pos %.0f %.0f %.0f)", st.name, gMe.bits[0], gMe.bits[1], gMe.bits[6], gMe.pos[0], gMe.pos[1], gMe.pos[2]);
    {
        std::string mine, mans, win;
        for (auto& a : now.w) {
            const std::string& nm = a.second.first;
            const auto& b = a.second.second;
            bool holds = false;
            size_t at = 0;
            for (size_t o = 0; o + 4 <= std::min<size_t>(b.size(), 0x800); o += 4) { uint32_t v; memcpy(&v, &b[o], 4); if (v && (v & 0x01FFFFFF) == (gMe.obj & 0x01FFFFFF)) { holds = true; at = o; break; } }
            if (nm.rfind("NewWeapon", 0) == 0 && nm != "NewWeaponChangeLinear" && nm != "NewWeaponMagazine" && holds) { char t[64]; snprintf(t, sizeof t, " %s@%x(+%zx)", nm.c_str(), a.first & 0xFFFF, at); mine += t; }
            if (nm == "GM_NewWeaponManager") {
                uint32_t cur; memcpy(&cur, &b[0], 4);
                std::string cn = "?";
                auto it = now.w.find(cur);
                for (auto& c : now.w) if ((c.first & 0x01FFFFFF) == (cur & 0x01FFFFFF)) cn = c.second.first;
                char t[96]; snprintf(t, sizeof t, " [%x%s: %s@%x]", a.first & 0xFFFF, holds ? " mine" : "", cn.c_str(), cur & 0xFFFF); mans += t;
                (void)it;
            }
            if (nm == "NewWeaponChangeLinear" || nm == "NewItemChangeLinear") {
                const size_t base = nm == "NewWeaponChangeLinear" ? 0x40 : 0x280;
                char t[200]; int k = snprintf(t, sizeof t, " %s:", nm == "NewWeaponChangeLinear" ? "weapons" : "items");
                for (size_t o = base; o < base + 0x28 && o + 2 <= b.size(); o += 4) { uint32_t v; memcpy(&v, &b[o], 4); k += snprintf(t + k, sizeof t - k, " %x", v); }
                win += t;
            }
        }
        Log("probe13: mine:%s | managers:%s |%s", mine.c_str(), mans.c_str(), win.c_str());
    }
    for (auto& a : now.w) {
        auto b = P.last.w.find(a.first);
        if (b == P.last.w.end()) { Log("probe13   new %s %08x", a.second.first.c_str(), a.first); continue; }
        if (b->second.first != a.second.first || b->second.second.size() != a.second.second.size()) continue;
        const auto& nz = P.noisy[a.first];
        std::string s; int m = 0;
        for (size_t o = 0; o + 2 <= a.second.second.size(); o += 2) {
            if (o / 2 < nz.size() && nz[o / 2]) continue;
            uint16_t x, y; memcpy(&x, &b->second.second[o], 2); memcpy(&y, &a.second.second[o], 2);
            if (x != y) { char tb[40]; snprintf(tb, sizeof tb, " +%zx:%x->%x", o, x, y); s += tb; ++m; }
        }
        if (m > 0 && m <= 40) Log("probe13   %s %08x%s", a.second.first.c_str(), a.first, s.c_str());
        else if (m > 40) Log("probe13   %s %08x: %d fields", a.second.first.c_str(), a.first, m);
    }
    for (auto& b : P.last.w) if (!now.w.count(b.first)) Log("probe13   gone %s %08x", b.second.first.c_str(), b.first);
    P.last = now; ++P.step; P.at = gFrame;
}

// probe 14: the ladders as the doll takes them (FollowPath): for each ladder of the map, a path to its top
// (from the ground: through the ladder), then one back to its foot; the times and the results logged
struct Probe14 { int ladder = -1, leg = 0, results = 0; long long at = 0; bool going = false; };
Probe14 gP14;
void Probe14Frame() {
    Probe14& P = gP14;
    bool other = false;                                  // the round runs (a second player in the world)
    for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
    if (!other || gRoom.phase != 2) return;
    const auto& L = gMap->nav->Ladders();
    if (L.empty()) { if (gFrame % 600 == 0) Log("probe14: no ladder on this map"); return; }
    if (P.ladder < 0) { P.ladder = 0; P.leg = 0; P.going = false; }
    if (P.ladder >= (int)L.size()) { if (gFrame % 600 == 0) Log("probe14: done, %d results", P.results); return; }
    const Ladder& l = L[P.ladder];
    const float* to = P.leg == 0 ? l.top : l.foot;
    if (!P.going) {
        if (!PlanTo(to)) {
            if (gFrame % 120 == 0) Log("probe14: ladder %03d: no way to its %s from %.0f %.0f %.0f", l.number, P.leg == 0 ? "top" : "foot", gMe.pos[0], gMe.pos[1], gMe.pos[2]);
            return;
        }
        P.going = true; P.at = gFrame;
        Log("probe14: ladder %03d: to its %s (%.0f %.0f %.0f) from %.0f %.0f %.0f", l.number, P.leg == 0 ? "top" : "foot", to[0], to[1], to[2], gMe.pos[0], gMe.pos[1], gMe.pos[2]);
    }
    const bool there = Dist2D(gMe.pos, to) < 900.f && std::fabs(gMe.pos[1] - 800.f - to[1]) < 700.f && !OnLadder();
    if (there || !Following() || gFrame - P.at > 5400) {
        Log("probe14: RESULT ladder %03d %s: %s in %.1f s (at %.0f %.0f %.0f; stuck %d, replans %d, given up %d)", l.number, P.leg == 0 ? "to the top" : "to the foot",
            there ? "MADE IT" : gFrame - P.at > 5400 ? "TOO LONG" : "PATH ENDED ELSEWHERE", (gFrame - P.at) / 60.f, gMe.pos[0], gMe.pos[1], gMe.pos[2], gNavStats.stuck,
            gNavStats.replans, gNavStats.failed);
        ++P.results;
        StopPath(); Steer(0, 0, 0);
        P.going = false;
        if (there || gFrame - P.at > 5400) { if (++P.leg >= 2) { P.leg = 0; ++P.ladder; } }
        return;
    }
    FollowPath();
}

// probe 15: a sniper rifle taken with the weapon window turns its scope's view on (state 0x83): the
// rifle taken (the window, as the brain does), then the buttons tried in turn (each a tap, 40 frames to
// see) until the state is out of the view; then the weapon held, read from the window
struct Probe15 { int phase = 0, step = 0; long long at = 0, moveAt = -1000; int want = 0xF; };
Probe15 gP15;
void Probe15Frame() {
    static const struct { uint16_t bit; int frames; const char* name; } kTry[] = {
        {0x8000, 20, "Square light 20"}, {0x1000, 4, "Triangle tap"}, {0x2000, 4, "Circle tap"}, {0x0400, 4, "L1 tap"},
        {0x0200, 4, "R2 tap"}, {0x0200, 4, "R2 tap again"}, {0x0100, 4, "L2 tap"}, {0x0008, 4, "START"}};
    const int n = (int)(sizeof kTry / sizeof kTry[0]);
    Probe15& P = gP15;
    const long long t = gFrame - P.at;
    const uint32_t wl = ps2world::ActorByName("NewWeaponChangeLinear");
    const int open = wl ? R16(wl + 0x44) : -1, still = wl ? R16(wl + 0x48) : -1, held = wl ? R16(wl + 0x50) : -1, cur = wl ? R16(wl + 0x54) : -1;
    auto say = [&](const char* what) { Log("probe15 %s: state %x %x %x %x, pos %.0f %.0f %.0f, window %d %d held %x cursor %x", what, gMe.bits[0], gMe.bits[1], gMe.bits[5], gMe.bits[6],
                                           gMe.pos[0], gMe.pos[1], gMe.pos[2], open, still, held, cur); };
    switch (P.phase) {
    case 0: {                                            // the round runs
        bool other = false;
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (other && gRoom.phase == 2 && gFrame > 5000) { P.phase = 1; P.at = gFrame; say("start"); }
        break;
    }
    case 1:                                              // the window: to the rifle (R2 held; not open in 60 frames: let go, again)
        if (open != 0xC && t % 70 >= 60) break;
        gPad.buttons |= 0x0200;
        if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; break; }
        if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) { if (t > 700) { say("window lost"); P.phase = 9; } break; }
        if (cur == P.want) { gPad.buttons &= ~0x0200u; say("rifle taken"); P.phase = 2; P.at = gFrame; break; }
        P.moveAt = gFrame; gPad.buttons |= 0x0020;
        break;
    case 2:                                              // the state after it
        if (t == 30) say("30 frames after");
        if (t == 60) { say("60 frames after"); P.phase = 3; P.at = gFrame; P.step = 0; }
        break;
    case 3:                                              // the tries
        if (P.step >= n) { say("tries over"); P.phase = 9; break; }
        if (t < kTry[P.step].frames) { gPad.buttons |= kTry[P.step].bit; if (kTry[P.step].bit == 0x8000) gPad.pressures[11] = 0x3F; break; }
        if (t == kTry[P.step].frames + 40) {
            say(kTry[P.step].name);
            if (!(gMe.bits[0] & 0x80)) { Log("probe15: RESULT out of the view with %s", kTry[P.step].name); P.phase = 4; P.at = gFrame; break; }
            ++P.step; P.at = gFrame;
        }
        break;
    case 4:                                              // walk a little (the stick moves the player again?), then the weapon held
        if (t < 60) Steer(0, 1, 1.f);
        if (t == 60) say("after a walk");
        if (t > 60) {
            gPad.buttons |= 0x0200;
            if (open == 0xC && still == 2) { say("the window: the weapon held"); P.phase = 5; P.at = gFrame; }
            if (t > 300) { say("window not open"); P.phase = 9; }
        }
        break;
    case 5:                                              // let go on the one held (the cursor on it)
        if (t == 40) say("closed");
        break;
    default:
        if (gFrame % 600 == 0) say("done");
        break;
    }
}

// probe 16: the grenade (the support weapon, code 0x13): taken from the weapon window, turned toward the
// most open direction (the longest clear line 1.5 m above the ground), then thrown: third person (Square
// held 20), first person view (R1 held) at pitches 0, 10, 20, 30, 45, 60, -10. The thrown grenade is the
// actor NewBulletGrenadeWithBuller (it appears 53 to 56 frames after the release): its point (+0x50) and
// its velocity (+0x60, mm a frame) logged every 2 frames until it goes off
struct Probe16Throw { bool fpv; float pitchDeg; int hold; const char* name; };
struct Probe16 { int phase = 0, step = 0; long long at = 0, moveAt = -1000, relAt = -1, seenAt = -1; uint32_t bullet = 0; float yaw = 0, from[3], lastPos[3]; };
Probe16 gP16;
void P16Find(uint32_t w, const std::string& name, void* ctx) { if (name == "NewBulletGrenadeWithBuller") *(uint32_t*)ctx = w; }
void Probe16Frame() {
    static const Probe16Throw kThrows[] = {
        {true, -20, 20, "first person -20"}, {true, -30, 20, "first person -30"}, {true, 0, 60, "first person 0 held 60"},
        {true, -10, 20, "first person -10"}, {true, 50, 20, "first person 50"}};
    const int n = (int)(sizeof kThrows / sizeof kThrows[0]);
    Probe16& P = gP16;
    const long long t = gFrame - P.at;
    const float kD = 3.14159265f / 180.f;
    const uint32_t wl = ps2world::ActorByName("NewWeaponChangeLinear");
    const int open = wl ? R16(wl + 0x44) : -1, still = wl ? R16(wl + 0x48) : -1, held = wl ? R16(wl + 0x50) : -1, cur = wl ? R16(wl + 0x54) : -1;
    float m[16] = {}; ps2world::ViewMatrix(m);
    const float camYaw = std::atan2(m[8], m[10]), camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    switch (P.phase) {
    case 0: {                                            // the round runs: the most open direction
        bool other = false;
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2 || gFrame < 5000) break;
        float best = -1;
        for (int k = 0; k < 72; ++k) {
            const float a = k * 5.f * kD;
            const float e[3] = {gMe.pos[0], gMe.pos[1] + 700.f, gMe.pos[2]};
            float d = 0;
            for (float r = 1000.f; r <= 40000.f; r += 1000.f) {
                const float q[3] = {e[0] + r * std::sin(a), e[1], e[2] + r * std::cos(a)};
                if (!LineClear(e, q)) break;
                d = r;
            }
            if (d > best) { best = d; P.yaw = a; }
        }
        Log("probe16: at %.0f %.0f %.0f, the most open direction %.0f (%.0f m clear)", gMe.pos[0], gMe.pos[1], gMe.pos[2], P.yaw / kD, best / 1000.f);
        P.phase = 1; P.at = gFrame;
        break;
    }
    case 1:                                              // the window: to the grenade
        if (open != 0xC && t % 70 >= 60) break;
        gPad.buttons |= 0x0200;
        if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; break; }
        if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) { if (t > 700) { Log("probe16: no window"); P.phase = 9; } break; }
        if (cur == 0x13) { gPad.buttons &= ~0x0200u; P.phase = 2; P.at = gFrame; break; }
        if (t > 600) { Log("probe16: no grenade in the window"); gPad.buttons &= ~0x0200u; P.phase = 9; break; }
        P.moveAt = gFrame; gPad.buttons |= 0x0020;
        break;
    case 2: {                                            // a throw: turned, aimed (first person: the pitch), Square held, let go
        if (P.step >= n) { Log("probe16: done"); P.phase = 9; break; }
        const Probe16Throw& th = kThrows[P.step];
        if (t < 45) {                                    // the facing toward the open direction (short full pushes)
            if (std::fabs(Wrap(P.yaw - Facing())) > 3.f * kD && gFrame % 6 < 2) Steer(std::sin(P.yaw), std::cos(P.yaw), 1.f);
            break;
        }
        if (th.fpv) gPad.buttons |= 0x0800;
        if (t < 120) {
            if (th.fpv && t > 60) {
                const float ye = Wrap(P.yaw - camYaw), pe = th.pitchDeg * kD - camPitch;
                auto sv = [](float e) { const float a = std::fabs(e) / (3.14159265f / 180.f); const float v = a < 0.3f ? 0.f : std::min(1.f, 0.36f + a * 0.045f); return e < 0 ? -v : v; };
                gPad.sticks[2] = (uint8_t)(0x80 + (int)(-sv(ye) * 127));
                gPad.sticks[3] = (uint8_t)(0x80 - (int)(sv(pe) * 127));
            }
            break;
        }
        if (t == 120) { memcpy(P.from, gMe.pos, sizeof P.from);
                        Log("probe16: %s: view yaw %.1f pitch %.1f, facing %.1f, eye %.0f %.0f %.0f, me %.0f %.0f %.0f", th.name, camYaw / kD, camPitch / kD, Facing() / kD, m[12], m[13], m[14],
                            gMe.pos[0], gMe.pos[1], gMe.pos[2]); }
        if (t < 120 + th.hold) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; break; }
        if (t == 120 + th.hold) { P.relAt = gFrame; P.bullet = 0; P.seenAt = -1; }
        const long long r = gFrame - P.relAt;
        uint32_t b = 0; ps2world::ForEachActor(&P16Find, &b);
        if (b && !P.bullet && r > 20) { P.bullet = b; P.seenAt = r; }
        if (P.bullet && b == P.bullet && r % 2 == 0) {
            float q[3], v[3]; for (int i = 0; i < 3; ++i) { q[i] = RF(b + 0x50 + 4 * i); v[i] = RF(b + 0x60 + 4 * i); }
            Log("probe16 %s +%lld: %.0f %.0f %.0f v %.1f %.1f %.1f (%.0f m away, %+.0f)", th.name, r, q[0], q[1], q[2], v[0], v[1], v[2], Dist2D(q, P.from) / 1000.f, q[1] - P.from[1]);
            memcpy(P.lastPos, q, sizeof q);
        }
        if (P.bullet && b != P.bullet) {
            Log("probe16: RESULT %s: appeared +%lld, went off +%lld at %.0f %.0f %.0f, %.1f m away (%+.0f)", th.name, P.seenAt, r, P.lastPos[0], P.lastPos[1], P.lastPos[2],
                Dist2D(P.lastPos, P.from) / 1000.f, P.lastPos[1] - P.from[1]);
            P.bullet = 0; ++P.step; P.at = gFrame; P.phase = held == 0x13 ? 2 : 1;
            break;
        }
        if (r > 400) { Log("probe16: %s: no grenade seen", th.name); ++P.step; P.at = gFrame; P.phase = 1; }
        break;
    }
    default:
        break;
    }
}

// probe 17: the support row's items (the weapons page's third row): each life with the next one (the
// loadout's ",s<n>"), the weapon window's list read after the spawn (right until it comes round), then
// the pause menu's SUICIDE for the next life
struct Probe17 { int k = 0, phase = 0; long long at = 0, moveAt = -1000; std::vector<int> codes; };
Probe17 gP17;
void Probe17Frame() {
    Probe17& P = gP17;
    const long long t = gFrame - P.at;
    const uint32_t wl = ps2world::ActorByName("NewWeaponChangeLinear");
    const int open = wl ? R16(wl + 0x44) : -1, still = wl ? R16(wl + 0x48) : -1, cur = wl ? R16(wl + 0x54) : -1;
    switch (P.phase) {
    case 0: {                                            // the round runs, a moment after the spawn
        bool other = false;
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; break; }
        if (t > 90) { P.phase = 1; P.at = gFrame; P.codes.clear(); }
        break;
    }
    case 1:                                              // the window's list
        if (open != 0xC && t % 70 >= 60) break;
        gPad.buttons |= 0x0200;
        if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; break; }
        if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) { if (t > 700) { Log("probe17: no window"); P.phase = 2; P.at = gFrame; } break; }
        if (!P.codes.empty() && cur == P.codes[0]) {
            std::string l; for (int c : P.codes) { char b[16]; snprintf(b, sizeof b, " %x", c); l += b; }
            Log("probe17: RESULT support %d: the window's list%s", SupportNow(), l.c_str());
            gPad.buttons &= ~0x0200u; P.phase = 2; P.at = gFrame; break;
        }
        P.codes.push_back(cur);
        if (P.codes.size() > 12) { Log("probe17: the list does not come round"); gPad.buttons &= ~0x0200u; P.phase = 2; P.at = gFrame; break; }
        P.moveAt = gFrame; gPad.buttons |= 0x0020;
        break;
    case 2:                                              // the next support, then SUICIDE
        if (t < 30) break;
        if (P.k >= 7) { if (gFrame % 600 == 0) Log("probe17: done"); break; }
        if (t == 30) { ++P.k; gSet.loadout = "ak47,m1911,s" + std::to_string(P.k); Log("probe17: next life with support %d", P.k); }
        if (!ProbeSuicideFrame() && t > 200) { P.phase = 0; P.at = gFrame; }
        break;
    }
}

// probe 18: the support item's use (the loadout's ",s<n>": 5 the claymore, 6 the 7th): taken from the
// weapon window (the code next to the primary's in its list), then tried: Square tapped standing, crouched
// (Cross) and Square tapped, Square held 30 frames then let go, first person view (R1) and Square; after
// each the new actors logged with their points (what was put down, thrown), the window's list again
struct Probe18 { int phase = 0, step = 0, code = -1; long long at = 0, moveAt = -1000; std::map<uint32_t, std::string> before; std::vector<int> codes; };
Probe18 gP18;
void P18Collect(uint32_t w, const std::string& name, void* ctx) { (*(std::map<uint32_t, std::string>*)ctx)[w] = name; }
void Probe18Frame() {
    static const struct { uint32_t buttons; int hold; bool crouch; const char* name; } kTry[] = {
        {0x8000, 6, false, "Square tapped standing"}, {0x8000, 6, true, "crouched, Square tapped"}, {0x8000, 30, false, "Square held 30"},
        {0x8800, 20, false, "first person, Square"}, {0x8000, 6, false, "Square tapped again"}};
    const int n = (int)(sizeof kTry / sizeof kTry[0]);
    Probe18& P = gP18;
    const long long t = gFrame - P.at;
    const uint32_t wl = ps2world::ActorByName("NewWeaponChangeLinear");
    const int open = wl ? R16(wl + 0x44) : -1, still = wl ? R16(wl + 0x48) : -1, held = wl ? R16(wl + 0x50) : -1, cur = wl ? R16(wl + 0x54) : -1;
    switch (P.phase) {
    case 0: {                                            // the round runs: the window's list (the support: the code not 0, 1, 0xC, 6, 5)
        bool other = false;
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2 || gFrame < 4800) break;
        P.phase = 1; P.at = gFrame; P.codes.clear();
        break;
    }
    case 1:
        if (open != 0xC && t % 70 >= 60) break;
        gPad.buttons |= 0x0200;
        if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; break; }
        if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) { if (t > 700) { Log("probe18: no window"); P.phase = 9; } break; }
        if (P.code < 0) {
            if (!P.codes.empty() && cur == P.codes[0]) {
                for (int c : P.codes) if (c != 0 && c != 1 && c != 0xC && c != 6 && c != 5) P.code = c;
                std::string l; for (int c : P.codes) { char b[16]; snprintf(b, sizeof b, " %x", c); l += b; }
                Log("probe18: the list%s: the support %x", l.c_str(), P.code);
                if (P.code < 0) { gPad.buttons &= ~0x0200u; P.phase = 9; break; }
            } else { P.codes.push_back(cur); P.moveAt = gFrame; gPad.buttons |= 0x0020; break; }
        }
        if (cur == P.code) { gPad.buttons &= ~0x0200u; Log("probe18: %x taken (held %x)", P.code, held); P.phase = 2; P.at = gFrame; P.step = 0; break; }
        P.moveAt = gFrame; gPad.buttons |= 0x0020;
        break;
    case 2: {                                            // the tries
        if (P.step >= n) { Log("probe18: done"); P.phase = 9; break; }
        const auto& tr = kTry[P.step];
        if (t <= 1) { P.before.clear(); ps2world::ForEachActor(&P18Collect, &P.before); if (t == 1) Log("probe18: %s (me %.0f %.0f %.0f facing %.0f, state %x, held %x)", tr.name, gMe.pos[0], gMe.pos[1], gMe.pos[2], Facing() * 57.3f, gMe.bits[0], held); }
        if (tr.crouch && t < 40) { if (!Low() && t % 30 == 0) gPad.buttons |= 0x4000; break; }
        const long long u = (tr.crouch ? t - 40 : t) - 2;
        if (u >= 0 && u < tr.hold) { gPad.buttons |= tr.buttons; gPad.pressures[11] = 0xFF; }
        if (u == 40 || u == 120) {
            std::map<uint32_t, std::string> now; ps2world::ForEachActor(&P18Collect, &now);
            for (auto& a : now) {
                auto b = P.before.find(a.first);
                if (b != P.before.end() && b->second == a.second) continue;
                std::string s;
                for (uint32_t o = 0x20; o + 12 <= 0x400 && s.size() < 300; o += 4) {
                    const float x = RF(a.first + o), y = RF(a.first + o + 4), z = RF(a.first + o + 8);
                    if (std::fabs(x - gMe.pos[0]) < 30000.f && std::fabs(z - gMe.pos[2]) < 30000.f && std::fabs(y - gMe.pos[1]) < 8000.f && (std::fabs(x) > 100.f || std::fabs(z) > 100.f) && x == x) {
                        char b2[64]; snprintf(b2, sizeof b2, " +%x(%.0f %.0f %.0f)", o, x, y, z); s += b2; o += 8;
                    }
                }
                Log("probe18 %s +%lld: new %s %08x:%s", tr.name, u, a.second.c_str(), a.first, s.c_str());
            }
            if (u == 120) Log("probe18 %s: after (state %x, held %x, me %.0f %.0f %.0f)", tr.name, gMe.bits[0], held, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
        }
        if (u >= 130) {
            if (tr.crouch && Low() && gFrame % 30 == 0) gPad.buttons |= 0x4000;   // standing again
            if (u >= 170) { ++P.step; P.at = gFrame; }
        }
        break;
    }
    default:
        break;
    }
}

// probe 19: the grenade module (doll_brain.cpp): to a point 10 to 16 m from the other player, a throw
// planned at it and done; the grenade's burst point (the actor's last point) against the plan's, the
// player's life before and after; again from another point
struct Probe19 { int phase = 0, throws = 0; long long at = 0; float spot[3], predicted[3], last[3]; uint32_t bullet = 0; int lifeBefore = 0; };
Probe19 gP19;
void P19Find(uint32_t w, const std::string& name, void* ctx) { if (name == "NewBulletGrenadeWithBuller") *(uint32_t*)ctx = w; }
void Probe19Frame() {
    Probe19& P = gP19;
    const long long t = gFrame - P.at;
    const Player* e = nullptr;
    for (const Player& p : gPlayers) if (!p.local && p.alive) e = &p;
    switch (P.phase) {
    case 0:                                              // a point 10 to 16 m from it, on the mesh
        if (!e || gRoom.phase != 2 || gFrame < 4800) break;
        if (P.throws >= 4) { if (gFrame % 600 == 0) Log("probe19: done"); break; }
        for (int k = 0; k < 40; ++k) {
            float c[3];
            if (!gMap->nav->RandomPoint(e->pos, 16000.f, c, Rnd)) continue;
            const float d = Dist2D(c, e->pos);
            if (d < 10000.f || d > 16000.f) continue;
            if (!PlanTo(c)) continue;
            memcpy(P.spot, c, sizeof c); P.phase = 1; P.at = gFrame;
            Log("probe19: to %.0f %.0f %.0f, %.1f m from %s", c[0], c[1], c[2], d / 1000.f, e->name.c_str());
            break;
        }
        break;
    case 1:                                              // there
        if (!Following() || t > 2400) { StopPath(); Steer(0, 0, 0); P.phase = 2; P.at = gFrame; P.lifeBefore = e ? e->life : 0; break; }
        FollowPath();
        break;
    case 2:                                              // the throw
        if (!e) { P.phase = 0; break; }
        if (!ProbeGrenadeAt(e->id, P.predicted)) {
            if (t < 5) { Log("probe19: no throw from here"); P.phase = 0; ++P.throws; break; }
            P.phase = 3; P.at = gFrame; P.bullet = 0;
        }
        break;
    case 3: {                                            // the grenade's flight: its last point
        uint32_t b = 0; ps2world::ForEachActor(&P19Find, &b);
        if (b && !P.bullet) P.bullet = b;
        if (b && b == P.bullet) for (int i = 0; i < 3; ++i) P.last[i] = RF(b + 0x50 + 4 * i);
        if ((P.bullet && b != P.bullet) || t > 300) {
            const Player* now = e;
            Log("probe19: RESULT %s: went off at %.0f %.0f %.0f, %.1f m from the plan's point; %s's life %d -> %d", P.bullet ? "thrown" : "NO GRENADE SEEN",
                P.last[0], P.last[1], P.last[2], std::sqrt((P.last[0] - P.predicted[0]) * (P.last[0] - P.predicted[0]) + (P.last[2] - P.predicted[2]) * (P.last[2] - P.predicted[2])) / 1000.f,
                now ? now->name.c_str() : "-", P.lifeBefore, now ? now->life : 0);
            ++P.throws; P.phase = 0; P.at = gFrame;
        }
        break;
    }
    }
}

// probe 20: burning, and the cardboard box against it. The WP grenade (support 1, 0x14) thrown just ahead
// (first person, the view 40 degrees down), then a walk into its fire; the player's state words, life and
// the new actors logged every 10 frames; once burning (the life going down with no one near), the box
// put on from the item window (0xE), the same logged; then the box off
struct Probe20 { int phase = 0, lastLife = 0; long long at = 0, moveAt = -1000, relAt = -1, burnAt = -1; std::map<uint32_t, std::string> before; int win = 0; };
Probe20 gP20;
float gP20Last[3] = {0, 0, 0};
void P20Collect(uint32_t w, const std::string& name, void* ctx) { (*(std::map<uint32_t, std::string>*)ctx)[w] = name; }
bool P20Window(int win, int code) {                     // (the window, as the doll does it) true while working
    Probe20& P = gP20;
    const uint32_t wl = ps2world::ActorByName(win ? "NewItemChangeLinear" : "NewWeaponChangeLinear");
    if (!wl) return false;
    const uint32_t button = win ? 0x0100 : 0x0200;
    const int open = R16(wl + 0x44), still = R16(wl + 0x48), cur = R16(wl + 0x54);
    gPad.buttons |= button;
    if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; return true; }
    if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) return true;
    if (cur == code) { gPad.buttons &= ~button; return false; }
    P.moveAt = gFrame; gPad.buttons |= 0x0020;
    return true;
}
void Probe20Frame() {
    Probe20& P = gP20;
    const long long t = gFrame - P.at;
    float m[16] = {}; ps2world::ViewMatrix(m);
    const float camPitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    auto say = [&](const char* what) {
        char b[600]; int k = snprintf(b, sizeof b, "probe20 %s: life %d pos %.0f %.0f %.0f state", what, gMe.life, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
        for (int i = 0; i < 11; ++i) k += snprintf(b + k, sizeof b - k, " %x", gMe.bits[i]);
        std::map<uint32_t, std::string> now; ps2world::ForEachActor(&P20Collect, &now);
        for (auto& a : now) { auto it = P.before.find(a.first); if ((it == P.before.end() || it->second != a.second) && k < (int)sizeof b - 40) k += snprintf(b + k, sizeof b - k, " +%s", a.second.c_str()); }
        P.before.swap(now);
        Log("%s", b);
    };
    switch (P.phase) {
    case 0: {
        bool other = false;
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2 || gFrame < 4800) break;
        P.before.clear(); ps2world::ForEachActor(&P20Collect, &P.before);
        P.phase = 1; P.at = gFrame; say("start");
        break;
    }
    case 1:                                              // the WP grenade in the hands
        if (!P20Window(0, 0x14) && t > 20) { P.phase = 2; P.at = gFrame; }
        if (t > 600) { Log("probe20: no WP grenade"); P.phase = 9; }
        break;
    case 2:                                              // the view 40 degrees down, Square 20 frames
        gPad.buttons |= 0x0800;
        if (t < 60) { const float pe = -30.f * 3.14159265f / 180.f - camPitch; gPad.sticks[3] = (uint8_t)(0x80 - (int)((pe < 0 ? -0.6f : 0.6f) * 127 * (std::fabs(pe) > 0.02f))); break; }
        if (t < 80) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; break; }
        P.phase = 3; P.at = gFrame; say("thrown");
        break;
    case 3: {                                            // the burst (160 frames), then a walk into its fire
        if (t < 170) {
            uint32_t w = ps2world::ActorByName("NewBulletFireGrenadeWithBuller");
            if (w) for (int i = 0; i < 3; ++i) gP20Last[i] = RF(w + 0x50 + 4 * i);
            if (t % 30 == 0) say("waiting");
            break;
        }
        static float fire[3]; static bool have = false;
        if (!have) {                                     // the fire's point: where the grenade went off (its last point)
            fire[0] = gP20Last[0]; fire[1] = gP20Last[1]; fire[2] = gP20Last[2]; have = true;
            Log("probe20: the fire at %.0f %.0f %.0f", fire[0], fire[1], fire[2]);
        }
        if (have && Dist2D(gMe.pos, fire) > 300.f) Steer(fire[0] - gMe.pos[0], fire[2] - gMe.pos[2], 0.8f);
        if (t % 10 == 0) say("into the fire");
        if (P.lastLife && gMe.life < P.lastLife - 1 && P.burnAt < 0) { P.burnAt = gFrame; say("BURNING?"); }
        P.lastLife = gMe.life;
        if (P.burnAt >= 0 && gFrame - P.burnAt > 20) { P.phase = 4; P.at = gFrame; }
        if (t > 700) { say("not burnt"); P.phase = 9; }
        break;
    }
    case 4:                                              // the box on
        if (t % 10 == 0) say("burning, the box going on");
        if (!P20Window(1, 0xE) && t > 20) { P.phase = 5; P.at = gFrame; say("the box on"); }
        break;
    case 5:
        if (t % 10 == 0) say("in the box");
        if (t > 300) { P.phase = 6; P.at = gFrame; }
        break;
    case 6:                                              // the box off
        if (!P20Window(1, 0) && t > 20) { P.phase = 9; say("the box off"); }
        break;
    default:
        if (gFrame % 600 == 0) say("done");
        break;
    }
}

// probe 21: who the player is (the special characters: Snake, Sokolov, Raikov, Reiko, Rowdy): at each
// spawn its life, the weapon window's list and the item window's list, the player's variable set's first
// bytes and a few of its work's words; then the pause menu's SUICIDE for the next life (6 lives)
struct Probe21 { int phase = 0, win = 0, lives = 0; long long at = 0, moveAt = -1000, lastCall = 0; std::vector<int> codes; std::string lists; };
Probe21 gP21;
void Probe21Frame() {
    Probe21& P = gP21;
    static const bool stay = getenv("PROBE21_STAY") != nullptr;   // (no suicide: each life logged as it comes)
    if (stay && gFrame - P.lastCall > 30 && P.phase == 3) { P.phase = 0; P.at = gFrame; }
    P.lastCall = gFrame;
    const long long t = gFrame - P.at;
    if (P.phase == 3) return;
    switch (P.phase) {
    case 0: {                                            // a moment after the spawn, the round running
        bool other = false;
        for (const Player& p : gPlayers) if (!p.local && p.alive) other = true;
        if (!other || gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; break; }
        if (t > 60) { P.phase = 1; P.at = gFrame; P.win = 0; P.codes.clear(); P.lists.clear(); }
        break;
    }
    case 1: {                                            // the two windows' lists
        const uint32_t wl = ps2world::ActorByName(P.win ? "NewItemChangeLinear" : "NewWeaponChangeLinear");
        if (!wl) { P.phase = 2; break; }
        const uint32_t button = P.win ? 0x0100 : 0x0200;
        const int open = R16(wl + 0x44), still = R16(wl + 0x48), cur = R16(wl + 0x54);
        if (open != 0xC && t % 70 >= 60) break;
        gPad.buttons |= button;
        if (gFrame - P.moveAt < 4) { gPad.buttons |= 0x0020; break; }
        if (open != 0xC || still != 2 || gFrame - P.moveAt < 10) { if (t > 700) { P.lists += " (no window)"; P.phase = 2; } break; }
        if (!P.codes.empty() && cur == P.codes[0]) {
            P.lists += P.win ? " | items:" : " weapons:";
            for (int c : P.codes) { char b[16]; snprintf(b, sizeof b, " %x", c); P.lists += b; }
            gPad.buttons &= ~button;
            P.codes.clear(); P.at = gFrame;
            if (++P.win >= 2) P.phase = 2;
            break;
        }
        P.codes.push_back(cur);
        if (P.codes.size() > 16) { gPad.buttons &= ~button; P.phase = 2; break; }
        P.moveAt = gFrame; gPad.buttons |= 0x0020;
        break;
    }
    case 2: {                                            // the log, then SUICIDE
        if (P.lists.size()) {
            uint8_t v[0x20] = {};
            ps2world::VariableSet(gMyId + 1, v, sizeof v);
            char b[800]; int k = snprintf(b, sizeof b, "probe21: life %d (max %d) team %d vars", gMe.life, gMe.life, gMyTeam);
            for (int i = 0; i < 0x14; ++i) k += snprintf(b + k, sizeof b - k, " %02x", v[i]);
            k += snprintf(b + k, sizeof b - k, " | work");
            static const uint32_t kOff[] = {0xB60, 0xB62, 0xB64, 0xB66, 0xB6A, 0xB6C, 0xB6E, 0xB70, 0xB74, 0xB78, 0xDEC, 0xDF0, 0xDF4, 0xE00, 0xE10};
            for (uint32_t o : kOff) k += snprintf(b + k, sizeof b - k, " +%x=%x", o, R32(gMe.obj + o));
            Log("%s |%s", b, P.lists.c_str());
            P.lists.clear(); ++P.lives;
        }
        if (stay) { P.phase = 3; break; }
        if (P.lives >= 6) { if (gFrame % 600 == 0) Log("probe21: done"); break; }
        if (!ProbeSuicideFrame() && t > 300) { P.phase = 0; P.at = gFrame; }
        break;
    }
    }
}

// probe 22: the items and the camouflage. At each life (no suicide) the inventory as the game has it (its
// lists; each weapon's and item's state: 0x38 bytes), then each item of the item window taken in turn and
// kept 5 s: what came with it (actors made or gone; the words that changed in the equipment [0x23A678]
// +0x500..0x700, in the camouflage gauge's work, in the player's work +0xB00..0xE80, in the item's
// state); then none again. Last, still standing then crouched: the gauge's words that change (the index)
struct ItemSnap { std::map<std::string, int> actors; std::vector<uint32_t> equip, gauge, work, state; uint32_t bits0 = 0; };
void CountActor(uint32_t, const std::string& name, void* ctx) { ++(*(std::map<std::string, int>*)ctx)[name]; }
void Words(uint32_t base, uint32_t from, uint32_t to, std::vector<uint32_t>& out) {
    out.clear();
    if (!base) return;
    for (uint32_t o = from; o < to; o += 4) out.push_back(R32(base + o));
}
ItemSnap TakeSnap(int item) {
    ItemSnap s;
    ps2world::ForEachActor(&CountActor, &s.actors);
    Words(EquipBase(), 0x500, 0x700, s.equip);
    Words(ps2world::ActorByName("NewCamoufGauge"), 0, 0x200, s.gauge);
    Words(gMe.obj, 0xB00, 0xE80, s.work);
    Words(ItemState(item), 0, 0x38, s.state);
    s.bits0 = gMe.bits[0];
    return s;
}
std::string SnapDiff(const ItemSnap& a, const ItemSnap& b) {
    std::string o;
    char t[96];
    for (const auto& kv : b.actors) { auto it = a.actors.find(kv.first); const int was = it == a.actors.end() ? 0 : it->second; if (kv.second > was) { snprintf(t, sizeof t, " +%s", kv.first.c_str()); o += t; } }
    for (const auto& kv : a.actors) { auto it = b.actors.find(kv.first); const int now = it == b.actors.end() ? 0 : it->second; if (kv.second > now) { snprintf(t, sizeof t, " -%s", kv.first.c_str()); o += t; } }
    auto words = [&](const char* what, const std::vector<uint32_t>& x, const std::vector<uint32_t>& y, uint32_t from) {
        int n = 0;
        for (size_t i = 0; i < x.size() && i < y.size(); ++i)
            if (x[i] != y[i] && n++ < 24) { snprintf(t, sizeof t, " %s+%x %x>%x", what, (unsigned)(from + 4 * i), x[i], y[i]); o += t; }
    };
    words("equip", a.equip, b.equip, 0x500);
    words("gauge", a.gauge, b.gauge, 0);
    words("work", a.work, b.work, 0xB00);
    words("state", a.state, b.state, 0);
    if (a.bits0 != b.bits0) { snprintf(t, sizeof t, " state0 %x>%x", a.bits0, b.bits0); o += t; }
    return o.empty() ? std::string(" (nothing)") : o;
}
struct Probe22 { int phase = 0; size_t idx = 0, snapped = (size_t)-1; long long at = 0, lastCall = 0; std::vector<int> queue; ItemSnap before; };
Probe22 gP22;
void Probe22Frame() {
    Probe22& P = gP22;
    if (gFrame - P.lastCall > 30 && P.phase == 9) { P.phase = 0; P.at = gFrame; }   // (a new life)
    P.lastCall = gFrame;
    const long long t = gFrame - P.at;
    auto say = [](const std::string& m) { Log("probe22: %s", m.c_str()); };
    switch (P.phase) {
    case 0: {                                            // a moment after the spawn, the round running
        if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; break; }
        if (t < 90) break;
        const Inventory& v = Inv();
        if (!v.ok) { if (t > 600) { say("no inventory (the page's states not found)"); P.phase = 9; } break; }
        char b[1600]; int n = snprintf(b, sizeof b, "life %d, work +b60 %x +b62 %x; in the hands %x, item %x; weapons:", gMe.life, R16(gMe.obj + 0xB60), R16(gMe.obj + 0xB62), v.weapon, v.item);
        for (int c : v.weapons) {
            n += snprintf(b + n, sizeof b - n, " [%x:", c);
            for (uint32_t o = 0x10; o < 0x28 && n < (int)sizeof b - 12; o += 2) n += snprintf(b + n, sizeof b - n, " %d", R16s(WeaponState(c) + o));
            n += snprintf(b + n, sizeof b - n, "]");
        }
        say(b);
        n = snprintf(b, sizeof b, "items:");
        for (int c : v.items) {
            n += snprintf(b + n, sizeof b - n, " [%x:", c);
            for (uint32_t o = 0x10; o < 0x28 && n < (int)sizeof b - 12; o += 2) n += snprintf(b + n, sizeof b - n, " %d", R16s(ItemState(c) + o));
            n += snprintf(b + n, sizeof b - n, "]");
        }
        n += snprintf(b + n, sizeof b - n, " | equipment +5a0:");
        for (uint32_t o = 0x5A0; o < 0x5E0 && n < (int)sizeof b - 12; o += 2) n += snprintf(b + n, sizeof b - n, " %x", R16(EquipBase() + o));
        say(b);
        P.queue.clear();
        for (int c : v.items) if (c != 0xE) P.queue.push_back(c);   // (the box: known)
        P.idx = 0; P.snapped = (size_t)-1; P.phase = 1; P.at = gFrame;
        break;
    }
    case 1:                                              // the next item taken
        if (P.idx >= P.queue.size()) { P.phase = 4; P.at = gFrame; P.before = TakeSnap(0); break; }
        if (P.snapped != P.idx) { P.before = TakeSnap(P.queue[P.idx]); P.snapped = P.idx; }   // (before the window)
        if (Inv().item == P.queue[P.idx] && t > 20) { P.phase = 2; P.at = gFrame; say("item " + std::to_string(P.queue[P.idx]) + " taken"); break; }
        if (!ProbeWindowTo(1, P.queue[P.idx]) && t > 400) { say("item " + std::to_string(P.queue[P.idx]) + " NOT taken"); ++P.idx; P.at = gFrame; }
        break;
    case 2: {                                            // kept 5 s: what came with it
        if (t == 30) {
            const uint32_t wl = ps2world::ActorByName("NewItemChangeLinear");
            if (wl) { char b[200]; snprintf(b, sizeof b, "item window +44 %x +48 %x +50 %x +54 %x", R16(wl + 0x44), R16(wl + 0x48), R16(wl + 0x50), R16(wl + 0x54)); say(b); }
        }
        if (t < 150) break;
        const ItemSnap after = TakeSnap(P.queue[P.idx]);
        char h[32]; snprintf(h, sizeof h, "item %x kept 5 s:", P.queue[P.idx]);
        say(h + SnapDiff(P.before, after));
        P.phase = 3; P.at = gFrame;
        break;
    }
    case 3:                                              // none again
        if (Inv().item == 0 && t > 20) { if (t > 60) { ++P.idx; P.phase = 1; P.at = gFrame; } break; }
        if (!ProbeWindowTo(1, 0) && t > 400) { say("none NOT taken"); ++P.idx; P.phase = 1; P.at = gFrame; }
        break;
    case 4:                                              // standing still, then crouched: the gauge
        Steer(0, 0, 0);
        if (t < 90) break;
        if (t == 90) { say("standing 3 s:" + SnapDiff(P.before, TakeSnap(0))); P.before = TakeSnap(0); }
        if (!SetLow(true) && t < 200) break;
        if (t < 300) break;
        say("crouched 3 s:" + SnapDiff(P.before, TakeSnap(0)));
        SetLow(false);
        P.phase = 9;
        break;
    default:
        break;
    }
}

// ---- probe 23: the radio (NewNetworkRadio): a sequence of presses (MGO1_DOLL_PROBE_SEQ = "frame:buttons
// hex:length,..." from the round's start, e.g. "120:10:60"), the radio actors' words logged when they change ----
struct Probe23 { long long at = -1; std::vector<std::array<long long, 3>> seq; std::map<uint32_t, std::string> last; bool read = false; };
Probe23 gP23;
void RadioActor(uint32_t w, const std::string& name, void* ctx) {
    if (name == "NewNetworkRadio") static_cast<std::vector<uint32_t>*>(ctx)->push_back(w);
}
void Probe23Frame() {
    Probe23& P = gP23;
    if (!P.read) {
        P.read = true;
        const char* e = getenv("MGO1_DOLL_PROBE_SEQ");
        for (const char* q = e ? e : "120:10:60,300:10:4,330:2000:4,420:10:4,440:40:4,460:2000:4"; *q;) {
            long long f = 0, len = 4; unsigned b = 0;
            if (sscanf(q, "%lld:%x:%lld", &f, &b, &len) >= 2) P.seq.push_back({f, (long long)b, len});
            const char* c = strchr(q, ','); if (!c) break; q = c + 1;
        }
    }
    if (gRoom.phase != 2) return;
    if (P.at < 0) { P.at = gFrame; Log("probe23: the round runs: the sequence starts (%zu presses)", P.seq.size()); }
    const long long t = gFrame - P.at;
    for (const auto& x : P.seq) if (t >= x[0] && t < x[0] + x[2]) gPad.buttons |= (uint32_t)x[1];
    {   // the pad's words the game reads (0x2C5E20: +0 held, +0x48, +0x5C, +0x60; 0x2C5238 +0x10 / +0x14)
        static std::string lastPad;
        std::string now; char pb[24];
        for (uint32_t o = 0; o < 0x80; o += 4) { snprintf(pb, sizeof pb, " %x", R32(0x2C5E20 + o)); now += pb; }
        snprintf(pb, sizeof pb, " | %x %x", R32(0x2C5248), R32(0x2C524C)); now += pb;
        if (now != lastPad && t < 900) { Log("probe23: t %lld pad %04x words:%s", t, gPad.buttons, now.c_str()); lastPad = now; }
    }
    std::vector<uint32_t> radios;
    if (const char* g = getenv("MGO1_DOLL_PROBE_RADIO")) {   // the radio menu's global (n011a: 2e508c)
        const uint32_t w = R32((uint32_t)strtoul(g, nullptr, 16));
        if (w >= 0x100000 && w < 0x02000000) radios.push_back(w);
    } else ps2world::ForEachActor(&RadioActor, &radios);
    static const uint32_t offs[] = {0xb0, 0xb4, 0xb8, 0xfc, 0x104, 0x108, 0x10c, 0x110, 0x114, 0x158, 0x160, 0x164, 0x168, 0x178, 0x17c, 0x180, 0x78c, 0x794};
    for (uint32_t w : radios) {
        std::string now;
        char b[48];
        for (uint32_t o : offs) { snprintf(b, sizeof b, " +%x %x", o, R32(w + o)); now += b; }
        if (P.last[w] != now) {
            Log("probe23: t %lld pad %04x radio %08x:%s", t, gPad.buttons, w, now.c_str());
            if (P.last[w].empty()) {                     // its phrase table: 4 categories x 4 directions (+0x118)
                std::string tab;
                for (uint32_t o = 0x118; o < 0x158; o += 4) { snprintf(b, sizeof b, " %d", (int)R32(w + o)); tab += b; }
                Log("probe23: radio %08x table:%s", w, tab.c_str());
            }
            P.last[w] = now;
        }
    }
}

// ---- probe 24: the crossings (doll_nav.h: an obstacle between two floors: a fence, a low wall, a window).
// The crossings reachable from the spawn (by path length, 10 at most; MGO1_DOLL_PROBE_KIND vault: the
// vaults as the paths take them (the follower's choice: a roll over a low one at level 3 and up, else the
// action button); rollvault: the vaults, a roll forced first, then the action button; roll: the roll
// links, the roll first, then the action button; all: both kinds, the action button first, then the
// roll): to its start by the paths, then across as the paths do (ForceCrossing: FollowPath's own code),
// and back from the far start (by the paths again); MGO1_DOLL_PROBE_ROLLAT the roll's distance to the
// obstacle (mm). Measured in v133: section 3 of docs/PLAYABLE_DOLL.md ----
struct Probe24 { int phase = 0, way = 0, ok = 0, tried = 0; size_t idx = 0; long long at = 0; uint8_t kind = 0, action = AREA_VAULT, first = AREA_VAULT; bool listed = false, solo = false;
                 std::vector<std::array<float, 8>> links; };
Probe24 gP24;
void Probe24Next(Probe24& P) { ++P.idx; P.way = 0; P.phase = 0; P.at = gFrame; P.action = P.first; StopPath(); }
void Probe24Frame() {
    Probe24& P = gP24;
    if (!P.listed) {
        P.listed = true;
        const char* k = getenv("MGO1_DOLL_PROBE_KIND");
        P.kind = k && !strcmp(k, "roll") ? AREA_ROLL : k && (!strcmp(k, "vault") || !strcmp(k, "rollvault")) ? AREA_VAULT : 0;
        P.first = k && (!strcmp(k, "roll") || !strcmp(k, "rollvault")) ? AREA_ROLL : AREA_VAULT;   // (rollvault: the vaults, the roll first)
        P.solo = k && !strcmp(k, "vault");
        if (const char* r = getenv("MGO1_DOLL_PROBE_ROLLAT")) gCrossRollAt = (float)atof(r);
        float g[3];
        if (!Ground(g)) { P.listed = false; return; }
        std::vector<std::pair<float, std::array<float, 8>>> all;
        int total = 0;
        for (int i = 0; i < gMap->nav->LinkCount(); ++i) {
            float a[3], b[3], sill = 0, lintel = 0; uint8_t area = 0;
            if (!gMap->nav->LinkAt(i, a, b, &area) || (area != AREA_VAULT && area != AREA_ROLL) || (P.kind && area != P.kind)) continue;
            ++total;
            gMap->nav->LinkShape(i, &sill, &lintel);
            std::vector<Waypoint> w; float len = 0;
            if (!gMap->nav->Path(g, a, w, &len) || len > 150000.f || Dist2D(w.back().p, a) > 600.f) continue;
            all.push_back({len, {a[0], a[1], a[2], b[0], b[1], b[2], sill, lintel}});
        }
        std::sort(all.begin(), all.end(), [](const std::pair<float, std::array<float, 8>>& x, const std::pair<float, std::array<float, 8>>& y) { return x.first < y.first; });
        for (size_t i = 0; i < all.size() && i < 10; ++i) P.links.push_back(all[i].second);
        Log("probe24: %d crossings of that kind on the map, %zu reachable, the %zu nearest tried (the roll %.0f mm before the obstacle)", total, all.size(), P.links.size(),
            gCrossRollAt);
        P.at = gFrame; P.action = P.first;
    }
    if (P.idx >= P.links.size()) {
        if (P.phase != 99) { Log("probe24: done: %d crossings of %d", P.ok, P.tried); P.phase = 99; }
        Steer(0, 0, 0);
        return;
    }
    const std::array<float, 8>& L = P.links[P.idx];
    const float* a = P.way == 0 ? &L[0] : &L[3];
    const float* b = P.way == 0 ? &L[3] : &L[0];
    switch (P.phase) {
    case 0:                                                    // to its start by the paths
        if (Dist2D(gMe.pos, a) < 500.f && std::fabs(gMe.pos[1] - 800.f - a[1]) < 900.f) { StopPath(); Steer(0, 0, 0); P.phase = 1; P.at = gFrame; return; }
        if (gFrame - P.at > 2400) { Log("probe24: crossing %zu at %.0f %.0f %.0f not reached", P.idx, a[0], a[1], a[2]); Probe24Next(P); return; }
        if (!Following() && !PlanTo(a)) { Log("probe24: crossing %zu at %.0f %.0f %.0f: no way to it", P.idx, a[0], a[1], a[2]); Probe24Next(P); return; }
        FollowPath();
        return;
    case 1:                                                    // still a moment, then across
        Steer(0, 0, 0);
        if (gFrame - P.at < 30) return;
        Log("probe24: crossing %zu %s by %s: from %.0f %.0f %.0f to %.0f %.0f %.0f (its top %.0f, %s %.0f)", P.idx, P.way ? "back" : "across",
            P.action == AREA_ROLL ? "a roll" : "the action button", a[0], a[1], a[2], b[0], b[1], b[2], L[6], L[7] > 0 ? "a lintel at" : "open above", L[7]);
        ForceCrossing(a, b, P.action);
        ++P.tried; P.phase = 2; P.at = gFrame;
        return;
    default: {                                                 // FollowPath until it ends
        if (Following()) { FollowPath(); return; }
        // across: past the obstacle (along the crossing, as FollowPath's own test), on the far floor
        const float len = std::max(1.f, Dist2D(a, b)), ux = (b[0] - a[0]) / len, uz = (b[2] - a[2]) / len;
        const float along = (gMe.pos[0] - a[0]) * ux + (gMe.pos[2] - a[2]) * uz;
        const float aside = (gMe.pos[0] - a[0]) * uz - (gMe.pos[2] - a[2]) * ux;   // (a roll carries on some metres)
        const bool crossed = along > len - 450.f && along < len + 5000.f && std::fabs(aside) < 1500.f && std::fabs(gMe.pos[1] - 800.f - b[1]) < 450.f;
        Log("probe24: crossing %zu %s by %s: %s in %lld frames (at %.0f %.0f %.0f, hips %.0f above the ground)", P.idx, P.way ? "back" : "across",
            P.action == AREA_ROLL ? "a roll" : "the action button", crossed ? "CROSSED" : "not crossed", gFrame - P.at, gMe.pos[0], gMe.pos[1], gMe.pos[2], HipHeight());
        if (crossed) ++P.ok;
        if (P.way == 0 && crossed) { P.way = 1; P.phase = 0; P.at = gFrame; StopPath(); return; }   // back: from its start (the paths)
        if (!crossed && !P.solo && P.action == P.first) {    // the other way next, from the same side
            P.action = P.action == AREA_ROLL ? AREA_VAULT : AREA_ROLL; P.phase = 0; P.at = gFrame; StopPath();
            return;
        }
        Probe24Next(P);
        return;
    }
    }
}

// ---- probe 26: the first person view's lean and crouch (v133). The round running, standing still: steps
// of held buttons (R1 the view, L2 / R2 the lean, Cross, Square readied then full); at each step's 2nd,
// middle and last frame: the view's eye and yaw (and their change from the first step's), the state
// words, the weapon in the hands and its rounds, the windows' open state. Three times ----
struct Probe26 { int step = 0, round = 0; long long at = -1; float eye0[3] = {0, 0, 0}; float yaw0 = 0; bool base = false; };
Probe26 gP26;
void Probe26Frame() {
    Probe26& P = gP26;
    struct Step { uint32_t buttons; int square, len; const char* what; };
    static const Step kSteps[] = {
        {0x0800, 0, 40, "R1 (the view)"},        {0x0900, 0, 60, "R1 + L2"},                    {0x0800, 0, 40, "R1, L2 let go"},
        {0x0A00, 0, 60, "R1 + R2"},              {0x0800, 0, 40, "R1, R2 let go"},              {0x4800, 0, 4, "R1 + Cross"},
        {0x0800, 0, 40, "R1 (crouched?)"},       {0x0800, 0, 30, "R1 crouched + the left stick right"}, {0x0800, 0, 30, "R1 crouched + the stick half right"},
        {0x0800, 0, 30, "R1 crouched + the stick up"},                                          {0x0900, 0, 60, "R1 + L2 crouched"},           {0x8900, 0x3F, 30, "R1 + L2 + Square readied"},
        {0x8900, 0xFF, 8, "R1 + L2 + a shot"},   {0x8900, 0x3F, 30, "R1 + L2 + Square readied"}, {0x8A00, 0x3F, 30, "R1 + R2 + Square readied"},
        {0x8A00, 0xFF, 8, "R1 + R2 + a shot"},   {0x0800, 0, 30, "R1"},                         {0x4800, 0, 4, "R1 + Cross"},
        {0x0800, 0, 40, "R1 (standing?)"},       {0x0A00, 0, 40, "R1 + R2 (the window)"},       {0x0A00, 0, 20, "R1 + R2 + the left stick right"},
        {0x0A00, 0, 30, "R1 + R2 again"},        {0x0800, 0, 30, "R1, R2 let go"},              {0x0900, 0, 20, "R1 + L2 + the left stick right"},
        {0x0000, 0, 90, "nothing"}};
    const int n = (int)(sizeof kSteps / sizeof kSteps[0]);
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = -1; return; }
    if (P.round >= 3) { if (gFrame % 600 == 0) Log("probe26: done"); return; }
    if (P.at < 0) { P.at = gFrame; P.step = 0; P.base = false; Log("probe26: round %d, weapon %x", P.round + 1, Inv().weapon); }
    const Step& s = kSteps[P.step];
    const long long t = gFrame - P.at;
    gPad.buttons = s.buttons;
    if (s.square) gPad.pressures[11] = (uint8_t)s.square;
    Steer(0, 0, 0);
    if (strstr(s.what, "stick right")) gPad.sticks[2] = 0xFF;          // (the left stick: the view's turn in the first person view)
    if (strstr(s.what, "stick half right")) gPad.sticks[2] = 0x80 + 58;   // (0.46: the fight's short pushes)
    if (strstr(s.what, "stick up")) gPad.sticks[3] = 0x00;
    float m[16] = {};
    const bool view = ps2world::ViewMatrix(m);
    const float yaw = std::atan2(m[8], m[10]), pitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    if (P.step == 0 && t == s.len - 1 && view) { memcpy(P.eye0, m + 12, sizeof P.eye0); P.yaw0 = yaw; P.base = true; }
    if (t == 2 || t == s.len / 2 || t == s.len - 1) {
        const float rx = -std::cos(P.yaw0), rz = std::sin(P.yaw0);          // the first view's right (the stick right turns the yaw down)
        const float dx = m[12] - P.eye0[0], dy = m[13] - P.eye0[1], dz = m[14] - P.eye0[2];
        const uint32_t ww = ps2world::ActorByName("NewWeaponChangeLinear"), iw = ps2world::ActorByName("NewItemChangeLinear");
        const Inventory& v = Inv();
        const uint32_t st = v.weapon > 0 ? WeaponState(v.weapon) : 0;
        Log("probe26: %s t %lld: eye %.0f %.0f %.0f (right %+.0f, up %+.0f, ahead %+.0f), yaw %+.1f pitch %.1f | state %x %x %x | weapon %x rounds %d+%d | "
            "windows %x/%x cursor %x, %x/%x | low %d hips %.0f, at %.0f %.0f %.0f",
            s.what, t, m[12], m[13], m[14], P.base ? dx * rx + dz * rz : 0.f, P.base ? dy : 0.f, P.base ? dx * std::sin(P.yaw0) + dz * std::cos(P.yaw0) : 0.f,
            P.base ? Wrap(yaw - P.yaw0) * 57.2958f : 0.f, pitch * 57.2958f, gMe.bits[0], gMe.bits[1], gMe.bits[2], v.weapon, st ? R16s(st + 0x1C) : -1,
            st ? R16s(st + 0x18) : -1, ww ? R16(ww + 0x44) : 0xFFFF, ww ? R16(ww + 0x48) : 0xFFFF, ww ? R16(ww + 0x54) : 0xFFFF, iw ? R16(iw + 0x44) : 0xFFFF,
            iw ? R16(iw + 0x48) : 0xFFFF,
            Low() ? 1 : 0, HipHeight(), gMe.pos[0], gMe.pos[1], gMe.pos[2]);
    }
    if (t + 1 >= s.len) { P.at = gFrame + 1; if (++P.step >= n) { P.step = 0; ++P.round; P.at = -1; } }
}

// ---- probe 27: hits at range (v133). The nearest other player (a scripted one, standing; deathmatch)
// 6, 12 then 25 m away; the doll still, its view (R1) aimed at the other's chest with the fight's stick:
// single shots (Square full 8 frames), bursts of 3 (17) and 4 (23), sprays (40); each trial its bullets
// (the magazine and the rounds before and after), the other's life before and at its lowest in the 45
// frames after. At 6 m also single shots at heights above its hips (650 to 950 mm): the life lost, a
// kill, the doll's headshots (+0x3A) ----
struct Probe27 { int phase = 0, dIdx = 0, trial = 0, lifeBefore = 0, lifeMin = 0, bulletsBefore = 0, hsBefore = 0, killsBefore = 0, target = -1;
                 long long at = 0, onSince = -1; uint32_t stateBefore = 0; bool spotHave = false; float spot[3] = {0, 0, 0}, spotFor[3] = {0, 0, 0}; };
Probe27 gP27;
void Probe27Frame() {
    Probe27& P = gP27;
    static const float kDist[3] = {6000.f, 12000.f, 25000.f};
    struct Trial { int frames; float above; const char* what; };
    static const Trial kBody[] = {{8, -1, "single"}, {8, -1, "single"}, {8, -1, "single"}, {8, -1, "single"}, {8, -1, "single"}, {8, -1, "single"},
                                  {17, -1, "burst 3"}, {17, -1, "burst 3"}, {17, -1, "burst 3"}, {17, -1, "burst 3"},
                                  {23, -1, "burst 4"}, {23, -1, "burst 4"}, {23, -1, "burst 4"}, {23, -1, "burst 4"},
                                  {40, -1, "spray"}, {40, -1, "spray"}, {40, -1, "spray"}};
    static const Trial kHead[] = {{8, 650, "head 650"}, {8, 650, "head 650"}, {8, 725, "head 725"}, {8, 725, "head 725"}, {8, 800, "head 800"}, {8, 800, "head 800"},
                                  {8, 850, "head 850"}, {8, 850, "head 850"}, {8, 900, "head 900"}, {8, 900, "head 900"}, {8, 950, "head 950"}, {8, 950, "head 950"}};
    const int nBody = (int)(sizeof kBody / sizeof kBody[0]), nHead = (int)(sizeof kHead / sizeof kHead[0]);
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    if (P.dIdx >= 3) { if (gFrame % 600 == 0) Log("probe27: done"); Steer(0, 0, 0); return; }
    const int nHeadHere = P.dIdx == 0 ? nHead : 0, nTrials = nBody + nHeadHere;   // (at 6 m the heads first)
    const Trial& tr = P.trial < nHeadHere ? kHead[P.trial] : kBody[P.trial - nHeadHere];
    const Player* tgt = nullptr; float best = 1e30f;
    for (const Player& p : gPlayers)
        if (!p.local && p.alive && p.team != 0xFE && Dist2D(p.pos, gMe.pos) < best) { best = Dist2D(p.pos, gMe.pos); tgt = &p; }   // (never the host)
    const long long t = gFrame - P.at;
    const Inventory& v = Inv();
    auto bullets = [&]() { const uint32_t st = v.weapon > 0 ? WeaponState(v.weapon) : 0; return st ? R16s(st + 0x18) : -1; };   // (+0x18 the rounds had)
    if (!tgt) { Steer(0, 0, 0); if (P.phase >= 2) { P.phase = 0; P.at = gFrame; } return; }
    const float want = kDist[P.dIdx], dist = best;
    float m[16] = {};
    const bool view = ps2world::ViewMatrix(m);
    const float gy = MapReady() ? gMap->nav->GroundBelow(tgt->pos, 2500.f) : -1e30f;
    const float h = gy > -1e29f ? std::max(250.f, tgt->pos[1] - gy) : 800.f;
    const float above = tr.above > 0 ? tr.above : 0.45f * h;
    const float aim[3] = {tgt->pos[0], tgt->pos[1] + above, tgt->pos[2]};
    switch (P.phase) {
    case 0: {                                            // to a spot at the distance with a clear line to its chest
        if (v.ok && v.weapon != 0xC && v.rounds[0xC] >= 0) { ProbeWindowTo(0, 0xC); return; }
        const float eye[3] = {gMe.pos[0], gMe.pos[1] + 760.f, gMe.pos[2]};
        const float chest[3] = {tgt->pos[0], tgt->pos[1] + 0.45f * h, tgt->pos[2]};
        if (std::fabs(dist - want) < 1200.f && LineClear(eye, chest)) { StopPath(); Steer(0, 0, 0); P.phase = 1; P.at = gFrame; P.onSince = -1; P.spotHave = false; return; }
        if (t > 2400) { Log("probe27: %.0f m not reached (at %.1f m)", want / 1000.f, dist / 1000.f); P.dIdx++; P.trial = 0; P.at = gFrame; StopPath(); P.spotHave = false; return; }
        if (P.spotHave && Dist2D(P.spotFor, tgt->pos) > 2000.f) P.spotHave = false;   // (it moved: another spot)
        if (!P.spotHave && MapReady()) {                 // 36 places around it at the distance: the reachable one nearest, a clear line
            float bestD = 1e30f;
            for (int k = 0; k < 36; ++k) {
                const float a = k * 10.f / 57.2958f;
                const float c[3] = {tgt->pos[0] + std::sin(a) * want, tgt->pos[1] - 800.f, tgt->pos[2] + std::cos(a) * want};
                float q[3];
                if (!gMap->nav->Nearest(c, 800.f, q) || std::fabs(Dist2D(q, tgt->pos) - want) > 800.f) continue;
                const float e2[3] = {q[0], q[1] + 1560.f, q[2]};
                if (!LineClear(e2, chest) || Dist2D(q, gMe.pos) >= bestD) continue;
                bestD = Dist2D(q, gMe.pos); memcpy(P.spot, q, sizeof P.spot); P.spotHave = true;
            }
            memcpy(P.spotFor, tgt->pos, sizeof P.spotFor);
            if (!P.spotHave) { Log("probe27: no spot %.0f m from %s with a clear line", want / 1000.f, tgt->name.c_str()); P.dIdx++; P.trial = 0; P.at = gFrame; return; }
            StopPath();
        }
        if (Dist2D(gMe.pos, P.spot) < 700.f) { StopPath(); Steer(0, 0, 0); if (t % 120 == 0) P.spotHave = false; return; }   // (there, and no line: another spot)
        if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
        FollowPath();
        return;
    }
    case 1: {                                            // the view on the point, still 12 frames
        gPad.buttons |= 0x8800; gPad.pressures[11] = 0x3F;
        if (!view || t < 20) return;
        const float yaw = std::atan2(m[8], m[10]), pitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
        const float tx = aim[0] - m[12], tz = aim[2] - m[14], ty = aim[1] - m[13];
        const float ye = Wrap(std::atan2(tx, tz) - yaw), pe = std::atan2(ty, std::sqrt(tx * tx + tz * tz)) - pitch;
        auto s = [](float deg) { const float a = std::fabs(deg); const float x = a < 0.2f ? 0.f : a < 1.5f ? (gFrame % 8 < 2 ? 0.46f : 0.f) : std::min(1.f, 0.36f + a * 0.045f); return deg < 0 ? -x : x; };
        const float tol = std::max(0.15f, std::atan2(40.f, dist) * 57.2958f);
        const bool on = std::fabs(ye) * 57.2958f < tol && std::fabs(pe) * 57.2958f < tol;
        if (!on) { P.onSince = -1; gPad.sticks[2] = (uint8_t)(0x80 + (int)(-s(ye * 57.2958f) * 127)); gPad.sticks[3] = (uint8_t)(0x80 - (int)(s(pe * 57.2958f) * 127)); }
        else if (P.onSince < 0) P.onSince = gFrame;
        if (t > 600) { Log("probe27: the aim not settled (%.2f %.2f deg)", ye * 57.2958f, pe * 57.2958f); P.phase = 0; P.at = gFrame; return; }
        if (P.onSince < 0 || gFrame - P.onSince < 12) return;
        P.lifeBefore = P.lifeMin = tgt->life; P.bulletsBefore = bullets(); P.hsBefore = MyScore(0x3A); P.killsBefore = MyScore(0x14); P.target = tgt->id;
        P.stateBefore = gMe.bits[1];
        P.phase = 2; P.at = gFrame;
        return;
    }
    case 2:                                              // the trigger, then 45 frames
        gPad.buttons |= 0x8800; gPad.pressures[11] = t < tr.frames ? 0xFF : 0x3F;
        if (tgt->id == P.target) P.lifeMin = std::min(P.lifeMin, tgt->life);
        else P.lifeMin = 0;                                // (another one nearest: it died)
        if (t < tr.frames + 45) return;
        Log("probe27: %.1f m %s (%d frames%s): bullets %d, %s life %d -> %d%s, headshots %+d, kills %+d (state 1 %x, then %x)", dist / 1000.f, tr.what, tr.frames,
            tr.above > 0 ? ", the point above its hips" : "", P.bulletsBefore - bullets(), tgt->name.c_str(), P.lifeBefore, P.lifeMin, P.lifeMin <= 0 ? " (down)" : "",
            MyScore(0x3A) - P.hsBefore, MyScore(0x14) - P.killsBefore, P.stateBefore, gMe.bits[1]);
        if (++P.trial >= nTrials) { P.trial = 0; ++P.dIdx; }
        P.phase = P.lifeMin <= 0 || std::fabs(dist - want) > 1500.f ? 0 : 1; P.at = gFrame; P.onSince = -1;
        return;
    }
}

// ---- probe 28: the third person's aim (v133, the audit: the beginners outscored the experts). The
// nearest other player (a scripted one, standing) 6, 12 then 20 m away; the AK-47 readied (Square light),
// the facing turned with the stick to the direction of its chest plus an offset (0 to 45 degrees, each
// side), the stick let go 20 frames (the facing's drift: does the game turn the player by itself?), then
// one bullet (Square full 5 frames) and 40 frames: the facing's error when the bullet leaves, the
// other's life before and at its lowest (a hit: 30) ----
struct Probe28 { int phase = 0, dIdx = 0, trial = 0, lifeBefore = 0, lifeMin = 0, bulletsBefore = 0, target = -1;
                 long long at = 0, onSince = -1, turnAt = 0; float errShot = 0, errReady = 0, driftMax = 0; bool spotHave = false; float spot[3] = {0, 0, 0}, spotFor[3] = {0, 0, 0}; };
Probe28 gP28;
void Probe28Frame() {
    Probe28& P = gP28;
    const float kD = 3.14159265f / 180.f;
    static const float kDist[3] = {6000.f, 12000.f, 20000.f};
    static const float kOff[] = {0, 3, -3, 6, -6, 10, -10, 15, -15, 20, -20, 30, -30, 45, -45};
    const int nTrials = (int)(sizeof kOff / sizeof kOff[0]);
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    if (P.dIdx >= 3) { if (gFrame % 600 == 0) Log("probe28: done"); Steer(0, 0, 0); return; }
    const Player* tgt = nullptr; float best = 1e30f;
    for (const Player& p : gPlayers)
        if (!p.local && p.alive && p.team != 0xFE && Dist2D(p.pos, gMe.pos) < best) { best = Dist2D(p.pos, gMe.pos); tgt = &p; }   // (never the host)
    const long long t = gFrame - P.at;
    const Inventory& v = Inv();
    auto bullets = [&]() { const uint32_t st = v.weapon > 0 ? WeaponState(v.weapon) : 0; return st ? R16s(st + 0x18) : -1; };
    if (!tgt) { Steer(0, 0, 0); if (P.phase >= 2) { P.phase = 0; P.at = gFrame; } return; }
    const float want = kDist[P.dIdx], dist = best;
    const float gy = MapReady() ? gMap->nav->GroundBelow(tgt->pos, 2500.f) : -1e30f;
    const float h = gy > -1e29f ? std::max(250.f, tgt->pos[1] - gy) : 800.f;
    const float chest[3] = {tgt->pos[0], tgt->pos[1] + 0.45f * h, tgt->pos[2]};
    const float fa = Facing(), mz[3] = {gMe.pos[0] + std::sin(fa) * 450.f, gMe.pos[1] + 380.f, gMe.pos[2] + std::cos(fa) * 450.f};   // (the muzzle)
    const float dirTo = AngleTo(mz, chest);
    const float off = kOff[P.trial] * kD;
    switch (P.phase) {
    case 0: {                                            // to a spot at the distance with a clear line to its chest
        if (v.ok && v.weapon != 0xC && v.rounds[0xC] >= 0) { ProbeWindowTo(0, 0xC); return; }
        const float eye[3] = {gMe.pos[0], gMe.pos[1] + 760.f, gMe.pos[2]};
        if (std::fabs(dist - want) < 1500.f && LineClear(eye, chest)) { StopPath(); Steer(0, 0, 0); P.phase = 1; P.at = gFrame; P.onSince = -1; P.spotHave = false; P.turnAt = gFrame; return; }
        if (t > 2400) { Log("probe28: %.0f m not reached (at %.1f m)", want / 1000.f, dist / 1000.f); P.dIdx++; P.trial = 0; P.at = gFrame; StopPath(); P.spotHave = false; return; }
        if (P.spotHave && Dist2D(P.spotFor, tgt->pos) > 2000.f) P.spotHave = false;
        if (!P.spotHave && MapReady()) {
            float bestD = 1e30f;
            for (int k = 0; k < 36; ++k) {
                const float a = k * 10.f / 57.2958f;
                const float c[3] = {tgt->pos[0] + std::sin(a) * want, tgt->pos[1] - 800.f, tgt->pos[2] + std::cos(a) * want};
                float q[3];
                if (!gMap->nav->Nearest(c, 800.f, q) || std::fabs(Dist2D(q, tgt->pos) - want) > 800.f) continue;
                const float e2[3] = {q[0], q[1] + 1560.f, q[2]};
                if (!LineClear(e2, chest) || Dist2D(q, gMe.pos) >= bestD) continue;
                bestD = Dist2D(q, gMe.pos); memcpy(P.spot, q, sizeof P.spot); P.spotHave = true;
            }
            memcpy(P.spotFor, tgt->pos, sizeof P.spotFor);
            if (!P.spotHave) { Log("probe28: no spot %.0f m from %s with a clear line", want / 1000.f, tgt->name.c_str()); P.dIdx++; P.trial = 0; P.at = gFrame; return; }
            StopPath();
        }
        if (Dist2D(gMe.pos, P.spot) < 700.f) { StopPath(); Steer(0, 0, 0); if (t % 120 == 0) P.spotHave = false; return; }
        if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
        FollowPath();
        return;
    }
    case 1: {                                            // readied, the facing turned to the direction plus the offset
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
        if (t < 15) return;
        const float goal = Wrap(dirTo + off), err = Wrap(goal - Facing());
        if (std::fabs(err) > 0.8f * kD) { P.onSince = -1; Steer(std::sin(goal), std::cos(goal), std::fabs(err) > 8.f * kD ? 0.64f : 0.57f); }
        else { Steer(0, 0, 0); if (P.onSince < 0) P.onSince = gFrame; }
        if (t > 600) { Log("probe28: the facing not turned to %+.0f deg (%.1f deg off)", kOff[P.trial], err / kD); P.phase = 0; P.at = gFrame; return; }
        if (P.onSince < 0 || gFrame - P.onSince < 6) return;
        P.errReady = Wrap(Facing() - dirTo); P.driftMax = 0;
        if (gDebug) Log("probe28: turned to %+.0f deg in %lld frames", kOff[P.trial], gFrame - P.turnAt);
        P.phase = 2; P.at = gFrame;
        return;
    }
    case 2: {                                            // the stick let go 20 frames: the facing's drift
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; Steer(0, 0, 0);
        P.driftMax = std::max(P.driftMax, std::fabs(Wrap(Facing() - dirTo - P.errReady)));
        if (t < 20) return;
        P.errShot = Wrap(Facing() - dirTo);
        P.lifeBefore = P.lifeMin = tgt->life; P.bulletsBefore = bullets(); P.target = tgt->id;
        P.phase = 3; P.at = gFrame;
        return;
    }
    case 3:                                              // the bullet, then 40 frames
        gPad.buttons |= 0x8000; gPad.pressures[11] = t < 5 ? 0xFF : 0x3F; Steer(0, 0, 0);
        if (tgt->id == P.target) P.lifeMin = std::min(P.lifeMin, tgt->life); else P.lifeMin = 0;
        if (t == 12) P.driftMax = std::max(P.driftMax, std::fabs(Wrap(Facing() - dirTo - P.errReady)));
        if (t < 45) return;
        Log("probe28: %.1f m, offset %+.0f deg: facing %.1f deg off readied, %.1f when fired (drift %.1f), bullets %d, %s life %d -> %d%s (state %x %x)", dist / 1000.f,
            kOff[P.trial], P.errReady / kD, P.errShot / kD, P.driftMax / kD, P.bulletsBefore - bullets(), tgt->name.c_str(), P.lifeBefore, P.lifeMin,
            P.lifeMin < P.lifeBefore ? " (hit)" : "", gMe.bits[0], gMe.bits[1]);
        if (++P.trial >= nTrials) { P.trial = 0; ++P.dIdx; }
        P.phase = P.lifeMin <= 0 || std::fabs(dist - want) > 2000.f ? 0 : 1; P.at = gFrame; P.onSince = -1; P.turnAt = gFrame;
        return;
    }
}

// ---- probe 29: the first person view's stick (v133: tracking a moving head). Still where it spawns, R1
// held; the stick right (the yaw) at a deflection d for N frames then let go, the view's yaw each frame
// until it rests (40 frames after): its turn at the release, its run-on after, the frame it began, the
// fastest frame; then a few pushes up and down (the pitch). Each trial a line ----
struct Probe29 { int trial = 0, phase = 0; long long at = 0; float yaw0 = 0, pitch0 = 0, prev = 0, atRelease = 0, maxRate = 0; int began = -1; std::string per; };
Probe29 gP29;
void Probe29Frame() {
    Probe29& P = gP29;
    struct Trial { float d; int n; bool pitch; };
    static std::vector<Trial> kT;
    if (kT.empty()) {
        for (float d : {0.3f, 0.35f, 0.4f, 0.46f, 0.55f, 0.7f, 0.85f, 1.f})
            for (int n : {1, 2, 4, 8, 16, 40}) kT.push_back({d, n, false});
        for (float d : {0.46f, 0.7f, 1.f})
            for (int n : {2, 8, 16}) { kT.push_back({d, n, true}); kT.push_back({-d, n, true}); }
    }
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    Steer(0, 0, 0);
    if (P.trial >= (int)kT.size()) { if (gFrame % 600 == 0) Log("probe29: done"); return; }
    gPad.buttons |= 0x0800;                              // R1: the first person view
    float m[16];
    if (!ps2world::ViewMatrix(m)) return;
    const float kD = 3.14159265f / 180.f;
    const float yaw = std::atan2(m[8], m[10]), pitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    const Trial& tr = kT[P.trial];
    const long long t = gFrame - P.at;
    if (P.phase == 0) {                                  // at rest 40 frames first
        if (t < 40) return;
        P.yaw0 = P.prev = tr.pitch ? pitch : yaw; P.atRelease = 0; P.maxRate = 0; P.began = -1; P.per.clear();
        P.phase = 1; P.at = gFrame;
        return;
    }
    const float now = tr.pitch ? pitch : yaw;
    const float step = (tr.pitch ? now - P.prev : -Wrap(now - P.prev)) / kD;   // (the yaw goes down to the right)
    P.prev = now;
    if (std::fabs(step) > 0.005f && P.began < 0) P.began = (int)t;
    P.maxRate = std::max(P.maxRate, std::fabs(step));
    if (t < 48) { char b[16]; snprintf(b, sizeof b, " %.2f", step); P.per += b; }
    if (t < tr.n) {                                      // pushed
        const int v = (int)(tr.d * 127);
        if (tr.pitch) gPad.sticks[3] = (uint8_t)(0x80 - v); else gPad.sticks[2] = (uint8_t)(0x80 + v);
        return;
    }
    if (t == tr.n) P.atRelease = (tr.pitch ? now - P.yaw0 : -Wrap(now - P.yaw0)) / kD;
    if (t < tr.n + 40) return;
    const float total = (tr.pitch ? now - P.yaw0 : -Wrap(now - P.yaw0)) / kD;
    Log("probe29: %s %.2f x %d frames: %.2f deg at the release, %.2f in all (run-on %.2f), began at frame %d, fastest %.2f deg a frame; frames:%s", tr.pitch ? "pitch" : "yaw",
        tr.d, tr.n, P.atRelease, total, total - P.atRelease, P.began, P.maxRate, P.per.c_str());
    ++P.trial; P.phase = 0; P.at = gFrame;
}

// ---- probe 30: the first person view's stick, pushes in a row (v133): how a push adds to the view's
// run-on, a push changed while held, a push the other way (a brake), trains of short pushes. Each
// sequence (deflection, frames; 0 let go) then 40 frames still: the yaw's step each frame ----
struct Probe30 { int seq = 0, phase = 0; long long at = 0; float prev = 0, yaw0 = 0; std::string per; };
Probe30 gP30;
void Probe30Frame() {
    Probe30& P = gP30;
    struct Seg { float d; int n; };
    static const std::vector<std::vector<Seg>> kSeq = {
        {{1.f, 16}, {0.f, 4}, {0.46f, 8}},
        {{1.f, 16}, {0.46f, 8}},
        {{0.46f, 16}, {1.f, 8}},
        {{0.7f, 8}, {0.f, 2}, {0.7f, 8}},
        {{0.7f, 8}, {-0.7f, 4}},
        {{1.f, 16}, {-0.46f, 6}},
        {{1.f, 16}, {-1.f, 4}},
        {{0.55f, 2}, {0.f, 2}, {0.55f, 2}, {0.f, 2}, {0.55f, 2}, {0.f, 2}, {0.55f, 2}, {0.f, 2}, {0.55f, 2}, {0.f, 2}, {0.55f, 2}, {0.f, 2}},
        {{0.55f, 2}, {0.f, 6}, {0.55f, 2}, {0.f, 6}, {0.55f, 2}, {0.f, 6}, {0.55f, 2}, {0.f, 6}},
        {{0.46f, 2}, {0.f, 2}, {0.46f, 2}, {0.f, 2}, {0.46f, 2}, {0.f, 2}, {0.46f, 2}, {0.f, 2}},
        {{0.7f, 2}, {0.f, 4}, {0.7f, 2}, {0.f, 4}, {0.7f, 2}, {0.f, 4}, {0.7f, 2}, {0.f, 4}},
        {{1.f, 1}, {0.f, 40}},
        {{0.7f, 3}, {0.f, 40}},
    };
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    Steer(0, 0, 0);
    if (P.seq >= (int)kSeq.size()) { if (gFrame % 600 == 0) Log("probe30: done"); return; }
    gPad.buttons |= 0x0800;                              // R1: the first person view
    float m[16];
    if (!ps2world::ViewMatrix(m)) return;
    const float kD = 3.14159265f / 180.f;
    const float yaw = std::atan2(m[8], m[10]);
    const std::vector<Seg>& sq = kSeq[P.seq];
    const long long t = gFrame - P.at;
    if (P.phase == 0) { if (t < 40) return; P.prev = P.yaw0 = yaw; P.per.clear(); P.phase = 1; P.at = gFrame; return; }
    const float step = -Wrap(yaw - P.prev) / kD;
    P.prev = yaw;
    char b[16]; snprintf(b, sizeof b, " %.2f", step); P.per += b;
    int total = 0; float d = 0.f; bool pushing = false;
    for (const Seg& s : sq) { if (t < total + s.n && !pushing) { d = s.d; pushing = true; } total += s.n; }
    if (pushing) { if (d != 0.f) gPad.sticks[2] = (uint8_t)(0x80 + (int)(d * 127)); return; }
    if (t < total + 40) return;
    std::string what;
    for (const Seg& s : sq) { char c[24]; snprintf(c, sizeof c, "%s%.2fx%d", what.empty() ? "" : " ", s.d, s.n); what += c; }
    Log("probe30: %s: %.2f deg in all; frames:%s", what.c_str(), -Wrap(yaw - P.yaw0) / kD, P.per.c_str());
    ++P.seq; P.phase = 0; P.at = gFrame;
}

// ---- probe 31: the new mechanics (v134). Two dolls: MGO1_DOLL_PROBE_ROLE actor (the steps) and victim
// (still where it spawns, facing the other player, a radio call every 5 s; in capture a KEROTAN held
// first). Both log their own state and the other player's when they change, the game's status word
// 0x2C5248, the actors made and gone (the grenades', the camera's and the radar's dumped), the rolls' and
// goggles' counters. The actor's steps (MGO1_DOLL_PROBE_STEPS, "ergt" by default): e the emote (Triangle
// held 120, 4, 30, 240 frames: the player's work before, during, after); r the roll into the victim (Cross
// while running at it, 1.4 to 2.2 m before it; the objectives' holders before and after); g a stun, a
// chaff and a smoke grenade (MGO1_DOLL_PROBE_GRENADES "012": a life each, ",s2" ",s3" ",s4") thrown at the
// victim, the stun's thrower turned away, a radio call every 5 s; t the thermal goggles (the weapon boxes
// in turn until had), on 60 s, off 30 s: their state; l the AK-47's magazine by five ways to reload (v136);
// p a pocket of the mesh at MGO1_DOLL_PROBE_AT (into it from its rim, out by the brain's escape, 3 times);
// b the cardboard box: on, moves, Square, R2 twice, off, the same moves without it (the victim logs the
// actor's box as its machine sees it). v137: q a ladder's freeze (the victim too: its steps hold q), h the
// hang at a ladder's top, v a vault (Q31, H31, V31 below) ----
struct Seen31 { uint32_t b[11] = {}; int life = -1, stun = -1, down = -1; bool have = false; };
struct Probe31 {
    int role = 0;                                        // 1 actor, 2 victim
    std::string steps; size_t stepIdx = 0;
    int sub = 0, trial = 0, grenade = 0, gIdx = 0;
    long long at = 0, lastCall = -100, pressAt = -1, radioAt = 0;
    bool newLife = true, spotHave = false, knocked = false, job = false;
    float spot[3] = {0, 0, 0}, predicted[3] = {0, 0, 0}, minDist = 1e30f;
    int rollsBefore = -1, victimId = -1;
    std::vector<uint32_t> s0, s1, s2, s3;                // the player's work: still, still, during, after
    std::vector<std::array<float, 3>> tried;             // the weapon boxes tried (the thermal goggles)
    std::map<uint32_t, std::string> actors;
    std::map<uint32_t, long long> special;               // a special actor's first frame (dumped every 60 frames 10 s)
    std::map<int, Seen31> seen;                          // -1 the doll itself
    std::map<int, std::array<int, 2>> counters;          // player id -> rolls, goggles
    long long smokeDumpAt = -1000, radarDumpAt = -1000;
    long long faceSince = -1;                            // (the emote step: turning to the other player since)
    int magBefore = -1, lastWeapon = -1, lastMag = -1;   // (the reload and box steps)
    long long reloadAt = -1, reloadEnd = -1;
    float moveFrom[3] = {0, 0, 0}, moveDir = 0;
    bool boxOther = false;                               // (the victim: the other player's box seen)
    size_t rimIdx = 0;                                   // (the pocket step: the rim tried)
};
Probe31 gP31;
bool gChokeBroken31 = false;                             // ((v141 beta 9) the choke step: a death or the round's end since its grab)
const char* kSpecial31[] = {"NewFlushOnCamera", "NewStunBlast", "NewChaffBlast", "NewChaffGrenade", "NewRadioBreak", "NewScreenNoize", "NewSmokeBlast",
                            "NewSpreadSmoke", "NewBulletSturnGrenade", "NewBulletSmokeGrenade", "NewBulletChaffGrenade", "NewThermalMode", "NewSightThermal",
                            "NewNightVisionMode", "NewStar", "NewSleep", "NewSpacy", "NewMonochrome", "NewFadeInOut", "NewCommonSmoke", "NewStrataSmoke", "NewDelayBlast"};
bool Special31(const std::string& n) { for (const char* s : kSpecial31) if (n.rfind(s, 0) == 0) return true; return false; }
void Collect31(uint32_t w, const std::string& name, void* ctx) { (*(std::map<uint32_t, std::string>*)ctx)[w] = name; }
// an actor's work: its words (8 a line) and the float triples that look like points near the doll
void Dump31(const char* tag, const std::string& name, uint32_t w, uint32_t bytes) {
    for (uint32_t o = 0; o < bytes; o += 32) {
        char b[160]; int n = snprintf(b, sizeof b, "probe31 dump %s %s %08x +%03x:", tag, name.c_str(), w, o);
        for (uint32_t k = 0; k < 32; k += 4) n += snprintf(b + n, sizeof b - n, " %08x", R32(w + o + k));
        Log("%s", b);
    }
    std::string s;
    for (uint32_t o = 0; o + 12 <= bytes && s.size() < 400; o += 4) {
        const float x = RF(w + o), y = RF(w + o + 4), z = RF(w + o + 8);
        if (x == x && z == z && std::fabs(x - gMe.pos[0]) < 40000.f && std::fabs(z - gMe.pos[2]) < 40000.f && std::fabs(y - gMe.pos[1]) < 8000.f && std::fabs(x) + std::fabs(z) > 200.f) {
            char b[64]; snprintf(b, sizeof b, " +%x(%.0f %.0f %.0f)", o, x, y, z); s += b; o += 8;
        }
    }
    if (!s.empty()) Log("probe31 dump %s %s %08x points:%s", tag, name.c_str(), w, s.c_str());
}
void Work31(std::vector<uint32_t>& out) { out.clear(); if (gMe.obj) for (uint32_t o = 0; o < 0x1000; o += 4) out.push_back(R32(gMe.obj + o)); }
// the words changed from a to b but not from n0 to n1 (the still frames' own changes left out)
void WorkDiff31(const char* what, const std::vector<uint32_t>& n0, const std::vector<uint32_t>& n1, const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    std::string s; int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size() && i < n0.size() && i < n1.size(); ++i)
        if (a[i] != b[i] && n0[i] == n1[i] && n++ < 60) { char t[48]; snprintf(t, sizeof t, " +%x %x>%x", (unsigned)(4 * i), a[i], b[i]); s += t; }
    Log("probe31 work %s (%d words):%s", what, n, s.c_str());
}
const Player* Other31() {                               // the other player (never the host)
    const Player* o = nullptr; float best = 1e30f;
    for (const Player& p : gPlayers) if (!p.local && p.alive && p.team != 0xFE && Dist2D(p.pos, gMe.pos) < best) { best = Dist2D(p.pos, gMe.pos); o = &p; }
    return o;
}
void Watch31() {
    Probe31& P = gP31;
    auto line = [&](int id, const char* who, const uint32_t* b, int life, int stun, int down, const float* pos) {
        Seen31& s = P.seen[id];
        bool ch = !s.have || life != s.life || down != s.down || std::abs(stun - s.stun) >= 1000;
        for (int k : {0, 1, 2, 6, 10}) if (b[k] != s.b[k]) ch = true;
        if (!ch) return;
        Log("probe31 %s: state %x %x %x | 6 %x 9 %x 10 %x | life %d stun %d down %d at %.0f %.0f %.0f (%.2f m)", who, b[0], b[1], b[2], b[6], b[9], b[10], life, stun, down,
            pos[0], pos[1], pos[2], Dist2D(pos, gMe.pos) / 1000.f);
        memcpy(s.b, b, sizeof s.b); s.life = life; s.stun = stun; s.down = down; s.have = true;
    };
    line(-1, "me", gMe.bits, gMe.life, gMe.stun, gMe.obj ? R8(gMe.obj + 0xDF0) : -1, gMe.pos);
    if (gMe.obj) {                                       // (v150-host beta 22, blast: the state word 5, the flash's auto aim count, the reaction)
        static uint32_t w5 = 0xFFFFFFFF; static int f3c = -1, de3 = -1;
        const uint32_t a = gMe.bits[5]; const int b = (int)R32(gMe.obj + 0xF3C), c = (int)R8(gMe.obj + 0xDE3);
        if (a != w5 || (b > 0) != (f3c > 0) || c != de3) { Log("probe31 me+: 5 %x, the flash's count +0xF3C %d, the reaction +0xDE3 %d", a, b, c); w5 = a; f3c = b; de3 = c; }
        static int de9 = -1, b72 = 0;                    // (v150-host beta 22, wake: its reading, its pending stun)
        const int r = (int)R8(gMe.obj + 0xDE9), q = (int)R16s(gMe.obj + 0xB72);
        if (r != de9 || (q != 0) != (b72 != 0)) { Log("probe31 me+: reading +0xDE9 %d, the pending stun +0xB72 %d, stamina %d of %d, state %x %x", r, q, gMe.stun, (int)R16s(gMe.obj + 0xB6A), gMe.bits[0], gMe.bits[1]); de9 = r; b72 = q; }
        float m[16];                                     // (the camera against the player: its eye from his hips along his facing, up, to his right; its pitch)
        if (gFrame % 120 == 0 && ps2world::ViewMatrix(m)) {
            const float f = Facing(), dx = m[12] - gMe.pos[0], dz = m[14] - gMe.pos[2];
            Log("probe31 cam: its eye %.0f along his facing, %.0f up, %.0f to his right of his hips; its forward's pitch %.1f, its yaw %.1f off his facing (state %x)",
                dx * std::sin(f) + dz * std::cos(f), m[13] - gMe.pos[1], dx * std::cos(f) - dz * std::sin(f), std::asin(std::max(-1.f, std::min(1.f, m[9]))) * 57.2958f,
                Wrap(std::atan2(m[8], m[10]) - f) * 57.2958f, gMe.bits[0]);
        }
    }
    for (const Player& p : gPlayers) {
        if (p.local || p.team == 0xFE) continue;
        char who[64]; snprintf(who, sizeof who, "watch %s", p.name.c_str());
        line(p.id, who, p.bits, p.life, p.stun, p.down ? 1 : 0, p.pos);
    }
    static uint32_t status = 0xFFFFFFFF;                 // (the game's status word: a chaff sets 0x10 each frame, n011a 0x4AA744)
    if (R32(0x2C5248) != status) { status = R32(0x2C5248); Log("probe31 status 2c5248: %x", status); }
    if (gFrame % 10 == 0)                                // the rolls' and goggles' counters (their variable sets)
        for (int id = 0; id < 16; ++id) {
            uint8_t v[0x4A]; int t = 0xFF; std::string nm;
            if (!ps2world::PlayerInfo(id, &t, &nm) || nm.empty() || t == 0xFE || !ps2world::VariableSet(id + 1, v, sizeof v)) continue;
            const std::array<int, 2> c = {v[0x46] | (v[0x47] << 8), v[0x48] | (v[0x49] << 8)};
            auto it = P.counters.find(id);
            if (it == P.counters.end() || it->second != c) { Log("probe31 counters: %s rolls %d goggles %d", nm.c_str(), c[0], c[1]); P.counters[id] = c; }
        }
}
void Actors31() {
    Probe31& P = gP31;
    if (gFrame % 4) return;
    std::map<uint32_t, std::string> now;
    ps2world::ForEachActor(&Collect31, &now);
    std::string added, gone;
    for (const auto& a : now) {
        auto it = P.actors.find(a.first);
        if (it != P.actors.end() && it->second == a.second) continue;
        if (a.second.find("Foot") == std::string::npos && added.size() < 600) added += " " + a.second;
        if (Special31(a.second)) { P.special[a.first] = gFrame; Dump31("new", a.second, a.first, 0x100); }
    }
    for (const auto& a : P.actors) {
        auto it = now.find(a.first);
        if (it != now.end() && it->second == a.second) continue;
        if (a.second.find("Foot") == std::string::npos && gone.size() < 600) gone += " " + a.second;
        if (Special31(a.second)) { char b[32]; snprintf(b, sizeof b, " (%lld frames)", gFrame - (P.special.count(a.first) ? P.special[a.first] : gFrame)); gone += b; P.special.erase(a.first); }
    }
    if (!added.empty()) Log("probe31 actors +%s", added.c_str());
    if (!gone.empty()) Log("probe31 actors -%s", gone.c_str());
    bool smoke = false, chaff = false;
    for (auto& s : P.special) {
        const auto it = now.find(s.first);
        if (it == now.end()) continue;
        if (it->second.find("Smoke") != std::string::npos) smoke = true;
        if (it->second.find("Chaff") != std::string::npos || it->second == "NewRadioBreak") chaff = true;
        const long long age = gFrame - s.second;
        if (age > 0 && age % 60 == 0 && age <= 600) { char tag[24]; snprintf(tag, sizeof tag, "+%lld", age); Dump31(tag, it->second, it->first, 0x100); }
    }
    if (smoke && gFrame - P.smokeDumpAt >= 120) {         // the smoke's manager while a smoke is out
        P.smokeDumpAt = gFrame;
        if (const uint32_t m = ps2world::ActorByName("NewNetworkSmokeManager")) Dump31("smoke", "NewNetworkSmokeManager", m, 0x300);
    }
    if (chaff && gFrame - P.radarDumpAt >= 120) {         // the radar's and the radio's works while a chaff is out
        P.radarDumpAt = gFrame;
        for (const char* n : {"NewOnlineRadar", "NewOnlineRadarMan", "NewNetworkRadio"}) if (const uint32_t w = ps2world::ActorByName(n)) Dump31("chaff", n, w, 0x200);
    }
    P.actors.swap(now);
}
// a place `want` from the other player on the mesh (a straight walkable way to it, or a clear line), the nearest
bool Spot31(const Player& o, float want, bool walkable) {
    Probe31& P = gP31;
    float bestD = 1e30f; bool any = false;
    const float feet[3] = {o.pos[0], o.pos[1] - 800.f, o.pos[2]}, chest[3] = {o.pos[0], o.pos[1] + 360.f, o.pos[2]};
    for (int k = 0; k < 36; ++k) {
        const float a = k * 10.f * 3.14159265f / 180.f;
        const float c[3] = {o.pos[0] + std::sin(a) * want, o.pos[1] - 800.f, o.pos[2] + std::cos(a) * want};
        float q[3];
        if (!gMap->nav->Nearest(c, 800.f, q) || std::fabs(Dist2D(q, o.pos) - want) > 800.f) continue;
        const float e[3] = {q[0], q[1] + 1560.f, q[2]};
        if (walkable ? !gMap->nav->Walkable(q, feet) : !LineClear(e, chest)) continue;
        std::vector<Waypoint> w; float len = 0; float g[3];
        if (!Ground(g) || !gMap->nav->Path(g, q, w, &len)) continue;
        if (len >= bestD) continue;
        bestD = len; memcpy(P.spot, q, sizeof P.spot); any = true;
    }
    P.spotHave = any;
    return any;
}
bool GetUp31();                                          // (v150-host beta 22, blast: below)
void Probe31Next() { Probe31& P = gP31; ++P.stepIdx; P.sub = 0; P.trial = 0; P.at = gFrame; P.spotHave = false; StopPath(); Steer(0, 0, 0); }
// a weapon's magazine (its state +0x1C), its size (+0x1E), its rounds (+0x18)
int MagOf(int c, int* size, int* rounds) {
    const uint32_t st = WeaponState(c);
    if (!st) { *size = *rounds = -1; return -1; }
    *rounds = R16s(st + 0x18); *size = R16s(st + 0x1E);
    return R16s(st + 0x1C);
}
// the most open way from the doll on the standing floor (16 directions), radians
float OpenWay31() {
    float g[3]; if (!Ground(g)) return Facing();
    float best = -1.f, a = Facing();
    for (int k = 0; k < 16; ++k) {
        const float d = k * 6.2831853f / 16.f, c = gMap->nav->ClearAlong(g, std::sin(d), std::cos(d), 12000.f, true);
        if (c > best) { best = c; a = d; }
    }
    return a;
}
struct FindBoxOf { uint32_t obj; bool on; };
void BoxOf31(uint32_t w, const std::string& name, void* ctx) {
    FindBoxOf& f = *(FindBoxOf*)ctx;
    if (name != "NewNetworkCBox_NoBreak" || !f.obj) return;
    for (uint32_t o = 0x20; o < 0x100; o += 4) if ((R32(w + o) & 0x01FFFFFF) == (f.obj & 0x01FFFFFF)) { f.on = true; return; }
}

// (v141 beta 8) the precision step's bullets: each new one's line (where it left, its step a frame), its pass by the point aimed
// at and by his head, across the view (right, up)
struct Bullets31 { std::map<uint32_t, long long> seen; float eye[3], fwd[3], right[3], up[3], point[3], head[3]; bool have = false; };
Bullets31 gB31;
void Bullet31(uint32_t w, const std::string& name, void* ctx) {
    if (name != "NewSniperBullet" && name != "NewBullet") return;
    const long long now = *(long long*)ctx;
    auto it = gB31.seen.find(w);
    const bool fresh = it == gB31.seen.end() || now - it->second > 3;   // (a work used again by a later bullet)
    gB31.seen[w] = now;
    if (!fresh) return;
    const float o[3] = {RF(w + 0xE0), RF(w + 0xE4), RF(w + 0xE8)}, v[3] = {RF(w + 0x110), RF(w + 0x114), RF(w + 0x118)};
    const float vl = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!gB31.have || vl < 1.f) { Log("probe31 bullet %08x: origin %.0f %.0f %.0f, step %.0f %.0f %.0f", w, o[0], o[1], o[2], v[0], v[1], v[2]); return; }
    auto pass = [&](const float* p, float* side, float* up, float* along) {   // the bullet's line at p's distance: its offset from p
        const float d[3] = {p[0] - o[0], p[1] - o[1], p[2] - o[2]};
        const float t = (d[0] * v[0] + d[1] * v[1] + d[2] * v[2]) / (vl * vl);
        const float q[3] = {o[0] + v[0] * t - p[0], o[1] + v[1] * t - p[1], o[2] + v[2] * t - p[2]};
        *side = q[0] * gB31.right[0] + q[1] * gB31.right[1] + q[2] * gB31.right[2];
        *up = q[0] * gB31.up[0] + q[1] * gB31.up[1] + q[2] * gB31.up[2];
        *along = t * vl;
    };
    float s1, u1, a1, s2, u2, a2; pass(gB31.point, &s1, &u1, &a1); pass(gB31.head, &s2, &u2, &a2);
    const float oe[3] = {o[0] - gB31.eye[0], o[1] - gB31.eye[1], o[2] - gB31.eye[2]};
    const float dot = (v[0] * gB31.fwd[0] + v[1] * gB31.fwd[1] + v[2] * gB31.fwd[2]) / vl;
    Log("probe31 bullet %08x: from %.0f %.0f %.0f, its step %.0f %.0f %.0f; from the eye %.0f right %.0f up %.0f ahead; its way %.2f degrees off the view; at the point aimed (%.1f m) %.0f mm right %.0f mm up; "
        "at his head %.0f mm right %.0f mm up", w, o[0], o[1], o[2], v[0], v[1], v[2], oe[0] * gB31.right[0] + oe[1] * gB31.right[1] + oe[2] * gB31.right[2], oe[0] * gB31.up[0] + oe[1] * gB31.up[1] + oe[2] * gB31.up[2],
        oe[0] * gB31.fwd[0] + oe[1] * gB31.fwd[1] + oe[2] * gB31.fwd[2], std::acos(std::max(-1.f, std::min(1.f, dot))) * 57.29578f, a1 / 1000.f, s1, u1, s2, u2);
}

// ---- v137, q: a ladder's freeze (MGO1_DOLL_PROBE_LADDER, 1). MGO1_DOLL_PROBE_QMODE: circle (default), stick,
// roll, none: the victim climbs up to the actor at the top (roll: 2 m back), who acts when it is on the ladder
// within 1.5 m (roll: 3.5 m) with its hips MGO1_DOLL_PROBE_QY (4000) over the foot's floor: Circle held 40 frames
// as CqcFrame (a CQC weapon in the hands first), the stick toward it the first 8 (stick), a roll at it (roll),
// nothing (none); cross, crossnone: the victim down from the top while the actor goes up, Circle at it from the
// top (cross). MGO1_DOLL_PROBE_QSPOT ("1": x 16.2 m, z -36 to -31 m on the upper floor, or "x,y,z,x,y,z"): the
// victim through those two floor points first. MGO1_DOLL_PROBE_QTRIALS (2). Both log every 30 frames for 30 s ----
const float kDeg31 = 3.14159265f / 180.f;
enum { QM_CIRCLE, QM_STICK, QM_ROLL, QM_NONE, QM_CROSS, QM_CROSSNONE, QM_COUNT };
const char* const kQMode31[QM_COUNT] = {"circle", "stick", "roll", "none", "cross", "crossnone"};
struct Q31 {
    bool init = false, have = false, spot = false;
    int mode = QM_CIRCLE, trials = 2, trial = 0, phase = 0, leg = 0, cphase = 0, ctries = 0;
    float qy = 4000.f, floorFoot = 0.f, floorTop = 0.f, edge[2] = {1.f, 0.f};
    float wait[3] = {0, 0, 0}, foot[3] = {0, 0, 0}, start[3] = {0, 0, 0}, spotPts[2][3] = {{16200.f, 6000.f, -36000.f}, {16200.f, 6000.f, -31000.f}};
    Ladder l;
    long long at = 0, cat = 0, cStill = -1, cOff = -1, winFrom = -1, dg0 = 0, rollAt = -1, thereSince = -1, seen = 0;
    float last[2][3] = {}, moved[2] = {0.f, 0.f}, pushFrom[3] = {0, 0, 0};
    double maxGap = 0.0;
};
Q31 gQ31;
long long gDeadAt31 = -1;                                // (the last frame dead: ProbeFrame)
// dead since the step's last frame (a grab's or a knock-down's gap is not a new life)
bool Died31(long long& seen) { const bool d = gDeadAt31 > seen; seen = gFrame; return d; }
float Dist3D31(const float* a, const float* b) { const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return std::sqrt(x * x + y * y + z * z); }
// in the RAM (its segments 0, 2, 3, 8, 0xA) or the scratchpad (0x70000000, 16 KB)
bool Ram31(uint32_t a) {
    const uint32_t seg = a >> 28, off = a & 0x0FFFFFFFu;
    return !a || (seg == 7 ? off < 0x4000u : (seg == 0 || seg == 2 || seg == 3 || seg == 8 || seg == 0xA) && off < 0x02000000u);
}
// the collision query's state (US program): 0x2891C0 the chain walk's double buffer (+0 its index, 0 or 1; +4, +8
// the chains pending; +0xC, +0x10 the buffers; +0x14, +0x18 the DMA sources); 0x288DC0 the object instances met (256
// words, then 0x2891C0; no bound at 0x1579AC), 0x23BC2C their count. Logged when the index is not 0 or 1, a chain or
// a source is outside the memory, or the count is over 200 (40 lines at most)
struct Coll31State { uint32_t w[6] = {}; int lines = 0; };
Coll31State gColl31;
void Coll31(const char* who) {
    Coll31State& C = gColl31;
    const uint32_t w[6] = {R32(0x2891C0), R32(0x2891C4), R32(0x2891C8), R32(0x2891D4), R32(0x2891D8), R32(0x23BC2C)};
    const bool bad = w[0] > 1 || !Ram31(w[1]) || !Ram31(w[2]) || !Ram31(w[3]) || !Ram31(w[4]) || w[5] > 200;
    const bool changed = memcmp(w, C.w, sizeof w) != 0;
    memcpy(C.w, w, sizeof w);
    if (!bad || !changed || C.lines >= 40) return;
    ++C.lines;
    Log("probe31 collision (%s): the walk's index %x, chains %08x %08x, sources %08x %08x, instances %u; me at %.0f %.0f %.0f, dg %lld", who, w[0], w[1], w[2], w[3], w[4],
        w[5], gMe.pos[0], gMe.pos[1], gMe.pos[2], ps2world::DgFrames());
}
void QLog31(const char* tag, const Player* o) {
    char b[900];
    int n = snprintf(b, sizeof b, "probe31 q %s: me %x %x %x 6 %x 7 %x at %.0f %.0f %.0f stun %d weapon %x", tag, gMe.bits[0], gMe.bits[1], gMe.bits[2], gMe.bits[6], gMe.bits[7],
                     gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.stun, Inv().weapon);
    if (o) n += snprintf(b + n, sizeof b - n, " | %s %x %x %x 6 %x at %.0f %.0f %.0f (%.2f m, %.2f m 3D) life %d stun %d", o->name.c_str(), o->bits[0], o->bits[1], o->bits[2],
                         o->bits[6], o->pos[0], o->pos[1], o->pos[2], Dist2D(o->pos, gMe.pos) / 1000.f, Dist3D31(o->pos, gMe.pos) / 1000.f, o->life, o->stun);
    snprintf(b + n, sizeof b - n, " | dg %lld, a datagram %.1f s ago | walk %x %08x %08x %08x %08x, instances %u", ps2world::DgFrames(), ps2world::SecondsSinceRecv(),
             R32(0x2891C0), R32(0x2891C4), R32(0x2891C8), R32(0x2891D4), R32(0x2891D8), R32(0x23BC2C));
    Log("%s", b);
}
// the weapon CqcFrame grabs with: the one in the hands if a CQC weapon (its state +0x15: 0x80), else the Mk22, the
// M1911A1, the knife, empty hands (-1: the inventory not read)
int CqcCode31() {
    const Inventory& v = Inv();
    if (!v.ok) return -1;
    auto cqc = [](int c) {
        if (c == 0) return true;
        const uint32_t st = c > 0 ? WeaponState(c) : 0;
        return st ? (R16(st + 0x14) & 0x8000) != 0 : c >= 1 && c <= 7;
    };
    if (v.weapon >= 0 && cqc(v.weapon)) return v.weapon;
    for (int c : {5, 6, 1}) if (v.rounds[c] >= 0) return c;
    return 0;
}
bool Q31Setup() {
    Q31& Q = gQ31;
    if (Q.init) return Q.have;
    Q.init = true;
    if (const char* m = getenv("MGO1_DOLL_PROBE_QMODE")) for (int k = 0; k < QM_COUNT; ++k) if (!strcmp(m, kQMode31[k])) Q.mode = k;
    if (const char* y = getenv("MGO1_DOLL_PROBE_QY")) Q.qy = (float)atof(y);
    if (const char* n = getenv("MGO1_DOLL_PROBE_QTRIALS")) Q.trials = std::max(1, atoi(n));
    if (const char* s = getenv("MGO1_DOLL_PROBE_QSPOT")) {
        Q.spot = true;
        float v[6];
        if (sscanf(s, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) memcpy(Q.spotPts, v, sizeof v);
    }
    int number = 1;
    if (const char* n = getenv("MGO1_DOLL_PROBE_LADDER")) number = atoi(n);
    const std::vector<Ladder> ls = FindLadders(*gMap->geom);
    const Ladder* pick = nullptr;
    for (const Ladder& l : ls) if (l.number == number) pick = &l;
    if (!pick) { Log("probe31 q: no ladder %03d on this map (%zu ladders)", number, ls.size()); return false; }
    Q.l = *pick;
    const Ladder& l = Q.l;
    const float fa[3] = {l.foot[0], l.foot[1] + l.footHalf[1], l.foot[2]}, ta[3] = {l.top[0], l.top[1] + l.topHalf[1], l.top[2]};
    Q.floorFoot = gMap->nav->GroundBelow(fa, 2 * l.footHalf[1] + 1500.f);
    Q.floorTop = gMap->nav->GroundBelow(ta, 2 * l.topHalf[1] + 1500.f);
    float dx = l.foot[0] - l.top[0], dz = l.foot[2] - l.top[2];
    const float len = std::max(1.f, std::sqrt(dx * dx + dz * dz));
    dx /= len; dz /= len;
    Q.edge[0] = dx; Q.edge[1] = dz;
    const float half = std::fabs(dx) * l.topHalf[0] + std::fabs(dz) * l.topHalf[2];     // (the top's place: half way to its edge side)
    const float w[3] = {l.top[0] + dx * half * 0.5f, Q.floorTop, l.top[2] + dz * half * 0.5f}, f[3] = {l.foot[0], Q.floorFoot, l.foot[2]};
    const float s[3] = {w[0] - dx * 2000.f, Q.floorTop, w[2] - dz * 2000.f};             // (the roll's start: 2 m back)
    float sm[3] = {0, 0, 0};
    const bool sOn = gMap->nav->Nearest(s, 500.f, sm) && Dist2D(sm, s) < 500.f;
    memcpy(Q.wait, w, sizeof w); memcpy(Q.foot, f, sizeof f); memcpy(Q.start, sOn ? sm : s, sizeof s);
    Log("probe31 q: ladder %03d, the foot's floor %.0f, the top's %.0f, up facing %.0f; the top's place %.0f %.0f %.0f, the foot's %.0f %.0f %.0f, the roll's start %.0f %.0f %.0f%s; "
        "mode %s, the victim's hips %.0f over the foot's floor, %d trials%s",
        l.number, Q.floorFoot, Q.floorTop, l.dir / kDeg31, w[0], w[1], w[2], f[0], f[1], f[2], Q.start[0], Q.start[1], Q.start[2], sOn ? "" : " (NOT on the mesh)",
        kQMode31[Q.mode], Q.qy, Q.trials, Q.spot ? ", the spot first" : "");
    Q.have = Q.floorFoot > -1e29f && Q.floorTop > -1e29f;
    return Q.have;
}
// to a floor point (the hips 800 over it): true there (on a ladder out of a path: the stick toward its floor)
bool Go31(const float* p, float r, long long t) {
    if (Dist2D(gMe.pos, p) < r && std::fabs(gMe.pos[1] - 800.f - p[1]) < 700.f) { StopPath(); Steer(0, 0, 0); return true; }
    if (Following()) { FollowPath(); return false; }
    if (OnLadder()) { gPad.sticks[3] = p[1] + 800.f < gMe.pos[1] ? 0xFF : 0x00; return false; }
    if (t % 30 == 0 && PlanTo(p)) { FollowPath(); return false; }
    if (std::fabs(gMe.pos[1] - 800.f - p[1]) < 700.f) Steer(p[0] - gMe.pos[0], p[2] - gMe.pos[2], Dist2D(gMe.pos, p) < 1500.f ? 0.7f : 1.f);
    else Steer(0, 0, 0);
    return false;
}
// facing a direction (short full pushes turn a standing player): true faced (or 2 s gone)
bool Face31(float face, long long t) {
    Steer(0, 0, 0);
    if (std::fabs(Wrap(face - Facing())) > 10.f * kDeg31 && t < 120) { if (gFrame % 6 < 2) Steer(std::sin(face), std::cos(face), 1.f); return false; }
    return true;
}
// the ladder taken as LadderSegment does (into its volume's middle, still facing it, Triangle, the stick): 0 going
// on, 1 off at the other end, -1 not (not taken, off elsewhere, 20 s on it)
int Climb31(bool down) {
    Q31& Q = gQ31;
    const Ladder& l = Q.l;
    const float* box = down ? l.top : l.foot;
    const float face = down ? Wrap(l.dir + 3.14159265f) : l.dir;
    const long long t = gFrame - Q.cat;
    if (OnLadder() && Q.cphase < 3) { Q.cphase = 3; Q.cat = gFrame; Log("probe31 q: on the ladder (%s) at %.0f %.0f %.0f", down ? "down" : "up", gMe.pos[0], gMe.pos[1], gMe.pos[2]); }
    switch (Q.cphase) {
    case 0:
        if (Dist2D(gMe.pos, box) > 300.f) {
            if (t > 300) { Log("probe31 q: the ladder's volume not reached"); return -1; }
            Steer(box[0] - gMe.pos[0], box[2] - gMe.pos[2], Dist2D(gMe.pos, box) < 900.f ? 0.7f : 1.f);
            return 0;
        }
        Steer(0, 0, 0); Q.cphase = 1; Q.cat = gFrame; Q.cStill = -1;
        return 0;
    case 1:
        Steer(0, 0, 0);
        if (std::fabs(Wrap(face - Facing())) > 12.f * kDeg31 && t < 90) { if (gFrame % 6 < 2) Steer(std::sin(face), std::cos(face), 1.f); Q.cStill = -1; return 0; }
        if (Q.cStill < 0) Q.cStill = gFrame;
        if (gFrame - Q.cStill < 10) return 0;
        gPad.buttons |= 0x1000;
        Q.cphase = 2; Q.cat = gFrame;
        return 0;
    case 2:
        Steer(0, 0, 0);
        if (t < 4) gPad.buttons |= 0x1000;
        if (t > 60) {
            if (++Q.ctries >= 3) { Log("probe31 q: the ladder not taken"); Q.cphase = 0; Q.ctries = 0; return -1; }
            Q.cphase = 0; Q.cat = gFrame;
        }
        return 0;
    default:
        if (OnLadder()) {
            Q.cOff = -1;
            if (t > 1200) { Log("probe31 q: 20 s on the ladder (hips %.0f, state %x %x)", gMe.pos[1], gMe.bits[0], gMe.bits[2]); Q.cphase = 0; Q.ctries = 0; return -1; }
            gPad.sticks[3] = down ? 0xFF : 0x00;
            return 0;
        }
        if (Q.cOff < 0) Q.cOff = gFrame;
        Steer(0, 0, 0);
        if (gFrame - Q.cOff < 10) return 0;
        Q.cphase = 0; Q.ctries = 0; Q.cOff = -1;
        return std::fabs(gMe.pos[1] - 800.f - (down ? Q.floorFoot : Q.floorTop)) < 900.f ? 1 : -1;
    }
}
void QWindowStart31(const Player* o) {
    Q31& Q = gQ31;
    Q.winFrom = gFrame; Q.dg0 = ps2world::DgFrames(); Q.maxGap = 0.0; Q.moved[0] = Q.moved[1] = 0.f;
    memcpy(Q.last[0], gMe.pos, sizeof Q.last[0]);
    if (o) memcpy(Q.last[1], o->pos, sizeof Q.last[1]);
    QLog31("+0", o);
}
// a frame of the 30 s window: true while it runs
bool QWindow31(const Player* o, const char* who) {
    Q31& Q = gQ31;
    if (Q.winFrom < 0) return false;
    const long long t = gFrame - Q.winFrom;
    Q.maxGap = std::max(Q.maxGap, ps2world::SecondsSinceRecv());
    Q.moved[0] += Dist3D31(Q.last[0], gMe.pos); memcpy(Q.last[0], gMe.pos, sizeof Q.last[0]);
    if (o) { Q.moved[1] += Dist3D31(Q.last[1], o->pos); memcpy(Q.last[1], o->pos, sizeof Q.last[1]); }
    if (t > 0 && t % 30 == 0) { char tag[24]; snprintf(tag, sizeof tag, "+%lld", t); QLog31(tag, o); }
    if (t < 1800) return true;
    const long long dg = ps2world::DgFrames() - Q.dg0;
    Log("probe31: RESULT q %d (%s, %s): in 30 s it moved %.2f m, %s %.2f m; the game's frames +%lld; a datagram at most %.1f s apart: %s", Q.trial, kQMode31[Q.mode], who,
        Q.moved[0] / 1000.f, o ? o->name.c_str() : "the other", Q.moved[1] / 1000.f, dg, Q.maxGap, dg < 300 ? "ITS GAME STOPPED" : Q.maxGap > 5.0 ? "THE HOST SILENT" : "running");
    Q.winFrom = -1;
    return false;
}
// the actor's q
void StepQ31(const Player* o) {
    Q31& Q = gQ31;
    if (!Q31Setup()) { Probe31Next(); return; }
    if (Died31(Q.seen) && Q.phase != 0) { Log("probe31 q %d: a new life, to its place again", Q.trial); Q.phase = 0; Q.at = gFrame; Q.cphase = 0; Q.winFrom = -1; StopPath(); }
    if (Q.trial >= Q.trials) { Log("probe31: q done (%d trials)", Q.trials); Probe31Next(); return; }
    const bool cross = Q.mode == QM_CROSS || Q.mode == QM_CROSSNONE;
    const long long t = gFrame - Q.at;
    auto go = [&](int ph) { Q.phase = ph; Q.at = gFrame; };
    switch (Q.phase) {
    case 0: {                                            // to its place: the top's (the foot's for cross, 2 m back for roll)
        const float* s = cross ? Q.foot : Q.mode == QM_ROLL ? Q.start : Q.wait;
        if (t == 0) Log("probe31 q %d: the actor to %.0f %.0f %.0f (%s)", Q.trial, s[0], s[1], s[2], kQMode31[Q.mode]);
        if (Go31(s, 350.f, t)) { go(1); return; }
        if (t > 7200) { Log("probe31 q %d: the actor's place not reached (at %.0f %.0f %.0f)", Q.trial, gMe.pos[0], gMe.pos[1], gMe.pos[2]); ++Q.trial; go(0); }
        return;
    }
    case 1:                                              // facing the ladder's edge (up the ladder at the foot)
        if (!Face31(cross ? Q.l.dir : std::atan2(Q.edge[0], Q.edge[1]), t)) return;
        QLog31("the actor in place", o);
        go(2);
        return;
    case 2: {                                            // a CQC weapon in the hands; waiting for the victim on the ladder
        Steer(0, 0, 0);
        const int want = Q.mode == QM_CIRCLE || Q.mode == QM_STICK || Q.mode == QM_CROSS ? CqcCode31() : -1;
        if (want >= 0 && Inv().weapon != want && t < 600) {
            if (t == 0) Log("probe31 q %d: the weapon %x to %x (CQC)", Q.trial, Inv().weapon, want);
            if (ProbeWindowTo(0, want)) return;
        }
        if (t % 300 == 0) QLog31("waiting", o);
        const bool onIt = o && (o->bits[2] & 0x40000) != 0;
        if (cross) {
            if (onIt) { Log("probe31 q %d: the victim on the ladder, the actor up it", Q.trial); Q.cphase = 0; Q.cat = gFrame; go(5); }
        } else if (onIt && Dist2D(o->pos, gMe.pos) <= (Q.mode == QM_ROLL ? 3500.f : 1500.f) && o->pos[1] - Q.floorFoot >= Q.qy) {
            QLog31("ACTION", o);
            Log("probe31 q %d: %s at %s on the ladder, its hips %.0f over the foot's floor", Q.trial, kQMode31[Q.mode], o->name.c_str(), o->pos[1] - Q.floorFoot);
            go(3);
            return;
        }
        if (t > 10800) { Log("probe31 q %d: the victim did not come", Q.trial); ++Q.trial; go(0); }
        return;
    }
    case 3: {                                            // the action
        const int m = Q.mode == QM_CROSS ? QM_CIRCLE : Q.mode;
        if (m == QM_NONE || m == QM_CROSSNONE) { QWindowStart31(o); go(4); return; }
        if (m == QM_ROLL) {                              // run at it, Cross 1.8 m before it (or after 1.5 s of the run)
            const bool running = (gMe.bits[0] & 0xFFFF) == 0x5002;
            if (o && (Q.rollAt < 0 || gFrame - Q.rollAt < 30)) Steer(o->pos[0] - gMe.pos[0], o->pos[2] - gMe.pos[2], 1.f); else Steer(0, 0, 0);
            if (Q.rollAt < 0 && o && ((running && Dist2D(o->pos, gMe.pos) <= 1800.f) || t > 90)) {
                Q.rollAt = gFrame;
                Log("probe31 q %d: Cross at %.2f m (state %x)", Q.trial, Dist2D(o->pos, gMe.pos) / 1000.f, gMe.bits[0]);
            }
            if (Q.rollAt >= 0 && gFrame - Q.rollAt < 4) gPad.buttons |= 0x4000;
            if (t % 10 == 0) QLog31("roll", o);
            if ((Q.rollAt >= 0 && gFrame - Q.rollAt >= 80) || t > 300) { Q.rollAt = -1; QWindowStart31(o); go(4); }
            return;
        }
        gPad.buttons |= 0x2000;                          // Circle held 40 frames (the stick toward it the first 8: stick)
        if (m == QM_STICK && t < 8 && o) Steer(o->pos[0] - gMe.pos[0], o->pos[2] - gMe.pos[2], 1.f); else Steer(0, 0, 0);
        if (t % 5 == 0) QLog31("Circle", o);
        if (t >= 40) { QWindowStart31(o); go(4); }
        return;
    }
    case 4: {                                            // the window; the stick pushed away from the edge 20 frames at +10 s and +20 s
        const long long w = gFrame - Q.winFrom;
        Steer(0, 0, 0);
        if (w == 600 || w == 1200) memcpy(Q.pushFrom, gMe.pos, sizeof Q.pushFrom);
        if ((w >= 600 && w < 620) || (w >= 1200 && w < 1220)) Steer(-Q.edge[0], -Q.edge[1], 0.7f);
        if (w == 650 || w == 1250) Log("probe31 q %d: the stick pushed 20 frames: moved %.2f m (state %x)", Q.trial, Dist2D(Q.pushFrom, gMe.pos) / 1000.f, gMe.bits[0]);
        if (QWindow31(o, "the actor")) return;
        ++Q.trial; go(0);
        return;
    }
    case 5: {                                            // (cross) up the ladder while the victim comes down
        const int r = Climb31(false);
        if (r == 0) return;
        Log("probe31 q %d: the actor %s (hips %.0f)", Q.trial, r > 0 ? "at the top" : "NOT up the ladder", gMe.pos[1]);
        if (r < 0) { QWindowStart31(o); go(4); return; }
        go(6);
        return;
    }
    default:                                             // (cross) at the top: Circle at the victim on the ladder within 1.5 m
        Steer(0, 0, 0);
        if (Q.mode == QM_CROSSNONE) { QWindowStart31(o); go(4); return; }
        if (o && (o->bits[2] & 0x40000) && Dist2D(o->pos, gMe.pos) <= 1500.f) {
            QLog31("ACTION", o);
            Log("probe31 q %d: Circle at %s on the ladder below, its hips %.0f over the foot's floor", Q.trial, o->name.c_str(), o->pos[1] - Q.floorFoot);
            go(3);
            return;
        }
        if (t > 180) { Log("probe31 q %d: the victim not on the ladder within 1.5 m: no Circle", Q.trial); QWindowStart31(o); go(4); }
        return;
    }
}
// (v150-host beta 24, snakehouse3) a point "x,y,z" of an environment variable
bool EnvPoint31(const char* name, float* p) { const char* e = getenv(name); return e && sscanf(e, "%f,%f,%f", &p[0], &p[1], &p[2]) == 3; }
// (v150-host beta 24, snakehouse3) the doorway step's victim (docs/VIRTUAL_DOLL.md 43): MGO1_DOLL_PROBE_D_VIA ("x,y,z", a floor
// point its mesh reaches) then straight on to MGO1_DOLL_PROBE_D_PLACE (beyond a doorway's panel, which its mesh holds as a wall:
// walked as a player walks), once the other player's hips are under MGO1_DOLL_PROBE_D_WHEN (he is in the house: the passage
// free); there still, facing him. Each life again. True while it goes there
bool VictimPlace31(const Player* o) {
    static int have = -1; static float via[3], at[3], when = 1e30f; static int phase = 0; static long long since = 0, life = -1;
    if (have < 0) { have = EnvPoint31("MGO1_DOLL_PROBE_D_VIA", via) && EnvPoint31("MGO1_DOLL_PROBE_D_PLACE", at);
                    if (getenv("MGO1_DOLL_PROBE_D_WHEN")) when = (float)atof(getenv("MGO1_DOLL_PROBE_D_WHEN")); }
    if (!have) return false;
    Probe31& P = gP31;
    if (P.newLife || life < 0) { phase = 0; since = gFrame; life = gFrame; }
    const long long t = gFrame - since;
    if (phase == 0) {                                    // the other player in the house first
        Steer(0, 0, 0);
        if (o && o->pos[1] < when) { phase = 1; since = gFrame; Log("probe31: doorway victim: %s in (hips %.0f), to its place", o->name.c_str(), o->pos[1]); }
        return true;
    }
    if (phase == 1) {
        if (Go31(via, 600.f, t)) { phase = 2; since = gFrame; Log("probe31: doorway victim: at the way's point %.0f %.0f %.0f", gMe.pos[0], gMe.pos[1], gMe.pos[2]); }
        else if (t > 3600) { phase = 2; since = gFrame; Log("probe31: doorway victim: the way's point not reached (at %.0f %.0f %.0f)", gMe.pos[0], gMe.pos[1], gMe.pos[2]); }
        return true;
    }
    // ((v150-host beta 25, wakeramp) MGO1_DOLL_PROBE_D_AGAIN=1: back to its place each time it is up 3 s away from it (a fall on stairs
    // slid it down them): the next knock-out at the same place)
    static const bool again = getenv("MGO1_DOLL_PROBE_D_AGAIN") != nullptr;
    static long long awayAt = -1;
    if (phase == 3 && again) {
        const bool up = gMe.alive && !(gMe.bits[0] & 0x1E000000u) && !((gMe.bits[4] >> 25) & 1) && !Low();
        if (!up || Dist2D(gMe.pos, at) < 600.f) awayAt = -1;
        else if (awayAt < 0) awayAt = gFrame;
        else if (gFrame - awayAt > 180) { phase = 2; since = gFrame; awayAt = -1; Log("probe31: doorway victim: %.2f m from its place: back to it", Dist2D(gMe.pos, at) / 1000.f); return true; }
    }
    if (phase == 2) {
        StopPath();
        const float d = Dist2D(gMe.pos, at);
        if (d < 250.f || t > 1200) { phase = 3; Steer(0, 0, 0); Log("probe31: doorway victim: %s %.0f %.0f %.0f (%.0f mm from its place)", d < 250.f ? "at its place" : "its place NOT reached, at",
                                                gMe.pos[0], gMe.pos[1], gMe.pos[2], d); return true; }
        Steer(at[0] - gMe.pos[0], at[2] - gMe.pos[2], 0.75f);
        return true;
    }
    return false;                                        // (there: still, facing him, as every victim)
}
// the victim's q (its steps hold q): true while it has the pad
bool VictimQ31(const Player* o) {
    Q31& Q = gQ31;
    if (gP31.steps.find('q') == std::string::npos || !Q31Setup()) return false;
    const bool cross = Q.mode == QM_CROSS || Q.mode == QM_CROSSNONE;
    if (Died31(Q.seen) && (Q.phase != 0 || Q.winFrom >= 0)) {
        Log("probe31 q %d: the victim's new life, to its place again", Q.trial);
        Q.phase = 0; Q.leg = 0; Q.at = gFrame; Q.cphase = 0; Q.winFrom = -1; StopPath();
    }
    if (Q.winFrom >= 0) {                                // its window (from its taking the ladder): the stick still pushed on it
        if (OnLadder()) gPad.sticks[3] = cross ? 0xFF : 0x00; else Steer(0, 0, 0);
        if (QWindow31(o, "the victim")) return true;
        ++Q.trial; Q.phase = 0; Q.leg = 0; Q.cphase = 0; Q.at = gFrame;
        return true;
    }
    if (Q.trial >= Q.trials) return false;               // (done: the victim's frames as before)
    if (gRule == 3 && !RadioPressing() && ProbeTakeObjective()) {   // (capture: the KEROTAN held first)
        if (gFrame % 120 == 0) Log("probe31 q: the victim going for the objective:%s", ProbeObjectives().c_str());
        Q.at = gFrame;
        return true;
    }
    const long long t = gFrame - Q.at;
    auto go = [&](int ph) { Q.phase = ph; Q.at = gFrame; };
    switch (Q.phase) {
    case 0:                                              // (the spot: through its two points first)
        if (!Q.spot || Q.leg >= 2) { go(1); return true; }
        if (Go31(Q.spotPts[Q.leg], 700.f, t)) {
            Log("probe31 q %d: the victim at the spot's point %d (%.0f %.0f %.0f), dg %lld; objectives:%s", Q.trial, Q.leg, gMe.pos[0], gMe.pos[1], gMe.pos[2],
                ps2world::DgFrames(), ProbeObjectives().c_str());
            ++Q.leg; Q.at = gFrame;
        } else if (t > 5400) { Log("probe31 q %d: the spot's point %d not reached", Q.trial, Q.leg); ++Q.leg; Q.at = gFrame; }
        return true;
    case 1: {                                            // to its place: the foot (the top for cross)
        const float* s = cross ? Q.wait : Q.foot;
        if (t == 0) Log("probe31 q %d: the victim to %.0f %.0f %.0f; objectives:%s", Q.trial, s[0], s[1], s[2], ProbeObjectives().c_str());
        if (Go31(s, 350.f, t)) { Q.thereSince = -1; go(2); return true; }
        if (t > 7200) { Log("probe31 q %d: the victim's place not reached (at %.0f %.0f %.0f)", Q.trial, gMe.pos[0], gMe.pos[1], gMe.pos[2]); ++Q.trial; Q.leg = 0; go(0); }
        return true;
    }
    case 2: {                                            // facing the ladder, the actor at its place a second
        Face31(cross ? Wrap(Q.l.dir + 3.14159265f) : Q.l.dir, t);
        const float* other = cross ? Q.foot : Q.mode == QM_ROLL ? Q.start : Q.wait;
        const bool there = o && Dist2D(o->pos, other) < 1000.f && std::fabs(o->pos[1] - 800.f - other[1]) < 600.f;
        if (t % 300 == 0) QLog31("the victim waiting", o);
        if (!there) Q.thereSince = -1; else if (Q.thereSince < 0) Q.thereSince = gFrame;
        if (Q.thereSince >= 0 && gFrame - Q.thereSince >= 60 && t > 120) { Q.cphase = 0; Q.cat = gFrame; go(3); return true; }
        if (t > 10800) { Log("probe31 q %d: the actor did not come", Q.trial); ++Q.trial; Q.leg = 0; go(0); }
        return true;
    }
    default: {                                           // the climb; its window from the moment it is on the ladder
        const int r = Climb31(cross);
        if (OnLadder()) { QWindowStart31(o); return true; }
        if (r == 0) return true;
        Log("probe31 q %d: the victim %s (hips %.0f)", Q.trial, r > 0 ? "off at the other end" : "NOT on the ladder", gMe.pos[1]);
        QWindowStart31(o);
        return true;
    }
    }
}

// ---- the map's own faces (all of them: the 0x2000000 layer the mesh leaves out too, doll_nav.cpp MakeSoup) ----
// the faces whose box meets [lo, hi]
void FacesIn31(const float* lo, const float* hi, std::vector<const Face*>& out) {
    out.clear();
    for (const Face& f : gMap->geom->faces) {
        float a[3] = {1e30f, 1e30f, 1e30f}, b[3] = {-1e30f, -1e30f, -1e30f};
        for (int k = 0; k < 4; ++k) for (int i = 0; i < 3; ++i) { a[i] = std::min(a[i], f.v[k][i]); b[i] = std::max(b[i], f.v[k][i]); }
        if (b[0] < lo[0] || a[0] > hi[0] || b[1] < lo[1] || a[1] > hi[1] || b[2] < lo[2] || a[2] > hi[2]) continue;
        out.push_back(&f);
    }
}
// a segment p-q against a triangle: its fraction along p-q
bool Tri31(const float* p, const float* q, const float* a, const float* b, const float* c, float* t) {
    float e1[3], e2[3], d[3], h[3], s[3], r[3];
    for (int k = 0; k < 3; ++k) { e1[k] = b[k] - a[k]; e2[k] = c[k] - a[k]; d[k] = q[k] - p[k]; s[k] = p[k] - a[k]; }
    h[0] = d[1] * e2[2] - d[2] * e2[1]; h[1] = d[2] * e2[0] - d[0] * e2[2]; h[2] = d[0] * e2[1] - d[1] * e2[0];
    const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if (std::fabs(det) < 1e-6f) return false;
    const float u = (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]) / det;
    if (u < 0.f || u > 1.f) return false;
    r[0] = s[1] * e1[2] - s[2] * e1[1]; r[1] = s[2] * e1[0] - s[0] * e1[2]; r[2] = s[0] * e1[1] - s[1] * e1[0];
    const float v = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / det;
    if (v < 0.f || u + v > 1.f) return false;
    *t = (e2[0] * r[0] + e2[1] * r[1] + e2[2] * r[2]) / det;
    return *t >= 0.f && *t <= 1.f;
}
// the first face of fs between p and q (solid: the mesh's faces only, the walls of the 0x2000000 layer left out)
const Face* Hit31(const float* p, const float* q, const std::vector<const Face*>& fs, bool solid, float* t) {
    const Face* best = nullptr;
    float bt = 2.f;
    for (const Face* f : fs) {
        if (solid && (f->attr & 0x2000000u) && f->n[1] < 0.7f) continue;
        float u = 0.f;
        if ((Tri31(p, q, f->v[0], f->v[1], f->v[2], &u) || Tri31(p, q, f->v[0], f->v[2], f->v[3], &u)) && u < bt) { bt = u; best = f; }
    }
    if (best) *t = bt;
    return best;
}
// the walls and slopes of fs (no floor): heights, along u from o, aside, normal, attribute (most lines at most)
void FaceList31(const char* tag, const std::vector<const Face*>& fs, const float* o, const float* u, int most) {
    int n = 0;
    for (const Face* f : fs) {
        if (f->n[1] >= 0.7f) continue;
        float y0 = 1e30f, y1 = -1e30f, a0 = 1e30f, a1 = -1e30f, s0 = 1e30f, s1 = -1e30f;
        for (int k = 0; k < 4; ++k) {
            const float dx = f->v[k][0] - o[0], dz = f->v[k][2] - o[2], al = dx * u[0] + dz * u[1], as = dx * u[1] - dz * u[0];
            y0 = std::min(y0, f->v[k][1]); y1 = std::max(y1, f->v[k][1]); a0 = std::min(a0, al); a1 = std::max(a1, al); s0 = std::min(s0, as); s1 = std::max(s1, as);
        }
        if (n++ >= most) { Log("probe31 %s face: more left out", tag); return; }
        Log("probe31 %s face: y %.0f..%.0f, along %.0f..%.0f, aside %.0f..%.0f, normal %.2f %.2f %.2f, attribute %08x%s%s", tag, y0, y1, a0, a1, s0, s1, f->n[0], f->n[1],
            f->n[2], f->attr, (f->attr & 0x2000000u) ? " (the layer the mesh leaves out)" : "", f->object ? " (an object's)" : "");
    }
}

// ---- v137, h: off a floor's edge by a ladder's top until hanging from it (MGO1_DOLL_PROBE_HANGAT "x,z", 14700,-24970:
// Graniny Gorki Lab's ladder 001, the edge at x 14.5 m), the ways in turn until one hangs (then that one again);
// hanging, the inputs in turn, 1.5 s each (hang k from the k-th), until it ends: up on the floor or fallen, told a
// second later. MGO1_DOLL_PROBE_HTRIALS (3) ----
enum { HA_TRIANGLE, HA_TOWARD, HA_AWAY, HA_CROSS, HA_CIRCLE, HA_COUNT };
const char* const kHangAct31[HA_COUNT] = {"Triangle taps", "the stick toward the wall", "the stick away", "Cross taps", "Circle taps"};
const char* const kHangWay31[] = {"walking off at 0.7 (the brain's step)", "walking off at 0.6", "backward, readied, at 0.64", "running off"};
const int kHangWays31 = 4;
struct H31 {
    bool init = false, have = false;
    int phase = 0, way = 0, trial = 0, trials = 3, first = 0, acts = 0, endedBy = -1, onRun = 0;
    float edgeP[3] = {0, 0, 0}, start[3] = {0, 0, 0}, dir[2] = {1.f, 0.f}, lo = 0.f, hi = 0.f, floorTop = 0.f, floorBelow = 0.f;
    long long at = 0, actAt = 0, since = -1, offAt = -1, endAt = -1, endedInto = 0, seen = 0;
};
H31 gH31;
bool H31Setup() {
    H31& H = gH31;
    if (H.init) return H.have;
    H.init = true;
    float hx = 14700.f, hz = -24970.f;
    if (const char* e = getenv("MGO1_DOLL_PROBE_HANGAT")) sscanf(e, "%f,%f", &hx, &hz);
    if (const char* n = getenv("MGO1_DOLL_PROBE_HTRIALS")) H.trials = std::max(1, atoi(n));
    const std::vector<Ladder> ls = FindLadders(*gMap->geom);
    const float hp[3] = {hx, 0.f, hz};
    const Ladder* best = nullptr;
    float bd = 1e30f;
    for (const Ladder& l : ls) if (Dist2D(l.top, hp) < bd) { bd = Dist2D(l.top, hp); best = &l; }
    if (!best || bd > 6000.f) { Log("probe31 h: no ladder's top within 6 m of %.0f %.0f", hx, hz); return false; }
    float dx = best->foot[0] - best->top[0], dz = best->foot[2] - best->top[2];
    const float len = std::max(1.f, std::sqrt(dx * dx + dz * dz));
    dx /= len; dz /= len;
    H.dir[0] = dx; H.dir[1] = dz;
    const float ta[3] = {best->top[0], best->top[1] + best->topHalf[1], best->top[2]};
    H.floorTop = gMap->nav->GroundBelow(ta, 2 * best->topHalf[1] + 1500.f);
    if (H.floorTop < -1e29f) { Log("probe31 h: no floor at ladder %03d's top", best->number); return false; }
    // the floor on the line (1.5 m back from the point); the edge: its last point toward the point
    auto ground = [&](float s) { const float p[3] = {hx + dx * s, H.floorTop + 300.f, hz + dz * s}; return gMap->nav->GroundBelow(p, 15000.f); };
    const float back = ground(-1500.f);
    if (back > -1e29f && std::fabs(back - H.floorTop) < 1500.f) H.floorTop = back;
    auto onTop = [&](float s) { return std::fabs(ground(s) - H.floorTop) < 300.f; };
    float edge = 0.f;
    bool found = false;
    if (onTop(0.f)) { for (float s = 25.f; s <= 2500.f && !found; s += 25.f) if (!onTop(s)) { edge = s - 25.f; found = true; } }
    else { for (float s = -25.f; s >= -2500.f && !found; s -= 25.f) if (onTop(s)) { edge = s; found = true; } }
    std::string prof;
    for (float s = -2000.f; s <= 1500.f; s += 250.f) { char b[32]; snprintf(b, sizeof b, " %.0f:%.0f", s, ground(s)); prof += b; }
    Log("probe31 h: ladder %03d's top, the floor %.0f, off it toward %.0f deg; the mesh's ground along the line through %.0f %.0f (mm along: height):%s", best->number,
        H.floorTop, std::atan2(dx, dz) / kDeg31, hx, hz, prof.c_str());
    if (!found) { Log("probe31 h: no edge on that line"); return false; }
    H.edgeP[0] = hx + dx * edge; H.edgeP[1] = H.floorTop; H.edgeP[2] = hz + dz * edge;
    H.floorBelow = ground(edge + 400.f);
    const float s[3] = {H.edgeP[0] - dx * 1200.f, H.floorTop, H.edgeP[2] - dz * 1200.f};
    float q[3] = {0, 0, 0};
    if (!gMap->nav->Nearest(s, 500.f, q) || Dist2D(q, s) > 500.f) { Log("probe31 h: the start %.0f %.0f %.0f not on the mesh", s[0], s[1], s[2]); return false; }
    memcpy(H.start, q, sizeof q);
    Log("probe31 h: the edge at %.0f %.0f %.0f (the ground below %.0f), the start %.0f %.0f %.0f; the map's walls by it:", H.edgeP[0], H.edgeP[1], H.edgeP[2], H.floorBelow,
        H.start[0], H.start[1], H.start[2]);
    const float lo[3] = {H.edgeP[0] - 1500.f, H.floorTop - 2000.f, H.edgeP[2] - 1500.f}, hi[3] = {H.edgeP[0] + 1500.f, H.floorTop + 3000.f, H.edgeP[2] + 1500.f};
    std::vector<const Face*> fs;
    FacesIn31(lo, hi, fs);
    FaceList31("h", fs, H.edgeP, H.dir, 30);
    H.have = true;
    return true;
}
void HLog31(const char* tag) {
    Log("probe31 h %s: at %.0f %.0f %.0f (the hips %.0f over the top's floor) state %x %x %x 6 %x 7 %x 9 %x stamina %d facing %.0f", tag, gMe.pos[0], gMe.pos[1], gMe.pos[2],
        gMe.pos[1] - gH31.floorTop, gMe.bits[0], gMe.bits[1], gMe.bits[2], gMe.bits[6], gMe.bits[7], gMe.bits[9], gMe.stun, Facing() / kDeg31);
}
void StepH31() {
    H31& H = gH31;
    if (!H31Setup()) { Probe31Next(); return; }
    if (H.trial >= H.trials || H.way >= kHangWays31) { Log("probe31: h done (%d hangs)", H.trial); Probe31Next(); return; }
    const long long t = gFrame - H.at;
    const bool hanging = (gMe.bits[2] & 0x10000000) != 0 && !OnLadder();
    auto go = [&](int ph) { H.phase = ph; H.at = gFrame; };
    if (Died31(H.seen) && H.phase != 0) {
        if (H.phase >= 3) { Log("probe31: RESULT hang %d (got %s): DIED (ended by %s %.1f s into it)", H.trial, kHangWay31[H.way], H.endedBy >= 0 ? kHangAct31[H.endedBy] : "nothing", H.endedInto / 60.0); ++H.trial; }
        else Log("probe31 h: a new life");
        StopPath(); go(0);
        return;
    }
    const float ax = std::atan2(H.dir[0], H.dir[1]);
    switch (H.phase) {
    case 0:                                              // to the start (from below: the ladder)
        if (Go31(H.start, 250.f, t)) { go(1); return; }
        if (t > 7200) { Log("probe31 h: the start not reached (at %.0f %.0f %.0f)", gMe.pos[0], gMe.pos[1], gMe.pos[2]); H.way = kHangWays31; }
        return;
    case 1:                                              // facing the edge (away from it: backward)
        if (!Face31(H.way == 2 ? Wrap(ax + 3.14159265f) : ax, t)) return;
        { char tag[96]; snprintf(tag, sizeof tag, "%d: %s, from", H.trial, kHangWay31[H.way]); HLog31(tag); }
        H.onRun = 0; H.endedBy = -1; H.endedInto = 0;
        go(2);
        return;
    case 2: {                                            // off the edge
        const float k = H.way == 0 ? 0.7f : H.way == 1 ? 0.6f : H.way == 2 ? 0.64f : 1.f;
        if (H.way == 2) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
        Steer(H.dir[0], H.dir[1], k);
        if (t % 10 == 0) { char tag[48]; snprintf(tag, sizeof tag, "%d +%lld", H.trial, t); HLog31(tag); }
        H.onRun = hanging ? H.onRun + 1 : 0;
        if (H.onRun >= 10) {
            H.since = gFrame - H.onRun; H.first = H.trial % HA_COUNT; H.acts = 0; H.actAt = gFrame; H.offAt = -1; H.endedBy = -1;
            { char tag[96]; snprintf(tag, sizeof tag, "%d: HANGING (%s, %lld frames)", H.trial, kHangWay31[H.way], t); HLog31(tag); }
            Log("probe31 h %d: the inputs from %s on, 1.5 s each", H.trial, kHangAct31[H.first]);
            go(3);
            return;
        }
        if (!hanging && gMe.pos[1] < H.floorTop - 1800.f) { Log("probe31: RESULT hang way %d (%s): FELL, no hang (hips %.0f)", H.way, kHangWay31[H.way], gMe.pos[1]); ++H.way; go(0); return; }
        if (t > 300) { HLog31("neither hanging nor fallen in 5 s"); Log("probe31: RESULT hang way %d (%s): NOT OFF THE EDGE", H.way, kHangWay31[H.way]); ++H.way; go(0); }
        return;
    }
    case 3: {                                            // hanging: the inputs in turn (a moment off: the input goes on)
        if ((gFrame - H.since) % 30 == 0) { char tag[48]; snprintf(tag, sizeof tag, "%d hanging +%lld", H.trial, gFrame - H.since); HLog31(tag); }
        const int act = (H.first + H.acts) % HA_COUNT;
        const long long ta = gFrame - H.actAt;
        if (!hanging && H.offAt < 0) { H.offAt = gFrame; H.endedBy = act; H.endedInto = ta; }
        if (hanging) H.offAt = -1;
        if (H.offAt >= 0 && gFrame - H.offAt >= 15) { H.endAt = H.offAt; H.lo = H.hi = gMe.pos[1]; Steer(0, 0, 0); go(4); return; }
        if (ta >= 90 && H.offAt < 0) {
            Log("probe31 h %d: %s did not end it in 1.5 s", H.trial, kHangAct31[act]);
            H.actAt = gFrame;
            if (++H.acts >= HA_COUNT) { Log("probe31 h %d: no input ended it (%.1f s hanging, stamina %d): left alone", H.trial, (gFrame - H.since) / 60.0, gMe.stun); go(5); }
            return;
        }
        switch (act) {
        case HA_TRIANGLE: if (ta % 15 < 4) gPad.buttons |= 0x1000; break;
        case HA_TOWARD: if (ta < 45) gPad.sticks[3] = 0x00; else Steer(-H.dir[0], -H.dir[1], 1.f); break;   // (the stick's up, then the wall's way)
        case HA_AWAY: if (ta < 45) gPad.sticks[3] = 0xFF; else Steer(H.dir[0], H.dir[1], 1.f); break;
        case HA_CROSS: if (ta % 15 < 4) gPad.buttons |= 0x4000; break;
        default: if (ta % 15 < 4) gPad.buttons |= 0x2000; break;
        }
        return;
    }
    case 4: {                                            // over: told a second later
        Steer(0, 0, 0);
        H.lo = std::min(H.lo, gMe.pos[1]); H.hi = std::max(H.hi, gMe.pos[1]);
        if (t < 60) return;
        const bool up = std::fabs(gMe.pos[1] - 800.f - H.floorTop) < 500.f, down = std::fabs(gMe.pos[1] - 800.f - H.floorBelow) < 700.f;
        Log("probe31: RESULT hang %d (got %s): ended by %s %.1f s into it (%.1f s hanging): %s, the hips %.0f (the top's floor %.0f, the ground below %.0f; %.0f to %.0f "
            "meanwhile), state %x %x %x%s",
            H.trial, kHangWay31[H.way], H.endedBy >= 0 ? kHangAct31[H.endedBy] : "nothing (left alone)", H.endedInto / 60.0, (H.endAt - H.since) / 60.0,
            up ? "UP ON THE FLOOR" : down ? "FELL" : "ELSEWHERE", gMe.pos[1], H.floorTop, H.floorBelow, H.lo, H.hi, gMe.bits[0], gMe.bits[1], gMe.bits[2],
            OnLadder() ? ", on the ladder" : "");
        ++H.trial;
        go(0);
        return;
    }
    default:                                             // every input tried: nothing pressed until it ends (60 s at most)
        Steer(0, 0, 0);
        if (t % 300 == 0) { char tag[48]; snprintf(tag, sizeof tag, "%d left alone +%lld", H.trial, t); HLog31(tag); }
        if (!hanging) { H.endAt = gFrame; H.endedBy = -1; H.endedInto = 0; H.lo = H.hi = gMe.pos[1]; go(4); return; }
        if (t > 3600) { Log("probe31: RESULT hang %d: still hanging 60 s after the inputs", H.trial); H.trial = H.trials; }
        return;
    }
}

// ---- v137, v: a vault (MGO1_DOLL_PROBE_VAULT "ax,ay,az,bx,by,bz": Mountaintop's, 120431,73179,-202300 to
// 120431,72900,-200300): its profile (the mesh's FirstHit, Clear and GroundBelow; the map's own faces), then the ways
// in turn from its near side ----
const char* const kVaultWay31[] = {"the crossing code (ForceCrossing)", "walking into it, Triangle every quarter second", "running into it, Triangle every quarter second",
                                   "running from 1 m farther, Triangle every quarter second", "a roll, Cross 0.9 m before its face",
                                   "pressed against it, still, Triangle held 60 frames then tapped", "running, Triangle held"};
const int kVaultWays31 = 7;
struct V31 {
    bool init = false, have = false, crossed = false;
    int phase = 0, trial = 0;
    float a[3] = {120431.f, 73179.f, -202300.f}, b[3] = {120431.f, 72900.f, -200300.f}, u[2] = {0.f, 1.f}, len = 2000.f;
    float nearFloor = 0.f, farFloor = 0.f, face = 375.f, start[3] = {0, 0, 0}, start2[3] = {0, 0, 0};
    float maxAlong = -1e30f, maxHips = -1e30f, lastAlong = -1e30f;
    long long at = 0, pressAt = -1, stillAt = -1, bestAt = 0, seen = 0;
    std::string states;
    std::vector<const Face*> faces;                      // (the map's faces about the crossing)
};
V31 gV31;
// along the crossing (0 at A), aside
void VAlong31(float* along, float* aside) {
    const V31& V = gV31;
    *along = (gMe.pos[0] - V.a[0]) * V.u[0] + (gMe.pos[2] - V.a[2]) * V.u[1];
    *aside = (gMe.pos[0] - V.a[0]) * V.u[1] - (gMe.pos[2] - V.a[2]) * V.u[0];
}
void VProfile31() {
    V31& V = gV31;
    const float ux = V.u[0], uz = V.u[1], sx = uz, sz = -ux;
    float lo[3] = {1e30f, std::min(V.nearFloor, V.farFloor) - 500.f, 1e30f}, hi[3] = {-1e30f, std::max(V.nearFloor, V.farFloor) + 4000.f, -1e30f};
    for (int e = 0; e < 2; ++e)
        for (float s : {-800.f, 800.f}) {
            const float* p = e ? V.b : V.a;
            const float k = e ? 1200.f : -1200.f, x = p[0] + ux * k + sx * s, z = p[2] + uz * k + sz * s;
            lo[0] = std::min(lo[0], x); hi[0] = std::max(hi[0], x); lo[2] = std::min(lo[2], z); hi[2] = std::max(hi[2], z);
        }
    FacesIn31(lo, hi, V.faces);
    Log("probe31 v profile: %zu faces of the map about it; its walls:", V.faces.size());
    FaceList31("v", V.faces, V.a, V.u, 40);
    for (int pass = 0; pass < 2; ++pass) {               // from the near side (along from A), then from the far side (from B back)
        const float* o = pass ? V.b : V.a;
        const float fl = pass ? V.farFloor : V.nearFloor, k = pass ? -1.f : 1.f;
        auto alongOf = [&](float t) { const float d = -600.f + t * 3600.f; return pass ? V.len - d : d; };
        for (int h = 100; h <= 3000; h += 100) {
            char b[640];
            int n = snprintf(b, sizeof b, "probe31 v profile: from the %s side, %4d over its floor: the mesh's first face (aside -300 0 300)", pass ? "far" : "near", h);
            for (float side : {-300.f, 0.f, 300.f}) {
                const float p0[3] = {o[0] - k * ux * 600.f + sx * side, fl + h, o[2] - k * uz * 600.f + sz * side};
                const float p1[3] = {o[0] + k * ux * 3000.f + sx * side, fl + h, o[2] + k * uz * 3000.f + sz * side};
                float t = 0.f, nrm[3] = {0, 0, 0};
                if (gMap->nav->FirstHit(p0, p1, &t, nrm)) n += snprintf(b + n, sizeof b - n, " %.0f (n %.2f %.2f %.2f)", alongOf(t), nrm[0], nrm[1], nrm[2]);
                else n += snprintf(b + n, sizeof b - n, " -");
            }
            const float p0[3] = {o[0] - k * ux * 600.f, fl + h, o[2] - k * uz * 600.f}, p1[3] = {o[0] + k * ux * 3000.f, fl + h, o[2] + k * uz * 3000.f};
            float t = 0.f;
            const Face* f = Hit31(p0, p1, V.faces, false, &t);
            if (f) n += snprintf(b + n, sizeof b - n, "; the map's first face %.0f (attribute %08x)", alongOf(t), f->attr);
            else n += snprintf(b + n, sizeof b - n, "; the map's first face -");
            if (!pass) {                                 // (a line across, 0.6 m short of A to 0.6 m past B)
                const float c0[3] = {V.a[0] - ux * 600.f, fl + h, V.a[2] - uz * 600.f}, c1[3] = {V.b[0] + ux * 600.f, fl + h, V.b[2] + uz * 600.f};
                float tc = 0.f;
                snprintf(b + n, sizeof b - n, "; clear across: the mesh's Clear %s, the map's faces %s", gMap->nav->Clear(c0, c1) ? "yes" : "no", Hit31(c0, c1, V.faces, false, &tc) ? "no" : "yes");
            }
            Log("%s", b);
        }
    }
    std::string g;
    for (float s = -1500.f; s <= V.len + 1500.f; s += 100.f) {
        const float p[3] = {V.a[0] + ux * s, std::max(V.nearFloor, V.farFloor) + 3500.f, V.a[2] + uz * s};
        char b[32]; snprintf(b, sizeof b, " %.0f:%.0f", s, gMap->nav->GroundBelow(p, 9000.f));
        g += b;
    }
    Log("probe31 v profile: the mesh's ground along the line (mm from A: height):%s", g.c_str());
}
void VLog31(const char* tag) {
    float along = 0.f, aside = 0.f; VAlong31(&along, &aside);
    float g[3] = {0, 0, 0}; const bool on = Ground(g);
    Log("probe31 v %s: along %.0f aside %.0f hips %.0f (its ground %.0f) state %x %x %x 6 %x 9 %x facing %.0f", tag, along, aside, gMe.pos[1], on ? g[1] : -1.f, gMe.bits[0],
        gMe.bits[1], gMe.bits[2], gMe.bits[6], gMe.bits[9], Facing() / kDeg31);
}
bool V31Setup() {
    V31& V = gV31;
    if (V.init) return V.have;
    V.init = true;
    if (const char* e = getenv("MGO1_DOLL_PROBE_VAULT")) {
        float v[6];
        if (sscanf(e, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) { memcpy(V.a, v, sizeof V.a); memcpy(V.b, v + 3, sizeof V.b); }
    }
    const float dx = V.b[0] - V.a[0], dz = V.b[2] - V.a[2];
    V.len = std::max(1.f, std::sqrt(dx * dx + dz * dz));
    V.u[0] = dx / V.len; V.u[1] = dz / V.len;
    const float pa[3] = {V.a[0], V.a[1] + 1000.f, V.a[2]}, pb[3] = {V.b[0], V.b[1] + 1000.f, V.b[2]};
    V.nearFloor = gMap->nav->GroundBelow(pa, 3000.f); V.farFloor = gMap->nav->GroundBelow(pb, 3000.f);
    const float top = gMap->nav->CrossingTop(V.a, V.b), topWide = gMap->wide ? gMap->wide->CrossingTop(V.a, V.b) : -1.f;
    Log("probe31 v: map %d, from %.0f %.0f %.0f to %.0f %.0f %.0f (%.0f mm), the floors %.0f and %.0f, the mesh's top %.0f (the wide mesh's %.0f)", gRoom.map, V.a[0], V.a[1], V.a[2],
        V.b[0], V.b[1], V.b[2], V.len, V.nearFloor, V.farFloor, top, topWide);
    if (V.nearFloor < -1e29f || V.farFloor < -1e29f) { Log("probe31 v: no floor at its ends"); return false; }
    VProfile31();
    float face = 1e30f;                                  // its near face: the mesh's first face on the line, knees to hips
    for (float h : {300.f, 500.f, 800.f}) {
        const float p0[3] = {V.a[0] - V.u[0] * 600.f, V.nearFloor + h, V.a[2] - V.u[1] * 600.f}, p1[3] = {V.b[0], V.nearFloor + h, V.b[2]};
        float t = 0.f, nrm[3] = {0, 0, 0};
        if (gMap->nav->FirstHit(p0, p1, &t, nrm)) face = std::min(face, -600.f + t * (V.len + 600.f));
    }
    V.face = face < 1e29f ? face : 375.f;
    for (int k = 0; k < 2; ++k) {                        // the starts: 1.5 m and 2.5 m before A
        const float back = k ? 2500.f : 1500.f;
        const float s[3] = {V.a[0] - V.u[0] * back, V.nearFloor, V.a[2] - V.u[1] * back};
        float q[3] = {0, 0, 0};
        const bool on = gMap->nav->Nearest(s, 400.f, q) && Dist2D(q, s) < 400.f;
        float* dst = k ? V.start2 : V.start;
        memcpy(dst, on ? q : s, sizeof V.start);
        Log("probe31 v: the start %.1f m before A %s at %.0f %.0f %.0f", back / 1000.f, on ? "on the mesh" : "NOT on the mesh", dst[0], dst[1], dst[2]);
    }
    Log("probe31 v: its near face %.0f mm along (the mesh)", V.face);
    V.have = true;
    return true;
}
// ---- (v141 beta 8) f: the first person view's entry. Still 60 frames (everything let go, the view off), then a way's lead-in and
// its buttons held until the view's state (the state word 0's 0x80) comes on, 90 frames at most; the ways in turn, 5 times each:
// 0 R1 alone, 1 R1 and Square light at once, 2 Square light 30 frames then R1 with it, 3 a walk (the stick full) 40 frames then R1
// and Square light, the stick let go, 4 the readied walk (Square light, the stick 0.64) 40 frames then R1 with it, the stick let go,
// 5 as 4 with the stick let go 8 frames before R1, 6 as 4 with R1 alone (Square let go with the stick). The frames to the view ----
struct F31 { int trial = 0, phase = 0; long long at = 0; float dir = 0.f; std::vector<int> res[7]; };
F31 gF31;
void StepF31() {
    F31& F = gF31;
    const int kWays = 7, kEach = 5;
    if (F.trial >= kWays * kEach) {
        if (F.phase != 9) {
            F.phase = 9;
            for (int w = 0; w < kWays; ++w) { std::string s; for (int v : F.res[w]) s += " " + std::to_string(v); Log("probe31: RESULT view entry way %d:%s", w, s.c_str()); }
            Log("probe31: done");
        }
        Steer(0, 0, 0);
        return;
    }
    const int way = F.trial % kWays;
    const long long t = gFrame - F.at;
    Steer(0, 0, 0);
    switch (F.phase) {
    case 0:                                              // still, everything let go, 60 frames (the view off)
        if (t < 60 || (gMe.bits[0] & 0x80)) return;
        F.phase = 1; F.at = gFrame; F.dir = Facing() + 1.5707963f * ((F.trial / kWays) % 2 ? 1.f : -1.f);   // (the walks sideways, each side in turn)
        return;
    case 1: {                                            // the way's lead-in
        const int lead = way == 2 ? 30 : way >= 3 ? 40 : 0;
        if (t < lead) {
            if (way == 2) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
            if (way == 3) Steer(std::sin(F.dir), std::cos(F.dir), 1.f);
            if (way >= 4) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; if (way != 5 || t < lead - 8) Steer(std::sin(F.dir), std::cos(F.dir), 0.64f); }
            return;
        }
        F.phase = 2; F.at = gFrame;
        return;
    }
    case 2: {                                            // R1 (and Square light, but in ways 0 and 6) until the view
        gPad.buttons |= 0x0800;
        if (way != 0 && way != 6) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
        if ((gMe.bits[0] & 0x80) || t >= 90) {
            const int n = (gMe.bits[0] & 0x80) ? (int)t : -1;
            F.res[way].push_back(n);
            Log("probe31: view entry %d way %d: %s %lld frames (state %x %x)", F.trial, way, n >= 0 ? "on after" : "not on after", t, gMe.bits[0] & 0xFFFF,
                gMe.bits[1] & 0xFFFF);
            ++F.trial; F.phase = 0; F.at = gFrame;
        } else if (t % 4 == 0) Log("probe31: view entry %d way %d: +%lld state %x %x", F.trial, way, t, gMe.bits[0] & 0xFFFF, gMe.bits[1] & 0xFFFF);
        return;
    }
    }
}
void StepV31() {
    V31& V = gV31;
    if (!V31Setup()) { Probe31Next(); return; }
    if (V.trial >= kVaultWays31) { Log("probe31: v done"); Probe31Next(); return; }
    const long long t = gFrame - V.at;
    auto go = [&](int ph) { V.phase = ph; V.at = gFrame; };
    float along = 0.f, aside = 0.f; VAlong31(&along, &aside);
    const float ux = V.u[0], uz = V.u[1];
    if (Died31(V.seen) && V.phase != 0) { Log("probe31 v: a new life"); StopPath(); go(0); return; }
    switch (V.phase) {
    case 0:                                              // to the start (1 m farther for way 3)
        if (Go31(V.trial == 3 ? V.start2 : V.start, 250.f, t)) { go(1); return; }
        if (t > 7200) { Log("probe31 v %d: the start not reached (at %.0f %.0f %.0f)", V.trial, gMe.pos[0], gMe.pos[1], gMe.pos[2]); ++V.trial; go(0); }
        return;
    case 1:                                              // facing along the crossing
        if (!Face31(std::atan2(ux, uz), t)) return;
        V.maxAlong = V.maxHips = V.lastAlong = -1e30f; V.states.clear(); V.pressAt = V.stillAt = -1; V.bestAt = gFrame; V.crossed = false;
        { char tag[96]; snprintf(tag, sizeof tag, "%d: %s, from", V.trial, kVaultWay31[V.trial]); VLog31(tag); }
        if (V.trial == 0) ForceCrossing(V.a, V.b, AREA_VAULT);
        go(2);
        return;
    case 2: {                                            // the way tried
        V.maxAlong = std::max(V.maxAlong, along); V.maxHips = std::max(V.maxHips, gMe.pos[1]);
        { char st[16]; snprintf(st, sizeof st, " %x", gMe.bits[0] & 0xFFFF); if (V.states.find(st) == std::string::npos && V.states.size() < 200) V.states += st; }
        if (t % 10 == 0) { char tag[48]; snprintf(tag, sizeof tag, "%d +%lld", V.trial, t); VLog31(tag); }
        V.crossed = along > V.len - 450.f && std::fabs(aside) < 900.f && std::fabs(gMe.pos[1] - 800.f - V.farFloor) < 450.f;
        if (along > V.lastAlong + 50.f) { V.lastAlong = along; V.bestAt = gFrame; }
        const bool facing = std::fabs(Wrap(std::atan2(ux, uz) - Facing())) < 35.f * kDeg31;
        const float k = V.trial == 1 || V.trial == 5 ? 0.7f : 1.f;
        bool end = V.crossed || t > (V.trial == 0 ? 900 : V.trial == 5 ? 420 : 300);
        switch (V.trial) {
        case 0: if (Following()) FollowPath(); else end = true; break;
        case 4:                                          // the roll: running, Cross 0.9 m before its face
            Steer(ux - uz * aside / 800.f, uz + ux * aside / 800.f, 1.f);
            if (V.pressAt < 0 && V.face - along <= 900.f) { V.pressAt = gFrame; VLog31("Cross"); }
            if (V.pressAt >= 0 && gFrame - V.pressAt < 4) gPad.buttons |= 0x4000;
            if (V.pressAt >= 0 && gFrame - V.pressAt > 90) end = true;
            break;
        case 5:                                          // pressed against it (no nearer 0.5 s), then still: Triangle held 60 frames, then taps
            if (V.stillAt < 0) {
                Steer(ux - uz * aside / 800.f, uz + ux * aside / 800.f, k);
                if (gFrame - V.bestAt > 30 && along > -900.f) { V.stillAt = gFrame; VLog31("against it"); }
            } else {
                Steer(0, 0, 0);
                const long long s = gFrame - V.stillAt;
                if (s >= 10 && s < 70) gPad.buttons |= 0x1000;
                else if (s >= 90 && s % 15 < 4) gPad.buttons |= 0x1000;
            }
            break;
        case 6:                                          // running, Triangle held all the way
            Steer(ux - uz * aside / 800.f, uz + ux * aside / 800.f, 1.f);
            if (along > -1500.f) gPad.buttons |= 0x1000;
            break;
        default:                                         // walking or running at it, Triangle every quarter second facing it
            Steer(ux - uz * aside / 800.f, uz + ux * aside / 800.f, k);
            if (along > -900.f && facing && gFrame % 15 < 3) gPad.buttons |= 0x1000;
            break;
        }
        if (!end) return;
        Log("probe31: RESULT vault %d (%s): %s, the farthest %.0f along (its face %.0f, the far floor's edge %.0f), the hips at most %.0f (the near floor %.0f), now %.0f along "
            "%.0f aside hips %.0f; the states%s",
            V.trial, kVaultWay31[V.trial], V.crossed ? "CROSSED" : "not crossed", V.maxAlong, V.face, V.len, V.maxHips, V.nearFloor, along, aside, gMe.pos[1], V.states.c_str());
        StopPath(); Steer(0, 0, 0);
        if (V.crossed) { ForceCrossing(V.b, V.a, AREA_VAULT); go(3); return; }
        ++V.trial; go(0);
        return;
    }
    default:                                             // crossed: back over it the other way (the crossing code), then the next way
        if (Following() && t < 900) { FollowPath(); return; }
        VLog31("back");
        StopPath();
        ++V.trial; go(0);
        return;
    }
}

// (v150-host beta 22, wake) MGO1_DOLL_PROBE_KO=1, the victim (benches only): knocked out by its own game when the other player
// crouches within 2.5 m (the actor's step w asks so), once it has been up 10 s with its stamina full: its pending stun (+0xB72,
// where a damage record's stun goes on the owner's machine, US n003a 0x4367A0) set to its maximum (+0xB6A), so that its own game
// knocks it out at its next tick by its own way (0x4143F0, 0x414368: the flag 0x99, the stars), as a Mosin dart's record does
void KoSelf31(const Player* o) {
    static const bool on = getenv("MGO1_DOLL_PROBE_KO") != nullptr;
    static long long upSince = -1;
    if (!on || !gMe.obj || !gMe.alive || (gMe.bits[0] & 0x1A000000)) { upSince = -1; return; }
    if (upSince < 0) upSince = gFrame;
    const int most = R16s(gMe.obj + 0xB6A);
    // ((v150-host beta 25, wakeway) MGO1_DOLL_PROBE_KO_ANY=1: whatever its gauge: a victim sent to its place by MGO1_DOLL_PROBE_D_PLACE
    // spent stamina on its walk and its gauge never came back full, wakeway_b_on_t1 "the victim not knocked out" three times)
    static const bool any = getenv("MGO1_DOLL_PROBE_KO_ANY") != nullptr;
    if (gFrame - upSince < 600 || (gMe.stun < most && !any) || R16s(gMe.obj + 0xB72) != 0) return;
    o = nullptr;                                         // (a teammate crouched within 2.5 m)
    for (const Player& p : gPlayers) if (!p.local && p.alive && p.team == gMyTeam && Dist2D(p.pos, gMe.pos) <= 2500.f && (p.bits[0] & 4)) o = &p;
    if (!o) return;
    const int16_t v = (int16_t)most;
    memcpy(ps2::gMem + ((gMe.obj + 0xB72) & 0x01FFFFFF), &v, 2);
    upSince = -1;
    Log("probe31: KO: %s crouched %.1f m away: the pending stun +0xB72 set to %d (its own game knocks it out); the ally wake's +0xB74 %d, the rate +0xB78 %d",
        o->name.c_str(), Dist2D(o->pos, gMe.pos) / 1000.f, most, (int)R8(gMe.obj + 0xB74), (int)R16s(gMe.obj + 0xB78));
}
// (v150-host beta 24, cqcfight) m: full CQC on the enemies, never a shot (a player's report on beta 23: "when I was going full CQC
// the bots stopped attacking until one KO'd another"). A CQC weapon in its hands, it runs at the nearest enemy standing (alive, not
// down, not knocked out), within 0.55 m facing him and still Circle held: the grab, held MGO1_DOLL_PROBE_HOLD frames more (60) with
// Circle down, let go (the throw), 40 frames for the throw's motion, then the nearest again (the same one once he is up). Logged:
// each grab and its let go with the enemies within 6 m, each fall of its own life (who stood within 1.5 m, holding or not), and a
// RESULT line each life: its grabs, the frames it spent within 1.5 m of an enemy and holding, the life it lost there and elsewhere
struct M31 { int sub = 0, target = -1, grabs = 0, tries = 0, life = -1, lostNear = 0, lostHold = 0, lostFar = 0; long long at = 0, grabAt = -1, nearF = 0, holdF = 0, lifeAt = -1; };
M31 gM31;
void StepM31() {
    M31& M = gM31;
    static const long long hold = getenv("MGO1_DOLL_PROBE_HOLD") ? atoll(getenv("MGO1_DOLL_PROBE_HOLD")) : 60;
    const long long t = gFrame - M.at;
    auto standing = [](const Player& p) { return !p.local && p.alive && Enemy(p) && !(p.bits[0] & 0x1E000000u) && !((p.bits[4] >> 25) & 1); };
    auto nearest = [&](float within) -> const Player* {
        const Player* best = nullptr; float bd = within;
        for (const Player& p : gPlayers) if (standing(p) && Dist2D(p.pos, gMe.pos) < bd && std::fabs(p.pos[1] - gMe.pos[1]) < 1500.f) { bd = Dist2D(p.pos, gMe.pos); best = &p; }
        return best;
    };
    auto around = [&]() {
        std::string s;
        for (const Player& p : gPlayers) if (!p.local && p.alive && Enemy(p) && Dist2D(p.pos, gMe.pos) < 6000.f) {
            char b[96]; snprintf(b, sizeof b, " %s %.1f m%s", p.name.c_str(), Dist2D(p.pos, gMe.pos) / 1000.f, (p.bits[0] & 0x1E000000u) ? " (down)" : ""); s += b;
        }
        return s.empty() ? std::string(" none") : s;
    };
    const bool holding = (gMe.bits[0] & 0xFFFF) == 2 && (gMe.bits[6] & 0x60000000) == 0x60000000;
    const Player* closest = nearest(1500.f);
    if (M.life < 0 || gP31.newLife) {
        if (M.lifeAt >= 0)
            Log("probe31: RESULT melee life: %d grabs (%d tries), %.1f s within 1.5 m of an enemy (%.1f s holding one), its life lost there %d (holding %d), elsewhere %d",
                M.grabs, M.tries, M.nearF / 60.0, M.holdF / 60.0, M.lostNear, M.lostHold, M.lostFar);
        M = M31(); M.life = gMe.life; M.lifeAt = gFrame; M.at = gFrame;
        return;
    }
    if (gMe.life < M.life) {
        const int lost = M.life - gMe.life;
        if (closest) M.lostNear += lost; else M.lostFar += lost;
        if (holding) M.lostHold += lost;
        Log("probe31: melee: its life %d -> %d (%s%s; the enemies within 6 m:%s)", M.life, gMe.life, closest ? "an enemy within 1.5 m" : "none within 1.5 m", holding ? ", holding one" : "", around().c_str());
    }
    M.life = gMe.life;
    if (closest) ++M.nearF;
    if (holding) ++M.holdF;
    if (!gMe.alive) { Steer(0, 0, 0); return; }
    if ((gMe.bits[0] & 0x1A000000u) || Low()) { Steer(0, 0, 0); if (gFrame % 4 < 2) gPad.buttons |= 0x4000; M.sub = 0; M.at = gFrame; return; }   // (down: up at once, as a player presses)
    switch (M.sub) {
    case 0: {                                                    // a CQC weapon in its hands, the nearest enemy standing
        const int want = CqcCode31();
        if (want >= 0 && Inv().ok && Inv().weapon != want && t < 600) { if (ProbeWindowTo(0, want)) return; }
        const Player* o = nearest(1e30f);
        if (!o) { Steer(0, 0, 0); return; }
        M.target = o->id; M.sub = 1; M.at = gFrame;
        return;
    }
    case 1: {                                                    // to him: its way, then straight within 2.5 m; within 0.55 m still
        const Player* o = PlayerById(M.target);
        if (!o || !standing(*o) || t > 1200) { StopPath(); M.sub = 0; M.at = gFrame; return; }
        const float d = Dist2D(gMe.pos, o->pos);
        if (d > 2500.f) { if (!Following() || gFrame % 30 == 0) PlanTo(o->pos); FollowPath(); return; }
        StopPath();
        if (d > 550.f) { Steer(o->pos[0] - gMe.pos[0], o->pos[2] - gMe.pos[2], 1.f); return; }
        if (!Face31(AngleTo(gMe.pos, o->pos), t % 120)) return;
        if ((gMe.bits[0] & 0xFFFF) != 3 && t % 60 < 20) { Steer(0, 0, 0); return; }   // (still first: Circle pressed walking is a punch)
        ++M.tries; M.grabAt = -1; M.sub = 2; M.at = gFrame;
        return;
    }
    case 2: {                                                    // Circle held: the grab, `hold` frames more, let go
        Steer(0, 0, 0);
        const Player* o = PlayerById(M.target);
        if (holding && M.grabAt < 0) { M.grabAt = gFrame; ++M.grabs; Log("probe31: melee: grabs %s (%lld frames; the enemies within 6 m:%s)", o ? o->name.c_str() : "?", t, around().c_str()); }
        if ((M.grabAt >= 0 && gFrame - M.grabAt >= hold) || (M.grabAt < 0 && t >= 40)) {
            Log("probe31: melee: Circle let go (%s, held %lld frames; the enemies within 6 m:%s)", M.grabAt >= 0 ? "a throw" : "NO grab", M.grabAt >= 0 ? gFrame - M.grabAt : 0, around().c_str());
            M.sub = 3; M.at = gFrame;
            return;
        }
        gPad.buttons |= 0x2000;
        return;
    }
    default:                                                     // the throw's motion, then the next one
        Steer(0, 0, 0);
        if (t >= 40) { M.sub = 0; M.at = gFrame; }
        return;
    }
}
// (v150-host beta 22, wake) w: a teammate's blows on the victim knocked out or reading a book (MGO1_DOLL_PROBE_WAKE, trials by
// comma, "k1,k3,kr,kn,b1,b3,br,bn" by default): k knocked out (the victim's MGO1_DOLL_PROBE_KO, asked by this actor crouching
// beside it), b reading a book this actor lays at its own feet beside it (its loadout's ,s6 taken by a new life); then 1 one
// Circle press (one punch), 3 three (the punch combo: punch, punch, kick), r a roll into it (Cross at 1.8 m from 5 m), n no
// blow. Logged: the presses, the victim's copy (its state words, stamina, +0xDE9 reading) each change for 600 frames; RESULT
struct W31 { size_t trial = 0; std::vector<std::string> trials; int sub = 0; long long at = 0, downAt = -1, readAt = -1, blowAt = -1, crossAt = -1; bool knocked = false; int read0 = -1;
             int dRuns = 0, stunPrev = -1; };     // ((v150-host beta 25, wakeramp) the runs at it so far; its stamina the frame before)
W31 gW31;
// (v150-host beta 25, wakeramp) MGO1_DOLL_PROBE_W_FROM ("x,y,z/x,y,z/..."; each a floor point): the d trials' run-up places in turn
// (each run the next, round again), in place of the place 5 m away: a teammate lying on stairs or a ramp run at from above, below
int WFrom31(float out[][3], int max) {
    const char* e = getenv("MGO1_DOLL_PROBE_W_FROM");
    int n = 0;
    for (const char* p = e; p && *p && n < max;) {
        if (sscanf(p, "%f,%f,%f", &out[n][0], &out[n][1], &out[n][2]) == 3) ++n;
        p = strpbrk(p, ";/"); if (p) ++p;
    }
    return n;
}
// (v150-host beta 25, wakeramp) a bone's place (its matrix's translation, the world's: the attack maker 0x408A10 puts the blow's sphere
// there, the kick's 600 mm on bone 20) of a player's body (+0x200: +0x80 its bones' count, +0x88 their matrices, 64 bytes each)
bool Bone31(uint32_t obj, int bone, float* p) {
    const uint32_t body = obj ? R32(obj + 0x200) : 0;
    if (body < 0x100000 || body >= 0x02000000) return false;
    const uint32_t nb = R32(body + 0x80), bones = R32(body + 0x88);
    if (bone < 0 || (uint32_t)bone >= nb || nb > 256 || bones < 0x100000 || bones + 64 * nb > 0x02000000) return false;
    for (int k = 0; k < 3; ++k) p[k] = RF(bones + 64 * bone + 0x30 + 4 * k);
    return std::fabs(p[0]) < 1e6f && std::fabs(p[1]) < 1e6f && std::fabs(p[2]) < 1e6f;
}
// (v150-host beta 25, wakeramp) the bone of player obj nearest a point: its number, its place, the distance (3D); -1 none
int NearBone31(uint32_t obj, const float* at, float* p, float* dist) {
    const uint32_t body = obj ? R32(obj + 0x200) : 0;
    const uint32_t nb = body >= 0x100000 && body < 0x02000000 ? R32(body + 0x80) : 0;
    int best = -1; *dist = 1e30f;
    for (int b = 0; b < (int)nb && b < 256; ++b) {
        float q[3];
        if (!Bone31(obj, b, q)) continue;
        const float d = std::sqrt((q[0] - at[0]) * (q[0] - at[0]) + (q[1] - at[1]) * (q[1] - at[1]) + (q[2] - at[2]) * (q[2] - at[2]));
        if (d < *dist) { *dist = d; best = b; memcpy(p, q, sizeof q); }
    }
    return best;
}
void StepW31(const Player* o) {
    W31& W = gW31;
    Probe31& P = gP31;
    const char* vn = getenv("MGO1_DOLL_PROBE_VICTIM_NAME");   // (the victim by name: an enemy's book, the after benches)
    o = vn ? PlayerByName(vn) : nullptr;                 // (else the nearest teammate: an enemy nearer is not the victim)
    if (!vn) for (const Player& p : gPlayers) if (!p.local && p.alive && p.team == gMyTeam && (!o || Dist2D(p.pos, gMe.pos) < Dist2D(o->pos, gMe.pos))) o = &p;
    if (o && !o->alive) o = nullptr;
    // (the victim dead or gone: the trial again from its start; a victim respawned elsewhere is taken there)
    if (!o) { Steer(0, 0, 0); if (W.sub) { Log("probe31: wake: the victim gone (dead): the trial again"); W.sub = 0; W.at = gFrame; W.blowAt = W.crossAt = -1; StopPath(); } return; }
    if (W.trials.empty()) {
        std::string v = getenv("MGO1_DOLL_PROBE_WAKE") ? getenv("MGO1_DOLL_PROBE_WAKE") : "k1,k3,kr,kn,b1,b3,br,bn";
        for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); if (e - a >= 2) W.trials.push_back(v.substr(a, e - a)); a = e + 1; }
        W.at = gFrame;
    }
    if (W.trial >= W.trials.size()) { Probe31Next(); return; }
    const std::string& tr = W.trials[W.trial];
    const bool book = tr[0] == 'b';
    // ((v150-host beta 24, stamina) s: the victim STANDING, up (an enemy by MGO1_DOLL_PROBE_VICTIM_NAME): the blow at once, no knock
    // out asked, then 900 frames watched: what a conscious player's gauge does after a blow or a CQC throw (s1e, s3e, sc ...))
    const bool standing = tr[0] == 's';
    const char blow = tr[1];
    const long long t = gFrame - W.at;
    const float dist = Dist2D(gMe.pos, o->pos);
    const int reading = o->obj ? (int)R8(o->obj + 0xDE9) : -1;
    // (knocked out: the flag 0x99, word 4 bit 25; lying: the state word 0's bit 3 (the flag 3) or 0x1B / 0x1C, the ally wake's own test)
    const bool out = ((o->bits[4] >> 25) & 1) != 0, lying = (o->bits[0] & 0x18000008u) != 0;
    const bool down = out || (o->bits[0] & 0x1A000000) != 0;
    const bool wide = tr.size() >= 3 && tr[2] == 'f';     // (k1f, b1f ...: Circle from 0.85 m; else from 0.55 m)
    const bool wideOrWalk = tr.size() >= 3 && tr[2] == 'w';   // (kdw: walking into it)
    const bool bare = tr.size() >= 3 && tr[2] == 'e';     // (k1e, k3e: empty hands, from where he fell: with a rifle in hand Circle
                                                          // is the rifle's own strike, US n003a 0x41B390, not the punches)
    const bool atFeet = bare || (tr.size() >= 3 && tr[2] == 'n');   // (k1n, k3n: Circle from where he fell beside it, no step back)
    auto next = [&](const char* why) { Log("probe31: wake %s: %s", tr.c_str(), why); ++W.trial; W.sub = 0; W.at = gFrame; W.blowAt = W.crossAt = W.downAt = -1; P.spotHave = false; StopPath(); Steer(0, 0, 0); };
    auto go = [&](int sub) { W.sub = sub; W.at = gFrame; };
    if (W.blowAt >= 0 && reading != W.read0) { Log("probe31: wake %s +%lld: %s's +0xDE9 %d (state %x %x stun %d)", tr.c_str(), gFrame - W.blowAt, o->name.c_str(), reading, o->bits[0], o->bits[1], o->stun); W.read0 = reading; }
    // (crouched out of the knock out's signal: Cross stands it up, in every phase but that signal and the blow)
    if ((W.sub == 0 || W.sub == 1) && (gMe.bits[0] & 4) && gFrame % 30 < 3) gPad.buttons |= 0x4000;   // (held 3 frames: wake_ts_off's 1-frame presses left it crouched)
    switch (W.sub) {
    case 0:                                              // the book's loadout first (a new life), the victim up
        if (book && (!Inv().ok || Inv().rounds[0x1C] <= 0)) {
            if (t == 1) { gSet.loadout = "ak47,m1911,s6"; Log("probe31: wake %s: next life with the book (%s)", tr.c_str(), gSet.loadout.c_str()); }
            if (P.job && P.newLife) { P.job = false; W.at = gFrame; return; }
            if (ProbeSuicideFrame()) { P.job = true; return; }
            if (t > 900) next("no book");
            return;
        }
        if (down || reading > 0) { Steer(0, 0, 0); W.at = gFrame; return; }   // (11 s up first: the victim's KO switch asks 10, its book cooldown 7.5)
        if (t < 660) return;
        go(1);
        return;
    case 1: {                                            // beside it (0.85 m: a punch's reach, docs/PLAYABLE_DOLL.md "under 0.9 m")
        const float feet[3] = {o->pos[0], o->pos[1] - 800.f, o->pos[2]};
        if (Go31(feet, wide ? 900.f : 600.f, t)) { go(2); return; }
        if (t > 2400) next("the victim not reached");
        return;
    }
    case 2:                                              // knocked out: crouched 60 frames (the victim's signal), up, its knock out awaited; a book: laid
        if (standing) { W.downAt = gFrame; go(3); return; }   // ((v150-host beta 24, stamina) standing: the blow at once)
        if (!book) {
            if (out && W.downAt < 0) { W.downAt = gFrame; Log("probe31: wake %s: %s knocked out (state %x, word 4 %x, stun %d, %.2f m)", tr.c_str(), o->name.c_str(), o->bits[0], o->bits[4], o->stun, dist / 1000.f); }
            if (out && lying) { Log("probe31: wake %s: %s lying knocked out (state %x, word 4 %x, stun %d, %.2f m, %lld frames after the knock out)", tr.c_str(), o->name.c_str(), o->bits[0], o->bits[4], o->stun, dist / 1000.f, gFrame - W.downAt); go(3); return; }
            if (W.downAt >= 0) return;                   // (knocked out, still falling: its lying awaited)
            if (t % 180 < 90) { if (!Low() && t % 30 == 0) gPad.buttons |= 0x4000; } else if (Low() && t % 30 == 0) gPad.buttons |= 0x4000;
            if (t > 1800) next("the victim not knocked out");
            return;
        }
        if (Inv().weapon != 0x1C) { ProbeWindowTo(0, 0x1C); if (t > 600) next("the book not taken in hand"); return; }
        if (t < 600) { W.at = gFrame - 600; return; }
        if (t < 606) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; if (t == 600) Log("probe31: wake %s: the book laid (Square) %.2f m from %s", tr.c_str(), dist / 1000.f, o->name.c_str()); return; }
        if (reading > 0) { W.readAt = gFrame; Log("probe31: wake %s: %s reads (state %x %x, %.2f m)", tr.c_str(), o->name.c_str(), o->bits[0], o->bits[1], dist / 1000.f); go(3); return; }
        if (t > 1200) next("the victim does not read");
        return;
    case 3:                                              // standing, the AK-47 back in hand; the roll's place 5 m away
        if ((Low() || (gMe.bits[0] & 4)) && t < 600) { if (t % 30 < 3) gPad.buttons |= 0x4000; return; }   // (crouched: Cross stands it up)
        if (Low() || (gMe.bits[0] & 4)) { next("still crouched after 10 s: no press (a crouched Circle is another move)"); return; }
        // (knocked out: lying at its feet after his fall; a step back to 0.6 to 0.9 m from his hips, his body before it, as a player stands to punch)
        if (!book && !atFeet && blow != 'r' && blow != 'c' && blow != 'd' && dist < 600.f && t < 300) { Steer(gMe.pos[0] - o->pos[0], gMe.pos[2] - o->pos[2], 0.5f); return; }
        if (bare || blow == 'd') { if (Inv().ok && Inv().weapon != 0 && t < 360) { ProbeWindowTo(0, 0); return; } }   // (empty hands: the weapon window's none)
        else if (Inv().ok && Inv().rounds[0xC] >= 0 && Inv().weapon != 0xC && t < 240) { ProbeWindowTo(0, 0xC); return; }
        if (blow == 'd' && !P.spotHave) {                // ((v150-host beta 25, wakeramp) MGO1_DOLL_PROBE_W_FROM: the run-up place given)
            float from[16][3];
            if (const int n = WFrom31(from, 16)) {
                memcpy(P.spot, from[W.dRuns % n], sizeof P.spot); P.spotHave = true;
                Log("probe31: wake %s: the run-up place %d (%.0f %.0f %.0f), %s at %.0f %.0f %.0f", tr.c_str(), W.dRuns % n, P.spot[0], P.spot[1], P.spot[2], o->name.c_str(), o->pos[0], o->pos[1], o->pos[2]);
            }
        }
        if (blow == 'r' || blow == 'c' || blow == 'd') {
            if (!P.spotHave && !Spot31(*o, 5000.f, true)) { next("no place 5 m away with a straight way"); return; }
            if (Dist2D(gMe.pos, P.spot) > 600.f && t < 900) { if (!Following() || gFrame % 120 == 0) PlanTo(P.spot); FollowPath(); return; }
            StopPath();
        }
        if (!Face31(AngleTo(gMe.pos, o->pos), t)) return;
        if (blow != 'r' && blow != 'c' && blow != 'd' && gMe.bits[0] != 3 && t < 360) { Steer(0, 0, 0); return; }   // (standing still first: a press while it runs is another move)
        W.read0 = reading;
        W.blowAt = gFrame;
        Log("probe31: wake %s: the blow %s at %.2f m (me %x, in hand %x, %s %x %x stun %d life %d +0xDE9 %d, %lld frames after %s)", tr.c_str(), blow == '1' ? "one Circle" : blow == '3' ? "three Circles" :
            blow == 'r' ? "a roll" : blow == 'c' ? "Circle running at it" : blow == 'd' ? (wideOrWalk ? "three Circles walking into it" : "three Circles running into it") : "none", dist / 1000.f, gMe.bits[0], Inv().ok ? Inv().weapon : -1, o->name.c_str(), o->bits[0], o->bits[1], o->stun, o->life, reading, gFrame - (book ? W.readAt : W.downAt), book ? "its reading began" : "its knock out");
        go(4);
        return;
    default: {                                           // the blow, then 600 frames watched
        if (blow == '1' && t < 3) gPad.buttons |= 0x2000;
        if (blow == '3' && (t < 3 || (t >= 12 && t < 15) || (t >= 24 && t < 27))) gPad.buttons |= 0x2000;
        if (blow == 'd') {                              // (d, dw: running / walking into it, Circle at 1.0 m and 12, 24 frames later, the stick on him)
            // ((v150-host beta 25, wakeramp) dh: the stick let go at the first press, no run on into him: the kick from where the press was)
            const bool halt = tr.size() >= 3 && tr[2] == 'h';
            if (W.crossAt < 0 || (!halt && gFrame - W.crossAt < 40)) Steer(o->pos[0] - gMe.pos[0], o->pos[2] - gMe.pos[2], wideOrWalk ? 0.4f : 1.f); else Steer(0, 0, 0);
            if (W.crossAt < 0 && dist <= 1000.f) {
                W.crossAt = gFrame; W.stunPrev = o->stun; ++W.dRuns;
                // ((v150-host beta 25, wakeramp) the heights: my hips (800 over my floor running), his (300 over his floor lying))
                Log("probe31: wake %s +%lld: Circle at %.2f m (me %x; my hips %.0f %.0f %.0f, his %.0f %.0f %.0f: my floor %+.0f mm over his)", tr.c_str(), t, dist / 1000.f, gMe.bits[0],
                    gMe.pos[0], gMe.pos[1], gMe.pos[2], o->pos[0], o->pos[1], o->pos[2], (gMe.pos[1] - 800.f) - (o->pos[1] - 300.f));
            }
            if (W.crossAt >= 0) { const long long u = gFrame - W.crossAt; if (u < 3 || (u >= 12 && u < 15) || (u >= 24 && u < 27)) gPad.buttons |= 0x2000; }
            if (W.crossAt >= 0) {                        // ((v150-host beta 25, wakeramp) the kick's frames: my foot (bone 20), his nearest bone, his stamina)
                const long long u = gFrame - W.crossAt;
                if (u >= 40 && u <= 75) {
                    float foot[3] = {0, 0, 0}, nb[3] = {0, 0, 0}, nd = -1.f;
                    const bool f = Bone31(gMe.obj, 20, foot);
                    const int b = f ? NearBone31(o->obj, foot, nb, &nd) : -1;
                    Log("probe31: wake %s kick +%lld: %.2f m, my hips %.0f %.0f %.0f, his %.0f %.0f %.0f, my foot (bone 20) %s%.0f %.0f %.0f, his bone %d %.0f %.0f %.0f %.0f mm from it, his stamina %d (me %x)",
                        tr.c_str(), u, dist / 1000.f, gMe.pos[0], gMe.pos[1], gMe.pos[2], o->pos[0], o->pos[1], o->pos[2], f ? "" : "NOT READ ", foot[0], foot[1], foot[2], b, nb[0], nb[1], nb[2], nd, o->stun, gMe.bits[0]);
                }
                if (u > 0 && u <= 200 && W.stunPrev >= 0 && o->stun - W.stunPrev >= 5000)
                    Log("probe31: wake %s +%lld after the press: his stamina %d -> %d (a blow's ally wake on his game; %.2f m, my floor %+.0f mm over his)", tr.c_str(), u, W.stunPrev, o->stun, dist / 1000.f,
                        (gMe.pos[1] - 800.f) - (o->pos[1] - 300.f));
                W.stunPrev = o->stun;
            }
            if (W.crossAt < 0 && t > 300) { next("never within 1.0 m"); return; }
        } else if (blow == 'r' || blow == 'c') {        // (c, v150-host beta 22: running at it, Circle 1.0 m before it: wake_m1's CQC throw)
            const float at = blow == 'r' ? 1800.f : 1000.f;
            if (W.crossAt < 0 || gFrame - W.crossAt < (blow == 'r' ? 30 : 4)) Steer(o->pos[0] - gMe.pos[0], o->pos[2] - gMe.pos[2], 1.f); else Steer(0, 0, 0);
            if (W.crossAt < 0 && dist <= at) { W.crossAt = gFrame; Log("probe31: wake %s +%lld: %s at %.2f m (me %x)", tr.c_str(), t, blow == 'r' ? "Cross" : "Circle", dist / 1000.f, gMe.bits[0]); }
            if (W.crossAt >= 0 && gFrame - W.crossAt < 4) gPad.buttons |= blow == 'r' ? 0x4000 : 0x2000;
            if (W.crossAt < 0 && t > 300) { next("the roll: never within 1.8 m"); return; }
        } else Steer(0, 0, 0);
        if ((gPad.buttons & 0x2000) && (t == 0 || t == 12 || t == 24)) Log("probe31: wake %s +%lld: Circle (%.2f m; %s %x stun %d)", tr.c_str(), t, dist / 1000.f, o->name.c_str(), o->bits[0], o->stun);
        if (t <= 120 && t % 10 == 0) Log("probe31: wake %s +%lld: me %x %x; %s %x %x (word 4 %x) stun %d life %d +0xDE9 %d at %.2f m", tr.c_str(), t, gMe.bits[0], gMe.bits[1], o->name.c_str(), o->bits[0], o->bits[1], o->bits[4], o->stun, o->life, reading, dist / 1000.f);
        if (standing) {                                  // ((v150-host beta 24, stamina) its stamina as this game holds its copy, each second)
            if (t % 60 == 0 && t > 120) Log("probe31: wake %s +%lld: %s %x %x (word 4 %x) stun %d life %d", tr.c_str(), t, o->name.c_str(), o->bits[0], o->bits[1], o->bits[4], o->stun, o->life);
            if (t >= 900) { Log("probe31: RESULT wake %s: %s %s 900 frames after the blow; its stamina %d, life %d, state %x", tr.c_str(), o->name.c_str(), out ? "knocked out" : "conscious", o->stun, o->life, o->bits[0]); next("over"); }
            return;
        }
        const bool done = book ? reading <= 0 : !out;
        if (done || t >= 600) {
            Log("probe31: RESULT wake %s: %s %s %lld frames after the blow (%lld after %s); its stamina %d, life %d, state %x", tr.c_str(), o->name.c_str(),
                done ? (book ? "no longer reads" : "no longer knocked out") : (book ? "STILL reads" : "STILL knocked out"), t, gFrame - (book ? W.readAt : W.downAt), book ? "its reading began" : "its knock out",
                o->stun, o->life, o->bits[0]);
            next("over");
        }
        return;
    }
    }
}
// (v150-host beta 24, cqc) x: CQC throws on the victim (MGO1_DOLL_PROBE_VICTIM_NAME, else the nearest other player; trials by comma in
// MGO1_DOLL_PROBE_CQC, "h,h,h,s,r" by default): h the hold's throw (a CQC weapon in hand, still within 0.55 m facing him, Circle held
// to the grab and 8 frames more, 40 at most, let go: the playable doll's own CQC throw, doll_brain.cpp CqcFrame), s Circle tapped
// standing still within 0.55 m with the AK-47 in hand, p the same with a CQC weapon in hand, r Circle pressed running at him 1.0 m
// before him (wake_m1's throw). Each trial once he is up MGO1_DOLL_PROBE_CQC_GAP frames (60). Logged: his copy here (its state
// words, stamina, life, the knock out flag 0x99) for 600 frames, my grab and my CQC count (+0x36), a RESULT line a trial
struct X31 { size_t trial = 0; std::vector<std::string> trials; int sub = 0; long long at = 0, blowAt = -1, crossAt = -1, grabAt = -1, upAt = -1;
             int stun0 = -1, stunMin = -1, life0 = -1, score0 = -1; bool fell = false, ko = false; };
X31 gX31;
void StepX31(const Player* o) {
    X31& X = gX31;
    Probe31& P = gP31;
    if (const char* vn = getenv("MGO1_DOLL_PROBE_VICTIM_NAME")) o = PlayerByName(vn);
    if (X.trials.empty()) {
        std::string v = getenv("MGO1_DOLL_PROBE_CQC") ? getenv("MGO1_DOLL_PROBE_CQC") : "h,h,h,s,r";
        for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); if (e > a) X.trials.push_back(v.substr(a, e - a)); a = e + 1; }
        X.at = gFrame;
    }
    if (X.trial >= X.trials.size()) { Probe31Next(); return; }
    const std::string tr = X.trials[X.trial];
    const char kind = tr[0];
    const long long t = gFrame - X.at;
    auto next = [&](const char* why) {
        Log("probe31: cqc %s %zu: %s", tr.c_str(), X.trial, why);
        ++X.trial; X.sub = 0; X.at = gFrame; X.blowAt = X.crossAt = X.grabAt = X.upAt = -1; P.spotHave = false; StopPath(); Steer(0, 0, 0);
    };
    if (!o || !o->alive) {
        Steer(0, 0, 0);
        if (X.sub == 4) {
            Log("probe31: RESULT cqc %s %zu: the victim DEAD %lld frames after the blow (stamina %d -> lowest %d, life %d)", tr.c_str(), X.trial, gFrame - X.blowAt, X.stun0, X.stunMin, X.life0);
            next("over (the victim dead)");
        } else if (X.sub) { Log("probe31: cqc %s %zu: the victim gone (dead): the trial again", tr.c_str(), X.trial); X.sub = 0; X.at = gFrame; StopPath(); }
        return;
    }
    const float dist = Dist2D(gMe.pos, o->pos);
    const bool down = (o->bits[0] & 0x1E000000u) != 0;       // (thrown 0x6000000, lying 0x8000000, knocked down 0x10000000)
    const bool out = ((o->bits[4] >> 25) & 1) != 0;           // (knocked out: the flag 0x99)
    static const long long gap = getenv("MGO1_DOLL_PROBE_CQC_GAP") ? atoll(getenv("MGO1_DOLL_PROBE_CQC_GAP")) : 60;
    switch (X.sub) {
    case 0:                                                  // him up (not down, not knocked out) `gap` frames, me standing
        if (down || out || Low() || (gMe.bits[0] & 4)) { X.at = gFrame; if ((Low() || (gMe.bits[0] & 4)) && gFrame % 30 < 3) gPad.buttons |= 0x4000; Steer(0, 0, 0); return; }
        if (t < gap) return;
        X.sub = 1; X.at = gFrame;
        return;
    case 1: {                                                // the weapon: a CQC weapon (h, p), the AK-47 (s, r)
        const int want = kind == 'h' || kind == 'p' ? CqcCode31() : (Inv().ok && Inv().rounds[0xC] >= 0 ? 0xC : -1);
        if (want >= 0 && Inv().ok && Inv().weapon != want && t < 600) { ProbeWindowTo(0, want); return; }
        X.sub = 2; X.at = gFrame;
        return;
    }
    case 2: {                                                // the place: within 0.55 m of him (r: 5 m away, a straight way)
        if (kind == 'r') {
            if (!P.spotHave && !Spot31(*o, 5000.f, true)) { next("no place 5 m away with a straight way"); return; }
            if (Dist2D(gMe.pos, P.spot) > 600.f && t < 900) { if (!Following() || gFrame % 120 == 0) PlanTo(P.spot); FollowPath(); return; }
            StopPath(); X.sub = 3; X.at = gFrame;
            return;
        }
        const float feet[3] = {o->pos[0], o->pos[1] - 800.f, o->pos[2]};
        if (Go31(feet, 550.f, t)) { X.sub = 3; X.at = gFrame; return; }
        if (t > 2400) next("the victim not reached");
        return;
    }
    case 3:                                                  // facing him, still (r: on at once), then the blow
        if (!Face31(AngleTo(gMe.pos, o->pos), t)) return;
        if (kind != 'r' && (gMe.bits[0] & 0xFFFF) != 3 && t < 150) { Steer(0, 0, 0); return; }
        if (kind != 'r' && dist > 800.f) { X.sub = 2; X.at = gFrame; return; }   // (he moved: closer again)
        X.stun0 = X.stunMin = o->stun; X.life0 = o->life; X.score0 = MyScore(0x36); X.fell = X.ko = false;
        X.blowAt = gFrame; X.crossAt = X.grabAt = X.upAt = -1;
        Log("probe31: cqc %s %zu: the blow (%s) at %.2f m: me %x (in hand %x, type %d); %s %x %x (word 4 %x) stamina %d life %d; my CQC count %d", tr.c_str(), X.trial,
            kind == 'h' ? "Circle held: the hold's throw" : kind == 's' ? "Circle tapped, the AK-47 in hand" : kind == 'p' ? "Circle tapped, a CQC weapon in hand" : "Circle running at him",
            dist / 1000.f, gMe.bits[0], Inv().ok ? Inv().weapon : -1, ProbeMyType(), o->name.c_str(), o->bits[0], o->bits[1], o->bits[4], o->stun, o->life, X.score0);
        X.sub = 4; X.at = gFrame;
        return;
    default: {                                               // the blow, then 600 frames watched
        if (kind == 'h') {
            const bool grab = (gMe.bits[0] & 0xFFFF) == 2 && (gMe.bits[6] & 0x60000000) == 0x60000000;
            if (grab && X.grabAt < 0) { X.grabAt = gFrame; Log("probe31: cqc %s %zu +%lld: grabbed (me %x, 6 %x)", tr.c_str(), X.trial, t, gMe.bits[0], gMe.bits[6]); }
            if (X.crossAt < 0) {
                if ((X.grabAt >= 0 && gFrame - X.grabAt >= 8) || t >= 40) { X.crossAt = gFrame; Log("probe31: cqc %s %zu +%lld: Circle let go (%s)", tr.c_str(), X.trial, t, X.grabAt >= 0 ? "grabbed" : "NO grab"); }
                else gPad.buttons |= 0x2000;
            }
            Steer(0, 0, 0);
        } else if (kind == 's' || kind == 'p') {
            if (t < 3) gPad.buttons |= 0x2000;
            Steer(0, 0, 0);
        } else {
            if (X.crossAt < 0 || gFrame - X.crossAt < 4) Steer(o->pos[0] - gMe.pos[0], o->pos[2] - gMe.pos[2], 1.f); else Steer(0, 0, 0);
            if (X.crossAt < 0 && dist <= 1000.f) { X.crossAt = gFrame; Log("probe31: cqc %s %zu +%lld: Circle at %.2f m (me %x)", tr.c_str(), X.trial, t, dist / 1000.f, gMe.bits[0]); }
            if (X.crossAt >= 0 && gFrame - X.crossAt < 4) gPad.buttons |= 0x2000;
            if (X.crossAt < 0 && t > 300) { next("never within 1.0 m"); return; }
        }
        X.stunMin = std::min(X.stunMin, o->stun);
        if (down) X.fell = true;
        if (out) X.ko = true;
        if (X.fell && !down && !out && X.upAt < 0) X.upAt = gFrame;
        if (t <= 240 && t % 10 == 0)
            Log("probe31: cqc %s %zu +%lld: me %x %x 6 %x; %s %x %x (word 4 %x) stamina %d life %d at %.2f m", tr.c_str(), X.trial, t, gMe.bits[0], gMe.bits[1], gMe.bits[6],
                o->name.c_str(), o->bits[0], o->bits[1], o->bits[4], o->stun, o->life, dist / 1000.f);
        if (t >= 600 || (X.upAt >= 0 && gFrame - X.upAt >= 60)) {
            char up[48] = "";
            if (X.upAt >= 0) snprintf(up, sizeof up, ", up %lld frames after the blow", X.upAt - X.blowAt);
            Log("probe31: RESULT cqc %s %zu: %s: stamina %d -> lowest %d (%d spent), life %d -> %d, %s, %s%s; grab %s, my CQC count %d -> %d (%lld frames watched)",
                tr.c_str(), X.trial, o->name.c_str(), X.stun0, X.stunMin, X.stun0 - X.stunMin, X.life0, o->life, X.fell ? "FELL" : "no fall",
                X.ko ? "KNOCKED OUT" : "not knocked out", up, X.grabAt >= 0 ? "yes" : "no", X.score0, MyScore(0x36), t);
            next("over");
        }
        return;
    }
    }
}
// (v150-host beta 24, cqc) the victim's posture (MGO1_DOLL_PROBE_VICTIM_POSE=crouch|prone|stand, or several by comma, each in turn
// for MGO1_DOLL_PROBE_VICTIM_POSE_FOR frames, 9000 by default) kept while it is up: crouched (Cross), or lying (crouched, then the
// stick pushed 6 frames: the crawl's first move, still again); true: held this frame
bool VictimPose31(const char* list) {
    if (!gMe.obj || !gMe.alive || (gMe.bits[0] & 0x1E000000u) || ((gMe.bits[4] >> 25) & 1)) return false;
    static std::vector<std::string> poses;
    static long long every = 9000;
    if (poses.empty()) {
        std::string v = list;
        for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); if (e > a) poses.push_back(v.substr(a, e - a)); a = e + 1; }
        if (poses.empty()) poses.push_back("stand");
        if (const char* f = getenv("MGO1_DOLL_PROBE_VICTIM_POSE_FOR")) every = std::max(600LL, atoll(f));
    }
    const char* pose = poses[(size_t)(gFrame / every) % poses.size()].c_str();
    static uint32_t said = 0xFFFFFFFF; static long long pushAt = -1000;
    const uint32_t st = gMe.bits[0] & 0xFFFF;
    if (st != said) { said = st; Log("probe31: the victim's posture %x (%s asked, hips %.0f over the ground)", st, pose, HipHeight()); }
    if (!strcmp(pose, "stand")) { if (Low() && gFrame % 30 < 3) gPad.buttons |= 0x4000; return Low(); }
    Steer(0, 0, 0);
    // (Cross held 3 frames, 45 apart, as the brain's PostureHold: a one-frame press was missed by the game, cqc_m2)
    static long long crossAt = -1000;
    if (!Low()) { if (gFrame - crossAt > 45 && (st & 0xFFF0) != 0x40) crossAt = gFrame; if (gFrame - crossAt < 3) gPad.buttons |= 0x4000; return true; }
    if (!strcmp(pose, "prone") && !Prone() && gFrame - pushAt > 90) pushAt = gFrame;
    if (!strcmp(pose, "prone") && gFrame - pushAt < 6) { const float f = Facing(); Steer(std::sin(f), std::cos(f), 1.f); }
    return true;
}

void Probe31Frame() {
    Probe31& P = gP31;
    if (!P.role) {
        const char* r = getenv("MGO1_DOLL_PROBE_ROLE");
        P.role = r && !strcmp(r, "victim") ? 2 : 1;
        const char* s = getenv("MGO1_DOLL_PROBE_STEPS");
        P.steps = s && *s ? s : "ergt";
        Log("probe31: the %s%s%s", P.role == 1 ? "actor, steps " : "victim", P.role == 1 ? P.steps.c_str() : "", "");
    }
    P.newLife = gFrame - P.lastCall > 30;
    P.lastCall = gFrame;
    if (P.newLife) { P.at = gFrame; Log("probe31: a life (the weapon %x, the item %x, support %d)", Inv().weapon, Inv().item, SupportNow()); }
    if (P.newLife || gRoom.phase != 2) gChokeBroken31 = true;   // ((v141 beta 9) the choke step's watch ended by a death or the round)
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    // the team ordered (red, blue): TEAM CHANGE first (the other player is the enemy)
    if ((gSet.team == "red" || gSet.team == "blue") && (gMyTeam == 0 || gMyTeam == 1) && gMyTeam != (gSet.team == "red" ? 0 : 1)) {
        if (ProbeTeamChangeFrame()) return;
    }
    const Player* o = Other31();
    if (const char* vn = getenv("MGO1_DOLL_PROBE_VICTIM_NAME")) if (P.role == 1) o = PlayerByName(vn);   // (v150-host beta 22, wake: the victim by name)
    const long long t = gFrame - P.at;
    const float kD = 3.14159265f / 180.f;
    if (P.role == 2) {                                   // the victim: still, facing the other player, a radio call every 5 s
        if (VictimQ31(o)) return;                        // (its steps hold q: the ladder's climbs first)
        if (VictimPlace31(o)) return;                    // (v150-host beta 24, snakehouse3: MGO1_DOLL_PROBE_D_PLACE)
        Steer(0, 0, 0);
        KoSelf31(o);                                     // (v150-host beta 22, wake: MGO1_DOLL_PROBE_KO)
        // (v150-host beta 22, wake) MGO1_DOLL_PROBE_UP_AWAKE=1: down but not knocked out (a CQC throw, a roll, the combo's kick: the
        // flag 0x99 clear), Cross 2 frames every 4 so that its own game stands it up (a thrown player who presses nothing lay 250 s
        // in wake_m1); knocked out it presses nothing (the wake's measure)
        if (getenv("MGO1_DOLL_PROBE_UP_AWAKE") && gMe.alive && (gMe.bits[0] & 0x1A000000) && !((gMe.bits[4] >> 25) & 1) && gFrame % 4 < 2) gPad.buttons |= 0x4000;
        if (GetUp31()) return;                           // (v150-host beta 22, blast: knocked down, MGO1_DOLL_PROBE_GETUP)
        if (const char* pose = getenv("MGO1_DOLL_PROBE_VICTIM_POSE")) if (VictimPose31(pose)) return;   // (v150-host beta 24, cqc)
        // (v150-host beta 22, blast) MGO1_DOLL_PROBE_VICTIM_VIEW=1: in its first person view (R1 held) facing the other player
        if (getenv("MGO1_DOLL_PROBE_VICTIM_VIEW") && !(gMe.bits[0] & 0x1A000000)) gPad.buttons |= 0x0800;
        if (o && gFrame % 15 == 0) {                     // (the other player's box as this machine sees it)
            FindBoxOf f{o->obj, false}; ps2world::ForEachActor(&BoxOf31, &f);
            if (f.on != P.boxOther) { P.boxOther = f.on; Log("probe31: %s's box %s here (%.1f m away, its state %x %x)", o->name.c_str(), f.on ? "ON" : "off", Dist2D(o->pos, gMe.pos) / 1000.f, o->bits[0], o->bits[1]); }
        }
        if (gRule == 3 && !RadioPressing() && ProbeTakeObjective()) {   // (capture: a KEROTAN held first, again when dropped)
            if (gFrame % 120 == 0) Log("probe31: going for the objective:%s", ProbeObjectives().c_str());
            return;
        }
        // ((v141 beta 6, aim) MGO1_DOLL_PROBE_VICTIM_BOX=1: in its cardboard box, still, no call (the auto aim's lock on a box: step a)
        if (getenv("MGO1_DOLL_PROBE_VICTIM_BOX")) {
            const bool boxed = Inv().ok && Inv().item == 0xE;
            static bool said = false;
            if (boxed != said) { said = boxed; Log("probe31: the victim %s its box", boxed ? "in" : "out of"); }
            if (!boxed) ProbeWindowTo(1, 0xE);
            return;
        }
        // ((v141 beta 9) the choke step's victim: its back to the other player, no call)
        static const bool back = getenv("MGO1_DOLL_PROBE_VICTIM_BRAIN") != nullptr;
        const bool quiet = getenv("MGO1_DOLL_PROBE_VICTIM_QUIET") != nullptr || back;   // ((v141 beta 8) the precision step's still victim)
        if (o) {
            const float want = AngleTo(gMe.pos, o->pos) + (back ? 3.14159265f : 0.f), err = Wrap(want - Facing());
            // (the precision step: turned only when far off, by short pushes: no walk)
            if (quiet ? std::fabs(err) > 40.f * kD && gFrame % 12 < 2 : std::fabs(err) > 10.f * kD && gFrame % 6 < 2 && !RadioPressing())
                Steer(std::sin(want), std::cos(want), quiet ? 0.6f : 1.f);
        }
        if (quiet) return;                                   // (no radio call: its pose kept)
        RadioFrame();
        if (gFrame - P.radioAt >= 300 && !RadioBusy()) { P.radioAt = gFrame; if (!RadioSay(PH_ENEMY, "probe31")) Log("probe31: the radio call not begun (not on the list)"); }
        RadioPad(true, true);
        return;
    }
    if (P.stepIdx < P.steps.size() && P.steps[P.stepIdx] == 'h') { StepH31(); return; }   // (h, v: no other player needed)
    if (P.stepIdx < P.steps.size() && P.steps[P.stepIdx] == 'v') { StepV31(); return; }
    if (P.stepIdx < P.steps.size() && P.steps[P.stepIdx] == 'f') { StepF31(); return; }   // ((v141 beta 8) the view's entry)
    if (P.stepIdx < P.steps.size() && P.steps[P.stepIdx] == 'm') { StepM31(); return; }   // ((v150-host beta 24, cqcfight) full CQC on the enemies)
    if (!o) { Steer(0, 0, 0); return; }
    if (P.stepIdx >= P.steps.size()) { Steer(0, 0, 0); if (gFrame % 600 == 0) Log("probe31: done"); return; }
    const char step = P.steps[P.stepIdx];
    const float dist = Dist2D(gMe.pos, o->pos);
    if (step == 'q') { StepQ31(o); return; }             // a ladder's freeze (Q31)
    if (step == 'z') {                                   // ((v150-host beta 24, stamina2) z: MGO1_DOLL_PROBE_WAIT seconds still (300), then the next step)
        static const long long wait = getenv("MGO1_DOLL_PROBE_WAIT") ? atoll(getenv("MGO1_DOLL_PROBE_WAIT")) : 300;
        static long long from = -1;                      // (its own frames from the step's start: its deaths meanwhile do not start it again)
        Steer(0, 0, 0);
        if (from < 0) { from = gFrame; Log("probe31: waits %lld s (%s %.1f m away)", wait, o->name.c_str(), dist / 1000.f); }
        if (gFrame - from >= wait * 60) {
            // (MGO1_DOLL_PROBE_TEAM_AFTER red or blue: then that team, by the TEAM CHANGE above: a teammate while it waited)
            if (const char* tm = getenv("MGO1_DOLL_PROBE_TEAM_AFTER")) gSet.team = tm;
            Log("probe31: waited %lld s%s%s", wait, getenv("MGO1_DOLL_PROBE_TEAM_AFTER") ? ", now to the team " : "", getenv("MGO1_DOLL_PROBE_TEAM_AFTER") ? gSet.team.c_str() : "");
            Probe31Next();
        }
        return;
    }
    if (step == 'w') { StepW31(o); return; }             // (v150-host beta 22, wake) a teammate's blows on the victim knocked out or reading
    if (step == 'x') { StepX31(o); return; }             // (v150-host beta 24, cqc) CQC throws on the victim
    // (v141 beta 6) k: the match's text chat (a map's chat keyboard, NewSoftwareKeyboard_*: "SELECT opens them",
    // docs/MC_PC_INPUT.md 3). SELECT tapped until a chat keyboard opens (the radio's flags and state logged at each tap),
    // then one way to close it (MGO1_DOLL_PROBE_CLOSE "wait,triangle,cross,circle,start,select,r1" in turn, a trial each):
    // when it closes, and the chat counter (the variable set's +0x34) before and after: a message sent or not
    if (step == 'k') {
        static std::vector<std::string> ways;
        if (ways.empty()) {
            std::string v = getenv("MGO1_DOLL_PROBE_CLOSE") ? getenv("MGO1_DOLL_PROBE_CLOSE") : "wait,triangle,cross,circle,start,select,r1";
            for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); ways.push_back(v.substr(a, e - a)); a = e + 1; }
        }
        if (P.trial >= (int)ways.size()) { Probe31Next(); return; }
        const std::string& way = ways[P.trial];
        static int chat0 = -1, radio0 = -1, taps = 0; static long long openAt = -1, pressAt = -1;
        const uint32_t hud = RadioHud();
        const bool open = ps2world::KeyboardTextEntry();
        auto radioLine = [&]() { char b[96]; snprintf(b, sizeof b, "radio flags %x state %u shown %d", hud ? R32(hud + 0x180) : 0, hud ? R32(hud + 0x78C) : 0, hud ? (int)R32(hud + 0x164) : -1); return std::string(b); };
        Steer(0, 0, 0);
        switch (P.sub) {
        case 0:                                          // calm a moment
            if (t < 120) return;
            taps = 0; chat0 = MyScore(0x34); radio0 = MyScore(0x32);
            Log("probe31: chat %d (%s): %s, keyboard %s, chat counter %d, radio counter %d", P.trial, way.c_str(), radioLine().c_str(), open ? "open" : "closed", chat0, radio0);
            P.sub = 1; P.at = gFrame;
            return;
        case 1:                                          // SELECT taps (4 frames, 60 apart) until the chat keyboard opens
            if (open) { openAt = gFrame; Log("probe31: chat %d: the chat keyboard open after %d SELECT tap%s (%s)", P.trial, taps, taps == 1 ? "" : "s", radioLine().c_str()); P.sub = 2; P.at = gFrame; return; }
            if (t % 60 < 4) gPad.buttons |= 0x0001;
            if (t % 60 == 4) { ++taps; Log("probe31: chat %d: SELECT tap %d: %s, keyboard %s", P.trial, taps, radioLine().c_str(), open ? "open" : "closed"); }
            if (taps >= 6 && t % 60 == 59) { Log("probe31: RESULT chat %d (%s): no chat keyboard after 6 SELECT taps (%s)", P.trial, way.c_str(), radioLine().c_str()); ++P.trial; P.sub = 0; P.at = gFrame; }
            return;
        case 2:                                          // open: a moment, then the way to close it
            if (t < 60) return;
            if (way != "wait") {
                const uint32_t b = way == "triangle" ? 0x1000 : way == "cross" ? 0x4000 : way == "circle" ? 0x2000 : way == "start" ? 0x0008 : way == "select" ? 0x0001 :
                                   way == "r1" ? 0x0800 : 0;
                if (t < 64) { gPad.buttons |= b; if (t == 60) pressAt = gFrame; return; }
            } else if (t == 60) pressAt = gFrame;
            if (!open || t > (way == "wait" ? 960 : 360)) {
                const int c1 = MyScore(0x34);
                Log("probe31: RESULT chat %d (%s): %s, chat counter %d -> %d (%s), radio counter %d -> %d, %s", P.trial, way.c_str(),
                    open ? "still open" : ("closed " + std::to_string(gFrame - pressAt) + " frames after").c_str(), chat0, c1, c1 > chat0 ? "A MESSAGE SENT" : "nothing sent",
                    radio0, MyScore(0x32), radioLine().c_str());
                P.sub = open ? 3 : 4; P.at = gFrame;
            }
            return;
        case 3:                                          // still open: SELECT to close it (its counter said)
            if (t < 4) { gPad.buttons |= 0x0001; return; }
            if (!open || t > 300) { Log("probe31: chat %d: closed by SELECT afterwards: %s, chat counter %d", P.trial, open ? "still open" : "closed", MyScore(0x34)); P.sub = 4; P.at = gFrame; }
            return;
        default:
            if (t < 120) return;
            ++P.trial; P.sub = 0; P.at = gFrame;
            return;
        }
    }
    // (v141 beta 6) r: the radio's menu closed without SELECT (a second SELECT opens the text chat: step k). SELECT tapped once
    // (the radio's flag 0x10), then one way (MGO1_DOLL_PROBE_RCLOSE "wait,cross,triangle,circle,r1,square,l1" in turn): the flag
    // watched 600 frames (its clearing), the chat keyboard (never wanted), the radio counter (+0x32: a phrase sent)
    if (step == 'r') {
        static std::vector<std::string> ways;
        if (ways.empty()) {
            std::string v = getenv("MGO1_DOLL_PROBE_RCLOSE") ? getenv("MGO1_DOLL_PROBE_RCLOSE") : "wait,cross,triangle,circle,r1,square,l1";
            for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); ways.push_back(v.substr(a, e - a)); a = e + 1; }
        }
        if (P.trial >= (int)ways.size()) { Probe31Next(); return; }
        const std::string& way = ways[P.trial];
        static int radio0 = -1; static long long onAt = -1;
        const uint32_t hud = RadioHud();
        const bool flag = hud && (R32(hud + 0x180) & 0x10);
        Steer(0, 0, 0);
        if (ps2world::KeyboardTextEntry()) { if (t % 60 < 4) gPad.buttons |= 0x0001; if (t % 60 == 4) Log("probe31: radio %d (%s): the chat keyboard open: SELECT to close it", P.trial, way.c_str()); return; }
        switch (P.sub) {
        case 0:
            if (t < 120) return;
            radio0 = MyScore(0x32);
            // (v141 beta 6, aim) still on from the last way (the aa3 bench: never off by itself, 25000 frames): this way pressed on it
            if (flag) { onAt = gFrame; Log("probe31: radio %d (%s): the radio still on from before (state %u)", P.trial, way.c_str(), R32(hud + 0x78C)); P.sub = 2; P.at = gFrame; return; }
            P.sub = 1; P.at = gFrame;
            return;
        case 1:                                          // SELECT once
            if (t < 4) { gPad.buttons |= 0x0001; return; }
            if (flag) { onAt = gFrame; Log("probe31: radio %d (%s): the radio on %lld frames after SELECT (state %u)", P.trial, way.c_str(), t, R32(hud + 0x78C)); P.sub = 2; P.at = gFrame; return; }
            if (t > 90) { Log("probe31: RESULT radio %d (%s): the radio not on after SELECT", P.trial, way.c_str()); ++P.trial; P.sub = 0; P.at = gFrame; }
            return;
        case 2: {                                        // 30 frames, the way, then the flag watched
            const uint32_t b = way == "cross" ? 0x4000 : way == "triangle" ? 0x1000 : way == "circle" ? 0x2000 : way == "r1" ? 0x0800 : way == "square" ? 0x8000 :
                               way == "l1" ? 0x0400 : 0;
            if (t >= 30 && t < 34) { gPad.buttons |= b; if (b == 0x8000) gPad.pressures[11] = 0x3F; }
            if (t == 40) Log("probe31: radio %d (%s): 10 frames after the press: radio flags %x state %u, the player's state %x %x", P.trial, way.c_str(), R32(hud + 0x180),
                             R32(hud + 0x78C), gMe.bits[0], gMe.bits[1]);
            if (t > 34 && !flag) {
                Log("probe31: RESULT radio %d (%s): the radio off %lld frames after the press (%lld after SELECT), chat keyboard %s, radio counter %d -> %d, the player's state %x",
                    P.trial, way.c_str(), t - 30, gFrame - onAt, ps2world::KeyboardTextEntry() ? "OPEN" : "closed", radio0, MyScore(0x32), gMe.bits[0]);
                P.sub = 3; P.at = gFrame;
            } else if (t > (getenv("MGO1_DOLL_PROBE_RWATCH") ? atoi(getenv("MGO1_DOLL_PROBE_RWATCH")) : 600) + 30) {
                Log("probe31: RESULT radio %d (%s): the radio still on %lld frames after the press (state %u), radio counter %d -> %d", P.trial, way.c_str(), t - 30,
                    R32(hud + 0x78C), radio0, MyScore(0x32));
                P.sub = 3; P.at = gFrame;
            }
            return;
        }
        default:                                         // ((v141 beta 6, aim) on or off, the next way after a second)
            if (t < 60) return;
            ++P.trial; P.sub = 0; P.at = gFrame;
            return;
        }
    }
    // (v141 beta 8) p: the first person view's precision. At MGO1_DOLL_PROBE_P_DIST metres ("10,20,30,40") from the victim (a clear
    // line), still, the view (R1) aimed by the fights' aim at his head, then his chest (MGO1_DOLL_PROBE_P_SHOTS each, 6): the view
    // steady on the point (0.05 degrees, 10 frames), then Square full 8 frames (one bullet of an automatic gun), the view's error
    // frame by frame; his life 60 frames later
    if (step == 'p') {
        static std::vector<float> dists, aboves;
        static int shots = 6;
        if (dists.empty()) {
            auto list = [](const char* env, const char* def, float k, std::vector<float>& out) {
                std::string v = getenv(env) ? getenv(env) : def;
                for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); out.push_back((float)atof(v.substr(a, e - a).c_str()) * k); a = e + 1; }
            };
            list("MGO1_DOLL_PROBE_P_DIST", "10,20,30,40", 1000.f, dists);
            if (getenv("MGO1_DOLL_PROBE_P_ABOVE")) list("MGO1_DOLL_PROBE_P_ABOVE", "", 1.f, aboves);   // (the heights tried in turn, a shot each)
            if (getenv("MGO1_DOLL_PROBE_P_SHOTS")) shots = std::max(1, atoi(getenv("MGO1_DOLL_PROBE_P_SHOTS")));
        }
        const int per = aboves.empty() ? shots * 2 : shots * (int)aboves.size();
        const int place = P.trial / per, k = P.trial % per;
        if (place >= (int)dists.size()) { Probe31Next(); return; }
        const float D = dists[place];
        const int part = aboves.empty() ? (k < shots ? 0 : 1) : 2;
        const float above = aboves.empty() ? 0.f : aboves[k % aboves.size()];
        static int life0 = -1; static long long steadySince = -1, viewAt = -1; static float errs[10][2];
        float ey = 0, ep = 0, ry = 0, rp = 0;
        switch (P.sub) {
        case 0:                                                       // the place D from him, a clear line
            if (!o->alive) { Steer(0, 0, 0); return; }
            if (!P.spotHave && !Spot31(*o, D, false)) { Log("probe31: precision: no place %.0f m from the victim with a clear line", D / 1000.f); P.trial = (place + 1) * per; P.at = gFrame; return; }
            if (Dist2D(gMe.pos, P.spot) < 600.f) { StopPath(); Steer(0, 0, 0); P.sub = 1; P.at = gFrame; return; }
            if (t > 2400) { Log("probe31: precision: the place %.0f m away not reached", D / 1000.f); P.spotHave = false; P.trial = (place + 1) * per; P.at = gFrame; return; }
            if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
            FollowPath();
            return;
        case 1: {                                                     // facing him, still
            const float want = AngleTo(gMe.pos, o->pos), e = Wrap(want - Facing());
            Steer(0, 0, 0);
            if (std::fabs(e) > 3.f * kD && t < 150) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), 1.f); return; }
            if (t < 60) return;
            P.sub = 2; P.at = gFrame; viewAt = -1; steadySince = -1;
            return;
        }
        case 2:                                                       // R1 and Square light, the aim held steady 10 frames
            gPad.buttons |= 0x0800 | 0x8000; gPad.pressures[11] = 0x3F;
            if (!o->alive) { P.sub = 0; P.spotHave = false; P.at = gFrame; return; }
            if (!(gMe.bits[0] & 0x80)) {
                gPad.sticks[2] = gPad.sticks[3] = 0x80;
                if (t > 150) { Log("probe31: precision %d: the view not on after %lld frames (state %x)", P.trial, t, gMe.bits[0]); P.sub = 1; P.at = gFrame; }
                return;
            }
            if (viewAt < 0) { viewAt = gFrame; ProbeAimReset(); }
            if (!ProbeAimAt(*o, part, &ey, &ep, &ry, &rp, above)) return;
            if (std::fabs(ey) < 0.05f && std::fabs(ep) < 0.05f && std::fabs(ry) < 0.03f && std::fabs(rp) < 0.03f) { if (steadySince < 0) steadySince = gFrame; } else steadySince = -1;
            if ((steadySince >= 0 && gFrame - steadySince >= 10) || t > 400) {
                if (t > 400) Log("probe31: precision %d: not steady after 400 frames (errors %.2f %.2f, turns %.2f %.2f): shot all the same", P.trial, ey, ep, ry, rp);
                life0 = o->life; P.sub = 3; P.at = gFrame;
            }
            return;
        case 3: {                                                     // Square full 8 frames (one bullet), the aim kept; his life after
            gPad.buttons |= 0x0800 | 0x8000; gPad.pressures[11] = t < 8 ? 0xFF : 0x3F;
            ProbeAimAt(*o, part, &ey, &ep, &ry, &rp, above);
            if (t <= 9) {                                             // (the view and the points each frame until the bullet leaves)
                float m[16];
                if (ps2world::ViewMatrix(m)) {
                    const float h = std::max(250.f, o->pos[1] - (MapReady() ? gMap->nav->GroundBelow(o->pos, 2500.f) : o->pos[1] - 800.f));
                    const float ab = above > 0.f ? above : part == 0 ? 1.06f : 0.45f;
                    gB31.point[0] = o->pos[0]; gB31.point[1] = o->pos[1] + h * ab; gB31.point[2] = o->pos[2];
                    gB31.head[0] = o->pos[0]; gB31.head[1] = o->pos[1] + h * 1.06f; gB31.head[2] = o->pos[2];
                    memcpy(gB31.eye, m + 12, 12); memcpy(gB31.fwd, m + 8, 12); memcpy(gB31.right, m + 0, 12); memcpy(gB31.up, m + 4, 12);
                    gB31.have = true;
                }
            }
            if (t < 24) { long long now = gFrame; ps2world::ForEachActor(&Bullet31, &now); }
            if (t < 10) { errs[t][0] = ey; errs[t][1] = ep; }
            if (t < 70) return;
            char b[256]; int n = 0;
            for (int i = 2; i < 10; ++i) n += snprintf(b + n, sizeof b - n, " %.2f/%.2f", errs[i][0], errs[i][1]);
            char what[32]; snprintf(what, sizeof what, "%.2f of his hips' height", above);
            Log("probe31: RESULT precision %d: %.1f m, %s, weapon %x; the view's error (yaw/pitch, degrees) frames 2 to 9:%s; the victim's life %d -> %d%s", P.trial, dist / 1000.f,
                part == 0 ? "head" : part == 1 ? "chest" : what, Inv().ok ? Inv().weapon : -1, b, life0, o->life,
                o->life < life0 ? (o->life <= 0 ? " HIT, KILLED" : " HIT") : " missed");
            ++P.trial; P.at = gFrame;
            P.sub = o->alive && o->life > 0 && P.trial % per != 0 ? 2 : 0;
            if (P.sub == 0) P.spotHave = false;
            steadySince = -1;
            return;
        }
        }
        return;
    }
    // (v150-host beta 24, snakehouse3) d: a doorway's panel between the actor and the victim (a wall face with 0x100000 that the
    // players walk through, docs/VIRTUAL_DOLL.md 43): does a player's bullet pass it, does the auto aim see through it? The actor to
    // MGO1_DOLL_PROBE_D_VIA ("x,y,z", a floor point its mesh reaches), then straight on to MGO1_DOLL_PROBE_D_AT (beyond the panel:
    // walked as a player walks); when the victim stands within 0.6 m of MGO1_DOLL_PROBE_D_VICTIM (his place) a second, facing
    // him: as step a, L1 held with the gun readied (or not: the control) 60 frames, the lock flag counted (the state word 1's
    // 0x1000), then Square full 12 frames (two bullets): his life before and after. MGO1_DOLL_PROBE_D_TRIALS (6: L1, the control)
    if (step == 'd') {
        static float via[3], at[3], vic[3]; static int trials = -1, life0 = -1, lockN = 0, lockFirst = -1, hits = 0, lockHits = 0; static long long vicSince = -1;
        if (trials < 0) {
            trials = getenv("MGO1_DOLL_PROBE_D_TRIALS") ? std::max(1, atoi(getenv("MGO1_DOLL_PROBE_D_TRIALS"))) : 6;
            if (!EnvPoint31("MGO1_DOLL_PROBE_D_VIA", via) || !EnvPoint31("MGO1_DOLL_PROBE_D_AT", at) || !EnvPoint31("MGO1_DOLL_PROBE_D_VICTIM", vic)) {
                Log("probe31: doorway: MGO1_DOLL_PROBE_D_VIA, _D_AT and _D_VICTIM wanted"); Probe31Next(); return;
            }
        }
        if (P.trial >= trials) { Log("probe31: RESULT doorway: %d trials, %d hit him (%d of them with the lock flag)", trials, hits, lockHits); Probe31Next(); return; }
        const bool l1 = P.trial % 2 == 0;
        switch (P.sub) {
        case 0:                                                       // to the way's point on its mesh
            if (Go31(via, 600.f, t)) { Log("probe31: doorway: at the way's point %.0f %.0f %.0f", gMe.pos[0], gMe.pos[1], gMe.pos[2]); P.sub = 1; P.at = gFrame; }
            else if (t > 3600) { Log("probe31: doorway: the way's point not reached (at %.0f %.0f %.0f)", gMe.pos[0], gMe.pos[1], gMe.pos[2]); P.sub = 1; P.at = gFrame; }
            return;
        case 1: {                                                     // straight on through the doorway to its place
            StopPath();
            const float d = Dist2D(gMe.pos, at);
            if (d < 250.f || t > 1200) {
                Steer(0, 0, 0);
                Log("probe31: doorway: %s %.0f %.0f %.0f (%.0f mm from its place)", d < 250.f ? "at its place" : "its place NOT reached, at", gMe.pos[0], gMe.pos[1], gMe.pos[2], d);
                P.sub = 2; P.at = gFrame; vicSince = -1; return;
            }
            Steer(at[0] - gMe.pos[0], at[2] - gMe.pos[2], 0.75f);
            return;
        }
        case 2: {                                                     // the victim at his place a second, faced
            if (Dist2D(gMe.pos, at) > 700.f) { P.sub = 1; P.at = gFrame; return; }
            const bool there = Dist2D(o->pos, vic) < 600.f && o->alive;
            if (!there) vicSince = -1; else if (vicSince < 0) vicSince = gFrame;
            const float want = AngleTo(gMe.pos, o->pos), e = Wrap(want - Facing());
            Steer(0, 0, 0);
            if (std::fabs(e) > 4.f * kD && gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), 1.f);
            if (t % 600 == 0) Log("probe31: doorway: waiting for %s at his place (he at %.0f %.0f %.0f, %.0f mm from it)", o->name.c_str(), o->pos[0], o->pos[1], o->pos[2], Dist2D(o->pos, vic));
            if (vicSince < 0 || gFrame - vicSince < 60 || std::fabs(e) > 4.f * kD) return;
            life0 = o->life; lockN = 0; lockFirst = -1;
            Log("probe31: doorway %d: at %.0f %.0f %.0f, %s at %.0f %.0f %.0f (%.1f m), facing him (error %.1f), %s; weapon %x, state %x %x", P.trial, gMe.pos[0], gMe.pos[1], gMe.pos[2],
                o->name.c_str(), o->pos[0], o->pos[1], o->pos[2], std::hypot(Dist2D(gMe.pos, o->pos), gMe.pos[1] - o->pos[1]) / 1000.f, e / kD, l1 ? "L1 held with the gun readied" : "the gun readied alone (control)", Inv().weapon, gMe.bits[0], gMe.bits[1]);
            P.sub = 3; P.at = gFrame;
            return;
        }
        case 3:                                                       // L1 (or not) + Square light 60 frames, then Square full 12
            Steer(0, 0, 0);
            if (l1) gPad.buttons |= 0x0400;
            gPad.buttons |= 0x8000; gPad.pressures[11] = t >= 60 && t < 72 ? 0xFF : 0x3F;
            if (t < 60 && (gMe.bits[1] & 0x1000)) { ++lockN; if (lockFirst < 0) lockFirst = (int)t; }
            if (t % 10 == 0) Log("probe31 doorway %d +%lld: state %x %x, %s life %d", P.trial, t, gMe.bits[0], gMe.bits[1], o->name.c_str(), o->life);
            if (t < 110) return;
            if (o->life < life0) { ++hits; if (lockN) ++lockHits; }
            Log("probe31: RESULT doorway %d: %s: lock flag %d of 60 frames (from %d); %s's life %d -> %d%s", P.trial, l1 ? "L1" : "control", lockN, lockFirst, o->name.c_str(), life0, o->life,
                o->life < life0 ? " HIT" : " not hit");
            P.sub = 4; P.at = gFrame;
            return;
        default:                                                      // let go, 1 s
            Steer(0, 0, 0);
            if (t < 60) return;
            ++P.trial; P.sub = 2; P.at = gFrame; vicSince = -1;
            return;
        }
    }
    // (v141 beta 6) a: the game's auto aim (L1). At 3, 6, 10 and 15 m from the victim (a clear line), its facing turned 30 and
    // 70 degrees off him, the gun readied (Square light) with L1 held or without (the control): the facing's error every 4
    // frames for 60 frames, the player's work before and 30 frames in (a lock's words), then Square full 12 frames (two
    // bullets): his life before and after. MGO1_DOLL_PROBE_AA_DIST "3,6,10,15" (m), MGO1_DOLL_PROBE_AA_ANG "30,70" (degrees)
    if (step == 'a') {
        static std::vector<float> dists, angs;
        if (dists.empty()) {
            auto list = [](const char* env, const char* def, float k, std::vector<float>& out) {
                std::string v = getenv(env) ? getenv(env) : def;
                for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); out.push_back((float)atof(v.substr(a, e - a).c_str()) * k); a = e + 1; }
            };
            list("MGO1_DOLL_PROBE_AA_DIST", "3,6,10,15", 1000.f, dists);
            list("MGO1_DOLL_PROBE_AA_ANG", "30,70", 1.f, angs);
        }
        const int perPlace = (int)angs.size() * 2;                    // (each angle: L1, then the control)
        const int place = P.trial / perPlace, k = P.trial % perPlace;
        if (place >= (int)dists.size()) { Probe31Next(); return; }
        const float D = dists[place], A = angs[k / 2] * kD;
        const bool l1 = k % 2 == 0;
        static float err0 = 0.f, errAt[16]; static int life0 = -1, side = 1, lockN = 0, lockFirst = -1; static bool turned = false;
        auto errNow = [&]() { return Wrap(AngleTo(gMe.pos, o->pos) - Facing()); };
        switch (P.sub) {
        case 0:                                                       // the place D from him, a clear line
            if (!P.spotHave && !Spot31(*o, D, false)) { Log("probe31: autoaim: no place %.0f m from %s with a clear line", D / 1000.f, o->name.c_str()); P.trial = (place + 1) * perPlace; P.at = gFrame; return; }
            if (Dist2D(gMe.pos, P.spot) < 600.f) { StopPath(); Steer(0, 0, 0); P.sub = 1; P.at = gFrame; return; }
            if (t > 2400) { Log("probe31: autoaim: the place %.0f m away not reached", D / 1000.f); P.spotHave = false; P.trial = (place + 1) * perPlace; P.at = gFrame; return; }
            if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
            FollowPath();
            return;
        case 1: {                                                     // facing A off him (the side alternated), still
            if (t == 0) side = (P.trial % 4 < 2) ? 1 : -1;
            const float want = AngleTo(gMe.pos, o->pos) + side * A, e = Wrap(want - Facing());
            Steer(0, 0, 0);
            if (std::fabs(e) > 4.f * kD && t < 150) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), 1.f); return; }
            if (t < 180) return;
            err0 = errNow(); life0 = o->life; turned = false; lockN = 0; lockFirst = -1;
            Work31(P.s0); Work31(P.s1);
            Log("probe31: autoaim %d: %.1f m from %s, %.0f degrees off him (error %.1f), %s; weapon %x, state %x %x", P.trial, dist / 1000.f, o->name.c_str(), side * A / kD,
                err0 / kD, l1 ? "L1 held with the gun readied" : "the gun readied alone (control)", Inv().weapon, gMe.bits[0], gMe.bits[1]);
            P.sub = 2; P.at = gFrame;
            return;
        }
        case 2: {                                                     // L1 (or not) + Square light 60 frames, then Square full 12
            Steer(0, 0, 0);
            if (l1) gPad.buttons |= 0x0400;
            gPad.buttons |= 0x8000; gPad.pressures[11] = t >= 60 && t < 72 ? 0xFF : 0x3F;
            if (t % 4 == 0 && t / 4 < 16) errAt[t / 4] = errNow();
            if (std::fabs(errNow()) < 5.f * kD) turned = true;
            if (t < 60 && (gMe.bits[1] & 0x1000)) { ++lockN; if (lockFirst < 0) lockFirst = (int)t; }   // (the state word 1's 0x1000: an enemy in the lock)
            if (t == 30) { Work31(P.s2); WorkDiff31(l1 ? "autoaim L1 held 30 frames" : "autoaim readied 30 frames (control)", P.s0, P.s1, P.s1, P.s2); }
            if (t % 10 == 0) Log("probe31 autoaim %d +%lld: error %.1f, state %x %x %x 5 %x 6 %x, %s life %d", P.trial, t, errNow() / kD, gMe.bits[0], gMe.bits[1], gMe.bits[2],
                                 gMe.bits[5], gMe.bits[6], o->name.c_str(), o->life);
            if (t < 110) return;
            char b[256]; int n = 0;
            for (int i = 0; i < 16; ++i) n += snprintf(b + n, sizeof b - n, " %.0f", errAt[i] / kD);
            FindBoxOf fb{o->obj, false}; ps2world::ForEachActor(&BoxOf31, &fb);   // ((v141 beta 6, aim) a boxed victim: MGO1_DOLL_PROBE_VICTIM_BOX)
            Log("probe31: RESULT autoaim %d: %.1f m, %.0f degrees off, %s: the error every 4 frames:%s; %s; lock flag %d of 60 frames (from %d); %s's life %d -> %d%s%s", P.trial,
                dist / 1000.f, err0 / kD, l1 ? "L1" : "control", b, turned ? "FACED HIM" : "not faced", lockN, lockFirst, o->name.c_str(), life0, o->life,
                o->life < life0 ? " HIT" : "", fb.on ? " (his box on)" : "");
            P.sub = 3; P.at = gFrame;
            return;
        }
        default:                                                      // let go, 1 s
            Steer(0, 0, 0);
            if (t < 60) return;
            ++P.trial; P.sub = 1; P.at = gFrame;
            if (P.trial % perPlace == 0) { P.sub = 0; P.spotHave = false; }
            return;
        }
    }
    // (v141 beta 9) c: the choke (players of 14 October: Circle held on a doll until it is knocked out; getting up, its
    // standing up played two or three times, then it turned round and round, unable to kill). Snake (sneaking, the actor
    // blue) at 0.8 m behind the victim (MGO1_DOLL_PROBE_VICTIM_BRAIN=1: its back to him, its own brain from its knock-out
    // on), a CQC weapon in his hands, Circle held until it is knocked out (its state word 0's 0x1A000000, its stamina under
    // 1000) and 60 frames more (15 s at most), let go, 3 m back, 40 s watched: its state every 6 frames, its falls and its
    // rises, its turns once up. MGO1_DOLL_PROBE_CTRIALS (3)
    if (step == 'c') {
        static int trials = -1;
        if (trials < 0) { const char* e = getenv("MGO1_DOLL_PROBE_CTRIALS"); trials = e ? std::max(1, atoi(e)) : 3; }
        if (P.trial >= trials) { Probe31Next(); return; }
        static long long koAt = -1, upAt = -1, grabAt = -1, upSince = -1;
        static int falls = 0, rises = 0, states = 0, myLife0 = 0, life0 = 0;
        static float turned = 0.f, lastFace = 0.f, upFrom[3] = {0, 0, 0}, wentFar = 0.f;
        static bool wasDown = false, said = false;
        static uint32_t lastBits = 0;
        const bool down = (o->bits[0] & 0x1A000000) != 0;
        auto result = [&](const char* how) {
            Log("probe31: RESULT choke %d (%s): grabbed %s, knocked out %s, falls %d, rises %d, up at %s, turned %.0f degrees in its first 10 s up, "
                "%.1f m from where it got up, its state changed %d times; its life %d -> %d, the actor's %d -> %d", P.trial, how, grabAt >= 0 ? "yes" : "NO",
                koAt >= 0 ? "yes" : "NO", falls, rises, upAt >= 0 ? std::to_string((upAt - koAt) / 60.0).substr(0, 5).c_str() : "never", turned / kD, wentFar / 1000.f,
                states, life0, o->life, myLife0, gMe.life);
            ++P.trial; P.sub = 0; P.at = gFrame;
        };
        switch (P.sub) {
        case 0: {                                                     // a CQC weapon in the hands
            if (!ProbeIAmSnake() && t < 2) Log("probe31: choke %d: not Snake (type %d): no grab expected", P.trial, ProbeMyType());
            const int want = CqcCode31();
            if (want >= 0 && Inv().weapon != want && t < 600) {
                if (t == 0) Log("probe31: choke %d: the weapon %x to %x (CQC)", P.trial, Inv().weapon, want);
                if (ProbeWindowTo(0, want)) return;
            }
            P.sub = 1; P.at = gFrame;
            return;
        }
        case 1: {                                                     // 0.8 m behind it
            const float b[3] = {o->pos[0] - std::sin(o->facing) * 800.f, o->pos[1] - 800.f, o->pos[2] - std::cos(o->facing) * 800.f};
            if (t % 300 == 0) Log("probe31: choke %d: to %.0f %.0f %.0f behind %s (%.1f m)", P.trial, b[0], b[1], b[2], o->name.c_str(), dist / 1000.f);
            if (Go31(b, 300.f, t)) { P.sub = 2; P.at = gFrame; return; }
            if (t > 3600) { Log("probe31: choke %d: not behind %s (%.1f m)", P.trial, o->name.c_str(), dist / 1000.f); ++P.trial; P.at = gFrame; }
            return;
        }
        case 2:                                                       // facing it
            if (!Face31(AngleTo(gMe.pos, o->pos), t)) return;
            gChokeBroken31 = false;
            grabAt = koAt = upAt = upSince = -1; falls = rises = states = 0; turned = wentFar = 0.f; wasDown = down; said = false; lastBits = o->bits[0];
            life0 = o->life; myLife0 = gMe.life;
            Log("probe31: choke %d: Circle held at %s, %.2f m, its facing %.0f off ours, its state %x stamina %d", P.trial, o->name.c_str(), dist / 1000.f,
                Wrap(o->facing - Facing()) / kD, o->bits[0], o->stun);
            P.sub = 3; P.at = gFrame;
            return;
        case 3: {                                                     // Circle held
            Steer(0, 0, 0);
            gPad.buttons |= 0x2000;
            const bool grab = (gMe.bits[0] & 0xFFFF) == 2 && (gMe.bits[6] & 0x60000000) == 0x60000000;
            if (grab && grabAt < 0) { grabAt = gFrame; Log("probe31: choke %d: grabbed after %lld frames", P.trial, t); }
            if (down && o->stun < 1000 && koAt < 0) { koAt = gFrame; Log("probe31: choke %d: knocked out after %lld frames (its state %x)", P.trial, t, o->bits[0]); }
            if (t % 10 == 0) QLog31("choke", o);
            if ((koAt >= 0 && gFrame - koAt >= 60) || t >= 900) {
                if (koAt < 0) { Log("probe31: choke %d: not knocked out in 15 s (its stamina %d, state %x)", P.trial, o->stun, o->bits[0]); koAt = gFrame; }
                P.sub = 4; P.at = gFrame;
            }
            return;
        }
        case 4: {                                                     // let go, 3 m back (40 frames), then watched
            if (t < 40) Steer(gMe.pos[0] - o->pos[0], gMe.pos[2] - o->pos[2], 0.7f); else Steer(0, 0, 0);
            if (gChokeBroken31) { result("the actor dead or the round over meanwhile"); return; }
            if (o->bits[0] != lastBits) { ++states; lastBits = o->bits[0]; }
            if (down != wasDown) {
                if (down) ++falls; else { ++rises; if (upAt < 0) upAt = gFrame; }
                Log("probe31 choke %d +%lld: %s %s (state %x, stamina %d, hips %.0f)", P.trial, t, o->name.c_str(), down ? "DOWN again" : "up", o->bits[0], o->stun, o->pos[1]);
                wasDown = down;
            }
            if (!down) { if (upSince < 0) { upSince = gFrame; lastFace = o->facing; memcpy(upFrom, o->pos, sizeof upFrom); } }
            else upSince = -1;
            if (upSince >= 0 && gFrame - upSince <= 600) {
                turned += std::fabs(Wrap(o->facing - lastFace)); lastFace = o->facing;
                wentFar = std::max(wentFar, Dist2D(o->pos, upFrom));
            }
            if (t % 6 == 0) Log("probe31 choke %d +%lld: %s %x %x %x 6 %x life %d stun %d at %.0f %.0f %.0f facing %.0f (%.1f m)", P.trial, t, o->name.c_str(), o->bits[0],
                                o->bits[1], o->bits[2], o->bits[6], o->life, o->stun, o->pos[0], o->pos[1], o->pos[2], o->facing / kD, dist / 1000.f);
            if (!o->alive && !said) { said = true; Log("probe31: choke %d: %s dead at +%lld", P.trial, o->name.c_str(), t); }
            if (t >= 2400) result("40 s watched");
            return;
        }
        }
        return;
    }
    if (step == 'e') {                                   // the emote: Triangle held
        static const int kHold[] = {120, 4, 30, 240};
        if (P.trial >= 4) { Probe31Next(); return; }
        const int hold = kHold[P.trial];
        Steer(0, 0, 0);
        if (P.sub == 0) {                                // (facing the other player first, 2 s at most: an emote made at it)
            const float want = AngleTo(gMe.pos, o->pos), err = Wrap(want - Facing());
            if (P.faceSince < 0) P.faceSince = gFrame;
            if (std::fabs(err) > 8.f * kD && gFrame - P.faceSince < 120) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), 1.f); P.at = gFrame - std::min<long long>(t, 9); return; }
            if (t == 10) Work31(P.s0);
            if (t < 40) return;
            Work31(P.s1);
            Log("probe31: emote %d: Triangle held %d frames (state %x, facing %.0f, %s %.1f m away)", P.trial, hold, gMe.bits[0], Facing() / kD, o->name.c_str(), dist / 1000.f);
            P.sub = 1; P.at = gFrame; P.faceSince = -1; return;
        }
        if (t < hold) gPad.buttons |= 0x1000;
        if (t == 20) Work31(P.s2);
        if (t == hold + 30) Work31(P.s3);
        if (t < hold + 300) return;
        WorkDiff31("emote held (from 20 frames in)", P.s0, P.s1, P.s1, P.s2);
        WorkDiff31("emote after (30 frames after the release)", P.s0, P.s1, P.s1, P.s3);
        ++P.trial; P.sub = 0; P.at = gFrame;
        return;
    }
    // (v150-host beta 22, mash) also o: the radio's step above took r since v141 beta 6, so the roll was never reached
    if (step == 'r' || step == 'o') {                    // the roll into it
        static const float kAt[] = {1800.f, 1600.f, 2200.f, 1400.f, 2000.f, 1700.f};
        if (P.trial >= 6) { Probe31Next(); return; }
        const float at = kAt[P.trial];
        switch (P.sub) {
        case 0:                                          // 8 m from it, a straight way to it
            if (!P.spotHave && !Spot31(*o, 8000.f, true)) { Log("probe31: roll %d: no place 8 m from %s with a straight way", P.trial, o->name.c_str()); ++P.trial; P.at = gFrame; return; }
            if (Dist2D(gMe.pos, P.spot) < 600.f) { StopPath(); Steer(0, 0, 0); P.sub = 1; P.at = gFrame; return; }
            if (t > 2400) { Log("probe31: roll %d: the place not reached", P.trial); P.spotHave = false; ++P.trial; P.at = gFrame; return; }
            if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
            FollowPath();
            return;
        case 1: {                                        // still, facing it
            const float want = AngleTo(gMe.pos, o->pos), err = Wrap(want - Facing());
            Steer(0, 0, 0);
            if (std::fabs(err) > 4.f * kD && t < 120) { if (gFrame % 6 < 2) Steer(std::sin(want), std::cos(want), 1.f); return; }
            if (t < 140) return;
            Work31(P.s1);
            P.rollsBefore = MyScore(0x46); P.victimId = o->id; P.knocked = false; P.minDist = dist; P.pressAt = -1;
            Log("probe31: roll %d: running at %s from %.1f m, Cross %.1f m before it (state %x, %s's %x); objectives:%s", P.trial, o->name.c_str(), dist / 1000.f, at / 1000.f, gMe.bits[0],
                o->name.c_str(), o->bits[0], ProbeObjectives().c_str());
            P.sub = 2; P.at = gFrame;
            return;
        }
        case 2: {                                        // the run, Cross at the distance
            if (P.pressAt < 0 || gFrame - P.pressAt < 30) Steer(o->pos[0] - gMe.pos[0], o->pos[2] - gMe.pos[2], 1.f); else Steer(0, 0, 0);
            if (P.pressAt < 0 && dist <= at) { P.pressAt = gFrame; Log("probe31: roll %d: Cross at %.2f m (state %x, speed %s)", P.trial, dist / 1000.f, gMe.bits[0], ""); }
            if (P.pressAt >= 0 && gFrame - P.pressAt < 4) gPad.buttons |= 0x4000;
            P.minDist = std::min(P.minDist, dist);
            if (o->id == P.victimId && ((o->bits[0] & 0x1A000000) || o->down)) P.knocked = true;
            if (P.pressAt >= 0) {
                const long long r = gFrame - P.pressAt;
                if (r == 20) Work31(P.s2);
                if (r <= 100) Log("probe31 roll %d +%lld: me %x %x %x 6 %x at %.2f m; %s %x %x down %d life %d stun %d", P.trial, r, gMe.bits[0], gMe.bits[1], gMe.bits[2], gMe.bits[6],
                                  dist / 1000.f, o->name.c_str(), o->bits[0], o->bits[1], o->down ? 1 : 0, o->life, o->stun);
                if (r >= 150) {
                    Log("probe31: RESULT roll %d (Cross at %.1f m): rolls %d -> %d, closest %.2f m, %s %s (its state %x)", P.trial, at / 1000.f, P.rollsBefore, MyScore(0x46),
                        P.minDist / 1000.f, o->name.c_str(), P.knocked ? "KNOCKED DOWN" : "not knocked down", o->bits[0]);
                    Log("probe31: after roll %d, objectives:%s", P.trial, ProbeObjectives().c_str());
                    ++P.trial; P.sub = 0; P.at = gFrame; P.spotHave = false;
                }
            } else if (t > 300) { Log("probe31: roll %d: never within %.1f m (%.1f m)", P.trial, at / 1000.f, dist / 1000.f); ++P.trial; P.sub = 0; P.at = gFrame; P.spotHave = false; }
            return;
        }
        }
        return;
    }
    if (step == 'g') {                                   // the grenades: a life each (the support row: 2 stun, 3 chaff, 4 smoke)
        // (MGO1_DOLL_PROBE_GRENADES, a throw a character: 0 a stun grenade 2.5 m before the victim, 3 one at its
        // feet, 1 a chaff at its feet, 2 a smoke half way)
        // (v150-host beta 21) 4: an empty magazine (0x18: the ones this life's reloads left, the step l before it) at the
        // victim's chest from 5 m, with no new life (a new life has none); 5 a WP grenade and 6 a grenade at its feet
        // (v150-host beta 22, blast) 7 a grenade 2.5 m before the victim, 8 one 5 m before it, 9 a stun grenade 6 m before it
        static const int kSup[10] = {2, 3, 4, 2, -1, 1, 0, 0, 0, 2}, kCode[10] = {0x15, 0x16, 0x17, 0x15, 0x18, 0x14, 0x13, 0x13, 0x13, 0x15};
        static const char* kName[10] = {"stun", "chaff", "smoke", "stun (at its feet)", "empty magazine: not a", "WP", "frag", "frag (2.5 m before it)", "frag (5 m before it)", "stun (6 m before it)"};
        static const char* kinds = getenv("MGO1_DOLL_PROBE_GRENADES") ? getenv("MGO1_DOLL_PROBE_GRENADES") : "012";
        while (P.gIdx < (int)strlen(kinds) && (kinds[P.gIdx] < '0' || kinds[P.gIdx] > '9')) ++P.gIdx;
        if (P.gIdx >= (int)strlen(kinds)) { Probe31Next(); return; }
        P.grenade = kinds[P.gIdx] - '0';
        const int code = kCode[P.grenade];
        switch (P.sub) {
        case 0:                                          // the loadout, then SUICIDE (the next life has it)
            if (P.grenade == 4) { P.sub = 1; P.at = gFrame; return; }   // (the magazine: this life's)
            // (v150-host beta 22, wake) MGO1_DOLL_PROBE_KEEP=1: that grenade had already, no new life (three stun grenades in a row)
            if (getenv("MGO1_DOLL_PROBE_KEEP") && Inv().ok && Inv().rounds[code] > 0) { P.sub = 1; P.at = gFrame; return; }
            if (t == 1) { gSet.loadout = "ak47,m1911,s" + std::to_string(kSup[P.grenade]); Log("probe31: next life with the %s grenade (%s)", kName[P.grenade], gSet.loadout.c_str()); }
            if (P.job && P.newLife) { P.job = false; P.sub = 1; P.at = gFrame; return; }   // (died by it: the next life)
            if (ProbeSuicideFrame()) { P.job = true; return; }
            if (P.job || t > 200) { P.job = false; P.sub = 1; P.at = gFrame; }
            return;
        case 1:                                          // a new life: a place 10 m from it with a clear line
            if (t < 60) return;
            if (!Inv().ok || Inv().rounds[code] <= 0) {
                if (t > 600) { Log("probe31: no %s grenade this life (support %d; the weapon's rounds %d)", kName[P.grenade], SupportNow(), Inv().ok ? Inv().rounds[code] : -99); ++P.gIdx; P.sub = 0; P.at = gFrame; }
                return;
            }
            if (!P.spotHave && !Spot31(*o, P.grenade == 4 ? 5000.f : 10000.f, false)) { Log("probe31: no place 10 m from %s", o->name.c_str()); ++P.gIdx; P.sub = 0; P.at = gFrame; return; }
            if (Dist2D(gMe.pos, P.spot) < 700.f) { StopPath(); Steer(0, 0, 0); P.sub = 2; P.at = gFrame; return; }
            if (t > 3000) { Log("probe31: the throwing place not reached"); P.spotHave = false; P.sub = 2; P.at = gFrame; return; }
            if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
            FollowPath();
            return;
        case 2: {                                        // the throw: stun 2.5 m before it (in its sight), chaff at its feet, smoke half way
            float pt[3];
            const float ux = (gMe.pos[0] - o->pos[0]) / std::max(1.f, dist), uz = (gMe.pos[2] - o->pos[2]) / std::max(1.f, dist);
            const float k = P.grenade == 0 || P.grenade == 7 ? 2500.f : P.grenade == 8 ? 5000.f : P.grenade == 9 ? 6000.f : P.grenade == 2 ? dist * 0.5f : 0.f;
            pt[0] = o->pos[0] + ux * k; pt[2] = o->pos[2] + uz * k;
            pt[1] = (MapReady() ? gMap->nav->GroundBelow(o->pos, 2500.f) : o->pos[1] - 800.f) + (P.grenade == 4 ? 1100.f : 60.f);   // (the magazine: its chest)
            if (t == 0 && P.grenade == 4) Log("probe31: the magazine's throw: %s's life %d stun %d state %x %x, %d magazines had", o->name.c_str(), o->life, o->stun, o->bits[0], o->bits[1], Inv().ok ? Inv().rounds[code] : -99);
            if (t == 0) Log("probe31: %s grenade from %.1f m (%s at %.0f %.0f %.0f facing %.0f)", kName[P.grenade], dist / 1000.f, o->name.c_str(), o->pos[0], o->pos[1], o->pos[2], o->facing / kD);
            if (ProbeThrowAt(pt, code, P.predicted)) return;
            Log("probe31: %s grenade %s", kName[P.grenade], t < 5 ? "NOT thrown (no throw from here)" : "thrown");
            P.sub = 3; P.at = gFrame; memcpy(P.spot, pt, sizeof P.spot);
            return;
        }
        default:                                         // watched 20 s (the smoke 40 s); the stun's thrower turned away; a radio call every 5 s
            Steer(0, 0, 0);
            if ((P.grenade == 0 || P.grenade == 3 || P.grenade == 9) && t < 400) {
                const float away = AngleTo(P.spot, gMe.pos), err = Wrap(away - Facing());
                if (t == 1) Log("probe31: turning away from the stun grenade");
                if (std::fabs(err) > 15.f * kD && gFrame % 6 < 2) Steer(std::sin(away), std::cos(away), 1.f);
            }
            RadioFrame();
            if (gFrame - P.radioAt >= 300 && !RadioBusy()) { P.radioAt = gFrame; RadioSay(PH_ENEMY, "probe31 actor"); }
            RadioPad(true, true);
            if (P.grenade >= 4 && (t == 60 || t == 180 || t == 400 || (P.grenade > 4 && t % 300 == 0))) Log("probe31: the %s +%lld: %s's life %d stun %d state %x %x down %d", P.grenade == 4 ? "magazine" : kName[P.grenade], t, o->name.c_str(), o->life, o->stun, o->bits[0], o->bits[1], o->down ? 1 : 0);
            if (t < (P.grenade == 2 ? 2400 : P.grenade == 4 ? 420 : 1200)) return;
            RadioStop("probe31");
            ++P.gIdx; P.sub = 0; P.at = gFrame; P.spotHave = false;
            return;
        }
    }
    if (step == 't') {                                   // the thermal goggles: the weapon boxes in turn until had, on 60 s, off 30 s
        const Inventory& v = Inv();
        const bool had = v.ok && v.count[7] != 0;
        switch (P.sub) {
        case 0: {                                        // the nearest weapon box not tried yet
            if (had) { Log("probe31: the thermal goggles had (items:%s)", ""); P.sub = 2; P.at = gFrame; Work31(P.s1); return; }
            std::vector<std::array<float, 3>> boxes;
            ps2world::ForEachActor([](uint32_t w, const std::string& name, void* ctx) {
                if (name != "NewItemBox2") return;
                const float p[3] = {RF(w + 0x1A0), RF(w + 0x1A4), RF(w + 0x1A8)};
                if (std::fabs(p[0]) < 1e6f && std::fabs(p[2]) < 1e6f && p[1] > -50000.f) ((std::vector<std::array<float, 3>>*)ctx)->push_back({p[0], p[1], p[2]});
            }, &boxes);
            float bd = 1e30f; bool any = false;
            for (const auto& b : boxes) {
                bool tried = false;
                for (const auto& q : P.tried) if (Dist2D(q.data(), b.data()) < 500.f) tried = true;
                if (tried || Dist2D(gMe.pos, b.data()) >= bd) continue;
                bd = Dist2D(gMe.pos, b.data()); memcpy(P.spot, b.data(), sizeof P.spot); any = true;
            }
            if (!any || getenv("MGO1_DOLL_PROBE_GIVE")) {
                // (tests: the goggles written into the equipment's list and their state's count, as a box gives them)
                const uint32_t eq = EquipBase(), st = ItemState(7);
                const int n = eq ? (int)R32(eq + 0x5B0) : -1;
                std::string list;
                int at = -1;
                int free = -1;
                for (int k = 0; k < n && k < 16; ++k) {       // (8 slots, the empty ones 0)
                    const int c = R16s(eq + 0x5C4 + 2 * k); char b[16]; snprintf(b, sizeof b, " %x", c); list += b;
                    if (c == 7) at = k;
                    if (c == 0 && free < 0) free = k;
                }
                if (at < 0) at = free;
                if (!eq || !st || at < 0) { Log("probe31: the thermal goggles not given (equipment %08x state %08x, %d items:%s)", eq, st, n, list.c_str()); Probe31Next(); return; }
                const uint16_t seven = 7, one = 1;
                memcpy(ps2::gMem + ((eq + 0x5C4 + 2 * at) & 0x01FFFFFF), &seven, 2);
                memcpy(ps2::gMem + ((st + 0x18) & 0x01FFFFFF), &one, 2);
                Log("probe31: the thermal goggles given (the list:%s; in its slot %d; their state %08x)", list.c_str(), at, st);
                Dump31("given", "state", st, 0x38);
                return;
            }
            Log("probe31: to the weapon box at %.0f %.0f %.0f (%.1f m)", P.spot[0], P.spot[1], P.spot[2], bd / 1000.f);
            P.sub = 1; P.at = gFrame;
            return;
        }
        case 1: {                                        // to it (the last steps straight onto it), then a second there
            const float d = Dist2D(gMe.pos, P.spot);
            if (d < 250.f || t > 3000) {
                Steer(0, 0, 0);
                if (P.pressAt < 0) P.pressAt = gFrame;
                if (gFrame - P.pressAt < 60) return;
                P.pressAt = -1; P.tried.push_back({P.spot[0], P.spot[1], P.spot[2]});
                Log("probe31: the box at %.0f %.0f %.0f %s: goggles %s", P.spot[0], P.spot[1], P.spot[2], d < 250.f ? "touched" : "not reached", had ? "HAD" : "not had");
                P.sub = 0; P.at = gFrame;
                return;
            }
            if (d < 900.f) { StopPath(); Steer(P.spot[0] - gMe.pos[0], P.spot[2] - gMe.pos[2], 1.f); return; }
            if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
            FollowPath();
            return;
        }
        case 2:                                          // on (the item window)
            Steer(0, 0, 0);
            if (v.item == 7 && t > 20) { Log("probe31: the thermal goggles on"); P.sub = 3; P.at = gFrame; return; }
            if (!ProbeWindowTo(1, 7) && t > 400) { Log("probe31: the thermal goggles NOT put on"); Probe31Next(); }
            return;
        case 3:                                          // 60 s on: its state and the work every 5 s
            Steer(0, 0, 0);
            if (t % 300 == 0) {
                if (const uint32_t st = ItemState(7)) Dump31("goggles on", "state", st, 0x38);
                Work31(P.s2); WorkDiff31("goggles on", P.s1, P.s1, P.s1, P.s2);
            }
            if (t < 3600 && v.item == 7) return;
            if (v.item != 7) Log("probe31: the thermal goggles off by themselves after %lld frames", t);
            P.sub = 4; P.at = gFrame;
            return;
        case 4:                                          // off
            Steer(0, 0, 0);
            if (v.item == 0 && t > 20) { Log("probe31: the thermal goggles off"); P.sub = 5; P.at = gFrame; return; }
            if (!ProbeWindowTo(1, 0) && t > 400) { P.sub = 5; P.at = gFrame; }
            return;
        default:                                         // 30 s off
            Steer(0, 0, 0);
            if (t % 300 == 0) if (const uint32_t st = ItemState(7)) Dump31("goggles off", "state", st, 0x38);
            if (t < 1800) return;
            Probe31Next();
            return;
        }
    }
    if (step == 'p') {                                   // a pocket of the mesh (MGO1_DOLL_PROBE_AT "x y z"): into it from its rim, then out by EscapeFrame, 3 times
        static float at[3] = {0, 0, 0}; static bool atHave = false;
        if (!atHave) {                                   // ("x,y,z")
            const char* e = getenv("MGO1_DOLL_PROBE_AT");
            if (!e || sscanf(e, "%f,%f,%f", &at[0], &at[1], &at[2]) != 3) { Log("probe31: pocket: no MGO1_DOLL_PROBE_AT"); Probe31Next(); return; }
            atHave = true;
        }
        float pm[3]; const bool pOn = gMap->nav->Nearest(at, 1000.f, pm);
        const int pocket = pOn ? gMap->nav->ComponentAt(pm) : -1;
        float g[3]; const bool gOn = Ground(g);
        const int here = gOn ? gMap->nav->ComponentAt(g) : -2;
        if (P.trial >= 3 || pocket < 0) { if (pocket < 0) Log("probe31: pocket: %.0f %.0f %.0f not on the mesh", at[0], at[1], at[2]); Probe31Next(); return; }
        switch (P.sub) {
        case 0: {                                        // the rim: the nearest point of a larger part around it (the trial's own: the nearest, the next...)
            if (!P.spotHave) {
                std::vector<std::pair<float, std::array<float, 3>>> rim;
                for (int a = 0; a < 32; ++a)
                    for (float r = 1000.f; r <= 6000.f; r += 500.f)
                        for (float dy : {0.f, 1200.f, 2400.f, -1200.f}) {
                            const float c[3] = {at[0] + std::sin(a * 0.19635f) * r, at[1] + dy, at[2] + std::cos(a * 0.19635f) * r};
                            float q[3];
                            if (!gMap->nav->Nearest(c, 400.f, q)) continue;
                            const int cc = gMap->nav->ComponentAt(q);
                            if (cc == pocket || gMap->nav->ComponentSize(cc) < 20 || std::fabs(q[1] - at[1]) > 2500.f) continue;   // (not a floor above it)
                            rim.push_back({Dist2D(q, at), {q[0], q[1], q[2]}});
                        }
                std::sort(rim.begin(), rim.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
                std::vector<std::array<float, 3>> picks;
                for (const auto& r : rim) { bool apart = true; for (const auto& q : picks) if (Dist2D(q.data(), r.second.data()) < 1500.f) apart = false; if (apart) picks.push_back(r.second); }
                if (picks.empty()) { Log("probe31: pocket: no rim"); Probe31Next(); return; }
                if (P.rimIdx >= picks.size()) { Log("probe31: pocket: no rim to get in from"); Probe31Next(); return; }
                const auto& pk = picks[P.rimIdx];
                memcpy(P.spot, pk.data(), sizeof P.spot); P.spotHave = true;
                Log("probe31: pocket %d: its part %d (%d polygons), the rim at %.0f %.0f %.0f (%.1f m from it)", P.trial, pocket, gMap->nav->ComponentSize(pocket), P.spot[0], P.spot[1], P.spot[2],
                    Dist2D(P.spot, at) / 1000.f);
            }
            if (Dist2D(gMe.pos, P.spot) < 600.f) { StopPath(); P.sub = 1; P.at = gFrame; return; }
            if (t > 3600) { Log("probe31: pocket %d: the rim not reached", P.trial); ++P.trial; P.spotHave = false; P.at = gFrame; return; }
            if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
            FollowPath();
            return;
        }
        case 1:                                          // in: the stick toward it (off the rim, through a door) until on its part
            Steer(at[0] - gMe.pos[0], at[2] - gMe.pos[2], 1.f);
            if (t % 20 < 3 && t > 60) gPad.buttons |= 0x1000;
            if (here == pocket && gMe.pos[1] - pm[1] < 1100.f) {   // (on its floor: not on a step above it)
                Log("probe31: pocket %d: in after %lld frames at %.0f %.0f %.0f", P.trial, t, gMe.pos[0], gMe.pos[1], gMe.pos[2]); StopPath(); Steer(0, 0, 0); EscapeForget(); P.sub = 2; P.at = gFrame;
                return;
            }
            if (t > 600) { Log("probe31: pocket %d: NOT in from that rim (at %.0f %.0f %.0f, part %d)", P.trial, gMe.pos[0], gMe.pos[1], gMe.pos[2], here); ++P.rimIdx; P.sub = 0; P.spotHave = false; P.at = gFrame; }
            return;
        default:                                         // out: the brain's escape
            if (t < 60) { Steer(0, 0, 0); return; }
            if (EscapeFrame()) return;
            Log("probe31: RESULT pocket %d: %s after %.1f s (at %.0f %.0f %.0f, part %d of %d polygons)", P.trial, here != pocket ? "OUT" : "still in", (t - 60) / 60.0, gMe.pos[0], gMe.pos[1],
                gMe.pos[2], here, gOn ? gMap->nav->ComponentSize(here) : 0);
            EscapeForget(); ++P.trial; P.sub = 0; P.spotHave = false; P.at = gFrame;
            return;
        }
    }
    if (step == 'l') {                                   // the reload: the AK-47's magazine by each way tried
        static const char* kWay[] = {"R2 tapped twice, 14 frames apart", "R2 tapped, again 64 frames later", "the window to the M1911A1 and back",
                                     "the window held open and let go on it", "the magazine emptied, the trigger pulled, the stick full meanwhile"};
        const int nWays = 5;
        const Inventory& v = Inv();
        int size = -1, rounds = -1;
        const int m = MagOf(0xC, &size, &rounds);
        if (P.trial >= nWays) { Probe31Next(); return; }
        const bool r80 = (gMe.bits[1] & 0x80) != 0;
        switch (P.sub) {
        case 0: {                                        // the AK-47 in the hands, turned away from the other player
            if (!v.ok || v.rounds[0xC] < 0) { if (t > 600) { Log("probe31: reload: no AK-47"); Probe31Next(); } return; }
            if (v.weapon != 0xC) { if (!ProbeWindowTo(0, 0xC) && t > 600) { Log("probe31: reload: the AK-47 not taken"); Probe31Next(); } return; }
            const float away = AngleTo(o->pos, gMe.pos), err = Wrap(away - Facing());
            Steer(0, 0, 0);
            if (std::fabs(err) > 10.f * kD && t < 150) { if (gFrame % 6 < 2) Steer(std::sin(away), std::cos(away), 1.f); return; }
            if (t < 170) return;
            Log("probe31: reload %d (%s): magazine %d of %d, rounds %d, state %x %x", P.trial, kWay[P.trial], m, size, rounds, gMe.bits[0], gMe.bits[1]);
            P.sub = 1; P.at = gFrame; P.magBefore = m; P.reloadAt = P.reloadEnd = -1;
            return;
        }
        case 1: {                                        // shots: readied 20 frames, full until 8 rounds are gone (the last way: all of them)
            Steer(0, 0, 0);
            const bool all = P.trial == 4;
            const int want = all ? 0 : std::max(0, P.magBefore - 8);
            if (r80 && P.reloadAt < 0) { P.reloadAt = gFrame; Log("probe31: reload %d: reloading while shooting (magazine %d, frame %lld)", P.trial, m, t); }
            if (t < 20) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; return; }
            if (m > want && t < (all ? 700 : 240)) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; return; }
            Log("probe31: reload %d: shot to %d (rounds %d) in %lld frames, state %x %x", P.trial, m, rounds, t, gMe.bits[0], gMe.bits[1]);
            P.sub = 2; P.at = gFrame; P.magBefore = m; P.lastWeapon = v.weapon; P.lastMag = m; P.job = false;
            memcpy(P.moveFrom, gMe.pos, sizeof P.moveFrom); P.moveDir = OpenWay31();
            return;
        }
        default: {                                       // the way tried, then watched
            if (v.weapon != P.lastWeapon || m != P.lastMag) {
                Log("probe31: reload %d +%lld: weapon %x, magazine %d, rounds %d, state %x %x", P.trial, t, v.weapon, m, rounds, gMe.bits[0], gMe.bits[1]);
                P.lastWeapon = v.weapon; P.lastMag = m;
            }
            if (r80 && P.reloadAt < 0) P.reloadAt = gFrame;
            if (!r80 && P.reloadAt >= 0 && P.reloadEnd < 0) P.reloadEnd = gFrame;
            Steer(0, 0, 0);
            long long end = 300;
            switch (P.trial) {
            case 0: if ((t >= 30 && t < 34) || (t >= 44 && t < 48)) gPad.buttons |= 0x0200; break;
            case 1: if ((t >= 30 && t < 34) || (t >= 94 && t < 98)) gPad.buttons |= 0x0200; break;
            case 2:
                end = 900;
                if (t < 30) break;
                if (!P.job) { if (v.weapon == 6) { P.job = true; Log("probe31: reload 2: the M1911A1 in the hands at +%lld", t); } else if (!ProbeWindowTo(0, 6) && t > 500) P.job = true; }
                else if (v.weapon != 0xC) ProbeWindowTo(0, 0xC);
                break;
            case 3: if (t >= 30 && t < 100) gPad.buttons |= 0x0200; break;
            default:
                if (t >= 30 && t < 50) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
                else if (t >= 50 && t < 60) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }
                if (t >= 20 && t < 200) Steer(std::sin(P.moveDir), std::cos(P.moveDir), 1.f);
                if (t == 200) Log("probe31: reload 4: moved %.2f m in 180 frames with the stick full (state %x %x)", Dist2D(P.moveFrom, gMe.pos) / 1000.f, gMe.bits[0], gMe.bits[1]);
                break;
            }
            if (t < end) return;
            Log("probe31: RESULT reload %d (%s): magazine %d -> %d of %d, rounds %d, reloading %s%lld frames, weapon %x", P.trial, kWay[P.trial], P.magBefore, m, size, rounds,
                P.reloadAt >= 0 ? "" : "never seen, ", P.reloadAt >= 0 ? (P.reloadEnd >= 0 ? P.reloadEnd : gFrame) - P.reloadAt : 0, v.weapon);
            ++P.trial; P.sub = 0; P.at = gFrame;
            return;
        }
        }
    }
    if (step == 'b') {                                   // the cardboard box: on, still, moving (full, 0.6), Square, R2 twice, off; the same moves without it
        const Inventory& v = Inv();
        int size = -1, rounds = -1;
        const int m = MagOf(0xC, &size, &rounds);
        switch (P.sub) {
        case 0:                                          // on
            Steer(0, 0, 0);
            if (!v.ok || v.count[0xE] == 0) { if (t > 300) { Log("probe31: box: not had"); Probe31Next(); } return; }
            if (v.item == 0xE && t > 10) { Log("probe31: box: on in %lld frames (state %x %x, weapon %x)", t, gMe.bits[0], gMe.bits[1], v.weapon); P.sub = 1; P.at = gFrame; return; }
            if (!ProbeWindowTo(1, 0xE) && t > 400) { Log("probe31: box: NOT put on"); Probe31Next(); }
            return;
        case 1: case 2: case 6: case 7: {                // 60 frames still, then 90 with the stick (1, 6 full; 2, 7 at 0.6): the box on (1, 2), off (6, 7)
            const float k = P.sub == 1 || P.sub == 6 ? 1.f : 0.6f;
            if (t == 0) { memcpy(P.moveFrom, gMe.pos, sizeof P.moveFrom); P.moveDir = OpenWay31(); }
            if (t < 60) { Steer(0, 0, 0); if (t == 59) memcpy(P.moveFrom, gMe.pos, sizeof P.moveFrom); return; }
            Steer(std::sin(P.moveDir), std::cos(P.moveDir), k);
            if (t == 90) Log("probe31: box %s, the stick at %.1f: state %x %x %x, facing %.0f (asked %.0f)", P.sub < 6 ? "on" : "off", k, gMe.bits[0], gMe.bits[1], gMe.bits[2], Facing() / kD, P.moveDir / kD);
            if (t < 150) return;
            Log("probe31: RESULT box %s, the stick at %.1f: %.2f m in 90 frames (%.1f mm a frame), still on %d", P.sub < 6 ? "on" : "off", k, Dist2D(P.moveFrom, gMe.pos) / 1000.f,
                Dist2D(P.moveFrom, gMe.pos) / 90.f, v.item == 0xE ? 1 : 0);
            Steer(0, 0, 0);
            ++P.sub; P.at = gFrame;
            if (P.sub == 8) Probe31Next();
            return;
        }
        case 3: {                                        // Square: readied 20, full 30: the bullets and the magazine
            Steer(0, 0, 0);
            if (t == 0) { P.magBefore = m; BulletsBorn(); }
            const int born = BulletsBorn();
            if (born) P.lastMag += born;
            if (t == 0) P.lastMag = 0;
            if (t < 20) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; return; }
            if (t < 50) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; if (t == 35) Log("probe31: box on, Square full: state %x %x", gMe.bits[0], gMe.bits[1]); return; }
            if (t < 80) return;
            Log("probe31: RESULT box on, Square: %d bullets, magazine %d -> %d, the box %s", P.lastMag, P.magBefore, m, v.item == 0xE ? "still on" : "OFF");
            P.sub = 4; P.at = gFrame; P.magBefore = m; P.lastWeapon = v.weapon;
            return;
        }
        case 4: {                                        // R2 twice (a reload?)
            Steer(0, 0, 0);
            if ((t >= 30 && t < 34) || (t >= 44 && t < 48)) gPad.buttons |= 0x0200;
            if (v.weapon != P.lastWeapon) { Log("probe31: box on, R2 twice +%lld: weapon %x", t, v.weapon); P.lastWeapon = v.weapon; }
            if (t < 200) return;
            Log("probe31: RESULT box on, R2 twice: magazine %d -> %d, weapon %x, the box %s", P.magBefore, m, v.weapon, v.item == 0xE ? "still on" : "OFF");
            P.sub = 5; P.at = gFrame;
            return;
        }
        default:                                         // 5: off
            Steer(0, 0, 0);
            if (v.item != 0xE && t > 10) { Log("probe31: box: off in %lld frames (state %x %x, weapon %x)", t, gMe.bits[0], gMe.bits[1], v.weapon); P.sub = 6; P.at = gFrame; return; }
            if (!ProbeWindowTo(1, 0) && t > 400) { Log("probe31: box: NOT taken off"); Probe31Next(); }
            return;
        }
    }
    Probe31Next();
}

// probe 40 (v141-host): the blocks of the stairs (AREA_STEPS, doll_nav.cpp's pass 4), how one is climbed. Each block of
// the map's stairs, from each side whose foot the doll reaches: to 1.2 m from its middle on that side, then three ways,
// each until the hips rise 0.7 m (MADE IT) or 150 frames (FAILED): A the stick into it at a walk, Triangle 3 frames in 8
// within 1.5 m facing it (the stairs' own way); B to 0.85 m from its middle, still, turned to it, Triangle 6 frames in 30;
// C the stick into it softly (0.4), Triangle held within 1.5 m. The state words every 10 frames; after a climb, off it
struct Probe40 {
    int phase = 0, blk = 0, side = 0, way = 0; long long at = 0; float y0 = 0, best = 1e30f;
    float c[3] = {0, 0, 0}, foot[3] = {0, 0, 0}, dir[2] = {0, 0}; bool init = false;
    std::vector<std::array<float, 3>> blocks; int made[3] = {0, 0, 0}, tries[3] = {0, 0, 0};
};
Probe40 gP40;
void Probe40Next() {                                     // the next way, side, block
    if (++gP40.way < 3) { gP40.phase = 1; gP40.at = gFrame; PlanTo(gP40.foot); return; }
    gP40.way = 0;
    if (++gP40.side >= 4) { gP40.side = 0; ++gP40.blk; }
    gP40.phase = 0;
}
// ---- (v150-host beta 21) probe 32: a book (the support row's 6, drawn as a magazine) and a real player.
// MGO1_DOLL_PROBE_ROLE layer: probe 18 (its support item laid where it stands: the loadout's ,s6 a book); walker: to the
// nearest book lying (its own game's copy), 5 s at it still, then the stick full away from it 40 frames: its state and
// whether it moved, logged; three times ----
struct Probe32 { int role = 0, phase = 0, tries = 0; long long at = 0; float book[3] = {0, 0, 0}, from[3] = {0, 0, 0}; };
Probe32 gP32;
void P32Book(uint32_t w, const std::string& name, void* ctx) {
    if (name != "NewYMBookOnline" && name != "NewYMBookFromGhost" && name != "NewYMBook") return;
    const uint32_t m = R32(w + 0x188);                   // (its model; the place at +0x40, as ps2world.cpp VdollTraps)
    if (m < 0x100000 || m + 0x4C > 0x02000000) return;
    const float p[3] = {RF(m + 0x40), RF(m + 0x44), RF(m + 0x48)};
    if (!(std::fabs(p[0]) < 1e6f && std::fabs(p[2]) < 1e6f) || (p[0] == 0.f && p[2] == 0.f)) return;
    ((std::vector<std::array<float, 3>>*)ctx)->push_back({p[0], p[1], p[2]});
}
// ---- (v150-host beta 22, book) probe 32's READER (MGO1_DOLL_PROBE_ROLE reader): the game's own book action of the local
// player (US n003a: its check 0x443C18, act 0x443718, end 0x443A08; docs/VIRTUAL_DOLL.md 30) watched near another
// player's book. Near the other player until a book of his lies (the game's own list: NewOnlineBookMan's entries), then
// still at 6 m with its back to the book, at 6 m facing it, then at 3, 2, 1.6, 1.2, 0.8 and 0.4 m facing it; what the
// check reads (its cooldown, the state bits it refuses, the books, the 3D distance it measures) logged each half second
// and at each change; the reading (+0xDE9), and while it lasts the stick pushed away from the book 20 frames a second;
// three readings ----
struct P32Entry { int slot = -1, timer = 0; uint32_t id = 0; float p[3] = {0, 0, 0}; };
void P32Man(uint32_t w, const std::string& name, void* ctx) { if (name == "NewOnlineBookMan") *(uint32_t*)ctx = w; }
std::vector<P32Entry> P32Entries() {                       // the books lying, as the game's manager lists them (state 1)
    uint32_t man = 0;
    ps2world::ForEachActor(&P32Man, &man);
    std::vector<P32Entry> v;
    if (man < 0x100000 || man > 0x01F00000) return v;
    for (int i = 0; i < 16; ++i) {
        const uint32_t e = man + 0xC0 + 0x40 * i;          // (+0x10 the place, +0x2C the id: the layer's slot << 10, +0x30 the state)
        if (R32(e + 0x30) != 1) continue;
        P32Entry b; b.id = R32(e + 0x2C); b.slot = (int)(b.id >> 10); b.timer = (int)R32(e + 0x34);
        for (int k = 0; k < 3; ++k) b.p[k] = RF(e + 0x10 + 4 * k);
        v.push_back(b);
    }
    return v;
}
uint32_t P32Action(uint32_t n, int depth) {                // the player's action of id 0x2EADB2E (the book's), as 0x30EE68 finds it ((v150-host beta 22, bookplay2) its low 24 bits 0xEADB2E: 0xADB2E found none)
    for (int guard = 0; n >= 0x100000 && n < 0x02000000 && guard < 200; ++guard, n = R32(n)) {
        if ((R32(n + 0x14) & 0xFFFFFF) == 0xEADB2E) return n;
        if (depth < 4) if (const uint32_t c = P32Action(R32(n + 8), depth + 1)) return c;
    }
    return 0;
}
struct P32Reader { int phase = 0, stage = 0, reads = 0, pushes = 0; long long at = 0, readAt = -1, pushAt = -1, afterAt = -1; uint32_t id = 0;
                   float book[3] = {0, 0, 0}, pushFrom[3] = {0, 0, 0}, moved = 0, readDist = 0; std::string last; };
P32Reader gP32R;
// (MGO1_DOLL_PROBE_PNG=1, with this doll's picture drawn: HEADLESS_<i>=0) its screen written, book_<frame>_<what>.png
void P32Png(const char* what) {
    static const bool on = getenv("MGO1_DOLL_PROBE_PNG") != nullptr;
    if (!on) return;
    char n[96]; snprintf(n, sizeof n, "book_%05lld_%s.png", gFrame, what);
    Log("probe32: picture %s (%s)", n, ps2::GsWritePng(n) ? "written" : "not written");
}
void Probe32Reader() {
    P32Reader& R = gP32R;
    static const float kStage[] = {6000, 6000, 3000, 2000, 1600, 1200, 800, 400};
    static const int kHold[] = {60, 120, 60, 60, 60, 90, 90, 90};
    const int nStage = (int)(sizeof kStage / sizeof kStage[0]);
    if (gRoom.phase != 2 || gFrame < 4800 || !gMe.alive || !gMe.obj) { R.at = gFrame; return; }
    const uint32_t o = gMe.obj;
    const int mySlot = R8(o + 0xB60);
    const std::vector<P32Entry> books = P32Entries();
    const P32Entry* cur = nullptr;
    for (const P32Entry& b : books) if (b.id == R.id) cur = &b;
    // what the game's check reads (0x443C18), and the action's own fields
    static const int kRefused[] = {9, 0x5C, 0x52, 0x99, 0xF0, 0x19, 0x1A, 0x1B, 0x1C};
    std::string refused;
    for (int b : kRefused) if (gMe.bits[b >> 5] & (1u << (b & 31))) { char t[8]; snprintf(t, sizeof t, " %x", b); refused += t; }
    const uint32_t k614 = R32(o + 0x614), act = P32Action(R32(o + 0x44), 0);
    static uint32_t dumped = 0;
    if (!act && dumped != o) {                               // (the action list as it is, once a player object: why none was found)
        dumped = o;
        std::string l;
        uint32_t n = R32(o + 0x44);
        for (int i = 0; i < 24 && n >= 0x100000 && n < 0x02000000; ++i, n = R32(n)) {
            char x[80]; snprintf(x, sizeof x, " [%08x %08x child %08x]", n, R32(n + 0x14), R32(n + 8)); l += x;
            if (R32(n + 8) >= 0x100000 && R32(n + 8) < 0x02000000) { const uint32_t c = R32(n + 8); snprintf(x, sizeof x, " {%08x %08x}", c, R32(c + 0x14)); l += x; }
        }
        Log("probe32: reader: no book action found from %08x:%s", R32(o + 0x44), l.c_str());
    }
    const int de9 = R8(o + 0xDE9), cool = (int)R32(o + 0xF54);
    float d3 = 1e9f;
    if (cur) { const float dx = RF(o + 0x110) - cur->p[0], dy = RF(o + 0x114) - cur->p[1], dz = RF(o + 0x118) - cur->p[2]; d3 = std::sqrt(dx * dx + dy * dy + dz * dz); }
    const float d2 = cur ? Dist2D(cur->p, gMe.pos) : 1e9f, err = cur ? Wrap(AngleTo(gMe.pos, cur->p) - Facing()) * 57.3f : 0.f;
    char sum[400];
    snprintf(sum, sizeof sum, "state %x %x %x | 4 %x 7 %x, refused [%s ], b62 %d, 614 %s, cooldown %d, de9 %d, fb4 %d, action %x flags %x sub %x motion %x book %x, books %zu",
             gMe.bits[0], gMe.bits[1], gMe.bits[2], gMe.bits[4], gMe.bits[7], refused.c_str(), R8(o + 0xB62),
             k614 >= 0x100000 && k614 < 0x02000000 ? (R8(k614 + 0x14) == 7 ? "kind 7" : "other") : "none", cool > 0 ? cool : 0, de9, R8(o + 0xFB4), act,
             act ? R32(act + 0x10) : 0, act ? R32(act + 0x34) : 0, act ? R32(act + 0x30) : 0, act ? R32(act + 0x50) : 0, books.size());
    const long long t = gFrame - R.at;
    if (sum != R.last || gFrame % 30 == 0) {
        std::string bl;
        for (const P32Entry& b : books) { char x[96]; snprintf(x, sizeof x, " [slot %d id %x read %d at %.0f %.0f %.0f]", b.slot, b.id, b.timer, b.p[0], b.p[1], b.p[2]); bl += x; }
        Log("probe32: reader (phase %d stage %d +%lld): %.2f m from the book (3D %.2f), facing it %+.0f, cam %.0f, me %08x (+0x44 %08x) slot %d at %.0f %.0f %.0f; %s%s", R.phase,
            R.stage, t, d2 / 1000.f, d3 / 1000.f, err, std::atan2(gCam.fwd[0], gCam.fwd[1]) * 57.3f, o, R32(o + 0x44), mySlot, gMe.pos[0], gMe.pos[1], gMe.pos[2], sum, bl.c_str());
        R.last = sum;
    }
    // the reading: its start, the stick away from the book 20 frames of each 60 while it lasts, its end
    if (de9 && R.readAt < 0) {
        R.readAt = gFrame; R.readDist = d3; R.pushes = 0; R.moved = 0; R.pushAt = gFrame + 30; StopPath(); Steer(0, 0, 0);
        uint32_t man = 0;
        ps2world::ForEachActor(&P32Man, &man);
        Log("probe32: READING %d starts at %.2f m from the book (3D %.2f, the stage %d at %.1f m), facing it %+.0f, state %x %x %x; the manager's read time %d", R.reads + 1,
            d2 / 1000.f, d3 / 1000.f, R.stage, kStage[std::min(R.stage, nStage - 1)] / 1000.f, err, gMe.bits[0], gMe.bits[1], gMe.bits[2], man ? (int)R32(man + 0x4D4) : -1);
    }
    if (R.readAt >= 0) {
        if (!de9) {
            Log("probe32: RESULT reading %d: %lld frames (%.1f s), begun at 3D %.2f m; the stick pushed away %d times moved it %.2f m in all; cooldown now %d",
                R.reads + 1, gFrame - R.readAt, (gFrame - R.readAt) / 60.f, R.readDist / 1000.f, R.pushes, R.moved / 1000.f, cool);
            R.readAt = -1; ++R.reads; R.phase = R.reads < 3 ? 0 : 9; R.stage = 2; R.at = gFrame; R.id = 0; Steer(0, 0, 0); R.afterAt = gFrame + 60;
            return;
        }
        const long long k = gFrame - R.pushAt;
        // ((v150-host beta 22, bookplay) MGO1_DOLL_PROBE_STILL=1: no push while it reads, its stick at rest: a reader who stands
        // still, as a virtual doll does, its copy on the other screens compared)
        static const bool still = getenv("MGO1_DOLL_PROBE_STILL") && *getenv("MGO1_DOLL_PROBE_STILL") == '1';
        if (k == 0) memcpy(R.pushFrom, gMe.pos, sizeof R.pushFrom);
        if (k >= 0 && k < 20 && cur && !still) { const float away = AngleTo(cur->p, gMe.pos); Steer(std::sin(away), std::cos(away), 1.f); }
        else Steer(0, 0, 0);
        if (gFrame - R.readAt == 45) P32Png("reading");
        if (gFrame - R.readAt == 300) P32Png("reading_5s");
        if (k == 20) {
            const float m = Dist2D(R.pushFrom, gMe.pos);
            ++R.pushes; R.moved += m; R.pushAt = gFrame + 40;
            Log("probe32: reading %d: the stick away 20 frames moved it %.2f m (state %x %x %x)", R.reads + 1, m / 1000.f, gMe.bits[0], gMe.bits[1], gMe.bits[2]);
        }
        return;
    }
    if (R.afterAt >= 0 && gFrame == R.afterAt) P32Png("after");
    switch (R.phase) {
    case 0: {                                            // near the other player until a book of another player lies (the newest: its whole life)
        const P32Entry* best = nullptr;
        for (const P32Entry& b : books) if (b.slot != mySlot && (!best || b.id > best->id)) best = &b;
        if (best && cool <= 0) {
            R.id = best->id; memcpy(R.book, best->p, sizeof R.book); R.phase = 1; R.at = gFrame; StopPath();
            Log("probe32: reader: the book %x of slot %d at %.0f %.0f %.0f, %.1f m away: to %.1f m (stage %d)", best->id, best->slot, best->p[0], best->p[1], best->p[2],
                Dist2D(best->p, gMe.pos) / 1000.f, kStage[R.stage] / 1000.f, R.stage);
            return;
        }
        const Player* l = Other31();
        if (l && Dist2D(l->pos, gMe.pos) > 9000.f) { if (!Following() || gFrame % 120 == 0) PlanTo(l->pos); FollowPath(); }
        else { StopPath(); Steer(0, 0, 0); }
        return;
    }
    case 1: {                                            // to the stage's distance
        if (!cur) { R.phase = 0; Log("probe32: reader: the book %x is gone", R.id); return; }
        if (d2 <= kStage[R.stage] + 150.f || t > 900) {
            StopPath(); Steer(0, 0, 0); R.phase = 2; R.at = gFrame;
            Log("probe32: reader: stage %d: %.2f m from the book (3D %.2f), %s", R.stage, d2 / 1000.f, d3 / 1000.f, R.stage == 0 ? "its back to it" : "facing it");
            return;
        }
        if (!Following() || t % 120 == 0) PlanTo(cur->p);
        FollowPath();
        return;
    }
    case 2: {                                            // still at that distance, turned (short pushes: no walk)
        if (!cur) { R.phase = 0; Log("probe32: reader: the book %x is gone", R.id); return; }
        const float want = AngleTo(gMe.pos, cur->p) + (R.stage == 0 ? 3.14159265f : 0.f), e = Wrap(want - Facing());
        if (std::fabs(e) > 0.7f && gFrame % 12 < 2) Steer(std::sin(want), std::cos(want), 0.6f);
        else Steer(0, 0, 0);
        if (t == kHold[R.stage] / 2 && R.stage == 1) P32Png("at6m_facing");
        if (t >= kHold[R.stage]) {
            Log("probe32: reader: stage %d done: %.2f m (3D %.2f), facing it %+.0f, no reading", R.stage, d2 / 1000.f, d3 / 1000.f, err);
            if (++R.stage >= nStage) { R.phase = 0; R.stage = 2; R.id = 0; }
            else R.phase = 1;
            R.at = gFrame;
        }
        return;
    }
    default:
        Steer(0, 0, 0);
        return;
    }
}
void Probe32Frame() {
    Probe32& P = gP32;
    if (!P.role) {
        const char* r = getenv("MGO1_DOLL_PROBE_ROLE");
        P.role = r && !strcmp(r, "walker") ? 2 : r && !strcmp(r, "reader") ? 3 : 1;
        Log("probe32: the %s", P.role == 1 ? "layer (probe 18)" : P.role == 2 ? "walker" : "reader");
    }
    // ((v150-host beta 22, bookplay) the team ordered (red, blue: the bench's DTEAMS): TEAM CHANGE first, as probe 31's; the
    // layer and the reader on one team read a teammate's book)
    if (gRoom.phase == 2 && gFrame >= 4800 && (gSet.team == "red" || gSet.team == "blue") && (gMyTeam == 0 || gMyTeam == 1) &&
        gMyTeam != (gSet.team == "red" ? 0 : 1)) {
        if (ProbeTeamChangeFrame()) return;
    }
    if (P.role == 1) {
        // ((v150-host beta 22, book) MGO1_DOLL_PROBE_NEAR=<mm>: its tries begin once another player is that near: a reader there)
        static const float nearMm = getenv("MGO1_DOLL_PROBE_NEAR") ? (float)atof(getenv("MGO1_DOLL_PROBE_NEAR")) : 0.f;
        if (nearMm > 0.f && gP18.phase == 0) {
            const Player* o = Other31();
            if (!o || Dist2D(o->pos, gMe.pos) > nearMm) { Steer(0, 0, 0); if (gFrame % 600 == 0) Log("probe32: the layer waits for a player within %.0f m", nearMm / 1000.f); return; }
        }
        Probe18Frame();
        return;
    }
    if (P.role == 3) { Probe32Reader(); return; }
    if (gRoom.phase != 2 || gFrame < 4800 || !gMe.alive) { P.at = gFrame; return; }
    const long long t = gFrame - P.at;
    switch (P.phase) {
    case 0: {                                            // the nearest book lying
        std::vector<std::array<float, 3>> books;
        ps2world::ForEachActor(&P32Book, &books);
        Steer(0, 0, 0);
        if (books.empty()) { if (t % 600 == 0) Log("probe32: no book lying"); return; }
        float bd = 1e30f;
        for (const auto& b : books) { const float d = Dist2D(b.data(), gMe.pos); if (d < bd) { bd = d; memcpy(P.book, b.data(), sizeof P.book); } }
        Log("probe32: a book at %.0f %.0f %.0f, %.1f m away (%zu lying): to it", P.book[0], P.book[1], P.book[2], bd / 1000.f, books.size());
        P.phase = 1; P.at = gFrame;
        return;
    }
    case 1: {                                            // to it
        const float d = Dist2D(P.book, gMe.pos);
        if (t % 30 == 0) Log("probe32: %.2f m from the book, state %x %x %x, life %d", d / 1000.f, gMe.bits[0], gMe.bits[1], gMe.bits[2], gMe.life);
        if (d < 600.f || t > 3600) {
            Log("probe32: at the book (%.2f m) after %lld frames, state %x %x %x", d / 1000.f, t, gMe.bits[0], gMe.bits[1], gMe.bits[2]);
            StopPath(); Steer(0, 0, 0); P.phase = 2; P.at = gFrame;
            return;
        }
        if (!Following() || t % 120 == 0) PlanTo(P.book);
        FollowPath();
        return;
    }
    case 2: {                                            // 5 s still at it, then the stick away 40 frames
        const float d = Dist2D(P.book, gMe.pos);
        Steer(0, 0, 0);
        if (t == 300) memcpy(P.from, gMe.pos, sizeof P.from);
        if (t >= 300 && t < 340) { const float away = AngleTo(P.book, gMe.pos); Steer(std::sin(away), std::cos(away), 1.f); }
        if (t % 30 == 0 && t <= 300) Log("probe32: at the book +%lld: %.2f m, facing %.0f, state %x %x %x | 3 %x 4 %x 6 %x 7 %x, life %d", t, d / 1000.f, Facing() * 57.3f,
                                         gMe.bits[0], gMe.bits[1], gMe.bits[2], gMe.bits[3], gMe.bits[4], gMe.bits[6], gMe.bits[7], gMe.life);
        if (t == 400) {
            Log("probe32: RESULT at the book %d: the stick away 40 frames moved it %.2f m (now %.2f m from it), state %x %x %x", P.tries + 1, Dist2D(P.from, gMe.pos) / 1000.f,
                d / 1000.f, gMe.bits[0], gMe.bits[1], gMe.bits[2]);
            P.phase = 3; P.at = gFrame;
        }
        return;
    }
    default:                                             // 10 s, then to the nearest book again (three times)
        Steer(0, 0, 0);
        if (t > 600 && ++P.tries < 3) { P.phase = 0; P.at = gFrame; }
        else if (t > 600) P.at = gFrame;
        return;
    }
}
void Probe40Frame() {
    if (!gP40.init) {
        gP40.init = true;
        for (const auto& st : gMap->nav->AllSteps())
            for (const auto& b : st.stones) {
                bool dup = false;
                for (const auto& o : gP40.blocks) dup |= Dist2D(o.data(), b.data()) < 500.f && std::fabs(o[1] - b[1]) < 300.f;
                if (!dup) gP40.blocks.push_back(b);
            }
        Log("probe40: %zu blocks in %zu stairs", gP40.blocks.size(), gMap->nav->AllSteps().size());
        for (size_t i = 0; i < gP40.blocks.size(); ++i) Log("probe40: block %zu at %.0f %.0f %.0f", i, gP40.blocks[i][0], gP40.blocks[i][1], gP40.blocks[i][2]);
    }
    const long long t = gFrame - gP40.at;
    switch (gP40.phase) {
    case 0: {                                            // a block's side: its foot
        if (gP40.blk >= (int)gP40.blocks.size()) {
            if (gFrame % 600 == 0) Log("probe40: done: A %d of %d, B %d of %d, C %d of %d", gP40.made[0], gP40.tries[0], gP40.made[1], gP40.tries[1], gP40.made[2], gP40.tries[2]);
            Steer(0, 0, 0);
            return;
        }
        memcpy(gP40.c, gP40.blocks[(size_t)gP40.blk].data(), sizeof gP40.c);
        static const float kDir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        gP40.dir[0] = kDir[gP40.side][0]; gP40.dir[1] = kDir[gP40.side][1];
        gP40.foot[0] = gP40.c[0] + 1200.f * gP40.dir[0]; gP40.foot[2] = gP40.c[2] + 1200.f * gP40.dir[1]; gP40.foot[1] = gP40.c[1] - 1000.f;
        if (!PlanTo(gP40.foot)) { Log("probe40: block %d side %d: no way to its foot", gP40.blk, gP40.side); gP40.way = 2; Probe40Next(); return; }
        gP40.phase = 1; gP40.at = gFrame;
        return;
    }
    case 1:                                              // to the foot
        if (Dist2D(gMe.pos, gP40.foot) < 300.f && std::fabs(gMe.pos[1] - 800.f - (gP40.c[1] - 1000.f)) < 600.f) {
            StopPath(); Steer(0, 0, 0);
            if (t < 20) return;                          // (still a moment)
            gP40.phase = 2; gP40.at = gFrame; gP40.y0 = gMe.pos[1]; gP40.best = 1e30f;
            Log("probe40: block %d side %d way %c: from %.0f %.0f %.0f, the block's top %.0f above the floor", gP40.blk, gP40.side, 'A' + gP40.way,
                gMe.pos[0], gMe.pos[1], gMe.pos[2], gP40.c[1] - (gMe.pos[1] - 800.f));
            return;
        }
        if (!Following() || t > 1500) { Log("probe40: block %d side %d: its foot not reached (%.1f m)", gP40.blk, gP40.side, Dist2D(gMe.pos, gP40.foot) / 1000.f); gP40.way = 2; Probe40Next(); return; }
        FollowPath();
        return;
    case 2: {                                            // a try
        const float dx = gP40.c[0] - gMe.pos[0], dz = gP40.c[2] - gMe.pos[2], d = Dist2D(gMe.pos, gP40.c);
        const bool facing = std::fabs(Wrap(AngleTo(gMe.pos, gP40.c) - Facing())) < 40.f * 3.14159265f / 180.f;
        gP40.best = std::min(gP40.best, d);
        if (gP40.way == 0) {
            Steer(dx, dz, 0.7f);                    // (the stairs' walk push)
            if (d < 1500.f && facing && gFrame % 8 < 3) gPad.buttons |= 0x1000;
        } else if (gP40.way == 1) {
            if (d > 850.f && t < 90) Steer(dx, dz, 0.45f);
            else {
                const bool square = std::fabs(Wrap(AngleTo(gMe.pos, gP40.c) - Facing())) < 12.f * 3.14159265f / 180.f;
                if (!square) Steer(dx, dz, 0.12f); else Steer(0, 0, 0);
                if (square && t % 30 < 6) gPad.buttons |= 0x1000;
            }
        } else {
            Steer(dx, dz, 0.4f);
            if (d < 1500.f) gPad.buttons |= 0x1000;
        }
        if (t % 10 == 0)
            Log("probe40: %c t %lld: pos %.0f %.0f %.0f (%+.0f), %.0f from its middle, facing %s, bits %x %x %x, pad %04x", 'A' + gP40.way, t, gMe.pos[0], gMe.pos[1], gMe.pos[2],
                gMe.pos[1] - gP40.y0, d, facing ? "yes" : "no", gMe.bits[0], gMe.bits[1], gMe.bits[2], gPad.buttons);
        const bool up = gMe.pos[1] - gP40.y0 > 700.f;
        if (up || t >= 150) {
            ++gP40.tries[gP40.way];
            if (up) ++gP40.made[gP40.way];
            Log("probe40: RESULT block %d side %d way %c: %s in %lld frames (hips %+.0f, %.0f from its middle at best, bits %x)", gP40.blk, gP40.side, 'A' + gP40.way,
                up ? "MADE IT" : "FAILED", t, gMe.pos[1] - gP40.y0, gP40.best, gMe.bits[0]);
            gP40.phase = up ? 3 : 4; gP40.at = gFrame;
        }
        return;
    }
    case 3:                                              // off it: the stick away from its middle
        Steer(gP40.dir[0], gP40.dir[1], 0.7f);
        if (t > 50 && gMe.pos[1] - gP40.y0 < 300.f) { Probe40Next(); return; }
        if (t > 240) { Log("probe40: still on the block after 4 s"); Probe40Next(); }
        return;
    default:                                             // 4: a step back, the next way
        Steer(gMe.pos[0] - gP40.c[0], gMe.pos[2] - gP40.c[2], 0.5f);
        if (t > 30) Probe40Next();
        return;
    }
}

// probe 41 (v141-host): the stairs of blocks taken again and again, as a path takes them (FollowPath, StepsSegment): each
// stairs of the map in turn, to its foot by any way, then up to its top; RESULT made it (the top within 0.6 m and 0.4 m
// of its height) or failed (the path over, or 30 s). With MGO1_STAIRS_TRACE the run's trace every 3rd frame
struct Probe41 { int phase = 0, k = 0, made = 0, tries = 0; long long at = 0; bool init = false; };
Probe41 gP41;
void Probe41Frame() {
    const auto& all = gMap->nav->AllSteps();
    if (!gP41.init) {
        gP41.init = true;
        Log("probe41: %zu stairs", all.size());
        for (size_t i = 0; i < all.size(); ++i)
            Log("probe41: stairs %zu from %.0f %.0f %.0f to %.0f %.0f %.0f by %zu blocks", i, all[i].a[0], all[i].a[1], all[i].a[2], all[i].b[0], all[i].b[1],
                all[i].b[2], all[i].stones.size());
    }
    if (all.empty()) { Steer(0, 0, 0); return; }
    const int k = gP41.k % (int)all.size();
    const NavMesh::Steps& st = all[(size_t)k];
    const float* lo = st.a[1] <= st.b[1] ? st.a : st.b;
    const float* hi = st.a[1] <= st.b[1] ? st.b : st.a;
    const long long t = gFrame - gP41.at;
    switch (gP41.phase) {
    case 0:                                              // to its foot (off the mesh, on a block: first off it, toward the foot)
        if (!PlanTo(lo)) {
            if (t < 120) { Steer(lo[0] - gMe.pos[0], lo[2] - gMe.pos[2], 0.7f); return; }
            Log("probe41: stairs %d: no way to its foot from %.0f %.0f %.0f", k, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
            ++gP41.k; gP41.at = gFrame;
            return;
        }
        gP41.phase = 1; gP41.at = gFrame;
        return;
    case 1:
        // (the foot off the mesh: the path ends 0.4 to 0.6 m short of it)
        if ((Dist2D(gMe.pos, lo) < 400.f || (!Following() && Dist2D(gMe.pos, lo) < 1200.f)) && std::fabs(gMe.pos[1] - 800.f - lo[1]) < 400.f) {
            StopPath(); Steer(0, 0, 0);
            gP41.phase = 2; gP41.at = gFrame;
            return;
        }
        if (!Following() || t > 3600) {
            Log("probe41: stairs %d: its foot not reached (%.1f m off, %.0f above it)", k, Dist2D(gMe.pos, lo) / 1000.f, gMe.pos[1] - 800.f - lo[1]);
            gP41.phase = 0; ++gP41.k; gP41.at = gFrame;
            return;
        }
        FollowPath();
        return;
    case 2:                                              // up
        Steer(0, 0, 0);
        if (t < 20) return;                              // (still a moment)
        if (!PlanTo(hi)) { Log("probe41: stairs %d: no way up", k); gP41.phase = 0; ++gP41.k; gP41.at = gFrame; return; }
        Log("probe41: stairs %d try %d: up from %.0f %.0f %.0f", k, gP41.tries + 1, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
        gP41.phase = 3; gP41.at = gFrame;
        return;
    default:
        if (Dist2D(gMe.pos, hi) < 600.f && std::fabs(gMe.pos[1] - 800.f - hi[1]) < 400.f) {
            ++gP41.made; ++gP41.tries;
            Log("probe41: RESULT stairs %d MADE IT in %.1f s (%d of %d)", k, t / 60.0, gP41.made, gP41.tries);
            StopPath(); gP41.phase = 0; ++gP41.k; gP41.at = gFrame;
            return;
        }
        if (!Following() || t > 1800) {
            ++gP41.tries;
            Log("probe41: RESULT stairs %d FAILED after %.1f s at %.0f %.0f %.0f (%d of %d)", k, t / 60.0, gMe.pos[0], gMe.pos[1], gMe.pos[2], gP41.made, gP41.tries);
            StopPath(); gP41.phase = 0; ++gP41.k; gP41.at = gFrame;
            return;
        }
        FollowPath();
        return;
    }
}

// probe 43 (v141 beta 3): every link of the map that takes a player up or across (the climbs, the ladders both ways, the
// stairs of blocks, the vaults), each taken as a path takes it: to its start by any way, then to its end (FollowPath).
// RESULT made it (the end within 0.7 m and 0.5 m of its height) or failed (the path over elsewhere, or 30 s); a map's
// summary when its links are done (24 at most, the kinds in turn). On a map change it starts again on the new map
struct P43Task { int kind = 0; float a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; };
struct Probe43 {
    const MapState* map = nullptr; std::string key; std::vector<P43Task> tasks; size_t k = 0; int phase = 0; long long at = 0;
    int made[4] = {0, 0, 0, 0}, tries[4] = {0, 0, 0, 0}, notWays[4] = {0, 0, 0, 0}; bool summary = false;
};
Probe43 gP43;
static const char* const kP43Kind[4] = {"climb", "ladder", "stairs", "vault"};
void Probe43Frame() {
    if (gP43.map != gMap.get() || gP43.key != gMap->key) {
        // ((v141 beta 6) a map left before its tasks were done: its summary all the same, cut short)
        if (gP43.map && !gP43.summary && !gP43.tasks.empty())
            Log("probe43: SUMMARY %s: climbs %d of %d, ladders %d of %d, stairs %d of %d, vaults %d of %d (not ways of the mesh: %d %d %d %d) (cut short at task %zu of %zu)",
                gP43.key.c_str(), gP43.made[0], gP43.tries[0], gP43.made[1], gP43.tries[1], gP43.made[2], gP43.tries[2], gP43.made[3], gP43.tries[3], gP43.notWays[0],
                gP43.notWays[1], gP43.notWays[2], gP43.notWays[3], gP43.k, gP43.tasks.size());
        UnstickFrame(false);
        gP43 = Probe43(); gP43.map = gMap.get(); gP43.key = gMap->key;
        std::vector<P43Task> byKind[4];
        for (int i = 0; i < gMap->nav->LinkCount(); ++i) {
            P43Task t; uint8_t area = 0;
            if (!gMap->nav->LinkAt(i, t.a, t.b, &area)) continue;
            if (area == AREA_CLIMB) { t.kind = 0; if (t.a[1] > t.b[1]) std::swap(t.a, t.b); }
            else if (area == AREA_STEPS) { t.kind = 2; if (t.a[1] > t.b[1]) std::swap(t.a, t.b); }
            else if (area == AREA_VAULT) t.kind = 3;
            else continue;
            bool dup = false;
            for (const auto& o : byKind[t.kind]) dup |= Dist2D(o.a, t.a) < 800.f && Dist2D(o.b, t.b) < 800.f;
            if (!dup) byKind[t.kind].push_back(t);
        }
        for (const Ladder& l : gMap->nav->Ladders()) {      // (both ways: up from its foot, down from its top)
            P43Task up; up.kind = 1; memcpy(up.a, l.foot, sizeof up.a); memcpy(up.b, l.top, sizeof up.b);
            P43Task down = up; std::swap(down.a, down.b);
            byKind[1].push_back(up); byKind[1].push_back(down);
        }
        // (the ends on their floors: a ladder's volumes are above them)
        auto floorOf = [](float* q) { const float up[3] = {q[0], q[1] + 300.f, q[2]}; const float g = gMap->nav->GroundBelow(up, 3000.f); if (g > -1e29f) q[1] = g; };
        for (auto& v : byKind) for (auto& t : v) { floorOf(t.a); floorOf(t.b); }
        for (size_t n = 0; gP43.tasks.size() < 24; ++n) {   // (the kinds in turn)
            bool any = false;
            for (int kd = 0; kd < 4 && gP43.tasks.size() < 24; ++kd) if (n < byKind[kd].size()) { gP43.tasks.push_back(byKind[kd][n]); any = true; }
            if (!any) break;
        }
        Log("probe43: %s: %zu climbs, %zu ladder ways, %zu stairs, %zu vaults; %zu tasks", gP43.key.c_str(), byKind[0].size(), byKind[1].size(), byKind[2].size(),
            byKind[3].size(), gP43.tasks.size());
    }
    // (two probes in one game: MGO1_P43_PART 0 or 1 takes every other task)
    static const int part = getenv("MGO1_P43_PART") ? atoi(getenv("MGO1_P43_PART")) : -1;
    if (part >= 0 && gP43.k < gP43.tasks.size() && (int)(gP43.k % 2) != part) { ++gP43.k; return; }
    if (gP43.k >= gP43.tasks.size()) {
        if (!gP43.summary) {
            gP43.summary = true; gP43.at = gFrame;
            Log("probe43: SUMMARY %s: climbs %d of %d, ladders %d of %d, stairs %d of %d, vaults %d of %d (not ways of the mesh: %d %d %d %d)", gP43.key.c_str(),
                gP43.made[0], gP43.tries[0], gP43.made[1], gP43.tries[1], gP43.made[2], gP43.tries[2], gP43.made[3], gP43.tries[3], gP43.notWays[0], gP43.notWays[1],
                gP43.notWays[2], gP43.notWays[3]);
            // ((v141 beta 6) out of the other probe's way: beta 3's done probe stood in a ladder's foot, the other one's task up
            // that ladder waited 10 s for it and failed, City Under Siege)
            float p[3];
            for (int i = 0; i < 8; ++i)
                if (RandomReachable(30000.f, p) && Dist2D(p, gMe.pos) > 8000.f && PlanTo(p)) { Log("probe43: done, off to %.0f %.0f %.0f", p[0], p[1], p[2]); break; }
        }
        if (Following() && gFrame - gP43.at < 1800) { FollowPath(); return; }
        StopPath(); Steer(0, 0, 0);
        return;
    }
    const P43Task& t = gP43.tasks[gP43.k];
    const long long dt = gFrame - gP43.at;
    auto next = [&]() { StopPath(); UnstickFrame(false); gP43.phase = 0; ++gP43.k; gP43.at = gFrame; };
    switch (gP43.phase) {
    case 0:
        if (!PlanTo(t.a)) {
            if (dt < 120) { Steer(t.a[0] - gMe.pos[0], t.a[2] - gMe.pos[2], 0.7f); return; }   // (off the mesh: toward it first)
            Log("probe43: %s %zu: no way to its start %.0f %.0f %.0f", kP43Kind[t.kind], gP43.k, t.a[0], t.a[1], t.a[2]);
            next(); return;
        }
        gP43.phase = 1; gP43.at = gFrame; return;
    case 1:
        // ((v141 beta 6) off a ladder: beta 3 took a ladder's foot for reached on the ladder coming down, and its way up then
        // began on the ladder going down: n022a's ladder 003 "failed" in 0.8 s)
        if ((Dist2D(gMe.pos, t.a) < 500.f || (!Following() && Dist2D(gMe.pos, t.a) < 1200.f)) && std::fabs(gMe.pos[1] - 800.f - t.a[1]) < 500.f && !OnLadder()) {
            StopPath(); UnstickFrame(false); Steer(0, 0, 0); gP43.phase = 2; gP43.at = gFrame; return;
        }
        if (!Following() || dt > 3600) { Log("probe43: %s %zu: its start not reached (%.1f m off)", kP43Kind[t.kind], gP43.k, Dist2D(gMe.pos, t.a) / 1000.f); next(); return; }
        {   // ((v141 beta 6) on the way to the start, a still spell (under 0.3 m in 5 s, no link under way): the brain's ways out)
            static float sAt[3] = {0, 0, 0};
            static long long sSince = -1;
            const int u = UnstickFrame(true);
            if (u == 1) return;
            if (u == -1) { UnstickGaveUp(); Log("probe43: %s %zu: the ways out ran out on the way to its start", kP43Kind[t.kind], gP43.k); next(); return; }
            if (sSince < 0 || (u == 0 && (Dist2D(sAt, gMe.pos) > 300.f || OnLinkNow()))) { memcpy(sAt, gMe.pos, sizeof sAt); sSince = gFrame; }
            if (u == 0 && gFrame - sSince >= 300 && UnstickOn()) {
                Log("still: probe43 5 s at %.0f %.0f %.0f on the way to its start: %s", gMe.pos[0], gMe.pos[1], gMe.pos[2], PathState().c_str());
                UnstickStart(); sSince = gFrame;
                if (UnstickFrame(true) == 1) return;
            }
        }
        FollowPath(); return;
    case 2: {
        Steer(0, 0, 0);
        if (dt < 20) return;
        if (!PlanTo(t.b)) { Log("probe43: %s %zu: no way to its end", kP43Kind[t.kind], gP43.k); next(); return; }
        // ((v141 beta 3) a way that does not take the link (its top not on the walkable mesh, or a way round): no path of a
        // doll takes that link either, not counted as a try of it)
        static const uint8_t kArea[4] = {AREA_CLIMB, AREA_LADDER, AREA_STEPS, AREA_VAULT};
        if (!PathHasLink(kArea[t.kind])) {
            Log("probe43: %s %zu: the way to its end does not take it (%zu corners): not a way of the mesh", kP43Kind[t.kind], gP43.k, PathCorners());
            ++gP43.notWays[t.kind]; next(); return;
        }
        gP43.phase = 3; gP43.at = gFrame; return;
    }
    default: {
        // ((v141 beta 6) a vault made: past its obstacle too, nothing between the doll's knees and its end's: beta 3 counted a doll
        // within 0.7 m of a 0.9 m vault's end on the near side, Pillbox Purgatory's "made it" in 0.7 s)
        const float kn[3] = {gMe.pos[0], gMe.pos[1] - 450.f, gMe.pos[2]}, kb[3] = {t.b[0], t.b[1] + 350.f, t.b[2]};
        const bool past = t.kind != 3 || LineClear(kn, kb);
        if (Dist2D(gMe.pos, t.b) < 700.f && std::fabs(gMe.pos[1] - 800.f - t.b[1]) < 500.f && past && !OnLadder()) {   // (off the ladder: its exit over)
            ++gP43.made[t.kind]; ++gP43.tries[t.kind];
            Log("probe43: RESULT %s %s %zu MADE IT in %.1f s (%.0f %.0f %.0f to %.0f %.0f %.0f)", gP43.key.c_str(), kP43Kind[t.kind], gP43.k, dt / 60.0, t.a[0], t.a[1],
                t.a[2], t.b[0], t.b[1], t.b[2]);
            next(); return;
        }
        if (!Following() || dt > 1800) {
            ++gP43.tries[t.kind];
            Log("probe43: RESULT %s %s %zu FAILED after %.1f s at %.0f %.0f %.0f (%.0f %.0f %.0f to %.0f %.0f %.0f)", gP43.key.c_str(), kP43Kind[t.kind], gP43.k, dt / 60.0,
                gMe.pos[0], gMe.pos[1], gMe.pos[2], t.a[0], t.a[1], t.a[2], t.b[0], t.b[1], t.b[2]);
            next(); return;
        }
        FollowPath(); return;
    }
    }
}

// (v150-host beta 22, blast) MGO1_DOLL_PROBE_GETUP=<n>, the victim knocked down (its state word 0's 0x2000000 going down,
// 0x8000000 or 0x10000000 down, docs/PLAYABLE_DOLL.md): Cross pressed 2 frames every n frames from the first frame down, so that
// its own game stands it up at the soonest a player can; each spell logged: its start (the state, the life, the stamina, the
// reaction kind +0xDE3 and its fall's state +0xDE5 to +0xDE6), each change of the state word 0 and the fall's state, its end
int Bytes31(uint32_t off) { return gMe.obj ? (int)R8(gMe.obj + off) : -1; }
// (v150-host beta 22, mash) MGO1_DOLL_PROBE_MASH=<pattern>,<pattern>,...: the victim's presses while it is down, one pattern a
// spell down in turn (the list again after its end), from the spell's first frame: 0 nothing; c<r> Cross r times a second
// (each press 2 frames, regular); u<r> the pad's up; l<r> L1; j<r> Cross r a second, each gap 0.75 to 1.25 of 60/r frames (at
// least 4: a press, a release); s<q> the left stick at full turned q quarter turns a second; h Cross held all along. Logged:
// the spell's pattern, then every 30 frames and at each change of the knocked-out flag (0x99) or of the fall's motion flag
// (0x14F, the presses not counted meanwhile: US n003a 0x41465C) the player's own stun gauge (+0xB68 of +0xB6A), the count
// of the knocked-out recovery (+0xB77: 8 + the ticks with a press among the last 32), its rate (+0xB78) and the presses sent;
// at its end a RESULT line (how long knocked out, how long down, the presses sent in all and after the fall's motion)
struct Mash31 { std::vector<std::string> pats; size_t next = 0; std::string pat; long long since = -1, koAt = -1, koEnd = -1, fallEnd = -1;
                int sent = 0, sentAfterFall = 0, b77max = 0, gauge0 = -1; long long nextPress = 0, pressEnd = -1; unsigned rnd = 0x5EED1234u; bool held = false;
                bool ko = false, falling = false; long long b77sum = 0, b77n = 0; };
Mash31 gMash31;
bool Flag31(int f) { return (gMe.bits[f >> 5] >> (f & 31)) & 1; }
void MashFrame31(long long t) {
    Mash31& M = gMash31;
    const uint32_t o = gMe.obj;
    const int gauge = o ? (int16_t)R16(o + 0xB68) : -1, full = o ? (int16_t)R16(o + 0xB6A) : -1, b77 = o ? (int)R8(o + 0xB77) : -1, rate = o ? (int)R16(o + 0xB78) : -1;
    const bool ko = Flag31(0x99), falling = Flag31(0x14F);
    if (t == 0) {
        if (M.pats.empty()) {
            const std::string v = getenv("MGO1_DOLL_PROBE_MASH");
            for (size_t a = 0; a < v.size();) { size_t e = v.find(',', a); if (e == std::string::npos) e = v.size(); if (e > a) M.pats.push_back(v.substr(a, e - a)); a = e + 1; }
            if (M.pats.empty()) M.pats.push_back("0");
        }
        M.pat = M.pats[M.next++ % M.pats.size()];
        M.since = gFrame; M.koAt = ko ? gFrame : -1; M.koEnd = -1; M.fallEnd = falling ? -1 : gFrame; M.sent = M.sentAfterFall = 0; M.b77max = 0; M.gauge0 = gauge;
        M.nextPress = 0; M.pressEnd = -1; M.held = false; M.ko = ko; M.falling = falling; M.b77sum = M.b77n = 0;
        Log("probe31: mash: down, the pattern %s (state %x, knocked out %d, the fall's motion %d, the gauge %d of %d, its rate %d, the count %d)", M.pat.c_str(), gMe.bits[0],
            ko, falling, gauge, full, rate, b77);
    }
    if (ko && M.koAt < 0) M.koAt = gFrame;
    if (!ko && M.ko && M.koEnd < 0) M.koEnd = gFrame;
    if (!falling && M.falling && M.fallEnd < 0) M.fallEnd = gFrame;
    if (ko) { M.b77max = std::max(M.b77max, b77); M.b77sum += b77; ++M.b77n; }
    const bool change = ko != M.ko || falling != M.falling;
    M.ko = ko; M.falling = falling;
    if (change || t % 30 == 0)
        Log("probe31: mash +%lld: the gauge %d of %d, the count %d, the rate %d, knocked out %d, the fall's motion %d, state %x, presses sent %d", t, gauge, full, b77, rate, ko, falling,
            gMe.bits[0], M.sent);
    // the pattern's presses
    const char k = M.pat.empty() ? '0' : M.pat[0];
    const double r = M.pat.size() > 1 ? atof(M.pat.c_str() + 1) : 0.0;
    auto press = [&](uint32_t btn, long long gap) {
        if (t >= M.nextPress) { M.pressEnd = t + 2; M.nextPress = t + std::max(4LL, gap); ++M.sent; if (M.fallEnd >= 0) ++M.sentAfterFall; }
        if (t < M.pressEnd) gPad.buttons |= btn;
    };
    if ((k == 'c' || k == 'u' || k == 'l') && r > 0) {
        // (regular: the k-th press at floor(k x 60 / r) frames)
        const uint32_t btn = k == 'c' ? 0x4000 : k == 'u' ? 0x0010 : 0x0400;
        const long long nk = (long long)std::floor((double)M.sent * 60.0 / r);
        if (t >= nk) { M.pressEnd = t + 2; ++M.sent; if (M.fallEnd >= 0) ++M.sentAfterFall; }
        if (t < M.pressEnd) gPad.buttons |= btn;
    } else if (k == 'j' && r > 0) {
        M.rnd = M.rnd * 1103515245u + 12345u;
        const double u = 0.75 + 0.5 * (double)((M.rnd >> 8) & 0xFFFF) / 65535.0;
        press(0x4000, (long long)std::lround(60.0 / r * u));
    } else if (k == 's' && r > 0) {
        const double a = 1.5707963 * r * (double)t / 60.0;
        gPad.sticks[2] = (uint8_t)std::lround(128.0 + 127.0 * std::sin(a));
        gPad.sticks[3] = (uint8_t)std::lround(128.0 - 127.0 * std::cos(a));
        const long long q = (long long)std::floor(r * (double)t / 60.0);
        if (q + 1 > M.sent) { ++M.sent; if (M.fallEnd >= 0) ++M.sentAfterFall; }
    } else if (k == 'h') {
        if (!M.held) { M.held = true; ++M.sent; }
        gPad.buttons |= 0x4000;
    }
}
void MashEnd31(const char* how) {
    Mash31& M = gMash31;
    if (M.since < 0) return;
    const long long ko = M.koAt >= 0 ? (M.koEnd >= 0 ? M.koEnd : gFrame) - M.koAt : -1;
    Log("probe31: mash: RESULT %s: the pattern %s, knocked out %lld frames (%.2f s, from the gauge %d), down %lld frames, the fall's motion over at +%lld, presses sent %d "
        "(%d after the fall's motion), the count's mean %.2f and most %d while knocked out", how, M.pat.c_str(), ko, ko / 59.94, M.gauge0, gFrame - M.since,
        M.fallEnd >= 0 ? M.fallEnd - M.since : -1LL, M.sent, M.sentAfterFall, M.b77n ? (double)M.b77sum / (double)M.b77n : 0.0, M.b77max);
    M.since = -1;
}
bool GetUp31() {
    static const int n = getenv("MGO1_DOLL_PROBE_GETUP") ? std::max(1, atoi(getenv("MGO1_DOLL_PROBE_GETUP"))) : 0;
    static const bool mash = getenv("MGO1_DOLL_PROBE_MASH") != nullptr;   // (v150-host beta 22, mash)
    static long long since = -1; static uint32_t w0 = 0; static int fall = -1;
    const bool down = gMe.alive && (gMe.bits[0] & 0x1A000000) != 0;
    if (!down) {
        if (since >= 0) Log("probe31: getup: up after %lld frames (state %x, life %d, stamina %d, at %.0f %.0f %.0f)", gFrame - since, gMe.bits[0], gMe.life, gMe.stun, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
        if (since >= 0 && mash) MashEnd31(gMe.alive ? "up" : "dead");
        since = -1;
        return false;
    }
    if (since < 0) {
        since = gFrame; w0 = gMe.bits[0]; fall = Bytes31(0xDE5);
        Log("probe31: getup: down (state %x %x, life %d, stamina %d, the reaction %d, the fall %d %d, at %.0f %.0f %.0f)%s", gMe.bits[0], gMe.bits[1], gMe.life, gMe.stun,
            Bytes31(0xDE3), Bytes31(0xDE5), Bytes31(0xDE6), gMe.pos[0], gMe.pos[1], gMe.pos[2], n ? "" : " (no input: MGO1_DOLL_PROBE_GETUP unset)");
    } else if (gMe.bits[0] != w0 || Bytes31(0xDE5) != fall) {
        w0 = gMe.bits[0]; fall = Bytes31(0xDE5);
        Log("probe31: getup: +%lld state %x, the fall %d %d, at %.0f %.0f %.0f", gFrame - since, gMe.bits[0], Bytes31(0xDE5), Bytes31(0xDE6), gMe.pos[0], gMe.pos[1], gMe.pos[2]);
    }
    if (mash) MashFrame31(gFrame - since);
    else if (n && (gFrame - since) % n < 2) gPad.buttons |= 0x4000;
    return true;
}
// ((v141 beta 9) MGO1_DOLL_PROBE_VICTIM_BRAIN=1, the choke step's victim: knocked out, its own brain 45 s from then (its getting up,
// its moves after it), then the probe's still victim again
bool VictimBrain31() {
    static const bool on = getenv("MGO1_DOLL_PROBE_VICTIM_BRAIN") != nullptr;
    static long long from = -1;
    if (!on || gP31.role != 2) return false;
    if (from >= 0 && gFrame >= from && gFrame - from < 2700) {
        // (knocked down, its own getting up's inputs: the player's work 20 and 10 frames before each and 12 frames after it, the words
        // the input changed (its motion: a getting up begun, or none))
        static std::vector<uint32_t> w0, w1, w2;
        static long long snapAt = -100, pressAt = -1;
        static std::string what;
        if (gMe.bits[0] & 0x18000000) {
            const Pad& lp = LastPad();
            const uint32_t b = lp.buttons;
            const bool stick = std::abs((int)lp.sticks[2] - 0x80) > 0x30 || std::abs((int)lp.sticks[3] - 0x80) > 0x30;
            const char* w = (b & 0x4000) ? "Cross" : (b & 0x1000) ? "Triangle" : (b & 0x2000) ? "Circle" : stick ? "the stick" : nullptr;
            if (w && pressAt < 0 && !w0.empty() && gFrame - snapAt <= 12) { pressAt = gFrame; what = w; }
            if (!w && pressAt < 0 && gFrame - snapAt >= 10) { w0 = w1; Work31(w1); snapAt = gFrame; }
            if (pressAt >= 0 && gFrame - pressAt == 12) {
                Work31(w2);
                WorkDiff31(("getting up, " + what + " +12, stamina " + std::to_string(gMe.stun)).c_str(), w0, w1, w1, w2);
                pressAt = -1; snapAt = -100; w0.clear(); w1.clear();
            }
        }
        return true;
    }
    if (from >= 0) { from = -1; Log("probe31: the victim's own brain off after 45 s: still again"); }
    if (gMe.alive && (gMe.bits[0] & 0x1A000000)) {
        from = gFrame;
        Log("probe31: the victim knocked out (state %x, stamina %d): its own brain from now", gMe.bits[0], gMe.stun);
        return true;
    }
    return false;
}

// ---- probe 44: the direct aim's places (v150-host beta 18). Still where it spawns. The first person view (R1 held) turned
// by the stick to the right, then up: the memory before and after each turn searched for the s16 angles (0x10000 a turn) that
// moved with the view; the pitch candidates written in turn (+10 degrees) until one moves the view, then the yaw beside it;
// the actor holding them. Then the bullets: the view's angles written and a shot (its direction against the view), the third
// person readied with the player's turn (work +0x142, its goal +0x14A) written and a shot, the view's pitch written in the
// third person and a shot (does it lift a third person bullet) ----
struct Probe44 { int phase = 0; long long at = 0; std::vector<uint8_t> s0, s1; float y[3] = {0, 0, 0}, p[3] = {0, 0, 0};
                 std::vector<uint32_t> pc, yc; size_t ci = 0; int mode = 0; uint32_t pa = 0; float camY0 = 0, camP0 = 0, f0 = 0, wantY = 0, wantP = 0;
                 int v0 = 0, sub = 0; std::string per; std::vector<uint32_t> seen; bool shotSaid = false; };
Probe44 gP44;
void W16s(uint32_t a, int v) { const int16_t x = (int16_t)v; memcpy(ps2::gMem + (a & 0x01FFFFFF), &x, 2); }
struct Holder44 { uint32_t a, best = 0; std::string name; };
void FindHolder44(uint32_t w, const std::string& name, void* ctx) {
    Holder44& h = *(Holder44*)ctx;
    if (w <= h.a && h.a - w < 0x4000 && (!h.best || w > h.best)) { h.best = w; h.name = name; }
}
void CollectBullets44(uint32_t w, const std::string& name, void* ctx) { if (name == "NewBullet") ((std::vector<uint32_t>*)ctx)->push_back(w); }
// the newest bullet's direction (yaw, pitch in degrees: from its muzzle +0xE0 to its point +0x100), once it is 0.5 m out
bool Bullet44(std::vector<uint32_t>& seen, float* yaw, float* pitch) {
    std::vector<uint32_t> now; ps2world::ForEachActor(&CollectBullets44, &now);
    for (uint32_t w : now) {
        if (std::find(seen.begin(), seen.end(), w) != seen.end()) continue;
        float m[3], q[3]; for (int i = 0; i < 3; ++i) { m[i] = RF(w + 0xE0 + 4 * i); q[i] = RF(w + 0x100 + 4 * i); }
        const float dx = q[0] - m[0], dy = q[1] - m[1], dz = q[2] - m[2], h = std::sqrt(dx * dx + dz * dz);
        if (h < 150.f) continue;
        seen.push_back(w);
        *yaw = std::atan2(dx, dz) * 57.29578f; *pitch = std::atan2(dy, h) * 57.29578f;
        return true;
    }
    return false;
}
void Probe44Frame() {
    Probe44& P = gP44;
    const float kD = 3.14159265f / 180.f, k16 = 65536.f / 360.f;
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    Steer(0, 0, 0);
    float m[16];
    if (!ps2world::ViewMatrix(m)) return;
    const float yaw = std::atan2(m[8], m[10]), pitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
    const long long t = gFrame - P.at;
    auto snap = [](std::vector<uint8_t>& v) { v.assign(ps2::gMem, ps2::gMem + 0x02000000); };
    const uint32_t me = gMe.obj & 0x01FFFFFF;
    auto next = [&](int ph) { P.phase = ph; P.at = gFrame; P.sub = 0; P.per.clear(); P.shotSaid = false; };
    float by, bp;
    switch (P.phase) {
    case 0: {                                            // (the AK-47 in the hands first, when it has one) the view entered and at rest
        const Inventory& v = Inv();
        static int tries = 0;
        if (v.ok && v.weapon != 0xC && v.rounds[0xC] >= 0 && tries < 900) { ++tries; ProbeWindowTo(0, 0xC); P.at = gFrame - 1; return; }
        gPad.buttons |= 0x0800;
        if (t < 90) return;
        snap(P.s0); P.y[0] = yaw; P.p[0] = pitch;
        Log("probe44: view at rest: yaw %.2f pitch %.2f, facing %.2f, work %x: +0x142 %d +0x14A %d", yaw / kD, pitch / kD, Facing() / kD, me, R16s(me + 0x142), R16s(me + 0x14A));
        next(1); return;
    }
    case 1:                                              // the stick right 0.85 12 frames, then at rest
        gPad.buttons |= 0x0800;
        if (t < 12) { gPad.sticks[2] = (uint8_t)(0x80 + 108); return; }
        if (t < 60) return;
        snap(P.s1); P.y[1] = yaw; P.p[1] = pitch;
        next(2); return;
    case 2: {                                            // the stick up 0.85 8 frames, then at rest: the search
        gPad.buttons |= 0x0800;
        if (t < 8) { gPad.sticks[3] = (uint8_t)(0x80 - 108); return; }
        if (t < 60) return;
        P.y[2] = yaw; P.p[2] = pitch;
        const float dy = Wrap(P.y[1] - P.y[0]) * 360.f / 6.2831853f * k16, dp = (P.p[2] - P.p[1]) * 360.f / 6.2831853f * k16;
        Log("probe44: turned: yaw %.2f deg, pitch %.2f deg", dy / k16, dp / k16);
        for (uint32_t a = 0x100000; a + 2 <= 0x02000000; a += 2) {
            int16_t x0, x1, x2; memcpy(&x0, &P.s0[a], 2); memcpy(&x1, &P.s1[a], 2); memcpy(&x2, ps2::gMem + a, 2);
            const float d1 = (float)(int16_t)(x1 - x0), d2 = (float)(int16_t)(x2 - x1);
            if (std::fabs(std::fabs(d1) - std::fabs(dy)) < 0.05f * std::fabs(dy) + 3.f && std::fabs(d2) < 3.f && P.yc.size() < 200) P.yc.push_back(a);
            if (std::fabs(std::fabs(d2) - std::fabs(dp)) < 0.05f * std::fabs(dp) + 3.f && std::fabs(d1) < 3.f && P.pc.size() < 200) P.pc.push_back(a);
        }
        Log("probe44: %d yaw candidates, %d pitch candidates", (int)P.yc.size(), (int)P.pc.size());
        P.s0.clear(); P.s0.shrink_to_fit(); P.s1.clear(); P.s1.shrink_to_fit();
        next(3); P.ci = 0; return;
    }
    case 3: {                                            // the pitch candidates written +10 degrees in turn: the one that moves the view
        gPad.buttons |= 0x0800;
        if (P.ci >= P.pc.size()) { Log("probe44: no pitch candidate moved the view"); next(9); return; }
        const uint32_t a = P.pc[P.ci];
        if (t < 8) return;
        if (t == 8) { P.camP0 = pitch; P.v0 = R16s(a); W16s(a, P.v0 - (int)(10.f * k16)); return; }
        if (t < 11) return;
        const float moved = (pitch - P.camP0) / kD;
        if (std::fabs(moved) > 5.f && R16s(a) == (int16_t)(P.v0 - (int)(10.f * k16))) {
            P.pa = a;
            Holder44 h; h.a = a; ps2world::ForEachActor(&FindHolder44, &h);
            int ptrs = 0; std::string where;
            if (h.best) for (uint32_t q = 0x100000; q + 4 <= 0x02000000 && ptrs < 8; q += 4) if (R32(q) == h.best || R32(q) == (h.best | 0x20000000)) { char b[40]; snprintf(b, sizeof b, " %x", q); where += b; ++ptrs; }
            Log("probe44: the view's pitch is %x (the value %d -> %d, the view %+.1f deg, kept); in %s's work %x +0x%x; the work pointed at from%s; the yaw beside it +2 = %d (facing %d)",
                a, P.v0, R16s(a), moved, h.best ? h.name.c_str() : "no actor", h.best, a - h.best, where.c_str(), R16s(a + 2), R16s(me + 0x142));
            W16s(a, P.v0);
            next(4); return;
        }
        W16s(a, P.v0); ++P.ci; P.at = gFrame; return;
    }
    case 4: {                                            // the yaw beside it written +20 degrees: once, then every frame 10 frames
        gPad.buttons |= 0x0800;
        const uint32_t a = P.pa + 2;
        if (t < 30) return;
        if (t == 30) { P.camY0 = yaw; P.f0 = Facing(); P.v0 = R16s(a); W16s(a, P.v0 + (int)(20.f * k16)); }
        else if (P.sub == 1 && t < 40) W16s(a, P.v0 + (int)(20.f * k16));
        if (t > 30 && t <= 46) { char b[48]; snprintf(b, sizeof b, " %.1f/%.1f", Wrap(yaw - P.camY0) / kD, Wrap(Facing() - P.f0) / kD); P.per += b; }
        if (t == 46) {
            Log("probe44: the view's yaw %x written +20 deg %s: the view/facing each frame:%s; the value now %d (written %d), +0x142 %d", a, P.sub ? "every frame 10" : "once",
                P.per.c_str(), R16s(a), (int16_t)(P.v0 + (int)(20.f * k16)), R16s(me + 0x142));
            P.per.clear(); P.at = gFrame;
            if (++P.sub > 1) next(5);
        }
        return;
    }
    case 5: {                                            // the view turned by writing at 26 degrees a frame to +5 yaw, -5 (up) pitch, then a shot
        gPad.buttons |= 0x0800 | 0x8000; gPad.pressures[11] = 0x3F;
        const uint32_t a = P.pa;
        if (t < 40) return;
        if (t == 40) { P.camY0 = yaw; P.camP0 = pitch; P.wantY = R16s(a + 2) + 5.f * k16; P.wantP = R16s(a) - 5.f * k16; std::vector<uint32_t> n; ps2world::ForEachActor(&CollectBullets44, &n); P.seen = n; }
        if (t >= 40 && t < 60) {
            const float cy = R16s(a + 2), cp = R16s(a), st = 26.f * k16;
            const float ey = (float)(int16_t)(int)(P.wantY - cy), ep = P.wantP - cp;
            W16s(a + 2, (int)lrintf(cy + std::max(-st, std::min(st, ey)))); W16s(a, (int)lrintf(cp + std::max(-st, std::min(st, ep))));
        }
        if (t >= 60 && t < 66) gPad.pressures[11] = 0xFF;
        if (t >= 60 && !P.shotSaid && Bullet44(P.seen, &by, &bp)) { P.shotSaid = true; Log("probe44: first person view's bullet %lld frames after the pull: yaw %.2f pitch %.2f; the view yaw %.2f pitch %.2f", t - 60, by, bp, yaw / kD, pitch / kD); }
        if (t == 100) { if (!P.shotSaid) { std::vector<uint32_t> n; ps2world::ForEachActor(&CollectBullets44, &n); Log("probe44: first person view: no bullet seen (%d bullets now, state %x %x)", (int)n.size(), gMe.bits[0], gMe.bits[1]); } next(6); }
        return;
    }
    case 6: {                                            // third person readied: the turn written 30 degrees (each trial the other way), the pull d frames after
        static const int kDelay[] = {0, 1, 2, 4, 6, 8, 12, 20, 30};
        gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
        if (P.sub >= 9) { next(9); return; }
        const int d = kDelay[P.sub];
        if (t < 50) return;
        if (t == 50) {
            P.f0 = Facing(); std::vector<uint32_t> n; ps2world::ForEachActor(&CollectBullets44, &n); P.seen = n;
            const int v = R16s(me + 0x142) + (P.sub % 2 ? -1 : 1) * (int)(30.f * k16); W16s(me + 0x142, v); W16s(me + 0x14A, v);
        }
        if (t > 50 + d && t <= 56 + d) gPad.pressures[11] = 0xFF;
        if (t > 50 + d && !P.shotSaid && Bullet44(P.seen, &by, &bp)) {
            P.shotSaid = true;
            Log("probe44: third person, the turn written 30 deg, the pull %d frames after: the bullet %lld frames after the pull, %.2f deg off the facing (yaw %.2f, facing %.2f)", d, t - 51 - d,
                Wrap(by * kD - Facing()) / kD, by, Facing() / kD);
        }
        if (t == 110) { if (!P.shotSaid) Log("probe44: third person: no bullet seen (pull %d)", d); const int sb = P.sub + 1; next(6); P.sub = sb; }
        return;
    }
    default:
        if (gFrame % 600 == 0) Log("probe44: done");
        return;
    }
}

// ---- probe 45 (v150-host beta 21, geom): the stances a player takes, held for the host's virtual dolls to look at. Each
// life: to the place of MGO1_DOLL_PROBE_AT ("x,y,z", a floor or hips point; none: where it stands), then by
// MGO1_DOLL_PROBE_STANCE: "stand", "crouch" (Cross), "lie" (Cross, then the stick: a crouched player who moves lies down;
// the stick let go, he lies still) or, by default, "cycle" (standing 1 s, crouched 2 s, lying for the rest of the life).
// Logged at each change: its state word, its hips over the floor ----
struct Probe45 { long long life = -1, at = -1, crossAt = -1000; int phase = 0; uint32_t st = ~0u; bool there = false; float goal[3] = {0, 0, 0}; bool haveGoal = false; };
Probe45 gP45;
void Probe45Frame() {
    Probe45& P = gP45;
    static int want = -1;
    if (want < 0) {
        const char* e = getenv("MGO1_DOLL_PROBE_STANCE");
        want = !e ? 3 : !strcmp(e, "stand") ? 0 : !strcmp(e, "crouch") ? 1 : !strcmp(e, "lie") ? 2 : 3;
        if (const char* a = getenv("MGO1_DOLL_PROBE_AT")) P.haveGoal = sscanf(a, "%f,%f,%f", &P.goal[0], &P.goal[1], &P.goal[2]) == 3;
    }
    if (gRoom.phase != 2) { P.life = -1; return; }
    if (P.life < 0 || gFrame - P.life > 3 || !gMe.alive) { P.at = gFrame; P.phase = 0; P.there = !P.haveGoal; P.crossAt = -1000; }   // (a new life)
    P.life = gFrame;
    gPad.buttons = 0;
    Steer(0, 0, 0);
    const uint32_t st = gMe.bits[0] & 0xFFFF;
    if (st != P.st) { Log("probe45: state %x (%x), hips %.0f over the floor, at %.0f %.0f %.0f", st, gMe.bits[0], HipHeight(), gMe.pos[0], gMe.pos[1], gMe.pos[2]); P.st = st; }
    if (!P.there) {                                       // to the place first (30 s at most)
        const float d = std::hypot(gMe.pos[0] - P.goal[0], gMe.pos[2] - P.goal[2]);
        if (d < 350.f || gFrame - P.at > 1800) { P.there = true; P.at = gFrame; Log("probe45: at the place (%.0f mm from it): %.0f %.0f %.0f", d, gMe.pos[0], gMe.pos[1], gMe.pos[2]); return; }
        if (!Following() || (gFrame - P.at) % 90 == 0) PlanTo(P.goal);
        FollowPath();
        return;
    }
    const long long t = gFrame - P.at;
    const bool crouched = (st & 4) != 0, lying = (st & 8) != 0;
    const int stance = want == 3 ? (t < 60 ? 0 : t < 180 ? 1 : 2) : want;
    if (stance == 0 || t < 20) return;                    // (standing; a moment still first)
    if (!crouched && !lying) {                            // Cross: down to a crouch (not twice within 45 frames)
        if (gFrame - P.crossAt > 45) P.crossAt = gFrame;
        if (gFrame - P.crossAt < 3) gPad.buttons |= 0x4000;
        return;
    }
    if (stance == 2 && crouched && !lying && gFrame - P.crossAt > 30) gPad.sticks[3] = 0x00;   // (crouched, the stick: he lies down)
}

// ---- probe 46 (v150-host beta 22, heavy): the RPG-7 and the flamethrower as a player fires them. Doll 1 (the actor) is given
// MGO1_DOLL_PROBE_WEAPON (hex: 11 the RPG-7, 9 the flamethrower; written into its equipment as a weapon box gives it, rounds
// MGO1_DOLL_PROBE_ROUNDS) at each life, takes it with the weapon window, and fires at the nearest other player of each kind in
// turn (a virtual doll, named VIRTUAL..., then a playable doll), from spots at the trial's distances with a clear line: the
// RPG-7 at his chest, at the floor 1.5 m before him, at the floor 3 m beside him; the flamethrower held 60 frames in the first
// person view and in the third person. Logged: each shot's frame, the rounds, the target's life as this game holds it before
// and at its lowest in the 150 frames after, and each rocket, blast and flame actor of this game (its place every 4 frames).
// The other dolls (the victims) stand still where they are, facing the actor, their own life, stamina and state logged at each
// change ----
struct Probe46 { int phase = 0, dIdx = 0, trial = 0, kind = 0, lifeBefore = 0, lifeMin = 0, roundsBefore = 0, target = -1, shots = 0;
                 long long at = 0, onSince = -1, giveAt = -1000, life = -1; bool spotHave = false; float spot[3] = {0, 0, 0}, spotFor[3] = {0, 0, 0};
                 int lastLife = -1, lastStun = -1; uint32_t lastBits0 = 0, lastBits1 = 0; };
Probe46 gP46;
struct Seen46 { uint32_t w; std::string name; long long born; float p[3]; };
std::vector<Seen46> gSeen46;
const char* const kWatch46[] = {"NewSniperBulletRPG7", "NewBulletRPG7", "NewBlast", "NewBlastWound", "NewOnlineFireThrower", "NewFireThrowerFury",
                                "NewFuryFireThrowerSeed", "NewFireForChara", "NewFireForCharaNoShimmer", "NewFireSingle2Life", "NewFireSingle2LifeFollow",
                                "NewExplosion_FireBall", "NewWeaponRGP7", "NewWeaponNetFlameThrower"};
bool Watched46(const std::string& n) { for (const char* w : kWatch46) if (n == w) return true; return false; }
// an actor's place: a bullet's object (+0x40) +0x10, a blast's +0x50, else its work +0x1A0 (the boxes' and the props')
void Place46(uint32_t w, const std::string& n, float* p) {
    uint32_t a = w + 0x50;
    if (n == "NewSniperBulletRPG7" || n == "NewBulletRPG7") { const uint32_t o = R32(w + 0x40); a = o >= 0x100000 && o < 0x2000000 ? o + 0x40 : w + 0x50; }   // (its matrix +0x10, the place its fourth row)
    for (int i = 0; i < 3; ++i) p[i] = RF(a + 4 * i);
}
void Watch46() {
    struct Ctx { std::vector<std::pair<uint32_t, std::string>> now; } c;
    ps2world::ForEachActor([](uint32_t w, const std::string& name, void* ctx) { if (Watched46(name)) ((Ctx*)ctx)->now.push_back({w, name}); }, &c);
    for (auto it = gSeen46.begin(); it != gSeen46.end();) {
        bool live = false;
        for (const auto& n : c.now) if (n.first == it->w && n.second == it->name) live = true;
        if (!live) { Log("probe46: actor %s %08x gone after %lld frames, last at %.0f %.0f %.0f", it->name.c_str(), it->w, gFrame - it->born, it->p[0], it->p[1], it->p[2]); it = gSeen46.erase(it); }
        else ++it;
    }
    for (const auto& n : c.now) {
        Seen46* s = nullptr;
        for (Seen46& x : gSeen46) if (x.w == n.first && x.name == n.second) s = &x;
        float p[3];
        Place46(n.first, n.second, p);
        if (!s) {
            gSeen46.push_back({n.first, n.second, gFrame, {p[0], p[1], p[2]}});
            Log("probe46: actor %s %08x made at %.0f %.0f %.0f (me at %.0f %.0f %.0f)", n.second.c_str(), n.first, p[0], p[1], p[2], gMe.pos[0], gMe.pos[1], gMe.pos[2]);
            if (n.second == "NewBlast" || n.second == "NewBlastWound")
                Log("probe46: %s %08x: attack word %08x, +0x54 %.0f, +0x60 %d, +0x64 %d, +0x68 %d", n.second.c_str(), n.first, R32(n.first + 0x6C), RF(n.first + 0x54), (int)R32(n.first + 0x60), (int)R32(n.first + 0x64), (int)R32(n.first + 0x68));
            continue;
        }
        if ((gFrame - s->born) % 4 == 0 && (n.second == "NewSniperBulletRPG7" || n.second == "NewBulletRPG7"))
            Log("probe46: actor %s %08x +%lld at %.0f %.0f %.0f (%.0f mm since the last)", n.second.c_str(), n.first, gFrame - s->born, p[0], p[1], p[2],
                std::sqrt((p[0] - s->p[0]) * (p[0] - s->p[0]) + (p[1] - s->p[1]) * (p[1] - s->p[1]) + (p[2] - s->p[2]) * (p[2] - s->p[2])));
        memcpy(s->p, p, sizeof s->p);
    }
}
// the weapon written into the equipment's list and its state's rounds, as a weapon box gives it (+0x18 the rounds had, +0x1C the
// magazine: its size at most)
bool Give46(int code, int rounds) {
    const uint32_t eq = EquipBase(), st = WeaponState(code);
    if (!eq || !st) return false;
    const int n = (int)R32(eq + 0x5AC);
    if (n < 0 || n > 8) return false;
    int at = -1, free = -1;
    std::string list;
    for (int k = 0; k < n; ++k) { const int c = R16s(eq + 0x5B4 + 2 * k); char b[12]; snprintf(b, sizeof b, " %x", c); list += b; if (c == code) at = k; if (c <= 0 && free < 0) free = k; }
    if (at < 0) at = free;
    if (at < 0 && n < 8) { at = n; const uint32_t n1 = (uint32_t)n + 1; memcpy(ps2::gMem + ((eq + 0x5AC) & 0x01FFFFFF), &n1, 4); }
    if (at < 0) return false;
    const int16_t c16 = (int16_t)code, r16 = (int16_t)rounds, size = R16s(st + 0x1E), m16 = (int16_t)std::max(1, std::min<int>(rounds, size > 0 ? size : 1));
    memcpy(ps2::gMem + ((eq + 0x5B4 + 2 * at) & 0x01FFFFFF), &c16, 2);
    memcpy(ps2::gMem + ((st + 0x18) & 0x01FFFFFF), &r16, 2);
    if (R16s(st + 0x1A) < rounds) memcpy(ps2::gMem + ((st + 0x1A) & 0x01FFFFFF), &r16, 2);
    memcpy(ps2::gMem + ((st + 0x1C) & 0x01FFFFFF), &m16, 2);
    Log("probe46: the weapon %x given (the list:%s; slot %d; its state %08x: rounds %d, most %d, magazine %d of %d)", code, list.c_str(), at, st, R16s(st + 0x18), R16s(st + 0x1A),
        R16s(st + 0x1C), R16s(st + 0x1E));
    return true;
}
void Probe46Frame() {
    Probe46& P = gP46;
    static int code = -1, rounds = 30;
    if (code < 0) {
        code = getenv("MGO1_DOLL_PROBE_WEAPON") ? (int)strtol(getenv("MGO1_DOLL_PROBE_WEAPON"), nullptr, 16) : 0x11;
        if (getenv("MGO1_DOLL_PROBE_ROUNDS")) rounds = std::max(1, atoi(getenv("MGO1_DOLL_PROBE_ROUNDS")));
        Log("probe46: the %s (doll %d), the weapon %x", gSet.number <= 1 ? "actor" : "victim", gSet.number, code);
    }
    Watch46();
    if (gRoom.phase != 2) { P.life = -1; return; }
    const bool newLife = P.life < 0 || gFrame - P.life > 3;
    P.life = gFrame;
    if (gSet.number > 1) {                               // a victim: still, facing the actor; its own life, stamina and state at each change
        gPad.buttons = 0;
        Steer(0, 0, 0);
        if (gMe.life != P.lastLife || gMe.stun != P.lastStun || gMe.bits[0] != P.lastBits0 || gMe.bits[1] != P.lastBits1) {
            Log("probe46: victim: life %d stun %d state %x %x (was %d %d %x %x), at %.0f %.0f %.0f", gMe.life, gMe.stun, gMe.bits[0], gMe.bits[1], P.lastLife, P.lastStun,
                P.lastBits0, P.lastBits1, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
            P.lastLife = gMe.life; P.lastStun = gMe.stun; P.lastBits0 = gMe.bits[0]; P.lastBits1 = gMe.bits[1];
        }
        return;
    }
    if (gFrame < 4800) { P.at = gFrame; return; }
    const bool rpg = code == 0x11;
    static const float kRpgDist[3] = {8000.f, 15000.f, 25000.f}, kFlameDist[4] = {2000.f, 3500.f, 5000.f, 7000.f};
    const int nDist = rpg ? 3 : 4, nTrials = rpg ? 4 : 2;
    if (P.dIdx >= nDist) { if (gFrame % 600 == 0) Log("probe46: done (%d shots)", P.shots); Steer(0, 0, 0); return; }
    const Inventory& v = Inv();
    if (newLife || (v.ok && v.rounds[code] < 0 && gFrame - P.giveAt > 120)) {
        if (!v.ok || v.rounds[code] < 0) { if (Give46(code, rounds)) P.giveAt = gFrame; }
        if (newLife) { P.phase = 0; P.at = gFrame; P.spotHave = false; }
    }
    // the target: the nearest other player of this trial's kind (the host never)
    const Player* tgt = nullptr; float best = 1e30f;
    for (const Player& p : gPlayers) {
        if (p.local || !p.alive || p.team == 0xFE) continue;
        const bool virt = p.name.compare(0, 7, "VIRTUAL") == 0;
        if (virt != (P.kind == 0)) continue;
        if (Dist2D(p.pos, gMe.pos) < best) { best = Dist2D(p.pos, gMe.pos); tgt = &p; }
    }
    const long long t = gFrame - P.at;
    auto roundsNow = [&]() { const uint32_t st = WeaponState(code); return st ? R16s(st + 0x18) : -1; };
    if (!tgt) { Steer(0, 0, 0); if (t > 1200) { P.kind ^= 1; P.at = gFrame; P.phase = 0; } return; }
    const float want = rpg ? kRpgDist[P.dIdx] : kFlameDist[P.dIdx], dist = best;
    float m[16] = {};
    const bool view = ps2world::ViewMatrix(m);
    const float gy = MapReady() ? gMap->nav->GroundBelow(tgt->pos, 2500.f) : -1e30f;
    const float h = gy > -1e29f ? std::max(250.f, tgt->pos[1] - gy) : 800.f;
    float aim[3] = {tgt->pos[0], tgt->pos[1] + 0.45f * h, tgt->pos[2]};
    if (rpg && P.trial > 0) {                              // the floor 1.5 m before him (toward the actor), or 3 m beside him
        const float a = AngleTo(tgt->pos, gMe.pos) + 1.5708f, r = P.trial == 1 ? 2500.f : P.trial == 2 ? 4500.f : 6500.f;   // (the floor beside him)
        aim[0] = tgt->pos[0] + std::sin(a) * r; aim[2] = tgt->pos[2] + std::cos(a) * r;
        float q[3]; const float c[3] = {aim[0], tgt->pos[1] - 800.f, aim[2]};
        aim[1] = MapReady() && gMap->nav->Nearest(c, 1500.f, q) ? q[1] + 50.f : tgt->pos[1] - 750.f;
    }
    const bool tps = !rpg && P.trial == 1;                 // (the flamethrower's second trial: the third person, its facing turned)
    if (P.phase >= 1 && (gMe.bits[0] != P.lastBits0 || gMe.bits[1] != P.lastBits1)) {   // (its own state at each change: the sight's opening, the reload)
        Log("probe46: phase %d +%lld: my state %x %x (was %x %x), in the hands %x, the rounds %d, magazine %d", P.phase, t, gMe.bits[0], gMe.bits[1], P.lastBits0, P.lastBits1, v.weapon,
            WeaponState(code) ? R16s(WeaponState(code) + 0x18) : -1, WeaponState(code) ? R16s(WeaponState(code) + 0x1C) : -1);
        P.lastBits0 = gMe.bits[0]; P.lastBits1 = gMe.bits[1];
    }
    switch (P.phase) {
    case 0: {                                            // the weapon in its hands, then to a spot at the distance with a clear line to its chest
        // (the flamethrower taken first: a player runs with it; the RPG-7 holds him where he stands: the handgun to walk, the
        // RPG-7 at the spot, phase 1)
        if (!rpg && v.ok && v.rounds[code] >= 0 && v.weapon != code) { ProbeWindowTo(0, code); return; }
        if (rpg && v.ok && v.weapon == code) { ProbeWindowTo(0, v.rounds[6] >= 0 ? 6 : 1); return; }
        const float eye[3] = {gMe.pos[0], gMe.pos[1] + 760.f, gMe.pos[2]};
        const float chest[3] = {tgt->pos[0], tgt->pos[1] + 0.45f * h, tgt->pos[2]};
        if (std::fabs(dist - want) < (rpg ? 1500.f : 600.f) && LineClear(eye, chest)) { StopPath(); Steer(0, 0, 0); P.phase = 1; P.at = gFrame; P.onSince = -1; P.spotHave = false; return; }
        if (t > 2400) { Log("probe46: %.1f m not reached (at %.1f m from %s)", want / 1000.f, dist / 1000.f, tgt->name.c_str()); P.kind ^= 1; if (!P.kind) P.dIdx++; P.trial = 0; P.at = gFrame; StopPath(); P.spotHave = false; return; }
        if (P.spotHave && Dist2D(P.spotFor, tgt->pos) > 1000.f) P.spotHave = false;
        if (!P.spotHave && MapReady()) {
            float bestD = 1e30f;
            for (int k = 0; k < 36; ++k) {
                const float a = k * 10.f / 57.2958f;
                const float c[3] = {tgt->pos[0] + std::sin(a) * want, tgt->pos[1] - 800.f, tgt->pos[2] + std::cos(a) * want};
                float q[3];
                if (!gMap->nav->Nearest(c, 800.f, q) || std::fabs(Dist2D(q, tgt->pos) - want) > (rpg ? 800.f : 400.f)) continue;
                const float e2[3] = {q[0], q[1] + 1560.f, q[2]};
                if (!LineClear(e2, chest) || Dist2D(q, gMe.pos) >= bestD) continue;
                bestD = Dist2D(q, gMe.pos); memcpy(P.spot, q, sizeof P.spot); P.spotHave = true;
            }
            memcpy(P.spotFor, tgt->pos, sizeof P.spotFor);
            if (!P.spotHave) { Log("probe46: no spot %.1f m from %s with a clear line", want / 1000.f, tgt->name.c_str()); P.kind ^= 1; if (!P.kind) P.dIdx++; P.trial = 0; P.at = gFrame; return; }
            StopPath();
        }
        if (Dist2D(gMe.pos, P.spot) < 500.f) { StopPath(); Steer(0, 0, 0); if (t % 120 == 0) P.spotHave = false; return; }
        if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
        FollowPath();
        return;
    }
    case 1: {                                            // its aim on the point, still 12 frames (the first person view: R1, or the RPG-7's own sight)
        if (v.weapon != code) {
            if (P.onSince != -2) { P.onSince = -2; Log("probe46: taking the weapon %x at the spot (in the hands %x, state %x %x)", code, v.weapon, gMe.bits[0], gMe.bits[1]); }
            Steer(0, 0, 0);
            if (!ProbeWindowTo(0, code) && t > 600) { Log("probe46: the weapon %x not taken in 600 frames", code); P.phase = 0; P.at = gFrame; }
            return;
        }
        if (P.onSince == -2) { P.onSince = -1; P.at = gFrame; Log("probe46: the weapon %x in the hands (state %x %x)", code, gMe.bits[0], gMe.bits[1]); return; }
        if (rpg) {                                       // (an empty tube: a pull reloads it, as a player's; its time logged)
            const uint32_t st = WeaponState(code);
            static long long pullAt = -1;
            if (st && R16s(st + 0x1C) <= 0) {
                if (pullAt < 0) { pullAt = gFrame; Log("probe46: the tube empty: a pull to reload (state %x %x)", gMe.bits[0], gMe.bits[1]); }
                if (gFrame - pullAt < 8) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }
                if (gFrame - pullAt > 400) { Log("probe46: no reload 400 frames after the pull"); pullAt = -1; }
                P.at = gFrame;
                return;
            }
            if (pullAt >= 0) { Log("probe46: reloaded %lld frames after the pull (state %x %x)", gFrame - pullAt, gMe.bits[0], gMe.bits[1]); pullAt = -1; P.at = gFrame; return; }
            float e[3] = {m[12], m[13], m[14]};
            if (view && P.trial > 0 && t == 1) {         // (the point's lines: from the eye, and from the point to his chest)
                const float c[3] = {tgt->pos[0], tgt->pos[1] + 0.45f * h, tgt->pos[2]}, up[3] = {aim[0], aim[1] + 300.f, aim[2]};
                Log("probe46: the floor point %.0f %.0f %.0f: the line from the eye %s, from it to his chest %s", aim[0], aim[1], aim[2], LineClear(e, aim) ? "clear" : "BLOCKED",
                    LineClear(up, c) ? "clear" : "BLOCKED");
            }
        }
        if (tps) {
            gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
            if (t < 15) return;
            const float goal = AngleTo(gMe.pos, aim), err = Wrap(goal - Facing());
            if (std::fabs(err) > 1.5f / 57.2958f) { P.onSince = -1; Steer(std::sin(goal), std::cos(goal), std::fabs(err) > 8.f / 57.2958f ? 0.64f : 0.57f); }
            else { Steer(0, 0, 0); if (P.onSince < 0) P.onSince = gFrame; }
        } else {
            if (!rpg) gPad.buttons |= 0x0800;
            if (!view || t < (rpg ? 40 : 20)) return;
            const float yaw = std::atan2(m[8], m[10]), pitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
            const float tx = aim[0] - m[12], tz = aim[2] - m[14], ty = aim[1] - m[13];
            const float ye = Wrap(std::atan2(tx, tz) - yaw), pe = std::atan2(ty, std::sqrt(tx * tx + tz * tz)) - pitch;
            auto s = [](float deg) { const float a = std::fabs(deg); const float x = a < 0.2f ? 0.f : a < 1.5f ? (gFrame % 8 < 2 ? 0.46f : 0.f) : std::min(1.f, 0.36f + a * 0.045f); return deg < 0 ? -x : x; };
            const float tol = std::max(0.3f, std::atan2(80.f, dist) * 57.2958f);
            const bool on = std::fabs(ye) * 57.2958f < tol && std::fabs(pe) * 57.2958f < tol;
            if (!on) { P.onSince = -1; gPad.sticks[2] = (uint8_t)(0x80 + (int)(-s(ye * 57.2958f) * 127)); gPad.sticks[3] = (uint8_t)(0x80 - (int)(s(pe * 57.2958f) * 127)); }
            else if (P.onSince < 0) P.onSince = gFrame;
            if (t > 900) { Log("probe46: the aim not settled (%.2f %.2f deg, state %x %x)", ye * 57.2958f, pe * 57.2958f, gMe.bits[0], gMe.bits[1]); P.phase = 0; P.at = gFrame; return; }
        }
        if (P.onSince < 0 || gFrame - P.onSince < 12) return;
        P.lifeBefore = P.lifeMin = tgt->life; P.roundsBefore = roundsNow(); P.target = tgt->id;
        Log("probe46: %s at %s %.1f m (%s; %s), his life %d, the rounds %d, state %x %x, its eye %.0f %.0f %.0f, the point %.0f %.0f %.0f", rpg ? "the RPG-7" : "the flamethrower",
            tgt->name.c_str(), dist / 1000.f, rpg ? (P.trial == 0 ? "his chest" : P.trial == 1 ? "the floor 2.5 m beside him" : P.trial == 2 ? "the floor 4.5 m beside him" : "the floor 6.5 m beside him") : tps ? "the third person" : "the first person view",
            tgt->name.compare(0, 7, "VIRTUAL") == 0 ? "a virtual doll" : "a playable doll", P.lifeBefore, P.roundsBefore, gMe.bits[0], gMe.bits[1], m[12], m[13], m[14], aim[0], aim[1], aim[2]);
        P.phase = 2; P.at = gFrame; ++P.shots;
        return;
    }
    case 2: {                                            // the trigger (the RPG-7 8 frames, the flamethrower held 60), then 150 frames
        const int pull = rpg ? 8 : 60;
        if (tps) gPad.buttons |= 0x8000; else gPad.buttons |= rpg ? 0x8000 : 0x8800;
        gPad.pressures[11] = t < pull ? 0xFF : (tps || !rpg ? 0x3F : 0);
        if (rpg && t >= pull) gPad.buttons &= ~0x8000;
        if (tgt->id == P.target) P.lifeMin = std::min(P.lifeMin, tgt->life);
        if (t % 10 == 0 && t <= pull + 150) Log("probe46: +%lld: %s's life %d (down %d, state %x %x), the rounds %d, my life %d, state %x %x", t, tgt->name.c_str(), tgt->life, tgt->down ? 1 : 0,
                                              tgt->bits[0], tgt->bits[1], roundsNow(), gMe.life, gMe.bits[0], gMe.bits[1]);
        if (t < pull + 150) return;
        Log("probe46: done at %s %.1f m: his life %d -> %d%s, the rounds %d -> %d", tgt->name.c_str(), dist / 1000.f, P.lifeBefore, P.lifeMin, P.lifeMin <= 0 ? " (down)" : "",
            P.roundsBefore, roundsNow());
        if (++P.trial >= nTrials) { P.trial = 0; P.kind ^= 1; if (!P.kind) ++P.dIdx; }
        P.phase = 0; P.at = gFrame; P.onSince = -1;
        return;
    }
    }
}

// ---- probe 47 (v150-host beta 22, pickup): the special items lying on the map as a player takes them. Each life: to the nearest
// special lying in its own game's drops' records (ps2world::DropRecords: the M63, the RPG-7, the flamethrower, the thermal
// goggles; MGO1_DOLL_PROBE_SPECIAL=<hex code> that one only), its path, the last steps straight onto the box, still on it until
// its game takes it (the record no longer lying, the inventory holding it), 20 s at most there. Logged: the record before, the
// take with the inventory, the records after. Then the M63: in its hands by the weapon window and probe 10's trigger patterns
// (MGO1_DOLL_TRIG, "hold" by default: its rate, its magazine, its reload); the goggles: on 20 s, off 30 s, their battery (the
// player's +0xB6C) each second; the RPG-7 and the flamethrower: taken only (agent heavy measures their fire) ----
struct Probe47 { int phase = 0, number = -1, code = 0, kind = 0; long long at = 0, life = -1; float spot[3] = {0, 0, 0}; std::vector<int> tried; bool there = false; };
Probe47 gP47;
const char* Special47(int kind, int code) {
    return kind == 3 && code == 0x0D ? "M63" : kind == 3 && code == 0x11 ? "RPG-7" : kind == 3 && code == 0x09 ? "flamethrower" : kind == 1 && code == 7 ? "thermal goggles" : nullptr;
}
void Probe47Frame() {
    Probe47& P = gP47;
    if (gRoom.phase != 2) { P.life = -1; return; }
    if (P.life < 0 || gFrame - P.life > 3) { if (P.phase == 1) P.phase = 0; P.there = false; }   // (a new life: a new choice)
    P.life = gFrame;
    ps2world::DropRec d[16];
    const int n = ps2world::DropRecords(d, 16);
    const long long t = gFrame - P.at;
    if (P.phase == 0) {                                   // the nearest special lying, not tried yet
        gPad.buttons = 0; Steer(0, 0, 0);
        const int only = getenv("MGO1_DOLL_PROBE_SPECIAL") ? (int)strtol(getenv("MGO1_DOLL_PROBE_SPECIAL"), nullptr, 16) : -1;
        int best = -1; float bd = 1e30f;
        for (int i = 0; i < n; ++i) {
            if (!Special47(d[i].kind, d[i].code) || d[i].state != 0 || !d[i].box || (only >= 0 && d[i].code != only)) continue;
            if (std::find(P.tried.begin(), P.tried.end(), d[i].number) != P.tried.end()) continue;
            const float dist = std::hypot(d[i].pos[0] - gMe.pos[0], d[i].pos[2] - gMe.pos[2]);
            if (dist < bd) { bd = dist; best = i; }
        }
        if (best < 0) { if (gFrame % 600 == 0) Log("probe47: no special item lying that it has not tried (%d records)", n); return; }
        P.number = d[best].number; P.code = d[best].code; P.kind = d[best].kind; memcpy(P.spot, d[best].pos, sizeof P.spot);
        P.phase = 1; P.at = gFrame; P.there = false;
        Log("probe47: to the %s (record %#x, %d rounds, flags %04x) at %.0f %.0f %.0f (%.1f m); its reach %.0f", Special47(P.kind, P.code), (unsigned)P.number,
            d[best].count, d[best].flags, P.spot[0], P.spot[1], P.spot[2], bd / 1000.f, d[best].reach);
        return;
    }
    if (P.phase == 1) {                                   // to it, onto it, until taken
        int at = -1;
        for (int i = 0; i < n; ++i) if (d[i].number == P.number) at = i;
        const bool lying = at >= 0 && d[at].state == 0 && d[at].box && (d[at].kind == 3 || d[at].kind == 1);
        // (its own game's claim: the state 1, then 2 once sent (0x3A0BD0); the take follows in a few frames: still meanwhile)
        const bool claimed = at >= 0 && (d[at].state == 1 || d[at].state == 2) && (d[at].kind == 3 || d[at].kind == 1);
        const Inventory& v = Inv();
        const bool had = v.ok && (P.kind == 1 ? v.count[P.code & 0x3F] > 0 : v.rounds[P.code & 0x3F] >= 0);
        static long long claimAt = -1;
        if (claimed && !had) {
            if (claimAt < 0) { claimAt = gFrame; Log("probe47: the %s (record %#x) claimed by its own game (state %d) at %.0f mm along x, %.0f along z", Special47(P.kind, P.code), (unsigned)P.number, d[at].state,
                                                    std::fabs(gMe.pos[0] - d[at].pos[0]), std::fabs(gMe.pos[2] - d[at].pos[2])); }
            StopPath(); Steer(0, 0, 0);
            if (gFrame - claimAt < 180) return;
        }
        if (!claimed) claimAt = -1;
        if (!lying) {
            claimAt = -1;
            P.tried.push_back(P.number);
            Log("probe47: the %s (record %#x) %s after %lld frames: the record %s; its inventory %s (rounds %d, count %d); at %.0f %.0f %.0f, %.0f mm from the box along x, %.0f along z",
                Special47(P.kind, P.code), (unsigned)P.number, had ? "TAKEN" : claimed ? "claimed, not given (180 frames)" : "gone (another player took it)", t,
                at < 0 ? "gone" : (std::string("kind ") + std::to_string(d[at].kind) + " state " + std::to_string(d[at].state) + (d[at].box ? " box" : " no box")).c_str(),
                had ? "holds it" : "does not hold it", v.ok ? v.rounds[P.code & 0x3F] : -9, v.ok ? v.count[P.code & 0x3F] : -9, gMe.pos[0], gMe.pos[1], gMe.pos[2],
                std::fabs(gMe.pos[0] - P.spot[0]), std::fabs(gMe.pos[2] - P.spot[2]));
            for (int i = 0; i < n; ++i) if (d[i].number >= 0 && Special47(d[i].kind == 0 ? 3 : d[i].kind, d[i].code))
                Log("probe47:   record %#x kind %d code %x state %d count %d%s at %.0f %.0f %.0f", (unsigned)d[i].number, d[i].kind, d[i].code, d[i].state, d[i].count, d[i].box ? " box" : "", d[i].pos[0], d[i].pos[1], d[i].pos[2]);
            P.phase = had ? 2 : 0; P.at = gFrame;
            StopPath(); Steer(0, 0, 0);
            if (had && P.code == 0x0D) {                  // (the M63: probe 10's patterns with it in the hands)
                if (!getenv("MGO1_DOLL_TRIG")) _putenv("MGO1_DOLL_TRIG=hold");
                _putenv("MGO1_DOLL_TRIG_WEAPON=d");
            }
            return;
        }
        memcpy(P.spot, d[at].pos, sizeof P.spot);         // (a box still falling moves)
        const float dist = std::hypot(gMe.pos[0] - P.spot[0], gMe.pos[2] - P.spot[2]);
        if (t > 3600 || (P.there && t > 1200)) { P.tried.push_back(P.number); Log("probe47: the %s (record %#x) not taken in %lld frames, %.0f mm from it", Special47(P.kind, P.code), (unsigned)P.number, t, dist); P.phase = 0; StopPath(); return; }
        if (dist < 200.f) { if (!P.there) { P.there = true; P.at = gFrame - 1; Log("probe47: on the box (%.0f mm), its hips %.0f over it", dist, gMe.pos[1] - P.spot[1]); } Steer(0, 0, 0); return; }
        if (dist < 1200.f) { StopPath(); Steer(P.spot[0] - gMe.pos[0], P.spot[2] - gMe.pos[2], 0.5f); return; }
        if (!Following() || t % 120 == 0) PlanTo(P.spot);
        FollowPath();
        return;
    }
    if (P.phase == 2) {                                   // its use
        if (P.code == 0x0D) { Probe10Frame(); if (gP10.step >= (int)Patterns().size() && t > 600) { Log("probe47: the M63's patterns done"); P.phase = 0; P.at = gFrame; gP10 = Probe10(); } return; }
        if (P.code == 7 && P.kind == 1) {
            gPad.buttons = 0; Steer(0, 0, 0);
            const Inventory& v = Inv();
            const int bat = gMe.obj ? (int)R16s(gMe.obj + 0xB6C) : -1;
            if (t % 60 == 0) Log("probe47: the goggles %s, the battery %d (second %lld)", v.item == 7 ? "on" : "off", bat, t / 60);
            if (t < 1200) { if (v.item != 7) ProbeWindowTo(1, 7); return; }
            if (t < 3000) { if (v.item == 7) ProbeWindowTo(1, 0); return; }
            P.phase = 0; P.at = gFrame;
            return;
        }
        P.phase = 0; P.at = gFrame;                      // (the RPG-7, the flamethrower: taken only)
    }
}

// ---- probe 48 (v150-host beta 25, food: docs/VIRTUAL_DOLL.md 44): the food lying on the map as a player eats it. Each life: to the
// nearest food box lying in its own game's mushroom man (ps2world::FoodRecords: the entry's state 2, its box in the world) that it
// has not tried, its path, the last steps straight onto the box, still on it until its game takes it (the entry no longer lying),
// 20 s at most there. Logged: the food before (its code, the gain its game would give), the take with its own stun gauge (+0xB68)
// and life the frame before and 1, 10 and 60 frames after, the entries after; each food it took watched until the host's record
// makes it lie again (its return's time); every other player's copy's stun gauge at each change of 1000 or more (a virtual doll's
// eating as its records give it). MGO1_DOLL_PROBE_FOOD_GAUGE=<n>: before its first food and every third after, its gauge lowered
// to n through its own pending stun (+0xB72, as StaminaWatch: its own game takes it off at its next tick as a hit's); the foods
// between go with whatever gauge it has (a gain clamped at the maximum, the full gauge's take) ----
struct Probe48 { int phase = 0, number = -1, code = 0, gain = 0, before = -1, lifeBefore = -1, picks = 0; bool lowered = false; long long at = 0, life = -1, tookAt = -1; float spot[3] = {0, 0, 0};
                 std::vector<int> tried; std::vector<std::pair<int, long long>> gone; bool there = false; std::map<int, int> copies; };
Probe48 gP48;
const char* Food48(int code) { return code == 0x5B ? "GOLOVA" : code == 0x56 ? "RUSSIAN GLOWCAP" : code == 0x5E ? "RATION" : "food"; }
void Probe48Frame() {
    Probe48& P = gP48;
    if (gRoom.phase != 2) { P.life = -1; return; }
    if (P.life < 0 || gFrame - P.life > 3) { if (P.phase == 1) P.phase = 0; P.there = false; }   // (a new life: a new choice)
    P.life = gFrame;
    ps2world::FoodRec d[16];
    const int n = ps2world::FoodRecords(d, 16);
    const long long t = gFrame - P.at;
    for (const Player& p : gPlayers) {                    // (the copies' gauges: a virtual doll's records)
        if (p.local || p.team == 0xFE || !p.alive) continue;
        auto it = P.copies.find(p.id);
        if (it == P.copies.end() || std::abs(p.stun - it->second) >= 1000) {
            if (it != P.copies.end()) Log("probe48: player %d's copy: its stun gauge %d -> %d (%+d), life %d, at %.0f %.0f %.0f", p.id + 1, it->second, p.stun, p.stun - it->second, p.life, p.pos[0], p.pos[1], p.pos[2]);
            P.copies[p.id] = p.stun;
        }
    }
    for (auto g = P.gone.begin(); g != P.gone.end();) {    // (a food taken: back when the host's record makes it lie again)
        int at = -1;
        for (int i = 0; i < n; ++i) if (d[i].number == g->first) at = i;
        if (at >= 0 && d[at].state == 2 && d[at].box) {
            Log("probe48: the food %#x lies again after %lld frames (%.1f s on its own clock) at %.0f %.0f %.0f", (unsigned)g->first, gFrame - g->second, (gFrame - g->second) / 59.94,
                d[at].pos[0], d[at].pos[1], d[at].pos[2]);
            g = P.gone.erase(g);
        } else ++g;
    }
    if (P.tookAt >= 0 && gMe.obj) {                       // (its gauge after the take: 1, 10 and 60 frames)
        const long long a = gFrame - P.tookAt;
        if (a == 1 || a == 10 || a == 60) Log("probe48: %lld frames after the take: its stun gauge %d of %d, life %d", a, gMe.stun, (int)R16s(gMe.obj + 0xB6A), gMe.life);
        if (a > 60) P.tookAt = -1;
    }
    if (P.phase == 0) {                                   // the nearest food lying, not tried yet
        gPad.buttons = 0; Steer(0, 0, 0);
        static const int lowerTo = getenv("MGO1_DOLL_PROBE_FOOD_GAUGE") ? atoi(getenv("MGO1_DOLL_PROBE_FOOD_GAUGE")) : 0;
        if (lowerTo > 0 && P.picks % 3 == 0 && !P.lowered && gMe.obj) {   // (its gauge lowered first: its own game's pending stun)
            if (R16s(gMe.obj + 0xB72) != 0 || ((gMe.bits[4] >> 25) & 1)) return;
            P.lowered = true;
            if (gMe.stun > lowerTo) {
                const int16_t v = (int16_t)(gMe.stun - lowerTo);
                memcpy(ps2::gMem + ((gMe.obj + 0xB72) & 0x01FFFFFF), &v, 2);
                Log("probe48: its pending stun +0xB72 set to %d (its gauge %d of %d: %d after its next tick)", (int)v, gMe.stun, (int)R16s(gMe.obj + 0xB6A), lowerTo);
                return;
            }
        }
        int best = -1; float bd = 1e30f;
        for (int i = 0; i < n; ++i) {
            if (d[i].state != 2 || !d[i].box) continue;
            if (std::find(P.tried.begin(), P.tried.end(), d[i].number) != P.tried.end()) continue;
            const float dist = std::hypot(d[i].pos[0] - gMe.pos[0], d[i].pos[2] - gMe.pos[2]);
            if (dist < bd) { bd = dist; best = i; }
        }
        if (best < 0) {
            if (gFrame % 600 == 0) Log("probe48: no food lying that it has not tried (%d entries)", n);
            if (gFrame % 3600 == 0 && !P.tried.empty()) { P.tried.clear(); Log("probe48: every food tried: all again"); }
            return;
        }
        P.number = d[best].number; P.code = d[best].code; P.gain = d[best].gain; memcpy(P.spot, d[best].pos, sizeof P.spot);
        P.phase = 1; P.at = gFrame; P.there = false; ++P.picks; P.lowered = false;
        Log("probe48: to the %s (food %#x, code %#x, its game's gain %d) at %.0f %.0f %.0f (%.1f m); its reach %.0f; its stun gauge %d of %d, life %d", Food48(P.code),
            (unsigned)P.number, P.code, P.gain, P.spot[0], P.spot[1], P.spot[2], bd / 1000.f, d[best].reach, gMe.stun, gMe.obj ? (int)R16s(gMe.obj + 0xB6A) : -1, gMe.life);
        return;
    }
    int at = -1;
    for (int i = 0; i < n; ++i) if (d[i].number == P.number) at = i;
    const bool lying = at >= 0 && d[at].state == 2 && d[at].box;
    if (!lying) {
        P.tried.push_back(P.number);
        // (its own take: its gauge rose, or its hips within the game's zone, 400 mm along x and z: it takes the box before it is on it)
        const bool mine = P.there || (P.before >= 0 && gMe.stun > P.before) || (std::fabs(gMe.pos[0] - P.spot[0]) < 400.f && std::fabs(gMe.pos[2] - P.spot[2]) < 400.f);
        Log("probe48: the %s (food %#x) %s after %lld frames: the entry %s; its stun gauge %d -> %d (%+d; the gain read %d), life %d -> %d; at %.0f %.0f %.0f, %.0f mm from the box along x, %.0f along z",
            Food48(P.code), (unsigned)P.number, mine ? "TAKEN" : "gone (another player took it)", t,
            at < 0 ? "gone" : (std::string("state ") + std::to_string(d[at].state) + (d[at].box ? " box" : " no box") + " timer " + std::to_string(d[at].timer)).c_str(),
            P.before, gMe.stun, gMe.stun - P.before, P.gain, P.lifeBefore, gMe.life, gMe.pos[0], gMe.pos[1], gMe.pos[2], std::fabs(gMe.pos[0] - P.spot[0]), std::fabs(gMe.pos[2] - P.spot[2]));
        for (int i = 0; i < n; ++i)
            Log("probe48:   food %#x code %#x state %d timer %d%s at %.0f %.0f %.0f", (unsigned)d[i].number, d[i].code, d[i].state, d[i].timer, d[i].box ? " box" : "", d[i].pos[0], d[i].pos[1], d[i].pos[2]);
        if (mine) { P.gone.push_back({P.number, gFrame}); P.tookAt = gFrame; }
        P.phase = 0; P.at = gFrame;
        StopPath(); Steer(0, 0, 0);
        return;
    }
    P.before = gMe.stun; P.lifeBefore = gMe.life;            // (the frame before its game takes it)
    memcpy(P.spot, d[at].pos, sizeof P.spot);
    const float dist = std::hypot(gMe.pos[0] - P.spot[0], gMe.pos[2] - P.spot[2]);
    if (t > 3600 || (P.there && t > 1200)) { P.tried.push_back(P.number); Log("probe48: the %s (food %#x) not taken in %lld frames, %.0f mm from it, its hips %.0f over it", Food48(P.code), (unsigned)P.number, t, dist, gMe.pos[1] - P.spot[1]); P.phase = 0; StopPath(); return; }
    if (dist < 200.f) { if (!P.there) { P.there = true; P.at = gFrame - 1; Log("probe48: on the box (%.0f mm), its hips %.0f over it", dist, gMe.pos[1] - P.spot[1]); } Steer(0, 0, 0); return; }
    if (dist < 1200.f) { StopPath(); Steer(P.spot[0] - gMe.pos[0], P.spot[2] - gMe.pos[2], 0.5f); return; }
    if (!Following() || t % 120 == 0) PlanTo(P.spot);
    FollowPath();
}

// ---- (v150-host beta 22, support) probe 49: the claymore taken by a real client (docs/VIRTUAL_DOLL.md 35). MGO1_DOLL_PROBE_ROLE:
// layer: its support item to its hands (probe 18's window), then crouched and Square tapped, a claymore laid; 6 m away; again until
// its claymores are spent (the weapon row's rounds and most logged each time); then (MGO1_DOLL_PROBE_WALK=1) the walker below.
// taker: to the nearest claymore lying not its own (MGO1_DOLL_PROBE_OWN=1: its own too) on its own game's list, to 3 m of it,
// crouched (Cross), lying (Cross), then crawling onto it (the stick toward it) until its list says it is gone; each 10 frames its
// distance, its state words, its claymores' row; the result. walker: to the nearest claymore not its own, walked onto standing
// (its own game sets it off): its life before and after ----
struct Probe49 { int role = 0, phase = 0, laid = 0, takes = 0; long long at = 0; uint32_t id = 0; float c[3] = {0, 0, 0}, from[3] = {0, 0, 0}; int roundsWas = 0, lifeWas = 0; bool rowsSaid = false; };
Probe49 gP49;
struct Clay49 { uint32_t id; int state; float p[3]; };
// its own game's claymores' list (NewOnlineClaymoreMan: 16 entries of 0xA0 bytes, +0x154 the id, +0x158 the state, +0x100 the place)
int Clays49(std::vector<Clay49>& out) {
    out.clear();
    const uint32_t m = ps2world::ActorByName("NewOnlineClaymoreMan");
    if (m < 0x100000 || m + 0xAC0 > 0x02000000) return 0;
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t e = m + 0xA0 * i;
        if (R32(e + 0x158) != 1) continue;
        Clay49 c; c.id = R32(e + 0x154); c.state = 1;
        for (int k = 0; k < 3; ++k) c.p[k] = RF(e + 0x100 + 4 * k);
        if (std::fabs(c.p[0]) < 1e6f && std::fabs(c.p[2]) < 1e6f) out.push_back(c);
    }
    return (int)out.size();
}
void Row49(int* rounds, int* most, int* mag, int code = 0x1B) {
    const uint32_t st = WeaponState(code);
    *rounds = st ? R16s(st + 0x18) : -99; *most = st ? R16s(st + 0x1A) : -99; *mag = st ? R16s(st + 0x1C) : -99;
}
bool InList49() { const uint32_t eq = EquipBase(); if (!eq) return false; const int n = (int)R32(eq + 0x5AC); for (int i = 0; i < n && i < 16; ++i) if (R16s(eq + 0x5B4 + 2 * i) == 0x1B) return true; return false; }
void Probe49Frame() {
    Probe49& P = gP49;
    if (!P.role) {
        const char* r = getenv("MGO1_DOLL_PROBE_ROLE");
        P.role = r && !strcmp(r, "taker") ? 2 : r && !strcmp(r, "walker") ? 3 : 1;
        int ro, mo, ma; Row49(&ro, &mo, &ma);
        Log("probe49: the %s (its claymores' row: %d rounds, %d at most, magazine %d; in its weapons' list: %s)", P.role == 1 ? "layer" : P.role == 2 ? "taker" : "walker", ro, mo, ma, InList49() ? "yes" : "no");
    }
    if (gRoom.phase != 2 || gFrame < 4800 || !gMe.alive) { P.at = gFrame; if (!gMe.alive) P.rowsSaid = false; if (!gMe.alive && P.phase > 1 && P.role == 1) P.phase = 0; return; }
    const long long t = gFrame - P.at;
    if (!P.rowsSaid) {                                   // (each life: what its support row gave, the claymores' and the books' rows)
        P.rowsSaid = true;
        int r1, m1, g1, r2, m2, g2; Row49(&r1, &m1, &g1, 0x1B); Row49(&r2, &m2, &g2, 0x1C);
        Log("probe49: this life's rows: the claymores %d rounds (%d at most), the books %d (%d at most); its support item %d", r1, m1, r2, m2, SupportNow());
    }
    std::vector<Clay49> l;
    Clays49(l);
    int ro, mo, ma; Row49(&ro, &mo, &ma);
    if (P.role == 1) {                                   // the layer
        if (P.phase == 0) {                              // its support to its hands (probe 18's window: phase 2 = taken)
            Probe18Frame();
            if (gP18.phase == 2) { gP18.phase = 9; P.phase = 1; P.at = gFrame; Log("probe49: the support %x in its hands (%d rounds, %d at most)", gP18.code, ro, mo); }
            else if (gP18.phase == 9) { P.phase = 7; Log("probe49: no support item in the window"); }
            return;
        }
        if (P.phase == 1) {                              // crouched, then Square tapped
            Steer(0, 0, 0);
            if (t < 40) { if (!Low() && t % 30 == 0) gPad.buttons |= 0x4000; return; }
            if (t == 41) { P.roundsWas = ro; Log("probe49: lays its claymore %d (me %.0f %.0f %.0f facing %.0f, state %x, rounds %d)", P.laid + 1, gMe.pos[0], gMe.pos[1], gMe.pos[2], Facing() * 57.3f, gMe.bits[0], ro); }
            if (t >= 42 && t < 48) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }
            if (t == 170) {                              // (the claymore made about 110 frames after the tap; armed 108 frames later: away first)
                ++P.laid;
                std::string s; char b[96];
                for (const Clay49& c : l) { snprintf(b, sizeof b, " %#x at %.0f %.0f %.0f", c.id, c.p[0], c.p[1], c.p[2]); s += b; }
                Log("probe49: RESULT laid %d: rounds %d -> %d (at most %d); its list:%s", P.laid, P.roundsWas, ro, mo, s.c_str());
                memcpy(P.from, gMe.pos, sizeof P.from);
                P.phase = 2; P.at = gFrame;
            }
            return;
        }
        if (P.phase == 2) {                              // away at once (out of its sensor before it is armed), up, then 6 m away
            if (t == 0) P.c[0] = Facing() + 3.14159265f * 0.75f;   // (the way away: behind it and to its side, fixed: the next one 6 m aside)
            const float away = P.c[0];
            if (t >= 70 && t < 110) { Steer(0, 0, 0); if (Low() && (t == 72 || t == 100)) gPad.buttons |= 0x4000; return; }   // (still a moment: up)
            if (Dist2D(P.from, gMe.pos) < 6000.f && t < 500) { Steer(std::sin(away), std::cos(away), t < 70 ? 1.f : 0.6f); return; }
            Steer(0, 0, 0);
            if (ro > 0 && P.laid < 4) { P.phase = 1; P.at = gFrame; return; }
            static const bool walk = getenv("MGO1_DOLL_PROBE_WALK") != nullptr;
            Log("probe49: its claymores laid: %d (rounds left %d)%s", P.laid, ro, walk ? "; now the walker" : "");
            P.phase = walk ? 8 : 7; P.at = gFrame;
            if (walk) { P.role = 3; P.phase = 0; }
            return;
        }
        Steer(0, 0, 0);
        return;
    }
    int me = -1;                                         // (its own slot: the local player's id)
    for (const Player& p : gPlayers) if (p.local) me = p.id;
    if (P.phase == 0) {                                  // the nearest claymore lying (not its own unless asked)
        Steer(0, 0, 0);
        static const bool own = getenv("MGO1_DOLL_PROBE_OWN") != nullptr;
        // (the walker waits 20 s after a claymore appears: the layer's crawl or the doll's laying done, the claymore armed)
        const Clay49* best = nullptr; float bd = 1e30f;
        for (const Clay49& c : l) { if (!own && (int)(c.id >> 10) == me) continue; const float d = Dist2D(c.p, gMe.pos); if (d < bd) { bd = d; best = &c; } }
        if (!best) { if (t % 600 == 0) Log("probe49: no claymore lying (its list %zu)", l.size()); return; }
        if (P.role == 3 && t < 1200) return;
        P.id = best->id; memcpy(P.c, best->p, sizeof P.c); P.phase = 1; P.at = gFrame; P.roundsWas = ro; P.lifeWas = gMe.life;
        // (benches: MGO1_DOLL_PROBE_LIFE=<n>, the walker's own life lowered to n first, so that one claymore (100) kills it: its death
        // and its credit seen in a bench, as the host's MGO1_VDOLL_TEST_TRAP_LIFE does for a virtual doll)
        static const int lifeTo = getenv("MGO1_DOLL_PROBE_LIFE") ? atoi(getenv("MGO1_DOLL_PROBE_LIFE")) : 0;
        if (P.role == 3 && lifeTo > 0 && gMe.obj && gMe.life > lifeTo) { W16(gMe.obj + 0xB64, (uint16_t)lifeTo); Log("probe49: its life %d set to %d (MGO1_DOLL_PROBE_LIFE)", gMe.life, lifeTo); P.lifeWas = lifeTo; }
        Log("probe49: the claymore %#x of player %d at %.0f %.0f %.0f, %.1f m away: to it (%s); its row %d rounds, %d at most, in its list %s", P.id, (int)(P.id >> 10) + 1, P.c[0], P.c[1], P.c[2],
            bd / 1000.f, P.role == 2 ? "to take it crawling" : "to walk onto it", ro, mo, InList49() ? "yes" : "no");
        return;
    }
    bool lies = false;
    for (const Clay49& c : l) if (c.id == P.id) lies = true;
    const float d = Dist2D(P.c, gMe.pos), dy = gMe.pos[1] - P.c[1];
    if (P.phase == 1) {                                  // to 3 m of it (the walker: onto it)
        const float stop = P.role == 2 ? 3000.f : 0.f;
        if (t % 30 == 0) Log("probe49: %.2f m from the claymore (height %.0f), state %x, life %d", d / 1000.f, dy, gMe.bits[0], gMe.life);
        if (!lies) {
            Log("probe49: RESULT the claymore %#x gone while it came (%.2f m away; life %d -> %d)", P.id, d / 1000.f, P.lifeWas, gMe.life);
            StopPath(); Steer(0, 0, 0); P.phase = 4; P.at = gFrame; return;
        }
        if ((P.role == 2 && d < stop) || t > 3600) { StopPath(); Steer(0, 0, 0); P.phase = 2; P.at = gFrame; Log("probe49: at %.2f m: down (state %x)", d / 1000.f, gMe.bits[0]); return; }
        if (P.role == 3 && d < 1500.f) { StopPath(); const float a = AngleTo(gMe.pos, P.c); Steer(std::sin(a), std::cos(a), 0.6f); return; }   // (its last steps straight onto it)
        if (!Following() || t % 120 == 0) PlanTo(P.c);
        FollowPath();
        return;
    }
    if (P.phase == 2) {                                  // crouched (Cross once: a second press stands it up again); the stick then lays it down
        Steer(0, 0, 0);
        if (!lies) { Log("probe49: RESULT the claymore %#x gone before it lay", P.id); P.phase = 4; P.at = gFrame; return; }
        if (Low() && t > 20) { P.phase = 3; P.at = gFrame; Log("probe49: crouched at %.2f m (state %x): the stick toward it", d / 1000.f, gMe.bits[0]); return; }
        if (t == 2 && !Low()) gPad.buttons |= 0x4000;
        if (t > 120 && !Low() && t % 60 == 2) gPad.buttons |= 0x4000;
        if (t > 400) { Log("probe49: not crouched after 400 frames (state %x)", gMe.bits[0]); P.phase = 4; P.at = gFrame; }
        return;
    }
    if (P.phase == 3) {                                  // crawling onto it (crouched, the stick: down on its front, then the crawl)
        static long long proneAt = -1;
        if (t == 0) proneAt = -1;
        if (Prone() && proneAt < 0) { proneAt = gFrame; Log("probe49: lying at %.2f m, %lld frames after the stick (state %x)", d / 1000.f, t, gMe.bits[0]); }
        if (t % 10 == 0) {
            const uint32_t eq = EquipBase();
            Log("probe49: crawling +%lld: %.0f mm from it on the ground, %.0f in height, state %x %x, its row %d rounds (%d at most), in its list %s, its held %d, life %d", t, d, dy,
                gMe.bits[0], gMe.bits[2], ro, mo, InList49() ? "yes" : "no", eq ? (int)R16s(eq + 0x5D4) : -1, gMe.life);
        }
        if (!lies) {
            ++P.takes;
            Log("probe49: RESULT take %d: the claymore %#x of player %d gone from its list at %.0f mm from it on the ground (%.0f in height), state %x; its row %d -> %d rounds (%d at most), in its list %s; life %d -> %d",
                P.takes, P.id, (int)(P.id >> 10) + 1, d, dy, gMe.bits[0], P.roundsWas, ro, mo, InList49() ? "yes" : "no", P.lifeWas, gMe.life);
            Steer(0, 0, 0); P.phase = 4; P.at = gFrame; return;
        }
        if (t > 900) { Log("probe49: RESULT not taken in 900 frames (%.0f mm, state %x)", d, gMe.bits[0]); Steer(0, 0, 0); P.phase = 4; P.at = gFrame; return; }
        const float a = AngleTo(gMe.pos, P.c);
        Steer(std::sin(a), std::cos(a), 1.f);
        return;
    }
    // up again (Cross twice), 10 s, then the next claymore
    Steer(0, 0, 0);
    if (Low() && t % 45 == 2 && t < 300) gPad.buttons |= 0x4000;
    if (t > 600) { P.phase = 0; P.at = gFrame; }
}
// ---- (v150-host beta 24, stamina) MGO1_DOLL_PROBE_STAMINA, with any probe (benches only): this player's own stun gauge (+0xB68 of
// +0xB6A, US n003a 0x4143F0 its only writer in play) logged every second while it is below its maximum and at each change while
// it is not knocked out, with its stance, its reading (+0xDE9), the flag 0x99 and its life. A number over 0: once a life, up 10 s
// with its gauge full, its pending stun (+0xB72, where a damage record's stun goes, 0x4367A0) set to it, so that its own game
// takes it off at its next tick as a hit's. MGO1_DOLL_PROBE_STAMINA_POSE=1: then 15 s each standing still, crouched, lying, and
// standing again (the pad its own meanwhile), the gauge watched in each ----
struct StamProbe { long long life = -1, upSince = -1, stunAt = -1, changedAt = -1, nextLog = 0, poseAt = -1, crossAt = -1000; int last = -1, pose = -1; bool stunned = false; };
StamProbe gStam;
const char* StanceName() {
    const uint32_t st = gMe.bits[0];
    if (st & 0x1A000000) return "down";
    return (st & 8) ? "lying" : (st & 4) ? "crouched" : (st & 0xFFFF) == 3 ? "standing still" : "standing";
}
void StaminaWatch() {
    static const char* e = getenv("MGO1_DOLL_PROBE_STAMINA");
    if (!e || !gMe.obj) return;
    StamProbe& S = gStam;
    if (!gMe.alive) { S.life = -1; return; }
    if (S.life < 0 || gFrame - S.life > 30) { S.upSince = -1; S.stunned = false; S.pose = -1; S.last = -1; Log("probe stamina: a life (gauge %d of %d)", gMe.stun, (int)R16s(gMe.obj + 0xB6A)); }
    S.life = gFrame;
    const int g = gMe.stun, most = R16s(gMe.obj + 0xB6A), out = (int)((gMe.bits[4] >> 25) & 1), de9 = (int)R8(gMe.obj + 0xDE9);
    if (S.last >= 0 && g != S.last && !out) {
        Log("probe stamina: %d -> %d (%+d) %s, reading %d, life %d, %.1f s after its last change (state %x)", S.last, g, g - S.last, StanceName(), de9, gMe.life,
            S.changedAt >= 0 ? (gFrame - S.changedAt) / 59.94 : -1.0, gMe.bits[0]);
        S.changedAt = gFrame;
    } else if (S.last >= 0 && g != S.last && (S.last == most || g == most)) S.changedAt = gFrame;
    if (S.last < 0) S.changedAt = gFrame;
    S.last = g;
    if (g < most && gFrame >= S.nextLog) {
        S.nextLog = gFrame + 60;
        Log("probe stamina: %d of %d, %s, knocked out %d, reading %d, life %d, %.1f s since its last change (state %x)", g, most, StanceName(), out, de9, gMe.life,
            (gFrame - S.changedAt) / 59.94, gMe.bits[0]);
    }
    const int amount = atoi(e);
    if (amount <= 0 || S.stunned || gRoom.phase != 2 || gFrame < 4800) return;
    if (out || (gMe.bits[0] & 0x1A000000) || g < most || R16s(gMe.obj + 0xB72) != 0) { S.upSince = -1; return; }
    if (S.upSince < 0) S.upSince = gFrame;
    if (gFrame - S.upSince < 600) return;
    const int16_t v = (int16_t)std::min(amount, 32767);
    memcpy(ps2::gMem + ((gMe.obj + 0xB72) & 0x01FFFFFF), &v, 2);
    S.stunned = true; S.stunAt = gFrame; S.poseAt = gFrame + 30; S.pose = 0;
    Log("probe stamina: its pending stun +0xB72 set to %d, its gauge %d of %d, %s (its own game takes it off at its next tick, as a damage record's stun)", (int)v, g, most, StanceName());
}
// the stance cycle after the self-stun (MGO1_DOLL_PROBE_STAMINA_POSE=1): true while it holds the pad
bool StaminaPose() {
    static const bool on = getenv("MGO1_DOLL_PROBE_STAMINA_POSE") && *getenv("MGO1_DOLL_PROBE_STAMINA_POSE") == '1';
    StamProbe& S = gStam;
    if (!on || S.pose < 0 || S.pose > 3 || gFrame < S.poseAt) return false;
    if ((gMe.bits[4] >> 25) & 1) return false;           // (knocked out: nothing pressed)
    const long long t = gFrame - S.poseAt;
    const int want = (int)(t / 900);
    if (want > 3) { S.pose = 4; Log("probe stamina: the stance cycle over (%s)", StanceName()); return false; }
    if (want != S.pose || t == 0) { S.pose = want; Log("probe stamina: now %s, 15 s (state %x)", want == 0 ? "standing still" : want == 1 ? "crouched" : want == 2 ? "lying" : "standing again", gMe.bits[0]); }
    gPad.buttons = 0;
    Steer(0, 0, 0);
    const uint32_t st = gMe.bits[0] & 0xFFFF;
    const bool crouched = (st & 4) != 0, lying = (st & 8) != 0;
    if (want == 0 || (want == 3 && !crouched && !lying)) return true;
    if (want == 3 || (want == 1 && lying)) {             // (Cross: up a step, not twice within 45 frames)
        if (gFrame - S.crossAt > 45) S.crossAt = gFrame;
        if (gFrame - S.crossAt < 3) gPad.buttons |= 0x4000;
        return true;
    }
    if (!crouched && !lying) {                            // (Cross: down to a crouch)
        if (gFrame - S.crossAt > 45) S.crossAt = gFrame;
        if (gFrame - S.crossAt < 3) gPad.buttons |= 0x4000;
        return true;
    }
    if (want == 2 && crouched && !lying && gFrame - S.crossAt > 30) gPad.sticks[3] = 0x00;   // (crouched, the stick: he lies down; probe 45's way)
    return true;
}
// ---- (v150-host beta 25, aimcost) probe 50: a player's moves while he aims, against the same moves without (docs/VIRTUAL_DOLL.md 45).
// Phases of MGO1_DOLL_PROBE_MOVES (letters, default "rRwhcCvs"), each 5 s to take its stance then MGO1_DOLL_PROBE_PHASE seconds (30)
// measured: r probe 7's ways, the stick as it steers (a run); R the same with Square light (readied); w probe 7's ways, the stick at
// 0.64 of its push; h the same readied; c crouched (Cross), then probe 7's ways; C crouched and readied, then the ways; v the first
// person view (R1) and the stick full ahead; s a scoped rifle's scope (R2 twice) and the stick full ahead (a scoped rifle in its
// hands only). Logged each second: its own gauge, its state word, the posture code its game gives the engine for it (0x4143F0's
// rule from its state bits), the metres of that second; each drain of its gauge (a fall of under 300 with no damage) with the code
// and the engine's counts (the 0x888 entry at 0x564308 whose player is its own: +0x424 moving, +0x428 still); at each phase's end the
// drains by size, the frames by code and by state word, the metres ----
struct P50 { int phase = -1; long long at = -1, life = -1, crossAt = -1000, r2At = -1000; int last = -1, drains = 0, spent = 0, settle = 0;
             float pos[3] = {0, 0, 0}, sec = 0.f, metres = 0.f; bool have = false; std::map<int, int> sizes, codes; std::map<uint32_t, int> states; };
P50 gP50;
// the posture code a player's tick gives the engine (US n003a 0x414420 to 0x414838, the branch delay slots read): 1 the flag 0xB or
// 0xAA, 2 0x10 or 0x82, 5 0x5C, 7 0x6A, 8 0x4A or 9, 9 0x28, else 0; moving (0xE): 6 with 0xA9, else 4 with 3, 3 with 0xC or 0xD,
// 7 with 0x6B, 6 with 0x52, else the first one
int PostureCode(const uint32_t* w) {
    auto f = [&](int b) { return (int)((w[b >> 5] >> (b & 31)) & 1); };
    int c = f(0xB) || f(0xAA) ? 1 : f(0x10) || f(0x82) ? 2 : f(0x5C) ? 5 : f(0x6A) ? 7 : f(0x4A) || f(9) ? 8 : f(0x28) ? 9 : 0;
    if (f(0xE)) c = f(0xA9) ? 6 : f(3) ? 4 : f(0xC) || f(0xD) ? 3 : f(0x6B) ? 7 : f(0x52) ? 6 : c;
    return c;
}
uint32_t StaminaEntry() {                                 // (the engine's entry of this player: [entry] + 4 is his +0x410)
    if (!gMe.obj) return 0;
    const uint32_t me = R32(gMe.obj + 0x410);
    for (uint32_t e = 0x564308; e < 0x564308 + 4 * 0x888; e += 0x888) { const uint32_t p = R32(e); if (p && R32(p + 4) == me) return e; }
    return 0;
}
void Probe50Frame() {
    P50& P = gP50;
    static std::string moves;
    static int secs = 30;
    if (moves.empty()) {
        const char* e = getenv("MGO1_DOLL_PROBE_MOVES");
        moves = e && *e ? e : "rRwhcCvs";
        if (const char* s = getenv("MGO1_DOLL_PROBE_PHASE")) secs = std::max(5, atoi(s));
        Log("probe50: the moves %s, %d s each (5 s to take its stance first)", moves.c_str(), secs);
    }
    gPad.buttons = 0;
    if (gRoom.phase != 2 || gFrame < 4800) { Steer(0, 0, 0); P.at = -1; return; }
    if (P.phase >= (int)moves.size()) { Steer(0, 0, 0); if (gFrame % 600 == 0) Log("probe50: done"); return; }
    if (P.at < 0) { P.phase = std::max(0, P.phase); P.at = gFrame; P.settle = 1; P.have = false; Log("probe50: phase %c (%d of %d), its stance first", moves[P.phase], P.phase + 1, (int)moves.size()); }
    const char m = moves[P.phase];
    const long long t = gFrame - P.at;
    const uint32_t st = gMe.bits[0] & 0xFFFF;
    const bool crouched = (st & 4) != 0, lying = (st & 8) != 0;
    const bool wantLow = m == 'c' || m == 'C';
    // its stance: standing for every phase but c and C (Cross, not twice within 45 frames), crouched for those
    const bool stanceOk = wantLow ? (crouched || lying) : !crouched && !lying;
    if (!stanceOk && gFrame - P.crossAt > 45) P.crossAt = gFrame;
    if (!stanceOk && gFrame - P.crossAt < 3) gPad.buttons |= 0x4000;
    const bool scoped = Inv().weapon == 0x0F || Inv().weapon == 0x10;
    if (m == 's' && !scoped) { Log("probe50: phase s: no scoped rifle in its hands (weapon %x): passed", Inv().weapon); P.phase++; P.at = -1; return; }
    if (m == 'v') { gPad.buttons |= 0x0800 | 0x8000; gPad.pressures[11] = 0x3F; Steer(0, 0, 0); if (t > 20) gPad.sticks[3] = 0x00; }
    else if (m == 's') {
        Steer(0, 0, 0);
        const bool view = (gMe.bits[0] & 0xFF) == 0x83;
        if (!view && t < 300 && gFrame - P.r2At > 40) P.r2At = gFrame;              // (R2 twice, 14 frames apart: the scope's view)
        if (!view && (gFrame - P.r2At < 3 || (gFrame - P.r2At >= 14 && gFrame - P.r2At < 17))) gPad.buttons |= 0x0200;
        if (view) gPad.sticks[3] = 0x00;
    } else {
        Probe7Frame();
        if (m == 'R' || m == 'h' || m == 'C') { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
        if (m == 'w' || m == 'h')                                                     // (the stick at 0.64 of the push asked)
            for (int k = 2; k <= 3; ++k) gPad.sticks[k] = (uint8_t)(0x80 + (int)lrintf(((int)gPad.sticks[k] - 0x80) * 0.64f));
    }
    if (P.settle) {                                       // (5 s for its stance, then the count begins)
        if (t >= 300) { P.settle = 0; P.at = gFrame; P.last = gMe.stun; P.drains = P.spent = 0; P.sizes.clear(); P.codes.clear(); P.states.clear(); P.metres = P.sec = 0.f;
                        memcpy(P.pos, gMe.pos, sizeof P.pos); P.have = true; Log("probe50: phase %c begins, its gauge %d, state %x", m, gMe.stun, gMe.bits[0]); }
        return;
    }
    const int code = PostureCode(gMe.bits);
    ++P.codes[code]; ++P.states[gMe.bits[0]];
    if (P.have) { const float d = Dist2D(gMe.pos, P.pos); if (d < 1000.f) { P.metres += d; P.sec += d; } }
    memcpy(P.pos, gMe.pos, sizeof P.pos); P.have = true;
    const uint32_t e = StaminaEntry();
    if (P.last >= 0 && gMe.stun < P.last && P.last - gMe.stun < 300) {
        const int c = P.last - gMe.stun;
        ++P.drains; P.spent += c; ++P.sizes[c];
        Log("probe50: %c: a drain of %d (%d -> %d), code %d, state %x %x, the engine's counts %d moving %d still", m, c, P.last, gMe.stun, code, gMe.bits[0], gMe.bits[1],
            e ? (int)R32(e + 0x424) : -1, e ? (int)R32(e + 0x428) : -1);
    } else if (P.last >= 0 && gMe.stun != P.last) Log("probe50: %c: its gauge %d -> %d (not a move's drain)", m, P.last, gMe.stun);
    P.last = gMe.stun;
    if (t % 60 == 59) {
        Log("probe50: %c t %lld s: gauge %d, state %x %x, code %d, %.2f m this second, the engine's counts %d %d", m, (t + 1) / 60, gMe.stun, gMe.bits[0], gMe.bits[1], code,
            P.sec / 1000.f, e ? (int)R32(e + 0x424) : -1, e ? (int)R32(e + 0x428) : -1);
        P.sec = 0.f;
    }
    if (t + 1 >= secs * 60) {
        std::string sz, cd, sts;
        for (const auto& kv : P.sizes) sz += " " + std::to_string(kv.first) + " x" + std::to_string(kv.second);
        for (const auto& kv : P.codes) cd += " " + std::to_string(kv.first) + ":" + std::to_string(kv.second);
        std::vector<std::pair<int, uint32_t>> top;
        for (const auto& kv : P.states) top.push_back({kv.second, kv.first});
        std::sort(top.rbegin(), top.rend());
        for (size_t i = 0; i < top.size() && i < 4; ++i) { char b[32]; snprintf(b, sizeof b, " %x:%d", top[i].second, top[i].first); sts += b; }
        Log("probe50: RESULT phase %c: %lld frames, %.1f m (%.2f m a second), %d drains costing %d (sizes%s), frames by code%s, by state%s; the engine's costs %d %d %d %d %d %d",
            m, t + 1, P.metres / 1000.f, P.metres / 1000.f / ((t + 1) / 59.94f), P.drains, P.spent, sz.c_str(), cd.c_str(), sts.c_str(),
            e ? R16s(e + 0x404) : -1, e ? R16s(e + 0x406) : -1, e ? R16s(e + 0x408) : -1, e ? R16s(e + 0x40A) : -1, e ? R16s(e + 0x40C) : -1, e ? R16s(e + 0x40E) : -1);
        ++P.phase; P.at = -1;
    }
}
// ---- (v150-host beta 25, rolltime) probe 51: a player's roll (docs/VIRTUAL_DOLL.md 48). Rolls of MGO1_DOLL_PROBE_ROLLS (letters, one a
// roll; MGO1_DOLL_PROBE_PHASE seconds to find the place of one, 60), each from a run along probe 7's ways: n Cross on a flat clear way (4 m
// clear at the knees and the chest, no step over 15 cm), the stick let go once the roll began; m the same, the stick held on (its first run
// after it); s Square tapped (light 4, full 4, let go 4) from the roll's start (its first round); S Square light 6 then full, held (an
// automatic's first round); c the stick held on and Cross tapped every 6 frames from the roll's start (its next roll); h Cross held (the
// roll into a crouch); a its gun readied standing still (Square light 60 frames), then Square let go, the stick 90 degrees aside and Cross
// tapped every 4 frames (from an aim to a roll); A the same, Square kept light; w a wall 1.0 to 2.2 m ahead (the knees and the chest);
// l a drop of 1 m or more 1.0 to 2.6 m ahead; p up a slope (0.3 to 1.5 m in 3 m); q down one. Logged: each tick of a roll from the press
// (its state words, its place along and across the line pressed on, its floor), a RESULT line a roll. Whatever it does, each other
// player's copy whose state has the roll's bit 9: its first rolls tick by tick, a COPY line a roll ----
struct Look51 { float floor = -1e30f, knee = 1e9f, chest = 1e9f, g[17]; };
Look51 LookAhead51(float yaw) {
    Look51 a;
    const float s = std::sin(yaw), c = std::cos(yaw);
    a.floor = gMap->nav->GroundBelow(gMe.pos, 2500.f);
    for (int i = 0; i <= 16; ++i) { const float d = 250.f * i; const float p[3] = {gMe.pos[0] + s * d, a.floor + 700.f, gMe.pos[2] + c * d}; a.g[i] = gMap->nav->GroundBelow(p, 3500.f); }
    for (int k = 0; k < 2; ++k) {
        const float h = k ? 1100.f : 450.f;
        const float p0[3] = {gMe.pos[0], a.floor + h, gMe.pos[2]}, p1[3] = {p0[0] + s * 4000.f, p0[1], p0[2] + c * 4000.f};
        float t = 1.f, n[3];
        if (gMap->nav->FirstHit(p0, p1, &t, n)) (k ? a.chest : a.knee) = t * 4000.f;
    }
    return a;
}
// the place for a roll of that kind ahead (yaw): "" none, else what it is
std::string Fits51(char k, const Look51& a) {
    char b[96];
    if (a.floor < -1e29f) return "";
    auto flatTo = [&](float d) { for (int i = 1; i <= 16 && 250.f * i <= d; ++i) if (a.g[i] < -1e29f || std::fabs(a.g[i] - a.floor) > 150.f) return false; return true; };
    if (strchr("nmsSchaA", k)) { if (a.knee >= 4000.f && a.chest >= 4000.f && flatTo(4000.f)) return "flat, clear 4 m"; return ""; }
    if (k == 'w') {
        if (a.knee >= 1000.f && a.knee <= 2200.f && a.chest >= 1000.f && a.chest <= 2400.f && flatTo(a.knee - 250.f)) { snprintf(b, sizeof b, "a wall at %.2f m (the chest %.2f)", a.knee / 1000.f, a.chest / 1000.f); return b; }
        return "";
    }
    if (k == 'l') {
        int i = 1;
        while (i <= 16 && a.g[i] > -1e29f && std::fabs(a.g[i] - a.floor) <= 150.f) ++i;
        if (i > 16 || 250.f * i < 1000.f || 250.f * i > 2600.f) return "";
        if (a.g[i] > -1e29f && a.g[i] > a.floor - 1000.f) return "";
        if (a.knee < 250.f * i + 1000.f || a.chest < 250.f * i + 1000.f) return "";
        snprintf(b, sizeof b, "a drop of %s at %.2f m", a.g[i] < -1e29f ? "more than 2.8 m" : (std::to_string((int)(a.floor - a.g[i])) + " mm").c_str(), 0.25f * i);
        return b;
    }
    if (k == 'p' || k == 'q') {
        if (a.knee < 4000.f || a.chest < 4000.f) return "";
        const float sign = k == 'p' ? 1.f : -1.f;
        for (int i = 1; i <= 12; ++i) { if (a.g[i] < -1e29f) return ""; const float st = sign * (a.g[i] - a.g[i - 1 > 0 ? i - 1 : 0]) ; if (i > 1 && (st < -50.f || st > 250.f)) return ""; }
        const float rise = sign * (a.g[12] - a.floor);
        if (rise < 300.f || rise > 1500.f) return "";
        snprintf(b, sizeof b, "%s %.2f m in 3 m", k == 'p' ? "up" : "down", rise / 1000.f);
        return b;
    }
    return "";
}
// a player's motion now (layer 0: its id at +0x60, the low 10 bits; its time, 1/300 s, 10 a tick, at [[+0x204] + 4] + 0x38: US n003a
// 0x318CB8, 0x318E20); its ticks counted by that pair's changes, whatever the frames (a slowed instance's VBlanks are not its ticks)
struct Mot51 { int id = -1; float time = -1.f; };
Mot51 MotionOf(uint32_t obj) {
    Mot51 m;
    if (!obj) return m;
    m.id = R16(obj + 0x60) & 0x3FF;
    const uint32_t mt = R32(obj + 0x204);
    if (mt) { const uint32_t ly = R32(mt + 4); if (ly) m.time = RF(ly + 0x38); }
    return m;
}
struct Copy51 { long long at = -1, endAt = -1, b10 = -1; float p0[3] = {0, 0, 0}, last[3] = {0, 0, 0}, yaw = 0.f, path = 0.f, maxStep = 0.f, along10 = 0.f, alongEnd = 0.f;
                uint32_t st = 0xFFFFFFFF; std::string states; int n = 0; long long stAt = 0; int ticks = 0, tickEnd = -1, tick10 = -1, stTick = 0; Mot51 mot; std::string mots;
                int jumps = 0; float jumpMax = 0.f; };   // (its place's jumps of a metre or more in one frame: the game pulling it to its records)
struct P51 { int idx = 0, stage = 0, n = 0, runFrames = 0, roundsWas = -1, handWas = -2; long long at = -1, pressAt = -1, rollAt = -1, b10At = -1, endAt = -1, firstRun = -1, firstShot = -1,
             nextRoll = -1, crouchAt = -1, lookAt = -1, readyAt = -1, lastTick = -1; float o[3] = {0, 0, 0}, last[3] = {0, 0, 0}, yaw = 0.f, maxStep = 0.f, path = 0.f, along10 = 0.f,
             alongEnd = 0.f, alongRun = 0.f, upEnd = 0.f, floor0 = 0.f; uint32_t st = 0xFFFFFFFF; std::string states, where, mots; long long stAt = 0; std::map<int, Copy51> copies;
             int ticks = 0, stTick = 0, tRoll = -1, t10 = -1, tEnd = -1, tRun = -1, tShot = -1, tNext = -1, tCrouch = -1; Mot51 mot; };
P51 gP51;
void Along51(const float* o, float yaw, const float* p, float* along, float* across) {
    const float dx = p[0] - o[0], dz = p[2] - o[2];
    *along = dx * std::sin(yaw) + dz * std::cos(yaw);
    *across = dx * std::cos(yaw) - dz * std::sin(yaw);
}
// the other players' copies (every frame of the map, dead or alive): a copy whose state word 0 has 0x200 (the roll's bit 9)
void Copies51() {
    P51& P = gP51;
    for (const Player& p : gPlayers) {
        if (p.local || p.id < 0) continue;
        Copy51& C = P.copies[p.id];
        const bool roll = (p.bits[0] & 0x200) != 0;
        if (C.at < 0 && !roll) { memcpy(C.last, p.pos, sizeof C.last); continue; }
        if (C.at < 0) { const int n = C.n; C = Copy51(); C.n = n; C.at = C.stAt = gFrame; memcpy(C.p0, p.pos, sizeof C.p0); memcpy(C.last, p.pos, sizeof C.last); C.yaw = p.facing; C.mot = MotionOf(p.obj); }
        const long long t = gFrame - C.at;
        float al, ac; Along51(C.p0, C.yaw, p.pos, &al, &ac);
        const float step = Dist2D(p.pos, C.last);
        if (step < 2000.f) { C.path += step; C.maxStep = std::max(C.maxStep, step); }
        if (step >= 1000.f && step < 30000.f) { ++C.jumps; C.jumpMax = std::max(C.jumpMax, step); }
        const Mot51 mo = MotionOf(p.obj);
        const bool tick = mo.id != C.mot.id || mo.time != C.mot.time;
        if (tick && t > 0) ++C.ticks;
        if (mo.id != C.mot.id) { char b[40]; snprintf(b, sizeof b, " %x@%d", mo.id, C.ticks); C.mots += b; }
        C.mot = mo;
        if (p.bits[0] != C.st) {
            if (C.st != 0xFFFFFFFF) { char b[48]; snprintf(b, sizeof b, " %x:%lld/%d", C.st, gFrame - C.stAt, C.ticks - C.stTick); C.states += b; }
            C.st = p.bits[0]; C.stAt = gFrame; C.stTick = C.ticks;
        }
        if (roll && (p.bits[0] & 0x400) && C.b10 < 0) { C.b10 = gFrame; C.tick10 = C.ticks; C.along10 = al; }
        if (!roll && C.endAt < 0) { C.endAt = gFrame; C.tickEnd = C.ticks; C.alongEnd = al; }
        if (C.n < 4 && (step > 0.5f || tick)) Log("probe51 copy P%d roll %d +%lld/%d: state %x %x, along %.0f across %.0f up %.0f, step %.0f, motion %x %.0f", p.id + 1, C.n, t, C.ticks, p.bits[0], p.bits[1], al, ac,
                                                  p.pos[1] - C.p0[1], step, mo.id, mo.time);
        memcpy(C.last, p.pos, sizeof C.last);
        if ((C.endAt >= 0 && gFrame - C.endAt >= 40) || (C.endAt >= 0 && roll) || !p.alive || t > 400) {
            char b[48]; snprintf(b, sizeof b, " %x:%lld/%d", C.st, gFrame - C.stAt, C.ticks - C.stTick); C.states += b;
            Log("probe51: COPY P%d roll %d: the bit 9 %lld frames, %d ticks (the bit 10 from tick %d), %.2f m along its facing by the bit 10, %.2f m by the end of the bit 9, %.2f m 40 frames after, "
                "%.2f m of path, %.0f mm the longest tick, %d jumps of a metre or more (the longest %.0f mm); its states (frames/ticks)%s; its motions%s%s", p.id + 1, C.n,
                (C.endAt >= 0 ? C.endAt : gFrame) - C.at, C.tickEnd >= 0 ? C.tickEnd : C.ticks, C.tick10, C.along10 / 1000.f, C.alongEnd / 1000.f, al / 1000.f, C.path / 1000.f, C.maxStep,
                C.jumps, C.jumpMax, C.states.c_str(), C.mots.c_str(), !p.alive ? " (dead)" : roll ? " (rolls again)" : "");
            const int n = C.n + 1;
            C = Copy51(); C.n = n; memcpy(C.last, p.pos, sizeof C.last);
            if (roll && p.alive) { C.at = C.stAt = gFrame; memcpy(C.p0, p.pos, sizeof C.p0); C.yaw = p.facing; C.mot = mo; }
        }
    }
}
void Probe51Frame() {
    P51& P = gP51;
    static std::string kinds;
    static int secs = 60;
    if (kinds.empty()) {
        const char* e = getenv("MGO1_DOLL_PROBE_ROLLS");
        kinds = e && *e ? e : "nnnnmmmmsssSSScccchhaaaAAAwwwlllpppqqq";
        if (const char* s = getenv("MGO1_DOLL_PROBE_PHASE")) secs = std::max(10, atoi(s));
        Log("probe51: the rolls %s (%d s at most to find each one's place)", kinds.c_str(), secs);
    }
    gPad.buttons = 0;
    if (gRoom.phase != 2) { Steer(0, 0, 0); P.stage = 0; P.at = -1; return; }
    if (P.idx >= (int)kinds.size()) { Probe7Frame(); if (gFrame % 600 == 0) Log("probe51: done"); return; }
    const char k = kinds[P.idx];
    const uint32_t w0 = gMe.bits[0] & 0xFFFF;
    const bool rolling = (gMe.bits[0] & 0x200) != 0;
    const Inventory& inv = Inv();
    const int rounds = inv.ok && inv.weapon > 0 && inv.weapon < 0x40 ? inv.rounds[inv.weapon] : -1;
    auto next = [&](const char* why) {
        if (why) Log("probe51: %c %d: %s", k, P.n, why);
        ++P.idx; ++P.n; P.stage = 0; P.at = -1; P.runFrames = 0;
    };
    if (P.stage == 0) {                                   // finding its place: along probe 7's ways (a, A: standing on a flat spot)
        if (P.at < 0) { P.at = gFrame; Log("probe51: %c %d: finding its place", k, P.n); }
        if (gFrame - P.at > secs * 60LL) { next("no place found"); return; }
        P.runFrames = w0 == 0x5002 ? P.runFrames + 1 : 0;
        if (k == 'a' || k == 'A') {
            if (P.readyAt < 0) {
                Probe7Frame();
                if (P.runFrames >= 20 && gFrame - P.lookAt >= 4) {
                    P.lookAt = gFrame;
                    const float side = Facing() + 1.5707963f;
                    if (!Fits51(k, LookAhead51(side)).empty()) { StopPath(); P.readyAt = gFrame; P.yaw = side; Log("probe51: %c %d: a flat clear way aside, its gun readied", k, P.n); }
                }
                return;
            }
            Steer(0, 0, 0);
            gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F;
            if (gFrame - P.readyAt < 60) return;
            P.where = "from its gun readied, standing";
        } else {
            Probe7Frame();
            if (P.runFrames < 30 || gFrame - P.lookAt < 4) return;
            P.lookAt = gFrame;
            const float yaw = Facing();
            if (gMe.bits[1] & 0x80) return;                // (not while it reloads)
            P.where = Fits51(k, LookAhead51(yaw));
            if (P.where.empty()) return;
            P.yaw = yaw;
        }
        StopPath();
        gP7.tryStep = -1; gP7.stillSince = -1;            // (probe 7's ways out of a freeze left: its step counts frames from its start)
        memcpy(P.o, gMe.pos, sizeof P.o); memcpy(P.last, gMe.pos, sizeof P.last);
        P.floor0 = gMap->nav->GroundBelow(gMe.pos, 2500.f);
        P.pressAt = gFrame; P.rollAt = P.b10At = P.endAt = P.firstRun = P.firstShot = P.nextRoll = P.crouchAt = -1;
        P.maxStep = P.path = P.along10 = P.alongEnd = P.alongRun = P.upEnd = 0.f; P.st = 0xFFFFFFFF; P.states.clear(); P.stAt = gFrame;
        P.roundsWas = rounds; P.handWas = inv.weapon; P.readyAt = -1; P.lastTick = -1;
        P.ticks = P.stTick = 0; P.tRoll = P.t10 = P.tEnd = P.tRun = P.tShot = P.tNext = P.tCrouch = -1; P.mot = MotionOf(gMe.obj);
        { char b[40]; snprintf(b, sizeof b, " %x@0", P.mot.id); P.mots = b; }
        P.stage = 1;
        Log("probe51: %c %d: Cross at %.0f %.0f %.0f, facing %.1f, state %x %x, its rounds %d (%s)", k, P.n, gMe.pos[0], gMe.pos[1], gMe.pos[2], P.yaw * 57.29578f, gMe.bits[0], gMe.bits[1], rounds,
            P.where.c_str());
    }
    if (P.stage != 1) return;
    if (!gMe.alive) { next("dead meanwhile"); return; }
    const long long t = gFrame - P.pressAt;
    const float sx = std::sin(P.yaw), sz = std::cos(P.yaw);
    // the pad: the stick along the line until the roll begins (a, A: aside, Cross tapped), then each kind's own
    if (P.rollAt < 0) {
        Steer(sx, sz, 1.f);
        if (k == 'a' || k == 'A') { if (t % 4 < 2) gPad.buttons |= 0x4000; if (k == 'A') { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; } }
        else if (t < 3 || k == 'h') gPad.buttons |= 0x4000;
        if (t > (k == 'a' || k == 'A' ? 120 : 30)) { next("no roll began"); return; }
    } else {
        const long long r = gFrame - P.rollAt;
        if (k == 'm' || k == 'c') Steer(sx, sz, 1.f); else Steer(0, 0, 0);
        if (k == 'h') gPad.buttons |= 0x4000;
        if (k == 'c' && r >= 2 && r % 6 < 3 && P.nextRoll < 0) gPad.buttons |= 0x4000;
        if (k == 's' && P.firstShot < 0) { const long long q = r % 12; if (q < 4) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; } else if (q < 8) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; } }
        if (k == 'S' && (P.firstShot < 0 || gFrame - P.firstShot < 10)) { gPad.buttons |= 0x8000; gPad.pressures[11] = r < 6 ? 0x3F : 0xFF; }
        if (k == 'A') { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
    }
    // the measures, in frames and in its own ticks (the pair motion and time changing)
    const Mot51 mo = MotionOf(gMe.obj);
    const bool tick = mo.id != P.mot.id || mo.time != P.mot.time;
    if (tick) ++P.ticks;
    if (mo.id != P.mot.id) { char b[40]; snprintf(b, sizeof b, " %x@%d", mo.id, P.ticks); P.mots += b; }
    P.mot = mo;
    if (rolling && P.rollAt < 0) { P.rollAt = gFrame; P.tRoll = P.ticks; }
    if (rolling && (gMe.bits[0] & 0x400) && P.b10At < 0) { P.b10At = gFrame; P.t10 = P.ticks; }
    if (rolling && (gMe.bits[0] & 0x4) && P.crouchAt < 0) { P.crouchAt = gFrame; P.tCrouch = P.ticks; }
    float al, ac; Along51(P.o, P.yaw, gMe.pos, &al, &ac);
    if (P.b10At == gFrame) P.along10 = al;
    if (P.rollAt >= 0 && !rolling && P.endAt < 0) { P.endAt = gFrame; P.tEnd = P.ticks; P.alongEnd = al; P.upEnd = gMap->nav->GroundBelow(gMe.pos, 2500.f) - P.floor0; }
    if (P.endAt >= 0 && rolling && P.nextRoll < 0) { P.nextRoll = gFrame; P.tNext = P.ticks; }
    if (P.rollAt >= 0 && P.firstRun < 0 && w0 == 0x5002 && gFrame > P.rollAt + 4) { P.firstRun = gFrame; P.tRun = P.ticks; P.alongRun = al; }
    if (rounds >= 0 && P.roundsWas >= 0 && inv.weapon == P.handWas && rounds < P.roundsWas && P.firstShot < 0 && P.rollAt >= 0) { P.firstShot = gFrame; P.tShot = P.ticks; }
    P.roundsWas = rounds; P.handWas = inv.weapon;
    if (gMe.bits[0] != P.st) {
        if (P.st != 0xFFFFFFFF) { char b[48]; snprintf(b, sizeof b, " %x:%lld/%d", P.st, gFrame - P.stAt, P.ticks - P.stTick); P.states += b; }
        P.st = gMe.bits[0]; P.stAt = gFrame; P.stTick = P.ticks;
    }
    const float step = Dist2D(gMe.pos, P.last);
    if (step < 2000.f && P.rollAt >= 0 && (P.endAt < 0 || gFrame - P.endAt < 2)) { P.path += step; P.maxStep = std::max(P.maxStep, step); }
    if (step > 0.5f || tick) {
        P.lastTick = gFrame;
        const float fl = gMap->nav->GroundBelow(gMe.pos, 2500.f);
        Log("probe51 %c %d +%lld/%d: state %x %x, along %.0f across %.0f, floor %+.0f, hips %.0f, facing %.1f, step %.0f, motion %x %.0f%s", k, P.n, t, P.ticks, gMe.bits[0], gMe.bits[1], al, ac,
            fl > -1e29f ? fl - P.floor0 : -9999.f, fl > -1e29f ? gMe.pos[1] - fl : -1.f, Facing() * 57.29578f, step, mo.id, mo.time, rounds >= 0 && P.firstShot == gFrame ? ", a round" : "");
    }
    memcpy(P.last, gMe.pos, sizeof P.last);
    const bool over = (P.endAt >= 0 && gFrame - P.endAt >= (k == 'c' ? 120 : 60)) || (k == 'c' && P.nextRoll >= 0 && gFrame - P.nextRoll >= 4) || t > 400;
    if (!over) return;
    char b[48]; snprintf(b, sizeof b, " %x:%lld/%d", P.st, gFrame - P.stAt, P.ticks - P.stTick); P.states += b;
    auto rel = [&](long long f) { return f >= 0 ? f - P.pressAt : -1LL; };
    Log("probe51: RESULT %c %d (%s): Cross at +0, the roll's bit 9 from +%lld to +%lld (%lld frames; the bit 10 from +%lld; a crouch from +%lld), its first run +%lld, its first round +%lld, its next "
        "roll +%lld; along its line %.2f m by the bit 10, %.2f m by the bit 9's end (%.2f m path, %.0f mm the longest tick), %.2f m across, %.2f m at its first run, its floor %+.0f mm at the end; "
        "its states%s", k, P.n, P.where.c_str(), rel(P.rollAt), rel(P.endAt), P.rollAt >= 0 && P.endAt >= 0 ? P.endAt - P.rollAt : -1LL, rel(P.b10At), rel(P.crouchAt), rel(P.firstRun),
        rel(P.firstShot), rel(P.nextRoll), P.along10 / 1000.f, P.alongEnd / 1000.f, P.path / 1000.f, P.maxStep, ac / 1000.f, P.alongRun / 1000.f, P.upEnd, P.states.c_str());
    Log("probe51: TICKS %c %d: from the press, the roll's bit 9 at tick %d to %d (the bit 10 at %d, a crouch at %d), its first run %d, its first round %d, its next roll %d; %d ticks in %lld frames; "
        "its motions (id@tick)%s", k, P.n, P.tRoll, P.tEnd, P.t10, P.tCrouch, P.tRun, P.tShot, P.tNext, P.ticks, t, P.mots.c_str());
    next(nullptr);
    P.stage = 0;
}
// ---- (v150-host beta 25, copyleash) probe 53: laps, a real client's runs while the host's copy of him is logged (docs/VIRTUAL_DOLL.md 53).
// MGO1_DOLL_PROBE_LAPS="<page>:x,y,z/x,y,z/...;<page>:...": on a listed map its way to each floor place in turn, again and again, standing
// MGO1_DOLL_PROBE_LAP_PAUSE frames at each (30); on another map probe 7's random ways. Each place reached logged with its run's frames ----
struct P53 { std::string page; std::vector<std::array<float, 3>> pts; int i = -1; long long from = -1, standUntil = -1; bool going = false; };
P53 gP53;
void Probe53Frame() {
    P53& P = gP53;
    const std::string page = ps2world::CurrentPage();
    if (page != P.page) {
        P = P53(); P.page = page;
        const char* e = getenv("MGO1_DOLL_PROBE_LAPS");
        const std::string s = e ? e : "";
        for (size_t k = 0; k < s.size();) {
            size_t semi = s.find(';', k);
            if (semi == std::string::npos) semi = s.size();
            const std::string part = s.substr(k, semi - k);
            const size_t c = part.find(':');
            if (c != std::string::npos && part.substr(0, c) == page)
                for (size_t j = c + 1; j < part.size();) {
                    size_t sl = part.find('/', j);
                    if (sl == std::string::npos) sl = part.size();
                    std::array<float, 3> q;
                    if (sscanf(part.substr(j, sl - j).c_str(), "%f,%f,%f", &q[0], &q[1], &q[2]) == 3) P.pts.push_back(q);
                    j = sl + 1;
                }
            k = semi + 1;
        }
        Log("probe53: map %s: %s", page.c_str(), P.pts.empty() ? "probe 7's random ways" : (std::to_string(P.pts.size()) + " places in turn").c_str());
    }
    if (P.pts.empty()) { Probe7Frame(); return; }
    static const int pause = getenv("MGO1_DOLL_PROBE_LAP_PAUSE") ? std::max(0, atoi(getenv("MGO1_DOLL_PROBE_LAP_PAUSE"))) : 30;
    if (P.going && !Following()) {
        P.going = false; P.standUntil = gFrame + pause;
        const auto& q = P.pts[(size_t)P.i];
        Log("probe53: place %d (%.0f %.0f %.0f) %s after %lld frames, at %.0f %.0f %.0f", P.i, q[0], q[1], q[2], Dist2D(q.data(), gMe.pos) < 1000.f ? "reached" : "NOT reached",
            gFrame - P.from, gMe.pos[0], gMe.pos[1], gMe.pos[2]);
    }
    if (!P.going && gFrame >= P.standUntil) {
        for (size_t n = 0; n < P.pts.size() && !P.going; ++n) {
            P.i = (P.i + 1) % (int)P.pts.size();
            float to[3] = {P.pts[(size_t)P.i][0], P.pts[(size_t)P.i][1], P.pts[(size_t)P.i][2]};
            if (Dist2D(to, gMe.pos) < 1000.f) continue;
            if (PlanTo(to)) { P.going = true; P.from = gFrame; Log("probe53: to place %d (%.0f %.0f %.0f), %.1f m", P.i, to[0], to[1], to[2], Dist2D(to, gMe.pos) / 1000.f); }
            else Log("probe53: no way to place %d (%.0f %.0f %.0f)", P.i, to[0], to[1], to[2]);
        }
        if (!P.going) P.standUntil = gFrame + 60;
    }
    if (P.going) FollowPath(); else Steer(0, 0, 0);
}
}  // namespace

// ---- (v150-host beta 25, bookhold) probe 52: when a player's own game lets him read a book, move by move (docs/VIRTUAL_DOLL.md 51,
// docs/PLAYABLE_DOLL.md). The game (US n003a): the book is one of the actions of the local player's root action (id 0x1B9F95, its
// check 0x410C50 registers them at the spawn): id 0xEADB2E, priority 0x13, flags 0x60000B0 (0x2000000: one of the exclusive ones).
// Each tick the scheduler 0x30F178 walks the root's actions by priority, the highest first: an exclusive action that runs (state 0)
// keeps every lower exclusive one from its check (the book's 0x443C18 is then not called: state 1) and ends a lower one that runs
// (a reading ended); the check itself refuses Raikov, a ghost, the state bits 9, 0x5C, 0x52, 0x99, 0xF0, 0x19 to 0x1C, the goggles
// on, its cooldown. The watch (each frame of a task): that gate read from its own game's memory, each change logged with what shut
// it (an action above the book that runs: its id and priority; a state bit). The book: an entry of its own game's book list
// (NewOnlineBookMan) made for the test, of a slot not its own, put 0.25 m under its head every frame from the move's start (in
// reach, a clear line: only the game's gate decides), its reading's start (+0xDE9 1, the book action running) logged in frames and
// in the player's ticks from the move's start and from the press; then the entry taken away (the reading ends), its cooldown
// cleared before the next. Tasks (MGO1_DOLL_PROBE_MOVES, default "c123vlkprh"; MGO1_DOLL_PROBE_REPEAT times each, 2): c a climb
// of 0.8 to 1.2 m (a block, the action button), 1 2 3 drops of about 1, 2 and 3 m, v a fence (a vault link, the action button), l a
// ladder up, k a crouch (Cross), p prone (a crouch, the stick: lying), r a roll (Cross running), h the others' hits
// (MGO1_DOLL_PROBE_PHASE seconds still in the open, 60: a flinch, a throw, a hold; MGO1_DOLL_PROBE_LIFE=<n> its life set back to n under
// half of it); f a fence's layer (a loadout with ",s6"): 0.6 m past the far side of the nearest fence it crosses, facing it, probe 18's
// tries lay its books there, 45 s for the others to come over to them, then a new life for the next. MGO1_DOLL_PROBE_BOOK=0: the
// watch only ----
struct Act52 { uint32_t id = 0, flags = 0; int prio = 0, st = 0; };
struct Gate52 {
    bool have = false, open = false, openNoCool = false, reading = false;
    int bookSt = -1, de9 = 0, cool = 0;
    std::string why, above, bits;                        // what shut it ("" open), the actions above the book that run, the refused bits set
};
uint32_t Root52(uint32_t obj) {
    uint32_t n = R32(obj + 0x44);
    for (int g = 0; n >= 0x100000 && n < 0x02000000 && g < 64; ++g, n = R32(n)) if ((R32(n + 0x14) & 0xFFFFFF) == 0x1B9F95) return n;
    return 0;
}
// the gate as the scheduler and the check see it now (their next tick reads the same memory)
Gate52 GateNow52() {
    Gate52 g;
    const uint32_t o = gMe.obj;
    if (!o) return g;
    const uint32_t root = Root52(o);
    if (!root) return g;
    std::vector<Act52> kids;
    uint32_t c = R32(root + 8);
    for (int k = 0; c >= 0x100000 && c < 0x02000000 && k < 64; ++k, c = R32(c)) {
        Act52 a; a.id = R32(c + 0x14) & 0xFFFFFF; a.flags = R32(c + 0x10); a.st = (int)(a.flags & 7); a.prio = R16(c + 0x18);
        kids.push_back(a);
    }
    int book = -1;
    for (size_t i = 0; i < kids.size(); ++i) if (kids[i].id == 0xEADB2E) book = (int)i;
    if (book < 0) return g;
    g.have = true;
    g.bookSt = kids[book].st;
    std::string blocker;
    for (int i = 0; i < book; ++i) {
        const Act52& a = kids[i];
        if (a.st != 0) continue;
        char t[32]; snprintf(t, sizeof t, " %x:%x", a.id, a.prio); g.above += t;
        if ((a.flags & 0x42000000u) == 0x2000000u && blocker.empty()) { char w[64]; snprintf(w, sizeof w, "the action %x (priority %x)", a.id, a.prio); blocker = w; }
    }
    static const int kRefused[] = {9, 0x5C, 0x52, 0x99, 0xF0, 0x19, 0x1A, 0x1B, 0x1C};
    for (int b : kRefused) if (gMe.bits[b >> 5] & (1u << (b & 31))) { char t[8]; snprintf(t, sizeof t, " %x", b); g.bits += t; }
    g.de9 = R8(o + 0xDE9);
    g.cool = (int)R32(o + 0xF54);
    const uint32_t k614 = R32(o + 0x614);
    const bool goggles = k614 >= 0x100000 && k614 < 0x02000000 && R8(k614 + 0x14) == 7 && (gMe.bits[0x150 >> 5] & (1u << (0x150 & 31)));
    g.reading = g.bookSt == 0 || g.de9 != 0;
    if (g.reading) g.why = "reading";
    else if (R8(o + 0xB62) == 4) g.why = "Raikov";
    else if (gMe.bits[0x156 >> 5] & (1u << (0x156 & 31))) g.why = "a ghost";
    else if (g.bookSt != 1 && g.bookSt != 4 && g.bookSt != 6) { char t[24]; snprintf(t, sizeof t, "the book's state %d", g.bookSt); g.why = t; }
    else if (!blocker.empty()) g.why = blocker;
    else if (!g.bits.empty()) g.why = "the state bit" + g.bits;
    else if (goggles) g.why = "the goggles";
    g.openNoCool = g.why.empty();
    if (g.why.empty() && g.cool > 0) g.why = "its cooldown";
    g.open = g.why.empty();
    return g;
}
// its ticks: the player's motion (layer 0: its id at +0x60, its time at [[+0x204] + 4] + 0x38, 10 a tick), a tick at each change
struct Tick52 { int id = -1; float time = -1.f; long long n = 0; };
Tick52 gTick52;
void TickNow52() {
    const uint32_t o = gMe.obj;
    if (!o) return;
    const int id = R16(o + 0x60) & 0x3FF;
    float tm = -1.f;
    const uint32_t mt = R32(o + 0x204);
    if (mt >= 0x100000 && mt < 0x02000000) { const uint32_t ly = R32(mt + 4); if (ly >= 0x100000 && ly < 0x02000000) tm = RF(ly + 0x38); }
    if (id != gTick52.id || tm != gTick52.time) { ++gTick52.n; gTick52.id = id; gTick52.time = tm; }
}
void W32m52(uint32_t a, uint32_t v) { memcpy(ps2::gMem + (a & 0x01FFFFFF), &v, 4); }
void WFm52(uint32_t a, float v) { memcpy(ps2::gMem + (a & 0x01FFFFFF), &v, 4); }
// the test's book: an entry of its own game's list (state 1, no actor: nothing drawn), of a slot not its own
struct Fake52 { uint32_t man = 0, e = 0, id = 0; int n = 0; };
Fake52 gFake52;
bool FakeOn52() { const Fake52& f = gFake52; return f.e && R32(f.e + 0x30) == 1 && R32(f.e + 0x2C) == f.id; }
void FakeOff52(const char* why) {
    Fake52& f = gFake52;
    if (FakeOn52()) {
        W32m52(f.e + 0x30, 2);                            // (as 0x3A7300: its state 2, the count one less)
        const uint32_t n = R32(f.man + 0x4C4);
        if (n > 0) W32m52(f.man + 0x4C4, n - 1);
        Log("probe52: the test's book %x taken away (%s)", f.id, why);
    }
    f.e = 0;
}
bool FakePut52(const float* at) {
    Fake52& f = gFake52;
    if (!FakeOn52()) {
        f.e = 0;
        uint32_t man = 0;
        ps2world::ForEachActor(&P32Man, &man);
        if (man < 0x100000 || man > 0x01F00000) return false;
        for (int i = 0; i < 16 && !f.e; ++i) { const uint32_t e = man + 0xC0 + 0x40 * i; if (R32(e + 0x30) == 0) f.e = e; }   // (a free entry; one taken away before if none)
        for (int i = 0; i < 16 && !f.e; ++i) { const uint32_t e = man + 0xC0 + 0x40 * i; if (R32(e + 0x30) == 2) f.e = e; }
        if (!f.e) return false;
        const int mine = R8(gMe.obj + 0xB60);
        const int slot = mine == 15 ? 14 : 15;
        f.man = man; f.id = ((uint32_t)slot << 10) | (0x3C0u + (uint32_t)(f.n++ % 0x3F));
        W32m52(f.e + 0, 0); W32m52(f.e + 4, 0);           // (no actor)
        W32m52(f.e + 0x2C, f.id); W32m52(f.e + 0x34, 0);
        for (int k = 0; k < 3; ++k) WFm52(f.e + 0x10 + 4 * k, at[k]);
        W32m52(f.e + 0x30, 1);
        W32m52(man + 0x4C4, R32(man + 0x4C4) + 1);
        Log("probe52: the test's book %x put at %.0f %.0f %.0f (its list's count %u)", f.id, at[0], at[1], at[2], R32(man + 0x4C4));
        return true;
    }
    for (int k = 0; k < 3; ++k) WFm52(f.e + 0x10 + 4 * k, at[k]);
    return true;
}
bool Head52(float* h) {
    const uint32_t a = R32(gMe.obj + 0x200);
    if (a < 0x100000 || a >= 0x02000000) return false;
    const uint32_t b = R32(a + 0x88);
    if (b < 0x100000 || b >= 0x02000000) return false;
    for (int k = 0; k < 3; ++k) h[k] = RF(b + 0x130 + 4 * k);
    return std::fabs(h[0]) < 1e6f && std::fabs(h[1]) < 1e6f && std::fabs(h[2]) < 1e6f;
}
struct Task52 { char kind = 0; float a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; };
struct P52 {
    std::string moves; int repeat = 2, idx = 0, rep = 0, phase = 0, tries = 0, done = 0; bool book = true; long long at = 0;
    Task52 task; std::vector<Task52> used;
    // the move under way: its start, the press, the book's put and read, the gate's spans (frames, ticks)
    long long winFrom = -1, moveAt = -1, moveT = -1, pressAt = -1, pressT = -1, putAt = -1, putT = -1, readAt = -1, readT = -1, endAt = -1, landAt = -1, landT = -1;
    long long shutAt = -1, shutT = -1, openAt = -1, openT = -1; std::string spans, shutWhy;
    float y0 = 0.f, minY = 1e30f, flat[3] = {0, 0, 0}, rollYaw = 0.f; int stage = 0;
    Gate52 last; bool lastHave = false; uint32_t lastW[3] = {0, 0, 0}; uint32_t padWas = 0;
    bool dead = false;                                          // (its death seen by the watch: the next frame alive is a new life)
};
P52 gP52;
void BookFrame52();
void Move52(const char* how);
// ((v150-host beta 25, fallflinch) probe 52's falls and reactions (docs/PLAYABLE_DOLL.md 28): every fall of its own player and of every
// other player's copy on its screen, frame by frame (the hips' height from the frame before the drop began, the step on the ground, the
// state word 0, the motion and its tick), kept when the drop is 0.4 m or more with a frame of 60 mm or more, then the line FALL with the
// frames to the lowest point (the landing); and its own player's hit reactions (the reaction action 0x29BD1B4 above the book): the kind
// its game took (+0xDE3), from whom (+0xEF4), the angle (+0xEAC), its length in frames and ticks, the motions played, how far it moved;
// MGO1_DOLL_PROBE_HSTICK=1: the stick pushed full and Cross every 8 frames while a reaction runs (what he can do in it)
struct FallW52 { long long onAt = -1, movedAt = -1; float onFrom[3] = {0, 0, 0}, lowPos[3] = {0, 0, 0}; std::string pend; int life0 = 0; long long at = -1; float y0 = 0.f, last[3] = {0, 0, 0}, low = 0.f, maxDrop = 0.f; long long lowAt = -1; int quiet = 0, n = 0, ticks = 0; Mot51 mot;
                 bool have = false; std::vector<std::string> lines; };
FallW52 gFallMe52, gFallCopy52[16];
int gFalls52 = 0;
void FallStep52(FallW52& F, const char* who, const float* pos, uint32_t st, uint32_t obj, bool alive) {
    if (F.onAt >= 0) {                                  // (its FALL line waits for it to move on from where it landed: 240 frames at most)
        const float moved = Dist2D(pos, F.onFrom);
        if (F.movedAt >= 0 || moved > 100.f || gFrame - F.onAt > 240 || !alive) {
            const long long at = F.movedAt >= 0 ? F.movedAt : gFrame;
            Log("%s; it moved on (100 mm) %s", F.pend.c_str(), F.movedAt >= 0 || moved > 100.f ? (std::to_string(at - F.onAt) + " frames after its lowest point").c_str() : "not within 240 frames");
            F.onAt = -1;
        }
    }
    if (!F.have || !alive) { F.have = alive; memcpy(F.last, pos, sizeof F.last); F.at = -1; F.lines.clear(); return; }
    const float dy = pos[1] - F.last[1], step = Dist2D(pos, F.last);
    if (step > 1500.f) { memcpy(F.last, pos, sizeof F.last); F.at = -1; F.lines.clear(); return; }   // (a respawn, a pull: no fall)
    const Mot51 mo = MotionOf(obj);
    if (F.at < 0) {
        if (dy > (strcmp(who, "me") ? -30.f : -12.f)) { memcpy(F.last, pos, sizeof F.last); F.mot = mo; return; }
        F.at = gFrame - 1; F.y0 = F.last[1]; F.low = pos[1]; F.lowAt = gFrame; F.quiet = 0; F.maxDrop = 0.f; F.ticks = 0; F.lines.clear(); F.life0 = gMe.life;
        memcpy(F.lowPos, pos, sizeof F.lowPos); F.movedAt = -1;
    }
    if (mo.id != F.mot.id || mo.time != F.mot.time) ++F.ticks;
    F.mot = mo;
    F.maxDrop = std::max(F.maxDrop, -dy);
    if (pos[1] < F.low - 5.f) { F.low = pos[1]; F.lowAt = gFrame; memcpy(F.lowPos, pos, sizeof F.lowPos); F.movedAt = -1; }
    else if (F.movedAt < 0 && Dist2D(pos, F.lowPos) > 100.f && F.y0 - F.low >= 300.f) F.movedAt = gFrame;
    F.quiet = std::fabs(dy) < 5.f ? F.quiet + 1 : 0;
    char b[200]; snprintf(b, sizeof b, "probe52 fall %s +%lld t%d: down %.0f (%+.0f), step %.0f, state %x, motion %x %.0f", who, gFrame - F.at, F.ticks, F.y0 - pos[1], dy, step, st, mo.id, mo.time);
    if (F.lines.size() < 200) F.lines.push_back(b);
    memcpy(F.last, pos, sizeof F.last);
    if (F.quiet >= 30 || gFrame - F.at > 240 || dy > 60.f) {
        const float drop = F.y0 - F.low;
        const bool me = !strcmp(who, "me");                 // (its own: every drop of 0.3 m or more, a step down walked too)
        if ((me ? drop >= 300.f : drop >= 400.f && F.maxDrop >= 60.f) && gFalls52 < 160) {
            ++gFalls52;
            for (const std::string& l : F.lines) Log("%s", l.c_str());
            char b2[300];
            snprintf(b2, sizeof b2, "probe52: FALL %s: %.2f m down in %lld frames to its lowest point (the frame before the drop to it), %d ticks of its motion meanwhile, the largest frame %.0f mm%s",
                     who, drop / 1000.f, F.lowAt - F.at, F.ticks, F.maxDrop, me ? (gMe.life != F.life0 ? " (its life changed)" : " (its life unchanged)") : "");
            if (F.onAt >= 0) Log("%s; it moved on: not before its next fall", F.pend.c_str());
            F.pend = b2; F.onAt = F.lowAt; memcpy(F.onFrom, F.lowPos, sizeof F.onFrom);
        }
        F.at = -1; F.lines.clear();
    }
}
struct React52 { long long at = -1; long long t0 = 0; float p0[3] = {0, 0, 0}; int kind = -1, from = -1, angle = 0; std::string mots; int lastMot = -1; uint32_t st0 = 0; float maxMove = 0.f; int n = 0;
                 int yaw0 = 0; };                    // ((v150-host beta 25, reactfix) its +0x142 at the start: the turn the reaction's angle made)
React52 gReact52;
void Reaction52(const Gate52& g) {
    React52& R = gReact52;
    const bool on = g.have && g.above.find("9bd1b4:29") != std::string::npos;
    static const bool stick = getenv("MGO1_DOLL_PROBE_HSTICK") && !strcmp(getenv("MGO1_DOLL_PROBE_HSTICK"), "1");
    const uint32_t o = gMe.obj;
    if (on && R.at < 0 && o && gMe.alive) {                // (dead: its death's reaction is no hit's)
        R.at = gFrame; R.t0 = gTick52.n; memcpy(R.p0, gMe.pos, sizeof R.p0); R.kind = R8(o + 0xDE3); R.from = R8(o + 0xEF4); R.angle = R16(o + 0xEAC);
        R.mots.clear(); R.lastMot = -1; R.st0 = gMe.bits[0]; R.maxMove = 0.f; R.yaw0 = R16(o + 0x142);
        Log("probe52: a reaction begins at f%lld t%lld: the kind %d from player %d, the angle %04x, its facing %.0f degrees, state %x %x %x, life %d, stamina %d", gFrame, gTick52.n, R.kind,
            R.from + 1, R.angle, Facing() * 57.2958f, gMe.bits[0], gMe.bits[2], gMe.bits[7], gMe.life, gMe.stun);
    }
    if (R.at >= 0) {
        const int m = R16(o + 0x60) & 0x3FF;
        if (m != R.lastMot) { char b[24]; snprintf(b, sizeof b, " %x@%lld", m, gTick52.n - R.t0); R.mots += b; R.lastMot = m; }
        R.maxMove = std::max(R.maxMove, Dist2D(gMe.pos, R.p0));
        if (on && stick && gMe.alive) {                   // (what a player can do in it: the stick full ahead, Cross every 8 frames)
            gControl = true;
            Steer(std::sin(Facing()), std::cos(Facing()), 1.f);
            if ((gFrame - R.at) % 8 < 2) gPad.buttons |= 0x4000;
        }
        if (!on || !gMe.alive) {
            Log("probe52: REACTION %d: the kind %d from player %d: %lld frames, %lld ticks (f%lld..f%lld); its motions%s; moved %.0f mm (at most %.0f), the hips %.0f to %.0f; state %x -> %x%s%s",
                R.n++, R.kind, R.from + 1, gFrame - R.at, gTick52.n - R.t0, R.at, gFrame, R.mots.c_str(), Dist2D(gMe.pos, R.p0), R.maxMove, R.p0[1], gMe.pos[1], R.st0, gMe.bits[0],
                stick ? " (the stick held and Cross pressed in it)" : "", !gMe.alive ? " (dead)" : "");
            // ((v150-host beta 25, reactfix) its yaw (+0x142, 16 bits) at the start and now: the turn the record's angle made)
            if (o) Log("probe52: REACTION %d's yaw: %04x at its start, %04x at its end (the record's angle %04x)", R.n - 1, R.yaw0, R16(o + 0x142), R.angle);
            R.at = -1;
        }
    }
}
// ((v150-host beta 25, fallflinch) every other player's copy flinching on this screen: its game plays the reaction record's motion on
// it (0x2E, 0x2F, 0x32, 0x31 and the kind 5's), its ticks, then the frames until its place moves on 100 mm (the player's own records
// again: a real client's after his own reaction, a virtual doll's after its flinch)
struct CopyFl52 { long long at = -1, endAt = -1; int mot = -1, ticks = 0, n = 0; float p0[3] = {0, 0, 0}; Mot51 last; };
CopyFl52 gCopyFl52[16];
void CopyFlinch52(const Player& p) {
    CopyFl52& C = gCopyFl52[p.id];
    const Mot51 mo = MotionOf(p.obj);
    const bool fl = mo.id == 0x2E || mo.id == 0x2F || mo.id == 0x32 || mo.id == 0x31;
    if (C.at < 0) {
        if (fl && p.alive && C.n < 60) { C.at = gFrame; C.endAt = -1; C.mot = mo.id; C.ticks = 0; memcpy(C.p0, p.pos, sizeof C.p0); C.last = mo; }
        return;
    }
    if (mo.id != C.last.id || mo.time != C.last.time) ++C.ticks;
    C.last = mo;
    if (C.endAt < 0 && !fl) { C.endAt = gFrame; C.ticks -= 1; memcpy(C.p0, p.pos, sizeof C.p0); }
    if (fl && C.endAt >= 0) { C.endAt = -1; }      // (a reaction again)
    const bool on = C.endAt >= 0 && Dist2D(p.pos, C.p0) > 100.f;
    if (on || !p.alive || gFrame - C.at > 600) {
        Log("probe52: COPY P%d flinch %d: the motion %x %d ticks (%lld frames) on this screen; its place moved on %s", p.id + 1, C.n++, C.mot, C.ticks,
            (C.endAt >= 0 ? C.endAt : gFrame) - C.at, on ? (std::to_string(gFrame - C.at) + " frames after the reaction's start").c_str() : !p.alive ? "never (dead)" : "not within 10 s");
        C.at = -1;
    }
}
void Falls52() {
    FallStep52(gFallMe52, "me", gMe.pos, gMe.bits[0], gMe.obj, gMe.alive && gMe.obj);
    for (const Player& p : gPlayers) {
        if (p.local || p.id < 0 || p.id >= 16) continue;
        char who[8]; snprintf(who, sizeof who, "P%d", p.id + 1);
        FallStep52(gFallCopy52[p.id], who, p.pos, p.bits[0], p.obj, p.alive && p.obj);
        if (p.obj) CopyFlinch52(p);
    }
}
// the watch's line at a change (in a task's window), and the spans of the gate shut by something else than its cooldown; every frame of
// the map (a hit's reaction holds the player's state word 7's 0x1000000, read by the doll as frozen: no probe frame then), so the hits'
// task's start and the test's book are kept here
void Watch52() {
    P52& P = gP52;
    TickNow52();
    const Gate52 g = GateNow52();
    Falls52(); Reaction52(g);                              // ((v150-host beta 25, fallflinch) every fall seen, its own reactions)
    const uint32_t w[3] = {gMe.bits[0], gMe.bits[2], gMe.bits[7]};
    const uint32_t pad = LastPad().buttons;                // (the frame before's, as the probe and the path pressed it)
    if (P.winFrom < 0) { P.last = g; P.lastHave = g.have; memcpy(P.lastW, w, sizeof w); P.padWas = pad; return; }
    const bool change = g.have != P.lastHave || g.open != P.last.open || g.why != P.last.why || g.bookSt != P.last.bookSt || g.de9 != P.last.de9 || g.above != P.last.above ||
                        g.bits != P.last.bits || memcmp(w, P.lastW, sizeof w) != 0 || ((pad ^ P.padWas) & 0xF000);
    if (change) Log("probe52 f%lld t%lld: %s%s%s; book %d de9 %d cool %d; above:%s; state %x %x %x; hips %.0f; pad %x", gFrame, gTick52.n, g.open ? "OPEN" : "SHUT",
                    g.why.empty() ? "" : " by ", g.why.c_str(), g.bookSt, g.de9, g.cool, g.above.empty() ? " -" : g.above.c_str(), w[0], w[1], w[2], gMe.pos[1], pad);
    // the spans shut by the move (its cooldown and a reading aside), from the window's start
    const bool shut = g.have && !g.openNoCool && !g.reading;
    if (shut && P.shutAt < 0) { P.shutAt = gFrame; P.shutT = gTick52.n; P.shutWhy = g.why; }
    if (!shut && P.shutAt >= 0) {
        char t[160]; snprintf(t, sizeof t, " [f%lld..f%lld t%lld..t%lld %s]", P.shutAt, gFrame, P.shutT, gTick52.n, P.shutWhy.c_str());
        if (P.spans.size() < 1500) P.spans += t;
        P.shutAt = -1;
        if (P.moveAt >= 0 && P.openAt < 0 && gFrame >= P.moveAt) { P.openAt = gFrame; P.openT = gTick52.n; }
    }
    if (shut && P.shutAt >= 0 && g.why != P.shutWhy && g.why != "reading") {   // (another cause: a span of its own)
        char t[160]; snprintf(t, sizeof t, " [f%lld..f%lld t%lld..t%lld %s]", P.shutAt, gFrame, P.shutT, gTick52.n, P.shutWhy.c_str());
        if (P.spans.size() < 1500) P.spans += t;
        P.shutAt = gFrame; P.shutT = gTick52.n; P.shutWhy = g.why;
    }
    if (g.reading && P.putAt >= 0 && P.readAt < 0) { P.readAt = gFrame; P.readT = gTick52.n; }
    P.last = g; P.lastHave = g.have; memcpy(P.lastW, w, sizeof w); P.padWas = pad;
    if (!gMe.alive) P.dead = true;
    // (the hits' task: its start at the first gate shut by something else than its cooldown, its own death's action aside)
    if (P.phase == 3 && (P.task.kind == 'h' || (P.task.kind == 'q' && P.stage >= 3)) && P.moveAt < 0 && g.have && !g.openNoCool && !g.reading && g.why.find("286558") == std::string::npos) Move52(g.why.c_str());
    BookFrame52();
}
// the test's book from the move's start (frame and tick now) until its reading began, then away
void BookFrame52() {
    P52& P = gP52;
    if (!P.book || P.moveAt < 0) return;
    if (P.readAt >= 0) { if (FakeOn52() && gFrame - P.readAt >= 4) FakeOff52("its reading began"); return; }
    if (P.putAt < 0) {
        P.putAt = gFrame; P.putT = gTick52.n;
        if (R32(gMe.obj + 0xF54) != 0) { Log("probe52: its cooldown %d cleared for the test", (int)R32(gMe.obj + 0xF54)); W32m52(gMe.obj + 0xF54, 0); }
    }
    float h[3];
    if (!Head52(h)) return;
    const float at[3] = {h[0], h[1] - 250.f, h[2]};
    FakePut52(at);
}
void Begin52(long long frame) {                               // a move's window opens (the watch logs from here)
    P52& P = gP52;
    P.winFrom = frame; P.moveAt = P.moveT = P.pressAt = P.pressT = P.putAt = P.putT = P.readAt = P.readT = P.endAt = P.landAt = P.landT = -1;
    P.shutAt = P.shutT = P.openAt = P.openT = -1; P.spans.clear(); P.shutWhy.clear();
    P.y0 = gMe.pos[1]; P.minY = gMe.pos[1]; P.stage = 0;
}
void Move52(const char* how) {                                 // the move's start: now
    P52& P = gP52;
    if (P.moveAt >= 0) return;
    P.moveAt = gFrame; P.moveT = gTick52.n;
    Log("probe52: %c %d: the move's start at f%lld t%lld (%s)", P.task.kind, P.rep, gFrame, gTick52.n, how);
}
void Result52(const char* what) {
    P52& P = gP52;
    if (P.shutAt >= 0) { char t[160]; snprintf(t, sizeof t, " [f%lld..still t%lld.. %s]", P.shutAt, P.shutT, P.shutWhy.c_str()); P.spans += t; P.shutAt = -1; }
    FakeOff52("the task's end");
    auto rel = [&](long long f, long long t) { char b[48]; if (f < 0 || P.moveAt < 0) snprintf(b, sizeof b, "none"); else snprintf(b, sizeof b, "%+lld frames %+lld ticks", f - P.moveAt, t - P.moveT); return std::string(b); };
    auto relp = [&](long long f, long long t) { char b[48]; if (f < 0 || P.pressAt < 0) snprintf(b, sizeof b, "none"); else snprintf(b, sizeof b, "%+lld frames %+lld ticks", f - P.pressAt, t - P.pressT); return std::string(b); };
    Log("probe52: RESULT %c %d (%s): the move at f%lld t%lld; the press %s; landed %s; the gate open again %s (from the press %s); the book put %s, READ %s (from the press %s); "
        "the hips %.0f to %.0f (lowest %.0f); the spans shut:%s", P.task.kind, P.rep, what, P.moveAt, P.moveT, rel(P.pressAt, P.pressT).c_str(), rel(P.landAt, P.landT).c_str(),
        rel(P.openAt, P.openT).c_str(), relp(P.openAt, P.openT).c_str(), rel(P.putAt, P.putT).c_str(), rel(P.readAt, P.readT).c_str(), relp(P.readAt, P.readT).c_str(),
        P.y0, gMe.pos[1], P.minY, P.spans.empty() ? " none" : P.spans.c_str());
    P.winFrom = -1;
}
// a link of the task's kind, the nearest from here not used yet
bool PickLink52(char kind, Task52& out) {
    P52& P = gP52;
    float best = 1e30f; bool any = false;
    auto usedNear = [&](const float* a) { for (const Task52& u : P.used) if (u.kind == kind && Dist2D(u.a, a) < 3000.f) return true; return false; };
    auto floorOf = [](float* q) { const float up[3] = {q[0], q[1] + 300.f, q[2]}; const float g = gMap->nav->GroundBelow(up, 3000.f); if (g > -1e29f) q[1] = g; };
    if (kind == 'l') {
        for (const Ladder& l : gMap->nav->Ladders()) {
            Task52 t; t.kind = kind; memcpy(t.a, l.foot, sizeof t.a); memcpy(t.b, l.top, sizeof t.b); floorOf(t.a); floorOf(t.b);
            const float d = Dist2D(t.a, gMe.pos);
            if (usedNear(t.a) || d >= best) continue;
            best = d; out = t; any = true;
        }
        return any;
    }
    for (int i = 0; i < gMap->nav->LinkCount(); ++i) {
        Task52 t; t.kind = kind; uint8_t area = 0;
        if (!gMap->nav->LinkAt(i, t.a, t.b, &area)) continue;
        if (kind == 'c') {
            if (area != AREA_CLIMB && area != AREA_STEPS) continue;
            if (t.a[1] > t.b[1]) std::swap(t.a, t.b);
            const float rise = t.b[1] - t.a[1];
            if (rise < 800.f || rise > 1200.f) continue;
        } else if (kind == 'v') {
            if (area != AREA_VAULT) continue;
        } else if (kind >= '0' && kind <= '4') {
            if (area != AREA_DROP) continue;
            const float fall = t.a[1] - t.b[1], want = 1000.f * (kind - '0');
            // ((v150-host beta 25, fallflinch) 0: a drop of 0.45 to 0.8 m; 4: the deepest of the map, 3 m or more)
            if (kind == '0' ? fall > 800.f : kind == '4' ? fall < 3000.f : std::fabs(fall - want) > (kind == '3' ? 500.f : 350.f)) continue;
        } else continue;
        float d = Dist2D(t.a, gMe.pos) + std::fabs(t.a[1] - (gMe.pos[1] - 800.f)) * 3.f;
        if (kind == '4') d = -(t.a[1] - t.b[1]) * 100.f + Dist2D(t.a, gMe.pos);   // (the deepest first)
        if (usedNear(t.a) || d >= best) continue;
        best = d; out = t; any = true;
    }
    return any;
}
// a flat clear way for a roll from here (a run, then the roll's 5.5 m): the direction with 9 m clear at the knees and the chest and the floor within 0.15 m
bool RollWay52(float* yawOut) {
    for (int k = 0; k < 16; ++k) {
        const float yaw = Facing() + k * 0.3927f * ((k & 1) ? 1.f : -1.f) * (float)((k + 1) / 2);
        const float s = std::sin(yaw), c = std::cos(yaw);
        const float floor = gMe.pos[1] - 800.f;
        bool ok = true;
        for (int i = 1; i <= 36 && ok; ++i) {
            const float p[3] = {gMe.pos[0] + s * 250.f * i, floor + 700.f, gMe.pos[2] + c * 250.f * i};
            const float g = gMap->nav->GroundBelow(p, 1500.f);
            if (g < -1e29f || std::fabs(g - floor) > 150.f) ok = false;
        }
        for (int h = 0; h < 2 && ok; ++h) {
            const float p0[3] = {gMe.pos[0], floor + (h ? 1100.f : 450.f), gMe.pos[2]}, p1[3] = {p0[0] + s * 9000.f, p0[1], p0[2] + c * 9000.f};
            float t = 1.f, n[3];
            if (gMap->nav->FirstHit(p0, p1, &t, n)) ok = false;
        }
        if (ok) { *yawOut = yaw; return true; }
    }
    return false;
}
void Next52(const char* why) {
    P52& P = gP52;
    if (why) Log("probe52: %c %d: %s", P.task.kind, P.rep, why);
    StopPath(); UnstickFrame(false); Steer(0, 0, 0);
    P.phase = 0; P.at = gFrame; P.tries = 0;
    if (++P.rep >= P.repeat) { P.rep = 0; ++P.idx; }
}
void Probe52Frame() {
    P52& P = gP52;
    static bool init = false;
    if (!init) {
        init = true;
        const char* m = getenv("MGO1_DOLL_PROBE_MOVES"); P.moves = m && *m ? m : "c123vlkprh";
        if (const char* r = getenv("MGO1_DOLL_PROBE_REPEAT")) P.repeat = std::max(1, atoi(r));
        P.book = !(getenv("MGO1_DOLL_PROBE_BOOK") && !strcmp(getenv("MGO1_DOLL_PROBE_BOOK"), "0"));
        Log("probe52: the book's gate move by move: tasks %s, %d each, the test's book %s", P.moves.c_str(), P.repeat, P.book ? "on" : "off (the watch only)");
    }
    static bool started = false;
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    if (!started || P.dead) { started = true; P.dead = false; if (P.winFrom >= 0) Result52("cut: a new life"); P.phase = 0; P.at = gFrame; }
    if (P.idx >= (int)P.moves.size()) {
        if (!P.done) { P.done = 1; Log("probe52: done"); }
        Steer(0, 0, 0);
        return;
    }
    const char kind = P.moves[P.idx];
    P.task.kind = kind;
    const long long t = gFrame - P.at;
    const bool linkKind = kind == 'c' || kind == 'v' || kind == 'l' || (kind >= '0' && kind <= '4');
    switch (P.phase) {
    case 0: {                                                   // the next place
        if (!linkKind && kind != 'f') {
            if (kind == 'r') {
                float yaw;
                if (RollWay52(&yaw)) { P.rollYaw = yaw; P.phase = 2; P.at = gFrame; break; }
                float p[3];
                if (++P.tries > 8) { Next52("no flat clear way for a roll found"); break; }
                if (RandomReachable(20000.f, p) && PlanTo(p)) { P.phase = 1; P.at = gFrame; memcpy(P.task.a, p, sizeof p); }
                break;
            }
            P.phase = 2; P.at = gFrame;
            break;
        }
        Task52 tk;
        if (kind == 'f') {                                       // (the fence's far side, 0.6 m past its end, on its floor)
            if (!PickLink52('v', tk)) { Log("probe52: f: no fence on this map"); P.rep = P.repeat - 1; Next52(nullptr); break; }
            const float dx = tk.b[0] - tk.a[0], dz = tk.b[2] - tk.a[2], dl = std::max(1.f, std::hypot(dx, dz));
            const float far0[3] = {tk.b[0] + dx / dl * 600.f, tk.b[1] + 300.f, tk.b[2] + dz / dl * 600.f};
            const float g = gMap->nav->GroundBelow(far0, 1500.f);
            if (g < -1e29f) { if (++P.tries > 6) Next52("no floor past the fence"); break; }
            tk.kind = 'f'; memcpy(P.flat, tk.a, sizeof P.flat);  // (the fence's near side: faced while it lays)
            tk.a[0] = far0[0]; tk.a[1] = g; tk.a[2] = far0[2];
            P.task = tk; Task52 u = tk; u.kind = 'v'; memcpy(u.a, P.flat, sizeof u.a); P.used.push_back(u);
            if (!PlanTo(tk.a)) { if (++P.tries > 6) Next52("no way to the fence's far side"); break; }
            gP18 = Probe18();
            Log("probe52: f %d: to the far side of the fence %.0f %.0f %.0f -> %.0f %.0f %.0f, its books laid there", P.rep, P.flat[0], P.flat[1], P.flat[2], tk.b[0], tk.b[1], tk.b[2]);
            P.phase = 1; P.at = gFrame;
            break;
        }
        if (!PickLink52(kind, tk)) { Log("probe52: %c: no such place on this map", kind); P.rep = P.repeat - 1; Next52(nullptr); break; }
        P.task = tk;
        P.used.push_back(tk);
        if (!PlanTo(tk.a)) { if (++P.tries > 6) Next52("no way to any such place"); break; }
        Log("probe52: %c %d: to %.0f %.0f %.0f, then %.0f %.0f %.0f (%+.2f m)", kind, P.rep, tk.a[0], tk.a[1], tk.a[2], tk.b[0], tk.b[1], tk.b[2], (tk.b[1] - tk.a[1]) / 1000.f);
        P.phase = 1; P.at = gFrame;
        break;
    }
    case 1: {                                                   // to its start
        if (kind == 'r') {
            if (!Following() || t > 2400) { StopPath(); Steer(0, 0, 0); P.phase = 0; P.at = gFrame; break; }
            FollowPath();
            break;
        }
        if ((Dist2D(gMe.pos, P.task.a) < (kind == 'f' ? 350.f : 500.f) || (!Following() && Dist2D(gMe.pos, P.task.a) < 1200.f)) && std::fabs(gMe.pos[1] - 800.f - P.task.a[1]) < 500.f && !OnLadder()) {
            StopPath(); UnstickFrame(false); Steer(0, 0, 0); P.phase = 2; P.at = gFrame; break;
        }
        if (!Following() || t > 3600) { Next52("its start not reached"); break; }
        const int u = UnstickFrame(true);
        if (u == 1) break;
        if (u == -1) { UnstickGaveUp(); Next52("the ways out ran out on the way to its start"); break; }
        FollowPath();
        break;
    }
    case 2:                                                     // still a moment there, then the move's window
        Steer(0, 0, 0);
        if (kind == 'f') {                                      // (turned to the fence by short pushes, then probe 18's tries)
            if (t < 24 && t % 6 < 2) Steer(P.flat[0] - gMe.pos[0], P.flat[2] - gMe.pos[2], 0.25f);
            if (t >= 40) { P.phase = 4; P.at = gFrame; Log("probe52: f %d: at %.0f %.0f %.0f facing the fence (%.0f degrees off), laying", P.rep, gMe.pos[0], gMe.pos[1], gMe.pos[2],
                                                         std::fabs(Wrap(Facing() - std::atan2(P.flat[0] - gMe.pos[0], P.flat[2] - gMe.pos[2]))) * 57.3f); }
            break;
        }
        if (t < 40) break;
        Begin52(gFrame);
        P.phase = 3; P.at = gFrame;
        if (linkKind && !PlanTo(P.task.b)) { Result52("no way over the link"); Next52(nullptr); }
        break;
    case 3: {                                                   // the move
        bool over = false; const char* what = "made";
        if (linkKind) {
            // its start: the gate shut by something else than its cooldown, a ladder, or on the link's own segment the hips 0.3 m up or
            // down (a move the game lets the book through: a step or a slope before it is no start)
            if (P.moveAt < 0 && ((P.lastHave && !P.last.openNoCool && !P.last.reading) || OnLadder() || (OnLinkNow() && std::fabs(gMe.pos[1] - P.y0) > 300.f)))
                Move52(OnLadder() ? "on the ladder" : (P.lastHave && !P.last.openNoCool && !P.last.reading) ? "the gate shut" : "on the link, the hips 0.3 m off");
            if (Following()) {
                const int u = UnstickFrame(true);
                if (u != 1) FollowPath();
            } else Steer(0, 0, 0);
            const bool there = Dist2D(gMe.pos, P.task.b) < 700.f && std::fabs(gMe.pos[1] - 800.f - P.task.b[1]) < 400.f && !OnLadder();
            if (there && P.endAt < 0) P.endAt = gFrame;
            if (kind >= '0' && kind <= '4' && P.moveAt >= 0 && P.landAt < 0 && gMe.pos[1] - 800.f < P.task.b[1] + 150.f) { P.landAt = gFrame; P.landT = gTick52.n; }
            const bool settled = P.endAt >= 0 && gFrame - P.endAt >= 120 && (P.readAt < 0 || gFrame - P.readAt >= 30) && (P.readAt >= 0 || !P.book || gFrame - P.endAt >= 300);
            if (settled) over = true;
            if (t > 2400) { over = true; what = P.endAt >= 0 ? "made, the book not read" : "not made in 40 s"; }
        } else if (kind == 'k' || kind == 'p') {
            // k: Cross (3 frames) after 30 still; crouched 60 frames, the book; 150 after the press, Cross again. p: crouched first, then
            // the stick 8 frames (he lies down); lying 60 frames, the book; 150 after the push, Cross twice (up). The way down: the watch
            const uint32_t st = gMe.bits[0] & 0xFFFF;
            const bool crouched = (st & 4) != 0, lying = (st & 8) != 0;
            Steer(0, 0, 0);
            if (P.stage == 0 && t >= 30) { P.stage = 1; P.at = gFrame; }
            const long long u = gFrame - P.at;
            if (P.stage == 1) {
                if (u < 3) gPad.buttons |= 0x4000;
                static long long lowAt = -1;
                if (u == 0) lowAt = -1;
                if (kind == 'k' && crouched && lowAt < 0) lowAt = gFrame;
                if (kind == 'k' && lowAt >= 0 && gFrame - lowAt == 60) Move52("crouched 1 s");
                if (kind == 'k' && u >= 200) { P.stage = 3; P.at = gFrame; if (!crouched) Log("probe52: k %d: not crouched after Cross (state %x)", P.rep, gMe.bits[0]); }
                if (kind == 'p' && u >= 60) { if (crouched) { P.stage = 2; P.at = gFrame; } else if (u > 200) { over = true; what = "not crouched"; } }
            } else if (P.stage == 2) {
                if (u < 8) { const float f = Facing(); Steer(std::sin(f), std::cos(f), 1.f); }
                static long long lieAt = -1;
                if (u == 0) lieAt = -1;
                if (lying && lieAt < 0) lieAt = gFrame;
                if (lieAt >= 0 && gFrame - lieAt == 60) Move52("lying 1 s");
                if (u >= 240) { P.stage = 3; P.at = gFrame; if (!lying) Log("probe52: p %d: not lying after the push (state %x)", P.rep, gMe.bits[0]); }
            } else if (P.stage == 3) {
                if ((u < 3 || (u >= 50 && u < 53)) && Low()) gPad.buttons |= 0x4000;
                if (u >= 120) over = true;
            }
        } else if (kind == 'r') {
            // the stick toward the clear way 20 frames (a run), then Cross 3 frames with the stick held, the stick let go once the roll
            // began (its bit 9, 12 frames at most: a press with no run crouches); 150 after
            if (P.stage == 0) {
                Steer(std::sin(P.rollYaw), std::cos(P.rollYaw), 1.f);
                if (t >= 20) { P.stage = 1; P.at = gFrame; }
            }
            if (P.stage == 1) {
                const long long u = gFrame - P.at;
                if (u < 3) gPad.buttons |= 0x4000;
                if (u < 12 && P.moveAt < 0) Steer(std::sin(P.rollYaw), std::cos(P.rollYaw), 1.f);
                if (u == 0) { P.pressAt = gFrame; P.pressT = gTick52.n; }
                // (the book from the roll's own state: the bit 9 of the state word 0; the press runs on 3 ticks first)
                if (P.moveAt < 0 && (gMe.bits[0] & 0x200)) Move52("the roll's bit 9");
                if (u >= 150) over = true;
            }
        } else if (kind == 'h' || kind == 'q') {
            // still where it is; the book from the first time the gate is shut by something else than its cooldown, then again 2 s
            // after each reading's start (the phase's length). ((v150-host beta 25, fallflinch) q: the same lying: Cross, then the stick
            // 8 frames once crouched, then still lying; down again the same way after a reaction stood it up)
            static const long long phaseF = 60LL * (getenv("MGO1_DOLL_PROBE_PHASE") ? std::max(10, atoi(getenv("MGO1_DOLL_PROBE_PHASE"))) : 60);
            Steer(0, 0, 0);
            if (kind == 'q') {
                const uint32_t st = gMe.bits[0] & 0xFFFF;
                const bool crouched = (st & 4) != 0, lying = (st & 8) != 0;
                static long long qAt = 0;                         // (the stages' own clock: P.at is the phase's)
                if (P.stage == 0 && qAt < P.at) qAt = gFrame;
                const long long u = gFrame - qAt;
                if (P.stage < 3 && lying) { P.stage = 3; Log("probe52: q %d: lying (state %x)", P.rep, gMe.bits[0]); }
                if (P.stage >= 3 && !lying && gReact52.at < 0 && (gMe.bits[0] & 0x1E000000) == 0 && P.moveAt < 0) { P.stage = 0; qAt = gFrame; }   // (stood up: down again)
                if (P.stage == 0) { if (u >= 20 && u < 23) gPad.buttons |= 0x4000; if (u >= 23) { P.stage = 1; qAt = gFrame; } }
                else if (P.stage == 1) { if (crouched && u >= 30) { P.stage = 2; qAt = gFrame; } else if (u > 120) { P.stage = 0; qAt = gFrame; } }
                else if (P.stage == 2) { if (u < 8) { const float f = Facing(); Steer(std::sin(f), std::cos(f), 1.f); } if (u > 120) { P.stage = 0; qAt = gFrame; } }
            }
            // (MGO1_DOLL_PROBE_LIFE=<n>: its life set back to n when under half of it, the hits' moments many in a life; a bench measure)
            static const int lifeTo = getenv("MGO1_DOLL_PROBE_LIFE") ? atoi(getenv("MGO1_DOLL_PROBE_LIFE")) : 0;
            if (lifeTo > 0 && gMe.obj && gMe.life > 0 && gMe.life < lifeTo / 2) { W16(gMe.obj + 0xB64, (uint16_t)lifeTo); Log("probe52: its life %d set to %d (MGO1_DOLL_PROBE_LIFE)", gMe.life, lifeTo); }
            // (its own death's action, 0x2286558, aside: no reading for the dead)
            if (P.readAt >= 0 && gFrame - P.readAt >= 120) { Result52("a hit's moment"); Begin52(gFrame); }
            if (t >= phaseF) over = true;
        }
        // the press (the action button, Cross; this frame's pad) and the hips, in the window
        if ((gPad.buttons & 0x5000) && P.moveAt < 0 && kind != 'r') { P.pressAt = gFrame; P.pressT = gTick52.n; }
        if (kind == 'p' && P.stage == 2 && gFrame - P.at < 8 && P.moveAt < 0) { P.pressAt = gFrame; P.pressT = gTick52.n; }
        P.minY = std::min(P.minY, gMe.pos[1]);
        BookFrame52();                                          // (its start this frame: the book now, the watch keeps it after)
        if (over) { Result52(what); Next52(nullptr); }
        break;
    }
    case 4:                                                     // f: probe 18's tries (its books), then 45 s, then a new life
        if (gP18.phase != 9 && t < 2400) { Probe18Frame(); break; }
        Steer(0, 0, 0);
        if (t < 2400 && gP18.phase == 9) { P.at = gFrame - 2400; Log("probe52: f %d: its books laid; 45 s for the others", P.rep); break; }
        if (t < 2400 + 2700) break;
        { const bool more = P.rep + 1 < P.repeat; Next52(nullptr); if (more) { P.phase = 5; P.at = gFrame; Log("probe52: f: a new life for its next books"); } }
        break;
    case 5:                                                     // f: SUICIDE (the pause menu), its next life the next fence
        if (!ProbeSuicideFrame() && t > 300) { P.phase = 0; P.at = gFrame; }
        break;
    }
}

// ---- (v150-host beta 25, reactfix) probe 54: the reaction kind a shooter's game tells and what the victim's game plays
// (docs/VIRTUAL_DOLL.md 58, docs/PLAYABLE_DOLL.md 29). Two playable dolls in a deathmatch: doll 1 the shooter, doll 2 the victim.
// The victim stands still where it spawned (MGO1_DOLL_PROBE_STANCE=prone: it lies, as probe 52's q), presses nothing; lying awake
// (knocked down) 240 frames it presses Cross to get up; probe 52's reaction lines on it (the kind its game took, from whom, the
// angle, its yaw at the start and at the end). The shooter: the trials of MGO1_DOLL_PROBE_TRIALS (by comma) "<w><degrees>[@<m>]":
// f the flamethrower (Square held to a reaction on his copy, 150 frames at most), m the M37 (one shot), M the M37 twice (the second
// once he lies), p three Circles with empty hands (the punches and the kick), at <degrees> from the victim's facing (0 his front,
// 180 his back) and <m> metres from him (f 2.5, m 3, p 0.55). Each shot logged with my place, his place, his facing as my game
// holds his copy (+0x142, the matrix), his state words and life; then his copy's motions until it is over. The records sent are
// in the P2P log ----
struct Trial54 { char w = 0, w2 = 0; float ang = 0.f, dist = 0.f; std::string text; };   // (k: w2 the weapon after the kick, f or m)
struct P54 { std::vector<Trial54> trials; size_t idx = 0; int phase = 0, tries = 0, shots = 0, second = 0, lastMot = -1, vLast = -1; long long at = 0, life = -1, giveAt = -1000,
             onSince = -1, firedAt = -1, lieSince = -1, upSince = -1; bool spotHave = false; float spot[3] = {0, 0, 0}, spotFor[3] = {0, 0, 0}; uint32_t vBits = 0; int vLife = -1;
             int vStage = 0; long long vAt = 0, vDownSince = -1; std::string mots; float spotAng = 0.f; int yawWas = -1; };
P54 gP54;
const Player* Victim54() {
    const Player* best = nullptr;
    for (const Player& p : gPlayers)
        if (!p.local && p.obj && p.team != 0xFE && p.name.compare(0, 7, "VIRTUAL") != 0 && (!best || Dist2D(p.pos, gMe.pos) < Dist2D(best->pos, gMe.pos))) best = &p;
    return best;
}
bool Reacts54(int m) { return (m >= 0x2E && m <= 0x33); }   // (the reaction motions: 0x2E kind 1, 0x2F 2, 0x30 8, 0x31 4, 0x32 3, 0x33 5)
void Victim54Frame() {
    P54& P = gP54;
    static bool prone = getenv("MGO1_DOLL_PROBE_STANCE") && !strcmp(getenv("MGO1_DOLL_PROBE_STANCE"), "prone");
    if (gRoom.phase != 2) return;
    gPad.buttons = 0;
    Steer(0, 0, 0);
    const uint32_t st = gMe.bits[0];
    const bool lyingAwake = (st & 0x18000000u) != 0, lying = (st & 8) != 0, crouched = (st & 4) != 0;
    if (gMe.life != P.vLife || gMe.bits[0] != P.vBits) {
        Log("probe54: victim: life %d stun %d state %x %x %x (was %d %x), at %.0f %.0f %.0f, its yaw %04x", gMe.life, gMe.stun, gMe.bits[0], gMe.bits[2], gMe.bits[7], P.vLife, P.vBits,
            gMe.pos[0], gMe.pos[1], gMe.pos[2], gMe.obj ? R16(gMe.obj + 0x142) : 0);
        P.vLife = gMe.life; P.vBits = gMe.bits[0];
    }
    if (lyingAwake) { if (P.vDownSince < 0) P.vDownSince = gFrame; if (gFrame - P.vDownSince >= 240 && (gFrame - P.vDownSince) % 10 < 3) gPad.buttons |= 0x4000; return; }
    P.vDownSince = -1;
    // (a quarter turn to the left when the shooter stands still 7 m away or more for 3 s and the last turn was 6 s ago or more: his next
    // spot may lie on another side of it; its yaw and the yaw it turns to written, the direct aim's way, doll_brain.cpp DirectFacing)
    const Player* sh = nullptr;
    for (const Player& p : gPlayers) if (!p.local && p.obj && p.team != 0xFE && p.name.compare(0, 7, "VIRTUAL") != 0) sh = &p;
    static long long farSince = -1, turnedAt = -100000;
    const bool farStill = sh && sh->alive && Dist2D(sh->pos, gMe.pos) >= 7000.f && std::hypot(sh->vel[0], sh->vel[1]) < 2.f;
    if (!farStill) farSince = -1; else if (farSince < 0) farSince = gFrame;
    if (!prone && farSince >= 0 && gFrame - farSince >= 180 && gFrame - turnedAt >= 360 && (st & 0xFFFF) == 3 && gReact52.at < 0 && gMe.obj) {
        const int y = (R16(gMe.obj + 0x142) + 0x4000) & 0xFFFF;
        W16(gMe.obj + 0x142, y); W16(gMe.obj + 0x14A, y);
        turnedAt = gFrame; farSince = -1;
        Log("probe54: victim: a quarter turn, its yaw now %04x (the shooter %.1f m away, still)", y, Dist2D(sh->pos, gMe.pos) / 1000.f);
    }
    if (!prone || gReact52.at >= 0) return;
    const long long u = gFrame - P.vAt;                  // (prone: Cross, then the stick 8 frames once crouched; again if it stood up)
    if (P.vStage < 3 && lying) { P.vStage = 3; Log("probe54: victim lying (state %x)", gMe.bits[0]); }
    if (P.vStage >= 3 && !lying) { P.vStage = 0; P.vAt = gFrame; }
    if (P.vStage == 0) { if (u >= 20 && u < 23) gPad.buttons |= 0x4000; if (u >= 23) { P.vStage = 1; P.vAt = gFrame; } }
    else if (P.vStage == 1) { if (crouched && u >= 30) { P.vStage = 2; P.vAt = gFrame; } else if (u > 120) { P.vStage = 0; P.vAt = gFrame; } }
    else if (P.vStage == 2) { if (u < 8) { const float f = Facing(); Steer(std::sin(f), std::cos(f), 1.f); } if (u > 120) { P.vStage = 0; P.vAt = gFrame; } }
}
void Shooter54Frame() {
    P54& P = gP54;
    if (P.trials.empty()) {
        const char* e = getenv("MGO1_DOLL_PROBE_TRIALS");
        const std::string v = e && *e ? e : "f0,f180,f90,f270,m0@2,m180@2,m0@5,m180@5,m0@8,m0@11,M0@2,M180@2,p0,p180";
        for (size_t a = 0; a < v.size();) {
            size_t c = v.find(',', a); if (c == std::string::npos) c = v.size();
            const std::string s = v.substr(a, c - a); a = c + 1;
            if (s.empty()) continue;
            Trial54 t; t.w = s[0]; t.text = s;
            if (t.w == 'k' && s.size() > 1) t.w2 = s[1];
            t.ang = (float)atof(s.c_str() + (t.w == 'k' ? 2 : 1));
            const size_t at = s.find('@');
            t.dist = at != std::string::npos ? (float)atof(s.c_str() + at + 1) * 1000.f : t.w == 'f' ? 2500.f : t.w == 'p' || t.w == 'k' ? 550.f : 3000.f;
            P.trials.push_back(t);
        }
        Log("probe54: the shooter (doll %d): %d trials (%s)", gSet.number, (int)P.trials.size(), v.c_str());
    }
    if (gRoom.phase != 2 || gFrame < 4800) { P.at = gFrame; return; }
    const bool newLife = P.life < 0 || gFrame - P.life > 3;
    P.life = gFrame;
    if (P.idx >= P.trials.size()) { if (gFrame % 600 == 0) Log("probe54: done (%d shots)", P.shots); Steer(0, 0, 0); return; }
    const Trial54& T = P.trials[P.idx];
    const int code = T.w == 'f' ? 0x09 : T.w == 'm' || T.w == 'M' ? 0x0E : 0;
    const int code2 = T.w == 'k' ? (T.w2 == 'f' ? 0x09 : 0x0E) : 0;   // (k: the weapon after the kick, given with the life)
    const Inventory& v = Inv();
    const int give = code ? code : code2;
    if (give && (newLife || (v.ok && v.rounds[give] < 0 && gFrame - P.giveAt > 120))) { if (!v.ok || v.rounds[give] < 0) { if (Give46(give, give == 0x09 ? 999 : 60)) P.giveAt = gFrame; } }
    if (newLife && P.phase > 0) { Log("probe54: %s: a new life: the trial again", T.text.c_str()); P.phase = 0; P.at = gFrame; P.spotHave = false; StopPath(); }
    const long long t = gFrame - P.at;
    auto next = [&](const char* why) { Log("probe54: %s: %s", T.text.c_str(), why); ++P.idx; P.phase = 0; P.at = gFrame; P.tries = 0; P.spotHave = false; P.second = 0; StopPath(); Steer(0, 0, 0); };
    const Player* o = Victim54();
    if (!o || !o->alive) { Steer(0, 0, 0); if (P.phase >= 1 && P.phase <= 2) { P.phase = 0; P.at = gFrame; P.spotHave = false; StopPath(); } if (t > 3600) next("no victim alive in 60 s"); return; }
    const float vf = FacingOf(o->obj);
    const int vyaw = R16(o->obj + 0x142);
    const float rel = Wrap(AngleTo(o->pos, gMe.pos) - vf) * 57.2958f;
    const float dist = Dist2D(gMe.pos, o->pos);
    const int mot = (int)(R16(o->obj + 0x60) & 0x3FF);
    if (P.phase >= 3 && mot != P.lastMot) { char b[24]; snprintf(b, sizeof b, " %x@%lld", mot, gFrame - P.firedAt); if (P.mots.size() < 300) P.mots += b; P.lastMot = mot; }
    float m[16] = {};
    const bool view = ps2world::ViewMatrix(m);
    const float gy = MapReady() ? gMap->nav->GroundBelow(o->pos, 2500.f) : -1e30f;
    const float h = gy > -1e29f ? std::max(250.f, o->pos[1] - gy) : 800.f;
    const float aim[3] = {o->pos[0], o->pos[1] + 0.45f * h, o->pos[2]};
    float aimErr[2] = {0, 0};
    auto aimStep = [&]() -> bool {                       // (the first person view's sticks toward his chest, probe 46's way: true when on it)
        if (!view) return false;
        const float yaw = std::atan2(m[8], m[10]), pitch = std::asin(std::max(-1.f, std::min(1.f, m[9])));
        const float tx = aim[0] - m[12], tz = aim[2] - m[14], ty = aim[1] - m[13];
        const float ye = Wrap(std::atan2(tx, tz) - yaw), pe = std::atan2(ty, std::sqrt(tx * tx + tz * tz)) - pitch;
        auto s = [](float deg) { const float a = std::fabs(deg); const float x = a < 0.2f ? 0.f : a < 1.5f ? (gFrame % 8 < 2 ? 0.46f : 0.f) : std::min(1.f, 0.36f + a * 0.045f); return deg < 0 ? -x : x; };
        const float tol = std::max(0.5f, std::atan2(100.f, std::max(1.f, dist)) * 57.2958f);
        aimErr[0] = ye * 57.2958f; aimErr[1] = pe * 57.2958f;
        const bool on = std::fabs(ye) * 57.2958f < tol && std::fabs(pe) * 57.2958f < tol;
        if (!on) { gPad.sticks[2] = (uint8_t)(0x80 + (int)(-s(ye * 57.2958f) * 127)); gPad.sticks[3] = (uint8_t)(0x80 - (int)(s(pe * 57.2958f) * 127)); }
        return on;
    };
    switch (P.phase) {
    case 0: {                                            // its weapon (empty hands for the blows), then a spot at the angle and distance
        if (v.ok && v.weapon != code) { if (!ProbeWindowTo(0, code) && t > 900) next("the weapon not taken"); return; }
        if ((o->bits[0] & 0x1E000008u) || Reacts54(mot)) { Steer(0, 0, 0); P.at = gFrame; return; }   // (he lies or reacts: his getting up first)
        if (!MapReady()) return;
        bool found = false;
        for (int k = 0; k < 13 * 3 && !found; ++k) {     // (the angle, then 10 to 60 degrees either side; the distance, 0.8 and 1.25 of it)
            const int ka = k % 13, kd = k / 13;
            const float a = vf + (T.ang + (ka == 0 ? 0.f : (ka & 1 ? 1.f : -1.f) * 10.f * (float)((ka + 1) / 2))) / 57.2958f;
            const float want = std::max(T.dist, T.w == 'p' || T.w == 'k' ? 1500.f : 0.f) * (kd == 0 ? 1.f : kd == 1 ? 0.8f : 1.25f);
            const float c[3] = {o->pos[0] + std::sin(a) * want, o->pos[1] - 800.f, o->pos[2] + std::cos(a) * want};
            float q[3];
            if (!gMap->nav->Nearest(c, 400.f, q) || std::fabs(Dist2D(q, o->pos) - want) > 500.f || std::fabs(q[1] + 800.f - o->pos[1]) > 600.f) continue;
            const float e2[3] = {q[0], q[1] + 1560.f, q[2]};
            if (!LineClear(e2, aim)) continue;
            memcpy(P.spot, q, sizeof P.spot); found = true; P.spotAng = Wrap(a - vf) * 57.2958f;
        }
        if (!found) { if (++P.tries > 4) next("no spot at that angle and distance"); else { P.at = gFrame; Log("probe54: %s: no spot from here (%d): away from him for his turn", T.text.c_str(), P.tries); P.phase = 5; P.spotHave = false; } return; }
        memcpy(P.spotFor, o->pos, sizeof P.spotFor); P.spotHave = true;
        PlanTo(P.spot);
        P.phase = 1; P.at = gFrame;
        return;
    }
    case 1:                                              // to the spot
        if (Dist2D(P.spotFor, o->pos) > 1000.f) { P.phase = 0; P.at = gFrame; StopPath(); return; }
        if (Dist2D(gMe.pos, P.spot) < 350.f) { StopPath(); Steer(0, 0, 0); P.phase = 2; P.at = gFrame; P.onSince = -1; return; }
        if (t > 2400) { if (++P.tries > 4) next("the spot not reached"); else { P.phase = 0; P.at = gFrame; } return; }
        if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
        FollowPath();
        return;
    case 2: {                                            // its aim on his chest in the first person view (the blows: in to 0.55 m facing him), still 12 frames
        if (std::fabs(Wrap((rel - P.spotAng) / 57.2958f)) * 57.2958f > 35.f) { if (++P.tries > 6) next("he turned away from the angle"); else { P.phase = 0; P.at = gFrame; } return; }
        if (T.w == 'p' || T.w == 'k') {
            const float goal = AngleTo(gMe.pos, o->pos), err = Wrap(goal - Facing());
            if (dist > T.dist + 60.f && t < 600) { Steer(std::sin(goal), std::cos(goal), 0.7f); P.onSince = -1; return; }   // (Go31's push near him)
            if (std::fabs(err) > 8.f / 57.2958f && t < 720) { Steer(0, 0, 0); if (gFrame % 6 < 2) Steer(std::sin(goal), std::cos(goal), 1.f); P.onSince = -1; return; }   // (Face31's short pushes)
            Steer(0, 0, 0);
            if (gMe.bits[0] != 3 && t < 900) { P.onSince = -1; return; }   // (standing still first: a press while it moves is another move)
            if (P.onSince < 0) P.onSince = gFrame;
        } else {
            gPad.buttons |= 0x0800;
            if (T.w != 'f') { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }   // (the M37 readied, Square light, as the brain's pull: doll_brain.cpp)
            if (!view || t < 20) return;
            if (!aimStep()) P.onSince = -1; else if (P.onSince < 0) P.onSince = gFrame;
            if (t > 900) { Log("probe54: %s: the aim not settled (%.2f %.2f deg)", T.text.c_str(), aimErr[0], aimErr[1]); P.phase = 0; P.at = gFrame; return; }
        }
        if (P.onSince < 0 || gFrame - P.onSince < 12) return;
        // (M: his full life first, so that he lives through both shots (100 and 75): a shot of the M37 to end this life, then his next one)
        if (T.w == 'M' && o->life < 200 && o->life > 0) { Log("probe54: %s: a shot to end his life first (%d)", T.text.c_str(), o->life); P.second = -1; P.phase = 3; P.at = gFrame; P.firedAt = gFrame; P.mots.clear(); P.lastMot = -1; P.lieSince = -1; return; }
        ++P.shots;
        Log("probe54: SHOT %s (%d): %s at %.2f m, %.0f degrees from his facing; me at %.0f %.0f %.0f facing %.1f (yaw %04x); he at %.0f %.0f %.0f, his copy's facing %.1f (+0x142 %04x), state %x %x, life %d, motion %x",
            T.text.c_str(), P.shots, T.w == 'f' ? "the flamethrower" : T.w == 'p' ? "three Circles, empty hands" : T.w == 'k' ? "three Circles, empty hands, then a hit while he lies" : "the M37", dist / 1000.f, rel, gMe.pos[0], gMe.pos[1], gMe.pos[2], Facing() * 57.2958f,
            R16(gMe.obj + 0x142), o->pos[0], o->pos[1], o->pos[2], vf * 57.2958f, vyaw, o->bits[0], o->bits[2], o->life, mot);
        Log("probe54: his copy's +0x108 %d %d %d (0x407CB0's radius the last), +0x14A %04x", R16s(o->obj + 0x108), R16s(o->obj + 0x10A), R16s(o->obj + 0x10C), R16(o->obj + 0x14A));
        P.phase = 3; P.at = gFrame; P.firedAt = gFrame; P.mots.clear(); P.lastMot = -1; P.second = 0; P.lieSince = -1;
        return;
    }
    case 3: {                                            // the shot: the flame held to a reaction, the M37's trigger (M: again once he lies), the Circles
        const bool reacting = Reacts54(mot) || (o->bits[0] & 0x1E000000u) != 0;
        if (T.w == 'f') {
            if (P.second == 0 && (reacting || t >= 150)) { P.second = 1; Log("probe54: %s: the flame let go %lld frames after its start (%s)", T.text.c_str(), t, reacting ? "his copy reacts" : "no reaction"); }
            if (P.second == 0) { gPad.buttons |= 0x8800; gPad.pressures[11] = 0xFF; }
        } else if (T.w == 'm' || T.w == 'M') {
            gPad.buttons |= 0x0800;
            if (t < 8) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }          // (the pull 8 frames, then let go: the pump; readied again from 40)
            else if (t >= 40 && T.w == 'M' && P.second == 0) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
            if (T.w == 'M' && P.second == -1) { if (t >= 120) { P.second = 0; P.phase = 0; P.at = gFrame; P.spotHave = false; } return; }   // (the life's end shot: then a spot again)
            if (T.w == 'M' && P.second == 0) {
                const bool down = (o->bits[0] & 0x18000008u) != 0;
                if (down && P.lieSince < 0) P.lieSince = gFrame;
                const bool on = t >= 8 && aimStep();             // (the view after him where he fell)
                if (P.lieSince >= 0 && t >= 100 && gFrame - P.lieSince >= 20 && (on || gFrame - P.lieSince >= 90)) {   // (the pump's 94 frames; lying 20 frames, the aim on his body)
                    P.second = 1; P.firedAt = gFrame;
                    Log("probe54: SHOT %s second: at %.2f m, he lies (state %x %x, motion %x, life %d), his copy's facing %.1f (+0x142 %04x)", T.text.c_str(), dist / 1000.f, o->bits[0], o->bits[2], mot, o->life,
                        vf * 57.2958f, vyaw);
                }
                if (t > 400 && P.second == 0) { P.second = 2; Log("probe54: %s: he never lay (state %x, motion %x)", T.text.c_str(), o->bits[0], mot); }
            }
            if (T.w == 'M' && P.second == 1 && gFrame - P.firedAt < 8) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }
        } else if (T.w == 'p') {
            if (t < 3 || (t >= 12 && t < 15) || (t >= 24 && t < 27)) gPad.buttons |= 0x2000;
        } else if (T.w == 'k') {
            // (the punches and the kick; once he lies down after it (0x1C) the weapon taken and fired at him while he lies: a hit on a man
            // already down, the kinds 4 and 8; the flame held to a reaction, the M37 readied 12 frames then pulled)
            if (P.second == 0 && (t < 3 || (t >= 12 && t < 15) || (t >= 24 && t < 27))) gPad.buttons |= 0x2000;
            const bool down = (o->bits[0] & 0x18000000u) != 0;
            if (P.second == 0 && down && t >= 30) { P.second = 1; P.lieSince = gFrame; P.onSince = -1; Log("probe54: %s: he lies (state %x, motion %x), the weapon %x taken", T.text.c_str(), o->bits[0], mot, code2); }
            if (P.second == 0 && t > 300) { P.second = 9; Log("probe54: %s: he never lay down (state %x)", T.text.c_str(), o->bits[0]); }
            if (P.second == 1) {
                if (v.ok && v.weapon != code2) { ProbeWindowTo(0, code2); if (gFrame - P.lieSince > 200) { P.second = 9; Log("probe54: %s: the weapon not in hand in time", T.text.c_str()); } }
                else {
                    gPad.buttons |= 0x0800;
                    if (code2 == 0x0E) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0x3F; }
                    const bool on = aimStep();
                    if ((on && P.onSince >= 0 && gFrame - P.onSince >= 12) || gFrame - P.lieSince > 160) {
                        P.second = 2; P.firedAt = gFrame;
                        Log("probe54: SHOT %s second: the %s at %.2f m, he lies (state %x %x, motion %x, life %d), his copy's facing %.1f (+0x142 %04x, +0x14A %04x)", T.text.c_str(),
                            code2 == 0x09 ? "flamethrower" : "M37", dist / 1000.f, o->bits[0], o->bits[2], mot, o->life, vf * 57.2958f, vyaw, R16(o->obj + 0x14A));
                    }
                    if (on && P.onSince < 0) P.onSince = gFrame; else if (!on) P.onSince = -1;
                }
            }
            if (P.second == 2) {
                const long long u = gFrame - P.firedAt;
                gPad.buttons |= 0x0800;
                if (code2 == 0x09) { if (u < 150 && !Reacts54(mot)) { gPad.buttons |= 0x8800; gPad.pressures[11] = 0xFF; } }
                else if (u < 8) { gPad.buttons |= 0x8000; gPad.pressures[11] = 0xFF; }
            }
        }
        if (t % 10 == 0 && t <= 300) Log("probe54: %s +%lld: his copy state %x %x, motion %x, life %d, stun %d, facing %.1f (+0x142 %04x), %.2f m", T.text.c_str(), t, o->bits[0], o->bits[2], mot, o->life, o->stun,
                                         vf * 57.2958f, vyaw, dist / 1000.f);
        const long long len = T.w == 'M' ? 520 : T.w == 'f' ? 260 : 200;
        const bool kOver = (P.second == 2 && gFrame - P.firedAt >= 200) || P.second == 9;   // (k: 200 frames after its second hit)
        if (T.w == 'k' ? !kOver : t < len) return;
        Log("probe54: RESULT %s: his copy's motions%s; his life %d, state %x", T.text.c_str(), P.mots.c_str(), o->life, o->bits[0]);
        P.phase = 4; P.at = gFrame;
        return;
    }
    case 4:                                              // his getting up and 2 s, then the next trial
        Steer(0, 0, 0);
        if ((o->bits[0] & 0x1E000008u) || Reacts54(mot)) { P.upSince = -1; if (t > 1800) next("over (he stays down)"); return; }
        if (P.upSince < 0) P.upSince = gFrame;
        if (gFrame - P.upSince >= 120) next("over");
        return;
    case 5: {                                            // no spot from here: 8 m away from him, still, until his quarter turn (20 s at most)
        if (P.yawWas < 0) P.yawWas = vyaw;
        const int turned = std::abs((int16_t)(uint16_t)(vyaw - P.yawWas));
        if (turned > 0x2AAA || t > 1200) { Log("probe54: %s: %s (his yaw %04x)", T.text.c_str(), turned > 0x2AAA ? "he turned" : "20 s", vyaw); P.yawWas = -1; StopPath(); Steer(0, 0, 0); P.phase = 0; P.at = gFrame; return; }
        if (dist >= 8000.f) { StopPath(); Steer(0, 0, 0); return; }
        if (!P.spotHave && MapReady()) {
            for (int k = 0; k < 12 && !P.spotHave; ++k) {
                const float a = AngleTo(o->pos, gMe.pos) + (k & 1 ? 1.f : -1.f) * 0.5236f * (float)((k + 1) / 2);
                const float c[3] = {o->pos[0] + std::sin(a) * 9500.f, o->pos[1] - 800.f, o->pos[2] + std::cos(a) * 9500.f};
                float q[3];
                if (gMap->nav->Nearest(c, 1500.f, q) && Dist2D(q, o->pos) >= 8000.f) { memcpy(P.spot, q, sizeof P.spot); P.spotHave = true; PlanTo(P.spot); }
            }
            if (!P.spotHave) { Steer(0, 0, 0); return; }
        }
        if (!Following() || gFrame % 120 == 0) PlanTo(P.spot);
        FollowPath();
        return;
    }
    }
}
void Probe54Frame() {
    if (gSet.number <= 1) Shooter54Frame(); else Victim54Frame();
}

bool ProbeFrame(bool mapPage) {
    if (!gProbe) return false;
    if (gProbe == 54 && mapPage && MapReady()) { TickNow52(); Reaction52(GateNow52()); }   // ((v150-host beta 25, reactfix) the victim's reactions, every frame)
    if (mapPage && MapReady()) StaminaWatch();           // (v150-host beta 24, stamina: MGO1_DOLL_PROBE_STAMINA)
    if (gProbe == 52 && mapPage && MapReady()) Watch52();   // (v150-host beta 25, bookhold: the book's gate, every frame of the map)
    if (gProbe == 5) { if (mapPage && MapReady()) Probe5Frame(); return true; }
    if (gProbe == 6) { if (mapPage && MapReady()) Probe6Frame(); return true; }
    // (31: its logs every frame of the world, dead too; knocked down or grabbed after a free frame of the
    // life, the pad left alone: never the brain's frame)
    if (gProbe == 31 && mapPage && MapReady()) {
        static bool free31 = false;
        if (!gMe.alive) free31 = false; else if (!gMe.frozen) free31 = true;
        if (!gMe.alive) gDeadAt31 = gFrame;
        Watch31(); Actors31();
        if (gP31.steps.find_first_of("qhv") != std::string::npos) Coll31(gP31.role == 2 ? "victim" : "actor");
        if (VictimBrain31()) return false;                // ((v141 beta 9) the choke step's victim: its own brain)
        if (gMe.alive && gMe.frozen && free31) { gControl = true; return true; }
    }
    if (gProbe == 51 && mapPage && MapReady()) Copies51();   // ((v150-host beta 25, rolltime) the other players' copies that roll, dead or alive)
    // 7 and 8 play as a doll does out of the world (the lobby, the spawn, the menus)
    if (gProbe >= 7 && (!mapPage || !gMe.alive || gMe.frozen || !MapReady())) return false;
    if (!mapPage || !gMe.alive || gMe.frozen || !MapReady()) return true;
    gControl = true;
    if (StaminaPose()) return true;                      // (v150-host beta 24, stamina: MGO1_DOLL_PROBE_STAMINA_POSE)
    switch (gProbe) {
    case 2: Probe2Frame(); break;
    case 3: Probe3Frame(); break;
    case 4: Probe4Frame(); break;
    case 7: Probe7Frame(); break;
    case 8: Probe8Frame(); break;
    case 9: Probe9Frame(); break;
    case 10: Probe10Frame(); break;
    case 11: Probe11Frame(); break;
    case 12: Probe12Frame(); break;
    case 13: Probe13Frame(); break;
    case 14: Probe14Frame(); break;
    case 15: Probe15Frame(); break;
    case 16: Probe16Frame(); break;
    case 17: Probe17Frame(); break;
    case 18: Probe18Frame(); break;
    case 19: Probe19Frame(); break;
    case 20: Probe20Frame(); break;
    case 21: Probe21Frame(); break;
    case 22: Probe22Frame(); break;
    case 23: Probe23Frame(); break;
    case 24: Probe24Frame(); break;
    case 26: Probe26Frame(); break;
    case 27: Probe27Frame(); break;
    case 28: Probe28Frame(); break;
    case 29: Probe29Frame(); break;
    case 30: Probe30Frame(); break;
    case 31: Probe31Frame(); break;
    case 32: Probe32Frame(); break;
    case 40: Probe40Frame(); break;
    case 41: Probe41Frame(); break;
    case 43: Probe43Frame(); break;
    case 44: Probe44Frame(); break;
    case 45: Probe45Frame(); break;                      // (v150-host beta 21, geom: the stances)
    case 46: Probe46Frame(); break;                      // (v150-host beta 22, heavy: the RPG-7 and the flamethrower)
    case 47: Probe47Frame(); break;                      // (v150-host beta 22, pickup: the special items)
    case 48: Probe48Frame(); break;                      // (v150-host beta 25, food: the food lying, eaten)
    case 49: Probe49Frame(); break;                      // (v150-host beta 22, support: the claymore taken crawling)
    case 50: Probe50Frame(); break;                      // (v150-host beta 25, aimcost: the moves while it aims)
    case 51: Probe51Frame(); break;                      // (v150-host beta 25, rolltime: a player's roll)
    case 52: Probe52Frame(); break;                      // (v150-host beta 25, bookhold: when a player's game lets him read a book)
    case 53: Probe53Frame(); break;                      // ((v150-host beta 25, copyleash) laps: a real client's runs, his host copy logged)
    case 54: Probe54Frame(); break;                      // ((v150-host beta 25, reactfix) the reaction kind a shooter's game tells)
    default: Probe1Frame(); break;
    }
    return true;
}

}  // namespace in
}  // namespace doll
