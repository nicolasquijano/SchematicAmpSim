#pragma once
// A whole amp: preamp feeding the power amp (full circuit, or the behavioural Lite one). The
// preamp rail comes from the power-amp supply so sag reaches both sections. One template covers
// both power-amp kinds; everything amp-specific comes from the AmpDef.

#include <cmath>
#include <cstdint>
#include <type_traits>

#include "sas/amp_def.h"
#include "sas/poweramp.h"
#include "sas/poweramp_lite.h"
#include "sas/oversample.h"
#include "sas/preamp.h"

namespace sas {

template <class Power>
class AmpT {
public:
    // `fs` is the (already oversampled) rate the circuits run at.
    // eco: triodes through tables (see sas/tables.h); the circuits are otherwise the same.
    // tablesOnly: the full circuits with every triode through tables (cheaper, same sound within ~1e-6).
    AmpT(const AmpDef& def, double fs, bool eco = false, bool tablesOnly = false, bool chord = true)
        : power_(makePower(def, fs, tablesOnly)), pre_(def, fs, power_.railX(), eco || tablesOnly, !tablesOnly) {
        if (tablesOnly) setChord(chord);
    }
    void setChord(bool on, double keep = 0.0625) {
        pre_.setChord(on, keep);
        if constexpr (requires { power_.solver(); }) {
            power_.solver().chord = on;
            power_.solver().luKeepRatio = keep;
        }
    }

    void setKnobs(const std::vector<double>& knobs, double presence, const std::vector<double>& powerKnobs = {}) {
        pre_.setKnobs(knobs);
        if (std::abs(power_.presence() - presence) > 1e-9) power_.setPresence(presence);
        if (!powerKnobs.empty() && powerKnobs.size() == power_.knobCount()) power_.setKnobs(powerKnobs);
    }
    void setJack(std::size_t jack) { pre_.setJack(jack); }
    void setSelector(std::size_t i, std::size_t pos) { pre_.setSelector(i, pos); }
    void setVibrato(bool on, double speed) { pre_.setVibrato(on, speed); }
    // Guitar ~7 kohm, a pedal's output far lower.
    void setSourceOhms(double ohms) { pre_.setSourceOhms(ohms); }
    void setGuitar(int pickup, int cableMeters, bool pedalOn) { pre_.setGuitar(pickup, cableMeters, pedalOn); }

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
    static Power makePower(const AmpDef& def, double fs, bool tables) {
        if constexpr (std::is_constructible_v<Power, const AmpDef&, double, bool>) return Power(def, fs, tables);
        else return Power(def, fs);
    }
    Power power_;  // declared first: the preamp's initial rail comes from it
    Preamp pre_;
};

using FullAmp = AmpT<PowerAmp>;
// The full circuits with the preamp at 2x the host rate and the power amp at the host rate (Quality::Efficient). The power amp is most of the
// cost of the Normal mode (60 to 80 % of the solver cycles) and it is a slow, large-signal stage, so it is run at half the rate: the preamp's
// two samples per host sample are decimated by the same half-band filter the chain's oversampler uses, and the power amp takes one. The sound
// is close to Normal but not the same (docs/PLAN_CPU.md: a few tenths of a dB in the tone and more aliasing under heavy drive).
class SplitAmp {
public:
    SplitAmp(const AmpDef& def, double fs) : power_(def, fs, true), pre_(def, fs * 2.0, power_.railX(), true, false), up_(23), down_(23) {}

    void setKnobs(const std::vector<double>& knobs, double presence, const std::vector<double>& powerKnobs = {}) {
        pre_.setKnobs(knobs);
        if (std::abs(power_.presence() - presence) > 1e-9) power_.setPresence(presence);
        if (!powerKnobs.empty() && powerKnobs.size() == power_.knobCount()) power_.setKnobs(powerKnobs);
    }
    void setJack(std::size_t jack) { pre_.setJack(jack); }
    void setSelector(std::size_t i, std::size_t pos) { pre_.setSelector(i, pos); }
    void setVibrato(bool on, double speed) { pre_.setVibrato(on, speed); }
    void setSourceOhms(double ohms) { pre_.setSourceOhms(ohms); }
    void setGuitar(int pickup, int cableMeters, bool pedalOn) { pre_.setGuitar(pickup, cableMeters, pedalOn); }

    // One host-rate sample in, one out. `input2x(s)` turns each of the two oversampled samples into the volts at the amp's input (the pedal,
    // the jack's level match and the noise, which live at the oversampled rate).
    template <class F>
    float process(float x, F&& input2x) {
        const auto ab = up_.up(x);
        pre_.setRail(power_.railX());
        const float pa = float(pre_.process(input2x(double(ab[0]))));
        pre_.setRail(power_.railX());
        const float pb = float(pre_.process(input2x(double(ab[1]))));
        return float(power_.process(double(down_.down(pa, pb))));
    }
    void reset() { up_.reset(); down_.reset(); }
    // Round trip of the two half-band stages, in host samples (the same as the chain's 2x oversampler).
    float latencySamples() const { return 2.0f * float(up_.half()) / 2.0f; }

    std::uint64_t newtonFailures() const { return pre_.newtonFailures() + power_.solver().newtonFailures; }
    Preamp& pre() { return pre_; }
    PowerAmp& power() { return power_; }

private:
    PowerAmp power_;  // declared first: the preamp's initial rail comes from it
    Preamp pre_;
    HalfbandStage up_, down_;
};
using LiteAmp = AmpT<PowerAmpLite>;

}  // namespace sas
