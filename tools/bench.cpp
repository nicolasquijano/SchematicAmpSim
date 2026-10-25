// Engine benchmark: CPU cost of one second of 96 kHz audio (= 2x oversampling of a 48 kHz host).
// Prints percent of one core. Same inputs as jcm800/examples/bench.rs so the two compare directly.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"
#include "sas/profile.h"
#include "sas/ts808_fast.h"

namespace {

std::vector<double> signal(double amp, std::size_t n, double fs) {
    std::uint64_t state = 0x123456789abcdef0ull;
    std::vector<double> v(n);
    const double pi = 3.14159265358979323846;
    for (std::size_t i = 0; i < n; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double noise = double(state >> 11) / double(1ull << 53) - 0.5;
        const double t = double(i) / fs;
        const double s = 0.6 * std::sin(2 * pi * 110.0 * t) + 0.3 * std::sin(2 * pi * 330.7 * t + 0.4) +
                         0.1 * std::sin(2 * pi * 1234.5 * t);
        v[i] = amp * (s + 0.02 * noise);
    }
    return v;
}

template <class F>
double percent(F&& run, double seconds) {
    const auto t0 = std::chrono::steady_clock::now();
    run();
    const std::chrono::duration<double> dt = std::chrono::steady_clock::now() - t0;
    return 100.0 * dt.count() / seconds;
}

}  // namespace

int main() {
    constexpr double fs = 96000.0, seconds = 3.0;
    const std::size_t n = std::size_t(fs * seconds);
    struct Case { const char* name; double volume, master, amp; };
    const Case cases[] = {{"clean", 0.2, 0.5, 0.05}, {"crunch", 0.6, 0.5, 0.15}, {"lead", 1.0, 0.8, 0.3}};
    double sink = 0;
    std::vector<std::pair<std::string, sas::AmpDef>> amps;
    for (const auto& id : sas::embeddedAmpIds()) amps.push_back({id, sas::loadEmbeddedAmp(id)});
    // SAS_BENCH_MONO=<path to afd100_preamp_full.net> also runs the unsplit AFD100 preamp for comparison.
    if (const char* mono = std::getenv("SAS_BENCH_MONO")) {
        sas::AmpDef d = sas::loadEmbeddedAmp("afd100");
        std::ifstream f(mono);
        std::stringstream ss;
        ss << f.rdbuf();
        d.preamp.netlistText = ss.str();
        d.preamp.stages.clear();
        amps.push_back({"afd100 (monolithic preamp)", d});
    }
    for (const auto& [id, def] : amps) {
        std::printf("== %s\n", id.c_str());
        for (const auto& c : cases) {
            std::vector<double> knobs;
            for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
            knobs[0] = c.volume;  // the first knob is the (first) volume in every amp
            for (std::size_t i = 0; i < knobs.size(); ++i)
                if (def.preamp.knobs[i].name == "master") knobs[i] = c.master;
            const auto in = signal(c.amp, n, fs);
#ifdef SAS_PROFILE
            // Lite runs only the preamp solver, so the counters isolate it.
            {
                sas::LiteAmp l(def, fs, true);
                l.setKnobs(knobs, def.power.presenceDefault);
                for (auto& v : sas::g_profile) v = 0;
                for (double x : in) sink += l.process(x);
                const double total = double(sas::g_profile[sas::ProfStep]);
                std::printf("  %-7s preamp cycles/sample %.0f: eval %2.0f%%  factor %2.0f%%  lu-solve %2.0f%%  S*r %2.0f%%  v=S*r-K*i %2.0f%%\n", c.name,
                            total / double(n), 100 * double(sas::g_profile[sas::ProfEval]) / total, 100 * double(sas::g_profile[sas::ProfFactor]) / total,
                            100 * double(sas::g_profile[sas::ProfSolve]) / total, 100 * double(sas::g_profile[sas::ProfMatvec]) / total,
                            100 * double(sas::g_profile[sas::ProfReconstruct]) / total);
            }
#endif
            sas::FullAmp full(def, fs);
            full.setKnobs(knobs, def.power.presenceDefault);
            const double pf = percent([&] { for (double x : in) sink += full.process(x); }, seconds);
            sas::LiteAmp lite(def, fs);
            lite.setKnobs(knobs, def.power.presenceDefault);
            const double pl = percent([&] { for (double x : in) sink += lite.process(x); }, seconds);
            sas::LiteAmp eco(def, fs, true);  // Lite power amp + triode tables
            eco.setKnobs(knobs, def.power.presenceDefault);
            const double pe = percent([&] { for (double x : in) sink += eco.process(x); }, seconds);
            const auto& ps = full.pre().solver(0);
            std::printf("  %-7s full %6.1f %%   lite %6.1f %%   eco %6.1f %%   | preamp: %zu stage(s), stage 1 = %zu nodes %zu ports, %.1f it/sample %llu cont\n",
                        c.name, pf, pl, pe, full.pre().stageCount(), ps.nodeCount(), ps.portCount(), double(ps.newtonIterations) / double(n),
                        (unsigned long long)ps.continuationRuns);
        }
    }
    {
        const auto in = signal(0.1, n, fs);
        sas::TubeScreamerFast ts(fs);
        const double p = percent([&] { for (double x : in) sink += ts.process(x); }, seconds);
        std::printf("ts808   fast %5.2f %%\n", p);
    }
    std::printf("(checksum %g)\n", sink);
}
