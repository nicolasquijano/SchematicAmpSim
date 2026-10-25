// Development tool: loads an embedded amp and prints its DC operating point and a few
// measurements, so a freshly transcribed schematic can be sanity-checked against the drawing.
//   sas_inspect <amp id>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"
#include "sas/netlist.h"

namespace {

constexpr double kFs = 96000.0;
constexpr double kPi = 3.14159265358979323846;

double rmsOf(const std::vector<double>& v, std::size_t from) {
    double s = 0;
    for (std::size_t i = from; i < v.size(); ++i) s += v[i] * v[i];
    return std::sqrt(s / double(v.size() - from));
}

}  // namespace

static int run(int argc, char** argv);

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

static int run(int argc, char** argv) {
    const std::string id = argc > 1 ? argv[1] : "plexi_1959";
    const sas::AmpDef def = sas::loadEmbeddedAmp(id);
    std::printf("amp %s (%s)\n\n", def.id.c_str(), def.name.c_str());

    // Power amp DC point.
    sas::PowerAmp power(def, kFs);
    const auto net = sas::parseNetlist(def.power.netlistText, def.power.netlist);
    auto v = [&](const char* a, const char* b = nullptr) {
        for (const char* n : {a, b}) if (n && net.nodes.count(n)) return power.solver().voltage(net.node(n));
        return std::nan("");
    };
    std::printf("power amp, idle after settling:\n");
    std::printf("  B+ (a)           %8.1f V\n", v("a"));
    std::printf("  rail x           %8.1f V\n", v("x"));
    const double ipair = (v("a") - v("pt")) / 40.0;
    std::printf("  plate pt         %8.1f V   idle %.1f mA per tube (pair via DCR)\n", v("pt"), ipair / 2 * 1e3);
    std::printf("  output tube grid %8.1f V\n", v("g45", "g12"));
    std::printf("  screen           %8.1f V\n", v("s45", "s12"));
    std::printf("  PI plates        %8.1f / %.1f V\n", v("pa"), v("pb"));
    std::printf("  PI cathode kc    %8.1f V\n", v("kc"));
    std::printf("  bias node        %8.1f V\n", v("bias"));
    std::printf("  failures         %llu\n\n", (unsigned long long)power.solver().newtonFailures);

    // Preamp DC point at the power amp's rail.
    sas::Preamp pre(def, kFs, power.railX());
    const auto pnet = sas::parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    std::printf("preamp node voltages:\n");
    for (const auto& [name, node] : pnet.nodes) std::printf("  %-6s %8.2f V\n", name.c_str(), pre.solver().voltage(node));

    // Level through the whole amp at a few drive levels, 220 Hz, each jack, default knobs.
    std::printf("\nfull amp output (rms volts at 16 ohm) for 220 Hz at peak input:\n");
    std::vector<double> knobs;
    for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
    std::printf("  jack");
    for (double a : {0.01, 0.05, 0.2, 0.5}) std::printf("  %7.2f V", a);
    std::printf("\n");
    for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) {
        std::printf("  %-9s", def.preamp.jacks[j].name.c_str());
        for (double amp : {0.01, 0.05, 0.2, 0.5}) {
            sas::FullAmp a(def, kFs);
            a.setKnobs(knobs, def.power.presenceDefault);
            a.setJack(j);
            std::vector<double> y;
            const std::size_t n = std::size_t(0.5 * kFs);
            for (std::size_t i = 0; i < n; ++i) y.push_back(a.process(amp * std::sin(2 * kPi * 220.0 * double(i) / kFs)));
            std::printf("  %8.2f", rmsOf(y, n / 2));
        }
        std::printf("\n");
    }

    // Full open: every knob at 1 (power knobs too): how loud is the amp at its loudest?
    {
        std::vector<double> open(def.preamp.knobs.size(), 1.0), openPower(def.power.knobs.size(), 1.0);
        std::printf("\nall knobs at 1, rms volts at 16 ohm (watts = V^2/16):\n");
        for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) {
            std::printf("  %-9s", def.preamp.jacks[j].name.c_str());
            for (double amp : {0.01, 0.05, 0.2, 0.5}) {
                sas::FullAmp a(def, kFs);
                a.setKnobs(open, 0.5, openPower);
                a.setJack(j);
                std::vector<double> y;
                const std::size_t n = std::size_t(0.5 * kFs);
                for (std::size_t i = 0; i < n; ++i) y.push_back(a.process(amp * std::sin(2 * kPi * 220.0 * double(i) / kFs)));
                const double r = rmsOf(y, n / 2);
                std::printf("  %5.1f V %5.0f W", r, r * r / 16.0);
            }
            std::printf("\n");
        }
    }

    // Where does the signal go? Peak of selected nodes through the amp (first jack, default knobs).
    std::printf("\nstage peaks (volts, 220 Hz, first jack, default knobs):\n");
    const char* preNodes[] = {"p5", "v1w", "p6a", "p6b", "p7a", "k7b", "so", "k8", "p8", "mo", "y", "o"};
    const char* powNodes[] = {"pin", "ga", "pa", "pb", "g12", "g45", "pt", "t16"};
    for (double amp : {0.01, 0.5}) {
        sas::FullAmp a(def, kFs);
        a.setKnobs(knobs, def.power.presenceDefault);
        std::map<std::string, double> peak;
        const std::size_t n = std::size_t(0.5 * kFs);
        for (std::size_t i = 0; i < n; ++i) {
            a.process(amp * std::sin(2 * kPi * 220.0 * double(i) / kFs));
            if (i < n / 2) continue;
            for (const char* nm : preNodes)
                if (pnet.nodes.count(nm)) {
                    const double x = a.pre().solver().voltage(pnet.node(nm));
                    peak[std::string("pre.") + nm] = std::max(peak[std::string("pre.") + nm], std::abs(x - (peak.count(std::string("dc.") + nm) ? peak[std::string("dc.") + nm] : 0.0)));
                }
            for (const char* nm : powNodes)
                if (net.nodes.count(nm)) {
                    const double x = a.power().solver().voltage(net.node(nm));
                    peak[std::string("pow.") + nm] = std::max(peak[std::string("pow.") + nm], std::abs(x - (peak.count(std::string("dc.") + nm) ? peak[std::string("dc.") + nm] : 0.0)));
                }
        }
        std::printf("  input %.2f V peak:\n   ", amp);
        for (const auto& [k, v] : peak) std::printf(" %s=%.1f", k.c_str(), v);
        std::printf("\n");
    }
    return 0;
}
