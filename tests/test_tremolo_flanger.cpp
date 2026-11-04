// The tremolo and the flanger.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/flanger.h"
#include "sas/tremolo.h"

namespace {
constexpr double kFs = 48000.0;

std::vector<float> tone(std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = 0.3f * float(std::sin(6.283185307179586 * 330.0 * double(i) / kFs) + 0.5 * std::sin(6.283185307179586 * 1170.0 * double(i) / kFs));
    return v;
}
template <class Fx>
void run(Fx& fx, std::vector<float>& l, std::vector<float>* r) {
    for (std::size_t s = 0; s < l.size(); s += 256) {
        const std::size_t n = std::min<std::size_t>(256, l.size() - s);
        fx.process(l.data() + s, r ? r->data() + s : nullptr, n);
    }
}
float peak(const std::vector<float>& v, std::size_t from, std::size_t to) {
    float m = 0.0f;
    for (std::size_t i = from; i < to; ++i) m = std::max(m, std::fabs(v[i]));
    return m;
}
}  // namespace

TEST_CASE("tremolo: off is transparent; on moves the level between full and 1 - depth", "[tremolo]") {
    sas::Tremolo t;
    t.prepare(kFs);
    sas::TremoloParams p;
    auto a = tone(48000), b = a;
    t.setParams(p);
    run(t, b, nullptr);
    CHECK(a == b);
    for (int shape = 0; shape < 3; ++shape) {
        p.on = true;
        p.depth = 1.0;
        p.rateHz = 4.0;
        p.shape = shape;
        t.prepare(kFs);
        t.setParams(p);
        auto x = tone(96000);
        run(t, x, nullptr);
        float lo = 1e9f, hi = 0.0f;
        for (std::size_t at = 12000; at + 480 < x.size(); at += 480) {  // peaks over 10 ms windows
            const float m = peak(x, at, at + 480);
            lo = std::min(lo, m);
            hi = std::max(hi, m);
        }
        CHECK(hi > 0.35f);
        CHECK(lo < 0.1f * hi);
        CHECK(hi <= 0.46f);
    }
}

TEST_CASE("tremolo: depth 0 leaves the signal; stereo mode pans the two channels in opposite directions", "[tremolo]") {
    sas::Tremolo t;
    sas::TremoloParams p;
    p.on = true;
    p.depth = 0.0;
    t.prepare(kFs);
    t.setParams(p);
    auto a = tone(24000), b = a;
    run(t, b, nullptr);
    for (std::size_t i = 2000; i < a.size(); ++i) REQUIRE(std::fabs(a[i] - b[i]) < 1e-5f);
    p.depth = 1.0;
    p.stereo = true;
    t.prepare(kFs);
    t.setParams(p);
    auto l = tone(48000), r = l;
    run(t, l, &r);
    // When the left is at its peak the right is at its bottom, and the other way round.
    const auto src = tone(48000);
    double sumLR = 0.0, sumSame = 0.0;
    for (std::size_t i = 6000; i < l.size(); ++i) {
        const double gl = std::fabs(src[i]) > 0.05 ? double(l[i]) / src[i] : 0.0, gr = std::fabs(src[i]) > 0.05 ? double(r[i]) / src[i] : 0.0;
        sumLR += gl * gr;
        sumSame += gl * gl;
    }
    CHECK(sumLR < 0.5 * sumSame);
}

TEST_CASE("flanger: off is transparent; on changes the sound, stays bounded with strong feedback of both signs", "[flanger]") {
    sas::Flanger f;
    sas::FlangerParams p;
    f.prepare(kFs);
    f.setParams(p);
    auto a = tone(48000), b = a;
    run(f, b, nullptr);
    CHECK(a == b);
    for (double fb : {0.9, -0.9, 0.0}) {
        p.on = true;
        p.feedback = fb;
        f.prepare(kFs);
        f.setParams(p);
        auto x = tone(96000);
        auto y = x;
        run(f, y, nullptr);
        double diff = 0.0;
        for (std::size_t i = 12000; i < x.size(); ++i) {
            REQUIRE(std::isfinite(y[i]));
            REQUIRE(std::fabs(y[i]) < 4.0f);
            diff += std::fabs(double(x[i]) - double(y[i]));
        }
        CHECK(diff > 100.0);
    }
}

TEST_CASE("flanger: on a stereo pair the two ears differ; the settings change what it does", "[flanger]") {
    sas::Flanger f;
    sas::FlangerParams p;
    p.on = true;
    f.prepare(kFs);
    f.setParams(p);
    auto l = tone(48000), r = l;
    run(f, l, &r);
    double d = 0.0;
    for (std::size_t i = 6000; i < l.size(); ++i) d += std::fabs(double(l[i]) - double(r[i]));
    CHECK(d > 50.0);
    auto render = [&](double manual) {
        sas::Flanger g;
        sas::FlangerParams q;
        q.on = true;
        q.manual = manual;
        g.prepare(kFs);
        g.setParams(q);
        auto x = tone(48000);
        run(g, x, nullptr);
        return x;
    };
    const auto lowM = render(0.0), highM = render(1.0);
    double dd = 0.0;
    for (std::size_t i = 6000; i < lowM.size(); ++i) dd += std::fabs(double(lowM[i]) - double(highM[i]));
    CHECK(dd > 50.0);
}
