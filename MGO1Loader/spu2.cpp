// spu2.cpp - the SPU2 in software (spu2.h): voices, ADPCM, envelopes, block input, the mix and
// the reverb. The reverb follows the formula of psx-spx (SPU Reverb Formula) as the SPU2 runs it:
// at 24 kHz, left and right on alternate samples, the input and the output through the 39-tap
// resampling filter psx-spx documents, the work area between the core's ESA and EEA.
#include "spu2.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace spu2 {
namespace {

constexpr uint32_t kRamSize = 2u << 20;
uint8_t gRam[kRamSize];
std::mutex gLock;

// a volume register's level (psx-spx: SPU Volume and ADSR Generator): fixed (bit 15 clear: the level is
// the 15-bit value times 2) or a sweep (bit 15: 14 exponential, 13 decrease, 12 negative phase, 6..0 the
// rate, as an envelope's) moving the level from where it is, one step at the rate's pace
struct Sweep {
    int32_t level = 0;                           // -0x8000 .. 0x7fff
    bool on = false, dec = false, expo = false, neg = false;
    int rate = 0;
    uint32_t count = 0;
};
struct Voice {
    uint16_t volL = 0, volR = 0, pitch = 0, adsr1 = 0, adsr2 = 0;
    Sweep swL, swR;                              // the volumes' levels (volL / volR as written)
    uint32_t ssa = 0, lsa = 0;                   // byte addresses, 16-byte blocks
    bool lsaManual = false;                      // LSAX written after the key on: the blocks' loop-start flags do not move it
    bool on = false;
    uint32_t cur = 0;                            // the block being played
    int idx = 28;                                // its next sample (28: decode the next block first)
    uint8_t flags = 0;                           // the block's loop flags
    int16_t pcm[28] = {};
    int32_t h1 = 0, h2 = 0;                      // ADPCM history
    uint32_t frac = 0;                           // pitch counter: 12 bits of fraction
    int16_t s[4] = {};                           // the last four samples (interpolation)
    int phase = 4;                               // envelope: 0 attack, 1 decay, 2 sustain, 3 release, 4 off
    int32_t env = 0;
    uint32_t envCount = 0;
    uint64_t gen = 0;                            // its key on (a substitute follows the voice while it is that one's)
    bool mute = false;                           // a substitute plays in its place: the voice runs on, unheard
};
struct Core {
    Voice v[24];
    uint32_t pmon = 0, non = 0, vmixl = 0, vmixr = 0, vmixel = 0, vmixer = 0, endx = 0;
    uint16_t mvolL = 0, mvolR = 0, evolL = 0, evolR = 0, avolL = 0, avolR = 0, bvolL = 0, bvolR = 0, mmix = 0;
    Sweep mvL, mvR;                              // MVOL's levels
    int32_t noiseLevel = 1, noiseTimer = 0;      // the noise generator (NON voices), its clock in attr[4]
    uint32_t esa = 0, eea = 0, tsa = 0, irqa = 0;
    uint32_t attr[8] = {};
    // reverb: the offsets in halfwords, the coefficients (1.15), the work area's start (halfwords)
    bool rvSet = false;
    uint32_t apf1Size = 0, apf2Size = 0, sameLDst = 0, sameRDst = 0, comb1L = 0, comb1R = 0, comb2L = 0, comb2R = 0,
             sameLSrc = 0, sameRSrc = 0, diffLDst = 0, diffRDst = 0, comb3L = 0, comb3R = 0, comb4L = 0, comb4R = 0,
             diffLSrc = 0, diffRSrc = 0, apf1LDst = 0, apf1RDst = 0, apf2LDst = 0, apf2RDst = 0;
    int32_t vIIR = 0, vComb1 = 0, vComb2 = 0, vComb3 = 0, vComb4 = 0, vWall = 0, vApf1 = 0, vApf2 = 0, vInL = 0, vInR = 0;
    uint32_t esaH = 0;
    int32_t down[2][128] = {}, up[2][128] = {};
    int bufPos = 0;
    uint64_t cycles = 0;
    // block input
    bool block = false, blockLoop = true;
    const uint8_t* mem = nullptr;
    uint32_t base = 0, size = 0, pos = 0;        // pos: samples played in the buffer (4 bytes of a block pair each)
    uint64_t halfSum[2] = {0, 0};                // each half's bytes when it last started (a half played again unchanged)
};
Core gCore[2];
long long gKeyOns = 0, gHalves = 0, gSamples = 0, gStaleHalves = 0;
// a half of the block input starting to play: the same bytes as when it last started (not silence) means the IOP did
// not refill it in time, the half before is heard again (a crackle in the music)
void HalfStarts(Core& c, int h) {
    const uint32_t half = c.size / 2, at = (c.base + (uint32_t)h * half) & 0x1fffff;
    if (at + half > 0x200000) return;
    uint64_t x = 0xcbf29ce484222325ull; bool any = false;
    for (uint32_t k = 0; k < half; k += 8) { uint64_t w; memcpy(&w, c.mem + at + k, 8); any |= w != 0; x = (x ^ w) * 0x100000001b3ull; }
    if (any && x == c.halfSum[h]) ++gStaleHalves;
    c.halfSum[h] = x;
}
uint64_t gGen = 0;
// the added weapons' sounds (Substitute): a voice's next key on given a sound of its own, and the sounds playing
struct Tag { bool set = false; std::shared_ptr<const Pcm> pcm; float gain = 1; uint64_t cut = 0; long long at = 0; };
Tag gTag[2][24];
struct Sub {
    std::shared_ptr<const Pcm> pcm;
    double pos = 0, step = 1;                    // in the sound's samples; step: its rate / 48000
    float gain = 1;
    int core = 0, voice = 0;
    uint64_t gen = 0;                            // the voice's key on it plays for
    int32_t lvL = 0, lvR = 0, env = 0;           // the voice's levels and attack (the last ones once it is another's)
    bool dl = false, dr = false, wl = false, wr = false;   // its mix: dry, to the reverb
    uint64_t cut = 0;
    int fade = -1;                               // samples left of its fade when cut (-1: none)
    bool done = false;
};
std::vector<Sub> gSubs;
constexpr int kCutFade = 240;                    // 5 ms

int32_t Vol(uint16_t r) {                        // a fixed volume register's level
    if (r & 0x8000) return 0x7fff;
    return (int32_t)(int16_t)(r << 1);           // -0x8000 .. 0x7ffe
}
void SweepSet(Sweep& w, uint16_t r) {
    if (!(r & 0x8000)) { w.on = false; w.level = (int32_t)(int16_t)(r << 1); return; }
    w.on = true; w.expo = (r & 0x4000) != 0; w.dec = (r & 0x2000) != 0; w.neg = (r & 0x1000) != 0; w.rate = r & 0x7f; w.count = 0;
    w.level = std::abs(w.level);
    if (w.level > 0x7fff) w.level = 0x7fff;
}
void SweepTick(Sweep& w) {
    if (!w.on) return;
    const int shift = w.rate >> 2;
    int step = w.dec ? -8 + (w.rate & 3) : 7 - (w.rate & 3);
    uint32_t cycles = 1u << std::max(0, shift - 11);
    step = shift < 11 ? step << (11 - shift) : step;
    int32_t m = w.neg ? -w.level : w.level;
    if (w.expo && !w.dec && m > 0x6000) cycles *= 4;
    if (w.expo && w.dec) step = (step * m) >> 15;
    if (++w.count < cycles) return;
    w.count = 0;
    m = std::clamp(m + step, 0, 0x7fff);
    w.level = w.neg ? -m : m;
}
// the noise generator's next level (psx-spx: SPU Noise Generator), clocked by the core's noise clock
void NoiseTick(Core& c) {
    const uint32_t clk = c.attr[4] & 0x3f;
    const int step = 4 + (int)(clk & 3), shift = (int)(clk >> 2);
    c.noiseTimer -= step;
    const int parity = ((c.noiseLevel >> 15) ^ (c.noiseLevel >> 12) ^ (c.noiseLevel >> 11) ^ (c.noiseLevel >> 10) ^ 1) & 1;
    if (c.noiseTimer < 0) c.noiseLevel = ((c.noiseLevel << 1) | parity) & 0xffff;
    if (c.noiseTimer < 0) c.noiseTimer += 0x20000 >> shift;
    if (c.noiseTimer < 0) c.noiseTimer += 0x20000 >> shift;
}
// one ADPCM block of 16 bytes: shift / filter, flags, 28 samples of 4 bits
void Decode(Voice& v) {
    static const int f0[5] = {0, 60, 115, 98, 122}, f1[5] = {0, 0, -52, -55, -60};
    const uint8_t* b = gRam + (v.cur & (kRamSize - 16));
    const int shift = b[0] & 15, filter = std::min(4, b[0] >> 4);
    v.flags = b[1];
    for (int k = 0; k < 28; ++k) {
        int n = (b[2 + k / 2] >> ((k & 1) * 4)) & 15;
        int32_t x = (int16_t)(n << 12) >> std::min(shift, 12);
        x += (v.h1 * f0[filter] + v.h2 * f1[filter] + 32) >> 6;
        x = std::clamp(x, -32768, 32767);
        v.pcm[k] = (int16_t)x;
        v.h2 = v.h1; v.h1 = x;
    }
    if ((v.flags & 4) && !v.lsaManual) v.lsa = v.cur;       // loop start
    v.idx = 0;
}
int16_t NextSample(Core& c, int vi) {
    Voice& v = c.v[vi];
    if (v.idx >= 28) Decode(v);
    const int16_t x = v.pcm[v.idx++];
    if (v.idx >= 28) {                                       // the block ends: its loop flags
        if (v.flags & 1) {
            c.endx |= 1u << vi;
            if (v.flags & 2) v.cur = v.lsa;
            else { v.phase = 4; v.env = 0; v.on = false; v.cur = v.lsa; }
        } else v.cur = (v.cur + 16) & (kRamSize - 1);
    }
    return x;
}
// the envelope's step at a rate (psx-spx: SPU ADSR)
void EnvStep(Voice& v, int rate, bool decrease, bool expo) {
    const int shift = rate >> 2;
    int step = decrease ? -8 + (rate & 3) : 7 - (rate & 3);
    uint32_t cycles = 1u << std::max(0, shift - 11);
    step = shift < 11 ? step << (11 - shift) : step;
    if (expo && !decrease && v.env > 0x6000) cycles *= 4;
    if (expo && decrease) step = (step * v.env) >> 15;
    if (++v.envCount < cycles) return;
    v.envCount = 0;
    v.env = std::clamp(v.env + step, 0, 0x7fff);
}
void Envelope(Voice& v) {
    switch (v.phase) {
    case 0:
        EnvStep(v, (v.adsr1 >> 8) & 0x7f, false, (v.adsr1 & 0x8000) != 0);
        if (v.env >= 0x7fff) { v.env = 0x7fff; v.phase = 1; v.envCount = 0; }
        break;
    case 1: {
        EnvStep(v, ((v.adsr1 >> 4) & 15) * 4, true, true);
        const int32_t sl = ((v.adsr1 & 15) + 1) * 0x800;
        if (v.env <= sl) { v.phase = 2; v.envCount = 0; }
        break; }
    case 2:
        EnvStep(v, (v.adsr2 >> 6) & 0x7f, (v.adsr2 & 0x4000) != 0, (v.adsr2 & 0x8000) != 0);
        break;
    case 3:
        EnvStep(v, (v.adsr2 & 31) * 4, true, (v.adsr2 & 0x20) != 0);
        if (v.env <= 0) { v.env = 0; v.phase = 4; v.on = false; }
        break;
    default: break;
    }
}
void (*gLog)(const char* fmt, ...) = nullptr;
int gKeyLog = 0;                                 // key ons still to log (SetLog)
long long gKeyLogFrom = 0;                       // from that sample on
int32_t Clamp16(int32_t x) { return std::clamp(x, -32768, 32767); }
int32_t Mul15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
// the reverb's resampling filter (psx-spx: 39 taps between 48 and 24 kHz)
const int32_t kFir[39] = {-1, 0, 2, 0, -10, 0, 35, 0, -103, 0, 266, 0, -616, 0, 1332, 0, -2960, 0, 10246, 16384,
                          10246, 0, -2960, 0, 1332, 0, -616, 0, 266, 0, -103, 0, 35, 0, -10, 0, 2, 0, -1};
// a work area position (halfwords): an offset from the running position, wrapped into [ESA, EEA]
uint32_t RvAt(const Core& c, int64_t off) {
    const uint32_t start = c.esaH & 0xfffff, end = ((c.eea >> 1) & 0xf0000) | 0xffff;
    if (end <= start) return start;
    const int64_t size = (int64_t)end - start + 1;
    int64_t x = ((int64_t)(c.cycles >> 1) + off) % size;
    if (x < 0) x += size;
    return (uint32_t)(start + x) & 0xfffff;
}
// one sample of a core's reverb: left on even samples, right on odd ones (24 kHz each)
void Reverb(Core& c, int32_t inL, int32_t inR, int32_t& outL, int32_t& outR) {
    c.down[0][c.bufPos] = c.down[0][c.bufPos | 64] = inL;
    c.down[1][c.bufPos] = c.down[1][c.bufPos | 64] = inR;
    const bool R = c.cycles & 1;
    int16_t* m = reinterpret_cast<int16_t*>(gRam);
    const uint32_t sameDstOff = R ? c.sameRDst : c.sameLDst, diffDstOff = R ? c.diffRDst : c.diffLDst;
    const uint32_t sameSrc = RvAt(c, R ? c.sameRSrc : c.sameLSrc), sameDst = RvAt(c, sameDstOff), samePrv = RvAt(c, (int64_t)sameDstOff - 1);
    const uint32_t diffSrc = RvAt(c, R ? c.diffLSrc : c.diffRSrc), diffDst = RvAt(c, diffDstOff), diffPrv = RvAt(c, (int64_t)diffDstOff - 1);
    const uint32_t c1 = RvAt(c, R ? c.comb1R : c.comb1L), c2 = RvAt(c, R ? c.comb2R : c.comb2L);
    const uint32_t c3 = RvAt(c, R ? c.comb3R : c.comb3L), c4 = RvAt(c, R ? c.comb4R : c.comb4L);
    const uint32_t apf1DstOff = R ? c.apf1RDst : c.apf1LDst, apf2DstOff = R ? c.apf2RDst : c.apf2LDst;
    const uint32_t apf1Src = RvAt(c, (int64_t)apf1DstOff - c.apf1Size), apf1Dst = RvAt(c, apf1DstOff);
    const uint32_t apf2Src = RvAt(c, (int64_t)apf2DstOff - c.apf2Size), apf2Dst = RvAt(c, apf2DstOff);
    int idx = (c.bufPos - 39) & 63;
    int64_t acc = 0;
    for (int i = 0; i < 39; ++i) acc += (int64_t)c.down[R][idx + i] * kFir[i];
    const int32_t in = Mul15(R ? c.vInR : c.vInL, Clamp16((int32_t)(acc >> 15)));
    const int32_t same = Mul15(c.vIIR, in + Mul15(c.vWall, m[sameSrc]) - m[samePrv]) + m[samePrv];
    const int32_t diff = Mul15(c.vIIR, in + Mul15(c.vWall, m[diffSrc]) - m[diffPrv]) + m[diffPrv];
    int32_t o = Mul15(c.vComb1, m[c1]) + Mul15(c.vComb2, m[c2]) + Mul15(c.vComb3, m[c3]) + Mul15(c.vComb4, m[c4]);
    const int32_t apf1 = o - Mul15(c.vApf1, m[apf1Src]);
    o = m[apf1Src] + Mul15(c.vApf1, apf1);
    const int32_t apf2 = o - Mul15(c.vApf2, m[apf2Src]);
    o = m[apf2Src] + Mul15(c.vApf2, apf2);
    if (c.attr[1]) {                                     // effect enable: the work area is written
        m[sameDst] = (int16_t)Clamp16(same); m[diffDst] = (int16_t)Clamp16(diff);
        m[apf1Dst] = (int16_t)Clamp16(apf1); m[apf2Dst] = (int16_t)Clamp16(apf2);
    }
    o = Clamp16(o);
    c.up[R][c.bufPos] = c.up[R][c.bufPos | 64] = o;
    c.up[!R][c.bufPos] = c.up[!R][c.bufPos | 64] = 0;
    c.bufPos = (c.bufPos + 1) & 63;
    idx = (c.bufPos - 39) & 63;
    int64_t l = 0, r = 0;
    for (int i = 0; i < 39; ++i) { l += (int64_t)c.up[0][idx + i] * kFir[i]; r += (int64_t)c.up[1][idx + i] * kFir[i]; }
    outL = Clamp16((int32_t)(l >> 14)); outR = Clamp16((int32_t)(r >> 14));
}

void KeyOn(Core& c, int vi) {
    Voice& v = c.v[vi];
    if (gKeyLog > 0 && gLog && gSamples >= gKeyLogFrom) {
        --gKeyLog;
        const uint8_t* b = gRam + (v.ssa & (kRamSize - 16));
        gLog("spu2: %.3f s: key on core %d voice %d: start %06x, pitch %04x, adsr %04x %04x, volume %04x %04x, first block %02x %02x, noise %d",
             gSamples / 48000.0, (int)(&c - gCore), vi, v.ssa, v.pitch, v.adsr1, v.adsr2, v.volL, v.volR, b[0], b[1], (int)(c.non >> vi & 1));
    }
    v.on = true; v.cur = v.ssa & (kRamSize - 16); v.idx = 28; v.h1 = v.h2 = 0; v.frac = 0;
    memset(v.s, 0, sizeof v.s);
    v.phase = 0; v.env = 0; v.envCount = 0; v.lsaManual = false;
    c.endx &= ~(1u << vi);
    ++gKeyOns;
    v.gen = ++gGen;
    v.mute = false;
    Tag& t = gTag[&c - gCore][vi];
    if (t.set && gSamples - t.at > 2400) t = Tag();             // set for a note not keyed on within 50 ms: not this one's
    if (t.set) {
        v.mute = true;
        if (t.pcm && !t.pcm->samples.empty() && t.pcm->rate) {
            if (t.cut)
                for (Sub& o : gSubs) if (o.cut == t.cut && o.fade < 0) o.fade = kCutFade;
            Sub s;
            s.pcm = t.pcm; s.step = t.pcm->rate / 48000.0; s.gain = t.gain; s.core = (int)(&c - gCore); s.voice = vi; s.gen = v.gen; s.cut = t.cut;
            gSubs.push_back(s);
            if (gLog && gKeyLog > 0) gLog("spu2: %.3f s: core %d voice %d plays a sound of its own (%zu samples at %u Hz)", gSamples / 48000.0,
                                         s.core, vi, t.pcm->samples.size(), t.pcm->rate);
        }
        t = Tag();
    }
}
// the substitutes of a core, one sample: added to its voices' dry and reverb sums
void SubsSample(Core& c, int ci, int32_t& sdl, int32_t& sdr, int32_t& swl, int32_t& swr) {
    for (Sub& s : gSubs) {
        if (s.core != ci || s.done) continue;
        const Voice& v = c.v[s.voice];
        if (v.gen == s.gen) {
            s.lvL = v.swL.level; s.lvR = v.swR.level;
            s.env = v.phase == 0 ? v.env : 0x7fff;              // the voice's attack, then full (its release is the driver's
            s.dl = c.vmixl >> s.voice & 1; s.dr = c.vmixr >> s.voice & 1;   // note, shorter than the sound)
            s.wl = c.vmixel >> s.voice & 1; s.wr = c.vmixer >> s.voice & 1;
        }
        const std::vector<int16_t>& a = s.pcm->samples;
        const size_t i = (size_t)s.pos;
        if (i >= a.size()) { s.done = true; continue; }
        const float f = (float)(s.pos - (double)i), x0 = a[i], x1 = i + 1 < a.size() ? a[i + 1] : 0.0f;
        float x = (x0 + (x1 - x0) * f) * s.gain;
        if (s.fade >= 0) { x = x * (float)s.fade / kCutFade; if (--s.fade <= 0) s.done = true; }
        const int32_t y = (Clamp16((int32_t)x) * s.env) >> 15;
        const int32_t l = (y * s.lvL) >> 15, r = (y * s.lvR) >> 15;
        if (s.dl) sdl += l;
        if (s.dr) sdr += r;
        if (s.wl) swl += l;
        if (s.wr) swr += r;
        s.pos += s.step;
    }
}
// one voice's next output sample at its pitch (4-point cubic between the decoded samples)
int32_t VoiceSample(Core& c, int vi) {
    Voice& v = c.v[vi];
    const uint32_t step = std::min<uint32_t>(v.pitch, 0x3fff);
    v.frac += step;
    while (v.frac >= 0x1000 && v.on) {
        v.frac -= 0x1000;
        v.s[0] = v.s[1]; v.s[1] = v.s[2]; v.s[2] = v.s[3];
        v.s[3] = NextSample(c, vi);
    }
    const float t = (v.frac & 0xfff) / 4096.0f;
    const float p0 = v.s[0], p1 = v.s[1], p2 = v.s[2], p3 = v.s[3];
    const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3, b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3, cc = -0.5f * p0 + 0.5f * p2;
    const float y = ((a * t + b) * t + cc) * t + p1;
    return std::clamp((int32_t)y, -32768, 32767);
}

}  // namespace

void SetLog(void (*log)(const char* fmt, ...), int keyOns, double fromSeconds) { gLog = log; gKeyLog = keyOns; gKeyLogFrom = (long long)(fromSeconds * 48000); }
void Reset() {
    std::lock_guard<std::mutex> g(gLock);
    for (auto& c : gCore) c = Core();
    memset(gRam, 0, sizeof gRam);
    for (auto& core : gTag) for (auto& t : core) t = Tag();
    gSubs.clear();
}
uint8_t* Ram() { return gRam; }

void SetParam(uint32_t entry, uint32_t value) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[entry & 1];
    Voice& v = c.v[((entry >> 1) & 31) % 24];
    const uint16_t x = (uint16_t)value;
    switch ((entry >> 8) & 0xff) {
    case 0x00: v.volL = x; SweepSet(v.swL, x); break;
    case 0x01: v.volR = x; SweepSet(v.swR, x); break;
    case 0x02: v.pitch = x; break;
    case 0x03: v.adsr1 = x; break;
    case 0x04: v.adsr2 = x; break;
    case 0x05: v.env = x & 0x7fff; break;
    case 0x08: c.mmix = x; break;
    case 0x09: c.mvolL = x; SweepSet(c.mvL, x); break;
    case 0x0a: c.mvolR = x; SweepSet(c.mvR, x); break;
    case 0x0b: c.evolL = x; break;
    case 0x0c: c.evolR = x; break;
    case 0x0d: c.avolL = x; break;
    case 0x0e: c.avolR = x; break;
    case 0x0f: c.bvolL = x; break;
    case 0x10: c.bvolR = x; break;
    default: break;
    }
}
uint32_t GetParam(uint32_t entry) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[entry & 1];
    Voice& v = c.v[((entry >> 1) & 31) % 24];
    switch ((entry >> 8) & 0xff) {
    case 0x00: return v.volL;
    case 0x01: return v.volR;
    case 0x02: return v.pitch;
    case 0x03: return v.adsr1;
    case 0x04: return v.adsr2;
    case 0x05: return (uint32_t)v.env;
    case 0x06: return (uint32_t)(uint16_t)(v.swL.level >> 1);   // VOLXL / VOLXR: the levels now
    case 0x07: return (uint32_t)(uint16_t)(v.swR.level >> 1);
    case 0x08: return c.mmix;
    case 0x09: return c.mvolL;
    case 0x0a: return c.mvolR;
    case 0x0b: return c.evolL;
    case 0x0c: return c.evolR;
    case 0x0d: return c.avolL;
    case 0x0e: return c.avolR;
    case 0x0f: return c.bvolL;
    case 0x10: return c.bvolR;
    case 0x11: return (uint32_t)(uint16_t)(c.mvL.level >> 1);  // MVOLXL / MVOLXR: the levels now
    case 0x12: return (uint32_t)(uint16_t)(c.mvR.level >> 1);
    }
    return 0;
}
void SetSwitch(uint32_t entry, uint32_t value) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[entry & 1];
    value &= 0xffffff;
    switch ((entry >> 8) & 0xff) {
    case 0x13: c.pmon = value; break;
    case 0x14:
        if (value != c.non && gLog) {                    // the noise voices (the first changes)
            static int told = 0;
            if (told++ < 8) gLog("spu2: %.3f s: core %d noise voices %06x (clock %02x)", gSamples / 48000.0, (int)(&c - gCore), value, c.attr[4] & 0x3f);
        }
        c.non = value;
        break;
    case 0x15: for (int k = 0; k < 24; ++k) if (value >> k & 1) KeyOn(c, k); break;
    case 0x16:
        for (int k = 0; k < 24; ++k) if ((value >> k & 1) && c.v[k].phase < 3) { c.v[k].phase = 3; c.v[k].envCount = 0; }
        if (gKeyLog > 0 && gLog && value) { --gKeyLog; gLog("spu2: %.3f s: key off core %d voices %06x", gSamples / 48000.0, (int)(&c - gCore), value); }
        break;
    case 0x17: c.endx = value; break;
    case 0x18: c.vmixl = value; break;
    case 0x19: c.vmixel = value; break;
    case 0x1a: c.vmixr = value; break;
    case 0x1b: c.vmixer = value; break;
    }
}
uint32_t GetSwitch(uint32_t entry) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[entry & 1];
    switch ((entry >> 8) & 0xff) {
    case 0x13: return c.pmon;
    case 0x14: return c.non;
    case 0x17: return c.endx;
    case 0x18: return c.vmixl;
    case 0x19: return c.vmixel;
    case 0x1a: return c.vmixr;
    case 0x1b: return c.vmixer;
    }
    return 0;
}
void SetAddr(uint32_t entry, uint32_t value) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[entry & 1];
    Voice& v = c.v[((entry >> 1) & 31) % 24];
    value &= kRamSize - 1;
    switch ((entry >> 8) & 0xff) {
    case 0x1c: c.esa = value; break;
    case 0x1d: c.eea = value; break;
    case 0x1e: c.tsa = value; break;
    case 0x1f: c.irqa = value; break;
    case 0x20: v.ssa = value & ~15u; break;
    case 0x21: v.lsa = value & ~15u; v.lsaManual = true; break;
    case 0x22: v.cur = value & ~15u; v.idx = 28; break;
    }
}
uint32_t GetAddr(uint32_t entry) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[entry & 1];
    Voice& v = c.v[((entry >> 1) & 31) % 24];
    switch ((entry >> 8) & 0xff) {
    case 0x1c: return c.esa;
    case 0x1d: return c.eea;
    case 0x1e: return c.tsa;
    case 0x1f: return c.irqa;
    case 0x20: return v.ssa;
    case 0x21: return v.lsa;
    case 0x22: return v.cur;
    }
    return 0;
}
void SetCoreAttr(uint32_t entry, uint32_t value) {
    std::lock_guard<std::mutex> g(gLock);
    gCore[entry & 1].attr[(entry >> 1) & 7] = value;
}
uint32_t GetCoreAttr(uint32_t entry) {
    std::lock_guard<std::mutex> g(gLock);
    return gCore[entry & 1].attr[(entry >> 1) & 7];
}
void SetReverb(int core, const uint16_t r[32], uint32_t esa) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[core & 1];
    auto a = [&](int k) { return (uint32_t)r[k] * 4; };                       // 8-byte units to halfwords
    auto v = [&](int k) { return (int32_t)(int16_t)r[k]; };
    c.apf1Size = a(0); c.apf2Size = a(1);
    c.vIIR = v(2); c.vComb1 = v(3); c.vComb2 = v(4); c.vComb3 = v(5); c.vComb4 = v(6); c.vWall = v(7); c.vApf1 = v(8); c.vApf2 = v(9);
    c.sameLDst = a(10); c.sameRDst = a(11); c.comb1L = a(12); c.comb1R = a(13); c.comb2L = a(14); c.comb2R = a(15);
    c.sameLSrc = a(16); c.sameRSrc = a(17); c.diffLDst = a(18); c.diffRDst = a(19); c.comb3L = a(20); c.comb3R = a(21);
    c.comb4L = a(22); c.comb4R = a(23); c.diffLSrc = a(24); c.diffRSrc = a(25);
    c.apf1LDst = a(26); c.apf1RDst = a(27); c.apf2LDst = a(28); c.apf2RDst = a(29);
    c.vInL = v(30); c.vInR = v(31);
    c.esa = esa & (kRamSize - 1); c.esaH = c.esa >> 1;
    c.rvSet = true;
}
void Clear(uint32_t from, uint32_t to) {
    std::lock_guard<std::mutex> g(gLock);
    from &= kRamSize - 1; to &= kRamSize - 1;
    if (to >= from) memset(gRam + from, 0, to - from + 1);
}

void BlockStart(int core, const uint8_t* mem, uint32_t iopAddr, uint32_t size, bool loop) {
    std::lock_guard<std::mutex> g(gLock);
    Core& c = gCore[core & 1];
    c.block = size >= 1024; c.blockLoop = loop; c.mem = mem; c.base = iopAddr; c.size = size & ~1023u; c.pos = 0;
}
void BlockStop(int core) { std::lock_guard<std::mutex> g(gLock); gCore[core & 1].block = false; }
uint32_t BlockStatus(int core) {
    std::lock_guard<std::mutex> g(gLock);
    const Core& c = gCore[core & 1];
    if (!c.block) return 0;
    const uint32_t bytes = c.pos * 4;                        // 2 bytes left + 2 right a sample
    return (bytes >= c.size / 2 ? 1u << 24 : 0) | ((c.base + bytes) & 0xffffff);
}

void Write(uint32_t spuAddr, const uint8_t* src, uint32_t size) {
    std::lock_guard<std::mutex> g(gLock);
    for (uint32_t k = 0; k < size; ++k) gRam[(spuAddr + k) & (kRamSize - 1)] = src[k];
}
void Read(uint32_t spuAddr, uint8_t* dst, uint32_t size) {
    std::lock_guard<std::mutex> g(gLock);
    for (uint32_t k = 0; k < size; ++k) dst[k] = gRam[(spuAddr + k) & (kRamSize - 1)];
}

void Mix(int16_t* out, int frames, void (*onHalf)(int core)) {
    for (int f = 0; f < frames; ++f) {
        int halfDone = -1;
        int32_t outL = 0, outR = 0;
        {
            std::lock_guard<std::mutex> g(gLock);
            int32_t extL = 0, extR = 0;                          // core 0's output, core 1's external input
            for (int ci = 0; ci < 2; ++ci) {
                Core& c = gCore[ci];
                int32_t sdl = 0, sdr = 0, swl = 0, swr = 0;       // the voices, dry and to the reverb
                NoiseTick(c);
                for (int vi = 0; vi < 24; ++vi) {
                    Voice& v = c.v[vi];
                    SweepTick(v.swL); SweepTick(v.swR);
                    if (!v.on && v.phase == 4) continue;
                    int32_t x = v.on ? VoiceSample(c, vi) : 0;       // its blocks run on for their flags
                    if (v.on && (c.non >> vi & 1)) x = (int32_t)(int16_t)c.noiseLevel;
                    Envelope(v);
                    if (v.mute) continue;                            // a substitute plays in its place
                    const int32_t y = (x * v.env) >> 15;
                    const int32_t l = (y * v.swL.level) >> 15, r = (y * v.swR.level) >> 15;
                    if (c.vmixl >> vi & 1) sdl += l;
                    if (c.vmixr >> vi & 1) sdr += r;
                    if (c.vmixel >> vi & 1) swl += l;
                    if (c.vmixer >> vi & 1) swr += r;
                }
                if (!gSubs.empty()) SubsSample(c, ci, sdl, sdr, swl, swr);
                // the block input (ADMA, the "sound data" input): 1024-byte blocks (a half), 256 left
                // samples then 256 right, at BVOL (sdlib: 0x7FFF on core 0, whose input it feeds; 0 on core 1)
                int32_t il = 0, ir = 0;
                if (c.block && c.mem) {
                    if (c.pos == 0 || c.pos == c.size / 8) HalfStarts(c, c.pos ? 1 : 0);
                    const uint32_t blk = (c.pos / 256) * 1024, i = c.pos % 256;
                    const uint32_t a = (c.base + blk + 2 * i) & 0x1fffff;
                    int16_t l, r;
                    memcpy(&l, c.mem + a, 2); memcpy(&r, c.mem + ((a + 512) & 0x1fffff), 2);
                    il = (l * (int32_t)(int16_t)c.bvolL) >> 15; ir = (r * (int32_t)(int16_t)c.bvolR) >> 15;
                    const uint32_t samples = c.size / 4;
                    ++c.pos;
                    if (c.pos == samples / 2) halfDone = ci;
                    if (c.pos >= samples) { c.pos = 0; halfDone = ci; if (!c.blockLoop) c.block = false; }
                }
                // core 1's external input: core 0's output at AVOL
                int32_t el = 0, er = 0;
                if (ci == 1) { el = (extL * (int32_t)(int16_t)c.avolL) >> 15; er = (extR * (int32_t)(int16_t)c.avolR) >> 15; }
                // MMIX: which of them go out dry and which to the reverb
                const uint32_t mm = c.mmix;
                int32_t dryL = (mm & 0x800 ? sdl : 0) + (mm & 0x80 ? il : 0) + (mm & 0x8 ? el : 0);
                int32_t dryR = (mm & 0x400 ? sdr : 0) + (mm & 0x40 ? ir : 0) + (mm & 0x4 ? er : 0);
                if (c.rvSet) {
                    const int32_t wetL = (mm & 0x200 ? swl : 0) + (mm & 0x20 ? il : 0) + (mm & 0x2 ? el : 0);
                    const int32_t wetR = (mm & 0x100 ? swr : 0) + (mm & 0x10 ? ir : 0) + (mm & 0x1 ? er : 0);
                    int32_t rl, rr;
                    Reverb(c, Clamp16(wetL), Clamp16(wetR), rl, rr);
                    dryL += (rl * (int32_t)(int16_t)c.evolL) >> 15;
                    dryR += (rr * (int32_t)(int16_t)c.evolR) >> 15;
                }
                ++c.cycles;
                SweepTick(c.mvL); SweepTick(c.mvR);
                const int32_t ol = Clamp16((Clamp16(dryL) * c.mvL.level) >> 15), orr = Clamp16((Clamp16(dryR) * c.mvR.level) >> 15);
                if (ci == 0) { extL = ol; extR = orr; } else { outL = ol; outR = orr; }
            }
        }
        ++gSamples;
        if (!gSubs.empty() && (gSamples & 255) == 0) {
            std::lock_guard<std::mutex> g(gLock);
            gSubs.erase(std::remove_if(gSubs.begin(), gSubs.end(), [](const Sub& s) { return s.done; }), gSubs.end());
        }
        if (halfDone >= 0) { ++gHalves; if (onHalf) onHalf(halfDone); }
        out[2 * f] = (int16_t)std::clamp(out[2 * f] + outL, -32768, 32767);
        out[2 * f + 1] = (int16_t)std::clamp(out[2 * f + 1] + outR, -32768, 32767);
    }
}

void Substitute(int core, int voice, std::shared_ptr<const Pcm> pcm, float gain, uint64_t cut) {
    std::lock_guard<std::mutex> g(gLock);
    if (voice < 0 || voice >= 24) return;
    Tag& t = gTag[core & 1][voice];
    t.set = true; t.pcm = std::move(pcm); t.gain = gain; t.cut = cut; t.at = gSamples;
}
int Substitutes() {
    std::lock_guard<std::mutex> g(gLock);
    int n = 0;
    for (const Sub& s : gSubs) if (!s.done) ++n;
    return n;
}

std::string Stats() {
    std::lock_guard<std::mutex> g(gLock);
    int on = 0;
    for (auto& c : gCore) for (auto& v : c.v) if (v.on) ++on;
    char b[240];
    snprintf(b, sizeof b, "%d voices on, %lld key ons, %lld block halves (%lld played again unchanged), core volumes %04x/%04x %04x/%04x, block %d/%d",
             on, gKeyOns, gHalves, gStaleHalves, gCore[0].mvolL, gCore[0].mvolR, gCore[1].mvolL, gCore[1].mvolR, (int)gCore[0].block, (int)gCore[1].block);
    return b;
}

}  // namespace spu2
