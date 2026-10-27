#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "sas/tuner.h"

using namespace sas;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A plucked-string-like signal: harmonics with the given amplitudes, plus optional noise; through the feed, then detected.
PitchResult measure(double hz, double hostRate, std::vector<double> harmonics, double noise = 0.0, double level = 0.2) {
    TunerFeed feed;
    feed.prepare(hostRate);
    const std::size_t n = std::size_t(hostRate * 0.6);
    std::vector<float> x(n);
    unsigned state = 7;
    for (std::size_t i = 0; i < n; ++i) {
        double v = 0.0;
        for (std::size_t h = 0; h < harmonics.size(); ++h) v += harmonics[h] * std::sin(2.0 * kPi * hz * double(h + 1) * double(i) / hostRate);
        state = state * 1664525u + 1013904223u;
        v += noise * (double(state >> 8) / double(1u << 24) - 0.5);
        x[i] = float(level * v);
    }
    // In host-sized blocks, like the processor does.
    for (std::size_t a = 0; a < n; a += 480) feed.push(x.data() + a, std::min<std::size_t>(480, n - a));
    std::vector<float> latest(3072);
    REQUIRE(feed.copyLatest(latest.data(), latest.size()));
    return PitchDetector::detect(latest.data(), latest.size(), feed.rate());
}

double centsOff(double measured, double expected) { return 1200.0 * std::log2(measured / expected); }

}  // namespace

TEST_CASE("tuner: the pitch of every open string and higher notes is found within a cent, at any host rate") {
    for (double hostRate : {44100.0, 48000.0, 96000.0}) {
        for (double hz : {82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 659.26, 1046.5}) {
            const auto r = measure(hz, hostRate, {1.0, 0.5, 0.3, 0.2});
            INFO("host " << hostRate << " Hz, note " << hz << " Hz -> " << r.hz << " (clarity " << r.clarity << ")");
            REQUIRE(r.valid);
            CHECK(std::abs(centsOff(r.hz, hz)) < 1.0);
        }
    }
}

TEST_CASE("tuner: no octave error when the fundamental is weaker than its harmonics") {
    for (double hz : {82.41, 110.0, 196.0}) {
        const auto r = measure(hz, 48000.0, {0.25, 1.0, 0.7, 0.4, 0.2});  // a thin low string: the 2nd harmonic dominates
        INFO("note " << hz << " Hz -> " << r.hz);
        REQUIRE(r.valid);
        CHECK(std::abs(centsOff(r.hz, hz)) < 2.0);
    }
}

TEST_CASE("tuner: steady under noise, and silent when there is no note") {
    const auto noisy = measure(196.0, 48000.0, {1.0, 0.5, 0.3}, 0.6);
    REQUIRE(noisy.valid);
    CHECK(std::abs(centsOff(noisy.hz, 196.0)) < 4.0);

    const auto silent = measure(196.0, 48000.0, {1.0}, 0.0, 0.0);
    CHECK_FALSE(silent.valid);
    const auto hiss = measure(196.0, 48000.0, {0.0}, 1.0, 0.0005);  // a faint hiss
    CHECK_FALSE(hiss.valid);
}

TEST_CASE("tuner: note names, octaves and cents follow the equal-tempered scale") {
    auto check = [](double hz, const char* name, int octave, double cents, double ref = 440.0) {
        const auto n = noteFor(hz, ref);
        INFO(hz << " Hz");
        CHECK(std::string(n.name) == name);
        CHECK(n.octave == octave);
        CHECK(n.cents == Catch::Approx(cents).margin(0.2));
    };
    check(440.0, "A", 4, 0.0);
    check(82.41, "E", 2, 0.0, 440.0);
    check(261.63, "C", 4, 0.0);
    check(329.63, "E", 4, 0.0);
    check(466.16, "A#", 4, 0.0);
    check(432.0, "A", 4, 0.0, 432.0);                       // another reference pitch
    check(440.0 * std::pow(2.0, 20.0 / 1200.0), "A", 4, 20.0);   // 20 cents sharp
    check(440.0 * std::pow(2.0, -35.0 / 1200.0), "A", 4, -35.0); // 35 cents flat
    check(880.0 * std::pow(2.0, -60.0 / 1200.0), "G#", 5, 40.0); // 60 cents flat of A5 is 40 cents sharp of G#5
}
