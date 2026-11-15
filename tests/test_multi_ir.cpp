// The multi impulse loader's core: the cursor weights and the baking of one impulse.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/multi_ir.h"

using namespace sas;

namespace {

struct Layout {
    float xs[kMultiIrSlots], ys[kMultiIrSlots], gain[kMultiIrSlots];
    Layout() {
        for (int k = 0; k < kMultiIrSlots; ++k) {
            multiIrDefaultPosition(k, xs[k], ys[k]);
            gain[k] = 1.0f;
        }
    }
};

double power(const float* w, int n) {
    double s = 0.0;
    for (int k = 0; k < n; ++k) s += double(w[k]) * w[k];
    return s;
}

}  // namespace

TEST_CASE("multi ir: the weights keep the same power wherever the cursor is", "[multiIr]") {
    Layout l;
    float w[kMultiIrSlots];
    for (float cx = 0.0f; cx <= 1.0f; cx += 0.125f)
        for (float cy = 0.0f; cy <= 1.0f; cy += 0.125f) {
            multiIrWeights(cx, cy, l.xs, l.ys, l.gain, kMultiIrSlots, 0.22f, 0.0f, w);
            INFO("cursor " << cx << ", " << cy);
            CHECK(power(w, kMultiIrSlots) == Catch::Approx(1.0).margin(1e-5));
        }
}

TEST_CASE("multi ir: on a point that impulse is heard almost alone, halfway two are heard equally", "[multiIr]") {
    Layout l;
    float w[kMultiIrSlots];
    multiIrWeights(l.xs[3], l.ys[3], l.xs, l.ys, l.gain, kMultiIrSlots, 0.15f, 0.001f, w);
    CHECK(w[3] > 0.95f);
    for (int k = 0; k < kMultiIrSlots; ++k)
        if (k != 3) CHECK(w[k] < 0.3f);
    multiIrWeights(0.5f * (l.xs[0] + l.xs[1]), 0.5f * (l.ys[0] + l.ys[1]), l.xs, l.ys, l.gain, kMultiIrSlots, 0.22f, 0.0f, w);
    CHECK(w[0] == Catch::Approx(w[1]).epsilon(1e-4));
    CHECK(w[0] > w[4]);
}

TEST_CASE("multi ir: empty slots get no weight and the others take their share", "[multiIr]") {
    Layout l;
    for (int k = 2; k < kMultiIrSlots; ++k) l.gain[k] = 0.0f;
    float w[kMultiIrSlots];
    multiIrWeights(0.5f, 0.5f, l.xs, l.ys, l.gain, kMultiIrSlots, 0.22f, 0.0f, w);
    for (int k = 2; k < kMultiIrSlots; ++k) CHECK(w[k] == 0.0f);
    CHECK(power(w, kMultiIrSlots) == Catch::Approx(1.0).margin(1e-5));
    // Nothing loaded: nothing to hear.
    for (auto& g : l.gain) g = 0.0f;
    multiIrWeights(0.5f, 0.5f, l.xs, l.ys, l.gain, kMultiIrSlots, 0.22f, 0.0f, w);
    CHECK(power(w, kMultiIrSlots) == 0.0);
}

TEST_CASE("multi ir: a cursor far from every point lets the nearest one through", "[multiIr]") {
    Layout l;
    float w[kMultiIrSlots];
    multiIrWeights(20.0f, 20.0f, l.xs, l.ys, l.gain, kMultiIrSlots, 0.05f, 0.0f, w);
    int heard = 0;
    for (int k = 0; k < kMultiIrSlots; ++k) heard += w[k] > 0.0f;
    CHECK(heard == 1);
}

TEST_CASE("multi ir: a level on a slot scales its weight and a weight under the floor is cut", "[multiIr]") {
    Layout l;
    float w[kMultiIrSlots], quiet[kMultiIrSlots];
    multiIrWeights(l.xs[2], l.ys[2], l.xs, l.ys, l.gain, kMultiIrSlots, 0.12f, 0.0f, w);
    l.gain[2] = 0.5f;
    multiIrWeights(l.xs[2], l.ys[2], l.xs, l.ys, l.gain, kMultiIrSlots, 0.12f, 0.0f, quiet);
    // (the others' share changes a little too: the level goes in after the weights are normalised)
    CHECK(quiet[2] == Catch::Approx(0.5f * w[2]).epsilon(0.05));
    multiIrWeights(l.xs[2], l.ys[2], l.xs, l.ys, l.gain, kMultiIrSlots, 0.05f, 0.01f, w);
    for (int k = 0; k < kMultiIrSlots; ++k)
        if (k != 2) CHECK(w[k] == 0.0f);
}

TEST_CASE("multi ir: baking an impulse sets its polarity, delay and filters", "[multiIr]") {
    constexpr double fs = 48000.0;
    std::vector<float> ir(2000, 0.0f);
    ir[10] = -1.0f;
    ir[40] = 0.4f;
    ir[41] = -0.3f;

    // Automatic polarity makes the biggest swing positive; "as it is" keeps it; "upside down" flips it.
    auto at = [&](int polarity, double delayMs) {
        IrBake b;
        b.polarity = polarity;
        b.delayMs = delayMs;
        return bakeImpulse(ir.data(), ir.size(), fs, b);
    };
    CHECK(at(0, 0.0)[10] == 1.0f);
    CHECK(at(1, 0.0)[10] == -1.0f);
    CHECK(at(2, 0.0)[10] == 1.0f);
    ir[10] = 1.0f;
    CHECK(at(0, 0.0)[10] == 1.0f);   // already positive: left alone
    CHECK(at(2, 0.0)[10] == -1.0f);

    const auto late = at(1, 1.0);  // 48 samples
    CHECK(late.size() == ir.size() + 48);
    CHECK(late[10 + 48] == 1.0f);
    CHECK(late[10] == 0.0f);

    // A high-pass takes the DC out of the impulse; a low-pass takes the top out of it.
    std::vector<float> dc(4000, 0.0f);
    for (int i = 0; i < 400; ++i) dc[std::size_t(i)] = float(std::exp(-i / 150.0));
    IrBake hp;
    hp.polarity = 1;
    hp.hpHz = 200.0;
    const auto hpOut = bakeImpulse(dc.data(), dc.size(), fs, hp);
    double sumIn = 0.0, sumOut = 0.0;
    for (float v : dc) sumIn += v;
    for (float v : hpOut) sumOut += v;
    CHECK(std::abs(sumOut) < 0.1 * std::abs(sumIn));

    std::vector<float> click(1000, 0.0f);
    click[100] = 1.0f;
    IrBake lp;
    lp.polarity = 1;
    lp.lpHz = 2000.0;
    const auto lpOut = bakeImpulse(click.data(), click.size(), fs, lp);
    double roughIn = 0.0, roughOut = 0.0;
    for (std::size_t i = 1; i < click.size(); ++i) roughIn += std::abs(click[i] - click[i - 1]);
    for (std::size_t i = 1; i < lpOut.size(); ++i) roughOut += std::abs(lpOut[i] - lpOut[i - 1]);
    CHECK(roughOut < 0.9 * roughIn);
    CHECK(lpOut.size() > click.size());  // room for the filter to ring out

    // No filter, no delay: the impulse as it was.
    IrBake none;
    none.polarity = 1;
    const auto same = bakeImpulse(ir.data(), ir.size(), fs, none);
    CHECK(same.size() == ir.size());
    CHECK(same[40] == 0.4f);
}

TEST_CASE("multi ir: alignMultiImpulses lines all the impulses up with the first", "[multiIr]") {
    constexpr double fs = 48000.0;
    auto burst = [](std::size_t start, float sign) {
        std::vector<float> x(2000, 0.0f);
        for (std::size_t i = start; i < x.size(); ++i) {
            const double t = double(i - start);
            x[i] = sign * float(std::exp(-t / 60.0) * (std::sin(2.0 * 3.14159265358979 * t / 96.0) + 0.4 * std::sin(2.0 * 3.14159265358979 * t / 23.0)));
        }
        return x;
    };
    const auto a = burst(30, 1.0f);    // the first
    const auto b = burst(50, -1.0f);   // 20 samples later and upside down
    const auto c = burst(15, 1.0f);    // 15 samples earlier
    const float* irs[kMultiIrSlots] = {a.data(), b.data(), nullptr, c.data()};
    std::size_t lengths[kMultiIrSlots] = {a.size(), b.size(), 0, c.size()};
    double rates[kMultiIrSlots] = {fs, fs, fs, fs};
    bool loaded[kMultiIrSlots] = {true, true, false, true};
    const auto r = alignMultiImpulses(irs, lengths, rates, loaded);
    CHECK(r.reference == 0);
    CHECK_FALSE(r.flip[0]);
    CHECK(r.flip[1]);
    CHECK_FALSE(r.flip[3]);
    // The latest one (b, +20) is not delayed; the first is delayed by 20, c (15 early) by 35.
    CHECK(r.extraDelay[1] == 0);
    CHECK(r.extraDelay[0] == 20);
    CHECK(r.extraDelay[3] == 35);
    CHECK(r.extraDelay[2] == 0);
    // Applied, the three bursts start together.
    auto startOf = [](const std::vector<float>& x, int delay) {
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::abs(x[i]) > 1e-6f) return int(i) + delay;
        return -1;
    };
    CHECK(startOf(a, r.extraDelay[0]) == startOf(b, r.extraDelay[1]));
    CHECK(startOf(c, r.extraDelay[3]) == startOf(b, r.extraDelay[1]));
}

TEST_CASE("multi ir: one impulse, or none, has nothing to line up with", "[multiIr]") {
    std::vector<float> a(500, 0.0f);
    a[10] = 1.0f;
    const float* irs[kMultiIrSlots] = {a.data()};
    std::size_t lengths[kMultiIrSlots] = {a.size()};
    double rates[kMultiIrSlots] = {48000.0};
    bool loaded[kMultiIrSlots] = {true};
    auto r = alignMultiImpulses(irs, lengths, rates, loaded);
    CHECK(r.reference == -1);
    CHECK(r.extraDelay[0] == 0);
    loaded[0] = false;
    r = alignMultiImpulses(irs, lengths, rates, loaded);
    CHECK(r.reference == -1);
}
