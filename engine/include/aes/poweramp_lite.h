#pragma once
// "Lite" power amp: a behavioural stand-in for PowerAmp2203 fitted to measurements of the full
// circuit (small-signal response vs presence, static saturation curve, HT sag vs drive).
// Port of jcm800/src/poweramp_lite.rs. The constants are the fit; change them only by refitting.

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "aes/filters.h"

namespace aes {

class PowerAmpLite {
public:
    static constexpr double kRailXIdle = 391.0;  // preamp rail (node X) at idle

    explicit PowerAmpLite(double fs)
        : fs_(fs),
          highPass_(fs, 0.0, 1.0 / (2.0 * kPi * kHighPassHz), 1.0 / (2.0 * kPi * kHighPassHz)),
          shelfA_(shelf(fs, 1.0)),
          shelfB_(shelf(fs, 1.0)),
          gain_(std::pow(10.0, kBaseGainDb / 20.0)),
          attack_(1.0 - std::exp(-1.0 / (kSagAttackS * fs))),
          release_(1.0 - std::exp(-1.0 / (kSagReleaseS * fs))) {
        setPresence(0.5);
    }

    // Presence knob, 0..1.
    void setPresence(double p) {
        p = std::clamp(p, 0.0, 1.0);
        if (std::abs(p - presence_) < 1e-9) return;
        presence_ = p;
        const double t = p * 4.0;
        const std::size_t i = std::min<std::size_t>(std::size_t(std::floor(t)), 3);
        const double db = kPresenceDb[i] + (kPresenceDb[i + 1] - kPresenceDb[i]) * (t - double(i));
        // Two identical shelves share the total boost.
        const double ratio = std::pow(10.0, db / 40.0);
        shelfA_.retune(shelf(fs_, ratio));
        shelfB_.retune(shelf(fs_, ratio));
    }
    double presence() const { return presence_; }

    // One sample: phase-inverter input volts in, speaker volts out.
    double process(double vin) {
        const double x = gain_ * shelfB_.process(shelfA_.process(highPass_.process(vin)));
        const double ax = std::abs(x);

        // Level follower -> sag.
        env_ += (ax - env_) * (ax > env_ ? attack_ : release_);
        sag_ = sagFor(env_);

        // The ceiling falls as drive rises: fitted to the circuit's output rms, which is what is
        // heard (its peaks carry ringing the body of the waveform does not have).
        const double ceiling = kCeilingHeavy + (kCeilingLight - kCeilingHeavy) * std::exp(-env_ / kCeilingDecay);
        const double r = std::pow(ax / ceiling, kKnee);
        return x / std::pow(1.0 + r, 1.0 / kKnee);
    }

    // Preamp rail voltage (node X), lowered by the sag.
    double railX() const { return kRailXIdle - kRailSagFraction * sag_; }
    double sagVolts() const { return sag_; }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kBaseGainDb = 25.72;  // phase-inverter input to 16 ohm tap, presence 0
    static constexpr double kHighPassHz = 21.9;
    static constexpr double kShelfHz = 564.0;
    // Total presence boost in dB at presence 0, .25, .5, .75, 1 (measured on the circuit).
    static constexpr std::array<double, 5> kPresenceDb{0.0, 0.21, 0.58, 1.42, 5.06};
    // Mean-|x| level (volts, unclipped) -> HT sag (volts), measured on the circuit.
    static constexpr std::array<std::pair<double, double>, 13> kSagTable{{
        {0.0, 0.0}, {6.0, 0.8}, {12.3, 5.6}, {18.4, 12.6}, {24.6, 20.7}, {30.6, 29.2}, {36.3, 37.4},
        {49.0, 47.8}, {61.0, 50.7}, {86.0, 55.6}, {123.0, 58.5}, {185.0, 59.8}, {307.0, 60.1}}};
    static constexpr double kRailSagFraction = 0.58;  // how much of the HT sag the preamp rail follows
    static constexpr double kCeilingLight = 66.5, kCeilingHeavy = 48.5, kCeilingDecay = 119.7, kKnee = 6.44;
    static constexpr double kSagAttackS = 0.010, kSagReleaseS = 0.060;

    // Shelf with `ratio` between its pole and zero frequencies; gain at high frequency = ratio.
    static FirstOrder shelf(double fs, double ratio) {
        return {fs, 1.0, 1.0 / (2.0 * kPi * kShelfHz), 1.0 / (2.0 * kPi * kShelfHz * ratio)};
    }

    static double sagFor(double env) {
        auto prev = kSagTable[0];
        for (std::size_t k = 1; k < kSagTable.size(); ++k) {
            const auto [x, y] = kSagTable[k];
            if (env <= x) return prev.second + (y - prev.second) * (env - prev.first) / (x - prev.first);
            prev = kSagTable[k];
        }
        return prev.second;
    }

    double fs_;
    FirstOrder highPass_, shelfA_, shelfB_;
    double gain_;
    double presence_ = -1.0;
    double env_ = 0.0, attack_, release_, sag_ = 0.0;
};

}  // namespace aes
