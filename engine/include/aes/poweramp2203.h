#pragma once
// JCM800 2203 power amp: long-tailed ECC83 phase inverter with the NFB/presence network, four
// EL34 as two parallel pairs, the output transformer as five coupled windings, and the HT supply
// solved together with the audio so sag and ripple come out of the circuit.
// Port of jcm800/src/poweramp.rs (transcribed from the factory schematic, drawing 2203 STD).

#include "aes/circuit.h"

namespace aes {

inline constexpr double kMainsHz = 50.0;
inline constexpr double kDefaultPowerSourceOhms = 50e3;
inline constexpr double kPowerAmpTolerance = 1e-4;

class PowerAmp2203 {
public:
    explicit PowerAmp2203(double fs);

    // Presence knob, 0..1: more presence shunts more of the high-frequency feedback.
    void setPresence(double p);
    double presence() const { return presence_; }
    void setLoad(double ohms) { solver_.setResistor(rLoad_, ohms); solver_.refresh(); }
    // Feedback resistor R22 (100k stock): larger = less negative feedback.
    void setNfbOhms(double ohms) { solver_.setResistor(rNfb_, ohms); solver_.refresh(); }

    // One sample: phase-inverter input volts in, speaker volts out.
    double process(double vin);

    // Voltage on the preamp rail (node X).
    double railX() const { return solver_.voltage(nodeX_); }
    double ht() const { return solver_.voltage(nodeHt_); }

    Solver& solver() { return solver_; }
    const Solver& solver() const { return solver_; }

private:
    struct Built;
    PowerAmp2203(Built&& b, double fs);
    static Built build();

    Solver solver_;
    SrcId srcAudio_, srcHt_;
    double vHt_;  // reservoir (C23) voltage, integrated at the audio rate from the real load current
    double dt_;
    ResId rPres_, rNfb_, rLoad_;
    double phase_ = 0.0, dphase_;
    Node nodeX_, nodeHt_, nodeOut_;
    double presence_ = 0.5;
};

}  // namespace aes
