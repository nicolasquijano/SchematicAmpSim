#include "sas/poweramp.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "sas/netlist.h"

namespace sas {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

struct PowerAmp::Built {
    ParsedNetlist net;
    SrcId srcAudio{0}, srcHt{0};
    ResId rPres{0}, rNfb{0}, rLoad{0}, rPresBottom{0};
    bool hasPresBottom = false, hasPres = false;
    std::vector<ResId> knobResistors;
    bool hasBias = false;
    SrcId srcBias{0};
    double biasNominal = 0.0;
    std::vector<std::pair<std::pair<Node, Node>, double>> idle;
    Node x = 0, ht = 0, out = 0;
};

PowerAmp::Built PowerAmp::build(const AmpDef& def) {
    const PowerDef& pd = def.power;
    Built b;
    b.net = parseNetlist(pd.netlistText, pd.netlist);
    b.srcAudio = b.net.source(pd.inputSource);
    b.srcHt = b.net.source(pd.htSource);
    if (!pd.presenceResistor.empty()) {
        b.rPres = b.net.resistor(pd.presenceResistor);
        b.hasPres = true;
    }
    if (!pd.presenceComplement.empty()) {
        b.rPresBottom = b.net.resistor(pd.presenceComplement);
        b.hasPresBottom = true;
    }
    for (const auto& k : pd.knobs) {
        b.knobResistors.push_back(b.net.resistor(k.resistor));
        // The solve for the DC point starts from the knob's default position.
        b.net.circuit.setResistor(b.knobResistors.back(), std::max(k.scale * k.def, k.minOhms));
    }
    if (pd.bias.adjustable()) {
        b.hasBias = true;
        b.srcBias = b.net.source(pd.bias.source);
        b.biasNominal = b.net.circuit.sourceDc(b.srcBias);
    }
    {
        for (const auto& name : pd.bias.idleResistors) {
            const auto& r = b.net.circuit.resistorAt(b.net.resistor(name));
            b.idle.push_back({{r.a, r.b}, r.ohms});
        }
    }
    if (!pd.nfbResistor.empty()) b.rNfb = b.net.resistor(pd.nfbResistor);
    b.rLoad = b.net.resistor(pd.loadResistor);
    if (pd.transformer.enabled && pd.bias.idleResistors.size() >= 2) {
        // The primary's two ends are where the plates' DC resistors meet the winding.
        Circuit& c = b.net.circuit;
        const Node top = c.resistorAt(b.net.resistor(pd.bias.idleResistors[0])).b, bottom = c.resistorAt(b.net.resistor(pd.bias.idleResistors[1])).b;
        if (pd.transformer.primaryPf > 0.0) c.capacitor(top, bottom, pd.transformer.primaryPf * 1e-12);
        if (pd.transformer.coreLossOhms > 0.0) c.resistor(top, bottom, pd.transformer.coreLossOhms);
    }
    if (pd.speakerLoad.enabled) {
        // The load resistor becomes the voice coil's DC resistance; behind it come its inductance and the cone resonance.
        const auto& sl = pd.speakerLoad;
        Circuit& c = b.net.circuit;
        const Circuit::Res load = c.resistorAt(b.rLoad);
        const double z = load.ohms;
        const Node sp = c.node("spk_coil"), spm = c.node("spk_cone");
        c.moveResistor(b.rLoad, load.a, sp);
        c.setResistor(b.rLoad, sl.reFraction * z);
        c.coupledInductors({{sp, spm}}, {sl.leMhPerOhm * 1e-3 * z});
        const double rres = sl.peakFraction * z, w0 = 2.0 * kPi * sl.resonanceHz;
        c.resistor(spm, load.b, rres);
        c.capacitor(spm, load.b, sl.q / (w0 * rres));
        c.coupledInductors({{spm, load.b}}, {rres / (sl.q * w0)});
    }
    b.x = b.net.node(pd.railNode);
    b.ht = b.net.node(pd.htNode);
    b.out = b.net.node(pd.outputNode);
    return b;
}

PowerAmp::PowerAmp(Built&& b, const AmpDef& def, double fs, bool useTables)
    : solver_(std::move(b.net.circuit), fs, useTables), supply_(def.power.supply), srcAudio_(b.srcAudio),
      srcHt_(b.srcHt), rPres_(b.rPres), rNfb_(b.rNfb), rLoad_(b.rLoad),
      presenceScale_(def.power.presenceScale), presenceMinOhms_(def.power.presenceMinOhms),
      rPresBottom_(b.rPresBottom), hasPresBottom_(b.hasPresBottom),
      knobs_([&] {
          std::vector<Knob> k;
          for (std::size_t i = 0; i < def.power.knobs.size(); ++i)
              k.push_back({b.knobResistors[i], def.power.knobs[i].scale, def.power.knobs[i].minOhms});
          return k;
      }()),
      knobValues_([&] {
          std::vector<double> v;
          for (const auto& k : def.power.knobs) v.push_back(k.def);
          return v;
      }()),
      nodeX_(b.x), nodeHt_(b.ht), nodeOut_(b.out), vHt_(def.power.supply.htDcSource), dt_(1.0 / fs),
      dphase_(2.0 * kPi * def.power.supply.mainsHz / fs), hasBias_(b.hasBias), srcBias_(b.srcBias), biasNominal_(b.biasNominal),
      idleTubesPerHalf_(def.power.bias.tubesPerHalf), idleK_(1.0 - std::exp(-1.0 / (0.3 * fs))) {
    for (const auto& p : b.idle) idleProbes_.push_back({p.first.first, p.first.second, p.second});
    if (!solver_.dcConverged) throw std::runtime_error("power amp DC operating point did not converge");
    // Signals here are volts to tens of volts, so a looser tolerance is inaudible (-125 dB).
    solver_.tolerance = def.power.tolerance;
    hasPres_ = b.hasPres;
    setPresence(def.power.presenceDefault);
    // Let the reservoir, bias and filter caps settle onto the real rectified waveform.
    for (std::size_t i = 0, n = std::size_t(supply_.settleSeconds * fs); i < n; ++i) process(0.0);
}

PowerAmp::PowerAmp(const AmpDef& def, double fs, bool useTables) : PowerAmp(build(def), def, fs, useTables) {}

void PowerAmp::setKnobs(const std::vector<double>& values) {
    if (values.size() != knobs_.size()) throw std::invalid_argument("wrong number of power knob values");
    if (values == knobValues_) return;
    knobValues_ = values;
    for (std::size_t i = 0; i < knobs_.size(); ++i)
        solver_.setResistor(knobs_[i].resistor, std::max(knobs_[i].scale * std::clamp(values[i], 0.0, 1.0), knobs_[i].minOhms));
    solver_.refresh();
}

void PowerAmp::setPresence(double p) {
    presence_ = std::clamp(p, 0.0, 1.0);
    if (!hasPres_) return;
    const double top = std::max(presenceScale_ * (1.0 - presence_), presenceMinOhms_);
    solver_.setResistor(rPres_, top);
    if (hasPresBottom_) solver_.setResistor(rPresBottom_, std::max(presenceScale_ - top, presenceMinOhms_));
    solver_.refresh();
}

double PowerAmp::process(double vin) {
    const double s = std::sin(phase_);
    phase_ += dphase_;
    if (phase_ > 2.0 * kPi) phase_ -= 2.0 * kPi;
    // Reservoir: charged through the bridge while |v_ac| exceeds it, drained by the load.
    double iRect = 0.0;
    if (supply_.tubeVolts > 0.0) {
        // Tube rectifier: headroom = i * Rs + a * i^(2/3). With u = i^(1/3) this is the convex cubic
        // Rs u^3 + a u^2 = headroom, which Newton solves monotonically from any point right of the root.
        const double headroom = supply_.vacPeak * std::abs(s) - vHt_;
        if (headroom > 0.0) {
            const double a = supply_.tubeVolts / std::pow(supply_.tubeAmps, 2.0 / 3.0), rs = supply_.rectSourceOhms;
            double u = std::min(std::sqrt(headroom / a), std::cbrt(headroom / rs));
            for (int k = 0; k < 4; ++k) {
                const double f = (rs * u + a) * u * u - headroom;
                u -= f / ((3.0 * rs * u + 2.0 * a) * u);
            }
            iRect = u * u * u;
        }
    } else {
        iRect = std::max((supply_.vacPeak * std::abs(s) - supply_.rectDrop - vHt_) / supply_.rectSourceOhms, 0.0);
    }
    const double iLoad = (vHt_ - solver_.voltage(nodeHt_)) / supply_.tieOhms;
    vHt_ += dt_ / supply_.reservoirFarads * (iRect - iLoad);
    solver_.setSource(srcHt_, vHt_);
    solver_.setSource(srcAudio_, vin);
    solver_.step();
    if (!idleProbes_.empty()) {
        double amps = 0.0;
        for (const auto& p : idleProbes_) amps += (solver_.voltage(p.b) - solver_.voltage(p.a)) / p.ohms;  // from the centre tap through the primary half to the plate
        const double mA = 1000.0 * amps / (double(idleProbes_.size()) * idleTubesPerHalf_);
        idleMa_ += idleK_ * (mA - idleMa_);
    }
    return solver_.voltage(nodeOut_);
}

}  // namespace sas
