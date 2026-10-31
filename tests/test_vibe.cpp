// The four-stage vibe.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/vibe.h"

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

std::vector<float> tone(double hz, std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = float(0.4 * std::sin(2.0 * kPi * hz * double(i) / kFs));
    return v;
}

void run(sas::Vibe& v, std::vector<float>& l, std::vector<float>* r) {
    for (std::size_t s = 0; s < l.size(); s += 256) {
        const std::size_t n = std::min<std::size_t>(256, l.size() - s);
        v.process(l.data() + s, r ? r->data() + s : nullptr, n);
    }
}

double diff(const std::vector<float>& a, const std::vector<float>& b, std::size_t from) {
    double d = 0;
    for (std::size_t i = from; i < a.size(); ++i) d += std::fabs(double(a[i]) - double(b[i]));
    return d;
}
}  // namespace

TEST_CASE("vibe: off is transparent; chorus and vibrato modes both change the sound and stay bounded") {
    sas::Vibe v;
    sas::VibeParams p;
    v.setParams(p);
    v.prepare(kFs);
    auto x = tone(440.0, 48000), y = x;
    run(v, y, nullptr);
    for (std::size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);

    std::vector<float> chorus, vibrato;
    for (bool vib : {false, true}) {
        sas::Vibe w;
        p.on = true;
        p.vibrato = vib;
        p.speedHz = 4.0;
        p.intensity = 1.0;
        w.setParams(p);
        w.prepare(kFs);
        auto z = tone(440.0, 96000);
        run(w, z, nullptr);
        for (float s : z) {
            REQUIRE(std::isfinite(s));
            REQUIRE(std::fabs(s) < 1.0f);
        }
        CHECK(diff(tone(440.0, 96000), z, 10000) > 100.0);
        (vib ? vibrato : chorus) = z;
    }
    CHECK(diff(chorus, vibrato, 10000) > 100.0);
}

TEST_CASE("vibe: intensity widens the sweep, and both channels of a pair are processed alike") {
    auto render = [](double intensity) {
        sas::Vibe w;
        sas::VibeParams p;
        p.on = true;
        p.speedHz = 3.0;
        p.intensity = intensity;
        w.setParams(p);
        w.prepare(kFs);
        auto z = tone(600.0, 96000);
        run(w, z, nullptr);
        return z;
    };
    CHECK(diff(render(1.0), render(0.1), 10000) > 100.0);
    sas::Vibe w;
    sas::VibeParams p;
    p.on = true;
    w.setParams(p);
    w.prepare(kFs);
    auto l = tone(500.0, 48000), r = l;
    run(w, l, &r);
    for (std::size_t i = 0; i < l.size(); ++i) REQUIRE(l[i] == r[i]);
}
