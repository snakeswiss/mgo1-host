// The PLAYABLE DOLL's radio (MGO1's codec calls, docs/PLAYABLE_DOLL.md 7): its calls made with the pad as
// a player makes them, the calls of the others read as its radio reads them.
//
// The map page's radio (one work, its pointer in a word of the page found by its code: "lw rX, g(rY);
// bnez rX" before "addiu a0, zero, 0x7A0; jal 0x122540"; n011a 0x2E508C):
//   +0x40          its network channel (0x174B20 reads a call: a word {code << 24 | voice << 16 | id << 8
//                  | team}, then a byte; 0x174BA8 writes one)
//   +0x180 & 0x10  on: a short SELECT (under 30 frames, the HUD at 0x3D4F80 sends it 0xA20000)
//   +0x78C         0 closed, 3 a direction held, 1 its list shown, 2 a call made (shown)
//   +0x158         the list (a category: 0 up, 1 down, 2 right, 3 left)
//   +0x118         16 words: the text of each category and direction (the voice's first text at +0xC0 +
//                  4 * voice, the voice at the player's +0xEF7): the phrase is the text less the first
//   +0x110, +0xB8  the last call's player id and code (the doll's own too); +0x164 the time its own call
//                  is shown (450, counted down; the radio off at 0)
// A call: SELECT tapped, a direction held and let go (the list), a direction (the phrase). Only alive, the
// round running, no window open, no chaff grenade out (v134: the radio does not open while it jams, 24 s, on
// every machine: Jammed). On the network a phrase goes as its code (the page's switch at 0x39EC20).
#include "doll_int.h"
#include "ps2world.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace doll {
namespace in {

const char* const kPhraseNames[kPhrases] = {
    "go go go", "cover me", "follow me", "circle around", "hit, enemy unknown", "grenade", "defend the target",
    "pull out", "hold", "stay together", "take cover", "roger", "negative", "enemy sighted", "all clear",
    "come here", "sorry", "nice job", "don't die", "counting on you", "huh"};

namespace {
// a phrase's code on the network, and back
const int kNetOf[kPhrases] = {0, 7, 17, 4, 2, 5, 6, 9, 3, 10, 19, 15, 16, 1, 18, 8, 11, 14, 13, 12, 20};
int PhraseOfNet(int code) { for (int p = 0; p < kPhrases; ++p) if (kNetOf[p] == code) return p; return -1; }

uint32_t gRadioPtr = 0, gRadioKey = 0;
// the page's word that holds the radio's work (found once per page)
uint32_t RadioPointer() {
    const uint32_t key = R32(0x2DA684) ^ (R32(0x2DA694) << 1) ^ R32(0x2DA690);
    if (key == gRadioKey) return gRadioPtr;
    gRadioKey = key; gRadioPtr = 0;
    const uint32_t lo = R32(0x2DA690), hi = R32(0x2DA694);
    if (R32(0x2DA680) != 0x2DA680 || lo < 0x2DA680 || hi <= lo || hi - lo > 0x400000) return 0;
    for (uint32_t a = lo + 160; a + 16 <= hi && !gRadioPtr; a += 4) {
        if (R32(a) != 0x240407A0) continue;
        bool call = false;
        for (uint32_t k = 1; k < 4; ++k) if (R32(a + 4 * k) == 0x0C048950) call = true;
        if (!call) continue;
        for (uint32_t b = 1; b < 40 && !gRadioPtr; ++b) {
            const uint32_t w = R32(a - 4 * b);
            if ((w & 0xFFFF8000u) == 0x27BD8000u) break;                      // the function's start
            if ((w >> 26) != 0x23) continue;                                   // lw rt, imm(rs)
            const uint32_t rt = (w >> 16) & 31, rs = (w >> 21) & 31, next = R32(a - 4 * b + 4);
            if ((next >> 26) != 0x05 || ((next >> 21) & 31) != rt) continue;   // bnez rt
            for (uint32_t c = b + 1; c < b + 12; ++c) {
                const uint32_t l = R32(a - 4 * c);
                if ((l >> 26) == 0x0F && ((l >> 16) & 31) == rs) { gRadioPtr = ((l & 0xFFFF) << 16) + (uint32_t)(int32_t)(int16_t)(w & 0xFFFF); break; }
            }
            break;
        }
    }
    if (gDebug) Log("radio: the page's radio at [%08x]", gRadioPtr);
    return gRadioPtr;
}
uint32_t RadioWork() {
    const uint32_t p = RadioPointer();
    const uint32_t w = p ? R32(p) : 0;
    return w >= 0x100000 && w < 0x02000000 ? w : 0;
}
// the category and direction that make a phrase (false: not on this voice's list)
bool Where(int phrase, int* cat, int* dir) {
    const uint32_t w = RadioWork();
    if (!w || !gMe.obj || phrase < 0 || phrase >= kPhrases) return false;
    const int voice = R8(gMe.obj + 0xEF7);
    if (voice >= 15) return false;
    const int first = (int)R32(w + 0xC0 + 4 * voice);
    for (int i = 0; i < 16; ++i)
        if ((int)R32(w + 0x118 + 4 * i) - first == phrase) { *cat = i / 4; *dir = i % 4; return true; }
    return false;
}
const uint32_t kDir[4] = {0x10, 0x40, 0x20, 0x80};          // up, down, right, left (the pad's bits)
const uint32_t kRadioButtons = 0x00F1;                       // SELECT and the directions

struct Tx {
    int phrase = -1, step = 0, cat = 0, dir = 0, tries = 0, presses = 0;
    long long asked = 0, at = 0;
    uint32_t shownBefore = 0;
    std::string why;
    const char* failure = nullptr;                          // (step 4: the radio closed, then this said)
    bool urgent = false;                                    // made even while the doll would shoot (it holds its fire)
    long long lowerSince = -1;                              // (urgent: the aim let go since, for the radio to take presses)
    int lastIdBefore = -1; uint32_t lastCodeBefore = 0;     // (the radio's last call when this one began: a stale record is no proof)
};
long long gRadioOnSince = -1;                               // the radio left on with no call being made, since
Tx gTx;
long long gTxDoneAt = -100000;
uint32_t gChannel = 0;
std::vector<Call> gHeard;
}  // namespace

uint32_t RadioHud() { return RadioWork(); }
bool ChatDebug() { static int d = -1; if (d < 0) { const char* e = getenv("MGO1_DOLL_CHAT_DEBUG"); d = e && *e && *e != '0' ? 1 : 0; } return d == 1; }

void RadioFrame() {
    const uint32_t w = RadioWork();
    if (gDebug && w) {                                        // (its on / off and state, when they change)
        static uint32_t last = 0xFFFFFFFF;
        const uint32_t now = (R32(w + 0x180) & 0x30) | (R32(w + 0x78C) << 8);
        if (now != last) { Log("radio: on %u, state %u, shown %d", (now >> 4) & 3, now >> 8, (int)R32(w + 0x164)); last = now; }
    }
    const uint32_t ch = w ? w + 0x40 : 0;
    if (ch != gChannel) { gChannel = ch; ps2world::SetRadioChannel(ch); gHeard.clear(); }
    uint32_t words[16];
    const size_t n = ps2world::TakeRadioCalls(words, 16);
    for (size_t i = 0; i < n; ++i) {
        Call c;
        c.phrase = PhraseOfNet((int)(words[i] >> 24));
        c.from = (int)((words[i] >> 8) & 0xFF);
        c.team = (int)(words[i] & 0xFF);
        if (gDebug) Log("radio: word %08x (player %d, team %d, %s)", words[i], c.from, c.team, c.phrase >= 0 ? kPhraseNames[c.phrase] : "?");
        if (c.phrase >= 0 && c.from != gMyId && gHeard.size() < 32) gHeard.push_back(c);
    }
}

bool RadioHeard(Call* c) {
    if (gHeard.empty()) return false;
    *c = gHeard.front();
    gHeard.erase(gHeard.begin());
    return true;
}

// (v141 beta 6) the radio's menu left on with no call (a call suspended by the aim, or one whose menu stayed up): no button closes
// it (probe 31's step r: Cross, Triangle, Circle, R1, Square, L1, 600 frames each; 25000 frames untouched) and the weapon window
// does not open meanwhile. Closed as a player can without a word sent: SELECT, its menu gives way to the match's text chat (the
// radio's flag off), then SELECT again closes the chat with nothing typed nor sent (probe 31's step k, the aa2 bench: twice, the
// chat counter unchanged). The second SELECT is the chat guard's (doll.cpp ChatFrame, which knows the chat this opened).
// MGO1_DOLL_RADIO_CLOSE=0: left on (the aim branch's way: the next call made from the menu up)
long long gRadioCloseAt = -100000;
long long RadioCloseAt() { return gRadioCloseAt; }
bool RadioCloseFrame(bool may) {
    static const bool off = getenv("MGO1_DOLL_RADIO_CLOSE") && !strcmp(getenv("MGO1_DOLL_RADIO_CLOSE"), "0");
    if (gFrame - gRadioCloseAt < 4) {                     // (its SELECT: 4 frames, nothing else)
        gPad.buttons = 0x0001; gPad.sticks[0] = gPad.sticks[1] = gPad.sticks[2] = gPad.sticks[3] = 0x80;
        return true;
    }
    if (off || !may || gTx.phrase >= 0 || gRadioOnSince < 0 || gFrame - gRadioOnSince < 60 || gFrame - gRadioCloseAt < 300) return false;
    const uint32_t w = RadioWork();
    if (!w || !(R32(w + 0x180) & 0x10) || !gMe.alive || gMe.frozen) return false;
    gRadioCloseAt = gFrame;
    Log("radio: left on %.1f s with no call (state %u): closed by SELECT (to the text chat, the chat guard's SELECT out: nothing sent)",
        (gFrame - gRadioOnSince) / 60.0, R32(w + 0x78C));
    gPad.buttons = 0x0001; gPad.sticks[0] = gPad.sticks[1] = gPad.sticks[2] = gPad.sticks[3] = 0x80;
    return true;
}

bool RadioOnMenu(int phrase) { int c, d; return Where(phrase, &c, &d); }
bool RadioBusy() { return gTx.phrase >= 0; }
bool RadioPressing() { return gTx.phrase >= 0 && gTx.step > 0; }
bool RadioUrgent() { return gTx.phrase >= 0 && gTx.urgent; }

bool RadioSay(int phrase, const std::string& why, bool urgent) {
    if (gTx.phrase >= 0 || Jammed()) return false;         // (a chaff out: the radio does not open)
    int cat = 0, dir = 0;
    if (!Where(phrase, &cat, &dir)) return false;
    gTx = Tx(); gTx.phrase = phrase; gTx.cat = cat; gTx.dir = dir; gTx.asked = gTx.at = gFrame; gTx.why = why; gTx.urgent = urgent;
    return true;
}

void RadioStop(const char* why) {
    if (gTx.phrase < 0) return;
    if (gTx.step > 0 || gDebug) Log("codec: \"%s\" not sent (%s)", kPhraseNames[gTx.phrase], why);
    gTx.phrase = -1; gTxDoneAt = gFrame;
}

// the call's presses, a step at a time: SELECT and the directions (the brain's other buttons kept, its
// aim and its shots held back); true when the pad was used. A direction the radio did not take is
// pressed again (3 times at most); a call given up closes the radio as a player does (SELECT). calm: no
// window, menu, throw, ladder, scope nor first person view; quiet: not aiming nor firing this frame (an
// urgent call goes all the same: the doll holds its fire a moment)
bool RadioPad(bool calm, bool quiet) {
    Tx& t = gTx;
    if (t.phrase < 0) {
        // the radio left on, no call being made (a call that did not go, its list suspended by the aim). (v141 beta 6) Never
        // SELECT to close it: a second SELECT while the radio's menu is up opens the match's text chat (probe 31's step k; beta 5
        // closed it so every 5 s and opened the chat each time: the players' blank chat lines). Left to the radio's own end
        const uint32_t w0 = RadioWork();
        if (!w0 || !(R32(w0 + 0x180) & 0x10) || !gMe.alive || gMe.frozen) { gRadioOnSince = -1; return false; }
        if (gRadioOnSince < 0) gRadioOnSince = gFrame;
        if (gFrame - gRadioOnSince == 180 && (gDebug || ChatDebug())) Log("radio: left on 3 s, no call (never closed by SELECT: the chat)");
        return false;
    }
    gRadioOnSince = -1;
    const uint32_t w = RadioWork();
    if (!w || !gMe.alive || gMe.frozen) { RadioStop("the radio not usable now"); return false; }
    if (Jammed() && t.step <= 1) { RadioStop("jammed by a chaff grenade"); return false; }
    // the radio's own blocks: the player's states 0x151, 0x9B, 0x99 (it takes no press in them) and 0x156,
    // 0x157 (aiming, firing: the radio's state 2, its list lost). State n is bit n & 31 of the word n >> 5
    const bool blocked = (gMe.bits[10] & 0xC20000) || (gMe.bits[4] & 0x0A000000);
    const long long d = gFrame - t.at;
    const bool on = (R32(w + 0x180) & 0x10) != 0;
    const uint32_t state = R32(w + 0x78C);
    // its presses have the pad's left thumb (the d-pad: the left stick let go, as a player's thumb leaves
    // it; the radio reads the stick's directions with the d-pad's), and no aim nor shot meanwhile (they
    // would stop the radio): a second of standing still
    // (L2, R2 too, their pressures with them: a lean of the first person view waits; R2 alone in the third
    // person view is the weapon window; (v141 beta 6, aim) L1 too: the fight's auto aim waits)
    auto hold = [&]() {
        gPad.buttons &= ~(kRadioButtons | 0x8800u | 0x0700u);
        gPad.pressures[11] = gPad.pressures[4] = gPad.pressures[5] = 0; gPad.sticks[2] = gPad.sticks[3] = 0x80;
    };
    auto press = [&](uint32_t b) { hold(); gPad.buttons |= b; return true; };
    auto idle = [&]() { hold(); return false; };
    auto giveUp = [&](const char* how) {                    // the radio closed first when it is on
        t.failure = how; t.step = 4; t.at = gFrame;
        return idle();
    };
    auto trace = [&](const char* what) {                    // (debug: the radio's gates when a press was not taken)
        if (gDebug) Log("radio: %s (on %d, state %u, input off %u, shown %d, pad lock %x %x, held %x, released %x, bits %x %x)", what, on, state,
                        R32(w + 0x168), (int)R32(w + 0x164), R32(0x2C5248), R32(0x2C524C), R32(0x2C5E68), R32(0x2C5E80), gMe.bits[4], gMe.bits[10]);
    };
    switch (t.step) {
    case 0: {                                                 // the radio closed and the doll free: SELECT tapped
        if (gFrame - t.asked > 300) { RadioStop("no moment to make it"); return false; }
        // (v141 beta 6, aim) the radio left on, its menu up with no call (state 0, nothing shown): it stays so whatever is pressed but
        // SELECT (probe 31's step r, the aim_radio bench: Cross, Triangle, Circle, R1, Square, L1 each watched 600 frames; the aa3
        // bench: 25000 frames untouched). The call made from there, its list's direction at once (a SELECT would open the text chat):
        // the call's own end closes the radio
        if (on && state == 0 && R32(w + 0x164) == 0 && calm && quiet && !blocked && gFrame - gTxDoneAt >= 30) {
            if (gDebug || ChatDebug()) Log("radio: \"%s\" from the radio left on (its list at once, no SELECT)", kPhraseNames[t.phrase]);
            t.step = 2; t.at = gFrame; t.presses = 0;
            t.shownBefore = R32(w + 0x164); t.lastIdBefore = R8(w + 0x110); t.lastCodeBefore = R32(w + 0xB8);
            return idle();
        }
        // urgent: the aim let go (the states 0x156, 0x157: the word 10's 0xC00000) until the radio takes
        // presses (40 frames at most), as a player lowers his gun to call
        // (10 frames at least: at a fight's first frame the aim has not risen yet, it would rise under the call)
        const bool aimUp = (gMe.bits[10] & 0xC00000) != 0;
        if (t.urgent && calm && !on && gFrame - gTxDoneAt >= 30 && !(gMe.bits[10] & 0x20000) && !(gMe.bits[4] & 0x0A000000)) {
            if (t.lowerSince < 0) t.lowerSince = gFrame;
            if (gFrame - t.lowerSince < 10 || (aimUp && gFrame - t.lowerSince < 40)) { hold(); return true; }
        }
        if (on || !calm || (!quiet && !t.urgent) || blocked || gFrame - gTxDoneAt < 30) return false;
        if (gDebug) Log("radio: \"%s\" begun after %lld frames (list %d, direction %d)", kPhraseNames[t.phrase], gFrame - t.asked, t.cat, t.dir);
        t.step = 1; t.at = gFrame; t.shownBefore = R32(w + 0x164); t.lastIdBefore = R8(w + 0x110); t.lastCodeBefore = R32(w + 0xB8);
        if (ChatDebug()) Log("DOLL CHAT: SELECT for the radio's call \"%s\" (radio flags %x, state %u)", kPhraseNames[t.phrase], R32(w + 0x180), state);
        return press(0x0001);
    }
    case 1:
        if (d < 4) return press(0x0001);                   // (its first frame in step 0: the line there)
        if (on) { t.step = 2; t.at = gFrame + 2; t.presses = 0; return idle(); }   // (its list a moment after it opens)
        // (v141 beta 6) not on: once more, but only 90 frames after (a second SELECT once the radio is up opens the text chat)
        if (d > 90) {
            trace("not opened");
            if (++t.tries >= 3) { t.failure = "the radio did not open"; t.step = 4; t.at = gFrame; return idle(); }
            t.step = 0; t.asked = gFrame;
        }
        return idle();
    case 2:                                                   // the list: its direction held 4 frames, let go
        if (d < 0) return idle();
        if (!on) { t.step = 0; t.asked = gFrame; if (++t.tries >= 3) { Log("codec: sending \"%s\" failed (the radio closed)", kPhraseNames[t.phrase]); t.phrase = -1; gTxDoneAt = gFrame; } return idle(); }
        if (d < 4) return press(kDir[t.cat]);
        if (state == 1 && (int)R32(w + 0x158) == t.cat) { t.step = 3; t.at = gFrame; t.presses = 0; return idle(); }
        if (state == 2) { t.failure = nullptr; t.step = 4; t.at = gFrame; return idle(); }   // (suspended: aiming)
        if (d >= 14 && state == 0) {                          // not taken: pressed again
            trace("list press not taken");
            if (++t.presses >= 3) return giveUp("its list did not open");
            t.at = gFrame + 6;
        }
        if (d > 40) return giveUp("its list did not open");
        return idle();
    case 3: {                                                 // the phrase: its direction, the call goes
        if (d < 0) return idle();
        if (d < 4) return press(kDir[t.dir]);
        // made: its shown time up, or the state 2 without the aim (the aim's state 2 is the call suspended),
        // or the last call's record changed to its own (a stale record is no proof)
        const bool aiming = (gMe.bits[10] & 0xC00000) != 0;
        const bool recordNew = (int)R8(w + 0x110) != t.lastIdBefore || R32(w + 0xB8) != t.lastCodeBefore;
        if (R32(w + 0x164) > t.shownBefore || (state == 2 && !aiming) ||
            (recordNew && (int)R8(w + 0x110) == gMyId && (int)R32(w + 0xB8) == kNetOf[t.phrase] && state == 2)) {
            if (gDebug) Log("radio: made in %lld frames (shown %u, state %u)", gFrame - t.asked, R32(w + 0x164), state);
            Log("codec: sent \"%s\"%s%s", kPhraseNames[t.phrase], t.why.empty() ? "" : ": ", t.why.c_str());
            t.phrase = -1; gTxDoneAt = gFrame;
            return idle();
        }
        if (state == 2 && d >= 6) { t.failure = nullptr; t.step = 4; t.at = gFrame; return idle(); }   // (suspended: aiming)
        if (d >= 14 && state == 1) {                          // not taken: pressed again
            trace("phrase press not taken");
            if (++t.presses >= 3) return giveUp("the call did not go");
            t.at = gFrame + 6;
        }
        if (d > 40) return giveUp("the call did not go");
        return idle();
    }
    case 4:                                                   // given up: (v141 beta 6) no SELECT (the text chat), then said
        if (on && d < 40) return idle();
        if (t.failure) Log("codec: sending \"%s\" failed (%s)", kPhraseNames[t.phrase], t.failure);
        else Log("codec: \"%s\" not sent (the radio suspended by the aim)", kPhraseNames[t.phrase]);
        t.phrase = -1; gTxDoneAt = gFrame + 240;              // (a while before the next)
        return idle();
    default:
        t.phrase = -1;
        return false;
    }
}

}  // namespace in
}  // namespace doll
