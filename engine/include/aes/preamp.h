#pragma once
// Data-driven preamp: one or more netlists plus the knob/jack mapping from amp.json. The knobs are
// pots whose sections are resistors of the netlists; jacks switch resistors and choose which source
// carries the guitar. With several netlists ("stages") each is solved on its own and the next one
// is fed from a node of the previous one, which is much cheaper than one big circuit.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "aes/amp_def.h"
#include "aes/circuit.h"

namespace aes {

class Preamp {
public:
    // railVolts overrides the rail supply's value in the netlists (the power amp drives it live).
    Preamp(const AmpDef& def, double fs, double railVolts);

    std::size_t knobCount() const { return knobs_.size(); }
    const std::vector<double>& knobs() const { return values_; }
    // Moves one knob (0..1). Cheap when nothing changed; otherwise re-inverts the circuit matrix.
    void setKnob(std::size_t index, double value);
    // Moves every knob at once with a single re-inversion.
    void setKnobs(const std::vector<double>& values);

    // Driven every sample from the power amp so the preamp feels the supply sag.
    void setRail(double volts) {
        for (auto& s : stages_) s.solver.setSource(s.rail, volts);
    }
    // ~7 kohm for a guitar, far less behind a pedal.
    void setSourceOhms(double ohms);
    void setJack(std::size_t jack);
    std::size_t jack() const { return jack_; }

    // One sample: guitar volts in, master-volume output (volts) out.
    double process(double vin) {
        const Jack& j = jacks_[jack_];
        Stage& first = stages_.front();
        if (j.drivesInput) {
            first.solver.process(vin);
        } else {
            first.solver.setSource(j.drive, vin);  // the unused jack is grounded by its own contact
            first.solver.process(0.0);
        }
        for (std::size_t k = 1; k < stages_.size(); ++k) {
            Stage& s = stages_[k];
            s.solver.setSource(s.link, stages_[k - 1].solver.voltage(s.linkFrom));
            s.solver.step();
        }
        return stages_.back().solver.voltage(out_);
    }

    std::size_t stageCount() const { return stages_.size(); }
    Solver& solver(std::size_t stage = 0) { return stages_[stage].solver; }
    const Solver& solver(std::size_t stage = 0) const { return stages_[stage].solver; }
    // Failed Newton solves summed over all stages.
    std::uint64_t newtonFailures() const {
        std::uint64_t n = 0;
        for (const auto& s : stages_) n += s.solver.newtonFailures;
        return n;
    }

private:
    // A resistor of some stage's netlist.
    struct Ref { std::size_t stage = 0; ResId id{0}; };
    struct Knob {
        PotDef::Type type;
        Ref top, bottom;  // Variable pots use `top`
        double total;
        Taper taper;
    };
    struct Jack {
        SrcId drive{0};
        bool drivesInput = false;
        std::vector<std::pair<Ref, double>> resistors;
    };
    struct Stage {
        Solver solver;
        SrcId rail{0};
        SrcId link{0};     // stages after the first: the source fed from the previous stage
        Node linkFrom = 0;  // node of the previous stage that drives it
    };

    static void potValues(const Knob& k, double value, double minOhms, double& top, double& bottom);
    void set(const Ref& r, double ohms) { stages_[r.stage].solver.setResistor(r.id, ohms); }
    void refreshAll() {
        for (auto& s : stages_) s.solver.refresh();
    }

    std::vector<Stage> stages_;
    Node out_ = 0;
    double potMinOhms_;
    double sourceOhmsMin_;
    std::vector<Ref> sourceResistors_;
    double sourceOhms_;
    std::vector<Knob> knobs_;
    std::vector<double> values_;
    std::vector<Jack> jacks_;
    std::size_t jack_ = 0;
};

}  // namespace aes
