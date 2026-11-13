#include "sas/drive_circuit.h"

#include <algorithm>
#include <stdexcept>

#include "sas/preamp.h"

namespace sas {

namespace {
constexpr double kMinOhms = 50.0;
// The RAT and the DS-1 clip hard (a square wave whose harmonics fall only as 1/k), so it runs at four times the host rate: at twice, the folded harmonics were about 45 dB under the note, an inharmonic fizz.
double oversampleFactor(DriveModel m) { return m == DriveModel::Rat || m == DriveModel::Ds1 || m == DriveModel::Mt2 ? 4.0 : 2.0; }
}

struct DriveCircuit::Built {
    ParsedNetlist net;
    bool bd2 = false, rat = false, ds1 = false, mt2 = false;
};

DriveCircuit::Built DriveCircuit::build(DriveModel model) {
    if (model == DriveModel::BluesDriver) return Built{parseNetlist(bd2NetlistText(), "bd2.net"), true, false};
    if (model == DriveModel::Rat) return Built{parseNetlist(ratNetlistText(), "rat.net"), false, true};
    if (model == DriveModel::Mt2) return Built{parseNetlist(mt2NetlistText(), "mt2.net"), false, false, false, true};
    if (model == DriveModel::Ds1) return Built{parseNetlist(ds1NetlistText(), "ds1.net"), false, false, true};
    return Built{parseNetlist(sd1NetlistText(), "sd1.net"), false, false};
}

DriveCircuit::DriveCircuit(Built&& b, DriveModel model, double fs)
    : model_(model), solver_(std::move(b.net.circuit), oversampleFactor(model) * fs), os_(model == DriveModel::Rat || model == DriveModel::Ds1 || model == DriveModel::Mt2 ? 2 : 1), out_(b.net.node("y")),
      drive1_(b.net.resistor(b.bd2 ? "VRG1" : b.rat ? "VRG" : b.ds1 ? "VR1_T" : "VRD")), drive2_(b.bd2 ? b.net.resistor("VRG2") : b.ds1 ? b.net.resistor("VR1_B") : drive1_),
      toneA_(b.mt2 ? drive1_ : b.net.resistor(b.bd2 ? "VRT_T" : b.rat ? "VR_F" : b.ds1 ? "VR3_T" : "VRT_A")), toneB_(b.rat || b.mt2 ? toneA_ : b.net.resistor(b.ds1 ? "VR3_B" : "VRT_B")),
      levelT_(b.mt2 ? drive1_ : b.net.resistor(b.rat ? "VR_VT" : b.ds1 ? "VR2_T" : "VRL_T")), levelB_(b.mt2 ? drive1_ : b.net.resistor(b.rat ? "VR_VB" : b.ds1 ? "VR2_B" : "VRL_B")) {
    if (!solver_.dcConverged) throw std::runtime_error("overdrive DC operating point did not converge");
    solver_.tolerance = 2e-6;
    solver_.chord = true;
    controls_.drive = -1.0;  // force the first setControls to apply
    setControls(TsControls{});
}

DriveCircuit::DriveCircuit(DriveModel model, double fs) : DriveCircuit(build(model), model, fs) {}

void DriveCircuit::setControls(const TsControls& c) {
    if (c == controls_) return;
    controls_ = c;
    auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
    const double drive = clamp01(c.drive), tone = clamp01(c.tone), level = clamp01(c.level);
    if (model_ == DriveModel::Mt2) {
        // DIST: 250k audio taper as the variable resistor in the feedback of the distortion stage (the tone and the level are applied by the module).
        solver_.setResistor(drive1_, std::max(250e3 * potTaper(Taper::Log, drive), kMinOhms));
    } else if (model_ == DriveModel::Ds1) {
        // DIST: 100k linear, the wiper moves from the top (gain 1) to the bottom (gain 22) as the knob goes clockwise. TONE: 20k linear, the wiper goes from the low-pass end (counter-clockwise)
        // to the high-pass end. LEVEL: 100k linear, the wiper goes up with the knob.
        solver_.setResistor(drive1_, std::max(100e3 * drive, kMinOhms));
        solver_.setResistor(drive2_, std::max(100e3 * (1.0 - drive), kMinOhms));
        solver_.setResistor(toneA_, std::max(20e3 * tone, kMinOhms));
        solver_.setResistor(toneB_, std::max(20e3 * (1.0 - tone), kMinOhms));
        solver_.setResistor(levelT_, std::max(100e3 * (1.0 - level), kMinOhms));
        solver_.setResistor(levelB_, std::max(100e3 * level, kMinOhms));
    } else if (model_ == DriveModel::Rat) {
        // DISTORTION: 100k audio taper as the feedback resistor. FILTER: 100k audio taper in series with the 1.5k, clockwise is darker (so TONE, clockwise brighter, is the reverse). VOLUME: 100k audio taper.
        solver_.setResistor(drive1_, std::max(100e3 * potTaper(Taper::Log, drive), kMinOhms));
        solver_.setResistor(toneA_, std::max(100e3 * potTaper(Taper::Log, 1.0 - tone), kMinOhms));
        const double lv = potTaper(Taper::Log, level);
        solver_.setResistor(levelB_, std::max(100e3 * lv, kMinOhms));
        solver_.setResistor(levelT_, std::max(100e3 * (1.0 - lv), kMinOhms));
    } else if (model_ == DriveModel::BluesDriver) {
        // GAIN: two 250k audio-taper gangs, each the upper part of the feedback of its stage. TONE: 10k linear, clockwise is the bright end. LEVEL: 100k audio taper.
        const double g = potTaper(Taper::Log, drive);
        solver_.setResistor(drive1_, std::max(250e3 * g, kMinOhms));
        solver_.setResistor(drive2_, std::max(250e3 * g, kMinOhms));
        solver_.setResistor(toneA_, std::max(10e3 * (1.0 - tone), kMinOhms));
        solver_.setResistor(toneB_, std::max(10e3 * tone, kMinOhms));
        const double lv = potTaper(Taper::Log, level);
        solver_.setResistor(levelB_, std::max(100e3 * lv, kMinOhms));
        solver_.setResistor(levelT_, std::max(100e3 * (1.0 - lv), kMinOhms));
    } else {
        // DRIVE: 1M linear, in series with R5 (33k) in the feedback. TONE: 20k, clockwise puts the wiper toward the - input (more treble gain). LEVEL: 10k linear.
        solver_.setResistor(drive1_, std::max(1e6 * drive, kMinOhms));
        solver_.setResistor(toneA_, std::max(20e3 * tone, kMinOhms));
        solver_.setResistor(toneB_, std::max(20e3 * (1.0 - tone), kMinOhms));
        solver_.setResistor(levelB_, std::max(10e3 * level, kMinOhms));
        solver_.setResistor(levelT_, std::max(10e3 * (1.0 - level), kMinOhms));
    }
    solver_.refresh();
}

void DriveCircuit::process(float* x, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        x[i] = os_.process(x[i], [&](float s) {
            solver_.process(double(s));
            return float(solver_.voltage(out_));
        });
}

void DriveCircuit::reset() { os_.reset(); }

}  // namespace sas
