#pragma once
// Data-driven preamp: one or more netlists plus the knob/jack mapping from amp.json. The knobs are
// pots whose sections are resistors of the netlists; jacks switch resistors and choose which source
// carries the guitar. With several netlists ("stages") each is solved on its own and the next one
// is fed from a node of the previous one, which is much cheaper than one big circuit.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "sas/amp_def.h"
#include "sas/circuit.h"
#include "sas/reverb.h"
#include "sas/vibrato.h"

namespace sas {

// Resistance fraction of a pot at rotation `w` (0..1) for the given taper.
double potTaper(Taper t, double w);

class Preamp {
public:
    // railVolts overrides the rail supply's value in the netlists (the power amp drives it live).
    // useTables: evaluate the triodes through interpolation tables (the Eco modes).
    // splitStages: with useTables, also use the amp's finer-split Eco preamp when it has one.
    Preamp(const AmpDef& def, double fs, double railVolts, bool useTables = false, bool splitStages = true);

    std::size_t knobCount() const { return knobs_.size(); }
    const std::vector<double>& knobs() const { return values_; }
    // Moves one knob (0..1). Cheap when nothing changed; otherwise re-inverts the circuit matrix.
    void setKnob(std::size_t index, double value);
    // Moves every knob at once with a single re-inversion.
    void setKnobs(const std::vector<double>& values);

    // Driven every sample from the power amp so the preamp feels the supply sag.
    void setRail(double volts) {
        for (auto& s : stages_)
            if (!s.pre) s.solver.setSource(s.rail, volts);
        if (vib_) vib_->setRail(volts);
        if (rev_) rev_->setRail(volts);
    }
    // ~7 kohm for a guitar, far less behind a pedal.
    void setSourceOhms(double ohms);
    // The guitar's own impedance instead of the plain source resistance: pickup 0 = off (RSRC), 1 = single coil,
    // 2 = humbucker, 3 = P-90; the cable is `cableMeters` (0..14, in steps of 2) of capacitance. A pedal in front of the amp (its buffer
    // isolates the guitar) turns it off. Does nothing for netlists without the pickup networks.
    void setGuitar(int pickup, int cableMeters, bool pedalOn);
    // Optical tremolo (amps that have one): footswitch and speed knob, 0..1.
    bool hasVibrato() const { return bool(vib_); }
    void setVibrato(bool on, double speed) {
        if (vib_) { vib_->setOn(on); vib_->setSpeed(speed, potMinOhms_); }
    }
    void setJack(std::size_t jack);
    // Selector `i` (amp.json "selectors") to position `pos`.
    void setSelector(std::size_t i, std::size_t pos);
    std::size_t selectorCount() const { return selectors_.size(); }
    std::size_t jack() const { return jack_; }

    // One sample: guitar volts in, master-volume output (volts) out.
    double process(double vin) {
        if (preOn_) vin = runPre(vin);
        const Jack& j = jacks_[jack_];
        Stage& first = stages_.front();
        if (rev_) first.solver.setSource(revSrc_, rev_->process(first.solver.voltage(revGrid_)));
        if (j.drivesInput) {
            first.solver.process(vin);
        } else {
            first.solver.setSource(j.drive, vin);  // the unused jack is grounded by its own contact
            first.solver.process(0.0);
        }
        for (std::size_t k = 1; k < mainStages_; ++k) {
            Stage& s = stages_[k];
            for (const Link& l : s.links) s.solver.setSource(l.src, stages_[l.from].solver.voltage(l.node));
            if (s.input) s.solver.process(j.drivesInput ? vin : 0.0);
            else s.solver.step();
        }
        if (vib_ && ++vibCount_ >= vibEvery_) advanceVibrato();
        return stages_[mainStages_ - 1].solver.voltage(out_);
    }

    std::size_t stageCount() const { return stages_.size(); }
    void setChord(bool on, double keep = 0.0625) { for (auto& s : stages_) { s.solver.chord = on; s.solver.luKeepRatio = keep; } }
    Solver& solver(std::size_t stage = 0) { return stages_[stage].solver; }
    const Solver& solver(std::size_t stage = 0) const { return stages_[stage].solver; }
    // Failed Newton solves summed over all stages.
    std::uint64_t newtonFailures() const {
        std::uint64_t n = 0;
        for (const auto& s : stages_) n += s.solver.newtonFailures;
        return n;
    }

private:
    // A resistor of some stage's netlist, and every copy of it: when stages are split, the network
    // between two tubes is repeated in both (the downstream one is its load) and must move together.
    struct Ref { std::size_t stage = 0; ResId id{0}; };
    using Refs = std::vector<Ref>;
    struct Knob {
        PotDef::Type type;
        Refs top, bottom;  // Variable pots use `top`
        double total;
        Taper taper;
    };
    struct Jack {
        SrcId drive{0};
        bool drivesInput = false;
        std::vector<std::pair<Refs, double>> resistors;
    };
    struct Link {
        std::size_t from = 0;  // the stage whose node drives the source
        SrcId src{0};
        Node node = 0;
    };
    struct Stage {
        Solver solver;
        SrcId rail{0};
        std::vector<Link> links;  // stages after the first: sources fed from earlier stages
        bool input = false;       // also driven by the guitar (a parallel channel)
        bool pre = false;         // the circuit in front of the preamp (amp.json "pre")
    };

    static void potValues(const Knob& k, double value, double minOhms, double& top, double& bottom);
    void advanceVibrato();
    // The circuit in front (Beast): one sample of it, then the gate; returns the volts for the preamp's input.
    double runPre(double vin);
    void updateGate();
    void set(const Refs& rs, double ohms) {
        for (const Ref& r : rs) stages_[r.stage].solver.setResistor(r.id, ohms);
    }
    void refreshAll() {
        for (auto& s : stages_) s.solver.refresh();
    }

    std::vector<Stage> stages_;
    std::size_t mainStages_ = 0;  // stages_ without the pre stage (which is stored last)
    bool preOn_ = false;
    std::size_t preSel_ = static_cast<std::size_t>(-1), gateKnob_ = static_cast<std::size_t>(-1);
    Node preOut_ = 0, preDet_ = 0;
    SrcId preIn_{0};
    double fs_ = 48000.0, gateEnv_ = 0.0, gateGain_ = 1.0, gateTarget_ = 1.0, gateThrDb_ = -200.0, gateAtk_ = 0.0, gateRel_ = 0.0, gateSmooth_ = 0.0;
    int gateCount_ = 0;
    Node out_ = 0;
    double potMinOhms_;
    double sourceOhmsMin_;
    std::vector<Refs> sourceResistors_;
    std::size_t rsrcIndex_ = static_cast<std::size_t>(-1);  // which of the source resistors is the guitar's RSRC
    bool hasPickups_ = false, guitarActive_ = false;
    int pickup_ = 0, cableMeters_ = 0;
    Refs pickupCoil_[2], pickupLoss_;  // the two coil branches (series resistance) and the shared loss resistor
    Refs cableSwitch_[3];
    double sourceOhms_;
    std::vector<Knob> knobs_;
    std::vector<double> values_;
    std::vector<Jack> jacks_;
    std::size_t jack_ = 0;
    std::vector<std::vector<std::vector<std::pair<Refs, double>>>> selectors_;  // [selector][position] -> resistors to set
    std::vector<std::vector<std::vector<std::pair<std::size_t, double>>>> selectorPots_;  // [selector][position] -> (knob, total of its pot)
    std::vector<std::size_t> selectorPos_;
    std::unique_ptr<Vibrato> vib_;
    std::unique_ptr<Reverb> rev_;  // spring reverb: tapped from a node of stage 0, injected at a source of it
    SrcId revSrc_{0};
    Node revGrid_ = 0;
    Refs cellRefs_;
    std::size_t vibCount_ = 0, vibEvery_ = 64;
    double lastCellOhms_ = 1.0;
};

}  // namespace sas
