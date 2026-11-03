// CPU cost of every effect module of the rack, in percent of one core, at 48 kHz and 96 kHz (docs/MODULAR_RACK.md, section 6).
// SAS_BENCH_NAM=<path to a .nam model> also measures the NAM pedal.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include "sas/chorus.h"
#include "sas/compressor.h"
#include "sas/delay.h"
#include "sas/eq.h"
#include "sas/gate.h"
#include "sas/nam_model.h"
#include "sas/phaser.h"
#include "sas/pitch_shifter.h"
#include "sas/plate.h"
#include "sas/ts808_fast.h"
#include "sas/vibe.h"

namespace {

constexpr std::size_t kBlock = 256;

std::vector<float> noise(std::size_t n) {
    std::uint64_t s = 0x9e3779b97f4a7c15ull;
    std::vector<float> v(n);
    for (auto& x : v) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        x = 0.1f * (float(double(s >> 11) / double(1ull << 53)) - 0.5f);
    }
    return v;
}

// Runs `block(l, r, n)` over `seconds` of audio and returns the percent of one core.
double percent(double fs, double seconds, const std::function<void(float*, float*, std::size_t)>& block) {
    auto l = noise(std::size_t(fs * seconds)), r = l;
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < l.size(); at += kBlock) block(l.data() + at, r.data() + at, std::min(kBlock, l.size() - at));
    const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
    return 100.0 * dt.count() / seconds;
}

}  // namespace

int main() {
    constexpr double seconds = 5.0;
    std::string namBytes;
    if (const char* path = std::getenv("SAS_BENCH_NAM")) {
        std::ifstream f(path, std::ios::binary);
        namBytes.assign(std::istreambuf_iterator<char>(f), {});
    }
    std::printf("%-22s %10s %10s\n", "module (% of one core)", "48 kHz", "96 kHz");
    auto row = [&](const char* name, const std::function<std::function<void(float*, float*, std::size_t)>(double)>& make) {
        std::printf("%-22s", name);
        for (double fs : {48000.0, 96000.0}) std::printf(" %9.2f%%", percent(fs, seconds, make(fs)));
        std::printf("\n");
    };
    row("gate", [](double fs) {
        auto g = std::make_shared<sas::NoiseGate>();
        g->prepare(fs);
        g->setParams(true, -50.0, 120.0);
        return [g](float* l, float*, std::size_t n) { g->process(l, n); };
    });
    row("compressor", [](double fs) {
        auto c = std::make_shared<sas::Compressor>(fs);
        return [c](float* l, float*, std::size_t n) { for (std::size_t i = 0; i < n; ++i) l[i] = float(c->process(double(l[i]))); };
    });
    row("overdrive (TS, 2x)", [](double fs) {
        auto t = std::make_shared<sas::TubeScreamerFast>(fs * 2.0);
        return [t](float* l, float*, std::size_t n) { for (std::size_t i = 0; i < n; ++i) l[i] = float(t->process(double(l[i]))); };
    });
    row("phaser", [](double fs) {
        auto m = std::make_shared<sas::Phaser>();
        m->prepare(fs);
        sas::PhaserParams p;
        p.on = true;
        m->setParams(p);
        return [m](float* l, float* r, std::size_t n) { m->process(l, r, n); };
    });
    row("chorus", [](double fs) {
        auto m = std::make_shared<sas::Chorus>();
        m->prepare(fs);
        sas::ChorusParams p;
        p.on = true;
        m->setParams(p);
        return [m](float* l, float* r, std::size_t n) { m->process(l, r, n); };
    });
    row("vibe", [](double fs) {
        auto m = std::make_shared<sas::Vibe>();
        m->prepare(fs);
        sas::VibeParams p;
        p.on = true;
        m->setParams(p);
        return [m](float* l, float* r, std::size_t n) { m->process(l, r, n); };
    });
    row("pitch +7", [](double fs) {
        auto m = std::make_shared<sas::PitchShifter>();
        m->prepare(fs);
        const double ratio = sas::PitchShifter::ratioFromSemitones(7.0);
        return [m, ratio](float* l, float*, std::size_t n) { m->process(l, l, n, ratio); };
    });
    row("eq (5 bands)", [](double fs) {
        auto m = std::make_shared<sas::ParametricEq>();
        m->prepare(fs);
        sas::EqParams p;
        p.on = true;
        for (auto& b : p.band) b.gainDb = 4.0;
        m->setParams(p);
        return [m](float* l, float* r, std::size_t n) { m->process(l, r, n); };
    });
    row("delay", [](double fs) {
        auto m = std::make_shared<sas::StereoDelay>();
        m->prepare(fs);
        sas::DelayParams p;
        p.on = true;
        m->setParams(p);
        return [m](float* l, float* r, std::size_t n) { m->process(l, r, n); };
    });
    for (const bool shimmer : {false, true})
        row(shimmer ? "reverb + shimmer" : "reverb (plate)", [shimmer](double fs) {
            auto m = std::make_shared<sas::PlateReverb>();
            m->prepare(fs);
            sas::PlateParams p;
            p.on = true;
            p.shimmer = shimmer ? 0.5 : 0.0;
            m->setParams(p);
            return [m](float* l, float* r, std::size_t n) { m->process(l, r, n); };
        });
    if (!namBytes.empty())
        row("NAM pedal", [&](double fs) {
            std::string err;
            std::shared_ptr<sas::NamModel> m = sas::NamModel::loadFromMemory(namBytes.data(), namBytes.size(), "bench", fs, int(kBlock), err);
            if (!m) { std::fprintf(stderr, "NAM: %s\n", err.c_str()); std::exit(1); }
            return [m](float* l, float*, std::size_t n) { std::vector<float> o(n); m->process(l, o.data(), n); std::copy(o.begin(), o.end(), l); };
        });
    return 0;
}
