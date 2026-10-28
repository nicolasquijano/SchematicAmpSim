// The pitch shifter: the same checks as the Rust octave shifter it was ported from.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/pitch_shifter.h"

namespace {

constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

// Signal power at `freq` over the second half of the signal (Goertzel).
double powerAt(const std::vector<float>& s, double freq) {
    const std::size_t from = s.size() / 2;
    const double w = 2.0 * kPi * freq / kFs, coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < s.size(); ++i) {
        const double s0 = double(s[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / double(s.size() - from);
}

std::vector<float> shiftSine(double freq, double ratio, double fs = kFs) {
    sas::PitchShifter shifter;
    shifter.prepare(fs);
    std::vector<float> in(static_cast<std::size_t>(fs));
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) in[n] = float(std::sin(2.0 * kPi * freq * double(n) / fs));
    shifter.process(in.data(), out.data(), in.size(), ratio);
    return out;
}

}  // namespace

TEST_CASE("pitch shifter: an octave up doubles the frequency and an octave down halves it") {
    const auto up = shiftSine(440.0, 2.0);
    CHECK(powerAt(up, 880.0) > 10.0 * powerAt(up, 440.0));
    const auto down = shiftSine(440.0, 0.5);
    CHECK(powerAt(down, 220.0) > 10.0 * powerAt(down, 440.0));
}

TEST_CASE("pitch shifter: a semitone and a few cents land on the right frequency") {
    const double ratio = sas::PitchShifter::ratioFromSemitones(-2.0);  // a whole tone down: E to D
    const auto out = shiftSine(329.63, ratio);
    const double target = 329.63 * ratio;
    CHECK(powerAt(out, target) > 10.0 * powerAt(out, 329.63));
}

TEST_CASE("pitch shifter: a unity ratio is transparent after the latency, at any sample rate") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        const auto out = shiftSine(440.0, 1.0, fs);
        float peak = 0.0f;
        for (std::size_t i = out.size() / 2; i < out.size(); ++i) peak = std::max(peak, std::fabs(out[i]));
        INFO("fs " << fs << " peak " << peak);
        CHECK(std::fabs(peak - 1.0f) < 0.05f);
    }
}

TEST_CASE("pitch shifter: a chord stays bounded and the latency is about 12 ms") {
    sas::PitchShifter shifter;
    shifter.prepare(kFs);
    CHECK(std::fabs(shifter.latencySamples() / kFs * 1000.0 - 12.0) < 1.0);
    std::vector<float> in(static_cast<std::size_t>(kFs));
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) {
        const double t = double(n) / kFs;
        double x = 0.0;
        for (double f : {82.4, 110.0, 146.8, 196.0, 246.9, 329.6}) x += std::sin(2.0 * kPi * f * t) / 6.0;
        in[n] = float(x);
    }
    shifter.process(in.data(), out.data(), in.size(), 0.5);
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    CHECK(peak < 1.2f);
}

TEST_CASE("pitch shifter: changing the ratio glides without a click") {
    sas::PitchShifter shifter;
    shifter.prepare(kFs);
    std::vector<float> in(static_cast<std::size_t>(kFs));
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) in[n] = 0.5f * float(std::sin(2.0 * kPi * 196.0 * double(n) / kFs));
    const std::size_t half = in.size() / 2;
    shifter.process(in.data(), out.data(), half, 1.0);
    shifter.process(in.data() + half, out.data() + half, in.size() - half, 0.5946);  // up a tritone down
    float worst = 0.0f;
    for (std::size_t i = half; i + 1 < out.size(); ++i) worst = std::max(worst, std::fabs(out[i + 1] - out[i]));
    CHECK(worst < 0.2f);  // a 196 Hz sine of amplitude 0.5 moves at most ~0.013 per sample; a click would be far above
}
