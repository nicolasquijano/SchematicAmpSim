// sas_beast_check: the Beast input stage of the British 800 KK against the tube preamp alone.
//   gain of preamp output with Beast on vs off at small signal, per Assault setting, and the gate on a decaying note.
#include <cmath>
#include <cstdio>
#include <vector>

#include "sas/embedded_amps.h"
#include "sas/preamp.h"

namespace {
constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;

std::size_t knob(const sas::AmpDef& d, const char* n) { return d.knobIndex(n); }

double rmsAt(sas::Preamp& p, double hz, double amp, double seconds = 0.5) {
    double s = 0;
    std::size_t n = std::size_t(seconds * kFs), warm = n / 2, c = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double y = p.process(amp * std::sin(2 * kPi * hz * double(i) / kFs));
        if (i >= warm) { s += y * y; ++c; }
    }
    return std::sqrt(s / double(c));
}
}  // namespace

int main() {
    const sas::AmpDef def = sas::loadEmbeddedAmp("jcm800_2203kk");
    const double freqs[] = {80, 200, 500, 1000, 2000, 4000, 8000};
    for (double assault : {0.0, 0.5, 1.0}) {
        std::printf("assault %.1f: gain of Beast on vs off (dB) at", assault);
        for (double hz : freqs) std::printf(" %g", hz);
        std::printf("\n   ");
        for (double hz : freqs) {
            double r[2];
            for (int on = 0; on < 2; ++on) {
                sas::Preamp p(def, kFs, 410.0);
                p.setKnob(knob(def, "volume"), 0.2);
                p.setKnob(knob(def, "assault"), assault);
                p.setKnob(knob(def, "gate"), 0.0);
                p.setSelector(0, std::size_t(on));
                r[on] = rmsAt(p, hz, 0.002);
            }
            std::printf(" %6.1f", 20 * std::log10(r[1] / r[0]));
        }
        std::printf("\n");
    }
    // The gate: a 220 Hz note at 50 mV that stops, then a hum-level signal; output level over time.
    for (double gate : {0.0, 0.4, 0.8}) {
        sas::Preamp p(def, kFs, 410.0);
        p.setKnob(knob(def, "volume"), 0.5);
        p.setKnob(knob(def, "assault"), 0.5);
        p.setKnob(knob(def, "gate"), gate);
        p.setSelector(0, 1);
        std::printf("gate %.1f: out rms (mV) in 50 ms blocks:", gate);
        const std::size_t blk = std::size_t(0.05 * kFs);
        for (int b = 0; b < 16; ++b) {
            double s = 0;
            for (std::size_t i = 0; i < blk; ++i) {
                const double t = double(b * blk + i) / kFs;
                const double a = t < 0.4 ? 0.05 * std::exp(-t * 3.0) : 0.0002;
                const double y = p.process(a * std::sin(2 * kPi * 220 * t));
                s += y * y;
            }
            std::printf(" %.1f", 1000 * std::sqrt(s / double(blk)));
        }
        std::printf("\n");
    }
    return 0;
}
