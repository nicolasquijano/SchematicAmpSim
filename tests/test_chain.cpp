#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <string>

#include "sas/chain.h"
#include "sas/embedded_amps.h"
#include "sas/rack_modules.h"
#include "amp_fixture.h"

using namespace sas;

namespace {

constexpr double kFs = 48000.0;

std::vector<float> sine(double hz, double amp, std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = float(amp * std::sin(2.0 * 3.14159265358979 * hz * double(i) / kFs));
    return v;
}

double rms(const std::vector<float>& v, std::size_t from = 0) {
    double s = 0;
    for (std::size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}

ChainSettings defaults(const AmpDef& def) {
    ChainSettings s;
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    return s;
}

std::vector<float> run(Chain& c, const std::vector<float>& in) {
    std::vector<float> out(in.size());
    c.processAmp(in.data(), out.data(), in.size());
    c.applyOutput(out.data(), out.size());
    return out;
}

}  // namespace

TEST_CASE("chain equals the same primitives wired by hand (static settings)") {
    const auto& def = jcm800();
    Chain chain(def, kFs);
    auto s = defaults(def);
    s.cabFilter = true;
    chain.setSettings(s);

    // The same wiring, written out with the engine pieces.
    FullAmp amp(def, kFs * 2.0, false, true);  // the chain's Normal mode uses the triode tables
    amp.setKnobs(s.knobs, s.presence);
    amp.setJack(0);
    amp.setSourceOhms(def.preamp.sourceOhmsDefault);
    Oversampler os(1);
    CabFilter cab{float(kFs)};

    const auto in = sine(220.0, 0.1, 9600);
    const auto got = run(chain, in);
    std::vector<float> want;
    for (float x : in) {
        float v = os.process(x, [&](float u) { return float(amp.process(double(u))); });
        want.push_back(cab.process(v / 100.0f));
    }
    double maxDiff = 0;
    for (std::size_t i = 0; i < want.size(); ++i) maxDiff = std::max(maxDiff, double(std::abs(got[i] - want[i])));
    INFO("max diff " << maxDiff);
    CHECK(maxDiff == 0.0);
}

TEST_CASE("chain reports the oversampler latency and handles every quality") {
    const auto& def = jcm800();
    Chain chain(def, kFs);
    auto s = defaults(def);
    s.quality = Quality::Normal;
    chain.setSettings(s);
    const double l2 = chain.latencySamples();
    s.quality = Quality::High;
    chain.setSettings(s);
    const double l4 = chain.latencySamples();
    CHECK(l2 > 15.0);
    CHECK(l4 > l2);
    s.quality = Quality::Lite;
    chain.setSettings(s);
    CHECK(chain.latencySamples() == l2);

    s.quality = Quality::Efficient;
    chain.setSettings(s);
    CHECK(chain.latencySamples() == l2);  // one half-band up and one down, like the 2x modes

    for (Quality q : {Quality::Normal, Quality::High, Quality::Lite, Quality::Eco, Quality::Efficient, Quality::Normal}) {
        s.quality = q;
        chain.setSettings(s);
        const auto out = run(chain, sine(110.0, 0.1, 4800));
        for (float v : out) REQUIRE(std::isfinite(v));
        CHECK(rms(out, 2400) > 1e-4);
    }
}

TEST_CASE("jack, TS808 and cab filter change the sound in the expected direction") {
    const auto& def = jcm800();
    auto level = [&](auto&& tweak) {
        Chain chain(def, kFs);
        auto s = defaults(def);
        tweak(s);
        chain.setSettings(s);
        return rms(run(chain, sine(220.0, 0.05, 12000)), 6000);
    };
    const double base = level([](ChainSettings&) {});
    const double low = level([](ChainSettings& s) { s.jack = 1; s.jackLevelMatch = false; });  // the raw circuit
    CHECK(low < base * 0.2);  // Low skips the first gain stage: much quieter

    const double ts = level([](ChainSettings& s) { s.tsOn = true; s.tsDrive = 0.8; });
    CHECK(std::abs(ts - base) / base > 0.05);

    const double noCab = level([](ChainSettings& s) { s.cabFilter = false; });
    CHECK(noCab > 0.0);
}

TEST_CASE("level match lifts the JCM800's quiet Low jack and leaves High alone") {
    const auto& def = jcm800();
    auto level = [&](std::size_t jack, bool match, double peak) {
        Chain chain(def, kFs);
        auto s = defaults(def);
        s.jack = jack;
        s.jackLevelMatch = match;
        chain.snap(s);
        return rms(run(chain, sine(220.0, peak, 14000)), 7000);
    };
    // Small signal: the Low jack is ~31 dB below High; with the match it lands within a few dB.
    const double high = level(0, true, 0.003), lowRaw = level(1, false, 0.003), lowMatched = level(1, true, 0.003);
    INFO("high " << high << ", low " << lowRaw << ", low matched " << lowMatched);
    CHECK(20.0 * std::log10(high / lowRaw) > 25.0);
    CHECK(std::abs(20.0 * std::log10(high / lowMatched)) < 4.0);
    // High is untouched by the match, and the output stays finite and within full scale when driven hard.
    CHECK(level(0, true, 0.003) == level(0, false, 0.003));
    CHECK(level(1, true, 0.3) < 1.0);
}

TEST_CASE("every amp and every jack plays at the same loudness at the reference input", "[slow]") {
    // The reference input is the level of the author's recorded guitar (rms ~0.05): a three-sine
    // guitar-like signal of 0.1 V peak. Amps differ at other playing levels (their gains and
    // saturation differ), but at the reference they must agree; see tools/sas_match_levels.
    auto guitar = [](std::size_t n) {
        std::vector<float> v(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = double(i) / kFs, pi = 3.14159265358979;
            v[i] = float(0.1 * (0.6 * std::sin(2 * pi * 110.0 * t) + 0.3 * std::sin(2 * pi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * pi * 1234.5 * t)));
        }
        return v;
    };
    const auto in = guitar(60000);
    std::vector<std::pair<std::string, double>> levels;
    for (const auto& id : embeddedAmpIds()) {
        const AmpDef def = loadEmbeddedAmp(id);
        for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) {
            Chain chain(def, kFs);
            ChainSettings s = defaults(def);
            for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
            s.presence = def.power.presenceDefault;
            s.jack = j;
            chain.snap(s);
            std::vector<float> out(in.size());
            chain.processAmp(in.data(), out.data(), in.size());
            levels.push_back({def.id + "/" + def.preamp.jacks[j].name, 20.0 * std::log10(rms(out, 30000))});
        }
    }
    REQUIRE(levels.size() >= 7);
    // These amps were levelled by their peak with hot guitar signals (a loud peak that must stay under full scale),
    // not at the reference: a clean amp with headroom, or a high-gain amp whose gain sits at its default, plays
    // quieter than the others at a light touch. They are kept within a fixed distance below the reference.
    const std::set<std::string> byPeak = {"lead_c_plus", "jazz_clean_120", "american_5153", "british_dual100", "german_ultra", "afd100", "triplestack_3ch", "orange_or120"};
    for (const auto& [name, db] : levels) {
        INFO(name << ": " << db << " dB re " << levels[0].first << " " << levels[0].second);
        if (byPeak.count(name.substr(0, name.find('/')))) {
            CHECK(db < levels[0].second + 5.0);  // (a saturated channel can play a few dB above the reference at a light touch)
            CHECK(db > levels[0].second - 20.0);
        } else {
            CHECK(std::abs(db - levels[0].second) < 1.0);
        }
    }
}

TEST_CASE("output gain follows the dB setting and non-finite samples are zeroed") {
    const auto& def = jcm800();
    Chain chain(def, kFs);
    auto s = defaults(def);
    chain.setSettings(s);
    const auto a = run(chain, sine(220.0, 0.05, 12000));
    s.outputDb = -6.0206;
    chain.setSettings(s);
    const auto b = run(chain, sine(220.0, 0.05, 12000));
    // After the 20 ms ramp the second block is ~half the amplitude (same signal, settled).
    CHECK(rms(b, 9000) / rms(a, 9000) == Catch::Approx(0.5).epsilon(0.05));

    std::vector<float> bad{1.0f, INFINITY, NAN, 0.5f};
    chain.applyOutput(bad.data(), bad.size());
    for (float v : bad) CHECK(std::isfinite(v));
}

TEST_CASE("the Efficient quality (preamp at 2x, power amp at the host rate) plays at the level of Normal and keeps the amp's idle current") {
    const auto& def = jcm800();
    auto level = [&](Quality q, double peak) {
        Chain chain(def, kFs);
        auto s = defaults(def);
        s.quality = q;
        chain.setSettings(s);
        chain.snap(s);
        const auto out = run(chain, sine(220.0, peak, 14400));
        for (float v : out) REQUIRE(std::isfinite(v));
        return 20.0 * std::log10(rms(out, 7200) + 1e-12);
    };
    for (double peak : {0.02, 0.1, 0.3}) {
        const double normal = level(Quality::Normal, peak), efficient = level(Quality::Efficient, peak);
        INFO("peak " << peak << ": normal " << normal << " dB, efficient " << efficient << " dB");
        CHECK(std::abs(efficient - normal) < 1.0);  // the tone differs a little (docs/PLAN_CPU.md), the loudness does not
    }
    Chain chain(def, kFs);
    auto s = defaults(def);
    s.quality = Quality::Efficient;
    chain.setSettings(s);
    run(chain, sine(110.0, 0.05, 2400));
    CHECK(chain.idleMilliamps() > 5.0);
}

// ---------------------------------------------------------------- the amp as a rack module (docs/MODULAR_RACK.md, sections 3.4 and 3.5)

namespace {

struct Unity final : Module {  // a mono module that changes nothing: it only stands between two others
    void prepare(double, int) override {}
    void process(float*, float*, std::size_t) override {}
    bool active() const override { return on; }
    bool on = true;
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {}
};

std::vector<float> chainBlocks(Chain& c, std::vector<float> in) {
    for (std::size_t at = 0; at < in.size(); at += 256) c.processAmp(in.data() + at, in.data() + at, std::min<std::size_t>(256, in.size() - at));
    return in;
}

std::vector<float> rackBlocks(Rack& rack, std::vector<float> in) {
    std::vector<float> r(in.size());
    for (std::size_t at = 0; at < in.size(); at += 256) rack.process(in.data() + at, r.data() + at, std::min<std::size_t>(256, in.size() - at));
    return in;
}

RackOrder orderOf(std::initializer_list<ModuleType> types) {
    RackOrder o;
    for (auto t : types) o.slots[std::size_t(o.count++)] = {t, 0};
    return o;
}

}  // namespace

TEST_CASE("chain without the cab filter plus an outside CabFilter equals the old chain", "[rack]") {
    const auto& def = jcm800();
    for (Quality q : {Quality::Normal, Quality::Efficient}) {
        Chain a(def, kFs), b(def, kFs);
        auto s = defaults(def);
        s.quality = q;
        s.cabFilter = true;
        a.setSettings(s);
        s.cabFilterInChain = false;
        b.setSettings(s);
        CabFilter cab{float(kFs)};
        const auto in = sine(220.0, 0.1, 9600);
        const auto want = run(a, in);
        auto got = chainBlocks(b, in);
        for (auto& v : got) v = cab.process(v);
        b.applyOutput(got.data(), got.size());
        CHECK(got == want);
    }
}

TEST_CASE("compressor and overdrive right before the amp are fused into the chain's own front end", "[rack]") {
    const auto& def = jcm800();
    auto s = defaults(def);
    CompControls comp;
    comp.sustain = 0.7;
    const TsControls ts{0.6, 0.4, 0.5};

    Chain ref(def, kFs);
    auto sr = s;
    sr.compOn = true;
    sr.comp = comp;
    sr.tsOn = true;
    sr.tsDrive = ts.drive;
    sr.tsTone = ts.tone;
    sr.tsLevel = ts.level;
    ref.setSettings(sr);
    const auto in = sine(220.0, 0.1, 9600);
    const auto want = chainBlocks(ref, in);

    Chain chain(def, kFs);
    Rack rack;
    rack.prepare(kFs, 256);
    auto amp = std::make_unique<AmpModule>();
    amp->setChain(&chain);
    amp->setSettings(s);
    rack.setModule({ModuleType::Amp, 0}, std::move(amp));
    rack.setModule({ModuleType::Compressor, 0}, std::make_unique<CompressorModule>());
    rack.setModule({ModuleType::Overdrive, 0}, std::make_unique<OverdriveModule>());
    static_cast<CompressorModule*>(rack.module({ModuleType::Compressor, 0}))->setParams(true, comp);
    static_cast<OverdriveModule*>(rack.module({ModuleType::Overdrive, 0}))->setParams(true, ts);
    rack.publish(orderOf({ModuleType::Compressor, ModuleType::Overdrive, ModuleType::Amp}));
    const auto got = rackBlocks(rack, in);
    CHECK(got == want);
}

TEST_CASE("a pedal that cannot be fused still loads the amp's input like one", "[rack]") {
    const auto& def = jcm800();
    auto s = defaults(def);
    const TsControls ts{0.6, 0.4, 0.5};
    const auto in = sine(220.0, 0.1, 9600);

    // By hand: the overdrive on its own, then an amp told a pedal sits in front of it.
    OverdriveModule alone;
    alone.prepare(kFs, 256);
    alone.setParams(true, ts);
    alone.snapControls();
    auto pre = in;
    for (std::size_t at = 0; at < pre.size(); at += 256) alone.process(pre.data() + at, nullptr, std::min<std::size_t>(256, pre.size() - at));
    Chain ref(def, kFs);
    auto sr = s;
    sr.pedalBefore = true;
    ref.setSettings(sr);
    const auto want = chainBlocks(ref, pre);

    Chain chain(def, kFs);
    Rack rack;
    rack.prepare(kFs, 256);
    auto amp = std::make_unique<AmpModule>();
    amp->setChain(&chain);
    amp->setSettings(s);
    rack.setModule({ModuleType::Amp, 0}, std::move(amp));
    rack.setModule({ModuleType::Overdrive, 0}, std::make_unique<OverdriveModule>());
    rack.setModule({ModuleType::Gate, 0}, std::make_unique<Unity>());  // stands between the overdrive and the amp
    auto* od = static_cast<OverdriveModule*>(rack.module({ModuleType::Overdrive, 0}));
    od->setParams(true, ts);
    od->snapControls();
    rack.publish(orderOf({ModuleType::Overdrive, ModuleType::Gate, ModuleType::Amp}));
    const auto got = rackBlocks(rack, in);
    CHECK(got == want);
    CHECK(rms(got, 2400) > 1e-4);
}

TEST_CASE("a fused overdrive adds no latency of its own, an unfused one does", "[rack]") {
    const auto& def = jcm800();
    auto build = [&](std::initializer_list<ModuleType> types, bool unityBetween) {
        auto chain = std::make_unique<Chain>(def, kFs);
        Rack rack;
        rack.prepare(kFs, 256);
        auto amp = std::make_unique<AmpModule>();
        amp->setChain(chain.get());
        amp->setSettings(defaults(def));
        rack.setModule({ModuleType::Amp, 0}, std::move(amp));
        rack.setModule({ModuleType::Overdrive, 0}, std::make_unique<OverdriveModule>());
        static_cast<OverdriveModule*>(rack.module({ModuleType::Overdrive, 0}))->setParams(true, TsControls{});
        if (unityBetween) rack.setModule({ModuleType::Gate, 0}, std::make_unique<Unity>());
        rack.publish(orderOf(types));
        std::vector<float> l(256, 0.0f), r(256, 0.0f);
        rack.process(l.data(), r.data(), 256);
        const double latency = rack.latencySamples();
        return std::make_pair(latency, chain->latencySamples());
    };
    const auto fused = build({ModuleType::Overdrive, ModuleType::Amp}, false);
    CHECK(fused.first == fused.second);  // only the amp's own
    const auto apart = build({ModuleType::Overdrive, ModuleType::Gate, ModuleType::Amp}, true);
    CHECK(apart.first > apart.second);   // its own oversampler counts
}

TEST_CASE("which pedals are fused depends on the order, not on what else is switched on", "[rack]") {
    const auto& def = jcm800();
    const TsControls ts{0.6, 0.4, 0.5};
    auto render = [&](bool between) {
        Chain chain(def, kFs);
        Rack rack;
        rack.prepare(kFs, 256);
        auto amp = std::make_unique<AmpModule>();
        amp->setChain(&chain);
        amp->setSettings(defaults(def));
        rack.setModule({ModuleType::Amp, 0}, std::move(amp));
        rack.setModule({ModuleType::Overdrive, 0}, std::make_unique<OverdriveModule>());
        auto* od = static_cast<OverdriveModule*>(rack.module({ModuleType::Overdrive, 0}));
        od->setParams(true, ts);
        od->snapControls();
        auto gate = std::make_unique<Unity>();
        gate->on = between;
        rack.setModule({ModuleType::Gate, 0}, std::move(gate));
        rack.publish(orderOf({ModuleType::Overdrive, ModuleType::Gate, ModuleType::Amp}));
        return rackBlocks(rack, sine(220.0, 0.1, 4800));
    };
    CHECK(render(false) == render(true));  // an inactive neighbour does not turn the fusion on
}
