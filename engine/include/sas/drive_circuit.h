#pragma once
// The overdrive pedals solved as circuits (the Tube Screamer is the analytic model of sas/ts808_fast.h; these are the others of the Overdrive module):
//   SD1: a Super OverDrive style pedal (engine/data/sd1.net): an op-amp gain stage with asymmetrical diodes (one against two) in its feedback, a low pass and an active tone stage.
//   BD2: a Blues Driver style pedal (engine/data/bd2.net): two discrete gain stages with the dual GAIN pot, a four-diode clipper between them, a passive tone, LEVEL and an
//        op-amp stage with a gyrator.
//   Ds1: a Boss DS-1 style distortion (engine/data/ds1.net): a transistor booster, a TA7136 gain stage, hard diode clipping and the passive tone; the first model of the Distortion module.
//   Mt2: the gain stages of a Boss MT-2 style distortion (engine/data/mt2.net, approximate); only DIST is a control here, the EQ and the level are applied after it by the module.
//   Rat: a RAT style distortion (engine/data/rat.net): an LM308 gain stage with the DISTORTION pot in its feedback, hard diode clipping, the FILTER low pass and a JFET buffer.
// Each runs at twice the host rate, the RAT at four times (the diodes alias otherwise) in front of the amp, and takes the same three controls as the Tube Screamer: drive (gain), tone and level, 0..1.

#include <cstddef>
#include <memory>

#include "sas/circuit.h"
#include "sas/netlist.h"
#include "sas/oversample.h"
#include "sas/ts808_fast.h"

namespace sas {

// The netlist texts embedded in the library.
const char* sd1NetlistText();
const char* bd2NetlistText();
const char* ratNetlistText();
const char* ds1NetlistText();
const char* mt2NetlistText();

enum class DriveModel { TubeScreamer = 0, SuperOverdrive = 1, BluesDriver = 2, Rat = 3, Ds1 = 4, Mt2 = 5 };
inline constexpr int kDriveModelCount = 4;  // the models of the Overdrive module (the first four)
inline constexpr int kDistortionModelCount = 2;  // the models of the Distortion module (DriveModel::Ds1 onwards)

class DriveCircuit {
public:
    // `model` is SuperOverdrive, BluesDriver or Rat; `fs` is the host rate.
    DriveCircuit(DriveModel model, double fs);

    void setControls(const TsControls& c);
    const TsControls& controls() const { return controls_; }

    // Processes `n` samples in place (volts in, volts out; AC coupled).
    void process(float* x, std::size_t n);
    float latencySamples() const { return os_.latencySamples(); }
    void reset();
    const Solver& solver() const { return solver_; }

private:
    struct Built;
    DriveCircuit(Built&& b, DriveModel model, double fs);
    static Built build(DriveModel model);

    DriveModel model_;
    Solver solver_;
    Oversampler os_;
    Node out_;
    ResId drive1_, drive2_, toneA_, toneB_, levelT_, levelB_;
    TsControls controls_;
};

}  // namespace sas
