#pragma once
// Reverb for the end of the chain: a plate (and, with "hall", a bigger room) built as a feedback delay network. The mono sum goes through a
// pre-delay and four diffusing all-passes into eight delay lines that feed each other through a Hadamard matrix; each line has its own
// decay gain (set from the decay time) and a low-pass (the tone), and the stereo output is taken from different lines for each ear. The
// line lengths drift a few samples with slow oscillators so the tail does not ring on a pitch. A high-pass on the wet signal keeps the
// low end clean. Switching it off lets the tail ring out.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct PlateParams {
    bool on = false;
    bool hall = false;        // false: a plate; true: a larger, slower room
    double decaySec = 2.0;    // 0.3 .. 8 (time to fall by 60 dB)
    double preMs = 15.0;      // 0 .. 150
    double toneHz = 8000.0;   // high-cut in the tail
    double lowCutHz = 150.0;  // high-pass on the wet signal
    double mix = 0.22;        // 0 .. 1
    double mod = 0.3;         // 0 .. 1, drift of the delay lines
};

class PlateReverb {
public:
    static constexpr int kLines = 8;

    void prepare(double fs) {
        fs_ = fs;
        const double scale = fs / 48000.0;
        for (int i = 0; i < kLines; ++i) {
            lineBuf_[i].assign(pow2(std::size_t(kBase[i] * 1.9 * scale) + 64), 0.0f);
            lineMask_[i] = long(lineBuf_[i].size() - 1);
        }
        for (int i = 0; i < 4; ++i) {
            apBuf_[i].assign(pow2(std::size_t(kAp[i] * 1.3 * scale) + 8), 0.0f);
            apMask_[i] = long(apBuf_[i].size() - 1);
        }
        preBuf_.assign(pow2(std::size_t(fs * 0.2) + 8), 0.0f);
        preMask_ = long(preBuf_.size() - 1);
        setSize(target_.hall);
        reset();
    }
    void reset() {
        for (auto& b : lineBuf_) std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : apBuf_) std::fill(b.begin(), b.end(), 0.0f);
        std::fill(preBuf_.begin(), preBuf_.end(), 0.0f);
        lineW_ = apW_ = preW_ = 0;
        lp_.fill(0.0f);
        hpState_[0] = hpState_[1] = 0.0f;
        inGain_ = target_.on ? 1.0 : 0.0;
        tailLeft_ = 0;
    }

    void setParams(const PlateParams& p) {
        if (p.hall != target_.hall || !ready_) {
            setSize(p.hall);
            if (ready_) {  // another room: start the lines empty (the old tail would jump in length)
                for (auto& b : lineBuf_) std::fill(b.begin(), b.end(), 0.0f);
                for (auto& b : apBuf_) std::fill(b.begin(), b.end(), 0.0f);
            }
        }
        if (target_.on && !p.on) tailLeft_ = long((std::min(p.decaySec, 9.0) * 1.3 + p.preMs * 0.001 + 0.2) * fs_);
        if (!target_.on && p.on) tailLeft_ = 0;
        target_ = p;
        if (!ready_) {
            preCur_ = p.preMs;
            mixCur_ = p.mix;
            inGain_ = p.on ? 1.0 : 0.0;
            ready_ = true;
        }
    }
    bool active() const { return target_.on || tailLeft_ > 0; }

    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const double kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kIn = 1.0 - std::exp(-1.0 / (0.01 * fs_));
        const double inTarget = target_.on ? 1.0 : 0.0;
        const float aTone = float(1.0 - std::exp(-6.283185307179586 * std::clamp(target_.toneHz, 500.0, 20000.0) / fs_));
        const float aLow = float(1.0 - std::exp(-6.283185307179586 * std::clamp(target_.lowCutHz, 10.0, 2000.0) / fs_));
        // Per-line decay gain from the decay time: each pass through a line of L samples loses 60 dB per `decay` seconds.
        std::array<float, kLines> gain{};
        for (int i = 0; i < kLines; ++i) gain[i] = float(std::pow(10.0, -3.0 * len_[i] / (fs_ * std::max(0.1, target_.decaySec))));
        const double depth = target_.mod * 6.0 * fs_ / 48000.0;
        for (std::size_t s = 0; s < n; ++s) {
            preCur_ += (target_.preMs - preCur_) * kParam;
            mixCur_ += (target_.mix - mixCur_) * kParam;
            inGain_ += (inTarget - inGain_) * kIn;

            // Pre-delay.
            preBuf_[std::size_t(preW_ & preMask_)] = float(0.5 * (double(l[s]) + double(r[s])) * inGain_);
            float x = readFrac(preBuf_, preMask_, double(preW_) - std::max(1.0, preCur_ * 0.001 * fs_));
            ++preW_;
            // Input diffusion: four Schroeder all-passes in a row.
            for (int a = 0; a < 4; ++a) {
                const float z = apBuf_[a][std::size_t((apW_ - apLen_[a]) & apMask_[a])];
                const float v = x - 0.7f * z;
                apBuf_[a][std::size_t(apW_ & apMask_[a])] = v;
                x = z + 0.7f * v;
            }
            ++apW_;
            // The eight lines.
            std::array<float, kLines> f{};
            for (int i = 0; i < kLines; ++i) {
                phase_[i] += kRate[i] * 6.283185307179586 / fs_;
                if (phase_[i] > 6.283185307179586) phase_[i] -= 6.283185307179586;
                const double d = len_[i] + depth * std::sin(phase_[i]);
                const float y = readFrac(lineBuf_[i], lineMask_[i], double(lineW_) - d);
                lp_[i] += aTone * (y - lp_[i]);
                f[i] = lp_[i] * gain[i];
            }
            // Hadamard 8x8 (fast Walsh-Hadamard), scaled to be orthogonal.
            for (int h = 1; h < kLines; h *= 2)
                for (int i = 0; i < kLines; i += 2 * h)
                    for (int j = i; j < i + h; ++j) {
                        const float a = f[j], b = f[j + h];
                        f[j] = a + b;
                        f[j + h] = a - b;
                    }
            constexpr float kNorm = 0.35355339f;  // 1 / sqrt(8)
            for (int i = 0; i < kLines; ++i) lineBuf_[i][std::size_t(lineW_ & lineMask_[i])] = f[i] * kNorm + x * kInject[i];
            ++lineW_;
            float wl = (lp_[0] + lp_[2] - lp_[4] - lp_[6]) * kOut;
            float wr = (lp_[1] - lp_[3] + lp_[5] - lp_[7]) * kOut;
            // High-pass on the wet signal.
            hpState_[0] += aLow * (wl - hpState_[0]);
            hpState_[1] += aLow * (wr - hpState_[1]);
            wl -= hpState_[0];
            wr -= hpState_[1];
            const float mix = float(mixCur_);
            l[s] += wl * mix;
            r[s] += wr * mix;
        }
        if (!target_.on) {
            tailLeft_ -= long(n);
            if (tailLeft_ <= 0) {
                tailLeft_ = 0;
                for (auto& b : lineBuf_) std::fill(b.begin(), b.end(), 0.0f);
                for (auto& b : apBuf_) std::fill(b.begin(), b.end(), 0.0f);
                std::fill(preBuf_.begin(), preBuf_.end(), 0.0f);
                lp_.fill(0.0f);
                hpState_[0] = hpState_[1] = 0.0f;
            }
        }
    }

private:
    static constexpr std::array<double, kLines> kBase{{1051, 1181, 1327, 1453, 1601, 1741, 1877, 2017}};  // samples at 48 kHz
    static constexpr std::array<double, 4> kAp{{142, 107, 379, 277}};
    static constexpr std::array<double, kLines> kRate{{0.11, 0.17, 0.23, 0.13, 0.19, 0.29, 0.21, 0.15}};
    static constexpr std::array<float, kLines> kInject{{0.16f, -0.16f, 0.16f, -0.16f, -0.16f, 0.16f, -0.16f, 0.16f}};
    static constexpr float kOut = 1.5f;  // wet level at mix 1, set against noise so the tail is about as loud as the dry signal

    static std::size_t pow2(std::size_t n) {
        std::size_t p = 1;
        while (p < n) p *= 2;
        return p;
    }
    static float readFrac(const std::vector<float>& b, long mask, double pos) {
        const long i0 = long(std::floor(pos));
        const float f = float(pos - double(i0));
        const float x0 = b[std::size_t(i0 & mask)], x1 = b[std::size_t((i0 + 1) & mask)];
        return x0 + (x1 - x0) * f;
    }
    void setSize(bool hall) {
        const double scale = fs_ / 48000.0 * (hall ? 1.9 : 1.0);
        for (int i = 0; i < kLines; ++i) len_[i] = kBase[i] * scale;
        for (int i = 0; i < 4; ++i) apLen_[i] = long(kAp[i] * fs_ / 48000.0 * (hall ? 1.3 : 1.0));
    }

    double fs_ = 48000.0;
    std::array<std::vector<float>, kLines> lineBuf_;
    std::array<std::vector<float>, 4> apBuf_;
    std::vector<float> preBuf_;
    std::array<long, kLines> lineMask_{};
    std::array<long, 4> apMask_{}, apLen_{};
    long preMask_ = 0, lineW_ = 0, apW_ = 0, preW_ = 0, tailLeft_ = 0;
    std::array<double, kLines> len_{}, phase_{};
    std::array<float, kLines> lp_{};
    float hpState_[2] = {0, 0};
    PlateParams target_;
    bool ready_ = false;
    double preCur_ = 15.0, mixCur_ = 0.22, inGain_ = 0.0;
};

}  // namespace sas
