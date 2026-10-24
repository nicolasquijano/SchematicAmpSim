#pragma once
// "Lite" power amp: a behavioural stand-in for PowerAmp fitted to measurements of the full circuit
// (small-signal response vs presence, static saturation curve, HT sag vs drive). The fit lives in
// the amp's amp.json ("lite"); refit it when the full circuit changes.

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "aes/amp_def.h"
#include "aes/filters.h"

namespace aes {

class PowerAmpLite {
public:
    PowerAmpLite(const AmpDef& def, double fs)
        : p_(def.lite),
          fs_(fs),
          highPass_(fs, 0.0, 1.0 / (2.0 * kPi * p_.highPassHz), 1.0 / (2.0 * kPi * p_.highPassHz)),
          shelfA_(shelf(1.0)),
          shelfB_(shelf(1.0)),
          gain_(std::pow(10.0, p_.baseGainDb / 20.0)),
          attack_(1.0 - std::exp(-1.0 / (p_.sagAttackS * fs))),
          release_(1.0 - std::exp(-1.0 / (p_.sagReleaseS * fs))) {
        setPresence(0.5);
    }

    // Presence knob, 0..1.
    void setPresence(double p) {
        p = std::clamp(p, 0.0, 1.0);
        if (std::abs(p - presence_) < 1e-9) return;
        presence_ = p;
        const double t = p * 4.0;
        const std::size_t i = std::min<std::size_t>(std::size_t(std::floor(t)), 3);
        const double db = p_.presenceDb[i] + (p_.presenceDb[i + 1] - p_.presenceDb[i]) * (t - double(i));
        // Two identical shelves share the total boost.
        const double ratio = std::pow(10.0, db / 40.0);
        shelfA_.retune(shelf(ratio));
        shelfB_.retune(shelf(ratio));
    }
    double presence() const { return presence_; }
    // The Lite fit is made at the power knobs' defaults; they do not change it.
    std::size_t knobCount() const { return 0; }
    void setKnobs(const std::vector<double>&) {}

    // One sample: phase-inverter input volts in, speaker volts out.
    double process(double vin) {
        const double x = gain_ * shelfB_.process(shelfA_.process(highPass_.process(vin)));
        const double ax = std::abs(x);

        // Level follower -> sag.
        env_ += (ax - env_) * (ax > env_ ? attack_ : release_);
        sag_ = sagFor(env_);

        // The ceiling falls as drive rises: fitted to the circuit's output rms, which is what is
        // heard (its peaks carry ringing the body of the waveform does not have).
        const double ceiling = p_.ceilingHeavy + (p_.ceilingLight - p_.ceilingHeavy) * std::exp(-env_ / p_.ceilingDecay);
        const double r = std::pow(ax / ceiling, p_.knee);
        return x / std::pow(1.0 + r, 1.0 / p_.knee);
    }

    // Preamp rail voltage, lowered by the sag.
    double railX() const { return p_.railXIdle - p_.railSagFraction * sag_; }
    double sagVolts() const { return sag_; }

private:
    static constexpr double kPi = 3.14159265358979323846;

    // Shelf with `ratio` between its pole and zero frequencies; gain at high frequency = ratio.
    FirstOrder shelf(double ratio) const {
        return {fs_, 1.0, 1.0 / (2.0 * kPi * p_.shelfHz), 1.0 / (2.0 * kPi * p_.shelfHz * ratio)};
    }

    double sagFor(double env) const {
        const auto& t = p_.sagTable;
        auto prev = t[0];
        for (std::size_t k = 1; k < t.size(); ++k) {
            const auto [x, y] = t[k];
            if (env <= x) return prev.second + (y - prev.second) * (env - prev.first) / (x - prev.first);
            prev = t[k];
        }
        return prev.second;
    }

    LiteParams p_;
    double fs_;
    FirstOrder highPass_, shelfA_, shelfB_;
    double gain_;
    double presence_ = -1.0;
    double env_ = 0.0, attack_, release_, sag_ = 0.0;
};

}  // namespace aes
