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
    CHECK(d < 1e-3);  // mono: both ears alike
    p.stereo = true;
    f.prepare(kFs);
    f.setParams(p);
    l = tone(48000);
    r = l;
    run(f, l, &r);
    d = 0.0;
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

#include "sas/chorus.h"
#include "sas/phaser.h"
#include "sas/vibe.h"

namespace {
// How different the two ears are after the effect, for a mono input put on both channels.
template <class Fx, class P>
double earGap(P p) {
    Fx fx;
    p.on = true;
    fx.prepare(kFs);
    fx.setParams(p);
    auto l = tone(48000), r = l;
    run(fx, l, &r);
    double d = 0.0;
    for (std::size_t i = 6000; i < l.size(); ++i) d += std::fabs(double(l[i]) - double(r[i]));
    return d;
}
}  // namespace

TEST_CASE("modulation effects: mono keeps both ears alike, stereo makes them differ", "[tremolo][flanger][stereo]") {
    {
        sas::PhaserParams p;
        CHECK(earGap<sas::Phaser>(p) < 1e-3);
        p.stereo = true;
        CHECK(earGap<sas::Phaser>(p) > 50.0);
    }
    {
        sas::VibeParams p;
        CHECK(earGap<sas::Vibe>(p) < 1e-3);
        p.stereo = true;
        CHECK(earGap<sas::Vibe>(p) > 50.0);
    }
    {
        sas::ChorusParams p;
        CHECK(earGap<sas::Chorus>(p) < 1e-3);
        p.stereo = true;
        CHECK(earGap<sas::Chorus>(p) > 50.0);
    }
    {
        sas::TremoloParams p;
        CHECK(earGap<sas::Tremolo>(p) < 1e-3);
        p.stereo = true;
        CHECK(earGap<sas::Tremolo>(p) > 50.0);
    }
}

#include "sas/delay.h"
#include "sas/plate.h"

TEST_CASE("delay and reverb: the channels option", "[stereo]") {
    {
        sas::DelayParams p;
        p.timeMs = 200.0;
        p.mix = 0.5;
        CHECK(earGap<sas::StereoDelay>(p) < 1e-3);  // mono: both ears alike, as it always was
        p.stereo = true;
        CHECK(earGap<sas::StereoDelay>(p) > 50.0);
    }
    {
        sas::PlateParams p;
        p.stereo = false;
        CHECK(earGap<sas::PlateReverb>(p) < 1e-3);  // the same tail on both sides
        p.stereo = true;
        CHECK(earGap<sas::PlateReverb>(p) > 5.0);
    }
}

#include "sas/wah.h"

namespace {
// RMS of what the wah leaves at one frequency, for a sine at that frequency.
double wahGain(sas::WahParams p, double hz) {
    sas::Wah w;
    p.on = true;
    w.prepare(kFs);
    w.setParams(p);
    std::vector<float> x(48000);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * float(std::sin(6.283185307179586 * hz * double(i) / kFs));
    auto in = x;
    run(w, x, nullptr);
    double a = 0.0, b = 0.0;
    for (std::size_t i = 12000; i < x.size(); ++i) {
        a += double(in[i]) * in[i];
        b += double(x[i]) * x[i];
    }
    return std::sqrt(b / a);
}
}  // namespace

TEST_CASE("wah: off is transparent; the pedal moves the peak between heel and toe", "[wah]") {
    sas::Wah w;
    sas::WahParams p;
    w.prepare(kFs);
    w.setParams(p);
    auto a = tone(48000), b = a;
    run(w, b, nullptr);
    CHECK(a == b);
    p.levelDb = 0.0;
    p.q = 6.0;
    p.position = 0.0;  // heel down: the peak is at 400 Hz
    CHECK(wahGain(p, 400.0) > 3.0 * wahGain(p, 2000.0));
    p.position = 1.0;  // toe down: at 2 kHz
    CHECK(wahGain(p, 2000.0) > 3.0 * wahGain(p, 400.0));
    p.heelHz = 700.0;
    p.position = 0.0;
    CHECK(wahGain(p, 700.0) > 3.0 * wahGain(p, 400.0));  // the range is the player's
}

TEST_CASE("wah: mix, level and Q do what they say", "[wah]") {
    sas::WahParams p;
    p.levelDb = 0.0;
    p.position = 0.0;
    p.q = 4.0;
    p.mix = 0.0;
    CHECK(std::fabs(wahGain(p, 3000.0) - 1.0) < 0.02);  // all dry
    p.mix = 1.0;
    const double flat = wahGain(p, 3000.0);
    p.levelDb = 6.0;
    CHECK(wahGain(p, 3000.0) > 1.9 * flat);
    p.levelDb = 0.0;
    p.q = 1.0;
    const double wide = wahGain(p, 1200.0);
    p.q = 10.0;
    CHECK(wahGain(p, 1200.0) < wide);  // a narrow peak leaves more of the neighbours out
}

TEST_CASE("wah: in envelope mode the playing moves it, in LFO mode it moves by itself, and nothing blows up", "[wah]") {
    for (int mode : {sas::WahParams::Envelope, sas::WahParams::Lfo}) {
        sas::Wah w;
        sas::WahParams p;
        p.on = true;
        p.mode = mode;
        p.q = 12.0;
        w.prepare(kFs);
        w.setParams(p);
        auto x = tone(96000);
        auto y = x;
        run(w, y, nullptr);
        double diff = 0.0;
        for (std::size_t i = 0; i < y.size(); ++i) {
            REQUIRE(std::isfinite(y[i]));
            REQUIRE(std::fabs(y[i]) < 8.0f);
            diff += std::fabs(double(x[i]) - double(y[i]));
        }
        CHECK(diff > 100.0);
    }
    // Envelope: a loud note and a soft one open it differently.
    auto level = [](double amp) {
        sas::Wah w;
        sas::WahParams p;
        p.on = true;
        p.mode = sas::WahParams::Envelope;
        p.levelDb = 0.0;
        w.prepare(kFs);
        w.setParams(p);
        std::vector<float> x(48000);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(amp * std::sin(6.283185307179586 * 1500.0 * double(i) / kFs));
        auto in = x;
        run(w, x, nullptr);
        double a = 0.0, b = 0.0;
        for (std::size_t i = 24000; i < x.size(); ++i) {
            a += double(in[i]) * in[i];
            b += double(x[i]) * x[i];
        }
        return std::sqrt(b / a);
    };
    CHECK(std::fabs(level(0.02) - level(0.6)) > 0.05);
}

#include "sas/fuzz.h"

namespace {
std::vector<float> fuzzRun(sas::FuzzParams p, double amp = 0.3, std::size_t n = 48000) {
    sas::Fuzz f;
    p.on = true;
    f.prepare(kFs);
    f.setParams(p);
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = float(amp * (std::sin(6.283185307179586 * 196.0 * double(i) / kFs) + 0.4 * std::sin(6.283185307179586 * 392.0 * double(i) / kFs)));
    run(f, x, nullptr);
    return x;
}
double rms(const std::vector<float>& v, std::size_t from = 12000) {
    double s = 0.0;
    for (std::size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}
// Energy above 1 kHz, a stand-in for "how much distortion": a one-pole high-pass.
double highs(const std::vector<float>& v) {
    float lp = 0.0f;
    double s = 0.0;
    const float a = float(1.0 - std::exp(-6.283185307179586 * 1000.0 / kFs));
    for (std::size_t i = 0; i < v.size(); ++i) {
        lp += a * (v[i] - lp);
        if (i > 12000) s += double(v[i] - lp) * (v[i] - lp);
    }
    return std::sqrt(s);
}
}  // namespace

TEST_CASE("fuzz: off is transparent; every type is bounded, has no DC, and more fuzz is more distortion", "[fuzz]") {
    sas::Fuzz f;
    sas::FuzzParams p;
    f.prepare(kFs);
    f.setParams(p);
    auto a = tone(48000), b = a;
    run(f, b, nullptr);
    CHECK(a == b);
    for (int type = 0; type < 3; ++type) {
        p.type = type;
        p.fuzz = 0.0;
        const auto low = fuzzRun(p, 0.03);  // a soft note
        p.fuzz = 1.0;
        const auto high = fuzzRun(p, 0.03);
        INFO("type " << type);
        double mean = 0.0;
        for (std::size_t i = 12000; i < high.size(); ++i) {
            REQUIRE(std::isfinite(high[i]));
            REQUIRE(std::fabs(high[i]) < 4.0f);
            mean += high[i];
        }
        CHECK(std::fabs(mean / double(high.size() - 12000)) < 0.02);
        CHECK(rms(high) > 0.01);
        CHECK(highs(high) / rms(high) > highs(low) / rms(low));
    }
}

TEST_CASE("fuzz: the types differ, the tone and the mix do what they say", "[fuzz]") {
    sas::FuzzParams p;
    p.fuzz = 0.8;
    p.type = 0;
    const auto face = fuzzRun(p);
    p.type = 1;
    const auto muff = fuzzRun(p);
    p.type = 2;
    const auto oct = fuzzRun(p);
    auto gap = [](const std::vector<float>& u, const std::vector<float>& v) {
        double d = 0.0;
        for (std::size_t i = 12000; i < u.size(); ++i) d += std::fabs(double(u[i]) - double(v[i]));
        return d;
    };
    CHECK(gap(face, muff) > 100.0);
    CHECK(gap(face, oct) > 100.0);
    CHECK(gap(muff, oct) > 100.0);
    p.type = 0;
    p.tone = 0.0;
    const auto dark = fuzzRun(p);
    p.tone = 1.0;
    const auto bright = fuzzRun(p);
    CHECK(highs(bright) / rms(bright) > highs(dark) / rms(dark));
    // Mix 0 is the dry guitar, late by the latency of the oversampling.
    p.mix = 0.0;
    const auto dryOnly = fuzzRun(p, 0.3);
    sas::Fuzz probe;
    probe.prepare(kFs);
    const int lat = int(std::lround(probe.latencySamples()));
    for (std::size_t i = 20000; i < 20100; ++i) {
        const double t = double(i - std::size_t(lat)) / kFs;
        const float expect = float(0.3 * (std::sin(6.283185307179586 * 196.0 * t) + 0.4 * std::sin(6.283185307179586 * 392.0 * t)));
        REQUIRE(std::fabs(dryOnly[i] - expect) < 2e-3f);
    }
}
