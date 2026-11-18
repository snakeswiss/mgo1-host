// The game's own holds on this player's controls in a match, logged when they change (docs/LOGS.md, input:): its
// logical pad's lock (0x2C5E20 + 0x1E: 1-7 the pad off, 8 / 0x10 buttons masked, 0x20 / 0x40 presses added, 0x1000
// the physical pad copied), the weapon and item windows blocked (GM flags 0x2C5250 / 0x2C5254: 1 the weapon window,
// 2 the item window, 0x10000 no weapon window opening; game flags 0x126 to 0x129, as the windows' acts 0x3768D8 and
// 0x3755A8 read them). Included by ps2world.cpp inside its namespace; InputHoldsWatch runs at each pass.
struct InputHolds { uint32_t lock = 0, gm = 0, flags = 0; bool any = false; long long since = 0; };
InputHolds gInputHolds;
void InputHoldsWatch() {
    InputHolds& h = gInputHolds;
    const uint32_t pad = V(0x2C5E20), gmw = V(0x2C5238), fl = V(0x23C790);   // the running version's
    if (!InMatch() || !pad || !gmw || !fl) { h = InputHolds(); return; }
    const uint32_t lock = R16(pad + 0x1E) & 0x107F;
    const uint32_t gm = (R32(gmw + 0x18) | R32(gmw + 0x1C)) & 0x10003;
    const uint32_t flags = (R32(fl + 0x1C + 4 * (0x126 >> 5)) >> (0x126 & 31)) & 0xF;
    if (h.any && lock == h.lock && gm == h.gm && flags == h.flags) return;
    static long long lines = 0;
    if (h.any && (++lines <= 300 || lines % 100 == 0))       // a log line a change: the first 300 a run, then one in 100
        Log("input: the game's holds on this player's controls: pad lock %04x, windows' GM flags %05x, flags 0x126-0x129 %x "
            "(was %04x %05x %x for %lld frames)", lock, gm, flags, h.lock, h.gm, h.flags, gFrameNo - h.since);
    h.lock = lock; h.gm = gm; h.flags = flags; h.any = true; h.since = gFrameNo;
}
