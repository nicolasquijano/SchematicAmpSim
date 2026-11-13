#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/leslie.h"

using namespace sas;

// The stereo sway: the correlation between the level (rms in 10 ms blocks) of the left and the right channel for a 2 kHz tone through the horn at fast speed.
// Close to +1 when the two channels swell together (nearly mono), negative when they sway against each other.
static double levelCorrelation(double width, double* meanDb = nullptr) {
    constexpr double fs = 48000.0, kPi = 3.14159265358979323846;
    Leslie les;
    les.prepare(fs);
    LeslieParams p;
    p.on = true;
    p.speed = 2;
    p.depth = 0.8;
    p.balance = 0.8;
    p.width = width;
    les.setParams(p);
    std::vector<float> l(std::size_t(10.0 * fs)), r(l.size());
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = float(0.1 * std::sin(2 * kPi * 2000.0 * double(i) / fs));
    les.process(l.data(), r.data(), l.size());
    const std::size_t block = std::size_t(0.01 * fs);
    std::vector<double> a, b;
    for (std::size_t at = l.size() / 2; at + block <= l.size(); at += block) {
        double el = 0, er = 0;
        for (std::size_t i = at; i < at + block; ++i) { el += double(l[i]) * l[i]; er += double(r[i]) * r[i]; }
        a.push_back(10.0 * std::log10(el / double(block) + 1e-12));
        b.push_back(10.0 * std::log10(er / double(block) + 1e-12));
    }
    double ma = 0, mb = 0;
    for (std::size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= double(a.size());
    mb /= double(b.size());
    double sab = 0, saa = 0, sbb = 0;
    for (std::size_t i = 0; i < a.size(); ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    if (meanDb) *meanDb = 0.5 * (ma + mb);
    return sab / std::sqrt(saa * sbb + 1e-30);
}

TEST_CASE("leslie width: opening the microphones makes the two channels sway against each other, and the level stays about the same", "[rack]") {
    double lvNarrow = 0, lvWide = 0;
    const double narrow = levelCorrelation(0.0, &lvNarrow), mid = levelCorrelation(0.5), wide = levelCorrelation(1.0, &lvWide);
    CHECK(narrow > 0.8);
    CHECK(narrow > mid);
    CHECK(mid > wide);
    CHECK(wide < -0.5);
    CHECK(std::abs(lvNarrow - lvWide) < 4.0);
}
