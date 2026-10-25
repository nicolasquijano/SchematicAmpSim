#pragma once
// Optical tremolo of a Fender amp: the phase-shift oscillator and LED driver circuit, solved at a
// low rate of its own (one step per `controlSamples` audio samples; the effect is a few hertz), and
// the photocell that the LED lights. The cell's resistance is what the preamp reads.

#include <cstdint>

#include "sas/amp_def.h"
#include "sas/circuit.h"
#include "sas/netlist.h"

namespace sas {

class Vibrato {
public:
    // fs is the audio rate of the preamp; railVolts the supply the oscillator hangs on.
    Vibrato(const VibratoDef& def, double fs, double railVolts, bool useTables);

    void setOn(bool on);
    void setSpeed(double value, double minOhms);  // 0..1
    void setRail(double volts) { solver_.setSource(rail_, volts); }

    // Advances the oscillator by one control period; returns the photocell resistance (ohms).
    double step();

    std::size_t controlSamples() const { return def_.controlSamples; }
    double cellOhms() const { return 1.0 / g_; }
    std::uint64_t newtonFailures() const { return solver_.newtonFailures; }

private:
    VibratoDef def_;
    ParsedNetlist net_;
    Solver solver_;
    SrcId rail_;
    ResId switch_, speed_;
    Node ledFrom_, ledTo_;
    double dt_;
    double g_;            // photocell conductance state (S)
    double aAttack_, aRelease_;
    bool on_ = false;
    double speedValue_ = -1.0;
};

}  // namespace sas
