#include "sas/preamp.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "sas/netlist.h"

namespace sas {

namespace {

// Exponential taper with the given fraction of the resistance at mid rotation; "log" is ~15%.
double expTaper(double w, double k) { return (std::exp(k * w) - 1.0) / (std::exp(k) - 1.0); }
double logTaper(double w) { return expTaper(w, 3.47); }

double applyTaper(Taper t, double w) {
    switch (t) {
        case Taper::Log: return logTaper(w);
        case Taper::Log10: return expTaper(w, 4.394);  // 2 ln(1/0.10 - 1)
        case Taper::Log30: return expTaper(w, 1.695);  // 2 ln(1/0.30 - 1)
        case Taper::RevLog: return 1.0 - logTaper(1.0 - w);
        default: return w;
    }
}

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

Preamp::Preamp(const AmpDef& def, double fs, double railVolts, bool useTables, bool splitStages)
    : potMinOhms_(def.preamp.potMinOhms), sourceOhmsMin_(def.preamp.sourceOhmsMin), sourceOhms_(def.preamp.sourceOhmsDefault) {
    // The Eco modes use the amp's finer-split preamp when it defines one.
    const PreampDef& pd = (useTables && splitStages && def.hasEcoPreamp) ? def.ecoPreamp : def.preamp;

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
        for (const auto& [a, b] : kd.pot.also) {
            for (const Ref& r : find(a)) k.top.push_back(r);
            if (kd.pot.type == PotDef::Type::Wiper)
                for (const Ref& r : find(b)) k.bottom.push_back(r);
        }
        knobs_.push_back(k);
        values_.push_back(kd.def);
    }
    if (pd.reverb.present) {
        rev_ = std::make_unique<Reverb>(pd.reverb, fs, railVolts, useTables);
        revSrc_ = nets[0].source(pd.reverb.mainSource);
        revGrid_ = nets[0].node(pd.reverb.gridNode);
    }
    if (pd.vibrato.present) {
        vib_ = std::make_unique<Vibrato>(pd.vibrato, fs, railVolts, useTables);
        cellRefs_ = find(pd.vibrato.cellResistor);
        vibEvery_ = vib_->controlSamples();
        lastCellOhms_ = vib_->cellOhms();
        for (const Ref& r : cellRefs_) nets[r.stage].circuit.setResistor(r.id, vib_->cellOhms());
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
    for (const auto& sd : pd.selectors) {
        selectors_.emplace_back();
        for (const auto& pos : sd.positions) {
            selectors_.back().emplace_back();
            for (const auto& [name, ohms] : pos.resistors) selectors_.back().back().push_back({find(name), ohms});
        }
        selectorPos_.push_back(0);  // the netlist's own values are position 0
    }
    for (const auto& name : pd.sourceResistors) {
        if (name == "RSRC") rsrcIndex_ = sourceResistors_.size();
        sourceResistors_.push_back(find(name));
    }
    // The guitar's pickup and cable networks (optional): switches that are open until setGuitar() closes them.
    {
        auto findAll = [&](const std::string& name) {
            Refs refs;
            for (std::size_t i = 0; i < nets.size(); ++i)
                if (nets[i].resistors.count(name)) refs.push_back({i, nets[i].resistor(name)});
            return refs;
        };
        pickupCoil_[0] = findAll("RPU1");
        pickupCoil_[1] = findAll("RPU2");
        pickupLoss_ = findAll("RPL");
        hasPickups_ = !pickupCoil_[0].empty() && !pickupCoil_[1].empty() && !pickupLoss_.empty();
        for (int b = 0; b < 3; ++b) {
            cableSwitch_[b] = findAll("SWK" + std::to_string(1 << b));
            if (cableSwitch_[b].empty()) hasPickups_ = false;
        }
        if (rsrcIndex_ == static_cast<std::size_t>(-1)) hasPickups_ = false;
    }

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

double potTaper(Taper t, double w) { return applyTaper(t, w); }

void Preamp::advanceVibrato() {
    vibCount_ = 0;
    // Re-inverting the circuit is the costly part: only when the cell has really moved.
    const double ohms = vib_->step();
    if (std::abs(ohms - lastCellOhms_) < 0.004 * lastCellOhms_) return;
    lastCellOhms_ = ohms;
    set(cellRefs_, ohms);
    stages_[cellRefs_.front().stage].solver.refresh();
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
    for (std::size_t i = 0; i < sourceResistors_.size(); ++i)
        if (!(guitarActive_ && i == rsrcIndex_)) set(sourceResistors_[i], ohms);
    refreshAll();
}

void Preamp::setGuitar(int pickup, int cableMeters, bool pedalOn) {
    if (!hasPickups_) return;
    pickup = std::clamp(pickup, 0, 3);
    cableMeters = std::clamp(cableMeters, 0, 14) / 2 * 2;
    const bool active = pickup > 0 && !pedalOn;
    if (active == guitarActive_ && pickup == pickup_ && (!active || cableMeters == cableMeters_)) return;
    guitarActive_ = active;
    pickup_ = pickup;
    cableMeters_ = cableMeters;
    constexpr double kOpen = 1e12, kClosed = 1.0;
    // Pickups (coil resistance, loss resistor): single coil on coil 1; humbucker and P-90 on coil 2.
    static constexpr double kCoilOhms[4][2] = {{kOpen, kOpen}, {6.5e3, kOpen}, {kOpen, 8.0e3}, {kOpen, 8.5e3}};
    static constexpr double kLossOhms[4] = {kOpen, 220e3, 180e3, 200e3};
    set(pickupCoil_[0], active ? kCoilOhms[pickup][0] : kOpen);
    set(pickupCoil_[1], active ? kCoilOhms[pickup][1] : kOpen);
    set(pickupLoss_, active ? kLossOhms[pickup] : kOpen);
    for (int b = 0; b < 3; ++b) set(cableSwitch_[b], active && (((cableMeters / 2) >> b) & 1) ? kClosed : kOpen);
    set(sourceResistors_[rsrcIndex_], active ? kOpen : sourceOhms_);
    refreshAll();
}

void Preamp::setSelector(std::size_t i, std::size_t pos) {
    if (i >= selectors_.size()) return;
    pos = std::min(pos, selectors_[i].size() - 1);
    if (pos == selectorPos_[i]) return;
    selectorPos_[i] = pos;
    for (const auto& [ref, ohms] : selectors_[i][pos]) set(ref, ohms);
    refreshAll();
}

void Preamp::setJack(std::size_t jack) {
    if (jack == jack_) return;
    jack_ = jack;
    for (const auto& [ref, ohms] : jacks_.at(jack).resistors) set(ref, ohms);
    refreshAll();
}

}  // namespace sas
