// Development tool: loads an embedded amp and prints its DC operating point and a few
// measurements, so a freshly transcribed schematic can be sanity-checked against the drawing.
//   aes_inspect <amp id>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "aes/amp.h"
#include "aes/embedded_amps.h"
#include "aes/netlist.h"

namespace {

constexpr double kFs = 96000.0;
constexpr double kPi = 3.14159265358979323846;

double rmsOf(const std::vector<double>& v, std::size_t from) {
    double s = 0;
    for (std::size_t i = from; i < v.size(); ++i) s += v[i] * v[i];
    return std::sqrt(s / double(v.size() - from));
}

}  // namespace

int main(int argc, char** argv) {
    const std::string id = argc > 1 ? argv[1] : "plexi_1959";
    const aes::AmpDef def = aes::loadEmbeddedAmp(id);
    std::printf("amp %s (%s)\n\n", def.id.c_str(), def.name.c_str());

    // Power amp DC point.
    aes::PowerAmp power(def, kFs);
    const auto net = aes::parseNetlist(def.power.netlistText, def.power.netlist);
    auto v = [&](const char* node) { return power.solver().voltage(net.node(node)); };
    std::printf("power amp, idle after settling:\n");
    std::printf("  B+ (a)           %8.1f V\n", v("a"));
    std::printf("  rail x           %8.1f V\n", v("x"));
    const double ipair = (v("a") - v("pt")) / 40.0;
    std::printf("  plate pt         %8.1f V   idle %.1f mA per tube (pair via DCR)\n", v("pt"), ipair / 2 * 1e3);
    std::printf("  output tube grid %8.1f V (g45)\n", v("g45"));
    std::printf("  screen s45       %8.1f V\n", v("s45"));
    std::printf("  PI plates        %8.1f / %.1f V\n", v("pa"), v("pb"));
    std::printf("  PI cathode kc    %8.1f V\n", v("kc"));
    std::printf("  bias node        %8.1f V\n", v("bias"));
    std::printf("  failures         %llu\n\n", (unsigned long long)power.solver().newtonFailures);

    // Preamp DC point at the power amp's rail.
    aes::Preamp pre(def, kFs, power.railX());
    const auto pnet = aes::parseNetlist(def.preamp.netlistText, def.preamp.netlist);
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
            aes::FullAmp a(def, kFs);
            a.setKnobs(knobs, def.power.presenceDefault);
            a.setJack(j);
            std::vector<double> y;
            const std::size_t n = std::size_t(0.5 * kFs);
            for (std::size_t i = 0; i < n; ++i) y.push_back(a.process(amp * std::sin(2 * kPi * 220.0 * double(i) / kFs)));
            std::printf("  %8.2f", rmsOf(y, n / 2));
        }
        std::printf("\n");
    }
}
