// sas_acoustic_check: gain in dB of the acoustic simulator by frequency for each body type (small signal), and the overall level of a guitar-like noise.
#include <cmath>
#include <cstdio>
#include <vector>

#include "sas/acoustic.h"

int main() {
    constexpr double fs = 48000.0, pi = 3.14159265358979323846;
    const double freqs[] = {60, 100, 200, 300, 500, 1000, 2000, 3500, 6000, 10000};
    const char* names[] = {"dreadnought", "jumbo", "parlor", "piezo"};
    for (int t = 0; t < 4; ++t) {
        std::printf("%-12s", names[t]);
        for (double hz : freqs) {
            sas::Acoustic a;
            a.prepare(fs);
            sas::AcousticParams p;
            p.on = true;
            p.type = t;
            p.squash = 0.0;
            a.setParams(p);
            double in = 0, out = 0;
            std::vector<float> x(48000);
            for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(0.01 * std::sin(2 * pi * hz * double(i) / fs));
            const auto y = x;
            a.process(x.data(), nullptr, x.size());
            for (std::size_t i = 24000; i < x.size(); ++i) { in += double(y[i]) * y[i]; out += double(x[i]) * x[i]; }
            std::printf(" %6.1f", 10 * std::log10(out / in));
        }
        std::printf("   dB at 60 100 200 300 500 1k 2k 3.5k 6k 10k Hz\n");
    }
    return 0;
}
