// Measures how loud every amp and every jack/mode plays through the whole chain (default knobs, cab
// filter on), so their level_db / output_db can be set to equalise them.
//   aes_match_levels [reference peak volts]
// Reference = the first amp's first jack, without trim, at the reference input (default 0.1 V peak).
// "Level" is the rms of the chain's output for a guitar-like signal (three sines + noise), in dB re
// the reference. The last column says how many dB are still missing at the reference input.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "aes/chain.h"
#include "aes/embedded_amps.h"

namespace {

constexpr double kFs = 48000.0;

std::vector<float> guitar(double peak, std::size_t n) {
    std::uint64_t state = 0x123456789abcdef0ull;
    std::vector<float> v(n);
    const double pi = 3.14159265358979323846;
    for (std::size_t i = 0; i < n; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double noise = double(state >> 11) / double(1ull << 53) - 0.5;
        const double t = double(i) / kFs;
        const double s = 0.6 * std::sin(2 * pi * 110.0 * t) + 0.3 * std::sin(2 * pi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * pi * 1234.5 * t);
        v[i] = float(peak * (s + 0.02 * noise));
    }
    return v;
}

// rms (digital full scale = 1) of the chain's output, second half of 1.5 s.
double level(const aes::AmpDef& def, std::size_t jack, double peak) {
    aes::Chain chain(def, kFs);
    aes::ChainSettings s;
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
    s.presence = def.power.presenceDefault;
    s.jack = jack;
    s.cabFilter = true;
    chain.snap(s);
    const std::size_t n = std::size_t(1.5 * kFs);
    const auto in = guitar(peak, n);
    std::vector<float> out(n);
    chain.processAmp(in.data(), out.data(), n);
    double acc = 0;
    for (std::size_t i = n / 2; i < n; ++i) acc += double(out[i]) * out[i];
    return std::sqrt(acc / double(n - n / 2));
}

double db(double x) { return 20.0 * std::log10(x); }

}  // namespace

int main(int argc, char** argv) {
    const double refPeak = argc > 1 ? std::atof(argv[1]) : 0.1;
    const std::vector<double> peaks = {0.01, 0.03, 0.1, 0.3};
    const auto ids = aes::embeddedAmpIds();
    // The reference is always measured without the amp's own output trim.
    aes::AmpDef first = aes::loadEmbeddedAmp(ids.front());
    first.outputDb = 0.0;
    const double ref = level(first, 0, refPeak);
    std::printf("reference: %s / %s at %.3f V peak = %.1f dBFS rms\n\n", first.name.c_str(), first.preamp.jacks[0].label.c_str(), refPeak, db(ref));
    std::printf("%-26s", "level re reference (dB)");
    for (double p : peaks) std::printf("  %6.2f V", p);
    std::printf("   still missing\n");
    for (const auto& id : ids) {
        const aes::AmpDef def = aes::loadEmbeddedAmp(id);
        for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) {
            std::printf("%-12s %-12s", def.name.substr(0, 12).c_str(), def.preamp.jacks[j].label.c_str());
            for (double p : peaks) std::printf("  %+7.1f", db(level(def, j, p) / ref));
            std::printf("   %+6.1f dB\n", -db(level(def, j, refPeak) / ref));
        }
    }
    return 0;
}
