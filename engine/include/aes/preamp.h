#pragma once
// Data-driven preamp: a netlist plus the knob/jack mapping from amp.json. The knobs are pots
// whose sections are resistors of the netlist; jacks switch resistors and choose which source
// carries the guitar.

#include <cstddef>
#include <vector>

#include "aes/amp_def.h"
#include "aes/circuit.h"

namespace aes {

class Preamp {
public:
    // railVolts overrides the rail supply's value in the netlist (the power amp drives it live).
    Preamp(const AmpDef& def, double fs, double railVolts);

    std::size_t knobCount() const { return knobs_.size(); }
    const std::vector<double>& knobs() const { return values_; }
    // Moves one knob (0..1). Cheap when nothing changed; otherwise re-inverts the circuit matrix.
    void setKnob(std::size_t index, double value);
    // Moves every knob at once with a single re-inversion.
    void setKnobs(const std::vector<double>& values);

    // Driven every sample from the power amp so the preamp feels the supply sag.
    void setRail(double volts) { solver_.setSource(railSrc_, volts); }
    // ~7 kohm for a guitar, far less behind a pedal.
    void setSourceOhms(double ohms);
    void setJack(std::size_t jack);
    std::size_t jack() const { return jack_; }

    // One sample: guitar volts in, master-volume output (volts) out.
    double process(double vin) {
        const Jack& j = jacks_[jack_];
        if (j.drivesInput) {
            solver_.process(vin);
        } else {
            solver_.setSource(j.drive, vin);  // the unused jack is grounded by its own contact
            solver_.process(0.0);
        }
        return solver_.voltage(out_);
    }

    Solver& solver() { return solver_; }
    const Solver& solver() const { return solver_; }

private:
    struct Knob {
        PotDef::Type type;
        ResId top{0}, bottom{0};  // Variable pots use `top`
        double total;
        Taper taper;
    };
    struct Jack {
        SrcId drive{0};
        bool drivesInput = false;
        std::vector<std::pair<ResId, double>> resistors;
    };
    struct Built;
    Preamp(Built&& b, const AmpDef& def, double fs);
    static Built build(const AmpDef& def, double railVolts);

    static void potValues(const Knob& k, double value, double minOhms, double& top, double& bottom);

    Solver solver_;
    SrcId railSrc_;
    Node out_;
    double potMinOhms_;
    double sourceOhmsMin_;
    std::vector<ResId> sourceResistors_;
    double sourceOhms_;
    std::vector<Knob> knobs_;
    std::vector<double> values_;
    std::vector<Jack> jacks_;
    std::size_t jack_ = 0;
};

}  // namespace aes
