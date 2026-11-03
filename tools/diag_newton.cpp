// Why does Newton take many iterations on an amp? Runs the amp exactly as the Normal mode builds it on the benchmark's input and reports,
// for each solver (preamp stages, power amp): iterations per sample (histogram), how often the step had to be shortened, continuation
// runs, and - for the samples that needed many iterations - which device port kept the largest residual (devices in netlist order; ports:
// triode 0 = grid-cathode, 1 = plate-cathode; pentode 0 = grid, 1 = plate, 2 = screen).
//   sas_diag_newton <amp id> [case: clean|crunch|lead|lead-hot] [threshold iterations, default 8]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"

namespace {

constexpr double kFs = 96000.0, kPi = 3.14159265358979323846;

std::vector<double> signal(double amp, std::size_t n) {
    std::uint64_t state = 0x123456789abcdef0ull;
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const double noise = double(state >> 11) / double(1ull << 53) - 0.5;
        const double t = double(i) / kFs;
        const double s = 0.6 * std::sin(2 * kPi * 110.0 * t) + 0.3 * std::sin(2 * kPi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * kPi * 1234.5 * t);
        v[i] = amp * (s + 0.02 * noise);
    }
    return v;
}

// The nonlinear device lines of a netlist, in order (they are the solver's devices in the same order).
std::vector<std::string> deviceLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const char c = line[0];
        if (c == 'T' || c == 'P' || c == 'D' || c == 'Q' || c == 'O' || c == 'N' || c == 'A') {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            out.push_back(line);
        }
    }
    return out;
}

struct Stats {
    const sas::Solver* solver = nullptr;
    std::vector<std::string> devices;
    std::size_t itThisSample = 0;
    std::map<std::size_t, std::uint64_t> histogram;  // iterations per sample -> samples
    std::uint64_t shortened = 0, steps = 0, rejected = 0;
    std::map<std::size_t, std::uint64_t> worst;      // for hard samples: device*8+port -> iterations where it had the largest residual
    std::vector<std::size_t> worstThisSample;
    std::vector<std::string> log;  // this sample's iterations, printed for the first hard sample (SAS_DUMP=1)
    double minVpk = 1e9;
    std::uint64_t lowPlate = 0, plateSamples = 0;
};

void onTrace(void* user, const sas::Solver::NewtonTrace& t) {
    auto& s = *static_cast<Stats*>(user);
    ++s.itThisSample;
    ++s.steps;
    if (!t.accepted) ++s.rejected;
    else if (t.lambda < 0.999) ++s.shortened;
    const std::size_t dev = s.solver->portDevice(t.worstPort);
    s.worstThisSample.push_back(dev * 8 + (t.worstPort - s.solver->devicePortBase(dev)));
    if (std::getenv("SAS_DUMP")) {
        char buf[512];
        int k = std::snprintf(buf, sizeof buf, "it %2zu lambda %.3g fmax %.3g worst dev %zu port %zu |", t.iteration, t.lambda, t.fmax, dev,
                              t.worstPort - s.solver->devicePortBase(dev));
        for (std::size_t d = 0; s.solver->deviceKind(d) >= 0 && k < 480; ++d) {
            const std::size_t b = s.solver->devicePortBase(d);
            if (s.solver->deviceKind(d) == 1) k += std::snprintf(buf + k, sizeof buf - k, " P%zu g%.1f p%.0f s%.0f", d, t.x[b], t.x[b + 1], t.x[b + 2]);
            else if (s.solver->deviceKind(d) == 0) k += std::snprintf(buf + k, sizeof buf - k, " T%zu g%.2f p%.0f", d, t.x[b], t.x[b + 1]);
        }
        s.log.push_back(buf);
    }
    // Plate-cathode voltage of every output pentode at each iteration (kind 1 = pentode, its port 1).
    for (std::size_t d = 0; s.solver->deviceKind(d) >= 0; ++d)
        if (s.solver->deviceKind(d) == 1) {
            const double vpk = t.x[s.solver->devicePortBase(d) + 1];
            s.minVpk = std::min(s.minVpk, vpk);
            if (vpk < 5.0) ++s.lowPlate;
            ++s.plateSamples;
        }
}

}  // namespace

// Experiments: SAS_PLAIN, SAS_FINAL, SAS_CONT_STAGE, SAS_CONT_BUDGET override the solver's work limits.
static void applyLimits(sas::Solver& s, bool power) {
    if (const char* v = std::getenv("SAS_PLAIN")) s.plainIterations = std::size_t(std::atoi(v));
    if (const char* v = std::getenv("SAS_FINAL")) s.finalIterations = std::size_t(std::atoi(v));
    if (const char* v = std::getenv("SAS_CONT_STAGE")) s.contStageIters = std::size_t(std::atoi(v));
    if (const char* v = std::getenv("SAS_CONT_BUDGET")) s.contBudget = std::atoi(v);
    if (const char* v = std::getenv(power ? "SAS_TOL_PWR" : "SAS_TOL_PRE")) s.tolerance *= std::atof(v);  // a scale on the amp's own tolerance
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: sas_diag_newton <amp id> [clean|crunch|lead|lead-hot] [threshold]\n");
        return 1;
    }
    const std::string id = argv[1];
    const std::string which = argc > 2 ? argv[2] : "lead";
    const std::size_t threshold = argc > 3 ? std::size_t(std::atoi(argv[3])) : 8;
    struct Case { const char* name; double volume, master, amp; };
    const Case cases[] = {{"clean", 0.2, 0.5, 0.05}, {"crunch", 0.6, 0.5, 0.15}, {"lead", 1.0, 0.8, 0.12}, {"lead-hot", 1.0, 0.8, 0.3}};
    const Case* c = &cases[2];
    for (const auto& k : cases)
        if (which == k.name) c = &k;

    sas::AmpDef def = sas::loadEmbeddedAmp(id);
    if (std::getenv("SAS_NO_PARASITICS")) def.power.transformer.enabled = false;  // experiment: without the winding capacitance and core loss
    if (const char* pf = std::getenv("SAS_PARASITIC_PF")) def.power.transformer.primaryPf = std::atof(pf);
    std::vector<double> knobs;
    for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
    knobs[0] = c->volume;
    for (std::size_t i = 0; i < knobs.size(); ++i)
        if (def.preamp.knobs[i].name == "master") knobs[i] = c->master;

    sas::FullAmp amp(def, kFs, false, true);  // the Normal mode
    amp.setKnobs(knobs, def.power.presenceDefault);

    std::vector<Stats> stats(amp.pre().stageCount() + 1);
    std::vector<std::string> names;
    for (std::size_t s = 0; s < amp.pre().stageCount(); ++s) {
        stats[s].solver = &amp.pre().solver(s);
        stats[s].devices = deviceLines(s == 0 ? def.preamp.netlistText : def.preamp.stages[s - 1].netlistText);
        names.push_back("preamp stage " + std::to_string(s + 1));
    }
    stats.back().solver = &amp.power().solver();
    stats.back().devices = deviceLines(def.power.netlistText);
    names.push_back("power amp");
    for (auto& st : stats) {
        auto* solver = const_cast<sas::Solver*>(st.solver);
        applyLimits(*solver, &st == &stats.back());
        solver->trace = onTrace;
        solver->traceUser = &st;
    }

    const std::size_t n = std::size_t(kFs * 2.0);
    const auto in = signal(c->amp, n);
    for (std::size_t i = 0; i < n; ++i) {
        for (auto& s : stats) { s.itThisSample = 0; s.worstThisSample.clear(); s.log.clear(); }
        amp.process(in[i]);
        for (auto& s : stats) {
            ++s.histogram[s.itThisSample];
            if (s.itThisSample >= threshold)
                for (auto d : s.worstThisSample) ++s.worst[d];
            static int dumped = 0;
            if (std::getenv("SAS_DUMP") && &s == &stats.back() && i > 30000 && s.itThisSample >= 30 && dumped < 1) {
                ++dumped;
                std::printf("---- sample %zu, %zu iterations:%c", i, s.itThisSample, 10);
                for (const auto& line : s.log) std::printf("%s%c", line.c_str(), 10);
            }
        }
    }

    std::printf("%s, case %s (input %.2f V peak), %zu samples at 96 kHz, Normal mode\n", id.c_str(), c->name, c->amp, n);
    for (std::size_t s = 0; s < stats.size(); ++s) {
        const auto& st = stats[s];
        std::printf("\n== %s: %zu ports, %.2f iterations/sample, %llu continuation runs, %llu failures\n", names[s].c_str(), st.solver->portCount(),
                    double(st.steps) / double(n), (unsigned long long)st.solver->continuationRuns, (unsigned long long)st.solver->newtonFailures);
        std::printf("   steps shortened: %.1f %%, rejected: %.1f %%\n", 100.0 * double(st.shortened) / double(std::max<std::uint64_t>(1, st.steps)),
                    100.0 * double(st.rejected) / double(std::max<std::uint64_t>(1, st.steps)));
        if (st.plateSamples)
            std::printf("   output pentodes: lowest plate-cathode voltage seen %.1f V; below 5 V in %.2f %% of the iterations%c", st.minVpk,
                        100.0 * double(st.lowPlate) / double(st.plateSamples), 10);
        std::printf("   iterations per sample:");
        for (const auto& [k, v] : st.histogram)
            if (100.0 * double(v) / double(n) >= 0.05) std::printf("  %zu:%.1f%%", k, 100.0 * double(v) / double(n));
        std::printf("\n");
        if (!st.worst.empty()) {
            std::printf("   on samples with %zu+ iterations, the device port with the largest residual:\n", threshold);
            std::uint64_t total = 0;
            for (const auto& [d, v] : st.worst) total += v;
            for (const auto& [d, v] : st.worst) {
                if (100.0 * double(v) / double(total) < 2.0) continue;
                const std::size_t dev = d / 8, port = d % 8;
                const std::string line = dev < st.devices.size() ? st.devices[dev] : std::string("?");
                std::printf("     %5.1f %%  device %zu port %zu: %s\n", 100.0 * double(v) / double(total), dev, port, line.c_str());
            }
        }
    }
}
