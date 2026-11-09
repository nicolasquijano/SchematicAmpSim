// sas_node_trace <amp id> <input peak volts> <node> ...: for the preamp's first stage (the main netlist; `2:k5` is node k5 of the second stage), a 220 Hz sine in, and for each node: the DC voltage, the AC peak and the
// harmonic distortion there (%, 2nd..8th against the fundamental). The knobs are at their defaults, the selectors at their first positions.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "sas/embedded_amps.h"
#include "sas/netlist.h"
#include "sas/preamp.h"

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;
    const sas::AmpDef def = sas::loadEmbeddedAmp(argv[1]);
    const double peak = std::atof(argv[2]);
    sas::Preamp pre(def, kFs, 400.0);
    for (std::size_t i = 0; i < def.preamp.knobs.size(); ++i) pre.setKnob(i, def.preamp.knobs[i].def);
    if (const char* sel = std::getenv("THD_SEL")) {  // "1,0,0": the selectors' positions
        std::size_t k = 0;
        for (const char* c = sel; *c;) {
            pre.setSelector(k++, std::size_t(std::atoi(c)));
            while (*c && *c != ',') ++c;
            if (*c == ',') ++c;
        }
    }
    std::vector<sas::Node> nodes;
    std::vector<std::size_t> stageOf;
    std::vector<sas::ParsedNetlist> nets;
    nets.push_back(sas::parseNetlist(def.preamp.netlistText, def.preamp.netlist));
    for (const auto& st : def.preamp.stages) nets.push_back(sas::parseNetlist(st.netlistText, st.netlist));
    for (int a = 3; a < argc; ++a) {
        const char* name = argv[a];
        std::size_t stage = 0;
        if (const char* colon = std::strchr(name, ':')) {  // "2:k5": node k5 of the third stage (the main netlist is stage 0)
            stage = std::size_t(std::atoi(name));
            name = colon + 1;
        }
        stageOf.push_back(stage);
        nodes.push_back(nets.at(stage).node(name));
    }
    const std::size_t n = std::size_t(2.0 * kFs), from = n / 2;
    std::vector<std::vector<double>> rec(nodes.size(), std::vector<double>(n));
    for (std::size_t i = 0; i < n; ++i) {
        pre.process(peak * std::sin(2 * kPi * 220.0 * double(i) / kFs));
        for (std::size_t k = 0; k < nodes.size(); ++k) rec[k][i] = pre.solver(stageOf[k]).voltage(nodes[k]);
    }
    auto bin = [&](const std::vector<double>& x, double hz) {
        double re = 0, im = 0;
        for (std::size_t i = from; i < x.size(); ++i) {
            const double w = 2 * kPi * hz * double(i) / kFs;
            re += x[i] * std::cos(w);
            im += x[i] * std::sin(w);
        }
        return 2.0 * std::sqrt(re * re + im * im) / double(x.size() - from);
    };
    for (std::size_t k = 0; k < nodes.size(); ++k) {
        double dc = 0;
        for (std::size_t i = from; i < n; ++i) dc += rec[k][i];
        dc /= double(n - from);
        const double f1 = bin(rec[k], 220.0);
        double h = 0;
        for (int m = 2; m <= 8; ++m) { const double v = bin(rec[k], 220.0 * m); h += v * v; }
        std::printf("%-8s DC %8.2f V   AC peak %9.4f V   THD %6.1f %%\n", argv[k + 3], dc, f1, 100.0 * std::sqrt(h) / (f1 + 1e-12));
    }
    return 0;
}
