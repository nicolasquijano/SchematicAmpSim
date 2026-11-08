#include "sas/klon.h"

#include <algorithm>
#include <stdexcept>

#include "sas/preamp.h"

namespace sas {

namespace {
constexpr double kMinOhms = 50.0;
}

struct Klon::Built {
    ParsedNetlist net;
};

Klon::Built Klon::build() { return Built{parseNetlist(klonNetlistText(), "klon.net")}; }

Klon::Klon(Built&& b, double fs)
    : solver_(std::move(b.net.circuit), 2.0 * fs), out_(b.net.node("y")), gB_(b.net.resistor("VRG_B")), gT_(b.net.resistor("VRG_T")),
      g2T_(b.net.resistor("VRG2_T")), g2B_(b.net.resistor("VRG2_B")), tT_(b.net.resistor("VRT_T")), tB_(b.net.resistor("VRT_B")),
      lT_(b.net.resistor("VRL_T")), lB_(b.net.resistor("VRL_B")) {
    if (!solver_.dcConverged) throw std::runtime_error("Klon DC operating point did not converge");
    solver_.tolerance = 2e-6;
    solver_.chord = true;
    controls_.gain = -1.0;  // force the first setControls to apply
    setControls(KlonControls{});
}

Klon::Klon(double fs) : Klon(build(), fs) {}

void Klon::setControls(const KlonControls& c) {
    if (c == controls_) return;
    controls_ = c;
    auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
    // GAIN: 100k audio taper, two sections on one shaft; clockwise lowers the section in the gain leg and raises the shunt on the + input.
    const double g = potTaper(Taper::Log, clamp01(c.gain));
    solver_.setResistor(gB_, std::max(100e3 * g, kMinOhms));
    solver_.setResistor(gT_, std::max(100e3 * (1.0 - g), kMinOhms));
    solver_.setResistor(g2B_, std::max(100e3 * (1.0 - g), kMinOhms));
    solver_.setResistor(g2T_, std::max(100e3 * g, kMinOhms));
    // TREBLE: 10k linear; clockwise puts the wiper at R21's end, the most treble.
    const double t = clamp01(c.treble);
    solver_.setResistor(tT_, std::max(10e3 * (1.0 - t), kMinOhms));
    solver_.setResistor(tB_, std::max(10e3 * t, kMinOhms));
    // LEVEL: 10k audio taper.
    const double lv = potTaper(Taper::Log, clamp01(c.level));
    solver_.setResistor(lB_, std::max(10e3 * lv, kMinOhms));
    solver_.setResistor(lT_, std::max(10e3 * (1.0 - lv), kMinOhms));
    solver_.refresh();
}

void Klon::process(float* x, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        x[i] = os_.process(x[i], [&](float s) {
            solver_.process(double(s));
            return float(solver_.voltage(out_));
        });
}

void Klon::reset() { os_.reset(); }

}  // namespace sas
