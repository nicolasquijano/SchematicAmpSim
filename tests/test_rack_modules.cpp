#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/chorus.h"
#include "sas/flanger.h"
#include "sas/tremolo.h"
#include "sas/compressor.h"
#include "sas/delay.h"
#include "sas/eq.h"
#include "sas/gate.h"
#include "sas/nam_model.h"
#include "sas/oversample.h"
#include "sas/phaser.h"
#include "sas/pitch_shifter.h"
#include "sas/plate.h"
#include "sas/rack_modules.h"
#include "sas/ts808_fast.h"
#include "sas/vibe.h"

using namespace sas;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kLen = 6000, kBlock = 256;

std::vector<float> guitar(double amp) {
    std::vector<float> v(kLen);
    for (std::size_t i = 0; i < kLen; ++i) {
        const double t = double(i) / kFs;
        v[i] = float(amp * (std::sin(2 * 3.14159265358979 * 110.0 * t) + 0.4 * std::sin(2 * 3.14159265358979 * 1234.0 * t)));
    }
    return v;
}

// Runs a module over `in` in blocks; `stereo` hands it a right channel (a copy of the input).
void run(Module& m, std::vector<float>& l, std::vector<float>* r) {
    for (std::size_t at = 0; at < l.size(); at += kBlock) {
        const std::size_t n = std::min(kBlock, l.size() - at);
        m.process(l.data() + at, r ? r->data() + at : nullptr, n);
    }
}

}  // namespace

TEST_CASE("gate module equals the gate it wraps", "[rack]") {
    NoiseGate ref;
    ref.prepare(kFs);
    ref.setParams(true, -30.0, 120.0);
    GateModule m;
    m.prepare(kFs, int(kBlock));
    m.setParams(true, -30.0, 120.0);
    auto a = guitar(0.01), b = a;  // quiet enough to close the gate
    for (std::size_t at = 0; at < a.size(); at += kBlock) ref.process(a.data() + at, std::min(kBlock, a.size() - at));
    run(m, b, nullptr);
    CHECK(a == b);
    CHECK(m.channels() == Channels::MonoOnly);
    m.setParams(false, -30.0, 120.0);
    for (int i = 0; i < 400 && m.active(); ++i) {  // off: it stays active only while the gain recovers
        auto x = guitar(0.01);
        run(m, x, nullptr);
    }
    CHECK_FALSE(m.active());
}

TEST_CASE("compressor module equals the circuit it wraps and is buffered", "[rack]") {
    Compressor ref(kFs);
    CompControls c;
    c.sustain = 0.8;
    ref.setControls(c);
    CompressorModule m;
    m.prepare(kFs, int(kBlock));
    m.setParams(true, c);
    auto a = guitar(0.1), b = a;
    for (auto& v : a) v = float(ref.process(double(v)));
    run(m, b, nullptr);
    CHECK(a == b);
    CHECK(m.buffered());
    m.setParams(false, c);
    CHECK_FALSE(m.active());
}

TEST_CASE("overdrive module equals the pedal wired through a 2x oversampler and survives a hot input", "[rack]") {
    TubeScreamerFast ts(kFs * 2.0);
    Oversampler os(1);
    const TsControls ctl{0.7, 0.4, 0.6};
    ts.setControls(ctl);
    OverdriveModule m;
    m.prepare(kFs, int(kBlock));
    m.setParams(true, ctl);
    m.snapControls();  // no ramp from the defaults
    auto a = guitar(0.1), b = a;
    for (auto& v : a) v = os.process(v, [&](float s) { return float(ts.process(double(s))); });
    run(m, b, nullptr);
    CHECK(a == b);
    CHECK(m.buffered());
    auto hot = guitar(2.0);
    run(m, hot, nullptr);
    for (float v : hot) REQUIRE(std::isfinite(v));
}

TEST_CASE("modulation, eq, delay and reverb modules equal their effects", "[rack]") {
    SECTION("phaser (flexible: mono and stereo)") {
        Phaser ref;
        ref.prepare(kFs);
        PhaserParams p;
        p.on = true;
        ref.setParams(p);
        PhaserModule m;
        m.prepare(kFs, int(kBlock));
        m.setParams(p);
        auto a = guitar(0.1), b = a;
        for (std::size_t at = 0; at < a.size(); at += kBlock) ref.process(a.data() + at, nullptr, std::min(kBlock, a.size() - at));
        run(m, b, nullptr);
        CHECK(a == b);
        CHECK(m.channels() == Channels::Flexible);
    }
    SECTION("tremolo and flanger are flexible too") {
        CHECK(TremoloModule().channels() == Channels::Flexible);
        CHECK(FlangerModule().channels() == Channels::Flexible);
    }
    SECTION("chorus and vibe are flexible too") {
        ChorusModule c;
        VibeModule v;
        CHECK(c.channels() == Channels::Flexible);
        CHECK(v.channels() == Channels::Flexible);
    }
    SECTION("eq (stereo)") {
        ParametricEq ref;
        ref.prepare(kFs);
        EqParams p;
        p.on = true;
        p.band[2].gainDb = 6.0;
        ref.setParams(p);
        EqModule m;
        m.prepare(kFs, int(kBlock));
        m.setParams(p);
        auto la = guitar(0.1), lb = la, ra = la, rb = la;
        for (std::size_t at = 0; at < la.size(); at += kBlock) ref.process(la.data() + at, ra.data() + at, std::min(kBlock, la.size() - at));
        run(m, lb, &rb);
        CHECK(la == lb);
        CHECK(ra == rb);
        CHECK(m.channels() == Channels::Stereo);
    }
    SECTION("delay and reverb, with their tails") {
        StereoDelay ref;
        ref.prepare(kFs);
        DelayParams p;
        p.on = true;
        ref.setParams(p);
        DelayModule m;
        m.prepare(kFs, int(kBlock));
        m.setParams(p);
        auto la = guitar(0.1), lb = la, ra = la, rb = la;
        for (std::size_t at = 0; at < la.size(); at += kBlock) ref.process(la.data() + at, ra.data() + at, std::min(kBlock, la.size() - at));
        run(m, lb, &rb);
        CHECK(la == lb);
        p.on = false;
        m.setParams(p);
        CHECK(m.active());  // the tail is still ringing
        ReverbModule rm;
        rm.prepare(kFs, int(kBlock));
        CHECK_FALSE(rm.active());
    }
}

TEST_CASE("pitch module delays only while shifting", "[rack]") {
    PitchModule m;
    m.prepare(kFs, int(kBlock));
    m.setSemitones(0.0);
    CHECK_FALSE(m.active());
    CHECK(m.latencySamples() == 0.0);
    m.setSemitones(7.0);
    CHECK(m.active());
    CHECK(m.latencySamples() > 0.0);
    PitchShifter ref;
    ref.prepare(kFs);
    auto a = guitar(0.1), b = a;
    const double ratio = PitchShifter::ratioFromSemitones(7.0);
    for (std::size_t at = 0; at < a.size(); at += kBlock) ref.process(a.data() + at, a.data() + at, std::min(kBlock, a.size() - at), ratio);
    run(m, b, nullptr);
    CHECK(a == b);
}

TEST_CASE("NAM pedal module is inactive without a model and applies drive and level", "[rack]") {
    NamPedalModule m;
    m.prepare(kFs, int(kBlock));
    m.setParams(true, 0.0, 0.0);
    CHECK_FALSE(m.active());  // no model
    std::string error;
    const std::string path = std::string(SAS_NAM_EXAMPLES_DIR) + "/wavenet.nam";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::string bytes;
    char buf[4096];
    while (const std::size_t got = std::fread(buf, 1, sizeof buf, f)) bytes.append(buf, got);
    std::fclose(f);
    auto model = NamModel::loadFromMemory(bytes.data(), bytes.size(), "wavenet", kFs, int(kBlock), error);
    REQUIRE(model != nullptr);
    m.setModel(model.get());
    m.setParams(true, 0.0, 0.0);
    CHECK(m.active());
    CHECK(m.channels() == Channels::MonoOnly);
    auto a = guitar(0.1), b = a;
    std::vector<float> o(a.size());
    model->reset();
    for (std::size_t at = 0; at < a.size(); at += kBlock) {
        const std::size_t n = std::min(kBlock, a.size() - at);
        model->process(a.data() + at, o.data() + at, n);
    }
    model->reset();
    run(m, b, nullptr);
    CHECK(o == b);
}

// ---------------------------------------------------------------- the Mix control of the modulation effects

namespace {

template <class Dsp, class Mod, class Params>
void checkMix(const char* name) {
    INFO(name);
    const auto dry = guitar(0.1);
    Params p;
    p.on = true;
    // What the effect alone does.
    auto wet = dry;
    {
        Dsp ref;
        ref.prepare(kFs);
        ref.setParams(p);
        for (std::size_t at = 0; at < wet.size(); at += kBlock) ref.process(wet.data() + at, nullptr, std::min(kBlock, wet.size() - at));
    }
    float differs = 0.0f;
    for (std::size_t i = 0; i < dry.size(); ++i) differs = std::max(differs, std::fabs(wet[i] - dry[i]));
    REQUIRE(differs > 1e-3f);  // the effect does something audible, or the checks below would prove nothing

    auto render = [&](double mix) {
        Mod m;
        m.prepare(kFs, int(kBlock));
        Params q = p;
        q.mix = mix;
        m.setParams(q);
        auto out = dry;
        run(m, out, nullptr);
        return out;
    };
    CHECK(render(1.0) == wet);  // 100 %: the effect as it is, bit for bit
    const auto none = render(0.0), half = render(0.5);
    for (std::size_t i = kBlock; i < dry.size(); ++i) {  // (the first block ramps from the full effect to the setting)
        CHECK(none[i] == Catch::Approx(dry[i]).margin(1e-6));
        CHECK(half[i] == Catch::Approx(dry[i] + 0.5f * (wet[i] - dry[i])).margin(1e-5));
    }
}

}  // namespace

TEST_CASE("the Mix of a modulation effect: 100 % is the effect, 0 % the dry signal, in between the blend", "[rack]") {
    checkMix<Phaser, PhaserModule, PhaserParams>("phaser");
    checkMix<Chorus, ChorusModule, ChorusParams>("chorus");
    checkMix<Vibe, VibeModule, VibeParams>("vibe");
    checkMix<Flanger, FlangerModule, FlangerParams>("flanger");
}
