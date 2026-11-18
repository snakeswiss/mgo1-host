// The PLAYABLE DOLL's direct aim's turn (MGO1Loader/doll_aim.h, v150-host beta 18): its rate is the virtual doll's kLevel turn
// for every level, it settles on the point without overshoot and stays, the difficulty's own error (an offset of the point) is
// kept, the yaw wraps the short way, the game's s16 angles go and come back. Build:
//   g++ -O1 -std=c++17 -I MGO1Loader tests/doll_aim_test.cpp -o /tmp/doll_aim_test && /tmp/doll_aim_test
#include "doll_aim.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace doll::aim;
static int gFails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++gFails; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char** argv) {
    const float d = kPi / 180.f;
    // 1. the rate: each level's degrees a frame, read from vdoll_body.cpp's kLevel itself when the file is there
    {
        std::ifstream f(argc > 1 ? argv[1] : "MGO1Loader/vdoll_body.cpp");
        std::stringstream ss; ss << f.rdbuf(); const std::string s = ss.str();
        const size_t at = s.find("const Level kLevel[10] = {");
        CHECK(at != std::string::npos, "vdoll_body.cpp's kLevel not found");
        size_t p = s.find('{', at);   // (the array's own brace)
        for (int lv = 1; lv <= 10 && at != std::string::npos; ++lv) {
            p = s.find('{', p + 1);
            int react; float err, sees, field, head, turn;
            CHECK(sscanf(s.c_str() + p, "{%d, %ff, %f, %f, %ff, %ff", &react, &err, &sees, &field, &head, &turn) == 6, "kLevel row %d not read", lv);
            CHECK(std::fabs(turn - RateDeg(lv)) < 1e-4f, "level %d: kLevel turns %.1f, doll_aim.h %.1f", lv, turn, RateDeg(lv));
        }
    }
    // 2. a turn of 120 degrees at each level: ceil(120 / rate) frames, each step the rate, the last what is left, then still
    for (int lv = 1; lv <= 10; ++lv) {
        float cur = 0.f; const float want = 120.f * d, r = Rate(lv);
        int frames = 0;
        while (std::fabs(Wrap(want - cur)) > 1e-5f && frames < 1000) {
            const float before = cur; cur = StepYaw(cur, want, r); ++frames;
            CHECK(std::fabs(Wrap(cur - before)) <= r + 1e-5f, "level %d: a step of %.3f deg over the rate %.1f", lv, Wrap(cur - before) / d, RateDeg(lv));
            CHECK(Wrap(want - cur) >= -1e-5f, "level %d: overshoot at frame %d", lv, frames);
        }
        CHECK(frames == (int)std::ceil(120.f / RateDeg(lv) - 1e-4f), "level %d: %d frames for 120 degrees (%.0f expected)", lv, frames, std::ceil(120.f / RateDeg(lv)));
        for (int k = 0; k < 10; ++k) cur = StepYaw(cur, want, r);
        CHECK(std::fabs(Wrap(want - cur)) < 1e-5f, "level %d: did not stay on the point", lv);
    }
    // 3. the settle on a moving point: it follows a point moving under the rate exactly, lags one moving over it by rate a frame
    {
        float cur = 0.f, want = 10.f * d; const float r = Rate(10);
        for (int k = 0; k < 20; ++k) { want += 5.f * d; cur = StepYaw(cur, want, r); }
        CHECK(std::fabs(Wrap(want - cur)) < 1e-4f, "level 10 lost a point moving 5 degrees a frame (%.2f off)", Wrap(want - cur) / d);
        cur = 0.f; want = 0.f; const float r5 = Rate(5);
        for (int k = 0; k < 10; ++k) { want += 10.f * d; cur = StepYaw(cur, want, r5); }
        CHECK(std::fabs(Wrap(want - cur) - 20.f * d) < 1e-3f, "level 5 behind a point moving 10 degrees a frame: %.2f (20 expected)", Wrap(want - cur) / d);
    }
    // 4. the error: the point offset by the difficulty's error is where the turn settles, not on the target itself
    {
        const float target = 30.f * d, err = 0.6f * d; float cur = 0.f;
        for (int k = 0; k < 20; ++k) cur = StepYaw(cur, target + err, Rate(5));
        CHECK(std::fabs(Wrap(cur - target) - err) < 1e-5f, "the error not kept: %.3f deg off (0.6 expected)", Wrap(cur - target) / d);
    }
    // 5. the short way round, the pitch unwrapped
    {
        float cur = 170.f * d; cur = StepYaw(cur, -170.f * d, Rate(10));
        CHECK(std::fabs(Wrap(cur - (-170.f * d))) < 1e-4f, "170 to -170 not the short way: %.2f", cur / d);
        float p = 0.f; for (int k = 0; k < 5; ++k) p = StepPitch(p, -40.f * d, Rate(4));
        CHECK(std::fabs(p + 30.f * d) < 1e-4f, "the pitch at level 4 after 5 frames: %.2f (-30 expected)", p / d);
    }
    // 6. the gun's lag behind a written facing (probe 44, paim_p44c: a 30 degree turn written, the pull d frames after, the bullet
    // 3 frames after the pull): the model at d + 4 frames within 1.5 degrees of what was measured and never under it (the shot waits)
    {
        const int delay[] = {0, 2, 4, 6, 8, 12, 20};
        const float measured[] = {9.59f, 5.42f, 2.92f, 1.77f, 0.88f, 0.39f, 0.03f};
        for (int i = 0; i < 7; ++i) {
            const float m = GunLagAfter(30.f, delay[i] + 4);
            CHECK(m >= measured[i] - 0.05f && m - measured[i] < 1.5f, "the gun's lag %d frames after the turn: model %.2f, measured %.2f", delay[i] + 4, m, measured[i]);
        }
    }
    // 7. the third person's bullet height (probe 44: 3.37 degrees down): on flat ground (his ground 0, the muzzle 1.18 m up, his head's
    // top 1.75 m) it reaches him to about 17 m, never a floor up (his ground 3 m above) nor one down within that, one 2.4 m down at 35 m
    {
        CHECK(TpvReaches(1180.f, 5000.f, 0.f, 1750.f) && TpvReaches(1180.f, 15000.f, 0.f, 1750.f), "flat ground within 15 m not reached");
        CHECK(!TpvReaches(1180.f, 20000.f, 0.f, 1750.f), "flat ground at 20 m reached (the bullet is under 0.15 m there)");
        CHECK(!TpvReaches(1180.f, 8000.f, 3000.f, 4750.f), "a floor up reached");
        CHECK(!TpvReaches(1180.f, 8000.f, -2400.f, -650.f), "a floor down at 8 m reached");
        CHECK(TpvReaches(1180.f, 35000.f, -2400.f, -650.f), "a floor down at 35 m not reached (the bullet at %.0f)", TpvBulletY(1180.f, 35000.f));
    }
    // 8. the s16 angles
    for (float a : {-179.9f, -90.f, -0.01f, 0.f, 33.3f, 90.f, 179.9f}) {
        const float back = From16(To16(a * d));
        CHECK(std::fabs(Wrap(back - a * d)) < 2.f * kPi / 65536.f, "s16 round trip of %.2f deg: %.4f", a, back / d);
    }
    CHECK(To16(-90.f * d) == -16384, "-90 degrees is %d (the probe read -16383 for a facing of -89.99)", To16(-90.f * d));
    printf("doll_aim_test: %s (%d failures)\n", gFails ? "FAILED" : "passed", gFails);
    return gFails ? 1 : 0;
}
