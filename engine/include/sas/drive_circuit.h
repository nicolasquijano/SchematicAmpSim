#pragma once
// The overdrive pedals solved as circuits (the Tube Screamer is the analytic model of sas/ts808_fast.h; these are the others of the Overdrive module):
//   SD1: a Super OverDrive style pedal (engine/data/sd1.net): an op-amp gain stage with asymmetrical diodes (one against two) in its feedback, a low pass and an active tone stage.
//   BD2: a Blues Driver style pedal (engine/data/bd2.net): two discrete gain stages with the dual GAIN pot, a four-diode clipper between them, a passive tone, LEVEL and an
//        op-amp stage with a gyrator.
//   Ds1: a DS-1 style distortion (engine/data/ds1.net): a transistor booster, a TA7136 gain stage, hard diode clipping and the passive tone; the first model of the Distortion module.
//   Rat: a RAT style distortion (engine/data/rat.net): an LM308 gain stage with the DISTORTION pot in its feedback, hard diode clipping, the FILTER low pass and a JFET buffer.
// Each runs at twice the host rate, the RAT at four times (the diodes alias otherwise) in front of the amp, and takes the same three controls as the Tube Screamer: drive (gain), tone and level, 0..1.

#include <cstddef>
#include <memory>

#include "sas/circuit.h"
#include "sas/netlist.h"
#include "sas/oversample.h"
#include "sas/rat_fast.h"
#include "sas/sd1_fast.h"
#include "sas/ts808_fast.h"

namespace sas {

// The netlist texts embedded in the library.
const char* sd1NetlistText();
const char* bd2NetlistText();
const char* ratNetlistText();
const char* ds1NetlistText();

enum class DriveModel { TubeScreamer = 0, SuperOverdrive = 1, BluesDriver = 2, Rat = 3, Ds1 = 4 };
inline constexpr int kDriveModelCount = 4;  // the models of the Overdrive module (the first four)
inline constexpr int kDistortionModelCount = 1;  // the models of the Distortion module (DriveModel::Ds1 onwards)

// How a pedal is solved: Fast is the analytic model where there is one (the SD1 and the RAT so far: sas/sd1_fast.h, sas/rat_fast.h, a few percent of the nodal solver's cost) and the circuit
// where there is not; Circuit is always the nodal solver, the reference the fast models are compared with.
enum class DriveSolve { Fast, Circuit };

class DriveCircuit {
public:
    // `model` is SuperOverdrive, BluesDriver or Rat; `fs` is the host rate.
    DriveCircuit(DriveModel model, double fs, DriveSolve solve = DriveSolve::Fast);

    void setControls(const TsControls& c);
    const TsControls& controls() const { return controls_; }

    // Processes `n` samples in place (volts in, volts out; AC coupled).
    void process(float* x, std::size_t n);
    float latencySamples() const { return os_.latencySamples(); }
    void reset();
    const Solver& solver() const { return solver_; }

private:
    struct Built;
    DriveCircuit(Built&& b, DriveModel model, double fs, DriveSolve solve);
    static Built build(DriveModel model);

    DriveModel model_;
    Solver solver_;
    Oversampler os_;
    Node out_;
    ResId drive1_, drive2_, toneA_, toneB_, levelT_, levelB_;
    TsControls controls_;
    std::unique_ptr<SuperOverdriveFast> fast_;  // set when the analytic model runs in place of the solver
    std::unique_ptr<RatFast> fastRat_;
};

}  // namespace sas
