#include "aes/poweramp2203.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace aes {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kVacPeak = 538.0;         // peak of the full-wave rectified HV secondary
constexpr double kRectSourceOhms = 42.0;   // mains transformer + rectifier
constexpr double kHtDcSource = 490.0;      // rectifier source value for the DC operating point
constexpr double kBiasOpenCircuit = -93.6; // bias supply as a Thevenin equivalent into C20
constexpr double kBiasSourceOhms = 54e3;
constexpr double kRectDrop = 1.4;          // bridge forward drop (two diodes)
constexpr double kCReservoir = 100e-6;
constexpr double kHtTieOhms = 1.0;         // reservoir model to HT node in the audio-rate solve

// Output transformer (C2668).
constexpr double kOtZpp = 1900.0;
constexpr double kOtLHalf = 3.5;  // henries, one primary half
constexpr double kOtKpp = 0.9998, kOtKps = 0.9993, kOtKss = 0.9998;
constexpr double kOtDcrHalf = 40.0;
constexpr double kPreampLoadOhms = 56e3;  // the rest of the preamp's draw on the X rail

}  // namespace

struct PowerAmp2203::Built {
    Circuit circuit;
    SrcId srcAudio{0}, srcHt{0};
    ResId rPres{0}, rNfb{0}, rLoad{0};
    Node x = 0, ht = 0, out = 0;
};

PowerAmp2203::Built PowerAmp2203::build() {
    Built b;
    Circuit& c = b.circuit;
    // Node creation order is part of the contract (golden DC dump lists nodes in this order).
    const Node pin = c.node("pin"), ga = c.node("ga"), kc = c.node("kc"), f1 = c.node("f1"),
               f2 = c.node("f2"), gb = c.node("gb"), pa = c.node("pa"), pbn = c.node("pb"),
               dt = c.node("dt"), db = c.node("db"), bias = c.node("bias"), g45 = c.node("g45"),
               g67 = c.node("g67"), s45 = c.node("s45"), s67 = c.node("s67"), sb = c.node("sb"),
               pt = c.node("pt"), pbm = c.node("pbm"), npt = c.node("npt"), npb = c.node("npb"),
               a = c.node("a"), t16 = c.node("t16"), t8 = c.node("t8"), bk = c.node("bk"),
               x = c.node("x"), nb = c.node("nb"), pr = c.node("pr"), rv = c.node("rv");

    b.srcAudio = c.source(pin, kDefaultPowerSourceOhms, 0.0);
    b.srcHt = c.source(a, kHtTieOhms, kHtDcSource);
    c.source(nb, kBiasSourceOhms, kBiasOpenCircuit);

    // Phase inverter V3 (ECC83): input on the first grid, feedback on the second.
    c.capacitor(pin, ga, 0.022e-6);  // C9
    c.resistor(ga, f1, 1e6);         // R17
    c.resistor(kc, f1, 470.0);       // R16
    c.resistor(f1, gb, 1e6);         // R19
    c.capacitor(gb, f2, 0.1e-6);     // C15
    c.resistor(f1, f2, 10e3);        // R20
    c.resistor(f2, GND, 4.7e3);      // R23
    c.triode(ga, pa, kc, TriodeModel::ecc83());
    c.triode(gb, pbn, kc, TriodeModel::ecc83());
    c.resistor(x, pa, 82e3);         // R18
    c.resistor(x, pbn, 100e3);       // R21
    c.capacitor(pa, dt, 0.022e-6);   // C14
    c.capacitor(pbn, db, 0.022e-6);  // C16
    c.capacitor(dt, db, 47e-12);     // C18

    // Presence: C17 in series with VR6 (variable resistor) to ground, on the feedback node.
    c.capacitor(f2, pr, 0.1e-6);     // C17
    b.rPres = c.resistor(pr, GND, 5e3);  // VR6

    // Power-tube grid return and fixed bias.
    c.resistor(dt, bias, 220e3);     // R24
    c.resistor(bias, db, 220e3);     // R25
    c.capacitor(bias, GND, 10e-6);   // C19
    c.resistor(bias, nb, 15e3);      // R27
    c.resistor(bias, rv, 47e3);      // R26
    c.resistor(rv, GND, 11e3);       // RV1 (bias trim)
    c.capacitor(nb, GND, 10e-6);     // C20

    // EL34s: each pair shares grid stoppers, screen resistors and plate load.
    c.resistor(dt, g45, 5.6e3 / 2.0);  // R31 || R33
    c.resistor(db, g67, 5.6e3 / 2.0);  // R32 || R34
    c.pentode(g45, pt, s45, GND, PentodeModel::el34(), 2.0);
    c.pentode(g67, pbm, s67, GND, PentodeModel::el34(), 2.0);
    c.resistor(sb, s45, 1e3 / 2.0);    // R35 || R37
    c.resistor(sb, s67, 1e3 / 2.0);    // R36 || R38

    // Output transformer: five windings, turns relative to one primary half.
    c.resistor(pt, npt, kOtDcrHalf);
    c.resistor(pbm, npb, kOtDcrHalf);
    constexpr double nPri = 1.0;
    const double n16 = 2.0 * nPri / std::sqrt(kOtZpp / 16.0);
    const std::array<double, 5> turns{nPri, nPri, 0.2929 * n16, 0.2071 * n16, 0.5 * n16};
    auto coupling = [](std::size_t i, std::size_t j) {
        const auto pri = [](std::size_t k) { return k < 2; };
        if (i == j) return 1.0;
        if (pri(i) && pri(j)) return kOtKpp;
        return pri(i) != pri(j) ? kOtKps : kOtKss;
    };
    std::vector<double> l(25);
    for (std::size_t i = 0; i < 5; ++i)
        for (std::size_t j = 0; j < 5; ++j) l[i * 5 + j] = kOtLHalf * turns[i] * turns[j] * coupling(i, j);
    c.coupledInductors({{npt, a}, {a, npb}, {t16, t8}, {t8, bk}, {bk, GND}}, l);
    b.rLoad = c.resistor(t16, GND, 16.0);

    // Negative feedback from the secondary tap to the phase-inverter feedback node.
    b.rNfb = c.resistor(bk, f2, 100e3);  // R22

    // HT supply: bridge (as one diode behind |v|), reservoir, choke, screen and preamp rails.
    c.resistor(a, sb, 100.0);        // T3 choke (DC resistance)
    c.capacitor(sb, GND, 100e-6);    // C22
    c.resistor(sb, x, 10e3);         // R29
    c.capacitor(x, GND, 50e-6);      // C21 (X side)
    c.resistor(x, GND, kPreampLoadOhms);

    b.x = x;
    b.ht = a;
    b.out = t16;
    return b;
}

PowerAmp2203::PowerAmp2203(Built&& b, double fs)
    : solver_(std::move(b.circuit), fs), srcAudio_(b.srcAudio), srcHt_(b.srcHt), vHt_(kHtDcSource),
      dt_(1.0 / fs), rPres_(b.rPres), rNfb_(b.rNfb), rLoad_(b.rLoad), dphase_(2.0 * kPi * kMainsHz / fs),
      nodeX_(b.x), nodeHt_(b.ht), nodeOut_(b.out) {
    if (!solver_.dcConverged) throw std::runtime_error("power amp DC operating point did not converge");
    // Signals here are volts to tens of volts, so a looser tolerance is inaudible (-125 dB).
    solver_.tolerance = kPowerAmpTolerance;
    setPresence(0.5);
    // Let the reservoir, bias and filter caps settle onto the real rectified waveform.
    for (std::size_t i = 0, n = std::size_t(0.15 * fs); i < n; ++i) process(0.0);
}

PowerAmp2203::PowerAmp2203(double fs) : PowerAmp2203(build(), fs) {}

void PowerAmp2203::setPresence(double p) {
    presence_ = std::clamp(p, 0.0, 1.0);
    solver_.setResistor(rPres_, std::max(22e3 * (1.0 - presence_), 50.0));
    solver_.refresh();
}

double PowerAmp2203::process(double vin) {
    const double s = std::sin(phase_);
    phase_ += dphase_;
    if (phase_ > 2.0 * kPi) phase_ -= 2.0 * kPi;
    // Reservoir: charged through the bridge while |v_ac| exceeds it, drained by the load.
    const double iRect = std::max((kVacPeak * std::abs(s) - kRectDrop - vHt_) / kRectSourceOhms, 0.0);
    const double iLoad = (vHt_ - solver_.voltage(nodeHt_)) / kHtTieOhms;
    vHt_ += dt_ / kCReservoir * (iRect - iLoad);
    solver_.setSource(srcHt_, vHt_);
    solver_.setSource(srcAudio_, vin);
    solver_.step();
    return solver_.voltage(nodeOut_);
}

}  // namespace aes
