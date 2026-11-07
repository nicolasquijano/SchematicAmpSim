// Cost of each solver of an amp (preamp stages): nodes, ports, Newton iterations and cycles per circuit sample, in the Normal
// mode (tables, 96 kHz). It runs the stages the way Preamp::process does but times each one apart. Used to estimate what splitting a big
// preamp into stages would save (the cost of a stage depends on its size; see docs/PLAN_CPU.md).
//   sas_stage_cost [amp id ...]     (no ids: every embedded amp)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <intrin.h>

#include "sas/amp.h"
#include "sas/embedded_amps.h"
#include "sas/netlist.h"

namespace {
constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;
}

int main(int argc, char** argv) {
    std::vector<std::string> ids;
    for (int i = 1; i < argc; ++i) ids.push_back(argv[i]);
    if (ids.empty()) ids = sas::embeddedAmpIds();
    const char* mode = std::getenv("SAS_CASE");  // crunch (default) or lead-hot
    const double amp = mode && std::string(mode) == "lead-hot" ? 0.3 : 0.15;
    std::printf("%-16s %-10s %5s %5s %8s %10s\n", "amp", "solver", "nodes", "ports", "it/samp", "cycles");
    for (const auto& id : ids) {
        sas::AmpDef def = sas::loadEmbeddedAmp(id);
        if (def.preamp.knobs.empty()) continue;
        sas::FullAmp full(def, kFs, false, true);
        std::vector<double> knobs;
        for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
        knobs[0] = 0.6;
        for (std::size_t i = 0; i < knobs.size(); ++i)
            if (def.preamp.knobs[i].name == "master") knobs[i] = 0.5;
        full.setKnobs(knobs, def.power.presenceDefault);

        auto& pre = full.pre();
        const std::size_t ns = pre.stageCount();
        struct L { std::size_t from; sas::SrcId src; sas::Node node; };
        std::vector<std::vector<L>> links(ns);
        std::vector<sas::ParsedNetlist> nets;
        nets.push_back(sas::parseNetlist(def.preamp.netlistText, "main"));
        for (const auto& sd : def.preamp.stages) nets.push_back(sas::parseNetlist(sd.netlistText, "stage"));
        std::vector<bool> fed(ns, false);
        fed[0] = true;
        for (std::size_t k = 1; k < ns; ++k) {
            const auto& sd = def.preamp.stages[k - 1];
            fed[k] = sd.input;
            if (!sd.linkSource.empty()) links[k].push_back({k - 1, nets[k].source(sd.linkSource), nets[k - 1].node(sd.linkFrom)});
            for (const auto& l : sd.links) links[k].push_back({l.stage, nets[k].source(l.source), nets[l.stage].node(l.from)});
        }
        const std::size_t warm = std::size_t(kFs * 0.5), count = std::size_t(kFs * 1.5);
        std::vector<std::uint64_t> cyc(ns, 0), it0(ns, 0), it1(ns, 0);
        std::uint64_t st = 0x123456789abcdef0ull;
        for (std::size_t i = 0; i < warm + count; ++i) {
            st = st * 6364136223846793005ull + 1442695040888963407ull;
            const double noise = double(st >> 11) / double(1ull << 53) - 0.5, t = double(i) / kFs;
            const double vin = amp * (0.6 * std::sin(2 * kPi * 110.0 * t) + 0.3 * std::sin(2 * kPi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * kPi * 1234.5 * t) + 0.02 * noise);
            const bool measure = i >= warm;
            if (i == warm) {
                for (std::size_t k = 0; k < ns; ++k) it0[k] = pre.solver(k).newtonIterations;
            }
            pre.setRail(full.power().railX());
            std::uint64_t t0 = __rdtsc();
            pre.solver(0).process(vin);
            std::uint64_t t1 = __rdtsc();
            if (measure) cyc[0] += t1 - t0;
            for (std::size_t k = 1; k < ns; ++k) {
                auto& s = pre.solver(k);
                for (const auto& l : links[k]) s.setSource(l.src, pre.solver(l.from).voltage(l.node));
                t0 = __rdtsc();
                if (fed[k]) s.process(vin); else s.step();
                t1 = __rdtsc();
                if (measure) cyc[k] += t1 - t0;
            }
        }
        for (std::size_t k = 0; k < ns; ++k) it1[k] = pre.solver(k).newtonIterations;
        for (std::size_t k = 0; k < ns; ++k) {
            const auto& s = pre.solver(k);
            std::printf("%-16s preamp %-3zu %5zu %5zu %8.2f %10.0f\n", id.c_str(), k + 1, s.nodeCount(), s.portCount(), double(it1[k] - it0[k]) / double(count),
                        double(cyc[k]) / double(count));
        }
    }
    return 0;
}
