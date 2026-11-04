#pragma once
// A wah: a resonant band-pass filter whose centre frequency sweeps between a low end (Heel) and a high end (Toe), the way a rocking pedal does it.
// What moves the sweep is the player's choice:
//  - Pedal: a position (0 = heel down, 1 = toe down) that the host can automate or map to a controller, smoothed so it does not zipper.
//  - Envelope: the level of the playing (an auto-wah): the harder the string is hit, the further the filter opens (or the lower it goes, with Down),
//    with its own attack and release times and a sensitivity.
//  - LFO: a slow sine that rocks it by itself.
// Heel and Toe set the range, Q how narrow and vocal the peak is, Mix how much of the filtered sound is blended with the dry one (below 100 % the
// bass and the pick attack stay in front), Level the loudness of what comes out. Works on one channel (before the amp) or a stereo pair (after
// it); in stereo mode the two channels follow their own envelope, and the LFO is half a cycle apart between them. Switching it on or off fades
// over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sas {

struct WahParams {
    enum Mode { Pedal = 0, Envelope = 1, Lfo = 2 };
    bool on = false;
    int mode = Pedal;
    double position = 0.5;    // pedal: 0 .. 1
    double heelHz = 400.0;    // 200 .. 1000, the lowest centre frequency
    double toeHz = 2000.0;    // 1000 .. 4000, the highest
    double q = 4.0;           // 0.7 .. 12
    double mix = 1.0;         // 0 .. 1
    double levelDb = 3.0;     // -24 .. 24
    double sensitivity = 0.5; // envelope: 0 .. 1
    double attackMs = 8.0;    // envelope: 1 .. 100
    double releaseMs = 120.0; // envelope: 20 .. 1000
    bool down = false;        // envelope: the loud notes close the filter instead of opening it
    double rateHz = 1.0;      // LFO: 0.1 .. 10
    bool stereo = false;      // stereo pair: each channel has its own envelope, and the LFO is half a cycle apart
};

class Wah {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        for (auto& s : ic1_) s.fill(0.0);
        for (auto& s : ic2_) s.fill(0.0);
        env_.fill(0.0);
        phase_ = 0.0;
        pos_.fill(target_.position);
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const WahParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            pos_.fill(p.position);
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    // l is always processed; r only when given (a stereo pair).
    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kPos = 1.0 - std::exp(-1.0 / (0.01 * fs_));
        const double kUp = 1.0 - std::exp(-1.0 / (std::max(0.5, target_.attackMs) * 0.001 * fs_));
        const double kDown = 1.0 - std::exp(-1.0 / (std::max(5.0, target_.releaseMs) * 0.001 * fs_));
        const double lfoInc = target_.rateHz / fs_;
        const double heel = std::clamp(target_.heelHz, 100.0, 2000.0), toe = std::max(heel * 1.2, std::clamp(target_.toeHz, 500.0, 6000.0));
        const double q = std::clamp(target_.q, 0.5, 15.0), k = 1.0 / q;
        const double level = std::pow(10.0, target_.levelDb / 20.0), mix = std::clamp(target_.mix, 0.0, 1.0);
        const double sens = 1.0 + 15.0 * std::clamp(target_.sensitivity, 0.0, 1.0);  // envelope gain into the sweep
        const bool stereoPair = r != nullptr;
        const int channels = stereoPair ? 2 : 1;
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            phase_ += lfoInc;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const double wet = amount_;
            for (int c = 0; c < channels; ++c) {
                float* x = c == 0 ? l : r;
                const double in = double(x[i]);
                double want;
                switch (target_.mode) {
                    case WahParams::Envelope: {
                        // The follower listens to its own channel in stereo mode, to the sum of both otherwise.
                        const double listen = stereoPair && !target_.stereo ? 0.5 * std::fabs(double(l[i]) + double(r[i])) : std::fabs(in);
                        double& e = env_[std::size_t(stereoPair && !target_.stereo ? 0 : c)];
                        if (c == 0 || target_.stereo || !stereoPair) e += (listen - e) * (listen > e ? kUp : kDown);
                        const double o = std::clamp(e * sens * 3.0, 0.0, 1.0);
                        want = target_.down ? 1.0 - o : o;
                        break;
                    }
                    case WahParams::Lfo: {
                        double ph = phase_ + (c == 1 && target_.stereo ? 0.5 : 0.0);
                        if (ph >= 1.0) ph -= 1.0;
                        want = 0.5 - 0.5 * std::cos(6.283185307179586 * ph);
                        break;
                    }
                    default: want = std::clamp(target_.position, 0.0, 1.0); break;
                }
                double& pos = pos_[std::size_t(c)];
                pos += (want - pos) * kPos;
                const double f = std::min(heel * std::pow(toe / heel, pos), 0.45 * fs_);
                // Band-pass of a trapezoidal state-variable filter (stable while it is swept); k scales it to unity gain at the peak.
                const double g = std::tan(3.14159265358979323846 * f / fs_);
                const double a1 = 1.0 / (1.0 + g * (g + k));
                const double a2 = g * a1;
                const double a3 = g * a2;
                const double v3 = in - ic2_[std::size_t(c)][0];
                const double v1 = a1 * ic1_[std::size_t(c)][0] + a2 * v3;
                const double v2 = ic2_[std::size_t(c)][0] + a2 * ic1_[std::size_t(c)][0] + a3 * v3;
                ic1_[std::size_t(c)][0] = 2.0 * v1 - ic1_[std::size_t(c)][0];
                ic2_[std::size_t(c)][0] = 2.0 * v2 - ic2_[std::size_t(c)][0];
                const double band = k * v1 * level;
                const double out = in * (1.0 - mix) + band * mix;
                x[i] = float(in + (out - in) * wet);
            }
        }
    }

private:
    double fs_ = 48000.0;
    std::array<std::array<double, 1>, 2> ic1_{}, ic2_{};
    std::array<double, 2> env_{}, pos_{};
    double phase_ = 0.0, amount_ = 0.0;
    WahParams target_;
    bool ready_ = false;
};

}  // namespace sas
