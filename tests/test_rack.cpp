#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/rack.h"

using namespace sas;

namespace {

struct Gain final : Module {
    explicit Gain(float g) : g_(g) {}
    void prepare(double, int) override {}
    void process(float* l, float*, std::size_t n) override { for (std::size_t i = 0; i < n; ++i) l[i] *= g_; }
    bool active() const override { return on; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {}
    float g_;
    bool on = true;
};

struct Add final : Module {
    explicit Add(float a) : a_(a) {}
    void prepare(double, int) override {}
    void process(float* l, float*, std::size_t n) override { ++calls; for (std::size_t i = 0; i < n; ++i) l[i] += a_; }
    bool active() const override { return on; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {}
    float a_;
    bool on = true;
    int calls = 0;
};

// Makes a stereo pair out of mono: r = -l.
struct Invert final : Module {
    void prepare(double, int) override {}
    void process(float* l, float* r, std::size_t n) override { for (std::size_t i = 0; i < n; ++i) r[i] = -l[i]; }
    bool active() const override { return true; }
    Channels channels() const override { return Channels::MonoToStereo; }
    void reset() override {}
};

RackOrder order(std::initializer_list<SlotRef> refs) {
    RackOrder o;
    for (const auto& r : refs) o.slots[std::size_t(o.count++)] = r;
    return o;
}

constexpr SlotRef kA{ModuleType::Gate, 0}, kB{ModuleType::Compressor, 0}, kC{ModuleType::Overdrive, 0};

}  // namespace

TEST_CASE("rack runs modules in the published order", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 64);
    rack.setModule(kA, std::make_unique<Gain>(2.0f));
    rack.setModule(kB, std::make_unique<Add>(1.0f));
    std::vector<float> l(8, 3.0f), r(8, 0.0f);
    rack.publish(order({kA, kB}));
    rack.process(l.data(), r.data(), l.size());
    CHECK(l[0] == 7.0f);  // 2x + 1
    std::fill(l.begin(), l.end(), 3.0f);
    rack.publish(order({kB, kA}));
    rack.process(l.data(), r.data(), l.size());
    CHECK(l[0] == 8.0f);  // 2(x + 1)
}

TEST_CASE("mono-only module after a stereo one sums to mono", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 64);
    rack.setModule(kA, std::make_unique<Invert>());
    rack.setModule(kB, std::make_unique<Gain>(1.0f));
    std::vector<float> l(16), r(16, 0.0f);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = float(std::sin(0.3 * double(i)));
    rack.publish(order({kA, kB}));
    const bool stereo = rack.process(l.data(), r.data(), l.size());
    CHECK_FALSE(stereo);  // the gain collapsed the pair
    for (float v : l) { CHECK(std::isfinite(v)); CHECK(v == 0.0f); }  // (x + -x) / 2
    rack.publish(order({kA}));
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = 1.0f;
    CHECK(rack.process(l.data(), r.data(), l.size()));
    CHECK(r[0] == -1.0f);
}

TEST_CASE("inactive modules are skipped and cost nothing", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 64);
    auto add = std::make_unique<Add>(1.0f);
    auto* raw = add.get();
    raw->on = false;
    rack.setModule(kA, std::move(add));
    std::vector<float> l(8, 5.0f), r(8, 0.0f);
    rack.publish(order({kA}));
    rack.process(l.data(), r.data(), l.size());
    CHECK(raw->calls == 0);
    CHECK(l[0] == 5.0f);
    raw->on = true;
    rack.process(l.data(), r.data(), l.size());
    CHECK(raw->calls == 1);
}

TEST_CASE("module type names round trip and unknown names are refused", "[rack]") {
    for (int t = 0; t <= int(ModuleType::Reverb); ++t) {
        const auto type = static_cast<ModuleType>(t);
        const auto back = moduleTypeFromName(moduleTypeName(type));
        REQUIRE(back.has_value());
        CHECK(*back == type);
    }
    CHECK_FALSE(moduleTypeFromName("Foo").has_value());
}

// ---------------------------------------------------------------- structure changes while playing

#include "sas/rack_modules.h"

namespace {

float maxStep(const std::vector<float>& v) {
    float m = 0.0f;
    for (std::size_t i = 1; i < v.size(); ++i) m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}

// Plays a 1 kHz sine for `blocks` blocks of 64 samples, changing the order after `at` of them.
std::vector<float> playAndReorder(Transition how, int at, int blocks) {
    Rack rack;
    rack.prepare(48000.0, 64);
    rack.setModule(kA, std::make_unique<Gain>(2.0f));
    rack.setModule(kB, std::make_unique<Add>(1.0f));
    rack.publish(order({kA, kB}));
    std::vector<float> out;
    std::vector<float> l(64), r(64);
    for (int b = 0; b < blocks; ++b) {
        if (b == at) rack.publish(order({kB, kA}), how);
        for (std::size_t i = 0; i < 64; ++i) l[i] = 0.5f * float(std::sin(2.0 * 3.14159265358979 * 1000.0 * double(out.size() + i) / 48000.0));
        rack.process(l.data(), r.data(), 64);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

}  // namespace

TEST_CASE("reordering a rack does not click when it ducks, and does without it", "[rack]") {
    const auto ducked = playAndReorder(Transition::Duck, 10, 40);
    const auto cut = playAndReorder(Transition::Immediate, 10, 40);
    CHECK(maxStep(ducked) < 0.2f);
    CHECK(maxStep(cut) > 0.5f);  // the DC step between 2x+1 and 2(x+1) is what the duck hides
    // After the transition the new order is in force: 2(x+1) swings between 1 and 3.
    float lo = 9.0f;
    for (std::size_t i = ducked.size() - 480; i < ducked.size(); ++i) lo = std::min(lo, ducked[i]);
    CHECK(lo > 0.9f);
}

TEST_CASE("a reverb tail survives being moved", "[rack]") {
    auto tailEnergy = [](bool move) {
        Rack rack;
        rack.prepare(48000.0, 64);
        auto reverb = std::make_unique<ReverbModule>();
        PlateParams p;
        p.on = true;
        p.mix = 1.0;
        reverb->setParams(p);
        rack.setModule(kA, std::move(reverb));
        rack.setModule(kB, std::make_unique<Gain>(1.0f));
        rack.publish(order({kA, kB}));
        std::vector<float> l(64), r(64);
        double energy = 0.0;
        for (int b = 0; b < 160; ++b) {
            if (b == 40 && move) rack.publish(order({kB, kA}), Transition::Duck);
            for (std::size_t i = 0; i < 64; ++i) l[i] = b < 10 ? 0.3f * float(std::sin(0.2 * double(b * 64 + int(i)))) : 0.0f;
            std::fill(r.begin(), r.end(), 0.0f);
            const bool stereo = rack.process(l.data(), r.data(), 64);
            if (b >= 80) for (std::size_t i = 0; i < 64; ++i) energy += double(l[i]) * l[i] + (stereo ? double(r[i]) * r[i] : 0.0);
        }
        return energy;
    };
    const double still = tailEnergy(false), moved = tailEnergy(true);
    REQUIRE(still > 0.0);
    CHECK(moved >= 0.5 * still);
}

// ---------------------------------------------------------------- cost per module

namespace {
// Burns some time so the clock has something to read.
struct Busy final : Module {
    void prepare(double, int) override {}
    void process(float* l, float*, std::size_t n) override {
        float acc = 0.0f;
        for (int k = 0; k < 200; ++k)
            for (std::size_t i = 0; i < n; ++i) acc += std::sin(l[i] + float(k));
        l[0] = acc * 0.0f + l[0];
    }
    bool active() const override { return on; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {}
    bool on = true;
};
}  // namespace

TEST_CASE("moduleCpu is zero for modules that do not run and positive for a busy one", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 512);
    rack.setModule(kA, std::make_unique<Busy>());
    auto idle = std::make_unique<Busy>();
    idle->on = false;
    rack.setModule(kB, std::move(idle));
    rack.publish(order({kA, kB}));
    std::vector<float> l(512, 0.1f), r(512, 0.0f);
    for (int i = 0; i < 20; ++i) rack.process(l.data(), r.data(), l.size());
    CHECK(rack.moduleCpu(kA) > 0.0f);
    CHECK(rack.moduleCpu(kB) == 0.0f);
    CHECK(rack.moduleCpu(kC) == 0.0f);  // no module at all
}

TEST_CASE("the channels of every module type are known without building it", "[rack]") {
    CHECK(moduleChannels(ModuleType::Amp) == Channels::MonoOnly);
    CHECK(moduleChannels(ModuleType::Cabinet) == Channels::MonoToStereo);
    CHECK(moduleChannels(ModuleType::Phaser) == Channels::Flexible);
    CHECK(moduleChannels(ModuleType::Reverb) == Channels::Stereo);
}

// ---------------------------------------------------------------- the review's findings: lifetime and removal

namespace {
int gDestroyed = 0;
struct Tracked final : Module {
    ~Tracked() override { ++gDestroyed; }
    void prepare(double, int) override {}
    void process(float* l, float*, std::size_t n) override { for (std::size_t i = 0; i < n; ++i) l[i] += 1.0f; }
    bool active() const override { return true; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {}
};
}  // namespace

TEST_CASE("a removed module is freed only after the audio thread has applied the order without it", "[rack]") {
    gDestroyed = 0;
    Rack rack;
    rack.prepare(48000.0, 64);
    rack.setModule(kA, std::make_unique<Tracked>());
    rack.publish(order({kA}));
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    rack.process(l.data(), r.data(), 64);
    rack.publish(order({}), Transition::Duck);
    rack.removeModule(kA);
    CHECK(rack.module(kA) == nullptr);
    rack.setModule(kB, std::make_unique<Gain>(1.0f));  // other message-thread calls must not free it either
    rack.setModule(kC, std::make_unique<Gain>(1.0f));
    rack.collect();
    CHECK(gDestroyed == 0);  // the audio thread has not run a block since: it may still be inside it
    for (int b = 0; b < 20; ++b) rack.process(l.data(), r.data(), 64);  // the fade out and in
    rack.collect();
    CHECK(gDestroyed == 1);
}

TEST_CASE("a module being removed keeps playing through the fade out", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 64);
    rack.setModule(kA, std::make_unique<Tracked>());
    rack.publish(order({kA}));
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    rack.process(l.data(), r.data(), 64);
    rack.publish(order({}), Transition::Duck);
    rack.removeModule(kA);
    std::fill(l.begin(), l.end(), 0.0f);
    rack.process(l.data(), r.data(), 64);
    CHECK(l[0] > 0.9f);  // still the module's +1, faded only slightly: the tail is not cut
}

// ---------------------------------------------------------------- parallel paths: a splitter, two lanes and a mixer

namespace {
constexpr SlotRef kS{ModuleType::Splitter, 0}, kM{ModuleType::Mixer, 0};

// Delays by n samples and says so.
struct Lag final : Module {
    explicit Lag(int n) : n_(n), buf_(std::size_t(n) + 1, 0.0f), declared_(double(n)) {}
    void prepare(double, int) override {}
    void process(float* l, float*, std::size_t count) override {
        for (std::size_t i = 0; i < count; ++i) {
            buf_[pos_] = l[i];
            const std::size_t at = (pos_ + buf_.size() - std::size_t(n_)) % buf_.size();
            l[i] = buf_[at];
            pos_ = (pos_ + 1) % buf_.size();
        }
    }
    bool active() const override { return true; }
    Channels channels() const override { return Channels::MonoOnly; }
    double latencySamples() const override { return declared_; }
    void reset() override {}
    int n_;
    std::vector<float> buf_;
    std::size_t pos_ = 0;
    double declared_;
};

// The order of the entries, each with its lane (0: shared or lane A, 1: lane B).
RackOrder lanesOf(std::initializer_list<std::pair<SlotRef, int>> entries) {
    RackOrder o;
    for (const auto& e : entries) {
        o.lane[std::size_t(o.count)] = std::uint8_t(e.second);
        o.slots[std::size_t(o.count++)] = e.first;
    }
    return o;
}

void setMixer(Rack& rack, const MixerParams& p) { static_cast<MixerModule*>(rack.module(kM))->setParams(p); }
}  // namespace

TEST_CASE("an order with a splitter and a mixer runs two lanes and sums them", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 128);
    rack.setModule(kS, std::make_unique<SplitterModule>());
    rack.setModule(kM, std::make_unique<MixerModule>());
    rack.setModule(kA, std::make_unique<Gain>(2.0f));
    rack.setModule(kB, std::make_unique<Add>(1.0f));
    rack.publish(lanesOf({{kS, 0}, {kA, 0}, {kB, 1}, {kM, 0}}));
    std::vector<float> l(128, 3.0f), r(128, 0.0f);
    const bool stereo = rack.process(l.data(), r.data(), l.size());
    CHECK(stereo);
    CHECK(l[5] == 10.0f);  // 2x from lane A plus x + 1 from lane B
    CHECK(r[5] == 10.0f);  // both at the centre: the same in both ears
}

TEST_CASE("modules before the splitter and after the mixer are shared", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 128);
    rack.setModule(kS, std::make_unique<SplitterModule>());
    rack.setModule(kM, std::make_unique<MixerModule>());
    rack.setModule(kA, std::make_unique<Gain>(2.0f));   // before the splitter
    rack.setModule(kB, std::make_unique<Add>(1.0f));    // lane A
    rack.setModule(kC, std::make_unique<Gain>(10.0f));  // after the mixer
    rack.publish(lanesOf({{kA, 0}, {kS, 0}, {kB, 0}, {kM, 0}, {kC, 0}}));
    std::vector<float> l(128, 1.0f), r(128, 0.0f);
    rack.process(l.data(), r.data(), l.size());
    // before the splitter: x2 = 2; lane A adds 1 = 3, lane B is empty = 2; the mixer sums them = 5; after it: x10
    CHECK(l[5] == 50.0f);
}

TEST_CASE("the lanes are aligned in time", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 128);
    rack.setModule(kS, std::make_unique<SplitterModule>());
    rack.setModule(kM, std::make_unique<MixerModule>());
    rack.setModule(kA, std::make_unique<Gain>(1.0f));
    rack.setModule(kB, std::make_unique<Lag>(37));
    rack.publish(lanesOf({{kS, 0}, {kA, 0}, {kB, 1}, {kM, 0}}));
    std::vector<float> l(128, 0.0f), r(128, 0.0f);
    l[0] = 1.0f;
    rack.process(l.data(), r.data(), l.size());
    CHECK(l[37] == 2.0f);  // both impulses land together
    CHECK(l[0] == 0.0f);
    CHECK(rack.latencySamples() == 37.0);
}

TEST_CASE("pan and phase of the mixer", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 128);
    rack.setModule(kS, std::make_unique<SplitterModule>());
    rack.setModule(kM, std::make_unique<MixerModule>());
    rack.setModule(kA, std::make_unique<Gain>(2.0f));
    rack.setModule(kB, std::make_unique<Gain>(3.0f));
    rack.publish(lanesOf({{kS, 0}, {kA, 0}, {kB, 1}, {kM, 0}}));
    MixerParams p;
    p.panA = -1.0f;
    p.panB = 1.0f;
    setMixer(rack, p);
    std::vector<float> l(128, 1.0f), r(128, 0.0f);
    REQUIRE(rack.process(l.data(), r.data(), l.size()));
    CHECK(l[5] == 2.0f);  // lane A only in the left ear
    CHECK(r[5] == 3.0f);  // lane B only in the right ear
    // The same gain in both lanes, one of them upside down: nothing is left.
    rack.setModule(kB, std::make_unique<Gain>(2.0f));
    p = MixerParams{};
    p.invertB = true;
    setMixer(rack, p);
    std::fill(l.begin(), l.end(), 1.0f);
    rack.process(l.data(), r.data(), l.size());
    CHECK(std::abs(l[5]) < 1e-6f);
}

TEST_CASE("an invalid lane layout is told apart", "[rack]") {
    CHECK(lanesOf({{kA, 0}, {kB, 0}}).valid());
    CHECK(lanesOf({{kS, 0}, {kA, 0}, {kB, 1}, {kM, 0}}).valid());
    CHECK_FALSE(lanesOf({{kA, 0}, {kB, 1}}).valid());                                   // lane B outside a parallel
    CHECK_FALSE(lanesOf({{kM, 0}, {kA, 0}, {kS, 0}}).valid());                          // the mixer before the splitter
    CHECK_FALSE(lanesOf({{kS, 0}, {kA, 0}}).valid());                                   // a splitter without a mixer
    CHECK_FALSE(lanesOf({{kS, 0}, {kM, 0}, {{ModuleType::Splitter, 1}, 0}, {kM, 0}}).valid());  // two of them
}

TEST_CASE("a change of the lane latency fades instead of clicking", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 64);
    rack.setModule(kS, std::make_unique<SplitterModule>());
    rack.setModule(kM, std::make_unique<MixerModule>());
    rack.setModule(kA, std::make_unique<Gain>(1.0f));
    auto lag = std::make_unique<Lag>(0);
    auto* lagPtr = lag.get();
    rack.setModule(kB, std::move(lag));
    rack.publish(lanesOf({{kS, 0}, {kA, 0}, {kB, 1}, {kM, 0}}));
    std::vector<float> out, l(64), r(64);
    for (int b = 0; b < 60; ++b) {
        if (b == 20) lagPtr->declared_ = 100.0;  // the lane now says it is 100 samples late
        for (std::size_t i = 0; i < 64; ++i) l[i] = 0.5f * float(std::sin(2.0 * 3.14159265358979 * 1000.0 * double(out.size() + i) / 48000.0));
        rack.process(l.data(), r.data(), 64);
        out.insert(out.end(), l.begin(), l.end());
    }
    float biggest = 0.0f;
    for (std::size_t i = 1; i < out.size(); ++i) biggest = std::max(biggest, std::abs(out[i] - out[i - 1]));
    CHECK(biggest < 0.35f);
}

namespace {
// An amp that is not ready yet: it reports itself inactive, like an AmpModule whose circuits are still being built.
struct LateAmp final : Module {
    void prepare(double, int) override {}
    void process(float* l, float*, std::size_t n) override { for (std::size_t i = 0; i < n; ++i) l[i] *= 3.0f; }
    bool active() const override { return ready; }
    bool isAmp() const override { return true; }
    Channels channels() const override { return Channels::MonoOnly; }
    void reset() override {}
    bool ready = false;
};
}  // namespace

TEST_CASE("a lane whose amp is not ready is silent and comes in smoothly", "[rack]") {
    Rack rack;
    rack.prepare(48000.0, 64);
    rack.setModule(kS, std::make_unique<SplitterModule>());
    rack.setModule(kM, std::make_unique<MixerModule>());
    rack.setModule(kA, std::make_unique<Gain>(1.0f));
    auto amp = std::make_unique<LateAmp>();
    auto* ampPtr = amp.get();
    rack.setModule({ModuleType::Amp, 1}, std::move(amp));
    rack.publish(lanesOf({{kS, 0}, {kA, 0}, {SlotRef{ModuleType::Amp, 1}, 1}, {kM, 0}}));
    std::vector<float> l(64, 1.0f), r(64, 0.0f);
    rack.process(l.data(), r.data(), 64);
    CHECK(l[10] == 1.0f);  // lane A alone: lane B has nothing to play yet
    ampPtr->ready = true;
    float previous = 1.0f, biggestStep = 0.0f;
    for (int b = 0; b < 12; ++b) {
        std::fill(l.begin(), l.end(), 1.0f);
        rack.process(l.data(), r.data(), 64);
        for (float v : l) {
            biggestStep = std::max(biggestStep, std::abs(v - previous));
            previous = v;
        }
    }
    CHECK(l[10] == 4.0f);          // both lanes, once it is ready: 1 + 3
    CHECK(biggestStep < 0.05f);    // and it arrived by a ramp, not a jump
}
