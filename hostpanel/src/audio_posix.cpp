// The menu sounds on Linux and macOS (platform.h SoundPlay ...): miniaudio (third_party/miniaudio, its
// playback device only: PulseAudio (PipeWire's too) or ALSA loaded at run time on Linux, Core Audio on
// macOS; none: silence). One sound at a time as PlaySound does; the device opened at the first
// sound, stopped when quiet.
#ifndef _WIN32
#include "platform.h"
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#ifdef __APPLE__
#define MA_NO_RUNTIME_LINKING                                  // Core Audio linked (build_macos.sh): notarization allows it
#endif
#define MINIAUDIO_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "miniaudio.h"
#pragma GCC diagnostic pop
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace plat {
namespace {

struct Out {
    std::mutex mu;                           // the sound and its place (the device's thread reads them)
    std::vector<int16_t> pcm;                // 48 kHz stereo
    size_t at = 0;
    ma_context ctx;
    ma_device dev;
    bool open = false, started = false;
    uint64_t failedAt = 0;                   // no device: not tried again for 10 s
    std::atomic<uint64_t> quietSince{0};     // the last sound's end (TickMs)
};
Out& O() { static Out* o = new Out; return *o; }   // (never freed: the device's thread may run at exit)

void Feed(ma_device*, void* output, const void*, ma_uint32 frames) {
    Out& o = O();
    int16_t* dst = (int16_t*)output;
    std::lock_guard<std::mutex> g(o.mu);
    const size_t want = (size_t)frames * 2, left = o.pcm.size() > o.at ? o.pcm.size() - o.at : 0, n = want < left ? want : left;
    if (n) memcpy(dst, o.pcm.data() + o.at, n * sizeof(int16_t));
    if (n < want) memset(dst + n, 0, (want - n) * sizeof(int16_t));
    o.at += n;
    if (n && o.at >= o.pcm.size()) o.quietSince = TickMs();
}

bool Ready(Out& o) {
    if (o.open) return true;
    if (o.failedAt && TickMs() - o.failedAt < 10000) return false;
#ifdef __APPLE__
    if (ma_context_init(nullptr, 0, nullptr, &o.ctx) != MA_SUCCESS) { o.failedAt = TickMs(); return false; }
#else
    const ma_backend backends[] = {ma_backend_pulseaudio, ma_backend_alsa, ma_backend_null};
    if (ma_context_init(backends, 3, nullptr, &o.ctx) != MA_SUCCESS) { o.failedAt = TickMs(); return false; }
#endif
    ma_device_config c = ma_device_config_init(ma_device_type_playback);
    c.playback.format = ma_format_s16;
    c.playback.channels = 2;
    c.sampleRate = 48000;
    c.dataCallback = Feed;
    if (ma_device_init(&o.ctx, &c, &o.dev) != MA_SUCCESS) { ma_context_uninit(&o.ctx); o.failedAt = TickMs(); return false; }
    o.open = true;
    return true;
}

}  // namespace

void SoundPlay(const unsigned char* wav, size_t size) {
    if (size <= 44 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 36, "data", 4) != 0) return;
    Out& o = O();
    {
        std::lock_guard<std::mutex> g(o.mu);
        o.pcm.assign((const int16_t*)(wav + 44), (const int16_t*)(wav + 44) + (size - 44) / sizeof(int16_t));
        o.at = 0;
        o.quietSince = 0;
    }
    if (!Ready(o)) return;
    if (!o.started) o.started = ma_device_start(&o.dev) == MA_SUCCESS;
}

void SoundStop() {
    Out& o = O();
    std::lock_guard<std::mutex> g(o.mu);
    o.pcm.clear();
    o.at = 0;
    if (!o.quietSince) o.quietSince = TickMs();
}

void SoundIdle() {
    Out& o = O();
    const uint64_t q = o.quietSince;
    if (o.started && q && TickMs() - q > 5000) { ma_device_stop(&o.dev); o.started = false; }
}

void SoundShutdown() {
    Out& o = O();
    SoundStop();
    if (o.open) { ma_device_uninit(&o.dev); ma_context_uninit(&o.ctx); o.open = o.started = false; }
}

}  // namespace plat
#endif
