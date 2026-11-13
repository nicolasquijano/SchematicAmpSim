// sas_amp_response <amp id> [name=value ...] [jack=N] [sel=I:P ...] [amp=volts]: the frequency response of an amp (Normal mode, 96 kHz) with the knobs set (0..1) and a sine of `amp` volts
// peak at the input (default 5 mV, a quiet guitar): output rms in dBV per frequency and the gain re 1 kHz.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/amp_def.h"
#include "sas/embedded_amps.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: sas_amp_response <amp id> [knob=value ...] [jack=N] [sel=I:P] [amp=volts]\n"); return 1; }
    constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;
    const std::string which = argv[1];  // an amp id, or a folder with an amp.json (to try a changed netlist)
    sas::AmpDef def = which.find_first_of("/\\") != std::string::npos ? sas::loadAmpDefFromFolder(which) : sas::loadEmbeddedAmp(which);
    std::vector<double> knobs;
    for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
    double amp = 0.005;
    bool vib = false;
    double vibSpeed = 0.5;
    std::size_t jack = 0;
    std::vector<std::pair<std::size_t, std::size_t>> sel;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const auto eq = a.find('=');
        if (eq == std::string::npos) continue;
        const std::string n = a.substr(0, eq);
        const double v = std::atof(a.c_str() + eq + 1);
        if (n == "amp") amp = v;
        else if (n == "vib") vib = v > 0.5;
        else if (n == "vibspeed") vibSpeed = v;
        else if (n == "jack") jack = std::size_t(v);
        else if (n == "sel") { const auto c = a.find(':'); sel.push_back({std::size_t(std::atoi(a.c_str() + eq + 1)), std::size_t(std::atoi(a.c_str() + c + 1))}); }
        else for (std::size_t k = 0; k < def.preamp.knobs.size(); ++k) if (def.preamp.knobs[k].name == n) knobs[k] = v;
    }
    std::printf("%s  input %.1f mV peak  (outputs in dBV rms at the 16 ohm tap; gain re 1 kHz)\n", argv[1], amp * 1000.0);
    if (vib) {  // the vibrato: the swing of the level of a 440 Hz sine over 4 s (rms in 20 ms windows)
        sas::FullAmp a(def, kFs, false, true);
        a.setKnobs(knobs, def.power.presenceDefault);
        a.setJack(jack);
        for (auto [i, p] : sel) a.setSelector(i, p);
        a.setVibrato(true, vibSpeed);
        const std::size_t n = std::size_t(4.0 * kFs), win = std::size_t(0.02 * kFs);
        double lo = 1e9, hi = -1e9, e = 0;
        std::size_t c = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double y = a.process(amp * std::sin(2 * kPi * 440.0 * double(i) / kFs));
            e += y * y;
            if (++c == win) {
                if (i > n / 3) { const double db = 10 * std::log10(e / double(c) + 1e-30); lo = std::min(lo, db); hi = std::max(hi, db); }
                e = 0;
                c = 0;
            }
        }
        std::printf("vibrato on, speed %.2f: level swing %.1f dB (%.1f to %.1f dBV)\n", vibSpeed, hi - lo, lo, hi);
        return 0;
    }
    double ref = 0;
    for (double hz : {1000.0, 80.0, 120.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 5000.0, 8000.0}) {
        sas::FullAmp a(def, kFs, false, true);
        a.setKnobs(knobs, def.power.presenceDefault);
        a.setJack(jack);
        for (auto [i, p] : sel) a.setSelector(i, p);
        const std::size_t n = std::size_t(1.2 * kFs);
        double e = 0;
        std::size_t c = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double y = a.process(amp * std::sin(2 * kPi * hz * double(i) / kFs));
            if (i > n / 2) { e += y * y; ++c; }
        }
        const double db = 10 * std::log10(e / double(c) + 1e-30);
        if (hz == 1000.0) ref = db;
        std::printf("  %6.0f Hz: %+6.1f dBV   %+5.1f dB\n", hz, db, db - ref);
    }
    return 0;
}
