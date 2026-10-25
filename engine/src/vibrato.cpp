#include "sas/vibrato.h"

#include <algorithm>
#include <cmath>

#include "sas/preamp.h"

namespace sas {

Vibrato::Vibrato(const VibratoDef& def, double fs, double railVolts, bool useTables)
    : def_(def), net_(parseNetlist(def.netlistText, def.netlist)),
      solver_([&] {
          net_.circuit.setSourceDc(net_.source(def.railSource), railVolts);
          return Solver(std::move(net_.circuit), fs / double(def.controlSamples), useTables);
      }()),
      rail_(net_.source(def.railSource)), switch_(net_.resistor(def.switchResistor)),
      speed_(net_.resistor(def.speedResistor)), ledFrom_(net_.node(def.ledFrom)), ledTo_(net_.node(def.ledTo)),
      dt_(double(def.controlSamples) / fs), g_(1.0 / def.cellDarkOhms) {
    aAttack_ = 1.0 - std::exp(-dt_ / def.cellAttackS);
    aRelease_ = 1.0 - std::exp(-dt_ / def.cellReleaseS);
    solver_.tolerance = 1e-4;
    setSpeed(def.speedDefault, 100.0);
    setOn(false);
    solver_.refresh();
}

void Vibrato::setOn(bool on) {
    if (on == on_) return;
    on_ = on;
    solver_.setResistor(switch_, on ? def_.switchOnOhms : def_.switchOffOhms);
    solver_.refresh();
}

void Vibrato::setSpeed(double value, double minOhms) {
    value = std::clamp(value, 0.0, 1.0);
    if (value == speedValue_) return;
    speedValue_ = value;
    const double x = potTaper(def_.speedTaper, def_.speedInvert ? 1.0 - value : value);
    solver_.setResistor(speed_, std::max(def_.speedTotal * x, minOhms));
    solver_.refresh();
}

double Vibrato::step() {
    solver_.step();
    const double amps = std::max((solver_.voltage(ledFrom_) - solver_.voltage(ledTo_)) / def_.ledOhms, 0.0);
    // Conductance of the cell: dark leakage plus a power law of the LED current, followed with a
    // fast attack and a slow release (the cell keeps glowing after the LED goes out).
    const double target = 1.0 / def_.cellDarkOhms + std::pow(amps * 1e3, def_.cellExponent) / def_.cellOhmsAt1mA;
    g_ += (target - g_) * (target > g_ ? aAttack_ : aRelease_);
    g_ = std::clamp(g_, 1.0 / def_.cellDarkOhms, 1.0 / 300.0);
    return 1.0 / g_;
}

}  // namespace sas
