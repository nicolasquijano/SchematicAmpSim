#include "aes/preamp2203.h"

#include <algorithm>
#include <cmath>

namespace aes {

namespace {

constexpr double kMasterLoadOhms = 1.0e6;
constexpr double kSwitchClosedOhms = 1.0;
constexpr double kSwitchOpenOhms = 1.0e12;
constexpr double kMinOhms = 100.0;  // wiper contact floor: a pot at its stop never makes a 0-ohm branch

// Audio ("log") taper: ~15% of the resistance at mid rotation.
double logTaper(double w) {
    constexpr double k = 3.47;
    return (std::exp(k * w) - 1.0) / (std::exp(k) - 1.0);
}

std::pair<double, double> split(double total, double fractionFromGround) {
    const double f = std::clamp(fractionFromGround, 0.0, 1.0);
    return {std::max(total * (1.0 - f), kMinOhms), std::max(total * f, kMinOhms)};
}

}  // namespace

struct Preamp2203::Built {
    Circuit circuit;
    SrcId railSrc{0}, srcLow{0};
    ResId c3Switch{0}, lowSwitch{0}, rIn{0}, rInLow{0};
    Pots pots{};
    Node out = 0;
};

std::array<std::pair<ResId, double>, 9> Preamp2203::potValues(const Pots& pots, const Controls& ctl) {
    const auto [vt, vb] = split(1.0e6, logTaper(ctl.volume));
    const auto [mt, mb] = split(1.0e6, logTaper(ctl.master));
    const auto [tt, tb] = split(220e3, ctl.treble);
    const auto [miT, miB] = split(22e3, ctl.middle);
    const double bass = std::max(1.0e6 * logTaper(ctl.bass), kMinOhms);
    return {{{pots.volTop, vt}, {pots.volBot, vb}, {pots.masterTop, mt}, {pots.masterBot, mb},
             {pots.trebleTop, tt}, {pots.trebleBot, tb}, {pots.midTop, miT}, {pots.midBot, miB},
             {pots.bass, bass}}};
}

Preamp2203::Built Preamp2203::build(double railVolts, const TriodeModel& tube) {
    Built b;
    Circuit& c = b.circuit;
    // Node creation order is part of the contract: the golden DC dump lists nodes in this order.
    const Node vx = c.node("x"), b1 = c.node("b1"), inp = c.node("in"), g1 = c.node("g1"),
               p1 = c.node("p1"), k1 = c.node("k1"), vt = c.node("vt"), vp = c.node("vp"),
               w = c.node("w"), p1b = c.node("p1b"), k1b = c.node("k1b"), g2 = c.node("g2"),
               g2a = c.node("g2a"), p2a = c.node("p2a"), k2a = c.node("k2a"), k2b = c.node("k2b"),
               m = c.node("m"), t = c.node("t"), w3 = c.node("w3"), n1 = c.node("n1"),
               n2 = c.node("n2"), wm = c.node("wm"), y = c.node("y");
    // Created last so the numbering above is unchanged.
    const Node c3n = c.node("c3n"), lowin = c.node("lowin"), gin = c.node("gin"), lowsrc = c.node("lowsrc");

    b.railSrc = c.supply(vx, railVolts, 100.0);
    // The guitar (or the pedal in front of the amp) behind a variable source resistance.
    c.input(gin, 1.0);
    b.rIn = c.resistor(gin, inp, kSourceOhms);

    // Supply decoupling: R8 and the two 50 uF sections of C21.
    c.resistor(vx, b1, 10e3);
    c.capacitor(b1, GND, 50e-6);
    c.capacitor(vx, GND, 50e-6);

    // Input and V1a.
    c.resistor(inp, GND, 1e6);   // R2
    c.resistor(inp, g1, 68e3);   // R3
    c.triode(g1, p1, k1, tube);
    c.resistor(b1, p1, 100e3);   // R4
    c.resistor(k1, GND, 2.7e3);  // R1
    c.capacitor(k1, GND, 0.68e-6);   // C1
    c.capacitor(p1, k1, 100e-12);    // C2

    // Coupling to the preamp volume network.
    c.capacitor(p1, c3n, 0.022e-6);  // C3
    b.c3Switch = c.resistor(c3n, vt, kSwitchClosedOhms);  // Low jack's normally-closed contact
    // Low jack: the guitar enters at the volume network when this switch is closed.
    b.srcLow = c.source(lowsrc, 1.0, 0.0);
    b.rInLow = c.resistor(lowsrc, lowin, kSourceOhms);
    b.lowSwitch = c.resistor(lowin, vt, kSwitchOpenOhms);
    c.resistor(vt, vp, 470e3);       // R5
    c.capacitor(vt, vp, 470e-12);    // C4
    c.capacitor(vp, w, 1e-9);        // C5
    b.pots.volTop = c.resistor(vp, w, 1.0);   // VR1 upper section
    b.pots.volBot = c.resistor(w, GND, 1.0);  // VR1 lower section

    // V1b.
    c.triode(w, p1b, k1b, tube);
    c.resistor(b1, p1b, 100e3);  // R7
    c.resistor(k1b, GND, 10e3);  // R6

    // C7 into V2a's divider.
    c.capacitor(p1b, g2, 0.022e-6);  // C7
    c.resistor(g2, g2a, 470e3);      // R10
    c.capacitor(g2, g2a, 470e-12);   // C8
    c.resistor(g2a, GND, 470e3);     // R11

    // V2a and the V2b cathode follower (plate on the rail).
    c.triode(g2a, p2a, k2a, tube);
    c.resistor(vx, p2a, 100e3);  // R12
    c.resistor(k2a, GND, 820.0); // R9
    c.triode(p2a, vx, k2b, tube);
    c.resistor(k2b, GND, 100e3); // R13

    // Tone stack.
    c.resistor(k2b, m, 33e3);        // R15
    c.capacitor(k2b, t, 470e-12);    // C10
    b.pots.trebleTop = c.resistor(t, w3, 1.0);   // VR3 upper section
    b.pots.trebleBot = c.resistor(w3, n1, 1.0);  // VR3 lower section
    c.capacitor(m, n1, 0.022e-6);    // C11
    b.pots.bass = c.resistor(n1, n2, 1.0);       // VR5 (wiper tied to top: variable resistor)
    b.pots.midTop = c.resistor(n2, wm, 1.0);     // VR4 upper section
    b.pots.midBot = c.resistor(wm, GND, 1.0);    // VR4 lower section
    c.capacitor(m, wm, 0.022e-6);    // C12

    // Master volume and the load it sees.
    b.pots.masterTop = c.resistor(w3, y, 1.0);   // VR2 upper section
    b.pots.masterBot = c.resistor(y, GND, 1.0);  // VR2 lower section
    c.resistor(y, GND, kMasterLoadOhms);

    b.out = y;
    // Pots start at their default positions so the DC point is solved with them.
    for (const auto& [id, ohms] : potValues(b.pots, Controls{})) c.setResistor(id, ohms);
    return b;
}

Preamp2203::Preamp2203(Built&& b, double fs)
    : solver_(std::move(b.circuit), fs), railSrc_(b.railSrc), srcLow_(b.srcLow), c3Switch_(b.c3Switch),
      lowSwitch_(b.lowSwitch), rIn_(b.rIn), rInLow_(b.rInLow), pots_(b.pots), out_(b.out) {
    // Guitar signals are millivolts at V1a's grid, so Newton must converge tightly here.
    solver_.tolerance = kPreampTolerance;
    solver_.refresh();
}

Preamp2203::Preamp2203(double fs, double railVolts, const TriodeModel& tube)
    : Preamp2203(build(railVolts, tube), fs) {}

void Preamp2203::setControls(const Controls& ctl) {
    if (ctl == controls_) return;
    controls_ = ctl;
    for (const auto& [id, ohms] : potValues(pots_, ctl)) solver_.setResistor(id, ohms);
    solver_.refresh();
}

void Preamp2203::setSourceOhms(double ohms) {
    ohms = std::max(ohms, 10.0);
    if (std::abs(ohms - sourceOhms_) < 1e-9) return;
    sourceOhms_ = ohms;
    solver_.setResistor(rIn_, ohms);
    solver_.setResistor(rInLow_, ohms);
    solver_.refresh();
}

void Preamp2203::setInputMode(InputMode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    const bool high = mode == InputMode::High;
    solver_.setResistor(c3Switch_, high ? kSwitchClosedOhms : kSwitchOpenOhms);
    solver_.setResistor(lowSwitch_, high ? kSwitchOpenOhms : kSwitchClosedOhms);
    solver_.refresh();
}

}  // namespace aes
