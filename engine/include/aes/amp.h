#pragma once
// A whole amp: preamp feeding the power amp (full circuit, or the behavioural Lite one). The
// preamp rail comes from the power-amp supply so sag reaches both sections. One template covers
// both power-amp kinds; everything amp-specific comes from the AmpDef.

#include <cmath>
#include <cstdint>

#include "aes/amp_def.h"
#include "aes/poweramp.h"
#include "aes/poweramp_lite.h"
#include "aes/preamp.h"

namespace aes {

template <class Power>
class AmpT {
public:
    // `fs` is the (already oversampled) rate the circuits run at.
    AmpT(const AmpDef& def, double fs)
        : power_(def, fs), pre_(def, fs, power_.railX()) {}

    void setKnobs(const std::vector<double>& knobs, double presence, const std::vector<double>& powerKnobs = {}) {
        pre_.setKnobs(knobs);
        if (std::abs(power_.presence() - presence) > 1e-9) power_.setPresence(presence);
        if (!powerKnobs.empty() && powerKnobs.size() == power_.knobCount()) power_.setKnobs(powerKnobs);
    }
    void setJack(std::size_t jack) { pre_.setJack(jack); }
    // Guitar ~7 kohm, a pedal's output far lower.
    void setSourceOhms(double ohms) { pre_.setSourceOhms(ohms); }

    // One sample: guitar volts in, speaker volts (16 ohm tap) out.
    double process(double vin) {
        pre_.setRail(power_.railX());
        return power_.process(pre_.process(vin));
    }

    std::uint64_t newtonFailures() const {
        std::uint64_t n = pre_.newtonFailures();
        if constexpr (requires { power_.solver(); }) n += power_.solver().newtonFailures;
        return n;
    }

    Preamp& pre() { return pre_; }
    Power& power() { return power_; }

private:
    Power power_;  // declared first: the preamp's initial rail comes from it
    Preamp pre_;
};

using FullAmp = AmpT<PowerAmp>;
using LiteAmp = AmpT<PowerAmpLite>;

}  // namespace aes
