// Engine benchmark: CPU cost of one second of 96 kHz audio (= 2x oversampling of a 48 kHz host).
// Prints percent of one core. Same inputs as jcm800/examples/bench.rs so the two compare directly.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "aes/amp.h"
#include "aes/embedded_amps.h"
#include "aes/profile.h"
#include "aes/ts808_fast.h"

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
    for (const auto& id : aes::embeddedAmpIds()) {
        const aes::AmpDef def = aes::loadEmbeddedAmp(id);
        std::printf("== %s\n", id.c_str());
        for (const auto& c : cases) {
            std::vector<double> knobs;
            for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
            knobs[0] = c.volume;  // the first knob is the (first) volume in every amp
            for (std::size_t i = 0; i < knobs.size(); ++i)
                if (def.preamp.knobs[i].name == "master") knobs[i] = c.master;
            const auto in = signal(c.amp, n, fs);
#ifdef AES_PROFILE
            // Lite runs only the preamp solver, so the counters isolate it.
            {
                aes::LiteAmp l(def, fs);
                l.setKnobs(knobs, def.power.presenceDefault);
                for (auto& v : aes::g_profile) v = 0;
                for (double x : in) sink += l.process(x);
                const double total = double(aes::g_profile[aes::ProfStep]);
                std::printf("  %-7s preamp cycles/sample %.0f: eval %2.0f%%  factor %2.0f%%  lu-solve %2.0f%%  S*r %2.0f%%  v=S*r-K*i %2.0f%%\n", c.name,
                            total / double(n), 100 * double(aes::g_profile[aes::ProfEval]) / total, 100 * double(aes::g_profile[aes::ProfFactor]) / total,
                            100 * double(aes::g_profile[aes::ProfSolve]) / total, 100 * double(aes::g_profile[aes::ProfMatvec]) / total,
                            100 * double(aes::g_profile[aes::ProfReconstruct]) / total);
            }
#endif
            aes::FullAmp full(def, fs);
            full.setKnobs(knobs, def.power.presenceDefault);
            const double pf = percent([&] { for (double x : in) sink += full.process(x); }, seconds);
            aes::LiteAmp lite(def, fs);
            lite.setKnobs(knobs, def.power.presenceDefault);
            const double pl = percent([&] { for (double x : in) sink += lite.process(x); }, seconds);
            const auto& ps = full.pre().solver();
            const auto& ws = full.power().solver();
            std::printf("  %-7s full %6.1f %%   lite %6.1f %%   | preamp %zu nodes %zu ports %.1f it/sample %llu cont | power %zu nodes %zu ports %.1f it/sample\n",
                        c.name, pf, pl, ps.nodeCount(), ps.portCount(), double(ps.newtonIterations) / double(n),
                        (unsigned long long)ps.continuationRuns, ws.nodeCount(), ws.portCount(), double(ws.newtonIterations) / double(n));
        }
    }
    {
        const auto in = signal(0.1, n, fs);
        aes::TubeScreamerFast ts(fs);
        const double p = percent([&] { for (double x : in) sink += ts.process(x); }, seconds);
        std::printf("ts808   fast %5.2f %%\n", p);
    }
    std::printf("(checksum %g)\n", sink);
}
