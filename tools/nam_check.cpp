// Runs real .nam models through NamModel the way the plugin does and reports what matters: whether they load, the rate they were
// trained at, CPU (% of a core at the host rate), and a sanity check of the output (finite, level, and the noise left on silence).
//   sas_nam_check [--rate 48000] [--block 256] <model.nam>...
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sas/nam_model.h"

constexpr double kPi = 3.14159265358979323846;

int main(int argc, char** argv) {
    double fs = 48000.0;
    int block = 256;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) fs = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--block") && i + 1 < argc) block = std::atoi(argv[++i]);
        else files.push_back(argv[i]);
    }
    if (files.empty()) { std::printf("usage: sas_nam_check [--rate hz] [--block n] <model.nam>...\n"); return 1; }

    const double seconds = 4.0;
    const std::size_t count = std::size_t(fs * seconds);
    std::vector<float> sig(count), zero(count, 0.0f), out(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double t = double(i) / fs;
        // A guitar-ish level (about -12 dBFS peaks) with a few partials and a decaying pluck every second.
        const double env = std::exp(-3.0 * std::fmod(t, 1.0));
        sig[i] = float(0.25 * env * (0.6 * std::sin(2 * kPi * 110.0 * t) + 0.3 * std::sin(2 * kPi * 330.7 * t) + 0.1 * std::sin(2 * kPi * 1234.5 * t)));
    }
    std::printf("%-58s %7s %6s %8s %9s %9s %s\n", "model", "trained", "match", "cpu %", "out peak", "silence", "notes");
    for (const auto& path : files) {
        std::string err;
        auto m = sas::NamModel::load(path, fs, block, err);
        const std::string shortName = path.substr(path.find_last_of("/\\") + 1);
        if (!m) { std::printf("%-58.58s LOAD FAILED: %s\n", shortName.c_str(), err.c_str()); continue; }
        auto run = [&](const std::vector<float>& in, double& wall) {
            m->reset();
            const auto t0 = std::chrono::steady_clock::now();
            for (std::size_t p = 0; p < count; p += std::size_t(block)) m->process(in.data() + p, out.data() + p, std::min<std::size_t>(block, count - p));
            wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        };
        double wall = 0, w2 = 0;
        run(sig, wall);
        double peak = 0;
        bool finite = true;
        for (std::size_t i = 0; i < count; ++i) { if (!std::isfinite(out[i])) finite = false; peak = std::max(peak, double(std::abs(out[i]))); }
        double wall1 = wall;
        run(sig, wall);  // second pass: first one pays cache warm-up
        wall = std::min(wall, wall1);
        run(zero, w2);
        double sil = 0;
        for (std::size_t i = count / 2; i < count; ++i) sil = std::max(sil, double(std::abs(out[i])));
        std::string notes;
        if (!finite) notes += "NON-FINITE ";
        if (!m->rateMatches()) notes += "rate-mismatch ";
        if (peak > 4.0) notes += "very-hot ";
        std::printf("%-58.58s %7.0f %6s %8.2f %9.3f %9.1e %s\n", shortName.c_str(), m->trainedRate(), m->rateMatches() ? "yes" : "NO", 100.0 * wall / seconds, peak, sil, notes.c_str());
    }
    return 0;
}
