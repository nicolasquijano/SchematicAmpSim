#pragma once
// The whole JCM800 2203: preamp feeding the power amp (full circuit) or the behavioural Lite
// power amp. The preamp rail comes from the power-amp supply so sag reaches both sections.
// Port of jcm800/src/amp.rs. One template instead of two near-identical classes.

#include <cmath>
#include <cstdint>

#include "aes/poweramp2203.h"
#include "aes/poweramp_lite.h"
#include "aes/preamp2203.h"

namespace aes {

template <class Power>
class Amp2203T {
public:
    // `fs` is the (already oversampled) rate the circuits run at.
    explicit Amp2203T(double fs)
        : power_(fs), pre_(fs, power_.railX(), TriodeModel::ecc83()) {}

    void setControls(const Controls& c, double presence) {
        pre_.setControls(c);
        if (std::abs(power_.presence() - presence) > 1e-9) power_.setPresence(presence);
    }
    void setInputMode(InputMode m) { pre_.setInputMode(m); }
    // Guitar ~7 kohm, a pedal's output far lower.
    void setSourceOhms(double ohms) { pre_.setSourceOhms(ohms); }

    // One sample: guitar volts in, speaker volts (16 ohm tap) out.
    double process(double vin) {
        pre_.setRail(power_.railX());
        return power_.process(pre_.process(vin));
    }

    std::uint64_t newtonFailures() const {
        std::uint64_t n = pre_.solver().newtonFailures;
        if constexpr (requires { power_.solver(); }) n += power_.solver().newtonFailures;
        return n;
    }

    Preamp2203& pre() { return pre_; }
    Power& power() { return power_; }

private:
    Power power_;  // declared first: the preamp's initial rail comes from it
    Preamp2203 pre_;
};

using Amp2203 = Amp2203T<PowerAmp2203>;
using AmpLite2203 = Amp2203T<PowerAmpLite>;

}  // namespace aes
