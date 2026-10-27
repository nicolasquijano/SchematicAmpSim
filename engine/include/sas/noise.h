#pragma once
// Noise and hum that a real amp adds in front of its first gain stage, as an equivalent voltage at the input:
//  - hiss: the thermal noise of the source and grid resistors plus the first tube's, 12 nV/sqrt(Hz) white, and a
//    flicker (1/f-like) part of 10 nV/sqrt(Hz) below about 300 Hz;
//  - hum: the mains and its first harmonics picked up by the guitar's coil (a single coil much more than a
//    humbucker, which cancels it), plus a small floor from the heaters and the wiring.
// The values are typical figures, not measurements of a particular amp (see README, "Noise and hum").
// The signal is applied after the pedal, like everything that is inside the amp.

#include <cmath>
#include <cstdint>

namespace sas {

class AmpNoise {
public:
    // `rate` is the rate next() is called at (the oversampled rate).
    void prepare(double rate, double mainsHz) {
        rate_ = rate;
        white_ = whiteDensity * std::sqrt(0.5 * rate);
        flickerWhite_ = flickerDensity * std::sqrt(0.5 * rate);
        flickerA_ = 1.0 - std::exp(-2.0 * kPi * flickerCornerHz / rate);
        for (int h = 0; h < 3; ++h) dphase_[h] = 2.0 * kPi * mainsHz * double(h + 1) / rate;
        flicker_ = 0.0;
    }
    // 0 = off, 1 = a typical amp.
    void setLevel(double level) { level_ = level; }
    // 0 off (a plain 7 kohm source), 1 single coil, 2 humbucker, 3 P-90.
    void setPickup(int pickup) {
        static constexpr double hum[4] = {2e-6, 100e-6, 3e-6, 40e-6};  // fundamental, volts
        humAmp_ = hum[pickup < 0 ? 0 : pickup > 3 ? 3 : pickup];
    }
    bool active() const { return level_ > 0.0; }

    double next() {
        if (level_ <= 0.0) return 0.0;
        flicker_ += flickerA_ * (flickerWhite_ * gauss() - flicker_);
        double hum = 0.0;
        static constexpr double harmonic[3] = {1.0, 0.45, 0.3};
        for (int h = 0; h < 3; ++h) {
            hum += harmonic[h] * std::sin(phase_[h]);
            phase_[h] += dphase_[h];
            if (phase_[h] > 2.0 * kPi) phase_[h] -= 2.0 * kPi;
        }
        return level_ * (white_ * gauss() + flicker_ + humAmp_ * hum);
    }

    static constexpr double whiteDensity = 12e-9;    // V / sqrt(Hz)
    static constexpr double flickerDensity = 10e-9;  // V / sqrt(Hz), flat below the corner
    static constexpr double flickerCornerHz = 300.0;

private:
    static constexpr double kPi = 3.14159265358979323846;
    double uniform() {  // xorshift64*, in [-1, 1)
        state_ ^= state_ >> 12; state_ ^= state_ << 25; state_ ^= state_ >> 27;
        return double((state_ * 0x2545F4914F6CDD1Dull) >> 11) / double(1ull << 52) - 1.0;
    }
    double gauss() { return (uniform() + uniform() + uniform()) * 1.0; }  // 3 uniforms on [-1,1): variance 1

    double rate_ = 96000.0, white_ = 0.0, flickerWhite_ = 0.0, flickerA_ = 0.0, flicker_ = 0.0, level_ = 0.0, humAmp_ = 2e-6;
    double phase_[3] = {0.0, 0.7, 1.9}, dphase_[3] = {0.0, 0.0, 0.0};
    std::uint64_t state_ = 0x9E3779B97F4A7C15ull;
};

}  // namespace sas
