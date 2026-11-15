// alignImpulses(): the time offset and the polarity that make two impulse responses add up instead of cancelling.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/ir_align.h"

using namespace sas;

namespace {

// A short damped burst with some low-frequency body, like the direct sound of a cabinet.
std::vector<float> burst(std::size_t n, std::size_t start) {
    std::vector<float> x(n, 0.0f);
    for (std::size_t i = start; i < n; ++i) {
        const double t = double(i - start);
        x[i] = float(std::exp(-t / 60.0) * (std::sin(2.0 * 3.14159265358979 * t / 96.0) + 0.4 * std::sin(2.0 * 3.14159265358979 * t / 23.0)));
    }
    return x;
}

double sumEnergy(const std::vector<float>& a, int delayA, const std::vector<float>& b, int delayB, bool invertB) {
    double e = 0.0;
    const std::size_t n = a.size() + 400;
    for (std::size_t i = 0; i < n; ++i) {
        const double va = (i >= std::size_t(delayA) && i - std::size_t(delayA) < a.size()) ? a[i - std::size_t(delayA)] : 0.0;
        double vb = (i >= std::size_t(delayB) && i - std::size_t(delayB) < b.size()) ? b[i - std::size_t(delayB)] : 0.0;
        if (invertB) vb = -vb;
        e += (va + vb) * (va + vb);
    }
    return e;
}

}  // namespace

TEST_CASE("ir align: identical impulses need nothing", "[irAlign]") {
    const auto a = burst(2000, 10);
    const auto r = alignImpulses(a.data(), a.size(), a.data(), a.size(), 300, 4800);
    CHECK(r.delayA == 0);
    CHECK(r.delayB == 0);
    CHECK_FALSE(r.invertB);
    CHECK(r.correlation > 0.99);
}

TEST_CASE("ir align: a late and upside-down copy is put back in phase", "[irAlign]") {
    const auto a = burst(2000, 10);
    auto b = burst(2000, 10 + 37);
    for (auto& v : b) v = -0.8f * v;
    const auto r = alignImpulses(a.data(), a.size(), b.data(), b.size(), 300, 4800);
    CHECK(r.delayA == 37);  // B is late: A is held back
    CHECK(r.delayB == 0);
    CHECK(r.invertB);
    CHECK(r.correlation > 0.99);
    // Mixed as they are the two add up only in part; aligned they reinforce each other fully.
    const double raw = sumEnergy(a, 0, b, 0, false), fixed = sumEnergy(a, r.delayA, b, r.delayB, r.invertB);
    CHECK(fixed > 1.3 * raw);
}

TEST_CASE("ir align: an early copy holds B back instead", "[irAlign]") {
    const auto a = burst(2000, 10 + 25);
    const auto b = burst(2000, 10);
    const auto r = alignImpulses(a.data(), a.size(), b.data(), b.size(), 300, 4800);
    CHECK(r.delayA == 0);
    CHECK(r.delayB == 25);
    CHECK_FALSE(r.invertB);
}

TEST_CASE("ir align: silence or nothing to look at leaves them alone", "[irAlign]") {
    const std::vector<float> zeros(1000, 0.0f);
    const auto a = burst(1000, 5);
    const auto r = alignImpulses(a.data(), a.size(), zeros.data(), zeros.size(), 300, 4800);
    CHECK(r.delayA == 0);
    CHECK(r.delayB == 0);
    CHECK_FALSE(r.invertB);
}
