// The voice between PCs (docs/VOICE.md): Opus (third_party/opus, BSD) at 48 kHz, 20 ms a packet.
// OpusTalker codes the microphone (WideChain's samples) while this player talks; OpusStream plays one
// talker: his packets by number in a jitter buffer, decoded 20 ms at a time (a lost one from the next
// one's FEC, else concealed), played while the game plays that talker; OpusListener holds one a player.
// Header only, no Windows: MGO1.exe and the simulation (tests/ps2world_test.cpp) use it.
#pragma once
#include "opus.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace voice {

constexpr int kOpusRate = 48000, kOpusFrame = 960, kOpusMaxPacket = 400, kOpusBitrate = 32000;

// 32 kbps VBR (about 30 used, with the FEC), up to 20 kHz of sound (fullband), speech, the next packet's FEC
// for a lost one (sized for 5 % lost)
class OpusTalker {
public:
    OpusTalker() {
        int e = 0;
        enc_ = opus_encoder_create(kOpusRate, 1, OPUS_APPLICATION_VOIP, &e);
        if (!enc_) return;
        opus_encoder_ctl(enc_, OPUS_SET_BITRATE(kOpusBitrate));
        opus_encoder_ctl(enc_, OPUS_SET_VBR(1));
        opus_encoder_ctl(enc_, OPUS_SET_VBR_CONSTRAINT(1));
        opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(8));
        opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(enc_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
        opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(5));
        opus_encoder_ctl(enc_, OPUS_SET_DTX(0));
    }
    ~OpusTalker() { if (enc_) opus_encoder_destroy(enc_); }
    OpusTalker(const OpusTalker&) = delete;
    OpusTalker& operator=(const OpusTalker&) = delete;
    bool Ok() const { return enc_ != nullptr; }
    void Reset() { if (enc_) opus_encoder_ctl(enc_, OPUS_RESET_STATE); fill_ = 0; }   // a talk starts
    // 48 kHz samples in; fn(packet, bytes) for each 20 ms coded
    template <class F> void Run(const float* x, size_t n, F&& fn) {
        if (!enc_) return;
        for (size_t i = 0; i < n; ++i) {
            frame_[fill_++] = x[i];
            if (fill_ < kOpusFrame) continue;
            fill_ = 0;
            unsigned char pkt[kOpusMaxPacket];
            const int b = opus_encode_float(enc_, frame_, kOpusFrame, pkt, kOpusMaxPacket);
            if (b > 0) fn(pkt, b);
        }
    }
private:
    OpusEncoder* enc_ = nullptr;
    float frame_[kOpusFrame] = {};
    int fill_ = 0;
};

// one talker's packets played. A talk (its number) starts the stream again. The jitter buffer holds a
// target of 3 packets (60 ms) to begin with: playing starts with that many, or 100 ms after the first,
// from the first that came (his game may start playing him later than his packets come: nothing is cut,
// the delay is taken back in his pauses); a packet that comes after its turn raises the target by one
// (10 at most) and the playing waits 20 ms (a concealed frame); 10 s without one lowers it again. With
// more than the target and one waiting, a silent frame (below -50 dBFS) is left out: the delay goes down
// in the pauses, the words are all played. Each 20 ms is the packet of that number, else the next one's
// FEC, else concealed (3 in a row at most while nothing comes: then it waits again); more than 45 waiting
// (a stall) go down to the target. Not played while the game does not play that talker (muted in the
// game, another channel): 400 ms of that and what waits is dropped.
class OpusStream {
public:
    struct Stats { long long packets = 0, decoded = 0, fec = 0, concealed = 0, late = 0, skipped = 0, dropped = 0, samples = 0; int target = 3; };
    OpusStream() { int e = 0; dec_ = opus_decoder_create(kOpusRate, 1, &e); }
    ~OpusStream() { if (dec_) opus_decoder_destroy(dec_); }
    OpusStream(const OpusStream&) = delete;
    OpusStream& operator=(const OpusStream&) = delete;
    void Push(int talk, int seq, const uint8_t* p, int n) {
        if (!dec_ || n <= 0) return;
        if (talk != talk_) Restart(talk, seq);
        ++stats_.packets;
        const int key = (uint16_t)(seq - base_);
        if (playing_ && key < next_) {                     // after its turn: more margin from now on
            ++stats_.late; sinceLate_ = 0;
            if (target_ < 10) { ++target_; ++stall_; }
            return;
        }
        jb_[key].assign(p, p + n);
        while (jb_.size() > 50) { jb_.erase(jb_.begin()); ++stats_.dropped; }
    }
    // n samples of 48 kHz added into out at gain; heard: the game plays this talker now
    void Mix(float* out, int n, bool heard, float gain) {
        if (talk_ < 0) return;
        if (!heard) {
            notHeard_ += n;
            if (notHeard_ > kOpusRate * 2 / 5) {
                if (!jb_.empty() || playing_) { stats_.dropped += (long long)jb_.size(); jb_.clear(); pcm_.clear(); playing_ = false; }
            } else if (!playing_) waited_ += n;
            return;
        }
        notHeard_ = 0;
        if (!playing_) {
            waited_ += n;
            if (jb_.empty() || ((int)jb_.size() < target_ && waited_ < kOpusRate / 10 + (target_ - 3) * kOpusFrame)) return;
            next_ = jb_.begin()->first; playing_ = true; plc_ = 0;
        }
        while ((int)pcm_.size() < n && playing_) Next();
        const int k = std::min(n, (int)pcm_.size());
        for (int i = 0; i < k; ++i) out[i] += pcm_[(size_t)i] * gain;
        pcm_.erase(pcm_.begin(), pcm_.begin() + k);
        stats_.samples += k;
    }
    bool Playing() const { return playing_; }
    int Talk() const { return talk_; }
    Stats TakeStats() { Stats s = stats_; s.target = target_; stats_ = Stats{}; return s; }
private:
    void Restart(int talk, int seq) {
        talk_ = talk; base_ = (uint16_t)seq; next_ = 0;
        jb_.clear(); pcm_.clear(); playing_ = false; waited_ = 0; notHeard_ = 0; plc_ = 0; stall_ = 0;
        opus_decoder_ctl(dec_, OPUS_RESET_STATE);
    }
    void Next() {                                          // 20 ms more into pcm_ (or left out: a pause while behind)
        float f[kOpusFrame];
        int got = 0;
        bool real = false;
        if (stall_ > 0 && !jb_.empty()) {                  // the playing waits 20 ms: a concealed frame, the same turn
            --stall_;
            got = opus_decode_float(dec_, nullptr, 0, f, kOpusFrame, 0);
            ++stats_.concealed;
            if (got > 0) pcm_.insert(pcm_.end(), f, f + got);
            return;
        }
        auto it = jb_.find(next_);
        if (it == jb_.end() && !jb_.empty() && jb_.begin()->first > next_ + 3) {   // a long gap: on to what came
            next_ = jb_.begin()->first; it = jb_.begin();
        }
        if (it != jb_.end()) {
            got = opus_decode_float(dec_, it->second.data(), (opus_int32)it->second.size(), f, kOpusFrame, 0);
            jb_.erase(it); plc_ = 0; ++stats_.decoded; real = true;
        } else if (auto nx = jb_.find(next_ + 1); nx != jb_.end()) {
            got = opus_decode_float(dec_, nx->second.data(), (opus_int32)nx->second.size(), f, kOpusFrame, 1);
            plc_ = 0; ++stats_.fec;
        } else if (!jb_.empty() || plc_ < 3) {
            got = opus_decode_float(dec_, nullptr, 0, f, kOpusFrame, 0);
            ++plc_; ++stats_.concealed;
        } else { playing_ = false; waited_ = 0; return; }
        ++next_;
        if (++sinceLate_ >= 500 && target_ > 3) { --target_; sinceLate_ = 0; }   // 10 s on time: less margin
        bool skip = false;
        if (real && got > 0 && (int)jb_.size() > target_ + 1) {   // behind: a silent frame left out
            double e = 0;
            for (int i = 0; i < got; ++i) e += (double)f[i] * f[i];
            skip = e / got < 1e-5;                         // -50 dBFS
        }
        if (skip) ++stats_.skipped;
        else if (got > 0) pcm_.insert(pcm_.end(), f, f + got);
        if (jb_.size() > 45) {                             // a stall: catch up
            while ((int)jb_.size() > target_) { jb_.erase(jb_.begin()); ++stats_.dropped; }
            next_ = jb_.begin()->first;
        }
    }
    OpusDecoder* dec_ = nullptr;
    int talk_ = -1, next_ = 0, plc_ = 0, target_ = 3, stall_ = 0, sinceLate_ = 0;
    uint16_t base_ = 0;
    bool playing_ = false;
    long long waited_ = 0, notHeard_ = 0;
    std::map<int, std::vector<uint8_t>> jb_;
    std::vector<float> pcm_;
    Stats stats_;
};

// the talkers of a match, by player id
class OpusListener {
public:
    void Push(int talker, int talk, int seq, const uint8_t* p, int n) {
        if (talker < 0 || talker >= 32) return;
        if (!s_[talker]) s_[talker].reset(new OpusStream());
        s_[talker]->Push(talk, seq, p, n);
    }
    // n samples of 48 kHz added into out at gain; heard: a bit a player id (VoiceNetHeard)
    void Mix(float* out, int n, uint32_t heard, float gain) {
        for (int t = 0; t < 32; ++t)
            if (s_[t]) {
                const bool was = s_[t]->Playing();
                s_[t]->Mix(out, n, (heard >> t) & 1, gain);
                if (was && !s_[t]->Playing() && ended_) ended_(t, s_[t]->TakeStats());
            }
    }
    uint32_t Playing() const { uint32_t m = 0; for (int t = 0; t < 32; ++t) if (s_[t] && s_[t]->Playing()) m |= 1u << t; return m; }
    void OnEnd(void (*fn)(int talker, OpusStream::Stats)) { ended_ = fn; }
private:
    std::unique_ptr<OpusStream> s_[32];
    void (*ended_)(int, OpusStream::Stats) = nullptr;
};

}  // namespace voice
