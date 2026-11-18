// (v150-host beta 21) the virtual dolls and a claymore's sensor (MGO1Loader/vdoll_trap.h, docs/VIRTUAL_DOLL.md 29): the
// choice the host makes for each target in the sensor of its copy of a claymore, against the game's own question
// (is the target's owner this program's player, not crawling, not a ghost).
//   g++ -O1 -std=c++17 -I MGO1Loader tests/vdoll_trap_test.cpp -o vdoll_trap_test && ./vdoll_trap_test
// Before the fix (the switch off stands for it: the game's question asked unchanged) the claymore never goes off for a
// virtual doll; with it, it goes off for a living doll as for a real player in his own game, and for nobody else.
#include "vdoll_trap.h"
#include <cstdio>

static int gFail = 0, gPass = 0;
static void Check(bool ok, const char* what) {
    if (ok) ++gPass; else { ++gFail; printf("FAIL: %s\n", what); }
}

int main() {
    using namespace vtrap;
    // a dedicated host's game: its own player in seat 0 (a spectator: no object), virtual dolls in seats 1 and 2, a real
    // player's copy in seat 3; seat 4 empty
    Seat seats[16];
    seats[1].present = true; seats[1].doll = true; seats[1].id = 0x0A1B2C; seats[1].life = 200; seats[1].bits0 = 0x3;
    seats[2].present = true; seats[2].doll = true; seats[2].id = 0x0A1B40; seats[2].life = 200; seats[2].bits0 = 0x3;
    seats[3].present = true; seats[3].doll = false; seats[3].id = 0x0A1B77; seats[3].life = 200; seats[3].bits0 = 0x3;
    Own host;                                            // the host has no player of its own: its link is empty
    int seat = -9;

    // 1. the defect: with the game's question unchanged, a doll's copy in the sensor sets nothing off
    Check(!GoesOff(seats, 16, 0x0A1B2C, host, false, &seat) && seat == -1, "before: a living doll in the sensor sets nothing off");
    // 2. the fix: the same target sets the claymore off, the doll standing in
    Check(GoesOff(seats, 16, 0x0A1B2C, host, true, &seat) && seat == 1, "a living doll in the sensor sets the claymore off");
    Check(GoesOff(seats, 16, 0x0A1B40, host, true, &seat) && seat == 2, "each doll by its own id");
    // (the owner's id is 24 bits: the word's top byte is not part of it)
    Check(GoesOff(seats, 16, 0x7F0A1B2C, host, true, &seat) && seat == 1, "the id is the low 24 bits");
    // 3. a real player's copy: never by the host (his own game decides for him)
    Check(!GoesOff(seats, 16, 0x0A1B77, host, true, &seat) && seat == -1, "a real player's copy sets nothing off on the host");
    // 4. a dead doll, a doll with no object, an unknown owner
    seats[2].life = 0;
    Check(!GoesOff(seats, 16, 0x0A1B40, host, true, &seat) && seat == -1, "a dead doll sets nothing off");
    seats[2].life = 200; seats[2].present = false;
    Check(!GoesOff(seats, 16, 0x0A1B40, host, true, &seat) && seat == -1, "an absent doll sets nothing off");
    seats[2].present = true;
    Check(!GoesOff(seats, 16, 0x123456, host, true, &seat) && seat == -1, "a target of nobody sets nothing off");
    // 5. the game's own exceptions hold for the doll, by the DOLL's state: crawling, the 0x57 state, a ghost
    seats[1].bits0 = 0x3 | kQuiet0;
    Check(!GoesOff(seats, 16, 0x0A1B2C, host, true, &seat) && seat == 1, "a crawling doll sets nothing off");
    seats[1].bits0 = 0x3; seats[1].bits2 = kQuiet2;
    Check(!GoesOff(seats, 16, 0x0A1B2C, host, true, &seat), "a doll in the 0x57 state sets nothing off");
    seats[1].bits2 = 0; seats[1].bits10 = kQuiet10;
    Check(!GoesOff(seats, 16, 0x0A1B2C, host, true, &seat), "a ghost doll sets nothing off");
    seats[1].bits10 = 0;
    // 6. the host's own state never decides for a doll: a host whose words hold the three bits (stale, or a host that
    // plays and crawls) still lets a standing doll set it off; and the other bits of its words are kept
    Own crawling; crawling.id = 0x555555; crawling.bits0 = kQuiet0 | 0x100; crawling.bits2 = kQuiet2 | 0x4; crawling.bits10 = kQuiet10 | 0x1;
    Check(GoesOff(seats, 16, 0x0A1B2C, crawling, true, &seat) && seat == 1, "the host's own state does not keep a doll's claymore quiet");
    const Own w = With(crawling, seats[1]);
    Check(w.id == 0x0A1B2C && w.bits0 == 0x100 && w.bits2 == 0x4 && w.bits10 == 0x1, "the host's other bits kept, the three from the doll");
    // 7. a host that plays: its own player still sets a claymore off by the game's question, unchanged
    Own playing; playing.id = 0x0A1B77;
    Check(GoesOff(seats, 16, 0x0A1B77, playing, true, &seat) && seat == -1, "the program's own player: the game's question unchanged");
    Check(GoesOff(seats, 16, 0x0A1B77, playing, false, &seat), "the same with the switch off");
    playing.bits0 = kQuiet0;
    Check(!GoesOff(seats, 16, 0x0A1B77, playing, true, &seat), "the program's own player crawling: quiet");
    // 8. the switch off: beta 20's (nothing for a doll)
    Check(StandIn(seats, 16, 0x0A1B2C, false) == -1, "MGO1_VDOLL_TRAPS=0: no stand-in");

    printf("%s: %d checks passed, %d failed\n", gFail ? "FAILED" : "ALL PASSED", gPass, gFail);
    return gFail ? 1 : 0;
}
