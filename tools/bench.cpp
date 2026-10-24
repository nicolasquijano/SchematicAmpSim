// Engine benchmark: CPU cost of one second of 96 kHz audio (= 2x oversampling of a 48 kHz host).
// Prints percent of one core. Same inputs as jcm800/examples/bench.rs so the two compare directly.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "aes/amp2203.h"
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
    struct Case { const char* name; aes::Controls ctl; double presence, amp; };
    const Case cases[] = {
        {"clean", {0.2, 0.5, 0.5, 0.6, 0.5}, 0.5, 0.05},
        {"crunch", {0.6, 0.5, 0.5, 0.6, 0.5}, 0.5, 0.15},
        {"lead", {1.0, 0.6, 0.4, 0.7, 0.8}, 0.7, 0.3},
    };
    double sink = 0;
    for (const auto& c : cases) {
        const auto in = signal(c.amp, n, fs);
        aes::Amp2203 full(fs);
        full.setControls(c.ctl, c.presence);
        const double pf = percent([&] { for (double x : in) sink += full.process(x); }, seconds);
        aes::AmpLite2203 lite(fs);
        lite.setControls(c.ctl, c.presence);
        const double pl = percent([&] { for (double x : in) sink += lite.process(x); }, seconds);
        std::printf("%-7s full %5.1f %%   lite %5.1f %%\n", c.name, pf, pl);
    }
    {
        const auto in = signal(0.1, n, fs);
        aes::TubeScreamerFast ts(fs);
        const double p = percent([&] { for (double x : in) sink += ts.process(x); }, seconds);
        std::printf("ts808   fast %5.2f %%\n", p);
    }
    std::printf("(checksum %g)\n", sink);
}
