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
