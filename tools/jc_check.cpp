// sas_jc_check: output of the preamp of jazz_clean_120 (volts rms at the node y) for a 1 kHz sine, with the distortion off and on, and the spring reverb at
// 0, 5 and 10 on a burst followed by silence.
#include <cmath>
#include <cstdio>

#include "sas/embedded_amps.h"
#include "sas/preamp.h"

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;
double rms(sas::Preamp& p, double hz, double amp, double sec) {
    double s = 0;
    const std::size_t n = std::size_t(sec * kFs);
    for (std::size_t i = 0; i < n; ++i) {
        const double y = p.process(amp * std::sin(2 * kPi * hz * double(i) / kFs));
        if (i >= n / 2) s += y * y;
    }
    return std::sqrt(s / double(n / 2));
}
}  // namespace

int main() {
    const sas::AmpDef def = sas::loadEmbeddedAmp("jazz_clean_120");
    for (int dist = 0; dist < 2; ++dist) {
        for (double a : {0.005, 0.02, 0.1}) {
            sas::Preamp p(def, kFs, 40.0);
            p.setKnob(def.knobIndex("volume"), 0.6);
            p.setKnob(def.knobIndex("distortion"), 0.6);
            p.setSelector(0, std::size_t(dist));
            std::printf("dist %d  in %.3f V peak: out %.4f V rms\n", dist, a, rms(p, 1000, a, 0.4));
        }
    }
    // The drawing's test condition: 21 mV at 1 kHz on the High jack, every tone control and the volume fully clockwise, distortion off: 290 mV at IC3b's output.
    {
        sas::Preamp p(def, kFs, 40.0);
        for (const char* k : {"volume", "bass", "middle", "treble", "hi_treble"}) p.setKnob(def.knobIndex(k), 1.0);
        p.setKnob(def.knobIndex("distortion"), 0.0);
        p.setKnob(def.knobIndex("reverb"), 0.0);
        const double out = rms(p, 1000, 0.021 * std::sqrt(2.0), 0.4);
        std::printf("FCW, 21 mV rms in: %.1f mV out (drawing: 290 mV)  -> gain %.1f dB (drawing 22.8 dB)\n", 1000 * out, 20 * std::log10(out / 0.021));
    }
    for (double rev : {0.0, 0.5, 1.0}) {
        sas::Preamp p(def, kFs, 40.0);
        p.setKnob(def.knobIndex("volume"), 0.6);
        p.setKnob(def.knobIndex("reverb"), rev);
        std::printf("reverb %.1f: rms per 100 ms, burst for 0.3 s then silence:", rev);
        for (int b = 0; b < 12; ++b) {
            double s = 0;
            for (int i = 0; i < int(0.1 * kFs); ++i) {
                const double t = double(b) * 0.1 + double(i) / kFs;
                const double y = p.process(t < 0.3 ? 0.02 * std::sin(2 * kPi * 440 * t) : 0.0);
                s += y * y;
            }
            std::printf(" %.4f", std::sqrt(s / (0.1 * kFs)));
        }
        std::printf("\n");
    }
    return 0;
}
