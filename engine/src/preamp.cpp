#include "aes/preamp.h"

#include <algorithm>
#include <cmath>

#include "aes/netlist.h"

namespace aes {

namespace {

// Audio ("log") taper: ~15% of the resistance at mid rotation.
double logTaper(double w) {
    constexpr double k = 3.47;
    return (std::exp(k * w) - 1.0) / (std::exp(k) - 1.0);
}

double applyTaper(Taper t, double w) { return t == Taper::Log ? logTaper(w) : w; }

}  // namespace

struct Preamp::Built {
    ParsedNetlist net;
    SrcId railSrc{0};
    Node out = 0;
    std::vector<Knob> knobs;
    std::vector<double> values;
    std::vector<Jack> jacks;
    std::vector<ResId> sourceResistors;
};

// A wiper pot is two resistors sharing the wiper; "variable" is one resistor (wiper tied to an end).
void Preamp::potValues(const Knob& k, double value, double minOhms, double& top, double& bottom) {
    const double x = applyTaper(k.taper, value);
    if (k.type == PotDef::Type::Wiper) {
        const double f = std::clamp(x, 0.0, 1.0);
        top = std::max(k.total * (1.0 - f), minOhms);
        bottom = std::max(k.total * f, minOhms);
    } else {
        top = std::max(k.total * x, minOhms);
        bottom = 0.0;
    }
}

Preamp::Built Preamp::build(const AmpDef& def, double railVolts) {
    const PreampDef& pd = def.preamp;
    Built b;
    b.net = parseNetlist(pd.netlistText, pd.netlist);
    b.railSrc = b.net.source(pd.railSource);
    b.net.circuit.setSourceDc(b.railSrc, railVolts);
    b.out = b.net.node(pd.outputNode);

    for (const auto& kd : pd.knobs) {
        Knob k{kd.pot.type, ResId{0}, ResId{0}, kd.pot.total, kd.pot.taper};
        if (kd.pot.type == PotDef::Type::Wiper) {
            k.top = b.net.resistor(kd.pot.top);
            k.bottom = b.net.resistor(kd.pot.bottom);
        } else {
            k.top = b.net.resistor(kd.pot.resistor);
        }
        b.knobs.push_back(k);
        b.values.push_back(kd.def);
    }
    // Pots start at their default positions so the DC point is solved with them.
    for (std::size_t i = 0; i < b.knobs.size(); ++i) {
        double top, bottom;
        potValues(b.knobs[i], b.values[i], pd.potMinOhms, top, bottom);
        b.net.circuit.setResistor(b.knobs[i].top, top);
        if (b.knobs[i].type == PotDef::Type::Wiper) b.net.circuit.setResistor(b.knobs[i].bottom, bottom);
    }

    const SrcId input = b.net.source(pd.inputSource);
    for (const auto& jd : pd.jacks) {
        Jack j;
        j.drive = b.net.source(jd.drive);
        j.drivesInput = j.drive.i == input.i;
        for (const auto& [name, ohms] : jd.resistors) j.resistors.push_back({b.net.resistor(name), ohms});
        b.jacks.push_back(std::move(j));
    }
    for (const auto& name : pd.sourceResistors) b.sourceResistors.push_back(b.net.resistor(name));
    return b;
}

Preamp::Preamp(Built&& b, const AmpDef& def, double fs)
    : solver_(std::move(b.net.circuit), fs), railSrc_(b.railSrc), out_(b.out),
      potMinOhms_(def.preamp.potMinOhms), sourceOhmsMin_(def.preamp.sourceOhmsMin),
      sourceResistors_(std::move(b.sourceResistors)), sourceOhms_(def.preamp.sourceOhmsDefault),
      knobs_(std::move(b.knobs)), values_(std::move(b.values)), jacks_(std::move(b.jacks)) {
    // Guitar signals are millivolts at the first grid, so Newton must converge tightly here.
    solver_.tolerance = def.preamp.tolerance;
    solver_.refresh();
}

Preamp::Preamp(const AmpDef& def, double fs, double railVolts) : Preamp(build(def, railVolts), def, fs) {}

void Preamp::setKnobs(const std::vector<double>& values) {
    if (values.size() != knobs_.size()) throw std::invalid_argument("wrong number of knob values");
    if (values == values_) return;
    values_ = values;
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        double top, bottom;
        potValues(knobs_[i], values_[i], potMinOhms_, top, bottom);
        solver_.setResistor(knobs_[i].top, top);
        if (knobs_[i].type == PotDef::Type::Wiper) solver_.setResistor(knobs_[i].bottom, bottom);
    }
    solver_.refresh();
}

void Preamp::setKnob(std::size_t index, double value) {
    auto v = values_;
    v.at(index) = value;
    setKnobs(v);
}

void Preamp::setSourceOhms(double ohms) {
    ohms = std::max(ohms, sourceOhmsMin_);
    if (std::abs(ohms - sourceOhms_) < 1e-9) return;
    sourceOhms_ = ohms;
    for (ResId r : sourceResistors_) solver_.setResistor(r, ohms);
    solver_.refresh();
}

void Preamp::setJack(std::size_t jack) {
    if (jack == jack_) return;
    jack_ = jack;
    for (const auto& [id, ohms] : jacks_.at(jack).resistors) solver_.setResistor(id, ohms);
    solver_.refresh();
}

}  // namespace aes
