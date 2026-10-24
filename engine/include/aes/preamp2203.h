#pragma once
// JCM800 2203 preamp (V1a, V1b, V2a, V2b cathode follower, tone stack, master) built from the
// factory schematic. Port of jcm800/src/preamp.rs.

#include <array>

#include "aes/circuit.h"

namespace aes {

// Knob positions, 0 (fully counter-clockwise) to 1.
struct Controls {
    double volume = 0.6, bass = 0.5, middle = 0.5, treble = 0.6, master = 0.5;
    bool operator==(const Controls&) const = default;
};

enum class InputMode { High, Low };

inline constexpr double kDefaultRailVolts = 410.0;
inline constexpr double kSourceOhms = 7000.0;
inline constexpr double kPreampTolerance = 2e-6;

class Preamp2203 {
public:
    Preamp2203(double fs, double railVolts, const TriodeModel& tube);

    const Controls& controls() const { return controls_; }
    void setControls(const Controls& ctl);
    // Driven every sample from the power amp so the preamp feels the supply sag.
    void setRail(double volts) { solver_.setSource(railSrc_, volts); }
    // ~7 kohm for a guitar, far less behind a pedal.
    void setSourceOhms(double ohms);
    void setInputMode(InputMode mode);
    InputMode inputMode() const { return mode_; }

    // One sample: guitar volts in, master-volume output (volts) out.
    double process(double vin) {
        if (mode_ == InputMode::High) {
            solver_.process(vin);
        } else {
            solver_.setSource(srcLow_, vin);  // the unused High jack is grounded by its own contact
            solver_.process(0.0);
        }
        return solver_.voltage(out_);
    }

    Solver& solver() { return solver_; }
    const Solver& solver() const { return solver_; }

private:
    struct Pots {
        ResId volTop, volBot, bass, midTop, midBot, trebleTop, trebleBot, masterTop, masterBot;
    };
    struct Built;  // circuit under construction plus the handles the preamp keeps
    explicit Preamp2203(Built&& b, double fs);
    static Built build(double railVolts, const TriodeModel& tube);
    static std::array<std::pair<ResId, double>, 9> potValues(const Pots& pots, const Controls& ctl);

    Solver solver_;
    SrcId railSrc_, srcLow_;
    ResId c3Switch_, lowSwitch_, rIn_, rInLow_;
    double sourceOhms_ = kSourceOhms;
    InputMode mode_ = InputMode::High;
    Pots pots_;
    Node out_;
    Controls controls_;
};

}  // namespace aes
