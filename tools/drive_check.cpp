// sas_drive_check: the SD-1 and BD-2 style drives (sas/drive_circuit.h) at a guitar level: the gain of a 1 kHz sine at 20 / 100 / 300 mV peak by DRIVE setting, the response at
// drive 0.5, and the Tube Screamer (sas/ts808_fast.h) for comparison.   sas_drive_check [tone] [level]
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <vector>

#include "sas/drive_circuit.h"

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;
using Proc = std::function<void(float*, std::size_t)>;
double rmsDb(const Proc& p, double hz, double amp) {
    std::vector<float> x(std::size_t(0.8 * kFs));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(amp * std::sin(2 * kPi * hz * double(i) / kFs));
    p(x.data(), x.size());
    double s = 0;
    for (std::size_t i = x.size() / 2; i < x.size(); ++i) s += double(x[i]) * x[i];
    return 10 * std::log10(s / double(x.size() - x.size() / 2) + 1e-30);
}
struct Pedal {
    const char* name;
    std::function<Proc(double d, double t, double l)> make;
};
}  // namespace

int main(int argc, char** argv) {
    const double tone = argc > 1 ? std::atof(argv[1]) : 0.5, level = argc > 2 ? std::atof(argv[2]) : 0.5;
    const std::vector<Pedal> pedals = {
        {"TS808", [&](double d, double t, double l) {
             auto ts = std::make_shared<sas::TubeScreamerFast>(2 * kFs);
             ts->setControls({d, t, l});
             auto os = std::make_shared<sas::Oversampler>(1);
             return Proc([ts, os](float* x, std::size_t n) { for (std::size_t i = 0; i < n; ++i) x[i] = os->process(x[i], [&](float s) { return float(ts->process(double(s))); }); });
         }},
        {"SD-1", [&](double d, double t, double l) {
             auto c = std::make_shared<sas::DriveCircuit>(sas::DriveModel::SuperOverdrive, kFs);
             c->setControls({d, t, l});
             return Proc([c](float* x, std::size_t n) { c->process(x, n); });
         }},
        {"RAT", [&](double d, double t, double l) {
             auto c = std::make_shared<sas::DriveCircuit>(sas::DriveModel::Rat, kFs);
             c->setControls({d, t, l});
             return Proc([c](float* x, std::size_t n) { c->process(x, n); });
         }},
        {"BD-2", [&](double d, double t, double l) {
             auto c = std::make_shared<sas::DriveCircuit>(sas::DriveModel::BluesDriver, kFs);
             c->setControls({d, t, l});
             return Proc([c](float* x, std::size_t n) { c->process(x, n); });
         }}};
    for (const auto& ped : pedals) {
        std::printf("== %s (tone %.2f, level %.2f)\n", ped.name, tone, level);
        for (double g : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            std::printf("  drive %.2f:", g);
            for (double a : {0.02, 0.1, 0.3}) {
                const double inDb = 10 * std::log10(a * a / 2);
                std::printf("  in %.2f V: %+5.1f dB", a, rmsDb(ped.make(g, tone, level), 1000, a) - inDb);
            }
            std::printf("\n");
        }
        std::printf("  response at drive 0.5 (dB re 1 kHz), 40 mV peak:");
        const double r0 = rmsDb(ped.make(0.5, tone, level), 1000, 0.04);
        for (double hz : {80.0, 200.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) std::printf(" %g:%+.1f", hz, rmsDb(ped.make(0.5, tone, level), hz, 0.04) - r0);
        std::printf("\n");
        // CPU: 4 s of a guitar-like signal at 100 mV, as a percentage of a core at 48 kHz.
        std::vector<float> x(std::size_t(4 * kFs));
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(0.1 * std::sin(2 * kPi * 220.0 * double(i) / kFs));
        auto proc = ped.make(0.7, tone, level);
        const auto t0 = std::chrono::steady_clock::now();
        proc(x.data(), x.size());
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  cost: %.1f %% of a core\n", 100.0 * sec / 4.0);
    }
    return 0;
}
