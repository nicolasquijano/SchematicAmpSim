#include "sas/spring.h"

#include <algorithm>
#include <cmath>

namespace sas {

namespace {
constexpr double kPi = 3.14159265358979323846;
double onePole(double hz, double fs) { return 1.0 - std::exp(-2.0 * kPi * hz / fs); }
}  // namespace

SpringTank::SpringTank(const ReverbDef& def, double fs) : gain_(def.gain) {
    decim_ = std::max<std::size_t>(1, std::size_t(std::lround(fs / def.tankRateHz)));
    const double fsTank = fs / double(decim_);
    hpA_ = onePole(def.inputLowHz, fs);
    lpInA_ = onePole(def.inputHighHz, fs);
    lpOutA_ = onePole(def.outputHighHz, fs);
    for (const SpringDef& d : def.springs) {
        Spring s;
        s.chain.resize(d.sections);
        for (auto& ap : s.chain) ap.a = d.coefficient;
        // The chain itself delays by about sections * (1 - a) / (1 + a) samples at low frequencies.
        const double chainDelay = double(d.sections) * (1.0 - d.coefficient) / (1.0 + d.coefficient);
        const double total = d.delayMs * 1e-3 * fsTank;
        s.line.assign(std::max<std::size_t>(2, std::size_t(std::max(total - chainDelay, 2.0))), 0.0);
        s.lossA = onePole(d.lossHz, fsTank);
        s.gain = d.loopGain;
        springs_.push_back(std::move(s));
    }
}

void SpringTank::reset() {
    for (auto& s : springs_) {
        std::fill(s.line.begin(), s.line.end(), 0.0);
        for (auto& ap : s.chain) ap.s = 0.0;
        s.loss = 0.0;
    }
    hp_ = lpIn1_ = lpIn2_ = lpOut_ = acc_ = prevOut_ = lastOut_ = 0.0;
    count_ = 0;
}

double SpringTank::stepTank(double x) {
    double out = 0.0;
    for (Spring& s : springs_) {
        double f = s.line[s.pos];
        for (AllPass& ap : s.chain) f = ap.tick(f);
        s.loss += s.lossA * (f - s.loss);
        s.line[s.pos] = x + s.gain * s.loss;
        if (++s.pos >= s.line.size()) s.pos = 0;
        out += f;
    }
    return out / double(springs_.size());
}

}  // namespace sas
