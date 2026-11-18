// ---- map 15 (BIG SHELL STRUT E, docs/HELIPORT_MAP.md): a mission object (CAPTURE's KEROTAN, RESCUE's GA-KO, TEAM
// SNEAKING's two) dropped below the deck, its carrier fallen off an edge, goes back to its place at the round's start
// (the game's own return puts an object fallen far below where it was dropped: under the deck). The mission objects
// ([bf76c9] NewOnlineItemChara2): +0x410 their count, 0x120 each from +0xB0: +4 the model's code, +0x90 x y z, +0xD4
// the carrier (0: none). Every PC applies it.
extern uint32_t gTsneObjects;
bool TsneRound();
bool BcRound();
const float kHeliLostY = 9500.f;                        // the fall zones' top: no floor of the deck below it
struct HeliHome { uint32_t code = 0; float x = 0, y = 0, z = 0; };
HeliHome gHeliHome[3];
uint32_t gHeliObjects = 0, gHeliHomeOf = 0;             // the mission objects' actor (TSNE's: gTsneObjects); the homes'
bool HeliportObjects(Ctx& c, uint32_t va) {             // the mission objects' options (US n003a 0x39E0B0), not TSNE's
    if (gLoadedMap != 15 || TsneRound() || BcRound()) { gHeliObjects = 0; return false; }
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    const uint32_t mo = (uint32_t)c.r[4];
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    if ((int32_t)(uint32_t)c.r[2] >= 0 && mo >= 0x100000 && mo + 0x8C0 < 0x02000000) { gHeliObjects = mo; gHeliHomeOf = 0; }
    return true;
}
void HeliportTick() {                                   // a pass of the actor lists
    if (gLoadedMap != 15) return;
    const uint32_t mo = TsneRound() ? gTsneObjects : gHeliObjects;
    if (!mo || R32(mo + 0x410) > 3) return;
    const bool fresh = gHeliHomeOf != mo;
    gHeliHomeOf = mo;
    for (uint32_t k = 0; k < R32(mo + 0x410); ++k) {
        const uint32_t o = mo + 0xB0 + 0x120 * k, code = R32(o + 4) & 0xFFFFFF;
        HeliHome& h = gHeliHome[k];
        const float y = F32(o + 0x94);
        if (fresh || h.code != code) {                  // its place at the round's start
            if (R32(o + 0xD4) || y < kHeliLostY) { if (fresh) h.code = 0; continue; }
            h.code = code; h.x = F32(o + 0x90); h.y = y; h.z = F32(o + 0x98);
            Log("heliport: object %u (%06x) starts at %.0f %.0f %.0f", k, code, h.x, h.y, h.z);
            continue;
        }
        if (!h.code || R32(o + 0xD4) || y >= kHeliLostY) continue;
        Log("heliport: object %u (%06x) lost at %.0f %.0f %.0f, back at %.0f %.0f %.0f", k, code, F32(o + 0x90), y,
            F32(o + 0x98), h.x, h.y, h.z);
        uint32_t u;
        memcpy(&u, &h.x, 4); W32(o + 0x90, u);
        memcpy(&u, &h.y, 4); W32(o + 0x94, u);
        memcpy(&u, &h.z, 4); W32(o + 0x98, u);
    }
}
