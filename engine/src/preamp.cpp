#include "aes/preamp.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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

Preamp::Preamp(const AmpDef& def, double fs, double railVolts, bool useTables)
    : potMinOhms_(def.preamp.potMinOhms), sourceOhmsMin_(def.preamp.sourceOhmsMin), sourceOhms_(def.preamp.sourceOhmsDefault) {
    // The Eco modes use the amp's finer-split preamp when it defines one.
    const PreampDef& pd = (useTables && def.hasEcoPreamp) ? def.ecoPreamp : def.preamp;

    // Parse every stage's netlist.
    std::vector<ParsedNetlist> nets;
    nets.push_back(parseNetlist(pd.netlistText, pd.netlist));
    for (const auto& s : pd.stages) nets.push_back(parseNetlist(s.netlistText, s.netlist));

    // A resistor name finds every copy of it, in whichever stages it appears.
    auto find = [&](const std::string& name) {
        Refs refs;
        for (std::size_t i = 0; i < nets.size(); ++i)
            if (nets[i].resistors.count(name)) refs.push_back({i, nets[i].resistor(name)});
        if (refs.empty()) throw std::runtime_error("amp '" + def.id + "': no resistor named '" + name + "' in any preamp netlist");
        return refs;
    };

    for (const auto& kd : pd.knobs) {
        Knob k{kd.pot.type, {}, {}, kd.pot.total, kd.pot.taper};
        if (kd.pot.type == PotDef::Type::Wiper) {
            k.top = find(kd.pot.top);
            k.bottom = find(kd.pot.bottom);
        } else {
            k.top = find(kd.pot.resistor);
        }
        knobs_.push_back(k);
        values_.push_back(kd.def);
    }
    // Pots start at their default positions so the DC point is solved with them.
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        double top, bottom;
        potValues(knobs_[i], values_[i], potMinOhms_, top, bottom);
        for (const Ref& r : knobs_[i].top) nets[r.stage].circuit.setResistor(r.id, top);
        if (knobs_[i].type == PotDef::Type::Wiper)
            for (const Ref& r : knobs_[i].bottom) nets[r.stage].circuit.setResistor(r.id, bottom);
    }

    const SrcId input = nets[0].source(pd.inputSource);
    for (const auto& jd : pd.jacks) {
        Jack j;
        j.drive = nets[0].source(jd.drive);
        j.drivesInput = j.drive.i == input.i;
        for (const auto& [name, ohms] : jd.resistors) j.resistors.push_back({find(name), ohms});
        jacks_.push_back(std::move(j));
    }
    for (const auto& name : pd.sourceResistors) sourceResistors_.push_back(find(name));

    // Build the solvers in signal order: each later stage's DC point starts from what the
    // previous one settled to at the link node.
    stages_.reserve(nets.size());
    for (std::size_t i = 0; i < nets.size(); ++i) {
        ParsedNetlist& net = nets[i];
        const SrcId rail = net.source(pd.railSource);
        net.circuit.setSourceDc(rail, railVolts);
        SrcId link{0};
        Node linkFrom = 0;
        if (i > 0) {
            const PreampStageDef& sd = pd.stages[i - 1];
            link = net.source(sd.linkSource);
            linkFrom = nets[i - 1].node(sd.linkFrom);
            net.circuit.setSourceDc(link, stages_[i - 1].solver.voltage(linkFrom));
        }
        stages_.push_back(Stage{Solver(std::move(net.circuit), fs, useTables), rail, link, linkFrom});
        // Guitar signals are millivolts at the first grid, so Newton must converge tightly here.
        stages_.back().solver.tolerance = pd.tolerance;
        stages_.back().solver.refresh();
    }
    out_ = nets.back().node(pd.outputNode);
}

void Preamp::setKnobs(const std::vector<double>& values) {
    if (values.size() != knobs_.size()) throw std::invalid_argument("wrong number of knob values");
    if (values == values_) return;
    values_ = values;
    for (std::size_t i = 0; i < knobs_.size(); ++i) {
        double top, bottom;
        potValues(knobs_[i], values_[i], potMinOhms_, top, bottom);
        set(knobs_[i].top, top);
        if (knobs_[i].type == PotDef::Type::Wiper) set(knobs_[i].bottom, bottom);
    }
    refreshAll();
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
    for (const Refs& r : sourceResistors_) set(r, ohms);
    refreshAll();
}

void Preamp::setJack(std::size_t jack) {
    if (jack == jack_) return;
    jack_ = jack;
    for (const auto& [ref, ohms] : jacks_.at(jack).resistors) set(ref, ohms);
    refreshAll();
}

}  // namespace aes
