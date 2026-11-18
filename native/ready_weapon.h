// The weapon readied without a shot, as MGS3 Master Collection's READY WEAPON (docs/MC_PC_INPUT.md): the PC's
// controls (the pad's L3 toggles it, READY WEAPON holds it, WEAPON fires, R3 reloads) as the PS2's Square pressure
// and R3. MGO1's own rules (US n003a): from a pressure of 0x11 the weapon is raised (0x311D30); let go, it fires
// unless the pressure was under 0x10 at the tick before and is now (0x416280; the automatic weapons, flag 0x800:
// under 0x10 now); R3 reloads only with Square up (the gun's start 0x41DC28, its L1 states 12 and 13: 0x417F10). A
// grenade is thrown when Square is let go, however slowly: only a gun is readied.
// Used by native/input.cpp and by the harness (tests/ps2world_test.cpp, SIM_PC_READY).
#pragma once
#include <cstdint>
#include <cstdio>

namespace ready {

constexpr uint8_t kRaised = 0x3F;      // a DualShock 2's light pressure: the weapon raised
constexpr uint8_t kLowered = 0x08;     // under 0x10: let go from it, the weapon comes down without a shot
constexpr int kLowerFrames = 4;        // VBlanks at kLowered (a tick reads it at every frame rate)
constexpr int kShotFrames = 2;         // the fire button let go: Square up (the shot), then raised again
constexpr int kDownMax = 60;           // VBlanks for the weapon to come down before R3 goes anyway
constexpr int kR3Frames = 4;           // R3 held for the reload
constexpr int kAfterFrames = 6;        // Square kept up after R3: the reload starts before the weapon is raised

struct In {
    bool match = false;     // a map's page loaded (false: the lobby)
    bool toggle = false;    // the pad's L3 held: each press readies or lowers the weapon
    bool hold = false;      // READY WEAPON held
    bool fire = false;      // WEAPON or the pad's Square held
    bool r3 = false;        // R3 held (the pad's, CAMERA ANGLE)
    bool windows = false;   // L2 or R2 held (the item or weapon window)
    bool menu = false;      // the in-game menu open: the weapon let down, the toggle off
    int aimed = -1;         // the game: the local player's weapon raised or coming down (his flag 0x2E); -1 unknown
    int reloadable = -1;    // the game: his gun's magazine can take rounds (0x4079C8's rule); -1 unknown
    int gun = -1;           // the game: he holds a gun (weapons 5 to 17; a grenade is thrown however let go); -1 unknown
};
struct Out {
    bool own = false;       // Square and R3 set here (false: the controls' own)
    bool square = false;
    uint8_t pressure = 0;
    bool r3 = false;
};

class Weapon {
public:
    // once a VBlank
    Out Step(const In& in) {
        event_ = nullptr;
        if (in.toggle && !togglePrev_ && in.match) {
            if (in.gun == 0) event_ = "L3 with no gun in hand: nothing readied";
            else { toggled_ = !toggled_; event_ = toggled_ ? "the weapon readied (L3)" : "the weapon lowered (L3)"; }
        }
        togglePrev_ = in.toggle;
        if (!in.match || in.menu || in.gun == 0) toggled_ = false;
        const bool want = (toggled_ || in.hold) && in.match && !in.windows && !in.menu && in.gun != 0;
        const bool r3Edge = in.r3 && !r3Prev_;
        r3Prev_ = in.r3;
        if (!in.r3) r3Latch_ = false;
        if (phase_ >= kReloadLower) {
            if (!in.match || in.menu || (in.fire && want)) phase_ = kIdle;   // the match left, the menu, or fired: given up
            else return Reload(in);
        }
        Out o;
        if (want) {
            o.own = true;
            if (phase_ == kLowering) phase_ = kIdle;              // readied again while coming down
            if (r3Edge && !r3Latch_ && !in.fire) r3Pending_ = true;   // (pressed just after a shot: taken after it)
            if (in.fire) { o.square = true; o.pressure = 0xff; firePrev_ = true; raised_ = false; }
            else if (firePrev_) { firePrev_ = false; gap_ = kShotFrames; }   // the fire button let go: the shot
            else if (gap_ > 0) --gap_;
            else if (r3Pending_ && in.reloadable == 1) {
                r3Pending_ = false; phase_ = kReloadLower; left_ = kLowerFrames; raised_ = false;
                return Reload(in);
            } else { r3Pending_ = false; o.square = true; o.pressure = kRaised; raised_ = true; }
            o.r3 = in.r3 && !r3Latch_ && !r3Pending_;
            return o;
        }
        if (raised_ && in.match) { phase_ = kLowering; left_ = kLowerFrames; }   // let down from the light pressure
        raised_ = false; firePrev_ = false; gap_ = 0; r3Pending_ = false;
        if (phase_ == kLowering) {
            if (in.fire) { phase_ = kIdle; return o; }   // the fire button: the controls' own Square from here
            o.own = true; o.square = true; o.pressure = kLowered; o.r3 = in.r3;
            if (--left_ <= 0) phase_ = kIdle;
        }
        return o;
    }
    const char* Event() const { return event_; }   // what this VBlank changed, for the log (null: nothing)

private:
    enum { kIdle, kLowering, kReloadLower, kReloadDown, kReloadR3, kReloadAfter };
    // R3 with the weapon readied: let down without a shot, R3 once the game has it down (MGO1 reloads then, by its
    // rules), Square up a few VBlanks more, then readied again by Step if it still is
    Out Reload(const In& in) {
        Out o; o.own = true;
        switch (phase_) {
        case kReloadLower:
            o.square = true; o.pressure = kLowered;
            if (--left_ <= 0) { phase_ = kReloadDown; waited_ = 0; down_ = 0; }
            break;
        case kReloadDown:
            ++waited_;
            down_ = in.aimed == 0 ? down_ + 1 : 0;
            if (down_ >= 2 || waited_ >= kDownMax) { phase_ = kReloadR3; left_ = kR3Frames; downAt_ = waited_; }
            break;
        case kReloadR3:
            o.r3 = true;
            if (--left_ <= 0) { phase_ = kReloadAfter; left_ = kAfterFrames; }
            break;
        case kReloadAfter:
            if (--left_ <= 0) {
                phase_ = kIdle; r3Latch_ = in.r3;
                snprintf(text_, sizeof text_, "R3 with the weapon readied: let down, R3 %d VBlanks later%s", kLowerFrames + downAt_,
                         downAt_ >= kDownMax ? " (the game kept it raised)" : "");
                event_ = text_;
            }
            break;
        }
        return o;
    }
    bool toggled_ = false, togglePrev_ = false, firePrev_ = false, raised_ = false, r3Prev_ = false, r3Latch_ = false, r3Pending_ = false;
    int phase_ = kIdle, gap_ = 0, left_ = 0, waited_ = 0, down_ = 0, downAt_ = 0;
    const char* event_ = nullptr;
    char text_[96] = {};
};

}  // namespace ready
