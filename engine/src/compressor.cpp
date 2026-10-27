#include "sas/compressor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sas {

namespace {

constexpr double kMinOhms = 50.0;

// Exponential audio taper, ~15% of the resistance at mid rotation (the same law as the amps' log pots).
double logTaper(double w) { return (std::exp(3.47 * w) - 1.0) / (std::exp(3.47) - 1.0); }

}  // namespace

struct Compressor::Built {
    ParsedNetlist net;
};

Compressor::Built Compressor::build() { return Built{parseNetlist(compressorNetlistText(), "compressor.net")}; }

Compressor::Compressor(Built&& b, double fs)
    : solver_(std::move(b.net.circuit), fs), out_(b.net.node("out")), env_(b.net.node("env")), q5e_(b.net.node("q5e")), pin1_(b.net.node("pin1")),
      rSus_(b.net.resistor("RSUS")), rTone_(b.net.resistor("RTONE")), rBtTop_(b.net.resistor("RBT_TOP")), rBtBot_(b.net.resistor("RBT_BOT")),
      rLvTop_(b.net.resistor("RLV_TOP")), rLvBot_(b.net.resistor("RLV_BOT")), rAttack_(b.net.resistor("SWATK")), rSrc_(b.net.resistor("RSRC")) {
    if (!solver_.dcConverged) throw std::runtime_error("compressor DC operating point did not converge");
    // Signals are millivolts at the OTA input, so the Newton tolerance has to be well below them.
    solver_.tolerance = 2e-6;
    solver_.chord = true;  // reuse the previous sample's factorisation for the first Newton iteration
    controls_.sustain = -1.0;  // force the first setControls to apply
    setControls(CompControls{});
}

Compressor::Compressor(double fs) : Compressor(build(), fs) {}

void Compressor::setSourceOhms(double ohms) {
    solver_.setResistor(rSrc_, std::max(ohms, 1.0));
    solver_.refresh();
}

void Compressor::setControls(const CompControls& c) {
    if (c == controls_) return;
    controls_ = c;
    auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
    // Sustain (500k reverse log, a rheostat from the Q5 end to the wiper tied to the R12 end): full clockwise is 0 ohms.
    sustainOhms_ = std::max(500e3 * logTaper(1.0 - clamp01(c.sustain)), kMinOhms);
    solver_.setResistor(rSus_, sustainOhms_);
    // Tone (100k linear rheostat): clockwise is 0 ohms, the most treble.
    solver_.setResistor(rTone_, std::max(100e3 * (1.0 - clamp01(c.tone)), kMinOhms));
    // Blend (10k linear, wiper to VC): clockwise shorts the dry side to VC, so the compressed path only.
    solver_.setResistor(rBtTop_, std::max(10e3 * (1.0 - clamp01(c.blend)), kMinOhms));
    solver_.setResistor(rBtBot_, std::max(10e3 * clamp01(c.blend), kMinOhms));
    // Level (100k audio taper).
    const double lv = logTaper(clamp01(c.level));
    solver_.setResistor(rLvBot_, std::max(100e3 * lv, kMinOhms));
    solver_.setResistor(rLvTop_, std::max(100e3 * (1.0 - lv), kMinOhms));
    // The Attack switch: closed shorts R58.
    solver_.setResistor(rAttack_, c.fastRelease ? 1.0 : 1e12);
    solver_.refresh();
}

}  // namespace sas
