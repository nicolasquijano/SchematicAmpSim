#pragma once
// Noise gate for the guitar input: a peak envelope opens it above the threshold and closes it
// (after a hold) 4 dB below, with a fast opening and a release set by the user. Header-only and
// free of JUCE so it can be tested alone.

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sas {

class NoiseGate {
public:
    void prepare(double sampleRate) {
        fs_ = sampleRate;
        openCoef_ = coef(0.0015);
        envDecay_ = 1.0 - coef(0.030);  // per-sample multiplier of the envelope's decay
        holdSamples_ = int(0.040 * fs_);
        reset();
    }

    void reset() {
        env_ = 0.0;
        gain_ = 1.0;
        open_ = true;
        hold_ = 0;
    }

    void setParams(bool on, double thresholdDb, double releaseMs) {
        on_ = on;
        openLevel_ = std::pow(10.0, thresholdDb / 20.0);
        closeLevel_ = openLevel_ * 0.63;  // 4 dB of hysteresis
        releaseCoef_ = coef(std::max(releaseMs, 1.0) * 1e-3 / 3.0);  // the release time is about three time constants
    }

    // Current gain (1 = open), for a display.
    double gain() const { return gain_; }

    void process(float* x, std::size_t n) {
        constexpr double kFloor = 1e-4;  // -80 dB when closed: fully off without a click
        for (std::size_t i = 0; i < n; ++i) {
            const double a = std::fabs(double(x[i]));
            env_ = a > env_ ? a : env_ * envDecay_;
            if (!on_) {
                open_ = true;
            } else if (env_ > openLevel_) {
                open_ = true;
                hold_ = holdSamples_;
            } else if (open_ && env_ < closeLevel_) {
                if (hold_ > 0) --hold_;
                else open_ = false;
            }
            const double target = open_ ? 1.0 : kFloor;
            gain_ += (target - gain_) * (open_ ? openCoef_ : releaseCoef_);
            x[i] = float(double(x[i]) * gain_);
        }
    }

private:
    double coef(double seconds) const { return 1.0 - std::exp(-1.0 / (seconds * fs_)); }

    double fs_ = 48000.0;
    bool on_ = false;
    double openLevel_ = 1e-3, closeLevel_ = 6.3e-4;
    double openCoef_ = 0.1, releaseCoef_ = 0.001, envDecay_ = 0.999;  // envDecay_ is a multiplier, not a smoothing coefficient
    int holdSamples_ = 1920, hold_ = 0;
    double env_ = 0.0, gain_ = 1.0;
    bool open_ = true;
};

}  // namespace sas
