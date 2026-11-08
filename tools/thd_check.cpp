// sas_thd_check <amp id> ...: total harmonic distortion (2nd..8th harmonic against the fundamental, %) of the chain's output for a 220 Hz sine at 10, 30 and 100 mV
// peak, with every knob at its default, the first jack and the selectors' first positions, plus the even / odd split at 30 mV.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>
#include <vector>

#include "sas/chain.h"
#include "sas/embedded_amps.h"
#include "sas/preamp.h"
#include <cstdlib>

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

double bin(const std::vector<float>& x, std::size_t from, double hz) {
    double re = 0, im = 0;
    for (std::size_t i = from; i < x.size(); ++i) {
        const double w = 2 * kPi * hz * double(i) / kFs;
        re += double(x[i]) * std::cos(w);
        im += double(x[i]) * std::sin(w);
    }
    return std::sqrt(re * re + im * im);
}

static double gPeakDb = 0, gRmsDb = 0;
void thd(const sas::AmpDef& def, double peak, double& total, double& even, double& odd) {
    sas::Chain chain(def, kFs);
    sas::ChainSettings s;
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    if (const char* kn = std::getenv("THD_KNOB")) {  // "lead_gain=0.4,master=0.5": knob positions 0..1
        for (const char* c = kn; *c;) {
            const char* eq = std::strchr(c, '=');
            if (!eq) break;
            const std::string name(c, eq);
            const double v = std::atof(eq + 1);
            for (std::size_t i = 0; i < def.preamp.knobs.size(); ++i)
                if (def.preamp.knobs[i].name == name) s.knobs[i] = v;
            c = std::strchr(eq, ',') ? std::strchr(eq, ',') + 1 : eq + std::strlen(eq);
        }
    }
    for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
    s.presence = def.power.presenceDefault;
    s.jack = std::getenv("THD_JACK") ? std::size_t(std::atoi(std::getenv("THD_JACK"))) : 0;
    if (const char* sel = std::getenv("THD_SEL")) {  // "1,0,0": the selectors' positions
        for (const char* c = sel; *c;) {
            s.selectors.push_back(std::size_t(std::atoi(c)));
            while (*c && *c != ',') ++c;
            if (*c == ',') ++c;
        }
    }
    s.cabFilter = false;
    chain.snap(s);
    const std::size_t n = std::size_t(2.0 * kFs);
    std::vector<float> in(n), out(n);
    for (std::size_t i = 0; i < n; ++i) in[i] = float(peak * std::sin(2 * kPi * 220.0 * double(i) / kFs));
    chain.processAmp(in.data(), out.data(), n);
    const std::size_t from = n / 2;
    {
        double pk = 0, ac = 0;
        for (std::size_t i = from; i < n; ++i) { pk = std::max(pk, std::abs(double(out[i]))); ac += double(out[i]) * out[i]; }
        gPeakDb = 20 * std::log10(pk + 1e-12);
        gRmsDb = 10 * std::log10(ac / double(n - from) + 1e-24);
    }
    const double f1 = bin(out, from, 220.0);
    double e = 0, o = 0;
    for (int h = 2; h <= 8; ++h) {
        const double v = bin(out, from, 220.0 * h);
        (h % 2 ? o : e) += v * v;
    }
    total = 100.0 * std::sqrt(e + o) / f1;
    even = 100.0 * std::sqrt(e) / f1;
    odd = 100.0 * std::sqrt(o) / f1;
}
// The preamp alone (THD_PRE=1): the same sine, the output node's voltage.
void thdPre(const sas::AmpDef& def, double peak, double& total) {
    sas::Preamp pre(def, kFs, 400.0);
    for (std::size_t i = 0; i < def.preamp.knobs.size(); ++i) pre.setKnob(i, def.preamp.knobs[i].def);
    const std::size_t n = std::size_t(2.0 * kFs);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = float(pre.process(peak * std::sin(2 * kPi * 220.0 * double(i) / kFs)));
    const std::size_t from = n / 2;
    const double f1 = bin(out, from, 220.0);
    double e = 0;
    for (int h = 2; h <= 8; ++h) { const double v = bin(out, from, 220.0 * h); e += v * v; }
    total = 100.0 * std::sqrt(e) / f1;
}
}  // namespace

int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        const sas::AmpDef def = sas::loadEmbeddedAmp(argv[a]);
        std::printf("%-16s THD %% at 10 / 30 / 100 mV:", argv[a]);
        const bool hot = std::getenv("THD_HOT") != nullptr;
        if (std::getenv("THD_PRE")) {
            double t;
            for (double p : {0.01, 0.03, 0.1}) { thdPre(def, p, t); std::printf(" %6.1f", t); }
            std::printf("   (preamp alone)\n");
            continue;
        }
        double t, e, o, e30 = 0, o30 = 0;
        if (hot) {
            std::printf("  (hot: sine peak 0.13 / 0.30 / 0.55 V -> THD %%, out peak dBFS)");
            for (double p : {0.13, 0.30, 0.55}) { thd(def, p, t, e, o); std::printf("  %.0f%%, %+.1f dB", t, gPeakDb); }
            std::printf("\n");
            continue;
        }
        for (double p : {0.01, 0.03, 0.1}) {
            thd(def, p, t, e, o);
            std::printf(" %6.1f", t);
            if (p == 0.03) { e30 = e; o30 = o; }
        }
        std::printf("   (30 mV: even %.1f, odd %.1f)\n", e30, o30);
    }
    return 0;
}
