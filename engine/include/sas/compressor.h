#pragma once
// The compressor pedal, solved as a circuit: engine/data/compressor.net is a transcription of the Keeley Compressor Plus schematic
// (OTA gain cell, transistor envelope detector, feedback). The pedal runs at the host sample rate, in front of the amp.

#include "sas/circuit.h"
#include "sas/netlist.h"

namespace sas {

// The netlist text embedded in the library.
const char* compressorNetlistText();

struct CompControls {
    double sustain = 0.5;      // 0..1: more compression and gain (the OTA bias current)
    double blend = 0.7;        // 0..1: 0 = dry only, 1 = compressed only
    double tone = 0.5;         // 0..1: treble of the compressed path
    double level = 0.5;        // 0..1: output level (audio taper)
    bool fastRelease = false;  // the pedal's ATTACK switch: the envelope recovers through 10k instead of 160k
    bool operator==(const CompControls&) const = default;
};

class Compressor {
public:
    explicit Compressor(double fs);

    void setControls(const CompControls& c);
    const CompControls& controls() const { return controls_; }
    // Resistance of the source (the guitar or the pedal before it).
    void setSourceOhms(double ohms);

    // One sample: volts in, volts out (AC coupled).
    double process(double vin) {
        solver_.process(vin);
        return solver_.voltage(out_);
    }

    // Envelope voltage (about the supply when idle, lower the harder the compressor works) and the OTA bias current, for tests and meters.
    double envelope() const { return solver_.voltage(env_); }
    double otaBiasAmps() const { return (solver_.voltage(q5e_) - solver_.voltage(pin1_)) / (27e3 + sustainOhms_); }
    const Solver& solver() const { return solver_; }
    Solver& solver() { return solver_; }

private:
    struct Built;
    Compressor(Built&& b, double fs);
    static Built build();

    Solver solver_;
    Node out_, env_, q5e_, pin1_;
    ResId rSus_, rTone_, rBtTop_, rBtBot_, rLvTop_, rLvBot_, rAttack_, rSrc_;
    CompControls controls_;
    double sustainOhms_ = 100e3;
};

}  // namespace sas
