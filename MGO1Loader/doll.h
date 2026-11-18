// The PLAYABLE DOLL (docs/PLAYABLE_DOLL.md): a player run by the server. The game itself runs as a
// joiner (the dedicated host's executable, headless); the doll reads what the game knows (the
// players, their teams, the objectives, the map's collision) and plays it with the pad, as a person
// would: no position is ever written, no packet made up. (v150-host beta 18) With the direct aim (MGO1_DOLL_AIM=direct, off
// by default) its own facing and its own view's angles are written in its own game, nothing else (doll_aim.h).
//
// MGO1_DOLL=<folder>: status.json written there every second (what the doll does), `command`
// read there (orders: difficulty, objective, a target, hide, the team), map.nav cache files.
#pragma once
#include <cstdint>
#include <string>

namespace doll {

struct Pad {
    uint32_t buttons = 0;          // held (the pad's bits: 0x10 up .. 0x8000 square)
    uint8_t sticks[4] = {0x80, 0x80, 0x80, 0x80};   // RX RY LX LY
    uint8_t pressures[12] = {};    // the pressure of the button of bit 4 + i: up right down left L2 R2 L1 R1 triangle circle cross square
};

bool Enabled();                    // MGO1_DOLL set
void Init();                       // once, at the start (reads the environment)
void Frame(long long frame);       // every VBlank, after the game's frame
bool Control();                    // the doll drives the pad now (in a match, alive or choosing)
const Pad& CurrentPad();           // the pad for this frame
void Log(const char* fmt, ...);    // the doll's lines in the run's log (no log-in data ever)

}  // namespace doll
