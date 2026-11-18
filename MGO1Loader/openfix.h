// openfix.h - fixes of the open list (v141): the pictures between the ticks at 60 (docs/FPS60.md, The 2D at 60);
// the extra maps of a game told in the host list, so a join tries their dialect first (docs/BIG_GAMES.md).
// Included by ps2world.cpp inside its namespace, after the memory and version helpers, before ExtraPicture.
#pragma once

// ---- the host list's hint of a game's extra maps. A game whose rotation holds maps 13 and up speaks a dialect of
// those maps (MapsSet); a joiner learns it from its host's answer, trying each set in turn (JoinCandidates: 18 with
// three extra maps and TEAM SNEAKING's), and a join sends about 8 connects: maps 13 and 14 were the 9th set, all
// three the 17th, so a player failed one or two joins (20 s each) before he got in. The host tells its extra maps in
// bits 2..4 of the options' second byte (settings +0x2AA in the create message 0x4310), which the tx55 server keeps
// and relays whole in the host list (GameSettings.Bytes[1]: the entry's +0x1C); the stock programs read bits 0 and 1
// there (team kill kick, voice chat), TEAM SNEAKING marks bit 7, BASE CAPTURE bit 6 (their rules' dialect too); bit 5
// a game of more than 8 (the lobby server is told 8: the PCs' set was the third tried).
const uint8_t kMapsHintBit[3] = {0x04, 0x08, 0x10};    // maps 13, 14, 15
const uint8_t kBigHintBit = 0x20;                      // more than 8 players
uint8_t MapsListHint(const uint8_t* maps, bool big) {
    uint8_t h = big ? kBigHintBit : 0;
    for (int i = 0; i < 16 && maps[i]; ++i) if (maps[i] >= 13 && maps[i] <= 15) h |= kMapsHintBit[maps[i] - 13];
    return h;
}
void JoinHintFrom(uint8_t opts) {                      // the chosen game's options byte: its set tried first
    std::vector<int> ids;
    for (int k = 0; k < 3; ++k) if (opts & kMapsHintBit[k]) ids.push_back(13 + k);
    int r = ids.empty() ? 2 : MapsSet(ids);
    if (!ids.empty() && r < 3) {                        // one of its maps is not here: no hint, the join will fail
        r = -1;
        Log("session: the game chosen plays the extra maps%s%s%s (options %02x), not all in this PC's maps folder: its join cannot succeed",
            opts & 0x04 ? " 13" : "", opts & 0x08 ? " 14" : "", opts & 0x10 ? " 15" : "", opts);
    }
    if (r >= 0 && (opts & 0xC0)) r = TsneSet(r);        // a rule of the PCs' own (TEAM SNEAKING, BASE CAPTURE)
    if (r >= 0 && ids.empty() && !(opts & 0xE0)) r = -1;   // a game of 8 of the stock maps and rules: its region's sets
    gJoinHintSet = r;
    if (r >= 0) Log("session: the game chosen tells its extra maps or rules (options %02x): its %s dialect tried first", opts, SetName(r));
}

// ---- the host's last game settings (docs/BIG_GAMES.md, Create Game's settings). The lobby server gives a player's
// last game settings back at his next Create Game (0x4304, the answer read by 0x194838): a PC game's came back in the
// form LobbyCreateMessage told (stock maps and rules, 8 players, the host list's marks in VOICE CHAT's field).
struct LastGame { uint8_t told[33], own[33]; };          // rules 0..15, maps 16..31, most players 32
LastGame gLastGame;
int gLastGameState = 0;                                  // 0 not read yet, 1 known, -1 none
std::string LastGamePath() {                             // next to the memory card's folder
    const char* r = getenv("MGO1_CARD_DIR");
    return std::string(r && *r ? r : "memcard") + "_lastgame.bin";
}
void LastGameTold(uint32_t st, const uint8_t* maps, const uint8_t* rules, uint8_t most) {   // before the 0x4310 is sent
    for (int i = 0; i < 16; ++i) {
        gLastGame.told[i] = R8(st + 0x234 + i); gLastGame.told[16 + i] = R8(st + 0x244 + i);
        gLastGame.own[i] = rules[i]; gLastGame.own[16 + i] = maps[i];
    }
    gLastGame.told[32] = R8(st + 0x257); gLastGame.own[32] = most;
    gLastGameState = 1;
    if (FILE* f = fopen(LastGamePath().c_str(), "wb")) { fwrite("MGO1LG1", 1, 8, f); fwrite(&gLastGame, sizeof gLastGame, 1, f); fclose(f); }
}
bool LastSettingsRead(Ctx& c, uint32_t va) {             // 0x194838 (a0: the lobby's context)
    ps2::Fn f = ps2::FindFn(va);
    if (!f) return false;
    const uint32_t ctx = (uint32_t)c.r[4];
    const uint64_t ra = c.r[31];
    f(c);
    c.r[31] = ra;
    if ((int32_t)(uint32_t)c.r[2] != 0 || !ctx) return true;
    Ctx x = c;
    x.r[29] = ((uint32_t)c.r[29] - 0x400) & ~0xfu;
    x.r[31] = V(0x1005f0);
    x.r[4] = ctx;
    Call(x, V(0x194c30));                                  // the context's settings
    const uint32_t st = (uint32_t)x.r[2] & 0x01ffffff;
    if (st < 0x100000 || st + 0x2b0 >= 0x02000000) return true;
    const uint8_t opts = R8(st + 0x2aa);
    if (!(opts & 0xFC)) return true;                       // no mark: not a PC game's settings
    W8(st + 0x2aa, opts & 0x03);                           // bits 1..7 are VOICE CHAT's field (on when not 0)
    if (!gLastGameState) {
        gLastGameState = -1;
        if (FILE* fl = fopen(LastGamePath().c_str(), "rb")) {
            char tag[8] = {0};
            if (fread(tag, 1, 8, fl) == 8 && !memcmp(tag, "MGO1LG1", 8) && fread(&gLastGame, sizeof gLastGame, 1, fl) == 1) gLastGameState = 1;
            fclose(fl);
        }
    }
    bool same = gLastGameState == 1 && R8(st + 0x257) == gLastGame.told[32];
    for (int i = 0; same && i < 15; ++i)                   // the server keeps 15 pairs
        same = R8(st + 0x234 + i) == gLastGame.told[i] && R8(st + 0x244 + i) == gLastGame.told[16 + i];
    int missing = 0;                                       // an extra map of the own rotation this PC no longer has
    for (int i = 0; same && i < 15 && !missing; ++i) {
        const uint8_t m = gLastGame.own[16 + i];
        const ExtraMap* e = m > 12 ? ExtraMapById(m) : nullptr;
        if (m > 12 && !(e && e->ready)) missing = m;
    }
    if (same && !missing) {
        for (int i = 0; i < 15; ++i) { W8(st + 0x234 + i, gLastGame.own[i]); W8(st + 0x244 + i, gLastGame.own[16 + i]); }
        W8(st + 0x257, gLastGame.own[32]);
    }
    Log("lobby: the last game's settings from the lobby server: options %02x -> %02x (the host list's marks off)%s", opts, opts & 0x03,
        !same ? "" : !missing ? ", the rotation and the players as this PC made them"
                              : ", the server's rotation kept (this PC's own named a map not in its maps folder now)");
    if (same && missing) Log("lobby: map %d of the last game's rotation is not in the maps folder", missing);
    return true;
}

// DG's dmapacks (US base: the queue insert 0x12FFC8, the plugin 0x130008 called by the build 0x1298F0): a view's
// queue [chanl + 0x3A0] holds 4 lists from +0x278, 0x28 bytes each (+4 the count, +0xC the first, linked by +4).
// A dmapack: +0x10 its flags (0x4000: one buffer, its index at +0x20), +0x18 / +0x1C its two packets, +0x2C a
// callback (built otherwise). Its writer (the HUD, the font system 0x106CE8, the effects) writes packet[DG clock]
// when it acts; the build CALLs packet[DG clock] (0x23AFD0, flipped by each kick 0x12AD90). At 30 a tick's writes
// are the next build's; at 60 two builds follow a tick, the second one at the other clock: it drew the packet of
// the last tick of that parity (the HUD's count of an older tick, docs/FPS60.md). DgPacksSync copies each
// dmapack's chain written at the tick into the other packet before such a build. The DMA data is read at the kick
// (the GS thread copies it there too), so the copy never races a drawing.
// Some packets are built once for each clock with that clock's frame buffer (City's glow source, US n003a 0x30C450:
// TEX0 TBP clock << 12, frame buffer c at FBP c x 0x80): a copy swaps the two frame buffers in its GS register
// writes (DmaChainSwapBuffers), else the other clock's build read the previous picture (glow over glow).
long long gPackCopies = 0, gPackBytes = 0, gPackSkipped = 0, gPackSwaps = 0;
// the bytes of a DMA chain from p up to its last RET (0: not a chain held in one buffer of at most `cap`)
uint32_t DmaChainLength(uint32_t p, uint32_t cap) {
    uint32_t a = p;
    for (int n = 0; n < 4096 && a + 16 <= p + cap; ++n) {
        const uint32_t tag = R32(a), qwc = tag & 0xFFFF, id = (tag >> 28) & 7;
        switch (id) {
        case 1: a += 16 * (qwc + 1); break;              // cnt: its data follows
        case 3: case 4: a += 16; break;                   // ref, refs: its data elsewhere
        case 5: a += 16 * (qwc + 1); break;               // call: the called chain is another's (its RET comes back here)
        case 6: case 7: return a + 16 * (qwc + 1) - p;    // ret, end: the chain's end
        default: return 0;                                // refe, next: not a chain of one buffer
        }
    }
    return 0;
}
// the chain's GIF data sent through VIF1 DIRECT / DIRECTHL in its own buffer (cnt, call, ret, end): A+D writes of
// TEX0_1/2, FRAME_1/2 and BITBLTBUF naming frame buffer `from` name buffer `to` and the other way; the count changed
uint32_t DmaChainSwapBuffers(uint32_t p, uint32_t len, int from, int to) {
    if (from == to) return 0;
    const uint64_t tA = (uint64_t)from << 12, tB = (uint64_t)to << 12, fA = (uint64_t)from << 7, fB = (uint64_t)to << 7;
    auto swap = [](uint64_t v, int shift, uint64_t mask, uint64_t a, uint64_t b) {
        const uint64_t x = (v >> shift) & mask;
        return x == a ? (v & ~(mask << shift)) | (b << shift) : x == b ? (v & ~(mask << shift)) | (a << shift) : v;
    };
    uint32_t changed = 0;
    for (uint32_t a = p, n = 0; a + 16 <= p + len && n < 4096; ++n) {
        const uint32_t tag = R32(a), qwc = tag & 0xFFFF, id = (tag >> 28) & 7, vif = R32(a + 12), cmd = (vif >> 24) & 0x7F;
        if (id != 1 && id != 5 && id != 6 && id != 7) { a += 16; continue; }   // ref, refs: their data elsewhere
        uint32_t g = a + 16;
        const uint32_t end = g + 16 * std::min<uint32_t>(qwc, (cmd == 0x50 || cmd == 0x51) ? ((vif & 0xFFFF) ? (vif & 0xFFFF) : 0x10000) : 0);
        while (g + 16 <= end && g + 16 <= p + len) {         // GIF tags
            const uint64_t lo = R64(g), hi = R64(g + 8);
            const uint32_t nloop = (uint32_t)(lo & 0x7FFF), flg = (uint32_t)(lo >> 58) & 3, nreg = ((uint32_t)(lo >> 60) & 15) ? (uint32_t)(lo >> 60) & 15 : 16;
            g += 16;
            if (flg == 0) {                                   // packed
                for (uint32_t l = 0; l < nloop && g + 16 <= end; ++l)
                    for (uint32_t r = 0; r < nreg && g + 16 <= end; ++r, g += 16) {
                        if (((hi >> (4 * r)) & 15) != 0xE) continue;   // A+D
                        const uint32_t reg = R32(g + 8) & 0xFF;
                        const uint64_t v = R64(g);
                        uint64_t w = v;
                        if (reg == 0x06 || reg == 0x07) w = swap(v, 0, 0x3FFF, tA, tB);
                        else if (reg == 0x4C || reg == 0x4D) w = swap(v, 0, 0x1FF, fA, fB);
                        else if (reg == 0x50) w = swap(swap(v, 0, 0x3FFF, tA, tB), 32, 0x3FFF, tA, tB);
                        if (w != v) { W64(g, w); ++changed; }
                    }
            } else if (flg == 1) g += 8 * ((nloop * nreg + 1) & ~1u);   // reglist
            else g += 16 * nloop;                             // image
        }
        if (id == 6 || id == 7) break;
        a += 16 * (qwc + 1);
    }
    return changed;
}
void DgPacksSync(int writers) {
    const char* env = getenv("MGO1_DMAPACK_SYNC");       // tests: 0 off, 2 the font's view only (read each time: a step may set it)
    const int mode = env ? atoi(env) : 1;
    static int told = -1;
    if (mode != told) { told = mode; Log("exact 60: the 2D packets' sync %s", mode == 0 ? "off" : mode == 2 ? "on for the HUD's view" : "on"); }
    if (!mode || writers < 0) return;
    const int clock = (int)(R32(V(0x23AFD0)) & 1);
    if (clock == writers) return;                         // the next build reads what the tick wrote
    std::map<uint32_t, uint32_t> lists;                  // a list -> its view's number (+0x354)
    for (uint32_t ch = R32(V(0x23AF80)), n = 0; ch && n < 16; ch = R32(ch + 0x360), ++n) {
        if (ch < 0x100000 || ch + 0x400 > 0x02000000) break;
        const uint32_t q = R32(ch + 0x3A0) & 0x01FFFFFF;
        if (q < 0x100000 || q + 0x318 > 0x02000000) continue;
        for (uint32_t k = 0; k < 4; ++k) lists.emplace(q + 0x278 + 0x28 * k, R32(ch + 0x354));
    }
    static int described = 0;                             // the first sync's dmapacks described in the log
    const bool describe = !described++ || getenv("SIM_DMAPACK_LOG");
    uint32_t fontView = 0xFFFFFFFF;                       // the view whose lists hold the font system's dmapack (the HUD's)
    for (const auto& lv : lists) {
        uint32_t o = R32(lv.first + 0xC) & 0x01FFFFFF;
        for (uint32_t i = 0; i < R32(lv.first + 4) && i < 512 && o >= 0x100000 && o + 0x30 < 0x02000000 && R32(o + 4); ++i, o = R32(o + 4) & 0x01FFFFFF)
            if (o == (R32(V(0x27F340) + 0x44) & 0x01FFFFFF)) fontView = lv.second;
    }
    if (describe) Log("exact 60: the font system's dmapack %08x (US 0x27F340 + 0x44) in view %d; sync mode %d", R32(V(0x27F340) + 0x44), (int)fontView, mode);
    int packs = 0;
    for (const auto& lv : lists) {
        if (mode == 2 && lv.second != fontView) continue;
        const uint32_t l = lv.first, count = R32(l + 4);
        uint32_t o = R32(l + 0xC) & 0x01FFFFFF;
        for (uint32_t i = 0; i < count && i < 512 && o >= 0x100000 && o + 0x30 < 0x02000000 && R32(o + 4); ++i, o = R32(o + 4) & 0x01FFFFFF) {
            const uint32_t flags = R32(o + 0x10);
            const uint32_t src = R32(o + 0x18 + 4 * writers) & 0x01FFFFFF, dst = R32(o + 0x18 + 4 * clock) & 0x01FFFFFF;
            const uint32_t gap = src > dst ? src - dst : dst - src;
            const bool skip = (flags & 0x4000) || R32(o + 0x2C) || src == dst || src < 0x100000 || dst < 0x100000 ||
                              src >= 0x02000000 || dst >= 0x02000000;   // one buffer, built by its callback, or none
            const uint32_t len = skip ? 0 : DmaChainLength(src, std::min<uint32_t>(gap, 0x40000));
            if (describe)
                Log("exact 60: dmapack %08x view %u list %08x flags %08x phase %d packets %08x %08x callback %08x: %s %u bytes",
                    o, lv.second, l, flags, (int16_t)R16(o + 0x14),
                    R32(o + 0x18), R32(o + 0x1C), R32(o + 0x2C), skip ? "left" : len ? "copied" : "not a chain", len);
            if (skip) continue;
            if (!len || dst + len > 0x02000000) { ++gPackSkipped; continue; }
            memcpy(gMem + dst, gMem + src, len);
            const uint32_t swaps = DmaChainSwapBuffers(dst, len, writers, clock);
            if (swaps && (describe || !gPackSwaps)) Log("exact 60: dmapack %08x copied with %u frame buffer refs swapped (%d -> %d)", o, swaps, writers, clock);
            ++packs; ++gPackCopies; gPackBytes += len; gPackSwaps += swaps;
        }
    }
    static long long calls = 0;
    if (packs && (++calls == 1 || calls % 3600 == 0))
        Log("exact 60: the tick's 2D packets copied for the picture of the other clock (%d dmapacks now; %lld copies, %lld bytes, %lld not chains, %lld frame buffer refs swapped, in %lld syncs)",
            packs, gPackCopies, gPackBytes, gPackSkipped, gPackSwaps, calls);
}
