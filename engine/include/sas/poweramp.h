#pragma once
// Data-driven full power amp: a netlist (phase inverter, output tubes, output transformer, HT
// supply) plus the rectifier/reservoir model from amp.json. The reservoir voltage is integrated
// at the audio rate from the real load current, so sag and ripple come out of the circuit.

#include "sas/amp_def.h"
#include "sas/circuit.h"

namespace sas {

class PowerAmp {
public:
    PowerAmp(const AmpDef& def, double fs);

    // Presence knob, 0..1: more presence shunts more of the high-frequency feedback.
    void setPresence(double p);
    double presence() const { return presence_; }
    // Power-amp controls from amp.json "power.knobs" (0..1 each), e.g. a feedback "power" pot.
    std::size_t knobCount() const { return knobs_.size(); }
    void setKnobs(const std::vector<double>& values);
    void setLoad(double ohms) { solver_.setResistor(rLoad_, ohms); solver_.refresh(); }
    // Feedback resistor (100k stock in the 2203): larger = less negative feedback.
    void setNfbOhms(double ohms) { solver_.setResistor(rNfb_, ohms); solver_.refresh(); }

    // Bias control: multiplies the bias supply of the netlist (1 = as drawn). The bias filter cap settles
    // in a few tens of milliseconds, like turning the trim pot of a real amp.
    bool hasBias() const { return hasBias_; }
    void setBias(double scale) { if (hasBias_) solver_.setSource(srcBias_, biasNominal_ * scale); }
    // Average plate current of one output tube, in mA (smoothed over about a third of a second).
    double idleMilliamps() const { return idleMa_; }

    // One sample: phase-inverter input volts in, speaker volts out.
    double process(double vin);

    // Voltage on the preamp rail.
    double railX() const { return solver_.voltage(nodeX_); }
    double ht() const { return solver_.voltage(nodeHt_); }

    Solver& solver() { return solver_; }
    const Solver& solver() const { return solver_; }

private:
    struct Built;
    PowerAmp(Built&& b, const AmpDef& def, double fs);
    // Dimensions ordered so the member initialisers below stay in declaration order.
    static Built build(const AmpDef& def);

    Solver solver_;
    SupplyDef supply_;
    SrcId srcAudio_, srcHt_;
    ResId rPres_, rNfb_, rLoad_;
    double presenceScale_, presenceMinOhms_;
    ResId rPresBottom_;
    bool hasPresBottom_;
    bool hasPres_ = true;
    struct Knob { ResId resistor; double scale, minOhms; };
    std::vector<Knob> knobs_;
    std::vector<double> knobValues_;
    Node nodeX_, nodeHt_, nodeOut_;
    double vHt_;  // reservoir voltage
    double dt_;
    double phase_ = 0.0, dphase_;
    bool hasBias_ = false;
    SrcId srcBias_{0};
    double biasNominal_ = 0.0;
    struct IdleProbe { Node a, b; double ohms; };
    std::vector<IdleProbe> idleProbes_;
    double idleTubesPerHalf_ = 2.0, idleMa_ = 0.0, idleK_ = 0.0;
    double presence_ = 0.5;
};

}  // namespace sas
