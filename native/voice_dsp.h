// The voice chat's sound on the PC side (docs/VOICE.md): the microphone's samples made into the
// 8 kHz mono PCM the game's LPC-10 coder reads, and the game's decoded 8 kHz voice made into the
// mix's 48 kHz. Header only, no Windows: tests/voice_dsp_test.cpp runs it natively.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace voice {

constexpr double kPi = 3.14159265358979323846;

// a second-order section (RBJ's cookbook), direct form I
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    static Biquad HighPass(double rate, double f, double q = 0.7071) {
        const double w = 2 * kPi * f / rate, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        Biquad s; s.b0 = (1 + c) / 2 / a0; s.b1 = -(1 + c) / a0; s.b2 = (1 + c) / 2 / a0; s.a1 = -2 * c / a0; s.a2 = (1 - al) / a0;
        return s;
    }
    static Biquad LowPass(double rate, double f, double q = 0.7071) {
        const double w = 2 * kPi * f / rate, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        Biquad s; s.b0 = (1 - c) / 2 / a0; s.b1 = (1 - c) / a0; s.b2 = (1 - c) / 2 / a0; s.a1 = -2 * c / a0; s.a2 = (1 - al) / a0;
        return s;
    }
    static Biquad Peak(double rate, double f, double q, double db) {         // a bell: + or - db at f
        const double A = std::pow(10.0, db / 40), w = 2 * kPi * f / rate, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al / A;
        Biquad s; s.b0 = (1 + al * A) / a0; s.b1 = -2 * c / a0; s.b2 = (1 - al * A) / a0; s.a1 = -2 * c / a0; s.a2 = (1 - al / A) / a0;
        return s;
    }
    double Run(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// a rate change by a windowed sinc (Kaiser, beta 8.6): 256 phases of `taps` taps, the phase between
// two linearly interpolated; the cutoff at `cut` of the lower rate's Nyquist
class Resampler {
public:
    Resampler(double from, double to, int taps = 48, double cut = 0.9) : step_(from / to), taps_(taps) {
        const double fc = cut * 0.5 * std::min(1.0, to / from);            // of the input rate
        table_.assign((size_t)(kPhases + 1) * taps_, 0.0f);
        const double beta = 8.6, i0b = I0(beta);
        for (int p = 0; p <= kPhases; ++p) {
            double sum = 0;
            for (int k = 0; k < taps_; ++k) {
                const double t = (k - taps_ / 2 + 1) - (double)p / kPhases;   // the tap's time from the output sample
                const double x = 2 * fc * t, sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                const double r = t / (taps_ / 2.0);
                const double win = std::fabs(r) >= 1 ? 0 : I0(beta * std::sqrt(1 - r * r)) / i0b;
                const double v = 2 * fc * sinc * win;
                table_[(size_t)p * taps_ + k] = (float)v; sum += v;
            }
            for (int k = 0; k < taps_; ++k) table_[(size_t)p * taps_ + k] = (float)(table_[(size_t)p * taps_ + k] / sum);
        }
        hist_.assign((size_t)taps_, 0.0f);
    }
    // in: n samples; out: the samples they complete (appended)
    void Run(const float* in, size_t n, std::vector<float>& out) {
        for (size_t i = 0; i < n; ++i) {
            buf_.push_back(in[i]);
        }
        while (pos_ + taps_ <= buf_.size()) {
            const double frac = pos_frac_;
            const double ph = frac * kPhases;
            const int p = (int)ph; const float w = (float)(ph - p);
            const float* a = &table_[(size_t)p * taps_]; const float* b = &table_[(size_t)(p + 1) * taps_];
            const float* x = &buf_[pos_];
            float acc = 0;
            for (int k = 0; k < taps_; ++k) acc += x[k] * (a[k] + w * (b[k] - a[k]));
            out.push_back(acc);
            pos_frac_ += step_;
            const size_t adv = (size_t)pos_frac_;
            pos_ += adv; pos_frac_ -= adv;
        }
        if (pos_ > 4096) { buf_.erase(buf_.begin(), buf_.begin() + (long)pos_); pos_ = 0; }
    }
private:
    static double I0(double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }
    static constexpr int kPhases = 256;
    double step_, pos_frac_ = 0;
    int taps_;
    size_t pos_ = 0;
    std::vector<float> table_, hist_, buf_;
};

// the level: a gate below the room's noise, then a gain toward `target` (RMS, full scale 1) at most
// `maxGain`, quick to fall on loud words (attack) and slow to rise (release); a soft limit at the top
struct Leveler {
    double rate, target = 0.12, maxGain = 8.0, gateDb = -52.0;
    double env = 0, gain = 1, noise = 1e-4, gateOpen = 0; int hold = 0;
    explicit Leveler(double r) : rate(r) {}
    float Run(float x) {
        const double a = std::fabs(x);
        const double atk = 1 - std::exp(-1.0 / (0.005 * rate)), rel = 1 - std::exp(-1.0 / (0.120 * rate));
        env += (a > env ? atk : rel) * (a - env);
        // the noise floor: follows the envelope down at once, up slowly (10 s)
        noise = env < noise ? env : noise + (env - noise) * (1 - std::exp(-1.0 / (10.0 * rate)));
        const double gate = std::max(std::pow(10.0, gateDb / 20), noise * 2.5);
        if (env > gate) { hold = (int)(0.25 * rate); } else if (hold > 0) --hold;
        const double open = hold > 0 ? 1.0 : 0.0;
        gateOpen += (open - gateOpen) * (open > gateOpen ? 1 - std::exp(-1.0 / (0.002 * rate)) : 1 - std::exp(-1.0 / (0.060 * rate)));
        const double want = std::min(maxGain, target / std::max(env * 1.4142, 1e-6));
        const double g = want < gain ? 1 - std::exp(-1.0 / (0.010 * rate)) : 1 - std::exp(-1.0 / (0.800 * rate));
        gain += (want - gain) * g;
        double y = x * gain * gateOpen;
        if (y > 0.9) y = 0.9 + 0.1 * std::tanh((y - 0.9) / 0.1);          // a soft top
        if (y < -0.9) y = -0.9 + 0.1 * std::tanh((y + 0.9) / 0.1);
        return (float)y;
    }
};

// an FFT of n = a power of two points, in place (radix 2)
inline void Fft(std::vector<double>& re, std::vector<double>& im, bool inverse) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double a = 2 * kPi / (double)len * (inverse ? 1 : -1);
        const double wr = std::cos(a), wi = std::sin(a);
        for (size_t i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k) {
                const size_t u = i + k, v = i + k + len / 2;
                const double tr = re[v] * cr - im[v] * ci, ti = re[v] * ci + im[v] * cr;
                re[v] = re[u] - tr; im[v] = im[u] - ti; re[u] += tr; im[u] += ti;
                const double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
    if (inverse) for (size_t i = 0; i < n; ++i) { re[i] /= (double)n; im[i] /= (double)n; }
}

// the room's steady noise held back: n-point frames every n / 2 samples (sqrt-Hann in and out; 256 at
// 8 kHz, 32 ms), the noise of each band as the least of its smoothed power over the last 1.5 s (minimum
// statistics), a gain per band from the decision-directed SNR (Ephraim and Malah), 18 dB at most taken
// off; n - n / 2 samples of delay
class NoiseSuppressor {
public:
    explicit NoiseSuppressor(int n = 256, double rate = 8000.0) : N(n), H(n / 2), B(n / 2 + 1), U(8),
        V(std::max(1, (int)std::lround(1.5 * rate / (n / 2) / 8))), win_(N), in_(N, 0.0), ola_(N, 0.0), ps_(B, 0.0),
        noise_(B, 0.0), gPrev_(B, 1.0), pPrev_(B, 0.0), curMin_(B, 1e30), ring_(U, std::vector<double>(B, 1e30)) {
        for (int i = 0; i < N; ++i) win_[i] = std::sqrt(0.5 - 0.5 * std::cos(2 * kPi * i / N));
    }
    void Run(const float* x, size_t n, std::vector<float>& out) {
        for (size_t i = 0; i < n; ++i) {
            in_[N - H + fill_] = x[i];
            if (++fill_ == H) { Frame(out); fill_ = 0; }
        }
    }
    double reduction = 18.0;                               // dB at most
private:
    void Frame(std::vector<float>& out) {
        std::vector<double> re(N), im(N, 0.0);
        for (int i = 0; i < N; ++i) re[i] = in_[i] * win_[i];
        Fft(re, im, false);
        const double gMin = std::pow(10.0, -reduction / 20);
        for (int k = 0; k < B; ++k) {
            const double p = re[k] * re[k] + im[k] * im[k];
            ps_[k] = frames_ ? 0.7 * ps_[k] + 0.3 * p : p;
            curMin_[k] = std::min(curMin_[k], ps_[k]);
        }
        if (++frames_ % V == 0) { ring_[(frames_ / V) % U] = curMin_; std::fill(curMin_.begin(), curMin_.end(), 1e30); }
        for (int k = 0; k < B; ++k) {
            double m = curMin_[k];
            for (int u = 0; u < U; ++u) m = std::min(m, ring_[u][k]);
            noise_[k] = std::max(1e-9, 1.5 * m);             // the minimum's bias
            const double p = re[k] * re[k] + im[k] * im[k], post = p / noise_[k];
            const double prio = 0.98 * gPrev_[k] * gPrev_[k] * pPrev_[k] / noise_[k] + 0.02 * std::max(post - 1.0, 0.0);
            double g = prio / (1.0 + prio);
            g = std::max(g, gMin);
            gPrev_[k] = g; pPrev_[k] = p;
            re[k] *= g; im[k] *= g;
            if (k > 0 && k < N / 2) { re[N - k] = re[k]; im[N - k] = -im[k]; }
        }
        Fft(re, im, true);
        for (int i = 0; i < N; ++i) ola_[i] += re[i] * win_[i];
        for (int i = 0; i < H; ++i) out.push_back((float)ola_[i]);
        std::copy(ola_.begin() + H, ola_.end(), ola_.begin()); std::fill(ola_.end() - H, ola_.end(), 0.0);
        std::copy(in_.begin() + H, in_.end(), in_.begin());
    }
    const int N, H, B, U, V;
    std::vector<double> win_, in_, ola_, ps_, noise_, gPrev_, pPrev_, curMin_;
    std::vector<std::vector<double>> ring_;
    int fill_ = 0; long frames_ = 0;
};

// the vocoder's voice made clearer (8 kHz): its formants sharpened and the noise between them lowered
// by a short-term postfilter A(z/0.55) / A(z/0.7) from an order-10 LPC of the last 30 ms every 10 ms,
// its spectral tilt made up (G.729's), its level kept the input's
class Postfilter {
public:
    static constexpr int P = 10, Blk = 80, Win = 240;
    Postfilter() : hist_(Win, 0.0), zNum_(P, 0.0), zDen_(P, 0.0) {
        for (int i = 0; i < Win; ++i) ham_[i] = 0.54 - 0.46 * std::cos(2 * kPi * i / (Win - 1));
        for (int i = 0; i <= P; ++i) lag_[i] = std::exp(-0.5 * std::pow(2 * kPi * 60.0 * i / 8000.0, 2));   // 60 Hz lag window
        a_[0] = 1;
    }
    void Run(const float* x, size_t n, std::vector<float>& out) {
        for (size_t i = 0; i < n; ++i) {
            blk_[fill_++] = x[i];
            if (fill_ == Blk) { Block(out); fill_ = 0; }
        }
    }
    double gn = 0.55, gd = 0.7, gt = 0.8;
private:
    void Block(std::vector<float>& out) {
        std::copy(hist_.begin() + Blk, hist_.end(), hist_.begin());
        std::copy(blk_, blk_ + Blk, hist_.end() - Blk);
        double r[P + 1] = {};
        double w[Win];
        for (int i = 0; i < Win; ++i) w[i] = hist_[i] * ham_[i];
        for (int k = 0; k <= P; ++k) { for (int i = k; i < Win; ++i) r[k] += w[i] * w[i - k]; r[k] *= lag_[k]; }
        r[0] *= 1.0001;
        double a[P + 1] = {1};
        if (r[0] > 1e-9) {                                 // Levinson-Durbin
            double e = r[0];
            for (int i = 1; i <= P && e > 1e-12; ++i) {
                double acc = r[i];
                for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
                const double k = -acc / e;
                double t[P + 1]; std::copy(a, a + P + 1, t);
                for (int j = 1; j < i; ++j) a[j] = t[j] + k * t[i - j];
                a[i] = k; e *= 1 - k * k;
            }
        }
        double num[P + 1], den[P + 1], gp = 1, dp = 1;
        for (int i = 0; i <= P; ++i) { num[i] = a[i] * gp; den[i] = a[i] * dp; gp *= gn; dp *= gd; }
        // the filter's impulse response: its tilt (the first reflection) made up by 1 + mu z^-1
        double h[24] = {}, zn[P] = {}, zd[P] = {};
        for (int t = 0; t < 24; ++t) {
            const double xin = t == 0 ? 1.0 : 0.0;
            double y = xin;
            for (int i = 1; i <= P; ++i) y += num[i] * zn[i - 1];
            for (int i = 1; i <= P; ++i) y -= den[i] * zd[i - 1];
            for (int i = P - 1; i > 0; --i) { zn[i] = zn[i - 1]; zd[i] = zd[i - 1]; }
            zn[0] = xin; zd[0] = y; h[t] = y;
        }
        double r0 = 0, r1 = 0;
        for (int t = 0; t < 24; ++t) { r0 += h[t] * h[t]; if (t + 1 < 24) r1 += h[t] * h[t + 1]; }
        const double k1 = r0 > 0 ? -r1 / r0 : 0, mu = k1 < 0 ? gt * k1 : 0;
        double ein = 0, eout = 0, y[Blk];
        for (int t = 0; t < Blk; ++t) {
            const double xin = blk_[t];
            double v = xin;
            for (int i = 1; i <= P; ++i) v += num[i] * zNum_[i - 1];
            for (int i = 1; i <= P; ++i) v -= den[i] * zDen_[i - 1];
            for (int i = P - 1; i > 0; --i) { zNum_[i] = zNum_[i - 1]; zDen_[i] = zDen_[i - 1]; }
            zNum_[0] = xin; zDen_[0] = v;
            y[t] = v + mu * tiltPrev_; tiltPrev_ = v;
            ein += xin * xin; eout += y[t] * y[t];
        }
        const double target = eout > 1e-12 ? std::sqrt(ein / eout) : 1.0;
        for (int t = 0; t < Blk; ++t) { gain_ = 0.9 * gain_ + 0.1 * target; out.push_back((float)(y[t] * gain_)); }
    }
    std::vector<double> hist_, zNum_, zDen_;
    double ham_[Win], lag_[P + 1], a_[P + 1] = {};
    float blk_[Blk];
    int fill_ = 0;
    double tiltPrev_ = 0, gain_ = 1;
};

// the microphone's side: mono samples at the device's rate in, 8 kHz s16 out: a high pass at 100 Hz
// (fourth order), the rate change (192-tap windowed sinc, 3.8 kHz), the room's noise held back, the
// level (a gate below the remaining noise, the gain toward 0.12 RMS)
class MicChain {
public:
    explicit MicChain(double rate, bool suppress = true) : rate_(rate), suppress_(suppress), hp_(Biquad::HighPass(rate, 100)),
        hp2_(Biquad::HighPass(rate, 100)), lev_(8000.0), rs_(rate, 8000.0, 192, 0.95) {}
    void Run(const float* in, size_t n, std::vector<int16_t>& out) {
        tmp_.resize(n);
        for (size_t i = 0; i < n; ++i) tmp_[i] = (float)hp2_.Run(hp_.Run(in[i]));
        low_.clear();
        rs_.Run(tmp_.data(), n, low_);
        const std::vector<float>* src = &low_;
        if (suppress_) { clean_.clear(); ns_.Run(low_.data(), low_.size(), clean_); src = &clean_; }
        for (float v : *src) out.push_back((int16_t)std::clamp((int)std::lrint(lev_.Run(v) * 32767.0f), -32768, 32767));
    }
    float Level() const { return (float)lev_.env; }
private:
    double rate_;
    bool suppress_;
    Biquad hp_, hp2_;
    Leveler lev_;
    Resampler rs_;
    NoiseSuppressor ns_;
    std::vector<float> tmp_, low_, clean_;
};

// the microphone for the voice between PCs (Opus): mono samples at the device's rate in, 48 kHz out: a
// second-order high pass at 50 Hz (Opus' own voice mode cuts below the speaker's pitch), the rate made 48
// kHz (when it is not), the room's noise held back (1024-point frames, 21 ms), the same level
class WideChain {
public:
    explicit WideChain(double rate) : hp_(Biquad::HighPass(rate, 50)), lev_(48000.0), ns_(1024, 48000.0) {
        if (std::fabs(rate - 48000.0) > 0.5) rs_.reset(new Resampler(rate, 48000.0, 64, 0.95));
    }
    void Run(const float* in, size_t n, std::vector<float>& out) {
        tmp_.resize(n);
        for (size_t i = 0; i < n; ++i) tmp_[i] = (float)hp_.Run(in[i]);
        const std::vector<float>* src = &tmp_;
        if (rs_) { mid_.clear(); rs_->Run(tmp_.data(), n, mid_); src = &mid_; }
        clean_.clear();
        ns_.Run(src->data(), src->size(), clean_);
        for (float v : clean_) out.push_back(lev_.Run(v));
    }
private:
    Biquad hp_;
    Leveler lev_;
    NoiseSuppressor ns_;
    std::unique_ptr<Resampler> rs_;
    std::vector<float> tmp_, mid_, clean_;
};

// the speaker's side: the game's 8 kHz voice (its low rumble cut, the postfilter, LPC-10's buzz above
// 3.5 kHz taken down), 48 kHz out
class SpeakerChain {
public:
    explicit SpeakerChain(bool post = true) : post_(post), lp_(Biquad::LowPass(8000, 3500, 0.6)), hp_(Biquad::HighPass(8000, 120)),
        rs_(8000.0, 48000.0, 48, 0.85) {}
    void Run(const int16_t* in, size_t n, std::vector<float>& out) {
        tmp_.resize(n);
        for (size_t i = 0; i < n; ++i) tmp_[i] = (float)hp_.Run(in[i] / 32768.0);
        const std::vector<float>* src = &tmp_;
        if (post_) { pf_out_.clear(); pf_.Run(tmp_.data(), n, pf_out_); src = &pf_out_; }
        mid_.resize(src->size());
        for (size_t i = 0; i < src->size(); ++i) mid_[i] = (float)lp_.Run((*src)[i]);
        rs_.Run(mid_.data(), mid_.size(), out);
    }
private:
    bool post_;
    Biquad lp_, hp_;
    Postfilter pf_;
    Resampler rs_;
    std::vector<float> tmp_, pf_out_, mid_;
};

}  // namespace voice
