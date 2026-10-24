#pragma once
// The signal chain of the plugin without any framework: input gain -> [TS808] -> oversampled amp
// -> cab filter, plus the output gain stage. Port of the processing loop in jcm800/src/lib.rs.
// The cabinet impulse response (a convolution) is applied by the host plugin between
// processAmp() and applyOutput().

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "aes/amp.h"
#include "aes/cab.h"
#include "aes/oversample.h"
#include "aes/ts808_fast.h"

namespace aes {

// Quality order is part of saved-project compatibility: new qualities are only ever appended.
enum class Quality { Normal = 0, High = 1, Lite = 2, Eco = 3 };

// Knob values are normalised 0..1 here (the plugin shows 0..10).
struct ChainSettings {
    std::vector<double> knobs;       // in AmpDef::preamp.knobs order
    std::vector<double> powerKnobs;  // in AmpDef::power.knobs order
    double presence = 0.5;
    double inputDb = 0.0, outputDb = 0.0;
    Quality quality = Quality::Normal;
    std::size_t jack = 0;
    bool jackLevelMatch = true;  // apply the jack's `level_db` at the amp input
    bool cabFilter = true;
    bool tsOn = false;
    double tsDrive = 0.5, tsTone = 0.5, tsLevel = 0.5;
};

// Linear ramp to a target over a fixed time; advance one sample or a whole control period.
class Smoother {
public:
    void reset(double value, double fs, double ms) { cur_ = target_ = value; steps_ = std::max(1.0, ms * 0.001 * fs); remaining_ = 0; }
    void setTarget(double t) {
        if (t == target_) return;
        target_ = t;
        remaining_ = steps_;
        inc_ = (t - cur_) / steps_;
    }
    double next() { return nextStep(1); }
    double nextStep(double n) {
        if (remaining_ > 0) {
            const double k = std::min(n, remaining_);
            cur_ += inc_ * k;
            remaining_ -= k;
            if (remaining_ <= 0) cur_ = target_;
        }
        return cur_;
    }

private:
    double cur_ = 0, target_ = 0, inc_ = 0, steps_ = 1, remaining_ = 0;
};

class Chain {
public:
    // Builds all circuits for sample rate `fs` (expensive: solves DC points and settles the supply).
    Chain(const AmpDef& def, double fs);

    void setSettings(const ChainSettings& s);
    // Like setSettings, but the knobs start at these values instead of ramping from their defaults
    // (for a chain that is about to replace a running one).
    void snap(const ChainSettings& s);

    // Round-trip latency in host samples for the active quality.
    double latencySamples() const;
    Quality activeQuality() const { return active_; }
    void reset();

    // `mono` is the host input already averaged to one channel; writes speaker volts / 100 (and
    // the cab filter, when on) to `out`. Safe for in-place use.
    void processAmp(const float* mono, float* out, std::size_t n);
    // Output gain and NaN guard.
    void applyOutput(float* buf, std::size_t n);

    static constexpr double kOutputFullScaleVolts = 100.0;  // a clean 100 W swing is ~57 V peak
    static constexpr double kTsOutputOhms = 600.0;          // source resistance behind the pedal's buffer

private:
    static constexpr std::size_t kControlEvery = 32;

    AmpDef def_;
    double fs_;
    ChainSettings target_;
    std::unique_ptr<FullAmp> amp2_, amp4_;
    std::unique_ptr<LiteAmp> lite_, eco_;  // eco_: Lite power amp + triode tables
    TubeScreamerFast ts2_, ts4_;
    Oversampler os2_{1}, os4_{2};
    CabFilter cab_;
    float ampTrim_ = 1.0f;  // the amp's output_db as a gain
    Quality active_ = Quality::Normal;
    std::vector<Smoother> knobs_, powerKnobs_;
    Smoother presence_, tsDrive_, tsTone_, tsLevel_, inputDb_, outputDb_, jackCompDb_;
    std::vector<double> knobScratch_, powerScratch_;
};

}  // namespace aes
