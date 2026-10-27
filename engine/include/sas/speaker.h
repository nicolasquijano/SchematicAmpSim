#pragma once
// Speaker dynamics: what a real loudspeaker does when it is pushed and a fixed impulse response cannot.
// It sits between the power amp and the cabinet (filter or impulse response), on the speaker-level signal (volts / 100).
//
//  1. Cone excursion. The excursion is estimated by a resonator at the speaker's free-air resonance (the drive's bass, boosted
//     by the resonance). Far from the rest position the motor force Bl(x) falls, so the whole signal loses gain in proportion to
//     the excursion squared: a bass-heavy hit squashes what rides on top of it (intermodulation, odd harmonics, compression).
//     The same excursion stiffens the suspension, so the resonator's frequency rises with it (the bass tightens as it is pushed).
//  2. Voice-coil heating. The mean power, followed with a slow attack (seconds) and a slower release, raises the coil's
//     resistance: a sustained heavy signal loses a little level ("power compression").
//  3. Cone break-up. Above a threshold the upper band (above ~1.5 kHz, where the cone stops moving as one piece) is soft limited,
//     which adds the edge a speaker has when it is driven hard. Below the threshold it is untouched.
//
// All three scale with `amount` (0 = bit-exact bypass). The values are typical figures of a 12 inch guitar speaker, tuned by ear,
// not measurements of a particular one.

#include <algorithm>
#include <cmath>

namespace sas {

class SpeakerDynamics {
public:
    void prepare(double fs) {
        fs_ = fs;
        g0_ = std::tan(kPi * kResonanceHz / fs);
        heatUp_ = 1.0 - std::exp(-1.0 / (kHeatSeconds * fs));
        heatDown_ = 1.0 - std::exp(-1.0 / (kCoolSeconds * fs));
        hp_ = 1.0 - std::exp(-2.0 * kPi * kBreakupHz / fs);
        amountA_ = 1.0 - std::exp(-1.0 / (0.02 * fs));
        reset();
    }
    void reset() {
        ic1_ = ic2_ = heat_ = lp_ = 0.0;
        amountNow_ = amount_;
    }
    // 0..1; `intense` strengthens every effect.
    void setAmount(double amount, bool intense) {
        amount_ = std::clamp(amount, 0.0, 1.0);
        intense_ = intense;
    }

    float process(float x) {
        // Follow the amount so a knob move does not click; exactly 0 is a bypass.
        amountNow_ += amountA_ * (amount_ - amountNow_);
        if (amount_ == 0.0 && amountNow_ < 1e-5) {
            amountNow_ = 0.0;
            return x;
        }
        const double a = amountNow_ * (intense_ ? 1.8 : 1.0);
        const double in = double(x);

        // Cone excursion: a state-variable low-pass at the resonance whose frequency rises with the excursion.
        const double xr = excursion_ / kExcursionRef;
        const double stiff = std::min(1.0 + 0.35 * a * xr * xr, 1.7);
        const double g = g0_ * stiff;
        const double k = 1.0 / kQ;
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const double v3 = in - ic2_;
        const double v1 = a1 * ic1_ + a2 * v3;
        const double v2 = ic2_ + a2 * ic1_ + a3 * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        excursion_ = v2;
        const double bl = 1.0 / (1.0 + 0.35 * a * xr * xr);  // motor force at this excursion

        // Voice-coil heating: the mean power drives the resistance up.
        const double p = in * in;
        heat_ += (p > heat_ ? heatUp_ : heatDown_) * (p - heat_);
        const double thermal = 1.0 / (1.0 + 1.2 * a * heat_);

        double y = in * bl * thermal;

        // Cone break-up: soft limit of the upper band beyond a threshold.
        lp_ += hp_ * (y - lp_);
        const double h = y - lp_;
        const double ah = std::abs(h);
        if (ah > kBreakupKnee) {
            const double limited = kBreakupKnee + kBreakupSoft * std::tanh((ah - kBreakupKnee) / kBreakupSoft);
            y += std::min(a * 0.8, 1.0) * (std::copysign(limited, h) - h);
        }
        return float(y);
    }

    static constexpr double kResonanceHz = 95.0, kQ = 1.6, kExcursionRef = 0.6;
    static constexpr double kHeatSeconds = 2.0, kCoolSeconds = 5.0;
    static constexpr double kBreakupHz = 1500.0, kBreakupKnee = 0.12, kBreakupSoft = 0.15;

private:
    static constexpr double kPi = 3.14159265358979323846;
    double fs_ = 48000.0, g0_ = 0.0, heatUp_ = 0.0, heatDown_ = 0.0, hp_ = 0.0, amountA_ = 0.0;
    double amount_ = 0.0, amountNow_ = 0.0;
    bool intense_ = false;
    double ic1_ = 0.0, ic2_ = 0.0, excursion_ = 0.0, heat_ = 0.0, lp_ = 0.0;
};

}  // namespace sas
