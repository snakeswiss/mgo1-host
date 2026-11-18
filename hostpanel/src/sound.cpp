// MGO1's menu sounds, rendered from the user's disc (sound.h). The system sound bank sits in
// STAGE_N.DAT as the game loads it at sound memory 0x20000; it is found by the hash of 64 bytes of a
// sample (as MGO1.exe's title finds its Start sound: MGO1Loader/iop.cpp RenderStartSound), loaded at
// its place in the SPU2's memory, and each sound's voices are keyed on and off with the settings the
// game's sound driver gives them (measured: docs/SOUND.md, "The menus' sounds in MGO1 Host.exe").
#include "sound.h"
#include "platform.h"
#include "remote.h"
#include "../../MGO1Loader/spu2.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace sound {
namespace {

struct Voice { int core, voice; uint32_t start, pitch, adsr1, adsr2, volL, volR; int onMs, offMs; uint64_t head; };
struct Effect { const char* name; Voice v[4]; int n; };

// measured on the US disc (IOP_KEY_LOG=600 in the log-in and lobby menus, tests/steps and the check
// steps of docs/SOUND.md): each voice's start in sound memory, its pitch, envelope (ADSR1, ADSR2) and
// volumes as the driver keys it on, its key on and key off (ms); head: the hash of the sample's first
// 64 bytes (another disc whose bank differs: that sound left out). The cursor: Up or Down in a menu; OK:
// a choice made (the title's Start sound, the same two voices); cancel: back out of a screen
const Effect kEffects[COUNT] = {
    {"cursor", {{1, 20, 0x20080, 0x3275, 0x1b06, 0x4f46, 0x0f53, 0x0f53, 0, 70, 0xdc1a9327e4c63cf1ull}}, 1},
    {"ok", {{0, 0, 0x238f0, 0x2052, 0x2d14, 0x4ecf, 0x08e3, 0x0819, 0, 390, 0x1e8b9437b4218137ull},
            {1, 21, 0x25450, 0x2acc, 0x2f03, 0x4eca, 0x0b21, 0x0b21, 0, 110, 0x50e9c54f51a4897cull}}, 2},
    {"cancel", {{1, 12, 0x238f0, 0x3a44, 0x22ff, 0x4bce, 0x0165, 0x0165, 0, 170, 0x1e8b9437b4218137ull},
                {1, 13, 0x26f50, 0x1a92, 0x25ff, 0x4c0e, 0x01e0, 0x01b5, 0, 215, 0x0f03c347c5bbe7d4ull}}, 2},
};
constexpr int kGain = 5;                  // at 100 %: kGain / 2 times the game's level (its menus peak at -15 dBFS)

constexpr uint32_t kBank = 0x20000, kBankSize = 0x20000;        // the bank's span loaded (128 KB)
constexpr uint64_t kAnchor = 0x7f5e18537954e84eull;              // 64 bytes at 0x23900 (RenderStartSound)
constexpr int kRate = 48000;

uint64_t Fnv64(const uint8_t* p) { uint64_t h = 0xcbf29ce484222325ull; for (int k = 0; k < 64; ++k) h = (h ^ p[k]) * 0x100000001b3ull; return h; }

// (never freed: a render thread may still run while the program exits)
struct Shared {
    std::mutex lock, render;             // render: one rendering at a time (the SPU2 is one)
    std::vector<uint8_t> pcm[COUNT];     // 48 kHz stereo 16-bit, full level
    std::vector<uint8_t> wav[COUNT];     // RIFF files at the volume set (what plat::SoundPlay plays)
    std::atomic<int> state{NO_DISC};
    std::atomic<unsigned> gen{0};        // Init's count: a render thread of an older folder drops its result
    std::string dir;
    bool on = true;
    int volume = 60;
};
Shared& G() { static Shared* s = new Shared; return *s; }

// a RIFF file of the sound at a volume (0..100)
std::vector<uint8_t> MakeWav(const std::vector<uint8_t>& p, int volume) {
    std::vector<uint8_t> w(44 + p.size(), 0);
    auto u32 = [&](size_t at, uint32_t v) { memcpy(&w[at], &v, 4); };
    auto u16 = [&](size_t at, uint16_t v) { memcpy(&w[at], &v, 2); };
    memcpy(&w[0], "RIFF", 4); u32(4, (uint32_t)(36 + p.size())); memcpy(&w[8], "WAVEfmt ", 8);
    u32(16, 16); u16(20, 1); u16(22, 2); u32(24, kRate); u32(28, kRate * 4); u16(32, 4); u16(34, 16);
    memcpy(&w[36], "data", 4); u32(40, (uint32_t)p.size());
    const int gain = std::clamp(volume, 0, 100) * kGain;             // (per 200)
    for (size_t k = 0; k + 1 < p.size(); k += 2) {
        int16_t x; memcpy(&x, &p[k], 2);
        const int16_t o = (int16_t)std::clamp(x * gain / 200, -32768, 32767);
        memcpy(&w[44 + k], &o, 2);
    }
    return w;
}
void BuildWav(int id) { G().wav[id] = MakeWav(G().pcm[id], G().volume); }     // (G().lock held)

// the bank's 128 KB from STAGE_N.DAT (the file read in 1 MB pieces, the anchor searched every 16 bytes)
bool LoadBank(const std::string& path, std::vector<uint8_t>& bank) {
    FILE* f = plat::Open(path, "rb");
    if (!f) return false;
    plat::Seek(f, 0, SEEK_END);
    const long long size = plat::Tell(f);
    long long at0 = -1;
    std::vector<uint8_t> buf((1 << 20) + 64);
    for (long long at = 0; at < size && at0 < 0; at += 1 << 20) {
        const size_t want = (size_t)std::min<long long>((1 << 20) + 64, size - at);
        if (plat::Seek(f, at, SEEK_SET) != 0 || fread(buf.data(), 1, want, f) != want) break;
        for (size_t k = 0; k + 64 <= want && k < (1u << 20); k += 16)
            if (Fnv64(&buf[k]) == kAnchor) { at0 = at + (long long)k - 0x3900; break; }
    }
    bool ok = false;
    if (at0 >= 0) {
        bank.assign(kBankSize, 0);
        const size_t n = (size_t)std::min<long long>(kBankSize, size - at0);
        ok = plat::Seek(f, at0, SEEK_SET) == 0 && fread(bank.data(), 1, n, f) == n;
    }
    fclose(f);
    return ok;
}

// one sound: the core's settings as the driver sets them (RenderStartSound), its voices keyed on and
// off at their times, the mix until the last one is silent (2 s at most)
std::vector<uint8_t> Render(const std::vector<uint8_t>& bank, const Effect& e) {
    for (int i = 0; i < e.n; ++i) {
        const Voice& v = e.v[i];
        if (v.start < kBank || v.start + 64 > kBank + kBankSize || Fnv64(&bank[v.start - kBank]) != v.head) return {};
    }
    spu2::Reset();
    spu2::Write(kBank, bank.data(), (uint32_t)bank.size());
    auto en = [](int param, int core, int voice) { return (uint32_t)(param << 8 | voice << 1 | core); };
    for (int core = 0; core < 2; ++core) {
        spu2::SetParam(en(0x09, core, 0), 0x3fff); spu2::SetParam(en(0x0a, core, 0), 0x3fff);
        spu2::SetParam(en(0x08, core, 0), core ? 0xfcc : 0xfcf);
        spu2::SetParam(en(0x0d, core, 0), 0x7fff); spu2::SetParam(en(0x0e, core, 0), 0x7fff);
        spu2::SetSwitch(en(0x18, core, 0), 0xffffff); spu2::SetSwitch(en(0x1a, core, 0), 0xffffff);
    }
    for (int i = 0; i < e.n; ++i) {
        const Voice& v = e.v[i];
        spu2::SetParam(en(0x00, v.core, v.voice), v.volL); spu2::SetParam(en(0x01, v.core, v.voice), v.volR);
        spu2::SetParam(en(0x02, v.core, v.voice), v.pitch);
        spu2::SetParam(en(0x03, v.core, v.voice), v.adsr1); spu2::SetParam(en(0x04, v.core, v.voice), v.adsr2);
        spu2::SetAddr(en(0x20, v.core, v.voice), v.start);
    }
    // the key ons and offs in time order
    struct Ev { int at; bool on; int core, voice; };
    std::vector<Ev> evs;
    int end = 0;
    for (int i = 0; i < e.n; ++i) {
        evs.push_back({e.v[i].onMs * kRate / 1000, true, e.v[i].core, e.v[i].voice});
        evs.push_back({e.v[i].offMs * kRate / 1000, false, e.v[i].core, e.v[i].voice});
        end = std::max(end, e.v[i].offMs * kRate / 1000);
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });
    const int total = end + 2 * kRate;
    std::vector<int16_t> out(2 * (size_t)total, 0);
    int done = 0;
    for (const Ev& x : evs) {
        if (x.at > done) { spu2::Mix(out.data() + 2 * done, x.at - done, nullptr); done = x.at; }
        spu2::SetSwitch(en(x.on ? 0x15 : 0x16, x.core, 0), 1u << x.voice);
    }
    spu2::Mix(out.data() + 2 * done, total - done, nullptr);
    int last = 0;                                                   // the silence at the end cut off
    for (int k = 0; k < total; ++k) if (out[2 * k] || out[2 * k + 1]) last = k + 1;
    spu2::Reset();
    std::vector<uint8_t> pcm(4 * (size_t)last);
    memcpy(pcm.data(), out.data(), pcm.size());
    return pcm;
}

void RenderAll(std::string dir, unsigned gen) {
    Shared& s = G();
    std::string root;
    std::vector<uint8_t> bank;
    std::vector<uint8_t> pcm[COUNT];
    bool any = false;
    {
        std::lock_guard<std::mutex> r(s.render);
        if (gen != s.gen.load()) return;
        if (FindDisc(dir, root) && LoadBank(plat::Find(root, "MGS/NET/STAGE_N.DAT"), bank))
            for (int i = 0; i < COUNT; ++i) { pcm[i] = Render(bank, kEffects[i]); any = any || !pcm[i].empty(); }
    }
    std::lock_guard<std::mutex> g(s.lock);
    if (gen != s.gen.load()) return;
    plat::SoundStop();
    for (int i = 0; i < COUNT; ++i) { s.pcm[i].swap(pcm[i]); BuildWav(i); }
    s.state = any ? READY : NOT_FOUND;
}

}  // namespace

void Init(const std::string& discDir) {
    Shared& s = G();
    std::lock_guard<std::mutex> g(s.lock);
    if (discDir == s.dir && s.state != NO_DISC) return;
    s.dir = discDir;
    const unsigned gen = ++s.gen;
    if (discDir.empty()) { s.state = NO_DISC; return; }
    s.state = SEARCHING;
    std::thread(RenderAll, discDir, gen).detach();
}

void Set(bool on, int volume) {
    Shared& s = G();
    std::lock_guard<std::mutex> g(s.lock);
    volume = std::clamp(volume, 0, 100);
    s.on = on;
    if (volume == s.volume) return;
    s.volume = volume;
    plat::SoundStop();                                              // (not a buffer being played rewritten)
    for (int i = 0; i < COUNT; ++i) if (!s.pcm[i].empty()) BuildWav(i);
}

void Play(Id id) {
    Shared& s = G();
    std::lock_guard<std::mutex> g(s.lock);
    if (!s.on || s.volume <= 0 || s.state != READY || id < 0 || id >= COUNT || s.wav[id].size() <= 44) return;
    plat::SoundPlay(s.wav[id].data(), s.wav[id].size());
}

State Get() { return (State)G().state.load(); }

int WriteFiles(const std::string& discDir, const std::string& outDir) {
    std::lock_guard<std::mutex> r(G().render);
    std::string root;
    std::vector<uint8_t> bank;
    if (!FindDisc(discDir, root) || !LoadBank(plat::Find(root, "MGS/NET/STAGE_N.DAT"), bank)) return 0;
    int n = 0;
    for (int i = 0; i < COUNT; ++i) {
        const std::vector<uint8_t> pcm = Render(bank, kEffects[i]);
        if (pcm.empty()) continue;
        const std::vector<uint8_t> wav = MakeWav(pcm, 100);
        if (FILE* f = plat::Open(plat::Join(outDir, std::string(kEffects[i].name) + ".wav"), "wb")) {
            n += fwrite(wav.data(), 1, wav.size(), f) == wav.size();
            fclose(f);
        }
    }
    return n;
}

void Shutdown() {
    Shared& s = G();
    std::lock_guard<std::mutex> g(s.lock);
    ++s.gen;
    plat::SoundShutdown();
}

}  // namespace sound
