#pragma once
// A Klon Centaur-style overdrive, solved as a circuit: engine/data/klon.net is a transcription of the ElectroSmash drawing (buffer, gain stage with germanium
// clipping diodes, the clean path, the summing amplifier, the tone stage and the level). The circuit runs at twice the host rate (the diodes alias otherwise),
// in front of the amp. Gain is the dual-gang pot, Treble the tone pot of the second op-amp, Level the output pot.

#include <cstddef>

#include "sas/circuit.h"
#include "sas/netlist.h"
#include "sas/oversample.h"

namespace sas {

// The netlist text embedded in the library.
const char* klonNetlistText();

struct KlonControls {
    double gain = 0.5;    // 0..1 (audio taper, two sections of one pot)
    double treble = 0.5;  // 0..1 (linear)
    double level = 0.5;   // 0..1 (audio taper)
    bool operator==(const KlonControls&) const = default;
};

class Klon {
public:
    explicit Klon(double fs);

    void setControls(const KlonControls& c);
    const KlonControls& controls() const { return controls_; }

    // Processes `n` samples in place (volts in, volts out; AC coupled).
    void process(float* x, std::size_t n);
    float latencySamples() const { return os_.latencySamples(); }
    void reset();
    const Solver& solver() const { return solver_; }

private:
    struct Built;
    Klon(Built&& b, double fs);
    static Built build();

    Solver solver_;
    Oversampler os_{1};
    Node out_;
    ResId gB_, gT_, g2T_, g2B_, tT_, tB_, lT_, lB_;
    KlonControls controls_;
};

}  // namespace sas
