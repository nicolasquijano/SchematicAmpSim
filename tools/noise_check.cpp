// Hiss and hum at the output of the whole Chain, against the output with a guitar-like signal, for each amp (and each setting of its selectors):
// the amp's own noise model ("Noise and hum" Real, humbucker and single coil) and a plain input noise of the given level (an interface's floor).
//   sas_noise_check [amp id ...]        SAS_SEL="channel=2,..." sets selectors; SAS_GAIN_KNOB=0.3 puts the first knob there; SAS_FLOOR_UV=<microvolts rms of input noise> (default 3)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sas/chain.h"
#include "sas/embedded_amps.h"

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

std::vector<std::size_t> selFromEnv(const sas::AmpDef& def) {
    std::vector<std::size_t> v(def.preamp.selectors.size(), 0);
    const char* e = std::getenv("SAS_SEL");
    std::string text = e ? e : "";
    for (std::size_t start = 0; start < text.size();) {
        std::size_t comma = text.find(',', start);
        if (comma == std::string::npos) comma = text.size();
        const std::string item = text.substr(start, comma - start);
        const std::size_t eq = item.find('=');
        if (eq != std::string::npos)
            for (std::size_t i = 0; i < def.preamp.selectors.size(); ++i)
                if (def.preamp.selectors[i].name == item.substr(0, eq)) v[i] = std::size_t(std::atoi(item.c_str() + eq + 1));
        start = comma + 1;
    }
    return v;
}

// rms of the last 1.5 s, in dB re full scale.
double run(const sas::AmpDef& def, double noise, int pickup, double floorVolts, double signalPeak) {
    sas::Chain chain(def, kFs);
    sas::ChainSettings s;
    s.selectors = selFromEnv(def);
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    if (const char* g = std::getenv("SAS_GAIN_KNOB")) s.knobs[0] = std::atof(g);  // the first knob (the gain) at this position, 0..1
    s.quality = std::getenv("SAS_QUALITY") ? static_cast<sas::Quality>(std::atoi(std::getenv("SAS_QUALITY"))) : sas::Quality::Eco;  // SAS_QUALITY: 0 Normal 1 High 2 Lite 3 Eco 4 Efficient
    s.noise = noise;
    s.pickup = pickup;
    chain.setSettings(s);
    chain.snap(s);
    const std::size_t n = std::size_t(kFs * 2.5), block = 512;
    std::vector<float> in(block), out(block);
    std::uint64_t st = 99;
    double e = 0, sum = 0;
    std::size_t cnt = 0, at = 0, crossings = 0;
    float prev = 0.0f;
    for (std::size_t b = 0; b + block <= n; b += block) {
        for (std::size_t i = 0; i < block; ++i, ++at) {
            st = st * 6364136223846793005ull + 1442695040888963407ull;
            const double g = (double(st >> 11) / double(1ull << 53) - 0.5) * 3.4641016;  // unit variance
            const double t = double(at) / kFs;
            in[i] = float(signalPeak * (0.6 * std::sin(2 * kPi * 110.0 * t) + 0.3 * std::sin(2 * kPi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * kPi * 1234.5 * t)) + floorVolts * g);
        }
        chain.setSettings(s);
        chain.processAmp(in.data(), out.data(), block);
        chain.applyOutput(out.data(), block);
        if (b >= std::size_t(kFs)) for (std::size_t i = 0; i < block; ++i) { e += double(out[i]) * out[i]; sum += out[i]; ++cnt; if ((out[i] > 0) != (prev > 0)) ++crossings; prev = out[i]; }
    }
    const double mean = sum / double(std::max<std::size_t>(cnt, 1)), ms = e / double(std::max<std::size_t>(cnt, 1));
    if (std::getenv("SAS_DETAIL")) std::printf("   [dc %.4g, ac rms %.4g (full scale = 1), about %.0f Hz]\n", mean, std::sqrt(std::max(0.0, ms - mean * mean)), 0.5 * double(crossings) / (double(cnt) / kFs));
    return 10.0 * std::log10(ms + 1e-30);
}
}  // namespace

int main(int argc, char** argv) {
    const double floorUv = std::getenv("SAS_FLOOR_UV") ? std::atof(std::getenv("SAS_FLOOR_UV")) : 3.0;
    std::vector<std::string> ids;
    for (int i = 1; i < argc; ++i) ids.push_back(argv[i]);
    if (ids.empty()) ids = sas::embeddedAmpIds(true);
    std::printf("%-18s %9s %12s %12s %12s   (dBFS rms; input floor %.1f uV)\n", "amp", "signal", "real+humbkr", "real+single", "floor", floorUv);
    std::printf("(each amp is followed by a line with its output in silence: no input, no noise)\n");
    for (const auto& id : ids) {
        const auto def = sas::loadEmbeddedAmp(id);
        if (def.nam) continue;
        const double sig = run(def, 0.0, 0, 0.0, 0.1);
        std::printf("%-18s %9.1f %12.1f %12.1f %12.1f\n", id.c_str(), sig, run(def, 1.0, 2, 0.0, 0.0), run(def, 1.0, 1, 0.0, 0.0), run(def, 0.0, 0, floorUv * 1e-6, 0.0));
        std::printf("%-18s silence %.1f\n", "", run(def, 0.0, 0, 0.0, 0.0));
        std::fflush(stdout);
    }
    return 0;
}
