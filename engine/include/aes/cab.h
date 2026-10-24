#pragma once
// Placeholder cabinet voicing: 75 Hz high-pass plus a 4th-order 5.2 kHz low-pass (port of cab.rs).
// Used until a real impulse response is loaded.

#include <array>
#include <cmath>

namespace aes {

// RBJ biquad, direct form I, float.
class Biquad {
public:
    static Biquad lowpass(float fs, float f0, float q) {
        const auto [cw, alpha] = trig(fs, f0, q);
        const float a0 = 1.0f + alpha;
        Biquad b;
        b.b0_ = (1.0f - cw) / 2.0f / a0;
        b.b1_ = (1.0f - cw) / a0;
        b.b2_ = (1.0f - cw) / 2.0f / a0;
        b.a1_ = -2.0f * cw / a0;
        b.a2_ = (1.0f - alpha) / a0;
        return b;
    }

    static Biquad highpass(float fs, float f0, float q) {
        const auto [cw, alpha] = trig(fs, f0, q);
        const float a0 = 1.0f + alpha;
        Biquad b;
        b.b0_ = (1.0f + cw) / 2.0f / a0;
        b.b1_ = -(1.0f + cw) / a0;
        b.b2_ = (1.0f + cw) / 2.0f / a0;
        b.a1_ = -2.0f * cw / a0;
        b.a2_ = (1.0f - alpha) / a0;
        return b;
    }

    float process(float x) {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }

    void clear() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

private:
    static std::array<float, 2> trig(float fs, float f0, float q) {
        constexpr float pi = 3.14159265358979323846f;
        const float w = 2.0f * pi * f0 / fs;
        return {std::cos(w), std::sin(w) / (2.0f * q)};
    }

    float b0_ = 0, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

class CabFilter {
public:
    explicit CabFilter(float fs)
        : stages_{Biquad::highpass(fs, 75.0f, 0.707f), Biquad::lowpass(fs, 5200.0f, 0.707f),
                  Biquad::lowpass(fs, 5200.0f, 0.707f)} {}

    void reset() { for (auto& s : stages_) s.clear(); }

    float process(float x) {
        for (auto& s : stages_) x = s.process(x);
        return x;
    }

private:
    std::array<Biquad, 3> stages_;
};

}  // namespace aes
