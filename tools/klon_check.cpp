// sas_klon_check: the Klon-style drive at a guitar level: output rms for a 1 kHz sine at 20 / 100 / 300 mV peak, by Gain setting, and the response at gain 0.5.
#include <cmath>
#include <cstdio>
#include <vector>

#include "sas/klon.h"

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;
double rmsDb(sas::Klon& k, double hz, double amp) {
    std::vector<float> x(std::size_t(0.6 * kFs));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(amp * std::sin(2 * kPi * hz * double(i) / kFs));
    k.process(x.data(), x.size());
    double s = 0;
    for (std::size_t i = x.size() / 2; i < x.size(); ++i) s += double(x[i]) * x[i];
    return 10 * std::log10(s / double(x.size() - x.size() / 2) + 1e-30);
}
}  // namespace

int main() {
    for (double g : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        std::printf("gain %.2f:", g);
        for (double a : {0.02, 0.1, 0.3}) {
            sas::Klon k(kFs);
            k.setControls({g, 0.5, 0.5});
            const double inDb = 10 * std::log10(a * a / 2);
            std::printf("  in %.2f V: %+5.1f dB", a, rmsDb(k, 1000, a) - inDb);
        }
        std::printf("\n");
    }
    std::printf("response at gain 0.5 (dB re 1 kHz), 40 mV peak:");
    sas::Klon ref(kFs);
    ref.setControls({0.5, 0.5, 0.5});
    const double r0 = rmsDb(ref, 1000, 0.04);
    for (double hz : {80.0, 200.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) {
        sas::Klon k(kFs);
        k.setControls({0.5, 0.5, 0.5});
        std::printf(" %g:%+.1f", hz, rmsDb(k, hz, 0.04) - r0);
    }
    std::printf("\n");
    return 0;
}
